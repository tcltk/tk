/*
 * Theme engine: private definitions.
 *
 * Copyright © 2004 Joe English.  Freely redistributable.
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

/*
 * Element render-cache policy.  An element class may opt into per-node caching
 * of its composited output and supply a query reporting its opacity and content
 * epoch.  Off by default.
 */
#define TTK_ELEMENT_CACHEABLE	0x1	/* opts into per-node render caching */

/*
 * Cache info an element reports for the current (tkwin, state): whether it is
 * fully opaque, and a content epoch that bumps when its own pixels change.
 */
typedef struct Ttk_ElementCacheInfo {
    int		opaque;		/* element fully opaque (bg-independent) */
    unsigned	epoch;		/* bumps when the element's own content changes */
} Ttk_ElementCacheInfo;

typedef void Ttk_ElementCacheProc(void *clientData, void *elementRecord,
	Tk_Window tkwin, Ttk_Box b, Ttk_State state, Ttk_ElementCacheInfo *info);

MODULE_SCOPE void TtkSetElementCachePolicy(
	Ttk_ElementClass *, unsigned cacheFlags, Ttk_ElementCacheProc *);
MODULE_SCOPE int Ttk_ElementClassCacheable(Ttk_ElementClass *);
MODULE_SCOPE void Ttk_ElementGetCacheInfo(
	Ttk_ElementClass *, Ttk_Style, void *recordPtr, Tk_OptionTable,
	Tk_Window tkwin, Ttk_Box b, Ttk_State state, Ttk_ElementCacheInfo *);

/*
 * Per-node element render cache (ttkNodeCache.c).  The layout-draw traversal
 * opens a context, draws each node's element through it, and closes it.  Both
 * types are opaque; a layout node stores a NodeDrawCache* slot.
 */
typedef struct NodeDrawCache NodeDrawCache;
typedef struct NodeDrawContext NodeDrawContext;

MODULE_SCOPE NodeDrawContext *TtkNodeDrawBegin(
	Ttk_Style style, void *recordPtr, Tk_OptionTable optionTable,
	Tk_Window tkwin, Drawable d);
MODULE_SCOPE void TtkDrawCachedElement(NodeDrawContext *,
	Ttk_ElementClass *, NodeDrawCache **cacheSlot, Ttk_Box b, Ttk_State);
MODULE_SCOPE void TtkNodeDrawEnd(NodeDrawContext *);
MODULE_SCOPE void TtkFreeNodeDrawCache(NodeDrawCache *);

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

MODULE_SCOPE bool TtkBoxEqual(Ttk_Box, Ttk_Box);

#define TTK_OPTION_UNDERLINE_DEF(type, field) NULL, offsetof(type, field), TCL_INDEX_NONE, TK_OPTION_NULL_OK, NULL

#endif /* _TTKTHEMEINT */
