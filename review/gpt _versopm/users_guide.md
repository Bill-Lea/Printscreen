# PrintScreen v3 User Guide

## What this mod does

PrintScreen v3 is a Skyrim SKSE screenshot plugin with a Papyrus-driven front end. It lets you capture still images and animated captures from inside the game, with settings exposed through MCM and Papyrus. Based on the files reviewed, the plugin supports these output types:

* PNG
* BMP
* JPG
* TIF
* DDS
* GIF
* APNG
* AGIF

The C++ plugin registers native Papyrus functions, while the Papyrus scripts handle hotkeys, menu logic, polling, settings persistence, and user feedback.

## Main parts a player interacts with

### 1\. Screenshot hotkey

The main quest script stores a configurable screenshot key in `Key\_TakePhoto`. The current default in the script is key code `14`, which the included key map labels as **Backspace**. It is essential that this key is unique to the screen grabber. If it is shared the mod will not function. Pick an unused key using the MCM. 

&#x20;

### 2\. MCM settings page

The MCM script exposes settings for:

* output path
* image type
* automatic UI disable / HUD hide
* screenshot hotkey
* JPG quality
* TIF compression mode
* DDS compression mode
* animation duration
* frame rate
* loop count
* animated compression / optimization settings
* 

### 3\. Status messages

The mod reports status through Papyrus notifications and message boxes, including:

* success
* cancellation
* timeout
* invalid path
* missing dependencies

## Required runtime dependencies

From the Papyrus scripts, this mod expects these Skyrim-side dependencies to be present:

* SKSE
* ConsoleUtil
* PapyrusUtil / JsonUtil
* JContainers
* MCM support (the MCM script extends `SKI\_ConfigBase`)

If those are missing, the main quest script shows startup errors and the mod may not function correctly.



## First-time setup

### Step 1: Install the mod and dependencies

Install the plugin and Papyrus scripts with your normal mod manager workflow. Make sure the required Skyrim script/runtime dependencies listed above are also installed.



### Step 2: Start the game and let the mod initialize

On initialization, the main quest script checks for:

* SKSE
* ConsoleUtil
* JsonUtil / PapyrusUtil
* JContainers

If all checks pass, it registers the screenshot hotkey and mod events used to track capture completion.



### Step 3: Open the MCM

Open the mod configuration menu and review the settings page.

Recommended first changes:

* set a real screenshot folder path. The path must be absolute, free from illegal charicters and writable.



* choose your preferred file format
* confirm the screenshot hotkey
* decide whether automatic UI removal should be enabled. This can cause problems during long running captures: AOIF, APNG, DDS. If a window opens during one of these operations the game will seem to freeze. It's just waiting for you tp respond to the window which of course with the UI disabled you can't see.  You can either wait for the operation to compete or use the console to turn  on the UI. You open te console type TM<CR> and then close the cpnsole and close te window. The next cinfusing issue it that on completioh of the scsreen c apture operation the program will toggle the UI off again. This is a small problem jst use he console to turn it ba vk on again. 

## Everyday use

### Taking a normal screenshot

1. Make sure you are not in a text input or blocked menu state.
2. Press the configured screenshot key.
3. The Papyrus script  calls the native function `TakePhoto(...)`.
4. If the capture starts, the script begins polling for completion.
5. When finished, the script restores the HUD if needed and shows a notification.
6. 

### Taking an animated capture

For `AGIF` or `APNG`:

1. Select the animated output type in MCM.
2. Set duration and FPS.
3. Optionally adjust loop count, compression, and optimization.
4. Trigger the screenshot hotkey.
5. Wait for the longer background capture and encode cycle to finish.

Animated captures use a slower poll interval because they can take significantly longer than still-image captures.



## Output formats and when to use them

### PNG

Good default choice for lossless still screenshots.



### BMP

Simple uncompressed still image output.



### JPG

Smaller files with adjustable quality. Use when file size matters more than lossless quality.



### TIF

Still image output with selectable TIFF compression mode.



### DDS

Useful for workflows that need DDS output. The MCM script exposes multiple DDS compression modes. The menu text warns that some DDS modes, especially BC6H and BC7 variants, may take several minutes.



### GIF / AGIF

Animated capture output. The scripts expose duration, FPS, looping, compression, and optimization.



### APNG

Animated PNG capture. Similar user controls to AGIF, but with PNG-based animated output.



## Automatic HUD / UI hiding

The main quest script contains `HideHUD()` and `ShowHUD()` helpers and uses ConsoleUtil to run the hide/show behavior. In the MCM, this appears as **Automatic UI disable (Experimental)**.

