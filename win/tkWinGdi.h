/*
 * tkWinGdi.h --
 *
 *	defines macros for gdicalls ,implementation is tkWinGdi.c
 *
 * Copyright (c) Brueckner & Jarosch Ing.GmbH,Erfurt,Germany 1997
 * hacked by Leo
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */
#ifndef __CKGRAPH_H
#define __CKGRAPH_H
/*
 * define this for having profile stubs for VC
 */
//#define PROFILE_INFO
/* 
 * define this before including tkWinGdi.h to have pure Gdi-calls
 *  regardless of the defines in this header
 */
#ifndef NO_CKGRAPH_IMP
#define USE_CKGRAPH_IMP
#define CKGRAPH_IMP
/*uncomment this for tracing nearly all Gdi-objects*/
//////#define CKGRAPH_DEBUG
#endif /*NO_CKGRAPH_IMP*/
//#define CKGRAPH_TRACE
#ifdef CKGRAPH_TRACE
#define GTRACE(A) gdiprintf A
#else
#define GTRACE(A)
#endif

void CkGraph_Init(HINSTANCE hInstance);

void  CkGraph_SetTracing(int trace);
int   CkGraph_GetTracing(void);
char* CkGraph_GetTraceFile(void);

#ifdef CKGRAPH_IMP
/*functions dealing with hashed memory DC's*/
HDC     CkGraph_GetHashedDC(void);
void    CkGraph_ReleaseHashedDC(HDC hdc);
void    CkGraph_FreeHashedDCs(void) ;
void    CkGraph_ClearDC(HDC dc);
#ifdef CKGRAPH_DEBUG
void    CkGraph_CheckSelectedBitmap(HDC hdc,HBITMAP hbitmap);
#endif
void    CkGraph_CheckDCs(char* fileName);

/*functions dealing with hashed Bitmaps*/
HBITMAP CkGraph_GetHashedBitmap( int width, int height,int planes,int depth);
void    CkGraph_ReleaseHashedBitmap(HBITMAP pixmap);
void    CkGraph_FreeHashedBitmaps(void);
int     CkGraph_IsPixmap(HANDLE drawable);

void CkGraph_RegisterDeviceContext(HDC hdc) ;
void CkGraph_UnRegisterDeviceContext(HDC hdc) ;
int CkGraph_DumpActiveObjects (char* fileName);

void CkGraph_FreeObjects(void);
#ifdef CKGRAPH_DEBUG
extern int tkWinGdi_CreatePen;
extern int tkWinGdi_ExtCreatePen;
extern int tkWinGdi_CreateSolidBrush;
extern int tkWinGdi_CreatePatternBrush;
extern int tkWinGdi_CreateDIBitmap;
extern int tkWinGdi_CreateCompatibleBitmap;
extern int tkWinGdi_LoadBitmap;
extern int tkWinGdi_CreateBitmap;
extern int tkWinGdi_CreateRectRgn;
extern int tkWinGdi_CreateFont;
extern int tkWinGdi_CreateFontIndirect;
extern int tkWinGdi_CreatePalette;
extern int tkWinGdi_GetDC;
extern int tkWinGdi_CreateDC;
extern int tkWinGdi_ReleaseDC;
extern int tkWinGdi_CreateCompatibleDC;
extern int tkWinGdi_DeleteDC;
extern int tkWinGdi_BeginPaint;
extern int tkWinGdi_EndPaint;
extern int tkWinGdi_SelectObject;
extern int tkWinGdi_SelectBitmap;
extern int tkWinGdi_SelectPen;
extern int tkWinGdi_SelectBrush;
extern int tkWinGdi_SelectPalette;
extern int tkWinGdi_SelectFont;
extern int tkWinGdi_DeleteObject;
extern int tkWinGdi_DeleteBrush;
extern int tkWinGdi_DeletePen;
extern int tkWinGdi_DeleteFont;
extern int tkWinGdi_DeleteBitmap;
extern int tkWinGdi_DeletePalette;
extern int tkWinGdi_RealizePalette;
extern int tkWinGdi_SetROP2;
extern int tkWinGdi_SetBkMode;
extern int tkWinGdi_SetBkColor;
extern int tkWinGdi_SetTextColor;
extern int tkWinGdi_SelectClipRgn;
extern int tkWinGdi_OffsetClipRgn;
extern int tkWinGdi_BitBlt;
extern int tkWinGdi_FillRect;
extern int tkWinGdi_Polyline;
extern int tkWinGdi_Polygon;
extern int tkWinGdi_Arc;
extern int tkWinGdi_Chord;
extern int tkWinGdi_Pie;
extern int tkWinGdi_Rectangle;
extern int tkWinGdi_TextOut;
extern int tkWinGdi_ExtTextOut;
extern int tkWinGdi_SetPolyFillMode;

void CkGraph_RegisterDeviceContext(HDC hdc) ;
void CkGraph_UnRegisterDeviceContext(HDC hdc) ;
int CkGraph_DumpActiveObjects (char* fileName);

HPEN CkGraph_CreatePen(int fnPenStyle,int nWidth,COLORREF  crColor,char* file,int line);
HPEN CkGraph_ExtCreatePen(DWORD dwPenStyle,DWORD dwWidth,CONST LOGBRUSH* lplb,
                          DWORD dwStyleCount, CONST DWORD* lpStyle,
                          char* file,int line);
