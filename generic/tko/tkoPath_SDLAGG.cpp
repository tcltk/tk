/*
 * tkSDLAGGPath.cpp --
 *
 *        This file implements path drawing API's using SDL/Agg2D.
 *
 * Copyright (c) 2015, 2016 Christian Werner
 */

/* This should go into configure.in but don't know how. */
#ifdef USE_PANIC_ON_PHOTO_ALLOC_FAILURE
#undef USE_PANIC_ON_PHOTO_ALLOC_FAILURE
#endif

#include <tkSDLInt.h>
#include <SdlTkInt.h>

#include "tkoPath.h"

/* Avoid name clashes with Agg2D */

static const int X11_JoinMiter = JoinMiter;
static const int X11_JoinRound = JoinRound;
static const int X11_JoinBevel = JoinBevel;

#undef JoinMiter
#undef JoinRound
#undef JoinBevel

static const int X11_CapButt = CapButt;
static const int X11_CapRound = CapRound;
static const int X11_CapSquare = CapProjecting;

#undef CapButt
#undef CapRound
#undef CapProjecting
#undef CapSquare

/* XColor components to 8 bit unsigned */

#define XC_R(xc) ((xc)->red >> 8)
#define XC_G(xc) ((xc)->green >> 8)
#define XC_B(xc) ((xc)->blue >> 8)

#include "agg2d.h"

/*
 * Agg2D state for save/restore in surface.
 */

typedef struct {
    Agg2D::Color fillColor;
    Agg2D::Color lineColor;
    Agg2D::LineCap lineCap;
    Agg2D::LineJoin lineJoin;
    double lineWidth;
    bool fillEvenOdd;
        Agg2D::BlendMode blendMode;
        Agg2D::Transformations trans;
    int widthCode;             /* Used to depixelize the strokes: */
    /* 0: not integer width */
    /* 1: odd integer width */
    /* 2: even integer width */
} Agg2DState;

/*
 * This is used as a place holder for platform dependent stuff
 * between each call.
 */

typedef struct TkPathContext_ {
    Display *display;
    Agg2D *agg2d;
    double x, y;
    int width, height, widthCode;
    unsigned char *fb;         /* Frame buffer for surface or NULL. */
    int stack;                 /* State stack pointer. */
    Agg2DState states[8];      /* State stack for surface. */
} TkPathContext_;

/*
 * This is used to keep text information.
 */

typedef struct {
    char fontName[64];         /* Used as font name in Agg2D::font(). */
    const char *fontFile;      /* Used for XGetFTStream(). */
    int fontFileSize;          /* Used for XGetFTStream(). */
    int nLines;                /* Number of lines of text. */
    Tcl_DString uniString;     /* UCS-4 encoded multi-line text. */
} TextConf;

MODULE_SCOPE int gDepixelize;
MODULE_SCOPE int gSurfaceCopyPremultiplyAlpha;

static int
strlenU(
    const unsigned *string)
{
    int length = 0;
    while(*string != 0) {
        length++;
        string++;
    }
    return length;
}

/*
 * Standard tkpath interface.
 */

TkPathContext
TkPathInit(
    Tk_Window tkwin,
    Drawable d)
{
TkPathContext_ *context = (TkPathContext_ *) ckalloc(sizeof(TkPathContext_));
    context->display = Tk_Display(tkwin);
    context->agg2d = (Agg2D *) XGetAgg2D(context->display, d);
    context->x = context->y = 0;
    context->fb = NULL;
    context->stack = 0;
    context->agg2d->flipText(true);
    context->agg2d->masterAlpha(1.0);
    context->agg2d->imageResample(Agg2D::ResampleAlways);
    return (TkPathContext) context;
}

TkPathContext
TkPathInitSurface(
    Display * display,
    int width,
    int height)
{
    if((width <= 0) || (height <= 0)) {
        return (TkPathContext) NULL;
    }
Agg2D *agg2d = (Agg2D *) XCreateAgg2D(display);
    if(agg2d != NULL) {
TkPathContext_ *context =
    (TkPathContext_ *) attemptckalloc(sizeof(TkPathContext_));
        if(context == NULL) {
            goto error;
        }
        context->display = display;
        context->agg2d = agg2d;
        context->x = context->y = 0;
        context->width = width;
        context->height = height;
unsigned int fbsize = width * height * 4;
        context->fb = (unsigned char *)attemptckalloc(fbsize);
        if(context->fb == NULL) {
            ckfree((char *)context);
            goto error;
        }
        memset(context->fb, 0, fbsize);
        context->stack = 0;
        context->agg2d->attach(context->fb, width, height, width * 4);
        context->agg2d->flipText(true);
        context->agg2d->antiAliasGamma(1.5);
        context->agg2d->masterAlpha(0.9);
        context->agg2d->imageResample(Agg2D::ResampleAlways);
        return (TkPathContext) context;
    }
  error:
    if(agg2d != NULL) {
        XDestroyAgg2D(display, (void *)agg2d);
    }
    return (TkPathContext) NULL;
}

void
TkPathPushTMatrix(
    TkPathContext ctx,
    TMatrix * m)
{
    if(m == NULL) {
        return;
    }
TkPathContext_ *context = (TkPathContext_ *) ctx;
    Agg2D::Affine newTrans(m->a, m->b, m->c, m->d, m->tx, m->ty);
    Agg2D::Affine oldTrans(context->agg2d->transformations().affineMatrix);
    context->agg2d->resetTransformations();
    /* Order is important! */
    context->agg2d->affine(newTrans);
    context->agg2d->affine(oldTrans);
}

void
TkPathResetTMatrix(
    TkPathContext ctx)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    context->agg2d->resetTransformations();
}

void
TkPathSaveState(
    TkPathContext ctx)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
unsigned i = context->stack;
    if(i + 1 >= sizeof(context->states) / sizeof(context->states[0])) {
        Tcl_Panic("out of path context stack space");
    }
Agg2DState *state = &context->states[i];
    state->fillColor = context->agg2d->fillColor();
    state->lineColor = context->agg2d->lineColor();
    state->lineCap = context->agg2d->lineCap();
    state->lineJoin = context->agg2d->lineJoin();
    state->lineWidth = context->agg2d->lineWidth();
    state->fillEvenOdd = context->agg2d->fillEvenOdd();
    state->blendMode = context->agg2d->blendMode();
    state->trans = context->agg2d->transformations();
    state->widthCode = context->widthCode;
    context->stack = i + 1;
}

void
TkPathRestoreState(
    TkPathContext ctx)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
unsigned i = context->stack;
    if(i <= 0) {
        return;
    }
    --i;
Agg2DState *state = &context->states[i];
    context->agg2d->fillColor(state->fillColor);
    context->agg2d->lineColor(state->lineColor);
    context->agg2d->lineCap(state->lineCap);
    context->agg2d->lineJoin(state->lineJoin);
    context->agg2d->lineWidth(state->lineWidth);
    context->agg2d->fillEvenOdd(state->fillEvenOdd);
    context->agg2d->blendMode(state->blendMode);
    context->agg2d->transformations(state->trans);
    context->widthCode = state->widthCode;
    context->stack = i;
}

void
TkPathBeginPath(
    TkPathContext ctx,
    Tk_PathStyle * style)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
int nint;
double width;
    context->agg2d->resetPath();
    context->agg2d->fillEvenOdd(style->fillRule != WindingRule);
    if(style->strokeColor == NULL) {
        context->widthCode = 0;
    } else {
        width = style->strokeWidth;
        nint = (int)(width + 0.5);
        context->widthCode = (fabs(width - nint) > 0.01) ? 0 : (2 - nint % 2);
    }
}

void
TkPathMoveTo(
    TkPathContext ctx,
    double x,
    double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if(gDepixelize) {
        x = PATH_DEPIXELIZE(context->widthCode, x);
        y = PATH_DEPIXELIZE(context->widthCode, y);
    }
    context->agg2d->moveTo(x, y);
    context->x = x;
    context->y = y;
}

void
TkPathLineTo(
    TkPathContext ctx,
    double x,
    double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    context->agg2d->lineTo(x, y);
    if(gDepixelize) {
        x = PATH_DEPIXELIZE(context->widthCode, x);
        y = PATH_DEPIXELIZE(context->widthCode, y);
    }
    context->x = x;
    context->y = y;
}

void
TkPathLinesTo(
    TkPathContext ctx,
    double *pts,
    int n)
{
    /* @@@ TODO */
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
    if(gDepixelize) {
        x = PATH_DEPIXELIZE(context->widthCode, x);
        y = PATH_DEPIXELIZE(context->widthCode, y);
    }
    context->agg2d->quadricCurveTo(ctrlX, ctrlY, x, y);
    context->x = x;
    context->y = y;
}

void
TkPathCurveTo(
    TkPathContext ctx,
    double ctrlX1,
    double ctrlY1,
    double ctrlX2,
    double ctrlY2,
    double x,
    double y)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if(gDepixelize) {
        x = PATH_DEPIXELIZE(context->widthCode, x);
        y = PATH_DEPIXELIZE(context->widthCode, y);
    }
    context->agg2d->cubicCurveTo(ctrlX1, ctrlY1, ctrlX2, ctrlY2, x, y);
    context->x = x;
    context->y = y;
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
    if(gDepixelize) {
        x = PATH_DEPIXELIZE(context->widthCode, x);
        y = PATH_DEPIXELIZE(context->widthCode, y);
    }
    double phi = context->agg2d->deg2Rad(phiDegrees);
    context->agg2d->arcTo(rx, ry, phi, largeArcFlag, sweepFlag, x, y);
    context->x = x;
    context->y = y;
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
    if(gDepixelize) {
        x = PATH_DEPIXELIZE(context->widthCode, x);
        y = PATH_DEPIXELIZE(context->widthCode, y);
    }
    context->agg2d->closePolygon();
    context->agg2d->moveTo(x, y);
    context->agg2d->lineRel(width, 0);
    context->agg2d->lineRel(0, height);
    context->agg2d->lineRel(-width, 0);
    context->agg2d->closePolygon();
    context->x = x;
    context->y = y;
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
    context->agg2d->closePolygon();
    context->agg2d->addEllipse(cx, cy, rx, ry, Agg2D::CCW);
    context->x = cx;
    context->y = cy;
}

