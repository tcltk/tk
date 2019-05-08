/*
 * tkoWidget.h --
 *
 *    Header file for the internals of the tko widget package.
 *
 * Copyright (c) 2019 Rene zaumseil
 *
 */

#ifndef _TKOWIDGET_H
#define _TKOWIDGET_H

#include "tcl.h"
#include "tclInt.h"     /* TclIsInfinite() */

#include "tclOO.h"

#include "tk.h"
#include "default.h"
#include "tk3d.h"
#include "tkFont.h"

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

#define _USE_MATH_DEFINES
#include <math.h>       /* VC math constants M_PI, M_SQRT1_2 */
#include <float.h>      /* DBL_MAX,.. */
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

#define DEGREES_TO_RADIANS (M_PI/180.0)
#define RADIANS_TO_DEGREES (180.0/M_PI)

/*
* Static tcl objects.
*/
    typedef struct tkoObj {
        Tcl_Obj *empty;
        Tcl_Obj *point;
        Tcl_Obj *next;
        Tcl_Obj *uplevel;
        Tcl_Obj *oo_define;
        Tcl_Obj *oo_objdefine;
        Tcl_Obj *method;
        Tcl_Obj *_tko_configure;
        Tcl_Obj *tko;
        Tcl_Obj *tko_options;
        Tcl_Obj *lsort;
        Tcl_Obj *tko_widget;
        Tcl_Obj *tko_frame;
        Tcl_Obj *tko_labelframe;
        Tcl_Obj *tko_toplevel;
        Tcl_Obj *path;
        Tcl_Obj *graph;
        Tcl_Obj *_screen;
        Tcl_Obj *_labelwidget;
        Tcl_Obj *_0;
        Tcl_Obj *_1;
    } tkoObj;
    MODULE_SCOPE tkoObj TkoObj;

/*
 * tkoWidgetOptionType --
 *
 * Suported type in the TkowidgetOptinSet() function.
 * In comments is the type of the address pointer.
 */
    typedef enum tkoWidgetOptionType {
        TKO_SET_CLASS = 1,     /* (Tcl_Obj **)address */
        TKO_SET_VISUAL, /* (Tcl_Obj **)address */
        TKO_SET_COLORMAP,       /* (Tcl_Obj **)address */
        TKO_SET_USENULL,        /* (Tcl_Obj **)address */
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
        TKO_SET_STRINGNULL,     /* (char **)address */
        TKO_SET_SCROLLREGION,   /* (int *[4])address */
        TKO_SET_JUSTIFY /* (Tk_Justify *)address */
    } tkoWidgetOptionType;

/*
 * tkoWidgetOptionDefine --
 *
 * Widget definition data used in class.
 " An option set method "-option" is created in the following order:
 * - If "proc" is given it will be used as option set method.
 * - If "method" is given it will be used as option set method.
 * - If "type" is greater 0 a common option set method will be used.
 *   In this case "type", "meta" and "offset" are used as parameters for
 *   the TkoWidgetOptionSet() function call.
 */
    typedef struct tkoWidgetOptionDefine {
        const char *option;    /* Name of option. Starts with "-" minus sign */
        const char *dbname;    /* Option DB name or synonym option if dbclass is NULL */
        const char *dbclass;   /* Option DB class name or NULL for synonym options. */
        const char *defvalue;  /* Default value. */
        int flags;             /* bit array of TKO_OPTION_* values to configure option behaviour */
		Tcl_Obj *optionPtr;    /* tko internally used, always init with NULL! */
        const char *proc;      /* If not NULL it is the body of the newly created -option method */
        Tcl_MethodCallProc *method;     /* If not NULL it is the function name of the -option method */
        tkoWidgetOptionType type;       /* if greater 0 then option type used in common option set method */
        Tcl_ObjectMetadataType *meta;   /* meta data address used in common option set method */
        int offset;            /* offset in meta data struct */
    } tkoWidgetOptionDefine;
#define TKO_OPTION_READONLY 0x1 /* option is only setable at creation time */
#define TKO_OPTION_HIDE     0x2 /* option is hidden in configure method */      

/* tkoFrame.c */
    MODULE_SCOPE int Tko_FrameInit(
        Tcl_Interp * interp);
/* tkoVector.c */
    MODULE_SCOPE int Tko_VectorInit(
        Tcl_Interp * interp);
/* tkoGraph.c */
    MODULE_SCOPE int Tko_GraphInit(
        Tcl_Interp * interp);
/* tkoPath.c */
    MODULE_SCOPE int Tko_PathInit(
        Tcl_Interp * interp);
/* tkoWidget.c */
    MODULE_SCOPE int Tko_WidgetInit(
        Tcl_Interp * interp);
    MODULE_SCOPE Tk_Window *TkoWidgetWindow(
        Tcl_Object object);
    MODULE_SCOPE Tcl_Obj *TkoWidgetOptionVar(
        Tcl_Object object);
    MODULE_SCOPE Tcl_Obj *TkoWidgetOptionGet(
        Tcl_Interp * interp,
        Tcl_Object object,
        Tcl_Obj * option);
    MODULE_SCOPE int TkoWidgetOptionSet(
        Tcl_Interp * interp,
        Tcl_ObjectContext context,
        Tcl_Obj * option,
        tkoWidgetOptionType type,
        Tcl_ObjectMetadataType * meta,
        size_t offset);
    MODULE_SCOPE int TkoWidgetClassDefine(
        Tcl_Interp * interp,
        Tcl_Class clazz,
        Tcl_Obj * classname,
        const Tcl_MethodType * methods,
        tkoWidgetOptionDefine * options);

/*
 * end block for C++
 */

#ifdef __cplusplus
}
#endif
#endif                         /* _TKOWIDGET_H */
/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
