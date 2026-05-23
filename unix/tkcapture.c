#include <tk.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>

/* Implementation: capture_window <window_path> <filename> */
static int CaptureWindowToPPM_ObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "window_path filename");
        return TCL_ERROR;
    }

    const char *windowPath = Tcl_GetString(objv[1]);
    const char *filename = Tcl_GetString(objv[2]);
    
    Tk_Window mainWin = Tk_MainWindow(interp);
    Tk_Window tkwin = Tk_NameToWindow(interp, windowPath, mainWin);

    if (tkwin == NULL || !Tk_WindowId(tkwin)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Window not realized", -1));
        return TCL_ERROR;
    }

    Display *display = Tk_Display(tkwin);
    Drawable drawable = Tk_WindowId(tkwin);
    int width = Tk_Width(tkwin);
    int height = Tk_Height(tkwin);

    /* Grab the raw image from the X server */
    XImage *image = XGetImage(display, drawable, 0, 0, width, height, AllPlanes, ZPixmap);
    if (!image) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("XGetImage failed", -1));
        return TCL_ERROR;
    }

    FILE *file = fopen(filename, "wb");
    if (!file) {
        XDestroyImage(image);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("File open failed", -1));
        return TCL_ERROR;
    }

    /* PPM P6 Header: Binary RGB, 255 max color value */
    fprintf(file, "P6\n%d %d\n255\n", width, height);

    /* Iterate through pixels and write RGB bytes */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            unsigned long pixel = XGetPixel(image, x, y);
            unsigned char rgb[3];
            /* Standard X11 Pixel to RGB extraction */
            rgb[0] = (pixel >> 16) & 0xFF; // Red
            rgb[1] = (pixel >> 8) & 0xFF;  // Green
            rgb[2] = pixel & 0xFF;         // Blue
            fwrite(rgb, 3, 1, file);
        }
    }

    fclose(file);
    XDestroyImage(image);
    
    Tcl_SetObjResult(interp, Tcl_NewStringObj(filename, -1));
    return TCL_OK;
}

/* Tcl 9.1 Extension Init */
int Tkcapture_Init(Tcl_Interp *interp) {
    if (Tcl_InitStubs(interp, "9.0", 0) == NULL) return TCL_ERROR;
    if (Tk_InitStubs(interp, "9.0", 0) == NULL) return TCL_ERROR;

    Tcl_CreateObjCommand(interp, "capture_window", CaptureWindowToPPM_ObjCmd, NULL, NULL);
    Tcl_PkgProvide(interp, "tkcapture", "1.0");

    return TCL_OK;
}