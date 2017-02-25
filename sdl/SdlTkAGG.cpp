extern "C" {
#include "tkSDLInt.h"
#include "tkFont.h"
}
#include "SdlTkInt.h"
#include "Xregion.h"

#ifdef AGG_CUSTOM_ALLOCATOR
#include <new>
#endif
#include "agg_bezier_arc.h"
#include "agg_conv_curve.h"
#include "agg_ellipse.h"
#include "agg_font_freetype.h"
#ifndef AGG23
#include "agg_image_accessors.h"
#endif
#include "agg_pixfmt_rgb.h"
#include "agg_pixfmt_rgb_packed.h"
#include "agg_pixfmt_rgba.h"
#include "agg_pixfmt_gray.h"
#include "agg_renderer_base.h"
#include "agg_renderer_mclip.h"
#include "agg_renderer_primitives.h"
#include "agg_renderer_outline_aa.h"
#include "agg_rasterizer_outline_aa.h"
#include "agg_rasterizer_scanline_aa.h"
#include "agg_scanline_u.h"
#include "agg_span_pattern_rgba.h"
#include "agg_span_allocator.h"
#include "agg_renderer_scanline.h"
#include "agg_conv_stroke.h"
#include "agg_conv_dash.h"
#include "agg_vcgen_stroke.h"
#include "agg_vcgen_dash.h"

#ifdef AGG23

/*
 * This is a span generator. It is used when drawing text and primitives
 * using a Bitmap as a stipple pattern.
 */
template<class ColorT,
	class Order, 
	class WrapModeX,
	class WrapModeY,
	class Allocator = agg::span_allocator<ColorT> > 
class span_stipple : public agg::span_pattern_rgba<ColorT, Order, WrapModeX, WrapModeY, Allocator>
{
public:
    typedef ColorT color_type;
    typedef Order order_type;
    typedef Allocator alloc_type;
    typedef agg::span_pattern_rgba<color_type, order_type, WrapModeX, WrapModeY, alloc_type> base_type;
    typedef typename color_type::value_type value_type;

    span_stipple(alloc_type& alloc,
	const agg::rendering_buffer& src, 
	unsigned offset_x, unsigned offset_y) :
            base_type(alloc, src, offset_x, offset_y),
            m_wrap_mode_x(src.width()),
            m_wrap_mode_y(src.height())
    {}

    //--------------------------------------------------------------------
    color_type* generate(int x, int y, unsigned len)
    {   
	color_type* span = base_type::allocator().span();
	unsigned sx = m_wrap_mode_x(x - base_type::offset_x());
	const value_type* row_ptr = 
	    (const value_type*)base_type::base_type::source_image().row(
		m_wrap_mode_y(
		    y - base_type::offset_y()));
	do
	{
	    const value_type* p = row_ptr + sx; /* 1 byte-per-pixel */
	    if (p[0]) {
		*span = m_color;
	    } else {
		span->clear();
	    }
	    sx = ++m_wrap_mode_x;
	    ++span;
	}
	while(--len);
	return base_type::allocator().span();
    }

    //---------------------------------------------------------------------
    void color(const color_type& c) { m_color = c; }
    const color_type& color() const { return m_color; }

private:
    color_type m_color;
    WrapModeX m_wrap_mode_x;
    WrapModeY m_wrap_mode_y;
};

#else

/* Needed since stipple bitmap is 1 byte per pixel */

namespace agg
{
    //-----------------------------------------------------image_accessor_wrap_gray8
    template<class PixFmt, class WrapX, class WrapY> class image_accessor_wrap_gray8
    {
    public:
        typedef PixFmt   pixfmt_type;
        typedef typename pixfmt_type::color_type color_type;
        typedef typename pixfmt_type::order_type order_type;
        typedef typename pixfmt_type::value_type value_type;

        image_accessor_wrap_gray8() {}
        explicit image_accessor_wrap_gray8(const pixfmt_type& pixf) : 
            m_pixf(&pixf), 
            m_wrap_x(pixf.width()), 
            m_wrap_y(pixf.height())
        {}

        void attach(const pixfmt_type& pixf)
        {
            m_pixf = &pixf;
        }

        AGG_INLINE const int8u* span(int x, int y, unsigned)
        {
            m_x = x;
            m_row_ptr = m_pixf->row_ptr(m_wrap_y(y));
            return m_row_ptr + m_wrap_x(x);
        }

        AGG_INLINE const int8u* next_x()
        {
            int x = ++m_wrap_x;
            return m_row_ptr + x;
        }

        AGG_INLINE const int8u* next_y()
        {
            m_row_ptr = m_pixf->row_ptr(++m_wrap_y);
            return m_row_ptr + m_wrap_x(m_x);
        }

    private:
        const pixfmt_type* m_pixf;
        const int8u*       m_row_ptr;
        int                m_x;
        WrapX              m_wrap_x;
        WrapY              m_wrap_y;
    };
}

/*
 * This is a span generator. It is used when drawing text and primitives
 * using a Bitmap as a stipple pattern.
 */
template<class Source>
class span_stipple : public agg::span_pattern_rgba<Source>
{
public:
    typedef Source source_type;
    typedef typename source_type::color_type color_type;
    typedef typename source_type::order_type order_type;
    typedef typename color_type::value_type value_type;
    typedef typename color_type::calc_type calc_type;

    span_stipple(source_type& src,
		 unsigned offset_x, unsigned offset_y) :
	    m_src(&src),
            m_offset_x(offset_x),
            m_offset_y(offset_y)
    {}

    //--------------------------------------------------------------------
    void generate(color_type* span, int x, int y, unsigned len)
    {   
	x += m_offset_x;
	y += m_offset_y;
	const Uint8* p = (const Uint8*)m_src->span(x, y, 1);
	do {
	    if (p[0]) {
		*span = m_color;
	    } else {
		span->clear();
	    }
	    p = m_src->next_x();
	    ++span;
	} while(--len);
    }


    //---------------------------------------------------------------------
    void color(const color_type& c) { m_color = c; }
    const color_type& color() const { return m_color; }

private:
    source_type* m_src;
    color_type   m_color;
    unsigned     m_offset_x;
    unsigned     m_offset_y;
};

#endif

template<class PixelFormat>
void
doDrawArc(Drawable d, GC gc, int x, int y,
    unsigned int width, unsigned int height, int start, int extent)
{
    SDL_Surface *sdl;
    int xOff = 0, yOff = 0;
    long i;
    REGION *rgn = 0;

    if (IS_WINDOW(d)) {
	rgn = SdlTkGetVisibleRegion((_Window *) d);

	/* Window is unmapped or totally obscured */
	if (XEmptyRegion(rgn)) {
	    return;
	}
    }

    sdl = SdlTkGetDrawableSurface(d, &xOff, &yOff, NULL);

    /* Lock surface */
    if (SDL_MUSTLOCK(sdl)) {
	if (SDL_LockSurface(sdl) < 0) {
	    return;
	}
    }

    /* Rendering buffer, points to SDL_Surface memory */
    agg::rendering_buffer rbuf((agg::int8u *) sdl->pixels, sdl->w, sdl->h, sdl->pitch);

    /* Pixel-format renderer, a low-level pixel-rendering object */
    PixelFormat ren_pixf(rbuf);

    /* A basic renderer that does clipping to multiple boxes */
    typedef agg::renderer_mclip<PixelFormat> t_renderer_mclip;
    t_renderer_mclip ren_mclip(ren_pixf);

    /* The color, in the format used by the pixel-format renderer */
    Uint8 r, g, b;
    SDL_GetRGB(gc->foreground, SdlTkX.sdlsurf->format, &r, &g, &b);
    agg::rgba8 c(r, g, b);

    /* Apparently agg::arc is deprecated */
    agg::bezier_arc arc(xOff + x + width / 2.0, yOff + y + height / 2.0,
	width / 2.0, height / 2.0,
	agg::deg2rad(start / 64.0), agg::deg2rad(extent / 64.0));
    typedef agg::conv_curve<agg::bezier_arc, agg::curve3_div, agg::curve4_div> t_conv_curve;
    t_conv_curve curve(arc);

    /* Thing that generates scanlines */
    agg::rasterizer_scanline_aa<> rasterizer;
    rasterizer.reset();
    if (((unsigned) gc->line_width >= width / 2) ||
        ((unsigned) gc->line_width >= height / 2)) {
	rasterizer.add_path(curve);
    } else {
	agg::conv_stroke<t_conv_curve> stroke(curve);
	stroke.width((double) gc->line_width);
	rasterizer.add_path(stroke);
    }

    /* Scanline needed by the rasterizer -> renderer */
    agg::scanline_u8 scanline;

    if (rgn) {
	for (i = 0; i < rgn->numRects; i++) {
	    BoxPtr box = &rgn->rects[i];
	    ren_mclip.add_clip_box(xOff + box->x1, yOff + box->y1,
		xOff + box->x2 - 1, yOff + box->y2 - 1);
	}
    }

    /* FIXME: FillOpaqueStippled not implemented */
    if ((gc->fill_style == FillStippled
	    || gc->fill_style == FillOpaqueStippled)
	    && gc->stipple != None) {

	_Pixmap *stipple = (_Pixmap *) gc->stipple;

	/* Rendering buffer that points to the bitmap */
	agg::rendering_buffer stipple_buf((agg::int8u *) stipple->sdl->pixels,
	    stipple->sdl->w, stipple->sdl->h, stipple->sdl->pitch);

	/* A span allocator holds 1 line of pixels */
	agg::span_allocator<agg::rgba8> span_allocator;

	/* Generates spans (lines of pixels) from a source buffer */
#ifdef AGG23
	typedef span_stipple<agg::rgba8, agg::order_argb,
	    agg::wrap_mode_repeat, agg::wrap_mode_repeat> t_span_stipple;
	t_span_stipple span_stipple(span_allocator, stipple_buf, gc->ts_x_origin, gc->ts_y_origin);
	span_stipple.color(c);

	typedef agg::renderer_scanline_aa<t_renderer_mclip, t_span_stipple> t_renderer_scanline_aa;
	t_renderer_scanline_aa ren_scanline_aa(ren_mclip, span_stipple);

	agg::render_scanlines(rasterizer, scanline, ren_scanline_aa);
#else
	typedef agg::image_accessor_wrap_gray8<PixelFormat,
	    agg::wrap_mode_repeat, agg::wrap_mode_repeat> img_src_type;
	PixelFormat src_pixf(stipple_buf);
	img_src_type img_src(src_pixf);

	typedef span_stipple<img_src_type> t_span_stipple;
	t_span_stipple span_stipple(img_src, gc->ts_x_origin, gc->ts_y_origin);
	span_stipple.color(c);

	typedef agg::renderer_scanline_aa<t_renderer_mclip,
	    agg::span_allocator<agg::rgba8>, t_span_stipple> t_renderer_scanline_aa;
	t_renderer_scanline_aa ren_scanline_aa(ren_mclip, span_allocator, span_stipple);

	agg::render_scanlines(rasterizer, scanline, ren_scanline_aa);
#endif
    } else {
	agg::renderer_scanline_aa_solid<t_renderer_mclip> ren_scanline(ren_mclip);
	ren_scanline.color(c);

	agg::render_scanlines(rasterizer, scanline, ren_scanline);
    }

    /* Unlock surface */
    if (SDL_MUSTLOCK(sdl)) {
	SDL_UnlockSurface(sdl);
    }
}

