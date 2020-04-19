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
/*
 * A couple of simple definitions to make code a bit more self-explaining.
 *
 * For the assignments of Mod1==meta==command and Mod2==alt==option, see also
 * tkMacOSXMouseEvent.c.
 */

#define LATIN1_MAX	 255
#define MAC_KEYCODE_MAX	 0x7F
#define MAC_KEYCODE_MASK 0xFF
#define COMMAND_MASK	 Mod1Mask
#define OPTION_MASK	 Mod2Mask


/*
 * Table enumerating the special keys defined on Mac keyboards. These are
 * necessary for correct keysym mappings for all keys where the keysyms are
 * not identical with their ASCII or Latin-1 code points.
 *
 * This table includes every key listed in Apple's documentation of Function-Key
 * Unicodes which is not marked as "Not on most Macintosh keyboards".  I
 * know of no reliable way to find the Apple "Virtual Keycode" for any of the
 * keys that are so marked.
 */

typedef struct {
    int virtual;	       /* value of [NSEvent keyCode] */
    KeySym keysym;	       /* X11 keysym */
    KeyCode keychar;           /* XEvent keycode & 0xFFFF */
} KeyInfo;

static const KeyInfo keyArray[] = {
    {36,	XK_Return,	NSNewlineCharacter},
    {48,	XK_Tab,		NSTabCharacter},
    {51,	XK_BackSpace,	NSBackspaceCharacter},
    {52,	XK_Return,	NSNewlineCharacter},  /* Used on some Powerbooks */
    {53,	XK_Escape,	0x1B},
    {54,	XK_Meta_R,      0},
    {55,	XK_Meta_L,	0},
    {56,	XK_Shift_L,	0},
    {57,	XK_Caps_Lock,   0},
    {58,	XK_Alt_L,	0},
    {59,	XK_Control_L,	0},
    {60,	XK_Shift_R, 	0},
    {61,	XK_Alt_R,	0},
    {62,	XK_Control_R,	0},
    {63,	XK_Super_L,	0},
    {64,	XK_F17,		NSF17FunctionKey},
    {71,	XK_Clear,       NSClearLineFunctionKey}, /* Numlock on PC */
    {76,	XK_KP_Enter,	NSEnterCharacter},       /* Fn Return */
    {79,	XK_F18,		NSF18FunctionKey},
    {80,	XK_F19,		NSF19FunctionKey},
    {90,	XK_F20,		NSF20FunctionKey}, /* For scripting only */
    {96,	XK_F5,		NSF5FunctionKey},
    {97,	XK_F6,		NSF6FunctionKey},
    {98,	XK_F7,		NSF7FunctionKey},
    {99,	XK_F3,		NSF3FunctionKey},
    {100,	XK_F8,		NSF8FunctionKey},
    {101,	XK_F9,		NSF9FunctionKey},
    {103,	XK_F11,		NSF11FunctionKey},
    {105,	XK_F13,		NSF13FunctionKey},
    {107,	XK_F14,		NSF14FunctionKey},
    {109,	XK_F10,		NSF10FunctionKey},
    {105,	XK_F13,		NSF13FunctionKey},
    {106,	XK_F16,		NSF16FunctionKey},
    {111,	XK_F12,		NSF12FunctionKey},
    {113,	XK_F15,		NSF15FunctionKey},
    {114,	XK_Help,	NSHelpFunctionKey},
    {115,	XK_Home,	NSHomeFunctionKey},     /* Fn Left */
    {116,	XK_Page_Up,	NSPageUpFunctionKey},   /* Fn Up */
    {117,	XK_Delete,	NSDeleteFunctionKey},   /* Fn Deleete */
    {118,	XK_F4,		NSF4FunctionKey},
    {119,	XK_End,		NSEndFunctionKey},      /* Fn Right */
    {120,	XK_F2,		NSF2FunctionKey},
    {121,	XK_Page_Down,	NSPageDownFunctionKey}, /* Fn Down */
    {122,	XK_F1,		NSF1FunctionKey},
    {123,	XK_Left,	NSLeftArrowFunctionKey},
    {124,	XK_Right,	NSRightArrowFunctionKey},
    {125,	XK_Down,	NSDownArrowFunctionKey},
    {126,	XK_Up,		NSUpArrowFunctionKey},
    {0,		0, 		0}
};

/*
 * X11 keysyms for modifier keys, in order.
 */

