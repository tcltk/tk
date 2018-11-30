/*
 * tkPathInt.h --
 *
 *    Header file for the internals of the tkpath package.
 *
 * Copyright (c) 2005-2008  Mats Bengtsson
 *
 */

#ifndef _TKPATHINT_H
#define _TKPATHINT_H

#include "tcl.h"
#include "tclOO.h"

#include "tk.h"
#include "default.h"

#if defined(_WIN32)
#include "tkWinInt.h"
#elif defined(MAC_OSX_TK)
#include "tkMacOSXInt.h"
#else
#include "tkUnixInt.h"
#endif

#define _USE_MATH_DEFINES
#include <math.h> /* VC math constants M_PI, M_SQRT1_2 */
#include <float.h> /* DBL_MAX,..*/
#include <assert.h>

/*
 * For C++ compilers, use extern "C"
 */
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Mathematical functions
 */
#undef ABS
#define ABS(x)        (((x)<0)?(-(x)):(x))

#undef MIN
#define MIN(a,b)    (((a)<(b))?(a):(b))

#undef MAX
#define MAX(a,b)    (((a)>(b))?(a):(b))

#define DEGREES_TO_RADIANS (M_PI/180.0)
#define RADIANS_TO_DEGREES (180.0/M_PI)

/*
 * Tkpath defines start here
 */
#define TKPATH_VERSION    "0.3"
#define TKPATH_PATCHLEVEL "0.3.3"
#define TKPATH_REQUIRE    "8.6.8"

/*
 *  Global variables set in tkpathMain.c
 */
MODULE_SCOPE int Tk_PathAntiAlias;
MODULE_SCOPE int Tk_PathDepixelize;
MODULE_SCOPE int Tk_PathSurfaceCopyPremultiplyAlpha;

/*
 * So far we use a fixed number of straight line segments when
 * doing various things, but it would be better to use the de Castlejau
 * algorithm to iterate these segments.
 */
#define TK_PATH_NUMSEGEMENTS_CurveTo         18
#define TK_PATH_NUMSEGEMENTS_QuadBezier     12
#define TK_PATH_NUMSEGEMENTS_Max        18
#define TK_PATH_NUMSEGEMENTS_Ellipse         48

#define TK_PATH_UNIT_TMATRIX  {1.0, 0.0, 0.0, 1.0, 0.0, 0.0}

/*
 * Flag bits for gradient and style changes.
 */
enum {
    TK_PATH_GRADIENT_FLAG_CONFIGURE    = (1L << 0),
    TK_PATH_GRADIENT_FLAG_DELETE
};

enum {
    TK_PATH_STYLE_FLAG_CONFIGURE        = (1L << 0),
    TK_PATH_STYLE_FLAG_DELETE
};


enum {
    TK_PATH_TEXTANCHOR_Start        = 0L,
    TK_PATH_TEXTANCHOR_Middle,
    TK_PATH_TEXTANCHOR_End,
    TK_PATH_TEXTANCHOR_N,
    TK_PATH_TEXTANCHOR_W,
    TK_PATH_TEXTANCHOR_S,
    TK_PATH_TEXTANCHOR_E,
    TK_PATH_TEXTANCHOR_NW,
    TK_PATH_TEXTANCHOR_NE,
    TK_PATH_TEXTANCHOR_SW,
    TK_PATH_TEXTANCHOR_SE,
    TK_PATH_TEXTANCHOR_C
};

enum {
    TK_PATH_IMAGEINTERPOLATION_None = 0,
    TK_PATH_IMAGEINTERPOLATION_Fast,
    TK_PATH_IMAGEINTERPOLATION_Best
};

/* These MUST be kept in sync with methodST and unitsST! */
enum {
    TK_PATH_GRADIENTMETHOD_Pad        = 0L,
    TK_PATH_GRADIENTMETHOD_Repeat,
    TK_PATH_GRADIENTMETHOD_Reflect
};

enum {
    TK_PATH_GRADIENTUNITS_BoundingBox =    0L,
    TK_PATH_GRADIENTUNITS_UserSpace
};

enum {
    TK_PATH_ARC_OK,
    TK_PATH_ARC_Line,
    TK_PATH_ARC_Skip
};

enum {
    TK_PATH_GRADIENTTYPE_LINEAR =    0L,
    TK_PATH_GRADIENTTYPE_RADIAL
};

/*
 * Flags for 'TkPathStyleMergeStyles'.
 */
enum {
    TK_PATH_MERGESTYLE_NOTFILL =         0L,
    TK_PATH_MERGESTYLE_NOTSTROKE
};

/*
 * The enum below defines the valid types for the TkPathAtom's.
 */
typedef enum {
    TK_PATH_ATOM_M = 'M',
    TK_PATH_ATOM_L = 'L',
    TK_PATH_ATOM_A = 'A',
    TK_PATH_ATOM_Q = 'Q',
    TK_PATH_ATOM_C = 'C',
    TK_PATH_ATOM_Z = 'Z',
    TK_PATH_ATOM_ELLIPSE = '1',    /* These are not a standard atoms
                                 * since they are more complex (molecule).
                                 * Not all features supported for these! */
    TK_PATH_ATOM_RECT = '2'
} TkPathAtomType;

typedef struct TkPathRect {
    double x1;
    double y1;
    double x2;
    double y2;
} TkPathRect;

typedef struct TkPathPoint {
    double x;
    double y;
} TkPathPoint;

/*
 * The transformation matrix:
 *        | a  b  0 |
 *        | c  d  0 |
 *        | tx ty 1 |
 */
typedef struct TkPathMatrix {
    double a, b, c, d;
    double tx, ty;
} TkPathMatrix;

/*
 * Records used for parsing path to a linked list of primitive
 * drawing instructions.
 *
 * PathAtom: vaguely modelled after Tk_PathItem. Each atom has a PathAtom record
 * in its first position, padded with type specific data.
 */
typedef struct TkPathAtom {
    TkPathAtomType type;        /* Type of PathAtom. */
    struct TkPathAtom *nextPtr;    /* Next PathAtom along the path. */
} TkPathAtom;

typedef struct TkLookupTable {
    int from;
    int to;
} TkLookupTable;

/*
 * Records used for parsing path to a linked list of primitive
 * drawing instructions.
 *
 * TkPathAtom: vaguely modelled after Tk_PathItem. Each atom has a TkPathAtom record
 * in its first position, padded with type specific data.
 */

typedef struct TkMoveToAtom {
    TkPathAtom pathAtom;        /* Generic stuff that's the same for all
                                 * types.  MUST BE FIRST IN STRUCTURE. */
    double x;
    double y;
} TkMoveToAtom;

typedef struct TkLineToAtom {
    TkPathAtom pathAtom;
    double x;
    double y;
} TkLineToAtom;

typedef struct TkArcAtom {
    TkPathAtom pathAtom;
    double radX;
    double radY;
    double angle;        /* In degrees! */
    char largeArcFlag;
    char sweepFlag;
    double x;
    double y;
} TkArcAtom;

typedef struct TkQuadBezierAtom {
    TkPathAtom pathAtom;
    double ctrlX;
    double ctrlY;
    double anchorX;
    double anchorY;
} TkQuadBezierAtom;

typedef struct TkCurveToAtom {
    TkPathAtom pathAtom;
    double ctrlX1;
    double ctrlY1;
    double ctrlX2;
    double ctrlY2;
    double anchorX;
    double anchorY;
} TkCurveToAtom;

typedef struct TkCloseAtom {
    TkPathAtom pathAtom;
    double x;
    double y;
} TkCloseAtom;

typedef struct TkEllipseAtom {
    TkPathAtom pathAtom;
    double cx;
    double cy;
    double rx;
    double ry;
} TkEllipseAtom;

typedef struct TkRectAtom {
    TkPathAtom pathAtom;
    double x;
    double y;
    double width;
    double height;
} TkRectAtom;

/*
 * Structures used for Dashing and Outline.
 */
typedef struct TkPathDash {
    int number;
    float *array;
} TkPathDash;

/*
 * Records for gradient fills.
 * We need a separate GradientStopArray to simplify option parsing.
 */

typedef struct TkGradientStop {
    double offset;
    XColor *color;
    double opacity;
} TkGradientStop;

typedef struct TkGradientStopArray {
    int nstops;
    TkGradientStop **stops;    /* Array of pointers to GradientStop. */
} TkGradientStopArray;

typedef struct TkLinearGradientFill {
    TkPathRect *transitionPtr;    /* Actually not a proper rect but a vector. */
    int method;
    int fillRule;        /* Not yet used. */
    int units;
    TkGradientStopArray *stopArrPtr;
} TkLinearGradientFill;

typedef struct TkRadialTransition {
    double centerX;
    double centerY;
    double radius;
    double focalX;
    double focalY;
} TkRadialTransition;

typedef struct TkRadialGradientFill {
    TkRadialTransition *radialPtr;
    int method;
    int fillRule;        /* Not yet used. */
    int units;
    TkGradientStopArray *stopArrPtr;
} TkRadialGradientFill;

/*
 * Tk_PathCanvas_ is just a dummy which is never defined anywhere.
 * This happens to work because Tk_PathCanvas is a pointer.
 * Its reason is to hide the internals of TkPathCanvas to item code.
 */
typedef struct Tk_PathCanvas_ *Tk_PathCanvas;

/*
 * This is the main record for a gradient object.
 */
typedef struct TkPathGradientMaster {
    int type;            /* Any of TK_PATH_GRADIENTTYPE_LINEAR or TK_PATH_GRADIENTTYPE_RADIAL */
    Tk_OptionTable optionTable;
    Tk_Uid name;
    Tcl_Obj *transObj;
    Tcl_Obj *stopsObj;
    TkPathMatrix *matrixPtr;        /*  a  b   default (NULL): 1 0
                                    c  d           0 1
                                    tx ty            0 0 */

    struct TkPathGradientInst *instancePtr;
                /* Pointer to first in list of instances
                 * derived from this gradient name. */
    union {            /* Depending on the 'type'. */
        TkLinearGradientFill linearFill;
        TkRadialGradientFill radialFill;
    };
} TkPathGradientMaster;

typedef void (TkPathGradientChangedProc)(ClientData clientData, int flags);

/*
 * This defines an instance of a gradient with specified name and hash table.
 */
typedef struct TkPathGradientInst {
    struct TkPathGradientMaster *masterPtr;
                /* Each instance also points to the actual
                 * TkPathGradientMaster in order to remove itself
                 * from its linked list. */
    TkPathGradientChangedProc *changeProc;
                /* Code in item to call when gradient changes
                 * in a way that affects redisplay. */
    ClientData clientData;    /* Argument to pass to changeProc. */
    struct TkPathGradientInst *nextPtr;
                /* Next in list of all gradient instances
                 * associated with the same gradient name. */
} TkPathGradientInst;

