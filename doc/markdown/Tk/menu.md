---
CommandName: menu
title: menu
ManualSection: n
Version: 4.1
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - bind(n)
 - menubutton(n)
 - ttk::menubutton(n)
 - toplevel(n)
Keywords:
 - menu
 - widget
Copyright:
 - Copyright (c) 1990-1994 The Regents of the University of California.
 - Copyright (c) 1994-1997 Sun Microsystems, Inc.
---

# Name

menu, tk\_menuSetFocus - Create and manipulate 'menu' widgets and menubars

# Synopsis

::: {.synopsis} :::
[menu]{.cmd} [pathName]{.arg} [options]{.optarg}
[tk\_menuSetFocus]{.cmd} [pathName]{.arg}
:::

# Standard options

::: {.standardoptions} :::
-activebackground
-activeborderwidth
-activeforeground
-activerelief
-background
-borderwidth
-cursor
-disabledforeground
-font
-foreground
-relief
-takefocus
:::

See the [options] manual entry for details on the standard options.

# Widget-specific options

[-postcommand]{.cmdline} [postCommand]{.dbname} [Command]{.dbclass}
: If this option is specified then it provides a Tcl command to execute each time the menu is posted.  The command is invoked by the **post** widget command before posting the menu. Note that in Tk 8.0 on Macintosh and Windows, all post-commands in a system of menus are executed before any of those menus are posted. This is due to the limitations in the individual platforms' menu managers.

[-selectcolor]{.cmdline} [selectColor]{.dbname} [Background]{.dbclass}
: For menu entries that are check buttons or radio buttons, this option specifies the color to display in the indicator when the check button or radio button is selected.

[-tearoff]{.cmdline} [tearOff]{.dbname} [TearOff]{.dbclass}
: This option must have a proper boolean value (default is false), which specifies whether or not the menu should include a tear-off entry at the top.  If so, it will exist as entry 0 of the menu and the other entries will number starting at 1.  The default menu bindings arrange for the menu to be torn off when the tear-off entry is invoked. This option is ignored under Aqua/macOS, where menus cannot be torn off.

[-tearoffcommand]{.cmdline} [tearOffCommand]{.dbname} [TearOffCommand]{.dbclass}
: If this option has a non-empty value, then it specifies a Tcl command to invoke whenever the menu is torn off.  The actual command will consist of the value of this option, followed by a space, followed by the name of the menu window, followed by a space, followed by the name of the name of the torn off menu window.  For example, if the option's value is "**a b**" and menu **.x.y** is torn off to create a new menu **.x.tearoff1**, then the command "**a b .x.y .x.tearoff1**" will be invoked. This option is ignored under Aqua/macOS, where menus cannot be torn off.

[-title]{.cmdline} [title]{.dbname} [Title]{.dbclass}
: The string will be used to title the window created when this menu is torn off. If the title is NULL, then the window will have the title of the menubutton or the text of the cascade item from which this menu was invoked.

[-type]{.cmdline} [type]{.dbname} [Type]{.dbclass}
: This option can be one of **menubar**, **tearoff**, or **normal**, and is set when the menu is created. While the string returned by the configuration database will change if this option is changed, this does not affect the menu widget's behavior. This is used by the cloning mechanism and is not normally set outside of the Tk library.


# Introduction

The **menu** command creates a new top-level window (given by the *pathName* argument) and makes it into a menu widget. That menu widget can either be used as a pop-up window or applied to a [toplevel] (with its **-menu** option) to make it into the menubar for that toplevel. Additional options, described above, may be specified on the command line or in the option database to configure aspects of the menu such as its colors and font. The **menu** command returns its *pathName* argument.  At the time this command is invoked, there must not exist a window named *pathName*, but *pathName*'s parent must exist.

A menu is a widget that displays a collection of one-line entries arranged in one or more columns.  There exist several different types of entries, each with different properties.  Entries of different types may be combined in a single menu.  Menu entries are not the same as entry widgets.  In fact, menu entries are not even distinct widgets; the entire menu is one widget.

