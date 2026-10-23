/*
 * tkWinDirect2D.h --
 *
 *	C declarations of the subset of Direct2D and DirectWrite used by
 *	tkWinColorEmoji.c: the interfaces as flat vtables, methods in the
 *	order of the COM ABI, and the few types and constants involved.  The
 *	headers of the Windows SDK declare these interfaces for C++ only,
 *	those of MinGW-w64 for C as well; this private copy gives both
 *	toolchains the same code.  Only the methods called have a prototype;
 *	the other slots keep their name and take no argument.
 *
 *	Reference: the C declarations of MinGW-w64 (d2d1.h, dwrite.h,
 *	dwrite_1.h, dwrite_2.h), derived from the SDK.  Not to be included
 *	with d2d1.h or dwrite.h.
 *
 * Copyright © 2026 Nicolas Bats
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKWINDIRECT2D
#define _TKWINDIRECT2D

#if defined(_D2D1_H_) || defined(DWRITE_H_INCLUDED)
#error "tkWinDirect2D.h replaces d2d1.h and dwrite.h"
#endif

#include <unknwn.h>		/* IUnknown, REFIID. */

/*
 * Types and constants.
 */

typedef enum D2D1_FACTORY_TYPE {
    D2D1_FACTORY_TYPE_SINGLE_THREADED = 0,
    D2D1_FACTORY_TYPE_MULTI_THREADED = 1
} D2D1_FACTORY_TYPE;

typedef enum D2D1_RENDER_TARGET_TYPE {
    D2D1_RENDER_TARGET_TYPE_DEFAULT = 0,
    D2D1_RENDER_TARGET_TYPE_SOFTWARE = 1,
    D2D1_RENDER_TARGET_TYPE_HARDWARE = 2
} D2D1_RENDER_TARGET_TYPE;

typedef enum D2D1_ALPHA_MODE {
    D2D1_ALPHA_MODE_UNKNOWN = 0,
    D2D1_ALPHA_MODE_PREMULTIPLIED = 1,
    D2D1_ALPHA_MODE_STRAIGHT = 2,
    D2D1_ALPHA_MODE_IGNORE = 3
} D2D1_ALPHA_MODE;

typedef enum DXGI_FORMAT {
    DXGI_FORMAT_UNKNOWN = 0,
    DXGI_FORMAT_B8G8R8A8_UNORM = 87
} DXGI_FORMAT;

typedef enum D2D1_RENDER_TARGET_USAGE {
    D2D1_RENDER_TARGET_USAGE_NONE = 0
} D2D1_RENDER_TARGET_USAGE;

typedef enum D2D1_FEATURE_LEVEL {
    D2D1_FEATURE_LEVEL_DEFAULT = 0
} D2D1_FEATURE_LEVEL;

typedef enum D2D1_ANTIALIAS_MODE {
    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE = 0,
    D2D1_ANTIALIAS_MODE_ALIASED = 1
} D2D1_ANTIALIAS_MODE;

typedef enum D2D1_TEXT_ANTIALIAS_MODE {
    D2D1_TEXT_ANTIALIAS_MODE_DEFAULT = 0,
    D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE = 1,
    D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE = 2,
    D2D1_TEXT_ANTIALIAS_MODE_ALIASED = 3
} D2D1_TEXT_ANTIALIAS_MODE;

typedef enum DWRITE_FACTORY_TYPE {
    DWRITE_FACTORY_TYPE_SHARED = 0,
    DWRITE_FACTORY_TYPE_ISOLATED = 1
} DWRITE_FACTORY_TYPE;

typedef enum DWRITE_MEASURING_MODE {
    DWRITE_MEASURING_MODE_NATURAL = 0,
    DWRITE_MEASURING_MODE_GDI_CLASSIC = 1,
    DWRITE_MEASURING_MODE_GDI_NATURAL = 2
} DWRITE_MEASURING_MODE;

typedef UINT64 D2D1_TAG;

