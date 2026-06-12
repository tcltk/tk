/*
 * tkUnixDraw.c --
 *
 *	This file contains X specific drawing routines.
 *
 * Copyright © 1995 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"

#ifndef _WIN32
#include "tkUnixInt.h"
#endif

#ifdef HAVE_XRENDER
#include <X11/extensions/Xrender.h>

#define TK_XRENDER_MIN_AREA (64 * 64)
				/* Use XRender only at or above this many
				 * pixels; below it the software blend is
				 * faster (see TkpPutRGBAImage). */
#endif

/*
 * The following structure is used to pass information to ScrollRestrictProc
 * from TkScrollWindow.
 */

typedef struct ScrollInfo {
    int done;			/* Flag is 0 until filtering is done. */
    Display *display;		/* Display to filter. */
    Window window;		/* Window to filter. */
    Region region;		/* Region into which damage is accumulated. */
    int dx, dy;			/* Amount by which window was shifted. */
} ScrollInfo;

/*
 * Forward declarations for functions declared later in this file:
 */

static Tk_RestrictProc ScrollRestrictProc;

/*
 *----------------------------------------------------------------------
 *
 * TkScrollWindow --
 *
 *	Scroll a rectangle of the specified window and accumulate damage
 *	information in the specified Region.
 *
 * Results:
 *	Returns false if no damage additional damage was generated. Sets damageRgn
 *	to contain the damaged areas and returns true if GraphicsExpose events
 *	were detected.
 *
 * Side effects:
 *	Scrolls the bits in the window and enters the event loop looking for
 *	damage events.
 *
 *----------------------------------------------------------------------
 */

bool
TkScrollWindow(
    Tk_Window tkwin,		/* The window to be scrolled. */
    GC gc,			/* GC for window to be scrolled. */
    int x, int y, int width, int height,
				/* Position rectangle to be scrolled. */
    int dx, int dy,		/* Distance rectangle should be moved. */
    Region damageRgn)		/* Region to accumulate damage in. */
{
    Tk_RestrictProc *prevProc;
    void *prevArg;
    ScrollInfo info;

    XCopyArea(Tk_Display(tkwin), Tk_WindowId(tkwin), Tk_WindowId(tkwin), gc,
	    x, y, (unsigned) width, (unsigned) height, x+dx, y+dy);

    info.done = 0;
    info.window = Tk_WindowId(tkwin);
    info.display = Tk_Display(tkwin);
    info.region = damageRgn;
    info.dx = dx;
    info.dy = dy;

    /*
     * Sync the event stream so all of the expose events will be on the Tk
     * event queue before we start filtering. This avoids busy waiting while
     * we filter events.
     */

    TkpSync(info.display);
    prevProc = Tk_RestrictEvents(ScrollRestrictProc, &info, &prevArg);
    while (!info.done) {
	Tcl_ServiceEvent(TCL_WINDOW_EVENTS);
    }
    Tk_RestrictEvents(prevProc, prevArg, &prevArg);

    return !XEmptyRegion(damageRgn);
}

/*
 *----------------------------------------------------------------------
 *
 * ScrollRestrictProc --
 *
 *	A Tk_RestrictProc used by TkScrollWindow to gather up Expose
 *	information into a single damage region. It accumulates damage events
 *	on the specified window until a NoExpose or the last GraphicsExpose
 *	event is detected.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Discards Expose events after accumulating damage information
 *	for a particular window.
 *
 *----------------------------------------------------------------------
 */

