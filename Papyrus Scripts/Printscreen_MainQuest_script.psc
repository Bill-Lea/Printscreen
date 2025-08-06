Scriptname Printscreen_MainQuest_script extends Quest  

bool bResult = false
int Property Key_TakePhoto auto  
int Property KeyMapCode auto
String Property ImageType auto 
String Property Path auto
float Property GIF_MultiFrame_Duration Auto 

bool Property IsLatentScreenshotActive = false Auto
bool property bConfigOpen auto
float property JPG_Compression auto
String Property Mode auto
string Property Tif_Mode Auto
String Property DDS_Mode auto 
String Property Result auto  
Bool Property Menu = true auto 
bool Property HUD_Visable = true Auto
int Property Shots auto
String Property Version ="2.0.0" auto 
bool   Property Read_Write_Configead_Write_Config auto
String Property JsonFileName auto
String Property KeyName_Path auto
String Property KeyName_TakePhoto auto
String Property Keyname_Menu auto
String Property KeyName_JPG_Compression auto
String Property KeyName_Mode auto
String Property KeyName_DDS_Mode auto
String Property KeyName_TIF_Mode auto
String Property KeyName_ImageType auto
String Property KeyName_UseJsonFile  auto
String Property Keyname_HUD_Control  Auto
String Property KeyName_GIF_Duration Auto
bool Property UseJsonFile=false  auto

; ADDED: Track if we're in the middle of starting a capture
bool Property IsStartingCapture = false Auto Hidden

; ADDED: For key debouncing
float Property LastKeyPressTime = 0.0 Auto Hidden

; Function to check if ConsoleUtil is available
bool Function CheckConsoleUtil() 
    int Vn = ConsoleUtil.GetVersion()
    if (Vn == 0)
        Debug.MessageBox("ERROR: ConsoleUtil not detected. PrintScreen requires ConsoleUtil for HUD toggling.")
        return false
    endif
    return true
EndFunction

; Function to check if JsonUtil is available
bool Function CheckJsonUtil()
    int vr = papyrusUtil.GetScriptVersion()
    if Vr != 0 
        return true
    else 
        return false
    endif
EndFunction

bool Function JContainersCheck()
    bool isLoaded = false
    int Av = 4 
    
    if(JContainers.APIVersion() == Av)
        isLoaded = true
        return isLoaded
    endif 

    int testMap = JMap.object()
    if testMap != 0
        isLoaded = true
        JValue.release(testMap)
    endif
    
    return isLoaded
EndFunction

function Sanatize_ImageType()
    If(Imagetype == "png" || Imagetype == "PNG")
        ImageType = "PNG"
        return
    elseif(Imagetype == "BMP" || Imagetype == "bmp")
        ImageType = "BMP"
        return
    elseif(Imagetype == "JPEG" || Imagetype == "JPG" || Imagetype == "jpg" || Imagetype == "jpeg")
        ImageType = "JPG"
        return
    elseif(ImageType == "GIF" || ImageType == "gif")
        ImageType = "GIF"
        return
    elseif(Imagetype == "TIF" || Imagetype == "TIFF" || Imagetype == "tif" || Imagetype == "tiff")
        ImageType = "TIF"
        Return
    Elseif(ImageType == "DDS" || ImageType == "dds")
        ImageType = "DDS"
        return
    elseif(ImageType == "GIF_MULTIFRAME" || ImageType == "gif_multiframe" || ImageType == "GIF_Multiframe" || ImageType == "gif_multiframe")
        ImageType= "GIF_MULTIFRAME"
        return
    else
        ImageType = "PNG"
        return
    endif 
endFunction

function Sanatize_TIF_MODE()
    if(Tif_Mode == "RLE")
        return
    elseif(Tif_Mode == "LZW")
        return
    elseif(Tif_Mode == "ZIP")
        return
    else
        Tif_Mode = "UNCOMPRESSED"
        return
    endif   
endFunction