Menu entries are displayed with up to three separate fields. The main field is a label in the form of a text string, a bitmap, or an image, controlled by the **-label**, **-bitmap**, and **-image** options for the entry. If the  **-accelerator** option is specified for an entry then a second textual field is displayed to the right of the label.  The accelerator typically describes a keystroke sequence that may be used in the application to cause the same result as invoking the menu entry. This is a display option, it does not actually set the corresponding binding (which can be achieved using the [bind] command). The third field is an *indicator*.  The indicator is present only for checkbutton or radiobutton entries.  It indicates whether the entry is selected or not, and is displayed to the left of the entry's string.

In normal use, an entry becomes active (displays itself differently) whenever the mouse pointer is over the entry.  If a mouse button is released over the entry then the entry is *invoked*. The effect of invocation is different for each type of entry; these effects are described below in the sections on individual entries.

Entries may be *disabled*, which causes their labels and accelerators to be displayed with dimmer colors. The default menu bindings will not allow a disabled entry to be activated or invoked. Disabled entries may be re-enabled, at which point it becomes possible to activate and invoke them again.

Whenever a menu's active entry is changed, a <<MenuSelect>> virtual event is send to the menu. The active item can then be queried from the menu, and an action can be taken, such as setting context-sensitive help text for the entry.

# Types of entries

## Command entries

The most common kind of menu entry is a command entry, which behaves much like a button widget.  When a command entry is invoked, a Tcl command is executed.  The Tcl command is specified with the **-command** option.

## Separator entries

A separator is an entry that is displayed as a horizontal dividing line.  A separator may not be activated or invoked, and it has no behavior other than its display appearance.

## Checkbutton entries

A checkbutton menu entry behaves much like a checkbutton widget. When it is invoked it toggles back and forth between the selected and deselected states.  When the entry is selected, a particular value is stored in a particular global variable (as determined by the **-onvalue** and **-variable** options for the entry);  when the entry is deselected another value (determined by the **-offvalue** option) is stored in the global variable. An indicator box is displayed to the left of the label in a checkbutton entry.  If the entry is selected then the indicator's center is displayed in the color given by the **-selectcolor** option for the entry; otherwise the indicator's center is displayed in the background color for the menu.  If a **-command** option is specified for a checkbutton entry, then its value is evaluated as a Tcl command each time the entry is invoked;  this happens after toggling the entry's selected state.

## Radiobutton entries

A radiobutton menu entry behaves much like a radiobutton widget. Radiobutton entries are organized in groups of which only one entry may be selected at a time.  Whenever a particular entry becomes selected it stores a particular value into a particular global variable (as determined by the **-value** and **-variable** options for the entry).  This action causes any previously-selected entry in the same group to deselect itself. Once an entry has become selected, any change to the entry's associated variable will cause the entry to deselect itself. Grouping of radiobutton entries is determined by their associated variables:  if two entries have the same associated variable then they are in the same group. An indicator diamond is displayed to the left of the label in each radiobutton entry.  If the entry is selected then the indicator's center is displayed in the color given by the **-selectcolor** option for the entry; otherwise the indicator's center is displayed in the background color for the menu.  If a **-command** option is specified for a radiobutton entry, then its value is evaluated as a Tcl command each time the entry is invoked;  this happens after selecting the entry.

## Cascade entries

A cascade entry is one with an associated menu (determined by the **-menu** option).  Cascade entries allow the construction of cascading menus. The **postcascade** widget command can be used to post and unpost the associated menu just next to of the cascade entry. The associated menu must be a child of the menu containing the cascade entry (this is needed in order for menu traversal to work correctly).

A cascade entry posts its associated menu by invoking a Tcl command of the form

```
menu post x y
```

where *menu* is the path name of the associated menu, and *x* and *y* are the root-window coordinates of the upper-right corner of the cascade entry. On Unix, the lower-level menu is unposted by executing a Tcl command with the form

```
menu unpost
```

where *menu* is the name of the associated menu. On other platforms, the platform's native code takes care of unposting the menu.

If a **-command** option is specified for a cascade entry then it is not evaluated when the user selects the entry: selecting it posts the associated submenu.  It is evaluated only if the entry is invoked with the **invoke** widget command.  Use the **-postcommand** option of the submenu for a command evaluated when the submenu is posted.

