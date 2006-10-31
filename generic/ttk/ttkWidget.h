/* $Id: ttkWidget.h,v 1.1 2006/10/31 01:42:26 hobbs Exp $
 * Copyright (c) 2003, Joe English
 *
 * Helper routines for widget implementations.
 *
 * Require: ttkTheme.h.
 */

#ifndef WIDGET_H
#define WIDGET_H 1

/* State flags for 'flags' field.
 * @@@ todo: distinguish:
 * need reconfigure, need redisplay, redisplay pending
 */
#define WIDGET_DESTROYED	0x0001
#define REDISPLAY_PENDING 	0x0002	/* scheduled call to RedisplayWidget */
#define WIDGET_REALIZED		0x0010	/* set at first ConfigureNotify */
#define CURSOR_ON 		0x0020	/* See BlinkCursor() */
#define WIDGET_USER_FLAG        0x0100  /* 0x0100 - 0x8000 for user flags */

/*
 * Bit fields for OptionSpec 'mask' field:
 */
#define READONLY_OPTION 	0x1
#define STYLE_CHANGED   	0x2
#define GEOMETRY_CHANGED	0x4

/*
 * Core widget elements
 */
typedef struct WidgetSpec_ WidgetSpec;	/* Forward */

typedef struct
{
    Tk_Window tkwin;		/* Window associated with widget */
    Tcl_Interp *interp;		/* Interpreter associated with widget. */
    WidgetSpec *widgetSpec;	/* Widget class hooks */
    Tcl_Command widgetCmd;	/* Token for widget command. */
    Tk_OptionTable optionTable;	/* Option table */
    Ttk_Layout layout;  	/* Widget layout */

    /*
     * Storage for resources:
     */
    Tcl_Obj *takeFocusPtr;	/* Storage for -takefocus option */
    Tcl_Obj *cursorObj;		/* Storage for -cursor option */
    Tcl_Obj *styleObj;		/* Name of currently-applied style */
    Tcl_Obj *classObj;		/* Class name (readonly option) */

    Ttk_State state;		/* Current widget state */
    unsigned int flags;		/* internal flags, see above */

} WidgetCore;

/*
 * Subcommand specifications:
 */
typedef int (*WidgetSubcommandProc)(
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[], void *recordPtr);
typedef struct {
    const char *name;
    WidgetSubcommandProc command;
} WidgetCommandSpec;

extern int WidgetEnsembleCommand(	/* Run an ensemble command */
    WidgetCommandSpec *commands, int cmdIndex,
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[], void *recordPtr);

/*
 * Widget specifications:
 */
struct WidgetSpec_
{
    const char 		*className;	/* Widget class name */
    size_t 		recordSize;	/* #bytes in widget record */
    Tk_OptionSpec	*optionSpecs;	/* Option specifications */
    WidgetCommandSpec	*commands;	/* Widget instance subcommands */

    /*
     * Hooks:
     */
    int  	(*initializeProc)(Tcl_Interp *, void *recordPtr);
    void	(*cleanupProc)(void *recordPtr);
    int 	(*configureProc)(Tcl_Interp *, void *recordPtr, int flags);
    int 	(*postConfigureProc)(Tcl_Interp *, void *recordPtr, int flags);
    Ttk_Layout	(*getLayoutProc)(Tcl_Interp *,Ttk_Theme, void *recordPtr);
    int 	(*sizeProc)(void *recordPtr, int *widthPtr, int *heightPtr);
    void	(*layoutProc)(void *recordPtr);
    void	(*displayProc)(void *recordPtr, Drawable d);
};

/*
 * Common factors for widget implementations:
 */
extern int  NullInitialize(Tcl_Interp *, void *);
extern int  NullPostConfigure(Tcl_Interp *, void *, int);
extern void NullCleanup(void *recordPtr);
extern Ttk_Layout WidgetGetLayout(Tcl_Interp *, Ttk_Theme, void *recordPtr);
extern Ttk_Layout WidgetGetOrientedLayout(
    Tcl_Interp *, Ttk_Theme, void *recordPtr, Tcl_Obj *orientObj);
extern int  WidgetSize(void *recordPtr, int *w, int *h);
extern void WidgetDoLayout(void *recordPtr);
extern void WidgetDisplay(void *recordPtr, Drawable);

extern int CoreConfigure(Tcl_Interp*, void *, int mask);

/* Commands present in all widgets:
 */
extern int WidgetConfigureCommand(Tcl_Interp *, int, Tcl_Obj*const[], void *);
extern int WidgetCgetCommand(Tcl_Interp *, int, Tcl_Obj*const[], void *);
extern int WidgetInstateCommand(Tcl_Interp *, int, Tcl_Obj*const[], void *);
extern int WidgetStateCommand(Tcl_Interp *, int, Tcl_Obj*const[], void *);

/* Common widget commands:
 */
extern int WidgetIdentifyCommand(Tcl_Interp *, int, Tcl_Obj*const[], void *);

extern int WidgetConstructorObjCmd(ClientData,Tcl_Interp*,int,Tcl_Obj*CONST[]);

#define RegisterWidget(interp, name, specPtr) \
    Tcl_CreateObjCommand(interp, name, \
	WidgetConstructorObjCmd, (ClientData)specPtr,NULL)

/* WIDGET_TAKES_FOCUS --
 * Add this to the OptionSpecs table of widgets that
 * take keyboard focus during traversal to override
 * CoreOptionSpec's -takefocus default value:
 */
