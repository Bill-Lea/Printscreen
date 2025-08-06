Scriptname Printscreen_ModConfig_scripot extends SKI_ConfigBase  

Printscreen_MainQuest_script Property Mainquest Auto 


;Establish Properties 
;Set option ID's as Properties
int Property PathID auto
int Property ImageTypeID auto
int Property MenuID auto
int Property PhotoKeyID auto
int Property RemoveMenuID auto
int Property keyCodeID auto
int Property JPG_CompressionID auto ;Slider for JPG Compression
int Property Tif_ModeID auto ;Menu for Tif Compression Mode
int pRoperty DDS_ModeID auto ;Menu for DDS Mode
int Property Gif_ID auto
int Property Gif_MultiFrame_ID Auto
int Property GIF_MultiFrame_DurationID Auto
int Property UseJsonFileID Auto



;Establish variables 
int J = 0
int ImageTypeID = 0
String MyPath = ""  
String InfoText=""

;initialize the initial modifyer flags to disable the widgits
int TIF_OPTION_Flag = 0X00000001  ;OPTION_FLAG_DISABLED
int JPG_OPTION_FLAG = 0X00000001  ;OPTION_FLAG_DISABLED
int DDS_OPTION_FLAG = 0X00000001  ;OPTION_FLAG_DISABLED
int GIF_MULTIFRAME_OPTION_FLAG = 0X00000001 ;OPTION_FLAG_DISABLED


;Establish functions 

String[] Function DDSArray()
  Int size= 10
  string[] dds = Utility.CreateStringArray(size )
  dds[0] = "UNCOMPRESSED"
  dds[1] = "BC1"
  dds[2] = "BC2"
  dds[3] = "BC3"
  dds[4] = "BC4"
  dds[5] = "BC5"
  dds[6] = "BC6h"
  dds[7] = "BC7_SLOW"
  dds[8] = "BC7_NORMAL"
  DDS[9] = "BC7_FAST"  
  return dds
EndFunction

String[] function ImageArray()
  int size= 7  
  string[] OpArray = Utility.CreateStringArray(size )
  OpArray[0]= "PNG"
  OpArray[1]= "BMP"
  OpArray[2]= "TIF"
  OpArray[3] = "JPG"
  OpArray[4] ="GIF"
  OpArray[5] = "GIF_MULTIFRAME"
  OpArray[6] = "DDS"
  return OpArray
  endFunction
  
  String[] function TifArray()
  int size = 4
  String[] Tif_Array = Utility.CreateStringArray(size )
  Tif_Array[0] ="UNCOMPRESSED"
  Tif_Array[1] = "RLE"
  Tif_Array[2] = "LZW"
  Tif_Array[3] = "ZIP"
 return Tif_Array
EndFunction

;Establish Events

Event onConfigOpen()
    MainQuest.bConfigOpen = true
    ;Debug.MessageBox("on ConfigOpen bConfigOpen = " + MainQuest.bConfigOpen)
endevent

Event OnConfigInit()
    ModName ="PrintScreen"  
    pages = New string[1] 
    pages[0] = "Settings" 
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

 ; Set Initial Flags
 ;here we set the initial flags for the JPG TIF and GIF
 ; widgits. First set all the widgets to Off.
TIF_OPTION_FLAG = 0X00000001 ;OPTION_FLAG_DISABLED
JPG_OPTION_FLAG = 0X00000001 ;OPTION_FLAG_DISABLED
DDS_OPTION_FLAG = 0X00000001 ;OPTION_FLAG_DISABLED
GIF_MULTIFRAME_OPTION_FLAG = 0X00000001 ;OPTION_FLAG_DISABLED
;Now set the widgits according to the current MainQuest.ImageType
if(MainQuest.ImageType == "TIF")
  TIF_OPTION_FLAG = 0X00000000 ;OPTION_FLAG_NONE 
Elseif(MainQuest.ImageType == "JPG")
  JPG_OPTION_FLAG = 0X00000000 ;OPTION_FLAG_NONE
elseif(MainQuest.ImageType == "DDS")
  DDS_OPTION_FLAG = 0X00000000 ;OPTION_FLAG_NONE
elseif(MainQuest.ImageType == "GIF_MULTIFRAME")
  GIF_MULTIFRAME_OPTION_FLAG = 0X00000000 ;OPTION_FLAG_NONE