static Tk_RestrictAction
ScrollRestrictProc(
    void *arg,
    XEvent *eventPtr)
{
    ScrollInfo *info = (ScrollInfo *) arg;
    XRectangle rect;

    /*
     * Defer events which aren't for the specified window.
     */

    if (info->done || (eventPtr->xany.display != info->display)
	    || (eventPtr->xany.window != info->window)) {
	return TK_DEFER_EVENT;
    }

    if (eventPtr->type == NoExpose) {
	info->done = 1;
    } else if (eventPtr->type == GraphicsExpose) {
	rect.x = (short)eventPtr->xgraphicsexpose.x;
	rect.y = (short)eventPtr->xgraphicsexpose.y;
	rect.width = (unsigned short)eventPtr->xgraphicsexpose.width;
	rect.height = (unsigned short)eventPtr->xgraphicsexpose.height;
	XUnionRectWithRegion(&rect, info->region,
		info->region);

	if (eventPtr->xgraphicsexpose.count == 0) {
	    info->done = 1;
	}
    } else if (eventPtr->type == Expose) {
	/*
	 * This case is tricky. This event was already queued before the
	 * XCopyArea was issued. If this area overlaps the area being copied,
	 * then some of the copied area may be invalid. The easiest way to
	 * handle this case is to mark both the original area and the shifted
	 * area as damaged.
	 */

	rect.x = (short)eventPtr->xexpose.x;
	rect.y = (short)eventPtr->xexpose.y;
	rect.width = (unsigned short)eventPtr->xexpose.width;
	rect.height = (unsigned short)eventPtr->xexpose.height;
	XUnionRectWithRegion(&rect, info->region,
		info->region);
	rect.x += (short)info->dx;
	rect.y += (short)info->dy;
	XUnionRectWithRegion(&rect, info->region,
		info->region);
    } else {
	return TK_DEFER_EVENT;
    }
    return TK_DISCARD_EVENT;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_DrawHighlightBorder --
 *
 *	This function draws a rectangular ring around the outside of a widget
 *	to indicate that it has received the input focus.
 *
 *      On Unix, we just draw the simple inset ring. On other sytems, e.g. the
 *      Mac, the focus ring is a little more complicated, so we need this
 *      abstraction.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A rectangle "width" pixels wide is drawn in "drawable", corresponding
 *	to the outer area of "tkwin".
 *
 *----------------------------------------------------------------------
 */

void
Tk_DrawHighlightBorder(
    Tk_Window tkwin,
    GC fgGC,
    GC bgGC,
    int highlightWidth,
    Drawable drawable)
{
    (void)bgGC;

    TkDrawInsetFocusHighlight(tkwin, fgGC, highlightWidth, drawable, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDrawFrameEx --
 *
 *	This function draws the rectangular frame area.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws inside the tkwin area.
 *
 *----------------------------------------------------------------------
 */

void
TkpDrawFrameEx(
    Tk_Window tkwin,
    Drawable drawable,
    Tk_3DBorder border,
    int highlightWidth,
    int borderWidth,
    int relief)
{
    Tk_Fill3DRectangle(tkwin, drawable, border, highlightWidth,
	    highlightWidth, Tk_Width(tkwin) - 2*highlightWidth,
	    Tk_Height(tkwin) - 2*highlightWidth, borderWidth, relief);
}

#ifdef HAVE_XRENDER

/*
 *----------------------------------------------------------------------
 *
 * TkpPutRGBAImage --
 *
 *	Composite an RGBA image (4 bytes/pixel, NOT premultiplied) onto a
 *	drawable with XRenderComposite (Porter-Duff
 *	source-over).  Called by TkImgPhotoDisplay for partial-alpha photos
 *	in place of the software BlendComplexAlpha path, which reads the
 *	destination back with XGetImage and blends per pixel on the CPU.
 *	Works for both window and pixmap drawables.
 *
 * Results:
 *	Success, or BadDrawable when the composite could not be performed
 *	(no RENDER extension, or no picture format for the drawable's
 *	depth); the caller then falls back to the software blend.
 *
 * Side effects:
 *	Draws onto the drawable.
 *
 *----------------------------------------------------------------------
 */

int
TkpPutRGBAImage(
    Display *display,
    Drawable drawable,
    GC gc,			/* Unused: the composite is unclipped, like
				 * the macOS and Windows implementations. */
    XImage *image,		/* Source image; RGBA, not premultiplied. */
    int src_x, int src_y,	/* Top-left of the sub-rect within image. */
    int dest_x, int dest_y,	/* Top-left within drawable. */
    unsigned int width, unsigned int height)
{
    Window root;
    int x, y, eventBase, errorBase, screen = -1, i;
    unsigned int gw, gh, bw, depth;
    XRenderPictFormat *srcFmt, *dstFmt = NULL;
    Pixmap srcPix;
    GC srcGC;
    Picture srcPic, dstPic;
    XImage *argb;
    char *buf;
    int w = (int) width, h = (int) height;
    (void) gc;

    if (w <= 0 || h <= 0) {
	return Success;
    }

    /*
     * Below this area the fixed per-call cost (XGetGeometry round trip,
     * scratch pixmap/picture churn) outweighs the read-back and CPU blend
     * it replaces, so small composites are better off on the caller's
     * software path.  Measured crossover on a software-RENDER server
     * (Xwayland/pixman) is ~100x100; accelerated servers cross lower.
     */

    if ((unsigned long) w * h < TK_XRENDER_MIN_AREA) {
	return BadDrawable;
    }
    if (!XRenderQueryExtension(display, &eventBase, &errorBase)) {
	return BadDrawable;
    }

    /*
     * One round trip: only a Drawable is passed in, so ask the server for
     * its depth (and its root window, to identify the screen).
     */

    if (!XGetGeometry(display, drawable, &root, &x, &y, &gw, &gh, &bw,
	    &depth)) {
	return BadDrawable;
    }
    for (i = 0; i < ScreenCount(display); i++) {
	if (RootWindow(display, i) == root) {
	    screen = i;
	    break;
	}
    }

    if (depth == 32) {
	dstFmt = XRenderFindStandardFormat(display, PictStandardARGB32);
    } else if (depth == 24) {
	dstFmt = XRenderFindStandardFormat(display, PictStandardRGB24);
    } else if (screen >= 0
	    && depth == (unsigned) DefaultDepth(display, screen)) {
	dstFmt = XRenderFindVisualFormat(display,
		DefaultVisual(display, screen));
    }
    srcFmt = XRenderFindStandardFormat(display, PictStandardARGB32);
    if (dstFmt == NULL || srcFmt == NULL) {
	return BadDrawable;
    }

    /*
     * Stage the premultiplied sub-rect in a depth-32 scratch pixmap.
     * TkPremultiplyRGBA writes B,G,R,A bytes; declared LSBFirst, the pixel
     * value is 0xAARRGGBB = PictStandardARGB32 regardless of client
     * endianness, and Xlib re-swaps for MSBFirst servers in XPutImage.  The
     * buffer is ckalloc'ed, so detach it before XDestroyImage and free it
     * ourselves.
     */

    buf = (char *) ckalloc((size_t) w * h * 4);
    TkPremultiplyRGBA(image, src_x, src_y, w, h, (unsigned char *) buf, w * 4);

    argb = XCreateImage(display, NULL, 32, ZPixmap, 0, buf,
	    width, height, 32, 4 * w);
    if (argb == NULL) {
	ckfree(buf);
	return BadDrawable;
    }
    argb->byte_order = LSBFirst;	/* Bytes were written B,G,R,A. */

    srcPix = XCreatePixmap(display, drawable, width, height, 32);
    srcGC = XCreateGC(display, srcPix, 0, NULL);
    XPutImage(display, srcPix, srcGC, argb, 0, 0, 0, 0, width, height);
    XFreeGC(display, srcGC);
    argb->data = NULL;
    XDestroyImage(argb);
    ckfree(buf);

    srcPic = XRenderCreatePicture(display, srcPix, srcFmt, 0, NULL);
    dstPic = XRenderCreatePicture(display, drawable, dstFmt, 0, NULL);
    XRenderComposite(display, PictOpOver, srcPic, None, dstPic,
	    0, 0, 0, 0, dest_x, dest_y, width, height);
    XRenderFreePicture(display, dstPic);
    XRenderFreePicture(display, srcPic);
    XFreePixmap(display, srcPix);
    return Success;
}
#endif /* HAVE_XRENDER */

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
