#ifndef _SDLTKX_H
#define _SDLTKX_H

#include "X11/Xlib.h"
#include "X11/Xutil.h"

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(USE_TK_STUBS) || !defined(_WIN32)

#define XAllocColor SdlTkXAllocColor
#define XAllocNamedColor SdlTkXAllocNamedColor
#define XBell SdlTkXBell
#define XChangeGC SdlTkXChangeGC
#define XChangeProperty SdlTkXChangeProperty
#define XChangeWindowAttributes SdlTkXChangeWindowAttributes
#define XClearWindow 0
#define XClipBox SdlTkXClipBox
#define XCloseDisplay SdlTkXCloseDisplay
#define XConfigureWindow SdlTkXConfigureWindow
#define XConvertSelection SdlTkXConvertSelection
#define XCopyArea SdlTkXCopyArea
#define XCopyGC SdlTkXCopyGC
#define XCopyPlane SdlTkXCopyPlane
#define XCreateBitmapFromData SdlTkXCreateBitmapFromData
#define XCreateColormap SdlTkXCreateColormap
#define XCreateGC SdlTkXCreateGC
#define XCreateGlyphCursor SdlTkXCreateGlyphCursor
#define XCreateImage SdlTkXCreateImage
#define XCreatePixmap SdlTkXCreatePixmap
#define XCreatePixmapCursor SdlTkXCreatePixmapCursor
#define XCreateRegion SdlTkXCreateRegion
#define XCreateWindow SdlTkXCreateWindow
#define XDefineCursor SdlTkXDefineCursor
#define XDeleteProperty SdlTkXDeleteProperty
#define XDestroyRegion SdlTkXDestroyRegion
#define XDestroyWindow SdlTkXDestroyWindow
#define XDrawArc SdlTkXDrawArc
#define XDrawArcs SdlTkXDrawArcs
#define XDrawLine SdlTkXDrawLine
#define XDrawLines SdlTkXDrawLines
#define XDrawPoint SdlTkXDrawPoint
#define XDrawPoints SdlTkXDrawPoints
#define XDrawRectangle SdlTkXDrawRectangle
#define XDrawRectangles SdlTkXDrawRectangles
#define XDrawSegments SdlTkXDrawSegments
#define XDrawString SdlTkXDrawString
#define XDrawString16 SdlTkXDrawString16
#define XDrawStringAngle SdlTkXDrawStringAngle
#define XEmptyRegion SdlTkXEmptyRegion
#define XEqualRegion SdlTkXEqualRegion
#define XEventsQueued SdlTkXEventsQueued
#define XFillArc SdlTkXFillArc
#define XFillArcs SdlTkXFillArcs
#define XFillPolygon SdlTkXFillPolygon
#define XFillRectangle SdlTkXFillRectangle
#define XFillRectangles SdlTkXFillRectangles
#define XFlush SdlTkXFlush
#define XForceScreenSaver SdlTkXForceScreenSaver
#define XFree SdlTkXFree
#define XFreeColormap SdlTkXFreeColormap
#define XFreeColors SdlTkXFreeColors
#define XFreeCursor SdlTkXFreeCursor
#define XFreeFont SdlTkXFreeFont
#define XFreeFontNames SdlTkXFreeFontNames
#define XFreeGC SdlTkXFreeGC
#define XFreeModifiermap SdlTkXFreeModifiermap
#define XFreePixmap SdlTkXFreePixmap
#define XGContextFromGC SdlTkXGContextFromGC
#define XGetAtomName SdlTkXGetAtomName
#define XGetFontProperty SdlTkXGetFontProperty
#define XGetGeometry SdlTkXGetGeometry
#define XGetImage SdlTkXGetImage
#define XGetInputFocus SdlTkXGetInputFocus
#define XGetModifierMapping SdlTkXGetModifierMapping
#define XGetVisualInfo SdlTkXGetVisualInfo
#define XGetWindowAttributes SdlTkXGetWindowAttributes
#define XGetWindowProperty SdlTkXGetWindowProperty
#define XGetWMColormapWindows SdlTkXGetWMColormapWindows
#define XGrabKeyboard SdlTkXGrabKeyboard
#define XGrabPointer SdlTkXGrabPointer
#define XGrabServer SdlTkXGrabServer
#define XIconifyWindow SdlTkXIconifyWindow
#define XInternAtom SdlTkXInternAtom
#define XIntersectRegion SdlTkXIntersectRegion
#define XKeycodeToKeysym SdlTkXKeycodeToKeysym
#define XKeysymToKeycode SdlTkXKeysymToKeycode
#define XKeysymToString SdlTkXKeysymToString
#define XListFonts SdlTkXListFonts
#define XListHosts SdlTkXListHosts
#define XLoadFont SdlTkXLoadFont
#define XLoadQueryFont SdlTkXLoadQueryFont
#define XLookupColor SdlTkXLookupColor
#define XMapWindow SdlTkXMapWindow
#define XMoveResizeWindow SdlTkXMoveResizeWindow
#define XMoveWindow SdlTkXMoveWindow
#define XNextEvent SdlTkXNextEvent
#define XNoOp SdlTkXNoOp
#define XOffsetRegion SdlTkXOffsetRegion
#define XOpenDisplay SdlTkXOpenDisplay
#define XParseColor SdlTkXParseColor
#define XPointInRegion SdlTkXPointInRegion
#define XPutImage SdlTkXPutImage
#define XQueryColors SdlTkXQueryColors
#define XQueryPointer SdlTkXQueryPointer
#define XQueryTree SdlTkXQueryTree
#define XRaiseWindow SdlTkXRaiseWindow
#define XLowerWindow SdlTkXLowerWindow
#define XReconfigureWMWindow SdlTkXReconfigureWMWindow
#define XRectInRegion SdlTkXRectInRegion
#define XRefreshKeyboardMapping SdlTkXRefreshKeyboardMapping
#define XReparentWindow SdlTkXReparentWindow
#define XResizeWindow SdlTkXResizeWindow
#define XRootWindow SdlTkXRootWindow
#define XSelectInput SdlTkXSelectInput
#define XSendEvent SdlTkXSendEvent
#define XSetClipMask SdlTkXSetClipMask
#define XSetClipOrigin SdlTkXSetClipOrigin
#define XSetCommand SdlTkXSetCommand
#define XSetDashes SdlTkXSetDashes
#define XSetErrorHandler SdlTkXSetErrorHandler
#define XSetFillRule 0
#define XSetFillStyle SdlTkXSetFillStyle
#define XSetFont SdlTkXSetFont
#define XSetForeground SdlTkXSetForeground
#define XSetFunction 0
#define XSetIconName SdlTkXSetIconName
#define XSetInputFocus SdlTkXSetInputFocus
#define XSetLineAttributes SdlTkXSetLineAttributes
#define XSetRegion SdlTkXSetRegion
#define XSetSelectionOwner SdlTkXSetSelectionOwner
#define XSetStipple SdlTkXSetStipple
#define XSetTransientForHint SdlTkXSetTransientForHint
#define XSetTSOrigin SdlTkXSetTSOrigin
#define XSetWindowBackground SdlTkXSetWindowBackground
#define XSetWindowBackgroundPixmap SdlTkXSetWindowBackgroundPixmap
#define XSetWindowBorder SdlTkXSetWindowBorder
#define XSetWindowBorderPixmap SdlTkXSetWindowBorderPixmap
#define XSetWindowBorderWidth SdlTkXSetWindowBorderWidth
#define XSetWindowColormap SdlTkXSetWindowColormap
#define XSetWMClientMachine SdlTkXSetWMClientMachine
#define XSetWMColormapWindows SdlTkXSetWMColormapWindows
#define XShrinkRegion SdlTkXShrinkRegion
#define XStoreName SdlTkXStoreName
#define XStringListToTextProperty SdlTkXStringListToTextProperty
#define XStringToKeysym SdlTkXStringToKeysym
#define XSubtractRegion SdlTkXSubtractRegion
#define XSync SdlTkXSync
#define XSynchronize SdlTkXSynchronize
#define XTextWidth SdlTkXTextWidth
#define XTextWidthX SdlTkXTextWidthX
#define XTextWidth16 SdlTkXTextWidth16
#define XTranslateCoordinates SdlTkXTranslateCoordinates
#define XUngrabKeyboard SdlTkXUngrabKeyboard
#define XUngrabPointer SdlTkXUngrabPointer
#define XUngrabServer SdlTkXUngrabServer
#define XUnionRectWithRegion SdlTkXUnionRectWithRegion
#define XUnmapWindow SdlTkXUnmapWindow
#define XVisualIDFromVisual SdlTkXVisualIDFromVisual
#define XWarpPointer SdlTkXWarpPointer
#define XWithdrawWindow SdlTkXWithdrawWindow
#define XXorRegion SdlTkXXorRegion

