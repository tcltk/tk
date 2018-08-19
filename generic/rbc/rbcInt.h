/*
 * rbcInt.h --
 *
 *      This file constructs the basic functionality of the
 *      rbc commands.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _RBCINT
#define _RBCINT

#include <tcl.h>
#include <tclInt.h>             /* only #define's and inline functions */

#include <tk.h>
#include <tkInt.h>

#ifndef _WIN32
#include <X11/Xproto.h>
#endif

#if defined(_WIN32)
#include "tkWinInt.h"
#elif defined(MAC_OSX_TK)
#include "tkMacOSXInt.h"
#else
#include "tkUnixInt.h"
#endif

#include "tk3d.h"
#include "tkFont.h"

#include <assert.h>

/*
 * Mathematical functions
 */
#undef ABS
#define ABS(x)		(((x)<0)?(-(x)):(x))

#undef EXP10
#define EXP10(x)	(pow(10.0,(x)))

#undef FABS
#define FABS(x) 	(((x)<0.0)?(-(x)):(x))

#undef SIGN
#define SIGN(x)		(((x) < 0.0) ? -1 : 1)

#undef MIN
#define MIN(a,b)	(((a)<(b))?(a):(b))

#undef MAX
#define MAX(a,b)	(((a)>(b))?(a):(b))

#undef MIN3
#define MIN3(a,b,c)	(((a)<(b))?(((a)<(c))?(a):(c)):(((b)<(c))?(b):(c)))

#undef MAX3
#define MAX3(a,b,c)	(((a)>(b))?(((a)>(c))?(a):(c)):(((b)>(c))?(b):(c)))

#define CLAMP(val,low,high)	\
	(((val) < (low)) ? (low) : ((val) > (high)) ? (high) : (val))

/*
 * Be careful when using the next two macros.  They both assume the floating
 * point number is less than the size of an int.  That means, for example, you
 * can't use these macros with numbers bigger than than 2^31-1.
 */
#undef FMOD
#define FMOD(x,y) 	((x)-(((int)((x)/(y)))*y))

#undef ROUND
#define ROUND(x) 	((int)((x) + (((x)<0.0) ? -0.5 : 0.5)))

/*
 * RBC defines start here
 */
#define RBC_VERSION "0.2"
#define RBC_MAJOR_VERSION 0
#define RBC_MINOR_VERSION 2

/*
 * Used standard values
 */
#define RBC_NORMAL_BG_MONO	    "white"
#define RBC_NORMAL_FG_MONO	    "black"
#define RBC_ACTIVE_BG_MONO	    "black"
#define RBC_ACTIVE_FG_MONO	    "white"
#define RBC_SELECT_BG_MONO	    "black"
#define RBC_SELECT_FG_MONO      "black"
#define RBC_SELECT_BORDERWIDTH  "2"
#define RBC_BORDERWIDTH 	    "2"
#define RBC_FONT                "TkDefaultFont 10"
#define RBC_FONT_HUGE           "TkDefaultFontArial 16"
#define RBC_FONT_LARGE          "TkDefaultFontArial 12"
#define RBC_FONT_SMALL          "TkDefaultFontArial 8"
#define RBC_NORMAL_BACKGROUND   "gray85"
#define RBC_NORMAL_FOREGROUND   "black"
#define RBC_ACTIVE_BACKGROUND   "gray64"
#define RBC_ACTIVE_FOREGROUND   "black"
#define RBC_SELECT_BACKGROUND   "lightblue1"
#define RBC_SELECT_FOREGROUND   "black"
#define RBC_INDICATOR_COLOR	    "maroon"

#define RBC_OP_LINEAR_SEARCH	1
#define RBC_OP_BINARY_SEARCH	0

#define RBC_STATIC_STRING_SPACE 150

#define RBC_NS_SEARCH_NONE		(0)
#define RBC_NS_SEARCH_CURRENT	(1<<0)
#define RBC_NS_SEARCH_GLOBAL	(1<<1)
#define RBC_NS_SEARCH_BOTH		(RBC_NS_SEARCH_GLOBAL | RBC_NS_SEARCH_CURRENT)

/* Recognize "min", "max", and "++end" as valid indices */
#define RBC_INDEX_SPECIAL	(1<<0)

/* Also recognize a range of indices separated by a colon */
#define RBC_INDEX_COLON	(1<<1)

/* Verify that the specified index or range of indices are within limits */
#define RBC_INDEX_CHECK	(1<<2)
#define RBC_INDEX_ALL_FLAGS    (RBC_INDEX_SPECIAL | RBC_INDEX_COLON | RBC_INDEX_CHECK)
#define RBC_SPECIAL_INDEX		-2

/* The data of the vector has changed.  Update the min and max limits when they are needed */
#define RBC_UPDATE_RANGE		(1<<9)

#define RBC_COLOR_NONE		    (XColor *)0
#define RBC_COLOR_DEFAULT		(XColor *)1
#define RBC_COLOR_ALLOW_DEFAULTS	1

#define RBC_STATE_NORMAL	0
#define RBC_STATE_ACTIVE	(1<<0)
#define RBC_STATE_DISABLED	(1<<1)
#define RBC_STATE_EMPHASIS	(1<<2)

#define RBC_ROTATE_0	0
#define RBC_ROTATE_90	1
#define RBC_ROTATE_180	2
#define RBC_ROTATE_270	3

#define RBC_SEARCH_X	0
#define RBC_SEARCH_Y	1
#define RBC_SEARCH_BOTH	2

#define RBC_SHOW_NONE	0
#define RBC_SHOW_X		1
#define RBC_SHOW_Y		2
#define RBC_SHOW_BOTH	3

#define RBC_SEARCH_POINTS	0       /* Search for closest data point. */
#define RBC_SEARCH_TRACES	1       /* Search for closest point on trace.
                                         * Interpolate the connecting line segments
                                         * if necessary. */
#define RBC_SEARCH_AUTO	2       /* Automatically determine whether to search
                                 * for data points or traces.  Look for
                                 * traces if the linewidth is > 0 and if
                                 * there is more than one data point. */

#define	RBC_ELEM_ACTIVE	(1<<8)  /* Non-zero indicates that the element
                                 * should be drawn in its active
                                 * foreground and background
                                 * colors. */
#define	RBC_ACTIVE_PENDING	(1<<7)

#define	RBC_LABEL_ACTIVE 	(1<<9)  /* Non-zero indicates that the
                                         * element's entry in the legend
                                         * should be drawn in its active
                                         * foreground and background
                                         * colors. */
#define RBC_SCALE_SYMBOL	(1<<10)

#define RBC_SWITCH_ARGV_ONLY		(1<<0)
#define RBC_SWITCH_OBJV_ONLY		(1<<0)
#define RBC_SWITCH_ARGV_PARTIAL		(1<<1)
#define RBC_SWITCH_OBJV_PARTIAL		(1<<1)

/*
 * Possible flag values for RbcSwitchSpec structures.  Any bits at
 * or above RBC_SWITCH_USER_BIT may be used by clients for selecting
 * certain entries.
 */
#define RBC_SWITCH_NULL_OK		(1<<0)
#define RBC_SWITCH_DONT_SET_DEFAULT	(1<<3)
#define RBC_SWITCH_SPECIFIED		(1<<4)
#define RBC_SWITCH_USER_BIT		(1<<8)

/*
 * Bit flags definitions:
 *
 * 	All kinds of state information kept here.  All these
 *	things happen when the window is available to draw into
 *	(DisplayGraph). Need the window width and height before
 *	we can calculate graph layout (i.e. the screen coordinates
 *	of the axes, elements, titles, etc). But we want to do this
 *	only when we have to, not every time the graph is redrawn.
 *
 *	Same goes for maintaining a pixmap to double buffer graph
 *	elements.  Need to mark when the pixmap needs to updated.
 *
 *
 *	MAP_ITEM		Indicates that the element/marker/axis
 *				configuration has changed such that
 *				its layout of the item (i.e. its
 *				position in the graph window) needs
 *				to be recalculated.
 *
 *	MAP_ALL			Indicates that the layout of the axes and
 *				all elements and markers and the graph need
 *				to be recalculated. Otherwise, the layout
 *				of only those markers and elements that
 *				have changed will be reset.
 *
 *	GET_AXIS_GEOMETRY	Indicates that the size of the axes needs
 *				to be recalculated.
 *
 *	RESET_AXES		Flag to call to RbcResetAxes routine.
 *				This routine recalculates the scale offset
 *				(used for mapping coordinates) of each axis.
 *				If an axis limit has changed, then it sets
 *				flags to re-layout and redraw the entire
 *				graph.  This needs to happend before the axis
 *				can compute transformations between graph and
 *				screen coordinates.
 *
 *	LAYOUT_NEEDED
 *
 *	REDRAW_BACKING_STORE	If set, redraw all elements into the pixmap
 *				used for buffering elements.
 *
 *	REDRAW_PENDING		Non-zero means a DoWhenIdle handler has
 *				already been queued to redraw this window.
 *
 *	DRAW_LEGEND		Non-zero means redraw the legend. If this is
 *				the only DRAW_* flag, the legend display
 *				routine is called instead of the graph
 *				display routine.
 *
 *	DRAW_MARGINS		Indicates that the margins bordering
 *				the plotting area need to be redrawn.
 *				The possible reasons are:
 *
 *				1) an axis configuration changed
 *				2) an axis limit changed
 *				3) titles have changed
 *				4) window was resized.
 *
 *	GRAPH_FOCUS
 */
#define	RBC_MAP_ITEM		(1<<0)  /* 0x0001 */
#define	RBC_MAP_ALL			(1<<1)  /* 0x0002 */
#define	RBC_GET_AXIS_GEOMETRY	(1<<2)  /* 0x0004 */
#define RBC_RESET_AXES		(1<<3)  /* 0x0008 */
#define RBC_LAYOUT_NEEDED		(1<<4)  /* 0x0010 */

#define RBC_REDRAW_PENDING		(1<<8)  /* 0x0100 */
#define RBC_DRAW_LEGEND		(1<<9)  /* 0x0200 */
#define RBC_DRAW_MARGINS		(1<<10) /* 0x0400 */
#define	RBC_REDRAW_BACKING_STORE	(1<<11) /* 0x0800 */

#define RBC_GRAPH_FOCUS		(1<<12) /* 0x1000 */
#define RBC_DATA_CHANGED		(1<<13) /* 0x2000 */

#define	RBC_MAP_WORLD		(RBC_MAP_ALL|RBC_RESET_AXES|RBC_GET_AXIS_GEOMETRY)
#define RBC_REDRAW_WORLD	(RBC_DRAW_MARGINS | RBC_DRAW_LEGEND)
#define RBC_RESET_WORLD		(RBC_REDRAW_WORLD | RBC_MAP_WORLD)

/* Legend */
#define RBC_LEGEND_RIGHT	(1<<0)  /* Right margin */
#define RBC_LEGEND_LEFT	(1<<1)  /* Left margin */
#define RBC_LEGEND_BOTTOM	(1<<2)  /* Bottom margin */
#define RBC_LEGEND_TOP	(1<<3)  /* Top margin, below the graph title. */
#define RBC_LEGEND_PLOT	(1<<4)  /* Plot area */
#define RBC_LEGEND_XY	(1<<5)  /* Screen coordinates in the plotting
                                 * area. */
#define RBC_LEGEND_WINDOW	(1<<6)  /* External window. */
#define RBC_LEGEND_IN_MARGIN \
	(RBC_LEGEND_RIGHT | RBC_LEGEND_LEFT | RBC_LEGEND_BOTTOM | RBC_LEGEND_TOP)
#define RBC_LEGEND_IN_PLOT  (RBC_LEGEND_PLOT | RBC_LEGEND_XY)

#define RBC_MARKER_UNDER	1       /* Draw markers designated to lie underneath
                                         * elements, grids, legend, etc. */
#define RBC_MARKER_ABOVE	0       /* Draw markers designated to rest above
                                         * elements, grids, legend, etc. */

/*
 * Mask values used to selectively enable GRAPH or BARCHART entries in
 * the various configuration specs.
 */
#define RBC_GRAPH		(TK_CONFIG_USER_BIT << 1)
#define RBC_STRIPCHART	(TK_CONFIG_USER_BIT << 2)
#define RBC_BARCHART	(TK_CONFIG_USER_BIT << 3)
#define RBC_LINE_GRAPHS	(RBC_GRAPH | RBC_STRIPCHART)
#define RBC_ALL_GRAPHS	(RBC_GRAPH | RBC_BARCHART | RBC_STRIPCHART)

#define RBC_PEN_DELETE_PENDING	(1<<0)
#define RBC_ACTIVE_PEN		(TK_CONFIG_USER_BIT << 6)
#define RBC_NORMAL_PEN		(TK_CONFIG_USER_BIT << 7)
#define RBC_ALL_PENS		(RBC_NORMAL_PEN | RBC_ACTIVE_PEN)

