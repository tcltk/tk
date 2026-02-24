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
#include "tkGlfwInt.h"
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

/* Forward declarations. */
static void DeleteStressedCmap(Display *display, Colormap colormap);
static int  ParseColorString(const char *name, NVGcolor *color);

/*
 *----------------------------------------------------------------------
 *
 * TkpFreeColor --
 *
 *      Release a previously allocated TkColor structure.
 *
 *      In the NanoVG backend, colors themselves require no special
 *      freeing, but any associated stress colormap cache entries are
 *      cleaned up.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory associated with the TkColor is freed, and any stress
 *      colormap entry is removed from the display's cache.
 *
 *----------------------------------------------------------------------
 */

void
TkpFreeColor(TkColor *tkColPtr)
{
    if (tkColPtr->colormap != None) {
        DeleteStressedCmap(NULL, tkColPtr->colormap);
    }
    Tcl_Free((char *)tkColPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetColor --
 *
 *      Allocate a new TkColor structure for the color with the given name.
 *
 *      The color name may be either a standard X color name or a
 *      hexadecimal string (#RGB, #RRGGBB, #RRGGBBAA, or #RRRRGGGGBBBB).
 *
 * Results:
 *      Returns a pointer to a newly allocated TkColor structure, or
 *      NULL if the color name could not be parsed.
 *
 * Side effects:
 *      Memory is allocated for the TkColor structure.
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
    
    /* Convert NVGcolor back to XColor */
    xcolor.red   = (unsigned short)(nvgcolor.r * 65535.0f + 0.5f);
    xcolor.green = (unsigned short)(nvgcolor.g * 65535.0f + 0.5f);
    xcolor.blue  = (unsigned short)(nvgcolor.b * 65535.0f + 0.5f);
    xcolor.flags = DoRed | DoGreen | DoBlue;
    /* pixel and pad remain zero */

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
 *      Allocate a new TkColor structure for the color described by the
 *      given XColor structure.
 *
 *      In the NanoVG backend, exact RGB values are always available,
 *      so this function always succeeds (subject to memory allocation).
 *
 * Results:
 *      Returns a pointer to a newly allocated TkColor structure, or
 *      NULL if memory allocation fails.
 *
 * Side effects:
 *      Memory is allocated for the TkColor structure.
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
 *      Remove a colormap from the display's stress cache.
 *
 *      This is a stub implementation for the NanoVG backend, as
 *      colormap stress is not a concept in this environment. The
 *      function exists only for API compatibility.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The stress cache entry for the given colormap is removed if found.
 *
 *----------------------------------------------------------------------
 */

static void
DeleteStressedCmap(
    TCL_UNUSED(Display *), /* display*/
    Colormap colormap)          /* Colormap to remove from cache */
{
    TkDisplay *dispPtr;
    TkStressedCmap *prevPtr = NULL;
    TkStressedCmap *stressPtr;
    void *cmapPtr = (void *)colormap;

    dispPtr = TkGetDisplay(NULL);   /* may need real display later */
    if (dispPtr == NULL || dispPtr->stressPtr == NULL) {
        return;
    }

    for (stressPtr = dispPtr->stressPtr; stressPtr != NULL;
         prevPtr = stressPtr, stressPtr = stressPtr->nextPtr) {

        if (stressPtr->colormap == cmapPtr) {
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
 *      Determine whether a colormap is known to be out of entries.
 *
 *      This is a stub implementation for the NanoVG backend, always
 *      returning 0 (not stressed) as colormaps are not used.
 *
 * Results:
 *      0 always (colormap not stressed).
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
bool
TkpCmapStressed(
    Tk_Window tkwin,
    Colormap colormap)          /* Colormap to check (unsigned long) */
{
    TkDisplay *dispPtr = ((TkWindow *)tkwin)->dispPtr;
    TkStressedCmap *stressPtr;

    if (dispPtr == NULL) {
        return false;
    }

    for (stressPtr = dispPtr->stressPtr; stressPtr != NULL;
         stressPtr = stressPtr->nextPtr) {
        if (stressPtr->colormap == (void *)colormap) {
            return true;
        }
    }
    return false;
}

/*
 *----------------------------------------------------------------------
 *
 * ParseColorString --
 *
 *      Parse a color name or hexadecimal string into an NVGcolor structure.
 *
 *      Supported formats:
 *        - Named colors (e.g., "red", "blue", "SystemButtonFace")
 *        - #RGB (3-digit hexadecimal)
 *        - #RRGGBB (6-digit hexadecimal)
 *        - #RRGGBBAA (8-digit hexadecimal with alpha)
 *        - #RRRRGGGGBBBB (12-digit hexadecimal, X11 16-bit format)
 *
 * Results:
 *      1 if the color string was successfully parsed, 0 otherwise.
 *
 * Side effects:
 *      The NVGcolor structure pointed to by 'color' is filled with the
 *      parsed color components.
 *
 *----------------------------------------------------------------------
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
 *----------------------------------------------------------------------
 *
 * TkColorToNVG --
 *
 *      Extract an NVGcolor from a TkColor structure.
 *
 *      This is a convenience helper used by the drawing code to obtain
 *      NanoVG color values from Tk's internal color representation.
 *
 * Results:
 *      Returns an NVGcolor structure corresponding to the TkColor.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

NVGcolor
TkColorToNVG(TkColor *tkColPtr)
{
    return TkGlfwXColorToNVG(&tkColPtr->color);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
