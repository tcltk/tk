/*
 * tkUnixCairoPath.c --
 *
 *     This file implements path drawing API's using the Cairo rendering engine.
 *
 *  TODO: implement text drawing using glyphs instead of the "toy" text API.
 *
 * Copyright (c) 2005-2008  Mats Bengtsson
 *
 */

/* This should go into configure.in but don't know how. */
#ifdef USE_PANIC_ON_PHOTO_ALLOC_FAILURE
#undef USE_PANIC_ON_PHOTO_ALLOC_FAILURE
#endif

#include <cairo.h>
#include <cairo-xlib.h>
#include <tkUnixInt.h>
#include "tkoPath.h"

#define TINT_INT_CALCULATION

#define Blue255FromXColorPtr(xc)   ((xc)->blue >> 8)
#define Green255FromXColorPtr(xc)  ((xc)->green >> 8)
#define Red255FromXColorPtr(xc)    ((xc)->red >> 8)

#define BlueDoubleFromXColorPtr(xc)   ((double) ((xc)->blue >> 8) / 255.0)
#define GreenDoubleFromXColorPtr(xc)  ((double) ((xc)->green >> 8) / 255.0)
#define RedDoubleFromXColorPtr(xc)    ((double) ((xc)->red >> 8) / 255.0)

MODULE_SCOPE int Tk_PathAntiAlias;
MODULE_SCOPE int Tk_PathSurfaceCopyPremultiplyAlpha;
MODULE_SCOPE int Tk_PathDepixelize;

static union {
    short set;
    char little;
} kEndianess;

/*
 * @@@ Need to use cairo_image_surface_create_for_data() here since
 *     prior to 1.2 there doesn't exist any cairo_image_surface_get_data()
 *     accessor.
 */
typedef struct PathSurfaceCairoRecord {
    unsigned char *data;
    cairo_format_t format;
    int width;
    int height;
    int stride;                /* number of bytes between the start of rows in the buffer */
} PathSurfaceCairoRecord;

/*
 * This is used as a place holder for platform dependent
 * stuff between each call.
 */
typedef struct TkPathContext_ {
    cairo_t *c;
    cairo_surface_t *surface;
    PathSurfaceCairoRecord *record;     /* NULL except for memory surfaces.
                                         * Skip when cairo 1.2 widely spread. */
    int widthCode;             /* Used to depixelize the strokes:
                                * 0: not integer width
                                * 1: odd integer width
                                * 2: even integer width */
    cairo_matrix_t def_matrix; /* For TkPathResetTMatrix() */
} TkPathContext_;

static void TkPathPrepareForStroke(
    TkPathContext ctx,
    Tk_PathStyle * style);

static void
CairoSetFill(
    TkPathContext ctx,
    Tk_PathStyle * style)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    /* Patch from Tim Edwards to handle color correctly on 64 bit arch. */
    cairo_set_source_rgba(context->c,
        (double)(GetColorFromPathColor(style->fill)->red) / 0xFFFF,
        (double)(GetColorFromPathColor(style->fill)->green) / 0xFFFF,
        (double)(GetColorFromPathColor(style->fill)->blue) / 0xFFFF,
        style->fillOpacity);
    cairo_set_fill_rule(context->c,
        (style->fillRule == WindingRule) ? CAIRO_FILL_RULE_WINDING :
        CAIRO_FILL_RULE_EVEN_ODD);
}

TkPathContext
TkPathInit(
    Tk_Window tkwin,
    Drawable d)
{
cairo_t *c;
cairo_surface_t *surface;
TkPathContext_ *context = (TkPathContext_ *)
        ckalloc((unsigned)(sizeof(TkPathContext_)));
Window dummy;
int x, y;
unsigned int width, height, borderWidth, depth;

    /* Find size of Drawable */
    XGetGeometry(Tk_Display(tkwin), d,
        &dummy, &x, &y, &width, &height, &borderWidth, &depth);

    surface = cairo_xlib_surface_create(Tk_Display(tkwin), d, Tk_Visual(tkwin),
        width, height);
    c = cairo_create(surface);
    context->c = c;
    context->surface = surface;
    context->record = NULL;
    context->widthCode = 0;
    cairo_get_matrix(context->c, &context->def_matrix);
    return (TkPathContext) context;
}

TkPathContext
TkPathInitSurface(
    Display * display,
    int width,
    int height)
{
cairo_t *c;
cairo_surface_t *surface;
unsigned char *data;
int stride;

    /*
     * @@@ Need to use cairo_image_surface_create_for_data() here since
     *     prior to 1.2 there doesn't exist any cairo_image_surface_get_data()
     *     accessor.
     */
TkPathContext_ *context = (TkPathContext_ *)
        ckalloc((unsigned)(sizeof(TkPathContext_)));
PathSurfaceCairoRecord *record = (PathSurfaceCairoRecord *)
        ckalloc((unsigned)(sizeof(PathSurfaceCairoRecord)));
    stride = 4 * width;
    /* Round up to nearest multiple of 16 */
    stride = (stride + (16 - 1)) & ~(16 - 1);
    data = (unsigned char *)ckalloc(height * stride);
    memset(data, '\0', height * stride);
    surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32,
        width, height, stride);
    record->data = data;
    record->format = CAIRO_FORMAT_ARGB32;
    record->width = width;
    record->height = height;
    record->stride = stride;
    c = cairo_create(surface);
    context->c = c;
    context->surface = surface;
    context->record = record;
    context->widthCode = 0;
    cairo_get_matrix(context->c, &context->def_matrix);
    return (TkPathContext) context;
}