/*
 * Only one of color and gradientInstPtr must be non NULL!
 */
typedef struct TkPathColor {
    XColor *color;        /* Foreground color for filling. */
    TkPathGradientInst *gradientInstPtr;
                /* This is an instance of a gradient.
                 * It points to the actual gradient object, the master. */
} TkPathColor;


#define TK_PATH_STYLE_OPTION_INDEX_END 17    /* Use this for item specific flags */

typedef struct Tk_PathStyle {
    Tk_OptionTable optionTable;    /* Not used for canvas. */
    Tk_Uid name;        /* Not used for canvas. */
    int mask;            /* Bits set for actual options modified. */
    XColor *strokeColor;    /* Stroke color. */
    double strokeWidth;        /* Width of stroke. */
    double strokeOpacity;
    int offset;            /* Dash offset */
    TkPathDash *dashPtr;    /* Dash pattern. */
    int capStyle;        /* Cap style for stroke. */
    int joinStyle;        /* Join style for stroke. */
    double miterLimit;
    Tcl_Obj *fillObj;        /* This is just used for option parsing. */
    TkPathColor *fill;        /* Record XColor + TkPathGradientInst. */
    double fillOpacity;
    int fillRule;        /* WindingRule or EvenOddRule. */
    TkPathMatrix *matrixPtr;        /*  a  b   default (NULL): 1 0
                    c  d           0 1
                    tx ty            0 0 */
    struct TkPathStyleInst *instancePtr;
                /* Pointer to first in list of instances
                 * derived from this style name. */
} Tk_PathStyle;

typedef void (TkPathStyleChangedProc)(ClientData clientData, int flags);

/*
 * This defines an instance of a style with specified name and hash table.
 */
typedef struct TkPathStyleInst {
    struct Tk_PathStyle *masterPtr;
                /* Each instance also points to the actual
                 * Tk_PathStyle in order to remove itself
                 * from its linked list. */
    TkPathStyleChangedProc *changeProc;
                /* Code in item to call when style changes
                 * in a way that affects redisplay. */
    ClientData clientData;    /* Argument to pass to changeProc. */
    struct TkPathStyleInst *nextPtr;
                /* Next in list of all style instances
                 * associated with the same style name. */
} TkPathStyleInst;

/*
 *--------------------------------------------------------------
 *
 * Procedure prototypes and structures used for defining new canvas items:
 *
 *--------------------------------------------------------------
 */

typedef enum {
    TK_PATHSTATE_NULL = -1, TK_PATHSTATE_ACTIVE, TK_PATHSTATE_DISABLED,
    TK_PATHSTATE_NORMAL, TK_PATHSTATE_HIDDEN
} Tk_PathState;


typedef struct Tk_PathSmoothMethod {
    const char *name;
    int (*coordProc)(Tk_PathCanvas canvas,
        double *pointPtr, int numPoints, int numSteps,
        XPoint xPoints[], double dblPoints[]);
    void (*postscriptProc)(Tcl_Interp *interp,
        Tk_PathCanvas canvas, double *coordPtr,
        int numPoints, int numSteps);
} Tk_PathSmoothMethod;

/*
 * For each item in a canvas widget there exists one record with the following
 * structure. Each actual item is represented by a record with the following
 * stuff at its beginning, plus additional type-specific stuff after that.
 */

#define TK_PATHTAG_SPACE 3

typedef struct Tk_PathTags {
    Tk_Uid *tagPtr;        /* Pointer to array of tags. */
    int tagSpace;        /* Total amount of tag space available at
                 * tagPtr. */
    int numTags;        /* Number of tag slots actually used at
                 * *tagPtr. */
} Tk_PathTags;

typedef struct Tk_PathItem {
    int id;            /* Unique identifier for this item (also
                 * serves as first tag for item). */
    Tk_OptionTable optionTable;    /* Option table */
    struct Tk_PathItem *nextPtr;/* Next sibling in display list of this group.
                 * Later items in list are drawn on
                 * top of earlier ones. */
    struct Tk_PathItem *prevPtr;/* Previous sibling in display list of this group. */
    struct Tk_PathItem *parentPtr;
                /* Parent of item or NULL if root. */
    struct Tk_PathItem *firstChildPtr;
                /* First child item, only for groups. */
    struct Tk_PathItem *lastChildPtr;
                /* Last child item, only for groups. */
    Tcl_Obj *parentObj;        /*   */
    Tk_PathTags *pathTagsPtr;    /* Allocated struct for storing tags.
                 * This is needed by the custom option handling. */

    struct Tk_PathItemType *typePtr;/* Table of procedures that implement this
                 * type of item. */
    int x1, y1, x2, y2;        /* Bounding box for item, in integer canvas
                 * units. Set by item-specific code and
                 * guaranteed to contain every pixel drawn in
                 * item. Item area includes x1 and y1 but not
                 * x2 and y2. */
    Tk_PathState state;        /* State of item. */
    TkPathRect bbox;            /* Bounding box with zero width outline.
                                 * Untransformed coordinates. */
    TkPathRect totalBbox;            /* Bounding box including stroke.
                                 * Untransformed coordinates. */
    char *reserved1;        /* reserved for future use */
    int redraw_flags;        /* Some flags used in the canvas */

    /*
     *------------------------------------------------------------------
     * Starting here is additional type-specific stuff; see the declarations
     * for individual types to see what is part of each type. The actual space
     * below is determined by the "itemInfoSize" of the type's Tk_PathItemType
     * record.
     *------------------------------------------------------------------
     */
} Tk_PathItem;

/*
 * Records of the following type are used to describe a type of item (e.g.
 * lines, circles, etc.) that can form part of a canvas widget.
 */

typedef int    Tk_PathItemCreateProc(Tcl_Interp *interp,
            Tk_PathCanvas canvas, Tk_PathItem *itemPtr, int objc,
            Tcl_Obj *const objv[]);
typedef int    Tk_PathItemConfigureProc(Tcl_Interp *interp,
            Tk_PathCanvas canvas, Tk_PathItem *itemPtr, int objc,
            Tcl_Obj *const objv[], int flags);
typedef int    Tk_PathItemCoordProc(Tcl_Interp *interp,
            Tk_PathCanvas canvas, Tk_PathItem *itemPtr, int objc,
            Tcl_Obj *const objv[]);
typedef void    Tk_PathItemDeleteProc(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, Display *display);
typedef void    Tk_PathItemDisplayProc(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, Display *display, Drawable dst,
            int x, int y, int width, int height);
typedef void    TkPathItemBboxProc(Tk_PathCanvas canvas, Tk_PathItem *itemPtr,
            int mask);
typedef double    Tk_PathItemPointProc(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, double *pointPtr);
typedef int    Tk_PathItemAreaProc(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, double *rectPtr);
typedef int    Tk_PathItemPdfProc(Tcl_Interp *interp,
            Tk_PathCanvas canvas, Tk_PathItem *itemPtr, int objc,
            Tcl_Obj *const objv[], int prepass);
typedef void    Tk_PathItemScaleProc(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, int compensate,
            double originX, double originY,
            double scaleX, double scaleY);
typedef void    Tk_PathItemTranslateProc(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, int compensate,
            double deltaX, double deltaY);
typedef int    Tk_PathItemIndexProc(Tcl_Interp *interp,
            Tk_PathCanvas canvas, Tk_PathItem *itemPtr, char *indexString,
            int *indexPtr);
typedef void    Tk_PathItemCursorProc(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, int index);
typedef int    Tk_PathItemSelectionProc(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, int offset, char *buffer,
            int maxBytes);
typedef void    Tk_PathItemInsertProc(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, int beforeThis, char *string);
typedef void    Tk_PathItemDCharsProc(Tk_PathCanvas canvas,
            Tk_PathItem *itemPtr, int first, int last);

#ifndef __NO_OLD_CONFIG

typedef struct Tk_PathItemType {
    const char *name;        /* The name of this type of item, such as
                 * "line". */
    int itemSize;        /* Total amount of space needed for item's
                 * record. */
    Tk_PathItemCreateProc *createProc;
                /* Procedure to create a new item of this
                 * type. */
    Tk_OptionSpec *optionSpecs;    /* Pointer to array of option specs for
                 * this type. Used for returning option
                 * info. */
    Tk_PathItemConfigureProc *configProc;
                /* Procedure to call to change configuration
                 * options. */
    Tk_PathItemCoordProc *coordProc;/* Procedure to call to get and set the item's
                 * coordinates. */
    Tk_PathItemDeleteProc *deleteProc;
                /* Procedure to delete existing item of this
                 * type. */
    Tk_PathItemDisplayProc *displayProc;
                /* Procedure to display items of this type. */
    int alwaysRedraw;        /* Non-zero means displayProc should be called
                 * even when the item has been moved
                 * off-screen. */
    TkPathItemBboxProc *bboxProc;
                /* Procedure that is invoked by group items
                 * on its children when it has reconfigured in
                 * any way that affect the childrens bbox display. */
    Tk_PathItemPointProc *pointProc;
                /* Computes distance from item to a given
                 * point. */
    Tk_PathItemAreaProc *areaProc;
                /* Computes whether item is inside, outside,
                 * or overlapping an area. */
    Tk_PathItemPdfProc *pdfProc;
                /* Procedure to write a Pdf description
                 * for items of this type. */
    Tk_PathItemScaleProc *scaleProc;/* Procedure to rescale items of this type. */
    Tk_PathItemTranslateProc *translateProc;
                /* Procedure to translate items of this
                 * type. */
    Tk_PathItemIndexProc *indexProc;/* Procedure to determine index of indicated
                 * character. NULL if item doesn't support
                 * indexing. */
    Tk_PathItemCursorProc *icursorProc;
                /* Procedure to set insert cursor posn to just
                 * before a given position. */
    Tk_PathItemSelectionProc *selectionProc;
                /* Procedure to return selection (in STRING
                 * format) when it is in this item. */
    Tk_PathItemInsertProc *insertProc;
                /* Procedure to insert something into an
                 * item. */
    Tk_PathItemDCharsProc *dCharsProc;
                /* Procedure to delete characters from an
                 * item. */
    struct Tk_PathItemType *nextPtr;/* Used to link types together into a list. */
    int isPathType;        /* False for original canvas item types. */
} Tk_PathItemType;

#endif

/*
 * The following structure provides information about the selection and the
 * insertion cursor. It is needed by only a few items, such as those that
 * display text. It is shared by the generic canvas code and the item-specific
 * code, but most of the fields should be written only by the canvas generic
 * code.
 */