#define WIDGET_TAKES_FOCUS \
    {TK_OPTION_STRING, "-takefocus", "takeFocus", "TakeFocus", \
	"ttk::takefocus", Tk_Offset(WidgetCore, takeFocusPtr), -1, 0,0,0 }

/* WIDGET_INHERIT_OPTIONS(baseOptionSpecs) --
 * Add this at the end of an OptionSpecs table to inherit
 * the options from 'baseOptionSpecs'.
 */
#define WIDGET_INHERIT_OPTIONS(baseOptionSpecs) \
    {TK_OPTION_END, 0,0,0, NULL, -1,-1, 0, (ClientData)baseOptionSpecs, 0}

/*
 * Useful routines for use inside widget implementations:
 */
extern int WidgetDestroyed(WidgetCore *);
#define WidgetDestroyed(corePtr) ((corePtr)->flags & WIDGET_DESTROYED)

extern void WidgetChangeState(WidgetCore *,
	unsigned int setBits, unsigned int clearBits);

extern void TtkRedisplayWidget(WidgetCore *);
extern void TtkResizeWidget(WidgetCore *);

extern void TrackElementState(WidgetCore *);
extern void BlinkCursor(WidgetCore *);

/*
 * -state option values (compatibility)
 */
extern void CheckStateOption(WidgetCore *, Tcl_Obj *);

/*
 * Variable traces:
 */
typedef void (*Ttk_TraceProc)(void *recordPtr, const char *value);
typedef struct TtkTraceHandle_ Ttk_TraceHandle;

extern Ttk_TraceHandle *Ttk_TraceVariable(
    Tcl_Interp*, Tcl_Obj *varnameObj, Ttk_TraceProc callback, void *clientData);
extern void Ttk_UntraceVariable(Ttk_TraceHandle *);
extern int Ttk_FireTrace(Ttk_TraceHandle *);

/*
 * Utility routines for managing -image option:
 */
extern int GetImageList(
    Tcl_Interp *, WidgetCore *, Tcl_Obj *imageOption, Tk_Image **imageListPtr);
extern void FreeImageList(Tk_Image *);

/*
 * Virtual events:
 */
extern void SendVirtualEvent(Tk_Window tgtWin, const char *eventName);

/*
 * Helper routines for data accessor commands:
 */
extern int EnumerateOptions(
    Tcl_Interp *, void *recordPtr, Tk_OptionSpec *, Tk_OptionTable, Tk_Window);
extern int GetOptionValue(
    Tcl_Interp *, void *recordPtr, Tcl_Obj *optName, Tk_OptionTable, Tk_Window);

/*
 * Helper routines for scrolling widgets (see scroll.c).
 */
typedef struct {
    int 	first;		/* First visible item */
    int 	last;		/* Last visible item */
    int 	total;		/* Total #items */
    char 	*scrollCmd;	/* Widget option */
} Scrollable;

typedef struct ScrollHandleRec *ScrollHandle;

extern ScrollHandle CreateScrollHandle(WidgetCore *, Scrollable *);
extern void FreeScrollHandle(ScrollHandle);

extern int ScrollviewCommand(
    Tcl_Interp *interp, int objc, Tcl_Obj *const objv[], ScrollHandle);

extern void ScrollTo(ScrollHandle, int newFirst);
extern void Scrolled(ScrollHandle, int first, int last, int total);
extern void ScrollbarUpdateRequired(ScrollHandle);

/*
 * Tag sets (work in progress, half-baked)
 */

typedef struct TtkTag *Ttk_Tag;
typedef struct TtkTagTable *Ttk_TagTable;

extern Ttk_TagTable Ttk_CreateTagTable(Tk_OptionTable, int tagRecSize);
extern void Ttk_DeleteTagTable(Ttk_TagTable);

extern Ttk_Tag Ttk_GetTag(Ttk_TagTable, const char *tagName);
extern Ttk_Tag Ttk_GetTagFromObj(Ttk_TagTable, Tcl_Obj *);

extern Tcl_Obj **Ttk_TagRecord(Ttk_Tag);

extern int Ttk_GetTagListFromObj(
    Tcl_Interp *interp, Ttk_TagTable, Tcl_Obj *objPtr,
    int *nTags_rtn, void **taglist_rtn);

extern void Ttk_FreeTagList(void **taglist);


/*
 * Useful widget base classes:
 */
extern Tk_OptionSpec CoreOptionSpecs[];

/*
 * String tables for widget resource specifications:
 */

extern const char *TTKOrientStrings[];
extern const char *TTKCompoundStrings[];
extern const char *TTKDefaultStrings[];

/*
 * ... other option types...
 */
extern int TtkGetLabelAnchorFromObj(Tcl_Interp*,Tcl_Obj*,Ttk_PositionSpec *);

/*
 * Package initialiation routines:
 */
extern void RegisterElements(Tcl_Interp *);

#if defined(__WIN32__)
#define Ttk_PlatformInit Ttk_WinPlatformInit
extern int Ttk_WinPlatformInit(Tcl_Interp *);
#elif defined(MAC_OSX_TK)
#define Ttk_PlatformInit Ttk_MacPlatformInit
extern int Ttk_MacPlatformInit(Tcl_Interp *);
#else
#define Ttk_PlatformInit(interp) /* TTK_X11PlatformInit() */
#endif

#endif /* WIDGET_H */
