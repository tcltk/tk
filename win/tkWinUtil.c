/*
 *  tkWinUtil.c --
 *
 *	implements a simple debug terminal and tcl-commands
 *	for gdi-debugging
 *
 * Copyright (c) Brueckner & Jarosch Ing.GmbH,Erfurt,Germany 1997
 * hacked by Leo
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */
#include "tkWinInt.h"
//#include <stdlib.h>
//#include <stdarg.h>


#define TERM_CLASS "tktermclass"
static int initialized=0;
static WNDCLASS termClass;
static HWND hTermTop=(HWND)0;
static HWND hTerm=(HWND)0;
static LRESULT CALLBACK TermProc (HWND hwnd,UINT  message,WPARAM wParam,LPARAM lParam) ;
/*                                                                              
 *----------------------------------------------------------------------        
 * RegisterTerm --                                                       
 *      registers the widgetclass for the debugterminal
 *----------------------------------------------------------------------
 */
static void RegisterTerm(void) {
    if (initialized) 
        return;
    initialized = 1;
    termClass.style = CS_HREDRAW | CS_VREDRAW | CS_CLASSDC;
    termClass.cbClsExtra = 0;
    termClass.cbWndExtra = 0;
    termClass.hInstance = Tk_GetHINSTANCE();
    termClass.hbrBackground = NULL;
    termClass.lpszMenuName = NULL;
    termClass.lpszClassName = TERM_CLASS;
    termClass.lpfnWndProc = TermProc;
    termClass.hIcon = LoadIcon(Tk_GetHINSTANCE(), "tk");
    termClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClass(&termClass)) 
	panic("Unable to register tktermclass");
}
/*                                                                              
 *----------------------------------------------------------------------        
 * TkWin_CreateDebugTerminal --                                                       
 *      creates a window for debug-output , useful for gdi-debugging,
 *      because there's no connection with the Xlib-emulation.
 *      If you want for example debug XDrawString it's very difficult
 *      with the textwidget-console
 *----------------------------------------------------------------------        
 */   
HWND TkWin_CreateDebugTerminal(char* name){
  RECT r;
  int x=CW_USEDEFAULT;
  int y=CW_USEDEFAULT;
  int width=CW_USEDEFAULT;
  int height=CW_USEDEFAULT;
  HINSTANCE hinst= Tk_GetHINSTANCE();
  DWORD style=WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN ;
  int id=0;
  HWND parent=NULL;
  LPVOID paramPtr=NULL;
  if(hTermTop && hTerm) {
    return hTerm;
  }
  RegisterTerm();
/*                                                                              
 * Create the toplevel for the debugterminal
 */
  hTermTop=CreateWindow(TERM_CLASS,name,style,x,y,width,height,parent,
                     (HMENU)id,hinst,paramPtr);
/*                                                                              
 * Create an edit-control inside the toplevel for displaying the text
 */
  GetClientRect(hTermTop,&r);
  x=0;
  y=0;
  width=r.right-r.left;
  height=r.bottom-r.top;
  style = WS_CHILD | WS_HSCROLL | WS_VSCROLL | WS_VISIBLE |
     ES_LEFT | ES_WANTRETURN | ES_AUTOHSCROLL | ES_AUTOVSCROLL | ES_MULTILINE ;
  parent=hTermTop;
  id=1;
  hTerm=CreateWindow("edit","",style,x,y,width,height,parent,
                     (HMENU)id,hinst,paramPtr);
  SendMessage(hTerm,EM_LIMITTEXT,0,(LPARAM)0x7FFFFFFF);
  SendMessage(hTerm,WM_SETFONT,(WPARAM)GetStockObject(ANSI_FIXED_FONT),0);
  return hTerm;
}
/*                                                                              
 *----------------------------------------------------------------------        
 * TermProc --                                                       
 *      Toplevel-Windows Proc for the debugterminal
 *----------------------------------------------------------------------
 */
