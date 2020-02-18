/*
 * tkoWidget.h --
 *
 *    Header file for the internals of the tko widget package.
 *
 * Copyright (c) 2019 Rene Zaumseil
 *
 */

#ifndef _TKOWIDGET_H
#define _TKOWIDGET_H

#include "tcl.h"
#include "tclInt.h"
#include "tclOO.h"
#include "tk.h"
#include "default.h"

#ifndef _WIN32
#if !defined(MAC_OSX_TK)
#include <X11/Xproto.h>
#endif
#endif

#if defined(_WIN32)
#include "tkWinInt.h"
#elif defined(MAC_OSX_TK)
#include "tkMacOSXInt.h"
#else
#include "tkUnixInt.h"
#endif

/*
 * For C++ compilers, use extern "C"
 */
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tko_WidgetOptionType --
 *
 * Supported type in the TkoWidgetOptionSet() function.
 * In comments is the type of the address pointer.
 */
    typedef enum Tko_WidgetOptionType {
        TKO_SET_NONE = 0,     /* Nono */
        TKO_SET_CLASS = 1,     /* (Tcl_Obj **)address */
        TKO_SET_VISUAL, /* (Tcl_Obj **)address */
        TKO_SET_COLORMAP,       /* (Tcl_Obj **)address */
        TKO_SET_USE,        /* (Tcl_Obj **)address */
        TKO_SET_CONTAINER,      /* (int *)address */
        TKO_SET_TCLOBJ, /* (Tcl_Obj **)address */
        TKO_SET_XCOLOR, /* (Xcolor **)address */
        TKO_SET_3DBORDER,       /* (Tk_3DBorder *)address */
        TKO_SET_PIXEL,  /* (int *)address */
        TKO_SET_PIXELNONEGATIV, /* (int *)address */
        TKO_SET_PIXELPOSITIV,   /* (int *)address */
        TKO_SET_DOUBLE, /* (double *)address */
        TKO_SET_BOOLEAN,        /* (int *)address */
        TKO_SET_CURSOR, /* (Tk_Cursor *)address */
        TKO_SET_INT,    /* (int *)address */
        TKO_SET_RELIEF, /* (int *)address */
        TKO_SET_ANCHOR, /* (int *)address */
        TKO_SET_WINDOW, /* (Tk_Window *)address */
        TKO_SET_FONT,   /* (Tk_Font *)address */
        TKO_SET_STRING, /* (char **)address */
        TKO_SET_SCROLLREGION,   /* (int *[4])address */
        TKO_SET_JUSTIFY /* (Tk_Justify *)address */
    } Tko_WidgetOptionType;

/*
* Tko_CreateMode --
*
* Supported values in Tko_WdigetCreate() function call.
*/
    typedef enum Tko_WidgetCreateMode {
        TKO_CREATE_WIDGET, /* Create new widget */
        TKO_CREATE_TOPLEVEL, /* Create new toplevel widget */
        TKO_CREATE_CLASS, /* See "tko initclass" */
        TKO_CREATE_WRAP /* See "tko initwrap" */
    } Tko_WidgetCreateMode;

/*
 * Tko_WidgetOptionDefine --
 *
 * Widget definition data used in class.
 * An option set method "-option" is created in the following order:
 * - "option"=NULL indicate the end of a list of option definitions.
 * - If "method" is given it will be used as option set method.
 * - If "type" is greater 0 a common option set method will be used.
 *   In this case "offset" are used as offset in the widget structure.
 */
    typedef struct Tko_WidgetOptionDefine {
        const char *option;    /* Name of option. Starts with "-" minus sign */
        const char *dbname;    /* Option DB name or synonym option if dbclass is NULL */
        const char *dbclass;   /* Option DB class name or NULL for synonym options. */
        const char *defvalue;  /* Default value. */
        int flags;             /* bit array of TKO_OPTION_* values to configure option behaviour */
        Tcl_MethodCallProc *method;    /* If not NULL it is the function name of the -option method */
        Tko_WidgetOptionType type;  /* if greater 0 then option type used in common option set method */
        size_t offset;            /* offset in meta data struct */
    } Tko_WidgetOptionDefine;
#define TKO_OPTION_READONLY 0x1 /* option is only setable at creation time */
#define TKO_OPTION_HIDE     0x2 /* option is hidden in configure method */
#define TKO_OPTION_NULL      0x4 /* empty values are saved as NULL */
#define TKO_OPTION__USER    0x8 /* internally used */

    /*
    * Widget structure data used in objects.
    * These structure will be filled in the **Tko\_WidgetCreate** call
    * and cleared in the **Tko\_WidgetDestroy** call.
    * Widget methods should check the value of *tkWin* on NULL before using it.
    */
    typedef struct Tko_Widget {
        Tk_Window tkWin;           /* Window that embodies the widget. NULL means
                                   * that the window has been destroyed but the
                                   * data structures haven't yet been cleaned
                                   * up.*/
        Display *display;        /* Display containing widget. Used, among
                                 * other things, so that resources can be
                                 * freed even after tkwin has gone away. */
        Tcl_Interp *interp;        /* Interpreter associated with widget. */
        Tcl_Command widgetCmd;     /* Token for command. */
        Tcl_Object object;         /* our own object */
        Tcl_Obj *myCmd;            /* Objects "my" command. Needed to call internal methods. */
        Tcl_Obj *optionsArray;     /* Name of option array variable */
        Tcl_HashTable *optionsTable; /* Hash table containing all used options */
    } Tko_Widget;

/* tkoFrame.c */
    MODULE_SCOPE int Tko_FrameInit(
        Tcl_Interp * interp);
    MODULE_SCOPE int Tko_VectorInit(
        Tcl_Interp * interp);
    MODULE_SCOPE int Tko_GraphInit(
        Tcl_Interp * interp);
/* tkoWidget.c */
    MODULE_SCOPE int Tko_WidgetClassDefine(
        Tcl_Interp *interp,
        Tcl_Obj *classname,
        const Tcl_MethodType *methods,
        const Tko_WidgetOptionDefine *options);
    MODULE_SCOPE int Tko_WidgetCreate(
        ClientData clientdata,
        Tcl_Interp *interp,
        Tcl_Object object,
        Tko_WidgetCreateMode createmode,
        Tcl_Obj *arglist);
    MODULE_SCOPE void Tko_WidgetDestroy(
        Tcl_ObjectContext context);
    MODULE_SCOPE ClientData Tko_WidgetClientData(
        Tcl_ObjectContext context);
    MODULE_SCOPE Tcl_Obj *Tko_WidgetOptionGet(
        Tko_Widget *widget,
        Tcl_Obj *option);
    MODULE_SCOPE Tcl_Obj *Tko_WidgetOptionSet(
        Tko_Widget *widget,
        Tcl_Obj *option,
        Tcl_Obj *value);

/*
 * end block for C++
 */

#ifdef __cplusplus
}
#endif
#endif                         /* _TKOWIDGET_H */
/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