void
TkPathPushTMatrix(
    TkPathContext ctx,
    TkPathMatrix * m)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
cairo_matrix_t matrix;
    if(m == NULL) {
        return;
    }
    cairo_matrix_init(&matrix, m->a, m->b, m->c, m->d, m->tx, m->ty);
    cairo_transform(context->c, &matrix);
}

void
TkPathResetTMatrix(
    TkPathContext ctx)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    context->widthCode = 0;
    cairo_set_matrix(context->c, &context->def_matrix);
}

void
TkPathSaveState(
    TkPathContext ctx)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_save(context->c);
}

void
TkPathRestoreState(
    TkPathContext ctx)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_restore(context->c);
}

void
TkPathBeginPath(
    TkPathContext ctx,
    Tk_PathStyle * style)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
int nint;
double width;
    cairo_new_path(context->c);
    if(style->strokeColor == NULL) {
        context->widthCode = 0;
    } else {
        width = style->strokeWidth;
        nint = (int)(width + 0.5);
        context->widthCode = fabs(width - nint) > 0.01 ? 0 : 2 - nint % 2;
    }
}

void
TkPathMoveTo(
    TkPathContext ctx,
    double x,
    double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if(Tk_PathDepixelize) {
        x = TK_PATH_DEPIXELIZE(context->widthCode, x);
        y = TK_PATH_DEPIXELIZE(context->widthCode, y);
    }
    cairo_move_to(context->c, x, y);
}

void
TkPathLineTo(
    TkPathContext ctx,
    double x,
    double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if(Tk_PathDepixelize) {
        x = TK_PATH_DEPIXELIZE(context->widthCode, x);
        y = TK_PATH_DEPIXELIZE(context->widthCode, y);
    }
    cairo_line_to(context->c, x, y);
}

void
TkPathQuadBezier(
    TkPathContext ctx,
    double ctrlX,
    double ctrlY,
    double x,
    double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    double cx, cy;
    double x31, y31, x32, y32;

    if(Tk_PathDepixelize) {
        x = TK_PATH_DEPIXELIZE(context->widthCode, x);
        y = TK_PATH_DEPIXELIZE(context->widthCode, y);
    }
    cairo_get_current_point(context->c, &cx, &cy);

    /*
     * Conversion of quadratic bezier curve to
     * cubic bezier curve: (mozilla/svg)
     * Unchecked! Must be an approximation!
     */
    x31 = cx + (ctrlX - cx) * 2 / 3;
    y31 = cy + (ctrlY - cy) * 2 / 3;
    x32 = ctrlX + (x - ctrlX) / 3;
    y32 = ctrlY + (y - ctrlY) / 3;

    cairo_curve_to(context->c, x31, y31, x32, y32, x, y);
}

void
TkPathCurveTo(
    TkPathContext ctx,
    double x1,
    double y1,
    double x2,
    double y2,
    double x,
    double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if(Tk_PathDepixelize) {
        x = TK_PATH_DEPIXELIZE(context->widthCode, x);
        y = TK_PATH_DEPIXELIZE(context->widthCode, y);
    }
    cairo_curve_to(context->c, x1, y1, x2, y2, x, y);
}

void
TkPathArcTo(
    TkPathContext ctx,
    double rx,
    double ry,
    double phiDegrees,         /* The rotation angle in degrees! */
    char largeArcFlag,
    char sweepFlag,
    double x,
    double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if(Tk_PathDepixelize) {
        x = TK_PATH_DEPIXELIZE(context->widthCode, x);
        y = TK_PATH_DEPIXELIZE(context->widthCode, y);
    }
    TkPathArcToUsingBezier(ctx, rx, ry, phiDegrees, largeArcFlag,
        sweepFlag, x, y);
}

void
TkPathRectangle(
    TkPathContext ctx,
    double x,
    double y,
    double width,
    double height)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if(Tk_PathDepixelize) {
        x = TK_PATH_DEPIXELIZE(context->widthCode, x);
        y = TK_PATH_DEPIXELIZE(context->widthCode, y);
    }
    cairo_rectangle(context->c, x, y, width, height);
}