Endif 

;Create all the widgits Here

    SetCursorFillmode(TOP_TO_BOTTOM )
    SetCursorPosition(0)
    AddheaderOption("Printscreen version "+MainQuest.Version)    
	  AddEmptyOption() 
	
	int OL=0
	int O_flag = OPTION_FLAG_NONE as int 
	OL= stringUtil.getLength(MainQuest.Path)
	if(OL >30)
		MyPath = "Json Long Path Option"
		InfoText="Long Text Input Frpm json File MCM path editing Disabled"
		O_Flag = OPTION_FLAG_DISABLED as int
	else
		MyPath=MainQuest.Path
		InfoText="Enter path to Image Storage"
		O_flag = OPTION_FLAG_NONE as int
	endif

   PathID = AddInputOption("Path",MyPath, O_Flag)

   AddEmptyOption() 
  
    int I =ImageArray().Find( MainQuest.ImageType )
    if (I<0) 
      I=0
    endif
  ImageTypeID = AddMenuOption("Select Image file type",ImageArray()[I] , 0)
  AddEmptyOption()   
  RemoveMenuID = AddToggleOption("Automatic Menu Revoval",MainQuest.Menu, 0)

  AddEmptyOption()
  KeyCodeID = AddKeyMapOption( "Select Take Photo Key", MainQuest.Key_TakePhoto, 0)
  AddEmptyOption() 
   UseJsonFileID = AddToggleOption("Save/Restore Configuration",MainQuest.UseJsonFile)

  
  ; Move to Left colum add options for jpg Compression, Tif Compression Mode,  DDS_Mode 
  ; and  Gif MultiFrame_Duration
  SetCursorPosition(3)
  JPG_CompressionID  =  AddSliderOption("JPG Quality",MainQuest.Jpg_Compression,"{0}", JPG_OPTION_FLAG)
   AddEmptyOption() 
    GIF_MultiFrame_DurationID = AddSliderOption("Gif Capture Duration", MainQuest.GIF_MultiFrame_Duration, "{0}", GIF_MULTIFRAME_OPTION_FLAG)
   AddEmptyOption() 
   I=DDSArray().Find(MainQuest.DDS_Mode)
  if(I<0)
    I=0
  endif

   DDS_ModeID = AddMenuOption("DDS Compression Mode",DDSArray()[I],DDS_OPTION_FLAG)

   I=TifArray().Find(MainQuest.Tif_Mode)
  if(I<0)
    I=0
  endif 
   Tif_ModeID = AddMenuOption("Tif Compression Mode", TifArray()[I],Tif_OPTION_FLAG )
  endif 
     EndEvent

event OnOptionHighlight(int option)
  if(option ==ImageTypeID )
      SetInfoText("Select the type of image to create ")
  elseif(option == RemoveMenuID )
    SetInfoText("Toggle Automatic Removal of HUD/Menu ")
  elseif(option== KeyCodeID )
    SetInfoText("Select an unused Key to TAKE pHOTO ")
  Elseif(option == PathID  )
      setinfoText("Enrer Path String \nMust be absolute and contain no illegal charicters")
  (option == Jpg_CompressionID)
	SetInfoText("Select QUA	LITY factor for jpg and Tiff files  50-LOWEST TO 100 HIGHEST quality")
   elseif(option == UseJsonFileID)
    SetInfoText("Enable Configuration Paramiters to be Saved/Restored")
  elseif(option == DDS_ModeID) 
    SetInfoText("Select DDS mode of operation\nNote BC6h, BC7_Fast and BC7 Take several minutes")
  elseif(option == GIF_MultiFrame_DurationID) 
  SetInfoText("Select GIF MultiFrame Duration of image collection (seconds)")
  endif 
EndEvent

;path input processing

event OnOptionInputAccept(int a_option, string a_input)
	if(a_Option == PathID) 
	bool bRes=Printscreen_Formula_script.checkpath(a_input)
		if(bRes) 
      MainQuest.Path =a_input
      SetInputOptionValue(a_Option,MainQuest.Path,false)
    else
        Debug.MessageBox("Path failed Validation/Creation Reenter")
        SetInputOptionValue(a_Option,MainQuest.Path,false)
    endif	
		;debug.MessageBox(MainQuest.Path)
      Endif
endEvent

