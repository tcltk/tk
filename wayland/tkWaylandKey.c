/*
 * tkWaylandKey.c --
 *
 * This file contains routines for dealing with international keyboard
 * input using GLFW.
 *
 * Copyright © 1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkUnixInt.h"
#include <stdlib.h>
#include <string.h>
#include <GLFW/glfw3.h>


/*
 * GLFW-specific structures
 */
typedef struct {
    int mode_mod_mask;
    int meta_mod_mask;
    int alt_mod_mask;
    int lock_usage;  /* LU_IGNORE, LU_CAPS, LU_SHIFT */
    KeyCode *mod_key_codes;
    Tcl_Size num_mod_key_codes;
    int bind_info_stale;
} GLFWKeymapInfo;

/* Global GLFW keymap info. */
static GLFWKeymapInfo *glfwKeymapInfo = NULL;

/* Function prototypes. */
int GLFW_KeyToXKeycode(int glfw_key);
KeySym GLFW_KeyToKeysym(int glfw_key, int mods);
void GLFW_ProcessKeyEvent(TkWindow *winPtr, int glfw_key, int scancode, int action, int mods);

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetCaretPos --
 *
 * This enables correct placement of the text input caret.
 * This is called by widgets to indicate their cursor placement.
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
    
    /* Check if position has changed. */
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
     * Update GLFW text input caret position.
     */
#ifdef TK_USE_INPUT_METHODS
    /* For GLFW, caret position might be used for IME positioning */
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetString --
 *
 * Retrieve the UTF string associated with a keyboard event.
 *
 * Results:
 * Returns the UTF string.
 *
 * Side effects:
 * Stores the input string in the specified Tcl_DString.
 *
 *----------------------------------------------------------------------
 */