void
TkPathOval(
    TkPathContext ctx,
    double cx,
    double cy,
    double rx,
    double ry)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if(rx == ry) {
        cairo_move_to(context->c, cx + rx, cy);
        cairo_arc(context->c, cx, cy, rx, 0.0, 2 * M_PI);
        cairo_close_path(context->c);
    } else {
        cairo_save(context->c);
        cairo_translate(context->c, cx, cy);
        cairo_scale(context->c, rx, ry);
        cairo_move_to(context->c, 1.0, 0.0);
        cairo_arc(context->c, 0.0, 0.0, 1.0, 0.0, 2 * M_PI);
        cairo_close_path(context->c);
        cairo_restore(context->c);
    }
}

static cairo_filter_t
convertInterpolationToCairoFilter(
    int interpolation)
{
    switch (interpolation) {
    case TK_PATH_IMAGEINTERPOLATION_None:
        return CAIRO_FILTER_FAST;
    case TK_PATH_IMAGEINTERPOLATION_Fast:
        return CAIRO_FILTER_GOOD;
    case TK_PATH_IMAGEINTERPOLATION_Best:
        return CAIRO_FILTER_BEST;
    }
    return CAIRO_FILTER_GOOD;
}

void
TkPathImage(
    TkPathContext ctx,
    Tk_Image image,
    Tk_PhotoHandle photo,
    double x,
    double y,
    double width0,
    double height0,
    double fillOpacity,
    XColor * tintColor,
    double tintAmount,
    int interpolation,
    TkPathRect * srcRegion)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    Tk_PhotoImageBlock block;
    cairo_surface_t *surface;
    cairo_format_t format;
    unsigned char *data = NULL;
    unsigned char *ptr = NULL;
    unsigned char *srcPtr, *dstPtr;
    int srcR, srcG, srcB, srcA; /* The source pixel offsets. */
    int dstR, dstG, dstB, dstA; /* The destination pixel offsets. */
    int pitch;
    int iwidth, iheight;
    int i, j;
    double width, height;
    cairo_filter_t filter;

    /* Return value? */
    Tk_PhotoGetImage(photo, &block);
    iwidth = block.width;
    iheight = block.height;
    pitch = block.pitch;
    width = (width0 == 0.0) ? (double)iwidth : width0;
    height = (height0 == 0.0) ? (double)iheight : height0;

    /*
     * @format: the format of pixels in the buffer
     * @width: the width of the image to be stored in the buffer
     * @height: the eight of the image to be stored in the buffer
     * @stride: the number of bytes between the start of rows
     *   in the buffer. Having this be specified separate from @width
     *   allows for padding at the end of rows, or for writing
     *   to a subportion of a larger image.
     */

    /*
     * cairo_format_t
     * @CAIRO_FORMAT_ARGB32: each pixel is a 32-bit quantity, with
     *   alpha in the upper 8 bits, then red, then green, then blue.
     *   The 32-bit quantities are stored native-endian. Pre-multiplied
     *   alpha is used. (That is, 50% transparent red is 0x80800000,
     *   not 0x80ff0000.)
     */
    if(block.pixelSize == 4) {
        format = CAIRO_FORMAT_ARGB32;

        /*
         * The offset array contains the offsets from the address of a
         * pixel to the addresses of the bytes containing the red, green,
         * blue and alpha (transparency) components.
         *
         * We need to copy pixel data from the source using the photo offsets
         * to cairos ARGB format which is in *native* endian order; Switch!
         */
        srcR = block.offset[0];
        srcG = block.offset[1];
        srcB = block.offset[2];
        srcA = block.offset[3];
        dstR = 1;
        dstG = 2;
        dstB = 3;
        dstA = 0;
        if(!kEndianess.set) {
            kEndianess.set = 1;
        }
        if(kEndianess.little) {
            dstR = 3 - dstR, dstG = 3 - dstG, dstB = 3 - dstB, dstA = 3 - dstA;
        }

        data = (unsigned char *)ckalloc(pitch * iheight);
        ptr = data;

        if(tintColor && tintAmount > 0.0) {
#ifdef TINT_INT_CALCULATION
            /* calculate with integer arithmetic */
    uint32_t tintR, tintG, tintB, uAmount, uRemain;
            if(tintAmount > 1.0)
                tintAmount = 1.0;
            uAmount = (uint32_t) (tintAmount * 256.0);
            uRemain = 256 - uAmount;
            tintR = Red255FromXColorPtr(tintColor);
            tintG = Green255FromXColorPtr(tintColor);
            tintB = Blue255FromXColorPtr(tintColor);

            for(i = 0; i < iheight; i++) {
                srcPtr = block.pixelPtr + i * pitch;
                dstPtr = ptr + i * pitch;
                for(j = 0; j < iwidth; j++) {
                    /* extract */
    uint32_t r = *(srcPtr + srcR);
    uint32_t g = *(srcPtr + srcG);
    uint32_t b = *(srcPtr + srcB);
    uint32_t a = *(srcPtr + srcA);
                    /* transform */
    uint32_t lumAmount = ((r * 6966 + g * 23436 + b * 2366)
                        * uAmount) >> 23;       /* 0-256 */

                    r = (uRemain * r + lumAmount * tintR);
                    g = (uRemain * g + lumAmount * tintG);
                    b = (uRemain * b + lumAmount * tintB);

                    if(a != 255) {
                        /* Cairo expects RGB premultiplied by alpha */
                        r = r * a / 255;
                        g = g * a / 255;
                        b = b * a / 255;
                    }

                    /* fix range */
                    r = r > 0xFFFF ? 0xFFFF : r;
                    g = g > 0xFFFF ? 0xFFFF : g;
                    b = b > 0xFFFF ? 0xFFFF : b;

                    /* and put back */
                    *(dstPtr + dstR) = r >> 8;
                    *(dstPtr + dstG) = g >> 8;
                    *(dstPtr + dstB) = b >> 8;
                    *(dstPtr + dstA) = a;
                    srcPtr += 4;
                    dstPtr += 4;
                }
            }
#else
    double tintR, tintG, tintB;
            if(tintAmount > 1.0)
                tintAmount = 1.0;
            tintR = RedDoubleFromXColorPtr(tintColor);
            tintG = GreenDoubleFromXColorPtr(tintColor);
            tintB = BlueDoubleFromXColorPtr(tintColor);

            for(i = 0; i < iheight; i++) {
                srcPtr = block.pixelPtr + i * pitch;
                dstPtr = ptr + i * pitch;
                for(j = 0; j < iwidth; j++) {
                    /* extract */
    int r = *(srcPtr + srcR);
    int g = *(srcPtr + srcG);
    int b = *(srcPtr + srcB);
    int a = *(srcPtr + srcA);
                    /* transform */
    int lum = (int)(0.2126 * r + 0.7152 * g + 0.0722 * b);

                    r = (int)((1.0 - tintAmount) * r +
                        tintAmount * lum * tintR);
                    g = (int)((1.0 - tintAmount) * g +
                        tintAmount * lum * tintG);
                    b = (int)((1.0 - tintAmount) * b +
                        tintAmount * lum * tintB);

                    if(a != 255) {
                        /* Cairo expects RGB premultiplied by alpha */
                        r = r * a / 255;
                        g = g * a / 255;
                        b = b * a / 255;
                    }

                    /* fix range */
                    r = r < 0 ? 0 : r > 255 ? 255 : r;
                    g = g < 0 ? 0 : g > 255 ? 255 : g;
                    b = b < 0 ? 0 : b > 255 ? 255 : b;

                    /* and put back */
                    *(dstPtr + dstR) = r;
                    *(dstPtr + dstG) = g;
                    *(dstPtr + dstB) = b;
                    *(dstPtr + dstA) = a;
                    srcPtr += 4;
                    dstPtr += 4;
                }
            }
#endif
        } else {
            for(i = 0; i < iheight; i++) {
                srcPtr = block.pixelPtr + i * pitch;
                dstPtr = ptr + i * pitch;
                for(j = 0; j < iwidth; j++) {
    unsigned int alpha = *(srcPtr + srcA);
                    *(dstPtr + dstA) = alpha;
                    if(alpha == 255) {
                        *(dstPtr + dstR) = *(srcPtr + srcR);
                        *(dstPtr + dstG) = *(srcPtr + srcG);
                        *(dstPtr + dstB) = *(srcPtr + srcB);
                    } else {
                        /* Cairo expects RGB premultiplied by alpha */
                        *(dstPtr + dstR) = alpha * *(srcPtr + srcR) / 255;
                        *(dstPtr + dstG) = alpha * *(srcPtr + srcG) / 255;
                        *(dstPtr + dstB) = alpha * *(srcPtr + srcB) / 255;
                    }
                    srcPtr += 4;
                    dstPtr += 4;
                }
            }
        }
    } else if(block.pixelSize == 3) {
        /* Could do something about this? */
        fprintf(stderr,
            "TkPathImage: unaccepted pixel format: 1 pixel is 3 bytes\n");
        return;
    } else {
        fprintf(stderr,
            "TkPathImage: unaccepted pixel format: 1 pixel is %d bytes\n",
            block.pixelSize);
        return;
    }
    surface = cairo_image_surface_create_for_data(ptr, format, (int)iwidth, (int)iheight, pitch);       /* stride */

    filter = convertInterpolationToCairoFilter(interpolation);
    if(width == (double)iwidth && height == (double)iheight && !srcRegion) {
        cairo_set_source_surface(context->c, surface, x, y);
        cairo_pattern_set_filter(cairo_get_source(context->c), filter);
        cairo_paint_with_alpha(context->c, fillOpacity);
    } else if(srcRegion) {
        /* crop x0, y0 positions: */
    int xcrop = srcRegion->x1;
    int ycrop = srcRegion->y1;
    double xscale, yscale, xoffs, yoffs;
    cairo_matrix_t matrix;
    cairo_pattern_t *pattern;

        width = (width0 == 0.0) ? srcRegion->x2 - srcRegion->x1 : width0;
        height = (height0 == 0.0) ? srcRegion->y2 - srcRegion->y1 : height0;
        /* scale image: */
        xscale = width / (srcRegion->x2 - srcRegion->x1);
        yscale = height / (srcRegion->y2 - srcRegion->y1);
        xoffs = xcrop * xscale;
        yoffs = ycrop * yscale;

        pattern = cairo_pattern_create_for_surface(surface);
        cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);

        cairo_translate(context->c, (x - xoffs), (y - yoffs));

        cairo_matrix_init_scale(&matrix, 1.0 / xscale, 1.0 / yscale);
        cairo_pattern_set_matrix(pattern, &matrix);

        cairo_set_source(context->c, pattern);

        cairo_pattern_set_filter(cairo_get_source(context->c), filter);
        cairo_rectangle(context->c, xoffs, yoffs, width, height);
        cairo_fill(context->c);

        cairo_pattern_destroy(pattern);
    } else {
        cairo_save(context->c);
        cairo_translate(context->c, x, y);
        cairo_scale(context->c, width / iwidth, height / iheight);
        cairo_set_source_surface(context->c, surface, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(context->c), filter);
        cairo_paint_with_alpha(context->c, fillOpacity);
        cairo_restore(context->c);
    }
    cairo_surface_destroy(surface);
    if(data) {
        ckfree((char *)data);
    }
}