fUNCTION sANATIZE_dds_Mode()
    if(DDS_Mode == "UNCOMPRESSED")
        return
    elseif(DDS_Mode == "BC7_FAST")
        DDS_Mode = "BC7_FAST"
        return
    elseif(DDS_Mode == "BC1")
        return
    elseif(DDS_Mode == "BC2")
        return
    elseif(DDS_Mode == "BC3")       
        return
    elseif(DDS_Mode == "BC4")
         return
    elseif(DDS_Mode == "BC5")
        return
    elseif(DDS_Mode == "BC6H")
        return
    elseif(DDS_Mode == "BC7_SLOW")
        return
    elseif(DDS_Mode == "BC7_NORMAL")  
                return
    elseif(DDS_Mode == "BC7_Fast")
        return 
    else
        DDS_Mode = "BC1"
        Return
    endif
endFunction

function Sanatize_Mode()
    if(Mode == "UNCOMPRESSED")
        return
    ElseIF(Mode == "BC7_FAST")
        Mode = "BC1"
        return
    elseif(Mode == "BC2")
        return
    elseif(Mode == "BC3")       
        return
    elseif(Mode == "BC4")
        return
    elseif(Mode == "BC5")
        return
    Elseif(Mode == "BC6H")
        return
    elseif(Mode == "BC7_SLOW")
        return
    elseif(Mode == "BC7_NORMAL")  
        return
    elseif(Mode == "BC1")
        return
    elseif(Mode == "RLE")
        return
    elseif(Mode == "LZW")
        return
    elseif(Mode == "ZIP")
        return
    else
        Mode = "UNCOMPRESSED"
        return
    endif
    return
EndFunction

function HideHud()
    
    if(!HUD_Visable)
        return
    Else
    ConsoleUtil.ExecuteCommand("tm")
    HUD_Visable = false
    utility.wait(0.1)
    endif
EndFunction 

Function ShowHud()    
    if(HUD_Visable)
        return  
    Else 
    ConsoleUtil.ExecuteCommand("tm")
    HUD_Visable = true
    Endif 
EndFunction

; SIMPLIFIED: Capture function that doesn't duplicate state checks
Function CaptureImage(String path, String imageType, Float compression, String Mode, float GIF_MultiFrame_Duration)
    ; NOTE: IsStartingCapture is ALREADY set by the caller
    ; NOTE: All state checks are ALREADY done by the caller
    
    Debug.Trace("CaptureImage called - flags already set")
    
    if(Menu)
        HideHud()
        Utility.Wait(0.1)
    endif
    
    ; Direct call to native function
    String startResult = Printscreen_Formula_Script.TakePhoto(path, imageType, compression, Mode, GIF_MultiFrame_Duration)
    Debug.Trace("TakePhoto result: " + startResult)
  
    if (startResult == "Started")
        IsLatentScreenshotActive = true
        IsStartingCapture = false  ; Clear starting flag
        Utility.Wait(0.1)
        SendModEvent("PrintScreenPoll")
        
    elseif (startResult == "Already running")
        ; This should be extremely rare now
        Debug.Trace("Unexpected 'Already running' - state check failed")
        IsStartingCapture = false
        if (Menu)
            ShowHud()
        endif
        
    else
        ; Failed to start
        Debug.Notification("Failed to start screenshot: " + startResult)
        IsLatentScreenshotActive = false
        IsStartingCapture = false
        if (Menu)
            ShowHud()
        endif
    endif
EndFunction; Enhanced completion handler with better state cleanup
Function OnScreenshotCompleted(string resultMsg)
    Debug.Notification("Screenshot completed: " + resultMsg)
    
    if (resultMsg == "Success")
        Shots = Shots + 1
        Debug.Notification("Screenshot succeeded. Total Shots: " + Shots)
    elseif (resultMsg == "Cancelled")
        Debug.Notification("Screenshot cancelled")
    else
        Debug.Notification("Screenshot failed: " + resultMsg)
    endif
    
    ; FIXED: Always reset ALL state flags on completion
    IsLatentScreenshotActive = false
    IsStartingCapture = false
    
    ; FIXED: Also ensure native state is clean
    String finalNativeState = Printscreen_Formula_Script.Get_Result()
    Debug.Trace("Final native state after completion: " + finalNativeState)
    
    ; If native still thinks it's running after completion, force reset
    if (finalNativeState == "Running" || finalNativeState == "Starting")
        Debug.Trace("Native state still active after completion - force resetting")
        Printscreen_Formula_Script.ForceReset()
    endif
    
    if (Menu)
        ShowHud()
    endif