## Tear-off entries

A tear-off entry appears at the top of the menu if enabled with the **-tearoff** option.  It is not like other menu entries in that it cannot be created with the **add** widget command and cannot be deleted with the **delete** widget command. When a tear-off entry is created it appears as a dashed line at the top of the menu.  Under the default bindings, invoking the tear-off entry causes a torn-off copy to be made of the menu and all of its submenus.

# Menubars

Any menu can be set as a menubar for a toplevel window (see [toplevel] command for syntax). On the Macintosh, whenever the toplevel is in front, this menu's cascade items will appear in the menubar across the top of the main screen. On Windows and Unix, this menu's items will be displayed in a menubar across the top of the window. These menus will behave according to the interface guidelines of their platforms. In particular, since macOS requires that a menubar always be displayed for the active app, if no menu is assigned by a **configure -menu** for a certain toplevel then a standard default menu will be displayed whenever that toplevel has focus.  For every menu set as a menubar, a clone menu is made. See the [Clones] section for more information.

As noted, menubars may behave differently on different platforms.  One example of this concerns the handling of checkbuttons and radiobuttons within the menu.  While it is permitted to put these menu elements on menubars, they may not be drawn with indicators on some platforms, due to system restrictions.

## Special menus in menubars

Certain menus in a menubar will be treated specially.  On the Macintosh, access to the special Application, Window and Help menus is provided. On Windows, access to the Windows System menu in each window is provided. On X Windows, a special right-justified help menu may be provided if Motif menu compatibility is enabled. In all cases, these menus must be created with the command name of the menubar menu concatenated with the special name. So for a menubar named .menubar, on the Macintosh, the special menus would be .menubar.apple, .menubar.window and .menubar.help; on Windows, the special menu would be .menubar.system; on X Windows, the help menu would be .menubar.help.

When Tk sees a .menubar.apple menu as the first menu in a menubar on the Macintosh, that menu's contents make up the first items of the Application menu whenever the window containing the menubar is in front. After all of the Tk-defined items, the menu will have a separator, followed by all standard Application menu items. Such a .apple menu must be present in a menu when that menu is first configured as a toplevel's menubar, otherwise a default application menu (hidden from Tk) will be inserted into the menubar at that time and subsequent addition of a .apple menu will no longer result in it becoming the Application menu.

When Tk sees a .menubar.window menu on the Macintosh, the menu's contents are inserted into the standard Window menu of the user's menubar whenever the window's menubar is in front. The first items in the menu are provided by macOS, and the names of the current toplevels are automatically appended after all the Tk-defined items and a separator. The Window menu on the Mac also allows toggling the window into a fullscreen state, and managing a tabbed window interface (multiple windows grouped into a single window) if supported by that version of the operating system.

When Tk sees a .menubar.help menu on the Macintosh, the menu's contents are appended to the standard Help menu of the user's menubar whenever the window's menubar is in front. The first items in the menu are provided by macOS.

When Tk sees a System menu on Windows, its items are appended to the system menu that the menubar is attached to. This menu is tied to the application icon and can be invoked with the mouse or by typing Alt+Spacebar.  Due to limitations in the Windows API, any font changes, colors, images, bitmaps, or tearoff images will not appear in the system menu.

When Tk sees a Help menu on X Windows and Motif menu compatibility is enabled the menu is moved to be last in the menubar and is right justified. Motif menu compatibility is enabled by setting the Tk option **\*Menu.useMotifHelp** to true.

# Clones

When a menu is set as a menubar for a toplevel window, or when a menu is torn off, a clone of the menu is made. This clone is a menu widget in its own right, but it is a child of the original. Changes in the configuration of the original are reflected in the clone. Additionally, any cascades that are pointed to are also cloned so that menu traversal will work right. Clones are destroyed when either the tearoff or menubar goes away, or when the original menu is destroyed.

It is the clone which is posted and displayed, not the original menu, and on Windows and macOS the menus are drawn by the system, so no Tk window is mapped at all.  Therefore [winfo] and [wm] do not return any meaningful geometry information for a menu which is used as a menubar or is torn off, nor for the menus of its cascade entries.

# Widget command

The **menu** command creates a new Tcl command whose name is *pathName*.  This command may be used to invoke various operations on the widget.  It has the following general form:

```
pathName option ?arg ...?
```

*Option* and the *arg*s determine the exact behavior of the command.

Many of the widget commands for a menu take as one argument an indicator of which entry of the menu to operate on. These indicators are called *index*es and may be specified in any of the following forms:

**active**
: Indicates the entry that is currently active.  If no entry is active then this form is equivalent to **{}**.  This form may not be abbreviated.

**end**
: Indicates the bottommost entry in the menu.  If there are no entries in the menu then this form is equivalent to **{}**. This form may not be abbreviated.

**last**
: Same as **end**.

**{}**
: Indicates "no entry at all"; this is used most commonly with the **activate** option to deactivate all the entries in the menu.  In most cases the specification of **{}** causes nothing to happen in the widget command.

**@***x***,***y*
: Indicates the entry that covers the point in the menu's window specified by *x* and *y* (in pixel coordinates). If no entry covers that point, then this form is equivalent to **{}**. If only a single number is specified, it is treated as the y-coordinate.

*number*
: Specifies the entry numerically, where 0 corresponds to the top-most entry of the menu, 1 to the entry below it, and so on.

*id*
: If the index does not satisfy one of the above forms then the menu is searched for an entry with the specified id.

*pattern*
: If all of the above methods for finding an entry fail, this form is used.  *Pattern* is pattern-matched against the label of each entry in the menu, in order from the top down, until a matching entry is found.  The rules of [string match][string] are used.


If the index could match more than one of the above forms, then the form earlier in the above list takes precedence. Indexes support the same simple interpretation as for the command [string index][string], with simple integer index arithmetic and indexing relative to **end**.

The following widget commands are possible for menu widgets:

[pathName]{.ins} [activate]{.sub} [index]{.arg}
: Change the state of the entry indicated by *index* to **active** and redisplay it using its active colors. Any previously-active entry is deactivated.  If *index* is specified as **{}**, or if the specified entry is disabled, then the menu ends up with no active entry. Returns an empty string.

[pathName]{.ins} [add]{.sub} [type]{.arg} [id]{.optarg} [option value option value]{.optdot}
: Add a new entry to the bottom of the menu.  The new entry's type is given by *type* and must be one of **cascade**, **checkbutton**, **command**, **radiobutton**, or **separator**, or a unique abbreviation of one of the above. If the *id* argument is specified, it is used as the entry identifier; *id* must not already exist in the menu. Otherwise, a new unique identifier is generated. If additional arguments are present, they specify the options listed in the [Menu entry options] section below. The **add** widget command returns the id of the new entry.

[pathName]{.ins} [cget]{.sub} [option]{.arg}
: Returns the current value of the configuration option given by *option*. *Option* may have any of the values accepted by the **menu** command.

[pathName]{.ins} [clone]{.sub} [newPathname]{.arg} [cloneType]{.optarg}
: Makes a clone of the current menu named *newPathName*. This clone is a menu in its own right, but any changes to the clone are propagated to the original menu and vice versa. *cloneType* can be **normal**, **menubar**, or **tearoff**. Should not normally be called outside of the Tk library. See the [Clones] section for more information.

[pathName]{.ins} [configure]{.sub} [option]{.optarg} [value option value]{.optdot}
: Query or modify the configuration options of the widget. If no *option* is specified, returns a list describing all of the available options for *pathName* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list).  If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified).  If one or more *option-value* pairs are specified, then the command modifies the given widget option(s) to have the given value(s);  in this case the command returns an empty string. *Option* may have any of the values accepted by the **menu** command.

[pathName]{.ins} [delete]{.sub} [index1]{.arg} [index2]{.optarg}
: Delete all of the menu entries between *index1* and *index2* inclusive. If *index2* is omitted then it defaults to *index1*. Attempts to delete a tear-off menu entry are ignored (instead, you should change the **-tearoff** option to remove the tear-off entry).

[pathName]{.ins} [entrycget]{.sub} [index]{.arg} [option]{.arg}
: Returns the current value of a configuration option for the entry given by *index*. *Option* may have any of the names described in the [Menu entry options] section below.