HBRUSH CkGraph_CreateSolidBrush(COLORREF crColor,char* file,int line);
HBRUSH CkGraph_CreatePatternBrush(HBITMAP hbitmap,char* file,int line);
HBITMAP CkGraph_CreateDIBitmap(HDC hdc, BITMAPINFOHEADER *lpbmih,
                               DWORD fdwInit, VOID*lpbInit,
                                BITMAPINFO *lpbmi,UINT fuUsage,char* file,int line);
HBITMAP CkGraph_CreateBitmap(int nWidth,int nHeight,UINT cPlanes,UINT cBitsPerPel,
                            VOID* lpvBits,char* file,int line);
HBITMAP CkGraph_CreateCompatibleBitmap(HDC hdc,int nWidth,int nHeight,char* file,int line);
HBITMAP  CkGraph_LoadBitmap(HINSTANCE hinst,LPCTSTR lpszBitmap,char* file,int line);
HRGN CkGraph_CreateRectRgn(int nLeftRect,int nTopRect,int nRightRect,int nBottomRect,char* file,int line);
HFONT CkGraph_CreateFontIndirect( LOGFONT* lplogfont,char* file,int line);
HPALETTE CkGraph_CreatePalette(  LOGPALETTE* lplogpal,char* file,int line);
HPALETTE CkGraph_CreatePalette(  LOGPALETTE* lplogpal,char* file,int line);
HGDIOBJ CkGraph_GetStockObject(int stockobj,char* file,int line);

#define CkCreatePen(fnPenStyle,nWidth,crColor) \
        CkGraph_CreatePen(fnPenStyle,nWidth,crColor,__FILE__,__LINE__)
#define CkExtCreatePen(dwPenStyle,dwWidth,lplb,dwStyleCount,lpStyle) \
        CkGraph_ExtCreatePen(dwPenStyle,dwWidth,lplb,dwStyleCount,lpStyle,__FILE__,__LINE__)
#define CkCreateSolidBrush(crColor) CkGraph_CreateSolidBrush(crColor,__FILE__,__LINE__)
#define CkCreatePatternBrush(bitmap) CkGraph_CreatePatternBrush(bitmap,__FILE__,__LINE__)
#define CkCreateDIBitmap(hdc,lpbmih,fdwInit,lpbInit,lpbmi,fuUsage) \
        CkGraph_CreateDIBitmap(hdc,lpbmih,fdwInit,lpbInit,lpbmi,fuUsage,__FILE__,__LINE__)
#define CkCreateBitmap(nWidth,nHeight,cPlanes,cBitsPerPel,lpvBits)\
        CkGraph_CreateBitmap(nWidth,nHeight,cPlanes,cBitsPerPel,lpvBits,__FILE__,__LINE__)
#define CkCreateCompatibleBitmap(hdc,nWidth,nHeight) \
        CkGraph_CreateCompatibleBitmap(hdc,nWidth,nHeight,__FILE__,__LINE__)
#define CkLoadBitmap(hinst,lpszBitmap) \
        CkGraph_LoadBitmap(hinst,lpszBitmap,__FILE__,__LINE__)
#define CkCreateRectRgn(nLeftRect,nTopRect,nRightRect,nBottomRect)\
        CkGraph_CreateRectRgn(nLeftRect,nTopRect,nRightRect,nBottomRect,__FILE__,__LINE__)
#define CkCreateFontIndirect(lplogfont) CkGraph_CreateFontIndirect(lplogfont,__FILE__,__LINE__)
#define CkCreatePalette(lplogpal) CkGraph_CreatePalette(lplogpal,__FILE__,__LINE__)
#define CkGetStockObject(stockobj) CkGraph_GetStockObject(stockobj,__FILE__,__LINE__)

HDC CkGraph_GetDC(HWND hwnd,char* file,int line);
int CkGraph_ReleaseDC(HWND hwnd,HDC hdc,char* file,int line);
HDC CkGraph_CreateCompatibleDC(HDC hdc,char* file,int line);
HDC CkGraph_CreateDC(LPCTSTR lpszDriver,LPCTSTR lpszDevice,LPCTSTR lpszOutput,
                     CONST DEVMODE* lpInitData,char* file,int line);
int CkGraph_DeleteDC(HDC hdc,char* file,int line);
HDC CkGraph_BeginPaint(HWND hwnd,LPPAINTSTRUCT lpps,char* file,int line);
BOOL CkGraph_EndPaint(HWND hwnd, PAINTSTRUCT* lpPaint,char* file,int line);
HGDIOBJ CkGraph_SelectObject(HDC hdc,HGDIOBJ hobj_in,char* file,int line);
HBRUSH CkGraph_SelectBrush(HDC hdc,HBRUSH hobj_in,char* file,int line);
HPEN CkGraph_SelectPen(HDC hdc,HPEN hobj_in,char* file,int line);
HFONT CkGraph_SelectFont(HDC hdc,HFONT hobj_in,char* file,int line);
HBITMAP CkGraph_SelectBitmap(HDC hdc,HBITMAP hobj_in,char* file,int line);
void CkGraph_CheckOutBitmap(HBITMAP hobj_in,char* file,int line);
int CkGraph_SelectClipRgn(HDC hdc,HRGN hrgn,char* file,int line);
int CkGraph_OffsetClipRgn(HDC hdc,int xoff,int yoff,char* file,int line);
HPALETTE CkGraph_SelectPalette(HDC hdc,HPALETTE hobj_in,BOOL f,char* file,int line);
UINT CkGraph_RealizePalette(HDC hdc,char* file,int line);
UINT CkGraph_SetPaletteEntries(HPALETTE hPal,UINT iStart,UINT cEntries ,
               CONST PALETTEENTRY* lppe,char* file,int line);
UINT CkGraph_ResizePalette(HPALETTE hPal,UINT nEntries,char* file,int line);
int CkGraph_SetROP2(HDC hdc,int fnDrawMode,char* file,int line);
int CkGraph_SetBkMode(HDC hdc,int iBkMode,char* file,int line);
COLORREF CkGraph_SetColor(HDC hdc,COLORREF color,int bk,char* file,int line);
int CkGraph_SetPolyFillMode(HDC hdc, int iMode,char* file,int line);
int CkGraph_DeleteObject(HGDIOBJ hobj,char* file,int line);
int CkGraph_DeleteBrush(HBRUSH hobj,char* file,int line);
int CkGraph_DeletePen(HPEN hobj,char* file,int line);
int CkGraph_DeleteFont(HFONT hobj,char* file,int line);
int CkGraph_DeleteBitmap(HBITMAP hobj,char* file,int line);
int CkGraph_DeletePalette(HPALETTE hobj,char* file,int line);

#define CkGetDC(hwnd) CkGraph_GetDC(hwnd,__FILE__,__LINE__)
#define CkReleaseDC(hwnd,hdc) CkGraph_ReleaseDC(hwnd,hdc,__FILE__,__LINE__)
#define CkCreateCompatibleDC(hdc) CkGraph_CreateCompatibleDC(hdc,__FILE__,__LINE__)
#define CkCreateDC(lpszDriver,lpszDevice,lpszOutput,lpInitData)\
        CkGraph_CreateDC(lpszDriver,lpszDevice,lpszOutput,lpInitData,__FILE__,__LINE__)
#define CkDeleteDC(hdc) CkGraph_DeleteDC(hdc,__FILE__,__LINE__)
#define CkBeginPaint(hwnd,lpps) CkGraph_BeginPaint(hwnd,lpps,__FILE__,__LINE__)
#define CkEndPaint(hwnd,lpPaint) CkGraph_EndPaint(hwnd,lpPaint,__FILE__,__LINE__)
#define CkSelectObject(hdc,hobj_in) CkGraph_SelectObject(hdc,hobj_in,__FILE__,__LINE__)
#define CkSelectBitmap(hdc,hobj_in) CkGraph_SelectBitmap(hdc,hobj_in,__FILE__,__LINE__)
#define CkCheckOutBitmap(hobj_in) CkGraph_CheckOutBitmap(hobj_in,__FILE__,__LINE__)
#define CkSelectBrush(hdc,hobj_in) CkGraph_SelectBrush(hdc,hobj_in,__FILE__,__LINE__)
#define CkSelectPen(hdc,hobj_in) CkGraph_SelectPen(hdc,hobj_in,__FILE__,__LINE__)
#define CkSelectFont(hdc,hobj_in) CkGraph_SelectFont(hdc,hobj_in,__FILE__,__LINE__)
#define CkSelectClipRgn(hdc,hrgn) CkGraph_SelectClipRgn(hdc,hrgn,__FILE__,__LINE__)
#define CkOffsetClipRgn(hdc,xoff,yoff) CkGraph_OffsetClipRgn(hdc,xoff,yoff,__FILE__,__LINE__)
#define CkSelectPalette(hdc,hobj_in,f) CkGraph_SelectPalette(hdc,hobj_in,f,__FILE__,__LINE__)
#define CkRealizePalette(hdc) CkGraph_RealizePalette(hdc,__FILE__,__LINE__)
#define CkSetPaletteEntries(hPal,iStart,cEntries,lppe)\
   CkGraph_SetPaletteEntries(hPal,iStart,cEntries,lppe,__FILE__,__LINE__)
#define CkResizePalette(hPal,nEntries) CkGraph_ResizePalette(hPal,nEntries,__FILE__,__LINE__)
#define CkSetROP2(hdc,rop) CkGraph_SetROP2(hdc,rop,__FILE__,__LINE__)
#define CkSetBkMode(hdc,bkmode) CkGraph_SetBkMode(hdc,bkmode,__FILE__,__LINE__)
#define CkSetBkColor(hdc,bkcolor)    CkGraph_SetColor(hdc,bkcolor,1,__FILE__,__LINE__)
#define CkSetTextColor(hdc,txtcolor) CkGraph_SetColor(hdc,txtcolor,0,__FILE__,__LINE__)
#define CkSetPolyFillMode(hdc,iMode) CkGraph_SetPolyFillMode(hdc,iMode,__FILE__,__LINE__)
#define CkDeleteObject(hobj) CkGraph_DeleteObject(hobj,__FILE__,__LINE__)
#define CkDeleteBrush(hobj) CkGraph_DeleteBrush(hobj,__FILE__,__LINE__)
#define CkDeletePen(hobj) CkGraph_DeletePen(hobj,__FILE__,__LINE__)
#define CkDeleteFont(hobj) CkGraph_DeleteFont(hobj,__FILE__,__LINE__)
#define CkDeleteBitmap(hobj) CkGraph_DeleteBitmap(hobj,__FILE__,__LINE__)
#define CkDeletePalette(hobj) CkGraph_DeletePalette(hobj,__FILE__,__LINE__)
#else  /*CKGRAPH_DEBUG*/
#ifdef PROFILE_INFO
HPEN CkGraph_CreatePen(int fnPenStyle,int nWidth,COLORREF  crColor);
HPEN CkGraph_ExtCreatePen(DWORD dwPenStyle,DWORD dwWidth,CONST LOGBRUSH* lplb,
                          DWORD dwStyleCount, CONST DWORD* lpStyle);