void
SdlTkGfxDrawArc(Drawable d, GC gc, int x, int y,
    unsigned int width, unsigned int height, int start, int extent)
{
    SDL_Surface *sdl;
    int format;

    sdl = SdlTkGetDrawableSurface(d, NULL, NULL, &format);
    if (sdl == NULL) {
	return;
    }
    start = -start;
    extent = -extent;

    switch (format) {
    case SDLTK_RGB565:
	doDrawArc<agg::pixfmt_rgb565>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_BGR565:
	doDrawArc<agg::pixfmt_bgr565>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_RGB24:
	doDrawArc<agg::pixfmt_rgb24>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_BGR24:
	doDrawArc<agg::pixfmt_bgr24>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_RGBA32:
	doDrawArc<agg::pixfmt_rgba32>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_ARGB32:
	doDrawArc<agg::pixfmt_argb32>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_BGRA32:
	doDrawArc<agg::pixfmt_bgra32>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_ABGR32:
	doDrawArc<agg::pixfmt_abgr32>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_RGB555:
	doDrawArc<agg::pixfmt_rgb555>(d, gc, x, y, width, height, start, extent);
	break;
    }
}

template<class PixelFormat>
void
doDrawBitmap(
    Drawable src,
    Drawable dest,
    GC gc,
    int src_x, int src_y,
    unsigned int width, unsigned int height,
    int dest_x, int dest_y)
{
    SDL_Surface *sdl;
    int xOff = 0, yOff = 0;
    long i;
    REGION *rgn = 0;
    Region tmpRgn = 0;
    TkpClipMask *clipPtr = (TkpClipMask *) gc->clip_mask;

    if (IS_WINDOW(dest)) {
	rgn = SdlTkGetVisibleRegion((_Window *) dest);

	/* Window is unmapped or totally obscured */
	if (XEmptyRegion(rgn)) {
	    return;
	}
    }

    sdl = SdlTkGetDrawableSurface(dest, &xOff, &yOff, NULL);

    /* Lock surface */
    if (SDL_MUSTLOCK(sdl)) {
	if (SDL_LockSurface(sdl) < 0) {
	    return;
	}
    }

    /* Rendering buffer, points to SDL_Surface memory */
    agg::rendering_buffer rbuf((agg::int8u *) sdl->pixels, sdl->w, sdl->h, sdl->pitch);

    /* Pixel-format renderer, a low-level pixel-rendering object */
    /* The pixel format should match that of the SDL_Surface */
    PixelFormat ren_pixf(rbuf);

    /* A basic renderer that does clipping to multiple boxes */
    typedef agg::renderer_mclip<PixelFormat> t_renderer_mclip;
    t_renderer_mclip ren_mclip(ren_pixf);

    /*
     * If the clipping region is specified, intersect it with the visible
     * region of the window, or if this is a pixmap then use the clipping
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
    }

    if (rgn) {
	for (i = 0; i < rgn->numRects; i++) {
	    BoxPtr box = &rgn->rects[i];
	    ren_mclip.add_clip_box(xOff + box->x1, yOff + box->y1,
		xOff + box->x2 - 1, yOff + box->y2 - 1);
	}
    }

    /* The color, in the format used by the pixel-format renderer */
    Uint8 r, g, b;
    SDL_GetRGB(gc->foreground, SdlTkX.sdlsurf->format, &r, &g, &b);
    agg::rgba8 fg(r, g, b);

    SDL_GetRGB(gc->background, SdlTkX.sdlsurf->format, &r, &g, &b);
    agg::rgba8 bg(r, g, b);
    bool transparent = true;
    Uint8 *mpixels = NULL;

    if (clipPtr && clipPtr->type == TKP_CLIP_PIXMAP) {
	if (clipPtr->value.pixmap == src) {
	    transparent = true;
	} else {
	    if (((_Pixmap *) clipPtr->value.pixmap)->sdl->pitch ==
		((_Pixmap *) src)->sdl->pitch &&
		((_Pixmap *) clipPtr->value.pixmap)->sdl->h ==
		((_Pixmap *) src)->sdl->h) {
		mpixels = (Uint8 *)
		    ((_Pixmap *) clipPtr->value.pixmap)->sdl->pixels;
	    }
	}
    } else {
	transparent = false;
    }

    Uint8 *pixels = (Uint8 *) ((_Pixmap *) src)->sdl->pixels;
    int pitch = ((_Pixmap *) src)->sdl->pitch;
    unsigned int x, y;
    for (y = src_y; y < src_y + height; y++) {
	Uint8 *row = pixels + y * pitch;
	Uint8 *mrow = (mpixels != NULL) ? (mpixels + y * pitch) : NULL;
	for (x = src_x; x < src_x + width; x++) {
	    if (transparent) {
		if (mrow) {
		    if (mrow[x]) {
			ren_mclip.copy_pixel(xOff + dest_x + (x - src_x),
			    yOff + dest_y + (y - src_y), row[x] ? fg : bg);
		    }
		} else {
		    if (row[x]) {
			ren_mclip.copy_pixel(xOff + dest_x + (x - src_x),
			    yOff + dest_y + (y - src_y), fg);
		    }
		}
	    } else {
		ren_mclip.copy_pixel(xOff + dest_x + (x - src_x),
		    yOff + dest_y + (y - src_y), row[x] ? fg : bg);
	    }
	}
    }

    if (tmpRgn) {
	SdlTkRgnPoolFree(tmpRgn);
    }

    /* Unlock surface */
    if (SDL_MUSTLOCK(sdl)) {
	SDL_UnlockSurface(sdl);
    }
}

void
SdlTkGfxDrawBitmap(
    Drawable src,
    Drawable dest,
    GC gc,
    int src_x, int src_y,
    unsigned int width, unsigned int height,
    int dest_x, int dest_y)
{
    SDL_Surface *sdl;
    int format;

    sdl = SdlTkGetDrawableSurface(dest, NULL, NULL, &format);
    if (sdl == NULL) {
	return;
    }

    switch (format) {
    case SDLTK_RGB565:
	doDrawBitmap<agg::pixfmt_rgb565>(src, dest, gc, src_x, src_y, width, height, dest_x, dest_y);
	break;
    case SDLTK_BGR565:
	doDrawBitmap<agg::pixfmt_bgr565>(src, dest, gc, src_x, src_y, width, height, dest_x, dest_y);
	break;
    case SDLTK_RGB24:
	doDrawBitmap<agg::pixfmt_rgb24>(src, dest, gc, src_x, src_y, width, height, dest_x, dest_y);
	break;
    case SDLTK_BGR24:
	doDrawBitmap<agg::pixfmt_bgr24>(src, dest, gc, src_x, src_y, width, height, dest_x, dest_y);
	break;
    case SDLTK_RGBA32:
	doDrawBitmap<agg::pixfmt_rgba32>(src, dest, gc, src_x, src_y, width, height, dest_x, dest_y);
	break;
    case SDLTK_ARGB32:
	doDrawBitmap<agg::pixfmt_argb32>(src, dest, gc, src_x, src_y, width, height, dest_x, dest_y);
	break;
    case SDLTK_BGRA32:
	doDrawBitmap<agg::pixfmt_bgra32>(src, dest, gc, src_x, src_y, width, height, dest_x, dest_y);
	break;
    case SDLTK_ABGR32:
	doDrawBitmap<agg::pixfmt_abgr32>(src, dest, gc, src_x, src_y, width, height, dest_x, dest_y);
	break;
    case SDLTK_RGB555:
	doDrawBitmap<agg::pixfmt_rgb555>(src, dest, gc, src_x, src_y, width, height, dest_x, dest_y);
	break;
    }
}

class VertexSource_XPoints {
public:
    VertexSource_XPoints(XPoint *points, int npoints, int xOff, int yOff) :
	m_points(points),
	m_npoints(npoints),
	m_xOff(xOff),
	m_yOff(yOff),
	m_idx(0)
    {
    }
    void rewind(unsigned path_id)
    {
	m_idx = 0;
    }
    unsigned vertex(double *x, double *y)
    {
	if (m_idx == 0) {
	    *x = m_xOff + m_points[m_idx].x;
	    *y = m_yOff + m_points[m_idx].y;
	    ++m_idx;
	    return agg::path_cmd_move_to;
	}
	if (m_idx < m_npoints) {
	    *x = m_xOff + m_points[m_idx].x;
	    *y = m_yOff + m_points[m_idx].y;
	    ++m_idx;
	    return agg::path_cmd_line_to;
	}
	return agg::path_cmd_stop;
    }
private:
    XPoint *m_points;
    int m_npoints;
    int m_xOff, m_yOff;
    int m_idx;
};

