Scriptname Printscreen_MainQuest_Script extends Quest  

int Property TakePhoto auto  
int Property KeyMapCode auto
int Property KeyCode auto 
int Property Validate auto
int Property shots auto
int Property HUD_Control auto
bool Property UseJsonFile auto 
String Property ImageType auto 
String Property Path auto
Bool Property Menu auto 
bool property bConfigOpen auto

float property Compression auto
String Property DDS_Compression auto
String Result = ""
String Property Version  auto 
bool Property Read_Write_Config auto
String Property JsonFileName auto
String Property KeyName_Path auto
String Property KeyName_TakePhoto auto
String Property Keyname_Menu auto
String Property  KeyName_Compression auto
String Property KeyName_DDS_Compression auto
String Property KeyName_ImageType auto
String Property KeyName_UseJsonFile auto
String Property Keyname_HUD_Control Auto
function toggleHud()
    ConsoleUtil.ExecuteCommand("tm")
    utility.wait(0.1)
endfunction

function Sanatize_ImageType()
  If(Imagetype == "png")
    return
  elseif(Imagetype == "BMP")
    return
  elseif(Imagetype == "JPEG")
    return
  elseif(ImageType == "GIF")
    return
  elseif(Imagetype == "TIF")
    Return
    Elseif(ImageType == "DDS")
    return
  else
    ImageType = "PNG"
    return
  endif 
endFunction
function Sanatize_DDS_Compression()
  if(DDS_Compression == "UNCOMPRSSED")
    return
  ElseIF(DDS_Compression == "BC1")
    return
  elseif(DDS_Compression == "BC2")
    return
  elseif(DDS_Compression == "BC3")
    return
  elseif(DDS_Compression == "BC4" )
    return
  elseif(DDS_Compression == "BC5")
    return
  Elseif(DDS_Compression == "BC6H")
    return
  elseif(DDS_Compression == "BC7_FAST")
    return
  elseif(DDS_Compression == "BC7")
    return
  else
    DDS_Compression = "UNCOMPRESSED"  ;
    Return
  endif
EndFunction


Event onInit()
    ;Define Default values
    Version="1.7.0"    
    shots = 0 
    menu =true
 
    bConfigOpen = false 
    UseJsonFile=False
    JsonFilename = "Printscreen_Configure"
    KeyName_Compression = "Compression"
    Keyname_ImageType = "ImageType"
    KeyName_TakePhoto= "TakePhoto"
    KeyName_Path= "Path"
    Keyname_Menu = "Menu"

    KeyName_UseJsonFile = "UseJsonFile"
    KeyName_DDS_Compression = "DDS_Compression"
;Set defaults
    Menu = true
    TakePhoto = 14
    Imagetype = "PNG"
    Path = "C:/Pictures" 
	  Compression = 85.0
    DDS_Compression  = "UNCOMPRESSED"
    RegisterForKey(TakePhoto)

;Validate initial path on startup Note 
;the validation process will try to create c:/pictures but this
;is a standard windows directory and should always extist and be writable. A failure here
;means a problem with the operating system you will have to fix.
 Result = PrintScreen_formula_SCript.PrintScreen(1,Path,Imagetype,Compression, DDS_Compression )
if(Result != "Success")
  Debug.Notification("Printscreen - On Initialization Path failed validation \n use MCM to fix")

else
;Debug.MessageBox("Path validated succeccfully\n"+ Path)

endif
;json logic. How to determine if a jason file should be loaded.
; if the file exists read from it. If the useJason is 1 load defaaults
; if usejason is 0 do not load variables.
;when the MCM is closed write out all variables.

if(!jsonUtil.jsonExists(JsonFilename))
 Debug.Notification("Json file Not Found - create it")
;Create the jason file
JsonUtil.SetIntValue(JsonFilename,KeyName_TakePhoto,TakePhoto )
jsonUtil.SetStringValue(jsonFileName, KeyName_Path,Path)
jsonUtil.SetFloatValue(jsonFileName, KeyName_Compression,Compression)
jsonUtil.SetStringValue(jsonFilename, KeyName_DDS_Compression,DDS_Compression)
if(menu)
  jsonUtil.SetIntValue(jsonFileName, Keyname_Menu,1)

else
  jsonUtil.SetIntValue(jsonFileName, KeyName_Menu,0)
  
Endif
jsonUtil.SetStringValue(jsonFilename, KeyName_ImageType, ImageType)
if(useJsonFile)
  jsonUtil.SetIntValue(jsonFileName,KeyName_UseJsonFile,1)
else
  jsonUtil.SetIntValue(jsonFileName,KeyName_UseJsonFile,0)
endif
Debug.Notification("Json file initially created")
jsonUtil.Save(jsonFileName, false)
else
;json file exists if good check 
if(JsonUtil.isGood(jsonFilename) && (jsonUtil.getIntValue(jsonFileName,KeyName_UseJsonFile) ==1))
  ;Read values and assign to defaults
  Compression = JsonUtil.GetFloatValue(jsonFilename, KeyName_Compression  )
  ImageType = JsonUtil.getStringValue(jsonFileName,Keyname_ImageType)
  Sanatize_ImageType()
  DDs_Compression = JsonUtil.getStringvalue(jsonFileName, KeyName_DDS_Compression)
  Sanatize_DDS_Compression()
  TakePhoto = JsonUtil.getIntvalue(jsonFileName, KeyName_TakePhoto)
  if(jsonUtil.getintvalue(jsonFileName,Keyname_Menu) == 1 )
    Menu = True
  else
    Menu = False
  endif
  If(JsonUtil.GetIntValue(JsonFilename,KeyName_UseJsonFile)==1)
    useJsonFile = True
  else
    UseJsonFile = False
  Endif

String TestPath = JsonUtil.GetStringValue(jsonFileName, KeyName_Path)
;Debug.MessageBox ("json path was:\n " + TestPath)
Result = PrintScreen_formula_SCript.PrintScreen(1,TestPath,"PNG",85.0,"BC1" )
if(Result != "Success")
  debug.messageBox("Printscreen - Json Path failed Edit json file to fix")

else
 ; Debug.MessageBox("Path validated succeccfully\n"+ Path)
  Path = TestPath
endif
endif 
endif
EndEvent

Event OnKeyUP(int theKey, float holdtime)
  If( (theKey != TakePhoto) || \
      (bConfigOpen) || \
      (ui.IsTextInputEnabled()) )
      ;Debug.MessageBox("Printscreen Returning faild entry tests")
  return
  Endif
  if(Menu)
    toggleHud()
   endif

;  Debug.MessageBox("Calling PrintScreen function with HUD Control: " + HuD_Control)

  Result = PrintScreen_Formula_SCript.PrintScreen(0,Path,ImageType, Compression, DDS_Compression)
    if(Result == "Success")
       Shots = shots + 1
    Endif
    ;regardless of rresult restore menues
if(Menu)  
  toggleHud()       
    endif
EndEvent
