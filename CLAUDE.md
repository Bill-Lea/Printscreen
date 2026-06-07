# PrintScreen V4 — SKSE Plugin

C++ SKSE plugin for Skyrim Special Edition providing advanced screenshot and video capture. Built on CommonLibSSE-NG, DirectX 11, WIC, DirectXTex, and (new in V4) Microsoft Media Foundation for H.264 video.

## Build

- **Toolchain:** CMake + vcpkg, Visual Studio (MSVC)
- **Project root:** `C:\CodePackages\PrintscreenV3` (repo name is V3; V4 is the current development version)
- **Key dependencies:** CommonLibSSE-NG, DirectXTex (via vcpkg). Media Foundation ships with the Windows SDK — no vcpkg entry needed.
- **New CMake link targets for V4:** `mfplat`, `mf`, `mfreadwrite`, `mfuuid`, `strmiids`

## Architecture Overview

### Capture Formats

| Format | Type | Encoding | Max Duration |
|--------|------|----------|-------------|
| PNG / JPEG / BMP / TIFF / DDS | Still | WIC / DirectXTex | N/A |
| APNG | Animated | Differential delta, disk-based frame buffer | 15s |
| AGIF | Animated | Palette-based transparency delta | 15s |
| H264 (V4 new) | Video | Media Foundation `IMFSinkWriter` → MP4 | 120s |

### Core Classes & Files

- `ScreenCapture.cpp/h` — Top-level capture orchestration, Desktop Duplication frame grab, worker thread dispatch
- `VideoCapture.cpp/h` — **(V4 new)** Media Foundation H.264 encoder wrapper
- `PapyrusInterface.cpp` — Papyrus ↔ C++ bridge, native function registration, property getters/setters
- `*.psc` — Papyrus scripts for MCM (SkyUI Mod Configuration Menu) UI
- JSON config loader — settings persistence with multi-stage validation

### Threading Model

- Async worker thread for animated/video capture with Papyrus polling state machine
- SKSE task queue dispatch for thread-safe callbacks (not `ExecuteConsoleCommandOnMainThreadAndWait`)
- `MenuEventHandler` (`BSTEventSink<RE::MenuOpenCloseEvent>`) aborts captures on menu interruption
- `RE::UI::ShowMenus(bool)` hides HUD during capture — do NOT use per-menu Scaleform manipulation (mod ecosystem makes individual menu tracking unreliable)
- HUD is restored immediately after frame capture, before encoding

### Memory & Encoding

- `MemoryPool` with RAII for APNG differential encoding
- Disk-based frame buffer for animated captures (not full in-memory)
- Cooperative cancellation pattern across all animated/video paths

## V4 Video Capture — Design Contracts

### VideoCapture Class Interface

```cpp
bool Initialize(ID3D11Device*, const VideoCaptureConfig&);
bool EncodeFrame(ID3D11Texture2D*, uint64_t frameIndex);
bool Finalize();    // normal completion
void Abort();       // cooperative cancellation — destructor auto-aborts if not finalized, deletes partial MP4
```

### Critical Requirements

1. **`D3D11_CREATE_DEVICE_VIDEO_SUPPORT`** must be set on the existing `D3D11CreateDevice` call in the Desktop Duplication init. Without it, hardware encoder MFTs silently refuse to bind. Verify this flag exists before any other V4 work.
2. **`ID3D10Multithread::SetMultithreadProtected(TRUE)`** — called in `Initialize`, required for HW encoder.
3. **Input format is BGRA8 (`MFVideoFormat_ARGB32`)** — matches Desktop Duplication output. Sink writer auto-inserts a GPU color-conversion MFT to NV12.
4. **Timestamp formula:** `(frameIndex * 10'000'000) / fps` — NOT `frameIndex * (10'000'000 / fps)` (the second form accumulates rounding drift).
5. **Streams directly to disk** via sink writer — no in-memory frame buffering (unlike APNG/AGIF paths).
6. Shares the Desktop Duplication D3D11 device — no cross-device copy.

### Encoder Selection Logic