template<class PixelFormat>
void
doDrawLines(Drawable d, GC gc, XPoint *points, int npoints, int mode)
{
    SDL_Surface *sdl;
    int xOff = 0, yOff = 0;
    long i;
    REGION *rgn = 0;

    if (IS_WINDOW(d)) {
	rgn = SdlTkGetVisibleRegion((_Window *) d);

	/* Window is unmapped or totally obscured */
	if (XEmptyRegion(rgn)) {
	    return;
	}
    }

    sdl = SdlTkGetDrawableSurface(d, &xOff, &yOff, NULL);

    /* Lock surface */
    if (SDL_MUSTLOCK(sdl)) {
	if (SDL_LockSurface(sdl) < 0) {
	    return;
	}
    }

    /* Rendering buffer, points to SDL_Surface memory */
    agg::rendering_buffer rbuf((agg::int8u *) sdl->pixels, sdl->w, sdl->h, sdl->pitch);

    /* Pixel-format renderer, a low-level pixel-rendering object */
    PixelFormat ren_pixf(rbuf);

    /* A basic renderer that does clipping to multiple boxes */
    typedef agg::renderer_mclip<PixelFormat> t_renderer_mclip;
    t_renderer_mclip ren_mclip(ren_pixf);

    /* The color, in the format used by the pixel-format renderer */
    Uint8 r, g, b;
    SDL_GetRGB(gc->foreground, SdlTkX.sdlsurf->format, &r, &g, &b);
    agg::rgba8 c(r, g, b);

    /* Thing that generates scanlines */
    agg::rasterizer_scanline_aa<> rasterizer;
    rasterizer.reset();

    VertexSource_XPoints vertexSrc(points, npoints, xOff, yOff);

    Uint8 *dashes = (Uint8 *) &gc->dashes;

    if ((gc->line_style == LineOnOffDash) && dashes[0]) {
	agg::conv_dash<VertexSource_XPoints> dash(vertexSrc);
	agg::conv_stroke<agg::conv_dash<VertexSource_XPoints> > stroke(dash);
	unsigned dindex = 0, dlw;

	dlw = (gc->line_width <= 0) ? 1 : gc->line_width;
	dash.remove_all_dashes();
	while (dashes[dindex] && dashes[dindex + 1] &&
	       (dindex < sizeof (gc->dash_array))) {
	    dash.add_dash(dashes[dindex] * dlw, dashes[dindex + 1] * dlw);
	    dindex += 2;
	}
	dash.dash_start(gc->dash_offset);
	if (gc->line_width > 1) {
	    stroke.width((double) gc->line_width - 0.5);
	} else {
	    stroke.width(1);
	}
	if (gc->line_width >= 2) {
	    switch (gc->cap_style) {
	    case CapNotLast:
	    case CapButt:
		stroke.line_cap(agg::butt_cap);
		break;
	    case CapRound:
		stroke.line_cap(agg::round_cap);
		break;
	    default:
	 	stroke.line_cap(agg::square_cap);
		break;
	    }
	    switch (gc->join_style) {
	    case JoinMiter:
		stroke.line_join(agg::miter_join);
		break;
	    case JoinRound:
		stroke.line_join(agg::round_join);
		break;
	    default:
		stroke.line_join(agg::bevel_join);
		break;
	    }
	}
	rasterizer.add_path(stroke);
    } else {
	agg::conv_stroke<VertexSource_XPoints> stroke(vertexSrc);

	if (gc->line_width > 1) {
	    stroke.width((double) gc->line_width - 0.5);
	} else {
	    stroke.width((double) gc->line_width);
	}
	if (gc->line_width >= 2) {
	    switch (gc->cap_style) {
	    case CapNotLast:
	    case CapButt:
		stroke.line_cap(agg::butt_cap);
		break;
	    case CapRound:
		stroke.line_cap(agg::round_cap);
		break;
	    default:
	 	stroke.line_cap(agg::square_cap);
		break;
	    }
	    switch (gc->join_style) {
	    case JoinMiter:
		stroke.line_join(agg::miter_join);
		break;
	    case JoinRound:
		stroke.line_join(agg::round_join);
		break;
	    default:
		stroke.line_join(agg::bevel_join);
		break;
	    }
	}
	rasterizer.add_path(stroke);
    }

    /* Scanline needed by the rasterizer -> renderer */
    agg::scanline_u8 scanline;

    if (rgn) {
	for (i = 0; i < rgn->numRects; i++) {
	    BoxPtr box = &rgn->rects[i];
	    ren_mclip.add_clip_box(xOff + box->x1, yOff + box->y1,
		xOff + box->x2 - 1, yOff + box->y2 - 1);
	}
    }

    if ((gc->fill_style == FillStippled
	    || gc->fill_style == FillOpaqueStippled)
	    && gc->stipple != None) {

	_Pixmap *stipple = (_Pixmap *) gc->stipple;

	/* Rendering buffer that points to the bitmap */
	agg::rendering_buffer stipple_buf((agg::int8u *) stipple->sdl->pixels,
	    stipple->sdl->w, stipple->sdl->h, stipple->sdl->pitch);

	/* A span allocator holds 1 line of pixels */
	agg::span_allocator<agg::rgba8> span_allocator;

	/* Generates spans (lines of pixels) from a source buffer */
#ifdef AGG23
	typedef span_stipple<agg::rgba8, agg::order_argb,
	    agg::wrap_mode_repeat, agg::wrap_mode_repeat> t_span_stipple;
	t_span_stipple span_stipple(span_allocator, stipple_buf, gc->ts_x_origin, gc->ts_y_origin);
	span_stipple.color(c);

	typedef agg::renderer_scanline_aa<t_renderer_mclip, t_span_stipple> t_renderer_scanline_aa;
	t_renderer_scanline_aa ren_scanline_aa(ren_mclip, span_stipple);

	agg::render_scanlines(rasterizer, scanline, ren_scanline_aa);
#else
	typedef agg::image_accessor_wrap_gray8<PixelFormat,
	    agg::wrap_mode_repeat, agg::wrap_mode_repeat> img_src_type;
	PixelFormat src_pixf(stipple_buf);
	img_src_type img_src(src_pixf);

	typedef span_stipple<img_src_type> t_span_stipple;
	t_span_stipple span_stipple(img_src, gc->ts_x_origin, gc->ts_y_origin);
	span_stipple.color(c);

	typedef agg::renderer_scanline_aa<t_renderer_mclip,
	    agg::span_allocator<agg::rgba8>, t_span_stipple> t_renderer_scanline_aa;
	t_renderer_scanline_aa ren_scanline_aa(ren_mclip, span_allocator, span_stipple);

	agg::render_scanlines(rasterizer, scanline, ren_scanline_aa);
#endif
    } else {
	typedef agg::renderer_scanline_aa_solid<t_renderer_mclip> t_renderer_scanline_aa_solid;
	t_renderer_scanline_aa_solid ren_scanline(ren_mclip);
	ren_scanline.color(c);

	agg::render_scanlines(rasterizer, scanline, ren_scanline);
    }

    /* Unlock surface */
    if (SDL_MUSTLOCK(sdl)) {
	SDL_UnlockSurface(sdl);
    }
}

void
SdlTkGfxDrawLines(Drawable d, GC gc, XPoint *points, int npoints, int mode)
{
    SDL_Surface *sdl;
    int format;

    sdl = SdlTkGetDrawableSurface(d, NULL, NULL, &format);
    if (sdl == NULL) {
	return;
    }

    switch (format) {
    case SDLTK_RGB565:
	doDrawLines<agg::pixfmt_rgb565>(d, gc, points, npoints, mode);
	break;
    case SDLTK_BGR565:
	doDrawLines<agg::pixfmt_bgr565>(d, gc, points, npoints, mode);
	break;
    case SDLTK_RGB24:
	doDrawLines<agg::pixfmt_rgb24>(d, gc, points, npoints, mode);
	break;
    case SDLTK_BGR24:
	doDrawLines<agg::pixfmt_bgr24>(d, gc, points, npoints, mode);
	break;
    case SDLTK_RGBA32:
	doDrawLines<agg::pixfmt_rgba32>(d, gc, points, npoints, mode);
	break;
    case SDLTK_ARGB32:
	doDrawLines<agg::pixfmt_argb32>(d, gc, points, npoints, mode);
	break;
    case SDLTK_BGRA32:
	doDrawLines<agg::pixfmt_bgra32>(d, gc, points, npoints, mode);
	break;
    case SDLTK_ABGR32:
	doDrawLines<agg::pixfmt_abgr32>(d, gc, points, npoints, mode);
	break;
    case SDLTK_RGB555:
	doDrawLines<agg::pixfmt_rgb555>(d, gc, points, npoints, mode);
	break;
    }
}

/* http://www.libsdl.org/cgi/docwiki.cgi/Pixel_20Access */
static void
putpixel(SDL_Surface *surface, int x, int y, Uint32 pixel)
{
    int bpp = surface->format->BytesPerPixel;
    /* Here p is the address to the pixel we want to set */
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch(bpp) {
    case 1:
        *p = pixel;
        break;

    case 2:
        *(Uint16 *)p = pixel;
        break;

    case 3:
        if(SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            p[0] = (pixel >> 16) & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = pixel & 0xff;
        } else {
            p[0] = pixel & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = (pixel >> 16) & 0xff;
        }
        break;

    case 4:
        *(Uint32 *)p = pixel;
        break;
    }
}

void
SdlTkGfxDrawPoint(Drawable d, GC gc, int x, int y)
{
    SDL_Surface *sdl;
    int xOff = 0, yOff = 0;
    REGION *rgn = 0;

    if (IS_WINDOW(d)) {
	rgn = SdlTkGetVisibleRegion((_Window *) d);

	/* Window is unmapped or totally obscured */
	if (XEmptyRegion(rgn)) {
	    return;
	}
    }

    sdl = SdlTkGetDrawableSurface(d, &xOff, &yOff, NULL);

    /* Lock surface */
    if (SDL_MUSTLOCK(sdl)) {
	if (SDL_LockSurface(sdl) < 0) {
	    return;
	}
    }

    if (x >= 0 && x < sdl->w && y >= 0 && y < sdl->h) {
	if (rgn) {
	    if (XPointInRegion(rgn, x, y)) {
		putpixel(sdl, x, y, gc->foreground);
	    }
	} else {
	    putpixel(sdl, x, y, gc->foreground);
	}
    }

    /* Unlock surface */
    if (SDL_MUSTLOCK(sdl)) {
	SDL_UnlockSurface(sdl);
    }
}

class VertexSource_XRectangle {
public:
    VertexSource_XRectangle(int x, int y, int w, int h) :
	m_idx(0)
    {
	m_rect.x = x, m_rect.y = y, m_rect.width = w, m_rect.height = h;
    }
    void rewind(unsigned path_id)
    {
	m_idx = 0;
    }
    unsigned vertex(double *x, double *y)
    {
	if (m_idx == 0) {
	    *x = m_rect.x;
	    *y = m_rect.y;
	    ++m_idx;
	    return agg::path_cmd_move_to;
	}
	if (m_idx == 1) {
	    *x = m_rect.x + m_rect.width;
	    *y = m_rect.y;
	    ++m_idx;
	    return agg::path_cmd_line_to;
	}
	if (m_idx == 2) {
	    *x = m_rect.x + m_rect.width;
	    *y = m_rect.y + m_rect.height;
	    ++m_idx;
	    return agg::path_cmd_line_to;
	}
	if (m_idx == 3) {
	    *x = m_rect.x;
	    *y = m_rect.y + m_rect.height;
	    ++m_idx;
	    return agg::path_cmd_line_to;
	}
	if (m_idx == 4) {
	    *x = m_rect.x;
	    *y = m_rect.y;
	    ++m_idx;
	    return agg::path_cmd_end_poly | agg::path_flags_close;
	}
	return agg::path_cmd_stop;
    }
private:
    XRectangle m_rect;
    int m_idx;
};

