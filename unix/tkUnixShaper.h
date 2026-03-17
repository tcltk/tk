/*
 * tkUnixShaper.h --
 *
 *	Public interface for text shaping + bidi. Provides pre-shaped glyph
 *	buffers.
 *
 * Copyright (c) 2026 Kevin Walzer
 */

#include "tkUnixInt.h"
#include "tkFont.h"
#include <X11/Xft/Xft.h>

/* Public structures. */

typedef struct X11ShapedGlyph {
    void      *kbFont;     /* opaque — only for rotated face lookup */
    XftFont   *xftFont;    /* resolved at shape time */
    FT_UInt    glyphId;
    int        x, y;
    int        advanceX;
} X11ShapedGlyph;

typedef struct TkShapedTextBuffer {
    int               glyphCount;
    const X11ShapedGlyph *glyphs;      /* owned by cache — valid until next Tk_ShapeText call */
    int               totalWidth;
    int               textLenBytes;
} TkShapedTextBuffer;

/* Returns cached or freshly shaped buffer. Never NULL. */
extern TkShapedTextBuffer *
Tk_ShapeText(Tk_Font tkfont, const char *utf8, int numBytes);

/* Cleanup hook for TkpDeleteFont / FinishedWithFont */
extern void
Tk_ShaperDestroy(Tk_Font tkfont);


/* Internal structures. */

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

/*
 * Local Variables:
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */

