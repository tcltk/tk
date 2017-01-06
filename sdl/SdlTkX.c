#ifdef linux
#define _BSD_SOURCE
#if defined(__arm__) || defined(__aarch64__) || defined(ANDROID)
#include <sys/eventfd.h>
#endif
#include <dlfcn.h>
#endif
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#endif
#include "tkInt.h"
#include "tkIntPlatDecls.h"
#include "SdlTk.h"
#include "SdlTkX.h"
#include "SdlTkInt.h"
#include <SDL_syswm.h>
#include <SDL_scancode.h>

#ifdef ANDROID
#include <android/log.h>
#else
#include <SDL_opengl.h>
#endif

#undef TRACE_EVENTS
#undef TRACE_XEVENTS
#undef TRACE_GL

#ifdef TRACE_EVENTS
#ifdef ANDROID
#define EVLOG(...) __android_log_print(ANDROID_LOG_ERROR,"SDLEV",__VA_ARGS__)
#else
#define EVLOG(...) SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,__VA_ARGS__)
#endif
#else
#define EVLOG(...)
#endif
#ifdef TRACE_GL
#define GLLOG(...) SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,__VA_ARGS__)
#else
#define GLLOG(...)
#endif

#ifdef AGG_CUSTOM_ALLOCATOR
/* AGG custom allocator functions */
void *(*agg_custom_alloc)(unsigned int size) = NULL;
void (*agg_custom_free)(void *ptr) = NULL;
#endif

TCL_DECLARE_MUTEX(atom_mutex);
static int atom_initialized = 0;
static Tcl_HashTable atom_table;
static int prop_initialized = 0;
static Tcl_HashTable prop_table;

struct RootSizeRequest {
    int running;
    int width;
    int height;
};

struct PanZoomRequest {
    int running;
    SDL_Rect r;
};

#ifndef ANDROID
struct WindowFlagsRequest {
    int running;
    int flags;
    SDL_Rect r;
    float opacity;
};
#endif

struct EventThreadStartup {
    int init_done;
    int *root_width;
    int *root_height;
};

struct prop_key {
    _Window *w;
    Atom name;
};

struct prop_val {
    int length;
    char data[1];
};

TCL_DECLARE_MUTEX(xlib_lock);
static Display *xlib_grab = NULL;
static Tcl_Condition xlib_cond;
static Tcl_Condition time_cond;
static int timer_enabled = 0;
static int num_displays = 0;

static void SdlTkDestroyWindow(Display *display, Window w);
static void SdlTkMapWindow(Display *display, Window w);
static void SdlTkUnmapWindow(Display *display, Window w);
static int  SdlTkReparentWindow(Display *display, Window w,
				Window parent, int x, int y);
static void SdlTkChangeWindowAttributes(Display *display, Window w,
					unsigned long valueMask,
					XSetWindowAttributes *attributes);
static Window SdlTkCreateWindow(Display *display, Window parent, int x, int y,
				unsigned int width, unsigned int height,
				unsigned int border_width, int depth,
				unsigned int clazz, Visual *visual,
				unsigned long valuemask,
				XSetWindowAttributes *attributes);
static void SdlTkLostFocusWindow(void);

void
SdlTkLock(Display *display)
{
    Tcl_MutexLock(&xlib_lock);
    if (display != NULL) {
	while ((xlib_grab != NULL) && (xlib_grab != display)) {
	    Tcl_ConditionWait(&xlib_cond, &xlib_lock, NULL);
	}
    }
}

void
SdlTkUnlock(Display *display)
{
    Tcl_MutexUnlock(&xlib_lock);
}

void
SdlTkWaitLock(void)
{
    Tcl_ConditionWait(&xlib_cond, &xlib_lock, NULL);
}

void
SdlTkWaitVSync(void)
{
    Tcl_ConditionWait(&time_cond, &xlib_lock, NULL);
}

/*
 * Undocumented Xlib internal function
 */

int
_XInitImageFuncPtrs(XImage *image)
{
    return 0;
}

XClassHint *
XAllocClassHint(void)
{
    XClassHint *hint;

    hint = (XClassHint *) ckalloc(sizeof (XClassHint));
    return hint;
}

int
XAllocColor(Display *display, Colormap colormap, XColor *color)
{
    /* NOTE: If this changes, update TkpGetPixel */
    Uint8 r = (color->red / 65535.0) * 255.0;
    Uint8 g = (color->green / 65535.0) * 255.0;
    Uint8 b = (color->blue / 65535.0) * 255.0;

    color->pixel = SDL_MapRGB(SdlTkX.sdlsurf->format, r, g, b);
    return 1;
}

Status
XAllocNamedColor(display, colormap, color_name, screen_def_return,
	exact_def_return)
    Display* display;
    Colormap colormap;
    const char* color_name;
    XColor* screen_def_return;
    XColor* exact_def_return;
{
    /* xcolors.c */
    if (XParseColor(display, colormap, color_name, exact_def_return) == 1) {
	*screen_def_return = *exact_def_return;
	return XAllocColor(display, colormap, screen_def_return);
    }
    return 0;
}

XSizeHints *
XAllocSizeHints(void)
{
    return (XSizeHints *) ckalloc(sizeof (XSizeHints));
}

void
XBell(Display *display, int percent)
{
}

void
XChangeGC(Display *display, GC gc, unsigned long mask, XGCValues *values)
{
    if (mask & GCFunction) {
	gc->function = values->function;
    }
    if (mask & GCPlaneMask) {
	gc->plane_mask = values->plane_mask;
    }
    if (mask & GCForeground) {
	gc->foreground = values->foreground;
    }
    if (mask & GCBackground) {
	gc->background = values->background;
    }
    if (mask & GCLineWidth) {
	gc->line_width = values->line_width;
    }
    if (mask & GCLineStyle) {
	gc->line_style = values->line_style;
    }
    if (mask & GCCapStyle) {
	gc->cap_style = values->cap_style;
    }
    if (mask & GCJoinStyle) {
	gc->join_style = values->join_style;
    }
    if (mask & GCFillStyle) {
	gc->fill_style = values->fill_style;
    }
    if (mask & GCFillRule) {
	gc->fill_rule = values->fill_rule;
    }
    if (mask & GCArcMode) {
	gc->arc_mode = values->arc_mode;
    }
    if (mask & GCTile) {
	gc->tile = values->tile;
    }
    if (mask & GCStipple) {
	gc->stipple = values->stipple;
    }
    if (mask & GCTileStipXOrigin) {
	gc->ts_x_origin = values->ts_x_origin;
    }
    if (mask & GCTileStipYOrigin) {
	gc->ts_y_origin = values->ts_y_origin;
    }
    if (mask & GCFont) {
	gc->font = values->font;
    }
    if (mask & GCSubwindowMode) {
	gc->subwindow_mode = values->subwindow_mode;
    }
    if (mask & GCGraphicsExposures) {
	gc->graphics_exposures = values->graphics_exposures;
    }
    if (mask & GCClipXOrigin) {
	gc->clip_x_origin = values->clip_x_origin;
    }
    if (mask & GCClipYOrigin) {
	gc->clip_y_origin = values->clip_y_origin;
    }
    if (mask & GCClipMask) {
	XSetClipMask(display, gc, values->clip_mask);
    }
    if (mask & GCDashOffset) {
	gc->dash_offset = values->dash_offset;
    }
    if (mask & GCDashList) {
	gc->dashes = values->dashes ? values->dashes : 0;
	(&gc->dashes)[1] = values->dashes ? values->dashes : 0;
	(&gc->dashes)[2] = 0;
    }
}

void
XChangeProperty(Display *display, Window w, Atom property, Atom type,
		int format, int mode, _Xconst unsigned char *data,
		int nelements)
{
    _Window *_w = (_Window *) w;

    SdlTkLock(display);

    display->request++;

    if ((_w == NULL) || (_w->display == NULL)) {
	goto done;
    }

    if (property == SdlTkX.nwmn_atom) {
	if (_w->title != NULL) {
	    ckfree((char *) _w->title);
	}
	_w->title = ckalloc(nelements + 1); /* UTF-8 */
	strcpy((char *) _w->title, (char *) data);

	/* Redraw frame titlebar */
	if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
	    SdlTkDecSetDraw(_w->parent, 1);
	    SdlTkScreenChanged();
	}
	goto done;
    }
    if ((property == SdlTkX.mwm_atom) && (type == SdlTkX.mwm_atom)) {
	long *props = (long *) data;

	if ((props[0] & 2) && !_w->atts.override_redirect) {
	    XSetWindowAttributes atts;

	    atts.override_redirect = props[2] ? 0 : 1;
	    if (_w->atts.override_redirect != atts.override_redirect) {
		SdlTkChangeWindowAttributes(display, w,
					    CWOverrideRedirect, &atts);
	    }
	}
	goto done;
    }
    if (property == SdlTkX.nwms_atom) {
	int i, fullscreen = 0;
	Atom *props = (Atom *) data;
	XPropertyEvent xproperty;
	_Window *_ww = _w;

	for (i = 0; i < nelements; i++) {
	    if (props[i] == SdlTkX.nwmsf_atom) {
		fullscreen = 1;
		break;
	    }
	}
	if (fullscreen && !_w->fullscreen) {
	    int xx, yy, ww, hh;

	    _w->atts_saved = _w->atts;
	    xx = yy = 0;
	    ww = SdlTkX.screen->width;
	    hh = SdlTkX.screen->height;
	    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
		xx -= SdlTkX.dec_frame_width;
		yy -= SdlTkX.dec_title_height;
		ww += SdlTkX.dec_frame_width * 2;
		hh += SdlTkX.dec_title_height + SdlTkX.dec_frame_width;
	    }
	    SdlTkMoveResizeWindow(display, w, xx, yy, ww, hh);
	    while (!IS_ROOT((Window) _ww)) {
		_ww->fullscreen = 1;
		_ww = _ww->parent;
	    }
	} else if (!fullscreen && _w->fullscreen) {
	    while (!IS_ROOT((Window) _ww)) {
		_ww->fullscreen = 0;
		_ww = _ww->parent;
	    }
	    SdlTkMoveResizeWindow(display, w,
				  _w->atts_saved.x, _w->atts_saved.y,
				  _w->atts_saved.width, _w->atts_saved.height);
	}
	memset(&xproperty, 0, sizeof (xproperty));
	xproperty.type = PropertyNotify;
	xproperty.serial = _w->display->request;
	xproperty.send_event = False;
	xproperty.atom = SdlTkX.nwms_atom;
	xproperty.display = _w->display;
	xproperty.window = (Window) _w;
	xproperty.state = PropertyNewValue;
	xproperty.time = SdlTkX.time_count;
	SdlTkQueueEvent((XEvent *) &xproperty);
	goto done;
    }
    /* FIXME: _NET_WM_ICON_NAME as well */

    if (type == XA_STRING) {
	struct prop_key key;
	struct prop_val *val;
	Tcl_HashEntry *hPtr;
	int isNew;

	if (!prop_initialized) {
	    Tcl_InitHashTable(&prop_table,
			      sizeof (struct prop_key) / sizeof (int));
	    prop_initialized = 1;
	}
	memset(&key, 0, sizeof (key));
	key.w = (_Window *) w;
	key.name = property;
	hPtr = Tcl_CreateHashEntry(&prop_table, (ClientData) &key, &isNew);
	if (mode == PropModeReplace) {
	    if (!isNew) {
		ckfree(Tcl_GetHashValue(hPtr));
	    }
	    val = ckalloc(nelements + sizeof (struct prop_val));
	    val->length = nelements;
	    memcpy(val->data, data, nelements);
	    Tcl_SetHashValue(hPtr, (ClientData) val);
	} else if (mode == PropModeAppend) {
	    if (!isNew) {
		struct prop_val *old =
		    (struct prop_val *) Tcl_GetHashValue(hPtr);
		int len;

		len = old->length + nelements;
		val = ckalloc(len + sizeof (struct prop_val));
		val->length = len;
		memcpy(val->data, old->data, old->length);
		memcpy(val->data + old->length, data, nelements);
		ckfree(old);
		Tcl_SetHashValue(hPtr, (ClientData) val);
	    } else {
		val = ckalloc(nelements + sizeof (struct prop_val));
		val->length = nelements;
		memcpy(val->data, data, nelements);
		Tcl_SetHashValue(hPtr, (ClientData) val);
	    }
	}
	if (!IS_ROOT(w)) {
	    XPropertyEvent xproperty;

	    memset(&xproperty, 0, sizeof (xproperty));
	    xproperty.type = PropertyNotify;
	    xproperty.serial = _w->display->request;
	    xproperty.send_event = False;
	    xproperty.atom = property;
	    xproperty.display = _w->display;
	    xproperty.window = w;
	    xproperty.state = PropertyNewValue;
	    xproperty.time = SdlTkX.time_count;
	    SdlTkQueueEvent((XEvent *) &xproperty);
	}
    }
done:
    SdlTkUnlock(display);
}

static void
SdlTkChangeWindowAttributes(Display *display, Window w,
			    unsigned long valueMask,
			    XSetWindowAttributes *attributes)
{
    _Window *_w = (_Window *) w;

    if (_w->display == NULL) {
	return;
    }
    if (valueMask & CWBackPixel) {
	_w->back_pixel_set = 1;
	_w->back_pixel = attributes->background_pixel;
	_w->back_pixmap = NULL;
    } else if (valueMask & CWBackPixmap) {
	_w->back_pixel_set = 0;
	if (attributes->background_pixmap == ParentRelative) {
	    _w->back_pixmap = (_Pixmap *) attributes->background_pixmap;
	} else {
	    _w->back_pixmap = NULL;
	}
    }
    if (valueMask & CWCursor) {
	XDefineCursor(display, w, attributes->cursor);
    }
    if (valueMask & CWEventMask) {
	_w->atts.your_event_mask = attributes->event_mask;
    }
    if (valueMask & CWOverrideRedirect) {

	/* Tk won't call us unless it changed */
	_w->atts.override_redirect = attributes->override_redirect;

	if (attributes->override_redirect) {

	    /*
	     * Is override_redirect, wasn't before.
	     * Decorative frame may not have been allocated yet
	     * if the window was never mapped
	     */

	    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
		/* Reparent to root */
		SdlTkReparentWindow(display, w, SdlTkX.screen->root,
				    _w->parent->atts.x, _w->parent->atts.y);
		SdlTkGenerateConfigureNotify(NULL, w);
	    }
	} else {
	    /* Was override_redirect, isn't now */
	    SdlTkUnmapWindow(display, w);
	    SdlTkMapWindow(display, w);
	}
    }
}

void
XChangeWindowAttributes(Display *display, Window w,
			unsigned long valueMask,
			XSetWindowAttributes *attributes)
{
    SdlTkLock(display);
    display->request++;
    SdlTkChangeWindowAttributes(display, w, valueMask, attributes);
    SdlTkUnlock(display);
}

#if 0
void
XClipBox(Region r, XRectangle *rect_return)
{
}
#endif

int
XCloseDisplay(Display *display)
{
    Display *prev, *curr;
    _XSQEvent *qevent;
    _Window *_w;

    EVLOG("XCloseDisplay %p", display);

    SdlTkLock(display);

    display->request++;

#ifdef _WIN32
    if ((HANDLE) display->fd != INVALID_HANDLE_VALUE) {
	CloseHandle((HANDLE) display->fd);
	display->fd = INVALID_HANDLE_VALUE;
    }
#else
    if (display->fd >= 0) {
	close(display->fd);
	display->fd = -1;
    }
    if (display->ext_number >= 0) {
	close(display->ext_number);
	display->ext_number = -1;
    }
#endif

    ckfree((char *) display->screens);
    display->screens = NULL;
    if (display->display_name != NULL) {
	ckfree((char *) display->display_name);
	display->display_name = NULL;
    }

    /* Remove left over windows */
    _w = ((_Window *) SdlTkX.screen->root)->child;
    while (_w != NULL) {
	if (_w->display == display) {
	    SdlTkDestroyWindow(display, (Window) _w);
	    _w = ((_Window *) SdlTkX.screen->root)->child;
	    continue;
	}
	_w = _w->next;
    }

#ifdef ANDROID
    if (display->gl_rend != NULL) {
	SDL_DestroyRenderer((SDL_Renderer *) display->gl_rend);
    }
#endif

    /* Cleanup event queues */
    Tcl_MutexLock((Tcl_Mutex *) &display->qlock);
    qevent = display->head;
    while (qevent != NULL) {
	_XSQEvent *next = qevent->next;

	ckfree((char *) qevent);
	qevent = next;
    }
    qevent = display->qfree;
    while (qevent != NULL) {
	_XSQEvent *next = qevent->next;

	ckfree((char *) qevent);
	qevent = next;
    }
    Tcl_MutexUnlock((Tcl_Mutex *) &display->qlock);
    Tcl_MutexFinalize((Tcl_Mutex *) &display->qlock);

    /* Dequeue cloned display */
    prev = SdlTkX.display;
    curr = prev->next_display;
    while ((curr != NULL) && (curr != display)) {
	prev = curr;
	curr = curr->next_display;
    }
    if (curr == display) {
	prev->next_display = display->next_display;
    }

    --num_displays;

    if (display->agg2d != NULL) {
	XDestroyAgg2D(display, display->agg2d);
    }

    /* Free GCs and Pixmaps */
    while (display->gcs != NULL) {
	XGCValues *next = display->gcs->next;

	if (display->gcs->clip_mask != None) {
	    ckfree((char*) display->gcs->clip_mask);
	}
	memset(display->gcs, 0xFE, sizeof (XGCValues));
	ckfree((char *) display->gcs);
	display->gcs = next;
    }
    while (display->pixmaps != NULL) {
	_Pixmap *_p = (_Pixmap *) display->pixmaps;

	display->pixmaps = _p->next;
	SDL_FreeSurface(_p->sdl);
	memset(_p, 0xFE, sizeof (_Pixmap));
	ckfree((char *) _p);
    }

    xlib_grab = NULL;
    Tcl_ConditionNotify(&xlib_cond);
    SdlTkUnlock(display);

    memset(display, 0, sizeof (Display));
    ckfree((char *) display);

    return 0;
}

void
XConfigureWindow(Display *display, Window w, unsigned int value_mask,
		 XWindowChanges *values)
{
    _Window *_w = (_Window *) w;

    SdlTkLock(display);
    display->request++;

    if (_w->display == NULL) {
	goto done;
    }

    /*
     * I don't think this border_width is ever used, so hard to test it.
     * A widget's -borderwidth option is completely different.
     */

    if (value_mask & CWBorderWidth) {
	_w->atts.border_width = values->border_width;
	_w->parentWidth = _w->atts.width + 2 * values->border_width;
	_w->parentHeight = _w->atts.height + 2 * values->border_width;
	SdlTkScreenChanged();
    }

    /* Need this for Tk_RestackWindow and Tk_MakeWindowExist */
    if (value_mask & CWStackMode) {
	_Window *sibling = NULL;

	if (value_mask & CWSibling) {
	    sibling = (_Window *) values->sibling;
	}

	SdlTkRestackWindow(_w, sibling, values->stack_mode);
	/*SdlTkRestackTransients(_w); -- this isn't a toplevel */

	SdlTkScreenChanged();
    }
done:
    SdlTkUnlock(display);
}

int
XConvertSelection(Display *display, Atom selection, Atom target,
		  Atom property, Window requestor, Time time)
{
    return 0;
}

void
XCopyArea(Display *display, Drawable src, Drawable dest, GC gc,
	  int src_x, int src_y, unsigned int width, unsigned int height,
	  int dest_x, int dest_y)
{
    SdlTkLock(display);
    display->request++;

    SdlTkGfxCopyArea(src, dest, gc, src_x, src_y, width, height,
		     dest_x, dest_y);

    if (IS_WINDOW(dest)) {
	TkpClipMask *clipPtr = (TkpClipMask *) gc->clip_mask;

	SdlTkScreenChanged();
	if ((clipPtr != NULL) && (clipPtr->type == TKP_CLIP_REGION)) {
	    Region clipRgn = (Region) clipPtr->value.region;

	    SdlTkDirtyRegion(dest, clipRgn);
	} else {
	    SdlTkDirtyArea(dest, dest_x, dest_y, width, height);
	}
    }
    SdlTkUnlock(display);
}

void
XCopyPlane(Display *display, Drawable src, Drawable dest, GC gc,
	   int src_x, int src_y, unsigned int width, unsigned int height,
	   int dest_x, int dest_y, unsigned long plane)
{
    SdlTkLock(display);
    display->request++;

    SdlTkGfxDrawBitmap(src, dest, gc, src_x, src_y, width, height,
		       dest_x, dest_y);

    if (IS_WINDOW(dest)) {
	SdlTkScreenChanged();
    }
    SdlTkUnlock(display);
}

Pixmap
XCreateBitmapFromData(Display *display, Drawable d, _Xconst char *data,
		      unsigned int width, unsigned int height)
{
    XImage ximage;
    Pixmap pix = None;
    _Pixmap *_p;
    SDL_Surface *sdl;
    SDL_Color colors[2];
    SDL_Palette *pal;

    SdlTkLock(display);

    /* Use 1 byte-per-pixel for efficient drawing/stippling */
    sdl = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height,
			       8, 0, 0, 0, 0);
    if (sdl == NULL) {
	goto done;
    }

    /*
     * New 8-bit surfaces have an empty palette. Set the palette
     * to black and white.
     */

    colors[0].r = colors[0].g = colors[0].b = 0;
    colors[1].r = colors[1].g = colors[1].b = 255;
    colors[0].a = colors[1].a = 255;
    pal = SDL_AllocPalette(256);
    SDL_SetPaletteColors(pal, colors + 1, 0, 1);
    SDL_SetPaletteColors(pal, colors + 0, 255, 1);
    SDL_SetSurfacePalette(sdl, pal);
    SDL_FreePalette(pal);

    _p = (_Pixmap *) ckalloc(sizeof (_Pixmap));
    memset(_p, 0, sizeof (_Pixmap));
    _p->type = DT_PIXMAP;
    _p->sdl = sdl;
    _p->format = SdlTkPixelFormat(sdl);
    _p->next = (_Pixmap *) display->pixmaps;
    display->pixmaps = _p;

    pix = (Pixmap) _p;

    ximage.height = height;
    ximage.width = width;
    ximage.depth = 1;
    ximage.bits_per_pixel = 1;
    ximage.xoffset = 0;
    ximage.format = XYPixmap;
    ximage.data = (char *)data;
    ximage.byte_order = LSBFirst;
    ximage.bitmap_unit = 8;
    ximage.bitmap_bit_order = LSBFirst;
    ximage.bitmap_pad = 8;
    ximage.bytes_per_line = (width+7)/8;
    ximage.red_mask = 0;
    ximage.green_mask = 0;
    ximage.blue_mask = 0;

    SdlTkGfxPutImage(pix, None, &ximage, 0, 0, 0, 0, width, height, 1);

done:
    SdlTkUnlock(display);

    return pix;
}

Colormap
XCreateColormap(Display *display, Window w, Visual *visual, int alloc)
{
    _Colormap *_colormap;

    _colormap = (_Colormap *) ckalloc(sizeof (_Colormap));
    _colormap->whatever = 1234;

    return (Colormap) _colormap;
}

GC
XCreateGC(Display *display, Drawable d, unsigned long mask, XGCValues *values)
{
    GC gp;

    /*
     * In order to have room for a dash list, dash_array defines extra
     * chars. The list is assumed to end with a 0-char, so this must be
     * set explicitely during initialization.
     */

    if (display == NULL) {
	return None;
    }

    gp = (XGCValues *)ckalloc(sizeof (XGCValues));
    if (!gp) {
	return None;
    }
    memset(gp, 0, sizeof (XGCValues));
    gp->next = display->gcs;
    display->gcs = gp;
    gp->function = (mask & GCFunction) ? values->function : GXcopy;
    gp->plane_mask = (mask & GCPlaneMask) ? values->plane_mask : ~0;
    gp->foreground = (mask & GCForeground) ? values->foreground : 0;
    gp->background = (mask & GCBackground) ? values->background : 0xffffff;
    gp->line_width = (mask & GCLineWidth) ? values->line_width : 1;
    gp->line_style = (mask & GCLineStyle) ? values->line_style : LineSolid;
    gp->cap_style = (mask & GCCapStyle)	? values->cap_style : 0;
    gp->join_style = (mask & GCJoinStyle) ? values->join_style : 0;
    gp->fill_style = (mask & GCFillStyle) ? values->fill_style : FillSolid;
    gp->fill_rule = (mask & GCFillRule)	? values->fill_rule : WindingRule;
    gp->arc_mode = (mask & GCArcMode) ? values->arc_mode : ArcPieSlice;
    gp->tile = (mask & GCTile) ? values->tile : None;
    gp->stipple = (mask & GCStipple) ? values->stipple : None;
    gp->ts_x_origin = (mask & GCTileStipXOrigin) ? values->ts_x_origin : 0;
    gp->ts_y_origin = (mask & GCTileStipYOrigin) ? values->ts_y_origin : 0;
    gp->font = (mask & GCFont) ? values->font : None;
    gp->subwindow_mode = (mask & GCSubwindowMode) ? values->subwindow_mode
	: ClipByChildren;
    gp->graphics_exposures = (mask & GCGraphicsExposures) ?
	values->graphics_exposures : True;
    gp->clip_x_origin = (mask & GCClipXOrigin) ? values->clip_x_origin : 0;
    gp->clip_y_origin = (mask & GCClipYOrigin) ? values->clip_y_origin : 0;
    gp->dash_offset = (mask & GCDashOffset) ? values->dash_offset : 0;
    if (mask & GCDashList) {
	gp->dashes = values->dashes ? values->dashes : 0;
	(&gp->dashes)[1] = values->dashes ? values->dashes : 0;
	(&gp->dashes)[2] = 0;
    } else {
	gp->dashes = 2;
	(&gp->dashes)[1] = 2;
	(&gp->dashes)[2] = 0;
    }
    if (mask & GCClipMask) {
	gp->clip_mask = (Pixmap)ckalloc(sizeof (TkpClipMask));
	((TkpClipMask*)gp->clip_mask)->type = TKP_CLIP_PIXMAP;
	((TkpClipMask*)gp->clip_mask)->value.pixmap = values->clip_mask;
    } else {
	gp->clip_mask = None;
    }
    return gp;
}

int
XCopyGC(Display *display, GC src, unsigned long mask, GC dest)
{
    if (mask & GCFunction) {
        dest->function = src->function;
    }
    if (mask & GCPlaneMask) {
        dest->plane_mask = src->plane_mask;
    }
    if (mask & GCForeground) {
        dest->foreground = src->foreground;
    }
    if (mask & GCBackground) {
        dest->background = src->background;
    }
    if (mask & GCLineWidth) {
        dest->line_width = src->line_width;
    }
    if (mask & GCLineStyle) {
        dest->line_style = src->line_style;
    }
    if (mask & GCCapStyle) {
        dest->cap_style = src->cap_style;
    }
    if (mask & GCJoinStyle) {
        dest->join_style = src->join_style;
    }
    if (mask & GCFillStyle) {
        dest->fill_style = src->fill_style;
    }
    if (mask & GCFillRule) {
        dest->fill_rule = src->fill_rule;
    }
    if (mask & GCArcMode) {
        dest->arc_mode = src->arc_mode;
    }
    if (mask & GCTile) {
        dest->tile = src->tile;
    }
    if (mask & GCStipple) {
        dest->stipple = src->stipple;
    }
    if (mask & GCTileStipXOrigin) {
        dest->ts_x_origin = src->ts_x_origin;
    }
    if (mask & GCTileStipYOrigin) {
        dest->ts_y_origin = src->ts_y_origin;
    }
    if (mask & GCFont) {
        dest->font = src->font;
    }
    if (mask & GCSubwindowMode) {
        dest->subwindow_mode = src->subwindow_mode;
    }
    if (mask & GCGraphicsExposures) {
        dest->graphics_exposures = src->graphics_exposures;
    }
    if (mask & GCClipXOrigin) {
        dest->clip_x_origin = src->clip_x_origin;
    }
    if (mask & GCClipYOrigin) {
        dest->clip_y_origin = src->clip_y_origin;
    }
    if (mask & GCDashOffset) {
        dest->dash_offset = src->dash_offset;
    }
    if (mask & GCDashList) {
        dest->dashes = src->dashes;
	memcpy(dest->dash_array, src->dash_array, sizeof (dest->dash_array));
    }
    if (mask & GCClipMask) {
        if ((dest->clip_mask == None) && (src->clip_mask != None)) {
	    dest->clip_mask = (Pixmap) ckalloc(sizeof (TkpClipMask));
	    ((TkpClipMask *) dest->clip_mask)->type = TKP_CLIP_PIXMAP;
	    ((TkpClipMask *) dest->clip_mask)->value.pixmap =
		((TkpClipMask *) src->clip_mask)->value.pixmap;
	} else if ((dest->clip_mask != None) && (src->clip_mask == None)) {
	    ckfree(dest->clip_mask);
	    dest->clip_mask = None;
	} else if ((dest->clip_mask != None) && (src->clip_mask != None)) {
	    ((TkpClipMask *) dest->clip_mask)->value.pixmap =
		((TkpClipMask *) src->clip_mask)->value.pixmap;
	}
    }
    return 1;
}

Cursor
XCreateGlyphCursor(Display *display, Font source_font, Font mask_font,
		   unsigned int source_char, unsigned int mask_char,
		   XColor *foreground_color, XColor *background_color)
{
    _Cursor *_c = (_Cursor *) ckalloc(sizeof (_Cursor));
    int shape;

    switch (source_char) {
    case XC_xterm:
	shape = SDL_SYSTEM_CURSOR_IBEAM;
	break;
    case XC_watch:
	shape = SDL_SYSTEM_CURSOR_WAIT;
	break;
    case XC_cross:
    case XC_cross_reverse:
    case XC_tcross:
    case XC_crosshair:
    case XC_diamond_cross:
    case XC_circle:
    case XC_dot:
    case XC_dotbox:
    case XC_draped_box:
	shape = SDL_SYSTEM_CURSOR_CROSSHAIR;
	break;
    case XC_hand1:
    case XC_hand2:
	shape = SDL_SYSTEM_CURSOR_HAND;
	break;
    case XC_sb_h_double_arrow:
    case XC_sb_left_arrow:
    case XC_sb_right_arrow:
	shape = SDL_SYSTEM_CURSOR_SIZEWE;
	break;
    case XC_sb_v_double_arrow:
    case XC_sb_up_arrow:
    case XC_sb_down_arrow:
    case XC_double_arrow:
	shape = SDL_SYSTEM_CURSOR_SIZENS;
	break;
    case XC_fleur:
	shape = SDL_SYSTEM_CURSOR_SIZEALL;
	break;
    case XC_pirate:
	shape = SDL_SYSTEM_CURSOR_NO;
	break;
    case XC_bottom_right_corner:
    case XC_top_left_corner:
	shape = SDL_SYSTEM_CURSOR_SIZENWSE;
	break;
    case XC_bottom_left_corner:
    case XC_top_right_corner:
	shape = SDL_SYSTEM_CURSOR_SIZENESW;
	break;
    default:
	shape = SDL_SYSTEM_CURSOR_ARROW;
	break;
    }
    _c->shape = shape;
    return (Cursor) _c;
}

XIC
XCreateIC(XIM xim, ...)
{
    return NULL;
}

XImage *
XCreateImage(Display *display, Visual *visual, unsigned int depth,
	     int format, int offset, char *data,
	     unsigned int width, unsigned int height,
	     int bitmap_pad, int bytes_per_line)
{
    XImage *ximage = (XImage *) ckalloc(sizeof (XImage));

    SdlTkLock(display);

    display->request++;

    ximage->height = height;
    ximage->width = width;
    ximage->depth = depth;
    ximage->xoffset = offset;
    ximage->format = format;
    ximage->data = data;
    ximage->bitmap_pad = bitmap_pad;
    if (bytes_per_line == 0) {
	if (depth == 8) {
	    ximage->bytes_per_line = width;
	} else {
	    ximage->bytes_per_line =
		width * SdlTkX.sdlsurf->format->BytesPerPixel;
	}
    } else {
	ximage->bytes_per_line = bytes_per_line;
    }

    if (format == ZPixmap) {
	if (depth == 8) {
	    ximage->bits_per_pixel = 8;
	    ximage->bitmap_unit = 8;
	} else {
	    ximage->bits_per_pixel = SdlTkX.sdlsurf->format->BitsPerPixel;
	    ximage->bitmap_unit = SdlTkX.sdlsurf->format->BitsPerPixel;
	}
    } else {
	ximage->bits_per_pixel = 1;
	ximage->bitmap_unit = 8;
    }
    ximage->byte_order = LSBFirst;
    ximage->bitmap_bit_order = LSBFirst;
    ximage->red_mask = visual->red_mask;
    ximage->green_mask = visual->green_mask;
    ximage->blue_mask = visual->blue_mask;

    ximage->obdata = NULL;
    ximage->f.destroy_image = SdlTkImageDestroy;
    ximage->f.get_pixel = SdlTkImageGetPixel;
    ximage->f.put_pixel = SdlTkImagePutPixel;
    ximage->f.sub_image = NULL;
    ximage->f.add_pixel = NULL;

    SdlTkUnlock(display);

    return ximage;
}

Pixmap
XCreatePixmap(Display *display, Drawable d, unsigned int width,
	      unsigned int height, unsigned int depth)
{
    _Pixmap *_p = NULL;
    SDL_Surface *sdl;

    SdlTkLock(display);

    display->request++;

    if (depth == 8) {
	sdl = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height,
				   8, 0, 0, 0, 0);
	if (sdl != NULL) {
	    int i;
	    SDL_Palette *pal = SDL_AllocPalette(256);
	    SDL_Color graymap[256];

	    for (i = 0; i < 256; i++) {
		graymap[i].r = graymap[i].b = graymap[i].g = i;
		graymap[i].a = 255;
	    }
	    SDL_SetPaletteColors(pal, graymap, 0, 256);
	    SDL_SetSurfacePalette(sdl, pal);
	    SDL_FreePalette(pal);
	}
    } else if (depth == (unsigned) -32) {
	/* special case: tkpath + AGG2D, force BGRA8 for AGG2D */
	sdl = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 32,
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
				   0x0000FF00,
				   0x00FF0000,
				   0xFF000000,
				   0x000000FF
#else
				   0x00FF0000,
				   0x0000FF00,
				   0x000000FF,
				   0xFF000000
#endif
				  );
	SDL_SetSurfaceBlendMode(sdl, SDL_BLENDMODE_NONE);
    } else {
	sdl = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height,
				   (depth == 1) ? depth :
				   SdlTkX.sdlsurf->format->BitsPerPixel,
				   SdlTkX.sdlsurf->format->Rmask,
				   SdlTkX.sdlsurf->format->Gmask,
				   SdlTkX.sdlsurf->format->Bmask,
				   SdlTkX.sdlsurf->format->Amask);
    }
    if (sdl == NULL) {
	goto done;
    }

    _p = (_Pixmap *) ckalloc(sizeof (_Pixmap));
    memset(_p, 0, sizeof (_Pixmap));
    _p->type = DT_PIXMAP;
    _p->sdl = sdl;
    _p->format = SdlTkPixelFormat(sdl);
    _p->next = (_Pixmap *) display->pixmaps;
    display->pixmaps = _p;

done:
    SdlTkUnlock(display);

    return (Pixmap) _p;
}

Cursor
XCreatePixmapCursor(Display *display, Pixmap source, Pixmap mask,
		    XColor *foreground_color, XColor *background_color,
		    unsigned int x, unsigned int y)
{
    _Cursor *_c = (_Cursor *) ckalloc(sizeof (_Cursor));

    _c->shape = SDL_SYSTEM_CURSOR_ARROW;
    return (Cursor) _c;
}

Window
SdlTkCreateWindow(Display *display, Window parent, int x, int y,
		  unsigned int width, unsigned int height,
		  unsigned int border_width, int depth, unsigned int clazz,
		  Visual *visual, unsigned long valuemask,
		  XSetWindowAttributes *attributes)
{
    _Window *_parent = (_Window *) parent;
    _Window *_w;

    _w = (SdlTkX.nwfree >= 16) ? SdlTkX.wfree : NULL;
    if (_w == NULL) {
	_w = (_Window *) ckalloc(sizeof (_Window));
	memset(_w, 0, sizeof (_Window));
	SdlTkX.nwtotal++;
    } else {
	SdlTkX.wfree = _w->next;
	if (SdlTkX.wfree == NULL) {
	    SdlTkX.wtail = NULL;
	}
	SdlTkX.nwfree--;
    }

    _w->type = DT_WINDOW;
    _w->display = display;
    _w->parent = _parent;
    _w->atts.x = x;
    _w->atts.y = y;
    _w->atts.width = width;
    _w->atts.height = height;
    _w->atts.border_width = border_width;
    _w->atts.visual = visual;
    _w->atts.map_state = IsUnmapped;
    _w->atts.override_redirect =
	((attributes != NULL) && (valuemask & CWOverrideRedirect)) ?
	attributes->override_redirect : False;
    _w->atts.your_event_mask =
	((attributes != NULL) && (valuemask & CWEventMask)) ?
	attributes->event_mask : 0;
    if ((attributes != NULL) && (valuemask & CWBackPixel)) {
	_w->back_pixel_set = 1;
	_w->back_pixel = attributes->background_pixel;
	_w->back_pixmap = NULL;
    } else if ((attributes != NULL) && (valuemask & CWBackPixmap)) {
	_w->back_pixel_set = 0;
	if (attributes->background_pixmap == ParentRelative) {
	    _w->back_pixmap = (_Pixmap *) attributes->background_pixmap;
	} else {
	    _w->back_pixmap = NULL;
	}
    }

    /*
     * A window's requested width/height are *inside* its borders.
     * It doesn't look like Tk uses this border_width anywhere, it is
     * different than the -borderwidth option.
     */

    _w->parentWidth = width + 2 * border_width;
    _w->parentHeight = height + 2 * border_width;

    _w->visRgn = SdlTkRgnPoolGet();
    _w->visRgnInParent = SdlTkRgnPoolGet();
    _w->dirtyRgn = SdlTkRgnPoolGet();

    _w->clazz = (clazz == InputOnly) ? InputOnly : InputOutput;

    /* Make first child of parent */
    _w->next = _parent->child;
    _parent->child = _w;

    if (_parent->atts.your_event_mask & SubstructureNotifyMask) {
	/* Make CreateNotify */
	XEvent event;

	memset(&event, 0, sizeof (event));
	event.type = CreateNotify;
	event.xcreatewindow.serial = _w->display->request;
	event.xcreatewindow.send_event = False;
	event.xcreatewindow.display = _w->display;
	event.xcreatewindow.parent = parent;
	event.xcreatewindow.window = (Window) _w;
	event.xcreatewindow.x = _w->atts.x;
	event.xcreatewindow.y = _w->atts.y;
	event.xcreatewindow.width = _w->atts.width;
	event.xcreatewindow.height = _w->atts.height;
	event.xcreatewindow.border_width = _w->atts.border_width;
	event.xcreatewindow.override_redirect = _w->atts.override_redirect;
	SdlTkQueueEvent(&event);
	if (!IS_ROOT(parent) && (_parent->display != _w->display)) {
	    event.xcreatewindow.serial = _parent->display->request;
	    event.xcreatewindow.display = _parent->display;
	    SdlTkQueueEvent(&event);
	}
    }

    return (Window) _w;
}

Window
XCreateWindow(Display *display, Window parent, int x, int y,
	      unsigned int width, unsigned int height,
	      unsigned int border_width, int depth, unsigned int clazz,
	      Visual *visual, unsigned long valuemask,
	      XSetWindowAttributes *attributes)
{
    Window w;

    SdlTkLock(display);
    display->request++;
    w = SdlTkCreateWindow(display, parent, x, y, width, height,
			  border_width, depth, clazz, visual,
			  valuemask, attributes);
    SdlTkUnlock(display);
    return w;
}

void
XDeleteProperty(Display *display, Window w, Atom property)
{
    struct prop_key key;
    _Window *_w = (_Window *) w;
    Tcl_HashEntry *hPtr;

    SdlTkLock(display);
    display->request++;
    if (_w->display == NULL) {
	goto done;
    }
    if (property == XA_WM_TRANSIENT_FOR) {
	_w->master = NULL;
	goto done;
    }
    if (!prop_initialized) {
	Tcl_InitHashTable(&prop_table,
			  sizeof (struct prop_key) / sizeof (int));
	prop_initialized = 1;
    }
    memset(&key, 0, sizeof (key));
    key.w = (_Window *) w;
    key.name = property;
    hPtr = Tcl_FindHashEntry(&prop_table, (ClientData) &key);
    if (hPtr != NULL) {
	ckfree(Tcl_GetHashValue(hPtr));
	Tcl_DeleteHashEntry(hPtr);
	if (!IS_ROOT(w)) {
	    XPropertyEvent xproperty;

	    memset(&xproperty, 0, sizeof (xproperty));
	    xproperty.type = PropertyNotify;
	    xproperty.serial = _w->display->request;
	    xproperty.send_event = False;
	    xproperty.atom = property;
	    xproperty.display = _w->display;
	    xproperty.window = (Window) _w;
	    xproperty.state = PropertyDelete;
	    xproperty.time = SdlTkX.time_count;
	    SdlTkQueueEvent((XEvent *) &xproperty);
	}
    }
done:
    SdlTkUnlock(display);
}

void
XDestroyIC(XIC ic)
{
}

static void
SdlTkDestroyWindow(Display *display, Window w)
{
    _Window *_w = (_Window *) w;
    _Window *wdec = ((_w->parent != NULL) && (_w->parent->dec != NULL)) ?
			_w->parent : NULL;
    XEvent event;
    int hadFocus = 0, doNotify;

    _w->tkwin = NULL;
    if (_w->display == NULL) {
	return;
    }
#ifndef ANDROID
    if (_w->gl_rend) {
	SDL_DestroyRenderer(_w->gl_rend);
	_w->gl_rend = NULL;
    }
    if (_w->gl_wind) {
	SDL_DestroyWindow(_w->gl_wind);
	_w->gl_wind = NULL;
    }
#endif
    if (_w->gl_tex != NULL) {
	SDL_DestroyTexture(_w->gl_tex);
	_w->gl_tex = NULL;
    }
    if (_w->display->focus_window == w) {
	_w->display->focus_window = None;
    }
    if (SdlTkX.focus_window == w) {
	hadFocus = 1;
	SdlTkX.focus_window = None;
    }
    if (SdlTkX.keyboard_window == _w) {
	SdlTkX.keyboard_window = NULL;
    }
    if (SdlTkX.focus_window_old == w) {
	SdlTkX.focus_window_old = None;
    }
    if (SdlTkX.focus_window_not_override == w) {
	SdlTkX.focus_window_not_override = None;
    }
    SdlTkClearPointer(_w);
    if (SdlTkX.current_primary == w) {
	SdlTkX.current_primary = None;
	SDL_SetClipboardText("");
    }
    if (SdlTkX.current_clipboard == w) {
	SdlTkX.current_clipboard = None;
	SDL_SetClipboardText("");
    }

    if (_w->atts.map_state != IsUnmapped) {
	SdlTkUnmapWindow(display, w);
    }

    /* Destroy children recursively */
    while (_w->child != NULL) {
	SdlTkDestroyWindow(display, (Window) _w->child);
    }

    doNotify = (_w->atts.your_event_mask & StructureNotifyMask) != 0;
    doNotify = doNotify && (_w->display != _w->parent->display);

    /* Remove from parent */
    SdlTkRemoveFromParent(_w);

    /* Free the decorative frame record */
    if (_w->dec != NULL) {
	SdlTkDecDestroy(_w);
    }

    if (_w->title != NULL) {
	ckfree((char *) _w->title);
    }

    SdlTkRgnPoolFree(_w->visRgn);
    SdlTkRgnPoolFree(_w->visRgnInParent);
    SdlTkRgnPoolFree(_w->dirtyRgn);

    if (doNotify) {
	event.type = DestroyNotify;
	event.xdestroywindow.serial = _w->display->request;
	event.xdestroywindow.send_event = False;
	event.xdestroywindow.display = _w->display;
	event.xdestroywindow.event = w;
	event.xdestroywindow.window = w;
	SdlTkQueueEvent(&event);
    }

    memset(_w, 0, sizeof (_Window));
    if (SdlTkX.wtail == NULL) {
	SdlTkX.wtail = SdlTkX.wfree = _w;
    } else {
	SdlTkX.wtail->next = _w;
	SdlTkX.wtail = _w;
    }
    SdlTkX.nwfree++;

    /*
     * Remove properties
     */
    if (prop_initialized) {
	Tcl_HashEntry *hPtr;
	Tcl_HashSearch search;
	struct prop_key *keyPtr;

	hPtr = Tcl_FirstHashEntry(&prop_table, &search);
	while (hPtr != NULL) {
	     keyPtr = (struct prop_key *) Tcl_GetHashKey(&prop_table, hPtr);

	     if (keyPtr->w == _w) {
		ckfree(Tcl_GetHashValue(hPtr));
		Tcl_DeleteHashEntry(hPtr);
	     }
	     hPtr = Tcl_NextHashEntry(&search);
	}
    }

    /*
     * Destroy decorative frame:
     * actually this shouldn't happen, since Tk reparents the wrapper
     * to the root before destroying it, and reparenting should destroy the
     * decorative frame
     */

    if (wdec != NULL) {
	SdlTkDestroyWindow(display, (Window) wdec);
    }

    if (hadFocus) {
	SdlTkLostFocusWindow();
    }

    SdlTkScreenChanged();
}

void
XDestroyWindow(Display *display, Window w)
{
    SdlTkLock(display);
    display->request++;
    SdlTkDestroyWindow(display, w);
    SdlTkUnlock(display);
}

void
XDrawArc(Display *display, Drawable d, GC gc, int x, int y,
	 unsigned int width, unsigned int height, int start, int extent)
{
    SdlTkLock(display);
    display->request++;

    SdlTkGfxDrawArc(d, gc, x, y, width, height, start, extent);

    if (IS_WINDOW(d)) {
	SdlTkScreenChanged();
	SdlTkDirtyAll(d);
    }
    SdlTkUnlock(display);
}

void
XDrawArcs(Display *display, Drawable d, GC gc, XArc *arcs, int narcs)
{
    int n;

    SdlTkLock(display);
    display->request++;

    for (n = 0; n < narcs; n++) {
	SdlTkGfxDrawArc(d, gc, arcs[n].x, arcs[n].y,
			arcs[n].width, arcs[n].height, arcs[n].angle1,
			arcs[n].angle2);
    }

    if (IS_WINDOW(d)) {
	SdlTkScreenChanged();
	SdlTkDirtyAll(d);
    }
    SdlTkUnlock(display);
}

void
XDrawLine(Display *display, Drawable d, GC gc, int x1, int y1, int x2, int y2)
{
    XPoint points[2];

    points[0].x = x1;
    points[0].y = y1;
    points[1].x = x2;
    points[1].y = y2;
    XDrawLines(display, d, gc, points, 2, CoordModeOrigin);
}

void
XDrawLines(Display *display, Drawable d, GC gc, XPoint *points,
	   int npoints, int mode)
{
    SdlTkLock(display);
    display->request++;

    SdlTkGfxDrawLines(d, gc, points, npoints, mode);

    if (IS_WINDOW(d)) {
	SdlTkScreenChanged();
	SdlTkDirtyAll(d);
    }
    SdlTkUnlock(display);
}

void
XDrawPoint(Display *display, Drawable d, GC gc, int x, int y)
{
    SdlTkLock(display);
    display->request++;

    SdlTkGfxDrawPoint(d, gc, x, y);
    SdlTkUnlock(display);
}

void
XDrawPoints(Display *display, Drawable d, GC gc, XPoint *points, int npoints,
	    int mode)
{
    int n, x = 0, y = 0;

    SdlTkLock(display);
    display->request++;

    for (n = 0; n < npoints; n++) {
	if ((n == 0) || (mode == CoordModeOrigin)) {
	    x = points[n].x;
	    y = points[n].y;
	} else {
	    x += points[n].x;
	    y += points[n].y;
	}
	SdlTkGfxDrawPoint(d, gc, x, y);
    }

    if (IS_WINDOW(d)) {
	SdlTkScreenChanged();
	SdlTkDirtyAll(d);
    }
    SdlTkUnlock(display);
}

void
XDrawRectangle(Display *display, Drawable d, GC gc, int x, int y,
	       unsigned int width, unsigned int height)
{
    SdlTkLock(display);
    display->request++;

    SdlTkGfxDrawRect(d, gc, x, y, width, height);

    if (IS_WINDOW(d)) {
	SdlTkScreenChanged();
	SdlTkDirtyAll(d);
    }
    SdlTkUnlock(display);
}

void
XDrawRectangles(Display *display, Drawable d, GC gc, XRectangle rects[],
		int nrects)
{
    int n;

    SdlTkLock(display);
    display->request++;

    for (n = 0; n < nrects; n++) {
	SdlTkGfxDrawRect(d, gc, rects[n].x, rects[n].y, rects[n].width,
			 rects[n].height);
    }

    if (IS_WINDOW(d)) {
	SdlTkScreenChanged();
	SdlTkDirtyAll(d);
    }
    SdlTkUnlock(display);
}

void
XDrawSegments(Display *display, Drawable d, GC gc, XSegment *segs, int nsegs)
{
    int n;
    XPoint points[2];

    SdlTkLock(display);
    display->request++;

    for (n = 0; n < nsegs; n++) {
	points[0].x = segs[n].x1;
	points[0].y = segs[n].y1;
	points[1].x = segs[n].x2;
	points[1].y = segs[n].y2;
	SdlTkGfxDrawLines(d, gc, points, 2, CoordModeOrigin);
    }

    if (IS_WINDOW(d)) {
	SdlTkScreenChanged();
	SdlTkDirtyAll(d);
    }
    SdlTkUnlock(display);
}

int
XDrawString(Display *display, Drawable d, GC gc, int x, int y,
	    _Xconst char *string, int length)
{
    SdlTkLock(display);
    display->request++;

    SdlTkGfxDrawString(d, gc, x, y, string, length, 0.0, NULL, NULL);

    if (IS_WINDOW(d)) {
	SdlTkScreenChanged();
	SdlTkDirtyAll(d);
    }
    SdlTkUnlock(display);

    return 0;
}

int
XDrawString16(Display *display, Drawable d, GC gc, int x, int y,
	      const XChar2b *string, int length)
{
    SdlTkLock(display);
    display->request++;

    SdlTkGfxDrawString(d, gc, x, y, (char *) string, length, 0.0, NULL, NULL);
    SdlTkUnlock(display);

    return 0;
}

int
XDrawStringAngle(Display *display, Drawable d, GC gc, int x, int y,
	         _Xconst char *string, int length, double angle,
		 int *xret, int *yret)
{
    SdlTkLock(display);
    display->request++;

    SdlTkGfxDrawString(d, gc, x, y, string, length, angle, xret, yret);

    if (IS_WINDOW(d)) {
	SdlTkScreenChanged();
	SdlTkDirtyAll(d);
    }
    SdlTkUnlock(display);

    return 0;
}

#if 0
int
XEmptyRegion(Region r)
{
    return 0;
}
#endif

int
XEventsQueued(Display *display, int mode)
{
    int ret;

    Tcl_MutexLock((Tcl_Mutex *) &display->qlock);
    ret = display->qlen;
    Tcl_MutexUnlock((Tcl_Mutex *) &display->qlock);
    return ret;
}

void
XFillArc(Display *display, Drawable d, GC gc, int x, int y,
	 unsigned int width, unsigned int height, int start, int extent)
{
    SdlTkLock(display);
    display->request++;

    SdlTkGfxFillArc(d, gc, x, y, width, height, start, extent);

    if (IS_WINDOW(d)) {
	SdlTkScreenChanged();
	SdlTkDirtyAll(d);
    }
    SdlTkUnlock(display);
}

void
XFillArcs(Display *display, Drawable d, GC gc, XArc *arcs, int narcs)
{
    int n;

    SdlTkLock(display);
    display->request++;

    for (n = 0; n < narcs; n++) {
	SdlTkGfxFillArc(d, gc, arcs[n].x, arcs[n].y,
			arcs[n].width, arcs[n].height, arcs[n].angle1,
			arcs[n].angle2);
    }

    if (IS_WINDOW(d)) {
	SdlTkScreenChanged();
	SdlTkDirtyAll(d);
    }
    SdlTkUnlock(display);
}

void
XFillPolygon(Display *display, Drawable d, GC gc, XPoint *points,
	     int npoints, int shape, int mode)
{
    SdlTkLock(display);
    display->request++;

    SdlTkGfxFillPolygon(d, gc, points, npoints, shape, mode);

    if (IS_WINDOW(d)) {
	SdlTkScreenChanged();
	SdlTkDirtyAll(d);
    }
    SdlTkUnlock(display);
}

void
XFillRectangle(Display *display, Drawable d, GC gc, int x, int y,
	       unsigned int width, unsigned int height)
{
    XRectangle rectangle;
    rectangle.x = x;
    rectangle.y = y;
    rectangle.width = width;
    rectangle.height = height;
    XFillRectangles(display, d, gc, &rectangle, 1);
}

void
XFillRectangles(Display *display, Drawable d, GC gc,
		XRectangle *rectangles, int nrectangles)
{
    int i;

    SdlTkLock(display);
    display->request++;

    for (i = 0; i < nrectangles; i++) {
	SdlTkGfxFillRect(d, gc, rectangles[i].x, rectangles[i].y,
	    rectangles[i].width, rectangles[i].height);
    }

    if (IS_WINDOW(d)) {
	SdlTkScreenChanged();
	SdlTkDirtyAll(d);
    }
    SdlTkUnlock(display);
}

Bool
XFilterEvent(XEvent *event, Window window)
{
    return 0;
}

void
SdlTkQueueEvent(XEvent *event)
{
    Display *display = event->xany.display;
    _XSQEvent *qevent;
    int trigger = 0;

    EVLOG("QueueEvent %d %p", event->xany.type, (void *) event->xany.window);

    if ((display == NULL) || (display->screens == NULL)) {
	return;
    }

    Tcl_MutexLock((Tcl_Mutex *) &display->qlock);

    /* Grab an unused event from the list */
    qevent = display->qfree;
    if (qevent == NULL) {
	qevent = (_XSQEvent *) ckalloc(sizeof (_XSQEvent));
	display->nqtotal++;
    } else {
	display->qfree = qevent->next;
    }
    qevent->event = *event;
    qevent->next = NULL;

    /* Append to event queue */
    if (display->tail) {
	display->tail->next = qevent;
#ifdef linux
	if (display->ext_number < 0) {
	    trigger = 1;
	} else
#endif
	{
	    trigger = (display->qlen & (64 - 1)) == 0;
	}
    } else {
	display->head = qevent;
	trigger = 1;
    }
    /* Delay trigger for (Graphics)Expose with count greater zero */
    if ((event->xany.type == Expose) && (event->xexpose.count > 0)) {
	trigger = 0;
    } else if ((event->xany.type == GraphicsExpose) &&
	       (event->xgraphicsexpose.count > 0)) {
	trigger = 0;
    }
    display->tail = qevent;
    display->qlen++;
    if (display->qlen > display->qlenmax) {
	display->qlenmax = display->qlen;
    }

#ifdef _WIN32
    if (trigger && ((HANDLE) display->fd != INVALID_HANDLE_VALUE)) {
	SetEvent((HANDLE) display->fd);
    }
#else
#ifdef linux
    if (trigger && (display->fd >= 0) && (display->ext_number < 0)) {
	static const long long buf[1] = { 1 };
	int n = write(display->fd, buf, sizeof (buf));

	if ((n < 0) && (errno != EWOULDBLOCK) && (errno != EAGAIN)) {
	    close(display->fd);
	    display->fd = -1;
	}
    }
#endif
    if (trigger && (display->ext_number >= 0)) {
	int n = write(display->ext_number, "e", 1);

	if ((n < 0) && (errno != EWOULDBLOCK) && (errno != EAGAIN)) {
	    close(display->ext_number);
	    display->ext_number = -1;
	    close(display->fd);
	    display->fd = -1;
	}
    }
#endif

    Tcl_MutexUnlock((Tcl_Mutex *) &display->qlock);
}

int
XFlush(Display *display)
{
    SdlTkLock(display);
    display->request++;
    SdlTkUnlock(display);
    return 0;
}

void
XForceScreenSaver(Display *display, int mode)
{
}

int
XFree(void *data)
{
    if (data != NULL) {
	ckfree(data);
    }
    return 0;
}

void
XFreeColormap(Display *display, Colormap colormap)
{
    if (colormap != None) {
	_Colormap *_colormap = (_Colormap *) colormap;

	if (_colormap->whatever == 1234) {
	    _colormap->whatever = 4321;
	    ckfree((char *) _colormap);
	}
    }
}

void
XFreeColors(Display *display, Colormap colormap, unsigned long *pixels,
	    int npixels, unsigned long planes)
{
}

void
XFreeCursor(Display *display, Cursor cursor)
{
}

/*
 * "The XFreeFont() function deletes the association between the font
 * resource ID and the specified font and frees the XFontStruct structure.
 * The font itself will be freed when no other resource references it.
 * The data and the font should not be referenced again."
 */

int
XFreeFont(Display *display, XFontStruct *font_struct)
{
    SdlTkFontFreeFont(font_struct);
    return 0;
}

int
XFreeFontNames(char **list)
{
    int i = 0;

    if (list == NULL) {
	return 0;
    }
    while (list[i] != NULL) {
	ckfree(list[i++]);
    }
    ckfree((char *) list);
    return 0;
}

void
XFreeGC(Display *display, GC gc)
{
    XGCValues *prev, *curr;

    if (gc != None) {
	if (gc->clip_mask != None) {
	    ckfree((char*) gc->clip_mask);
	}
	prev = NULL;
	curr = display->gcs;
	while (curr != NULL) {
	    if (curr == (XGCValues *) gc) {
		if (prev == NULL) {
		    display->gcs = curr->next;
		} else {
		    prev->next = curr->next;
		}
		break;
	    }
	    prev = curr;
	    curr = curr->next;
	}
	memset(gc, 0xFE, sizeof (XGCValues));
	ckfree((char *) gc);
    }
}

void
XFreeModifiermap(XModifierKeymap *modmap)
{
    ckfree((char *) modmap->modifiermap);
    ckfree((char *) modmap);
}

int
XFreePixmap(Display *display, Pixmap pixmap)
{
    _Pixmap *_p = (_Pixmap *) pixmap;
    _Pixmap *prev, *curr;
    SDL_Surface *sdl;

    if ((_p == NULL) || (_p->type != DT_PIXMAP)) {
	return 0;
    }

    SdlTkLock(display);

    sdl = _p->sdl;
    prev = NULL;
    curr = (_Pixmap *) display->pixmaps;
    while (curr != NULL) {
	if (curr == _p) {
	    if (prev == NULL) {
		display->pixmaps = curr->next;
	    } else {
		prev->next = curr->next;
	    }
	    break;
	}
	prev = curr;
	curr = curr->next;
    }
    memset(_p, 0xFE, sizeof (_Pixmap));
    ckfree((char *) _p);
    SDL_FreeSurface(sdl);

    SdlTkUnlock(display);

    return 0;
}

GContext
XGContextFromGC(GC gc)
{
    return (GContext) NULL;
}

char *
XGetAtomName(Display *display, Atom atom)
{
    char *ret = NULL;

    if ((atom != None) && (atom <= XA_LAST_PREDEFINED)) {
	const char *names[] = {
	    "NO_ATOM",
	    "XA_PRIMARY",
	    "XA_SECONDARY",
	    "XA_ARC",
	    "XA_ATOM",
	    "XA_BITMAP",
	    "XA_CARDINAL",
	    "XA_COLORMAP",
	    "XA_CURSOR",
	    "XA_CUT_BUFFER0",
	    "XA_CUT_BUFFER1",
	    "XA_CUT_BUFFER2",
	    "XA_CUT_BUFFER3",
	    "XA_CUT_BUFFER4",
	    "XA_CUT_BUFFER5",
	    "XA_CUT_BUFFER6",
	    "XA_CUT_BUFFER7",
	    "XA_DRAWABLE",
	    "XA_FONT",
	    "XA_INTEGER",
	    "XA_PIXMAP",
	    "XA_POINT",
	    "XA_RECTANGLE",
	    "XA_RESOURCE_MANAGER",
	    "XA_RGB_COLOR_MAP",
	    "XA_RGB_BEST_MAP",
	    "XA_RGB_BLUE_MAP",
	    "XA_RGB_DEFAULT_MAP",
	    "XA_RGB_GRAY_MAP",
	    "XA_RGB_GREEN_MAP",
	    "XA_RGB_RED_MAP",
	    "XA_STRING",
	    "XA_VISUALID",
	    "XA_WINDOW",
	    "XA_WM_COMMAND",
	    "XA_WM_HINTS",
	    "XA_WM_CLIENT_MACHINE",
	    "XA_WM_ICON_NAME",
	    "XA_WM_ICON_SIZE",
	    "XA_WM_NAME",
	    "XA_WM_NORMAL_HINTS",
	    "XA_WM_SIZE_HINTS",
	    "XA_WM_ZOOM_HINTS",
	    "XA_MIN_SPACE",
	    "XA_NORM_SPACE",
	    "XA_MAX_SPACE",
	    "XA_END_SPACE",
	    "XA_SUPERSCRIPT_X",
	    "XA_SUPERSCRIPT_Y",
	    "XA_SUBSCRIPT_X",
	    "XA_SUBSCRIPT_Y",
	    "XA_UNDERLINE_POSITION",
	    "XA_UNDERLINE_THICKNESS",
	    "XA_STRIKEOUT_ASCENT",
	    "XA_STRIKEOUT_DESCENT",
	    "XA_ITALIC_ANGLE",
	    "XA_X_HEIGHT",
	    "XA_QUAD_WIDTH",
	    "XA_WEIGHT",
	    "XA_POINT_SIZE",
	    "XA_RESOLUTION",
	    "XA_COPYRIGHT",
	    "XA_NOTICE",
	    "XA_FONT_NAME",
	    "XA_FAMILY_NAME",
	    "XA_FULL_NAME",
	    "XA_CAP_HEIGHT",
	    "XA_WM_CLASS",
	    "XA_WM_TRANSIENT_FOR"
	};

	ret = ckalloc(strlen(names[(long) atom]) + 1);
	strcpy(ret, names[(long) atom]);
    } else if (atom != None) {
	int len = strlen((char *) atom) + 1;

	ret = ckalloc(len);
	strcpy(ret, (char *) atom);
    }
    return ret;
}

Bool
XGetFontProperty(XFontStruct *font_struct, Atom atom,
		 unsigned long *value_return)
{
    if (atom == XA_FONT) {
	_Font *_f = (_Font *) font_struct->fid;

	*value_return = (unsigned long) XInternAtom(NULL, _f->xlfd, False);
	return True;
    }
    return False;
}

Status
XGetGeometry(Display *display, Drawable d, Window *root_return,
	     int *x_return, int *y_return, unsigned int *width_return,
	     unsigned int *height_return, unsigned int *border_width_return,
	     unsigned int *depth_return)
{
    _Pixmap *_p = (_Pixmap *) d;
    _Window *_w = (_Window *) d;

    SdlTkLock(display);
    display->request++;

    *root_return = SdlTkX.screen->root;

    if (_p->type == DT_PIXMAP) {
	*x_return = *y_return = 0;
	*width_return = _p->sdl->w;
	*height_return = _p->sdl->h;
	*border_width_return = 0;
	*depth_return = _p->sdl->format->BitsPerPixel;
    }
    if (_w->type == DT_WINDOW) {
	*x_return = _w->atts.x;
	*y_return = _w->atts.y;
	*width_return = _w->atts.width;
	*height_return = _w->atts.height;
	*border_width_return = _w->atts.border_width;
	*depth_return = SdlTkX.screen->root_depth;
    }

    SdlTkUnlock(display);
    return 1;
}

XImage *
XGetImage(Display *display, Drawable d, int x, int y,
	  unsigned int width, unsigned int height, unsigned long plane_mask,
	  int format)
{
    SDL_Surface *sdl;
    char *pixels;
    _Pixmap *_p = (_Pixmap *) d;
    _Pixmap rp;
    int bpp;
    XGCValues fakeGC;

    SdlTkLock(display);

    if (_p->type == DT_PIXMAP) {
	/* Allocate a block of pixels to hold the result. */
	pixels = ckalloc(width * height * _p->sdl->format->BytesPerPixel);

	/* Create an SDL surface that uses the pixels we allocated above */
	sdl = SDL_CreateRGBSurfaceFrom(pixels, width, height,
				       _p->sdl->format->BitsPerPixel,
				       /* pitch */
				       width * _p->sdl->format->BytesPerPixel,
				       _p->sdl->format->Rmask,
				       _p->sdl->format->Gmask,
				       _p->sdl->format->Bmask,
				       _p->sdl->format->Amask);
	bpp = _p->sdl->format->BitsPerPixel;
	if ((bpp == 8) && (sdl != NULL)) {
	    int i;
	    SDL_Palette *pal = SDL_AllocPalette(256);
	    SDL_Color graymap[256];

	    for (i = 0; i < 256; i++) {
		graymap[i].r = graymap[i].b = graymap[i].g = i;
		graymap[i].a = 255;
	    }
	    SDL_SetPaletteColors(pal, graymap, 0, 256);
	    SDL_SetSurfacePalette(sdl, pal);
	    SDL_FreePalette(pal);
	}
    } else {
	/* Allocate a block of pixels to hold the result. */
	pixels = ckalloc(width * height *
			 SdlTkX.sdlsurf->format->BytesPerPixel);

	/* Create an SDL surface that uses the pixels we allocated above */
	sdl = SDL_CreateRGBSurfaceFrom(pixels, width, height,
				       SdlTkX.sdlsurf->format->BitsPerPixel,
				       /* pitch */
				       width *
				       SdlTkX.sdlsurf->format->BytesPerPixel,
				       SdlTkX.sdlsurf->format->Rmask,
				       SdlTkX.sdlsurf->format->Gmask,
				       SdlTkX.sdlsurf->format->Bmask,
				       SdlTkX.sdlsurf->format->Amask);
	bpp = SdlTkX.sdlsurf->format->BitsPerPixel;
    }

    if (sdl == NULL) {
	SdlTkUnlock(display);
	ckfree(pixels);
	return NULL;
    }

    /* Create a pixmap from the SDL surface. */
    rp.type = DT_PIXMAP;
    rp.sdl = sdl;
    rp.format = SdlTkPixelFormat(sdl);

    fakeGC.clip_mask = None;
    fakeGC.graphics_exposures = False;

    /* Copy from the drawable to our pixmap */
    SdlTkGfxCopyArea(d, (Pixmap) &rp, &fakeGC, x, y, width, height, 0, 0);

    /* Free the surface. The pixels are *not* freed */
    SDL_FreeSurface(sdl);

    SdlTkUnlock(display);

    /* Allocate the XImage using the pixels we allocated above */
    return XCreateImage(display, SdlTkX.screen->root_visual,
			bpp, ZPixmap, 0, pixels, width, height, 0, 0);
}