void
TkPathClosePath(
    TkPathContext ctx)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_close_path(context->c);
}

int
TkPathTextConfig(
    Tcl_Interp * interp,
    Tk_PathTextStyle * textStylePtr,
    char *utf8,
    void **customPtr)
{
    return TCL_OK;
}

static cairo_font_slant_t
convertTkFontSlant2CairoFontSlant(
    enum TkFontSlant slant)
{
    switch (slant) {
    case TK_PATH_TEXT_SLANT_NORMAL:
        return CAIRO_FONT_SLANT_NORMAL;
    case TK_PATH_TEXT_SLANT_ITALIC:
        return CAIRO_FONT_SLANT_ITALIC;
    case TK_PATH_TEXT_SLANT_OBLIQUE:
        return CAIRO_FONT_SLANT_OBLIQUE;
    }
    return CAIRO_FONT_SLANT_NORMAL;
}

static cairo_font_weight_t
convertTkFontWeight2CairoFontWeight(
    enum TkFontWeight weight)
{
    switch (weight) {
    case TK_PATH_TEXT_WEIGHT_NORMAL:
        return CAIRO_FONT_WEIGHT_NORMAL;
    case TK_PATH_TEXT_WEIGHT_BOLD:
        return CAIRO_FONT_WEIGHT_BOLD;
    }
    return CAIRO_FONT_WEIGHT_NORMAL;
}

