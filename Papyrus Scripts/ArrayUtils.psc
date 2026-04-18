Scriptname ArrayUtils Extends Quest

; =========================================================
; Array Search Helpers
; =========================================================

; --- STRING SEARCH ---
; Returns index of target String in arr, or -1 if not found
Int Function FindString(String[] arr, String target) Global
    if arr == none
        return -1
    endif
    Int i = 0
    while (i < arr.Length)
        if (arr[i] == target)
            return i
        endif
        i += 1
    endwhile
    return -1
EndFunction

; --- FORM SEARCH ---
; Works with any Form (Weapons, Actors, Quests, etc.)
Int Function FindForm(Form[] arr, Form target) Global
    if arr == none
        return -1
    endif
    Int i = 0
    while (i < arr.Length)
        if (arr[i] == target)
            return i
        endif
        i += 1
    endwhile
    return -1
EndFunction

; --- GENERIC BOOL CHECK ---
; Returns TRUE if item exists, FALSE otherwise
Bool Function ContainsForm(Form[] arr, Form target) Global
    return (FindForm(arr, target) != -1)
EndFunction

Bool Function ContainsString(String[] arr, String target) Global
    return (FindString(arr, target) != -1)
EndFunction