int
XGetInputFocus(Display *display, Window *focus_return, int *revert_to_return)
{
    SdlTkLock(display);
    display->request++;
    *focus_return = SdlTkX.focus_window;
    *revert_to_return = RevertToParent;
    SdlTkUnlock(display);
    return 0;
}

XModifierKeymap	*
XGetModifierMapping(Display *display)
{
    XModifierKeymap *map;

    map = (XModifierKeymap *)ckalloc(sizeof (XModifierKeymap));

    map->max_keypermod = 2;
    map->modifiermap = (KeyCode *) ckalloc(sizeof (KeyCode) * 16);
    memset(map->modifiermap, 0, sizeof (KeyCode) * 16);
    map->modifiermap[ShiftMapIndex * 2 + 0] = SDL_SCANCODE_LSHIFT;
    map->modifiermap[ShiftMapIndex * 2 + 1] = SDL_SCANCODE_RSHIFT;
    map->modifiermap[LockMapIndex * 2 + 0] = SDL_SCANCODE_CAPSLOCK;
    map->modifiermap[ControlMapIndex * 2 + 0] = SDL_SCANCODE_LCTRL;
    map->modifiermap[ControlMapIndex * 2 + 1] = SDL_SCANCODE_RCTRL;
    map->modifiermap[Mod1MapIndex * 2 + 0] = SDL_SCANCODE_LALT;
    map->modifiermap[Mod2MapIndex * 2 + 0] = SDL_SCANCODE_NUMLOCKCLEAR;
    map->modifiermap[Mod3MapIndex * 2 + 0] = SDL_SCANCODE_SCROLLLOCK;
    map->modifiermap[Mod4MapIndex * 2 + 0] = SDL_SCANCODE_RALT;
    return map;
}

int
XGetWindowAttributes(Display *display, Window w,
		     XWindowAttributes *window_attributes_return)
{
    int ret = 0;
    if (window_attributes_return != NULL) {
	_Window *_w = (_Window *) w;

	SdlTkLock(display);
	display->request++;
	if (_w->display != NULL) {
	    *window_attributes_return = _w->atts;
	    window_attributes_return->root = SdlTkX.screen->root;
	    window_attributes_return->screen = display->screens;
	    ret = 1;
	}
	SdlTkUnlock(display);
    }
    return ret;
}

int
XGetWindowProperty(Display *display, Window w, Atom property,
		   long long_offset, long long_length, Bool delete,
		   Atom req_type, Atom *actual_type_return,
		   int *actual_format_return, unsigned long *nitems_return,
		   unsigned long *bytes_after_return,
		   unsigned char **prop_return)
{
    struct prop_key key;
    Tcl_HashEntry *hPtr;

    *actual_type_return = None;
    *actual_format_return = 0;
    *nitems_return = 0;
    *bytes_after_return = 0;
    *prop_return = NULL;
    if (property == SdlTkX.nwms_atom) {
	_Window *_w = (_Window *) w;

	SdlTkLock(display);
	display->request++;
	if (_w->fullscreen) {
	    *prop_return = (unsigned char *) ckalloc(sizeof (Atom));
	    ((Atom *) *prop_return)[0] = SdlTkX.nwmsf_atom;
	    *nitems_return = 1;
	}
	SdlTkUnlock(display);
	return Success;
    }
    if (req_type != XA_STRING) {
	return BadValue;
    }

    SdlTkLock(display);
    display->request++;
    if (((_Window *) w)->display == NULL) {
	SdlTkUnlock(display);
	return BadValue;
    }
    if (!prop_initialized) {
	Tcl_InitHashTable(&prop_table,
			  sizeof (struct prop_key) / sizeof (int));
	prop_initialized = 1;
    }
    memset(&key, 0, sizeof (key));
    key.w = (_Window *) w;
    key.name = property;
    hPtr = Tcl_FindHashEntry(&prop_table, (ClientData) &key);
    if (hPtr != NULL) {
	struct prop_val *val =
	    (struct prop_val *) Tcl_GetHashValue(hPtr);
	int len;
	char *data;

	len = val->length;
	data = val->data;
	long_offset *= 4;
	long_length *= 4;
	if (long_offset < len) {
	    len -= long_offset;
	    data += long_offset;
	    if (len > long_length) {
		*bytes_after_return = len - long_length;
		len = long_length;
	    }
	    if (len > 0) {
		*actual_format_return = 8;
		*actual_type_return = XA_STRING;
		*nitems_return = len;
		*prop_return = (unsigned char *) ckalloc(len + 1);
		memcpy(*prop_return, data, len);
		(*prop_return)[len] = '\0';
	    }
	}
	if (delete && (*bytes_after_return == 0)) {
	    ckfree(Tcl_GetHashValue(hPtr));
	    Tcl_DeleteHashEntry(hPtr);
	    if (!IS_ROOT(w)) {
		XPropertyEvent xproperty;

		memset(&xproperty, 0, sizeof (xproperty));
		xproperty.type = PropertyNotify;
		xproperty.serial = ((_Window *) w)->display->request;
		xproperty.send_event = False;
		xproperty.atom = property;
		xproperty.display = ((_Window *) w)->display;
		xproperty.window = w;
		xproperty.state = PropertyDelete;
		xproperty.time = SdlTkX.time_count;
		SdlTkQueueEvent((XEvent *) &xproperty);
	    }
	}
    }
    SdlTkUnlock(display);
    return Success;
}

XVisualInfo *
XGetVisualInfo(Display *display, long vinfo_mask, XVisualInfo *vinfo_template,
	       int *nitems_return)
{
    XVisualInfo *info = (XVisualInfo *)ckalloc(sizeof (XVisualInfo));

    memset(info, 0, sizeof (XVisualInfo));
    info->visual = DefaultVisual(display, 0);
    info->visualid = info->visual->visualid;
    info->screen = 0;
    info->depth = info->visual->bits_per_rgb;
    info->class = info->visual->class;
    info->colormap_size = info->visual->map_entries;
    info->bits_per_rgb = info->visual->bits_per_rgb;
    info->red_mask = info->visual->red_mask;
    info->green_mask = info->visual->green_mask;
    info->blue_mask = info->visual->blue_mask;

    if (((vinfo_mask & VisualIDMask)
	    && (vinfo_template->visualid != info->visualid))
	    || ((vinfo_mask & VisualScreenMask)
		    && (vinfo_template->screen != info->screen))
	    || ((vinfo_mask & VisualDepthMask)
		    && (vinfo_template->depth != info->depth))
	    || ((vinfo_mask & VisualClassMask)
		    && (vinfo_template->class != info->class))
	    || ((vinfo_mask & VisualColormapSizeMask)
		    && (vinfo_template->colormap_size != info->colormap_size))
	    || ((vinfo_mask & VisualBitsPerRGBMask)
		    && (vinfo_template->bits_per_rgb != info->bits_per_rgb))
	    || ((vinfo_mask & VisualRedMaskMask)
		    && (vinfo_template->red_mask != info->red_mask))
	    || ((vinfo_mask & VisualGreenMaskMask)
		    && (vinfo_template->green_mask != info->green_mask))
	    || ((vinfo_mask & VisualBlueMaskMask)
		    && (vinfo_template->blue_mask != info->blue_mask))
	) {
	ckfree((char *) info);
	return NULL;
    }

    *nitems_return = 1;
    return info;
}

Status
XGetWMColormapWindows(Display *display, Window w, Window **windows_return,
		      int *count_return)
{
    return (Status) 0;
}

int
XGrabKeyboard(Display *display, Window grab_window, Bool owner_events,
	      int pointer_mode, int keyboard_mode, Time time)
{
    _Window *_w = (_Window *) grab_window;
    int ret = GrabSuccess;

    SdlTkLock(display);
    if (_w->display == NULL) {
	ret = GrabNotViewable;
	goto done;
    }
    if (SdlTkX.keyboard_window != NULL) {
	if (SdlTkX.keyboard_window->display != display) {
	    ret = AlreadyGrabbed;
	    goto done;
	}
    }
    if (SdlTkX.keyboard_window != _w) {
	SdlTkX.keyboard_window = _w;
	if (SdlTkX.focus_window != _w->display->focus_window) {
	    SdlTkSetInputFocus(display, _w->display->focus_window,
			       RevertToParent, CurrentTime);
	}
    }
done:
    SdlTkUnlock(display);
    return ret;
}

#if 0
int
XGrabPointer(Display *display, Window w1, Bool b, unsigned int ui,
	     int i1, int i2, Window w2, Cursor c, Time t)
{
    return 0;
}
#endif

int
XGrabServer(Display *display)
{
    SdlTkLock(display);
    display->request++;
    xlib_grab = display;
    SdlTkUnlock(display);
    return 0;
}

int
XIconifyWindow(Display *display, Window w, int screen_number)
{
    return 0;
}

Atom
XInternAtom(Display *display, _Xconst char *atom_name, Bool only_if_exists)
{
    Tcl_HashEntry *hPtr;
    Atom ret = None;

    Tcl_MutexLock(&atom_mutex);
    if (!atom_initialized) {
	Tcl_InitHashTable(&atom_table, TCL_STRING_KEYS);
	atom_initialized = 1;
    }
    if (only_if_exists) {
	hPtr = Tcl_FindHashEntry(&atom_table, (char *) atom_name);
    } else {
	int new;

	hPtr = Tcl_CreateHashEntry(&atom_table, (char *) atom_name, &new);
	if (new) {
	    Tcl_SetHashValue(hPtr, Tcl_GetHashKey(&atom_table, hPtr));
	}
    }
    if (hPtr != NULL) {
	ret = (Atom) Tcl_GetHashValue(hPtr);
    }
    Tcl_MutexUnlock(&atom_mutex);
    return ret;
}

/* keycode is SDL_SCANCODE_xxx */
static int keymap_initialized = 0;
static KeySym keymap[SDL_NUM_SCANCODES];

static void
keymap_init(void)
{
    int i;

    if (!keymap_initialized) {
	for (i = 0; i < SDL_NUM_SCANCODES; i++) {
	    keymap[i] = NoSymbol;
	}

	for (i = 0; i < 26; i++) {
	    keymap[SDL_SCANCODE_A + i] = XK_a + i;
	}

	keymap[SDL_SCANCODE_SPACE] = XK_space;
	keymap[SDL_SCANCODE_KP_EXCLAM] = XK_exclam;
	keymap[SDL_SCANCODE_KP_HASH] = XK_numbersign;
	keymap[SDL_SCANCODE_KP_PERCENT] = XK_percent;
	keymap[SDL_SCANCODE_KP_AMPERSAND] = XK_ampersand;
	keymap[SDL_SCANCODE_KP_LEFTPAREN] = XK_parenleft;
	keymap[SDL_SCANCODE_KP_RIGHTPAREN] = XK_parenright;
	keymap[SDL_SCANCODE_KP_PLUS] = XK_plus;
	keymap[SDL_SCANCODE_COMMA] = XK_comma;
	keymap[SDL_SCANCODE_MINUS] = XK_minus;
	keymap[SDL_SCANCODE_PERIOD] = XK_period;
	keymap[SDL_SCANCODE_SLASH] = XK_slash;
	keymap[SDL_SCANCODE_GRAVE] = XK_grave;
	keymap[SDL_SCANCODE_APOSTROPHE] = XK_acute;
	keymap[SDL_SCANCODE_SEMICOLON] = XK_semicolon;
	keymap[SDL_SCANCODE_BACKSLASH] = XK_backslash;
	keymap[SDL_SCANCODE_LEFTBRACKET] = XK_bracketleft;
	keymap[SDL_SCANCODE_RIGHTBRACKET] = XK_bracketright;

	keymap[SDL_SCANCODE_0] = XK_0;
	keymap[SDL_SCANCODE_1] = XK_1;
	keymap[SDL_SCANCODE_2] = XK_2;
	keymap[SDL_SCANCODE_3] = XK_3;
	keymap[SDL_SCANCODE_4] = XK_4;
	keymap[SDL_SCANCODE_5] = XK_5;
	keymap[SDL_SCANCODE_6] = XK_6;
	keymap[SDL_SCANCODE_7] = XK_7;
	keymap[SDL_SCANCODE_8] = XK_8;
	keymap[SDL_SCANCODE_9] = XK_9;

	keymap[SDL_SCANCODE_KP_COLON] = XK_colon;
	keymap[SDL_SCANCODE_KP_LESS] = XK_less;
	keymap[SDL_SCANCODE_EQUALS] = XK_equal;
	keymap[SDL_SCANCODE_KP_GREATER] = XK_greater;
	keymap[SDL_SCANCODE_KP_AT] = XK_at;

	keymap[SDL_SCANCODE_KP_0] = XK_KP_0;
	keymap[SDL_SCANCODE_KP_1] = XK_KP_1;
	keymap[SDL_SCANCODE_KP_2] = XK_KP_2;
	keymap[SDL_SCANCODE_KP_3] = XK_KP_3;
	keymap[SDL_SCANCODE_KP_4] = XK_KP_4;
	keymap[SDL_SCANCODE_KP_5] = XK_KP_5;
	keymap[SDL_SCANCODE_KP_6] = XK_KP_6;
	keymap[SDL_SCANCODE_KP_7] = XK_KP_7;
	keymap[SDL_SCANCODE_KP_8] = XK_KP_8;
	keymap[SDL_SCANCODE_KP_9] = XK_KP_9;

	keymap[SDL_SCANCODE_KP_PERIOD] = XK_KP_Decimal;
	keymap[SDL_SCANCODE_KP_DIVIDE] = XK_KP_Divide;
	keymap[SDL_SCANCODE_KP_MULTIPLY] = XK_KP_Multiply;
	keymap[SDL_SCANCODE_KP_MINUS] = XK_KP_Subtract;
	keymap[SDL_SCANCODE_KP_PLUS] = XK_KP_Add;
	keymap[SDL_SCANCODE_KP_ENTER] = XK_KP_Enter;
	keymap[SDL_SCANCODE_KP_EQUALS] = XK_KP_Equal;

	keymap[SDL_SCANCODE_LGUI] = XK_Win_L;
	keymap[SDL_SCANCODE_RGUI] = XK_Win_R;
	keymap[SDL_SCANCODE_MENU] = XK_App;

	keymap[SDL_SCANCODE_BACKSPACE] = XK_BackSpace;
	keymap[SDL_SCANCODE_DELETE] = XK_Delete;
	keymap[SDL_SCANCODE_TAB] = XK_Tab;
	keymap[SDL_SCANCODE_RETURN] = XK_Return;
	keymap[SDL_SCANCODE_LALT] = XK_Alt_L;
	keymap[SDL_SCANCODE_LCTRL] = XK_Control_L;
	keymap[SDL_SCANCODE_LSHIFT] = XK_Shift_L;
	keymap[SDL_SCANCODE_RALT] = XK_Mode_switch;
	keymap[SDL_SCANCODE_RCTRL] = XK_Control_R;
	keymap[SDL_SCANCODE_RSHIFT] = XK_Shift_R;
	keymap[SDL_SCANCODE_PAUSE] = XK_Pause;
	keymap[SDL_SCANCODE_ESCAPE] = XK_Escape;
	keymap[SDL_SCANCODE_PAGEUP] = XK_Prior;
	keymap[SDL_SCANCODE_PAGEDOWN] = XK_Next;
	keymap[SDL_SCANCODE_END] = XK_End;
	keymap[SDL_SCANCODE_HOME] = XK_Home;
	keymap[SDL_SCANCODE_LEFT] = XK_Left;
	keymap[SDL_SCANCODE_RIGHT] = XK_Right;
	keymap[SDL_SCANCODE_UP] = XK_Up;
	keymap[SDL_SCANCODE_DOWN] = XK_Down;
	keymap[SDL_SCANCODE_INSERT] = XK_Insert;

	keymap[SDL_SCANCODE_AC_BACK] = XK_Break;
	keymap[SDL_SCANCODE_AC_FORWARD] = XK_Cancel;
	keymap[SDL_SCANCODE_AC_HOME] = XK_Execute;
	keymap[SDL_SCANCODE_AC_SEARCH] = XK_Find;
	keymap[SDL_SCANCODE_AC_BOOKMARKS] = XK_Help;

	for (i = 0; i < 12; i++) {
	    keymap[SDL_SCANCODE_F1 + i] = XK_F1 + i;
	}

	keymap[SDL_SCANCODE_CAPSLOCK] = XK_Caps_Lock;
	keymap[SDL_SCANCODE_NUMLOCKCLEAR] = XK_Num_Lock;
	keymap[SDL_SCANCODE_SCROLLLOCK] = XK_Scroll_Lock;

	keymap_initialized = 1;
    }
}

KeySym
XKeycodeToKeysym(Display *display, unsigned int keycode, int index)
{
    keymap_init();
    if (keycode >= SDL_NUM_SCANCODES) {
	return NoSymbol;
    }
    return keymap[keycode];
}

KeyCode
XKeysymToKeycode(Display *display, KeySym keysym)
{
    int i;

    keymap_init();
    for (i = 0; i < SDL_NUM_SCANCODES; i++) {
	if (keymap[i] == keysym) {
	    return i;
	}
    }
    return 0;
}

char *
XKeysymToString(KeySym keysym)
{
    return NULL;
}

char **
XListFonts(Display *display, _Xconst char *pattern, int maxnames,
	   int *actual_count_return)
{
    return SdlTkListFonts(pattern, actual_count_return);
}

XHostAddress *
XListHosts(Display *display, int *nhosts_return, Bool *state_return)
{
    return NULL;
}

Font
XLoadFont(Display *display, _Xconst char *name)
{
    return SdlTkFontLoadXLFD(name);
}

XFontStruct *
XLoadQueryFont(Display *display, _Xconst char *name)
{
    Font f = SdlTkFontLoadXLFD(name);
    return f ? ((_Font *) f)->fontStruct : NULL;
}

int
XLookupColor(Display *display, Colormap colormap, _Xconst char *color_name,
	     XColor *exact_def_return, XColor *screen_def_return)
{
    return 0;
}

/* KeySym to UNICODE mapping table from X11R6.4 xterm */

struct codepair {
    unsigned short keysym;
    unsigned short ucs;
};

