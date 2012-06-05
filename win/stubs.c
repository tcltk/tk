#include "tk.h"

/*
 * Undocumented Xlib internal function
 */

int _XInitImageFuncPtrs(XImage *image)
{
    return Success;
}

/*
 * From Xutil.h
 */

void
XSetWMClientMachine(display, w, text_prop)
    Display* display;
    Window w;
    XTextProperty* text_prop;
{
}

Status
XStringListToTextProperty(list, count, text_prop_return)
    char** list;
    int count;
    XTextProperty* text_prop_return;
{
    return (Status) NULL;
}

/*
 * From Xlib.h
 */

int
XChangeProperty(display, w, property, type, format, mode, data, nelements)
    Display* display;
    Window w;
    Atom property;
    Atom type;
    int format;
    int mode;
    _Xconst unsigned char* data;
    int nelements;
{
    return Success;
}

Cursor
XCreateGlyphCursor(display, source_font, mask_font, source_char, mask_char,
	foreground_color, background_color)
    Display* display;
    Font source_font;
    Font mask_font;
    unsigned int source_char;
    unsigned int mask_char;
    XColor _Xconst* foreground_color;
    XColor _Xconst* background_color;
{
    return 1;
}

XIC
XCreateIC TCL_VARARGS_DEF(XIM,xim)
{
    return NULL;
}

Cursor
XCreatePixmapCursor(display, source, mask, foreground_color,
	background_color, x, y)
    Display* display;
    Pixmap source;
    Pixmap mask;
    XColor* foreground_color;
    XColor* background_color;
    unsigned int x;
    unsigned int y;
{
    return (Cursor) NULL;
}

int
XDeleteProperty(display, w, property)
    Display* display;
    Window w;
    Atom property;
{
    return Success;
}

void
XDestroyIC(ic)
    XIC ic;
{
}

Bool
XFilterEvent(event, window)
    XEvent* event;
    Window window;
{
    return 0;
}

int XForceScreenSaver(display, mode)
    Display* display;
    int mode;
{
    return Success;
}

int
XFreeCursor(display, cursor)
    Display* display;
    Cursor cursor;
{
    return Success;
}

GContext
XGContextFromGC(gc)
    GC gc;
{
    return (GContext) NULL;
}

char *
XGetAtomName(display, atom)
    Display* display;
    Atom atom;
{
    return NULL;
}

int
XGetWindowAttributes(display, w, window_attributes_return)
    Display* display;
    Window w;
    XWindowAttributes* window_attributes_return;
{
    return Success;
}

Status
XGetWMColormapWindows(display, w, windows_return, count_return)
    Display* display;
    Window w;
    Window** windows_return;
    int* count_return;
{
    return (Status) NULL;
}

int
XIconifyWindow(display, w, screen_number)
    Display* display;
    Window w;
    int screen_number;
{
    return Success;
}

XHostAddress *
XListHosts(display, nhosts_return, state_return)
    Display* display;
    int* nhosts_return;
    Bool* state_return;
{
    return NULL;
}

int
XLookupColor(display, colormap, color_name, exact_def_return,
	screen_def_return)
    Display* display;
    Colormap colormap;
    _Xconst char* color_name;
    XColor* exact_def_return;
    XColor* screen_def_return;
{
    return Success;
}

int
XNextEvent(display, event_return)
    Display* display;
    XEvent* event_return;
{
    return Success;
}

int
XPutBackEvent(display, event)
    Display* display;
    XEvent* event;
{
    return Success;
}

int
XQueryColors(display, colormap, defs_in_out, ncolors)
    Display* display;
    Colormap colormap;
    XColor* defs_in_out;
    int ncolors;
{
    return Success;
}

int
XQueryTree(display, w, root_return, parent_return, children_return,
	nchildren_return)
    Display* display;
    Window w;
    Window* root_return;
    Window* parent_return;
    Window** children_return;
    unsigned int* nchildren_return;
{
    return Success;
}

int
XRefreshKeyboardMapping(event_map)
    XMappingEvent* event_map;
{
    return Success;
}

Window
XRootWindow(display, screen_number)
    Display* display;
    int screen_number;
{
    return (Window) NULL;
}

int
XSelectInput(display, w, event_mask)
    Display* display;
    Window w;
    long event_mask;
{
    return Success;
}

int
XSendEvent(display, w, propagate, event_mask, event_send)
    Display* display;
    Window w;
    Bool propagate;
    long event_mask;
    XEvent* event_send;
{
    return Success;
}

int
XSetCommand(display, w, argv, argc)
    Display* display;
    Window w;
    char** argv;
    int argc;
{
    return Success;
}

XErrorHandler
XSetErrorHandler (handler)
    XErrorHandler handler;
{
    return NULL;
}

int
XSetIconName(display, w, icon_name)
    Display* display;
    Window w;
    _Xconst char* icon_name;
{
    return Success;
}

int
XSetWindowBackground(display, w, background_pixel)
    Display* display;
    Window w;
    unsigned long background_pixel;
{
    return Success;
}

int
XSetWindowBackgroundPixmap(display, w, background_pixmap)
    Display* display;
    Window w;
    Pixmap background_pixmap;
{
    return Success;
}

int
XSetWindowBorder(display, w, border_pixel)
    Display* display;
    Window w;
    unsigned long border_pixel;
{
    return Success;
}

int
XSetWindowBorderPixmap(display, w, border_pixmap)
    Display* display;
    Window w;
    Pixmap border_pixmap;
{
    return Success;
}

int
XSetWindowBorderWidth(display, w, width)
    Display* display;
    Window w;
    unsigned int width;
{
    return Success;
}

int
XSetWindowColormap(display, w, colormap)
    Display* display;
    Window w;
    Colormap colormap;
{
    return Success;
}

Bool
XTranslateCoordinates(display, src_w, dest_w, src_x, src_y, dest_x_return,
	dest_y_return, child_return)
    Display* display;
    Window src_w;
    Window dest_w;
    int src_x;
    int src_y;
    int* dest_x_return;
    int* dest_y_return;
    Window* child_return;
{
    return 0;
}

int
XWindowEvent(display, w, event_mask, event_return)
    Display* display;
    Window w;
    long event_mask;
    XEvent* event_return;
{
    return Success;
}

int
XWithdrawWindow(display, w, screen_number)
    Display* display;
    Window w;
    int screen_number;
{
    return Success;
}

int
XmbLookupString(ic, event, buffer_return, bytes_buffer, keysym_return,
	status_return)
    XIC ic;
    XKeyPressedEvent* event;
    char* buffer_return;
    int bytes_buffer;
    KeySym* keysym_return;
    Status* status_return;
{
    return Success;
}

int
XGetWindowProperty(display, w, property, long_offset, long_length, delete,
	req_type, actual_type_return, actual_format_return, nitems_return,
	bytes_after_return, prop_return)
    Display* display;
    Window w;
    Atom property;
    long long_offset;
    long long_length;
    Bool delete;
    Atom req_type;
    Atom* actual_type_return;
    int* actual_format_return;
    unsigned long* nitems_return;
    unsigned long* bytes_after_return;
    unsigned char** prop_return;
{
    *actual_type_return = None;
    *actual_format_return = 0;
    *nitems_return = 0;
    *bytes_after_return = 0;
    *prop_return = NULL;
    return BadValue;
}
