/*
 * tkWaylandXId.c --
 *
 * Copyright © 1993 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"           
#include <stddef.h>          
#include <X11/Xlib.h>  
#include <X11/Xatom.h>      
#include <GL/gl.h>
#include "nanovg.h"
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"   

/*
 * In Wayland with NanoVG, we need to track pixmap/bitmap data.
 * NanoVG doesn't have direct pixmap support, so we use NVG images
 * or simple NVGpaint objects.
 */

typedef struct {
    int imageId;           /* NanoVG image ID for texture-based pixmaps */
    NVGpaint paint;        /* NanoVG paint for gradient/solid/zero-size pixmaps */
    int width;
    int height;
    int depth;
    int type;              /* 0 = image, 1 = paint */
} TkPixmap;

static TkPixmap *pixmapStore = NULL;
static int pixmapCount = 0;
static int pixmapCapacity = 0;
static NVGcontext* nvgContext = NULL;
 
/*
 *----------------------------------------------------------------------
 *
 * Tk_SetNanoVGContext --
 *
 *	Sets the NanoVG context to be used for pixmap operations.
 *	This must be called before using any pixmap functions.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the global NanoVG context.
 *
 *----------------------------------------------------------------------
 */

void
Tk_SetNanoVGContext(NVGcontext* vg)
{
    nvgContext = vg;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetPixmap --
 *
 *	Creates a pixmap equivalent for Wayland/NanoVG.
 *	Now supports only image-based (type 0) or paint-based (type 1) pixmaps.
 *
 * Results:
 *	Returns a "pixmap" identifier (actually a pointer to TkPixmap struct).
 *
 * Side effects:
 *	Allocates memory for the pixmap structure.
 *
 *----------------------------------------------------------------------
 */

Pixmap
Tk_GetPixmap(
    TCL_UNUSED(Display *),		/* Display for new pixmap (unused in Wayland). */
    TCL_UNUSED(Drawable),		/* Drawable where pixmap will be used (unused). */
    int width, int height,	/* Dimensions of pixmap. */
    int depth)			/* Bits per pixel for pixmap. */
{
    TkPixmap *pixmap;
    
    if (nvgContext == NULL) {
        /* Fallback: return a simple identifier. */
        return (Pixmap)(width + (height << 16));
    }
    
    /* Allocate new pixmap structure. */
    if (pixmapCount >= pixmapCapacity) {
        pixmapCapacity = pixmapCapacity == 0 ? 16 : pixmapCapacity * 2;
        pixmapStore = (TkPixmap *)realloc(pixmapStore, 
                                         pixmapCapacity * sizeof(TkPixmap));
    }
    
    pixmap = &pixmapStore[pixmapCount];
    
    /* Initialize the pixmap structure. */
    memset(pixmap, 0, sizeof(TkPixmap));
    pixmap->width = width;
    pixmap->height = height;
    pixmap->depth = depth;
    
    if (width > 0 && height > 0) {
        /* Create empty RGBA image data. */
        unsigned char* data = (unsigned char*)calloc(width * height * 4, 1);
        if (data) {
            pixmap->imageId = nvgCreateImageRGBA(nvgContext, width, height, 
                                                NVG_IMAGE_NEAREST, data);
            ckfree(data);
            pixmap->type = 0; /* Image type */
        } else {
            /* Allocation failed → fallback to paint */
            pixmap->type = 1;
        }
    }
    
    if (pixmap->type != 0) {
        /* For zero-size or failed allocation: simple transparent paint */
        pixmap->paint = nvgLinearGradient(nvgContext, 0, 0, 1, 1,
                                         nvgRGBA(0, 0, 0, 0),
                                         nvgRGBA(0, 0, 0, 0));
        pixmap->type = 1; /* Paint type */
    }
    
    pixmapCount++;
    
    /* Return the pointer as the pixmap ID. */
    return (Pixmap)pixmap;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_FreePixmap --
 *
 *	Frees a pixmap created by Tk_GetPixmap.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The pixmap resources are freed.
 *
 *----------------------------------------------------------------------
 */

void
Tk_FreePixmap(
    TCL_UNUSED(Display *),		/* Display for which pixmap was allocated (unused). */
    Pixmap pixmap)		/* Identifier for pixmap. */
{
    TkPixmap *pix = (TkPixmap *)pixmap;
    
    if (pix == NULL || nvgContext == NULL) {
        return;
    }
    
    /* Free resources based on type. */
    if (pix->type == 0 && pix->imageId != 0) {
        nvgDeleteImage(nvgContext, pix->imageId);
    }
    /* type 1 (paint) needs no explicit cleanup */
    
    /* Clear the structure. */
    memset(pix, 0, sizeof(TkPixmap));
}

/*
 *----------------------------------------------------------------------
 *
 * TkpScanWindowId --
 *
 *	Given a string, produce the corresponding Window Id.
 *	In Wayland, window IDs are typically file descriptors or other
 *	identifiers from the Wayland protocol.
 *
 * Results:
 *	The return value is normally TCL_OK; in this case *idPtr will be set
 *	to the Window value equivalent to string. If string is improperly
 *	formed then TCL_ERROR is returned and an error message will be left in
 *	the interp's result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkpScanWindowId(
    Tcl_Interp *interp,
    const char *string,
    Window *idPtr)
{
    int code;
    Tcl_Obj obj;

    obj.refCount = 1;
    obj.bytes = (char *) string;
    obj.length = strlen(string);
    obj.typePtr = NULL;

    /* Parse as a long integer - in Wayland this might represent
     * a file descriptor or other resource ID. */
    code = Tcl_GetLongFromObj(interp, &obj, (long *)idPtr);

    if (obj.refCount > 1) {
	Tcl_Panic("invalid sharing of Tcl_Obj on C stack");
    }
    if (obj.typePtr && obj.typePtr->freeIntRepProc) {
	obj.typePtr->freeIntRepProc(&obj);
    }
    return code;
}

/*
 * Helper function to get NanoVG image ID from pixmap.
 */
int
Tk_GetPixmapImageId(Pixmap pixmap)
{
    TkPixmap *pix = (TkPixmap *)pixmap;
    if (pix && pix->type == 0) {
        return pix->imageId;
    }
    return 0;
}

/*
 * Helper function to get NanoVG paint from pixmap.
 */
NVGpaint*
Tk_GetPixmapPaint(Pixmap pixmap)
{
    TkPixmap *pix = (TkPixmap *)pixmap;
    if (pix && pix->type == 1) {
        return &pix->paint;
    }
    return NULL;
}

/*
 * Helper function to get pixmap type.
 */
int
Tk_GetPixmapType(Pixmap pixmap)
{
    TkPixmap *pix = (TkPixmap *)pixmap;
    if (pix) {
        return pix->type;
    }
    return -1;
}

/*
 * Helper function to get pixmap dimensions.
 */
void
Tk_GetPixmapDimensions(Pixmap pixmap, int *width, int *height, int *depth)
{
    TkPixmap *pix = (TkPixmap *)pixmap;
    if (pix) {
        if (width) *width = pix->width;
        if (height) *height = pix->height;
        if (depth) *depth = pix->depth;
    }
}

/*
 * Update pixmap image data (only for type 0).
 */
int
Tk_UpdatePixmapImage(Pixmap pixmap, const unsigned char* data)
{
    TkPixmap *pix = (TkPixmap *)pixmap;
    if (pix == NULL || nvgContext == NULL || pix->type != 0) {
        return 0;
    }
    
    if (pix->imageId != 0) {
        nvgDeleteImage(nvgContext, pix->imageId);
        pix->imageId = 0;
    }
    
    if (data) {
        pix->imageId = nvgCreateImageRGBA(nvgContext, pix->width, pix->height,
                                         NVG_IMAGE_NEAREST, data);
    }
    
    return (pix->imageId != 0);
}

/*
 * Cleanup function for pixmap store.
 */
void
Tk_CleanupPixmapStore(void)
{
    int i;
    
    if (nvgContext == NULL) {
        return;
    }
    
    for (i = 0; i < pixmapCount; i++) {
        TkPixmap *pix = &pixmapStore[i];
        
        if (pix->type == 0 && pix->imageId != 0) {
            nvgDeleteImage(nvgContext, pix->imageId);
        }
    }
    
    ckfree(pixmapStore);
    pixmapStore = NULL;
    pixmapCount = 0;
    pixmapCapacity = 0;
    nvgContext = NULL;
}

/*
 * Miscellaneous X11 functions required for compatibility. 
 * They are non-functional on Wayland. 
 */

void
TkUnixDoOneXEvent(void)
{
    /* no-op */
    return;
}

void
TkCreateXEventSource(void)
{
    /* no-op */
    return;
}

void
TkClipCleanup(TCL_UNUSED(TkDisplay *) /* dispPtr */)
{
    /* no-op */
    return;
}

void
TkUnixSetMenubar(TCL_UNUSED(Tk_Window) /* tkwin */,
		 TCL_UNUSED(Tk_Window) /* menubar */)
{
    /* no-op */
    return;
}

int
TkScrollWindow(
	       TCL_UNUSED(Tk_Window), /* tkwin */
	       TCL_UNUSED(GC), /* gc */
	       TCL_UNUSED(int), /* x */
	       TCL_UNUSED(int), /* y */
	       TCL_UNUSED(int), /* width */
	       TCL_UNUSED(int), /* height */
	       TCL_UNUSED(int), /* dx */
	       TCL_UNUSED(int), /* dy */
	       TCL_UNUSED(TkRegion)) /* damageRgn */
{
    /* no-op */
    return 0;
}

void
Tk_SetMainMenubar(TCL_UNUSED(Tcl_Interp *), /* interp */
		  TCL_UNUSED(Tk_Window), /* tkwin */
		  TCL_UNUSED(const char *)) /* menu name */
{
    /* no-op */
    return;
}

int
XGetWindowProperty(
    Display *display,
    TCL_UNUSED(Window),			/* w */
    TCL_UNUSED(Atom),			/* property */
    TCL_UNUSED(long),			/* long_offset */
    TCL_UNUSED(long),			/* long_length */
    TCL_UNUSED(Bool),			/* delete */
    TCL_UNUSED(Atom),			/* req_type */
    Atom *actual_type_return,
    int *actual_format_return,
    unsigned long *nitems_return,
    unsigned long *bytes_after_return,
    unsigned char **prop_return)
{
	
	  if (display == NULL) {
        fprintf(stderr, "WARNING: XGetWindowProperty called with NULL display!\n");
        fflush(stderr);
    }
    /* Return "property does not exist." */
    *actual_type_return = None;
    *actual_format_return = 0;
    *nitems_return = 0;
    *bytes_after_return = 0;
    *prop_return = NULL;
    return Success;  
}


char *
XResourceManagerString(
    TCL_UNUSED(Display *))		/* display */
{
	/* no-op */
    return NULL;
}

int
XFree(TCL_UNUSED(void *)) /* data */
{
    return 0;
}
typedef struct _XGC {
    int dummy;   /* Just so sizeof works */
} _XGC;


GC
XCreateGC(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Drawable),
    TCL_UNUSED(unsigned long),
    TCL_UNUSED(XGCValues *))
{
    GC gc;

    gc = (GC)ckalloc(sizeof(struct _XGC));
    memset(gc, 0, sizeof(struct _XGC));

    return gc;
}