[pathName]{.ins} [entryconfigure]{.sub} [index]{.arg} [options...]{.optarg}
: This command is similar to the **configure** command, except that it applies to the options for an individual entry, whereas **configure** applies to the options for the menu as a whole. *Options* may have any of the values described in the [Menu entry options] section below.  If *options* are specified, options are modified as indicated in the command and the command returns an empty string. If no *options* are specified, returns a list describing the current options for entry *index* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list).

[pathName]{.ins} [id]{.sub} [index]{.arg}
: Returns the id of the menu entry given by *index*. This is the identifier that was assigned to the entry when it was created using the **add** or **insert** widget command. Returns an empty string for the tear-off entry, or if *index* is equivalent to **{}**.

[pathName]{.ins} [index]{.sub} [index]{.arg}
: Returns the numerical index corresponding to *index*, or **{}** if *index* was specified as **{}**.

[pathName]{.ins} [insert]{.sub} [index]{.arg} [type]{.arg} [id]{.optarg} [option value option value]{.optdot}
: Same as the **add** widget command except that it inserts the new entry just before the entry given by *index*, instead of appending to the end of the menu.  The *type*, *id*, *option*, and *value* arguments have the same interpretation as for the **add** widget command.  It is not possible to insert new menu entries before the tear-off entry, if the menu has one. The **insert** widget command returns the id of the new entry.

[pathName]{.ins} [invoke]{.sub} [index]{.arg}
: Invoke the action of the menu entry.  See the sections on the individual entries above for details on what happens.  If the menu entry is disabled then nothing happens.  If the entry has a command associated with it then the result of that command is returned as the result of the **invoke** widget command.  Otherwise the result is an empty string.  Note:  invoking a menu entry does not automatically unpost the menu;  the default bindings normally take care of this before invoking the **invoke** widget command.

[pathName]{.ins} [post]{.sub} [x]{.arg} [y]{.arg} [index]{.optarg}
: Arrange for the menu to be displayed on the screen at the root-window coordinates given by *x* and *y*.  If an index is specified the menu will be located so that the entry with that index is displayed at the point.  These coordinates are adjusted if necessary to guarantee that the entire menu is visible on the screen.  This command normally returns an empty string.  If the **-postcommand** option has been specified, then its value is executed as a Tcl script before posting the menu and the result of that script is returned as the result of the **post** widget command.  If an error returns while executing the command, then the error is returned without posting the menu. On X11 this command returns as soon as the menu is posted.  On Windows and macOS the menu is displayed by the system, which runs its own event loop, and the command does not return before the menu is unposted again.

[pathName]{.ins} [postcascade]{.sub} [index]{.arg}
: Posts the submenu associated with the cascade entry given by *index*, and unposts any previously posted submenu. If *index* does not correspond to a cascade entry, or if *pathName* is not posted, the command has no effect except to unpost any currently posted submenu.

[pathName]{.ins} [type]{.sub} [index]{.arg}
: Returns the type of the menu entry given by *index*. This is the *type* argument passed to the **add** or **insert** widget command when the entry was created, such as **command** or **separator**, or **tearoff** for a tear-off entry.

[pathName]{.ins} [unpost]{.sub}
: Unmap the window so that it is no longer displayed.  If a lower-level cascaded menu is posted, unpost that menu.  Returns an empty string. This subcommand does not work on Windows and the Macintosh, as those platforms have their own way of unposting menus.

[pathName]{.ins} [xposition]{.sub} [index]{.arg}
: Returns a decimal string giving the x-coordinate within the menu window of the leftmost pixel in the entry specified by *index*.

[pathName]{.ins} [yposition]{.sub} [index]{.arg}
: Returns a decimal string giving the y-coordinate within the menu window of the topmost pixel in the entry specified by *index*. If the menu is scrolled (see **yview**), the coordinate is given as if the menu was not scrolled.

