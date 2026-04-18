# MemoryPool Integration for PrintScreen V3

## Overview

This package contains the finished MemoryPool class and its integration into the
PrintScreen V3 SKSE plugin. The pool provides reusable memory blocks for large
frame buffers used during animated captures (APNG, GIF), avoiding repeated
OS-level `new[]` / `delete[]` calls for multi-megabyte buffers.

## Files Modified

### New Files
- `memory_pool/MemoryPool.h` — Complete MemoryPool class with RAII `PoolBuffer` wrapper
- `memory_pool/MemoryPool.cpp` — Full implementation with best-fit allocation, stats, trim

### Modified Files
- `screencapture.h` — Added `GetMemoryPool()` and `ResetMemoryPool()` declarations
- `screencapture.cpp` — Integrated pool for APNG differential encoding buffer,
  added pool stats logging after animated captures, pool trim on completion
- `PapyrusInterface.cpp` — Added `ResetMemoryPool()` call in `ForceReset()` to
  reclaim idle pool memory on state reset

### Unchanged Files
- `PapyrusInterface.h` — No changes needed
- `Config.h` — No changes needed

## What Changed vs. the Original Partial Integration

The original code declared `static MemoryPool g_memoryPool(1024 * 1024);` in
`screencapture.cpp` and `#include`d the header, but **never actually used the pool
for any allocations**. All frame buffers still used `std::vector<uint8_t>`.

This finished integration:

1. **Rewrote MemoryPool class** — The original pre-allocated 10×1MB blocks on
   construction (wasting 10MB even for single-frame PNG captures). The new version
   uses on-demand allocation with best-fit block selection.

2. **Added `PoolBuffer` RAII wrapper** — Callers get a move-only handle that
   automatically returns memory to the pool when it goes out of scope. No manual
   `deallocate()` needed, no leak risk.

3. **Integrated into APNG differential encoding** — The `prevFrameBuffer` (~8MB
   at 1080p BGRA) now uses `PoolBuffer`. On repeated APNG captures, the pool
   reuses the same block instead of allocating and freeing 8MB each time.

4. **Added pool lifecycle management**:
   - `trim()` after each animated capture to release idle blocks
   - `ResetMemoryPool()` called from `ForceReset()` for clean state
   - Stats logging (pool size, reuse rate) after GIF/APNG captures

5. **Added safety features**: double-free detection, block count limit warning
   (256 blocks), unknown-pointer error logging, destructor leak warning.

## Why Not the GIF Buffer Too?

The GIF `prevQuantizedPixels` buffer (~2MB for 8-bit indexed at 1080p) uses a
complex resize-on-every-frame pattern across multiple code paths. Converting it
to `PoolBuffer` would touch ~10 locations in fragile encoding logic. The APNG
buffer (8MB BGRA, allocated once per capture) was the highest-impact target with
the cleanest conversion. GIF pool integration can be done as a follow-up.

## Build Notes

- `MemoryPool.cpp` includes `"logger.h"` — ensure the project's include path
  covers the parent of `memory_pool/` (same path that resolves
  `#include "memory_pool/MemoryPool.h"` from `screencapture.cpp`).
- No new external dependencies. Standard C++ only (`<mutex>`, `<atomic>`,
  `<vector>`, `<memory>`).
- Add `memory_pool/MemoryPool.cpp` to your CMakeLists.txt source list if not
  already present.