What to expect:

* if enabled, the mod attempts to hide the HUD when capture begins
* once capture completes or fails, the script attempts to restore the HUD
* the script includes safety cleanup for state mismatches, timeouts, and cancelled captures

The option is marked experimental, use it carefully with your UI setup and mod list. It will not crash the game but you may have to use the console TM command if the game or a mod opens a window or menu while a capture is running.



## Configuration storage

This mod uses two configuration layers.

### In-game JSON settings

The Papyrus scripts persist user-facing settings such as:

* path
* image type
* key binding
* menu toggle
* JPG compression
* mode values
* DDS/TIF settings
* loop count
* FPS
* duration
* quality
* optimization
* compression

If the JSON path value is invalid, the script falls back to default behavior and warns the user.



### Plugin INI settings

The C++ plugin also creates or reads an INI at the Skyrim Special Edition SKSE folder. Based on the code, the preferred simple path is:

`Documents\\My Games\\Skyrim Special Edition\\SKSE\\PrintScreen.ini`

The plugin also checks a legacy plugin INI path:

`Documents\\My Games\\Skyrim Special Edition\\SKSE\\Plugins\\Printscreen\_Log.ini`

The INI controls logging and some performance settings.



## Plugin INI options

The code exposes these modern INI keys:

### \[Logging]

* `LogLevel`
* `ConsoleOutput`
* `FileOutput`
* `ShowTimestamps`
* `MaxLogFileSizeMB`

### \[Performance]

* `ParallelCompression`
* `CompressionThreads`

### \[Capture]

* `LogCaptureProgress`
* `LogTimingInfo`

The plugin also still accepts legacy flat keys such as:

* `Level`
* `File`
* `Console`
* `Timestamps`
* `LogPath`

For new installs, the code creates defaults automatically and uses **DEBUG** logging by default.



## How capture state works

The Papyrus side is designed to avoid getting stuck between “started” and “finished.” It tracks states like:

* `Ready`
* `Starting`
* `Running`
* `Capture started`
* `Cancelling`
* callback success / saved states
* callback error states
* cancelled states

If something goes wrong, the script can:

* cancel an in-progress capture
* force-reset stale native state
* clean up HUD visibility
* recover from timeout

## 

## Cancelling or recovering from a stuck capture

The native interface exposes these functions:

* `Get\_Result()`
* `Cancel()`
* `ForceReset()`
* `ResetState()`

In practice, that means the mod has built-in recovery hooks for interrupted or stuck capture sessions. If screenshots stop responding, the likely recovery path is:

1. exit combat / menus / overlays
2. wait for polling to complete
3. retry once
4. if state remains stuck, use the mod's reset path if exposed in your UI/testing flow
5. restart the game if the native and Papyrus states are badly out of sync.
6. 

## Known practical cautions from the source

* Long-running formats such as animated output and heavier DDS modes may take noticeably longer.
* Automatic UI disable is marked experimental.
* The scripts deliberately avoid firing while text input or certain menu states are active.
* Path validation is strict. Invalid output paths will block capture.
* This source set includes functionality, but not a packaged installer or full end-user distribution layout, so some install details depend on the mod package structure outside these files.

## Quick-start recommendation

If you just want reliable screenshots:

1. set output type to **PNG**
2. set a valid output path
3. leave UI disable off until tested
4. confirm the hotkey
5. take one still screenshot first
6. only then experiment with DDS or animated formats
7. 

## Troubleshooting checklist

### Nothing happens when the key is pressed

Check:

* SKSE is installed and loading
* ConsoleUtil is installed
* JContainers is installed
* PapyrusUtil / JsonUtil is installed
* the chosen hotkey is not conflicting badly with another mod
* you are not in a blocked text-input or menu state
* 

### “Invalid path” behavior

Set a simpler folder path first, then test again.



### HUD stays hidden

This usually points to an interrupted capture or state mismatch. Wait for timeout or completion first, then toggle UI state with console "TM". Disable automatic UI removal if the behavior is unstable in your load order.



### Animated capture is very slow

Reduce:

* duration
* optimization level
* compression complexity
* 

### DDS capture is very slow

Use a lighter DDS mode first. The MCM text explicitly warns that some BC6H / BC7 modes may take several minutes.



## Summary

For players, the intended flow is simple:

* configure the mod in MCM
* choose an output type and folder
* press the hotkey to capture
* use animated and DDS modes only after a basic still-image setup is working

The source shows a thoughtful design with native capture on the C++ side and state management on the Papyrus side, especially around completion polling, cancellation, and HUD restoration.