void
TkPathImage(
    TkPathContext ctx,
    Tk_Image image,
    Tk_PhotoHandle photo,
    double x,
    double y,
    double width,
    double height,
    double fillOpacity,
    XColor * tintColor,
    double tintAmount,
    int interpolation,
    PathRect * srcRegion)
{
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    Tk_PhotoImageBlock block;
    Tk_PhotoGetImage(photo, &block);
    int srcX = (srcRegion != NULL) ? srcRegion->x1 : 0;
    int srcY = (srcRegion != NULL) ? srcRegion->y1 : 0;
    int srcWidth = (srcRegion != NULL) ?
        (srcRegion->x2 - srcRegion->x1) : block.width;
    int srcHeight = (srcRegion != NULL) ?
        (srcRegion->y2 - srcRegion->y1) : block.height;
    if(width == 0.0) {
        width = srcWidth;
    }
    if(height == 0.0) {
        height = srcHeight;
    }
    if(fillOpacity > 1.0) {
        fillOpacity = 1.0;
    } else if(fillOpacity < 0.0) {
        fillOpacity = 0.0;
    }
    double tintR, tintG, tintB;
    if((tintColor != NULL) && (tintAmount > 0.0)) {
        if(tintAmount > 1.0) {
            tintAmount = 1.0;
        }
        tintR = (double)XC_R(tintColor) / 0xFF;
        tintG = (double)XC_G(tintColor) / 0xFF;
        tintB = (double)XC_B(tintColor) / 0xFF;
    } else {
        tintAmount = 0.0;
        tintR = 0.0;
        tintG = 0.0;
        tintB = 0.0;
    }
    unsigned char *data = NULL;
    int iPitch;
    if(block.pixelSize == 4) {
    int srcR, srcG, srcB, srcA;
    int dstR, dstG, dstB, dstA;
        iPitch = block.width * 4;
        srcR = block.offset[0];
        srcG = block.offset[1];
        srcB = block.offset[2];
        srcA = block.offset[3];
        dstR = 2;
        dstG = 1;
        dstB = 0;
        dstA = 3;
        data = (unsigned char *)attemptckalloc(iPitch * block.height);
        if(data == NULL) {
            return;
        }
    int i, j;
        for(i = 0; i < block.height; i++) {
    unsigned char *srcPtr = block.pixelPtr + i * block.pitch;
    unsigned char *dstPtr = data + i * iPitch;
    unsigned char R, G, B;
            for(j = 0; j < block.width; j++) {
                R = *(srcPtr + srcR);
                G = *(srcPtr + srcG);
                B = *(srcPtr + srcB);
                if(tintAmount > 0.0) {
    int RR, GG, BB;
                    RR = (1.0 - tintAmount) * R +
                        (tintAmount * tintR * 0.2126 * R +
                        tintAmount * tintR * 0.7152 * G +
                        tintAmount * tintR * 0.0722 * B);
                    GG = (1.0 - tintAmount) * G +
                        (tintAmount * tintG * 0.2126 * R +
                        tintAmount * tintG * 0.7152 * G +
                        tintAmount * tintG * 0.0722 * B);
                    BB = (1.0 - tintAmount) * B +
                        (tintAmount * tintB * 0.2126 * R +
                        tintAmount * tintB * 0.7152 * G +
                        tintAmount * tintB * 0.0722 * B);
                    R = (RR > 0xFF) ? 0xFF : RR;
                    G = (GG > 0xFF) ? 0xFF : GG;
                    B = (BB > 0xFF) ? 0xFF : BB;
                }
                if(fillOpacity < 1.0) {
                    *(dstPtr + dstR) = R * fillOpacity;
                    *(dstPtr + dstG) = G * fillOpacity;
                    *(dstPtr + dstB) = B * fillOpacity;
                    *(dstPtr + dstA) = *(srcPtr + srcA) * fillOpacity;
                } else {
                    *(dstPtr + dstR) = R;
                    *(dstPtr + dstG) = G;
                    *(dstPtr + dstB) = B;
                    *(dstPtr + dstA) = *(srcPtr + srcA);
                }
                srcPtr += 4;
                dstPtr += 4;
            }
        }
    } else if(block.pixelSize == 3) {
    int srcR, srcG, srcB;
    int dstR, dstG, dstB, dstA;
        iPitch = block.width * 4;
        srcR = block.offset[0];
        srcG = block.offset[1];
        srcB = block.offset[2];
        dstR = 2;
        dstG = 1;
        dstB = 0;
        dstA = 3;
        data = (unsigned char *)attemptckalloc(iPitch * block.height);
        if(data == NULL) {
            return;
        }
    int i, j;
        for(i = 0; i < block.height; i++) {
    unsigned char *srcPtr = block.pixelPtr + i * block.pitch;
    unsigned char *dstPtr = data + i * iPitch;
    unsigned char R, G, B;
            for(j = 0; j < block.width; j++) {
                R = *(srcPtr + srcR);
                G = *(srcPtr + srcG);
                B = *(srcPtr + srcB);
                if(tintAmount > 0.0) {
    int RR, GG, BB;
                    RR = (1.0 - tintAmount) * R +
                        (tintAmount * tintR * 0.2126 * R +
                        tintAmount * tintR * 0.7152 * G +
                        tintAmount * tintR * 0.0722 * B);
                    GG = (1.0 - tintAmount) * G +
                        (tintAmount * tintG * 0.2126 * R +
                        tintAmount * tintG * 0.7152 * G +
                        tintAmount * tintG * 0.0722 * B);
                    BB = (1.0 - tintAmount) * B +
                        (tintAmount * tintB * 0.2126 * R +
                        tintAmount * tintB * 0.7152 * G +
                        tintAmount * tintB * 0.0722 * B);
                    R = (RR > 0xFF) ? 0xFF : RR;
                    G = (GG > 0xFF) ? 0xFF : GG;
                    B = (BB > 0xFF) ? 0xFF : BB;
                }
                if(fillOpacity < 1.0) {
                    *(dstPtr + dstR) = R * fillOpacity;
                    *(dstPtr + dstG) = G * fillOpacity;
                    *(dstPtr + dstB) = B * fillOpacity;
                    *(dstPtr + dstA) = 0xFF * fillOpacity;
                } else {
                    *(dstPtr + dstR) = R;
                    *(dstPtr + dstG) = G;
                    *(dstPtr + dstB) = B;
                    *(dstPtr + dstA) = 0xFF;
                }
                srcPtr += 3;
                dstPtr += 4;
            }
        }
    } else if(block.pixelSize == 1) {
    int srcC;
    int dstR, dstG, dstB, dstA;
        iPitch = block.width * 4;
        srcC = block.offset[0];
        dstR = 2;
        dstG = 1;
        dstB = 0;
        dstA = 3;
        data = (unsigned char *)attemptckalloc(iPitch * block.height);
        if(data == NULL) {
            return;
        }
    int i, j;
        for(i = 0; i < block.height; i++) {
    unsigned char *srcPtr = block.pixelPtr + i * block.pitch;
    unsigned char *dstPtr = data + i * iPitch;
    unsigned char R, G, B;
            for(j = 0; j < block.width; j++) {
                R = *(srcPtr + srcC);
                G = *(srcPtr + srcC);
                B = *(srcPtr + srcC);
                if(tintAmount > 0.0) {
    int RR, GG, BB;
                    RR = (1.0 - tintAmount) * R +
                        (tintAmount * tintR * 0.2126 * R +
                        tintAmount * tintR * 0.7152 * G +
                        tintAmount * tintR * 0.0722 * B);
                    GG = (1.0 - tintAmount) * G +
                        (tintAmount * tintG * 0.2126 * R +
                        tintAmount * tintG * 0.7152 * G +
                        tintAmount * tintG * 0.0722 * B);
                    BB = (1.0 - tintAmount) * B +
                        (tintAmount * tintB * 0.2126 * R +
                        tintAmount * tintB * 0.7152 * G +
                        tintAmount * tintB * 0.0722 * B);
                    R = (RR > 0xFF) ? 0xFF : RR;
                    G = (GG > 0xFF) ? 0xFF : GG;
                    B = (BB > 0xFF) ? 0xFF : BB;
                }
                if(fillOpacity < 1.0) {
                    *(dstPtr + dstR) = R * fillOpacity;
                    *(dstPtr + dstG) = G * fillOpacity;
                    *(dstPtr + dstB) = B * fillOpacity;
                    *(dstPtr + dstA) = 0xFF * fillOpacity;
                } else {
                    *(dstPtr + dstR) = R;
                    *(dstPtr + dstG) = G;
                    *(dstPtr + dstB) = B;
                    *(dstPtr + dstA) = 0xFF;
                }
                srcPtr += 1;
                dstPtr += 4;
            }
        }
    } else {
        return;
    }
    Agg2D::Image img(data, block.width, block.height, iPitch);
    Agg2D::ImageFilter filter;
    switch (interpolation) {
    default:
    case kPathImageInterpolationNone:
        filter = Agg2D::NoFilter;
        break;
    case kPathImageInterpolationFast:
        filter = Agg2D::Bilinear;
        break;
    case kPathImageInterpolationBest:
        filter = Agg2D::Bicubic;
        break;
    }
    if(srcRegion != NULL) {
        context->agg2d->imageWrapMode(Agg2D::WrapRepeat);
    }
    context->agg2d->imageFilter(filter);
    Agg2D::Affine oldTrans(context->agg2d->transformations().affineMatrix);
    context->agg2d->resetTransformations();
    context->agg2d->translate(x, y);
    context->agg2d->affine(oldTrans);
    context->agg2d->transformImage(img, srcX, srcY,
        srcX + srcWidth, srcY + srcHeight, 0, 0, width, height);
    if(data != NULL) {
        ckfree((char *)data);
    }
}

void
TkPathClosePath(
    TkPathContext ctx)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    context->agg2d->closePolygon();
}

int
TkPathTextConfig(
    Tcl_Interp * interp,
    Tk_PathTextStyle * textStylePtr,
    char *utf8,
    void **customPtr)
{
    const char *fontFile;
    int fontFileSize;
    if(!XGetFontFile(textStylePtr->fontFamily, textStylePtr->fontSize,
            textStylePtr->fontWeight == PATH_TEXT_WEIGHT_BOLD,
            (textStylePtr->fontSlant == PATH_TEXT_SLANT_ITALIC) ||
            (textStylePtr->fontSlant == PATH_TEXT_SLANT_OBLIQUE),
            &fontFile, &fontFileSize)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("font not found", -1));
        return TCL_ERROR;
    }
    TextConf *tconf = (TextConf *) ckalloc(sizeof(TextConf));
    sprintf(tconf->fontName, "font_%p_0x%08x", fontFile, fontFileSize);
    tconf->fontFile = fontFile;
    tconf->fontFileSize = fontFileSize;
    Tcl_DStringInit(&tconf->uniString);
    tconf->nLines = 1;
    int length = strlen(utf8);
    Tcl_DStringSetLength(&tconf->uniString, length * sizeof(unsigned));
    Tcl_DStringSetLength(&tconf->uniString, 0);
    const char *p = utf8;
    unsigned chu;
    while(p < utf8 + length) {
    Tcl_UniChar ch;
    int next = Tcl_UtfToUniChar(p, &ch);
        chu = ch;
        if(chu == '\n') {
            chu = 0;    /* end-of-line marker */
            tconf->nLines++;
        } else if(chu == '\r') {
            chu = 0;    /* end-of-line marker */
            if(p[next] == '\n') {
                ++next;
            }
            tconf->nLines++;
        } else if(chu == '\t') {
            /* two blanks */
            chu = ' ';
            Tcl_DStringAppend(&tconf->uniString, (const char *)&chu,
                sizeof(unsigned));
        } else if(chu < ' ') {
            goto skip;
        }
        Tcl_DStringAppend(&tconf->uniString, (const char *)&chu,
            sizeof(unsigned));
      skip:
        p += next;
    }
    chu = 0;   /* end-of-line marker */
    Tcl_DStringAppend(&tconf->uniString, (const char *)&chu, sizeof(unsigned));
    *customPtr = (void *)tconf;
    return TCL_OK;
}

static void
textLineU(
    Agg2D * agg2d,
    double &x,
    double &y,
    const unsigned **string)
{
    int length = strlenU(*string);
    agg2d->textU(x, y, *string, length);
    *string += length + 1;
    y += agg2d->fontAscent() - agg2d->fontDescent();
    agg2d->resetPath();
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
    TextConf *tconf = (TextConf *) custom;
    context->agg2d->font(tconf->fontName, textStylePtr->fontSize,
        textStylePtr->fontWeight == PATH_TEXT_WEIGHT_BOLD,
        (textStylePtr->fontSlant == PATH_TEXT_SLANT_ITALIC) ||
        (textStylePtr->fontSlant == PATH_TEXT_SLANT_OBLIQUE),
        Agg2D::VectorFontCache, 0.0,
        (const char *)XGetFTStream(tconf->fontFile, tconf->fontFileSize), 0);
    int hasStroke = style->strokeColor != NULL;
    int hasFill = (style->fill != NULL) && (style->fill->color != NULL);
    double opacity, tx, ty;
    const unsigned *string;
    if(fillOverStroke && hasFill && hasStroke) {
        context->agg2d->lineWidth(style->strokeWidth);
        context->agg2d->miterLimit(style->miterLimit);
        opacity = style->strokeOpacity;
        if(opacity > 1.0) {
            opacity = 1.0;
        } else if(opacity < 0.0) {
            opacity = 0.0;
        }
        context->agg2d->lineColor(XC_R(style->strokeColor),
            XC_G(style->strokeColor),
            XC_B(style->strokeColor), (unsigned)(opacity * 0xFF));
        context->agg2d->noFill();
        string = (const unsigned *)Tcl_DStringValue(&tconf->uniString);
        tx = x;
        ty = y;
        for(int i = 0; i < tconf->nLines; i++) {
            textLineU(context->agg2d, tx, ty, &string);
        }
        opacity = style->fillOpacity;
        if(opacity > 1.0) {
            opacity = 1.0;
        } else if(opacity < 0.0) {
            opacity = 0.0;
        }
        context->agg2d->fillColor(XC_R(style->fill->color),
            XC_G(style->fill->color),
            XC_B(style->fill->color), (unsigned)(opacity * 0xFF));
        context->agg2d->noLine();
        string = (const unsigned *)Tcl_DStringValue(&tconf->uniString);
        tx = x;
        ty = y;
        for(int i = 0; i < tconf->nLines; i++) {
            textLineU(context->agg2d, tx, ty, &string);
        }
    } else if(hasStroke || hasFill) {
        if(hasFill) {
            opacity = style->fillOpacity;
            if(opacity > 1.0) {
                opacity = 1.0;
            } else if(opacity < 0.0) {
                opacity = 0.0;
            }
            context->agg2d->fillColor(XC_R(style->fill->color),
                XC_G(style->fill->color),
                XC_B(style->fill->color), (unsigned)(opacity * 0xFF));
        } else {
            context->agg2d->noFill();
        }
        if(hasStroke) {
            opacity = style->strokeOpacity;
            if(opacity > 1.0) {
                opacity = 1.0;
            } else if(opacity < 0.0) {
                opacity = 0.0;
            }
            context->agg2d->lineWidth(style->strokeWidth);
            context->agg2d->miterLimit(style->miterLimit);
            context->agg2d->lineColor(XC_R(style->strokeColor),
                XC_G(style->strokeColor),
                XC_B(style->strokeColor), (unsigned)(opacity * 0xFF));
        } else {
            context->agg2d->noLine();
        }
        string = (const unsigned *)Tcl_DStringValue(&tconf->uniString);
        tx = x;
        ty = y;
        for(int i = 0; i < tconf->nLines; i++) {
            textLineU(context->agg2d, tx, ty, &string);
        }
    }
}

