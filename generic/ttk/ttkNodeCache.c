/*
 * ttkNodeCache.c --
 *
 *	Per-node element *render* cache for the Ttk layout-draw traversal.
 *	ttkLayout.c walks a widget's node tree in z-order and draws each element
 *	through this module, which caches the composited result on the layout
 *	node -- so it is per-widget-instance -- replacing the per-tile redraw
 *	with a single copy on a hit.  A node is reused while its size, position,
 *	state and content epoch hold; a translucent node also rebuilds when a
 *	lower node drawn this pass overlaps it, so a changed background shows
 *	through.  Invalidation needs no help from widgets or elements.
 *
 *	Compiled in only where XCopyArea accepts a Pixmap; without double
 *	buffering every entry point degrades to a direct element draw.
 *
 * Copyright © 2026 Tk contributors.  Freely redistributable.
 */

#include "tkInt.h"
#include "ttkThemeInt.h"

/*
 * NodeDrawContext --
 *	Per-draw-pass state (TtkNodeDrawBegin/End): element-draw parameters, the
 *	destination, a lazy GXcopy GC, and the parcels drawn this pass.
 */
struct NodeDrawContext {
    Ttk_Style		style;
    void		*recordPtr;
    Tk_OptionTable	optionTable;
    Tk_Window		tkwin;
    Drawable		d;
#ifndef TK_NO_DOUBLE_BUFFERING
    Display		*display;
    GC			copyGC;		/* Lazily allocated GXcopy GC, or NULL */
    Ttk_Box		*dirty;		/* Parcels (re)drawn fresh this pass */
    int			ndirty, dirtycap;
#endif
};

#ifndef TK_NO_DOUBLE_BUFFERING

/*
 * NodeDrawCache --
 *	One cached composited element, hung off a layout node.
 */
struct NodeDrawCache {
    Pixmap	pixmap;		/* Cached composited element, or None */
    Display	*display;	/* Display owning pixmap (for Tk_FreePixmap) */
    int		x, y, w, h;	/* Parcel the pixmap was built for */
    Ttk_State	state;		/* State the pixmap was built for */
    int		opaque;		/* 1: background-independent; 0: bg baked in */
    unsigned	epoch;		/* Element content epoch when built */
    int		valid;		/* Nonzero when the cache holds the key */
};

static int BoxesIntersect(Ttk_Box a, Ttk_Box b)
{
    return a.x < b.x + b.width  && b.x < a.x + a.width
	&& a.y < b.y + b.height && b.y < a.y + a.height;
}

/* DirtyOverlaps, DirtyAdd --
 *	The parcels drawn this pass; a translucent node rebuilds when one
 *	overlaps its parcel.
 */
static int DirtyOverlaps(NodeDrawContext *ctx, Ttk_Box b)
{
    int i;
    for (i = 0; i < ctx->ndirty; ++i) {
	if (BoxesIntersect(ctx->dirty[i], b)) {
	    return 1;
	}
    }
    return 0;
}

static void DirtyAdd(NodeDrawContext *ctx, Ttk_Box b)
{
    if (ctx->ndirty >= ctx->dirtycap) {
	ctx->dirtycap = ctx->dirtycap ? 2 * ctx->dirtycap : 8;
	ctx->dirty = (Ttk_Box *)Tcl_Realloc(ctx->dirty,
		ctx->dirtycap * sizeof(Ttk_Box));
    }
    ctx->dirty[ctx->ndirty++] = b;
}

static GC NodeCopyGC(NodeDrawContext *ctx)
{
    if (ctx->copyGC == NULL) {
	XGCValues gcv;
	gcv.function = GXcopy;
	gcv.graphics_exposures = False;
	ctx->copyGC = Tk_GetGC(ctx->tkwin,
		GCFunction | GCGraphicsExposures, &gcv);
    }
    return ctx->copyGC;
}

#endif /* !TK_NO_DOUBLE_BUFFERING */

/* TtkNodeDrawBegin --
 *	Open a draw pass; graphics state is allocated lazily.
 */
NodeDrawContext *TtkNodeDrawBegin(
    Ttk_Style style,
    void *recordPtr,
    Tk_OptionTable optionTable,
    Tk_Window tkwin,
    Drawable d)
{
    NodeDrawContext *ctx = (NodeDrawContext *)Tcl_Alloc(sizeof(*ctx));

    ctx->style = style;
    ctx->recordPtr = recordPtr;
    ctx->optionTable = optionTable;
    ctx->tkwin = tkwin;
    ctx->d = d;
#ifndef TK_NO_DOUBLE_BUFFERING
    ctx->display = Tk_Display(tkwin);
    ctx->copyGC = NULL;
    ctx->dirty = NULL;
    ctx->ndirty = ctx->dirtycap = 0;
#endif
    return ctx;
}

/* TtkNodeDrawEnd --
 *	Close a draw pass: free the shared GC and dirty set.  Node pixmaps
 *	persist; they are freed by TtkFreeNodeDrawCache.
 */
