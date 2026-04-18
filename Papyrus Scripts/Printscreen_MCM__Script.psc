Scriptname Printscreen_MCM__Script extends SKI_Configbase

;Establish Properties 
Printscreen_MainQuest_script Property Mainquest Auto 

;Set option ID's as Properties
int Property PathID auto
int Property ImageTypeID auto
int Property MenuID auto
int Property PhotoKeyID auto
int Property RemoveMenuID auto
int Property keyCodeID auto
int Property JPG_CompressionID auto ;Slider for JPG Compression
int Property DIF_Compression_ID auto ;Slider for PNG Compression

int Property Tif_ModeID auto ;Menu for Tif Compression Mode
int Property DDS_ModeID auto ;Menu for DDS Mode
int Property GIF_ModeID auto ;Slider for GIF/AGIF Mode 
int Property AGIF_ModeID auto ;Slider for AGIF Mode ID
Int Property GIF_ScalePct_ID auto ;Slider for GIF Scale Percentage
 
int Property LoopID auto ;Slider for AGIF Loop Count
int Property OptimizeID auto ;Slider for Optimize Differential Compression
int Property DurationID Auto
int Property AGIF_APNG_Quality_ID auto ;Slider for Optimize Differential Compression


;Set variables 
int J = 0
int ImageTypeID = 0
String MyPath = ""  
String InfoText=""

;initialize the initial modifyer flags to disable the widgits
int TIF_OPTION_Flag = 0X00000001  ;OPTION_FLAG_DISABLED
int JPG_OPTION_FLAG = 0X00000001  ;OPTION_FLAG_DISABLED
int DDS_OPTION_FLAG = 0X00000001  ;OPTION_FLAG_DISABLED
int GIF_OPTION_FLAG = 0X00000001 ;OPTION_FLAG_DISABLED
int AGIF_OPTION_FLAG = 0X00000001 ;OPTION_FLAG_DISABLED
int PNG_OPTION_FLAG =   0X00000001 ;OPTION_FLAG_DISABLED
int APNG_OPTION_FLAG     = 0x00000001 ;OPTION_FLAG_DISABLED
int ANIMATED_OPTION_FLAG = 0x00000001 ;OPTION_FLAG_DISABLED

;Define  functions 
float Function SetFPS(float TimeLength)
if(TimeLength<4.0)
return 15.0
elseif(TimeLength <6.0)
return 12.0
elseif(TimeLength<9.0)
return 10.0
elseif(TimeLength<13.0)
return 8.0
else
return 6.0
endif 
EndFunction

Function ResetFlags()
;Set all widgits to disabled
SetOptionFlags(Tif_ModeID, OPTION_FLAG_DISABLED)
setOptionFlags(JPG_CompressionID, OPTION_FLAG_DISABLED)
setOptionFlags(DDS_ModeID,  OPTION_FLAG_DISABLED)
setOptionFlags(GIF_ModeID,  OPTION_FLAG_DISABLED)

setOptionFlags(AGIF_ModeID, OPTION_FLAG_DISABLED)
setOptionFlags(OptimizeID, OPTION_FLAG_DISABLED)
SetOPTIONFlags(LoopID, OPTION_FLAG_DISABLED)
SetOPTIONFlags(AGIF_APNG_Quality_ID, OPTION_FLAG_DISABLED)
SetOptionFlags(DIF_Compression_ID, OPTION_FLAG_DISABLED)
 SetOptionFlags(DurationID,    OPTION_FLAG_DISABLED) 

EndFunction



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
  int size= 8  
  string[] OpArray = Utility.CreateStringArray(size )
  OpArray[0]= "PNG"
  OpArray[1]="APNG"
  OpArray[2]= "BMP"
  OpArray[3]= "TIF"
  OpArray[4] = "JPG"
  OpArray[5] ="GIF"
  OpArray[6] = "AGIF"
  OpArray[7] = "DDS"
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
;Define  Arrays 
;;Gif Arrays

;+========================================================= Array Definitionns
;PNG Arrays

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
    
    int I = ImageArray().Find(MainQuest.ImageType)
    if (I<0) 
      I=0; png is default if find functon fails 