template<class PixelFormat>
void
doDrawRect(Drawable d, GC gc, int x, int y, int w, int h)
{
    SDL_Surface *sdl;
    int xOff = 0, yOff = 0;
    long i;
    REGION *rgn = 0;
    Region tmpRgn = 0;
    TkpClipMask *clipPtr = (TkpClipMask *) gc->clip_mask;

    if (IS_WINDOW(d)) {
	rgn = SdlTkGetVisibleRegion((_Window *) d);

	/* Window is unmapped or totally obscured */
	if (XEmptyRegion(rgn)) {
	    return;
	}
    }

    sdl = SdlTkGetDrawableSurface(d, &xOff, &yOff, NULL);
    x += xOff;
    y += yOff;

    /* Lock surface */
    if (SDL_MUSTLOCK(sdl)) {
	if (SDL_LockSurface(sdl) < 0) {
	    return;
	}
    }

    /* Rendering buffer, points to SDL_Surface memory */
    agg::rendering_buffer rbuf((agg::int8u *) sdl->pixels, sdl->w, sdl->h, sdl->pitch);

    /* Pixel-format renderer, a low-level pixel-rendering object */
    PixelFormat ren_pixf(rbuf);

    /* A basic renderer that does clipping to multiple boxes */
    typedef agg::renderer_mclip<PixelFormat> t_renderer_mclip;
    t_renderer_mclip ren_mclip(ren_pixf);

    /* The color, in the format used by the pixel-format renderer */
    Uint8 r, g, b;
    SDL_GetRGB(gc->foreground, SdlTkX.sdlsurf->format, &r, &g, &b);
    agg::rgba8 c(r, g, b);

#if 1
    /*
     * If the clipping region is specified, intersect it with the visible
     * region of the window, or if this is a pixmap then use the clipping
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
    }

    if (rgn) {
	for (i = 0; i < rgn->numRects; i++) {
	    BoxPtr box = &rgn->rects[i];
	    ren_mclip.add_clip_box(xOff + box->x1, yOff + box->y1,
		xOff + box->x2 - 1, yOff + box->y2 - 1);
	}
    }

    /*
     * A 1-pixel thick line is inside the top-left, but outside the
     * bottom-right (that is what Tk expects and how Win32 draws it)
     */
    if (gc->line_width == 1) {
	agg::renderer_primitives<t_renderer_mclip> ren_prim(ren_mclip);
	ren_prim.line_color(c);
	ren_prim.rectangle(x, y, x + w, y + h);

    /* This handles 1-pixel thick lines correctly but is slower */
    } else {
	int thick = gc->line_width;
	int half = thick / 2;
	int noDups = thick; /* to avoid drawing pixels twice */
	ren_mclip.copy_bar(
	    x - half,
	    y - half,
	    x + w - half + thick - 1,
	    y - half + thick - 1, c); /* top */
	ren_mclip.copy_bar(
	    x - half,
	    y + h - half,
	    x + w - half + thick - 1,
	    y + h - half + thick - 1, c); /* bottom */
	ren_mclip.copy_bar(
	    x - half,
	    y - half + noDups,
	    x - half + thick - 1,
	    y + h - half + thick - 1 - noDups, c); /* left */
	ren_mclip.copy_bar(
	    x + w - half,
	    y - half + noDups,
	    x + w - half + thick - 1,
	    y + h - half + thick - 1 - noDups, c); /* right */
    }

#else /* aa */
    typedef agg::renderer_scanline_aa_solid<t_renderer_mclip> t_renderer_scanline_aa_solid;
    t_renderer_scanline_aa_solid ren_scanline(ren_mclip);
    ren_scanline.color(c);

    /*
     * A 1-pixel thick line appears as 2-pixel thick anti-aliased line.
     * But that is outside the bounds of a canvas rect item.
     */
    if (gc->line_width & 1) {
	x += 1;
	y += 1;
	w -= 2;
	h -= 2;
    }

    VertexSource_XRectangle vertexSrc(x, y, w, h);
    agg::conv_stroke<VertexSource_XRectangle> stroke(vertexSrc);
    stroke.width((double) gc->line_width);

    /* Thing that generates scanlines */
    agg::rasterizer_scanline_aa<> rasterizer;
    rasterizer.reset();
    rasterizer.add_path(stroke);

    /* Scanline needed by the rasterizer -> renderer */
    agg::scanline_u8 scanline;

    /*
     * If the clipping region is specified, intersect it with the visible
     * region of the window, or if this is a pixmap then use the clipping
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
    }

    if (rgn) {
	for (i = 0; i < rgn->numRects; i++) {
	    BoxPtr box = &rgn->rects[i];
	    ren_mclip.add_clip_box(xOff + box->x1, yOff + box->y1,
		xOff + box->x2 - 1, yOff + box->y2 - 1);
	}
    }

    agg::render_scanlines(rasterizer, scanline, ren_scanline);
#endif

    if (tmpRgn) {
	SdlTkRgnPoolFree(tmpRgn);
    }

    /* Unlock surface */
    if (SDL_MUSTLOCK(sdl)) {
	SDL_UnlockSurface(sdl);
    }
}

/*
 * This is a pixel-format renderer that simply XORs the destination pixel
 * For 3 bytes-per-pixel
 */
class pixfmt_3Bpp_xor
{
public:
    typedef agg::rendering_buffer::row_data row_data;
#ifdef AGG23
    typedef agg::rendering_buffer::span_data span_data;
#endif
    typedef agg::rgba8 color_type;

    //--------------------------------------------------------------------
    pixfmt_3Bpp_xor(agg::rendering_buffer& rb) :
	m_rbuf(&rb)
    {}

    //--------------------------------------------------------------------
    AGG_INLINE unsigned width()  const { return m_rbuf->width();  }
    AGG_INLINE unsigned height() const { return m_rbuf->height(); }

    //--------------------------------------------------------------------
    void copy_pixel(int x, int y, const color_type& c)
    {
#ifdef AGG23
	Uint8* p = (Uint8*)m_rbuf->row(y) + x;
#else
	Uint8* p = (Uint8*)m_rbuf->row_ptr(y) + x;
#endif
	p[0] ^= 0xFF;
	p[1] ^= 0xFF;
	p[2] ^= 0xFF;
    }

    //--------------------------------------------------------------------
    void copy_hline(int x, int y, 
			    unsigned len, 
			    const color_type& c)
    {
#ifdef AGG23
	Uint8* p = (Uint8*)m_rbuf->row(y) + x + x + x;
#else
	Uint8* p = (Uint8*)m_rbuf->row_ptr(y) + x + x + x;
#endif
	do {
	    p[0] ^= 0xFF;
	    p[1] ^= 0xFF;
	    p[2] ^= 0xFF;
	    p += 3;
	} while(--len);
    }

    //--------------------------------------------------------------------
    void blend_hline(int x, int y,
		    unsigned len, 
		    const color_type& c,
		    agg::int8u cover)
    {
#ifdef AGG23
	Uint8* p = (Uint8*)m_rbuf->row(y) + x + x + x;
#else
	Uint8* p = (Uint8*)m_rbuf->row_ptr(y) + x + x + x;
#endif
	do {
	    p[0] ^= 0xFF;
	    p[1] ^= 0xFF;
	    p[2] ^= 0xFF;
	    p += 3;
	} while(--len);
    }

    //--------------------------------------------------------------------
    void blend_vline(int x, int y,
		    unsigned len, 
		    const color_type& c,
		    agg::int8u cover)
    {
#ifdef AGG23
	Uint8* p = (Uint8*)m_rbuf->row(y) + x + x + x;
#else
	Uint8* p = (Uint8*)m_rbuf->row_ptr(y) + x + x + x;
#endif
	do {
	    p[0] ^= 0xFF;
	    p[1] ^= 0xFF;
	    p[2] ^= 0xFF;
#ifdef AGG23
	    p = (Uint8*)m_rbuf->next_row(p);
#else
	    p += m_rbuf->stride();
#endif
	} while(--len);
    }

private:
    agg::rendering_buffer* m_rbuf;
};

/*
 * This is a pixel-format renderer that simply XORs the destination pixel
 * For 1, 2 or 4 bytes-per-pixel
 */
template<class Type> class pixfmt_1_2_4Bpp_xor
{
public:
    typedef agg::rendering_buffer::row_data row_data;
#ifdef AGG23
    typedef agg::rendering_buffer::span_data span_data;
#endif
    typedef agg::rgba8 color_type;

    //--------------------------------------------------------------------
    pixfmt_1_2_4Bpp_xor(agg::rendering_buffer& rb) :
	m_rbuf(&rb)
    {}

    //--------------------------------------------------------------------
    AGG_INLINE unsigned width()  const { return m_rbuf->width();  }
    AGG_INLINE unsigned height() const { return m_rbuf->height(); }

    //--------------------------------------------------------------------
    void copy_pixel(int x, int y, const color_type& c)
    {
#ifdef AGG23
	Type* p = (Type*)m_rbuf->row(y) + x;
#else
	Type* p = (Type*)m_rbuf->row_ptr(y) + x;
#endif
	*p ^= 0xFFFFFFFF;
    }

    //--------------------------------------------------------------------
    void copy_hline(int x, int y, 
		    unsigned len, 
		    const color_type& c)
    {
#ifdef AGG23
	Type* p = (Type*)m_rbuf->row(y) + x;
#else
	Type* p = (Type*)m_rbuf->row_ptr(y) + x;
#endif
	do {
	    *p ^= 0xFFFFFFFF;
	    p += 1;
	} while(--len);
    }

    //--------------------------------------------------------------------
    void blend_hline(int x, int y,
		    unsigned len, 
		    const color_type& c,
		    agg::int8u cover)
    {
#ifdef AGG23
	Type* p = (Type*)m_rbuf->row(y) + x;
#else
	Type* p = (Type*)m_rbuf->row_ptr(y) + x;
#endif
	do {
	    *p ^= 0xFFFFFFFF;
	    p += 1;
	} while(--len);
    }

    //--------------------------------------------------------------------
    void blend_vline(int x, int y,
		    unsigned len, 
		    const color_type& c,
		    agg::int8u cover)
    {
#ifdef AGG23
	Type* p = (Type*)m_rbuf->row(y) + x;
#else
	Type* p = (Type*)m_rbuf->row_ptr(y) + x;
#endif
	do {
	    *p ^= 0xFFFFFFFF;
#ifdef AGG23
	    p = (Type*)m_rbuf->next_row(p);
#else
	    y++;
	    p = (Type*)m_rbuf->row_ptr(y) + x;
#endif
	} while(--len);
    }

private:
    agg::rendering_buffer* m_rbuf;
};

