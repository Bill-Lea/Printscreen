Scriptname Printscreen_ME_script extends activemagiceffect  
Printscreen_MainQuest_script Property MainQuest auto 
Printscreen_MAP_script Property MAP Auto 
 

event OnEffectStart(actor target, actor castor )   

    String Keyname = Map.GetKeyName(MainQuest.Key_TakePhoto)
	
if(MainQuest.ImageType=="PNG"||Mainquest.ImageType=="BMP"||MainQuest.ImageType == "GIF")

Debug.MessageBox("Printscreen version " + MainQuest.Version +"\n" + \
"\nThe Image File Type is: "+ MainQuest.ImageType + \
"\n The Path is: "+ MainQuest.Path + \
"\n Automatic Ui removal is: "+ MainQuest.menu + \
"\n The Photo Key is "+ KeyName  + \
"\n" + MainQuest.Shots + " Sreenshots taken this session" )

        return
		
    elseif(MainQuest.ImageType =="JPG")

Debug.MessageBox("Printscreen version " + MainQuest.Version +"\n" + \
"\nThe Image File Type is: "+ MainQuest.ImageType + \
"\n The JPG Quality is: " + MainQuest.Jpg_Compression + \
"\n The Path is: "+ MainQuest.Path + \
"\n Automatic Ui removal is: "+ MainQuest.menu + \
"\n The Photo Key is "+ KeyName  + \
"\n" + MainQuest.shots + " Sreenshots taken this session" )
return 

    elseif(Mainquest.Imagetype == "TIF")
        Debug.MessageBox("Printscreen version " + MainQuest.Version +"\n" + \
"\nThe Image File Type is: "+ MainQuest.ImageType + \
"\n The Tif Compression Mode is: " + MainQuest.MODE + \
"\n The Path is: "+ MainQuest.Path + \
"\n Automatic Ui removal is: "+ MainQuest.menu + \
"\n The Photo Key is "+ KeyName  + \
"\n" + MainQuest.Shots + " Sreenshots taken this session" )

        return

    elseif(MainQuest.ImageType == "DDS")

Debug.MessageBox("Printscreen version " + MainQuest.Version +"\n" + \
"\nThe Image File Type is: "+ MainQuest.ImageType + \
"\n The DDS Compression Mode is: " + MainQuest.Mode + \
"\n The Path is: "+ MainQuest.Path + \
"\n Automatic Ui removal is: "+ MainQuest.menu + \
"\n The Photo Key is "+ KeyName  + \
"\n" + MainQuest.Shots + " Sreenshots taken this session" )

        return	
		
    elseif((MainQuest.Imagetype == "AGIF")||( MainQuest.ImageType == "APNG"))
        
    
Debug.MessageBox( "Printscreen version " + MainQuest.Version +"\n" + \
"\nThe Image File Type is: "+ MainQuest.ImageType + \
"\n The Capture Duration time is: " + mainQuest.Duration +\
"\n The Capture Frame Rate is: " + MainQuest.FPS + \
"\n The Capture Looping is: " + MainQuest.Loopcount + \
"\n Optimization is set to: " + MainQuest.Optimize + \
"\nFile Compression is set to: " + MainQuest.Compression + \
"\n The Path is: "+ MainQuest.Path + \
"\n Automatic Ui removal is: "+ MainQuest.menu + \
"\n The Photo Key is "+ KeyName  + \
"\n" + MainQuest.Shots + " Sreenshots taken this session" )
        return

    else
        Debug.MessageBox("Printscreen: Invalid Image Type selected." +\
"\n Please check the settings in the MCM menu." +\
"\n The Image Type is: " + MainQuest.ImageType  )
        return 
    endif  
EndEvent 
