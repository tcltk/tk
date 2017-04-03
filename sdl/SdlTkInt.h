#ifndef _SDLTKINT_H
#define _SDLTKINT_H

#include <SDL.h>

/*
 * For C++ compilers, use extern "C"
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _Colormap _Colormap;
typedef struct _Cursor _Cursor;
typedef struct _Font _Font;
typedef struct _Pixmap _Pixmap;
typedef struct _Window _Window;
typedef struct _XSQEvent _XSQEvent;
typedef struct _DecFrame *DecFrame;

/* X11 Colormap internal rep */
struct _Colormap {
    int whatever;
};

/* X11 Cursor internal rep */
struct _Cursor {
    int shape;
};

typedef struct GlyphIndexHash {
    int refCnt;
    char *familyName;
    unsigned long faceFlags;
    unsigned long styleFlags;
    char *xlfdPattern;
    int hashLoaded;
    Tcl_HashTable hash;
} GlyphIndexHash;

/* X11 Font internal rep */
struct _Font {
    int refCnt;
    const char *file; /* XInternAtom */
    int file_size;
    int index; /* face index in file */
    int size; /* pixel size */
    const char *xlfd; /* malloc */
    GlyphIndexHash *glyphIndexHash;
    int fixedWidth;
    XFontStruct *fontStruct;
};

enum {
    SDLTK_GRAY8,
    SDLTK_RGB565,
    SDLTK_BGR565,
    SDLTK_RGB24,
    SDLTK_BGR24,
    SDLTK_RGBA32,
    SDLTK_ARGB32,
    SDLTK_BGRA32,
    SDLTK_ABGR32,
    SDLTK_BITMAP,
    SDLTK_RGB555,
};

/* Drawable type */
#define DT_PIXMAP 1
#define DT_WINDOW 2

/* X11 Pixmap internal rep */
struct _Pixmap {
    int type; /* must be first */
    SDL_Surface *sdl; /* SDLTK_RGB24 etc */
    int format;
    struct _Pixmap *next;
};

/* X11 Window internal rep */
struct _Window {
    int type; /* must be first */
    _Window *parent; /* May be root; NULL for root */
    _Window *child; /* first child (highest in stacking order) */
    _Window *next; /* next sibling (lower in stacking order) */
    _Window *master; /* Master if this is a transient */
    Display *display;
    XWindowAttributes atts, atts_saved;
    int back_pixel_set;
    unsigned long back_pixel;
    struct _Pixmap *back_pixmap;
    int fullscreen;
    int clazz;
    XSizeHints size;
    int parentWidth, parentHeight; /* Our width/height + 2*atts.border_width */
    TkWindow *tkwin; /* NULL for decorative frame */
    DecFrame dec; /* Only for decorative frame */
#ifdef ANDROID
    int gl_flags;
#else
    SDL_Renderer *gl_rend;
    SDL_Window *gl_wind;
#endif
    SDL_Texture *gl_tex;
    int format; /* SDLTK_RGB24 etc */
    const char *title; /* malloc'd. Tk will set this for the wrapper */
    Region visRgnInParent; /* Contains the areas of the window which are
			    * not obscured by ancestors or children of
			    * ancestors.
			    * If the window or any ancestor is unmapped, the
			    * region is empty.
			    * For the root, this region covers the entire
			    * screen area.
			    * For a double-buffered toplevel, this region
			    * covers the entire area of the window.
			    */
    Region visRgn; 	/* Same as visRgnInParent, minus areas of any mapped
			 * children.
			 * This is the region used to restrict drawing in the
			 * window.
			 */
    Region dirtyRgn; /* For toplevels only. Holds the parts
		      * of the window that need to be updated
		      * on screen. */
};

/*
 * Frame rate (and timer) for periodic timer events.
 * Used for screen updates and in time stamps in X events.
 */
#define SDLTK_FRAMERATE 50

#ifdef ANDROID

/* Ring buffer for the last second of accelerometer values */
typedef struct {
    int index;
    long time;
    short values[SDLTK_FRAMERATE];
} AccelRing;

#endif

/* This event goes into Display.head event queue */
struct _XSQEvent {
    _XSQEvent *next;
    XEvent event;
};