void
SdlTkGfxDrawRect(Drawable d, GC gc, int x, int y, int w, int h)
{
    SDL_Surface *sdl;
    int format;

    sdl = SdlTkGetDrawableSurface(d, NULL, NULL, &format);
    if (sdl == NULL) {
	return;
    }

    if (gc->function == GXinvert) {
	switch (sdl->format->BitsPerPixel) {
	case 16:
	    doDrawRect<pixfmt_1_2_4Bpp_xor<Uint16> >(d, gc, x, y, w, h);
	    break;
	case 24:
	    doDrawRect<pixfmt_3Bpp_xor>(d, gc, x, y, w, h);
	    break;
	case 32:
	    doDrawRect<pixfmt_1_2_4Bpp_xor<Uint32> >(d, gc, x, y, w, h);
	    break;
	}
	return;
    }

    switch (format) {
    case SDLTK_RGB565:
	doDrawRect<agg::pixfmt_rgb565>(d, gc, x, y, w, h);
	break;
    case SDLTK_BGR565:
	doDrawRect<agg::pixfmt_bgr565>(d, gc, x, y, w, h);
	break;
    case SDLTK_RGB24:
	doDrawRect<agg::pixfmt_rgb24>(d, gc, x, y, w, h);
	break;
    case SDLTK_BGR24:
	doDrawRect<agg::pixfmt_bgr24>(d, gc, x, y, w, h);
	break;
    case SDLTK_RGBA32:
	doDrawRect<agg::pixfmt_rgba32>(d, gc, x, y, w, h);
	break;
    case SDLTK_ARGB32:
	doDrawRect<agg::pixfmt_argb32>(d, gc, x, y, w, h);
	break;
    case SDLTK_BGRA32:
	doDrawRect<agg::pixfmt_bgra32>(d, gc, x, y, w, h);
	break;
    case SDLTK_ABGR32:
	doDrawRect<agg::pixfmt_abgr32>(d, gc, x, y, w, h);
	break;
    case SDLTK_RGB555:
	doDrawRect<agg::pixfmt_rgb555>(d, gc, x, y, w, h);
	break;
    }
}

/*
 * Font manager/engine are protected by txt_mutex.
 */

TCL_DECLARE_MUTEX(txt_mutex);

typedef agg::font_engine_freetype_int16 t_font_engine;
typedef agg::font_cache_manager<t_font_engine> t_font_manager;
static t_font_engine *feng = 0;
static t_font_manager *fman = 0;

void
SdlTkGfxInitFC(void)
{
    Tcl_MutexLock(&txt_mutex);
    if (!feng) {
#ifdef AGG_CUSTOM_ALLOCATOR
	unsigned size;
	void *p;
	size = sizeof (t_font_engine);
	p = ckalloc(size);
	memset(p, 0, size);
	feng = new (p) t_font_engine;
	size = sizeof (t_font_manager);
	p = ckalloc(size);
	memset(p, 0, size);
	fman = new (p) t_font_manager(*feng);
#else
	feng = new t_font_engine;
	fman = new t_font_manager(*feng);
#endif
    }
    Tcl_MutexUnlock(&txt_mutex);
}

void
SdlTkGfxDeinitFC(void)
{
    Tcl_MutexLock(&txt_mutex);
    if (feng) {
#ifdef AGG_CUSTOM_ALLOCATOR
	void *p, *q;
	p = fman;
	q = feng;
	fman->~t_font_manager();
	feng->~t_font_engine();
	ckfree(p);
	ckfree(q);
#else
	delete fman;
	delete feng;
#endif
	fman = 0;
	feng = 0;
    }
    Tcl_MutexUnlock(&txt_mutex);
}

XFontStruct *
SdlTkGfxAllocFontStruct(_Font *_f)
{
    XFontStruct *fs = (XFontStruct *) ckalloc(sizeof (XFontStruct));

    memset(fs, 0, sizeof(XFontStruct));
    Tcl_MutexLock(&txt_mutex);
    fs->fid = (Font) _f;
    if (feng) {
	(void) feng->load_font(_f->file, _f->index, agg::glyph_ren_agg_gray8,
		(const char *) XGetFTStream(_f->file, _f->file_size));
	feng->char_map(FT_ENCODING_UNICODE);
	feng->flip_y(true);
	feng->height(_f->size);
	fs->ascent = (int) (feng->ascender() + 0.5);
	fs->descent = 0 - (int) (feng->descender() - 0.5);
    } else {
	fs->ascent = fs->descent = 1;
    }
    fs->max_bounds.width = 10; /* FIXME */
    Tcl_MutexUnlock(&txt_mutex);
    return fs;
}

extern "C" unsigned SdlTkGetNthGlyphIndex(_Font *_f, const char *s, int n);

int SdlTkGfxTextWidth(Font f, const char *string, int length, int *maxw)
{
    _Font *_f = (_Font *) f;
    int i;
    double w = 0.0;

    Tcl_MutexLock(&txt_mutex);
    if (!feng) {
	w = length;
	goto done;
    }
    (void) feng->load_font(_f->file, _f->index, agg::glyph_ren_agg_gray8,
	   (const char *) XGetFTStream(_f->file, _f->file_size));
    feng->flip_y(true);
    feng->height(_f->size);
    length /= sizeof(unsigned int) /* FcChar32 */;
    for (i = 0; i < length; i++) {
	const agg::glyph_cache *glyph = fman->glyph(SdlTkGetNthGlyphIndex(_f, string, i));
	if (glyph) {
	    w += glyph->advance_x;
	}
	if (maxw != NULL && w >= *maxw) {
	    *maxw = i;
	    break;
	}
    }
done:
    Tcl_MutexUnlock(&txt_mutex);
    return (int) w;
}

template<class PixelFormat>
void
doDrawString(Drawable d, GC gc, int x, int y, const char *string,
	     int length, double angle, int *xret, int *yret)
{
    SDL_Surface *sdl;
    int xOff = 0, yOff = 0;
    long i;
    _Font *_f = (_Font *) gc->font;
    double fx, fy;
    TkpClipMask *clipPtr = (TkpClipMask *) gc->clip_mask;
    REGION *rgn = 0;
    Region tmpRgn = 0;
    agg::glyph_rendering gr = agg::glyph_ren_native_gray8;

    if (IS_WINDOW(d)) {
	rgn = SdlTkGetVisibleRegion((_Window *) d);

	/* Window is unmapped or totally obscured */
	if (XEmptyRegion(rgn)) {
	    return;
	}
    }

    sdl = SdlTkGetDrawableSurface(d, &xOff, &yOff, NULL);

    /* Lock surface */
    if (SDL_MUSTLOCK(sdl)) {
	if (SDL_LockSurface(sdl) < 0) {
	    return;
	}
    }

    /* Rendering buffer, points to SDL_Surface memory */
    agg::rendering_buffer rbuf((agg::int8u *) sdl->pixels, sdl->w, sdl->h,
	sdl->pitch);

    /* Pixel-format renderer, a low-level pixel-rendering object */
    PixelFormat ren_pixf(rbuf);

    /* A basic renderer that does clipping to multiple boxes */
    typedef agg::renderer_mclip<PixelFormat> t_renderer_mclip;
    t_renderer_mclip ren_mclip(ren_pixf);

    /* The color, in the format used by the pixel-format renderer */
    Uint8 r, g, b;
    SDL_GetRGB(gc->foreground, SdlTkX.sdlsurf->format, &r, &g, &b);
    agg::rgba8 c(r, g, b);

    fx = xOff + x;
    fy = yOff + y;

    if (angle != 0.0) {
	gr = agg::glyph_ren_agg_gray8;
    }

    /* agg::glyph_ren_agg_gray8 is BROKEN with MS Gothic japanese chars */
    (void) feng->load_font(_f->file, _f->index, gr,
	   (const char *) XGetFTStream(_f->file, _f->file_size));
    feng->flip_y(true);
    feng->height(_f->size);

    if (angle != 0.0) {
	agg::trans_affine mtx;

	mtx *= agg::trans_affine_rotation(agg::deg2rad(-angle));
	feng->transform(mtx);
    }

    /*
     * If the clipping region is specified, intersect it with the visible
     * region of the window, or if this is a pixmap then use the clipping
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
    }

    if (rgn) {
	for (i = 0; i < rgn->numRects; i++) {
	    BoxPtr box = &rgn->rects[i];
	    ren_mclip.add_clip_box(xOff + box->x1, yOff + box->y1,
		xOff + box->x2 - 1, yOff + box->y2 - 1);
	}
    }

    length /= sizeof(unsigned int) /* FcChar32 */;

    /* FIXME: FillOpaqueStippled not implemented */
    if ((gc->fill_style == FillStippled
	    || gc->fill_style == FillOpaqueStippled)
	    && gc->stipple != None) {

	_Pixmap *stipple = (_Pixmap *) gc->stipple;

	/* Rendering buffer that points to the bitmap */
	agg::rendering_buffer stipple_buf((agg::int8u *) stipple->sdl->pixels,
	    stipple->sdl->w, stipple->sdl->h, stipple->sdl->pitch);

	/* A span allocator holds 1 line of pixels */
	agg::span_allocator<agg::rgba8> span_allocator;

	/* Generates spans (lines of pixels) from a source buffer */
#ifdef AGG23
	typedef span_stipple<agg::rgba8, agg::order_argb,
	    agg::wrap_mode_repeat, agg::wrap_mode_repeat> t_span_stipple;
	/* FIXME: stippled text doesn't line up with other stippled primitives. */
	t_span_stipple span_stipple(span_allocator, stipple_buf, gc->ts_x_origin + 1, gc->ts_y_origin);
	span_stipple.color(c);

	typedef agg::renderer_scanline_aa<t_renderer_mclip, t_span_stipple> t_renderer_scanline_aa;
	t_renderer_scanline_aa ren_scanline_aa(ren_mclip, span_stipple);

	for (i = 0; i < length; i++) {
	    const agg::glyph_cache *glyph = fman->glyph(SdlTkGetNthGlyphIndex(_f, string, i));
	    if (glyph) {
		fman->init_embedded_adaptors(glyph, fx, fy);
		agg::render_scanlines(fman->gray8_adaptor(), fman->gray8_scanline(), ren_scanline_aa);
		fx += glyph->advance_x;
		fy += glyph->advance_y;
	    }
	}
#else
	typedef agg::image_accessor_wrap_gray8<PixelFormat,
	    agg::wrap_mode_repeat, agg::wrap_mode_repeat> img_src_type;
	PixelFormat src_pixf(stipple_buf);
	img_src_type img_src(src_pixf);

	/* FIXME: stippled text doesn't line up with other stippled primitives. */
	typedef span_stipple<img_src_type> t_span_stipple;
	t_span_stipple span_stipple(img_src, gc->ts_x_origin + 1, gc->ts_y_origin);
	span_stipple.color(c);

	typedef agg::renderer_scanline_aa<t_renderer_mclip,
	    agg::span_allocator<agg::rgba8>, t_span_stipple> t_renderer_scanline_aa;
	t_renderer_scanline_aa ren_scanline_aa(ren_mclip, span_allocator, span_stipple);

	for (i = 0; i < length; i++) {
	    const agg::glyph_cache *glyph = fman->glyph(SdlTkGetNthGlyphIndex(_f, string, i));
	    if (glyph) {
		fman->init_embedded_adaptors(glyph, fx, fy);
		agg::render_scanlines(fman->gray8_adaptor(), fman->gray8_scanline(), ren_scanline_aa);
		fx += glyph->advance_x;
		fy += glyph->advance_y;
	    }
	}
#endif
    } else {
	typedef agg::renderer_scanline_aa_solid<t_renderer_mclip> t_renderer_scanline_aa_solid;
	t_renderer_scanline_aa_solid ren_aa(ren_mclip);
	ren_aa.color(c);

	for (i = 0; i < length; i++) {
	    const agg::glyph_cache *glyph = fman->glyph(SdlTkGetNthGlyphIndex(_f, string, i));
	    if (glyph) {
		fman->init_embedded_adaptors(glyph, fx, fy);
		agg::render_scanlines(fman->gray8_adaptor(),
		    fman->gray8_scanline(), ren_aa);
		fx += glyph->advance_x;
		fy += glyph->advance_y;
	    }
	}
    }

    if (angle != 0.0) {
	agg::trans_affine mtx;
	feng->transform(mtx);
    }

    if (xret != NULL) {
	*xret = fx - xOff;
    }
    if (yret != NULL) {
	*yret = fy - yOff;
    }

    if (tmpRgn) {
	SdlTkRgnPoolFree(tmpRgn);
    }

    /* Unlock surface */
    if (SDL_MUSTLOCK(sdl)) {
	SDL_UnlockSurface(sdl);
    }
}

