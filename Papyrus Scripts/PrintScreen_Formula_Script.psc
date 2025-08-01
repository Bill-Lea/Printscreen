Scriptname Printscreen_Formula_script extends Quest  

; Non-latent implementation with OnUpdate polling

; Native function declarations
bool Function CheckPath(String path) Global Native
String Function TakePhoto(String basePath, String imageType, float jpgCompression, String Mode, float GIF_MultiFrame_Duration) Global Native
String Function Get_Result() Global Native
String Function Cancel() Global Native
String Function ForceReset() Global Native
String Function ResetState() Global Native  ; ADDED: Clean state reset