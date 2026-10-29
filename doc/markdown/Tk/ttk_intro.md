---
CommandName: ttk::intro
title: ttk::intro
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - ttk::style(n)
Copyright:
 - Copyright (c) 2004 Joe English
---

# Name

ttk::intro - Introduction to the Tk theme engine

# Overview

The Tk themed widget set is based on a revised and enhanced version of TIP #48 (https://tip.tcl-lang.org/48) specified style engine. The main concepts are described below. The basic idea is to separate, to the extent possible, the code implementing a widget's behavior from the code implementing its appearance. Widget class bindings are primarily responsible for maintaining the widget state and invoking callbacks; all aspects of the widget's appearance are controlled by the style of the widget (i.e. the style of the elements of the widget).

# Themes

A *theme* is a collection of elements and styles that determine the look and feel of the widget set. Themes can be used to:

- isolate platform differences (X11 vs. classic Windows vs. XP vs. Aqua ...)

- adapt to display limitations (low-color, grayscale, monochrome, tiny screens)

- accessibility (high contrast, large type)

- application suite branding

- blend in with the rest of the desktop (Gnome, KDE, Java)

- and, of course: eye candy.


# Elements

An *element* displays an individual part of a widget. For example, a vertical scrollbar widget contains **uparrow**, **downarrow**, **trough** and **slider** elements.

Element names use a recursive dotted notation. For example, **uparrow** identifies a generic arrow element, and **Scrollbar.uparrow** and **Combobox.uparrow** identify widget-specific elements. When looking for an element, the style engine looks for the specific name first, and if an element of that name is not found it looks for generic elements by stripping off successive leading components of the element name.

Like widgets, elements have *options* which specify what to display and how to display it. For example, the **text** element (which displays a text string) has **-text**, **-font**, **-foreground**, **-background**, **-underline**, and **-width** options. The value of an element option is taken from (in precedence order):

- an option of the same name and type in the widget containing the element;

- a dynamic setting specified by **style map** and the current state;

- the default setting specified by **style configure**; or

- the element's built-in default value for the option.


# Layouts

A *layout* specifies which elements make up a widget and how they are arranged. The layout engine uses a simplified version of the [pack] algorithm: starting with an initial cavity equal to the size of the widget, elements are allocated a parcel within the cavity along the side specified by the **-side** option, and placed within the parcel according to the **-sticky** option. For example, the layout for a horizontal scrollbar is:

```
ttk::style layout Horizontal.TScrollbar {
    Scrollbar.trough -children {
	Scrollbar.leftarrow  -side left  -sticky w
	Scrollbar.rightarrow -side right -sticky e
	Scrollbar.thumb -sticky ew
    }
}
```

By default, the layout for a widget is the same as its class name. Some widgets may override this (for example, the [ttk::scrollbar][ttk_scrollbar] widget chooses different layouts based on the **-orient** option).

# States

In standard Tk, many widgets have a **-state** option which (in most cases) is either **normal** or **disabled**. Some widgets support additional states, such as the [entry] widget which has a **readonly** state and the various flavors of buttons which have **active** state.

The themed Tk widgets generalizes this idea: every widget has a bitmap of independent state flags. Widget state flags include **active**, **disabled**, **pressed**, **focus**, etc., (see *ttk::widget(n)* for the full list of state flags).

Instead of a **-state** option, every widget now has a **state** widget command which is used to set or query the state. A *state specification* is a list of symbolic state names indicating which bits are set, each optionally prefixed with an exclamation point indicating that the bit is cleared instead.

For example, the class bindings for the [ttk::button][ttk_button] widget are:

```
bind TButton <Enter>		{ %W state active }
bind TButton <Leave>		{ %W state !active }
bind TButton <Button-1>		{ %W state pressed }
bind TButton <Button1-Leave>	{ %W state !pressed }
bind TButton <Button1-Enter>	{ %W state pressed }
bind TButton <ButtonRelease-1>	\
    { %W instate {pressed} { %W state !pressed ; %W invoke } }
```

This specifies that the widget becomes **active** when the pointer enters the widget, and inactive when it leaves. Similarly it becomes **pressed** when the mouse button is pressed, and **!pressed** on the ButtonRelease event. In addition, the button unpresses if pointer is dragged outside the widget while Button-1 is held down, and represses if it's dragged back in. Finally, when the mouse button is released, the widget's **-command** is invoked, but only if the button is currently in the **pressed** state. (The actual bindings are a little more complicated than the above, but not by much).

# Styles

Each widget is associated with a *style*, which specifies values for element options. Style names use a recursive dotted notation like layouts and elements; by default, widgets use the class name to look up a style in the current theme. For example:

```
ttk::style configure TButton \
	-background #d9d9d9 \
	-foreground black \
	-relief raised \
	;
```

Many elements are displayed differently depending on the widget state. For example, buttons have a different background when they are active, a different foreground when disabled, and a different relief when pressed. The **style map** command specifies dynamic option settings for a particular style:

```
ttk::style map TButton \
	-background [list disabled #d9d9d9  active #ececec] \
	-foreground [list disabled #a3a3a3] \
	-relief [list {pressed !disabled} sunken] \
	;
```


[entry]: entry.md
[pack]: pack.md
[ttk_button]: ttk_button.md
[ttk_scrollbar]: ttk_scrollbar.md

