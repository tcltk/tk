/* 
 * tkMacOSXKeyboard.c --
 *
 *      Routines to support keyboard events on the Macintosh.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright 2001, Apple Computer, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkMacOSXKeyboard.c,v 1.9 2003/12/15 16:37:39 cc_benny Exp $
 */

#include "tkInt.h"
#include "X11/Xlib.h"
#include "X11/keysym.h"
#include <Carbon/Carbon.h>
#include "tkMacOSXInt.h"
#include "tkMacOSXEvent.h"      /* TkMacOSXKeycodeToUnicode() FIXME: That
                                 * function should probably move here. */

/*
 * A couple of simple definitions to make code a bit more self-explaining.
 *
 * For the assignments of Mod1==alt==command and Mod2==meta==option, see also
 * tkMacOSXMouseEvent.c.
 */

#define LATIN1_MAX       255
#define MAC_KEYCODE_MAX  0x7F
#define ALT_MASK         Mod1Mask
#define OPTION_MASK      Mod2Mask


/*
 * Tables enumerating the special keys defined on Mac keyboards.  These are
 * necessary for correct keysym mappings for all keys where the keysyms are
 * not identical with their ASCII or Latin-1 code points.
 */

typedef struct {
    int keycode;                /* Macintosh keycode. */
    KeySym keysym;              /* X windows keysym. */
} KeyInfo;

/*
 * Notes on keyArray:
 *
 * 0x34, XK_Return - Powerbooks use this and some keymaps define it.
 *
 * 0x4C, XK_Return - XFree86 and Apple's X11 call this one XK_KP_Enter.
 *
 * 0x47, XK_Clear - This key is NumLock when used on PCs, but Mac
 * applications don't use it like that, nor does Apple's X11.
 *
 * All other keycodes are taken from the published ADB keyboard layouts.
 */

static KeyInfo keyArray[] = {
    {0x24,      XK_Return},
    {0x30,      XK_Tab},
    {0x33,      XK_BackSpace},
    {0x34,      XK_Return},
    {0x35,      XK_Escape},

    {0x47,      XK_Clear},
    {0x4C,      XK_Return},

    {0x72,      XK_Help},
    {0x73,      XK_Home},
    {0x74,      XK_Page_Up},
    {0x75,      XK_Delete},
    {0x77,      XK_End},
    {0x79,      XK_Page_Down},

    {0x7B,      XK_Left},
    {0x7C,      XK_Right},
    {0x7D,      XK_Down},
    {0x7E,      XK_Up},

    {0,         0}
};

static KeyInfo virtualkeyArray[] = {
    {122,       XK_F1},
    {120,       XK_F2},
    {99,        XK_F3},
    {118,       XK_F4},
    {96,        XK_F5},
    {97,        XK_F6},
    {98,        XK_F7},
    {100,       XK_F8},
    {101,       XK_F9},
    {109,       XK_F10},
    {103,       XK_F11},
    {111,       XK_F12},
    {105,       XK_F13},
    {107,       XK_F14},
    {113,       XK_F15},
    {0,         0}
};

static int initialized = 0;
static Tcl_HashTable keycodeTable;      /* keyArray hashed by keycode value. */
static Tcl_HashTable vkeyTable;         /* virtualkeyArray hashed by virtual
                                         * keycode value. */

/*
 * Prototypes for static functions used in this file.
 */

static void     InitKeyMaps (void);


/*
 *----------------------------------------------------------------------
 *
 * InitKeyMaps --
 *
 *      Creates hash tables used by some of the functions in this file.
 *
 *      FIXME: As keycodes are defined to be in the limited range 0-127, it
 *      would be easier and more efficient to use directly initialized plain
 *      arrays and drop this function.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Allocates memory & creates some hash tables.
 *
 *----------------------------------------------------------------------
 */

static void
InitKeyMaps()
{
    Tcl_HashEntry *hPtr;
    KeyInfo *kPtr;
    int dummy;
                
    Tcl_InitHashTable(&keycodeTable, TCL_ONE_WORD_KEYS);
    for (kPtr = keyArray; kPtr->keycode != 0; kPtr++) {
        hPtr = Tcl_CreateHashEntry(&keycodeTable, (char *) kPtr->keycode,
                &dummy);
        Tcl_SetHashValue(hPtr, kPtr->keysym);
    }
    Tcl_InitHashTable(&vkeyTable, TCL_ONE_WORD_KEYS);
    for (kPtr = virtualkeyArray; kPtr->keycode != 0; kPtr++) {
        hPtr = Tcl_CreateHashEntry(&vkeyTable, (char *) kPtr->keycode,
                &dummy);
        Tcl_SetHashValue(hPtr, kPtr->keysym);
    }
    initialized = 1;
}

