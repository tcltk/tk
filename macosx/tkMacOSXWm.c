/*
 * tkMacOSXWm.c --
 *
 *	This module takes care of the interactions between a Tk-based
 *	application and the window manager. Among other things, it implements
 *	the "wm" command and passes geometry information to the window manager.
 *
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2006-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2010 Kevin Walzer/WordTech Communications LLC.
 * Copyright © 2017-2019 Marc Culler.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkScrollbar.h"
#include "tkMacOSXWm.h"
#include "tkMacOSXInt.h"
#include "tkMacOSXDebug.h"
#include "tkMacOSXConstants.h"

/*
 * Setting this to 1 prints when each window is freed, setting it to 2 adds
 * dumps of the autorelease pools, and setting it to 3 also shows each retain
 * and release.
 */

#define DEBUG_ZOMBIES 0

/*
#ifdef TK_MAC_DEBUG
#define TK_MAC_DEBUG_WINDOWS
#endif
*/

/*
 * Carbon window attributes and classes.
 */

#define WM_NSMASK_SHIFT 36
#define tkWindowDoesNotHideAttribute \
	((UInt64) 1 << kHIWindowBitDoesNotHide)
#define tkCanJoinAllSpacesAttribute \
	((UInt64) NSWindowCollectionBehaviorCanJoinAllSpaces << 34)
#define tkMoveToActiveSpaceAttribute \
	((UInt64) NSWindowCollectionBehaviorMoveToActiveSpace << 34)
#define tkNonactivatingPanelAttribute \
	((UInt64) NSNonactivatingPanelMask << WM_NSMASK_SHIFT)
#define tkHUDWindowAttribute \
	((UInt64) NSHUDWindowMask << WM_NSMASK_SHIFT)
#define tkAlwaysValidAttributes (kWindowNoUpdatesAttribute \
	| kWindowNoActivatesAttribute	    | kWindowHideOnSuspendAttribute \
	| kWindowHideOnFullScreenAttribute  | kWindowNoConstrainAttribute \
	| kWindowNoShadowAttribute	    | kWindowLiveResizeAttribute \
	| kWindowOpaqueForEventsAttribute   | kWindowIgnoreClicksAttribute \
	| kWindowDoesNotCycleAttribute	    | tkWindowDoesNotHideAttribute \
	| tkCanJoinAllSpacesAttribute	    | tkMoveToActiveSpaceAttribute \
	| tkNonactivatingPanelAttribute	    | tkHUDWindowAttribute)

static const struct {
    const UInt64 validAttrs, defaultAttrs, forceOnAttrs, forceOffAttrs;
    int flags; NSUInteger styleMask;
} macClassAttrs[] = {
    [kAlertWindowClass] = {
	.defaultAttrs = kWindowDoesNotCycleAttribute, },
    [kMovableAlertWindowClass] = {
	.defaultAttrs = kWindowDoesNotCycleAttribute, },
    [kModalWindowClass] = {
	.defaultAttrs = kWindowDoesNotCycleAttribute, },
    [kMovableModalWindowClass] = {
	.validAttrs = kWindowCloseBoxAttribute | kWindowMetalAttribute |
		kWindowFullZoomAttribute | kWindowResizableAttribute,
	.defaultAttrs = kWindowDoesNotCycleAttribute, },
    [kFloatingWindowClass] = {
	.validAttrs = kWindowCloseBoxAttribute | kWindowCollapseBoxAttribute |
		kWindowMetalAttribute | kWindowToolbarButtonAttribute |
		kWindowNoTitleBarAttribute | kWindowFullZoomAttribute |
		kWindowResizableAttribute | kWindowSideTitlebarAttribute,
	.defaultAttrs = kWindowStandardFloatingAttributes |
		kWindowHideOnSuspendAttribute | kWindowDoesNotCycleAttribute,
	.forceOnAttrs = kWindowResizableAttribute,
	.forceOffAttrs = kWindowCollapseBoxAttribute,
	.styleMask = NSUtilityWindowMask, },
    [kDocumentWindowClass] = {
	.validAttrs = kWindowCloseBoxAttribute | kWindowCollapseBoxAttribute |
		kWindowMetalAttribute | kWindowToolbarButtonAttribute |
		kWindowNoTitleBarAttribute |
		kWindowUnifiedTitleAndToolbarAttribute |
		kWindowInWindowMenuAttribute | kWindowFullZoomAttribute |
		kWindowResizableAttribute,
	.forceOnAttrs = kWindowResizableAttribute,
	.defaultAttrs = kWindowStandardDocumentAttributes |
		kWindowLiveResizeAttribute | kWindowInWindowMenuAttribute, },
    [kUtilityWindowClass] = {
	.validAttrs = kWindowCloseBoxAttribute | kWindowCollapseBoxAttribute |
		kWindowMetalAttribute | kWindowToolbarButtonAttribute |
		kWindowNoTitleBarAttribute | kWindowFullZoomAttribute |
		kWindowResizableAttribute | kWindowSideTitlebarAttribute,
	.defaultAttrs = kWindowStandardFloatingAttributes |
		kWindowHideOnFullScreenAttribute |
		tkWindowDoesNotHideAttribute | tkNonactivatingPanelAttribute |
		kWindowDoesNotCycleAttribute,
	.forceOnAttrs = kWindowResizableAttribute,
	.forceOffAttrs = kWindowCollapseBoxAttribute,
	.flags = WM_TOPMOST,
	.styleMask = NSUtilityWindowMask, },
    [kHelpWindowClass] = {
	.defaultAttrs = kWindowHideOnSuspendAttribute,
	.forceOnAttrs = kWindowNoTitleBarAttribute |
		kWindowDoesNotCycleAttribute,
	.flags = WM_TOPMOST,
	.styleMask = 0},
    [kSheetWindowClass] = {
	.validAttrs = kWindowResizableAttribute,
	.forceOnAttrs = kWindowNoTitleBarAttribute |
		kWindowDoesNotCycleAttribute,
	.styleMask = NSDocModalWindowMask, },
    [kToolbarWindowClass] = {
	.defaultAttrs = kWindowHideOnSuspendAttribute,
	.forceOnAttrs = kWindowNoTitleBarAttribute |
		kWindowDoesNotCycleAttribute,
	.styleMask = NSUtilityWindowMask, },
    [kPlainWindowClass] = {
	.defaultAttrs = kWindowDoesNotCycleAttribute,
	.forceOnAttrs = kWindowNoTitleBarAttribute, },
    [kOverlayWindowClass] = {
	.forceOnAttrs = kWindowNoTitleBarAttribute |
		kWindowDoesNotCycleAttribute,
	.flags = WM_TOPMOST | WM_TRANSPARENT, },
    [kSheetAlertWindowClass] = {
	.forceOnAttrs = kWindowNoTitleBarAttribute |
		kWindowDoesNotCycleAttribute,
	.styleMask = NSDocModalWindowMask, },
    [kAltPlainWindowClass] = {
	.defaultAttrs = kWindowDoesNotCycleAttribute,
	.forceOnAttrs = kWindowNoTitleBarAttribute, },
    [kSimpleWindowClass] = {
	.defaultAttrs = kWindowDoesNotCycleAttribute,
	.forceOnAttrs = kWindowNoTitleBarAttribute, },
    [kDrawerWindowClass] = {
	.validAttrs = kWindowMetalAttribute | kWindowResizableAttribute,
	.forceOnAttrs = kWindowNoTitleBarAttribute |
		kWindowDoesNotCycleAttribute, },
};

#define ForceAttributes(attributes, class) \
	((attributes) & (~macClassAttrs[(class)].forceOffAttrs | \
	(macClassAttrs[(class)].forceOnAttrs & ~kWindowResizableAttribute)))

/*
 * Structures and data for the wm attributes command (macOS 10.13 and later):
 */

/* Hash tables for attributes which can be set before a window exists */
static Tcl_HashTable pathnameToSubclass;
static Tcl_HashTable pathnameToTabbingId;
static Tcl_HashTable pathnameToTabbingMode;

enum NSWindowSubclass {
    subclassNSWindow = 0,
    subclassNSPanel = 1
};
/* This array must be indexed by the enum above.*/
static const char *subclassNames[] = {"nswindow", "nspanel", NULL};

typedef struct buttonField_t {
    unsigned zoom: 1;
    unsigned miniaturize: 1;
    unsigned close: 1;
} buttonField;

/* The order of these names must match the order of the bits above! */
static const char *buttonNames[] = {"zoom", "miniaturize", "close", NULL};

typedef union windowButtonState_t {
    int intvalue;
    buttonField bits;
} windowButtonState;

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101300
enum NSWindowClass {
    NSWindowClass_any = 0,
    NSWindowClass_window = 1,
    NSWindowClass_panel = 2
};
typedef struct styleMaskBit_t {
    const char *bitname;
    unsigned long bitvalue;
    enum NSWindowClass allowed;
} styleMaskBit;

static const styleMaskBit styleMaskBits[] = {
    /* Make the titlebar visible and use round corners. */
    {"titled", NSWindowStyleMaskTitled, NSWindowClass_window},
    /* Enable the close button. */
    {"closable", NSWindowStyleMaskClosable, NSWindowClass_window},
    /* Enable the miniaturize button. */
    {"miniaturizable", NSWindowStyleMaskMiniaturizable, NSWindowClass_window},
    /* Allow the user to resize the window. */
    {"resizable", NSWindowStyleMaskResizable, NSWindowClass_window},
    /*
     * Make the content view extend under the titlebar.  We force
     * titlebarAppearsTransparent when this bit is set.  Otherwise it is
     * pretty useless.
     */
    {"fullsizecontentview", NSWindowStyleMaskFullSizeContentView, NSWindowClass_window},
    /* Rounded corners, cannot have a titlebar (overrides titled bit). */
    {"docmodal", NSWindowStyleMaskDocModalWindow, NSWindowClass_any},
    /* ============================================
     * The following bits are only valid for panels.
     */
    /* Make the title bar thinner. */
    {"utility", NSWindowStyleMaskUtilityWindow, NSWindowClass_panel},
    /* Do not activate the app when the window is activated. */
    {"nonactivatingpanel", NSWindowStyleMaskNonactivatingPanel, NSWindowClass_panel},
    /*
     * Requires utility.  Cannot be resizable.  Close button is an X; no other buttons.
     * Cannot be a docmodal.
     */
    {"HUDwindow", NSWindowStyleMaskHUDWindow, NSWindowClass_panel},
    {NULL, 0, NSWindowClass_any}
};

typedef struct tabbingMode_t {
    const char *modeName;
    long modeValue;
} tabbingMode;

static const tabbingMode tabbingModes[] = {
    {"auto",  NSWindowTabbingModeAutomatic},
    {"disallowed", NSWindowTabbingModeDisallowed},
    {"preferred", NSWindowTabbingModePreferred},
    {NULL, -1}
};

static const char *const appearanceStrings[] = {
    "aqua", "auto", "darkaqua", NULL
};
enum appearances {
    APPEARANCE_AQUA, APPEARANCE_AUTO, APPEARANCE_DARKAQUA
};

static Bool wantsToBeTab(NSWindow *macWindow) {
    Bool result;
    switch ([macWindow tabbingMode]) {
    case NSWindowTabbingModeDisallowed:
	result = False;
	break;
    case NSWindowTabbingModePreferred:
	result = True;
	break;
    case NSWindowTabbingModeAutomatic:
	result = ([NSWindow userTabbingPreference] ==
		NSWindowUserTabbingPreferenceAlways);
	break;
    default:
	result = False;
	break;
    }
    return result;
}

/*
 * Helper for the tkLayoutChanged methods.  Synchronizes Tk's understanding of
 * the bounds of a contentView with the window's.  It is needed because there
 * are situations when the window manager can change the layout of an NSWindow
 * without having been requested to do so by Tk.  Examples are when a window
 * goes FullScreen or shows a tab bar.  NSWindow methods which involve such
 * layout changes should be overridden or protected by methods which call this.
 */

static void syncLayout(NSWindow *macWindow)
{
    TkWindow *winPtr = TkMacOSXGetTkWindow(macWindow);

    if (winPtr) {
	// Using screen coordinates with origin at bottom left.
	NSRect frameRect = [macWindow frame];
	// This accounts for the tab bar, if there is one.
	NSRect contentRect = [macWindow contentRectForFrameRect: frameRect];
	WmInfo *wmPtr = winPtr->wmInfoPtr;

	// The parent includes the title bar, tab bar and window frame.
	wmPtr->xInParent = frameRect.origin.x - contentRect.origin.x;
	wmPtr->yInParent = (frameRect.origin.y + frameRect.size.height -
	    contentRect.origin.y - contentRect.size.height);
	wmPtr->parentWidth = winPtr->changes.width + frameRect.size.width -
	    contentRect.size.width;
	wmPtr->parentHeight = winPtr->changes.height + frameRect.size.height -
	    contentRect.size.height;
	TkMacOSXInvalClipRgns((Tk_Window)winPtr);
    }
}
#endif

typedef enum {
    WMATT_ALPHA, WMATT_APPEARANCE, WMATT_BUTTONS, WMATT_FULLSCREEN,
    WMATT_ISDARK, WMATT_MODIFIED, WMATT_NOTIFY, WMATT_TITLEPATH, WMATT_TOPMOST,
    WMATT_TRANSPARENT, WMATT_STYLEMASK, WMATT_CLASS, WMATT_TABBINGID,
    WMATT_TABBINGMODE, WMATT_TYPE, _WMATT_LAST_ATTRIBUTE
} WmAttribute;

static const char *const WmAttributeNames[] = {
    "-alpha", "-appearance", "-buttons", "-fullscreen", "-isdark", "-modified",
    "-notify", "-titlepath", "-topmost", "-transparent", "-stylemask", "-class",
    "-tabbingid", "-tabbingmode", "-type", NULL
};

/*
 * The variable below is used to enable or disable tracing in this module. If
 * tracing is enabled, then information is printed on standard output about
 * interesting interactions with the window manager.
 */

static int wmTracing = 0;

/*
 * The following structure is the official type record for geometry management
 * of top-level windows.
 */

static void TopLevelReqProc(void *dummy, Tk_Window tkwin);

static const Tk_GeomMgr wmMgrType = {
    "wm",			/* name */
    TopLevelReqProc,		/* requestProc */
    NULL,			/* lostContentProc */
};

/*
 * The following keeps state for Aqua dock icon bounce notification.
 */

static int tkMacOSXWmAttrNotifyVal = 0;

/*
 * Declarations of static functions defined in this file:
 */

static NSRect		InitialWindowBounds(TkWindow *winPtr,
			    NSWindow *macWindow);
static int		ParseGeometry(Tcl_Interp *interp, char *string,
			    TkWindow *winPtr);
static void		TopLevelEventProc(void *clientData,
			    XEvent *eventPtr);
static void		WmStackorderToplevelWrapperMap(TkWindow *winPtr,
			    Display *display, Tcl_HashTable *table);
static void		UpdateGeometryInfo(void *clientData);
static void		UpdateSizeHints(TkWindow *winPtr);
static void		UpdateVRootGeometry(WmInfo *wmPtr);
static int		WmAspectCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmAttributesCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmClientCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmColormapwindowsCmd(Tk_Window tkwin,
			    TkWindow *winPtr, Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmCommandCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmDeiconifyCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmFocusmodelCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmForgetCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmFrameCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmGeometryCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmGridCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmGroupCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmIconbadgeCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmIconbitmapCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmIconifyCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmIconmaskCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmIconnameCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmIconphotoCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmIconpositionCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmIconwindowCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmManageCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmMaxsizeCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmMinsizeCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmOverrideredirectCmd(Tk_Window tkwin,
			    TkWindow *winPtr, Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmPositionfromCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmProtocolCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmResizableCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmSizefromCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmStackorderCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmStateCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmTitleCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmTransientCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		WmWithdrawCmd(Tk_Window tkwin, TkWindow *winPtr,
			    Tcl_Interp *interp, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static void		WmUpdateGeom(WmInfo *wmPtr, TkWindow *winPtr);
static int		WmWinStyle(Tcl_Interp *interp, TkWindow *winPtr,
			    Tcl_Size objc, Tcl_Obj *const objv[]);
static int		WmWinAppearance(Tcl_Interp *interp, TkWindow *winPtr,
			    Tcl_Size objc, Tcl_Obj *const objv[]);
static void		ApplyWindowAttributeFlagChanges(TkWindow *winPtr,
			    NSWindow *macWindow, UInt64 oldAttributes,
			    int oldFlags, int create, int initial);
static void		ApplyContainerOverrideChanges(TkWindow *winPtr,
			    NSWindow *macWindow);
static void		GetMinSize(TkWindow *winPtr, int *minWidthPtr,
			    int *minHeightPtr);
static void		GetMaxSize(TkWindow *winPtr, int *maxWidthPtr,
			    int *maxHeightPtr);
static void		RemapWindows(TkWindow *winPtr,
			    MacDrawable *parentWin);
static void             RemoveTransient(TkWindow *winPtr);

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101300

/*
 * Add a window as a tab in the group specified by its tabbingid, or
 * make it a standalone window if it is the only window with that
 * tabbingid.  Adjust the window size if a tab bar appeared or
 * disappeared.
 */

static void placeAsTab(TKWindow *macWindow) {
    TkWindow *winPtr = NULL, *winPtr2 = NULL;
    TKWindow *target = NULL, *sibling = NULL;
    NSString *identifier = [macWindow tabbingIdentifier];
    if (!wantsToBeTab(macWindow)) {
	[macWindow moveTabToNewWindow:NSApp];
	[(TKWindow *)target tkLayoutChanged];
	return;
    }
    for (NSWindow *window in [NSApp windows]) {
	if (window == macWindow) {
	    continue;
	}
	if ([identifier isEqualTo: [window tabbingIdentifier]] &&
	    wantsToBeTab(window)) {
	    target = (TKWindow*) window;
	    syncLayout(target);
	    break;
	}
    }
    syncLayout(macWindow);
    NSArray<NSWindow *> *tabs = [macWindow tabbedWindows];
    if ([tabs count] == 2) {
	sibling = tabs[0] == macWindow ? (TKWindow *)tabs[1] : (TKWindow *)tabs[0];
	syncLayout(sibling);
	winPtr2 = TkMacOSXGetTkWindow(sibling);
    }
    if (target) {
	CGFloat winHeight = [macWindow contentRectForFrameRect:
				[macWindow frame]].size.height;
	CGFloat winDelta = 0, targetHeight, targetDelta = 0;
	targetHeight =  [target contentRectForFrameRect:
			    [target frame]].size.height;
	[target addTabbedWindow:macWindow ordered:NSWindowAbove];
	targetDelta = targetHeight - [target contentRectForFrameRect:
					 [target frame]].size.height;
	winDelta = winHeight - [target contentRectForFrameRect:
				   [target frame]].size.height;
	if (winDelta) {
	    winPtr = TkMacOSXGetTkWindow(macWindow);
	    XMoveResizeWindow(winPtr->display, winPtr->window,
			      winPtr->changes.x, winPtr->changes.y,
			      winPtr->changes.width, winPtr->changes.height + winDelta );
	    if (sibling) {
		winPtr = TkMacOSXGetTkWindow(sibling);
		XMoveResizeWindow(winPtr->display, winPtr->window,
				  winPtr->changes.x, winPtr->changes.y,
				  winPtr->changes.width, winPtr->changes.height - winDelta );
	    }
	}
	if (targetDelta) {
	    winPtr = TkMacOSXGetTkWindow(target);
	    XMoveResizeWindow(winPtr->display, winPtr->window,
			      winPtr->changes.x, winPtr->changes.y,
			      winPtr->changes.width, winPtr->changes.height + targetDelta );
	}
    } else {
	CGFloat height = [macWindow contentRectForFrameRect:
			     [macWindow frame]].size.height;
	[macWindow moveTabToNewWindow:NSApp];
	CGFloat delta = height - [macWindow contentRectForFrameRect:
			    [macWindow frame]].size.height;
	winPtr = TkMacOSXGetTkWindow(macWindow);
	XMoveResizeWindow(winPtr->display, winPtr->window,
			  winPtr->changes.x, winPtr->changes.y,
			  winPtr->changes.width, winPtr->changes.height + delta);
	if (winPtr2) {
	    XMoveResizeWindow(winPtr2->display, winPtr2->window,
			      winPtr2->changes.x, winPtr2->changes.y,
			      winPtr2->changes.width, winPtr2->changes.height + delta );
	}
    }
}
#endif

#pragma mark NSWindow(TKWm)

@implementation NSWindow(TKWm)

- (NSPoint) tkConvertPointToScreen: (NSPoint) point
{
    NSRect pointrect = {point, {0,0}};
    return [self convertRectToScreen:pointrect].origin;
}

- (NSPoint) tkConvertPointFromScreen: (NSPoint)point
{
    NSRect pointrect = {point, {0,0}};
    return [self convertRectFromScreen:pointrect].origin;
}
@end

#pragma mark -

@implementation TKPanel: NSPanel
@synthesize tkWindow = _tkWindow;

- (void) tkLayoutChanged
{
    syncLayout(self);
}

@end

@implementation TKDrawerWindow: NSWindow
@synthesize tkWindow = _tkWindow;
@end

@implementation TKWindow: NSWindow
@synthesize tkWindow = _tkWindow;
@end

#pragma mark TKWindow(TKWm)

@implementation TKWindow(TKWm)

- (void) tkLayoutChanged
{
    syncLayout(self);
}

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101300
- (void)toggleTabBar:(id)sender
{
    TkWindow *winPtr = TkMacOSXGetTkWindow(self);
    if (!winPtr) {
	return;
    }
    [super toggleTabBar:sender];
    [self tkLayoutChanged];
}
#endif

- (NSSize)windowWillResize:(NSWindow *)sender
		    toSize:(NSSize)frameSize
{
    NSRect currentFrame = [sender frame];
    TkWindow *winPtr = TkMacOSXGetTkWindow(sender);
    if (winPtr) {
	if (winPtr->wmInfoPtr->flags & WM_WIDTH_NOT_RESIZABLE) {
	    frameSize.width = currentFrame.size.width;
	}
	if (winPtr->wmInfoPtr->flags & WM_HEIGHT_NOT_RESIZABLE) {
	    frameSize.height = currentFrame.size.height;
	}
    }
    return frameSize;
}

- (BOOL) canBecomeKeyWindow
{
    TkWindow *winPtr = TkMacOSXGetTkWindow(self);

    if (!winPtr || !winPtr->wmInfoPtr) {
	return NO;
    }
    return (winPtr->wmInfoPtr &&
	    (winPtr->wmInfoPtr->macClass == kHelpWindowClass ||
	     winPtr->wmInfoPtr->attributes & kWindowNoActivatesAttribute)
	    ) ? NO : YES;
}

#if DEBUG_ZOMBIES
- (id) retain
{
    id result = [super retain];
    const char *title = [[self title] UTF8String];
    if (title == nil) {
	title = "unnamed window";
    }
    if (DEBUG_ZOMBIES > 2) {
	fprintf(stderr, "Retained <%s>. Count is: %lu\n",
		title, [self retainCount]);
    }
    return result;
}

- (id) autorelease
{
    id result = [super autorelease];
    const char *title = [[self title] UTF8String];
    if (title == nil) {
	title = "unnamed window";
    }
    if (DEBUG_ZOMBIES > 2) {
	fprintf(stderr, "Autoreleased <%s>. Count is %lu\n",
		title, [self retainCount]);
    }
    return result;
}

- (oneway void) release {
    const char *title = [[self title] UTF8String];
    if (title == nil) {
	title = "unnamed window";
    }
    if (DEBUG_ZOMBIES > 2) {
	fprintf(stderr, "Releasing <%s>. Count is %lu\n",
		title, [self retainCount]);
    }
    [super release];
}

- (void) dealloc {
    const char *title = [[self title] UTF8String];
    if (title == nil) {
	title = "unnamed window";
    }
    if (DEBUG_ZOMBIES > 0) {
	fprintf(stderr, ">>>> Freeing <%s>. Count is %lu\n",
		title, [self retainCount]);
    }
    [super dealloc];
}

#endif
@end

#pragma mark -

/*
 *----------------------------------------------------------------------
 *
 * SetWindowSizeLimits --
 *
 *	Sets NSWindow size limits
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
SetWindowSizeLimits(
    TkWindow *winPtr)
{
    NSWindow *macWindow = TkMacOSXGetNSWindowForDrawable(winPtr->window);
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int minWidth, minHeight, maxWidth, maxHeight, base;

    if (!macWindow) {
	return;
    }
    GetMinSize(winPtr, &minWidth, &minHeight);
    GetMaxSize(winPtr, &maxWidth, &maxHeight);
    if (wmPtr->gridWin) {
	base = winPtr->reqWidth - (wmPtr->reqGridWidth * wmPtr->widthInc);
	if (base < 0) {
	    base = 0;
	}
	minWidth = base + (minWidth * wmPtr->widthInc);
	maxWidth = base + (maxWidth * wmPtr->widthInc);
	base = winPtr->reqHeight - (wmPtr->reqGridHeight * wmPtr->heightInc);
	if (base < 0) {
	    base = 0;
	}
	minHeight = base + (minHeight * wmPtr->heightInc);
	maxHeight = base + (maxHeight * wmPtr->heightInc);
    }
    if (wmPtr->flags & WM_WIDTH_NOT_RESIZABLE) {
	minWidth = maxWidth = wmPtr->configWidth;
    }
    if (wmPtr->flags & WM_HEIGHT_NOT_RESIZABLE) {
	minHeight = maxHeight = wmPtr->configHeight;
    }
    if (wmPtr->gridWin) {
	[macWindow setResizeIncrements:NSMakeSize(wmPtr->widthInc,
		wmPtr->heightInc)];
    } else if (wmPtr->sizeHintsFlags & PAspect && wmPtr->minAspect.x ==
	    wmPtr->maxAspect.x && wmPtr->minAspect.y == wmPtr->maxAspect.y) {
	NSSize aspect = NSMakeSize(wmPtr->minAspect.x, wmPtr->minAspect.y);
	CGFloat ratio = aspect.width/aspect.height;

	[macWindow setContentAspectRatio:aspect];
	if ((CGFloat)minWidth/(CGFloat)minHeight > ratio) {
	    minHeight = lround(minWidth / ratio);
	} else {
	    minWidth = lround(minHeight * ratio);
	}
	if ((CGFloat)maxWidth/(CGFloat)maxHeight > ratio) {
	    maxWidth = lround(maxHeight * ratio);
	} else {
	    maxHeight = lround(maxWidth / ratio);
	}
	if ((CGFloat)wmPtr->configWidth/(CGFloat)wmPtr->configHeight > ratio) {
	    wmPtr->configWidth = lround(wmPtr->configHeight * ratio);
	    if (wmPtr->configWidth < minWidth) {
		wmPtr->configWidth = minWidth;
		wmPtr->configHeight = minHeight;
	    }
	} else {
	    wmPtr->configHeight = lround(wmPtr->configWidth / ratio);
	    if (wmPtr->configHeight < minHeight) {
		wmPtr->configWidth = minWidth;
		wmPtr->configHeight = minHeight;
	    }
	}
    } else {
	[macWindow setResizeIncrements:NSMakeSize(1.0, 1.0)];
    }
    [macWindow setContentMinSize:NSMakeSize(minWidth, minHeight)];
    [macWindow setContentMaxSize:NSMakeSize(maxWidth, maxHeight)];
}

/*
 *----------------------------------------------------------------------
 *
 * FrontWindowAtPoint --
 *
 *	Find frontmost toplevel window at a given screen location which has the
 *      specified mainPtr.  If the location is in the title bar, return NULL.
 *
 * Results:
 *	TkWindow*.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkWindow*
FrontWindowAtPoint(
    int x,
    int y)
{
    NSPoint p = NSMakePoint(x, TkMacOSXZeroScreenHeight() - y);

    for (NSWindow *w in [NSApp orderedWindows]) {
	TkWindow *winPtr = TkMacOSXGetTkWindow(w);
	if (winPtr) {
	    NSRect windowFrame = [w frame];
	    NSRect contentFrame = windowFrame;

	    /*
	     * For consistency with other platforms, points in the
	     * title bar are not considered to be contained in the
	     * window.
	     */

	    contentFrame.size.height = [[w contentView] frame].size.height;
	    if (NSMouseInRect(p, contentFrame, NO)) {
		return winPtr;
	    } else if (NSMouseInRect(p, windowFrame, NO)) {
		/*
		 * The pointer is in the title bar of the highest NSWindow
		 * containing it, and therefore it should not be considered
		 * to be contained in any Tk window.
		 */
		return NULL;
	    }
	}
    }
    return NULL;
}

