/*
 * tkWinGdi.c --
 *
 *	implements checking of gdicalls and optimizing SelectObjects
 *
 * Copyright (c) Brueckner & Jarosch Ing.GmbH,Erfurt,Germany 1997
 * hacked by Leo
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */
#ifdef WTK
#include <windows.h>
#pragma hdrstop
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "testwver.h"
#include "panic.h"
#include "ckalloc.h"
#include "dprintf.h"
#include "ckgraph.h"
#define  gd_alloc ckalloc
#define  gd_free ckfree
#else
#include "tkWinInt.h"
#define  gd_alloc malloc
#define  gd_free free
#endif


#ifdef CKGRAPH_IMP

#define CKOBJ_UNKNOWN         0
#define CKOBJ_PEN             OBJ_PEN
#define CKOBJ_BRUSH           OBJ_BRUSH
#define CKOBJ_DC              OBJ_DC
#define CKOBJ_METADC          OBJ_METADC
#define CKOBJ_PAL             OBJ_PAL
#define CKOBJ_FONT            OBJ_FONT
#define CKOBJ_BITMAP          OBJ_BITMAP
#define CKOBJ_REGION          OBJ_REGION
#define CKOBJ_METAFILE        OBJ_METAFILE
#define CKOBJ_MEMDC           OBJ_MEMDC
#define CKOBJ_EXTPEN          OBJ_EXTPEN
#define CKOBJ_ENHMETADC       OBJ_ENHMETADC
#define CKOBJ_ENHMETAFILE     OBJ_ENHMETAFILE

#ifndef WTK
static int  Wtk_test_win32s(void){
//  return tkpIsWin32s;
  return 0;
}
#endif
/*Stock Objects, must be initialized first!*/
static HPEN     stock_NULL_PEN=NULL;
static HPEN     stock_WHITE_PEN=NULL;
static HPEN     stock_BLACK_PEN=NULL;
static HBRUSH   stock_NULL_BRUSH=NULL;
static HBRUSH   stock_WHITE_BRUSH=NULL;
static HBRUSH   stock_BLACK_BRUSH=NULL;
static HBRUSH   stock_GRAY_BRUSH=NULL;
static HBRUSH   stock_DKGRAY_BRUSH=NULL;
static HBRUSH   stock_LTGRAY_BRUSH=NULL;
static HFONT    stock_SYSTEM_FONT=NULL;
static HFONT    stock_OEM_FIXED_FONT=NULL;
static HFONT    stock_SYSTEM_FIXED_FONT=NULL;
static HFONT    stock_ANSI_VAR_FONT=NULL;
static HFONT    stock_ANSI_FIXED_FONT=NULL;
static HFONT    stock_DEVICE_DEFAULT_FONT=NULL;
static HPALETTE stock_DEFAULT_PALETTE=NULL;

/*should be the first function called in this package*/
void CkGraph_Init(HINSTANCE hInstance){
  stock_NULL_PEN=GetStockObject(NULL_PEN);
  stock_WHITE_PEN=GetStockObject(WHITE_PEN);
  stock_BLACK_PEN=GetStockObject(BLACK_PEN);
  stock_NULL_BRUSH=GetStockObject(NULL_BRUSH);
  stock_WHITE_BRUSH=GetStockObject(WHITE_BRUSH);
  stock_BLACK_BRUSH=GetStockObject(BLACK_BRUSH);
  stock_GRAY_BRUSH=GetStockObject(GRAY_BRUSH);
  stock_DKGRAY_BRUSH=GetStockObject(DKGRAY_BRUSH);
  stock_LTGRAY_BRUSH=GetStockObject(LTGRAY_BRUSH);
  stock_SYSTEM_FONT=GetStockObject(SYSTEM_FONT);
  stock_OEM_FIXED_FONT=GetStockObject(OEM_FIXED_FONT);
  stock_SYSTEM_FIXED_FONT=GetStockObject(SYSTEM_FIXED_FONT);
  stock_ANSI_VAR_FONT=GetStockObject(ANSI_VAR_FONT);
  stock_ANSI_FIXED_FONT=GetStockObject(ANSI_FIXED_FONT);
  stock_DEVICE_DEFAULT_FONT=GetStockObject(DEVICE_DEFAULT_FONT);
  stock_DEFAULT_PALETTE=GetStockObject(DEFAULT_PALETTE);
}
static int is_stockobject(int type,HGDIOBJ hobj){
  switch(type){
    case CKOBJ_PEN:   if(stock_NULL_PEN==hobj)            return 1;
                      if(stock_WHITE_PEN==hobj)           return 1;
                      if(stock_BLACK_PEN==hobj)           return 1;
                      break;
    case CKOBJ_BRUSH: if(stock_NULL_BRUSH==hobj)          return 1;
                      if(stock_WHITE_BRUSH==hobj)         return 1;
                      if(stock_BLACK_BRUSH==hobj)         return 1;
                      if(stock_GRAY_BRUSH==hobj)          return 1;
                      if(stock_DKGRAY_BRUSH==hobj)        return 1;
                      if(stock_LTGRAY_BRUSH==hobj)        return 1;
                      break;
    case CKOBJ_FONT:  if(stock_SYSTEM_FONT==hobj)         return 1;
                      if(stock_OEM_FIXED_FONT==hobj)      return 1;
                      if(stock_SYSTEM_FIXED_FONT==hobj)   return 1;
                      if(stock_ANSI_VAR_FONT==hobj)       return 1;
                      if(stock_ANSI_FIXED_FONT==hobj)     return 1;
                      if(stock_DEVICE_DEFAULT_FONT==hobj) return 1;
                      break;
    case CKOBJ_PAL:   if(stock_DEFAULT_PALETTE==hobj)      return 1;
                      break;
  }
  return 0;
}
static char* obj_type(int type){
  switch (type){
    case CKOBJ_UNKNOWN:     return "UNKNOWN";
    case CKOBJ_PEN:         return "PEN";
    case CKOBJ_BRUSH:       return "BRUSH";
    case CKOBJ_DC:          return "DC";
    case CKOBJ_METADC:      return "METADC";
    case CKOBJ_PAL:         return "PAL";
    case CKOBJ_FONT:        return "FONT";
    case CKOBJ_BITMAP:      return "BITMAP";
    case CKOBJ_REGION:      return "REGION";
    case CKOBJ_METAFILE:    return "METAFILE";
    case CKOBJ_MEMDC:       return "MEMDC";
    case CKOBJ_EXTPEN:      return "EXTPEN";
    case CKOBJ_ENHMETADC:   return "ENHMETADC";
    case CKOBJ_ENHMETAFILE: return "ENHMETAFILE";
  }
  return "UNKNOWN";
}

#ifdef CKGRAPH_DEBUG
int tkWinGdi_verbose=1;
int tkWinGdi_CreatePen;
int tkWinGdi_ExtCreatePen;
int tkWinGdi_CreateSolidBrush;
int tkWinGdi_CreatePatternBrush;
int tkWinGdi_CreateDIBitmap;
int tkWinGdi_CreateCompatibleBitmap;
int tkWinGdi_LoadBitmap;
int tkWinGdi_CreateBitmap;
int tkWinGdi_CreateRectRgn;
int tkWinGdi_CreateFont;
int tkWinGdi_CreateFontIndirect;
int tkWinGdi_CreatePalette;
int tkWinGdi_GetDC;
int tkWinGdi_CreateDC;
int tkWinGdi_ReleaseDC;
int tkWinGdi_CreateCompatibleDC;
int tkWinGdi_DeleteDC;
int tkWinGdi_BeginPaint;
int tkWinGdi_EndPaint;
int tkWinGdi_SelectObject;
int tkWinGdi_SelectBitmap;
int tkWinGdi_SelectPen;
int tkWinGdi_SelectBrush;
int tkWinGdi_SelectPalette;
int tkWinGdi_SelectFont;
int tkWinGdi_DeleteObject;
int tkWinGdi_DeleteBrush;
int tkWinGdi_DeletePen;
int tkWinGdi_DeleteFont;
int tkWinGdi_DeleteBitmap;
int tkWinGdi_DeletePalette;
int tkWinGdi_RealizePalette;
int tkWinGdi_SetROP2;
int tkWinGdi_SetBkMode;
int tkWinGdi_SetBkColor;
int tkWinGdi_SetTextColor;
int tkWinGdi_SelectClipRgn;
int tkWinGdi_OffsetClipRgn;
int tkWinGdi_FillRect;
int tkWinGdi_Polyline;
int tkWinGdi_Polygon;
int tkWinGdi_Arc;
int tkWinGdi_Chord;
int tkWinGdi_Pie;
int tkWinGdi_Rectangle;
int tkWinGdi_TextOut;
int tkWinGdi_ExtTextOut;
int tkWinGdi_SetPolyFillMode;
int tkWinGdi_SetPaletteEntries;
int tkWinGdi_ResizePalette;
int tkWinGdi_BitBlt;

#ifdef CKGRAPH_TRACE
static int dotracing=0;
static int tracefirst=0;
static char tracefile[255];
void  CkGraph_SetTracing(int trace){
  dotracing=trace;
}
int   CkGraph_GetTracing(void){
  return dotracing;
}
char* CkGraph_GetTraceFile(void){
  return tracefile;
}
static void openfirst(void){
  int i;
  tracefirst=1;
  for(i=0;i<100;i++){
     FILE* f;
     sprintf(tracefile,"tgdi%d.log",i);
     if((f=fopen(tracefile,"r"))==NULL){
       break;
     } else {
       fclose(f);
     }
  }
}
void gdiprintf(const char *format, ...) {
  va_list argList;
  FILE* ftrace;
  if(!dotracing)
    return;
  if(!tracefirst){
     openfirst();
  }
  ftrace=fopen(tracefile,"a");
  if(ftrace!=NULL){
    va_start(argList,format);
    (void) vfprintf(ftrace,format,argList);
    va_end(argList);
    fclose(ftrace);
  }
}
#else
static int dummytrace;
void  CkGraph_SetTracing(int trace){
  dummytrace=trace;
}
int   CkGraph_GetTracing(void){
  return 0;
}
char* CkGraph_GetTraceFile(void){
  return "";
}
#endif
#endif
static int convert_gdiobjtype(int wingditype){
  int type;
  switch (wingditype){
    case 0:               type=CKOBJ_UNKNOWN;break;
    case OBJ_BITMAP:      type=CKOBJ_BITMAP;break;
    case OBJ_BRUSH:       type=CKOBJ_BRUSH;break;
    case OBJ_FONT:        type=CKOBJ_FONT;break;
    case OBJ_PAL:         type=CKOBJ_PAL;break;
    case OBJ_PEN:         type=CKOBJ_PEN;break;
    case OBJ_EXTPEN:      type=CKOBJ_EXTPEN;break;
    case OBJ_REGION:      type=CKOBJ_REGION;break;
    case OBJ_DC:          type=CKOBJ_DC;break;
    case OBJ_MEMDC:       type=CKOBJ_MEMDC;break;
    case OBJ_METAFILE:    type=CKOBJ_METAFILE;break;
    case OBJ_METADC:      type=CKOBJ_METADC;break;
    case OBJ_ENHMETAFILE: type=CKOBJ_ENHMETAFILE;break;
    case OBJ_ENHMETADC:   type=CKOBJ_ENHMETADC;break;
    default:              type=CKOBJ_UNKNOWN;break;
  }
  return type;
}
#ifdef WTK
typedef char* ClientData;
#endif

/*
 * define's a linked list of CompatibleDC's , if a DC was added to
 * this list it never dies upon program termination
 *
 */
typedef struct _CompatDC {
  HDC hdc;
#ifdef CKGRAPH_DEBUG
  HBITMAP hbitmap;
#endif
  int used;
  struct _CompatDC* next;
} CompatDC;
/*
 * listhead
 */
TCL_DECLARE_MUTEX(dcMutex)
static CompatDC* compatDCList=NULL;
static CompatDC* currcompat=NULL;
/*
 * number of DC's in the list
 */
static int xhdcs=0;
/*
 *----------------------------------------------------------------------
 * AddCompatDC --
 *	Adds a CompatibleDC to our list of known CompatibleDC's.
 *----------------------------------------------------------------------
 */
static void AddCompatDC(HDC hdc) {
    CompatDC *hdcElem;

    hdcElem = (CompatDC *) gd_alloc(sizeof(CompatDC));
    Tcl_MutexLock(&dcMutex);
    hdcElem->next = compatDCList;
    hdcElem->hdc = hdc;
#ifdef CKGRAPH_DEBUG
    hdcElem->hbitmap=(HBITMAP)0;
#endif
    hdcElem->used=1;
    currcompat = compatDCList = hdcElem;
    xhdcs++;
    Tcl_MutexUnlock(&dcMutex);
}
/*
 *----------------------------------------------------------------------
 * RemoveCompatDC --
 *	Removes a CompatibleDC from our list of known CompatibleDC's
 *----------------------------------------------------------------------
 */
static void RemoveCompatDC(HDC hdc) {
  CompatDC *prev, *curr, *next;
  Tcl_MutexLock(&dcMutex);
  for (prev = NULL, curr = compatDCList; curr != NULL; prev = curr, curr = next)  {
    next = curr->next;
    if (curr->hdc == hdc) {
      if (prev != NULL) {
        prev->next = curr->next;
      } else {
        compatDCList = curr->next;
      }
      xhdcs--;
      if(xhdcs<0)
        panic("xhdcs<0 in RemoveCompatDC");
      if(currcompat==curr)
        currcompat=NULL;
      Tcl_MutexUnlock(&dcMutex);
      gd_free((char *) curr);
      return;
    }
  }
  Tcl_MutexUnlock(&dcMutex);
}
#define FINDFREECOMPAT() \
  (currcompat!=NULL &&  currcompat->used==0 ) ? currcompat : \
                                          FindFreeCompatDC()
