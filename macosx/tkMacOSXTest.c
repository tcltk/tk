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

#if MAC_OS_X_VERSION_MAX_ALLOWED < 1080
static Tcl_ObjCmdProc DebuggerObjCmd;
#endif
static Tcl_ObjCmdProc PressButtonObjCmd;
static Tcl_ObjCmdProc MoveMouseObjCmd;
static Tcl_ObjCmdProc InjectKeyEventObjCmd;
static Tcl_ObjCmdProc MenuBarHeightObjCmd;


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

#if MAC_OS_X_VERSION_MAX_ALLOWED < 1080
    Tcl_CreateObjCommand(interp, "debugger", DebuggerObjCmd, NULL, NULL);
#endif
    Tcl_CreateObjCommand(interp, "pressbutton", PressButtonObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "movemouse", MoveMouseObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "injectkeyevent", InjectKeyEventObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "menubarheight", MenuBarHeightObjCmd, NULL, NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DebuggerObjCmd --
 *
 *	This procedure simply calls the low level debugger, which was
 *      deprecated in OSX 10.8.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#if MAC_OS_X_VERSION_MAX_ALLOWED < 1080
static int
DebuggerObjCmd(
    TCL_UNUSED(void *),		/* Not used. */
    TCL_UNUSED(Tcl_Interp *),			/* Not used. */
    TCL_UNUSED(int),				/* Not used. */
    TCL_UNUSED(Tcl_Obj *const *))			/* Not used. */
{
    Debugger();
    return TCL_OK;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * MenuBarHeightObjCmd --
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
MenuBarHeightObjCmd(
    TCL_UNUSED(void *),		/* Not used. */
    Tcl_Interp *interp,			/* Not used. */
    TCL_UNUSED(int),				/* Not used. */
    TCL_UNUSED(Tcl_Obj *const *))		/* Not used. */
{
    static int height = 0;
    if (height == 0) {
	height = (int) [[NSApp mainMenu] menuBarHeight];
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(height));
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
    MacDrawable *macWin = (MacDrawable *)drawable;
    NSWindow *win = nil;
    if (macWin->toplevel && macWin->toplevel->winPtr &&
	macWin->toplevel->winPtr->wmInfoPtr &&
	macWin->toplevel->winPtr->wmInfoPtr->window) {
	win = macWin->toplevel->winPtr->wmInfoPtr->window;
    } else if (macWin->winPtr && macWin->winPtr->wmInfoPtr &&
	       macWin->winPtr->wmInfoPtr->window) {
	win = macWin->winPtr->wmInfoPtr->window;
    }
    if (win) {
	return ([win contentView] == [NSView focusView]);
    } else {
	return True;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpReadPixelSRGB --
 *
 *	Read one pixel from a window and convert it from the display's color
 *	profile to sRGB.  macOS composites windows through the display profile,
 *	so a raw read-back differs from the sRGB color values that were drawn;
 *	this returns the drawn values, matching what X11 and Windows read back.
 *	Used only by the "testpixel" test command.
 *
 * Results:
 *	TCL_OK with the sRGB channels in *r, *g, *b, or TCL_ERROR if the pixel
 *	could not be read.
 *
 *----------------------------------------------------------------------
 */

int
TkpReadPixelSRGB(
    Tk_Window tkwin,
    int x,
    int y,
    unsigned char *r,
    unsigned char *g,
    unsigned char *b)
{
    Drawable d = Tk_WindowId(tkwin);
    XImage *image = XGetImage(Tk_Display(tkwin), d, x, y, 1, 1,
	    AllPlanes, ZPixmap);
    NSWindow *win = TkMacOSXGetNSWindowForDrawable(d);
    NSNumber *screenNumber = [[[win screen] deviceDescription]
	    objectForKey:@"NSScreenNumber"];
    CGColorSpaceRef displaySpace = screenNumber ?
	    CGDisplayCopyColorSpace([screenNumber unsignedIntValue]) : NULL;
    CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    unsigned long pixel;
    CGFloat comps[4], cr, cg, cb;
    CGColorRef src, dst;
    const CGFloat *out;

    if (!image || !displaySpace || !srgb) {
	if (image) XDestroyImage(image);
	if (displaySpace) CGColorSpaceRelease(displaySpace);
	if (srgb) CGColorSpaceRelease(srgb);
	return TCL_ERROR;
    }

    /*
     * The captured pixel is in the window's display profile.  Wrap it in a
     * CGColor tagged with that profile and match it to sRGB, the space the
     * drawn color values were specified in.  (macOS XGetImage returns a 32bpp
     * image with R, G, B in bits 16, 8, 0.)
     */

    pixel = XGetPixel(image, 0, 0);
    comps[0] = ((pixel & image->red_mask)   >> 16) / 255.0;
    comps[1] = ((pixel & image->green_mask) >>  8) / 255.0;
    comps[2] =  (pixel & image->blue_mask)         / 255.0;
    comps[3] = 1.0;
    XDestroyImage(image);

    src = CGColorCreate(displaySpace, comps);
    dst = CGColorCreateCopyByMatchingToColorSpace(srgb,
	    kCGRenderingIntentDefault, src, NULL);
    out = dst ? CGColorGetComponents(dst) : NULL;
    if (out) {
	cr = out[0] < 0.0 ? 0.0 : (out[0] > 1.0 ? 1.0 : out[0]);
	cg = out[1] < 0.0 ? 0.0 : (out[1] > 1.0 ? 1.0 : out[1]);
	cb = out[2] < 0.0 ? 0.0 : (out[2] > 1.0 ? 1.0 : out[2]);
	*r = (unsigned char) (cr * 255.0 + 0.5);
	*g = (unsigned char) (cg * 255.0 + 0.5);
	*b = (unsigned char) (cb * 255.0 + 0.5);
    }

    if (src) CGColorRelease(src);
    if (dst) CGColorRelease(dst);
    CGColorSpaceRelease(displaySpace);
    CGColorSpaceRelease(srgb);
    return out ? TCL_OK : TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * PressButtonObjCmd --
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
PressButtonObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    int x = 0, y = 0, i, value;
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
 * MoveMouseObjCmd --
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
MoveMouseObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    int x = 0, y = 0, i, value;
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
InjectKeyEventObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    static const char *const optionStrings[] = {
	"press", "release", "flagschanged", NULL};
    NSUInteger types[3] = {NSKeyDown, NSKeyUp, NSFlagsChanged};
    static const char *const argStrings[] = {
	"-shift", "-control", "-option", "-command", "-function", "-x", "-y", NULL};
    enum args {KEYEVENT_SHIFT, KEYEVENT_CONTROL, KEYEVENT_OPTION, KEYEVENT_COMMAND,
	       KEYEVENT_FUNCTION, KEYEVENT_X, KEYEVENT_Y};
    int i, index, keysym, mods = 0, x = 0, y = 0;
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