static struct codepair keysymtab[] = {
    { 0x01a1, 0x0104 },
    { 0x01a2, 0x02d8 },
    { 0x01a3, 0x0141 },
    { 0x01a5, 0x013d },
    { 0x01a6, 0x015a },
    { 0x01a9, 0x0160 },
    { 0x01aa, 0x015e },
    { 0x01ab, 0x0164 },
    { 0x01ac, 0x0179 },
    { 0x01ae, 0x017d },
    { 0x01af, 0x017b },
    { 0x01b1, 0x0105 },
    { 0x01b2, 0x02db },
    { 0x01b3, 0x0142 },
    { 0x01b5, 0x013e },
    { 0x01b6, 0x015b },
    { 0x01b7, 0x02c7 },
    { 0x01b9, 0x0161 },
    { 0x01ba, 0x015f },
    { 0x01bb, 0x0165 },
    { 0x01bc, 0x017a },
    { 0x01bd, 0x02dd },
    { 0x01be, 0x017e },
    { 0x01bf, 0x017c },
    { 0x01c0, 0x0154 },
    { 0x01c3, 0x0102 },
    { 0x01c5, 0x0139 },
    { 0x01c6, 0x0106 },
    { 0x01c8, 0x010c },
    { 0x01ca, 0x0118 },
    { 0x01cc, 0x011a },
    { 0x01cf, 0x010e },
    { 0x01d0, 0x0110 },
    { 0x01d1, 0x0143 },
    { 0x01d2, 0x0147 },
    { 0x01d5, 0x0150 },
    { 0x01d8, 0x0158 },
    { 0x01d9, 0x016e },
    { 0x01db, 0x0170 },
    { 0x01de, 0x0162 },
    { 0x01e0, 0x0155 },
    { 0x01e3, 0x0103 },
    { 0x01e5, 0x013a },
    { 0x01e6, 0x0107 },
    { 0x01e8, 0x010d },
    { 0x01ea, 0x0119 },
    { 0x01ec, 0x011b },
    { 0x01ef, 0x010f },
    { 0x01f0, 0x0111 },
    { 0x01f1, 0x0144 },
    { 0x01f2, 0x0148 },
    { 0x01f5, 0x0151 },
    { 0x01f8, 0x0159 },
    { 0x01f9, 0x016f },
    { 0x01fb, 0x0171 },
    { 0x01fe, 0x0163 },
    { 0x01ff, 0x02d9 },
    { 0x02a1, 0x0126 },
    { 0x02a6, 0x0124 },
    { 0x02a9, 0x0130 },
    { 0x02ab, 0x011e },
    { 0x02ac, 0x0134 },
    { 0x02b1, 0x0127 },
    { 0x02b6, 0x0125 },
    { 0x02b9, 0x0131 },
    { 0x02bb, 0x011f },
    { 0x02bc, 0x0135 },
    { 0x02c5, 0x010a },
    { 0x02c6, 0x0108 },
    { 0x02d5, 0x0120 },
    { 0x02d8, 0x011c },
    { 0x02dd, 0x016c },
    { 0x02de, 0x015c },
    { 0x02e5, 0x010b },
    { 0x02e6, 0x0109 },
    { 0x02f5, 0x0121 },
    { 0x02f8, 0x011d },
    { 0x02fd, 0x016d },
    { 0x02fe, 0x015d },
    { 0x03a2, 0x0138 },
    { 0x03a3, 0x0156 },
    { 0x03a5, 0x0128 },
    { 0x03a6, 0x013b },
    { 0x03aa, 0x0112 },
    { 0x03ab, 0x0122 },
    { 0x03ac, 0x0166 },
    { 0x03b3, 0x0157 },
    { 0x03b5, 0x0129 },
    { 0x03b6, 0x013c },
    { 0x03ba, 0x0113 },
    { 0x03bb, 0x0123 },
    { 0x03bc, 0x0167 },
    { 0x03bd, 0x014a },
    { 0x03bf, 0x014b },
    { 0x03c0, 0x0100 },
    { 0x03c7, 0x012e },
    { 0x03cc, 0x0116 },
    { 0x03cf, 0x012a },
    { 0x03d1, 0x0145 },
    { 0x03d2, 0x014c },
    { 0x03d3, 0x0136 },
    { 0x03d9, 0x0172 },
    { 0x03dd, 0x0168 },
    { 0x03de, 0x016a },
    { 0x03e0, 0x0101 },
    { 0x03e7, 0x012f },
    { 0x03ec, 0x0117 },
    { 0x03ef, 0x012b },
    { 0x03f1, 0x0146 },
    { 0x03f2, 0x014d },
    { 0x03f3, 0x0137 },
    { 0x03f9, 0x0173 },
    { 0x03fd, 0x0169 },
    { 0x03fe, 0x016b },
    { 0x047e, 0x203e },
    { 0x04a1, 0x3002 },
    { 0x04a2, 0x300c },
    { 0x04a3, 0x300d },
    { 0x04a4, 0x3001 },
    { 0x04a5, 0x30fb },
    { 0x04a6, 0x30f2 },
    { 0x04a7, 0x30a1 },
    { 0x04a8, 0x30a3 },
    { 0x04a9, 0x30a5 },
    { 0x04aa, 0x30a7 },
    { 0x04ab, 0x30a9 },
    { 0x04ac, 0x30e3 },
    { 0x04ad, 0x30e5 },
    { 0x04ae, 0x30e7 },
    { 0x04af, 0x30c3 },
    { 0x04b0, 0x30fc },
    { 0x04b1, 0x30a2 },
    { 0x04b2, 0x30a4 },
    { 0x04b3, 0x30a6 },
    { 0x04b4, 0x30a8 },
    { 0x04b5, 0x30aa },
    { 0x04b6, 0x30ab },
    { 0x04b7, 0x30ad },
    { 0x04b8, 0x30af },
    { 0x04b9, 0x30b1 },
    { 0x04ba, 0x30b3 },
    { 0x04bb, 0x30b5 },
    { 0x04bc, 0x30b7 },
    { 0x04bd, 0x30b9 },
    { 0x04be, 0x30bb },
    { 0x04bf, 0x30bd },
    { 0x04c0, 0x30bf },
    { 0x04c1, 0x30c1 },
    { 0x04c2, 0x30c4 },
    { 0x04c3, 0x30c6 },
    { 0x04c4, 0x30c8 },
    { 0x04c5, 0x30ca },
    { 0x04c6, 0x30cb },
    { 0x04c7, 0x30cc },
    { 0x04c8, 0x30cd },
    { 0x04c9, 0x30ce },
    { 0x04ca, 0x30cf },
    { 0x04cb, 0x30d2 },
    { 0x04cc, 0x30d5 },
    { 0x04cd, 0x30d8 },
    { 0x04ce, 0x30db },
    { 0x04cf, 0x30de },
    { 0x04d0, 0x30df },
    { 0x04d1, 0x30e0 },
    { 0x04d2, 0x30e1 },
    { 0x04d3, 0x30e2 },
    { 0x04d4, 0x30e4 },
    { 0x04d5, 0x30e6 },
    { 0x04d6, 0x30e8 },
    { 0x04d7, 0x30e9 },
    { 0x04d8, 0x30ea },
    { 0x04d9, 0x30eb },
    { 0x04da, 0x30ec },
    { 0x04db, 0x30ed },
    { 0x04dc, 0x30ef },
    { 0x04dd, 0x30f3 },
    { 0x04de, 0x309b },
    { 0x04df, 0x309c },
    { 0x05ac, 0x060c },
    { 0x05bb, 0x061b },
    { 0x05bf, 0x061f },
    { 0x05c1, 0x0621 },
    { 0x05c2, 0x0622 },
    { 0x05c3, 0x0623 },
    { 0x05c4, 0x0624 },
    { 0x05c5, 0x0625 },
    { 0x05c6, 0x0626 },
    { 0x05c7, 0x0627 },
    { 0x05c8, 0x0628 },
    { 0x05c9, 0x0629 },
    { 0x05ca, 0x062a },
    { 0x05cb, 0x062b },
    { 0x05cc, 0x062c },
    { 0x05cd, 0x062d },
    { 0x05ce, 0x062e },
    { 0x05cf, 0x062f },
    { 0x05d0, 0x0630 },
    { 0x05d1, 0x0631 },
    { 0x05d2, 0x0632 },
    { 0x05d3, 0x0633 },
    { 0x05d4, 0x0634 },
    { 0x05d5, 0x0635 },
    { 0x05d6, 0x0636 },
    { 0x05d7, 0x0637 },
    { 0x05d8, 0x0638 },
    { 0x05d9, 0x0639 },
    { 0x05da, 0x063a },
    { 0x05e0, 0x0640 },
    { 0x05e1, 0x0641 },
    { 0x05e2, 0x0642 },
    { 0x05e3, 0x0643 },
    { 0x05e4, 0x0644 },
    { 0x05e5, 0x0645 },
    { 0x05e6, 0x0646 },
    { 0x05e7, 0x0647 },
    { 0x05e8, 0x0648 },
    { 0x05e9, 0x0649 },
    { 0x05ea, 0x064a },
    { 0x05eb, 0x064b },
    { 0x05ec, 0x064c },
    { 0x05ed, 0x064d },
    { 0x05ee, 0x064e },
    { 0x05ef, 0x064f },
    { 0x05f0, 0x0650 },
    { 0x05f1, 0x0651 },
    { 0x05f2, 0x0652 },
    { 0x06a1, 0x0452 },
    { 0x06a2, 0x0453 },
    { 0x06a3, 0x0451 },
    { 0x06a4, 0x0454 },
    { 0x06a5, 0x0455 },
    { 0x06a6, 0x0456 },
    { 0x06a7, 0x0457 },
    { 0x06a8, 0x0458 },
    { 0x06a9, 0x0459 },
    { 0x06aa, 0x045a },
    { 0x06ab, 0x045b },
    { 0x06ac, 0x045c },
    { 0x06ae, 0x045e },
    { 0x06af, 0x045f },
    { 0x06b0, 0x2116 },
    { 0x06b1, 0x0402 },
    { 0x06b2, 0x0403 },
    { 0x06b3, 0x0401 },
    { 0x06b4, 0x0404 },
    { 0x06b5, 0x0405 },
    { 0x06b6, 0x0406 },
    { 0x06b7, 0x0407 },
    { 0x06b8, 0x0408 },
    { 0x06b9, 0x0409 },
    { 0x06ba, 0x040a },
    { 0x06bb, 0x040b },
    { 0x06bc, 0x040c },
    { 0x06be, 0x040e },
    { 0x06bf, 0x040f },
    { 0x06c0, 0x044e },
    { 0x06c1, 0x0430 },
    { 0x06c2, 0x0431 },
    { 0x06c3, 0x0446 },
    { 0x06c4, 0x0434 },
    { 0x06c5, 0x0435 },
    { 0x06c6, 0x0444 },
    { 0x06c7, 0x0433 },
    { 0x06c8, 0x0445 },
    { 0x06c9, 0x0438 },
    { 0x06ca, 0x0439 },
    { 0x06cb, 0x043a },
    { 0x06cc, 0x043b },
    { 0x06cd, 0x043c },
    { 0x06ce, 0x043d },
    { 0x06cf, 0x043e },
    { 0x06d0, 0x043f },
    { 0x06d1, 0x044f },
    { 0x06d2, 0x0440 },
    { 0x06d3, 0x0441 },
    { 0x06d4, 0x0442 },
    { 0x06d5, 0x0443 },
    { 0x06d6, 0x0436 },
    { 0x06d7, 0x0432 },
    { 0x06d8, 0x044c },
    { 0x06d9, 0x044b },
    { 0x06da, 0x0437 },
    { 0x06db, 0x0448 },
    { 0x06dc, 0x044d },
    { 0x06dd, 0x0449 },
    { 0x06de, 0x0447 },
    { 0x06df, 0x044a },
    { 0x06e0, 0x042e },
    { 0x06e1, 0x0410 },
    { 0x06e2, 0x0411 },
    { 0x06e3, 0x0426 },
    { 0x06e4, 0x0414 },
    { 0x06e5, 0x0415 },
    { 0x06e6, 0x0424 },
    { 0x06e7, 0x0413 },
    { 0x06e8, 0x0425 },
    { 0x06e9, 0x0418 },
    { 0x06ea, 0x0419 },
    { 0x06eb, 0x041a },
    { 0x06ec, 0x041b },
    { 0x06ed, 0x041c },
    { 0x06ee, 0x041d },
    { 0x06ef, 0x041e },
    { 0x06f0, 0x041f },
    { 0x06f1, 0x042f },
    { 0x06f2, 0x0420 },
    { 0x06f3, 0x0421 },
    { 0x06f4, 0x0422 },
    { 0x06f5, 0x0423 },
    { 0x06f6, 0x0416 },
    { 0x06f7, 0x0412 },
    { 0x06f8, 0x042c },
    { 0x06f9, 0x042b },
    { 0x06fa, 0x0417 },
    { 0x06fb, 0x0428 },
    { 0x06fc, 0x042d },
    { 0x06fd, 0x0429 },
    { 0x06fe, 0x0427 },
    { 0x06ff, 0x042a },
    { 0x07a1, 0x0386 },
    { 0x07a2, 0x0388 },
    { 0x07a3, 0x0389 },
    { 0x07a4, 0x038a },
    { 0x07a5, 0x03aa },
    { 0x07a7, 0x038c },
    { 0x07a8, 0x038e },
    { 0x07a9, 0x03ab },
    { 0x07ab, 0x038f },
    { 0x07ae, 0x0385 },
    { 0x07af, 0x2015 },
    { 0x07b1, 0x03ac },
    { 0x07b2, 0x03ad },
    { 0x07b3, 0x03ae },
    { 0x07b4, 0x03af },
    { 0x07b5, 0x03ca },
    { 0x07b6, 0x0390 },
    { 0x07b7, 0x03cc },
    { 0x07b8, 0x03cd },
    { 0x07b9, 0x03cb },
    { 0x07ba, 0x03b0 },
    { 0x07bb, 0x03ce },
    { 0x07c1, 0x0391 },
    { 0x07c2, 0x0392 },
    { 0x07c3, 0x0393 },
    { 0x07c4, 0x0394 },
    { 0x07c5, 0x0395 },
    { 0x07c6, 0x0396 },
    { 0x07c7, 0x0397 },
    { 0x07c8, 0x0398 },
    { 0x07c9, 0x0399 },
    { 0x07ca, 0x039a },
    { 0x07cb, 0x039b },
    { 0x07cc, 0x039c },
    { 0x07cd, 0x039d },
    { 0x07ce, 0x039e },
    { 0x07cf, 0x039f },
    { 0x07d0, 0x03a0 },
    { 0x07d1, 0x03a1 },
    { 0x07d2, 0x03a3 },
    { 0x07d4, 0x03a4 },
    { 0x07d5, 0x03a5 },
    { 0x07d6, 0x03a6 },
    { 0x07d7, 0x03a7 },
    { 0x07d8, 0x03a8 },
    { 0x07d9, 0x03a9 },
    { 0x07e1, 0x03b1 },
    { 0x07e2, 0x03b2 },
    { 0x07e3, 0x03b3 },
    { 0x07e4, 0x03b4 },
    { 0x07e5, 0x03b5 },
    { 0x07e6, 0x03b6 },
    { 0x07e7, 0x03b7 },
    { 0x07e8, 0x03b8 },
    { 0x07e9, 0x03b9 },
    { 0x07ea, 0x03ba },
    { 0x07eb, 0x03bb },
    { 0x07ec, 0x03bc },
    { 0x07ed, 0x03bd },
    { 0x07ee, 0x03be },
    { 0x07ef, 0x03bf },
    { 0x07f0, 0x03c0 },
    { 0x07f1, 0x03c1 },
    { 0x07f2, 0x03c3 },
    { 0x07f3, 0x03c2 },
    { 0x07f4, 0x03c4 },
    { 0x07f5, 0x03c5 },
    { 0x07f6, 0x03c6 },
    { 0x07f7, 0x03c7 },
    { 0x07f8, 0x03c8 },
    { 0x07f9, 0x03c9 },
    { 0x08a1, 0x23b7 },
    { 0x08a2, 0x250c },
    { 0x08a3, 0x2500 },
    { 0x08a4, 0x2320 },
    { 0x08a5, 0x2321 },
    { 0x08a6, 0x2502 },
    { 0x08a7, 0x23a1 },
    { 0x08a8, 0x23a3 },
    { 0x08a9, 0x23a4 },
    { 0x08aa, 0x23a6 },
    { 0x08ab, 0x239b },
    { 0x08ac, 0x239d },
    { 0x08ad, 0x239e },
    { 0x08ae, 0x23a0 },
    { 0x08af, 0x23a8 },
    { 0x08b0, 0x23ac },
    { 0x08bc, 0x2264 },
    { 0x08bd, 0x2260 },
    { 0x08be, 0x2265 },
    { 0x08bf, 0x222b },
    { 0x08c0, 0x2234 },
    { 0x08c1, 0x221d },
    { 0x08c2, 0x221e },
    { 0x08c5, 0x2207 },
    { 0x08c8, 0x223c },
    { 0x08c9, 0x2243 },
    { 0x08cd, 0x21d4 },
    { 0x08ce, 0x21d2 },
    { 0x08cf, 0x2261 },
    { 0x08d6, 0x221a },
    { 0x08da, 0x2282 },
    { 0x08db, 0x2283 },
    { 0x08dc, 0x2229 },
    { 0x08dd, 0x222a },
    { 0x08de, 0x2227 },
    { 0x08df, 0x2228 },
    { 0x08ef, 0x2202 },
    { 0x08f6, 0x0192 },
    { 0x08fb, 0x2190 },
    { 0x08fc, 0x2191 },
    { 0x08fd, 0x2192 },
    { 0x08fe, 0x2193 },
    { 0x09e0, 0x25c6 },
    { 0x09e1, 0x2592 },
    { 0x09e2, 0x2409 },
    { 0x09e3, 0x240c },
    { 0x09e4, 0x240d },
    { 0x09e5, 0x240a },
    { 0x09e8, 0x2424 },
    { 0x09e9, 0x240b },
    { 0x09ea, 0x2518 },
    { 0x09eb, 0x2510 },
    { 0x09ec, 0x250c },
    { 0x09ed, 0x2514 },
    { 0x09ee, 0x253c },
    { 0x09ef, 0x23ba },
    { 0x09f0, 0x23bb },
    { 0x09f1, 0x2500 },
    { 0x09f2, 0x23bc },
    { 0x09f3, 0x23bd },
    { 0x09f4, 0x251c },
    { 0x09f5, 0x2524 },
    { 0x09f6, 0x2534 },
    { 0x09f7, 0x252c },
    { 0x09f8, 0x2502 },
    { 0x0aa1, 0x2003 },
    { 0x0aa2, 0x2002 },
    { 0x0aa3, 0x2004 },
    { 0x0aa4, 0x2005 },
    { 0x0aa5, 0x2007 },
    { 0x0aa6, 0x2008 },
    { 0x0aa7, 0x2009 },
    { 0x0aa8, 0x200a },
    { 0x0aa9, 0x2014 },
    { 0x0aaa, 0x2013 },
    { 0x0aae, 0x2026 },
    { 0x0aaf, 0x2025 },
    { 0x0ab0, 0x2153 },
    { 0x0ab1, 0x2154 },
    { 0x0ab2, 0x2155 },
    { 0x0ab3, 0x2156 },
    { 0x0ab4, 0x2157 },
    { 0x0ab5, 0x2158 },
    { 0x0ab6, 0x2159 },
    { 0x0ab7, 0x215a },
    { 0x0ab8, 0x2105 },
    { 0x0abb, 0x2012 },
    { 0x0abc, 0x2329 },
    { 0x0abe, 0x232a },
    { 0x0ac3, 0x215b },
    { 0x0ac4, 0x215c },
    { 0x0ac5, 0x215d },
    { 0x0ac6, 0x215e },
    { 0x0ac9, 0x2122 },
    { 0x0aca, 0x2613 },
    { 0x0acc, 0x25c1 },
    { 0x0acd, 0x25b7 },
    { 0x0ace, 0x25cb },
    { 0x0acf, 0x25af },
    { 0x0ad0, 0x2018 },
    { 0x0ad1, 0x2019 },
    { 0x0ad2, 0x201c },
    { 0x0ad3, 0x201d },
    { 0x0ad4, 0x211e },
    { 0x0ad6, 0x2032 },
    { 0x0ad7, 0x2033 },
    { 0x0ad9, 0x271d },
    { 0x0adb, 0x25ac },
    { 0x0adc, 0x25c0 },
    { 0x0add, 0x25b6 },
    { 0x0ade, 0x25cf },
    { 0x0adf, 0x25ae },
    { 0x0ae0, 0x25e6 },
    { 0x0ae1, 0x25ab },
    { 0x0ae2, 0x25ad },
    { 0x0ae3, 0x25b3 },
    { 0x0ae4, 0x25bd },
    { 0x0ae5, 0x2606 },
    { 0x0ae6, 0x2022 },
    { 0x0ae7, 0x25aa },
    { 0x0ae8, 0x25b2 },
    { 0x0ae9, 0x25bc },
    { 0x0aea, 0x261c },
    { 0x0aeb, 0x261e },
    { 0x0aec, 0x2663 },
    { 0x0aed, 0x2666 },
    { 0x0aee, 0x2665 },
    { 0x0af0, 0x2720 },
    { 0x0af1, 0x2020 },
    { 0x0af2, 0x2021 },
    { 0x0af3, 0x2713 },
    { 0x0af4, 0x2717 },
    { 0x0af5, 0x266f },
    { 0x0af6, 0x266d },
    { 0x0af7, 0x2642 },
    { 0x0af8, 0x2640 },
    { 0x0af9, 0x260e },
    { 0x0afa, 0x2315 },
    { 0x0afb, 0x2117 },
    { 0x0afc, 0x2038 },
    { 0x0afd, 0x201a },
    { 0x0afe, 0x201e },
    { 0x0ba3, 0x003c },
    { 0x0ba6, 0x003e },
    { 0x0ba8, 0x2228 },
    { 0x0ba9, 0x2227 },
    { 0x0bc0, 0x00af },
    { 0x0bc2, 0x22a5 },
    { 0x0bc3, 0x2229 },
    { 0x0bc4, 0x230a },
    { 0x0bc6, 0x005f },
    { 0x0bca, 0x2218 },
    { 0x0bcc, 0x2395 },
    { 0x0bce, 0x22a4 },
    { 0x0bcf, 0x25cb },
    { 0x0bd3, 0x2308 },
    { 0x0bd6, 0x222a },
    { 0x0bd8, 0x2283 },
    { 0x0bda, 0x2282 },
    { 0x0bdc, 0x22a2 },
    { 0x0bfc, 0x22a3 },
    { 0x0cdf, 0x2017 },
    { 0x0ce0, 0x05d0 },
    { 0x0ce1, 0x05d1 },
    { 0x0ce2, 0x05d2 },
    { 0x0ce3, 0x05d3 },
    { 0x0ce4, 0x05d4 },
    { 0x0ce5, 0x05d5 },
    { 0x0ce6, 0x05d6 },
    { 0x0ce7, 0x05d7 },
    { 0x0ce8, 0x05d8 },
    { 0x0ce9, 0x05d9 },
    { 0x0cea, 0x05da },
    { 0x0ceb, 0x05db },
    { 0x0cec, 0x05dc },
    { 0x0ced, 0x05dd },
    { 0x0cee, 0x05de },
    { 0x0cef, 0x05df },
    { 0x0cf0, 0x05e0 },
    { 0x0cf1, 0x05e1 },
    { 0x0cf2, 0x05e2 },
    { 0x0cf3, 0x05e3 },
    { 0x0cf4, 0x05e4 },
    { 0x0cf5, 0x05e5 },
    { 0x0cf6, 0x05e6 },
    { 0x0cf7, 0x05e7 },
    { 0x0cf8, 0x05e8 },
    { 0x0cf9, 0x05e9 },
    { 0x0cfa, 0x05ea },
    { 0x0da1, 0x0e01 },
    { 0x0da2, 0x0e02 },
    { 0x0da3, 0x0e03 },
    { 0x0da4, 0x0e04 },
    { 0x0da5, 0x0e05 },
    { 0x0da6, 0x0e06 },
    { 0x0da7, 0x0e07 },
    { 0x0da8, 0x0e08 },
    { 0x0da9, 0x0e09 },
    { 0x0daa, 0x0e0a },
    { 0x0dab, 0x0e0b },
    { 0x0dac, 0x0e0c },
    { 0x0dad, 0x0e0d },
    { 0x0dae, 0x0e0e },
    { 0x0daf, 0x0e0f },
    { 0x0db0, 0x0e10 },
    { 0x0db1, 0x0e11 },
    { 0x0db2, 0x0e12 },
    { 0x0db3, 0x0e13 },
    { 0x0db4, 0x0e14 },
    { 0x0db5, 0x0e15 },
    { 0x0db6, 0x0e16 },
    { 0x0db7, 0x0e17 },
    { 0x0db8, 0x0e18 },
    { 0x0db9, 0x0e19 },
    { 0x0dba, 0x0e1a },
    { 0x0dbb, 0x0e1b },
    { 0x0dbc, 0x0e1c },
    { 0x0dbd, 0x0e1d },
    { 0x0dbe, 0x0e1e },
    { 0x0dbf, 0x0e1f },
    { 0x0dc0, 0x0e20 },
    { 0x0dc1, 0x0e21 },
    { 0x0dc2, 0x0e22 },
    { 0x0dc3, 0x0e23 },
    { 0x0dc4, 0x0e24 },
    { 0x0dc5, 0x0e25 },
    { 0x0dc6, 0x0e26 },
    { 0x0dc7, 0x0e27 },
    { 0x0dc8, 0x0e28 },
    { 0x0dc9, 0x0e29 },
    { 0x0dca, 0x0e2a },
    { 0x0dcb, 0x0e2b },
    { 0x0dcc, 0x0e2c },
    { 0x0dcd, 0x0e2d },
    { 0x0dce, 0x0e2e },
    { 0x0dcf, 0x0e2f },
    { 0x0dd0, 0x0e30 },
    { 0x0dd1, 0x0e31 },
    { 0x0dd2, 0x0e32 },
    { 0x0dd3, 0x0e33 },
    { 0x0dd4, 0x0e34 },
    { 0x0dd5, 0x0e35 },
    { 0x0dd6, 0x0e36 },
    { 0x0dd7, 0x0e37 },
    { 0x0dd8, 0x0e38 },
    { 0x0dd9, 0x0e39 },
    { 0x0dda, 0x0e3a },
    { 0x0ddf, 0x0e3f },
    { 0x0de0, 0x0e40 },
    { 0x0de1, 0x0e41 },
    { 0x0de2, 0x0e42 },
    { 0x0de3, 0x0e43 },
    { 0x0de4, 0x0e44 },
    { 0x0de5, 0x0e45 },
    { 0x0de6, 0x0e46 },
    { 0x0de7, 0x0e47 },
    { 0x0de8, 0x0e48 },
    { 0x0de9, 0x0e49 },
    { 0x0dea, 0x0e4a },
    { 0x0deb, 0x0e4b },
    { 0x0dec, 0x0e4c },
    { 0x0ded, 0x0e4d },
    { 0x0df0, 0x0e50 },
    { 0x0df1, 0x0e51 },
    { 0x0df2, 0x0e52 },
    { 0x0df3, 0x0e53 },
    { 0x0df4, 0x0e54 },
    { 0x0df5, 0x0e55 },
    { 0x0df6, 0x0e56 },
    { 0x0df7, 0x0e57 },
    { 0x0df8, 0x0e58 },
    { 0x0df9, 0x0e59 },
    { 0x0ea1, 0x3131 },
    { 0x0ea2, 0x3132 },
    { 0x0ea3, 0x3133 },
    { 0x0ea4, 0x3134 },
    { 0x0ea5, 0x3135 },
    { 0x0ea6, 0x3136 },
    { 0x0ea7, 0x3137 },
    { 0x0ea8, 0x3138 },
    { 0x0ea9, 0x3139 },
    { 0x0eaa, 0x313a },
    { 0x0eab, 0x313b },
    { 0x0eac, 0x313c },
    { 0x0ead, 0x313d },
    { 0x0eae, 0x313e },
    { 0x0eaf, 0x313f },
    { 0x0eb0, 0x3140 },
    { 0x0eb1, 0x3141 },
    { 0x0eb2, 0x3142 },
    { 0x0eb3, 0x3143 },
    { 0x0eb4, 0x3144 },
    { 0x0eb5, 0x3145 },
    { 0x0eb6, 0x3146 },
    { 0x0eb7, 0x3147 },
    { 0x0eb8, 0x3148 },
    { 0x0eb9, 0x3149 },
    { 0x0eba, 0x314a },
    { 0x0ebb, 0x314b },
    { 0x0ebc, 0x314c },
    { 0x0ebd, 0x314d },
    { 0x0ebe, 0x314e },
    { 0x0ebf, 0x314f },
    { 0x0ec0, 0x3150 },
    { 0x0ec1, 0x3151 },
    { 0x0ec2, 0x3152 },
    { 0x0ec3, 0x3153 },
    { 0x0ec4, 0x3154 },
    { 0x0ec5, 0x3155 },
    { 0x0ec6, 0x3156 },
    { 0x0ec7, 0x3157 },
    { 0x0ec8, 0x3158 },
    { 0x0ec9, 0x3159 },
    { 0x0eca, 0x315a },
    { 0x0ecb, 0x315b },
    { 0x0ecc, 0x315c },
    { 0x0ecd, 0x315d },
    { 0x0ece, 0x315e },
    { 0x0ecf, 0x315f },
    { 0x0ed0, 0x3160 },
    { 0x0ed1, 0x3161 },
    { 0x0ed2, 0x3162 },
    { 0x0ed3, 0x3163 },
    { 0x0ed4, 0x11a8 },
    { 0x0ed5, 0x11a9 },
    { 0x0ed6, 0x11aa },
    { 0x0ed7, 0x11ab },
    { 0x0ed8, 0x11ac },
    { 0x0ed9, 0x11ad },
    { 0x0eda, 0x11ae },
    { 0x0edb, 0x11af },
    { 0x0edc, 0x11b0 },
    { 0x0edd, 0x11b1 },
    { 0x0ede, 0x11b2 },
    { 0x0edf, 0x11b3 },
    { 0x0ee0, 0x11b4 },
    { 0x0ee1, 0x11b5 },
    { 0x0ee2, 0x11b6 },
    { 0x0ee3, 0x11b7 },
    { 0x0ee4, 0x11b8 },
    { 0x0ee5, 0x11b9 },
    { 0x0ee6, 0x11ba },
    { 0x0ee7, 0x11bb },
    { 0x0ee8, 0x11bc },
    { 0x0ee9, 0x11bd },
    { 0x0eea, 0x11be },
    { 0x0eeb, 0x11bf },
    { 0x0eec, 0x11c0 },
    { 0x0eed, 0x11c1 },
    { 0x0eee, 0x11c2 },
    { 0x0eef, 0x316d },
    { 0x0ef0, 0x3171 },
    { 0x0ef1, 0x3178 },
    { 0x0ef2, 0x317f },
    { 0x0ef3, 0x3181 },
    { 0x0ef4, 0x3184 },
    { 0x0ef5, 0x3186 },
    { 0x0ef6, 0x318d },
    { 0x0ef7, 0x318e },
    { 0x0ef8, 0x11eb },
    { 0x0ef9, 0x11f0 },
    { 0x0efa, 0x11f9 },
    { 0x0eff, 0x20a9 },
    { 0x13a4, 0x20ac },
    { 0x13bc, 0x0152 },
    { 0x13bd, 0x0153 },
    { 0x13be, 0x0178 },
    { 0x20ac, 0x20ac },
};

int
SdlTkKeysym2Unicode(KeySym keysym)
{
    int min = 0;
    int max = sizeof (keysymtab) / sizeof (keysymtab[0]) - 1;
    int mid;

    /* Latin-1 */
    if (((keysym >= 0x0020) && (keysym <= 0x007e)) ||
	((keysym >= 0x00a0) && (keysym <= 0x00ff))) {
	return keysym;
    }

    /* binary search in table */
    while (max >= min) {
	mid = (min + max) / 2;
	if (keysymtab[mid].keysym < keysym) {
	    min = mid + 1;
	} else if (keysymtab[mid].keysym > keysym) {
	    max = mid - 1;
	} else {
	    return keysymtab[mid].ucs;
	}
    }
    return 0;
}

static
int ucscmp(const void *a, const void *b)
{
    const struct codepair *pa = (struct codepair *) a;
    const struct codepair *pb = (struct codepair *) b;

    return (int) pa->ucs - (int) pb->ucs;
}

KeySym
SdlTkUnicode2Keysym(int ucs)
{
    int min = 0;
    int max = sizeof (keysymtab) / sizeof (keysymtab[0]) - 1;
    int mid;
    static struct codepair *revsymtab = NULL;
    TCL_DECLARE_MUTEX(revsym_mutex);

    if ((ucs < 0) || (ucs > 0xffff)) {
	return NoSymbol;
    }

    /* Latin-1 */
    if (((ucs >= 0x0020) && (ucs <= 0x007e)) ||
	((ucs >= 0x00a0) && (ucs <= 0x00ff))) {
	return (KeySym) ucs;
    }

    if (revsymtab == NULL) {
        Tcl_MutexLock(&revsym_mutex);
	if (revsymtab == NULL) {
	    revsymtab = (struct codepair *) ckalloc(sizeof (keysymtab));
	    memcpy(revsymtab, keysymtab, sizeof (keysymtab));
	    qsort(revsymtab, max + 1, sizeof (revsymtab[0]), ucscmp);
	}
	Tcl_MutexUnlock(&revsym_mutex);
    }
    if (revsymtab == NULL) {
	return NoSymbol;
    }

    /* binary search in table */
    while (max >= min) {
	mid = (min + max) / 2;
	if (revsymtab[mid].ucs < ucs) {
	    min = mid + 1;
	} else if (revsymtab[mid].ucs > ucs) {
	    max = mid - 1;
	} else {
	    return (KeySym) revsymtab[mid].keysym;
	}
    }
    return NoSymbol;
}

KeySym
SdlTkUtf2KeySym(const char *utf, int len, int *lenret)
{
    if (len > 0) {
	Tcl_UniChar ch;
	int n;

	n = Tcl_UtfToUniChar(utf, &ch);
	if ((n > 0) && (n <= len)) {
	    if (lenret != NULL) {
		*lenret = n;
	    }
	    return SdlTkUnicode2Keysym(ch);
	}
	if (lenret != NULL) {
	    *lenret = -1;
	}
    } else if (lenret != NULL) {
	*lenret = 0;
    }
    return NoSymbol;
}

/* Needed for TkpGetString */
int
XLookupString(XKeyEvent *event_struct, char *buffer_return, int bytes_buffer,
	      KeySym *keysym_return, XComposeStatus *status_in_out)
{
    if (keysym_return) {
	if (event_struct->nbytes > 0) {
	    KeySym keysym = SdlTkUtf2KeySym(event_struct->trans_chars,
					    event_struct->nbytes, NULL);

	    if (keysym != NoSymbol) {
		*keysym_return = keysym;
		goto conv;
	    }
	}
	*keysym_return = XKeycodeToKeysym(NULL, event_struct->keycode, 0);
    }
    /* Already converted to UTF-8 by SdlTkTranslateEvent */
conv:
    memcpy(buffer_return, event_struct->trans_chars, event_struct->nbytes);
    return event_struct->nbytes; /* length */
}

/*
 *----------------------------------------------------------------------
 *
 * NotifyVisibility --
 *
 *      This function recursively notifies the mapped children of the
 *      specified window of a change in visibility.  Note that we don't
 *      properly report the visibility state, since Windows does not
 *      provide that info.  The eventPtr argument must point to an event
 *      that has been completely initialized except for the window slot.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates lots of events.
 *
 *----------------------------------------------------------------------
 */

static void
NotifyVisibility(XEvent *eventPtr, Window w)
{
    _Window *_w = (_Window *) w;

    if (_w->atts.your_event_mask & VisibilityChangeMask) {
	eventPtr->xvisibility.serial = _w->display->request;
	eventPtr->xvisibility.display = _w->display;
        eventPtr->xvisibility.window = w;
        SdlTkQueueEvent(eventPtr);
    }
    for (_w = _w->child; _w != NULL; _w = _w->next) {
        if (_w->atts.map_state != IsUnmapped) {
            NotifyVisibility(eventPtr, (Window) _w);
        }
    }
}

static void
SdlTkMapWindow(Display *display, Window w)
{
    _Window *_w = (_Window *) w;
    XEvent event;
    int doconf = 0;

    if (_w->display == NULL) {
	return;
    }
    if (_w->atts.map_state != IsUnmapped) {
	return;
    }

    memset(&event, 0, sizeof (event));

    if (_w->fullscreen) {
	if ((_w->atts.width != SdlTkX.screen->width) ||
	    (_w->atts.height != SdlTkX.screen->height)) {
	    _w->atts_saved = _w->atts;
	    _w->atts.width = SdlTkX.screen->width;
	    _w->atts.height = SdlTkX.screen->height;
	    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
		_Window *_p = _w->parent;

		_w->atts.x = SdlTkX.dec_frame_width;
		_w->atts.y = SdlTkX.dec_title_height;
		_p->atts.width = _w->atts.width + 2 * SdlTkX.dec_frame_width;
		_p->atts.height = _w->atts.width + SdlTkX.dec_frame_width +
				  SdlTkX.dec_title_height;
		_p->atts.x = -SdlTkX.dec_frame_width;
		_p->atts.y = -SdlTkX.dec_title_height;
	    } else {
		_w->atts.x = _w->atts.y = 0;
	    }
	    doconf = 1;
	}
    }
    if (_w->atts.your_event_mask & StructureNotifyMask) {
	doconf = 1;
    }

    /*
     * A reparenting window manager like twm will get a MapRequest event
     * when XMapWindow is called. It will then create a decorative frame
     * window to contain the window, and reparent the window inside the
     * decorative frame.
     */

    if (PARENT_IS_ROOT(w) &&
	    (_w->tkwin != NULL) &&
	    !_w->atts.override_redirect &&
	    (_w->dec == NULL)) {
	Window wdec;
	int x, y, width, height;

	/*
	 * Each reparent gets a drawing surface for itself and all its
	 * children.
	 */

	x = _w->atts.x;
	y = _w->atts.y;
	width = _w->atts.width + SdlTkX.dec_frame_width * 2;
	height = _w->atts.height + SdlTkX.dec_title_height +
	    SdlTkX.dec_frame_width;
	if (_w->fullscreen) {
	    x -= SdlTkX.dec_frame_width;
	    y -= SdlTkX.dec_title_height;
	}

	wdec = SdlTkCreateWindow(display, SdlTkX.screen->root,
				 x, y, width, height, 0,
				 SdlTkX.screen->root_depth, InputOutput,
				 SdlTkX.screen->root_visual, 0, NULL);

	SdlTkDecCreate((_Window *) wdec);

	/*
	 * Reparent the window into the decorative frame, and move it
	 * to the proper place.
	 */

	SdlTkReparentWindow(display, w, wdec,
			    SdlTkX.dec_frame_width, SdlTkX.dec_title_height);

	/* Let Tk know I moved the window. You would think the <Reparent>
	 * event would be a good clue */
	if (doconf) {
	    SdlTkGenerateConfigureNotify(NULL, w);
	}
    } else if ((_w->tkwin != NULL) && doconf) {
	SdlTkGenerateConfigureNotify(NULL, w);
    }

    /* Map decorative frame */
    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
	_w->parent->atts.map_state = IsViewable;
    }

    _w->atts.map_state = IsViewable;

    /* Tk only cares about this for wrapper windows */
    if (_w->atts.your_event_mask & StructureNotifyMask) {
	event.type = MapNotify;
	event.xmap.serial = _w->display->request;
	event.xmap.send_event = False;
	event.xmap.display = _w->display;
	event.xmap.event = w;
	event.xmap.window = w;
	event.xmap.override_redirect = _w->atts.override_redirect;
	SdlTkQueueEvent(&event);
    }

    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
	SdlTkVisRgnChanged(_w->parent, VRC_CHANGED | VRC_DO_PARENT, 0, 0);
    } else {
	SdlTkVisRgnChanged(_w, VRC_CHANGED | VRC_DO_PARENT, 0, 0);
    }

    if (!_w->atts.override_redirect) {
	SdlTkRestackTransients(_w);
    }

    /*
     * Generate a <FocusIn> if this is the top-most Tk wrapper window.
     * Don't focus on override_redirect's though (i.e., menus).
     */

    if (_w == SdlTkTopVisibleWrapper() && (_w->parent != NULL) &&
	!_w->atts.override_redirect) {
	if (SdlTkX.keyboard_window == NULL) {
	    SdlTkSetInputFocus(display, w, RevertToParent, CurrentTime);
	}
    }

    SdlTkScreenChanged();

    /*
     * Generate VisibilityNotify events for this window and its mapped
     * children.
     */

    event.type = VisibilityNotify;
    event.xvisibility.serial = _w->display->request;
    event.xvisibility.send_event = False;
    event.xvisibility.display = _w->display;
    event.xvisibility.window = w;
    event.xvisibility.state = VisibilityUnobscured;
    NotifyVisibility(&event, w);
}

void
XMapWindow(Display *display, Window w)
{
    SdlTkLock(display);
    display->request++;
    SdlTkMapWindow(display, w);
    SdlTkUnlock(display);
}

void
XRaiseWindow(Display *display, Window w)
{
    _Window *_w = (_Window *) w;

    SdlTkLock(display);

    display->request++;

    if (_w->display == NULL) {
	goto done;
    }
    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
	SdlTkRestackWindow(_w->parent, NULL, Above);
	SdlTkRestackTransients(_w);
    } else {
	SdlTkRestackWindow(_w, NULL, Above);
	if (PARENT_IS_ROOT(_w) && !_w->atts.override_redirect) {
	    SdlTkRestackTransients(_w);
	}
    }

    /*
     * Generate a <FocusIn> if this is the top-most Tk wrapper window.
     * Don't focus on override_redirect's though (i.e., menus).
     */

    if (_w == SdlTkTopVisibleWrapper() && (_w->parent != NULL) &&
	!_w->atts.override_redirect) {
	if (SdlTkX.keyboard_window == NULL) {
	    SdlTkSetInputFocus(display, w, RevertToParent, CurrentTime);
	}
    }

    SdlTkScreenChanged();
done:
    SdlTkUnlock(display);
}

void
XLowerWindow(Display *display, Window w)
{
    _Window *_w = (_Window *) w;

    SdlTkLock(display);

    display->request++;

    if (_w->display == NULL) {
	goto done;
    }
    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
	SdlTkRestackWindow(_w->parent, NULL, Below);
    } else {
	SdlTkRestackWindow(_w, NULL, Below);
    }

    SdlTkScreenChanged();
done:
    SdlTkUnlock(display);
}

void
SdlTkMoveWindow(Display *display, Window w, int x, int y)
{
    _Window *_w = (_Window *) w;
    int ox = 0, oy = 0, flags;

    if (_w->display == NULL) {
	return;
    }
    if (_w->fullscreen) {
	if (_w->atts.your_event_mask & StructureNotifyMask) {
	    SdlTkGenerateConfigureNotify(NULL, w);
	}
	return;
    }

    flags = VRC_CHANGED | VRC_DO_PARENT;

    /*
     * If the window has a decorative frame, move the decorative frame
     * instead of the given window.
     */

    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
	_Window *wdec = _w->parent;

	if ((x != wdec->atts.x) || (y != wdec->atts.y)) {
	    ox = wdec->atts.x;
	    oy = wdec->atts.y;
	    flags |= VRC_MOVE | VRC_EXPOSE;
	    wdec->atts.x = x;
	    wdec->atts.y = y;
	}
    } else {
	if ((x != _w->atts.x) || (y != _w->atts.y)) {
	    ox = _w->atts.x;
	    oy = _w->atts.y;
	    flags |= VRC_MOVE | VRC_EXPOSE;
	    _w->atts.x = x;
	    _w->atts.y = y;
	}
    }

    if (_w->atts.your_event_mask & StructureNotifyMask) {
	SdlTkGenerateConfigureNotify(NULL, w);
    }

    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
	SdlTkVisRgnChanged(_w->parent, flags, ox, oy);
    } else {
	SdlTkVisRgnChanged(_w, flags, ox, oy);
    }

    SdlTkScreenChanged();
}

void
XMoveWindow(Display *display, Window w, int x, int y)
{
    SdlTkLock(display);
    display->request++;
    SdlTkMoveWindow(display, w, x, y);
    SdlTkUnlock(display);
}

void
SdlTkMoveResizeWindow(Display *display, Window w, int x, int y,
		      unsigned int width, unsigned int height)
{
    _Window *_w = (_Window *) w;
    int ox = 0, oy = 0, flags;

    if (_w->display == NULL) {
	return;
    }
    if (_w->fullscreen) {
	if (_w->atts.your_event_mask & StructureNotifyMask) {
	    SdlTkGenerateConfigureNotify(NULL, w);
	}
	return;
    }

    if ((int) width < 1) {
	width = 1;
    }
    if ((int) height < 1) {
	height = 1;
    }

    flags = VRC_CHANGED | VRC_DO_PARENT;

    /*
     * If this window has a decorative frame, move the decorative frame,
     * not the given window
     */

    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
	_Window *wdec = _w->parent;

	if ((x != wdec->atts.x) || (y != wdec->atts.y)) {
	    ox = wdec->atts.x;
	    oy = wdec->atts.y;
	    flags |= VRC_MOVE | VRC_EXPOSE;
	    wdec->atts.x = x;
	    wdec->atts.y = y;
	}
    } else {

	/*
	 * ConfigureEvent will call this on the children of a wrapper even if
	 * their size/position doesn't change. ConfigureEvent doesn't wait
	 * for <ConfigureNotify> so do nothing in this case.
	 */

	if ((x == _w->atts.x) && (y == _w->atts.y) &&
	    (width == _w->atts.width) && (height == _w->atts.height)) {
	    return;
	}

	if ((x != _w->atts.x) || (y != _w->atts.y)) {
	    ox = _w->atts.x;
	    oy = _w->atts.y;
	    flags |= VRC_MOVE | VRC_EXPOSE;
	    _w->atts.x = x;
	    _w->atts.y = y;
	}
    }

    /* "wm geom +x+y" will call this, even though the size doesn't change */
    if ((_w->atts.width != width) || (_w->atts.height != height)) {

	/* If this window has a decorative frame, resize it */
	if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
	    _Window *wdec = _w->parent;

	    wdec->atts.width =
		width + SdlTkX.dec_frame_width * 2;
	    wdec->atts.height =
		height + SdlTkX.dec_title_height + SdlTkX.dec_frame_width;

	    /*
	     * A window's requested width/height are *inside* its borders,
	     * a child window is clipped within its parent's borders.
	     */

	    wdec->parentWidth =
		wdec->atts.width + 2 * wdec->atts.border_width;
	    wdec->parentHeight =
		wdec->atts.height + 2 * wdec->atts.border_width;
	}

	if ((width > _w->atts.width) || (height > _w->atts.height)) {
	    flags |= VRC_EXPOSE;
	}
	_w->atts.width = width;
	_w->atts.height = height;

	/*
	 * A window's requested width/height are *inside* its borders,
	 * a child window is clipped within its parent's borders.
	 */

	_w->parentWidth = width + 2 * _w->atts.border_width;
	_w->parentHeight = height + 2 * _w->atts.border_width;

    }

    if (_w->atts.your_event_mask & StructureNotifyMask) {
	SdlTkGenerateConfigureNotify(NULL, w);
    }

    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
	SdlTkVisRgnChanged(_w->parent, flags, ox, oy);
    } else {
	SdlTkVisRgnChanged(_w, flags, ox, oy);
    }

    SdlTkScreenChanged();
}

void
XMoveResizeWindow(Display *display, Window w, int x, int y,
		  unsigned int width, unsigned int height)
{
    SdlTkLock(display);
    display->request++;
    SdlTkMoveResizeWindow(display, w, x, y, width, height);
    SdlTkUnlock(display);
}