HBRUSH CkGraph_CreateSolidBrush(COLORREF crColor);
HBRUSH CkGraph_CreatePatternBrush(HBITMAP hbitmap);
HBITMAP CkGraph_CreateDIBitmap(HDC hdc, BITMAPINFOHEADER *lpbmih,
                               DWORD fdwInit, VOID*lpbInit,
                                BITMAPINFO *lpbmi,UINT fuUsage);
HBITMAP CkGraph_CreateCompatibleBitmap(HDC hdc,int nWidth,int nHeight);
HBITMAP CkGraph_LoadBitmap(HINSTANCE hinst,LPCTSTR lpszBitmap);
HBITMAP CkGraph_CreateBitmap(int nWidth,int nHeight,UINT cPlanes,UINT cBitsPerPel, VOID* lpvBits);
HRGN CkGraph_CreateRectRgn(int nLeftRect,int nTopRect,int nRightRect,int nBottomRect);
HFONT CkGraph_CreateFontIndirect( LOGFONT* lplogfont);
HPALETTE CkGraph_CreatePalette( LOGPALETTE* lplogpal);
HGDIOBJ CkGraph_GetStockObject(int stockobj);
#define CkCreatePen(fnPenStyle,nWidth,crColor) \
        CkGraph_CreatePen(fnPenStyle,nWidth,crColor)
#define CkExtCreatePen(dwPenStyle,dwWidth,lplb,dwStyleCount,lpStyle) \
        CkGraph_ExtCreatePen(dwPenStyle,dwWidth,lplb,dwStyleCount,lpStyle)
#define CkCreateSolidBrush(crColor) CkGraph_CreateSolidBrush(crColor)
#define CkCreatePatternBrush(bitmap) CkGraph_CreatePatternBrush(bitmap)
#define CkCreateDIBitmap(hdc,lpbmih,fdwInit,lpbInit,lpbmi,fuUsage) \
        CkGraph_CreateDIBitmap(hdc,lpbmih,fdwInit,lpbInit,lpbmi,fuUsage)
#define CkCreateBitmap(nWidth,nHeight,cPlanes,cBitsPerPel,lpvBits)\
        CkGraph_CreateBitmap(nWidth,nHeight,cPlanes,cBitsPerPel,lpvBits)
#define CkCreateCompatibleBitmap(hdc,nWidth,nHeight) \
        CkGraph_CreateCompatibleBitmap(hdc,nWidth,nHeight)
#define CkLoadBitmap(hinst,lpszBitmap) \
        CkGraph_LoadBitmap(hinst,lpszBitmap)
#define CkCreateRectRgn(nLeftRect,nTopRect,nRightRect,nBottomRect)\
        CkGraph_CreateRectRgn(nLeftRect,nTopRect,nRightRect,nBottomRect)
#define CkCreateFontIndirect(lplogfont) CkGraph_CreateFontIndirect(lplogfont)
#define CkCreatePalette(lplogpal) CkGraph_CreatePalette(lplogpal)
#define CkGetStockObject(stockobj) CkGraph_GetStockObject(stockobj)

#else /*PROFILE_INFO*/
#define CkCreatePen(fnPenStyle,nWidth,crColor) \
        CreatePen(fnPenStyle,nWidth,crColor)
#define CkExtCreatePen(dwPenStyle,dwWidth,lplb,dwStyleCount,lpStyle) \
        ExtCreatePen(dwPenStyle,dwWidth,lplb,dwStyleCount,lpStyle)
#define CkCreateSolidBrush(crColor) CreateSolidBrush(crColor)
#define CkCreatePatternBrush(bitmap) CreatePatternBrush(bitmap)
#define CkCreateDIBitmap(hdc,lpbmih,fdwInit,lpbInit,lpbmi,fuUsage) \
        CreateDIBitmap(hdc,lpbmih,fdwInit,lpbInit,lpbmi,fuUsage)
#define CkCreateBitmap(nWidth,nHeight,cPlanes,cBitsPerPel,lpvBits)\
        CreateBitmap(nWidth,nHeight,cPlanes,cBitsPerPel,lpvBits)
#define CkCreateCompatibleBitmap(hdc,nWidth,nHeight) \
        CreateCompatibleBitmap(hdc,nWidth,nHeight)
#define CkLoadBitmap(hinst,lpszBitmap) \
        LoadBitmap(hinst,lpszBitmap)
#define CkCreateRectRgn(nLeftRect,nTopRect,nRightRect,nBottomRect)\
        CreateRectRgn(nLeftRect,nTopRect,nRightRect,nBottomRect)
#define CkCreateFontIndirect(lplogfont) CreateFontIndirect(lplogfont)
#define CkCreatePalette(lplogpal) CreatePalette(lplogpal)
#define CkGetStockObject(stockobj) GetStockObject(stockobj)