#define RBC_MARGIN_NONE     -1
#define RBC_MARGIN_BOTTOM   0
#define RBC_MARGIN_LEFT     1
#define RBC_MARGIN_TOP      2
#define RBC_MARGIN_RIGHT    3

/* forward references, defined later */
typedef struct RbcGraph RbcGraph;
typedef struct RbcElement RbcElement;
typedef struct RbcLegend RbcLegend;
typedef struct RbcBindTable RbcBindTable;
typedef struct RbcChainLink RbcChainLink;
typedef struct RbcTileClient *RbcTile;  /* Opaque type for tiles */
typedef struct RbcPsToken RbcPsToken;
typedef struct RbcPen RbcPen;
typedef struct RbcMarker RbcMarker;
typedef struct RbcCrosshairs RbcCrosshairs;
typedef struct RbcParseValue RbcParseValue;

/*
 * RbcUid --
 */
typedef const char *RbcUid;

/*
 * RbcPad --
 *
 * 	Specifies vertical and horizontal padding.
 *
 *	Padding can be specified on a per side basis.  The fields
 *	side1 and side2 refer to the opposite sides, either
 *	horizontally or vertically.
 *
 *		side1	side2
 *              -----   -----
 *      x | left    right
 *	    y | top     bottom
 */
typedef struct {
    short int       side1;
    short int       side2;
} RbcPad;

/*
 * RbcTextFragment --
 */
typedef struct {
    char           *text;       /* Text to be displayed */
    short int       x, y;       /* X-Y offset of the baseline from the
                                 * upper-left corner of the bbox. */
    short int       sx, sy;     /* See rbcWinUtil.c */
    short int       count;      /* Number of bytes in text. The actual
                                 * character count may differ because of
                                 * multi-byte UTF encodings. */
    short int       width;      /* Width of segment in pixels. This
                                 * information is used to draw
                                 * PostScript strings the same width
                                 * as X. */
} RbcTextFragment;

/*
 * RbcTextLayout --
 */
typedef struct {
    int             nFrags;     /* # fragments of text */
    short int       width, height;      /* Dimensions of text bounding box */
    RbcTextFragment fragArr[1]; /* Information about each fragment of text */
} RbcTextLayout;

/*
 * RbcShadow --
 */
typedef struct {
    XColor         *color;
    int             offset;
} RbcShadow;

/*
 * RbcTextStyle --
 *
 * 	Represents a convenient structure to hold text attributes
 *	which determine how a text string is to be displayed on the
 *	window, or drawn with PostScript commands.  The alternative
 *	is to pass lots of parameters to the drawing and printing
 *	routines. This seems like a more efficient and less cumbersome
 *	way of passing parameters.
 */
typedef struct {
    unsigned int    state;      /* If non-zero, indicates to draw text
                                 * in the active color */
    short int       width, height;      /* Extents of text */
    XColor         *color;      /* Normal color */
    XColor         *activeColor;        /* Active color */
    Tk_Font         font;       /* Font to use to draw text */
    Tk_3DBorder     border;     /* Background color of text.  This is also
                                 * used for drawing disabled text. */
    RbcShadow       shadow;     /* Drop shadow color and offset */
    Tk_Justify      justify;    /* Justification of the text string. This
                                 * only matters if the text is composed
                                 * of multiple lines. */
    GC              gc;         /* GC used to draw the text */
    double          theta;      /* Rotation of text in degrees. */
    Tk_Anchor       anchor;     /* Indicates how the text is anchored around
                                 * its x and y coordinates. */
    RbcPad          padX, padY; /* # pixels padding of around text region */
    short int       leader;     /* # pixels spacing between lines of text */
} RbcTextStyle;

/*
 *
 */
typedef ClientData(
    RbcBindPickProc) (
    ClientData clientData,
    int x,
    int y,
    ClientData * contextPtr);

/*
 * RbcBindTable --
 *
 * Binding structure information:
 */
typedef struct RbcBindTable {
    unsigned int    flags;
    Tk_BindingTable bindingTable;       /* Table of all bindings currently defined.
                                         * NULL means that no bindings exist, so the
                                         * table hasn't been created.  Each "object"
                                         * used for this table is either a Tk_Uid for
                                         * a tag or the address of an item named by
                                         * id. */
    ClientData      currentItem;        /* The item currently containing the mouse
                                         * pointer, or NULL if none. */
    ClientData      currentContext;     /* One word indicating what kind of object
                                         * was picked. */
    ClientData      newItem;    /* The item that is about to become the
                                 * current one, or NULL.  This field is
                                 * used to detect deletions of the new
                                 * current item pointer that occur during
                                 * Leave processing of the previous current
                                 * tab.  */
    ClientData      newContext; /* One-word indicating what kind of object
                                 * was just picked. */
    ClientData      focusItem;
    ClientData      focusContext;
    XEvent          pickEvent;  /* The event upon which the current choice
                                 * of the current tab is based.  Must be saved
                                 * so that if the current item is deleted,
                                 * we can pick another. */
    int             activePick; /* The pick event has been initialized so
                                 * that we can repick it */
    int             state;      /* Last known modifier state.  Used to
                                 * defer picking a new current object
                                 * while buttons are down. */
    ClientData      clientData;
    Tk_Window       tkwin;
    RbcBindPickProc *pickProc;  /* Routine to report the item the mouse is
                                 * currently over. */
} RbcBindTable;

/*
 * RbcChainLink --
 *
 * A RbcChainLink is the container structure for the RbcChain.
 */
typedef struct RbcChainLink {
    RbcChainLink   *prevPtr;    /* Link to the previous link */
    RbcChainLink   *nextPtr;    /* Link to the next link */
    ClientData      clientData; /* Pointer to the data object */
} RbcChainLink;

typedef int     (RbcChainCompareProc) (
    RbcChainLink ** l1PtrPtr,
    RbcChainLink ** l2PtrPtr);

/*
 * RbcChain --
 *
 * A RbcChain is a doubly chained list structure.
 */
typedef struct {
    RbcChainLink   *headPtr;    /* Pointer to first element in chain */
    RbcChainLink   *tailPtr;    /* Pointer to last element in chain */
    int             nLinks;     /* Number of elements in chain */
} RbcChain;

/*
 * RbcVector --
 */
typedef struct {
    double         *valueArr;   /* Array of values (possibly malloc-ed) */
    int             numValues;  /* Number of values in the array */
    int             arraySize;  /* Size of the allocated space */
    double          min, max;   /* Minimum and maximum values in the vector */
    int             dirty;      /* Indicates if the vector has been updated */
    int             reserved;   /* Reserved for future use */
} RbcVector;

/*
 * RbcVectorInterpData --
 */
typedef struct {
    Tcl_HashTable   vectorTable;        /* Table of vectors */
    Tcl_HashTable   mathProcTable;      /* Table of vector math functions */
    Tcl_HashTable   indexProcTable;
    Tcl_Interp     *interp;
    unsigned int    nextId;
} RbcVectorInterpData;

/*
 * RbcVectorObject --
 *
 *	A vector is an array of double precision values.  It can be
 *	accessed through a Tcl command, a Tcl array variable, or C
 *	API. The storage for the array points initially to a
 *	statically allocated buffer, but to malloc-ed memory if more
 *	is necessary.
 *
 *	Vectors can be shared by several clients (for example, two
 *	different graph widgets).  The data is shared. When a client
 *	wants to use a vector, it allocates a vector identifier, which
 *	identifies the client.  Clients use this ID to specify a
 *	callback routine to be invoked whenever the vector is modified
 *	or destroyed.  Whenever the vector is updated or destroyed,
 *	each client is notified of the change by their callback
 *	routine.
 */
typedef struct {
    /*
     * If you change these fields, make sure you change the definition
     * of RbcVector in rbcVector.h too.
     */
    double         *valueArr;   /* Array of values (malloc-ed) */
    int             length;     /* Current number of values in the array. */
    int             size;       /* Maximum number of values that can be stored
                                 * in the value array. */
    double          min, max;   /* Minimum and maximum values in the vector */
    int             dirty;      /* Indicates if the vector has been updated */
    int             reserved;
    /* The following fields are local to this module  */
    char           *name;       /* The namespace-qualified name of the vector command.
                                 * It points to the hash key allocated for the
                                 * entry in the vector hash table. */
    RbcVectorInterpData *dataPtr;
    Tcl_Interp     *interp;     /* Interpreter associated with the vector */
    Tcl_HashEntry  *hashPtr;    /* If non-NULL, pointer in a hash table to
                                 * track the vectors in use. */
    Tcl_FreeProc   *freeProc;   /* Address of procedure to call to
                                 * release storage for the value
                                 * array, Optionally can be one of the
                                 * following: TCL_STATIC, TCL_DYNAMIC,
                                 * or TCL_VOLATILE. */
    char           *arrayName;  /* The namespace-qualified name of the
                                 * Tcl array variable
                                 * mapped to the vector
                                 * (malloc'ed). If NULL, indicates
                                 * that the vector isn't mapped to any variable */
    int             offset;     /* Offset from zero of the vector's
                                 * starting index */
    Tcl_Command     cmdToken;   /* Token for vector's Tcl command. */
    RbcChain       *chainPtr;   /* List of clients using this vector */
    int             notifyFlags;        /* Notification flags. See definitions
                                         * below */
    int             varFlags;   /* Indicate if the variable is global,
                                 * namespace, or local */
    int             freeOnUnset;        /* For backward compatibility only: If
                                         * non-zero, free the vector when its
                                         * variable is unset. */
    int             flush;
    int             first, last;        /* Selected region of vector. This is used
                                         * mostly for the math routines */
} RbcVectorObject;

typedef struct RbcVectorIdStruct *RbcVectorId;

/*
 * RbcVectorNotify --
 */
typedef enum {
    RBC_VECTOR_NOTIFY_UPDATE = 1,       /* The vector's values has been updated */
    RBC_VECTOR_NOTIFY_DESTROY   /* The vector has been destroyed and the client
                                 * should no longer use its data (calling
                                 * RbcFreeVectorId) */
} RbcVectorNotify;

typedef void    (RbcVectorChangedProc) (
    Tcl_Interp * interp,
    ClientData clientData,
    RbcVectorNotify notify);
typedef double  (RbcVectorIndexProc) (
    RbcVector * vecPtr);

/*
 * RbcParseValue --
 *
 *	The following data structure is used by various parsing
 *	procedures to hold information about where to store the
 *	results of parsing (e.g. the substituted contents of a quoted
 *	argument, or the result of a nested command).  At any given
 *	time, the space available for output is fixed, but a procedure
 *	may be called to expand the space available if the current
 *	space runs out.
 */
typedef struct RbcParseValue {
    char           *buffer;     /* Address of first character in
                                 * output buffer. */
    char           *next;       /* Place to store next character in
                                 * output buffer. */
    char           *end;        /* Address of the last usable character
                                 * in the buffer. */
    void            (
        *expandProc)    (
        RbcParseValue * pvPtr,
        int needed);
    /* Procedure to call when space runs out;
     * it will make more space. */
    ClientData      clientData; /* Arbitrary information for use of
                                 * expandProc. */
} RbcParseValue;

/*
 * RbcParseVector --
 */
typedef struct {
    RbcVectorObject *vPtr;
    char            staticSpace[RBC_STATIC_STRING_SPACE];
    RbcParseValue   pv;         /* Used to hold a string value, if any. */
} RbcParseVector;

/*
 * RbcOp --
 *
 * 	Generic function prototype of CmdOptions.
 */
typedef int     (*RbcOp)         ();

/*
 * RbcOpSpec --
 *
 * 	Structure to specify a set of operations for a Tcl command.
 *      This is passed to the RbcGetOp procedure to look
 *      for a function pointer associated with the operation name.
 */
    typedef struct {
    const char     *name;       /* Name of operation */
    int             minChars;   /* Minimum # characters to disambiguate */
    RbcOp           proc;
    int             minArgs;    /* Minimum # args required */
    int             maxArgs;    /* Maximum # args required */
    const char     *usage;      /* Usage message */
} RbcOpSpec;

/*
 * RbcOpIndex --
 */
typedef enum {
    RBC_OP_ARG0,                /* Op is the first argument. */
    RBC_OP_ARG1,                /* Op is the second argument. */
    RBC_OP_ARG2,                /* Op is the third argument. */
    RBC_OP_ARG3,                /* Op is the fourth argument. */
    RBC_OP_ARG4                 /* Op is the fifth argument. */
} RbcOpIndex;

typedef int     (QSortCompareProc) (
    const void *,
    const void *);

/*
 * RbcDashes --
 *
 * 	List of dash values (maximum 11 based upon PostScript limit).
 */
typedef struct {
    char            values[12];
    int             offset;
} RbcDashes;
#define RbcLineIsDashed(d) ((d).values[0] != 0)

/*
 * RbcPoint2D --
 *
 *	2-D coordinate.
 */
