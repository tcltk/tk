/*
 * tkWinColorEmoji.c --
 *
 *	Color glyph rendering for Windows fonts that carry a COLR table (color
 *	emoji).  GDI draws only the outlines of such glyphs; this file draws
 *	their color layers (COLR v0) with DirectWrite and Direct2D, reusing the
 *	glyphs, advances and offsets shaped by Uniscribe in tkWinFont.c, so the
 *	layout does not change.
 *
 *	The layers are rendered into a 32-bit premultiplied DIB which is then
 *	alpha-blended onto Tk's device context: the composition over whatever
 *	the DC already holds is explicit, the clip region of the DC applies,
 *	and the DIB is the unit a glyph cache can keep.
 *
 *	d2d1.dll and dwrite.dll are loaded on demand and the factories live in
 *	thread-specific data, like the font families of tkWinFont.c.  Anything
 *	missing or failing makes TkWinDrawColorGlyphs() report that nothing was
 *	drawn, and the caller falls back to ScriptTextOut().
 *
 * Copyright © 2026 Nicolas Bats
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkWinInt.h"
#include <d2d1.h>
#include <dwrite_2.h>

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
    HMODULE d2dModule;		/* d2d1.dll, loaded on demand. */
    HMODULE dwriteModule;	/* dwrite.dll, loaded on demand. */
    ID2D1Factory *d2dFactory;
    IDWriteFactory2 *dwriteFactory;
				/* IDWriteFactory2: TranslateColorGlyphRun
				 * (Windows 8.1 and later). */
    IDWriteGdiInterop *gdiInterop;
				/* Font faces from GDI fonts. */
    ID2D1DCRenderTarget *target;
				/* Bound to the DIB of each run. */
    ID2D1SolidColorBrush *brush;
				/* One brush, recolored per layer. */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

static void		ReleaseContext(void *clientData);
static int		CreateTarget(ThreadSpecificData *tsdPtr);
static ThreadSpecificData *GetContext(void);

/*
 * Every method is called through the interface that introduces it: the C
 * vtables of MinGW nest the base interfaces while those of MSVC flatten
 * them, and only the introducing interface has the member in both.
 */

#define UNKNOWN(obj)	((IUnknown *) (obj))
#define RELEASE(obj) \
    do { if (obj) { UNKNOWN(obj)->lpVtbl->Release(UNKNOWN(obj)); (obj) = NULL; } } while (0)
#define RENDER_TARGET(tsdPtr)	((ID2D1RenderTarget *) (tsdPtr)->target)

/*
 *----------------------------------------------------------------------
 *
 * ReleaseContext --
 *
 *	Release the Direct2D/DirectWrite objects of the thread and unload the
 *	DLLs.  Thread exit handler; also used to start over after a device
 *	loss.
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
    if (tsdPtr->d2dModule) {
	FreeLibrary(tsdPtr->d2dModule);
	tsdPtr->d2dModule = NULL;
    }
    if (tsdPtr->dwriteModule) {
	FreeLibrary(tsdPtr->dwriteModule);
	tsdPtr->dwriteModule = NULL;
    }
    tsdPtr->state = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateTarget --
 *
 *	Create the DC render target and its brush.  Premultiplied alpha: the
 *	target writes the alpha of the layers into the DIB, which AlphaBlend
 *	then composes onto Tk's DC.
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

    memset(&props, 0, sizeof(props));
    props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
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

    tsdPtr->dwriteModule = LoadLibraryW(L"dwrite.dll");
    tsdPtr->d2dModule = LoadLibraryW(L"d2d1.dll");
    if (tsdPtr->dwriteModule == NULL || tsdPtr->d2dModule == NULL) {
	ReleaseContext(tsdPtr);
	tsdPtr->state = -1;
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
 * TkWinDrawColorGlyphs --
 *
 *	Draw the color layers of a run of glyphs of the font selected in hdc,
 *	with its baseline origin at (x, y), from the glyph indices, advances
 *	and offsets that Uniscribe produced for it.  Glyphs of the run without
 *	color layers are drawn in the text color of the DC, like the layers
 *	that refer to it.
 *
 * Results:
 *	1 if the run was drawn, 0 if the caller must draw it with GDI: no
 *	layer in the run, DirectWrite or Direct2D unavailable, a raster
 *	operation other than copy, or any failure.
 *
 * Side effects:
 *	Draws on hdc.  A lost Direct2D device releases the context, which is
 *	rebuilt at the next call.
 *
 *----------------------------------------------------------------------
 */