[pathName]{.ins} [yview]{.sub} [args]{.optarg}
: This command is used to query and change the vertical scrolling of the entries of a menu which is higher than the screen.  Such a menu is limited to the height of the screen, and its entries can be scrolled with the arrows displayed at its top and bottom.  This is only done on X11; on Windows and macOS the system scrolls menus. It can take any of the following forms:

    *pathName* **yview**
    : Returns a list containing two elements, both of which are real fractions between 0 and 1.  The first element gives the position of the entries at the top of the visible area, relative to the height of all entries (0.0 means the first entry is at the top).  The second element gives the position just after the visible area.  For a menu which is not scrolled, the result is **0.0 1.0**.

    *pathName* **yview moveto** *fraction*
    : Scrolls the entries so that the given fraction of their height is above the visible area.

    *pathName* **yview scroll** *number what*
    : Scrolls the entries by *number* lines or pages.  *What* must be either **units** (the height of a line of the menu font) or **pages** (the height of the visible area, minus a line).



# Menu entry options

The following options are allowed on menu entries. Most options are not supported by all entry types.

[-activebackground]{.lit} [value]{.arg}
: Specifies a background color to use for displaying this entry when it is active. This option is ignored on Aqua/macOS. If it is specified as an empty string (the default), then the **-activebackground** option for the overall menu is used. If the [tk\_strictMotif][tkvars] variable has been set to request strict Motif compliance, then this option is ignored and the **-background** option is used in its place. This option is not available for separator or tear-off entries.

[-activeforeground]{.lit} [value]{.arg}
: Specifies a foreground color to use for displaying this entry when it is active.   This option is ignored on Aqua/macOS. If this option is specified as an empty string (the default), then the **-activeforeground** option for the overall menu is used.

[-accelerator]{.lit} [value]{.arg}
: Specifies a string to display at the right side of the menu entry. Normally describes an accelerator keystroke sequence that may be used to invoke the same function as the menu entry. This is a display option, it does not actually set the corresponding binding (which can be achieved using the [bind] command). This option is not available for separator or tear-off entries.

[-background]{.lit} [value]{.arg}
: Specifies a background color to use for displaying this entry when it is in the normal state (neither active nor disabled). This option is ignored on Aqua/macOS. If it is specified as an empty string (the default), then the **-background** option for the overall menu is used. This option is not available for separator or tear-off entries.

[-bitmap]{.lit} [value]{.arg}
: Specifies a bitmap to display in the menu instead of a textual label, in any of the forms accepted by [Tk\_GetBitmap][GetBitmap]. This option overrides the **-label** option (as controlled by the **-compound** option) but may be reset to an empty string to enable a textual label to be displayed. If a **-image** option has been specified, it overrides **-bitmap**. This option is not available for separator or tear-off entries.

[-columnbreak]{.lit} [value]{.arg}
: When this option is zero, the entry appears below the previous entry. When this option is one, the entry appears at the top of a new column in the menu. This option is ignored on Aqua/macOS, where menus are always a single column.

[-command]{.lit} [value]{.arg}
: Specifies a Tcl command to execute when the menu entry is invoked. Not available for separator or tear-off entries.

[-compound]{.lit} [value]{.arg}
: Specifies whether the menu entry should display both an image and text, and if so, where the image should be placed relative to the text. Valid values for this option are **bottom**, **center**, **left**, **none**, **right** and **top**.  The default value is **none**, meaning that the button will display either an image or text, depending on the values of the **-image** and **-bitmap** options.

[-font]{.lit} [value]{.arg}
: Specifies the font to use when drawing the label or accelerator string in this entry. If this option is specified as an empty string (the default) then the **-font** option for the overall menu is used. This option is not available for separator or tear-off entries.

[-foreground]{.lit} [value]{.arg}
: Specifies a foreground color to use for displaying this entry when it is in the normal state (neither active nor disabled). This option is ignored on Aqua/macOS. If it is specified as an empty string (the default), then the **-foreground** option for the overall menu is used. This option is not available for separator or tear-off entries.

[-hidemargin]{.lit} [value]{.arg}
: Specifies whether the standard margins should be drawn for this menu entry. This is useful when creating palette with images in them, i.e., color palettes, pattern palettes, etc. 1 indicates that the margin for the entry is hidden; 0 means that the margin is used.