int
XFreeGC(
    TCL_UNUSED(Display *),
    GC gc)
{
    if (gc) {
        ckfree((char *)gc);
    }
    return 0;
}

int
XChangeGC(
    TCL_UNUSED(Display *),
    TCL_UNUSED(GC),
    TCL_UNUSED(unsigned long),
    TCL_UNUSED(XGCValues *))
{
    return 0;
}

int
XCopyGC(
    TCL_UNUSED(Display *),
    TCL_UNUSED(GC),
    TCL_UNUSED(unsigned long),
    TCL_UNUSED(GC))
{
    return 0;
}

int
XSetForeground(
    TCL_UNUSED(Display *),		/* display */
    TCL_UNUSED(GC),			/* gc */
    TCL_UNUSED(unsigned long))		/* color */
{
    return 0;
}


int
XSetBackground(
    TCL_UNUSED(Display *),		/* display */
    TCL_UNUSED(GC),			/* gc */
    TCL_UNUSED(unsigned long))		/* color */
{
    return 0;
}

Atom
XInternAtom(
    TCL_UNUSED(Display *),		/* display */
    TCL_UNUSED(const char *),		/* atom_name */
    TCL_UNUSED(Bool))			/* only_if_exists */
{
	/* return dummy data */
    static Atom fakeAtom = 1;
    return fakeAtom++;
}


char *
XGetAtomName(
    TCL_UNUSED(Display *),		/* display */
    TCL_UNUSED(Atom))			/* atom */
{
	/* no-op */
    return NULL;
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
