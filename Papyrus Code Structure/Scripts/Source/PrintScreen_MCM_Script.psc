Scriptname PrintScreen_MCM_Script extends Ski_configbase  

PrintScreen_MainQuest_Script property Test  auto

;Set option ID's as Properties
int Property PathID auto
int Property ImageTypeID auto
int Property MenuID auto
int Property PhotoKeyID auto
int Property RemoveMenuID auto
int Property keyCodeID auto
int Property CompressionID auto 
int Property DDS_CompressionID auto  
int Property UseJsonFileID auto
int J = 0
String MyPath = ""  
String InfoText=""
String[] Function DDSArray()
  Int size=9
  string[] dds = Utility.CreateStringArray(size )
  dds[0] = "UNCOMPRESSED"
  dds[1] = "BC1"
  dds[2] = "BC2"
  dds[3] = "BC3"
  dds[4] = "BC4"
  dds[5] = "BC5"
  dds[6] = "BC6h"
  dds[7] = "BC7_Fast"
  dds[8] = "BC7"
  return dds
EndFunction

String[] function OptionArray()
  int size= 6
  string[] OpArray = Utility.CreateStringArray(size )
  OpArray[0]= "PNG"
  OpArray[1]= "BMP"
  OpArray[2]= "TIF"
  OpArray[3] = "JPG"
  OpArray[4] ="GIF"
  OpArray[5] = "DDS"
    return OpArray
  endFunction

  Event onConfigOpen()
    Test.bConfigOpen = true
    ;Debug.MessageBox("on ConfigOpen bConfigOpen = " + Test.bConfigOpen)
endevent

Event OnConfigInit()
    ModName ="Print Screen"  
    pages = New string[1] 
    pages[0] = "settings" 
    RegisterforKey(14) 
EndEvent

EVENT OnPageReset(string pagename) 
if(pagename == "")
    SetCursorFillmode(TOP_TO_BOTTOM )
    SetCursorPosition(1)
    int ImageSelect
    ImageSelect = Utility.RandomInt(1,6)    
    if(ImageSelect == 1)
      LoadCustomContent("PrintScreen/ScreenShot01.dds")
    Endif
     If(Imageselect == 2)
        LoadCustomContent("PrintScreen/ScreenShot02.dds")
     EndIf
    if(Imageselect == 3)
         LoadCustomContent("PrintScreen/ScreenShot03.dds")
    endif
    if(imageSelect == 4)
     LoadCustomContent("PrintScreen/ScreenShot04.dds")
     endif
    if(imageSelect == 5)
         LoadCustomContent("PrintScreen/ScreenShot05.dds")
    endif
    if(imageSelect ==6)
      LoadCustomContent("PrintScreen/ScreenShot06.dds")
    endif
else
    UnloadCustomContent()
Endif


if(pagename == "Settings")
    SetCursorFillmode(TOP_TO_BOTTOM )
    SetCursorPosition(0)
    AddheaderOption("Printscreen version "+Test.Version)
    
    addEmptyOption() 
  int OL=0

  int O_flag = OPTION_FLAG_NONE as int 
  OL= stringUtil.getLength(Test.Path)
if(OL >30)
  MyPath = "Json Long Path Option"
  InfoText="Long Text Input Frpm json File MCM path editing Disabled"
  O_Flag = OPTION_FLAG_DISABLED as int
  
else
  MyPath=Test.Path
  InfoText="Enter path to Image Storage"
  O_flag = OPTION_FLAG_NONE as int
endif
    PathID = AddInputOption("Path",MyPath, O_Flag)
    AddEmptyOption() 
    int I = OptionArray().Find(Test.ImageType)
    if((I<0)||(I>5))
      I=0
    endif
    ImageTypeID = AddMenuOption("Select Image file type",OptionArray()[I] , 0)
   AddEmptyOption() 
   RemoveMenuID = AddToggleOption("Automatic Menu Revoval",Test.Menu, 0)
   AddEmptyOption()
   CompressionID =  AddSliderOption("JPG/TIF Quality",Test.Compression,"{0}", 0)
   KeyCodeID = AddKeyMapOption( "Select Take Photo Key", Test.TakePhoto, 0)
  AddEmptyOption()   
   UseJsonFileID = AddToggleOption("Save/Restore Configuration",Test.UseJsonFile)

  ; Debug.MessageBox("Test.Compression was: " + Test.DDS_Compression)
   SetCursorPosition(3)

 J = DDSArray().Find(Test.DDS_Compression)
  if(J<0)
    J=0
  Endif
DDS_CompressionID = AddMenuOption("DDS Mode",\
   DDSArray()[J],0)
  EndIf
EndEvent

event OnOptionHighlight(int option)
  if(option ==ImageTypeID )
      SetInfoText("Select the type of image to create ")
  elseif(option == RemoveMenuID )
    SetInfoText("Toggle Automatic Removal of HUD/Menu ")
  elseif(option== KeyCodeID )
    SetInfoText("Select an unused Key to TAKE pHOTO ")
  Elseif(option == PathID  )