#define NUM_MOD_KEYCODES 14
static KeyCode modKeyArray[NUM_MOD_KEYCODES] = {
    XK_Shift_L,
    XK_Shift_R,
    XK_Control_L,
    XK_Control_R,
    XK_Caps_Lock,
    XK_Shift_Lock,
    XK_Meta_L,
    XK_Meta_R,
    XK_Alt_L,
    XK_Alt_R,
    XK_Super_L,
    XK_Super_R,
    XK_Hyper_L,
    XK_Hyper_R,
};

static int initialized = 0;
static int keyboardChanged = 1;
static Tcl_HashTable virtual2keysym;	/* Maps Mac keyCode to X11 keysym. */
static Tcl_HashTable keysym2keycode;	/* Maps X11 keysym to Mac keycode. */
static int latin1Table[LATIN1_MAX+1];	/* Reverse mapping table for Latin-1. */

/*
 * Prototypes for static functions used in this file.
 */

static void	InitKeyMaps (void);
static void	InitLatin1Table(Display *display);
static int	KeycodeToUnicode(UniChar * uniChars, int maxChars,
			UInt16 keyaction, UInt32 keycode, UInt32 modifiers,
			UInt32 * deadKeyStatePtr);

#pragma mark TKApplication(TKKeyboard)

@implementation TKApplication(TKKeyboard)
- (void) keyboardChanged: (NSNotification *) notification
{
#ifdef TK_MAC_DEBUG_NOTIFICATIONS
    TKLog(@"-[%@(%p) %s] %@", [self class], self, _cmd, notification);
#endif
    keyboardChanged = 1;
}
@end

#pragma mark -

/*
 *----------------------------------------------------------------------
 *
 * InitKeyMaps --
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
InitKeyMaps(void)
{
    Tcl_HashEntry *hPtr;
    const KeyInfo *kPtr;
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
    initialized = 1;
}

/*
 *----------------------------------------------------------------------
 *
 * InitLatin1Table --
 *
 *	Creates a simple table to be used for mapping from Latin-1 keysyms to
 *      keycodes as used in XEvents.  Always needs to be called before using
 *      latin1Table, because the keyboard layout may have changed, and then the
 *      table must be re-computed.  The high order byte of these keycodes is
 *      overwritten with the modifier flags, which can be used to set the state
 *      of an XEvent that uses the keycode.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the global latin1Table.
 *
 *----------------------------------------------------------------------
 */

