Scriptname Printscreen_PlayerRef_Script extends ReferenceAlias

Printscreen_MainQuest_script Property MainQuest Auto

Event OnInit()
    Debug.Trace("PrintScreen: player alias initialized")
    if MainQuest
        MainQuest.InitializePrintscreen()
    else
        Debug.MessageBox("PrintScreen ERROR: MainQuest property is not filled on the player alias")
    endif
EndEvent

Event OnPlayerLoadGame()
    Debug.Trace("PrintScreen: save loaded, reinitializing")
    if MainQuest
        MainQuest.InitializePrintscreen()
    else
        Debug.MessageBox("PrintScreen ERROR: MainQuest property is not filled on the player alias")
    endif
EndEvent
