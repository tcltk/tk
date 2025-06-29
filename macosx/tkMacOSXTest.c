/*
 * tkMacOSXTest.c --
 *
 *	Contains commands for platform specific tests for
 *	the Macintosh platform.
 *
 * Copyright © 1996 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkMacOSXConstants.h"
#include "tkMacOSXWm.h"

/*
 * Forward declarations of procedures defined later in this file:
 */

static Tcl_ObjCmdProc2 TestpressbuttonObjCmd;
static Tcl_ObjCmdProc2 TestmovemouseObjCmd;
static Tcl_ObjCmdProc2 TestinjectkeyeventObjCmd;
static Tcl_ObjCmdProc2 TestmenubarheightObjCmd;


/*
 *----------------------------------------------------------------------
 *
 * TkplatformtestInit --
 *
 *	Defines commands that test platform specific functionality for
 *	Unix platforms.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Defines new commands.
 *
 *----------------------------------------------------------------------
 */

int
TkplatformtestInit(
    Tcl_Interp *interp)		/* Interpreter to add commands to. */
{
    /*
     * Set a flag indicating that testing is in progress.
     */

    testsAreRunning = 1;

    /*
     * Add commands for platform specific tests on MacOS here.
     */

    Tcl_CreateObjCommand2(interp, "testpressbutton", TestpressbuttonObjCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "testmovemouse", TestmovemouseObjCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "testinjectkeyevent", TestinjectkeyeventObjCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "testmenubarheight", TestmenubarheightObjCmd, NULL, NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TestmenubarheightObjCmd --
 *
 *	This procedure calls [NSMenu menuBarHeight] and returns the result
 *      as an integer.  Windows can never be placed to overlap the MenuBar,
 *      so tests need to be aware of its size.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TestmenubarheightObjCmd(
    TCL_UNUSED(void *),		/* Not used. */
    Tcl_Interp *interp,			/* Not used. */
    TCL_UNUSED(Tcl_Size),				/* Not used. */
    TCL_UNUSED(Tcl_Obj *const *))		/* Not used. */
{
    static int height = 0;
    if (height == 0) {
	height = (int) [[NSApp mainMenu] menuBarHeight];
    }
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(height));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTestLogDisplay --
 *
 *      The test image display procedure calls this to determine whether it
 *      should write a log message recording that it has being run.
 *
 * Results:
 *      Returns true if and only if the NSView of the drawable is the
 *      current focusView, which on 10.14 and newer systems can only be the
 *      case when within [NSView drawRect].
 *      NOTE: This is no longer needed when we use updateLayer instead
 *      of drawRect.  Now it always returns True.
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE Bool
TkTestLogDisplay(
    Drawable drawable)
{
    (void) drawable;
    return True;
}

/*
 *----------------------------------------------------------------------
 *
 * TestpressbuttonObjCmd --
 *
 *	This Tcl command simulates a button press at a specific screen
 *      location.  It injects NSEvents into the NSApplication event queue, as
 *      opposed to adding events to the Tcl queue as event generate would do.
 *      One application is for testing the grab command. These events have
 *      their timestamp property set to 0 as a signal indicating that they
 *      should not be ignored by [NSApp tkProcessMouseEvent].
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TestpressbuttonObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    int x = 0, y = 0, value;
    Tcl_Size i;
    CGPoint pt;
    NSPoint loc;
    NSEvent *motion, *press, *release;
    NSArray *screens = [NSScreen screens];
    CGFloat ScreenHeight = 0;
    enum {X=1, Y};

    if (screens && [screens count]) {
	ScreenHeight = [[screens objectAtIndex:0] frame].size.height;
    }

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "x y");
	return TCL_ERROR;
    }
    for (i = 1; i < objc; i++) {
	if (Tcl_GetIntFromObj(interp,objv[i],&value) != TCL_OK) {
	    return TCL_ERROR;
	}
	switch (i) {
	case X:
	    x = value;
	    break;
	case Y:
	    y = value;
	    break;
	default:
	    break;
	}
    }
    pt.x = loc.x = x;
    pt.y = y;
    loc.y = ScreenHeight - y;

    /*
     *  We set the timestamp to 0 as a signal to tkProcessMouseEvent.
     */

    CGWarpMouseCursorPosition(pt);
    motion = [NSEvent mouseEventWithType:NSMouseMoved
	location:loc
	modifierFlags:0
	timestamp:0
	windowNumber:0
	context:nil
	eventNumber:0
	clickCount:1
	pressure:0];
    [NSApp postEvent:motion atStart:NO];
    press = [NSEvent mouseEventWithType:NSLeftMouseDown
	location:loc
	modifierFlags:0
	timestamp:0
	windowNumber:0
	context:nil
	eventNumber:0
	clickCount:1
	pressure:0];
    [NSApp postEvent:press atStart:NO];
    release = [NSEvent mouseEventWithType:NSLeftMouseUp
	location:loc
	modifierFlags:0
	timestamp:0
	windowNumber:0
	context:nil
	eventNumber:0
	clickCount:1
	pressure:0];
    [NSApp postEvent:release atStart:NO];
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TestmovemouseObjCmd --
 *
 *	This Tcl command simulates a mouse motion to a specific screen
 *      location.  It injects an NSEvent into the NSApplication event queue,
 *      as opposed to adding events to the Tcl queue as event generate would
 *      do.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TestmovemouseObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    int x = 0, y = 0, value;
    Tcl_Size i;
    CGPoint pt;
    NSPoint loc;
    NSEvent *motion;
    NSArray *screens = [NSScreen screens];
    CGFloat ScreenHeight = 0;
    enum {X=1, Y};

    if (screens && [screens count]) {
	ScreenHeight = [[screens objectAtIndex:0] frame].size.height;
    }

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "x y");
	return TCL_ERROR;
    }
    for (i = 1; i < objc; i++) {
	if (Tcl_GetIntFromObj(interp,objv[i],&value) != TCL_OK) {
	    return TCL_ERROR;
	}
	switch (i) {
	case X:
	    x = value;
	    break;
	case Y:
	    y = value;
	    break;
	default:
	    break;
	}
    }
    pt.x = loc.x = x;
    pt.y = y;
    loc.y = ScreenHeight - y;

    /*
     *  We set the timestamp to 0 as a signal to tkProcessMouseEvent.
     */

    CGWarpMouseCursorPosition(pt);
    motion = [NSEvent mouseEventWithType:NSMouseMoved
	location:loc
	modifierFlags:0
	timestamp:0
	windowNumber:0
	context:nil
	eventNumber:0
	clickCount:1
	pressure:0];
    [NSApp postEvent:motion atStart:NO];
    return TCL_OK;
}

