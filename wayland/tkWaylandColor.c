/*
 * tkWaylandColor.c --
 *
 *      This file contains the platform specific color routines needed for
 *      Wayland/GLFW/libcg support.
 *
 * Copyright © 1996 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkColor.h"
#include "tkWaylandInt.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * In Wayland/GLFW/libcg we don't have X-style colormaps.
 * Colors are plain cg_color_t structs.
 * We simulate minimal colormap stress behavior for Tk compatibility.
 */

struct TkStressedCmap {
    void                  *colormap;   /* Placeholder for colormap ID */
    int                    numColors;  /* Placeholder */
    struct cg_color_t     *colorPtr;   /* Array of colors (placeholder) */
    struct TkStressedCmap *nextPtr;
};

/* Forward declarations. */
static void DeleteStressedCmap(Display *display, Colormap colormap);
static int  ParseColorString(const char *name, struct cg_color_t *color);

/*
 *----------------------------------------------------------------------
 *
 * TkpFreeColor --
 *
 *      Release a previously allocated TkColor structure.
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
 *      Allocate a new TkColor for the color with the given name.
 *
 *      Supported formats: named colors, #RGB, #RRGGBB, #RRGGBBAA,
 *      #RRRRGGGGBBBB.
 *
 * Results:
 *      Returns a newly allocated TkColor, or NULL on parse failure.
 *
 * Side effects:
 *      Memory is allocated for the TkColor structure.
 *
 *----------------------------------------------------------------------
 */

