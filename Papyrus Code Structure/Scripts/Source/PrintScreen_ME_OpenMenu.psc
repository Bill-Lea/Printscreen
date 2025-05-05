Scriptname PrintScreen_ME_OpenMenu extends activemagiceffect  
; For functions that require a menuName, potential values are
;	"InventoryMenu"
;	"Console"
;	"Dialogue Menu"
;	"HUD Menu"
;	"Main Menu"
;	"MessageBoxMenu"
;	"Cursor Menu"
;	"Fader Menu"
;	"MagicMenu"
;	"Top Menu"
;	"Overlay Menu"
;	"Overlay Interaction Menu"
;	"Loading Menu"
;	"TweenMenu"
;	"BarterMenu"
;	"GiftMenu"
;	"Debug Text Menu"
;	"MapMenu"
;	"Lockpicking Menu"
;	"Quantity Menu"
;	"StatsMenu"
;	"ContainerMenu"
;	"Sleep/Wait Menu"
;	"LevelUp Menu"
;	"Journal Menu"
;	"Book Menu"
;	"FavoritesMenu"
;	"RaceSex Menu"
;	"Crafting Menu"
;	"Training Menu"
;	"Mist Menu"
;	"Tutorial Menu"
;	"Credits Menu"
;	"TitleSequence Menu"
;	"Console Native UI Menu"
;	"Kinect Menu"
;UI.SetInt("HUD Menu", "_root._alpha", 0)
;UI.SetInt("TrueHUD", "_root._alpha", 0)
;UI.SetInt("MoreHUD","_root._alpha",0)
;UI.Setint("immersiveHud","_root._alpha", 0)
;UI.SetInt("MiniMapMenu", "_root._alpha", 0)
;UI.SetInt("Dialogue Menu", "_root._alpha", 0)  
;UI.SetINt("CursorMenu","_root._alpha",0)
;UI.Setint("CrosshairMenu", "_root._alpha",0)
;ui.Setint("ActivateMenu", "_root._alpha",0)
;ui.Setint("ContainerMenu", "_root_alpha",0)  
;UI.setInt("StatsMenu", "_root._alpha",0)
string Name ="CrossHairMenu"

event OnEffectStart(actor castor, actor target)

 Debug.Messagebox( \
 "Is trueHud open: "+ UI.isMenuOpen("TrueHUD")+ "\n "+ \
 "Is ImmersivwHUD open: "+ Ui.ismenuOpen("immersiveHud")+"\n " +\
 "is MoreHud Open: " + Ui.isMenuOpen("MoreHud")+"\n "+ \
"Is the "+ name + " open: " + Ui.ismenuOpen(Name) +"\n " + \
"\n is "+ Name + "visiable: "+ UI.GetBool(Name, "_root._visible")+"\n " + \
 "\n "+ Name +" alpha was: " +  Ui.GetInt(Name,"_root._alpha")+\
 "\n try to set alpha to 100: " + UI.SetInt(Name,"_root._alpha", 100) )


endEvent