static void
InitLatin1Table(
    Display *display)
{
    int keycode;
    KeySym keysym;
    int state;
    int modifiers;

    memset(latin1Table, 0, sizeof(latin1Table));

    /*
     * In the common X11 implementations, a keymap has four columns
     * "plain", "Shift", "Mode_switch" and "Mode_switch + Shift". We don't
     * use "Mode_switch", but we use "Option" instead. (This is similar to
     * Apple's X11 implementation, where "Mode_switch" is used as an alias
     * for "Option".)
     *
     * So here we go through all 4 columns of the keymap and find all
     * Latin-1 compatible keycodes. We go through the columns back-to-front
     * from the more exotic columns to the more simple, so that simple
     * keycode-modifier combinations are preferred in the resulting table.
     */

    for (state = 3; state >= 0; state--) {
	modifiers = 0;
	if (state & 1) {
	    modifiers |= shiftKey;
	}
	if (state & 2) {
	    modifiers |= optionKey;
	}

	for (keycode = 0; keycode <= MAC_KEYCODE_MAX; keycode++) {
	    keysym = XKeycodeToKeysym(display, keycode<<16, state);
	    if (keysym != NoSymbol && keysym <= LATIN1_MAX) {
		latin1Table[keysym] = modifiers << 16 | keycode << 16 | keysym;
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * KeycodeToUnicode --
 *
 *	Given MacOS key event data this function generates the Unicode
 *	characters. It does this using OS resources and APIs.
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
KeycodeToUnicode(
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

	keycode &= 0xFF;
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
 *	Translate from the system-dependent keycode used in an XEvent
 *      to a system-independent X11 keysym.
 *
 * Results:
 *	Returns the translated keysym, or NoSymbol on failure.
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
	InitKeyMaps();
    }

    /*
     * First check if the virtual keycode corresponds to a special key such as
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
     * No? Check if the key represents a Latin-1 letter.
     */

    modifiers = (state & 1 ? shiftKey : 0) | (state & 2 ? optionKey : 0);
    KeycodeToUnicode(&keyChar, 1, kUCKeyActionDown, virtual, modifiers, NULL);
    if ((keyChar >= XK_space) && (keyChar <= LATIN1_MAX)) {
	return keyChar;
    }

    /*
     * This keycode does not belong to a key on any known Macintosh keyboard.
     */

    return NoSymbol;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetString --
 *
 *	Retrieve the string equivalent for the given keyboard event.
 *
 * Results:
 *	Returns the UTF string.
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
     * MacOSX doesn't use the key codes for the modifiers for anything, and we
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
 *	However, Tk already does this for the most common keysyms. Therefore,
 *	these functions only need to support keysyms that will be specific to
 *	the Macintosh. Currently, there are none.
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
 *	8-bit "virtual keycode" in the third byte.  (The virtual keycode is the
 *	8-bit value that appears as [NSEvent keycode] in an NSKeyUp or
 *	NSKeyDown event.)  On the Mac pressing a modifier key generates an
 *	NSFlagsChanged event but not an NSKeyDown event.  If a keysym
 *	represents a modifier key, the unicode character is 0, but the modifier
 *	flags are set in the high order byte.  For keysyms that represent other
 *	keys, the resulting keycode has no modifiers.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

KeyCode
XKeysymToKeycode(
    Display *display,
    KeySym keysym)
{
    Tcl_HashEntry *hPtr;

    if (keysym <= LATIN1_MAX) {

	/*
	 * Handle keysyms in the Latin-1 range where keysym and Unicode
	 * character code point are the same.
	 */

	if (keyboardChanged) {
	    InitLatin1Table(display);
	    keyboardChanged = 0;
	}
	return latin1Table[keysym];
    }

    /*
     * This is not a Latin-1 key.  Try doing a hash table lookup to find the
     * keycode.
     */

    if (!initialized) {
	InitKeyMaps();
    }

    hPtr = Tcl_FindHashEntry(&keysym2keycode, INT2PTR(keysym));
    if (hPtr != NULL) {
	return (KeyCode) Tcl_GetHashValue(hPtr);
    }

    /*
     * The keysym is not Latin-1 and not in our hash table, so it does not
     * appear on any known Macintosh keyboard; just return 0.
     */

    return 0;
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
	Display *display = Tk_Display(tkwin);
	eventPtr->xkey.keycode = XKeysymToKeycode(display, keysym);
	if ((shiftKey << 16) & eventPtr->xkey.keycode) {
	    eventPtr->xkey.state |= ShiftMask;
	}
	if ((optionKey << 16) & eventPtr->xkey.keycode) {
	    eventPtr->xkey.state |= OPTION_MASK;
	}
	eventPtr->xkey.keycode &= 0xFFFFFF;
	if (keysym <= LATIN1_MAX) {
	    int length = TkUniCharToUtf(keysym, eventPtr->xkey.trans_chars);
	    eventPtr->xkey.trans_chars[length] = 0;
	} else {
	    eventPtr->xkey.trans_chars[0] = 0;
	}
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
     * Handle pure modifier keys specially. We use -1 as a signal for
     * this.
     */

    if (eventPtr->xany.send_event == -1) {
	switch (eventPtr->xkey.keycode >> 16) {
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

    /* If nbytes has been set, it's not a function key, but a regular key that
       has been translated in tkMacOSXKeyEvent.c; just use that. */
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

    /*
     * We want Option key combinations to use their base chars as keysyms, so
     * we ignore the option modifier here.
     */

#if 0
    if (eventPtr->xkey.state & OPTION_MASK) {
	index |= 2;
    }
#endif

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
     * only caps lock, not shift lock, and the shifted keysym isn't upper-case
     * alphabetic, then switch back to the unshifted keysym.
     */

    if ((index & 1) && !(eventPtr->xkey.state & ShiftMask)
	    /*&& (dispPtr->lockUsage == LU_CAPS)*/ ) {
	/*
	 * FIXME: Keysyms are only identical to Unicode for ASCII and Latin-1,
	 * so we can't use Tcl_UniCharIsUpper() for keysyms outside that range.
	 * This may be a serious problem here.
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
     * Behaviours that are variable on X11 are defined constant on MacOSX.
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

    dispPtr->altModMask = OPTION_MASK;
    dispPtr->metaModMask = COMMAND_MASK;
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
