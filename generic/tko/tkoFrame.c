/*
 * tkoFrame.c --
 *
 *	This module implements "frame", "labelframe" and "toplevel" widgets
 *	for the Tk toolkit. Frames are windows with a background color and
 *	possibly a 3-D effect, but not much else in the way of attributes.
 *
 * Copyright (c) 1990-1994 The Regents of the University of California.
 * Copyright (c) 1994-1997 Sun Microsystems, Inc.
 * Copyright (c) 2019 Rene Zaumseil
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkoWidget.h"

 /*
  * The following enum is used to define the type of the frame.
  */
enum FrameType {
    TYPE_FRAME, TYPE_TOPLEVEL, TYPE_LABELFRAME
};

/*
 * tkoFrame --
 *
 * A data structure of the following type is kept for each
 * frame that currently exists for this process:
 */
typedef struct tkoFrame {
    Tk_Window *win;
    Tcl_Object object;
    Tcl_Interp *interp;
    Display *display;
    enum FrameType type;       /* Type of widget, such as TYPE_FRAME. */
    char *menuName;            /* Textual description of menu to use for
                                * menubar. Malloc-ed, may be NULL. */
    Colormap colormap;         /* If not None, identifies a colormap
                                * allocated for this window, which must be
                                * freed when the window is deleted. */
    Tk_3DBorder border;        /* Structure used to draw 3-D border and
                                * background. NULL means no background or
                                * border. */
    int borderWidth;           /* Width of 3-D border (if any). */
    int relief;                /* 3-d effect: TK_RELIEF_RAISED etc. */
    int highlightWidth;        /* Width in pixels of highlight to draw around
                                * widget when it has the focus. 0 means don't
                                * draw a highlight. */
    XColor *highlightBgColorPtr;
    /* Color for drawing traversal highlight area
     * when highlight is off. */
    XColor *highlightColorPtr; /* Color for drawing traversal highlight. */
    int width;                 /* Width to request for window. <= 0 means
                                * don't request any size. */
    int height;                /* Height to request for window. <= 0 means
                                * don't request any size. */
    Tk_Cursor cursor;          /* Current cursor for window, or None. */
    int isContainer;           /* 1 means this window is a container, 0 means
                                * that it isn't. */
    Tcl_Obj *useThis;          /* If the window is embedded, this points to
                                * the name of the window in which it is
                                * embedded (malloc'ed). For non-embedded
                                * windows this is NULL. */
    int flags;                 /* Various flags; see below for
                                * definitions. */
    int padX;                  /* Integer value corresponding to padXPtr. */
    int padY;                  /* Integer value corresponding to padYPtr. */
    unsigned int mask;
} tkoFrame;

/*
 * tkoLabelframe --
 *
 * A data structure of the following type is kept for each labelframe widget
 * managed by this file:
 */
typedef struct tkoLabelframe {
    tkoFrame frame;            /* A pointer to the generic frame structure.
                                * This must be the first element of the
                                * tkoLabelframe. */
    /*
     * tkoLabelframe specific configuration settings.
     */
    Tcl_Obj *textPtr;          /* Value of -text option: specifies text to
                                * display in button. */
    Tk_Font tkfont;            /* Value of -font option: specifies font to
                                * use for display text. */
    XColor *textColorPtr;      /* Value of -fg option: specifies foreground
                                * color in normal mode. */
    int labelAnchor;           /* Value of -labelanchor option: specifies
                                * where to place the label. */
    Tk_Window labelWin;        /* Value of -labelwidget option: Window to use
                                * as label for the frame. */
    /*
     * tkoLabelframe specific fields for use with configuration settings above.
     */
    GC  textGC;                /* GC for drawing text in normal mode. */
    Tk_TextLayout textLayout;  /* Stored text layout information. */
    XRectangle labelBox;       /* The label's actual size and position. */
    int labelReqWidth;         /* The label's requested width. */
    int labelReqHeight;        /* The label's requested height. */
    int labelTextX, labelTextY; /* Position of the text to be drawn. */
} tkoLabelframe;

/*
 * The following macros define how many extra pixels to leave around a label's
 * text.
 */
#define LABELSPACING 1
#define LABELMARGIN 4

 /*
  * Flag bits for frames:
  *
  * REDRAW_PENDING:             Non-zero means a DoWhenIdle handler has
  *                             already been queued to redraw this window.
  * GOT_FOCUS:                  Non-zero means this widget currently has the
  *                             input focus.
  */
#define REDRAW_PENDING		1
#define GOT_FOCUS		4

  /*
   * The following enum is used to define a type for the -labelanchor option of
   * the Labelframe widget. These values are used as indices into the string
   * table below.
   */
enum labelanchor {
    LABELANCHOR_E, LABELANCHOR_EN, LABELANCHOR_ES,
    LABELANCHOR_N, LABELANCHOR_NE, LABELANCHOR_NW,
    LABELANCHOR_S, LABELANCHOR_SE, LABELANCHOR_SW,
    LABELANCHOR_W, LABELANCHOR_WN, LABELANCHOR_WS
};