static char *
ckstrdup(
    const char *str)
{
    unsigned len = strlen(str);
    char *newstr = ckalloc(len + 1);

    if(newstr != NULL) {
        strcpy(newstr, str);
    }
    return newstr;
}

static char *
linebreak(
    char *str,
    char **nextp)
{
    char *ret;

    if(str == NULL) {
        str = *nextp;
    }
    str += strspn(str, "\r\n");
    if(*str == '\0') {
        return NULL;
    }
    ret = str;
    str += strcspn(str, "\r\n");
    if(*str) {
    int ch = *str;

        *str++ = '\0';
        if((ch == '\r') && (*str == '\n')) {
            str++;
        }
    }
    *nextp = str;
    return ret;
}

static void
multiline_show_text(
    TkPathContext ctx,
    double x,
    double y,
    double dy,
    char *utf8)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    char *token, *savep;
    char *str = ckstrdup(utf8);

    for(token = linebreak(str, &savep); token;
        token = linebreak(NULL, &savep), y += dy) {
        cairo_move_to(context->c, x, y);
        cairo_show_text(context->c, token);
    }
    ckfree(str);
}

static void
multiline_text_path(
    TkPathContext ctx,
    double x,
    double y,
    double dy,
    char *utf8)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    char *token, *savep;
    char *str = ckstrdup(utf8);

    for(token = linebreak(str, &savep); token;
        token = linebreak(NULL, &savep), y += dy) {
        cairo_move_to(context->c, x, y);
        cairo_text_path(context->c, token);
    }
    ckfree(str);
}