void
TkPathTextFree(
    Tk_PathTextStyle * textStylePtr,
    void *custom)
{
    if(custom != NULL) {
    TextConf *tconf = (TextConf *) custom;
        Tcl_DStringFree(&tconf->uniString);
        ckfree((char *)custom);
    }
}

PathRect
TkPathTextMeasureBbox(
    Display * display,
    Tk_PathTextStyle * textStylePtr,
    char *utf8,
    double *lineSpacing,
    void *custom)
{
    PathRect rect = { 0.0, 0.0, 0.0, 0.0 };
    if(custom == NULL) {
        return rect;
    }
    Agg2D *agg2d = (Agg2D *) XGetAgg2D(display, None);
    if(agg2d == NULL) {
        return rect;
    }
    TextConf *tconf = (TextConf *) custom;
    agg2d->font(tconf->fontName, textStylePtr->fontSize,
        textStylePtr->fontWeight == PATH_TEXT_WEIGHT_BOLD,
        (textStylePtr->fontSlant == PATH_TEXT_SLANT_ITALIC) ||
        (textStylePtr->fontSlant == PATH_TEXT_SLANT_OBLIQUE),
        Agg2D::VectorFontCache, 0.0,
        (const char *)XGetFTStream(tconf->fontFile, tconf->fontFileSize), 0);
    rect.y1 = 0.0 - agg2d->fontAscent();
    rect.y2 = tconf->nLines * (agg2d->fontAscent() - agg2d->fontDescent())
        - agg2d->fontAscent();
    const unsigned *string =
        (const unsigned *)Tcl_DStringValue(&tconf->uniString);
    for(int i = 0; i < tconf->nLines; i++) {
    int length = strlenU(string);
    double x2 = agg2d->textWidthU(string, length);
        if(x2 > rect.x2) {
            rect.x2 = x2;
        }
        string += length + 1;
    }
    if(lineSpacing != NULL) {
        *lineSpacing = agg2d->fontAscent() - agg2d->fontDescent();
    }
    XDestroyAgg2D(display, (void *)agg2d);
    return rect;
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
    if(context->fb == NULL) {
        return;
    }
    int x = (int)(dx + 0.5);
    int y = (int)(dy + 0.5);
    int width = (int)(dwidth + 0.5);
    int height = (int)(dheight + 0.5);
    x = MAX(0, MIN(context->width, x));
    y = MAX(0, MIN(context->height, y));
    width = MAX(0, width);
    height = MAX(0, height);
    int xend = MIN(x + width, context->width);
    int yend = MIN(y + height, context->height);
    int bwidth = (xend - x) * 4;
    int stride = context->width * 4;
    for(int i = y; i < yend; i++) {
    unsigned char *dst = context->fb + i * stride + x * 4;
        memset(dst, 0, bwidth);
    }
}

void
TkPathSurfaceToPhoto(
    Tcl_Interp * interp,
    TkPathContext ctx,
    Tk_PhotoHandle photo)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    if(context->fb == NULL) {
        return;
    }
Tk_PhotoImageBlock block;
    Tk_PhotoGetImage(photo, &block);
unsigned char *pixel = (unsigned char *)
        attemptckalloc(context->height * context->width * 4);
    if(pixel == NULL) {
        return;
    }
    if(gSurfaceCopyPremultiplyAlpha) {
        TkPathCopyBitsPremultipliedAlphaBGRA(context->fb, pixel,
            context->width, context->height, context->width * 4);
    } else {
        TkPathCopyBitsBGRA(context->fb, pixel, context->width, context->height,
            context->width * 4);
    }
    block.pixelPtr = pixel;
    block.width = context->width;
    block.height = context->height;
    block.pitch = context->width * 4;
    block.pixelSize = 4;
    block.offset[0] = 0;
    block.offset[1] = 1;
    block.offset[2] = 2;
    block.offset[3] = 3;
    Tk_PhotoPutBlock(interp, photo, &block, 0, 0, context->width,
        context->height, TK_PHOTO_COMPOSITE_OVERLAY);
    ckfree((char *)pixel);
}

void
TkPathEndPath(
    TkPathContext ctx)
{
}

void
TkPathFree(
    TkPathContext ctx)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    XDestroyAgg2D(context->display, (void *)context->agg2d);
    if(context->fb != NULL) {
        ckfree((char *)context->fb);
    }
    ckfree((char *)context);
}

void
TkPathClipToPath(
    TkPathContext ctx,
    int fillRule)
{
}

void
TkPathReleaseClipToPath(
    TkPathContext ctx)
{
}

void
TkPathStroke(
    TkPathContext ctx,
    Tk_PathStyle * style)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