template<class PixelFormat>
void
doDrawStringGray(Drawable d, GC gc, int x, int y, const char *string,
		 int length, double angle, int *xret, int *yret)
{
    SDL_Surface *sdl;
    int xOff = 0, yOff = 0;
    long i;
    _Font *_f = (_Font *) gc->font;
    double fx, fy;
    TkpClipMask *clipPtr = (TkpClipMask *) gc->clip_mask;
    REGION *rgn = 0;
    Region tmpRgn = 0;
    agg::glyph_rendering gr = agg::glyph_ren_native_gray8;

    if (IS_WINDOW(d)) {
	rgn = SdlTkGetVisibleRegion((_Window *) d);

	/* Window is unmapped or totally obscured */
	if (XEmptyRegion(rgn)) {
	    return;
	}
    }

    sdl = SdlTkGetDrawableSurface(d, &xOff, &yOff, NULL);

    /* Lock surface */
    if (SDL_MUSTLOCK(sdl)) {
	if (SDL_LockSurface(sdl) < 0) {
	    return;
	}
    }

    /* Rendering buffer, points to SDL_Surface memory */
    agg::rendering_buffer rbuf((agg::int8u *) sdl->pixels, sdl->w, sdl->h,
	sdl->pitch);

    /* Pixel-format renderer, a low-level pixel-rendering object */
    PixelFormat ren_pixf(rbuf);

    /* A basic renderer that does clipping to multiple boxes */
    typedef agg::renderer_mclip<PixelFormat> t_renderer_mclip;
    t_renderer_mclip ren_mclip(ren_pixf);

    /* The color, in the format used by the pixel-format renderer */
    Uint8 r, g, b;
    SDL_GetRGB(gc->foreground, SdlTkX.sdlsurf->format, &r, &g, &b);
    agg::rgba8 c(r, g, b);

    fx = xOff + x;
    fy = yOff + y;

    if (angle != 0.0) {
	gr = agg::glyph_ren_agg_gray8;
    }

    /* agg::glyph_ren_agg_gray8 is BROKEN with MS Gothic japanese chars */
    (void) feng->load_font(_f->file, _f->index, gr,
	   (const char *) XGetFTStream(_f->file, _f->file_size));
    feng->flip_y(true);
    feng->height(_f->size);

    if (angle != 0.0) {
	agg::trans_affine mtx;

	mtx *= agg::trans_affine_rotation(agg::deg2rad(-angle));
	feng->transform(mtx);
    }

    /*
     * If the clipping region is specified, intersect it with the visible
     * region of the window, or if this is a pixmap then use the clipping
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
    }

    if (rgn) {
	for (i = 0; i < rgn->numRects; i++) {
	    BoxPtr box = &rgn->rects[i];
	    ren_mclip.add_clip_box(xOff + box->x1, yOff + box->y1,
		xOff + box->x2 - 1, yOff + box->y2 - 1);
	}
    }

    length /= sizeof(unsigned int) /* FcChar32 */;

    typedef agg::renderer_scanline_aa_solid<t_renderer_mclip> t_renderer_scanline_aa_solid;
    t_renderer_scanline_aa_solid ren_aa(ren_mclip);
    ren_aa.color(c);

    for (i = 0; i < length; i++) {
        const agg::glyph_cache *glyph = fman->glyph(SdlTkGetNthGlyphIndex(_f, string, i));
	if (glyph) {
	    fman->init_embedded_adaptors(glyph, fx, fy);
	    agg::render_scanlines(fman->gray8_adaptor(),
		fman->gray8_scanline(), ren_aa);
	    fx += glyph->advance_x;
	    fy += glyph->advance_y;
	}
    }

    if (angle != 0.0) {
	agg::trans_affine mtx;
	feng->transform(mtx);
    }

    if (xret != NULL) {
        *xret = fx - xOff;
    }
    if (yret != NULL) {
        *yret = fy - yOff;
    }

    if (tmpRgn) {
	SdlTkRgnPoolFree(tmpRgn);
    }

    /* Unlock surface */
    if (SDL_MUSTLOCK(sdl)) {
	SDL_UnlockSurface(sdl);
    }
}

void
SdlTkGfxDrawString(Drawable d, GC gc, int x, int y, const char *string,
		   int length, double angle, int *xret, int *yret)
{
    SDL_Surface *sdl;
    int format;

    sdl = SdlTkGetDrawableSurface(d, NULL, NULL, &format);
    if (sdl == NULL) {
	return;
    }

    Tcl_MutexLock(&txt_mutex);

    if (!feng) {
	if (xret != NULL) {
	    *xret = x;
	}
	if (yret != NULL) {
	    *yret = y;
	}
	goto done;
    }

    switch (format) {
    case SDLTK_RGB565:
	doDrawString<agg::pixfmt_rgb565>(d, gc, x, y, string, length,
					 angle, xret, yret);
	break;
    case SDLTK_BGR565:
	doDrawString<agg::pixfmt_bgr565>(d, gc, x, y, string, length,
					 angle, xret, yret);
	break;
    case SDLTK_RGB24:
	doDrawString<agg::pixfmt_rgb24>(d, gc, x, y, string, length,
					angle, xret, yret);
	break;
    case SDLTK_BGR24:
	doDrawString<agg::pixfmt_bgr24>(d, gc, x, y, string, length,
					angle, xret, yret);
	break;
    case SDLTK_RGBA32:
	doDrawString<agg::pixfmt_rgba32>(d, gc, x, y, string, length,
					 angle, xret, yret);
	break;
    case SDLTK_ARGB32:
	doDrawString<agg::pixfmt_argb32>(d, gc, x, y, string, length,
					 angle, xret, yret);
	break;
    case SDLTK_BGRA32:
	doDrawString<agg::pixfmt_bgra32>(d, gc, x, y, string, length,
					 angle, xret, yret);
	break;
    case SDLTK_ABGR32:
	doDrawString<agg::pixfmt_abgr32>(d, gc, x, y, string, length,
					 angle, xret, yret);
	break;
    case SDLTK_GRAY8:
	doDrawStringGray<agg::pixfmt_gray8>(d, gc, x, y, string, length,
					    angle, xret, yret);
	break;
    case SDLTK_RGB555:
	doDrawString<agg::pixfmt_rgb555>(d, gc, x, y, string, length,
					 angle, xret, yret);
	break;
    }
done:
    Tcl_MutexUnlock(&txt_mutex);
}

