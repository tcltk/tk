# path(n) -- 2D canvas widget

*   [SYNOPSIS](#SYNOPSIS)
*   [STANDARD OPTIONS](#STANDARD-OPTIONS)  
  [-background or -bg, background, Background](options.htm#M-background)   
  [-borderwidth or -bd, borderWidth, BorderWidth](options.htm#M-borderwidth)  
  [-cursor, cursor, Cursor](options.htm#M-cursor)  
  [-highlightbackground, highlightBackground, HighlightBackground](options.htm#M-highlightbackground)  
  [-highlightcolor, highlightColor, HighlightColor](options.htm#M-highlightcolor)  
  [-highlightthickness, highlightThickness, HighlightThickness](options.htm#M-highlightthickness)  
  [-insertbackground, insertBackground, Foreground](options.htm#M-insertbackground)  
  [-insertborderwidth, insertBorderWidth, BorderWidth](options.htm#M-insertborderwidth)  
  [-insertofftime, insertOffTime, OffTime](options.htm#M-insertofftime)  
  [-insertontime, insertOnTime, OnTime](options.htm#M-insertontime)  
  [-insertwidth, insertWidth, InsertWidth](options.htm#M-insertwidth)  
  [-relief, relief, Relief](options.htm#M-relief)  
  [-selectbackground, selectBackground, Foreground](options.htm#M-selectbackground)  
  [-selectborderwidth, selectBorderWidth, BorderWidth](options.htm#M-selectborderwidth)  
  [-selectforeground, selectForeground, Background](options.htm#M-selectforeground)  
  [-takefocus, takeFocus, TakeFocus](options.htm#M-takefocus)  
  [-xscrollcommand, xScrollCommand, ScrollCommand](options.htm#M-xscrollcommand)  
  [-yscrollcommand, yScrollCommand, ScrollCommand](options.htm#M-yscrollcommand)  
* [WIDGET-SPECIFIC OPTIONS](#WIDGET-SPECIFIC-OPTIONS)  
  [-closeenough, closeEnough, CloseEnough](#-closeenough)  
  [-confine, confine, Confine](#-confine)  
  [-height, height, Height](#-height)  
  [-scrollregion, scrollRegion, ScrollRegion](#-scrollregion)  
  [-state, state, State](#-state)  
  [-tagstyle, tagstyle, Tagstyle](#-tagstyle)  
  [-width, width, Width](#-width)  
  [-xscrollincrement, xScrollIncrement, ScrollIncrement](#-xscrollincrement)  
  [-yscrollincrement, yScrollIncrement, ScrollIncrement](#-yscrollincrement)  
*   [DESCRIPTION](#DESCRIPTION)  
  * [DISPLAY LIST](#DISPLAY-LIST)  
  * [ITEM IDS AND TAGS](#ITEM-IDS-AND-TAGS)  
  * [COORDINATES](#COORDINATES)  
  * [TEXT INDICES](#TEXT-INDICES)  
  * [TRANSFORMATIONS](#TRANSFORMATIONS)    
  [**path::matrix rotate**](#path::matrix-rotate) *angle ?cx? ?cy?*  
  [**path::matrix scale**](#path::matrix-scale) *sx ?sy?*  
  [**path::matrix flip**](#path::matrix-flip) *?cx? ?cy? ?fx? ?fy?*  
  [**path::matrix rotateflip**](#path::matrix-rotateflip) *?angle? ?cx? ?cy? ?fx? ?fy?*  
  [**path::matrix skewx**](#path::matrix-skewx) *?angle?*  
  [**path::matrix skewy**](#path::matrix-skewy) *?angle?*  
  [**path::matrix move**](#path::matrix-move) *dx dy*  
  [**path::matrix mult**](#path::matrix-mult) *ma mb*  
  * [STYLES](#STYLES)  
  [**path::style cget**](#path::style-cget) *token option*  
  [**path::style configure**](#path::style-configure) *token ?option? ?value option value...?*  
  [**path::style create**](#path::style-create) *?fillOptions strokeOptions?*  
  [**path::style delete**](#path::style-delete) *token*  
  [**path::style inuse**](#path::style-inuse) *token*  
  [**path::style names**](#path::style-names)  
  * [GRADIENTS](#GRADIENTS)  
  [**path::gradient cget**](#path::gradient-cget) *token option*  
  [**path::gradient configure**](#path::gradient-configure) *token ?option? ?value option value...?*  
  [**path::gradient create**](#path::gradient-create) *type ?-key value ...?*  
  [**path::gradient delete**](#path::gradient-delete) *token*  
  [**path::gradient inuse**](#path::gradient-inuse) *token*  
  [**path::gradient names**](#path::gradient-names)  
  [**path::gradient type**](#path::gradient-type) *token*  
  * [SURFACE](#SURFACE)  
  [**path::surface names**](#path::surface-names)  
  [**path::surface new**](#path::surface-new) *width height*  
*   [WIDGET COMMAND](#WIDGET-COMMAND)  
  [*pathName* **addtag**](#pathName-addtag) *tag searchSpec ?arg arg ...?*  
  [*pathName* **ancestors**](#pathName-ancestors) *tagOrId*  
  [*pathName* **bind**](#pathName-bind) *tagOrId ?sequence? ?command?*  
  [*pathName* **canvasy**](#pathName-canvasy) *screeny ?gridspacing?*  
  [*pathName* **cget**](#pathName-cget) *option*  
  [*pathName* **children**](#pathName-children) *tagOrId*  
  [*pathName* **configure**](#pathName-configure) *?option? ?value? ?option value ...?*  
  [*pathName* **coords**](#pathName-coords) *tagOrId ?x0 y0 ...?*  
  [*pathName* **create**](#pathName-create) *type x y ?x y ...? ?option value ...?*  
  [*pathName* **dchars**](#pathName-dchars) *tagOrId first ?last?*  
  [*pathName* **delete**](#pathName-delete) *?*tagOrId* *tagOrId* ...?*  
  [*pathName* **depth**](#pathName-depth) *tagOrId*  
  [*pathName* **distance**](#pathName-distance) *tagOrId x y*  
  [*pathName* **dtag**](#pathName-dtag) *tagOrId ?tagToDelete?*  
  [*pathName* **find**](#pathName-find) *searchCommand ?arg arg ...?*  
  [*pathName* **firstchild**](#pathName-firstchild) *tagOrId*  
  [*pathName* **focus**](#pathName-focus) *?*tagOrId*?*  
  [*pathName* **gettags**](#pathName-gettags) *tagOrId*  
  [*pathName* **gradient**](#pathName-gradient) *command ?options?*  
  [*pathName* **icursor**](#pathName-icursor) *tagOrId index*   
  [*pathName* **imove**](#pathName-imove) *tagOrId index x y*  
  [*pathName* **index**](#pathName-index) *tagOrId index*  
  [*pathName* **insert**](#pathName-insert) *tagOrId beforeThis string*  
  [*pathName* **itemcget**](#pathName-itemcget) *tagOrId option*  
  [*pathName* **itemconfigure**](#pathName-itemconfigure) *tagOrId ?option? ?value? ?option value ...?*  
  [*pathName* **itempdf**](#pathName-itempdf) *tagOrId ?extgsProc objProc gradProc?*  
  [*pathName* **lastchild**](#pathName-lastchild) *tagOrId*  
  [*pathName* **lower**](#pathName-lower) *tagOrId ?belowThis?*  
  [*pathName* **move**](#pathName-move) *tagOrId xAmount yAmount*  
  [*pathName* **moveto**](#pathName-moveto) *tagOrId xPos yPos*  
  [*pathName* **nextsibling**](#pathName-nextsibling) *tagOrId*  
  [*pathName* **parent**](#pathName-parent) *tagOrId*  
  [*pathName* **prevsibling**](#pathName-prevsibling) *tagOrId*  
  [*pathName* **raise**](#pathName-raise) *tagOrId ?aboveThis?*  
  [*pathName* **rchars**](#pathName-rchars) *tagOrId first last string*  
  [*pathName* **scale**](#pathName-scale) *tagOrId xOrigin yOrigin xScale yScale*  
  [*pathName* **scan**](#pathName-scan) *option args*  
  [*pathName* **select**](#pathName-select) *option ?*tagOrId* arg?*  
  [*pathName* **style**](#pathName-style) *cmd ?options?*  
  [*pathName* **type**](#pathName-type) *tagOrId*  
  [*pathName* **types**](#pathName-types)  
  [*pathName* **xview**](#pathName-xview) *?args?*  
  [*pathName* **yview**](#pathName-yview) *?args?*  
*   [ITEM TYPES](#ITEM-TYPES)  
  * [COMMON ITEM OPTIONS](#COMMON-ITEM-OPTIONS)  
  [**-fill** *color\|gradientToken*](#ITEM-fill)  
  [**-fillopacity** *value*](#ITEM-fillopacity)  
  [**-fillrule** *nonzero\|evenodd*](#ITEM-fillrule)  
  [**-stroke** *color*](#ITEM-stroke)  
  [**-strokedasharray** *dashArray*](#ITEM-strokedasharray)  
  [**-strokelinecap** *butt\|round\|square*](#ITEM-strokelinecap)  
  [**-strokelinejoin** *miter\|round\|bevel*](#ITEM-strokelinejoin)  
  [**-strokemiterlimit** *float*](#ITEM-strokemiterlimit)  
  [**-strokeopacity** *value*](#ITEM-strokeopacity)  
  [**-strokewidth** *float*](#ITEM-strokewidth)  
  [**-matrix** *{a b c d tx ty}*](#ITEM-matrix)  
  [**-parent** *tagOrId*](#ITEM-parent)  
  [**-state** *active\|disabled\|normal\|hidden*](#ITEM-state)  
  [**-style** *styleToken*](#ITEM-style)  
  [**-tags** *tagList*](#ITEM-tags)  
  [**-startarrow** *boolean*](#ITEM-startarrow)  
  [**-startarrowlength** *float*](#ITEM-startarrowlength)  
  [**-startarrowwidth** *float*](#ITEM-startarrowwidth)  
  [**-startarrowfill** *float*](#ITEM-startarrowfill)  
  [**-endarrow** *boolean*](#ITEM-endarrow)  
  [**-endarrowlength** *float*](#ITEM-endarrowlength)  
  [**-endarrowwidth** *float*](#ITEM-endarrowwidth)  
  [**-endarrowfill** *float*](#ITEM-endarrowfill)  
  * [GROUP ITEM](#GROUP-ITEM)  
  [*pathName* **create group**](#pathName-create-group) *?fillOptions strokeOptions genericOptions?*  
  * [PATH ITEMS](#PATH-ITEM)  
  [*pathName* **create path**](#pathName-create-path) *pathSpec ?fillOptions strokeOptions arrowOptions genericOptions?*  
  * [LINE ITEM](#LINE-ITEM)  
  [*pathName* **create line**](#pathName-create-line) *x1 y1 x2 y2 ?strokeOptions arrowOptions genericOptions?*  
  * [POLYLINE ITEM](#POLYLINE-ITEM)  
  [*pathName* **create polyline**](#pathName-create-polyline) *x1 y1 x2 y2 .... ?strokeOptions arrowOptions genericOptions?*  
  * [POLYGON ITEM](#POLYGON-ITEM)  
  [*pathName* **create polygon**](#pathName-create-polygon) *x1 y1 x2 y2 .... ?fillOptions strokeOptions genericOptions?*  
  * [RECT ITEM](#RECT-ITEM)  
  [*pathName* **create rect**](#pathName-create-rect) *x1 y1 x2 y2 ?-rx value? ?-ry value? ?fillOptions strokeOptions genericOptions?*  
  * [CIRCLE ITEM](#CIRCLE-ITEM)  
  [*pathName* **create circle**](#pathName-create-circle) *cx cy ?-r fillOptions strokeOptions genericOptions?*  
  * [ELLIPSE ITEM](#ELLIPSE-ITEM)  
  [*pathName* **create ellipse**](#pathName-create-ellipse) *cx cy ?-rx value? ?-ry value? ?fillOptions strokeOptions genericOptions?*  
  * [IMAGE ITEM](#IMAGE-ITEM)  
  [*pathName* **create image**](#pathName-create-image) *x y ?-image -width -height genericOptions?*  
  * [TEXT ITEM](#TEXT-ITEM)  
  [*pathName* **create text**](#pathName-create-text) *x y ?-text string? ?-textanchor start\|middle\|end\|n\|w\|s\|e\|nw\|ne\|sw\|se\|c? ?-fontfamily fontname -fontsize float? ?-fontslant normal\|italic\|oblique? ?-fontweight normal\|bold? ?fillOptions strokeOptions genericOptions? ?-filloverstroke BOOLEAN?*  
  * [WINDOW ITEM](#WINDOW-ITEM)  
  [*pathName* **create window**](#pathName-create-window) *x y ?option value ...?*  
*   [CREDITS](#CREDITS)  
*   [KEYWORDS](#KEYWORDS)  
*   [COPYRIGHT](#COPYRIGHT)  

<a name="SYNOPSIS"></a>
## SYNOPSIS

**path** *pathName ?options?*

The **path** command creates a new Tcl command whose name is *pathName*. This command may be used to invoke various operations on the widget.

<a name="STANDARD-OPTIONS"></a>
## STANDARD OPTIONS

[-background or -bg, background, Background](options.htm#M-background)  
[-borderwidth or -bd, borderWidth, BorderWidth](options.htm#M-borderwidth)  
[-cursor, cursor, Cursor](options.htm#M-cursor)  
[-highlightbackground, highlightBackground, HighlightBackground](options.htm#M-highlightbackground)  
[-highlightcolor, highlightColor, HighlightColor](options.htm#M-highlightcolor)  
[-highlightthickness, highlightThickness, HighlightThickness](options.htm#M-highlightthickness)  
[-insertbackground, insertBackground, Foreground](options.htm#M-insertbackground)  
[-insertborderwidth, insertBorderWidth, BorderWidth](options.htm#M-insertborderwidth)  
[-insertofftime, insertOffTime, OffTime](options.htm#M-insertofftime)  
[-insertontime, insertOnTime, OnTime](options.htm#M-insertontime)  
[-insertwidth, insertWidth, InsertWidth](options.htm#M-insertwidth)  
[-relief, relief, Relief](options.htm#M-relief)  
[-selectbackground, selectBackground, Foreground](options.htm#M-selectbackground)  
[-selectborderwidth, selectBorderWidth, BorderWidth](options.htm#M-selectborderwidth)  
[-selectforeground, selectForeground, Background](options.htm#M-selectforeground)  
[-takefocus, takeFocus, TakeFocus](options.htm#M-takefocus)  
[-xscrollcommand, xScrollCommand, ScrollCommand](options.htm#M-xscrollcommand)  
[-yscrollcommand, yScrollCommand, ScrollCommand](options.htm#M-yscrollcommand)  

See the [options][] manual entry for details on the standard options.

<a name="WIDGET-SPECIFIC-OPTIONS"></a>
## WIDGET-SPECIFIC OPTIONS

<a name="-closeenough"></a>
Command-Line Name: **-closeenough**  
Database Name: **closeEnough**  
Database Class: **CloseEnough**

> Specifies a floating-point value indicating how close the mouse cursor must be to an item before it is considered to be "inside" the item. Defaults to 1.0. 

<a name="-confine"></a>
Command-Line Name: **-confine**  
Database Name: **confine**  
Database Class: **Confine**

> Specifies a boolean value that indicates whether or not it should be allowable to set the canvas's view outside the region defined by the **scrollRegion** argument. Defaults to true, which means that the view will be constrained within the scroll region. 

<a name="-height"></a>
Command-Line Name: **-height**  
Database Name: **height**  
Database Class: **Height**

> Specifies a desired window height that the canvas widget should request from its geometry manager. The value may be specified in any of the forms described in the [COORDINATES][] section below. 

<a name="-scrollregion"></a>
Command-Line Name: **-scrollregion**  
Database Name: **scrollRegion**  
Database Class: **ScrollRegion**

> Specifies a list with four coordinates describing the left, top, right, and bottom coordinates of a rectangular region. This region is used for scrolling purposes and is considered to be the boundary of the information in the canvas. Each of the coordinates may be specified in any of the forms given in the [COORDINATES][] section below. 

<a name="-state"></a>
Command-Line Name: **-state**  
Database Name: **state**  
Database Class: **State**

> Modifies the default state of the canvas where state may be set to one of: **normal**, **disabled**, or **hidden**. Individual canvas objects all have their own state option which may override the default state. Many options can take separate specifications such that the appearance of the item can be different in different situations. The options that start with **active** control the appearance when the mouse pointer is over it, while the option starting with **disabled** controls the appearance when the state is disabled. Canvas items which are **disabled** will not react to canvas bindings. 

<a name="-tagstyle"></a>
Command-Line Name: **-tagstyle**  
Database Name: **tagstyle**  
Database Class: **Tagstyle**

> Define working with tags. Possible values are *expr\|exact\|glob*. Default is *expr*. TODO

<a name="-width"></a>
Command-Line Name: **-width**  
Database Name: **width**  
Database Class: **Width**

> Specifies a desired window width that the canvas widget should request from its geometry manager. The value may be specified in any of the forms described in the [COORDINATES][] section below. 

<a name="-xscrollincrement"></a>
Command-Line Name: **-xscrollincrement**  
Database Name: **xScrollIncrement**  
Database Class: **ScrollIncrement**

> Specifies an increment for horizontal scrolling, in any of the usual forms permitted for screen distances. If the value of this option is greater than zero, the horizontal view in the window will be constrained so that the canvas x coordinate at the left edge of the window is always an even multiple of **xScrollIncrement**; furthermore, the units for scrolling (e.g., the change in view when the left and right arrows of a scrollbar are selected) will also be **xScrollIncrement**. If the value of this option is less than or equal to zero, then horizontal scrolling is unconstrained. 

<a name="-yscrollincrement"></a>
Command-Line Name: **-yscrollincrement**  
Database Name: **yScrollIncrement**  
Database Class: **ScrollIncrement**

> Specifies an increment for vertical scrolling, in any of the usual forms permitted for screen distances. If the value of this option is greater than zero, the vertical view in the window will be constrained so that the canvas y coordinate at the top edge of the window is always an even multiple of **yScrollIncrement**; furthermore, the units for scrolling (e.g., the change in view when the top and bottom arrows of a scrollbar are selected) will also be **yScrollIncrement**. If the value of this option is less than or equal to zero, then vertical scrolling is unconstrained. 

<a name="DESCRIPTION"></a>
## DESCRIPTION

The **path** command creates a new window (given by the pathName argument) and makes it into a canvas widget. Additional options, described above, may be specified on the command line or in the option database to configure aspects of the canvas such as its colors and 3-D relief. The **path** command returns its pathName argument. At the time this command is invoked, there must not exist a window named pathName, but pathName's parent must exist.

Canvas widgets implement structured graphics. A canvas displays any number of items, which may be things like rectangles, circles, lines, and text. Items may be manipulated (e.g. moved or re-colored) and commands may be associated with items in much the same way that the [bind][] command allows commands to be bound to widgets. For example, a particular command may be associated with the <Button-1> event so that the command is invoked whenever button 1 is pressed with the mouse cursor over an item. This means that items in a canvas can have behaviors defined by the Tcl scripts bound to them. 

This widget implements a canvas widget which is modelled after its [SVG][] counterpart. Items are put in a tree structure with a persistent root item with id 0. All other items are descendants of this root item. The path items, described below, are by default a child of the root item, but can be configured to be a child of any group item using the -parent option.

<a name="DISPLAY-LIST"></a>
### DISPLAY LIST

The items in a canvas are ordered for purposes of display, with the first item in the display list being displayed first, followed by the next item in the list, and so on. Items later in the display list obscure those that are earlier in the display list and are sometimes referred to as being "on top" of earlier items. When a new item is created it is placed at the end of the display list, on top of everything else. Widget commands may be used to re-arrange the order of the display list.

Window items are an exception to the above rules. The underlying window systems require them always to be drawn on top of other items. In addition, the stacking order of window items is not affected by any of the canvas widget commands; you must use the Tk [raise][] command and [lower][] command instead. 

Items can be structured using groups.

A group item is merely a placeholder for other items, similar to how a frame widget is a container for other widgets. It is a building block for the tree structure. Unlike other items, and unlike frame widgets, it doesn't display anything. It has no coordinates which is an additional difference. The root item is a special group item with id 0 and tags equal to "root". The root group can be configured like other items, but its -tags and -parent options are read only. Group items define the canvas tree structure:

      0----
          1
          2
          3
          4
          5----
              6
              7
          8----
              9
             10
         11
         12

Antialiasing, if available, is controlled by the variable **path::antialias**. To switch on set it to 1.

<a name="ITEM-IDS-AND-TAGS"></a>
### ITEM IDS AND TAGS

Items in a canvas widget may be named in either of two ways: by id or by tag. Each item has a unique identifying number, which is assigned to that item when it is created. The id of an item never changes and id numbers are never re-used within the lifetime of a canvas widget. 
Each item may also have any number of tags associated with it. A tag is just a string of characters, and it may take any form except that of an integer. For example, "x123" is OK but "123" is not. The same tag may be associated with many different items. This is commonly done to group items in various interesting ways; for example, all selected items might be given the tag "selected". 

The tag **all** is implicitly associated with every item in the canvas; it may be used to invoke operations on all the items in the canvas. Note that this presently also includes the root item which can result in some unexpected behavior. In many case you can operate on the root item (0) instead. As an example, if you want to move, scale etc. all items in canvas, then do:

> <code>
    pathName move 0 x y  
</code>

The tag **current** is managed automatically by Tk; it applies to the current item, which is the topmost item whose drawn area covers the position of the mouse cursor (different item types interpret this in varying ways; see the individual item type documentation for details). If the mouse is not in the canvas widget or is not over an item, then no item has the **current** tag. 

When specifying items in canvas widget commands, if the specifier is an integer then it is assumed to refer to the single item with that id. If the specifier is not an integer, then it is assumed to refer to all of the items in the canvas that have a tag matching the specifier. The symbol *tagOrId* is used below to indicate that an argument specifies either an id that selects a single item or a tag that selects zero or more items. 

*tagOrId* may contain a logical expressions of tags by using operators: "**&&**", "**\|\|**", "**^**", "**!**", and parenthesized subexpressions. For example: 

> <code>
    .c find withtag {(a&&!b)||(!a&&b)}  
</code>

or equivalently:
 
> <code>
    .c find withtag {a^b}  
</code>

will find only those items with either "a" or "b" tags, but not both. 

Some widget commands only operate on a single item at a time; if *tagOrId* is specified in a way that names multiple items, then the normal behavior is for the command to use the first (lowest) of these items in the display list that is suitable for the command. Exceptions are noted in the widget command descriptions below. 

<a name="COORDINATES"></a>
### COORDINATES

All coordinates related to canvases are stored as floating-point numbers. Coordinates and distances are specified in screen units, which are floating-point numbers optionally followed by one of several letters. If no letter is supplied then the distance is in pixels. If the letter is **m** then the distance is in millimeters on the screen; if it is **c** then the distance is in centimeters; **i** means inches, and **p** means printers points (1/72 inch). Larger y-coordinates refer to points lower on the screen; larger x-coordinates refer to points farther to the right. Coordinates can be specified either as an even number of parameters, or as a single list parameter containing an even number of x and y coordinate values. 

The command **path::pixelalign** says how the platform graphics library draw when we specify integer coordinates. Some libraries position a one pixel wide line exactly at the pixel boundaries, and smears it out, if antialiasing, over the adjecent pixels. This can look blurred since a one pixel wide black line suddenly becomes a two pixel wide grey line.  It seems that cairo and quartz (MacOSX) do this, while gdi+ on Windows doesn't. This command just provides the info for you so you may take actions. Either you can manually position lines with odd integer widths at the center of pixels (adding 0.5), or set the **path::depixelize** equal to 1, see below.

With the boolean variable **path::depixelize** equal to 1 we try to adjust coordinates for objects with integer line widths.

There can be subtle differences compared to the original canvas. One such situation is where an option value has switched from an integer to float (double).

<a name="TEXT-INDICES"></a>
### TEXT INDICES

Text items support the notion of an index for identifying particular positions within the item. In a similar fashion, line and polygon items support index for identifying, inserting and deleting subsets of their coordinates. Indices are used for commands such as inserting or deleting a range of characters or coordinates, and setting the insertion cursor position. An index may be specified in any of a number of ways, and different types of items may support different forms for specifying indices. Text items support the following forms for an index; if you define new types of text-like items, it would be advisable to support as many of these forms as practical. Note that it is possible to refer to the character just after the last one in the text item; this is necessary for such tasks as inserting new text at the end of the item. Lines and Polygons do not support the insertion cursor and the selection. Their indices are supposed to be even always, because coordinates always appear in pairs. 

*number*

> A decimal number giving the position of the desired character within the text item. 0 refers to the first character, 1 to the next character, and so on. If indexes are odd for lines and polygons, they will be automatically decremented by one. A number less than 0 is treated as if it were zero, and a number greater than the length of the text item is treated as if it were equal to the length of the text item. For polygons, numbers less than 0 or greater then the length of the coordinate list will be adjusted by adding or subtracting the length until the result is between zero and the length, inclusive. 

**end**

> Refers to the character or coordinate just after the last one in the item (same as the number of characters or coordinates in the item). 

**insert**

> Refers to the character just before which the insertion cursor is drawn in this item. Not valid for lines and polygons. 

**sel.first**

> Refers to the first selected character in the item. If the selection is not in this item then this form is illegal. 

**sel.last**

> Refers to the last selected character in the item. If the selection is not in this item then this form is illegal. 

*@x,y*

> Refers to the character or coordinate at the point given by x and y, where x and y are specified in the coordinate system of the canvas. If x and y lie outside the coordinates covered by the text item, then they refer to the first or last character in the line that is closest to the given point. 

<a name="TRANSFORMATIONS"></a>
### TRANSFORMATIONS

Normally the origin of the canvas coordinate system is at the upper-left corner of the window containing the canvas. It is possible to adjust the origin of the canvas coordinate system relative to the origin of the window using the **xview** and **yview** widget commands; this is typically used for scrolling. Canvases do not support scaling or rotation of the canvas coordinate system relative to the window coordinate system. 
Individual items may be moved or scaled using widget commands described below, but they may not be rotated. 

Note that the default origin of the canvas's visible area is coincident with the origin for the whole window as that makes bindings using the mouse position easier to work with; you only need to use the **canvasx** and **canvasy** widget commands if you adjust the origin of the visible area. However, this also means that any focus ring (as controlled by the **-highlightthickness** option) and window border (as controlled by the **-borderwidth** option) must be taken into account before you get to the visible area of the canvas. 

Each path item has a **-matrix** option which defines the local coordinate system for that item. It is defined as a double list {a b c d tx ty} where a simple scaling is {sx 0 0 sy 0 0}, a translation {1 0 0 1 tx ty}, and a rotation around origin with an angle 'a' is {cos(a) sin(a) -sin(a) cos(a) 0 0}. The simplest way to interpret this is to design an extra coordinate system according to the matrix, and then draw the item in that system.

Inheritance works differently for the **-matrix** option than for the other options which are just overwritten. Instead any set -matrix option starting from the root, via any number of group items, to the actual item being displayed, are nested. That is, any defined matrices from the root down define a sequence of coordinate transformations.

The following functions provide some basic matrix operations such as rotation, translation etc.. All function return a matrix which can be used as value for the *-matrix* option.


<a name="path::matrix-rotate"></a>
**path::matrix rotate** *angle ?cx? ?cy?*

> Return matrix with rotation of *angle* around *cx, cy*.

<a name="path::matrix-scale"></a>
**path::matrix scale** *sx ?sy?*

> Return scaling matrix. If *sy* is not provided use *sx* for x and y direction.

<a name="path::matrix-flip"></a>
**path::matrix flip** *?cx? ?cy? ?fx? ?fy?*

> Return matrix with translation of *cx, cy* and flip with *fx* and *fy*.

<a name="path::matrix-rotateflip"></a>
**path::matrix rotateflip** *?angle? ?cx? ?cy? ?fx? ?fy?*

> Return matrix with rotation of *angle* around *cx, cy* and flip with *fx* and *fy*.

<a name="path::matrix-skewx"></a>
**path::matrix skewx** *?angle?*

> Return matrix with skew in x-direction of *angle*

<a name="path::matrix-skewy"></a>
**path::matrix skewy** *?angle?*

> Return matrix with skew in y-direction of *angle*

<a name="path::matrix-move"></a>
**path::matrix move** *dx dy*

> Return matrix with translation of *dx* in x-direction and *dy* in y-direction.

<a name="path::matrix-mult"></a>
**path::matrix mult** *ma mb*

> Return product of matrix multiplication of *ma* and *mb*.

<a name="STYLES"></a>
### STYLES

Styles are created and configured using:

> <code>
    path::style command ?options?  
</code>

<a name="path::style-cget"></a>
**path::style cget** *token option*

> Returns the value of an option.

<a name="path::style-configure"></a>
**path::style configure** *token ?option? ?value option value...?*

> Configures the object in the usual tcl way.

<a name="path::style-create"></a>
**path::style create** *?fillOptions strokeOptions?*

> Creates a style object and returns its token.

<a name="path::style-delete"></a>
**path::style delete** *token*

> Deletes the object.

<a name="path::style-inuse"></a>
**path::style inuse** *token*

> If any item is configured with the style token 1 is returned, else 0.

<a name="path::style-names"></a>
**path::style names**

> Returns all existing tokens.

The same options as for the item are supported with the exception of **-style**, **-state**, and **-tags**.

<a name="GRADIENTS"></a>
### GRADIENTS

Gradients are created and configured using:

> <code>
    path::gradient command ?options?  
</code>

<a name="path::gradient-cget"></a>
**path::gradient cget** *token option*

> Returns the value of an option.

<a name="path::gradient-configure"></a>
**path::gradient configure** *token ?option? ?value option value...?*

> Configures the object in the usual tcl way.

<a name="path::gradient-create"></a>
**path::gradient create** *type ?-key value ...?*

> Creates a linear gradient object with type any of linear or radial and returns its token.

<a name="path::gradient-delete"></a>
**path::gradient delete** *token*

> Deletes the object.

<a name="path::gradient-inuse"></a>
**path::gradient inuse** *token*

> If any item is configured with the gradient token 1 is returned, else 0.

<a name="path::gradient-names"></a>
**path::gradient names**

> Returns all existing tokens.

<a name="path::gradient-type"></a>
**path::gradient type** *token*

> Returns the type (linear\|radial) of the gradient. The options for linear gradients are:

> **-method** pad\|repeat\|reflect

> > Partial implementation; defaults to pad

> **-stops** *{stopSpec ?stopSpec...?}*

> > Where *stopSpec* is a list {offset color ?opacity?}. All offsets must be ordered and run from 0 to 1.

> **-lineartransition** *{x1 y1 x2 y2}*

> > Specifies the transtion vector relative the items bounding box. Depending on **-units** it gets interpreted differently. If **-units** is 'bbox' coordinates run from 0 to 1 and are relative the items bounding box. If **-units** is 'userspace' then they are defined in absolute coordinates but in the space of the items coordinate system. It defaults to {0 0 1 0}, left to right.

> **-matrix** *{a b c d tx ty}*

> > Sets a specific transformation for the gradient pattern only. NB: not sure about the order transforms, see **-units**.

> **-units** bbox\|userspace

> > Sets the units of the transition coordinates. See above. Defaults to 'bbox'.

The options for radial gradients are the same as for linear gradients except that the **-lineartransition** is replaced by a **-radialtransition**:

> **-radialtransition** *{cx cy ?r? ?fx fy?}*

> > Specifies the transition circles relative the items bounding box and run from 0 to 1. They default to {0.5 0.5 0.5 0.5 0.5}. *cx,cy* is the center of the end circle and *fx,fy* the center of the start point.

> **path::gradientstopstyle** *name args*

> Currently is only 'rainbow' as name supported. The function illustrate the  definition of gradients.

<a name="SURFACE"></a>
### SURFACE

In memory drawing surface.

<a name="path::surface-names"></a>
**path::surface names**

> Lists the existing surface tokens.

<a name="path::surface-new"></a>
**path::surface new** *width height*

> Creates an in memory drawing surface. Its format is platform dependent. It returns a *surfaceToken* which is a new command.

The surface token commands are:

> *surfaceToken* **copy** *imageName*

> > Copies the surface to an existing image (photo) and returns the name of
the image so you can do:

> > <code>
    set image [$token copy [image create photo]]  
</code>

> > See [Tk_PhotoPutBlock][] for how it affects the existing image.

> > The boolean variable **path::premultiplyalpha** controls how the copy action handles surfaces with the alpha component premultiplied. If 1 the copy process correctly handles any format with premultiplied alpha. This gets the highest quality for antialiasing and correct results for partial transparency. It is also slower. If 0 the alpha values are not remultiplied and the result is wrong for transparent regions, and gives poor antialiasing effects. But it is faster. The default is 1.

> *surfaceTtoken* **create** *type coords ?options?*

> > Draws the item of type to the surface. All item types except the group and the corresponding options as described above are supported, except the canvas specific **-tags** and **-state**.

> *surfaceToken* **destroy**

> > Destroys surface.

> *surfaceToken* **erase** *x y width height*

> > Erases the indicated area to transparent.

> *surfaceToken* **height**

> > Returns height.

> *surfaceToken* **width**

> > Returns width.

> Note that the surface behaves different from the canvas widget. When you have put an item there there is no way to configure it or to remove it. If you have done a mistake then you have to erase the complete surface and start all over. Better to experiment on the canvas and then reproduce your drawing to a surface when you are satisfied with it.

> NB: gdi+ seems unable to produce antialiasing effects here but there seems to be no gdi+ specific way of drawing in memory bitmaps but had to call CreateDIBSection() which is a Win32 GDI API.

<a name="WIDGET COMMAND"></a>
## WIDGET COMMAND

<a name="pathName-addtag"></a>
*pathName* **addtag** *tag searchSpec ?arg arg ...?*

> For each item that meets the constraints specified by *searchSpec* and the *args*, add *tag* to the list of tags associated with the item if it is not already present on that list. It is possible that no items will satisfy the constraints given by searchSpec and args, in which case the command has no effect. This command returns an empty string as result. *SearchSpec* and *arg'*s may take any of the following forms: 

> **above** *tagOrId*
	
> > Selects the item just after (above) the one given by *tagOrId* in the display list. If *tagOrId* denotes more than one item, then the last (topmost) of these items in the display list is used. The command is constrained to siblings.

> **all**

> > Selects all the items in the canvas. 

> **below** *tagOrId*

> > Selects the item just before (below) the one given by *tagOrId* in the display list. If *tagOrId* denotes more than one item, then the first (lowest) of these items in the display list is used. The command is constrained to siblings.

> **closest** *x y ?halo? ?start?*

> > Selects the item closest to the point given by *x* and *y*. If more than one item is at the same closest distance (e.g. two items overlap the point), then the topmost of these items (the last one in the display list) is used. If *halo* is specified, then it must be a non-negative value. Any item closer than *halo* to the point is considered to overlap it. The *start* argument may be used to step circularly through all the closest items. If *start* is specified, it names an item using a tag or id (if by tag, it selects the first item in the display list with the given tag). Instead of selecting the topmost closest item, this form will select the topmost closest item that is below start in the display list; if no such item exists, then the selection behaves as if the start argument had not been specified. 

> **enclosed** *x1 y1 x2 y2*

> > Selects all the items completely enclosed within the rectangular region given by *x1, y1, x2, and y2*. *X1* must be no greater then *x2* and *y1* must be no greater than *y2*. 

> **overlapping** *x1 y1 x2 y2*

> > Selects all the items that overlap or are enclosed within the rectangular region given by *x1, y1, x2,* and *y2*. *X1* must be no greater then *x2* and *y1* must be no greater than *y2*. 

> **withtag** *tagOrId*

> > Selects all the items given by *tagOrId*. 

<a name="pathName-ancestors"></a>
*pathName* **ancestors** *tagOrId*

> Returns a list of item id's of the first item matching *tagOrId* starting with the root item with id 0.

*pathName* **bbox** *tagOrId ?tagOrId tagOrId ...?*

> Returns a list with four elements giving an approximate bounding box for all the items named by the *tagOrId* arguments. The list has the form *x1 y1 x2 y2* such that the drawn areas of all the named elements are within the region bounded by *x1* on the left, *x2* on the right, *y1* on the top, and *y2* on the bottom. The return value may overestimate the actual bounding box by a few pixels. If no items match any of the *tagOrId* arguments or if the matching items have empty bounding boxes (i.e. they have nothing to display) then an empty string is returned. 

<a name="pathName-bind"></a>
*pathName* **bind** *tagOrId ?sequence? ?command?*

> This command associates *command* with all the items given by *tagOrId* such that whenever the event sequence given by *sequence* occurs for one of the items the command will be invoked. This widget command is similar to the [bind][] command except that it operates on items in a canvas rather than entire widgets. See the [bind][] manual entry for complete details on the syntax of sequence and the substitutions performed on command before invoking it. If all arguments are specified then a new binding is created, replacing any existing binding for the same *sequence* and *tagOrId* (if the first character of *command* is + then *command* augments an existing binding rather than replacing it). In this case the return value is an empty string. If *command* is omitted then the command returns the *command* associated with *tagOrId* and *sequence* (an error occurs if there is no such binding). If both *command* and *sequence* are omitted then the command returns a list of all the sequences for which bindings have been defined for *tagOrId*. 

> The only events for which bindings may be specified are those related to the mouse and keyboard (such as **Enter**, **Leave**, **ButtonPress**, **Motion**, and **KeyPress**) or virtual events. The handling of events in canvases uses the current item defined in [ITEM IDS AND TAGS][] above. **Enter** and **Leave** events trigger for an item when it becomes the current item or ceases to be the current item; note that these events are different than **Enter** and **Leave** events for windows. Mouse-related events are directed to the current item, if any. Keyboard-related events are directed to the focus item, if any (see the **focus** widget command below for more on this). If a virtual event is used in a binding, that binding can trigger only if the virtual event is defined by an underlying mouse-related or keyboard-related event. 

> It is possible for multiple bindings to match a particular event. This could occur, for example, if one binding is associated with the item's id and another is associated with one of the item's tags. When this occurs, all of the matching bindings are invoked. A binding associated with the **all** tag is invoked first, followed by one binding for each of the item's tags (in order), followed by a binding associated with the item's id. If there are multiple matching bindings for a single tag, then only the most specific binding is invoked. A [continue][] command in a binding script terminates that script, and a [break][] command terminates that script and skips any remaining scripts for the event, just as for the [bind][] command. 

> If bindings have been created for a canvas window using the [bind][] command, then they are invoked in addition to bindings created for the canvas's items using the **bind** widget command. The bindings for items will be invoked before any of the bindings for the window as a whole. 

<a name="pathName-canvasx"></a>
*pathName* **canvasx** *screenx ?gridspacing?*

> Given a window x-coordinate in the canvas *screenx*, this command returns the canvas x-coordinate that is displayed at that location. If *gridspacing* is specified, then the canvas coordinate is rounded to the nearest multiple of *gridspacing* units. 

<a name="pathName-canvasy"></a>
*pathName* **canvasy** *screeny ?gridspacing?*

> Given a window y-coordinate in the canvas *screeny*, this command returns the canvas y-coordinate that is displayed at that location. If *gridspacing* is specified, then the canvas coordinate is rounded to the nearest multiple of *gridspacing* units. 

<a name="pathName-cget"></a>
*pathName* **cget** *option*

> Returns the current value of the configuration option given by *option*. *Option* may have any of the values accepted by the **path** command. 

<a name="pathName-children"></a>
*pathName* **children** *tagOrId*

> Lists all children of the first item matching *tagOrId*.

<a name="pathName-configure"></a>
*pathName* **configure** *?option? ?value? ?option value ...?*

> Query or modify the configuration options of the widget. If no *option* is specified, returns a list describing all of the available options for pathName (see [Tk_ConfigureInfo][] for information on the format of this list). If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no option is specified). If one or more *option-value* pairs are specified, then the command modifies the given widget option(s) to have the given value(s); in this case the command returns an empty string. *Option* may have any of the values accepted by the **path** command. 

<a name="pathName-coords"></a>
*pathName* **coords** *tagOrId ?x0 y0 ...?*

*pathName* **coords** *tagOrId ?coordList?*

> Query or modify the coordinates that define an item. If no coordinates are specified, this command returns a list whose elements are the coordinates of the item named by *tagOrId*. If coordinates are specified, then they replace the current coordinates for the named item. If *tagOrId* refers to multiple items, then the first one in the display list is used. 

<a name="pathName-create"></a>
*pathName* **create** *type x y ?x y ...? ?option value ...?*

*pathName* **create** *type coordList ?option value ...?*

> Create a new item in pathName of type *type*. The exact format of the arguments after type depends on *type*, but usually they consist of the coordinates for one or more points, followed by specifications for zero or more item options. See the subsections on individual item types below for more on the syntax of this command. This command returns the id for the new item. 

> For further informations about useable items see section [ITEM TYPES](#ITEM-TYPES).

<a name="pathName-dchars"></a>
*pathName* **dchars** *tagOrId first ?last?*

> For each item given by *tagOrId*, delete the characters, or coordinates, in the range given by *first* and *last*, inclusive. If some of the items given by *tagOrId* do not support indexing operations then they ignore this operation. Text items interpret *first* and *last* as indices to a character, line and polygon items interpret them as indices to a coordinate (an x,y pair). Indices are described in [INDICES][] above. If *last* is omitted, it defaults to *first*. This command returns an empty string. 

<a name="pathName-delete"></a>
*pathName* **delete** *?*tagOrId* *tagOrId* ...?*

> Delete each of the items given by each *tagOrId*, and return an empty string. 

<a name="pathName-depth"></a>
*pathName* **depth** *tagOrId*

> Returns the depth in the tree hierarchy of the first item matching *tagOrId*. The root item has depth 0 and children of the root has depth 1 and so on.

<a name="pathName-distance"></a>
*pathName* **distance** *tagOrId x y*

> Returns the closest distance between the point (*x, y*) and the first item matching *tagOrId*.

<a name="pathName-dtag"></a>
*pathName* **dtag** *tagOrId ?tagToDelete?*

> For each of the items given by *tagOrId*, delete the tag given by *tagToDelete* from the list of those associated with the item. If an item does not have the tag *tagToDelete* then the item is unaffected by the command. If *tagToDelete* is omitted then it defaults to *tagOrId*. This command returns an empty string. 

<a name="pathName-find"></a>
*pathName* **find** *searchCommand ?arg arg ...?*

> This command returns a list consisting of all the items that meet the constraints specified by *searchCommand* and *arg'*s. *SearchCommand* and *args* have any of the forms accepted by the **addtag** command. The items are returned in stacking order, with the lowest item first. 

<a name="pathName-firstchild"></a>
*pathName* **firstchild** *tagOrId*

> Returns the first child item of the first item matching *tagOrId*. Applies only for groups.

<a name="pathName-focus"></a>
*pathName* **focus** *?*tagOrId*?*

> Set the keyboard focus for the canvas widget to the item given by *tagOrId*. If *tagOrId* refers to several items, then the focus is set to the first such item in the display list that supports the insertion cursor. If *tagOrId* does not refer to any items, or if none of them support the insertion cursor, then the focus is not changed. If *tagOrId* is an empty string, then the focus item is reset so that no item has the focus. If *tagOrId* is not specified then the command returns the id for the item that currently has the focus, or an empty string if no item has the focus. 

> Once the focus has been set to an item, the item will display the insertion cursor and all keyboard events will be directed to that item. The focus item within a canvas and the focus window on the screen (set with the focus command) are totally independent: a given item does not actually have the input focus unless (a) its canvas is the focus window and (b) the item is the focus item within the canvas. In most cases it is advisable to follow the focus widget command with the focus command to set the focus window to the canvas (if it was not there already). 

<a name="pathName-gettags"></a>
*pathName* **gettags** *tagOrId*

> Return a list whose elements are the tags associated with the item given by *tagOrId*. If *tagOrId* refers to more than one item, then the tags are returned from the first such item in the display list. If *tagOrId* does not refer to any items, or if the item contains no tags, then an empty string is returned. 

<a name="pathName-gradient"></a>
*pathName* **gradient** *command ?options?*

> See [GRADIENTS][] for the commands. The gradients created with this command are local to the canvas instance. Only gradients defined this way can be used.

<a name="pathName-icursor"></a>
*pathName* **icursor** *tagOrId index*

> Set the position of the insertion cursor for the item(s) given by *tagOrId* to just before the character whose position is given by *index*. If some or all of the items given by *tagOrId* do not support an insertion cursor then this command has no effect on them. See INDICES above for a description of the legal forms for *index*. Note: the insertion cursor is only displayed in an item if that item currently has the keyboard focus (see the focus widget command, above), but the cursor position may be set even when the item does not have the focus. This command returns an empty string. 

<a name="pathName-imove"></a>
*pathName* **imove** *tagOrId index x y*

> This command causes the *index*'th coordinate of each of the items indicated by *tagOrId* to be relocated to the location (*x,y*). Each item interprets *index *independently according to the rules described in [INDICES][] above. Out of the standard set of items, only line and polygon items may have their coordinates relocated this way. 

> If you apply move on a group item it will apply this to all its descendants, also to child group items in a recursive way.

<a name="pathName-index"></a>
*pathName* **index** *tagOrId index*

> This command returns a decimal string giving the numerical index within *tagOrId* corresponding to *index*. Index gives a textual description of the desired position as described in INDICES above. Text items interpret *index* as an index to a character, line and polygon items interpret it as an index to a coordinate (an x,y pair). The return value is guaranteed to lie between 0 and the number of characters, or coordinates, within the item, inclusive. If *tagOrId* refers to multiple items, then the *index* is processed in the first of these items that supports indexing operations (in display list order). 

<a name="pathName-insert"></a>
*pathName* **insert** *tagOrId beforeThis string*

> For each of the items given by *tagOrId*, if the item supports text or coordinate, insertion then string is inserted into the item's text just before the character, or coordinate, whose index is *beforeThis*. Text items interpret *beforeThis* as an index to a character, line and polygon items interpret it as an index to a coordinate (an x,y pair). For lines and polygons the string must be a valid coordinate sequence. See INDICES above for information about the forms allowed for *beforeThis*. This command returns an empty string. 

<a name="pathName-itemcget"></a>
*pathName* **itemcget** *tagOrId option*

> Returns the current value of the configuration option for the item given by *tagOrId* whose name is *option*. This command is similar to the **cget** widget command except that it applies to a particular item rather than the widget as a whole. *Option* may have any of the values accepted by the **create** widget command when the item was created. If *tagOrId* is a tag that refers to more than one item, the first (lowest) such item is used. 

<a name="pathName-itemconfigure"></a>
*pathName* **itemconfigure** *tagOrId ?option? ?value? ?option value ...?*

> This command is similar to the **configure** widget command except that it modifies item-specific options for the items given by *tagOrId* instead of modifying options for the overall canvas widget. If no *option* is specified, returns a list describing all of the available options for the first item given by *tagOrId* (see [Tk_ConfigureInfo][] for information on the format of this list). If *option* is specified with no value, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified). If one or more *option-value* pairs are specified, then the command modifies the given widget option(s) to have the given value(s) in each of the items given by *tagOrId*; in this case the command returns an empty string. The *options* and *values* are the same as those permissible in the **create** widget command when the item(s) were created; see the sections describing individual item types below for details on the legal options. 

<a name="pathName-itempdf"></a>
*pathName* **itempdf** *tagOrId ?extgsProc objProc gradProc?*

> The command return pdf code describing the given *tagOrId*. The function is optimised to work with [pdf4tcl][]. To create pdf from a **path** just call the **canvas** method from the pdf object p.e.:

    $pdf canvas $pathName -bbox [$pathName bbox] -x 0 -y 0

> See function `CanvasDoTkpathItem` in [pdf4tcl][] for details.

> If *extgsProc* is given and item has special graphic state *extgsProc* is called.

> If *objProc* is given and item has special object info *objProc* is called.

> If *gradProc* is given and the item uses a gradient the *gradProc* is called.

<a name="pathName-lastchild"></a>
*pathName* **lastchild** *tagOrId*

> Returns the last child item of the first item matching *tagOrId*. Applies only for groups.

<a name="pathName-lower"></a>
*pathName* **lower** *tagOrId ?belowThis?*

> Move all of the items given by *tagOrId* to a new position in the display list just before the item given by *belowThis*. If *tagOrId* refers to more than one item then all are moved but the relative order of the moved items will not be changed. *BelowThis* is a tag or id; if it refers to more than one item then the first (lowest) of these items in the display list is used as the destination location for the moved items. Note: this command has no effect on window items. Window items always obscure other item types, and the stacking order of window items is determined by the [raise][] command and [lower][] command, not the **raise** widget command and **lower** widget command for canvases. This command returns an empty string.

> Movement is constrained to siblings. If reference *tagOrId* not given it defaults to first/last item of the root items children. Items which are not siblings to the reference *tagOrId* are silently ignored.

<a name="pathName-move"></a>
*pathName* **move** *tagOrId xAmount yAmount*

> Move each of the items given by *tagOrId* in the canvas coordinate space by adding *xAmount* to the x-coordinate of each point associated with the item and *yAmount* to the y-coordinate of each point associated with the item. This command returns an empty string. 

<a name="pathName-moveto"></a>
*pathName* **moveto** *tagOrId xPos yPos*

Move the items given by *tagOrId* in the canvas coordinate space so that the first coordinate pair of the bottommost item with tag *tagOrId* is located at position (*xPos,yPos*). *xPos* and *yPos* may be the empty string, in which case the corresponding coordinate will be unchanged. All items matching *tagOrId* remain in the same positions relative to each other. This command returns an empty string. 

<a name="pathName-nextsibling"></a>
*pathName* **nextsibling** *tagOrId*

> Returns the next sibling item of the first item matching *tagOrId*. If *tagOrId* is the last child we return empty.

<a name="pathName-parent"></a>
*pathName* **parent** *tagOrId*

> Returns the parent item of the first item matching *tagOrId*. This command works for all items, also for the standard ones. It is therefore better to use this than `cget id -parent` which is only supported for the new path items.

<a name="pathName-prevsibling"></a>
*pathName* **prevsibling** *tagOrId*

> Returns the previous sibling item of the first item matching *tagOrId*. If *tagOrId* is the first child we return empty.

<a name="pathName-raise"></a>
*pathName* **raise** *tagOrId ?aboveThis?*

> Move all of the items given by *tagOrId* to a new position in the display list just after the item given by aboveThis. If *tagOrId* refers to more than one item then all are moved but the relative order of the moved items will not be changed. AboveThis is a tag or id; if it refers to more than one item then the last (topmost) of these items in the display list is used as the destination location for the moved items. This command returns an empty string. 

> Movement is constrained to siblings. If reference *tagOrId* not given it defaults to first/last item of the root items children. Items which are not siblings to the reference *tagOrId* are silently ignored.

> Note: this command has no effect on window items. Window items always obscure other item types, and the stacking order of window items is determined by the raise command and lower command, not the raise widget command and lower widget command for canvases. 

<a name="pathName-rchars"></a>
*pathName* **rchars** *tagOrId first last string*

> This command causes the text or coordinates between *first* and *last* for each of the items indicated by *tagOrId* to be replaced by string. Each item interprets *first* and *last* independently according to the rules described in [INDICES][] above. Out of the standard set of items, text items support this operation by altering their text as directed, and line and polygon items support this operation by altering their coordinate list (in which case string should be a list of coordinates to use as a replacement). The other items ignore this operation. 

<a name="pathName-scale"></a>
*pathName* **scale** *tagOrId xOrigin yOrigin xScale yScale*

> Rescale the coordinates of all of the items given by *tagOrId* in canvas coordinate space. *XOrigin* and *yOrigin* identify the origin for the scaling operation and *xScale* and *yScale* identify the scale factors for x- and y-coordinates, respectively (a scale factor of 1.0 implies no change to that coordinate). For each of the points defining each item, the x-coordinate is adjusted to change the distance from *xOrigin* by a factor of *xScale*. Similarly, each y-coordinate is adjusted to change the distance from *yOrigin* by a factor of *yScale*. This command returns an empty string. 

> Note that some items have only a single pair of coordinates (e.g. windows) and so scaling of them by this command can only move them around.

> If you apply scale on a group item it will apply this to all its descendants, also to child group items in a recursive way. 

<a name="pathName-scan"></a>
*pathName* **scan** *option args*

> This command is used to implement scanning on canvases. It has two forms, depending on option: 

> *pathName* **scan mark** *x y*

> > Records *x* and *y* and the canvas's current view; used in conjunction with later scan dragto commands. Typically this command is associated with a mouse button press in the widget and *x* and *y* are the coordinates of the mouse. It returns an empty string. 

> *pathName* **scan dragto** *x y ?gain?*

> > This command computes the difference between its *x* and *y* arguments (which are typically mouse coordinates) and the x and y arguments to the last scan mark command for the widget. It then adjusts the view by *gain* times the difference in coordinates, where *gain* defaults to 10. This command is typically associated with mouse motion events in the widget, to produce the effect of dragging the canvas at high speed through its window. The return value is an empty string. 

<a name="pathName-select"></a>
*pathName* **select** *option ?*tagOrId* arg?*

> Manipulates the selection in one of several ways, depending on *option*. The command may take any of the forms described below. In all of the descriptions below, *tagOrId* must refer to an item that supports indexing and selection; if it refers to multiple items then the first of these that supports indexing and the selection is used. Index gives a textual description of a position within *tagOrId*, as described in [INDICES][] above. 

> *pathName* **select adjust** *tagOrId index*

> > Locate the end of the selection in *tagOrId* nearest to the character given by *index*, and adjust that end of the selection to be at *index* (i.e. including but not going beyond index). The other end of the selection is made the anchor point for future select to commands. If the selection is not currently in *tagOrId* then this command behaves the same as the select to widget command. Returns an empty string. 

> *pathName* **select clear**

> > Clear the selection if it is in this widget. If the selection is not in this widget then the command has no effect. Returns an empty string. 

> *pathName* **select from** *tagOrId index*

> > Set the selection anchor point for the widget to be just before the character given by *index* in the item given by *tagOrId*. This command does not change the selection; it just sets the fixed end of the selection for future select to commands. Returns an empty string. 

>  *pathName* **select item**

> > Returns the id of the selected item, if the selection is in an item in this canvas. If the selection is not in this canvas then an empty string is returned. 

> *pathName* **select to** *tagOrId index*

> > Set the selection to consist of those characters of *tagOrId* between the selection anchor point and *index*. The new selection will include the character given by *index*; it will include the character given by the anchor point only if *index* is greater than or equal to the anchor point. The anchor point is determined by the most recent select adjust or select from command for this widget. If the selection anchor point for the widget is not currently in *tagOrId*, then it is set to the same character given by *index*. Returns an empty string. 

<a name="pathName-style"></a>
*pathName* **style** *cmd ?options?*

> See [STYLES](#STYLES) for the commands. The styles created with this command are local to the canvas instance. Only styles defined this way can be used.

> The styleToken is a style created with 'pathName style create'. It's options take precedence over any other options set directly. This is how SVG works (bad?). Currently all a style's options ever set are recorded in a cumulative way using a mask. Even if an option is set to its default it takes precedence over an items option.

<a name="pathName-type"></a>
*pathName* **type** *tagOrId*

> Returns the type of the item given by *tagOrId*, such as rectangle or text. If *tagOrId* refers to more than one item, then the type of the first item in the display list is returned. If *tagOrId* does not refer to any items at all then an empty string is returned. 

<a name="pathName-types"></a>
*pathName* **types**

> List all item types defined in canvas.

<a name=*pathName-xview"></a>
*pathName* **xview** *?args?*

> This command is used to query and change the horizontal position of the information displayed in the canvas's window. It can take any of the following forms: 

> *pathName* **xview**

> > Returns a list containing two elements. Each element is a real fraction between 0 and 1; together they describe the horizontal span that is visible in the window. For example, if the first element is .2 and the second element is .6, 20% of the canvas's area (as defined by the **-scrollregion** option) is off-screen to the left, the middle 40% is visible in the window, and 40% of the canvas is off-screen to the right. These are the same values passed to scrollbars via the **-xscrollcommand** option. 

> *pathName* **xview moveto** *fraction*

> > Adjusts the view in the window so that *fraction* of the total width of the canvas is off-screen to the left. *Fraction* must be a fraction between 0 and 1. 

> *pathName* **xview scroll** *number what*

> > This command shifts the view in the window left or right according to *number* and *what*. *Number* must be an integer. *What* must be either **units** or **pages** or an abbreviation of one of these. If *what* is units, the view adjusts left or right in **units** of the **xScrollIncrement** option, if it is greater than zero, or in units of one-tenth the window's width otherwise. If what is **pages** then the view adjusts in units of nine-tenths the window's width. If *number* is negative then information farther to the left becomes visible; if it is positive then information farther to the right becomes visible. 

<a name="pathName-yview"></a>
*pathName* **yview** *?args?*

> This command is used to query and change the vertical position of the information displayed in the canvas's window. It can take any of the following forms: 

> *pathName* **yview**

> > Returns a list containing two elements. Each element is a real fraction between 0 and 1; together they describe the vertical span that is visible in the window. For example, if the first element is .6 and the second element is 1.0, the lowest 40% of the canvas's area (as defined by the **-scrollregion** option) is visible in the window. These are the same values passed to scrollbars via the **-yscrollcommand** option. 

> *pathName* **yview moveto** *fraction*

> > Adjusts the view in the window so that *fraction* of the canvas's area is off-screen to the top. *Fraction* is a fraction between 0 and 1. 

> *pathName* **yview scroll** *number what*

> > This command adjusts the view in the window up or down according to *number* and *what*. *Number* must be an integer. *What* must be either **units** or **pages**. If *what* is **units**, the view adjusts up or down in units of the **yScrollIncrement** option, if it is greater than zero, or in units of one-tenth the window's height otherwise. If what is pages then the view adjusts in units of nine-tenths the window's height. If *number* is negative then higher information becomes visible; if it is positive then lower information becomes visible. 

<a name="ITEM-TYPES"></a>
## ITEM TYPES

The sections below describe the various types of items supported by canvas widgets. Each item type is characterized by two things: first, the form of the **create** command used to create instances of the type; and second, a set of configuration options for items of that type, which may be used in the **create** and **itemconfigure** widget commands. Most items do not support indexing or selection or the commands related to them, such as **index** and **insert**. Where items do support these facilities, it is noted explicitly in the descriptions below. At present, text, line and polygon items provide this support. For lines and polygons the indexing facility is used to manipulate the coordinates of the item. 

<a name="COMMON-ITEM-OPTIONS"></a>
### COMMON ITEM OPTIONS

The options can be separated into a few groups depending on the nature of an item for which they apply. Not all are implemented.

Arrow options accepted by line, polyline and path objects. Arrows are not
implemented on surfaces (see path::surface).

Options set in a group are inherited by its children but they never override
options explicitly set in children. This also applies to group items configured
with a -style.

    .c create group ?fillOptions strokeOptions genericOptions?

<a name="ITEM-fill"></a>
**-fill** *color\|gradientToken*

> This is either a usual tk color or the name of a gradient.

<a name="ITEM-fillopacity"></a>
**-fillopacity** *value*

> The given *value* is a float value between 0.0 and 1.0

<a name="ITEM-fillrule"></a>
**-fillrule** *nonzero\|evenodd*

<a name="ITEM-stroke"></a>
**-stroke** *color*

<a name="ITEM-strokedasharray"></a>
**-strokedasharray** *dashArray*

> The *dashArray* is a list of integers. Each element represents the number of pixels of a line segment. Only the odd segments are drawn using the "outline" color. The other segments are drawn transparent. 

<a name="ITEM-strokelinecap"></a>
**-strokelinecap** *butt\|round\|square*

<a name="ITEM-strokelinejoin"></a>
**-strokelinejoin** *miter\|round\|bevel*

<a name="ITEM-strokemiterlimit"></a>
**-strokemiterlimit** *float*

<a name="ITEM-strokeopacity"></a>
**-strokeopacity** *value*

> The given *value* is a float value between 0.0 and 1.0

<a name="ITEM-strokewidth"></a>
**-strokewidth** *float*

<a name="ITEM-matrix"></a>
**-matrix** *{a b c d tx ty}*

<a name="ITEM-parent"></a>
**-parent** *tagOrId*

<a name="ITEM-state"></a>
**-state** *active\|disabled\|normal\|hidden*

<a name="ITEM-style"></a>
**-style** *styleToken*

<a name="ITEM-tags"></a>
**-tags** *tagList*

<a name="ITEM-startarrow"></a>
**-startarrow** *boolean*

> Arrowhead on/off; the default value is off.

<a name="ITEM-startarrowlength"></a>
**-startarrowlength** *float*

> Length of the arrowhead.
  * 0.0 is special and draws '|-----'
  * negative values draw '>----'

<a name="ITEM-startarrowwidth"></a>
**-startarrowwidth** *float*

> Arrow width; must be positive.

<a name="ITEM-startarrowfill"></a>
**-startarrowfill** *float*

> Relative to startarrowlength; for example:
  * 0.0: do not fill arrowhead, arrowhead will be two lines
  * 1.0: '<|-------'
  * 2.0: '<>-------'

<a name="ITEM-endarrow"></a>
**-endarrow** *boolean*

> See **-startarrow**.

<a name="ITEM-endarrowlength"></a>
**-endarrowlength** *float*

> See **-startarrowlength**.

<a name="ITEM-endarrowwidth"></a>
**-endarrowwidth** *float*

> See **-startarrowwidth**.

<a name="ITEM-endarrowfill"></a>
**-endarrowfill** *float*

> See **-startarrowfill**.

<a name="GROUP-ITEM"></a>
### GROUP ITEM

<a name="pathName-create-group"></a>
*pathName* **create group** *?fillOptions strokeOptions genericOptions?*

A group item is merely a placeholder for other items, similar to how a
frame widget is a container for other widgets. It is a building block for
the tree structure. Unlike other items, and unlike frame widgets, it
doesn't display anything. It has no coordinates which is an additional
difference. The root item is a special group item with id 0 and tags
equal to "root". The root group can be configured like other items, but
its -tags and -parent options are read only.
Options set in a group are inherited by its children but they never override
options explicitly set in children. This also applies to group items configured
with a -style.

<a name="PATH-ITEM"></a>
### PATH ITEM

<a name="pathName-create-path"></a>
*pathName* **create path** *pathSpec ?fillOptions strokeOptions arrowOptions genericOptions?*

The path specification must be a single list and not concateneted with the rest of the command:

    pathName create path {M 10 10 h 10 v 10 h -10 z} -fill blue
    pathName create path M 10 10 h 10 v 10 h -10 z -fill blue    ;# Error

Furthermore, coordinates are pixel coordinates and nothing else.
SVG: It implements the complete syntax of the path elements d attribute with
one major difference: all separators must be whitespace, no commas, no
implicit assumptions; all instructions and numbers must form a tcl list.

All path specifications are normalized initially to the fundamental atoms
M, L, A, Q, and C, all upper case. When you use the canvas 'coords' command
it is the normalized path spec that is returned. Bad?

Visualize this as a pen which always has a current coordinate after
the first M. Coordinates are floats:

* M x y

> Put the pen on the paper at specified coordinate. Must be the first atom but can appear any time later. The pen doesn't draw anything when moved to this point.

* L x y

> Draw a straight line to the given coordinate. 

* H x

> Draw a horizontal line to the given x coordinate.

* V y

> Draw a vertical line to the given y coordinate.

* A rx ry phi largeArc sweep x y

> Draw an elliptical arc from the current point to (x, y). The points are on an ellipse with x-radius rx and y-radius ry. The ellipse is rotated by phi degrees. If the arc is less than 180 degrees, largeArc is zero, else it is one. If the arc is to be drawn in cw direction, sweep is one, and zero for the ccw direction.

> NB: the start and end points may not coincide else the result  is undefined. If you want to make a circle just do two 180 degree arcs.

* Q x1 y1 x y

> Draw a qadratic Bezier curve from the current point to (x, y) using control point (x1, y1).

* T x y

> Draw a qadratic Bezier curve from the current point to (x, y) The control point will be the reflection of the previous Q atoms control point. This makes smooth paths.

* C x1 y1 x2 y2 x y

> Draw a cubic Bezier curve from the current point to (x, y) using control points (x1, y1) and (x2, y2).

* S x2 y2 x y

> Draw a cubic Bezier curve from the current point to (x, y), using (x2, y2) as the control point for this new endpoint. The first control point will be the reflection of the previous C atoms ending control point. This makes smooth paths.

* Z
   
> Close path by drawing from the current point to the preceeding M point.

You may use lower case characters for all atoms which then means that all
coordinates, where relevant, are interpreted as coordinates relative the
current point.

Helper function for making path definitions:

> **path::path ellipse** *x y rx ry*

> > The function return a path definition of an ellipse with a middle point at *x* and *y* and a radius in x-disrection of *rx* and a radius in y-direction of *ry*.

> **path::path circle** *x y r*

> > The function return a path definition of an circle with a middle point at *x* and *y* and a radius of *r*.

<a name="LINE-ITEM"></a>
### LINE ITEM

<a name="pathName-create-line"></a>
*pathName* **create line** *x1 y1 x2 y2 ?strokeOptions arrowOptions genericOptions?*

Makes a single-segment straight line.

<a name="POLYLINE-ITEM"></a>
### POLYLINE ITEM

<a name="pathName-create-polyline"></a>
*pathName* **create polyline** *x1 y1 x2 y2 .... ?strokeOptions arrowOptions genericOptions?*

Makes a multi-segment line with open ends.

<a name="POLYGON-ITEM"></a>
### POLYGON ITEM

<a name="pathName-create-polygon"></a>
*pathName* **create polygon** *x1 y1 x2 y2 .... ?fillOptions strokeOptions genericOptions?*

Makes a closed polygon.

<a name="RECT-ITEM"></a>
### RECT ITEM

<a name="pathName-create-rect"></a>
*pathName* **create rect** *x1 y1 x2 y2 ?-rx value? ?-ry value? ?fillOptions strokeOptions genericOptions?*

This is a rectangle item with optionally rounded corners.
Item specific options:

**-rx** *value*

> Corner x-radius, or if -ry not given it sets the uniform radius.

**-ry** *value*

> Corner y-radius

<a name="CIRCLE-ITEM"></a>
### CIRCLE ITEM

<a name="pathName-create-circle"></a>
*pathName* **create circle** *cx cy ?-r fillOptions strokeOptions genericOptions?*

A plain circle item. Item specific options:

**-r** *value*

> Its radius; defaults to zero

<a name="ELLIPSE-ITEM"></a>
### ELLIPSE ITEM

<a name="pathName-create-ellipse"></a>
*pathName* **create ellipse** *cx cy ?-rx value? ?-ry value? ?fillOptions strokeOptions genericOptions?*

An ellipse item. Item specific options:

**-rx** *value*

> Its x-radius

**-ry** *value*

> Its y-radius


<a name="IMAGE-ITEM"></a>
### IMAGE ITEM

<a name="pathName-create-image"></a>
*pathName* **create image** *x y ?-image -width -height genericOptions?*

This displays an image in the canvas anchored nw. If -width or -height is
nonzero then the image is scaled to this size prior to any affine transform.

image extra options:

**-anchor** *n\|w\|s\|e\|nw\|ne\|sw\|se\|c*

> Default value is nw

**-tintcolor** *color*

> Tint color; the default value is "" which means no tinting

**-tintamount** *value*

*Value* is amount for tinting between 0. and 1.

**-interpolation** *mode*

> Image interpolation *mode* is one of **none, fast** or **best**

**-srcregion** *{x1 y1 x2 y2}*

> Shows only the specified region of image; if x2 or y2 are larger than the image bounds, then the image will be repeated (tiling)

These options are not implemented on surfaces (see path::surface).

<a name="TEXT-ITEM"></a>
### TEXT ITEM

<a name="pathName-create-text"></a>
*pathName* **create text** *x y ?-text string? ?-textanchor start\|middle\|end\|n\|w\|s\|e\|nw\|ne\|sw\|se\|c? ?-fontfamily fontname -fontsize float? ?-fontslant normal\|italic\|oblique? ?-fontweight normal\|bold? ?fillOptions strokeOptions genericOptions? ?-filloverstroke BOOLEAN?*

Displays text as expected. Note that the x coordinate marks the baseline
of the text. Gradient fills are unsupported so far. Especially the font
handling and settings will likely be developed further.
Editing not implemented. The default font family and size is platform dependent.

text extra options:

**-textanchor** *n\|w\|s\|e\|nw\|ne\|sw\|se\|c\|start\|middle\|end*

> Textanchor extended with points of compass.

**-fontslant** *normal\|italic\|oblique*

> Default value is normal

**-fontweight** *normal\|bold*

> Default value is normal

**-filloverstroke** *boolean*

> Fill drawn over the stroke; default value is false

These options are not implemented on surface items (path::surface), except for

**-textanchor** *start\|middle\|end*

<a name="WINDOW-ITEM"></a>
### WINDOW ITEM

Items of type window cause a particular window to be displayed at a given position on the canvas. Window items are created with widget commands of the following form: 

<a name="pathName-create-window"></a>
*pathName* **create window** *x y ?option value ...?*

*pathName* **create window** *coordList ?option value ...?*

The arguments x and y or coordList (which must have two elements) specify the coordinates of a point used to position the window on the display, as controlled by the -anchor option. After the coordinates there may be any number of option-value pairs, each of which sets one of the configuration options for the item. These same option-value pairs may be used in itemconfigure widget commands to change the item's configuration. Theoretically, a window item becomes the current item when the mouse pointer is over any part of its bounding box, but in practice this typically does not happen because the mouse pointer ceases to be over the canvas at that point. 

The following standard options are supported by window items: 

> **-anchor**

> **-state**

> **-tags**

The following extra options are supported for window items: 

**-height** *pixels*

> Specifies the height to assign to the item's window. Pixels may have any of the forms described in the [COORDINATES][] section above. If this option is not specified, or if it is specified as zero, then the window is given whatever height it requests internally. 

**-width** *pixels*

> Specifies the width to assign to the item's window. Pixels may have any of the forms described in the [COORDINATES][] section above. If this option is not specified, or if it is specified as zero, then the window is given whatever width it requests internally. 

**-window** *pathName*

> Specifies the window to associate with this item. The window specified by pathName must either be a child of the canvas widget or a child of some ancestor of the canvas widget. PathName may not refer to a top-level window. 

Note: due to restrictions in the ways that windows are managed, it is not possible to draw other graphical items (such as lines and images) on top of window items. A window item always obscures any graphics that overlap it, regardless of their order in the display list. Also note that window items, unlike other canvas items, are not clipped for display by their containing canvas's border, and are instead clipped by the parent widget of the window specified by the -window option; when the parent widget is the canvas, this means that the window item can overlap the canvas's border. 

<a name="BINDINGS"></a>
## BINDINGS

In the current implementation, new canvases are not given any default behavior: you will have to execute explicit Tcl commands to give the canvas its behavior. 

<a name="CREDITS"></a>
## CREDITS

Tk's canvas widget is a blatant ripoff of ideas from Joel Bartlett's ezd program. Ezd provides structured graphics in a Scheme environment and preceded canvases by a year or two. Its simple mechanisms for placing and animating graphical objects inspired the functions of canvases.

[Tkpath][] was originally developed by Matts Bengtsson.

User visisble changes to the original [Tkpath][] code are:

- changed widget name in **path** and use of namespace **::path**
- -matrix is now a flat list
- tk canvas items removed, only exception is **window** item
- changed item names to match [SVG][] names:
  - **pimage** => **image**
  - **pline** => **line**
  - **ppolygon** => **polygon**
  - **prect** => **rect**
  - **ptext** => **text**
- no unique abbreviations of widget commands

<a name="KEYWORDS"></a>
## KEYWORDS

canvas, svg, widget

<a name="COPYRIGHT"></a>
## COPYRIGHT

&copy; 2005-2008 Mats Bengtsson

&copy; 2015- Christian Werner <Christian.Werner@t-online.de>

&copy; 2016- René Zaumseil <r.zaumseil@freenet.de>

BSD style license.

<a name="SEE-ALSO"></a>
## SEE ALSO

[bind][], [font][], [image][], [scrollbar][], [pdf4tcl][], [SVG][], [Cairo][]

<a name="KEYWORDS"></a>
## KEYWORDS

canvas, pdf, svg, widget

[bind]: bind.htm
[break]: break.htm
[continue]: continue.htm
[font]: font.htm
[image]: image.htm
[options]: options.htm
[raise]: raise.htm
[scrollbar]: scrollbar.htm
[lower]: lower.htm
[Tk_ConfigureInfo]: ConfigWidg.htm
[Tkpath]: <https://sourceforge.net/projects/tclbitprint/>
[pdf4tcl]: <https://sourceforge.net/projects/pdf4tcl/>
[SVG]: <http://www.w3.org/TR/SVG11/>
[Cairo]: <http://cairographics.org>


