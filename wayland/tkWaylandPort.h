/* We can draw directly into our backing store. */
#define TK_NO_DOUBLE_BUFFERING

/* We implement TkpPutRGBAImage */
#define TK_CAN_RENDER_RGBA

MODULE_SCOPE int TkpPutRGBAImage(
		     Display* display, Drawable drawable, GC gc, XImage* image,
		     int src_x, int src_y, int dest_x, int dest_y,
		     unsigned int width, unsigned int height);

/* This avoids having to implement XKeysymToString and XStringToKeysym */
#define REDO_KEYSYM_LOOKUP
