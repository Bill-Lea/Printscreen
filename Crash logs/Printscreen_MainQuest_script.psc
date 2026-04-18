Scriptname Printscreen_MainQuest_script extends Quest

Import StringUtil
Import Debug
Import Utility
Import Input
Import UI
string Property DDS_Mode = "BC1" Auto
string Property TIF_Mode = "UNCOMPRESSED" Auto

;**************** JSON Properties *********************
String Property JsonFileName auto
String Property KeyName_Path = "Path" auto
String Property KeyName_TakePhoto= "TakePhoto" auto
String Property Keyname_Menu= "Menu" auto
String Property KeyName_JPG_Compression ="JPEG Compression" auto
String Property KeyName_Mode = "Mode"   auto
String Property KeyName_DDS_Mode = "DDS Mode" auto
String Property KeyName_TIF_Mode = "TIF Mode" auto
String Property KeyName_ImageType ="Imagetype" auto
String Property Keyname_LoopCount = "LoopCount" auto
String Property Keyname_FPS = "FPS" auto
String Property Keyname_Optimize = "Optimize" auto
String Property KeyName_Compression = "compression" auto 
String Property KeyName_Duration ="Duration" auto
String Property KeyName_Quality ="Quality"auto 
Float Property Quality = 1.0 auto
String Property KeyName_UseJsonFile  = "UseJsonFile" auto 
bool Property UseJsonFile =  true  auto



; ==============================================================================
; PROPERTIES - MCM Configurable
; ==============================================================================
String Property Path = "C:/Pictures" Auto
String Property ImageType = "PNG" Auto
float Property JPG_Compression = 90.0 Auto
String Property Mode = "Normal" Auto
float Property Duration = 5.0 Auto
float Property Fps = 15.0 Auto
int Property LoopCount = 0 Auto
int Property Compression = 9 Auto
int Property Optimize = 1 Auto
bool Property Menu = true Auto
int Property Key_TakePhoto = 14 Auto

float Property PollIntervalFast = 0.1 Auto
float Property PollIntervalSlow = 0.5 Auto
float Property PollTimeoutSeconds = 300.0 Auto
string MenuNane = "console"
bool UI_Hidden = false
bool property PathValid   = true auto
; ==============================================================================
; INTERNAL STATE
; ==============================================================================
bool Property bConfigOpen = false Auto Hidden
bool Property IsLatentScreenshotActive = false Auto Hidden
bool Property IsStartingCapture = false Auto Hidden
String Property Result = "Ready" Auto Hidden
int Property Shots = 0 Auto Hidden

float _CaptureStartRealTime = 0.0
float LastKeyPressTime = 0.0
bool _CompletionHandled = false
float Property version = 3.01 Auto Hidden
; ==============================================================================
; INITIALIZATION
; ==============================================================================

Event OnInit()
    bool allOK = true
    if (SKSE.GetVersion() == 0)
        Debug.MessageBox("ERROR: SKSE not detected.")
        allOK = false
    endif
    if (!CheckJsonUtil())
        Debug.Notification("ERROR: JsonUtil has errors.")
        writejson(); Write default values to json to prevent errors on next load
        allOK = false
    Else
        ; code
        RegisterForKey(Key_TakePhoto)
        RegisterForModEvent("PrintScreenComplete", "OnPrintScreenComplete")
        RegisterForModEvent("PrintScreenPoll", "OnPrintScreenPoll")
         ReadJson() ; Read new default from json file 
        
    endif
EndEvent

bool Function CheckJsonUtil()
    int testVal = JsonUtil.IntListCount("__test__", "__test__")
    return true
EndFunction

; ==============================================================================
; HUD CONTROL - Now uses tn UI control as 'experimental'
Function HideHUD()
    IF(!mENU)
        RETURN
    ENDIF
    debug.notification("Experimental UIHiding in action.")
    ConsoleUtil.ExecuteCommand("tm")
    UI_Hidden = true
endfunction


Function ShowHUD()
    IF(!Menu)
        RETURN
    ENDIF
   ConsoleUtil.ExecuteCommand("tm")
    UI_Hidden = false
   debug.notification("Experimental UI Restoration in action.")
endfunction

; ==============================================================================
; CAPTURE LOGIC
; ==============================================================================

