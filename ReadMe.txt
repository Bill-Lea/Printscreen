# PrintScreen Mod Documentation for Papyrus scripts

## Overview
PrintScreen is a Skyrim mod that provides enhanced screenshot functionality with configurable options through MCM (Mod Configuration Menu). The mod allows players to take screenshots in various image formats, with customizable paths, compression settings, and hotkeys, while also providing the option to automatically hide the HUD/menu when taking screenshots.

## Core Components

### Script Architecture
The mod consists of six primary script files that work together to provide the screenshot functionality:
- PrintScreen_Quest_Script: Main quest script handling core functionality
- PrintScreen_mcm_script: MCM interface implementation
- PrintScreen_MAP_Script: Key mapping functionality
- PrintScreen_formula_Script: Native screenshot function implementation
- PrintScreen_ME_Script: Magic effect script for mod information display
- PSV4_PlayerRef_Script: Player reference handling

## Detailed Component Documentation

### PrintScreen_Quest_Script
This is the main quest script that handles the core functionality of the mod.

#### Properties
- `TakePhoto`: Integer storing the keycode for screenshot hotkey
- `ImageType`: String storing the selected image format
- `Path`: String storing the screenshot save location
- `Menu`: Boolean controlling automatic HUD/menu removal
- `bConfigOpen`: Boolean tracking MCM menu state
- `Compression`: Float controlling image compression (50.0-100.0)

#### Key Functions
- `OnInit()`: Initializes default values for all properties
- `OnKeyUp()`: Handles screenshot capture logic when hotkey is pressed

#### Screenshot Process Flow
1. Checks if MCM is open or text input is active
2. If screenshot hotkey is pressed:
   - Optionally removes HUD/menu
   - Calls PrintScreen function
   - Handles success/failure notifications
   - Maintains screenshot counter

### PrintScreen_mcm_script
Implements the MCM interface for mod configuration.

#### Configuration Options
- Path Setting: Customizable screenshot save location
- Image Type Selection: PNG, BMP, TIF, JPG, GIF
- Menu Toggle: Enable/disable automatic HUD removal
- Compression Setting: Quality control for JPG/TIFF (50-100)
- Hotkey Configuration: Customizable screenshot key

#### Events
- `OnConfigInit()`: Sets up MCM pages and initial registration
- `OnPageReset()`: Handles MCM page layout and options
- `OnOptionSelect()`: Processes toggle options
- `OnOptionMenuAccept()`: Handles menu selection changes
- `OnOptionSliderAccept()`: Processes compression value changes

### PrintScreen_MAP_Script
Provides key mapping functionality with a comprehensive key code to key name mapping system.

#### Key Mapping Features
- Maps all standard keyboard keys
- Maps mouse buttons and wheel
- Maps numpad keys
- Maps function keys (F1-F12)
- Maps special keys (Ctrl, Alt, Shift)

### PrintScreen_formula_Script
Native implementation of the screenshot functionality.

#### Function
```papyrus
string Function PrintScreen(String Path, String ImageType, float Compression) global native
```

#### Parameters
- `Path`: Directory path for saving screenshots
- `ImageType`: Format of the screenshot (PNG, BMP, TIF, JPG, GIF)
- `Compression`: Quality setting for compressed formats (50.0-100.0)

#### Returns
- "Success" on successful capture
- Error message string on failure

### PrintScreen_ME_Script
Handles the display of mod information through magic effects.

#### Features
- Displays current mod version
- Shows current configuration settings
- Provides feedback on current key mappings

### PSV4_PlayerRef_Script
Manages player reference and item additions.

#### Functions
- Handles initial item distribution to player
- Manages player reference aliases

## Installation Requirements
- Skyrim Special Edition
- SKSE64
- SkyUI
- JContainers

## Technical Notes

### Performance Considerations
- Screenshot capture process is asynchronous
- HUD/menu removal is handled through console commands
- Key registration is managed through SKSE

### Known Limitations
- Text input must be inactive for screenshots
- MCM must be closed for hotkey to function
- Path must be valid and accessible
- Compression only affects JPG and TIFF formats

### Debugging Tips
- Check debug notifications for shot counter
- Monitor return values from PrintScreen function
- Verify path accessibility if captures fail
- Check for key binding conflicts in MCM

## Version History
Current Version: 4.0.0
- Added compression control
- Enhanced format support
- Improved error handling
- Added automatic HUD removal option

## Best Practices
1. Set appropriate compression values for JPG/TIFF (85-95 recommended for quality)
2. Use absolute paths for screenshot directory
3. Avoid key bindings that conflict with common game functions
4. Test screenshot path before extended use


# Desktop Screen Capture Implementation
A C++ implementation for capturing desktop screenshots using DirectX 11 and Windows Imaging Component (WIC).

## Overview
The `PrintScreen` function provides a robust method for capturing desktop screenshots in various image formats. It utilizes DirectX 11 for screen capture and WIC for image encoding, supporting multiple output formats with configurable compression.

## Features
- Multiple output formats: BMP, GIF, TIF, JPG, PNG
- Configurable compression for JPEG and TIFF formats
- Automatic timestamp-based filename generation
- Error handling with detailed status messages
- Support for high-DPI displays

## Requirements
- DirectX 11
- Windows Imaging Component (WIC)
- COM environment
- C++17 or later (for std::filesystem support)

## Function Signature
```cpp
std::string PrintScreen(RE::StaticFunctionTag*, std::string basePath, std::string imageType, float compression)
```

### Parameters
- `basePath`: Directory where screenshots will be saved
- `imageType`: File format (bmp, gif, tif/tiff, jpg/jpeg, png)
- `compression`: Quality setting (0.0-100.0) for JPEG and TIFF formats

### Return Value
Returns an empty string on success or an error message describing the failure.

## Implementation Details

### Key Components
1. **Directory Management**
   - Creates output directory if it doesn't exist
   - Generates unique filenames using timestamps

2. **DirectX Setup**
   - Initializes D3D11 device and context
   - Creates DXGI output duplication chain
   - Handles desktop frame capture

3. **Image Processing**
   - Captures desktop frame to staging texture
   - Converts DirectX texture to WIC bitmap
   - Applies format-specific encoding

4. **Format Support**
- **JPEG**: Variable compression quality
- **TIFF**: ZIP compression with quality settings
- **PNG**: Lossless compression
- **BMP**: Uncompressed
- **GIF**: Basic encoding

## Error Handling
The implementation includes comprehensive error checking at each stage:
- Directory creation failures
- COM initialization issues
- DirectX device creation problems
- Frame capture failures
- Encoding errors

## Integration
The code integrates with Skyrim Script Extender (SKSE) via Papyrus bindings:
```cpp
vm->RegisterFunction("PrintScreen", "PrintScreen_Formula_Script", PrintScreen);
```

## Best Practices
1. Release COM resources properly
2. Handle multi-threading considerations
3. Check return values for error messages
4. Use appropriate compression values for quality-sensitive outputs

## Limitations
- Requires DirectX 11 compatible graphics hardware
- Windows platform specific
- Single monitor capture (primary display)
- BGRA pixel format requirement