/*
 *----------------------------------------------------------------------
 * FindFreeCompatDC --
 *	Finds an unused CompatibleDC
 *----------------------------------------------------------------------
 */
static CompatDC* FindFreeCompatDC(void) {
    CompatDC  *curr;
    Tcl_MutexLock(&dcMutex);
    for (curr = compatDCList; curr != NULL; curr = curr->next) {
      if(curr->used==0) {
        Tcl_MutexUnlock(&dcMutex);
        return currcompat=curr;
      }
    }
    Tcl_MutexUnlock(&dcMutex);
    return 0;
}
#define FINDCOMPAT(_hdc) \
  (currcompat!=NULL && currcompat->hdc==(_hdc) ) ? currcompat : FindCompatDC(_hdc)
/*
 *----------------------------------------------------------------------
 * FindCompatDC --
 *	Searches a given CompatibleDC in the List of CompatibleDC's
 *----------------------------------------------------------------------
 */
static CompatDC* FindCompatDC(HDC hdc) {
  CompatDC *curr;
  for (curr = compatDCList; curr != NULL; curr = curr->next) {
    if (curr->hdc == hdc) {
      return currcompat=curr;
    }
  }
  return NULL;
}
/*
 *----------------------------------------------------------------------
 * CkGraph_GetHashedDC --
 *	searches a free element in the list, if nothing is found, creates
 *      a new CompatibleDC and adds it to the list
 *----------------------------------------------------------------------
 */
static int clearTextColor=0x0;
static int clearBkColor=0xffffff;
static int clearBkMode=OPAQUE;
HDC CkGraph_GetHashedDC(void){
   CompatDC* pe=FINDFREECOMPAT();
   if(!pe){
     static int first=1;
     HDC hdc=CkCreateCompatibleDC(NULL);
     if(!hdc)
       panic("No compatDC in CkGraph_GetHashedDC");
     if(first){
       first=0;
         clearTextColor=GetTextColor(hdc);
         clearBkColor=GetBkColor(hdc);
         clearBkMode=GetBkMode(hdc);
     }
     AddCompatDC( hdc);
     return hdc;
   }
   pe->used=1;
   return pe->hdc;
};
/*
 *----------------------------------------------------------------------
 * CkGraph_ReleaseHashedDC --
 *	releases a list element, the CompatibleDC remains allocated
 *----------------------------------------------------------------------
 */
void CkGraph_ReleaseHashedDC(HDC hdc){
  CompatDC* pe;
  //if(compatDCList==NULL)
  //  return;
  if((pe=FINDCOMPAT(hdc))==NULL){
    panic("Could not find a hdc in CkGraph_ReleaseHashedDC");
  }
  pe->used=0;
}
/*
 *----------------------------------------------------------------------
 * CkGraph_FreeHashedDCs --
 *	frees all allocated DCs during termination of the program
 *----------------------------------------------------------------------
 */
void CkGraph_FreeHashedDCs(void) {
  while(compatDCList!=NULL){
    HDC hdc=compatDCList->hdc;
    RemoveCompatDC(hdc);
    if(hdc)
      CkDeleteDC(hdc);
  }
}
/*
 *----------------------------------------------------------------------
 * CkGraph_ClearDC --
 *	reset the values in a DC with the default values of a freshly
 *      created memory DC
 *----------------------------------------------------------------------
 */
void CkGraph_ClearDC(HDC dc){
    CkSetBkMode(dc,clearBkMode);
    CkSetTextColor(dc,clearTextColor);
    CkSetBkColor(dc,clearBkColor);
}
#ifdef CKGRAPH_DEBUG
void CkGraph_CheckSelectedBitmap(HDC hdc,HBITMAP hbitmap){
  CompatDC *curr;
  CompatDC *found=NULL;
  for (curr = compatDCList; curr != NULL; curr = curr->next) {
    if (curr->hdc==hdc){
      found=curr;
    }
    if (curr->used && curr->hbitmap == hbitmap && curr->hdc!=hdc ) {
      dprintf("AddSelBitmap: hdc 0x%x already has bitmap 0x%x selected,"
               "during setting of 0x%x\n",
               curr->hdc,curr->hbitmap,hdc);
    }
  }
  if(found!=NULL)
    found->hbitmap=hbitmap;
}
#endif

typedef struct _PixElem {
  HBITMAP pixmap;
  int width;
  int height;
  int planes;
  int depth;
  int used;
  int delete_after_use;
  struct _PixElem* next;
} PixElem;
TCL_DECLARE_MUTEX(pixMutex)
static PixElem* pixmapList=NULL;
static PixElem* currpix=NULL;
int xpixmaps=0;

static void DeletePixmap(HBITMAP pixmap) {
  if (pixmap != NULL)
    CkDeleteBitmap(pixmap);
}
static HBITMAP NewPixmap(int width,int height,int planes,int depth) {
  return CkCreateBitmap(width, height, planes, depth, NULL);
}

/*
 *----------------------------------------------------------------------
 * AddPixmap --
 *	Add a pixmap to our list of known pixmaps.  
 *----------------------------------------------------------------------
 */
static void AddPixmap( HBITMAP pixmap,int width ,int height,int planes,int depth,int delete_after_use) {
    PixElem *pixElem;

    pixElem = (PixElem *) gd_alloc(sizeof(PixElem));
    Tcl_MutexLock(&pixMutex);
    pixElem->next = pixmapList;
    pixElem->pixmap = pixmap;
    pixElem->width = width;
    pixElem->height = height;
    pixElem->planes = planes;
    pixElem->depth = depth;
    pixElem->used=1;
    pixElem->delete_after_use=delete_after_use;
    currpix=pixmapList = pixElem;
    xpixmaps++;
    Tcl_MutexUnlock(&pixMutex);
}

/*
 *----------------------------------------------------------------------
 * RemovePixmap --
 *	Removes a pixmap from our list of known pixmaps
 *----------------------------------------------------------------------
 */
static void RemovePixmap(HBITMAP pixmap) {
  PixElem *prev, *curr, *next;
  Tcl_MutexLock(&pixMutex);
  for (prev = NULL, curr = pixmapList; curr != NULL;
  prev = curr, curr = next)  {
    next = curr->next;
    if (curr->pixmap == pixmap) {
      if (prev != NULL) {
        prev->next = curr->next;
      } else {
        pixmapList = curr->next;
      }
      xpixmaps--;
      if(xpixmaps<0)
        panic("xpixmaps<0 in RemovePixmap");
      if(currpix==curr)
        currpix=NULL;
      Tcl_MutexUnlock(&pixMutex);
      gd_free((char *) curr);
      return;
    }
  }
  Tcl_MutexUnlock(&pixMutex);
}
#define FINDFREEPIXMAP() \
  (currpix!=NULL && currpix->used==0 ) ? currpix : \
                                                      FindFreePixmap()
/*
 *----------------------------------------------------------------------
 * FindFreePixmap --
 *	Finds an unused HBITMAP
 *----------------------------------------------------------------------
 */
static PixElem* FindFreePixmap(void)
{
    PixElem  *curr;
    Tcl_MutexLock(&pixMutex);
    for (curr = pixmapList; curr != NULL; curr = curr->next) {
      if(curr->used==0) {
        Tcl_MutexUnlock(&pixMutex);
        return currpix=curr;
      }
    }
    Tcl_MutexUnlock(&pixMutex);
    return NULL;
}
#define FINDPIXMAP(_pixmap) \
  (currpix && currpix->pixmap==(_pixmap)) ? currpix : FindPixmap(_pixmap)
/*
 *----------------------------------------------------------------------
 * FindPixmap --
 *	Finds a HBITMAP in the Lists of Pixmaps
 *----------------------------------------------------------------------
 */
static PixElem* FindPixmap(HBITMAP pixmap)
{
  PixElem *curr;
  Tcl_MutexLock(&pixMutex);
  for (curr = pixmapList; curr != NULL; curr = curr->next) {
    if (curr->pixmap == pixmap) {
      Tcl_MutexUnlock(&pixMutex);
      return currpix=curr;
    }
  }
  Tcl_MutexUnlock(&pixMutex);
  return NULL;
}
/*                                                                              
 *----------------------------------------------------------------------        
 * CkGraph_GetHashedBitmap --  
 *      searches a free element in the list, if nothing is found, creates       
 *      a new Pixmap and adds it to the list                              
 *----------------------------------------------------------------------        
 */      
HBITMAP CkGraph_GetHashedBitmap( int width, int height,int planes,int depth){
   PixElem* pe=FINDFREEPIXMAP();
   if(!pe){
     HBITMAP pixmap=NewPixmap(width,height,planes,depth);
     AddPixmap(pixmap,width,height,planes,depth,0);
     return pixmap;
   }
   pe->used=1;
   if( (pe->width < width) || (pe->height < height) || 
       (pe->planes != planes) || (pe->depth != depth) ){
     DeletePixmap(pe->pixmap);
     if(pe->planes==planes && pe->depth==depth){
       width =max(width, pe->width);
       height=max(height,pe->height);
     } else {
       //dprintf("pe->planes:%d planes:%d,pe->depth:%d depth:%d\n",
       //         pe->planes,planes,pe->depth,depth);
     }
     pe->pixmap=NewPixmap(width,height,planes,depth);
     pe->width=width;
     pe->height=height;
     pe->planes=planes;
     pe->depth=depth;
   }
   return pe->pixmap;
}
/*                                                                              
 *----------------------------------------------------------------------        
 * CkGraph_ReleaseHashedBitmap --     
 *      releases a list element, the Pixmap remains allocated,             
 *      if delete_after_use is 0
 *----------------------------------------------------------------------        
 */   
void CkGraph_ReleaseHashedBitmap(HBITMAP pixmap){
  PixElem* pe;
  //if(pixmapList==NULL)
  //  return;
  if((pe=FINDPIXMAP(pixmap))==NULL){
    panic("Could not ReleaseHashedBitmap");
  }
  pe->used=0;
  if(pe->delete_after_use){
    RemovePixmap(pixmap);
    DeletePixmap(pixmap);
  }
}
/*                                                                              
 *----------------------------------------------------------------------        
 * CkGraph_FreeHashedBitmaps -- 
 *      frees all allocated Pixmaps during termination of the program 
 *----------------------------------------------------------------------        
 */    
void CkGraph_FreeHashedBitmaps(void)
{
  while(pixmapList!=NULL){
    HBITMAP pixmap=pixmapList->pixmap;
    RemovePixmap(pixmap);
    DeletePixmap(pixmap);
  }
  /*PixElem *curr;
  for (curr = pixmapList; curr != NULL;) {
    PixElem* temp=curr;
    HBITMAP pixmap=temp->pixmap;
    curr=curr->next;
    RemovePixmap(pixmap);
    DeletePixmap(pixmap);
  }*/
}
/*
 *----------------------------------------------------------------------
 * CkGraph_IsPixmap --
 *	Returns TRUE if the drawable is a pixmap.
 *----------------------------------------------------------------------
 */
int CkGraph_IsPixmap(HANDLE drawable){
  if((FINDPIXMAP(drawable))!=NULL)
    return TRUE;
  else
    return FALSE;
}

#ifdef CKGRAPH_DEBUG
/*
 * in this structure the handle of an gdi allocated is stored
 */
typedef struct GdiObj {
  char              *file;
  int                line;
  ClientData         handle;
  int                type;
  struct GdiObj *next;
  struct GdiObj *prev;
} GdiObj;
/* 
 * in this List all gdi-allocated objects are stored
 */
TCL_DECLARE_MUTEX(objMutex)
static GdiObj *gdiObjlistPtr = (GdiObj*)0;  

static void CkGraphCreateObject(int type,ClientData handle,char* file,int line){
  GdiObj* result = (GdiObj *)gd_alloc(sizeof(GdiObj));
  Tcl_MutexLock(&objMutex);
  result->file   = file;
  result->line   = line;
  result->handle = handle;
  result->type   = type;
  result->next  = gdiObjlistPtr;
  result->prev  = NULL;
  if (gdiObjlistPtr != NULL)
    gdiObjlistPtr->prev = result;
  gdiObjlistPtr = result;
  Tcl_MutexUnlock(&objMutex);
}
static GdiObj* CkGraphFindObject(int type,ClientData handle){
   GdiObj* objPtr;
   Tcl_MutexLock(&objMutex);
   for (objPtr = gdiObjlistPtr ;objPtr;objPtr=objPtr->next)
     if((objPtr->type==type && objPtr->handle==handle)||
        (objPtr->type==CKOBJ_UNKNOWN && objPtr->handle==handle)) {
       Tcl_MutexUnlock(&objMutex);
       return objPtr;
   }
   Tcl_MutexUnlock(&objMutex);
   return (GdiObj*)0;
}
static void CkGraphDeleteObject(int type,ClientData handle,char* file,int line){
  GdiObj* objPtr;
  Tcl_MutexLock(&objMutex);
  if((objPtr=CkGraphFindObject(type,handle))==(GdiObj*)0){
    if (!is_stockobject(type,(HGDIOBJ)handle)) {
      dprintf("Could not find %s %8lx %s %d for delete\n",
        obj_type(type),handle,file,line);
      CkGraph_DumpActiveObjects(NULL);
    }
    return;
  }
  if (objPtr->next)
      objPtr->next->prev = objPtr->prev;
  if (objPtr->prev)
      objPtr->prev->next = objPtr->next;
  if (gdiObjlistPtr == objPtr)
      gdiObjlistPtr = objPtr->next;
  Tcl_MutexUnlock(&objMutex);
  gd_free((char *) objPtr);
}
static int isDC (int type){
  switch(type){
    case CKOBJ_DC:
    case CKOBJ_MEMDC:
    case CKOBJ_METAFILE:
    case CKOBJ_METADC:
    case CKOBJ_ENHMETAFILE:
    case CKOBJ_ENHMETADC:
    return 1;
  }
  return 0;
}
#endif /*CKGRAPH_DEBUG*/