int
XNextEvent(Display *display, XEvent *event_return)
{
    _XSQEvent *qevent;
    int n, once = 1;

again:
    Tcl_MutexLock((Tcl_Mutex *) &display->qlock);
#ifdef _WIN32
    if ((HANDLE) display->fd != INVALID_HANDLE_VALUE) {
	WaitForSingleObject((HANDLE) display->fd, once ? 0 : 10);
    }
#else
    if (display->fd >= 0) {
	char buffer[64];

#ifdef linux
	if (display->ext_number < 0) {
	    long long buffer;

	    n = read(display->fd, &buffer, sizeof (buffer));
	    if ((n < 0) && (errno != EWOULDBLOCK) && (errno != EAGAIN)) {
		close(display->fd);
		display->fd = -1;
	    }
	} else
#endif
	for (;;) {
	    n = read(display->fd, buffer, sizeof (buffer));
	    if ((n < 0) && ((errno == EWOULDBLOCK) || (errno == EAGAIN))) {
		break;
	    }
	    if (n <= 0) {
		close(display->fd);
		display->fd = -1;
		close(display->ext_number);
		display->ext_number = -1;
		break;
	    }
	}
    }
#endif
    qevent = display->head;
    if (qevent != NULL) {
	*event_return = qevent->event;
	/* Remove from front of queue */
	display->head = qevent->next;
	if (display->head == NULL) {
	    display->tail = NULL;
	}
	/* Add to front of free list */
	qevent->next = display->qfree;
	display->qfree = qevent;
	display->qlen--;
	/* Shrink free list down to 4 times initial pre-allocated size */
	n = 0;
	while (display->nqtotal > 4 * 128) {
	    qevent = display->qfree;
	    if (qevent == NULL) {
		break;
	    }
	    display->qfree = qevent->next;
	    display->nqtotal--;
	    ckfree((char *) qevent);
	    if (++n > 16) {
		break;
	    }
	}
    } else {
	Tcl_MutexUnlock((Tcl_Mutex *) &display->qlock);
	if (once) {
	    once = 0;
	    EVLOG("XNextEvent sleeping");
	}
#ifndef _WIN32
	/* On Windows the sleep is in WaitForSingleObject() above. */
	Tcl_Sleep(10);
#endif
	goto again;
    }
    Tcl_MutexUnlock((Tcl_Mutex *) &display->qlock);
    if (event_return->xany.type == VirtualEvent) {
	/* Convert name field to thread-specific Tk_Uid. */
	XVirtualEvent *xe = (XVirtualEvent *) event_return;

	xe->name = Tk_GetUid((const char *) xe->name);
	EVLOG("VirtualEvent '%s'", (char *) xe->name);
    } else if (event_return->xany.type == PointerUpdate) {
	/* Pointer updates handled similar to Windows */
	XUpdatePointerEvent *pe = (XUpdatePointerEvent *) event_return;

	if (((_Window *) pe->window)->display == display) {
	    Tk_UpdatePointer(pe->tkwin, pe->x, pe->y, pe->state);
	}
    } else if ((event_return->xany.type == ConfigureNotify) &&
	       (event_return->xconfigure.event == display->screens[0].root)) {
	/* Size change of root window handled specially */
	int oldw = display->screens[0].width;
	int oldh = display->screens[0].height;
	int neww = event_return->xconfigure.width;
	int newh = event_return->xconfigure.height;

	display->screens[0].width = neww;
	display->screens[0].height = newh;
	if (display->screens[0].moverride) {
	    if (((oldw < oldh) && (neww > newh)) ||
		((oldw > oldh) && (neww < newh))) {
		int swap = display->screens[0].mwidth;

		display->screens[0].mwidth = display->screens[0].mheight;
		display->screens[0].mheight = swap;
	    }
	} else {
	    display->screens[0].mwidth = event_return->xconfigure.x;
	    display->screens[0].mheight = event_return->xconfigure.y;
	}
	event_return->xconfigure.x = event_return->xconfigure.y = 0;
    }
    EVLOG("XNextEvent %d %p", event_return->xany.type,
	  (void *) event_return->xany.window);
    return 0;
}

int
XNoOp(Display *display)
{
    SdlTkLock(display);
    display->request++;
    SdlTkUnlock(display);
    return 0;
}

void
SdlTkPanInt(int dx, int dy)
{
    int x, y, w, h, sw, sh;

    SDL_GetWindowSize(SdlTkX.sdlscreen, &sw, &sh);
    if (SdlTkX.root_w) {
	w = SdlTkX.root_w;
	h = SdlTkX.root_h;
    } else {
	w = sw;
	h = sh;
    }
    x = SdlTkX.viewport.x + dx;
    y = SdlTkX.viewport.y + dy;
    if (w - (SdlTkX.viewport.w + x) < 0) {
	x = w - SdlTkX.viewport.w;
    }
    if (x < 0) {
	x = 0;
    }
    if (h - (SdlTkX.viewport.h + y) < 0) {
	y = h - SdlTkX.viewport.h;
    }
    if (y < 0) {
	y = 0;
    }
    if ((x != SdlTkX.viewport.x) || (y != SdlTkX.viewport.y)) {
	SdlTkX.viewport.x = x;
	SdlTkX.viewport.y = y;
	SdlTkX.draw_later |= SDLTKX_RENDCLR | SDLTKX_PRESENT;
	SdlTkSendViewportUpdate();
    }
}

int
SdlTkZoomInt(int x, int y, float z)
{
    float scale = SdlTkX.scale * z;
    int vw, vh, sw, sh, ow, oh, ret = 0;

    if (scale - 0.0001 < SdlTkX.scale_min) {
	scale = SdlTkX.scale_min;
    } else if (scale > 8.0f) {
	return -1;
    }
#ifdef ANDROID
    if (SDL_fabs(scale - 1.0f) < 0.005) {
	scale = 1.0f;
    }
#else
    if (SDL_fabs(scale - 1.0f) < 0.02) {
	scale = 1.0f;
    }
#endif
    SDL_GetWindowSize(SdlTkX.sdlscreen, &sw, &sh);
    vw = (int) (sw / scale);
    vh = (int) (sh / scale);
    x =  (int) (x / SdlTkX.scale) + SdlTkX.viewport.x - (int) (x / scale);
    y =  (int) (y / SdlTkX.scale) + SdlTkX.viewport.y - (int) (y / scale);
    if (sw - (vw + x) < 0) {
	x = sw - vw;
    }
    if (x < 0) {
	x = 0;
    }
    if (sh - (vh + y) < 0) {
	y = sh - vh;
    }
    if (y < 0) {
	y = 0;
    }
    if (SdlTkX.root_w) {
	if (vw > SdlTkX.root_w) {
	    x -= vw - SdlTkX.root_w;
	    vw = SdlTkX.root_w;
	    if (x < 0) {
		x = 0;
	    }
	}
	if (vh > SdlTkX.root_h) {
	    y -= vh - SdlTkX.root_h;
	    vh = SdlTkX.root_h;
	    if (y < 0) {
		y = 0;
	    }
	}
    } else {
	if (vw > sw) {
	    vw = sw;
	}
	if (vh > sh) {
	    vh = sh;
	}
    }
    if ((scale != SdlTkX.scale) ||
	(x != SdlTkX.viewport.x) ||
	(y != SdlTkX.viewport.y) ||
	(vw != SdlTkX.viewport.w) ||
	(vh != SdlTkX.viewport.h)) {
	SdlTkX.scale = scale;
	SdlTkX.viewport.x = x;
	SdlTkX.viewport.y = y;
	SdlTkX.viewport.w = vw;
	SdlTkX.viewport.h = vh;
	SdlTkX.draw_later |= SDLTKX_RENDCLR | SDLTKX_PRESENT;
	SdlTkSendViewportUpdate();
	ret = 1;
    }
    ow = (int) SDL_ceil(vw * SdlTkX.scale);
    oh = (int) SDL_ceil(vh * SdlTkX.scale);
    if ((ow < sw) || (oh < sh)) {
	if ((SdlTkX.outrect == NULL) ||
	    (SdlTkX.outrect->w != ow) ||
	    (SdlTkX.outrect->h != oh)) {
	    SdlTkX.draw_later |= SDLTKX_RENDCLR | SDLTKX_PRESENT;
	}
	SdlTkX.outrect = &SdlTkX.outrect0;
	SdlTkX.outrect->x = (sw - ow) / 2;
	SdlTkX.outrect->y = (sh - oh) / 2;
	SdlTkX.outrect->w = ow;
	SdlTkX.outrect->h = oh;
    } else {
	SdlTkX.outrect = NULL;
    }
    if ((SdlTkX.viewport.w == sw) && (SdlTkX.viewport.h == sh)) {
	SdlTkX.draw_later &= ~SDLTKX_SCALED;
    } else {
	SdlTkX.draw_later |= SDLTKX_SCALED;
    }
    return ret;
}

static int
HandlePanZoom(struct PanZoomRequest *pz)
{
    int x, y, vw, vh, sw, sh, ow, oh, ret = 0;
    float aspReal, aspSpec, scale;

    SDL_GetWindowSize(SdlTkX.sdlscreen, &sw, &sh);
    x = pz->r.x;
    y = pz->r.y;
    vw = pz->r.w;
    vh = pz->r.h;
    if (SdlTkX.root_w) {
	aspReal = (float) SdlTkX.root_w / SdlTkX.root_h;
    } else {
	aspReal = (float) sw / sh;
    }
    aspSpec = (float) vw / vh;
    if (SDL_fabs(aspReal - aspSpec) > 0.0001) {
	vh = (int) (vw * aspReal);
    }
    scale = (float) sw / vw;
    if (scale - 0.0001 < SdlTkX.scale_min) {
	scale = SdlTkX.scale_min;
	x = y = 0;
	if (SdlTkX.root_w) {
	    vw = SdlTkX.root_w;
	    vh = SdlTkX.root_h;
	} else {
	    vw = sw;
	    vh = sh;
	}
    } else if (scale > 8.0f) {
	scale = 8.0f;
	if (SdlTkX.root_w) {
	    vw = SdlTkX.root_w / 8.0f;
	    vh = SdlTkX.root_h / 8.0f;
	    x = SdlTkX.root_w - vw;
	    y = SdlTkX.root_h - vh;
	} else {
	    vw = sw / 8.0f;
	    vh = sh / 8.0f;
	    x = sw - vw;
	    y = sh - vh;
	}
    } else {
	vw = (int) (sw / scale);
	vh = (int) (sh / scale);
	x =  (int) (x / SdlTkX.scale) + SdlTkX.viewport.x - (int) (x / scale);
	y =  (int) (y / SdlTkX.scale) + SdlTkX.viewport.y - (int) (y / scale);
    }
    if (sw - (vw + x) < 0) {
	x = sw - vw;
    }
    if (x < 0) {
	x = 0;
    }
    if (sh - (vh + y) < 0) {
	y = sh - vh;
    }
    if (y < 0) {
	y = 0;
    }
    if (SdlTkX.root_w) {
	if (vw > SdlTkX.root_w) {
	    x -= vw - SdlTkX.root_w;
	    vw = SdlTkX.root_w;
	    if (x < 0) {
		x = 0;
	    }
	}
	if (vh > SdlTkX.root_h) {
	    y -= vh - SdlTkX.root_h;
	    vh = SdlTkX.root_h;
	    if (y < 0) {
		y = 0;
	    }
	}
    } else {
	if (vw > sw) {
	    vw = sw;
	}
	if (vh > sh) {
	    vh = sh;
	}
    }
    if ((scale != SdlTkX.scale) ||
	(x != SdlTkX.viewport.x) ||
	(y != SdlTkX.viewport.y) ||
	(vw != SdlTkX.viewport.w) ||
	(vh != SdlTkX.viewport.h)) {
	SdlTkX.scale = scale;
	SdlTkX.viewport.x = x;
	SdlTkX.viewport.y = y;
	SdlTkX.viewport.w = vw;
	SdlTkX.viewport.h = vh;
	SdlTkX.draw_later |= SDLTKX_RENDCLR | SDLTKX_PRESENT;
	SdlTkSendViewportUpdate();
	ret = 1;
    }
    ow = (int) SDL_ceil(vw * SdlTkX.scale);
    oh = (int) SDL_ceil(vh * SdlTkX.scale);
    if ((ow < sw) || (oh < sh)) {
	if ((SdlTkX.outrect == NULL) ||
	    (SdlTkX.outrect->w != ow) ||
	    (SdlTkX.outrect->h != oh)) {
	    SdlTkX.draw_later |= SDLTKX_RENDCLR | SDLTKX_PRESENT;
	}
	SdlTkX.outrect = &SdlTkX.outrect0;
	SdlTkX.outrect->x = (sw - ow) / 2;
	SdlTkX.outrect->y = (sh - oh) / 2;
	SdlTkX.outrect->w = ow;
	SdlTkX.outrect->h = oh;
    } else {
	SdlTkX.outrect = NULL;
    }
    if ((SdlTkX.viewport.w == sw) && (SdlTkX.viewport.h == sh)) {
	SdlTkX.draw_later &= ~SDLTKX_SCALED;
    } else {
	SdlTkX.draw_later |= SDLTKX_SCALED;
    }
    SdlTkX.draw_later |= SDLTKX_DRAW | SDLTKX_DRAWALL;
    if (pz->running) {
	pz->running = 0;
	Tcl_ConditionNotify(&xlib_cond);
    }
    return ret;
}

int
SdlTkPanZoom(int locked, int x, int y, int w, int h)
{
    struct PanZoomRequest pz;
    SDL_Event event;

    pz.running = !locked;
    pz.r.x = x;
    pz.r.y = y;
    pz.r.w = w;
    pz.r.h = h;
    if (locked) {
	return HandlePanZoom(&pz);
    }
    SdlTkLock(NULL);
    event.type = SDL_USEREVENT;
    event.user.windowID = 0;
    event.user.code = 0;
    event.user.data1 = (void *) HandlePanZoom;
    event.user.data2 = (void *) &pz;
    SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
    while (pz.running) {
	SdlTkWaitLock();
    }
    SdlTkUnlock(NULL);
    return 0;
}

static void
HandleRootSize(struct RootSizeRequest *r)
{
    int width, height, oldw, oldh, sw, sh;
    SDL_PixelFormat *pfmt;
    SDL_Surface *newsurf = NULL;
    SDL_Texture *newtex = NULL;
    _Window *_w;
    int xdpi, ydpi;
#ifndef ANDROID
    int tfmt = SDL_PIXELFORMAT_RGB888;
#endif
    float aspReal, aspRoot;

    SDL_GetWindowSize(SdlTkX.sdlscreen, &sw, &sh);
    width = r->width;
    height = r->height;
    oldw = SdlTkX.screen->width;
    oldh = SdlTkX.screen->height;
    if ((width == oldw) && (height == oldh)) {
	goto done;
    }
    if ((width == 0) || (height == 0)) {
	/* adjust to real screen */
	width = sw;
	height = sh;
    }
    pfmt = SdlTkX.sdlsurf->format;
    newsurf = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height,
				   pfmt->BitsPerPixel, pfmt->Rmask,
				   pfmt->Gmask, pfmt->Bmask, pfmt->Amask);
#ifndef ANDROID
    if (pfmt->BitsPerPixel == 15) {
	tfmt = SDL_PIXELFORMAT_RGB555;
    } else if (pfmt->BitsPerPixel == 16) {
	tfmt = SDL_PIXELFORMAT_RGB565;
    } else if (pfmt->BitsPerPixel == 24) {
	if (pfmt->BytesPerPixel == 3) {
	    tfmt = SDL_PIXELFORMAT_RGB24;
	}
    }
#endif
    newtex = SDL_CreateTexture(SdlTkX.sdlrend,
#ifdef ANDROID
			       SDL_PIXELFORMAT_RGB888,
#else
			       tfmt,
#endif
			       SDL_TEXTUREACCESS_STREAMING,
			       width, height);
#ifdef ANDROID
    if ((newsurf != NULL) && (newtex != NULL)) {
	SDL_GL_SwapWindow(SdlTkX.sdlscreen);
    }
#endif
    if ((newsurf != NULL) && (newtex != NULL)) {
	SDL_Rect sr;
	Uint32 pixel;
	_Window *child;
	Display *dpy;

	SDL_SetRenderDrawColor(SdlTkX.sdlrend, 0, 0, 0, 255);
	SDL_RenderClear(SdlTkX.sdlrend);
	SDL_BlitSurface(SdlTkX.sdlsurf, NULL, newsurf, NULL);
	SDL_FreeSurface(SdlTkX.sdlsurf);
	SdlTkX.sdlsurf = newsurf;
	SDL_DestroyTexture(SdlTkX.sdltex);
	SdlTkX.sdltex = newtex;
	if ((r->width == 0) && (r->height == 0)) {
	    SdlTkX.root_w = 0;
	    SdlTkX.root_h = 0;
	} else {
	    SdlTkX.root_w = width;
	    SdlTkX.root_h = height;
	}
	SdlTkX.screen->width = width;
	SdlTkX.screen->height = height;
	xdpi = SdlTkX.arg_xdpi;
	ydpi = SdlTkX.arg_ydpi;
	if (xdpi == 0) {
	    xdpi = ydpi;
	}
	if (ydpi == 0) {
	    ydpi = xdpi;
	}
#if defined(ANDROID) && defined(SDL_HAS_GETWINDOWDPI)
	if (xdpi == 0) {
	    SDL_GetWindowDPI(SdlTkX.sdlscreen, &xdpi, &ydpi);
	}
#endif
	if (xdpi && ydpi) {
	    SdlTkX.screen->mwidth = (254 * width) / xdpi;
	    SdlTkX.screen->mwidth /= 10;
	    SdlTkX.screen->mheight = (254 * height) / ydpi;
	    SdlTkX.screen->mheight /= 10;
	} else {
#ifdef ANDROID
	    SdlTkX.screen->mwidth = (width * 254 + 360) / 1440;
	    SdlTkX.screen->mheight = (height * 254 + 360) / 1440;
#else
	    SdlTkX.screen->mwidth = (width * 254 + 360) / 720;
	    SdlTkX.screen->mheight = (height * 254 + 360) / 720;
#endif
	}
	dpy = SdlTkX.display->next_display;
	while (dpy != NULL) {
	    SdlTkGenerateConfigureNotify(dpy, dpy->screens[0].root);
	    dpy = dpy->next_display;
	}
	_w = (_Window *) SdlTkX.screen->root;
	_w->atts.width = _w->parentWidth = width;
	_w->atts.height = _w->parentHeight = height;
	pixel = SDL_MapRGB(SdlTkX.sdlsurf->format,
#ifdef ANDROID
			   0x00, 0x00, 0x00
#else
			   0x00, 0x4E, 0x78
#endif
			   );
	if (width > oldw) {
	    sr.x = oldw;
	    sr.w = width - oldw;
	    sr.y = 0;
	    sr.h = height;
	    SDL_FillRect(SdlTkX.sdlsurf, &sr, pixel);
	}
	if (height > oldh) {
	    sr.y = oldh;
	    sr.h = height - oldh;
	    sr.x = 0;
	    sr.w = width;
	    SDL_FillRect(SdlTkX.sdlsurf, &sr, pixel);
	}
	if ((width > oldw) || (height > oldh)) {
	    SdlTkVisRgnChanged(_w, VRC_CHANGED, 0, 0);
	}
	child = _w->child;
	while (child != NULL) {
	    if (child->fullscreen) {
		int xx, yy, ww, hh;
		_Window *_ww = child;

		xx = 0;
		yy = 0;
		ww = width;
		hh = height;
		if (child->dec != NULL) {
		    xx -= SdlTkX.dec_frame_width;
		    yy -= SdlTkX.dec_title_height;
		    _ww = child->child;
		}
		child->fullscreen = 0;
		_ww->fullscreen = 0;
		SdlTkMoveResizeWindow(SdlTkX.display,
				      (Window) _ww,
				      xx, yy, ww, hh);
		_ww->fullscreen = 1;
		child->fullscreen = 1;
	    }
	    child = child->next;
	}
    } else {
	if (newsurf != NULL) {
	    SDL_FreeSurface(newsurf);
	}
	if (newtex != NULL) {
	    SDL_DestroyTexture(newtex);
	}
	goto done;
    }
    aspReal = (float) sw / sh;
    aspRoot = (float) width / height;
    SdlTkX.scale_min = 1.0f;
    if (SDL_fabs(aspRoot - aspReal) < 0.0001) {
	if (width > sw) {
	    SdlTkX.scale_min = (float) sw / width;
	}
    } else if (aspRoot > aspReal) {
	if (width > sw) {
	    SdlTkX.scale_min = (float) sw / width;
	}
    } else {
	if (height > sh) {
	    SdlTkX.scale_min = (float) sh / height;
	}
    }
    if ((SdlTkX.viewport.w > width) ||
	(SdlTkX.viewport.h > height) ||
	(SdlTkX.scale < SdlTkX.scale_min)) {
	SdlTkX.scale = 1.0f;
	SdlTkX.viewport.x = 0;
	SdlTkX.viewport.y = 0;
	SdlTkX.viewport.w = width;
	SdlTkX.viewport.h = height;
    }
    SdlTkX.draw_later |= SDLTKX_DRAW | SDLTKX_DRAWALL;
    if (!SdlTkPanZoom(1,
		      SdlTkX.viewport.x, SdlTkX.viewport.y,
		      SdlTkX.viewport.w, SdlTkX.viewport.h)) {
	SdlTkSendViewportUpdate();
    }
done:
    if (r->running) {
	r->running = 0;
	Tcl_ConditionNotify(&xlib_cond);
    }
}

void
SdlTkSetRootSize(int w, int h)
{
    struct RootSizeRequest root;
    SDL_Event event;

    SdlTkLock(NULL);
    root.running = 1;
    root.width = w;
    root.height = h;
    event.type = SDL_USEREVENT;
    event.user.windowID = 0;
    event.user.code = 0;
    event.user.data1 = (void *) HandleRootSize;
    event.user.data2 = (void *) &root;
    SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
    while (root.running) {
	SdlTkWaitLock();
    }
    SdlTkUnlock(NULL);
}

#ifndef ANDROID
static void
HandleWindowFlags(struct WindowFlagsRequest *r)
{
    int flags;

    /* sdltk opacity */
    if (r->flags == 0) {
	SDL_SetWindowOpacity(SdlTkX.sdlscreen, r->opacity);
	goto done;
    }

    flags = SDL_GetWindowFlags(SdlTkX.sdlscreen);

    /* sdltk fullscreen */
    if ((r->flags & SDL_WINDOW_FULLSCREEN) &&
	!(flags & SDL_WINDOW_FULLSCREEN) &&
	!SdlTkX.arg_fullscreen && SdlTkX.arg_resizable) {
	int num;
	SDL_DisplayMode info;

	if (flags & SDL_WINDOW_HIDDEN) {
	    SDL_ShowWindow(SdlTkX.sdlscreen);
	}
	num = SDL_GetWindowDisplayIndex(SdlTkX.sdlscreen);
	if ((num >= 0) && SDL_GetDesktopDisplayMode(num, &info) == 0) {
	    SDL_SetWindowSize(SdlTkX.sdlscreen, info.w, info.h);
	    SDL_SetWindowFullscreen(SdlTkX.sdlscreen, SDL_WINDOW_FULLSCREEN);
	}
	goto done;
    }
    /* sdltk restore */
    if ((r->flags & (SDL_WINDOW_SHOWN | SDL_WINDOW_HIDDEN)) ==
	(SDL_WINDOW_SHOWN | SDL_WINDOW_HIDDEN)) {
	if (flags & SDL_WINDOW_HIDDEN) {
	    SDL_ShowWindow(SdlTkX.sdlscreen);
	}
	if (flags & SDL_WINDOW_FULLSCREEN) {
	    SDL_SetWindowFullscreen(SdlTkX.sdlscreen, 0);
	} else {
	    SDL_RestoreWindow(SdlTkX.sdlscreen);
	}
	goto done;
    }
    /* sdltk deiconify */
    if (r->flags & SDL_WINDOW_SHOWN) {
	if (flags & SDL_WINDOW_HIDDEN) {
	    SDL_ShowWindow(SdlTkX.sdlscreen);
	}
	if (flags & SDL_WINDOW_FULLSCREEN) {
	    /* nothing */
	} else if (flags & SDL_WINDOW_MAXIMIZED) {
	    SDL_MaximizeWindow(SdlTkX.sdlscreen);
	} else {
	    SDL_RestoreWindow(SdlTkX.sdlscreen);
	}
	goto done;
    }
    /* sdltk iconify */
    if (r->flags & SDL_WINDOW_MINIMIZED) {
	if (flags & SDL_WINDOW_HIDDEN) {
	    SDL_ShowWindow(SdlTkX.sdlscreen);
	}
	if (!(flags & SDL_WINDOW_MINIMIZED)) {
	    SDL_MinimizeWindow(SdlTkX.sdlscreen);
	}
	goto done;
    }
    /* sdltk withdraw */
    if (r->flags & SDL_WINDOW_HIDDEN) {
	if (!(flags & SDL_WINDOW_HIDDEN)) {
	    SDL_HideWindow(SdlTkX.sdlscreen);
	}
	goto done;
    }
    /* sdltk maximize */
    if (r->flags & SDL_WINDOW_MAXIMIZED) {
	if (!(flags & SDL_WINDOW_MAXIMIZED) &&
	    !SdlTkX.arg_fullscreen && SdlTkX.arg_resizable) {
	    if (!(flags & (SDL_WINDOW_SHOWN | SDL_WINDOW_MINIMIZED))) {
		SDL_ShowWindow(SdlTkX.sdlscreen);
	    }
	    if (flags & SDL_WINDOW_FULLSCREEN) {
		SDL_SetWindowFullscreen(SdlTkX.sdlscreen, 0);
	    }
	    SDL_MaximizeWindow(SdlTkX.sdlscreen);
	}
	goto done;
    }
done:
    if (r->running) {
	r->running = 0;
	Tcl_ConditionNotify(&xlib_cond);
    }
}
#endif

void
SdlTkSetWindowFlags(int flags, int x, int y, int w, int h)
{
#ifndef ANDROID
    struct WindowFlagsRequest wreq;
    SDL_Event event;
    SDL_SysWMinfo wminfo;

    if (flags == 0) {
	return;
    }
    SdlTkLock(NULL);
    SDL_VERSION(&wminfo.version);
    if (SDL_GetWindowWMInfo(SdlTkX.sdlscreen, &wminfo)) {
	if (wminfo.subsystem == SDL_SYSWM_WAYLAND) {
	    /*
	     * Currently there's no stable support for
	     * changing the window visibility/state/size
	     * in the Wayland video driver.
	     */
	    goto done;
	}
    }
    wreq.running = 1;
    wreq.flags = flags;
    wreq.r.x = x;
    wreq.r.y = y;
    wreq.r.w = w;
    wreq.r.h = h;
    wreq.opacity = 1.0;
    event.type = SDL_USEREVENT;
    event.user.windowID = 0;
    event.user.code = 0;
    event.user.data1 = (void *) HandleWindowFlags;
    event.user.data2 = (void *) &wreq;
    SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
    while (wreq.running) {
	SdlTkWaitLock();
    }
done:
    SdlTkUnlock(NULL);
#endif
}

void
SdlTkSetWindowOpacity(double opacity)
{
#ifndef ANDROID
    struct WindowFlagsRequest wreq;
    SDL_Event event;

    SdlTkLock(NULL);
    wreq.running = 1;
    wreq.flags = 0;
    wreq.r.x = 0;
    wreq.r.y = 0;
    wreq.r.w = -1;
    wreq.r.h = -1;
    wreq.opacity = opacity;
    event.type = SDL_USEREVENT;
    event.user.windowID = 0;
    event.user.code = 0;
    event.user.data1 = (void *) HandleWindowFlags;
    event.user.data2 = (void *) &wreq;
    SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
    while (wreq.running) {
	SdlTkWaitLock();
    }
    SdlTkUnlock(NULL);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * TimerCallback --
 *
 *	SDL timer callback function invoked every 20 ms. If enabled
 *	sends an SDL_USEREVENT message to the SDL event queue to
 *	wake up EventThread().
 *
 *----------------------------------------------------------------------
 */

static Uint32
TimerCallback(Uint32 interval, void *clientData)
{
    volatile int *timerPtr = (int *) clientData;

    timerPtr[0] += interval;
    Tcl_ConditionNotify(&time_cond);
    if (timer_enabled) {
	SDL_Event event;

	event.type = SDL_USEREVENT;
	event.user.windowID = 0;
	event.user.code = timerPtr[0];
	event.user.data1 = (void *) TimerCallback;
	event.user.data2 = clientData;
	SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
    }
    return interval;
}

#ifndef ANDROID

/*
 *----------------------------------------------------------------------
 *
 * SDL RW operations for icon file loading.
 *
 *----------------------------------------------------------------------
 */

static Sint64
RWIconSize(struct SDL_RWops *rwops)
{
    return -1;
}

static Sint64
RWIconSeek(struct SDL_RWops *rwops, Sint64 offset, int whence)
{
    Tcl_Channel chan = (Tcl_Channel) rwops->hidden.unknown.data1;
    int op;

    switch (whence) {
    case RW_SEEK_SET:
	op = SEEK_SET;
	break;
    case RW_SEEK_CUR:
	op = SEEK_CUR;
	break;
    case RW_SEEK_END:
	op = SEEK_END;
	break;
    default:
	return -1;
    }
    return Tcl_Seek(chan, offset, op);
}

static size_t
RWIconRead(struct SDL_RWops *rwops, void *p, size_t size, size_t max)
{
    Tcl_Channel chan = (Tcl_Channel) rwops->hidden.unknown.data1;
    int n = size * max;

    return Tcl_Read(chan, p, n);
}

static size_t
RWIconWrite(struct SDL_RWops *rwops, const void *p, size_t size, size_t max)
{
    return -1;
}

static int
RWIconClose(struct SDL_RWops *rwops)
{
    Tcl_Channel chan = (Tcl_Channel) rwops->hidden.unknown.data1;

    Tcl_Close(NULL, chan);
    return 0;
}

#endif

/*
 *----------------------------------------------------------------------
 *
 * PerformSDLInit --
 *
 *	SDL initialization (long)
 *
 *----------------------------------------------------------------------
 */

static int
PerformSDLInit(int *root_width, int *root_height)
{
    Display *display;
    Screen *screen;
    int i, width, height, min_w = 200, min_h = 200;
    Uint32 videoFlags;
    _Window *_w;
    SDL_SysWMinfo wminfo;
    Uint32 fmt;
    SDL_DisplayMode info;
    SDL_PixelFormat *pfmt;
    int xdpi, ydpi;
    int initMask = SDL_INIT_VIDEO | SDL_INIT_JOYSTICK;
#ifndef ANDROID
    int tfmt = SDL_PIXELFORMAT_RGB888;
#endif
    XGCValues values;

#ifdef AGG_CUSTOM_ALLOCATOR
    /* init AGG custom allocator functions */
    agg_custom_alloc = (void *(*)(unsigned int)) Tcl_Alloc;
    agg_custom_free = (void (*)(void *)) Tcl_Free;
#endif

    if (SdlTkX.arg_sdllog) {
	SDL_LogSetAllPriority(SdlTkX.arg_sdllog);
    }
#ifndef ANDROID
#ifdef linux
    /*
     * Wayland: if SDL_VIDEODRIVER is unset but WAYLAND_DISPLAY
     * is set, prefer the Wayland video driver.
     */
    if (getenv("SDL_VIDEODRIVER") == NULL) {
	char *p = getenv("WAYLAND_DISPLAY");

	if ((p != NULL) && p[0]) {
	    putenv("SDL_VIDEODRIVER=wayland");
	}
    }
#endif
retryInit:
#endif
    if (SDL_Init(initMask) < 0) {
#ifdef ANDROID
	__android_log_print(ANDROID_LOG_ERROR, "libtk",
			    "Couldn't initialize SDL: %s", SDL_GetError());
#else
	if (initMask & SDL_INIT_JOYSTICK) {
	    initMask &= ~SDL_INIT_JOYSTICK;
	    goto retryInit;
	}
	SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
			"Couldn't initialize SDL: %s", SDL_GetError());
#endif
fatal:
	return 0;
    }
#ifdef ANDROID
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
#else
    if (!SdlTkX.arg_nogl) {
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    }
#endif

    /* preset some defaults */
    SdlTkX.dec_frame_width = 6;
    SdlTkX.dec_title_height = 20;
    SdlTkX.dec_font_size = 12;
    SdlTkX.dec_line_width = 2;
    SdlTkX.nearby_pixels = 9;
#ifdef ANDROID
    SdlTkX.sdlfocus = 1;
    SdlTkX.accel_id = -1;
    SdlTkX.accel_enabled = 0;
#else
    SdlTkX.sdlfocus = 0;
    Tcl_InitHashTable(&SdlTkX.sdlcursors, TCL_ONE_WORD_KEYS);
#endif
    Tcl_InitHashTable(&SdlTkX.joystick_table, TCL_ONE_WORD_KEYS);

    videoFlags = SDL_SWSURFACE;
#ifdef ANDROID
    videoFlags |= SDL_WINDOW_FULLSCREEN;
    videoFlags |= SDL_WINDOW_RESIZABLE;
    videoFlags |= SDL_WINDOW_BORDERLESS;
    width = 200;
    height = 200;
#else
    if (SdlTkX.arg_fullscreen) {
	videoFlags |= SDL_WINDOW_FULLSCREEN;
    }
    if (SdlTkX.arg_resizable) {
	videoFlags |= SDL_WINDOW_RESIZABLE;
    }
    if (SdlTkX.arg_noborder) {
	videoFlags |= SDL_WINDOW_BORDERLESS;
    }
    width = 1024;
    height = 768;
    /*
     * Start the root window hidden since font init
     * may take some time. At end of font init in
     * SdlTkUtils.c the root window is shown.
     */
    videoFlags |= SDL_WINDOW_HIDDEN;
#endif
    if (SdlTkX.arg_width != NULL) {
	int tmp = 0;

	sscanf(SdlTkX.arg_width, "%d", &tmp);
	if (tmp > 0) {
	    width = tmp;
	}
    }
    if (SdlTkX.arg_height != NULL) {
	int tmp = 0;

	sscanf(SdlTkX.arg_height, "%d", &tmp);
	if (tmp > 0) {
	    height = tmp;
	}
    }
    if ((width <= 0) || (height <= 0)) {
#ifdef ANDROID
	width = 200;
	height = 200;
#else
	width = 1024;
	height = 768;
#endif
    }
    if (SdlTkX.arg_rootwidth != NULL) {
	int tmp = 0;

	sscanf(SdlTkX.arg_rootwidth, "%d", &tmp);
	if (tmp >0 ) {
	    *root_width = tmp;
	}
    }
    if (SdlTkX.arg_rootheight != NULL) {
	int tmp = 0;

	sscanf(SdlTkX.arg_rootheight, "%d", &tmp);
	if (tmp > 0) {
	    *root_height = tmp;
	}
    }
    if ((*root_width <= 0) || (*root_height <= 0)) {
	*root_width = *root_height = 0;
    }
#ifndef ANDROID
    if (SdlTkX.arg_nogl) {
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    } else {
#ifndef _WIN32
	videoFlags |= SDL_WINDOW_OPENGL;
#endif
    }
#endif
#ifdef SDL_HINT_VIDEO_ALLOW_SCREENSAVER
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
#endif
#ifdef SDL_HINT_RENDER_SCALE_QUALITY
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
#endif
    SDL_GetDesktopDisplayMode(0, &info);
    pfmt = SDL_AllocFormat(info.format);
    if ((info.w > 0) && (info.h > 0)) {
	if (videoFlags & SDL_WINDOW_FULLSCREEN) {
	    width = info.w;
	    height = info.h;
	}
	if (width > info.w) {
	    width = info.w;
	}
	if (height > info.h) {
	    height = info.h;
	}
	if (width <= 0) {
	    width = info.w;
	}
	if (height <= 0) {
	    height = info.h;
	}
    }
    if (SdlTkX.arg_resizable) {
	if (min_w > width) {
	    min_w = width;
	}
	if (min_h > height) {
	    min_h = height;
	}
    }
#ifndef ANDROID
retry:
#endif
    SdlTkX.sdlscreen = SDL_CreateWindow("SDLWISH", SDL_WINDOWPOS_UNDEFINED,
				       SDL_WINDOWPOS_UNDEFINED, width,
				       height, videoFlags);
    if (SdlTkX.sdlscreen == NULL) {
#ifdef ANDROID
	__android_log_print(ANDROID_LOG_ERROR, "libtk",
			    "Couldn't create SDL Window : %s", SDL_GetError());
	goto fatal;
#else
	SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
			"Couldn't create SDL window: %s", SDL_GetError());
	if (SdlTkX.arg_nogl) {
	    /* no retry possible */
	    goto fatal;
	}
	SdlTkX.arg_nogl = 1;
	videoFlags &= ~SDL_WINDOW_OPENGL;
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
	goto retry;
#endif
    }
