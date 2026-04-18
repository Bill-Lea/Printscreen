#include "MemoryPool.h"
#include "logger.h"
#include <algorithm>

// ============================================================================
// PoolBuffer implementation
// ============================================================================

void PoolBuffer::release() {
    if (ptr_ && pool_) {
        pool_->deallocate(ptr_);
        ptr_ = nullptr;
        size_ = 0;
        pool_ = nullptr;
    }
}


// ============================================================================
// MemoryPool implementation
// ============================================================================

MemoryPool::MemoryPool(size_t /*initialBlockSize*/) {
    // No pre-allocation. Blocks are created on demand to avoid wasting memory
    // when capture formats vary in buffer size requirements.
    logger::debug("MemoryPool created (on-demand allocation)");
}

MemoryPool::~MemoryPool() {
    std::lock_guard<std::mutex> lock(poolMutex);

    size_t inUseCount = 0;
    for (const auto& block : blocks) {
        if (block.inUse) ++inUseCount;
    }

    if (inUseCount > 0) {
        logger::warn("MemoryPool destroyed with {} block(s) still in use!", inUseCount);
    }

    logger::debug("MemoryPool destroyed: {} blocks, {} bytes total allocated",
                  blocks.size(), totalAllocated.load());
    blocks.clear();
}

uint8_t* MemoryPool::allocate(size_t size) {
    if (size == 0) {
        logger::warn("MemoryPool::allocate called with size 0");
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(poolMutex);

    // Strategy: find the smallest free block that is >= requested size.
    // This avoids wasting a 32MB block on a 1KB request.
    PoolBlock* bestFit = nullptr;

    for (auto& block : blocks) {
        if (!block.inUse && block.size >= size) {
            if (!bestFit || block.size < bestFit->size) {
                bestFit = &block;
                // Perfect match - no need to keep looking
                if (block.size == size) break;
            }
        }
    }

    if (bestFit) {
        bestFit->inUse = true;
        totalUsed += bestFit->size;
        reuseCount.fetch_add(1, std::memory_order_relaxed);
        allocationCount.fetch_add(1, std::memory_order_relaxed);

        logger::trace("MemoryPool: reused block ({} bytes, requested {})",
                      bestFit->size, size);
        return bestFit->data.get();
    }

    // No suitable block found - create a new one
    if (blocks.size() >= kMaxBlockCount) {
        logger::error("MemoryPool: block limit reached ({} blocks). "
                      "Possible leak - check that all PoolBuffers are being released.",
                      kMaxBlockCount);
        // Still allocate, but this warning indicates a problem
    }

    blocks.emplace_back(size);
    blocks.back().inUse = true;
    totalAllocated += size;
    totalUsed += size;
    allocationCount.fetch_add(1, std::memory_order_relaxed);

    logger::trace("MemoryPool: new block allocated ({} bytes, pool total: {} bytes)",
                  size, totalAllocated.load());
    return blocks.back().data.get();
}

void MemoryPool::deallocate(uint8_t* ptr) {
    if (!ptr) return;

    std::lock_guard<std::mutex> lock(poolMutex);

    for (auto& block : blocks) {
        if (block.data.get() == ptr) {
            if (!block.inUse) {
                logger::warn("MemoryPool: double-free detected for block at {:p}", 
                             static_cast<void*>(ptr));
                return;
            }
            block.inUse = false;
            totalUsed -= block.size;
            logger::trace("MemoryPool: block returned ({} bytes)", block.size);
            return;
        }
    }

    logger::error("MemoryPool: deallocate called with unknown pointer {:p} - "
                  "this pointer was not allocated by this pool!",
                  static_cast<void*>(ptr));
}

PoolBuffer MemoryPool::allocateBuffer(size_t size) {
    uint8_t* ptr = allocate(size);
    if (!ptr) {
        return PoolBuffer(); // empty/invalid
    }
    return PoolBuffer(ptr, size, this);
}

void MemoryPool::reset() {
    std::lock_guard<std::mutex> lock(poolMutex);

    size_t inUseCount = 0;
    for (const auto& block : blocks) {
        if (block.inUse) ++inUseCount;
    }

    if (inUseCount > 0) {
        logger::warn("MemoryPool::reset() called while {} block(s) still in use! "
                     "Outstanding PoolBuffers will become dangling.", inUseCount);
    }

    size_t prevSize = totalAllocated.load();
    blocks.clear();
    totalAllocated.store(0);
    totalUsed.store(0);
    allocationCount.store(0);
    reuseCount.store(0);

    logger::debug("MemoryPool: reset (freed {} bytes across blocks)", prevSize);
}

void MemoryPool::trim() {
    std::lock_guard<std::mutex> lock(poolMutex);

    size_t freedBytes = 0;
    size_t freedBlocks = 0;

    // Remove unused blocks using erase-remove idiom
    auto it = std::remove_if(blocks.begin(), blocks.end(),
        [&](const PoolBlock& block) {
            if (!block.inUse) {
                freedBytes += block.size;
                ++freedBlocks;
                return true;
            }
            return false;
        });
    blocks.erase(it, blocks.end());

    totalAllocated -= freedBytes;

    if (freedBlocks > 0) {
        logger::debug("MemoryPool: trimmed {} unused block(s), freed {} bytes",
                      freedBlocks, freedBytes);
    }
}

size_t MemoryPool::getPoolSize() const {
    return totalAllocated.load();
}

size_t MemoryPool::getUsedSize() const {
    return totalUsed.load();
}

size_t MemoryPool::getBlockCount() const {
    // This is a rough count; not locked, but atomic reads of the vector size
    // aren't possible, so this is approximate. For logging only.
    return blocks.size();
}

size_t MemoryPool::getFreeBlockCount() const {
    // Approximate - no lock. For stats/logging only.
    size_t count = 0;
    for (const auto& block : blocks) {
        if (!block.inUse) ++count;
    }
    return count;
}

size_t MemoryPool::getAllocationCount() const {
    return allocationCount.load();
}

size_t MemoryPool::getReuseCount() const {
    return reuseCount.load();
}

void MemoryPool::logStats(const char* context) const {
    const char* ctx = context ? context : "MemoryPool stats";
    size_t poolSz = totalAllocated.load();
    size_t usedSz = totalUsed.load();
    size_t allocs = allocationCount.load();
    size_t reuses = reuseCount.load();

    logger::info("{}: pool={} KB, used={} KB, blocks={}, allocs={}, reuses={}",
                 ctx, poolSz / 1024, usedSz / 1024,
                 blocks.size(), allocs, reuses);

    if (allocs > 0) {
        float reusePercent = (static_cast<float>(reuses) / static_cast<float>(allocs)) * 100.0f;
        logger::info("  Reuse rate: {:.1f}%", reusePercent);
    }
}