typedef struct {
    double          x;
    double          y;
} RbcPoint2D;

/*
 * RbcPoint3D --
 *
 *	3-D coordinate.
 */
typedef struct {
    double          x;
    double          y;
    double          z;
} RbcPoint3D;

/*
 * RbcSegment2D --
 *
 *	2-D line segment.
 */
typedef struct {
    RbcPoint2D      p;       /* First end point of the segment. */
    RbcPoint2D      q;       /* Last end point of the segment. */
} RbcSegment2D;

/*
 * RbcDim2D --
 *
 *	2-D dimension.
 */
typedef struct {
    short int       width;
    short int       height;
} RbcDim2D;

/*
 * RbcRegion2D --
 *
 *      2-D region.  Used to copy parts of images.
 */
typedef struct {
    int             left;
    int             right;
    int             top;
    int             bottom;
} RbcRegion2D;

/*
 * RbcExtents2D --
 */
typedef struct {
    double          left;
    double          right;
    double          top;
    double          bottom;
} RbcExtents2D;

/*
 * RbcExtents3D --
 */
typedef struct {
    double          left;
    double          right;
    double          top;
    double          bottom;
    double          front;
    double          back;
} RbcExtents3D;

/* int RbcPointInRegion(RbcRegion2D e, int x, int y) */
#define RbcPointInRegion(e,x,y) \
	(((x) <= (e)->right) && ((x) >= (e)->left) && \
	 ((y) <= (e)->bottom) && ((y) >= (e)->top))

/*
 * RbcColorPair --
 *
 *	Holds a pair of foreground, background colors.
 */
typedef struct {
    XColor         *fgColor, *bgColor;
} RbcColorPair;

typedef void    (
    RbcTileChangedProc) (
    ClientData clientData,
    RbcTile tile);

/*
 * RbcPix32 --
 *
 *      A union representing either a pixel as a RGB triplet or a
 *	single word value.
 */
typedef union {
    unsigned int    value;      /* Lookup table address */
    struct RGBA {
        unsigned char   red;    /* Red intensity 0..255 */
        unsigned char   green;  /* Green intensity 0.255 */
        unsigned char   blue;   /* Blue intensity 0..255 */
        unsigned char   alpha;  /* Alpha-channel for compositing. 0..255 */
    } rgba;
    unsigned char   channel[4];
} RbcPix32;

/*
 * RbcColorImage --
 *
 *      The structure below represents a color image.  Each pixel
 *	occupies a 32-bit word of memory: one byte for each of the
 *	red, green, and blue color intensities, and another for
 *	alpha-channel image compositing (e.g. transparency).
 */
typedef struct RbcColorImage {
    int             width;      /* Dimensions of the image */
    int             height;     /* Dimensions of the image */
    RbcPix32       *bits;       /* Array of pixels representing the image. */
} RbcColorImage;

/*
 * ResampleFilterProc --
 *
 *      A function implementing a 1-D filter.
 */
typedef double  (
    ResampleFilterProc) (
    double value);

/*
 * RbcFilter2D --
 *
 *      Defines a convolution mask for a 2-D filter.  Used to smooth or
 *	enhance images.
 */
typedef struct {
    double          support;    /* Radius of filter */
    double          sum, scale; /* Sum of kernel */
    double         *kernel;     /* Array of values (malloc-ed) representing
                                 * the discrete 2-D filter. */
} RbcFilter2D;

/*
 * RbcPsColorMode --
 */
typedef enum {
    PS_MODE_MONOCHROME,         /* Only black and white. */
    PS_MODE_GREYSCALE,          /* Color converted to greyscale. */
    PS_MODE_COLOR               /* Full color */
} RbcPsColorMode;

/*
 * RbcPsToken --
 */
typedef struct RbcPsToken {
    Tcl_Interp     *interp;     /* Interpreter to report errors to. */
    Tk_Window       tkwin;      /* Tk_Window used to get font and color
                                 * information */
    Tcl_DString     dString;    /* Dynamic string used to contain the
                                 * PostScript generated. */
    char           *fontVarName;        /* Name of a Tcl array variable to convert
                                         * X font names to PostScript fonts. */
    char           *colorVarName;       /* Name of a Tcl array variable to convert
                                         * X color names to PostScript. */
    RbcPsColorMode  colorMode;  /* Mode: color or greyscale */
    /*
     * Utility space for building strings.  Currently used to create
     * PostScript output for the "postscript" command.
     */
    char            scratchArr[BUFSIZ*2];
} RbcPsToken;

/*
 * RbxAxisRange --
 *
 *	Designates a range of values by a minimum and maximum limit.
 */
typedef struct {
    double          min;
    double          max;
    double          range;
    double          scale;
} RbcAxisRange;

/*
 * RbcTicks --
 *
 * 	Structure containing information where the ticks (major or
 *	minor) will be displayed on the graph.
 */
typedef struct {
    int             nTicks;     /* # of ticks on axis */
    double          values[1];  /* Array of tick values (malloc-ed). */
} RbcTicks;

/*
 * RbcTickSweep --
 *
 * 	Structure containing information where the ticks (major or
 *	minor) will be displayed on the graph.
 */
typedef struct {
    double          initial;    /* Initial value */
    double          step;       /* Size of interval */
    int             nSteps;     /* Number of intervals. */
} RbcTickSweep;

/*
 * RbcAxis --
 *
 * 	Structure contains options controlling how the axis will be
 * 	displayed.
 */
typedef struct {
    char           *name;       /* Identifier to refer the element.
                                 * Used in the "insert", "delete", or
                                 * "show", commands. */
    RbcUid          classUid;   /* Type of axis. */
    RbcGraph       *graphPtr;   /* Graph widget of element */
    unsigned int    flags;      /* Set bit field definitions below */
    /*
     * AXIS_DRAWN               Axis is designated as a logical axis
     * AXIS_DIRTY
     *
     * AXIS_CONFIG_MAJOR        User specified major ticks.
     * AXIS_CONFIG_MINOR        User specified minor ticks.
     */
    char          **tags;
    const char     *detail;
    int             deletePending;      /* Indicates that the axis was
                                         * scheduled for deletion. The actual
                                         * deletion may be deferred until the
                                         * axis is no longer in use.  */
    int             refCount;   /* Number of elements referencing this
                                 * axis. */
    Tcl_HashEntry  *hashPtr;    /* Points to axis entry in hash
                                 * table. Used to quickly remove axis
                                 * entries. */
    int             logScale;   /* If non-zero, scale the axis values
                                 * logarithmically. */
    int             hidden;     /* If non-zero, don't display the
                                 * axis title, ticks, or line. */
    int             showTicks;  /* If non-zero, display tick marks and
                                 * labels. */
    int             descending; /* If non-zero, display the range of
                                 * values on the axis in descending
                                 * order, from high to low. */
    int             looseMin, looseMax; /* If non-zero, axis range extends to
                                         * the outer major ticks, otherwise at
                                         * the limits of the data values. This
                                         * is overriddened by setting the -min
                                         * and -max options.  */
    char           *title;      /* Title of the axis. */
    RbcTextStyle    titleTextStyle;     /* Text attributes (color, font,
                                         * rotation, etc.)  of the axis
                                         * title. */
    int             titleAlternate;     /* Indicates whether to position the
                                         * title above/left of the axis. */
    RbcPoint2D      titlePos;   /* Position of the title */
    unsigned short int titleWidth, titleHeight;
    int             lineWidth;  /* Width of lines representing axis
                                 * (including ticks).  If zero, then
                                 * no axis lines or ticks are
                                 * drawn. */
    const char    **limitsFormats;      /* One or two strings of sprintf-like
                                         * formats describing how to display
                                         * virtual axis limits. If NULL,
                                         * display no limits. */
    int             nFormats;
    RbcTextStyle    limitsTextStyle;    /* Text attributes (color, font,
                                         * rotation, etc.)  of the limits. */
    double          windowSize; /* Size of a sliding window of values
                                 * used to scale the axis automatically
                                 * as new data values are added. The axis
                                 * will always display the latest values
                                 * in this range. */
    double          shiftBy;    /* Shift maximum by this interval. */
    int             tickLength; /* Length of major ticks in pixels */
    RbcTextStyle    tickTextStyle;      /* Text attributes (color, font, rotation,
                                         * etc.) for labels at each major tick. */
    char           *formatCmd;  /* Specifies a Tcl command, to be invoked
                                 * by the axis whenever it has to generate
                                 * tick labels. */
    char           *scrollCmdPrefix;
    int             scrollUnits;
    double          min, max;   /* The actual axis range. */
    double          reqMin, reqMax;     /* Requested axis bounds. Consult the
                                         * axisPtr->flags field for
                                         * AXIS_CONFIG_MIN and AXIS_CONFIG_MAX
                                         * to see if the requested bound have
                                         * been set.  They override the
                                         * computed range of the axis
                                         * (determined by auto-scaling). */
    double          scrollMin, scrollMax;       /* Defines the scrolling reqion of the axis.
                                                 * Normally the region is determined from
                                                 * the data limits. If specified, these
                                                 * values override the data-range. */
    RbcAxisRange    valueRange; /* Range of data values of elements mapped
                                 * to this axis. This is used to auto-scale
                                 * the axis in "tight" mode. */
    RbcAxisRange    axisRange;  /* Smallest and largest major tick values
                                 * for the axis.  The tick values lie outside
                                 * the range of data values.  This is used to
                                 * auto-scale the axis in "loose" mode. */
    double          prevMin, prevMax;
    double          reqStep;    /* If > 0.0, overrides the computed major
                                 * tick interval.  Otherwise a stepsize
                                 * is automatically calculated, based
                                 * upon the range of elements mapped to the
                                 * axis. The default value is 0.0. */
    double          tickZoom;   /* If > 0.0, overrides the computed major
                                 * tick interval.  Otherwise a stepsize
                                 * is automatically calculated, based
                                 * upon the range of elements mapped to the
                                 * axis. The default value is 0.0. */
    GC              tickGC;     /* Graphics context for axis and tick labels */
    RbcTicks       *t1Ptr;      /* Array of major tick positions. May be
                                 * set by the user or generated from the
                                 * major sweep below. */
    RbcTicks       *t2Ptr;      /* Array of minor tick positions. May be
                                 * set by the user or generated from the
                                 * minor sweep below. */
    RbcTickSweep    minorSweep, majorSweep;
    int             reqNumMinorTicks;   /* If non-zero, represents the
                                         * requested the number of minor ticks
                                         * to be uniformally displayed along
                                         * each major tick. */
    int             labelOffset;        /* If non-zero, indicates that the tick
                                         * label should be offset to sit in the
                                         * middle of the next interval. */
    /* The following fields are specific to logical axes */
    RbcChainLink   *linkPtr;    /* Axis link in margin list. */
    RbcChain       *chainPtr;
    short int       width, height;      /* Extents of axis */
    RbcSegment2D   *segments;   /* Array of line segments representing
                                 * the major and minor ticks, but also
                                 * the axis line itself. The segment
                                 * coordinates are relative to the
                                 * axis. */
    int             nSegments;  /* Number of segments in the above array. */
    RbcChain       *tickLabels; /* Contains major tick label strings
                                 * and their offsets along the axis. */
    RbcRegion2D     region;
    Tk_3DBorder     border;
    int             borderWidth;
    int             relief;
} RbcAxis;

/*
 * RbcAxis2D --
 *
 *	The pair of axes mapping a point onto the graph.
 */
typedef struct {
    RbcAxis        *x;
    RbcAxis        *y;
} RbcAxis2D;

/*
* RbcElemWeight --
*
*	Designates a range of values by a minimum and maximum limit.
*/
typedef struct {
    double          min;
    double          max;
    double          range;
} RbcElemWeight;

/*
 * RbcPenStyle --
 *
 * An element has one or more vectors plus several attributes, such as
 * line style, thickness, color, and symbol type.  It has an
 * identifier which distinguishes it among the list of all elements.
 */
typedef struct {
    RbcElemWeight   weight;     /* Weight range where this pen is valid. */
    RbcPen         *penPtr;     /* Pen to use. */
    RbcSegment2D   *xErrorBars; /* Point to start of this pen's X-error bar
                                 * segments in the element's array. */
    RbcSegment2D   *yErrorBars; /* Point to start of this pen's Y-error bar
                                 * segments in the element's array. */
    int             xErrorBarCnt;       /* # of error bars for this pen. */
    int             yErrorBarCnt;       /* # of error bars for this pen. */
    int             errorBarCapWidth;   /* Length of the cap ends on each
                                         * error bar. */
    int             symbolSize; /* Size of the pen's symbol scaled to
                                 * the current graph size. */
} RbcPenStyle;

