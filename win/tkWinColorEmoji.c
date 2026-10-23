/*
 * tkWinColorEmoji.c --
 *
 *	Color glyph rendering for Windows fonts that carry a COLR table (color
 *	emoji).  GDI draws only the outlines of such glyphs; this file draws
 *	their color layers (COLR v0) with DirectWrite and Direct2D, reusing the
 *	glyphs, advances and offsets shaped by Uniscribe in tkWinFont.c, so the
 *	layout does not change.
 *
 *	Each subfont owns a cache: the glyphs it has drawn, rendered once into
 *	32-bit premultiplied atlas pages, and alpha-blended from there onto
 *	Tk's device context (the composition over whatever the DC holds is
 *	explicit, and the clip region of the DC applies).  The cache is keyed
 *	by glyph index, not by run: after shaping, an emoji sequence is one
 *	glyph, and a long run of emoji is as many cells.  Pages are bounded;
 *	the least recently used one is emptied when they are all full, and a
 *	glyph too large for a page is drawn through a transient bitmap.
 *
 *	d2d1.dll and dwrite.dll are loaded on demand, and never unloaded: the
 *	caches hold DirectWrite faces whose code lives there.  The factories
 *	live in thread-specific data, like the font families of tkWinFont.c.
 *	Anything missing or failing makes TkWinDrawColorGlyphs() report that
 *	nothing was drawn, and the caller falls back to ScriptTextOut().
 *
 *	The interfaces are declared in tkWinDirect2D.h, for C on every
 *	toolchain.
 *
 * Copyright © 2026 Nicolas Bats
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkWinInt.h"
#include "tkWinDirect2D.h"

/*
 * The two IIDs needed to create the factories, defined here so that no uuid
 * library is involved on any toolchain.
 */

static const GUID iidD2D1Factory = {0x06152247, 0x6f50, 0x465a,
	{0x92, 0x45, 0x11, 0x8b, 0xfd, 0x3b, 0x60, 0x07}};
static const GUID iidDWriteFactory2 = {0x0439fc60, 0xca44, 0x4994,
	{0x8d, 0xee, 0x3a, 0x9a, 0xf7, 0xb7, 0x32, 0xec}};

typedef HRESULT (WINAPI *D2D1CreateFactoryProc)(D2D1_FACTORY_TYPE factoryType,
	REFIID riid, const D2D1_FACTORY_OPTIONS *options, void **factory);
typedef HRESULT (WINAPI *DWriteCreateFactoryProc)(DWRITE_FACTORY_TYPE factoryType,
	REFIID riid, IUnknown **factory);

/*
 * Per-thread rendering context, created at the first color glyph and
 * released at thread exit.  Direct2D single-threaded factories are not
 * shared between threads, which is also what keeps this file free of global
 * state.
 */