Tk_PathDash *dashes = style->dashPtr;
    context->agg2d->lineWidth(style->strokeWidth);
    if(style->capStyle == X11_CapRound) {
        context->agg2d->lineCap(Agg2D::CapRound);
    } else if(style->capStyle == X11_CapSquare) {
        context->agg2d->lineCap(Agg2D::CapSquare);
    } else {
        context->agg2d->lineCap(Agg2D::CapButt);
    }
    if(style->joinStyle == X11_JoinMiter) {
        context->agg2d->lineJoin(Agg2D::JoinMiter);
    } else if(style->joinStyle == X11_JoinRound) {
        context->agg2d->lineJoin(Agg2D::JoinRound);
    } else {
        context->agg2d->lineJoin(Agg2D::JoinBevel);
    }
    context->agg2d->miterLimit(style->miterLimit);
double opacity = style->strokeOpacity;
    if(opacity > 1.0) {
        opacity = 1.0;
    } else if(opacity < 0.0) {
        opacity = 0.0;
    }
    context->agg2d->lineColor(XC_R(style->strokeColor),
        XC_G(style->strokeColor),
        XC_B(style->strokeColor), (unsigned)(opacity * 0xFF));
    context->agg2d->noFill();
    if((dashes != NULL) && (dashes->number > 0)) {
        context->agg2d->setDash(dashes->array, dashes->number, style->offset);
    }
    context->agg2d->drawPath(Agg2D::StrokeOnly);
    if((dashes != NULL) && (dashes->number > 0)) {
        context->agg2d->setDash((float *)NULL, 0);
    }
}

void
TkPathFill(
    TkPathContext ctx,
    Tk_PathStyle * style)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
double opacity = style->fillOpacity;
    if(opacity > 1.0) {
        opacity = 1.0;
    } else if(opacity < 0.0) {
        opacity = 0.0;
    }
    context->agg2d->fillColor(XC_R(style->fill->color),
        XC_G(style->fill->color),
        XC_B(style->fill->color), (unsigned)(opacity * 0xFF));
    context->agg2d->noLine();
    context->agg2d->drawPath(Agg2D::FillOnly);
}

void
TkPathFillAndStroke(
    TkPathContext ctx,
    Tk_PathStyle * style)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
Tk_PathDash *dashes = style->dashPtr;
    context->agg2d->lineWidth(style->strokeWidth);
    if(style->capStyle == X11_CapRound) {
        context->agg2d->lineCap(Agg2D::CapRound);
    } else if(style->capStyle == X11_CapSquare) {
        context->agg2d->lineCap(Agg2D::CapSquare);
    } else {
        context->agg2d->lineCap(Agg2D::CapButt);
    }
    if(style->joinStyle == X11_JoinMiter) {
        context->agg2d->lineJoin(Agg2D::JoinMiter);
    } else if(style->joinStyle == X11_JoinRound) {
        context->agg2d->lineJoin(Agg2D::JoinRound);
    } else {
        context->agg2d->lineJoin(Agg2D::JoinBevel);
    }
    context->agg2d->miterLimit(style->miterLimit);
double opacity = style->strokeOpacity;
    if(opacity > 1.0) {
        opacity = 1.0;
    } else if(opacity < 0.0) {
        opacity = 0.0;
    }
    context->agg2d->lineColor(XC_R(style->strokeColor),
        XC_G(style->strokeColor),
        XC_B(style->strokeColor), (unsigned)(opacity * 0xFF));
    opacity = style->fillOpacity;
    if(opacity > 1.0) {
        opacity = 1.0;
    } else if(opacity < 0.0) {
        opacity = 0.0;
    }
    context->agg2d->fillColor(XC_R(style->fill->color),
        XC_G(style->fill->color),
        XC_B(style->fill->color), (unsigned)(opacity * 0xFF));
    if((dashes != NULL) && (dashes->number > 0)) {
        context->agg2d->setDash(dashes->array, dashes->number, style->offset);
    }
    context->agg2d->drawPath(Agg2D::FillAndStroke);
    if((dashes != NULL) && (dashes->number > 0)) {
        context->agg2d->setDash((float *)NULL, 0);
    }
}

int
TkPathGetCurrentPosition(
    TkPathContext ctx,
    PathPoint * ptPtr)
{
TkPathContext_ *context = (TkPathContext_ *) ctx;
    ptPtr->x = context->x;
    ptPtr->y = context->y;
    return TCL_OK;
}

int
TkPathDrawingDestroysPath(
    void)
{
    return 0;
}

int
TkPathPixelAlign(
    void)
{
    return 0;
}

