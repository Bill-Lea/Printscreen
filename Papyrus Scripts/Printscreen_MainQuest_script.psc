Scriptname Printscreen_MainQuest_script extends Quest

Import StringUtil
Import Debug
Import Utility
Import Input
Import UI
string Property DDS_Mode = "UNCOMPRESSED" Auto
string Property TIF_Mode = "UNCOMPRESSED" Auto

;**************** JSON Properties *********************
String Property JsonFileName= "PrintScreenConfig" auto
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

; ==============================================================================
; PROPERTIES - MCM Configurable
; ==============================================================================
String Property Path = "C:/Pictures" Auto
String Property ImageType = "PNG" Auto
float Property JPG_Compression = 90.0 Auto
String Property Mode = "Normal" Auto
float Property Duration = 5.0 Auto
float Property Fps = 5.0 Auto
int Property LoopCount = 0 Auto
int Property Compression = 0 Auto
int Property Optimize = 1 Auto
bool Property Menu = true Auto
int Property Key_TakePhoto = 14 Auto

float Property PollIntervalFast = 0.1 Auto
float Property PollIntervalSlow = 1.5 Auto
float Property PollTimeoutSeconds = 300.0 Auto

bool property UI_Hidden = false  auto
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


bool Function CheckJson()

if(jsonUtil.JsonExists(jsonfilename))
    if(Jsonutil.isGood(JsonFileName))
        jsonComplete()
        Debug.Notification("PrintScreen: JSON file exists and is valid")
        return true
    else
        Debug.Notification("PrintScreen: JSON file exists but is corrupted or invalid")
        return false
    endif
else
    Debug.Notification("PrintScreen: JSON file does not exist") 
    return false
endif 
EndFunction

Event OnInit()
    bool allOK = true
    if (SKSE.GetVersion() == 0)
        Debug.MessageBox("ERROR: SKSE not detected.")
        allOK = false
        Debug.messageBox("PrintScreen requires SKSE. Please install SKSE to use this mod.")
    endif
    if(!CheckConsoleUtil() )    
      allOK = false
      Debug.messageBox("ERROR: ConsoleUtil not detected. PrintScreen requires ConsoleUtil for HUD toggling. Please install ConsoleUtil to use this mod.")
    endif   
    if(!CheckJsonUtil())
        allOK = false
    endif
    if(!JContainersCheck())
        allOK = false
        Debug.messageBox("ERROR: JContainers not detected. PrintScreen requires JContainers for JSON handling. Please install JContainers to use this mod.")
    endif

    if (allOK)
        ; code
; So if all the prerequisits are present we will proceed
;and test thejason file amd if it is good andcomplete
; we will load it and sanatize the content
If(CheckJson())
    Readjson() ; Read from json file if it exists and is valid
    SanitizeAll() ; Sanitize all loaded values to ensure they're within valid ranges
else
    writejson() ; Create new json with default values if it doesn't exist or is invalid
EndIf 

        RegisterForKey(Key_TakePhoto)
        RegisterForModEvent("PrintScreenComplete", "OnPrintScreenComplete")
        RegisterForModEvent("PrintScreenPoll", "OnPrintScreenPoll")
         ReadJson() ; Read new default from json file 
    endif
EndEvent

; ==============================================================================
; GAME LOAD RECOVERY
; ==============================================================================
; SKSE fires this event on every save load for active quest scripts.
; Mod event registrations do NOT survive save/load, so we must re-register.
; The C++ plugin may also have stale state from an interrupted capture.

