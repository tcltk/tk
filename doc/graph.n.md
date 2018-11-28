# graph(n) -- 2D graph for plotting X-Y coordinate data

*   [NAME]((#NAME)
*   [SYNOPSIS](#SYNOPSIS)
*   [DESCRIPTION](#DESCRIPTION)
*   [INTRODUCTION](#INTRODUCTION)
*   [SYNTAX](#SYNTAX)
*   [GRAPH OPERATIONS](#GRAPH-OPERATIONS)
  * [pathName axis](#pathName-axis) *operation ?arg?...*
  * [pathName binding](#pathName-binding) *?crosshairs? ?findelement? ?legend? ?zoom?*
  * [pathName bar](#pathName-bar) *elemName ?option value?...*
  * [pathName cget](#pathName-cget) *option*
  * [pathName configure](#pathName-configure) *?option value?...*
  * [pathName crosshairs](#pathName-crosshairs) *operation ?arg?*
  * [pathName element](#pathName-element) *operation ?arg?...*
  * [pathName extents](#pathName-extents) *item*
  * [pathName grid](#pathName-grid) *operation ?arg?...*
  * [pathName invtransform](#pathName-invtransform) *winX winY*
  * [pathName inside](#pathName-inside) *x y*
  * [pathName legend](#pathName-legend) *operation ?arg?...*
  * [pathName line](#pathName-line) *operation arg...*
  * [pathName marker](#pathName-marker) *operation ?arg?...*
  * [pathName postscript](#pathName-postscript) *operation ?arg?...*
  * [pathName snap](#pathName-snap) *?switches? outputName*
  * [pathName transform](#pathName-transform) *x y*
  * [pathName xaxis](#pathName-xaxis) *operation ?arg?...*
  * [pathName x2axis](#pathName-x2axis) *operation ?arg?...*
  * [pathName yaxis](#pathName-yaxis) *operation ?arg?...*
  * [pathName y2axis](#pathName-y2axis) *operation ?arg?...*
* [AXIS COMPONENT](#AXIS-COMPONENT)
  * [pathName axis bind](#pathName-axis-bind) *tagName ?sequence? ?command?*
  * [pathName axis cget](#pathName-axis-cget) *axisName option*
  * [pathName axis configure](#pathName-axis-configure) *axisName ?axisName?... ?option value?...*
  * [pathName axis create](#pathName-axis-create) *axisName ?option value?...*
  * [pathName axis delete](#pathName-axis-delete) *?axisName?...*
  * [pathName axis invtransform](#pathName-axis-invtransform) *axisName value*
  * [pathName axis limits](#pathName-axis-limits) *axisName*
  * [pathName axis names](#pathName-axis-names) *?pattern?...*
  * [pathName axis transform](#pathName-axis-transform) *axisName value*
  * [pathName axis view](#pathName-axis-view) *axisName*
* [CROSSHAIRS COMPONENT](#)
  * [pathName crosshairs cget](#pathName-crosshairs-cget) *option*
  * [pathName crosshairs configure](#pathName-crosshairs-configure) *?option value?...*
  * [pathName crosshairs off](#pathName-crosshairs-off)
  * [pathName crosshairs on](#pathName-crosshairs-on)
  * [pathName crosshairs toggle](#pathName-crosshairs-toggle)
* [ELEMENT COMPONENT](#ELEMENT-COMPONENT)
  * [pathName element activate](#pathName-element-activate) *elemName ?index?...*
  * [pathName element bind](#pathName-element-bind) *tagName ?sequence? ?command?*
  * [pathName element cget](#pathName-element-cget) *elemName option*
  * [pathName element closest](#pathName-element-closest) *x y varName ?option value?... ?elemName?...*
  * [pathName element configure](#pathName-element-configure) *elemName ?elemName... ?option value?...*
  * [pathName element create](#pathName-element-create) *elemName ?option value?...*
  * [pathName element deactivate](#pathName-element-deactivate) *elemName ?elemName?...*
  * [pathName element delete](#pathName-element-delete) *?elemName?...*
  * [pathName element exists](#pathName-element-exists) *elemName*
  * [pathName element names](#pathName-element-names) *?pattern?...*
  * [pathName element show](#pathName-element-show) *?nameList?*
  * [pathName element type](#pathName-element-type) *elemName*
* [GRID COMPONENT](#GRID-COMPONENT)
  * [pathName grid cget](#pathName-grid-cget) *option*
  * [pathName grid configure](#pathName-grid-configure) *?option value?...*
  * [pathName grid off](#pathName-grid-off)
  * [pathName grid on](#pathName-grid-on)
  * [pathName grid toggle](#pathName-grid-toggle)
* [LEGEND COMPONENT](#LEGEND-COMPONENT)
  * [pathName legend activate](#pathName-legend-activate) *pattern...*
  * [pathName legend bind](#pathName-legend-bind) *tagName ?sequence? ?command?*
  * [pathName legend cget](#pathName-legend-cget) *option*
  * [pathName legend configure](#pathName-legend-configure) *?option value?...*
  * [pathName legend deactivate](#pathName-legend-deactivate) *pattern...*
  * [pathName legend get](#pathName-legend-get) *pos*
* [PEN COMPONENT](#PEN-COMPONENT)
  * [pathName pen cget](#pathName-pen-cget) *penName option*
  * [pathName pen configure](#pathName-pen-configure) *penName ?penName... ?option value?...*
  * [pathName pen create](#pathName-pen-create) *penName ?option value?...*
  * [pathName pen delete](#pathName-pen-delete) *?penName?...*
  * [pathName pen names](#pathName-pen-names) *?pattern?...*
* [POSTSCRIPT COMPONENT](#POSTSCRIPT-COMPONENT)
  * [pathName postscript cget](#pathName-postscript-cget) *option*
  * [pathName postscript configure](#pathName-postscript-configure) *?option value?...*
  * [pathName postscript output](#pathName-postscript-output) *?fileName? ?option value?...*
* [MARKER COMPONENT](#MARKER-COMPONENT)
  * [pathName marker after](#pathName-marker-after) *markerId ?afterId?*
  * [pathName marker before](#pathName-marker-before) *markerId ?beforeId?*
  * [pathName marker bind](#pathName-marker-bind) *tagName ?sequence? ?command?*
  * [pathName marker cget](#pathName-marker-cget) *option*
  * [pathName marker configure](#pathName-marker-configure) *markerId ?option value?...*
  * [pathName marker create](#pathName-marker-create) *type ?option value?...*
  * [pathName marker delete](#pathName-marker-delete) *?name?...*
  * [pathName marker exists](#pathName-marker-exists) *markerId*
  * [pathName marker names](#pathName-marker-names) *?pattern?*
  * [pathName marker type](#pathName-marker-type) *markerId*
  * [BITMAP MARKERS](#BITMAP-MARKERS)
  * [pathName marker create bitmap](#pathName-marker-create-bitmap) *?option value?...*
  * [IMAGE MARKERS](#IMAGE-MARKERS)
  * [pathName marker create image](#pathName-marker-create-image) *?option value?...*
  * [LINE MARKERS](#LINE-MARKERS)
  * [pathName marker create line](#pathName-marker-create-line) *?option value?...*
  * [POLYGON MARKERS](#POLYGON-MARKERS)
  * [pathName marker create polygon](#pathName-marker-create-polygon) *?option value?...*
  * [TEXT MARKERS](#TEXT-MARKERS)
  * [pathName marker create text](#pathName-marker-create-text) *?option value?...*
  * [WINDOW MARKERS](#WINDOW-MARKERS)
  * [pathName marker create window](#pathName-marker-create-window) *?option value?...*
*   [GRAPH COMPONENT BINDINGS](#GRAPH-COMPONENT-BINDINGS)
*   [C LANGUAGE API](#[C-LANGUAGE-API)
*   [SPEED TIPS](#SPEED-TIPS)
*   [LIMITATIONS](#LIMITATIONS)
*   [EXAMPLE](#EXAMPLE)
*   [KEYWORDS](#KEYWORDS)

<a name="NAME"></a>
## NAME 

graph - 2D graph for plotting X-Y coordinate data.

<a name="SYNOPSIS"></a>
## SYNOPSIS 

**graph** *pathName ?option value?...*

<a name="DESCRIPTION"></a>
## DESCRIPTION 

The graph command creates a graph for plotting two-dimensional data (X-Y coordinates). It has many configurable components: coordinate axes, elements, legend, grid lines, cross hairs, etc. They allow you to customize the look and feel of the graph.

<a name="INTRODUCTION"></a>
## INTRODUCTION 

The graph command creates a new window for plotting two-dimensional data (X-Y coordinates). Data points are plotted in a rectangular area displayed in the center of the new window. This is the plotting area. The coordinate axes are drawn in the margins around the plotting area. By default, the legend is displayed in the right margin. The title is displayed in top margin.

The graph widget is composed of several components: coordinate axes, data elements, legend, grid, cross hairs, pens, postscript, and annotation markers.

**axis**
    The graph has four standard axes (x, x2, y, and y2), but you can create and display any number of axes. Axes control what region of data is displayed and how the data is scaled. Each axis consists of the axis line, title, major and minor ticks, and tick labels. Tick labels display the value at each major tick.

**crosshairs**
    Cross hairs are used to position the mouse pointer relative to the X and Y coordinate axes. Two perpendicular lines, intersecting at the current location of the mouse, extend across the plotting area to the coordinate axes.

**element**
    An element represents a set of data points. Elements can be plotted with a symbol at each data point and lines connecting the points. The appearance of the element, such as its symbol, line width, and color is configurable.

**grid**
    Extends the major and minor ticks of the X-axis and/or Y-axis across the plotting area.

**legend**
    The legend displays the name and symbol of each data element. The legend can be drawn in any margin or in the plotting area.

**marker**
    Markers are used annotate or highlight areas of the graph. For example, you could use a polygon marker to fill an area under a curve, or a text marker to label a particular data point. Markers come in various forms: text strings, bitmaps, connected line segments, images, polygons, or embedded widgets.

**pen**
    Pens define attributes (both symbol and line style) for elements. Data elements use pens to specify how they should be drawn. A data element may use many pens at once. Here, the particular pen used for a data point is determined from each element's weight vector (see the element's -weight and -style options).

**postscript**
    The widget can generate encapsulated PostScript output. This component has several options to configure how the PostScript is generated.

<a name="SYNTAX"></a>
## SYNTAX

**graph** *pathName ?-style line|bar|strip? ?option value?...*

The graph command creates a new window pathName and makes it into a graph widget. At the time this command is invoked, there must not exist a window named pathName, but pathName's parent must exist. Additional options may be specified on the command line or in the option database to configure aspects of the graph such as its colors and font. See the configure operation below for the exact details about what option and value pairs are valid.

The *-style* option can only be set on creation time of the new window. The option control the appearance of the widget. Default value is "line".

If successful, graph returns the path name of the widget. It also creates a new Tcl command by the same name. You can use this command to invoke various operations that query or modify the graph. The general form is:

**pathName** *operation ?arg?...*

Both operation and its arguments determine the exact behavior of the command. The operations available for the graph are described in the section.

The command can also be used to access components of the graph.

**pathName component** *operation ?arg?...*

A graph is composed of several components: coordinate axes, data elements, legend, grid, cross hairs, postscript, and annotation markers. Instead of one big set of configuration options and operations, the graph is partitioned, where each component has its own configuration options and operations that specifically control that aspect or part of the graph.

The operation, now located after the name of the component, is the function to be performed on that component. Each component has its own set of operations that manipulate that component. They will be described below in their own sections.

<a name="GRAPH-OPERATIONS"></a>
## GRAPH OPERATIONS

<a name="pathName-axis"></a>
**pathName axis** *operation ?arg?...*

> See the [AXIS COMPONENT](#AXIS-COMPONENT) section.

<a name="pathName-bar"></a>
**pathName bar** *elemName ?option value?...*

> Creates a new barchart element elemName. It's an error if an element elemName already exists. See the manual for barchart for details about what option and value pairs are valid. TODO

name="pathName-binding"></a>
**pathName binding** *?crosshairs? ?findelement? ?legend? ?zoom?*

The procedure allows the setting of some default bindings. It will add bindings if a name is given and remove bindings if a name is not given.

> If given then the following bindings will be applied:

> *crosshairs*

> > <Any-Motion> will draw crosshairs in the graph window. Leaving the widget will remove the crosshairs.

> *findelement*

> > <Control-ButtonPress-2> This will display informations of the nearest element point.  
> > <Control-Buttonrelease-2> This will remove the displayed informations.  

> *legend* 

> > <Enter> an element will activate the elment entry in the legend window.  
> > <Leave> an element will deactivate the element entry in the legend window.  
> > <ButtonPress-1> will select the element entry in the legend window.  

> *zoom*

> > <ButtonPress-1> Create first and second point of a zooming window. When the second point is created the graph will be zoomed to the new window and the old window will be remembered.  
> > <ButtonPress-3> Undo the last zoom operation.  

<a name="pathName-cget"></a>
**pathName cget** *option*

> Returns the current value of the configuration option given by option. Option may be any option described below for the configure operation.

<a name="pathName-configure"></a>
**pathName configure** *?option value?...*

> Queries or modifies the configuration options of the graph. If option isn't specified, a list describing the current options for pathName is returned. If option is specified, but not value, then a list describing option is returned. If one or more option and value pairs are specified, then for each pair, the option option is set to value. The following options are valid.

> **-aspect** *width/height*

> > Force a fixed aspect ratio of width/height, a floating point number.

> **-background** *color*

> > Sets the background color. This includes the margins and legend, but not the plotting area.

> **-borderwidth** *pixels*

> > Sets the width of the 3-D border around the outside edge of the widget. The -relief option determines if the border is to be drawn. The default is 2.

> **-bottommargin** *pixels*

> > If non-zero, overrides the computed size of the margin extending below the X-coordinate axis. If pixels is 0, the automatically computed size is used. The default is 0.

> **-bufferelements** *boolean*

> > Indicates whether an internal pixmap to buffer the display of data elements should be used. If boolean is true, data elements are drawn to an internal pixmap. This option is especially useful when the graph is redrawn frequently while the remains data unchanged (for example, moving a marker across the plot). See the section. The default is 1.

> **-cursor** *cursor*

> > Specifies the widget's cursor. The default cursor is crosshair.

> **-font** *fontName*

> > Specifies the font of the graph title. The default is `*-Helvetica-Bold-R-Normal-*-18-180-*`.

> **-halo** *pixels*

> > Specifies a maximum distance to consider when searching for the closest data point (see the element's closest operation below). Data points further than pixels away are ignored. The default is 0.5i.

> **-height** *pixels*

> > Specifies the requested height of widget. The default is 4i.

> **-invertxy** *boolean*

> > Indicates whether the placement X-axis and Y-axis should be inverted. If boolean is true, the X and Y axes are swapped. The default is 0.

> **-justify** *justify*

> > Specifies how the title should be justified. This matters only when the title contains more than one line of text. Justify must be left, right, or center. The default is center.

> **-leftmargin** *pixels*

> > If non-zero, overrides the computed size of the margin extending from the left edge of the window to the Y-coordinate axis. If pixels is 0, the automatically computed size is used. The default is 0.

> **-plotbackground** *color*

> > Specifies the background color of the plotting area. The default is white.

> **-plotborderwidth** *pixels*

> > Sets the width of the 3-D border around the plotting area. The -plotrelief option determines if a border is drawn. The default is 2.

> **-plotpadx** *pad*

> > Sets the amount of padding to be added to the left and right sides of the plotting area. Pad can be a list of one or two screen distances. If pad has two elements, the left side of the plotting area entry is padded by the first distance and the right side by the second. If pad is just one distance, both the left and right sides are padded evenly. The default is 8.

> **-plotpady** *pad*

> > Sets the amount of padding to be added to the top and bottom of the plotting area. Pad can be a list of one or two screen distances. If pad has two elements, the top of the plotting area is padded by the first distance and the bottom by the second. If pad is just one distance, both the top and bottom are padded evenly. The default is 8.

> **-plotrelief** *relief*

> > Specifies the 3-D effect for the plotting area. Relief specifies how the interior of the plotting area should appear relative to rest of the graph; for example, raised means the plot should appear to protrude from the graph, relative to the surface of the graph. The default is sunken.

> **-relief** *relief*

> > Specifies the 3-D effect for the graph widget. Relief specifies how the graph should appear relative to widget it is packed into; for example, raised means the graph should appear to protrude. The default is flat.

> **-rightmargin** *pixels*

> > If non-zero, overrides the computed size of the margin extending from the plotting area to the right edge of the window. By default, the legend is drawn in this margin. If pixels is 0, the automatically computed size is used. The default is 0.

> **-takefocus** *focus*

> > Provides information used when moving the focus from window to window via keyboard traversal (e.g., Tab and Shift-Tab). If focus is 0, this means that this window should be skipped entirely during keyboard traversal. 1 means that the this window should always receive the input focus. An empty value means that the traversal scripts make the decision whether to focus on the window. The default is "".

> **-tile** *image*

> > Specifies a tiled background for the widget. If image isn't "", the background is tiled using image. Otherwise, the normal background color is drawn (see the -background option). Image must be an image created using the Tk image command. The default is "".

> **-title** *text*

> > Sets the title to text. If text is "", no title will be displayed.

> **-topmargin** *pixels*

> > If non-zero, overrides the computed size of the margin above the x2 axis. If pixels is 0, the automatically computed size is used. The default is 0.

> **-width** *pixels*

> > Specifies the requested width of the widget. The default is 5i.

<a name="pathName-crosshairs"></a>
**pathName crosshairs** *operation ?arg?*

> See the [CROSSHAIRS COMPONENT](#CROSSHAIRS-COMPONENT) section.

<a name="pathName-element"></a>
**pathName element** *operation ?arg?...*

> See the [ELEMENT COMPONENT](#ELEMENT-COMPONENT) section.

<a name="pathName-extents"></a>
**pathName extents** *item*

> Returns the size of a particular item in the graph. Item must be either leftmargin, rightmargin, topmargin, bottommargin, plotwidth, or plotheight.

<a name="pathName-grid"></a>
**pathName grid** *operation ?arg?...*

> See the [GRID COMPONENT](#GRID-COMPONENT) section.

<a name="pathName-invtransform"></a>
**pathName invtransform** *winX winY*

> Performs an inverse coordinate transformation, mapping window coordinates back to graph coordinates, using the standard X-axis and Y-axis. Returns a list of containing the X-Y graph coordinates.

<a name="pathName-inside"></a>
**pathName inside** *x y*

> Returns 1 is the designated screen coordinate (x and y) is inside the plotting area and 0 otherwise.

<a name="pathName-legend"></a>
**pathName legend** *operation ?arg?...*

> See the [LEGEND COMPONENT](#LEGEND-COMPONENT) section.

<a name="pathName-line"></a>
**pathName line** *operation arg...*

> The operation is the same as element.

<a name="pathName-marker"></a>
**pathName marker** *operation ?arg?...*

> See the [MARKER COMPONENT](#MARKER-COMPONENT) section.

<a name="pathName-postscript"></a>
**pathName postscript** *operation ?arg?...*

> See the [POSTSCIPT COMPONENT](#POSTSCRIPT-COMPONENT)section.

<a name="pathName-snap"></a>
**pathName snap** *?switches? outputName*

> Takes a snapshot of the graph, saving the output in outputName. The following switches are available.

> **-format** *format*

> > Specifies how the snapshot is output. Format may be one of the following listed below. The default is photo.

> > **photo**

> > > Saves a Tk photo image. OutputName represents the name of a Tk photo image that must already have been created.

> > **wmf**

> > > Saves an Aldus Placeable Metafile. OutputName represents the filename where the metafile is written. If outputName is CLIPBOARD, then output is written directly to the Windows clipboard. This format is available only under Microsoft Windows.

> > **emf**

> > > Saves an Enhanced Metafile. OutputName represents the filename where the metafile is written. If outputName is CLIPBOARD, then output is written directly to the Windows clipboard. This format is available only under Microsoft Windows.

> **-height** *size*

> > Specifies the height of the graph. Size is a screen distance. The graph will be redrawn using this dimension, rather than its current window height.

> **-width** *size*

> > Specifies the width of the graph. Size is a screen distance. The graph will be redrawn using this dimension, rather than its current window width.

<a name="pathName-transform"></a>
**pathName transform** *x y*

> > Performs a coordinate transformation, mapping graph coordinates to window coordinates, using the standard X-axis and Y-axis. Returns a list containing the X-Y screen coordinates.

<a name="pathName-xaxis"></a>
**pathName xaxis** *operation ?arg?...*

<a name="pathName-x2axis"></a>
**pathName x2axis** *operation ?arg?...*

<a name="pathName-yaxis"></a>
**pathName yaxis** *operation ?arg?...*

<a name="pathName-y2axis"></a>
**pathName y2axis** *operation ?arg?...*

> See the [AXIS COMPONENT](#AXIS-COMPONENT) section.

<a name="AXIS-COMPONENT"></a>
## AXIS COMPONENT

Four coordinate axes are automatically created: two X-coordinate axes (x and x2) and two Y-coordinate axes (y, and y2). By default, the axis x is located in the bottom margin, y in the left margin, x2 in the top margin, and y2 in the right margin.

An axis consists of the axis line, title, major and minor ticks, and tick labels. Major ticks are drawn at uniform intervals along the axis. Each tick is labeled with its coordinate value. Minor ticks are drawn at uniform intervals within major ticks.

The range of the axis controls what region of data is plotted. Data points outside the minimum and maximum limits of the axis are not plotted. By default, the minimum and maximum limits are determined from the data, but you can reset either limit.

You can have several axes. To create an axis, invoke the axis component and its create operation.

> <code>
	# Create a new axis called "tempAxis"  
	.g axis create tempAxis  
</code>

You map data elements to an axis using the element's -mapy and -mapx configuration options. They specify the coordinate axes an element is mapped onto.

> <code>
	# Now map the tempAxis data to this axis.  
	.g element create "e1" -xdata $x -ydata $y -mapy tempAxis  
</code>

Any number of axes can be displayed simultaneously. They are drawn in the margins surrounding the plotting area. The default axes x and y are drawn in the bottom and left margins. The axes x2 and y2 are drawn in top and right margins. By default, only x and y are shown. Note that the axes can have different scales.

To display a different axis or more than one axis, you invoke one of the following components: xaxis, yaxis, x2axis, and y2axis. Each component has a use operation that designates the axis (or axes) to be drawn in that corresponding margin: xaxis in the bottom, yaxis in the left, x2axis in the top, and y2axis in the right.

> <code>
	# Display the axis tempAxis in the left margin.  
	.g yaxis use tempAxis  
</code>

The use operation takes a list of axis names as its last argument. This is the list of axes to be drawn in this margin.

You can configure axes in many ways. The axis scale can be linear or logarithmic. The values along the axis can either monotonically increase or decrease. If you need custom tick labels, you can specify a Tcl procedure to format the label any way you wish. You can control how ticks are drawn, by changing the major tick interval or the number of minor ticks. You can define non-uniform tick intervals, such as for time-series plots.

<a name="pathName-axis-bind"></a>
**pathName axis bind** *tagName ?sequence? ?command?*

> Associates command with tagName such that whenever the event sequence given by sequence occurs for an axis with this tag, command will be invoked. The syntax is similar to the bind command except that it operates on graph axes, rather than widgets. See the bind manual entry for complete details on sequence and the substitutions performed on command before invoking it.

> If all arguments are specified then a new binding is created, replacing any existing binding for the same sequence and tagName. If the first character of command is + then command augments an existing binding rather than replacing it. If no command argument is provided then the command currently associated with tagName and sequence (it's an error occurs if there's no such binding) is returned. If both command and sequence are missing then a list of all the event sequences for which bindings have been defined for tagName.

<a name="pathName-axis-cget"></a>
**pathName axis cget** *axisName option*

> Returns the current value of the option given by option for axisName. Option may be any option described below for the axis configure operation.

<a name="pathName-axis-configure"></a>
**pathName axis configure** *axisName ?axisName?... ?option value?...*

> Queries or modifies the configuration options of axisName. Several axes can be changed. If option isn't specified, a list describing all the current options for axisName is returned. If option is specified, but not value, then a list describing option is returned. If one or more option and value pairs are specified, then for each pair, the axis option option is set to value. The following options are valid for axes.

> **-bindtags** *tagList*

> > Specifies the binding tags for the axis. TagList is a list of binding tag names. The tags and their order will determine how events for axes are handled. Each tag in the list matching the current event sequence will have its Tcl command executed. Implicitly the name of the element is always the first tag in the list. The default value is all.

> **-color** *color*

> > Sets the color of the axis and tick labels. The default is black.

> **-command** *prefix*

> > Specifies a Tcl command to be invoked when formatting the axis tick labels. Prefix is a string containing the name of a Tcl proc and any extra arguments for the procedure. This command is invoked for each major tick on the axis. Two additional arguments are passed to the procedure: the pathname of the widget and the current the numeric value of the tick. The procedure returns the formatted tick label. If "" is returned, no label will appear next to the tick. You can get the standard tick labels again by setting prefix to "". The default is "".

> > Please note that this procedure is invoked while the graph is redrawn. You may query configuration options. But do not them, because this can have unexpected results.

> **-descending** *boolean*

> > Indicates whether the values along the axis are monotonically increasing or decreasing. If boolean is true, the axis values will be decreasing. The default is 0.

> **-hide** *boolean*

> > Indicates if the axis is displayed. If boolean is false the axis will be displayed. Any element mapped to the axis is displayed regardless. The default value is 0.

> **-justify** *justify*

> > Specifies how the axis title should be justified. This matters only when the axis title contains more than one line of text. Justify must be left, right, or center. The default is center.

> **-limits** *formatStr*

> > Specifies a printf-like description to format the minimum and maximum limits of the axis. The limits are displayed at the top/bottom or left/right sides of the plotting area. FormatStr is a list of one or two format descriptions. If one description is supplied, both the minimum and maximum limits are formatted in the same way. If two, the first designates the format for the minimum limit, the second for the maximum. If "" is given as either description, then the that limit will not be displayed. The default is "".

> **-linewidth** *pixels*

> > Sets the width of the axis and tick lines. The default is 1 pixel.

> **-logscale** *boolean*

> > Indicates whether the scale of the axis is logarithmic or linear. If boolean is true, the axis is logarithmic. The default scale is linear.

> **-loose** *boolean*

> > Indicates whether the limits of the axis should fit the data points tightly, at the outermost data points, or loosely, at the outer tick intervals. If the axis limit is set with the -min or -max option, the axes are displayed tightly. If boolean is true, the axis range is "loose". The default is 0.

> **-majorticks** *majorList*

> > Specifies where to display major axis ticks. You can use this option to display ticks at non-uniform intervals. MajorList is a list of axis coordinates designating the location of major ticks. No minor ticks are drawn. If majorList is "", major ticks will be automatically computed. The default is "".

> **-max** *value*

> > Sets the maximum limit of axisName. Any data point greater than value is not displayed. If value is "", the maximum limit is calculated using the largest data value. The default is "".

> **-min** *value*

> > Sets the minimum limit of axisName. Any data point less than value is not displayed. If value is "", the minimum limit is calculated using the smallest data value. The default is "".

> **-minorticks** *minorList*

> > Specifies where to display minor axis ticks. You can use this option to display minor ticks at non-uniform intervals. MinorList is a list of real values, ranging from 0.0 to 1.0, designating the placement of a minor tick. No minor ticks are drawn if the -majortick option is also set. If minorList is "", minor ticks will be automatically computed. The default is "".

> **-rotate** *theta*

> > Specifies the how many degrees to rotate the axis tick labels. Theta is a real value representing the number of degrees to rotate the tick labels. The default is 0.0 degrees.

> **-scrollcommand** *command*

> > Specify the prefix for a command used to communicate with scrollbars for this axis, such as .sbar set.

> **-scrollmax** *value*

> > Sets the maximum limit of the axis scroll region. If value is "", the maximum limit is calculated using the largest data value. The default is "".

> **-scrollmin** *value*

> > Sets the minimum limit of axis scroll region. If value is "", the minimum limit is calculated using the smallest data value. The default is "".

> **-showticks** *boolean*

> > Indicates whether axis ticks should be drawn. If boolean is true, ticks are drawn. If false, only the axis line is drawn. The default is 1.

> **-stepsize** *value*

> > Specifies the interval between major axis ticks. If value isn't a valid interval (must be less than the axis range), the request is ignored and the step size is automatically calculated.

> **-subdivisions** *number*

> > Indicates how many minor axis ticks are to be drawn. For example, if number is two, only one minor tick is drawn. If number is one, no minor ticks are displayed. The default is 2.

> **-tickfont** *fontName*

> > Specifies the font for axis tick labels. The default is `*-Courier-Bold-R-Normal-*-100-*`.

> **-ticklength** *pixels*

> > Sets the length of major and minor ticks (minor ticks are half the length of major ticks). If pixels is less than zero, the axis will be inverted with ticks drawn pointing towards the plot. The default is 0.1i.

> **-title** *text*

> > Sets the title of the axis. If text is "", no axis title will be displayed.

> **-titlealternate** *boolean*

> > Indicates to display the axis title in its alternate location. Normally the axis title is centered along the axis. This option places the axis either to the right (horizontal axes) or above (vertical axes) the axis. The default is 0.

> **-titlecolor** *color*

> > Sets the color of the axis title. The default is black.

> **-titlefont** *fontName*

> > Specifies the font for axis title. The default is `*-Helvetica-Bold-R-Normal-*-14-140-*`.

Axis configuration options may be also be set by the option command. The resource class is Axis. The resource names are the names of the axes (such as x or x2).

> <code>
        option add *Graph.Axis.Color  blue
        option add *Graph.x.LogScale  true
        option add *Graph.x2.LogScale false
</code>

<a name="pathName-axis-create"></a>
**pathName axis create** *axisName ?option value?...*

> Creates a new axis by the name axisName. No axis by the same name can already exist. Option and value are described in above in the axis configure operation.

<a name="pathName-axis-delete"></a>
**pathName axis delete** *?axisName?...*

> Deletes the named axes. An axis is not really deleted until it is not longer in use, so it's safe to delete axes mapped to elements.

<a name="pathName-axis-invtransform"></a>
**pathName axis invtransform** *axisName value*

> Performs the inverse transformation, changing the screen coordinate value to a graph coordinate, mapping the value mapped to axisName. Returns the graph coordinate.

<a name="pathName-axis-limits"></a>
**pathName axis limits** *axisName*

> Returns a list of the minimum and maximum limits for axisName. The order of the list is min max.

<a name="pathName-axis-names"></a>
**pathName axis names** *?pattern?...*

> Returns a list of axes matching zero or more patterns. If no pattern argument is give, the names of all axes are returned.

<a name="pathName-axis-transform"></a>
**pathName axis transform** *axisName value*

> Transforms the coordinate value to a screen coordinate by mapping the it to axisName. Returns the transformed screen coordinate.

<a name="pathName-axis-view"></a>
**pathName axis view** *axisName*

> Change the viewable area of this axis. Use as an argument to a scrollbar's "-command".

The default axes are x, y, x2, and y2. But you can display more than four axes simultaneously. You can also swap in a different axis with use operation of the special axis components: xaxis, x2axis, yaxis, and y2axis.

> <code>
	.g create axis temp  
	.g create axis time  
	...  
	.g xaxis use temp  
	.g yaxis use time  
</code>

Only the axes specified for use are displayed on the screen.

The xaxis, x2axis, yaxis, and y2axis components operate on an axis location rather than a specific axis like the more general axis component does. They implicitly control the axis that is currently using to that location. By default, xaxis uses the x axis, yaxis uses y, x2axis uses x2, and y2axis uses y2. When more than one axis is displayed in a margin, it represents the first axis displayed.

<a name="CROSSHAIRS-COMPONENT"></a>
## CROSSHAIRS COMPONENT

Cross hairs consist of two intersecting lines (one vertical and one horizontal) drawn completely across the plotting area. They are used to position the mouse in relation to the coordinate axes. Cross hairs differ from line markers in that they are implemented using XOR drawing primitives. This means that they can be quickly drawn and erased without redrawing the entire graph.

The following operations are available for cross hairs:

<a name="pathName-crosshairs-cget"></a>
**pathName crosshairs cget** *option*

> Returns the current value of the cross hairs configuration option given by option. Option may be any option described below for the cross hairs configure operation.

<a name="pathName-crosshairs-configure"></a>
**pathName crosshairs configure** *?option value?...*

> Queries or modifies the configuration options of the cross hairs. If option isn't specified, a list describing all the current options for the cross hairs is returned. If option is specified, but not value, then a list describing option is returned. If one or more option and value pairs are specified, then for each pair, the cross hairs option option is set to value. The following options are available for cross hairs.

> **-color** *color*

> > Sets the color of the cross hairs. The default is black.

> **-dashes** *dashList*

> > Sets the dash style of the cross hairs. DashList is a list of up to 11 numbers that alternately represent the lengths of the dashes and gaps on the cross hair lines. Each number must be between 1 and 255. If dashList is "", the cross hairs will be solid lines.

> **-hide** *boolean*

> > Indicates whether cross hairs are drawn. If boolean is true, cross hairs are not drawn. The default is yes.

> **-linewidth** *pixels*

> > Set the width of the cross hair lines. The default is 1.

> **-position** *pos*

> > Specifies the screen position where the cross hairs intersect. Pos must be in the form "@x,y", where x and y are the window coordinates of the intersection.

> Cross hairs configuration options may be also be set by the option command. The resource name and class are crosshairs and Crosshairs respectively.

> > <code>
        option add *Graph.Crosshairs.LineWidth 2  
        option add *Graph.Crosshairs.Color     red  
</code>

<a name="pathName-crosshairs-off"></a>
**pathName crosshairs off**

> Turns off the cross hairs.

<a name="pathName-crosshairs-on"></a>
**pathName crosshairs on**

> Turns on the display of the cross hairs.

<a name="pathName-crosshairs-toggle"></a>
**pathName crosshairs toggle**

> Toggles the current state of the cross hairs, alternately mapping and unmapping the cross hairs.

<a name="ELEMENT-COMPONENT"></a>
## ELEMENT COMPONENT

A data element represents a set of data. It contains x and y vectors containing the coordinates of the data points. Elements can be displayed with a symbol at each data point and lines connecting the points. Elements also control the appearance of the data, such as the symbol type, line width, color etc.

When new data elements are created, they are automatically added to a list of displayed elements. The display list controls what elements are drawn and in what order.

The following operations are available for elements.

<a name="pathName-element-activate"></a>
**pathName element activate** *elemName ?index?...*

> Specifies the data points of element elemName to be drawn using active foreground and background colors. ElemName is the name of the element and index is a number representing the index of the data point. If no indices are present then all data points become active.

<a name="pathName-element-bind"></a>
**pathName element bind** *tagName ?sequence? ?command?*

> Associates command with tagName such that whenever the event sequence given by sequence occurs for an element with this tag, command will be invoked. The syntax is similar to the bind command except that it operates on graph elements, rather than widgets. See the bind manual entry for complete details on sequence and the substitutions performed on command before invoking it.

> If all arguments are specified then a new binding is created, replacing any existing binding for the same sequence and tagName. If the first character of command is + then command augments an existing binding rather than replacing it. If no command argument is provided then the command currently associated with tagName and sequence (it's an error occurs if there's no such binding) is returned. If both command and sequence are missing then a list of all the event sequences for which bindings have been defined for tagName.

<a name="pathName-element-cget"></a>
**pathName element cget** *elemName option*

> Returns the current value of the element configuration option given by option. Option may be any of the options described below for the element configure operation.

<a name="pathName-element-closest"></a>
**pathName element closest** *x y varName ?option value?... ?elemName?...*

> Searches for the data point closest to the window coordinates x and y. By default, all elements are searched. Hidden elements (see the -hide option is false) are ignored. You can limit the search by specifying only the elements you want to be considered. ElemName must be the name of an element that is not be hidden. VarName is the name of a Tcl array variable and will contain the search results: the name of the closest element, the index of the closest data point, and the graph coordinates of the point. Returns 0, if no data point within the threshold distance can be found, otherwise 1 is returned. The following option-value pairs are available.

> **-along** *direction*

> > Search for the closest element using the following criteria:

> > *x*

> > > Find closest element vertically from the given X-coordinate.

> > *y*

> > > Find the closest element horizontally from the given Y-coordinate.

> > *both*

> > > Find the closest element for the given point (using both the X and Y coordinates).

> **-halo** *pixels*

> > Specifies a threshold distance where selected data points are ignored. Pixels is a valid screen distance, such as 2 or 1.2i. If this option isn't specified, then it defaults to the value of the graph's -halo option.

> **-interpolate** *string*

> > Indicates whether to consider projections that lie along the line segments connecting data points when searching for the closest point. The default value is 0. The values for string are described below.

> > *no*

> > > Search only for the closest data point.

> > *yes*

> > > Search includes projections that lie along the line segments connecting the data points.

<a name="pathName-element-configure"></a>
**pathName element configure** *elemName ?elemName... ?option value?...*

> Queries or modifies the configuration options for elements. Several elements can be modified at the same time. If option isn't specified, a list describing all the current options for elemName is returned. If option is specified, but not value, then a list describing the option option is returned. If one or more option and value pairs are specified, then for each pair, the element option option is set to value. The following options are valid for elements.

> **-activepen** *penName*

> > Specifies pen to use to draw active element. If penName is "", no active elements will be drawn. The default is activeLine.

> **-bindtags** *tagList*

> > Specifies the binding tags for the element. TagList is a list of binding tag names. The tags and their order will determine how events are handled for elements. Each tag in the list matching the current event sequence will have its Tcl command executed. Implicitly the name of the element is always the first tag in the list. The default value is all.

> **-color** *color*

> > Sets the color of the traces connecting the data points.

> **-dashes** *dashList*

> > Sets the dash style of element line. DashList is a list of up to 11 numbers that alternately represent the lengths of the dashes and gaps on the element line. Each number must be between 1 and 255. If dashList is "", the lines will be solid.

> **-data** *coordList*

> > Specifies the X-Y coordinates of the data. CoordList is a list of numeric expressions representing the X-Y coordinate pairs of each data point.

> **-fill** *color*

> > Sets the interior color of symbols. If color is "", then the interior of the symbol is transparent. If color is defcolor, then the color will be the same as the -color option. The default is defcolor.

> **-hide** *boolean*

> > Indicates whether the element is displayed. The default is no.

> **-label** *text*

> > Sets the element's label in the legend. If text is "", the element will have no entry in the legend. The default label is the element's name.

> **-linewidth** *pixels*

> > Sets the width of the connecting lines between data points. If pixels is 0, no connecting lines will be drawn between symbols. The default is 0.

> **-mapx** *xAxis*

> > Selects the X-axis to map the element's X-coordinates onto. XAxis must be the name of an axis. The default is x.

> **-mapy** *yAxis*

> > Selects the Y-axis to map the element's Y-coordinates onto. YAxis must be the name of an axis. The default is y.

> **-offdash** *color*

> > Sets the color of the stripes when traces are dashed (see the -dashes option). If color is "", then the "off" pixels will represent gaps instead of stripes. If color is defcolor, then the color will be the same as the -color option. The default is defcolor.

> **-outline** *color*

> > Sets the color or the outline around each symbol. If color is "", then no outline is drawn. If color is defcolor, then the color will be the same as the -color option. The default is defcolor.

> **-pen** *penname*

> > Set the pen to use for this element.

> **-outlinewidth** *pixels*

> > Sets the width of the outline bordering each symbol. If pixels is 0, no outline will be drawn. The default is 1.

> **-pixels** *pixels*

> > Sets the size of symbols. If pixels is 0, no symbols will be drawn. The default is 0.125i.

> **-scalesymbols** *boolean*

> > If boolean is true, the size of the symbols drawn for elemName will change with scale of the X-axis and Y-axis. At the time this option is set, the current ranges of the axes are saved as the normalized scales (i.e scale factor is 1.0) and the element is drawn at its designated size (see the -pixels option). As the scale of the axes change, the symbol will be scaled according to the smaller of the X-axis and Y-axis scales. If boolean is false, the element's symbols are drawn at the designated size, regardless of axis scales. The default is 0.

> **-smooth** *smooth*

> > Specifies how connecting line segments are drawn between data points. Smooth can be either linear, step, natural, or quadratic. If smooth is linear, a single line segment is drawn, connecting both data points. When smooth is step, two line segments are drawn. The first is a horizontal line segment that steps the next X-coordinate. The second is a vertical line, moving to the next Y-coordinate. Both natural and quadratic generate multiple segments between data points. If natural, the segments are generated using a cubic spline. If quadratic, a quadratic spline is used. The default is linear.

> **-styles** *styleList*

> > Specifies what pen to use based on the range of weights given. StyleList is a list of style specifications. Each style specification, in turn, is a list consisting of a pen name, and optionally a minimum and maximum range. Data points whose weight (see the -weight option) falls in this range, are drawn with this pen. If no range is specified it defaults to the index of the pen in the list. Note that this affects only symbol attributes. Line attributes, such as line width, dashes, etc. are ignored.

> **-symbol** *symbol*

> > Specifies the symbol for data points. Symbol can be either square, circle, diamond, plus, cross, splus, scross, triangle, "" (where no symbol is drawn), or a bitmap. Bitmaps are specified as "source ?mask?", where source is the name of the bitmap, and mask is the bitmap's optional mask. The default is circle.

> **-trace** *direction*

> > Indicates whether connecting lines between data points (whose X-coordinate values are either increasing or decreasing) are drawn. Direction must be increasing, decreasing, or both. For example, if direction is increasing, connecting lines will be drawn only between those data points where X-coordinate values are monotonically increasing. If direction is both, connecting lines will be draw between all data points. The default is both.

> **-weights** *wVec*

> > Specifies the weights of the individual data points. This, with the list pen styles (see the -styles option), controls how data points are drawn. WVec is the name of a graph vector or a list of numeric expressions representing the weights for each data point.

> **-xdata** *xVec*

> > Specifies the X-coordinates of the data. XVec is the name of a graph vector or a list of numeric expressions.

> **-ydata** *yVec*

> > Specifies the Y-coordinates of the data. YVec is the name of a graph vector or a list of numeric expressions.

> Element configuration options may also be set by the option command. The resource class is Element. The resource name is the name of the element.

> > <code>
        option add *Graph.Element.symbol line  
        option add *Graph.e1.symbol line  
</code>

<a name="pathName-element-create"></a>
**pathName element create** *elemName ?option value?...*

> Creates a new element elemName. It's an error is an element elemName already exists. If additional arguments are present, they specify options valid for the element configure operation.

<a name="pathName-element-deactivate"></a>
**pathName element deactivate** *elemName ?elemName?...*

> Deactivates all the elements matching pattern. Elements whose names match any of the patterns given are redrawn using their normal colors.

<a name="pathName-element-delete"></a>
**pathName element delete** *?elemName?...*

> Deletes all the named elements. The graph is automatically redrawn.

<a name="pathName-element-exists"></a>
**pathName element exists** *elemName*

> Returns 1 if an element elemName currently exists and 0 otherwise.

<a name="pathName-element-names"></a>
**pathName element names** *?pattern?...*

> Returns the elements matching one or more pattern. If no pattern is given, the names of all elements is returned.

<a name="pathName-element-show"></a>
**pathName element show** *?nameList?*

> Queries or modifies the element display list. The element display list designates the elements drawn and in what order. NameList is a list of elements to be displayed in the order they are named. If there is no nameList argument, the current display list is returned.

<a name="pathName-element-type"></a>
**pathName element type** *elemName*

> Returns the type of elemName. If the element is a bar element, the commands returns the string "bar", otherwise it returns "line".

<a name="GRID-COMPONENT"></a>
## GRID COMPONENT

Grid lines extend from the major and minor ticks of each axis horizontally or vertically across the plotting area. The following operations are available for grid lines.

<a name="pathName-grid-cget"></a>
**pathName grid cget** *option*

> Returns the current value of the grid line configuration option given by option. Option may be any option described below for the grid configure operation.

<a name="pathName-grid-configure"></a>
**pathName grid configure** *?option value?...*

> Queries or modifies the configuration options for grid lines. If option isn't specified, a list describing all the current grid options for pathName is returned. If option is specified, but not value, then a list describing option is returned. If one or more option and value pairs are specified, then for each pair, the grid line option option is set to value. The following options are valid for grid lines.

> **-color** *color*

> > Sets the color of the grid lines. The default is black.

> **-dashes** *dashList*

> > Sets the dash style of the grid lines. DashList is a list of up to 11 numbers that alternately represent the lengths of the dashes and gaps on the grid lines. Each number must be between 1 and 255. If dashList is "", the grid will be solid lines.

> **-hide** *boolean*

> > Indicates whether the grid should be drawn. If boolean is true, grid lines are not shown. The default is yes.

> **-linewidth** *pixels*

> > Sets the width of grid lines. The default width is 1.

> **-mapx** *xAxis*

> > Specifies the X-axis to display grid lines. XAxis must be the name of an axis or "" for no grid lines. The default is "".

> **-mapy** *yAxis*

> > Specifies the Y-axis to display grid lines. YAxis must be the name of an axis or "" for no grid lines. The default is y.

> **-minor** *boolean*

> > Indicates whether the grid lines should be drawn for minor ticks. If boolean is true, the lines will appear at minor tick intervals. The default is 1.

> **-raised** *boolean*

> > Grid is to be raised or drawn over elements.

> Grid configuration options may also be set by the option command. The resource name and class are grid and Grid respectively.

> > <code>
        option add *Graph.grid.LineWidth 2  
        option add *Graph.Grid.Color     black  
</code>

<a name="pathName-grid-off"></a>
**pathName grid off**

> Turns off the display the grid lines.

<a name="pathName-grid-on"></a>
**pathName grid on**

> Turns on the display the grid lines.

<a name="pathName-grid-toggle"></a>
**pathName grid toggle**

> Toggles the display of the grid.

<a name="LEGEND-COMPONENT"></a>
## LEGEND COMPONENT

The legend displays a list of the data elements. Each entry consists of the element's symbol and label. The legend can appear in any margin (the default location is in the right margin). It can also be positioned anywhere within the plotting area.

The following operations are valid for the legend.

<a name="pathName-legend-activate"></a>
**pathName legend activate** *pattern...*

> Selects legend entries to be drawn using the active legend colors and relief. All entries whose element names match pattern are selected. To be selected, the element name must match only one pattern.

<a name="pathName-legend-bind"></a>
**pathName legend bind** *tagName ?sequence? ?command?*

> Associates command with tagName such that whenever the event sequence given by sequence occurs for a legend entry with this tag, command will be invoked. Implicitly the element names in the entry are tags. The syntax is similar to the bind command except that it operates on legend entries, rather than widgets. See the bind manual entry for complete details on sequence and the substitutions performed on command before invoking it.

> If all arguments are specified then a new binding is created, replacing any existing binding for the same sequence and tagName. If the first character of command is + then command augments an existing binding rather than replacing it. If no command argument is provided then the command currently associated with tagName and sequence (it's an error occurs if there's no such binding) is returned. If both command and sequence are missing then a list of all the event sequences for which bindings have been defined for tagName.

<a name="pathName-legend-cget"></a>
**pathName legend cget** *option*

> Returns the current value of a legend configuration option. Option may be any option described below in the legend configure operation.

<a name="pathName-legend-configure"></a>
**pathName legend configure** *?option value?...*

> Queries or modifies the configuration options for the legend. If option isn't specified, a list describing the current legend options for pathName is returned. If option is specified, but not value, then a list describing option is returned. If one or more option and value pairs are specified, then for each pair, the legend option option is set to value. The following options are valid for the legend.

> **-activebackground** *color*

> > Sets the background color for active legend entries. All legend entries marked active (see the legend activate operation) are drawn using this background color.

> **-activeborderwidth** *pixels*

> > Sets the width of the 3-D border around the outside edge of the active legend entries. The default is 2.

> **-activeforeground** *color*

> > Sets the foreground color for active legend entries. All legend entries marked as active (see the legend activate operation) are drawn using this foreground color.

> **-activerelief** *relief*

> > Specifies the 3-D effect desired for active legend entries. Relief denotes how the interior of the entry should appear relative to the legend; for example, raised means the entry should appear to protrude from the legend, relative to the surface of the legend. The default is flat.

> **-anchor** *anchor*

> > Tells how to position the legend relative to the positioning point for the legend. This is dependent on the value of the -position option. The default is center.

> > *left* or *right*

> > > The anchor describes how to position the legend vertically.

> > *top* or *bottom*

> > > The anchor describes how to position the legend horizontally.

> > *@x,y*

> > > The anchor specifies how to position the legend relative to the positioning point. For example, if anchor is center then the legend is centered on the point; if anchor is n then the legend will be drawn such that the top center point of the rectangular region occupied by the legend will be at the positioning point.

> > *plotarea*

> > > The anchor specifies how to position the legend relative to the plotting area. For example, if anchor is center then the legend is centered in the plotting area; if anchor is ne then the legend will be drawn such that occupies the upper right corner of the plotting area.

> **-background** *color*

> > Sets the background color of the legend. If color is "", the legend background with be transparent.

> **-bindtags** *tagList*

> > Specifies the binding tags for legend entries. TagList is a list of binding tag names. The tags and their order will determine how events are handled for legend entries. Each tag in the list matching the current event sequence will have its Tcl command executed. The default value is all.

> **-borderwidth** *pixels*

> > Sets the width of the 3-D border around the outside edge of the legend (if such border is being drawn; the relief option determines this). The default is 2 pixels.

> **-font** *fontName*

> > FontName specifies a font to use when drawing the labels of each element into the legend. The default is `*-Helvetica-Bold-R-Normal-*-12-120-*`.

> **-foreground** *color*

> > Sets the foreground color of the text drawn for the element's label. The default is black.

> **-hide** *boolean*

> > Indicates whether the legend should be displayed. If boolean is true, the legend will not be draw. The default is no.

> **-ipadx** *pad*

> > Sets the amount of internal padding to be added to the width of each legend entry. Pad can be a list of one or two screen distances. If pad has two elements, the left side of the legend entry is padded by the first distance and the right side by the second. If pad is just one distance, both the left and right sides are padded evenly. The default is 2.

> **-ipady** *pad*

> > Sets an amount of internal padding to be added to the height of each legend entry. Pad can be a list of one or two screen distances. If pad has two elements, the top of the entry is padded by the first distance and the bottom by the second. If pad is just one distance, both the top and bottom of the entry are padded evenly. The default is 2.

> **-padx** *pad*

> > Sets the padding to the left and right exteriors of the legend. Pad can be a list of one or two screen distances. If pad has two elements, the left side of the legend is padded by the first distance and the right side by the second. If pad has just one distance, both the left and right sides are padded evenly. The default is 4.

> **-pady** *pad*

> > Sets the padding above and below the legend. Pad can be a list of one or two screen distances. If pad has two elements, the area above the legend is padded by the first distance and the area below by the second. If pad is just one distance, both the top and bottom areas are padded evenly. The default is 0.

> **-position** *pos*

> > Specifies where the legend is drawn. The -anchor option also affects where the legend is positioned. If pos is left, left, top, or bottom, the legend is drawn in the specified margin. If pos is plotarea, then the legend is drawn inside the plotting area at a particular anchor. If pos is in the form "@x,y", where x and y are the window coordinates, the legend is drawn in the plotting area at the specified coordinates. The default is right.

> **-raised** *boolean*

> > Indicates whether the legend is above or below the data elements. This matters only if the legend is in the plotting area. If boolean is true, the legend will be drawn on top of any elements that may overlap it. The default is no.

> **-relief** *relief*

> > Specifies the 3-D effect for the border around the legend. Relief specifies how the interior of the legend should appear relative to the graph; for example, raised means the legend should appear to protrude from the graph, relative to the surface of the graph. The default is sunken.

> Legend configuration options may also be set by the option command. The resource name and class are legend and Legend respectively.

> > <code>
        option add *Graph.legend.Foreground blue  
        option add *Graph.Legend.Relief     raised  
</code>

<a name="pathName-legend-deactivate"></a>
**pathName legend deactivate** *pattern...*

> Selects legend entries to be drawn using the normal legend colors and relief. All entries whose element names match pattern are selected. To be selected, the element name must match only one pattern.

<a name="pathName-legend-get"></a>
**pathName legend get** *pos*

> Returns the name of the element whose entry is at the screen position pos in the legend. Pos must be in the form "@x,y", where x and y are window coordinates. If the given coordinates do not lie over a legend entry, "" is returned.

<a name="PEN-COMPONENT"></a>
## PEN COMPONENT

Pens define attributes (both symbol and line style) for elements. Pens mirror the configuration options of data elements that pertain to how symbols and lines are drawn. Data elements use pens to determine how they are drawn. A data element may use several pens at once. In this case, the pen used for a particular data point is determined from each element's weight vector (see the element's -weight and -style options).

One pen, called activeLine, is automatically created. It's used as the default active pen for elements. So you can change the active attributes for all elements by simply reconfiguring this pen.

> <code>
	.g pen configure "activeLine" -color green  
</code>

You can create and use several pens. To create a pen, invoke the pen component and its create operation.

> <code>
	.g pen create myPen  
</code>

You map pens to a data element using either the element's -pen or -activepen options.

> <code>
	.g element create "line1" -xdata $x -ydata $tempData -pen myPen  
</code>

An element can use several pens at once. This is done by specifying the name of the pen in the element's style list (see the -styles option).

> <code>
	.g element configure "line1" -styles { myPen 2.0 3.0 }  
</code>

This says that any data point with a weight between 2.0 and 3.0 is to be drawn using the pen myPen. All other points are drawn with the element's default attributes.

The following operations are available for pen components.

<a name="pathName-pen-cget"></a>
**pathName pen cget** *penName option*

> Returns the current value of the option given by option for penName. Option may be any option described below for the pen configure operation.

<a name="pathName-pen-configure"></a>
**pathName pen configure** *penName ?penName... ?option value?...*

> Queries or modifies the configuration options of penName. Several pens can be modified at once. If option isn't specified, a list describing the current options for penName is returned. If option is specified, but not value, then a list describing option is returned. If one or more option and value pairs are specified, then for each pair, the pen option option is set to value. The following options are valid for pens.

> **-color** *color*

> > Sets the color of the traces connecting the data points.

> **-dashes** *dashList*

> > Sets the dash style of element line. DashList is a list of up to 11 numbers that alternately represent the lengths of the dashes and gaps on the element line. Each number must be between 1 and 255. If dashList is "", the lines will be solid.

> **-fill** *color*

> > Sets the interior color of symbols. If color is "", then the interior of the symbol is transparent. If color is defcolor, then the color will be the same as the -color option. The default is defcolor.

> **-linewidth** *pixels*

> > Sets the width of the connecting lines between data points. If pixels is 0, no connecting lines will be drawn between symbols. The default is 0.

> **-offdash** *color*

> > Sets the color of the stripes when traces are dashed (see the -dashes option). If color is "", then the "off" pixels will represent gaps instead of stripes. If color is defcolor, then the color will be the same as the -color option. The default is defcolor.

> **-outline** *color*

> > Sets the color or the outline around each symbol. If color is "", then no outline is drawn. If color is defcolor, then the color will be the same as the -color option. The default is defcolor.

> **-outlinewidth** *pixels*

> > Sets the width of the outline bordering each symbol. If pixels is 0, no outline will be drawn. The default is 1.

> **-pixels** *pixels*

> > Sets the size of symbols. If pixels is 0, no symbols will be drawn. The default is 0.125i.

> **-symbol** *symbol*

> Specifies the symbol for data points. Symbol can be either square, circle, diamond, plus, cross, splus, scross, triangle, "" (where no symbol is drawn), or a bitmap. Bitmaps are specified as "source ?mask?", where source is the name of the bitmap, and mask is the bitmap's optional mask. The default is circle.

> **-type** *elemType*

> > Specifies the type of element the pen is to be used with. This option should only be employed when creating the pen. This is for those that wish to mix different types of elements (bars and lines) on the same graph. The default type is "line".

> Pen configuration options may be also be set by the option command. The resource class is Pen. The resource names are the names of the pens.

> > <code>
        option add *Graph.Pen.Color  blue  
        option add *Graph.activeLine.color  green  
</code>

<a name="pathName-pen-create"></a>
**pathName pen create** *penName ?option value?...*

> Creates a new pen by the name penName. No pen by the same name can already exist. Option and value are described in above in the pen configure operation.

<a name="pathName-pen-delete"></a>
**pathName pen delete** *?penName?...*

> Deletes the named pens. A pen is not really deleted until it is not longer in use, so it's safe to delete pens mapped to elements.

<a name="pathName-pen-names"></a>
**pathName pen names** *?pattern?...*

> Returns a list of pens matching zero or more patterns. If no pattern argument is give, the names of all pens are returned.

<a name="POSTSCRIPT-COMPONENT"></a>
## POSTSCRIPT COMPONENT

The graph can generate encapsulated PostScript output. There are several configuration options you can specify to control how the plot will be generated. You can change the page dimensions and borders. The plot itself can be scaled, centered, or rotated to landscape. The PostScript output can be written directly to a file or returned through the interpreter.

The following postscript operations are available.

<a name="pathName-postscript-cget"></a>
**pathName postscript cget** *option*

> Returns the current value of the postscript option given by option. Option may be any option described below for the postscript configure operation.

<a name="pathName-postscript-configure"></a>
**pathName postscript configure** *?option value?...*

> Queries or modifies the configuration options for PostScript generation. If option isn't specified, a list describing the current postscript options for pathName is returned. If option is specified, but not value, then a list describing option is returned. If one or more option and value pairs are specified, then for each pair, the postscript option option is set to value. The following postscript options are available.

> **-center** *boolean*

> > Indicates whether the plot should be centered on the PostScript page. If boolean is false, the plot will be placed in the upper left corner of the page. The default is 1.

> **-colormap** *varName*

> > VarName must be the name of a global array variable that specifies a color mapping from the X color name to PostScript. Each element of varName must consist of PostScript code to set a particular color value (e.g. ``1.0 1.0 0.0 setrgbcolor''). When generating color information in PostScript, the array variable varName is checked if an element of the name as the color exists. If so, it uses its value as the PostScript command to set the color. If this option hasn't been specified, or if there isn't an entry in varName for a given color, then it uses the red, green, and blue intensities from the X color.

> **-colormode** *mode*

> > Specifies how to output color information. Mode must be either color (for full color output), gray (convert all colors to their gray-scale equivalents) or mono (convert foreground colors to black and background colors to white). The default mode is color.

> **-fontmap** *varName*

> > VarName must be the name of a global array variable that specifies a font mapping from the X font name to PostScript. Each element of varName must consist of a Tcl list with one or two elements; the name and point size of a PostScript font. When outputting PostScript commands for a particular font, the array variable varName is checked to see if an element by the specified font exists. If there is such an element, then the font information contained in that element is used in the PostScript output. (If the point size is omitted from the list, the point size of the X font is used). Otherwise the X font is examined in an attempt to guess what PostScript font to use. This works only for fonts whose foundry property is Adobe (such as Times, Helvetica, Courier, etc.). If all of this fails then the font defaults to Helvetica-Bold.

> **-decorations** *boolean*

> > Indicates whether PostScript commands to generate color backgrounds and 3-D borders will be output. If boolean is false, the background will be white and no 3-D borders will be generated. The default is 1.

> **-height** *pixels*

> > Sets the height of the plot. This lets you print the graph with a height different from the one drawn on the screen. If pixels is 0, the height is the same as the widget's height. The default is 0.

> **-landscape** *boolean*

> > If boolean is true, this specifies the printed area is to be rotated 90 degrees. In non-rotated output the X-axis of the printed area runs along the short dimension of the page (``portrait'' orientation); in rotated output the X-axis runs along the long dimension of the page (``landscape'' orientation). Defaults to 0.

> **-maxpect** *boolean*

> > Indicates to scale the plot so that it fills the PostScript page. The aspect ratio of the graph is still retained. The default is 0.

> **-padx** *pad*

> > Sets the horizontal padding for the left and right page borders. The borders are exterior to the plot. Pad can be a list of one or two screen distances. If pad has two elements, the left border is padded by the first distance and the right border by the second. If pad has just one distance, both the left and right borders are padded evenly. The default is 1i.

> **-pady** *pad*

> > Sets the vertical padding for the top and bottom page borders. The borders are exterior to the plot. Pad can be a list of one or two screen distances. If pad has two elements, the top border is padded by the first distance and the bottom border by the second. If pad has just one distance, both the top and bottom borders are padded evenly. The default is 1i.

> **-paperheight** *pixels*

> > Sets the height of the postscript page. This can be used to select between different page sizes (letter, A4, etc). The default height is 11.0i.

> **-paperwidth** *pixels*

> > Sets the width of the postscript page. This can be used to select between different page sizes (letter, A4, etc). The default width is 8.5i.

> **-width** *pixels*

> > Sets the width of the plot. This lets you generate a plot of a width different from that of the widget. If pixels is 0, the width is the same as the widget's width. The default is 0.

> Postscript configuration options may be also be set by the option command. The resource name and class are postscript and Postscript respectively.

> > <code>
        option add *Graph.postscript.Decorations false  
        option add *Graph.Postscript.Landscape   true  
</code>

<a name="pathName-postscript-output"></a>
**pathName postscript output** *?fileName? ?option value?...*

> Outputs a file of encapsulated PostScript. If a fileName argument isn't present, the command returns the PostScript. If any option-value pairs are present, they set configuration options controlling how the PostScript is generated. Option and value can be anything accepted by the postscript configure operation above.

<a name="MARKER-COMPONENT"></a>
## MARKER COMPONENT

Markers are simple drawing procedures used to annotate or highlight areas of the graph. Markers have various types: text strings, bitmaps, images, connected lines, windows, or polygons. They can be associated with a particular element, so that when the element is hidden or un-hidden, so is the marker. By default, markers are the last items drawn, so that data elements will appear in behind them. You can change this by configuring the -under option.

Markers, in contrast to elements, don't affect the scaling of the coordinate axes. They can also have elastic coordinates (specified by -Inf and Inf respectively) that translate into the minimum or maximum limit of the axis. For example, you can place a marker so it always remains in the lower left corner of the plotting area, by using the coordinates -Inf,-Inf.

The following operations are available for markers.

<a name="pathName-marker-after"></a>
**pathName marker after markerId ?afterId?
    Changes the order of the markers, drawing the first marker after the second. If no second afterId argument is specified, the marker is placed at the end of the display list. This command can be used to control how markers are displayed since markers are drawn in the order of this display list.

<a name="pathName-marker-before"></a>
**pathName marker before** *markerId ?beforeId?*

> Changes the order of the markers, drawing the first marker before the second. If no second beforeId argument is specified, the marker is placed at the beginning of the display list. This command can be used to control how markers are displayed since markers are drawn in the order of this display list.

<a name="pathName-marker-bind"></a>
**pathName marker bind** *tagName ?sequence? ?command?*

> Associates command with tagName such that whenever the event sequence given by sequence occurs for a marker with this tag, command will be invoked. The syntax is similar to the bind command except that it operates on graph markers, rather than widgets. See the bind manual entry for complete details on sequence and the substitutions performed on command before invoking it.

> If all arguments are specified then a new binding is created, replacing any existing binding for the same sequence and tagName. If the first character of command is + then command augments an existing binding rather than replacing it. If no command argument is provided then the command currently associated with tagName and sequence (it's an error occurs if there's no such binding) is returned. If both command and sequence are missing then a list of all the event sequences for which bindings have been defined for tagName.

<a name="pathName-marker-cget"></a>
**pathName marker cget** *option*

> Returns the current value of the marker configuration option given by option. Option may be any option described below in the configure operation.

<a name="pathName-marker-configure"></a>
**pathName marker configure** *markerId ?option value?...*

> Queries or modifies the configuration options for markers. If option isn't specified, a list describing the current options for markerId is returned. If option is specified, but not value, then a list describing option is returned. If one or more option and value pairs are specified, then for each pair, the marker option option is set to value.

> The following options are valid for all markers. Each type of marker also has its own type-specific options. They are described in the sections below.

> **-bindtags** *tagList*

> > Specifies the binding tags for the marker. TagList is a list of binding tag names. The tags and their order will determine how events for markers are handled. Each tag in the list matching the current event sequence will have its Tcl command executed. Implicitly the name of the marker is always the first tag in the list. The default value is all.

> **-coords** *coordList*

> > Specifies the coordinates of the marker. CoordList is a list of graph coordinates. The number of coordinates required is dependent on the type of marker. Text, image, and window markers need only two coordinates (an X-Y coordinate). Bitmap markers can take either two or four coordinates (if four, they represent the corners of the bitmap). Line markers need at least four coordinates, polygons at least six. If coordList is "", the marker will not be displayed. The default is "".

> **-element** *elemName*

> > Links the marker with the element elemName. The marker is drawn only if the element is also currently displayed (see the element's show operation). If elemName is "", the marker is always drawn. The default is "".

> **-hide** *boolean*

> > Indicates whether the marker is drawn. If boolean is true, the marker is not drawn. The default is no.

> **-mapx** *xAxis*

> > Specifies the X-axis to map the marker's X-coordinates onto. XAxis must the name of an axis. The default is x.

> **-mapy** *yAxis*

> > Specifies the Y-axis to map the marker's Y-coordinates onto. YAxis must the name of an axis. The default is y.

> **-name** *markerId*

> > Changes the identifier for the marker. The identifier markerId can not already be used by another marker. If this option isn't specified, the marker's name is uniquely generated.

> **-under** *boolean*

> > Indicates whether the marker is drawn below/above data elements. If boolean is true, the marker is be drawn underneath the data element symbols and lines. Otherwise, the marker is drawn on top of the element. The default is 0.

> **-xoffset** *pixels*

> > Specifies a screen distance to offset the marker horizontally. Pixels is a valid screen distance, such as 2 or 1.2i. The default is 0.

> **-yoffset** *pixels*

> > Specifies a screen distance to offset the markers vertically. Pixels is a valid screen distance, such as 2 or 1.2i. The default is 0.

> Marker configuration options may also be set by the option command. The resource class is either BitmapMarker, ImageMarker, LineMarker, PolygonMarker, TextMarker, or WindowMarker, depending on the type of marker. The resource name is the name of the marker.

> > <code>
        option add *Graph.TextMarker.Foreground white  
        option add *Graph.BitmapMarker.Foreground white  
        option add *Graph.m1.Background     blue  
</code>

<a name="pathName-marker-create"></a>
**pathName marker create** *type ?option value?...*

> Creates a marker of the selected type. Type may be either text, line, bitmap, image, polygon, or window. This command returns the marker identifier, used as the markerId argument in the other marker-related commands. If the -name option is used, this overrides the normal marker identifier. If the name provided is already used for another marker, the new marker will replace the old.

<a name="pathName-marker-delete"></a>
**pathName marker delete** *?name?...*

> Removes one of more markers. The graph will automatically be redrawn without the marker.

<a name="pathName-marker-exists"></a>
**pathName marker exists** *markerId*

> Returns 1 if the marker markerId exists and 0 otherwise.

<a name="pathName-marker-names"></a>
**pathName marker names** *?pattern?*

> Returns the names of all the markers that currently exist. If pattern is supplied, only those markers whose names match it will be returned.

<a name="pathName-marker-type"></a>
**pathName marker type** *markerId*

> Returns the type of the marker given by markerId, such as line or text. If markerId is not a valid a marker identifier, "" is returned.

<a name="BITMAP-MARKERS"></a>
### BITMAP MARKERS

A bitmap marker displays a bitmap. The size of the bitmap is controlled by the number of coordinates specified. If two coordinates, they specify the position of the top-left corner of the bitmap. The bitmap retains its normal width and height. If four coordinates, the first and second pairs of coordinates represent the corners of the bitmap. The bitmap will be stretched or reduced as necessary to fit into the bounding rectangle.

Bitmap markers are created with the marker's create operation in the form:

<a name="pathName-marker-create-bitmap"></a>
**pathName marker create bitmap** *?option value?...*

There may be many option-value pairs, each sets a configuration options for the marker. These same option-value pairs may be used with the marker's configure operation.

The following options are specific to bitmap markers:

> **-background** *color*

> > Same as the -fill option.

> **-bitmap** *bitmap*

> > Specifies the bitmap to be displayed. If bitmap is "", the marker will not be displayed. The default is "".

> **-fill** *color*

> > Sets the background color of the bitmap. If color is the empty string, no background will be transparent. The default background color is "".

> **-foreground** *color*

> > Same as the -outline option.

> **-mask** *mask*

> > Specifies a mask for the bitmap to be displayed. This mask is a bitmap itself, denoting the pixels that are transparent. If mask is "", all pixels of the bitmap will be drawn. The default is "".

> **-outline** *color*

> > Sets the foreground color of the bitmap. The default value is black.

> **-rotate** *theta*

> > Sets the rotation of the bitmap. Theta is a real number representing the angle of rotation in degrees. The marker is first rotated and then placed according to its anchor position. The default rotation is 0.0.

<a name="IMAGE-MARKERS"></a>
### IMAGE MARKERS

A image marker displays an image. Image markers are created with the marker's create operation in the form:

<a name="pathName-marker-create-image"></a>
**pathName marker create image** *?option value?...*

There may be many option-value pairs, each sets a configuration option for the marker. These same option-value pairs may be used with the marker's configure operation.

The following options are specific to image markers:

> **-anchor** *anchor*

> > Anchor tells how to position the image relative to the positioning point for the image. For example, if anchor is center then the image is centered on the point; if anchor is n then the image will be drawn such that the top center point of the rectangular region occupied by the image will be at the positioning point. This option defaults to center.

> **-image** *image*

> > Specifies the image to be drawn. If image is "", the marker will not be drawn. The default is "".

<a name="LINE-MARKERS"></a>
### LINE MARKERS

A line marker displays one or more connected line segments. Line markers are created with marker's create operation in the form:

<a name="pathName-marker-create-line"></a>
**pathName marker create line** *?option value?...*

There may be many option-value pairs, each sets a configuration option for the marker. These same option-value pairs may be used with the marker's configure operation.

The following options are specific to line markers:

> **-dashes** *dashList*

> > Sets the dash style of the line. DashList is a list of up to 11 numbers that alternately represent the lengths of the dashes and gaps on the line. Each number must be between 1 and 255. If dashList is "", the marker line will be solid.

> **-fill** *color*

> > Sets the background color of the line. This color is used with striped lines (see the -fdashes option). If color is the empty string, no background color is drawn (the line will be dashed, not striped). The default background color is "".

> **-linewidth** *pixels*

> > Sets the width of the lines. The default width is 0.

> **-outline** *color*

> > Sets the foreground color of the line. The default value is black.

> **-stipple** *bitmap*

> > Specifies a stipple pattern used to draw the line, rather than a solid line. Bitmap specifies a bitmap to use as the stipple pattern. If bitmap is "", then the line is drawn in a solid fashion. The default is "".

<a name=">POLYGON-MARKERS"></a>
### POLYGON MARKERS

A polygon marker displays a closed region described as two or more connected line segments. It is assumed the first and last points are connected. Polygon markers are created using the marker create operation in the form:

<a name="pathName-marker-create-polygon"></a>
**pathName marker create polygon** *?option value?...*

There may be many option-value pairs, each sets a configuration option for the marker. These same option-value pairs may be used with the marker configure command to change the marker's configuration. The following options are supported for polygon markers:

> **-dashes** *dashList*
    Sets the dash style of the outline of the polygon. DashList is a list of up to 11 numbers that alternately represent the lengths of the dashes and gaps on the outline. Each number must be between 1 and 255. If dashList is "", the outline will be a solid line.

> **-fill** *color*

> > Sets the fill color of the polygon. If color is "", then the interior of the polygon is transparent. The default is white.

> **-linewidth** *pixels*

> > Sets the width of the outline of the polygon. If pixels is zero, no outline is drawn. The default is 0.

> **-outline** *color*

> > Sets the color of the outline of the polygon. If the polygon is stippled (see the -stipple option), then this represents the foreground color of the stipple. The default is black.

> **-stipple** *bitmap*

> > Specifies that the polygon should be drawn with a stippled pattern rather than a solid color. Bitmap specifies a bitmap to use as the stipple pattern. If bitmap is "", then the polygon is filled with a solid color (if the -fill option is set). The default is "".

<a name=TEXT-MARKERS"></a>
### TEXT MARKERS

A text marker displays a string of characters on one or more lines of text. Embedded newlines cause line breaks. They may be used to annotate regions of the graph. Text markers are created with the create operation in the form:

<a name="pathName-marker-create-text"></a>
**pathName marker create text** *?option value?...*

There may be many option-value pairs, each sets a configuration option for the text marker. These same option-value pairs may be used with the marker's configure operation.

The following options are specific to text markers:

> **-anchor** *anchor*

> > Anchor tells how to position the text relative to the positioning point for the text. For example, if anchor is center then the text is centered on the point; if anchor is n then the text will be drawn such that the top center point of the rectangular region occupied by the text will be at the positioning point. This default is center.

> **-background** *color*

> > Same as the -fill option.

> **-font** *fontName*

> > Specifies the font of the text. The default is `*-Helvetica-Bold-R-Normal-*-120-*`.

> **-fill** *color*

> > Sets the background color of the text. If color is the empty string, no background will be transparent. The default background color is "".

> **-foreground** *color*

> > Same as the -outline option.

> **-justify** *justify*

> > Specifies how the text should be justified. This matters only when the marker contains more than one line of text. Justify must be left, right, or center. The default is center.

> **-outline** *color*

> > Sets the color of the text. The default value is black.

> **-padx** *pad*

> > Sets the padding to the left and right exteriors of the text. Pad can be a list of one or two screen distances. If pad has two elements, the left side of the text is padded by the first distance and the right side by the second. If pad has just one distance, both the left and right sides are padded evenly. The default is 4.

> **-pady** *pad*

> > Sets the padding above and below the text. Pad can be a list of one or two screen distances. If pad has two elements, the area above the text is padded by the first distance and the area below by the second. If pad is just one distance, both the top and bottom areas are padded evenly. The default is 4.

> **-rotate** *theta*

> > Specifies the number of degrees to rotate the text. Theta is a real number representing the angle of rotation. The marker is first rotated along its center and is then drawn according to its anchor position. The default is 0.0.

> **-text** *text*

> > Specifies the text of the marker. The exact way the text is displayed may be affected by other options such as -anchor or -rotate.

<a name="WINDOW-MARKERS"></a>
### WINDOW MARKERS

A window marker displays a widget at a given position. Window markers are created with the marker's create operation in the form:

<a name="pathName-marker-create-window"></a>
**pathName marker create window** *?option value?...*

There may be many option-value pairs, each sets a configuration option for the marker. These same option-value pairs may be used with the marker's configure command.

The following options are specific to window markers:

> **-anchor** *anchor*

> > Anchor tells how to position the widget relative to the positioning point for the widget. For example, if anchor is center then the widget is centered on the point; if anchor is n then the widget will be displayed such that the top center point of the rectangular region occupied by the widget will be at the positioning point. This option defaults to center.

> **-height** *pixels*

> > Specifies the height to assign to the marker's window. If this option isn't specified, or if it is specified as "", then the window is given whatever height the widget requests internally.

> **-width** *pixels*

> > Specifies the width to assign to the marker's window. If this option isn't specified, or if it is specified as "", then the window is given whatever width the widget requests internally.

> **-window** *pathName*

> > Specifies the widget to be managed by the graph. PathName must be a child of the graph widget.

<a name="GRAPH-COMPONENT-BINDINGS"></a>
## GRAPH COMPONENT BINDINGS

Specific graph components, such as elements, markers and legend entries, can have a command trigger when event occurs in them, much like canvas items in Tk's canvas widget. Not all event sequences are valid. The only binding events that may be specified are those related to the mouse and keyboard (such as Enter, Leave, ButtonPress, Motion, and KeyPress).

Only one element or marker can be picked during an event. This means, that if the mouse is directly over both an element and a marker, only the uppermost component is selected. This isn't true for legend entries. Both a legend entry and an element (or marker) binding commands will be invoked if both items are picked.

It is possible for multiple bindings to match a particular event. This could occur, for example, if one binding is associated with the element name and another is associated with one of the element's tags (see the -bindtags option). When this occurs, all of the matching bindings are invoked. A binding associated with the element name is invoked first, followed by one binding for each of the element's bindtags. If there are multiple matching bindings for a single tag, then only the most specific binding is invoked. A continue command in a binding script terminates that script, and a break command terminates that script and skips any remaining scripts for the event, just as for the bind command.

The -bindtags option for these components controls addition tag names which can be matched. Implicitly elements and markers always have tags matching their names. Setting the value of the -bindtags option doesn't change this.

Some common bindings can be set with the [pathName binding](#pathName-binding) command.

## <a name="C-LANGUAGE-API"></a>
## C LANGUAGE API 

You can manipulate data elements from the C language. There may be situations where it is too expensive to translate the data values from ASCII strings. Or you might want to read data in a special file format.

Data can manipulated from the C language using graph vectors. You specify the X-Y data coordinates of an element as vectors and manipulate the vector from C. The graph will be redrawn automatically after the vectors are updated.

From Tcl, create the vectors and configure the element to use them.

> <code>
	vector X Y  
	.g element configure line1 -xdata X -ydata Y  
</code>

To set data points from C, you pass the values as arrays of doubles using the Rbc_ResetVector call. The vector is reset with the new data and at the next idle point (when Tk re-enters its event loop), the graph will be redrawn automatically.

> <code>
	#include "tcl.h"  
	#include "rbcInt.h"  

	register int i;  
	Rbc_Vector *xVec, *yVec;  
	double x[50], y[50];  

	/* Get the graph vectors "X" and "Y" (created above from Tcl) */  
	if ((Rbc_GetVector(interp, "X", &xVec) != TCL_OK) ||  
	    (Rbc_GetVector(interp, "Y", &yVec) != TCL_OK)) {  
	    return TCL_ERROR;  
 	}  

	for (i = 0; i < 50; i++) {  
	    x[i] = i * 0.02;  
	    y[i] = sin(x[i]);  
	}  

	/* Put the data into graph vectors */  
	if ((Rbc_ResetVector(xVec, x, 50, 50, TCL_VOLATILE) != TCL_OK) ||  
	    (Rbc_ResetVector(yVec, y, 50, 50, TCL_VOLATILE) != TCL_OK)) {  
	    return TCL_ERROR;  
	}  
</code>

See the vector manual page for more details.

<a name="SPEED-TIPS"></a>
## SPEED TIPS

There may be cases where the graph needs to be drawn and updated as quickly as possible. If drawing speed becomes a big problem, here are a few tips to speed up displays.

Try to minimize the number of data points. The more data points the looked at, the more work the graph must do.

If your data is generated as floating point values, the time required to convert the data values to and from ASCII strings can be significant, especially when there any many data points. You can avoid the redundant string-to-decimal conversions using the C API to graph vectors.

Data elements without symbols are drawn faster than with symbols. Set the data element's -symbol option to none. If you need to draw symbols, try using the simple symbols such as splus and scross.

Don't stipple or dash the element. Solid lines are much faster.

If you update data elements frequently, try turning off the widget's -bufferelements option. When the graph is first displayed, it draws data elements into an internal pixmap. The pixmap acts as a cache, so that when the graph needs to be redrawn again, and the data elements or coordinate axes haven't changed, the pixmap is simply copied to the screen. This is especially useful when you are using markers to highlight points and regions on the graph. But if the graph is updated frequently, changing either the element data or coordinate axes, the buffering becomes redundant.

## <a name="LIMITATIONS"></a>
## LIMITATIONS

Auto-scale routines do not use requested min/max limits as boundaries when the axis is logarithmically scaled.

The PostScript output generated for polygons with more than 1500 points may exceed the limits of some printers (See PostScript Language Reference Manual, page 568). The work-around is to break the polygon into separate pieces.

<a name="EXAMPLE"></a>
## EXAMPLE

The graph command creates a new graph.

> <code>
	# Create a new graph.  Plotting area is black.  
	graph .g -plotbackground black
</code>

A new Tcl command .g is also created. This command can be used to query and modify the graph. For example, to change the title of the graph to "My Plot", you use the new command and the graph's configure operation.

> <code>
	# Change the title.  
	.g configure -title "My Plot"
</code>

A graph has several components. To access a particular component you use the component's name. For example, to add data elements, you use the new command and the element component.

> <code>
	# Create a new element named "line1"  
	.g element create line1 \\  
	-xdata { 0.2 0.4 0.6 0.8 1.0 1.2 1.4 1.6 1.8 2.0 } \\  
	-ydata { 26.18 50.46 72.85 93.31 111.86 128.47 143.14 155.85 166.60 175.38 }
</code>

The element's X-Y coordinates are specified using lists of numbers. Alternately, graph vectors could be used to hold the X-Y coordinates.

> <code>
	# Create two vectors and add them to the graph.
	vector xVec yVec  
	xVec set { 0.2 0.4 0.6 0.8 1.0 1.2 1.4 1.6 1.8 2.0 }  
	yVec set { 26.18 50.46 72.85 93.31 111.86 128.47 143.14 155.85 166.60 175.38 }  
	.g element create line1 -xdata xVec -ydata yVec  
</code>

The advantage of using vectors is that when you modify one, the graph is automatically redrawn to reflect the new values.

> <code>
	# Change the y coordinate of the first point.  
	set yVector(0) 25.18
</code>

An element named e1 is now created in .b. It is automatically added to the display list of elements. You can use this list to control in what order elements are displayed. To query or reset the element display list, you use the element's show operation.

> <code>
	# Get the current display list 	
	set elemList [.b element show]  
	# Remove the first element so it won't be displayed.  
	.b element show [lrange $elemList 0 end]  
</code>

The element will be displayed by as many bars as there are data points (in this case there are ten). The bars will be drawn centered at the x-coordinate of the data point. All the bars will have the same attributes (colors, stipple, etc). The width of each bar is by default one unit. You can change this with using the -barwidth option.

> <code>
	# Change the X-Y coordinates of the first point.  
	set xVec(0) 0.18  
	set yVec(0) 25.18  
</code>

An element named line1 is now created in .g. By default, the element's label in the legend will be also line1. You can change the label, or specify no legend entry, again using the element's configure operation.

> <code>
	# Don't display "line1" in the legend.  
	.g element configure line1 -label ""  
</code>

You can configure more than just the element's label. An element has many attributes such as symbol type and size, dashed or solid lines, colors, line width, etc.

> <code>
	# Configure line1  
	.g element configure line1 -symbol square -color red -dashes { 2 4 2 } -linewidth 2 -pixels 2c  
</code>

Four coordinate axes are automatically created: x, x2, y, and y2. And by default, elements are mapped onto the axes x and y. This can be changed with the -mapx and -mapy options.

> <code>
	# Map "line1" on the alternate Y-axis "y2".  
	.g element configure line1 -mapy y2  
</code>
Axes can be configured in many ways too. For example, you change the scale of the Y-axis from linear to log using the axis component.

> <code>
	# Y-axis is log scale.  
	.g axis configure y -logscale yes  
</code>

One important way axes are used is to zoom in on a particular data region. Zooming is done by simply specifying new axis limits using the -min and -max configuration options.

> <code>
	.g axis configure x -min 1.0 -max 1.5  
	.g axis configure y -min 12.0 -max 55.15  
</code>

To zoom interactively, you link the axis configure operations with some user interaction (such as pressing the mouse button), using the bind command. To convert between screen and graph coordinates, use the invtransform operation.

> <code>
	# Click the button to set a new minimum  
	bind .g <ButtonPress-1> {  
    		%W axis configure x -min [%W axis invtransform x %x]  
    		%W axis configure x -min [%W axis invtransform x %y]  
	}  
</code>

By default, the limits of the axis are determined from data values. To reset back to the default limits, set the -min and -max options to the empty value.

> <code>
	# Reset the axes to autoscale again.  
	.g axis configure x -min {} -max {}  
	.g axis configure y -min {} -max {}  
</code>

By default, the legend is drawn in the right margin. You can change this or any legend configuration options using the legend component.

> <code>
	# Configure the legend font, color, and relief  
	.g legend configure -position left -relief raised -font fixed -fg blue  
</code>

To prevent the legend from being displayed, turn on the -hide option.

> <code>
	# Don't display the legend.  
	.g legend configure -hide yes  
</code>

The graph widget has simple drawing procedures called markers. They can be used to highlight or annotate data in the graph. The types of markers available are bitmaps, images, polygons, lines, or windows. Markers can be used, for example, to mark or brush points. In this example, is a text marker that labels the data first point. Markers are created using the marker component.

> <code>
	# Create a label for the first data point of "line1".  
	.g marker create text -name first_marker -coords { 0.2 26.18 } \\  
	-text "start" -anchor se -xoffset -10 -yoffset -10  
</code>
This creates a text marker named first_marker. It will display the text "start" near the coordinates of the first data point. The -anchor, -xoffset, and -yoffset options are used to display the marker above and to the left of the data point, so that the data point isn't covered by the marker. By default, markers are drawn last, on top of data. You can change this with the -under option.

> <code>
	# Draw the label before elements are drawn.  
	.g marker configure first_marker -under yes  
</code>

You can add cross hairs or grid lines using the crosshairs and grid components.

> <code>
	# Display both cross hairs and grid lines.  
	.g crosshairs configure -hide no -color red  
	.g grid configure -hide no -dashes { 2 2 }  

	# Set up a binding to reposition the crosshairs.  
	bind .g <Motion> {  
    		.g crosshairs configure -position @%x,%y  
	}  
</code>

The crosshairs are repositioned as the mouse pointer is moved in the graph. The pointer X-Y coordinates define the center of the crosshairs.

Finally, to get hardcopy of the graph, use the postscript component.

> <code>
	# Print the graph into file "file.ps"  
	.g postscript output file.ps -maxpect yes -decorations no  
</code>

This generates a file file.ps containing the encapsulated PostScript of the graph. The option -maxpect says to scale the plot to the size of the page. Turning off the -decorations option denotes that no borders or color backgrounds should be drawn (i.e. the background of the margins, legend, and plotting area will be white).

<a name="KEYWORDS"></a>
## KEYWORDS

graph, widget

<a name="COPYRIGHT"></a>
## COPYRIGHT

&copy; 1995-1997 Roger E. Critchlow Jr.

&copy; 2001 George A. Howlett.

&copy; 2018 René Zaumseil <r.zaumseil@freenet.de>