EndFunction

Event onInit()
    bool allPrerequisitesMet = true
    
    ; Check for SKSE
    int skseVersion = SKSE.GetVersion()
    if skseVersion == 0
        Debug.MessageBox("ERROR: SKSE not detected. PrintScreen requires SKSE to function.")
        allPrerequisitesMet = false
    endif
    
    ; Check for ConsoleUtil
    if !CheckConsoleUtil()
        allPrerequisitesMet = false
    endif
    
    ; Check for JsonUtil
    if !CheckJsonUtil()
        Debug.MessageBox("ERROR: JsonUtil not detected. PrintScreen requires JsonUtil for configuration storage.")
        allPrerequisitesMet = false
    endif
    
    ; Check for jContainer version valid
    if !JContainersCheck()
        Debug.MessageBox("ERROR: JContainers not detected. PrintScreen requires JContainers for configuration storage.")
        allPrerequisitesMet = false
    endif 
    
    ; If prerequisites aren't met, disable functionality
    if !allPrerequisitesMet
        Debug.MessageBox("PrintScreen will be disabled until all required mods are installed.")
        UnregisterForAllKeys()
        return
    endif 
    
    ; ADDED: Ensure clean state on script startup
    String resetResult = Printscreen_Formula_Script.ResetState()
    Debug.Trace("Initial state reset: " + resetResult)
    
    ; Register for our custom polling event
    RegisterForModEvent("PrintScreenPoll", "OnPrintScreenPoll")
    
    ; Define Default values    
    Shots = 0 
    Menu = true
    JPG_Compression = 85.0
    Tif_Mode = "UNCOMPRESSED"
    DDS_Mode = "UNCOMPRESSED"
    Result = ""
    bResult = false
    Hud_Visable = true
    IsLatentScreenshotActive = false
    IsStartingCapture = false
    LastKeyPressTime = 0.0  ; ADDED: Initialize debounce timer
    bConfigOpen = false 
    UseJsonFile = False
    JsonFilename = "Printscreen_Configure"
    
    KeyName_JPG_Compression = "Compression"
    Keyname_ImageType = "ImageType"
    KeyName_TakePhoto = "TakePhoto"
    KeyName_Path = "Path"
    Keyname_Menu = "Menu"
    KeyName_UseJsonFile = "UseJsonFile"
    KeyName_Mode = "Mode"
    KeyName_DDS_Mode = "DDS Mode"
    KeyName_TIF_Mode="TIF Mode"
    KeyName_GIF_Duration ="GIF MultiFrame Duration"
    
    ; Set defaults
    Menu = true
    Key_TakePhoto = 14
    Imagetype = "PNG"
    DDS_Mode = "UNCOMPRESSED"
    Tif_Mode = "UNCOMPRESSED"
    Mode = "UNCOMPRESSED"
    Path = "C:/Pictures" 
    JPG_Compression = 85.0
    GIF_MultiFrame_Duration =1
    Tif_Mode = "UNCOMPRESSED"
    DDS_Mode = "UNCOMPRESSED"
    Result = "Success"
    bResult = true
    IsLatentScreenshotActive = false
    IsStartingCapture = false
    bConfigOpen = false
    UseJsonFile = false
    
    ; Sanitize the ImageType and Mode
    Sanatize_ImageType()
    Sanatize_Mode()
    Sanatize_TIF_MODE()
    Sanatize_DDS_Mode() 

    RegisterForKey(Key_TakePhoto) 

    ; Validate initial path on startup using the new CheckPath function
    bResult = Printscreen_Formula_Script.CheckPath(Path)
    if(!bResult)
        ; Debug.MessageBox("Path validation failed on init")
    endif
      
    ; JSON logic handling
    if(!jsonUtil.jsonExists(JsonFilename))
        ; Create the json file
        JsonUtil.SetIntValue(JsonFilename, KeyName_TakePhoto, Key_TakePhoto)
        jsonUtil.SetStringValue(jsonFileName, KeyName_Path, Path)
        jsonUtil.SetFloatValue(jsonFileName, KeyName_JPG_Compression, JPG_Compression)
        jsonUtil.SetStringValue(jsonFilename, Keyname_Mode, Mode)
        JsonUtil.SetStringValue(jsonFilename, KeyName_TIF_Mode, Tif_Mode)
        jsonUtil.SetStringValue(jsonFilename, KeyName_DDS_Mode, DDS_Mode)
        jsonUtil.SetFloatValue(JsonFilename, KeyName_GIF_Duration, GIF_MultiFrame_Duration)
        if(menu)
            jsonUtil.SetIntValue(jsonFileName, Keyname_Menu, 1)
        else
            jsonUtil.SetIntValue(jsonFileName, KeyName_Menu, 0)
        Endif
        jsonUtil.SetStringValue(jsonFilename, KeyName_ImageType, ImageType)
        if(useJsonFile)
            jsonUtil.SetIntValue(jsonFileName, KeyName_UseJsonFile, 1)
        else
            jsonUtil.SetIntValue(jsonFileName, KeyName_UseJsonFile, 0)
        endif
        Debug.Notification("Json file initially created")
        jsonUtil.Save(jsonFileName, false)
    else
        ; JSON file exists check if good
        if(JsonUtil.isGood(jsonFilename) && (jsonUtil.getIntValue(jsonFileName, KeyName_UseJsonFile) == 1))
            ; Read values and assign to defaults
            JPG_Compression = JsonUtil.GetFloatValue(jsonFilename, KeyName_JPG_Compression)
            ImageType = JsonUtil.getStringValue(jsonFileName, Keyname_ImageType)
            Sanatize_ImageType()
            Mode = JsonUtil.getStringvalue(jsonFileName, KeyName_Mode)
            Sanatize_Mode() 
            Tif_Mode = JsonUtil.getStringValue(jsonFileName, KeyName_TIF_Mode)
            Sanatize_TIF_Mode() 
            DDS_Mode = JsonUtil.getStringValue(jsonFileName, KeyName_DDS_Mode)
            Sanatize_DDS_Mode() 

            GIF_MultiFrame_Duration = jsonUtil.GetFloatvalue(jsonFileName, KeyName_Gif_Duration)
            Tif_Mode = JsonUtil.getStringValue(jsonFileName, KeyName_TIF_Mode)
            DDS_Mode = JsonUtil.getStringValue(jsonFileName, KeyName_DDS_Mode)
            Key_TakePhoto = JsonUtil.getIntvalue(jsonFileName, KeyName_TakePhoto)
            if(jsonUtil.getintvalue(jsonFileName, Keyname_Menu) == 1)
                Menu = True
            else
                Menu = False
            endif
            If(JsonUtil.GetIntValue(jsonFilename, KeyName_UseJsonFile) == 1)
                useJsonFile = True
            else
                UseJsonFile = False
            Endif

            String TestPath = JsonUtil.GetStringValue(jsonFileName, KeyName_Path)
            bResult = Printscreen_Formula_Script.CheckPath(TestPath)                        
            if(!bResult)
                ; Debug.MessageBox("Printscreen - JSON Path failed")
            else
                ; Debug.MessageBox("Path validated successfully")
                Path = TestPath
            endif
        endif 
    endif
    
    ; Sanitize again after reading from JSON
    Sanatize_ImageType()    
    Sanatize_TIF_MODE()
    Sanatize_DDS_Mode()     
    Sanatize_Mode()