template<class PixelFormat>
void
doFillArc(Drawable d, GC gc, int x, int y,
    unsigned int width, unsigned int height, int start, int extent)
{
    SDL_Surface *sdl;
    int xOff = 0, yOff = 0;
    long i;
    REGION *rgn = 0;

    if (IS_WINDOW(d)) {
	rgn = SdlTkGetVisibleRegion((_Window *) d);

	/* Window is unmapped or totally obscured */
	if (XEmptyRegion(rgn)) {
	    return;
	}
    }

    sdl = SdlTkGetDrawableSurface(d, &xOff, &yOff, NULL);

    /* Lock surface */
    if (SDL_MUSTLOCK(sdl)) {
	if (SDL_LockSurface(sdl) < 0) {
	    return;
	}
    }

    /* Rendering buffer, points to SDL_Surface memory */
    agg::rendering_buffer rbuf((agg::int8u *) sdl->pixels, sdl->w, sdl->h,
	sdl->pitch);

    /* Pixel-format renderer, a low-level pixel-rendering object */
    PixelFormat ren_pixf(rbuf);

    /* A basic renderer that does clipping to multiple boxes */
    typedef agg::renderer_mclip<PixelFormat> t_renderer_mclip;
    t_renderer_mclip ren_mclip(ren_pixf);

    /* The color, in the format used by the pixel-format renderer */
    Uint8 r, g, b;
    SDL_GetRGB(gc->foreground, SdlTkX.sdlsurf->format, &r, &g, &b);
    agg::rgba8 c(r, g, b);

    /* Apparently agg::arc is deprecated */
    agg::bezier_arc arc(xOff + x + width / 2.0, yOff + y + height / 2.0,
	width / 2.0, height / 2.0,
	agg::deg2rad(start / 64.0), agg::deg2rad(extent / 64.0),
        gc->arc_mode == ArcPieSlice ? true : false);
    typedef agg::conv_curve<agg::bezier_arc, agg::curve3_div, agg::curve4_div> t_conv_curve;
    t_conv_curve curve(arc);

    /* Thing that generates scanlines */
    agg::rasterizer_scanline_aa<> rasterizer;
    rasterizer.reset();
    rasterizer.add_path(curve);

    /* Scanline needed by the rasterizer -> renderer */
    agg::scanline_u8 scanline;

    if (rgn) {
	for (i = 0; i < rgn->numRects; i++) {
	    BoxPtr box = &rgn->rects[i];
	    ren_mclip.add_clip_box(xOff + box->x1, yOff + box->y1,
		xOff + box->x2 - 1, yOff + box->y2 - 1);
	}
    }

    /* FIXME: FillOpaqueStippled not implemented */
    if ((gc->fill_style == FillStippled
	    || gc->fill_style == FillOpaqueStippled)
	    && gc->stipple != None) {

	_Pixmap *stipple = (_Pixmap *) gc->stipple;

	/* Rendering buffer that points to the bitmap */
	agg::rendering_buffer stipple_buf((agg::int8u *) stipple->sdl->pixels,
	    stipple->sdl->w, stipple->sdl->h, stipple->sdl->pitch);

	/* A span allocator holds 1 line of pixels */
	agg::span_allocator<agg::rgba8> span_allocator;

	/* Generates spans (lines of pixels) from a source buffer */
#ifdef AGG23
	typedef span_stipple<agg::rgba8, agg::order_argb,
	    agg::wrap_mode_repeat, agg::wrap_mode_repeat> t_span_stipple;
	t_span_stipple span_stipple(span_allocator, stipple_buf, gc->ts_x_origin, gc->ts_y_origin);
	span_stipple.color(c);

	typedef agg::renderer_scanline_aa<t_renderer_mclip, t_span_stipple> t_renderer_scanline_aa;
	t_renderer_scanline_aa ren_scanline_aa(ren_mclip, span_stipple);

	agg::render_scanlines(rasterizer, scanline, ren_scanline_aa);
#else
	typedef agg::image_accessor_wrap_gray8<PixelFormat,
	    agg::wrap_mode_repeat, agg::wrap_mode_repeat> img_src_type;
	PixelFormat src_pixf(stipple_buf);
	img_src_type img_src(src_pixf);

	typedef span_stipple<img_src_type> t_span_stipple;
	t_span_stipple span_stipple(img_src, gc->ts_x_origin, gc->ts_y_origin);
	span_stipple.color(c);

	typedef agg::renderer_scanline_aa<t_renderer_mclip,
	    agg::span_allocator<agg::rgba8>, t_span_stipple> t_renderer_scanline_aa;
	t_renderer_scanline_aa ren_scanline_aa(ren_mclip, span_allocator, span_stipple);

	agg::render_scanlines(rasterizer, scanline, ren_scanline_aa);
#endif
    } else {
	typedef agg::renderer_scanline_aa_solid<t_renderer_mclip> t_renderer_scanline_aa_solid;
	t_renderer_scanline_aa_solid ren_scanline(ren_mclip);
	ren_scanline.color(c);

	agg::render_scanlines(rasterizer, scanline, ren_scanline);
    }

    /* Unlock surface */
    if (SDL_MUSTLOCK(sdl)) {
	SDL_UnlockSurface(sdl);
    }
}

void
SdlTkGfxFillArc(Drawable d, GC gc, int x, int y,
    unsigned int width, unsigned int height, int start, int extent)
{
    SDL_Surface *sdl;
    int format;

    sdl = SdlTkGetDrawableSurface(d, NULL, NULL, &format);
    if (sdl == NULL) {
	return;
    }
    start = -start;
    extent = -extent;

    switch (format) {
    case SDLTK_RGB565:
	doFillArc<agg::pixfmt_rgb565>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_BGR565:
	doFillArc<agg::pixfmt_bgr565>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_RGB24:
	doFillArc<agg::pixfmt_rgb24>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_BGR24:
	doFillArc<agg::pixfmt_bgr24>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_RGBA32:
	doFillArc<agg::pixfmt_rgba32>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_ARGB32:
	doFillArc<agg::pixfmt_argb32>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_BGRA32:
	doFillArc<agg::pixfmt_bgra32>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_ABGR32:
	doFillArc<agg::pixfmt_abgr32>(d, gc, x, y, width, height, start, extent);
	break;
    case SDLTK_RGB555:
	doFillArc<agg::pixfmt_rgb555>(d, gc, x, y, width, height, start, extent);
	break;
    }
}

template<class PixelFormat>
void
doFillPolygon(Drawable d, GC gc, XPoint *points, int npoints, int shape, int mode)
{
    SDL_Surface *sdl;
    int xOff = 0, yOff = 0;
    long i;
    REGION *rgn = 0;

    if (IS_WINDOW(d)) {
	rgn = SdlTkGetVisibleRegion((_Window *) d);

	/* Window is unmapped or totally obscured */
	if (XEmptyRegion(rgn)) {
	    return;
	}
    }

    sdl = SdlTkGetDrawableSurface(d, &xOff, &yOff, NULL);

    /* Lock surface */
    if (SDL_MUSTLOCK(sdl)) {
	if (SDL_LockSurface(sdl) < 0) {
	    return;
	}
    }

    /* Rendering buffer, points to SDL_Surface memory */
    agg::rendering_buffer rbuf((agg::int8u *) sdl->pixels, sdl->w, sdl->h,
	sdl->pitch);

    /* Pixel-format renderer, a low-level pixel-rendering object */
    PixelFormat ren_pixf(rbuf);

    /* A basic renderer that does clipping to multiple boxes */
    typedef agg::renderer_mclip<PixelFormat> t_renderer_mclip;
    t_renderer_mclip ren_mclip(ren_pixf);

    /* The color, in the format used by the pixel-format renderer */
    Uint8 r, g, b;
    SDL_GetRGB(gc->foreground, SdlTkX.sdlsurf->format, &r, &g, &b);
    agg::rgba8 c(r, g, b);

    /* Thing that creates scanlines for a filled polygon (with aa) */
    agg::rasterizer_scanline_aa<> rasterizer;
    rasterizer.reset();
#if 1
    VertexSource_XPoints vertexSrc(points, npoints, xOff, yOff);
    rasterizer.add_path(vertexSrc);
#else
    rasterizer.move_to((xOff + points[0].x) << 8, (yOff + points[0].y) << 8);
    for (i = 1; i < npoints; i++)
	rasterizer.line_to((xOff + points[i].x) << 8, (yOff + points[i].y) << 8);
#endif
    /* Scanline needed by the rasterizer -> renderer */
    agg::scanline_u8 scanline;

    if (rgn) {
	for (i = 0; i < rgn->numRects; i++) {
	    BoxPtr box = &rgn->rects[i];
	    ren_mclip.add_clip_box(xOff + box->x1, yOff + box->y1,
		xOff + box->x2 - 1, yOff + box->y2 - 1);
	}
    }

    /* FIXME: FillOpaqueStippled not implemented */
    if ((gc->fill_style == FillStippled
	    || gc->fill_style == FillOpaqueStippled)
	    && gc->stipple != None) {

	_Pixmap *stipple = (_Pixmap *) gc->stipple;

	/* Rendering buffer that points to the bitmap */
	agg::rendering_buffer stipple_buf((agg::int8u *) stipple->sdl->pixels,
	    stipple->sdl->w, stipple->sdl->h, stipple->sdl->pitch);

	/* A span allocator holds 1 line of pixels */
	agg::span_allocator<agg::rgba8> span_allocator;

	/* Generates spans (lines of pixels) from a source buffer */
#ifdef AGG23
	typedef span_stipple<agg::rgba8, agg::order_argb,
	    agg::wrap_mode_repeat, agg::wrap_mode_repeat> t_span_stipple;
	t_span_stipple span_stipple(span_allocator, stipple_buf, gc->ts_x_origin, gc->ts_y_origin);
	span_stipple.color(c);

	typedef agg::renderer_scanline_aa<t_renderer_mclip, t_span_stipple> t_renderer_scanline_aa;
	t_renderer_scanline_aa ren_scanline_aa(ren_mclip, span_stipple);

	agg::render_scanlines(rasterizer, scanline, ren_scanline_aa);
#else
	typedef agg::image_accessor_wrap_gray8<PixelFormat,
	    agg::wrap_mode_repeat, agg::wrap_mode_repeat> img_src_type;
	PixelFormat src_pixf(stipple_buf);
	img_src_type img_src(src_pixf);

	typedef span_stipple<img_src_type> t_span_stipple;
	t_span_stipple span_stipple(img_src, gc->ts_x_origin, gc->ts_y_origin);
	span_stipple.color(c);

	typedef agg::renderer_scanline_aa<t_renderer_mclip,
	    agg::span_allocator<agg::rgba8>, t_span_stipple> t_renderer_scanline_aa;
	t_renderer_scanline_aa ren_scanline_aa(ren_mclip, span_allocator, span_stipple);

	agg::render_scanlines(rasterizer, scanline, ren_scanline_aa);
#endif
    } else {
	/* Thing that renders the scanlines */
	typedef agg::renderer_scanline_aa_solid<t_renderer_mclip> t_renderer_scanline_aa_solid;
	t_renderer_scanline_aa_solid ren_scanline(ren_mclip);
	ren_scanline.color(c);

	/* Thing that calculates the scanlines that make up the shape */
	agg::render_scanlines(rasterizer, scanline, ren_scanline);
    }

    /* Unlock surface */
    if (SDL_MUSTLOCK(sdl)) {
	SDL_UnlockSurface(sdl);
    }
}

void
SdlTkGfxFillPolygon(Drawable d, GC gc, XPoint *points, int npoints, int shape, int mode)
{
    SDL_Surface *sdl;
    int format;

    sdl = SdlTkGetDrawableSurface(d, NULL, NULL, &format);
    if (sdl == NULL) {
	return;
    }

    switch (format) {
    case SDLTK_RGB565:
	doFillPolygon<agg::pixfmt_rgb565>(d, gc, points, npoints, shape, mode);
	break;
    case SDLTK_BGR565:
	doFillPolygon<agg::pixfmt_bgr565>(d, gc, points, npoints, shape, mode);
	break;
    case SDLTK_RGB24:
	doFillPolygon<agg::pixfmt_rgb24>(d, gc, points, npoints, shape, mode);
	break;
    case SDLTK_BGR24:
	doFillPolygon<agg::pixfmt_bgr24>(d, gc, points, npoints, shape, mode);
	break;
    case SDLTK_RGBA32:
	doFillPolygon<agg::pixfmt_rgba32>(d, gc, points, npoints, shape, mode);
	break;
    case SDLTK_ARGB32:
	doFillPolygon<agg::pixfmt_argb32>(d, gc, points, npoints, shape, mode);
	break;
    case SDLTK_BGRA32:
	doFillPolygon<agg::pixfmt_bgra32>(d, gc, points, npoints, shape, mode);
	break;
    case SDLTK_ABGR32:
	doFillPolygon<agg::pixfmt_abgr32>(d, gc, points, npoints, shape, mode);
	break;
    case SDLTK_RGB555:
	doFillPolygon<agg::pixfmt_rgb555>(d, gc, points, npoints, shape, mode);
	break;
    }
}