/*
* Methods
*/
static int FrameConstructorFrame(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int FrameConstructorLabelframe(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int FrameConstructorToplevel(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int FrameDestructor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int FrameMethod_tko_configure(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int FrameMethod_labelanchor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int FrameMethod_labelwidget(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);

/*
 * Functions
 */
static int FrameConstructor(
    int type,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static void FrameMetaDestroy(
    tkoFrame * frame);
static void
FrameMetaDelete(
    ClientData clientData)
{
    Tcl_EventuallyFree(clientData, (Tcl_FreeProc *) FrameMetaDestroy);
}

static void FrameComputeGeometry(
    tkoFrame * frame);
static void FrameDisplay(
    ClientData clientData);
static void FrameEventProc(
    ClientData clientData,
    XEvent * eventPtr);
static void FrameLostSlaveProc(
    ClientData clientData,
    Tk_Window tkWin);
static void FrameRequestProc(
    ClientData clientData,
    Tk_Window tkWin);
static void FrameStructureProc(
    ClientData clientData,
    XEvent * eventPtr);
static void FrameWorldChanged(
    ClientData instanceData);
static void FrameLabelwinRemove(
    tkoLabelframe * labelframe);
static void FrameMap(
    ClientData clientData);

/*
 * Data
 */

/*
 * frameMeta --
 *
 * The structure is used to identify our own data inoo objects.
 */
static Tcl_ObjectMetadataType frameMeta = {
    TCL_OO_METADATA_VERSION_CURRENT,
    "FrameMeta",
    FrameMetaDelete,
    NULL
};

/*
 * frameClass --
 *
 * The structure below defines frame class behavior by means of functions that
 * can be invoked from generic window code.
 */
static const Tk_ClassProcs frameClass = {
    sizeof(Tk_ClassProcs),      /* size */
    FrameWorldChanged,  /* worldChangedProc */
    NULL,      /* createProc */
    NULL       /* modalProc */
};

/*
 * frameGeomType --
 *
 * The structure below defines the official type record for the labelframe's
 * geometry manager:
 */
static const Tk_GeomMgr frameGeomType = {
    "labelframe",       /* name */
    FrameRequestProc,   /* requestProc */
    FrameLostSlaveProc  /* lostSlaveProc */
};

/*
 * Definition of options created in object constructor.
 * Order of used options in definition is important:
 * -class -visual -colormap -container -use
 */

/* Common options for all defined widgets. */
#define FRAME_COMMONDEFINE \
	{ "-background" , "background", "Background",\
		DEF_FRAME_BG_COLOR, NULL, NULL, 0,\
		TKO_SET_3DBORDER, &frameMeta, offsetof(tkoFrame, border)}, \
	{ "-bg" , "-background", NULL, NULL, NULL, NULL, 0,0,NULL,0}, \
	{ "-bd" , "-borderwidth", NULL, NULL, NULL, NULL, 0,0,NULL,0}, \
	{ "-cursor" , "cursor", "Cursor", \
		DEF_FRAME_CURSOR, NULL, NULL, 0, \
		TKO_SET_CURSOR, &frameMeta, offsetof(tkoFrame, cursor)}, \
	{ "-height" , "height", "Height", \
		DEF_FRAME_HEIGHT, NULL, NULL, 0,\
		TKO_SET_PIXEL, &frameMeta, offsetof(tkoFrame, height)}, \
	{ "-highlightbackground", "highlightbackground", "highlightBackground", \
		DEF_FRAME_HIGHLIGHT_BG, NULL, NULL, 0,\
		TKO_SET_XCOLOR, &frameMeta, offsetof(tkoFrame, highlightBgColorPtr)}, \
	{ "-highlightcolor", "highlightColor", "HighlightColor", \
		DEF_FRAME_HIGHLIGHT, NULL, NULL, 0,\
		TKO_SET_XCOLOR, &frameMeta, offsetof(tkoFrame, highlightColorPtr)}, \
	{ "-highlightthickness" , "highlightThickness", "HighlightThickness", \
		DEF_FRAME_HIGHLIGHT_WIDTH, NULL, NULL, 0,\
		TKO_SET_PIXEL, &frameMeta, offsetof(tkoFrame, highlightWidth)}, \
	{ "-padx" , "padX", "Pad",\
		DEF_FRAME_PADX, NULL, NULL, 0,\
		TKO_SET_PIXEL, &frameMeta, offsetof(tkoFrame, padX)}, \
	{ "-pady" , "padY", "Pad", \
		DEF_FRAME_PADY, NULL, NULL, 0,\
		TKO_SET_PIXEL, &frameMeta, offsetof(tkoFrame, padY)}, \
	{ "-takefocus" , "takeFocus", "TakeFocus", \
		DEF_FRAME_TAKE_FOCUS, NULL, NULL, 0,\
		TKO_SET_STRING, NULL, 0}, \
	{ "-width" , "width", "Width", \
		DEF_FRAME_WIDTH, NULL, NULL, 0,\
		TKO_SET_PIXEL, &frameMeta, offsetof(tkoFrame, width)}, \
	{ NULL,NULL,NULL,NULL,NULL,NULL,0,\
		0,NULL,0}

/* tko::frame options */
static tkoWidgetOptionDefine frameOptions[] = {
    {"-class", "class", "Class", "TkoFrame",
            NULL, NULL, TKO_WIDGETOPTIONREADONLY,
        TKO_SET_CLASS, NULL, 0},
    {"-visual", "visual", "Visual",
            DEF_FRAME_VISUAL, NULL, NULL, TKO_WIDGETOPTIONREADONLY,
        TKO_SET_VISUAL, NULL, 0},
    {"-colormap", "colormap", "Colormap",
            DEF_FRAME_COLORMAP, NULL, NULL, TKO_WIDGETOPTIONREADONLY,
        TKO_SET_COLORMAP, NULL, 0},
    {"-container", "container", "Container",
            DEF_FRAME_CONTAINER, NULL, NULL, TKO_WIDGETOPTIONREADONLY,
        TKO_SET_CONTAINER, &frameMeta, offsetof(tkoFrame, isContainer)},
    {"-borderwidth", "borderWidth", "BorderWidth",
            DEF_FRAME_BORDER_WIDTH, NULL, NULL, 0,
        TKO_SET_PIXEL, &frameMeta, offsetof(tkoFrame, borderWidth)},
    {"-relief", "relief", "Relief",
            DEF_FRAME_RELIEF, NULL, NULL, 0,
        TKO_SET_RELIEF, &frameMeta, offsetof(tkoFrame, relief)},
    FRAME_COMMONDEFINE
};

/* tko::toplevel options */
static tkoWidgetOptionDefine toplevelOptions[] = {
    {"-screen", "screen", "Screen",
            "", NULL, NULL, TKO_WIDGETOPTIONREADONLY,
        TKO_SET_STRING, NULL, 0},
    {"-class", "class", "Class",
            "TkoToplevel", NULL, NULL, TKO_WIDGETOPTIONREADONLY,
        TKO_SET_CLASS, NULL, 0},
    {"-container", "container", "Container",
            DEF_FRAME_CONTAINER, NULL, NULL, TKO_WIDGETOPTIONREADONLY,
        TKO_SET_CONTAINER, &frameMeta, offsetof(tkoFrame, isContainer)},
    {"-use", "use", "Use",
            DEF_TOPLEVEL_USE, NULL, NULL, TKO_WIDGETOPTIONREADONLY,
        TKO_SET_USENULL, &frameMeta, offsetof(tkoFrame, useThis)},
    {"-visual", "visual", "Visual",
            DEF_FRAME_VISUAL, NULL, NULL, TKO_WIDGETOPTIONREADONLY,
        TKO_SET_VISUAL, NULL, 0},
    {"-colormap", "colormap", "Colormap",
            DEF_FRAME_COLORMAP, NULL, NULL, TKO_WIDGETOPTIONREADONLY,
        TKO_SET_COLORMAP, NULL, 0},
    {"-borderwidth", "borderWidth", "BorderWidth",
            DEF_FRAME_BORDER_WIDTH, NULL, NULL, 0,
        TKO_SET_PIXEL, &frameMeta, offsetof(tkoFrame, borderWidth)},
    {"-menu", "menu", "Menu",
            DEF_TOPLEVEL_MENU, NULL, NULL, 0,
        TKO_SET_STRINGNULL, &frameMeta, offsetof(tkoFrame, menuName)},
    {"-relief", "relief", "Relief",
            DEF_FRAME_RELIEF, NULL, NULL, 0,
        TKO_SET_RELIEF, &frameMeta, offsetof(tkoFrame, relief)},
    FRAME_COMMONDEFINE
};

/* tko::labelframe options */
static tkoWidgetOptionDefine labelframeOptions[] = {
    {"-class", "class", "Class",
            "TkoLabelframe", NULL, NULL, TKO_WIDGETOPTIONREADONLY,
        TKO_SET_CLASS, NULL, 0},
    {"-visual", "visual", "Visual",
            DEF_FRAME_VISUAL, NULL, NULL, TKO_WIDGETOPTIONREADONLY,
        TKO_SET_VISUAL, NULL, 0},
    {"-colormap", "colormap", "Colormap",
            DEF_FRAME_COLORMAP, NULL, NULL, TKO_WIDGETOPTIONREADONLY,
        TKO_SET_COLORMAP, NULL, 0},
    {"-borderwidth", "borderWidth", "BorderWidth",
            DEF_LABELFRAME_BORDER_WIDTH, NULL, NULL, 0,
        TKO_SET_PIXEL, &frameMeta, offsetof(tkoFrame, borderWidth)},
    {"-fg", "-foreground", NULL, NULL, NULL, NULL, 0,
        0, NULL, 0},
    {"-font", "font", "Font",
            DEF_LABELFRAME_FONT, NULL, NULL, 0,
        TKO_SET_FONT, &frameMeta, offsetof(tkoLabelframe, tkfont)},
    {"-foreground", "foreground", "Foreground",
            DEF_LABELFRAME_FG, NULL, NULL, 0,
        TKO_SET_XCOLOR, &frameMeta, offsetof(tkoLabelframe, textColorPtr)},
    {"-labelanchor", "labelAnchor", "LabelAnchor",
            DEF_LABELFRAME_LABELANCHOR, NULL, FrameMethod_labelanchor, 0,
        0, NULL, 0},
    {"-labelwidget", "labelWidget", "LabelWidget",
            "", NULL, FrameMethod_labelwidget, 0,
        0, NULL, 0},
    {"-relief", "relief", "Relief",
            DEF_LABELFRAME_RELIEF, NULL, NULL, 0,
        TKO_SET_RELIEF, &frameMeta, offsetof(tkoFrame, relief)},
    {"-text", "text", "Text",
            DEF_LABELFRAME_TEXT, NULL, NULL, 0,
        TKO_SET_TCLOBJ, &frameMeta, offsetof(tkoLabelframe, textPtr)},
    FRAME_COMMONDEFINE
};

/*
 * Definition of object methods created in Tko_FrameInit() function.
 */

/* tko::frame methods. */
static Tcl_MethodType frameMethods[] = {
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, FrameConstructorFrame, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, FrameDestructor, NULL, NULL},
    {-1, NULL, NULL, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "_tko_configure", FrameMethod_tko_configure,
            NULL, NULL},
    {-1, NULL, NULL, NULL, NULL}
};

/* tko::labelframe methods. */
static Tcl_MethodType labelframeMethods[] = {
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, FrameConstructorLabelframe, NULL,
            NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, FrameDestructor, NULL, NULL},
    {-1, NULL, NULL, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "_tko_configure", FrameMethod_tko_configure,
            NULL, NULL},
    {-1, NULL, NULL, NULL, NULL}
};

/* tko::toplevel methods. */
static Tcl_MethodType toplevelMethods[] = {
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, FrameConstructorToplevel, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, FrameDestructor, NULL, NULL},
    {-1, NULL, NULL, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "_tko_configure", FrameMethod_tko_configure,
            NULL, NULL},
    {-1, NULL, NULL, NULL, NULL}
};

/*
 * Tko_FrameInit --
 *
 * Create tko frame widget class objects.
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
int
Tko_FrameInit(
    Tcl_Interp * interp)
{              /* Tcl interpreter. */
Tcl_Class clazz;
Tcl_Object object;

    /* Create class like tk command and remove oo functions from widget commands */
static const char *initScript =
    "::oo::class create ::tko::frame {superclass ::tko::widget; variable tko; {*}$::tko::unknown}\n"
    "::oo::class create ::tko::labelframe {superclass ::tko::widget; variable tko; {*}$::tko::unknown}\n"
    "::oo::class create ::tko::toplevel {superclass ::tko::widget; variable tko; {*}$::tko::unknown}\n";

    /* Create widget class. */
    if(Tcl_GlobalEval(interp, initScript) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * ::tko::toplevel
     */
    /* Get class object */
    if((object = Tcl_GetObjectFromObj(interp, TkoObj.tko_toplevel)) == NULL
        || (clazz = Tcl_GetObjectAsClass(object)) == NULL) {
        return TCL_ERROR;
    }
    /* Add methods and options */
    if(TkoWidgetClassDefine(interp, clazz, Tcl_GetObjectName(interp, object),
            toplevelMethods, toplevelOptions) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * ::tko::frame
     */
    /* Get class object */
    if((object = Tcl_GetObjectFromObj(interp, TkoObj.tko_frame)) == NULL
        || (clazz = Tcl_GetObjectAsClass(object)) == NULL) {
        return TCL_ERROR;
    }
    /* Add methods and options */
    if(TkoWidgetClassDefine(interp, clazz, Tcl_GetObjectName(interp, object),
            frameMethods, frameOptions) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * ::tko::labelframe
     */
    /* Get class object */
    if((object = Tcl_GetObjectFromObj(interp, TkoObj.tko_labelframe)) == NULL
        || (clazz = Tcl_GetObjectAsClass(object)) == NULL) {
        return TCL_ERROR;
    }
    /* Add methods and options */
    if(TkoWidgetClassDefine(interp, clazz, Tcl_GetObjectName(interp, object),
            labelframeMethods, labelframeOptions) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * FrameConstructorFrame --
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
FrameConstructorFrame(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    return FrameConstructor(TYPE_FRAME, interp, context, objc, objv);
}

/*
 * FrameConstructorLabelframe --
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
FrameConstructorLabelframe(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    return FrameConstructor(TYPE_LABELFRAME, interp, context, objc, objv);
}

/*
 * FrameConstructorToplevel --
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
FrameConstructorToplevel(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    return FrameConstructor(TYPE_TOPLEVEL, interp, context, objc, objv);
}

/*
 * FrameDestructor --
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
FrameDestructor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    int skip;
    Tcl_Object object;
    tkoFrame *frame;
    Tk_Window tkWin = NULL;
    tkoLabelframe *labelframe;
    if((object = Tcl_ObjectContextObject(context)) == NULL) {
        return TCL_ERROR;
    }
    skip = Tcl_ObjectContextSkippedArgs(context);
    if((frame = (tkoFrame *) Tcl_ObjectGetMetadata(object, &frameMeta)) != NULL) {
        Tcl_Preserve(frame);
        labelframe = (tkoLabelframe *) frame;

        if(frame->win) {
            tkWin = *(frame->win);
            frame->win = NULL;
        }
        if(tkWin) {
            Tk_DeleteEventHandler(tkWin, frame->mask, FrameEventProc, frame);
        }
        if(frame->cursor != None) {
            if(frame->display != None) {
                Tk_FreeCursor(frame->display, frame->cursor);
            }
            frame->cursor = None;
        }
        frame->flags = 0;
        Tcl_CancelIdleCall(FrameDisplay, frame);
        Tcl_CancelIdleCall(FrameMap, frame);

        if(frame->menuName != NULL && tkWin) {
            TkSetWindowMenuBar(frame->interp, tkWin, frame->menuName, NULL);
            frame->menuName = NULL;
        }
        if(frame->type == TYPE_LABELFRAME && labelframe->labelWin) {
            Tk_ManageGeometry(labelframe->labelWin, NULL, NULL);
            if(tkWin && (tkWin != Tk_Parent(labelframe->labelWin))) {
                Tk_UnmaintainGeometry(labelframe->labelWin, tkWin);
            }
            Tk_UnmapWindow(labelframe->labelWin);
            labelframe->labelWin = NULL;
        }
        Tcl_Release(frame);
        Tcl_ObjectSetMetadata(object, &frameMeta, NULL);
    }
    /* ignore errors */
    Tcl_ObjectContextInvokeNext(interp, context, objc, objv, skip);

    return TCL_OK;
}

/*
 * FrameMethod_tko_configure --
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
FrameMethod_tko_configure(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    char *oldMenuName;
    Tcl_Object object;
    tkoFrame *frame;
    tkoLabelframe *labelframe;
    Tk_Window oldWindow;
    Tk_Window tkwin;
    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (frame =
            (tkoFrame *) Tcl_ObjectGetMetadata(object, &frameMeta)) == NULL
        || frame->win == NULL || (tkwin = *(frame->win)) == NULL) {
        return TCL_ERROR;
    }
    labelframe = (tkoLabelframe *) frame;

    /*
     * Need the old menubar name for the menu code to delete it.
     */

    if(frame->menuName == NULL) {
        oldMenuName = NULL;
    } else {
        oldMenuName = ckalloc(strlen(frame->menuName) + 1);
        strcpy(oldMenuName, frame->menuName);
    }

    if(frame->type == TYPE_LABELFRAME) {
        oldWindow = labelframe->labelWin;
    }
    /*TODO ???      if (oldMenuName != NULL) {
     * ckfree(oldMenuName);
     * }
     */

    /*
     * A few of the options require additional processing.
     */

    if((((oldMenuName == NULL) && (frame->menuName != NULL))
            || ((oldMenuName != NULL) && (frame->menuName == NULL))
            || ((oldMenuName != NULL) && (frame->menuName != NULL)
                && strcmp(oldMenuName, frame->menuName) != 0))
        && frame->type == TYPE_TOPLEVEL) {
        TkSetWindowMenuBar(frame->interp, tkwin, oldMenuName, frame->menuName);
    }

    if(oldMenuName != NULL) {
        ckfree(oldMenuName);
    }

    if(frame->border != NULL) {
        Tk_SetBackgroundFromBorder(tkwin, frame->border);
    } else {
        Tk_SetWindowBackgroundPixmap(tkwin, None);
    }

    if(frame->highlightWidth < 0) {
        frame->highlightWidth = 0;
    }
    if(frame->padX < 0) {
        frame->padX = 0;
    }
    if(frame->padY < 0) {
        frame->padY = 0;
    }

    FrameWorldChanged(frame);
    if(Tcl_ObjectContextInvokeNext(interp, context, objc, objv,
            Tcl_ObjectContextSkippedArgs(context)) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * FrameMethod_labelanchor --
 *
 * Process -labelanchor option.
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
FrameMethod_labelanchor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    int index, code;
    Tcl_Object object;
    tkoLabelframe *labelframe;
    Tcl_Obj *value;
	static const char *const labelAnchorStrings[] = {
		"e", "en", "es", "n", "ne", "nw", "s", "se", "sw", "w", "wn", "ws",
		NULL
	};

	if((object = Tcl_ObjectContextObject(context)) == NULL
        || (labelframe =
            (tkoLabelframe *) Tcl_ObjectGetMetadata(object, &frameMeta)) == NULL
        || (value =
            TkoWidgetOptionGet(interp, object, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }
    code =
        Tcl_GetIndexFromObj(interp, value, labelAnchorStrings, "labelanchor", 0,
        &index);
    if(code != TCL_OK) {
        return TCL_ERROR;
    }
    labelframe->labelAnchor = (Tk_Anchor) index;
    return TCL_OK;
}

/*
 * FrameMethod_labelwidget --
 *
 * Process -labelwidget option.
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
FrameMethod_labelwidget(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tk_Window oldWindow = NULL;
    Tk_Window newWindow = NULL;
    Tk_Window tkwin = NULL;
    Tk_Window ancestor, parent, sibling = NULL;
    Tcl_Object object;
    tkoLabelframe *labelframe;
    Tcl_Obj *value;
    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (labelframe =
            (tkoLabelframe *) Tcl_ObjectGetMetadata(object, &frameMeta)) == NULL
        || (value =
            TkoWidgetOptionGet(interp, object, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }

    if(labelframe->frame.win == NULL
        || (tkwin = *(labelframe->frame.win)) == NULL) {
        return TCL_ERROR;
    }

    if(value == NULL || Tcl_GetCharLength(value) == 0) {
        newWindow = NULL;
    } else if(TkGetWindowFromObj(interp, tkwin, value, &newWindow) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * If a -labelwidget is specified, check that it is valid and set up
     * geometry management for it.
     */
    oldWindow = labelframe->labelWin;
    if(oldWindow != newWindow) {
        if(newWindow != NULL) {
            /*
             * Make sure that the frame is either the parent of the window
             * used as label or a descendant of that parent. Also, don't
             * allow a top-level window to be managed inside the frame.
             */
            parent = Tk_Parent(newWindow);
            for(ancestor = tkwin;; ancestor = Tk_Parent(ancestor)) {
                if(ancestor == parent) {
                    break;
                }
                sibling = ancestor;
                if(Tk_IsTopLevel(ancestor)) {
                    goto badLabelWindow;
                }
            }
            if(Tk_IsTopLevel(newWindow)) {
                goto badLabelWindow;
            }
            if(newWindow == tkwin) {
                goto badLabelWindow;
            }
        }
        if(oldWindow != NULL) {
            Tk_DeleteEventHandler(oldWindow, StructureNotifyMask,
                FrameStructureProc, labelframe);
            Tk_ManageGeometry(oldWindow, NULL, NULL);
            Tk_UnmaintainGeometry(oldWindow, tkwin);
            Tk_UnmapWindow(oldWindow);
        }
        if(newWindow != NULL) {
            Tk_CreateEventHandler(newWindow,
                StructureNotifyMask, FrameStructureProc, labelframe);
            Tk_ManageGeometry(newWindow, &frameGeomType, labelframe);
            /*
             * If the frame is not parent to the label, make sure the
             * label is above its sibling in the stacking order.
             */
            if(sibling != NULL) {
                Tk_RestackWindow(newWindow, Above, sibling);
            }
        }
        labelframe->labelWin = newWindow;
    }
    //      FrameWorldChanged(labelframe);
    return TCL_OK;

  badLabelWindow:
    Tcl_SetObjResult(interp,
        Tcl_ObjPrintf("can't use %s as label in this frame",
            Tk_PathName(labelframe->labelWin)));
    Tcl_SetErrorCode(interp, "TK", "GEOMETRY", "HIERARCHY", NULL);
    labelframe->labelWin = NULL;
    return TCL_ERROR;
}

/*
 * FrameConstructor --
 *
 * Common part of all widget contructors.
 *
 * Results:
 *  TODO
 *
 * Side effects:
 *  TODO
 */
static int
FrameConstructor(
    int type,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Object object;
    tkoFrame *frame;
    int skip;
    Tcl_Obj *myObjv[2];

    /* Get current object. Should not fail? */
    if((object = Tcl_ObjectContextObject(context)) == NULL) {
        return TCL_ERROR;
    }
    skip = Tcl_ObjectContextSkippedArgs(context);
    /* Check objv[] arguments: ... optionlist arglist */
    if(objc - skip != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "optionlist arglist");
        return TCL_ERROR;
    }
    if(type == TYPE_FRAME) {
        frame = ckalloc(sizeof(tkoFrame));
        memset(frame, 0, sizeof(tkoFrame));
        myObjv[0] =
            Tcl_ObjGetVar2(interp, TkoObj.tko_options, TkoObj.tko_frame,
            TCL_GLOBAL_ONLY);
        myObjv[1] = objv[objc - 1];
    } else if(type == TYPE_LABELFRAME) {
    tkoLabelframe *labelframe;

        frame = ckalloc(sizeof(tkoLabelframe));
        memset(frame, 0, sizeof(tkoLabelframe));
        myObjv[0] =
            Tcl_ObjGetVar2(interp, TkoObj.tko_options, TkoObj.tko_labelframe,
            TCL_GLOBAL_ONLY);
        myObjv[1] = objv[objc - 1];
        labelframe = (tkoLabelframe *) frame;
        labelframe->textPtr = NULL;
        labelframe->tkfont = NULL;
        labelframe->textColorPtr = NULL;
        labelframe->labelAnchor = LABELANCHOR_NW;
        labelframe->labelWin = NULL;
        labelframe->textGC = None;
        labelframe->textLayout = NULL;
        /*labelframe->labelBox */
        labelframe->labelReqWidth = 0;
        labelframe->labelReqHeight = 0;
        labelframe->labelTextX = 0;
        labelframe->labelTextY = 0;
    } else if(type == TYPE_TOPLEVEL) {
        myObjv[1] = Tcl_NewStringObj("-screen {}", -1);
        Tcl_IncrRefCount(myObjv[1]);
        if(Tcl_ListObjAppendList(interp, myObjv[1], objv[objc - 1]) != TCL_OK) {
            Tcl_DecrRefCount(myObjv[1]);
            return TCL_ERROR;
        }
        frame = ckalloc(sizeof(tkoFrame));
        memset(frame, 0, sizeof(tkoFrame));
        myObjv[0] =
            Tcl_ObjGetVar2(interp, TkoObj.tko_options, TkoObj.tko_toplevel,
            TCL_GLOBAL_ONLY);
    } else {
        Tcl_WrongNumArgs(interp, 1, objv, "internal type error");
        return TCL_ERROR;
    }
    if(myObjv[0] == NULL) {
        return TCL_ERROR;
    }
    frame->win = NULL;
    frame->object = object;
    frame->interp = interp;
    frame->display = None;
    frame->type = type;
    frame->menuName = NULL;
    frame->colormap = None;
    frame->border = NULL;
    frame->borderWidth = 0;
    frame->relief = TK_RELIEF_FLAT;
    frame->highlightWidth = 0;
    frame->highlightBgColorPtr = NULL;
    frame->highlightColorPtr = NULL;
    frame->width = 0;
    frame->height = 0;
    frame->cursor = None;
    frame->isContainer = 0;
    frame->useThis = NULL;
    frame->flags = 0;
    frame->padX = 0;
    frame->padY = 0;
    frame->mask = ExposureMask | StructureNotifyMask | FocusChangeMask;
    if(type == TYPE_TOPLEVEL) {
        frame->mask |= ActivateMask;
    }

    Tcl_ObjectSetMetadata(object, &frameMeta, (ClientData) frame);

    myObjv[0] = Tcl_DuplicateObj(myObjv[0]);
    Tcl_IncrRefCount(myObjv[0]);
    Tcl_ListObjAppendList(interp, myObjv[0], objv[objc - 2]);
    if(Tcl_ObjectContextInvokeNext(interp, context, 2, myObjv, 0) != TCL_OK) {
        Tcl_DecrRefCount(myObjv[0]);
        if(type == TYPE_TOPLEVEL) {
            Tcl_DecrRefCount(myObjv[1]);
        }
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(myObjv[0]);
    if(type == TYPE_TOPLEVEL) {
        Tcl_DecrRefCount(myObjv[1]);
    }
    frame->win = TkoWidgetWindow(object);
    if(frame->win == NULL || *(frame->win) == NULL) {
        return TCL_ERROR;
    }
    if((frame->display = Tk_Display(*(frame->win))) == None) {
        return TCL_ERROR;
    }
    if(frame->isContainer && frame->useThis != NULL) {
        Tcl_SetObjResult(interp,
            Tcl_NewStringObj
            ("windows cannot have both the -use and the -container"
                " option set", -1));
        Tcl_SetErrorCode(interp, "TK", "FRAME", "CONTAINMENT", NULL);
        return TCL_ERROR;
    }
    /*
     * For top-level windows, provide an initial geometry request of 200x200,
     * just so the window looks nicer on the screen if it doesn't request a
     * size for itself.
     */
    if(type == TYPE_TOPLEVEL) {
        Tk_GeometryRequest(*(frame->win), 200, 200);
    }

    /*
     * Store backreference to frame widget in window structure.
     */

    Tk_SetClassProcs(*(frame->win), &frameClass, frame);

    /*
     * Mark Tk frames as suitable candidates for [wm manage].
     */

    ((TkWindow *) * (frame->win))->flags |= TK_WM_MANAGEABLE;

    Tk_CreateEventHandler(*(frame->win), frame->mask, FrameEventProc, frame);

    if(type == TYPE_TOPLEVEL) {
        Tcl_DoWhenIdle(FrameMap, frame);
    }

    return TCL_OK;
}

/*
 * FrameMetaDestroy --
 *
 *	This function is invoked by Tcl_EventuallyFree or Tcl_Release to clean
 *	up the internal structure of a frame at a safe time (when no-one is
 *	using it anymore).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Everything associated with the frame is freed up.
 */
static void
FrameMetaDestroy(
    tkoFrame * frame)
{              /* Info about frame widget. */
tkoLabelframe *labelframe = (tkoLabelframe *) frame;

    if(frame->menuName != NULL) {
        ckfree(frame->menuName);
    }
    if(frame->useThis) {
        Tcl_DecrRefCount(frame->useThis);
    }
    if(frame->type == TYPE_LABELFRAME) {
        if(labelframe->textLayout) {
            Tk_FreeTextLayout(labelframe->textLayout);
        }
        if(labelframe->textGC != None && frame->display != None) {
            Tk_FreeGC(frame->display, labelframe->textGC);
        }
    }
    if(frame->border) {
        Tk_Free3DBorder(frame->border);
    }
    if(frame->colormap != None && frame->display != None) {
        Tk_FreeColormap(frame->display, frame->colormap);
    }
    if(frame->highlightBgColorPtr != NULL) {
        Tk_FreeColor(frame->highlightBgColorPtr);
    }
    if(frame->highlightColorPtr != NULL) {
        Tk_FreeColor(frame->highlightColorPtr);
    }
    ckfree(frame);
}

/*
 * FrameWorldChanged --
 *
 *	This function is called when the world has changed in some way and the
 *	widget needs to recompute all its graphics contexts and determine its
 *	new geometry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frame will be relayed out and redisplayed.
 */
static void
FrameWorldChanged(
    ClientData instanceData)
{              /* Information about widget. */
tkoFrame *frame = instanceData;
tkoLabelframe *labelframe = instanceData;
XGCValues gcValues;
GC  gc;
int anyTextLabel, anyWindowLabel;
int bWidthLeft, bWidthRight, bWidthTop, bWidthBottom;
const char *labelText;
    if(frame->win == NULL || *(frame->win) == NULL)
        return;

    anyTextLabel = (frame->type == TYPE_LABELFRAME) &&
        (labelframe->textPtr != NULL) && (labelframe->labelWin == NULL);
    anyWindowLabel = (frame->type == TYPE_LABELFRAME) &&
        (labelframe->labelWin != NULL);

    if(frame->type == TYPE_LABELFRAME) {
        /*
         * The textGC is needed even in the labelWin case, so it's always
         * created for a labelframe.
         */

        gcValues.font = Tk_FontId(labelframe->tkfont);
        gcValues.foreground = labelframe->textColorPtr->pixel;
        gcValues.graphics_exposures = False;
        gc = Tk_GetGC(*(frame->win),
            GCForeground | GCFont | GCGraphicsExposures, &gcValues);
        if(labelframe->textGC != None) {
            Tk_FreeGC(frame->display, labelframe->textGC);
        }
        labelframe->textGC = gc;

        /*
         * Calculate label size.
         */

        labelframe->labelReqWidth = labelframe->labelReqHeight = 0;

        if(anyTextLabel) {
            labelText = Tcl_GetString(labelframe->textPtr);
            if(labelframe->textLayout) {
                Tk_FreeTextLayout(labelframe->textLayout);
            }
            labelframe->textLayout =
                Tk_ComputeTextLayout(labelframe->tkfont,
                labelText, -1, 0, TK_JUSTIFY_CENTER, 0,
                &labelframe->labelReqWidth, &labelframe->labelReqHeight);
            labelframe->labelReqWidth += 2 * LABELSPACING;
            labelframe->labelReqHeight += 2 * LABELSPACING;
        } else if(anyWindowLabel) {
            labelframe->labelReqWidth = Tk_ReqWidth(labelframe->labelWin);
            labelframe->labelReqHeight = Tk_ReqHeight(labelframe->labelWin);
        }

        /*
         * Make sure label size is at least as big as the border. This
         * simplifies later calculations and gives a better appearance with
         * thick borders.
         */

        if((labelframe->labelAnchor >= LABELANCHOR_N) &&
            (labelframe->labelAnchor <= LABELANCHOR_SW)) {
            if(labelframe->labelReqHeight < frame->borderWidth) {
                labelframe->labelReqHeight = frame->borderWidth;
            }
        } else {
            if(labelframe->labelReqWidth < frame->borderWidth) {
                labelframe->labelReqWidth = frame->borderWidth;
            }
        }
    }

    /*
     * Calculate individual border widths.
     */

    bWidthBottom = bWidthTop = bWidthRight = bWidthLeft =
        frame->borderWidth + frame->highlightWidth;

    bWidthLeft += frame->padX;
    bWidthRight += frame->padX;
    bWidthTop += frame->padY;
    bWidthBottom += frame->padY;

    if(anyTextLabel || anyWindowLabel) {
        switch (labelframe->labelAnchor) {
        case LABELANCHOR_E:
        case LABELANCHOR_EN:
        case LABELANCHOR_ES:
            bWidthRight += labelframe->labelReqWidth - frame->borderWidth;
            break;
        case LABELANCHOR_N:
        case LABELANCHOR_NE:
        case LABELANCHOR_NW:
            bWidthTop += labelframe->labelReqHeight - frame->borderWidth;
            break;
        case LABELANCHOR_S:
        case LABELANCHOR_SE:
        case LABELANCHOR_SW:
            bWidthBottom += labelframe->labelReqHeight - frame->borderWidth;
            break;
        default:
            bWidthLeft += labelframe->labelReqWidth - frame->borderWidth;
            break;
        }
    }

    Tk_SetInternalBorderEx(*(frame->win), bWidthLeft, bWidthRight, bWidthTop,
        bWidthBottom);

    FrameComputeGeometry(frame);

    /*
     * A labelframe should request size for its label.
     */

    if(frame->type == TYPE_LABELFRAME) {
int minwidth = labelframe->labelReqWidth;
int minheight = labelframe->labelReqHeight;
int padding = frame->highlightWidth;

        if(frame->borderWidth > 0) {
            padding += frame->borderWidth + LABELMARGIN;
        }
        padding *= 2;
        if((labelframe->labelAnchor >= LABELANCHOR_N) &&
            (labelframe->labelAnchor <= LABELANCHOR_SW)) {
            minwidth += padding;
            minheight += frame->borderWidth + frame->highlightWidth;
        } else {
            minheight += padding;
            minwidth += frame->borderWidth + frame->highlightWidth;
        }
        Tk_SetMinimumRequestSize(*(frame->win), minwidth, minheight);
    }

    if((frame->width > 0) || (frame->height > 0)) {
        Tk_GeometryRequest(*(frame->win), frame->width, frame->height);
    }

    if(Tk_IsMapped(*(frame->win))) {
        if(!(frame->flags & REDRAW_PENDING)) {
            Tcl_DoWhenIdle(FrameDisplay, frame);
        }
        frame->flags |= REDRAW_PENDING;
    }
}

/*
 * FrameComputeGeometry --
 *
 *	This function is called to compute various geometrical information for
 *	a frame, such as where various things get displayed. It's called when
 *	the window is reconfigured.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Display-related numbers get changed in *frame.
 */

static void
FrameComputeGeometry(
    register tkoFrame * frame)
{              /* Information about widget. */
    int otherWidth, otherHeight, otherWidthT, otherHeightT, padding;
    int maxWidth, maxHeight;
    tkoLabelframe *labelframe = (tkoLabelframe *) frame;
    if(frame->win == NULL || *(frame->win) == NULL)
        return;

    /*
     * We have nothing to do here unless there is a label.
     */

    if(frame->type != TYPE_LABELFRAME) {
        return;
    }
    if(labelframe->textPtr == NULL && labelframe->labelWin == NULL) {
        return;
    }

    /*
     * Calculate the available size for the label
     */

    labelframe->labelBox.width = labelframe->labelReqWidth;
    labelframe->labelBox.height = labelframe->labelReqHeight;

    padding = frame->highlightWidth;
    if(frame->borderWidth > 0) {
        padding += frame->borderWidth + LABELMARGIN;
    }
    padding *= 2;

    maxHeight = Tk_Height(*(frame->win));
    maxWidth = Tk_Width(*(frame->win));

    if((labelframe->labelAnchor >= LABELANCHOR_N) &&
        (labelframe->labelAnchor <= LABELANCHOR_SW)) {
        maxWidth -= padding;
        if(maxWidth < 1) {
            maxWidth = 1;
        }
    } else {
        maxHeight -= padding;
        if(maxHeight < 1) {
            maxHeight = 1;
        }
    }
    if(labelframe->labelBox.width > maxWidth) {
        labelframe->labelBox.width = maxWidth;
    }
    if(labelframe->labelBox.height > maxHeight) {
        labelframe->labelBox.height = maxHeight;
    }

    /*
     * Calculate label and text position. The text's position is based on the
     * requested size (= the text's real size) to get proper alignment if the
     * text does not fit.
     */

    otherWidth = Tk_Width(*(frame->win)) - labelframe->labelBox.width;
    otherHeight = Tk_Height(*(frame->win)) - labelframe->labelBox.height;
    otherWidthT = Tk_Width(*(frame->win)) - labelframe->labelReqWidth;
    otherHeightT = Tk_Height(*(frame->win)) - labelframe->labelReqHeight;
    padding = frame->highlightWidth;

    switch (labelframe->labelAnchor) {
    case LABELANCHOR_E:
    case LABELANCHOR_EN:
    case LABELANCHOR_ES:
        labelframe->labelTextX = otherWidthT - padding;
        labelframe->labelBox.x = otherWidth - padding;
        break;
    case LABELANCHOR_N:
    case LABELANCHOR_NE:
    case LABELANCHOR_NW:
        labelframe->labelTextY = padding;
        labelframe->labelBox.y = padding;
        break;
    case LABELANCHOR_S:
    case LABELANCHOR_SE:
    case LABELANCHOR_SW:
        labelframe->labelTextY = otherHeightT - padding;
        labelframe->labelBox.y = otherHeight - padding;
        break;
    default:
        labelframe->labelTextX = padding;
        labelframe->labelBox.x = padding;
        break;
    }

    if(frame->borderWidth > 0) {
        padding += frame->borderWidth + LABELMARGIN;
    }

    switch (labelframe->labelAnchor) {
    case LABELANCHOR_NW:
    case LABELANCHOR_SW:
        labelframe->labelTextX = padding;
        labelframe->labelBox.x = padding;
        break;
    case LABELANCHOR_N:
    case LABELANCHOR_S:
        labelframe->labelTextX = otherWidthT / 2;
        labelframe->labelBox.x = otherWidth / 2;
        break;
    case LABELANCHOR_NE:
    case LABELANCHOR_SE:
        labelframe->labelTextX = otherWidthT - padding;
        labelframe->labelBox.x = otherWidth - padding;
        break;
    case LABELANCHOR_EN:
    case LABELANCHOR_WN:
        labelframe->labelTextY = padding;
        labelframe->labelBox.y = padding;
        break;
    case LABELANCHOR_E:
    case LABELANCHOR_W:
        labelframe->labelTextY = otherHeightT / 2;
        labelframe->labelBox.y = otherHeight / 2;
        break;
    default:
        labelframe->labelTextY = otherHeightT - padding;
        labelframe->labelBox.y = otherHeight - padding;
        break;
    }
}

/*
 * FrameDisplay --
 *
 *	This function is invoked to display a frame widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Commands are output to X to display the frame in its current mode.
 */
static void
FrameDisplay(
    ClientData clientData /* Information about widget. */)
{             
    tkoFrame *frame = clientData;
    int bdX1, bdY1, bdX2, bdY2, hlWidth;
    Pixmap pixmap;
    TkRegion clipRegion = NULL;

    if(frame->win == NULL || *(frame->win) == NULL)
        return;

    frame->flags &= ~REDRAW_PENDING;
    if(!Tk_IsMapped(*(frame->win))) {
        return;
    }

    /*
     * Highlight shall always be drawn if it exists, so do that first.
     */

    hlWidth = frame->highlightWidth;

    if(hlWidth != 0) {
GC  fgGC, bgGC;

        bgGC = Tk_GCForColor(frame->highlightBgColorPtr,
            Tk_WindowId(*(frame->win)));
        if(frame->flags & GOT_FOCUS) {
            fgGC = Tk_GCForColor(frame->highlightColorPtr,
                Tk_WindowId(*(frame->win)));
            TkpDrawHighlightBorder(*(frame->win), fgGC, bgGC, hlWidth,
                Tk_WindowId(*(frame->win)));
        } else {
            TkpDrawHighlightBorder(*(frame->win), bgGC, bgGC, hlWidth,
                Tk_WindowId(*(frame->win)));
        }
    }

    /*
     * If -background is set to "", no interior is drawn.
     */

    if(frame->border == NULL) {
        return;
    }

    if(frame->type != TYPE_LABELFRAME) {
        /*
         * Pass to platform specific draw function. In general, it just draws
         * a simple rectangle, but it may "theme" the background.
         */

      noLabel:
        TkpDrawFrame(*(frame->win), frame->border, hlWidth,
            frame->borderWidth, frame->relief);
    } else {
        tkoLabelframe *labelframe = (tkoLabelframe *) frame;

        if((labelframe->textPtr == NULL) && (labelframe->labelWin == NULL)) {
            goto noLabel;
        }
#ifndef TK_NO_DOUBLE_BUFFERING
        /*
         * In order to avoid screen flashes, this function redraws the frame
         * into off-screen memory, then copies it back on-screen in a single
         * operation. This means there's no point in time where the on-screen
         * image has been cleared.
         */

        pixmap = Tk_GetPixmap(frame->display, Tk_WindowId(*(frame->win)),
            Tk_Width(*(frame->win)), Tk_Height(*(frame->win)),
            Tk_Depth(*(frame->win)));
#else
        pixmap = Tk_WindowId(tkWin);
#endif /* TK_NO_DOUBLE_BUFFERING */

        /*
         * Clear the pixmap.
         */

        Tk_Fill3DRectangle(*(frame->win), pixmap, frame->border, 0, 0,
            Tk_Width(*(frame->win)), Tk_Height(*(frame->win)), 0,
            TK_RELIEF_FLAT);

        /*
         * Calculate how the label affects the border's position.
         */

        bdX1 = bdY1 = hlWidth;
        bdX2 = Tk_Width(*(frame->win)) - hlWidth;
        bdY2 = Tk_Height(*(frame->win)) - hlWidth;

        switch (labelframe->labelAnchor) {
        case LABELANCHOR_E:
        case LABELANCHOR_EN:
        case LABELANCHOR_ES:
            bdX2 -= (labelframe->labelBox.width - frame->borderWidth) / 2;
            break;
        case LABELANCHOR_N:
        case LABELANCHOR_NE:
        case LABELANCHOR_NW:
            /*
             * Since the glyphs of the text tend to be in the lower part we
             * favor a lower border position by rounding up.
             */

            bdY1 += (labelframe->labelBox.height - frame->borderWidth + 1) / 2;
            break;
        case LABELANCHOR_S:
        case LABELANCHOR_SE:
        case LABELANCHOR_SW:
            bdY2 -= (labelframe->labelBox.height - frame->borderWidth) / 2;
            break;
        default:
            bdX1 += (labelframe->labelBox.width - frame->borderWidth) / 2;
            break;
        }

        /*
         * Draw border
         */

        Tk_Draw3DRectangle(*(frame->win), pixmap, frame->border, bdX1, bdY1,
            bdX2 - bdX1, bdY2 - bdY1, frame->borderWidth, frame->relief);

        if(labelframe->labelWin == NULL) {
            /*
             * Clear behind the label
             */

            Tk_Fill3DRectangle(*(frame->win), pixmap,
                frame->border, labelframe->labelBox.x,
                labelframe->labelBox.y, labelframe->labelBox.width,
                labelframe->labelBox.height, 0, TK_RELIEF_FLAT);

            /*
             * Draw label. If there is not room for the entire label, use
             * clipping to get a nice appearance.
             */

            if((labelframe->labelBox.width < labelframe->labelReqWidth)
                || (labelframe->labelBox.height < labelframe->labelReqHeight)) {
                clipRegion = TkCreateRegion();
                TkUnionRectWithRegion(&labelframe->labelBox, clipRegion,
                    clipRegion);
                TkSetRegion(frame->display, labelframe->textGC, clipRegion);
            }

            Tk_DrawTextLayout(frame->display, pixmap,
                labelframe->textGC, labelframe->textLayout,
                labelframe->labelTextX + LABELSPACING,
                labelframe->labelTextY + LABELSPACING, 0, -1);

            if(clipRegion != NULL) {
                XSetClipMask(frame->display, labelframe->textGC, None);
                TkDestroyRegion(clipRegion);
            }
        } else {
            /*
             * Reposition and map the window (but in different ways depending
             * on whether the frame is the window's parent).
             */

            if(*(frame->win) == Tk_Parent(labelframe->labelWin)) {
                if((labelframe->labelBox.x != Tk_X(labelframe->labelWin))
                    || (labelframe->labelBox.y != Tk_Y(labelframe->labelWin))
                    || (labelframe->labelBox.width !=
                        Tk_Width(labelframe->labelWin))
                    || (labelframe->labelBox.height !=
                        Tk_Height(labelframe->labelWin))) {
                    Tk_MoveResizeWindow(labelframe->labelWin,
                        labelframe->labelBox.x,
                        labelframe->labelBox.y,
                        labelframe->labelBox.width,
                        labelframe->labelBox.height);
                }
                Tk_MapWindow(labelframe->labelWin);
            } else {
                Tk_MaintainGeometry(labelframe->labelWin, *(frame->win),
                    labelframe->labelBox.x, labelframe->labelBox.y,
                    labelframe->labelBox.width, labelframe->labelBox.height);
            }
        }

#ifndef TK_NO_DOUBLE_BUFFERING
        /*
         * Everything's been redisplayed; now copy the pixmap onto the screen
         * and free up the pixmap.
         */

        XCopyArea(frame->display, pixmap, Tk_WindowId(*(frame->win)),
            labelframe->textGC, hlWidth, hlWidth,
            (unsigned)(Tk_Width(*(frame->win)) - 2 * hlWidth),
            (unsigned)(Tk_Height(*(frame->win)) - 2 * hlWidth),
            hlWidth, hlWidth);
        Tk_FreePixmap(frame->display, pixmap);
#endif /* TK_NO_DOUBLE_BUFFERING */
    }

}

/*
 * FrameEventProc --
 *
 *	This function is invoked by the Tk dispatcher on structure changes to
 *	a frame. For frames with 3D borders, this function is also invoked for
 *	exposures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	When the window gets deleted, internal structures get cleaned up.
 *	When it gets exposed, it is redisplayed.
 */
static void
FrameEventProc(
    ClientData clientData,     /* Information about window. */
    register XEvent * eventPtr)
{              /* Information about event. */
    tkoFrame *frame = clientData;
    if(eventPtr->type == DestroyNotify || frame->win == NULL
        || *(frame->win) == NULL)
        return;

    if((eventPtr->type == Expose) && (eventPtr->xexpose.count == 0)) {
        goto redraw;
    } else if(eventPtr->type == ConfigureNotify) {
        FrameComputeGeometry(frame);
        goto redraw;
    } else if(eventPtr->type == FocusIn) {
        if(eventPtr->xfocus.detail != NotifyInferior) {
            frame->flags |= GOT_FOCUS;
            if(frame->highlightWidth > 0) {
                goto redraw;
            }
        }
    } else if(eventPtr->type == FocusOut) {
        if(eventPtr->xfocus.detail != NotifyInferior) {
            frame->flags &= ~GOT_FOCUS;
            if(frame->highlightWidth > 0) {
                goto redraw;
            }
        }
    } else if(eventPtr->type == ActivateNotify) {
        TkpSetMainMenubar(frame->interp, *(frame->win), frame->menuName);
    }
    return;

  redraw:
    if(!(frame->flags & REDRAW_PENDING)) {
        Tcl_DoWhenIdle(FrameDisplay, frame);
        frame->flags |= REDRAW_PENDING;
    }
}

/*
 * FrameMap --
 *
 *	This function is invoked as a when-idle handler to map a newly-created
 *	top-level frame.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The frame given by the clientData argument is mapped.
 */
static void
FrameMap(
    ClientData clientData)
{              /* Pointer to frame structure. */
tkoFrame *frame = clientData;
    if(frame->win == NULL || *(frame->win) == NULL)
        return;

    /*
     * Wait for all other background events to be processed before mapping
     * window. This ensures that the window's correct geometry will have been
     * determined before it is first mapped, so that the window manager
     * doesn't get a false idea of its desired geometry.
     */

    Tcl_Preserve(frame);
    while(1) {
        if(Tcl_DoOneEvent(TCL_IDLE_EVENTS) == 0) {
            break;
        }

        /*
         * After each event, make sure that the window still exists and quit
         * if the window has been destroyed.
         */
        if(frame->win == NULL || *(frame->win) == NULL) {
            Tcl_Release(frame);
            return;
        }
    }
    Tk_MapWindow(*(frame->win));
    Tcl_Release(frame);
}

/*
 * FrameStructureProc --
 *
 *	This function is invoked whenever StructureNotify events occur for a
 *	window that's managed as label for the frame. This procudure's only
 *	purpose is to clean up when windows are deleted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window is disassociated from the frame when it is deleted.
 */
static void
FrameStructureProc(
    ClientData clientData,     /* Pointer to record describing frame. */
    XEvent * eventPtr)
{              /* Describes what just happened. */
tkoLabelframe *labelframe = clientData;

    /*
     * This should only happen in a labelframe but it doesn't hurt to be
     * careful.
     */
    if((eventPtr->type == DestroyNotify)
        && (labelframe->frame.type == TYPE_LABELFRAME)) {
        FrameLabelwinRemove(labelframe);
    }
}

/*
 * FrameLabelwinRemove --
 *
 * Results:
 *  None.
 *
 * Side effects:
 */
static void
FrameLabelwinRemove(
    tkoLabelframe * labelframe)
{
tkoFrame *frame = (tkoFrame *) labelframe;
Tcl_Obj *arrayName = TkoWidgetOptionVar(frame->object);
    labelframe->labelWin = NULL;
    if(arrayName == NULL)
        return;
    Tcl_ObjSetVar2(frame->interp, arrayName, TkoObj._labelwidget, TkoObj.empty,
        TCL_GLOBAL_ONLY);
    FrameWorldChanged(labelframe);
}

/*
 * FrameRequestProc --
 *
 *	This function is invoked whenever a window that's associated with a
 *	frame changes its requested dimensions.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The size and location on the screen of the window may change depending
 *	on the options specified for the frame.
 */
static void
FrameRequestProc(
    ClientData clientData,     /* Pointer to record for frame. */
    Tk_Window tkWin)
{              /* Window that changed its desired size. */
tkoFrame *frame = clientData;

    FrameWorldChanged(frame);
}

/*
 * FrameLostSlaveProc --
 *
 *	This function is invoked by Tk whenever some other geometry claims
 *	control over a slave that used to be managed by us.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Forgets all frame-related information about the slave.
 */
static void
FrameLostSlaveProc(
    ClientData clientData,     /* Frame structure for slave window that was
                                * stolen away. */
    Tk_Window tkWin            /* Tk's handle for the slave window. */)
{
    tkoLabelframe *labelframe = clientData;

    /*
     * This should only happen in a labelframe but it doesn't hurt to be
     * careful.
     */

    if(labelframe->frame.type == TYPE_LABELFRAME) {
        Tk_DeleteEventHandler(labelframe->labelWin, StructureNotifyMask,
            FrameStructureProc, labelframe);
        if(tkWin != Tk_Parent(labelframe->labelWin)) {
            Tk_UnmaintainGeometry(labelframe->labelWin, tkWin);
        }
        Tk_UnmapWindow(labelframe->labelWin);
        FrameLabelwinRemove(labelframe);
    }
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
