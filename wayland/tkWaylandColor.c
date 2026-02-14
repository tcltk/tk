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

/* Convert NVGcolor to XColor (for Tk compatibility). */
static void
NVGToXColor(const NVGcolor *nc, XColor *xc)
{
    xc->red   = (unsigned short)(nc->r * 65535.0f + 0.5f);
    xc->green = (unsigned short)(nc->g * 65535.0f + 0.5f);
    xc->blue  = (unsigned short)(nc->b * 65535.0f + 0.5f);
    xc->flags = DoRed | DoGreen | DoBlue;
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
 *----------------------------------------------------------------------
 */

TkColor *
TkpGetColorByValue(
    TCL_UNUSED(Tk_Window), /* tkwin */
    XColor   *colorPtr)
{
    TkColor *tkColPtr;

    tkColPtr = (TkColor *)Tcl_Alloc(sizeof(TkColor));
    if (tkColPtr == NULL) {
        return NULL;
    }

    tkColPtr->color            = *colorPtr;
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
        return 0;
    }

    /* Minimal set of named colors. */
    static const char *color_names[] = {
        "red", "green", "blue", "white", "black", "gray", "grey", NULL
    };
    
    static const float color_values[][4] = {
        {1.0f, 0.0f, 0.0f, 1.0f},  /* red */
        {0.0f, 1.0f, 0.0f, 1.0f},  /* green */
        {0.0f, 0.0f, 1.0f, 1.0f},  /* blue */
        {1.0f, 1.0f, 1.0f, 1.0f},  /* white */
        {0.0f, 0.0f, 0.0f, 1.0f},  /* black */
        {0.5f, 0.5f, 0.5f, 1.0f},  /* gray */
        {0.5f, 0.5f, 0.5f, 1.0f},  /* grey */
    };

    for (int i = 0; color_names[i] != NULL; i++) {
        if (strcasecmp(name, color_names[i]) == 0) {
            color->r = color_values[i][0];
            color->g = color_values[i][1];
            color->b = color_values[i][2];
            color->a = color_values[i][3];
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