/*
 * per gdi-HDC there are all selected handles held in this structure
 * it enables the control over the selecting and destroying of objects
 */  
#define MAXGDIOBJ 15
typedef struct GdiContext {
  int type; //type of DC can be:
    //CKOBJ_DC;
    //CKOBJ_MEMDC
    //CKOBJ_METAFILE
    //CKOBJ_METADC
    //CKOBJ_ENHMETAFILE
    //CKOBJ_ENHMETADC
  HDC hdc;
  HGDIOBJ hobjs[MAXGDIOBJ]; //selectable objects
  HBITMAP defaultbitmap;
  //HPALETTE defaultpalette;
  int palettevalid;
  int rop;
  int bkmode;
  int fillmode;
  COLORREF bkcolor;
  COLORREF textcolor;
  HRGN hrgn;
  int  hrgntype;
  int  hrgn_xoff;
  int  hrgn_yoff;
  struct GdiContext *next;
  struct GdiContext *prev;
} GdiContext;
static GdiContext *devHead = (GdiContext*)0;  /* List of allocated dcs */
static GdiContext *currdc = (GdiContext*)0;  /* hashed dc */

static GdiContext* CkGraphCreateDeviceContext(int type,HDC hdc){
  GdiContext* result = (GdiContext *)gd_alloc(sizeof(GdiContext));
  memset(result,0,sizeof(GdiContext));
  result->type  = type;
  result->hdc   = hdc;
  result->textcolor   = CLR_INVALID;
  result->bkcolor     = CLR_INVALID;
  result->hrgn        = NULL;
  result->hrgntype    = NULLREGION;
  result->next  = devHead;
  result->prev  = (GdiContext*)0;
  if (devHead)
    devHead->prev = result;
  currdc = devHead = result;
  return result;
}
#define FINDCONTEXT(_hdc) \
  (currdc && currdc->hdc==(_hdc) ) ? currdc : CkGraphFindDeviceContext(_hdc)
static GdiContext* CkGraphFindDeviceContext(HDC hdc){
   GdiContext* devPtr;
   //if(currdc && currdc->hdc==hdc)
   //  return currdc;
   for (devPtr = devHead ;devPtr;devPtr=devPtr->next)
     if(devPtr->hdc==hdc){
       return currdc=devPtr;
   }
#ifdef CKGRAPH_DEBUG
   dprintf("Could not find a context for hdc 0x%x\n",hdc);
#endif
   return (GdiContext*)0;
}
static void CkGraphDeleteDeviceContext(HDC hdc){
  GdiContext* devPtr=FINDCONTEXT(hdc);
  if(devPtr==(GdiContext*)0){
#ifdef CKGRAPH_DEBUG
    dprintf("Could not find 0x%x for delete\n",hdc);
#endif
    return;
  }
  if(currdc==devPtr)
    currdc=(GdiContext*)0;
  if (devPtr->next)
      devPtr->next->prev = devPtr->prev;
  if (devPtr->prev)
      devPtr->prev->next = devPtr->next;
  if (devHead == devPtr)
      devHead = devPtr->next;
  gd_free((char *) devPtr);
}

void CkGraph_RegisterDeviceContext(HDC hdc) {
  GdiContext* devPtr=CkGraphCreateDeviceContext(CKOBJ_DC,hdc);
  devPtr->textcolor=GetTextColor(hdc);
  devPtr->bkcolor=GetBkColor(hdc);
  devPtr->bkmode=GetBkMode(hdc);
  devPtr->rop=GetROP2(hdc);
  devPtr->fillmode=GetPolyFillMode(hdc);
}
void CkGraph_UnRegisterDeviceContext(HDC hdc) {
  CkGraphDeleteDeviceContext(hdc);
}

/*
 * checks if the GdiContext structures represent the actual state
 * in the DC's and prints out the state
 */
typedef struct _SelectTest {
  int type;
  HGDIOBJ defobj;
} SelectTest;

void CkGraph_CheckDCs( char* fileName ) {
  GdiContext* devPtr;
  for (devPtr=devHead;devPtr!=NULL;devPtr=devPtr->next) {
    int i;
#define OBJCOUNT 6
    SelectTest selTest[OBJCOUNT]; 
    selTest[0].type=                CKOBJ_PEN ;
    selTest[0].defobj=(HGDIOBJ)stock_NULL_PEN;
    selTest[1].type=                CKOBJ_BRUSH ;
    selTest[1].defobj=(HGDIOBJ)stock_NULL_BRUSH;
    selTest[2].type=                       CKOBJ_PAL ;
    selTest[2].defobj=(HGDIOBJ)stock_DEFAULT_PALETTE;
    selTest[3].type=                  CKOBJ_FONT ;
    selTest[3].defobj=(HGDIOBJ)stock_SYSTEM_FONT;
    selTest[4].type=    CKOBJ_REGION ;
    selTest[4].defobj=(HGDIOBJ)NULL;
    selTest[5].type=                  CKOBJ_BITMAP ;
    selTest[5].defobj=(HGDIOBJ)devPtr->defaultbitmap;
    dprintf("%-12.12s 0x%8.8x\n",obj_type(devPtr->type),(int)devPtr->hdc);
    for (i=0;i<OBJCOUNT;i++) {
      HGDIOBJ hsel;
      if(selTest[i].type==CKOBJ_PAL) {
        hsel=(HGDIOBJ)SelectPalette(devPtr->hdc,selTest[i].defobj,TRUE);
      } else {
        hsel=SelectObject(devPtr->hdc,selTest[i].defobj);
      }
      dprintf("  %-12.12s 0x%8.8x %s\n",obj_type(selTest[i].type),hsel,
               is_stockobject(selTest[i].type,hsel)?"stockobj":"");
      if(devPtr->hobjs[selTest[i].type]!=0 &&
           hsel!=devPtr->hobjs[selTest[i].type]) {
        /*
         * the actual selected object is not the one we expect
         */
        dprintf("    %-12.12s 0x%8.8x was expected\n","ERROR",
                         devPtr->hobjs[selTest[i].type]);
#ifdef CKGRAPH_DEBUG
        /*
         * look if the current object is owned by us
         */
         if(CkGraphFindObject(selTest[i].type,(ClientData)hsel)==NULL) {
            dprintf("    %-12.12s 0x%8.8x isn't owned by us\n","ERROR",
                         hsel);
         }
#endif
       }
       /* select back */
       if(selTest[i].type==CKOBJ_PAL) {
         SelectPalette(devPtr->hdc,hsel,TRUE);
       } else {
         SelectObject(devPtr->hdc,hsel);
       }
     }
  }
}

#ifdef CKGRAPH_DEBUG

int CkGraph_DumpActiveObjects (char* fileName)
{
    FILE              *fileP;
    GdiObj *objPtr;
    int i=0;
    if(fileName)
      if((fileP = fopen (fileName, "a"))==(FILE*)0)
        return 0;
    /*go to the end*/
    for (objPtr = gdiObjlistPtr; objPtr; objPtr = objPtr->next)
      if(!objPtr->next)
        break;
    for (; objPtr ; objPtr=objPtr->prev,i++) {
      if(!fileName)  {
        dprintf ("%d:%-12.12s %8lx %s %d\n",i,
            obj_type(objPtr->type),objPtr->handle,objPtr->file,objPtr->line);
        if(isDC(objPtr->type)) {
          GdiContext* devhead=FINDCONTEXT((HDC)objPtr->handle);
          if (devhead!=NULL){
            dprintf("   defbitmap=0x%x\n",devhead->defaultbitmap);
          }
        }
      } else {
        fprintf (fileP, "%-12.12s 0x%x %s %d\n",
         obj_type(objPtr->type),objPtr->handle,objPtr->file,objPtr->line);
      }
    }
    if(fileName)
      fclose (fileP);
    return 1;
}
//if debugging is off Windows-CreatePen a.s.o is used
HPEN CkGraph_CreatePen(int fnPenStyle,int nWidth,COLORREF  crColor,char* file,int line){
  HPEN hpen;
  if((hpen=CreatePen(fnPenStyle,nWidth,crColor))==(HPEN)0){
    dprintf("CkCreatePen failed in %s %d\n",file,line);
    return (HPEN)0;
  }
  tkWinGdi_CreatePen++;
  GTRACE(("%d:CreatePen(..,%d,%0x%x) return 0x%x in %s %d\n",tkWinGdi_CreatePen,nWidth,crColor,hpen,file,line);)
  CkGraphCreateObject(CKOBJ_PEN,(ClientData)hpen,file,line);
  return hpen;
}
HPEN CkGraph_ExtCreatePen(DWORD dwPenStyle,DWORD dwWidth,CONST LOGBRUSH* lplb,
                          DWORD dwStyleCount, CONST DWORD* lpStyle,
                          char* file,int line){
  HPEN hpen;
  if((hpen=ExtCreatePen(dwPenStyle,dwWidth,lplb,dwStyleCount,lpStyle))==(HPEN)0){
    dprintf("CkExtCreatePen failed in %s %d\n",file,line);
    return (HPEN)0;
  }
  /* do create a PEN instead a EXTPEN , cause it's used in the same meaning*/
  CkGraphCreateObject(CKOBJ_PEN,(ClientData)hpen,file,line);
  tkWinGdi_ExtCreatePen++;
  GTRACE(("%d:ExtCreatePen(..,%d,%0x%x) return 0x%x in %s %d\n",tkWinGdi_ExtCreatePen,dwWidth,lplb->lbColor,hpen,file,line);)
  return hpen;
}
HBRUSH CkGraph_CreateSolidBrush(COLORREF crColor,char* file,int line){
  HBRUSH hbrush;
  if((hbrush=CreateSolidBrush(crColor))==(HBRUSH)0){
    dprintf("CkCreateSolidBrush failed in %s %d\n",file,line);
    return (HBRUSH)0;
  }
  CkGraphCreateObject(CKOBJ_BRUSH,(ClientData)hbrush,file,line);
  tkWinGdi_CreateSolidBrush++;
  GTRACE(("%d:CreateSolidBrush(0x%x) return 0x%x in %s %d\n",tkWinGdi_CreateSolidBrush,crColor,hbrush,file,line);)
  return hbrush;
}
HBRUSH CkGraph_CreatePatternBrush(HBITMAP hbitmap,char* file,int line){
  HBRUSH hbrush;
  if((hbrush=CreatePatternBrush(hbitmap))==(HBRUSH)0){
    dprintf("CkCreatePatternBrush(0x%x) failed in %s %d\n",hbitmap,file,line);
    return (HBRUSH)0;
  }
  CkGraphCreateObject(CKOBJ_BRUSH,(ClientData)hbrush,file,line);
  tkWinGdi_CreatePatternBrush++;
  GTRACE(("%d:CreatePatternBrush(0x%x) return 0x%x in %s %d\n",tkWinGdi_CreatePatternBrush,hbitmap,hbrush,file,line);)
  return hbrush;
}
HBITMAP CkGraph_CreateDIBitmap(HDC hdc, BITMAPINFOHEADER *lpbmih,
                               DWORD fdwInit, VOID*lpbInit,
                                BITMAPINFO *lpbmi,UINT fuUsage,char* file,int line){
  HBITMAP hbitmap;
  if((hbitmap = CreateDIBitmap(hdc,lpbmih,fdwInit,lpbInit,lpbmi,fuUsage))==(HBITMAP)0){
    dprintf("CkCreateDIBitmap failed in %s %d\n",file,line);
    return (HBITMAP)0;
  }
  CkGraphCreateObject(CKOBJ_BITMAP,(ClientData)hbitmap,file,line);
  tkWinGdi_CreateDIBitmap++;
  GTRACE(("%d:CreateDIBitmap(0x%x,..) return 0x%x in %s %d\n",tkWinGdi_CreateDIBitmap,hdc,hbitmap,file,line);)
  return hbitmap;
}
HBITMAP CkGraph_CreateCompatibleBitmap(HDC hdc,int nWidth,int nHeight,char* file,int line){
  HBITMAP hbitmap;
  if((hbitmap = CreateCompatibleBitmap(hdc,nWidth,nHeight))==(HBITMAP)0){
    dprintf("CkCreateCompatibleBitmap failed in %s %d\n",file,line);
    return (HBITMAP)0;
  }
  CkGraphCreateObject(CKOBJ_BITMAP,(ClientData)hbitmap,file,line);
  tkWinGdi_CreateCompatibleBitmap++;
  GTRACE(("%d:CreateCompatibleBitmap(0x%x,%d,%d) return 0x%x in %s %d\n",tkWinGdi_CreateCompatibleBitmap,hdc,nWidth,nHeight,hbitmap,file,line);)
  return hbitmap;
}
HBITMAP CkGraph_LoadBitmap(HINSTANCE hinst,LPCTSTR lpszBitmap,char* file,int line){
  HBITMAP hbitmap;
  if((hbitmap = LoadBitmap(hinst,lpszBitmap))==(HBITMAP)0){
    dprintf("CkLoadBitmap failed in %s %d\n",file,line);
    return (HBITMAP)0;
  }
  CkGraphCreateObject(CKOBJ_BITMAP,(ClientData)hbitmap,file,line);
  tkWinGdi_LoadBitmap++;
  GTRACE(("%d:LoadBitmap(..,%s) return 0x%x in %s %d\n",tkWinGdi_LoadBitmap,lpszBitmap,hbitmap,file,line);)
  return hbitmap;
}