#define _XInitImageFuncPtrs SdlTk_XInitImageFuncPtrs
#define XAllocClassHint SdlTkXAllocClassHint
#define XAllocSizeHints SdlTkXAllocSizeHints
#define XCreateIC SdlTkXCreateIC
#define XDestroyIC SdlTkXDestroyIC
#define XFilterEvent SdlTkXFilterEvent
#define XLookupString SdlTkXLookupString
#define XPutBackEvent SdlTkXPutBackEvent
#define XSetArcMode 0
#define XSetBackground SdlTkXSetBackground
#define XSetClassHint SdlTkXSetClassHint
#define XSetWMHints SdlTkXSetWMHints
#define XSetWMNormalHints SdlTkXSetWMNormalHints
#define XUnionRegion SdlTkXUnionRegion
#define XWindowEvent SdlTkXWindowEvent
#define XmbLookupString SdlTkXMbLookupString

#define XGetAgg2D SdlTkXGetAgg2D
#define XCreateAgg2D SdlTkXCreateAgg2D
#define XDestroyAgg2D SdlTkXDestroyAgg2D
#define XGetFontFile SdlTkXGetFontFile
#define XGetFTStream SdlTkXGetFTStream
#define XOffsetRegion SdlTkXOffsetRegion
#define XUnionRegion SdlTkXUnionRegion

