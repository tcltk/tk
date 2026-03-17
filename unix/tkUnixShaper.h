/*
 * tkUnixShaper.h --
 *
 *	Public interface for text shaping + bidi. Provides pre-shaped glyph
 *	buffers.
 *
 * Copyright (c) 2026 Kevin Walzer
 */

#pragma once

#include "tkUnixInt.h"
#include "tkFont.h"
#include <kb_text_shaper.h> 

#define MAX_CACHED_COLORS 16
#define TK_DRAW_IN_CONTEXT
#define NUM_SPEC 1024


/* Text shaping data types. */
typedef struct X11ShapedGlyph {
    void      *kbFont;     /* opaque — only for rotated face lookup */
    XftFont   *xftFont;    /* resolved at shape time */
    FT_UInt    glyphId;
    int        x, y;
    int        advanceX;
} X11ShapedGlyph;

typedef struct {
    kbts_shape_context *context;
    struct {
        kbts_font *kbFont;
        int        faceIndex;
    } fontMap[8];
    int numFonts;

    /* LRU cache (16 slots) */
    struct {
        char          *text;
        int            textLen;
        X11ShapedGlyph *glyphs;
        int            glyphCount;
        int            glyphCapacity;
        int            totalWidth;
        int            lruSeq;
    } cache[16];
    int lruCounter;
} X11Shape;

typedef struct TkShapedTextBuffer {
    int               glyphCount;
    const X11ShapedGlyph *glyphs;      /* owned by cache — valid until next Tk_ShapeText call */
    int               totalWidth;
    int               textLenBytes;
} TkShapedTextBuffer;


/*
 * One UnixFtFace per fontconfig face (main + fallbacks).
 */
typedef struct {
    XftFont *ftFont;       /* rotated variant (if angle != 0) */
    XftFont *ft0Font;      /* upright (angle == 0) */
    FcPattern *source;
    FcCharSet *charset;
    double angle;
} UnixFtFace;

/*
 * Cached XftColor entries (avoids repeated XQueryColor round-trips).
 */
typedef struct {
    XftColor color;
    int next;
} UnixFtColorList;

/*
 * The per-font structure.
 */
typedef struct {
    TkFont font;		/* Stuff used by generic font package. Must be first. */
    UnixFtFace *faces;
    int nfaces;
    FcFontSet *fontset;
    FcPattern *pattern;

    Display *display;
    int screen;
    Colormap colormap;
    Visual *visual;
    XftDraw *ftDraw;
    int ncolors;
    int firstColor;
    UnixFtColorList colors[MAX_CACHED_COLORS];
    X11Shape shape;
} UnixFtFont;


/* Returns cached or freshly shaped buffer. Never NULL. */
TkShapedTextBuffer *Tk_ShapeText(Tk_Font tkfont, const char *utf8, int numBytes);

/* Cleanup hook for TkpDeleteFont / FinishedWithFont */
void Tk_ShaperDestroy(Tk_Font tkfont);

void TclpX11ShapeInit(X11Shape *s);


/*
 * Local Variables:
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */

