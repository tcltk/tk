/*
 * tkMacOSXMenus.c --
 *
 *	These calls set up the default menus for Tk.
 *
 * Copyright © 1995-1996 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkMenu.h"
#include "tkMacOSXConstants.h"

static void		GenerateEditEvent(const char *name);
static Tcl_Obj *	GetWidgetDemoPath(Tcl_Interp *interp);


#pragma mark TKApplication(TKMenus)

@implementation TKApplication(TKMenus)
- (void) _setupMenus
{
    if (_defaultMainMenu) {
	return;
    }
    TkMenuInit();

    NSString *applicationName = [[NSBundle mainBundle]
	    objectForInfoDictionaryKey:@"CFBundleName"];

    if (!applicationName) {
	applicationName = [[NSProcessInfo processInfo] processName];
    }

    NSString *aboutName = (applicationName &&
	    ![applicationName isEqualToString:@"Wish"] &&
	    ![applicationName hasPrefix:@"tclsh"]) ?
	    applicationName : @"Tcl & Tk";

    _servicesMenu = [NSMenu menuWithTitle:@"Services"];
    _defaultApplicationMenuItems = [[NSArray arrayWithObjects:
	    [NSMenuItem separatorItem],
	    [NSMenuItem itemWithTitle:
		   [NSString stringWithFormat:@"Preferences%C", 0x2026]
		   action:@selector(preferences:) keyEquivalent:@","],
	    [NSMenuItem separatorItem],
	    [NSMenuItem itemWithTitle:@"Services" submenu:_servicesMenu],
	    [NSMenuItem separatorItem],
	    [NSMenuItem itemWithTitle:
		   [NSString stringWithFormat:@"Hide %@", applicationName]
		   action:@selector(hide:) keyEquivalent:@"h"],
	    [NSMenuItem itemWithTitle:@"Hide Others"
		   action:@selector(hideOtherApplications:) keyEquivalent:@"h"
		   keyEquivalentModifierMask:
		   NSCommandKeyMask|NSAlternateKeyMask],
	    [NSMenuItem itemWithTitle:@"Show All"
		   action:@selector(unhideAllApplications:)],
	    [NSMenuItem separatorItem],
	    [NSMenuItem itemWithTitle:
		   [NSString stringWithFormat:@"Quit %@", applicationName]
		   action: @selector(terminate:) keyEquivalent:@"q"],
	    nil] retain];
    _defaultApplicationMenu = [TKMenu menuWithTitle:applicationName
	    menuItems:_defaultApplicationMenuItems];
    [_defaultApplicationMenu insertItem:
	    [NSMenuItem itemWithTitle:
		    [NSString stringWithFormat:@"About %@", aboutName]
		    action:@selector(orderFrontStandardAboutPanel:)] atIndex:0];
    _defaultFileMenuItems =
	    [[NSArray arrayWithObjects:
	    [NSMenuItem itemWithTitle:
		   [NSString stringWithFormat:@"Source%C", 0x2026]
		   action:@selector(tkSource:)],
	    [NSMenuItem itemWithTitle:@"Run Widget Demo"
		   action:@selector(tkDemo:)],
	    [NSMenuItem itemWithTitle:@"Close" action:@selector(performClose:)
		   target:nil keyEquivalent:@"w"],
	    nil] retain];
    _demoMenuItem = [_defaultFileMenuItems objectAtIndex:1];
    TKMenu *fileMenu = [TKMenu menuWithTitle:@"File"
	    menuItems: _defaultFileMenuItems];
    TKMenu *editMenu = [TKMenu menuWithTitle:@"Edit" menuItems:
	    [NSArray arrayWithObjects:
	    [NSMenuItem itemWithTitle:@"Undo" action:@selector(undo:)
		   target:nil keyEquivalent:@"z"],
	    [NSMenuItem itemWithTitle:@"Redo" action:@selector(redo:)
		   target:nil keyEquivalent:@"y"],
	    [NSMenuItem separatorItem],
	    [NSMenuItem itemWithTitle:@"Cut" action:@selector(cut:)
		   target:nil keyEquivalent:@"x"],
	    [NSMenuItem itemWithTitle:@"Copy" action:@selector(copy:)
		   target:nil keyEquivalent:@"c"],
	    [NSMenuItem itemWithTitle:@"Paste" action:@selector(paste:)
		   target:nil keyEquivalent:@"v"],
	    [NSMenuItem itemWithTitle:@"Delete" action:@selector(delete:)
		   target:nil],
	    nil]];

    _defaultWindowsMenuItems = [NSArray arrayWithObjects:
	    [NSMenuItem itemWithTitle:@"Minimize"
		   action:@selector(performMiniaturize:) target:nil
		   keyEquivalent:@"m"],
	    [NSMenuItem itemWithTitle:@"Zoom" action:@selector(performZoom:)
		   target:nil],
	    nil];

    /*
     * On OS X 10.12 we get duplicate tab control items if we create them here.
     */

    if ([NSApp macOSVersion] >= 101300) {
	_defaultWindowsMenuItems = [_defaultWindowsMenuItems
	     arrayByAddingObjectsFromArray:
	     [NSArray arrayWithObjects:
		    [NSMenuItem separatorItem],
		    [NSMenuItem itemWithTitle:@"Show Previous Tab"
			   action:@selector(selectPreviousTab:)
			   target:nil
			   keyEquivalent:@"\t"
			   keyEquivalentModifierMask:
			       NSControlKeyMask|NSShiftKeyMask],
		    [NSMenuItem itemWithTitle:@"Show Next Tab"
			   action:@selector(selectNextTab:)
			   target:nil
			   keyEquivalent:@"\t"
			   keyEquivalentModifierMask:NSControlKeyMask],
		    [NSMenuItem itemWithTitle:@"Move Tab To New Window"
			   action:@selector(moveTabToNewWindow:)
			   target:nil],
		    [NSMenuItem itemWithTitle:@"Merge All Windows"
			   action:@selector(mergeAllWindows:)
			   target:nil],
		    [NSMenuItem separatorItem],
		    nil]];
    }
    _defaultWindowsMenuItems = [_defaultWindowsMenuItems arrayByAddingObject:
	    [NSMenuItem itemWithTitle:@"Bring All to Front"
		   action:@selector(arrangeInFront:)]];
    [_defaultWindowsMenuItems retain];
    TKMenu *windowsMenu = [TKMenu menuWithTitle:@"Window" menuItems:
				      _defaultWindowsMenuItems];
    _defaultHelpMenuItems = [[NSArray arrayWithObjects:
	    [NSMenuItem itemWithTitle:
		   [NSString stringWithFormat:@"%@ Help", applicationName]
		   action:@selector(showHelp:) keyEquivalent:@"?"],
	    nil] retain];
    TKMenu *helpMenu = [TKMenu menuWithTitle:@"Help" menuItems:
	    _defaultHelpMenuItems];
    [self setServicesMenu:_servicesMenu];
    [self setWindowsMenu:windowsMenu];
    _defaultMainMenu = [[TKMenu menuWithTitle:@"" submenus:[NSArray
	    arrayWithObjects:_defaultApplicationMenu, fileMenu, editMenu,
	    windowsMenu, helpMenu, nil]] retain];
    [_defaultMainMenu setSpecial:tkMainMenu];
    [_defaultApplicationMenu setSpecial:tkApplicationMenu];
    [windowsMenu setSpecial:tkWindowsMenu];
    [helpMenu setSpecial:tkHelpMenu];
    [self tkSetMainMenu:nil];
}

