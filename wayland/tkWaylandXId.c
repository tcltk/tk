/*
 * tkUnixXId.c --
 *
 * Copyright © 1993 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkUnixInt.h"
#include <nanovg.h>

/*
 * In Wayland with NanoVG, we need to track pixmap/bitmap data.
 * NanoVG doesn't have direct pixmap support, so we'll use NVGpaint
 * or create offscreen FBOs for complex cases.
 */

typedef struct {
    int imageId;           /* NanoVG image ID for texture-based pixmaps */
    NVGpaint paint;        /* NanoVG paint for gradient/solid pixmaps */
    int width;
    int height;
    int depth;
    int type;              /* 0 = image, 1 = paint, 2 = framebuffer */
    NVGframebuffer* fb;    /* For FBO-based pixmaps */
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
 *	For NanoVG, we can create images, paints, or framebuffers.
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
    Display *display,		/* Display for new pixmap (unused in Wayland). */
    Drawable d,			/* Drawable where pixmap will be used (unused). */
    int width, int height,	/* Dimensions of pixmap. */
    int depth)			/* Bits per pixel for pixmap. */
{
    TkPixmap *pixmap;
    
    if (nvgContext == NULL) {
        /* Fallback: return a simple identifier */
        return (Pixmap)(width + (height << 16));
    }
    
    /* Allocate new pixmap structure */
    if (pixmapCount >= pixmapCapacity) {
        pixmapCapacity = pixmapCapacity == 0 ? 16 : pixmapCapacity * 2;
        pixmapStore = (TkPixmap *)realloc(pixmapStore, 
                                         pixmapCapacity * sizeof(TkPixmap));
    }
    
    pixmap = &pixmapStore[pixmapCount];
    
    /* Initialize the pixmap structure */
    memset(pixmap, 0, sizeof(TkPixmap));
    pixmap->width = width;
    pixmap->height = height;
    pixmap->depth = depth;
    
    /* Default to creating an empty image in NanoVG */
    if (width > 0 && height > 0) {
        /* Create empty RGBA image data */
        unsigned char* data = (unsigned char*)calloc(width * height * 4, 1);
        if (data) {
            pixmap->imageId = nvgCreateImageRGBA(nvgContext, width, height, 
                                                NVG_IMAGE_NEAREST, data);
            free(data);
            pixmap->type = 0; /* Image type */
        }
    } else {
        /* For zero-size pixmaps, create a simple paint */
        pixmap->paint = nvgLinearGradient(nvgContext, 0, 0, 1, 1,
                                         nvgRGBA(0, 0, 0, 0),
                                         nvgRGBA(0, 0, 0, 0));
        pixmap->type = 1; /* Paint type */
    }
    
    pixmapCount++;
    
    /* Return the pointer as the pixmap ID */
    return (Pixmap)pixmap;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetPixmapAsFramebuffer --
 *
 *	Creates a pixmap as a NanoVG framebuffer for offscreen rendering.
 *
 * Results:
 *	Returns a pixmap identifier, or 0 on failure.
 *
 * Side effects:
 *	Creates an FBO in NanoVG.
 *
 *----------------------------------------------------------------------
 */

Pixmap
Tk_GetPixmapAsFramebuffer(
    Display *display,
    int width, int height,
    int imageFlags)
{
    TkPixmap *pixmap;
    
    if (nvgContext == NULL || width <= 0 || height <= 0) {
        return 0;
    }
    
    /* Allocate new pixmap structure */
    if (pixmapCount >= pixmapCapacity) {
        pixmapCapacity = pixmapCapacity == 0 ? 16 : pixmapCapacity * 2;
        pixmapStore = (TkPixmap *)realloc(pixmapStore, 
                                         pixmapCapacity * sizeof(TkPixmap));
    }
    
    pixmap = &pixmapStore[pixmapCount];
    
    /* Initialize the pixmap structure */
    memset(pixmap, 0, sizeof(TkPixmap));
    pixmap->width = width;
    pixmap->height = height;
    pixmap->depth = 32; /* FBOs are typically 32-bit RGBA */
    
    /* Create framebuffer */
    pixmap->fb = nvgCreateFramebuffer(nvgContext, width, height, imageFlags);
    if (pixmap->fb == NULL) {
        /* Clean up the slot */
        return 0;
    }
    
    pixmap->type = 2; /* Framebuffer type */
    pixmapCount++;
    
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
    Display *display,		/* Display for which pixmap was allocated (unused). */
    Pixmap pixmap)		/* Identifier for pixmap. */
{
    TkPixmap *pix = (TkPixmap *)pixmap;
    
    if (pix == NULL || nvgContext == NULL) {
        return;
    }
    
    /* Free resources based on type */
    switch (pix->type) {
        case 0: /* Image type */
            if (pix->imageId != 0) {
                nvgDeleteImage(nvgContext, pix->imageId);
            }
            break;
        case 2: /* Framebuffer type */
            if (pix->fb != NULL) {
                nvgDeleteFramebuffer(nvgContext, pix->fb);
            }
            break;
        case 1: /* Paint type - nothing to free */
        default:
            break;
    }
    
    /* Clear the structure */
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
     * a file descriptor or other resource ID */
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
 * Helper function to get NanoVG image ID from pixmap
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
 * Helper function to get NanoVG paint from pixmap
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
 * Helper function to get NanoVG framebuffer from pixmap
 */
NVGframebuffer*
Tk_GetPixmapFramebuffer(Pixmap pixmap)
{
    TkPixmap *pix = (TkPixmap *)pixmap;
    if (pix && pix->type == 2) {
        return pix->fb;
    }
    return NULL;
}

/*
 * Helper function to get pixmap type
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
 * Helper function to get pixmap dimensions
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
 * Update pixmap image data
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
    }
    
    pix->imageId = nvgCreateImageRGBA(nvgContext, pix->width, pix->height,
                                     NVG_IMAGE_NEAREST, data);
    return (pix->imageId != 0);
}

/*
 * Bind pixmap framebuffer for drawing
 */
void
Tk_BindPixmapFramebuffer(Pixmap pixmap)
{
    TkPixmap *pix = (TkPixmap *)pixmap;
    if (pix && pix->type == 2 && pix->fb && nvgContext) {
        nvgBindFramebuffer(nvgContext, pix->fb);
    }
}

/*
 * Cleanup function for pixmap store
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
        
        switch (pix->type) {
            case 0: /* Image type */
                if (pix->imageId != 0) {
                    nvgDeleteImage(nvgContext, pix->imageId);
                }
                break;
            case 2: /* Framebuffer type */
                if (pix->fb != NULL) {
                    nvgDeleteFramebuffer(nvgContext, pix->fb);
                }
                break;
            default:
                break;
        }
    }
    
    free(pixmapStore);
    pixmapStore = NULL;
    pixmapCount = 0;
    pixmapCapacity = 0;
    nvgContext = NULL;
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
