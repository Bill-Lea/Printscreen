ScriptName ItemCardDebug extends Quest

    Function DumpMenuInfo()
        Debug.Trace("Checking menus...")
        
        ; Check which menu is actually open
        if UI.IsMenuOpen("InventoryMenu")
            Debug.Trace("InventoryMenu is open")
        endif
        
        if UI.IsMenuOpen("ContainerMenu")
            Debug.Trace("ContainerMenu is open")
        endif
        
        if UI.IsMenuOpen("BarterMenu")
            Debug.Trace("BarterMenu is open")
        endif
        
        if UI.IsMenuOpen("ItemMenu")
            Debug.Trace("ItemMenu is open")
        endif
    EndFunction