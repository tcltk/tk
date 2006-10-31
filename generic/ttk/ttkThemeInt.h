/*
 * $Id: ttkThemeInt.h,v 1.1 2006/10/31 01:42:26 hobbs Exp $
 *
 * Theme engine: private definitions.
 *
 * Copyright (c) 2004 Joe English.  Freely redistributable.
 */

#ifndef TKTHEMEINT_INCLUDED
#define TKTHEMEINT_INCLUDED 1

#include "ttkTheme.h"

typedef struct Ttk_Style_ *Ttk_Style;
typedef struct Ttk_TemplateNode_ Ttk_TemplateNode, *Ttk_LayoutTemplate;

extern Ttk_Element Ttk_GetElement(Ttk_Theme theme, const char *name);
extern const char *Ttk_ElementName(Ttk_Element);

extern void Ttk_ElementSize(
	Ttk_Element element, Ttk_Style, char *recordPtr, Tk_OptionTable,
	Tk_Window tkwin, Ttk_State state,
	int *widthPtr, int *heightPtr, Ttk_Padding*);
extern void Ttk_DrawElement(
	Ttk_Element element, Ttk_Style, char *recordPtr, Tk_OptionTable,
	Tk_Window tkwin, Drawable d, Ttk_Box b, Ttk_State state);

extern Tcl_Obj *Ttk_QueryStyle(
    Ttk_Style, void *, Tk_OptionTable, const char *, Ttk_State state);

extern Ttk_LayoutTemplate Ttk_ParseLayoutTemplate(Tcl_Interp *, Tcl_Obj *);
extern Tcl_Obj *Ttk_UnparseLayoutTemplate(Ttk_LayoutTemplate);
extern Ttk_LayoutTemplate Ttk_BuildLayoutTemplate(Ttk_LayoutSpec);
extern void Ttk_FreeLayoutTemplate(Ttk_LayoutTemplate);

extern Ttk_Style Ttk_GetStyle(Ttk_Theme themePtr, const char *styleName);
extern Ttk_LayoutTemplate Ttk_FindLayoutTemplate(
    Ttk_Theme themePtr, const char *layoutName);

extern const char *Ttk_StyleName(Ttk_Style);


#endif /* TKTHEMEINT_INCLUDED */