Debug.Notification("Invalid Initial Image TYPE Detected.Defaulting to PNG type")             
    endif
  ImageTypeID = AddMenuOption("Select Image file type",ImageArray()[I] , 0)
  AddEmptyOption()   
  RemoveMenuID = AddToggleOption("Automatic UI disable (Experimental)",MainQuest.Menu, 0)

  AddEmptyOption()
  KeyCodeID = AddKeyMapOption( "Select Take Photo Key", MainQuest.Key_TakePhoto, 0)
  
  
  ; Move to Left colum add options for jpg Compression, Tif Compression Mode,  DDS_Mode 
  ; and  Gif MultiFrame_Duration
  SetCursorPosition(3)
    AddEmptyOption()

   If(MainQuest.ImageType == "APNG" || MainQuest.ImageType == "AGIF")
    AGIF_APNG_Quality_ID = AddSliderOption("Differential Quality" , MainQuest.Quality , "{0}",OPTION_FLAG_NONE) 
    DurationID = AddSliderOption("Capture Duration", MainQuest.Duration, "{0}", OPTION_FLAG_NONE)
    Else
       AGIF_APNG_Quality_ID = AddSliderOption("Differential Quality" , MainQuest.Quality , "{0}",OPTION_FLAG_DISABLED) 
    DurationID = AddSliderOption("Capture Duration", MainQuest.Duration, "{0}", OPTION_FLAG_DISABLED)
    EndIf 
   AddEmptyOption()
   
   
I = DDSArray().fIND(  Mainquest.DDS_MODE)
    if(I<0)
      I=0  
      Debug.Notification("Invalid Initial DDS mode Set DDSMODE to UNCOMPRESSED")  
    endif
    IF(MainQuest.ImageTYPE == "DDS")
   DDS_ModeID = AddMenuOption("DDS Compression Mode", DDSArray()[I],OPTION_FLAG_NONE )  
    Else
      DDS_ModeID = AddMenuOption("DDS Compression Mode", DDSArray()[I],OPTION_FLAG_DISABLED )  
    Endif
   
  I = TifArray().FIND( MainQuest.Tif_Mode)
     if(I<0)
      I=0
   endif 
   If(MainQuest.ImageType == "TIF")
      Tif_ModeID = AddMenuOption("Tif Compression Mode", TifArray()[I],OPTION_FLAG_NONE )  
   Else
    Tif_ModeID = AddMenuOption("Tif Compression Mode", TifArray()[I],OPTION_FLAG_DISABLED ) 
  Endif 

  If(MainQuest.ImageType == "JPG")
      
      JPG_CompressionID = AddSliderOption("JPG Quality", MainQuest.Jpg_Compression, "{0}", OPTION_FLAG_NONE)
    Else
      
    JPG_CompressionID = AddSliderOption("JPG Quality", MainQuest.Jpg_Compression, "{0}", OPTION_FLAG_DISABLED)
  EndIf 

  If(Mainquest.ImageType == "AGIF" || MainQuest.ImageType == "APNG")
    LoopID = AddSliderOption("Loop Count", 0.0, "{0}",OPTION_FLAG_NONE) 
    OptimizeID = AddSliderOption("Optimize", MainQuest.Optimize , "{0}", OPTION_FLAG_NONE) 
  Else
    LoopID = AddSliderOption("Loop Count", 0.0, "{0}",OPTION_FLAG_DISABLED) 
    OptimizeID = AddSliderOption("Optimize", MainQuest.Optimize , "{0}", OPTION_FLAG_DISABLED)   
  Endif
ENDIF 

if(Mainquest.ImageType == "APNG" || MainQuest.ImageType == "AGIF")
DIF_Compression_ID = AddSliderOption("Differential Compression", MainQuest.Compression as float, "{0}", OPTION_FLAG_NONE )
Else
DIF_Compression_ID = AddSliderOption("Differential Compression", MainQuest.Compression as float, "{0}", OPTION_FLAG_DISABLED )
endIf
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
  elseif(option == Jpg_CompressionID)
    SetInfoText("Select QUALITY factor for jpg and Tiff files  50-LOWEST TO 100 HIGHEST quality")
  elseif(option == DDS_ModeID) 
    SetInfoText("Select DDS mode of operation\nNote BC6h, BC7_Fast and BC7 Take several minutes")
  elseif(option == DurationID) 
  SetInfoText("Select Duration of image collection (seconds)")
  elseif(option == OptimizeID)
    SetInfoText("Enable Optomization of Animated images (AGIF/APNG) \n May increase processing time") 
  elseif(option ==    DIF_Compression_ID)
    SetInfoText("Select  Compression Level 0 (none) to 1  ")
  elseif(option == LoopID)
    SetInfoText("Select the Number of times to Loop Animated Image \n 0 = Infinite Looping")
   elseif(option == AGIF_APNG_Quality_ID)
    SetInfoText("Select Quality Factor for Animated Images (APNG/AGIF) 0 Low to 100 High")
  endif 
EndEvent

;path input processing

event OnOptionInputAccept(int a_option, string a_input)
	if(a_Option == PathID) 
    bool bRes = false
   bRes=Printscreen_Formula_script.checkpath(a_input)
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
ResetFlags() 
	; Weave Menues for Image, Tif and DDS_Compression Mode to be handled
	;Debug.Messagebox("OnOptionMenuOpen called for Option: " + Option) 