typedef struct Tk_PathCanvasTextInfo {
    Tk_3DBorder selBorder;    /* Border and background for selected
                 * characters. Read-only to items.*/
    int selBorderWidth;        /* Width of border around selection. Read-only
                 * to items. */
    XColor *selFgColorPtr;    /* Foreground color for selected text.
                 * Read-only to items. */
    Tk_PathItem *selItemPtr;    /* Pointer to selected item. NULL means
                 * selection isn't in this canvas. Writable by
                 * items. */
    int selectFirst;        /* Character index of first selected
                 * character. Writable by items. */
    int selectLast;        /* Character index of last selected character.
                 * Writable by items. */
    Tk_PathItem *anchorItemPtr;    /* Item corresponding to "selectAnchor": not
                 * necessarily selItemPtr. Read-only to
                 * items. */
    int selectAnchor;        /* Character index of fixed end of selection
                 * (i.e. "select to" operation will use this
                 * as one end of the selection). Writable by
                 * items. */
    Tk_3DBorder insertBorder;    /* Used to draw vertical bar for insertion
                 * cursor. Read-only to items. */
    int insertWidth;        /* Total width of insertion cursor. Read-only
                 * to items. */
    int insertBorderWidth;    /* Width of 3-D border around insert cursor.
                 * Read-only to items. */
    Tk_PathItem *focusItemPtr;    /* Item that currently has the input focus, or
                 * NULL if no such item. Read-only to items. */
    int gotFocus;        /* Non-zero means that the canvas widget has
                 * the input focus. Read-only to items.*/
    int cursorOn;        /* Non-zero means that an insertion cursor
                 * should be displayed in focusItemPtr.
                 * Read-only to items.*/
} Tk_PathCanvasTextInfo;

typedef struct Tk_PathOutline {
    GC gc;            /* Graphics context. */
    double width;        /* Width of outline. */
    double activeWidth;        /* Width of outline. */
    double disabledWidth;    /* Width of outline. */
    int offset;            /* Dash offset. */
    Tk_Dash *dashPtr;        /* Dash pattern. */
    Tk_Dash *activeDashPtr;    /* Dash pattern if state is active. */
    Tk_Dash *disabledDashPtr;    /* Dash pattern if state is disabled. */

    VOID *reserved1;        /* Reserved for future expansion. */
    VOID *reserved2;
    VOID *reserved3;
    Tk_TSOffset *tsoffsetPtr;    /* Stipple offset for outline. */
    XColor *color;        /* Outline color. */
    XColor *activeColor;    /* Outline color if state is active. */
    XColor *disabledColor;    /* Outline color if state is disabled. */
    Pixmap stipple;        /* Outline Stipple pattern. */
    Pixmap activeStipple;    /* Outline Stipple pattern if state is
                 * active. */
    Pixmap disabledStipple;    /* Outline Stipple pattern if state is
                 * disabled. */
} Tk_PathOutline;


#define TK_PATH_OPTION_SPEC_ARROWLENGTH_DEFAULT  "10.0"
#define TK_PATH_OPTION_SPEC_ARROWWIDTH_DEFAULT    "5.0"
#define TK_PATH_OPTION_SPEC_ARROWFILL_DEFAULT     "0.7"

#define TK_PATH_OPTION_SPEC_STARTARROW(Item)                \
    {TK_OPTION_BOOLEAN, "-startarrow", NULL, NULL,            \
    "0", -1, Tk_Offset(Item, startarrow.arrowEnabled),        \
    0, 0, 0}

#define TK_PATH_OPTION_SPEC_STARTARROWLENGTH(Item)                \
    {TK_OPTION_DOUBLE, "-startarrowlength", NULL, NULL,            \
    TK_PATH_OPTION_SPEC_ARROWLENGTH_DEFAULT, -1,            \
    Tk_Offset(Item, startarrow.arrowLength),            \
    0, 0, 0}

#define TK_PATH_OPTION_SPEC_STARTARROWWIDTH(Item)                \
    {TK_OPTION_DOUBLE, "-startarrowwidth", NULL, NULL,            \
    TK_PATH_OPTION_SPEC_ARROWWIDTH_DEFAULT, -1,            \
    Tk_Offset(Item, startarrow.arrowWidth),                \
    0, 0, 0}

#define TK_PATH_OPTION_SPEC_STARTARROWFILL(Item)                \
    {TK_OPTION_DOUBLE, "-startarrowfill", NULL, NULL,            \
    TK_PATH_OPTION_SPEC_ARROWFILL_DEFAULT, -1,                \
    Tk_Offset(Item, startarrow.arrowFillRatio),            \
    0, 0, 0}

#define TK_PATH_OPTION_SPEC_ENDARROW(Item)                    \
    {TK_OPTION_BOOLEAN, "-endarrow", NULL, NULL,            \
    "0", -1, Tk_Offset(Item, endarrow.arrowEnabled),        \
    0, 0, 0}

#define TK_PATH_OPTION_SPEC_ENDARROWLENGTH(Item)                \
    {TK_OPTION_DOUBLE, "-endarrowlength", NULL, NULL,            \
    TK_PATH_OPTION_SPEC_ARROWLENGTH_DEFAULT, -1,            \
    Tk_Offset(Item, endarrow.arrowLength),                \
    0, 0, 0}

#define TK_PATH_OPTION_SPEC_ENDARROWWIDTH(Item)                \
    {TK_OPTION_DOUBLE, "-endarrowwidth", NULL, NULL,            \
    TK_PATH_OPTION_SPEC_ARROWWIDTH_DEFAULT, -1,            \
    Tk_Offset(Item, endarrow.arrowWidth),                \
    0, 0, 0}

#define TK_PATH_OPTION_SPEC_ENDARROWFILL(Item)                \
    {TK_OPTION_DOUBLE, "-endarrowfill", NULL, NULL,            \
    TK_PATH_OPTION_SPEC_ARROWFILL_DEFAULT, -1,                \
    Tk_Offset(Item, endarrow.arrowFillRatio),            \
    0, 0, 0}

#define TK_PATH_OPTION_SPEC_STARTARROW_GRP(Item)        \
    TK_PATH_OPTION_SPEC_STARTARROW(Item),        \
    TK_PATH_OPTION_SPEC_STARTARROWLENGTH(Item),    \
    TK_PATH_OPTION_SPEC_STARTARROWWIDTH(Item),        \
    TK_PATH_OPTION_SPEC_STARTARROWFILL(Item)

#define TK_PATH_OPTION_SPEC_ENDARROW_GRP(Item)        \
    TK_PATH_OPTION_SPEC_ENDARROW(Item),        \
    TK_PATH_OPTION_SPEC_ENDARROWLENGTH(Item),        \
    TK_PATH_OPTION_SPEC_ENDARROWWIDTH(Item),        \
    TK_PATH_OPTION_SPEC_ENDARROWFILL(Item)


typedef struct TkPathTagSearchExpr_s TkPathTagSearchExpr;

struct TkPathTagSearchExpr_s {
    TkPathTagSearchExpr *next;    /* For linked lists of expressions - used in
                 * bindings. */
    Tk_Uid uid;            /* The uid of the whole expression. */
    Tk_Uid *uids;        /* Expresion compiled to an array of uids. */
    int allocated;        /* Available space for array of uids. */
    int length;            /* Length of expression. */
    int index;            /* Current position in expression
                 * evaluation. */
    int match;            /* This expression matches event's item's
                 * tags. */
};

/*
 * Opaque platform dependent struct.
 */

typedef ClientData TkPathContext;

/*
 * The record below describes a canvas widget. It is made available to the
 * item functions so they can access certain shared fields such as the overall
 * displacement and scale factor for the canvas.
 */