void
TkPathTextDraw(
    TkPathContext ctx,
    Tk_PathStyle * style,
    Tk_PathTextStyle * textStylePtr,
    double x,
    double y,
    int fillOverStroke,
    char *utf8,
    void *custom)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_font_extents_t fontExtents;
    int hasStroke = (style->strokeColor != NULL);
    int hasFill = (GetColorFromPathColor(style->fill) != NULL);

    cairo_select_font_face(context->c, textStylePtr->fontFamily,
        convertTkFontSlant2CairoFontSlant(textStylePtr->fontSlant),
        convertTkFontWeight2CairoFontWeight(textStylePtr->fontWeight));
    cairo_set_font_size(context->c, textStylePtr->fontSize);
    cairo_font_extents(context->c, &fontExtents);

    if(hasStroke && hasFill) {
        multiline_text_path(ctx, x, y,
            fontExtents.ascent + fontExtents.descent, utf8);
        if(fillOverStroke) {
            TkPathPrepareForStroke(ctx, style);
            cairo_stroke_preserve(context->c);
            CairoSetFill(ctx, style);
            cairo_fill(context->c);
        } else {
            TkPathFillAndStroke(ctx, style);
        }
    } else if(hasFill) {
        CairoSetFill(ctx, style);
        multiline_show_text(ctx, x, y,
            fontExtents.ascent + fontExtents.descent, utf8);
    } else if(hasStroke) {
        multiline_text_path(ctx, x, y,
            fontExtents.ascent + fontExtents.descent, utf8);
        TkPathStroke(ctx, style);
    }
}

void
TkPathTextFree(
    Tk_PathTextStyle * textStylePtr,
    void *custom)
{
    /* Empty. */
}

TkPathRect
TkPathTextMeasureBbox(
    Display * display,
    Tk_PathTextStyle * textStylePtr,
    char *utf8,
    double *lineSpacing,
    void *custom)
{
    cairo_t *c;
    cairo_surface_t *surface;
    cairo_text_extents_t extents;
    cairo_font_extents_t fontExtents;
    TkPathRect r;
    int lc;
    char *token, *savep;
    double x;
    char *str = ckstrdup(utf8);

    /*
     * @@@ Not very happy about this but it seems that there is no way to
     *     measure text without having a surface (drawable) in cairo.
     */
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
    c = cairo_create(surface);
    cairo_select_font_face(c, textStylePtr->fontFamily,
        convertTkFontSlant2CairoFontSlant(textStylePtr->fontSlant),
        convertTkFontWeight2CairoFontWeight(textStylePtr->fontWeight));
    cairo_set_font_size(c, textStylePtr->fontSize);

    cairo_font_extents(c, &fontExtents);

    r.x2 = 0.0;
    for(lc = 0, token = linebreak(str, &savep); token;
        lc++, token = linebreak(NULL, &savep)) {
        cairo_text_extents(c, token, &extents);
        x = extents.x_bearing + extents.width;
        if(x > r.x2)
            r.x2 = x;
    }
    r.y1 = -fontExtents.ascent;
    r.x1 = 0.0;
    r.y2 = lc * (fontExtents.ascent + fontExtents.descent) - fontExtents.ascent;

    if(lineSpacing != NULL) {
        *lineSpacing = fontExtents.ascent + fontExtents.descent;
    }

    cairo_destroy(c);
    cairo_surface_destroy(surface);
    ckfree(str);

    return r;
}

void
TkPathSurfaceErase(
    TkPathContext ctx,
    double dx,
    double dy,
    double dwidth,
    double dheight)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    unsigned char *data, *dst;
    int i;
    int x, y, width, height;
    int xend, yend;
    int stride;
    int bwidth;

    /*
     * Had to do it directly on the bits. Assuming CAIRO_FORMAT_ARGB32
     * cairos ARGB format is in *native* endian order; Switch!
     * Be careful not to address the bitmap outside its limits.
     */
    data = context->record->data;
    stride = context->record->stride;
    x = (int)(dx + 0.5);
    y = (int)(dy + 0.5);
    width = (int)(dwidth + 0.5);
    height = (int)(dheight + 0.5);
    x = MAX(0, MIN(context->record->width, x));
    y = MAX(0, MIN(context->record->height, y));
    width = MAX(0, width);
    height = MAX(0, height);
    xend = MIN(x + width, context->record->width);
    yend = MIN(y + height, context->record->height);
    bwidth = 4 * (xend - x);

    for(i = y; i < yend; i++) {
        dst = data + i * stride + 4 * x;
        memset(dst, '\0', bwidth);
    }
}

void
TkPathSurfaceToPhoto(
    Tcl_Interp * interp,
    TkPathContext ctx,
    Tk_PhotoHandle photo)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
cairo_surface_t *surface = context->surface;
Tk_PhotoImageBlock block;
unsigned char *data;
unsigned char *pixel;
int width, height;
int stride;                    /* Bytes per row. */

    width = cairo_image_surface_get_width(surface);
    height = cairo_image_surface_get_height(surface);
    data = context->record->data;
    stride = context->record->stride;

    Tk_PhotoGetImage(photo, &block);
    pixel = (unsigned char *)attemptckalloc(height * stride);
    if(pixel == NULL) {
        return;
    }

    if(Tk_PathSurfaceCopyPremultiplyAlpha) {
        if(!kEndianess.set) {
            kEndianess.set = 1;
        }
        if(kEndianess.little) {
            TkPathCopyBitsPremultipliedAlphaBGRA(data, pixel, width, height,
                stride);
        } else {
            TkPathCopyBitsPremultipliedAlphaARGB(data, pixel, width, height,
                stride);
        }
    } else {
        if(!kEndianess.set) {
            kEndianess.set = 1;
        }
        if(kEndianess.little) {
            TkPathCopyBitsBGRA(data, pixel, width, height, stride);
        } else {
            TkPathCopyBitsARGB(data, pixel, width, height, stride);
        }
    }
    block.pixelPtr = pixel;
    block.width = width;
    block.height = height;
    block.pitch = stride;
    block.pixelSize = 4;
    block.offset[0] = 0;
    block.offset[1] = 1;
    block.offset[2] = 2;
    block.offset[3] = 3;
    Tk_PhotoPutBlock(interp, photo, &block, 0, 0, width, height,
        TK_PHOTO_COMPOSITE_OVERLAY);
    ckfree((char *)pixel);
}