int
TkWinDrawColorGlyphs(
    HDC hdc,			/* Target device context, font selected. */
    HFONT hFont,		/* Font of the run. */
    int x, int y,		/* Baseline origin of the run. */
    TCL_UNUSED(const SCRIPT_ANALYSIS *),
				/* Direction of the run: the glyphs come in
				 * visual order, as ScriptTextOut draws them. */
    const WORD *glyphs,		/* Glyph indices. */
    const int *advances,	/* Advance widths in pixels. */
    const GOFFSET *offsets,	/* Glyph offsets. */
    int glyphCount)		/* Number of glyphs. */
{
    ThreadSpecificData *tsdPtr;
    IDWriteFontFace *face = NULL;
    IDWriteColorGlyphRunEnumerator *layers = NULL;
    DWRITE_GLYPH_RUN run;
    FLOAT *glyphAdvances;
    DWRITE_GLYPH_OFFSET *glyphOffsets;
    LOGFONTW lf;
    TEXTMETRICW tm;
    BITMAPINFO bmi;
    BLENDFUNCTION blend;
    HDC memDC = NULL;
    HBITMAP dib = NULL, oldBitmap = NULL;
    void *bits;
    RECT rect;
    COLORREF textColor;
    D2D1_COLOR_F color;
    HRESULT hr;
    int i, width, margin, dibWidth, dibHeight, drawn = 0;

    if (glyphCount <= 0 || GetROP2(hdc) != R2_COPYPEN) {
	return 0;
    }
    tsdPtr = GetContext();
    if (tsdPtr == NULL || GetObjectW(hFont, sizeof(lf), &lf) != sizeof(lf)
	    || !GetTextMetricsW(hdc, &tm)) {
	return 0;
    }
    if (FAILED(tsdPtr->gdiInterop->lpVtbl->CreateFontFaceFromHdc(
	    tsdPtr->gdiInterop, hdc, &face))) {
	return 0;
    }

    /*
     * The glyph run of DirectWrite is the one Uniscribe shaped.  Tk creates
     * its fonts with a negative height, which is the em size in pixels; a
     * positive height is a cell height, em size = height - internal leading.
     */

    glyphAdvances = (FLOAT *)Tcl_Alloc(sizeof(FLOAT) * glyphCount);
    glyphOffsets = (DWRITE_GLYPH_OFFSET *)
	    Tcl_Alloc(sizeof(DWRITE_GLYPH_OFFSET) * glyphCount);
    width = 0;
    for (i = 0; i < glyphCount; i++) {
	glyphAdvances[i] = (FLOAT) advances[i];
	glyphOffsets[i].advanceOffset = (FLOAT) offsets[i].du;
	glyphOffsets[i].ascenderOffset = (FLOAT) offsets[i].dv;
	width += advances[i];
    }
    memset(&run, 0, sizeof(run));
    run.fontFace = face;
    run.fontEmSize = (lf.lfHeight < 0) ? (FLOAT) -lf.lfHeight
	    : (FLOAT) (tm.tmHeight - tm.tmInternalLeading);
    run.glyphCount = (UINT32) glyphCount;
    run.glyphIndices = glyphs;
    run.glyphAdvances = glyphAdvances;
    run.glyphOffsets = glyphOffsets;
    run.isSideways = FALSE;
    run.bidiLevel = 0;

    /* DWRITE_E_NOCOLOR: no layer in the run, GDI draws it. */
    hr = tsdPtr->dwriteFactory->lpVtbl->TranslateColorGlyphRun(
	    tsdPtr->dwriteFactory, 0.0f, 0.0f, &run, NULL,
	    DWRITE_MEASURING_MODE_GDI_CLASSIC, NULL, 0, &layers);
    if (FAILED(hr)) {
	layers = NULL;
	goto done;
    }

    /*
     * A DIB the size of the run plus one em on each side: layers may
     * overhang the advances, and offsets move glyphs around.
     */

    margin = (int) run.fontEmSize;
    if (margin < 1) {
	margin = 1;
    }
    dibWidth = width + 2 * margin;
    dibHeight = tm.tmAscent + tm.tmDescent + 2 * margin;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = dibWidth;
    bmi.bmiHeader.biHeight = -dibHeight;	/* Top-down. */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    memDC = CreateCompatibleDC(hdc);
    if (memDC == NULL) {
	goto done;
    }
    dib = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (dib == NULL) {
	goto done;
    }
    oldBitmap = (HBITMAP) SelectObject(memDC, dib);

    rect.left = 0;
    rect.top = 0;
    rect.right = dibWidth;
    rect.bottom = dibHeight;
    if (FAILED(tsdPtr->target->lpVtbl->BindDC(tsdPtr->target, memDC, &rect))) {
	goto done;
    }
    RENDER_TARGET(tsdPtr)->lpVtbl->BeginDraw(RENDER_TARGET(tsdPtr));
    color.r = color.g = color.b = color.a = 0.0f;
    RENDER_TARGET(tsdPtr)->lpVtbl->Clear(RENDER_TARGET(tsdPtr), &color);

    textColor = GetTextColor(hdc);
    for (;;) {
	const DWRITE_COLOR_GLYPH_RUN *layer;
	D2D1_POINT_2F origin;
	BOOL hasRun;

	if (FAILED(layers->lpVtbl->MoveNext(layers, &hasRun)) || !hasRun
		|| FAILED(layers->lpVtbl->GetCurrentRun(layers, &layer))) {
	    break;
	}
	if (layer->paletteIndex == 0xFFFF) {
	    color.r = GetRValue(textColor) / 255.0f;
	    color.g = GetGValue(textColor) / 255.0f;
	    color.b = GetBValue(textColor) / 255.0f;
	    color.a = 1.0f;
	} else {
	    color.r = layer->runColor.r;
	    color.g = layer->runColor.g;
	    color.b = layer->runColor.b;
	    color.a = layer->runColor.a;
	}
	tsdPtr->brush->lpVtbl->SetColor(tsdPtr->brush, &color);
	origin.x = (FLOAT) margin + layer->baselineOriginX;
	origin.y = (FLOAT) (margin + tm.tmAscent) + layer->baselineOriginY;
	RENDER_TARGET(tsdPtr)->lpVtbl->DrawGlyphRun(RENDER_TARGET(tsdPtr), origin,
		&layer->glyphRun, (ID2D1Brush *) tsdPtr->brush,
		DWRITE_MEASURING_MODE_GDI_CLASSIC);
    }
    hr = RENDER_TARGET(tsdPtr)->lpVtbl->EndDraw(RENDER_TARGET(tsdPtr), NULL, NULL);
    if (FAILED(hr)) {
	/* Device lost: start over at the next call. */
	ReleaseContext(tsdPtr);
	goto done;
    }

    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    drawn = AlphaBlend(hdc, x - margin, y - tm.tmAscent - margin,
	    dibWidth, dibHeight, memDC, 0, 0, dibWidth, dibHeight, blend) ? 1 : 0;

  done:
    if (memDC != NULL) {
	if (dib != NULL) {
	    SelectObject(memDC, oldBitmap);
	    DeleteObject(dib);
	}
	DeleteDC(memDC);
    }
    RELEASE(layers);
    RELEASE(face);
    Tcl_Free(glyphOffsets);
    Tcl_Free(glyphAdvances);
    return drawn;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
