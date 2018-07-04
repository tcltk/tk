/*
 * rbcGrMarker.c --
 *
 *      This module implements markers for the rbc graph widget.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

#define MAX_OUTLINE_POINTS	12

/* Map graph coordinates to normalized coordinates [0..1] */
#define NORMALIZE(A,x) 	(((x) - (A)->axisRange.min) / (A)->axisRange.range)

#define DEF_MARKER_ANCHOR       "center"
#define DEF_MARKER_BACKGROUND   "white"
#define DEF_MARKER_BG_MONO      "white"
#define DEF_MARKER_BITMAP       (char *)NULL
#define DEF_MARKER_CAP_STYLE    "butt"
#define DEF_MARKER_COORDS       (char *)NULL
#define DEF_MARKER_DASHES       (char *)NULL
#define DEF_MARKER_DASH_OFFSET  "0"
#define DEF_MARKER_ELEMENT      (char *)NULL
#define DEF_MARKER_FOREGROUND   "black"
#define DEF_MARKER_FG_MONO      "black"
#define DEF_MARKER_FILL_COLOR   "red"
#define DEF_MARKER_FILL_MONO    "white"
#define DEF_MARKER_FONT         RBC_FONT
#define DEF_MARKER_GAP_COLOR    "pink"
#define DEF_MARKER_GAP_MONO     "black"
#define DEF_MARKER_HEIGHT       "0"
#define DEF_MARKER_HIDE         "no"
#define DEF_MARKER_JOIN_STYLE   "miter"
#define DEF_MARKER_JUSTIFY      "left"
#define DEF_MARKER_LINE_WIDTH   "1"
#define DEF_MARKER_MAP_X        "x"
#define DEF_MARKER_MAP_Y        "y"
#define DEF_MARKER_NAME         (char *)NULL
#define DEF_MARKER_OUTLINE_COLOR    "black"
#define DEF_MARKER_OUTLINE_MONO "black"
#define DEF_MARKER_PAD          "4"
#define DEF_MARKER_ROTATE       "0.0"
#define DEF_MARKER_SCALE        "1.0"
#define DEF_MARKER_SHADOW_COLOR (char *)NULL
#define DEF_MARKER_SHADOW_MONO  (char *)NULL
#define DEF_MARKER_STATE        "normal"
#define DEF_MARKER_STIPPLE      (char *)NULL
#define DEF_MARKER_TEXT         (char *)NULL
#define DEF_MARKER_UNDER        "no"
#define DEF_MARKER_WIDTH        "0"
#define DEF_MARKER_WINDOW       (char *)NULL
#define DEF_MARKER_XOR          "no"
#define DEF_MARKER_X_OFFSET     "0"
#define DEF_MARKER_Y_OFFSET     "0"

#define DEF_MARKER_TEXT_TAGS    "Text all"
#define DEF_MARKER_IMAGE_TAGS   "Image all"
#define DEF_MARKER_BITMAP_TAGS  "Bitmap all"
#define DEF_MARKER_WINDOW_TAGS  "Window all"
#define DEF_MARKER_POLYGON_TAGS "Polygon all"
#define DEF_MARKER_LINE_TAGS    "Line all"

static Tk_OptionParseProc StringToCoordinates;
static Tk_OptionPrintProc CoordinatesToString;
static Tk_CustomOption coordsOption = {
    StringToCoordinates, CoordinatesToString, (ClientData) 0
};

extern Tk_CustomOption rbcColorPairOption;
extern Tk_CustomOption rbcDashesOption;
extern Tk_CustomOption rbcDistanceOption;
extern Tk_CustomOption rbcListOption;
extern Tk_CustomOption rbcPadOption;
extern Tk_CustomOption rbcPositiveDistanceOption;
extern Tk_CustomOption rbcShadowOption;
extern Tk_CustomOption rbcStateOption;
extern Tk_CustomOption rbcXAxisOption;
extern Tk_CustomOption rbcYAxisOption;

typedef RbcMarker *(
    MarkerCreateProc) (
    void);
typedef void    (
    MarkerDrawProc) (
    RbcMarker * markerPtr,
    Drawable drawable);
typedef void    (
    MarkerFreeProc) (
    RbcGraph * graphPtr,
    RbcMarker * markerPtr);
typedef int     (
    MarkerConfigProc) (
    RbcMarker * markerPtr);
typedef void    (
    MarkerMapProc)  (
    RbcMarker * markerPtr);
typedef void    (
    MarkerPostScriptProc) (
    RbcMarker * markerPtr,
    RbcPsToken * psToken);
typedef int     (
    MarkerPointProc) (
    RbcMarker * markerPtr,
    RbcPoint2D * samplePtr);
typedef int     (
    MarkerRegionProc) (
    RbcMarker * markerPtr,
    RbcExtents2D * extsPtr,
    int enclosed);

typedef struct {
    Tk_ConfigSpec  *configSpecs;        /* Marker configuration specifications */
    MarkerConfigProc *configProc;
    MarkerDrawProc *drawProc;
    MarkerFreeProc *freeProc;
    MarkerMapProc  *mapProc;
    MarkerPointProc *pointProc;
    MarkerRegionProc *regionProc;
    MarkerPostScriptProc *postscriptProc;

} MarkerClass;

/*
 * -------------------------------------------------------------------
 *
 * Marker --
 *
 *      Structure defining the generic marker.  In C++ parlance this
 *      would be the base type from which all markers are derived.
 *
 *      This structure corresponds with the specific types of markers.
 *      Don't change this structure without changing the individual
 *      marker structures of each type below.
 *
 * -------------------------------------------------------------------
 */
typedef struct RbcMarker {
    char           *name;       /* Identifier for marker in list */
    RbcUid          classUid;   /* Type of marker. */
    RbcGraph       *graphPtr;   /* Graph widget of marker. */
    unsigned int    flags;
    char          **tags;
    int             hidden;     /* If non-zero, don't display the marker. */
    Tcl_HashEntry  *hashPtr;
    RbcChainLink   *linkPtr;
    RbcPoint2D     *worldPts;   /* Coordinate array to position marker */
    int             nWorldPts;  /* Number of points in above array */
    char           *elemName;   /* Element associated with marker */
    RbcAxis2D       axes;
    int             drawUnder;  /* If non-zero, draw the marker
                                 * underneath any elements. This can
                                 * be a performance penalty because
                                 * the graph must be redraw entirely
                                 * each time the marker is redrawn. */
    int             clipped;    /* Indicates if the marker is totally
                                 * clipped by the plotting area. */
    int             xOffset, yOffset;   /* Pixel offset from graph position */
    MarkerClass    *classPtr;
    int             state;
} RbcMarker;

/*
 * -------------------------------------------------------------------
 *
 * TextMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    char           *name;       /* Identifier for marker */
    RbcUid          classUid;   /* Type of marker */
    RbcGraph       *graphPtr;   /* The graph this marker belongs to */
    unsigned int    flags;
    char          **tags;
    int             hidden;     /* If non-zero, don't display the
                                 * marker. */
    Tcl_HashEntry  *hashPtr;
    RbcChainLink   *linkPtr;
    RbcPoint2D     *worldPts;   /* Position of marker (1 X-Y coordinate) in
                                 * world (graph) coordinates. */
    int             nWorldPts;  /* Number of points */
    char           *elemName;   /* Element associated with marker */
    RbcAxis2D       axes;
    int             drawUnder;  /* If non-zero, draw the marker
                                 * underneath any elements. There can
                                 * be a performance because the graph
                                 * must be redraw entirely each time
                                 * this marker is redrawn. */
    int             clipped;    /* Indicates if the marker is totally
                                 * clipped by the plotting area. */
    int             xOffset, yOffset;   /* pixel offset from anchor */
    MarkerClass    *classPtr;
    int             state;

    /*
     * Text specific fields and attributes
     */
    char           *string;     /* Text string to be display.  The string
                                 * make contain newlines. */
    Tk_Anchor       anchor;     /* Indicates how to translate the given
                                 * marker position. */
    RbcPoint2D      anchorPos;  /* Translated anchor point. */
    int             width, height;      /* Dimension of bounding box.  */
    RbcTextStyle    style;      /* Text attributes (font, fg, anchor, etc) */
    RbcTextLayout  *textPtr;    /* Contains information about the layout
                                 * of the text. */
    RbcPoint2D      outline[5];
    XColor         *fillColor;
    GC              fillGC;
} TextMarker;

