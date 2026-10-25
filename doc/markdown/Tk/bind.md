---
CommandName: bind
title: bind
ManualSection: n
Version: 8.0
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - bgerror(n)
 - bindtags(n)
 - event(n)
 - focus(n)
 - grab(n)
 - keysyms(n)
Keywords:
 - binding
 - event
Copyright:
 - Copyright (c) 1990 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
 - Copyright (c) 1998 Scriptics Corporation.
---

# Name

bind - Arrange for X events to invoke Tcl scripts

# Synopsis

::: {.synopsis} :::
[bind]{.cmd} [tag]{.arg} [sequence]{.optarg} [+=??+script]{.optlit}
:::

# Introduction

The **bind** command associates Tcl scripts with X events. If all three arguments are specified, **bind** will arrange for *script* (a Tcl script called the "binding script" to be evaluated whenever the event(s) given by *sequence* occur in the window(s) identified by *tag*. If *script* is prefixed with a "+", then it is appended to any existing binding for *sequence*;  otherwise *script* replaces any existing binding. If *script* is an empty string then the current binding for *sequence* is destroyed, leaving *sequence* unbound. In all of the cases where a *script* argument is provided, **bind** returns an empty string.

If *sequence* is specified without a *script*, then the script currently bound to *sequence* is returned, or an empty string is returned if there is no binding for *sequence*. If neither *sequence* nor *script* is specified, then the return value is a list whose elements are all the sequences for which there exist bindings for *tag*.

The *tag* argument determines which window(s) the binding applies to. If *tag* begins with a dot, as in **.a.b.c**, then it must be the path name for a window; otherwise it may be an arbitrary string. Each window has an associated list of tags, and a binding applies to a particular window if its tag is among those specified for the window. Although the [bindtags] command may be used to assign an arbitrary set of binding tags to a window, the default binding tags provide the following behavior:

- If a tag is the name of an internal window the binding applies to that window.

- If the tag is the name of a class of widgets, such as **Button**, the binding applies to all widgets in that class.

- If the tag is the name of a toplevel window the binding applies to the toplevel window and all its internal windows.

- If *tag* has the value **all**, the binding applies to all windows in the application.


Tk creates some bindings for the **all** tag itself, for example for keyboard traversal of menus.  On Windows, the bindings for **<Alt-Key>**, **<Alt-KeyRelease>**, **<F10>**, **<KeyRelease-F10>** and the Alt keys pass the key to Windows, which handles the native menubar and system keys such as Alt-F4.  Replacing these bindings disables that; extend them by prefixing the script with **+** instead.

# Event patterns

The *sequence* argument specifies a sequence of one or more event patterns, with optional white space between the patterns.  Each event pattern may take one of three forms.  In the simplest case it is a single printing ASCII character, such as **a** or **\[**.  The character may not be a space character or the character **<**.  This form of pattern matches a **Key** event for the particular character.  The second form of pattern is longer but more general. It has the following syntax:

```
<modifier-modifier-type-detail>
```

The entire event pattern is surrounded by angle brackets. Inside the angle brackets are zero or more modifiers, an event type, and an extra piece of information (*detail*) identifying a particular button or keysym.  Any of the fields may be omitted, as long as at least one of *type* and *detail* is present. The fields must be separated by white space or dashes.

The third form of pattern is used to specify a user-defined, named virtual event.  It has the following syntax:

```
<<name>>
```

The entire virtual event pattern is surrounded by double angle brackets. Inside the angle brackets is the user-defined name of the virtual event. Modifiers, such as **Shift** or **Control**, may not be combined with a virtual event to modify it.  Bindings on a virtual event may be created before the virtual event is defined, and if the definition of a virtual event changes dynamically, all windows bound to that virtual event will respond immediately to the new definition.

Some widgets (e.g. [menu] and [text]) issue virtual events when their internal state is updated in some ways.  Please see the manual page for each widget for details.

## Modifiers

Modifiers consist of any of the following values:

::: {.info DISPLAY="yes"}
**Control**	**Mod1**, **M1**, **Command** **Alt**	**Mod2**, **M2**, **Option** **Shift**	**Mod3**, **M3**, **Num** **Lock**	**Mod4**, **M4**, **Fn** **Extended**	**Mod5**, **M5** **Button1**, **B1**	**Meta**, **M** **Button2**, **B2**	**Double** **Button3**, **B3**	**Triple** **Button4**, **B4**	**Quadruple** **Button5**, **B5**
:::

Where more than one value is listed, separated by commas, the values are equivalent. Most of the modifiers have the obvious X meanings. For example, **Button1** requires that button 1 be depressed when the event occurs. For a binding to match a given event, the modifiers in the event must include all of those specified in the event pattern. An event may also contain additional modifiers not specified in the binding. For example, if button 1 is pressed while the shift and control keys are down, the pattern **<Control-Button-1>** will match the event, but **<Mod1-Button-1>** will not. If no modifiers are specified, then any combination of modifiers may be present in the event.

**Meta** and **M** refer to whichever of the **M1** through **M5** modifiers is associated with the Meta key(s) on the keyboard (keysyms **Meta\_R** and **Meta\_L**). If there are no Meta keys, or if they are not associated with any modifiers, then **Meta** and **M** will not match any events. Similarly, the **Alt** modifier refers to whichever modifier is associated with the alt key(s) on the keyboard (keysyms **Alt\_L** and **Alt\_R**).

The **Double**, **Triple** and **Quadruple** modifiers are a convenience for specifying double mouse clicks and other repeated events. They cause a particular event pattern to be repeated 2, 3 or 4 times, and also place a time and space requirement on the sequence: for a sequence of events to match a **Double**, **Triple** or **Quadruple** pattern, all of the events must occur close together in time and without substantial mouse motion in between.  For example, **<Double-Button-1>** is equivalent to **<Button-1><Button-1>** with the extra time and space requirement.

The **Command**, **Option**, **Num** and **Fn**modifiers are equivalents of **Mod1** up to **Mod4**; they correspond to Macintosh-specific modifier keys.

The **Extended** modifier is, at present, specific to Windows.  It appears on events that are associated with the keys on the "extended keyboard". On a US keyboard, the extended keys include the **Alt** and **Control** keys at the right of the keyboard, the cursor keys in the cluster to the left of the numeric pad, the **NumLock** key, the **Break** key, the **PrintScreen** key, and the **/** and **Enter** keys in the numeric keypad.

## Event types

The *type* field may be any of the standard X event types, with a few extra abbreviations.  The *type* field will also accept a couple non-standard X event types that were added to better support the Macintosh and Windows platforms.  Below is a list of all the valid types; where two names appear together, they are synonyms.

::: {.info DISPLAY="yes"}
**Activate**	**Destroy**	**Map** **Button**, **ButtonPress**	**Enter**	**MapRequest** **ButtonRelease**	**Expose**	**Motion** **Circulate**	**FocusIn**	**MouseWheel** **TouchpadScroll**	**CirculateRequest**	**FocusOut** **Property**	**Colormap**	**Gravity** **Reparent**	**Configure**	**Key**, **KeyPress** **ResizeRequest**	**ConfigureRequest**	**KeyRelease** **Unmap**   **Create**	**Leave**	**Visibility** **Deactivate**
:::

Most of the above events have the same fields and behaviors as events in the X Windowing system.  You can find more detailed descriptions of these events in any X window programming book.  A couple of the events are extensions to the X event system to support features unique to the Macintosh and Windows platforms.  We provide a little more detail on these events here.  These include:

**Activate**, **Deactivate**
: These two events are sent to every sub-window of a toplevel when they change state.  In addition to the focus Window, the Macintosh platform and Windows platforms have a notion of an active window (which often has but is not required to have the focus).  On the Macintosh, widgets in the active window have a different appearance than widgets in deactive windows.  The **Activate** event is sent to all the sub-windows in a toplevel when it changes from being deactive to active.  Likewise, the **Deactive** event is sent when the window's state changes from active to deactive.  There are no useful percent substitutions you would make when binding to these events.

**MouseWheel**
: Many contemporary mice include a mouse wheel, which is used for scrolling documents without using the scrollbars.  By rolling the wheel, the system will generate **MouseWheel** events that the application can use to scroll.  The event is routed to the window currently under the mouse pointer. When the event is received you can use the **%D** substitution to get the *delta* field for the event, which is an integer value describing how the mouse wheel has moved.  The smallest value for which the system will report is defined by the OS.  The sign of the value determines which direction your widget should scroll.  Positive values should scroll up and negative values should scroll down.

    Horizontal scrolling uses **Shift-MouseWheel** events, with positive **%D** *delta* substitution indicating left scrolling and negative right scrolling. Horizontal scrolling events are generated tilt wheels on some mice.  Horizontal scrolling can also be emulated by holding Shift and scrolling vertically.

**TouchpadScroll**
: On some platforms (currently Windows and macOS) there is support for high-resolution scrolling devices, such as touchpads.  This is provided via **TouchpadScroll** events.  These events store two 16 bit delta values in the integer provided by the **%D** substitution.  The *X* delta is in the high order 16 bits and the *Y* delta is in the low order 16 bits.  These values can be unpacked by using the tk::PreciseScrollDeltas utility procedure.  For example:

    ```
    lassign [tk::PreciseScrollDeltas %D] deltaX deltaY
    ```

    The **$#** substitution is a counter for **TouchpadScroll** events which can be used by widgets that only support scrolling by units to ignore some portion of the events.

**KeyPress**, **KeyRelease**
: The **KeyPress** and **KeyRelease** events are generated whenever a key is pressed or released.  **KeyPress** and **KeyRelease**

**Key**, **KeyRelease**
: The **Key** and **KeyRelease** events are generated whenever a key is pressed or released.  **Key** and **KeyRelease** events are sent to the window which currently has the keyboard focus.

**Button**, **ButtonRelease**, **Motion**
: The **Button** and **ButtonRelease** events are generated when the user presses or releases a mouse button. **Motion** events are generated whenever the pointer is moved. **Button**, **ButtonRelease**, and **Motion** events are normally sent to the window containing the pointer.

    When a mouse button is pressed, the window containing the pointer automatically obtains a temporary pointer grab. Subsequent **Button**, **ButtonRelease**, and **Motion** events will be sent to that window, regardless of which window contains the pointer, until all buttons have been released.

**Configure**
: A **Configure** event is sent to a window whenever its size, position, or border width changes, and sometimes when it has changed position in the stacking order.

**Map**, **Unmap**
: The **Map** and **Unmap** events are generated whenever the mapping state of a window changes.

    Windows are created in the unmapped state. Top-level windows become mapped when they transition to the **normal** state, and are unmapped in the **withdrawn** and **iconic** states. Other windows become mapped when they are placed under control of a geometry manager (for example [pack] or [grid]).

    A window is *viewable* only if it and all of its ancestors are mapped. Note that geometry managers typically do not map their children until they have been mapped themselves, and unmap all children when they become unmapped; hence in Tk **Map** and **Unmap** events indicate whether or not a window is viewable.

**Visibility**
: A window is said to be *obscured* when another window above it in the stacking order fully or partially overlaps it. **Visibility** events are generated whenever a window's obscurity state changes; the *state* field (**%s**) specifies the new state.

**Expose**
: An **Expose** event is generated whenever all or part of a window should be redrawn (for example, when a window is first mapped or if it becomes unobscured). It is normally not necessary for client applications to handle **Expose** events, since Tk handles them internally.

**Destroy**
: A **Destroy** event is delivered to a window when it is destroyed.

    When the **Destroy** event is delivered to a widget, it is in a "half-dead" state: the widget still exists, but operations that involve it may return invalid results, or return an error.

**FocusIn**, **FocusOut**
: The **FocusIn** and **FocusOut** events are generated whenever the keyboard focus changes. A **FocusOut** event is sent to the old focus window, and a **FocusIn** event is sent to the new one.

    In addition, if the old and new focus windows do not share a common parent, "virtual crossing" focus events are sent to the intermediate windows in the hierarchy. Thus a **FocusIn** event indicates that the target window or one of its descendants has acquired the focus, and a **FocusOut** event indicates that the focus has been changed to a window outside the target window's hierarchy.

    The keyboard focus may be changed explicitly by a call to [focus], or implicitly by the window manager.

**Enter**, **Leave**
: An **Enter** event is sent to a window when the pointer enters that window, and a **Leave** event is sent when the pointer leaves it.

    If there is a pointer grab in effect, **Enter** and **Leave** events are only delivered to the window owning the grab.

    In addition, when the pointer moves between two windows, **Enter** and **Leave** "virtual crossing" events are sent to intermediate windows in the hierarchy in the same manner as for **FocusIn** and **FocusOut** events.

**Property**
: A **Property** event is sent to a window whenever an X property belonging to that window is changed or deleted. **Property** events are not normally delivered to Tk applications as they are handled by the Tk core.

**Colormap**
: A **Colormap** event is generated whenever the colormap associated with a window has been changed, installed, or uninstalled.

    Widgets may be assigned a private colormap by specifying a **-colormap** option; the window manager is responsible for installing and uninstalling colormaps as necessary.

    Note that Tk provides no useful details for this event type.

**MapRequest**, **CirculateRequest**, **ResizeRequest**, **ConfigureRequest**, **Create**
: These events are not normally delivered to Tk applications. They are included for completeness, to make it possible to write X11 window managers in Tk. (These events are only delivered when a client has selected **SubstructureRedirectMask** on a window; the Tk core does not use this mask.)

**Gravity**, **Reparent**, **Circulate**
: The events **Gravity** and **Reparent** are not normally delivered to Tk applications. They are included for completeness.

    A **Circulate** event indicates that the window has moved to the top or to the bottom of the stacking order as a result of an **XCirculateSubwindows** protocol request. Note that the stacking order may be changed for other reasons which do not generate a **Circulate** event, and that Tk does not use **XCirculateSubwindows()** internally. This event type is included only for completeness; there is no reliable way to track changes to a window's position in the stacking order.


## Event details

The last part of a long event specification is *detail*.  In the case of a **Button** or **ButtonRelease** event, it is the number of a button (1-9).  If a button number is given, then only an event on that particular button will match;  if no button number is given, then an event on any button will match.  Note:  giving a specific button number is different than specifying a button modifier; in the first case, it refers to a button being pressed or released, while in the second it refers to some other button that is already depressed when the matching event occurs.  If a button number is given then *type* may be omitted:  if will default to **Button**.  For example, the specifier **<1>** is equivalent to **<Button-1>**.

If the event type is **Key** or **KeyRelease**, then *detail* may be specified in the form of an X keysym.  Keysyms are textual specifications for particular keys on the keyboard; they include all the alphanumeric ASCII characters (e.g. "a" is the keysym for the ASCII character "a"), plus descriptions for non-alphanumeric characters ("comma" is the keysym for the comma character), plus descriptions for all the non-ASCII keys on the keyboard (e.g. "Shift\_L" is the keysym for the left shift key, and "F1" is the keysym for the F1 function key, if it exists).  The complete list of keysyms is not presented here;  it is available in other X documentation and may vary from system to system. If necessary, you can use the **%K** notation described below to print out the keysym name for a particular key. If a keysym *detail* is given, then the *type* field may be omitted;  it will default to **Key**. For example, **<Control-comma>** is equivalent to **<Control-Key-comma>**.

On Windows, if the **Control** or **Alt** modifier is down and the character that the current keyboard layout assigns to a letter or digit key is not a Latin letter or a digit (for example, with a Cyrillic or Greek layout), the keysym of the event is the Latin letter or digit that Windows associates with the key, which is usually printed on the key beside the non-Latin character.  Thus bindings such as **<Control-c>** or **<Alt-x>** work regardless of the keyboard layout, as keyboard shortcuts do in other Windows applications.  This does not affect the **%A** substitution, which still gives the character of the current layout.

On X11, the keysym of the event is the one that the current keyboard layout (XKB group) assigns to the key.  If the **Control** or **Alt** modifier is down and that keysym is not a Latin-1 character (for example, with a Cyrillic or Greek layout), the keysym of the same key in a Latin layout is used instead, if the keyboard has one, so that bindings such as **<Control-c>** or **<Alt-x>** work regardless of the keyboard layout, as keyboard shortcuts do in other X11 toolkits. This does not affect the **%A** substitution.

# Binding scripts and substitutions

The *script* argument to **bind** is a Tcl script, called the "binding script" which will be executed whenever the given event sequence occurs. *Command* will be executed in the same interpreter that the **bind** command was executed in, and it will run at global level (only global variables will be accessible). If *script* contains any **%** characters, then the script will not be executed directly.  Instead, a new script will be generated by replacing each **%**, and the character following it, with information from the current event.  The replacement depends on the character following the **%**, as defined in the list below.  Unless otherwise indicated, the replacement string is the decimal value of the given field from the current event. Some of the substitutions are only valid for certain types of events;  if they are used for other types of events the string "**??**" is substituted.

**%%**
: Replaced with a single percent.

**%#**
: The number of the last client request processed by the server (the *serial* field from the event).  Valid for all event types.

**%a**
: The *above* field from the event, formatted as a hexadecimal number. Valid only for **Configure** events. Indicates the sibling window immediately below the receiving window in the stacking order, or **0** if the receiving window is at the bottom.

**%b**
: The number of the button that was pressed or released.  Valid only for **Button** and **ButtonRelease** events.

**%c**
: The *count* field from the event.  Valid only for **Expose** events. Indicates that there are *count* pending **Expose** events which have not yet been delivered to the window.

**%d**
: The *detail* or *user\_data* field from the event.  The **%d** is replaced by a string identifying the detail.  For **Enter**, **Leave**, **FocusIn**, and **FocusOut** events, the string will be one of the following:

    **NotifyAncestor**	**NotifyNonlinearVirtual** **NotifyDetailNone**	**NotifyPointer** **NotifyInferior**	**NotifyPointerRoot** **NotifyNonlinear**	**NotifyVirtual**
:::

    For **ConfigureRequest** events, the string will be one of:

    **Above**	**Opposite** **Below**	**None** **BottomIf**	**TopIf**
:::

    For virtual events, the string will be whatever value is stored in the *user\_data* field when the event was created (typically with [event generate][event]), or the empty string if the field is NULL. Virtual events corresponding to key sequence presses (see [event add][event] for details) set the *user\_data* to NULL. For events other than these, the string "**??**" is substituted.

**%f**
: The *focus* field from the event (**0** or **1**).  Valid only for **Enter** and **Leave** events.  **1** if the receiving window is the focus window or a descendant of the focus window, **0** otherwise.

**%h**
: The *height* field from the event.  Valid for the **Configure**, **ConfigureRequest**, **Create**, **ResizeRequest**, and **Expose** events. Indicates the new or requested height of the window.

**%i**
: The *window* field from the event, represented as a hexadecimal integer.  Valid for all event types.

**%k**
: The *keycode* field from the event.  Valid only for **Key** and **KeyRelease** events.

**%m**
: The *mode* field from the event.  The substituted string is one of **NotifyNormal**, **NotifyGrab**, **NotifyUngrab**, or **NotifyWhileGrabbed**.  Valid only for **Enter**, **FocusIn**, **FocusOut**, and **Leave** events.

**%o**
: The *override\_redirect* field from the event.  Valid only for **Map**, **Reparent**, and **Configure** events.

**%p**
: The *place* field from the event, substituted as one of the strings **PlaceOnTop** or **PlaceOnBottom**.  Valid only for **Circulate** and **CirculateRequest** events.

**%s**
: The *state* field from the event.  For **Button**, **ButtonRelease**, **Enter**, **Key**, **KeyRelease**, **Leave**, and **Motion** events, a decimal string is substituted.  For **Visibility**, one of the strings **VisibilityUnobscured**, **VisibilityPartiallyObscured**, and **VisibilityFullyObscured** is substituted. For **Property** events, substituted with either the string **NewValue** (indicating that the property has been created or modified) or **Delete** (indicating that the property has been removed).

**%t**
: The *time* field from the event. This is the X server timestamp (typically the time since the last server reset) in milliseconds, when the event occurred. Valid for most events.

**%w**
: The *width* field from the event. Indicates the new or requested width of the window. Valid only for **Configure**, **ConfigureRequest**, **Create**, **ResizeRequest**, and **Expose** events.

**%x**, **%y**
: The *x* and *y* fields from the event. For **Button**, **ButtonRelease**, **Motion**, **Key**, **KeyRelease**, and **MouseWheel** events, **%x** and **%y** indicate the position of the mouse pointer relative to the receiving window. For key events on the Macintosh these are the coordinates of the mouse at the moment when an X11 KeyEvent is sent to Tk, which could be slightly later than the time of the physical press or release. For **Enter** and **Leave** events, the position where the mouse pointer crossed the window, relative to the receiving window. For **Configure** and **Create** requests, the *x* and *y* coordinates of the window relative to its parent window.

**%A**
: Substitutes the UNICODE character corresponding to the event, or the empty string if the event does not correspond to a UNICODE character (e.g. the shift key was pressed). On X11, **XmbLookupString** (or **XLookupString** when input method support is turned off) does all the work of translating from the event to a UNICODE character. On X11, valid only for **Key** event. On Windows and macOS/aqua, valid only for **Key** and **KeyRelease** events.

**%B**
: The *border\_width* field from the event.  Valid only for **Configure**, **ConfigureRequest**, and **Create** events.

**%D**
: This reports the *delta* value of a **MouseWheel** event.  The *delta* value represents the rotation units the mouse wheel has been moved. The sign of the value represents the direction the mouse wheel was scrolled.

**%E**
: The *send\_event* field from the event.  Valid for all event types. **0** indicates that this is a "normal" event, **1** indicates that it is a "synthetic" event generated by **SendEvent**.

**%K**
: The keysym corresponding to the event, substituted as a textual string.  Valid only for **Key** and **KeyRelease** events.

**%M**
: The number of script-based binding patterns matched so far for the event.  Valid for all event types.

**%N**
: The keysym corresponding to the event, substituted as a decimal number.  Valid only for **Key** and **KeyRelease** events.

**%P**
: The name of the property being updated or deleted (which may be converted to an XAtom using [winfo atom][winfo].) Valid only for **Property** events.

**%R**
: The *root* window identifier from the event.  Valid only for events containing a *root* field.

**%S**
: The *subwindow* window identifier from the event, formatted as a hexadecimal number. Valid only for events containing a *subwindow* field.

**%T**
: The *type* field from the event.  Valid for all event types.

**%W**
: The path name of the window to which the event was reported (the *window* field from the event).  Valid for all event types.

**%X**, **%Y**
: The *x\_root* and  *y\_root* fields from the event. If a virtual-root window manager is being used then the substituted values are the corresponding x-coordinate and y-coordinate in the virtual root. Valid only for **Button**, **ButtonRelease**, **Enter**, **Key**, **KeyRelease**, **Leave** and **Motion** events. Same meaning as **%x** and **%y**, except relative to the (virtual) root window.


The replacement string for a %-replacement is formatted as a proper Tcl list element. This means that spaces or special characters such as **$** and **{** may be preceded by backslashes. This guarantees that the string will be passed through the Tcl parser when the binding script is evaluated. Most replacements are numbers or well-defined strings such as **Above**;  for these replacements no special formatting is ever necessary. The most common case where reformatting occurs is for the **%A** substitution.  For example, if *script* is

```
insert %A
```

and the character typed is an open square bracket, then the script actually executed will be

```
insert \[
```

This will cause the **insert** to receive the original replacement string (open square bracket) as its first argument. If the extra backslash had not been added, Tcl would not have been able to parse the script correctly.

# Multiple matches

It is possible for several bindings to match a given X event. If the bindings are associated with different *tag*'s, then each of the bindings will be executed, in order. By default, a binding for the widget will be executed first, followed by a class binding, a binding for its toplevel, and an **all** binding. The [bindtags] command may be used to change this order for a particular window or to associate additional binding tags with the window.

The [continue] and [break] commands may be used inside a binding script to control the processing of matching scripts. If [continue] is invoked within a binding script, then this binding script, including all other "+" appended scripts, is terminated but Tk will continue processing binding scripts associated with other *tag*'s. If the [break] command is invoked within a binding script, then that script terminates and no other scripts will be invoked for the event.

Within a script called from the binding script, [return] **-code ok** may be used to continue processing (including "+" appended scripts), or [return] **-code break** may be used to stop processing all other binding scripts.

If more than one binding matches a particular event and they have the same *tag*, then the most specific binding is chosen and its script is evaluated. The following tests are applied, in order, to determine which of several matching sequences is more specific:

(a)
: an event pattern that specifies a specific button or key is more specific than one that does not;

(b)
: a sequence with the most highest-ordered patterns (in term of highest repetition count) is more specific than a sequence with less highest-ordered patterns;

(c)
: if the modifiers specified in one pattern are a subset of the modifiers in another pattern, then the pattern with more modifiers is more specific;

(d)
: a virtual event whose physical pattern matches the sequence is less specific than the same physical pattern that is not associated with a virtual event;

(e)
: given a sequence that matches two or more virtual events, one of the virtual events will be chosen, but the order is undefined.


If the matching sequences contain more than one event, then tests (c)-(e) are applied in order from the most recent event to the least recent event in the sequences.  If these tests fail to determine a winner, then the most recently registered sequence is the winner.

If there are two (or more) virtual events that are both triggered by the same sequence, and both of those virtual events are bound to the same window tag, then only one of the virtual events will be triggered, and it will be picked at random:

```
event add <<Paste>> <Control-y>
event add <<Paste>> <Button-2>
event add <<Scroll>> <Button-2>
bind Entry <<Paste>> {puts Paste}
bind Entry <<Scroll>> {puts Scroll}
```

If the user types Control-y, the **<<Paste>>** binding will be invoked, but if the user presses button 2 then one of either the **<<Paste>>** or the **<<Scroll>>** bindings will be invoked, but exactly which one gets invoked is undefined.

If an X event does not match any of the existing bindings, then the event is ignored. An unbound event is not considered to be an error.

# Multi-event sequences and ignored events

When a *sequence* specified in a **bind** command contains more than one event pattern, then its script is executed whenever the recent events (leading up to and including the current event) match the given sequence.  This means, for example, that if button 1 is clicked repeatedly the sequence **<Double-Button-1>** will match each button press but the first. If extraneous events that would prevent a match occur in the middle of an event sequence then the extraneous events are ignored unless they are **Key** or **Button** events. For example, **<Double-Button-1>** will match a sequence of presses of button 1, even though there will be **ButtonRelease** events (and possibly **Motion** events) between the **Button** events. Furthermore, a **Key** event may be preceded by any number of other **Key** events for modifier keys without the modifier keys preventing a match. For example, the event sequence **aB** will match a press of the **a** key, a release of the **a** key, a press of the **Shift** key, and a press of the **b** key:  the press of **Shift** is ignored because it is a modifier key. Finally, if several **Motion** events occur in a row, only the last one is used for purposes of matching binding sequences.

# Errors

If an error occurs in executing the script for a binding then the [bgerror] mechanism is used to report the error. The [bgerror] command will be executed at global level (outside the context of any Tcl procedure).

# Examples

Arrange for a string describing the motion of the mouse to be printed out when the mouse is double-clicked:

```
bind . <Double-1> {
    puts "hi from (%x,%y)"
}
```

A little GUI that displays what the keysym name of the last key pressed is:

```
set keysym "Press any key"
pack [label .l -textvariable keysym -padx 2m -pady 1m]
bind . <Key> {
    set keysym "You pressed %K"
}
```


[bgerror]: ../Tcl/bgerror.md
[bindtags]: bindtags.md
[break]: ../Tcl/break.md
[continue]: ../Tcl/continue.md
[event]: event.md
[focus]: focus.md
[grid]: grid.md
[menu]: menu.md
[pack]: pack.md
[return]: ../Tcl/return.md
[text]: text.md
[winfo]: winfo.md