- (void) dealloc
{
    [_defaultMainMenu release];
    [_defaultHelpMenuItems release];
    [_defaultWindowsMenuItems release];
    [_defaultApplicationMenuItems release];
    [_defaultFileMenuItems release];
    [super dealloc];
}

- (BOOL) validateUserInterfaceItem: (id <NSValidatedUserInterfaceItem>) anItem
{
    SEL action = [anItem action];

    if (sel_isEqual(action, @selector(preferences:))) {
	return (_eventInterp && Tcl_FindCommand(_eventInterp,
		"::tk::mac::ShowPreferences", NULL, 0));
    } else if (sel_isEqual(action, @selector(tkDemo:))) {
	BOOL haveDemo = NO;

	if (_eventInterp) {
	    Tcl_Obj *path = GetWidgetDemoPath(_eventInterp);

	    if (path) {
		Tcl_IncrRefCount(path);
		haveDemo = (Tcl_FSAccess(path, R_OK) == 0);
		Tcl_DecrRefCount(path);
	    }
	}
	return haveDemo;
    } else {
	return [super validateUserInterfaceItem:anItem];
    }
}

- (void) orderFrontStandardAboutPanel: (id) sender
{
    (void)sender;

    if (!_eventInterp || !Tcl_FindCommand(_eventInterp, "tkAboutDialog",
	    NULL, 0) || (GetCurrentEventKeyModifiers() & optionKey)) {
	[super orderFrontStandardAboutPanel:NSApp];
    } else {
	int code = Tcl_EvalEx(_eventInterp, "tkAboutDialog", TCL_INDEX_NONE,
		TCL_EVAL_GLOBAL);

	if (code != TCL_OK) {
	    Tcl_BackgroundException(_eventInterp, code);
	}
	Tcl_ResetResult(_eventInterp);
    }
}