Function CaptureImage(String basePath, String imgType, float jpgComp, String captureMode, float captureDuration)
    Debug.Trace("PrintScreen: CaptureImage starting")
    ; this function calls check path for each image. We don't need to do this, but it allows us to fail early if the path is invalid, and not start the capture process at all. Otherwise, we would have to wait for the timeout to know the path is invalid, which would be a bad user experience.
    if (!PathValid )
        Debug.Notification("Invalid screenshot path!")
        IsStartingCapture = false
        return
    endif
    
    IsLatentScreenshotActive = true
    _CaptureStartRealTime = Utility.GetCurrentRealTime()
    _CompletionHandled = false
    HideHud()
    String startResult = Printscreen_Formula_script.TakePhoto(basePath, imgType, jpgComp, \
        captureMode, captureDuration, Fps, LoopCount, Compression, Optimize,Menu)
    
    if (StringUtil.Find(startResult, "Error") >= 0)
        Debug.Notification("Screenshot failed: " + startResult)
        OnScreenshotCompleted(startResult)
        return
    endif
    
    RegisterForSingleUpdate(GetPollInterval())
EndFunction

; ==============================================================================
; COMPLETION HANDLING
; ==============================================================================

Function OnScreenshotCompleted(String completionResult)
    Debug.Trace("PrintScreen: OnScreenshotCompleted: " + completionResult)
    ShowHUD() ; Ensure HUD is shown after capture, 
    if (StringUtil.Find(completionResult, "Success") >= 0)
        Shots += 1
        Debug.Notification("Screenshot saved! Total: " + Shots)
    elseif (StringUtil.Find(completionResult, "Cancel") >= 0)

        Debug.Notification("Screenshot cancelled")
    else

        Debug.Notification("Screenshot: " + completionResult)
    endif
    
    IsLatentScreenshotActive = false
    IsStartingCapture = false
    _CaptureStartRealTime = 0.0
    
    String finalState = Printscreen_Formula_script.Get_Result()
    if (finalState == "Running" || finalState == "Starting")
        Printscreen_Formula_script.ForceReset()
    endif
EndFunction

; ==============================================================================
; EVENT HANDLERS
; ==============================================================================

Event OnPrintScreenComplete(string eventName, string strArg, float numArg, Form sender)
    Debug.Trace("PrintScreen: OnPrintScreenComplete: " + strArg)
    _HandlePollResult(strArg)
EndEvent

Event OnUpdate()
    if (!IsLatentScreenshotActive && !IsStartingCapture)
        return
    endif
    if (_CompletionHandled)
        return
    endif
    float now = Utility.GetCurrentRealTime()
    if (_CaptureStartRealTime > 0.0 && (now - _CaptureStartRealTime) > PollTimeoutSeconds)
        Debug.Notification("PrintScreen: Timed out")
        Printscreen_Formula_script.Cancel()
        OnScreenshotCompleted("Timeout")
        return
    endif
    String r = Printscreen_Formula_script.Get_Result()
    if (!_HandlePollResult(r))
        RegisterForSingleUpdate(GetPollInterval())
    endif
EndEvent

Event OnPrintScreenPoll(string eventName, string strArg, float numArg, Form sender)
    if (IsLatentScreenshotActive || IsStartingCapture)
        RegisterForSingleUpdate(GetPollInterval())
    endif
EndEvent

Event OnKeyUp(int theKey, float holdtime)
    if (theKey != Key_TakePhoto)
        return
    endif
    if (bConfigOpen || UI.IsTextInputEnabled() || Utility.IsInMenuMode())
        return
    endif
    String nativeState = Printscreen_Formula_script.Get_Result()
    if (nativeState == "Ready" && (IsStartingCapture || IsLatentScreenshotActive))
        IsStartingCapture = false
        IsLatentScreenshotActive = false
        _CompletionHandled = false
    endif
    if (nativeState == "Running" || nativeState == "Starting")
        Debug.Notification("Cancelling...")
        Printscreen_Formula_script.Cancel()
        IsLatentScreenshotActive = false
        IsStartingCapture = false
        _CompletionHandled = false

        return
    endif
    float currentTime = Utility.GetCurrentRealTime()
    if (currentTime - LastKeyPressTime < 0.75)
        return
    endif
    IsStartingCapture = true
    LastKeyPressTime = currentTime
    _CompletionHandled = false
    Debug.Notification("Taking screenshot...")
    CaptureImage(Path, ImageType, JPG_Compression, Mode, Duration)
EndEvent

; ==============================================================================
; HELPER FUNCTIONS
; ==============================================================================

bool Function _IsLongRunningCapture()
    if (ImageType == "GIF" || ImageType == "AGIF" || ImageType == "APNG")
        return true
    endif
    if (ImageType == "DDS")
        if (StringUtil.Find(Mode, "BC6") >= 0 || StringUtil.Find(Mode, "BC7") >= 0)
            return true
        endif
    endif
    return false
EndFunction

float Function GetPollInterval()
    if (_IsLongRunningCapture())
        return PollIntervalSlow
    endif
    return PollIntervalFast
EndFunction

