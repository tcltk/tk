#include "tkSDLInt.h"
#include "SdlTk.h"
#include "SdlTkInt.h"
#include "Xregion.h"

/*
 * Blit on same surface given source and destination rectangles.
 * This overcomes bugs in SDL2-2.0.0 when blit regions overlap.
 */

static void
BlitOneSurface(SDL_Surface *sdl, SDL_Rect *src0, SDL_Rect *dst0)
{
    int overlap;
    Uint8 *psrc, *pdst;
    int w, h;
    int pitch;
    SDL_Rect r[2], *src = &r[0], *dst = &r[1];;

    *src = *src0;
    *dst = *dst0;
    if (dst->x < 0) {
	dst->w += dst->x;
	src->x -= dst->x;
	dst->x = 0;
    }
    if (dst->y < 0) {
	dst->h += dst->y;
	src->y -= dst->y;
	dst->y = 0;
    }
    if (dst->x + dst->w > sdl->w) {
	dst->w = sdl->w - dst->x;
    }
    if (dst->y + dst->h > sdl->h) {
	dst->h = sdl->h - dst->y;
    }
    src->w = dst->w;
    src->h = dst->h;
    if (src->x + src->w < 0 || src->x >= sdl->w ||
	src->y + src->h < 0 || src->y >= sdl->h) {
	return;
    }
    if (src->x < 0) {
	dst->w += src->x;
	dst->x -= src->x;
	src->x = 0;
    }
    if (src->y < 0) {
	dst->h += src->y;
	dst->y -= src->y;
	src->y = 0;
    }
    if (src->x + src->w > sdl->w) {
	dst->w += sdl->w - (src->x + src->w);
    }
    if (src->y + src->h > sdl->h) {
	dst->h += sdl->h - (src->y + src->h);
    }
    if (dst->w <= 0 || dst->h <= 0) {
	return;
    }
    if (dst->x + dst->w > sdl->w || dst->x < 0 ||
	dst->y + dst->h > sdl->h || dst->y < 0) {
	return;
    }
    w = dst->w * sdl->format->BytesPerPixel;
    h = dst->h;
    pitch = sdl->pitch;
    psrc = (Uint8 *) sdl->pixels + src->y * pitch
         + src->x * sdl->format->BytesPerPixel;
    pdst = (Uint8 *) sdl->pixels + dst->y * pitch
         + dst->x * sdl->format->BytesPerPixel;

    if (psrc < pdst) {
        overlap = pdst < psrc + h * pitch;
    } else {
        overlap = psrc < pdst + h * pitch;
    }
    if (overlap) {
        if (psrc < pdst) {
	    psrc += h * pitch;
	    pdst += h * pitch;
	    while (h--) {
		psrc -= pitch;
		pdst -= pitch;
	        memmove(pdst, psrc, w);
	    }
	} else {
	    while (h--) {
	        memmove(pdst, psrc, w);
		psrc += pitch;
		pdst += pitch;
	    }
	}
    } else {
        while (h--) {
	    memcpy(pdst, psrc, w);
	    psrc += pitch;
	    pdst += pitch;
	}
    }
}

/*
 * Blit using third gray8 surface as mask.
 * Source/dest surfaces must have same format.
 */

