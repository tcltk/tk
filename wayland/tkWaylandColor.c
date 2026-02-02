/*
 * tkWaylandColor.c --
 *
 *	This file contains the platform specific color routines needed for
 *	Wayland/GLFW/NanoVG support.
 *
 * Copyright © 1996 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkColor.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * In Wayland/GLFW/NanoVG, we don't have X-style colormaps. Instead, we use
 * RGBA colors directly with NanoVG. We'll simulate colormap stress behavior
 * for compatibility.
 */

struct TkStressedCmap {
    void *colormap;		/* Placeholder for colormap ID */
    int numColors;		/* Number of colors in simulated colormap */
    NVGcolor *colorPtr;		/* Array of colors in the colormap */
    struct TkStressedCmap *nextPtr;
};

/* NanoVG uses RGBA colors in float format (0.0-1.0). */
typedef struct {
    float r, g, b, a;
} NVGcolor;

/* Convert XColor (16-bit) to NVGcolor (float). */
static NVGcolor XColorToNVG(const XColor *xc) {
    NVGcolor color;
    color.r = (float)xc->red / 65535.0f;
    color.g = (float)xc->green / 65535.0f;
    color.b = (float)xc->blue / 65535.0f;
    color.a = 1.0f;
    return color;
}

/* Convert NVGcolor to XColor. */
static void NVGToXColor(const NVGcolor *nc, XColor *xc) {
    xc->red = (unsigned short)(nc->r * 65535.0f);
    xc->green = (unsigned short)(nc->g * 65535.0f);
    xc->blue = (unsigned short)(nc->b * 65535.0f);
    xc->flags = DoRed | DoGreen | DoBlue;
}

/*
 * Forward declarations for functions defined in this file:
 */

static void DeleteStressedCmap(Display *display, void *colormap);
static void FindClosestColor(Tk_Window tkwin, XColor *desiredColorPtr, 
                             XColor *actualColorPtr);
static int ParseColorString(const char *name, NVGcolor *color);
static NVGcolor FindClosestNVGColor(Tk_Window tkwin, const NVGcolor *desired);

/*
 *----------------------------------------------------------------------
 *
 * TkpFreeColor --
 *
 *	Release the specified color back to the system.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Invalidates the colormap cache for the colormap associated with the
 *	given color.
 *
 *----------------------------------------------------------------------
 */

