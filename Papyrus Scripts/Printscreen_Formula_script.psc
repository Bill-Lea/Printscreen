Scriptname Printscreen_Formula_script extends Quest

; ==============================================================================
; NATIVE FUNCTION DECLARATIONS - Core Capture Functions
; ==============================================================================

bool Function CheckPath(String path) Global Native

; TakePhoto — full V4 signature with video capture parameters.
; The final AutoUI parameter MUST match the C++ binding.
; It has a default so older Papyrus call sites that pass only the original
; video parameters can still compile.
; Returns immediately: "Started", "Already running", or an error string.
; Completion is notified via the PrintScreenComplete mod event.
String Function TakePhoto(String basePath, String imageType, float jpgCompression, \
    String Mode, float Duration, float Fps, int LoopCount, int Compression, int Optimize, \
    float VideoDuration, int TargetResolution, int VideoFrameRate, int QualityPreset, \
    int VideoBitrate, float KeyframeInterval, int EncoderPreference, \
    int RateControl, int VideoContainer, bool AutoUI = true) Global Native

; DEPRECATED: Polling is no longer supported.
; Use the PrintScreenComplete mod event instead.
; This function now always returns "Deprecated".
String Function Get_Result() Global Native

String Function Cancel() Global Native

; Reset — force=true hard-aborts any active capture; force=false is a safe reset.
String Function MYReset(bool force = false) Global Native

; UI helpers exposed by Bindings.cpp.
bool Function SaveAndHideAllUI() Global Native
bool Function RestoreAllUI() Global Native
bool Function IsGamePaused() Global Native