HBITMAP CkGraph_CreateBitmap(int nWidth,int nHeight,UINT cPlanes,UINT cBitsPerPel,
                            VOID* lpvBits,char* file,int line){
  HBITMAP hbitmap;
  if((hbitmap = CreateBitmap(nWidth,nHeight,cPlanes,cBitsPerPel,lpvBits))==(HBITMAP)0){
    dprintf("CkCreateBitmap failed in %s %d\n",file,line);
    return (HBITMAP)0;
  }
  CkGraphCreateObject(CKOBJ_BITMAP,(ClientData)hbitmap,file,line);
  tkWinGdi_CreateBitmap++;
  GTRACE(("%d:CreateBitmap(%d,%d,%d,%d,...) return 0x%x in %s %d\n",tkWinGdi_CreateBitmap,nWidth,nHeight,cPlanes,cBitsPerPel,hbitmap,file,line);)
  return hbitmap;
}

HRGN CkGraph_CreateRectRgn(int nLeftRect,int nTopRect,int nRightRect,int nBottomRect,char* file,int line){
  HRGN hrgn;
  if((hrgn = CreateRectRgn(nLeftRect,nTopRect,nRightRect,nBottomRect))==(HRGN)0){
    dprintf("CkCreateRectRgn failed in %s %d\n",file,line);
    return (HRGN)0;
  }
  CkGraphCreateObject(CKOBJ_REGION,(ClientData)hrgn,file,line);
  tkWinGdi_CreateRectRgn++;
  GTRACE(("%d:CreateRectRgn(%d,%d,%d,%d) return 0x%x in %s %d\n",tkWinGdi_CreateRectRgn,nLeftRect,nTopRect,nRightRect,nBottomRect,hrgn,file,line);)
  return hrgn;
}
HFONT CkGraph_CreateFontIndirect( LOGFONT* lplogfont,char* file,int line){
  HFONT hfont;
  if((hfont = CreateFontIndirect(lplogfont))==(HFONT)0){
    dprintf("CkCreateFontIndirect failed in %s %d\n",file,line);
    return (HFONT)0;
  }
  CkGraphCreateObject(CKOBJ_FONT,(ClientData)hfont,file,line);
  tkWinGdi_CreateFontIndirect++;
  GTRACE(("%d:CreateFontIndirect(%s,%d) return 0x%x in %s %d\n",tkWinGdi_CreateFontIndirect,lplogfont->lfFaceName,lplogfont->lfHeight,hfont,file,line);)
  return hfont;
}
#endif /*CKGRAPH_DEBUG*/

typedef struct PalObj {
  HPALETTE       hpal;
  struct PalObj *next;
} PalObj;

/* 
 * in this List all gdi-allocated palettes are stored
 * the reason for storing palettes is, that sometimes the deletion of
 * a palette fails because the palette is still selected into one of
 * our device-contexts (because of the schema always select in , never select
 * out). so we store the palettes in a free list and try from time to time
 * to give it away to windows(at least at the end of the program :-).
 */

TCL_DECLARE_MUTEX(palMutex)
static PalObj *allocPals = (PalObj*)0;  
static PalObj *freePals = (PalObj*)0;  
/* adds a Palette to a given paletteList */
void AddPalObj(PalObj** rootPtrPtr,HPALETTE hpal) {
  PalObj* newPtr,*currPtr;
  Tcl_MutexLock(&palMutex);
  for ( currPtr = *rootPtrPtr; currPtr != NULL; currPtr = currPtr->next ) {
      if(currPtr->hpal==hpal) {
         /* there's no need for allocating , because we already have it */
         Tcl_MutexUnlock(&palMutex);
         return;
      }
  }
  newPtr=(PalObj*)gd_alloc(sizeof(PalObj));
  newPtr->hpal = hpal;
  newPtr->next = *rootPtrPtr;
  *rootPtrPtr  = newPtr ;
  Tcl_MutexUnlock(&palMutex);
}
/* deletes a Palette from a given paletteList */
void RemovePalObj(PalObj** rootPtrPtr,HPALETTE hpal) {
  PalObj *currPtr;
  PalObj *prevPtr;
  PalObj *nextPtr;
  Tcl_MutexLock(&palMutex);
  for (prevPtr = NULL, currPtr = *rootPtrPtr; 
       currPtr != NULL;
       prevPtr = currPtr , currPtr = nextPtr ) {
    nextPtr = currPtr->next;
    if(currPtr->hpal==hpal) {
      /* we found our candidate so remove it */
      if (currPtr==*rootPtrPtr) {
        *rootPtrPtr = currPtr->next;
      } else {
        prevPtr->next = currPtr->next;
      }
      Tcl_MutexUnlock(&palMutex);
      gd_free((char*)currPtr);
      return;
    } 
  }
  /*notfound*/
  Tcl_MutexUnlock(&palMutex);
}
/* walks through a palette list and tries to free all hpal gdiobjects */
/* if this was successful , the palette is deleted from the list */
static void TryFreePalettes(PalObj** rootPtrPtr) {
  PalObj *currPtr;
  PalObj *prevPtr;
  PalObj *nextPtr;
  Tcl_MutexLock(&palMutex);
  for (prevPtr = NULL, currPtr = *rootPtrPtr; 
       currPtr != NULL;
       prevPtr = currPtr , currPtr = nextPtr ) {
    nextPtr = currPtr->next;
    if(DeleteObject(currPtr->hpal)) {
#ifdef CKGRAPH_DEBUG
     dprintf("successful removed palette 0x%x in TryFreePalettes\n",
        currPtr->hpal);
     CkGraphDeleteObject(CKOBJ_PAL,(HGDIOBJ)currPtr->hpal,__FILE__,__LINE__);
#endif
      /* delete object was successful, so remove the thing */
      if(currPtr==*rootPtrPtr) {
        *rootPtrPtr=currPtr->next;
      } else {
        prevPtr->next=currPtr->next;
      }
      gd_free((char*)currPtr);
    } else {
#ifdef CKGRAPH_DEBUG
     dprintf("failed removing palette 0x%x in TryFreePalettes\n",
        currPtr->hpal);
#endif
    }
  }
  Tcl_MutexUnlock(&palMutex);
}

#ifdef CKGRAPH_DEBUG
HPALETTE CkGraph_CreatePalette( LOGPALETTE* lplogpal,char* file,int line)
#else
HPALETTE CkGraph_CreatePalette( LOGPALETTE* lplogpal)
#endif
{
  HPALETTE hpal;
  if((hpal = CreatePalette(lplogpal))==(HPALETTE)0){
#ifdef CKGRAPH_DEBUG
    dprintf("CkCreatePalette failed in %s %d\n",file,line);
#endif
    return (HPALETTE)0;
  }
#ifdef CKGRAPH_DEBUG
  CkGraphCreateObject(CKOBJ_PAL,(ClientData)hpal,file,line);
  tkWinGdi_CreatePalette++;
  GTRACE(("%d:CreatePalette(%d) return 0x%x in %s %d\n",tkWinGdi_CreatePalette,lplogpal->palNumEntries,hpal,file,line);)
#endif
  AddPalObj(&allocPals,hpal);
  return hpal;
}
#ifdef CKGRAPH_DEBUG
HGDIOBJ CkGraph_GetStockObject(int stockobj,char* file,int line)
#else
HGDIOBJ CkGraph_GetStockObject(int stockobj)
#endif /*CKGRAPH_DEBUG*/
{
  HGDIOBJ hobj=NULL;
  switch(stockobj){
    case NULL_PEN:
      hobj=stock_NULL_PEN;break;
    case WHITE_PEN:
      hobj=stock_WHITE_PEN;break;
    case BLACK_PEN:
      hobj=stock_BLACK_PEN;break;
    case NULL_BRUSH:
      hobj=stock_NULL_BRUSH;break;
    case WHITE_BRUSH:
      hobj=stock_WHITE_BRUSH;break;
    case BLACK_BRUSH:
      hobj=stock_BLACK_BRUSH;break;
    case GRAY_BRUSH:
      hobj=stock_GRAY_BRUSH;break;
    case DKGRAY_BRUSH:
      hobj=stock_DKGRAY_BRUSH;break;
    case LTGRAY_BRUSH:
      hobj=stock_LTGRAY_BRUSH;break;
    case SYSTEM_FONT:
      hobj=stock_SYSTEM_FONT;break;
    case OEM_FIXED_FONT:
      hobj=stock_OEM_FIXED_FONT;break;
    case SYSTEM_FIXED_FONT:
      hobj=stock_SYSTEM_FIXED_FONT;break;
    case ANSI_VAR_FONT:
      hobj=stock_ANSI_VAR_FONT;break;
    case ANSI_FIXED_FONT:
      hobj=stock_ANSI_FIXED_FONT;break;
    case DEVICE_DEFAULT_FONT:
      hobj=stock_DEVICE_DEFAULT_FONT;break;
    case DEFAULT_PALETTE:
      hobj=stock_DEFAULT_PALETTE;break;
#ifdef CKGRAPH_DEBUG
    default:
      dprintf("unknown stockobject %d\n",stockobj);
#endif
  }
#ifdef CKGRAPH_DEBUG
  if(hobj==NULL){
    dprintf("CkGetStockObject failed in %s %d\n",file,line);
  }
#endif
  return hobj;
}



#ifdef CKGRAPH_DEBUG
HDC CkGraph_GetDC(HWND hwnd,char* file,int line)
#else
HDC CkGraph_GetDC(HWND hwnd)
#endif
{
  HDC hdc;
  if((hdc=GetDC(hwnd))==(HDC)0){
#ifdef CKGRAPH_DEBUG
    dprintf("CkGetDC failed in %s %d\n",file,line);
#endif
    return (HDC)0;
  }
#ifdef CKGRAPH_DEBUG
  CkGraphCreateObject(CKOBJ_DC,(ClientData)hdc,file,line);
  tkWinGdi_GetDC++;
  GTRACE(("%d:GetDC(0x%x) return 0x%x in %s %d\n",tkWinGdi_GetDC,hwnd,hdc,file,line);)
#endif
  CkGraphCreateDeviceContext(CKOBJ_DC,hdc);
  return hdc;
}

#ifdef CKGRAPH_DEBUG
HDC CkGraph_CreateDC(LPCTSTR lpszDriver,LPCTSTR lpszDevice,LPCTSTR lpszOutput,
                     CONST DEVMODE* lpInitData,char* file,int line)
#else
HDC CkGraph_CreateDC(LPCTSTR lpszDriver,LPCTSTR lpszDevice,LPCTSTR lpszOutput,
                     CONST DEVMODE* lpInitData)
#endif
{
  HDC hdc;
  if((hdc=CreateDC(lpszDriver,lpszDevice,lpszOutput,lpInitData))==(HDC)0){
#ifdef CKGRAPH_DEBUG
    dprintf("CkCreateDC failed in %s %d\n",file,line);
#endif
    return (HDC)0;
  }
#ifdef CKGRAPH_DEBUG
  {
    int type;
    if(Wtk_test_win32s())
      type=CKOBJ_DC;
    else {
      if((type=GetObjectType(hdc))!=CKOBJ_DC){
        dprintf("ObjectType in CkCreateCompatibleDC is %s\n",obj_type(type));
      }
    }
    CkGraphCreateObject(type,(ClientData)hdc,file,line);
    tkWinGdi_CreateDC++;
  }
  GTRACE(("%d:CreateDC(%s,..) return 0x%x in %s %d\n",tkWinGdi_CreateDC,lpszDriver,hdc,file,line);)
#endif
  CkGraphCreateDeviceContext(CKOBJ_DC,hdc);
  return hdc;
}

#ifdef CKGRAPH_DEBUG
int CkGraph_ReleaseDC(HWND hwnd,HDC hdc,char* file,int line)
#else
int CkGraph_ReleaseDC(HWND hwnd,HDC hdc)
#endif
{
  int result;
  result=ReleaseDC(hwnd,hdc);
#ifdef CKGRAPH_DEBUG
  if(!result)
    dprintf("CkReleaseDC failed in %s %d\n",file,line);
  CkGraphDeleteObject(CKOBJ_DC,(ClientData)hdc,file,line);
  tkWinGdi_ReleaseDC++;
  GTRACE(("%d:ReleaseDC(0x%x,0x%x) return 0x%x in %s %d\n",tkWinGdi_ReleaseDC,hwnd,hdc,result,file,line);)
#endif
  CkGraphDeleteDeviceContext(hdc);
  return result;
}