if(Option == ImageTypeID)
	ResetFlags()
    int I = ImageArray().Find( MainQuest.ImageType)
		if(I<0)
		I =0
		endif 
		SetMenuDialogOptions(ImageArray())
		SetMenuDialogDefaultIndex(0)
		SetMenuDialogStartIndex(I) 
;Set the status of aSSOCIATED WIDGETS ACCORDING TO SELECTED iMAGE TYPE
if(MainQuest.ImageType == "TIF")
  	ResetFlags()   
  SetOptionFlags(TIF_ModeID, OPTION_FLAG_NONE)

Elseif(MainQuest.ImageType == "JPG")
  	ResetFlags()
    SetOptionFlags(JPG_CompressionID, OPTION_FLAG_NONE)
elseif(MainQuest.ImageType == "DDS")
  ResetFlags()   
  SetOptionFlags(DDS_ModeID, OPTION_FLAG_NONE)
elseif(MainQuest.ImageType == "GIF")  
  	ResetFlags()
SetOptionflags(GIF_MODEID,OPTION_FLAG_NONE)

elseif(MainQuest.ImageType == "AGIF")
  ResetFlags()
  setOptionFlags(AGIF_ModeID,OPTION_FLAG_NONE)
  SetOptionFlags(OptimizeID, OPTION_FLAG_NONE)
  SetOPTIONFlags(LoopID, OPTION_FLAG_NONE)
  SetOPTIONFlags(AGIF_APNG_Quality_ID  , OPTION_FLAG_NONE)
  SetOptionFlags(DIF_Compression_ID, OPTION_FLAG_NONE)
  SetOptionFLAGS(DurationID, OPTION_FLAG_NONE)

elseif(MainQuest.ImageType=="PNG")
  ResetFlags()	
  

Elseif (Mainquest.ImageType == "APNG")
    ResetFlags()     
    SetOptionFlags(OptimizeID, OPTION_FLAG_NONE)  
    SetOPTIONFlags(LoopID,OPTION_FLAG_NONE)
    SetOPTIONFlags(AGIF_APNG_Quality_ID  , OPTION_FLAG_NONE)
    SetOptionFlags(DIF_Compression_ID, OPTION_FLAG_NONE)
    SetOptionFLAGS(DurationID, OPTION_FLAG_NONE)
    
elseif(MainQuest.ImageType == "BMP" )
  ;no additional widgits to enable
  Resetflags( )
ENDIF

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
 else
	Debug.MessageBox( "Invalid OptionID " + Option ) 
      EndIf
EndEvent


Event OnOptionMenuAccept(int Option, int  index)
	;When Initially opened the widges will have been Enabled according to the values of THe MainQuest variables
	;so it may be possible to skip the initial ImageType Menu, however If you select an
	;ImageType all these wigits should be reset.


if(Option == ImageTypeID)
  ;;Debug.MessageBox("ImageType Menu Accepted Index: " + index)
		MainQuest.ImageType= ImageArray()[index]
		SetMenuOptionValue( option, MainQuest.Imagetype, false)
;Debug .MessageBox("Selected Image Type: " + MainQuest.ImageType)
		;below here is where the on/off code goes
		;Initially turn them all off
	ResetFlags()
		;Turn On Selected Wigits according to selected ImageType
  
 	if(MainQuest.ImageType == "JPG")
    		
		SetOptionFlags(JPG_CompressionID, OPTION_FLAG_NONE)

	elseif(MainQuest.ImageType == "TIF")
    		TIF_OPTION_FLAG =OPTION_FLAG_NONE
		SetOptionFlags(Tif_ModeID, TIF_OPTION_FLAG)

	elseif(MainQuest.ImageType =="DDS")    
		DDS_OPTION_FLAG = OPTION_FLAG_NONE  
		SetOptionFlags(DDS_ModeID, DDS_OPTION_FLAG)
;Debug.MessageBox("DDS Option Enabled")
 	elseif(MainQuest.ImageType == "GIF")    		
		SetOptionFlags(GIF_ModeID, OPTION_FLAG_NONE)

	Elseif(MainQuest.ImageType == "AGIf")		   
    SetOptionFlags(OptimizeID, OPTION_FLAG_NONE)
    SetOPTIONFlags(LoopID, OPTION_FLAG_NONE)
    SetOPTIONFlags(AGIF_APNG_Quality_ID  , OPTION_FLAG_NONE)
    SetOptionFlags(DIF_Compression_ID, OPTION_FLAG_NONE)
    SetOptionFLAGS(DurationID, OPTION_FLAG_NONE)

	Elseif(MainQuest.ImageType == "APNG")
    SetOptionFlags(OptimizeID, OPTION_FLAG_NONE)
    SetOPTIONFlags(LoopID, OPTION_FLAG_NONE)
    SetOPTIONFlags(AGIF_APNG_Quality_ID  , OPTION_FLAG_NONE)
    SetOptionFlags(DIF_Compression_ID, OPTION_FLAG_NONE)
    SetOptionFLAGS(DurationID, OPTION_FLAG_NONE)

	Elseif(MainQuest.ImageType == "PNG") 
		 ResetFlags() 
		
  else 
    ;no additional widgits to enable
    Resetflags( )
	endif