;Toggle Input processing.

Event  OnOptionSelect(int Option)
  if(Option == RemoveMenuID)
    
    MainQuest.Menu = !MainQuest.Menu
    SetToggleOptionValue(Option,MainQuest.Menu, false)
  endif
  if(option == UseJsonFileID)
    
    MainQuest.UseJsonFile = ! MainQuest.UseJsonFile
    SetToggleOptionValue(option,MainQuest.UseJsonFile,false)
  Endif
EndEvent 

; process keyCode selection

Event OnOptionKeyMapChange(int Option, int keyCode, string conflictControl, string conflictName)
;check for conflicts

	if((conflictName =="") && (conflictcontrol == "") && (input.GetMappedControl(KeyCode)==""))
		unregisterForKey(MainQuest.Key_TakePhoto)
		MainQuest.Key_TakePhoto = keyCode
		; debug.MessageBox("No Conflict assign takephoto: " + MainQuest.Key_TakePhoto )
		SetKeyMapOptionValue( Option,MainQuest.Key_TakePhoto  , false)
	Else 
		Debug.messagebox("Key conflict Detected -- Please choose an unassined key")
	endif 
endEvent

; Process Menu operations

event OnOptionMenuOpen(int Option)
	; We have Menues for Image, Tif and DDS_Compression Mode to be handled
	;Debug.Messagebox("OnOptionMenuOpen called for Option: " + Option) 
	if(Option == ImageTypeID)
		int I = ImageArray().Find(MainQuest.ImageType)
		if(i<0)
		I=0
		endif 
		SetMenuDialogOptions(ImageArray())
		SetMenuDialogDefaultIndex(0)
		SetMenuDialogStartIndex(I)  
	elseif(Option== TIF_MODEID)
		int I = TifArray().Find(MainQuest.Tif_Mode)
		if(I < 0)
			I=0
		endif 
		SetMenuDialogOptions(TifArray()) 
		SetMenuDialogDefaultIndex(0)
		SetMenuDialogStartIndex(I)   
	elseif(option == DDS_MODEID)
		int I  = DDSArray().Find(MainQuest.DDS_Mode)
		if(I<0	)
		I = 0 
	endif 
	SetMenuDialogOptions(DDSArray()) 
	SetMenuDialogStartIndex(I) 
	SetMenuDialogDefaultIndex(0)
	Endif 
EndEvent


Event OnOptionMenuAccept(int Option, int  index)
	;When Initially opened the widges will have been Enabled according to the values of THe MainQuest variables
	;so it may be possible to skip the initial ImageType Menu, however If you select an
	;ImageType all these wigits should be reset.

	if(index <0)
		index = 0
	endif
	if(Option == ImageTypeID)
		MainQuest.ImageType= ImageArray()[index]
		SetMenuOptionValue( option, MainQuest.Imagetype, false)

		;below here is where the on/off code goes
		;Initially turn them all off
		SetOptionFlags(JPG_CompressionID, OPTION_FLAG_DISABLED)
		SetOptionFlags(Tif_ModeID, OPTION_FLAG_DISABLED)
		SetOptionFlags(DDS_ModeID, OPTION_FLAG_DISABLED)
		SetOptionFlags(GIF_MultiFrame_DurationID, OPTION_FLAG_DISABLED)
		;Turn On Selected Wigits according to selected ImaageType
  
	if(MainQuest.ImageType == "JPG")
		SetOptionFlags(JPG_CompressionID, OPTION_FLAG_NONE)
  
	elseif(MainQuest.ImageType == "TIF")
		SetOptionFlags(Tif_ModeID, OPTION_FLAG_NONE)  
	elseif(MainQuest.ImageType =="DDS")  
		SetOptionFlags(DDS_ModeID, OPTION_FLAG_NONE)
	elseif(MainQuest.ImageType=="GIF_MULTIFRAME")
		SetOptionFlags(GIF_MultiFrame_DurationID, OPTION_FLAG_NONE)
	endif
endif

;need to handle the other menu widgits when selected. There 
;There are two menues to handle: DDS_Mode  and Tif_Mode and 
;These widgits return here for processing 

if(Option == TIF_ModeID )
	MainQuest.Tif_Mode = TifArray()[index]
  MainQuest.Mode = MainQuest.Tif_Mode
	SetMenuOptionValue(Option,TifArray()[index])