#ifdef CKGRAPH_DEBUG
HDC CkGraph_CreateCompatibleDC(HDC hdc,char* file,int line)
#else
HDC CkGraph_CreateCompatibleDC(HDC hdc)
#endif
{
  HDC hdcmem;
  if((hdcmem=CreateCompatibleDC(hdc))==(HDC)0){
#ifdef CKGRAPH_DEBUG
    dprintf("CkCreateCompatibleDC(0x%x) failed in %s %d\n",hdc,file,line);
#endif
    return (HDC)0;
  }
#ifdef CKGRAPH_DEBUG
  {
    int type;
    if(Wtk_test_win32s())
      type=CKOBJ_MEMDC;
    else {
      if((type=GetObjectType(hdcmem))!=CKOBJ_MEMDC){
        dprintf("ObjectType in CkCreateCompatibleDC is %s\n",obj_type(type));
      }
    }
    CkGraphCreateObject(type,(ClientData)hdcmem,file,line);
    tkWinGdi_CreateCompatibleDC++;
  }
  GTRACE(("%d:CreateCompatibleDC(0x%x) return 0x%x in %s %d\n",tkWinGdi_CreateCompatibleDC,hdc,hdcmem,file,line);)
#endif
  CkGraphCreateDeviceContext(CKOBJ_MEMDC,hdcmem);
  return hdcmem;
}
#ifdef CKGRAPH_DEBUG
int CkGraph_DeleteDC(HDC hdc,char* file,int line)
#else
int CkGraph_DeleteDC(HDC hdc)
#endif
{
  int result;
#ifdef CKGRAPH_DEBUG
  int type;
  if(!Wtk_test_win32s()){
    if((type=GetObjectType(hdc))!=CKOBJ_MEMDC && type!=CKOBJ_DC ){
      dprintf("ObjectType in DeleteDC is %s\n",obj_type(type));
    }
  } 
#endif
  /*select out the palette because otherwise the deletion of the
    palette sometimes fail*/
  SelectPalette(hdc,stock_DEFAULT_PALETTE,TRUE);
  result=DeleteDC(hdc);
#ifdef CKGRAPH_DEBUG
  if(!result)
    dprintf("CkDeleteDC failed in %s %d\n",file,line);
  CkGraphDeleteObject(type,(ClientData)hdc,file,line);
  tkWinGdi_DeleteDC++;
  GTRACE(("%d:DeleteDC(0x%x) return 0x%x in %s %d\n",tkWinGdi_DeleteDC,hdc,result,file,line);)
#endif
  CkGraphDeleteDeviceContext(hdc);
  return result;
}
#ifdef CKGRAPH_DEBUG
HDC CkGraph_BeginPaint(HWND hwnd,LPPAINTSTRUCT lpps,char* file,int line)
#else
HDC CkGraph_BeginPaint(HWND hwnd,LPPAINTSTRUCT lpps)
#endif
{
  HDC hdc;
  if((hdc=BeginPaint(hwnd,lpps))==(HDC)0){
#ifdef CKGRAPH_DEBUG
    dprintf("CkBeginPaint failed in %s %d\n",file,line);
#endif
    return (HDC)0;
  }
#ifdef CKGRAPH_DEBUG
  CkGraphCreateObject(CKOBJ_DC,(ClientData)hdc,file,line);
  tkWinGdi_BeginPaint++;
  GTRACE(("%d:BeginPaint(0x%x,..) return 0x%x in %s %d\n",tkWinGdi_BeginPaint,hwnd,hdc,file,line);)
#endif
  CkGraphCreateDeviceContext(CKOBJ_DC,hdc);
  return hdc;
}

#ifdef CKGRAPH_DEBUG
BOOL CkGraph_EndPaint(HWND hwnd, PAINTSTRUCT* lpPaint,char* file,int line)
#else
BOOL CkGraph_EndPaint(HWND hwnd, PAINTSTRUCT* lpPaint)
#endif
{
  HDC hdc=lpPaint->hdc;
  BOOL result;
  result=EndPaint(hwnd,lpPaint);
#ifdef CKGRAPH_DEBUG
  if(result==FALSE)
    dprintf("CkEndPaint failed in %s %d\n",file,line);
  CkGraphDeleteObject(CKOBJ_DC,(ClientData)hdc,file,line);
  tkWinGdi_EndPaint++;
  GTRACE(("%d:EndPaint(0x%x,..) return 0x%x in %s %d\n",tkWinGdi_EndPaint,hwnd,result,file,line);)
#endif
  CkGraphDeleteDeviceContext(hdc);
  return result;
}
#ifdef CKGRAPH_DEBUG
HGDIOBJ CkGraph_SelectObject(HDC hdc,HGDIOBJ hobj_in,char* file,int line)
#else
HGDIOBJ CkGraph_SelectObject(HDC hdc,HGDIOBJ hobj_in)
#endif
{
  HGDIOBJ hobj_out;
  int typein;
  int typeout;
  GdiContext *devPtr=FINDCONTEXT(hdc);
  /*regions are not considered here*/
  if(Wtk_test_win32s()){
    dprintf("CkSelectObject called under win32s"
#ifdef CKGRAPH_DEBUG
            " in %s %d\n",file,line
#endif
           );
  }
  if((hobj_out=SelectObject(hdc,hobj_in))==(HGDIOBJ)0){
#ifdef CKGRAPH_DEBUG
    dprintf("CkSelectObject failed in %s %d\n",file,line);
#endif
    return (HGDIOBJ)0;
  }
  //GetObjectType only runs well under NT and Win95 not Win32s
  //use the CkGraph_Selectxx functions instead
  typein=convert_gdiobjtype(GetObjectType(hobj_in));
  typeout=convert_gdiobjtype(GetObjectType(hobj_out));
  if(devPtr && typein>=0 && typein<MAXGDIOBJ){
    devPtr->hobjs[typein]=hobj_in;
    if(typeout==CKOBJ_BITMAP && !devPtr->defaultbitmap)
      devPtr->defaultbitmap=hobj_out;
  }
  /*
  if(!(CkGraphFindObject(typein,hobj_in) || CkGraphFindObject(typeout,hobj_out))){
    if(!(is_stockobject(typein,hobj_in) || is_stockobject(typeout,hobj_out)))
      dprintf("CkSelectObject with handles not from us in %s %d\n",file,line);
  }
  */
#ifdef CKGRAPH_DEBUG
  tkWinGdi_SelectObject++;
  GTRACE(("%d:SelectObject(0x%x,0x%x) return 0x%x in %s %d\n",tkWinGdi_SelectObject,hdc,hobj_in,hobj_out,file,line);)
#endif
  return hobj_out;
}
#ifdef CKGRAPH_DEBUG
HBITMAP CkGraph_SelectBitmap(HDC hdc,HBITMAP hbitmap,char* file,int line)
#else
HBITMAP CkGraph_SelectBitmap(HDC hdc,HBITMAP hbitmap)
#endif /*CKGRAPH_DEBUG*/
{
   GdiContext *devPtr=FINDCONTEXT(hdc);
   GdiContext *dcPtr;
   HBITMAP hout;
   if(!hbitmap){
#ifdef CKGRAPH_DEBUG
     dprintf("No bitmap in CkGraph_SelectBitmap for %s %d\n",file,line);
#else
     dprintf("No bitmap in CkGraph_SelectBitmap\n");
#endif
     return (HBITMAP)0;
   }
   //if already selected,make a simple return
   if(devPtr && devPtr->hobjs[CKOBJ_BITMAP]==(HGDIOBJ)hbitmap){
#ifdef CKGRAPH_DEBUG
     //check, if we REALLY assume this  correct!
     if(SelectObject(hdc,devPtr->defaultbitmap)!=hbitmap)
       dprintf("bitmap 0x%x not selected into 0x%x\n",hbitmap,hdc);
     hbitmap=SelectObject(hdc,hbitmap);
#endif
     return hbitmap;
   }
   //check for defaultbitmap
   //there are seldom cases,where the defaultbitmap of another
   //hdc is selected into a hdc with a defaultbitmap==0,where the
   //tests not work
   if(devPtr && hbitmap!=devPtr->defaultbitmap){
     //only one bitmap per hdc,check it
     for (dcPtr = devHead ;dcPtr;dcPtr=dcPtr->next){
       if(dcPtr==devPtr)
         continue;
       if(dcPtr->hobjs[CKOBJ_BITMAP]==(HGDIOBJ)hbitmap){
#ifdef CKGRAPH_DEBUG
         if(tkWinGdi_verbose>1){
           GdiObj *objPtr=CkGraphFindObject(dcPtr->type,(ClientData)dcPtr->hdc);
           dprintf("SelectBitmap(0x%x):%s %d,found another hdc 0x%x %s %d with the same bitmap,def=0x%x\n",
                  hbitmap,file,line,dcPtr->hdc,objPtr?objPtr->file:"??",objPtr?objPtr->line:0,dcPtr->defaultbitmap);
         }
#endif
#ifdef CKGRAPH_DEBUG
         hout=SelectObject(dcPtr->hdc,dcPtr->defaultbitmap);
#else
         SelectObject(dcPtr->hdc,dcPtr->defaultbitmap);
#endif

#ifdef CKGRAPH_DEBUG
         tkWinGdi_SelectBitmap++;
         GTRACE(("%d:SelectBitmapO(0x%x,0x%x) return 0x%x in %s %d\n",tkWinGdi_SelectBitmap,dcPtr->hdc,dcPtr->defaultbitmap,hout,file,line);)
#endif
         dcPtr->hobjs[CKOBJ_BITMAP]=dcPtr->defaultbitmap;
         break;
       }
     }
   }
   hout=(HBITMAP)SelectObject(hdc,(HGDIOBJ)hbitmap);
   if(hout && devPtr){
     devPtr->hobjs[CKOBJ_BITMAP]=(HGDIOBJ)hbitmap;
     if(!devPtr->defaultbitmap)
       devPtr->defaultbitmap=hout;
   }
#ifdef CKGRAPH_DEBUG
  tkWinGdi_SelectBitmap++;
  if(!hout)
     dprintf("SelectBitmap failed in %s %d\n",file,line);
  GTRACE(("%d:SelectBitmap(0x%x,0x%x) return 0x%x in %s %d\n",tkWinGdi_SelectBitmap,hdc,hbitmap,hout,file,line);)
#endif
   return hout;
}
/*
  look for a bitmap already selected into a registered dc
  and deselect this bitmap from the dc
*/
#ifdef CKGRAPH_DEBUG
void CkGraph_CheckOutBitmap(HBITMAP hbitmap,char* file,int line)
#else
void CkGraph_CheckOutBitmap(HBITMAP hbitmap)
#endif /*CKGRAPH_DEBUG*/
{
  GdiContext *dcPtr;
  for (dcPtr = devHead ;dcPtr;dcPtr=dcPtr->next){
    if(dcPtr->hobjs[CKOBJ_BITMAP]==(HGDIOBJ)hbitmap){
#ifdef CKGRAPH_DEBUG
      GdiObj *objPtr=CkGraphFindObject(dcPtr->type,(ClientData)dcPtr->hdc);
      dprintf("CheckOutBitmap(0x%x):%s %d,found another hdc 0x%x %s %d with the same bitmap,def=0x%x\n",
              hbitmap,file,line,dcPtr->hdc,objPtr?objPtr->file:"??",objPtr?objPtr->line:0,dcPtr->defaultbitmap);
#endif
      SelectObject(dcPtr->hdc,dcPtr->defaultbitmap);
      dcPtr->hobjs[CKOBJ_BITMAP]=dcPtr->defaultbitmap;
      break;
    }
  }
}