EndEvent

; BULLETPROOF: Key handling that prevents double calls but allows cancellation
Event OnKeyUP(int theKey, float holdtime)
    ; Basic validation
    If((theKey != Key_TakePhoto) || (bConfigOpen) || (ui.IsTextInputEnabled()))
        return
    Endif
    
    ; CANCEL CHECK: If already starting OR running, treat as cancel command
    if (IsStartingCapture || IsLatentScreenshotActive)
        Debug.Trace("=== CANCEL REQUEST DETECTED ===")
        Debug.Notification("Cancelling screenshot...")
        
        String cancelResult = Printscreen_Formula_Script.Cancel()
        Debug.Trace("Cancel result: " + cancelResult)
        
        ; Reset all state flags
        IsLatentScreenshotActive = false
        IsStartingCapture = false
        
        if (Menu)
            ShowHud()
        endif
        return
    endif
    
    ; Check native state for cancel too
    String nativeState = Printscreen_Formula_Script.Get_Result()
    if (nativeState == "Running" || nativeState == "Starting")
        Debug.Trace("=== CANCEL REQUEST (Native Running) ===")
        Debug.Notification("Cancelling screenshot...")
        
        String cancelResult = Printscreen_Formula_Script.Cancel()
        Debug.Trace("Cancel result: " + cancelResult)
        
        ; Reset state flags
        IsLatentScreenshotActive = false
        IsStartingCapture = false
        
        if (Menu)
            ShowHud()
        endif
        return
    endif
    
    ; DEBOUNCE CHECK: Only for rapid new start attempts (not cancels)
    float currentTime = Utility.GetCurrentRealTime()
    if (currentTime - LastKeyPressTime < 0.75)
        Debug.Trace("Key press debounced - too rapid for new screenshot")
        return
    endif
    
    ; ATOMIC: Set flag immediately to block duplicate start calls
    IsStartingCapture = true
    LastKeyPressTime = currentTime
    
    ; All checks passed - proceed with NEW screenshot
    Debug.Notification("Taking screenshot...")
    Debug.Trace("=== SINGLE KEY PRESS - STARTING NEW CAPTURE ===")
    
    ; Call the simplified capture function
    CaptureImage(Path, ImageType, JPG_Compression, Mode, GIF_MultiFrame_Duration)
