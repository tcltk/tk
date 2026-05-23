#include <tk.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>

/* The core logic from your original script */
static int CaptureWindowToPPM_ObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "window_path filename");
        return TCL_ERROR;
    }

    /* Get the Tk_Window from the path (e.g., ".") */
    const char *windowPath = Tcl_GetString(objv[1]);
    const char *filename = Tcl_GetString(objv[2]);
    
    Tk_Window mainWin = Tk_MainWindow(interp);
    Tk_Window tkwin = Tk_NameToWindow(interp, windowPath, mainWin);

    if (tkwin == NULL) {
        return TCL_ERROR; // Tk_NameToWindow sets the error message
    }

    /* Ensure the window is actually mapped/visible */
    if (!Tk_WindowId(tkwin)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Window not realized/mapped", -1));
        return TCL_ERROR;
    }

    Display *display = Tk_Display(tkwin);
    Drawable drawable = Tk_WindowId(tkwin);
    int width = Tk_Width(tkwin);
    int height = Tk_Height(tkwin);

    XImage *image = XGetImage(display, drawable, 0, 0, width, height, AllPlanes, ZPixmap);
    if (!image) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Failed to get XImage", -1));
        return TCL_ERROR;
    }

    FILE *file = fopen(filename, "wb");
    if (!file) {
        XDestroyImage(image);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Could not open output file", -1));
        return TCL_ERROR;
    }

    /* Write PPM Header */
    fprintf(file, "P6\n%d %d\n255\n", width, height);

    /* Write Pixels directly without extra malloc for speed */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            unsigned long pixel = XGetPixel(image, x, y);
            unsigned char rgb[3];
            rgb[0] = (pixel >> 16) & 0xFF;
            rgb[1] = (pixel >> 8) & 0xFF;
            rgb[2] = pixel & 0xFF;
            fwrite(rgb, 3, 1, file);
        }
    }

    fclose(file);
    XDestroyImage(image);

    Tcl_SetObjResult(interp, Tcl_NewStringObj(filename, -1));
    return TCL_OK;
}

/* * Extension Initialization: This MUST be named [PackageName]_Init 
 * If your file is libtkcapture.so, the function is Tkcapture_Init.
 */
#ifdef __cplusplus
extern "C" {
#endif
DLLEXPORT int Tkcapture_Init(Tcl_Interp *interp) {
    /* Initialize Tcl and Tk stubs to ensure compatibility */
    if (Tcl_InitStubs(interp, "9.0", 0) == NULL) return TCL_ERROR;
    if (Tk_InitStubs(interp, "9.0", 0) == NULL) return TCL_ERROR;

    /* Register the new command: capture_window */
    Tcl_CreateObjCommand(interp, "capture_window", CaptureWindowToPPM_ObjCmd, NULL, NULL);

    /* Provide the package to Tcl */
    Tcl_PkgProvide(interp, "tkcapture", "1.0");
    return TCL_OK;
}
#ifdef __cplusplus
}
#endif