typedef struct TkPathCanvas {
    Tk_Window tkwin;        /* Window that embodies the canvas. NULL means
                 * that the window has been destroyed but the
                 * data structures haven't yet been cleaned
                 * up.*/
    Display *display;        /* Display containing widget; needed, among
                 * other things, to release resources after
                 * tkwin has already gone away. */
    Tcl_Interp *interp;        /* Interpreter associated with canvas. */
    Tcl_Command widgetCmd;    /* Token for canvas's widget command. */
    Tk_OptionTable optionTable;    /* Table that defines configuration options
                 * available for this widget. */
    Tk_PathItem *rootItemPtr;    /* The root item with id 0, always there. */

    /*
     * Information used when displaying widget:
     */

    Tcl_Obj *borderWidthPtr;    /* Value of -borderWidth option: specifies
                 * width of border in pixels. */
    int borderWidth;        /* Width of 3-D border around window. *
                 * Integer value corresponding to
                 * borderWidthPtr. Always >= 0. */
    Tk_3DBorder bgBorder;    /* Used for canvas background. */
    int relief;            /* Indicates whether window as a whole is
                 * raised, sunken, or flat. */
    Tcl_Obj *highlightWidthPtr;    /* Value of -highlightthickness option:
                 * specifies width in pixels of highlight to
                 * draw around widget when it has the focus.
                 * <= 0 means don't draw a highlight. */
    int highlightWidth;        /* Integer value corresponding to
                 * highlightWidthPtr. Always >= 0. */
    XColor *highlightBgColorPtr;
                /* Color for drawing traversal highlight area
                 * when highlight is off. */
    XColor *highlightColorPtr;    /* Color for drawing traversal highlight. */
    int inset;            /* Total width of all borders, including
                 * traversal highlight and 3-D border.
                 * Indicates how much interior stuff must be
                 * offset from outside edges to leave room for
                 * borders. */
    GC pixmapGC;        /* Used to copy bits from a pixmap to the
                 * screen and also to clear the pixmap. */
    int width, height;        /* Dimensions to request for canvas window,
                 * specified in pixels. */
    int redrawX1, redrawY1;    /* Upper left corner of area to redraw, in
                 * pixel coordinates. Border pixels are
                 * included. Only valid if REDRAW_PENDING flag
                 * is set. */
    int redrawX2, redrawY2;    /* Lower right corner of area to redraw, in
                 * integer canvas coordinates. Border pixels
                 * will *not* be redrawn. */
    int confine;        /* Non-zero means constrain view to keep as
                 * much of canvas visible as possible. */

    /*
     * Information used to manage the selection and insertion cursor:
     */

    Tk_PathCanvasTextInfo textInfo; /* Contains lots of fields; see tk.h for
                 * details. This structure is shared with the
                 * code that implements individual items. */
    int insertOnTime;        /* Number of milliseconds cursor should spend
                 * in "on" state for each blink. */
    int insertOffTime;        /* Number of milliseconds cursor should spend
                 * in "off" state for each blink. */
    Tcl_TimerToken insertBlinkHandler;
                /* Timer handler used to blink cursor on and
                 * off. */

    /*
     * Transformation applied to canvas as a whole: to compute screen
     * coordinates (X,Y) from canvas coordinates (x,y), do the following:
     *
     * X = x - xOrigin;
     * Y = y - yOrigin;
     */

    int xOrigin, yOrigin;    /* Canvas coordinates corresponding to
                 * upper-left corner of window, given in
                 * canvas pixel units. */
    int drawableXOrigin, drawableYOrigin;
                /* During redisplay, these fields give the
                 * canvas coordinates corresponding to the
                 * upper-left corner of the drawable where
                 * items are actually being drawn (typically a
                 * pixmap smaller than the whole window). */

    /*
     * Information used for event bindings associated with items.
     */

    Tk_BindingTable bindingTable;
                /* Table of all bindings currently defined for
                 * this canvas. NULL means that no bindings
                 * exist, so the table hasn't been created.
                 * Each "object" used for this table is either
                 * a Tk_Uid for a tag or the address of an
                 * item named by id. */
    Tk_PathItem *currentItemPtr;    /* The item currently containing the mouse
                 * pointer, or NULL if none. */
    Tk_PathItem *newCurrentPtr;    /* The item that is about to become the
                 * current one, or NULL. This field is used to
                 * detect deletions of the new current item
                 * pointer that occur during Leave processing
                 * of the previous current item. */
    double closeEnough;        /* The mouse is assumed to be inside an item
                 * if it is this close to it. */
    XEvent pickEvent;        /* The event upon which the current choice of
                 * currentItem is based. Must be saved so that
                 * if the currentItem is deleted, can pick
                 * another. */
    int state;            /* Last known modifier state. Used to defer
                 * picking a new current object while buttons
                 * are down. */

    /*
     * Information used for managing scrollbars:
     */

    char *xScrollCmd;        /* Command prefix for communicating with
                 * horizontal scrollbar. NULL means no
                 * horizontal scrollbar. Malloc'ed. */
    char *yScrollCmd;        /* Command prefix for communicating with
                 * vertical scrollbar. NULL means no vertical
                 * scrollbar. Malloc'ed. */
    int scrollX1, scrollY1, scrollX2, scrollY2;
                /* These four coordinates define the region
                 * that is the 100% area for scrolling (i.e.
                 * these numbers determine the size and
                 * location of the sliders on scrollbars).
                 * Units are pixels in canvas coords. */
    char *regionString;        /* The option string from which scrollX1 etc.
                 * are derived. Malloc'ed. */
    int xScrollIncrement;    /* If >0, defines a grid for horizontal
                 * scrolling. This is the size of the "unit",
                 * and the left edge of the screen will always
                 * lie on an even unit boundary. */
    int yScrollIncrement;    /* If >0, defines a grid for horizontal
                 * scrolling. This is the size of the "unit",
                 * and the left edge of the screen will always
                 * lie on an even unit boundary. */

    /*
     * Information used for scanning:
     */

    int scanX;            /* X-position at which scan started (e.g.
                 * button was pressed here). */
    int scanXOrigin;        /* Value of xOrigin field when scan started. */
    int scanY;            /* Y-position at which scan started (e.g.
                 * button was pressed here). */
    int scanYOrigin;        /* Value of yOrigin field when scan started. */

    /*
     * Information used to speed up searches by remembering the last item
     * created or found with an item id search.
     */

    Tk_PathItem *hotPtr;    /* Pointer to "hot" item (one that's been
                 * recently used. NULL means there's no hot
                 * item. */
    Tk_PathItem *hotPrevPtr;    /* Pointer to predecessor to hotPtr (NULL
                 * means item is first in list). This is only
                 * a hint and may not really be hotPtr's
                 * predecessor. */

    /*
     * Miscellaneous information:
     */

    Tk_Cursor cursor;        /* Current cursor for window, or None. */
    char *takeFocus;        /* Value of -takefocus option; not used in the
                 * C code, but used by keyboard traversal
                 * scripts. Malloc'ed, but may be NULL. */
    double pixelsPerMM;        /* Scale factor between MM and pixels; used
                 * when converting coordinates. */
    int flags;            /* Various flags; see below for
                 * definitions. */
    int nextId;            /* Number to use as id for next item created
                 * in widget. */
    Tcl_HashTable idTable;    /* Table of integer indices. */
/* @@@ TODO: as pointers instead??? */
    Tcl_HashTable styleTable;    /* Table for styles.
                 * This defines the namespace for style names. */
    Tcl_HashTable gradientTable;/* Table for gradients.
                 * This defines the namespace for gradient names. */
    int styleUid;        /* Running integer used to number style tokens. */
    int gradientUid;        /* Running integer used to number gradient tokens. */
    int tagStyle;

    Tk_PathState canvas_state;    /* State of canvas. */
    TkPathContext context;    /* Path context allocated during a redraw. */
    Tk_TSOffset *tsoffsetPtr;
    TkPathTagSearchExpr *bindTagExprs;/* Linked list of tag expressions used in
                 * bindings. */
} TkPathCanvas;

/*
 * This is an extended item record that is used for the new
 * path based items to allow more generic code to be used for them
 * since all of them (?) anyhow include a Tk_PathStyle record.
 */

typedef struct Tk_PathItemEx  {
    Tk_PathItem header;        /* Generic stuff that's the same for all
                             * types.  MUST BE FIRST IN STRUCTURE. */
    Tk_PathCanvas canvas;   /* Canvas containing item. */
    Tk_PathStyle style;        /* Contains most drawing info. */
    Tcl_Obj *styleObj;        /* Object with style name. */
    TkPathStyleInst *styleInst;
                /* The referenced style instance from styleObj. */

    /*
     *------------------------------------------------------------------
     * Starting here is additional type-specific stuff; see the declarations
     * for individual types to see what is part of each type. The actual space
     * below is determined by the "itemInfoSize" of the type's Tk_PathItemType
     * record.
     *------------------------------------------------------------------
     */
} Tk_PathItemEx;


/*
 * Retrieve TkPathContext from Tk_PathCanvas.
 */

#define ContextOfCanvas(canvas) (((TkPathCanvas *) (canvas))->context)


/*
 * New API option parsing.
 */

#define TK_PATH_DEF_STATE "normal"

/* These MUST be kept in sync with Tk_PathState! */

#define TK_PATH_OPTION_STRING_TABLES_STATE                    \
    static const char *stateStrings[] = {                \
    "active", "disabled", "normal", "hidden", NULL            \
    };

#define TK_PATH_CUSTOM_OPTION_TAGS                        \
    static Tk_ObjCustomOption tagsCO = {                \
        "tags",                                \
        Tk_PathCanvasTagsOptionSetProc,                    \
        Tk_PathCanvasTagsOptionGetProc,                    \
        Tk_PathCanvasTagsOptionRestoreProc,                \
        Tk_PathCanvasTagsOptionFreeProc,                \
        (ClientData) NULL                        \
    };

#define TK_PATH_OPTION_SPEC_PARENT                        \
    {TK_OPTION_STRING, "-parent", NULL, NULL,                \
        "0", Tk_Offset(Tk_PathItem, parentObj), -1,            \
    0, 0, TK_PATH_CORE_OPTION_PARENT}

#define TK_PATH_OPTION_SPEC_CORE(typeName)                    \
    {TK_OPTION_STRING_TABLE, "-state", NULL, NULL,            \
        TK_PATH_DEF_STATE, -1, Tk_Offset(Tk_PathItem, state),        \
        0, (ClientData) stateStrings, 0},                \
    {TK_OPTION_STRING, "-style", (char *) NULL, (char *) NULL,        \
    "", Tk_Offset(typeName, styleObj), -1,                \
    TK_OPTION_NULL_OK, 0, TK_PATH_CORE_OPTION_STYLENAME},        \
    {TK_OPTION_CUSTOM, "-tags", NULL, NULL,                \
    NULL, -1, Tk_Offset(Tk_PathItem, pathTagsPtr),            \
    TK_OPTION_NULL_OK, (ClientData) &tagsCO, TK_PATH_CORE_OPTION_TAGS}


/*
 * Information used for parsing configuration options.
 * Mask bits for options changed.
 */

enum {
    TK_PATH_STYLE_OPTION_FILL            = (1L << 0),
    TK_PATH_STYLE_OPTION_FILL_OFFSET        = (1L << 1),
    TK_PATH_STYLE_OPTION_FILL_OPACITY        = (1L << 2),
    TK_PATH_STYLE_OPTION_FILL_RULE            = (1L << 3),
    TK_PATH_STYLE_OPTION_FILL_STIPPLE        = (1L << 4),
    TK_PATH_STYLE_OPTION_MATRIX            = (1L << 5),
    TK_PATH_STYLE_OPTION_STROKE            = (1L << 6),
    TK_PATH_STYLE_OPTION_STROKE_DASHARRAY        = (1L << 7),
    TK_PATH_STYLE_OPTION_STROKE_LINECAP        = (1L << 8),
    TK_PATH_STYLE_OPTION_STROKE_LINEJOIN       = (1L << 9),
    TK_PATH_STYLE_OPTION_STROKE_MITERLIMIT     = (1L << 10),
    TK_PATH_STYLE_OPTION_STROKE_OFFSET        = (1L << 11),
    TK_PATH_STYLE_OPTION_STROKE_OPACITY        = (1L << 12),
    TK_PATH_STYLE_OPTION_STROKE_STIPPLE        = (1L << 13),
    TK_PATH_STYLE_OPTION_STROKE_WIDTH        = (1L << 14),
    TK_PATH_CORE_OPTION_PARENT            = (1L << 15),
    TK_PATH_CORE_OPTION_STYLENAME            = (1L << 16),
    TK_PATH_CORE_OPTION_TAGS            = (1L << 17),
};
/* @@@ TODO: Much more to be added here! */

enum TkFontWeight {
    TK_PATH_TEXT_WEIGHT_NORMAL,
    TK_PATH_TEXT_WEIGHT_BOLD
};

enum TkFontSlant {
    TK_PATH_TEXT_SLANT_NORMAL,
    TK_PATH_TEXT_SLANT_ITALIC,
    TK_PATH_TEXT_SLANT_OBLIQUE
};

typedef struct Tk_PathTextStyle {
    char *fontFamily;
    double fontSize;
    enum TkFontWeight fontWeight;
    enum TkFontSlant fontSlant;
} Tk_PathTextStyle;