typedef struct {
    int             halo;       /* Maximal distance a candidate point
                                 * can be from the sample window
                                 * coordinate */
    int             mode;       /* Indicates whether to find the closest
                                 * data point or the closest point on the
                                 * trace by interpolating the line segments.
                                 * Can also be SEARCH_AUTO, indicating to
                                 * choose how to search.*/
    int             x, y;       /* Screen coordinates of test point */
    int             along;      /* Indicates to let search run along a
                                 * particular axis: x, y, or both. */
    /* Output */
    RbcElement     *elemPtr;    /* Name of the closest element */
    RbcPoint2D      point;      /* Graph coordinates of closest point */
    int             index;      /* Index of closest data point */
    double          dist;       /* Distance in screen coordinates */
} RbcClosestSearch;

typedef void    (
    RbcElementDrawProc) (
    RbcGraph * graphPtr,
    Drawable drawable,
    RbcElement * elemPtr);
typedef void    (
    RbcElementToPostScriptProc) (
    RbcGraph * graphPtr,
    RbcPsToken * psToken,
    RbcElement * elemPtr);
typedef void    (
    RbcElementDestroyProc) (
    RbcGraph * graphPtr,
    RbcElement * elemPtr);
typedef int     (
    RbcElementConfigProc) (
    RbcGraph * graphPtr,
    RbcElement * elemPtr);
typedef void    (
    RbcElementMapProc) (
    RbcGraph * graphPtr,
    RbcElement * elemPtr);
typedef void    (
    RbcElementExtentsProc) (
    RbcElement * elemPtr,
    RbcExtents2D * extsPtr);
typedef void    (
    RbcElementClosestProc) (
    RbcGraph * graphPtr,
    RbcElement * elemPtr,
    RbcClosestSearch * searchPtr);
typedef void    (
    RbcElementDrawSymbolProc) (
    RbcGraph * graphPtr,
    Drawable drawable,
    RbcElement * elemPtr,
    int x,
    int y,
    int symbolSize);
typedef void    (
    RbcElementSymbolToPostScriptProc) (
    RbcGraph * graphPtr,
    RbcPsToken * psToken,
    RbcElement * elemPtr,
    double x,
    double y,
    int symSize);

/*
 * RbcElementProcs --
 */
typedef struct {
    RbcElementClosestProc *closestProc;
    RbcElementConfigProc *configProc;
    RbcElementDestroyProc *destroyProc;
    RbcElementDrawProc *drawActiveProc;
    RbcElementDrawProc *drawNormalProc;
    RbcElementDrawSymbolProc *drawSymbolProc;
    RbcElementExtentsProc *extentsProc;
    RbcElementToPostScriptProc *printActiveProc;
    RbcElementToPostScriptProc *printNormalProc;
    RbcElementSymbolToPostScriptProc *printSymbolProc;
    RbcElementMapProc *mapProc;
} RbcElementProcs;

/*
 * RbcElemVector --
 *
 * The data structure below contains information pertaining to a line
 * vector.  It consists of an array of floating point data values and
 * for convenience, the number and minimum/maximum values.
 */
typedef struct {
    RbcVector      *vecPtr;
    double         *valueArr;
    int             nValues;
    int             arraySize;
    double          min, max;
    RbcVectorId     clientId;   /* If non-NULL, a client token identifying the
                                 * external vector. */
    RbcElement     *elemPtr;    /* Element associated with vector. */
} RbcElemVector;

/*
 * RbcElement --
 */
typedef struct RbcElement {
    char           *name;       /* Identifier to refer the element.
                                 * Used in the "insert", "delete", or
                                 * "show", commands. */
    RbcUid          classUid;   /* Type of element */
    RbcGraph       *graphPtr;   /* Graph widget of element */
    unsigned int    flags;      /* Indicates if the entire element is
                                 * active, or if coordinates need to
                                 * be calculated */
    char          **tags;
    int             hidden;     /* If non-zero, don't display the element. */
    Tcl_HashEntry  *hashPtr;
    char           *label;      /* Label displayed in legend */
    int             labelRelief;        /* Relief of label in legend. */
    RbcAxis2D       axes;       /* X-axis and Y-axis mapping the element */
    RbcElemVector   x, y, w;    /* Contains array of floating point
                                 * graph coordinate values. Also holds
                                 * min/max and the number of
                                 * coordinates */
    RbcElemVector   xError;     /* Relative/symmetric X error values. */
    RbcElemVector   yError;     /* Relative/symmetric Y error values. */
    RbcElemVector   xHigh, xLow;        /* Absolute/asymmetric X-coordinate high/low
                                           error values. */
    RbcElemVector   yHigh, yLow;        /* Absolute/asymmetric Y-coordinate high/low
                                           error values. */
    int            *activeIndices;      /* Array of indices (malloc-ed) which
                                         * indicate which data points are
                                         * active (drawn with "active"
                                         * colors). */
    int             nActiveIndices;     /* Number of active data points.
                                         * Special case: if nActiveIndices < 0
                                         * and the active bit is set in
                                         * "flags", then all data points are
                                         * drawn active. */
    RbcElementProcs *procsPtr;
    Tk_ConfigSpec  *specsPtr;   /* Configuration specifications. */
    RbcSegment2D   *xErrorBars; /* Point to start of this pen's X-error bar
                                 * segments in the element's array. */
    RbcSegment2D   *yErrorBars; /* Point to start of this pen's Y-error bar
                                 * segments in the element's array. */
    int             xErrorBarCnt;       /* # of error bars for this pen. */
    int             yErrorBarCnt;       /* # of error bars for this pen. */
    int            *xErrorToData;       /* Maps error bar segments back to the data
                                         * point. */
    int            *yErrorToData;       /* Maps error bar segments back to the data
                                         * point. */
    int             errorBarCapWidth;   /* Length of cap on error bars */
    RbcPen         *activePenPtr;       /* Standard Pens */
    RbcPen         *normalPenPtr;
    RbcChain       *palette;    /* Palette of pens. */
    /* Symbol scaling */
    int             scaleSymbols;       /* If non-zero, the symbols will scale
                                         * in size as the graph is zoomed
                                         * in/out.  */
    double          xRange, yRange;     /* Initial X-axis and Y-axis ranges:
                                         * used to scale the size of element's
                                         * symbol. */
    int             state;
} RbcElement;

/*
 * RbcFreqInfo --
 */
typedef struct {
    int             freq;       /* Number of occurrences of x-coordinate */
    RbcAxis2D       axes;       /* Indicates which x and y axis are mapped to
                                 * the x-value */
    double          sum;        /* Sum of the ordinates of each duplicate
                                 * abscissa */
    int             count;
    double          lastY;

} RbcFreqInfo;

/*
 * RbcBarMode --
 *
 *	Bar elements are displayed according to their x-y coordinates.
 *	If two bars have the same abscissa (x-coordinate), the bar
 *	segments will be drawn according to one of the following
 *	modes:
 */
typedef enum {
    MODE_INFRONT,               /* Each successive segment is drawn in
                                 * front of the previous. */
    MODE_STACKED,               /* Each successive segment is drawn
                                 * stacked above the previous. */
    MODE_ALIGNED,               /* Each successive segment is drawn
                                 * aligned to the previous from
                                 * right-to-left. */
    MODE_OVERLAP                /* Like "aligned", each successive segment
                                 * is drawn from right-to-left. In addition
                                 * the segments will overlap each other
                                 * by a small amount */
} RbcBarMode;

typedef RbcPen *(
    PenCreateProc)  (
    void);
typedef int     (
    PenConfigureProc) (
    RbcGraph * graphPtr,
    RbcPen * penPtr);
typedef void    (
    PenDestroyProc) (
    RbcGraph * graphPtr,
    RbcPen * penPtr);

/*
 * RbcPen --
 */
typedef struct RbcPen {
    char           *name;       /* Pen style identifier.  If NULL pen
                                 * was statically allocated. */
    RbcUid          classUid;   /* Type of pen */
    char           *typeId;     /* String token identifying the type of pen */
    unsigned int    flags;      /* Indicates if the pen element is active or
                                 * normal */
    int             refCount;   /* Reference count for elements using
                                 * this pen. */
    Tcl_HashEntry  *hashPtr;

    Tk_ConfigSpec  *configSpecs;        /* Configuration specifications */

    PenConfigureProc *configProc;
    PenDestroyProc *destroyProc;

} RbcPen;

/*
 * RbcPostScript --
 *
 * 	Structure contains information specific to the outputting of
 *	PostScript commands to print the graph.
 *
 */
typedef struct {
    /* User configurable fields */

    int             decorations;        /* If non-zero, print graph with
                                         * color background and 3D borders */

    int             reqWidth, reqHeight;        /* If greater than zero, represents the
                                                 * requested dimensions of the printed graph */
    int             reqPaperWidth;
    int             reqPaperHeight;     /* Requested dimensions for the PostScript
                                         * page. Can constrain the size of the graph
                                         * if the graph (plus padding) is larger than
                                         * the size of the page. */
    RbcPad          padX, padY; /* Requested padding on the exterior of the
                                 * graph. This forms the bounding box for
                                 * the page. */
    RbcPsColorMode  colorMode;  /* Selects the color mode for PostScript page
                                 * (0=monochrome, 1=greyscale, 2=color) */
    char           *colorVarName;       /* If non-NULL, is the name of a Tcl array
                                         * variable containing X to PostScript color
                                         * translations */
    char           *fontVarName;        /* If non-NULL, is the name of a Tcl array
                                         * variable containing X to PostScript font
                                         * translations */
    int             landscape;  /* If non-zero, orient the page 90 degrees */
    int             center;     /* If non-zero, center the graph on the page */
    int             maxpect;    /* If non-zero, indicates to scale the graph
                                 * so that it fills the page (maintaining the
                                 * aspect ratio of the graph) */
    int             addPreview; /* If non-zero, generate a preview image and
                                 * add it to the PostScript output */
    int             footer;     /* If non-zero, a footer with the title, date
                                 * and user will be added to the PostScript
                                 * output outside of the bounding box. */
    int             previewFormat;      /* Format of EPS preview:
                                         * PS_PREVIEW_WMF, PS_PREVIEW_EPSI, or
                                         * PS_PREVIEW_TIFF. */

    /* Computed fields */

    int             left, bottom;       /* Bounding box of PostScript plot. */
    int             right, top;

    double          pageScale;  /* Scale of page. Set if "-maxpect" option
                                 * is set, otherwise 1.0. */
} RbcPostScript;

/*
 * RbcGrid --
 *
 *	Contains attributes of describing how to draw grids (at major
 *	ticks) in the graph.  Grids may be mapped to either/both x and
 *	y axis.
 */
typedef struct {
    GC              gc;         /* Graphics context for the grid. */
    RbcAxis2D       axes;
    int             hidden;     /* If non-zero, grid isn't displayed. */
    int             minorGrid;  /* If non-zero, draw grid line for minor
                                 * axis ticks too */
    RbcDashes       dashes;     /* Dashstyle of the grid. This represents
                                 * an array of alternatingly drawn pixel
                                 * values. */
    int             lineWidth;  /* Width of the grid lines */
    XColor         *colorPtr;   /* Color of the grid lines */

    struct GridSegments {
        RbcSegment2D   *segments;       /* Array of line segments representing the
                                         * x or y grid lines */
        int             nSegments;      /* # of axis segments. */
    } x            ,
                    y;

} RbcGrid;

/*
 * RbcMargin --
 */
typedef struct {
    short int       width, height;      /* Extents of the margin */

    short int       axesOffset;
    short int       axesTitleLength;    /* Width of the widest title to be shown.
                                         * Multiple titles are displayed in
                                         * another margin. This is the minimum
                                         * space requirement. */
    unsigned int    nAxes;      /* Number of axes to be displayed */
    RbcChain       *axes;       /* Extra axes associated with this margin */

    char           *varName;    /* If non-NULL, name of variable to be
                                 * updated when the margin size changes */

    int             reqSize;    /* Requested size of margin */
    int             site;       /* Indicates where margin is located:
                                 * left/right/top/bottom. */
} RbcMargin;

/*
 * RbcGraph --
 *
 *	Top level structure containing everything pertaining to
 *	the graph.
 */