Status XAllocColor(Display *d, Colormap c, XColor *xp);
Status XAllocNamedColor(Display *display, Colormap colormap,
    const char *color_name, XColor *screen_def_return,
    XColor *exact_def_return);
void XBell(Display *d, int i);
void XChangeGC(Display *d, GC gc, unsigned long mask, XGCValues *values);
void XChangeProperty(Display *d, Window w,
    Atom a1, Atom a2, int i1, int i2, const unsigned char *c, int i3);
void XChangeWindowAttributes(Display *d,
    Window w, unsigned long ul, XSetWindowAttributes *x);
int XClipBox(Region r, XRectangle *rect);
int XCloseDisplay(Display *display);
void XConfigureWindow(Display *d, Window w, unsigned int i,
    XWindowChanges *x);
int XConvertSelection(Display *display,
    Atom selection, Atom target, Atom property,
    Window requestor, Time time);
void XCopyArea(Display *d, Drawable dr1,
    Drawable dr2, GC g, int i1, int i2,
    unsigned int ui1, unsigned int ui2, int i3, int i4);
int XCopyGC(Display *d, GC src, unsigned long gcmask, GC dest);
void XCopyPlane(Display *d, Drawable dr1,
    Drawable dr2, GC g, int i1, int i2,
    unsigned int ui1, unsigned int ui2, int i3, int i4, unsigned long ul);
Pixmap XCreateBitmapFromData(Display *display,
    Drawable d, const char *data, unsigned int width, unsigned int height);
Colormap XCreateColormap(Display *d, Window w, Visual *v, int i);
GC XCreateGC(Display *display, Drawable d, unsigned long valuemask,
    XGCValues *values);
Cursor XCreateGlyphCursor(Display *d, Font f1,
    Font f2, unsigned int ui1, unsigned int ui2, XColor *x1, XColor *x2);
XImage *XCreateImage(Display *d, Visual *v,
    unsigned int ui1, int i1, int i2, char *cp,
    unsigned int ui2, unsigned int ui3, int i3, int i4);
Pixmap XCreatePixmap(Display *display, Drawable d, unsigned int width,
    unsigned int height, unsigned int depth);
Cursor XCreatePixmapCursor(Display *d, Pixmap p1, Pixmap p2, XColor *x1,
    XColor *x2, unsigned int ui1, unsigned int ui2);
