---
CommandName: wm
title: wm
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - toplevel(n)
 - winfo(n)
Keywords:
 - aspect ratio
 - deiconify
 - focus model
 - geometry
 - grid
 - group
 - icon
 - iconify
 - increments
 - position
 - size
 - title
 - top-level window
 - units
 - window manager
Copyright:
 - Copyright (c) 1991-1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

wm - Communicate with window manager

# Synopsis

::: {.synopsis} :::
[wm]{.cmd} [option]{.arg} [window]{.arg} [args]{.optarg}
:::

# Description

The **wm** command is used to interact with window managers in order to control such things as the title for a window, its geometry, or the increments in terms of which it may be resized.  The **wm** command can take any of a number of different forms, depending on the *option* argument.  All of the forms expect at least one additional argument, *window*, which must be the path name of a top-level window.

The legal forms for the **wm** command are:

[wm]{.cmd} [aspect]{.sub} [window]{.arg} [minNumer minDenom maxNumer maxDenom]{.optarg}
: If *minNumer*, *minDenom*, *maxNumer*, and *maxDenom* are all specified, then they will be passed to the window manager and the window manager should use them to enforce a range of acceptable aspect ratios for *window*.  The aspect ratio of *window* (width/length) will be constrained to lie between *minNumer*/*minDenom* and *maxNumer*/*maxDenom*. If *minNumer* etc. are all specified as empty strings, then any existing aspect ratio restrictions are removed. If *minNumer* etc. are specified, then the command returns an empty string.  Otherwise, it returns a Tcl list containing four elements, which are the current values of *minNumer*, *minDenom*, *maxNumer*, and *maxDenom* (if no aspect restrictions are in effect, then an empty string is returned).

[wm]{.cmd} [attributes]{.sub} [window]{.arg}
: see below ...

**wm attributes** *window* ?[option]?
: see below ...