typedef struct RbcGraph {
    unsigned int    flags;      /* Flags;  see below for definitions. */
    Tcl_Interp     *interp;     /* Interpreter associated with graph */
    Tk_Window       tkwin;      /* Window that embodies the graph.  NULL
                                 * means that the window has been
                                 * destroyed but the data structures
                                 * haven't yet been cleaned up. */
    Display        *display;    /* Display containing widget; needed,
                                 * among other things, to release
                                 * resources after tkwin has already gone
                                 * away. */
    Tcl_Command     cmdToken;   /* Token for graph's widget command. */
    char           *data;       /* This value isn't used in C code.
                                 * It may be used in Tcl bindings to
                                 * associate extra data. */
    Tk_Cursor       cursor;
    int             inset;      /* Sum of focus highlight and 3-D
                                 * border.  Indicates how far to
                                 * offset the graph from outside
                                 * edge of the window. */
    int             borderWidth;        /* Width of the exterior border */
    int             relief;     /* Relief of the exterior border */
    Tk_3DBorder     border;     /* 3-D border used to delineate the plot
                                 * surface and outer edge of window */
    int             highlightWidth;     /* Width in pixels of highlight to draw
                                         * around widget when it has the focus.
                                         * <= 0 means don't draw a highlight. */
    XColor         *highlightBgColor;   /* Color for drawing traversal highlight
                                         * area when highlight is off. */
    XColor         *highlightColor;     /* Color for drawing traversal highlight. */
    char           *title;
    short int       titleX, titleY;
    RbcTextStyle    titleTextStyle;     /* Graph title */
    char           *takeFocus;
    int             reqWidth, reqHeight;        /* Requested size of graph window */
    int             width, height;      /* Size of graph window or PostScript
                                         * page */
    Tcl_HashTable   penTable;   /* Table of pens */
    struct Component {
        Tcl_HashTable   table;  /* Hash table of ids. */
        RbcChain       *displayList;    /* Display list. */
        Tcl_HashTable   tagTable;       /* Table of bind tags. */
    } elements     ,
                    markers,
                    axes;
    RbcUid          classUid;   /* Default element type */
    RbcBindTable   *bindTable;
    int             nextMarkerId;       /* Tracks next marker identifier available */
    RbcChain       *axisChain[4];       /* Chain of axes for each of the
                                         * margins.  They're separate from the
                                         * margin structures to make it easier
                                         * to invert the X-Y axes by simply
                                         * switching chain pointers.
                                         */
    RbcMargin          margins[4];
    RbcPostScript  *postscript; /* PostScript options: see rbcGrPS.c */
    RbcLegend      *legend;     /* Legend information: see rbcGrLegd.c */
    RbcCrosshairs  *crosshairs; /* Crosshairs information: see rbcGrHairs.c */
    RbcGrid        *gridPtr;    /* Grid attribute information */
    int             halo;       /* Maximum distance allowed between points
                                 * when searching for a point */
    int             inverted;   /* If non-zero, indicates the x and y axis
                                 * positions should be inverted. */
    RbcTile         tile;
    GC              drawGC;     /* Used for drawing on the margins. This
                                 * includes the axis lines */
    GC              fillGC;     /* Used to fill the background of the
                                 * margins. The fill is governed by
                                 * the background color or the tiled
                                 * pixmap. */
    int             plotBorderWidth;    /* Width of interior 3-D border. */
    int             plotRelief; /* 3-d effect: TK_RELIEF_RAISED etc. */
    XColor         *plotBg;     /* Color of plotting surface */
    GC              plotFillGC; /* Used to fill the plotting area with a
                                 * solid background color. The fill color
                                 * is stored in "plotBg". */
    /* If non-zero, force plot to conform to aspect ratio W/H */
    double          aspect;
    short int       left, right;        /* Coordinates of plot bbox */
    short int       top, bottom;
    RbcPad          padX;       /* Vertical padding for plotarea */
    int             vRange, vOffset;    /* Vertical axis range and offset from the
                                         * left side of the graph window. Used to
                                         * transform coordinates to vertical
                                         * axes. */
    RbcPad          padY;       /* Horizontal padding for plotarea */
    int             hRange, hOffset;    /* Horizontal axis range and offset from
                                         * the top of the graph window. Used to
                                         * transform horizontal axes */
    double          vScale, hScale;
    int             doubleBuffer;       /* If non-zero, draw the graph into a pixmap
                                         * first to reduce flashing. */
    int             backingStore;       /* If non-zero, cache elements by drawing
                                         * them into a pixmap */
    Pixmap          backPixmap; /* Pixmap used to cache elements
                                 * displayed.  If *backingStore* is
                                 * non-zero, each element is drawn
                                 * into this pixmap before it is
                                 * copied onto the screen.  The pixmap
                                 * then acts as a cache (only the
                                 * pixmap is redisplayed if the none
                                 * of elements have changed). This is
                                 * done so that markers can be redrawn
                                 * quickly over elements without
                                 * redrawing each element. */
    int             backWidth, backHeight;      /* Size of element backing store pixmap. */
    /*
     * barchart specific information
     */
    double          baseline;   /* Baseline from bar chart.  */
    double          barWidth;   /* Default width of each bar in graph units.
                                 * The default width is 1.0 units. */
    RbcBarMode      mode;       /* Mode describing how to display bars
                                 * with the same x-coordinates. Mode can
                                 * be "stack", "align", or "normal" */
    RbcFreqInfo    *freqArr;    /* Contains information about duplicate
                                 * x-values in bar elements (malloc-ed).
                                 * This information can also be accessed
                                 * by the frequency hash table */
    Tcl_HashTable   freqTable;  /* */
    int             nStacks;    /* Number of entries in frequency array.
                                 * If zero, indicates nothing special needs
                                 * to be done for "stack" or "align" modes */
    char           *dataCmd;    /* New data callback? */
} RbcGraph;

typedef         ClientData(
    MakeTagProc)    (
    RbcGraph * graphPtr,
    const char *tagName);

typedef int     (
    RbcSwitchParseProc) (
    ClientData clientData,
    Tcl_Interp * interp,
    char *switchName,
    char *value,
    char *record,
    int offset);

typedef void    (
    RbcSwitchFreeProc) (
    char *ptr);

/*
 * RbcSwitchCustom --
 */
typedef struct {
    RbcSwitchParseProc *parseProc;      /* Procedure to parse a switch value
                                         * and store it in its converted
                                         * form in the data record. */
    RbcSwitchFreeProc *freeProc;        /* Procedure to free a switch. */
    ClientData      clientData; /* Arbitrary one-word value
                                 * used by switch parser,
                                 * passed to parseProc. */
} RbcSwitchCustom;

/*
 * Type values for RbcSwitchSpec structures.  See the user
 * documentation for details.
 */
typedef enum {
    RBC_SWITCH_BOOLEAN, RBC_SWITCH_INT, RBC_SWITCH_INT_POSITIVE,
    RBC_SWITCH_INT_NONNEGATIVE, RBC_SWITCH_DOUBLE, RBC_SWITCH_STRING,
    RBC_SWITCH_LIST, RBC_SWITCH_FLAG, RBC_SWITCH_VALUE, RBC_SWITCH_CUSTOM,
    RBC_SWITCH_END
} RbcSwitchTypes;

typedef struct {
    RbcSwitchTypes  type;       /* Type of option, such as RBC_SWITCH_DOUBLE;
                                 * see definitions above.  Last option in
                                 * table must have type RBC_SWITCH_END. */
    const char     *switchName; /* Switch used to specify option in argv.
                                 * NULL means this spec is part of a group. */
    int             offset;     /* Where in widget record to store value;
                                 * use RbcOffset macro to generate values
                                 * for this. */
    int             flags;      /* Any combination of the values defined
                                 * below. */
    RbcSwitchCustom *customPtr; /* If type is RBC_SWITCH_CUSTOM then this is
                                 * a pointer to info about how to parse and
                                 * print the option.  Otherwise it is
                                 * irrelevant. */
    int             value;
} RbcSwitchSpec;

/*
 * RbcResampleFilter --
 *
 *      Contains information about a 1-D filter (its support and
 *	the procedure implementing the filter).
 */
typedef struct {
    const char     *name;       /* Name of the filter */
    ResampleFilterProc *proc;   /* 1-D filter procedure. */
    double          support;    /* Width of 1-D filter */
} RbcResampleFilter;

/*
 * Data declarations:
 */
extern double   rbcNaN;
extern RbcResampleFilter *rbcBoxFilterPtr;      /* The ubiquitous box filter */
extern RbcUid   rbcBarElementUid;
extern RbcUid   rbcLineElementUid;
extern RbcUid   rbcStripElementUid;
extern RbcUid   rbcLineMarkerUid;
extern RbcUid   rbcBitmapMarkerUid;
extern RbcUid   rbcImageMarkerUid;
extern RbcUid   rbcTextMarkerUid;
extern RbcUid   rbcPolygonMarkerUid;
extern RbcUid   rbcWindowMarkerUid;
extern RbcUid   rbcXAxisUid;
extern RbcUid   rbcYAxisUid;

/*
 * Inline function declarations:
 */

/* int RbcNumberOfPoints(RbcAxis2D e); */
#define RbcNumberOfPoints(e)	MIN((e)->x.nValues, (e)->y.nValues)

/* int RbcLineWidth(int w); */
#define RbcLineWidth(w)	(((w) > 1) ? (w) : 0)

/* int RbcPadding(RbcPad w); */
#define RbcPadding(x)	((x).side1 + (x).side2)

/* ClientData RbcGetCurrentItem(RbcBindTable *bindPtr); */
#define RbcGetCurrentItem(bindPtr)  ((bindPtr)->currentItem)
/* */
/* ClientData RbcGetBindingData(RbcBindTable *bindPtr); */
#define RbcGetBindingData(bindPtr)  ((bindPtr)->clientData)
/* int RbcChainGetLength(RbcChain *c); */
#define RbcChainGetLength(c)	(((c) == NULL) ? 0 : (c)->nLinks)
/* RbcChainLink *RbcChainFirstLink(RbcChain *c); */
#define RbcChainFirstLink(c)	(((c) == NULL) ? NULL : (c)->headPtr)
/* */
/* RbcChainLink *RbcChainLastLink(RbcChain *c); */
#define RbcChainLastLink(c)	(((c) == NULL) ? NULL : (c)->tailPtr)
/* RbcChainLink *RbcChainPrevLink(RbcChainLink *l); */
#define RbcChainPrevLink(l)	((l)->prevPtr)
/* RbcChainLink *RbcChainNextLink(RbcChainLink *l); */
#define RbcChainNextLink(l) 	((l)->nextPtr)
/* ClientData RbcChainGetValue(RbcChainLink *l); */
#define RbcChainGetValue(l)  	((l)->clientData)
/* void RbcChainSetValue(RbcChainLink *l, ClientData value); */
#define RbcChainSetValue(l, value) ((l)->clientData = (ClientData)(value))

/*
 * Function declarations:
 */

/* rbcBind.c */
MODULE_SCOPE int RbcConfigureBindings(
    Tcl_Interp * interp,
    RbcBindTable * table,
    ClientData item,
    int argc,
    const char **argv);
MODULE_SCOPE int RbcConfigureBindingsFromObj(
    Tcl_Interp * interp,
    RbcBindTable * table,
    ClientData item,
    int objc,
    Tcl_Obj * const *objv);
MODULE_SCOPE RbcBindTable *RbcCreateBindingTable(
    Tcl_Interp * interp,
    Tk_Window tkwin,
    ClientData clientData,
    RbcBindPickProc * pickProc);
MODULE_SCOPE void RbcDestroyBindingTable(
    RbcBindTable * table);
MODULE_SCOPE void RbcPickCurrentItem(
    RbcBindTable * table);
MODULE_SCOPE void RbcDeleteBindings(
    RbcBindTable * table,
    ClientData object);
MODULE_SCOPE void RbcMoveBindingTable(
    RbcBindTable * table,
    Tk_Window tkwin);

/* rbcChain,c */
MODULE_SCOPE RbcChain *RbcChainCreate(
    );
MODULE_SCOPE void RbcChainInit(
    RbcChain * chainPtr);
MODULE_SCOPE void RbcChainLinkAfter(
    RbcChain * chainPtr,
    RbcChainLink * linkPtr,
    RbcChainLink * afterLinkPtr);
MODULE_SCOPE void RbcChainLinkBefore(
    RbcChain * chainPtr,
    RbcChainLink * linkPtr,
    RbcChainLink * beforeLinkPtr);
MODULE_SCOPE RbcChainLink *RbcChainNewLink(
    void);
MODULE_SCOPE void RbcChainReset(
    RbcChain * chainPtr);
MODULE_SCOPE void RbcChainDestroy(
    RbcChain * chainPtr);
MODULE_SCOPE void RbcChainUnlinkLink(
    RbcChain * chainPtr,
    RbcChainLink * linkPtr);
MODULE_SCOPE void RbcChainDeleteLink(
    RbcChain * chainPtr,
    RbcChainLink * linkPtr);
MODULE_SCOPE RbcChainLink *RbcChainAppend(
    RbcChain * chainPtr,
    ClientData clientData);
MODULE_SCOPE RbcChainLink *RbcChainPrepend(
    RbcChain * chainPtr,
    ClientData clientData);
MODULE_SCOPE RbcChainLink *RbcChainAllocLink(
    unsigned int size);

/* rbcConfig.c */
MODULE_SCOPE int RbcGetPixels(
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *string,
    int check,
    int *valuePtr);
MODULE_SCOPE int RbcConfigModified(
    Tk_ConfigSpec * specs,
    ...);
MODULE_SCOPE int RbcConfigureWidgetComponent(
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *name,
    const char *class,
    const Tk_ConfigSpec * specs,
    int argc,
    const char **argv,
    char *widgRec,
    int flags);

/* rbcGraph.c */
MODULE_SCOPE void RbcEventuallyRedrawGraph(
    RbcGraph * graphPtr);