setinfoText(InfoText)
elseif(option == CompressionID)
SetInfoText("Select QUALITY factor for jpg and Tiff files  50-LOWEST TO 100 HIGHEST quality")
elseif(option == UseJsonFileID)
  SetInfoText("Enable Configuration Paramiters to be Saved/Restored")
elseif(option == DDS_CompressionID)
  SetInfoText("Select DDS mode of operation\nNote BC6h, BC7_Fast and BC7 Take several minutes")
  endif 
EndEvent

;path input processing

event OnOptionInputAccept(int a_option, string a_input)
	if(a_Option == PathID)  
    Test.validate=1
String Result=PrintScreen_formula_Script.PrintScreen( test.Validate, a_input, Test.ImageType, Test.Compression, Test.DDS_Compression) 
		if(Result== "Success") 
      Test.Path =a_input
      SetInputOptionValue(a_Option,Test.Path,false)
      Test.Validate=0
    else
        Debug.MessageBox("Path failed Validation/Creation Reenter")
    endif	
		;debug.MessageBox(Test.Path)
      Endif
endEvent

;Toggle Input processing.

Event  OnOptionSelect(int Option)
  if(Option == RemoveMenuID)
    Test.Menu = !Test.menu
    SetToggleOptionValue(Option,Test.menu, false)

  endif
  if(option == UseJsonFileID)
    Test.UseJsonFile = ! Test.UseJsonFile
    SetToggleOptionValue(option,Test.UseJsonFile,false)
  Endif
endevent

; process keyCode selection

Event OnOptionKeyMapChange(int Option, int keyCode, string conflictControl, string conflictName)
;check for conflicts

if( (conflictName =="") && (conflictcontrol == "") && (input.GetMappedControl(KeyCode)==""))
    unregisterForKey(Test.TakePhoto)
    Test.TakePhoto = keyCode
    ;debug.MessageBox("No Conflict assign takephoto: " + Test.TakePhoto )
    SetKeyMapOptionValue( Option,Test.TakePhoto  , false)
Else 
Debug.messagebox("Key conflict Detected -- Please choose an unassined key")
endif 
endEvent

; Process Menu operations

event OnOptionMenuOpen(int Option)
if(Option == ImageTypeID) 
  SetMenuDialogOptions(OptionArray())
	SetMenuDialogDefaultIndex(0)
	SetMenuDialogStartIndex(0)   
elseif(Option == DDS_CompressionID)  
  SetMenuDialogOptions(DDSArray())
	SetMenuDialogDefaultIndex(0)
	SetMenuDialogStartIndex(0) 
Endif
EndEvent

Event OnOptionMenuAccept(int Option, int  index)
  if(index <0)
    index = 0
  endif
  if(Option == ImageTypeID)
  Test.ImageType= OptionArray()[index]
  SetMenuOptionValue( option, Test.Imagetype, false)
  ;  debug.MessageBox("the imagde type was" +Test.ImageType)
  elseif(Option == DDS_CompressionID)
    ;Debug.MessageBox("DDS_Compression[ " + index + " ]" + " was " + DDSArray()[index ])
    Test.DDS_Compression = DDSArrAY()[index]
    SetMenuOptionValue( option, DDSArray()[index],false)

  endif
EndEvent

Event OnOptionSliderOpen(int a_option)
	;{Called when a slider option has been selected}
	 SetSliderOptionValue(a_option, Test.Compression,  "{0}", false)
	 SetSliderDialogStartValue(Test.Compression)
	 SetSliderDialogDefaultValue(85.0)
	 SetSliderDialogRange(0.0, 100.0)
	 SetSliderDialogInterval(5.0)
endEvent

event OnOptionSliderAccept(int a_option, float a_value)
	;{Called when a new slider value has been accepted}
	Test.Compression = a_value
SetSliderOptionValue(a_option, Test.Compression,  "{0}", false)
endEvent
 
event OnConfigClose()
  ;bConfigOpen is a flag variable which prevents activation of
  ;PrintScreenKey while the MCM is open
  Test.bConfigOpen = false 
  RegisterforKey(Test.TakePhoto )
if(Test.UseJsonFile)
  JsonUtil.SetFloatValue(Test.jsonFilename,Test.Keyname_Compression, Test.Compression)
  JsonUtil.SetStringValue(Test.jsonFilename, Test.Keyname_ImageType, Test.ImageType)
  JsonUtil.SetStringValue(Test.jsonFilename, Test.KeyName_Path, Test.Path)
  jsonUtil.SetStringValue(Test.JsonFilename, Test.KeyName_DDS_Compression,Test.DDS_Compression)
  JsonUtil.SetIntValue(Test.jsonFilename, Test.keyName_TakePhoto, Test.TakePhoto)
  jsonUtil.SetIntValue(Test.jsonFilename, Test.KeyName_UseJsonFile,1)
 
  if(Test.Menu)
    JsonUtil.SetIntValue(Test.jsonFilename, Test.KeyName_Menu,1)
  else
    JsonUtil.SetIntValue(Test.jsonFilename, Test.Keyname_Menu,0)
  EndIf
  JsonUtil.Save(Test.jsonFilename)
Endif
EndEvent