[-image]{.lit} [value]{.arg}
: Specifies an image to display in the menu instead of a text string or bitmap. The image must have been created by some previous invocation of [image create][image]. This option overrides the **-label** and **-bitmap** options (as controlled by the **-compound** option) but may be reset to an empty string to enable a textual or bitmap label to be displayed. This option is not available for separator or tear-off entries.

[-indicatoron]{.lit} [value]{.arg}
: Available only for checkbutton and radiobutton entries. *Value* is a boolean that determines whether or not the indicator should be displayed.

[-label]{.lit} [value]{.arg}
: Specifies a string to display as an identifying label in the menu entry.  Not available for separator or tear-off entries.

[-menu]{.lit} [value]{.arg}
: Available only for cascade entries.  Specifies the path name of the submenu associated with this entry. The submenu must be a child of the menu.

[-offvalue]{.lit} [value]{.arg}
: Available only for checkbutton entries.  Specifies the value to store in the entry's associated variable when the entry is deselected.

[-onvalue]{.lit} [value]{.arg}
: Available only for checkbutton entries.  Specifies the value to store in the entry's associated variable when the entry is selected.

[-selectcolor]{.lit} [value]{.arg}
: Available only for checkbutton and radiobutton entries. Specifies the color to display in the indicator when the entry is selected. If the value is an empty string (the default) then the **-selectcolor** option for the menu determines the indicator color.

[-selectimage]{.lit} [value]{.arg}
: Available only for checkbutton and radiobutton entries. Specifies an image to display in the entry (in place of the **-image** option) when it is selected. *Value* is the name of an image, which must have been created by some previous invocation of [image create][image]. This option is ignored unless the **-image** option has been specified.

[-state]{.lit} [value]{.arg}
: Specifies one of three states for the entry:  **normal**, **active**, or **disabled**.  In normal state the entry is displayed using the **-foreground** option for the menu and the **-background** option from the entry or the menu. The active state is typically used when the pointer is over the entry. In active state the entry is displayed using the **-activeforeground** option for the menu along with the **-activebackground** option from the entry.  Disabled state means that the entry should be insensitive:  the default bindings will refuse to activate or invoke the entry. In this state the entry is displayed according to the **-disabledforeground** option for the menu and the **-background** option from the entry. This option is not available for separator entries.

[-underline]{.lit} [value]{.arg}
: Specifies the integer index of a character to underline in the entry. This option is also queried by the default bindings and used to implement keyboard traversal. 0 corresponds to the first character of the text displayed in the entry, 1 to the next character, and so on. If a bitmap or image is displayed in the entry then this option is ignored. This option is not available for separator or tear-off entries.

[-value]{.lit} [value]{.arg}
: Available only for radiobutton entries.  Specifies the value to store in the entry's associated variable when the entry is selected. If an empty string is specified, then the **-label** option for the entry as the value to store in the variable.

[-variable]{.lit} [value]{.arg}
: Available only for checkbutton and radiobutton entries.  Specifies the name of a global variable to set when the entry is selected. For checkbutton entries the variable is also set when the entry is deselected.  For radiobutton entries, changing the variable causes the currently-selected entry to deselect itself.

    For checkbutton entries, the default value of this option is taken from the **-label** option, and for radiobutton entries a single fixed value is used. It is recommended that you always set the **-variable** option when creating either a checkbutton or a radiobutton.


# Menu configurations

The default bindings support four different ways of using menus:

**Pulldown Menus in Menubar**
: This is the most common case. You create a menu widget that will become the menu bar. You then add cascade entries to this menu, specifying the pull down menus you wish to use in your menu bar. You then create all of the pulldowns. Once you have done this, specify the menu using the **-menu** option of the toplevel's widget command. See the [toplevel] manual entry for details.

**Pulldown Menus in Menu Buttons**
: This is the compatible way to do menu bars.  You create one menubutton widget for each top-level menu, and typically you arrange a series of menubuttons in a row in a menubar window.  You also create the top-level menus and any cascaded submenus, and tie them together with **-menu** options in menubuttons and cascade menu entries.  The top-level menu must be a child of the menubutton, and each submenu must be a child of the menu that refers to it.  Once you have done this, the default bindings will allow users to traverse and invoke the tree of menus via its menubutton;  see the [menubutton] manual entry for details.