#define IS_PIXMAP(d) (((_Pixmap *) (d))->type == DT_PIXMAP)
#define IS_WINDOW(d) (((_Window *) (d))->type == DT_WINDOW)
#define IS_ROOT(w) ((Window) w == SdlTkX.screen->root)
#define PARENT_IS_ROOT(w) IS_ROOT(((_Window *) w)->parent)

/* Flags for SdlTkXInfo.draw_later */
#define SDLTKX_DRAW    0x01
#define SDLTKX_DRAWALL 0x02
#define SDLTKX_PRESENT 0x04
#define SDLTKX_RENDCLR 0x08
#define SDLTKX_SCALED  0x10

typedef struct SdlTkXInfo {

    /* Counters */
    long frame_count;
    long time_count;

    /* SDL elements for rendering */
    SDL_Window *sdlscreen;
    SDL_Surface *sdlsurf;
    SDL_Renderer *sdlrend;
    SDL_Texture *sdltex;
    float scale, scale_min;
    SDL_Rect viewport;
    SDL_Rect *outrect;
    SDL_Rect outrect0;
    int root_w, root_h;

    /* Display/Screen/Window elements */
    Display *display;
    Screen *screen;
    int nwfree, nwtotal;
    _Window *wfree, *wtail;

    /* Geometry of decorative frames */
    int dec_frame_width;
    int dec_title_height;
    int dec_font_size;
    int dec_line_width;

    /* Focus/mouse handling */
    Window focus_window;
    Window focus_window_old;
    Window focus_window_not_override;
    int nearby_pixels;
    TkWindow *capture_window;
    _Window *mouse_window;
    _Window *keyboard_window;
    int mouse_x;
    int mouse_y;
    int sdlfocus;
    int keyuc;
    int cursor_change;
#ifndef ANDROID
    Tcl_HashTable sdlcursors;
#endif

    /* Screen refresh, life-cycle */
    Region screen_dirty_region;
    Region screen_update_region;
    int in_background;
    int draw_later;
    Tcl_ThreadId event_tid;

    /* Command line */
    char *arg_width;
    char *arg_height;
    int arg_fullscreen;
    int arg_resizable;
    int arg_noborder;
    int arg_nogl;
    int arg_xdpi;
    int arg_ydpi;
    int arg_opacity;
    char *arg_rootwidth;
    char *arg_rootheight;
    int arg_sdllog;
    char *arg_icon;
    int arg_nosysfonts;

    /* Various atoms */
    Atom mwm_atom;
    Atom nwmn_atom;
    Atom nwms_atom;
    Atom nwmsf_atom;
    Atom clipboard_atom;
    Atom comm_atom;
    Atom interp_atom;
    Atom tkapp_atom;
    Atom wm_prot_atom;
    Atom wm_dele_atom;

    /* Selection */
    Window current_primary;
    Window current_clipboard;

    /* Joysticks + Accelerometer */
    Tcl_HashTable joystick_table;
#ifdef ANDROID
    SDL_JoystickID accel_id;
    int accel_enabled;
    AccelRing accel_ring[3];
#endif

    /* OpenGL stuff */
#ifdef ANDROID
    SDL_GLContext gl_context;
#endif

} SdlTkXInfo;

extern SdlTkXInfo SdlTkX;

/* SdlTkInt.c */
extern void SdlTkSendViewportUpdate(void);
extern void SdlTkAttachTkWindow(Window w, TkWindow *tkwin);
extern void SdlTkScreenChanged(void);
extern void SdlTkScreenRefresh(void);
extern void SdlTkWaitLock(void);
extern void SdlTkWaitVSync(void);
extern int SdlTkGetMouseState(int *x, int *y);
extern int SdlTkTranslateEvent(SDL_Event *sdl_event, XEvent *event,
    unsigned long now_ms);
extern void SdlTkQueueEvent(XEvent *event);
extern void SdlTkGenerateConfigureNotify(Display *display, Window w);
extern void SdlTkGenerateExpose(Window w, int x, int y,
    int width, int height, int count);