#ifdef CKGRAPH_DEBUG
HPEN CkGraph_SelectPen(HDC hdc,HPEN hpen,char* file,int line)
#else
HPEN CkGraph_SelectPen(HDC hdc,HPEN hpen)
#endif /*CKGRAPH_DEBUG*/
{
   GdiContext *devPtr=FINDCONTEXT(hdc);
   HPEN hout;
   //if already selected,make a simple return
   if(devPtr && devPtr->hobjs[CKOBJ_PEN]==(HGDIOBJ)hpen){
#ifdef CKGRAPH_DEBUG
     if((hout=(HPEN)SelectObject(hdc,(HGDIOBJ)stock_NULL_PEN))!=hpen){
       dprintf("pen is 0x%x, should be 0x%x in %s %d\n",hout,hpen,file,line);
     }
     hpen=(HPEN)SelectObject(hdc,(HGDIOBJ)hpen);
#endif
     return hpen;
   }
   hout=(HPEN)SelectObject(hdc,(HGDIOBJ)hpen);
   if(hout && devPtr)
     devPtr->hobjs[CKOBJ_PEN]=(HGDIOBJ)hpen;
#ifdef CKGRAPH_DEBUG
  tkWinGdi_SelectPen++;
   if(!hout)
     dprintf("CkSelectPen failed in %s %d\n",file,line);
   GTRACE(("%d:SelectPen(0x%x,0x%x) return 0x%x in %s %d\n",tkWinGdi_SelectPen,hdc,hpen,hout,file,line);)
#endif
  return hout;
}
#ifdef CKGRAPH_DEBUG
HBRUSH CkGraph_SelectBrush(HDC hdc,HBRUSH hbrush,char* file,int line)
#else
HBRUSH CkGraph_SelectBrush(HDC hdc,HBRUSH hbrush)
#endif /*CKGRAPH_DEBUG*/
{
   GdiContext *devPtr=FINDCONTEXT(hdc);
   HBRUSH hout;
   //if already selected,make a simple return
   if(devPtr && devPtr->hobjs[CKOBJ_BRUSH]==(HGDIOBJ)hbrush){
#ifdef CKGRAPH_DEBUG
     if((hout=SelectObject(hdc,stock_NULL_BRUSH))!=hbrush)
       dprintf("brush is 0x%x, should be 0x%x in %s %d\n",hout,hbrush,file,line);
     hbrush=SelectObject(hdc,hbrush);
#endif
     return hbrush;
   }
   hout=(HBRUSH)SelectObject(hdc,(HGDIOBJ)hbrush);
   if(hout && devPtr)
     devPtr->hobjs[CKOBJ_BRUSH]=(HGDIOBJ)hbrush;
#ifdef CKGRAPH_DEBUG
   tkWinGdi_SelectBrush++;
   if(!hout)
     dprintf("CkSelectBrush failed in %s %d\n",file,line);
   GTRACE(("%d:SelectBrush(0x%x,0x%x) return 0x%x in %s %d\n",tkWinGdi_SelectBrush,hdc,hbrush,hout,file,line);)
#endif
  return hout;
}
#ifdef CKGRAPH_DEBUG
int CkGraph_SelectClipRgn(HDC hdc,HRGN hrgn,char* file,int line)
#else
int CkGraph_SelectClipRgn(HDC hdc,HRGN hrgn)
#endif /*CKGRAPH_DEBUG*/
{
   GdiContext *devPtr=FINDCONTEXT(hdc);
   int result;
   /*
    * if the region is already selected,make a simple return
    */
   if(devPtr && devPtr->hrgn==hrgn)
     return devPtr->hrgntype;
   result=SelectClipRgn(hdc,hrgn);
   if(result!=ERROR && devPtr) {
     devPtr->hrgn=hrgn;
     devPtr->hrgntype=result;
   }
   /*
    * reset the offsets to mark them invalid
    */
   if(devPtr) {
     devPtr->hrgn_xoff=0;
     devPtr->hrgn_yoff=0;
   }
#ifdef CKGRAPH_DEBUG
   tkWinGdi_SelectClipRgn++;
   if(result==ERROR)
     dprintf("CkSelectClipRgn failed in %s %d\n",file,line);
   GTRACE(("%d:SelectClipRgn(0x%x,0x%x) return 0x%x in %s %d\n",tkWinGdi_SelectClipRgn,hdc,hrgn,result,file,line);)
#endif
  return result;
}
#ifdef CKGRAPH_DEBUG
int CkGraph_OffsetClipRgn(HDC hdc,int xoff,int yoff,char* file,int line)
#else
int CkGraph_OffsetClipRgn(HDC hdc,int xoff,int yoff)
#endif /*CKGRAPH_DEBUG*/
{
   GdiContext *devPtr=FINDCONTEXT(hdc);
   int result;
   /*
    * if the offsets don't differ make a simple return
    * however in the case of xoff==yoff==0 we force an Offset
    * because a previous selecting might be invoked before
    */
   if(devPtr && devPtr->hrgn_xoff==xoff && devPtr->hrgn_yoff==yoff
      && xoff!=0 && yoff!=0 ) {
     return devPtr->hrgntype;
   }
   result=OffsetClipRgn(hdc,xoff,yoff);
   if(result!=ERROR && devPtr) {
     devPtr->hrgn_xoff=xoff;
     devPtr->hrgn_yoff=yoff;
   }
#ifdef CKGRAPH_DEBUG
   tkWinGdi_OffsetClipRgn++;
   if(result==ERROR)
     dprintf("CkOffsetClipRgn failed in %s %d\n",file,line);
   GTRACE(("%d:OffsetClipRgn(0x%x,%d,%d) return 0x%x in %s %d\n",tkWinGdi_OffsetClipRgn,hdc,xoff,yoff,result,file,line);)
#endif
  return result;
}
#ifdef CKGRAPH_DEBUG
HPALETTE CkGraph_SelectPalette(HDC hdc,HPALETTE hpal,BOOL f,char* file,int line)
#else
HPALETTE CkGraph_SelectPalette(HDC hdc,HPALETTE hpal,BOOL f)
#endif /*CKGRAPH_DEBUG*/
{
   GdiContext *devPtr=FINDCONTEXT(hdc);
   HPALETTE hout;
   //if already selected,make a simple return
   if(devPtr && devPtr->hobjs[CKOBJ_PAL]==(HGDIOBJ)hpal){
#ifdef CKGRAPH_DEBUG
     if((hout=SelectPalette(hdc,stock_DEFAULT_PALETTE,f))!=hpal)
       dprintf("palette is 0x%x,should be 0x%x in %s %d\n",hout,hpal,file,line);
     hpal=SelectPalette(hdc,hpal,f);
#endif
     return hpal;
   }
   hout=SelectPalette(hdc,hpal,f);
   if(hout && devPtr){
     devPtr->hobjs[CKOBJ_PAL]=(HGDIOBJ)hpal;
     devPtr->palettevalid=0;
/*
     if(!devPtr->defaultpalette){
       devPtr->defaultpalette=hout;
       if(hout==stock_DEFAULT_PALETTE){
         dprintf("Default palette was selected\n");
       }
     }
*/
   }
#ifdef CKGRAPH_DEBUG
   tkWinGdi_SelectPalette++;
   if(!hout)
     dprintf("CkSelectPalette failed in %s %d\n",file,line);
   GTRACE(("%d:SelectPalette(0x%x,0x%x,%d) return 0x%x in %s %d\n",tkWinGdi_SelectPalette,hdc,hpal,f,hout,file,line);)
#endif
  return hout;
}

#ifdef CKGRAPH_DEBUG
UINT CkGraph_RealizePalette(HDC hdc,char* file,int line)
#else
UINT CkGraph_RealizePalette(HDC hdc)
#endif /*CKGRAPH_DEBUG*/
{
  UINT numcolors;
  GdiContext *devPtr=FINDCONTEXT(hdc);
  if (devPtr && devPtr->type==CKOBJ_MEMDC && devPtr->palettevalid==1){
    return 0;
  }
  numcolors=RealizePalette(hdc);
#ifdef CKGRAPH_DEBUG
  tkWinGdi_RealizePalette++;
  if(numcolors==GDI_ERROR)
    dprintf("CkRealizePalette failed in %s %d\n",file,line);
   GTRACE(("%d:RealizePalette(0x%x) return %d in %s %d\n",tkWinGdi_RealizePalette,hdc,numcolors,file,line);)
#endif
  if (devPtr && devPtr->type==CKOBJ_MEMDC){
    devPtr->palettevalid=1;
  }
  return numcolors;
}
static void InvalidateSelectedPalette(HPALETTE hPal){
  GdiContext *dcPtr;
  for (dcPtr = devHead ;dcPtr;dcPtr=dcPtr->next){
    if(dcPtr->hobjs[CKOBJ_PAL]==hPal)
      dcPtr->palettevalid=0;
  }
}
#ifdef CKGRAPH_DEBUG
UINT CkGraph_SetPaletteEntries(HPALETTE hPal,UINT iStart,UINT cEntries ,
               CONST PALETTEENTRY* lppe,char* file,int line)
#else
UINT CkGraph_SetPaletteEntries(HPALETTE hPal,UINT iStart,UINT cEntries ,
               CONST PALETTEENTRY* lppe)
#endif /*CKGRAPH_DEBUG*/
{
  UINT result=SetPaletteEntries(hPal,iStart,cEntries,lppe);
  if(result){
    InvalidateSelectedPalette(hPal);
  }
#ifdef CKGRAPH_DEBUG
  else {
    dprintf("CkRealizePalette failed in %s %d\n",file,line);
  }
  tkWinGdi_SetPaletteEntries++;
  GTRACE(("%d:SetPaletteEntries(0x%x,%d,%d,..) return 0x%x in %s %d\n",tkWinGdi_SetPaletteEntries,hPal,iStart,cEntries,result,file,line);)
#endif
  return result;
}
#ifdef CKGRAPH_DEBUG
UINT CkGraph_ResizePalette(HPALETTE hPal,UINT nEntries,char* file,int line)
#else
UINT CkGraph_ResizePalette(HPALETTE hPal,UINT nEntries)
#endif /*CKGRAPH_DEBUG*/
{
  BOOL result=ResizePalette(hPal,nEntries);
  if(result){
    InvalidateSelectedPalette(hPal);
  }
#ifdef CKGRAPH_DEBUG
  else {
    dprintf("CkResizePalette failed in %s %d\n",file,line);
  }
  tkWinGdi_ResizePalette++;
  GTRACE(("%d:ResizePalette(0x%x,%d) return 0x%x in %s %d\n",tkWinGdi_ResizePalette,hPal,nEntries,result,file,line);)
#endif
  return result;
}
#ifdef CKGRAPH_DEBUG
int CkGraph_SetROP2(HDC hdc,int fnDrawMode,char* file,int line)
#else
int CkGraph_SetROP2(HDC hdc,int fnDrawMode)
#endif /*CKGRAPH_DEBUG*/
{
   GdiContext *devPtr=FINDCONTEXT(hdc);
   int ropout;
   //if already selected,make a simple return
   if(devPtr && devPtr->rop==fnDrawMode){
#ifdef CKGRAPH_DEBUG
     //check if nobody behind us changed the ROP...
     if((ropout=GetROP2(hdc))!=fnDrawMode){
       dprintf("rop is %d,should be %d in %s %d\n",ropout,fnDrawMode,file,line);
       fnDrawMode=SetROP2(hdc,fnDrawMode);
     }
#endif
     return fnDrawMode;
   }
   ropout=SetROP2(hdc,fnDrawMode);
   if(ropout && devPtr){
     devPtr->rop=fnDrawMode;
   }
#ifdef CKGRAPH_DEBUG
   tkWinGdi_SetROP2++;
   if(!ropout)
     dprintf("CkSetROP2 failed in %s %d\n",file,line);
  GTRACE(("%d:SetROP2(0x%x,%d) return 0x%x in %s %d\n",tkWinGdi_SetROP2,hdc,fnDrawMode,ropout,file,line);)
#endif
  return ropout;
}
#ifdef CKGRAPH_DEBUG
int CkGraph_SetBkMode(HDC hdc,int iBkMode,char* file,int line)
#else
int CkGraph_SetBkMode(HDC hdc,int iBkMode)
#endif /*CKGRAPH_DEBUG*/
{
   GdiContext *devPtr=FINDCONTEXT(hdc);
   int bkout;
   //if already selected,make a simple return
   if(devPtr && devPtr->bkmode==iBkMode){
#ifdef CKGRAPH_DEBUG
     //check if nobody behind us changed the bkmode...
     if((bkout=GetBkMode(hdc))!=iBkMode){
       dprintf("bkmode is %d, should be %d in %s %d\n",bkout,iBkMode,file,line);
       SetBkMode(hdc,iBkMode);
     }
#endif
     return iBkMode;
   }
   bkout=SetBkMode(hdc,iBkMode);
   if(bkout && devPtr){
     devPtr->bkmode=iBkMode;
   }
#ifdef CKGRAPH_DEBUG
   tkWinGdi_SetBkMode++;
   if(!bkout)
     dprintf("CkSetBkMode failed in %s %d\n",file,line);
  GTRACE(("%d:SetBkMode(0x%x,%d) return 0x%x in %s %d\n",tkWinGdi_SetBkMode,hdc,iBkMode,bkout,file,line);)
#endif
  return bkout;
}
#ifdef CKGRAPH_DEBUG
COLORREF CkGraph_SetColor(HDC hdc,COLORREF color,int bk,char* file,int line)
#else
COLORREF CkGraph_SetColor(HDC hdc,COLORREF color,int bk)
#endif /*CKGRAPH_DEBUG*/
{
   GdiContext *devPtr=FINDCONTEXT(hdc);
   COLORREF colorout;
   COLORREF* colorPtr;
   //if already selected,make a simple return
   if(devPtr) { 
     if( *(colorPtr=(bk?&devPtr->bkcolor:&devPtr->textcolor))==color)
#ifdef CKGRAPH_DEBUG
     //is it REALLY selected?
     //check if nobody behind us changed the colors...
     {
       COLORREF realbk;
       if((realbk=bk?GetBkColor(hdc):GetTextColor(hdc))!=color){
         dprintf("%s is 0x%x,should be 0x%x in %s %d\n",bk?"bkcolor":"txtcolor",realbk,color,file,line);
         return bk?SetBkColor(hdc,color):SetTextColor(hdc,color);
       }
       return color;
       
     }
#else
       return color;
#endif
   }
   colorout=bk?SetBkColor(hdc,color):SetTextColor(hdc,color);
   if(colorout!=CLR_INVALID && devPtr){
     *colorPtr=color;
   }
#ifdef CKGRAPH_DEBUG
   if(bk) tkWinGdi_SetBkColor++; else tkWinGdi_SetTextColor++;
   if(colorout==CLR_INVALID)
     dprintf("%s failed in %s %d\n",bk?"CkSetBkColor":"CkSetTextColor",file,line);
  GTRACE(("%d:%s(0x%x,0x%x) return 0x%x in %s %d\n",bk?tkWinGdi_SetBkColor:tkWinGdi_SetTextColor,bk?"SetBkColor":"SetTextColor",hdc,color,colorout,file,line);)
#endif
  return colorout;
}
#ifdef CKGRAPH_DEBUG
int CkGraph_SetPolyFillMode(HDC hdc, int iMode,char* file,int line)
#else
int CkGraph_SetPolyFillMode(HDC hdc, int iMode)
#endif /*CKGRAPH_DEBUG*/
{
   GdiContext *devPtr=FINDCONTEXT(hdc);
   int fillout;
   //if already selected,make a simple return
   if(devPtr && devPtr->fillmode==iMode){
#ifdef CKGRAPH_DEBUG
     //check if nobody behind us changed the fillmode...
     if((fillout=GetPolyFillMode(hdc))!=iMode){
       dprintf("fillmode is %d,should be %d in %s %d\n",fillout,iMode,file,line);
       iMode=SetPolyFillMode(hdc,iMode);
     }
#endif
     return iMode;
   }
   fillout=SetPolyFillMode(hdc,iMode);
   if(fillout && devPtr){
     devPtr->fillmode=iMode;
   }
#ifdef CKGRAPH_DEBUG
   tkWinGdi_SetPolyFillMode++;
   if(!fillout)
     dprintf("CkSetPolyFillMode failed in %s %d\n",file,line);
  GTRACE(("%d:SetPolyFillMode(0x%x,%d) return 0x%x in %s %d\n",tkWinGdi_SetPolyFillMode,hdc,iMode,fillout,file,line);)
#endif
  return fillout;
}