bool Function _HandlePollResult(String r)
    if (_CompletionHandled)
        if(Menu)

        endif 
        return true
    endif
    Result = r
    if (StringUtil.Find(Result, "CALLBACK_") == 0)
        _CompletionHandled = true
        if (Result == "CALLBACK_SUCCESS")
            OnScreenshotCompleted("Success")
        elseif (Result == "CALLBACK_CANCELLED")
            OnScreenshotCompleted("Cancelled")
        elseif (StringUtil.Find(Result, "CALLBACK_ERROR:") == 0)
            OnScreenshotCompleted(StringUtil.Substring(Result, 15))
        else
            OnScreenshotCompleted(Result)
        endif
        return true
    endif
    if (Result == "Ready")
        _CompletionHandled = true
        OnScreenshotCompleted("Success")
        return true
    elseif (StringUtil.Find(Result, "Success") >= 0)
        _CompletionHandled = true
        OnScreenshotCompleted("Success")
        return true
    elseif (StringUtil.Find(Result, "Error") >= 0 || StringUtil.Find(Result, "Failed") >= 0)
        _CompletionHandled = true
        OnScreenshotCompleted(Result)
        return true
    elseif (StringUtil.Find(Result, "Cancel") >= 0)
        _CompletionHandled = true
        OnScreenshotCompleted("Cancelled")
        return true
    endif
    return false
EndFunction

Function UpdateHotkey(int newKey)
    UnregisterForKey(Key_TakePhoto)
    Key_TakePhoto = newKey
    RegisterForKey(Key_TakePhoto)
EndFunction

Function SetConfigOpen(bool isOpen)
    bConfigOpen = true ; isOpen
EndFunction

function Sanatize_ImageType()
    If(Imagetype == "png" || Imagetype == "PNG")
        ImageType = "PNG"
    elseif(Imagetype == "BMP" || Imagetype == "bmp")
        ImageType = "BMP"
    elseif(Imagetype == "JPEG" || Imagetype == "JPG" || Imagetype == "jpg" || Imagetype == "jpeg")
        ImageType = "JPG"
    elseif(ImageType == "GIF" || ImageType == "gif")
        ImageType = "GIF"
    elseif(Imagetype == "TIF" || Imagetype == "TIFF" || Imagetype == "tif" || Imagetype == "tiff")
        ImageType = "TIF"
    Elseif(ImageType == "DDS" || ImageType == "dds")
        ImageType = "DDS"
    elseif(ImageType == "AGIF" || ImageType == "agif")
        ImageType= "AGIF"
    elseif(ImageType=="APNG"||ImageType=="apng")
        ImageType = "APNG"
    else
        ImageType = "PNG"
    endif 
endFunction

function Sanatize_TIF_MODE()
    if(Tif_Mode != "RLE" && Tif_Mode != "LZW" && Tif_Mode != "ZIP")
        Tif_Mode = "UNCOMPRESSED"
    endif   
endFunction
;This function checks if the provided DDS mode is valid, and if not, it defaults to "BC1"
function Sanatize_DDS_Mode()
    String[] validModes = new String[10]
    validModes[0] = "UNCOMPRESSED"
    validModes[1] = "BC7_FAST"
    validModes[2] = "BC1"
    validModes[3] = "BC2"
    validModes[4] = "BC3"
    validModes[5] = "BC4"
    validModes[6] = "BC5"
    validModes[7] = "BC6H"
    validModes[8] = "BC7_SLOW"
    validModes[9] = "BC7_NORMAL"
    int i = 0
    while i < 10
        if DDS_Mode == validModes[i]
            return
        endif
        i += 1
    endwhile
    ;if you get here, the mode is invalid, so default to BC1
    DDS_Mode = "BC1"
endFunction

function Sanatize_Mode()
    if(Mode != "UNCOMPRESSED" && Mode != "BC1" && Mode != "BC2" && Mode != "BC3" && \
       Mode != "BC4" && Mode != "BC5" && Mode != "BC6H" && Mode != "BC7_SLOW" && \
       Mode != "BC7_NORMAL" && Mode != "BC7_FAST" && Mode != "RLE" && Mode != "LZW" && Mode != "ZIP")
        Mode = "UNCOMPRESSED"
    endif
EndFunction

Function Sanitise_Duration()
    if(Duration < 1.0)
        Duration = 1.0              
    elseif(Duration > 30.0)
        Duration = 30.0
    endif
EndFunction