**Popup Menus**
: Popup menus typically post in response to a mouse button press or keystroke.  You create the popup menus and any cascaded submenus, then you call the [tk\_popup][popup] procedure at the appropriate time to post the top-level menu.

**Option Menus**
: An option menu consists of a menubutton with an associated menu that allows you to select one of several values.  The current value is displayed in the menubutton and is also stored in a global variable.  Use the [tk\_optionMenu][optionMenu] procedure to create option menubuttons and their menus.

**Torn-off Menus**
: You create a torn-off menu by invoking the tear-off entry at the top of an existing menu.  The default bindings will create a new menu that is a copy of the original menu and leave it permanently posted as a top-level window.  The torn-off menu behaves just the same as the original menu.


# Default bindings

Tk automatically creates class bindings for menus that give them the following default behavior:

1. When the mouse enters a menu, the entry underneath the mouse cursor activates;  as the mouse moves around the menu, the active entry changes to track the mouse.

2. When the mouse leaves a menu all of the entries in the menu deactivate, except in the special case where the mouse moves from a menu to a cascaded submenu.

3. When a button is released over a menu, the active entry (if any) is invoked. The menu also unposts unless it is a torn-off menu. However, if the menu was posted under the mouse cursor by the button press (for example, a menu moved to fit on the screen), the release is ignored and no entry is activated until the mouse moves.

4. The Space and Return keys invoke the active entry and unpost the menu.

5. If any of the entries in a menu have letters underlined with the **-underline** option, then pressing one of the underlined letters (or its upper-case or lower-case equivalent) invokes that entry and unposts the menu.

6. The Escape key aborts a menu selection in progress without invoking any entry.  It also unposts the menu unless it is a torn-off menu.

7. The Up and Down keys activate the next higher or lower entry in the menu.  When one end of the menu is reached, the active entry wraps around to the other end.

8. The Left key moves to the next menu to the left. If the current menu is a cascaded submenu, then the submenu is unposted and the current menu entry becomes the cascade entry in the parent. If the current menu is a top-level menu posted from a menubutton, then the current menubutton is unposted and the next menubutton to the left is posted. Otherwise the key has no effect. The left-right order of menubuttons is determined by their stacking order:  Tk assumes that the lowest menubutton (which by default is the first one created) is on the left.

9. The Right key moves to the next menu to the right. If the current entry is a cascade entry, then the submenu is posted and the  current menu entry becomes the first entry in the submenu. Otherwise, if the current menu was posted from a menubutton, then the current menubutton is unposted and the next menubutton to the right is posted.

10. On X11, a menu higher than the screen is scrolled with the mouse wheel, or while the mouse is over the arrow at its top or bottom.  An entry which is activated with the keyboard is scrolled into view.


Disabled menu entries are non-responsive:  they do not activate and they ignore mouse button presses and releases.

Several of the bindings make use of the command **tk\_menuSetFocus**. It saves the current focus and sets the focus to its *pathName* argument, which is a menu widget.

The behavior of menus can be changed by defining new bindings for individual widgets or by redefining the class bindings.

# Bugs

At present it is not possible to use the option database to specify values for the options to individual entries.

# Platform notes

**Windows**
: The menubar of a toplevel may only honor font and color options when the not documented MS-Windows messages WM\_UAHDRAWMENU, WM\_UAHDRAWMENUITEM and WM\_UAHMEASUREMENUITEM are sent by the system. Currently, this is the case for MS-Windows 8 to 11, but not for Wine. Any sub-menu will always honor those options.

**macOS**
: font and colors of menus may not be customized.



[bind]: bind.md
[ConfigWidg]: ../TkCAPI/ConfigWidg.md
[GetBitmap]: ../TkCAPI/GetBitmap.md
[image]: image.md
[menubutton]: menubutton.md
[optionMenu]: optionMenu.md
[options]: options.md
[popup]: popup.md
[string]: ../Tcl/string.md
[tkvars]: tkvars.md
[toplevel]: toplevel.md
[winfo]: winfo.md
[wm]: wm.md

