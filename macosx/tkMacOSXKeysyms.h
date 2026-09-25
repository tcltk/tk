/*
 * tkMacOSXKeysyms.h --
 *
 *      Contains data used for processing key events, some of which was
 *      moved from tkMacOSXKeyboard.c.
 *
 * Copyright © 1990-1994 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2006-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2020 Marc Culler
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TKMACOSXKEYSYMS_H
#define TKMACOSXKEYSYMS_H 1

/*
 * This table enumerates the keys on Mac keyboards which do not represent
 * letters.  This is static data -- these keys do not change when the keyboard
 * layout changes.  The unicode representation of a special key which is not a
 * modifier and does not have an ASCII code point lies in the reserved range
 * 0xF700 - 0xF8FF.
 *
 * The table includes every key listed in Apple's documentation of Function-Key
 * Unicodes which is not marked as "Not on most Macintosh keyboards", as well
 * as F20, which is reported to be usable in scripts even though it does not
 * appear on any Macintosh keyboard.
 */

typedef struct {
    int virt;	/* value of [NSEvent keyCode] */
    KeySym keysym;	/* X11 keysym */
    KeyCode keychar;	/* XEvent keycode & 0xFFFF */
} KeyInfo;

static const KeyInfo keyArray[] = {
    {36,	XK_Return,	NSNewlineCharacter},
    {48,	XK_Tab,		NSTabCharacter},
    {51,	XK_BackSpace,	NSDeleteCharacter},
    {52,	XK_Return,	NSNewlineCharacter},  /* Used on some Powerbooks */
    {53,	XK_Escape,	0x1B},
    {54,	XK_Meta_R,      MOD_KEYCHAR},
    {55,	XK_Meta_L,	MOD_KEYCHAR},
    {56,	XK_Shift_L,	MOD_KEYCHAR},
    {57,	XK_Caps_Lock,   MOD_KEYCHAR},
    {58,	XK_Alt_L,	MOD_KEYCHAR},
    {59,	XK_Control_L,	MOD_KEYCHAR},
    {60,	XK_Shift_R,	MOD_KEYCHAR},
    {61,	XK_Alt_R,	MOD_KEYCHAR},
    {62,	XK_Control_R,	MOD_KEYCHAR},
    {63,	XK_Super_L,	MOD_KEYCHAR},
    {64,	XK_F17,		NSF17FunctionKey},
    {65,	XK_KP_Decimal,	'.'},
    {67,	XK_KP_Multiply, '*'},
    {69,	XK_KP_Add,	'+'},
    {71,	XK_Clear,       NSClearLineFunctionKey}, /* Numlock on PC */
    {75,	XK_KP_Divide,   '/'},
    {76,	XK_KP_Enter,	NSEnterCharacter},       /* Fn Return */
    {78,	XK_KP_Subtract, '-'},
    {79,	XK_F18,		NSF18FunctionKey},
    {80,	XK_F19,		NSF19FunctionKey},
    {81,	XK_KP_Equal,	'='},
    {82,	XK_KP_0,	'0'},
    {83,	XK_KP_1,	'1'},
    {84,	XK_KP_2,	'2'},
    {85,	XK_KP_3,	'3'},
    {86,	XK_KP_4,	'4'},
    {87,	XK_KP_5,	'5'},
    {88,	XK_KP_6,	'6'},
    {89,	XK_KP_7,	'7'},
    {90,	XK_F20,		NSF20FunctionKey}, /* For scripting only */
    {91,	XK_KP_8,	'8'},
    {92,	XK_KP_9,	'9'},
    {96,	XK_F5,		NSF5FunctionKey},
    {97,	XK_F6,		NSF6FunctionKey},
    {98,	XK_F7,		NSF7FunctionKey},
    {99,	XK_F3,		NSF3FunctionKey},
    {100,	XK_F8,		NSF8FunctionKey},
    {101,	XK_F9,		NSF9FunctionKey},
    {103,	XK_F11,		NSF11FunctionKey},
    {105,	XK_F13,		NSF13FunctionKey},
    {106,	XK_F16,		NSF16FunctionKey},
    {107,	XK_F14,		NSF14FunctionKey},
    {109,	XK_F10,		NSF10FunctionKey},
    {110,	XK_Menu,	UNKNOWN_KEYCHAR},
    {111,	XK_F12,		NSF12FunctionKey},
    {113,	XK_F15,		NSF15FunctionKey},
    {114,	XK_Help,	NSHelpFunctionKey},
    {115,	XK_Home,	NSHomeFunctionKey},     /* Fn Left */
    {116,	XK_Prior,	NSPageUpFunctionKey},   /* Fn Up */
    {117,	XK_Delete,	NSDeleteFunctionKey},   /* Fn Delete */
    {118,	XK_F4,		NSF4FunctionKey},
    {119,	XK_End,		NSEndFunctionKey},      /* Fn Right */
    {120,	XK_F2,		NSF2FunctionKey},
    {121,	XK_Next,	NSPageDownFunctionKey}, /* Fn Down */
    {122,	XK_F1,		NSF1FunctionKey},
    {123,	XK_Left,	NSLeftArrowFunctionKey},
    {124,	XK_Right,	NSRightArrowFunctionKey},
    {125,	XK_Down,	NSDownArrowFunctionKey},
    {126,	XK_Up,		NSUpArrowFunctionKey},
    {0, 0, 0}
};

/*
 * X11 keysyms for modifier keys, in order.  This list includes keys
 * which do not appear on Apple keyboards, such as Shift_Lock and
 * Super_R.  While most systems don't provide events for the "fn"
 * function key, Apple does.  We map it to Super_L when processing a
 * FlagsChanged NSEvent.
 */

#define NUM_MOD_KEYCODES 14
static const KeyCode modKeyArray[NUM_MOD_KEYCODES] = {
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

#include "tkUnicodeKeysyms.h"

#endif
