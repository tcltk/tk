/*
 * tkWaylandKey.c --
 *
 *
*  This file contains routines for dealing with international keyboard
 * input. Ported to Wayland.
 *
 * Copyright © 1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkUnixInt.h"
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <stdlib.h>
#include <string.h>

/*
 * Wayland-specific structures
 */
typedef struct {
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    int mode_mod_mask;
    int meta_mod_mask;
    int alt_mod_mask;
    int lock_usage;  /* LU_IGNORE, LU_CAPS, LU_SHIFT */
    KeyCode *mod_key_codes;
    Tcl_Size num_mod_key_codes;
    int bind_info_stale;
} WaylandKeymapInfo;

/*
 * Prototypes for local functions defined in this file:
 */

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetCaretPos --
 *
 *
This enables correct placement of the text input caret.
 *
This is called by widgets to indicate their cursor placement.
 *
 *----------------------------------------------------------------------
 */

void
Tk_SetCaretPos(
    Tk_Window tkwin,
    int x,
    int y,
    int height)
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    TkDisplay *dispPtr = winPtr->dispPtr;
    
    /* Check if position has changed */
    if ((dispPtr->caret.winPtr == winPtr)
    && (dispPtr->caret.x == x)
    && (dispPtr->caret.y == y)
    && (dispPtr->caret.height == height)) {
return;
    }

    dispPtr->caret.winPtr = winPtr;
    dispPtr->caret.x = x;
    dispPtr->caret.y = y;
    dispPtr->caret.height = height;

    /*
     * Update Wayland text input caret position
     */
#ifdef TK_USE_INPUT_METHODS
    if (dispPtr->waylandData && winPtr->waylandSurface) {
        /* In Wayland, we'd update the text input context */
        /* This is handled at the compositor level */
    }
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetString --
 *
 *
Retrieve the UTF string associated with a keyboard event.
 *
 * Results:
 *
Returns the UTF string.
 *
 * Side effects:
 *
Stores the input string in the specified Tcl_DString.
 *
 *----------------------------------------------------------------------
 */

const char *
TkpGetString(
    TkWindow *winPtr,
/* Window where event occurred */
    XEvent *eventPtr,
/* X keyboard event. */
    Tcl_DString *dsPtr)
/* Initialized, empty string to hold result. */
{
    Tcl_Size len;
    TkKeyEvent *kePtr = (TkKeyEvent *) eventPtr;
    TkDisplay *dispPtr = winPtr->dispPtr;

    /*
     * If we have the value cached already, use it now.
     */
    if (kePtr->charValuePtr != NULL) {
Tcl_DStringSetLength(dsPtr, kePtr->charValueLen);
memcpy(Tcl_DStringValue(dsPtr), kePtr->charValuePtr,
kePtr->charValueLen+1);
return Tcl_DStringValue(dsPtr);
    }

    /*
     * Only do this for KeyPress events
     */
    if (eventPtr->type != KeyPress) {
len = 0;
Tcl_DStringSetLength(dsPtr, len);
goto done;
    }

#ifdef TK_USE_INPUT_METHODS
    if (dispPtr->flags & TK_DISPLAY_USE_IM) {
        /*
         * For Wayland with input methods, text comes from compositor
         * via text-input protocol. We need to check if there's pending text.
         */
        if (winPtr->pendingText) {
            len = strlen(winPtr->pendingText);
            Tcl_DStringAppend(dsPtr, winPtr->pendingText, len);
            free(winPtr->pendingText);
            winPtr->pendingText = NULL;
        } else {
            len = 0;
        }
    } else
#endif /* TK_USE_INPUT_METHODS */
    {
        /*
         * Fallback: convert keysym to string using xkbcommon
         */
        KeySym keysym = TkpGetKeySym(dispPtr, eventPtr);
        char buffer[32];
        
        if (keysym != NoSymbol) {
            /* Map keysym to UTF-8 string */
            if (keysym >= XK_space && keysym <= XK_asciitilde) {
                /* ASCII printable characters */
                buffer[0] = (char)(keysym & 0xFF);
                buffer[1] = '\0';
                len = 1;
            } else if (keysym >= XK_A && keysym <= XK_Z) {
                /* Uppercase letters */
                buffer[0] = 'A' + (keysym - XK_A);
                buffer[1] = '\0';
                len = 1;
            } else if (keysym >= XK_a && keysym <= XK_z) {
                /* Lowercase letters */
                buffer[0] = 'a' + (keysym - XK_a);
                buffer[1] = '\0';
                len = 1;
            } else {
                /* For other keysyms, use xkb_keysym_to_utf8 */
                len = xkb_keysym_to_utf8(keysym, buffer, sizeof(buffer));
            }
            
            if (len > 0) {
                Tcl_DStringAppend(dsPtr, buffer, len);
            } else {
                len = 0;
            }
        } else {
            len = 0;
        }
    }

    /*
     * Cache the string in the event
     */
done:
    if (len > 0) {
        kePtr->charValuePtr = (char *)Tcl_Alloc(len + 1);
        kePtr->charValueLen = len;
        memcpy(kePtr->charValuePtr, Tcl_DStringValue(dsPtr), len);
        kePtr->charValuePtr[len] = '\0';
    } else {
        kePtr->charValuePtr = (char *)Tcl_Alloc(1);
        kePtr->charValueLen = 0;
        kePtr->charValuePtr[0] = '\0';
    }
    return Tcl_DStringValue(dsPtr);
}