typedef struct D2D1_COLOR_F {
    FLOAT r;
    FLOAT g;
    FLOAT b;
    FLOAT a;
} D2D1_COLOR_F;
typedef D2D1_COLOR_F DWRITE_COLOR_F;

typedef struct D2D1_POINT_2F {
    FLOAT x;
    FLOAT y;
} D2D1_POINT_2F;

typedef struct D2D1_RECT_F {
    FLOAT left;
    FLOAT top;
    FLOAT right;
    FLOAT bottom;
} D2D1_RECT_F;

typedef struct D2D1_PIXEL_FORMAT {
    DXGI_FORMAT format;
    D2D1_ALPHA_MODE alphaMode;
} D2D1_PIXEL_FORMAT;

typedef struct D2D1_RENDER_TARGET_PROPERTIES {
    D2D1_RENDER_TARGET_TYPE type;
    D2D1_PIXEL_FORMAT pixelFormat;
    FLOAT dpiX;
    FLOAT dpiY;
    D2D1_RENDER_TARGET_USAGE usage;
    D2D1_FEATURE_LEVEL minLevel;
} D2D1_RENDER_TARGET_PROPERTIES;

typedef struct DWRITE_GLYPH_OFFSET {
    FLOAT advanceOffset;
    FLOAT ascenderOffset;
} DWRITE_GLYPH_OFFSET;

/* Only ever passed as NULL, or received as opaque pointers. */
typedef struct D2D1_FACTORY_OPTIONS D2D1_FACTORY_OPTIONS;
typedef struct D2D1_BRUSH_PROPERTIES D2D1_BRUSH_PROPERTIES;
typedef struct DWRITE_GLYPH_RUN_DESCRIPTION DWRITE_GLYPH_RUN_DESCRIPTION;
typedef struct DWRITE_MATRIX DWRITE_MATRIX;

typedef struct ID2D1Factory ID2D1Factory;
typedef struct ID2D1RenderTarget ID2D1RenderTarget;
typedef struct ID2D1DCRenderTarget ID2D1DCRenderTarget;
typedef struct ID2D1Brush ID2D1Brush;
typedef struct ID2D1SolidColorBrush ID2D1SolidColorBrush;
typedef struct IDWriteFactory IDWriteFactory;
typedef struct IDWriteFactory2 IDWriteFactory2;
typedef struct IDWriteGdiInterop IDWriteGdiInterop;
typedef struct IDWriteFontFace IDWriteFontFace;
typedef struct IDWriteColorGlyphRunEnumerator IDWriteColorGlyphRunEnumerator;

typedef struct DWRITE_GLYPH_RUN {
    IDWriteFontFace *fontFace;
    FLOAT fontEmSize;
    UINT32 glyphCount;
    const UINT16 *glyphIndices;
    const FLOAT *glyphAdvances;
    const DWRITE_GLYPH_OFFSET *glyphOffsets;
    BOOL isSideways;
    UINT32 bidiLevel;
} DWRITE_GLYPH_RUN;

typedef struct DWRITE_COLOR_GLYPH_RUN {
    DWRITE_GLYPH_RUN glyphRun;
    DWRITE_GLYPH_RUN_DESCRIPTION *glyphRunDescription;
    FLOAT baselineOriginX;
    FLOAT baselineOriginY;
    DWRITE_COLOR_F runColor;
    UINT16 paletteIndex;
} DWRITE_COLOR_GLYPH_RUN;

#ifndef DWRITE_E_NOCOLOR
#define DWRITE_E_NOCOLOR	((HRESULT) 0x8898500CL)
#endif

/*
 * Interfaces.  A slot without prototype is a method this code never calls.
 */

#define TK_COM_UNKNOWN_SLOTS \
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *This, REFIID riid, void **object); \
    ULONG (STDMETHODCALLTYPE *AddRef)(void *This); \
    ULONG (STDMETHODCALLTYPE *Release)(void *This)
#define TK_COM_SLOT(name)	void (STDMETHODCALLTYPE *name)(void)