void TkMacOSXAssignNewKeyWindow(
    Tcl_Interp *interp,
    NSWindow *ignore)
{
    TkWindow *winPtr;

    /*
     * Avoid bug 5692042764: set tkEventTarget to NULL if there is no window to
     * send Tk events to.
     */

    [NSApp setTkEventTarget: NULL];
    for (NSWindow *w in [NSApp orderedWindows]) {
	WmInfo *wmPtr;
	BOOL isOnScreen;
	winPtr = TkMacOSXGetTkWindow(w);
	if (!winPtr
	    || !winPtr->wmInfoPtr
	    || (winPtr->flags & TK_ALREADY_DEAD)) {
	    continue;
	}
	if (interp && interp != Tk_Interp((Tk_Window) winPtr)) {
	    continue;
	}
	wmPtr = winPtr->wmInfoPtr;
	isOnScreen = (wmPtr->hints.initial_state != IconicState &&
		      wmPtr->hints.initial_state != WithdrawnState);
	if (w != ignore && isOnScreen && [w canBecomeKeyWindow]) {
	    TKMenu *menu;
	    [w makeKeyAndOrderFront:NSApp];
	    /* Set the menubar for the new front window. */
	    if (winPtr->wmInfoPtr &&
		winPtr->wmInfoPtr->menuPtr &&
		winPtr->wmInfoPtr->menuPtr->mainMenuPtr) {
		menu = (TKMenu *) winPtr->wmInfoPtr->menuPtr->platformData;
		[NSApp tkSetMainMenu:menu];
		[NSApp setTkEventTarget: winPtr];
	    }
	    break;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmNewWindow --
 *
 *	This procedure is invoked whenever a new top-level window is created.
 *	Its job is to initialize the WmInfo structure for the window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A WmInfo structure gets allocated and initialized.
 *
 *----------------------------------------------------------------------
 */

void
TkWmNewWindow(
    TkWindow *winPtr)		/* Newly-created top-level window. */
{
    WmInfo *wmPtr = (WmInfo *)ckalloc(sizeof(WmInfo));

    wmPtr->winPtr = winPtr;
    wmPtr->reparent = None;
    wmPtr->titleUid = NULL;
    wmPtr->iconName = NULL;
    wmPtr->container = NULL;
    wmPtr->hints.flags = InputHint | StateHint;
    wmPtr->hints.input = True;
    wmPtr->hints.initial_state = NormalState;
    wmPtr->hints.icon_pixmap = None;
    wmPtr->hints.icon_window = None;
    wmPtr->hints.icon_x = wmPtr->hints.icon_y = 0;
    wmPtr->hints.icon_mask = None;
    wmPtr->hints.window_group = None;
    wmPtr->leaderName = NULL;
    wmPtr->icon = NULL;
    wmPtr->iconFor = NULL;
    wmPtr->transientPtr = NULL;
    wmPtr->sizeHintsFlags = 0;
    wmPtr->minWidth = wmPtr->minHeight = 1;
    wmPtr->maxWidth = 0;
    wmPtr->maxHeight = 0;
    wmPtr->gridWin = NULL;
    wmPtr->widthInc = wmPtr->heightInc = 1;
    wmPtr->minAspect.x = wmPtr->minAspect.y = 1;
    wmPtr->maxAspect.x = wmPtr->maxAspect.y = 1;
    wmPtr->reqGridWidth = wmPtr->reqGridHeight = -1;
    wmPtr->gravity = NorthWestGravity;
    wmPtr->width = -1;
    wmPtr->height = -1;
    wmPtr->x = winPtr->changes.x;
    wmPtr->y = winPtr->changes.y;
    wmPtr->parentWidth = winPtr->changes.width
	    + 2*winPtr->changes.border_width;
    wmPtr->parentHeight = winPtr->changes.height
	    + 2*winPtr->changes.border_width;
    wmPtr->xInParent = 0;
    wmPtr->yInParent = 0;
    wmPtr->cmapList = NULL;
    wmPtr->cmapCount = 0;
    wmPtr->configX = 0;
    wmPtr->configY = 0;
    wmPtr->configWidth = -1;
    wmPtr->configHeight = -1;
    wmPtr->vRoot = None;
    wmPtr->protPtr = NULL;
    wmPtr->commandObj = NULL;
    wmPtr->clientMachine = NULL;
    wmPtr->flags = WM_NEVER_MAPPED;
    wmPtr->macClass = kDocumentWindowClass;
    wmPtr->attributes = macClassAttrs[kDocumentWindowClass].defaultAttrs;
    wmPtr->scrollWinPtr = NULL;
    wmPtr->menuPtr = NULL;
    wmPtr->window = nil;
    winPtr->wmInfoPtr = wmPtr;

    // initialize wmPtr->NSWindowSubclass here

    UpdateVRootGeometry(wmPtr);

    /*
     * Tk must monitor structure events for top-level windows, in order to
     * detect size and position changes caused by window managers.
     */

    Tk_CreateEventHandler((Tk_Window)winPtr, StructureNotifyMask,
	    TopLevelEventProc, winPtr);

    /*
     * Arrange for geometry requests to be reflected from the window to the
     * window manager.
     */

    Tk_ManageGeometry((Tk_Window)winPtr, &wmMgrType, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmMapWindow --
 *
 *	This procedure is invoked to map a top-level window. This module gets
 *	a chance to update all window-manager-related information in
 *	properties before the window manager sees the map event and checks the
 *	properties. It also gets to decide whether or not to even map the
 *	window after all.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Properties of winPtr may get updated to provide up-to-date information
 *	to the window manager. The window may also get mapped, but it may not
 *	be if this procedure decides that isn't appropriate (e.g. because the
 *	window is withdrawn).
 *
 *----------------------------------------------------------------------
 */

void
TkWmMapWindow(
    TkWindow *winPtr)		/* Top-level window that's about to be
				 * mapped. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    XEvent event;

    if (wmPtr->flags & WM_NEVER_MAPPED) {
	/*
	 * Create the underlying Mac window for this Tk window.
	 */

	if (!TkMacOSXHostToplevelExists(winPtr)) {
	    TkMacOSXMakeRealWindowExist(winPtr);
	}

	wmPtr->flags &= ~WM_NEVER_MAPPED;

	/*
	 * Generate configure event when we first map the window.
	 */

	TkGenWMConfigureEvent((Tk_Window)winPtr, wmPtr->x, wmPtr->y, -1, -1,
		TK_LOCATION_CHANGED);

	/*
	 * This is the first time this window has ever been mapped. Store all
	 * the window-manager-related information for the window.
	 */

	if (wmPtr->titleUid == NULL) {
	    wmPtr->titleUid = winPtr->nameUid;
	}

	if (!Tk_IsEmbedded(winPtr)) {
	    TkSetWMName(winPtr, wmPtr->titleUid);
	}

	TkWmSetClass(winPtr);

	if (wmPtr->iconName != NULL) {
	    XSetIconName(winPtr->display, winPtr->window, wmPtr->iconName);
	}

	wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    }
    if (wmPtr->hints.initial_state == WithdrawnState) {
	return;
    }

    /*
     * TODO: we need to display a window if it's iconic on creation.
     */

    if (wmPtr->hints.initial_state == IconicState) {
	return;
    }

    /*
     * Update geometry information.
     */

    wmPtr->flags |= WM_ABOUT_TO_MAP;
    if (wmPtr->flags & WM_UPDATE_PENDING) {
	Tcl_CancelIdleCall(UpdateGeometryInfo, winPtr);
    }
    UpdateGeometryInfo(winPtr);
    wmPtr->flags &= ~WM_ABOUT_TO_MAP;

    /*
     * Map the window and process a MapNotify event for it.
     */

    winPtr->flags |= TK_MAPPED;
    XMapWindow(winPtr->display, winPtr->window);
    event.xany.serial = LastKnownRequestProcessed(winPtr->display);
    event.xany.send_event = False;
    event.xany.display = winPtr->display;
    event.xmap.window = winPtr->window;
    event.xmap.type = MapNotify;
    event.xmap.event = winPtr->window;
    event.xmap.override_redirect = winPtr->atts.override_redirect;
    Tk_HandleEvent(&event);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmUnmapWindow --
 *
 *	This procedure is invoked to unmap a top-level window. On the
 *	Macintosh all we do is call XUnmapWindow.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Unmaps the window.
 *
 *----------------------------------------------------------------------
 */

void
TkWmUnmapWindow(
    TkWindow *winPtr)		/* Top-level window that's about to be
				 * unmapped. */
{
    winPtr->flags &= ~TK_MAPPED;
    if ((winPtr->window != None)
	    && (XUnmapWindow(winPtr->display, winPtr->window) == Success)) {
	XEvent event;

	event.xany.serial = LastKnownRequestProcessed(winPtr->display);
	event.xany.send_event = False;
	event.xany.display = winPtr->display;
	event.xunmap.type = UnmapNotify;
	event.xunmap.window = winPtr->window;
	event.xunmap.event = winPtr->window;
	event.xunmap.from_configure = false;
	Tk_HandleEvent(&event);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmDeadWindow --
 *
 *	This procedure is invoked when a top-level window is about to be
 *	deleted. It cleans up the wm-related data structures for the window.
 *      If the dead window contains the pointer, TkUpdatePointer is called
 *      to tell Tk which window will be the new pointer window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The WmInfo structure for winPtr gets freed.  Tk's cached pointer
 *      window may change.
 *
 *----------------------------------------------------------------------
 */

void
TkWmDeadWindow(
    TkWindow *winPtr)		/* Top-level window that's being deleted. */
{
    TkWindow *winPtr2;
    NSWindow *w;
    WmInfo *wmPtr = winPtr->wmInfoPtr, *wmPtr2;
    TKWindow *deadNSWindow = NULL;
    if (Tk_WindowId(winPtr) == None) {
	fprintf(stderr, "TkWmDeadWindow: no window id\n");
    } else {
	deadNSWindow = (TKWindow *)TkMacOSXGetNSWindowForDrawable(Tk_WindowId(winPtr));
    }
    /*
     *If the dead window is a transient, remove it from the container's list.
     */

    RemoveTransient(winPtr);
    Tk_ManageGeometry((Tk_Window)winPtr, NULL, NULL);
    Tk_DeleteEventHandler((Tk_Window)winPtr, StructureNotifyMask,
	    TopLevelEventProc, winPtr);
    if (wmPtr->hints.flags & IconPixmapHint) {
	Tk_FreeBitmap(winPtr->display, wmPtr->hints.icon_pixmap);
    }
    if (wmPtr->hints.flags & IconMaskHint) {
	Tk_FreeBitmap(winPtr->display, wmPtr->hints.icon_mask);
    }
    if (wmPtr->iconName != NULL) {
	ckfree(wmPtr->iconName);
    }
    if (wmPtr->leaderName != NULL) {
	ckfree(wmPtr->leaderName);
    }
    if (wmPtr->icon != NULL) {
	wmPtr2 = ((TkWindow *)wmPtr->icon)->wmInfoPtr;
	wmPtr2->iconFor = NULL;
    }
    if (wmPtr->iconFor != NULL) {
	wmPtr2 = ((TkWindow *)wmPtr->iconFor)->wmInfoPtr;
	wmPtr2->icon = NULL;
	wmPtr2->hints.flags &= ~IconWindowHint;
    }
    while (wmPtr->protPtr != NULL) {
	ProtocolHandler *protPtr = wmPtr->protPtr;
	wmPtr->protPtr = protPtr->nextPtr;
	Tcl_EventuallyFree(protPtr, TCL_DYNAMIC);
    }
    if (wmPtr->commandObj != NULL) {
	Tcl_DecrRefCount(wmPtr->commandObj);
    }
    if (wmPtr->clientMachine != NULL) {
	ckfree(wmPtr->clientMachine);
    }
    if (wmPtr->flags & WM_UPDATE_PENDING) {
	Tcl_CancelIdleCall(UpdateGeometryInfo, winPtr);
    }

    /*
     * If the dead window has a transient, remove references to it from
     * the transient.
     */

    for (Transient *transientPtr = wmPtr->transientPtr;
	    transientPtr != NULL; transientPtr = transientPtr->nextPtr) {
	TkWindow *containerPtr = (TkWindow *)TkMacOSXGetContainer(
	    transientPtr->winPtr);
	if (containerPtr == winPtr) {
	    wmPtr2 = transientPtr->winPtr->wmInfoPtr;
	    wmPtr2->container = NULL;
	}
    }

    while (wmPtr->transientPtr != NULL) {
	Transient *transientPtr = wmPtr->transientPtr;

	wmPtr->transientPtr = transientPtr->nextPtr;
	ckfree(transientPtr);
    }

    /*
     * Remove references to the Tk window from the mouse event processing
     * state which is recorded in the NSApplication object and notify Tk
     * of the new pointer window.
     */

    NSPoint mouse = [NSEvent mouseLocation];
    [NSApp setTkPointerWindow:nil];
    winPtr2 = NULL;

    for (w in [NSApp orderedWindows]) {
	if (w == deadNSWindow || w == NULL) {
	    continue;
	}
	winPtr2 = TkMacOSXGetTkWindow(w);
	if (winPtr2 == NULL) {
	    continue;
	}
	if (NSPointInRect(mouse, [w frame])) {
	    [NSApp setTkPointerWindow: winPtr2];
	    break;
	}
    }
    if (winPtr2) {
	/*
	 * We now know which toplevel will contain the pointer when the window
	 * is destroyed.  We need to know which Tk window within the
	 * toplevel will contain the pointer.
	 */
	NSPoint local = [w tkConvertPointFromScreen: mouse];
	int top_x = floor(local.x),
	    top_y = floor(w.frame.size.height - local.y);
	int root_x = floor(mouse.x),
	    root_y = floor(TkMacOSXZeroScreenHeight() - mouse.y);
	int win_x, win_y;
	Tk_Window target = Tk_TopCoordsToWindow((Tk_Window) winPtr2, top_x, top_y, &win_x, &win_y);
	/*
	 * A non-toplevel window can have a NULL parent while it is in the process of
	 * being destroyed.  We should not call Tk_UpdatePointer in that case.
	 */
	if (Tk_Parent(target) != NULL || Tk_IsTopLevel(target)) {
	    Tk_UpdatePointer(target, root_x, root_y, [NSApp tkButtonState]);
	}
    }

    /*
     * Unregister the NSWindow and remove all references to it from the Tk
     * data structures.  If the NSWindow is a child, disassociate it from
     * the parent.  Then close and release the NSWindow.
     */

    if (deadNSWindow && !Tk_IsEmbedded(winPtr)) {
	NSWindow *parent = [deadNSWindow parentWindow];
	[deadNSWindow setTkWindow:None];
	if (winPtr->window) {
	    ((MacDrawable *)winPtr->window)->view = nil;
	}
	wmPtr->window = NULL;

	if (parent) {
	    [parent removeChildWindow:deadNSWindow];
	}

#if DEBUG_ZOMBIES > 1
	{
	    const char *title = [[deadNSWindow title] UTF8String];
	    if (title == nil) {
		title = "unnamed window";
	    }
	    fprintf(stderr, ">>>> Closing <%s>. Count is: %lu\n", title,
		    [deadNSWindow retainCount]);
	}
#endif

	/*
	 * When a window is closed we want to move the focus to the next
	 * highest window.  Apple's documentation says that calling the
	 * orderOut method of the key window will accomplish this.  But
	 * experiment shows that this is not the case.  So we have to reset the
	 * key window ourselves.  When the window is the last one on the screen
	 * there is no choice for a new key window.  Moreover, if the host
	 * computer has a TouchBar then the TouchBar holds a reference to the
	 * key window which prevents it from being deallocated until it stops
	 * being the key window.  On these systems the only option for
	 * preventing zombies is to set the key window to nil.
	 */

	/*
	 * Prevent zombies on systems with a TouchBar.
	 */

	if (deadNSWindow == [NSApp keyWindow]) {
	    [NSApp _setKeyWindow:nil];
	    [NSApp _setMainWindow:nil];
	}

	/*
	 * Find a new keyWindow.  It will be assinged as the new
	 * TkEventTarget when [NSApp WindowActivation] is called..
	 */

	TkMacOSXAssignNewKeyWindow(Tk_Interp((Tk_Window) winPtr), deadNSWindow);

	/*
	 * Avoid redrawing the view after it is released.
	 */

	TKContentView *deadView = [deadNSWindow contentView];
	Tcl_CancelIdleCall(TkMacOSXRedrawViewIdleTask, (void *) deadView);
	Tcl_CancelIdleCall(TkMacOSXUpdateViewIdleTask, (void *) deadView);
	CGContextRelease(deadView.tkLayerBitmapContext);
	[deadNSWindow close];
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
	NSUserDefaults *preferences = [NSUserDefaults standardUserDefaults];
	[preferences removeObserver:deadNSWindow.contentView
		      forKeyPath:@"AppleHighlightColor"];
#endif
	[deadNSWindow release];

#if DEBUG_ZOMBIES > 1
	fprintf(stderr, "================= Pool dump ===================\n");
	[NSAutoreleasePool showPools];
#endif

    }

    /*
     * Deallocate the wmInfo and clear the wmInfoPtr.
     */

    ckfree(wmPtr);
    winPtr->wmInfoPtr = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmSetClass --
 *
 *	This procedure is invoked whenever a top-level window's class is
 *	changed. If the window has been mapped then this procedure updates the
 *	window manager property for the class. If the window hasn't been
 *	mapped, the update is deferred until just before the first mapping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A window property may get updated.
 *
 *----------------------------------------------------------------------
 */

void
TkWmSetClass(
    TCL_UNUSED(TkWindow *))		/* Newly-created top-level window. */
{
    return;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_WmObjCmd --
 *
 *	This procedure is invoked to process the "wm" Tcl command. See the
 *	user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

int
Tk_WmObjCmd(
    void *clientData,	/* Main window associated with interpreter. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Tk_Window tkwin = (Tk_Window)clientData;
    static const char *const optionStrings[] = {
	"aspect", "attributes", "client", "colormapwindows",
	"command", "deiconify", "focusmodel", "forget",
	"frame", "geometry", "grid", "group", "iconbadge",
	"iconbitmap", "iconify", "iconmask", "iconname",
	"iconphoto", "iconposition", "iconwindow",
	"manage", "maxsize", "minsize", "overrideredirect",
	"positionfrom", "protocol", "resizable", "sizefrom",
	"stackorder", "state", "title", "transient",
	"withdraw", NULL };
    enum options {
	WMOPT_ASPECT, WMOPT_ATTRIBUTES, WMOPT_CLIENT, WMOPT_COLORMAPWINDOWS,
	WMOPT_COMMAND, WMOPT_DEICONIFY, WMOPT_FOCUSMODEL, WMOPT_FORGET,
	WMOPT_FRAME, WMOPT_GEOMETRY, WMOPT_GRID, WMOPT_GROUP,  WMOPT_ICONBADGE,
	WMOPT_ICONBITMAP, WMOPT_ICONIFY, WMOPT_ICONMASK, WMOPT_ICONNAME,
	WMOPT_ICONPHOTO, WMOPT_ICONPOSITION, WMOPT_ICONWINDOW,
	WMOPT_MANAGE, WMOPT_MAXSIZE, WMOPT_MINSIZE, WMOPT_OVERRIDEREDIRECT,
	WMOPT_POSITIONFROM, WMOPT_PROTOCOL, WMOPT_RESIZABLE, WMOPT_SIZEFROM,
	WMOPT_STACKORDER, WMOPT_STATE, WMOPT_TITLE, WMOPT_TRANSIENT,
	WMOPT_WITHDRAW };
    int index;
    Tcl_Size length;
    char *argv1;
    TkWindow *winPtr;

    if (objc < 2) {
    wrongNumArgs:
	Tcl_WrongNumArgs(interp, 1, objv, "option window ?arg ...?");
	return TCL_ERROR;
    }

    argv1 = Tcl_GetStringFromObj(objv[1], &length);
    if ((argv1[0] == 't') && (strncmp(argv1, "tracing", length) == 0)
	    && (length >= 3)) {
	if ((objc != 2) && (objc != 3)) {
	    Tcl_WrongNumArgs(interp, 2, objv, "?boolean?");
	    return TCL_ERROR;
	}
	if (objc == 2) {
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(wmTracing));
	    return TCL_OK;
	}
	return Tcl_GetBooleanFromObj(interp, objv[2], &wmTracing);
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[1], optionStrings,
	    sizeof(char *), "option", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }
    if (objc < 3) {
	goto wrongNumArgs;
    }
    if (index == WMOPT_ATTRIBUTES && objc == 5 &&
	strcmp(Tcl_GetString(objv[3]), "-class") == 0) {
	if (TkGetWindowFromObj(NULL, tkwin, objv[2], (Tk_Window *) &winPtr)
	    == TCL_OK) {
	    if (winPtr->wmInfoPtr->window != NULL) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "Cannot change the class after the mac window is created.",-1));
		Tcl_SetErrorCode(interp, "TK", "CLASS_CHANGE", (char *)NULL);
		return TCL_ERROR;
	    }
	} else {
		winPtr = NULL;
	}
    } else if (index == WMOPT_ATTRIBUTES && objc == 5 &&
	       strcmp(Tcl_GetString(objv[3]), "-tabbingid") == 0) {
	    if (TkGetWindowFromObj(NULL, tkwin, objv[2], (Tk_Window *) &winPtr)
		!= TCL_OK) {
		winPtr = NULL;
	    }
    } else if (index == WMOPT_ATTRIBUTES && objc == 5 &&
	       strcmp(Tcl_GetString(objv[3]), "-tabbingmode") == 0) {
	    if (TkGetWindowFromObj(NULL, tkwin, objv[2], (Tk_Window *) &winPtr)
		!= TCL_OK) {
		winPtr = NULL;
	    }
    } else if (TkGetWindowFromObj(interp, tkwin, objv[2], (Tk_Window *) &winPtr)
	       != TCL_OK) {
	return TCL_ERROR;
    }
    if (winPtr && !Tk_IsTopLevel(winPtr)
	    && (index != WMOPT_MANAGE) && (index != WMOPT_FORGET)) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"window \"%s\" isn't a top-level window", winPtr->pathName));
	Tcl_SetErrorCode(interp, "TK", "LOOKUP", "TOPLEVEL", winPtr->pathName,
		NULL);
	return TCL_ERROR;
    }

    switch ((enum options) index) {
    case WMOPT_ASPECT:
	return WmAspectCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ATTRIBUTES:
	return WmAttributesCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_CLIENT:
	return WmClientCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_COLORMAPWINDOWS:
	return WmColormapwindowsCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_COMMAND:
	return WmCommandCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_DEICONIFY:
	return WmDeiconifyCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_FOCUSMODEL:
	return WmFocusmodelCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_FORGET:
	return WmForgetCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_FRAME:
	return WmFrameCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_GEOMETRY:
	return WmGeometryCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_GRID:
	return WmGridCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_GROUP:
	return WmGroupCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONBADGE:
	return WmIconbadgeCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONBITMAP:
	return WmIconbitmapCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONIFY:
	return WmIconifyCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONMASK:
	return WmIconmaskCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONNAME:
	return WmIconnameCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONPHOTO:
	return WmIconphotoCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONPOSITION:
	return WmIconpositionCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_ICONWINDOW:
	return WmIconwindowCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_MANAGE:
	return WmManageCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_MAXSIZE:
	return WmMaxsizeCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_MINSIZE:
	return WmMinsizeCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_OVERRIDEREDIRECT:
	return WmOverrideredirectCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_POSITIONFROM:
	return WmPositionfromCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_PROTOCOL:
	return WmProtocolCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_RESIZABLE:
	return WmResizableCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_SIZEFROM:
	return WmSizefromCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_STACKORDER:
	return WmStackorderCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_STATE:
	return WmStateCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_TITLE:
	return WmTitleCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_TRANSIENT:
	return WmTransientCmd(tkwin, winPtr, interp, objc, objv);
    case WMOPT_WITHDRAW:
	return WmWithdrawCmd(tkwin, winPtr, interp, objc, objv);
    }

    /* This should not happen */
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * WmAspectCmd --
 *
 *	This procedure is invoked to process the "wm aspect" Tcl command. See
 *	the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmAspectCmd(
    TCL_UNUSED(Tk_Window),		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int numer1, denom1, numer2, denom2;

    if ((objc != 3) && (objc != 7)) {
	Tcl_WrongNumArgs(interp, 2, objv,
		"window ?minNumer minDenom maxNumer maxDenom?");
	return TCL_ERROR;
    }
    if (objc == 3) {
	if (wmPtr->sizeHintsFlags & PAspect) {
	    Tcl_Obj *results[4];

	    results[0] = Tcl_NewWideIntObj(wmPtr->minAspect.x);
	    results[1] = Tcl_NewWideIntObj(wmPtr->minAspect.y);
	    results[2] = Tcl_NewWideIntObj(wmPtr->maxAspect.x);
	    results[3] = Tcl_NewWideIntObj(wmPtr->maxAspect.y);
	    Tcl_SetObjResult(interp, Tcl_NewListObj(4, results));
	}
	return TCL_OK;
    }
    if (*Tcl_GetString(objv[3]) == '\0') {
	wmPtr->sizeHintsFlags &= ~PAspect;
    } else {
	if ((Tcl_GetIntFromObj(interp, objv[3], &numer1) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[4], &denom1) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[5], &numer2) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[6], &denom2) != TCL_OK)) {
	    return TCL_ERROR;
	}
	if ((numer1 <= 0) || (denom1 <= 0) || (numer2 <= 0) ||
		(denom2 <= 0)) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "aspect number can't be <= 0", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "WM", "ASPECT", (char *)NULL);
	    return TCL_ERROR;
	}
	wmPtr->minAspect.x = numer1;
	wmPtr->minAspect.y = denom1;
	wmPtr->maxAspect.x = numer2;
	wmPtr->maxAspect.y = denom2;
	wmPtr->sizeHintsFlags |= PAspect;
    }
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmSetAttribute --
 *
 *	Helper routine for WmAttributesCmd. Sets the value of the specified
 *	attribute.
 *
 * Returns:
 *
 *	TCL_OK if successful, TCL_ERROR otherwise. In case of an error, leaves
 *	a message in the interpreter's result.
 *
 *----------------------------------------------------------------------
 */

static int
WmSetAttribute(
    TkWindow *winPtr,		/* Toplevel to work with */
    NSWindow *macWindow,
    Tcl_Interp *interp,		/* Current interpreter */
    WmAttribute attribute,	/* Code of attribute to set */
    Tcl_Obj *value)		/* New value */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int boolValue;
    NSString *identifier;

    switch (attribute) {
    case WMATT_ALPHA: {
	double dval;

	if (Tcl_GetDoubleFromObj(interp, value, &dval) != TCL_OK) {
	    return TCL_ERROR;
	}

	/*
	 * The user should give (transparent) 0 .. 1.0 (opaque)
	 */

	if (dval < 0.0) {
	    dval = 0.0;
	} else if (dval > 1.0) {
	    dval = 1.0;
	}
	[macWindow setAlphaValue:dval];
	break;
    }
    case WMATT_APPEARANCE: {
	int index;
	if (Tcl_GetIndexFromObjStruct(interp, value, appearanceStrings,
	    sizeof(char *), "appearancename", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	switch ((enum appearances) index) {
	case APPEARANCE_AQUA:
	    macWindow.appearance = [NSAppearance appearanceNamed:
		NSAppearanceNameAqua];
	    break;
	case APPEARANCE_DARKAQUA:
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
	    if (@available(macOS 10.14, *)) {
		macWindow.appearance = [NSAppearance appearanceNamed:
		    NSAppearanceNameDarkAqua];
	    }
#endif // MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
	    break;
	default:
	    macWindow.appearance = nil;
	}
	break;
    }
    case WMATT_BUTTONS: {
	windowButtonState state = {0};
	Tcl_Obj **elements;
	Tcl_Size nElements, i;
	if (Tcl_ListObjGetElements(interp, value, &nElements, &elements) == TCL_OK) {
	    int index = 0;
	    for (i = 0; i < nElements; i++) {
		if (Tcl_GetIndexFromObjStruct(interp, elements[i], buttonNames,
		       sizeof(char *), "window button name", 0, &index) != TCL_OK) {
		    return TCL_ERROR;
		} else {
		    state.intvalue |= (1 << index);
		}
	    }
	} else if (Tcl_GetIntFromObj(interp, value, &state.intvalue) != TCL_OK) {
	    return TCL_ERROR;
	}
	NSButton *closer = [macWindow standardWindowButton:
			       NSWindowCloseButton];
	NSButton *miniaturizer = [macWindow standardWindowButton:
				     NSWindowMiniaturizeButton];
	NSButton *zoomer = [macWindow standardWindowButton:
			      NSWindowZoomButton];
	closer.enabled = (state.bits.close != 0);
	miniaturizer.enabled = (state.bits.miniaturize != 0);
	zoomer.enabled = (state.bits.zoom != 0);
	break;
    }
    case WMATT_FULLSCREEN:
	if (Tcl_GetBooleanFromObj(interp, value, &boolValue) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (boolValue != (([macWindow styleMask] & NSFullScreenWindowMask) != 0)) {
	    [macWindow toggleFullScreen:macWindow];
	}
	break;
    case WMATT_MODIFIED:
	if (Tcl_GetBooleanFromObj(interp, value, &boolValue) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (boolValue != [macWindow isDocumentEdited]) {
	    [macWindow setDocumentEdited:(BOOL)boolValue];
	}
	break;
    case WMATT_NOTIFY:
	if (Tcl_GetBooleanFromObj(interp, value, &boolValue) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (boolValue == !tkMacOSXWmAttrNotifyVal) {
	    static NSInteger request = -1;

	    if (request >= 0) {
		[NSApp cancelUserAttentionRequest:request];
		request = -1;
	    }
	    if (boolValue) {
		request = [NSApp requestUserAttention:NSCriticalRequest];
	    }
	    tkMacOSXWmAttrNotifyVal = boolValue;
	}
	break;
    case WMATT_STYLEMASK: {
	unsigned long styleMaskValue = 0;
	Tcl_Obj **elements;
	Tcl_Size nElements, i;
	if (Tcl_ListObjGetElements(interp, value, &nElements, &elements) == TCL_OK) {
	    int index;
	    for (i = 0; i < nElements; i++) {
		if (Tcl_GetIndexFromObjStruct(interp, elements[i], styleMaskBits,
		       sizeof(styleMaskBit), "styleMask bit", 0, &index) != TCL_OK) {
		    return TCL_ERROR;
		} else if (![macWindow isKindOfClass: [NSPanel class]] &&
			   styleMaskBits[index].allowed == NSWindowClass_panel) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"styleMask bit \"%s\" can only be used with an NSPanel",
			styleMaskBits[index].bitname));
		    Tcl_SetErrorCode(interp, "TK", "INVALID_STYLEMASK_BIT", (char *)NULL);
		    return TCL_ERROR;
		} else {
		    styleMaskValue |= styleMaskBits[index].bitvalue;
		}
		/*
		 * Be sure not to change the fullscreen bit.
		 */
		styleMaskValue |= (NSWindowStyleMaskFullScreen & macWindow.styleMask);
	    }
	    /*
	     * A resizable docmodal NSWindow or NSPanel does not work
	     * correctly.  It cannot be resized from the top edge.  Other bits,
	     * such as titled are ignored for docmodals.  To be safe, we clear
	     * all other bits when the docmodal bit is set.
	     */
	    if (styleMaskValue & NSDocModalWindowMask) {
		styleMaskValue &= ~NSWindowStyleMaskResizable;
	    }
	    if ([macWindow isKindOfClass: [NSPanel class]]) {
		/*
		 * We always make NSPanels titled, nonactivating utility windows,
		 * even if these bits are not requested in the command.
		 */
		if (!(styleMaskValue & NSWindowStyleMaskTitled) ) {
		    styleMaskValue |= NSWindowStyleMaskTitled;
		    styleMaskValue |= NSWindowStyleMaskUtilityWindow;
		    styleMaskValue |= NSWindowStyleMaskNonactivatingPanel;
		}
	    }
	    if (styleMaskValue & NSWindowStyleMaskFullSizeContentView) {
		macWindow.titlebarAppearsTransparent = YES;
	    } else {
		macWindow.titlebarAppearsTransparent = NO;
	    }
	} else {
	    return TCL_ERROR;
	}
	NSRect oldFrame = [macWindow frame];
#ifdef DEBUG
	fprintf(stderr, "Current styleMask: %lx\n", [macWindow styleMask]);
	fprintf(stderr, "Setting styleMask to %lx\n", styleMaskValue);
#endif
	macWindow.styleMask = (unsigned long) styleMaskValue;
	NSRect newFrame = [macWindow frame];
	int heightDiff = newFrame.size.height - oldFrame.size.height;
	int newHeight = heightDiff < 0 ? newFrame.size.height :
	    newFrame.size.height - heightDiff;
	[(TKWindow *)macWindow tkLayoutChanged];
	if (heightDiff) {
	    // Calling XMoveResizeWindow twice is a hack to force a relayout
	    // of the window.
	    XMoveResizeWindow(winPtr->display, winPtr->window,
			  winPtr->changes.x, winPtr->changes.y,
			  newFrame.size.width, newHeight - 1);
	    XMoveResizeWindow(winPtr->display, winPtr->window,
			  winPtr->changes.x, winPtr->changes.y,
			  newFrame.size.width, newHeight);
	}
	break;
    }
    case WMATT_TABBINGID: {
	NSString *oldId = [macWindow tabbingIdentifier];
	char *valueString;
	Tcl_Size length;
	if ([NSApp macOSVersion] < 101300) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		  "Tabbing identifiers require macOS 10.13", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "WM", "TABBINGID", (char *)NULL);
	    return TCL_ERROR;
	}
	valueString = Tcl_GetStringFromObj(value, &length);
	identifier = [NSString stringWithUTF8String:valueString];
	[macWindow setTabbingIdentifier: identifier];

	/*
	 * If the tabbingIdentifier of a tab is changed we move it into
	 * the tab group with that identifier.
	 */

	if ([oldId compare:identifier] != NSOrderedSame) {
	    placeAsTab((TKWindow *)macWindow);
	}
	break;
    }
    case WMATT_TABBINGMODE: {
	int index;
	tabbingMode mode;
	if (Tcl_GetIndexFromObjStruct(interp, value, tabbingModes,
	   sizeof(tabbingMode), "NSWindow Tabbing Mode", 0, &index) != TCL_OK) {
		return TCL_ERROR;
	    }
	mode = tabbingModes[index];
	[macWindow setTabbingMode: mode.modeValue];
	placeAsTab((TKWindow *)macWindow);
	break;
    }
    case WMATT_ISDARK: {
	break;
    }
    case WMATT_TITLEPATH: {
	const char *path = (const char *)Tcl_FSGetNativePath(value);
	NSString *filename = @"";

	if (path && *path) {
	    filename = [NSString stringWithUTF8String:path];
	}
	[macWindow setRepresentedFilename:filename];
	break;
    }
    case WMATT_TOPMOST:
	if (Tcl_GetBooleanFromObj(interp, value, &boolValue) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (boolValue != ((wmPtr->flags & WM_TOPMOST) != 0)) {
	    int oldFlags = wmPtr->flags;

	    if (boolValue) {
		wmPtr->flags |= WM_TOPMOST;
	    } else {
		wmPtr->flags &= ~WM_TOPMOST;
	    }
	    ApplyWindowAttributeFlagChanges(winPtr, macWindow,
		    wmPtr->attributes, oldFlags, 1, 0);
	}
	break;
    case WMATT_TRANSPARENT:
	if (Tcl_GetBooleanFromObj(interp, value, &boolValue) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (boolValue != ((wmPtr->flags & WM_TRANSPARENT) != 0)) {
	    UInt64 oldAttributes = wmPtr->attributes;
	    int oldFlags = wmPtr->flags;

	    if (boolValue) {
		wmPtr->flags |= WM_TRANSPARENT;
		wmPtr->attributes |= kWindowNoShadowAttribute;
	    } else {
		wmPtr->flags &= ~WM_TRANSPARENT;
		wmPtr->attributes &= ~kWindowNoShadowAttribute;
	    }
	    ApplyWindowAttributeFlagChanges(winPtr, macWindow, oldAttributes,
		    oldFlags, 1, 0);
	    [macWindow setBackgroundColor:boolValue ? [NSColor clearColor] : nil];
	    [macWindow setOpaque:!boolValue];
	    TkMacOSXInvalidateWindow((MacDrawable *)winPtr->window,
		    TK_PARENT_WINDOW);
	    }
	break;
    case WMATT_CLASS:
	break;
    case WMATT_TYPE:
	TKLog(@"The type attribute is ignored on macOS.");
	break;
    case _WMATT_LAST_ATTRIBUTE:
    default:
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmGetAttribute --
 *
 *	Helper routine for WmAttributesCmd. Returns the current value of the
 *	specified attribute.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *
WmGetAttribute(
    TkWindow *winPtr,		/* Toplevel to work with */
    NSWindow *macWindow,
    WmAttribute attribute)	/* Code of attribute to get */
{
    Tcl_Obj *result = NULL;
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    switch (attribute) {
    case WMATT_ALPHA:
	result = Tcl_NewDoubleObj([macWindow alphaValue]);
	break;
    case WMATT_APPEARANCE: {
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101300
	NSAppearanceName appearance;
#else
	NSString *appearance;
#endif // MAC_OS_X_VERSION_MAX_ALLOWED >= 101300
	const char *resultString = "unrecognized";
	appearance = macWindow.appearance.name;
	if (appearance == nil) {
	    resultString = appearanceStrings[APPEARANCE_AUTO];
	} else if (appearance == NSAppearanceNameAqua) {
	    resultString = appearanceStrings[APPEARANCE_AQUA];
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
	} else if (@available(macOS 10.14, *)) {
	    if (appearance == NSAppearanceNameDarkAqua) {
		resultString = appearanceStrings[APPEARANCE_DARKAQUA];
	    }
#endif // MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
	}
	result = Tcl_NewStringObj(resultString, TCL_INDEX_NONE);
	break;
    }
    case WMATT_BUTTONS: {
	result = Tcl_NewListObj(3, NULL);
	if ([macWindow standardWindowButton:NSWindowCloseButton].enabled) {
	    Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj("close", TCL_INDEX_NONE));
	}
	if ([macWindow standardWindowButton:NSWindowMiniaturizeButton].enabled) {
	    Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj("miniaturize", TCL_INDEX_NONE));
	}
	if ([macWindow standardWindowButton:NSWindowZoomButton].enabled) {
	    Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj("zoom", TCL_INDEX_NONE));
	}
	break;
    }
    case WMATT_CLASS:
	if ([macWindow isKindOfClass:[NSPanel class]]) {
	    result = Tcl_NewStringObj(subclassNames[subclassNSPanel], TCL_INDEX_NONE);
	} else {
	    result = Tcl_NewStringObj(subclassNames[subclassNSWindow], TCL_INDEX_NONE);
	}
	break;
    case WMATT_FULLSCREEN:
	result = Tcl_NewBooleanObj([macWindow styleMask] & NSFullScreenWindowMask);
	break;
    case WMATT_ISDARK:
	result = Tcl_NewBooleanObj(TkMacOSXInDarkMode((Tk_Window)winPtr));
	break;
    case WMATT_MODIFIED:
	result = Tcl_NewBooleanObj([macWindow isDocumentEdited]);
	break;
    case WMATT_NOTIFY:
	result = Tcl_NewBooleanObj(tkMacOSXWmAttrNotifyVal);
	break;
    case WMATT_STYLEMASK: {
	unsigned long styleMaskValue = [macWindow styleMask];
	const styleMaskBit *bit;
	result = Tcl_NewListObj(9, NULL);
	for (bit = styleMaskBits; bit->bitname != NULL; bit++) {
	    if (styleMaskValue & bit->bitvalue) {
		Tcl_ListObjAppendElement(NULL, result,
		    Tcl_NewStringObj(bit->bitname, TCL_INDEX_NONE));
	    }
	}
	break;
    }
    case WMATT_TABBINGID:
	result = Tcl_NewStringObj([[macWindow tabbingIdentifier] UTF8String],
		-1);
	break;
    case WMATT_TABBINGMODE: {
	long mode = [macWindow tabbingMode];
	const char *name = "unrecognized";
	for (const tabbingMode *m = tabbingModes; m->modeName != NULL; m++) {
	    if (m->modeValue == mode) {
		name = m->modeName;
		break;
	    }
	}
	result = Tcl_NewStringObj(name, TCL_INDEX_NONE);
	break;
    }
    case WMATT_TITLEPATH:
	result = Tcl_NewStringObj([[macWindow representedFilename] UTF8String],
		TCL_INDEX_NONE);
	break;
    case WMATT_TOPMOST:
	result = Tcl_NewBooleanObj(wmPtr->flags & WM_TOPMOST);
	break;
    case WMATT_TRANSPARENT:
	result = Tcl_NewBooleanObj(wmPtr->flags & WM_TRANSPARENT);
	break;
    case WMATT_TYPE:
	result = Tcl_NewStringObj("unsupported", TCL_INDEX_NONE);
	break;
    case _WMATT_LAST_ATTRIBUTE:
    default:
	break;
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * WmAttributesCmd --
 *
 *	This procedure is invoked to process the "wm attributes" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmAttributesCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    int attribute = 0;
    NSWindow *macWindow;
    if (winPtr == NULL && objc == 5) {
	int index, isNew = 0;
	Tcl_Size length;

	/*
	 * If we are setting an atttribute of a future window, save the value
	 * in a hash table so we can look it up when the window is actually
	 * created.
	 */

	if (strcmp(Tcl_GetString(objv[3]), "-class") == 0) {
	    if (Tcl_GetIndexFromObjStruct(interp, objv[4], subclassNames,
		    sizeof(char *), "NSWindow subclass", 0, &index) != TCL_OK) {
		return TCL_ERROR;
	    }
	    Tcl_HashEntry *hPtr = Tcl_CreateHashEntry(&pathnameToSubclass,
		Tcl_GetString(objv[2]), &isNew);
	    if (hPtr) {
		Tcl_SetHashValue(hPtr, INT2PTR(index));
		return TCL_OK;
	    }
	} else if (strcmp(Tcl_GetString(objv[3]), "-tabbingid") == 0) {
	    char *identifier = Tcl_GetStringFromObj(objv[4], &length);
	    char *value = (char *)ckalloc(length + 1);
	    strncpy(value, identifier, length + 1);
	    Tcl_HashEntry *hPtr = Tcl_CreateHashEntry(&pathnameToTabbingId,
				      Tcl_GetString(objv[2]), &isNew);
	    if (hPtr) {
		Tcl_SetHashValue(hPtr, value);
		return TCL_OK;
	    }
	} else if (strcmp(Tcl_GetString(objv[3]), "-tabbingmode") == 0) {
	    long value = NSWindowTabbingModeAutomatic;
	    int modeIndex;
	    if (Tcl_GetIndexFromObjStruct(interp, objv[4], tabbingModes,
	       sizeof(tabbingMode), "NSWindow Tabbing Mode", 0, &modeIndex) == TCL_OK) {
		value = tabbingModes[modeIndex].modeValue;
	    }
	    Tcl_HashEntry *hPtr = Tcl_CreateHashEntry(&pathnameToTabbingMode,
		Tcl_GetString(objv[2]), &isNew);
	    if (hPtr) {
		Tcl_SetHashValue(hPtr, value);
		return TCL_OK;
	    }
	}
    }
    if (!winPtr) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	   "Only -class, -tabbingid, or -tabbingmode can be set before the window exists."));
	Tcl_SetErrorCode(interp, "TK", "NO_WINDOW", (char *)NULL);
	return TCL_ERROR;
    }
    if (winPtr && winPtr->window == None) {
	Tk_MakeWindowExist((Tk_Window)winPtr);
    }
    if (!TkMacOSXHostToplevelExists(winPtr)) {
	TkMacOSXMakeRealWindowExist(winPtr);
    }
    macWindow = TkMacOSXGetNSWindowForDrawable(winPtr->window);

    if (objc == 3) {		/* wm attributes $win */
	Tcl_Obj *result = Tcl_NewObj();

	for (attribute = 0; attribute < _WMATT_LAST_ATTRIBUTE; ++attribute) {
	    Tcl_ListObjAppendElement(NULL, result,
		    Tcl_NewStringObj(WmAttributeNames[attribute], TCL_INDEX_NONE));
	    Tcl_ListObjAppendElement(NULL, result,
		    WmGetAttribute(winPtr, macWindow, (WmAttribute)attribute));
	}
	Tcl_SetObjResult(interp, result);
    } else if (objc == 4) {	/* wm attributes $win -attribute */
	if (Tcl_GetIndexFromObjStruct(interp, objv[3], WmAttributeNames,
		sizeof(char *), "attribute", 0, &attribute) != TCL_OK) {
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, WmGetAttribute(winPtr, macWindow, (WmAttribute)attribute));
    } else if ((objc - 3) % 2 == 0) {	/* wm attributes $win -att value... */
	Tcl_Size i;

	for (i = 3; i < objc; i += 2) {
	    if (Tcl_GetIndexFromObjStruct(interp, objv[i], WmAttributeNames,
		    sizeof(char *), "attribute", 0, &attribute) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (WmSetAttribute(winPtr, macWindow, interp, (WmAttribute)attribute, objv[i+1])
		    != TCL_OK) {
		return TCL_ERROR;
	    }
	}
    } else {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?-attribute ?value ...??");
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmClientCmd --
 *
 *	This procedure is invoked to process the "wm client" Tcl command. See
 *	the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmClientCmd(
    TCL_UNUSED(Tk_Window),		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    char *argv3;
    Tcl_Size length;

    if ((objc != 3) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?name?");
	return TCL_ERROR;
    }
    if (objc == 3) {
	if (wmPtr->clientMachine != NULL) {
	    Tcl_SetObjResult(interp,
		    Tcl_NewStringObj(wmPtr->clientMachine, TCL_INDEX_NONE));
	}
	return TCL_OK;
    }
    argv3 = Tcl_GetStringFromObj(objv[3], &length);
    if (argv3[0] == 0) {
	if (wmPtr->clientMachine != NULL) {
	    ckfree(wmPtr->clientMachine);
	    wmPtr->clientMachine = NULL;
	}
	return TCL_OK;
    }
    if (wmPtr->clientMachine != NULL) {
	ckfree(wmPtr->clientMachine);
    }
    wmPtr->clientMachine = (char *)ckalloc(length + 1);
    strcpy(wmPtr->clientMachine, argv3);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmColormapwindowsCmd --
 *
 *	This procedure is invoked to process the "wm colormapwindows" Tcl
 *	command. See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmColormapwindowsCmd(
    Tk_Window tkwin,		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    TkWindow **cmapList, *winPtr2;
    Tcl_Size i, windowObjc;
    int gotToplevel = 0;
    Tcl_Obj **windowObjv, *resultObj;

    if ((objc != 3) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?windowList?");
	return TCL_ERROR;
    }
    if (objc == 3) {
	Tk_MakeWindowExist((Tk_Window)winPtr);
	resultObj = Tcl_NewObj();
	for (i = 0; i < wmPtr->cmapCount; i++) {
	    if ((i == (wmPtr->cmapCount-1))
		    && (wmPtr->flags & WM_ADDED_TOPLEVEL_COLORMAP)) {
		break;
	    }
	    Tcl_ListObjAppendElement(NULL, resultObj,
		    Tk_NewWindowObj((Tk_Window)wmPtr->cmapList[i]));
	}
	Tcl_SetObjResult(interp, resultObj);
	return TCL_OK;
    }
    if (Tcl_ListObjGetElements(interp, objv[3], &windowObjc, &windowObjv)
	    != TCL_OK) {
	return TCL_ERROR;
    }
    cmapList = (TkWindow **)ckalloc((windowObjc+1) * sizeof(TkWindow*));
    for (i = 0; i < windowObjc; i++) {
	if (TkGetWindowFromObj(interp, tkwin, windowObjv[i],
		(Tk_Window *) &winPtr2) != TCL_OK) {
	    ckfree(cmapList);
	    return TCL_ERROR;
	}
	if (winPtr2 == winPtr) {
	    gotToplevel = 1;
	}
	if (winPtr2->window == None) {
	    Tk_MakeWindowExist((Tk_Window)winPtr2);
	}
	cmapList[i] = winPtr2;
    }
    if (!gotToplevel) {
	wmPtr->flags |= WM_ADDED_TOPLEVEL_COLORMAP;
	cmapList[windowObjc] = winPtr;
	windowObjc++;
    } else {
	wmPtr->flags &= ~WM_ADDED_TOPLEVEL_COLORMAP;
    }
    wmPtr->flags |= WM_COLORMAPS_EXPLICIT;
    if (wmPtr->cmapList != NULL) {
	ckfree(wmPtr->cmapList);
    }
    wmPtr->cmapList = cmapList;
    wmPtr->cmapCount = windowObjc;

    /*
     * On the Macintosh all of this is just an excercise in compatibility as
     * we don't support colormaps. If we did they would be installed here.
     */

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmCommandCmd --
 *
 *	This procedure is invoked to process the "wm command" Tcl command. See
 *	the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmCommandCmd(
    TCL_UNUSED(Tk_Window),		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    Tcl_Size len;

    if ((objc != 3) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?value?");
	return TCL_ERROR;
    }
    if (objc == 3) {
	if (wmPtr->commandObj != NULL) {
	    Tcl_SetObjResult(interp, wmPtr->commandObj);
	}
	return TCL_OK;
    }
    if (*Tcl_GetString(objv[3]) == '\0') {
	if (wmPtr->commandObj != NULL) {
	    Tcl_DecrRefCount(wmPtr->commandObj);
	    wmPtr->commandObj = NULL;
	}
	return TCL_OK;
    }
    if (Tcl_ListObjLength(interp, objv[3], &len) != TCL_OK) {
	return TCL_ERROR;
    }
    if (wmPtr->commandObj != NULL) {
	Tcl_DecrRefCount(wmPtr->commandObj);
    }
    wmPtr->commandObj = Tcl_DuplicateObj(objv[3]);
    Tcl_IncrRefCount(wmPtr->commandObj);
    Tcl_InvalidateStringRep(wmPtr->commandObj);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmDeiconifyCmd --
 *
 *	This procedure is invoked to process the "wm deiconify" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmDeiconifyCmd(
    TCL_UNUSED(Tk_Window),		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    NSWindow *win = nil;
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "window");
	return TCL_ERROR;
    }
    if (wmPtr->iconFor != NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't deiconify %s: it is an icon for %s",
		Tcl_GetString(objv[2]), Tk_PathName(wmPtr->iconFor)));
	Tcl_SetErrorCode(interp, "TK", "WM", "DEICONIFY", "ICON", (char *)NULL);
	return TCL_ERROR;
    } else if (winPtr->flags & TK_EMBEDDED) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't deiconify %s: it is an embedded window",
		winPtr->pathName));
	Tcl_SetErrorCode(interp, "TK", "WM", "DEICONIFY", "EMBEDDED", (char *)NULL);
	return TCL_ERROR;
    }

    if (winPtr->window) {
	win = TkMacOSXGetNSWindowForDrawable(winPtr->window);
    }
    TkpWmSetState(winPtr, TkMacOSXIsWindowZoomed(winPtr) ?
	    ZoomState : NormalState);
    if (win) {
	[win setExcludedFromWindowsMenu:NO];
	TkMacOSXApplyWindowAttributes(winPtr, win);
	[win orderFront:NSApp];
    }
    if (wmPtr->icon) {
	Tk_UnmapWindow((Tk_Window)wmPtr->icon);
    }

    /*
     * If this window has a transient, the transient must also be deiconified if
     * it was withdrawn by the container.
     */

    for (Transient *transientPtr = wmPtr->transientPtr;
	    transientPtr != NULL; transientPtr = transientPtr->nextPtr) {
	TkWindow *winPtr2 = transientPtr->winPtr;
	WmInfo *wmPtr2 = winPtr2->wmInfoPtr;
	TkWindow *containerPtr = (TkWindow *)TkMacOSXGetContainer(winPtr2);

	if (containerPtr == winPtr) {
	    if ((wmPtr2->hints.initial_state == WithdrawnState) &&
		    ((transientPtr->flags & WITHDRAWN_BY_CONTAINER) != 0)) {
		TkpWmSetState(winPtr2, NormalState);
		transientPtr->flags &= ~WITHDRAWN_BY_CONTAINER;
	    }
	}
    }

    //Tcl_DoWhenIdle(TkMacOSXDrawAllViews, NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmFocusmodelCmd --
 *
 *	This procedure is invoked to process the "wm focusmodel" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmFocusmodelCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    static const char *const optionStrings[] = {
	"active", "passive", NULL };
    enum options {
	OPT_ACTIVE, OPT_PASSIVE };
    int index;

    if ((objc != 3) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?active|passive?");
	return TCL_ERROR;
    }
    if (objc == 3) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		wmPtr->hints.input ? "passive" : "active", TCL_INDEX_NONE));
	return TCL_OK;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[3], optionStrings,
	    sizeof(char *), "argument", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }
    if (index == OPT_ACTIVE) {
	wmPtr->hints.input = False;
    } else { /* OPT_PASSIVE */
	wmPtr->hints.input = True;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmForgetCmd --
 *
 *	This procedure is invoked to process the "wm forget" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmForgetCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel or Frame to work with */
    TCL_UNUSED(Tcl_Interp *),	/* Current interpreter. */
    TCL_UNUSED(Tcl_Size),			/* Number of arguments. */
    TCL_UNUSED(Tcl_Obj *const *))	/* Argument objects. */
{
    Tk_Window frameWin = (Tk_Window)winPtr;

    if (Tk_IsTopLevel(frameWin)) {
	MacDrawable *macWin;

	Tk_MakeWindowExist(frameWin);
	Tk_MakeWindowExist((Tk_Window)winPtr->parentPtr);

	macWin = (MacDrawable *)winPtr->window;

	TkFocusJoin(winPtr);
	Tk_UnmapWindow(frameWin);

	macWin->toplevel->referenceCount--;
	macWin->toplevel = winPtr->parentPtr->privatePtr->toplevel;
	macWin->toplevel->referenceCount++;
	macWin->flags &= ~TK_HOST_EXISTS;

	RemapWindows(winPtr, (MacDrawable *)winPtr->parentPtr->window);

	/*
	 * Make sure wm no longer manages this window
	 */
	Tk_ManageGeometry(frameWin, NULL, NULL);

	winPtr->flags &= ~(TK_TOP_HIERARCHY|TK_TOP_LEVEL|TK_HAS_WRAPPER|TK_WIN_MANAGED);

	/*
	 * Flags (above) must be cleared before calling TkMapTopFrame (below).
	 */

	TkMapTopFrame(frameWin);
    } else {
	/*
	 * Already not managed by wm - ignore it.
	 */
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmFrameCmd --
 *
 *	This procedure is invoked to process the "wm frame" Tcl command. See
 *	the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmFrameCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    Window window;
    char buf[TCL_INTEGER_SPACE];

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "window");
	return TCL_ERROR;
    }
    window = wmPtr->reparent;
    if (window == None) {
	window = Tk_WindowId((Tk_Window)winPtr);
    }
    snprintf(buf, sizeof(buf), "0x%" TCL_Z_MODIFIER "x", (size_t)window);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, TCL_INDEX_NONE));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmGeometryCmd --
 *
 *	This procedure is invoked to process the "wm geometry" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmGeometryCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    NSWindow *win = nil;
    char xSign = '+', ySign = '+';
    int width, height, x = wmPtr->x, y= wmPtr->y;
    char *argv3;
    if (winPtr && winPtr->window) {
	win = TkMacOSXGetNSWindowForDrawable(winPtr->window);
    }
    if ((objc != 3) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?newGeometry?");
	return TCL_ERROR;
    }
    if (objc == 3) {
	if (wmPtr->gridWin != NULL) {
	    width = wmPtr->reqGridWidth + (winPtr->changes.width
		    - winPtr->reqWidth)/wmPtr->widthInc;
	    height = wmPtr->reqGridHeight + (winPtr->changes.height
		    - winPtr->reqHeight)/wmPtr->heightInc;
	} else {
	    width = winPtr->changes.width;
	    height = winPtr->changes.height;
	}
	if (win) {
	    if (wmPtr->flags & WM_NEGATIVE_X) {
		xSign = '-';
		x = wmPtr->vRootWidth - wmPtr->x
		    - (width + (wmPtr->parentWidth - winPtr->changes.width));
	    }
	    if (wmPtr->flags & WM_NEGATIVE_Y) {
		ySign = '-';
		y = wmPtr->vRootHeight - wmPtr->y
		    - (height + (wmPtr->parentHeight - winPtr->changes.height));
	    }
	}
	Tcl_SetObjResult(interp, Tcl_ObjPrintf("%dx%d%c%d%c%d",
		width, height, xSign, x, ySign, y));
	return TCL_OK;
    }
    argv3 = Tcl_GetString(objv[3]);
    if (*argv3 == '\0') {
	wmPtr->width = -1;
	wmPtr->height = -1;
	WmUpdateGeom(wmPtr, winPtr);
	return TCL_OK;
    }
    return ParseGeometry(interp, argv3, winPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * WmGridCmd --
 *
 *	This procedure is invoked to process the "wm grid" Tcl command. See
 *	the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmGridCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int reqWidth, reqHeight, widthInc, heightInc;
    const char *errorMsg;

    if ((objc != 3) && (objc != 7)) {
	Tcl_WrongNumArgs(interp, 2, objv,
		"window ?baseWidth baseHeight widthInc heightInc?");
	return TCL_ERROR;
    }
    if (objc == 3) {
	if (wmPtr->sizeHintsFlags & PBaseSize) {
	    Tcl_Obj *results[4];

	    results[0] = Tcl_NewWideIntObj(wmPtr->reqGridWidth);
	    results[1] = Tcl_NewWideIntObj(wmPtr->reqGridHeight);
	    results[2] = Tcl_NewWideIntObj(wmPtr->widthInc);
	    results[3] = Tcl_NewWideIntObj(wmPtr->heightInc);
	    Tcl_SetObjResult(interp, Tcl_NewListObj(4, results));
	}
	return TCL_OK;
    }
    if (*Tcl_GetString(objv[3]) == '\0') {
	/*
	 * Turn off gridding and reset the width and height to make sense as
	 * ungridded numbers.
	 */

	wmPtr->sizeHintsFlags &= ~PBaseSize;
	if (wmPtr->width != -1) {
	    wmPtr->width = winPtr->reqWidth + (wmPtr->width
		    - wmPtr->reqGridWidth)*wmPtr->widthInc;
	    wmPtr->height = winPtr->reqHeight + (wmPtr->height
		    - wmPtr->reqGridHeight)*wmPtr->heightInc;
	}
	wmPtr->widthInc = 1;
	wmPtr->heightInc = 1;
    } else {
	if ((Tcl_GetIntFromObj(interp, objv[3], &reqWidth) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[4], &reqHeight) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[5], &widthInc) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[6], &heightInc)!=TCL_OK)) {
	    return TCL_ERROR;
	}
	if (reqWidth < 0) {
	    errorMsg = "baseWidth can't be < 0";
	    goto error;
	} else if (reqHeight < 0) {
	    errorMsg = "baseHeight can't be < 0";
	    goto error;
	} else if (widthInc <= 0) {
	    errorMsg = "widthInc can't be <= 0";
	    goto error;
	} else if (heightInc <= 0) {
	    errorMsg = "heightInc can't be <= 0";
	    goto error;
	}
	Tk_SetGrid((Tk_Window)winPtr, reqWidth, reqHeight, widthInc,
		heightInc);
    }
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);
    return TCL_OK;

  error:
    Tcl_SetObjResult(interp, Tcl_NewStringObj(errorMsg, TCL_INDEX_NONE));
    Tcl_SetErrorCode(interp, "TK", "WM", "GRID", (char *)NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * WmGroupCmd --
 *
 *	This procedure is invoked to process the "wm group" Tcl command. See
 *	the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmGroupCmd(
    Tk_Window tkwin,		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    Tk_Window tkwin2;
    char *argv3;
    Tcl_Size length;

    if ((objc != 3) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?pathName?");
	return TCL_ERROR;
    }
    if (objc == 3) {
	if (wmPtr->hints.flags & WindowGroupHint) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(wmPtr->leaderName, TCL_INDEX_NONE));
	}
	return TCL_OK;
    }

    argv3 = Tcl_GetStringFromObj(objv[3], &length);
    if (*argv3 == '\0') {
	wmPtr->hints.flags &= ~WindowGroupHint;
	if (wmPtr->leaderName != NULL) {
	    ckfree(wmPtr->leaderName);
	}
	wmPtr->leaderName = NULL;
    } else {
	if (TkGetWindowFromObj(interp, tkwin, objv[3], &tkwin2) != TCL_OK) {
	    return TCL_ERROR;
	}
	Tk_MakeWindowExist(tkwin2);
	if (wmPtr->leaderName != NULL) {
	    ckfree(wmPtr->leaderName);
	}
	wmPtr->hints.window_group = Tk_WindowId(tkwin2);
	wmPtr->hints.flags |= WindowGroupHint;
	wmPtr->leaderName = (char *)ckalloc(length + 1);
	strcpy(wmPtr->leaderName, argv3);
    }
    return TCL_OK;
}

 /*----------------------------------------------------------------------
 *
 * WmIconbadgeCmd --
 *
 *	This procedure is invoked to process the "wm iconbadge" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconbadgeCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    (void) winPtr;
    NSString *label;

    if (objc < 4) {
	Tcl_WrongNumArgs(interp, 2, objv,"window badge");
	return TCL_ERROR;
    }

    label = [NSString stringWithUTF8String:Tcl_GetString(objv[3])];

    int number = [label intValue];
    NSDockTile *dockicon = [NSApp dockTile];

    /*
     * First, check that the label is not a decimal. If it is,
     * return an error.
     */

    if ([label containsString:@"."]) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't use \"%s\" as icon badge", Tcl_GetString(objv[3])));
	return TCL_ERROR;
    }

    /*
     * Next, check that label is an int, empty string, or exclamation
     * point. If so, set the icon badge on the Dock icon. Otherwise,
     * return an error.
     */

    NSArray *array = @[@"", @"!"];
    if ([array containsObject: label]) {
	[dockicon setBadgeLabel:label];
    } else if (number > 0) {
	NSString *str = [@(number) stringValue];
	[dockicon setBadgeLabel:str];
    } else {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't use \"%s\" as icon badge", Tcl_GetString(objv[3])));
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconbitmapCmd --
 *
 *	This procedure is invoked to process the "wm iconbitmap" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconbitmapCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    Pixmap pixmap;
    char *str;
    Tcl_Size len;

    if ((objc != 3) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?bitmap?");
	return TCL_ERROR;
    }
    if (objc == 3) {
	if (wmPtr->hints.flags & IconPixmapHint) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    Tk_NameOfBitmap(winPtr->display,wmPtr->hints.icon_pixmap),
		    -1));
	}
	return TCL_OK;
    }
    str = Tcl_GetStringFromObj(objv[3], &len);
    if (winPtr->window == None) {
	Tk_MakeWindowExist((Tk_Window)winPtr);
    }
    if (!TkMacOSXHostToplevelExists(winPtr)) {
	TkMacOSXMakeRealWindowExist(winPtr);
    }
    if (WmSetAttribute(winPtr, TkMacOSXGetNSWindowForDrawable(winPtr->window), interp,
	    WMATT_TITLEPATH, objv[3]) != TCL_OK) {
	return TCL_ERROR;
    }
    if (!len) {
	if (wmPtr->hints.icon_pixmap != None) {
	    Tk_FreeBitmap(winPtr->display, wmPtr->hints.icon_pixmap);
	    wmPtr->hints.icon_pixmap = None;
	}
	wmPtr->hints.flags &= ~IconPixmapHint;
    } else {
	pixmap = Tk_GetBitmap(interp, (Tk_Window)winPtr, str);
	if (pixmap == None) {
	    return TCL_ERROR;
	}
	wmPtr->hints.icon_pixmap = pixmap;
	wmPtr->hints.flags |= IconPixmapHint;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconifyCmd --
 *
 *	This procedure is invoked to process the "wm iconify" Tcl command. See
 *	the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconifyCmd(
    TCL_UNUSED(Tk_Window),		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "window");
	return TCL_ERROR;
    }

    if (Tk_Attributes((Tk_Window)winPtr)->override_redirect) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't iconify \"%s\": override-redirect flag is set",
		winPtr->pathName));
	Tcl_SetErrorCode(interp, "TK", "WM", "ICONIFY", "OVERRIDE_REDIRECT",
		NULL);
	return TCL_ERROR;
    } else if (wmPtr->container != NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't iconify \"%s\": it is a transient", winPtr->pathName));
	Tcl_SetErrorCode(interp, "TK", "WM", "ICONIFY", "TRANSIENT", (char *)NULL);
	return TCL_ERROR;
    } else if (wmPtr->iconFor != NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't iconify \"%s\": it is an icon for \"%s\"",
		winPtr->pathName, Tk_PathName(wmPtr->iconFor)));
	Tcl_SetErrorCode(interp, "TK", "WM", "ICONIFY", "ICON", (char *)NULL);
	return TCL_ERROR;
    } else if (winPtr->flags & TK_EMBEDDED) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't iconify \"%s\": it is an embedded window",
		winPtr->pathName));
	Tcl_SetErrorCode(interp, "TK", "WM", "ICONIFY", "EMBEDDED", (char *)NULL);
	return TCL_ERROR;
    }

    TkpWmSetState(winPtr, IconicState);
    if (wmPtr->icon) {
	Tk_MapWindow((Tk_Window)wmPtr->icon);
    }

    /*
     * If this window has a transient the transient must be withdrawn when
     * the container is iconified.
     */

    for (Transient *transientPtr = wmPtr->transientPtr;
	    transientPtr != NULL; transientPtr = transientPtr->nextPtr) {
	TkWindow *winPtr2 = transientPtr->winPtr;
	TkWindow *containerPtr = (TkWindow *)TkMacOSXGetContainer(winPtr2);
	if (containerPtr == winPtr &&
		winPtr2->wmInfoPtr->hints.initial_state != WithdrawnState) {
	    TkpWmSetState(winPtr2, WithdrawnState);
	    transientPtr->flags |= WITHDRAWN_BY_CONTAINER;
	}
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconmaskCmd --
 *
 *	This procedure is invoked to process the "wm iconmask" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconmaskCmd(
    Tk_Window tkwin,		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    Pixmap pixmap;
    char *argv3;

    if ((objc != 3) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?bitmap?");
	return TCL_ERROR;
    }

    if (objc == 3) {
	if (wmPtr->hints.flags & IconMaskHint) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    Tk_NameOfBitmap(winPtr->display, wmPtr->hints.icon_mask),
		    -1));
	}
	return TCL_OK;
    }

    argv3 = Tcl_GetString(objv[3]);
    if (*argv3 == '\0') {
	if (wmPtr->hints.icon_mask != None) {
	    Tk_FreeBitmap(winPtr->display, wmPtr->hints.icon_mask);
	}
	wmPtr->hints.flags &= ~IconMaskHint;
    } else {
	pixmap = Tk_GetBitmap(interp, tkwin, argv3);
	if (pixmap == None) {
	    return TCL_ERROR;
	}
	wmPtr->hints.icon_mask = pixmap;
	wmPtr->hints.flags |= IconMaskHint;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconnameCmd --
 *
 *	This procedure is invoked to process the "wm iconname" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconnameCmd(
    TCL_UNUSED(Tk_Window),		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    const char *argv3;
    Tcl_Size length;

    if (objc > 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?newName?");
	return TCL_ERROR;
    }
    if (objc == 3) {
	if (wmPtr->iconName != NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(wmPtr->iconName, TCL_INDEX_NONE));
	}
	return TCL_OK;
    }

    if (wmPtr->iconName != NULL) {
	ckfree(wmPtr->iconName);
    }
    argv3 = Tcl_GetStringFromObj(objv[3], &length);
    wmPtr->iconName = (char *)ckalloc(length + 1);
    strcpy(wmPtr->iconName, argv3);
    if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
	XSetIconName(winPtr->display, winPtr->window, wmPtr->iconName);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconphotoCmd --
 *
 *	This procedure is invoked to process the "wm iconphoto" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *
 *----------------------------------------------------------------------
 */

static int
WmIconphotoCmd(
    Tk_Window tkwin,		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Tk_Image tk_icon;
    int width, height;
    NSImage *newIcon = NULL;

    if (objc < 4) {
	Tcl_WrongNumArgs(interp, 2, objv,
			 "window ?-default? image1 ?image2 ...?");
	return TCL_ERROR;
    }

    /*
     * Parse args.
     */

    if (strcmp(Tcl_GetString(objv[3]), "-default") == 0) {
	if (objc == 4) {
	    Tcl_WrongNumArgs(interp, 2, objv,
		    "window ?-default? image1 ?image2 ...?");
	    return TCL_ERROR;
	}
    }

    /*
     * Get icon name. We only use the first icon name because macOS does not
     * support multiple images in Tk photos.
     */

    char *icon;
    if (strcmp(Tcl_GetString(objv[3]), "-default") == 0) {
	icon = Tcl_GetString(objv[4]);
    } else {
	icon = Tcl_GetString(objv[3]);
    }

    /*
     * Get image and convert to NSImage that can be displayed as icon.
     */

    tk_icon = Tk_GetImage(interp, tkwin, icon, NULL, NULL);
    if (tk_icon == NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	      "can't use \"%s\" as iconphoto: not a photo image",
	      icon));
	Tcl_SetErrorCode(interp, "TK", "WM", "ICONPHOTO", "PHOTO", (char *)NULL);
	return TCL_ERROR;
    }

    Tk_SizeOfImage(tk_icon, &width, &height);
    if (width != 0 && height != 0) {
	newIcon = TkMacOSXGetNSImageFromTkImage(winPtr->display, tk_icon,
						width, height);
    }
    Tk_FreeImage(tk_icon);
    if (newIcon == NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "failed to create an iconphoto with image \"%s\"", icon));
	Tcl_SetErrorCode(interp, "TK", "WM", "ICONPHOTO", "IMAGE", (char *)NULL);
	return TCL_ERROR;
    }
    [NSApp setApplicationIconImage: newIcon];
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconpositionCmd --
 *
 *	This procedure is invoked to process the "wm iconposition" Tcl
 *	command. See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconpositionCmd(
    TCL_UNUSED(Tk_Window),		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int x, y;

    if ((objc != 3) && (objc != 5)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?x y?");
	return TCL_ERROR;
    }

    if (objc == 3) {
	if (wmPtr->hints.flags & IconPositionHint) {
	    Tcl_Obj *results[2];

	    results[0] = Tcl_NewWideIntObj(wmPtr->hints.icon_x);
	    results[1] = Tcl_NewWideIntObj(wmPtr->hints.icon_y);
	    Tcl_SetObjResult(interp, Tcl_NewListObj(2, results));
	}
	return TCL_OK;
    }

    if (*Tcl_GetString(objv[3]) == '\0') {
	wmPtr->hints.flags &= ~IconPositionHint;
    } else {
	if ((Tcl_GetIntFromObj(interp, objv[3], &x) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[4], &y) != TCL_OK)) {
	    return TCL_ERROR;
	}
	wmPtr->hints.icon_x = x;
	wmPtr->hints.icon_y = y;
	wmPtr->hints.flags |= IconPositionHint;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmIconwindowCmd --
 *
 *	This procedure is invoked to process the "wm iconwindow" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmIconwindowCmd(
    Tk_Window tkwin,		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    Tk_Window tkwin2;
    WmInfo *wmPtr2;

    if ((objc != 3) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?pathName?");
	return TCL_ERROR;
    }

    if (objc == 3) {
	if (wmPtr->icon != NULL) {
	    Tcl_SetObjResult(interp, Tk_NewWindowObj(wmPtr->icon));
	}
	return TCL_OK;
    }

    if (*Tcl_GetString(objv[3]) == '\0') {
	wmPtr->hints.flags &= ~IconWindowHint;
	if (wmPtr->icon != NULL) {
	    wmPtr2 = ((TkWindow *)wmPtr->icon)->wmInfoPtr;
	    wmPtr2->iconFor = NULL;
	    wmPtr2->hints.initial_state = WithdrawnState;
	}
	wmPtr->icon = NULL;
    } else {
	if (TkGetWindowFromObj(interp, tkwin, objv[3], &tkwin2) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (!Tk_IsTopLevel(tkwin2)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "can't use %s as icon window: not at top level",
		    Tk_PathName(tkwin2)));
	    Tcl_SetErrorCode(interp, "TK", "WM", "ICONWINDOW", "TOPLEVEL",
		    NULL);
	    return TCL_ERROR;
	}
	wmPtr2 = ((TkWindow *)tkwin2)->wmInfoPtr;
	if (wmPtr2->iconFor != NULL) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "%s is already an icon for %s",
		    Tcl_GetString(objv[3]), Tk_PathName(wmPtr2->iconFor)));
	    Tcl_SetErrorCode(interp, "TK", "WM", "ICONWINDOW", "ICON", (char *)NULL);
	    return TCL_ERROR;
	}
	if (wmPtr->icon != NULL) {
	    NSWindow *win = nil;
	    TkWindow *oldIcon = (TkWindow *)wmPtr->icon;
	    if (winPtr && winPtr->window) {
		win = TkMacOSXGetNSWindowForDrawable(winPtr->window);
	    }
	    /*
	     * The old icon should be withdrawn.
	     */
	    if (oldIcon) {
		WmInfo *wmPtr3 = oldIcon->wmInfoPtr;
		TkpWmSetState(oldIcon, WithdrawnState);
		if (wmPtr3) {
		    wmPtr3->iconFor = NULL;
		}
	    }
	    [win orderOut:NSApp];
	    [win setExcludedFromWindowsMenu:YES];
	}
	Tk_MakeWindowExist(tkwin2);
	wmPtr->hints.icon_window = Tk_WindowId(tkwin2);
	wmPtr->hints.flags |= IconWindowHint;
	wmPtr->icon = tkwin2;
	wmPtr2->iconFor = (Tk_Window)winPtr;
	if (!(wmPtr2->flags & WM_NEVER_MAPPED)) {
	    /*
	     * If the window is in normal or zoomed state, the icon should be
	     * unmapped.
	     */

	    if (wmPtr->hints.initial_state == NormalState ||
		    wmPtr->hints.initial_state == ZoomState) {
		Tk_UnmapWindow(tkwin2);
	    }
	}
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmManageCmd --
 *
 *	This procedure is invoked to process the "wm manage" Tcl command. See
 *	the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmManageCmd(
    TCL_UNUSED(Tk_Window),			/* Main window of the application. */
    TkWindow *winPtr,			/* Toplevel or Frame to work with */
    Tcl_Interp *interp,			/* Current interpreter. */
    TCL_UNUSED(Tcl_Size),			/* Number of arguments. */
    TCL_UNUSED(Tcl_Obj *const *))	/* Argument objects. */
{
    Tk_Window frameWin = (Tk_Window)winPtr;
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    if (!Tk_IsTopLevel(frameWin)) {
	MacDrawable *macWin = (MacDrawable *)winPtr->window;

	if (!Tk_IsManageable(frameWin)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "window \"%s\" is not manageable: must be a"
		    " frame, labelframe or toplevel",
		    Tk_PathName(frameWin)));
	    Tcl_SetErrorCode(interp, "TK", "WM", "MANAGE", (char *)NULL);
	    return TCL_ERROR;
	}

	/*
	 * Draw the managed widget at the top left corner of its toplevel.
	 * See [4a40c6cace].
	 */

	if (macWin) {
	    winPtr->changes.x -= macWin->xOff;
	    winPtr->changes.y -= macWin->yOff;
	    XMoveWindow(winPtr->display, winPtr->window, 0, 0);
	}

	TkFocusSplit(winPtr);
	Tk_UnmapWindow(frameWin);
	if (wmPtr == NULL) {
	    TkWmNewWindow(winPtr);
	    if (winPtr->window == None) {
		Tk_MakeWindowExist((Tk_Window)winPtr);
		macWin = (MacDrawable *)winPtr->window;
	    }
	}
	wmPtr = winPtr->wmInfoPtr;
	winPtr->flags &= ~TK_MAPPED;
	macWin->toplevel->referenceCount--;
	macWin->toplevel = macWin;
	macWin->toplevel->referenceCount++;
	RemapWindows(winPtr, macWin);
	winPtr->flags |=
		(TK_TOP_HIERARCHY|TK_TOP_LEVEL|TK_HAS_WRAPPER|TK_WIN_MANAGED);
	TkMapTopFrame(frameWin);
	TkWmMapWindow(winPtr);
    } else if (Tk_IsTopLevel(frameWin)) {
	/* Already managed by wm - ignore it */
    }
    Tk_ManageGeometry((Tk_Window)winPtr, &wmMgrType, NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmMaxsizeCmd --
 *
 *	This procedure is invoked to process the "wm maxsize" Tcl command. See
 *	the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmMaxsizeCmd(
    Tk_Window tkwin,	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int width, height;

    if ((objc != 3) && (objc != 5)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?width height?");
	return TCL_ERROR;
    }

    if (objc == 3) {
	Tcl_Obj *results[2];

	GetMaxSize(winPtr, &width, &height);
	results[0] = Tcl_NewWideIntObj(width);
	results[1] = Tcl_NewWideIntObj(height);
	Tcl_SetObjResult(interp, Tcl_NewListObj(2, results));
	return TCL_OK;
    }

    if ((Tk_GetPixelsFromObj(interp, tkwin, objv[3], &width) != TCL_OK)
	    || (Tk_GetPixelsFromObj(interp, tkwin, objv[4], &height) != TCL_OK)) {
	return TCL_ERROR;
    }
    wmPtr->maxWidth = width;
    wmPtr->maxHeight = height;
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmMinsizeCmd --
 *
 *	This procedure is invoked to process the "wm minsize" Tcl command. See
 *	the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmMinsizeCmd(
    Tk_Window tkwin,		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int width, height;

    if ((objc != 3) && (objc != 5)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?width height?");
	return TCL_ERROR;
    }

    if (objc == 3) {
	Tcl_Obj *results[2];

	GetMinSize(winPtr, &width, &height);
	results[0] = Tcl_NewWideIntObj(width);
	results[1] = Tcl_NewWideIntObj(height);
	Tcl_SetObjResult(interp, Tcl_NewListObj(2, results));
	return TCL_OK;
    }

    if ((Tk_GetPixelsFromObj(interp, tkwin, objv[3], &width) != TCL_OK)
	    || (Tk_GetPixelsFromObj(interp, tkwin, objv[4], &height) != TCL_OK)) {
	return TCL_ERROR;
    }
    wmPtr->minWidth = width;
    wmPtr->minHeight = height;
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmOverrideredirectCmd --
 *
 *	This procedure is invoked to process the "wm overrideredirect" Tcl
 *	command. See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmOverrideredirectCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Bool boolValue;
    XSetWindowAttributes atts;
    NSWindow *win = nil;
    if (winPtr && winPtr->window) {
	win = TkMacOSXGetNSWindowForDrawable(winPtr->window);
    }


    if ((objc != 3) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?boolean?");
	return TCL_ERROR;
    }

    if (objc == 3) {
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(
		Tk_Attributes((Tk_Window)winPtr)->override_redirect));
	return TCL_OK;
    }

    if (Tcl_GetBooleanFromObj(interp, objv[3], &boolValue) != TCL_OK) {
	return TCL_ERROR;
    }
    atts.override_redirect = boolValue;
    Tk_ChangeWindowAttributes((Tk_Window)winPtr, CWOverrideRedirect, &atts);
    if ([NSApp macOSVersion] >= 101300) {
	if (boolValue) {
	    win.styleMask |= NSWindowStyleMaskDocModalWindow;
	} else {
	    win.styleMask &= ~NSWindowStyleMaskDocModalWindow;
	}
    } else {
	ApplyContainerOverrideChanges(winPtr, win);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmPositionfromCmd --
 *
 *	This procedure is invoked to process the "wm positionfrom" Tcl
 *	command. See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmPositionfromCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    static const char *const optionStrings[] = {
	"program", "user", NULL };
    enum options {
	OPT_PROGRAM, OPT_USER };
    int index;

    if ((objc != 3) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?user/program?");
	return TCL_ERROR;
    }

    if (objc == 3) {
	if (wmPtr->sizeHintsFlags & USPosition) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("user", TCL_INDEX_NONE));
	} else if (wmPtr->sizeHintsFlags & PPosition) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("program", TCL_INDEX_NONE));
	}
	return TCL_OK;
    }

    if (*Tcl_GetString(objv[3]) == '\0') {
	wmPtr->sizeHintsFlags &= ~(USPosition|PPosition);
    } else {
	if (Tcl_GetIndexFromObjStruct(interp, objv[3], optionStrings,
		sizeof(char *), "argument", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (index == OPT_USER) {
	    wmPtr->sizeHintsFlags &= ~PPosition;
	    wmPtr->sizeHintsFlags |= USPosition;
	} else {
	    wmPtr->sizeHintsFlags &= ~USPosition;
	    wmPtr->sizeHintsFlags |= PPosition;
	}
    }
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmProtocolCmd --
 *
 *	This procedure is invoked to process the "wm protocol" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmProtocolCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    ProtocolHandler *protPtr, *prevPtr;
    Atom protocol;
    Tcl_Obj *resultObj;

    if ((objc < 3) || (objc > 5)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?name? ?command?");
	return TCL_ERROR;
    }

    if (objc == 3) {
	/*
	 * Return a list of all defined protocols for the window.
	 */

	resultObj = Tcl_NewObj();
	for (protPtr = wmPtr->protPtr; protPtr != NULL;
		protPtr = protPtr->nextPtr) {
	    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(
		    Tk_GetAtomName((Tk_Window)winPtr, protPtr->protocol),-1));
	}
	Tcl_SetObjResult(interp, resultObj);
	return TCL_OK;
    }

    protocol = Tk_InternAtom((Tk_Window)winPtr, Tcl_GetString(objv[3]));
    if (objc == 4) {
	/*
	 * Return the command to handle a given protocol.
	 */

	for (protPtr = wmPtr->protPtr; protPtr != NULL;
		protPtr = protPtr->nextPtr) {
	    if (protPtr->protocol == protocol) {
		Tcl_SetObjResult(interp, protPtr->commandObj);
		return TCL_OK;
	    }
	}
	return TCL_OK;
    }

    /*
     * Delete any current protocol handler, then create a new one with the
     * specified command, unless the command is empty.
     */

    for (protPtr = wmPtr->protPtr, prevPtr = NULL; protPtr != NULL;
	    prevPtr = protPtr, protPtr = protPtr->nextPtr) {
	if (protPtr->protocol == protocol) {
	    if (prevPtr == NULL) {
		wmPtr->protPtr = protPtr->nextPtr;
	    } else {
		prevPtr->nextPtr = protPtr->nextPtr;
	    }
	    if (protPtr->commandObj)
		Tcl_DecrRefCount(protPtr->commandObj);
	    Tcl_EventuallyFree(protPtr, TCL_DYNAMIC);
	    break;
	}
    }
    if (Tcl_GetString(objv[4])[0]) {
	protPtr = (ProtocolHandler *)ckalloc(sizeof(ProtocolHandler));
	protPtr->protocol = protocol;
	protPtr->nextPtr = wmPtr->protPtr;
	wmPtr->protPtr = protPtr;
	protPtr->interp = interp;
	protPtr->commandObj = objv[4];
	Tcl_IncrRefCount(protPtr->commandObj);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmResizableCmd --
 *
 *	This procedure is invoked to process the "wm resizable" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmResizableCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int width, height;
    UInt64 oldAttributes = wmPtr->attributes;
    int oldFlags = wmPtr->flags;

    if ((objc != 3) && (objc != 5)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?width height?");
	return TCL_ERROR;
    }

    if (objc == 3) {
	Tcl_Obj *results[2];

	results[0] = Tcl_NewBooleanObj(!(wmPtr->flags & WM_WIDTH_NOT_RESIZABLE));
	results[1] = Tcl_NewBooleanObj(!(wmPtr->flags & WM_HEIGHT_NOT_RESIZABLE));
	Tcl_SetObjResult(interp, Tcl_NewListObj(2, results));
	return TCL_OK;
    }

    if ((Tcl_GetBooleanFromObj(interp, objv[3], &width) != TCL_OK)
	    || (Tcl_GetBooleanFromObj(interp, objv[4], &height) != TCL_OK)) {
	return TCL_ERROR;
    }
    if (width) {
	wmPtr->flags &= ~WM_WIDTH_NOT_RESIZABLE;
	wmPtr->attributes |= kWindowHorizontalZoomAttribute;
    } else {
	wmPtr->flags |= WM_WIDTH_NOT_RESIZABLE;
	wmPtr->attributes &= ~kWindowHorizontalZoomAttribute;
    }
    if (height) {
	wmPtr->flags &= ~WM_HEIGHT_NOT_RESIZABLE;
	wmPtr->attributes |= kWindowVerticalZoomAttribute;
    } else {
	wmPtr->flags |= WM_HEIGHT_NOT_RESIZABLE;
	wmPtr->attributes &= ~kWindowVerticalZoomAttribute;
    }
    if (width || height) {
	wmPtr->attributes |= kWindowResizableAttribute;
    } else {
	wmPtr->attributes &= ~kWindowResizableAttribute;
    }
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    if (wmPtr->scrollWinPtr != NULL) {
	TkScrollbarEventuallyRedraw((TkScrollbar *)
		wmPtr->scrollWinPtr->instanceData);
    }
    WmUpdateGeom(wmPtr, winPtr);
    ApplyWindowAttributeFlagChanges(winPtr, NULL, oldAttributes, oldFlags, 1,0);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmSizefromCmd --
 *
 *	This procedure is invoked to process the "wm sizefrom" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmSizefromCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    static const char *const optionStrings[] = {
	"program", "user", NULL };
    enum options {
	OPT_PROGRAM, OPT_USER };
    int index;

    if ((objc != 3) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?user|program?");
	return TCL_ERROR;
    }

    if (objc == 3) {
	if (wmPtr->sizeHintsFlags & USSize) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("user", TCL_INDEX_NONE));
	} else if (wmPtr->sizeHintsFlags & PSize) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("program", TCL_INDEX_NONE));
	}
	return TCL_OK;
    }

    if (*Tcl_GetString(objv[3]) == '\0') {
	wmPtr->sizeHintsFlags &= ~(USSize|PSize);
    } else {
	if (Tcl_GetIndexFromObjStruct(interp, objv[3], optionStrings,
		sizeof(char *), "argument", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (index == OPT_USER) {
	    wmPtr->sizeHintsFlags &= ~PSize;
	    wmPtr->sizeHintsFlags |= USSize;
	} else { /* OPT_PROGRAM */
	    wmPtr->sizeHintsFlags &= ~USSize;
	    wmPtr->sizeHintsFlags |= PSize;
	}
    }
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    WmUpdateGeom(wmPtr, winPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmStackorderCmd --
 *
 *	This procedure is invoked to process the "wm stackorder" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmStackorderCmd(
    Tk_Window tkwin,		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    TkWindow **windows, **windowPtr;
    static const char *const optionStrings[] = {
	"isabove", "isbelow", NULL
    };
    enum options {
	OPT_ISABOVE, OPT_ISBELOW
    };
    Tcl_Obj *resultObj;
    int index;

    if ((objc != 3) && (objc != 5)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?isabove|isbelow window?");
	return TCL_ERROR;
    }

    if (objc == 3) {
	windows = TkWmStackorderToplevel(winPtr);
	if (windows != NULL) {
	    resultObj = Tcl_NewObj();
	    for (windowPtr = windows; *windowPtr ; windowPtr++) {
		Tcl_ListObjAppendElement(NULL, resultObj,
			Tk_NewWindowObj((Tk_Window)*windowPtr));
	    }
	    Tcl_SetObjResult(interp, resultObj);
	    ckfree(windows);
	    return TCL_OK;
	} else {
	    return TCL_ERROR;
	}
    } else {
	TkWindow *winPtr2;
	int index1 = -1, index2 = -1, result;

	if (TkGetWindowFromObj(interp, tkwin, objv[4], (Tk_Window *) &winPtr2)
		!= TCL_OK) {
	    return TCL_ERROR;
	}

	if (!Tk_IsTopLevel(winPtr2)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "window \"%s\" isn't a top-level window",
		    winPtr2->pathName));
	    Tcl_SetErrorCode(interp, "TK", "WM", "STACK", "TOPLEVEL", (char *)NULL);
	    return TCL_ERROR;
	}

	if (!Tk_IsMapped(winPtr)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "window \"%s\" isn't mapped", winPtr->pathName));
	    Tcl_SetErrorCode(interp, "TK", "WM", "STACK", "MAPPED", (char *)NULL);
	    return TCL_ERROR;
	} else if (!Tk_IsMapped(winPtr2)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "window \"%s\" isn't mapped", winPtr2->pathName));
	    Tcl_SetErrorCode(interp, "TK", "WM", "STACK", "MAPPED", (char *)NULL);
	    return TCL_ERROR;
	}

	/*
	 * Lookup stacking order of all toplevels that are children of "." and
	 * find the position of winPtr and winPtr2 in the stacking order.
	 */

	windows = TkWmStackorderToplevel(winPtr->mainPtr->winPtr);
	if (windows == NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "TkWmStackorderToplevel failed", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "WM", "STACK", "FAIL", (char *)NULL);
	    return TCL_ERROR;
	}

	for (windowPtr = windows; *windowPtr ; windowPtr++) {
	    if (*windowPtr == winPtr) {
		index1 = windowPtr - windows;
	    }
	    if (*windowPtr == winPtr2) {
		index2 = windowPtr - windows;
	    }
	}
	if (index1 == -1) {
	    Tcl_Panic("winPtr window not found");
	} else if (index2 == -1) {
	    Tcl_Panic("winPtr2 window not found");
	}

	ckfree(windows);

	if (Tcl_GetIndexFromObjStruct(interp, objv[3], optionStrings,
		sizeof(char *), "argument", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (index == OPT_ISABOVE) {
	    result = index1 > index2;
	} else { /* OPT_ISBELOW */
	    result = index1 < index2;
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(result));
	return TCL_OK;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * WmStateCmd --
 *
 *	This procedure is invoked to process the "wm state" Tcl command. See
 *	the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmStateCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    static const char *const optionStrings[] = {
	"iconic", "normal", "withdrawn", "zoomed", NULL };
    enum options {
	OPT_ICONIC, OPT_NORMAL, OPT_WITHDRAWN, OPT_ZOOMED };
    int index;

    if ((objc < 3) || (objc > 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?state?");
	return TCL_ERROR;
    }

    if (objc == 4) {
	if (wmPtr->iconFor != NULL) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "can't change state of \"%s\": it is an icon for \"%s\"",
		    Tcl_GetString(objv[2]), Tk_PathName(wmPtr->iconFor)));
	    Tcl_SetErrorCode(interp, "TK", "WM", "STATE", "ICON", (char *)NULL);
	    return TCL_ERROR;
	}
	if (winPtr->flags & TK_EMBEDDED) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "can't change state of \"%s\": it is an embedded window",
		    winPtr->pathName));
	    Tcl_SetErrorCode(interp, "TK", "WM", "STATE", "EMBEDDED", (char *)NULL);
	    return TCL_ERROR;
	}

	if (Tcl_GetIndexFromObjStruct(interp, objv[3], optionStrings,
		sizeof(char *), "argument", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}

	switch (index) {
	case OPT_NORMAL:
	    TkpWmSetState(winPtr, NormalState);

	    /*
	     * This varies from 'wm deiconify' because it does not force the
	     * window to be raised and receive focus
	     */

	    break;
	case OPT_ICONIC:
	    if (Tk_Attributes((Tk_Window)winPtr)->override_redirect) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"can't iconify \"%s\": override-redirect flag is set",
			winPtr->pathName));
		Tcl_SetErrorCode(interp, "TK", "WM", "STATE",
			"OVERRIDE_REDIRECT", NULL);
		return TCL_ERROR;
	    }
	    if (wmPtr->container != NULL) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"can't iconify \"%s\": it is a transient",
			winPtr->pathName));
		Tcl_SetErrorCode(interp, "TK", "WM", "STATE", "TRANSIENT",
			NULL);
		return TCL_ERROR;
	    }
	    TkpWmSetState(winPtr, IconicState);
	    break;
	case OPT_WITHDRAWN:
	    TkpWmSetState(winPtr, WithdrawnState);
	    break;
	default: /* OPT_ZOOMED */
	    TkpWmSetState(winPtr, ZoomState);
	    break;
	}
    } else if (wmPtr->iconFor != NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("icon", TCL_INDEX_NONE));
    } else {
	if (wmPtr->hints.initial_state == NormalState ||
		wmPtr->hints.initial_state == ZoomState) {
	    wmPtr->hints.initial_state = (TkMacOSXIsWindowZoomed(winPtr) ?
		    ZoomState : NormalState);
	}
	switch (wmPtr->hints.initial_state) {
	case NormalState:
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("normal", TCL_INDEX_NONE));
	    break;
	case IconicState:
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("iconic", TCL_INDEX_NONE));
	    break;
	case WithdrawnState:
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("withdrawn", TCL_INDEX_NONE));
	    break;
	case ZoomState:
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("zoomed", TCL_INDEX_NONE));
	    break;
	}
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmTitleCmd --
 *
 *	This procedure is invoked to process the "wm title" Tcl command. See
 *	the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmTitleCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    char *argv3;
    Tcl_Size length;

    if (objc > 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?newTitle?");
	return TCL_ERROR;
    }

    if (objc == 3) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		wmPtr->titleUid ? wmPtr->titleUid : winPtr->nameUid, TCL_INDEX_NONE));
	return TCL_OK;
    }

    argv3 = Tcl_GetStringFromObj(objv[3], &length);
    wmPtr->titleUid = Tk_GetUid(argv3);
    if (!(wmPtr->flags & WM_NEVER_MAPPED) && !Tk_IsEmbedded(winPtr)) {
	TkSetWMName(winPtr, wmPtr->titleUid);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmTransientCmd --
 *
 *	This procedure is invoked to process the "wm transient" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmTransientCmd(
    Tk_Window tkwin,		/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    Tk_Window container;
    TkWindow *containerPtr, *w;
    WmInfo *wmPtr2;
    Transient *transient;

    if ((objc != 3) && (objc != 4)) {
	Tcl_WrongNumArgs(interp, 2, objv, "window ?window?");
	return TCL_ERROR;
    }
    if (objc == 3) {
	if (wmPtr->container != NULL) {
	    Tcl_SetObjResult(interp,
		Tcl_NewStringObj(Tk_PathName(wmPtr->container), TCL_INDEX_NONE));
	}
	return TCL_OK;
    }
    if (*Tcl_GetString(objv[3]) == '\0') {
	RemoveTransient(winPtr);
    } else {
	if (TkGetWindowFromObj(interp, tkwin, objv[3], &container) != TCL_OK) {
	    return TCL_ERROR;
	}
	RemoveTransient(winPtr);
	containerPtr = (TkWindow*) container;
	while (!Tk_TopWinHierarchy(containerPtr)) {
	    /*
	     * Ensure that the container window is actually a Tk toplevel.
	     */

	    containerPtr = containerPtr->parentPtr;
	}
	Tk_MakeWindowExist((Tk_Window)containerPtr);

	if (wmPtr->iconFor != NULL) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "can't make \"%s\" a transient: it is an icon for %s",
		    Tcl_GetString(objv[2]), Tk_PathName(wmPtr->iconFor)));
	    Tcl_SetErrorCode(interp, "TK", "WM", "TRANSIENT", "ICON", (char *)NULL);
	    return TCL_ERROR;
	}

	wmPtr2 = containerPtr->wmInfoPtr;

	/*
	 * Under some circumstances, wmPtr2 is NULL here.
	 */

	if (wmPtr2 != NULL && wmPtr2->iconFor != NULL) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "can't make \"%s\" a container: it is an icon for %s",
		    Tcl_GetString(objv[3]), Tk_PathName(wmPtr2->iconFor)));
	    Tcl_SetErrorCode(interp, "TK", "WM", "TRANSIENT", "ICON", (char *)NULL);
	    return TCL_ERROR;
	}

	for (w = containerPtr; w != NULL && w->wmInfoPtr != NULL;
	    w = (TkWindow *)w->wmInfoPtr->container) {
	    if (w == winPtr) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "can't set \"%s\" as container: would cause management loop",
		    Tk_PathName(containerPtr)));
		Tcl_SetErrorCode(interp, "TK", "WM", "TRANSIENT", "SELF", (char *)NULL);
		return TCL_ERROR;
	    }
	}

	/*
	 * Add the transient to the container's list, if it not already there.
	 */

	for (transient = wmPtr2->transientPtr;
	     transient != NULL && transient->winPtr != winPtr;
	     transient = transient->nextPtr) {}
	if (transient == NULL) {
	    transient = (Transient *)ckalloc(sizeof(Transient));
	    transient->winPtr = winPtr;
	    transient->flags = 0;
	    transient->nextPtr = wmPtr2->transientPtr;
	    wmPtr2->transientPtr = transient;
	}

	/*
	 * If the container is withdrawn or iconic then withdraw the transient.
	 */

	if ((wmPtr2->hints.initial_state == WithdrawnState ||
		wmPtr2->hints.initial_state == IconicState) &&
		wmPtr->hints.initial_state != WithdrawnState) {
	    TkpWmSetState(winPtr, WithdrawnState);
	    transient->flags |= WITHDRAWN_BY_CONTAINER;
	}

	wmPtr->container = (Tk_Window)containerPtr;
    }
    ApplyContainerOverrideChanges(winPtr, NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RemoveTransient --
 *
 *      Clears the transient's container record and removes the transient from the
 *      container's list.
 *
 * Results:
 *	None
 *
 * Side effects:
 *      References to a container are removed from the transient's wmInfo
 *	structure and references to the transient are removed from its container's
 *	wmInfo.
 *
 *----------------------------------------------------------------------
 */

static void
RemoveTransient(
    TkWindow *winPtr)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr, *wmPtr2;
    TkWindow *containerPtr;
    Transient *transPtr, *temp;

    if (wmPtr == NULL || wmPtr->container == NULL) {
	return;
    }
    containerPtr = (TkWindow *)wmPtr->container;
    wmPtr2 = containerPtr->wmInfoPtr;
    if (wmPtr2 == NULL) {
	return;
    }
    wmPtr->container= NULL;
    transPtr = wmPtr2->transientPtr;
    while (transPtr != NULL) {
	if (transPtr->winPtr != winPtr) {
	    break;
	}
	temp = transPtr->nextPtr;
	ckfree(transPtr);
	transPtr = temp;
    }
    wmPtr2->transientPtr = transPtr;
    while (transPtr != NULL) {
	if (transPtr->nextPtr && transPtr->nextPtr->winPtr == winPtr) {
	    temp = transPtr->nextPtr;
	    transPtr->nextPtr = temp->nextPtr;
	    ckfree(temp);
	} else {
	    transPtr = transPtr->nextPtr;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * WmWithdrawCmd --
 *
 *	This procedure is invoked to process the "wm withdraw" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
WmWithdrawCmd(
    TCL_UNUSED(Tk_Window),	/* Main window of the application. */
    TkWindow *winPtr,		/* Toplevel to work with */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "window");
	return TCL_ERROR;
    }

    if (wmPtr->iconFor != NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"can't withdraw %s: it is an icon for %s",
		Tcl_GetString(objv[2]), Tk_PathName(wmPtr->iconFor)));
	Tcl_SetErrorCode(interp, "TK", "WM", "WITHDRAW", "ICON", (char *)NULL);
	return TCL_ERROR;
    }

    TkpWmSetState(winPtr, WithdrawnState);

    /*
     * If this window has a transient, the transient must also be withdrawn.
     */

    for (Transient *transientPtr = wmPtr->transientPtr;
	    transientPtr != NULL; transientPtr = transientPtr->nextPtr) {
	TkWindow *winPtr2 = transientPtr->winPtr;
	TkWindow *containerPtr = (TkWindow *)TkMacOSXGetContainer(winPtr2);

	if (containerPtr == winPtr &&
		winPtr2->wmInfoPtr->hints.initial_state != WithdrawnState) {
	    TkpWmSetState(winPtr2, WithdrawnState);
	    transientPtr->flags |= WITHDRAWN_BY_CONTAINER;
	}
    }

    return TCL_OK;
}

