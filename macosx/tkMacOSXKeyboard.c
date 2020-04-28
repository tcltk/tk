/*
 * tkMacOSXKeyboard.c --
 *
 *	Routines to support keyboard events on the Macintosh.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright 2001-2009, Apple Inc.
 * Copyright (c) 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright (c) 2020 Marc Culler
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkMacOSXEvent.h"
#include "tkMacOSXConstants.h"
#include "tkMacOSXKeysyms.h"

#define IS_PRINTABLE(keychar) ((keychar >= 0x20) && \
			       (keychar != 0x7f) && \
			       (keychar < 0xF700))
#define ON_KEYPAD(virtual) ((virtual >= 0x41) && (virtual <= 0x5C))
#define VIRTUAL_MAX	 0x7F
#define MAC_KEYCHAR_MASK 0xFFFFFF

/*
 * About keyboards
 * ---------------
 * Keyboards are complicated.  This long comment is an attempt to provide
 * enough information about them to make it possible to read and understand
 * the code in this file.
 *
 * Every key on a keyboard is identified by a number between 0 and 127.  In
 * macOS, pressing or releasing a key on the keyboard generates an NSEvent of
 * type KeyDown, KeyUp or FlagsChanged.  The 8-bit identifier of the key that
 * was involved in this event is provided in the attribute [NSEvent keyCode].
 * Apple also refers to this number as a "Virtual KeyCode".  In this file, to
 * avoid confusion with other uses of the word keycode, we will refer to this
 * key identifier as a "virtual keycode", usually the value of a variable named
 * "virtual".
 *
 * Some of the keys on a keyboard, such as the Shift, Option, Command or
 * Control keys, are "modifier" keys.  The effect of pressing or releasing a
 * key depends on three quantities:
 *     - which key is being pressed or released
 *     - which modifier keys are being held down at the moment
 *     - the current keyboard layout
 * If the key is a modifier key then the effect of pressing or releasing it is
 * only to change the list of which modifier keys are being held down.  Apple
 * reports this by sending an NSEvent of type FlagsChanged.  X11 reports this
 * as a KeyPress or KeyRelease event for the modifier key.  Note that there may
 * be combinations of modifier key states and key presses which have no effect.
 *
 * In X11 every meaningful effect from a key action is identified by a 16 bit
 * value known as a keysym.  Every keysym has an associated string name, also
 * known as a keysym.  The Tk bind command uses the X11 keysym string to
 * specify a key event which should invoke a certain action and it provides the
 * numeric and symbolic keysyms to the bound proc as %N and %K respectively.
 * An X11 XEvent which reports a KeyPress or KeyRelease does not include the
 * keysym.  Instead it includes a platform-specific numerical value called a
 * keycode which is available to the bound procedure as %k.  A platform port of
 * Tk must provide functions which convert between keycodes and numerical
 * keysyms.  Conversion between numerical and symbolic keysyms is provided by
 * the generic Tk code, although platforms are allowed to provide their own by
 * defining the XKeysymToString and XStringToKeysym functions and undefining
 * the macro REDO_KEYSYM_LOOKUP.  This macOS port uses the conversion provided
 * by the generic code.
 *
 * When the keyboard focus is on a Tk widget which provides text input, there
 * are some X11 KeyPress events which cause text to be inserted.  We will call
 * these "printable" events.  On macOS the text which should be inserted is
 * contained in the xkeys.trans_chars field of a key XEvent as a
 * null-terminated unicode string encoded with a special Tcl encoding.  The
 * value of the trans_chars string in an Xevent depends on more than the three
 * items above.  It may also depend on the sequence of keypresses that preceded
 * the one being reported by the XEvent.  For example, on macOS an <Alt-e>
 * event does not cause text to be inserted but a following <a> event causes an
 * accented 'a' to be inserted.  The events in such a composition sequence,
 * other than the final one, are known as "dead-key" events.
 *
 * MacOS packages the information described above in a different way.  Every
 * meaningful effect from a key action *other than changing the state of
 * modifier keys* is identified by a unicode string which is provided as the
 * [NSEvent characters] attribute of a KeyDown or KeyUp event.  FlagsChanged
 * events do not have characters.  In principle, the characters attribute could
 * be an arbitrary unicode string but in practice it is always a single UTF-16
 * character which we usually store in a variable named keychar.  While the
 * keychar is a legal unicode code point, it does not necessarily represent a
 * glyph. MacOS uses unicode code points in the private-use range 0xF700-0xF8FF
 * for non-printable events which have no associated ASCII code point.  For
 * example, pressing the F2 key generates an NSEvent with the character 0xF705,
 * the Backspace key produces 0x7F (ASCII del) and the Delete key produces
 * 0xF728.
 *
 * With the exception of modifier keys, it is possible to translate between
 * numerical X11 keysyms and macOS keychars; this file constructs Tcl hash
 * tables to do this job, using data defined in the file tkMacOSXKeysyms.h.
 * The code here adopts the convention that the keychar of any modifier key
 * is 0xF8FF, the last value in the private-use range.
 *
 * The macosx platform-specific scheme for generating a keycode when mapping an
 * NSEvent of type KeyUp, KeyDown or FlagsChanged to an XEvent of type KeyPress
 * or KeyRelease is as follows:
 *     keycode = (virtual << 24) | keychar
 * A few remarks are in order.  First, we are using 32 bits for the keycode and
 * we are allowing room for up to 24 bits for the keychar.  This means that
 * there is enough room in the keycode to hold a UTF-32 character, which only
 * requires 21 bits.  So, in the future, when macs have emoji keyboards, no
 * change will be needed in how keycodes are generated.  Second, the KeyCode
 * type for the keycode field in an XEvent is currently defined as unsigned
 * long, which means that it is 64 bits on modern macOS systems. Finally, there
 * is no obstruction to generating KeyPress events for keys that represent
 * letters which do not exist on the current keyboard layout.  And different
 * keyboard layouts can assign a given letter to different keys.  So we need a
 * convention for what value to assign to "virtual" when computing the keycode
 * for a generated event.  The convention used here is as follows:
 *   If there is a key on the current keyboard which produces the keychar,
 *   use the virtual keycode of that key.  Otherwise set virtual = 0.
 */

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
static int	KeyDataToUnicode(UniChar *uniChars, int maxChars,
			UInt16 keyaction, UInt32 virtual, UInt32 modifiers,
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
	Tcl_SetHashValue(hPtr, INT2PTR(kPtr->keychar | (kPtr->virtual << 24)));
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
 *      maps unicode characters to virtual keycodes with states.  In order
 *      for this to be well-defined we have to ignore virtual keycodes for
 *      keypad keys, since each keypad key has the same character as the
 *      corresponding key on the main keyboard.
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
    UniChar keychar;
    Tcl_HashEntry *hPtr;
    int virtual, state, modifiers, dummy;

    if (!keymapInitialized) {
	Tcl_InitHashTable(&unichar2virtual, TCL_ONE_WORD_KEYS);
    } else {
	Tcl_DeleteHashTable(&unichar2virtual);
    }
    for (state = 4; state >= 0; state--) {
	for (virtual = 0; virtual <= VIRTUAL_MAX; virtual++) {
	    if (ON_KEYPAD(virtual)) {
		continue;
	    }
	    modifiers =  (state & 1 ? shiftKey : 0) |
		(state & 2 ? optionKey : 0);
	    KeyDataToUnicode(&keychar, 1, kUCKeyActionDown, virtual, modifiers,
			     NULL);
	    hPtr = Tcl_CreateHashEntry(&unichar2virtual, INT2PTR(keychar),
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
 *	Given MacOS key event data this function generates the keychar.  It
 *	does this by using OS resources from the Carbon framework.
 *
 *	The parameter deadKeyStatePtr can be NULL, if no deadkey handling is
 *	needed (which is always the case here).
 *
 *	This function is called from XKeycodeToKeysym().
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
    UInt32 virtual,
    UInt32 modifiers,
    UInt32 *deadKeyStatePtr)
{
    static const void *layoutData = NULL;
    static UInt32 keyboardType = 0;
    UniCharCount actuallength = 0;

    if (keyboardChanged) {
	TISInputSourceRef currentKeyboardLayout =
		TISCopyCurrentKeyboardLayoutInputSource();

	if (currentKeyboardLayout) {
	    CFDataRef keyLayoutData = (CFDataRef) TISGetInputSourceProperty(
		    currentKeyboardLayout, kTISPropertyUnicodeKeyLayoutData);

	    if (keyLayoutData) {
		layoutData = CFDataGetBytePtr(keyLayoutData);
		keyboardType = LMGetKbdType();
	    }
	    CFRelease(currentKeyboardLayout);
	}
	keyboardChanged = 0;
    }
    if (layoutData) {
	OptionBits options = 0;
	UInt32 dummyState;
	OSStatus err;

	virtual &= 0xFF;
	modifiers = (modifiers >> 8) & 0xFF;
	if (!deadKeyStatePtr) {
	    options = kUCKeyTranslateNoDeadKeysMask;
	    dummyState = 0;
	    deadKeyStatePtr = &dummyState;
	}
	err = ChkErr(UCKeyTranslate, layoutData, virtual, keyaction, modifiers,
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
 *	This is a stub function which translates from the keycode used in an
 *      XEvent to a numerical keysym.  On macOS, the display parameter is
 *      ignored and only the the virtual keycode stored in bits 24-31 is
 *      used.
 *
 * Results:
 *      Returns the corresponding numerical keysym, or NoSymbol if the keysym
 *      cannot be found.
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

    virtual = (keycode >> 24) & 0xFF;
    if (virtual) {
	hPtr = Tcl_FindHashEntry(&virtual2keysym, INT2PTR(virtual));
	if (hPtr != NULL) {
	    return (KeySym) Tcl_GetHashValue(hPtr);
	}
    }

    /*
     * If not, use the Carbon Framework to find the unicode character and
     * translate it to a keysym using the unicode2keysym hash table.
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
 *	This is a stub function which retrieves the string stored in the
 *      transchars field of an XEvent and converts it to a Tcl_DString.
 *
 * Results:
 *	Returns a pointer to the string value of the DString.
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
 *	X11 stub function to get the keycodes used as modifiers.  This
 *      is never called by the macOS port.
 *
 * Results:
 *	Returns a newly allocated modifier map.
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
 *	Deallocates a modifier map that was created by XGetModifierMapping.
 *      This is also never called by the macOS port.
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
 *	These X11 stub functions map keysyms to strings & strings to keysyms.
 *      A platform can do its own conversion by defining these and undefining
 *      REDO_KEYSYM_LOOKUP.  The macOS port defines REDO_KEYSYM_LOOKUP so these
 *      are never called and Tk does the conversion for us.
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
 *	This is a stub function which converts a numerical keysym to the
 *      platform-specific keycode used in a KeyPress or KeyRelease XEvent.  The
 *      implementation calls a static function which also provides information
 *      about the modifier state, needed by TkpSetKeycodeAndState.
 *
 * Results:
 *
 *      On macOS, a KeyCode with a unicode character in the lowest 16 bits and
 *	the 8-bit "virtual keycode" in the highest byte.  See the description
 *	of keycodes on the Macintosh at the top of this file.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static KeyCode
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
	    return virtual << 24 | character;
	} else {
	    return character;
	}
    }

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
 *	to the values implied by the keysym. Also fills in xkey.trans_chars for
 *	printable events.
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
	int state;
	UniChar keychar;
	Display *display = Tk_Display(tkwin);
	eventPtr->xkey.keycode = XKeysymToKeycodeWithState(display, keysym,
							   &state);
	eventPtr->xkey.state |= state;
	keychar = eventPtr->xkey.keycode & MAC_KEYCHAR_MASK;

	/*
	 * Set trans_chars for keychars outside of the private-use range.
	 */

	if (IS_PRINTABLE(keychar)) {
	    int length = TkUniCharToUtf(keychar, eventPtr->xkey.trans_chars);
	    eventPtr->xkey.trans_chars[length] = 0;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetKeySym --
 *
 *	This is a stub function called in tkBind.c.  Given a KeyPress or
 *	KeyRelease XEvent, it maps the keycode in the event to a numerical
 *      keysym.
 *
 * Results:
 *	The return value is the keysym corresponding to eventPtr, or NoSymbol
 *	if no matching keysym could be found.
 *
 * Side effects:
 *	In the first call for a given display, calls TkpInitKeymapInfo.
 *
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

    if ((eventPtr->xkey.keycode & MAC_KEYCHAR_MASK) == 0xF8FF) {
	switch (eventPtr->xkey.keycode >> 24) { /* the virtual keyCode */
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
     * Figure out which of the four slots in the keymap vector to use for this
     * key. Refer to Xlib documentation for more info on how this computation
     * works.
     */

    index = 0;
    if (eventPtr->xkey.state & Mod2Mask) { /* Option */
	index |= 2;
    }
    if ((eventPtr->xkey.state & ShiftMask) || /* Shift or caps lock. */
	(eventPtr->xkey.state & LockMask)) {
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
 *	This procedure initializes fields in the display that pertain
 *      to modifier keys.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifier key information in dispPtr is initialized.
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
     * On macOS the caps lock key is always interpreted to mean that alphabetic
     * keys become uppercase but other keys do not get shifted.  (X11 allows
     * a configuration option which makes the caps lock equivalent to holding
     * down the shift key.)
     * There is no offical "Mode_switch" key.
     */

    dispPtr->lockUsage = LU_CAPS;

    /* This field is no longer used by tkBind.c */

    dispPtr->modeModMask = 0;

    /* The Alt and Meta keys are interchanged on Macintosh keyboards compared
     * to PC keyboards.  These fields could be set to make the Alt key on a PC
     * keyboard behave likd an Alt key. That would also require interchanging
     * Mod1Mask and Mod2Mask in tkMacOSXKeyEvent.c.
     */

    dispPtr->altModMask = 0;
    dispPtr->metaModMask = 0;

    /*
     * The modKeyCodes table lists the keycodes that appear in KeyPress or
     * KeyRelease XEvents for modifier keys.  In tkBind.c this table is
     * searched to determine whether an XEvent corresponds to a modifier key.
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