MODULE_SCOPE int RbcGraphInstCmdProc(
    ClientData clientData,
    Tcl_Interp * interp,
    int argc,
    const char *argv[]);
MODULE_SCOPE void RbcLayoutGraph(
    RbcGraph * graphPtr);
MODULE_SCOPE void RbcDrawGraph(
    RbcGraph * graphPtr,
    Drawable drawable,
    int backingStore);
MODULE_SCOPE int RbcGraphInit(
    Tcl_Interp * interp);
MODULE_SCOPE RbcGraph *RbcGetGraphFromWindowData(
    Tk_Window tkwin);
MODULE_SCOPE int RbcGraphType(
    RbcGraph * graphPtr);

/* rbcGrAxis.c */
MODULE_SCOPE double RbcInvHMap(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double x);
MODULE_SCOPE double RbcInvVMap(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double x);
MODULE_SCOPE double RbcHMap(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double x);
MODULE_SCOPE double RbcVMap(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    double y);
MODULE_SCOPE RbcPoint2D RbcMap2D(
    RbcGraph * graphPtr,
    double x,
    double y,
    RbcAxis2D * pairPtr);
MODULE_SCOPE RbcPoint2D RbcInvMap2D(
    RbcGraph * graphPtr,
    double x,
    double y,
    RbcAxis2D * pairPtr);
MODULE_SCOPE void RbcResetAxes(
    RbcGraph * graphPtr);
MODULE_SCOPE void RbcGetAxisSegments(
    RbcGraph * graphPtr,
    RbcAxis * axisPtr,
    RbcSegment2D ** segPtrPtr,
    int *nSegmentsPtr);
MODULE_SCOPE void RbcLayoutMargins(
    RbcGraph * graphPtr);
MODULE_SCOPE void RbcDestroyAxes(
    RbcGraph * graphPtr);
MODULE_SCOPE int RbcDefaultAxes(
    RbcGraph * graphPtr);
MODULE_SCOPE int RbcVirtualAxisOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
MODULE_SCOPE int RbcAxisOp(
    RbcGraph * graphPtr,
    int margin,
    int argc,
    const char **argv);
MODULE_SCOPE void RbcMapAxes(
    RbcGraph * graphPtr);
MODULE_SCOPE void RbcDrawAxes(
    RbcGraph * graphPtr,
    Drawable drawable);
MODULE_SCOPE void RbcAxesToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken);
MODULE_SCOPE void RbcDrawAxisLimits(
    RbcGraph * graphPtr,
    Drawable drawable);
MODULE_SCOPE void RbcAxisLimitsToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken);
MODULE_SCOPE RbcAxis *RbcGetFirstAxis(
    RbcChain * chainPtr);
MODULE_SCOPE RbcAxis *RbcNearestAxis(
    RbcGraph * graphPtr,
    int x,
    int y);
MODULE_SCOPE ClientData RbcMakeAxisTag(
    RbcGraph * graphPtr,
    const char *tagName);

/* rbcGrBar.c */
MODULE_SCOPE RbcPen *RbcBarPen(
    const char *penName);
MODULE_SCOPE RbcElement *RbcBarElement(
    RbcGraph * graphPtr,
    const char *name,
    RbcUid type);
MODULE_SCOPE void RbcInitFreqTable(
    RbcGraph * graphPtr);
MODULE_SCOPE void RbcComputeStacks(
    RbcGraph * graphPtr);
MODULE_SCOPE void RbcResetStacks(
    RbcGraph * graphPtr);

/* rbcGrElem.c */
MODULE_SCOPE double RbcFindElemVectorMinimum(
    RbcElemVector * vecPtr,
    double minLimit);
MODULE_SCOPE void RbcFreePalette(
    RbcGraph * graphPtr,
    RbcChain * palette);
MODULE_SCOPE int RbcStringToStyles(
    ClientData clientData,
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *string,
    char *widgRec,
    int offset);
MODULE_SCOPE const char *RbcStylesToString(
    ClientData clientData,
    Tk_Window tkwin,
    char *widgRec,
    int offset,
    Tcl_FreeProc ** freeProcPtr);
MODULE_SCOPE RbcPenStyle **RbcStyleMap(
    RbcElement * elemPtr);
MODULE_SCOPE void RbcMapErrorBars(
    RbcGraph * graphPtr,
    RbcElement * elemPtr,
    RbcPenStyle ** dataToStyle);
MODULE_SCOPE void RbcDestroyElements(
    RbcGraph * graphPtr);
MODULE_SCOPE void RbcMapElements(
    RbcGraph * graphPtr);
MODULE_SCOPE void RbcDrawElements(
    RbcGraph * graphPtr,
    Drawable drawable);
MODULE_SCOPE void RbcDrawActiveElements(
    RbcGraph * graphPtr,
    Drawable drawable);
MODULE_SCOPE void RbcElementsToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken);
MODULE_SCOPE void RbcActiveElementsToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken);
MODULE_SCOPE int RbcGraphUpdateNeeded(
    RbcGraph * graphPtr);
MODULE_SCOPE ClientData RbcMakeElementTag(
    RbcGraph * graphPtr,
    const char *tagName);
MODULE_SCOPE int RbcElementOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv,
    RbcUid classUid);

/* rbcGrGrid.c */
MODULE_SCOPE void RbcMapGrid(
    RbcGraph * graphPtr);
MODULE_SCOPE void RbcDrawGrid(
    RbcGraph * graphPtr,
    Drawable drawable);
MODULE_SCOPE void RbcGridToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken);
MODULE_SCOPE void RbcDestroyGrid(
    RbcGraph * graphPtr);
MODULE_SCOPE int RbcCreateGrid(
    RbcGraph * graphPtr);
MODULE_SCOPE int RbcGridOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);

/* rbcGrHairs.c */
MODULE_SCOPE void RbcConfigureCrosshairs(
    RbcGraph * graphPtr);
MODULE_SCOPE void RbcEnableCrosshairs(
    RbcGraph * graphPtr);
MODULE_SCOPE void RbcDisableCrosshairs(
    RbcGraph * graphPtr);
MODULE_SCOPE void RbcUpdateCrosshairs(
    RbcGraph * graphPtr);
MODULE_SCOPE void RbcDestroyCrosshairs(
    RbcGraph * graphPtr);
MODULE_SCOPE int RbcCreateCrosshairs(
    RbcGraph * graphPtr);
MODULE_SCOPE int RbcCrosshairsOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);

/* rbcGrLegd.c */
MODULE_SCOPE void RbcMapLegend(
    RbcLegend * legendPtr,
    int width,
    int height);
MODULE_SCOPE void RbcDrawLegend(
    RbcLegend * legendPtr,
    Drawable drawable);
MODULE_SCOPE void RbcLegendToPostScript(
    RbcLegend * legendPtr,
    RbcPsToken * psToken);
MODULE_SCOPE void RbcDestroyLegend(
    RbcGraph * graphPtr);
MODULE_SCOPE int RbcCreateLegend(
    RbcGraph * graphPtr);
MODULE_SCOPE int RbcLegendOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
MODULE_SCOPE int RbcLegendSite(
    RbcLegend * legendPtr);
MODULE_SCOPE int RbcLegendWidth(
    RbcLegend * legendPtr);
MODULE_SCOPE int RbcLegendHeight(
    RbcLegend * legendPtr);
MODULE_SCOPE int RbcLegendIsHidden(
    RbcLegend * legendPtr);
MODULE_SCOPE int RbcLegendIsRaised(
    RbcLegend * legendPtr);
MODULE_SCOPE int RbcLegendX(
    RbcLegend * legendPtr);
MODULE_SCOPE int RbcLegendY(
    RbcLegend * legendPtr);
MODULE_SCOPE void RbcLegendRemoveElement(
    RbcLegend * legendPtr,
    RbcElement * elemPtr);

/* rbcGrLine.c */
MODULE_SCOPE RbcPen *RbcLinePen(
    const char *penName);
MODULE_SCOPE RbcElement *RbcLineElement(
    RbcGraph *graphPtr,
    const char *name,
    RbcUid classUid);

/* rbcGrMarker.c */
MODULE_SCOPE ClientData RbcMakeMarkerTag(
    RbcGraph * graphPtr,
    const char *tagName);
MODULE_SCOPE int RbcMarkerOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
MODULE_SCOPE void RbcMarkersToPostScript(
    RbcGraph * graphPtr,
    RbcPsToken * psToken,
    int under);
MODULE_SCOPE void RbcDrawMarkers(
    RbcGraph * graphPtr,
    Drawable drawable,
    int under);
MODULE_SCOPE void RbcMapMarkers(
    RbcGraph * graphPtr);
MODULE_SCOPE void RbcDestroyMarkers(
    RbcGraph * graphPtr);
MODULE_SCOPE RbcMarker *RbcNearestMarker(
    RbcGraph * graphPtr,
    int x,
    int y,
    int under);

/* rbcGrMisc.c */
MODULE_SCOPE int RbcGetXY(
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *string,
    int *x,
    int *y);
MODULE_SCOPE void RbcFreeColorPair(
    RbcColorPair * pairPtr);
MODULE_SCOPE int RbcPointInSegments(
    RbcPoint2D * samplePtr,
    RbcSegment2D * segments,
    int nSegments,
    double halo);
MODULE_SCOPE int RbcPointInPolygon(
    RbcPoint2D * samplePtr,
    RbcPoint2D * screenPts,
    int nScreenPts);
MODULE_SCOPE int RbcRegionInPolygon(
    RbcExtents2D * extsPtr,
    RbcPoint2D * points,
    int nPoints,
    int enclosed);
MODULE_SCOPE void RbcGraphExtents(
    RbcGraph * graphPtr,
    RbcExtents2D * extsPtr);
MODULE_SCOPE int RbcLineRectClip(
    RbcExtents2D * extsPtr,
    RbcPoint2D * p,
    RbcPoint2D * q);
MODULE_SCOPE int RbcPolyRectClip(
    RbcExtents2D * extsPtr,
    RbcPoint2D * inputPts,
    int nInputPts,
    RbcPoint2D * outputPts);
MODULE_SCOPE RbcPoint2D RbcGetProjection(
    int x,
    int y,
    RbcPoint2D * p,
    RbcPoint2D * q);
MODULE_SCOPE int RbcAdjustViewport(
    int offset,
    int worldSize,
    int windowSize,
    int scrollUnits,
    int scrollMode);
MODULE_SCOPE int RbcGetScrollInfo(
    Tcl_Interp * interp,
    int argc,
    char **argv,
    int *offsetPtr,
    int worldSize,
    int windowSize,
    int scrollUnits,
    int scrollMode);
MODULE_SCOPE int RbcGetScrollInfoFromObj(
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const *objv,
    int *offsetPtr,
    int worldSize,
    int windowSize,
    int scrollUnits,
    int scrollMode);
MODULE_SCOPE void RbcUpdateScrollbar(
    Tcl_Interp * interp,
    char *scrollCmd,
    double firstFract,
    double lastFract);
MODULE_SCOPE GC RbcGetPrivateGCFromDrawable(
    Display * display,
    Drawable drawable,
    unsigned long gcMask,
    XGCValues * valuePtr);
MODULE_SCOPE GC RbcGetPrivateGC(
    Tk_Window tkwin,
    unsigned long gcMask,
    XGCValues * valuePtr);
MODULE_SCOPE void RbcFreePrivateGC(
    Display * display,
    GC gc);
MODULE_SCOPE void RbcSetDashes(
    Display * display,
    GC gc,
    RbcDashes * dashesPtr);
MODULE_SCOPE int RbcSimplifyLine(
    RbcPoint2D * origPts,
    int low,
    int high,
    double tolerance,
    int indices[]);
MODULE_SCOPE void RbcDraw2DSegments(
    Display * display,
    Drawable drawable,
    GC gc,
    RbcSegment2D * segments,
    int nSegments);
MODULE_SCOPE int RbcMaxRequestSize(
    Display * display,
    unsigned int elemSize);

/* rbcPen.c */
MODULE_SCOPE void RbcFreePen(
    RbcGraph * graphPtr,
    RbcPen * penPtr);
MODULE_SCOPE RbcPen *RbcCreatePen(
    RbcGraph * graphPtr,
    const char *penName,
    RbcUid classUid,
    int nOpts,
    const char **options);
MODULE_SCOPE int RbcGetPen(
    RbcGraph * graphPtr,
    const char *name,
    RbcUid classUid,
    RbcPen ** penPtrPtr);
MODULE_SCOPE void RbcDestroyPens(
    RbcGraph * graphPtr);
MODULE_SCOPE int RbcPenOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char **argv);

/* rbcGrPs.c */
MODULE_SCOPE void RbcDestroyPostScript(
    RbcGraph * graphPtr);
MODULE_SCOPE int RbcCreatePostScript(
    RbcGraph * graphPtr);