Region XCreateRegion(void);
Window XCreateWindow(Display *display, Window parent, int x, int y,
    unsigned int width, unsigned int height,
    unsigned int border_width, int depth,
    unsigned int clazz, Visual *visual,
    unsigned long valuemask,
    XSetWindowAttributes *attributes);
int XDefineCursor(Display *d, Window w, Cursor c);
void XDeleteProperty(Display *d, Window w, Atom a);
int XDestroyRegion(Region r);
void XDestroyWindow(Display *d, Window w);
void XDrawArc(Display *d, Drawable dr, GC g,
    int i1, int i2, unsigned int ui1, unsigned int ui2, int i3, int i4);
void XDrawArcs(Display *d, Drawable dr, GC g, XArc *arcs, int n);
void XDrawLine(Display *d, Drawable dr, GC g, int x1, int y1, int x2, int y2);
void XDrawLines(Display *d, Drawable dr, GC g, XPoint *x, int i1, int i2);
void XDrawPoint(Display *display, Drawable d, GC gc, int x, int y);
void XDrawPoints(Display *display, Drawable d, GC gc, XPoint *points,
    int n, int m);
void XDrawRectangle(Display *d, Drawable dr, GC g, int i1, int i2,
    unsigned int ui1, unsigned int ui2);
void XDrawRectangles(Display *d, Drawable dr, GC g, XRectangle rects[], int n);
void XDrawSegments(Display *d, Drawable dr, GC g, XSegment *segs, int n);
int XDrawString(Display *display, Drawable d, GC gc, int x, int y,
    const char *string, int length);
int XDrawString16(Display *display, Drawable d, GC gc, int x, int y,
    const XChar2b *string, int length);
int XDrawStringAngle(Display *display, Drawable d, GC gc, int x, int y,
    const char *string, int length, double angle, int *xret, int *yret);
int XEmptyRegion(Region r);
int XEqualRegion(Region r1, Region r2);
int XEventsQueued(Display *display, int mode);
void XFillArc(Display *d, Drawable dr, GC g, int i1, int i2, unsigned int ui1,
    unsigned int ui2, int i3, int i4);
void XFillArcs(Display *d, Drawable dr, GC g, XArc *arcs, int n);
void XFillPolygon(Display *d, Drawable dr, GC g, XPoint *x, int i1, int i2,
    int i3);
void XFillRectangle(Display *display, Drawable d, GC gc, int x, int y,
    unsigned int width, unsigned int height);
void XFillRectangles(Display *d, Drawable dr, GC g, XRectangle *x, int i);
int XFlush(Display *display);
void XForceScreenSaver(Display *display, int mode);
int XFree(void *data);
void XFreeColormap(Display *d, Colormap c);
void XFreeColors(Display *d, Colormap c, unsigned long *ulp, int i,
    unsigned long ul);
void XFreeCursor(Display *d, Cursor c);
int XFreeFont(Display *display, XFontStruct *font_struct);
int XFreeFontNames(char **list);
void XFreeGC(Display *display, GC gc);
void XFreeModifiermap(XModifierKeymap *x);
int XFreePixmap(Display *display, Pixmap pixmap);
GContext XGContextFromGC(GC g);
char *XGetAtomName(Display *d, Atom a);
Bool XGetFontProperty(XFontStruct *font_struct, Atom atom,
    unsigned long *value_return);
Status XGetGeometry(Display *d, Drawable dr,
    Window *w, int *i1, int *i2,
    unsigned int *ui1, unsigned int *ui2,
    unsigned int *ui3, unsigned int *ui4);
XImage *XGetImage(Display *d, Drawable dr,
    int i1, int i2, unsigned int ui1,
    unsigned int ui2, unsigned long ul, int i3);
int XGetInputFocus(Display *display, Window *focus_return,
    int *revert_to_return);
XModifierKeymap *XGetModifierMapping(Display *d);
XVisualInfo *XGetVisualInfo(Display *display, long vinfo_mask,
    XVisualInfo *vinfo_template, int *nitems_return);