Event OnPlayerLoadGame()
    Debug.Trace("PrintScreen: OnPlayerLoadGame - recovering state")

    ; --- Re-register events that don't persist across loads ---
    RegisterForKey(Key_TakePhoto)
    RegisterForModEvent("PrintScreenComplete", "OnPrintScreenComplete")
    RegisterForModEvent("PrintScreenPoll", "OnPrintScreenPoll")

    ; --- Force-reset the C++ plugin to clear any interrupted capture ---
    Printscreen_Formula_script.ForceReset()

    ; --- Sanitize all properties restored from save ---
    ; Properties may be stale from an older version or corrupted save.
    ; This ensures Duration, FPS, Compression, etc. are all within valid ranges.
    Sanitise_Duration()
    Sanatise_FPS()
    SanatiseCompression()
    Sanatise_LoopCount()
    Sanatise_Optimise()
    Sanatise_Quality()
    Sanitise_JPG_Compression()
    Sanatize_ImageType()
    Sanatise_Mode()
    Sanatize_TIF_MODE()
    Sanatize_DDS_Mode()

    ; --- CRITICAL: Recalculate FPS from Duration ---
    ; FPS must always match what SetFPS(Duration) would produce.
    ; Without this, a stale FPS value from save/JSON causes wrong frame counts.
    RecalculateFPS()

    ; --- Reset Papyrus-side flags that may be stale from the save ---
    if (IsLatentScreenshotActive || IsStartingCapture)
        Debug.Trace("PrintScreen: Clearing stale capture flags from interrupted session")
        IsLatentScreenshotActive = false
        IsStartingCapture = false
        _CaptureStartRealTime = 0.0
        _CompletionHandled = false
    endif

    ; --- Restore HUD in case HideHUD() ran but ShowHUD() never did ---
    ; UI_Hidden is a plain variable (not saved), so after reload it defaults
    ; to false even if the HUD was toggled off.  Force it true first so that
    ; ShowHUD() doesn't early-out, then call ShowHUD() to guarantee
    ; the HUD is visible.
    UI_Hidden = true
    ShowHUD()

    Debug.Trace("PrintScreen: Game-load recovery complete")
EndEvent



; ==============================================================================
; HUD CONTROL - Now uses tm UI control as 'experimental'
; ==============================================================================

Function HideHUD()
    if (!Menu)
        return
    endif
    if (UI_Hidden)
        Debug.Trace("PrintScreen: HUD already hidden, skipping")
        return
    endif  
    ConsoleUtil.ExecuteCommand("tm")
    UI_Hidden = true
EndFunction


Function ShowHUD()
    if (!Menu)
        return
    endif
    if (!UI_Hidden)
        Debug.Trace("PrintScreen: HUD already visible, skipping")
        return
    endif
    Debug.Trace("PrintScreen: Restoring HUD")
    ConsoleUtil.ExecuteCommand("tm")
    UI_Hidden = false
    EndFunction

; ==============================================================================
; HELPER: Check if native plugin reports capture is in progress
; ==============================================================================
; Returns true if the native state indicates a capture is actively running.
; This must match ALL possible "in progress" states the C++ plugin can return.

bool Function _IsNativeCaptureActive(String nativeState)
    ; These are all the states where the worker thread is actively running
    if (nativeState == "Running")
        return true
    endif
    if (nativeState == "Starting")
        return true
    endif
    if (nativeState == "Capture started")
        return true
    endif
    if (nativeState == "Cancelling")
        return true
    endif
    ; "Already running" means we tried to start but one was already going
    if (nativeState == "Already running")
        return true
    endif
    return false
EndFunction

; ==============================================================================
; CAPTURE LOGIC
; ==============================================================================