static void
BlitWithMask(SDL_Surface *sdl1, SDL_Rect *src, SDL_Surface *sdl2,
	     SDL_Rect *dst, SDL_Surface *sdlmask)
{
    Uint8 *psrc, *pdst, *pmask;
    int w, h, inc;

    w = dst->w;
    inc = sdl2->format->BytesPerPixel;
    h = dst->h;
    psrc = (Uint8 *) sdl1->pixels + src->y * sdl1->pitch
         + src->x * sdl1->format->BytesPerPixel;
    pdst = (Uint8 *) sdl2->pixels + dst->y * sdl2->pitch
         + dst->x * sdl2->format->BytesPerPixel;
    pmask = (Uint8 *) sdlmask->pixels + src->y * sdlmask->pitch
          + src->x * sdlmask->format->BytesPerPixel;
    while (h--) {
        int x, xx;

	for (x = xx = 0; x < w; x++, xx += inc) {
	    if (pmask[x]) {
	        memcpy(pdst + xx, psrc + xx, inc);
	    }
	}
	psrc += sdl1->pitch;
	pdst += sdl2->pitch;
	pmask += sdlmask->pitch;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkPixelFormat --
 *
 *	Determine the pixel format of an SDL_Surface.
 *
 * Results:
 *	Returns one of the SDLTK_xxx enumeration values.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
SdlTkPixelFormat(SDL_Surface *sdl)
{
    int format = -1;

    switch (sdl->format->BitsPerPixel) {
    case 1:
	format = SDLTK_BITMAP;
	break;
    case 8:
	format = SDLTK_GRAY8;
	break;
    case 15:
	format = SDLTK_RGB555;
	break;
    case 16:
	format = (sdl->format->Rshift > sdl->format->Bshift) ?
	    SDLTK_RGB565 : SDLTK_BGR565;
	break;
    case 24:
	if (sdl->format->BytesPerPixel > 3) {
	    /* fall through */
	} else {
	    format = (sdl->format->Rshift > sdl->format->Bshift) ?
		SDLTK_RGB24 : SDLTK_BGR24;	/* !!! */
	    break;
	}
    case 32:
	if (sdl->format->Rshift > sdl->format->Bshift) {
	    format = sdl->format->Rshift ? SDLTK_BGRA32 : SDLTK_ABGR32;
	} else {
	    format = sdl->format->Bshift ? SDLTK_RGBA32 : SDLTK_ARGB32;
	}
	break;
    }
    if (format == -1) {
	Tcl_Panic("SdlTkPixelFormat: unsupported pixel format");
    }
    return format;
}

/*
 * Sort the rectangles in a region so the pixels contained by each rectangle
 * may be copied within a single surface without interference.
 */

static int
CmpBoxes(const void *p0, const void *p1)
{
    BoxPtr b0 = (BoxPtr) p0;
    BoxPtr b1 = (BoxPtr) p1;

    if (b0->y1 < b1->y1) {
        return -1;
    }
    if (b0->y1 == b1->y1 && b0->x1 < b1->x1) {
        return -1;
    }
    if (b0->y1 == b1->y1 && b0->x1 == b1->x1) {
        return 0;
    }
    return 1;
}

void
SdlTkGfxCopyArea(Drawable src, Drawable dest, GC gc,
		 int src_x, int src_y, unsigned int width, unsigned int height,
		 int dest_x, int dest_y)
{
    SDL_Surface *sdl1, *sdl2;
    SDL_Rect r1, r2;
    int xOff, yOff;
    long i;
    XEvent event;
    TkpClipMask *clipPtr = (TkpClipMask *) gc->clip_mask;
    REGION *rgn = NULL;
    Region tmpRgn = NULL;

    if (IS_WINDOW(dest)) {
	rgn = SdlTkGetVisibleRegion((_Window *) dest);
	if (XEmptyRegion(rgn)) {
	    if (IS_WINDOW(src) && gc->graphics_exposures) {
		event.xnoexpose.type = NoExpose;
		event.xnoexpose.serial = ((_Window *) src)->display->request;
		event.xnoexpose.send_event = False;
		event.xnoexpose.display = ((_Window *) src)->display;
		event.xnoexpose.drawable = dest;
		event.xnoexpose.major_code = 0;
		event.xnoexpose.minor_code = 0;
		SdlTkQueueEvent(&event);
	    }
	    return;
	}
    }

    r1.x = src_x;
    r1.y = src_y;

    r2.x = dest_x;
    r2.y = dest_y;

    r1.w = r2.w = width;
    r1.h = r2.h = height;

    sdl1 = SdlTkGetDrawableSurface(src, &xOff, &yOff, NULL);
    r1.x += xOff;
    r1.y += yOff;

    sdl2 = SdlTkGetDrawableSurface(dest, &xOff, &yOff, NULL);
    r2.x += xOff;
    r2.y += yOff;

    /*
     * If the clipping region is specified, intersect it with the visible
     * region of the dest window, or if this is a pixmap then use the clipping
     * region unmodified.
     */
    if (clipPtr && clipPtr->type == TKP_CLIP_REGION) {
	Region clipRgn = (Region) clipPtr->value.region;

	if (rgn) {
	    tmpRgn = SdlTkRgnPoolGet();
	    XIntersectRegion(rgn, clipRgn, tmpRgn);
	    rgn = tmpRgn;
	} else {
	    rgn = clipRgn;
	}
	xOff += gc->clip_x_origin;
	yOff += gc->clip_y_origin;
    }

    if (rgn) {
	/*
	 * When attempting to copy multiple areas within the same surface,
	 * care must be taken so those areas are copied in the correct
	 * order.
	 */
	if (src == dest) {
	    qsort(rgn->rects, rgn->numRects, sizeof (Box), CmpBoxes);
	    if ((r1.y < r2.y) || (r1.y == r2.y && r1.x < r2.x)) {
	        for (i = rgn->numRects - 1; i >= 0; i--) {
		    BoxPtr box = &rgn->rects[i];
		    SDL_Rect rr1 = r1, rr2, rr3;
		    rr2.x = xOff + box->x1;
		    rr2.y = yOff + box->y1;
		    rr2.w = box->x2 - box->x1;
		    rr2.h = box->y2 - box->y1;
		    if (SDL_IntersectRect(&r2, &rr2, &rr3)) {
		        rr1.x += rr3.x - r2.x;
			rr1.y += rr3.y - r2.y;
			rr1.w -= rr3.x - r2.x;
			rr1.h -= rr3.y - r2.y;
			BlitOneSurface(sdl1, &rr1, &rr3);
		    }
		}
	    } else {
	        for (i = 0; i < rgn->numRects; i++) {
		    BoxPtr box = &rgn->rects[i];
		    SDL_Rect rr1 = r1, rr2, rr3;
		    rr2.x = xOff + box->x1;
		    rr2.y = yOff + box->y1;
		    rr2.w = box->x2 - box->x1;
		    rr2.h = box->y2 - box->y1;
		    if (SDL_IntersectRect(&r2, &rr2, &rr3)) {
		        rr1.x += rr3.x - r2.x;
			rr1.y += rr3.y - r2.y;
			rr1.w -= rr3.x - r2.x;
			rr1.h -= rr3.y - r2.y;
			BlitOneSurface(sdl1, &rr1, &rr3);
		    }
		}
	    }
	} else {
	    for (i = 0; i < rgn->numRects; i++) {
		BoxPtr box = &rgn->rects[i];
		SDL_Rect sdl_rect, preserveR1 = r1, preserveR2 = r2;
		sdl_rect.x = xOff + box->x1;
		sdl_rect.y = yOff + box->y1;
		sdl_rect.w = box->x2 - box->x1;
		sdl_rect.h = box->y2 - box->y1;
		SDL_SetClipRect(sdl2, &sdl_rect);
		if ((rgn->numRects == 1) && !IS_WINDOW(src) &&
		    (sdl1->format->BytesPerPixel == sdl2->format->BytesPerPixel) &&
		    clipPtr && (clipPtr->type == TKP_CLIP_PIXMAP) &&
		    (((_Pixmap *) (clipPtr->value.pixmap))->format == SDLTK_GRAY8) &&
		    (gc->clip_x_origin == dest_x - src_x) &&
		    (gc->clip_y_origin == dest_y - src_y)) {
		    BlitWithMask(sdl1, &r1, sdl2, &r2,
				 ((_Pixmap *) clipPtr->value.pixmap)->sdl);
		} else {
		    SDL_BlitSurface(sdl1, &preserveR1,
				    sdl2, &preserveR2);
		}
	    }
	    SDL_SetClipRect(sdl2, NULL);
	}
    } else {
        if ((sdl1 != sdl2) && !IS_WINDOW(src) &&
	    (sdl1->format->BytesPerPixel == sdl2->format->BytesPerPixel) &&
	    clipPtr && (clipPtr->type == TKP_CLIP_PIXMAP) &&
	    (((_Pixmap *) (clipPtr->value.pixmap))->format == SDLTK_GRAY8) &&
	    (gc->clip_x_origin == dest_x - src_x) &&
	    (gc->clip_y_origin == dest_y - src_y)) {
  	    BlitWithMask(sdl1, &r1, sdl2, &r2,
			 ((_Pixmap *) clipPtr->value.pixmap)->sdl);
	} else if (sdl1 == sdl2) {
	    BlitOneSurface(sdl1, &r1, &r2);
	} else {
	    SDL_BlitSurface(sdl1, &r1, sdl2, &r2);
	}
    }

    if (tmpRgn) {
	SdlTkRgnPoolFree(tmpRgn);
    }

    if (IS_WINDOW(src) && gc->graphics_exposures) {
	REGION *visRgn = SdlTkGetVisibleRegion((_Window *) src);
	Region damageRgn = SdlTkRgnPoolGet();
	XRectangle rect;

	/*
	 * Create a region as big as the source area. This area may
	 * be larger than the source drawable.
	 */
	rect.x = src_x;
	rect.y = src_y;
	rect.width = width;
	rect.height = height;
	XUnionRectWithRegion(&rect, damageRgn, damageRgn);

	/*
	 * Subtract the visible region.
	 * This leaves holes where any child windows are and for any areas
	 * outside the source drawable.
	 */
	XSubtractRegion(damageRgn, visRgn, damageRgn);

	/* Convert the region to destination coordinates */
	/* *** assume dest=src *** */
	XOffsetRegion(damageRgn, dest_x - src_x, dest_y - src_y);

	/* Intersect with destination visible region */
	/* *** assume dest=src *** */
	XIntersectRegion(damageRgn, visRgn, damageRgn);

	if (XEmptyRegion(damageRgn)) {
	    event.xnoexpose.type = NoExpose;
	    event.xnoexpose.serial = ((_Window *) src)->display->request;
	    event.xnoexpose.send_event = False;
	    event.xnoexpose.display = ((_Window *) src)->display;
	    event.xnoexpose.drawable = dest;
	    event.xnoexpose.major_code = 0;
	    event.xnoexpose.minor_code = 0;
	    SdlTkQueueEvent(&event);
	} else {
	    XGraphicsExposeEvent *e = &event.xgraphicsexpose;

	    e->type = GraphicsExpose;
	    e->serial = ((_Window *) src)->display->request;
	    e->send_event = False;
	    e->display = ((_Window *) src)->display;
	    e->drawable = dest;
	    e->major_code = 0;
	    e->minor_code = 0;
	    for (i = 0; i < damageRgn->numRects; i++) {
		BoxPtr box = &damageRgn->rects[i];
		e->x = box->x1;
		e->y = box->y1;
		e->width = box->x2 - box->x1;
		e->height = box->y2 - box->y1;
		e->count = damageRgn->numRects - i - 1;
		SdlTkQueueEvent(&event);
	    }
	}
	SdlTkRgnPoolFree(damageRgn);
    }

}

void
SdlTkGfxBlitRegion(SDL_Surface *src, Region rgn, SDL_Surface *dest,
		   int dest_x, int dest_y)
{
    long i;

    for (i = 0; i < rgn->numRects; i++) {
	BoxPtr box = &rgn->rects[i];
	SDL_Rect r1, r2;

	r1.x = box->x1;
	r1.y = box->y1;
	r1.w = box->x2 - box->x1;
	r1.h = box->y2 - box->y1;
	r2.x = dest_x + r1.x;
	r2.y = dest_y + r1.y;
	(void) SDL_BlitSurface(src, &r1, dest, &r2);
    }
}

void
SdlTkGfxFillRegion(Drawable d, Region rgn, Uint32 pixel)
{
    SDL_Surface *sdl;
    int xOff = 0, yOff = 0;
    long i;

    sdl = SdlTkGetDrawableSurface(d, &xOff, &yOff, NULL);
    if (sdl == NULL) {
	return;
    }

    for (i = 0; i < rgn->numRects; i++) {
	BoxPtr box = &((REGION *)rgn)->rects[i];
	SDL_Rect sdl_rect;

	sdl_rect.x = box->x1 + xOff;
	sdl_rect.y = box->y1 + yOff;
	sdl_rect.w = box->x2 - box->x1;
	sdl_rect.h = box->y2 - box->y1;
	SDL_FillRect(sdl, &sdl_rect, pixel);
    }
}

int
SdlTkGfxClearRegion(Window w, Region dirtyRgn)
{
    _Window *_w = (_Window *) w;
    int filled = 0;

    if (XEmptyRegion(dirtyRgn)) {
	return filled;
    }
    if (_w->back_pixel_set) {
	SdlTkGfxFillRegion((Drawable) w, dirtyRgn, _w->back_pixel);
	filled = 1;
    } else if (_w->back_pixmap == (_Pixmap *) ParentRelative) {
	_Window *parent = _w->parent;

	while (!IS_ROOT(parent)) {
	    if (parent->back_pixel_set) {
		SdlTkGfxFillRegion((Drawable) w, dirtyRgn, parent->back_pixel);
		filled = 1;
		break;
	    }
	    parent = parent->parent;
	}
    }
    if (filled) {
	SdlTkDirtyRegion((Drawable) w, dirtyRgn);
    }
    return filled;
}

int
SdlTkGfxExposeRegion(Window w, Region dirtyRgn)
{
    long i;
    int count;

    if (!(((_Window *) w)->atts.your_event_mask & ExposureMask)) {
	return 0;
    }
    if (XEmptyRegion(dirtyRgn)) {
	return 0;
    }
    count = dirtyRgn->numRects - 1;
    for (i = 0; i < dirtyRgn->numRects; i++, count--) {
	BoxPtr box = &((REGION *) dirtyRgn)->rects[i];

	SdlTkGenerateExpose(w, box->x1, box->y1,
			    box->x2 - box->x1, box->y2 - box->y1, count);
    }
    return dirtyRgn->numRects;
}

void
SdlTkGfxUpdateRegion(SDL_Renderer *rend, SDL_Texture *tex,
		     SDL_Surface *surf, Region rgn)
{
#ifdef ANDROID
    BoxPtr box = &((REGION *) rgn)->extents;
    SDL_Rect rect[1];
    void *src;

    rect[0].w = box->x2 - box->x1;
    rect[0].h = box->y2 - box->y1;
    rect[0].x = box->x1;
    rect[0].y = box->y1;
    if (rect[0].x < 0) {
	rect[0].x = 0;
    }
    if (rect[0].y < 0) {
	rect[0].y = 0;
    }
    if (rect[0].x + rect[0].w > surf->w) {
	rect[0].w = surf->w - rect[0].x;
    }
    if (rect[0].y + rect[0].h > surf->h) {
	rect[0].h = surf->h - rect[0].y;
    }
    if ((rect[0].w > 0) && (rect[0].h > 0)) {
	src = (void *)((Uint8 *) surf->pixels +
		       rect[0].y * surf->pitch +
		       rect[0].x * surf->format->BytesPerPixel);
	SDL_UpdateTexture(tex, rect, src, surf->pitch);
    }
    if (SdlTkX.draw_later & SDLTKX_RENDCLR) {
	SdlTkX.draw_later &= ~SDLTKX_RENDCLR;
	SDL_SetRenderDrawColor(SdlTkX.sdlrend, 0, 0, 0, 255);
	SDL_RenderClear(SdlTkX.sdlrend);
    }
    SDL_RenderCopy(rend, tex, &SdlTkX.viewport, SdlTkX.outrect);
    SDL_RenderPresent(rend);
#else
    long i;
    int count = 0;

    if ((SdlTkX.draw_later & (SDLTKX_SCALED | SDLTKX_RENDCLR))
	== SDLTKX_RENDCLR) {
	SdlTkX.draw_later &= ~SDLTKX_RENDCLR;
	SdlTkX.draw_later |= SDLTKX_PRESENT;
	SDL_SetRenderDrawColor(SdlTkX.sdlrend, 0, 0, 0, 255);
	SDL_RenderClear(SdlTkX.sdlrend);
    }
    for (i = 0; i < rgn->numRects; i++) {
	BoxPtr box = &((REGION *) rgn)->rects[i];
	SDL_Rect rect[1];
	void *src;

	rect[0].w = box->x2 - box->x1;
	rect[0].h = box->y2 - box->y1;
	rect[0].x = box->x1;
	rect[0].y = box->y1;
	if (rect[0].x < 0) {
	    rect[0].x = 0;
	}
	if (rect[0].y < 0) {
	    rect[0].y = 0;
	}
	if (rect[0].x + rect[0].w > surf->w) {
	    rect[0].w = surf->w - rect[0].x;
	}
	if (rect[0].y + rect[0].h > surf->h) {
	    rect[0].h = surf->h - rect[0].y;
	}
	if ((rect[0].w <= 0) && (rect[0].h <= 0)) {
	    continue;
	}
        src = (void *)((Uint8 *) surf->pixels +
                        rect[0].y * surf->pitch +
                        rect[0].x * surf->format->BytesPerPixel);
	SDL_UpdateTexture(tex, rect, src, surf->pitch);
	if (!(SdlTkX.draw_later & SDLTKX_SCALED)) {
	    SDL_Rect *orect = rect, orect0[1];

	    if (SdlTkX.outrect) {
		orect = orect0;
		orect[0] = rect[0];
		orect[0].x += SdlTkX.outrect->x;
		orect[0].y += SdlTkX.outrect->y;
	    }
	    SDL_RenderCopy(rend, tex, rect, orect);
	}
	count++;
    }
    if (count || (SdlTkX.draw_later & SDLTKX_PRESENT)) {
	if (SdlTkX.draw_later & SDLTKX_RENDCLR) {
	    SdlTkX.draw_later &= ~SDLTKX_RENDCLR;
	    SDL_SetRenderDrawColor(SdlTkX.sdlrend, 0, 0, 0, 255);
	    SDL_RenderClear(SdlTkX.sdlrend);
	}
	SDL_RenderCopy(rend, tex, &SdlTkX.viewport, SdlTkX.outrect);
	SDL_RenderPresent(rend);
    }
#endif
}

void
SdlTkGfxPresent(SDL_Renderer *rend, SDL_Texture *tex)
{
    if (SdlTkX.draw_later & SDLTKX_RENDCLR) {
	SdlTkX.draw_later &= ~SDLTKX_RENDCLR;
	SDL_SetRenderDrawColor(SdlTkX.sdlrend, 0, 0, 0, 255);
	SDL_RenderClear(SdlTkX.sdlrend);
    }
    SDL_RenderCopy(rend, tex, &SdlTkX.viewport, SdlTkX.outrect);
    SDL_RenderPresent(rend);
}

unsigned long
SdlTkImageGetPixel(XImage *image, int x, int y)
{
    unsigned long pixel = 0;
    unsigned char *srcPtr =
	(unsigned char *) &(image->data[(y * image->bytes_per_line)
	    + ((x * image->bits_per_pixel) / NBBY)]);

    switch (image->bits_per_pixel) {
    case 1:
	pixel = ((*srcPtr) & (0x80 >> (x%8))) ? 1 : 0;
	break;
    case 8:
	pixel = *srcPtr;
	break;
    case 15:
    case 16:
	pixel = *(Uint16 *) srcPtr;
	break;
    case 24:
	if (image->red_mask > image->blue_mask) {
	    pixel = srcPtr[2] | (srcPtr[1] << 8) | (srcPtr[0] << 16);
	} else {
	    pixel = srcPtr[0] | (srcPtr[1] << 8) | (srcPtr[2] << 16);
	}
	break;
    case 32:
	pixel = *(Uint32 *) srcPtr;
	break;
    }
    return pixel;
}

/* screen visual format */

int
SdlTkImagePutPixel(XImage *image, int x, int y, unsigned long pixel)
{
    unsigned char *destPtr =
	(unsigned char *) &(image->data[(y * image->bytes_per_line)
	    + ((x * image->bits_per_pixel) / NBBY)]);

    switch (image->bits_per_pixel) {
    case 1: {
	int mask = (0x80 >> (x%8));
	if (pixel) {
	    (*destPtr) |= mask;
	} else {
	    (*destPtr) &= ~mask;
	}
	break;
    }
    case 8:
	*destPtr = pixel;
	break;
    case 15:
    case 16:
	*((Uint16 *) destPtr) = (Uint16) pixel & 0xFFFF;
	break;
    case 24:
	if (image->red_mask > image->blue_mask) {
	    destPtr[0] = (pixel >> 16) & 0xff;
	    destPtr[1] = (pixel >> 8) & 0xff;
	    destPtr[2] = pixel & 0xff;
	} else {
	    destPtr[0] = pixel & 0xff;
	    destPtr[1] = (pixel >> 8) & 0xff;
	    destPtr[2] = (pixel >> 16) & 0xff;
	}
	break;
    case 32:
	*((Uint32 *) destPtr) = pixel;
	break;
    }
    return 0;
}

int
SdlTkImageDestroy(XImage *image)
{
    if (image->data != NULL) {
	ckfree((char *) image->data);
    }
    ckfree((char *) image);
    return 0;
}

void
SdlTkGfxPutImage(Drawable d, Region r, XImage* image, int src_x, int src_y,
		 int dest_x, int dest_y,
		 unsigned int width, unsigned int height, int flipbw)
{
    void *pixels = image->data;
    char *alignedData = 0;
    SDL_Surface *sdl;

    /* If this is a bitmap, swap bytes if needed */
    if ((image->bits_per_pixel == 1) && (image->bitmap_bit_order != MSBFirst)) {
	alignedData = TkAlignImageData(image, 1, MSBFirst);
	pixels = alignedData;
    }

    /*
     * Create a new surface that points to the image data. No copy of the
     * data is made.
     */
    if (image->bits_per_pixel == 8) {
	sdl = SDL_CreateRGBSurfaceFrom(pixels, image->width, image->height,
				       image->bits_per_pixel,
				       image->bytes_per_line, 0, 0, 0, 0);
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
    } else {
	sdl = SDL_CreateRGBSurfaceFrom(pixels, image->width, image->height,
				       image->bits_per_pixel,
				       image->bytes_per_line, image->red_mask,
				       image->green_mask, image->blue_mask, 0);

	if (image->bits_per_pixel == 1 && flipbw) {
	    SDL_Palette *pal = SDL_AllocPalette(2);
	    SDL_Color colors[2];

	    colors[0].r = colors[0].g = colors[0].b = 255;
	    colors[1].r = colors[1].g = colors[1].b = 0;
	    colors[0].a = colors[1].a = 255;
	    SDL_SetPaletteColors(pal, colors, 0, 2);
	    SDL_SetSurfacePalette(sdl, pal);
	    SDL_FreePalette(pal);
	}
    }

    if (sdl != NULL) {
	XGCValues fakeGC;
	_Pixmap _p;
	TkpClipMask tkpcm;

	memset(&fakeGC, 0, sizeof (fakeGC));
	/* Create a fake pixmap from the surface we made */
	_p.type = DT_PIXMAP;
	_p.sdl = sdl;
	if (r) {
	    fakeGC.clip_mask = (Pixmap) &tkpcm;
	    tkpcm.type = TKP_CLIP_REGION;
	    tkpcm.value.region = (TkRegion) r;
	} else {
	    fakeGC.clip_mask = None;
	}
	fakeGC.graphics_exposures = False;

	/*
	 * Straightforward blit from pixmap holding the image data to the
	 * destination drawable.
	 */
	SdlTkGfxCopyArea((Pixmap) &_p, d, &fakeGC, src_x, src_y, width, height,
	    dest_x, dest_y);

	SDL_FreeSurface(sdl);
    }

    if (alignedData) {
	ckfree(alignedData);
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