/*
 * When mapping from a keysym to key information for Wayland
 */

void
TkpSetKeycodeAndState(
    Tk_Window tkwin,
    KeySym keySym,
    XEvent *eventPtr)
{
    TkDisplay *dispPtr = ((TkWindow *) tkwin)->dispPtr;
    
    if (keySym == NoSymbol) {
        eventPtr->xkey.keycode = 0;
        return;
    }

    /*
     * In Wayland, we can't arbitrarily set keycodes.
     * We store the keysym and let the compositor handle keycodes.
     */
    eventPtr->xkey.keysym = keySym;
    
    /*
     * For modifier state, we need to track what modifiers are active
     * based on xkbcommon state
     */
    if (dispPtr->waylandKeymapInfo && dispPtr->waylandKeymapInfo->xkb_state) {
        struct xkb_state *xkb_state = dispPtr->waylandKeymapInfo->xkb_state;
        
        /* Update modifier state based on xkbcommon */
        eventPtr->xkey.state = 0;
        
        if (xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_SHIFT,
                XKB_STATE_MODS_EFFECTIVE)) {
            eventPtr->xkey.state |= ShiftMask;
        }
        if (xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_CAPS,
                XKB_STATE_MODS_EFFECTIVE)) {
            eventPtr->xkey.state |= LockMask;
        }
        if (xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_CTRL,
                XKB_STATE_MODS_EFFECTIVE)) {
            eventPtr->xkey.state |= ControlMask;
        }
        if (xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_ALT,
                XKB_STATE_MODS_EFFECTIVE)) {
            eventPtr->xkey.state |= Mod1Mask;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetKeySym --
 *
 *
Given an X KeyPress or KeyRelease event, map the keycode in the event
 *
into a KeySym.
 *
 * Results:
 *
The return value is the KeySym corresponding to eventPtr, or NoSymbol
 *
if no matching Keysym could be found.
 *
 * Side effects:
 *
In the first call for a given display, keymap info gets loaded.
 *
 *----------------------------------------------------------------------
 */

KeySym
TkpGetKeySym(
    TkDisplay *dispPtr,
/* Display in which to map keycode. */
    XEvent *eventPtr)
/* Description of X event. */
{
    TkKeyEvent* kePtr = (TkKeyEvent*) eventPtr;
    
    /*
     * If we already have a cached keysym (from input method processing),
     * return it.
     */
    if (eventPtr->type == KeyPress && kePtr->keysym != NoSymbol) {
        return kePtr->keysym;
    }

    /*
     * Refresh the mapping information if it's stale.
     */
    if (dispPtr->waylandKeymapInfo && dispPtr->waylandKeymapInfo->bind_info_stale) {
        TkpInitKeymapInfo(dispPtr);
    }

    /*
     * Convert keycode to keysym using xkbcommon
     */
    if (dispPtr->waylandKeymapInfo && dispPtr->waylandKeymapInfo->xkb_state) {
        struct xkb_state *xkb_state = dispPtr->waylandKeymapInfo->xkb_state;
        xkb_keycode_t keycode = eventPtr->xkey.keycode;
        
        /* Map keycode to keysym using xkbcommon */
        xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state, keycode);
        
        /*
         * Special handling for Lock key (Caps Lock)
         */
        if (dispPtr->waylandKeymapInfo->lock_usage == LU_CAPS) {
            /* If Caps Lock is active and sym is alphabetic, adjust case */
            if (xkb_state_mod_name_is_active(xkb_state, XKB_MOD_NAME_CAPS,
                    XKB_STATE_MODS_EFFECTIVE)) {
                if (sym >= XK_a && sym <= XK_z) {
                    sym = XK_A + (sym - XK_a);
                } else if (sym >= XK_A && sym <= XK_Z) {
                    sym = XK_a + (sym - XK_A);
                }
            }
        }
        
        return (KeySym)sym;
    }

    return NoSymbol;
}