EndEvent


; FIXED: Improved polling with better state management
Event OnPrintScreenPoll(string eventName, string strArg, float numArg, Form sender)
    ; FIXED: Check both flags for active state
    if (!IsLatentScreenshotActive && !IsStartingCapture)
        Debug.Trace("Polling called but no active screenshot - stopping poll")
        return
    endif
    
    Result = Printscreen_Formula_Script.Get_Result()
    Debug.Trace("Poll result: " + Result)
    
    if (StringUtil.Find(Result, "CALLBACK_") == 0)
        ; Handle completion callbacks
        if (Result == "CALLBACK_SUCCESS")
            OnScreenshotCompleted("Success")
        elseif (Result == "CALLBACK_CANCELLED")
            OnScreenshotCompleted("Cancelled")
        elseif (StringUtil.Find(Result, "CALLBACK_ERROR:") == 0)
            String errorMsg = StringUtil.Substring(Result, 15)
            OnScreenshotCompleted(errorMsg)
        endif
        
    elseif (Result == "Running" || Result == "Starting")
        ; Continue polling - FIXED: Handle "Starting" state
        Utility.Wait(0.2)  ; FIXED: Faster polling
        SendModEvent("PrintScreenPoll")
        
    elseif (Result == "Already running")
        ; FIXED: Handle this case gracefully - check native state
        Debug.Trace("Native reports 'Already running' - checking state...")
        
        ; If we don't think we're running, there's a state mismatch
        if (!IsLatentScreenshotActive && !IsStartingCapture)
            Debug.Notification("State mismatch detected - native thinks it's running but we don't")
            ; Try to get a fresh status
            Utility.Wait(0.5)
            SendModEvent("PrintScreenPoll")
        else
            ; We think we're running too, so continue polling
            Utility.Wait(0.5)
            SendModEvent("PrintScreenPoll")
        endif
        
    else
        ; Any other result means completion or error
        OnScreenshotCompleted(Result)
    endif
EndEvent