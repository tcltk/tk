/*
 * tkMacOSXKeyboard.c --
 *
 *	Routines to support keyboard events on the Macintosh.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright 2001-2009, Apple Inc.
 * Copyright (c) 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkMacOSXEvent.h"
#include "tkMacOSXConstants.h"
#include "tkMacOSXKeysyms.h"

/*
 * When converting native key events to X11 key events, each platform is
 * allowed to choose what information to encode in the XEvent.xkey.keycode
 * field.  On the Macintosh, every non-modifier key has a unicode character
 * associated to it.  For non-text keys this unicode character is in the
 * private use range 0xF700 - 0xF8FF.  Modifier keys, however, do not produce
 * KeyDown or KeyUp events, rather FlagsChanged events, so they do not have an
 * associated unicode character.
 *
 * When constructing an XEvent from a KeyDown or KeyUo NSEvent, the
 * XEvent.xkey.keycode field is constructed by using bits 0-15 to hold the
 * first unicode character of the [NSEvent characters] attribute of the
 * NSEvent, and bits 16-23 to hold the value of the [NSEvent keyCode]
 * attribute.  The keyCode attribute identifies a location on the keyboard,
 * and Apple calls it a "virtual keycode". It is allowed for the characters
 * attribute to have length greater than 1, but that does not appear to happen
 * for any known keyboards.
 *
 * When generating an XEvent with the event generate command, the unicode
 * character is determined from the X11 keysym and, if that unicode character
 * belongs to a key on the current keyboard layout bits 16-23 are set to the
 * virtual keycode of that key.  Otherwise they are cleared.
 *
 * KeyPress or KeyRelease XEvents are also constructed when a FlagsChanged
 * NSEvent is processed.  For these, the unicode character is set to 0xF8FF,
 * the last value of the private use range.
 */

#define VIRTUAL_MAX	 0x7F
#define MAC_KEYCODE_MASK 0xFF

/*
 * Hash tables used to translate between various key attributes.
 */

static Tcl_HashTable virtual2keysym;	/* Special virtual keycode to keysym */
static Tcl_HashTable keysym2keycode;	/* keysym to XEvent keycode */
static Tcl_HashTable keysym2unichar;	/* keysym to unichar */
static Tcl_HashTable unichar2keysym;	/* unichar to X11 keysym */
static Tcl_HashTable unichar2virtual;	/* unichar to virtual keycode */

/*
 * Flags.
 */

static BOOL initialized = NO;
static BOOL keyboardChanged = YES;

/*
 * Prototypes for static functions used in this file.
 */

static void	InitHashTables(void);
static void     UpdateKeymap(void);
static int	KeyDataToUnicode(UniChar * uniChars, int maxChars,
			UInt16 keyaction, UInt32 keycode, UInt32 modifiers,
			UInt32 * deadKeyStatePtr);

#pragma mark TKApplication(TKKeyboard)

@implementation TKApplication(TKKeyboard)
- (void) keyboardChanged: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
#endif
    keyboardChanged = YES;
    UpdateKeymap();
}
@end

#pragma mark -

/*
 *----------------------------------------------------------------------
 *
 * InitHashTables --
 *
 *	Creates hash tables used by some of the functions in this file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates memory & creates some hash tables.
 *
 *----------------------------------------------------------------------
 */