#define TK_PATH_STYLE_CUSTOM_OPTION_MATRIX                \
    static Tk_ObjCustomOption matrixCO = {            \
        "matrix",                        \
        TkPathMatrixSetOption,                    \
        TkPathMatrixGetOption,                    \
        TkPathMatrixRestoreOption,                    \
        TkPathMatrixFreeOption,                    \
        (ClientData) NULL                    \
    };

#define TK_PATH_STYLE_CUSTOM_OPTION_DASH                \
    static Tk_ObjCustomOption dashCO = {            \
        "dasharray",                        \
        Tk_PathDashOptionSetProc,                \
        Tk_PathDashOptionGetProc,                \
        Tk_PathDashOptionRestoreProc,                \
        Tk_PathDashOptionFreeProc,                \
        (ClientData) NULL                    \
    };

#define TK_PATH_STYLE_CUSTOM_OPTION_PATHCOLOR            \
    static Tk_ObjCustomOption pathColorCO = {            \
        "pathcolor",                        \
        TkPathColorSetOption,                    \
        TkPathColorGetOption,                    \
        TkPathColorRestoreOption,                    \
        TkPathColorFreeOption,                    \
        (ClientData) NULL                    \
    };

#define TK_PATH_STYLE_CUSTOM_OPTION_RECORDS            \
    TK_PATH_STYLE_CUSTOM_OPTION_MATRIX                 \
    TK_PATH_STYLE_CUSTOM_OPTION_DASH


/*
 * These must be kept in sync with defines in X.h!
 */

#define TK_PATH_OPTION_STRING_TABLES_FILL                \
    static const char *fillRuleST[] = {                \
    "evenodd", "nonzero", (char *) NULL            \
    };

#define TK_PATH_OPTION_STRING_TABLES_STROKE            \
    static const char *lineCapST[] = {                \
    "notlast", "butt", "round", "projecting", (char *) NULL    \
    };                                \
    static const char *lineJoinST[] = {                \
    "miter", "round", "bevel", (char *) NULL        \
    };


#define TK_PATH_OPTION_SPEC_STYLENAME(typeName)                \
    {TK_OPTION_STRING, "-style", NULL, NULL,                \
        "", Tk_Offset(typeName, styleObj), -1, TK_OPTION_NULL_OK, 0, 0}

/*
 * This assumes that we have a Tk_PathStyle struct element named 'style'.
 */

#define TK_PATH_OPTION_SPEC_STYLE_FILL(typeName, theColor)            \
    {TK_OPTION_STRING, "-fill", NULL, NULL,                \
    theColor, Tk_Offset(typeName, style.fillObj), -1,        \
    TK_OPTION_NULL_OK, 0, TK_PATH_STYLE_OPTION_FILL},            \
    {TK_OPTION_DOUBLE, "-fillopacity", NULL, NULL,            \
        "1.0", -1, Tk_Offset(typeName, style.fillOpacity), 0, 0,        \
        TK_PATH_STYLE_OPTION_FILL_OPACITY},                                \
    {TK_OPTION_STRING_TABLE, "-fillrule", NULL, NULL,            \
        "nonzero", -1, Tk_Offset(typeName, style.fillRule),             \
        0, (ClientData) fillRuleST, TK_PATH_STYLE_OPTION_FILL_RULE}

#define TK_PATH_OPTION_SPEC_STYLE_MATRIX(typeName)                         \
    {TK_OPTION_CUSTOM, "-matrix", NULL, NULL,                \
    NULL, -1, Tk_Offset(typeName, style.matrixPtr),            \
    TK_OPTION_NULL_OK, (ClientData) &matrixCO, TK_PATH_STYLE_OPTION_MATRIX}

#define TK_PATH_OPTION_SPEC_STYLE_STROKE(typeName, theColor)        \
    {TK_OPTION_COLOR, "-stroke", NULL, NULL,                \
        theColor, -1, Tk_Offset(typeName, style.strokeColor),        \
        TK_OPTION_NULL_OK, 0, TK_PATH_STYLE_OPTION_STROKE},        \
    {TK_OPTION_CUSTOM, "-strokedasharray", NULL, NULL,            \
    NULL, -1, Tk_Offset(typeName, style.dashPtr),            \
    0, (ClientData) &dashCO,                    \
        TK_PATH_STYLE_OPTION_STROKE_DASHARRAY},                \
    {TK_OPTION_STRING_TABLE, "-strokelinecap", NULL, NULL,        \
        "butt", -1, Tk_Offset(typeName, style.capStyle),        \
        0, (ClientData) lineCapST, TK_PATH_STYLE_OPTION_STROKE_LINECAP},    \
    {TK_OPTION_STRING_TABLE, "-strokelinejoin", NULL, NULL,        \
        "round", -1, Tk_Offset(typeName, style.joinStyle),        \
        0, (ClientData) lineJoinST, TK_PATH_STYLE_OPTION_STROKE_LINEJOIN}, \
    {TK_OPTION_DOUBLE, "-strokemiterlimit", NULL, NULL,            \
        "4.0", -1, Tk_Offset(typeName, style.miterLimit), 0, 0,        \
        TK_PATH_STYLE_OPTION_STROKE_MITERLIMIT},                           \
    {TK_OPTION_DOUBLE, "-strokeopacity", NULL, NULL,            \
        "1.0", -1, Tk_Offset(typeName, style.strokeOpacity), 0, 0,    \
        TK_PATH_STYLE_OPTION_STROKE_OPACITY},                \
    {TK_OPTION_DOUBLE, "-strokewidth", NULL, NULL,            \
        "1.0", -1, Tk_Offset(typeName, style.strokeWidth), 0, 0,        \
        TK_PATH_STYLE_OPTION_STROKE_WIDTH}

#define TK_PATH_OPTION_SPEC_END                        \
    {TK_OPTION_END, NULL, NULL, NULL,                \
        NULL, 0, -1, 0, (ClientData) NULL, 0}

/* for arrows: */

/* ARROW_BOTH = ARROWS_FIRST | ARROWS_LAST */
typedef enum {
    TK_PATH_ARROWS_OFF = 0, TK_PATH_ARROWS_ON = 1
} TkPathArrowState;


typedef struct TkPathArrowDescr
{
    TkPathArrowState arrowEnabled;    /* Indicates whether or not to draw arrowheads: off/on */
    double arrowLength;         /* Length of arrowhead. */
    double arrowWidth;          /* width of arrowhead. */
    double arrowFillRatio;      /* filled part of arrow head, relative to arrowLengthRel.
                                 * 0: special case, arrowhead only 2 line, without fill */
    TkPathPoint *arrowPointsPtr;  /* Points to array of PTS_IN_ARROW points
                                 * describing polygon for arrowhead in line.
                                 * NULL means no arrowhead at current point. */
} TkPathArrowDescr;



/*
 * Inline function declarations:
 */

/*
 * If stroke width is an integer, widthCode=1,2, move coordinate
 * to pixel boundary if even stroke width, widthCode=2,
 * or to pixel center if odd stroke width, widthCode=1.
 */
#define TK_PATH_DEPIXELIZE(widthCode,x) \
    (!(widthCode) ? (x) : ((int) (floor((x) + 0.001)) + (((widthCode) == 1) ? 0.5 : 0)));

#define GetColorFromPathColor(pcol)\
    (((pcol != NULL) && (pcol->color != NULL)) ? pcol->color : NULL )
#define GetGradientMasterFromPathColor(pcol) \
    (((pcol != NULL) && (pcol->gradientInstPtr != NULL)) ? pcol->gradientInstPtr->masterPtr : NULL )
#define HaveAnyFillFromPathColor(pcol) \
    (((pcol != NULL) && ((pcol->color != NULL) || (pcol->gradientInstPtr != NULL))) ? 1 : 0 )

/*
 * tkpath specific item types.
 */
MODULE_SCOPE Tk_PathItemType tkPathTypeWindow;
MODULE_SCOPE Tk_PathItemType tkPathTypePath;
MODULE_SCOPE Tk_PathItemType tkPathTypeRect;
MODULE_SCOPE Tk_PathItemType tkPathTypeLine;
MODULE_SCOPE Tk_PathItemType tkPathTypePolyline;
MODULE_SCOPE Tk_PathItemType tkPathTypePolygon;
MODULE_SCOPE Tk_PathItemType tkPathTypeCircle;
MODULE_SCOPE Tk_PathItemType tkPathTypeEllipse;
MODULE_SCOPE Tk_PathItemType tkPathTypeImage;
MODULE_SCOPE Tk_PathItemType tkPathTypeText;
MODULE_SCOPE Tk_PathItemType tkPathTypeGroup;
MODULE_SCOPE Tk_PathSmoothMethod    tkPathBezierSmoothMethod;

/*
 * Tcl variable and command names
 */
/* Tcl variables */
#define TK_PATHVAR_PREMULTIPLYALPHA "::path::premultiplyalpha"
#define TK_PATHVAR_DEPIXELIZE		"::path::depixelize"
#define TK_PATHVAR_ANTIALIAS		"::path::antialias"
/* Tcl commands */
#define TK_PATHCMD_CLASS            "::path"
#define TK_PATHCMD_PIXELALIGN		"::path::pixelalign"
#define TK_PATHCMD_GRADIENT			"::gradient"
#define TK_PATHCMD_PATHGRADIENT		"::path::gradient"
#define TK_PATHCMD_STYLE			"::style"
#define TK_PATHCMD_PATHSTYLE		"::path::style"
#define TK_PATHCMD_PATHSURFACE		"::path::surface"

/*
 * Function declarations:
 */

/* tkPathCanvArrow.c */
MODULE_SCOPE void   TkPathArrowDescrInit(TkPathArrowDescr *descr);
MODULE_SCOPE void   TkPathIncludeArrowPointsInRect(TkPathRect *bbox,
    TkPathArrowDescr *arrowDescrPtr);
MODULE_SCOPE void   TkPathIncludeArrowPoints(Tk_PathItem *itemPtr,
    TkPathArrowDescr *arrowDescrPtr);
MODULE_SCOPE void   TkPathPreconfigureArrow(TkPathPoint *pf,
    TkPathArrowDescr *arrowDescr);
MODULE_SCOPE TkPathPoint TkPathConfigureArrow(TkPathPoint pf,
    TkPathPoint pl, TkPathArrowDescr *arrow, Tk_PathStyle *lineStyle,
    int updateFirstPoint);
MODULE_SCOPE void   TkPathTranslateArrow(TkPathArrowDescr *arrowDescr,
    double deltaX, double deltaY);
MODULE_SCOPE void   TkPathScaleArrow(TkPathArrowDescr *arrowDescr,
    double originX, double originY, double scaleX, double scaleY);
