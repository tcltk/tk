/*
 * tkWaylandColor.c --
 *
 *      This file contains the platform specific color routines needed for
 *      Wayland/GLFW/NanoVG support.
 *
 * Copyright © 1996 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkColor.h"
#include "nanovg.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * In Wayland/GLFW/NanoVG, we don't have X-style colormaps.
 * We use RGBA colors directly with NanoVG.
 * We simulate minimal colormap stress behavior for Tk compatibility.
 */

struct TkStressedCmap {
    void          *colormap;        /* Placeholder for colormap ID */
    int            numColors;       /* Number of colors (placeholder) */
    NVGcolor      *colorPtr;        /* Array of colors (placeholder) */
    struct TkStressedCmap *nextPtr;
};

/* Convert XColor (16-bit) to NVGcolor (float 0..1). */
static NVGcolor
XColorToNVG(const XColor *xc)
{
    NVGcolor color;
    color.r = (float)xc->red   / 65535.0f;
    color.g = (float)xc->green / 65535.0f;
    color.b = (float)xc->blue  / 65535.0f;
    color.a = 1.0f;
    return color;
}

/* Convert NVGcolor to XColor (for Tk compatibility).
 *
 * IMPORTANT: caller must have zero-initialised xc before calling this,
 * so that xc->pixel and xc->pad are never left as stack garbage.
 * Tk uses the entire XColor struct as a hash key in Tk_GetGC; any
 * uninitialised bytes will corrupt the GC hash table.
 */
static void
NVGToXColor(const NVGcolor *nc, XColor *xc)
{
    xc->red   = (unsigned short)(nc->r * 65535.0f + 0.5f);
    xc->green = (unsigned short)(nc->g * 65535.0f + 0.5f);
    xc->blue  = (unsigned short)(nc->b * 65535.0f + 0.5f);
    xc->flags = DoRed | DoGreen | DoBlue;
    /* xc->pixel and xc->pad must be zero — ensured by caller's memset. */
}

/* Forward declarations. */
static void DeleteStressedCmap(Display *display, void *colormap);
static int  ParseColorString(const char *name, NVGcolor *color);

/*
 *----------------------------------------------------------------------
 *
 * TkpFreeColor --
 *
 *      Release the specified color back to the system.
 *      In NanoVG nothing needs to be freed, but we clean up stress cache.
 *
 *----------------------------------------------------------------------
 */

