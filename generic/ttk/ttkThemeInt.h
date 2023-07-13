/*
 * Theme engine: private definitions.
 *
 * Copyright (c) 2004 Joe English.  Freely redistributable.
 */

#ifndef _TTKTHEMEINT
#define _TTKTHEMEINT

#include "ttkTheme.h"

typedef struct Ttk_TemplateNode_ Ttk_TemplateNode, *Ttk_LayoutTemplate;

MODULE_SCOPE Ttk_ElementClass *Ttk_GetElement(Ttk_Theme, const char *name);
MODULE_SCOPE const char *Ttk_ElementClassName(Ttk_ElementClass *);

MODULE_SCOPE void Ttk_ElementSize(
	Ttk_ElementClass *, Ttk_Style, void *recordPtr, Tk_OptionTable,
	Tk_Window tkwin, Ttk_State state,
	int *widthPtr, int *heightPtr, Ttk_Padding*);
MODULE_SCOPE void Ttk_DrawElement(
	Ttk_ElementClass *, Ttk_Style, void *recordPtr, Tk_OptionTable,
	Tk_Window tkwin, Drawable d, Ttk_Box b, Ttk_State state);

MODULE_SCOPE Tcl_Obj *Ttk_QueryStyle(
    Ttk_Style, void *, Tk_OptionTable, const char *, Ttk_State state);

MODULE_SCOPE Ttk_LayoutTemplate Ttk_ParseLayoutTemplate(
	Tcl_Interp *, Tcl_Obj *);
MODULE_SCOPE Tcl_Obj *Ttk_UnparseLayoutTemplate(Ttk_LayoutTemplate);
MODULE_SCOPE Ttk_LayoutTemplate Ttk_BuildLayoutTemplate(Ttk_LayoutSpec);
MODULE_SCOPE void Ttk_FreeLayoutTemplate(Ttk_LayoutTemplate);
MODULE_SCOPE void Ttk_RegisterLayoutTemplate(
    Ttk_Theme theme, const char *layoutName, Ttk_LayoutTemplate);

MODULE_SCOPE Ttk_Style Ttk_GetStyle(Ttk_Theme themePtr, const char *styleName);
MODULE_SCOPE Ttk_LayoutTemplate Ttk_FindLayoutTemplate(
    Ttk_Theme themePtr, const char *layoutName);

MODULE_SCOPE const char *Ttk_StyleName(Ttk_Style);

MODULE_SCOPE void TtkSetBlinkCursorTimes(Tcl_Interp* interp);

#if !defined(TK_NO_DEPRECATED) && (TCL_MAJOR_VERSION < 9)
#   define TTK_OPTION_UNDERLINE_DEF(type, field) "-1", offsetof(type, field), TCL_INDEX_NONE, 0, NULL
#else
#   define TTK_OPTION_UNDERLINE_DEF(type, field) NULL, offsetof(type, field), TCL_INDEX_NONE, TK_OPTION_NULL_OK, NULL
#endif

#endif /* _TTKTHEMEINT */