TkColor *
TkpGetColor(
    TCL_UNUSED(Tk_Window),
    const char *name)
{
    XColor            xcolor;
    struct cg_color_t cgcolor;
    TkColor          *tkColPtr;

    if (strlen(name) > 99) {
        return NULL;
    }

    if (!ParseColorString(name, &cgcolor)) {
        return NULL;
    }

    /*
     * Zero-initialise the entire XColor before filling fields.
     * Tk uses the whole struct (including pixel and pad) as a hash key
     * inside Tk_GetGC → CreateHashEntry.  Uninitialised bytes cause
     * hash collisions and heap corruption.
     */
    memset(&xcolor, 0, sizeof(XColor));

    xcolor.red   = (unsigned short)(cgcolor.r * 65535.0 + 0.5);
    xcolor.green = (unsigned short)(cgcolor.g * 65535.0 + 0.5);
    xcolor.blue  = (unsigned short)(cgcolor.b * 65535.0 + 0.5);
    xcolor.flags = DoRed | DoGreen | DoBlue;

    /* Encode RGB into pixel as 0x00RRGGBB so GC foreground values
     * can be decoded back to color by TkGlfwPixelToCG. */
    xcolor.pixel =
        (((unsigned long)(cgcolor.r * 255.0 + 0.5)) << 16) |
        (((unsigned long)(cgcolor.g * 255.0 + 0.5)) <<  8) |
         ((unsigned long)(cgcolor.b * 255.0 + 0.5));

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
 *      Allocate a new TkColor for the color described by an XColor.
 *
 * Results:
 *      Returns a newly allocated TkColor, or NULL on allocation failure.
 *
 * Side effects:
 *      Memory is allocated for the TkColor structure.
 *
 *----------------------------------------------------------------------
 */

TkColor *
TkpGetColorByValue(
    TCL_UNUSED(Tk_Window),
    XColor *colorPtr)
{
    TkColor *tkColPtr;
    XColor   safeColor;

    /*
     * The incoming colorPtr may have uninitialised pixel/pad fields.
     * Copy into a zero-initialised local to guarantee a clean hash key.
     */
    memset(&safeColor, 0, sizeof(XColor));
    safeColor.red   = colorPtr->red;
    safeColor.green = colorPtr->green;
    safeColor.blue  = colorPtr->blue;
    safeColor.flags = colorPtr->flags;

    /* Encode pixel from the 16-bit channel values. */
    safeColor.pixel =
        (((unsigned long)(safeColor.red   >> 8)) << 16) |
        (((unsigned long)(safeColor.green >> 8)) <<  8) |
         ((unsigned long)(safeColor.blue  >> 8));

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
 *      Stub for libcg backend — colormaps are not used.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The cache entry for the colormap is removed if found.
 *
 *----------------------------------------------------------------------
 */

static void
DeleteStressedCmap(
    TCL_UNUSED(Display *),
    Colormap colormap)
{
    TkDisplay     *dispPtr;
    TkStressedCmap *prevPtr = NULL;
    TkStressedCmap *stressPtr;
    void          *cmapPtr  = (void *)colormap;

    dispPtr = TkGetDisplay(NULL);
    if (dispPtr == NULL || dispPtr->stressPtr == NULL) {
        return;
    }

    for (stressPtr = dispPtr->stressPtr; stressPtr != NULL;
         prevPtr = stressPtr, stressPtr = stressPtr->nextPtr) {
        if (stressPtr->colormap == cmapPtr) {
            if (prevPtr == NULL)
                dispPtr->stressPtr = stressPtr->nextPtr;
            else
                prevPtr->nextPtr   = stressPtr->nextPtr;
            if (stressPtr->colorPtr)
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
 *      Determine whether a colormap is out of entries.
 *      Always returns 0 — colormaps are not used in the libcg backend.
 *
 * Results:
 *      0 always.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

bool
TkpCmapStressed(
    Tk_Window tkwin,
    Colormap  colormap)
{
    TkDisplay     *dispPtr   = ((TkWindow *)tkwin)->dispPtr;
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
 *      Parse a color name or hex string into a cg_color_t.
 *
 *      Supported formats:
 *        Named colors (e.g. "red", "SystemButtonFace")
 *        #RGB             (3-digit hex)
 *        #RRGGBB          (6-digit hex)
 *        #RRGGBBAA        (8-digit hex with alpha)
 *        #RRRRGGGGBBBB    (12-digit X11 16-bit per channel)
 *
 * Results:
 *      1 on success, 0 on failure.
 *
 * Side effects:
 *      *color is filled on success.
 *
 *----------------------------------------------------------------------
 */

static int
ParseColorString(const char *name, struct cg_color_t *color)
{
    if (name[0] == '#') {
        unsigned int hex = 0;
        int len = (int)strlen(name + 1);

        if (len == 12) {
            /* #RRRRGGGGBBBB — X11 16-bit per channel. */
            unsigned int r, g, b;
            if (sscanf(name + 1, "%4x%4x%4x", &r, &g, &b) == 3) {
                color->r = r / 65535.0;
                color->g = g / 65535.0;
                color->b = b / 65535.0;
                color->a = 1.0;
                return 1;
            }
            return 0;
        }

        if (sscanf(name + 1, "%x", &hex) != 1) {
            return 0;
        }

        if (len == 3) {
            color->r = (((hex >> 8) & 0xF) * 0x11) / 255.0;
            color->g = (((hex >> 4) & 0xF) * 0x11) / 255.0;
            color->b = (( hex       & 0xF) * 0x11) / 255.0;
            color->a = 1.0;
            return 1;
        }
        if (len == 6) {
            color->r = ((hex >> 16) & 0xFF) / 255.0;
            color->g = ((hex >>  8) & 0xFF) / 255.0;
            color->b = ( hex        & 0xFF) / 255.0;
            color->a = 1.0;
            return 1;
        }
        if (len == 8) {
            color->r = ((hex >> 24) & 0xFF) / 255.0;
            color->g = ((hex >> 16) & 0xFF) / 255.0;
            color->b = ((hex >>  8) & 0xFF) / 255.0;
            color->a = ( hex        & 0xFF) / 255.0;
            return 1;
        }
        return 0;
    }

    /* Named colors — covers the full set Tk uses at startup. */
    static const struct { const char *name; double r, g, b; } named[] = {
        { "red",                 1.000, 0.000, 0.000 },
        { "green",               0.000, 0.502, 0.000 },
        { "blue",                0.000, 0.000, 1.000 },
        { "white",               1.000, 1.000, 1.000 },
        { "black",               0.000, 0.000, 0.000 },
        { "gray",                0.502, 0.502, 0.502 },
        { "grey",                0.502, 0.502, 0.502 },
        { "yellow",              1.000, 1.000, 0.000 },
        { "cyan",                0.000, 1.000, 1.000 },
        { "magenta",             1.000, 0.000, 1.000 },
        { "orange",              1.000, 0.647, 0.000 },
        { "pink",                1.000, 0.753, 0.796 },
        { "purple",              0.502, 0.000, 0.502 },
        { "brown",               0.647, 0.165, 0.165 },
        { "navy",                0.000, 0.000, 0.502 },
        { "teal",                0.000, 0.502, 0.502 },
        { "maroon",              0.502, 0.000, 0.000 },
        { "lime",                0.000, 1.000, 0.000 },
        { "aqua",                0.000, 1.000, 1.000 },
        { "fuchsia",             1.000, 0.000, 1.000 },
        { "silver",              0.753, 0.753, 0.753 },
        { "gold",                1.000, 0.843, 0.000 },
        { "coral",               1.000, 0.498, 0.314 },
        { "salmon",              0.980, 0.502, 0.447 },
        { "turquoise",           0.251, 0.878, 0.816 },
        { "violet",              0.933, 0.510, 0.933 },
        { "indigo",              0.294, 0.000, 0.510 },
        { "tan",                 0.824, 0.706, 0.549 },
        { "khaki",               0.941, 0.902, 0.549 },
        { "beige",               0.961, 0.961, 0.863 },
        { "ivory",               1.000, 1.000, 0.941 },
        { "lavender",            0.902, 0.902, 0.980 },
        { "linen",               0.980, 0.941, 0.902 },
        { "snow",                1.000, 0.980, 0.980 },
        { "wheat",               0.961, 0.871, 0.702 },
        { "chocolate",           0.824, 0.412, 0.118 },
        { "tomato",              1.000, 0.388, 0.278 },
        { "orchid",              0.855, 0.439, 0.839 },
        { "plum",                0.867, 0.627, 0.867 },
        { "sienna",              0.627, 0.322, 0.176 },
        { "gray0",               0.000, 0.000, 0.000 },
        { "gray100",             1.000, 1.000, 1.000 },
        { "gray85",              0.851, 0.851, 0.851 },
        { "gray90",              0.898, 0.898, 0.898 },
        { "gray75",              0.749, 0.749, 0.749 },
        { "gray50",              0.502, 0.502, 0.502 },
        { "gray25",              0.251, 0.251, 0.251 },
        { "grey85",              0.851, 0.851, 0.851 },
        { "grey90",              0.898, 0.898, 0.898 },
        { "grey75",              0.749, 0.749, 0.749 },
        { "grey50",              0.502, 0.502, 0.502 },
        { "grey25",              0.251, 0.251, 0.251 },
        { "SystemButtonFace",    0.878, 0.878, 0.878 },
        { "SystemButtonText",    0.000, 0.000, 0.000 },
        { "SystemHighlight",     0.000, 0.475, 0.843 },
        { "SystemHighlightText", 1.000, 1.000, 1.000 },
        { "SystemWindow",        1.000, 1.000, 1.000 },
        { "SystemWindowText",    0.000, 0.000, 0.000 },
        { NULL, 0, 0, 0 }
    };

    for (int i = 0; named[i].name != NULL; i++) {
        if (strcasecmp(name, named[i].name) == 0) {
            color->r = named[i].r;
            color->g = named[i].g;
            color->b = named[i].b;
            color->a = 1.0;
            return 1;
        }
    }

    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkColorToCG --
 *
 *      Extract a cg_color_t from a TkColor structure.
 *
 * Results:
 *      Returns a cg_color_t corresponding to the TkColor.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

struct cg_color_t
TkColorToCG(TkColor *tkColPtr)
{
    return TkGlfwXColorToCG(&tkColPtr->color);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
