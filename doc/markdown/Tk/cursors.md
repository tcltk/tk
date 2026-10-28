---
CommandName: cursors
title: cursors
ManualSection: n
Version: 8.3
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - cursor
 - option
Copyright:
 - Copyright (c) 1998-2000 Scriptics Corporation.
 - Copyright (c) 2006-2007 Daniel A. Steffen <das@users.sourceforge.net>
---

# Name

cursors - mouse cursors available in Tk

# Description

The **-cursor** widget option allows a Tk programmer to change the mouse cursor for a particular widget.  The cursor names recognized by Tk on all platforms are:

```
X_cursor
arrow
based_arrow_down
based_arrow_up
boat
bogosity
bottom_left_corner
bottom_right_corner
bottom_side
bottom_tee
box_spiral
center_ptr
circle
clock
coffee_mug
cross
cross_reverse
crosshair
diamond_cross
dot
dotbox
double_arrow
draft_large
draft_small
draped_box
exchange
fleur
gobbler
gumby
hand1
hand2
heart
icon
iron_cross
left_ptr
left_side
left_tee
leftbutton
ll_angle
lr_angle
man
middlebutton
mouse
none
pencil
pirate
plus
question_arrow
right_ptr
right_side
right_tee
rightbutton
rtl_logo
sailboat
sb_down_arrow
sb_h_double_arrow
sb_left_arrow
sb_right_arrow
sb_up_arrow
sb_v_double_arrow
shuttle
sizing
spider
spraycan
star
target
tcross
top_left_arrow
top_left_corner
top_right_corner
top_side
top_tee
trek
ul_angle
umbrella
ur_angle
watch
xterm
```

The **none** cursor can be specified to eliminate the cursor.

# Portability issues

**Windows**
: On Windows systems, the following cursors are mapped to native cursors:

    ```
    arrow
    center_ptr
    crosshair
    fleur
    ibeam
    icon
    none
    sb_h_double_arrow
    sb_v_double_arrow
    watch
    xterm
    ```

    And the following additional cursors are available:

    ```
    no
    starting
    size
    size_ne_sw
    size_ns
    size_nw_se
    size_we
    uparrow
    wait
    ```

**macOS**
: On macOS, the following cursors are mapped to native cursors:

    ```
    arrow
    top_left_arrow
    left_ptr
    cross
    crosshair
    tcross
    ibeam
    none
    xterm
    ```

    And the following additional native cursors are available:

    ```
    copyarrow
    aliasarrow
    contextualmenuarrow
    movearrow
    text
    cross-hair
    hand
    openhand
    closedhand
    fist
    pointinghand
    resize
    resizeleft
    resizeright
    resizeleftright
    resizeup
    resizedown
    resizeupdown
    resizebottomleft
    resizetopleft
    resizebottomright
    resizetopright
    notallowed
    poof
    wait
    countinguphand
    countingdownhand
    countingupanddownhand
    spinning
    help
    bucket
    cancel
    eyedrop
    eyedrop-full
    zoom-in
    zoom-out
    ```