extern void SdlTkRootCoords(_Window *_w, int *x, int *y);
extern _Window *SdlTkToplevelForWindow(_Window *_w, int *x, int *y);
extern _Window *SdlTkWrapperForWindow(_Window *_w);
extern _Window *SdlTkTopVisibleWrapper(void);
extern SDL_Surface *SdlTkGetDrawableSurface(Drawable d, int *x, int *y,
    int *format);
extern _Window *SdlTkPointToWindow(_Window *_w, int x, int y,
    Bool mapped, Bool depth);
extern void SdlTkRemoveFromParent(_Window *_w);
extern void SdlTkRestackWindow(_Window *_w, _Window *sibling, int stack_mode);
extern void SdlTkRestackTransients(_Window *_w);
extern void SdlTkBringToFrontIfNeeded(_Window *_w);
extern int SdlTkIsTransientOf(_Window *_w, _Window *other);
extern void SdlTkCalculateVisibleRegion(_Window *_w);
extern Region SdlTkGetVisibleRegion(_Window *_w);
#define VRC_DO_PARENT 0x0001
#define VRC_SELF_ONLY 0x0002
#define VRC_MOVE 0x0004
#define VRC_CHANGED 0x0008
#define VRC_EXPOSE 0x0010
#define VRC_DO_SIBLINGS 0x0020
extern void SdlTkVisRgnChanged(_Window *_w, int flags, int x, int y);
extern void SdlTkDirtyAll(Window w);
extern void SdlTkDirtyArea(Window w, int x, int y, int width, int height);
extern void SdlTkDirtyRegion(Window w, Region rgn);
extern int SdlTkGrabCheck(_Window *_w, int *othergrab);
extern void SdlTkSetCursor(TkpCursor cursor);
extern void SdlTkClearPointer(_Window *w);

/* SdlTkX.c */
extern void SdlTkLock(Display *display);
extern void SdlTkUnlock(Display *display);
extern void SdlTkPanInt(int x, int y);
extern int SdlTkZoomInt(int x, int y, float z);
extern int SdlTkPanZoom(int locked, int x, int y, int w, int h);
extern void SdlTkSetRootSize(int w, int h);
extern void SdlTkSetWindowFlags(int flags, int x, int y, int w, int h);
extern void SdlTkSetWindowOpacity(double opacity);
extern int SdlTkKeysym2Unicode(KeySym keysym);
extern KeySym SdlTkUnicode2Keysym(int ucs);
extern KeySym SdlTkUtf2KeySym(const char *utf, int len, int *lenret);
extern void SdlTkMoveWindow(Display *display, Window w, int x, int y);
extern void SdlTkMoveResizeWindow(Display *display, Window w, int x, int y,
    unsigned int width, unsigned int height);
extern void SdlTkResizeWindow(Display *display, Window w,
    unsigned int width, unsigned int height);
extern void SdlTkSetInputFocus(Display *display, Window focus, int revert_to,
    Time time);
extern void SdlTkSetSelectionOwner(Display *display, Atom selection,
   Window owner, Time time);
#ifndef _TKINTXLIBDECLS
extern int SdlTkGLXAvailable(Display *display);
extern void *SdlTkGLXCreateContext(Display *display, Window w,
   Tk_Window tkwin);
extern void SdlTkGLXDestroyContext(Display *display, Window w, void *ctx);
extern void SdlTkGLXMakeCurrent(Display *display, Window w, void *ctx);
extern void SdlTkGLXReleaseCurrent(Display *display, Window w, void *ctx);
extern void SdlTkGLXSwapBuffers(Display *display, Window w);
#endif
extern void SdlTkDumpXEvent(XEvent *eventPtr);

/* SdlTkAGG.c */
extern void SdlTkGfxDrawArc(Drawable d, GC gc, int x, int y,
    unsigned int width, unsigned int height, int start, int extent);
extern void SdlTkGfxDrawBitmap(Drawable src, Drawable dest, GC gc,
    int src_x, int src_y, unsigned int width, unsigned int height,
    int dest_x, int dest_y);
extern void SdlTkGfxDrawLines(Drawable d, GC gc, XPoint *points,
    int npoints, int mode);