typedef struct ThreadSpecificData {
    int state;			/* 0: not tried yet, 1: ready, -1: color
				 * rendering unavailable on this system. */
    int exitHandler;		/* Thread exit handler registered. */
    HMODULE d2dModule;		/* d2d1.dll, loaded once, kept. */
    HMODULE dwriteModule;	/* dwrite.dll, loaded once, kept. */
    ID2D1Factory *d2dFactory;
    IDWriteFactory2 *dwriteFactory;
				/* IDWriteFactory2: TranslateColorGlyphRun
				 * (Windows 8.1 and later). */
    IDWriteGdiInterop *gdiInterop;
				/* Font faces from GDI fonts. */
    ID2D1DCRenderTarget *target;
				/* Bound to the cell being rendered. */
    ID2D1SolidColorBrush *brush;
				/* One brush, recolored per layer. */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * The cache of a subfont: atlas pages and one entry per glyph index.
 */

#define PAGE_SIZE	512	/* Side of an atlas page, in pixels. */
#define MAX_PAGES	4	/* Pages per subfont: 4 MB at most. */
#define HOT_DRAWS	256	/* A page used by one of that many last draws
				 * is not emptied. */

typedef struct Page {
    HBITMAP dib;		/* PAGE_SIZE x PAGE_SIZE, 32-bit premultiplied. */
    void *bits;			/* Pixels of the DIB section. */
    int shelfX, shelfY;		/* Next free cell on the current shelf. */
    unsigned long lastUse;	/* Generation of its last use. */
    int inUse;			/* Holds a cell of the run being drawn:
				 * not evictable. */
} Page;

typedef struct Entry {
    int hasColor;		/* The glyph has color layers. */
    int usesTextColor;		/* A layer, or the whole glyph, takes the
				 * text color. */
    COLORREF textColor;		/* Text color the cell was rendered with. */
    int page;			/* Page of the cell, -1 if none. */
    int x, y, width;		/* Cell in the page; its height is the
				 * cellHeight of the cache. */
} Entry;

struct TkWinColorGlyphCache {
    IDWriteFontFace *face;	/* DirectWrite face of the subfont. */
    FLOAT emSize;		/* Em size in pixels. */
    int ascent, descent;	/* Text metrics of the font. */
    int margin;			/* Around a glyph in its cell: layers may
				 * overhang the advance a little. */
    int cellHeight;		/* ascent + descent + 2 * margin. */
    HDC memDC;			/* Pages are selected in it to render and to
				 * blit. */
    HBITMAP stockBitmap;	/* Restored before deleting memDC. */
    Page pages[MAX_PAGES];
    int pageCount;
    unsigned long generation;	/* One per run drawn. */
    Tcl_HashTable entries;	/* Glyph index -> Entry. */
};

/*
 * Every method is called through the interface that introduces it.
 */

#define UNKNOWN(obj)	((IUnknown *) (obj))
#define RELEASE(obj) \
    do { if (obj) { UNKNOWN(obj)->lpVtbl->Release(UNKNOWN(obj)); (obj) = NULL; } } while (0)
#define RENDER_TARGET(tsdPtr)	((ID2D1RenderTarget *) (tsdPtr)->target)

static void		ReleaseContext(void *clientData);
static int		CreateTarget(ThreadSpecificData *tsdPtr);
static ThreadSpecificData *GetContext(void);
static TkWinColorGlyphCache *CreateCache(ThreadSpecificData *tsdPtr, HDC hdc,
			    HFONT hFont);
static Entry *		GetEntry(ThreadSpecificData *tsdPtr,
			    TkWinColorGlyphCache *cache, WORD glyph, int advance);
static int		RenderGlyphAt(ThreadSpecificData *tsdPtr,
			    TkWinColorGlyphCache *cache, int cellX, int cellY,
			    int cellWidth, WORD glyph, int advance,
			    COLORREF textColor, int hasColor);
static int		CreatePage(TkWinColorGlyphCache *cache);
static void		EvictPage(TkWinColorGlyphCache *cache, int p);
static int		AllocCell(TkWinColorGlyphCache *cache, int width, Entry *entry);
static int		RenderPending(ThreadSpecificData *tsdPtr,
			    TkWinColorGlyphCache *cache, Entry **entries,
			    const int *pending, int pendingCount, const WORD *glyphs,
			    const int *advances, COLORREF textColor);
static int		DrawDirect(ThreadSpecificData *tsdPtr,
			    TkWinColorGlyphCache *cache, HDC hdc, int x, int y,
			    WORD glyph, int advance, COLORREF textColor,
			    int hasColor);
static void		SetColorRef(ThreadSpecificData *tsdPtr, COLORREF ref);

/*
 *----------------------------------------------------------------------
 *
 * ReleaseContext --
 *
 *	Release the Direct2D/DirectWrite objects of the thread.  Thread exit
 *	handler; also used to start over after a device loss.  The DLLs stay
 *	loaded: the faces held by the caches, released with their fonts, are
 *	implemented there.
 *
 *----------------------------------------------------------------------
 */

static void
ReleaseContext(
    void *clientData)		/* The ThreadSpecificData of the thread. */
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)clientData;

    RELEASE(tsdPtr->brush);
    RELEASE(tsdPtr->target);
    RELEASE(tsdPtr->gdiInterop);
    RELEASE(tsdPtr->dwriteFactory);
    RELEASE(tsdPtr->d2dFactory);
    tsdPtr->state = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateTarget --
 *
 *	Create the software DC render target and its brush.  Premultiplied alpha: the
 *	target writes the alpha of the layers into the DIB it is bound to,
 *	which AlphaBlend then composes onto Tk's DC.
 *
 * Results:
 *	1 on success, 0 otherwise.
 *
 *----------------------------------------------------------------------
 */