/*
 *----------------------------------------------------------------------
 *
 * XKeycodeToKeysym --
 *
 *      Translate from a system-dependent keycode to a system-independent
 *      keysym.
 *
 * Results:
 *      Returns the translated keysym, or NoSymbol on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

KeySym 
XKeycodeToKeysym(
    Display* display,
    KeyCode keycode,
    int index)
{
    register Tcl_HashEntry *hPtr;
    int c;
    int virtualKey;
    int newKeycode;
    UniChar newChar;

    (void) display; /*unused*/

    if (!initialized) {
        InitKeyMaps();
    }

    if (keycode == 0) {

        /* 
         * This means we had a pure modifier keypress or something similar
         * which is a TO DO.
         */

        return NoSymbol;
    }
    
    virtualKey = keycode >> 16;
    c = keycode & 0xFFFF;
    if (c > MAC_KEYCODE_MAX) {
        return NoSymbol;
    }

    /*
     * When determining what keysym to produce we first check to see if the
     * key is a function key.  We then check to see if the character is
     * another non-printing key.  Finally, we return the key syms for all
     * ASCII and Latin-1 chars.
     */

    if (c == 0x10) {
        hPtr = Tcl_FindHashEntry(&vkeyTable, (char *) virtualKey);
        if (hPtr != NULL) {
            return (KeySym) Tcl_GetHashValue(hPtr);
        }
    }
    hPtr = Tcl_FindHashEntry(&keycodeTable, (char *) virtualKey);
    if (hPtr != NULL) {
        return (KeySym) Tcl_GetHashValue(hPtr);
    }

    /* 
     * Add in the Mac modifier flags for shift and option.
     */

    newKeycode = virtualKey;
    if (index & 1) {
        newKeycode |= shiftKey;
    }
    if (index & 2) {
        newKeycode |= optionKey;
    }

    newChar = 0;
    TkMacOSXKeycodeToUnicode(
        &newChar, 1, kEventRawKeyDown,
        newKeycode & 0x00FF, newKeycode & 0xFF00, NULL);

    /*
     * X11 keysyms are identical to Unicode for ASCII and Latin-1.  Give up
     * for other characters for now.
     */

    if ((newChar >= XK_space) && (newChar <= LATIN1_MAX)) {
        return newChar;
    }

    return NoSymbol; 
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetString --
 *
 *      Retrieve the string equivalent for the given keyboard event.
 *
 * Results:
 *      Returns the UTF string.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
TkpGetString(
    TkWindow *winPtr,           /* Window where event occurred: Needed to get
                                 * input context. */
    XEvent *eventPtr,           /* X keyboard event. */
    Tcl_DString *dsPtr)         /* Uninitialized or empty string to hold
                                 * result. */
{
    (void) winPtr; /*unused*/
    Tcl_DStringInit(dsPtr);
    return Tcl_DStringAppend(dsPtr, eventPtr->xkey.trans_chars, -1);
}

/*
 *----------------------------------------------------------------------
 *
 * XGetModifierMapping --
 *
 *      Fetch the current keycodes used as modifiers.
 *
 * Results:
 *      Returns a new modifier map.
 *
 * Side effects:
 *      Allocates a new modifier map data structure.
 *
 *----------------------------------------------------------------------
 */

XModifierKeymap * 
XGetModifierMapping(
    Display* display)
{ 
    XModifierKeymap * modmap;

    (void) display; /*unused*/

    /*
     * MacOSX doesn't use the key codes for the modifiers for anything, and
     * we don't generate them either.  So there is no modifier map.
     */

    modmap = (XModifierKeymap *) ckalloc(sizeof(XModifierKeymap));
    modmap->max_keypermod = 0;
    modmap->modifiermap = NULL;
    return modmap;
}

/*
 *----------------------------------------------------------------------
 *
 * XFreeModifiermap --
 *
 *      Deallocate a modifier map that was created by XGetModifierMapping.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Frees the datastructure referenced by modmap.
 *
 *----------------------------------------------------------------------
 */