#ifndef ANDROID
    SDL_SetWindowMinimumSize(SdlTkX.sdlscreen, min_w, min_h);
#endif
    SDL_GetWindowSize(SdlTkX.sdlscreen, &width, &height);

    fmt = SDL_GetWindowPixelFormat(SdlTkX.sdlscreen);
    if (fmt == SDL_PIXELFORMAT_UNKNOWN) {
	/*
	 * This can happen with the Wayland video driver,
	 * thus try to go on with 24 bit RGB.
	 */
	fmt = SDL_PIXELFORMAT_RGB888;
    }
    pfmt = SDL_AllocFormat(fmt);

    SdlTkX.sdlsurf = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height,
					  pfmt->BitsPerPixel,
					  pfmt->Rmask, pfmt->Gmask,
					  pfmt->Bmask, pfmt->Amask);
    if (SdlTkX.sdlsurf == NULL) {
#ifdef ANDROID
	__android_log_print(ANDROID_LOG_ERROR, "libtk",
			    "Couldn't create SDL RGB surface: %s",
			    SDL_GetError());
#else
	SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
			"Couldn't create SDL RGB surface: %s",
			SDL_GetError());
#endif
	goto fatal;
    } else {
#ifdef ANDROID
	Uint32 pixel =
	    SDL_MapRGB(SdlTkX.sdlsurf->format, 0x00, 0x00, 0x00);
#else
	Uint32 pixel =
	    SDL_MapRGB(SdlTkX.sdlsurf->format, 0x00, 0x4E, 0x78);
#endif

	SDL_FillRect(SdlTkX.sdlsurf, NULL, pixel);
    }
#ifndef ANDROID
    /* GL debug output? */
    if (!SdlTkX.arg_nogl && SdlTkX.arg_sdllog) {
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    }
#endif
    SdlTkX.sdlrend = SDL_CreateRenderer(SdlTkX.sdlscreen, -1, 0);
    if (SdlTkX.sdlrend == NULL) {
#ifdef ANDROID
	__android_log_print(ANDROID_LOG_ERROR, "libtk",
			    "Couldn't create SDL renderer: %s",
			    SDL_GetError());
#else
	SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
			"Couldn't create SDL renderer: %s",
			SDL_GetError());
#endif
	goto fatal;
    }
#ifndef ANDROID
    if (!SdlTkX.arg_nogl) {
	/* check for OpenGL >= 2.x, otherwise fall back to SW renderer */
	int glvernum = -1;
	int hasFBO = 0;
	SDL_GLContext ctx;

#ifndef _WIN32
ctxRetry:
#endif
	ctx = SDL_GL_CreateContext(SdlTkX.sdlscreen);
	if (ctx != NULL) {
	    const char *glver = NULL;
#ifdef _WIN32
	    const char *APIENTRY (*glgs)(int n);
#else
	    const char *(*glgs)(int n);
#endif

	    glgs = SDL_GL_GetProcAddress("glGetString");
	    if (glgs != NULL) {
		glver = glgs(GL_VERSION);
	    }
	    if (glver != NULL) {
		SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
			     "GL version: %s", glver);
		sscanf(glver, "%d", &glvernum);
	    }
	    hasFBO = SDL_GL_ExtensionSupported("GL_EXT_framebuffer_object");
	    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
			 "GL_EXT_framebuffer_object%savailable",
			hasFBO ? " " : " not ");
	    SDL_GL_DeleteContext(ctx);
	}
#ifndef _WIN32
	else {
	    /* No GL context created, maybe try again with version 1.4 */
	    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &glvernum);
	    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
			 "GL version requested was %d.x", glvernum);
	    if (glvernum > 1) {
		SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
			     "retry with GL version 1.4");
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 4);
		glvernum = -1;
		goto ctxRetry;
	    }
	}
	if ((glvernum >= 0) && (glvernum < 1))
#else
	/* _WIN32 GL 1.4 on Intel GMA is broken, so we want 2.x at least */
	if ((glvernum >= 0) && (glvernum < 2))
#endif
	{
	    SDL_DestroyRenderer(SdlTkX.sdlrend);
	    SdlTkX.sdlrend = NULL;
	    SDL_FreeSurface(SdlTkX.sdlsurf);
	    SdlTkX.sdlsurf = NULL;
	    SDL_DestroyWindow(SdlTkX.sdlscreen);
	    SdlTkX.sdlscreen = NULL;
	    SdlTkX.arg_nogl = 1;
	    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
	    goto retry;
	}
#ifdef _WIN32
	SdlTkX.arg_nogl = (glvernum < 2) || !hasFBO;
#else
	SdlTkX.arg_nogl = (glvernum < 1) || !hasFBO;
#endif
    }
    if (pfmt->BitsPerPixel == 15) {
	tfmt = SDL_PIXELFORMAT_RGB555;
    } else if (pfmt->BitsPerPixel == 16) {
	tfmt = SDL_PIXELFORMAT_RGB565;
    } else if (pfmt->BitsPerPixel == 24) {
	if (pfmt->BytesPerPixel == 3) {
	    tfmt = SDL_PIXELFORMAT_RGB24;
	}
    } else if (pfmt->BitsPerPixel < 15) {
	SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
			"Unsupported pixel format (%d bpp)",
			pfmt->BitsPerPixel);
	goto fatal;
    }
#endif
    SdlTkX.sdltex = SDL_CreateTexture(SdlTkX.sdlrend,
#ifdef ANDROID
				      SDL_PIXELFORMAT_RGB888,
#else
				      tfmt,
#endif
				      SDL_TEXTUREACCESS_STREAMING,
				      width, height);
    if (SdlTkX.sdltex == NULL) {
#ifdef ANDROID
	__android_log_print(ANDROID_LOG_ERROR, "libtk",
			    "Couldn't create SDL texture: %s",
			    SDL_GetError());
#else
	SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
			"Couldn't create SDL texture: %s",
			SDL_GetError());
#endif
	goto fatal;
    }

#if defined(SDL_RENDERER_HAS_TARGET_3D) && !defined(ANDROID)
    /* Probe for 3d canvas if we can create FBO textures */
    if (!SdlTkX.arg_nogl) {
	SDL_Texture *tex;

	tex = SDL_CreateTexture(SdlTkX.sdlrend, SDL_PIXELFORMAT_ABGR8888,
				SDL_TEXTUREACCESS_TARGET_3D, 64, 64);
	if (tex == NULL) {
	    SdlTkX.arg_nogl = 1;
	    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"No support for FBOs: %s", SDL_GetError());
	} else {
	    SDL_DestroyTexture(tex);
	}
    }
#endif

    /* From win/tkWinX.c TkpOpenDisplay */
    display = (Display *) ckalloc(sizeof (Display));
    memset(display, 0, sizeof (Display));

    display->display_name = NULL;

    display->cursor_font = 1;
    display->nscreens = 1;
    display->request = 1;

    screen = (Screen *) ckalloc(sizeof (Screen));
    memset(screen, 0, sizeof (Screen));
    screen->display = display;

    screen->white_pixel = SDL_MapRGB(pfmt, 255, 255, 255);
    screen->black_pixel = SDL_MapRGB(pfmt, 0, 0, 0);
    screen->cmap = None;

    display->screens = screen;
    display->nscreens = 1;
    display->default_screen = 0;
    display->vendor = "unknown vendor";
    display->proto_major_version = 11;
    display->proto_minor_version = 6;
    display->release = 0;

    /* TkWinDisplayChanged */
    screen->width = width;
    screen->height = height;
    xdpi = SdlTkX.arg_xdpi;
    ydpi = SdlTkX.arg_ydpi;
    if (xdpi == 0) {
	xdpi = ydpi;
    }
    if (ydpi == 0) {
	ydpi = xdpi;
    }
#if defined(ANDROID) && defined(SDL_HAS_GETWINDOWDPI)
    if (xdpi == 0) {
	SDL_GetWindowDPI(SdlTkX.sdlscreen, &xdpi, &ydpi);
    }
#endif
    if (xdpi && ydpi) {
	int dpi = (ydpi < xdpi) ? ydpi : xdpi;
	extern int ttkMinThumbSize;
	extern char ttkDefScrollbarWidth[];
	extern char tkDefScrollbarWidth[];

	screen->mwidth = (254 * screen->width) / xdpi;
	screen->mwidth /= 10;
	screen->mheight = (254 * screen->height) / ydpi;
	screen->mheight /= 10;
	if (dpi < 140) {
	    /* keep X11 defaults */
	} else if (dpi < 190) {
	    SdlTkX.dec_frame_width = 8;
	    SdlTkX.dec_title_height = 30;
	    SdlTkX.dec_font_size = 14;
	    SdlTkX.dec_line_width = 3;
	    SdlTkX.nearby_pixels = 12;
	} else if (dpi < 240) {
	    SdlTkX.dec_frame_width = 12;
	    SdlTkX.dec_title_height = 38;
	    SdlTkX.dec_font_size = 18;
	    SdlTkX.dec_line_width = 4;
	    SdlTkX.nearby_pixels = 15;
	} else if (dpi < 320) {
	    SdlTkX.dec_frame_width = 16;
	    SdlTkX.dec_title_height = 46;
	    SdlTkX.dec_font_size = 24;
	    SdlTkX.dec_line_width = 5;
	    SdlTkX.nearby_pixels = 20;
	} else if (dpi < 420) {
	    SdlTkX.dec_frame_width = 20;
	    SdlTkX.dec_title_height = 60;
	    SdlTkX.dec_font_size = 32;
	    SdlTkX.dec_line_width = 7;
	    SdlTkX.nearby_pixels = 27;
	} else {
	    SdlTkX.dec_frame_width = 26;
	    SdlTkX.dec_title_height = 78;
	    SdlTkX.dec_font_size = 40;
	    SdlTkX.dec_line_width = 9;
	    SdlTkX.nearby_pixels = 35;
	}
	if (dpi > 140) {
	    int dsw;

	    ttkMinThumbSize = (20 * 100 * dpi) / 14000;
	    dsw = (19 * 100 * dpi) / 14000;
	    sprintf(ttkDefScrollbarWidth, "%d", dsw);
	    dsw = (17 * 100 * dpi) / 14000;
	    sprintf(tkDefScrollbarWidth, "%d", dsw);
	}
    } else {
#ifdef ANDROID
	screen->mwidth = (screen->width * 254 + 360) / 1440;
	screen->mheight = (screen->height * 254 + 360) / 1440;
	SdlTkX.dec_frame_width = 8;
	SdlTkX.dec_title_height = 30;
	SdlTkX.dec_font_size = 14;
	SdlTkX.dec_line_width = 3;
#else
	screen->mwidth = (screen->width * 254 + 360) / 720; /* from Mac Tk */
	screen->mheight = (screen->height * 254 + 360) / 720;
#endif
    }

    screen->root_depth = pfmt->BitsPerPixel;

    screen->root_visual = (Visual *) ckalloc(sizeof (Visual));
    memset(screen->root_visual, 0, sizeof (Visual));
    screen->root_visual->visualid = 0;

    if (pfmt->palette != NULL) {
	screen->root_visual->map_entries = pfmt->palette->ncolors;
	screen->root_visual->class = PseudoColor;
	screen->root_visual->red_mask = 0x0;
	screen->root_visual->green_mask = 0x0;
	screen->root_visual->blue_mask = 0x0;
    } else if (screen->root_depth == 4) {
	screen->root_visual->class = StaticColor;
	screen->root_visual->map_entries = 16;
    } else if (screen->root_depth == 8) {
	screen->root_visual->class = StaticColor;
	screen->root_visual->map_entries = 256;
    } else if (screen->root_depth == 12) {
	screen->root_visual->class = TrueColor;
	screen->root_visual->map_entries = 32;
	screen->root_visual->red_mask = 0xf0;
	screen->root_visual->green_mask = 0xf000;
	screen->root_visual->blue_mask = 0xf00000;
    } else if (screen->root_depth == 15) {
	screen->root_visual->class = TrueColor;
	screen->root_visual->map_entries = 64;
	screen->root_visual->red_mask = pfmt->Rmask;
	screen->root_visual->green_mask = pfmt->Gmask;
	screen->root_visual->blue_mask = pfmt->Bmask;
    } else if (screen->root_depth == 16) {
	screen->root_visual->class = TrueColor;
	screen->root_visual->map_entries = 64;
	screen->root_visual->red_mask = pfmt->Rmask;
	screen->root_visual->green_mask = pfmt->Gmask;
	screen->root_visual->blue_mask = pfmt->Bmask;
    } else if (screen->root_depth >= 24) {
	screen->root_visual->class = TrueColor;
	screen->root_visual->map_entries = 256;
	if (pfmt->BytesPerPixel == 3) {
	    /* Seems to help with DirectFB! */
	    screen->root_visual->blue_mask = pfmt->Rmask;
	    screen->root_visual->green_mask = pfmt->Gmask;
	    screen->root_visual->red_mask = pfmt->Bmask;
	} else {
	    screen->root_visual->red_mask = pfmt->Rmask;
	    screen->root_visual->green_mask = pfmt->Gmask;
	    screen->root_visual->blue_mask = pfmt->Bmask;
	}
    }
    screen->root_visual->bits_per_rgb = pfmt->BitsPerPixel;

    screen->cmap = XCreateColormap(display, None, screen->root_visual,
				   AllocNone);

    /* Create the root (desktop) window */
    _w = (_Window *) ckalloc(sizeof (_Window));
    memset(_w, 0, sizeof (_Window));
    _w->type = DT_WINDOW;
    _w->display = display;
    _w->format = SdlTkPixelFormat(SdlTkX.sdlsurf);
    _w->atts.x = 0;
    _w->atts.y = 0;
    SDL_GetWindowSize(SdlTkX.sdlscreen, &_w->atts.width, &_w->atts.height);
    _w->parentWidth = _w->atts.width;
    _w->parentHeight = _w->atts.height;
    _w->atts.border_width = 0;
    _w->atts.map_state = IsViewable;
    _w->visRgn = SdlTkRgnPoolGet();
    _w->visRgnInParent = SdlTkRgnPoolGet();
    _w->clazz = InputOutput;

    screen->root = (Window) _w;
    screen->display = display;
    values.graphics_exposures = False;
    values.foreground = screen->black_pixel;
    values.background = screen->white_pixel;
    screen->default_gc =
	XCreateGC(display, screen->root,
		  GCGraphicsExposures|GCForeground|GCBackground,
		  &values);

    /* Nasty globals */
    SdlTkX.display = display;
    SdlTkX.screen = screen;

    /* See TkpDoOneEvent */
    SDL_VERSION(&wminfo.version);
#ifdef _WIN32
    display->fd = INVALID_HANDLE_VALUE;
#else
    display->fd = -1;
#endif
    display->ext_number = -1;
    SDL_EventState(SDL_JOYDEVICEADDED, SDL_ENABLE);
    SDL_EventState(SDL_JOYDEVICEREMOVED, SDL_ENABLE);
    SDL_EventState(SDL_JOYBALLMOTION, SDL_ENABLE);
    SDL_EventState(SDL_JOYHATMOTION, SDL_ENABLE);
    SDL_EventState(SDL_JOYBUTTONDOWN, SDL_ENABLE);
    SDL_EventState(SDL_JOYBUTTONUP, SDL_ENABLE);
    SDL_EventState(SDL_JOYAXISMOTION, SDL_ENABLE);
#ifdef ANDROID
    SDL_EventState(SDL_APP_LOWMEMORY, SDL_ENABLE);
    SDL_EventState(SDL_APP_TERMINATING, SDL_ENABLE);
    SDL_EventState(SDL_APP_WILLENTERBACKGROUND, SDL_ENABLE);
    SDL_EventState(SDL_APP_DIDENTERBACKGROUND, SDL_ENABLE);
    SDL_EventState(SDL_APP_WILLENTERFOREGROUND, SDL_ENABLE);
    SDL_EventState(SDL_APP_DIDENTERFOREGROUND, SDL_ENABLE);
    SDL_EventState(SDL_FINGERDOWN, SDL_ENABLE);
    SDL_EventState(SDL_FINGERUP, SDL_ENABLE);
    SDL_EventState(SDL_FINGERMOTION, SDL_ENABLE);
    SDL_JoystickOpen(0);	/* should pick accelerometer */
    SDL_JoystickUpdate();
#else
    /*
     * Try loading and setting BMP icon on SDL window.
     */
    if (SdlTkX.arg_icon != NULL) {
	Tcl_Channel chan;
	SDL_RWops rwops;
	SDL_Surface *icon = NULL;

	chan = Tcl_OpenFileChannel(NULL, SdlTkX.arg_icon, "r", 0666);
	if (chan != NULL) {
	    rwops.size = RWIconSize;
	    rwops.seek = RWIconSeek;
	    rwops.read = RWIconRead;
	    rwops.write = RWIconWrite;
	    rwops.close = RWIconClose;
	    rwops.type = SDL_RWOPS_UNKNOWN;
	    rwops.hidden.unknown.data1 = chan;
	    rwops.hidden.unknown.data2 = NULL;
	    icon = SDL_LoadBMP_RW(&rwops, SDL_TRUE);
	}
	if (icon != NULL) {
	    SDL_SetWindowIcon(SdlTkX.sdlscreen, icon);
	    SDL_FreeSurface(icon);
	} else {
	    SdlTkX.arg_icon = NULL;
	}
    }
    if (SDL_GetWindowWMInfo(SdlTkX.sdlscreen, &wminfo)) {
#ifdef _WIN32
	if (wminfo.subsystem == SDL_SYSWM_WINDOWS) {
	    HWND hwnd = wminfo.info.win.window;
	    HICON hicon;

	    if (SdlTkX.arg_icon == NULL) {
		hicon = LoadIconA(GetModuleHandle(NULL), "tk");
		SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM) hicon);
		SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM) hicon);
	    }
	}
#else
	if (wminfo.subsystem != SDL_SYSWM_X11) {
	    SdlTkX.sdlfocus = 1;
#ifdef linux
	    /* Wayland? Try to load libGL.so for 3D canvas. */
	    if (!SdlTkX.arg_nogl) {
		dlopen("libGL.so.1", RTLD_NOW | RTLD_GLOBAL);
	    }
#endif
	}
#endif
    } else {
	SdlTkX.sdlfocus = 1;
    }
#endif
    SdlTkSetCursor(None);
#ifndef ANDROID
    if (SdlTkX.arg_opacity > 0) {
	double d = SdlTkX.arg_opacity / 100.0;

	SDL_SetWindowOpacity(SdlTkX.sdlscreen, d);
    }
#endif

    /* Pre-allocate some events */
    display->head = display->tail = NULL;
    display->qfree = NULL;
    display->qlen = display->qlenmax = 0;
    display->nqtotal = 0;
    for (i = 0; i < 128; i++) {
	_XSQEvent *qevent = (_XSQEvent *) ckalloc(sizeof (_XSQEvent));

	memset(qevent, 0, sizeof (_XSQEvent));
	qevent->next = display->qfree;
	display->qfree = qevent;
	display->nqtotal++;
    }

    SdlTkX.draw_later &= ~(SDLTKX_SCALED | SDLTKX_RENDCLR);
#ifdef ANDROID
    SdlTkX.draw_later |= SDLTKX_DRAW | SDLTKX_DRAWALL;
#endif
    SdlTkX.scale = SdlTkX.scale_min = 1.0f;
    SdlTkX.outrect = NULL;
    SdlTkX.viewport.x = 0;
    SdlTkX.viewport.y = 0;
    SdlTkX.viewport.w = SdlTkX.sdlsurf->w;
    SdlTkX.viewport.h = SdlTkX.sdlsurf->h;

    /* Inflate event queue mutex */
    Tcl_MutexLock((Tcl_Mutex *) &display->qlock);
    Tcl_MutexUnlock((Tcl_Mutex *) &display->qlock);

    SdlTkX.display = display;

    SDL_EnableScreenSaver();

    /* Some well known atoms */
    SdlTkX.mwm_atom = XInternAtom(NULL, "_MOTIF_WM_HINTS", False);
    SdlTkX.nwmn_atom = XInternAtom(NULL, "_NET_WM_NAME", False);
    SdlTkX.nwms_atom = XInternAtom(NULL, "_NET_WM_STATE", False);
    SdlTkX.nwmsf_atom = XInternAtom(NULL, "_NET_WM_STATE_FULLSCREEN", False);
    SdlTkX.clipboard_atom = XInternAtom(NULL, "CLIPBOARD", False);
    SdlTkX.comm_atom = XInternAtom(NULL, "Comm", False);
    SdlTkX.interp_atom = XInternAtom(NULL, "InterpRegistry", False);
    SdlTkX.tkapp_atom = XInternAtom(NULL, "TK_APPLICATION", False);
    SdlTkX.wm_prot_atom = XInternAtom(NULL, "WM_PROTOCOLS", False);
    SdlTkX.wm_dele_atom = XInternAtom(NULL, "WM_DELETE_WINDOW", False);

    /* Pre-allocate some _Window structs */
    for (i = 0; i < 128; i++) {
	_w = (_Window *) ckalloc(sizeof (_Window));
	memset(_w, 0, sizeof (_Window));
	if (SdlTkX.wtail == NULL) {
	    SdlTkX.wtail = SdlTkX.wfree = _w;
	} else {
	    SdlTkX.wtail->next = _w;
	    SdlTkX.wtail = _w;
	}
	SdlTkX.nwtotal++;
	SdlTkX.nwfree++;
    }

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * EventThread
 *
 *	This function handles SDL events and carries out screen
 *	updates. It dispatches X events to the various Display
 *	structures.
 *
 *----------------------------------------------------------------------
 */

static Tcl_ThreadCreateType
EventThread(ClientData clientData)
{
    SDL_Event sdl_event;
    XEvent event;
    SDL_TimerID timerId;
    int skipRefresh = 0, overrun, initSuccess;
#ifndef ANDROID
    /* Key repeat handling for Wayland. */
    SDL_Event key_event, txt_event;
    int key_rpt_state = 0, key_rpt_time = 0;
    extern int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode,
				   Uint16 rate, Uint16 delay);
    extern int SDL_SendKeyboardText(const char *text);
#endif
    struct EventThreadStartup *evs = (struct EventThreadStartup *) clientData;

    EVLOG("EventThread start");
#ifdef ANDROID
    Android_JNI_SetupThread();
#endif
    SdlTkLock(NULL);
    initSuccess = PerformSDLInit(evs->root_width, evs->root_height);
    evs->init_done = 1;
    Tcl_ConditionNotify(&xlib_cond);
    if (!initSuccess) {
	SdlTkUnlock(NULL);
	goto eventThreadEnd;
    }
    evs = NULL;		/* just in case */

    SDL_SetRenderTarget(SdlTkX.sdlrend, NULL);
#ifdef ANDROID
    SDL_GL_SwapWindow(SdlTkX.sdlscreen);
    SdlTkX.gl_context = SDL_GL_GetCurrentContext();
#else
    SDL_UpdateTexture(SdlTkX.sdltex, NULL, SdlTkX.sdlsurf->pixels,
		      SdlTkX.sdlsurf->pitch);
    SDL_RenderCopy(SdlTkX.sdlrend, SdlTkX.sdltex, NULL, NULL);
#endif
    SdlTkUnlock(NULL);

    timerId = SDL_AddTimer(1000 / SDLTK_FRAMERATE, TimerCallback,
			   (void *) &SdlTkX.time_count);
    EVLOG("EventThread enter loop");
    /*
     * Add all pending SDL events to the X event queues and
     * deal with screen refresh.
     */
    while (1) {
	_XSQEvent *qevent;

	/* Enable timer messages. */
	timer_enabled = !SdlTkX.in_background;
	if (!SDL_WaitEvent(&sdl_event)) {
	    break;
	}
	if (SdlTkX.sdlscreen == NULL) {
	    break;
	}
	memset(&event, 0, sizeof (event));
	SdlTkLock(NULL);
	if ((sdl_event.type == SDL_USEREVENT) &&
	    (sdl_event.user.data1 == TimerCallback) &&
	    (sdl_event.user.data2 != NULL)) {
	    /* Disable timer messages. */
	    timer_enabled = 0;
	    if (!skipRefresh) {
		SdlTkScreenRefresh();
	    }
	    overrun = (SdlTkX.time_count - sdl_event.user.code) > 0;
	    skipRefresh = !skipRefresh && overrun;
	    /* Mark event to be skipped in SdlTkTranslateEvent() */
	    sdl_event.type = SDL_USEREVENT + 0x1000;
#ifndef ANDROID
	    /* Key repeat handling for Wayland. */
	    if (key_rpt_state && (SdlTkX.time_count - key_rpt_time >= 0)) {
		if (key_event.key.rate) {
		    key_rpt_time = SdlTkX.time_count + 1000/key_event.key.rate;
		    if (key_rpt_state > 1) {
			SDL_SendKeyboardText(txt_event.text.text);
		    } else {
			SDL_SendKeyboardKey(SDL_PRESSED,
					    key_event.key.keysym.scancode,
					    key_event.key.rate, 0);
		    }
		} else {
		    key_rpt_state = 0;
		}
	    }
#endif
	}
#ifndef ANDROID
	/* Key repeat handling for Wayland. */
	if ((sdl_event.type == SDL_KEYDOWN) && sdl_event.key.rate &&
	    sdl_event.key.delay && !sdl_event.key.repeat) {
	    key_rpt_state = 1;
	    key_event = sdl_event;
	    key_rpt_time = SdlTkX.time_count + key_event.key.delay;
	    if (SDL_PeepEvents(&txt_event, 1, SDL_PEEKEVENT,
			       SDL_TEXTINPUT, SDL_TEXTINPUT) == 1) {
		key_rpt_state = 2;
	    }
	} else if (key_rpt_state && (sdl_event.type == SDL_KEYUP) &&
		   sdl_event.key.rate && sdl_event.key.delay &&
		   !sdl_event.key.repeat) {
	    key_rpt_state = 0;
	}
#endif
	if ((sdl_event.type == SDL_USEREVENT) &&
	    (sdl_event.user.data1 == HandlePanZoom) &&
	    (sdl_event.user.data2 != NULL)) {
	    HandlePanZoom((struct PanZoomRequest *) sdl_event.user.data2);
	    /* Mark event to be skipped in SdlTkTranslateEvent() */
	    sdl_event.type = SDL_USEREVENT + 0x1001;
	}
	if ((sdl_event.type == SDL_USEREVENT) &&
	    (sdl_event.user.data1 == HandleRootSize) &&
	    (sdl_event.user.data2 != NULL)) {
	    HandleRootSize((struct RootSizeRequest *) sdl_event.user.data2);
	    /* Mark event to be skipped in SdlTkTranslateEvent() */
	    sdl_event.type = SDL_USEREVENT + 0x1002;
	}
#ifndef ANDROID
	if ((sdl_event.type == SDL_USEREVENT) &&
	    (sdl_event.user.data1 == HandleWindowFlags) &&
	    (sdl_event.user.data2 != NULL)) {
	    HandleWindowFlags((struct WindowFlagsRequest *) sdl_event.user.data2);
	    /* Mark event to be skipped in SdlTkTranslateEvent() */
	    sdl_event.type = SDL_USEREVENT + 0x1003;
	}
#endif
	if (SdlTkTranslateEvent(&sdl_event, &event, SdlTkX.time_count)) {
	    SdlTkQueueEvent(&event);
	}
	SdlTkUnlock(NULL);
	/* Remove left over events from SdlTkX.display */
	Tcl_MutexLock((Tcl_Mutex *) &SdlTkX.display->qlock);
	qevent = SdlTkX.display->head;
	while (qevent != NULL) {
	    _XSQEvent *next = qevent->next;

	    EVLOG("RemoveEvent %d %p", qevent->event.xany.type,
		  (void *) qevent->event.xany.window);
	    qevent->next = SdlTkX.display->qfree;
	    SdlTkX.display->qfree = qevent;
	    qevent = next;
	}
	SdlTkX.display->head = SdlTkX.display->tail = NULL;
	SdlTkX.display->qlen = 0;
	Tcl_MutexUnlock((Tcl_Mutex *) &SdlTkX.display->qlock);
    }
    SDL_RemoveTimer(timerId);
    /* tear down font manager/engine */
    SdlTkGfxDeinitFC();
eventThreadEnd:
    TCL_THREAD_CREATE_RETURN;
}

static void
EventThreadExitHandler(ClientData clientData)
{
    int state;
    Tcl_ThreadId event_tid = SdlTkX.event_tid;

    if (event_tid) {
	SdlTkX.event_tid = NULL;
	SdlTkX.sdlscreen = NULL;
	Tcl_JoinThread(event_tid, &state);
    }
}

static void
OpenVeryFirstDisplay(int *root_width, int *root_height)
{
    struct EventThreadStartup evs;

    /*
     * Run thread to startup SDL, to collect SDL events,
     * and to perform screen updates.
     */
    evs.init_done = 0;
    evs.root_width = root_width;
    evs.root_height = root_height;
    Tcl_CreateThread(&SdlTkX.event_tid, EventThread, &evs,
		     TCL_THREAD_STACK_DEFAULT, TCL_THREAD_NOFLAGS);
    while (!evs.init_done) {
	SdlTkWaitLock();
    }
    Tcl_CreateExitHandler(EventThreadExitHandler, NULL);
}

Display *
XOpenDisplay(_Xconst char *display_name)
{
    Display *display;
    Screen *screen;
    int i, root_width = 0, root_height = 0;

    SdlTkLock(NULL);

    if (SdlTkX.display == NULL) {
	OpenVeryFirstDisplay(&root_width, &root_height);
	if (SdlTkX.display != NULL) {
	    /* Set title for window */
	    SDL_SetWindowTitle(SdlTkX.sdlscreen, display_name);
	}
    }

    if (SdlTkX.display == NULL) {
	SdlTkUnlock(NULL);
	return NULL;
    }

    display = (Display *) ckalloc(sizeof (Display));
    memset(display, 0, sizeof (Display));

    display->display_name = (char *) ckalloc(strlen(display_name)+1);
    strcpy(display->display_name, display_name);

    display->cursor_font = 1;
    display->nscreens = 1;
    display->request = 1;
    display->qlen = 0;

    screen = (Screen *) ckalloc(sizeof (Screen));
    *screen = *SdlTkX.screen;
    screen->display = display;

    display->screens = screen;
    display->nscreens = 1;
    display->default_screen = 0;
    display->vendor = "unknown vendor";
    display->proto_major_version = 11;
    display->proto_minor_version = 6;
    display->release = 0;

#ifdef _WIN32
    display->fd = (void *) CreateEvent(NULL, 0, 0, NULL);
#else
#ifdef linux
    /*
     * Hacked call to eventfd2() to enable build and run
     * with/on old platforms. Intel and ARM CPUs for now.
     */
#if defined(__arm__) || defined(__aarch64__) || defined(ANDROID)
    display->fd = eventfd(0, 0);
#elif defined(__i386__)
    display->fd = syscall(328, 0, 0);
#elif defined(__x86_64__)
    display->fd = syscall(290, 0, 0);
#else
    display->fd = -1;
#endif
    if (display->fd != -1) {
	if ((fcntl(display->fd, F_SETFD, FD_CLOEXEC) < 0) ||
	    (fcntl(display->fd, F_SETFL, O_NONBLOCK) < 0)) {
	    close(display->fd);
	    display->fd = -1;
	} else {
	    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "using eventfd %d",
			   display->fd);
	}
	display->ext_number = -1;
    }