#endif /*PROFILE_INFO*/
HDC CkGraph_GetDC(HWND hwnd);
int CkGraph_ReleaseDC(HWND hwnd,HDC hdc);
HDC CkGraph_CreateCompatibleDC(HDC hdc);
HDC CkGraph_CreateDC(LPCTSTR lpszDriver,LPCTSTR lpszDevice,LPCTSTR lpszOutput,
                     CONST DEVMODE* lpInitData);
int CkGraph_DeleteDC(HDC hdc);
HDC CkGraph_BeginPaint(HWND hwnd,LPPAINTSTRUCT lpps);
BOOL     CkGraph_EndPaint(HWND hwnd, PAINTSTRUCT* lpPaint);
HGDIOBJ  CkGraph_SelectObject(HDC hdc,HGDIOBJ hobj_in);
HBRUSH   CkGraph_SelectBrush(HDC hdc,HBRUSH hobj_in);
HPEN     CkGraph_SelectPen(HDC hdc,HPEN hobj_in);
HFONT    CkGraph_SelectFont(HDC hdc,HFONT hobj_in);
HBITMAP  CkGraph_SelectBitmap(HDC hdc,HBITMAP hobj_in);
void     CkGraph_CheckOutBitmap(HBITMAP hobj_in);
int CkGraph_SelectClipRgn(HDC hdc,HRGN hrgn);
int CkGraph_OffsetClipRgn(HDC hdc,int xoff,int yoff);
HPALETTE CkGraph_SelectPalette(HDC hdc,HPALETTE hobj_in,BOOL f);
UINT CkGraph_RealizePalette(HDC hdc);
UINT CkGraph_SetPaletteEntries(HPALETTE hPal,UINT iStart,UINT cEntries ,
               CONST PALETTEENTRY* lppe);
UINT CkGraph_ResizePalette(HPALETTE hPal,UINT nEntries);
int CkGraph_SetROP2(HDC hdc,int fnDrawMode);
int CkGraph_SetBkMode(HDC hdc,int iBkMode);
COLORREF CkGraph_SetColor(HDC hdc,COLORREF color,int bk);
int CkGraph_SetPolyFillMode(HDC hdc, int iMode);
int CkGraph_DeleteObject(HGDIOBJ hobj);
int CkGraph_DeleteBrush(HBRUSH hobj);
int CkGraph_DeletePen(HPEN hobj);
int CkGraph_DeleteFont(HFONT hobj);
int CkGraph_DeleteBitmap(HBITMAP hobj);
int CkGraph_DeletePalette(HPALETTE hobj);

#define CkGetDC(hwnd) CkGraph_GetDC(hwnd)
#define CkReleaseDC(hwnd,hdc) CkGraph_ReleaseDC(hwnd,hdc)
#define CkCreateCompatibleDC(hdc) CkGraph_CreateCompatibleDC(hdc)
#define CkCreateDC(lpszDriver,lpszDevice,lpszOutput,lpInitData)\
        CkGraph_CreateDC(lpszDriver,lpszDevice,lpszOutput,lpInitData)
#define CkDeleteDC(hdc) CkGraph_DeleteDC(hdc)
#define CkBeginPaint(hwnd,lpps) CkGraph_BeginPaint(hwnd,lpps)
#define CkEndPaint(hwnd,lpPaint) CkGraph_EndPaint(hwnd,lpPaint)
#define CkSelectObject(hdc,hobj_in) CkGraph_SelectObject(hdc,hobj_in)
#define CkSelectBitmap(hdc,hobj_in) CkGraph_SelectBitmap(hdc,hobj_in)
#define CkCheckOutBitmap(hobj_in)  CkGraph_CheckOutBitmap(hobj_in)
#define CkSelectBrush(hdc,hobj_in) CkGraph_SelectBrush(hdc,hobj_in)
#define CkSelectPen(hdc,hobj_in) CkGraph_SelectPen(hdc,hobj_in)
#define CkSelectFont(hdc,hobj_in) CkGraph_SelectFont(hdc,hobj_in)
#define CkSelectClipRgn(hdc,hrgn) CkGraph_SelectClipRgn(hdc,hrgn)
#define CkOffsetClipRgn(hdc,xoff,yoff) CkGraph_OffsetClipRgn(hdc,xoff,yoff)
#define CkSelectPalette(hdc,hobj_in,f) CkGraph_SelectPalette(hdc,hobj_in,f)
#define CkRealizePalette(hdc) CkGraph_RealizePalette(hdc)
#define CkSetPaletteEntries(hPal,iStart,cEntries,lppe)\
   CkGraph_SetPaletteEntries(hPal,iStart,cEntries,lppe)
#define CkResizePalette(hPal,nEntries) CkGraph_ResizePalette(hPal,nEntries)
#define CkSetROP2(hdc,rop) CkGraph_SetROP2(hdc,rop)
#define CkSetBkMode(hdc,bkmode) CkGraph_SetBkMode(hdc,bkmode)
#define CkSetBkColor(hdc,bkcolor)    CkGraph_SetColor(hdc,bkcolor,1)
#define CkSetTextColor(hdc,txtcolor) CkGraph_SetColor(hdc,txtcolor,0)
#define CkSetPolyFillMode(hdc,iMode) CkGraph_SetPolyFillMode(hdc,iMode)
#define CkDeleteObject(hobj) CkGraph_DeleteObject(hobj)
#define CkDeleteBrush(hobj) CkGraph_DeleteBrush(hobj)
#define CkDeletePen(hobj) CkGraph_DeletePen(hobj)
#define CkDeleteFont(hobj) CkGraph_DeleteFont(hobj)
#define CkDeleteBitmap(hobj) CkGraph_DeleteBitmap(hobj)
#define CkDeletePalette(hobj) CkGraph_DeletePalette(hobj)
#endif /*CKGRAPH_DEBUG*/
#else /*!CKGRAPH_IMP*/
#define CkCreatePen(fnPenStyle,nWidth,crColor) \
        CreatePen(fnPenStyle,nWidth,crColor)