static LRESULT CALLBACK TermProc (HWND hwnd,UINT  message,WPARAM wParam,LPARAM lParam) {
    switch (message) {
        /*                                                                              
         * Set the Focus to the edit-control
         */
        case WM_SETFOCUS:
          if(hTerm)
            SetFocus(hTerm);
          return 0;
        /*                                                                              
         * Size the edit-control to the size of the toplevel
         */
	case WM_SIZE:
          if(hTerm)
            MoveWindow(hTerm,0,0,LOWORD(lParam),HIWORD(lParam),TRUE);
          return 0;
	case WM_DESTROY:
          hTerm=(HWND)0;
          hTermTop=(HWND)0;
          /*fallthrough*/
	default:
	    break;
    }
    return DefWindowProc(hwnd,message,wParam,lParam);
}
/*                                                                              
 *----------------------------------------------------------------------        
 * TermDestroy --                                                       
 *      destroys the toplevel and the edit-control of the debugterminal
 *----------------------------------------------------------------------
 */

static int TermDestroy(void){
  if(hTermTop){
    if(!DestroyWindow(hTermTop))
      return 0;
    hTermTop=(HWND)0;
    hTerm=(HWND)0;
  }
  return 1;
}
/*                                                                              
 *----------------------------------------------------------------------        
 * dprintf --                                                       
 *     writes a string to the debug terminal if it's present 
 *----------------------------------------------------------------------
 */

void dprintf(const char *format, ...) {
va_list argList;
/*
 * define a huge buffer, hope that nobody will override the end ...
 */
#ifdef  __TURBOC__ 
#define BUFLEN 0xf000
char buffer[BUFLEN];
char buffer2[BUFLEN];
#else
#define BUFLEN 0x7fff
char* buffer=malloc(BUFLEN);
char* buffer2=malloc(BUFLEN);
#endif
  va_start(argList,format);
  (void) vsprintf(buffer,format,argList);
  va_end(argList);
  if(hTerm){
  /*wputs(buffer);*/
    char *p,*p1;
    for (p=buffer,p1=buffer2;*p && (p1-buffer2)<(BUFLEN-1) ; ) {
      if (*p=='\n')
        *p1++='\r';
      *p1++=*p++;
    }
    *p1=0;
    SendMessage(hTerm,EM_REPLACESEL,0,(LPARAM)buffer2);
  }
#ifndef  __TURBOC__
  free(buffer);
  free(buffer2);
#endif
}

