#include "tkInt.h"

/*
 * Undocumented Xlib internal function
 */

int
_XInitImageFuncPtrs(
    TCL_UNUSED(XImage *))
{
    return Success;
}

/*
 * From Xutil.h
 */

void
XSetWMClientMachine(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(XTextProperty *))
{
}

Status
XStringListToTextProperty(
    TCL_UNUSED(char **),
    TCL_UNUSED(int),
    TCL_UNUSED(XTextProperty *))
{
    return Success;
}

/*
 * From Xlib.h
 */

int
XChangeProperty(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Atom),
    TCL_UNUSED(Atom),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(_Xconst unsigned char *),
    TCL_UNUSED(int))
{
    return Success;
}

XIC
XCreateIC(TCL_UNUSED(XIM), ...)
{
    return NULL;
}

int
XDeleteProperty(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Atom))
{
    return Success;
}

Bool
XFilterEvent(
    TCL_UNUSED(XEvent *),
    TCL_UNUSED(Window))
{
    return 0;
}

int
XForceScreenSaver(
    TCL_UNUSED(Display *),
    TCL_UNUSED(int))
{
    return Success;
}

int
XFreeCursor(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Cursor))
{
    return Success;
}

GContext
XGContextFromGC(
    TCL_UNUSED(GC))
{
    return (GContext) NULL;
}

char *
XGetAtomName(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Atom))
{
    return NULL;
}

int
XGetWindowAttributes(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(XWindowAttributes *))
{
    return Success;
}

Status
XGetWMColormapWindows(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Window **),
    TCL_UNUSED(int *))
{
    return Success;
}

int
XIconifyWindow(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(int))
{
    return Success;
}

XHostAddress *
XListHosts(
    TCL_UNUSED(Display *),
    TCL_UNUSED(int *),
    TCL_UNUSED(Bool *))
{
    return NULL;
}

int
XLookupColor(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Colormap),
    TCL_UNUSED(_Xconst char *),
    TCL_UNUSED(XColor *),
    TCL_UNUSED(XColor *))
{
    return Success;
}

int
XNextEvent(
    TCL_UNUSED(Display *),
    TCL_UNUSED(XEvent *))
{
    return Success;
}

int
XPutBackEvent(
    TCL_UNUSED(Display *),
    TCL_UNUSED(XEvent *))
{
    return Success;
}

int
XQueryColors(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Colormap),
    TCL_UNUSED(XColor *),
    TCL_UNUSED(int))
{
    return Success;
}

int
XQueryTree(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Window *),
    TCL_UNUSED(Window *),
    TCL_UNUSED(Window **),
    TCL_UNUSED(unsigned int *))
{
    return Success;
}

int
XRefreshKeyboardMapping(
    TCL_UNUSED(XMappingEvent *))
{
    return Success;
}

Window
XRootWindow(
    TCL_UNUSED(Display *),
    TCL_UNUSED(int))
{
    return (Window) NULL;
}

int
XSelectInput(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(long))
{
    return Success;
}

int
XSendEvent(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Bool),
    TCL_UNUSED(long),
    TCL_UNUSED(XEvent *))
{
    return Success;
}

int
XSetCommand(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(char **),
    TCL_UNUSED(int))
{
    return Success;
}

XErrorHandler
XSetErrorHandler(
    TCL_UNUSED(XErrorHandler))
{
    return NULL;
}

int
XSetIconName(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(_Xconst char *))
{
    return Success;
}

int
XSetWindowBackground(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(unsigned long))
{
    return Success;
}

int
XSetWindowBackgroundPixmap(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Pixmap))
{
    return Success;
}

int
XSetWindowBorder(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(unsigned long))
{
    return Success;
}

int
XSetWindowBorderPixmap(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Pixmap))
{
    return Success;
}

int
XSetWindowBorderWidth(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(unsigned int))
{
    return Success;
}

int
XSetWindowColormap(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Colormap))
{
    return Success;
}

Bool
XTranslateCoordinates(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Window),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int *),
    TCL_UNUSED(int *),
    TCL_UNUSED(Window *))
{
    return 0;
}

int
XWindowEvent(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(long),
    TCL_UNUSED(XEvent *))
{
    return Success;
}

int
XWithdrawWindow(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(int))
{
    return Success;
}

int
XmbLookupString(
    TCL_UNUSED(XIC),
    TCL_UNUSED(XKeyPressedEvent *),
    TCL_UNUSED(char *),
    TCL_UNUSED(int),
    TCL_UNUSED(KeySym *),
    TCL_UNUSED(Status *))
{
    return Success;
}

int
XGetWindowProperty(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Atom),
    TCL_UNUSED(long),
    TCL_UNUSED(long),
    TCL_UNUSED(Bool),
    TCL_UNUSED(Atom),
    Atom *actual_type_return,
    int *actual_format_return,
    unsigned long *nitems_return,
    unsigned long *bytes_after_return,
    unsigned char **prop_return)
{
    *actual_type_return = None;
    *actual_format_return = 0;
    *nitems_return = 0;
    *bytes_after_return = 0;
    *prop_return = NULL;
    return BadValue;
}

/*
 * The following functions were implemented as macros under Windows.
 */

int
XFlush(
    TCL_UNUSED(Display *))
{
    return 0;
}

int
XGrabServer(
    TCL_UNUSED(Display *))
{
    return 0;
}

int
XUngrabServer(
    TCL_UNUSED(Display *))
{
    return 0;
}

int
XFree(
    void *data)
{
	if (data != NULL) {
		ckfree(data);
	}
    return 0;
}

int
XNoOp(
    Display *display)
{
    LastKnownRequestProcessed(display)++;
    return 0;
}

XAfterFunction
XSynchronize(
    Display *display,
    TCL_UNUSED(Bool))
{
    LastKnownRequestProcessed(display)++;
    return NULL;
}

int
XSync(
    Display *display,
    TCL_UNUSED(Bool))
{
    LastKnownRequestProcessed(display)++;
    return 0;
}

VisualID
XVisualIDFromVisual(
    Visual *visual)
{
    return visual->visualid;
}

int
XOffsetRegion(
    TCL_UNUSED(Region),
	TCL_UNUSED(int),
	TCL_UNUSED(int))
{
	return 0;
}