#ifdef CKGRAPH_DEBUG
HFONT CkGraph_SelectFont(HDC hdc,HFONT hfont,char* file,int line)
#else
HFONT CkGraph_SelectFont(HDC hdc,HFONT hfont)
#endif /*CKGRAPH_DEBUG*/
{
   GdiContext *devPtr=FINDCONTEXT(hdc);
   HFONT hout;
   //if already selected,make a simple return
   if(devPtr && devPtr->hobjs[CKOBJ_FONT]==(HGDIOBJ)hfont){
#ifdef CKGRAPH_DEBUG
     hout=(HFONT)SelectObject(hdc,(HGDIOBJ)stock_SYSTEM_FONT);
     if(hout!=hfont){
       dprintf("font is 0x%x,should be 0x%x in %s %d\n",hout,hfont,file,line);
     }
     hfont=(HFONT)SelectObject(hdc,(HGDIOBJ)hfont);
#endif
     return hfont;
   }
   hout=(HFONT)SelectObject(hdc,(HGDIOBJ)hfont);
   if(hout && devPtr){
     devPtr->hobjs[CKOBJ_FONT]=(HGDIOBJ)hfont;
   }
#ifdef CKGRAPH_DEBUG
   tkWinGdi_SelectFont++;
   if(!hout)
     dprintf("CkSelectFont failed in %s %d\n",file,line);
   GTRACE(("%d:SelectFont(0x%x,0x%x) return 0x%x in %s %d\n",tkWinGdi_SelectFont,hdc,hfont,hout,file,line);)
#endif
  return hout;
}
//DeleteObject functions

#ifdef CKGRAPH_DEBUG
#define CHECKSEL(x) if(!(x)){dprintf("%s failed in %s,line %d\n",##x,__FILE__,__LINE__);}
#define DODELETEOBJECT(type,hobj,file,line) DoDeleteObject(type,hobj,file,line)
static int DoDeleteObject(int type,HGDIOBJ hobj,char* file,int line)
#else
#define CHECKSEL(x) x
#define DODELETEOBJECT(type,hobj,file,line) DoDeleteObject(type,hobj)
static int DoDeleteObject(int type,HGDIOBJ hobj)
#endif
{
  int success=1;
  GdiContext* objPtr;
#ifdef CKGRAPH_DEBUG
  if(is_stockobject(type,hobj)){
    dprintf("fatal try to delete the stockobject 0x%x in %s %d\n",hobj,file,line);
    return 0;
  }
#endif
  //walk through the list of dc's and look if the object is still selected in a dc
  for (objPtr = devHead ;objPtr;objPtr=objPtr->next){
    if(objPtr->hobjs[type]==hobj){
    //yupp ,it does
#ifdef CKGRAPH_DEBUG
      if (tkWinGdi_verbose>1){
        GdiObj* hdcobj=CkGraphFindObject(objPtr->type,
                                             (ClientData)objPtr->hdc);
        dprintf(
            "CkDeleteObject:%s 0x%x from %s %d still selected in 0x%x %s %d\n",
                        obj_type(type),(int)hobj,file,line,
               (int)objPtr->hdc,hdcobj?hdcobj->file:"??",hdcobj?hdcobj->line:0);
      }
#endif
      //repair with deselecting the object
      switch(type){
        case CKOBJ_PEN:CHECKSEL(SelectObject(objPtr->hdc,objPtr->hobjs[type]=stock_NULL_PEN));break;
        case CKOBJ_BRUSH:CHECKSEL(SelectObject(objPtr->hdc,objPtr->hobjs[type]=stock_NULL_BRUSH));break;
        case CKOBJ_FONT:CHECKSEL(SelectObject(objPtr->hdc,objPtr->hobjs[type]=stock_SYSTEM_FONT));break;
        case CKOBJ_REGION:CHECKSEL(SelectClipRgn(objPtr->hdc,objPtr->hobjs[type]=(HGDIOBJ)NULL));break;
        case CKOBJ_PAL:CHECKSEL(SelectPalette(objPtr->hdc,objPtr->hobjs[type]=stock_DEFAULT_PALETTE,TRUE));break;
        case CKOBJ_BITMAP:CHECKSEL(SelectObject(objPtr->hdc,objPtr->hobjs[type]=objPtr->defaultbitmap));break;
        default:
          dprintf("have no success select for 0x%x\n");
          success=0;
      }
    }
  }
  return success;
}
#ifdef CKGRAPH_DEBUG
static int DoRealDelete(int type,HGDIOBJ hobj,char* file,int line)
{
  int result=DeleteObject(hobj);
  if(!result)
    dprintf("CkDeleteObject 0x%x failed in %s %d\n",(int)hobj,file,line);
  CkGraphDeleteObject(type,(ClientData)hobj,file,line);
  return result;
}
#else
#define DoRealDelete(type,hobj,file,line) DeleteObject(hobj)
#endif


#ifdef CKGRAPH_DEBUG
int CkGraph_DeleteObject(HGDIOBJ hobj,char* file,int line)
#else
int CkGraph_DeleteObject(HGDIOBJ hobj)
#endif
{
  int success=1;
  int type;
  //GetObjectType only runs well under NT and Win95 not Win32s
  //use the CkGraph_Deletexx functions instead
  if(Wtk_test_win32s()){
    dprintf("CkDeleteObject called under win32s"
#ifdef CKGRAPH_DEBUG
            " in %s %d\n",file,line
#endif
           );
  }
  type=convert_gdiobjtype(GetObjectType(hobj));
  if(type>=0 && type <MAXGDIOBJ){
    success=DODELETEOBJECT(type,hobj,file,line);
  }
#ifdef CKGRAPH_DEBUG
  tkWinGdi_DeleteObject++;
   GTRACE(("%d:DeleteObject(0x%x) return %d in %s %d\n",tkWinGdi_DeleteObject,hobj,success,file,line);)
#endif
  return success?DoRealDelete(type,hobj,file,line):FALSE;
}

#ifdef CKGRAPH_DEBUG
int CkGraph_DeleteBrush(HBRUSH hobj,char* file,int line)
#else
int CkGraph_DeleteBrush(HBRUSH hobj)
#endif
{
  int success=DODELETEOBJECT(CKOBJ_BRUSH,hobj,file,line);
#ifdef CKGRAPH_DEBUG
  tkWinGdi_DeleteBrush++;
  GTRACE(("%d:DeleteBrush(0x%x) return %d in %s %d\n",tkWinGdi_DeleteBrush,hobj,success,file,line);)
#endif
  return success?DoRealDelete(CKOBJ_BRUSH,hobj,file,line):FALSE;
}

#ifdef CKGRAPH_DEBUG
int CkGraph_DeletePen(HPEN hobj,char* file,int line)
#else
int CkGraph_DeletePen(HPEN hobj)
#endif
{
  int success=DODELETEOBJECT(CKOBJ_PEN,hobj,file,line);
#ifdef CKGRAPH_DEBUG
  tkWinGdi_DeletePen++;
  GTRACE(("%d:DeletePen(0x%x) return %d in %s %d\n",tkWinGdi_DeletePen,hobj,success,file,line);)
#endif
  return success?DoRealDelete(CKOBJ_PEN,hobj,file,line):FALSE;
}
#ifdef CKGRAPH_DEBUG
int CkGraph_DeleteFont(HFONT hobj,char* file,int line)
#else
int CkGraph_DeleteFont(HFONT hobj)
#endif
{
  int success=DODELETEOBJECT(CKOBJ_FONT,hobj,file,line);
#ifdef CKGRAPH_DEBUG
  tkWinGdi_DeleteFont++;
  GTRACE(("%d:DeleteFont(0x%x) return %d in %s %d\n",tkWinGdi_DeleteFont,hobj,success,file,line);)
#endif
  return success?DoRealDelete(CKOBJ_FONT,hobj,file,line):FALSE;
}
#ifdef CKGRAPH_DEBUG
int CkGraph_DeleteBitmap(HBITMAP hobj,char* file,int line)
#else
int CkGraph_DeleteBitmap(HBITMAP hobj)
#endif
{
  int success=DODELETEOBJECT(CKOBJ_BITMAP,hobj,file,line);
#ifdef CKGRAPH_DEBUG
  tkWinGdi_DeleteBitmap++;
  GTRACE(("%d:DeleteBitmap(0x%x) return %d in %s %d\n",tkWinGdi_DeleteBitmap,hobj,success,file,line);)
#endif
  return success?DoRealDelete(CKOBJ_BITMAP,hobj,file,line):FALSE;
}
#ifdef CKGRAPH_DEBUG
int CkGraph_DeletePalette(HPALETTE hobj,char* file,int line)
#else
int CkGraph_DeletePalette(HPALETTE hobj)
#endif
{
  int success=DODELETEOBJECT(CKOBJ_PAL,hobj,file,line);
  /*now it's a good point to try freeing the freePals if there were previous*/
  if(freePals) {
    TryFreePalettes(&freePals);
  }
#ifdef CKGRAPH_DEBUG
  tkWinGdi_DeletePalette++;
  GTRACE(("%d:DeletePalette(0x%x) return %d in %s %d\n",tkWinGdi_DeletePalette,hobj,success,file,line);)
#endif
  if(DoRealDelete(CKOBJ_PAL,hobj,file,line)==FALSE) {
    /*Oops, the deletion failed , so try it later*/
#ifdef CKGRAPH_DEBUG
    CkGraphCreateObject(CKOBJ_PAL,(ClientData)hobj,file,line);
#endif
    /*promise the caller it was ok*/
    success=TRUE;
    AddPalObj(&freePals,(HPALETTE)hobj);
  }
  RemovePalObj(&allocPals,(HPALETTE)hobj);
  return success;
}

/*
  this bloody function tries to free all objects allocated here
  it walks trough the list of DC's and tries to wipe out all
  objects, afterwards all DC's are freed
*/
void CkGraph_FreeObjects(void){
  GdiContext *devPtr,*objPtr2,*nextPtr;
  int  i;
  int gdiobjs[5]={CKOBJ_BRUSH,CKOBJ_PEN,CKOBJ_FONT,CKOBJ_BITMAP,CKOBJ_PAL};
  currdc=(GdiContext*)0;
  for (devPtr = devHead ;devPtr;){
    nextPtr = devPtr->next;
    /*look for all selected objects in gdiobjs*/
    for (i=0;i<sizeof(gdiobjs)/sizeof(gdiobjs[0]);i++){
      int objtype=gdiobjs[i];
      HGDIOBJ hgdiobj=devPtr->hobjs[objtype];
      /*don't delete StockObjects */
      if (hgdiobj!=0 && !is_stockobject(objtype,hgdiobj)){
        for(objPtr2=devPtr;objPtr2;objPtr2=objPtr2->next)
          DODELETEOBJECT(objtype,hgdiobj,__FILE__,__LINE__);
        DoRealDelete(objtype,hgdiobj,__FILE__,__LINE__);
      }
    }
    /*now deleting this dc*/
    if (devPtr->type==CKOBJ_MEMDC){
      DeleteDC(devPtr->hdc);
    }
#ifdef CKGRAPH_DEBUG
    CkGraphDeleteObject(devPtr->type,(ClientData)devPtr->hdc,__FILE__,__LINE__);
#endif
    gd_free((char *)devPtr);
    devPtr=nextPtr;
    devHead=nextPtr;
  }
  devHead=(GdiContext*)0;
  /*now last try to remove the palettes*/
  TryFreePalettes(&freePals);
  TryFreePalettes(&allocPals);
}

#endif /*CKGRAPH_IMP*/

#ifdef PROFILE_INFO
/*the following funcs are only called if PROFILE_INFO is on,
  it's strongly recommended to remove PROFILE_INFO for a release version*/

#ifdef CKGRAPH_DEBUG
BOOL CkGraph_BitBlt( HDC hdcDest, int nXDest, int nYDest, int nWidth, int nHeight, HDC hdcSrc, int nXSrc, int nYSrc, DWORD dwRop,char* file,int line)
#else
BOOL CkGraph_BitBlt( HDC hdcDest, int nXDest, int nYDest, int nWidth, int nHeight, HDC hdcSrc, int nXSrc, int nYSrc, DWORD dwRop)
#endif
{
  BOOL result=
   BitBlt(hdcDest,nXDest,nYDest,nWidth,nHeight,hdcSrc,nXSrc,nYSrc,dwRop);
#ifdef CKGRAPH_DEBUG
  tkWinGdi_BitBlt++;
  if(!result)
     dprintf("CkBitBlt failed in %s %d\n",file,line);
  GTRACE(("%d:BitBlt(0x%x,...,0x%x,...) return %d in %s %d\n",tkWinGdi_BitBlt,hdcDest,hdcSrc,result,file,line);)
#endif
  return result;
}