static void
InitHashTables(void)
{
    Tcl_HashEntry *hPtr;
    const KeyInfo *kPtr;
    const KeysymInfo *ksPtr;
    int dummy;

    Tcl_InitHashTable(&virtual2keysym, TCL_ONE_WORD_KEYS);
    Tcl_InitHashTable(&keysym2keycode, TCL_ONE_WORD_KEYS);
    for (kPtr = keyArray; kPtr->virtual != 0; kPtr++) {
	hPtr = Tcl_CreateHashEntry(&virtual2keysym, INT2PTR(kPtr->virtual),
				   &dummy);
	Tcl_SetHashValue(hPtr, INT2PTR(kPtr->keysym));
	hPtr = Tcl_CreateHashEntry(&keysym2keycode, INT2PTR(kPtr->keysym),
				   &dummy);
	Tcl_SetHashValue(hPtr, INT2PTR(kPtr->keychar | (kPtr->virtual << 16)));
    }
    Tcl_InitHashTable(&keysym2unichar, TCL_ONE_WORD_KEYS);
    Tcl_InitHashTable(&unichar2keysym, TCL_ONE_WORD_KEYS);
    for (ksPtr = keysymTable; ksPtr->keysym != 0; ksPtr++) {
	hPtr = Tcl_CreateHashEntry(&keysym2unichar, INT2PTR(ksPtr->keysym),
				   &dummy);
	Tcl_SetHashValue(hPtr, INT2PTR(ksPtr->keycode));
	hPtr = Tcl_CreateHashEntry(&unichar2keysym, INT2PTR(ksPtr->keycode),
				   &dummy);
	Tcl_SetHashValue(hPtr, INT2PTR(ksPtr->keysym));
    }
    UpdateKeymap();
    initialized = YES;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateKeymap --
 *
 *	Called when the keyboard changes to update the hash table that
 *      maps unicode characters to virtual keycodes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initializes, if necessary, and updates the unichar2virtual table.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateKeymap()
{
    static int keymapInitialized = 0;
    UniChar keyChar = 0;
    Tcl_HashEntry *hPtr;
    int virtual, state, modifiers, dummy;

    if (!keymapInitialized) {
	Tcl_InitHashTable(&unichar2virtual, TCL_ONE_WORD_KEYS);
    } else {
	Tcl_DeleteHashTable(&unichar2virtual);
    }
    for (state = 3; state >= 0; state--) {
	for (virtual = 0; virtual <= VIRTUAL_MAX; virtual++) {
	    modifiers = (state & 1 ? shiftKey : 0) | (state & 2 ? optionKey : 0);
	    KeyDataToUnicode(&keyChar, 1, kUCKeyActionDown, virtual, modifiers, NULL);
	    hPtr = Tcl_CreateHashEntry(&unichar2virtual, INT2PTR(keyChar),
				       &dummy);
	    Tcl_SetHashValue(hPtr, INT2PTR(state << 8 | virtual));
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * KeyDataToUnicode --
 *
 *	Given MacOS key event data this function generates the unicode
 *	characters. It does this using OS resources from the Carbon
 *      framework.
 *
 *	The parameter deadKeyStatePtr can be NULL, if no deadkey handling is
 *	needed.
 *
 *	This function is called from XKeycodeToKeysym() in tkMacOSKeyboard.c.
 *
 * Results:
 *	The number of characters generated if any, 0 if we are waiting for
 *	another byte of a dead-key sequence. Fills in the uniChars array with a
 *	Unicode string.
 *
 * Side Effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static int
KeyDataToUnicode(
    UniChar *uniChars,
    int maxChars,
    UInt16 keyaction,
    UInt32 keycode,
    UInt32 modifiers,
    UInt32 *deadKeyStatePtr)
{
    static const void *uchr = NULL;
    static UInt32 keyboardType = 0;
    UniCharCount actuallength = 0;

    if (keyboardChanged) {
	TISInputSourceRef currentKeyboardLayout =
		TISCopyCurrentKeyboardLayoutInputSource();

	if (currentKeyboardLayout) {
	    CFDataRef keyLayoutData = (CFDataRef) TISGetInputSourceProperty(
		    currentKeyboardLayout, kTISPropertyUnicodeKeyLayoutData);

	    if (keyLayoutData) {
		uchr = CFDataGetBytePtr(keyLayoutData);
		keyboardType = LMGetKbdType();
	    }
	    CFRelease(currentKeyboardLayout);
	}
	keyboardChanged = 0;
    }
    if (uchr) {
	OptionBits options = 0;
	UInt32 dummyState;
	OSStatus err;

	keycode &= 0xFFFF;
	modifiers = (modifiers >> 8) & 0xFF;
	if (!deadKeyStatePtr) {
	    options = kUCKeyTranslateNoDeadKeysMask;
	    dummyState = 0;
	    deadKeyStatePtr = &dummyState;
	}
	err = ChkErr(UCKeyTranslate, uchr, keycode, keyaction, modifiers,
		keyboardType, options, deadKeyStatePtr, maxChars,
		&actuallength, uniChars);
	if (!actuallength && *deadKeyStatePtr) {

	    /*
	     * We are waiting for another key.
	     */

	    return 0;
	}
	*deadKeyStatePtr = 0;
	if (err != noErr) {
	    actuallength = 0;
	}
    }
    return actuallength;
}

/*
 *----------------------------------------------------------------------
 *
 * XKeycodeToKeysym --
 *
 *	Stub function which translate from the platform-specific keycode used
 *      in an XEvent to an X11 keysym.  On the Macintosh, the display input
 *      is ignored and only the virtual keycode in bits 16-23 is used.
 *
 * Results:
 *      Returns the corresponding keysym, or NoSymbol if the keysym cannot
 *	be found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

KeySym
XKeycodeToKeysym(
    Display* display,
    KeyCode keycode,
    int state)
{
    Tcl_HashEntry *hPtr;
    int virtual, modifiers = 0;
    UniChar keyChar = 0;

    (void) display; /*unused*/

    if (!initialized) {
	InitHashTables();
    }

    /*
     * First check if the virtual keycode corresponds to a special key, such as
     * an Fn function key or Tab, Backspace, Home, End, etc.
     */

    virtual = (keycode >> 16) & 0xFF;
    if (virtual) {
	hPtr = Tcl_FindHashEntry(&virtual2keysym, INT2PTR(virtual));
	if (hPtr != NULL) {
	    return (KeySym) Tcl_GetHashValue(hPtr);
	}
    }

    /*
     * If not, use Carbon to find the unicode character and translate it
     * to a keysym using a hash table.
     */

    modifiers = (state & 1 ? shiftKey : 0) | (state & 2 ? optionKey : 0);
    KeyDataToUnicode(&keyChar, 1, kUCKeyActionDown, virtual, modifiers, NULL);
    hPtr = Tcl_FindHashEntry(&unichar2keysym, INT2PTR(keyChar));
    if (hPtr != NULL) {
	return (KeySym) Tcl_GetHashValue(hPtr);
    }
    return NoSymbol;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetString --
 *
 *	Retrieve the string stored in the transchars field of an XEvent
 *      and convert it to a DString.
 *
 * Results:
 *	Returns the DString.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
TkpGetString(
    TkWindow *winPtr,		/* Window where event occurred: Needed to get
				 * input context. */
    XEvent *eventPtr,		/* X keyboard event. */
    Tcl_DString *dsPtr)		/* Uninitialized or empty string to hold
				 * result. */
{
    (void) winPtr; /*unused*/
    int ch;

    Tcl_DStringInit(dsPtr);
    return Tcl_DStringAppend(dsPtr, eventPtr->xkey.trans_chars,
	    TkUtfToUniChar(eventPtr->xkey.trans_chars, &ch));
}

/*
 *----------------------------------------------------------------------
 *
 * XGetModifierMapping --
 *
 *	Fetch the current keycodes used as modifiers.
 *
 * Results:
 *	Returns a new modifier map.
 *
 * Side effects:
 *	Allocates a new modifier map data structure.
 *
 *----------------------------------------------------------------------
 */

XModifierKeymap *
XGetModifierMapping(
    Display *display)
{
    XModifierKeymap *modmap;

    (void) display; /*unused*/

    /*
     * MacOS doesn't use the key codes for the modifiers for anything, and we
     * don't generate them either. So there is no modifier map.
     */

    modmap = ckalloc(sizeof(XModifierKeymap));
    modmap->max_keypermod = 0;
    modmap->modifiermap = NULL;
    return modmap;
}

/*
 *----------------------------------------------------------------------
 *
 * XFreeModifiermap --
 *
 *	Deallocate a modifier map that was created by XGetModifierMapping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees the datastructure referenced by modmap.
 *
 *----------------------------------------------------------------------
 */

int
XFreeModifiermap(
    XModifierKeymap *modmap)
{
    if (modmap->modifiermap != NULL) {
	ckfree(modmap->modifiermap);
    }
    ckfree(modmap);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XKeysymToString, XStringToKeysym --
 *
 *	These X window functions map keysyms to strings & strings to keysyms.
 *      These are never called because we define REDO_KEYSYM_LOOKUP, which
 *      instructs tkBind to do the conversion for us.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
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
 *	Converts an XKeysym to the value which would be used as the keycode
 *      field of a KeyPress or KeyRelease XEvent for the corresponding key.
 *      This is an opaque stub function which knows how the keycode field is
 *      generated on a Mac.
 *
 * Results:
 *
 *      An X11 KeyCode with a unicode character in the low 16 bits and the
 *	8-bit "virtual keycode" in the third byte.  See the description of
 *      keycodes on the Macintosh at the top of this file.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
KeyCode
XKeysymToKeycodeWithState(
    Display *display,
    KeySym keysym,
    int *state)
{
    Tcl_HashEntry *hPtr;

    if (!initialized) {
	InitHashTables();
    }
    hPtr = Tcl_FindHashEntry(&keysym2unichar, INT2PTR(keysym));
    if (hPtr != NULL) {
	KeyCode character = (KeyCode) Tcl_GetHashValue(hPtr);
	hPtr = Tcl_FindHashEntry(&unichar2virtual, INT2PTR(character));
	if (hPtr != NULL) {
	    KeyCode lookup = ((KeyCode) Tcl_GetHashValue(hPtr));
	    KeyCode virtual = lookup & 0xFF;
	    *state = lookup >> 8;
	    return virtual << 16 | character;
	} else {
	    return character;
	}
    }

    /*
     * This is not a text key.  Try doing a hash table lookup to find the
     * keycode for a special key.
     */

    hPtr = Tcl_FindHashEntry(&keysym2keycode, INT2PTR(keysym));
    if (hPtr != NULL) {
	return (KeyCode) Tcl_GetHashValue(hPtr);
    }

    /*
     * Could not construct a keycode.
     */

    return 0;
}

KeyCode
XKeysymToKeycode(
    Display *display,
    KeySym keysym)
{
    int state;
    return XKeysymToKeycodeWithState(display, keysym, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetKeycodeAndState --
 *
 *	This function accepts a keysym and an XEvent and sets some fields of
 *	the XEvent.  It is used by the event generate command.
 *
 * Results:
 *      None
 *
 * Side effects:
 *
 *	Modifies the XEvent. Sets the xkey.keycode to a keycode value formatted
 *	by XKeysymToKeycode and sets the shift and option flags in xkey.state
 *	to the values implied by the keysym. Also fills in xkey.trans_chars, so
 *	that the actual characters can be retrieved later.
 *
 *----------------------------------------------------------------------
 */
void
TkpSetKeycodeAndState(
    Tk_Window tkwin,
    KeySym keysym,
    XEvent *eventPtr)
{
    if (keysym == NoSymbol) {
	eventPtr->xkey.keycode = 0;
    } else {
	int state, length = 0;
	UniChar keychar;
	Display *display = Tk_Display(tkwin);
	eventPtr->xkey.keycode = XKeysymToKeycodeWithState(display, keysym,
							   &state);
	eventPtr->xkey.state |= state;
	keychar = eventPtr->xkey.keycode & 0xFFFF;
	
	/*
	 * Set trans_chars for keychars outside of the private-use range.
	 */

	if (keychar < 0xF700) {
	    length = TkUniCharToUtf(keychar, eventPtr->xkey.trans_chars);
	}
	eventPtr->xkey.trans_chars[length] = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetKeySym --
 *
 *	Given an X KeyPress or KeyRelease event, map the keycode in the event
 *	to a keysym.
 *
 * Results:
 *	The return value is the keysym corresponding to eventPtr, or NoSymbol
 *	if no matching keysym could be found.
 *
 * Side effects:
 *	In the first call for a given display, keycode-to-keysym maps get
 *	loaded.
 *
 *----------------------------------------------------------------------
 */

KeySym
TkpGetKeySym(
    TkDisplay *dispPtr,		/* Display in which to map keycode. */
    XEvent *eventPtr)		/* Description of X event. */
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
     * Modifier key events have a special mac keycode (see tkProcessKeyEvent).
     */

    if ((eventPtr->xkey.keycode & MAC_KEYCODE_MASK) == 0xF8FF) {
	switch (eventPtr->xkey.keycode >> 16) { /* the virtual keyCode */
	case 54:
	    return XK_Meta_R;
	case 55:
	    return XK_Meta_L;
	case 56:
	    return XK_Shift_L;
	case 57:
	    return XK_Caps_Lock;
	case 58:
	    return XK_Alt_L;
	case 59:
	    return XK_Control_L;
	case 60:
	    return XK_Shift_R;
	case 61:
	    return XK_Alt_R;
	case 62:
	    return XK_Control_R;
	case 63:
	    return XK_Super_L;
	default:
	    return NoSymbol;
	}
    }

    /*
     * If nbytes has been set, it's not a function key, but a regular key that
     * has been translated in tkMacOSXKeyEvent.c; just use that.
     */

    if (eventPtr->xkey.nbytes) {
	return eventPtr->xkey.keycode;
    }

    /*
     * Figure out which of the four slots in the keymap vector to use for this
     * key. Refer to Xlib documentation for more info on how this computation
     * works. (Note: We use "Option" in keymap columns 2 and 3 where other
     * implementations have "Mode_switch".)
     */

    index = 0;
    if (eventPtr->xkey.state & Mod2Mask) { /* Option */
	index |= 2;
    }
    if ((eventPtr->xkey.state & ShiftMask)
	    || (/* (dispPtr->lockUsage != LU_IGNORE)
	    && */ (eventPtr->xkey.state & LockMask))) {
	index |= 1;
    }

    /*
     * First do the straightforward lookup.
     */

    sym = XKeycodeToKeysym(dispPtr->display, eventPtr->xkey.keycode, index);

    /*
     * Special handling: If the key was shifted because of Lock, but lock is
     * only caps lock, not shift lock, and the shifted keysym isn't upper-case
     * alphabetic, then switch back to the unshifted keysym.
     */

    if ((index & 1) && !(eventPtr->xkey.state & ShiftMask)
	    /*&& (dispPtr->lockUsage == LU_CAPS)*/ ) {

	if ((sym == NoSymbol) || !Tcl_UniCharIsUpper(sym)) {
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
 *	This procedure is invoked to scan keymap information to recompute stuff
 *	that's important for binding, such as the modifier key (if any) that
 *	corresponds to the "Mode_switch" keysym.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Keymap-related information in dispPtr is updated.
 *
 *--------------------------------------------------------------
 */

void
TkpInitKeymapInfo(
    TkDisplay *dispPtr)		/* Display for which to recompute keymap
				 * information. */
{
    dispPtr->bindInfoStale = 0;

    /*
     * Behaviors that are variable on X11 are defined constant on MacOSX.
     * lockUsage is only used above in TkpGetKeySym(), nowhere else currently.
     * There is no offical "Mode_switch" key.
     */

    dispPtr->lockUsage = LU_CAPS;
    dispPtr->modeModMask = 0;
#if 0
    /*
     * With this, <Alt> and <Meta> become synonyms for <Command> and <Option>
     * in bindings like they are (and always have been) in the keysyms that
     * are reported by KeyPress events. But the init scripts like text.tcl
     * have some disabling bindings for <Meta>, so we don't want this without
     * some changes in those scripts. See also bug #700311.
     */

    dispPtr->altModMask = Mod2Mask;  /* Option key */
    dispPtr->metaModMask = Mod1Mask; /* Command key */
#else
    dispPtr->altModMask = 0;
    dispPtr->metaModMask = 0;
#endif

    /*
     * MacOSX doesn't create a key event when a modifier key is pressed or
     * released.  However, it is possible to generate key events for
     * modifier keys, and this is done in the tests.  So we construct an array
     * containing the keycodes of the standard modifier keys from static data.
     */

    if (dispPtr->modKeyCodes != NULL) {
	ckfree(dispPtr->modKeyCodes);
    }
    dispPtr->numModKeyCodes = NUM_MOD_KEYCODES;
    dispPtr->modKeyCodes = (KeyCode *)ckalloc(NUM_MOD_KEYCODES * sizeof(KeyCode));
    for (int i = 0; i < NUM_MOD_KEYCODES; i++) {
	dispPtr->modKeyCodes[i] = XKeysymToKeycode(NULL, modKeyArray[i]);
    }
}

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
