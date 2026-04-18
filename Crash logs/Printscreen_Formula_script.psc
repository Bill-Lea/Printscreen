Scriptname Printscreen_Formula_script extends Quest  

; ==============================================================================
; NATIVE FUNCTION DECLARATIONS - Core Capture Functions
; ==============================================================================

bool Function CheckPath(String path) Global Native
String Function TakePhoto(String basePath, String imageType, float jpgCompression, \
    String Mode, float Duration, float Fps, int LoopCount, int Compression, int Optimize, bool Menu) Global Native


String Function Get_Result() Global Native
String Function Cancel() Global Native
String Function ForceReset() Global Native
String Function ResetState() Global Native
;