/*
 *--------------------------------------------------------------
 *
 * TkpInitKeymapInfo --
 *
 *
This function is invoked to scan keymap information to recompute stuff
 *
that's important for binding, such as modifier keys.
 *
 * Results:
 *
None.
 *
 * Side effects:
 *
Keymap-related information in dispPtr is updated.
 *
 *--------------------------------------------------------------
 */

void
TkpInitKeymapInfo(
    TkDisplay *dispPtr)
/* Display for which to recompute keymap
 * information. */
{
    if (!dispPtr->waylandKeymapInfo) {
        dispPtr->waylandKeymapInfo = (WaylandKeymapInfo *)
            Tcl_Alloc(sizeof(WaylandKeymapInfo));
        memset(dispPtr->waylandKeymapInfo, 0, sizeof(WaylandKeymapInfo));
    }
    
    WaylandKeymapInfo *info = dispPtr->waylandKeymapInfo;
    
    /* Initialize xkbcommon context if needed */
    if (!info->xkb_context) {
        info->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!info->xkb_context) {
            dispPtr->bindInfoStale = 0;
            return;
        }
    }
    
    /*
     * In Wayland, we get keymap from compositor via wl_keyboard.keymap event.
     * This is typically set up elsewhere in the Wayland initialization.
     * For now, we'll create a default keymap.
     */
    if (!info->xkb_keymap && info->xkb_context) {
        struct xkb_rule_names names = {
            .rules = NULL,
            .model = "pc105",
            .layout = "us",
            .variant = "",
            .options = ""
        };
        
        info->xkb_keymap = xkb_keymap_new_from_names(info->xkb_context, &names,
                                                     XKB_KEYMAP_COMPILE_NO_FLAGS);
        
        if (info->xkb_keymap) {
            info->xkb_state = xkb_state_new(info->xkb_keymap);
        }
    }
    
    if (!info->xkb_state) {
        dispPtr->bindInfoStale = 0;
        return;
    }
    
    /*
     * Determine lock usage from xkbcommon
     */
    info->lock_usage = LU_IGNORE;
    
    /* Check for Caps Lock */
    xkb_mod_index_t caps_index = xkb_keymap_mod_get_index(info->xkb_keymap, 
                                                         XKB_MOD_NAME_CAPS);
    if (caps_index != XKB_MOD_INVALID) {
        info->lock_usage = LU_CAPS;
    }
    
    /* Check for Shift Lock (less common) */
    /* Note: xkbcommon doesn't have a direct "Shift Lock" concept */
    
    /*
     * Look for modifier masks
     */
    info->mode_mod_mask = 0;
    info->meta_mod_mask = 0;
    info->alt_mod_mask = 0;
    
    /* Map xkbcommon modifiers to X11-style masks */
    xkb_mod_index_t mod_index;
    
    mod_index = xkb_keymap_mod_get_index(info->xkb_keymap, "Mod5");
    if (mod_index != XKB_MOD_INVALID) {
        info->mode_mod_mask = (1 << mod_index);
    }
    
    mod_index = xkb_keymap_mod_get_index(info->xkb_keymap, "Mod1");
    if (mod_index != XKB_MOD_INVALID) {
        info->alt_mod_mask = (1 << mod_index);
    }
    
    mod_index = xkb_keymap_mod_get_index(info->xkb_keymap, "Mod2");
    if (mod_index != XKB_MOD_INVALID) {
        info->meta_mod_mask = (1 << mod_index);
    }
    
    /*
     * Build array of modifier keycodes
     */
    if (info->mod_key_codes != NULL) {
        Tcl_Free(info->mod_key_codes);
    }
    
    /* Count modifier keys */
    info->num_mod_key_codes = 0;
    xkb_keycode_t min_keycode = xkb_keymap_min_keycode(info->xkb_keymap);
    xkb_keycode_t max_keycode = xkb_keymap_max_keycode(info->xkb_keymap);
    
    /* Allocate array (simplified - in reality we'd need to scan keymap) */
    info->mod_key_codes = (KeyCode *)Tcl_Alloc(32 * sizeof(KeyCode));
    
    /* Look for keys that are bound to modifiers */
    for (xkb_keycode_t keycode = min_keycode; 
         keycode <= max_keycode && info->num_mod_key_codes < 32; 
         keycode++) {
        
        /* Check if this key is bound to any modifier */
        for (xkb_mod_index_t mod = 0; mod < xkb_keymap_num_mods(info->xkb_keymap); mod++) {
            if (xkb_keymap_key_get_mods_for_level(info->xkb_keymap, keycode, 
                                                  mod, 0, NULL) > 0) {
                info->mod_key_codes[info->num_mod_key_codes++] = keycode;
                break;
            }
        }
    }
    
    dispPtr->bindInfoStale = 0;
    info->bind_info_stale = 0;
    
    /* Update TkDisplay fields for compatibility */
    dispPtr->modeModMask = info->mode_mod_mask;
    dispPtr->metaModMask = info->meta_mod_mask;
    dispPtr->altModMask = info->alt_mod_mask;
    dispPtr->lockUsage = info->lock_usage;
    dispPtr->modKeyCodes = info->mod_key_codes;
    dispPtr->numModKeyCodes = info->num_mod_key_codes;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetKeymapInfo --
 *
 *