void
TkPathClipToPath(
    TkPathContext ctx,
    int fillRule)
{
    /* Clipping to path is done by default. */
    /* Note: cairo_clip does not consume the current path */
    /* cairo_clip(context->c); */
}

void
TkPathReleaseClipToPath(
    TkPathContext ctx)
{
    /* cairo_reset_clip(context->c); */
}

static void
TkPathPrepareForStroke(
    TkPathContext ctx,
    Tk_PathStyle * style)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
TkPathDash *dashPtr;

    /* Patch from Tim Edwards to handle color correctly on 64 bit arch. */
    cairo_set_source_rgba(context->c,
        (double)(style->strokeColor->red) / 0xFFFF,
        (double)(style->strokeColor->green) / 0xFFFF,
        (double)(style->strokeColor->blue) / 0xFFFF, style->strokeOpacity);
    cairo_set_line_width(context->c, style->strokeWidth);

    switch (style->capStyle) {
    case CapNotLast:
    case CapButt:
        cairo_set_line_cap(context->c, CAIRO_LINE_CAP_BUTT);
        break;
    case CapRound:
        cairo_set_line_cap(context->c, CAIRO_LINE_CAP_ROUND);
        break;
    default:
        cairo_set_line_cap(context->c, CAIRO_LINE_CAP_SQUARE);
        break;
    }
    switch (style->joinStyle) {
    case JoinMiter:
        cairo_set_line_join(context->c, CAIRO_LINE_JOIN_MITER);
        break;
    case JoinRound:
        cairo_set_line_join(context->c, CAIRO_LINE_JOIN_ROUND);
        break;
    default:
        cairo_set_line_join(context->c, CAIRO_LINE_JOIN_BEVEL);
        break;
    }
    cairo_set_miter_limit(context->c, style->miterLimit);

    dashPtr = style->dashPtr;
    if((dashPtr != NULL) && (dashPtr->number != 0)) {
int i;
double *dashes = (double *)ckalloc(dashPtr->number * sizeof(double));

        for(i = 0; i < dashPtr->number; i++) {
            dashes[i] = dashPtr->array[i] * style->strokeWidth;
        }
        cairo_set_dash(context->c, dashes, dashPtr->number, style->offset);
        ckfree((char *)dashes);
    }
}

void
TkPathStroke(
    TkPathContext ctx,
    Tk_PathStyle * style)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    TkPathPrepareForStroke(ctx, style);
    cairo_stroke(context->c);
}

void
TkPathFill(
    TkPathContext ctx,
    Tk_PathStyle * style)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    CairoSetFill(ctx, style);
    cairo_fill(context->c);
}

void
TkPathFillAndStroke(
    TkPathContext ctx,
    Tk_PathStyle * style)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    CairoSetFill(ctx, style);
    cairo_fill_preserve(context->c);
    TkPathStroke(ctx, style);
}

void
TkPathEndPath(
    TkPathContext ctx)
{
    /* Empty ??? */
}

void
TkPathFree(
    TkPathContext ctx)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_destroy(context->c);
    cairo_surface_destroy(context->surface);
    if(context->record) {
        ckfree((char *)context->record->data);
        ckfree((char *)context->record);
    }
    ckfree((char *)context);
}

int
TkPathDrawingDestroysPath(
    void)
{
    return 1;
}

int
TkPathPixelAlign(
    void)
{
    return 0;
}

int
TkPathGetCurrentPosition(
    TkPathContext ctx,
    TkPathPoint * pt)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    cairo_get_current_point(context->c, &(pt->x), &(pt->y));
    return TCL_OK;
}

int
TkPathBoundingBox(
    TkPathContext ctx,
    TkPathRect * rPtr)
{
    return TCL_ERROR;
}

static int
GetCairoExtend(
    int method)
{
    cairo_extend_t extend;

    switch (method) {
    case TK_PATH_GRADIENTMETHOD_Pad:
        extend = CAIRO_EXTEND_PAD;
        break;
    case TK_PATH_GRADIENTMETHOD_Repeat:
        extend = CAIRO_EXTEND_REPEAT;
        break;
    case TK_PATH_GRADIENTMETHOD_Reflect:
        extend = CAIRO_EXTEND_REFLECT;
        break;
    default:
        extend = CAIRO_EXTEND_NONE;
        break;
    }
    return extend;
}

