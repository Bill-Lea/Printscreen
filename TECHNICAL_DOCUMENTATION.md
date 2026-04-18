# PrintScreen V3 - Technical Documentation

**Version:** 3.0.0
**Author:** William G Lea
**Platform:** Windows x64
**Language:** C++20 (MSVC)

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Project Structure](#project-structure)
4. [Build System](#build-system)
5. [Dependencies](#dependencies)
6. [Core Components](#core-components)
7. [API Reference](#api-reference)
8. [Configuration](#configuration)
9. [Data Flow](#data-flow)
10. [Memory Management](#memory-management)
11. [Supported Formats](#supported-formats)
12. [Runtime Compatibility](#runtime-compatibility)
13. [Build Instructions](#build-instructions)

---

## Overview

PrintScreen V3 is a professional-grade SKSE (Skyrim Script Extender) plugin for Skyrim Special Edition that provides advanced screenshot and animated image capture capabilities. The plugin captures frames using the Windows Desktop Duplication API, avoiding direct hooks into the game's render pipeline for maximum compatibility.

### Key Features

- Multi-format image capture (PNG, JPG, BMP, TIF, DDS, GIF, APNG)
- Animated GIF/APNG recording with differential encoding
- GPU-accelerated DDS compression (BC1-BC7)
- Custom memory pool for efficient frame buffering
- Cooperative cancellation support
- Configurable logging with rotation
- MCM (Mod Configuration Menu) integration via SkyUI

### Code Metrics

| Component | Lines of Code |
|-----------|---------------|
| screencapture.cpp | ~3,276 |
| PapyrusInterface.cpp | ~643 |
| config.cpp | ~260 |
| **Total C++ Source** | **~4,878** |

---

## Architecture

### High-Level Design

```
┌─────────────────────────────────────────────────────────────────┐
│                        Skyrim SE Process                         │
├─────────────────────────────────────────────────────────────────┤
│  ┌──────────────────┐    ┌──────────────────────────────────┐  │
│  │   Papyrus VM     │    │         SKSE Framework            │  │
│  │                  │    │                                    │  │
│  │  MCM Scripts     │◄──►│  CommonLibSSE-NG Abstraction      │  │
│  │  Quest Scripts   │    │                                    │  │
│  └────────┬─────────┘    └──────────────┬───────────────────┘  │
│           │                              │                       │
│           ▼                              ▼                       │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                  Printscreen.dll                          │  │
│  │  ┌────────────────┐  ┌─────────────┐  ┌──────────────┐   │  │
│  │  │PapyrusInterface│  │ScreenCapture│  │   Config     │   │  │
│  │  │                │  │             │  │              │   │  │
│  │  │ Native funcs   │─►│ Capture     │  │ INI parsing  │   │  │
│  │  │ Worker threads │  │ Encode      │  │ Settings     │   │  │
│  │  └────────────────┘  │ Save        │  └──────────────┘   │  │
│  │                      └──────┬──────┘                      │  │
│  │                             │                             │  │
│  │  ┌──────────────┐  ┌───────▼───────┐  ┌──────────────┐   │  │
│  │  │  MemoryPool  │  │   DirectX 11  │  │    Logger    │   │  │
│  │  │              │  │   DXGI 1.2    │  │   (spdlog)   │   │  │
│  │  │ Frame buffers│  │   DirectXTex  │  │              │   │  │
│  │  └──────────────┘  └───────────────┘  └──────────────┘   │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│              Windows Desktop Duplication API                     │
│                    (D3D11 / DXGI 1.2)                           │
└─────────────────────────────────────────────────────────────────┘
```

### Threading Model

- **Main Thread**: Papyrus VM calls, state management
- **Worker Thread**: Detached `std::thread` for capture operations
- **Synchronization**: `std::atomic<bool>` flags, `std::mutex` for result string

---

## Project Structure

```
PrintscreenV3/
├── src/                           # C++ source directory
│   ├── plugin.cpp                 # SKSE entry points (Query/Version/Load)
│   ├── screencapture.cpp/.h       # Core capture engine
│   ├── PapyrusInterface.cpp/.h    # Papyrus native function bindings
│   ├── config.cpp/.h              # INI configuration system
│   ├── logger.cpp/.h              # spdlog-based logging wrapper
│   ├── pch.h                      # Precompiled headers
│   ├── stringutils.h              # UTF-8/UTF-16 conversion utilities
│   ├── cancel.h                   # Cancellation exception support
│   ├── memory_pool/
│   │   ├── MemoryPool.cpp/.h      # Custom memory pool allocator
│   └── CMakeLists.txt             # Source-level CMake configuration
│
├── Papyrus Scripts/               # Papyrus source scripts (.psc)
│   ├── Printscreen_Formula_script.psc     # Native function declarations
│   ├── Printscreen_MainQuest_script.psc   # Main state management
│   ├── Printscreen_MCM__Script.psc        # MCM menu configuration
│   ├── Printscreen_MAP_script.psc         # Key mapping support
│   ├── Printscreen_ME_script.psc          # Magic effect handler
│   ├── Printscreen_PlayerRef_Script.psc   # Player reference alias
│   └── ArrayUtils.psc                     # Array utility functions
│
├── CMakeLists.txt                 # Root CMake configuration
├── CMakePresets.json              # CMake build presets
├── vcpkg.json                     # vcpkg dependency manifest
├── vcpkg-configuration.json       # vcpkg registry configuration
├── pch.h                          # Root precompiled header
│
├── usersGuide.md                  # End-user documentation
├── technical_summary.md           # Architecture overview
├── Nexus+description.txt          # Nexus Mods description
│
└── build/                         # Build output directory
    └── bin/                       # Compiled DLL output
```

---

## Build System

### Toolchain

| Component | Version/Specification |
|-----------|----------------------|
| Build Tool | CMake 3.25+ |
| Generator | Ninja |
| Compiler | MSVC (Visual Studio 2022) |
| C++ Standard | C++20 |
| Architecture | x64 (x64-windows-static-md) |
| Package Manager | vcpkg |

### CMake Configuration

**Compiler Flags:**
- `/permissive-` - Strict conformance mode
- `/Zc:preprocessor` - Conforming preprocessor
- `/EHsc` - Exception handling
- `/MP` - Parallel compilation
- `/W4` - Warning level 4
- `/utf-8` - UTF-8 source encoding

**Preprocessor Definitions:**
- `WIN32_LEAN_AND_MEAN`
- `NOMINMAX`
- `UNICODE` / `_UNICODE`
- `_CRT_SECURE_NO_WARNINGS`

### Build Presets

| Preset | Configuration | Runtime | Use Case |
|--------|---------------|---------|----------|
| `debug` | Debug | MultiThreadedDebugDLL | Development |
| `release` | Release | MultiThreadedDLL | Distribution |
| `relwithdebinfo` | RelWithDebInfo | MultiThreadedDLL | Profiling |

---

## Dependencies

### Runtime Dependencies

| Library | Purpose | Integration |
|---------|---------|-------------|
| **CommonLibSSE-NG** | SKSE abstraction layer | vcpkg (colorglass registry) |
| **DirectXTex** | Texture processing, BC compression | vcpkg |
| **DirectXTK** | DirectX helper utilities | vcpkg |
| **spdlog** | Logging framework | vcpkg |
| **fmt** | Format string library | vcpkg (spdlog dependency) |
| **libpng** | PNG encoding (APNG support) | vcpkg (optional) |
| **lodepng** | Alternative PNG encoder | vcpkg (optional) |

### Windows SDK Components

| Library | Purpose |
|---------|---------|
| `d3d11.lib` | Direct3D 11 device creation |
| `dxgi.lib` | DXGI adapter/output enumeration |
| `windowscodecs.lib` | Windows Imaging Component |
| `ole32.lib` | COM runtime |
| `propsys.lib` | Property system (WIC) |

### Game Dependencies

| Dependency | Purpose |
|------------|---------|
| SKSE | Script Extender framework |
| SkyUI | MCM menu framework |
| Address Library | Version-independent addressing |
| ConsoleUtil | HUD toggle (`tm` command) |
| PapyrusUtil / JsonUtil | JSON configuration storage |
| JContainers | Advanced key mapping |

---

## Core Components

### 1. plugin.cpp - SKSE Entry Point

Implements required SKSE export functions:

```cpp
extern "C" __declspec(dllexport) bool SKSEPlugin_Query(
    const SKSE::QueryInterface* a_skse,
    SKSE::PluginInfo* a_info);

extern "C" __declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version;

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(
    const SKSE::LoadInterface* a_skse);
```

**Responsibilities:**
- Runtime version validation (rejects Creation Kit)
- Configuration system initialization
- Logging setup (spdlog sinks)
- Papyrus function registration

### 2. ScreenCapture - Core Capture Engine

**Location:** `src/screencapture.cpp` (~3,276 lines)

#### Key Enumerations

```cpp
enum class ImageFormat { PNG, JPEG, BMP, TIF, GIF, APNG, DDS };

enum class TiffMode { NONE, LZW, CCITT1D, CCITT4, RLE, ZIP };

enum class DDSCompression {
    BC1, BC2, BC3, BC4, BC5, BC6H,
    BC7_SLOW, BC7_NORMAL, BC7_FAST
};
```

#### Core Structures

```cpp
struct CaptureParams {
    ImageFormat format;
    int quality;                    // JPEG quality (0-100)
    TiffMode tiffMode;
    DDSCompression ddsMode;
    float animDuration;             // Animation duration (seconds)
    float animFPS;                  // Frames per second
    int loopCount;                  // GIF/APNG loop count
    int compression;                // Differential encoding mode
    int optimize;                   // True delta encoding flag
    std::atomic<bool>* cancelFlag;
    std::function<void()> onFramesCaptured;
};

struct CaptureResult {
    bool success;
    std::string errorMessage;
    std::string outputPath;
};
```

#### Desktop Duplication Pipeline

1. **Setup** (`SetupDesktopDuplication()`):
   - Create D3D11 device with `D3D_DRIVER_TYPE_HARDWARE`
   - Query DXGI factory → enumerate adapters → enumerate outputs
   - Call `DuplicateOutput()` on primary display

2. **Frame Capture** (`CaptureSingleFrame()`):
   - `AcquireNextFrame()` with timeout
   - Query `IDXGIResource` for `ID3D11Texture2D`
   - Create CPU-readable staging texture
   - `CopyResource()` GPU → staging
   - `Map()` staging → `memcpy()` to ScratchImage

3. **Encoding**:
   - WIC path: PNG, JPEG, BMP, TIF, static GIF
   - DirectXTex path: DDS with BC compression
   - Custom path: Animated GIF/APNG

#### Animated Capture (Disk-Based)

To prevent memory exhaustion during long recordings:

```
┌─────────────────────────────────────────────────────────────┐
│                    Capture Phase                             │
├─────────────────────────────────────────────────────────────┤
│  for each frame in duration:                                │
│    1. AcquireNextFrame()                                    │
│    2. Copy to staging texture                               │
│    3. SaveTempFrame() as BMP to disk                        │
│    4. Release ScratchImage memory                           │
│    5. Sleep until next frame time                           │
│    6. Check cancellation flag                               │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                    Callback Phase                            │
├─────────────────────────────────────────────────────────────┤
│  Fire onFramesCaptured() → Papyrus restores HUD             │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                    Encoding Phase                            │
├─────────────────────────────────────────────────────────────┤
│  for each temp BMP:                                         │
│    1. Load from disk                                        │
│    2. Quantize to 8-bit indexed (GIF)                       │
│    3. Compute differential region                           │
│    4. Encode frame to output                                │
│    5. Release memory                                        │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│                    Cleanup Phase                             │
├─────────────────────────────────────────────────────────────┤
│  Delete temporary BMP files                                 │
└─────────────────────────────────────────────────────────────┘
```

#### Differential Encoding

**GIF Optimization (`gifOptimize`):**
- `0`: Region-only delta (extract bounding box of changed pixels)
- `1`: True delta (unchanged pixels marked as transparent)

**APNG Optimization (`apngOptimize`):**
- `0`: Region-only delta with fcTL offset
- `1`: True delta with `APNG_BLEND_OP_OVER`

### 3. PapyrusInterface - Script Bridge

**Location:** `src/PapyrusInterface.cpp` (~643 lines)

Bridges Papyrus VM to C++ capture logic via CommonLibSSE-NG bindings.

#### State Management

```cpp
namespace PrintScreenPapyrus {
    std::atomic<bool> g_cancelRequested{false};
    std::atomic<bool> g_operationInProgress{false};
    std::atomic<bool> g_isStarting{false};
    std::atomic<bool> g_resultReady{false};

    std::mutex g_resultMutex;
    std::string g_captureResult;

    std::thread::id g_mainThreadId;
}
```

#### Worker Thread Flow

```cpp
void CaptureWorkerThread(/* params */) {
    try {
        g_operationInProgress.store(true);

        // Route to appropriate capture function
        switch (format) {
            case SINGLE_FRAME:
                result = PerformImageCapture(params);
                break;
            case GIF:
                result = PerformGifCapture(params);
                break;
            case APNG:
                result = PerformAPNGCapture(params);
                break;
        }

        // Store result atomically
        {
            std::lock_guard<std::mutex> lock(g_resultMutex);
            g_captureResult = result;
        }
        g_resultReady.store(true);

    } catch (const Cancelled&) {
        SetResult("CALLBACK_CANCELLED");
    } catch (const std::exception& e) {
        SetResult("CALLBACK_ERROR: " + std::string(e.what()));
    }

    g_operationInProgress.store(false);
}
```

### 4. Config - Configuration System

**Location:** `src/config.cpp` (~260 lines)

#### INI File Locations

| Type | Path |
|------|------|
| Modern | `Documents/My Games/Skyrim Special Edition/SKSE/PrintScreen.ini` |
| Legacy | `Documents/My Games/Skyrim Special Edition/SKSE/Plugins/Printscreen_Log.ini` |

#### Settings Structure

```cpp
struct Settings {
    // Logging
    LogLevel logLevel = LogLevel::INFO;
    bool consoleOutput = false;
    bool fileOutput = true;
    bool showTimestamps = true;
    uint32_t maxLogFileSizeMB = 8;

    // Performance
    bool parallelCompression = true;
    uint32_t compressionThreads = 0;  // 0 = auto-detect

    // Capture
    bool logCaptureProgress = false;
    bool logTimingInfo = false;

    // DDS
    DDSMode ddsMode = DDSMode::BC7;
};
```

### 5. MemoryPool - Custom Allocator

**Location:** `src/memory_pool/MemoryPool.cpp`

#### Design Goals

- Reusable frame buffers (~8 MB at 1080p BGRA)
- Avoid repeated OS malloc/free during capture loops
- Best-fit block selection to minimize fragmentation
- Thread-safe allocation/deallocation

#### Interface

```cpp
class MemoryPool {
public:
    void* allocate(size_t size);
    PoolBuffer allocateBuffer(size_t size);  // RAII wrapper
    void deallocate(void* ptr);
    void reset();   // Free all blocks (must not be in use)
    void trim();    // Free only unused blocks
    void logStats();

private:
    struct PoolBlock {
        std::unique_ptr<uint8_t[]> data;
        size_t size;
        bool inUse;
    };
    std::vector<PoolBlock> blocks;
    std::mutex mutex;
    // Statistics...
};

class PoolBuffer {
public:
    PoolBuffer(MemoryPool* pool, void* ptr, size_t size);
    ~PoolBuffer();  // Auto-returns to pool
    PoolBuffer(PoolBuffer&& other) noexcept;  // Move-only

    void* data() const;
    size_t size() const;
};
```

### 6. Logger - Logging System

**Location:** `src/logger.cpp`

Wraps spdlog with project-specific configuration:

```cpp
namespace logger {
    template<typename... Args>
    void trace(fmt::format_string<Args...> fmt, Args&&... args);

    template<typename... Args>
    void debug(fmt::format_string<Args...> fmt, Args&&... args);

    template<typename... Args>
    void info(fmt::format_string<Args...> fmt, Args&&... args);

    template<typename... Args>
    void warn(fmt::format_string<Args...> fmt, Args&&... args);

    template<typename... Args>
    void error(fmt::format_string<Args...> fmt, Args&&... args);

    template<typename... Args>
    void critical(fmt::format_string<Args...> fmt, Args&&... args);

    void SetupLog();
}
```

**Log Output:**
- File: `{SKSE}/Printscreen.log` (rotating, default 8 MB)
- Console: MSVC debug output (optional)

---

## API Reference

### Papyrus Native Functions

Registered on `Printscreen_Formula_script`:

| Function | Signature | Description |
|----------|-----------|-------------|
| `CheckPath` | `bool(string path)` | Validates output directory, creates if needed |
| `TakePhoto` | `string(string basePath, string imageType, float jpgCompression, string compressionMode, float duration, float fps, int loopCount, int compression, int optimize)` | Initiates async capture |
| `Get_Result` | `string()` | Returns current state or callback result |
| `Cancel` | `string()` | Sets cancellation flag |
| `ForceReset` | `string()` | Resets all state, trims memory pool |
| `ResetState` | `string()` | Alias for ForceReset |

### TakePhoto Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `basePath` | string | Output directory path |
| `imageType` | string | Format: "PNG", "JPG", "BMP", "TIF", "DDS", "GIF", "AGIF", "APNG" |
| `jpgCompression` | float | JPEG quality (0-100) |
| `compressionMode` | string | TIFF: "UNCOMPRESSED", "RLE", "LZW", "ZIP"; DDS: "BC1"-"BC7" |
| `duration` | float | Animation duration in seconds |
| `fps` | float | Animation frames per second |
| `loopCount` | int | Loop count (0 = infinite) |
| `compression` | int | 0 = full frames, 1 = differential |
| `optimize` | int | 0 = region delta, 1 = true delta (transparency) |

### Result States

| Result | Meaning |
|--------|---------|
| `"Running"` | Capture in progress |
| `"CALLBACK_SAVED"` | Capture completed successfully |
| `"CALLBACK_ERROR: {message}"` | Capture failed with error |
| `"CALLBACK_CANCELLED"` | Capture was cancelled |

---

## Configuration

### INI File Structure

```ini
[Logging]
LogLevel=3              ; 0=NONE, 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG, 5=TRACE
ConsoleOutput=0         ; Output to debugger console
FileOutput=1            ; Write to log file
ShowTimestamps=1        ; Include timestamps
MaxLogFileSizeMB=8      ; Log rotation size

[Performance]
ParallelCompression=1   ; Multi-threaded DDS compression
CompressionThreads=0    ; 0 = auto-detect CPU count

[Capture]
LogCaptureProgress=0    ; Log frame capture progress
LogTimingInfo=0         ; Log timing details

[DDS]
DDSMode=BC7             ; Default DDS compression format
```

### MCM Configuration (JSON)

User preferences stored via JContainers:

| Setting | Type | Description |
|---------|------|-------------|
| Path | string | Output directory |
| ImageType | string | Default format |
| JPG_Compression | int | JPEG quality |
| Duration | float | Animation duration |
| FPS | float | Animation FPS |
| LoopCount | int | Animation loops |
| Compression | int | Differential mode |
| Optimize | int | True delta mode |
| Key_TakePhoto | int | Screenshot hotkey |
| Menu | int | MCM menu state |

---

## Data Flow

### Screenshot Capture Sequence

```
┌─────────────┐     ┌────────────────────┐     ┌─────────────────┐
│   User      │     │   Papyrus VM       │     │   C++ Plugin    │
│ (Keyboard)  │     │                    │     │                 │
└──────┬──────┘     └─────────┬──────────┘     └────────┬────────┘
       │                      │                         │
       │ Hotkey Press         │                         │
       ├─────────────────────►│                         │
       │                      │                         │
       │                      │ OnKeyDown()             │
       │                      ├────────────────────────►│
       │                      │ TakePhoto()             │
       │                      │                         │
       │                      │◄────────────────────────┤
       │                      │ "Running"               │
       │                      │                         │
       │                      │         ┌───────────────┤
       │                      │         │ Worker Thread │
       │                      │         │               │
       │                      │         │ Desktop Dup   │
       │                      │         │ Capture Frame │
       │                      │         │ Encode/Save   │
       │                      │         │               │
       │                      │ Poll    │               │
       │                      ├────────►│               │
       │                      │Get_Result()             │
       │                      │◄────────┤               │
       │                      │"Running"│               │
       │                      │         │               │
       │                      │   ...   │ Complete      │
       │                      │         │◄──────────────┤
       │                      │         │               │
       │                      │ Poll    │               │
       │                      ├────────►│               │
       │                      │Get_Result()             │
       │                      │◄────────┤               │
       │                      │"CALLBACK_SAVED"         │
       │                      │                         │
       │ Notification         │                         │
       │◄─────────────────────┤                         │
       │                      │                         │
```

---

## Memory Management

### Frame Buffer Strategy

| Scenario | Strategy |
|----------|----------|
| Single-frame capture | MemoryPool allocation → capture → encode → deallocate |
| Animated capture | MemoryPool allocation → capture → save to disk → deallocate → repeat |
| Post-capture encoding | Load from disk → encode one frame → deallocate → repeat |

### Memory Pool Statistics

Available via `logStats()`:
- Total bytes allocated
- Bytes currently in use
- Allocation count
- Reuse count
- Block count and sizes

---

## Supported Formats

### Single-Frame Formats

| Format | Encoder | Compression Options |
|--------|---------|---------------------|
| PNG | WIC | Lossless |
| JPEG | WIC | Quality 0-100 |
| BMP | WIC | Uncompressed |
| TIF | WIC | None, LZW, RLE, ZIP, CCITT |
| DDS | DirectXTex | BC1-BC7 |

### Animated Formats

| Format | Encoder | Features |
|--------|---------|----------|
| GIF | WIC + Custom | 8-bit color, differential, transparency delta |
| APNG | Custom chunk assembly | Full color, differential, blend operations |

### DDS Compression Modes

| Mode | Description | Use Case |
|------|-------------|----------|
| BC1 | RGB, 1-bit alpha | Simple textures |
| BC2 | RGB, explicit 4-bit alpha | Sharp alpha edges |
| BC3 | RGB, interpolated 8-bit alpha | Smooth alpha |
| BC4 | Single channel | Grayscale/height maps |
| BC5 | Two channels | Normal maps |
| BC6H | HDR RGB | HDR content |
| BC7 | High-quality RGBA | Best quality |

---

## Runtime Compatibility

### Supported Skyrim Versions

| Version | Status |
|---------|--------|
| 1.5.39 | Supported |
| 1.5.97 | Supported |
| 1.6.318 | Supported |
| 1.6.353 | Supported |
| 1.6.629 | Supported |
| 1.6.640 | Supported |
| 1.6.659 | Supported |
| 1.6.678 | Supported |
| 1.6.1130 | Supported |
| 1.6.1170 | Supported |

### Requirements

- Windows 10/11 x64
- DirectX 11.1 compatible GPU
- SKSE (Skyrim Script Extender)
- Address Library for SKSE Plugins

---

## Build Instructions

### Prerequisites

1. Visual Studio 2022 with C++ workload
2. CMake 3.25+
3. vcpkg package manager
4. Git

### Build Steps

```bash
# Clone repository
git clone <repository-url>
cd PrintscreenV3

# Configure with CMake preset
cmake --preset release

# Build
cmake --build build --config Release
```

### Build Outputs

| File | Location |
|------|----------|
| `Printscreen.dll` | `build/bin/` |

### Deployment

Copy to Mod Organizer 2 or manual installation:

```
Skyrim Special Edition/
└── Data/
    └── SKSE/
        └── Plugins/
            └── Printscreen.dll
```

### Debug Build

```bash
cmake --preset debug
cmake --build build --config Debug
```

Debug build enables:
- Debug symbols
- Runtime checks
- Verbose logging

---

## Error Handling

### Exception Types

| Exception | Source | Handling |
|-----------|--------|----------|
| `Cancelled` | Cancel flag check | Clean unwind, return CALLBACK_CANCELLED |
| `std::exception` | General errors | Log error, return CALLBACK_ERROR |
| COM/HRESULT | DirectX/WIC | Convert to readable message, return CALLBACK_ERROR |

### HRESULT Error Formatting

DirectX and WIC errors are converted to human-readable messages with error codes for debugging.

---

## Technical Highlights

1. **No Render Hook**: Uses Windows Desktop Duplication API for compatibility
2. **Memory Efficient**: Custom pool + disk buffering prevents exhaustion
3. **Full Cancellation**: Cooperative cancellation with exception-based unwinding
4. **Modern C++20**: Atomics, mutexes, threads, filesystem, optional
5. **Multi-Format**: Seven formats with format-specific optimizations
6. **Differential Encoding**: Minimal file sizes for animations
7. **Parallel Compression**: Multi-threaded BC encoding
8. **Cross-Version**: Address Library ensures Skyrim version compatibility

---

## License

Copyright William G Lea. All rights reserved.

---

*Generated: April 2026*
