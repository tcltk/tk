---
CommandName: ttk::style
title: ttk::style
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::intro(n)
 - ttk::widget(n)
 - photo(n)
 - ttk_image(n)
Keywords:
 - style
 - theme
 - appearance
Copyright:
 - Copyright (c) 2004 Joe English
---

# Name

ttk::style - Manipulate style database

# Synopsis

::: {.synopsis} :::
[ttk::style]{.cmd} [option]{.arg} [args]{.optarg}
:::

# Notes

See also the Tcl'2004 conference presentation, available at https://tktable.sourceforge.net/tile/tile-tcl2004.pdf

# Definitions

Each widget is assigned a *style*, which specifies the set of elements making up the widget and how they are arranged, along with dynamic and default settings for element options. By default, the style name is the same as the widget's class; this may be overridden by the **-style** option.

A *theme* is a collection of elements and styles which controls the overall look and feel of an application. The "." style is the theme root style on which derived styles are based.

# Description

The **ttk::style** command takes the following arguments:

[ttk::style]{.cmd} [configure]{.sub} [style]{.arg}
: see below ...

**ttk::style configure** *style* *-option*
: see below ...

**ttk::style configure** *style* *-option value* ?*option value ...*?
: Get the value(s) or sets the default value(s) of the specified option(s) in *style*. If *style* does not exist, it is created. In its forst form (when only a *style* is specified), returns a list of all options and their default values of style *style*. In its second form (when only the *style* and an *-option* are specified), get the default value for option *-option* of style *style*. Otherwise, in its third form, set the specified *-option*s to the given *value*s.

[ttk::style]{.cmd} [element]{.sub} [args]{.arg}
: 
[ttk::style]{.cmd} [element]{.sub} [create]{.lit} [elementName]{.arg} [type]{.arg} [args...]{.optarg}
: Creates a new element in the current theme of type *type*. The only cross-platform built-in element type is *image* (see [ttk\_image][ttk_image](n)) but themes may define other element types (see **Ttk\_RegisterElementFactory**). On suitable versions of Windows an element factory is registered to create Windows theme elements (see [ttk\_vsapi][ttk_vsapi](n)).

**ttk::style element names**
: Returns the list of elements defined in the current theme.

**ttk::style element options** *element*
: Returns the list of *element*'s options.


**ttk::style layout** *style* ?*layoutSpec*?
: Define the widget layout for style *style*. See [Layouts] below for the format of *layoutSpec*. If *layoutSpec* is omitted, return the layout specification for style *style*.

[ttk::style]{.cmd} [lookup]{.sub} [style]{.arg} [-option]{.arg} [state]{.optarg}
: see below ...

**ttk::style lookup** *style -option* *state* ?*default*?
: Returns the value(s) specified for *-option* in style *style*, using the standard lookup rules for element options. *state* is a list of state names; if omitted, it defaults to all bits off (the "normal" state). If the *default* argument is present, it is used as a fallback value in case no specification for *-option* is found. If *style* does not exist, it is created.

[ttk::style]{.cmd} [map]{.sub} [style]{.arg} [-option statespecList]{.optdot}
: Sets dynamic (state dependent) values of the specified option(s) in *style*. *statespecList* is a list with pairs of *statespec* and *value*, and each pair is examined in order; the value corresponding to the first matching *statespec* is used. If *style* does not exist, it is created. If only *style* and *-option* are specified, get the dynamic values for option *-option* of style *style*. If only *style* is specified, get the dynamic values for all options of style *style*.

[ttk::style]{.cmd} [theme]{.sub} [args]{.arg}
: 
**ttk::style theme create** *themeName* ?**-parent** *basedon*? ?**-settings** *script...* ?
: Creates a new theme.  It is an error if *themeName* already exists. If **-parent** is specified, the new theme will inherit styles, elements, and layouts from the parent theme *basedon*. If **-settings** is present, *script* is evaluated in the context of the new theme as per **ttk::style theme settings**.

**ttk::style theme names**
: Returns a list of all known themes.

**ttk::style theme settings** *themeName script*
: Temporarily sets the current theme to *themeName*, evaluate *script*, then restore the previous theme. Typically *script* simply defines styles and elements, though arbitrary Tcl code may appear.

**ttk::style theme styles** ?*themeName*?
: Returns a list of all styles in *themeName*. If *themeName* is omitted, the current theme is used.

**ttk::style theme use** ?*themeName*?
: Without an argument the result is the name of the current theme. Otherwise this command sets the current theme to *themeName*, and refreshes all widgets.



# Layouts

A *layout* specifies a list of elements, each followed by one or more options specifying how to arrange the element. The layout mechanism uses a simplified version of the [pack] geometry manager: given an initial cavity, each element is allocated a parcel. Then the parcel actually used by the element is adjusted within the allocated parcel. Valid options are:

[-children]{.lit} [sublayoutList]{.arg}
: Specifies a list of one or more sublayouts with their elements to place inside the element.

[-expand]{.lit} [boolean]{.arg}
: Specifies whether the allocated parcel is the entire cavity. If so, simultaneous specification of **-side** is ignored. Defaults to 0.

[-side]{.lit} [side]{.arg}
: Specifies which side of the cavity to place the element; one of **left**, **right**, **top**, or **bottom**. For instance, **-side top** allocates the parcel along the top of the cavity having width and height respectively the width of the cavity and the height of the element. If omitted, the allocated parcel is the entire cavity (same effect as **-expand** 1).

[-sticky]{.lit} [style]{.lit}
: Specifies the actual parcel position and size inside the allocated parcel. *style* is a string that contains zero or more of the characters n, s, e or w. If specified as an empty string then the actual parcel is centered in the allocated parcel. The default *style* is nswe.


For example:

```
ttk::style layout Horizontal.TScrollbar {
    Scrollbar.trough -children {
        Scrollbar.leftarrow -side left
        Scrollbar.rightarrow -side right
        Horizontal.Scrollbar.thumb -side left -sticky ew
    }
}
```

# Root style

The "." theme root style can be configured like any other style, providing defaults for its derived styles. In addition to the usual options, "." styling options configurable with **ttk::style** are:

**-insertofftime** *amount* Specifies a non-negative integer value indicating the number of milliseconds the insertion cursor should remain "off" in each blink cycle. If this option is zero then the cursor does not blink: it is on all the time. Defaults to 300 ms, unless overriden with a **RESOURCE\_MANAGER** property or **.Xdefaults** file.

**-insertontime** *amount* Specifies a non-negative integer value indicating the number of milliseconds the insertion cursor should remain "on" in each blink cycle. Defaults to 600 ms, unless overriden with a **RESOURCE\_MANAGER** property or **.Xdefaults** file.


[pack]: pack.md
[ttk_image]: ttk_image.md
[ttk_vsapi]: ttk_vsapi.md

