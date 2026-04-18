# PrintScreen V3 — Technical Summary

## Architecture Overview

PrintScreen V3 is a Skyrim Special Edition SKSE plugin written in C++20 (MSVC) with a Papyrus script layer that handles game-side UI, configuration persistence, and state management. The plugin captures the desktop framebuffer via the DXGI Desktop Duplication API and encodes it into one of seven supported image formats.

```
┌──────────────────────────────────────────────────────────────┐
│  Papyrus Scripts (game thread)                               │
│  ┌──────────────┐  ┌───────────────┐  ┌──────────────────┐  │
│  │ MainQuest     │  │ MCM Script    │  │ Formula (native  │  │
│  │ (state, HUD,  │  │ (SkyUI MCM    │  │  function stubs) │  │
│  │  polling)     │  │  interface)   │  │                  │  │
│  └──────┬───────┘  └───────┬───────┘  └────────┬─────────┘  │
│         │                  │                   │             │
│         └──────────────────┴───────────────────┘             │
│                            │ Native calls                    │
├────────────────────────────┼─────────────────────────────────┤
│  C++ SKSE Plugin           │                                 │
│  ┌─────────────────────────▼─────────────────────────────┐   │
│  │ PapyrusInterface (registration, worker thread launch) │   │
│  └─────────────────────────┬─────────────────────────────┘   │
│                            │                                 │
│  ┌─────────────────────────▼─────────────────────────────┐   │
│  │ ScreenCapture namespace                               │   │
│  │  - Desktop Duplication setup                          │   │
│  │  - Single-frame capture (PNG/JPG/BMP/TIF/DDS/GIF)    │   │
│  │  - Animated GIF capture + WIC encoding                │   │
│  │  - Animated PNG capture + manual APNG chunk assembly  │   │
│  │  - Differential / true-delta encoding                 │   │
│  │  - MemoryPool (reusable frame buffers)                │   │
│  └───────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

## Build System

The project uses CMake 3.25+ with vcpkg for dependency management. The build targets MSVC with C++20 and produces a single DLL.

### Key Dependencies

- **CommonLibSSE-NG** — SKSE abstraction layer. Provides Papyrus VM binding, address-library relocation, and RE (reverse-engineered) Skyrim headers. Sourced via a vcpkg registry (colorglass).
- **DirectXTex** — Microsoft's texture processing library. Used for `ScratchImage` management, DDS block compression (BC1–BC7), and WIC file I/O.
- **DirectXTK** — DirectX Tool Kit. Available for auxiliary texture operations.
- **libpng / lodepng** — Optional PNG libraries (linked if found). The primary PNG path uses WIC directly.
- **spdlog + fmt** — Logging framework. Wrapped in a `logger` namespace with level-aware convenience functions.

### Precompiled Header

`pch.h` includes CommonLibSSE-NG, Windows/COM, DirectX, WIC, and standard library headers. The `Config.h` header is included in the PCH so settings are globally accessible.

## C++ Module Breakdown

### plugin.cpp — Entry Point

Implements the three SKSE export functions:

- `SKSEPlugin_Query` — version compatibility check.
- `SKSEPlugin_Version` — declares plugin metadata, address library usage, and compatible runtime versions (1.5.39 through 1.6.1170).
- `SKSEPlugin_Load` — initializes Config, sets up the logger, registers Papyrus functions via `PrintScreenPapyrus::RegisterFunctions`, and logs a startup banner.

### Config.h / config.cpp — Configuration

Reads a Windows INI file from the SKSE folder. Supports two INI formats: a modern sectioned format (`[Logging]`, `[Performance]`, `[Capture]`) and a legacy flat format for backward compatibility. Unknown keys are detected and reported at startup.

Settings include log level (0–5), console/file output toggles, parallel compression enable, thread count, and capture progress logging. A `Save()` function writes current settings back, and `SaveDefaults()` creates a fresh INI with sensible defaults (DEBUG log level for new installs).

### PapyrusInterface.h / PapyrusInterface.cpp — Script Bridge

Registers six native functions on the `Printscreen_Formula_script` Papyrus class:

| Function | Signature | Purpose |
|----------|-----------|---------|
| `CheckPath` | `bool (string)` | Validates and optionally creates the output directory. Performs a full read/write probe using a temp file. |
| `TakePhoto` | `string (string, string, float, string, float, float, int, int, int)` | Launches a detached worker thread for the requested capture. Returns immediately. |
| `Get_Result` | `string ()` | Returns the current state string. Consumes callback results (CALLBACK_SAVED, CALLBACK_ERROR, CALLBACK_CANCELLED). |
| `Cancel` | `string ()` | Sets the atomic cancel flag. |
| `ForceReset` | `string ()` | Resets all state flags, clears the result, trims the memory pool. |
| `ResetState` | `string ()` | Alias for ForceReset. |

#### Worker Thread Model

`TakePhoto` validates the path, sets state flags (`g_isStarting`, `g_operationInProgress`), stores `"Running"` in the guarded result string, and spawns a detached `std::thread` running `CaptureWorkerThread`. The worker routes to one of three internal functions based on the normalized image type string:

- `PerformImageCapture` — single-frame formats.
- `PerformGifCapture` — animated GIF.
- `PerformAPNGCapture` — animated PNG.

On completion (success, error, or cancellation), the worker stores a `CALLBACK_*` prefixed result string and sets `g_resultReady`. The Papyrus polling loop (OnUpdate) calls `Get_Result` to detect completion.

#### Concurrency

All shared state is protected by `std::atomic<bool>` flags and a single `std::mutex` guarding the result string. The cancel flag uses relaxed memory ordering for low-overhead cooperative cancellation.

### ScreenCapture.h / ScreenCapture.cpp — Capture Engine

This is the core of the plugin at ~3,200 lines. Key subsystems:

#### Desktop Duplication

`SetupDesktopDuplication` creates a D3D11 device (hardware driver), walks the DXGI adapter chain to the primary output, and calls `DuplicateOutput` to get an `IDXGIOutputDuplication` interface. Cancellation is checked between each COM call.

`CaptureSingleFrame` acquires a frame (up to 5 retries with 1-second timeout each), copies the GPU texture to a CPU-readable staging texture, and memcpys the pixel rows into a `DirectX::ScratchImage`. Cancellation is checked every 100 rows during the pixel copy.

#### Single-Frame Save Pipeline

- **WIC path** (PNG, JPEG, BMP, TIF, static GIF) — `SaveToWIC` initializes COM, creates a WIC factory, and encodes the image. PNG uses a manual WIC pipeline (CreateBitmapFromMemory → WriteSource) to ensure correct BGRA color handling. JPEG uses a quality property bag. TIFF uses a compression-method property bag.
- **DDS path** — `SaveToDDS` calls `DirectX::Compress` with the selected BC format and `TEX_COMPRESS_PARALLEL` where possible, then `SaveToDDSFile`. Compression timing and ratio are logged.

#### Animated GIF Pipeline (Disk-Based)

To avoid memory exhaustion on long captures, the GIF pipeline stores frames to disk as temporary BMP files during capture, then loads them one at a time during encoding:

1. Capture loop: acquire frame → `SaveTempFrame` as BMP → release ScratchImage → sleep to next frame time.
2. Fire `onFramesCaptured` callback (allows early HUD restoration).
3. Encoding loop: load each BMP from disk → quantize to 8-bit indexed via WIC format converter → optionally compute differential region → write GIF frame via WIC encoder → release frame memory.
4. Cleanup temp directory.

**Differential GIF encoding** (when `gifCompression == 1`):

- Quantize current frame to 8bpp using the previous frame's palette for consistency.
- Compare quantized pixels to find the bounding box of changed indices.
- If `gifOptimize == 1` (true delta): find the least-used color index, mark unchanged pixels with that index, set it as the GIF transparent color index. This allows GIF decoders to composite only changed pixels.
- If `gifOptimize == 0` (region only): extract the changed bounding box without transparency.
- Set disposal method to "do not dispose" (1) so the previous frame persists under the diff region.
- Use `WritePixels` instead of `WriteSource` for transparency frames to prevent WIC from re-quantizing.

#### Animated PNG (APNG) Pipeline (Disk-Based)

Same disk-buffered capture loop as GIF. The encoding side manually assembles APNG chunks because WIC has no APNG encoder:

1. Encode the first frame as a standard PNG via WIC.
2. Parse its chunks (IHDR, IDAT, etc.) using a custom PNG parser (`APNGHelper`).
3. Write PNG signature → IHDR → acTL (animation control: frame count, loop count) → fcTL (frame control for frame 0) → IDAT chunks from frame 0.
4. For subsequent frames:
   - If differential encoding is enabled, compute the changed bounding box via `ComputeAPNGDiffRect` (per-channel threshold of 2 for noise tolerance).
   - If true delta (`apngOptimize == 1`): call `ExtractDeltaSubRegion` which produces a BGRA region where unchanged pixels are fully transparent (alpha=0). Encode with `EncodeRegionToPNGWithAlpha` to preserve the alpha channel.
   - If region-only: call `ExtractSubRegion` and encode with `EncodeRegionToPNG`.
   - Parse the encoded region PNG's IDAT chunks.
   - Write fcTL (with x/y offsets, blend_op=OVER for diff frames) → fdAT chunks (IDAT data prefixed with sequence number).
5. Write IEND.

The `APNGHelper` namespace implements CRC32 calculation, big-endian I/O, and chunk read/write functions conforming to the PNG/APNG specification.

#### MemoryPool

A custom memory pool (`memory_pool/MemoryPool.h/.cpp`) provides reusable buffers for large BGRA frame data (~8 MB at 1080p). Key properties:

- On-demand allocation with best-fit block selection (no upfront pre-allocation).
- `PoolBuffer` RAII wrapper — move-only handle that returns memory to the pool on destruction.
- `trim()` releases idle blocks back to the OS.
- Safety features: double-free detection, block count warnings (256), unknown-pointer logging, destructor leak warnings.
- Thread-safe via internal `std::mutex`.

Currently integrated for the APNG `prevFrameBuffer` (the frame-to-frame comparison buffer). The GIF pipeline's `prevQuantizedPixels` still uses `std::vector` due to the complexity of its resize-on-every-frame pattern.

#### Cancellation Model

Two complementary mechanisms:

1. **Exception-based** (`CancelIfRequested`): checks the atomic flag and throws a `Cancelled` exception. Caught at the top-level `CaptureScreen`/`CaptureGIF`/`CaptureAPNG` try-catch, which cleans up and returns a failure result.
2. **Return-code based** (`CheckCancellation`): returns a bool. Used inside functions where exception unwinding would be problematic (e.g., inside COM/WIC callback lambdas, tight loops with resource handles).

Both check `g_cancelFlag` which is set by the Papyrus `Cancel()` call.

### logger.h / logger.cpp / logger_shim.hpp — Logging

Thin wrappers around spdlog. `SetupLog` creates file and console sinks based on Config settings. Log levels map to spdlog levels (0=off through 5=trace). The `logger_shim.hpp` provides `logger::u8()` wrappers for UTF-8 string logging.

### stringutils.h — String Conversion

Inline `utf8_to_wstring` and `wstring_to_utf8` functions using the Win32 `MultiByteToWideChar` / `WideCharToMultiByte` APIs.

### cancel.h — Cancellation Helpers

Standalone declaration of the `Cancelled` exception struct and `CancelIfRequested` inline function (also duplicated in `screencapture.cpp` for self-containment).

## Papyrus Script Layer

### Printscreen_Formula_script

A stub script with `Global Native` function declarations that bind to the C++ registered functions. This is the bridge class — all native calls go through it.

### Printscreen_MainQuest_script

The primary quest script (~880 lines). Manages:

- **Initialization**: checks for SKSE, ConsoleUtil, JsonUtil, JContainers on `OnInit`. Reads or creates the JSON config.
- **Game load recovery** (`OnPlayerLoadGame`): re-registers mod events and hotkeys (which don't survive save/load), calls `ForceReset` on the native plugin, sanitizes all properties, recalculates FPS from Duration, clears stale capture flags, and force-restores the HUD.
- **Capture orchestration**: `CaptureImage` validates the path, sets state, hides the HUD, calls `TakePhoto`, then starts a polling loop via `RegisterForSingleUpdate`.
- **Polling**: `OnUpdate` checks for timeout (300s), calls `Get_Result`, and dispatches to `_HandlePollResult`. The poll interval adapts — 0.1s for fast captures, 1.5s for long-running ones (AGIF, APNG, DDS BC6H/BC7).
- **Result handling**: `_HandlePollResult` parses the result string prefix (CALLBACK_SAVED, CALLBACK_ERROR, CALLBACK_CANCELLED, etc.) and calls `OnScreenshotCompleted` for terminal states. In-progress states (Running, Starting, Cancelling) return false to continue polling.
- **Hotkey** (`OnKeyUp`): if a capture is active, pressing the key cancels it. Otherwise, it starts a new capture after a 0.75s debounce.
- **HUD control**: `HideHUD` / `ShowHUD` use `ConsoleUtil.ExecuteCommand("tm")` with a `UI_Hidden` flag to prevent double-toggling.
- **JSON persistence**: `readJson` / `writeJson` use PapyrusUtil's JsonUtil API. Every value is sanitized after reading (clamped ranges, valid enum strings, FPS recalculation from Duration).

### Printscreen_MCM__Script

Extends `SKI_ConfigBase` (SkyUI MCM framework). Builds a single "Settings" page with:

- Input field for path.
- Menu for image type selection (8 options).
- Toggle for automatic UI removal.
- Keymap selector for the photo key.
- Context-sensitive sliders and menus that enable/disable based on the selected image type (JPG quality, TIF mode, DDS mode, duration, loop count, differential compression, optimize, quality).
- `ResetFlags()` helper that disables all format-specific widgets, called before selectively enabling the relevant ones.
- FPS auto-calculation in `SetFPS()` based on duration (15 fps for <4s, scaling down to 6 fps for >13s).
- Writes JSON on `OnConfigClose`.

### Printscreen_ME_script

A magic effect script that displays a MessageBox with the current configuration summary when activated (used as an in-game status spell).

### Printscreen_MAP_script

A key-code-to-name lookup utility using JContainers' JIntMap. Maps DirectInput scan codes (1–265) to human-readable key names.

### ArrayUtils

Generic Papyrus array search helpers (`FindString`, `FindForm`, `ContainsString`, `ContainsForm`).

### Printscreen_PlayerRef_Script

An empty ReferenceAlias script placeholder.

## Data Flow Summary

```
User presses hotkey
       │
       ▼