MODULE_SCOPE int RbcPostScriptOp(
    RbcGraph * graphPtr,
    Tcl_Interp * interp,
    int argc,
    const char *argv[]);

/* rbcImage.c */
MODULE_SCOPE RbcColorImage *RbcCreateColorImage(
    int width,
    int height);
MODULE_SCOPE void RbcFreeColorImage(
    RbcColorImage * image);
MODULE_SCOPE void RbcGammaCorrectColorImage(
    RbcColorImage * src,
    double newGamma);
MODULE_SCOPE void RbcColorImageToGreyscale(
    RbcColorImage * image);
MODULE_SCOPE void RbcColorImageToPhoto(
    Tcl_Interp * interp,
    RbcColorImage * image,
    Tk_PhotoHandle photo);
MODULE_SCOPE RbcColorImage *RbcPhotoRegionToColorImage(
    Tk_PhotoHandle photo,
    int x,
    int y,
    int width,
    int height);
MODULE_SCOPE RbcColorImage *RbcPhotoToColorImage(
    Tk_PhotoHandle photo);
MODULE_SCOPE int RbcGetResampleFilter(
    Tcl_Interp * interp,
    char *filterName,
    RbcResampleFilter ** filterPtrPtr);
MODULE_SCOPE RbcColorImage *RbcResampleColorImage(
    RbcColorImage * image,
    int destWidth,
    int destHeight,
    RbcResampleFilter * horzFilterPtr,
    RbcResampleFilter * vertFilterPtr);
MODULE_SCOPE void RbcResamplePhoto(
    Tcl_Interp * interp,
    Tk_PhotoHandle srcPhoto,
    int x,
    int y,
    int width,
    int height,
    Tk_PhotoHandle destPhoto,
    RbcResampleFilter * horzFilterPtr,
    RbcResampleFilter * vertFilterPtr);
MODULE_SCOPE void RbcResizePhoto(
    Tcl_Interp * interp,
    Tk_PhotoHandle srcPhoto,
    int x,
    int y,
    int width,
    int height,
    Tk_PhotoHandle destPhoto);
MODULE_SCOPE RbcColorImage *RbcResizeColorImage(
    RbcColorImage * src,
    int x,
    int y,
    int width,
    int height,
    int destWidth,
    int destHeight);
MODULE_SCOPE RbcColorImage *RbcResizeColorSubimage(
    RbcColorImage * src,
    int x,
    int y,
    int width,
    int height,
    int destWidth,
    int destHeight);
MODULE_SCOPE RbcColorImage *RbcConvolveColorImage(
    RbcColorImage * srcImage,
    RbcFilter2D * filter);
MODULE_SCOPE int RbcSnapPhoto(
    Tcl_Interp * interp,
    Tk_Window tkwin,
    Drawable drawable,
    int x,
    int y,
    int width,
    int height,
    int destWidth,
    int destHeight,
    const char *photoName,
    double inputGamma);
MODULE_SCOPE RbcColorImage *RbcRotateColorImage(
    RbcColorImage * image,
    double theta);
MODULE_SCOPE int RbcQuantizeColorImage(
    RbcColorImage * src,
    RbcColorImage * dest,
    int nColors);
MODULE_SCOPE RbcRegion2D *RbcSetRegion(
    int x,
    int y,
    int width,
    int height,
    RbcRegion2D * regionPtr);
MODULE_SCOPE Tk_Image RbcCreateTemporaryImage(
    Tcl_Interp * interp,
    Tk_Window tkwin,
    ClientData clientData);
MODULE_SCOPE int RbcDestroyTemporaryImage(
    Tcl_Interp * interp,
    Tk_Image tkImage);
MODULE_SCOPE const char *RbcNameOfImage(
    Tk_Image tkImage);
MODULE_SCOPE int RbcImageIsDeleted(
    Tk_Image tkImage);

/* rbcParse.c */
MODULE_SCOPE void RbcExpandParseValue(
    RbcParseValue * parsePtr,
    int needed);
MODULE_SCOPE int RbcParseNestedCmd(
    Tcl_Interp * interp,
    char *string,
    int flags,
    char **termPtr,
    RbcParseValue * parsePtr);
MODULE_SCOPE int RbcParseBraces(
    Tcl_Interp * interp,
    char *string,
    char **termPtr,
    RbcParseValue * parsePtr);
MODULE_SCOPE int RbcParseQuotes(
    Tcl_Interp * interp,
    char *string,
    int termChar,
    int flags,
    char **termPtr,
    RbcParseValue * parsePtr);

/* rbcPs.c */
MODULE_SCOPE RbcPsToken *RbcGetPsToken(
    Tcl_Interp * interp,
    Tk_Window tkwin);
MODULE_SCOPE void RbcReleasePsToken(
    RbcPsToken * psToken);
MODULE_SCOPE char *RbcPostScriptFromToken(
    RbcPsToken * psToken);
MODULE_SCOPE char *RbcScratchBufferFromToken(
    RbcPsToken * psToken);
MODULE_SCOPE void RbcAppendToPostScript(
    RbcPsToken * psToken,
    ...);
MODULE_SCOPE void RbcFormatToPostScript(
    RbcPsToken * psToken,
    ...);
MODULE_SCOPE int RbcFileToPostScript(
    RbcPsToken * psToken,
    const char *fileName);
MODULE_SCOPE void RbcBackgroundToPostScript(
    RbcPsToken * psToken,
    XColor * colorPtr);
MODULE_SCOPE void RbcForegroundToPostScript(
    RbcPsToken * psToken,
    XColor * colorPtr);
MODULE_SCOPE void RbcBitmapDataToPostScript(
    RbcPsToken * psToken,
    Display * display,
    Pixmap bitmap,
    int width,
    int height);
MODULE_SCOPE int RbcColorImageToPsData(
    RbcColorImage * image,
    int nComponents,
    Tcl_DString * resultPtr,
    const char *prefix);
MODULE_SCOPE void RbcClearBackgroundToPostScript(
    RbcPsToken * psToken);
MODULE_SCOPE void RbcLineWidthToPostScript(
    RbcPsToken * psToken,
    int lineWidth);
MODULE_SCOPE void RbcLineDashesToPostScript(
    RbcPsToken * psToken,
    RbcDashes * dashesPtr);
MODULE_SCOPE void RbcLineAttributesToPostScript(
    RbcPsToken * psToken,
    XColor * colorPtr,
    int lineWidth,
    RbcDashes * dashesPtr,
    int capStyle,
    int joinStyle);
MODULE_SCOPE void RbcRectangleToPostScript(
    RbcPsToken * psToken,
    double x,
    double y,
    int width,
    int height);
MODULE_SCOPE void RbcRegionToPostScript(
    RbcPsToken * psToken,
    double x,
    double y,
    int width,
    int height);
MODULE_SCOPE void RbcPathToPostScript(
    RbcPsToken * psToken,
    RbcPoint2D * screenPts,
    int nScreenPts);
MODULE_SCOPE void RbcPolygonToPostScript(
    RbcPsToken * psToken,
    RbcPoint2D * screenPts,
    int nScreenPts);
MODULE_SCOPE void RbcSegmentsToPostScript(
    RbcPsToken * psToken,
    XSegment * segArr,
    int nSegs);
MODULE_SCOPE void RbcRectanglesToPostScript(
    RbcPsToken * psToken,
    XRectangle * rectArr,
    int nRects);
MODULE_SCOPE void RbcDraw3DRectangleToPostScript(
    RbcPsToken * psToken,
    Tk_3DBorder border,
    double x,
    double y,
    int width,
    int height,
    int borderWidth,
    int relief);
MODULE_SCOPE void RbcFill3DRectangleToPostScript(
    RbcPsToken * psToken,
    Tk_3DBorder border,
    double x,
    double y,
    int width,
    int height,
    int borderWidth,
    int relief);
MODULE_SCOPE void RbcStippleToPostScript(
    RbcPsToken * psToken,
    Display * display,
    Pixmap bitmap);
MODULE_SCOPE void RbcColorImageToPostScript(
    RbcPsToken * psToken,
    RbcColorImage * image,
    double x,
    double y);
MODULE_SCOPE void RbcWindowToPostScript(
    RbcPsToken * psToken,
    Tk_Window tkwin,
    double x,
    double y);
MODULE_SCOPE void RbcPhotoToPostScript(
    RbcPsToken * psToken,
    Tk_PhotoHandle photoToken,
    double x,
    double y);
MODULE_SCOPE void RbcFontToPostScript(
    RbcPsToken * psToken,
    Tk_Font font);
MODULE_SCOPE void RbcTextToPostScript(
    RbcPsToken * psToken,
    char *string,
    RbcTextStyle * attrPtr,
    double x,
    double y);
MODULE_SCOPE void RbcLineToPostScript(
    RbcPsToken * psToken,
    XPoint * pointArr,
    int nPoints);
MODULE_SCOPE void RbcBitmapToPostScript(
    RbcPsToken * psToken,
    Display * display,
    Pixmap bitmap,
    double scaleX,
    double scaleY);
MODULE_SCOPE void Rbc2DSegmentsToPostScript(
    RbcPsToken * psToken,
    RbcSegment2D * segments,
    int nSegments);

/* rbcSpline.c */
MODULE_SCOPE int RbcQuadraticSpline(
    RbcPoint2D * origPts,
    int nOrigPts,
    RbcPoint2D * intpPts,
    int nIntpPts);
MODULE_SCOPE int RbcNaturalSpline(
    RbcPoint2D * origPts,
    int nOrigPts,
    RbcPoint2D * intpPts,
    int nIntpPts);
MODULE_SCOPE int RbcSplineInit(
    Tcl_Interp * interp);
MODULE_SCOPE int RbcNaturalParametricSpline(
    RbcPoint2D * origPts,
    int nOrigPts,
    RbcExtents2D * extsPtr,
    int isClosed,
    RbcPoint2D * intpPts,
    int nIntpPts);
MODULE_SCOPE int RbcCatromParametricSpline(
    RbcPoint2D * origPts,
    int nOrigPts,
    RbcPoint2D * intpPts,
    int nIntpPts);

/* rbcSwitch.c */
MODULE_SCOPE int RbcProcessSwitches(
    Tcl_Interp * interp,
    RbcSwitchSpec * specs,
    int argc,
    const char **argv,
    char *record,
    int flags);
MODULE_SCOPE int RbcProcessObjSwitches(
    Tcl_Interp * interp,
    RbcSwitchSpec * specPtr,
    int objc,
    Tcl_Obj * const *objv,
    char *record,
    int flags);
MODULE_SCOPE void RbcFreeSwitches(
    RbcSwitchSpec * specs,
    char *record,
    int flags);
MODULE_SCOPE int RbcSwitchChanged(
    RbcSwitchSpec * specs,
    ...);

/* rbcText.c */
MODULE_SCOPE RbcTextLayout *RbcGetTextLayout(
    char *string,
    RbcTextStyle * stylePtr);
MODULE_SCOPE void RbcGetTextExtents(
    RbcTextStyle * stylePtr,
    char *text,
    int *widthPtr,
    int *heightPtr);
MODULE_SCOPE void RbcGetBoundingBox(
    int width,
    int height,
    double theta,
    double *widthPtr,
    double *heightPtr,
    RbcPoint2D * points);
MODULE_SCOPE void RbcTranslateAnchor(
    int x,
    int y,
    int width,
    int height,
    Tk_Anchor anchor,
    int *transXPtr,
    int *transYPtr);
MODULE_SCOPE RbcPoint2D RbcTranslatePoint(
    RbcPoint2D * pointPtr,
    int width,
    int height,
    Tk_Anchor anchor);
MODULE_SCOPE void RbcInitTextStyle(
    RbcTextStyle * stylePtr);
MODULE_SCOPE void RbcSetDrawTextStyle(
    RbcTextStyle * stylePtr,
    Tk_Font font,
    GC gc,
    XColor * normalColor,
    XColor * activeColor,
    XColor * shadowColor,
    double theta,
    Tk_Anchor anchor,
    Tk_Justify justify,
    int leader,
    int shadowOffset);
MODULE_SCOPE void RbcSetPrintTextStyle(
    RbcTextStyle * stylePtr,
    Tk_Font font,
    XColor * fgColor,
    XColor * bgColor,
    XColor * shadowColor,
    double theta,
    Tk_Anchor anchor,
    Tk_Justify justify,
    int leader,
    int shadowOffset);
MODULE_SCOPE void RbcDrawTextLayout(
    Tk_Window tkwin,
    Drawable drawable,
    RbcTextLayout * textPtr,
    RbcTextStyle * stylePtr,
    int x,
    int y);
MODULE_SCOPE void RbcDrawText2(
    Tk_Window tkwin,
    Drawable drawable,
    char *string,
    RbcTextStyle * stylePtr,
    int x,
    int y,
    RbcDim2D * dimPtr);