- (void) showHelp: (id) sender
{
    if (!_eventInterp || !Tcl_FindCommand(_eventInterp,
	    "::tk::mac::ShowHelp", NULL, 0)) {
	[super showHelp:sender];
    } else {
	int code = Tcl_EvalEx(_eventInterp, "::tk::mac::ShowHelp", TCL_INDEX_NONE,
		TCL_EVAL_GLOBAL);

	if (code != TCL_OK) {
	    Tcl_BackgroundException(_eventInterp, code);
	}
	Tcl_ResetResult(_eventInterp);
    }
}

- (void) tkSource: (id) sender
{
    (void)sender;

    if (_eventInterp) {
	if (Tcl_EvalEx(_eventInterp, "tk_getOpenFile -filetypes {"
		"{{TCL Scripts} {.tcl} TEXT} {{Text Files} {} TEXT}}",
		TCL_INDEX_NONE, TCL_EVAL_GLOBAL) == TCL_OK) {
	    Tcl_Obj *path = Tcl_GetObjResult(_eventInterp);
	    Tcl_Size len;

	    Tcl_GetStringFromObj(path, &len);
	    if (len) {
		Tcl_IncrRefCount(path);

		int code = Tcl_FSEvalFileEx(_eventInterp, path, NULL);

		if (code != TCL_OK) {
		    Tcl_BackgroundException(_eventInterp, code);
		}
		Tcl_DecrRefCount(path);
	    }
	}
	Tcl_ResetResult(_eventInterp);
    }
}

- (void) tkDemo: (id) sender
{
	(void)sender;

    if (_eventInterp) {
	Tcl_Obj *path = GetWidgetDemoPath(_eventInterp);

	if (path) {
	    Tcl_IncrRefCount(path);

	    [_demoMenuItem setHidden:YES];
	    int code = Tcl_FSEvalFileEx(_eventInterp, path, NULL);

	    if (code != TCL_OK) {
		Tcl_BackgroundException(_eventInterp, code);
	    }
	    Tcl_DecrRefCount(path);
	    Tcl_ResetResult(_eventInterp);
	}
    }
}
@end

#pragma mark TKContentView(TKMenus)

@implementation TKContentView(TKMenus)

- (BOOL) validateUserInterfaceItem: (id <NSValidatedUserInterfaceItem>) anItem
{
    (void)anItem;

    return YES;
}

