---
CommandName: ttk_image
title: ttk_image
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::intro(n)
 - ttk::style(n)
 - ttk_vsapi(n)
 - image(n)
 - photo(n)
Keywords:
 - style
 - theme
 - appearance
 - pixmap theme
 - image
Copyright:
 - Copyright (c) 2004 Joe English
---

# Name

ttk\_image - Define an element based on an image

# Synopsis

::: {.synopsis} :::
[ttk::style]{.cmd} [element]{.sub} [create]{.lit} [name]{.arg} [image]{.lit} [imageSpec]{.arg} [options]{.optarg}
:::

# Description

The *image* element factory creates a new element in the current theme whose visual appearance is determined by Tk images. *imageSpec* is a list of one or more elements. The first element is the default image name. The rest of the list is a sequence of *statespec / value* pairs specifying other images to use when the element is in a particular state or combination of states.

# Options

Valid *options* are:

[-border]{.lit} [padding]{.arg}
: *padding* is a list of up to four integers, specifying the left, top, right, and bottom borders, respectively. If fewer than four elements are specified, *bottom* defaults to *top*, *right* defaults to *left*, and *top* defaults to *left*. In other words, a list of three numbers specify the left, vertical, and right border; a list of two numbers specify the horizontal and the vertical border; a single number specifies the same border all the way around the element. See [Image stretching], below.

[-height]{.lit} [height]{.arg}
: Specifies a minimum height for the element. If negative, the base image's height is used as a default.

[-padding]{.lit} [padding]{.arg}
: Specifies the element's interior padding. The padding is a list of up to four length specifications *left top right bottom*. If fewer than four elements are specified, *bottom* defaults to *top*, *right* defaults to *left*, and *top* defaults to *left*. In other words, a list of three numbers specify the left, vertical, and right padding; a list of two numbers specify the horizontal and the vertical padding; a single number specifies the same padding all the way around the widget. Defaults to **-border** if not specified.

[-sticky]{.lit} [spec]{.arg}
: Specifies how the image is placed within the final parcel. *spec* contains zero or more characters "n", "s", "w", or "e".

[-width]{.lit} [width]{.arg}
: Specifies a minimum width for the element. If negative, the base image's width is used as a default.


# Image stretching

If the element's allocated parcel is larger than the image, the image will be placed in the parcel based on the **-sticky** option. If the image needs to stretch horizontally (i.e., **-sticky ew**) or vertically (**-sticky ns**), subregions of the image are replicated to fill the parcel based on the **-border** option. The **-border** divides the image into 9 regions: four fixed corners, top and left edges (which may be tiled horizontally), left and right edges (which may be tiled vertically), and the central area (which may be tiled in both directions).

An image element that is not meant to claim any space (for example when used as a background image) should use **-width 0** and **-height 0**.

# Example

```
set img1 [image create photo -file button.png]
set img2 [image create photo -file button-pressed.png]
set img3 [image create photo -file button-active.png]
ttk::style element create Button.button image \
    [list $img1  pressed $img2  active $img3] \
    -border {2 4} -sticky we
```