/*
 * Invoked by those wm subcommands that affect geometry.  Schedules a geometry
 * update.
 */

static void
WmUpdateGeom(
    WmInfo *wmPtr,
    TkWindow *winPtr)
{
    if (!(wmPtr->flags & (WM_UPDATE_PENDING|WM_NEVER_MAPPED))) {
	Tcl_DoWhenIdle(UpdateGeometryInfo, winPtr);
	wmPtr->flags |= WM_UPDATE_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetGrid --
 *
 *	This procedure is invoked by a widget when it wishes to set a grid
 *	coordinate system that controls the size of a top-level window. It
 *	provides a C interface equivalent to the "wm grid" command and is
 *	usually asscoiated with the -setgrid option.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Grid-related information will be passed to the window manager, so that
 *	the top-level window associated with tkwin will resize on even grid
 *	units. If some other window already controls gridding for the
 *	top-level window then this procedure call has no effect.
 *
 *----------------------------------------------------------------------
 */

void
Tk_SetGrid(
    Tk_Window tkwin,		/* Token for window. New window mgr info will
				 * be posted for the top-level window
				 * associated with this window. */
    int reqWidth,		/* Width (in grid units) corresponding to the
				 * requested geometry for tkwin. */
    int reqHeight,		/* Height (in grid units) corresponding to the
				 * requested geometry for tkwin. */
    int widthInc, int heightInc)/* Pixel increments corresponding to a change
				 * of one grid unit. */
{
    TkWindow *winPtr = (TkWindow *)tkwin;
    WmInfo *wmPtr;

    /*
     * Ensure widthInc and heightInc are greater than 0
     */

    if (widthInc <= 0) {
	widthInc = 1;
    }
    if (heightInc <= 0) {
	heightInc = 1;
    }

    /*
     * Find the top-level window for tkwin, plus the window manager
     * information.
     */

    while (!(winPtr->flags & TK_TOP_LEVEL)) {
	winPtr = winPtr->parentPtr;
    }
    wmPtr = winPtr->wmInfoPtr;

    if ((wmPtr->gridWin != NULL) && (wmPtr->gridWin != tkwin)) {
	return;
    }

    if ((wmPtr->reqGridWidth == reqWidth)
	    && (wmPtr->reqGridHeight == reqHeight)
	    && (wmPtr->widthInc == widthInc)
	    && (wmPtr->heightInc == heightInc)
	    && ((wmPtr->sizeHintsFlags & PBaseSize) == PBaseSize)) {
	return;
    }

    /*
     * If gridding was previously off, then forget about any window size
     * requests made by the user or via "wm geometry": these are in pixel units
     * and there's no easy way to translate them to grid units since the new
     * requested size of the top-level window in pixels may not yet have been
     * registered yet (it may filter up the hierarchy in DoWhenIdle handlers).
     * However, if the window has never been mapped yet then just leave the
     * window size alone: assume that it is intended to be in grid units but
     * just happened to have been specified before this procedure was called.
     */

    if ((wmPtr->gridWin == NULL) && !(wmPtr->flags & WM_NEVER_MAPPED)) {
	wmPtr->width = -1;
	wmPtr->height = -1;
    }

    /*
     * Set the new gridding information, and start the process of passing all
     * of this information to the window manager.
     */

    wmPtr->gridWin = tkwin;
    wmPtr->reqGridWidth = reqWidth;
    wmPtr->reqGridHeight = reqHeight;
    wmPtr->widthInc = widthInc;
    wmPtr->heightInc = heightInc;
    wmPtr->sizeHintsFlags |= PBaseSize;
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    if (!(wmPtr->flags & (WM_UPDATE_PENDING|WM_NEVER_MAPPED))) {
	Tcl_DoWhenIdle(UpdateGeometryInfo, winPtr);
	wmPtr->flags |= WM_UPDATE_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_UnsetGrid --
 *
 *	This procedure cancels the effect of a previous call to Tk_SetGrid.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If tkwin currently controls gridding for its top-level window,
 *	gridding is cancelled for that top-level window; if some other window
 *	controls gridding then this procedure has no effect.
 *
 *----------------------------------------------------------------------
 */

void
Tk_UnsetGrid(
    Tk_Window tkwin)		/* Token for window that is currently
				 * controlling gridding. */
{
    TkWindow *winPtr = (TkWindow *)tkwin;
    WmInfo *wmPtr;

    /*
     * Find the top-level window for tkwin, plus the window manager
     * information.
     */

    while (!(winPtr->flags & TK_TOP_LEVEL)) {
	winPtr = winPtr->parentPtr;
    }
    wmPtr = winPtr->wmInfoPtr;
    if (tkwin != wmPtr->gridWin) {
	return;
    }

    wmPtr->gridWin = NULL;
    wmPtr->sizeHintsFlags &= ~PBaseSize;
    if (wmPtr->width != -1) {
	wmPtr->width = winPtr->reqWidth + (wmPtr->width
		- wmPtr->reqGridWidth)*wmPtr->widthInc;
	wmPtr->height = winPtr->reqHeight + (wmPtr->height
		- wmPtr->reqGridHeight)*wmPtr->heightInc;
    }
    wmPtr->widthInc = 1;
    wmPtr->heightInc = 1;

    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    if (!(wmPtr->flags & (WM_UPDATE_PENDING|WM_NEVER_MAPPED))) {
	Tcl_DoWhenIdle(UpdateGeometryInfo, winPtr);
	wmPtr->flags |= WM_UPDATE_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TopLevelEventProc --
 *
 *	This procedure is invoked when a top-level (or other externally-
 *	managed window) is restructured in any way.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Tk's internal data structures for the window get modified to reflect
 *	the structural change.
 *
 *----------------------------------------------------------------------
 */

static void
TopLevelEventProc(
    void *clientData,	/* Window for which event occurred. */
    XEvent *eventPtr)		/* Event that just happened. */
{
    TkWindow *winPtr = (TkWindow *)clientData;

    winPtr->wmInfoPtr->flags |= WM_VROOT_OFFSET_STALE;
    if (eventPtr->type == DestroyNotify) {
	if (!(winPtr->flags & TK_ALREADY_DEAD)) {
	    /*
	     * A top-level window was deleted externally (e.g., by the window
	     * manager). This is probably not a good thing, but cleanup as
	     * best we can. The error handler is needed because
	     * Tk_DestroyWindow will try to destroy the window, but of course
	     * it's already gone.
	     */

	    Tk_ErrorHandler handler = Tk_CreateErrorHandler(winPtr->display,
		    -1, -1, -1, NULL, NULL);

	    Tk_DestroyWindow((Tk_Window)winPtr);
	    Tk_DeleteErrorHandler(handler);
	}
	if (wmTracing) {
	    TkMacOSXDbgMsg("TopLevelEventProc: %s deleted", winPtr->pathName);
	}
    } else if (eventPtr->type == ReparentNotify) {
	Tcl_Panic("received unwanted reparent event");
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TopLevelReqProc --
 *
 *	This procedure is invoked by the geometry manager whenever the
 *	requested size for a top-level window is changed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Arrange for the window to be resized to satisfy the request (this
 *	happens as a when-idle action).
 *
 *----------------------------------------------------------------------
 */

static void
TopLevelReqProc(
    TCL_UNUSED(void *),		/* Not used. */
    Tk_Window tkwin)		/* Information about window. */
{
    TkWindow *winPtr = (TkWindow *)tkwin;
    WmInfo *wmPtr;

    wmPtr = winPtr->wmInfoPtr;
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    if (!(wmPtr->flags & (WM_UPDATE_PENDING|WM_NEVER_MAPPED))) {
	Tcl_DoWhenIdle(UpdateGeometryInfo, winPtr);
	wmPtr->flags |= WM_UPDATE_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateGeometryInfo --
 *
 *	This procedure is invoked when a top-level window is first mapped, and
 *	also as a when-idle procedure, to bring the geometry and/or position
 *	of a top-level window back into line with what has been requested by
 *	the user and/or widgets. This procedure doesn't return until the
 *	window manager has responded to the geometry change.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window's size and location may change, unless the WM prevents that
 *	from happening.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateGeometryInfo(
    void *clientData)	/* Pointer to the window's record. */
{
    TkWindow *winPtr = (TkWindow *)clientData;
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int x, y, width, height, min, max;

    wmPtr->flags &= ~WM_UPDATE_PENDING;

    if (wmPtr->flags & WM_FULLSCREEN) {
	return;
    }

    /*
     * Compute the new size for the top-level window. See the user
     * documentation for details on this, but the size requested depends on
     * (a) the size requested internally by the window's widgets, (b) the size
     * requested by the user in a "wm geometry" command or via wm-based
     * interactive resizing (if any), and (c) whether or not the window is
     * gridded. Don't permit sizes <= 0 because this upsets the X server.
     */

    if (wmPtr->width == -1) {
	width = winPtr->reqWidth;
    } else if (wmPtr->gridWin != NULL) {
	width = winPtr->reqWidth
		+ (wmPtr->width - wmPtr->reqGridWidth)*wmPtr->widthInc;
    } else {
	width = wmPtr->width;
    }
    if (width <= 0) {
	width = 1;
    }

    /*
     * Account for window max/min width
     */

    if (wmPtr->gridWin != NULL) {
	min = winPtr->reqWidth
		+ (wmPtr->minWidth - wmPtr->reqGridWidth)*wmPtr->widthInc;
	if (wmPtr->maxWidth > 0) {
	    max = winPtr->reqWidth
		    + (wmPtr->maxWidth - wmPtr->reqGridWidth)*wmPtr->widthInc;
	} else {
	    max = 0;
	}
    } else {
	min = wmPtr->minWidth;
	max = wmPtr->maxWidth;
    }
    if (width < min) {
	width = min;
    } else if ((max > 0) && (width > max)) {
	width = max;
    }

    if (wmPtr->height == -1) {
	height = winPtr->reqHeight;
    } else if (wmPtr->gridWin != NULL) {
	height = winPtr->reqHeight
		+ (wmPtr->height - wmPtr->reqGridHeight)*wmPtr->heightInc;
    } else {
	height = wmPtr->height;
    }
    if (height <= 0) {
	height = 1;
    }

    /*
     * Account for window max/min height
     */

    if (wmPtr->gridWin != NULL) {
	min = winPtr->reqHeight
		+ (wmPtr->minHeight - wmPtr->reqGridHeight)*wmPtr->heightInc;
	if (wmPtr->maxHeight > 0) {
	    max = winPtr->reqHeight
		    + (wmPtr->maxHeight-wmPtr->reqGridHeight)*wmPtr->heightInc;
	} else {
	    max = 0;
	}
    } else {
	min = wmPtr->minHeight;
	max = wmPtr->maxHeight;
    }
    if (height < min) {
	height = min;
    } else if ((max > 0) && (height > max)) {
	height = max;
    }
    x = wmPtr->x;
    y = wmPtr->y;

    /*
     * If the window's size is going to change and the window is supposed to
     * not be resizable by the user, then we have to update the size hints.
     * There may also be a size-hint-update request pending from somewhere
     * else, too.
     */

    if (((width != winPtr->changes.width)
	    || (height != winPtr->changes.height))
	    && (wmPtr->gridWin == NULL)
	    && !(wmPtr->sizeHintsFlags & (PMinSize|PMaxSize))) {
	wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    }
    if (wmPtr->flags & WM_UPDATE_SIZE_HINTS) {
	UpdateSizeHints(winPtr);
    }

    /*
     * Reconfigure the window if it isn't already configured correctly. A few
     * tricky points:
     *
     * 1. If the window is embedded and the container is also in this process,
     *    don't actually reconfigure the window; just pass the desired size on
     *    to the container. Also, zero out any position information, since
     *    embedded windows are not allowed to move.
     * 2. Sometimes the window manager will give us a different size than we
     *    asked for (e.g. mwm has a minimum size for windows), so base the
     *    size check on what we *asked for* last time, not what we got.
     * 3. Don't move window unless a new position has been requested for it.
     *    This is because of "features" in some window managers (e.g. twm, as
     *    of 4/24/91) where they don't interpret coordinates according to
     *    ICCCM. Moving a window to its current location may cause it to shift
     *    position on the screen.
     */

    if (Tk_IsEmbedded(winPtr)) {
	Tk_Window contWinPtr = Tk_GetOtherWindow((Tk_Window)winPtr);

	/*
	 * TODO: Here we should handle out of process embedding.
	 */

	if (contWinPtr != NULL) {
	    /*
	     * This window is embedded and the container is also in this
	     * process, so we don't need to do anything special about the
	     * geometry, except to make sure that the desired size is known by
	     * the container. Also, zero out any position information, since
	     * embedded windows are not allowed to move.
	     */

	    wmPtr->x = wmPtr->y = 0;
	    wmPtr->flags &= ~(WM_NEGATIVE_X|WM_NEGATIVE_Y);
	    Tk_GeometryRequest(contWinPtr, width, height);
	}
	return;
    }
    if (wmPtr->flags & WM_MOVE_PENDING) {
	wmPtr->configWidth = width;
	wmPtr->configHeight = height;
	if (wmTracing) {
	    TkMacOSXDbgMsg("Moving to %d %d, resizing to %d x %d", x, y,
		    width, height);
	}
	SetWindowSizeLimits(winPtr);
	wmPtr->flags |= WM_SYNC_PENDING;
	XMoveResizeWindow(winPtr->display, winPtr->window, x, y,
		wmPtr->configWidth, wmPtr->configHeight);
	wmPtr->flags &= ~WM_SYNC_PENDING;
    } else if ((width != wmPtr->configWidth)
	    || (height != wmPtr->configHeight)) {
	wmPtr->configWidth = width;
	wmPtr->configHeight = height;
	if (wmTracing) {
	    TkMacOSXDbgMsg("Resizing to %d x %d\n", width, height);
	}
	SetWindowSizeLimits(winPtr);
	wmPtr->flags |= WM_SYNC_PENDING;
	XResizeWindow(winPtr->display, winPtr->window, wmPtr->configWidth,
		wmPtr->configHeight);
	wmPtr->flags &= ~WM_SYNC_PENDING;
    } else {
	SetWindowSizeLimits(winPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateSizeHints --
 *
 *	This procedure is called to update the window manager's size hints
 *	information from the information in a WmInfo structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Properties get changed for winPtr.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateSizeHints(
    TkWindow *winPtr)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    wmPtr->flags &= ~WM_UPDATE_SIZE_HINTS;
}

/*
 *----------------------------------------------------------------------
 *
 * ParseGeometry --
 *
 *	This procedure parses a geometry string and updates information used
 *	to control the geometry of a top-level window.
 *
 * Results:
 *	A standard Tcl return value, plus an error message in the interp's
 *	result if an error occurs.
 *
 * Side effects:
 *	The size and/or location of winPtr may change.
 *
 *----------------------------------------------------------------------
 */

static int
ParseGeometry(
    Tcl_Interp *interp,		/* Used for error reporting. */
    char *string,		/* String containing new geometry. Has the
				 * standard form "=wxh+x+y". */
    TkWindow *winPtr)		/* Pointer to top-level window whose geometry
				 * is to be changed. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int x, y, width, height, flags;
    char *end;
    char *p = string;

    /*
     * The leading "=" is optional.
     */

    if (*p == '=') {
	p++;
    }

    /*
     * Parse the width and height, if they are present. Don't actually update
     * any of the fields of wmPtr until we've successfully parsed the entire
     * geometry string.
     */

    width = wmPtr->width;
    height = wmPtr->height;
    x = -1;
    y = -1;
    flags = wmPtr->flags;
    if (isdigit(UCHAR(*p))) {
	width = strtoul(p, &end, 10);
	p = end;
	if (*p != 'x') {
	    goto error;
	}
	p++;
	if (!isdigit(UCHAR(*p))) {
	    goto error;
	}
	height = strtoul(p, &end, 10);
	p = end;
    }

    /*
     * Parse the X and Y coordinates, if they are present.
     */

    if (*p != '\0') {
	flags &= ~(WM_NEGATIVE_X | WM_NEGATIVE_Y);
	if (*p == '-') {
	    flags |= WM_NEGATIVE_X;
	} else if (*p != '+') {
	    goto error;
	}
	p++;
	if (!isdigit(UCHAR(*p)) && (*p != '-')) {
	    goto error;
	}
	x = strtol(p, &end, 10);
	p = end;
	if (*p == '-') {
	    flags |= WM_NEGATIVE_Y;
	} else if (*p != '+') {
	    goto error;
	}
	p++;
	if (!isdigit(UCHAR(*p)) && (*p != '-')) {
	    goto error;
	}
	y = strtol(p, &end, 10);
	if (*end != '\0') {
	    goto error;
	}

	/*
	 * Assume that the geometry information came from the user, unless an
	 * explicit source has been specified. Otherwise most window managers
	 * assume that the size hints were program-specified and they ignore
	 * them.
	 */

	if (!(wmPtr->sizeHintsFlags & (USPosition|PPosition))) {
	    wmPtr->sizeHintsFlags |= USPosition;
	    flags |= WM_UPDATE_SIZE_HINTS;
	}
    }

    /*
     * Everything was parsed OK. Update the fields of *wmPtr and arrange for
     * the appropriate information to be percolated out to the window manager
     * at the next idle moment.
     *
     * Computing the new position for the upper-left pixel of the window's
     * decorative frame is tricky because we need to include the border
     * widths supplied by a reparented parent in the calculation, but we can't
     * use the parent's current overall size since that may change as a result
     * of this code.
     */

    wmPtr->width = width;
    wmPtr->height = height;
    if (flags & WM_NEGATIVE_X) {
	int borderwidth = wmPtr->parentWidth - winPtr->changes.width;
	int newWidth = width == -1 ? winPtr->changes.width : width;

	x = (x == -1) ?
		wmPtr->x + winPtr->changes.width - newWidth :
		wmPtr->vRootWidth - x - newWidth - borderwidth;
    }
    if (x == -1) {
	x = wmPtr->x;
    }
    if (flags & WM_NEGATIVE_Y) {
	int borderheight = wmPtr->parentHeight - winPtr->changes.height;
	int newHeight = height == -1 ? winPtr->changes.height : height;

	y = (y == -1) ?
		wmPtr->y + winPtr->changes.height - newHeight :
		wmPtr->vRootHeight - y - newHeight - borderheight;
    }
    if (y == -1) {
	y = wmPtr->y;
    }
    if (wmPtr->flags & WM_FULLSCREEN) {
	wmPtr->configX = x;
	wmPtr->configY = y;
    } else {
	wmPtr->x = x;
	wmPtr->y = y;
    }
    flags |= WM_MOVE_PENDING;
    wmPtr->flags = flags;
    if (!(wmPtr->flags & (WM_UPDATE_PENDING|WM_NEVER_MAPPED))) {
	Tcl_DoWhenIdle(UpdateGeometryInfo, winPtr);
	wmPtr->flags |= WM_UPDATE_PENDING;
    }
    return TCL_OK;

  error:
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "bad geometry specifier \"%s\"", string));
    Tcl_SetErrorCode(interp, "TK", "VALUE", "GEOMETRY", (char *)NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetRootCoords --
 *
 *	Given a token for a window, this procedure traces through the window's
 *	lineage to find the (virtual) root-window coordinates corresponding to
 *	point (0,0) in the window.
 *
 * Results:
 *	The locations pointed to by xPtr and yPtr are filled in with the root
 *	coordinates of the (0,0) point in tkwin. If a virtual root window is
 *	in effect for the window, then the coordinates in the virtual root are
 *	returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Tk_GetRootCoords(
    Tk_Window tkwin,		/* Token for window. */
    int *xPtr,			/* Where to store x-displacement of (0,0). */
    int *yPtr)			/* Where to store y-displacement of (0,0). */
{
    int x, y;
    TkWindow *winPtr = (TkWindow *)tkwin;

    /*
     * Search back through this window's parents all the way to a top-level
     * window, combining the offsets of each window within its parent.
     */

    x = y = 0;
    while (1) {
	x += winPtr->changes.x + winPtr->changes.border_width;
	y += winPtr->changes.y + winPtr->changes.border_width;
	if (winPtr->flags & TK_TOP_LEVEL) {
	    TkWindow *otherPtr;

	    if (!(Tk_IsEmbedded(winPtr))) {
		x += winPtr->wmInfoPtr->xInParent;
		y += winPtr->wmInfoPtr->yInParent;
		break;
	    }

	    otherPtr = (TkWindow *)Tk_GetOtherWindow((Tk_Window)winPtr);
	    if (otherPtr == NULL) {
		break;
	    }

	    /*
	     * The container window is in the same application. Query its
	     * coordinates.
	     */

	    winPtr = otherPtr;
	    continue;
	}
	winPtr = winPtr->parentPtr;
    }
    *xPtr = x;
    *yPtr = y;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_CoordsToWindow --
 *
 *	This is a Macintosh specific implementation of this function. Given
 *	the root coordinates of a point, this procedure returns the token for
 *	the top-most window covering that point, if there exists such a window
 *	in this application.
 *
 * Results:
 *	The return result is either a token for the window corresponding to
 *	rootX and rootY, or else NULL to indicate that there is no such window.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tk_Window
Tk_CoordsToWindow(
    int rootX, int rootY,	/* Coordinates of point in root window. If a
				 * virtual-root window manager is in use,
				 * these coordinates refer to the virtual
				 * root, not the real root. */
    Tk_Window tkwin)		/* Token for any window in application; used
				 * to identify the display. */
{
    TkWindow *winPtr, *childPtr;
    TkWindow *nextPtr;		/* Coordinates of highest child found so far
				 * that contains point. */
    int x, y;			/* Coordinates in winPtr. */
    int tmpx, tmpy, bd;

    /*
     * Step 1: find the top-level window that contains the desired point.
     */

    winPtr = FrontWindowAtPoint(rootX, rootY);
    if (!winPtr) {
	return NULL;
    }

    /*
     * Step 2: work down through the hierarchy underneath this window. At each
     * level, scan through all the children to find the highest one in the
     * stacking order that contains the point. Then repeat the whole process
     * on that child.
     */

    x = rootX - winPtr->wmInfoPtr->xInParent;
    y = rootY - winPtr->wmInfoPtr->yInParent;
    while (1) {
	x -= winPtr->changes.x;
	y -= winPtr->changes.y;
	nextPtr = NULL;

	/*
	 * Container windows cannot have children. So if it is a container,
	 * look there, otherwise inspect the children.
	 */

	if (Tk_IsContainer(winPtr)) {
	    childPtr = (TkWindow *)Tk_GetOtherWindow((Tk_Window)winPtr);
	    if (childPtr != NULL) {
		if (Tk_IsMapped(childPtr)) {
		    tmpx = x - childPtr->changes.x;
		    tmpy = y - childPtr->changes.y;
		    bd = childPtr->changes.border_width;

		    if ((tmpx >= -bd) && (tmpy >= -bd)
			    && (tmpx < (childPtr->changes.width + bd))
			    && (tmpy < (childPtr->changes.height + bd))) {
			nextPtr = childPtr;
		    }
		}
	    }

	    /*
	     * TODO: Here we should handle out of process embedding.
	     */
	} else {
	    for (childPtr = winPtr->childList; childPtr != NULL;
		    childPtr = childPtr->nextPtr) {
		if (!Tk_IsMapped(childPtr) ||
			(childPtr->flags & TK_TOP_LEVEL)) {
		    continue;
		}
		tmpx = x - childPtr->changes.x;
		tmpy = y - childPtr->changes.y;
		bd = childPtr->changes.border_width;
		if ((tmpx >= -bd) && (tmpy >= -bd)
			&& (tmpx < (childPtr->changes.width + bd))
			&& (tmpy < (childPtr->changes.height + bd))) {
		    nextPtr = childPtr;
		}
	    }
	}
	if (nextPtr == NULL) {
	    break;
	}
	winPtr = nextPtr;
    }
    if (winPtr->mainPtr != ((TkWindow *)tkwin)->mainPtr) {
	return NULL;
    }
    return (Tk_Window)winPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_TopCoordsToWindow --
 *
 *	Given a Tk Window, and coordinates of a point relative to that window
 *	this procedure returns the top-most child of the window (excluding
 *	toplevels) covering that point, if there exists such a window in this
 *	application. It also sets newX, and newY to the coords of the point
 *	relative to the window returned.
 *
 * Results:
 *	The return result is either a token for the window corresponding to
 *	rootX and rootY, or else NULL to indicate that there is no such
 *	window. newX and newY are also set to the coords of the point relative
 *	to the returned window.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tk_Window
Tk_TopCoordsToWindow(
    Tk_Window tkwin,		/* Token for a Tk Window which defines the
				 * coordinates for rootX & rootY */
    int rootX, int rootY,	/* Coordinates of a point in tkWin. */
    int *newX, int *newY)	/* Coordinates of point in the upperMost child
				 * of tkWin containing (rootX,rootY) */
{
    TkWindow *winPtr, *childPtr;
    TkWindow *nextPtr;		/* Coordinates of highest child found so far
				 * that contains point. */
    int x, y;			/* Coordinates in winPtr. */

    winPtr = (TkWindow *)tkwin;
    x = rootX;
    y = rootY;
    while (1) {
	nextPtr = NULL;

	/*
	 * Container windows cannot have children. So if it is a container,
	 * look there, otherwise inspect the children.
	 */

	if (Tk_IsContainer(winPtr)) {
	    childPtr = (TkWindow *)Tk_GetOtherWindow((Tk_Window)winPtr);
	    if (childPtr != NULL) {
		if (Tk_IsMapped(childPtr) &&
			x > childPtr->changes.x &&
			x < childPtr->changes.x + childPtr->changes.width &&
			y > childPtr->changes.y &&
			y < childPtr->changes.y + childPtr->changes.height) {
		    nextPtr = childPtr;
		}
	    }

	    /*
	     * TODO: Here we should handle out of process embedding.
	     */
	} else {
	    for (childPtr = winPtr->childList; childPtr != NULL;
		    childPtr = childPtr->nextPtr) {
		if (!Tk_IsMapped(childPtr) ||
			(childPtr->flags & TK_TOP_LEVEL)) {
		    continue;
		}
		if (x < childPtr->changes.x || y < childPtr->changes.y) {
		    continue;
		}
		if (x > childPtr->changes.x + childPtr->changes.width ||
			y > childPtr->changes.y + childPtr->changes.height) {
		    continue;
		}
		nextPtr = childPtr;
	    }
	}
	if (nextPtr == NULL) {
	    break;
	}
	winPtr = nextPtr;
	x -= winPtr->changes.x;
	y -= winPtr->changes.y;
    }
    *newX = x;
    *newY = y;
    return (Tk_Window)winPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateVRootGeometry --
 *
 *	This procedure is called to update all the virtual root geometry
 *	information in wmPtr.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The vRootX, vRootY, vRootWidth, and vRootHeight fields in wmPtr are
 *	filled with the most up-to-date information.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateVRootGeometry(
    WmInfo *wmPtr)		/* Window manager information to be updated.
				 * The wmPtr->vRoot field must be valid. */
{
    TkWindow *winPtr = wmPtr->winPtr;
    unsigned int bd, dummy;
    Window dummy2;
    Status status;
    Tk_ErrorHandler handler;

    /*
     * If this isn't a virtual-root window manager, just return information
     * about the screen.
     */

    wmPtr->flags &= ~WM_VROOT_OFFSET_STALE;
    if (wmPtr->vRoot == None) {
    noVRoot:
	wmPtr->vRootX = wmPtr->vRootY = 0;
	wmPtr->vRootWidth = DisplayWidth(winPtr->display, winPtr->screenNum);
	wmPtr->vRootHeight = DisplayHeight(winPtr->display, winPtr->screenNum);
	return;
    }

    /*
     * Refresh the virtual root information if it's out of date.
     */

    handler = Tk_CreateErrorHandler(winPtr->display, -1, -1, -1, NULL, NULL);
    status = XGetGeometry(winPtr->display, wmPtr->vRoot,
	    &dummy2, &wmPtr->vRootX, &wmPtr->vRootY,
	    &wmPtr->vRootWidth, &wmPtr->vRootHeight, &bd, &dummy);
    if (wmTracing) {
	TkMacOSXDbgMsg("x = %d, y = %d, width = %d, height = %d, status = %d",
		wmPtr->vRootX, wmPtr->vRootY, wmPtr->vRootWidth,
		wmPtr->vRootHeight, status);
    }
    Tk_DeleteErrorHandler(handler);
    if (status == 0) {
	/*
	 * The virtual root is gone! Pretend that it never existed.
	 */

	wmPtr->vRoot = None;
	goto noVRoot;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetVRootGeometry --
 *
 *	This procedure returns information about the virtual root window
 *	corresponding to a particular Tk window.
 *
 * Results:
 *	The values at xPtr, yPtr, widthPtr, and heightPtr are set with the
 *	offset and dimensions of the root window corresponding to tkwin. If
 *	tkwin is being managed by a virtual root window manager these values
 *	correspond to the virtual root window being used for tkwin; otherwise
 *	the offsets will be 0 and the dimensions will be those of the screen.
 *
 * Side effects:
 *	Vroot window information is refreshed if it is out of date.
 *
 *----------------------------------------------------------------------
 */

void
Tk_GetVRootGeometry(
    Tk_Window tkwin,		/* Window whose virtual root is to be
				 * queried. */
    int *xPtr, int *yPtr,	/* Store x and y offsets of virtual root
				 * here. */
    int *widthPtr,		/* Store dimensions of virtual root here. */
    int *heightPtr)
{
    WmInfo *wmPtr;
    TkWindow *winPtr = (TkWindow *)tkwin;

    /*
     * Find the top-level window for tkwin, and locate the window manager
     * information for that window.
     */

    while (!(winPtr->flags & TK_TOP_LEVEL)) {
	winPtr = winPtr->parentPtr;
    }
    wmPtr = winPtr->wmInfoPtr;

    /*
     * Make sure that the geometry information is up-to-date, then copy it out
     * to the caller.
     */

    if (wmPtr->flags & WM_VROOT_OFFSET_STALE) {
	UpdateVRootGeometry(wmPtr);
    }
    *xPtr = wmPtr->vRootX;
    *yPtr = wmPtr->vRootY;
    *widthPtr = wmPtr->vRootWidth;
    *heightPtr = wmPtr->vRootHeight;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MoveToplevelWindow --
 *
 *	This procedure is called instead of Tk_MoveWindow to adjust the x-y
 *	location of a top-level window. It delays the actual move to a later
 *	time and keeps window-manager information up-to-date with the move.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window is eventually moved so that its upper-left corner
 *	(actually, the upper-left corner of the window's decorative frame, if
 *	there is one) is at (x,y).
 *
 *----------------------------------------------------------------------
 */

void
Tk_MoveToplevelWindow(
    Tk_Window tkwin,		/* Window to move. */
    int x, int y)		/* New location for window (within parent). */
{
    TkWindow *winPtr = (TkWindow *)tkwin;
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    if (!(winPtr->flags & TK_TOP_LEVEL)) {
	Tcl_Panic("Tk_MoveToplevelWindow called with non-toplevel window");
    }
    wmPtr->x = x;
    wmPtr->y = y;
    wmPtr->flags |= WM_MOVE_PENDING;
    if (!(wmPtr->sizeHintsFlags & (USPosition|PPosition))) {
	wmPtr->sizeHintsFlags |= USPosition;
	wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    }

    /*
     * If the window has already been mapped, must bring its geometry
     * up-to-date immediately, otherwise an event might arrive from the server
     * that would overwrite wmPtr->x and wmPtr->y and lose the new position.
     */

    if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
	if (wmPtr->flags & WM_UPDATE_PENDING) {
	    Tcl_CancelIdleCall(UpdateGeometryInfo, winPtr);
	}
	UpdateGeometryInfo(winPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmRestackToplevel --
 *
 *	This procedure restacks a top-level window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	WinPtr gets restacked as specified by aboveBelow and otherPtr. This
 *	procedure doesn't return until the restack has taken effect and the
 *	ConfigureNotify event for it has been received.
 *
 *----------------------------------------------------------------------
 */
#define PRINT_STACK					\
    for (NSWindow *w in [NSApp orderedWindows]) {		\
    TkWindow *winPtr2 = TkMacOSXGetTkWindow(w);			\
	if (winPtr2) {						\
	    fprintf(stderr, "%s ", Tk_PathName(winPtr2));	\
	}							\
    }								\
    fprintf(stderr, "\n");					\
    fflush(stderr)

void
TkWmRestackToplevel(
    TkWindow *winPtr,		/* Window to restack. */
    int aboveBelow,		/* Gives relative position for restacking;
				 * must be Above or Below. */
    TkWindow *otherPtr)		/* Window relative to which to restack; if
				 * NULL, then winPtr gets restacked above or
				 * below *all* siblings. */
{
    NSWindow *macWindow;
    NSWindow *otherMacWindow;
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int macAboveBelow = (aboveBelow == Above ? NSWindowAbove : NSWindowBelow);
    int otherNumber = 0; /* 0 will be used when otherPtr is NULL. */

    /*
     * If the Tk windows has no drawable, or is withdrawn do nothing.
     */

    if (winPtr->window == None ||
	    wmPtr == NULL      ||
	    wmPtr->hints.initial_state == WithdrawnState) {
	return;
    }
    macWindow = TkMacOSXGetNSWindowForDrawable(winPtr->window);
    if (macWindow == nil) {
	return;
    }
    if (otherPtr) {
	/*
	 * When otherPtr is non-NULL, if the other window has no drawable or is
	 * withdrawn, do nothing.
	 */

	WmInfo *otherWmPtr = otherPtr->wmInfoPtr;
	if (winPtr->window == None ||
		otherWmPtr == NULL ||
		otherWmPtr->hints.initial_state == WithdrawnState) {
	    return;
	}
	otherMacWindow = TkMacOSXGetNSWindowForDrawable(otherPtr->window);
	if (otherMacWindow == nil) {
	    return;
	}

	/*
	 * If the other window is OK, get its number.
	 */

	otherNumber = [otherMacWindow windowNumber];
    }

    /*
     * Just let the Mac window manager deal with all the subtleties of keeping
     * track of off-screen windows, etc.
     */
#if 0
    fprintf(stderr, "window order: "); PRINT_STACK;
#endif
    [macWindow orderWindow:macAboveBelow relativeTo:otherNumber];
#if 0
    fprintf(stderr, "new window order: "); PRINT_STACK;
#endif
#undef PRINT_STACK
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmAddToColormapWindows --
 *
 *	This procedure is called to add a given window to the
 *	WM_COLORMAP_WINDOWS property for its top-level, if it isn't already
 *	there. It is invoked by the Tk code that creates a new colormap, in
 *	order to make sure that colormap information is propagated to the
 *	window manager by default.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	WinPtr's window gets added to the WM_COLORMAP_WINDOWS property of its
 *	nearest top-level ancestor, unless the colormaps have been set
 *	explicitly with the "wm colormapwindows" command.
 *
 *----------------------------------------------------------------------
 */

void
TkWmAddToColormapWindows(
    TkWindow *winPtr)		/* Window with a non-default colormap. Should
				 * not be a top-level window. */
{
    TkWindow *topPtr;
    TkWindow **oldPtr, **newPtr;
    int count, i;

    if (winPtr->window == None) {
	return;
    }

    for (topPtr = winPtr->parentPtr; ; topPtr = topPtr->parentPtr) {
	if (topPtr == NULL) {
	    /*
	     * Window is being deleted. Skip the whole operation.
	     */

	    return;
	}
	if (topPtr->flags & TK_TOP_LEVEL) {
	    break;
	}
    }
    if (topPtr->wmInfoPtr->flags & WM_COLORMAPS_EXPLICIT) {
	return;
    }

    /*
     * Make sure that the window isn't already in the list.
     */

    count = topPtr->wmInfoPtr->cmapCount;
    oldPtr = topPtr->wmInfoPtr->cmapList;

    for (i = 0; i < count; i++) {
	if (oldPtr[i] == winPtr) {
	    return;
	}
    }

    /*
     * Make a new bigger array and use it to reset the property. Automatically
     * add the toplevel itself as the last element of the list.
     */

    newPtr = (TkWindow **)ckalloc((count+2) * sizeof(TkWindow *));
    if (count > 0) {
	memcpy(newPtr, oldPtr, count * sizeof(TkWindow *));
    }
    if (count == 0) {
	count++;
    }
    newPtr[count-1] = winPtr;
    newPtr[count] = topPtr;
    if (oldPtr != NULL) {
	ckfree(oldPtr);
    }

    topPtr->wmInfoPtr->cmapList = newPtr;
    topPtr->wmInfoPtr->cmapCount = count+1;

    /*
     * On the Macintosh all of this is just an excercise in compatibility as
     * we don't support colormaps. If we did they would be installed here.
     */
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmRemoveFromColormapWindows --
 *
 *	This procedure is called to remove a given window from the
 *	WM_COLORMAP_WINDOWS property for its top-level. It is invoked when
 *	windows are deleted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	WinPtr's window gets removed from the WM_COLORMAP_WINDOWS property of
 *	its nearest top-level ancestor, unless the top-level itself is being
 *	deleted too.
 *
 *----------------------------------------------------------------------
 */

void
TkWmRemoveFromColormapWindows(
    TkWindow *winPtr)		/* Window that may be present in
				 * WM_COLORMAP_WINDOWS property for its
				 * top-level. Should not be a top-level
				 * window. */
{
    TkWindow *topPtr, **oldPtr;
    int count, i, j;

    for (topPtr = winPtr->parentPtr; ; topPtr = topPtr->parentPtr) {
	if (topPtr == NULL) {
	    /*
	     * Ancestors have been deleted, so skip the whole operation. Seems
	     * like this can't ever happen?
	     */

	    return;
	}
	if (topPtr->flags & TK_TOP_LEVEL) {
	    break;
	}
    }
    if (topPtr->flags & TK_ALREADY_DEAD) {
	/*
	 * Top-level is being deleted, so there's no need to cleanup the
	 * WM_COLORMAP_WINDOWS property.
	 */

	return;
    }

    /*
     * Find the window and slide the following ones down to cover it up.
     */

    count = topPtr->wmInfoPtr->cmapCount;
    oldPtr = topPtr->wmInfoPtr->cmapList;
    for (i = 0; i < count; i++) {
	if (oldPtr[i] == winPtr) {
	    for (j = i ; j < count-1; j++) {
		oldPtr[j] = oldPtr[j+1];
	    }
	    topPtr->wmInfoPtr->cmapCount = count - 1;
	    break;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetPointerCoords --
 *
 *	Fetch the position of the mouse pointer.
 *
 * Results:
 *	*xPtr and *yPtr are filled in with the (virtual) root coordinates of
 *	the mouse pointer for tkwin's display. If the pointer isn't on tkwin's
 *	screen, then -1 values are returned for both coordinates. The argument
 *	tkwin must be a toplevel window.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkGetPointerCoords(
    TCL_UNUSED(Tk_Window),	/* Toplevel window that identifies screen on
				 * which lookup is to be done. */
    int *xPtr, int *yPtr)	/* Store pointer coordinates here. */
{
    XQueryPointer(NULL, None, NULL, NULL, xPtr, yPtr, NULL, NULL, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * InitialWindowBounds --
 *
 *	This function calculates the initial bounds for a new Mac toplevel
 *	window. Unless the geometry is specified by the user this code will
 *	auto place the windows in a cascade diagonially across the main monitor
 *	of the Mac.
 *
 * Results:
 *	Window bounds.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static NSRect
InitialWindowBounds(
    TkWindow *winPtr,		/* Window to get initial bounds for. */
    NSWindow *macWindow)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    if (!(wmPtr->sizeHintsFlags & (USPosition | PPosition))) {
	static NSPoint cascadePoint = { .x = 0, .y = 0 };
	NSRect frame;

	cascadePoint = [macWindow cascadeTopLeftFromPoint:cascadePoint];
	frame = [macWindow frame];
	wmPtr->x = frame.origin.x;
	wmPtr->y = TkMacOSXZeroScreenHeight() - (frame.origin.y +
		frame.size.height);
    }
    return NSMakeRect(wmPtr->x, wmPtr->y, winPtr->changes.width,
	    winPtr->changes.height);
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXResizable --
 *
 *	This function determines if the passed in window is part of a toplevel
 *	window that is resizable. If the window is resizable in the x, y or
 *	both directions, true is returned.
 *
 * Results:
 *	True if resizable, false otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkMacOSXResizable(
    TkWindow *winPtr)		/* Tk window or NULL. */
{
    WmInfo *wmPtr;

    if (winPtr == NULL) {
	return false;
    }
    while (winPtr->wmInfoPtr == NULL) {
	winPtr = winPtr->parentPtr;
    }

    wmPtr = winPtr->wmInfoPtr;
    if ((wmPtr->flags & WM_WIDTH_NOT_RESIZABLE) &&
	    (wmPtr->flags & WM_HEIGHT_NOT_RESIZABLE)) {
	return false;
    } else {
	return true;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXGrowToplevel --
 *
 *	The function is invoked when the user clicks in the grow region of a
 *	Tk window. The function will handle the dragging procedure and not
 *	return until completed. Finally, the function may place information
 *	Tk's event queue is the window was resized.
 *
 * Results:
 *	True if events were placed on event queue, false otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkMacOSXGrowToplevel(
    TCL_UNUSED(void *),
    TCL_UNUSED(XPoint))
{
    return false;
}

/*
 *----------------------------------------------------------------------
 *
 * TkSetWMName --
 *
 *	Set the title for a toplevel window. If the window is embedded, do not
 *	change the window title.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The title of the window is changed.
 *
 *----------------------------------------------------------------------
 */

void
TkSetWMName(
    TkWindow *winPtr,
    Tk_Uid titleUid)
{
    if (Tk_IsEmbedded(winPtr)) {
	return;
    }

    NSString *title = [[TKNSString alloc] initWithTclUtfBytes:titleUid length:TCL_INDEX_NONE];
    [TkMacOSXGetNSWindowForDrawable(winPtr->window) setTitle:title];
    [title release];
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXGetContainer --
 *
 *	If the passed window has the TRANSIENT_FOR property set this will
 *	return the container window. Otherwise it will return None.
 *
 * Results:
 *	The container window or None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tk_Window
TkMacOSXGetContainer(
    TkWindow *winPtr)
{
    if (Tk_PathName(winPtr)) {
	return (Tk_Window)winPtr->wmInfoPtr->container;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXGetXWindow --
 *
 *	Stub function that returns the X window Id associated with the
 *      given NSWindow*.
 *
 * Results:
 *	The window id is returned. None is returned if not a Tk window.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Window
TkMacOSXGetXWindow(
    void *macWinPtr)
{
    Window window = None;
    TKWindow *w = (TKWindow *)macWinPtr;
    if ([w respondsToSelector: @selector (tkWindow)]) {
	window = [w tkWindow];
    }
    return window ? window : None;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MacOSXGetTkWindow --
 *
 *	Returns the Tk_Window associated with the given NSWindow*.  This
 *      function is a stub, so the NSWindow* parameter must be declared as
 *      void*.
 *
 * Results:
 *	A Tk_Window, or None if the NSWindow is not associated with
 *      any Tk window.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tk_Window
Tk_MacOSXGetTkWindow(
    void *w)
{
    Window window = None;
    if ([(NSWindow *)w respondsToSelector: @selector (tkWindow)]) {
	window = [(TKWindow *)w tkWindow];
	TkDisplay *dispPtr = TkGetDisplayList();
	if (window && dispPtr && dispPtr->display) {
	    return Tk_IdToWindow(dispPtr->display, window);
	}
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXIsWindowZoomed --
 *
 *	Ask Cocoa if the given window is in the zoomed out state. Because
 *	dragging & growing a window can change the Cocoa zoom state, we
 *	cannot rely on wmInfoPtr->hints.initial_state for this information.
 *
 * Results:
 *	True if window is zoomed out, false otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkMacOSXIsWindowZoomed(
    TkWindow *winPtr)
{
    NSWindow *macWindow = nil;
    if (winPtr && winPtr->window) {
	macWindow = TkMacOSXGetNSWindowForDrawable(winPtr->window);
    }
    return [macWindow isZoomed];
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXZoomToplevel --
 *
 *	The function is invoked when the user clicks in the zoom region of a
 *	Tk window or when the window state is set/unset to "zoomed" manually.
 *	If the window is to be zoomed (in or out), the window size is changed
 *	and events are generated to let Tk know what happened.
 *
 * Results:
 *	True if events were placed on event queue, false otherwise.
 *
 * Side effects:
 *	The window may be resized & events placed on Tk's queue.
 *
 *----------------------------------------------------------------------
 */

int
TkMacOSXZoomToplevel(
    void *whichWindow,		/* The Macintosh window to zoom. */
    short zoomPart)		/* Either inZoomIn or inZoomOut */
{
    NSWindow *window = (NSWindow *)whichWindow;
    TkWindow *winPtr = (TkWindow *)TkMacOSXGetTkWindow(window);
    WmInfo *wmPtr;

    if (!winPtr || !winPtr->wmInfoPtr) {
	return false;
    }
    wmPtr = winPtr->wmInfoPtr;
    if ((wmPtr->flags & WM_WIDTH_NOT_RESIZABLE) &&
	    (wmPtr->flags & WM_HEIGHT_NOT_RESIZABLE)) {
	return false;
    }

    /*
     * Do nothing if already in desired zoom state.
     */

    if (([window isZoomed] == (zoomPart == inZoomOut))) {
	return false;
    }
    [window zoom:NSApp];

    wmPtr->hints.initial_state =
	(zoomPart == inZoomIn ? NormalState : ZoomState);
    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TkUnsupported1ObjCmd --
 *
 *	This procedure is invoked to process the
 *	"::tk::unsupported::MacWindowStyle" Tcl command. This command allows
 *	you to set the style of decoration for a Macintosh window.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Changes the style of a new Mac window.
 *
 *----------------------------------------------------------------------
 */

int
TkUnsupported1ObjCmd(
    void *clientData,	/* Main window associated with interpreter. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    static const char *const subcmds[] = {
	"appearance", "isdark", "style", NULL
    };
    enum SubCmds {
	TKMWS_APPEARANCE, TKMWS_ISDARK, TKMWS_STYLE
    };
    Tk_Window tkwin = (Tk_Window)clientData;
    TkWindow *winPtr;
    int index;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "option window ?arg ...?");
	return TCL_ERROR;
    }

    winPtr = (TkWindow *)
	    Tk_NameToWindow(interp, Tcl_GetString(objv[2]), tkwin);
    if (winPtr == NULL) {
	return TCL_ERROR;
    }
    if (!(winPtr->flags & TK_TOP_LEVEL)) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"window \"%s\" isn't a top-level window", winPtr->pathName));
	Tcl_SetErrorCode(interp, "TK", "WINDOWSTYLE", "TOPLEVEL", (char *)NULL);
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[1], subcmds,
	    sizeof(char *), "option", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }
    switch((enum SubCmds) index) {
    case TKMWS_STYLE:
	if ((objc < 3) || (objc > 5)) {
	    Tcl_WrongNumArgs(interp, 2, objv, "window ?class attributes?");
	    return TCL_ERROR;
	}
	return WmWinStyle(interp, winPtr, objc, objv);
    case TKMWS_APPEARANCE:
	if ((objc < 3) || (objc > 4)) {
	    Tcl_WrongNumArgs(interp, 2, objv, "window ?appearancename?");
	    return TCL_ERROR;
	}
	if (objc == 4 && [NSApp macOSVersion] < 101400) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "Window appearances cannot be changed before OSX 10.14.",
		    -1));
	    Tcl_SetErrorCode(interp, "TK", "WINDOWSTYLE", "APPEARANCE", (char *)NULL);
	    return TCL_ERROR;
	}
	return WmWinAppearance(interp, winPtr, objc, objv);
    case TKMWS_ISDARK:
	if ((objc != 3)) {
	    Tcl_WrongNumArgs(interp, 2, objv, "window");
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(
		TkMacOSXInDarkMode((Tk_Window)winPtr)));
	return TCL_OK;
    default:
	return TCL_ERROR;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * WmWinStyle --
 *
 *	This procedure is invoked to process the
 *	"::tk::unsupported::MacWindowStyle style" subcommand. This command
 *	allows you to set the style of decoration for a Macintosh window.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Changes the style of a new Mac window.
 *
 *----------------------------------------------------------------------
 */

static int
WmWinStyle(
    Tcl_Interp *interp,		/* Current interpreter. */
    TkWindow *winPtr,		/* Window to be manipulated. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj * const objv[])	/* Argument objects. */
{
    struct StrIntMap {
	const char *strValue;
	UInt64 intValue;
    };
    static const struct StrIntMap classMap[] = {
	{ "alert",		kAlertWindowClass			     },
	{ "moveableAlert",	kMovableAlertWindowClass		     },
	{ "modal",		kModalWindowClass			     },
	{ "moveableModal",	kMovableModalWindowClass		     },
	{ "floating",		kFloatingWindowClass			     },
	{ "document",		kDocumentWindowClass			     },
	{ "utility",		kUtilityWindowClass			     },
	{ "help",		kHelpWindowClass			     },
	{ "sheet",		kSheetWindowClass			     },
	{ "toolbar",		kToolbarWindowClass			     },
	{ "plain",		kPlainWindowClass			     },
	{ "overlay",		kOverlayWindowClass			     },
	{ "sheetAlert",		kSheetAlertWindowClass			     },
	{ "altPlain",		kAltPlainWindowClass			     },
	{ "simple",		kSimpleWindowClass			     },
	{ "drawer",		kDrawerWindowClass			     },
	{ NULL, 0 }
    };
    static const struct StrIntMap compositeAttrMap[] = {
	{ "none",		kWindowNoAttributes			     },
	{ "standardDocument",	kWindowStandardDocumentAttributes	     },
	{ "standardFloating",	kWindowStandardFloatingAttributes	     },
	{ "fullZoom",		kWindowFullZoomAttribute		     },
	{ NULL, 0 }
    };

    /*
     * Map window attributes. Color and opacity are mapped to NULL; these are
     * parsed from the objv in TkUnsupported1ObjCmd.
     */

    static const struct StrIntMap attrMap[] = {
	{ "closeBox",		kWindowCloseBoxAttribute		     },
	{ "horizontalZoom",	kWindowHorizontalZoomAttribute		     },
	{ "verticalZoom",	kWindowVerticalZoomAttribute		     },
	{ "collapseBox",	kWindowCollapseBoxAttribute		     },
	{ "resizable",		kWindowResizableAttribute		     },
	{ "sideTitlebar",	kWindowSideTitlebarAttribute		     },
	{ "toolbarButton",	kWindowToolbarButtonAttribute		     },
	{ "unifiedTitleAndToolbar", kWindowUnifiedTitleAndToolbarAttribute   },
	{ "metal",		kWindowMetalAttribute			     },
	{ "noTitleBar",		kWindowNoTitleBarAttribute		     },
	{ "texturedSquareCorners", kWindowTexturedSquareCornersAttribute     },
	{ "metalNoContentSeparator", kWindowMetalNoContentSeparatorAttribute },
	{ "doesNotCycle",	kWindowDoesNotCycleAttribute		     },
	{ "noUpdates",		kWindowNoUpdatesAttribute		     },
	{ "noActivates",	kWindowNoActivatesAttribute		     },
	{ "opaqueForEvents",	kWindowOpaqueForEventsAttribute		     },
	{ "noShadow",		kWindowNoShadowAttribute		     },
	{ "hideOnSuspend",	kWindowHideOnSuspendAttribute		     },
	{ "hideOnFullScreen",	kWindowHideOnFullScreenAttribute	     },
	{ "inWindowMenu",	kWindowInWindowMenuAttribute		     },
	{ "liveResize",		kWindowLiveResizeAttribute		     },
	{ "ignoreClicks",	kWindowIgnoreClicksAttribute		     },
	{ "noConstrain",	kWindowNoConstrainAttribute		     },
	{ "doesNotHide",	tkWindowDoesNotHideAttribute		     },
	{ "canJoinAllSpaces",	tkCanJoinAllSpacesAttribute		     },
	{ "moveToActiveSpace",	tkMoveToActiveSpaceAttribute		     },
	{ "nonActivating",	tkNonactivatingPanelAttribute		     },
	{ "hud",		tkHUDWindowAttribute			     },
	{ NULL, 0 }
    };

    int index;
    Tcl_Size i;
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    if (objc == 3) {
	Tcl_Obj *attributeList, *newResult = NULL;
	UInt64 attributes;

	for (i = 0; classMap[i].strValue != NULL; i++) {
	    if (wmPtr->macClass == classMap[i].intValue) {
		newResult = Tcl_NewStringObj(classMap[i].strValue, TCL_INDEX_NONE);
		break;
	    }
	}
	if (newResult == NULL) {
	    Tcl_Panic("invalid class");
	}

	attributeList = Tcl_NewListObj(0, NULL);
	attributes = wmPtr->attributes;

	for (i = 0; compositeAttrMap[i].strValue != NULL; i++) {
	    UInt64 intValue = compositeAttrMap[i].intValue;

	    if (intValue && (attributes & intValue) == intValue) {
		Tcl_ListObjAppendElement(NULL, attributeList,
			Tcl_NewStringObj(compositeAttrMap[i].strValue,
			-1));
		attributes &= ~intValue;
		break;
	    }
	}
	for (i = 0; attrMap[i].strValue != NULL; i++) {
	    if (attributes & attrMap[i].intValue) {
		Tcl_ListObjAppendElement(NULL, attributeList,
			Tcl_NewStringObj(attrMap[i].strValue, TCL_INDEX_NONE));
	    }
	}
	Tcl_ListObjAppendElement(NULL, newResult, attributeList);
	Tcl_SetObjResult(interp, newResult);
    } else {
	Tcl_Size attrObjc;
	Tcl_Obj **attrObjv = NULL;
	WindowClass macClass;
	UInt64 oldAttributes = wmPtr->attributes;
	int oldFlags = wmPtr->flags;

	if (Tcl_GetIndexFromObjStruct(interp, objv[3], classMap,
		sizeof(struct StrIntMap), "class", 0, &index) != TCL_OK) {
	    goto badClassAttrs;
	}
	macClass = classMap[index].intValue;
	if (objc == 5) {
	    if (Tcl_ListObjGetElements(interp, objv[4], &attrObjc, &attrObjv)
		    != TCL_OK) {
		goto badClassAttrs;
	    }
	    wmPtr->attributes = kWindowNoAttributes;
	    for (i = 0; i < attrObjc; i++) {
		if (Tcl_GetIndexFromObjStruct(interp, attrObjv[i],
			compositeAttrMap, sizeof(struct StrIntMap),
			"attribute", 0, &index) == TCL_OK) {
		    wmPtr->attributes |= compositeAttrMap[index].intValue;
		} else if (Tcl_GetIndexFromObjStruct(interp, attrObjv[i],
			attrMap, sizeof(struct StrIntMap),
			"attribute", 0, &index) == TCL_OK) {
		    Tcl_ResetResult(interp);
		    wmPtr->attributes |= attrMap[index].intValue;
		} else {
		    goto badClassAttrs;
		}
	    }
	} else {
	    wmPtr->attributes = macClassAttrs[macClass].defaultAttrs;
	}
	wmPtr->attributes &= (tkAlwaysValidAttributes |
		macClassAttrs[macClass].validAttrs);
	wmPtr->flags |= macClassAttrs[macClass].flags;
	wmPtr->macClass = macClass;
	ApplyWindowAttributeFlagChanges(winPtr, NULL, oldAttributes, oldFlags,
		0, 1);
	return TCL_OK;

    badClassAttrs:
	wmPtr->attributes = oldAttributes;
	return TCL_ERROR;
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WmWinAppearance --
 *
 *	This procedure is invoked to process the
 *	"::tk::unsupported::MacWindowStyle appearance" subcommand. The command
 *	allows you to get or set the appearance for the NSWindow associated
 *	with a Tk Window.  The syntax is:
 *
 *	    tk::unsupported::MacWindowStyle appearance window ?newAppearance?
 *
 *      Allowed appearance names are "aqua", "darkaqua", and "auto".
 *
 * Results:
 *      Returns the appearance setting of the window prior to calling this
 *	function.
 *
 * Side effects:
 *      The underlying NSWindow's appearance property is set to the specified
 *      value if the optional newAppearance argument is supplied. Otherwise the
 *      window's appearance property is not changed.  If the appearance is set
 *      to aqua or darkaqua then the window will use the associated
 *      NSAppearance even if the user has selected a different appearance with
 *      the system preferences.  If it is set to auto then the appearance
 *      property is set to nil, meaning that the preferences will determine the
 *      appearance.
 *
 *----------------------------------------------------------------------
 */

static int
WmWinAppearance(
    Tcl_Interp *interp,		/* Current interpreter. */
    TkWindow *winPtr,		/* Window to be manipulated. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj * const objv[])	/* Argument objects. */
{
#if MAC_OS_X_VERSION_MAX_ALLOWED < 101000
    (void) interp;
    (void) winPtr;
    (void) objc;
    (void) objv;
    return TCL_OK;
#else
    Tcl_Obj *result = NULL;
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101300
    NSAppearanceName appearance;
#else
    NSString *appearance;
#endif // MAC_OS_X_VERSION_MAX_ALLOWED >= 101300

    const char *resultString = "unrecognized";
    NSWindow *win = TkMacOSXGetNSWindowForDrawable(winPtr->window);
    if (win) {
	appearance = win.appearance.name;
	if (appearance == nil) {
	    resultString = appearanceStrings[APPEARANCE_AUTO];
	} else if (appearance == NSAppearanceNameAqua) {
	    resultString = appearanceStrings[APPEARANCE_AQUA];
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
	} else if (@available(macOS 10.14, *)) {
	    if (appearance == NSAppearanceNameDarkAqua) {
		resultString = appearanceStrings[APPEARANCE_DARKAQUA];
	    }
#endif // MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
	}
	result = Tcl_NewStringObj(resultString, strlen(resultString));
    }
    if (result == NULL) {
	NSLog(@"Failed to read appearance name; try calling update idletasks before getting/setting the appearance of the window.");
	return TCL_OK;
    }
    if (objc == 4) {
	int index;
	if (Tcl_GetIndexFromObjStruct(interp, objv[3], appearanceStrings,
		sizeof(char *), "appearancename", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	switch ((enum appearances) index) {
	case APPEARANCE_AQUA:
	    win.appearance = [NSAppearance appearanceNamed:
		NSAppearanceNameAqua];
	    break;
	case APPEARANCE_DARKAQUA:
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
	    if (@available(macOS 10.14, *)) {
		win.appearance = [NSAppearance appearanceNamed:
		    NSAppearanceNameDarkAqua];
	    }
#endif // MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
	    break;
	default:
	    win.appearance = nil;
	}
    }
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMakeMenuWindow --
 *
 *	Configure the window to be either a undecorated pull-down (or pop-up)
 *	menu, or as a toplevel floating menu (palette).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the style bit used to create a new Mac toplevel.
 *
 *----------------------------------------------------------------------
 */

void
TkpMakeMenuWindow(
    Tk_Window tkwin,		/* New window. */
    int transient)		/* 1 means menu is only posted briefly as a
				 * popup or pulldown or cascade. 0 means menu
				 * is always visible, e.g. as a floating
				 * menu. */
{
    TkWindow *winPtr = (TkWindow *)tkwin;

    if (transient) {
	winPtr->wmInfoPtr->macClass = kSimpleWindowClass;
	winPtr->wmInfoPtr->attributes = kWindowNoActivatesAttribute;
    } else {
	winPtr->wmInfoPtr->macClass = kFloatingWindowClass;
	winPtr->wmInfoPtr->attributes = kWindowStandardFloatingAttributes;
	winPtr->wmInfoPtr->flags |= WM_WIDTH_NOT_RESIZABLE;
	winPtr->wmInfoPtr->flags |= WM_HEIGHT_NOT_RESIZABLE;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXMakeRealWindowExist --
 *
 *	This function finally creates the real Macintosh window that the Mac
 *	actually understands.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A new Macintosh toplevel is created.
 *
 *----------------------------------------------------------------------
 */

void
TkMacOSXMakeRealWindowExist(
    TkWindow *winPtr)		/* Tk window. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    MacDrawable *macWin;
    WindowClass macClass;
    Class winClass = nil;
    Bool overrideRedirect = Tk_Attributes((Tk_Window)winPtr)->override_redirect;
    Tcl_HashEntry *hPtr = NULL;
    NSUInteger styleMask;
    NSString *identifier;
    char *tabbingId = NULL;
    long tabbingMode = NSWindowTabbingModeAutomatic;
    static int initialized = 0;

    if (TkMacOSXHostToplevelExists(winPtr)) {
	return;
    }

    macWin = (MacDrawable *)winPtr->window;

    /*
     * If this is embedded, make sure its container's toplevel exists, then
     * return...
     */

    if (Tk_IsEmbedded(winPtr)) {
	TkWindow *contWinPtr = (TkWindow *)Tk_GetOtherWindow((Tk_Window)winPtr);

	if (contWinPtr != NULL) {
	    TkMacOSXMakeRealWindowExist(
		    contWinPtr->privatePtr->toplevel->winPtr);
	    macWin->flags |= TK_HOST_EXISTS;
	    return;
	}

	Tcl_Panic("TkMacOSXMakeRealWindowExist could not find container");
	return;

	/*
	 * TODO: Here we should handle out of process embedding.
	 */
    }

    if ([NSApp macOSVersion] >= 101300) {
	/*
	 * Prior to macOS 10.12 the styleMask was readonly.  From macOS 10.12
	 * onward, the styleMask can replace the Carbon window classes and
	 * attributes.
	 */
	int index;
	if (!initialized) {
	    Tcl_InitHashTable(&pathnameToSubclass, TCL_STRING_KEYS);
	    Tcl_InitHashTable(&pathnameToTabbingId, TCL_STRING_KEYS);
	    Tcl_InitHashTable(&pathnameToTabbingMode, TCL_STRING_KEYS);
	    initialized = 1;
	}
	hPtr = Tcl_FindHashEntry(&pathnameToSubclass, Tk_PathName(winPtr));
	index = hPtr ? PTR2INT(Tcl_GetHashValue(hPtr)) : subclassNSWindow;
	switch(index) {
	case subclassNSPanel:
	    winClass = [TKPanel class];
	    styleMask =  (NSWindowStyleMaskTitled        |
			  NSWindowStyleMaskClosable      |
			  NSWindowStyleMaskResizable     |
			  NSWindowStyleMaskUtilityWindow |
			  NSWindowStyleMaskNonactivatingPanel );
		break;
	default:
	    winClass = [TKWindow class];
	    styleMask =  (NSWindowStyleMaskTitled         |
			  NSWindowStyleMaskClosable       |
			  NSWindowStyleMaskMiniaturizable |
			  NSWindowStyleMaskResizable );
		break;
	}
	if (overrideRedirect) {
	    styleMask |= NSWindowStyleMaskDocModalWindow;
	}
	/* Help windows (used for tooltips) should have stylemask 0. */
	if (wmPtr->macClass == kHelpWindowClass) {
	    styleMask = 0;
	}
	if (hPtr) {
	    Tcl_DeleteHashEntry(hPtr);
	}
	hPtr = Tcl_FindHashEntry(&pathnameToTabbingId, Tk_PathName(winPtr));
	if (hPtr) {
	    tabbingId = (char *)Tcl_GetHashValue(hPtr);
	    Tcl_DeleteHashEntry(hPtr);
	}
	hPtr = Tcl_FindHashEntry(&pathnameToTabbingMode, Tk_PathName(winPtr));
	if (hPtr) {
	    tabbingMode = PTR2INT(Tcl_GetHashValue(hPtr));
	    Tcl_DeleteHashEntry(hPtr);
	}
    } else {

	/*
	 * If this is an override-redirect window, the NSWindow is created first as
	 * a document window then converted to a simple window.
	 */

	if (overrideRedirect) {
	    wmPtr->macClass = kDocumentWindowClass;
	}
	macClass = wmPtr->macClass;
	wmPtr->attributes &= (tkAlwaysValidAttributes |
			      macClassAttrs[macClass].validAttrs);
	wmPtr->flags |= macClassAttrs[macClass].flags |
	    ((wmPtr->attributes & kWindowResizableAttribute) ? 0 :
	     WM_WIDTH_NOT_RESIZABLE|WM_HEIGHT_NOT_RESIZABLE);
	UInt64 attributes = (wmPtr->attributes &
			     ~macClassAttrs[macClass].forceOffAttrs) |
	    macClassAttrs[macClass].forceOnAttrs;
	styleMask = macClassAttrs[macClass].styleMask |
	    ((attributes & kWindowNoTitleBarAttribute) ? 0 : NSTitledWindowMask) |
	    ((attributes & kWindowCloseBoxAttribute) ? NSClosableWindowMask : 0) |
	    ((attributes & kWindowCollapseBoxAttribute) ?
	     NSMiniaturizableWindowMask : 0) |
	    ((attributes & kWindowResizableAttribute) ? NSResizableWindowMask : 0) |
	    ((attributes & kWindowMetalAttribute) ?
	     NSTexturedBackgroundWindowMask : 0) |
	    ((attributes & kWindowUnifiedTitleAndToolbarAttribute) ?
	     NSUnifiedTitleAndToolbarWindowMask : 0) |
	    ((attributes & kWindowSideTitlebarAttribute) ? 1 << 9 : 0) |
	    (attributes >> WM_NSMASK_SHIFT);
	winClass = (macClass == kDrawerWindowClass ? [TKDrawerWindow class] :
		    (styleMask & (NSUtilityWindowMask|NSDocModalWindowMask|
				  NSNonactivatingPanelMask|NSHUDWindowMask)) ?
		    [TKPanel class] : [TKWindow class]);
    }
    NSRect structureRect = [winClass frameRectForContentRect:NSZeroRect
	    styleMask:styleMask];
    NSRect contentRect = NSMakeRect(5 - structureRect.origin.x,
	    TkMacOSXZeroScreenHeight() - (TkMacOSXZeroScreenTop() + 5 +
	    structureRect.origin.y + structureRect.size.height + 200), 200, 200);
    if (wmPtr->hints.initial_state == WithdrawnState) {
	//// ???????
    }
    TKWindow *window = [[winClass alloc] initWithContentRect:contentRect
	    styleMask:styleMask backing:NSBackingStoreBuffered defer:YES];
    if (!window) {
	Tcl_Panic("couldn't allocate new Mac window");
    }
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101300
    if (tabbingId) {
	identifier = [NSString stringWithUTF8String:tabbingId];
    } else {
	identifier = [NSString stringWithUTF8String:Tk_PathName(winPtr)];
    }
    [window setTabbingIdentifier: identifier];
    [window setTabbingMode: tabbingMode];
#endif
    if (tabbingId) {
	ckfree(tabbingId);
    }
    TKContentView *contentView = [[TKContentView alloc]
				     initWithFrame:NSZeroRect];
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
    NSUserDefaults *preferences = [NSUserDefaults standardUserDefaults];

    /*
     * AppKit calls the viewDidChangeEffectiveAppearance method when the
     * user changes the Accent Color but not when the user changes the
     * Highlight Color.  So we register to receive KVO notifications for
     * Highlight Color as well.
     */

    [preferences addObserver:contentView
		  forKeyPath:@"AppleHighlightColor"
		     options:NSKeyValueObservingOptionNew
		     context:NULL];
#endif
    [window setContentView:contentView];
    [contentView release];
    [window setDelegate:NSApp];
    [window setAcceptsMouseMovedEvents:NO];
    [window setReleasedWhenClosed:NO];
    if (styleMask & NSUtilityWindowMask) {
	[(TKPanel*)window setFloatingPanel:YES];
    }
    if ((styleMask & (NSTexturedBackgroundWindowMask|NSHUDWindowMask)) &&
	    !(styleMask & NSDocModalWindowMask)) {
	/*
	 * Workaround for [Bug 2824538]: Textured windows are draggable from
	 *                               opaque content.
	 */
	[window setMovableByWindowBackground:NO];
    }
    [window setDocumentEdited:NO];
    wmPtr->window = window;
    macWin->view = window.contentView;
    TkMacOSXApplyWindowAttributes(winPtr, window);
    NSRect geometry = InitialWindowBounds(winPtr, window);
    geometry.size.width += structureRect.size.width;
    geometry.size.height += structureRect.size.height;
    geometry.origin.y = TkMacOSXZeroScreenHeight() - (geometry.origin.y +
	    geometry.size.height);
    [window setFrame:geometry display:YES];
    [window setTkWindow: (Window) macWin];

    macWin->flags |= TK_HOST_EXISTS;
    if (overrideRedirect) {
	XSetWindowAttributes atts;

	atts.override_redirect = True;
	Tk_ChangeWindowAttributes((Tk_Window)winPtr, CWOverrideRedirect, &atts);
	if ([NSApp macOSVersion] >= 101300) {
	    window.styleMask |= NSWindowStyleMaskDocModalWindow;
	} else {
	    ApplyContainerOverrideChanges(winPtr, NULL);
	}
    }
    [window display];
}

/*
 *----------------------------------------------------------------------
 *
 * TkpRedrawWidget --
 *
 *      This is a stub called only from tkTextDisp.c.  It was introduced
 *      to deal with an issue in macOS 10.14 and is not needed
 *      even for that OS with updateLayer in use.  It would add the widget bounds
 *      to the dirtyRect, which is not currently used, and set the
 *      TkNeedsDisplay flag.  Now it is a no-op.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The widget's bounding rectangle is marked as dirty.
 *
 *----------------------------------------------------------------------
 */

void
TkpRedrawWidget(Tk_Window tkwin) {
    (void) tkwin;
#if 0
    TkWindow *winPtr = (TkWindow *)tkwin;
    NSWindow *w = nil;
    Rect tkBounds;
    NSRect bounds;

    if (winPtr && winPtr->window) {
	w = TkMacOSXGetNSWindowForDrawable(winPtr->window);
    }
    if (w) {
	TKContentView *view = [w contentView];
	TkMacOSXWinBounds(winPtr, &tkBounds);
	bounds = NSMakeRect(tkBounds.left,
			    [view bounds].size.height - tkBounds.bottom,
			    tkBounds.right - tkBounds.left,
			    tkBounds.bottom - tkBounds.top);
	[view setNeedsDisplay:YES];
    }
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXSetScrollbarGrow --
 *
 *	Sets a flag for a toplevel window indicating that the passed Tk
 *	scrollbar window will display the grow region for the toplevel window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A flag is set int windows toplevel parent.
 *
 *----------------------------------------------------------------------
 */

void
TkMacOSXSetScrollbarGrow(
    TkWindow *winPtr,		/* Tk scrollbar window. */
    int flag)			/* Boolean value true or false. */
{
    if (flag) {
	winPtr->privatePtr->toplevel->flags |= TK_SCROLLBAR_GROW;
	winPtr->privatePtr->toplevel->winPtr->wmInfoPtr->scrollWinPtr = winPtr;
    } else if (winPtr->privatePtr->toplevel->winPtr->wmInfoPtr->scrollWinPtr
	    == winPtr) {
	winPtr->privatePtr->toplevel->flags &= ~TK_SCROLLBAR_GROW;
	winPtr->privatePtr->toplevel->winPtr->wmInfoPtr->scrollWinPtr = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmFocusToplevel --
 *
 *	This is a utility procedure invoked by focus-management code. It
 *	exists because of the extra wrapper windows that exist under Unix; its
 *	job is to map from wrapper windows to the corresponding toplevel
 *	windows. On PCs and Macs there are no wrapper windows so no mapping is
 *	necessary; this procedure just determines whether a window is a
 *	toplevel or not.
 *
 * Results:
 *	If winPtr is a toplevel window, returns the pointer to the window;
 *	otherwise returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkWindow *
TkWmFocusToplevel(
    TkWindow *winPtr)		/* Window that received a focus-related
				 * event. */
{
    if (!(winPtr->flags & TK_TOP_LEVEL)) {
	return NULL;
    }
    return winPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetWrapperWindow --
 *
 *	This is a utility procedure invoked by focus-management code. It maps
 *	to the wrapper for a top-level, which is just the same as the
 *	top-level on Macs and PCs.
 *
 * Results:
 *	If winPtr is a toplevel window, returns the pointer to the window;
 *	otherwise returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkWindow *
TkpGetWrapperWindow(
    TkWindow *winPtr)		/* Window that received a focus-related
				 * event. */
{
    if (!(winPtr->flags & TK_TOP_LEVEL)) {
	return NULL;
    }
    return winPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpWmSetState --
 *
 *	Sets the window manager state for the wrapper window of a given
 *	toplevel window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May maximize, minimize, restore, or withdraw a window.
 *
 *----------------------------------------------------------------------
 */

int
TkpWmSetState(
    TkWindow *winPtr,		/* Toplevel window to operate on. */
    int state)			/* One of IconicState, ZoomState, NormalState,
				 * or WithdrawnState. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    NSWindow *macWin = nil;

    wmPtr->hints.initial_state = state;
    if (wmPtr->flags & WM_NEVER_MAPPED) {
	goto setStateEnd;
    }
    if (winPtr && winPtr->window) {
	macWin = TkMacOSXGetNSWindowForDrawable(winPtr->window);
    }

    /*
     * Make sure windows are updated before the state change.  As an exception,
     * do not process idle tasks before withdrawing a window.  The purpose of
     * this is to support the common paradigm of immediately withdrawing the
     * root window.  Processing idle tasks before changing the state causes the
     * root to briefly flash on the screen, which users of this paradigm find
     * annoying.  Not processing the events does not guarantee that the window
     * will not appear but makes it more likely.
     */

    if (state != WithdrawnState) {
	while (Tcl_DoOneEvent(TCL_IDLE_EVENTS)) {};
    }
    if (state == WithdrawnState) {
	TkWmUnmapWindow(winPtr);
    } else if (state == IconicState) {

	/*
	 * The window always gets unmapped. If we can show the icon version of
	 * the window we also collapse it.
	 */

	if (macWin && ([macWin styleMask] & NSMiniaturizableWindowMask) &&
		![macWin isMiniaturized]) {
	    [macWin miniaturize:NSApp];
	}
	TkWmUnmapWindow(winPtr);
    } else if (state == NormalState || state == ZoomState) {
	TkWmMapWindow(winPtr);
	[macWin deminiaturize:NSApp];
	[macWin orderFront:NSApp];
	TkMacOSXZoomToplevel(macWin, state == NormalState ? inZoomIn : inZoomOut);
    }

    /*
     * Make sure windows are updated after the state change too.  This is needed
     * in order for the event-9.11-20 tests to pass.
     */

    while (Tcl_DoOneEvent(TCL_IDLE_EVENTS)){}
setStateEnd:
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpIsWindowFloating --
 *
 *	Returns 1 if a window is floating, 0 otherwise.
 *
 * Results:
 *	1 or 0 depending on window's floating attribute.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkpIsWindowFloating(
    void *wRef)
{
    return [(NSWindow *)wRef level] == kCGFloatingWindowLevel;
}

/*
 *--------------------------------------------------------------
 *
 * TkMacOSXWindowOffset --
 *
 *	Determines the x and y offset from the orgin of the toplevel window
 *	dressing (the structure region, i.e. title bar) and the orgin of the
 *	content area.
 *
 * Results:
 *	The x & y offset in pixels.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkMacOSXWindowOffset(
    void *wRef,
    int *xOffset,
    int *yOffset)
{
    TkWindow *winPtr = TkMacOSXGetTkWindow(wRef);

    if (winPtr && winPtr->wmInfoPtr) {
	*xOffset = winPtr->wmInfoPtr->xInParent;
	*yOffset = winPtr->wmInfoPtr->yInParent;
    } else {
	*xOffset = 0;
	*yOffset = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetMS --
 *
 *	Return a relative time in milliseconds. It doesn't matter when the
 *	epoch was.
 *
 * Results:
 *	Number of milliseconds.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned long
TkpGetMS(void)
{
    Tcl_Time now;

    Tcl_GetTime(&now);
    return (long) now.sec * 1000 + now.usec / 1000;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetInputFocus --
 *
 *	Change the focus window for the application.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetInputFocus(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(int),
    TCL_UNUSED(Time))
{
    /*
     * Don't need to do a thing. Tk manages the focus for us.
     */
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpChangeFocus --
 *
 *	This function is called when Tk moves focus from one window to another.
 *      It should be passed a non-embedded TopLevel. That toplevel gets raised
 *      to the top of the Tk stacking order and the associated NSWindow is
 *      ordered Front.
 *
 * Results:
 *	The return value is the serial number of the command that changed the
 *	focus. It may be needed by the caller to filter out focus change
 *	events that were queued before the command. If the procedure doesn't
 *	actually change the focus then it returns 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkpChangeFocus(
    TkWindow *winPtr,		/* Window that is to receive the X focus. */
    int force)			/* Non-zero means claim the focus even if it
				 * didn't originally belong to topLevelPtr's
				 * application. */
{
    if (!winPtr ||
	(winPtr->flags & TK_ALREADY_DEAD) ||
	!Tk_IsMapped(winPtr) ||
	winPtr->atts.override_redirect) {
	return 0;
    }
    if (Tk_IsTopLevel(winPtr) && !Tk_IsEmbedded(winPtr)) {
	NSWindow *win = TkMacOSXGetNSWindowForDrawable(winPtr->window);

	TkWmRestackToplevel(winPtr, Above, NULL);
	if (force) {
	    [NSApp activateIgnoringOtherApps:YES];
	}
	if (win && [win canBecomeKeyWindow]) {
	    [win makeKeyAndOrderFront:NSApp];
	    [NSApp setTkEventTarget:TkMacOSXGetTkWindow(win)];
	}
    }

    /*
     * Remember the current serial number for the X server and issue a dummy
     * server request. This marks the position at which we changed the focus,
     * so we can distinguish FocusIn and FocusOut events on either side of the
     * mark.
     */

    return NextRequest(winPtr->display);
}

/*
 *----------------------------------------------------------------------
 *
 * WmStackorderToplevelWrapperMap --
 *
 *	This procedure will create a table that maps the reparent wrapper X id
 *	for a toplevel to the TkWindow structure that it wraps. Tk keeps track
 *	of a mapping from the window X id to the TkWindow structure but that
 *	does us no good here since we only get the X id of the wrapper window.
 *	Only those toplevel windows that are mapped have a position in the
 *	stacking order.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds entries to the passed hashtable.
 *
 *----------------------------------------------------------------------
 */

static void
WmStackorderToplevelWrapperMap(
    TkWindow *winPtr,		/* TkWindow to recurse on */
    Display *display,		/* X display of parent window */
    Tcl_HashTable *table)	/* Maps mac window to TkWindow */
{
    TkWindow *childPtr;
    Tcl_HashEntry *hPtr;
    int newEntry;

    if (Tk_IsMapped(winPtr) && Tk_IsTopLevel(winPtr) && !Tk_IsEmbedded(winPtr)
	    && (winPtr->display == display)) {
	hPtr = Tcl_CreateHashEntry(table,
		(void *)TkMacOSXGetNSWindowForDrawable(winPtr->window), &newEntry);
	Tcl_SetHashValue(hPtr, winPtr);
    }

    for (childPtr = winPtr->childList; childPtr != NULL;
	    childPtr = childPtr->nextPtr) {
	WmStackorderToplevelWrapperMap(childPtr, display, table);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWmStackorderToplevel --
 *
 *	This procedure returns the stack order of toplevel windows.
 *
 * Results:
 *	A NULL terminated array of pointers to tk window objects in stacking
 *	order or else NULL if there was an error.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkWindow **
TkWmStackorderToplevel(
    TkWindow *parentPtr)	/* Parent toplevel window. */
{
    TkWindow *childWinPtr, **windows, **windowPtr;
    Tcl_HashTable table;
    Tcl_HashEntry *hPtr;
    NSArray *macWindows = [NSApp orderedWindows];
    NSArray* backToFront = [[macWindows reverseObjectEnumerator] allObjects];
    NSInteger windowCount = [macWindows count];

    windows = windowPtr = (TkWindow **)ckalloc((windowCount + 1) * sizeof(TkWindow *));
    if (windows != NULL) {
	Tcl_InitHashTable(&table, TCL_ONE_WORD_KEYS);
	WmStackorderToplevelWrapperMap(parentPtr, parentPtr->display, &table);
	for (NSWindow *w in backToFront) {
	    hPtr = Tcl_FindHashEntry(&table, (char*) w);
	    if (hPtr != NULL) {
		childWinPtr = (TkWindow *)Tcl_GetHashValue(hPtr);
		*windowPtr++ = childWinPtr;
	    }
	}
	*windowPtr = NULL;
	Tcl_DeleteHashTable(&table);
    }
    return windows;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXApplyWindowAttributes --
 *
 *	This procedure applies all window attributes to the NSWindow.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkMacOSXApplyWindowAttributes(
    TkWindow *winPtr,
    NSWindow *macWindow)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;

    ApplyWindowAttributeFlagChanges(winPtr, macWindow, 0, 0, 0, 1);
    if (wmPtr->container != NULL || winPtr->atts.override_redirect) {
	ApplyContainerOverrideChanges(winPtr, macWindow);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ApplyWindowAttributeFlagChanges --
 *
 *	This procedure applies window attribute and flag changes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
ApplyWindowAttributeFlagChanges(
    TkWindow *winPtr,
    NSWindow *macWindow,
    UInt64 oldAttributes,
    int oldFlags,
    int create,
    int initial)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    UInt64 newAttributes = ForceAttributes(wmPtr->attributes, wmPtr->macClass);
    UInt64 changedAttributes = newAttributes ^ ForceAttributes(oldAttributes,
	    wmPtr->macClass);

    if (changedAttributes || wmPtr->flags != oldFlags || initial) {
	if (!macWindow) {
	    if (winPtr->window == None) {
		if (!create) {
		    return;
		}
		Tk_MakeWindowExist((Tk_Window)winPtr);
	    }
	    if (!TkMacOSXHostToplevelExists(winPtr)) {
		if (!create) {
		    return;
		}
		TkMacOSXMakeRealWindowExist(winPtr);
	    }
	    macWindow = TkMacOSXGetNSWindowForDrawable(winPtr->window);
	}
	if ((changedAttributes & kWindowCloseBoxAttribute) || initial) {
	    [[macWindow standardWindowButton:NSWindowCloseButton]
		    setEnabled:!!(newAttributes & kWindowCloseBoxAttribute)];
	}
	if ((changedAttributes & kWindowCollapseBoxAttribute) || initial) {
	    [[macWindow standardWindowButton:NSWindowMiniaturizeButton]
		    setEnabled:!!(newAttributes & kWindowCollapseBoxAttribute)];
	}
	if ((changedAttributes & (kWindowResizableAttribute |
		kWindowFullZoomAttribute)) || initial) {
	    [[macWindow standardWindowButton:NSWindowZoomButton]
		    setEnabled:(newAttributes & kWindowResizableAttribute) &&
		    (newAttributes & kWindowFullZoomAttribute)];
	    if (newAttributes & kWindowHorizontalZoomAttribute) {
		wmPtr->flags &= ~(WM_WIDTH_NOT_RESIZABLE);
	    } else {
		wmPtr->flags |= (WM_WIDTH_NOT_RESIZABLE);
	    }
	    if (newAttributes & kWindowVerticalZoomAttribute) {
		wmPtr->flags &= ~(WM_HEIGHT_NOT_RESIZABLE);
	    } else {
		wmPtr->flags |= (WM_HEIGHT_NOT_RESIZABLE);
	    }
	    WmUpdateGeom(wmPtr, winPtr);
	}
	if ((changedAttributes & kWindowToolbarButtonAttribute) || initial) {
	    [macWindow setShowsToolbarButton:
		    !!(newAttributes & kWindowToolbarButtonAttribute)];
	    if ((newAttributes & kWindowToolbarButtonAttribute) &&
		    ![macWindow toolbar]) {
		NSToolbar *toolbar = [[NSToolbar alloc] initWithIdentifier:@""];

		[toolbar setVisible:NO];
		[macWindow setToolbar:toolbar];
		[toolbar release];
		NSCell *toolbarButtonCell = [[macWindow standardWindowButton:
			NSWindowToolbarButton] cell];
		[toolbarButtonCell setTarget:[macWindow contentView]];
		[toolbarButtonCell setAction:@selector(tkToolbarButton:)];
	    }
	}
	if ((changedAttributes & kWindowNoShadowAttribute) || initial) {
	    [macWindow setHasShadow:
		    !(newAttributes & kWindowNoShadowAttribute)];
	}
	if ((changedAttributes & kWindowHideOnSuspendAttribute) || initial) {
	    [macWindow setHidesOnDeactivate:
		    !!(newAttributes & kWindowHideOnSuspendAttribute)];
	}
	if ((changedAttributes & kWindowInWindowMenuAttribute) || initial) {
	    [macWindow setExcludedFromWindowsMenu:
		    !(newAttributes & kWindowInWindowMenuAttribute)];
	}
	if ((changedAttributes & kWindowIgnoreClicksAttribute) || initial) {
	    [macWindow setIgnoresMouseEvents:
		    !!(newAttributes & kWindowIgnoreClicksAttribute)];
	}
	if ((changedAttributes & tkWindowDoesNotHideAttribute) || initial) {
	    [macWindow setCanHide:
		    !(newAttributes & tkWindowDoesNotHideAttribute)];
	}
	if ((changedAttributes & (kWindowDoesNotCycleAttribute |
		tkCanJoinAllSpacesAttribute | tkMoveToActiveSpaceAttribute)) ||
		initial) {
	    NSWindowCollectionBehavior b = NSWindowCollectionBehaviorDefault;

	    /*
	     * This behavior, which makes the green button expand a window to
	     * full screen, was included in the default as of OSX 10.13.  For
	     * uniformity we use the new default in all versions of the OS
	     * after 10.10.
	     */

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101100
	    if (!(macWindow.styleMask & NSUtilityWindowMask)) {
		/*
		 * Exclude overrideredirect, transient, and "help"-styled
		 * windows from moving into their own fullscreen space.
		 */

		if ((winPtr->atts.override_redirect) ||
			(wmPtr->container != NULL) ||
			(winPtr->wmInfoPtr->macClass == kHelpWindowClass)) {
		    b |= (NSWindowCollectionBehaviorCanJoinAllSpaces |
			    NSWindowCollectionBehaviorFullScreenAuxiliary);
		} else {
		    b |= NSWindowCollectionBehaviorFullScreenPrimary;

		    /*
		     * The default max size has height less than the screen
		     * height. This causes the window manager to refuse to
		     * allow the window to be resized when it is a split
		     * window. To work around this we make the max size equal
		     * to the screen size.  (For 10.11 and up, only)
		     */

		    if ([NSApp macOSVersion] >= 101100) {
			NSSize screenSize = [[macWindow screen] frame].size;
			[macWindow setMaxFullScreenContentSize:screenSize];
		    }
		}
	    }
#endif

	    if (newAttributes & tkCanJoinAllSpacesAttribute) {
		b |= NSWindowCollectionBehaviorCanJoinAllSpaces;
	    } else if (newAttributes & tkMoveToActiveSpaceAttribute) {
		b |= NSWindowCollectionBehaviorMoveToActiveSpace;
	    }
	    if (newAttributes & kWindowDoesNotCycleAttribute) {
		b |= NSWindowCollectionBehaviorIgnoresCycle;
	    } else {
		b |= NSWindowCollectionBehaviorParticipatesInCycle;
	    }
	    [macWindow setCollectionBehavior:b];
	}
	if ((wmPtr->flags & WM_TOPMOST) != (oldFlags & WM_TOPMOST)) {
	    [macWindow setLevel:(wmPtr->flags & WM_TOPMOST) ?
		    kCGUtilityWindowLevel : ([macWindow isKindOfClass:
		    [TKPanel class]] && [macWindow isFloatingPanel] ?
		    kCGFloatingWindowLevel : kCGNormalWindowLevel)];
	}

	/*
	 * The change of window class/attributes might have changed the window
	 * frame geometry:
	 */

	NSRect structureRect = [macWindow frameRectForContentRect:NSZeroRect];

	wmPtr->xInParent = -structureRect.origin.x;
	wmPtr->yInParent = structureRect.origin.y + structureRect.size.height;
	wmPtr->parentWidth = winPtr->changes.width + structureRect.size.width;
	wmPtr->parentHeight = winPtr->changes.height + structureRect.size.height;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ApplyContainerOverrideChanges --
 *
 *	This procedure applies changes to override_redirect or container.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
ApplyContainerOverrideChanges(
    TkWindow *winPtr,
    NSWindow *macWindow)
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    UInt64 oldAttributes = wmPtr->attributes;
    int oldFlags = wmPtr->flags;
    unsigned long styleMask;
    NSRect structureRect;
    NSWindow *parentWindow;

    if (!macWindow && winPtr->window != None &&
	    TkMacOSXHostToplevelExists(winPtr)) {
	macWindow = TkMacOSXGetNSWindowForDrawable(winPtr->window);
    }
    styleMask = [macWindow styleMask];

    /*
     * FIX: We need an UpdateWrapper equivalent to make this 100% correct
     */

    if (winPtr->atts.override_redirect) {
	if (wmPtr->macClass == kDocumentWindowClass) {
	    wmPtr->macClass = kSimpleWindowClass;
	    wmPtr->attributes = macClassAttrs[kSimpleWindowClass].defaultAttrs;
	}
	wmPtr->attributes |= kWindowNoActivatesAttribute;
	if ([NSApp macOSVersion] == 100600) {
	    styleMask = 0;
	} else {
	    styleMask &= ~NSTitledWindowMask;
	}
    } else {
	if (wmPtr->macClass == kSimpleWindowClass &&
	    (oldAttributes & kWindowNoActivatesAttribute)) {
	    wmPtr->macClass = kDocumentWindowClass;
	    wmPtr->attributes =
		    macClassAttrs[kDocumentWindowClass].defaultAttrs;
	}
	wmPtr->attributes &= ~kWindowNoActivatesAttribute;
	if ([NSApp macOSVersion] == 100600) {
	    styleMask = NSTitledWindowMask         |
			NSClosableWindowMask       |
			NSMiniaturizableWindowMask |
			NSResizableWindowMask;
	} else {
	    styleMask |= NSTitledWindowMask;
	}
    }
    if (macWindow) {
	structureRect = [NSWindow frameRectForContentRect:NSZeroRect
		styleMask:styleMask];

	/*
	 * Synchronize the wmInfoPtr to match the new window configuration
	 * so windowBoundsChanged won't corrupt the window manager info.
	 */

	wmPtr->xInParent = -structureRect.origin.x;
	wmPtr->yInParent = structureRect.origin.y + structureRect.size.height;
	wmPtr->parentWidth = winPtr->changes.width + structureRect.size.width;
	wmPtr->parentHeight = winPtr->changes.height + structureRect.size.height;
	if (winPtr->atts.override_redirect) {
	    [macWindow setExcludedFromWindowsMenu:YES];
	    [macWindow setStyleMask:styleMask];
	    if (wmPtr->hints.initial_state == NormalState) {
		[macWindow orderFront:NSApp];
	    }
	    if (wmPtr->container != NULL) {
		wmPtr->flags |= WM_TOPMOST;
	    } else {
		wmPtr->flags &= ~WM_TOPMOST;
	    }
	} else {
	    const char *title = winPtr->wmInfoPtr->titleUid;

	    if (!title) {
		title = winPtr->nameUid;
	    }
	    [macWindow setStyleMask:styleMask];
	    [macWindow setTitle:[NSString stringWithUTF8String:title]];
	    [macWindow setExcludedFromWindowsMenu:NO];
	    wmPtr->flags &= ~WM_TOPMOST;
	}
	if (wmPtr->container != NULL) {
	    TkWindow *containerWinPtr = (TkWindow *)wmPtr->container;

	    if (containerWinPtr && (containerWinPtr->window != None)
		    && TkMacOSXHostToplevelExists(containerWinPtr)) {
		NSWindow *containerMacWin = TkMacOSXGetNSWindowForDrawable(
			containerWinPtr->window);

		/*
		 * Try to add the transient window as a child window of the
		 * container. A child NSWindow retains its relative position
		 * with respect to the parent when the parent is moved.  This
		 * is pointless if the parent is offscreen, and adding a child
		 * to an offscreen window causes the parent to be displayed as
		 * a zombie.  So we only do this if the parent is visible.
		 */

		if (containerMacWin && [containerMacWin isVisible]
			&& (winPtr->flags & TK_MAPPED)) {
		    /*
		     * If the transient is already a child of some other window,
		     * remove it.
		     */

		    parentWindow = [macWindow parentWindow];
		    if (parentWindow && parentWindow != containerMacWin) {
			[parentWindow removeChildWindow:macWindow];
		    }
		    [macWindow orderFront:NSApp];
		    [containerMacWin addChildWindow:macWindow
					    ordered:NSWindowAbove];
		}
	    }
	} else {
	    parentWindow = [macWindow parentWindow];
	    if (parentWindow) {
		[parentWindow removeChildWindow:macWindow];
	    }
	}
	if (wmPtr->flags & WM_TOPMOST) {
	    [macWindow setLevel:kCGUtilityWindowLevel];
	}
	ApplyWindowAttributeFlagChanges(winPtr, macWindow, oldAttributes,
		oldFlags, 0, 0);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetMinSize --
 *
 *	This function computes the current minWidth and minHeight values for a
 *	window, taking into account the possibility that they may be
 *	defaulted.
 *
 * Results:
 *	The values at *minWidthPtr and *minHeightPtr are filled in with the
 *	minimum allowable dimensions of wmPtr's window, in grid units. If the
 *	requested minimum is smaller than the system required minimum, then
 *	this function computes the smallest size that will satisfy both the
 *	system and the grid constraints.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
GetMinSize(
    TkWindow *winPtr,		/* Toplevel window to operate on. */
    int *minWidthPtr,		/* Where to store the current minimum width of
				 * the window. */
    int *minHeightPtr)		/* Where to store the current minimum height
				 * of the window. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    int minWidth = 1, minHeight = 1;

    /*
     * Compute the minimum width & height by taking the default client size and
     * rounding it up to the nearest grid unit. Return the greater of the
     * default minimum and the specified minimum.
     */

    switch (wmPtr->macClass) {
    case kDocumentWindowClass:
    case kMovableAlertWindowClass:
    case kMovableModalWindowClass:
	minWidth = 72;
	if (wmPtr->attributes & kWindowResizableAttribute) {
	    minHeight = 15;
	}
	if (wmPtr->attributes & kWindowToolbarButtonAttribute) {
	    minWidth += 29;
	}
	break;
    case kFloatingWindowClass:
    case kUtilityWindowClass:
	minWidth = 59;
	if (wmPtr->attributes & kWindowResizableAttribute) {
	    minHeight = 11;
	}
	if (wmPtr->attributes & kWindowSideTitlebarAttribute) {
	    int tmp = minWidth;

	    minWidth = minHeight;
	    minHeight = tmp;
	} else if (wmPtr->attributes & kWindowToolbarButtonAttribute) {
	    minWidth += 29;
	}
	break;
    default:
	if (wmPtr->attributes & kWindowResizableAttribute) {
	    minWidth = 15;
	    minHeight = 15;
	}
	break;
    }

    if (wmPtr->gridWin != NULL) {
	int base = winPtr->reqWidth - (wmPtr->reqGridWidth * wmPtr->widthInc);

	if (base < 0) {
	    base = 0;
	}
	minWidth = ((minWidth - base) + wmPtr->widthInc-1)/wmPtr->widthInc;
	base = winPtr->reqHeight - (wmPtr->reqGridHeight * wmPtr->heightInc);
	if (base < 0) {
	    base = 0;
	}
	minHeight = ((minHeight - base) + wmPtr->heightInc-1)/wmPtr->heightInc;
    }
    if (minWidth < wmPtr->minWidth) {
	minWidth = wmPtr->minWidth;
    }
    if (minHeight < wmPtr->minHeight) {
	minHeight = wmPtr->minHeight;
    }
    *minWidthPtr = minWidth;
    *minHeightPtr = minHeight;
}

/*
 *----------------------------------------------------------------------
 *
 * GetMaxSize --
 *
 *	This function computes the current maxWidth and maxHeight values for a
 *	window, taking into account the possibility that they may be
 *	defaulted.
 *
 * Results:
 *	The values at *maxWidthPtr and *maxHeightPtr are filled in with the
 *	maximum allowable dimensions of wmPtr's window, in grid units. If no
 *	maximum has been specified for the window, then this function computes
 *	the largest sizes that will fit on the screen.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
GetMaxSize(
    TkWindow *winPtr,		/* Toplevel window to operate on. */
    int *maxWidthPtr,		/* Where to store the current maximum width of
				 * the window. */
    int *maxHeightPtr)		/* Where to store the current maximum height
				 * of the window. */
{
    WmInfo *wmPtr = winPtr->wmInfoPtr;
    NSRect *maxBounds = (NSRect*)(ScreenOfDisplay(winPtr->display, 0)->ext_data);

    if (wmPtr->maxWidth > 0) {
	*maxWidthPtr = wmPtr->maxWidth;
    } else {
	int maxWidth = maxBounds->size.width - wmPtr->xInParent;

	if (wmPtr->gridWin != NULL) {
	    maxWidth = wmPtr->reqGridWidth
		    + (maxWidth - winPtr->reqWidth)/wmPtr->widthInc;
	}
	*maxWidthPtr = maxWidth;
    }
    if (wmPtr->maxHeight > 0) {
	*maxHeightPtr = wmPtr->maxHeight;
    } else {
	int maxHeight = maxBounds->size.height - wmPtr->yInParent;

	if (wmPtr->gridWin != NULL) {
	    maxHeight = wmPtr->reqGridHeight
		    + (maxHeight - winPtr->reqHeight)/wmPtr->heightInc;
	}
	*maxHeightPtr = maxHeight;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RemapWindows
 *
 *	Adjust parent/child relation ships of the given window hierarchy.
 *
 * Results:
 *	none
 *
 * Side effects:
 *	keeps windowing system (X11) happy
 *
 *----------------------------------------------------------------------
 */

static void
RemapWindows(
    TkWindow *winPtr,
    MacDrawable *parentWin)
{
    TkWindow *childPtr;

    /*
     * Remove the OS specific window. It will get rebuilt when the window gets
     * Mapped.
     */

    if (winPtr->window != None) {
	MacDrawable *macWin = (MacDrawable *)winPtr->window;

	macWin->toplevel->referenceCount--;
	macWin->toplevel = parentWin->toplevel;
	macWin->toplevel->referenceCount++;
	winPtr->flags &= ~TK_MAPPED;
#ifdef TK_REBUILD_TOPLEVEL
	winPtr->flags |= TK_REBUILD_TOPLEVEL;
#endif
    }

    /*
     * Repeat for all the children.
     */

    for (childPtr = winPtr->childList; childPtr != NULL;
	    childPtr = childPtr->nextPtr) {
	RemapWindows(childPtr, (MacDrawable *)winPtr->window);
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