elseIf(Option == DDS_ModeID) 
	MainQuest.DDS_Mode = DDSArray()[index]
  MainQuest.Mode = MainQuest.DDS_Mode
	SetMenuOptionValue(Option,MainQuest.DDS_Mode)
Endif 
EndEvent

Event OnOptionSliderOpen(int a_option)
	;{Called when a slider option has been selected}
	; There are sliders for Jpg Compression and GIF_MultiFrame_Duration
  If(a_oPTION == Jpg_CompressionID)
	 SetSliderOptionValue(a_option,MainQuest.Jpg_Compression,  "{0}", false)
	 SetSliderDialogStartValue(MainQuest.Jpg_Compression)
	 SetSliderDialogDefaultValue(85.0)
	 SetSliderDialogRange(0.0, 100.0)
	 SetSliderDialogInterval(5.0)
  elseif(a_option == GIF_MultiFrame_DurationID) 
    SetSliderOptionValue(a_option, MainQuest.GIF_MultiFrame_Duration, "{0}", false)
    SetSliderDialogStartValue(MainQuest.GIF_MultiFrame_Duration)
    SetSliderDialogDefaultValue(1.0)
    SetSliderDialogRange(0.0, 30.0)
    SetSliderDialogInterval(1.0)
  ENDIF
endEvent

event OnOptionSliderAccept(int a_option, float a_value)
	;{Called when a new slider value has been accepted}
	;there aare two sliders to process: Jpg Compression and GIF_MiltiFrame_Duration
  If(a_option == JPG_CompressionID) 
	MainQuest.Jpg_Compression = a_value
    SetSliderOptionValue(a_option, MainQuest.Jpg_Compression,  "{0}", 0)
Elseif(a_Option ==  GIF_MultiFrame_DurationID)
    MainQuest.GIF_MultiFrame_Duration = a_value
    SetSliderOptionValue(a_option, MainQuest.GIF_MultiFrame_Duration, "{0}", false)
  Endif
endEvent

 
event OnConfigClose()
  ;bConfigOpen is a flag variable which prevents activation of
  ;PrintScreenKey while the MCM is open
  MainQuest.bConfigOpen = false 
  RegisterforKey(MainQuest.Key_TakePhoto )
  if(MainQuest.UseJsonFile)
  JsonUtil.SetFloatValue(MainQuest.jsonFilename,MainQuest.Keyname_JPG_Compression, MainQuest.Jpg_Compression)
  JsonUtil.SetStringValue(MainQuest.jsonFilename, MainQuest.Keyname_ImageType, MainQuest.ImageType)
  JsonUtil.SetStringValue(MainQuest.jsonFilename, MainQuest.KeyName_Path, MainQuest.Path)
  JsonUtil.SetStringValue(MainQuest.jsonFilename, MainQuest.KeyName_Mode,MainQuest.Mode)
  JsonUtil.SetStringValue(MainQuest.jsonFilename, MainQuest.KeyName_Tif_Mode, MainQuest.Tif_Mode)
  JsonUtil.SetStringValue(MainQuest.jsonFilename, MainQuest.KeyName_DDS_Mode, MainQuest.DDS_Mode)
  JsonUtil.SetFloatValue(MainQuest.jsonFilename, MainQuest.KeyName_JPG_Compression, MainQuest.JPG_Compression)
  jsonUtil.SetStringValue(MainQuest.JsonFilename, MainQuest.KeyName_Mode,MainQuest.Mode)
  jsonUtil.SetFloatValue(MainQuest.JsonFilename, MainQuest.KeyName_GIF_Duration,MainQuest.GIF_MultiFrame_Duration)
  JsonUtil.SetIntValue(MainQuest.jsonFilename, MainQuest.keyName_TakePhoto, MainQuest.Key_TakePhoto)
  jsonUtil.SetIntValue(MainQuest.jsonFilename, MainQuest.KeyName_UseJsonFile,1)
 
  if(MainQuest.Menu)
    JsonUtil.SetIntValue(MainQuest.jsonFilename, MainQuest.KeyName_Menu,1)
  else
    JsonUtil.SetIntValue(MainQuest.jsonFilename, MainQuest.Keyname_Menu,0)
  EndIf
  JsonUtil.Save(MainQuest.jsonFilename)
Endif
EndEvent