#else
    display->fd = -1;
#endif
    if (display->fd == -1) {
	int pfd[2] = { -1, -1 };

	display->fd = pipe(pfd);
	fcntl(pfd[0], F_SETFD, FD_CLOEXEC);
	fcntl(pfd[1], F_SETFD, FD_CLOEXEC);
	fcntl(pfd[0], F_SETFL, O_NONBLOCK);
	fcntl(pfd[1], F_SETFL, O_NONBLOCK);
	display->fd = pfd[0];
	display->ext_number = pfd[1];
    }
#endif

    /* Pre-allocate some events */
    display->head = display->tail = NULL;
    display->qfree = NULL;
    display->qlen = display->qlenmax = 0;
    display->nqtotal = 0;
    for (i = 0; i < 128; i++) {
	_XSQEvent *qevent = (_XSQEvent *) ckalloc(sizeof (_XSQEvent));

	memset(qevent, 0, sizeof (_XSQEvent));
	qevent->next = display->qfree;
	display->qfree = qevent;
	display->nqtotal++;
    }

    /* Inflate event queue mutex */
    Tcl_MutexLock((Tcl_Mutex *) &display->qlock);
    Tcl_MutexUnlock((Tcl_Mutex *) &display->qlock);

    /* Queue cloned display */
    display->next_display = SdlTkX.display->next_display;
    SdlTkX.display->next_display = display;

    i = ++num_displays;
    SdlTkUnlock(NULL);

#ifdef ANDROID
    /*
     * Enroll thread into Java VM. Cleanup is automatic at exit of thread.
     */
    Android_JNI_SetupThread();
#endif

    /* Wait for server grabs being released */
    SdlTkLock(display);
    if (i == 1) {
	/* First display, let refresh complete */
	SdlTkWaitVSync();
    }
    SdlTkUnlock(display);

    if ((root_width > 0) && (root_height > 0)) {
	if (root_width <= 0) {
	    root_width = screen->width;
	}
	if (root_height <= 0) {
	    root_height = screen->height;
	}
	SdlTkSetRootSize(root_width, root_height);
    }

    EVLOG("XOpenDisplay %p", display);
    return display;
}

void
XPutBackEvent(Display *display, XEvent *event)
{
}

int
XPutImage(Display *display, Drawable d, GC gc, XImage *image,
	  int src_x, int src_y, int dest_x, int dest_y,
	  unsigned int width, unsigned int height)
{
    TkpClipMask *clipPtr = (TkpClipMask *) gc->clip_mask;
    Region r = None;

    SdlTkLock(display);

    display->request++;

    if ((clipPtr != NULL) && (clipPtr->type == TKP_CLIP_REGION)) {
	r = (Region) clipPtr->value.region;
    }

    SdlTkGfxPutImage(d, r, image, src_x, src_y, dest_x, dest_y,
		     width, height, 0);

    if (IS_WINDOW(d)) {
	SdlTkScreenChanged();
	if (r != None) {
	    SdlTkDirtyRegion(d, r);
	} else {
	    SdlTkDirtyArea(d, dest_x, dest_y, width, height);
	}
    }

    SdlTkUnlock(display);

    return 0;
}

/* TkTreeCtrl loupe uses this */
void
XQueryColors(Display *display, Colormap colormap, XColor *defs_in_out,
	     int ncolors)
{
    int i;
    Uint32 rm, gm, bm;
    int rs, gs, bs;

    rm = SdlTkX.sdlsurf->format->Rmask;
    gm = SdlTkX.sdlsurf->format->Gmask;
    bm = SdlTkX.sdlsurf->format->Bmask;

    rs = SdlTkX.sdlsurf->format->Rshift;
    gs = SdlTkX.sdlsurf->format->Gshift;
    bs = SdlTkX.sdlsurf->format->Bshift;

    for (i = 0; i < ncolors; i++) {
	defs_in_out[i].red =
	    ((defs_in_out[i].pixel & rm) >> rs) / 255.0 * USHRT_MAX;
	defs_in_out[i].green =
	    ((defs_in_out[i].pixel & gm) >> gs) / 255.0 * USHRT_MAX;
	defs_in_out[i].blue =
	    ((defs_in_out[i].pixel & bm) >> bs) / 255.0 * USHRT_MAX;
    }
}

Bool
XQueryPointer(Display *display, Window w, Window *root_return,
	      Window *child_return, int *root_x_return,
	      int *root_y_return, int *win_x_return, int *win_y_return,
	      unsigned int *mask_return)
{
    int state, mask = 0;

    SdlTkLock(display);

    display->request++;

    state = SdlTkGetMouseState(root_x_return, root_y_return);

    /* CHECK THIS: win_x/y_return not used in Tk library */

    *win_x_return = *root_x_return;
    *win_y_return = *root_y_return;

    if (state & SDL_BUTTON(1)) {
	mask |= Button1Mask;
    }
    if (state & SDL_BUTTON(2)) {
	mask |= Button2Mask;
    }
    if (state & SDL_BUTTON(3)) {
	mask |= Button3Mask;
    }

    SdlTkUnlock(display);

    *mask_return = mask;

    return True;
}

int
XQueryTree(Display *display, Window w, Window *root_return,
	   Window *parent_return, Window **children_return,
	   unsigned int *nchildren_return)
{
    _Window *_w = (_Window *) w;
    _Window *child;
    int k, n = 0;

    SdlTkLock(display);

    display->request++;

    *root_return = SdlTkX.screen->root;
    *parent_return = (Window) _w->parent;

    if (_w->child == NULL) {
	*children_return = NULL;
	*nchildren_return = 0;
	goto done;
    }

    /* Count children */
    child = _w->child;
    while (child != NULL) {
	n++;
	child = child->next;
    }

    /* Make array of children */
    *children_return = (Window *) ckalloc(sizeof (Window) * n);
    k = n;
    child = _w->child;
    while (child != NULL) {
	(*children_return)[--k] = (Window) child;
	child = child->next;
    }
    *nchildren_return = n;

done:
    SdlTkUnlock(display);

    return 1;
}

/*
 * The XReconfigureWMWindow() function issues a ConfigureWindow request on the
 * specified top-level window. If the stacking mode is changed and the request
 * fails with a BadMatch error, the error is trapped by Xlib and a synthetic
 * ConfigureRequestEvent containing the same configuration parameters is sent
 * to the root of the specified window. Window managers may elect to receive
 * this event and treat it as a request to reconfigure the indicated window.
 * It returns a nonzero status if the request or event is successfully sent;
 * otherwise, it returns a zero status.
 */

Status
XReconfigureWMWindow(Display *display, Window w, int screen_number,
		     unsigned int mask, XWindowChanges *changes)
{
    _Window *_w = (_Window *) w;
    _Window *parent = _w->parent;
    _Window *sibling = NULL;

    SdlTkLock(display);

    display->request++;

    if (mask & CWStackMode) {

	SdlTkScreenChanged();

	/* Attempting to restack a wrapper? Restack decframe instead. */
	/* override_redirects won't have a decframe however */
	if ((parent != NULL) && (parent->dec != NULL)) {
	    _w = parent;
	    parent = parent->parent;
	}

	/* Stack above/below decframe of sibling if any */
	if (mask & CWSibling) {
	    sibling = (_Window *) changes->sibling;
	    if (sibling->parent->dec != NULL) {
		sibling = sibling->parent;
	    }
	}

	SdlTkRestackWindow(_w, sibling, changes->stack_mode);
	SdlTkRestackTransients(_w);
    }

    SdlTkUnlock(display);

    return 0;
}

#if 0
int
XRectInRegion(Region r, int x, int y, unsigned int width, unsigned int height)
{
    return 0;
}
#endif

void
XRefreshKeyboardMapping(XMappingEvent *event_map)
{
}

static int
SdlTkReparentWindow(Display *display, Window w, Window parent, int x, int y)
{
    _Window *_parent = (_Window *) parent;
    _Window *_w = (_Window *) w;
    _Window *wdec = NULL;
    XEvent event;

    if ((_w->display == NULL) || (_parent->display == NULL)) {
	return 0;
    }
    memset(&event, 0, sizeof (event));

    /* Remove from old parent */
    if (_w->parent != NULL) {
	if (_w->parent->dec != NULL) {
	    wdec = _w->parent;
	}
	SdlTkRemoveFromParent(_w);
    }

    /* Add to new parent */
    _w->parent = _parent;
    _w->next = _parent->child;
    _parent->child = _w;

    /* Update position */
    _w->atts.x = x;
    _w->atts.y = y;

    event.type = ReparentNotify;
    event.xreparent.serial = _w->display->request;
    event.xreparent.send_event = False;
    event.xreparent.display = _w->display;
    event.xreparent.event = w;
    event.xreparent.window = w;
    event.xreparent.parent = parent;
    event.xreparent.x = x;
    event.xreparent.y = y;
    event.xreparent.override_redirect = _w->atts.override_redirect;
    SdlTkQueueEvent(&event);

    if (_w->fullscreen && !_parent->fullscreen) {
	int xx, yy, ww, hh;

	_parent->atts_saved = _w->atts;
	xx = yy = 0;
	ww = SdlTkX.screen->width;
	hh = SdlTkX.screen->height;
	if (_parent->dec != NULL) {
	    xx -= SdlTkX.dec_frame_width;
	    yy -= SdlTkX.dec_title_height;
	    ww += SdlTkX.dec_frame_width * 2;
	    hh += SdlTkX.dec_title_height + SdlTkX.dec_frame_width;
	}
	SdlTkMoveResizeWindow(display, (Window) _parent, xx, yy, ww, hh);
	while (!IS_ROOT((Window) _parent)) {
	    _parent->fullscreen = 1;
	    _parent = _parent->parent;
	}
    }

    /* Destroy decorative frame */
    if ((wdec != NULL) && (wdec->child == NULL)) {
	SdlTkDestroyWindow(display, (Window) wdec);
    }

    return 0;
}

int
XReparentWindow(Display *display, Window w, Window parent, int x, int y)
{
    int ret;

    SdlTkLock(display);
    display->request++;
    ret = SdlTkReparentWindow(display, w, parent, x, y);
    SdlTkUnlock(display);
    return ret;
}

void
SdlTkResizeWindow(Display *display, Window w,
		  unsigned int width, unsigned int height)
{
    _Window *_w = (_Window *) w;
    int flags;

    if (_w->display == NULL) {
	return;
    }
    if (_w->fullscreen) {
	if (_w->atts.your_event_mask & StructureNotifyMask) {
	    SdlTkGenerateConfigureNotify(NULL, w);
	}
	return;
    }

    if ((int) width < 1) {
	width = 1;
    }
    if ((int) height < 1) {
	height = 1;
    }

    flags = VRC_CHANGED | VRC_DO_PARENT;

    /* If this window has a decorative frame, resize it */
    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
	_Window *wdec = _w->parent;

	wdec->atts.width = width + SdlTkX.dec_frame_width * 2;
	wdec->atts.height = height + SdlTkX.dec_title_height +
	    SdlTkX.dec_frame_width;

	/*
	 * A window's requested width/height are *inside* its borders,
	 * a child window is clipped within its parent's borders.
	 */

	wdec->parentWidth = wdec->atts.width + 2 * wdec->atts.border_width;
	wdec->parentHeight = wdec->atts.height + 2 * wdec->atts.border_width;
    }

    if ((width > _w->atts.width) || (height > _w->atts.height)) {
	flags |= VRC_EXPOSE;
    }
    _w->atts.width = width;
    _w->atts.height = height;

    /*
     * A window's requested width/height are *inside* its borders,
     * a child window is clipped within its parent's borders.
     */

    _w->parentWidth = width + 2 * _w->atts.border_width;
    _w->parentHeight = height + 2 * _w->atts.border_width;

    if (_w->atts.your_event_mask & StructureNotifyMask) {
	SdlTkGenerateConfigureNotify(NULL, w);
    }

    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
	SdlTkVisRgnChanged(_w->parent, flags, 0, 0);
    } else {
	SdlTkVisRgnChanged(_w, flags, 0, 0);
    }

    SdlTkScreenChanged();
}

void
XResizeWindow(Display *display, Window w,
	      unsigned int width, unsigned int height)
{
    SdlTkLock(display);
    display->request++;
    SdlTkResizeWindow(display, w, width, height);
    SdlTkUnlock(display);
}

Window
XRootWindow(Display *display, int screen_number)
{
    SdlTkLock(display);
    display->request++;
    SdlTkUnlock(display);
    return SdlTkX.screen->root;
}

void
XSelectInput(Display *display, Window w, long event_mask)
{
    _Window *_w = (_Window *) w;

    SdlTkLock(display);
    display->request++;
    _w->atts.your_event_mask = event_mask;
    SdlTkUnlock(display);
}

int
XSendEvent(Display *display, Window w, Bool propagate, long event_mask,
	   XEvent *event_send)
{
    XEvent event;
    int ret = 0;

    SdlTkLock(display);
    display->request++;
    event = *event_send;
    if ((event.xany.type == ClientMessage) && (w != None) &&
	(w != PointerRoot) && (w != InputFocus) &&
	(event.xclient.message_type == SdlTkX.nwms_atom) &&
	(event.xclient.data.l[1] == SdlTkX.nwmsf_atom)) {
	_Window *_w = (_Window *) event.xany.window;
	int fullscreen = event.xclient.data.l[0];
	int send_nwms = 0;
	_Window *_ww = _w;

	if ((_w == NULL) || (_w->display == NULL)) {
	    goto done;
	}
	if (fullscreen && !_w->fullscreen) {
	    int xx, yy, ww, hh;

	    _w->atts_saved = _w->atts;
	    xx = yy = 0;
	    ww = SdlTkX.screen->width;
	    hh = SdlTkX.screen->height;
	    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
		xx -= SdlTkX.dec_frame_width;
		yy -= SdlTkX.dec_title_height;
	    }
	    SdlTkMoveResizeWindow(display, (Window) _w, xx, yy, ww, hh);
	    while (!IS_ROOT((Window) _ww)) {
		_ww->fullscreen = 1;
		_ww = _ww->parent;
	    }
	    send_nwms = 1;
	} else if ((_w != NULL) && !fullscreen && _w->fullscreen) {
	    while (!IS_ROOT((Window) _ww)) {
		_ww->fullscreen = 0;
		_ww = _ww->parent;
	    }
	    SdlTkMoveResizeWindow(display, (Window) _w,
				  _w->atts_saved.x, _w->atts_saved.y,
				  _w->atts_saved.width, _w->atts_saved.height);
	    send_nwms = 1;
	}
	if (send_nwms) {
	    XPropertyEvent xproperty;

	    memset(&xproperty, 0, sizeof (xproperty));
	    xproperty.type = PropertyNotify;
	    xproperty.serial = _w->display->request;
	    xproperty.send_event = False;
	    xproperty.atom = SdlTkX.nwms_atom;
	    xproperty.display = _w->display;
	    xproperty.window = (Window) _w;
	    xproperty.state = PropertyNewValue;
	    xproperty.time = SdlTkX.time_count;
	    SdlTkQueueEvent((XEvent *) &xproperty);
	}
	ret = 1;
	goto done;
    }
    if (w == PointerRoot) {
	goto done;
    }
    if (w == InputFocus) {
	w = SdlTkX.focus_window;
    }
    if ((w != None) && (((_Window *) w)->display != NULL)) {
	event.xany.display = ((_Window *) w)->display;
	SdlTkQueueEvent(&event);
	ret = 1;
    }
done:
    SdlTkUnlock(display);
    return ret;
}

int
XSetCommand(Display *display, Window w, char **argv, int argc)
{
    return 0;
}

void
XSetBackground(Display *display, GC gc, unsigned long background)
{
    gc->background = background;
}

int
XSetClassHint(Display *display, Window w, XClassHint *class_hints)
{
    return 0;
}

void
XSetClipMask(Display *display, GC gc, Pixmap pixmap)
{
    if (pixmap == None) {
	if (gc->clip_mask) {
	    ckfree((char*) gc->clip_mask);
	    gc->clip_mask = None;
	}
	return;
    }

    if (gc->clip_mask == None) {
	gc->clip_mask = (Pixmap)ckalloc(sizeof (TkpClipMask));
    }
    ((TkpClipMask*)gc->clip_mask)->type = TKP_CLIP_PIXMAP;
    ((TkpClipMask*)gc->clip_mask)->value.pixmap = pixmap;
}

void
XSetStipple(Display *display, GC gc, Pixmap stipple)
{
    gc->stipple = stipple;
}

void
XSetFillStyle(Display *display, GC gc, int fill_style)
{
    gc->fill_style = fill_style;
}

void
XSetClipOrigin(Display *display, GC gc, int clip_x_origin, int clip_y_origin)
{
    gc->clip_x_origin = clip_x_origin;
    gc->clip_y_origin = clip_y_origin;
}

void
XSetDashes(Display *display, GC gc, int dash_offset, _Xconst char *dash_list,
	   int n)
{
    char *p = &gc->dashes;
    int i, nn = 0;

    if (n & 1) {
	nn = n;
    }
    gc->dash_offset = dash_offset;
    if (n + nn >= sizeof (gc->dash_array)) {
	if (nn) {
	    n = nn = sizeof (gc->dash_array) / 2;
	} else {
	    n = sizeof (gc->dash_array) - 1;
	    n &= ~1;
	}
    }
    i = 0;
    while (n-- > 0) {
	*p++ = dash_list[i++];
    }
    /*
     * XSetDashes() man page: "Specifying an odd-length list is
     * equivalent to specifying the same list concatenated with
     * itself to produce an even-length list."
     */
    i = 0;
    while (nn-- > 0) {
	*p++ = dash_list[i++];
    }
    /*
     * Mark end of list.
     */
    *p = 0;
}

XErrorHandler
XSetErrorHandler(XErrorHandler handler)
{
    return NULL;
}

void
XSetFont(Display *display, GC gc, Font font)
{
    gc->font = font;
}

void
XSetForeground(Display *display, GC gc, unsigned long foreground)
{
    gc->foreground = foreground;
}

void
XSetIconName(Display *display, Window w, _Xconst char *icon_name)
{
}

void
SdlTkSetInputFocus(Display *display, Window focus, int revert_to, Time time)
{
    _Window *_w;
    XEvent event;

    if ((focus != None) && (focus != PointerRoot)) {
	_w = (_Window *) focus;
	if (_w->display == NULL) {
	    return;
	}
    }
    if (SdlTkX.focus_window == focus) {
	return;
    }
    if ((SdlTkX.keyboard_window != NULL) &&
	(SdlTkX.keyboard_window->display != display)) {
	return;
    }

    memset(&event, 0, sizeof (event));

    if (SdlTkX.focus_window != None) {
	_w = (_Window *) SdlTkX.focus_window;
	_w->display->focus_window = SdlTkX.focus_window;
	event.type = FocusOut;
	event.xfocus.serial = _w->display->request;
	event.xfocus.send_event = False;
	event.xfocus.display = _w->display;
	event.xfocus.window = SdlTkX.focus_window;
	event.xfocus.mode = NotifyNormal;
	event.xfocus.detail = NotifyNonlinear;
	SdlTkQueueEvent(&event);
    }

    SdlTkX.focus_window = focus;
    if ((focus == None) || (focus == PointerRoot)) {
	_w = NULL;
    } else {
	_w = (_Window *) focus;
	_w->display->focus_window = focus;
    }
    if (_w == NULL) {
	SdlTkX.focus_window_not_override = None;
    } else if (!_w->atts.override_redirect) {
	SdlTkX.focus_window_not_override = focus;
    }

    if (SdlTkX.keyboard_window != NULL) {
	SdlTkX.keyboard_window = _w;
    }

    if ((focus != None) && (focus != PointerRoot)) {
	event.type = FocusIn;
	event.xfocus.serial = _w->display->request;
	event.xfocus.send_event = False;
	event.xfocus.display = _w->display;
	event.xfocus.window = focus;
	event.xfocus.mode = NotifyNormal;
	event.xfocus.detail = NotifyNonlinear;
	SdlTkQueueEvent(&event);
    }

    if ((_w != NULL) && (_w->parent != NULL) && (_w->parent->dec != NULL)) {
	SdlTkScreenChanged();
    }
}

void
XSetInputFocus(Display *display, Window focus, int revert_to, Time time)
{
    SdlTkLock(display);
    display->request++;
    if (SdlTkX.keyboard_window != NULL) {
	if (SdlTkX.keyboard_window->display != display) {
	    goto done;
	}
    }
    SdlTkSetInputFocus(display, focus, revert_to, time);
done:
    SdlTkUnlock(display);
}

void
XSetLineAttributes(Display *display, GC gc, unsigned int line_width,
		   int line_style, int cap_style, int join_style)
{
    gc->line_width = line_width;
    gc->line_style = line_style;
    gc->cap_style = cap_style;
    gc->join_style = join_style;
}

int
XSetRegion(Display *display, GC gc, Region r)
{
    TkpClipMask *clipPtr = (TkpClipMask *) gc->clip_mask;
    Region rgn;

    SdlTkLock(display);
    if (r == None) {
	if (clipPtr != NULL) {
	    if (clipPtr->type == TKP_CLIP_REGION) {
		SdlTkRgnPoolFree((Region) clipPtr->value.region);
	    }
	    ckfree((char*) clipPtr);
	    gc->clip_mask = None;
	}
	goto done;
    }

    if (clipPtr == NULL) {
	clipPtr = (TkpClipMask *) ckalloc(sizeof (TkpClipMask));
	clipPtr->type = TKP_CLIP_PIXMAP;
	clipPtr->value.region = None;
	gc->clip_mask = (Pixmap) clipPtr;
    }

    if (clipPtr->type == TKP_CLIP_REGION) {
	SdlTkRgnPoolFree((Region) clipPtr->value.region);
    }
    rgn = SdlTkRgnPoolGet();
    XUnionRegion(rgn, r, rgn);
    clipPtr->type = TKP_CLIP_REGION;
    clipPtr->value.region = (TkRegion) rgn;
done:
    SdlTkUnlock(display);
    return 1;
}

void
SdlTkSetSelectionOwner(Display *display, Atom selection, Window owner,
		      Time time)
{
    Window *current, clear = None;
    XEvent event;

    if (owner != None) {
	_Window *_w = (_Window *) owner;

	if (_w->display == NULL) {
	    return;
	}
    }
    memset(&event, 0, sizeof (event));
    if (selection == None) {
	/* called through SDL_CLIPBOARDUPDATE */
	if (SdlTkX.current_primary != None) {
	    clear = SdlTkX.current_primary;
	    SdlTkX.current_primary = None;
	    event.type = SelectionClear;
	    event.xselectionclear.serial =
		((_Window *) clear)->display->request;
	    event.xselectionclear.send_event = False;
	    event.xselectionclear.display = ((_Window *) clear)->display;
	    event.xselectionclear.window = clear;
	    event.xselectionclear.selection = XA_PRIMARY;
	    event.xselectionclear.time = time;
	    SdlTkQueueEvent(&event);
	}
	if ((SdlTkX.clipboard_atom != None) &&
	    (SdlTkX.current_clipboard != None)) {
	    current = &SdlTkX.current_clipboard;
	    selection = SdlTkX.clipboard_atom;
	    goto sendClr;
	}
	return;
    }
    if (selection == XA_PRIMARY) {
	current = &SdlTkX.current_primary;
    } else if (selection == SdlTkX.clipboard_atom) {
	current = &SdlTkX.current_clipboard;
    } else {
	return;
    }
    if ((owner == None) && (*current != None)) {
	SDL_SetClipboardText("");
    }
sendClr:
    clear = *current;
    *current = owner;
    if (clear != None) {
	event.type = SelectionClear;
	event.xselectionclear.serial = ((_Window *) clear)->display->request;
	event.xselectionclear.send_event = False;
	event.xselectionclear.display = ((_Window *) clear)->display;
	event.xselectionclear.window = clear;
	event.xselectionclear.selection = selection;
	event.xselectionclear.time = time;
	SdlTkQueueEvent(&event);
    }
}

void
XSetSelectionOwner(Display *display, Atom selection, Window owner, Time time)
{
    SdlTkLock(display);
    SdlTkSetSelectionOwner(display, selection, owner, time);
    display->request++;
    SdlTkUnlock(display);
}

int
XSetTransientForHint(Display *display, Window w, Window prop_window)
{
    _Window *_w, *_p, *_parent;
    int ret = 1;

    SdlTkLock(display);
    display->request++;

    _w = (_Window *) w;
    _p = (_Window *) prop_window;
    if (_w->display == NULL) {
	ret = 0;
	goto done;
    }
    if (_p != NULL) {
	_parent = _p->parent;
	while ((_parent != NULL) && !IS_ROOT(_parent)) {
	    _p = _parent;
	    _parent = _p->parent;
	}
	if ((_p != NULL) && (_p->dec != NULL)) {
	    _p = _p->child;
	}
    }
    _w->master = _p;
    if ((_p != NULL) && IS_ROOT(_p)) {
	_w->master = NULL;
	SdlTkMapWindow(display, w);
	SdlTkBringToFrontIfNeeded(_w);
	if (SdlTkX.keyboard_window == NULL) {
	    SdlTkSetInputFocus(SdlTkX.display,
			       (Window) SdlTkWrapperForWindow(_w),
			       RevertToParent, CurrentTime);
	    /* Frames need redrawing if the focus changed */
	    SdlTkScreenChanged();
	}
    }

    SdlTkUnlock(display);
done:
    return ret;
}

void
XSetTSOrigin(Display *display, GC gc, int x, int y)
{
    gc->ts_x_origin = x;
    gc->ts_y_origin = y;
}

void
XSetWindowBackground(Display *display, Window w,
		     unsigned long background_pixel)
{
    struct _Window *_w = (_Window *) w;

    SdlTkLock(display);
    display->request++;
    if (_w->display == NULL) {
	return;
    }
    _w->back_pixel_set = 1;
    _w->back_pixel = background_pixel;
    _w->back_pixmap = NULL;
    SdlTkUnlock(display);
}

void
XSetWindowBackgroundPixmap(Display *display, Window w,
			   Pixmap background_pixmap)
{
    struct _Window *_w = (_Window *) w;
    struct _Pixmap *_p = (_Pixmap *) background_pixmap;

    SdlTkLock(display);
    display->request++;
    if (_w->display == NULL) {
	return;
    }
    _w->back_pixel_set = 0;
    if (background_pixmap == ParentRelative) {
	_w->back_pixmap = _p;
    } else {
	_w->back_pixmap = NULL;
    }
    SdlTkUnlock(display);
}

void
XSetWindowBorder(Display *display, Window w, unsigned long border_pixel)
{
}

void
XSetWindowBorderPixmap(Display *display, Window w, Pixmap border_pixmap)
{
}

void
XSetWindowBorderWidth(Display *display, Window w, unsigned int width)
{
    _Window *_w = (_Window *) w;

    SdlTkLock(display);
    display->request++;

    _w->atts.border_width = width;
    _w->parentWidth = _w->atts.width + 2 * width;
    _w->parentHeight = _w->atts.height + 2 * width;
    SdlTkScreenChanged();

    SdlTkUnlock(display);
}

void
XSetWindowColormap(Display *display, Window w, Colormap colormap)
{
}

void
XSetWMClientMachine(Display *display, Window w, XTextProperty *text_prop)
{
}

Status
XSetWMColormapWindows(Display *display, Window w, Window *colormap_windows,
		      int count)
{
    return 0;
}

int
XSetWMHints(Display *display, Window w, XWMHints *wm_hints)
{
    return 0;
}

void
XSetWMNormalHints(Display *display, Window w, XSizeHints *hints)
{
    _Window *_w = (_Window *) w;

    SdlTkLock(display);
    display->request++;

    if (_w->display == NULL) {
	goto done;
    }
    if (hints->flags & PBaseSize) {
	_w->size.base_width = hints->base_width;
	_w->size.base_height = hints->base_height;
    }
    if (hints->flags & PMinSize) {
	_w->size.min_width = hints->min_width;
	_w->size.min_height = hints->min_height;
    }
    if (hints->flags & PMaxSize) {
	_w->size.max_width = hints->max_width;
	_w->size.max_height = hints->max_height;
    }
    if (hints->flags & PResizeInc) {
	_w->size.width_inc = hints->width_inc;
	_w->size.height_inc = hints->height_inc;
    }
    _w->size.flags = hints->flags;
done:
    SdlTkUnlock(display);
}

int
XStoreName(Display *display, Window w, _Xconst char *window_name)
{
    return 0;
}

Status
XStringListToTextProperty(char **list, int count,
			  XTextProperty *text_prop_return)
{
    return (Status) 0;
}

KeySym
XStringToKeysym(_Xconst char *string)
{
    return NoSymbol;
}

#if 0
void
XSubtractRegion(Region sra, Region srb, Region dr_return)
{
}
#endif

int
XSync(Display *display, Bool discard)
{
    SdlTkLock(display);
    display->request++;
    SdlTkUnlock(display);
    return 0;
}

int
XSynchronize(Display *display, Bool discard)
{
    SdlTkLock(display);
    display->request++;
    SdlTkUnlock(display);
    return 0;
}

int
XTextWidth(XFontStruct *font_struct, const char *string, int count)
{
    return SdlTkGfxTextWidth(font_struct->fid, string, count, NULL);
}

int
XTextWidthX(XFontStruct *font_struct, const char *string, int count, int *maxw)
{
    return SdlTkGfxTextWidth(font_struct->fid, string, count, maxw);
}

int
XTextWidth16(XFontStruct *font_struct, const XChar2b *string, int count)
{
    return SdlTkGfxTextWidth(font_struct->fid, (char *) string, count, 0);
}

/*
 * If XTranslateCoordinates() returns True, it takes the src_x and src_y
 * coordinates relative to the source window's origin and returns these
 * coordinates to dest_x_return and dest_y_return relative to the destination
 * window's origin. If XTranslateCoordinates() returns False, src_w and dest_w
 * are on different screens, and dest_x_return and dest_y_return are zero.
 * If the coordinates are contained in a mapped child of dest_w, that child is
 * returned to child_return. Otherwise, child_return is set to None.
 */

Bool
XTranslateCoordinates(Display *display, Window src_w, Window dest_w,
		      int src_x, int src_y, int *dest_x_return,
		      int *dest_y_return, Window *child_return)
{
    _Window *_src = (_Window *) src_w;
    _Window *_dest = (_Window *) dest_w;
    int rootx, rooty;

    SdlTkLock(display);
    display->request++;

    SdlTkRootCoords(_src, &rootx, &rooty);
    src_x += rootx;
    src_y += rooty;

    SdlTkRootCoords(_dest, &rootx, &rooty);
    *dest_x_return = src_x - rootx;
    *dest_y_return = src_y - rooty;

    *child_return = (Window) SdlTkPointToWindow(_dest, src_x, src_y,
						True, False);
    if (*child_return == dest_w) {
	*child_return = None;
    }

    SdlTkUnlock(display);

    return True;
}

void
XUngrabKeyboard(Display *display, Time time)
{
    SdlTkLock(display);
    display->request++;
    if ((SdlTkX.keyboard_window != NULL) &&
	(SdlTkX.keyboard_window->display == display)) {
	SdlTkX.keyboard_window = NULL;
    }
    SdlTkUnlock(display);
}

#if 0
int
XUngrabPointer(Display *display, Time time)
{
    return 0;
}
#endif

int
XUngrabServer(Display *display)
{
    SdlTkLock(display);
    display->request++;
    xlib_grab = NULL;
    Tcl_ConditionNotify(&xlib_cond);
    SdlTkUnlock(display);
    return 0;
}

#if 0
void
XUnionRectWithRegion(XRectangle *rectangle, Region src_region,
		     Region dest_region_return)
{
}
#endif

/*
 * "The XUnmapWindow() function unmaps the specified window and causes the X
 * server to generate an UnmapNotify event. If the specified window is already
 * unmapped, XUnmapWindow() has no effect. Normal exposure processing on
 * formerly obscured windows is performed. Any child window will no longer be
 * visible until another map call is made on the parent. In other words, the
 * subwindows are still mapped but are not visible until the parent is mapped.
 * Unmapping a window will generate Expose events on windows that were formerly
 * obscured by it."
 */

static void
SdlTkUnmapWindow(Display *display, Window w)
{
    _Window *_w = (_Window *) w;
    XEvent event;

    if (_w->display == NULL) {
	return;
    }
    if (_w->atts.map_state == IsUnmapped) {
	return;
    }

    /* Unmap decorative frame */
    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
	_w->parent->atts.map_state = IsUnmapped;
    }

    _w->atts.map_state = IsUnmapped;

    /* Tk only cares about this for wrapper windows */
    if (_w->atts.your_event_mask & StructureNotifyMask) {
	memset(&event, 0, sizeof (event));
	event.type = UnmapNotify;
	event.xunmap.serial = _w->display->request;
	event.xunmap.send_event = False;
	event.xunmap.display = _w->display;
	event.xunmap.event = w;
	event.xunmap.window = w;
	event.xunmap.from_configure = False;
	SdlTkQueueEvent(&event);
    }

    if ((_w->parent != NULL) && (_w->parent->dec != NULL)) {
	SdlTkVisRgnChanged(_w->parent, VRC_CHANGED | VRC_DO_PARENT, 0, 0);
    } else {
	SdlTkVisRgnChanged(_w, VRC_CHANGED | VRC_DO_PARENT, 0, 0);
    }

    /*
     * "All FocusOut events caused by a window unmap are
     * generated after any UnmapNotify event"
     */

    if (SdlTkX.focus_window_not_override == w) {
	SdlTkX.focus_window_not_override = None;
    }
    if (SdlTkX.focus_window == w) {
	SdlTkLostFocusWindow();
    }
    if (SdlTkX.keyboard_window == _w) {
	SdlTkX.keyboard_window = NULL;
    }

    SdlTkScreenChanged();
}