Status XGetWindowAttributes(Display *d, Window w, XWindowAttributes *x);
int XGetWindowProperty(Display *d, Window w, Atom a1, long l1, long l2, Bool b,
    Atom a2, Atom *ap, int *ip,
    unsigned long *ulp1, unsigned long *ulp2, unsigned char **cpp);
Status XGetWMColormapWindows(Display *d, Window w, Window **wpp, int *ip);
int XGrabKeyboard(Display *d, Window w, Bool b, int i1, int i2, Time t);
int XGrabPointer(Display *d, Window w1, Bool b, unsigned int ui, int i1,
    int i2, Window w2, Cursor c, Time t);
int XGrabServer(Display *display);
Status XIconifyWindow(Display *d, Window w, int i);
Atom XInternAtom(Display *display, const char *atom_name, Bool only_if_exists);
int XIntersectRegion(Region reg1, Region reg2, Region newReg);
KeySym XKeycodeToKeysym(Display *d, unsigned int k, int i);
KeyCode XKeysymToKeycode(Display *d, KeySym k);
char *XKeysymToString(KeySym k);
char **XListFonts(Display *display, const char *pattern, int maxnames,
    int *actual_count_return);
XHostAddress *XListHosts(Display *d, int *i, Bool *b);
Font XLoadFont(Display *display, const char *name);
XFontStruct *XLoadQueryFont(Display *display, const char *name);
Status XLookupColor(Display *d, Colormap c1, const char *c2, XColor *x1,
    XColor *x2);
void XLowerWindow(Display *display, Window w);
void XMapWindow(Display *d, Window w);
void XMoveResizeWindow(Display *d, Window w, int i1, int i2, unsigned int ui1,
    unsigned int ui2);
void XMoveWindow(Display *d, Window w, int i1, int i2);
int XNextEvent(Display *display, XEvent *event_return);
int XNoOp(Display *display);
int XOffsetRegion(Region pRegion, int x, int y);
Display *XOpenDisplay(const char *display_name);
Status XParseColor(Display *display, Colormap map, const char *spec,
    XColor *colorPtr);
int XPointInRegion(Region pRegion, int x, int y);
int XPutImage(Display *display, Drawable d, GC gc, XImage *image,
    int src_x, int src_y, int dest_x, int dest_y, unsigned int width,
    unsigned int height);
void XQueryColors(Display *d, Colormap c, XColor *x, int i);
Bool XQueryPointer(Display *d, Window w1, Window *w2, Window *w3, int *i1,
    int *i2, int *i3, int *i4, unsigned int *ui);
Status XQueryTree(Display *d, Window w1, Window *w2, Window *w3, Window **w4,
    unsigned int *ui);
void XRaiseWindow(Display *display, Window w);
Status XReconfigureWMWindow(Display *display, Window w, int screen_number,
    unsigned int mask, XWindowChanges *changes);
int XRectInRegion(Region region, int rx, int ry, unsigned int rwidth,
    unsigned int rheight);
void XRefreshKeyboardMapping(XMappingEvent *x);
int XReparentWindow(Display *display, Window w, Window parent, int x, int y);
void XResizeWindow(Display *d, Window w, unsigned int ui1, unsigned int ui2);
Window XRootWindow(Display *d, int i);
void XSelectInput(Display *d, Window w, long l);
Status XSendEvent(Display *d, Window w, Bool b, long l, XEvent *x);
void XSetClipMask(Display *display, GC gc, Pixmap pixmap);
void XSetClipOrigin(Display *display, GC gc, int clip_x_org, int clip_y_org);
int XSetCommand(Display *display, Window w, char **argv, int argc);
void XSetDashes(Display *display, GC gc, int dash_offset,
    const char *dash_list, int n);
XErrorHandler XSetErrorHandler(XErrorHandler x);
void XSetFont(Display *display, GC gc, Font font);
void XSetForeground(Display *display, GC gc, unsigned long foreground);
void XSetIconName(Display *d, Window w, const char *c);
void XSetInputFocus(Display *d, Window w, int i, Time t);
void XSetLineAttributes(Display *display, GC gc, unsigned int line_width,
    int line_style, int cap_style, int join_style);