;need to handle the other menu widgits when selected. There 
;There are two menues to handle: DDS_Mode  and Tif_Mode and 
;These widgits return here for processing 

Elseif(Option == TIF_ModeID )
	MainQuest.Tif_Mode = TifArray()[index]
  MainQuest.Mode = MainQuest.Tif_Mode
	SetMenuOptionValue(Option,TifArray()[index])  
    SetOptionFlags(TIF_ModeID, TIF_OPTION_FLAG)
elseIf(Option == DDS_ModeID) 
	MainQuest.DDS_Mode = DDSArray()[index]
 	 MainQuest.Mode = MainQuest.DDS_Mode
	SetMenuOptionValue(Option,MainQuest.DDS_Mode)
    SetOptionFlags(DDS_ModeID, DDS_OPTION_FLAG)
endif
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
  elseif(a_option == DIF_Compression_ID)
    SetSliderOptionValue(a_option, MainQuest.Compression as float, "{0}", false)
    SetSliderDialogStartValue(MainQuest.Compression as float)
    SetSliderDialogDefaultValue(1.0)
    SetSliderDialogRange(0.0, 1.0)
    SetSliderDialogInterval(1.0)
  elseif(a_option == DurationID) 
    SetSliderOptionValue(a_option, MainQuest.Duration, "{0}", false)
    SetSliderDialogStartValue(MainQuest.Duration)
    SetSliderDialogDefaultValue(1.0)
    SetSliderDialogRange(0.0, 15.0)
    SetSliderDialogInterval(1.0)

  ElseIf (A_option == LoopID)
    SetSliderOptionValue(a_option, MainQuest.LoopCount, "{0}", false)
    SetSliderDialogStartValue(MainQuest.LoopCount)
    SetSliderDialogDefaultValue(0.0)
    SetSliderDialogRange(0.0, 10.0)
    SetSliderDialogInterval(1.0)
  elseif(a_option == OptimizeID  )
    SetSliderOptionValue(a_option, MainQuest.Optimize, "{0}", false)
    SetSliderDialogDefaultValue(1.0)
    SetSliderDialogRange(0.0, 1.0)  
    SetSliderDialogInterval(1.0)
    SetSliderDialogStartValue(MainQuest.Optimize) 

  elseif(a_option == AGIF_APNG_Quality_ID)
    SetSliderOptionValue(a_option, MainQuest.Quality*100.0, "{0}", false)
    SetSliderDialogStartValue(MainQuest.Quality*100)
    SetSliderDialogDefaultValue(85.0)
    SetSliderDialogRange(0.0, 100.0)
    SetSliderDialogInterval(5.0)
  ENDIF
endEvent

event OnOptionSliderAccept(int a_option, float a_value)
	;{Called when a new slider value has been accepted}
  If(a_option == JPG_CompressionID) 
	MainQuest.Jpg_Compression = a_value
    SetSliderOptionValue(a_option, MainQuest.Jpg_Compression,  "{0}", 0)
  elseif(a_option == DIF_Compression_ID)
    MainQuest.Compression = a_value as int
    SetSliderOptionValue(a_option, MainQuest.Compression, "{0}", false)
  Elseif(a_Option ==  DurationID)
    MainQuest.Duration = a_value
    SetSliderOptionValue(a_option, MainQuest.Duration, "{0}", false) 
    mainQuest.FPS=SetFPS(a_value)
  Elseif(a_option == LoopID)
    MainQuest.LoopCount = a_value as int
    SetSliderOptionValue(a_option, MainQuest.LoopCount, "{0}", false) 
  elseif(a_option == OptimizeID)
    MainQuest.Optimize = a_value as int
    SetSliderOptionValue(a_option, MainQuest.Optimize, "{0}", false)
  Elseif(a_option == AGIF_APNG_Quality_ID)
  ;Mainqest.Quality is a float 0.0 to 1.0 so we need to convert
  ;the slider value which is 0 to 100
    MainQuest.Quality = a_value as float /100.0 
    SetSliderOptionValue(a_option, MainQuest.Quality*100.0, "{0}", false ) 
  Endif
endEvent

 
event OnConfigClose()
  ;bConfigOpen is a flag variable which prevents activation of
  ;PrintScreenKey while the MCM is open
  MainQuest.bConfigOpen = false 
  RegisterforKey(MainQuest.Key_TakePhoto )
 Debug.Notification("Config Closed bConfigOpen = " + MainQuest.bConfigOpen)  
 Mainquest.Writejson()
EndEvent