typedef struct ID2D1FactoryVtbl {
    TK_COM_UNKNOWN_SLOTS;
    TK_COM_SLOT(ReloadSystemMetrics);
    TK_COM_SLOT(GetDesktopDpi);
    TK_COM_SLOT(CreateRectangleGeometry);
    TK_COM_SLOT(CreateRoundedRectangleGeometry);
    TK_COM_SLOT(CreateEllipseGeometry);
    TK_COM_SLOT(CreateGeometryGroup);
    TK_COM_SLOT(CreateTransformedGeometry);
    TK_COM_SLOT(CreatePathGeometry);
    TK_COM_SLOT(CreateStrokeStyle);
    TK_COM_SLOT(CreateDrawingStateBlock);
    TK_COM_SLOT(CreateWicBitmapRenderTarget);
    TK_COM_SLOT(CreateHwndRenderTarget);
    TK_COM_SLOT(CreateDxgiSurfaceRenderTarget);
    HRESULT (STDMETHODCALLTYPE *CreateDCRenderTarget)(ID2D1Factory *This,
	    const D2D1_RENDER_TARGET_PROPERTIES *renderTargetProperties,
	    ID2D1DCRenderTarget **dcRenderTarget);
} ID2D1FactoryVtbl;
struct ID2D1Factory {
    const ID2D1FactoryVtbl *lpVtbl;
};

/*
 * ID2D1RenderTarget, shared with ID2D1DCRenderTarget which adds BindDC.
 */

#define TK_D2D1_RENDER_TARGET_SLOTS \
    TK_COM_UNKNOWN_SLOTS; \
    TK_COM_SLOT(GetFactory); \
    TK_COM_SLOT(CreateBitmap); \
    TK_COM_SLOT(CreateBitmapFromWicBitmap); \
    TK_COM_SLOT(CreateSharedBitmap); \
    TK_COM_SLOT(CreateBitmapBrush); \
    HRESULT (STDMETHODCALLTYPE *CreateSolidColorBrush)(ID2D1RenderTarget *This, \
	    const D2D1_COLOR_F *color, \
	    const D2D1_BRUSH_PROPERTIES *brushProperties, \
	    ID2D1SolidColorBrush **solidColorBrush); \
    TK_COM_SLOT(CreateGradientStopCollection); \
    TK_COM_SLOT(CreateLinearGradientBrush); \
    TK_COM_SLOT(CreateRadialGradientBrush); \
    TK_COM_SLOT(CreateCompatibleRenderTarget); \
    TK_COM_SLOT(CreateLayer); \
    TK_COM_SLOT(CreateMesh); \
    TK_COM_SLOT(DrawLine); \
    TK_COM_SLOT(DrawRectangle); \
    TK_COM_SLOT(FillRectangle); \
    TK_COM_SLOT(DrawRoundedRectangle); \
    TK_COM_SLOT(FillRoundedRectangle); \
    TK_COM_SLOT(DrawEllipse); \
    TK_COM_SLOT(FillEllipse); \
    TK_COM_SLOT(DrawGeometry); \
    TK_COM_SLOT(FillGeometry); \
    TK_COM_SLOT(FillMesh); \
    TK_COM_SLOT(FillOpacityMask); \
    TK_COM_SLOT(DrawBitmap); \
    TK_COM_SLOT(DrawText); \
    TK_COM_SLOT(DrawTextLayout); \
    void (STDMETHODCALLTYPE *DrawGlyphRun)(ID2D1RenderTarget *This, \
	    D2D1_POINT_2F baselineOrigin, const DWRITE_GLYPH_RUN *glyphRun, \
	    ID2D1Brush *foregroundBrush, DWRITE_MEASURING_MODE measuringMode); \
    TK_COM_SLOT(SetTransform); \
    TK_COM_SLOT(GetTransform); \
    TK_COM_SLOT(SetAntialiasMode); \
    TK_COM_SLOT(GetAntialiasMode); \
    void (STDMETHODCALLTYPE *SetTextAntialiasMode)(ID2D1RenderTarget *This, \
	    D2D1_TEXT_ANTIALIAS_MODE textAntialiasMode); \
    TK_COM_SLOT(GetTextAntialiasMode); \
    TK_COM_SLOT(SetTextRenderingParams); \
    TK_COM_SLOT(GetTextRenderingParams); \
    TK_COM_SLOT(SetTags); \
    TK_COM_SLOT(GetTags); \
    TK_COM_SLOT(PushLayer); \
    TK_COM_SLOT(PopLayer); \
    TK_COM_SLOT(Flush); \
    TK_COM_SLOT(SaveDrawingState); \
    TK_COM_SLOT(RestoreDrawingState); \
    void (STDMETHODCALLTYPE *PushAxisAlignedClip)(ID2D1RenderTarget *This, \
	    const D2D1_RECT_F *clipRect, D2D1_ANTIALIAS_MODE antialiasMode); \
    void (STDMETHODCALLTYPE *PopAxisAlignedClip)(ID2D1RenderTarget *This); \
    void (STDMETHODCALLTYPE *Clear)(ID2D1RenderTarget *This, \
	    const D2D1_COLOR_F *clearColor); \
    void (STDMETHODCALLTYPE *BeginDraw)(ID2D1RenderTarget *This); \
    HRESULT (STDMETHODCALLTYPE *EndDraw)(ID2D1RenderTarget *This, \
	    D2D1_TAG *tag1, D2D1_TAG *tag2); \
    TK_COM_SLOT(GetPixelFormat); \
    TK_COM_SLOT(SetDpi); \
    TK_COM_SLOT(GetDpi); \
    TK_COM_SLOT(GetSize); \
    TK_COM_SLOT(GetPixelSize); \
    TK_COM_SLOT(GetMaximumBitmapSize); \
    TK_COM_SLOT(IsSupported)

