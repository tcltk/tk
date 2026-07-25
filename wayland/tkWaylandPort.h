/* We can draw directly into our backing store. */
#define TK_NO_DOUBLE_BUFFERING

/* We implement TkpPutRGBAImage */
#define TK_CAN_RENDER_RGBA

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


MODULE_SCOPE int TkpPutRGBAImage(
		     Display* display, Drawable drawable, GC gc, XImage* image,
		     int src_x, int src_y, int dest_x, int dest_y,
		     unsigned int width, unsigned int height);

/* This avoids having to implement XKeysymToString and XStringToKeysym */
#define REDO_KEYSYM_LOOKUP