void 
XFreeModifiermap(
    XModifierKeymap *modmap)
{
    if (modmap->modifiermap != NULL) {
        ckfree((char *) modmap->modifiermap);
    }
    ckfree((char *) modmap);
}

/*
 *----------------------------------------------------------------------
 *
 * XKeysymToString, XStringToKeysym --
 *
 *      These X window functions map keysyms to strings & strings to keysyms.
 *      However, Tk already does this for the most common keysyms.
 *      Therefore, these functions only need to support keysyms that will be
 *      specific to the Macintosh.  Currently, there are none.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char * 
XKeysymToString(
    KeySym keysym)
{
    return NULL; 
}

KeySym 
XStringToKeysym(
    const char* string)
{ 
    return NoSymbol;
}

/*
 *----------------------------------------------------------------------
 *
 * XKeysymToKeycode --
 *
 *      The function XKeysymToKeycode takes an X11 keysym and converts it
 *      into a Mac keycode.  It is in the stubs table for compatibility but
 *      not used anywhere in the core.
 *
 * Results:
 *      A 32 bit keycode with the the mac keycode but without modifiers in
 *      the higher 16 bits and the keysym in the lower 16 bits.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

KeyCode
XKeysymToKeycode(
    Display* display,
    KeySym keysym)
{
    KeyCode keycode = 0;
    char virtualKeyCode = 0;
    
    (void) display; /*unused*/

    if ((keysym >= XK_space) && (XK_asciitilde)) {
        if (keysym == 'a') {
            virtualKeyCode = 0x00;
        } else if (keysym == 'b' || keysym == 'B') {
            virtualKeyCode = 0x0B;
        } else if (keysym == 'c') {
            virtualKeyCode = 0x08;
        } else if (keysym == 'x' || keysym == 'X') {
            virtualKeyCode = 0x07;
        } else if (keysym == 'z') {
            virtualKeyCode = 0x06;
        } else if (keysym == ' ') {
            virtualKeyCode = 0x31;
        } else if (keysym == XK_Return) {
            virtualKeyCode = 0x24;
            keysym = '\r';
        }
        keycode = keysym + (virtualKeyCode <<16);
    }

    return keycode;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetKeycodeAndState --
 *
 *      The function TkpSetKeycodeAndState takes a keysym and fills in the
 *      appropriate members of an XEvent.  It is similar to XKeysymToKeycode,
 *      but it also sets the modifier mask in the XEvent.  It is used by
 *      [event generate] and it is in the stubs table.
 *
 * Results:
 *      Fills an XEvent, sets the member xkey.keycode with a keycode
 *      formatted the same as XKeysymToKeycode and the member xkey.state with
 *      the modifiers implied by the keysym.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