typedef struct ID2D1RenderTargetVtbl {
    TK_D2D1_RENDER_TARGET_SLOTS;
} ID2D1RenderTargetVtbl;
struct ID2D1RenderTarget {
    const ID2D1RenderTargetVtbl *lpVtbl;
};

typedef struct ID2D1DCRenderTargetVtbl {
    TK_D2D1_RENDER_TARGET_SLOTS;
    HRESULT (STDMETHODCALLTYPE *BindDC)(ID2D1DCRenderTarget *This,
	    const HDC hDC, const RECT *pSubRect);
} ID2D1DCRenderTargetVtbl;
struct ID2D1DCRenderTarget {
    const ID2D1DCRenderTargetVtbl *lpVtbl;
};

typedef struct ID2D1SolidColorBrushVtbl {
    TK_COM_UNKNOWN_SLOTS;
    TK_COM_SLOT(GetFactory);
    TK_COM_SLOT(SetOpacity);
    TK_COM_SLOT(SetTransform);
    TK_COM_SLOT(GetOpacity);
    TK_COM_SLOT(GetTransform);
    void (STDMETHODCALLTYPE *SetColor)(ID2D1SolidColorBrush *This,
	    const D2D1_COLOR_F *color);
    TK_COM_SLOT(GetColor);
} ID2D1SolidColorBrushVtbl;
struct ID2D1SolidColorBrush {
    const ID2D1SolidColorBrushVtbl *lpVtbl;
};

/*
 * IDWriteFactory, extended by IDWriteFactory1 and IDWriteFactory2.
 */