#ifdef CKGRAPH_DEBUG
static int gdiusage(Tcl_Interp* interp){
  char msg[2048];
  sprintf(msg,
  "CreatePen:%d\n"
  "ExtCreatePen:%d\n"
  "CreateSolidBrush:%d\n"
  "CreatePatternBrush:%d\n"
  "CreateDIBitmap:%d\n"
  "CreateCompatibleBitmap:%d\n"
  "LoadBitmap:%d\n"
  "CreateBitmap:%d\n"
  "CreateRectRgn:%d\n"
  "CreateFont:%d\n"
  "CreateFontIndirect:%d\n"
  "CreatePalette:%d\n"
  "GetDC:%d\n"
  "CreateDC:%d\n"
  "ReleaseDC:%d\n"
  "CreateCompatibleDC:%d\n"
  "DeleteDC:%d\n"
  "BeginPaint:%d\n"
  "EndPaint:%d\n"
  "SelectObject:%d\n"
  "SelectBitmap:%d\n"
  "SelectPen:%d\n"
  "SelectBrush:%d\n"
  "SelectPalette:%d\n"
  "SelectFont:%d\n"
  "DeleteObject:%d\n"
  "DeleteBrush:%d\n"
  "DeletePen:%d\n"
  "DeleteFont:%d\n"
  "DeleteBitmap:%d\n"
  "DeletePalette:%d\n"
  "RealizePalette:%d\n"
  "SetROP2:%d\n"
  "SetBkMode:%d\n"
  "SetBkColor:%d\n"
  "SetTextColor:%d\n"
  "SelectClipRgn:%d\n"
  "OffsetClipRgn:%d\n"
  "BitBlit:%d\n"
  "FillRect:%d\n"
  "Polyline:%d\n"
  "Polygon:%d\n"
  "Arc:%d\n"
  "Chord:%d\n"
  "Pie:%d\n"
  "Rectangle:%d\n"
  "TextOut:%d\n"
  "ExtTextOut:%d\n"
  "SetPolyFillMode:%d\n",
  tkWinGdi_CreatePen,
  tkWinGdi_ExtCreatePen,
  tkWinGdi_CreateSolidBrush,
  tkWinGdi_CreatePatternBrush,
  tkWinGdi_CreateDIBitmap,
  tkWinGdi_CreateCompatibleBitmap,
  tkWinGdi_LoadBitmap,
  tkWinGdi_CreateBitmap,
  tkWinGdi_CreateRectRgn,
  tkWinGdi_CreateFont,
  tkWinGdi_CreateFontIndirect,
  tkWinGdi_CreatePalette,
  tkWinGdi_GetDC,
  tkWinGdi_CreateDC,
  tkWinGdi_ReleaseDC,
  tkWinGdi_CreateCompatibleDC,
  tkWinGdi_DeleteDC,
  tkWinGdi_BeginPaint,
  tkWinGdi_EndPaint,
  tkWinGdi_SelectObject,
  tkWinGdi_SelectBitmap,
  tkWinGdi_SelectPen,
  tkWinGdi_SelectBrush,
  tkWinGdi_SelectPalette,
  tkWinGdi_SelectFont,
  tkWinGdi_DeleteObject,
  tkWinGdi_DeleteBrush,
  tkWinGdi_DeletePen,
  tkWinGdi_DeleteFont,
  tkWinGdi_DeleteBitmap,
  tkWinGdi_DeletePalette,
  tkWinGdi_RealizePalette,
  tkWinGdi_SetROP2,
  tkWinGdi_SetBkMode,
  tkWinGdi_SetBkColor,
  tkWinGdi_SetTextColor,
  tkWinGdi_SelectClipRgn,
  tkWinGdi_OffsetClipRgn,
  tkWinGdi_BitBlt,
  tkWinGdi_FillRect,
  tkWinGdi_Polyline,
  tkWinGdi_Polygon,
  tkWinGdi_Arc,
  tkWinGdi_Chord,
  tkWinGdi_Pie,
  tkWinGdi_Rectangle,
  tkWinGdi_TextOut,
  tkWinGdi_ExtTextOut,
  tkWinGdi_SetPolyFillMode
  );
  Tcl_AppendResult(interp,msg,(char*)0);
  return TCL_OK;
}
#endif /*CKGRAPH_DEBUG*/

/*                                                                              
 *----------------------------------------------------------------------        
 * TkWinGdiInit --                                                       
 *      Inits the Gdi-optimization package
 *----------------------------------------------------------------------        
 */   
void TkWinGdiInit(HINSTANCE hInstance) {
    CkGraph_Init(hInstance);
#ifdef CKGRAPH_DEBUG
    TkWin_CreateDebugTerminal("debug");
#endif
}
/*                                                                              
 *----------------------------------------------------------------------        
 * TkWinGdiCleanup --                                                       
 *      frees all allocated Gdi-objects during termination of the program               
 *----------------------------------------------------------------------        
 */   
void TkWinGdiCleanup(HINSTANCE hInstance) {
#ifdef USE_CKGRAPH_IMP
  CkGraph_FreeHashedDCs();
  CkGraph_FreeHashedBitmaps();
#endif
  CkGraph_FreeObjects();
#ifdef CKGRAPH_DEBUG
  CkGraph_DumpActiveObjects("gdiexit.log");
#endif
}

#ifdef CKGRAPH_DEBUG
/*                                                                              
 *----------------------------------------------------------------------        
 * GdiCmd --                                                       
 *      prints some statistics about the allocated gdi-objects
 *----------------------------------------------------------------------        
 */   
