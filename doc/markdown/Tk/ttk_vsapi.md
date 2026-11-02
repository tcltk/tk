---
CommandName: ttk_vsapi
title: ttk_vsapi
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::intro(n)
 - ttk::widget(n)
 - ttk::style(n)
 - ttk_image(n)
Keywords:
 - style
 - theme
 - appearance
 - windows
Copyright:
 - Copyright (c) 2008 Pat Thoyts
---

# Name

ttk\_vsapi - Define a Microsoft Visual Styles element

# Synopsis

::: {.synopsis} :::
[ttk::style]{.cmd} [element]{.sub} [create]{.lit} [name]{.arg} [vsapi]{.lit} [className]{.arg} [partId]{.arg} [stateMap]{.optarg} [options]{.optarg}
:::

# Description

The **vsapi** element factory creates a new element in the current theme whose visual appearance is drawn using the Microsoft Visual Styles API which is used in ttk for the MS-Windows native styles. This factory permits any of the Visual Styles parts to be declared as Ttk elements that can then be included in a style layout to modify the appearance of Ttk widgets.

The *className* and *partId* are required parameters and specify the Visual Styles class and part as given in the Microsoft documentation. The *stateMap* may be provided to map Ttk states to Visual Styles API states (see [State map]).

# Options

Valid *options* are:

[-padding]{.lit} [padding]{.arg}
: Specify the element's interior padding. *padding* is a list of up to four integers specifying the left, top, right and bottom padding quantities respectively. If fewer than four elements are specified, *bottom* defaults to *top*, *right* defaults to *left*, and *top* defaults to *left*. In other words, a list of three numbers specify the left, vertical, and right padding; a list of two numbers specify the horizontal and the vertical padding; a single number specifies the same padding all the way around the widget. This option may not be mixed with any other options.

[-margins]{.lit} [padding]{.arg}
: Specifies the elements exterior padding. *padding* is a list of up to four integers specifying the left, top, right and bottom padding quantities respectively. This option may not be mixed with any other options.

[-width]{.lit} [width]{.arg}
: Specifies the height for the element. If this option is set then the Visual Styles API will not be queried for the recommended size or the part. If this option is set then **-height** should also be set. The **-width** and **-height** options cannot be mixed with the **-padding** or **-margins** options.

[-height]{.lit} [height]{.arg}
: Specifies the height of the element. See the comments for **-width**.


# State map

The *stateMap* parameter is a list of ttk states and the corresponding Visual Styles API state value. This permits the element appearance to respond to changes in the widget state such as becoming active or being pressed. The list should be as described for the [ttk::style map][ttk_style] command but note that the last pair in the list should be the default state and is typically an empty list and 1. Unfortunately all the Visual Styles parts have different state values and these must be looked up either in the Microsoft documentation or more likely in the header files. The original header to use was *tmschema.h*, but in more recent versions of the Windows Development Kit this is *vssym32.h*.

If no *stateMap* parameter is given there is an implicit default map of {{} 1}

# Example

Create a correctly themed close button by changing the layout of a [ttk::button][ttk_button](n). This uses the WINDOW part WP\_SMALLCLOSEBUTTON and as documented the states CBS\_DISABLED, CBS\_HOT, CBS\_NORMAL and CBS\_PUSHED are mapped from ttk states.

```
ttk::style element create smallclose vsapi WINDOW 19 \
    {disabled 4 pressed 3 active 2 {} 1}
ttk::style layout CloseButton {CloseButton.smallclose -sticky news}
pack [ttk::button .close -style CloseButton]
```

Change the appearance of a [ttk::checkbutton][ttk_checkbutton](n) to use the Explorer pin part EBP\_HEADERPIN.

```
ttk::style element create pin vsapi EXPLORERBAR 3 {
    {pressed !selected} 3
    {active !selected} 2
    {pressed selected} 6
    {active selected} 5
    {selected} 4
    {} 1
}
ttk::style layout Explorer.Pin {Explorer.Pin.pin -sticky news}
pack [ttk::checkbutton .pin -style Explorer.Pin]
```


[ttk_button]: ttk_button.md
[ttk_checkbutton]: ttk_checkbutton.md
[ttk_style]: ttk_style.md

