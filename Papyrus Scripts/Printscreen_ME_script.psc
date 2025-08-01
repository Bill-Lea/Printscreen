Scriptname Printscreen_ME_script extends activemagiceffect  
Printscreen_MainQuest_script Property MainQuest auto 
Printscreen_MAP_script Property MAP Auto 
 

event OnEffectStart(actor target, actor castor )   
    If(!MainQuest.HUD_Visable) 
        return 
    Endif 
    String Keyname = Map.GetKeyName(MainQuest.Key_TakePhoto)
	
    if(MainQuest.ImageType=="PNG"||Mainquest.ImageType=="BMP"||MainQuest.ImageType == "GIF")

Debug.MessageBox("Printscreen version " + MainQuest.Version +"\n" + \
"\nThe Image File Type is: "+ MainQuest.ImageType + \
"\n The Path is: "+ MainQuest.Path + \
"\n Automatic HUD/menu removal is: "+ MainQuest.menu + \
"\n The Photo Key is "+ KeyName  + \
"\n Is the Capture Running: " + MainQuest.IsLatentScreenshotActive +\
"\n" + MainQuest.Shots + " Sreenshots taken this session" )

        return
		
    elseif(MainQuest.ImageType =="JPG")

Debug.MessageBox("Printscreen version " + MainQuest.Version +"\n" + \
"\nThe Image File Type is: "+ MainQuest.ImageType + \
"\n The JPG Quality is: " + MainQuest.Jpg_Compression + \
"\n The Path is: "+ MainQuest.Path + \
"\n Automatic HUD/menu removal is: "+ MainQuest.menu + \
"\n The Photo Key is "+ KeyName  + \
"\n Is the Capture Running: " + MainQuest.IsLatentScreenshotActive +\
"\n" + MainQuest.shots + " Sreenshots taken this session" )
return 

    elseif(Mainquest.Imagetype == "TIF")
        Debug.MessageBox("Printscreen version " + MainQuest.Version +"\n" + \
"\nThe Image File Type is: "+ MainQuest.ImageType + \
"\n The Tif Compression Mode is: " + MainQuest.MODE + \
"\n The Path is: "+ MainQuest.Path + \
"\n Automatic HUD/menu removal is: "+ MainQuest.menu + \
"\n The Photo Key is "+ KeyName  + \
"\n Is the Capture Running: " + MainQuest.IsLatentScreenshotActive +\
"\n" + MainQuest.Shots + " Sreenshots taken this session" )

        return

    elseif(MainQuest.ImageType == "DDS")

Debug.MessageBox("Printscreen version " + MainQuest.Version +"\n" + \
"\nThe Image File Type is: "+ MainQuest.ImageType + \
"\n The DDS Compression Mode is: " + MainQuest.Mode + \
"\n The Path is: "+ MainQuest.Path + \
"\n Automatic HUD/menu removal is: "+ MainQuest.menu + \
"\n The Photo Key is "+ KeyName  + \
"\n Is the Capture Running: " + MainQuest.IsLatentScreenshotActive +\
"\n" + MainQuest.Shots + " Sreenshots taken this session" )

        return
			
    elseif(MainQuest.Imagetype == "GIF_MULTIFRAME" ) 
       Debug.MessageBox( "Printscreen version " + MainQuest.Version +"\n" + \
"\nThe Image File Type is: "+ MainQuest.ImageType + \
"\n The Multiframe GIF Duration time is: " + mainQuest.Gif_MultiFrame_Duration +\
"\n The Path is: "+ MainQuest.Path + \
"\n Automatic HUD/menu removal is: "+ MainQuest.menu + \
"\n The Photo Key is "+ KeyName  + \
"\n Is the Capture Running: " + MainQuest.IsLatentScreenshotActive +\
"\n" + MainQuest.Shots + " Sreenshots taken this session" )
        return
    else
        Debug.MessageBox("Printscreen: Invalid Image Type selected." +\
"\n Please check the settings in the MCM menu." +\
"\n The Image Type is: " + MainQuest.ImageType  )
        return 
    endif  
EndEvent 