#ifdef CKGRAPH_DEBUG
int CkGraph_FillRect(HDC hDC,CONST RECT *lprc,HBRUSH hbr,char* file,int line)
#else
int CkGraph_FillRect(HDC hDC,CONST RECT *lprc,HBRUSH hbr)
#endif
{
  int result= FillRect(hDC,lprc,hbr);
#ifdef CKGRAPH_DEBUG
  tkWinGdi_FillRect++;
  if(!result)
     dprintf("CkFillRect failed in %s %d\n",file,line);
  GTRACE(("%d:FillRect(0x%x,..,0x%x) return %d in %s %d\n",tkWinGdi_FillRect,hDC,hbr,result,file,line);)
#endif
  return result;
}
#ifdef CKGRAPH_DEBUG
BOOL CkGraph_Polyline(HDC hdc,CONST POINT *lppt,int cPoints,char* file,int line)
#else
BOOL CkGraph_Polyline(HDC hdc,CONST POINT *lppt,int cPoints)
#endif
{
  BOOL result= Polyline(hdc,lppt,cPoints);
#ifdef CKGRAPH_DEBUG
  tkWinGdi_Polyline++;
  if(!result)
     dprintf("CkPolyline failed in %s %d\n",file,line);
  GTRACE(("%d:Polyline(0x%x,..,%d) return %d in %s %d\n",tkWinGdi_Polyline,hdc,cPoints,result,file,line);)
#endif
  return result;
}
#ifdef CKGRAPH_DEBUG
BOOL CkGraph_Polygon(HDC hdc,CONST POINT *lppt,int cPoints,char* file,int line)
#else
BOOL CkGraph_Polygon(HDC hdc,CONST POINT *lppt,int cPoints)
#endif
{
  BOOL result= Polygon(hdc,lppt,cPoints);
#ifdef CKGRAPH_DEBUG
  tkWinGdi_Polygon++;
  if(!result)
     dprintf("CkPolygon failed in %s %d\n",file,line);
  GTRACE(("%d:Polygon(0x%x,..,%d) return %d in %s %d\n",tkWinGdi_Polygon,hdc,cPoints,result,file,line);)
#endif
  return result;
}
#ifdef CKGRAPH_DEBUG
BOOL CkGraph_Arc(HDC hdc,int nLeftRect,int nTopRect,int nRightRect,
    int nBottomRect,int nXStartArc,int nYStartArc,int nXEndArc,int nYEndArc,
    char* file,int line)
#else
BOOL CkGraph_Arc(HDC hdc,int nLeftRect,int nTopRect,int nRightRect,
    int nBottomRect,int nXStartArc,int nYStartArc,int nXEndArc,int nYEndArc )
#endif
{
  BOOL result= 
   Arc(hdc,nLeftRect,nTopRect,nRightRect,
    nBottomRect,nXStartArc,nYStartArc,nXEndArc,nYEndArc);
#ifdef CKGRAPH_DEBUG
  tkWinGdi_Arc++;
  if(!result)
     dprintf("CkArc failed in %s %d\n",file,line);
  GTRACE(("%d:Arc(0x%x,..) return %d in %s %d\n",tkWinGdi_Arc,hdc,result,file,line);)
#endif
  return result;
}
#ifdef CKGRAPH_DEBUG
BOOL CkGraph_Chord(HDC hdc,int nLeftRect,int nTopRect,int nRightRect,
     int nBottomRect,int nXRadial1,int nYRadial1,int nXRadial2,int nYRadial2,
     char* file,int line )
#else
BOOL CkGraph_Chord(HDC hdc,int nLeftRect,int nTopRect,int nRightRect,
     int nBottomRect,int nXRadial1,int nYRadial1,int nXRadial2,int nYRadial2 )
#endif
{
  BOOL result= 
   Chord(hdc,nLeftRect,nTopRect,nRightRect,
     nBottomRect,nXRadial1,nYRadial1,nXRadial2,nYRadial2);
#ifdef CKGRAPH_DEBUG
  tkWinGdi_Chord++;
  if(!result)
     dprintf("CkChord failed in %s %d\n",file,line);
  GTRACE(("%d:Chord(0x%x,..) return %d in %s %d\n",tkWinGdi_Chord,hdc,result,file,line);)
#endif
  return result;
}
#ifdef CKGRAPH_DEBUG
BOOL CkGraph_Pie(HDC hdc,int nLeftRect,int nTopRect,int nRightRect,
     int nBottomRect,int nXRadial1,int nYRadial1,int nXRadial2,int nYRadial2,
     char* file,int line )
#else
BOOL CkGraph_Pie(HDC hdc,int nLeftRect,int nTopRect,int nRightRect,
     int nBottomRect,int nXRadial1,int nYRadial1,int nXRadial2,int nYRadial2 )
#endif
{
  BOOL result= 
   Pie(hdc,nLeftRect,nTopRect,nRightRect,
     nBottomRect,nXRadial1,nYRadial1,nXRadial2,nYRadial2);
#ifdef CKGRAPH_DEBUG
  tkWinGdi_Pie++;
  if(!result)
     dprintf("CkPie failed in %s %d\n",file,line);
  GTRACE(("%d:Pie(0x%x,..) return %d in %s %d\n",tkWinGdi_Pie,hdc,result,file,line);)
#endif
  return result;
}
#ifdef CKGRAPH_DEBUG
BOOL CkGraph_Rectangle(HDC hdc,int nLeftRect,int nTopRect,int nRightRect, 
              int nBottomRect,char* file,int line)
#else
BOOL CkGraph_Rectangle(HDC hdc,int nLeftRect,int nTopRect,int nRightRect, 
              int nBottomRect)
#endif
{
  BOOL result= 
   Rectangle(hdc,nLeftRect,nTopRect,nRightRect,nBottomRect);
#ifdef CKGRAPH_DEBUG
  tkWinGdi_Rectangle++;
  if(!result)
     dprintf("CkRectangle failed in %s %d\n",file,line);
  GTRACE(("%d:Rectangle(0x%x,..) return %d in %s %d\n",tkWinGdi_Rectangle,hdc,result,file,line);)
#endif
  return result;
}
static char* PrBk(int bkmode){
  if(bkmode==OPAQUE)
    return "OPAQUE";
  if(bkmode==TRANSPARENT)
    return "TRANSPARENT";
  return "UNKNOWN";
}
#ifdef CKGRAPH_DEBUG
BOOL CkGraph_TextOut(HDC hdc,int nXStart, int nYStart, LPCTSTR str, int len,
                      char* file,int line)
#else
BOOL CkGraph_TextOut(HDC hdc,int nXStart, int nYStart, LPCTSTR str, int len)
#endif
{
  BOOL result=TextOut(hdc,nXStart,nYStart,str,len);
#ifdef CKGRAPH_DEBUG
  {
/*
    int bkmode=GetBkMode(hdc);
    COLORREF bkcolor=GetBkColor(hdc);
    COLORREF textcolor=GetTextColor(hdc);
    GdiContext *devPtr=FINDCONTEXT(hdc);
*/
    tkWinGdi_TextOut++;
/*
    if(!devPtr){
      dprintf("did not find devPtr for TextOut\n");
      goto printout;
    }
    if(devPtr->bkmode!=bkmode)
      dprintf("bkmode:%s!=devPtr->bkmode:%s\n",PrBk(bkmode),PrBk(devPtr->bkmode));
    if(devPtr->bkcolor!=bkcolor)
      dprintf("bkcolor:0x%x!=devPtr->bkcolor:0x%x\n",bkcolor,devPtr->bkcolor);
    if(devPtr->textcolor!=textcolor)
      dprintf("bkcolor:0x%x!=devPtr->bkcolor:0x%x\n",bkcolor,devPtr->bkcolor);
printout:
    dprintf("TextOut(0x%x,%d,%d,%s,%d),bkmode:%s,bkcolor:0x%x,textcolor:0x%x\n",
      hdc,nXStart,nYStart,str,len,PrBk(bkmode),bkcolor,textcolor);
*/
    GTRACE(("%d:TextOut(0x%x,..,%s,..) return %d in %s %d\n",tkWinGdi_TextOut,hdc,str,result,file,line);)
  
  }
#endif
  return result;
}
#ifdef CKGRAPH_DEBUG
BOOL CkGraph_ExtTextOut( HDC hdc, int X, int Y, UINT fuOptions,
    CONST RECT *lprc, LPCTSTR lpString, UINT cbCount, CONST INT *lpDx,
    char* file,int line )
#else
BOOL CkGraph_ExtTextOut( HDC hdc, int X, int Y, UINT fuOptions,
    CONST RECT *lprc, LPCTSTR lpString, UINT cbCount, CONST INT *lpDx)
#endif
{
  BOOL result= 
   ExtTextOut(hdc,X,Y, fuOptions,lprc,lpString,cbCount,lpDx);
#ifdef CKGRAPH_DEBUG
  tkWinGdi_ExtTextOut++;
  if(!result)
     dprintf("CkExtTextOut failed in %s %d\n",file,line);
  GTRACE(("%d:ExtTextOut(0x%x,....) return %d in %s %d\n",tkWinGdi_ExtTextOut,hdc,result,file,line);)
#endif
  return result;
}

//if PROFILE_INFO is off Windows-CreatePen a.s.o is used
#ifndef CKGRAPH_DEBUG
HPEN CkGraph_CreatePen(int fnPenStyle,int nWidth,COLORREF  crColor){
  HPEN hpen;
  if((hpen=CreatePen(fnPenStyle,nWidth,crColor))==(HPEN)0){
    return (HPEN)0;
  }
  return hpen;
}
HPEN CkGraph_ExtCreatePen(DWORD dwPenStyle,DWORD dwWidth,CONST LOGBRUSH* lplb,
                          DWORD dwStyleCount, CONST DWORD* lpStyle){
  HPEN hpen;
  if((hpen=ExtCreatePen(dwPenStyle,dwWidth,lplb,dwStyleCount,lpStyle))==(HPEN)0){
    return (HPEN)0;
  }
  return hpen;
}
HBRUSH CkGraph_CreateSolidBrush(COLORREF crColor){
  HBRUSH hbrush;
  if((hbrush=CreateSolidBrush(crColor))==(HBRUSH)0){
    return (HBRUSH)0;
  }
  return hbrush;
}
HBRUSH CkGraph_CreatePatternBrush(HBITMAP hbitmap){
  HBRUSH hbrush;
  if((hbrush=CreatePatternBrush(hbitmap))==(HBRUSH)0){
    return (HBRUSH)0;
  }
  return hbrush;
}
HBITMAP CkGraph_CreateDIBitmap(HDC hdc, BITMAPINFOHEADER *lpbmih,
                               DWORD fdwInit, VOID*lpbInit,
                                BITMAPINFO *lpbmi,UINT fuUsage){
  HBITMAP hbitmap;
  if((hbitmap = CreateDIBitmap(hdc,lpbmih,fdwInit,lpbInit,lpbmi,fuUsage))==(HBITMAP)0){
    return (HBITMAP)0;
  }
  return hbitmap;
}
HBITMAP CkGraph_CreateCompatibleBitmap(HDC hdc,int nWidth,int nHeight){
  HBITMAP hbitmap;
  if((hbitmap = CreateCompatibleBitmap(hdc,nWidth,nHeight))==(HBITMAP)0){
    return (HBITMAP)0;
  }
  return hbitmap;
}
HBITMAP CkGraph_LoadBitmap(HINSTANCE hinst,LPCTSTR lpszBitmap){
  HBITMAP hbitmap;
  if((hbitmap = LoadBitmap(hinst,lpszBitmap))==(HBITMAP)0){
    return (HBITMAP)0;
  }
  return hbitmap;
}

HBITMAP CkGraph_CreateBitmap(int nWidth,int nHeight,UINT cPlanes,UINT cBitsPerPel,
                            VOID* lpvBits){
  HBITMAP hbitmap;
  if((hbitmap = CreateBitmap(nWidth,nHeight,cPlanes,cBitsPerPel,lpvBits))==(HBITMAP)0){
    return (HBITMAP)0;
  }
  return hbitmap;
}

HRGN CkGraph_CreateRectRgn(int nLeftRect,int nTopRect,int nRightRect,int nBottomRect){
  HRGN hrgn;
  if((hrgn = CreateRectRgn(nLeftRect,nTopRect,nRightRect,nBottomRect))==(HRGN)0){
    return (HRGN)0;
  }
  return hrgn;
}
HFONT CkGraph_CreateFontIndirect( LOGFONT* lplogfont){
  HFONT hfont;
  if((hfont = CreateFontIndirect(lplogfont))==(HFONT)0){
    return (HFONT)0;
  }
  return hfont;
}
HPALETTE CkGraph_CreatePalette( LOGPALETTE* lplogpal){
  HPALETTE hpal;
  if((hpal = CreatePalette(lplogpal))==(HPALETTE)0){
    return (HPALETTE)0;
  }
  return hpal;
}
#endif /*CKGRAPH_DEBUG*/
#endif /*PROFILE_INFO*/