static int
TestinjectkeyeventObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    static const char *const optionStrings[] = {
	"flagschanged", "press", "release", NULL};
    NSUInteger types[3] = {NSFlagsChanged, NSKeyDown, NSKeyUp};
    static const char *const argStrings[] = {
	"-command", "-control", "-function", "-option", "-shift", "-x", "-y", NULL};
    enum args {KEYEVENT_COMMAND, KEYEVENT_CONTROL, KEYEVENT_FUNCTION, KEYEVENT_OPTION,
	       KEYEVENT_SHIFT, KEYEVENT_X, KEYEVENT_Y};
    Tcl_Size i;
    int index, keysym, mods = 0, x = 0, y = 0;
    NSString *chars = nil, *unmod = nil, *upper, *lower;
    NSEvent *keyEvent;
    NSUInteger type;
    MacKeycode macKC;

    if (objc < 3) {
    wrongArgs:
	Tcl_WrongNumArgs(interp, 1, objv, "option keysym ?arg?");
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObjStruct(interp, objv[1], optionStrings,
	    sizeof(char *), "option", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }
    type = types[index];
    if (Tcl_GetIntFromObj(interp, objv[2], &keysym) != TCL_OK) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			 "keysym must be an integer"));
	Tcl_SetErrorCode(interp, "TK", "TEST", "INJECT", "KEYSYM", (char *)NULL);
	return TCL_ERROR;
    }
    macKC.uint = XKeysymToKeycode(NULL, keysym);
    for (i = 3; i < objc; i++) {
	if (Tcl_GetIndexFromObjStruct(interp, objv[i], argStrings,
		sizeof(char *), "option", TCL_EXACT, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	switch ((enum args) index) {
	case KEYEVENT_SHIFT:
	    mods |= NSShiftKeyMask;
	    break;
	case KEYEVENT_CONTROL:
	    mods |= NSControlKeyMask;
	    break;
	case KEYEVENT_OPTION:
	    mods |= NSAlternateKeyMask;
	    break;
	case KEYEVENT_COMMAND:
	    mods |= NSCommandKeyMask;
	    break;
	case KEYEVENT_FUNCTION:
	    mods |= NSFunctionKeyMask;
	    break;
	case KEYEVENT_X:
	    if (++i >= objc) {
		goto wrongArgs;
	    }
	    if (Tcl_GetIntFromObj(interp,objv[i], &x) != TCL_OK) {
		return TCL_ERROR;
	    }
	    break;
	case KEYEVENT_Y:
	    if (++i >= objc) {
		goto wrongArgs;
	    }
	    if (Tcl_GetIntFromObj(interp,objv[i], &y) != TCL_OK) {
		return TCL_ERROR;
	    }
	    break;
	}
    }
    if (type != NSFlagsChanged) {
	UniChar keychar = macKC.v.keychar;
	chars = [[NSString alloc] initWithCharacters: &keychar length:1];
	upper = [chars uppercaseString];
	lower = [chars lowercaseString];
	if (![upper isEqual: lower] && [chars isEqual: upper]) {
	    mods |= NSShiftKeyMask;
	}
	if (mods & NSShiftKeyMask) {
	    chars = upper;
	    unmod = lower;
	    macKC.v.o_s |= INDEX_SHIFT;
	} else {
	    unmod = chars;
	}
	if (macKC.v.o_s & INDEX_OPTION) {
	    mods |= NSAlternateKeyMask;
	}
    }
    keyEvent = [NSEvent keyEventWithType:type
	location:NSMakePoint(x, y)
	modifierFlags:mods
	timestamp:GetCurrentEventTime()
	windowNumber:0
	context:nil
	characters:chars
	charactersIgnoringModifiers:unmod
	isARepeat:NO
	keyCode:macKC.v.virt];
    [NSApp postEvent:keyEvent atStart:NO];
    return TCL_OK;
}
/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