void
TkPathPaintLinearGradient(
    TkPathContext ctx,
    PathRect * bbox,
    LinearGradientFill * fillPtr,
    int fillRule,
    double fillOpacity,
    TMatrix * mPtr)
{
    if(fillOpacity > 1.0) {
        fillOpacity = 1.0;
    } else if(fillOpacity < 0.0) {
        fillOpacity = 0.0;
    }
    /*
     * We need to do like this since this is how SVG defines gradient drawing
     * in case the transition vector is in relative coordinates.
     */
    PathRect *tPtr = fillPtr->transitionPtr;
    double x1, y1, x2, y2;
    if(fillPtr->units == kPathGradientUnitsBoundingBox) {
    double x = bbox->x1;
    double y = bbox->y1;
    double width = bbox->x2 - bbox->x1;
    double height = bbox->y2 - bbox->y1;
        x1 = x + tPtr->x1 * width;
        y1 = y + tPtr->y1 * height;
        x2 = x + tPtr->x2 * width;
        y2 = y + tPtr->y2 * height;
    } else {
        x1 = tPtr->x1;
        y1 = tPtr->y1;
        x2 = tPtr->x2;
        y2 = tPtr->y2;
    }
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if(fillPtr->stopArrPtr->nstops > 0) {
    int nstops = fillPtr->stopArrPtr->nstops;
    double *offsets = (double *)Tcl_Alloc(nstops * sizeof(double));
        Agg2D::Color * colors =
            (Agg2D::Color *) ckalloc(nstops * sizeof(Agg2D::Color));
        for(int i = 0; i < nstops; i++) {
    GradientStop *stop = fillPtr->stopArrPtr->stops[i];
            offsets[i] = stop->offset;
    double opacity = stop->opacity;
            if(opacity > 1.0) {
                opacity = 1.0;
            } else if(opacity < 0.0) {
                opacity = 0.0;
            }
            Agg2D::Color color(XC_R(stop->color),
                XC_G(stop->color),
                XC_B(stop->color), (unsigned)(opacity * fillOpacity * 0xFF));
            colors[i] = color;
        }
        Agg2D::GradientMode meth;
        switch (fillPtr->method) {
        default:
        case kPathGradientMethodPad:
            meth = Agg2D::GradientPad;
            break;
        case kPathGradientMethodRepeat:
            meth = Agg2D::GradientRepeat;
            break;
        case kPathGradientMethodReflect:
            meth = Agg2D::GradientReflect;
            break;
        }
        context->agg2d->fillLinearGradient(x1, y1, x2, y2,
            nstops, offsets, colors, meth);
        ckfree((char *)offsets);
        ckfree((char *)colors);
    }
    context->agg2d->fillEvenOdd(fillRule != WindingRule);
    context->agg2d->drawPath(Agg2D::FillOnly);
}

void
TkPathPaintRadialGradient(
    TkPathContext ctx,
    PathRect * bbox,
    RadialGradientFill * fillPtr,
    int fillRule,
    double fillOpacity,
    TMatrix * mPtr)
{
    if(fillOpacity > 1.0) {
        fillOpacity = 1.0;
    } else if(fillOpacity < 0.0) {
        fillOpacity = 0.0;
    }
    /*
     * We need to do like this since this is how SVG defines gradient drawing
     * in case the transition vector is in relative coordinates.
     */
    RadialTransition *tPtr = fillPtr->radialPtr;
    double centerX, centerY, radiusX, focalX, focalY, scaleX, scaleY;
#if 0
    double radiusY;
#endif
    double width = bbox->x2 - bbox->x1;
    double height = bbox->y2 - bbox->y1;
    if(fillPtr->units == kPathGradientUnitsBoundingBox) {
        centerX = width * tPtr->centerX;
        centerY = height * tPtr->centerY;
        radiusX = width * tPtr->radius;
#if 0
        radiusY = height * tPtr->radius;
#endif
        focalX = width * tPtr->focalX;
        focalY = height * tPtr->focalY;
    } else {
        centerX = tPtr->centerX;
        centerY = tPtr->centerY;
        radiusX = tPtr->radius;
#if 0
        radiusY = tPtr->radius;
#endif
        focalX = tPtr->focalX;
        focalY = tPtr->focalY;
    }
    if(width > height) {
        scaleX = 1.0;
        scaleY = height / width;
    } else {
        scaleX = width / height;
        scaleY = 1.0;
    }
    TkPathContext_ *context = (TkPathContext_ *) ctx;
    if(fillPtr->stopArrPtr->nstops > 0) {
    int nstops = fillPtr->stopArrPtr->nstops;
    double *offsets = (double *)Tcl_Alloc(nstops * sizeof(double));
        Agg2D::Color * colors =
            (Agg2D::Color *) ckalloc(nstops * sizeof(Agg2D::Color));
        for(int i = 0; i < nstops; i++) {
    GradientStop *stop = fillPtr->stopArrPtr->stops[i];
            offsets[i] = stop->offset;
    double opacity = stop->opacity;
            if(opacity > 1.0) {
                opacity = 1.0;
            } else if(opacity < 0.0) {
                opacity = 0.0;
            }
            Agg2D::Color color(XC_R(stop->color),
                XC_G(stop->color),
                XC_B(stop->color), (unsigned)(opacity * fillOpacity * 0xFF));
            colors[i] = color;
        }
        Agg2D::GradientMode meth;
        switch (fillPtr->method) {
        default:
        case kPathGradientMethodPad:
            meth = Agg2D::GradientPad;
            break;
        case kPathGradientMethodRepeat:
            meth = Agg2D::GradientRepeat;
            break;
        case kPathGradientMethodReflect:
            meth = Agg2D::GradientReflect;
            break;
        }
        context->agg2d->fillRadialGradient(centerX + bbox->x1,
            centerY + bbox->y1,
            focalX + bbox->x1,
            focalY + bbox->y1,
            radiusX, scaleX, scaleY, nstops, offsets, colors, meth);
        ckfree((char *)offsets);
        ckfree((char *)colors);
    }
    context->agg2d->fillEvenOdd(fillRule != WindingRule);
    context->agg2d->drawPath(Agg2D::FillOnly);
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
