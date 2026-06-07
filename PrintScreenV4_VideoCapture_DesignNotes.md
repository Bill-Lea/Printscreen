# PrintScreen V4 — Video Capture Design Notes

Design decisions and rationale for adding H.264 video capture to the PrintScreen SKSE plugin. Covers encoder selection, user-facing parameters, MCM layout, validation flow, C++ architecture, and integration plan.

---

## 1. Encoder Selection: Media Foundation + H.264 + MP4

**Decision:** Use Microsoft Media Foundation (MF) with the `IMFSinkWriter` API, encoding H.264 into an MP4 container.

**Why Media Foundation:**
- Native Windows API — no external dependency to bundle with the Nexus release
- GPU-agnostic: auto-selects NVENC (Nvidia), AMF (AMD), QuickSync (Intel), or software fallback based on what's available, without vendor-specific code paths
- Accepts `ID3D11Texture2D` directly via `MFCreateDXGISurfaceBuffer`, so Desktop Duplication output flows in without a CPU roundtrip
- No licensing or LGPL paperwork

**Why H.264 specifically:**
- Universal playback (Nexus, Discord, YouTube, every video player)
- All hardware encoders support it — HEVC is patchier on older AMD cards
- No patent/redistribution concerns at the codec level for this use case

**Alternatives rejected:**
- FFmpeg/libav — too heavy for an SKSE plugin, LGPL baggage
- Direct NVENC/AMF/QSV SDKs — defeats the GPU-agnostic goal
- Windows.Media.Transcoding (WinRT) — awkward interop from SKSE

---

## 2. User-Facing Parameters

### Main MCM page
- **ImageType** — gains new value `H264` alongside existing PNG/JPEG/BMP/TIFF/DDS/APNG/AGIF
- **Duration** — shared across types; clamped per-type in validation

### Video Settings (second MCM page)
Order top-to-bottom matches user mental model ("what am I capturing" then "how is it encoded"):

1. Duration
2. Target Resolution (Native / 1080p / 720p)
3. Frame Rate (30 / 60)
4. Quality Preset (Low / Medium / High / Very High / Custom)
5. Bitrate (kbps) — *greyed out unless Quality Preset = Custom*
6. Keyframe Interval (seconds)
7. Encoder Preference (Auto / Prefer Hardware / Force Software)
8. Rate Control (CBR / VBR / CQP)
9. Container (MP4; MKV reserved for future)

### Parameters deliberately NOT exposed
- Codec picker (commit to H.264 for v1)
- Pixel format / color space
- H.264 profile / level
- B-frame count, reference frames, other tuning knobs

### Interaction rules
- Quality Preset + Bitrate: preset selects bitrate internally; Bitrate slider only enabled when preset = Custom
- Rate Control = CQP: greys out Bitrate (meaningless for quality-based encoding)
- ImageType = H264: greys out still-image fields (JPEG quality, DDS format)
- ImageType switch re-ranges Duration slider (15s for APNG/AGIF, 120s for H264)

---

## 3. Duration Handling

**Decision:** Single shared `Duration` parameter across all ImageTypes; clamp per-type in validation.

- APNG / AGIF: max 15s
- H264: max 120s
- Stills: parameter hidden/irrelevant

**MCM behavior:** On `ImageType` change, `OnOptionSelect` re-invokes slider setup with new max. Silent-clamp the current value when switching down (e.g. H264 @ 90s → APNG forces 15s) but show a `Debug.Notification` so the user sees the change.

**C++ side:** Single source of truth is `ClampDurationForType(ImageType, float)`. Per-type limits as `constexpr float` constants in one place.

---

## 4. JSON Validation Flow

Existing flow extended with one new stage:

1. Check JSON file exists → if fail, write default, reload
2. Check JSON is valid → if fail, write default, reload
3. Check all parameters present → if fail, write default, reload
4. **Sanitize each value (per-field absolute bounds)**
5. **Cross-field validation (NEW — type-dependent clamps)**
6. Set properties

### Per-field absolute bounds (stage 4) for new video fields
- `Bitrate`: 500–100,000 kbps
- `FrameRate`: 15–120
- `KeyframeInterval`: 1–10 seconds
- `Duration`: 1–120 (absolute max across all types)

Stage 4 stays the "is this value sane in isolation" gate. Stage 5 is the "is this value sane given the other values" gate. Clean separation; `ClampDurationForType` is called in stage 5 and becomes the single cross-field decision point.

**Must also run stage 5 in `OnPlayerLoadGame`** after property restore — persisted saves can drift out of range across versions.

---

## 5. C++ Architecture

New class `Printscreen::VideoCapture` encapsulates all MF interaction. Files delivered:

- `src/VideoCapture.h`
- `src/VideoCapture.cpp`
- Updated root `CMakeLists.txt` (adds `mfplat / mf / mfreadwrite / mfuuid / strmiids`)
- `vcpkg.json` — **no changes** (MF ships with Windows SDK)