void
TkpFreeColor(TkColor *tkColPtr)
{
    if (tkColPtr->colormap != None) {
        DeleteStressedCmap(NULL, (Colormap *) tkColPtr->colormap);
    }
    Tcl_Free((char *)tkColPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetColor --
 *
 *      Allocate a new TkColor for the color with the given name.
 *
 * Results:
 *      Returns a newly allocated TkColor, or NULL on failure.
 *
 * Side effects:
 *      Allocates memory.
 *
 *----------------------------------------------------------------------
 */

TkColor *
TkpGetColor(
    TCL_UNUSED(Tk_Window), /* tkwin */
    const char *name)
{
    XColor   xcolor;
    NVGcolor nvgcolor;
    TkColor *tkColPtr;

    if (strlen(name) > 99) {
        return NULL;
    }

    if (!ParseColorString(name, &nvgcolor)) {
        return NULL;
    }

    /*
     * Zero-initialise the entire XColor before filling in fields.
     * Tk uses the whole struct (including pixel and pad) as a hash key
     * inside Tk_GetGC → CreateHashEntry.  Any uninitialised bytes cause
     * hash collisions, table corruption, and eventual heap corruption that
     * surfaces as crashes in completely unrelated code paths (e.g. the font
     * cache).
     */
    memset(&xcolor, 0, sizeof(XColor));
    NVGToXColor(&nvgcolor, &xcolor);

    tkColPtr = (TkColor *)Tcl_Alloc(sizeof(TkColor));
    if (tkColPtr == NULL) {
        return NULL;
    }

    tkColPtr->color            = xcolor;
    tkColPtr->colormap         = None;
    tkColPtr->screen           = NULL;
    tkColPtr->visual           = NULL;
    tkColPtr->resourceRefCount = 1;

    return tkColPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetColorByValue --
 *
 *      Given desired RGB, return a TkColor (NanoVG always gives exact match).
 *
 * Results:
 *      Returns a newly allocated TkColor, or NULL on failure.
 *
 * Side effects:
 *      Allocates memory.
 *
 *----------------------------------------------------------------------
 */

TkColor *
TkpGetColorByValue(
    TCL_UNUSED(Tk_Window), /* tkwin */
    XColor   *colorPtr)
{
    TkColor *tkColPtr;
    XColor   safeColor;

    /*
     * The incoming colorPtr may have uninitialised pixel/pad fields (e.g.
     * when called from Tk internals that only set red/green/blue).
     * Copy into a zero-initialised local to guarantee a clean hash key.
     */
    memset(&safeColor, 0, sizeof(XColor));
    safeColor.red   = colorPtr->red;
    safeColor.green = colorPtr->green;
    safeColor.blue  = colorPtr->blue;
    safeColor.flags = colorPtr->flags;
    /* pixel and pad remain zero */

    tkColPtr = (TkColor *)Tcl_Alloc(sizeof(TkColor));
    if (tkColPtr == NULL) {
        return NULL;
    }

    tkColPtr->color            = safeColor;
    tkColPtr->colormap         = None;
    tkColPtr->screen           = NULL;
    tkColPtr->visual           = NULL;
    tkColPtr->resourceRefCount = 1;

    return tkColPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * DeleteStressedCmap --
 *
 *      Release cached stress information for a colormap (stub).
 *
 *----------------------------------------------------------------------
 */

static void
DeleteStressedCmap(
    TCL_UNUSED(Display *), /* display*/
    void    *colormap)
{
    TkDisplay *dispPtr;
    TkStressedCmap *prevPtr = NULL;
    TkStressedCmap *stressPtr;

    dispPtr = TkGetDisplay(NULL);   /* may need real display later */
    if (dispPtr == NULL || dispPtr->stressPtr == NULL) {
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
            if (stressPtr->colorPtr) {
                Tcl_Free(stressPtr->colorPtr);
            }
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
 *      Check whether colormap is known to be out of entries (stub).
 *
 * Results:
 *      1 if stressed, 0 otherwise.
 *
 *----------------------------------------------------------------------
 */

int
TkpCmapStressed(
    Tk_Window tkwin,
    void     *colormap)
{
    TkDisplay *dispPtr = ((TkWindow *)tkwin)->dispPtr;
    TkStressedCmap *stressPtr;

    if (dispPtr == NULL) {
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
 * Helper functions.
 */

static int
ParseColorString(const char *name, NVGcolor *color)
{
    if (name[0] == '#') {
        unsigned int hex = 0;
        int len = strlen(name + 1);

        if (sscanf(name + 1, "%x", &hex) != 1) {
            return 0;
        }

        if (len == 3) {         /* #RGB — expand each nibble */
            color->r = ((((hex >> 8) & 0xF) * 0x11)) / 255.0f;
            color->g = ((((hex >> 4) & 0xF) * 0x11)) / 255.0f;
            color->b = ((( hex       & 0xF) * 0x11)) / 255.0f;
            color->a = 1.0f;
            return 1;
        }
        if (len == 6) {         /* #RRGGBB */
            color->r = ((hex >> 16) & 0xFF) / 255.0f;
            color->g = ((hex >>  8) & 0xFF) / 255.0f;
            color->b = ( hex        & 0xFF) / 255.0f;
            color->a = 1.0f;
            return 1;
        }
        if (len == 8) {         /* #RRGGBBAA */
            color->r = ((hex >> 24) & 0xFF) / 255.0f;
            color->g = ((hex >> 16) & 0xFF) / 255.0f;
            color->b = ((hex >>  8) & 0xFF) / 255.0f;
            color->a = ( hex        & 0xFF) / 255.0f;
            return 1;
        }
        if (len == 12) {        /* #RRRRGGGGBBBB (X11 16-bit per channel) */
            unsigned int r, g, b;
            if (sscanf(name + 1, "%4x%4x%4x", &r, &g, &b) == 3) {
                color->r = r / 65535.0f;
                color->g = g / 65535.0f;
                color->b = b / 65535.0f;
                color->a = 1.0f;
                return 1;
            }
        }
        return 0;
    }

    /* Named colors — extended to cover the full set Tk uses at startup. */
    static const struct { const char *name; float r, g, b; } named[] = {
        { "red",                1.000f, 0.000f, 0.000f },
        { "green",              0.000f, 0.502f, 0.000f },
        { "blue",               0.000f, 0.000f, 1.000f },
        { "white",              1.000f, 1.000f, 1.000f },
        { "black",              0.000f, 0.000f, 0.000f },
        { "gray",               0.502f, 0.502f, 0.502f },
        { "grey",               0.502f, 0.502f, 0.502f },
        { "yellow",             1.000f, 1.000f, 0.000f },
        { "cyan",               0.000f, 1.000f, 1.000f },
        { "magenta",            1.000f, 0.000f, 1.000f },
        { "orange",             1.000f, 0.647f, 0.000f },
        { "pink",               1.000f, 0.753f, 0.796f },
        { "purple",             0.502f, 0.000f, 0.502f },
        { "brown",              0.647f, 0.165f, 0.165f },
        { "navy",               0.000f, 0.000f, 0.502f },
        { "teal",               0.000f, 0.502f, 0.502f },
        { "maroon",             0.502f, 0.000f, 0.000f },
        { "lime",               0.000f, 1.000f, 0.000f },
        { "aqua",               0.000f, 1.000f, 1.000f },
        { "fuchsia",            1.000f, 0.000f, 1.000f },
        { "silver",             0.753f, 0.753f, 0.753f },
        { "gold",               1.000f, 0.843f, 0.000f },
        { "coral",              1.000f, 0.498f, 0.314f },
        { "salmon",             0.980f, 0.502f, 0.447f },
        { "turquoise",          0.251f, 0.878f, 0.816f },
        { "violet",             0.933f, 0.510f, 0.933f },
        { "indigo",             0.294f, 0.000f, 0.510f },
        { "tan",                0.824f, 0.706f, 0.549f },
        { "khaki",              0.941f, 0.902f, 0.549f },
        { "beige",              0.961f, 0.961f, 0.863f },
        { "ivory",              1.000f, 1.000f, 0.941f },
        { "lavender",           0.902f, 0.902f, 0.980f },
        { "linen",              0.980f, 0.941f, 0.902f },
        { "snow",               1.000f, 0.980f, 0.980f },
        { "wheat",              0.961f, 0.871f, 0.702f },
        { "chocolate",          0.824f, 0.412f, 0.118f },
        { "tomato",             1.000f, 0.388f, 0.278f },
        { "orchid",             0.855f, 0.439f, 0.839f },
        { "plum",               0.867f, 0.627f, 0.867f },
        { "sienna",             0.627f, 0.322f, 0.176f },
        { "gray0",              0.000f, 0.000f, 0.000f },
        { "gray100",            1.000f, 1.000f, 1.000f },
        /* Tk startup uses these specific grays */
        { "gray85",             0.851f, 0.851f, 0.851f },
        { "gray90",             0.898f, 0.898f, 0.898f },
        { "gray75",             0.749f, 0.749f, 0.749f },
        { "gray50",             0.502f, 0.502f, 0.502f },
        { "gray25",             0.251f, 0.251f, 0.251f },
        { "grey85",             0.851f, 0.851f, 0.851f },
        { "grey90",             0.898f, 0.898f, 0.898f },
        { "grey75",             0.749f, 0.749f, 0.749f },
        { "grey50",             0.502f, 0.502f, 0.502f },
        { "grey25",             0.251f, 0.251f, 0.251f },
        { "SystemButtonFace",   0.878f, 0.878f, 0.878f },
        { "SystemButtonText",   0.000f, 0.000f, 0.000f },
        { "SystemHighlight",    0.000f, 0.475f, 0.843f },
        { "SystemHighlightText",1.000f, 1.000f, 1.000f },
        { "SystemWindow",       1.000f, 1.000f, 1.000f },
        { "SystemWindowText",   0.000f, 0.000f, 0.000f },
        { NULL, 0, 0, 0 }
    };

    for (int i = 0; named[i].name != NULL; i++) {
        if (strcasecmp(name, named[i].name) == 0) {
            color->r = named[i].r;
            color->g = named[i].g;
            color->b = named[i].b;
            color->a = 1.0f;
            return 1;
        }
    }

    return 0;
}

/*
 * Get NVGcolor from TkColor (used by drawing code).
 */
NVGcolor
TkColorToNVG(TkColor *tkColPtr)
{
    return XColorToNVG(&tkColPtr->color);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