void
TkPathPaintLinearGradient(
    TkPathContext ctx,
    TkPathRect * bbox,
    TkLinearGradientFill * fillPtr,
    int fillRule,
    double fillOpacity,
    TkPathMatrix * mPtr)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    int i;
    int nstops;
    TkPathRect *tPtr;          /* The transition line. */
    TkGradientStop *stop;
    TkGradientStopArray *stopArrPtr;
    cairo_pattern_t *pattern;

    stopArrPtr = fillPtr->stopArrPtr;
    tPtr = fillPtr->transitionPtr;
    nstops = stopArrPtr->nstops;

    /*
     * The current path is consumed by filling.
     * Need therfore to save the current context and restore after.
     */
    cairo_save(context->c);

    pattern = cairo_pattern_create_linear(tPtr->x1, tPtr->y1,
        tPtr->x2, tPtr->y2);

    /*
     * We need to do like this since this is how SVG defines gradient drawing
     * in case the transition vector is in relative coordinates.
     */
    if(fillPtr->units == TK_PATH_GRADIENTUNITS_BoundingBox) {
        cairo_translate(context->c, bbox->x1, bbox->y1);
        cairo_scale(context->c, bbox->x2 - bbox->x1, bbox->y2 - bbox->y1);
    }
    if(mPtr) {
    cairo_matrix_t matrix;
        cairo_matrix_init(&matrix, mPtr->a, mPtr->b, mPtr->c, mPtr->d,
            mPtr->tx, mPtr->ty);
        cairo_pattern_set_matrix(pattern, &matrix);
    }

    for(i = 0; i < nstops; i++) {
        stop = stopArrPtr->stops[i];
        cairo_pattern_add_color_stop_rgba(pattern, stop->offset,
            RedDoubleFromXColorPtr(stop->color),
            GreenDoubleFromXColorPtr(stop->color),
            BlueDoubleFromXColorPtr(stop->color), stop->opacity * fillOpacity);
    }
    cairo_set_source(context->c, pattern);
    cairo_set_fill_rule(context->c,
        (fillRule == WindingRule) ? CAIRO_FILL_RULE_WINDING :
        CAIRO_FILL_RULE_EVEN_ODD);
    cairo_pattern_set_extend(pattern, GetCairoExtend(fillPtr->method));
    cairo_fill(context->c);

    cairo_pattern_destroy(pattern);
    cairo_restore(context->c);
}

void
TkPathPaintRadialGradient(
    TkPathContext ctx,
    TkPathRect * bbox,
    TkRadialGradientFill * fillPtr,
    int fillRule,
    double fillOpacity,
    TkPathMatrix * mPtr)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    int i;
    int nstops;
    TkGradientStop *stop;
    cairo_pattern_t *pattern;
    TkGradientStopArray *stopArrPtr;
    TkRadialTransition *tPtr;

    stopArrPtr = fillPtr->stopArrPtr;
    nstops = stopArrPtr->nstops;
    tPtr = fillPtr->radialPtr;

    /*
     * The current path is consumed by filling.
     * Need therfore to save the current context and restore after.
     */
    cairo_save(context->c);
    pattern = cairo_pattern_create_radial(tPtr->focalX, tPtr->focalY, 0.0,
        tPtr->centerX, tPtr->centerY, tPtr->radius);

    if(fillPtr->units == TK_PATH_GRADIENTUNITS_BoundingBox) {
        cairo_translate(context->c, bbox->x1, bbox->y1);
        cairo_scale(context->c, bbox->x2 - bbox->x1, bbox->y2 - bbox->y1);
    }
    if(mPtr) {
    cairo_matrix_t matrix;
        cairo_matrix_init(&matrix, mPtr->a, mPtr->b, mPtr->c, mPtr->d,
            mPtr->tx, mPtr->ty);
        cairo_pattern_set_matrix(pattern, &matrix);
    }

    for(i = 0; i < nstops; i++) {
        stop = stopArrPtr->stops[i];
        cairo_pattern_add_color_stop_rgba(pattern, stop->offset,
            RedDoubleFromXColorPtr(stop->color),
            GreenDoubleFromXColorPtr(stop->color),
            BlueDoubleFromXColorPtr(stop->color), stop->opacity * fillOpacity);
    }
    cairo_set_source(context->c, pattern);
    cairo_set_fill_rule(context->c,
        (fillRule == WindingRule) ? CAIRO_FILL_RULE_WINDING :
        CAIRO_FILL_RULE_EVEN_ODD);
    cairo_pattern_set_extend(pattern, GetCairoExtend(fillPtr->method));
    cairo_fill(context->c);

    cairo_pattern_destroy(pattern);
    cairo_restore(context->c);
}

int
TkPathSetup(
    Tcl_Interp * interp)
{
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
