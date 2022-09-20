/*
 * tkoFrame.c --
 *
 *    This module implements "frame", "labelframe" and "toplevel" widgets
 *    for the Tk toolkit. Frames are windows with a background color and
 *    possibly a 3-D effect, but not much else in the way of attributes.
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
 * frame that currently exists for this process.
 *
 * ATTENTION!!!
 * tkWinWM.c will call TkInstallFromMenu() from file tkFrame.c for toplevels.
 * Inside these function a struct Frame and memeber menuName will be used.
 * We noe have to ensure that our structure has the same form as Frame.
 * Therefore we place some dummy arguments in the structure.
 */
typedef struct tkoFrame {
    Tko_Widget widget;
    enum FrameType type;       /* Type of widget, such as TYPE_FRAME. */
    char *dummy1;
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
    Tk_Window tkWinCreate;
    char *dummy2;
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
    Tk_Image bgimg;            /* Derived from -backgroundimage by calling
                                * Tk_GetImage, or NULL. */
    int tile;                  /* Whether to tile the bgimg. */
#ifndef TK_NO_DOUBLE_BUFFERING
    GC copyGC;                 /* GC for copying when double-buffering. */
#endif /* TK_NO_DOUBLE_BUFFERING */
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
#define REDRAW_PENDING        1
#define GOT_FOCUS        4

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
static int FrameConstructor(
    enum FrameType type,
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
static int FrameMethod_backgroundimage(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int FrameMethod_menu(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);

/*
 * Functions
 */
static void FrameComputeGeometry(
    tkoFrame * frame);
static void FrameDisplay(
    ClientData clientData);
static void    FrameDrawBackground(
    Tk_Window tkwin,
    Pixmap pixmap,
    int highlightWidth,
    int borderWidth,
    Tk_Image bgimg,
    int bgtile);
static void    FrameBgImageProc(
    ClientData clientData,
    int x,
    int y,
    int width,
    int height,
    int imgWidth,
    int imgHeight);
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
    { "-background" , "background", "Background", DEF_FRAME_BG_COLOR, TKO_OPTION_NULL, \
        NULL, TKO_SET_3DBORDER, offsetof(tkoFrame, border)}, \
    { "-backgroundimage", "backgroundImage", "BackgroundImage", DEF_FRAME_BG_IMAGE, 0, \
        FrameMethod_backgroundimage ,TKO_SET_NONE, 0}, \
    { "-bg" , "-background", NULL, NULL, 0, NULL,TKO_SET_NONE,0}, \
    { "-bgimg", "-backgroundimage", NULL, NULL, 0, NULL,TKO_SET_NONE,0}, \
    { "-bd" , "-borderwidth", NULL, NULL, 0, NULL, TKO_SET_NONE,0}, \
    { "-cursor" , "cursor", "Cursor", DEF_FRAME_CURSOR, 0, \
        NULL, TKO_SET_CURSOR, offsetof(tkoFrame, cursor)}, \
    { "-height" , "height", "Height", DEF_FRAME_HEIGHT, 0, \
        NULL, TKO_SET_PIXEL, offsetof(tkoFrame, height)}, \
    { "-highlightbackground", "highlightbackground", "highlightBackground", DEF_FRAME_HIGHLIGHT_BG, 0, \
        NULL, TKO_SET_XCOLOR, offsetof(tkoFrame, highlightBgColorPtr)}, \
    { "-highlightcolor", "highlightColor", "HighlightColor", DEF_FRAME_HIGHLIGHT, 0, \
        NULL, TKO_SET_XCOLOR, offsetof(tkoFrame, highlightColorPtr)}, \
    { "-highlightthickness" , "highlightThickness", "HighlightThickness", DEF_FRAME_HIGHLIGHT_WIDTH,  0, \
        NULL, TKO_SET_PIXEL, offsetof(tkoFrame, highlightWidth)}, \
    { "-padx" , "padX", "Pad", DEF_FRAME_PADX, 0, \
        NULL, TKO_SET_PIXEL, offsetof(tkoFrame, padX)}, \
    { "-pady" , "padY", "Pad", DEF_FRAME_PADY, 0, \
        NULL, TKO_SET_PIXEL, offsetof(tkoFrame, padY)}, \
    { "-takefocus" , "takeFocus", "TakeFocus", DEF_FRAME_TAKE_FOCUS,  0, \
        NULL, TKO_SET_STRING, 0}, \
    { "-tile", "tile", "Tile", DEF_FRAME_BG_TILE, 0, \
        NULL, TKO_SET_BOOLEAN, offsetof(tkoFrame, tile)}, \
    { "-width" , "width", "Width", DEF_FRAME_WIDTH,  0, \
        NULL, TKO_SET_PIXEL, offsetof(tkoFrame, width)}, \
    { NULL,NULL,NULL,NULL,0,NULL,TKO_SET_NONE,0}

/*
 * frameOptions --
 *  List of tko::frame options.
 */
static const Tko_WidgetOptionDefine frameOptions[] = {
    {"-class", "class", "Class", "TkoFrame", TKO_OPTION_READONLY,
    NULL, TKO_SET_CLASS, 0},
    {"-visual", "visual", "Visual", DEF_FRAME_VISUAL, TKO_OPTION_READONLY,
    NULL, TKO_SET_VISUAL, 0},
    {"-colormap", "colormap", "Colormap", DEF_FRAME_COLORMAP, TKO_OPTION_READONLY,
    NULL, TKO_SET_COLORMAP, 0},
    {"-container", "container", "Container", DEF_FRAME_CONTAINER, TKO_OPTION_READONLY,
    NULL, TKO_SET_CONTAINER, offsetof(tkoFrame, isContainer)},
    {"-borderwidth", "borderWidth", "BorderWidth", DEF_FRAME_BORDER_WIDTH, 0,
    NULL, TKO_SET_PIXEL, offsetof(tkoFrame, borderWidth)},
    {"-relief", "relief", "Relief", DEF_FRAME_RELIEF, 0,
    NULL, TKO_SET_RELIEF, offsetof(tkoFrame, relief)},
    FRAME_COMMONDEFINE
};

/*
 * toplevelOptions --
 *  List of tko::toplevel options.
 */
static const Tko_WidgetOptionDefine toplevelOptions[] = {
    {"-screen", "screen", "Screen", "", TKO_OPTION_READONLY,
    NULL, TKO_SET_STRING, 0},
    {"-class", "class", "Class", "TkoToplevel", TKO_OPTION_READONLY,
    NULL, TKO_SET_CLASS, 0},
    {"-container", "container", "Container", DEF_FRAME_CONTAINER, TKO_OPTION_READONLY,
    NULL, TKO_SET_CONTAINER, offsetof(tkoFrame, isContainer)},
    {"-use", "use", "Use", DEF_TOPLEVEL_USE, TKO_OPTION_READONLY|TKO_OPTION_NULL,
    NULL, TKO_SET_USE, offsetof(tkoFrame, useThis)},
    {"-visual", "visual", "Visual", DEF_FRAME_VISUAL, TKO_OPTION_READONLY,
    NULL, TKO_SET_VISUAL, 0},
    {"-colormap", "colormap", "Colormap", DEF_FRAME_COLORMAP, TKO_OPTION_READONLY,
    NULL, TKO_SET_COLORMAP, 0},
    {"-borderwidth", "borderWidth", "BorderWidth", DEF_FRAME_BORDER_WIDTH, 0,
    NULL, TKO_SET_PIXEL, offsetof(tkoFrame, borderWidth)},
    {"-menu", "menu", "Menu", DEF_TOPLEVEL_MENU, TKO_OPTION_NULL,
    FrameMethod_menu, TKO_SET_NONE, 0},
    {"-relief", "relief", "Relief", DEF_FRAME_RELIEF, 0,
    NULL, TKO_SET_RELIEF, offsetof(tkoFrame, relief)},
    FRAME_COMMONDEFINE
};

/*
 * labelframeOptions --
 *  List of tko::labelframe options.
 */
static const Tko_WidgetOptionDefine labelframeOptions[] = {
    {"-class", "class", "Class", "TkoLabelframe", TKO_OPTION_READONLY,
    NULL, TKO_SET_CLASS, 0},
    {"-visual", "visual", "Visual", DEF_FRAME_VISUAL, TKO_OPTION_READONLY,
    NULL, TKO_SET_VISUAL, 0},
    {"-colormap", "colormap", "Colormap", DEF_FRAME_COLORMAP, TKO_OPTION_READONLY,
    NULL, TKO_SET_COLORMAP, 0},
    {"-borderwidth", "borderWidth", "BorderWidth", DEF_LABELFRAME_BORDER_WIDTH, 0,
    NULL, TKO_SET_PIXEL, offsetof(tkoFrame, borderWidth)},
    {"-fg", "-foreground", NULL, NULL, 0, NULL, TKO_SET_NONE, 0},
    {"-font", "font", "Font", DEF_LABELFRAME_FONT, 0,
    NULL, TKO_SET_FONT, offsetof(tkoLabelframe, tkfont)},
    {"-foreground", "foreground", "Foreground", DEF_LABELFRAME_FG, 0,
    NULL, TKO_SET_XCOLOR, offsetof(tkoLabelframe, textColorPtr)},
    {"-labelanchor", "labelAnchor", "LabelAnchor", DEF_LABELFRAME_LABELANCHOR, 0,
    FrameMethod_labelanchor, TKO_SET_NONE, 0},
    {"-labelwidget", "labelWidget", "LabelWidget", "",0,
    FrameMethod_labelwidget, TKO_SET_NONE, 0},
    {"-relief", "relief", "Relief", DEF_LABELFRAME_RELIEF, 0,
    NULL, TKO_SET_RELIEF, offsetof(tkoFrame, relief)},
    {"-text", "text", "Text", DEF_LABELFRAME_TEXT, 0,
    NULL, TKO_SET_TCLOBJ, offsetof(tkoLabelframe, textPtr)},
    FRAME_COMMONDEFINE
};

/*
 * Definition of object methods created in Tko_FrameInit() function.
 */

/*
 * frameMethods --
 *    List of used public and private tko::frame methods.
 */
static Tcl_MethodType frameMethods[] = {
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, FrameConstructorFrame, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, FrameDestructor, NULL, NULL},
    {-1, NULL, NULL, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "_tko_configure", FrameMethod_tko_configure,
            NULL, NULL},
    {-1, NULL, NULL, NULL, NULL}
};

/*
 * labelframeMethods --
 *    List of used public and private tko::labelframe methods.
 */
static Tcl_MethodType labelframeMethods[] = {
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, FrameConstructorLabelframe, NULL,
            NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, FrameDestructor, NULL, NULL},
    {-1, NULL, NULL, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "_tko_configure", FrameMethod_tko_configure,
            NULL, NULL},
    {-1, NULL, NULL, NULL, NULL}
};

/*
 * toplevelMethods --
 *    List of used public and private tko::toplevel methods.
 */
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
 *    A standard Tcl result.
 *
 * Side effects:
 *  Create new oo::class's.
 */
int
Tko_FrameInit(
    Tcl_Interp * interp)
{              /* Tcl interpreter. */
    Tcl_Obj *tmpPtr;
    int ret;

    /*
     * ::tko::toplevel
     */
    tmpPtr = Tcl_NewStringObj("::tko::toplevel", -1);
    Tcl_IncrRefCount(tmpPtr);
    ret = Tko_WidgetClassDefine(interp, tmpPtr,
        toplevelMethods, toplevelOptions);
    Tcl_DecrRefCount(tmpPtr);
    if (ret != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * ::tko::frame
     */
    tmpPtr = Tcl_NewStringObj("::tko::frame", -1);
    Tcl_IncrRefCount(tmpPtr);
    ret = Tko_WidgetClassDefine(interp, tmpPtr,
        frameMethods, frameOptions);
    Tcl_DecrRefCount(tmpPtr);
    if (ret != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * ::tko::labelframe
     */
    tmpPtr = Tcl_NewStringObj("::tko::labelframe", -1);
    Tcl_IncrRefCount(tmpPtr);
    ret = Tko_WidgetClassDefine(interp, tmpPtr,
        labelframeMethods, labelframeOptions);
    Tcl_DecrRefCount(tmpPtr);
    if (ret != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * FrameConstructorFrame --
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *  Call common constructor for frames.
 */
static int
FrameConstructorFrame(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    (void)dummy;

    return FrameConstructor(TYPE_FRAME, interp, context, objc, objv);
}

/*
 * FrameConstructorLabelframe --
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *  Call common constructor for labelframes.
 */
static int
FrameConstructorLabelframe(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    (void)dummy;
    return FrameConstructor(TYPE_LABELFRAME, interp, context, objc, objv);
}

/*
 * FrameConstructorToplevel --
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *  Call common constructor for toplevels.
 */
static int
FrameConstructorToplevel(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    (void)dummy;
    return FrameConstructor(TYPE_TOPLEVEL, interp, context, objc, objv);
}

/*
 * FrameConstructor --
 *
 * Common part of all widget contructors.
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *  Create new widget and options.
 *    Set readonly options and default option values.
 */
static int
FrameConstructor(
    enum FrameType type,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Object object;
    Tko_Widget *widget;
    tkoFrame *frame;
    Tcl_Obj *myArglist;
    int skip;
    Tko_WidgetCreateMode createMode;

    /* Get current object. Should not fail? */
    if ((object = Tcl_ObjectContextObject(context)) == NULL) {
        return TCL_ERROR;
    }
    if (type == TYPE_FRAME) {
        frame = (tkoFrame *)ckalloc(sizeof(tkoFrame));
        assert(frame);
        memset(frame, 0, sizeof(tkoFrame));
        createMode = TKO_CREATE_WIDGET;
    }
    else if (type == TYPE_LABELFRAME) {
        tkoLabelframe *labelframe;
        labelframe = (tkoLabelframe *)ckalloc(sizeof(tkoLabelframe));
        assert(labelframe);
        memset(labelframe, 0, sizeof(tkoLabelframe));
        frame = (tkoFrame *)labelframe;
        labelframe->textPtr = NULL;
        labelframe->tkfont = NULL;
        labelframe->textColorPtr = NULL;
        labelframe->labelAnchor = LABELANCHOR_NW;
        labelframe->labelWin = NULL;
        labelframe->textGC = NULL;
        labelframe->textLayout = NULL;
        /*labelframe->labelBox */
        labelframe->labelReqWidth = 0;
        labelframe->labelReqHeight = 0;
        labelframe->labelTextX = 0;
        labelframe->labelTextY = 0;
        createMode = TKO_CREATE_WIDGET;
    }
    else if (type == TYPE_TOPLEVEL) {
        frame = (tkoFrame *)ckalloc(sizeof(tkoFrame));
        assert(frame);
        memset(frame, 0, sizeof(tkoFrame));
        createMode = TKO_CREATE_TOPLEVEL;
    }
    else {
        Tcl_WrongNumArgs(interp, 1, objv, "internal type error");
        return TCL_ERROR;
    }
    widget = (Tko_Widget *)frame;
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
    frame->cursor = NULL;
    frame->isContainer = 0;
    frame->useThis = NULL;
    frame->flags = 0;
    frame->padX = 0;
    frame->padY = 0;
    frame->mask = ExposureMask | StructureNotifyMask | FocusChangeMask;
    frame->bgimg = NULL;
#ifndef TK_NO_DOUBLE_BUFFERING
    frame->copyGC = NULL;
#endif
    frame->tile = 0;
    if (type == TYPE_TOPLEVEL) {
        frame->mask |= ActivateMask;
    }
    skip = Tcl_ObjectContextSkippedArgs(context);
    if (objc - skip > 0) {
        myArglist = Tcl_NewListObj(objc - skip, &objv[skip]);
    }
    else {
        myArglist = Tcl_NewListObj(0, NULL);
    }
    if (Tko_WidgetCreate(&(frame->widget), interp, object, createMode,
        myArglist) != TCL_OK) {
        Tcl_DecrRefCount(myArglist);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(myArglist);
    frame->tkWinCreate = widget->tkWin;
    if (frame->isContainer && frame->useThis != NULL) {
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
    if (type == TYPE_TOPLEVEL) {
        Tk_GeometryRequest(widget->tkWin, 200, 200);
    }

    /*
    * Store backreference to frame widget in window structure.
    */

    Tk_SetClassProcs(widget->tkWin, &frameClass, frame);

    /*
    * Mark Tk frames as suitable candidates for [wm manage].
    */

    ((TkWindow *) widget->tkWin)->flags |= TK_WM_MANAGEABLE;

    Tk_CreateEventHandler(widget->tkWin, frame->mask, FrameEventProc, frame);

    if (type == TYPE_TOPLEVEL) {
        Tcl_DoWhenIdle(FrameMap, frame);
    }

    return TCL_OK;
}

/*
 * FrameDestructor --
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *  Delete widget ressources.
 */
static int
FrameDestructor(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tko_Widget *widget;
    (void)dummy;
    (void)interp;
    (void)objc;
    (void)objv;

    if((widget = (Tko_Widget *)Tko_WidgetClientData(context)) != NULL) {
        tkoFrame *frame = (tkoFrame *)widget;
        tkoLabelframe *labelframe = (tkoLabelframe *) widget;
        Tcl_Preserve(widget);

        if(widget->tkWin) {
            Tk_DeleteEventHandler(widget->tkWin, frame->mask, FrameEventProc, frame);
        }
        if(widget->display != NULL) {
#ifndef TK_NO_DOUBLE_BUFFERING
            if (frame->copyGC != NULL) {
                Tk_FreeGC(widget->display, frame->copyGC);
            }
            frame->copyGC = NULL;
#endif /* TK_NO_DOUBLE_BUFFERING */
            if(frame->cursor != NULL) {
                Tk_FreeCursor(widget->display, frame->cursor);
            }
            frame->cursor = NULL;
        }
        if (frame->bgimg != NULL) {
            Tk_FreeImage(frame->bgimg);
        }
        frame->bgimg = NULL;
        frame->flags = 0;
        Tcl_CancelIdleCall(FrameDisplay, frame);
        Tcl_CancelIdleCall(FrameMap, frame);

        if(frame->menuName != NULL && frame->tkWinCreate) {
            Tk_SetWindowMenubar(frame->widget.interp, frame->tkWinCreate, frame->menuName, NULL);
            ckfree(frame->menuName);
            frame->menuName = NULL;
        }
        if(frame->type == TYPE_LABELFRAME && labelframe->labelWin) {
            Tk_ManageGeometry(labelframe->labelWin, NULL, NULL);
            if(widget->tkWin && (widget->tkWin != Tk_Parent(labelframe->labelWin))) {
                Tk_UnmaintainGeometry(labelframe->labelWin, widget->tkWin);
            }
            Tk_UnmapWindow(labelframe->labelWin);
            labelframe->labelWin = NULL;
        }
        if (frame->useThis) {
            Tcl_DecrRefCount(frame->useThis);
        }
        if (frame->type == TYPE_LABELFRAME) {
            if (labelframe->textLayout) {
                Tk_FreeTextLayout(labelframe->textLayout);
            }
            if (labelframe->textGC != NULL && widget->display != NULL) {
                Tk_FreeGC(widget->display, labelframe->textGC);
            }
        }
        if (frame->border) {
            Tk_Free3DBorder(frame->border);
        }
        if (frame->colormap != None && widget->display != NULL) {
            Tk_FreeColormap(widget->display, frame->colormap);
        }
        if (frame->highlightBgColorPtr != NULL) {
            Tk_FreeColor(frame->highlightBgColorPtr);
        }
        if (frame->highlightColorPtr != NULL) {
            Tk_FreeColor(frame->highlightColorPtr);
        }
        Tko_WidgetDestroy(context);
        Tcl_Release(frame);
    }
    return TCL_OK;
}

/*
 * FrameMethod_tko_configure --
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *  After configure step.
 */
static int
FrameMethod_tko_configure(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tko_Widget *widget;
    tkoFrame *frame;
    (void)dummy;
    (void)interp;
    (void)objc;
    (void)objv;

    if((widget = (Tko_Widget *)Tko_WidgetClientData(context)) == NULL
        || widget->tkWin == NULL) {
        return TCL_ERROR;
    }
    frame = (tkoFrame *)widget;

    if(frame->border != NULL) {
        Tk_SetBackgroundFromBorder(widget->tkWin, frame->border);
    } else {
        Tk_SetWindowBackgroundPixmap(widget->tkWin, None);
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
    return TCL_OK;
}

/*
 * FrameMethod_labelanchor --
 *
 * Process -labelanchor option.
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *  Set new option value.
 */
static int
FrameMethod_labelanchor(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    int index, code;
    tkoFrame *frame;
    tkoLabelframe *labelframe;
    Tcl_Obj *value;
    static const char *const labelAnchorStrings[] = {
        "e", "en", "es", "n", "ne", "nw", "s", "se", "sw", "w", "wn", "ws",
        NULL
    };
    (void)dummy;

    if((frame =
            (tkoFrame *)Tko_WidgetClientData(context)) == NULL
        || (value =
            Tko_WidgetOptionGet(&frame->widget, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }
    labelframe = (tkoLabelframe *)frame;
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
 *    A standard Tcl result.
 *
 * Side effects:
 *  Set new option value.
 */
static int
FrameMethod_labelwidget(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tko_Widget *widget;
    Tk_Window oldWindow = NULL;
    Tk_Window newWindow = NULL;
    Tk_Window ancestor, parent, sibling = NULL;
    tkoLabelframe *labelframe;
    Tcl_Obj *value;
    (void)dummy;

    if((widget = (Tko_Widget *)Tko_WidgetClientData(context)) == NULL
        || widget->tkWin == NULL
        || (value = Tko_WidgetOptionGet(widget, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }
    labelframe = (tkoLabelframe *)widget;

    if(value == NULL || Tcl_GetCharLength(value) == 0) {
        newWindow = NULL;
    } else if(TkGetWindowFromObj(interp, widget->tkWin, value, &newWindow) != TCL_OK) {
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
            for(ancestor = widget->tkWin;; ancestor = Tk_Parent(ancestor)) {
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
            if(newWindow == widget->tkWin) {
                goto badLabelWindow;
            }
        }
        if(oldWindow != NULL) {
            Tk_DeleteEventHandler(oldWindow, StructureNotifyMask,
                FrameStructureProc, labelframe);
            Tk_ManageGeometry(oldWindow, NULL, NULL);
            Tk_UnmaintainGeometry(oldWindow, widget->tkWin);
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
 * FrameMethod_backgroundimage --
 *
 * Process -backgroundimage option.
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *  Set new option value.
 */
static int
FrameMethod_backgroundimage(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tko_Widget *widget;
    tkoFrame *frame;
    Tcl_Obj *value;
    Tk_Image image;
    (void)dummy;

    if((widget = (Tko_Widget *)Tko_WidgetClientData(context)) == NULL
        || widget->tkWin == NULL
        || (value = Tko_WidgetOptionGet(widget, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }
    frame = (tkoFrame *)widget;
    /* check on widget destroyed */
    if(widget->tkWin == NULL)
        return TCL_OK;
    /* try to create new image */
    if(value == NULL || Tcl_GetCharLength(value) == 0) {
        image = NULL;
    } else {
        image = Tk_GetImage(interp, widget->tkWin,
            Tcl_GetString(value), FrameBgImageProc, frame);
        if (image == NULL) {
            return TCL_ERROR;
        }
    }
    if (frame->bgimg) {
        Tk_FreeImage(frame->bgimg);
    }
    frame->bgimg = image;
    return TCL_OK;
}

/*
* FrameMethod_menu --
*
* Process -menu option.
*
* Results:
*    A standard Tcl result.
*
* Side effects:
*  Set new option value.
*/
static int
FrameMethod_menu(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tko_Widget *widget;
    tkoFrame *frame;
    Tcl_Obj *value;
    char *newMenu;
    int length;
    (void)dummy;

    if((widget = (Tko_Widget *)Tko_WidgetClientData(context)) == NULL
        || widget->tkWin == NULL
        || (value = Tko_WidgetOptionGet(widget, objv[objc - 1])) == NULL) {
        return TCL_ERROR;
    }
    frame = (tkoFrame *)widget;

    newMenu = Tcl_GetStringFromObj(value, &length);
    if (length==0) {
        newMenu = NULL;
    }
    if ((((newMenu == NULL) && (frame->menuName != NULL))
        || ((newMenu != NULL) && (frame->menuName == NULL))
        || ((newMenu != NULL) && (frame->menuName != NULL)
            && strcmp(newMenu, frame->menuName) != 0))
        && frame->type == TYPE_TOPLEVEL) {
        Tk_SetWindowMenubar(interp, widget->tkWin, frame->menuName, newMenu);
        if (frame->menuName) { ckfree(frame->menuName); }
        if (length) {
            frame->menuName = (char *)ckalloc(length + 1);
            assert(frame->menuName);
            strncpy(frame->menuName,newMenu,length);
            frame->menuName[length] = '\0';
        }
        else {
            frame->menuName = NULL;
        }
    }
    return TCL_OK;
}

/*
 * FrameWorldChanged --
 *
 *    This function is called when the world has changed in some way and the
 *    widget needs to recompute all its graphics contexts and determine its
 *    new geometry.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Frame will be relayed out and redisplayed.
 */
static void
FrameWorldChanged(
    ClientData clientData)
{              /* Information about widget. */
    Tko_Widget *widget = (Tko_Widget *)clientData;
    tkoFrame *frame = (tkoFrame *)clientData;
    tkoLabelframe *labelframe = (tkoLabelframe *)clientData;
    XGCValues gcValues;
    GC  gc;
    int anyTextLabel, anyWindowLabel;
    int bWidthLeft, bWidthRight, bWidthTop, bWidthBottom;
    const char *labelText;

    if (widget->tkWin == NULL) {
        return;
    }

    anyTextLabel = (frame->type == TYPE_LABELFRAME) &&
        (labelframe->textPtr != NULL) && (labelframe->labelWin == NULL);
    anyWindowLabel = (frame->type == TYPE_LABELFRAME) &&
        (labelframe->labelWin != NULL);

#ifndef TK_NO_DOUBLE_BUFFERING
    gcValues.graphics_exposures = False;
    gc = Tk_GetGC(widget->tkWin, GCGraphicsExposures, &gcValues);
    if (frame->copyGC != NULL) {
        Tk_FreeGC(widget->display, frame->copyGC);
    }
    frame->copyGC = gc;
#endif /* TK_NO_DOUBLE_BUFFERING */

    if(frame->type == TYPE_LABELFRAME) {
        /*
         * The textGC is needed even in the labelWin case, so it's always
         * created for a labelframe.
         */

        gcValues.font = Tk_FontId(labelframe->tkfont);
        gcValues.foreground = labelframe->textColorPtr->pixel;
        gcValues.graphics_exposures = False;
        gc = Tk_GetGC(widget->tkWin, GCForeground | GCFont | GCGraphicsExposures,
            &gcValues);
        if(labelframe->textGC != NULL) {
            Tk_FreeGC(widget->display, labelframe->textGC);
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

    Tk_SetInternalBorderEx(widget->tkWin, bWidthLeft, bWidthRight, bWidthTop,
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
        Tk_SetMinimumRequestSize(widget->tkWin, minwidth, minheight);
    }

    if((frame->width > 0) || (frame->height > 0)) {
        Tk_GeometryRequest(widget->tkWin, frame->width, frame->height);
    }

    if(Tk_IsMapped(widget->tkWin)) {
        if(!(frame->flags & REDRAW_PENDING)) {
            Tcl_DoWhenIdle(FrameDisplay, frame);
        }
        frame->flags |= REDRAW_PENDING;
    }
}

/*
 * FrameComputeGeometry --
 *
 *    This function is called to compute various geometrical information for
 *    a frame, such as where various things get displayed. It's called when
 *    the window is reconfigured.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Display-related numbers get changed in *frame.
 */

static void
FrameComputeGeometry(
    tkoFrame * frame)
{
    int otherWidth, otherHeight, otherWidthT, otherHeightT, padding;
    int maxWidth, maxHeight;
    Tko_Widget *widget = (Tko_Widget *)frame;
    tkoLabelframe *labelframe = (tkoLabelframe *) frame;

    /*
     * We have nothing to do here unless there is a label.
     */
    if (widget->tkWin == NULL || frame->type != TYPE_LABELFRAME) {
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

    maxHeight = Tk_Height(widget->tkWin);
    maxWidth = Tk_Width(widget->tkWin);

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

    otherWidth = Tk_Width(widget->tkWin) - labelframe->labelBox.width;
    otherHeight = Tk_Height(widget->tkWin) - labelframe->labelBox.height;
    otherWidthT = Tk_Width(widget->tkWin) - labelframe->labelReqWidth;
    otherHeightT = Tk_Height(widget->tkWin) - labelframe->labelReqHeight;
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
 *    This function is invoked to display a frame widget.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Commands are output to X to display the frame in its current mode.
 */
static void
FrameDisplay(
    ClientData clientData /* Information about widget. */)
{
    Tko_Widget *widget = (Tko_Widget *)clientData;
    tkoFrame *frame = (tkoFrame *)clientData;
    int bdX1, bdY1, bdX2, bdY2, hlWidth;
    Pixmap pixmap;
    TkRegion clipRegion = NULL;

    if (widget->tkWin == NULL) {
        return;
    }

    frame->flags &= ~REDRAW_PENDING;
    if(!Tk_IsMapped(widget->tkWin)) {
        return;
    }

    /*
     * Highlight shall always be drawn if it exists, so do that first.
     */

    hlWidth = frame->highlightWidth;

    if(hlWidth != 0) {
        GC  fgGC, bgGC;

        bgGC = Tk_GCForColor(frame->highlightBgColorPtr,
            Tk_WindowId(widget->tkWin));
        if(frame->flags & GOT_FOCUS) {
            fgGC = Tk_GCForColor(frame->highlightColorPtr,
                Tk_WindowId(widget->tkWin));
            Tk_DrawHighlightBorder(widget->tkWin, fgGC, bgGC, hlWidth,
                Tk_WindowId(widget->tkWin));
        } else {
            Tk_DrawHighlightBorder(widget->tkWin, bgGC, bgGC, hlWidth,
                Tk_WindowId(widget->tkWin));
        }
    }

    /*
     * If -background is set to "", no interior is drawn.
     */

    if(frame->border == NULL) {
        return;
    }

#ifndef TK_NO_DOUBLE_BUFFERING
    /*
     * In order to avoid screen flashes, this function redraws the frame into
     * off-screen memory, then copies it back on-screen in a single operation.
     * This means there's no point in time where the on-screen image has been
     * cleared.
     */

    pixmap = Tk_GetPixmap(widget->display, Tk_WindowId(widget->tkWin),
        Tk_Width(widget->tkWin), Tk_Height(widget->tkWin), Tk_Depth(widget->tkWin));
#else
    pixmap = Tk_WindowId(widget->tkWin);
#endif /* TK_NO_DOUBLE_BUFFERING */

    if(frame->type != TYPE_LABELFRAME) {
        /*
         * Pass to platform specific draw function. In general, it just draws
         * a simple rectangle, but it may "theme" the background.
         */

    noLabel:
        TkpDrawFrameEx(widget->tkWin, pixmap, frame->border,
            hlWidth, frame->borderWidth, frame->relief);
        if (frame->bgimg) {
            FrameDrawBackground(widget->tkWin, pixmap, hlWidth,
                frame->borderWidth, frame->bgimg, frame->tile);
        }
    } else {
        tkoLabelframe *labelframe = (tkoLabelframe *) frame;

        if((labelframe->textPtr == NULL) && (labelframe->labelWin == NULL)) {
            goto noLabel;
        }

        /*
         * Clear the pixmap.
         */

        Tk_Fill3DRectangle(widget->tkWin, pixmap, frame->border, 0, 0,
            Tk_Width(widget->tkWin), Tk_Height(widget->tkWin), 0,
            TK_RELIEF_FLAT);

        /*
         * Calculate how the label affects the border's position.
         */

        bdX1 = bdY1 = hlWidth;
        bdX2 = Tk_Width(widget->tkWin) - hlWidth;
        bdY2 = Tk_Height(widget->tkWin) - hlWidth;

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

        Tk_Draw3DRectangle(widget->tkWin, pixmap, frame->border, bdX1, bdY1,
            bdX2 - bdX1, bdY2 - bdY1, frame->borderWidth, frame->relief);

        if(labelframe->labelWin == NULL) {
            /*
             * Clear behind the label
             */

            Tk_Fill3DRectangle(widget->tkWin, pixmap,
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
                TkSetRegion(widget->display, labelframe->textGC, clipRegion);
            }

            Tk_DrawTextLayout(widget->display, pixmap,
                labelframe->textGC, labelframe->textLayout,
                labelframe->labelTextX + LABELSPACING,
                labelframe->labelTextY + LABELSPACING, 0, -1);

            if(clipRegion != NULL) {
                XSetClipMask(widget->display, labelframe->textGC, None);
                TkDestroyRegion(clipRegion);
            }
        } else {
            /*
             * Reposition and map the window (but in different ways depending
             * on whether the frame is the window's parent).
             */

            if(widget->tkWin == Tk_Parent(labelframe->labelWin)) {
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
                Tk_MaintainGeometry(labelframe->labelWin, widget->tkWin,
                    labelframe->labelBox.x, labelframe->labelBox.y,
                    labelframe->labelBox.width, labelframe->labelBox.height);
            }
        }
    }
#ifndef TK_NO_DOUBLE_BUFFERING
    /*
    * Everything's been redisplayed; now copy the pixmap onto the screen
    * and free up the pixmap.
    */

    XCopyArea(widget->display, pixmap, Tk_WindowId(widget->tkWin),
        frame->copyGC, hlWidth, hlWidth,
        (unsigned)(Tk_Width(widget->tkWin) - 2 * hlWidth),
        (unsigned)(Tk_Height(widget->tkWin) - 2 * hlWidth),
        hlWidth, hlWidth);
    Tk_FreePixmap(widget->display, pixmap);
#endif /* TK_NO_DOUBLE_BUFFERING */
}

/*
 * FrameEventProc --
 *
 *    This function is invoked by the Tk dispatcher on structure changes to
 *    a frame. For frames with 3D borders, this function is also invoked for
 *    exposures.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    When the window gets deleted, internal structures get cleaned up.
 *    When it gets exposed, it is redisplayed.
 */
static void
FrameEventProc(
    ClientData clientData,     /* Information about window. */
    register XEvent * eventPtr)
{              /* Information about event. */
    Tko_Widget *widget = (Tko_Widget *)clientData;
    tkoFrame *frame = (tkoFrame *)clientData;
    if(eventPtr->type == DestroyNotify || widget->tkWin == NULL
        || widget->tkWin == NULL)
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
        Tk_SetMainMenubar(frame->widget.interp, widget->tkWin, frame->menuName);
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
 *    This function is invoked as a when-idle handler to map a newly-created
 *    top-level frame.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The frame given by the clientData argument is mapped.
 */
static void
FrameMap(
    ClientData clientData)
{              /* Pointer to frame structure. */
    Tko_Widget *widget = (Tko_Widget *)clientData;
    tkoFrame *frame = (tkoFrame *)clientData;

    if (widget->tkWin == NULL) {
        return;
    }

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
        if(widget->tkWin == NULL) {
            Tcl_Release(frame);
            return;
        }
    }
    Tk_MapWindow(widget->tkWin);
    Tcl_Release(frame);
}


/*
 * FrameStructureProc --
 *
 *    This function is invoked whenever StructureNotify events occur for a
 *    window that's managed as label for the frame. This procudure's only
 *    purpose is to clean up when windows are deleted.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The window is disassociated from the frame when it is deleted.
 */
static void
FrameStructureProc(
    ClientData clientData,     /* Pointer to record describing frame. */
    XEvent * eventPtr)
{              /* Describes what just happened. */
    tkoLabelframe *labelframe = (tkoLabelframe *)clientData;

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
    Tcl_Obj *tmpPtr;

    labelframe->labelWin = NULL;
    tmpPtr = Tcl_NewStringObj("-labelwidget", -1);
    Tcl_IncrRefCount(tmpPtr);
    Tko_WidgetOptionSet(&frame->widget, tmpPtr, Tcl_NewStringObj("", 0));
    Tcl_DecrRefCount(tmpPtr);
    FrameWorldChanged(labelframe);
}

/*
 * FrameRequestProc --
 *
 *    This function is invoked whenever a window that's associated with a
 *    frame changes its requested dimensions.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The size and location on the screen of the window may change depending
 *    on the options specified for the frame.
 */
static void
FrameRequestProc(
    ClientData clientData,     /* Pointer to record for frame. */
    Tk_Window tkWin)
{              /* Window that changed its desired size. */
    tkoFrame *frame = (tkoFrame *)clientData;
    (void)tkWin;

    FrameWorldChanged(frame);
}

/*
 * FrameLostSlaveProc --
 *
 *    This function is invoked by Tk whenever some other geometry claims
 *    control over a slave that used to be managed by us.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Forgets all frame-related information about the slave.
 */
static void
FrameLostSlaveProc(
    ClientData clientData,     /* Frame structure for slave window that was
                                * stolen away. */
    Tk_Window tkWin            /* Tk's handle for the slave window. */)
{
    tkoLabelframe *labelframe = (tkoLabelframe *)clientData;

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

/*
 *----------------------------------------------------------------------
 *
 * FrameBgImageProc --
 *
 *    This function is invoked by the image code whenever the manager for an
 *    image does something that affects the size or contents of an image
 *    displayed on a frame's background.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Arranges for the button to get redisplayed.
 *
 *----------------------------------------------------------------------
 */

static void
FrameBgImageProc(
    ClientData clientData,    /* Pointer to widget record. */
    int x, int y,        /* Upper left pixel (within image) that must
                 * be redisplayed. */
    int width, int height,    /* Dimensions of area to redisplay (might be
                 * <= 0). */
    int imgWidth, int imgHeight)/* New dimensions of image. */
{
    Tko_Widget *widget = (Tko_Widget *)clientData;
    tkoFrame *frame = (tkoFrame *)clientData;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)imgWidth;
    (void)imgHeight;

    if (widget->tkWin == NULL) return;

    /*
     * Changing the background image never alters the dimensions of the frame.
     */

    if (Tk_IsMapped(widget->tkWin) &&
        !(frame->flags & REDRAW_PENDING)) {
        Tcl_DoWhenIdle(FrameDisplay, frame);
        frame->flags |= REDRAW_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FrameDrawBackground --
 *
 *    This function draws the background image of a rectangular frame area.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Draws inside the tkwin area.
 *
 *----------------------------------------------------------------------
 */

static void
FrameDrawBackground(
    Tk_Window tkwin,
    Pixmap pixmap,
    int highlightWidth,
    int borderWidth,
    Tk_Image bgimg,
    int bgtile)
{
    int width, height;            /* Area to paint on. */
    int imageWidth, imageHeight;    /* Dimensions of image. */
    const int bw = highlightWidth + borderWidth;

    Tk_SizeOfImage(bgimg, &imageWidth, &imageHeight);
    width = Tk_Width(tkwin) - 2*bw;
    height = Tk_Height(tkwin) - 2*bw;

    if (bgtile) {
        /*
         * Draw the image tiled in the widget (inside the border).
         */

        int x, y;

        for (x = bw; x - bw < width; x += imageWidth) {
            int w = imageWidth;
            if (x - bw + imageWidth > width) {
            w = (width + bw) - x;
            }
            for (y = bw; y < height + bw; y += imageHeight) {
            int h = imageHeight;
            if (y - bw + imageHeight > height) {
                h = (height + bw) - y;
            }
            Tk_RedrawImage(bgimg, 0, 0, w, h, pixmap, x, y);
            }
        }
    } else {
        /*
         * Draw the image centred in the widget (inside the border).
         */

        int x, y, xOff, yOff, w, h;

        if (width > imageWidth) {
            x = 0;
            xOff = (Tk_Width(tkwin) - imageWidth) / 2;
            w = imageWidth;
        } else {
            x = (imageWidth - width) / 2;
            xOff = bw;
            w = width;
        }
        if (height > imageHeight) {
            y = 0;
            yOff = (Tk_Height(tkwin) - imageHeight) / 2;
            h = imageHeight;
        } else {
            y = (imageHeight - height) / 2;
            yOff = bw;
            h = height;
        }
        Tk_RedrawImage(bgimg, x, y, w, h, pixmap, xOff, yOff);
    }
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