extern void SdlTkGfxDrawPoint(Drawable d, GC gc, int x, int y);
extern void SdlTkGfxDrawRect(Drawable d, GC gc, int x, int y, int w, int h);
extern void SdlTkGfxFillArc(Drawable d, GC gc, int x, int y,
    unsigned int width, unsigned int height, int start, int extent);
extern void SdlTkGfxFillPolygon(Drawable d, GC gc, XPoint *points,
    int npoints, int shape, int mode);
extern void SdlTkGfxFillRegion(Drawable d, Region rgn, Uint32 pixel);
extern void SdlTkGfxFillRect(Drawable d, GC gc, int x, int y, int w, int h);
extern void SdlTkGfxInitFC(void);
extern void SdlTkGfxDeinitFC(void);
extern XFontStruct *SdlTkGfxAllocFontStruct(_Font *_f);
extern void SdlTkGfxDrawString(Drawable d, GC gc, int x, int y,
    const char *string, int length, double angle, int *xret, int *yret);
extern int SdlTkGfxTextWidth(Font f, const char *string, int length, int *maxw);

/* SdlTkGfx.c */
extern int SdlTkPixelFormat(SDL_Surface *sdl);
extern void SdlTkGfxBlitRegion(SDL_Surface *src, Region rgn,
    SDL_Surface *dest, int dest_x, int dest_y);
extern void SdlTkGfxCopyArea(Drawable src, Drawable dest, GC gc,
    int src_x, int src_y, unsigned int width, unsigned int height,
    int dest_x, int dest_y);
extern int SdlTkGfxClearRegion(Window w, Region dirtyRgn);
extern int SdlTkGfxExposeRegion(Window w, Region dirtyRgn);
extern void SdlTkGfxUpdateRegion(SDL_Renderer *rend,
    SDL_Texture *tex, SDL_Surface *surf, Region rgn);
extern void SdlTkGfxPresent(SDL_Renderer *rend, SDL_Texture *tex);
extern unsigned long SdlTkImageGetPixel(XImage *image, int x, int y);
extern int SdlTkImagePutPixel(XImage *image, int x, int y, unsigned long pixel);
extern int SdlTkImageDestroy(XImage *image);
extern void SdlTkGfxPutImage(Drawable d, Region r, XImage* image,
    int src_x, int src_y, int dest_x, int dest_y,
    unsigned int width, unsigned int height, int flipbw);

/* SdlTkUtils.c */
extern Region SdlTkRgnPoolGet(void);
extern void SdlTkRgnPoolFree(Region r);
extern int *SdlTkRgnPoolStat(void);
extern char **SdlTkListFonts(const char *xlfd, int *count);
extern Font SdlTkFontLoadXLFD(const char *xlfd);
extern int SdlTkFontInit(Tcl_Interp *interp);
extern int SdlTkFontAdd(Tcl_Interp *interp, const char *fileName);
extern int SdlTkFontList(Tcl_Interp *interp);
extern void SdlTkFontFreeFont(XFontStruct *fontStructPtr);
extern int SdlTkFontShrink(XFontStruct *fontStructPtr, int n, int *offsPtr);
extern int SdlTkFontRestore(XFontStruct *fontStructPtr, int n);
extern int SdlTkFontIsFixedWidth(XFontStruct *fontStructPtr);
extern int SdlTkFontHasChar(XFontStruct *fontStructPtr, char *buf);
extern int SdlTkFontCanDisplayChar(char *xlfd, TkFontAttributes *faPtr, int ch);
extern unsigned SdlTkGetNthGlyphIndex(_Font *_f, const char *s, int n);

/* decframe.c */
extern int SdlTkDecSetActive(_Window *_w, int active);
extern int SdlTkDecSetDraw(_Window *_w, int draw);
extern void SdlTkDecDrawFrame(_Window *_w);
extern int SdlTkDecFrameEvent(_Window *_w, SDL_Event *sdl_event, int x, int y);
extern void SdlTkDecCreate(_Window *_w);
extern void SdlTkDecDestroy(_Window *_w);

#define XSetEmptyRegion(r) XSubtractRegion(r,r,r)

/*
 * end block for C++
 */

#ifdef __cplusplus
}
#endif

#endif /* _SDLTKINT_H */

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