MODULE_SCOPE void   TkPathFreeArrow(TkPathArrowDescr *arrowDescr);
MODULE_SCOPE int    TkPathGetSegmentsFromPathAtomList(TkPathAtom *firstAtom,
    TkPathPoint **firstPt, TkPathPoint *secondPt,
    TkPathPoint *penultPt, TkPathPoint **lastPt);
MODULE_SCOPE TkPathAtom *   TkPathMakePathAtomsFromArrow(
    TkPathArrowDescr *arrowDescr);
MODULE_SCOPE void   TkPathDisplayArrow(Tk_PathCanvas canvas,
    TkPathArrowDescr *arrowDescr, Tk_PathStyle *const style,
    TkPathMatrix *mPtr, TkPathRect *bboxPtr);
MODULE_SCOPE void   TkPathPaintArrow(TkPathContext context,
    TkPathArrowDescr *arrowDescr, Tk_PathStyle *const style,
    TkPathRect *bboxPtr);

/* tkPathCanvas.c */
MODULE_SCOPE int    Tk_PathCanvasObjCmd(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
MODULE_SCOPE void   Tk_PathCanvasEventuallyRedraw(Tk_PathCanvas canvas,
    int x1, int y1, int x2, int y2);
MODULE_SCOPE void   TkPathCanvasSetParent(Tk_PathItem *parentPtr,
    Tk_PathItem *itemPtr);
MODULE_SCOPE int    TkPathCanvasFindGroup(Tcl_Interp *interp,
    Tk_PathCanvas canvas, Tcl_Obj *parentObj, Tk_PathItem **parentPtrPtr);
MODULE_SCOPE void   TkPathCanvasGroupBbox(Tk_PathCanvas canvas,
    Tk_PathItem *itemPtr, int *x1Ptr, int *y1Ptr, int *x2Ptr, int *y2Ptr);
MODULE_SCOPE Tk_PathItem *  TkPathCanvasItemIteratorNext(Tk_PathItem *itemPtr);
MODULE_SCOPE Tk_PathItem *  TkPathCanvasItemIteratorPrev(Tk_PathItem *itemPtr);
MODULE_SCOPE void   TkPathCanvasItemDetach(Tk_PathItem *itemPtr);
MODULE_SCOPE void   TkPathGroupItemConfigured(Tk_PathCanvas canvas,
    Tk_PathItem *itemPtr, int mask);
MODULE_SCOPE void   TkPathCanvasTranslateGroup(Tk_PathCanvas canvas,
    Tk_PathItem *itemPtr, int compensate, double deltaX, double deltaY);
MODULE_SCOPE void   TkPathCanvasScaleGroup(Tk_PathCanvas canvas,
    Tk_PathItem *itemPtr, int compensate,
    double originX, double originY, double scaleX, double scaleY);
MODULE_SCOPE void   TkPathCanvasSetParentToRoot(Tk_PathItem *itemPtr);

/* tkPathCanvEllipse.c */

/* tkPathCanvGroup.c */
MODULE_SCOPE void   TkPathCanvasSetGroupDirtyBbox(Tk_PathItem *itemPtr);
MODULE_SCOPE void   TkPathCanvasUpdateGroupBbox(Tk_PathCanvas canvas,
    Tk_PathItem *itemPtr);

/* tkPathCanvImage.c */

/* tkPathCanvLine.c */

/* tkPathCanvPath.c */

/* tkPathCanvPoly.c */

/* tkPathCanvRect.c */

/* tkPathCanvText.c */

/* tkPathCanvWindow.c */

/* tkPathGeneric.c */
MODULE_SCOPE int    TkPathParseToAtoms(Tcl_Interp *interp, Tcl_Obj *listObjPtr,
    TkPathAtom **atomPtrPtr, int *lenPtr);
MODULE_SCOPE void   TkPathFreeAtoms(TkPathAtom *pathAtomPtr);
MODULE_SCOPE int    TkPathNormalize(Tcl_Interp *interp, TkPathAtom *atomPtr,
    Tcl_Obj **listObjPtrPtr);
MODULE_SCOPE int    TkPathMakePath(TkPathContext ctx, TkPathAtom *atomPtr,
    Tk_PathStyle *stylePtr);
MODULE_SCOPE void   TkPathArcToUsingBezier(TkPathContext ctx,
    double rx, double ry, double phiDegrees, char largeArcFlag, char sweepFlag,
    double x2, double y2);
MODULE_SCOPE int    TkPathPdfNumber(Tcl_Obj *ret, int fracDigis,
    double number, const char *append);
MODULE_SCOPE int    TkPathPdfColor(Tcl_Obj *ret, XColor *colorPtr,
    const char *command);
MODULE_SCOPE int    TkPathPdfArrow(Tcl_Interp *interp, TkPathArrowDescr *arrow,
    Tk_PathStyle *const style);
MODULE_SCOPE int    TkPathPdf(Tcl_Interp *interp, TkPathAtom *atomPtr,
    Tk_PathStyle *stylePtr, TkPathRect *bboxPtr,
    int objc, Tcl_Obj *const objv[]);
MODULE_SCOPE Tcl_Obj    *TkPathExtGS(Tk_PathStyle *stylePtr, long *smaskRef);
MODULE_SCOPE TkPathAtom *TkPathNewMoveToAtom(double x, double y);
MODULE_SCOPE TkPathAtom *TkPathNewLineToAtom(double x, double y);
MODULE_SCOPE TkPathAtom *TkPathNewArcAtom(double radX, double radY,
    double angle, char largeArcFlag, char sweepFlag,
    double x, double y);
MODULE_SCOPE TkPathAtom *TkPathNewQuadBezierAtom(double ctrlX, double ctrlY,
    double anchorX, double anchorY);
MODULE_SCOPE TkPathAtom *TkPathNewCurveToAtom(double ctrlX1, double ctrlY1,
    double ctrlX2, double ctrlY2,
    double anchorX, double anchorY);
MODULE_SCOPE TkPathAtom *TkPathNewRectAtom(double pointsPtr[]);
MODULE_SCOPE TkPathAtom *TkPathNewCloseAtom(double x, double y);
MODULE_SCOPE int TkPathPixelAlignObjCmd(ClientData clientData,
    Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]);

/* tkPathGradient.c */
MODULE_SCOPE TkPathColor * TkPathGetPathColorStatic(Tcl_Interp *interp,
    Tk_Window tkwin, Tcl_Obj *nameObj);
MODULE_SCOPE TkPathGradientInst *TkPathGetGradient(Tcl_Interp *interp,
    const char *name, Tcl_HashTable *tablePtr,
    TkPathGradientChangedProc *changeProc, ClientData clientData);
MODULE_SCOPE void    TkPathFreeGradient(TkPathGradientInst *gradientPtr);
MODULE_SCOPE void    TkPathGradientChanged(TkPathGradientMaster *masterPtr,
    int flags);
MODULE_SCOPE void   TkPathGradientInit(Tcl_Interp* interp);
MODULE_SCOPE void   TkPathGradientPaint(TkPathContext ctx, TkPathRect *bbox,
    TkPathGradientMaster *gradientStylePtr, int fillRule, double fillOpacity);
MODULE_SCOPE void   TkPathGradientInit(Tcl_Interp* interp);
MODULE_SCOPE void   TkPathGradientPaint(TkPathContext ctx, TkPathRect *bbox,
    TkPathGradientMaster *gradientStylePtr, int fillRule, double fillOpacity);
MODULE_SCOPE int    TkPathCanvasGradientObjCmd(Tcl_Interp* interp,
    TkPathCanvas *canvasPtr, int objc, Tcl_Obj* const objv[]);
MODULE_SCOPE void   TkPathCanvasGradientsFree(TkPathCanvas *canvasPtr);

/* tkPathInit.c */
MODULE_SCOPE int TkPathSurfaceInit(Tcl_Interp *interp);

/* tkPathStyle.c */
MODULE_SCOPE TkPathDash     *TkPathDashNew(Tcl_Interp *interp,
    Tcl_Obj *dashObjPtr);
MODULE_SCOPE void   TkPathDashFree(TkPathDash *dashPtr);
MODULE_SCOPE Tcl_Obj    *Tk_PathDashOptionGetProc(ClientData clientData,
    Tk_Window tkwin, char *recordPtr, int internalOffset);
MODULE_SCOPE void   Tk_PathDashOptionRestoreProc(ClientData clientData,
    Tk_Window tkwin, char *internalPtr, char *oldInternalPtr);
MODULE_SCOPE void   Tk_PathDashOptionFreeProc(ClientData clientData,
    Tk_Window tkwin, char *internalPtr);
MODULE_SCOPE int    TkPathConfigStyle(Tcl_Interp* interp,
    Tk_PathStyle *stylePtr, int objc, Tcl_Obj* const objv[]);
MODULE_SCOPE int    TkPathStyleMergeStyleStatic(Tcl_Interp* interp,
    Tcl_Obj *styleObj, Tk_PathStyle *dstStyle, long flags);
MODULE_SCOPE void   TkPathStyleMergeStyles(Tk_PathStyle *srcStyle,
    Tk_PathStyle *dstStyle, long flags);
MODULE_SCOPE void   TkPathInitStyle(Tk_PathStyle *style);
MODULE_SCOPE void   TkPathDeleteStyle(Tk_PathStyle *style);
MODULE_SCOPE TkPathStyleInst *    TkPathGetStyle(Tcl_Interp *interp,
    const char *name, Tcl_HashTable *tablePtr,
    TkPathStyleChangedProc *changeProc, ClientData clientData);
MODULE_SCOPE void   TkPathFreeStyle(TkPathStyleInst *stylePtr);
MODULE_SCOPE void   TkPathStyleChanged(Tk_PathStyle *masterPtr, int flags);
MODULE_SCOPE void   TkPathStyleInit(Tcl_Interp* interp);
MODULE_SCOPE int    Tk_PathDashOptionSetProc(ClientData clientData,
    Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj **value, char *recordPtr,
    int internalOffset, char *oldInternalPtr, int flags);
MODULE_SCOPE void   TkPathStylesFree(Tk_Window tkwin,
    Tcl_HashTable *hashTablePtr);
MODULE_SCOPE int    TkPathCanvasStyleObjCmd(Tcl_Interp* interp,
    TkPathCanvas *canvasPtr, int objc, Tcl_Obj* const objv[]);
MODULE_SCOPE int    TkPathMatrixSetOption(ClientData clientData,
    Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj **value, char *recordPtr,
    int internalOffset, char *oldInternalPtr, int flags);
MODULE_SCOPE Tcl_Obj    *TkPathMatrixGetOption(ClientData clientData,
    Tk_Window tkwin, char *recordPtr, int internalOffset);