#define CkExtCreatePen(dwPenStyle,dwWidth,lplb,dwStyleCount,lpStyle) \
        ExtCreatePen(dwPenStyle,dwWidth,lplb,dwStyleCount,lpStyle)
#define CkCreateSolidBrush(crColor) CreateSolidBrush(crColor)
#define CkCreatePatternBrush(bitmap) CreatePatternBrush(bitmap)
#define CkCreateDIBitmap(hdc,lpbmih,fdwInit,lpbInit,lpbmi,fuUsage) \
        CreateDIBitmap(hdc,lpbmih,fdwInit,lpbInit,lpbmi,fuUsage)
#define CkCreateBitmap(nWidth,nHeight,cPlanes,cBitsPerPel,lpvBits)\
        CreateBitmap(nWidth,nHeight,cPlanes,cBitsPerPel,lpvBits)
#define CkCreateCompatibleBitmap(hdc,nWidth,nHeight) \
        CreateCompatibleBitmap(hdc,nWidth,nHeight)
#define CkLoadBitmap(hinst,lpszBitmap) \
        LoadBitmap(hinst,lpszBitmap)
#define CkCreateRectRgn(nLeftRect,nTopRect,nRightRect,nBottomRect)\
        CreateRectRgn(nLeftRect,nTopRect,nRightRect,nBottomRect)
#define CkCreateFontIndirect(lplogfont) CreateFontIndirect(lplogfont)
#define CkCreatePalette(lplogpal) CreatePalette(lplogpal)
#define CkGetStockObject(stockobj) GetStockObject(stockobj)
#define CkGetDC(hwnd) GetDC(hwnd)
#define CkReleaseDC(hwnd,hdc) ReleaseDC(hwnd,hdc)
#define CkCreateCompatibleDC(hdc) CreateCompatibleDC(hdc)
#define CkCreateDC(lpszDriver,lpszDevice,lpszOutput,lpInitData)\
        CreateDC(lpszDriver,lpszDevice,lpszOutput,lpInitData)
#define CkDeleteDC(hdc) DeleteDC(hdc)
#define CkBeginPaint(hwnd,lpps) BeginPaint(hwnd,lpps)
#define CkEndPaint(hwnd,lpPaint) EndPaint(hwnd,lpPaint)
#define CkSelectObject(hdc,hobj_in) SelectObject(hdc,hobj_in)
#define CkSelectBrush(hdc,hobj_in) SelectObject(hdc,hobj_in)
#define CkSelectPen(hdc,hobj_in) SelectObject(hdc,hobj_in)
#define CkSelectFont(hdc,hobj_in) SelectObject(hdc,hobj_in)
#define CkSelectBitmap(hdc,hobj_in) SelectObject(hdc,hobj_in)
#define CkCheckOutBitmap(hobj_in) 
#define CkSelectClipRgn(hdc,hrgn) SelectClipRgn(hdc,hrgn)
#define CkOffsetClipRgn(hdc,xoff,yoff) OffsetClipRgn(hdc,xoff,yoff)
#define CkSelectPalette(hdc,hobj_in,f) SelectPalette(hdc,hobj_in,f)
#define CkRealizePalette(hdc) RealizePalette(hdc)
#define CkSetPaletteEntries(hPal,iStart,cEntries,lppe)\
   SetPaletteEntries(hPal,iStart,cEntries,lppe)
#define CkResizePalette(hPal,nEntries) ResizePalette(hPal,nEntries)
#define CkSetROP2(hdc,rop) SetROP2(hdc,rop)
#define CkSetBkMode(hdc,bkmode)      SetBkMode(hdc,bkmode)
#define CkSetBkColor(hdc,bkcolor)    SetBkColor(hdc,bkcolor)
#define CkSetTextColor(hdc,txtcolor) SetTextColor(hdc,txtcolor)
#define CkSetPolyFillMode(hdc,iMode) SetPolyFillMode(hdc,iMode)
#define CkDeleteObject(hobj) DeleteObject(hobj)
#define CkDeleteBrush(hobj) DeleteObject(hobj)
#define CkDeletePen(hobj) DeleteObject(hobj)
#define CkDeleteFont(hobj) DeleteObject(hobj)
#define CkDeleteBitmap(hobj) DeleteObject(hobj)
#define CkDeletePalette(hobj) DeleteObject(hobj)
#endif  /*CKGRAPH_IMP*/