Helper function to get Wayland keymap information
 *
 *----------------------------------------------------------------------
 */

void
Tk_GetKeymapInfo(
    TkDisplay *dispPtr,
    struct xkb_keymap **keymap,
    struct xkb_state **state)
{
    if (dispPtr->waylandKeymapInfo) {
        if (keymap) *keymap = dispPtr->waylandKeymapInfo->xkb_keymap;
        if (state) *state = dispPtr->waylandKeymapInfo->xkb_state;
    } else {
        if (keymap) *keymap = NULL;
        if (state) *state = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetKeymapInfo --
 *
 *
Set Wayland keymap information (called from Wayland event handler)
 *
 *----------------------------------------------------------------------
 */

void
Tk_SetKeymapInfo(
    TkDisplay *dispPtr,
    struct xkb_keymap *keymap)
{
    if (!dispPtr->waylandKeymapInfo) {
        dispPtr->waylandKeymapInfo = (WaylandKeymapInfo *)
            Tcl_Alloc(sizeof(WaylandKeymapInfo));
        memset(dispPtr->waylandKeymapInfo, 0, sizeof(WaylandKeymapInfo));
    }
    
    WaylandKeymapInfo *info = dispPtr->waylandKeymapInfo;
    
    /* Clean up old state */
    if (info->xkb_state) {
        xkb_state_unref(info->xkb_state);
    }
    if (info->xkb_keymap && info->xkb_keymap != keymap) {
        xkb_keymap_unref(info->xkb_keymap);
    }
    
    /* Set new keymap */
    info->xkb_keymap = keymap;
    if (keymap) {
        xkb_keymap_ref(keymap);
        info->xkb_state = xkb_state_new(keymap);
    } else {
        info->xkb_state = NULL;
    }
    
    /* Mark bind info as stale so it gets reinitialized */
    dispPtr->bindInfoStale = 1;
    info->bind_info_stale = 1;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_UpdateKeyState --
 *
 *
Update keyboard state from Wayland key events
 *
 *----------------------------------------------------------------------
 */

void
Tk_UpdateKeyState(
    TkDisplay *dispPtr,
    xkb_keycode_t keycode,
    int pressed)
{
    if (dispPtr->waylandKeymapInfo && dispPtr->waylandKeymapInfo->xkb_state) {
        struct xkb_state *state = dispPtr->waylandKeymapInfo->xkb_state;
        
        if (pressed) {
            xkb_state_update_key(state, keycode, XKB_KEY_DOWN);
        } else {
            xkb_state_update_key(state, keycode, XKB_KEY_UP);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_UpdateModifierState --
 *
 *
Update modifier state from Wayland events
 *
 *----------------------------------------------------------------------
 */

void
Tk_UpdateModifierState(
    TkDisplay *dispPtr,
    uint32_t depressed,
    uint32_t latched,
    uint32_t locked,
    uint32_t group)
{
    if (dispPtr->waylandKeymapInfo && dispPtr->waylandKeymapInfo->xkb_state) {
        struct xkb_state *state = dispPtr->waylandKeymapInfo->xkb_state;
        
        xkb_state_update_mask(state, depressed, latched, locked,
                             0, 0, group);
    }
}

/*
 * Cleanup function
 */

void
TkpCleanupKeymapInfo(
    TkDisplay *dispPtr)
{
    if (dispPtr->waylandKeymapInfo) {
        WaylandKeymapInfo *info = dispPtr->waylandKeymapInfo;
        
        if (info->xkb_state) {
            xkb_state_unref(info->xkb_state);
        }
        if (info->xkb_keymap) {
            xkb_keymap_unref(info->xkb_keymap);
        }
        if (info->xkb_context) {
            xkb_context_unref(info->xkb_context);
        }
        if (info->mod_key_codes) {
            Tcl_Free(info->mod_key_codes);
        }
        
        Tcl_Free(info);
        dispPtr->waylandKeymapInfo = NULL;
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