#define TK_DWRITE_FACTORY_SLOTS \
    TK_COM_UNKNOWN_SLOTS; \
    TK_COM_SLOT(GetSystemFontCollection); \
    TK_COM_SLOT(CreateCustomFontCollection); \
    TK_COM_SLOT(RegisterFontCollectionLoader); \
    TK_COM_SLOT(UnregisterFontCollectionLoader); \
    TK_COM_SLOT(CreateFontFileReference); \
    TK_COM_SLOT(CreateCustomFontFileReference); \
    TK_COM_SLOT(CreateFontFace); \
    TK_COM_SLOT(CreateRenderingParams); \
    TK_COM_SLOT(CreateMonitorRenderingParams); \
    TK_COM_SLOT(CreateCustomRenderingParams); \
    TK_COM_SLOT(RegisterFontFileLoader); \
    TK_COM_SLOT(UnregisterFontFileLoader); \
    TK_COM_SLOT(CreateTextFormat); \
    TK_COM_SLOT(CreateTypography); \
    HRESULT (STDMETHODCALLTYPE *GetGdiInterop)(IDWriteFactory *This, \
	    IDWriteGdiInterop **gdiInterop); \
    TK_COM_SLOT(CreateTextLayout); \
    TK_COM_SLOT(CreateGdiCompatibleTextLayout); \
    TK_COM_SLOT(CreateEllipsisTrimmingSign); \
    TK_COM_SLOT(CreateTextAnalyzer); \
    TK_COM_SLOT(CreateNumberSubstitution); \
    TK_COM_SLOT(CreateGlyphRunAnalysis)

typedef struct IDWriteFactoryVtbl {
    TK_DWRITE_FACTORY_SLOTS;
} IDWriteFactoryVtbl;
struct IDWriteFactory {
    const IDWriteFactoryVtbl *lpVtbl;
};

typedef struct IDWriteFactory2Vtbl {
    TK_DWRITE_FACTORY_SLOTS;
    TK_COM_SLOT(GetEudcFontCollection);		/* IDWriteFactory1 */
    TK_COM_SLOT(CreateCustomRenderingParams1);
    TK_COM_SLOT(GetSystemFontFallback);		/* IDWriteFactory2 */
    TK_COM_SLOT(CreateFontFallbackBuilder);
    HRESULT (STDMETHODCALLTYPE *TranslateColorGlyphRun)(IDWriteFactory2 *This,
	    FLOAT baselineOriginX, FLOAT baselineOriginY,
	    const DWRITE_GLYPH_RUN *glyphRun,
	    const DWRITE_GLYPH_RUN_DESCRIPTION *glyphRunDescription,
	    DWRITE_MEASURING_MODE measuringMode,
	    const DWRITE_MATRIX *worldToDeviceTransform,
	    UINT32 colorPaletteIndex,
	    IDWriteColorGlyphRunEnumerator **colorLayers);
    TK_COM_SLOT(CreateCustomRenderingParams2);
    TK_COM_SLOT(CreateGlyphRunAnalysis2);
} IDWriteFactory2Vtbl;
struct IDWriteFactory2 {
    const IDWriteFactory2Vtbl *lpVtbl;
};

typedef struct IDWriteGdiInteropVtbl {
    TK_COM_UNKNOWN_SLOTS;
    TK_COM_SLOT(CreateFontFromLOGFONT);
    TK_COM_SLOT(ConvertFontToLOGFONT);
    TK_COM_SLOT(ConvertFontFaceToLOGFONT);
    HRESULT (STDMETHODCALLTYPE *CreateFontFaceFromHdc)(IDWriteGdiInterop *This,
	    HDC hdc, IDWriteFontFace **fontFace);
    TK_COM_SLOT(CreateBitmapRenderTarget);
} IDWriteGdiInteropVtbl;
struct IDWriteGdiInterop {
    const IDWriteGdiInteropVtbl *lpVtbl;
};

typedef struct IDWriteColorGlyphRunEnumeratorVtbl {
    TK_COM_UNKNOWN_SLOTS;
    HRESULT (STDMETHODCALLTYPE *MoveNext)(IDWriteColorGlyphRunEnumerator *This,
	    BOOL *hasRun);
    HRESULT (STDMETHODCALLTYPE *GetCurrentRun)(
	    IDWriteColorGlyphRunEnumerator *This,
	    const DWRITE_COLOR_GLYPH_RUN **colorGlyphRun);
} IDWriteColorGlyphRunEnumeratorVtbl;
struct IDWriteColorGlyphRunEnumerator {
    const IDWriteColorGlyphRunEnumeratorVtbl *lpVtbl;
};

#endif /* _TKWINDIRECT2D */