OnKeyUp (MainQuest script)
       │
       ├── If capture active → Cancel()
       │
       └── If idle → CaptureImage()
              │
              ├── HideHUD()
              ├── TakePhoto() [native call]
              │      │
              │      └── Spawns CaptureWorkerThread
              │             │
              │             ├── PerformImageCapture / PerformGifCapture / PerformAPNGCapture
              │             │      │
              │             │      ├── SetupDesktopDuplication
              │             │      ├── CaptureSingleFrame (loop for animated)
              │             │      ├── SaveToWIC / SaveToDDS / Encode GIF / Assemble APNG
              │             │      └── Return CALLBACK_SAVED / CALLBACK_ERROR
              │             │
              │             └── Store result in g_result, set g_resultReady
              │
              └── RegisterForSingleUpdate (poll loop)
                     │
                     ▼
              OnUpdate → Get_Result() → _HandlePollResult()
                     │
                     ├── Terminal → OnScreenshotCompleted → ShowHUD() + notify
                     └── In-progress → re-register poll
```

## File Listing

### C++ Source (src.7z)

| File | Purpose |
|------|---------|
| `plugin.cpp` | SKSE entry point and Papyrus registration |
| `PapyrusInterface.h/cpp` | Native function implementations, worker thread |
| `screencapture.h/cpp` | Capture engine (Desktop Duplication, encoding, differential, APNG assembly) |
| `Config.h / config.cpp` | INI configuration reader/writer |
| `logger.h / logger.cpp` | spdlog wrapper |
| `logger_shim.hpp` | UTF-8 logging helpers |
| `stringutils.h` | UTF-8 ↔ UTF-16 conversion |
| `cancel.h` | Cancellation exception and helper |
| `pch.h` | Precompiled header |
| `CMakeLists.txt` | Build configuration |
| `printscreen.rc` | Resource file (version info) |

### Papyrus Source (Source.7z)

| File | Purpose |
|------|---------|
| `Printscreen_Formula_script.psc` | Native function stubs |
| `Printscreen_MainQuest_script.psc` | Core game logic, state machine, HUD, JSON |
| `Printscreen_MCM__Script.psc` | SkyUI MCM interface |
| `Printscreen_ME_script.psc` | Status display magic effect |
| `Printscreen_MAP_script.psc` | Key code → name mapper |
| `ArrayUtils.psc` | Generic array search utilities |
| `Printscreen_PlayerRef_Script.psc` | Empty alias placeholder |