MODULE_SCOPE void    TkPathMatrixRestoreOption(ClientData clientData,
    Tk_Window tkwin, char *internalPtr, char *oldInternalPtr);
MODULE_SCOPE void    TkPathMatrixFreeOption(ClientData clientData,
    Tk_Window tkwin, char *internalPtr);
MODULE_SCOPE int     TkPathColorSetOption(ClientData clientData,
    Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj **value, char *recordPtr,
    int internalOffset, char *oldInternalPtr, int flags);
MODULE_SCOPE Tcl_Obj    *TkPathColorGetOption(ClientData clientData,
    Tk_Window tkwin, char *recordPtr, int internalOffset);
MODULE_SCOPE void    TkPathColorRestoreOption(ClientData clientData,
    Tk_Window tkwin, char *internalPtr, char *oldInternalPtr);
MODULE_SCOPE void    TkPathColorFreeOption(ClientData clientData,
    Tk_Window tkwin, char *internalPtr);

/* tkPathSurface.c */

/* tkPathUtil.c */
MODULE_SCOPE void   TkPathMakePrectAtoms(double *pointsPtr,
    double rx, double ry, TkPathAtom **atomPtrPtr);
MODULE_SCOPE void   TkPathDrawPath(TkPathContext context,
    TkPathAtom *atomPtr, Tk_PathStyle *stylePtr,
    TkPathMatrix *mPtr, TkPathRect *bboxPtr);
MODULE_SCOPE void   TkPathPaintPath(TkPathContext context, TkPathAtom *atomPtr,
    Tk_PathStyle *stylePtr, TkPathRect *bboxPtr);
MODULE_SCOPE TkPathRect TkPathGetTotalBbox(TkPathAtom *atomPtr,
    Tk_PathStyle *stylePtr);
MODULE_SCOPE TkPathColor *TkPathNewPathColor(Tcl_Interp *interp,
    Tk_Window tkwin, Tcl_Obj *nameObj);
MODULE_SCOPE TkPathColor *TkPathGetPathColor(Tcl_Interp *interp,
    Tk_Window tkwin, Tcl_Obj *nameObj, Tcl_HashTable *tablePtr,
    TkPathGradientChangedProc *changeProc, ClientData clientData);
MODULE_SCOPE void   TkPathFreePathColor(TkPathColor *colorPtr);
MODULE_SCOPE void   TkPathCopyBitsARGB(unsigned char *from,
    unsigned char *to, int width, int height, int bytesPerRow);
MODULE_SCOPE void   TkPathCopyBitsBGRA(unsigned char *from,
    unsigned char *to, int width, int height, int bytesPerRow);
MODULE_SCOPE void   TkPathCopyBitsPremultipliedAlphaRGBA(unsigned char *from,
    unsigned char *to, int width, int height, int bytesPerRow);
MODULE_SCOPE void   TkPathCopyBitsPremultipliedAlphaARGB(unsigned char *from,
    unsigned char *to, int width, int height, int bytesPerRow);
MODULE_SCOPE void   TkPathCopyBitsPremultipliedAlphaBGRA(unsigned char *from,
    unsigned char *to, int width, int height, int bytesPerRow);
MODULE_SCOPE int    TkPathTableLookup(TkLookupTable *map, int n, int from);
MODULE_SCOPE void   TkPathMMulTMatrix(TkPathMatrix *m1, TkPathMatrix *m2);
MODULE_SCOPE int    TkPathGetTMatrix(Tcl_Interp* interp, const char *list,
    TkPathMatrix *matrixPtr);
MODULE_SCOPE int    TkPathGetTclObjFromTMatrix(Tcl_Interp* interp,
    TkPathMatrix *matrixPtr, Tcl_Obj **listObjPtrPtr);
MODULE_SCOPE int    TkPathGenericCmdDispatcher(
    Tcl_Interp* interp, Tk_Window tkwin, int objc, Tcl_Obj* const objv[],
    char *baseName, int *baseNameUIDPtr, Tcl_HashTable *hashTablePtr,
    Tk_OptionTable optionTable, char *(*createAndConfigProc)(Tcl_Interp *interp,
    char *name, int objc, Tcl_Obj *const objv[]),
    void (*configNotifyProc)(char *recordPtr, int mask,
    int objc, Tcl_Obj *const objv[]),
    void (*freeProc)(Tcl_Interp *interp, char *recordPtr));
MODULE_SCOPE int    TkPathObjectIsEmpty(Tcl_Obj *objPtr);
MODULE_SCOPE void   TkPathIncludePoint(Tk_PathItem *itemPtr, double *pointPtr);
MODULE_SCOPE void   TkPathBezierScreenPoints(Tk_PathCanvas canvas,
    double control[], int numSteps, XPoint *xPointPtr);
MODULE_SCOPE void   TkPathBezierPoints(double control[], int numSteps,
    double *coordPtr);
MODULE_SCOPE int    TkPathMakeBezierCurve(Tk_PathCanvas canvas,
    double *pointPtr, int numPoints, int numSteps,
    XPoint xPoints[], double dblPoints[]);
MODULE_SCOPE int    TkPathMakeRawCurve(Tk_PathCanvas canvas, double *pointPtr,
    int numPoints, int numSteps, XPoint xPoints[], double dblPoints[]);
MODULE_SCOPE int    TkPathOffsetOptionSetProc(ClientData clientData,
    Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj **value, char *recordPtr,
    int internalOffset, char *oldInternalPtr, int flags);
MODULE_SCOPE Tcl_Obj *    TkPathOffsetOptionGetProc(ClientData clientData,
    Tk_Window tkwin, char *recordPtr, int internalOffset);
MODULE_SCOPE void   TkPathOffsetOptionRestoreProc(ClientData clientData,
    Tk_Window tkwin, char *internalPtr, char *oldInternalPtr);
MODULE_SCOPE void   TkPathOffsetOptionFreeProc(ClientData clientData,
    Tk_Window tkwin, char *internalPtr);
MODULE_SCOPE int    Tk_PathPixelOptionSetProc(ClientData clientData,
    Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj **value, char *recordPtr,
    int internalOffset, char *oldInternalPtr, int flags);
MODULE_SCOPE Tcl_Obj *    Tk_PathPixelOptionGetProc(ClientData clientData,
    Tk_Window tkwin, char *recordPtr, int internalOffset);
MODULE_SCOPE void   Tk_PathPixelOptionRestoreProc(ClientData clientData,
    Tk_Window tkwin, char *internalPtr, char *oldInternalPtr);
MODULE_SCOPE Tk_Window    Tk_PathCanvasTkwin(Tk_PathCanvas canvas);
MODULE_SCOPE void   Tk_PathCanvasDrawableCoords(Tk_PathCanvas canvas,
    double x, double y, short *drawableXPtr, short *drawableYPtr);
MODULE_SCOPE void    Tk_PathCanvasWindowCoords(Tk_PathCanvas canvas,
    double x, double y, short *screenXPtr, short *screenYPtr);
MODULE_SCOPE int    Tk_PathCanvasGetCoord(Tcl_Interp *interp,
    Tk_PathCanvas canvas, const char *string, double *doublePtr);
MODULE_SCOPE int    Tk_PathCanvasGetCoordFromObj(Tcl_Interp *interp,
    Tk_PathCanvas canvas, Tcl_Obj *obj, double *doublePtr);
MODULE_SCOPE void   Tk_PathCanvasSetStippleOrigin(Tk_PathCanvas canvas, GC gc);
MODULE_SCOPE void   Tk_PathCanvasSetOffset(Tk_PathCanvas canvas, GC gc,
    Tk_TSOffset *offset);
MODULE_SCOPE int    TkPathCanvasGetDepth(Tk_PathItem *itemPtr);
MODULE_SCOPE Tk_PathStyle   TkPathCanvasInheritStyle(Tk_PathItem *itemPtr,
    long flags);
MODULE_SCOPE void   TkPathCanvasFreeInheritedStyle(Tk_PathStyle *stylePtr);
MODULE_SCOPE TkPathMatrix   TkPathCanvasInheritTMatrix(Tk_PathItem *itemPtr);
MODULE_SCOPE Tcl_HashTable  *TkPathCanvasGradientTable(Tk_PathCanvas canvas);
MODULE_SCOPE Tcl_HashTable  *TkPathCanvasStyleTable(Tk_PathCanvas canvas);
MODULE_SCOPE Tk_PathState   TkPathCanvasState(Tk_PathCanvas canvas);
MODULE_SCOPE Tk_PathItem    *TkPathCanvasCurrentItem(Tk_PathCanvas canvas);
MODULE_SCOPE Tk_PathCanvasTextInfo *Tk_PathCanvasGetTextInfo(
    Tk_PathCanvas canvas);
MODULE_SCOPE Tk_PathTags    *TkPathAllocTagsFromObj(Tcl_Interp *interp,
    Tcl_Obj *valuePtr);
MODULE_SCOPE int    Tk_PathCanvasTagsOptionSetProc(ClientData clientData,
    Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj **value, char *recordPtr,
    int internalOffset, char *oldInternalPtr, int flags);
MODULE_SCOPE Tcl_Obj    *Tk_PathCanvasTagsOptionGetProc(ClientData clientData,
    Tk_Window tkwin, char *recordPtr, int internalOffset);
MODULE_SCOPE void   Tk_PathCanvasTagsOptionRestoreProc(ClientData clientData,
    Tk_Window tkwin, char *internalPtr, char *oldInternalPtr);
MODULE_SCOPE void   Tk_PathCanvasTagsOptionFreeProc(ClientData clientData,
    Tk_Window tkwin, char *internalPtr);
MODULE_SCOPE int    Tk_DashOptionSetProc(ClientData clientData,
    Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj **value, char *recordPtr,
    int internalOffset, char *oldInternalPtr, int flags);
MODULE_SCOPE Tcl_Obj    *Tk_DashOptionGetProc(ClientData clientData,
    Tk_Window tkwin, char *recordPtr, int internalOffset);
MODULE_SCOPE void   Tk_DashOptionRestoreProc(ClientData clientData,
    Tk_Window tkwin, char *internalPtr, char *oldInternalPtr);
MODULE_SCOPE void   Tk_DashOptionFreeProc(ClientData clientData,
    Tk_Window tkwin, char *internalPtr);
MODULE_SCOPE void   Tk_PathCreateSmoothMethod(Tcl_Interp * interp,
    Tk_PathSmoothMethod * method);
MODULE_SCOPE int    TkPathSmoothOptionSetProc(ClientData clientData,
    Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj **value, char *recordPtr,
    int internalOffset, char *oldInternalPtr, int flags);
