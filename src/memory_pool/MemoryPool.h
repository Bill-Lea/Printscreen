#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <functional>

// Forward declaration for RAII buffer
class MemoryPool;

// ============================================================================
// PoolBuffer - RAII wrapper for pool-allocated memory
// ============================================================================
// Automatically returns memory to the pool when it goes out of scope.
// Movable but not copyable, just like unique_ptr.
//
// Usage:
//   auto buf = pool.allocateBuffer(frameSize);
//   memcpy(buf.data(), source, frameSize);
//   // buf is automatically returned to the pool when destroyed
// ============================================================================

class PoolBuffer {
public:
    PoolBuffer() noexcept : ptr_(nullptr), size_(0), pool_(nullptr) {}

    // Move constructor
    PoolBuffer(PoolBuffer&& other) noexcept
        : ptr_(other.ptr_), size_(other.size_), pool_(other.pool_) {
        other.ptr_ = nullptr;
        other.size_ = 0;
        other.pool_ = nullptr;
    }

    // Move assignment
    PoolBuffer& operator=(PoolBuffer&& other) noexcept {
        if (this != &other) {
            release();
            ptr_ = other.ptr_;
            size_ = other.size_;
            pool_ = other.pool_;
            other.ptr_ = nullptr;
            other.size_ = 0;
            other.pool_ = nullptr;
        }
        return *this;
    }

    // Non-copyable
    PoolBuffer(const PoolBuffer&) = delete;
    PoolBuffer& operator=(const PoolBuffer&) = delete;

    ~PoolBuffer() { release(); }

    // Access
    uint8_t* data() noexcept { return ptr_; }
    const uint8_t* data() const noexcept { return ptr_; }
    size_t size() const noexcept { return size_; }
    bool valid() const noexcept { return ptr_ != nullptr; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    // Manually release back to pool early
    void release();

private:
    friend class MemoryPool;
    PoolBuffer(uint8_t* ptr, size_t size, MemoryPool* pool) noexcept
        : ptr_(ptr), size_(size), pool_(pool) {}

    uint8_t* ptr_;
    size_t size_;
    MemoryPool* pool_;
};


// ============================================================================
// MemoryPool - Thread-safe block allocator for reusable frame buffers
// ============================================================================
// Designed for the repeated allocate/deallocate pattern in animated capture
// loops (GIF, APNG). Blocks are kept and reused rather than freed, which
// avoids the overhead of repeated OS-level allocations for large buffers
// (e.g. 8MB per full-resolution BGRA frame).
//
// Thread safety: All public methods are mutex-protected.
// ============================================================================

class MemoryPool {
private:
    struct PoolBlock {
        std::unique_ptr<uint8_t[]> data;
        size_t size;
        bool inUse;

        PoolBlock(size_t blockSize) : size(blockSize), inUse(false) {
            data = std::make_unique<uint8_t[]>(blockSize);
        }

        // Movable for vector storage
        PoolBlock(PoolBlock&&) noexcept = default;
        PoolBlock& operator=(PoolBlock&&) noexcept = default;
    };

    std::vector<PoolBlock> blocks;
    std::mutex poolMutex;
    std::atomic<size_t> totalAllocated{0};
    std::atomic<size_t> totalUsed{0};
    std::atomic<size_t> allocationCount{0};
    std::atomic<size_t> reuseCount{0};

    // Maximum ratio of total allocated to total used before triggering a warning
    static constexpr size_t kMaxBlockCount = 256;

public:
    // Default block size: 1MB. Pre-allocates nothing; blocks are created on demand.
    MemoryPool(size_t initialBlockSize = 1024 * 1024);
    ~MemoryPool();

    // ---- Raw allocation (prefer allocateBuffer for RAII) ----
    uint8_t* allocate(size_t size);
    void deallocate(uint8_t* ptr);

    // ---- RAII allocation (preferred) ----
    // Returns a PoolBuffer that auto-returns memory to the pool on destruction.
    PoolBuffer allocateBuffer(size_t size);

    // ---- Pool management ----

    // Release ALL blocks (used and unused). Call between capture operations
    // when you know nothing is still referencing pool memory.
    void reset();

    // Release only unused blocks to reclaim OS memory. Safe to call at any time.
    void trim();

    // ---- Statistics ----
    size_t getPoolSize() const;       // Total bytes allocated by the pool
    size_t getUsedSize() const;       // Bytes currently in use
    size_t getBlockCount() const;     // Total number of blocks
    size_t getFreeBlockCount() const; // Number of available (unused) blocks
    size_t getAllocationCount() const; // Total allocations since creation/reset
    size_t getReuseCount() const;     // Times an existing block was reused

    // Log current pool statistics (requires logger.h)
    void logStats(const char* context = nullptr) const;
};