void
XUnmapWindow(Display *display, Window w)
{
    SdlTkLock(display);
    display->request++;
    SdlTkUnmapWindow(display, w);
    SdlTkUnlock(display);
}

int
XWindowEvent(Display *display, Window w, long event_mask, XEvent *event_return)
{
    return 0;
}

int
XWithdrawWindow(Display *display, Window w, int screen_number)
{
    XUnmapWindow(display, w);
    return 1;
}

int
XmbLookupString(XIC ic, XKeyPressedEvent *event, char *buffer_return,
		int bytes_buffer, KeySym *keysym_return, Status *status_return)
{
    return 0;
}

VisualID
XVisualIDFromVisual(Visual *visual)
{
    return 0;
}

void
XWarpPointer(Display *display, Window src_w, Window dest_w,
	     int src_x, int src_y, unsigned int src_width,
	     unsigned int src_height, int dest_x, int dest_y)
{
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkLostFocusWindow --
 *
 *	Called when the X wrapper which had the focus is unmapped.
 *	Sets the focus to the topmost visible wrapper window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The focus changes.
 *
 *----------------------------------------------------------------------
 */

static void
SdlTkLostFocusWindow(void)
{
    _Window *focus;

    focus = SdlTkTopVisibleWrapper();
    SdlTkSetInputFocus(SdlTkX.display, (Window) focus,
		       RevertToParent, CurrentTime);
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkGLXAvailable --
 *
 *	Test if OpenGL support available.
 *
 * Results:
 *	0 (no OpenGL), 1 else.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

int
SdlTkGLXAvailable(Display *display)
{
#ifdef SDL_RENDERER_HAS_TARGET_3D
    return ((display != NULL) && !SdlTkX.arg_nogl);
#else
    return 0;
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkGLXCreateContext --
 *
 *	Create GL context given display, X window identifier
 *	and Tk_Window token.
 *
 * Results:
 *	GL context or NULL.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

void *
SdlTkGLXCreateContext(Display *display, Window w, Tk_Window tkwin)
{
    _Window *_w = (_Window *) w;
#ifdef SDL_RENDERER_HAS_TARGET_3D
#ifdef ANDROID
    int depth;
#else
    SDL_GLContext ctx;
#endif

    SdlTkLock(display);
    display->request++;
    if (_w->display == NULL) {
	goto done;
    }
    _w->tkwin = (TkWindow *) tkwin;
#ifdef ANDROID
    if (_w->gl_tex != NULL) {
	goto done;
    }
    while (SdlTkX.in_background) {
	Tcl_ConditionWait(&time_cond, &xlib_lock, NULL);
    }
    SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &depth);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    if (display->gl_rend == NULL) {
	display->gl_rend = SDL_CreateRendererGLES1(SdlTkX.sdlscreen);
    }
    if (display->gl_rend != NULL) {
	SDL_Texture *tex;

	tex = SDL_CreateTexture((SDL_Renderer *) display->gl_rend,
				SDL_PIXELFORMAT_ABGR8888,
				SDL_TEXTUREACCESS_TARGET_3D,
				_w->atts.width, _w->atts.height);
	if (tex != NULL) {
	    SDL_SetRenderTarget((SDL_Renderer *) display->gl_rend, tex);
	    _w->gl_tex = tex;
	}
    }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depth);
#else
    if (SdlTkX.arg_nogl) {
	goto done;
    }
    if (SDL_CreateWindowAndRenderer(64, 64,
				    SDL_WINDOW_HIDDEN | SDL_WINDOW_POPUP_MENU,
				    &_w->gl_wind, &_w->gl_rend) < 0) {
	goto done2;
    }
    ctx = SDL_GL_GetCurrentContext();
    if (ctx != NULL) {
	SDL_Texture *tex;

	tex = SDL_CreateTexture(_w->gl_rend, SDL_PIXELFORMAT_ABGR8888,
				SDL_TEXTUREACCESS_TARGET_3D,
				_w->atts.width, _w->atts.height);
	if (tex != NULL) {
	    SDL_SetRenderTarget(_w->gl_rend, tex);
	    _w->gl_tex = tex;
	} else {
	    SDL_DestroyRenderer(_w->gl_rend);
	    _w->gl_rend = NULL;
	    SDL_DestroyWindow(_w->gl_wind);
	    _w->gl_wind = NULL;
	}
    }
done2:
    GLLOG("SdlTkGLXCreateContext: tex=%p", _w->gl_tex);
#endif
done:
    SdlTkUnlock(display);
    return (void *) _w->gl_tex;
#else
    return NULL;
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkGLXDestroyContext --
 *
 *	Destroy GL context given display, X window identifier.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

void
SdlTkGLXDestroyContext(Display *display, Window w, void *ctx)
{
#ifdef SDL_RENDERER_HAS_TARGET_3D
    _Window *_w = (_Window *) w;

    SdlTkLock(display);
    display->request++;
    if (_w->display == NULL) {
	goto done;
    }
#ifndef ANDROID
    GLLOG("SdlTkGLXDestroyContext: tex=%p", _w->gl_tex);
#endif
    if (_w->gl_tex != NULL) {
	SDL_DestroyTexture(_w->gl_tex);
	_w->gl_tex = NULL;
    }
#ifndef ANDROID
    if (_w->gl_rend != NULL) {
	SDL_DestroyRenderer(_w->gl_rend);
	_w->gl_rend = NULL;
    }
    if (_w->gl_wind != NULL) {
	SDL_DestroyWindow(_w->gl_wind);
	_w->gl_wind = NULL;
    }
#endif
done:
    SdlTkUnlock(display);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkGLXMakeCurrent --
 *
 *	Activate given GL context given display, X window identifier.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

void
SdlTkGLXMakeCurrent(Display *display, Window w, void *ctx)
{
#ifdef SDL_RENDERER_HAS_TARGET_3D
    _Window *_w = (_Window *) w;
    SDL_Renderer *rend;

    SdlTkLock(display);
    display->request++;
#ifdef ANDROID
    rend = (SDL_Renderer *) display->gl_rend;
#else
    rend = _w->gl_rend;
#endif
    if ((_w->display == NULL) || (rend == NULL)) {
	goto done;
    }
#ifdef ANDROID
    if (SdlTkX.in_background) {
	if (_w->atts.map_state != IsUnmapped) {
	    _w->gl_flags |= 1;
	}
	goto done;
    }
    SDL_SetRenderTargetQuick(rend, _w->gl_tex);
#else
    GLLOG("SdlTkGLXMakeCurrent: tex=%p", _w->gl_tex);
#endif
done:
    SdlTkUnlock(display);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkGLXReleaseCurrent --
 *
 *	Deactivate given GL context given display, X window identifier.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

void
SdlTkGLXReleaseCurrent(Display *display, Window w, void *ctx)
{
#ifdef SDL_RENDERER_HAS_TARGET_3D
    _Window *_w = (_Window *) w;
    SDL_Renderer *rend;

    SdlTkLock(display);
    display->request++;
#ifdef ANDROID
    rend = (SDL_Renderer *) display->gl_rend;
#else
    rend = _w->gl_rend;
#endif
    if ((_w->display == NULL) || (rend == NULL)) {
	goto done;
    }
#ifdef ANDROID
    if (SdlTkX.in_background) {
	if (_w->atts.map_state != IsUnmapped) {
	    _w->gl_flags |= 1;
	}
	goto done;
    }
    SDL_SetRenderTarget(rend, NULL);
#else
    GLLOG("SdlTkGLXReleaseCurrent: tex=%p", _w->gl_tex);
#endif
done:
    SdlTkUnlock(display);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkGLXSwapBuffers --
 *
 *	Put pixels from GL context on given display, X window identifier.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

void
SdlTkGLXSwapBuffers(Display *display, Window w)
{
#ifdef SDL_RENDERER_HAS_TARGET_3D
    _Window *_w = (_Window *) w;
    SDL_Renderer *rend;
    XGCValues xgc;
    int doClear = 1;

    SdlTkLock(display);
    display->request++;
#ifdef ANDROID
    rend = (SDL_Renderer *) display->gl_rend;
#else
    rend = _w->gl_rend;
#endif
    if ((_w->display == NULL) || (rend == NULL)) {
	goto done;
    }
#ifdef ANDROID
    if (SdlTkX.in_background) {
	if (_w->atts.map_state != IsUnmapped) {
	    _w->gl_flags |= 1;
	}
	goto done;
    }
#else
    GLLOG("SdlTkGLXSwapBuffers: tex=%p", _w->gl_tex);
#endif
    memset(&xgc, 0, sizeof (xgc));
    if (_w->gl_tex != NULL) {
	int width, height;
	SDL_Surface *surf;

	SDL_QueryTexture(_w->gl_tex, NULL, NULL, &width, &height);
	if ((width != _w->atts.width) || (height != _w->atts.height)) {
	    SDL_Texture *tex;

	    tex = SDL_CreateTexture(rend, SDL_PIXELFORMAT_ABGR8888,
				    SDL_TEXTUREACCESS_TARGET_3D,
				    _w->atts.width, _w->atts.height);
	    if (tex != NULL) {
		SDL_SetRenderTarget(rend, tex);
		SDL_DestroyTexture(_w->gl_tex);
		_w->gl_tex = tex;
		SdlTkGenerateConfigureNotify(NULL, w);
		goto done;
	    }
	}
	SDL_QueryTexture(_w->gl_tex, NULL, NULL, &width, &height);
	surf = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height,
				    SdlTkX.sdlsurf->format->BitsPerPixel,
				    SdlTkX.sdlsurf->format->Rmask,
				    SdlTkX.sdlsurf->format->Gmask,
				    SdlTkX.sdlsurf->format->Bmask,
				    SdlTkX.sdlsurf->format->Amask);
	if (surf != NULL) {
#ifdef ANDROID
            int frame_count = SdlTkX.frame_count;
	    int wait_refr = 0;

	    if (SDL_SetRenderTarget(rend, _w->gl_tex) == 0) {
		Uint32 fmt = SDL_GetWindowPixelFormat(SdlTkX.sdlscreen);

		if (SDL_RenderReadPixels(rend, NULL, fmt, surf->pixels,
				         surf->pitch) == 0) {
		    _Pixmap p;

		    p.type = DT_PIXMAP;
		    p.sdl = surf;
		    p.format = _w->format;
		    p.next = NULL;
		    SdlTkGfxCopyArea((Pixmap) &p, w, &xgc, 0, 0,
				     width, height, 0, 0);
		    SdlTkScreenChanged();
		    SdlTkDirtyArea(w, 0, 0, width, height);
		    wait_refr = 1;
		    doClear = 0;
		}
	    }
	    SDL_FreeSurface(surf);
	    SDL_SetRenderTarget(rend, NULL);
	    /* wait for next screen refresh */
	    do {
		Tcl_ConditionWait(&time_cond, &xlib_lock, NULL);
	    } while (wait_refr && (SdlTkX.frame_count == frame_count));
#else
	    Uint32 fmt = SDL_GetWindowPixelFormat(_w->gl_wind);

	    if (fmt == SDL_PIXELFORMAT_UNKNOWN) {
		/*
		 * This can happen with the Wayland video driver,
		 * thus try to go on with 24 bit RGB.
		 */
		fmt = SDL_PIXELFORMAT_RGB888;
	    }
	    if (SDL_RenderReadPixels(rend, NULL, fmt, surf->pixels,
				     surf->pitch) == 0) {
		_Pixmap p;

		p.type = DT_PIXMAP;
		p.sdl = surf;
		p.format = _w->format;
		p.next = NULL;
		SdlTkGfxCopyArea((Pixmap) &p, w, &xgc, 0, 0,
				 width, height, 0, 0);
		SdlTkScreenChanged();
		SdlTkDirtyArea(w, 0, 0, width, height);
		doClear = 0;
	    }
	    SDL_FreeSurface(surf);
#endif
	}
    }
    if (doClear) {
	/* no texture or some other problem: clear window to black */
	memset(&xgc, 0, sizeof (xgc));
	xgc.foreground = SdlTkX.screen->black_pixel;
	SdlTkGfxFillRect(w, &xgc, 0, 0, _w->atts.width, _w->atts.height);
	SdlTkScreenChanged();
	SdlTkDirtyArea(w, 0, 0, _w->atts.width, _w->atts.height);
    }
done:
    SdlTkUnlock(display);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkDumpXEvent --
 *
 *	Poor man's "xev" like event printer.
 *
 *----------------------------------------------------------------------
 */

#ifdef TRACE_XEVENTS
#ifdef ANDROID
#define XELOG(...) __android_log_print(ANDROID_LOG_ERROR,"XEV",__VA_ARGS__)
#else
#define XELOG(...) SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,__VA_ARGS__)
#endif
#else
#define XELOG(...)
#endif

void
SdlTkDumpXEvent(XEvent *eventPtr)
{
#ifdef TRACE_XEVENTS
    const char *name, *sep1 = ",";
    char buffer[256];

    switch (eventPtr->xany.type) {
    case KeyPress:		name = "KeyPress"; break;
    case KeyRelease:		name = "KeyRelease"; break;
    case ButtonPress:		name = "ButtonPress"; break;
    case ButtonRelease:		name = "ButtonRelease"; break;
    case MotionNotify:		name = "MotionNotify"; break;
    case EnterNotify:		name = "EnterNotify"; break;
    case LeaveNotify:		name = "LeaveNotify"; break;
    case FocusIn:		name = "FocusIn"; break;
    case FocusOut:		name = "FocusOut"; break;
    case KeymapNotify:		name = "KeymapNotify"; break;
    case Expose:		name = "Expose"; break;
    case GraphicsExpose:	name = "GraphicsExpose"; break;
    case NoExpose:		name = "NoExpose"; break;
    case VisibilityNotify:	name = "VisibilityNotify"; break;
    case CreateNotify:		name = "CreateNotify"; break;
    case DestroyNotify:		name = "DestroyNotify"; break;
    case UnmapNotify:		name = "UnmapNotify"; break;
    case MapNotify:		name = "MapNotify"; break;
    case MapRequest:		name = "MapRequest"; break;
    case ReparentNotify:	name = "ReparentNotify"; break;
    case ConfigureNotify:	name = "ConfigureNotify"; break;
    case ConfigureRequest:	name = "ConfigureRequest"; break;
    case GravityNotify:		name = "GravityNotify"; break;
    case ResizeRequest:		name = "ResizeRequest"; break;
    case CirculateNotify:	name = "CirculateNotify"; break;
    case CirculateRequest:	name = "CirculateRequest"; break;
    case PropertyNotify:	name = "PropertyNotify"; break;
    case SelectionClear:	name = "SelectionClear"; break;
    case SelectionRequest:	name = "SelectionRequest"; break;
    case SelectionNotify:	name = "SelectionNotify"; break;
    case ColormapNotify:	name = "ColormapNotify"; break;
    case ClientMessage:		name = "ClientMessage"; break;
    case MappingNotify:		name = "MappingNotify"; break;
    case VirtualEvent:		name = "VirtualEvent"; break;
    case ActivateNotify:	name = "ActivateNotify"; sep1 = ""; break;
    case DeactivateNotify:	name = "DeactivateNotify"; sep1 = ""; break;
    case MouseWheelEvent:	name = "MouseWheelEvent"; sep1 = ""; break;
    case PointerUpdate:		name = "PointerUpdate"; sep1 = ""; break;
    default:
	sprintf(buffer, "UnknownType%d", eventPtr->xany.type);
	name = buffer;
	sep1 = "";
	break;
    }

    XELOG("%s event, serial %ld, synthetic %s, window 0x%lx%s", name,
	eventPtr->xany.serial, eventPtr->xany.send_event ? "YES" : "NO",
	eventPtr->xany.window, sep1);

    switch (eventPtr->xany.type) {

    case KeyPress:
    case KeyRelease: {
	XKeyEvent *evPtr = &eventPtr->xkey;
	int i;
	char line[80];

	XELOG("    root 0x%lx, subw 0x%lx, time %lu, (%d,%d), root:(%d,%d),",
	    evPtr->root, evPtr->subwindow, evPtr->time, evPtr->x, evPtr->y,
	    evPtr->x_root, evPtr->y_root);
	XELOG("    state 0x%x, keycode %u, same_screen %s, nbytes %d%s",
	    evPtr->state, evPtr->keycode, evPtr->same_screen ? "YES" : "NO",
	    evPtr->nbytes, (evPtr->nbytes > 0) ? "," : "");
	if (evPtr->nbytes > 0) {
	    for (i = 0; i < evPtr->nbytes; i++) {
		sprintf(line + i * 5, " 0x%02x", evPtr->trans_chars[i] & 0xff);
	    }
	    XELOG("    trans_chars:%s", line);
	}
	break;
    }

    case ButtonPress:
    case ButtonRelease: {
	XButtonEvent *evPtr = &eventPtr->xbutton;

	XELOG("    root 0x%lx, subw 0x%lx, time %lu, (%d,%d), root:(%d,%d),",
	    evPtr->root, evPtr->subwindow, evPtr->time, evPtr->x, evPtr->y,
	    evPtr->x_root, evPtr->y_root);
	XELOG("    state 0x%x, button %u, same_screen %s",
	    evPtr->state, evPtr->button, evPtr->same_screen ? "YES" : "NO");
	break;
    }

    case MotionNotify: {
	XMotionEvent *evPtr = &eventPtr->xmotion;

	XELOG ("    root 0x%lx, subw 0x%lx, time %lu, (%d,%d), root:(%d,%d),",
	    evPtr->root, evPtr->subwindow, evPtr->time, evPtr->x, evPtr->y,
	    evPtr->x_root, evPtr->y_root);
	XELOG ("    state 0x%x, is_hint %u, same_screen %s",
	    evPtr->state, evPtr->is_hint, evPtr->same_screen ? "YES" : "NO");
	break;
    }

    case EnterNotify:
    case LeaveNotify: {
	XCrossingEvent *evPtr = &eventPtr->xcrossing;
	const char *mode, *detail;
	char dmode[16], ddetail[16];

	switch (evPtr->mode) {
	case NotifyNormal:		mode = "NotifyNormal"; break;
	case NotifyGrab:		mode = "NotifyGrab"; break;
	case NotifyUngrab:		mode = "NotifyUngrab"; break;
	case NotifyWhileGrabbed:	mode = "NotifyWhileGrabbed"; break;
	default:
	    mode = dmode;
	    sprintf(dmode, "%u", evPtr->mode);
	    break;
	}
	switch (evPtr->detail) {
	case NotifyAncestor:		detail = "NotifyAncestor"; break;
	case NotifyVirtual:		detail = "NotifyVirtual"; break;
	case NotifyInferior:		detail = "NotifyInferior"; break;
	case NotifyNonlinear:		detail = "NotifyNonlinear"; break;
	case NotifyNonlinearVirtual:	detail = "NotifyNonlinearVirtual";
	    break;
	case NotifyPointer:		detail = "NotifyPointer"; break;
	case NotifyPointerRoot:		detail = "NotifyPointerRoot"; break;
	case NotifyDetailNone:		detail = "NotifyDetailNone"; break;
	default:
	    detail = ddetail;
	    sprintf(ddetail, "%u", evPtr->detail);
	    break;
	}
	XELOG("    root 0x%lx, subw 0x%lx, time %lu, (%d,%d), root:(%d,%d),",
	    evPtr->root, evPtr->subwindow, evPtr->time, evPtr->x, evPtr->y,
	    evPtr->x_root, evPtr->y_root);
	XELOG("    mode %s, detail %s, same_screen %s,",
	    mode, detail, evPtr->same_screen ? "YES" : "NO");
	XELOG("    focus %s, state %u", evPtr->focus ? "YES" : "NO",
	    evPtr->state);
	break;
    }

    case FocusIn:
    case FocusOut: {
	XFocusChangeEvent *evPtr = &eventPtr->xfocus;
	const char *mode, *detail;
	char dmode[16], ddetail[16];

	switch (evPtr->mode) {
	case NotifyNormal:		mode = "NotifyNormal"; break;
	case NotifyGrab:		mode = "NotifyGrab"; break;
	case NotifyUngrab:		mode = "NotifyUngrab"; break;
	case NotifyWhileGrabbed:	mode = "NotifyWhileGrabbed"; break;
	default:
	    mode = dmode;
	    sprintf(dmode, "%u", evPtr->mode);
	    break;
	}
	switch (evPtr->detail) {
	case NotifyAncestor:		detail = "NotifyAncestor"; break;
	case NotifyVirtual:		detail = "NotifyVirtual"; break;
	case NotifyInferior:		detail = "NotifyInferior"; break;
	case NotifyNonlinear:		detail = "NotifyNonlinear"; break;
	case NotifyNonlinearVirtual:	detail = "NotifyNonlinearVirtual";
	    break;
	case NotifyPointer:		detail = "NotifyPointer"; break;
	case NotifyPointerRoot:		detail = "NotifyPointerRoot"; break;
	case NotifyDetailNone:		detail = "NotifyDetailNone"; break;
	default:
	    detail = ddetail;
	    sprintf(ddetail, "%u", evPtr->detail);
	    break;
	}
	XELOG("    mode %s, detail %s", mode, detail);
	break;
    }

    case KeymapNotify: {
	XKeymapEvent *evPtr = &eventPtr->xkeymap;
	int i;
	char line[80];

	for (i = 0; i < 16; i++) {
	    sprintf(line + i * 4, "%-3u ",
		    (unsigned int) evPtr->key_vector[i]);
	}
	XELOG("    keys:  %s", line);
	for (; i < 32; i++) {
	    sprintf(line + (i - 16) * 4, "%-3u ",
		    (unsigned int) evPtr->key_vector[i]);
	}
	XELOG("           %s", line);
	break;
    }

    case Expose: {
	XExposeEvent *evPtr = &eventPtr->xexpose;

	XELOG("    (%d,%d), width %d, height %d, count %d",
	    evPtr->x, evPtr->y, evPtr->width, evPtr->height, evPtr->count);
	break;
    }

    case GraphicsExpose: {
	XGraphicsExposeEvent *evPtr = &eventPtr->xgraphicsexpose;
	const char *m;
	char mdummy[16];

	switch (evPtr->major_code) {
	case 62:	m = "CopyArea";  break;
	case 63:	m = "CopyPlane";  break;
	default:
	    m = mdummy;
	    sprintf(mdummy, "%d", evPtr->major_code);
	    break;
	}
	XELOG("    (%d,%d), width %d, height %d, count %d,",
	    evPtr->x, evPtr->y, evPtr->width, evPtr->height, evPtr->count);
	XELOG("    major %s, minor %d", m, evPtr->minor_code);
	break;
    }

    case NoExpose: {
	XNoExposeEvent *evPtr = &eventPtr->xnoexpose;
	const char *m;
	char mdummy[16];

	switch (evPtr->major_code) {
	case 62:	m = "CopyArea";  break;
	case 63:	m = "CopyPlane";  break;
	default:
	    m = mdummy;
	    sprintf(mdummy, "%d", evPtr->major_code);
	    break;
	}
	XELOG("    major %s, minor %d", m, evPtr->minor_code);
	break;
    }

    case VisibilityNotify: {
	XVisibilityEvent *evPtr = &eventPtr->xvisibility;
	const char *v;
	char vdummy[16];

	switch (evPtr->state) {
	case VisibilityUnobscured:
	    v = "VisibilityUnobscured";
	    break;
	case VisibilityPartiallyObscured:
	    v = "VisibilityPartiallyObscured";
	    break;
	case VisibilityFullyObscured:
	    v = "VisibilityFullyObscured";
	    break;
	default:
	    v = vdummy;
	    sprintf(vdummy, "%d", evPtr->state);
	    break;
	}
	XELOG("    state %s", v);
	break;
    }

    case CreateNotify: {
	XCreateWindowEvent *evPtr = &eventPtr->xcreatewindow;

	XELOG("    parent 0x%lx, window 0x%lx, (%d,%d), width %d, height %d,",
	    evPtr->parent, evPtr->window, evPtr->x, evPtr->y,
	    evPtr->width, evPtr->height);
	XELOG("    border_width %d, override %s",
	    evPtr->border_width, evPtr->override_redirect ? "YES" : "NO");
	break;
    }

    case DestroyNotify: {
	XDestroyWindowEvent *evPtr = &eventPtr->xdestroywindow;

	XELOG("    event 0x%lx, window 0x%lx", evPtr->event, evPtr->window);
	break;
    }

    case UnmapNotify: {
	XUnmapEvent *evPtr = &eventPtr->xunmap;

	XELOG("    event 0x%lx, window 0x%lx, from_configure %s",
	    evPtr->event, evPtr->window, evPtr->from_configure ? "YES" : "NO");
	break;
    }

    case MapNotify: {
	XMapEvent *evPtr = &eventPtr->xmap;

	XELOG("    event 0x%lx, window 0x%lx, override %s",
	    evPtr->event, evPtr->window,
	    evPtr->override_redirect ? "YES" : "NO");
	break;
    }

    case MapRequest: {
	XMapRequestEvent *evPtr = &eventPtr->xmaprequest;

	XELOG("    parent 0x%lx, window 0x%lx",
	      evPtr->parent, evPtr->window);
	break;
    }

    case ReparentNotify: {
	XReparentEvent *evPtr = &eventPtr->xreparent;

	XELOG("    event 0x%lx, window 0x%lx, parent 0x%lx,",
	    evPtr->event, evPtr->window, evPtr->parent);
	XELOG("    (%d,%d), override %s", evPtr->x, evPtr->y,
	    evPtr->override_redirect ? "YES" : "NO");
	break;
    }

    case ConfigureNotify: {
	XConfigureEvent *evPtr = &eventPtr->xconfigure;

	XELOG("    event 0x%lx, window 0x%lx, (%d,%d), width %d, height %d,",
	    evPtr->event, evPtr->window, evPtr->x, evPtr->y,
	    evPtr->width, evPtr->height);
	XELOG("    border_width %d, above 0x%lx, override %s",
	    evPtr->border_width, evPtr->above,
	    evPtr->override_redirect ? "YES" : "NO");
	break;
    }

    case ConfigureRequest: {
	XConfigureRequestEvent *evPtr = &eventPtr->xconfigurerequest;
	const char *detail;
	char ddummy[16];

	switch (evPtr->detail) {
	case Above:	detail = "Above";  break;
	case Below:	detail = "Below";  break;
	case TopIf:	detail = "TopIf";  break;
	case BottomIf:	detail = "BottomIf"; break;
	case Opposite:	detail = "Opposite"; break;
	default:
	    detail = ddummy;
	    sprintf(ddummy, "%d", evPtr->detail);
	    break;
	}
	XELOG("    parent 0x%lx, window 0x%lx, (%d,%d), width %d, height %d,",
	    evPtr->parent, evPtr->window, evPtr->x, evPtr->y,
	    evPtr->width, evPtr->height);
	XELOG("    border_width %d, above 0x%lx, detail %s, value 0x%lx",
	    evPtr->border_width, evPtr->above, detail, evPtr->value_mask);
	break;
    }

    case GravityNotify: {
	XGravityEvent *evPtr = &eventPtr->xgravity;

	XELOG("    event 0x%lx, window 0x%lx, (%d,%d)",
	    evPtr->event, evPtr->window, evPtr->x, evPtr->y);
	break;
    }

    case ResizeRequest: {
	XResizeRequestEvent *evPtr = &eventPtr->xresizerequest;

	XELOG("    width %d, height %d", evPtr->width, evPtr->height);
	break;
    }

    case CirculateNotify: {
	XCirculateEvent *evPtr = &eventPtr->xcirculate;
	const char *p;
	char pdummy[16];

	switch (evPtr->place) {
	case PlaceOnTop:	p = "PlaceOnTop"; break;
	case PlaceOnBottom:	p = "PlaceOnBottom"; break;
	default:
	    p = pdummy;
	    sprintf(pdummy, "%d", evPtr->place);
	    break;
	}
	XELOG("    event 0x%lx, window 0x%lx, place %s",
	    evPtr->event, evPtr->window, p);
	break;
    }

    case CirculateRequest: {
	XCirculateRequestEvent *evPtr = &eventPtr->xcirculaterequest;
	char *p;
	char pdummy[16];

	switch (evPtr->place) {
	case PlaceOnTop:	p = "PlaceOnTop"; break;
	case PlaceOnBottom:	p = "PlaceOnBottom"; break;
	default:
	    p = pdummy;
	    sprintf(pdummy, "%d", evPtr->place);
	    break;
	}
	XELOG("    parent 0x%lx, window 0x%lx, place %s",
	    evPtr->parent, evPtr->window, p);
	break;
    }

    case PropertyNotify: {
	XPropertyEvent *evPtr = &eventPtr->xproperty;
	char *aname = XGetAtomName(evPtr->display, evPtr->atom);
	char *s;
	char sdummy[16];

	switch (evPtr->state) {
	case PropertyNewValue:	s = "PropertyNewValue"; break;
	case PropertyDelete:	s = "PropertyDelete"; break;
	default:
	    s = sdummy;
	    sprintf(sdummy, "%d", evPtr->state);
	    break;
	}
	XELOG("    atom 0x%lx (%s), time %lu, state %s",
	   evPtr->atom, aname ? aname : "Unknown", evPtr->time,  s);
	if (aname) {
	    XFree(aname);
	}
	break;
    }

    case SelectionClear: {
	XSelectionClearEvent *evPtr = &eventPtr->xselectionclear;
	char *sname = XGetAtomName(evPtr->display, evPtr->selection);

	XELOG("    selection 0x%lx (%s), time %lu",
	    evPtr->selection, sname ? sname : "Unknown", evPtr->time);
	if (sname) {
	    XFree(sname);
	}
	break;
    }

    case SelectionRequest: {
	XSelectionRequestEvent *evPtr = &eventPtr->xselectionrequest;
	char *sname = XGetAtomName(evPtr->display, evPtr->selection);
	char *tname = XGetAtomName(evPtr->display, evPtr->target);
	char *pname = XGetAtomName(evPtr->display, evPtr->property);

	XELOG("    owner 0x%lx, requestor 0x%lx, selection 0x%lx (%s),",
	    evPtr->owner, evPtr->requestor, evPtr->selection,
	    sname ? sname : "Unknown");
	XELOG("    target 0x%lx (%s), property 0x%lx (%s), time %lu",
	    evPtr->target, tname ? tname : "Unknown", evPtr->property,
	    pname ? pname : "Unknown", evPtr->time);
	if (sname) {
	    XFree(sname);
	}
	if (tname) {
	    XFree(tname);
	}
	if (pname) {
	    XFree(pname);
	}
	break;
    }

    case SelectionNotify: {
	XSelectionEvent *evPtr = &eventPtr->xselection;
	char *sname = XGetAtomName(evPtr->display, evPtr->selection);
	char *tname = XGetAtomName(evPtr->display, evPtr->target);
	char *pname = XGetAtomName(evPtr->display, evPtr->property);

	XELOG("    selection 0x%lx (%s), target 0x%lx (%s),",
	    evPtr->selection, sname ? sname : "Unknown", evPtr->target,
	    tname ? tname : "Unknown");
	XELOG("    property 0x%lx (%s), time %lu",
	    evPtr->property, pname ? pname : "Unknown", evPtr->time);
	if (sname) {
	    XFree(sname);
	}
	if (tname) {
	    XFree(tname);
	}
	if (pname) {
	    XFree(pname);
	}
	break;
    }

    case ColormapNotify: {
	XColormapEvent *evPtr = &eventPtr->xcolormap;
	const char *s;
	char sdummy[16];

	switch (evPtr->state) {
	case ColormapInstalled:		s = "ColormapInstalled"; break;
	case ColormapUninstalled:	s = "ColormapUninstalled"; break;
	default:
	    s = sdummy;
	    sprintf(sdummy, "%d", evPtr->state);
	    break;
	}
	XELOG("    colormap 0x%lx, new %s, state %s",
	    evPtr->colormap, evPtr->new ? "YES" : "NO", s);
	break;
    }

    case ClientMessage: {
	XClientMessageEvent *evPtr = &eventPtr->xclient;
	char *mname = XGetAtomName(evPtr->display, evPtr->message_type);

        XELOG("    message_type 0x%lx (%s), format %d",
	    evPtr->message_type, mname ? mname : "Unknown", evPtr->format);
	if (mname) {
	    XFree(mname);
	}
	break;
    }

    case MappingNotify: {
	XMappingEvent *evPtr = &eventPtr->xmapping;
	const char *r;
	char rdummy[16];

	switch (evPtr->request) {
	case MappingModifier:	r = "MappingModifier"; break;
	case MappingKeyboard:	r = "MappingKeyboard"; break;
	case MappingPointer:	r = "MappingPointer"; break;
	default:
	    r = rdummy;
	    sprintf(rdummy, "%d", evPtr->request);
	    break;
	}
	XELOG("    request %s, first_keycode %d, count %d",
	    r, evPtr->first_keycode, evPtr->count);
	break;
    }

    case VirtualEvent: {
	XVirtualEvent *evPtr = (XVirtualEvent *) eventPtr;

	XELOG("    root 0x%lx, subw 0x%lx, time %lu, (%d,%d), root:(%d,%d),",
	    evPtr->root, evPtr->subwindow, evPtr->time, evPtr->x, evPtr->y,
	    evPtr->x_root, evPtr->y_root);
	XELOG("    state 0x%x, same_screen %s,", evPtr->state,
	    evPtr->same_screen ? "YES" : "NO");
	XELOG("    uid %p (%s), user_data %p", evPtr->name,
	    (char *) evPtr->name, evPtr->user_data);
	break;
    }
    }
#endif
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