MODULE_SCOPE Tcl_Obj    *TkPathSmoothOptionGetProc(ClientData clientData,
    Tk_Window tkwin, char *recordPtr, int internalOffset);
MODULE_SCOPE void   TkPathSmoothOptionRestoreProc(ClientData clientData,
    Tk_Window tkwin, char *internalPtr, char *oldInternalPtr);
MODULE_SCOPE void   Tk_PathCreateOutline(Tk_PathOutline *outline);
MODULE_SCOPE void   Tk_PathDeleteOutline(Display *display,
    Tk_PathOutline *outline);
MODULE_SCOPE int    Tk_PathConfigOutlineGC(XGCValues *gcValues,
    Tk_PathCanvas canvas, Tk_PathItem *item, Tk_PathOutline *outline);
MODULE_SCOPE int    Tk_PathChangeOutlineGC(Tk_PathCanvas canvas,
    Tk_PathItem *item, Tk_PathOutline *outline);
MODULE_SCOPE int    Tk_PathResetOutlineGC(Tk_PathCanvas canvas,
    Tk_PathItem *item, Tk_PathOutline *outline);
MODULE_SCOPE int    TkPathCanvTranslatePath(TkPathCanvas *canvPtr,
    int numVertex, double *coordPtr, int closed, XPoint *outPtr);
MODULE_SCOPE int    TkPathCanvasItemExConfigure(Tcl_Interp *interp,
    Tk_PathCanvas canvas, Tk_PathItemEx *itemExPtr, int mask);
MODULE_SCOPE int    TkPathEndpointToCentralArcParameters(
    /* Endpoints. */
    double x1, double y1, double x2, double y2,
    /* Radius. */
    double rx, double ry, double phi, char largeArcFlag, char sweepFlag,
    /* Output. */
    double *cxPtr, double *cyPtr, double *rxPtr, double *ryPtr,
    double *theta1Ptr, double *dthetaPtr);
MODULE_SCOPE void   TkPathGradientChangedPrc(ClientData clientData, int flags);
MODULE_SCOPE void   TkPathStyleChangedPrc(ClientData clientData, int flags);
MODULE_SCOPE void   TkPathCurveSegments(double control[], int includeFirst,
    int numSteps, double *coordPtr);
MODULE_SCOPE double TkPathRectToPoint(double rectPtr[], double width,
    int filled, double pointPtr[]);
MODULE_SCOPE int    TkPathRectToArea(double rectPtr[], double width,
    int filled, double *areaPtr);
MODULE_SCOPE int    TkPathRectToAreaWithMatrix(TkPathRect bbox,
    TkPathMatrix *mPtr, double *areaPtr);
MODULE_SCOPE double TkPathRectToPointWithMatrix(TkPathRect bbox,
    TkPathMatrix *mPtr, double *pointPtr);
MODULE_SCOPE void   TkPathCompensateScale(Tk_PathItem *itemPtr, int compensate,
    double *originX, double *originY, double *scaleX, double *scaleY);
MODULE_SCOPE void   TkPathCompensateTranslate(Tk_PathItem *itemPtr,
    int compensate, double *deltaX, double *deltaY);
MODULE_SCOPE void   TkPathScaleItemHeader(Tk_PathItem *itemPtr, double originX,
    double originY, double scaleX, double scaleY);
MODULE_SCOPE void   TkPathScalePathRect(TkPathRect *r, double originX,
    double originY, double scaleX, double scaleY);
MODULE_SCOPE void   TkPathTranslateItemHeader(Tk_PathItem *itemPtr,
    double deltaX, double deltaY);
MODULE_SCOPE int    TkPathCoordsForPointItems(Tcl_Interp *interp,
    Tk_PathCanvas canvas, double *pointPtr, int objc, Tcl_Obj *const objv[]);
MODULE_SCOPE int    TkPathCoordsForRectangularItems(Tcl_Interp *interp,
    Tk_PathCanvas canvas, TkPathRect *rectPtr, int objc, Tcl_Obj *const objv[]);
MODULE_SCOPE TkPathRect TkPathGetGenericBarePathBbox(TkPathAtom *atomPtr);
MODULE_SCOPE TkPathRect TkPathGetGenericPathTotalBboxFromBare(
    TkPathAtom *atomPtr, Tk_PathStyle *stylePtr, TkPathRect *bboxPtr);
MODULE_SCOPE void   TkPathSetGenericPathHeaderBbox(Tk_PathItem *headerPtr,
    TkPathMatrix *mPtr, TkPathRect *totalBboxPtr);
MODULE_SCOPE TkPathMatrix   TkPathGetCanvasTMatrix(Tk_PathCanvas canvas);

MODULE_SCOPE TkPathRect     TkPathNewEmptyPathRect(void);
MODULE_SCOPE void   TkPathIncludePointInRect(TkPathRect *r, double x, double y);
MODULE_SCOPE double TkPathGenericPathToPoint(Tk_PathCanvas canvas,
    Tk_PathItem *itemPtr, Tk_PathStyle *stylePtr, TkPathAtom *atomPtr,
    int maxNumSegments, double *pointPtr);
MODULE_SCOPE int    TkPathGenericPathToArea(Tk_PathCanvas canvas,
    Tk_PathItem *itemPtr, Tk_PathStyle *stylePtr, TkPathAtom * atomPtr,
    int maxNumSegments, double *areaPtr);
MODULE_SCOPE void   TkPathTranslatePathAtoms(TkPathAtom *atomPtr, double deltaX,
    double deltaY);
MODULE_SCOPE void   TkPathScalePathAtoms(TkPathAtom *atomPtr, double originX,
    double originY, double scaleX, double scaleY);
MODULE_SCOPE void   TkPathTranslatePathRect(TkPathRect *r, double deltaX,
    double deltaY);

/* tkMacOSXPath.c tkSDLAGGPath.cpp tkUnixCairoPath.c tkWinGDIPlusPath.cpp */
MODULE_SCOPE int    TkPathSetup(Tcl_Interp *interp);
MODULE_SCOPE TkPathContext  TkPathInit(Tk_Window tkwin, Drawable d);
MODULE_SCOPE TkPathContext  TkPathInitSurface(Display *display,
    int width, int height);
MODULE_SCOPE void   TkPathBeginPath(TkPathContext ctx, Tk_PathStyle *stylePtr);
MODULE_SCOPE void   TkPathEndPath(TkPathContext ctx);
MODULE_SCOPE void   TkPathMoveTo(TkPathContext ctx, double x, double y);
MODULE_SCOPE void   TkPathLineTo(TkPathContext ctx, double x, double y);
MODULE_SCOPE void   TkPathArcTo(TkPathContext ctx, double rx, double ry,
    double angle, char largeArcFlag, char sweepFlag, double x, double y);
MODULE_SCOPE void   TkPathQuadBezier(TkPathContext ctx,
    double ctrlX, double ctrlY, double x, double y);
MODULE_SCOPE void   TkPathCurveTo(TkPathContext ctx,
    double ctrlX1, double ctrlY1,
    double ctrlX2, double ctrlY2, double x, double y);
MODULE_SCOPE void   TkPathRectangle(TkPathContext ctx, double x, double y,
    double width, double height);
MODULE_SCOPE void   TkPathOval(TkPathContext ctx, double cx, double cy,
    double rx, double ry);
MODULE_SCOPE void   TkPathClosePath(TkPathContext ctx);
MODULE_SCOPE void   TkPathImage(TkPathContext ctx, Tk_Image image,
    Tk_PhotoHandle photo, double x, double y, double width, double height,
    double fillOpacity, XColor *tintColor, double tintAmount,
    int interpolation, TkPathRect *srcRegion);
MODULE_SCOPE int    TkPathTextConfig(Tcl_Interp *interp,
    Tk_PathTextStyle *textStylePtr, char *utf8, void **customPtr);
MODULE_SCOPE void   TkPathTextDraw(TkPathContext ctx, Tk_PathStyle *style,
    Tk_PathTextStyle *textStylePtr, double x, double y,
    int fillOverStroke, char *utf8, void *custom);
MODULE_SCOPE void   TkPathTextFree(Tk_PathTextStyle *textStylePtr,
    void *custom);
MODULE_SCOPE TkPathRect TkPathTextMeasureBbox(Display *display,
    Tk_PathTextStyle *textStylePtr, char *utf8,
    double *lineSpacing, void *custom);
MODULE_SCOPE void   TkPathSurfaceErase(TkPathContext ctx, double x, double y,
    double width, double height);
MODULE_SCOPE void   TkPathSurfaceToPhoto(Tcl_Interp *interp,
    TkPathContext ctx, Tk_PhotoHandle photo);
MODULE_SCOPE void   TkPathClipToPath(TkPathContext ctx, int fillRule);
MODULE_SCOPE void   TkPathReleaseClipToPath(TkPathContext ctx);
MODULE_SCOPE void   TkPathStroke(TkPathContext ctx, Tk_PathStyle *style);
MODULE_SCOPE void   TkPathFill(TkPathContext ctx, Tk_PathStyle *style);
MODULE_SCOPE void   TkPathFillAndStroke(TkPathContext ctx, Tk_PathStyle *style);
MODULE_SCOPE int    TkPathGetCurrentPosition(TkPathContext ctx,
    TkPathPoint *ptPtr);
MODULE_SCOPE int    TkPathBoundingBox(TkPathContext ctx, TkPathRect *rPtr);
MODULE_SCOPE void   TkPathPaintLinearGradient(TkPathContext ctx,
    TkPathRect *bbox, TkLinearGradientFill *fillPtr,
    int fillRule, double fillOpacity, TkPathMatrix *matrixPtr);
MODULE_SCOPE void   TkPathPaintRadialGradient(TkPathContext ctx,
    TkPathRect *bbox, TkRadialGradientFill *fillPtr,
    int fillRule, double fillOpacity, TkPathMatrix *mPtr);
MODULE_SCOPE void   TkPathFree(TkPathContext ctx);
MODULE_SCOPE int    TkPathDrawingDestroysPath(void);
MODULE_SCOPE int    TkPathPixelAlign(void);
MODULE_SCOPE void   TkPathPushTMatrix(TkPathContext ctx, TkPathMatrix *mPtr);
MODULE_SCOPE void   TkPathResetTMatrix(TkPathContext ctx);
MODULE_SCOPE void   TkPathSaveState(TkPathContext ctx);
MODULE_SCOPE void   TkPathRestoreState(TkPathContext ctx);

/*
 * end block for C++
 */

#ifdef __cplusplus
}
#endif

#endif      /* _TKPATHINT_H */
/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