static int
CreateTarget(
    ThreadSpecificData *tsdPtr)
{
    D2D1_RENDER_TARGET_PROPERTIES props;
    D2D1_COLOR_F black = {0.0f, 0.0f, 0.0f, 1.0f};

    /*
     * Software rendering: the cells are small, and a hardware target reads
     * the pixels back from the GPU at every EndDraw, which made a cell cost
     * milliseconds; no device to lose either.
     */

    memset(&props, 0, sizeof(props));
    props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;
    props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
    if (FAILED(tsdPtr->d2dFactory->lpVtbl->CreateDCRenderTarget(
	    tsdPtr->d2dFactory, &props, &tsdPtr->target))) {
	tsdPtr->target = NULL;
	return 0;
    }
    if (FAILED(RENDER_TARGET(tsdPtr)->lpVtbl->CreateSolidColorBrush(
	    RENDER_TARGET(tsdPtr), &black, NULL, &tsdPtr->brush))) {
	tsdPtr->brush = NULL;
	RELEASE(tsdPtr->target);
	return 0;
    }

    /* No ClearType on a transparent surface. */
    RENDER_TARGET(tsdPtr)->lpVtbl->SetTextAntialiasMode(RENDER_TARGET(tsdPtr),
	    D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * GetContext --
 *
 *	Return the rendering context of the thread, created on first use.
 *
 * Results:
 *	The context, or NULL when color rendering is unavailable (DLL or
 *	interface missing, as on Windows 8 and before, or any COM failure).
 *	The failure is remembered: later calls return NULL at once.
 *
 *----------------------------------------------------------------------
 */

static ThreadSpecificData *
GetContext(void)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    D2D1CreateFactoryProc createD2D;
    DWriteCreateFactoryProc createDWrite;

    if (tsdPtr->state != 0) {
	return (tsdPtr->state > 0) ? tsdPtr : NULL;
    }
    tsdPtr->state = -1;
    if (!tsdPtr->exitHandler) {
	Tcl_CreateThreadExitHandler(ReleaseContext, tsdPtr);
	tsdPtr->exitHandler = 1;
    }

    if (tsdPtr->dwriteModule == NULL) {
	tsdPtr->dwriteModule = LoadLibraryW(L"dwrite.dll");
    }
    if (tsdPtr->d2dModule == NULL) {
	tsdPtr->d2dModule = LoadLibraryW(L"d2d1.dll");
    }
    if (tsdPtr->dwriteModule == NULL || tsdPtr->d2dModule == NULL) {
	return NULL;
    }
    createDWrite = (DWriteCreateFactoryProc) (void *)
	    GetProcAddress(tsdPtr->dwriteModule, "DWriteCreateFactory");
    createD2D = (D2D1CreateFactoryProc) (void *)
	    GetProcAddress(tsdPtr->d2dModule, "D2D1CreateFactory");
    if (createDWrite == NULL || createD2D == NULL
	    || FAILED(createDWrite(DWRITE_FACTORY_TYPE_SHARED, &iidDWriteFactory2,
		    (IUnknown **) &tsdPtr->dwriteFactory))
	    || FAILED(((IDWriteFactory *) tsdPtr->dwriteFactory)->lpVtbl->GetGdiInterop(
		    (IDWriteFactory *) tsdPtr->dwriteFactory, &tsdPtr->gdiInterop))
	    || FAILED(createD2D(D2D1_FACTORY_TYPE_SINGLE_THREADED, &iidD2D1Factory,
		    NULL, (void **) &tsdPtr->d2dFactory))
	    || !CreateTarget(tsdPtr)) {
	ReleaseContext(tsdPtr);
	tsdPtr->state = -1;
	return NULL;
    }
    tsdPtr->state = 1;
    return tsdPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * SetColorRef --
 *
 *	Set the brush to a GDI color.
 *
 *----------------------------------------------------------------------
 */

static void
SetColorRef(
    ThreadSpecificData *tsdPtr,
    COLORREF ref)
{
    D2D1_COLOR_F color;

    color.r = GetRValue(ref) / 255.0f;
    color.g = GetGValue(ref) / 255.0f;
    color.b = GetBValue(ref) / 255.0f;
    color.a = 1.0f;
    tsdPtr->brush->lpVtbl->SetColor(tsdPtr->brush, &color);
}

/*
 *----------------------------------------------------------------------
 *
 * CreateCache --
 *
 *	Create the cache of a subfont from the font selected in hdc: its
 *	DirectWrite face, metrics and memory DC.  No page yet.
 *
 * Results:
 *	The cache, or NULL on failure.
 *
 *----------------------------------------------------------------------
 */

static TkWinColorGlyphCache *
CreateCache(
    ThreadSpecificData *tsdPtr,
    HDC hdc,			/* Font selected. */
    HFONT hFont)		/* The same font. */
{
    TkWinColorGlyphCache *cache;
    LOGFONTW lf;
    TEXTMETRICW tm;
    IDWriteFontFace *face;

    if (GetObjectW(hFont, sizeof(lf), &lf) != sizeof(lf)
	    || !GetTextMetricsW(hdc, &tm)
	    || FAILED(tsdPtr->gdiInterop->lpVtbl->CreateFontFaceFromHdc(
		    tsdPtr->gdiInterop, hdc, &face))) {
	return NULL;
    }
    cache = (TkWinColorGlyphCache *)Tcl_Alloc(sizeof(TkWinColorGlyphCache));
    memset(cache, 0, sizeof(TkWinColorGlyphCache));
    cache->face = face;

    /*
     * Tk creates its fonts with a negative height, which is the em size in
     * pixels; a positive height is a cell height, em size = height -
     * internal leading.
     */

    cache->emSize = (lf.lfHeight < 0) ? (FLOAT) -lf.lfHeight
	    : (FLOAT) (tm.tmHeight - tm.tmInternalLeading);
    cache->ascent = tm.tmAscent;
    cache->descent = tm.tmDescent;
    cache->margin = (int) (cache->emSize / 4.0f);
    if (cache->margin < 2) {
	cache->margin = 2;
    }
    cache->cellHeight = cache->ascent + cache->descent + 2 * cache->margin;
    cache->memDC = CreateCompatibleDC(hdc);
    if (cache->memDC == NULL) {
	RELEASE(cache->face);
	Tcl_Free(cache);
	return NULL;
    }
    cache->stockBitmap = (HBITMAP) GetCurrentObject(cache->memDC, OBJ_BITMAP);
    Tcl_InitHashTable(&cache->entries, TCL_ONE_WORD_KEYS);
    return cache;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinFreeColorGlyphCache --
 *
 *	Free the cache of a subfont, if any: pages, entries, face, DC.
 *	Called by ReleaseSubFont().
 *
 *----------------------------------------------------------------------
 */

void
TkWinFreeColorGlyphCache(
    TkWinColorGlyphCache **cachePtr)
{
    TkWinColorGlyphCache *cache = *cachePtr;
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    int p;

    if (cache == NULL) {
	return;
    }
    *cachePtr = NULL;
    for (hPtr = Tcl_FirstHashEntry(&cache->entries, &search); hPtr != NULL;
	    hPtr = Tcl_NextHashEntry(&search)) {
	Tcl_Free(Tcl_GetHashValue(hPtr));
    }
    Tcl_DeleteHashTable(&cache->entries);
    SelectObject(cache->memDC, cache->stockBitmap);
    for (p = 0; p < cache->pageCount; p++) {
	DeleteObject(cache->pages[p].dib);
    }
    DeleteDC(cache->memDC);
    RELEASE(cache->face);
    Tcl_Free(cache);
}

/*
 *----------------------------------------------------------------------
 *
 * GetEntry --
 *
 *	Return the entry of a glyph, classifying it on first sight: does it
 *	have color layers, and does one of them take the text color?
 *
 * Results:
 *	The entry, or NULL on a COM failure.
 *
 *----------------------------------------------------------------------
 */

static Entry *
GetEntry(
    ThreadSpecificData *tsdPtr,
    TkWinColorGlyphCache *cache,
    WORD glyph,
    int advance)
{
    Tcl_HashEntry *hPtr;
    Entry *entry;
    int isNew;
    DWRITE_GLYPH_RUN run;
    FLOAT glyphAdvance = (FLOAT) advance;
    DWRITE_GLYPH_OFFSET glyphOffset = {0.0f, 0.0f};
    IDWriteColorGlyphRunEnumerator *layers;
    HRESULT hr;

    hPtr = Tcl_CreateHashEntry(&cache->entries, INT2PTR(glyph), &isNew);
    if (!isNew) {
	return (Entry *)Tcl_GetHashValue(hPtr);
    }
    entry = (Entry *)Tcl_Alloc(sizeof(Entry));
    memset(entry, 0, sizeof(Entry));
    entry->page = -1;
    Tcl_SetHashValue(hPtr, entry);

    memset(&run, 0, sizeof(run));
    run.fontFace = cache->face;
    run.fontEmSize = cache->emSize;
    run.glyphCount = 1;
    run.glyphIndices = &glyph;
    run.glyphAdvances = &glyphAdvance;
    run.glyphOffsets = &glyphOffset;
    hr = tsdPtr->dwriteFactory->lpVtbl->TranslateColorGlyphRun(
	    tsdPtr->dwriteFactory, 0.0f, 0.0f, &run, NULL,
	    DWRITE_MEASURING_MODE_GDI_CLASSIC, NULL, 0, &layers);
    if (hr == DWRITE_E_NOCOLOR) {
	entry->usesTextColor = 1;	/* Drawn in the text color, if ever. */
	return entry;
    }
    if (FAILED(hr)) {
	Tcl_DeleteHashEntry(hPtr);
	Tcl_Free(entry);
	return NULL;
    }
    entry->hasColor = 1;
    for (;;) {
	const DWRITE_COLOR_GLYPH_RUN *layer;
	BOOL hasRun;

	if (FAILED(layers->lpVtbl->MoveNext(layers, &hasRun))) {
	    hasRun = FALSE;
	    hr = E_FAIL;
	}
	if (!hasRun) {
	    break;
	}
	if (FAILED(layers->lpVtbl->GetCurrentRun(layers, &layer))) {
	    hr = E_FAIL;
	    break;
	}
	if (layer->paletteIndex == 0xFFFF) {
	    entry->usesTextColor = 1;
	}
    }
    RELEASE(layers);
    if (FAILED(hr)) {
	Tcl_DeleteHashEntry(hPtr);
	Tcl_Free(entry);
	return NULL;
    }
    return entry;
}

/*
 *----------------------------------------------------------------------
 *
 * RenderGlyphAt --
 *
 *	Render one glyph into the render target, between BeginDraw and
 *	EndDraw: its color layers, or the glyph itself in the text color when
 *	it has none.  The cell is cleared first and clips the drawing.
 *
 * Results:
 *	1 on success, 0 if the layers could not be obtained; the caller still
 *	has to call EndDraw.
 *
 *----------------------------------------------------------------------
 */

static int
RenderGlyphAt(
    ThreadSpecificData *tsdPtr,
    TkWinColorGlyphCache *cache,
    int cellX, int cellY,	/* Top-left of the cell in the target. */
    int cellWidth,
    WORD glyph,
    int advance,
    COLORREF textColor,
    int hasColor)
{
    DWRITE_GLYPH_RUN run;
    FLOAT glyphAdvance = (FLOAT) advance;
    DWRITE_GLYPH_OFFSET glyphOffset = {0.0f, 0.0f};
    IDWriteColorGlyphRunEnumerator *layers = NULL;
    D2D1_COLOR_F color;
    D2D1_POINT_2F origin;
    D2D1_RECT_F clip;
    int ok = 1;

    memset(&run, 0, sizeof(run));
    run.fontFace = cache->face;
    run.fontEmSize = cache->emSize;
    run.glyphCount = 1;
    run.glyphIndices = &glyph;
    run.glyphAdvances = &glyphAdvance;
    run.glyphOffsets = &glyphOffset;
    if (hasColor && FAILED(tsdPtr->dwriteFactory->lpVtbl->TranslateColorGlyphRun(
	    tsdPtr->dwriteFactory, 0.0f, 0.0f, &run, NULL,
	    DWRITE_MEASURING_MODE_GDI_CLASSIC, NULL, 0, &layers))) {
	return 0;
    }

    clip.left = (FLOAT) cellX;
    clip.top = (FLOAT) cellY;
    clip.right = (FLOAT) (cellX + cellWidth);
    clip.bottom = (FLOAT) (cellY + cache->cellHeight);
    RENDER_TARGET(tsdPtr)->lpVtbl->PushAxisAlignedClip(RENDER_TARGET(tsdPtr),
	    &clip, D2D1_ANTIALIAS_MODE_ALIASED);
    color.r = color.g = color.b = color.a = 0.0f;
    RENDER_TARGET(tsdPtr)->lpVtbl->Clear(RENDER_TARGET(tsdPtr), &color);
    origin.x = (FLOAT) (cellX + cache->margin);
    origin.y = (FLOAT) (cellY + cache->margin + cache->ascent);
    if (!hasColor) {
	SetColorRef(tsdPtr, textColor);
	RENDER_TARGET(tsdPtr)->lpVtbl->DrawGlyphRun(RENDER_TARGET(tsdPtr), origin,
		&run, (ID2D1Brush *) tsdPtr->brush, DWRITE_MEASURING_MODE_GDI_CLASSIC);
    } else {
	for (;;) {
	    const DWRITE_COLOR_GLYPH_RUN *layer;
	    D2D1_POINT_2F layerOrigin;
	    BOOL hasRun;

	    if (FAILED(layers->lpVtbl->MoveNext(layers, &hasRun))) {
		ok = 0;
		break;
	    }
	    if (!hasRun) {
		break;
	    }
	    if (FAILED(layers->lpVtbl->GetCurrentRun(layers, &layer))) {
		ok = 0;
		break;
	    }
	    if (layer->paletteIndex == 0xFFFF) {
		SetColorRef(tsdPtr, textColor);
	    } else {
		color.r = layer->runColor.r;
		color.g = layer->runColor.g;
		color.b = layer->runColor.b;
		color.a = layer->runColor.a;
		tsdPtr->brush->lpVtbl->SetColor(tsdPtr->brush, &color);
	    }
	    layerOrigin.x = origin.x + layer->baselineOriginX;
	    layerOrigin.y = origin.y + layer->baselineOriginY;
	    RENDER_TARGET(tsdPtr)->lpVtbl->DrawGlyphRun(RENDER_TARGET(tsdPtr),
		    layerOrigin, &layer->glyphRun, (ID2D1Brush *) tsdPtr->brush,
		    DWRITE_MEASURING_MODE_GDI_CLASSIC);
	}
	RELEASE(layers);
    }
    RENDER_TARGET(tsdPtr)->lpVtbl->PopAxisAlignedClip(RENDER_TARGET(tsdPtr));
    return ok;
}

/*
 *----------------------------------------------------------------------
 *
 * CreatePage --
 *
 *	Add an atlas page to the cache.
 *
 * Results:
 *	1 on success, 0 if the DIB could not be created.
 *
 *----------------------------------------------------------------------
 */

static int
CreatePage(
    TkWinColorGlyphCache *cache)
{
    Page *page = &cache->pages[cache->pageCount];
    BITMAPINFO bmi;

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = PAGE_SIZE;
    bmi.bmiHeader.biHeight = -PAGE_SIZE;	/* Top-down. */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    memset(page, 0, sizeof(Page));
    page->dib = CreateDIBSection(cache->memDC, &bmi, DIB_RGB_COLORS,
	    &page->bits, NULL, 0);
    if (page->dib == NULL) {
	return 0;
    }
    memset(page->bits, 0, PAGE_SIZE * PAGE_SIZE * 4);
    cache->pageCount++;
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * EvictPage --
 *
 *	Empty a page: its cells are forgotten, their glyphs will be rendered
 *	again on demand.
 *
 *----------------------------------------------------------------------
 */

static void
EvictPage(
    TkWinColorGlyphCache *cache,
    int p)
{
    Page *page = &cache->pages[p];
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;

    for (hPtr = Tcl_FirstHashEntry(&cache->entries, &search); hPtr != NULL;
	    hPtr = Tcl_NextHashEntry(&search)) {
	Entry *entry = (Entry *)Tcl_GetHashValue(hPtr);

	if (entry->page == p) {
	    entry->page = -1;
	}
    }
    GdiFlush();
    memset(page->bits, 0, PAGE_SIZE * PAGE_SIZE * 4);
    page->shelfX = 0;
    page->shelfY = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * AllocCell --
 *
 *	Find room for a cell of the given width in the pages: on the current
 *	shelf of a page, on a new shelf, in a new page, or in the least
 *	recently used page once emptied.  Cells of a cache all have the same
 *	height, so shelves line up.  A page used by one of the last draws is
 *	kept: when the glyphs on screen outnumber the cells, emptying it would
 *	only make room for glyphs that evict it right back, so the extra
 *	glyphs are drawn without the cache instead.
 *
 * Results:
 *	1 and the cell stored in the entry, 0 if no page can take it.
 *
 *----------------------------------------------------------------------
 */

static int
AllocCell(
    TkWinColorGlyphCache *cache,
    int width,
    Entry *entry)
{
    int p, lru = -1;

    if (width > PAGE_SIZE || cache->cellHeight > PAGE_SIZE) {
	return 0;
    }
    for (;;) {
	for (p = 0; p < cache->pageCount; p++) {
	    Page *page = &cache->pages[p];

	    if (page->shelfX + width > PAGE_SIZE) {
		if (page->shelfY + 2 * cache->cellHeight > PAGE_SIZE) {
		    continue;		/* Page full. */
		}
		page->shelfX = 0;
		page->shelfY += cache->cellHeight;
	    }
	    if (page->shelfY + cache->cellHeight > PAGE_SIZE) {
		continue;
	    }
	    entry->page = p;
	    entry->x = page->shelfX;
	    entry->y = page->shelfY;
	    entry->width = width;
	    page->shelfX += width;
	    page->lastUse = cache->generation;
	    return 1;
	}
	if (cache->pageCount < MAX_PAGES) {
	    if (!CreatePage(cache)) {
		return 0;
	    }
	    continue;
	}
	for (p = 0; p < cache->pageCount; p++) {
	    if (!cache->pages[p].inUse
		    && cache->pages[p].lastUse + HOT_DRAWS < cache->generation
		    && (lru < 0
			|| cache->pages[p].lastUse < cache->pages[lru].lastUse)) {
		lru = p;
	    }
	}
	if (lru < 0) {
	    return 0;
	}
	EvictPage(cache, lru);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RenderPending --
 *
 *	Render the cells of a run that are missing or stale, one BeginDraw
 *	and EndDraw per page: a software render target makes EndDraw a copy
 *	of the page into its DIB, paid once for all the cells of the run.
 *
 * Results:
 *	1 on success, 0 on failure.  A cell not rendered is given up, so that
 *	no entry points to an empty cell; a lost device releases the context
 *	and gives up every pending cell.
 *
 *----------------------------------------------------------------------
 */

static int
RenderPending(
    ThreadSpecificData *tsdPtr,
    TkWinColorGlyphCache *cache,
    Entry **entries,		/* Entries of the run. */
    const int *pending,		/* Indices in entries of the cells to render. */
    int pendingCount,
    const WORD *glyphs,
    const int *advances,
    COLORREF textColor)
{
    RECT pageRect = {0, 0, PAGE_SIZE, PAGE_SIZE};
    int p, k, ok = 1;

    for (p = 0; p < cache->pageCount; p++) {
	int onPage = 0;

	for (k = 0; k < pendingCount; k++) {
	    if (entries[pending[k]]->page == p) {
		onPage = 1;
		break;
	    }
	}
	if (!onPage) {
	    continue;
	}
	SelectObject(cache->memDC, cache->pages[p].dib);
	if (FAILED(tsdPtr->target->lpVtbl->BindDC(tsdPtr->target, cache->memDC,
		&pageRect))) {
	    for (k = 0; k < pendingCount; k++) {
		if (entries[pending[k]]->page == p) {
		    entries[pending[k]]->page = -1;
		}
	    }
	    ok = 0;
	    continue;
	}
	RENDER_TARGET(tsdPtr)->lpVtbl->BeginDraw(RENDER_TARGET(tsdPtr));
	for (k = 0; k < pendingCount; k++) {
	    Entry *entry = entries[pending[k]];

	    if (entry->page != p) {
		continue;
	    }
	    if (RenderGlyphAt(tsdPtr, cache, entry->x, entry->y, entry->width,
		    glyphs[pending[k]], advances[pending[k]], textColor,
		    entry->hasColor)) {
		entry->textColor = textColor;
	    } else {
		entry->page = -1;
		ok = 0;
	    }
	}
	if (FAILED(RENDER_TARGET(tsdPtr)->lpVtbl->EndDraw(RENDER_TARGET(tsdPtr),
		NULL, NULL))) {
	    for (k = 0; k < pendingCount; k++) {
		entries[pending[k]]->page = -1;
	    }
	    ReleaseContext(tsdPtr);
	    return 0;
	}
    }
    return ok;
}

/*
 *----------------------------------------------------------------------
 *
 * DrawDirect --
 *
 *	Draw a glyph that no page can hold, through a transient bitmap.
 *
 * Results:
 *	1 if drawn, 0 otherwise.
 *
 *----------------------------------------------------------------------
 */

static int
DrawDirect(
    ThreadSpecificData *tsdPtr,
    TkWinColorGlyphCache *cache,
    HDC hdc,
    int x, int y,		/* Top-left of the cell on hdc. */
    WORD glyph,
    int advance,
    COLORREF textColor,
    int hasColor)
{
    BITMAPINFO bmi;
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC memDC;
    HBITMAP dib, oldBitmap;
    void *bits;
    RECT cell;
    int width = advance + 2 * cache->margin, drawn = 0;

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -cache->cellHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    memDC = CreateCompatibleDC(hdc);
    if (memDC == NULL) {
	return 0;
    }
    dib = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (dib == NULL) {
	DeleteDC(memDC);
	return 0;
    }
    oldBitmap = (HBITMAP) SelectObject(memDC, dib);
    cell.left = 0;
    cell.top = 0;
    cell.right = width;
    cell.bottom = cache->cellHeight;
    if (SUCCEEDED(tsdPtr->target->lpVtbl->BindDC(tsdPtr->target, memDC, &cell))) {
	RENDER_TARGET(tsdPtr)->lpVtbl->BeginDraw(RENDER_TARGET(tsdPtr));
	drawn = RenderGlyphAt(tsdPtr, cache, 0, 0, width, glyph, advance,
		textColor, hasColor);
	if (FAILED(RENDER_TARGET(tsdPtr)->lpVtbl->EndDraw(RENDER_TARGET(tsdPtr),
		NULL, NULL))) {
	    ReleaseContext(tsdPtr);
	    drawn = 0;
	}
	if (drawn) {
	    drawn = AlphaBlend(hdc, x, y, width, cache->cellHeight, memDC, 0, 0,
		    width, cache->cellHeight, blend) ? 1 : 0;
	}
    }
    SelectObject(memDC, oldBitmap);
    DeleteObject(dib);
    DeleteDC(memDC);
    return drawn;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinDrawColorGlyphs --
 *
 *	Draw a run of glyphs of the font selected in hdc, with its baseline
 *	origin at (x, y), from the glyph indices, advances and offsets that
 *	Uniscribe produced for it: the color layers of the glyphs that have
 *	some, the text color for the others.
 *
 * Results:
 *	1 if the run was drawn, 0 if the caller must draw it with GDI: no
 *	glyph with color layers in the run, DirectWrite or Direct2D
 *	unavailable, a raster operation other than copy, or any failure.
 *
 * Side effects:
 *	Draws on hdc.  Creates the cache of the subfont and fills it.
 *
 *----------------------------------------------------------------------
 */

int
TkWinDrawColorGlyphs(
    HDC hdc,			/* Target device context, font selected. */
    HFONT hFont,		/* Font of the run. */
    TkWinColorGlyphCache **cachePtr,
				/* Cache of the subfont, created here. */
    int x, int y,		/* Baseline origin of the run. */
    const SCRIPT_ANALYSIS *saPtr,
				/* Direction of the run.  The glyphs come in
				 * visual order, as ScriptTextOut draws them;
				 * only the offsets follow the direction. */
    const WORD *glyphs,		/* Glyph indices. */
    const int *advances,	/* Advance widths in pixels. */
    const GOFFSET *offsets,	/* Glyph offsets. */
    int glyphCount)		/* Number of glyphs. */
{
    ThreadSpecificData *tsdPtr;
    TkWinColorGlyphCache *cache;
    Entry **entries;
    int *pending;
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    COLORREF textColor;
    int i, p, anyColor = 0, pendingCount = 0, drawn = 1, pen = x;

    if (glyphCount <= 0 || GetROP2(hdc) != R2_COPYPEN) {
	return 0;
    }
    tsdPtr = GetContext();
    if (tsdPtr == NULL) {
	return 0;
    }
    if (*cachePtr == NULL) {
	*cachePtr = CreateCache(tsdPtr, hdc, hFont);
	if (*cachePtr == NULL) {
	    return 0;
	}
    }
    cache = *cachePtr;
    cache->generation++;
    textColor = GetTextColor(hdc);

    /*
     * Classify the glyphs; a run without any color glyph is left to GDI,
     * which keeps ClearType for text set in a color font.
     */

    entries = (Entry **)Tcl_Alloc(sizeof(Entry *) * glyphCount);
    for (i = 0; i < glyphCount; i++) {
	entries[i] = GetEntry(tsdPtr, cache, glyphs[i], advances[i]);
	if (entries[i] == NULL) {
	    Tcl_Free(entries);
	    return 0;
	}
	if (entries[i]->hasColor) {
	    anyColor = 1;
	}
    }
    if (!anyColor) {
	Tcl_Free(entries);
	return 0;
    }

    /*
     * Cells to render: missing, or stale because the glyph takes the text
     * color and the color changed.  The pages holding a cell of this run
     * must not be emptied to make room for another of its glyphs.
     */

    for (p = 0; p < cache->pageCount; p++) {
	cache->pages[p].inUse = 0;
    }
    for (i = 0; i < glyphCount; i++) {
	if (entries[i]->page >= 0) {
	    cache->pages[entries[i]->page].inUse = 1;
	}
    }
    pending = (int *)Tcl_Alloc(sizeof(int) * glyphCount);
    for (i = 0; i < glyphCount; i++) {
	Entry *entry = entries[i];
	int k, seen = 0;

	for (k = 0; k < pendingCount; k++) {
	    if (entries[pending[k]] == entry) {
		seen = 1;
		break;
	    }
	}
	if (seen) {
	    continue;
	}
	if (entry->page >= 0 && advances[i] + 2 * cache->margin > entry->width) {
	    entry->page = -1;		/* Wider than its cell: a new one. */
	}
	if (entry->page < 0) {
	    if (!AllocCell(cache, advances[i] + 2 * cache->margin, entry)) {
		continue;		/* Drawn directly below. */
	    }
	    cache->pages[entry->page].inUse = 1;
	} else if (!entry->usesTextColor || entry->textColor == textColor) {
	    continue;
	}
	pending[pendingCount++] = i;
    }
    if (pendingCount > 0 && !RenderPending(tsdPtr, cache, entries, pending,
	    pendingCount, glyphs, advances, textColor)) {
	drawn = 0;
    }

    /* Blit: the advances and offsets of Uniscribe place the cells. */
    for (i = 0; i < glyphCount && drawn; i++) {
	Entry *entry = entries[i];
	int du = saPtr->fRTL ? -offsets[i].du : offsets[i].du;
	int left = pen + du - cache->margin;
	int top = y - offsets[i].dv - cache->margin - cache->ascent;

	if (entry->page >= 0) {
	    Page *page = &cache->pages[entry->page];

	    SelectObject(cache->memDC, page->dib);
	    page->lastUse = cache->generation;
	    if (!AlphaBlend(hdc, left, top, entry->width, cache->cellHeight,
		    cache->memDC, entry->x, entry->y, entry->width,
		    cache->cellHeight, blend)) {
		drawn = 0;
	    }
	} else if (!DrawDirect(tsdPtr, cache, hdc, left, top, glyphs[i],
		advances[i], textColor, entry->hasColor)) {
	    drawn = 0;
	}
	pen += advances[i];
    }
    for (p = 0; p < cache->pageCount; p++) {
	cache->pages[p].inUse = 0;
    }
    Tcl_Free(pending);
    Tcl_Free(entries);
    return drawn;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
