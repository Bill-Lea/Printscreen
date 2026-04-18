# PrintScreen V3 — User's Guide

## What Is PrintScreen V3?

PrintScreen V3 is a Skyrim Special Edition mod that replaces the game's basic screenshot functionality with a full-featured screen capture system. It supports seven image formats — including animated GIF and animated PNG — and is controlled entirely through an in-game MCM (Mod Configuration Menu) panel and a configurable hotkey.

## Requirements

Before installing PrintScreen V3, make sure the following are already installed and working:

- **SKSE** (Skyrim Script Extender) — the plugin will not load without it.
- **SkyUI** — required for the MCM settings panel.
- **ConsoleUtil** — used to toggle the HUD on and off during captures.
- **JContainers** — used for persistent JSON-based configuration storage.
- **PapyrusUtil** — provides the JsonUtil API for reading and writing settings.
- **Address Library for SKSE Plugins** — required by CommonLibSSE-NG.

PrintScreen V3 is compatible with Skyrim SE versions 1.5.39 through 1.6.1170. It uses Address Library for version-independent addressing.

## Installation

Install using a mod manager such as Mod Organizer 2 or Vortex. The mod deploys:

- An SKSE plugin DLL (`Printscreen.dll`) into `SKSE/Plugins/`.
- Papyrus scripts into `Scripts/`.
- MCM configuration resources (menu XML, translation file, splash images) into the appropriate `Interface/` folders.

On first launch after installation, a default INI file is created automatically at:

```
Documents\My Games\Skyrim Special Edition\SKSE\PrintScreen.ini
```

A JSON configuration file (`PrintScreenConfig.json`) is also created via JContainers on first run to persist your MCM settings across saves.

## Getting Started