### Class interface
```cpp
bool Initialize(ID3D11Device*, const VideoCaptureConfig&);
bool EncodeFrame(ID3D11Texture2D*, uint64_t frameIndex);
bool Finalize();                    // normal completion path
void Abort();                       // cooperative-cancellation path
```

### Key design points
- **Shares the Desktop Duplication D3D11 device** — no cross-device copy
- **Enforces `ID3D10Multithread::SetMultithreadProtected(TRUE)`** in `Initialize` (HW encoder requirement)
- **Input format: BGRA8 (MFVideoFormat_ARGB32)** — matches Desktop Duplication output directly. Sink writer inserts a GPU color-conversion MFT to produce NV12 for the encoder. Future optimization: manual NV12 conversion shader.
- **Timestamp math:** `(frameIndex * 10'000'000) / fps`, not `frameIndex * (10'000'000 / fps)` — second form accumulates rounding drift over long captures
- **Destructor auto-aborts** if not finalized, removes the partial MP4 from disk
- **Streaming to disk** via sink writer — no full in-memory frame buffering, unlike the APNG/AGIF paths
- **Rate control modes** mapped via `CODECAPI_AVEncCommonRateControlMode` attributes passed through `SetInputMediaType`
- **Hardware/software selection** via `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS` on the sink writer factory

### Critical prerequisite
The existing D3D11 device **must be created with `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`**. Without it, hardware encoder MFTs silently refuse to bind and everything falls back to software. Verify the `D3D11CreateDevice` call in the Desktop Duplication init path.

### MKV status
Enum entry exists but falls back to MP4 with a log warning. MF's built-in sink writer doesn't speak Matroska. Revisit in v4.1 if demand warrants — options are FFmpeg (LGPL baggage) or a custom `IMFMediaSink`.

---

## 6. Integration / Wiring Plan

**Approach:** Use Claude Code for the multi-file wiring. The contracts (VideoCapture interface, parameter list, validation stages, duration limits) are now fixed; the integration is mechanical but touches enough files that patch-by-patch edits risk drift.

### Files to be modified
- `ScreenCapture.cpp/h` — new H264 code path, VideoCapture ownership, per-frame loop, abort threading
- `PapyrusInterface.cpp` — new properties, ImageType enum entry, cross-field clamp call
- Papyrus `.psc` — matching properties, MCM page 2 setup, conditional slider ranges, grey-out logic
- JSON config loader — new fields in stage 4 and stage 5
- Default JSON writer — new fields with sensible defaults
- `OnPlayerLoadGame` — add `ValidateLoadedConfig()` call after property restore

### Inputs to provide to Claude Code
1. `VideoCapture.h` and `VideoCapture.cpp` (interface contract)
2. Current `ScreenCapture.cpp` (mirror APNG worker-thread pattern — closest analog)
3. Current `PapyrusInterface.cpp` and matching `.psc` (property/registration style)
4. Current JSON loader (extend, don't parallel)
5. Task brief specifying the duration limits, parameter list, and MCM page layout from this document

### Human-in-the-loop check
Verify `D3D11_CREATE_DEVICE_VIDEO_SUPPORT` on the existing `D3D11CreateDevice` call — one-line change, easy to miss in a large diff, critical for hardware encoding to work.

---

## 7. Open Items / Future Work

- **MKV support** (v4.1+) — recoverable container for crash-during-capture scenarios
- **NV12 pre-conversion shader** — perf optimization for high-resolution captures, eliminates the color-conversion MFT
- **HEVC support** — better compression; wait for demand and confirm AMD encoder coverage
- **Audio capture** — out of scope for v4; would add significant complexity (loopback capture + audio stream to sink writer)
- **Long-capture robustness** — stress-test 120s captures against the existing `MenuOpenCloseEvent` abort path to confirm clean file cleanup

---

## 8. Quick Reference — Defaults

| Parameter           | Default              | Range            |
|---------------------|----------------------|------------------|
| Duration (H264)     | 30s                  | 1–120s           |
| Resolution          | Native               | Native/1080p/720p |
| Frame Rate          | 60                   | 15–120           |
| Quality Preset      | High                 | Low/Med/High/VH/Custom |
| Bitrate (1080p60)   | 16,000 kbps          | 500–100,000      |
| Keyframe Interval   | 2s                   | 1–10s            |
| Encoder Preference  | Auto                 | Auto/PreferHW/ForceSW |
| Rate Control        | VBR                  | CBR/VBR/CQP      |
| Container           | MP4                  | MP4 (MKV future) |

Quality preset → bitrate mapping (1080p60 baseline, scale linearly with pixel count × framerate):
- Low: 8 Mbps
- Medium: 16 Mbps
- High: 25 Mbps
- Very High: 50 Mbps
- Custom: user-supplied