MODULE_SCOPE void RbcDrawText(
    Tk_Window tkwin,
    Drawable drawable,
    char *string,
    RbcTextStyle * stylePtr,
    int x,
    int y);
MODULE_SCOPE GC RbcGetBitmapGC(
    Tk_Window tkwin);
MODULE_SCOPE void RbcResetTextStyle(
    Tk_Window tkwin,
    RbcTextStyle * stylePtr);
MODULE_SCOPE void RbcFreeTextStyle(
    Display * display,
    RbcTextStyle * stylePtr);

/* rbcTile.c */
MODULE_SCOPE int RbcGetTile(
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *imageName,
    RbcTile * tilePtr);
MODULE_SCOPE void RbcFreeTile(
    RbcTile tile);
MODULE_SCOPE const char *RbcNameOfTile(
    RbcTile tile);
Pixmap          RbcPixmapOfTile(
    RbcTile tile);
MODULE_SCOPE void RbcSizeOfTile(
    RbcTile tile,
    int *widthPtr,
    int *heightPtr);
MODULE_SCOPE void RbcSetTileChangedProc(
    RbcTile tile,
    RbcTileChangedProc * changeProc,
    ClientData clientData);
MODULE_SCOPE void RbcSetTileOrigin(
    Tk_Window tkwin,
    RbcTile tile,
    int x,
    int y);
MODULE_SCOPE void RbcSetTSOrigin(
    Tk_Window tkwin,
    RbcTile tile,
    int x,
    int y);
MODULE_SCOPE void RbcTilePolygon(
    Tk_Window tkwin,
    Drawable drawable,
    RbcTile tile,
    XPoint * pointArr,
    int nPoints);
MODULE_SCOPE void RbcTileRectangle(
    Tk_Window tkwin,
    Drawable drawable,
    RbcTile tile,
    int x,
    int y,
    unsigned int width,
    unsigned int height);
MODULE_SCOPE void RbcTileRectangles(
    Tk_Window tkwin,
    Drawable drawable,
    RbcTile tile,
    XRectangle * rectArr,
    int nRects);

/* rbcUtil.c */
MODULE_SCOPE void *RbcCalloc(
    unsigned int nElem,
    size_t size);
MODULE_SCOPE char *RbcStrdup(
    const char *ptr);
MODULE_SCOPE RbcUid RbcGetUid(
    const char *string);
MODULE_SCOPE void RbcFreeUid(
    RbcUid uid);
MODULE_SCOPE RbcUid RbcFindUid(
    char *string);
MODULE_SCOPE RbcOp RbcGetOp(
    Tcl_Interp * interp,
    int nSpecs,
    RbcOpSpec * specArr,
    int operPos,
    int argc,
    const char **argv,
    int flags);
MODULE_SCOPE RbcOp RbcGetOpFromObj(
    Tcl_Interp * interp,
    int nSpecs,
    RbcOpSpec * specArr,
    int operPos,
    int objc,
    Tcl_Obj * const *objv,
    int flags);

/* rbcVecMath.c */
MODULE_SCOPE void RbcVectorInstallMathFunctions(
    Tcl_HashTable * tablePtr);
MODULE_SCOPE void RbcVectorInstallSpecialIndices(
    Tcl_HashTable * tablePtr);
MODULE_SCOPE double RbcVecMin(
    RbcVector * vecPtr);
MODULE_SCOPE double RbcVecMax(
    RbcVector * vecPtr);
MODULE_SCOPE int RbcExprVector(
    Tcl_Interp * interp,
    char *string,
    RbcVector * vecPtr);

/* rbcVecObjCmd.c */
MODULE_SCOPE int RbcAppendOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcArithOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcBinreadOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcClearOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcDeleteOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcDupOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcExprOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcIndexOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcLengthOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcMergeOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcNormalizeOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcOffsetOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcPopulateOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcRandomOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcRangeOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcSearchOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcSeqOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcSetOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcSortOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcSplitOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int RbcVariableOp(
    RbcVectorObject * vPtr,
    Tcl_Interp * interp,
    int objc,
    Tcl_Obj * const objv[]);
MODULE_SCOPE int *RbcVectorSortIndex(
    RbcVectorObject ** vPtrPtr,
    int nVectors);

/* rbcVector.c */
MODULE_SCOPE double Rbcdrand48(
    void);
MODULE_SCOPE int RbcVectorInit(
    Tcl_Interp * interp);
MODULE_SCOPE RbcVectorInterpData *RbcVectorGetInterpData(
    Tcl_Interp * interp);
MODULE_SCOPE RbcVectorObject *RbcVectorNew(
    RbcVectorInterpData * dataPtr);
MODULE_SCOPE RbcVectorObject *RbcVectorCreate(
    RbcVectorInterpData * dataPtr,
    const char *vecName,
    const char *cmdName,
    const char *varName,
    int *newPtr);
MODULE_SCOPE void RbcVectorFree(
    RbcVectorObject * vPtr);
MODULE_SCOPE int RbcVectorDuplicate(
    RbcVectorObject * destPtr,
    RbcVectorObject * srcPtr);
MODULE_SCOPE void RbcVectorFlushCache(
    RbcVectorObject * vPtr);
MODULE_SCOPE int RbcVectorMapVariable(
    Tcl_Interp * interp,
    RbcVectorObject * vPtr,
    const char *name);
MODULE_SCOPE int RbcVectorReset(
    RbcVectorObject * vPtr,
    double *valueArr,
    int length,
    int size,
    Tcl_FreeProc * freeProc);
MODULE_SCOPE int RbcVectorNotifyPending(
    RbcVectorId clientId);
MODULE_SCOPE int RbcVectorChangeLength(
    RbcVectorObject * vPtr,
    int length);
MODULE_SCOPE int RbcVectorLookupName(
    RbcVectorInterpData * dataPtr,
    const char *vecName,
    RbcVectorObject ** vPtrPtr);
MODULE_SCOPE void RbcVectorUpdateRange(
    RbcVectorObject * vPtr);
MODULE_SCOPE int RbcVectorGetIndex(
    Tcl_Interp * interp,
    RbcVectorObject * vPtr,
    const char *string,
    int *indexPtr,
    int flags,
    RbcVectorIndexProc ** procPtrPtr);
MODULE_SCOPE int RbcVectorGetIndexRange(
    Tcl_Interp * interp,
    RbcVectorObject * vPtr,
    const char *string,
    int flags,
    RbcVectorIndexProc ** procPtrPtr);
RbcVectorObject *RbcVectorParseElement(
    Tcl_Interp * interp,
    RbcVectorInterpData * dataPtr,
    const char *start,
    char **endPtr,
    int flags);
MODULE_SCOPE void RbcVectorUpdateClients(
    RbcVectorObject * vPtr);
MODULE_SCOPE Tcl_Obj *RbcGetValues(
    RbcVectorObject * vPtr,
    int first,
    int last);
MODULE_SCOPE void RbcReplicateValue(
    RbcVectorObject * vPtr,
    int first,
    int last,
    double value);
MODULE_SCOPE int RbcGetDouble(
    Tcl_Interp * interp,
    Tcl_Obj * objPtr,
    double *valuePtr);
MODULE_SCOPE void RbcFreeVectorId(
    RbcVectorId clientId);
MODULE_SCOPE int RbcGetVectorById(
    Tcl_Interp * interp,
    RbcVectorId clientId,
    RbcVector ** vecPtrPtr);
MODULE_SCOPE int RbcVectorExists2(
    Tcl_Interp * interp,
    const char *vecName);
MODULE_SCOPE RbcVectorId RbcAllocVectorId(
    Tcl_Interp * interp,
    const char *vecName);
MODULE_SCOPE void RbcSetVectorChangedProc(
    RbcVectorId clientId,
    RbcVectorChangedProc * proc,
    ClientData clientData);
MODULE_SCOPE char *RbcNameOfVectorId(
    RbcVectorId clientId);
MODULE_SCOPE int RbcGetVector(
    Tcl_Interp * interp,
    const char *vecName,
    RbcVector ** vecPtrPtr);
MODULE_SCOPE int RbcCreateVector2(
    Tcl_Interp * interp,
    const char *vecName,
    const char *cmdName,
    const char *varName,
    int initialSize,
    RbcVector ** vecPtrPtr);
MODULE_SCOPE int RbcCreateVector(
    Tcl_Interp * interp,
    const char *vecName,
    int size,
    RbcVector ** vecPtrPtr);
MODULE_SCOPE int RbcResizeVector(
    RbcVector * vecPtr,
    int nValues);
MODULE_SCOPE char *RbcNameOfVector(
    RbcVector * vecPtr);
MODULE_SCOPE int RbcResetVector(
    RbcVector * vecPtr,
    double *dataArr,
    int nValues,
    int arraySize,
    Tcl_FreeProc * freeProc);

/* rbcWindow.c */
MODULE_SCOPE Tk_Window RbcFindChild(
    Tk_Window parent,
    char *name);
MODULE_SCOPE void RbcSetWindowInstanceData(
    Tk_Window tkwin,
    ClientData instanceData);
MODULE_SCOPE ClientData RbcGetWindowInstanceData(
    Tk_Window tkwin);
MODULE_SCOPE void RbcDeleteWindowInstanceData(
    Tk_Window tkwin);

/* rbcWinImage.c rbcUnixImage.c */
MODULE_SCOPE RbcColorImage *RbcDrawableToColorImage(
    Tk_Window tkwin,
    Drawable drawable,
    int x,
    int y,
    int width,
    int height,
    double inputGamma);
MODULE_SCOPE Pixmap RbcPhotoImageMask(
    Tk_Window tkwin,
    Tk_PhotoImageBlock src);
MODULE_SCOPE Pixmap RbcRotateBitmap(
    Tk_Window tkwin,
    Pixmap bitmap,
    int width,
    int height,
    double theta,
    int *widthPtr,
    int *heightPtr);
MODULE_SCOPE Pixmap RbcScaleBitmap(
    Tk_Window tkwin,
    Pixmap srcBitmap,
    int srcWidth,
    int srcHeight,
    int scaledWidth,
    int scaledHeight);
MODULE_SCOPE Pixmap RbcScaleRotateBitmapRegion(
    Tk_Window tkwin,
    Pixmap srcBitmap,
    unsigned int srcWidth,
    unsigned int srcHeight,
    int regionX,
    int regionY,
    unsigned int regionWidth,
    unsigned int regionHeight,
    unsigned int virtWidth,
    unsigned int virtHeight,
    double theta);

/* rbcScrollbar.c */
#include <tclOO.h>
MODULE_SCOPE int RbcScrollbarInit(
    Tcl_Interp *interp);
MODULE_SCOPE int RbcScrollbarConstructor(
    ClientData clientData,
	Tcl_Interp *interp,
    Tcl_ObjectContext objectContext,
    int objc,
    Tcl_Obj *const objv[]);
MODULE_SCOPE int RbcScrollbarDestructor(
    ClientData clientData,
	Tcl_Interp *interp,
    Tcl_ObjectContext objectContext,
    int objc,
    Tcl_Obj *const objv[]);
MODULE_SCOPE int RbcScrollbarActivate(
    ClientData clientData,
	Tcl_Interp *interp,
    Tcl_ObjectContext objectContext,
    int objc,
    Tcl_Obj *const objv[]);
MODULE_SCOPE int RbcScrollbarCget(
    ClientData clientData,
	Tcl_Interp *interp,
    Tcl_ObjectContext objectContext,
    int objc,
    Tcl_Obj *const objv[]);
MODULE_SCOPE int RbcScrollbarConfigure(
    ClientData clientData,
	Tcl_Interp *interp,
    Tcl_ObjectContext objectContext,
    int objc,
    Tcl_Obj *const objv[]);
MODULE_SCOPE int RbcScrollbarDelta(
    ClientData clientData,
	Tcl_Interp *interp,
    Tcl_ObjectContext objectContext,
    int objc,
    Tcl_Obj *const objv[]);
MODULE_SCOPE int RbcScrollbarFraction(
    ClientData clientData,
	Tcl_Interp *interp,
    Tcl_ObjectContext objectContext,
    int objc,
    Tcl_Obj *const objv[]);
MODULE_SCOPE int RbcScrollbarGet(
    ClientData clientData,
	Tcl_Interp *interp,
    Tcl_ObjectContext objectContext,
    int objc,
    Tcl_Obj *const objv[]);
MODULE_SCOPE int RbcScrollbarIdentify(
    ClientData clientData,
	Tcl_Interp *interp,
    Tcl_ObjectContext objectContext,
    int objc,
    Tcl_Obj *const objv[]);
MODULE_SCOPE int RbcScrollbarSet(
    ClientData clientData,
	Tcl_Interp *interp,
    Tcl_ObjectContext objectContext,
    int objc,
    Tcl_Obj *const objv[]);

/* Windows */
#ifdef _WIN32
#include "rbcWin.h"
#endif

#ifndef _WIN32
#define PurifyPrintf  printf
#endif /* _WIN32 */

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#endif /* _RBCINT */

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