static Tk_ConfigSpec textConfigSpecs[] = {
    {TK_CONFIG_ANCHOR, "-anchor", "anchor", "Anchor", DEF_MARKER_ANCHOR,
        Tk_Offset(TextMarker, anchor), 0},
    {TK_CONFIG_COLOR, "-background", "background", "MarkerBackground",
        (char *) NULL, Tk_Offset(TextMarker, fillColor), TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-bg", "background", "Background", (char *) NULL, 0, 0},
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags",
            DEF_MARKER_TEXT_TAGS, Tk_Offset(RbcMarker, tags), TK_CONFIG_NULL_OK,
        &rbcListOption},
    {TK_CONFIG_CUSTOM, "-coords", "coords", "Coords", DEF_MARKER_COORDS,
        Tk_Offset(RbcMarker, worldPts), TK_CONFIG_NULL_OK, &coordsOption},
    {TK_CONFIG_STRING, "-element", "element", "Element", DEF_MARKER_ELEMENT,
        Tk_Offset(RbcMarker, elemName), TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-fg", "foreground", "Foreground", (char *) NULL, 0, 0},
    {TK_CONFIG_SYNONYM, "-fill", "background", (char *) NULL, (char *) NULL, 0,
        0},
    {TK_CONFIG_FONT, "-font", "font", "Font", DEF_MARKER_FONT,
        Tk_Offset(TextMarker, style.font), 0},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground",
            DEF_MARKER_FOREGROUND, Tk_Offset(TextMarker, style.color),
        TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground",
            DEF_MARKER_FG_MONO, Tk_Offset(TextMarker, style.color),
        TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_JUSTIFY, "-justify", "justify", "Justify", DEF_MARKER_JUSTIFY,
        Tk_Offset(TextMarker, style.justify), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE,
        Tk_Offset(RbcMarker, hidden), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X,
        Tk_Offset(RbcMarker, axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y,
        Tk_Offset(RbcMarker, axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_STRING, "-name", (char *) NULL, (char *) NULL, DEF_MARKER_NAME,
        Tk_Offset(RbcMarker, name), TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-outline", "foreground", (char *) NULL, (char *) NULL,
        0, 0},
    {TK_CONFIG_CUSTOM, "-padx", "padX", "PadX", DEF_MARKER_PAD,
            Tk_Offset(TextMarker, style.padX), TK_CONFIG_DONT_SET_DEFAULT,
        &rbcPadOption},
    {TK_CONFIG_CUSTOM, "-pady", "padY", "PadY", DEF_MARKER_PAD,
            Tk_Offset(TextMarker, style.padY), TK_CONFIG_DONT_SET_DEFAULT,
        &rbcPadOption},
    {TK_CONFIG_DOUBLE, "-rotate", "rotate", "Rotate", DEF_MARKER_ROTATE,
        Tk_Offset(TextMarker, style.theta), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-shadow", "shadow", "Shadow", DEF_MARKER_SHADOW_COLOR,
            Tk_Offset(TextMarker, style.shadow), TK_CONFIG_COLOR_ONLY,
        &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-shadow", "shadow", "Shadow", DEF_MARKER_SHADOW_MONO,
            Tk_Offset(TextMarker, style.shadow), TK_CONFIG_MONO_ONLY,
        &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-state", "state", "State", DEF_MARKER_STATE,
            Tk_Offset(RbcMarker, state), TK_CONFIG_DONT_SET_DEFAULT,
        &rbcStateOption},
    {TK_CONFIG_STRING, "-text", "text", "Text", DEF_MARKER_TEXT,
        Tk_Offset(TextMarker, string), TK_CONFIG_NULL_OK},
    {TK_CONFIG_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER,
        Tk_Offset(RbcMarker, drawUnder), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET,
        Tk_Offset(RbcMarker, xOffset), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET,
        Tk_Offset(RbcMarker, yOffset), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

/*
 * -------------------------------------------------------------------
 *
 * WindowMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    char           *name;       /* Identifier for marker */
    RbcUid          classUid;   /* Type of marker */
    RbcGraph       *graphPtr;   /* Graph marker belongs to */
    unsigned int    flags;
    char          **tags;
    int             hidden;     /* Indicates if the marker is
                                 * currently hidden or not. */
    Tcl_HashEntry  *hashPtr;
    RbcChainLink   *linkPtr;
    RbcPoint2D     *worldPts;   /* Position of marker (1 X-Y coordinate) in
                                 * world (graph) coordinates. */
    int             nWorldPts;  /* Number of points */
    char           *elemName;   /* Element associated with marker */
    RbcAxis2D       axes;
    int             drawUnder;  /* If non-zero, draw the marker
                                 * underneath any elements. There can
                                 * be a performance because the graph
                                 * must be redraw entirely each time
                                 * this marker is redrawn. */
    int             clipped;    /* Indicates if the marker is totally
                                 * clipped by the plotting area. */
    int             xOffset, yOffset;   /* Pixel offset from anchor. */
    MarkerClass    *classPtr;
    int             state;

    /*
     * Window specific attributes
     */
    char           *pathName;   /* Name of child widget to be displayed. */
    Tk_Window       tkwin;      /* Window to display. */
    int             reqWidth, reqHeight;        /* If non-zero, this overrides the size
                                                 * requested by the child widget. */
    Tk_Anchor       anchor;     /* Indicates how to translate the given
                                 * marker position. */
    RbcPoint2D      anchorPos;  /* Translated anchor point. */
    int             width, height;      /* Current size of the child window. */
} WindowMarker;

static Tk_ConfigSpec windowConfigSpecs[] = {
    {TK_CONFIG_ANCHOR, "-anchor", "anchor", "Anchor", DEF_MARKER_ANCHOR,
        Tk_Offset(WindowMarker, anchor), 0},
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags",
            DEF_MARKER_WINDOW_TAGS, Tk_Offset(RbcMarker, tags),
        TK_CONFIG_NULL_OK, &rbcListOption},
    {TK_CONFIG_CUSTOM, "-coords", "coords", "Coords", DEF_MARKER_COORDS,
            Tk_Offset(WindowMarker, worldPts), TK_CONFIG_NULL_OK,
        &coordsOption},
    {TK_CONFIG_STRING, "-element", "element", "Element", DEF_MARKER_ELEMENT,
        Tk_Offset(RbcMarker, elemName), TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-height", "height", "Height", DEF_MARKER_HEIGHT,
            Tk_Offset(WindowMarker, reqHeight), TK_CONFIG_DONT_SET_DEFAULT,
        &rbcPositiveDistanceOption},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE,
        Tk_Offset(RbcMarker, hidden), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X,
        Tk_Offset(RbcMarker, axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y,
        Tk_Offset(RbcMarker, axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_STRING, "-name", (char *) NULL, (char *) NULL, DEF_MARKER_NAME,
        Tk_Offset(RbcMarker, name), TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-state", "state", "State", DEF_MARKER_STATE,
            Tk_Offset(RbcMarker, state), TK_CONFIG_DONT_SET_DEFAULT,
        &rbcStateOption},
    {TK_CONFIG_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER,
        Tk_Offset(RbcMarker, drawUnder), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-width", "width", "Width", DEF_MARKER_WIDTH,
            Tk_Offset(WindowMarker, reqWidth), TK_CONFIG_DONT_SET_DEFAULT,
        &rbcPositiveDistanceOption},
    {TK_CONFIG_STRING, "-window", "window", "Window", DEF_MARKER_WINDOW,
        Tk_Offset(WindowMarker, pathName), TK_CONFIG_NULL_OK},
    {TK_CONFIG_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET,
        Tk_Offset(RbcMarker, xOffset), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET,
        Tk_Offset(RbcMarker, yOffset), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

/*
 * -------------------------------------------------------------------
 *
 * BitmapMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    char           *name;       /* Identifier for marker */
    RbcUid          classUid;   /* Type of marker */
    RbcGraph       *graphPtr;   /* Graph marker belongs to */
    unsigned int    flags;
    char          **tags;
    int             hidden;     /* Indicates if the marker is currently
                                 * hidden or not. */
    Tcl_HashEntry  *hashPtr;
    RbcChainLink   *linkPtr;
    RbcPoint2D     *worldPts;   /* Position of marker in world (graph)
                                 * coordinates. If 2 pairs of X-Y
                                 * coordinates are specified, then the
                                 * bitmap is resized to fit this area.
                                 * Otherwise if 1 pair, then the bitmap
                                 * is positioned at the coordinate at its
                                 * normal size. */
    int             nWorldPts;  /* Number of points */
    char           *elemName;   /* Element associated with marker */
    RbcAxis2D       axes;
    int             drawUnder;  /* If non-zero, draw the marker
                                 * underneath any elements. There can
                                 * be a performance because the graph
                                 * must be redraw entirely each time
                                 * this marker is redrawn. */
    int             clipped;    /* Indicates if the marker is totally
                                 * clipped by the plotting area. */
    int             xOffset, yOffset;   /* Pixel offset from origin of bitmap */
    MarkerClass    *classPtr;
    int             state;

    /* Bitmap specific attributes */
    Pixmap          srcBitmap;  /* Original bitmap. May be further
                                 * scaled or rotated. */
    double          rotate;     /* Requested rotation of the bitmap */
    double          theta;      /* Normalized rotation (0..360
                                 * degrees) */
    Tk_Anchor       anchor;     /* If only one X-Y coordinate is
                                 * given, indicates how to translate
                                 * the given marker position.  Otherwise,
                                 * if there are two X-Y coordinates, then
                                 * this value is ignored. */
    RbcPoint2D      anchorPos;  /* Translated anchor point. */
    XColor         *outlineColor;       /* Foreground color */
    XColor         *fillColor;  /* Background color */
    GC              gc;         /* Private graphic context */
    GC              fillGC;     /* Shared graphic context */
    Pixmap          destBitmap; /* Bitmap to be drawn. */
    int             destWidth, destHeight;      /* Dimensions of the final bitmap */
    RbcPoint2D      outline[MAX_OUTLINE_POINTS];        /* Polygon representing the background
                                                         * of the bitmap. */
    int             nOutlinePts;
} BitmapMarker;

static Tk_ConfigSpec bitmapConfigSpecs[] = {
    {TK_CONFIG_ANCHOR, "-anchor", "anchor", "Anchor",
        DEF_MARKER_ANCHOR, Tk_Offset(BitmapMarker, anchor), 0},
    {TK_CONFIG_COLOR, "-background", "background", "Background",
            DEF_MARKER_BACKGROUND, Tk_Offset(BitmapMarker, fillColor),
        TK_CONFIG_COLOR_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_COLOR, "-background", "background", "Background",
            DEF_MARKER_BG_MONO, Tk_Offset(BitmapMarker, fillColor),
        TK_CONFIG_MONO_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-bg", "background", (char *) NULL,
        (char *) NULL, 0, 0},
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags",
            DEF_MARKER_BITMAP_TAGS, Tk_Offset(RbcMarker, tags),
        TK_CONFIG_NULL_OK, &rbcListOption},
    {TK_CONFIG_BITMAP, "-bitmap", "bitmap", "Bitmap",
            DEF_MARKER_BITMAP, Tk_Offset(BitmapMarker, srcBitmap),
        TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-coords", "coords", "Coords",
            DEF_MARKER_COORDS, Tk_Offset(RbcMarker, worldPts),
        TK_CONFIG_NULL_OK, &coordsOption},
    {TK_CONFIG_STRING, "-element", "element", "Element",
            DEF_MARKER_ELEMENT, Tk_Offset(RbcMarker, elemName),
        TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-fg", "foreground", (char *) NULL,
        (char *) NULL, 0, 0},
    {TK_CONFIG_SYNONYM, "-fill", "background", (char *) NULL,
        (char *) NULL, 0, 0},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground",
            DEF_MARKER_FOREGROUND, Tk_Offset(BitmapMarker, outlineColor),
        TK_CONFIG_COLOR_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground",
            DEF_MARKER_FG_MONO, Tk_Offset(BitmapMarker, outlineColor),
        TK_CONFIG_MONO_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide",
            DEF_MARKER_HIDE, Tk_Offset(RbcMarker, hidden),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX",
        DEF_MARKER_MAP_X, Tk_Offset(RbcMarker, axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY",
        DEF_MARKER_MAP_Y, Tk_Offset(RbcMarker, axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_STRING, "-name", (char *) NULL, (char *) NULL,
        DEF_MARKER_NAME, Tk_Offset(RbcMarker, name), TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-outline", "foreground", (char *) NULL,
        (char *) NULL, 0, 0},
    {TK_CONFIG_DOUBLE, "-rotate", "rotate", "Rotate",
            DEF_MARKER_ROTATE, Tk_Offset(BitmapMarker, rotate),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-state", "state", "State",
            DEF_MARKER_STATE, Tk_Offset(RbcMarker, state),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcStateOption},
    {TK_CONFIG_BOOLEAN, "-under", "under", "Under",
            DEF_MARKER_UNDER, Tk_Offset(RbcMarker, drawUnder),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-xoffset", "xOffset", "XOffset",
            DEF_MARKER_X_OFFSET, Tk_Offset(RbcMarker, xOffset),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-yoffset", "yOffset", "YOffset",
            DEF_MARKER_Y_OFFSET, Tk_Offset(RbcMarker, yOffset),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

/*
 * -------------------------------------------------------------------
 *
 * ImageMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    char           *name;       /* Identifier for marker */
    RbcUid          classUid;   /* Type of marker */
    RbcGraph       *graphPtr;   /* Graph marker belongs to */
    unsigned int    flags;
    char          **tags;
    int             hidden;     /* Indicates if the marker is
                                 * currently hidden or not. */

    Tcl_HashEntry  *hashPtr;
    RbcChainLink   *linkPtr;
    RbcPoint2D     *worldPts;   /* Position of marker in world (graph)
                                 * coordinates. If 2 pairs of X-Y
                                 * coordinates are specified, then the
                                 * image is resized to fit this area.
                                 * Otherwise if 1 pair, then the image
                                 * is positioned at the coordinate at
                                 * its normal size. */
    int             nWorldPts;  /* Number of points */

    char           *elemName;   /* Element associated with marker */
    RbcAxis2D       axes;
    int             drawUnder;  /* If non-zero, draw the marker
                                 * underneath any elements. There can
                                 * be a performance because the graph
                                 * must be redraw entirely each time
                                 * this marker is redrawn. */
    int             clipped;    /* Indicates if the marker is totally
                                 * clipped by the plotting area. */
    int             xOffset, yOffset;   /* Pixel offset from anchor */

    MarkerClass    *classPtr;

    int             state;

    /* Image specific attributes */
    char           *imageName;  /* Name of image to be displayed. */
    Tk_Image        tkImage;    /* Tk image to be displayed. */
    Tk_Anchor       anchor;     /* Indicates how to translate the given
                                 * marker position. */
    RbcPoint2D      anchorPos;  /* Translated anchor point. */
    int             width, height;      /* Dimensions of the image */
    Tk_Image        tmpImage;
    Pixmap          pixmap;     /* Pixmap containing the scaled image */
    RbcColorImage  *srcImage;
    GC              gc;

} ImageMarker;

static Tk_ConfigSpec imageConfigSpecs[] = {
    {TK_CONFIG_ANCHOR, "-anchor", "anchor", "Anchor",
        DEF_MARKER_ANCHOR, Tk_Offset(ImageMarker, anchor), 0},
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags",
            DEF_MARKER_IMAGE_TAGS, Tk_Offset(RbcMarker, tags),
        TK_CONFIG_NULL_OK, &rbcListOption},
    {TK_CONFIG_CUSTOM, "-coords", "coords", "Coords",
            DEF_MARKER_COORDS, Tk_Offset(RbcMarker, worldPts),
        TK_CONFIG_NULL_OK, &coordsOption},
    {TK_CONFIG_STRING, "-element", "element", "Element",
            DEF_MARKER_ELEMENT, Tk_Offset(RbcMarker, elemName),
        TK_CONFIG_NULL_OK},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide",
            DEF_MARKER_HIDE, Tk_Offset(RbcMarker, hidden),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_STRING, "-image", "image", "Image",
        (char *) NULL, Tk_Offset(ImageMarker, imageName), TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX",
        DEF_MARKER_MAP_X, Tk_Offset(RbcMarker, axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY",
        DEF_MARKER_MAP_Y, Tk_Offset(RbcMarker, axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_STRING, "-name", (char *) NULL, (char *) NULL,
        DEF_MARKER_NAME, Tk_Offset(RbcMarker, name), TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-state", "state", "State",
            DEF_MARKER_STATE, Tk_Offset(RbcMarker, state),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcStateOption},
    {TK_CONFIG_BOOLEAN, "-under", "under", "Under",
            DEF_MARKER_UNDER, Tk_Offset(RbcMarker, drawUnder),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-xoffset", "xOffset", "XOffset",
            DEF_MARKER_X_OFFSET, Tk_Offset(RbcMarker, xOffset),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-yoffset", "yOffset", "YOffset",
            DEF_MARKER_Y_OFFSET, Tk_Offset(RbcMarker, yOffset),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

/*
 * -------------------------------------------------------------------
 *
 * LineMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    char           *name;       /* Identifier for marker */
    RbcUid          classUid;   /* Type is "linemarker" */
    RbcGraph       *graphPtr;   /* Graph marker belongs to */
    unsigned int    flags;
    char          **tags;
    int             hidden;     /* Indicates if the marker is currently
                                 * hidden or not. */

    Tcl_HashEntry  *hashPtr;
    RbcChainLink   *linkPtr;

    RbcPoint2D     *worldPts;   /* Position of marker (X-Y coordinates) in
                                 * world (graph) coordinates. */
    int             nWorldPts;  /* Number of points */

    char           *elemName;   /* Element associated with marker */
    RbcAxis2D       axes;
    int             drawUnder;  /* If non-zero, draw the marker
                                 * underneath any elements. There can
                                 * be a performance because the graph
                                 * must be redraw entirely each time
                                 * this marker is redrawn. */
    int             clipped;    /* Indicates if the marker is totally
                                 * clipped by the plotting area. */
    int             xOffset, yOffset;   /* Pixel offset */

    MarkerClass    *classPtr;

    int             state;

    /* Line specific attributes */
    XColor         *fillColor;
    XColor         *outlineColor;       /* Foreground and background colors */

    int             lineWidth;  /* Line width. */
    int             capStyle;   /* Cap style. */
    int             joinStyle;  /* Join style. */
    RbcDashes       dashes;     /* Dash list values (max 11) */

    GC              gc;         /* Private graphic context */

    RbcSegment2D   *segments;   /* Malloc'ed array of points.
                                 * Represents individual line segments
                                 * (2 points per segment) comprising
                                 * the mapped line.  The segments may
                                 * not necessarily be connected after
                                 * clipping. */
    int             nSegments;  /* # segments in the above array. */

    int             xor;
    int             xorState;   /* State of the XOR drawing. Indicates
                                 * if the marker is currently drawn. */
} LineMarker;

static Tk_ConfigSpec lineConfigSpecs[] = {
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags",
            DEF_MARKER_LINE_TAGS, Tk_Offset(RbcMarker, tags),
        TK_CONFIG_NULL_OK, &rbcListOption},
    {TK_CONFIG_CAP_STYLE, "-cap", "cap", "Cap",
            DEF_MARKER_CAP_STYLE, Tk_Offset(LineMarker, capStyle),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-coords", "coords", "Coords",
            DEF_MARKER_COORDS, Tk_Offset(RbcMarker, worldPts),
        TK_CONFIG_NULL_OK, &coordsOption},
    {TK_CONFIG_CUSTOM, "-dashes", "dashes", "Dashes",
            DEF_MARKER_DASHES, Tk_Offset(LineMarker, dashes),
        TK_CONFIG_NULL_OK, &rbcDashesOption},
    {TK_CONFIG_CUSTOM, "-dashoffset", "dashOffset", "DashOffset",
            DEF_MARKER_DASH_OFFSET, Tk_Offset(LineMarker, dashes.offset),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_STRING, "-element", "element", "Element",
            DEF_MARKER_ELEMENT, Tk_Offset(RbcMarker, elemName),
        TK_CONFIG_NULL_OK},
    {TK_CONFIG_COLOR, "-fill", "fill", "Fill",
        (char *) NULL, Tk_Offset(LineMarker, fillColor), TK_CONFIG_NULL_OK},
    {TK_CONFIG_JOIN_STYLE, "-join", "join", "Join",
            DEF_MARKER_JOIN_STYLE, Tk_Offset(LineMarker, joinStyle),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-linewidth", "lineWidth", "LineWidth",
            DEF_MARKER_LINE_WIDTH, Tk_Offset(LineMarker, lineWidth),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide",
            DEF_MARKER_HIDE, Tk_Offset(RbcMarker, hidden),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX",
        DEF_MARKER_MAP_X, Tk_Offset(RbcMarker, axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY",
        DEF_MARKER_MAP_Y, Tk_Offset(RbcMarker, axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_STRING, "-name", (char *) NULL, (char *) NULL,
        DEF_MARKER_NAME, Tk_Offset(RbcMarker, name), TK_CONFIG_NULL_OK},
    {TK_CONFIG_COLOR, "-outline", "outline", "Outline",
            DEF_MARKER_OUTLINE_COLOR, Tk_Offset(LineMarker, outlineColor),
        TK_CONFIG_COLOR_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_COLOR, "-outline", "outline", "Outline",
            DEF_MARKER_OUTLINE_MONO, Tk_Offset(LineMarker, outlineColor),
        TK_CONFIG_MONO_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-state", "state", "State",
            DEF_MARKER_STATE, Tk_Offset(RbcMarker, state),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcStateOption},
    {TK_CONFIG_BOOLEAN, "-under", "under", "Under",
            DEF_MARKER_UNDER, Tk_Offset(RbcMarker, drawUnder),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-xoffset", "xOffset", "XOffset",
            DEF_MARKER_X_OFFSET, Tk_Offset(RbcMarker, xOffset),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-xor", "xor", "Xor",
            DEF_MARKER_XOR, Tk_Offset(LineMarker, xor),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-yoffset", "yOffset", "YOffset",
            DEF_MARKER_Y_OFFSET, Tk_Offset(RbcMarker, yOffset),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

/*
 * -------------------------------------------------------------------
 *
 * PolygonMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    char           *name;       /* Identifier for marker */
    RbcUid          classUid;   /* Type of marker */
    RbcGraph       *graphPtr;   /* Graph marker belongs to */
    unsigned int    flags;
    char          **tags;
    int             hidden;     /* Indicates if the marker is currently
                                 * hidden or not. */

    Tcl_HashEntry  *hashPtr;
    RbcChainLink   *linkPtr;

    RbcPoint2D     *worldPts;   /* Position of marker (X-Y coordinates) in
                                 * world (graph) coordinates. */
    int             nWorldPts;  /* Number of points */

    char           *elemName;   /* Element associated with marker */
    RbcAxis2D       axes;
    int             drawUnder;  /* If non-zero, draw the marker
                                 * underneath any elements. There can
                                 * be a performance because the graph
                                 * must be redraw entirely each time
                                 * this marker is redrawn. */
    int             clipped;    /* Indicates if the marker is totally
                                 * clipped by the plotting area. */
    int             xOffset, yOffset;   /* Pixel offset */

    MarkerClass    *classPtr;

    int             state;

    /* Polygon specific attributes and fields */

    RbcPoint2D     *screenPts;

    RbcColorPair    outline;
    RbcColorPair    fill;

    Pixmap          stipple;    /* Stipple pattern to fill the polygon. */
    int             lineWidth;  /* Width of polygon outline. */
    int             capStyle;
    int             joinStyle;
    RbcDashes       dashes;     /* List of dash values.  Indicates how
                                 * draw the dashed line.  If no dash
                                 * values are provided, or the first value
                                 * is zero, then the line is drawn solid. */

    GC              outlineGC;  /* Graphics context to draw the outline of
                                 * the polygon. */
    GC              fillGC;     /* Graphics context to draw the filled
                                 * polygon. */

    RbcPoint2D     *fillPts;    /* Malloc'ed array of points used to draw
                                 * the filled polygon. These points may
                                 * form a degenerate polygon after clipping.
                                 */

    int             nFillPts;   /* # points in the above array. */

    RbcSegment2D   *outlinePts; /* Malloc'ed array of points.
                                 * Represents individual line segments
                                 * (2 points per segment) comprising
                                 * the outline of the polygon.  The
                                 * segments may not necessarily be
                                 * closed or connected after clipping. */

    int             nOutlinePts;        /* # points in the above array. */

    int             xor;
    int             xorState;   /* State of the XOR drawing. Indicates
                                 * if the marker is visible. We have
                                 * to drawn it again to erase it. */
} PolygonMarker;

static Tk_ConfigSpec polygonConfigSpecs[] = {
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags",
            DEF_MARKER_POLYGON_TAGS, Tk_Offset(RbcMarker, tags),
        TK_CONFIG_NULL_OK, &rbcListOption},
    {TK_CONFIG_CAP_STYLE, "-cap", "cap", "Cap",
            DEF_MARKER_CAP_STYLE, Tk_Offset(PolygonMarker, capStyle),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-coords", "coords", "Coords",
            DEF_MARKER_COORDS, Tk_Offset(RbcMarker, worldPts),
        TK_CONFIG_NULL_OK, &coordsOption},
    {TK_CONFIG_CUSTOM, "-dashes", "dashes", "Dashes",
            DEF_MARKER_DASHES, Tk_Offset(PolygonMarker, dashes),
        TK_CONFIG_NULL_OK, &rbcDashesOption},
    {TK_CONFIG_STRING, "-element", "element", "Element",
            DEF_MARKER_ELEMENT, Tk_Offset(RbcMarker, elemName),
        TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-fill", "fill", "Fill",
            DEF_MARKER_FILL_COLOR, Tk_Offset(PolygonMarker, fill),
        TK_CONFIG_COLOR_ONLY | TK_CONFIG_NULL_OK, &rbcColorPairOption},
    {TK_CONFIG_CUSTOM, "-fill", "fill", "Fill",
            DEF_MARKER_FILL_MONO, Tk_Offset(PolygonMarker, fill),
        TK_CONFIG_MONO_ONLY | TK_CONFIG_NULL_OK, &rbcColorPairOption},
    {TK_CONFIG_JOIN_STYLE, "-join", "join", "Join",
            DEF_MARKER_JOIN_STYLE, Tk_Offset(PolygonMarker, joinStyle),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-linewidth", "lineWidth", "LineWidth",
            DEF_MARKER_LINE_WIDTH, Tk_Offset(PolygonMarker, lineWidth),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide",
            DEF_MARKER_HIDE, Tk_Offset(RbcMarker, hidden),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX",
        DEF_MARKER_MAP_X, Tk_Offset(RbcMarker, axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY",
        DEF_MARKER_MAP_Y, Tk_Offset(RbcMarker, axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_STRING, "-name", (char *) NULL, (char *) NULL,
        DEF_MARKER_NAME, Tk_Offset(RbcMarker, name), TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-outline", "outline", "Outline",
            DEF_MARKER_OUTLINE_COLOR, Tk_Offset(PolygonMarker, outline),
        TK_CONFIG_COLOR_ONLY | TK_CONFIG_NULL_OK, &rbcColorPairOption},
    {TK_CONFIG_CUSTOM, "-outline", "outline", "Outline",
            DEF_MARKER_OUTLINE_MONO, Tk_Offset(PolygonMarker, outline),
        TK_CONFIG_MONO_ONLY | TK_CONFIG_NULL_OK, &rbcColorPairOption},
    {TK_CONFIG_CUSTOM, "-state", "state", "State",
            DEF_MARKER_STATE, Tk_Offset(RbcMarker, state),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcStateOption},
    {TK_CONFIG_BITMAP, "-stipple", "stipple", "Stipple",
            DEF_MARKER_STIPPLE, Tk_Offset(PolygonMarker, stipple),
        TK_CONFIG_NULL_OK},
    {TK_CONFIG_BOOLEAN, "-under", "under", "Under",
            DEF_MARKER_UNDER, Tk_Offset(RbcMarker, drawUnder),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-xoffset", "xOffset", "XOffset",
            DEF_MARKER_X_OFFSET, Tk_Offset(RbcMarker, xOffset),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-xor", "xor", "Xor",
            DEF_MARKER_XOR, Tk_Offset(PolygonMarker, xor),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-yoffset", "yOffset", "YOffset",
            DEF_MARKER_Y_OFFSET, Tk_Offset(RbcMarker, yOffset),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

static MarkerCreateProc CreateBitmapMarker,
    CreateLineMarker,
    CreateImageMarker,
    CreatePolygonMarker, CreateTextMarker, CreateWindowMarker;
static MarkerDrawProc DrawBitmapMarker,
    DrawLineMarker,
    DrawImageMarker, DrawPolygonMarker, DrawTextMarker, DrawWindowMarker;
static MarkerFreeProc FreeBitmapMarker,
    FreeLineMarker,
    FreeImageMarker, FreePolygonMarker, FreeTextMarker, FreeWindowMarker;
static MarkerConfigProc ConfigureBitmapMarker,
    ConfigureLineMarker,
    ConfigureImageMarker,
    ConfigurePolygonMarker, ConfigureTextMarker, ConfigureWindowMarker;
static MarkerMapProc MapBitmapMarker,
    MapLineMarker,
    MapImageMarker, MapPolygonMarker, MapTextMarker, MapWindowMarker;
static MarkerPostScriptProc BitmapMarkerToPostScript,
    LineMarkerToPostScript,
    ImageMarkerToPostScript,
    PolygonMarkerToPostScript, TextMarkerToPostScript, WindowMarkerToPostScript;
static MarkerPointProc PointInBitmapMarker,
    PointInLineMarker,
    PointInImageMarker,
    PointInPolygonMarker, PointInTextMarker, PointInWindowMarker;
static MarkerRegionProc RegionInBitmapMarker,
    RegionInLineMarker,
    RegionInImageMarker,
    RegionInPolygonMarker, RegionInTextMarker, RegionInWindowMarker;
static Tk_ImageChangedProc ImageChangedProc;

static int      BoxesDontOverlap(
    RbcGraph * graphPtr,
    RbcExtents2D * extsPtr);
static int      GetCoordinate(
    Tcl_Interp * interp,
    const char *expr,
    double *valuePtr);
static const char *PrintCoordinate(
    Tcl_Interp * interp,
    double x);
static int      ParseCoordinates(
    Tcl_Interp * interp,
    RbcMarker * markerPtr,
    int nExprs,
    const char **exprArr);
static double   HMap(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double x);
static double   VMap(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double y);
static RbcPoint2D MapPoint(
    RbcGraph * graphPtr,
    RbcPoint2D * pointPtr,
    RbcAxis2D * axesPtr);
static RbcMarker *CreateMarker(
    RbcGraph * graphPtr,
    const char *name,
    RbcUid classUid);
static void     DestroyMarker(
    RbcMarker * markerPtr);
static int      NameToMarker(
    RbcGraph * graphPtr,
    const char *name,
    RbcMarker ** markerPtrPtr);
static int      RenameMarker(
    RbcGraph * graphPtr,
    RbcMarker * markerPtr,
    char *oldName,
    char *newName);
static int      NamesOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      BindOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      CgetOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      ConfigureOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      CreateOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      DeleteOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      GetOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char *argv[]);
static int      RelinkOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      FindOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      ExistsOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int      TypeOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static void     ChildEventProc(
    ClientData clientData,
    XEvent * eventPtr);
static void     ChildGeometryProc(
    ClientData clientData,
    Tk_Window tkwin);
static void     ChildCustodyProc(
    ClientData clientData,
    Tk_Window tkwin);

static MarkerClass bitmapMarkerClass = {
    bitmapConfigSpecs,
    ConfigureBitmapMarker,
    DrawBitmapMarker,
    FreeBitmapMarker,
    MapBitmapMarker,
    PointInBitmapMarker,
    RegionInBitmapMarker,
    BitmapMarkerToPostScript,
};

static MarkerClass imageMarkerClass = {
    imageConfigSpecs,
    ConfigureImageMarker,
    DrawImageMarker,
    FreeImageMarker,
    MapImageMarker,
    PointInImageMarker,
    RegionInImageMarker,
    ImageMarkerToPostScript,
};

static MarkerClass lineMarkerClass = {
    lineConfigSpecs,
    ConfigureLineMarker,
    DrawLineMarker,
    FreeLineMarker,
    MapLineMarker,
    PointInLineMarker,
    RegionInLineMarker,
    LineMarkerToPostScript,
};

static MarkerClass polygonMarkerClass = {
    polygonConfigSpecs,
    ConfigurePolygonMarker,
    DrawPolygonMarker,
    FreePolygonMarker,
    MapPolygonMarker,
    PointInPolygonMarker,
    RegionInPolygonMarker,
    PolygonMarkerToPostScript,
};

static MarkerClass textMarkerClass = {
    textConfigSpecs,
    ConfigureTextMarker,
    DrawTextMarker,
    FreeTextMarker,
    MapTextMarker,
    PointInTextMarker,
    RegionInTextMarker,
    TextMarkerToPostScript,
};

static MarkerClass windowMarkerClass = {
    windowConfigSpecs,
    ConfigureWindowMarker,
    DrawWindowMarker,
    FreeWindowMarker,
    MapWindowMarker,
    PointInWindowMarker,
    RegionInWindowMarker,
    WindowMarkerToPostScript,
};

/*
 * ----------------------------------------------------------------------
 *
 * BoxesDontOverlap --
 *
 *      Tests if the bounding box of a marker overlaps the plotting
 *      area in any way.  If so, the marker will be drawn.  Just do a
 *      min/max test on the extents of both boxes.
 *
 *      Note: It's assumed that the extents of the bounding box lie
 *            within the area.  So for a 10x10 rectangle, bottom and
 *            left would be 9.
 *
 * Results:
 *      Returns 0 is the marker is visible in the plotting area, and
 *      1 otherwise (marker is clipped).
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
BoxesDontOverlap(
    RbcGraph * graphPtr,
    RbcExtents2D * extsPtr)
{
    assert(extsPtr->right >= extsPtr->left);
    assert(extsPtr->bottom >= extsPtr->top);
    assert(graphPtr->right >= graphPtr->left);
    assert(graphPtr->bottom >= graphPtr->top);

    return (((double) graphPtr->right < extsPtr->left) ||
        ((double) graphPtr->bottom < extsPtr->top) ||
        (extsPtr->right < (double) graphPtr->left) ||
        (extsPtr->bottom < (double) graphPtr->top));
}

/*
 * ----------------------------------------------------------------------
 *
 * GetCoordinate --
 *
 *      Convert the expression string into a floating point value. The
 *      only reason we use this routine instead of RbcExprDouble is to
 *      handle "elastic" bounds.  That is, convert the strings "-Inf",
 *      "Inf" into -(DBL_MAX) and DBL_MAX respectively.
 *
 * Results:
 *      The return value is a standard Tcl result.  The value of the
 *      expression is passed back via valuePtr.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
GetCoordinate(
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    const char *expr,           /* Numeric expression string to parse */
    double *valuePtr)
{                               /* Real-valued result of expression */
    char            c;

    c = expr[0];
    if ((c == 'I') && (strcmp(expr, "Inf") == 0)) {
        *valuePtr = DBL_MAX;    /* Elastic upper bound */
    } else if ((c == '-') && (expr[1] == 'I') && (strcmp(expr, "-Inf") == 0)) {
        *valuePtr = -DBL_MAX;   /* Elastic lower bound */
    } else if ((c == '+') && (expr[1] == 'I') && (strcmp(expr, "+Inf") == 0)) {
        *valuePtr = DBL_MAX;    /* Elastic upper bound */
    } else if (Tcl_ExprDouble(interp, expr, valuePtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * PrintCoordinate --
 *
 *      Convert the floating point value into its string
 *      representation.  The only reason this routine is used in
 *      instead of sprintf, is to handle the "elastic" bounds.  That
 *      is, convert the values DBL_MAX and -(DBL_MAX) into "+Inf" and
 *      "-Inf" respectively.
 *
 * Results:
 *      The return value is a standard Tcl result.  The string of the
 *      expression is passed back via string.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ---------------------------------------------------------------------- */
static const char *
PrintCoordinate(
    Tcl_Interp * interp,
    double x)
{                               /* Numeric value */
    if (x == DBL_MAX) {
        return "+Inf";
    } else if (x == -DBL_MAX) {
        return "-Inf";
    } else {
        static char     string[TCL_DOUBLE_SPACE + 1];

        Tcl_PrintDouble(interp, (double) x, string);
        return string;
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ParseCoordinates --
 *
 *      The Tcl coordinate list is converted to their floating point
 *      values. It will then replace the current marker coordinates.
 *
 *      Since different marker types require different number of
 *      coordinates this must be checked here.
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side effects:
 *      If the marker coordinates are reset, the graph is eventually
 *      redrawn with at the new marker coordinates.
 *
 * ----------------------------------------------------------------------
 */
static int
ParseCoordinates(
    Tcl_Interp * interp,
    RbcMarker * markerPtr,
    int nExprs,
    const char **exprArr)
{
    int             nWorldPts;
    int             minArgs, maxArgs;
    RbcPoint2D     *worldPts;
    register int    i;
    register RbcPoint2D *pointPtr;
    double          x, y;

    if (nExprs == 0) {
        return TCL_OK;
    }
    if (nExprs & 1) {
        Tcl_AppendResult(interp, "odd number of marker coordinates specified",
            (char *) NULL);
        return TCL_ERROR;
    }
    if (markerPtr->classUid == rbcLineMarkerUid) {
        minArgs = 4, maxArgs = 0;
    } else if (markerPtr->classUid == rbcPolygonMarkerUid) {
        minArgs = 6, maxArgs = 0;
    } else if ((markerPtr->classUid == rbcWindowMarkerUid) ||
        (markerPtr->classUid == rbcTextMarkerUid)) {
        minArgs = 2, maxArgs = 2;
    } else if ((markerPtr->classUid == rbcImageMarkerUid) ||
        (markerPtr->classUid == rbcBitmapMarkerUid)) {
        minArgs = 2, maxArgs = 4;
    } else {
        Tcl_AppendResult(interp, "unknown marker type", (char *) NULL);
        return TCL_ERROR;
    }

    if (nExprs < minArgs) {
        Tcl_AppendResult(interp, "too few marker coordinates specified",
            (char *) NULL);
        return TCL_ERROR;
    }
    if ((maxArgs > 0) && (nExprs > maxArgs)) {
        Tcl_AppendResult(interp, "too many marker coordinates specified",
            (char *) NULL);
        return TCL_ERROR;
    }
    nWorldPts = nExprs / 2;
    worldPts = (RbcPoint2D *) ckalloc(nWorldPts * sizeof(RbcPoint2D));
    if (worldPts == NULL) {
        Tcl_AppendResult(interp, "can't allocate new coordinate array",
            (char *) NULL);
        return TCL_ERROR;
    }

    /* Don't free the old coordinate array until we've parsed the new
     * coordinates without errors.  */
    pointPtr = worldPts;
    for (i = 0; i < nExprs; i += 2) {
        if ((GetCoordinate(interp, exprArr[i], &x) != TCL_OK) ||
            (GetCoordinate(interp, exprArr[i + 1], &y) != TCL_OK)) {
            ckfree((char *) worldPts);
            return TCL_ERROR;
        }
        pointPtr->x = x, pointPtr->y = y;
        pointPtr++;
    }
    if (markerPtr->worldPts != NULL) {
        ckfree((char *) markerPtr->worldPts);
    }
    markerPtr->worldPts = worldPts;
    markerPtr->nWorldPts = nWorldPts;
    markerPtr->flags |= RBC_MAP_ITEM;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * StringToCoordinates --
 *
 *      Given a Tcl list of numeric expression representing the
 *      element values, convert into an array of floating point
 *      values. In addition, the minimum and maximum values are saved.
 *      Since elastic values are allow (values which translate to the
 *      min/max of the graph), we must try to get the non-elastic
 *      minimum and maximum.
 *
 * Results:
 *      The return value is a standard Tcl result.  The vector is
 *      passed back via the vecPtr.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
StringToCoordinates(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Not used. */
    const char *string,         /* Tcl list of numeric expressions */
    char *widgRec,              /* Marker record */
    int offset)
{                               /* Not used. */
    RbcMarker      *markerPtr = (RbcMarker *) widgRec;
    int             nExprs;
    const char    **exprArr;
    int             result;

    nExprs = 0;
    if ((string != NULL) &&
        (Tcl_SplitList(interp, string, &nExprs, &exprArr) != TCL_OK)) {
        return TCL_ERROR;
    }
    if (nExprs == 0) {
        if (markerPtr->worldPts != NULL) {
            ckfree((char *) markerPtr->worldPts);
            markerPtr->worldPts = NULL;
        }
        markerPtr->nWorldPts = 0;
        return TCL_OK;
    }
    result = ParseCoordinates(interp, markerPtr, nExprs, exprArr);
    ckfree((char *) exprArr);
    return result;
}

/*
 * ----------------------------------------------------------------------
 *
 * CoordinatesToString --
 *
 *      Convert the vector of floating point values into a Tcl list.
 *
 * Results:
 *      The string representation of the vector is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static const char *
CoordinatesToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Marker record */
    int offset,                 /* Not used. */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Memory deallocation scheme to use */
    RbcMarker      *markerPtr = (RbcMarker *) widgRec;
    Tcl_Interp     *interp;
    Tcl_DString     dString;
    char           *result;
    register int    i;
    register RbcPoint2D *p;

    if (markerPtr->nWorldPts < 1) {
        return "";
    }
    interp = markerPtr->graphPtr->interp;

    Tcl_DStringInit(&dString);
    p = markerPtr->worldPts;
    for (i = 0; i < markerPtr->nWorldPts; i++) {
        Tcl_DStringAppendElement(&dString, PrintCoordinate(interp, p->x));
        Tcl_DStringAppendElement(&dString, PrintCoordinate(interp, p->y));
        p++;
    }
    result = Tcl_DStringValue(&dString);

    /*
     * If memory wasn't allocated for the dynamic string, do it here (it's
     * currently on the stack), so that Tcl can free it normally.
     */
    if (result == dString.staticSpace) {
        result = RbcStrdup(result);
    }
    *freeProcPtr = (Tcl_FreeProc *) Tcl_Free;
    return result;
}

/*
 * ----------------------------------------------------------------------
 *
 * HMap --
 *
 *      Map the given graph coordinate value to its axis, returning a
 *      window position.
 *
 * Results:
 *      Returns a floating point number representing the window
 *      coordinate position on the given axis.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static double
HMap(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double x)
{
    register double norm;

    if (x == DBL_MAX) {
        norm = 1.0;
    } else if (x == -DBL_MAX) {
        norm = 0.0;
    } else {
        if (axisPtr->logScale) {
            if (x > 0.0) {
                x = log10(x);
            } else if (x < 0.0) {
                x = 0.0;
            }
        }
        norm = NORMALIZE(axisPtr, x);
    }
    if (axisPtr->descending) {
        norm = 1.0 - norm;
    }
    /* Horizontal transformation */
    return ((norm * graphPtr->hRange) + graphPtr->hOffset);
}

/*
 * ----------------------------------------------------------------------
 *
 * VMap --
 *
 *      Map the given graph coordinate value to its axis, returning a
 *      window position.
 *
 * Results:
 *      Returns a double precision number representing the window
 *      coordinate position on the given axis.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static double
VMap(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double y)
{
    register double norm;

    if (y == DBL_MAX) {
        norm = 1.0;
    } else if (y == -DBL_MAX) {
        norm = 0.0;
    } else {
        if (axisPtr->logScale) {
            if (y > 0.0) {
                y = log10(y);
            } else if (y < 0.0) {
                y = 0.0;
            }
        }
        norm = NORMALIZE(axisPtr, y);
    }
    if (axisPtr->descending) {
        norm = 1.0 - norm;
    }
    /* Vertical transformation */
    return (((1.0 - norm) * graphPtr->vRange) + graphPtr->vOffset);
}

/*
 * ----------------------------------------------------------------------
 *
 * MapPoint --
 *
 *      Maps the given graph x,y coordinate values to a window position.
 *
 * Results:
 *      Returns a XPoint structure containing the window coordinates
 *      of the given graph x,y coordinate.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static          RbcPoint2D
MapPoint(
    RbcGraph * graphPtr,
    RbcPoint2D * pointPtr,      /* Graph X-Y coordinate. */
    RbcAxis2D * axesPtr)
{                               /* Specifies which axes to use */
    RbcPoint2D      result;

    if (graphPtr->inverted) {
        result.x = HMap(graphPtr, axesPtr->y, pointPtr->y);
        result.y = VMap(graphPtr, axesPtr->x, pointPtr->x);
    } else {
        result.x = HMap(graphPtr, axesPtr->x, pointPtr->x);
        result.y = VMap(graphPtr, axesPtr->y, pointPtr->y);
    }
    return result;              /* Result is screen coordinate. */
}

/*
 *----------------------------------------------------------------------
 *
 * CreateMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static RbcMarker *
CreateMarker(
    RbcGraph * graphPtr,
    const char *name,
    RbcUid classUid)
{
    RbcMarker      *markerPtr;

    /* Create the new marker based upon the given type */
    if (classUid == rbcBitmapMarkerUid) {
        markerPtr = CreateBitmapMarker();       /* bitmap */
    } else if (classUid == rbcLineMarkerUid) {
        markerPtr = CreateLineMarker(); /* line */
    } else if (classUid == rbcImageMarkerUid) {
        markerPtr = CreateImageMarker();        /* image */
    } else if (classUid == rbcTextMarkerUid) {
        markerPtr = CreateTextMarker(); /* text */
    } else if (classUid == rbcPolygonMarkerUid) {
        markerPtr = CreatePolygonMarker();      /* polygon */
    } else if (classUid == rbcWindowMarkerUid) {
        markerPtr = CreateWindowMarker();       /* window */
    } else {
        return NULL;
    }
    assert(markerPtr);
    markerPtr->graphPtr = graphPtr;
    markerPtr->hidden = markerPtr->drawUnder = FALSE;
    markerPtr->flags |= RBC_MAP_ITEM;
    markerPtr->name = RbcStrdup(name);
    markerPtr->classUid = classUid;
    return markerPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void
DestroyMarker(
    RbcMarker * markerPtr)
{
    RbcGraph       *graphPtr = markerPtr->graphPtr;

    if (markerPtr->drawUnder) {
        graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
    }
    /* Free the resources allocated for the particular type of marker */
    (*markerPtr->classPtr->freeProc) (graphPtr, markerPtr);
    if (markerPtr->worldPts != NULL) {
        ckfree((char *) markerPtr->worldPts);
    }
    RbcDeleteBindings(graphPtr->bindTable, markerPtr);
    Tk_FreeOptions(markerPtr->classPtr->configSpecs, (char *) markerPtr,
        graphPtr->display, 0);
    if (markerPtr->hashPtr != NULL) {
        Tcl_DeleteHashEntry(markerPtr->hashPtr);
    }
    if (markerPtr->linkPtr != NULL) {
        RbcChainDeleteLink(graphPtr->markers.displayList, markerPtr->linkPtr);
    }
    if (markerPtr->name != NULL) {
        ckfree((char *) markerPtr->name);
    }
    if (markerPtr->elemName != NULL) {
        ckfree((char *) markerPtr->elemName);
    }
    if (markerPtr->tags != NULL) {
        ckfree((char *) markerPtr->tags);
    }
    ckfree((char *) markerPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureBitmapMarker --
 *
 *      This procedure is called to process an argv/argc list, plus
 *      the Tk option database, in order to configure (or reconfigure)
 *      a bitmap marker.
 *
 * Results:
 *      A standard Tcl result.  If TCL_ERROR is returned, then
 *      interp->result contains an error message.
 *
 * Side effects:
 *      Configuration information, such as bitmap pixmap, colors,
 *      rotation, etc. get set for markerPtr; old resources get freed,
 *      if there were any.  The marker is eventually redisplayed.
 *
 * ----------------------------------------------------------------------
 */
static int
ConfigureBitmapMarker(
    RbcMarker * markerPtr)
{
    RbcGraph       *graphPtr = markerPtr->graphPtr;
    BitmapMarker   *bmPtr = (BitmapMarker *) markerPtr;
    GC              newGC;
    XGCValues       gcValues;
    unsigned long   gcMask;

    if (bmPtr->srcBitmap == None) {
        return TCL_OK;
    }
    if (bmPtr->destBitmap == None) {
        bmPtr->destBitmap = bmPtr->srcBitmap;
    }
    bmPtr->theta = FMOD(bmPtr->rotate, 360.0);
    if (bmPtr->theta < 0.0) {
        bmPtr->theta += 360.0;
    }
    gcMask = 0;
    if (bmPtr->outlineColor != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = bmPtr->outlineColor->pixel;
    }
    if (bmPtr->fillColor != NULL) {
        gcValues.background = bmPtr->fillColor->pixel;
        gcMask |= GCBackground;
    } else {
        gcValues.clip_mask = bmPtr->srcBitmap;
        gcMask |= GCClipMask;
    }

    /* Note that while this is a "shared" GC, we're going to change
     * the clip origin right before the bitmap is drawn anyways.  This
     * assumes that any drawing code using this GC (with GCClipMask
     * set) is going to want to set the clip origin anyways.  */
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (bmPtr->gc != NULL) {
        Tk_FreeGC(graphPtr->display, bmPtr->gc);
    }
    bmPtr->gc = newGC;

    /* Create background GC color */

    if (bmPtr->fillColor != NULL) {
        gcValues.foreground = bmPtr->fillColor->pixel;
        newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
        if (bmPtr->fillGC != NULL) {
            Tk_FreeGC(graphPtr->display, bmPtr->fillGC);
        }
        bmPtr->fillGC = newGC;
    }
    bmPtr->flags |= RBC_MAP_ITEM;
    if (bmPtr->drawUnder) {
        graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
    }
    RbcEventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * MapBitmapMarker --
 *
 *      This procedure gets called each time the layout of the graph
 *      changes.  The x, y window coordinates of the bitmap marker are
 *      saved in the marker structure.
 *
 *      Additionly, if no background color was specified, the
 *      GCTileStipXOrigin and GCTileStipYOrigin attributes are set in
 *      the private GC.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Window coordinates are saved and if no background color was
 *      set, the GC stipple origins are changed to calculated window
 *      coordinates.
 *
 * ----------------------------------------------------------------------
 */
static void
MapBitmapMarker(
    RbcMarker * markerPtr)
{
    BitmapMarker   *bmPtr = (BitmapMarker *) markerPtr;
    RbcExtents2D    exts;
    RbcGraph       *graphPtr = markerPtr->graphPtr;
    RbcPoint2D      anchorPos;
    RbcPoint2D      corner1, corner2;
    int             destWidth, destHeight;
    int             srcWidth, srcHeight;
    register int    i;

    if (bmPtr->srcBitmap == None) {
        return;
    }
    if (bmPtr->destBitmap != bmPtr->srcBitmap) {
        Tk_FreePixmap(graphPtr->display, bmPtr->destBitmap);
        bmPtr->destBitmap = bmPtr->srcBitmap;
    }
    /*
     * Collect the coordinates.  The number of coordinates will determine
     * the calculations to be made.
     *
     *     x1 y1        A single pair of X-Y coordinates.  They represent
     *                  the anchor position of the bitmap.
     *
     *  x1 y1 x2 y2     Two pairs of X-Y coordinates.  They represent
     *                  two opposite corners of a bounding rectangle. The
     *                  bitmap is possibly rotated and scaled to fit into
     *                  this box.
     *
     */
    Tk_SizeOfBitmap(graphPtr->display, bmPtr->srcBitmap, &srcWidth, &srcHeight);
    corner1 = MapPoint(graphPtr, bmPtr->worldPts, &bmPtr->axes);
    if (bmPtr->nWorldPts > 1) {
        double          hold;

        corner2 = MapPoint(graphPtr, bmPtr->worldPts + 1, &bmPtr->axes);
        /* Flip the corners if necessary */
        if (corner1.x > corner2.x) {
            hold = corner1.x, corner1.x = corner2.x, corner2.x = hold;
        }
        if (corner1.y > corner2.y) {
            hold = corner1.y, corner1.y = corner2.y, corner2.y = hold;
        }
    } else {
        corner2.x = corner1.x + srcWidth - 1;
        corner2.y = corner1.y + srcHeight - 1;
    }
    destWidth = (int) (corner2.x - corner1.x) + 1;
    destHeight = (int) (corner2.y - corner1.y) + 1;

    if (bmPtr->nWorldPts == 1) {
        anchorPos = RbcTranslatePoint(&corner1, destWidth, destHeight,
            bmPtr->anchor);
    } else {
        anchorPos = corner1;
    }
    anchorPos.x += bmPtr->xOffset;
    anchorPos.y += bmPtr->yOffset;

    /* Check if the bitmap sits at least partially in the plot area. */
    exts.left = anchorPos.x;
    exts.top = anchorPos.y;
    exts.right = anchorPos.x + destWidth - 1;
    exts.bottom = anchorPos.y + destHeight - 1;

    bmPtr->clipped = BoxesDontOverlap(graphPtr, &exts);
    if (bmPtr->clipped) {
        return;                 /* Bitmap is offscreen. Don't generate
                                 * rotated or scaled bitmaps. */
    }

    /*
     * Scale the bitmap if necessary. It's a little tricky because we
     * only want to scale what's visible on the screen, not the entire
     * bitmap.
     */
    if ((bmPtr->theta != 0.0) || (destWidth != srcWidth) ||
        (destHeight != srcHeight)) {
        int             regionWidth, regionHeight;
        RbcRegion2D     region; /* Indicates the portion of the scaled
                                 * bitmap that we want to display. */
        double          left, right, top, bottom;

        /*
         * Determine the region of the bitmap visible in the plot area.
         */
        left = MAX(graphPtr->left, exts.left);
        right = MIN(graphPtr->right, exts.right);
        top = MAX(graphPtr->top, exts.top);
        bottom = MIN(graphPtr->bottom, exts.bottom);

        region.left = region.top = 0;
        if (graphPtr->left > exts.left) {
            region.left = (int) (graphPtr->left - exts.left);
        }
        if (graphPtr->top > exts.top) {
            region.top = (int) (graphPtr->top - exts.top);
        }
        regionWidth = (int) (right - left) + 1;
        regionHeight = (int) (bottom - top) + 1;
        region.right = region.left + (int) (right - left);
        region.bottom = region.top + (int) (bottom - top);

        anchorPos.x = left;
        anchorPos.y = top;
        bmPtr->destBitmap = RbcScaleRotateBitmapRegion(graphPtr->tkwin,
            bmPtr->srcBitmap, srcWidth, srcHeight,
            region.left, region.top, regionWidth, regionHeight,
            destWidth, destHeight, bmPtr->theta);
        bmPtr->destWidth = regionWidth;
        bmPtr->destHeight = regionHeight;
    } else {
        bmPtr->destWidth = srcWidth;
        bmPtr->destHeight = srcHeight;
        bmPtr->destBitmap = bmPtr->srcBitmap;
    }
    bmPtr->anchorPos = anchorPos;
    {
        double          xScale, yScale;
        double          tx, ty;
        double          rotWidth, rotHeight;
        RbcPoint2D      polygon[5];
        int             n;

        /*
         * Compute a polygon to represent the background area of the bitmap.
         * This is needed for backgrounds of arbitrarily rotated bitmaps.
         * We also use it to print a background in PostScript.
         */
        RbcGetBoundingBox(srcWidth, srcHeight, bmPtr->theta, &rotWidth,
            &rotHeight, polygon);
        xScale = (double) destWidth / rotWidth;
        yScale = (double) destHeight / rotHeight;

        /*
         * Adjust each point of the polygon. Both scale it to the new size
         * and translate it to the actual screen position of the bitmap.
         */
        tx = exts.left + destWidth * 0.5;
        ty = exts.top + destHeight * 0.5;
        for (i = 0; i < 4; i++) {
            polygon[i].x = (polygon[i].x * xScale) + tx;
            polygon[i].y = (polygon[i].y * yScale) + ty;
        }
        RbcGraphExtents(graphPtr, &exts);
        n = RbcPolyRectClip(&exts, polygon, 4, bmPtr->outline);
        assert(n <= MAX_OUTLINE_POINTS);
        if (n < 3) {
            memcpy(&bmPtr->outline, polygon, sizeof(RbcPoint2D) * 4);
            bmPtr->nOutlinePts = 4;
        } else {
            bmPtr->nOutlinePts = n;
        }
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * PointInBitmapMarker --
 *
 *      Indicates if the given point is over the bitmap marker.  The
 *      area of the bitmap is the rectangle.
 *
 * Results:
 *      Returns 1 is the point is over the bitmap marker, 0 otherwise.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
PointInBitmapMarker(
    RbcMarker * markerPtr,
    RbcPoint2D * samplePtr)
{
    BitmapMarker   *bmPtr = (BitmapMarker *) markerPtr;

    if (bmPtr->srcBitmap == None) {
        return 0;
    }
    if (bmPtr->theta != 0.0) {
        RbcPoint2D      points[MAX_OUTLINE_POINTS];
        register int    i;

        /*
         * Generate the bounding polygon (isolateral) for the bitmap
         * and see if the point is inside of it.
         */
        for (i = 0; i < bmPtr->nOutlinePts; i++) {
            points[i].x = bmPtr->outline[i].x + bmPtr->anchorPos.x;
            points[i].y = bmPtr->outline[i].y + bmPtr->anchorPos.y;
        }
        return RbcPointInPolygon(samplePtr, points, bmPtr->nOutlinePts);
    }
    return ((samplePtr->x >= bmPtr->anchorPos.x) &&
        (samplePtr->x < (bmPtr->anchorPos.x + bmPtr->destWidth)) &&
        (samplePtr->y >= bmPtr->anchorPos.y) &&
        (samplePtr->y < (bmPtr->anchorPos.y + bmPtr->destHeight)));
}

/*
 *----------------------------------------------------------------------
 *
 * RegionInBitmapMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
RegionInBitmapMarker(
    RbcMarker * markerPtr,
    RbcExtents2D * extsPtr,
    int enclosed)
{
    BitmapMarker   *bmPtr = (BitmapMarker *) markerPtr;

    if (bmPtr->nWorldPts < 1) {
        return FALSE;
    }
    if (bmPtr->theta != 0.0) {
        RbcPoint2D      points[MAX_OUTLINE_POINTS];
        register int    i;

        /*
         * Generate the bounding polygon (isolateral) for the bitmap
         * and see if the point is inside of it.
         */
        for (i = 0; i < bmPtr->nOutlinePts; i++) {
            points[i].x = bmPtr->outline[i].x + bmPtr->anchorPos.x;
            points[i].y = bmPtr->outline[i].y + bmPtr->anchorPos.y;
        }
        return RbcRegionInPolygon(extsPtr, points, bmPtr->nOutlinePts,
            enclosed);
    }
    if (enclosed) {
        return ((bmPtr->anchorPos.x >= extsPtr->left) &&
            (bmPtr->anchorPos.y >= extsPtr->top) &&
            ((bmPtr->anchorPos.x + bmPtr->destWidth) <= extsPtr->right) &&
            ((bmPtr->anchorPos.y + bmPtr->destHeight) <= extsPtr->bottom));
    }
    return !((bmPtr->anchorPos.x >= extsPtr->right) ||
        (bmPtr->anchorPos.y >= extsPtr->bottom) ||
        ((bmPtr->anchorPos.x + bmPtr->destWidth) <= extsPtr->left) ||
        ((bmPtr->anchorPos.y + bmPtr->destHeight) <= extsPtr->top));
}

/*
 * ----------------------------------------------------------------------
 *
 * DrawBitmapMarker --
 *
 *      Draws the bitmap marker that have a transparent of filled
 *      background.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      GC stipple origins are changed to current window coordinates.
 *      Commands are output to X to draw the marker in its current
 *      mode.
 *
 * ----------------------------------------------------------------------
 */
static void
DrawBitmapMarker(
    RbcMarker * markerPtr,
    Drawable drawable)
{                               /* Pixmap or window to draw into */
    RbcGraph       *graphPtr = markerPtr->graphPtr;
    BitmapMarker   *bmPtr = (BitmapMarker *) markerPtr;
    double          theta;

    if ((bmPtr->destBitmap == None) || (bmPtr->destWidth < 1) ||
        (bmPtr->destHeight < 1)) {
        return;
    }
    theta = FMOD(bmPtr->theta, (double) 90.0);
    if ((bmPtr->fillColor == NULL) || (theta != 0.0)) {

        /*
         * If the bitmap is rotated and a filled background is
         * required, then a filled polygon is drawn before the
         * bitmap.
         */

        if (bmPtr->fillColor != NULL) {
            int             i;
            XPoint          polygon[MAX_OUTLINE_POINTS];

            for (i = 0; i < bmPtr->nOutlinePts; i++) {
                polygon[i].x = (short int) bmPtr->outline[i].x;
                polygon[i].y = (short int) bmPtr->outline[i].y;
            }
            XFillPolygon(graphPtr->display, drawable, bmPtr->fillGC,
                polygon, bmPtr->nOutlinePts, Convex, CoordModeOrigin);
        }
        XSetClipMask(graphPtr->display, bmPtr->gc, bmPtr->destBitmap);
        XSetClipOrigin(graphPtr->display, bmPtr->gc, (int) bmPtr->anchorPos.x,
            (int) bmPtr->anchorPos.y);
    } else {
        XSetClipMask(graphPtr->display, bmPtr->gc, None);
        XSetClipOrigin(graphPtr->display, bmPtr->gc, 0, 0);
    }
    XCopyPlane(graphPtr->display, bmPtr->destBitmap, drawable, bmPtr->gc, 0, 0,
        bmPtr->destWidth, bmPtr->destHeight, (int) bmPtr->anchorPos.x,
        (int) bmPtr->anchorPos.y, 1);
}

/*
 * ----------------------------------------------------------------------
 *
 * BitmapMarkerToPostScript --
 *
 *      Generates PostScript to print a bitmap marker.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void
BitmapMarkerToPostScript(
    RbcMarker * markerPtr,      /* Marker to be printed */
    RbcPsToken * psToken)
{
    RbcGraph       *graphPtr = markerPtr->graphPtr;
    BitmapMarker   *bmPtr = (BitmapMarker *) markerPtr;

    if (bmPtr->destBitmap == None) {
        return;
    }
    if (bmPtr->fillColor != NULL) {
        RbcBackgroundToPostScript(psToken, bmPtr->fillColor);
        RbcPolygonToPostScript(psToken, bmPtr->outline, 4);
    }
    RbcForegroundToPostScript(psToken, bmPtr->outlineColor);

    RbcFormatToPostScript(psToken,
        "  gsave\n    %g %g translate\n    %d %d scale\n",
        bmPtr->anchorPos.x, bmPtr->anchorPos.y + bmPtr->destHeight,
        bmPtr->destWidth, -bmPtr->destHeight);
    RbcFormatToPostScript(psToken, "    %d %d true [%d 0 0 %d 0 %d] {",
        bmPtr->destWidth, bmPtr->destHeight, bmPtr->destWidth,
        -bmPtr->destHeight, bmPtr->destHeight);
    RbcBitmapDataToPostScript(psToken, graphPtr->display, bmPtr->destBitmap,
        bmPtr->destWidth, bmPtr->destHeight);
    RbcAppendToPostScript(psToken, "    } imagemask\n",
        "grestore\n", (char *) NULL);
}

/*
 * ----------------------------------------------------------------------
 *
 * FreeBitmapMarker --
 *
 *      Releases the memory and attributes of the bitmap marker.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Bitmap attributes (GCs, colors, bitmap, etc) get destroyed.
 *      Memory is released, X resources are freed, and the graph is
 *      redrawn.
 *
 * ----------------------------------------------------------------------
 */
static void
FreeBitmapMarker(
    RbcGraph * graphPtr,
    RbcMarker * markerPtr)
{
    BitmapMarker   *bmPtr = (BitmapMarker *) markerPtr;

    if (bmPtr->gc != NULL) {
        Tk_FreeGC(graphPtr->display, bmPtr->gc);
    }
    if (bmPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, bmPtr->fillGC);
    }
    if (bmPtr->destBitmap != bmPtr->srcBitmap) {
        Tk_FreePixmap(graphPtr->display, bmPtr->destBitmap);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateBitmapMarker --
 *
 *      Allocate memory and initialize methods for the new bitmap marker.
 *
 * Results:
 *      The pointer to the newly allocated marker structure is returned.
 *
 * Side effects:
 *      Memory is allocated for the bitmap marker structure.
 *
 * ----------------------------------------------------------------------
 */
static RbcMarker *
CreateBitmapMarker(
    )
{
    BitmapMarker   *bmPtr;

    bmPtr = RbcCalloc(1, sizeof(BitmapMarker));
    if (bmPtr != NULL) {
        bmPtr->classPtr = &bitmapMarkerClass;
    }
    return (RbcMarker *) bmPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * ImageChangedProc
 *
 *      TODO: Description
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void
ImageChangedProc(
    ClientData clientData,
    int x,                      /* Not used. */
    int y,                      /* Not used. */
    int width,                  /* Not used. */
    int height,                 /* Not used. */
    int imageWidth,             /* Not used. */
    int imageHeight)
{                               /* Not used. */
    ImageMarker    *imPtr = clientData;
    Tk_PhotoHandle  photo;

    photo = Tk_FindPhoto(imPtr->graphPtr->interp, imPtr->imageName);
    if (photo != NULL) {
        if (imPtr->srcImage != NULL) {
            RbcFreeColorImage(imPtr->srcImage);
        }
        /* Convert the latest incarnation of the photo image back to a
         * color image that we can scale. */
        imPtr->srcImage = RbcPhotoToColorImage(photo);
    }
    imPtr->graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
    imPtr->flags |= RBC_MAP_ITEM;
    RbcEventuallyRedrawGraph(imPtr->graphPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureImageMarker --
 *
 *      This procedure is called to process an argv/argc list, plus
 *      the Tk option database, in order to configure (or reconfigure)
 *      a image marker.
 *
 * Results:
 *      A standard Tcl result.  If TCL_ERROR is returned, then
 *      interp->result contains an error message.
 *
 * Side effects:
 *      Configuration information, such as image pixmap, colors,
 *      rotation, etc. get set for markerPtr; old resources get freed,
 *      if there were any.  The marker is eventually redisplayed.
 *
 * ----------------------------------------------------------------------
 */
static int
ConfigureImageMarker(
    RbcMarker * markerPtr)
{
    ImageMarker    *imPtr = (ImageMarker *) markerPtr;
    RbcGraph       *graphPtr = markerPtr->graphPtr;

    if (RbcConfigModified(markerPtr->classPtr->configSpecs, "-image",
            (char *) NULL)) {
        Tcl_Interp     *interp = graphPtr->interp;

        if (imPtr->tkImage != NULL) {
            Tk_FreeImage(imPtr->tkImage);
            imPtr->tkImage = NULL;
        }
        if (imPtr->imageName != NULL && imPtr->imageName[0] != '\0') {
            GC              newGC;
            Tk_PhotoHandle  photo;

            imPtr->tkImage = Tk_GetImage(interp, graphPtr->tkwin,
                imPtr->imageName, ImageChangedProc, imPtr);
            if (imPtr->tkImage == NULL) {
                Tcl_AppendResult(interp, "can't find an image \"",
                    imPtr->imageName, "\"", (char *) NULL);
                ckfree((char *) imPtr->imageName);
                imPtr->imageName = NULL;
                return TCL_ERROR;
            }
            photo = Tk_FindPhoto(interp, imPtr->imageName);
            if (photo != NULL) {
                if (imPtr->srcImage != NULL) {
                    RbcFreeColorImage(imPtr->srcImage);
                }
                /* Convert the photo into a color image */
                imPtr->srcImage = RbcPhotoToColorImage(photo);
            }
            newGC = Tk_GetGC(graphPtr->tkwin, 0L, (XGCValues *) NULL);
            if (imPtr->gc != NULL) {
                Tk_FreeGC(graphPtr->display, imPtr->gc);
            }
            imPtr->gc = newGC;
        }
    }
    imPtr->flags |= RBC_MAP_ITEM;
    if (imPtr->drawUnder) {
        graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
    }
    RbcEventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * MapImageMarker --
 *
 *      This procedure gets called each time the layout of the graph
 *      changes.  The x, y window coordinates of the image marker are
 *      saved in the marker structure.
 *
 *      Additionly, if no background color was specified, the
 *      GCTileStipXOrigin and GCTileStipYOrigin attributes are set in
 *      the private GC.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Window coordinates are saved and if no background color was *
 *      set, the GC stipple origins are changed to calculated window
 *      coordinates.
 *
 * ----------------------------------------------------------------------
 */
static void
MapImageMarker(
    RbcMarker * markerPtr)
{
    RbcExtents2D    exts;
    RbcGraph       *graphPtr;
    ImageMarker    *imPtr;
    RbcPoint2D      anchorPos;
    RbcPoint2D      corner1, corner2;
    int             scaledWidth, scaledHeight;
    int             srcWidth, srcHeight;

    imPtr = (ImageMarker *) markerPtr;
    if (imPtr->tkImage == NULL) {
        return;
    }
    graphPtr = imPtr->graphPtr;
    corner1 = MapPoint(graphPtr, imPtr->worldPts, &imPtr->axes);
    if (imPtr->srcImage == NULL) {
        /*
         * Don't scale or rotate non-photo images.
         */
        Tk_SizeOfImage(imPtr->tkImage, &srcWidth, &srcHeight);
        imPtr->width = srcWidth;
        imPtr->height = srcHeight;
        imPtr->anchorPos.x = corner1.x + imPtr->xOffset;
        imPtr->anchorPos.y = corner1.y + imPtr->yOffset;
        exts.left = imPtr->anchorPos.x;
        exts.top = imPtr->anchorPos.y;
        exts.right = exts.left + srcWidth - 1;
        exts.bottom = exts.top + srcHeight - 1;
        imPtr->clipped = BoxesDontOverlap(graphPtr, &exts);
        return;
    }

    imPtr->width = srcWidth = imPtr->srcImage->width;
    imPtr->height = srcHeight = imPtr->srcImage->height;
    if ((srcWidth == 0) && (srcHeight == 0)) {
        imPtr->clipped = TRUE;
        return;                 /* Empty image. */
    }
    if (imPtr->nWorldPts > 1) {
        double          hold;

        corner2 = MapPoint(graphPtr, imPtr->worldPts + 1, &imPtr->axes);
        /* Flip the corners if necessary */
        if (corner1.x > corner2.x) {
            hold = corner1.x, corner1.x = corner2.x, corner2.x = hold;
        }
        if (corner1.y > corner2.y) {
            hold = corner1.y, corner1.y = corner2.y, corner2.y = hold;
        }
    } else {
        corner2.x = corner1.x + srcWidth - 1;
        corner2.y = corner1.y + srcHeight - 1;
    }
    scaledWidth = (int) (corner2.x - corner1.x) + 1;
    scaledHeight = (int) (corner2.y - corner1.y) + 1;

    if (imPtr->nWorldPts == 1) {
        anchorPos = RbcTranslatePoint(&corner1, scaledWidth, scaledHeight,
            imPtr->anchor);
    } else {
        anchorPos = corner1;
    }
    anchorPos.x += imPtr->xOffset;
    anchorPos.y += imPtr->yOffset;

    /* Check if the image sits at least partially in the plot area. */
    exts.left = anchorPos.x;
    exts.top = anchorPos.y;
    exts.right = anchorPos.x + scaledWidth - 1;
    exts.bottom = anchorPos.y + scaledHeight - 1;

    imPtr->clipped = BoxesDontOverlap(graphPtr, &exts);
    if (imPtr->clipped) {
        return;                 /* Image is offscreen. Don't generate
                                 * rotated or scaled images. */
    }
    if ((scaledWidth != srcWidth) || (scaledHeight != srcHeight)) {
        Tk_PhotoHandle  photo;
        RbcColorImage  *destImage;
        int             x, y, width, height;
        int             left, right, top, bottom;

        /* Determine the region of the subimage inside of the
         * destination image. */
        left = MAX((int) exts.left, graphPtr->left);
        top = MAX((int) exts.top, graphPtr->top);
        right = MIN((int) exts.right, graphPtr->right);
        bottom = MIN((int) exts.bottom, graphPtr->bottom);

        /* Reset image location and coordinates to that of the region */
        anchorPos.x = left;
        anchorPos.y = top;

        x = y = 0;
        if (graphPtr->left > (int) exts.left) {
            x = graphPtr->left - (int) exts.left;
        }
        if (graphPtr->top > (int) exts.top) {
            y = graphPtr->top - (int) exts.top;
        }
        width = (int) (right - left + 1);
        height = (int) (bottom - top + 1);

        destImage = RbcResizeColorSubimage(imPtr->srcImage, x, y, width,
            height, scaledWidth, scaledHeight);
        imPtr->pixmap = None;
        if (imPtr->tmpImage == NULL) {
            imPtr->tmpImage = RbcCreateTemporaryImage(graphPtr->interp,
                graphPtr->tkwin, imPtr);
            if (imPtr->tmpImage == NULL) {
                return;
            }
        }
        /* Put the scaled colorimage into the photo. */
        photo = Tk_FindPhoto(graphPtr->interp, RbcNameOfImage(imPtr->tmpImage));
        RbcColorImageToPhoto(graphPtr->interp, destImage, photo);

        RbcFreeColorImage(destImage);
        imPtr->width = width;
        imPtr->height = height;
    }
    imPtr->anchorPos = anchorPos;
}

/*
 * ----------------------------------------------------------------------
 *
 * PointInWindowMarker --
 *
 *      Indicates if the given point is over the window marker.  The
 *      area of the window is the rectangle.
 *
 * Results:
 *      Returns 1 is the point is over the window marker, 0 otherwise.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
PointInImageMarker(
    RbcMarker * markerPtr,
    RbcPoint2D * samplePtr)
{
    ImageMarker    *imPtr = (ImageMarker *) markerPtr;

    return ((samplePtr->x >= imPtr->anchorPos.x) &&
        (samplePtr->x < (imPtr->anchorPos.x + imPtr->width)) &&
        (samplePtr->y >= imPtr->anchorPos.y) &&
        (samplePtr->y < (imPtr->anchorPos.y + imPtr->height)));
}

/*
 *----------------------------------------------------------------------
 *
 * RegionInImageMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
RegionInImageMarker(
    RbcMarker * markerPtr,
    RbcExtents2D * extsPtr,
    int enclosed)
{
    ImageMarker    *imPtr = (ImageMarker *) markerPtr;

    if (imPtr->nWorldPts < 1) {
        return FALSE;
    }
    if (enclosed) {
        return ((imPtr->anchorPos.x >= extsPtr->left) &&
            (imPtr->anchorPos.y >= extsPtr->top) &&
            ((imPtr->anchorPos.x + imPtr->width) <= extsPtr->right) &&
            ((imPtr->anchorPos.y + imPtr->height) <= extsPtr->bottom));
    }
    return !((imPtr->anchorPos.x >= extsPtr->right) ||
        (imPtr->anchorPos.y >= extsPtr->bottom) ||
        ((imPtr->anchorPos.x + imPtr->width) <= extsPtr->left) ||
        ((imPtr->anchorPos.y + imPtr->height) <= extsPtr->top));
}

/*
 * ----------------------------------------------------------------------
 *
 * DrawImageMarker --
 *
 *      This procedure is invoked to draw a image marker.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      GC stipple origins are changed to current window coordinates.
 *      Commands are output to X to draw the marker in its current mode.
 *
 * ----------------------------------------------------------------------
 */
static void
DrawImageMarker(
    RbcMarker * markerPtr,
    Drawable drawable)
{                               /* Pixmap or window to draw into */
    ImageMarker    *imPtr = (ImageMarker *) markerPtr;
    int             width, height;

    /* check is image still exists */
    if ((imPtr->tkImage == NULL) || (RbcImageIsDeleted(imPtr->tkImage))) {
        return;
    }
    if (imPtr->pixmap == None) {
        Pixmap          pixmap;
        Tk_Image        tkImage;

        tkImage = (imPtr->tmpImage != NULL) ? imPtr->tmpImage : imPtr->tkImage;
        Tk_SizeOfImage(tkImage, &width, &height);
        pixmap = None;
        if (pixmap == None) {   /* May not be a "photo" image. */
            Tk_RedrawImage(tkImage, 0, 0, width, height, drawable,
                (int) imPtr->anchorPos.x, (int) imPtr->anchorPos.y);
        } else {
            XCopyArea(imPtr->graphPtr->display, pixmap, drawable,
                imPtr->gc, 0, 0, width, height, (int) imPtr->anchorPos.x,
                (int) imPtr->anchorPos.y);
        }
    } else {
        XCopyArea(imPtr->graphPtr->display, imPtr->pixmap, drawable,
            imPtr->gc, 0, 0, imPtr->width, imPtr->height,
            (int) imPtr->anchorPos.x, (int) imPtr->anchorPos.y);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ImageMarkerToPostScript --
 *
 *      This procedure is invoked to print a image marker.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void
ImageMarkerToPostScript(
    RbcMarker * markerPtr,      /* Marker to be printed */
    RbcPsToken * psToken)
{
    ImageMarker    *imPtr = (ImageMarker *) markerPtr;
    const char     *imageName;
    Tk_PhotoHandle  photo;

    if ((imPtr->tkImage == NULL) || (RbcImageIsDeleted(imPtr->tkImage))) {
        return;                 /* Image doesn't exist anymore */
    }
    imageName = (imPtr->tmpImage == NULL)
        ? RbcNameOfImage(imPtr->tkImage) : RbcNameOfImage(imPtr->tmpImage);
    photo = Tk_FindPhoto(markerPtr->graphPtr->interp, imageName);
    if (photo == NULL) {
        return;                 /* Image isn't a photo image */
    }
    RbcPhotoToPostScript(psToken, photo, imPtr->anchorPos.x,
        imPtr->anchorPos.y);
}

/*
 * ----------------------------------------------------------------------
 *
 * FreeImageMarker --
 *
 *      Destroys the structure containing the attributes of the image
 *      marker.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Image attributes (GCs, colors, image, etc) get destroyed.
 *      Memory is released, X resources are freed, and the graph is
 *      redrawn.
 *
 * ----------------------------------------------------------------------
 */
static void
FreeImageMarker(
    RbcGraph * graphPtr,
    RbcMarker * markerPtr)
{
    ImageMarker    *imPtr = (ImageMarker *) markerPtr;

    if (imPtr->pixmap != None) {
        Tk_FreePixmap(graphPtr->display, imPtr->pixmap);
    }
    if (imPtr->tkImage != NULL) {
        Tk_FreeImage(imPtr->tkImage);
    }
    if (imPtr->tmpImage != NULL) {
        RbcDestroyTemporaryImage(graphPtr->interp, imPtr->tmpImage);
    }
    if (imPtr->srcImage != NULL) {
        RbcFreeColorImage(imPtr->srcImage);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateImageMarker --
 *
 *      Allocate memory and initialize methods for the new image marker.
 *
 * Results:
 *      The pointer to the newly allocated marker structure is returned.
 *
 * Side effects:
 *      Memory is allocated for the image marker structure.
 *
 * ----------------------------------------------------------------------
 */
static RbcMarker *
CreateImageMarker(
    )
{
    ImageMarker    *imPtr;

    imPtr = RbcCalloc(1, sizeof(ImageMarker));
    if (imPtr != NULL) {
        imPtr->classPtr = &imageMarkerClass;
    }
    return (RbcMarker *) imPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureTextMarker --
 *
 *      This procedure is called to process an argv/argc list, plus
 *      the Tk option database, in order to configure (or
 *      reconfigure) a text marker.
 *
 * Results:
 *      A standard Tcl result.  If TCL_ERROR is returned, then
 *      interp->result contains an error message.
 *
 * Side effects:
 *      Configuration information, such as text string, colors, font,
 *      etc. get set for markerPtr;  old resources get freed, if there
 *      were any.  The marker is eventually redisplayed.
 *
 * ----------------------------------------------------------------------
 */
static int
ConfigureTextMarker(
    RbcMarker * markerPtr)
{
    RbcGraph       *graphPtr = markerPtr->graphPtr;
    TextMarker     *tmPtr = (TextMarker *) markerPtr;
    GC              newGC;
    XGCValues       gcValues;
    unsigned long   gcMask;

    tmPtr->style.theta = FMOD(tmPtr->style.theta, 360.0);
    if (tmPtr->style.theta < 0.0) {
        tmPtr->style.theta += 360.0;
    }
    newGC = NULL;
    if (tmPtr->fillColor != NULL) {
        gcMask = GCForeground;
        gcValues.foreground = tmPtr->fillColor->pixel;
        newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    }
    if (tmPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, tmPtr->fillGC);
    }
    tmPtr->fillGC = newGC;
    RbcResetTextStyle(graphPtr->tkwin, &tmPtr->style);

    if (RbcConfigModified(tmPtr->classPtr->configSpecs, "-text", (char *) NULL)) {
        if (tmPtr->textPtr != NULL) {
            ckfree((char *) tmPtr->textPtr);
            tmPtr->textPtr = NULL;
        }
        tmPtr->width = tmPtr->height = 0;
        if (tmPtr->string != NULL) {
            register int    i;
            double          rotWidth, rotHeight;

            tmPtr->textPtr = RbcGetTextLayout(tmPtr->string, &tmPtr->style);
            RbcGetBoundingBox(tmPtr->textPtr->width, tmPtr->textPtr->height,
                tmPtr->style.theta, &rotWidth, &rotHeight, tmPtr->outline);
            tmPtr->width = ROUND(rotWidth);
            tmPtr->height = ROUND(rotHeight);
            for (i = 0; i < 4; i++) {
                tmPtr->outline[i].x += ROUND(rotWidth * 0.5);
                tmPtr->outline[i].y += ROUND(rotHeight * 0.5);
            }
            tmPtr->outline[4].x = tmPtr->outline[0].x;
            tmPtr->outline[4].y = tmPtr->outline[0].y;
        }
    }
    tmPtr->flags |= RBC_MAP_ITEM;
    if (tmPtr->drawUnder) {
        graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
    }
    RbcEventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * MapTextMarker --
 *
 *      Calculate the layout position for a text marker.  Positional
 *      information is saved in the marker.  If the text is rotated,
 *      a bitmap containing the text is created.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      If no background color has been specified, the GC stipple
 *      origins are changed to current window coordinates. For both
 *      rotated and non-rotated text, if any old bitmap is leftover,
 *      it is freed.
 *
 * ----------------------------------------------------------------------
 */
static void
MapTextMarker(
    RbcMarker * markerPtr)
{
    RbcGraph       *graphPtr = markerPtr->graphPtr;
    TextMarker     *tmPtr = (TextMarker *) markerPtr;
    RbcExtents2D    exts;
    RbcPoint2D      anchorPos;

    if (tmPtr->string == NULL) {
        return;
    }
    anchorPos = MapPoint(graphPtr, tmPtr->worldPts, &tmPtr->axes);
    anchorPos = RbcTranslatePoint(&anchorPos, tmPtr->width, tmPtr->height,
        tmPtr->anchor);
    anchorPos.x += tmPtr->xOffset;
    anchorPos.y += tmPtr->yOffset;
    /*
     * Determine the bounding box of the text and test to see if it
     * is at least partially contained within the plotting area.
     */
    exts.left = anchorPos.x;
    exts.top = anchorPos.y;
    exts.right = anchorPos.x + tmPtr->width - 1;
    exts.bottom = anchorPos.y + tmPtr->height - 1;
    tmPtr->clipped = BoxesDontOverlap(graphPtr, &exts);
    tmPtr->anchorPos = anchorPos;

}

/*
 *----------------------------------------------------------------------
 *
 * PointInTextMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
PointInTextMarker(
    RbcMarker * markerPtr,
    RbcPoint2D * samplePtr)
{
    TextMarker     *tmPtr = (TextMarker *) markerPtr;

    if (tmPtr->string == NULL) {
        return 0;
    }
    if (tmPtr->style.theta != 0.0) {
        RbcPoint2D      points[5];
        register int    i;

        /*
         * Figure out the bounding polygon (isolateral) for the text
         * and see if the point is inside of it.
         */

        for (i = 0; i < 5; i++) {
            points[i].x = tmPtr->outline[i].x + tmPtr->anchorPos.x;
            points[i].y = tmPtr->outline[i].y + tmPtr->anchorPos.y;
        }
        return RbcPointInPolygon(samplePtr, points, 5);
    }
    return ((samplePtr->x >= tmPtr->anchorPos.x) &&
        (samplePtr->x < (tmPtr->anchorPos.x + tmPtr->width)) &&
        (samplePtr->y >= tmPtr->anchorPos.y) &&
        (samplePtr->y < (tmPtr->anchorPos.y + tmPtr->height)));
}

/*
 *----------------------------------------------------------------------
 *
 * RegionInTextMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
RegionInTextMarker(
    RbcMarker * markerPtr,
    RbcExtents2D * extsPtr,
    int enclosed)
{
    TextMarker     *tmPtr = (TextMarker *) markerPtr;

    if (tmPtr->nWorldPts < 1) {
        return FALSE;
    }
    if (tmPtr->style.theta != 0.0) {
        RbcPoint2D      points[5];
        register int    i;

        /*
         * Generate the bounding polygon (isolateral) for the bitmap
         * and see if the point is inside of it.
         */
        for (i = 0; i < 4; i++) {
            points[i].x = tmPtr->outline[i].x + tmPtr->anchorPos.x;
            points[i].y = tmPtr->outline[i].y + tmPtr->anchorPos.y;
        }
        return RbcRegionInPolygon(extsPtr, points, 4, enclosed);
    }
    if (enclosed) {
        return ((tmPtr->anchorPos.x >= extsPtr->left) &&
            (tmPtr->anchorPos.y >= extsPtr->top) &&
            ((tmPtr->anchorPos.x + tmPtr->width) <= extsPtr->right) &&
            ((tmPtr->anchorPos.y + tmPtr->height) <= extsPtr->bottom));
    }
    return !((tmPtr->anchorPos.x >= extsPtr->right) ||
        (tmPtr->anchorPos.y >= extsPtr->bottom) ||
        ((tmPtr->anchorPos.x + tmPtr->width) <= extsPtr->left) ||
        ((tmPtr->anchorPos.y + tmPtr->height) <= extsPtr->top));
}

/*
 * ----------------------------------------------------------------------
 *
 * DrawTextMarker --
 *
 *      Draws the text marker on the graph.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Commands are output to X to draw the marker in its current
 *      mode.
 *
 * ----------------------------------------------------------------------
 */
static void
DrawTextMarker(
    RbcMarker * markerPtr,
    Drawable drawable)
{                               /* Pixmap or window to draw into */
    TextMarker     *tmPtr = (TextMarker *) markerPtr;
    RbcGraph       *graphPtr = markerPtr->graphPtr;

    if (tmPtr->string == NULL) {
        return;
    }
    if (tmPtr->fillGC != NULL) {
        XPoint          pointArr[4];
        register int    i;

        /*
         * Simulate the rotated background of the bitmap by
         * filling a bounding polygon with the background color.
         */
        for (i = 0; i < 4; i++) {
            pointArr[i].x = (short int)
                (tmPtr->outline[i].x + tmPtr->anchorPos.x);
            pointArr[i].y = (short int)
                (tmPtr->outline[i].y + tmPtr->anchorPos.y);
        }
        XFillPolygon(graphPtr->display, drawable, tmPtr->fillGC, pointArr, 4,
            Convex, CoordModeOrigin);
    }
    if (tmPtr->style.color != NULL) {
        RbcDrawTextLayout(graphPtr->tkwin, drawable, tmPtr->textPtr,
            &tmPtr->style, (int) tmPtr->anchorPos.x, (int) tmPtr->anchorPos.y);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * TextMarkerToPostScript --
 *
 *      Outputs PostScript commands to draw a text marker at a given
 *      x,y coordinate, rotation, anchor, and font.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      PostScript font and color settings are changed.
 *
 * ----------------------------------------------------------------------
 */
static void
TextMarkerToPostScript(
    RbcMarker * markerPtr,
    RbcPsToken * psToken)
{
    TextMarker     *tmPtr = (TextMarker *) markerPtr;

    if (tmPtr->string == NULL) {
        return;
    }
    if (tmPtr->fillGC != NULL) {
        RbcPoint2D      polygon[4];
        register int    i;

        /*
         * Simulate the rotated background of the bitmap by
         * filling a bounding polygon with the background color.
         */
        for (i = 0; i < 4; i++) {
            polygon[i].x = tmPtr->outline[i].x + tmPtr->anchorPos.x;
            polygon[i].y = tmPtr->outline[i].y + tmPtr->anchorPos.y;
        }
        RbcBackgroundToPostScript(psToken, tmPtr->fillColor);
        RbcPolygonToPostScript(psToken, polygon, 4);
    }
    RbcTextToPostScript(psToken, tmPtr->string, &tmPtr->style,
        tmPtr->anchorPos.x, tmPtr->anchorPos.y);
}

/*
 * ----------------------------------------------------------------------
 *
 * FreeTextMarker --
 *
 *      Destroys the structure containing the attributes of the text
 *      marker.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Text attributes (GCs, colors, stipple, font, etc) get destroyed.
 *      Memory is released, X resources are freed, and the graph is
 *      redrawn.
 *
 * ----------------------------------------------------------------------
 */
static void
FreeTextMarker(
    RbcGraph * graphPtr,
    RbcMarker * markerPtr)
{
    TextMarker     *tmPtr = (TextMarker *) markerPtr;

    RbcFreeTextStyle(graphPtr->display, &tmPtr->style);
    if (tmPtr->textPtr != NULL) {
        ckfree((char *) tmPtr->textPtr);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateTextMarker --
 *
 *      Allocate memory and initialize methods for the new text marker.
 *
 * Results:
 *      The pointer to the newly allocated marker structure is returned.
 *
 * Side effects:
 *      Memory is allocated for the text marker structure.
 *
 * ----------------------------------------------------------------------
 */
static RbcMarker *
CreateTextMarker(
    )
{
    TextMarker     *tmPtr;

    tmPtr = RbcCalloc(1, sizeof(TextMarker));
    assert(tmPtr);

    tmPtr->classPtr = &textMarkerClass;
    RbcInitTextStyle(&tmPtr->style);
    tmPtr->style.anchor = TK_ANCHOR_NW;
    tmPtr->style.padX.side1 = tmPtr->style.padX.side2 = 4;      /*x */
    tmPtr->style.padY.side1 = tmPtr->style.padY.side2 = 4;      /*y */

    return (RbcMarker *) tmPtr;
}

static Tk_GeomMgr winMarkerMgrInfo = {
    "graph",                    /* Name of geometry manager used by winfo */
    ChildGeometryProc,          /* Procedure to for new geometry requests */
    ChildCustodyProc,           /* Procedure when window is taken away */
};

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureWindowMarker --
 *
 *      This procedure is called to process an argv/argc list, plus
 *      the Tk option database, in order to configure (or reconfigure)
 *      a window marker.
 *
 * Results:
 *      A standard Tcl result.  If TCL_ERROR is returned, then
 *      interp->result contains an error message.
 *
 * Side effects:
 *      Configuration information, such as window pathname, placement,
 *      etc. get set for markerPtr; old resources get freed, if there
 *      were any.  The marker is eventually redisplayed.
 *
 * ----------------------------------------------------------------------
 */
static int
ConfigureWindowMarker(
    RbcMarker * markerPtr)
{
    RbcGraph       *graphPtr = markerPtr->graphPtr;
    WindowMarker   *wmPtr = (WindowMarker *) markerPtr;
    Tk_Window       tkwin;

    if (wmPtr->pathName == NULL) {
        return TCL_OK;
    }
    tkwin = Tk_NameToWindow(graphPtr->interp, wmPtr->pathName, graphPtr->tkwin);
    if (tkwin == NULL) {
        return TCL_ERROR;
    }
    if (Tk_Parent(tkwin) != graphPtr->tkwin) {
        Tcl_AppendResult(graphPtr->interp, "\"", wmPtr->pathName,
            "\" is not a child of \"", Tk_PathName(graphPtr->tkwin), "\"",
            (char *) NULL);
        return TCL_ERROR;
    }
    if (tkwin != wmPtr->tkwin) {
        if (wmPtr->tkwin != NULL) {
            Tk_DeleteEventHandler(wmPtr->tkwin, StructureNotifyMask,
                ChildEventProc, wmPtr);
            Tk_ManageGeometry(wmPtr->tkwin, (Tk_GeomMgr *) 0, (ClientData) 0);
            Tk_UnmapWindow(wmPtr->tkwin);
        }
        Tk_CreateEventHandler(tkwin, StructureNotifyMask, ChildEventProc,
            wmPtr);
        Tk_ManageGeometry(tkwin, &winMarkerMgrInfo, wmPtr);
    }
    wmPtr->tkwin = tkwin;

    wmPtr->flags |= RBC_MAP_ITEM;
    if (wmPtr->drawUnder) {
        graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
    }
    RbcEventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * MapWindowMarker --
 *
 *      Calculate the layout position for a window marker.  Positional
 *      information is saved in the marker.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void
MapWindowMarker(
    RbcMarker * markerPtr)
{
    WindowMarker   *wmPtr = (WindowMarker *) markerPtr;
    RbcGraph       *graphPtr = markerPtr->graphPtr;
    RbcExtents2D    exts;
    int             width, height;

    if (wmPtr->tkwin == (Tk_Window) NULL) {
        return;
    }
    wmPtr->anchorPos = MapPoint(graphPtr, wmPtr->worldPts, &wmPtr->axes);

    width = Tk_ReqWidth(wmPtr->tkwin);
    height = Tk_ReqHeight(wmPtr->tkwin);
    if (wmPtr->reqWidth > 0) {
        width = wmPtr->reqWidth;
    }
    if (wmPtr->reqHeight > 0) {
        height = wmPtr->reqHeight;
    }
    wmPtr->anchorPos = RbcTranslatePoint(&wmPtr->anchorPos, width, height,
        wmPtr->anchor);
    wmPtr->anchorPos.x += wmPtr->xOffset;
    wmPtr->anchorPos.y += wmPtr->yOffset;
    wmPtr->width = width;
    wmPtr->height = height;

    /*
     * Determine the bounding box of the window and test to see if it
     * is at least partially contained within the plotting area.
     */
    exts.left = wmPtr->anchorPos.x;
    exts.top = wmPtr->anchorPos.y;
    exts.right = wmPtr->anchorPos.x + wmPtr->width - 1;
    exts.bottom = wmPtr->anchorPos.y + wmPtr->height - 1;
    wmPtr->clipped = BoxesDontOverlap(graphPtr, &exts);
}

/*
 *----------------------------------------------------------------------
 *
 * PointInWindowMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
PointInWindowMarker(
    RbcMarker * markerPtr,
    RbcPoint2D * samplePtr)
{
    WindowMarker   *wmPtr = (WindowMarker *) markerPtr;

    return ((samplePtr->x >= wmPtr->anchorPos.x) &&
        (samplePtr->x < (wmPtr->anchorPos.x + wmPtr->width)) &&
        (samplePtr->y >= wmPtr->anchorPos.y) &&
        (samplePtr->y < (wmPtr->anchorPos.y + wmPtr->height)));
}

/*
 *----------------------------------------------------------------------
 *
 * RegionInWindowMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
RegionInWindowMarker(
    RbcMarker * markerPtr,
    RbcExtents2D * extsPtr,
    int enclosed)
{
    WindowMarker   *wmPtr = (WindowMarker *) markerPtr;

    if (wmPtr->nWorldPts < 1) {
        return FALSE;
    }
    if (enclosed) {
        return ((wmPtr->anchorPos.x >= extsPtr->left) &&
            (wmPtr->anchorPos.y >= extsPtr->top) &&
            ((wmPtr->anchorPos.x + wmPtr->width) <= extsPtr->right) &&
            ((wmPtr->anchorPos.y + wmPtr->height) <= extsPtr->bottom));
    }
    return !((wmPtr->anchorPos.x >= extsPtr->right) ||
        (wmPtr->anchorPos.y >= extsPtr->bottom) ||
        ((wmPtr->anchorPos.x + wmPtr->width) <= extsPtr->left) ||
        ((wmPtr->anchorPos.y + wmPtr->height) <= extsPtr->top));
}

/*
 *----------------------------------------------------------------------
 *
 * DrawWindowMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void
DrawWindowMarker(
    RbcMarker * markerPtr,
    Drawable drawable)
{                               /* Pixmap or window to draw into */
    WindowMarker   *wmPtr = (WindowMarker *) markerPtr;

    if (wmPtr->tkwin == NULL) {
        return;
    }
    if ((wmPtr->height != Tk_Height(wmPtr->tkwin)) ||
        (wmPtr->width != Tk_Width(wmPtr->tkwin)) ||
        ((int) wmPtr->anchorPos.x != Tk_X(wmPtr->tkwin)) ||
        ((int) wmPtr->anchorPos.y != Tk_Y(wmPtr->tkwin))) {
        Tk_MoveResizeWindow(wmPtr->tkwin, (int) wmPtr->anchorPos.x,
            (int) wmPtr->anchorPos.y, wmPtr->width, wmPtr->height);
    }
    if (!Tk_IsMapped(wmPtr->tkwin)) {
        Tk_MapWindow(wmPtr->tkwin);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * WindowMarkerToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void
WindowMarkerToPostScript(
    RbcMarker * markerPtr,
    RbcPsToken * psToken)
{
    WindowMarker   *wmPtr = (WindowMarker *) markerPtr;

    if (wmPtr->tkwin == NULL) {
        return;
    }
    if (Tk_IsMapped(wmPtr->tkwin)) {
        RbcWindowToPostScript(psToken, wmPtr->tkwin, wmPtr->anchorPos.x,
            wmPtr->anchorPos.y);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * FreeWindowMarker --
 *
 *      Destroys the structure containing the attributes of the window
 *      marker.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Window is destroyed and removed from the screen.
 *
 * ----------------------------------------------------------------------
 */
static void
FreeWindowMarker(
    RbcGraph * graphPtr,
    RbcMarker * markerPtr)
{
    WindowMarker   *wmPtr = (WindowMarker *) markerPtr;

    if (wmPtr->tkwin != NULL) {
        Tk_DeleteEventHandler(wmPtr->tkwin, StructureNotifyMask,
            ChildEventProc, wmPtr);
        Tk_ManageGeometry(wmPtr->tkwin, (Tk_GeomMgr *) 0, (ClientData) 0);
        Tk_DestroyWindow(wmPtr->tkwin);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateWindowMarker --
 *
 *      Allocate memory and initialize methods for the new window marker.
 *
 * Results:
 *      The pointer to the newly allocated marker structure is returned.
 *
 * Side effects:
 *      Memory is allocated for the window marker structure.
 *
 * ----------------------------------------------------------------------
 */
static RbcMarker *
CreateWindowMarker(
    )
{
    WindowMarker   *wmPtr;

    wmPtr = RbcCalloc(1, sizeof(WindowMarker));
    if (wmPtr != NULL) {
        wmPtr->classPtr = &windowMarkerClass;
    }
    return (RbcMarker *) wmPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * ChildEventProc --
 *
 *      This procedure is invoked whenever StructureNotify events
 *      occur for a window that's managed as part of a graph window
 *      marker. This procedure's only purpose is to clean up when
 *      windows are deleted.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The window is disassociated from the window item when it is
 *      deleted.
 *
 * ----------------------------------------------------------------------
 */
static void
ChildEventProc(
    ClientData clientData,      /* Pointer to record describing window item. */
    XEvent * eventPtr)
{                               /* Describes what just happened. */
    WindowMarker   *wmPtr = clientData;

    if (eventPtr->type == DestroyNotify) {
        wmPtr->tkwin = NULL;
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ChildGeometryProc --
 *
 *      This procedure is invoked whenever a window that's associated
 *      with a window item changes its requested dimensions.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The size and location on the window of the window may change,
 *      depending on the options specified for the window item.
 *
 * ----------------------------------------------------------------------
 */
static void
ChildGeometryProc(
    ClientData clientData,      /* Pointer to record for window item. */
    Tk_Window tkwin)
{                               /* Window that changed its desired size. */
    WindowMarker   *wmPtr = clientData;

    if (wmPtr->reqWidth == 0) {
        wmPtr->width = Tk_ReqWidth(tkwin);
    }
    if (wmPtr->reqHeight == 0) {
        wmPtr->height = Tk_ReqHeight(tkwin);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ChildCustodyProc --
 *
 *      This procedure is invoked when an embedded window has been
 *      stolen by another geometry manager.  The information and
 *      memory associated with the widget is released.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Arranges for the graph to be redrawn without the embedded
 *      widget at the next idle point.
 *
 * ----------------------------------------------------------------------
 */
static void
ChildCustodyProc(
    ClientData clientData,      /* Window marker to be destroyed. */
    Tk_Window tkwin)
{                               /* Not used. */
    RbcMarker      *markerPtr = clientData;
    RbcGraph       *graphPtr;

    graphPtr = markerPtr->graphPtr;
    DestroyMarker(markerPtr);
    /*
     * Not really needed. We should get an Expose event when the
     * child window is unmapped.
     */
    RbcEventuallyRedrawGraph(graphPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * MapLineMarker --
 *
 *      Calculate the layout position for a line marker.  Positional
 *      information is saved in the marker.  The line positions are
 *      stored in an array of points (malloc'ed).
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void
MapLineMarker(
    RbcMarker * markerPtr)
{
    RbcGraph       *graphPtr = markerPtr->graphPtr;
    LineMarker     *lmPtr = (LineMarker *) markerPtr;
    RbcPoint2D     *srcPtr, *endPtr;
    RbcSegment2D   *segments, *segPtr;
    RbcPoint2D      p, q, next;
    RbcExtents2D    exts;

    lmPtr->nSegments = 0;
    if (lmPtr->segments != NULL) {
        ckfree((char *) lmPtr->segments);
    }
    if (lmPtr->nWorldPts < 2) {
        return;                 /* Too few points */
    }
    RbcGraphExtents(graphPtr, &exts);

    /*
     * Allow twice the number of world coordinates. The line will
     * represented as series of line segments, not one continous
     * polyline.  This is because clipping against the plot area may
     * chop the line into several disconnected segments.
     */
    segments =
        (RbcSegment2D *) ckalloc(lmPtr->nWorldPts * sizeof(RbcSegment2D));
    srcPtr = lmPtr->worldPts;
    p = MapPoint(graphPtr, srcPtr, &lmPtr->axes);
    p.x += lmPtr->xOffset;
    p.y += lmPtr->yOffset;

    segPtr = segments;
    for (srcPtr++, endPtr = lmPtr->worldPts + lmPtr->nWorldPts;
        srcPtr < endPtr; srcPtr++) {
        next = MapPoint(graphPtr, srcPtr, &lmPtr->axes);
        next.x += lmPtr->xOffset;
        next.y += lmPtr->yOffset;
        q = next;
        if (RbcLineRectClip(&exts, &p, &q)) {
            segPtr->p = p;
            segPtr->q = q;
            segPtr++;
        }
        p = next;
    }
    lmPtr->nSegments = segPtr - segments;
    lmPtr->segments = segments;
    lmPtr->clipped = (lmPtr->nSegments == 0);
}

/*
 *----------------------------------------------------------------------
 *
 * PointInLineMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
PointInLineMarker(
    RbcMarker * markerPtr,
    RbcPoint2D * samplePtr)
{
    LineMarker     *lmPtr = (LineMarker *) markerPtr;

    return RbcPointInSegments(samplePtr, lmPtr->segments, lmPtr->nSegments,
        (double) lmPtr->graphPtr->halo);
}

/*
 *----------------------------------------------------------------------
 *
 * RegionInLineMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
RegionInLineMarker(
    RbcMarker * markerPtr,
    RbcExtents2D * extsPtr,
    int enclosed)
{
    LineMarker     *lmPtr = (LineMarker *) markerPtr;

    if (lmPtr->nWorldPts < 2) {
        return FALSE;
    }
    if (enclosed) {
        RbcPoint2D      p;
        RbcPoint2D     *pointPtr, *endPtr;

        for (pointPtr = lmPtr->worldPts,
            endPtr = lmPtr->worldPts + lmPtr->nWorldPts;
            pointPtr < endPtr; pointPtr++) {
            p = MapPoint(lmPtr->graphPtr, pointPtr, &lmPtr->axes);
            if ((p.x < extsPtr->left) && (p.x > extsPtr->right) &&
                (p.y < extsPtr->top) && (p.y > extsPtr->bottom)) {
                return FALSE;
            }
        }
        return TRUE;            /* All points inside bounding box. */
    } else {
        RbcPoint2D      p, q;
        int             count;
        RbcPoint2D     *pointPtr, *endPtr;

        count = 0;
        for (pointPtr = lmPtr->worldPts,
            endPtr = lmPtr->worldPts + (lmPtr->nWorldPts - 1);
            pointPtr < endPtr; pointPtr++) {
            p = MapPoint(lmPtr->graphPtr, pointPtr, &lmPtr->axes);
            q = MapPoint(lmPtr->graphPtr, pointPtr + 1, &lmPtr->axes);
            if (RbcLineRectClip(extsPtr, &p, &q)) {
                count++;
            }
        }
        return (count > 0);     /* At least 1 segment passes through region. */
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DrawLineMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void
DrawLineMarker(
    RbcMarker * markerPtr,
    Drawable drawable)
{                               /* Pixmap or window to draw into */
    LineMarker     *lmPtr = (LineMarker *) markerPtr;

    if (lmPtr->nSegments > 0) {
        RbcGraph       *graphPtr = markerPtr->graphPtr;

        RbcDraw2DSegments(graphPtr->display, drawable, lmPtr->gc,
            lmPtr->segments, lmPtr->nSegments);
        if (lmPtr->xor) {       /* Toggle the drawing state */
            lmPtr->xorState = (lmPtr->xorState == 0);
        }
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureLineMarker --
 *
 *      This procedure is called to process an argv/argc list, plus
 *      the Tk option database, in order to configure (or reconfigure)
 *      a line marker.
 *
 * Results:
 *      A standard Tcl result.  If TCL_ERROR is returned, then
 *      interp->result contains an error message.
 *
 * Side effects:
 *      Configuration information, such as line width, colors, dashes,
 *      etc. get set for markerPtr; old resources get freed, if there
 *      were any.  The marker is eventually redisplayed.
 *
 * ----------------------------------------------------------------------
 */
static int
ConfigureLineMarker(
    RbcMarker * markerPtr)
{
    RbcGraph       *graphPtr = markerPtr->graphPtr;
    LineMarker     *lmPtr = (LineMarker *) markerPtr;
    GC              newGC;
    XGCValues       gcValues;
    unsigned long   gcMask;
    Drawable        drawable;

    drawable = Tk_WindowId(graphPtr->tkwin);
    gcMask = (GCLineWidth | GCLineStyle | GCCapStyle | GCJoinStyle);
    if (lmPtr->outlineColor != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = lmPtr->outlineColor->pixel;
    }
    if (lmPtr->fillColor != NULL) {
        gcMask |= GCBackground;
        gcValues.background = lmPtr->fillColor->pixel;
    }
    gcValues.cap_style = lmPtr->capStyle;
    gcValues.join_style = lmPtr->joinStyle;
    gcValues.line_width = RbcLineWidth(lmPtr->lineWidth);
    gcValues.line_style = LineSolid;
    if (RbcLineIsDashed(lmPtr->dashes)) {
        gcValues.line_style =
            (gcMask & GCBackground) ? LineDoubleDash : LineOnOffDash;
    }
    if (lmPtr->xor) {
        unsigned long   pixel;
        gcValues.function = GXxor;

        gcMask |= GCFunction;
        if (graphPtr->plotBg == NULL) {
            pixel = WhitePixelOfScreen(Tk_Screen(graphPtr->tkwin));
        } else {
            pixel = graphPtr->plotBg->pixel;
        }
        if (gcMask & GCBackground) {
            gcValues.background ^= pixel;
        }
        gcValues.foreground ^= pixel;
        if (drawable != None) {
            DrawLineMarker(markerPtr, drawable);
        }
    }
    newGC = RbcGetPrivateGC(graphPtr->tkwin, gcMask, &gcValues);
    if (lmPtr->gc != NULL) {
        RbcFreePrivateGC(graphPtr->display, lmPtr->gc);
    }
    if (RbcLineIsDashed(lmPtr->dashes)) {
        RbcSetDashes(graphPtr->display, newGC, &lmPtr->dashes);
    }
    lmPtr->gc = newGC;
    if (lmPtr->xor) {
        if (drawable != None) {
            MapLineMarker(markerPtr);
            DrawLineMarker(markerPtr, drawable);
        }
        return TCL_OK;
    }
    lmPtr->flags |= RBC_MAP_ITEM;
    if (lmPtr->drawUnder) {
        graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
    }
    RbcEventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * LineMarkerToPostScript --
 *
 *      Prints postscript commands to display the connect line.
 *      Dashed lines need to be handled specially, especially if a
 *      background color is designated.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      PostScript output commands are saved in the interpreter
 *      (infoPtr->interp) result field.
 *
 * ----------------------------------------------------------------------
 */
static void
LineMarkerToPostScript(
    RbcMarker * markerPtr,
    RbcPsToken * psToken)
{
    LineMarker     *lmPtr = (LineMarker *) markerPtr;

    if (lmPtr->nSegments > 0) {
        RbcLineAttributesToPostScript(psToken, lmPtr->outlineColor,
            lmPtr->lineWidth, &lmPtr->dashes, lmPtr->capStyle,
            lmPtr->joinStyle);
        if ((RbcLineIsDashed(lmPtr->dashes)) && (lmPtr->fillColor != NULL)) {
            RbcAppendToPostScript(psToken, "/DashesProc {\n  gsave\n    ",
                (char *) NULL);
            RbcBackgroundToPostScript(psToken, lmPtr->fillColor);
            RbcAppendToPostScript(psToken, "    ", (char *) NULL);
            RbcLineDashesToPostScript(psToken, (RbcDashes *) NULL);
            RbcAppendToPostScript(psToken,
                "stroke\n", "  grestore\n", "} def\n", (char *) NULL);
        } else {
            RbcAppendToPostScript(psToken, "/DashesProc {} def\n",
                (char *) NULL);
        }
        Rbc2DSegmentsToPostScript(psToken, lmPtr->segments, lmPtr->nSegments);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * FreeLineMarker --
 *
 *      Destroys the structure and attributes of a line marker.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Line attributes (GCs, colors, stipple, etc) get released.
 *      Memory is deallocated, X resources are freed.
 *
 * ----------------------------------------------------------------------
 */
static void
FreeLineMarker(
    RbcGraph * graphPtr,
    RbcMarker * markerPtr)
{
    LineMarker     *lmPtr = (LineMarker *) markerPtr;

    if (lmPtr->gc != NULL) {
        RbcFreePrivateGC(graphPtr->display, lmPtr->gc);
    }
    if (lmPtr->segments != NULL) {
        ckfree((char *) lmPtr->segments);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateLineMarker --
 *
 *      Allocate memory and initialize methods for a new line marker.
 *
 * Results:
 *      The pointer to the newly allocated marker structure is returned.
 *
 * Side effects:
 *      Memory is allocated for the line marker structure.
 *
 * ----------------------------------------------------------------------
 */
static RbcMarker *
CreateLineMarker(
    )
{
    LineMarker     *lmPtr;

    lmPtr = RbcCalloc(1, sizeof(LineMarker));
    if (lmPtr != NULL) {
        lmPtr->classPtr = &lineMarkerClass;
        lmPtr->xor = FALSE;
        lmPtr->capStyle = CapButt;
        lmPtr->joinStyle = JoinMiter;
    }
    return (RbcMarker *) lmPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * MapPolygonMarker --
 *
 *      Calculate the layout position for a polygon marker.  Positional
 *      information is saved in the polygon in an array of points
 *      (malloc'ed).
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void
MapPolygonMarker(
    RbcMarker * markerPtr)
{
    RbcGraph       *graphPtr = markerPtr->graphPtr;
    PolygonMarker  *pmPtr = (PolygonMarker *) markerPtr;
    RbcPoint2D     *srcPtr, *destPtr, *endPtr;
    RbcPoint2D     *screenPts;
    RbcExtents2D    exts;
    int             nScreenPts;

    if (pmPtr->outlinePts != NULL) {
        ckfree((char *) pmPtr->outlinePts);
        pmPtr->outlinePts = NULL;
        pmPtr->nOutlinePts = 0;
    }
    if (pmPtr->fillPts != NULL) {
        ckfree((char *) pmPtr->fillPts);
        pmPtr->fillPts = NULL;
        pmPtr->nFillPts = 0;
    }
    if (pmPtr->screenPts != NULL) {
        ckfree((char *) pmPtr->screenPts);
        pmPtr->screenPts = NULL;
    }
    if (pmPtr->nWorldPts < 3) {
        return;                 /* Too few points */
    }

    /*
     * Allocate and fill a temporary array to hold the screen
     * coordinates of the polygon.
     */
    nScreenPts = pmPtr->nWorldPts + 1;
    screenPts = (RbcPoint2D *) ckalloc((nScreenPts + 1) * sizeof(RbcPoint2D));
    endPtr = pmPtr->worldPts + pmPtr->nWorldPts;
    destPtr = screenPts;
    for (srcPtr = pmPtr->worldPts; srcPtr < endPtr; srcPtr++) {
        *destPtr = MapPoint(graphPtr, srcPtr, &pmPtr->axes);
        destPtr->x += pmPtr->xOffset;
        destPtr->y += pmPtr->yOffset;
        destPtr++;
    }
    *destPtr = screenPts[0];

    RbcGraphExtents(graphPtr, &exts);
    pmPtr->clipped = TRUE;
    if (pmPtr->fill.fgColor != NULL) {  /* Polygon fill required. */
        RbcPoint2D     *fillPts;
        int             n;

        fillPts = (RbcPoint2D *) ckalloc(sizeof(RbcPoint2D) * nScreenPts * 3);
        assert(fillPts);
        n = RbcPolyRectClip(&exts, screenPts, pmPtr->nWorldPts, fillPts);
        if (n < 3) {
            ckfree((char *) fillPts);
        } else {
            pmPtr->nFillPts = n;
            pmPtr->fillPts = fillPts;
            pmPtr->clipped = FALSE;
        }
    }
    if ((pmPtr->outline.fgColor != NULL) && (pmPtr->lineWidth > 0)) {
        RbcSegment2D   *outlinePts;
        register RbcSegment2D *segPtr;
        /*
         * Generate line segments representing the polygon outline.
         * The resulting outline may or may not be closed from
         * viewport clipping.
         */
        outlinePts =
            (RbcSegment2D *) ckalloc(nScreenPts * sizeof(RbcSegment2D));
        if (outlinePts == NULL) {
            return;             /* Can't allocate point array */
        }
        /*
         * Note that this assumes that the point array contains an
         * extra point that closes the polygon.
         */
        segPtr = outlinePts;
        for (srcPtr = screenPts, endPtr = screenPts + (nScreenPts - 1);
            srcPtr < endPtr; srcPtr++) {
            segPtr->p = srcPtr[0];
            segPtr->q = srcPtr[1];
            if (RbcLineRectClip(&exts, &segPtr->p, &segPtr->q)) {
                segPtr++;
            }
        }
        pmPtr->nOutlinePts = segPtr - outlinePts;
        pmPtr->outlinePts = outlinePts;
        if (pmPtr->nOutlinePts > 0) {
            pmPtr->clipped = FALSE;
        }
    }
    pmPtr->screenPts = screenPts;
}

/*
 *----------------------------------------------------------------------
 *
 * PointInPolygonMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
PointInPolygonMarker(
    RbcMarker * markerPtr,
    RbcPoint2D * samplePtr)
{
    PolygonMarker  *pmPtr = (PolygonMarker *) markerPtr;

    if (pmPtr->nWorldPts < 2) {
        return FALSE;
    }
    return RbcPointInPolygon(samplePtr, pmPtr->screenPts, pmPtr->nWorldPts + 1);
}

/*
 *----------------------------------------------------------------------
 *
 * RegionInPolygonMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
RegionInPolygonMarker(
    RbcMarker * markerPtr,
    RbcExtents2D * extsPtr,
    int enclosed)
{
    PolygonMarker  *pmPtr = (PolygonMarker *) markerPtr;

    if (pmPtr->nWorldPts >= 3) {
        return RbcRegionInPolygon(extsPtr, pmPtr->screenPts, pmPtr->nWorldPts,
            enclosed);
    }
    return FALSE;
}

/*
 *----------------------------------------------------------------------
 *
 * DrawPolygonMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void
DrawPolygonMarker(
    RbcMarker * markerPtr,
    Drawable drawable)
{                               /* Pixmap or window to draw into */
    RbcGraph       *graphPtr = markerPtr->graphPtr;
    PolygonMarker  *pmPtr = (PolygonMarker *) markerPtr;

    /* Draw polygon fill region */
    if ((pmPtr->nFillPts > 0) && (pmPtr->fill.fgColor != NULL)) {
        XPoint         *destPtr, *pointArr;
        RbcPoint2D     *srcPtr, *endPtr;

        pointArr = (XPoint *) ckalloc(pmPtr->nFillPts * sizeof(XPoint));
        if (pointArr == NULL) {
            return;
        }
        destPtr = pointArr;
        for (srcPtr = pmPtr->fillPts,
            endPtr = pmPtr->fillPts + pmPtr->nFillPts; srcPtr < endPtr;
            srcPtr++) {
            destPtr->x = (short int) srcPtr->x;
            destPtr->y = (short int) srcPtr->y;
            destPtr++;
        }
        XFillPolygon(graphPtr->display, drawable, pmPtr->fillGC,
            pointArr, pmPtr->nFillPts, Complex, CoordModeOrigin);
        ckfree((char *) pointArr);
    }
    /* and then the outline */
    if ((pmPtr->nOutlinePts > 0) && (pmPtr->lineWidth > 0) &&
        (pmPtr->outline.fgColor != NULL)) {
        RbcDraw2DSegments(graphPtr->display, drawable, pmPtr->outlineGC,
            pmPtr->outlinePts, pmPtr->nOutlinePts);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PolygonMarkerToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void
PolygonMarkerToPostScript(
    RbcMarker * markerPtr,
    RbcPsToken * psToken)
{
    RbcGraph       *graphPtr = markerPtr->graphPtr;
    PolygonMarker  *pmPtr = (PolygonMarker *) markerPtr;

    if (pmPtr->fill.fgColor != NULL) {

        /*
         * Options:  fg bg
         *                      Draw outline only.
         *           x          Draw solid or stipple.
         *           x  x       Draw solid or stipple.
         */

        /* Create a path to use for both the polygon and its outline. */
        RbcPathToPostScript(psToken, pmPtr->fillPts, pmPtr->nFillPts);
        RbcAppendToPostScript(psToken, "closepath\n", (char *) NULL);

        /* If the background fill color was specified, draw the
         * polygon in a solid fashion with that color.  */
        if (pmPtr->fill.bgColor != NULL) {
            RbcBackgroundToPostScript(psToken, pmPtr->fill.bgColor);
            RbcAppendToPostScript(psToken, "Fill\n", (char *) NULL);
        }
        RbcForegroundToPostScript(psToken, pmPtr->fill.fgColor);
        if (pmPtr->stipple != None) {
            /* Draw the stipple in the foreground color. */
            RbcStippleToPostScript(psToken, graphPtr->display, pmPtr->stipple);
        } else {
            RbcAppendToPostScript(psToken, "Fill\n", (char *) NULL);
        }
    }

    /* Draw the outline in the foreground color.  */
    if ((pmPtr->lineWidth > 0) && (pmPtr->outline.fgColor != NULL)) {

        /*  Set up the line attributes.  */
        RbcLineAttributesToPostScript(psToken, pmPtr->outline.fgColor,
            pmPtr->lineWidth, &pmPtr->dashes, pmPtr->capStyle,
            pmPtr->joinStyle);

        /*
         * Define on-the-fly a PostScript macro "DashesProc" that
         * will be executed for each call to the Polygon drawing
         * routine.  If the line isn't dashed, simply make this an
         * empty definition.
         */
        if ((pmPtr->outline.bgColor != NULL)
            && (RbcLineIsDashed(pmPtr->dashes))) {
            RbcAppendToPostScript(psToken, "/DashesProc {\n", "gsave\n    ",
                (char *) NULL);
            RbcBackgroundToPostScript(psToken, pmPtr->outline.bgColor);
            RbcAppendToPostScript(psToken, "    ", (char *) NULL);
            RbcLineDashesToPostScript(psToken, (RbcDashes *) NULL);
            RbcAppendToPostScript(psToken,
                "stroke\n", "  grestore\n", "} def\n", (char *) NULL);
        } else {
            RbcAppendToPostScript(psToken, "/DashesProc {} def\n",
                (char *) NULL);
        }
        Rbc2DSegmentsToPostScript(psToken, pmPtr->outlinePts,
            pmPtr->nOutlinePts);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigurePolygonMarker --
 *
 *      This procedure is called to process an argv/argc list, plus
 *      the Tk option database, in order to configure (or reconfigure)
 *      a polygon marker.
 *
 * Results:
 *      A standard Tcl result.  If TCL_ERROR is returned, then
 *      interp->result contains an error message.
 *
 * Side effects:
 *      Configuration information, such as polygon color, dashes,
 *      fillstyle, etc. get set for markerPtr; old resources get
 *      freed, if there were any.  The marker is eventually
 *      redisplayed.
 *
 * ----------------------------------------------------------------------
 */
static int
ConfigurePolygonMarker(
    RbcMarker * markerPtr)
{
    RbcGraph       *graphPtr = markerPtr->graphPtr;
    PolygonMarker  *pmPtr = (PolygonMarker *) markerPtr;
    GC              newGC;
    XGCValues       gcValues;
    unsigned long   gcMask;
    Drawable        drawable;

    drawable = Tk_WindowId(graphPtr->tkwin);
    gcMask = (GCLineWidth | GCLineStyle);
    if (pmPtr->outline.fgColor != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = pmPtr->outline.fgColor->pixel;
    }
    if (pmPtr->outline.bgColor != NULL) {
        gcMask |= GCBackground;
        gcValues.background = pmPtr->outline.bgColor->pixel;
    }
    gcMask |= (GCCapStyle | GCJoinStyle);
    gcValues.cap_style = pmPtr->capStyle;
    gcValues.join_style = pmPtr->joinStyle;
    gcValues.line_style = LineSolid;
    gcValues.dash_offset = 0;
    gcValues.line_width = RbcLineWidth(pmPtr->lineWidth);
    if (RbcLineIsDashed(pmPtr->dashes)) {
        gcValues.line_style = (pmPtr->outline.bgColor == NULL)
            ? LineOnOffDash : LineDoubleDash;
    }
    if (pmPtr->xor) {
        unsigned long   pixel;
        gcValues.function = GXxor;

        gcMask |= GCFunction;
        if (graphPtr->plotBg == NULL) {
            /* The graph's color option may not have been set yet */
            pixel = WhitePixelOfScreen(Tk_Screen(graphPtr->tkwin));
        } else {
            pixel = graphPtr->plotBg->pixel;
        }
        if (gcMask & GCBackground) {
            gcValues.background ^= pixel;
        }
        gcValues.foreground ^= pixel;
        if (drawable != None) {
            DrawPolygonMarker(markerPtr, drawable);
        }
    }
    newGC = RbcGetPrivateGC(graphPtr->tkwin, gcMask, &gcValues);
    if (RbcLineIsDashed(pmPtr->dashes)) {
        RbcSetDashes(graphPtr->display, newGC, &pmPtr->dashes);
    }
    if (pmPtr->outlineGC != NULL) {
        RbcFreePrivateGC(graphPtr->display, pmPtr->outlineGC);
    }
    pmPtr->outlineGC = newGC;

    gcMask = 0;
    if (pmPtr->fill.fgColor != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = pmPtr->fill.fgColor->pixel;
    }
    if (pmPtr->fill.bgColor != NULL) {
        gcMask |= GCBackground;
        gcValues.background = pmPtr->fill.bgColor->pixel;
    }
    if (pmPtr->stipple != None) {
        gcValues.stipple = pmPtr->stipple;
        gcValues.fill_style = (pmPtr->fill.bgColor != NULL)
            ? FillOpaqueStippled : FillStippled;
        gcMask |= (GCStipple | GCFillStyle);
    }
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (pmPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, pmPtr->fillGC);
    }
    pmPtr->fillGC = newGC;

    if ((gcMask == 0) && !(graphPtr->flags & RBC_RESET_AXES) && (pmPtr->xor)) {
        if (drawable != None) {
            MapPolygonMarker(markerPtr);
            DrawPolygonMarker(markerPtr, drawable);
        }
        return TCL_OK;
    }
    pmPtr->flags |= RBC_MAP_ITEM;
    if (pmPtr->drawUnder) {
        graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
    }
    RbcEventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * FreePolygonMarker --
 *
 *      Release memory and resources allocated for the polygon element.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Everything associated with the polygon element is freed up.
 *
 * ----------------------------------------------------------------------
 */
static void
FreePolygonMarker(
    RbcGraph * graphPtr,
    RbcMarker * markerPtr)
{
    PolygonMarker  *pmPtr = (PolygonMarker *) markerPtr;

    if (pmPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, pmPtr->fillGC);
    }
    if (pmPtr->outlineGC != NULL) {
        RbcFreePrivateGC(graphPtr->display, pmPtr->outlineGC);
    }
    if (pmPtr->fillPts != NULL) {
        ckfree((char *) pmPtr->fillPts);
    }
    if (pmPtr->outlinePts != NULL) {
        ckfree((char *) pmPtr->outlinePts);
    }
    RbcFreeColorPair(&pmPtr->outline);
    RbcFreeColorPair(&pmPtr->fill);
}

/*
 * ----------------------------------------------------------------------
 *
 * CreatePolygonMarker --
 *
 *      Allocate memory and initialize methods for the new polygon
 *      marker.
 *
 * Results:
 *      The pointer to the newly allocated marker structure is
 *      returned.
 *
 * Side effects:
 *      Memory is allocated for the polygon marker structure.
 *
 * ----------------------------------------------------------------------
 */
static RbcMarker *
CreatePolygonMarker(
    )
{
    PolygonMarker  *pmPtr;

    pmPtr = RbcCalloc(1, sizeof(PolygonMarker));
    if (pmPtr != NULL) {
        pmPtr->classPtr = &polygonMarkerClass;
        pmPtr->capStyle = CapButt;
        pmPtr->joinStyle = JoinMiter;

    }
    return (RbcMarker *) pmPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * NameToMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
NameToMarker(
    RbcGraph * graphPtr,
    const char *name,
    RbcMarker ** markerPtrPtr)
{
    Tcl_HashEntry  *hPtr;

    hPtr = Tcl_FindHashEntry(&graphPtr->markers.table, name);
    if (hPtr != NULL) {
        *markerPtrPtr = (RbcMarker *) Tcl_GetHashValue(hPtr);
        return TCL_OK;
    }
    Tcl_AppendResult(graphPtr->interp, "can't find marker \"", name,
        "\" in \"", Tk_PathName(graphPtr->tkwin), (char *) NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * RenameMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
RenameMarker(
    RbcGraph * graphPtr,
    RbcMarker * markerPtr,
    char *oldName,
    char *newName)
{
    int             isNew;
    Tcl_HashEntry  *hPtr;

    /* Rename the marker only if no marker already exists by that name */
    hPtr = Tcl_CreateHashEntry(&graphPtr->markers.table, newName, &isNew);
    if (!isNew) {
        Tcl_AppendResult(graphPtr->interp, "can't rename marker: \"", newName,
            "\" already exists", (char *) NULL);
        return TCL_ERROR;
    }
    markerPtr->name = RbcStrdup(newName);
    markerPtr->hashPtr = hPtr;
    Tcl_SetHashValue(hPtr, (char *) markerPtr);

    /* Delete the old hash entry */
    hPtr = Tcl_FindHashEntry(&graphPtr->markers.table, oldName);
    Tcl_DeleteHashEntry(hPtr);
    if (oldName != NULL) {
        ckfree((char *) oldName);
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * NamesOp --
 *
 *      Returns a list of marker identifiers in interp->result;
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
NamesOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcMarker      *markerPtr;
    RbcChainLink   *linkPtr;
    register int    i;

    Tcl_ResetResult(interp);
    for (linkPtr = RbcChainFirstLink(graphPtr->markers.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        markerPtr = RbcChainGetValue(linkPtr);
        if (argc == 3) {
            Tcl_AppendElement(interp, markerPtr->name);
            continue;
        }
        for (i = 3; i < argc; i++) {
            if (Tcl_StringMatch(markerPtr->name, argv[i])) {
                Tcl_AppendElement(interp, markerPtr->name);
                break;
            }
        }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcMakeMarkerTag --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
ClientData
RbcMakeMarkerTag(
    RbcGraph * graphPtr,
    const char *tagName)
{
    Tcl_HashEntry  *hPtr;
    int             isNew;

    hPtr = Tcl_CreateHashEntry(&graphPtr->markers.tagTable, tagName, &isNew);
    assert(hPtr);
    return Tcl_GetHashKey(&graphPtr->markers.tagTable, hPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * BindOp --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
BindOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    if (argc == 3) {
        Tcl_HashEntry  *hPtr;
        Tcl_HashSearch  cursor;
        char           *tag;

        for (hPtr = Tcl_FirstHashEntry(&graphPtr->markers.tagTable, &cursor);
            hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
            tag = Tcl_GetHashKey(&graphPtr->markers.tagTable, hPtr);
            Tcl_AppendElement(interp, tag);
        }
        return TCL_OK;
    }
    return RbcConfigureBindings(interp, graphPtr->bindTable,
        RbcMakeMarkerTag(graphPtr, argv[3]), argc - 4, argv + 4);
}

/*
 *----------------------------------------------------------------------
 *
 * CgetOp --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
CgetOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcMarker      *markerPtr;

    if (NameToMarker(graphPtr, argv[3], &markerPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tk_ConfigureValue(interp, graphPtr->tkwin,
            markerPtr->classPtr->configSpecs, (char *) markerPtr, argv[4], 0)
        != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      TODO: Description
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
ConfigureOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcMarker      *markerPtr;
    int             flags = TK_CONFIG_ARGV_ONLY;
    char           *oldName;
    int             nNames, nOpts;
    const char    **options;
    register int    i;

    /* Figure out where the option value pairs begin */
    argc -= 3;
    argv += 3;
    for (i = 0; i < argc; i++) {
        if (argv[i][0] == '-') {
            break;
        }
        if (NameToMarker(graphPtr, argv[i], &markerPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    nNames = i;                 /* Number of element names specified */
    nOpts = argc - i;           /* Number of options specified */
    options = argv + nNames;    /* Start of options in argv  */

    for (i = 0; i < nNames; i++) {
        NameToMarker(graphPtr, argv[i], &markerPtr);
        if (nOpts == 0) {
            return Tk_ConfigureInfo(interp, graphPtr->tkwin,
                markerPtr->classPtr->configSpecs, (char *) markerPtr,
                (char *) NULL, flags);
        } else if (nOpts == 1) {
            return Tk_ConfigureInfo(interp, graphPtr->tkwin,
                markerPtr->classPtr->configSpecs, (char *) markerPtr,
                options[0], flags);
        }
        /* Save the old marker. */
        oldName = markerPtr->name;
        if (Tk_ConfigureWidget(interp, graphPtr->tkwin,
                markerPtr->classPtr->configSpecs, nOpts, options,
                (char *) markerPtr, flags) != TCL_OK) {
            return TCL_ERROR;
        }
        if (oldName != markerPtr->name) {
            if (RenameMarker(graphPtr, markerPtr, oldName, markerPtr->name)
                != TCL_OK) {
                markerPtr->name = oldName;
                return TCL_ERROR;
            }
        }
        if ((*markerPtr->classPtr->configProc) (markerPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateOp --
 *
 *      This procedure creates and initializes a new marker.
 *
 * Results:
 *      The return value is a pointer to a structure describing
 *      the new element.  If an error occurred, then the return
 *      value is NULL and an error message is left in interp->result.
 *
 * Side effects:
 *      Memory is allocated, etc.
 *
 * ----------------------------------------------------------------------
 */
static int
CreateOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcMarker      *markerPtr;
    Tcl_HashEntry  *hPtr;
    int             isNew;
    RbcUid          classUid;
    register int    i;
    const char     *name;
    char            string[200];
    unsigned int    length;
    char            c;

    c = argv[3][0];
    /* Create the new marker based upon the given type */
    if ((c == 't') && (strcmp(argv[3], "text") == 0)) {
        classUid = rbcTextMarkerUid;
    } else if ((c == 'l') && (strcmp(argv[3], "line") == 0)) {
        classUid = rbcLineMarkerUid;
    } else if ((c == 'p') && (strcmp(argv[3], "polygon") == 0)) {
        classUid = rbcPolygonMarkerUid;
    } else if ((c == 'i') && (strcmp(argv[3], "image") == 0)) {
        classUid = rbcImageMarkerUid;
    } else if ((c == 'b') && (strcmp(argv[3], "bitmap") == 0)) {
        classUid = rbcBitmapMarkerUid;
    } else if ((c == 'w') && (strcmp(argv[3], "window") == 0)) {
        classUid = rbcWindowMarkerUid;
    } else {
        Tcl_AppendResult(interp, "unknown marker type \"", argv[3],
            "\": should be \"text\", \"line\", \"polygon\", \"bitmap\", \"image\", or \
\"window\"", (char *) NULL);
        return TCL_ERROR;
    }
    /* Scan for "-name" option. We need it for the component name */
    name = NULL;
    for (i = 4; i < argc; i += 2) {
        length = strlen(argv[i]);
        if ((length > 1) && (strncmp(argv[i], "-name", length) == 0)) {
            name = argv[i + 1];
            break;
        }
    }
    /* If no name was given for the marker, make up one. */
    if (name == NULL) {
        sprintf(string, "marker%d", graphPtr->nextMarkerId++);
        name = string;
    } else if (name[0] == '-') {
        Tcl_AppendResult(interp, "name of marker \"", name,
            "\" can't start with a '-'", (char *) NULL);
        return TCL_ERROR;
    }
    markerPtr = CreateMarker(graphPtr, name, classUid);
    if (RbcConfigureWidgetComponent(interp, graphPtr->tkwin, name,
            markerPtr->classUid, markerPtr->classPtr->configSpecs,
            argc - 4, argv + 4, (char *) markerPtr, 0) != TCL_OK) {
        DestroyMarker(markerPtr);
        return TCL_ERROR;
    }
    if ((*markerPtr->classPtr->configProc) (markerPtr) != TCL_OK) {
        DestroyMarker(markerPtr);
        return TCL_ERROR;
    }
    hPtr = Tcl_CreateHashEntry(&graphPtr->markers.table, name, &isNew);
    if (!isNew) {
        RbcMarker      *oldMarkerPtr;
        /*
         * Marker by the same name already exists.  Delete the old
         * marker and it's list entry.  But save the hash entry.
         */
        oldMarkerPtr = (RbcMarker *) Tcl_GetHashValue(hPtr);
        oldMarkerPtr->hashPtr = NULL;
        DestroyMarker(oldMarkerPtr);
    }
    Tcl_SetHashValue(hPtr, markerPtr);
    markerPtr->hashPtr = hPtr;
    markerPtr->linkPtr =
        RbcChainAppend(graphPtr->markers.displayList, markerPtr);
    if (markerPtr->drawUnder) {
        graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
    }
    RbcEventuallyRedrawGraph(graphPtr);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(name, -1));
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * DeleteOp --
 *
 *      Deletes the marker given by markerId.
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      Graph will be redrawn to reflect the new display list.
 *
 * ----------------------------------------------------------------------
 */
static int
DeleteOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,        /* Not used. */
    int argc,
    const char **argv)
{
    RbcMarker      *markerPtr;
    register int    i;

    for (i = 3; i < argc; i++) {
        if (NameToMarker(graphPtr, argv[i], &markerPtr) == TCL_OK) {
            DestroyMarker(markerPtr);
        }
    }
    Tcl_ResetResult(interp);
    RbcEventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GetOp --
 *
 *      Find the legend entry from the given argument.  The argument
 *      can be either a screen position "@x,y" or the name of an
 *      element.
 *
 *      I don't know how useful it is to test with the name of an
 *      element.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Graph will be redrawn to reflect the new legend attributes.
 *
 *----------------------------------------------------------------------
 */
static int
GetOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,                   /* Not used. */
    const char *argv[])
{
    register RbcMarker *markerPtr;

    if ((argv[3][0] == 'c') && (strcmp(argv[3], "current") == 0)) {
        markerPtr = (RbcMarker *) RbcGetCurrentItem(graphPtr->bindTable);
        /* Report only on markers. */
        if (markerPtr == NULL) {
            return TCL_OK;
        }
        if ((markerPtr->classUid == rbcBitmapMarkerUid) ||
            (markerPtr->classUid == rbcLineMarkerUid) ||
            (markerPtr->classUid == rbcWindowMarkerUid) ||
            (markerPtr->classUid == rbcPolygonMarkerUid) ||
            (markerPtr->classUid == rbcTextMarkerUid) ||
            (markerPtr->classUid == rbcImageMarkerUid)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(markerPtr->name, -1));
        }
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * RelinkOp --
 *
 *      Reorders the marker (given by the first name) before/after
 *      the another marker (given by the second name) in the
 *      marker display list.  If no second name is given, the
 *      marker is placed at the beginning/end of the list.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Graph will be redrawn to reflect the new display list.
 *
 * ----------------------------------------------------------------------
 */
static int
RelinkOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,        /* Not used. */
    int argc,
    const char **argv)
{
    RbcChainLink   *linkPtr, *placePtr;
    RbcMarker      *markerPtr;

    /* Find the marker to be raised or lowered. */
    if (NameToMarker(graphPtr, argv[3], &markerPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    /* Right now it's assumed that all markers are always in the
       display list. */
    linkPtr = markerPtr->linkPtr;
    RbcChainUnlinkLink(graphPtr->markers.displayList, markerPtr->linkPtr);

    placePtr = NULL;
    if (argc == 5) {
        if (NameToMarker(graphPtr, argv[4], &markerPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        placePtr = markerPtr->linkPtr;
    }

    /* Link the marker at its new position. */
    if (argv[2][0] == 'a') {
        RbcChainLinkAfter(graphPtr->markers.displayList, linkPtr, placePtr);
    } else {
        RbcChainLinkBefore(graphPtr->markers.displayList, linkPtr, placePtr);
    }
    if (markerPtr->drawUnder) {
        graphPtr->flags |= RBC_REDRAW_BACKING_STORE;
    }
    RbcEventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * FindOp --
 *
 *      Returns if marker by a given ID currently exists.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
FindOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcChainLink   *linkPtr;
    RbcExtents2D    exts;
    RbcMarker      *markerPtr;
    int             mode;
    int             left, right, top, bottom;
    int             enclosed;

#define FIND_ENCLOSED	 (1<<0)
#define FIND_OVERLAPPING (1<<1)
    if (strcmp(argv[3], "enclosed") == 0) {
        mode = FIND_ENCLOSED;
    } else if (strcmp(argv[3], "overlapping") == 0) {
        mode = FIND_OVERLAPPING;
    } else {
        Tcl_AppendResult(interp, "bad search type \"", argv[3],
            ": should be \"enclosed\", or \"overlapping\"", (char *) NULL);
        return TCL_ERROR;
    }

    if ((Tcl_GetInt(interp, argv[4], &left) != TCL_OK) ||
        (Tcl_GetInt(interp, argv[5], &top) != TCL_OK) ||
        (Tcl_GetInt(interp, argv[6], &right) != TCL_OK) ||
        (Tcl_GetInt(interp, argv[7], &bottom) != TCL_OK)) {
        return TCL_ERROR;
    }
    if (left < right) {
        exts.left = (double) left;
        exts.right = (double) right;
    } else {
        exts.left = (double) right;
        exts.right = (double) left;
    }
    if (top < bottom) {
        exts.top = (double) top;
        exts.bottom = (double) bottom;
    } else {
        exts.top = (double) bottom;
        exts.bottom = (double) top;
    }
    enclosed = (mode == FIND_ENCLOSED);
    for (linkPtr = RbcChainFirstLink(graphPtr->markers.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        markerPtr = RbcChainGetValue(linkPtr);
        if (markerPtr->hidden) {
            continue;
        }
        if (markerPtr->elemName != NULL) {
            Tcl_HashEntry  *hPtr;

            hPtr = Tcl_FindHashEntry(&graphPtr->elements.table,
                markerPtr->elemName);
            if (hPtr != NULL) {
                RbcElement     *elemPtr;

                elemPtr = (RbcElement *) Tcl_GetHashValue(hPtr);
                if (elemPtr->hidden) {
                    continue;
                }
            }
        }
        if ((*markerPtr->classPtr->regionProc) (markerPtr, &exts, enclosed)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(markerPtr->name, -1));
            return TCL_OK;
        }
    }
    Tcl_ResetResult(interp);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * ExistsOp --
 *
 *      Returns if marker by a given ID currently exists.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
ExistsOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    Tcl_HashEntry  *hPtr;

    hPtr = Tcl_FindHashEntry(&graphPtr->markers.table, argv[3]);
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj((hPtr != NULL)));
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * TypeOp --
 *
 *      Returns a symbolic name for the type of the marker whose ID is
 *      given.
 *
 * Results:
 *      A standard Tcl result. interp->result will contain the symbolic
 *      type of the marker.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int
TypeOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv)
{
    RbcMarker      *markerPtr;

    if (NameToMarker(graphPtr, argv[3], &markerPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(markerPtr->classUid, -1));
    return TCL_OK;
}

/* Public routines */

static RbcOpSpec markerOps[] = {
    {"after", 1, (RbcOp) RelinkOp, 4, 5, "marker ?afterMarker?",},
    {"before", 2, (RbcOp) RelinkOp, 4, 5, "marker ?beforeMarker?",},
    {"bind", 2, (RbcOp) BindOp, 3, 6, "marker sequence command",},
    {"cget", 2, (RbcOp) CgetOp, 5, 5, "marker option",},
    {"configure", 2, (RbcOp) ConfigureOp, 4, 0,
        "marker ?marker?... ?option value?...",},
    {"create", 2, (RbcOp) CreateOp, 4, 0,
        "type ?option value?...",},
    {"delete", 1, (RbcOp) DeleteOp, 3, 0, "?marker?...",},
    {"exists", 1, (RbcOp) ExistsOp, 4, 4, "marker",},
    {"find", 1, (RbcOp) FindOp, 8, 8, "enclosed|overlapping x1 y1 x2 y2",},
    {"get", 1, (RbcOp) GetOp, 4, 4, "name",},
    {"names", 1, (RbcOp) NamesOp, 3, 0, "?pattern?...",},
    {"type", 1, (RbcOp) TypeOp, 4, 4, "marker",},
};

static int      nMarkerOps = sizeof(markerOps) / sizeof(RbcOpSpec);

/*
 * ----------------------------------------------------------------------
 *
 * RbcMarkerOp --
 *
 *      This procedure is invoked to process the Tcl command
 *      that corresponds to a widget managed by this module.
 *      See the user documentation for details on what it does.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 * ----------------------------------------------------------------------
 */
int
RbcMarkerOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,        /* Not used. */
    int argc,
    const char **argv)
{
    RbcOp           proc;
    int             result;

    proc = RbcGetOp(interp, nMarkerOps, markerOps, RBC_OP_ARG2, argc, argv, 0);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    result = (*proc) (graphPtr, interp, argc, argv);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcMarkersToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcMarkersToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken,
    int under)
{
    RbcChainLink   *linkPtr;
    register RbcMarker *markerPtr;

    for (linkPtr = RbcChainFirstLink(graphPtr->markers.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        markerPtr = RbcChainGetValue(linkPtr);
        if ((markerPtr->classPtr->postscriptProc == NULL) ||
            (markerPtr->nWorldPts == 0)) {
            continue;
        }
        if (markerPtr->drawUnder != under) {
            continue;
        }
        if (markerPtr->hidden) {
            continue;
        }
        if (markerPtr->elemName != NULL) {
            Tcl_HashEntry  *hPtr;

            hPtr = Tcl_FindHashEntry(&graphPtr->elements.table,
                markerPtr->elemName);
            if (hPtr != NULL) {
                RbcElement     *elemPtr;

                elemPtr = (RbcElement *) Tcl_GetHashValue(hPtr);
                if (elemPtr->hidden) {
                    continue;
                }
            }
        }
        RbcAppendToPostScript(psToken, "\n% Marker \"", markerPtr->name,
            "\" is a ", markerPtr->classUid, " marker\n", (char *) NULL);
        (*markerPtr->classPtr->postscriptProc) (markerPtr, psToken);
    }
}

/*
 * -------------------------------------------------------------------------
 *
 * RbcDrawMarkers --
 *
 *      Calls the individual drawing routines (based on marker type)
 *      for each marker in the display list.
 *
 *      A marker will not be drawn if
 *
 *      1) An element linked to the marker (indicated by elemName)
 *         is currently hidden.
 *
 *      2) No coordinates have been specified for the marker.
 *
 *      3) The marker is requesting to be drawn at a different level
 *         (above/below the elements) from the current mode.
 *
 *      4) The marker is configured as hidden (-hide option).
 *
 *      5) The marker isn't visible in the current viewport
 *         (i.e. clipped).
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      Markers are drawn into the drawable (pixmap) which will eventually
 *      be displayed in the graph window.
 *
 * -------------------------------------------------------------------------
 */
void
RbcDrawMarkers(
    RbcGraph * graphPtr,
    Drawable drawable,          /* Pixmap or window to draw into */
    int under)
{
    RbcChainLink   *linkPtr;
    RbcMarker      *markerPtr;

    for (linkPtr = RbcChainFirstLink(graphPtr->markers.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        markerPtr = RbcChainGetValue(linkPtr);

        if ((markerPtr->nWorldPts == 0) ||
            (markerPtr->drawUnder != under) ||
            (markerPtr->hidden) || (markerPtr->clipped)) {
            continue;
        }
        if (markerPtr->elemName != NULL) {
            Tcl_HashEntry  *hPtr;

            /* Look up the named element and see if it's hidden */
            hPtr = Tcl_FindHashEntry(&graphPtr->elements.table,
                markerPtr->elemName);
            if (hPtr != NULL) {
                RbcElement     *elemPtr;

                elemPtr = (RbcElement *) Tcl_GetHashValue(hPtr);
                if (elemPtr->hidden) {
                    continue;
                }
            }
        }

        (*markerPtr->classPtr->drawProc) (markerPtr, drawable);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcMapMarkers --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcMapMarkers(
    RbcGraph * graphPtr)
{
    RbcChainLink   *linkPtr;
    RbcMarker      *markerPtr;

    for (linkPtr = RbcChainFirstLink(graphPtr->markers.displayList);
        linkPtr != NULL; linkPtr = RbcChainNextLink(linkPtr)) {
        markerPtr = RbcChainGetValue(linkPtr);
        if ((markerPtr->nWorldPts == 0) || (markerPtr->hidden)) {
            continue;
        }
        if ((graphPtr->flags & RBC_MAP_ALL)
            || (markerPtr->flags & RBC_MAP_ITEM)) {
            (*markerPtr->classPtr->mapProc) (markerPtr);
            markerPtr->flags &= ~RBC_MAP_ITEM;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcDestroyMarkers --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcDestroyMarkers(
    RbcGraph * graphPtr)
{
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  cursor;
    RbcMarker      *markerPtr;

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->markers.table, &cursor);
        hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        markerPtr = (RbcMarker *) Tcl_GetHashValue(hPtr);
        /*
         * Dereferencing the pointer to the hash table prevents the
         * hash table entry from being automatically deleted.
         */
        markerPtr->hashPtr = NULL;
        DestroyMarker(markerPtr);
    }
    Tcl_DeleteHashTable(&graphPtr->markers.table);
    Tcl_DeleteHashTable(&graphPtr->markers.tagTable);
    RbcChainDestroy(graphPtr->markers.displayList);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcNearestMarker --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcMarker      *
RbcNearestMarker(
    RbcGraph * graphPtr,
    int x,                      /* Screen coordinates */
    int y,                      /* Screen coordinates */
    int under)
{
    RbcChainLink   *linkPtr;
    RbcMarker      *markerPtr;
    RbcPoint2D      point;

    point.x = (double) x;
    point.y = (double) y;
    for (linkPtr = RbcChainLastLink(graphPtr->markers.displayList);
        linkPtr != NULL; linkPtr = RbcChainPrevLink(linkPtr)) {
        markerPtr = RbcChainGetValue(linkPtr);
        if ((markerPtr->drawUnder == under) && (markerPtr->nWorldPts > 0) &&
            (!markerPtr->hidden) && (markerPtr->state == RBC_STATE_NORMAL)) {
            if ((*markerPtr->classPtr->pointProc) (markerPtr, &point)) {
                return markerPtr;
            }
        }
    }
    return NULL;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