void TtkNodeDrawEnd(NodeDrawContext *ctx)
{
#ifndef TK_NO_DOUBLE_BUFFERING
    if (ctx->copyGC != NULL) {
	Tk_FreeGC(ctx->display, ctx->copyGC);
    }
    if (ctx->dirty != NULL) {
	Tcl_Free(ctx->dirty);
    }
#endif
    Tcl_Free(ctx);
}

/* TtkFreeNodeDrawCache --
 *	Free a node's cached pixmap and record; NULL-tolerant.
 */
void TtkFreeNodeDrawCache(NodeDrawCache *cache)
{
    if (cache == NULL) {
	return;
    }
#ifndef TK_NO_DOUBLE_BUFFERING
    if (cache->pixmap != None) {
	Tk_FreePixmap(cache->display, cache->pixmap);
    }
    Tcl_Free(cache);
#endif
}

/* TtkDrawCachedElement --
 *	Draw one node's element through its per-node cache: copy the cached
 *	pixmap on a hit, else (re)render and store it.  Falls back to a direct
 *	draw for non-cacheable elements, an unrealized window, or alloc failure.
 */
void TtkDrawCachedElement(
    NodeDrawContext *ctx,
    Ttk_ElementClass *eclass,
    NodeDrawCache **cacheSlot,
    Ttk_Box b,
    Ttk_State state)
{
    if (b.width <= 0 || b.height <= 0) {
	return;
    }

#ifndef TK_NO_DOUBLE_BUFFERING
    if (Ttk_ElementClassCacheable(eclass)
	    && Tk_WindowId(ctx->tkwin) != None) {
	Ttk_ElementCacheInfo info;
	NodeDrawCache *cache = *cacheSlot;
	GC gc = NULL;

	Ttk_ElementGetCacheInfo(eclass, ctx->style, ctx->recordPtr,
		ctx->optionTable, ctx->tkwin, b, state, &info);

	/* Hit: size, position, state and epoch match, and -- if translucent
	 * -- nothing beneath changed this pass. */
	if (cache && cache->valid
		&& cache->x == b.x && cache->y == b.y
		&& cache->w == b.width && cache->h == b.height
		&& cache->state == state
		&& cache->opaque == info.opaque
		&& cache->epoch == info.epoch
		&& (info.opaque || !DirtyOverlaps(ctx, b))
		&& (gc = NodeCopyGC(ctx)) != NULL) {
	    XCopyArea(ctx->display, cache->pixmap, ctx->d, gc,
		    0, 0, (unsigned)b.width, (unsigned)b.height, b.x, b.y);
	    return;			/* clean: parcel not marked dirty */
	}

	/* Miss: (re)build the node pixmap. */
	if (cache == NULL) {
	    cache = *cacheSlot = (NodeDrawCache *)Tcl_Alloc(sizeof(*cache));
	    memset(cache, 0, sizeof(*cache));
	}
	if (cache->pixmap != None
		&& (cache->w != b.width || cache->h != b.height)) {
	    Tk_FreePixmap(cache->display, cache->pixmap);
	    cache->pixmap = None;
	}
	if (cache->pixmap == None) {
	    cache->pixmap = Tk_GetPixmap(ctx->display, Tk_WindowId(ctx->tkwin),
		    b.width, b.height, Tk_Depth(ctx->tkwin));
	    cache->display = ctx->display;
	}
	gc = NodeCopyGC(ctx);
	if (cache->pixmap != None && gc != NULL) {
	    if (!info.opaque) {
		/* Seed the live background for translucent pixels and
		 * non-filling -sticky margins. */
		XCopyArea(ctx->display, ctx->d, cache->pixmap, gc,
			b.x, b.y, (unsigned)b.width, (unsigned)b.height, 0, 0);
	    }
	    Ttk_DrawElement(eclass, ctx->style, ctx->recordPtr,
		    ctx->optionTable, ctx->tkwin, cache->pixmap,
		    Ttk_MakeBox(0, 0, b.width, b.height), state);
	    XCopyArea(ctx->display, cache->pixmap, ctx->d, gc,
		    0, 0, (unsigned)b.width, (unsigned)b.height, b.x, b.y);

	    cache->x = b.x;	cache->y = b.y;
	    cache->w = b.width;	cache->h = b.height;
	    cache->state = state;
	    cache->opaque = info.opaque;
	    cache->epoch = info.epoch;
	    cache->valid = 1;
	    DirtyAdd(ctx, b);
	    return;
	}

	/* Pixmap or GC allocation failed: fall through to a direct draw. */
	if (cache) {
	    cache->valid = 0;
	}
    }

    /* Direct draw: non-cacheable element, unrealized window, or alloc fail. */
    Ttk_DrawElement(eclass, ctx->style, ctx->recordPtr, ctx->optionTable,
	    ctx->tkwin, ctx->d, b, state);
    DirtyAdd(ctx, b);
#else
    (void)cacheSlot;
    Ttk_DrawElement(eclass, ctx->style, ctx->recordPtr, ctx->optionTable,
	    ctx->tkwin, ctx->d, b, state);
#endif /* !TK_NO_DOUBLE_BUFFERING */
}

/*EOF*/
