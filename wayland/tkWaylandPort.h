#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xlibint.h>
#include <X11/Xresource.h>
#include <X11/Xlocale.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <math.h>
#include <ctype.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <pwd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/file.h>
#ifdef HAVE_SYS_SELECT_H
#   include <sys/select.h>
#endif
#include <sys/stat.h>
#ifndef _TCL
#   include <tcl.h>
#endif
#ifdef HAVE_SYS_TIME_H
#	include <sys/time.h>
#endif
#include <time.h>
#include <inttypes.h>
#include <unistd.h>

/*
 * This macro stores a representation of the window handle in a string.
 */

#define TkpPrintWindowId(buf,w) \
	snprintf((buf), TCL_INTEGER_SPACE, "0x%lx", (unsigned long) (w))

/*
 * XParseColor (xlib/xcolors.c) uses this to fill in XColor.pixel.  The
 * Wayland port encodes pixels as 0x00RRGGBB (see TkpGetColor in
 * tkWaylandColor.c and TkWaylandPixelToNVG in tkWaylandInit.c), so decode
 * the 16-bit-per-channel XColor the same way.
 */

#define TkpGetPixel(p) (((((unsigned long)(p)->red >> 8) & 0xff) << 16) \
	| ((((unsigned long)(p)->green >> 8) & 0xff) << 8) \
	| (((unsigned long)(p)->blue >> 8) & 0xff))

/*
 * The following macro defines the type of the mask arguments to
 * select:
 */

#ifndef NO_FD_SET
#   define SELECT_MASK fd_set
#else
#   ifndef _AIX
	typedef long fd_mask;
#   endif
#   if defined(_IBMR2)
#	define SELECT_MASK void
#   else
#	define SELECT_MASK int
#   endif
#endif

/*
 * Used to tag functions that are only to be visible within the module being
 * built and not outside it (where this is supported by the linker).
 */

#ifndef MODULE_SCOPE
#   ifdef __cplusplus
#	define MODULE_SCOPE extern "C"
#   else
#	define MODULE_SCOPE extern
#   endif
#endif

/*
 * The following macro defines the number of fd_masks in an fd_set:
 */

#ifndef FD_SETSIZE
#   ifdef OPEN_MAX
#	define FD_SETSIZE OPEN_MAX
#   else
#	define FD_SETSIZE 256
#   endif
#endif
#if !defined(howmany)
#   define howmany(x, y) (((x)+((y)-1))/(y))
#endif
#ifndef NFDBITS
#   define NFDBITS NBBY*sizeof(fd_mask)
#endif
#define MASK_SIZE howmany(FD_SETSIZE, NFDBITS)

/*
 * Define "NBBY" (number of bits per byte) if it's not already defined.
 */

#ifndef NBBY
#   define NBBY 8
#endif


#define TkSetPixmapColormap(p,c) {}

/* We can draw directly into our backing store. */
#define TK_NO_DOUBLE_BUFFERING

/* We implement TkpPutRGBAImage */
#define TK_CAN_RENDER_RGBA

MODULE_SCOPE int TkpPutRGBAImage(
		     Display* display, Drawable drawable, GC gc, XImage* image,
		     int src_x, int src_y, int dest_x, int dest_y,
		     unsigned int width, unsigned int height);

/*
 * Platform hooks used by the generic pointer module (tkPointer.c), which
 * this port compiles in place of the X11 server-side pointer machinery.
 */

struct TkWindow;
MODULE_SCOPE void	TkpSetCursor(Cursor cursor);
MODULE_SCOPE void	TkpSetCapture(struct TkWindow *winPtr);
MODULE_SCOPE Tk_Window	TkpGetCapture(void);
MODULE_SCOPE void	TkPointerDeadWindow(struct TkWindow *winPtr);


/* This avoids having to implement XKeysymToString and XStringToKeysym */
#define REDO_KEYSYM_LOOKUP