1. Launch Skyrim and load a save.
2. Open the MCM menu (via SkyUI's System menu) and select **PrintScreen**.
3. On the **Settings** page you will see a splash image on the title page and all configuration options on the Settings tab.
4. Press the configured hotkey (default: **Backspace**) to take a screenshot.

## MCM Settings Reference

### Path

The filesystem path where images are saved. Must be an absolute Windows path (e.g. `C:\Pictures\Skyrim`). If the directory doesn't exist, PrintScreen will attempt to create it. The path is validated with a read/write probe test when you enter it.

If your path is longer than 30 characters, the MCM displays "Json Long Path Option" and path editing is disabled in the MCM — edit the path directly in the JSON config file instead.

### Image File Type

Choose one of eight formats:

| Format | Extension | Notes |
|--------|-----------|-------|
| **PNG** | .png | Lossless, default choice |
| **APNG** | .png | Animated PNG — multi-frame, lossless |
| **BMP** | .bmp | Uncompressed bitmap |
| **TIF** | .tif | TIFF with optional compression |
| **JPG** | .jpg | Lossy, adjustable quality |
| **GIF** | .gif | Static single-frame GIF |
| **AGIF** | .gif | Animated GIF — multi-frame |
| **DDS** | .dds | DirectX texture with BC compression |

When you change the image type, the MCM automatically enables and disables the relevant sub-options (e.g. JPG quality slider, DDS compression mode, animation duration).

### Take Photo Key

The hotkey that triggers a capture. Default is **Backspace** (keycode 14). Choose any unbound key. While a capture is in progress, pressing the hotkey again cancels it.

### Automatic UI Disable (Experimental)

When enabled, the HUD is automatically hidden before a capture and restored afterward using the `tm` console command. This is marked experimental because it toggles all UI elements.

### JPG Quality

Available when image type is JPG. A slider from 0 to 100 controlling JPEG compression quality. Higher values produce larger, better-looking files. Default is 90.

### TIF Compression Mode

Available when image type is TIF. Options are Uncompressed, RLE, LZW, or ZIP.

### DDS Compression Mode

Available when image type is DDS. Options include Uncompressed, BC1 through BC5, BC6H, and BC7 (in Slow, Normal, and Fast variants). BC7 modes produce the highest quality but take noticeably longer to encode — several seconds per capture.

### Capture Duration (Animated Only)

Available for AGIF and APNG. Controls how long the capture runs, from 1 to 15 seconds. The frame rate is automatically calculated from the duration (shorter durations get higher FPS, longer durations get lower FPS to keep file sizes manageable).

### Loop Count (Animated Only)

How many times the animated image loops on playback. 0 means infinite looping.

### Differential Compression (Animated Only)

Set to 0 for full-frame encoding (every frame stores the complete image) or 1 for differential encoding (only the changed region of each frame is stored). Differential encoding produces significantly smaller files.

### Optimize (Animated Only)

Set to 0 for region-extraction-only differential encoding, or 1 for true delta encoding. True delta encoding marks unchanged pixels as transparent within the changed region, achieving much better compression. Only meaningful when Differential Compression is also set to 1.

### Differential Quality (Animated Only)

A quality factor from 0 to 100 for animated captures. Controls the balance between file size and visual quality.

## How Capture Works

When you press the hotkey:

1. If "Automatic UI Disable" is on, the HUD is hidden.
2. The C++ plugin receives the capture request and launches a background worker thread.
3. For single-frame formats (PNG, JPG, BMP, TIF, DDS, static GIF), one frame is captured via DirectX Desktop Duplication and saved immediately.
4. For animated formats (AGIF, APNG), frames are captured at the computed FPS and stored to temporary BMP files on disk to avoid exhausting memory. After all frames are captured, they are encoded into the final animated file and the temp files are cleaned up.
5. The HUD is restored and a notification appears with the result.

The entire capture runs asynchronously. Skyrim remains responsive during the process.

## Cancelling a Capture

Press the hotkey again while a capture is in progress. The cancel signal is cooperative — the worker thread checks for cancellation at many points during the pipeline and stops as soon as possible. After cancellation, the HUD is restored and partial temporary files are cleaned up.

## Configuration Files

### INI File

Located at `Documents\My Games\Skyrim Special Edition\SKSE\PrintScreen.ini`. Controls logging behavior and performance tuning. Sections:

- **[Logging]** — LogLevel (0–5, default DEBUG for new installs), ConsoleOutput, FileOutput, ShowTimestamps, MaxLogFileSizeMB.
- **[Performance]** — ParallelCompression (true/false), CompressionThreads (0 = auto).
- **[Capture]** — LogCaptureProgress, LogTimingInfo.

A legacy flat-format INI (with keys like `Level`, `File`, `Console`) is also supported for backward compatibility.

### JSON Config

Managed automatically by the Papyrus scripts via JContainers' JsonUtil. Stores all MCM settings (path, image type, compression modes, duration, FPS, etc.) so they persist across game sessions. The file is written every time you close the MCM.

## Troubleshooting

**"Invalid screenshot path!"** — The configured path failed validation. Open the MCM and enter a valid absolute path, or edit the JSON config file directly.

**Capture hangs or times out** — A built-in 300-second (5-minute) timeout automatically cancels stalled captures. If this happens repeatedly, check the SKSE log for error messages. DDS BC7 compression on large resolutions can legitimately take a long time.

**HUD stuck hidden after a crash** — On the next game load, PrintScreen automatically resets the HUD state and runs `ForceReset` on the native plugin. If the HUD is still hidden, open the console and type `tm`.

**Black or washed-out PNG screenshots** — PrintScreen V3 uses manual WIC encoding for PNG to ensure correct BGRA color handling, which should prevent this. If you see color issues, check that no other screenshot overlay (like Steam's) is interfering.

**Large animated file sizes** — Enable Differential Compression (set to 1) and Optimize (set to 1) for the smallest animated files. Shorter durations also help.

## Version History

- **v3.0.0** — Current release. Multi-format capture (PNG, JPEG, BMP, TIFF, DDS, GIF, AGIF, APNG), differential and true-delta encoding for animations, MemoryPool integration, disk-based frame buffering, cooperative cancellation, async worker thread architecture, MCM configuration panel with SkyUI.