function writeJson()
    JsonUtil.SetStringValue(jsonfilename,KeyName_Path,Path)
    JsonUtil.SetStringValue(jsonfilename,KeyName_TakePhoto,Key_TakePhoto)
    JsonUtil.SetIntValue(jsonfilename,KeyName_Menu,menu as int)
    JsonUtil.SetFloatValue(jsonfilename,KeyName_JPG_Compression,JPG_Compression)
    JsonUtil.SetStringValue(jsonfilename,KeyName_Mode,Mode)
    JsonUtil.SetStringValue(jsonfilename,KeyName_DDS_Mode,DDS_Mode)
    JsonUtil.SetStringValue(jsonfilename,KeyName_TIF_Mode,TIF_Mode)
    JsonUtil.SetStringValue(jsonfilename,KeyName_ImageType,ImageType)
    JsonUtil.SetIntValue(jsonfilename,KeyName_LoopCount,LoopCount)
    JsonUtil.SetFloatValue(jsonfilename,KeyName_FPS,FPS)
    JsonUtil.SetIntValue(jsonfilename,KeyName_Optimize,Optimize)
    JsonUtil.SetIntValue(jsonfilename,KeyName_Compression,Compression)
    JsonUtil.SetIntValue(jsonfilename,KeyName_UseJsonFile,UseJsonFile as int)
    JsonUtil.SetFloatValue(jsonfilename,KeyName_Quality,Quality)
    JsonUtil.SetFloatValue(jsonfilename,KeyName_Duration,Duration)
    JsonUtil.Save(jsonfilename)
EndFunction

function readJson()
String TempPath= Path
if(!printscreen_formula_script.checkPath(TempPath))
    Path="C:/"  
endif 
 TempPath = JsonUtil.GetStringValue(jsonfilename,KeyName_Path)
if(printscreen_formula_script.checkPath(TempPath))
    path = TempPath
else
    TempPath=Path
    if(printscreen_formula_script.checkPath(TempPath))
        Path="C:/Pictures"
    else 
        Path="c:/"
    endif
endif
    Menu = JsonUtil.GetIntValue(jsonfilename,KeyName_Menu) as bool
    JPG_Compression = JsonUtil.GetFloatValue(jsonfilename,KeyName_JPG_Compression)
    Mode = JsonUtil.GetStringValue(jsonfilename,KeyName_Mode)
    DDS_Mode = JsonUtil.GetStringValue(jsonfilename,KeyName_DDS_Mode)
    TIF_Mode = JsonUtil.GetStringValue(jsonfilename,KeyName_TIF_Mode)
    ImageType = JsonUtil.GetStringValue(jsonfilename,KeyName_ImageType)   
    Duration = JsonUtil.GetFloatValue(jsonfilename,KeyName_Duration)
    LoopCount = JsonUtil.GetIntValue(jsonfilename,KeyName_LoopCount)
    FPS = JsonUtil.GetFloatValue(jsonfilename,KeyName_FPS)
    Optimize = JsonUtil.GetIntValue(jsonfilename,KeyName_Optimize)
    Quality = JsonUtil.GetFloatValue(jsonfilename,KeyName_Quality)
    Compression = JsonUtil.GetIntValue(jsonfilename,KeyName_Compression)
    UseJsonFile = JsonUtil.GetIntValue(jsonfilename,KeyName_UseJsonFile) as bool
EndFunction

bool function checkJson()
    if(!JsonUtil.JsonExists(jsonfilename))
        writeJson()
        return false
    endif
    ; Check that all required keys exist and have the correct type
    if(!JsonUtil.HasStringValue(jsonfilename,KeyName_Path))
        return false
    elseif(!JsonUtil.HasStringValue(jsonfilename,KeyName_ImageType))
        return false
    elseif(!JsonUtil.HasIntValue(jsonfilename,KeyName_Menu))
        return false
    elseif(!jsonutil.hasStringValue(jsonfilename,KeyName_Mode))
        return false
    elseif(!jsonutil.hasStringValue(jsonfilename,KeyName_TIF_Mode))
        return false
    elseif(!jsonutil.hasStringvalue(jSonfilename,keyName_DDS_Mode))
        return false
    elseif(!jsonutil.hasintValue(jsonfilename,Keyname_UseJsonFile))
        return false
    elseif(!jsonutil.hasIntValue(jsonfilename,Keyname_LoopCount))
        return false
    Elseif(!jsonutil.hasFloatvalue(jsonfileName,KeyName_FPS))
        return false
    elseif(!jsonutil.hasIntValue(jsonfileName, Keyname_Optimize))
        return false
    elseif(!jsonutil.HasfloatValue(jsonfilename,Keyname_jpg_Compression))
        return false
    elseif(!jsonutil.hasIntValue(jsonfileName,Keyname_compression))
        return false
    elseif(!jsonutil.hasfloatvalue(jsonfilename,KeyName_Quality))
        return false 
    elseif(!jsonutil.hasfloatvalue(jsonfilename,KeyName_Duration))
        return false
    endif
    return true
Endfunction