#define EDIT_ACTION(a, e) \
    - (void) a: (id) sender \
    { \
	if ([sender isKindOfClass:[NSMenuItem class]]) { \
	    GenerateEditEvent(#e); \
	} \
    }
EDIT_ACTION(cut, Cut)
EDIT_ACTION(copy, Copy)
EDIT_ACTION(paste, Paste)
EDIT_ACTION(delete, Clear)
EDIT_ACTION(undo, Undo)
EDIT_ACTION(redo, Redo)
#undef EDIT_ACTION
@end

#pragma mark -

/*
 *----------------------------------------------------------------------
 *
 * GetWidgetDemoPath --
 *
 *	Get path to the widget demo.
 *
 * Results:
 *	pathObj with ref count 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *
GetWidgetDemoPath(
    Tcl_Interp *interp)
{
    Tcl_Obj *result = NULL;

    if (Tcl_EvalEx(interp, "::tk::pkgconfig get demodir,runtime",
		   TCL_INDEX_NONE, TCL_EVAL_GLOBAL) == TCL_OK) {
	Tcl_Obj *libpath, *demo[1] = { Tcl_NewStringObj("widget", 6) };

	libpath = Tcl_GetObjResult(interp);
	Tcl_IncrRefCount(libpath);
	Tcl_IncrRefCount(demo[0]);
	result = Tcl_FSJoinToPath(libpath, 1, demo);
	Tcl_DecrRefCount(demo[0]);
	Tcl_DecrRefCount(libpath);
    }
    Tcl_ResetResult(interp);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXHandleMenuSelect --
 *
 *	Handles events that occur in the Menu bar.
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
TkMacOSXHandleMenuSelect(
    TCL_UNUSED(short),
    TCL_UNUSED(unsigned short),
    TCL_UNUSED(int))
{
    Tcl_Panic("TkMacOSXHandleMenuSelect: Obsolete, no more Carbon!");
}

/*
 *----------------------------------------------------------------------
 *
 * GenerateEditEvent --
 *
 *	Takes an edit menu item and posts the corasponding a virtual event to
 *	Tk's event queue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May place events of queue.
 *
 *----------------------------------------------------------------------
 */

static void
GenerateEditEvent(
    const char *name)
{
    union {XEvent general; XVirtualEvent virt;} event;
    int x, y;
    TkWindow *winPtr = TkMacOSXGetTkWindow([NSApp keyWindow]);
    Tk_Window tkwin;

    if (!winPtr) {
	return;
    }
    tkwin = (Tk_Window)winPtr->dispPtr->focusPtr;
    if (!tkwin) {
	return;
    }
    bzero(&event, sizeof(event));
    event.virt.type = VirtualEvent;
    event.virt.serial = LastKnownRequestProcessed(Tk_Display(tkwin));
    event.virt.send_event = false;
    event.virt.display = Tk_Display(tkwin);
    event.virt.event = Tk_WindowId(tkwin);
    event.virt.root = XRootWindow(Tk_Display(tkwin), 0);
    event.virt.subwindow = None;
    event.virt.time = TkpGetMS();
    XQueryPointer(NULL, winPtr->window, NULL, NULL,
	    &event.virt.x_root, &event.virt.y_root, &x, &y, &event.virt.state);
    Tk_TopCoordsToWindow(tkwin, x, y, &event.virt.x, &event.virt.y);
    event.virt.same_screen = true;
    event.virt.name = Tk_GetUid(name);
    Tk_QueueWindowEvent(&event.general, TCL_QUEUE_TAIL);
}

#pragma mark -

#pragma mark NSMenu & NSMenuItem Utilities

@implementation NSMenu(TKUtils)

+ (id) menuWithTitle: (NSString *) title
{
    NSMenu *m = [[self alloc] initWithTitle:title];

    return [m autorelease];
}

+ (id) menuWithTitle: (NSString *) title menuItems: (NSArray *) items
{
    NSMenu *m = [[self alloc] initWithTitle:title];

    for (NSMenuItem *i in items) {
	[m addItem:i];
    }
    return [m autorelease];
}

+ (id) menuWithTitle: (NSString *) title submenus: (NSArray *) submenus
{
    NSMenu *m = [[self alloc] initWithTitle:title];

    for (NSMenu *i in submenus) {
	[m addItem:[NSMenuItem itemWithSubmenu:i]];
    }
    return [m autorelease];
}

- (NSMenuItem *) itemWithSubmenu: (NSMenu *) submenu
{
    return [self itemAtIndex:[self indexOfItemWithSubmenu:submenu]];
}

- (NSMenuItem *) itemInSupermenu
{
    NSMenu *supermenu = [self supermenu];

    return (supermenu ? [supermenu itemWithSubmenu:self] : nil);
}
@end

@implementation NSMenuItem(TKUtils)

+ (id) itemWithSubmenu: (NSMenu *) submenu
{
    NSMenuItem *i = [[self alloc] initWithTitle:[submenu title] action:NULL
	    keyEquivalent:@""];

    [i setSubmenu:submenu];
    return [i autorelease];
}

+ (id) itemWithTitle: (NSString *) title submenu: (NSMenu *) submenu
{
    NSMenuItem *i = [[self alloc] initWithTitle:title action:NULL
	    keyEquivalent:@""];

    [i setSubmenu:submenu];
    return [i autorelease];
}

+ (id) itemWithTitle: (NSString *) title action: (SEL) action
{
    NSMenuItem *i = [[self alloc] initWithTitle:title action:action
	    keyEquivalent:@""];

    [i setTarget:NSApp];
    return [i autorelease];
}

+ (id) itemWithTitle: (NSString *) title action: (SEL) action
	target: (id) target
{
    NSMenuItem *i = [[self alloc] initWithTitle:title action:action
	    keyEquivalent:@""];

    [i setTarget:target];
    return [i autorelease];
}

+ (id) itemWithTitle: (NSString *) title action: (SEL) action
	keyEquivalent: (NSString *) keyEquivalent
{
    NSMenuItem *i = [[self alloc] initWithTitle:title action:action
	    keyEquivalent:keyEquivalent];

    [i setTarget:NSApp];
    return [i autorelease];
}

+ (id) itemWithTitle: (NSString *) title action: (SEL) action
	target: (id) target keyEquivalent: (NSString *) keyEquivalent
{
    NSMenuItem *i = [[self alloc] initWithTitle:title action:action
	    keyEquivalent:keyEquivalent];

    [i setTarget:target];
    return [i autorelease];
}

+ (id) itemWithTitle: (NSString *) title action: (SEL) action
	keyEquivalent: (NSString *) keyEquivalent
	keyEquivalentModifierMask: (NSUInteger) keyEquivalentModifierMask
{
    NSMenuItem *i = [[self alloc] initWithTitle:title action:action
	    keyEquivalent:keyEquivalent];

    [i setTarget:NSApp];
    [i setKeyEquivalentModifierMask:keyEquivalentModifierMask];
    return [i autorelease];
}

+ (id) itemWithTitle: (NSString *) title action: (SEL) action
	target: (id) target keyEquivalent: (NSString *) keyEquivalent
	keyEquivalentModifierMask: (NSUInteger) keyEquivalentModifierMask
{
    NSMenuItem *i = [[self alloc] initWithTitle:title action:action
	    keyEquivalent:keyEquivalent];

    [i setTarget:target];
    [i setKeyEquivalentModifierMask:keyEquivalentModifierMask];
    return [i autorelease];
}
@end

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