TkpSetKeycodeAndState(
    Tk_Window tkwin,
    KeySym keysym,
    XEvent *eventPtr)
{
    Display *display;
    int state;
    KeyCode keycode;
    
    display = Tk_Display(tkwin);
    
    if (keysym == NoSymbol) {
        keycode = 0;
    } else {
        keycode = XKeysymToKeycode(display, keysym);
    }
    if (keycode != 0) {
        for (state = 0; state < 4; state++) {
            if (XKeycodeToKeysym(display, keycode, state) == keysym) {
                if (state & 1) {
                    eventPtr->xkey.state |= ShiftMask;
                }
                if (state & 2) {
                    eventPtr->xkey.state |= OPTION_MASK;
                }
                break;
            }
        }
    }
    eventPtr->xkey.keycode = keycode;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetKeySym --
 *
 *      Given an X KeyPress or KeyRelease event, map the keycode in the event
 *      into a keysym.
 *
 * Results:
 *      The return value is the keysym corresponding to eventPtr, or NoSymbol
 *      if no matching keysym could be found.
 *
 * Side effects:
 *      In the first call for a given display, keycode-to-keysym maps get
 *      loaded.
 *
 *----------------------------------------------------------------------
 */

KeySym
TkpGetKeySym(
    TkDisplay *dispPtr,         /* Display in which to map keycode. */
    XEvent *eventPtr)           /* Description of X event. */
{
    KeySym sym;
    int index;

    /*
     * Refresh the mapping information if it's stale.
     */

    if (dispPtr->bindInfoStale) {
        TkpInitKeymapInfo(dispPtr);
    }

    /*
     * Handle pure modifier keys specially.  We use -1 as a signal for
     * this.
     */

    if (eventPtr->xany.send_event == -1) {
        int modifier = eventPtr->xkey.keycode;
        if (modifier == cmdKey) {
            return XK_Alt_L;
        } else if (modifier == shiftKey) {
            return XK_Shift_L;
        } else if (modifier == alphaLock) {
            return XK_Caps_Lock;
        } else if (modifier == optionKey) {
            return XK_Meta_L;
        } else if (modifier == controlKey) {
            return XK_Control_L;
        } else if (modifier == rightShiftKey) {
            return XK_Shift_R;
        } else if (modifier == rightOptionKey) {
            return XK_Meta_R;
        } else if (modifier == rightControlKey) {
            return XK_Control_R;
        } else {

            /*
             * If we get here, we probably need to implement something new.
             */

            return NoSymbol;
        } 
    }

    /*
     * Figure out which of the four slots in the keymap vector to use for
     * this key.  Refer to Xlib documentation for more info on how this
     * computation works.  (Note: We use "Option" in keymap columns 2 and 3
     * where other implementations have "Mode_switch".)
     */

    index = 0;
    if (eventPtr->xkey.state & OPTION_MASK) {
        index |= 2;
    }
    if ((eventPtr->xkey.state & ShiftMask)
            || (/* (dispPtr->lockUsage != LU_IGNORE)
                   && */ (eventPtr->xkey.state & LockMask))) {
        index |= 1;
    }

    /*
     * First try of the actual translation.
     */

    sym = XKeycodeToKeysym(dispPtr->display, eventPtr->xkey.keycode, index);

    /*
     * Special handling: If the key was shifted because of Lock, but lock is
     * only caps lock, not shift lock, and the shifted keysym isn't
     * upper-case alphabetic, then switch back to the unshifted keysym.
     */

    if ((index & 1) && !(eventPtr->xkey.state & ShiftMask)
            /*&& (dispPtr->lockUsage == LU_CAPS)*/ ) {

        /*
         * FIXME: Keysyms are only identical to Unicode for ASCII and
         * Latin-1, so we can't use Tcl_UniCharIsUpper() for keysyms outside
         * that range.  This may be a serious problem here.
         */

        if ((sym == NoSymbol) || (sym > LATIN1_MAX)
                || !Tcl_UniCharIsUpper(sym)) {
            index &= ~1;
            sym = XKeycodeToKeysym(dispPtr->display, eventPtr->xkey.keycode,
                    index);
        }
    }

    /*
     * Another bit of special handling: If this is a shifted key and there is
     * no keysym defined, then use the keysym for the unshifted key.
     */

    if ((index & 1) && (sym == NoSymbol)) {
        sym = XKeycodeToKeysym(dispPtr->display, eventPtr->xkey.keycode,
                index & ~1);
    }
    return sym;
}

/*
 *--------------------------------------------------------------
 *
 * TkpInitKeymapInfo --
 *
 *      This procedure is invoked to scan keymap information to recompute
 *      stuff that's important for binding, such as the modifier key (if any)
 *      that corresponds to the "Mode_switch" keysym.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Keymap-related information in dispPtr is updated.
 *
 *--------------------------------------------------------------
 */

void
TkpInitKeymapInfo(
    TkDisplay *dispPtr)         /* Display for which to recompute keymap
                                 * information. */
{
    dispPtr->bindInfoStale = 0;

    /*
     * Behaviours that are variable on X11 are defined constant on MacOSX.
     * lockUsage is only used above in TkpGetKeySym(), nowhere else
     * currently.  There is no offical "Mode_switch" key.
     */

    dispPtr->lockUsage = LU_CAPS;
    dispPtr->modeModMask = 0;
    dispPtr->altModMask = ALT_MASK;
    dispPtr->metaModMask = OPTION_MASK;

    /*
     * MacOSX doesn't use the keycodes for the modifiers for anything, and we
     * don't generate them either (the keycodes actually given in the
     * simulated modifier events are bogus).  So there is no modifier map.
     * If we ever want to simulate real modifier keycodes, the list will be
     * constant on Carbon.
     */

    if (dispPtr->modKeyCodes != NULL) {
        ckfree((char *) dispPtr->modKeyCodes);
    }
    dispPtr->numModKeyCodes = 0;
    dispPtr->modKeyCodes = NULL;
}