void
TkpFreeColor(
    TkColor *tkColPtr)
{
    /* 
     * In NanoVG, colors are just values, no allocation needed.
     * We still maintain the stressed colormap cache for compatibility. 
     */
    DeleteStressedCmap(NULL, tkColPtr->colormap);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetColor --
 *
 *	Allocate a new TkColor for the color with the given name.
 *
 * Results:
 *	Returns a newly allocated TkColor, or NULL on failure.
 *
 * Side effects:
 *	May invalidate the colormap cache associated with tkwin upon
 *	allocating a new colormap entry. Allocates a new TkColor structure.
 *
 *----------------------------------------------------------------------
 */

TkColor *
TkpGetColor(
    Tk_Window tkwin,
    const char *name)
{
    XColor xcolor;
    NVGcolor nvgcolor;
    TkColor *tkColPtr;
    
    if (strlen(name) > 99) {
        return NULL;
    }
    
    /* Parse color name to NVGcolor. */
    if (ParseColorString(name, &nvgcolor) == 0) {
        return NULL;
    }
    
    /* Convert to XColor for compatibility. */
    NVGToXColor(&nvgcolor, &xcolor);
    
    /* Create TkColor structure. */
    tkColPtr = (TkColor *)Tcl_Alloc(sizeof(TkColor));
    if (!tkColPtr) {
        return NULL;
    }
    
    tkColPtr->color = xcolor;
    tkColPtr->colormap = NULL;  /* Not used in NanoVG */
    tkColPtr->screen = NULL;
    tkColPtr->visual = NULL;
    tkColPtr->resourceRefCount = 1;
    
    /* Store NVG color in extension field if available. */
    if (sizeof(TkColor) > sizeof(struct TkColor_)) {
        /* Assuming TkColor has an extension field. */
        memcpy(((char*)tkColPtr) + sizeof(struct TkColor_), 
               &nvgcolor, sizeof(NVGcolor));
    }
    
    return tkColPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetColorByValue --
 *
 *	Given a desired set of red-green-blue intensities for a color, locate
 *	a pixel value to use to draw that color in a given window.
 *
 * Results:
 *	The return value is a pointer to an TkColor structure that indicates
 *	the closest red, blue, and green intensities available to those
 *	specified in colorPtr, and also specifies a pixel value to use to draw
 *	in that color.
 *
 * Side effects:
 *	May invalidate the colormap cache for the specified window. Allocates
 *	a new TkColor structure.
 *
 *----------------------------------------------------------------------
 */

TkColor *
TkpGetColorByValue(
    Tk_Window tkwin,
    XColor *colorPtr)
{
    TkColor *tkColPtr = (TkColor *)Tcl_Alloc(sizeof(TkColor));
    if (!tkColPtr) {
        return NULL;
    }
    
    /* In NanoVG, we can always use the exact color. */
    tkColPtr->color = *colorPtr;
    tkColPtr->colormap = NULL;
    tkColPtr->screen = NULL;
    tkColPtr->visual = NULL;
    tkColPtr->resourceRefCount = 1;
    
    /* Convert to NVGcolor and store in extension. */
    NVGcolor nvgcolor = XColorToNVG(colorPtr);
    if (sizeof(TkColor) > sizeof(struct TkColor_)) {
        memcpy(((char*)tkColPtr) + sizeof(struct TkColor_), 
               &nvgcolor, sizeof(NVGcolor));
    }
    
    return tkColPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * FindClosestColor --
 *
 *	When Tk can't allocate a color because a colormap has filled up, this
 *	function is called to find and allocate the closest available color in
 *	the colormap.
 *
 * Results:
 *	There is no return value, but *actualColorPtr is filled in with
 *	information about the closest available color in tkwin's colormap.
 *
 * Side effects:
 *	A color is allocated.
 *
 *----------------------------------------------------------------------
 */

static void
FindClosestColor(
    Tk_Window tkwin,
    XColor *desiredColorPtr,
    XColor *actualColorPtr)
{
    /* Convert to NVGcolor. */
    NVGcolor desired = XColorToNVG(desiredColorPtr);
    
    /* Find closest color (simulated for compatibility). */
    NVGcolor actual = FindClosestNVGColor(tkwin, &desired);
    
    /* Convert back to XColor. */
    NVGToXColor(&actual, actualColorPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * DeleteStressedCmap --
 *
 *	This function releases the information cached for "colormap" so that
 *	it will be refetched from the X server the next time it is needed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The TkStressedCmap structure for colormap is deleted; the colormap is
 *	no longer considered to be "stressed".
 *
 *----------------------------------------------------------------------
 */

static void
DeleteStressedCmap(
    Display *display,
    void *colormap)
{
    /* 
     * Implementation depends on how TkDisplay is structured in port.
     *  For now, simple placeholder.
     */
    
    TkDisplay *dispPtr = TkGetDisplay(display);
    TkStressedCmap *prevPtr = NULL;
    TkStressedCmap *stressPtr;
    
    if (!dispPtr || !dispPtr->stressPtr) {
        return;
    }
    
    for (stressPtr = dispPtr->stressPtr; stressPtr != NULL;
         prevPtr = stressPtr, stressPtr = stressPtr->nextPtr) {
        if (stressPtr->colormap == colormap) {
            if (prevPtr == NULL) {
                dispPtr->stressPtr = stressPtr->nextPtr;
            } else {
                prevPtr->nextPtr = stressPtr->nextPtr;
            }
            Tcl_Free(stressPtr->colorPtr);
            Tcl_Free(stressPtr);
            return;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpCmapStressed --
 *
 *	Check to see whether a given colormap is known to be out of entries.
 *
 * Results:
 *	1 is returned if "colormap" is stressed, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#undef TkpCmapStressed
int
TkpCmapStressed(
    Tk_Window tkwin,
    void *colormap)
{
    TkStressedCmap *stressPtr;
    TkDisplay *dispPtr = ((TkWindow *) tkwin)->dispPtr;
    
    if (!dispPtr) {
        return 0;
    }
    
    for (stressPtr = dispPtr->stressPtr; stressPtr != NULL; 
         stressPtr = stressPtr->nextPtr) {
        if (stressPtr->colormap == colormap) {
            return 1;
        }
    }
    return 0;
}

/*
 * Helper functions for Wayland/GLFW/NanoVG
 */

static int ParseColorString(const char *name, NVGcolor *color)
{
    /* Simplified color parsing for NanoVG. */
    if (name[0] == '#') {
        unsigned int hex;
        if (sscanf(name + 1, "%x", &hex) == 1) {
            int len = strlen(name + 1);
            if (len == 6) {  /* #RRGGBB */
                color->r = ((hex >> 16) & 0xFF) / 255.0f;
                color->g = ((hex >> 8) & 0xFF) / 255.0f;
                color->b = (hex & 0xFF) / 255.0f;
                color->a = 1.0f;
                return 1;
            } else if (len == 8) {  /* #RRGGBBAA */
                color->r = ((hex >> 24) & 0xFF) / 255.0f;
                color->g = ((hex >> 16) & 0xFF) / 255.0f;
                color->b = ((hex >> 8) & 0xFF) / 255.0f;
                color->a = (hex & 0xFF) / 255.0f;
                return 1;
            }
        }
    }
    
    /* Named colors. */
    struct ColorName {
        const char *name;
        NVGcolor color;
    };
    
    static const struct ColorName colors[] = {
        {"red", {1.0f, 0.0f, 0.0f, 1.0f}},
        {"green", {0.0f, 1.0f, 0.0f, 1.0f}},
        {"blue", {0.0f, 0.0f, 1.0f, 1.0f}},
        {"white", {1.0f, 1.0f, 1.0f, 1.0f}},
        {"black", {0.0f, 0.0f, 0.0f, 1.0f}},
        {"gray", {0.5f, 0.5f, 0.5f, 1.0f}},
        {NULL, {0.0f, 0.0f, 0.0f, 0.0f}}
    };
    
    for (int i = 0; colors[i].name != NULL; i++) {
        if (strcasecmp(name, colors[i].name) == 0) {
            *color = colors[i].color;
            return 1;
        }
    }
    
    return 0;
}

static NVGcolor FindClosestNVGColor(Tk_Window tkwin, const NVGcolor *desired)
{
    /* 
     * In NanoVG, we can always return the desired color. 
     * This function maintains compatibility with the X11 interface.
     */
    return *desired;
}

/*
 * Additional NanoVG-specific functions
 */

/* Get NVGcolor from TkColor. */
NVGcolor TkColorToNVG(TkColor *tkColPtr) {
    if (sizeof(TkColor) > sizeof(struct TkColor_)) {
        /* Retrieve from extension field */
        NVGcolor color;
        memcpy(&color, ((char*)tkColPtr) + sizeof(struct TkColor_), 
               sizeof(NVGcolor));
        return color;
    } else {
        /* Fall back to conversion from XColor. */
        return XColorToNVG(&tkColPtr->color);
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