#ifdef PROFILE_INFO
#ifdef CKGRAPH_DEBUG
BOOL CkGraph_BitBlt( HDC hdcDest, int nXDest, int nYDest, int nWidth, int nHeight, HDC hdcSrc, int nXSrc, int nYSrc, DWORD dwRop,char* file,int line);
int CkGraph_FillRect(HDC hDC,CONST RECT *lprc,HBRUSH hbr,char* file,int line);
BOOL CkGraph_Polyline(HDC hdc,CONST POINT *lppt,int cPoints,char* file,int line);
BOOL CkGraph_Polygon(HDC hdc,CONST POINT *lppt,int cPoints,char* file,int line);
BOOL CkGraph_Arc(HDC hdc,int nLeftRect,int nTopRect,int nRightRect,
    int nBottomRect,int nXStartArc,int nYStartArc,int nXEndArc,int nYEndArc,
    char* file,int line);
BOOL CkGraph_Chord(HDC hdc,int nLeftRect,int nTopRect,int nRightRect,
     int nBottomRect,int nXRadial1,int nYRadial1,int nXRadial2,int nYRadial2,
     char* file,int line );
BOOL CkGraph_Pie(HDC hdc,int nLeftRect,int nTopRect,int nRightRect,
    int nBottomRect,int nXRadial1,int nYRadial1,int nXRadial2,int nYRadial2 ,
     char* file,int line);
BOOL CkGraph_Rectangle(HDC hdc,int nLeftRect,int nTopRect,int nRightRect, 
              int nBottomRect,char* file,int line);
BOOL CkGraph_TextOut(HDC hdc,int nXStart, int nYStart, LPCTSTR str, int len,
                      char* file,int line);
BOOL CkGraph_ExtTextOut( HDC hdc, int X, int Y, UINT fuOptions,
    CONST RECT *lprc, LPCTSTR lpString, UINT cbCount, CONST INT *lpDx,
    char* file,int line );

#define CkBitBlt(hdcDest,nXDest,nYDest,nWidth,nHeight,hdcSrc,nXSrc,nYSrc,dwRop)\
 CkGraph_BitBlt(hdcDest,nXDest,nYDest,nWidth,nHeight,hdcSrc,nXSrc,nYSrc,dwRop,__FILE__,__LINE__)
#define CkFillRect(hDC,lprc,hbr) \
        CkGraph_FillRect(hDC,lprc,hbr,__FILE__,__LINE__)
#define CkPolyline(hdc,lppt,cPoints) \
        CkGraph_Polyline(hdc,lppt,cPoints,__FILE__,__LINE__)
#define CkPolygon(hdc,lppt,cPoints) \
        CkGraph_Polygon(hdc,lppt,cPoints,__FILE__,__LINE__)
#define CkArc(hdc,nLeftRect,nTopRect,nRightRect,\
    nBottomRect,nXStartArc,nYStartArc,nXEndArc,nYEndArc) \
   CkGraph_Arc(hdc,nLeftRect,nTopRect,nRightRect,\
    nBottomRect,nXStartArc,nYStartArc,nXEndArc,nYEndArc,__FILE__,__LINE__)
#define CkChord(hdc,nLeftRect,nTopRect,nRightRect,\
     nBottomRect,nXRadial1,nYRadial1,nXRadial2,nYRadial2)\
   CkGraph_Chord(hdc,nLeftRect,nTopRect,nRightRect,\
     nBottomRect,nXRadial1,nYRadial1,nXRadial2,nYRadial2,__FILE__,__LINE__)
#define CkPie(hdc,nLeftRect,nTopRect,nRightRect,\
     nBottomRect,nXRadial1,nYRadial1,nXRadial2,nYRadial2)\
   CkGraph_Pie(hdc,nLeftRect,nTopRect,nRightRect,\
     nBottomRect,nXRadial1,nYRadial1,nXRadial2,nYRadial2,__FILE__,__LINE__)
#define CkRectangle(hdc,nLeftRect,nTopRect,nRightRect, nBottomRect)\
   CkGraph_Rectangle(hdc,nLeftRect,nTopRect,nRightRect,nBottomRect,\
      __FILE__,__LINE__)
#define CkTextOut(hdc,nXStart,nYStart,str,len)\
    CkGraph_TextOut(hdc,nXStart,nYStart,str,len,__FILE__,__LINE__)
#define CkExtTextOut(hdc,X,Y,fuOptions,lprc,lpString,cbCount,lpDx)\
    CkGraph_ExtTextOut(hdc,X,Y,fuOptions,lprc,lpString,cbCount,lpDx,\
      __FILE__,__LINE__)

#else  /*CKGRAPH_DEBUG*/

BOOL CkGraph_BitBlt( HDC hdcDest, int nXDest, int nYDest, int nWidth, int nHeight, HDC hdcSrc, int nXSrc, int nYSrc, DWORD dwRop);
int CkGraph_FillRect(HDC hDC,CONST RECT *lprc,HBRUSH hbr);
BOOL CkGraph_Polyline(HDC hdc,CONST POINT *lppt,int cPoints);
BOOL CkGraph_Polygon(HDC hdc,CONST POINT *lppt,int cPoints);
BOOL CkGraph_Arc(HDC hdc,int nLeftRect,int nTopRect,int nRightRect,
    int nBottomRect,int nXStartArc,int nYStartArc,int nXEndArc,int nYEndArc );
BOOL CkGraph_Chord(HDC hdc,int nLeftRect,int nTopRect,int nRightRect,
     int nBottomRect,int nXRadial1,int nYRadial1,int nXRadial2,int nYRadial2 );