Function CaptureImage(String basePath, String imgType, float jpgComp, String captureMode, float captureDuration)
    Debug.Trace("PrintScreen: CaptureImage starting - Type: " + imgType + ", Duration: " + captureDuration)
    
    ; Validate path before starting
    if (!PathValid)
        Debug.Notification("Invalid screenshot path!")
        IsStartingCapture = false
        ; Don't call ShowHUD here - we haven't hidden it yet
        return
    endif
    
    ; Set state BEFORE hiding HUD
    IsLatentScreenshotActive = true
    IsStartingCapture = true
    _CaptureStartRealTime = Utility.GetCurrentRealTime()
    _CompletionHandled = false
    
    ; Now hide the HUD
    HideHUD()
    
    ; Start the capture - this returns immediately for async captures
    String startResult = Printscreen_Formula_script.TakePhoto(basePath, imgType, jpgComp, \
        captureMode, captureDuration, Fps, LoopCount, Compression, Optimize)
    
    Debug.Trace("PrintScreen: TakePhoto returned: " + startResult)
    
    ; Check for immediate failure (thread didn't start)
    if (StringUtil.Find(startResult, "Error") >= 0 || StringUtil.Find(startResult, "ERROR") >= 0)
        Debug.Notification("Screenshot failed: " + startResult)
        ; Capture failed to start - restore HUD and clean up
        _CompletionHandled = true
        IsLatentScreenshotActive = false
        IsStartingCapture = false
        _CaptureStartRealTime = 0.0
        ShowHUD()
        return
    endif
    
    ; Capture started successfully - begin polling for completion
    ; The worker thread is now running and will set result when done
    IsStartingCapture = false  ; We're past the starting phase, now actively capturing
    RegisterForSingleUpdate(GetPollInterval())
EndFunction

; ==============================================================================
; COMPLETION HANDLING
; ==============================================================================
; This should ONLY be called when capture is definitively complete (success, error, or cancel)

Function OnScreenshotCompleted(String completionResult)
    Debug.Trace("PrintScreen: OnScreenshotCompleted: " + completionResult)
    
    ; Clean up state
    IsLatentScreenshotActive = false
    IsStartingCapture = false
    _CaptureStartRealTime = 0.0
    
    ; CRITICAL: Restore HUD now that capture is definitively complete
    ShowHUD()
    
    ; Notify user of result
    if (StringUtil.Find(completionResult, "Success") >= 0)
        Shots += 1
        Debug.Notification("Screenshot saved! Total: " + Shots)
    elseif (StringUtil.Find(completionResult, "Cancel") >= 0)
        Debug.Notification("Screenshot cancelled")
    else
        Debug.Notification("Screenshot: " + completionResult)
    endif
    
    ; Safety: force reset if native is somehow still running
    String finalState = Printscreen_Formula_script.Get_Result()
    if (_IsNativeCaptureActive(finalState))
        Debug.Trace("PrintScreen: Native still active after completion, forcing reset")
        Printscreen_Formula_script.ForceReset()
    endif
EndFunction

; ==============================================================================
; EVENT HANDLERS
; ==============================================================================

Event OnPrintScreenComplete(string eventName, string strArg, float numArg, Form sender)
    Debug.Trace("PrintScreen: OnPrintScreenComplete event: " + strArg)
    _HandlePollResult(strArg)
EndEvent

Event OnUpdate()
    ; Exit early if we're not expecting updates
    if (!IsLatentScreenshotActive && !IsStartingCapture)
        Debug.Trace("PrintScreen: OnUpdate but no capture active, ignoring")
        return
    endif
    
    ; Exit early if already handled
    if (_CompletionHandled)
        Debug.Trace("PrintScreen: OnUpdate but completion already handled")
        return
    endif
    
    ; Check for timeout
    float now = Utility.GetCurrentRealTime()
    if (_CaptureStartRealTime > 0.0 && (now - _CaptureStartRealTime) > PollTimeoutSeconds)
        Debug.Notification("PrintScreen: Timed out")
        Printscreen_Formula_script.Cancel()
        _CompletionHandled = true
        OnScreenshotCompleted("Timeout")
        return
    endif
    
    ; Poll native state
    String r = Printscreen_Formula_script.Get_Result()
    Debug.Trace("PrintScreen: Polling - Get_Result returned: " + r)
    
    ; Process result - if not complete, re-register for next poll
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
    
    ; Ignore input in menus/text input
    if (bConfigOpen || UI.IsTextInputEnabled() || Utility.IsInMenuMode())
        return
    endif
    
    ; Get current native state
    String nativeState = Printscreen_Formula_script.Get_Result()
    Debug.Trace("PrintScreen: OnKeyUp - nativeState: " + nativeState + ", IsLatentScreenshotActive: " + IsLatentScreenshotActive)
    
    ; -------------------------------------------------------------------------
    ; CASE 1: Native is actively capturing - user wants to cancel
    ; -------------------------------------------------------------------------
    if (_IsNativeCaptureActive(nativeState))
        Debug.Trace("PrintScreen: Cancelling active capture")
        Debug.Notification("Cancelling...")
        Printscreen_Formula_script.Cancel()
        
        ; Mark as handled so polling doesn't double-process
        _CompletionHandled = true
        
        ; Clean up state and restore HUD
        IsLatentScreenshotActive = false
        IsStartingCapture = false
        _CaptureStartRealTime = 0.0
        ShowHUD()
        
        Debug.Notification("Screenshot cancelled")
        return
    endif
    
    ; -------------------------------------------------------------------------
    ; CASE 2: State mismatch - we think capture is active but native says Ready
    ; This can happen if the completion event was missed
    ; -------------------------------------------------------------------------
    if (nativeState == "Ready" && (IsStartingCapture || IsLatentScreenshotActive))
        Debug.Trace("PrintScreen: State mismatch - native Ready but flags set, cleaning up")
        IsLatentScreenshotActive = false
        IsStartingCapture = false
        _CaptureStartRealTime = 0.0
        _CompletionHandled = false
        ShowHUD()  ; Restore HUD in case it was hidden
        ; Fall through to allow starting a new capture
    endif
    
    ; -------------------------------------------------------------------------
    ; CASE 3: Debounce - ignore rapid key presses
    ; -------------------------------------------------------------------------
    float currentTime = Utility.GetCurrentRealTime()
    if (currentTime - LastKeyPressTime < 0.75)
        Debug.Trace("PrintScreen: Debounce - ignoring rapid keypress")
        return
    endif
    
    ; -------------------------------------------------------------------------
    ; CASE 4: Start a new capture
    ; -------------------------------------------------------------------------
    LastKeyPressTime = currentTime
    _CompletionHandled = false    
    CaptureImage(Path, ImageType, JPG_Compression, Mode, Duration)
EndEvent

; ==============================================================================
; HELPER FUNCTIONS
; ==============================================================================

bool Function _IsLongRunningCapture()
    if (ImageType == "AGIF" || ImageType == "APNG")
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

; ==============================================================================
; POLL RESULT HANDLER
; ==============================================================================
; This function checks the result from the C++ plugin and determines if capture
; is complete. It should ONLY call OnScreenshotCompleted() for definitive 
; terminal states, NOT for intermediate states.
;
; C++ plugin returns these values:
;   "Ready"                    - Initial state / idle
;   "Running"                  - Worker thread is capturing
;   "Starting"                 - Worker thread is starting up
;   "Capture started"          - TakePhoto() succeeded, worker running
;   "CALLBACK_SAVED: <path>"   - SUCCESS: Capture completed, file saved
;   "CALLBACK_ERROR: <msg>"    - ERROR: Capture failed with error
;   "CALLBACK_SUCCESS"         - SUCCESS: Alternative success format
;   "CALLBACK_CANCELLED"       - Capture was cancelled
;   "Cancelled"                - Capture was cancelled (alt format)
;   "Cancelling"               - Cancel request acknowledged, stopping soon
;   "No active operation"      - Cancel called but nothing was running
;   "Reset"                    - State was force reset
;   "Already running"          - Tried to start but capture in progress
; ==============================================================================

bool Function _HandlePollResult(String r)
    ; Guard against double-handling
    if (_CompletionHandled)
        Debug.Trace("PrintScreen: _HandlePollResult - already handled, ignoring")
        return true
    endif
    
    Result = r
    Debug.Trace("PrintScreen: _HandlePollResult: " + Result)
    
    ; =========================================================================
    ; TERMINAL STATES - Capture is definitively complete
    ; These should trigger OnScreenshotCompleted() which restores HUD
    ; =========================================================================
    
    ; Success: "CALLBACK_SAVED: <filepath>"
    if (StringUtil.Find(Result, "CALLBACK_SAVED:") == 0)
        Debug.Trace("PrintScreen: Terminal state - CALLBACK_SAVED")
        _CompletionHandled = true
        OnScreenshotCompleted("Success")
        return true
    endif
    
    ; Success: "CALLBACK_SUCCESS" (alternative format)
    if (Result == "CALLBACK_SUCCESS")
        Debug.Trace("PrintScreen: Terminal state - CALLBACK_SUCCESS")
        _CompletionHandled = true
        OnScreenshotCompleted("Success")
        return true
    endif
    
    ; Error: "CALLBACK_ERROR: <message>"
    if (StringUtil.Find(Result, "CALLBACK_ERROR:") == 0)
        Debug.Trace("PrintScreen: Terminal state - CALLBACK_ERROR")
        _CompletionHandled = true
        String errorMsg = StringUtil.Substring(Result, 16) ; Skip "CALLBACK_ERROR: "
        OnScreenshotCompleted(errorMsg)
        return true
    endif
    
    ; Cancelled: "CALLBACK_CANCELLED"
    if (Result == "CALLBACK_CANCELLED")
        Debug.Trace("PrintScreen: Terminal state - CALLBACK_CANCELLED")
        _CompletionHandled = true
        OnScreenshotCompleted("Cancelled")
        return true
    endif
    
    ; Cancelled: "Cancelled" (alternative format)
    if (Result == "Cancelled")
        Debug.Trace("PrintScreen: Terminal state - Cancelled")
        _CompletionHandled = true
        OnScreenshotCompleted("Cancelled")
        return true
    endif
    
    ; Generic error states (fallback for unexpected error formats)
    if (StringUtil.Find(Result, "Error:") == 0 || StringUtil.Find(Result, "ERROR:") == 0)
        Debug.Trace("PrintScreen: Terminal state - Error")
        _CompletionHandled = true
        OnScreenshotCompleted(Result)
        return true
    endif
    
    if (StringUtil.Find(Result, "Failed") >= 0)
        Debug.Trace("PrintScreen: Terminal state - Failed")
        _CompletionHandled = true
        OnScreenshotCompleted(Result)
        return true
    endif
    
    ; =========================================================================
    ; IN-PROGRESS STATES - Keep polling, do NOT call OnScreenshotCompleted()
    ; =========================================================================
    
    ; "Ready" - Initial state. For animated captures, worker may not have
    ; updated g_result yet. Keep polling.
    if (Result == "Ready")
        Debug.Trace("PrintScreen: In-progress state - Ready (worker may be starting)")
        return false
    endif
    
    ; "Running" - Worker thread is actively capturing frames
    if (Result == "Running")
        Debug.Trace("PrintScreen: In-progress state - Running")
        return false
    endif
    
    ; "Starting" - Worker thread is initializing
    if (Result == "Starting")
        Debug.Trace("PrintScreen: In-progress state - Starting")
        return false
    endif
    
    ; "Capture started" - TakePhoto() returned, worker is running
    if (Result == "Capture started")
        Debug.Trace("PrintScreen: In-progress state - Capture started")
        return false
    endif
    
    ; "Cancelling" - Cancel was requested, worker is stopping
    if (Result == "Cancelling")
        Debug.Trace("PrintScreen: In-progress state - Cancelling")
        return false
    endif
    
    ; "No active operation" - Cancel was called but nothing was running
    if (Result == "No active operation")
        Debug.Trace("PrintScreen: In-progress state - No active operation")
        return false
    endif
    
    ; "Reset" - State was reset, not a completion
    if (Result == "Reset")
        Debug.Trace("PrintScreen: In-progress state - Reset")
        return false
    endif
    
    ; "Already running" - Tried to start another capture while one is active
    if (Result == "Already running")
        Debug.Trace("PrintScreen: In-progress state - Already running")
        return false
    endif
    
    ; =========================================================================
    ; UNKNOWN STATE - Log it and keep polling
    ; Don't treat unknown states as completion to avoid premature ShowHUD()
    ; =========================================================================
    Debug.Trace("PrintScreen: Unknown poll result, continuing to poll: " + Result)
    return false
EndFunction

Function UpdateHotkey(int newKey)
    UnregisterForKey(Key_TakePhoto)
    Key_TakePhoto = newKey
    RegisterForKey(Key_TakePhoto)
EndFunction

Function SetConfigOpen(bool isOpen)
    bConfigOpen = isOpen
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
    DDS_Mode = "UNCOMPRESSED"
endFunction

function Sanatise_Mode()
    if(Mode != "UNCOMPRESSED" && Mode != "BC1" && Mode != "BC2" && Mode != "BC3" && \
       Mode != "BC4" && Mode != "BC5" && Mode != "BC6H" && Mode != "BC7_SLOW" && \
       Mode != "BC7_NORMAL" && Mode != "BC7_FAST" && Mode != "RLE" && Mode != "LZW" && Mode != "ZIP")
        Mode = "UNCOMPRESSED"
    endif
EndFunction

Function Sanitise_Duration()
    if(Duration < 1.0)
        Duration = 1.0              
    elseif(Duration > 15.0)
        Duration = 15.0
    endif
EndFunction
function Sanitise_JPG_Compression()
    if(JPG_Compression < 1.0)
        JPG_Compression = 1.0
    elseif(JPG_Compression > 100.0)
        JPG_Compression = 100.0
    endif
EndFunction
function Sanatise_FPS()
    if(FPS < 1.0)
        FPS = 1.0
    elseif(FPS > 15.0)
        FPS = 15.0
    endif
EndFunction

; Recalculate FPS from Duration to keep them synchronized.
; This mirrors the MCM SetFPS logic and must be called after loading
; Duration from JSON or save to prevent stale FPS values.
Function RecalculateFPS()
    if(Duration < 4.0)
        FPS = 15.0
    elseif(Duration < 6.0)
        FPS = 12.0
    elseif(Duration < 9.0)
        FPS = 10.0
    elseif(Duration < 13.0)
        FPS = 8.0
    else
        FPS = 6.0
    endif
    Debug.Trace("PrintScreen: RecalculateFPS - Duration=" + Duration + ", FPS=" + FPS)
EndFunction

function Sanatise_Optimise()
    if(Optimize < 0)
        Optimize = 0
    elseif(Optimize > 1)
        Optimize = 1
    endif
EndFunction 
function Sanatise_Quality()
    if(Quality < 0.0)
        Quality = 0.0
    elseif(Quality > 1.0)
        Quality = 1.0
    endif
EndFunction 
function SanatiseCompression()
    if(Compression < 0)
        Compression = 0
    elseif(Compression > 1)
        Compression = 1

    endif
EndFunction 
Function Sanatise_LoopCount()
    if(LoopCount < 0)
        LoopCount = 0
    elseif(LoopCount > 10)
        LoopCount = 10
    endif
EndFunction 

function writeJson()  
    JsonUtil.SetStringValue(jsonfilename,KeyName_Path,Path)
    JsonUtil.SetIntValue(jsonfilename,KeyName_TakePhoto,Key_TakePhoto)
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
    JsonUtil.SetFloatValue(jsonfilename,KeyName_Quality,Quality)
    JsonUtil.SetFloatValue(jsonfilename,KeyName_Duration,Duration)
    JsonUtil.Save(jsonfilename)
EndFunction

function readJson()
;The path defaults to C:/Pictures if the key is missing or invalid, but we still write it to the JSON file to ensure it exists for future loads.;the question is wheather or not to validate the default path. 
;and what to do if the default path is invalid. For now, we will assume that the default path is valid and not perform additional validation on it.
; Path = JsonUtil.GetStringValue(jsonfilename,KeyName_Path) and if THAT fails
;we will tell the user to set a new path through the MCM._CompletionHandled
;First read the json path and test it. If valid set it and we are done with path
;if it fails we will test the defaulkt path and if THAT fails we will tell the uswto use the mCM to select a valid path.
String tempPath = JsonUtil.GetStringValue(jsonfilename,KeyName_Path)

    if (tempPath != "" && Printscreen_formula_script.Checkpath(TempPath))
        Path = tempPath
        PathValid = true
    else
        if(Printscreen_Formula_script.Checkpath(Path))
            PathValid = true
            Debug.Notification("Warning: JSON path is invalid, but default path is valid. Using default path: " + Path)
        else
            PathValid = false
            Debug.Notification("Warning: JSON path is invalid, and default path is also invalid. Please set a valid path in the MCM.")
        endif
    endif
    int savedKey = JsonUtil.GetIntValue(jsonfilename,KeyName_TakePhoto)
    if (savedKey > 0)
        Key_TakePhoto = savedKey
    endif
    Menu = JsonUtil.GetIntValue(jsonfilename,KeyName_Menu) as bool
    JPG_Compression = JsonUtil.GetFloatValue(jsonfilename,KeyName_JPG_Compression)
    Sanitise_JPG_Compression()
    Mode = JsonUtil.GetStringValue(jsonfilename,KeyName_Mode)
    Sanatise_Mode() 
    DDS_Mode = JsonUtil.GetStringValue(jsonfilename,KeyName_DDS_Mode)
        Sanatize_DDS_Mode()
    TIF_Mode = JsonUtil.GetStringValue(jsonfilename,KeyName_TIF_Mode)
        Sanatize_TIF_MODE()
    ImageType = JsonUtil.GetStringValue(jsonfilename,KeyName_ImageType)   
    Sanatize_ImageType()
    Duration = JsonUtil.GetFloatValue(jsonfilename,KeyName_Duration)
    Sanitise_Duration()
       ; CRITICAL: Recalculate FPS from Duration after loading both values.
    ; This ensures that if Duration is changed in JSON, the corresponding FPS is updated
    
    RecalculateFPS()
    LoopCount = JsonUtil.GetIntValue(jsonfilename,KeyName_LoopCount)
    Sanatise_LoopCount()
    Optimize = JsonUtil.GetIntValue(jsonfilename,KeyName_Optimize)
    Sanatise_Optimise() 
    Quality = JsonUtil.GetFloatValue(jsonfilename,KeyName_Quality)
    Sanatise_Quality() 
    Compression = JsonUtil.GetIntValue(jsonfilename,KeyName_Compression)
    SanatiseCompression()  
EndFunction
function SanitizeAll()
    Sanatize_ImageType()
    Sanatise_Mode()
    Sanatize_DDS_Mode()
    Sanatize_TIF_MODE()
    Sanitise_Duration()
    Sanitise_JPG_Compression()
    Sanatise_FPS()
    Sanatise_Optimise() 
    Sanatise_Quality() 
    SanatiseCompression() 
    Sanatise_LoopCount()
EndFunction

bool function jsonComplete()
if( jsonUtil.HasStringValue(jsonfilename,KeyName_Path)\
&& JsonUtil.HasIntValue(jsonfilename,KeyName_Menu) \
&& JsonUtil.HasFloatValue(jsonfilename,KeyName_JPG_Compression) \
&& JsonUtil.HasStringValue(jsonfilename,KeyName_Mode) \
&& JsonUtil.HasStringValue(jsonfilename,KeyName_DDS_Mode) \
&& JsonUtil.HasStringValue(jsonfilename,KeyName_TIF_Mode) \
&& JsonUtil.HasStringValue(jsonfilename,KeyName_ImageType) \
&& JsonUtil.HasIntValue(jsonfilename,KeyName_LoopCount) \
&& JsonUtil.HasFloatValue(jsonfilename,KeyName_FPS) \
&& JsonUtil.HasIntValue(jsonfilename,KeyName_Optimize))
 Return true 
else
 return false
ENDIF
EndFunction