const char *
TkpGetString(
    TkWindow *winPtr,
    XEvent *eventPtr,
    Tcl_DString *dsPtr)
{
    Tcl_Size len;
    TkKeyEvent *kePtr = (TkKeyEvent *) eventPtr;

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
     * Only do this for KeyPress events.
     */
    if (eventPtr->type != KeyPress) {
        len = 0;
        Tcl_DStringSetLength(dsPtr, len);
        goto done;
    }

#ifdef TK_USE_INPUT_METHODS
    if (winPtr->dispPtr->flags & TK_DISPLAY_USE_IM) {
        /*
         * For GLFW with input methods, text comes from character callback.
         */
        char *pendingText = NULL; /* Would come from GLFW character callback. */
        if (pendingText) {
            len = strlen(pendingText);
            Tcl_DStringAppend(dsPtr, pendingText, len);
            free(pendingText);
        } else {
            len = 0;
        }
    } else
#endif /* TK_USE_INPUT_METHODS */
    {
        /*
         * For GLFW, we get the character from the key event
         * GLFW provides Unicode code points in its character callback.
         * For now, we'll use the keysym-to-character mapping.
         */
        KeySym keysym = TkpGetKeySym(winPtr->dispPtr, eventPtr);
        char buffer[8];  /* UTF-8 can use up to 4 bytes, plus null. */
        
        if (keysym != NoSymbol) {
            /* Simple ASCII mapping for common keys. */
            if (keysym >= XK_space && keysym <= XK_asciitilde) {
                /* ASCII printable characters. */
                buffer[0] = (char)(keysym & 0xFF);
                buffer[1] = '\0';
                len = 1;
            } else if (keysym >= XK_A && keysym <= XK_Z) {
                /* Uppercase letters - check shift state. */
                int shift_pressed = (eventPtr->xkey.state & ShiftMask) ? 1 : 0;
                if (shift_pressed) {
                    buffer[0] = 'A' + (keysym - XK_A);
                } else {
                    buffer[0] = 'a' + (keysym - XK_A);
                }
                buffer[1] = '\0';
                len = 1;
            } else if (keysym >= XK_a && keysym <= XK_z) {
                /* Lowercase letters - check shift state. */
                int shift_pressed = (eventPtr->xkey.state & ShiftMask) ? 1 : 0;
                if (shift_pressed) {
                    buffer[0] = 'A' + (keysym - XK_a);
                } else {
                    buffer[0] = 'a' + (keysym - XK_a);
                }
                buffer[1] = '\0';
                len = 1;
            } else if (keysym >= XK_0 && keysym <= XK_9) {
                /* Numbers */
                buffer[0] = '0' + (keysym - XK_0);
                buffer[1] = '\0';
                len = 1;
            } else {
                /* Special keys */
                switch (keysym) {
                    case XK_Return:
                    case XK_KP_Enter:
                        buffer[0] = '\n';
                        buffer[1] = '\0';
                        len = 1;
                        break;
                    case XK_Tab:
                        buffer[0] = '\t';
                        buffer[1] = '\0';
                        len = 1;
                        break;
                    case XK_BackSpace:
                        buffer[0] = '\b';
                        buffer[1] = '\0';
                        len = 1;
                        break;
                    default:
                        buffer[0] = '\0';
                        len = 0;
                        break;
                }
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
     * Cache the string in the event.
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
 * When mapping from a keysym to key information.
 */

void
TkpSetKeycodeAndState(
    TCL_UNUSED(Tk_Window), /* tkwin */
    KeySym keySym,
    XEvent *eventPtr)
{
    /* For GLFW, keycodes and states come from GLFW events. */
    
    if (keySym == NoSymbol) {
        eventPtr->xkey.keycode = 0;
        return;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetKeySym --
 *
 * Given an X KeyPress or KeyRelease event, map the keycode in the event
 * into a KeySym.
 *
 * Results:
 * The return value is the KeySym corresponding to eventPtr, or NoSymbol
 * if no matching Keysym could be found.
 *
 * Side effects:
 * In the first call for a given display, keymap info gets loaded.
 *
 *----------------------------------------------------------------------
 */

KeySym
TkpGetKeySym(
    TkDisplay *dispPtr,
    XEvent *eventPtr)
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
    if (dispPtr->bindInfoStale) {
        TkpInitKeymapInfo(dispPtr);
    }

    /*
     * For GLFW, we need to map GLFW key codes to X11 keysyms.
     * This mapping would typically be done when GLFW events are converted to X events.
     */
    
    /* If the event already has a keysym cached, use it. */
    if (kePtr->keysym != NoSymbol) {
        return kePtr->keysym;
    }
    
    /* Default: return the keycode as keysym (simplified). */
    return (KeySym)eventPtr->xkey.keycode;
}

/*
 *--------------------------------------------------------------
 *
 * TkpInitKeymapInfo --
 *
 * This function is invoked to scan keymap information to recompute stuff
 * that's important for binding, such as modifier keys.
 *
 * Results:
 * None.
 *
 * Side effects:
 * Keymap-related information in dispPtr is updated.
 *
 *--------------------------------------------------------------
 */

void
TkpInitKeymapInfo(
    TkDisplay *dispPtr)
{
    if (!glfwKeymapInfo) {
        glfwKeymapInfo = (GLFWKeymapInfo *)Tcl_Alloc(sizeof(GLFWKeymapInfo));
        memset(glfwKeymapInfo, 0, sizeof(GLFWKeymapInfo));
    }
    
    GLFWKeymapInfo *info = glfwKeymapInfo;
    
    /*
     * For GLFW, we set up default modifier masks.
     */
    info->mode_mod_mask = Mod5Mask;
    info->alt_mod_mask = Mod1Mask;
    info->meta_mod_mask = Mod2Mask;
    info->lock_usage = LU_CAPS;
    
    /*
     * Build array of modifier keycodes.
     */
    if (info->mod_key_codes != NULL) {
        Tcl_Free(info->mod_key_codes);
    }
    
    /* Allocate array for modifier keycodes. */
    info->mod_key_codes = (KeyCode *)Tcl_Alloc(8 * sizeof(KeyCode));
    info->num_mod_key_codes = 0;
    
    /*
     * Add typical modifier keycodes.
     */
    if (info->num_mod_key_codes < 8) info->mod_key_codes[info->num_mod_key_codes++] = 66;  /* Caps Lock */
    if (info->num_mod_key_codes < 8) info->mod_key_codes[info->num_mod_key_codes++] = 50;  /* Left Shift */
    if (info->num_mod_key_codes < 8) info->mod_key_codes[info->num_mod_key_codes++] = 62;  /* Right Shift */
    if (info->num_mod_key_codes < 8) info->mod_key_codes[info->num_mod_key_codes++] = 37;  /* Left Control */
    if (info->num_mod_key_codes < 8) info->mod_key_codes[info->num_mod_key_codes++] = 105; /* Right Control */
    if (info->num_mod_key_codes < 8) info->mod_key_codes[info->num_mod_key_codes++] = 64;  /* Alt */
    if (info->num_mod_key_codes < 8) info->mod_key_codes[info->num_mod_key_codes++] = 108; /* Alt Gr */
    
    dispPtr->bindInfoStale = 0;
    info->bind_info_stale = 0;
    
    /* Update TkDisplay fields for compatibility. */
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
 * GLFW_ProcessKeyEvent --
 *
 * Helper function to process GLFW key events and convert to Tk events
 *
 *----------------------------------------------------------------------
 */

void
GLFW_ProcessKeyEvent(
    TkWindow *winPtr,
    int glfw_key,
    int scancode,
    int action,
    int mods)
{
    /* Convert GLFW key event to Tk/X event. */
    XEvent event;
    memset(&event, 0, sizeof(XEvent));
    
    /* Set event type */
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        event.type = KeyPress;
    } else if (action == GLFW_RELEASE) {
        event.type = KeyRelease;
    }
    
    /* Convert GLFW key to X keycode. */
    event.xkey.keycode = GLFW_KeyToXKeycode(glfw_key);
    
    /* Convert GLFW modifiers to X modifier state. */
    event.xkey.state = 0;
    if (mods & GLFW_MOD_SHIFT) event.xkey.state |= ShiftMask;
    if (mods & GLFW_MOD_CONTROL) event.xkey.state |= ControlMask;
    if (mods & GLFW_MOD_ALT) event.xkey.state |= Mod1Mask;
    if (mods & GLFW_MOD_SUPER) event.xkey.state |= Mod4Mask;
    if (mods & GLFW_MOD_CAPS_LOCK) event.xkey.state |= LockMask;
    if (mods & GLFW_MOD_NUM_LOCK) event.xkey.state |= Mod2Mask;
    
    /* Convert GLFW key to X keysym.*/
    KeySym keysym = GLFW_KeyToKeysym(glfw_key, mods);
    
    /* Store keysym in the event for TkpGetKeySym. */
    ((TkKeyEvent *)&event)->keysym = keysym;
    
    /* Process the event in Tk.*/
    Tk_QueueWindowEvent(&event, Tk_WindowId((Tk_Window)winPtr));
}

/*
 * Helper function to map GLFW keys to X keycodes
 */
int
GLFW_KeyToXKeycode(int glfw_key)
{
    /* Simple mapping.  */
    switch (glfw_key) {
        case GLFW_KEY_SPACE: return 65;
        case GLFW_KEY_APOSTROPHE: return 48;
        case GLFW_KEY_COMMA: return 59;
        case GLFW_KEY_MINUS: return 20;
        case GLFW_KEY_PERIOD: return 60;
        case GLFW_KEY_SLASH: return 61;
        case GLFW_KEY_0: return 19;
        case GLFW_KEY_1: return 10;
        case GLFW_KEY_2: return 11;
        case GLFW_KEY_3: return 12;
        case GLFW_KEY_4: return 13;
        case GLFW_KEY_5: return 14;
        case GLFW_KEY_6: return 15;
        case GLFW_KEY_7: return 16;
        case GLFW_KEY_8: return 17;
        case GLFW_KEY_9: return 18;
        case GLFW_KEY_SEMICOLON: return 47;
        case GLFW_KEY_EQUAL: return 21;
        case GLFW_KEY_A: return 38;
        case GLFW_KEY_B: return 56;
        case GLFW_KEY_C: return 54;
        case GLFW_KEY_D: return 40;
        case GLFW_KEY_E: return 26;
        case GLFW_KEY_F: return 41;
        case GLFW_KEY_G: return 42;
        case GLFW_KEY_H: return 43;
        case GLFW_KEY_I: return 31;
        case GLFW_KEY_J: return 44;
        case GLFW_KEY_K: return 45;
        case GLFW_KEY_L: return 46;
        case GLFW_KEY_M: return 58;
        case GLFW_KEY_N: return 57;
        case GLFW_KEY_O: return 32;
        case GLFW_KEY_P: return 33;
        case GLFW_KEY_Q: return 24;
        case GLFW_KEY_R: return 27;
        case GLFW_KEY_S: return 39;
        case GLFW_KEY_T: return 28;
        case GLFW_KEY_U: return 30;
        case GLFW_KEY_V: return 55;
        case GLFW_KEY_W: return 25;
        case GLFW_KEY_X: return 53;
        case GLFW_KEY_Y: return 29;
        case GLFW_KEY_Z: return 52;
        case GLFW_KEY_ESCAPE: return 9;
        case GLFW_KEY_ENTER: return 36;
        case GLFW_KEY_TAB: return 23;
        case GLFW_KEY_BACKSPACE: return 22;
        case GLFW_KEY_INSERT: return 118;
        case GLFW_KEY_DELETE: return 119;
        case GLFW_KEY_RIGHT: return 114;
        case GLFW_KEY_LEFT: return 113;
        case GLFW_KEY_DOWN: return 116;
        case GLFW_KEY_UP: return 111;
        case GLFW_KEY_PAGE_UP: return 112;
        case GLFW_KEY_PAGE_DOWN: return 117;
        case GLFW_KEY_HOME: return 110;
        case GLFW_KEY_END: return 115;
        case GLFW_KEY_CAPS_LOCK: return 66;
        case GLFW_KEY_SCROLL_LOCK: return 78;
        case GLFW_KEY_NUM_LOCK: return 77;
        case GLFW_KEY_PRINT_SCREEN: return 107;
        case GLFW_KEY_PAUSE: return 127;
        case GLFW_KEY_F1: return 67;
        case GLFW_KEY_F2: return 68;
        case GLFW_KEY_F3: return 69;
        case GLFW_KEY_F4: return 70;
        case GLFW_KEY_F5: return 71;
        case GLFW_KEY_F6: return 72;
        case GLFW_KEY_F7: return 73;
        case GLFW_KEY_F8: return 74;
        case GLFW_KEY_F9: return 75;
        case GLFW_KEY_F10: return 76;
        case GLFW_KEY_F11: return 95;
        case GLFW_KEY_F12: return 96;
        case GLFW_KEY_LEFT_SHIFT: return 50;
        case GLFW_KEY_LEFT_CONTROL: return 37;
        case GLFW_KEY_LEFT_ALT: return 64;
        case GLFW_KEY_LEFT_SUPER: return 133;
        case GLFW_KEY_RIGHT_SHIFT: return 62;
        case GLFW_KEY_RIGHT_CONTROL: return 105;
        case GLFW_KEY_RIGHT_ALT: return 108;
        case GLFW_KEY_RIGHT_SUPER: return 134;
        case GLFW_KEY_MENU: return 135;
        default: return 0;
    }
}

/*
 * Helper function to map GLFW keys to X keysyms.
 */
KeySym
GLFW_KeyToKeysym(int glfw_key, int mods)
{
    /* Simple mapping. */
    switch (glfw_key) {
        case GLFW_KEY_SPACE: return XK_space;
        case GLFW_KEY_APOSTROPHE: return (mods & GLFW_MOD_SHIFT) ? XK_quotedbl : XK_apostrophe;
        case GLFW_KEY_COMMA: return (mods & GLFW_MOD_SHIFT) ? XK_less : XK_comma;
        case GLFW_KEY_MINUS: return (mods & GLFW_MOD_SHIFT) ? XK_underscore : XK_minus;
        case GLFW_KEY_PERIOD: return (mods & GLFW_MOD_SHIFT) ? XK_greater : XK_period;
        case GLFW_KEY_SLASH: return (mods & GLFW_MOD_SHIFT) ? XK_question : XK_slash;
        case GLFW_KEY_0: return (mods & GLFW_MOD_SHIFT) ? XK_parenright : XK_0;
        case GLFW_KEY_1: return (mods & GLFW_MOD_SHIFT) ? XK_exclam : XK_1;
        case GLFW_KEY_2: return (mods & GLFW_MOD_SHIFT) ? XK_at : XK_2;
        case GLFW_KEY_3: return (mods & GLFW_MOD_SHIFT) ? XK_numbersign : XK_3;
        case GLFW_KEY_4: return (mods & GLFW_MOD_SHIFT) ? XK_dollar : XK_4;
        case GLFW_KEY_5: return (mods & GLFW_MOD_SHIFT) ? XK_percent : XK_5;
        case GLFW_KEY_6: return (mods & GLFW_MOD_SHIFT) ? XK_asciicircum : XK_6;
        case GLFW_KEY_7: return (mods & GLFW_MOD_SHIFT) ? XK_ampersand : XK_7;
        case GLFW_KEY_8: return (mods & GLFW_MOD_SHIFT) ? XK_asterisk : XK_8;
        case GLFW_KEY_9: return (mods & GLFW_MOD_SHIFT) ? XK_parenleft : XK_9;
        case GLFW_KEY_SEMICOLON: return (mods & GLFW_MOD_SHIFT) ? XK_colon : XK_semicolon;
        case GLFW_KEY_EQUAL: return (mods & GLFW_MOD_SHIFT) ? XK_plus : XK_equal;
        case GLFW_KEY_A: return (mods & GLFW_MOD_SHIFT) ? XK_A : XK_a;
        case GLFW_KEY_B: return (mods & GLFW_MOD_SHIFT) ? XK_B : XK_b;
        case GLFW_KEY_C: return (mods & GLFW_MOD_SHIFT) ? XK_C : XK_c;
        case GLFW_KEY_D: return (mods & GLFW_MOD_SHIFT) ? XK_D : XK_d;
        case GLFW_KEY_E: return (mods & GLFW_MOD_SHIFT) ? XK_E : XK_e;
        case GLFW_KEY_F: return (mods & GLFW_MOD_SHIFT) ? XK_F : XK_f;
        case GLFW_KEY_G: return (mods & GLFW_MOD_SHIFT) ? XK_G : XK_g;
        case GLFW_KEY_H: return (mods & GLFW_MOD_SHIFT) ? XK_H : XK_h;
        case GLFW_KEY_I: return (mods & GLFW_MOD_SHIFT) ? XK_I : XK_i;
        case GLFW_KEY_J: return (mods & GLFW_MOD_SHIFT) ? XK_J : XK_j;
        case GLFW_KEY_K: return (mods & GLFW_MOD_SHIFT) ? XK_K : XK_k;
        case GLFW_KEY_L: return (mods & GLFW_MOD_SHIFT) ? XK_L : XK_l;
        case GLFW_KEY_M: return (mods & GLFW_MOD_SHIFT) ? XK_M : XK_m;
        case GLFW_KEY_N: return (mods & GLFW_MOD_SHIFT) ? XK_N : XK_n;
        case GLFW_KEY_O: return (mods & GLFW_MOD_SHIFT) ? XK_O : XK_o;
        case GLFW_KEY_P: return (mods & GLFW_MOD_SHIFT) ? XK_P : XK_p;
        case GLFW_KEY_Q: return (mods & GLFW_MOD_SHIFT) ? XK_Q : XK_q;
        case GLFW_KEY_R: return (mods & GLFW_MOD_SHIFT) ? XK_R : XK_r;
        case GLFW_KEY_S: return (mods & GLFW_MOD_SHIFT) ? XK_S : XK_s;
        case GLFW_KEY_T: return (mods & GLFW_MOD_SHIFT) ? XK_T : XK_t;
        case GLFW_KEY_U: return (mods & GLFW_MOD_SHIFT) ? XK_U : XK_u;
        case GLFW_KEY_V: return (mods & GLFW_MOD_SHIFT) ? XK_V : XK_v;
        case GLFW_KEY_W: return (mods & GLFW_MOD_SHIFT) ? XK_W : XK_w;
        case GLFW_KEY_X: return (mods & GLFW_MOD_SHIFT) ? XK_X : XK_x;
        case GLFW_KEY_Y: return (mods & GLFW_MOD_SHIFT) ? XK_Y : XK_y;
        case GLFW_KEY_Z: return (mods & GLFW_MOD_SHIFT) ? XK_Z : XK_z;
        case GLFW_KEY_ESCAPE: return XK_Escape;
        case GLFW_KEY_ENTER: return XK_Return;
        case GLFW_KEY_TAB: return XK_Tab;
        case GLFW_KEY_BACKSPACE: return XK_BackSpace;
        case GLFW_KEY_INSERT: return XK_Insert;
        case GLFW_KEY_DELETE: return XK_Delete;
        case GLFW_KEY_RIGHT: return XK_Right;
        case GLFW_KEY_LEFT: return XK_Left;
        case GLFW_KEY_DOWN: return XK_Down;
        case GLFW_KEY_UP: return XK_Up;
        case GLFW_KEY_PAGE_UP: return XK_Page_Up;
        case GLFW_KEY_PAGE_DOWN: return XK_Page_Down;
        case GLFW_KEY_HOME: return XK_Home;
        case GLFW_KEY_END: return XK_End;
        case GLFW_KEY_CAPS_LOCK: return XK_Caps_Lock;
        case GLFW_KEY_SCROLL_LOCK: return XK_Scroll_Lock;
        case GLFW_KEY_NUM_LOCK: return XK_Num_Lock;
        case GLFW_KEY_PRINT_SCREEN: return XK_Print;
        case GLFW_KEY_PAUSE: return XK_Pause;
        case GLFW_KEY_F1: return XK_F1;
        case GLFW_KEY_F2: return XK_F2;
        case GLFW_KEY_F3: return XK_F3;
        case GLFW_KEY_F4: return XK_F4;
        case GLFW_KEY_F5: return XK_F5;
        case GLFW_KEY_F6: return XK_F6;
        case GLFW_KEY_F7: return XK_F7;
        case GLFW_KEY_F8: return XK_F8;
        case GLFW_KEY_F9: return XK_F9;
        case GLFW_KEY_F10: return XK_F10;
        case GLFW_KEY_F11: return XK_F11;
        case GLFW_KEY_F12: return XK_F12;
        default: return NoSymbol;
    }
}

/*
 * Cleanup function.
 */

void
TkpCleanupKeymapInfo(
    TkDisplay *dispPtr)
{
    if (glfwKeymapInfo) {
        GLFWKeymapInfo *info = glfwKeymapInfo;
        
        if (info->mod_key_codes) {
            Tcl_Free(info->mod_key_codes);
        }
        
        Tcl_Free(info);
        glfwKeymapInfo = NULL;
        
        /* Clear TkDisplay fields */
        dispPtr->modKeyCodes = NULL;
        dispPtr->numModKeyCodes = 0;
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