BOOL CkGraph_Pie(HDC hdc,int nLeftRect,int nTopRect,int nRightRect,
    int nBottomRect,int nXRadial1,int nYRadial1,int nXRadial2,int nYRadial2 );
BOOL CkGraph_Rectangle(HDC hdc,int nLeftRect,int nTopRect,int nRightRect, 
              int nBottomRect);
BOOL CkGraph_TextOut(HDC hdc,int nXStart, int nYStart, LPCTSTR str, int len);
BOOL CkGraph_ExtTextOut( HDC hdc, int X, int Y, UINT fuOptions,
    CONST RECT *lprc, LPCTSTR lpString, UINT cbCount, CONST INT *lpDx);

#define CkBitBlt(hdcDest,nXDest,nYDest,nWidth,nHeight,hdcSrc,nXSrc,nYSrc,dwRop)\
 CkGraph_BitBlt(hdcDest,nXDest,nYDest,nWidth,nHeight,hdcSrc,nXSrc,nYSrc,dwRop)
#define CkFillRect(hDC,lprc,hbr) CkGraph_FillRect(hDC,lprc,hbr)
#define CkPolyline(hdc,lppt,cPoints) CkGraph_Polyline(hdc,lppt,cPoints)
#define CkPolygon(hdc,lppt,cPoints) CkGraph_Polygon(hdc,lppt,cPoints)
#define CkArc(hdc,nLeftRect,nTopRect,nRightRect,\
    nBottomRect,nXStartArc,nYStartArc,nXEndArc,nYEndArc) \
   CkGraph_Arc(hdc,nLeftRect,nTopRect,nRightRect,\
    nBottomRect,nXStartArc,nYStartArc,nXEndArc,nYEndArc)
#define CkChord(hdc,nLeftRect,nTopRect,nRightRect,\
     nBottomRect,nXRadial1,nYRadial1,nXRadial2,nYRadial2)\
   CkGraph_Chord(hdc,nLeftRect,nTopRect,nRightRect,\
     nBottomRect,nXRadial1,nYRadial1,nXRadial2,nYRadial2)
#define CkPie(hdc,nLeftRect,nTopRect,nRightRect,\
     nBottomRect,nXRadial1,nYRadial1,nXRadial2,nYRadial2)\
   CkGraph_Pie(hdc,nLeftRect,nTopRect,nRightRect,\
     nBottomRect,nXRadial1,nYRadial1,nXRadial2,nYRadial2)
#define CkRectangle(hdc,nLeftRect,nTopRect,nRightRect, nBottomRect)\
   CkGraph_Rectangle(hdc,nLeftRect,nTopRect,nRightRect,nBottomRect)
#define CkTextOut(hdc,nXStart,nYStart,str,len)\
    CkGraph_TextOut(hdc,nXStart,nYStart,str,len)
#define CkExtTextOut(hdc,X,Y,fuOptions,lprc,lpString,cbCount,lpDx)\
    CkGraph_ExtTextOut(hdc,X,Y,fuOptions,lprc,lpString,cbCount,lpDx)

#endif /*CKGRAPH_DEBUG*/
#else  /*PROFILE_INFO*/

#define CkBitBlt(hdcDest,nXDest,nYDest,nWidth,nHeight,hdcSrc,nXSrc,nYSrc,dwRop)\
   BitBlt(hdcDest,nXDest,nYDest,nWidth,nHeight,hdcSrc,nXSrc,nYSrc,dwRop)
#define CkFillRect(hDC,lprc,hbr) FillRect(hDC,lprc,hbr)
#define CkPolyline(hdc,lppt,cPoints) Polyline(hdc,lppt,cPoints)
#define CkPolygon(hdc,lppt,cPoints)  Polygon(hdc,lppt,cPoints)
#define CkArc(hdc,nLeftRect,nTopRect,nRightRect,\
    nBottomRect,nXStartArc,nYStartArc,nXEndArc,nYEndArc) \
   Arc(hdc,nLeftRect,nTopRect,nRightRect,\
    nBottomRect,nXStartArc,nYStartArc,nXEndArc,nYEndArc)
#define CkChord(hdc,nLeftRect,nTopRect,nRightRect,\
     nBottomRect,nXRadial1,nYRadial1,nXRadial2,nYRadial2)\
   Chord(hdc,nLeftRect,nTopRect,nRightRect,\
     nBottomRect,nXRadial1,nYRadial1,nXRadial2,nYRadial2)
#define CkPie(hdc,nLeftRect,nTopRect,nRightRect,\
     nBottomRect,nXRadial1,nYRadial1,nXRadial2,nYRadial2)\
   Pie(hdc,nLeftRect,nTopRect,nRightRect,\
     nBottomRect,nXRadial1,nYRadial1,nXRadial2,nYRadial2)
#define CkRectangle(hdc,nLeftRect,nTopRect,nRightRect, nBottomRect)\
   Rectangle(hdc,nLeftRect,nTopRect,nRightRect,nBottomRect)
#define CkTextOut(hdc,nXStart,nYStart,str,len)\
    TextOut(hdc,nXStart,nYStart,str,len)
#define CkExtTextOut(hdc,X,Y,fuOptions,lprc,lpString,cbCount,lpDx)\
    ExtTextOut(hdc,X,Y,fuOptions,lprc,lpString,cbCount,lpDx)

#endif /*PROFILE_INFO*/


#endif  /*__CKGRAPH_H*/