int XSetRegion(Display *display, GC gc, Region r);
void XSetSelectionOwner(Display *d, Atom a, Window w, Time t);
int XSetTransientForHint(Display *display, Window w, Window prop_window);
void XSetTSOrigin(Display *display, GC gc, int ts_x_origin, int ts_y_origin);
void XSetWindowBackground(Display *d, Window w, unsigned long ul);
void XSetWindowBackgroundPixmap(Display *d, Window w, Pixmap p);
void XSetWindowBorder(Display *d, Window w, unsigned long ul);
void XSetWindowBorderPixmap(Display *d, Window w, Pixmap p);
void XSetWindowBorderWidth(Display *d, Window w, unsigned int ui);
void XSetWindowColormap(Display *d, Window w, Colormap c);
void XSetWMClientMachine(Display *display, Window w, XTextProperty *text_prop);
Status XSetWMColormapWindows(Display *display, Window w,
    Window *colormap_windows, int count);
int XShrinkRegion(Region r, int dx, int dy);
int XStoreName(Display *display, Window w, const char *window_name);
Status XStringListToTextProperty(char **list, int count,
    XTextProperty *text_prop_return);
KeySym XStringToKeysym(const char *c);
int XSubtractRegion(Region regM, Region regS, Region regD);
int XSync(Display *display, Bool discard);
int XSynchronize(Display *display, Bool discard);
int XTextWidth(XFontStruct *font_struct, const char *string, int count);
int XTextWidthX(XFontStruct *font_struct, const char *string, int count,
    int *maxw);
int XTextWidth16(XFontStruct *font_struct, const XChar2b *string, int count);
Bool XTranslateCoordinates(Display *d, Window w1, Window w2, int i1, int i2,
    int *i3, int *i4, Window *w3);
void XUngrabKeyboard(Display *d, Time t);
int XUngrabPointer(Display *d, Time t);
int XUngrabServer(Display *display);
int XUnionRectWithRegion(XRectangle *rect, Region source, Region dest);
void XUnmapWindow(Display *d, Window w);
VisualID XVisualIDFromVisual(Visual *visual);
void XWarpPointer(Display *d, Window s, Window dw, int sx, int sy,
    unsigned int sw, unsigned int sh, int dx, int dy);
Status XWithdrawWindow(Display *d, Window w, int i);
int XXorRegion(Region sra, Region srb, Region dr);
int XLookupString(XKeyEvent *event_struct, char *buffer_return,
    int bytes_buffer, KeySym *keysym_return, XComposeStatus *status_in_out);
XClassHint *XAllocClassHint(void);
int XSetClassHint(Display *display, Window w, XClassHint *class_hints);
XSizeHints *XAllocSizeHints(void);
void XSetWMNormalHints(Display *display, Window w, XSizeHints *hints);
int XSetWMHints(Display *display, Window w, XWMHints *wm_hints);
int XUnionRegion(Region reg1, Region reg2, Region newReg);
int _XInitImageFuncPtrs(XImage *image);
void XSetStipple(Display *display, GC gc, Pixmap stipple);
void XSetFillStyle(Display *display, GC gc, int fill_style);
void XSetBackground(Display *display, GC gc, unsigned long background);
void XPutBackEvent(Display *display, XEvent *event);
XIC XCreateIC(XIM xim, ...);
void XDestroyIC(XIC xic);
Bool XFilterEvent(XEvent *event, Window w);
int XWindowEvent(Display *display, Window w, long event_mask,
    XEvent *event_return);
int XmbLookupString(XIC ic, XKeyPressedEvent *event, char *buffer_return,
    int bytes_buffer, KeySym *keysym_return, Status *status_return);

/* for "tkpath" */
void *XGetAgg2D(Display *displey, Drawable d);
void *XCreateAgg2D(Display *display);
void XDestroyAgg2D(Display *display, void *agg2d);
int XGetFontFile(const char *family, int size, int isBold, int isItalic,
    const char **nameRet, int *filesizeRet);
void *XGetFTStream(const char *pathname, int size);

/* for "tktreectrl" */
int XOffsetRegion(Region rgn, int dx, int dy);
int XUnionRegion(Region srca, Region srcb, Region dr_return);

#endif /* !USE_TK_STUBS */

#ifdef __cplusplus
}
#endif

#endif /* _SDLTKX_H */