template<class PixelFormat>
void
doFillRect(Drawable d, GC gc, int x, int y, int w, int h)
{
    SDL_Surface *sdl;
    int xOff = 0, yOff = 0;
    long i;
    TkpClipMask *clipPtr = (TkpClipMask*)gc->clip_mask;
    REGION *rgn = 0;
    Region tmpRgn = 0;

    if (IS_WINDOW(d)) {
	rgn = SdlTkGetVisibleRegion((_Window *) d);

	/* Window is unmapped or totally obscured */
	if (XEmptyRegion(rgn)) {
	    return;
	}
    }

    sdl = SdlTkGetDrawableSurface(d, &xOff, &yOff, NULL);
    x += xOff;
    y += yOff;

    /* Lock surface */
    if (SDL_MUSTLOCK(sdl)) {
	if (SDL_LockSurface(sdl) < 0) {
	    return;
	}
    }

    /* Rendering buffer, points to SDL_Surface memory */
    agg::rendering_buffer rbuf((agg::int8u *) sdl->pixels, sdl->w, sdl->h,
	sdl->pitch);

    /* Pixel-format renderer, a low-level pixel-rendering object */
    PixelFormat ren_pixf(rbuf);

    /* A basic renderer that does clipping to multiple boxes */
    typedef agg::renderer_mclip<PixelFormat> t_renderer_mclip;
    t_renderer_mclip ren_mclip(ren_pixf);

    /* The color, in the format used by the pixel-format renderer */
    Uint8 r, g, b;
    SDL_GetRGB(gc->foreground, SdlTkX.sdlsurf->format, &r, &g, &b);
    agg::rgba8 c(r, g, b);

    /*
     * If the clipping region is specified, intersect it with the visible
     * region of the window, or if this is a pixmap then use the clipping
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
    }

    if (rgn) {
	for (i = 0; i < rgn->numRects; i++) {
	    BoxPtr box = &rgn->rects[i];
	    ren_mclip.add_clip_box(xOff + box->x1, yOff + box->y1,
		xOff + box->x2 - 1, yOff + box->y2 - 1);
	}
    }

    /* FIXME: FillOpaqueStippled not implemented */
    if ((gc->fill_style == FillStippled
	    || gc->fill_style == FillOpaqueStippled)
	    && gc->stipple != None) {

	_Pixmap *stipple = (_Pixmap *) gc->stipple;

	/* Rendering buffer that points to the bitmap */
	agg::rendering_buffer stipple_buf((agg::int8u *) stipple->sdl->pixels,
	    stipple->sdl->w, stipple->sdl->h, stipple->sdl->pitch);
#if 1
	agg::wrap_mode_repeat wrap_x(stipple_buf.width());
	agg::wrap_mode_repeat wrap_y(stipple_buf.height());
	unsigned wy = wrap_y(y - gc->ts_y_origin);
	while (h--) {
#ifdef AGG23
	    agg::int8u *row_ptr = stipple_buf.row(wy);
#else
	    agg::int8u *row_ptr = stipple_buf.row_ptr(wy);
#endif
	    unsigned x1 = x;
	    unsigned wx = wrap_x(x - gc->ts_x_origin);
	    unsigned w1 = w;
	    while (w1--) {
		agg::int8u *p = row_ptr + wx;
		if (!p[0]) {
		    ren_mclip.copy_pixel(x1, y, c);
		}
		wx = ++wrap_x;
		++x1;
	    }
	    wy = ++wrap_y;
	    ++y;
	}
#else
	/* A span allocator holds 1 line of pixels */
	agg::span_allocator<agg::rgba8> span_allocator;

	/* Generates spans (lines of pixels) from a source buffer */
#ifdef AGG23
	typedef span_stipple<agg::rgba8, agg::order_argb,
	    agg::wrap_mode_repeat, agg::wrap_mode_repeat> t_span_stipple;
	t_span_stipple span_stipple(span_allocator, stipple_buf, gc->ts_x_origin, gc->ts_y_origin);
	span_stipple.color(c);

	typedef agg::renderer_scanline_aa<t_renderer_mclip, t_span_stipple> t_renderer_scanline_aa;
	t_renderer_scanline_aa ren_scanline_aa(ren_mclip, span_stipple);
#else
	typedef agg::image_accessor_wrap_gray8<PixelFormat,
	    agg::wrap_mode_repeat, agg::wrap_mode_repeat> img_src_type;
	PixelFormat src_pixf(stipple_buf);
	img_src_type img_src(src_pixf);

	typedef span_stipple<img_src_type> t_span_stipple;
	t_span_stipple span_stipple(img_src, gc->ts_x_origin, gc->ts_y_origin);
	span_stipple.color(c);

	typedef agg::renderer_scanline_aa<t_renderer_mclip,
	    agg::span_allocator<agg::rgba8>, t_span_stipple> t_renderer_scanline_aa;
	t_renderer_scanline_aa ren_scanline_aa(ren_mclip, span_allocator, span_stipple);

	agg::render_scanlines(rasterizer, scanline, ren_scanline_aa);
#endif

	VertexSource_XRectangle vertexSrc(x, y, w, h);
	agg::rasterizer_scanline_aa<> rasterizer;
	rasterizer.reset();
	rasterizer.add_path(vertexSrc);

	/* Scanline needed by the rasterizer -> renderer */
	agg::scanline_u8 scanline;

	render_scanlines(rasterizer, scanline, ren_scanline_aa);
#endif
    } else {
	ren_mclip.copy_bar(x, y, x + w - 1, y + h - 1, c);
    }

    if (tmpRgn)
	SdlTkRgnPoolFree(tmpRgn);

    /* Unlock surface */
    if (SDL_MUSTLOCK(sdl)) {
	SDL_UnlockSurface(sdl);
    }
}

void
SdlTkGfxFillRect(Drawable d, GC gc, int x, int y, int w, int h)
{
    SDL_Surface *sdl;
    int format;

    sdl = SdlTkGetDrawableSurface(d, NULL, NULL, &format);
    if (sdl == NULL) {
	return;
    }

    if (gc->function == GXinvert) {
	switch (sdl->format->BitsPerPixel) {
	case 16:
	    doFillRect<pixfmt_1_2_4Bpp_xor<Uint16> >(d, gc, x, y, w, h);
	    break;
	case 24:
	    doFillRect<pixfmt_3Bpp_xor>(d, gc, x, y, w, h);
	    break;
	case 32:
	    doFillRect<pixfmt_1_2_4Bpp_xor<Uint32> >(d, gc, x, y, w, h);
	    break;
	}
	return;
    }

    switch (format) {
    case SDLTK_RGB565:
	doFillRect<agg::pixfmt_rgb565>(d, gc, x, y, w, h);
	break;
    case SDLTK_BGR565:
	doFillRect<agg::pixfmt_bgr565>(d, gc, x, y, w, h);
	break;
    case SDLTK_RGB24:
	doFillRect<agg::pixfmt_rgb24>(d, gc, x, y, w, h);
	break;
    case SDLTK_BGR24:
	doFillRect<agg::pixfmt_bgr24>(d, gc, x, y, w, h);
	break;
    case SDLTK_RGBA32:
	doFillRect<agg::pixfmt_rgba32>(d, gc, x, y, w, h);
	break;
    case SDLTK_ARGB32:
	doFillRect<agg::pixfmt_argb32>(d, gc, x, y, w, h);
	break;
    case SDLTK_BGRA32:
	doFillRect<agg::pixfmt_bgra32>(d, gc, x, y, w, h);
	break;
    case SDLTK_ABGR32:
	doFillRect<agg::pixfmt_abgr32>(d, gc, x, y, w, h);
	break;
    case SDLTK_RGB555:
	doFillRect<agg::pixfmt_rgb555>(d, gc, x, y, w, h);
	break;
    case SDLTK_GRAY8:
	doFillRect<agg::pixfmt_gray8>(d, gc, x, y, w, h);
	break;
    }
}

#ifndef AGG23

#undef JoinMiter
#undef JoinRound
#undef JoinBevel
#undef CapButt
#undef CapSquare
#undef CapRound

#include "agg2d.h"

void *
SdlTkXCreateAgg2D(Display *display)
{
    Agg2D *agg2d;
    if (display == NULL) {
	return NULL;
    }
#ifdef AGG_CUSTOM_ALLOCATOR
    agg2d = agg::obj_allocator<Agg2D>::allocate();
#else
    agg2d = new Agg2D();
#endif
    return (void *) agg2d;
}

void
SdlTkXDestroyAgg2D(Display *display, void *ptr)
{
    if ((ptr == NULL) || (display == NULL)) {
	return;
    }
    if ((ptr != display->agg2d) || (display->screens == NULL)) {
	Agg2D *agg2d = (Agg2D *) ptr;
#ifdef AGG_CUSTOM_ALLOCATOR
	agg::obj_allocator<Agg2D>::deallocate(agg2d);
#else
	delete agg2d;
#endif
	if (ptr == display->agg2d) {
	    display->agg2d = NULL;
	}
    }
}

void *
SdlTkXGetAgg2D(Display *display, Drawable d)
{
    _Pixmap *_p = (_Pixmap *) d;
    SDL_Surface *sdl;
    Agg2D *agg2d;

    if (display == NULL) {
	return NULL;
    }
    if ((_p != NULL) &&
	((_p->type != DT_PIXMAP) || (_p->format != SDLTK_BGRA32))) {
	return NULL;
    }
    if (display->agg2d == NULL) {
#ifdef AGG_CUSTOM_ALLOCATOR
	agg2d = agg::obj_allocator<Agg2D>::allocate();
#else
	agg2d = new Agg2D();
#endif
	display->agg2d = (void *) agg2d;
    } else {
	agg2d = (Agg2D *) display->agg2d;
    }
    sdl = (_p != NULL) ? _p->sdl : NULL;
    if (sdl != NULL) {
	agg2d->attach((unsigned char *) sdl->pixels,
		      sdl->w, sdl->h, sdl->pitch);
    } else {
	agg2d->attach(display->agg2d_dummyfb, 1, 1, sizeof (int));
    }
    return display->agg2d;
}

#endif

/*
 * Local Variables:
 * mode: c++
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