static int GdiCmd(ClientData clientData,Tcl_Interp *interp,int argc,char **argv){
  int len;
  char* arg;
  if(argc<2)
    goto wrongargs;
  arg=argv[1];
  len=strlen(arg); 
  if(arg[0]=='a' && !strncmp(argv[1],"active",len)){
    if(!CkGraph_DumpActiveObjects((argc<3)?(char*)0:argv[2])){
      Tcl_AppendResult(interp,"error writing to ",argv[2],(char*)0);
      return TCL_ERROR;
    }
  } else if (arg[0]=='u' && !strncmp(argv[1],"usage",len)){
     return gdiusage(interp);
  } else if (arg[0]=='t' && !strncmp(argv[1],"tracing",len)){
     if(argc>=3){
       int a;
       if(Tcl_GetInt(interp,argv[2],&a)!=TCL_OK)
         return TCL_ERROR;
       CkGraph_SetTracing(a);
     }
     sprintf(interp->result,"%d",CkGraph_GetTracing());
     return TCL_OK;
  } else if (arg[0]=='t' && !strncmp(argv[1],"tracefile",len)){
     Tcl_AppendResult(interp,CkGraph_GetTraceFile(),(char*)0);
     return TCL_OK;
  } else if (arg[0]=='f' && !strncmp(argv[1],"free",len)){
     TkWinGdiCleanup((HINSTANCE)0);
     return TCL_OK;
  } else if (arg[0]=='d' && !strncmp(argv[1],"dcs",len)){
     CkGraph_CheckDCs(NULL);
     return TCL_OK;
  }
  return TCL_OK;
wrongargs:
  Tcl_AppendResult(interp,"wrong args, should be:",argv[0]," active|usage|tracing|tracefile|free",(char*)0);
  return TCL_ERROR;
}
#endif /*CKGRAPH_DEBUG*/

/*                                                                              
 *----------------------------------------------------------------------        
 * WinTermCmd --                                                       
 *      exposes a tcl-interface to the debugterminal
 *----------------------------------------------------------------------        
 */   
static int WinTermCmd(ClientData clientData,Tcl_Interp *interp,int argc,char **argv){
  int len;
  char* arg;
  if(argc<2)
    goto wrongargs;
  arg=argv[1];
  len=strlen(arg); 
  if(arg[0]=='c' && !strncmp(argv[1],"create",len)){
    char* name="debug";
    HWND result;
    if(argc>=3)
      name=argv[2];
    result=TkWin_CreateDebugTerminal(name);
    if(!result){
      Tcl_AppendResult(interp,"Could not create debugterminal",(char*)NULL);
      return TCL_ERROR;
    }
    sprintf(interp->result,"0x%x",(int)result);
    return TCL_OK;
  } else if(arg[0]=='d' && !strncmp(argv[1],"destroy",len)){
    if(!TermDestroy()){
      Tcl_AppendResult(interp,"Could not destroy debugterminal",(char*)NULL);
      return TCL_ERROR;
    }
    return TCL_OK;
  } else if(arg[0]=='p' && !strncmp(argv[1],"puts",len)){
    int newline,i=2;
    if(argc<3){
      Tcl_AppendResult(interp,"wrong args, should be:",
          argv[0]," puts ?-nonewline? <string>",(char*)0);
      return TCL_ERROR;
    }
    newline = 1;                                                                
    if ((argc > 3) && (!strcmp(argv[2],"-nonewline"))) {                                              
        newline = 0;                                                            
        i++;                                                                    
    } 
    if(newline)
      dprintf("%s\n",argv[i]);
    else 
      dprintf(argv[i]);
    return TCL_OK;
  }
wrongargs:
  Tcl_AppendResult(interp,"wrong args, should be:",argv[0]," create|destroy|puts",(char*)0);
  return TCL_ERROR;
}
/*                                                                              
 *----------------------------------------------------------------------        
 * TkWin_Init --                                                       
 *     Initialization of the TkWin-package 
 *----------------------------------------------------------------------        
 */   
int TkWin_Init(Tcl_Interp* interp){
#ifdef CKGRAPH_DEBUG
  Tcl_CreateCommand (interp, "gdi", GdiCmd, (ClientData) NULL,
                  (Tcl_CmdDeleteProc *) NULL);
#endif /*CKGRAPH_DEBUG*/
  Tcl_CreateCommand (interp, "winterm", WinTermCmd, (ClientData) NULL,
                  (Tcl_CmdDeleteProc *) NULL);
#ifdef USE_CKGRAPH_IMP
  Tcl_LinkVar(interp,"tkWinHashBrushs",(char*)&tkWinHashBrushs,TCL_LINK_INT);
  Tcl_LinkVar(interp,"tkWinHashPens",(char*)&tkWinHashPens,TCL_LINK_INT);
#endif
  return TCL_OK;
}