**wm attributes** *window* ?**option value option value...**?
: This subcommand returns or sets platform specific attributes associated with a window. The first form returns a list of the platform specific flags and their values. The second form returns the value for the specific option. The third form sets one or more of the values. The values are as follows:

    All platforms support the following attributes (though X11 users should see the notes below):

    [-alpha]{.lit}
    : Specifies the alpha transparency level of the toplevel. It accepts a value from **0.0** (fully transparent) to **1.0** (opaque).  Values outside that range will be constrained.  Where not supported, the **-alpha** value remains at **1.0**.

    **-appearance**
    : Specifies whether the window is rendered in "dark mode" on Windows or macOS; on linux this command has no effect.  Allowed values are **auto**, **light** and **dark**.  On macOS, if the setting is **auto** then the appearance of the window is controlled by the System Settings.  On Windows **auto** is equivalent to **light**.

    **-fullscreen**
    : Places the window in a mode that takes up the entire screen, has no borders, and covers the general use area (i.e. Start menu and taskbar on Windows, dock and menubar on macOS, general window decorations on X11).

    **-topmost**
    : Specifies whether this is a topmost window (displays above all other windows).


    On Windows, the following attributes may be set.

    **-disabled**
    : Specifies whether the window is in a disabled state.

    **-toolwindow**
    : Specifies a toolwindow style window (as defined in the MSDN).

    **-transparentcolor**
    : Specifies the transparent color index of the toplevel.  It takes any color value accepted by [Tk\_GetColor][GetColor].  If the empty string is specified (default), no transparent color is used.  This is supported on Windows 2000/XP+.  Where not supported, the **-transparentcolor** value remains at **{}**.


    On macOS, the following attributes may be set.

    **-class**
    : Specifies whether the underlying Aqua window for a toplevel is an object of the NSWindow class or the NSPanel class.  The two allowed values for this option are **nswindow** and **nspanel**.  It is not possible to change the class of the underlying Aqua window once that window has been instantiated, and attempting to do that is an error.  However it is allowed to set this option for a pathname that does not correspond to an existing window.  Doing that causes the class name to be cached for later use.  When a toplevel with that pathname is eventually created, the cached class name will determine which class is used for the underlying Aqua window.

    **-modified**
    : Specifies the modification state of the window (determines whether the window close widget contains the modification indicator and whether the proxy icon is draggable).

    **-notify**
    : Specifies process notification state (bouncing of the application dock icon).

    **-stylemask**
    : Specifies an integer to be assigned as the styleMask of the underlying Aqua window.  (See the Apple documentation for styleMask property of the NSWindow class.) The value of this option should be a list of bitnames.  Each bit named in the list will be set to 1, and all other bits will be set to 0.  The allowed bitnames are: **titled**, **closable**, **miniaturizable**, **resizable**, **fullsizecontentview**, **docmodal**, **utility**, **nonactivatingpanel**, and **HUDwindow**.  Note that a side effect of setting the fullsizecontentview bit is that the window title bar becomes transparent.

    **-tabbingid**
    : Controls how tabbed toplevel windows are grouped together.  Two tabs in the same group must correspond to toplevels with the same tabbingid, which can be an arbitrary UTF8 string.  In the Tk implementation, changing the tabbingid of a toplevel in a tab group will cause it to be moved into a different group, in which all tabs have the new tabbingid or, if the new tabbingid is unique, to become a normal non-tabbed toplevel.   It is allowed to set the tabbingid before the toplevel is created.  If the pathname provided in the command does not correspond to a toplevel, the value will be cached and used later when the toplevel is actually created.

    **-tabbingmode**
    : Controls whether a toplevel can be opened as a tab within a tab group. The allowed values are **auto**, **preferred** or **disallowed**.  A toplevel can be opened as a tab in exactly two situations: if its tabbingmode is **preferred**; or if its tabbingmode is **auto** and the user has selected "prefer tabs when opening documents" in the Desktop and Dock panel of the System Settings application.  It is allowed to set the tabbingmode before the toplevel is created.  If the pathname provided in the command does not correspond to a toplevel, the value will be cached and used later when the toplevel is actually created.

    **-titlepath**
    : Specifies the path of the file referenced as the window proxy icon (which can be dragged and dropped in lieu of the file's finder icon).

    **-transparent**
    : Makes the window content area transparent and turns off the window shadow. For the transparency to be effective, the toplevel background needs to be set to a color with some alpha, e.g. "systemTransparent".


    On X11, the following attributes may be set. These are not supported by all window managers, and will have no effect under older WMs.

    **-type**
    : [Requests that the window should be interpreted by the window manager as being of the specified type(s). This may cause the window to be decorated in a different way or otherwise managed differently, though exactly what happens is entirely up to the window manager. A list of types may be used, in order of preference. The following values are mapped to constants defined in the EWMH specification (using others is possible, but not advised):]{version="8.6"}

    [**desktop**]{version="8.6"}
    : [indicates a desktop feature,]{version="8.6"}

    [**dock**]{version="8.6"}
    : [indicates a dock/panel feature,]{version="8.6"}

    [**toolbar**]{version="8.6"}
    : [indicates a toolbar window that should be acting on behalf of another window, as indicated with **wm transient**,]{version="8.6"}

    [[menu]]{version="8.6"}
    : [indicates a torn-off menu that should be acting on behalf of another window, as indicated with **wm transient**,]{version="8.6"}

    [**utility**]{version="8.6"}
    : [indicates a utility window (e.g., palette or toolbox) that should be acting on behalf of another window, as indicated with **wm transient**,]{version="8.6"}

    [**splash**]{version="8.6"}
    : [indicates a splash screen, displayed during application start up,]{version="8.6"}

    [**dialog**]{version="8.6"}
    : [indicates a general dialog window, that should be acting on behalf of another window, as indicated with **wm transient**,]{version="8.6"}

    [**dropdown\_menu**]{version="8.6"}
    : [indicates a menu summoned from a menu bar, which should usually also be set to be override-redirected (with **wm overrideredirect**),]{version="8.6"}

    [**popup\_menu**]{version="8.6"}
    : [indicates a popup menu, which should usually also be set to be override-redirected (with **wm overrideredirect**),]{version="8.6"}

    [**tooltip**]{version="8.6"}
    : [indicates a tooltip window, which should usually also be set to be override-redirected (with **wm overrideredirect**),]{version="8.6"}

    [**notification**]{version="8.6"}
    : [indicates a window that provides a background notification of some event, which should usually also be set to be override-redirected (with **wm overrideredirect**),]{version="8.6"}

    [**combo**]{version="8.6"}
    : [indicates the drop-down list of a combobox widget, which should usually also be set to be override-redirected (with **wm overrideredirect**),]{version="8.6"}

    [**dnd**]{version="8.6"}
    : [indicates a window that represents something being dragged, which should usually also be set to be override-redirected (with **wm overrideredirect**),]{version="8.6"}

    [**normal**]{version="8.6"}
    : [indicates a window that has no special interpretation.]{version="8.6"}


[-zoomed]{.lit}
: Requests that the window should be maximized. This is the same as **wm state zoomed** on Windows and macOS.


On X11, changes to window attributes are performed asynchronously. Querying the value of an attribute returns the current state, which will not be the same as the value most recently set if the window manager has not yet processed the request or if it does not support the attribute.

[wm]{.cmd} [client]{.sub} [window]{.arg} [name]{.optarg}
: If *name* is specified, this command stores *name* (which should be the name of the host on which the application is executing) in *window*'s **WM\_CLIENT\_MACHINE** property for use by the window manager or session manager. The command returns an empty string in this case. If *name* is not specified, the command returns the last name set in a **wm client** command for *window*. If *name* is specified as an empty string, the command deletes the **WM\_CLIENT\_MACHINE** property from *window*.

[wm]{.cmd} [colormapwindows]{.sub} [window]{.arg} [windowList]{.optarg}
: This command is used to manipulate the **WM\_COLORMAP\_WINDOWS** property, which provides information to the window managers about windows that have private colormaps.

    If *windowList* is not specified, the command returns a list whose elements are the names of the windows in the **WM\_COLORMAP\_WINDOWS** property. If *windowList* is specified, it consists of a list of window path names;  the command overwrites the **WM\_COLORMAP\_WINDOWS** property with the given windows and returns an empty string. The **WM\_COLORMAP\_WINDOWS** property should normally contain a list of the internal windows within *window* whose colormaps differ from their parents.

    The order of the windows in the property indicates a priority order: the window manager will attempt to install as many colormaps as possible from the head of this list when *window* gets the colormap focus. If *window* is not included among the windows in *windowList*, Tk implicitly adds it at the end of the **WM\_COLORMAP\_WINDOWS** property, so that its colormap is lowest in priority. If **wm colormapwindows** is not invoked, Tk will automatically set the property for each top-level window to all the internal windows whose colormaps differ from their parents, followed by the top-level itself;  the order of the internal windows is undefined. See the ICCCM documentation for more information on the **WM\_COLORMAP\_WINDOWS** property.

[wm]{.cmd} [command]{.sub} [window]{.arg} [value]{.optarg}
: If *value* is specified, this command stores *value* in *window*'s **WM\_COMMAND** property for use by the window manager or session manager and returns an empty string. *Value* must have proper list structure;  the elements should contain the words of the command used to invoke the application. If *value* is not specified then the command returns the last value set in a **wm command** command for *window*. If *value* is specified as an empty string, the command deletes the **WM\_COMMAND** property from *window*.

[wm]{.cmd} [deiconify]{.sub} [window]{.arg}
: Arrange for *window* to be displayed in normal (non-iconified) form. This is done by mapping the window.  If the window has never been mapped then this command will not map the window, but it will ensure that when the window is first mapped it will be displayed in de-iconified form.  On Windows, a deiconified window will also be raised and be given the focus (made the active window). Returns an empty string.

[wm]{.cmd} [focusmodel]{.sub} [window]{.arg} [active=|§passive]{.optlit}
: If **active** or **passive** is supplied as an optional argument to the command, then it specifies the focus model for *window*. In this case the command returns an empty string.  If no additional argument is supplied, then the command returns the current focus model for *window*.

    An **active** focus model means that *window* will claim the input focus for itself or its descendants, even at times when the focus is currently in some other application.  **Passive** means that *window* will never claim the focus for itself:  the window manager should give the focus to *window* at appropriate times.  However, once the focus has been given to *window* or one of its descendants, the application may re-assign the focus among *window*'s descendants. The focus model defaults to **passive**, and Tk's [focus] command assumes a passive model of focusing.

[wm]{.cmd} [forget]{.sub} [window]{.arg}
: The *window* will be unmapped from the screen and will no longer be managed by **wm**.  Windows created with the [toplevel] command will be treated like [frame] windows once they are no longer managed by **wm**, however, the **-menu** configuration will be remembered and the menus will return once the widget is managed again.

[wm]{.cmd} [frame]{.sub} [window]{.arg}
: If *window* has been reparented by the window manager into a decorative frame, the command returns the platform specific window identifier for the outermost frame that contains *window* (the window whose parent is the root or virtual root).  If *window* has not been reparented by the window manager then the command returns the platform specific window identifier for *window*.

[wm]{.cmd} [geometry]{.sub} [window]{.arg} [newGeometry]{.optarg}
: If *newGeometry* is specified, then the geometry of *window* is changed and an empty string is returned.  Otherwise the current geometry for *window* is returned (this is the most recent geometry specified either by manual resizing or in a **wm geometry** command).  *NewGeometry* has the form **=***width***x***height***±***x***±***y*, where any of **=**, *width***x***height*, or **±***x***±***y* may be omitted.  *Width* and *height* are positive integers specifying the desired dimensions of *window*.  If *window* is gridded (see [Gridded geometry management] below) then the dimensions are specified in grid units;  otherwise they are specified in pixel units.

    *X* and *y* specify the desired location of *window* on the screen, in pixels. If *x* is preceded by **+**, it specifies the number of pixels between the left edge of the screen and the left edge of *window*'s border;  if preceded by **-** then *x* specifies the number of pixels between the right edge of the screen and the right edge of *window*'s border.  If *y* is preceded by **+** then it specifies the number of pixels between the top of the screen and the top of *window*'s border;  if *y* is preceded by **-** then it specifies the number of pixels between the bottom of *window*'s border and the bottom of the screen.

    If *newGeometry* is specified as an empty string then any existing user-specified geometry for *window* is cancelled, and the window will revert to the size requested internally by its widgets.

    Note that this is related to [winfo geometry][winfo], but not the same. That can only query the geometry, and always reflects Tk's current understanding of the actual size and location of *window*, whereas **wm geometry** allows both setting and querying of the *window manager*'s understanding of the size and location of the window. This can vary significantly, for example to reflect the addition of decorative elements to *window* such as title bars, and window managers are not required to precisely follow the requests made through this command.

[wm]{.cmd} [grid]{.sub} [window]{.arg} [baseWidth baseHeight widthInc heightInc]{.optarg}
: This command indicates that *window* is to be managed as a gridded window. It also specifies the relationship between grid units and pixel units. *BaseWidth* and *baseHeight* specify the number of grid units corresponding to the pixel dimensions requested internally by *window* using [Tk\_GeometryRequest][GeomReq].  *WidthInc* and *heightInc* specify the number of pixels in each horizontal and vertical grid unit. These four values determine a range of acceptable sizes for *window*, corresponding to grid-based widths and heights that are non-negative integers. Tk will pass this information to the window manager;  during manual resizing, the window manager will restrict the window's size to one of these acceptable sizes.

    Furthermore, during manual resizing the window manager will display the window's current size in terms of grid units rather than pixels. If *baseWidth* etc. are all specified as empty strings, then *window* will no longer be managed as a gridded window.  If *baseWidth* etc. are specified then the return value is an empty string.

    Otherwise the return value is a Tcl list containing four elements corresponding to the current *baseWidth*, *baseHeight*, *widthInc*, and *heightInc*;  if *window* is not currently gridded, then an empty string is returned.

    Note that this command should not be needed very often, since the [Tk\_SetGrid][SetGrid] library procedure and the **setGrid** option provide easier access to the same functionality.

[wm]{.cmd} [group]{.sub} [window]{.arg} [pathName]{.optarg}
: If *pathName* is specified, it gives the path name for the leader of a group of related windows.  The window manager may use this information, for example, to unmap all of the windows in a group when the group's leader is iconified.  *PathName* may be specified as an empty string to remove *window* from any group association.  If *pathName* is specified then the command returns an empty string;  otherwise it returns the path name of *window*'s current group leader, or an empty string if *window* is not part of any group.

[wm]{.cmd} [iconbadge]{.sub} [window]{.arg} [badge]{.arg}
: Sets a badge for the icon of the *window*. The badge can be a positive integer number, for instance the number of new or unread messages, or an exclamation point denoting attention needed. If the badge is an empty string, the badge image is removed from the application icon. Managing these changes through bindings, such as <FocusIn>, is the responsibility of the developer.

    On X11, for this command to work, the variable **::tk::icons::base\_icon($window)** must be set to the image that is being used for the window icon of $window. On Windows and X11, the iconphoto images work best at 32x32 or a similar dimension, as the badge images are provided by Tk and drawn to overlay the icon images using native (Windows) API's or Tk rendering. On macOS, the icon badge is rendered by a system API and is not provided by Tk. The icon image itself should be higher-resolution, preferably 512 pixels, to avoid being blurry.

    The icon badge is intended for display in the Dock (macOS), taskbar (Windows) or app panel (X11). On macOS, the last badge called will be displayed in the Dock, regardless of how many different icon badges may be assigned to different windows. On Windows, the taskbar display depends on whether the taskbar buttons are combined or not (this is an OS setting available to the user): if combined, the behavior is the same as on macOS, otherwise each button in the taskbar shows the badge it was assigned. Badge display on macOS is configured in the system preferences. App panel display behavior on X11 will depend on the window manager and/or desktop environment.

[wm]{.cmd} [iconbitmap]{.sub} [window]{.arg} [bitmap]{.optarg}
: If *bitmap* is specified, then it names a bitmap in the standard forms accepted by Tk (see the [Tk\_GetBitmap][GetBitmap] manual entry for details). This bitmap is passed to the window manager to be displayed in *window*'s icon, and the command returns an empty string.  If an empty string is specified for *bitmap*, then any current icon bitmap is cancelled for *window*. If *bitmap* is specified then the command returns an empty string. Otherwise it returns the name of the current icon bitmap associated with *window*, or an empty string if *window* has no icon bitmap.  On the Windows operating system, an additional flag is supported:

    [wm]{.cmd} [iconbitmap]{.sub} [window]{.arg} [-default]{.optlit} [image]{.optarg}
    : If the **-default** flag is given, the icon is applied to all toplevel windows (existing and future) to which no other specific icon has yet been applied. In addition to bitmap image types, a full path specification to any file which contains a valid Windows icon is also accepted (usually .ico or .icr files), or any file for which the shell has assigned an icon.  Tcl will first test if the file contains an icon, then if it has an assigned icon, and finally, if that fails, test for a bitmap.


[wm]{.cmd} [iconify]{.sub} [window]{.arg}
: Arrange for *window* to be iconified.  It *window* has not yet been mapped for the first time, this command will arrange for it to appear in the iconified state when it is eventually mapped.

[wm]{.cmd} [iconmask]{.sub} [window]{.arg} [bitmap]{.optarg}
: If *bitmap* is specified, then it names a bitmap in the standard forms accepted by Tk (see the [Tk\_GetBitmap][GetBitmap] manual entry for details). This bitmap is passed to the window manager to be used as a mask in conjunction with the **iconbitmap** option:  where the mask has zeroes no icon will be displayed;  where it has ones, the bits from the icon bitmap will be displayed.  If an empty string is specified for *bitmap* then any current icon mask is cancelled for *window* (this is equivalent to specifying a bitmap of all ones).  If *bitmap* is specified then the command returns an empty string.  Otherwise it returns the name of the current icon mask associated with *window*, or an empty string if no mask is in effect.

[wm]{.cmd} [iconname]{.sub} [window]{.arg} [newName]{.optarg}
: If *newName* is specified, then it is passed to the window manager;  the window manager should display *newName* inside the icon associated with *window*.  In this case an empty string is returned as result.  If *newName* is not specified then the command returns the current icon name for *window*, or an empty string if no icon name has been specified (in this case the window manager will normally display the window's title, as specified with the **wm title** command).

[wm]{.cmd} [iconphoto]{.sub} [window]{.arg} [-default]{.optlit} [image1]{.arg} [image2]{.optdot}
: Sets the titlebar icon for *window* based on the named photo images. If **-default** is specified, this is applied to all future created toplevels as well.  The data in the images is taken as a snapshot at the time of invocation.  If the images are later changed, this is not reflected to the titlebar icons.  Multiple images are accepted to allow different images sizes (e.g., 16x16 and 32x32) to be provided. The window manager may scale provided icons to an appropriate size.

    On Windows, the images are packed into a Windows icon structure. This will override an ico specified to **wm iconbitmap**, and vice versa. This command sets the taskbar icon for the window.

    On X, the images are arranged into the \_NET\_WM\_ICON X property, which most modern window managers support.  A **wm iconbitmap** may exist simultaneously.  It is recommended to use not more than 2 icons, placing the larger icon first. This command also sets the panel icon for the application if the window manager or desktop environment supports it.

    On Macintosh, the first image called is loaded into an OS-native icon format, and becomes the application icon in dialogs, the Dock, and other contexts. At the script level the command will accept only the first image passed in the parameters as support for multiple sizes/resolutions on macOS is outside Tk's scope. Developers should use the largest icon they can support (preferably 512 pixels) to ensure smooth rendering on the Mac.

[wm]{.cmd} [iconposition]{.sub} [window]{.arg} [x y]{.optarg}
: If *x* and *y* are specified, they are passed to the window manager as a hint about where to position the icon for *window*. In this case an empty string is returned.  If *x* and *y* are specified as empty strings then any existing icon position hint is cancelled. If neither *x* nor *y* is specified, then the command returns a Tcl list containing two values, which are the current icon position hints (if no hints are in effect then an empty string is returned).

[wm]{.cmd} [iconwindow]{.sub} [window]{.arg} [pathName]{.optarg}
: If *pathName* is specified, it is the path name for a window to use as icon for *window*: when *window* is iconified then *pathName* will be mapped to serve as icon, and when *window* is de-iconified then *pathName* will be unmapped again.  If *pathName* is specified as an empty string then any existing icon window association for *window* will be cancelled.  If the *pathName* argument is specified then an empty string is returned.  Otherwise the command returns the path name of the current icon window for *window*, or an empty string if there is no icon window currently specified for *window*. Button press events are disabled for *window* as long as it is an icon window;  this is needed in order to allow window managers to "own" those events. Note that not all window managers support the notion of an icon window, and the concept is entirely meaningless on non-X11 platforms.

[wm]{.cmd} [manage]{.sub} [widget]{.arg}
: The *widget* specified will become a stand alone top-level window.  The window will be decorated with the window managers title bar, etc. Only *frame*, *labelframe* and *toplevel* widgets can be used with this command. Attempting to pass any other widget type will raise an error. Attempting to manage a *toplevel* widget is benign and achieves nothing. See also [Geometry management].

[wm]{.cmd} [maxsize]{.sub} [window]{.arg} [width height]{.optarg}
: If *width* and *height* are specified, they give the maximum permissible dimensions for *window*. For gridded windows the dimensions are specified in grid units;  otherwise they are specified in pixel units. The window manager will restrict the window's dimensions to be less than or equal to *width* and *height*. If *width* and *height* are specified, then the command returns an empty string.  Otherwise it returns a Tcl list with two elements, which are the maximum width and height currently in effect. The maximum size defaults to the size of the screen. See the sections on geometry management below for more information.

[wm]{.cmd} [minsize]{.sub} [window]{.arg} [width height]{.optarg}
: If *width* and *height* are specified, they give the minimum permissible dimensions for *window*. For gridded windows the dimensions are specified in grid units;  otherwise they are specified in pixel units. The window manager will restrict the window's dimensions to be greater than or equal to *width* and *height*. If *width* and *height* are specified, then the command returns an empty string.  Otherwise it returns a Tcl list with two elements, which are the minimum width and height currently in effect. The minimum size defaults to one pixel in each dimension. See the sections on geometry management below for more information.

[wm]{.cmd} [overrideredirect]{.sub} [window]{.arg} [boolean]{.optarg}
: If *boolean* is specified, it must have a proper boolean form and the override-redirect flag for *window* is set to that value. If *boolean* is not specified then **1** or **0** is returned to indicate whether or not the override-redirect flag is currently set for *window*. Setting the override-redirect flag for a window causes it to be ignored by the window manager;  among other things, this means that the window will not be reparented from the root window into a decorative frame and the user will not be able to manipulate the window using the normal window manager mechanisms.

    The window manager will not give the keyboard focus to such a window either; on X11 an application that needs key events in it has to claim the focus with [focus -force][focus].

    Note that the override-redirect flag is only guaranteed to be taken notice of when the window is first mapped or when mapped after the state is changed from withdrawn to normal. Some, but not all, platforms will take notice at additional times.

[wm]{.cmd} [positionfrom]{.sub} [window]{.arg} [who]{.optarg}
: If *who* is specified, it must be either **program** or **user**, or an abbreviation of one of these two.  It indicates whether *window*'s current position was requested by the program or by the user.  Many window managers ignore program-requested initial positions and ask the user to manually position the window;  if **user** is specified then the window manager should position the window at the given place without asking the user for assistance. If *who* is specified as an empty string, then the current position source is cancelled. If *who* is specified, then the command returns an empty string. Otherwise it returns **user** or **program** to indicate the source of the window's current position, or an empty string if no source has been specified yet.  Most window managers interpret "no source" as equivalent to **program**. Tk will automatically set the position source to **user** when a **wm geometry** command is invoked, unless the source has been set explicitly to **program**.

[wm]{.cmd} [protocol]{.sub} [window]{.arg} [name]{.optarg} [command]{.optarg}
: This command is used to manage window manager protocols. The *name* argument in the **wm protocol** command is the name of an atom corresponding to a window manager protocol.  Examples include **WM\_DELETE\_WINDOW** or **WM\_SAVE\_YOURSELF** or **WM\_TAKE\_FOCUS**.

    A *window manager protocol* is a class of messages sent from a window manager to a Tk application outside of the normal event processing system. The main example is the **WM\_DELETE\_WINDOW** protocol; these messages are sent when the user clicks the close widget in the title bar of a window.  Handlers for window manager protocols are installed with the **wm protocol** command. As a rule, if no handler has been installed for a protocol by the **wm protocol** command then all messages of that protocol are ignored. The **WM\_DELETE\_WINDOW** protocol is an exception to this rule. At start-up Tk installs a handler for this protocol, which responds by destroying the window. The **wm protocol** command can be used to replace this default handler by one which responds differently.

    The list of available window manager protocols depends on the window manager, but all window managers supported by Tk provide **WM\_DELETE\_WINDOW**. On the Windows platform, a **WM\_SAVE\_YOURSELF** message is sent on user logout or system restart.

    If both *name* and *command* are specified, then *command* becomes the handler for the protocol specified by *name*. The atom for *name* will be added to *window*'s **WM\_PROTOCOLS** property to tell the window manager that the application has a handler for the protocol specified by *name*, and *command* will be invoked in the future whenever the window manager sends a message of that protocol to the Tk application.  In this case the **wm protocol** command returns an empty string.  If *name* is specified but *command* is not, then the current handler for *name* is returned, or an empty string if there is no handler defined for *name* (as a special case, the default handler for **WM\_DELETE\_WINDOW** is not returned).  If *command* is specified as an empty string then the atom for *name* is removed from the **WM\_PROTOCOLS** property of *window* and the handler is destroyed; an empty string is returned. Lastly, if neither *name* nor *command* is specified, the **wm protocol** command returns a list of all of the protocols for which handlers are currently defined for *window*.

[wm]{.cmd} [resizable]{.sub} [window]{.arg} [width height]{.optarg}
: This command controls whether or not the user may interactively resize a top-level window.  If *width* and *height* are specified, they are boolean values that determine whether the width and height of *window* may be modified by the user. In this case the command returns an empty string. If *width* and *height* are omitted then the command returns a list with two 0/1 elements that indicate whether the width and height of *window* are currently resizable. By default, windows are resizable in both dimensions. If resizing is disabled, then the window's size will be the size from the most recent interactive resize or **wm geometry** command.  If there has been no such operation then the window's natural size will be used.

[wm]{.cmd} [sizefrom]{.sub} [window]{.arg} [who]{.optarg}
: If *who* is specified, it must be either **program** or **user**, or an abbreviation of one of these two.  It indicates whether *window*'s current size was requested by the program or by the user.  Some window managers ignore program-requested sizes and ask the user to manually size the window;  if **user** is specified then the window manager should give the window its specified size without asking the user for assistance. If *who* is specified as an empty string, then the current size source is cancelled. If *who* is specified, then the command returns an empty string. Otherwise it returns **user** or **window** to indicate the source of the window's current size, or an empty string if no source has been specified yet.  Most window managers interpret "no source" as equivalent to **program**.

[wm]{.cmd} [stackorder]{.sub} [window]{.arg} [[isabove=|§isbelow]{.lit} [window]{.arg}]{.optarg}
: The **stackorder** command returns a list of toplevel windows in stacking order, from lowest to highest. When a single toplevel window is passed, the returned list recursively includes all of the window's children that are toplevels. Only those toplevels that are currently mapped to the screen are returned. The **stackorder** command can also be used to determine if one toplevel is positioned above or below a second toplevel. When two window arguments separated by either **isabove** or **isbelow** are passed, a boolean result indicates whether or not the first window is currently above or below the second window in the stacking order.

[wm]{.cmd} [state]{.sub} [window]{.arg} [newstate]{.optarg}
: If *newstate* is specified, the window will be set to the new state, otherwise it returns the current state of *window*: either **normal**, **iconic**, **withdrawn**, **icon**, or (Windows and macOS only) **zoomed**. The difference between **iconic** and **icon** is that **iconic** refers to a window that has been iconified (e.g., with the **wm iconify** command) while **icon** refers to a window whose only purpose is to serve as the icon for some other window (via the **wm iconwindow** command).  The **icon** state cannot be set.

[wm]{.cmd} [title]{.sub} [window]{.arg} [string]{.optarg}
: If *string* is specified, then it will be passed to the window manager for use as the title for *window* (the window manager should display this string in *window*'s title bar).  In this case the command returns an empty string.  If *string* is not specified then the command returns the current title for the *window*.  The title for a window defaults to its name.

[wm]{.cmd} [transient]{.sub} [window]{.arg} [container]{.optarg}
: If *container* is specified, then the window manager is informed that *window* is a transient window (e.g. pull-down menu) working on behalf of *container* (where *container* is the path name for a top-level window).  If *container* is specified as an empty string then *window* is marked as not being a transient window any more. Otherwise the command returns the path name of *window*'s current container, or an empty string if *window* is not currently a transient window.  A transient window will mirror state changes in the container and inherit the state of the container when initially mapped. The directed graph with an edge from each transient to its container must be acyclic.  In particular, it is an error to attempt to make a window a transient of itself.  The window manager may also decorate a transient window differently, removing some features normally present (e.g., minimize and maximize buttons) though this is entirely at the discretion of the window manager.

[wm]{.cmd} [withdraw]{.sub} [window]{.arg}
: Arranges for *window* to be withdrawn from the screen.  This causes the window to be unmapped and forgotten about by the window manager.  If the window has never been mapped, then this command causes the window to be mapped in the withdrawn state.  Not all window managers appear to know how to handle windows that are mapped in the withdrawn state. Note that it sometimes seems to be necessary to withdraw a window and then re-map it (e.g. with **wm deiconify**) to get some window managers to pay attention to changes in window attributes such as group.


# Geometry management

By default a top-level window appears on the screen in its *natural size*, which is the one determined internally by its widgets and geometry managers. If the natural size of a top-level window changes, then the window's size changes to match. A top-level window can be given a size other than its natural size in two ways. First, the user can resize the window manually using the facilities of the window manager, such as resize handles. Second, the application can request a particular size for a top-level window using the **wm geometry** command. These two cases are handled identically by Tk;  in either case, the requested size overrides the natural size. You can return the window to its natural by invoking **wm geometry** with an empty *geometry* string.

Normally a top-level window can have any size from one pixel in each dimension up to the size of its screen. However, you can use the **wm minsize** and **wm maxsize** commands to limit the range of allowable sizes. The range set by **wm minsize** and **wm maxsize** applies to all forms of resizing, including the window's natural size as well as manual resizes and the **wm geometry** command.  You can use any value accepted by [Tk\_GetPixels][GetPixels]. You can also use the command **wm resizable** to completely disable interactive resizing in one or both dimensions.

The **wm manage** and **wm forget** commands may be used to perform undocking and docking of windows.  After a widget is managed by **wm manage** command, all other **wm** subcommands may be used with the widget.  Only widgets created using the toplevel command may have an attached menu via the **-menu** configure option.  A toplevel widget may be used as a frame and managed with any of the other geometry managers after using the **wm forget** command.  Any menu associated with a toplevel widget will be hidden when managed by another geometry managers.  The menus will reappear once the window is managed by **wm**.  All custom bindtags for widgets in a subtree that have their top-level widget changed via a **wm manage** or **wm forget** command, must be redone to adjust any top-level widget path in the bindtags. Bindtags that have not been customized do not have to be redone.

# Gridded geometry management

Gridded geometry management occurs when one of the widgets of an application supports a range of useful sizes. This occurs, for example, in a text editor where the scrollbars, menus, and other adornments are fixed in size but the edit widget can support any number of lines of text or characters per line. In this case, it is usually desirable to let the user specify the number of lines or characters-per-line, either with the **wm geometry** command or by interactively resizing the window. In the case of text, and in other interesting cases also, only discrete sizes of the window make sense, such as integral numbers of lines and characters-per-line;  arbitrary pixel sizes are not useful.

Gridded geometry management provides support for this kind of application. Tk (and the window manager) assume that there is a grid of some sort within the application and that the application should be resized in terms of *grid units* rather than pixels. Gridded geometry management is typically invoked by turning on the **setGrid** option for a widget;  it can also be invoked with the **wm grid** command or by calling [Tk\_SetGrid][SetGrid]. In each of these approaches the particular widget (or sometimes code in the application as a whole) specifies the relationship between integral grid sizes for the window and pixel sizes. To return to non-gridded geometry management, invoke **wm grid** with empty argument strings.

When gridded geometry management is enabled then all the dimensions specified in **wm minsize**, **wm maxsize**, and **wm geometry** commands are treated as grid units rather than pixel units. Interactive resizing is also carried out in even numbers of grid units rather than pixels.

# Bugs

Most existing window managers appear to have bugs that affect the operation of the **wm** command.  For example, some changes will not take effect if the window is already active:  the window will have to be withdrawn and de-iconified in order to make the change happen.

# Examples

A fixed-size window that says that it is fixed-size too:

```
toplevel .fixed
wm title     .fixed "Fixed-size Window"
wm resizable .fixed 0 0
```

A simple dialog-like window, centred on the screen:

```
# Create and arrange the dialog contents.
toplevel .msg
label  .msg.l  -text "This is a very simple dialog demo."
button .msg.ok -text OK -default active -command {destroy .msg}
pack .msg.ok -side bottom -fill x
pack .msg.l  -expand 1    -fill both

# Now set the widget up as a centred dialog.

# But first, we need the geometry managers to finish setting
# up the interior of the dialog, for which we need to run the
# event loop with the widget hidden completely...
wm withdraw .msg
update
set x [expr {([winfo screenwidth .] - [winfo width .msg]) / 2}]
set y [expr {([winfo screenheight .] - [winfo height .msg]) / 2}]
wm geometry  .msg +$x+$y
wm transient .msg .
wm title     .msg "Dialog demo"
wm deiconify .msg
```


[focus]: focus.md
[frame]: frame.md
[GeomReq]: ../TkCAPI/GeomReq.md
[GetBitmap]: ../TkCAPI/GetBitmap.md
[GetColor]: ../TkCAPI/GetColor.md
[GetPixels]: ../TkCAPI/GetPixels.md
[menu]: menu.md
[option]: option.md
[SetGrid]: ../TkCAPI/SetGrid.md
[toplevel]: toplevel.md
[winfo]: winfo.md

