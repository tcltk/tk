#ifndef _SDLTK_H_
#define _SDLTK_H_

#ifdef PLATFORM_SDL

#include "SdlTkX.h"

EXTERN int		TkpSetCaptureEx(Display *display, TkWindow *winPtr);

#endif

#ifndef USE_TK_STUBS

#ifdef PLATFORM_SDL

EXTERN char *		TkAlignImageData(XImage *image,
				int alignment, int bitOrder);

EXTERN int		TkpCmapStressed(Tk_Window tkwin,
				Colormap colormap);

EXTERN void		TkCreateXEventSource(void);

EXTERN void		TkFreeXId(TkDisplay *dispPtr);

EXTERN unsigned long	TkpGetMS(void);

EXTERN int		TkpScanWindowId(Tcl_Interp *interp,
				const char *string, Window *idPtr);

EXTERN void		TkpSetCursor(TkpCursor cursor);

EXTERN void		TkpSync(Display *display);

EXTERN int		TkpWmSetState(TkWindow *winPtr, int state);

EXTERN void		TkFreeWindowId(TkDisplay *dispPtr, Window w);

EXTERN void		TkInitXId(TkDisplay *dispPtr);

EXTERN void		TkSendCleanup(TkDisplay *dispPtr);

EXTERN void		TkWmCleanup(TkDisplay *dispPtr);

EXTERN Window		TkpContainerId(TkWindow *winPtr);

EXTERN int		TkpDoOneXEvent(Tcl_Time *timePtr);

EXTERN void		TkpSetMenubar(Tk_Window tkwin,
				Tk_Window menubar);

#endif

#endif

#endif