Media Foundation auto-selects the best available encoder:
- NVENC (Nvidia), AMF (AMD), QuickSync (Intel), or software fallback
- Controlled by `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS` on the sink writer factory
- User-facing preference: Auto / Prefer Hardware / Force Software

### Rate Control

Mapped via `CODECAPI_AVEncCommonRateControlMode`: CBR, VBR, CQP. When CQP is active, bitrate is meaningless — grey it out in MCM.

## V4 MCM Settings

### ImageType Enum

`H264` added alongside existing PNG/JPEG/BMP/TIFF/DDS/APNG/AGIF.

### Video Settings Page (MCM page 2)

Order reflects user mental model — "what am I capturing" then "how is it encoded":

1. Duration (1–120s, default 30s)
2. Target Resolution (Native / 1080p / 720p)
3. Frame Rate (30 / 60, default 60)
4. Quality Preset (Low / Medium / High / Very High / Custom, default High)
5. Bitrate in kbps (500–100,000; greyed out unless Quality Preset = Custom)
6. Keyframe Interval (1–10s, default 2s)
7. Encoder Preference (Auto / Prefer Hardware / Force Software)
8. Rate Control (CBR / VBR / CQP, default VBR)
9. Container (MP4 only; MKV reserved for future — falls back to MP4 with log warning)

### Quality Preset → Bitrate (1080p60 baseline, scale linearly with pixel count × framerate)

- Low: 8 Mbps
- Medium: 16 Mbps
- High: 25 Mbps
- Very High: 50 Mbps

### MCM Interaction Rules

- Quality Preset selects bitrate internally; Bitrate slider only enabled when preset = Custom
- Rate Control = CQP → grey out Bitrate
- ImageType = H264 → grey out still-image fields (JPEG quality, DDS format)
- ImageType switch re-ranges Duration slider (15s for APNG/AGIF, 120s for H264) — silent-clamp with `Debug.Notification`

## JSON Validation Pipeline

Five stages, in order:

1. File exists → if not, write defaults and reload
2. Valid JSON → if not, write defaults and reload
3. All parameters present → if not, write defaults and reload
4. **Per-field absolute bounds** (value sane in isolation)
5. **Cross-field validation** (value sane given other values — `ClampDurationForType` lives here)

New per-field bounds for V4:
- `Bitrate`: 500–100,000
- `FrameRate`: 15–120
- `KeyframeInterval`: 1–10
- `Duration`: 1–120 (absolute; per-type clamping is stage 5)

Duration limits are `constexpr float` constants — single source of truth via `ClampDurationForType(ImageType, float)`.

**Stage 5 must also run in `OnPlayerLoadGame`** after property restore — saves can drift across versions.

## V4 Integration Checklist

Files to modify:
- `ScreenCapture.cpp/h` — H264 code path, VideoCapture ownership, per-frame loop, abort threading (mirror existing APNG worker-thread pattern)
- `PapyrusInterface.cpp` — new properties, ImageType enum entry, cross-field clamp
- Papyrus `.psc` — MCM page 2, conditional slider ranges, grey-out logic
- JSON config loader — new fields in stages 4 and 5
- Default JSON writer — new fields with sensible defaults
- `OnPlayerLoadGame` — add `ValidateLoadedConfig()` after property restore
- `CMakeLists.txt` — add MF link targets

## Conventions

- Complete corrected files preferred over diff patches
- Per-type duration limits as `constexpr float` — no magic numbers
- All Papyrus native functions registered through the existing `PapyrusInterface.cpp` pattern
- Error paths: log via the existing SKSE log mechanism, never silently swallow
- Naming: `RE::` namespace types for all Skyrim engine interaction

## Excluded from Builds

Do not read or modify anything under:
- `build/` and `vcpkg_installed/`
- `.git/`
- `Crash logs/`
- `review/`
- `.claude/`
- `pipeline_output/`
- `build_logs/`

Source file extensions: `.cpp`, `.h`, `.hpp`, `.psc`, `.inl`

## Out of Scope (Future)

- MKV container (v4.1 — crash-recovery scenario)
- NV12 pre-conversion compute shader (perf optimization)
- HEVC codec support
- Audio capture (loopback + audio stream to sink writer)
