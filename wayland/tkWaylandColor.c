/*
 * tkWaylandColor.c --
 *
 *      This file contains the platform specific color routines needed for
 *      Wayland/GLFW/NanoVG support.
 *
 * Copyright © 1996 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 * Copyright © 2026 Marc Culler
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkColor.h"
#include "tkGlfwInt.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/*
 * This file simply implements the platform specific functions
 * required by Tk stubs.  We ignore colormaps completely and
 * simply use Tk_Color objects with colormap set to None.
 */

/* Forward declarations. */
static bool  ParseColorString(const char *name, NVGcolor *color);
static bool  LookupNamedColor(const char *name, NVGcolor *color);
static bool  ParseGrayScale(const char *name, NVGcolor *color);
static bool  ParseX11ColorVariant(const char *name, NVGcolor *color);
static bool  ParseHexColor(const char *name, NVGcolor *color);

/*
 *----------------------------------------------------------------------
 *
 * TkpFreeColor --
 *
 *      Release platform-specific data associated with a previously
 *      allocated TkColor structure.  Since we have no such data,
 *      this is a no-op.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
TkpFreeColor(TkColor *tkColPtr)
{
    (void)tkColPtr;  /* Unused parameter */
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
     * inside Tk_GetGC -> CreateHashEntry.  Any uninitialised bytes cause
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

    /* Encode RGB into pixel as 0x00RRGGBB so GC foreground values
     * can be decoded back to color by TkGlfwPixelToNVG. */
    xcolor.pixel = (((unsigned long)(nvgcolor.r * 255.0f + 0.5f)) << 16)
                 | (((unsigned long)(nvgcolor.g * 255.0f + 0.5f)) <<  8)
                 |  ((unsigned long)(nvgcolor.b * 255.0f + 0.5f));

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
     * The incoming colorPtr may have uninitialized pixel/pad fields (e.g.
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
    return false;
}

/*
 *----------------------------------------------------------------------
 *
 * ParseHexColor --
 *
 *      Parse a hexadecimal color string into an NVGcolor structure.
 *
 *      Supported formats:
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

static bool
ParseHexColor(const char *name, NVGcolor *color)
{
    if (name[0] != '#') {
        return false;
    }

    unsigned int hex = 0;
    int len = strlen(name + 1);

    if (sscanf(name + 1, "%x", &hex) != 1) {
        return false;
    }

    if (len == 3) {         /* #RGB — expand each nibble */
        color->r = ((((hex >> 8) & 0xF) * 0x11)) / 255.0f;
        color->g = ((((hex >> 4) & 0xF) * 0x11)) / 255.0f;
        color->b = ((( hex       & 0xF) * 0x11)) / 255.0f;
        color->a = 1.0f;
        return true;
    }
    if (len == 6) {         /* #RRGGBB */
        color->r = ((hex >> 16) & 0xFF) / 255.0f;
        color->g = ((hex >>  8) & 0xFF) / 255.0f;
        color->b = ( hex        & 0xFF) / 255.0f;
        color->a = 1.0f;
        return true;
    }
    if (len == 8) {         /* #RRGGBBAA */
        color->r = ((hex >> 24) & 0xFF) / 255.0f;
        color->g = ((hex >> 16) & 0xFF) / 255.0f;
        color->b = ((hex >>  8) & 0xFF) / 255.0f;
        color->a = ( hex        & 0xFF) / 255.0f;
        return true;
    }
    if (len == 12) {        /* #RRRRGGGGBBBB (X11 16-bit per channel) */
        unsigned int r, g, b;
        if (sscanf(name + 1, "%4x%4x%4x", &r, &g, &b) == 3) {
            color->r = r / 65535.0f;
            color->g = g / 65535.0f;
            color->b = b / 65535.0f;
            color->a = 1.0f;
            return true;
        }
    }
    return false;
}

/*
 *----------------------------------------------------------------------
 *
 * ParseGrayScale --
 *
 *      Parse gray/grey scale color names (grayN or greyN where N=0..100)
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

static bool
ParseGrayScale(const char *name, NVGcolor *color)
{
    int n = -1;

    if (sscanf(name, "gray%d", &n) == 1 || sscanf(name, "grey%d", &n) == 1) {
        if (n >= 0 && n <= 100) {
            float v = n / 100.0f;
            color->r = color->g = color->b = v;
            color->a = 1.0f;
            return true;
        }
    }

    return false;
}

/*
 *----------------------------------------------------------------------
 *
 * ParseX11ColorVariant --
 *
 *      Parse X11 colorN variants: <basename>1 .. <basename>4
 *
 *      X11 scale factors (approximated from rgb.txt data):
 *        1 → 1.000 (full brightness)
 *        2 → 0.933
 *        3 → 0.804
 *        4 → 0.545
 *
 * Results:
 *      true if the color string was successfully parsed, false otherwise.
 *
 * Side effects:
 *      The NVGcolor structure pointed to by 'color' is filled with the
 *      parsed color components.
 *
 *----------------------------------------------------------------------
 */

static bool
ParseX11ColorVariant(const char *name, NVGcolor *color)
{
    static const float scale[4] = { 1.000f, 0.933f, 0.804f, 0.545f };
    char basename[64];
    int  n    = -1;
    int  len  = (int)strlen(name);

    if (len <= 1 || len >= 64) {
        return false;
    }

    /* Walk backwards over trailing digits. */
    int dstart = len - 1;
    while (dstart > 0 && isdigit((unsigned char)name[dstart])) {
        dstart--;
    }
    dstart++; /* Index of first digit character. */

    if (sscanf(name + dstart, "%d", &n) != 1 || n < 1 || n > 4) {
        return false;
    }

    /* Extract the base name (everything before the digits). */
    strncpy(basename, name, (size_t)dstart);
    basename[dstart] = '\0';

    /* Recurse to parse the base name. */
    NVGcolor base;
    if (!ParseColorString(basename, &base)) {
        return false;
    }

    float s  = scale[n - 1];
    color->r = base.r * s;
    color->g = base.g * s;
    color->b = base.b * s;
    color->a = base.a;
    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * LookupNamedColor --
 *
 *      Look up a named color from the X11 color database.
 *
 *      This table contains all standard X11 color names with their
 *      RGB values normalized to [0.0, 1.0] range.
 *
 * Results:
 *      true if the color name was found, false otherwise.
 *
 * Side effects:
 *      The NVGcolor structure pointed to by 'color' is filled with the
 *      parsed color components.
 *
 *----------------------------------------------------------------------
 */

static bool
LookupNamedColor(const char *name, NVGcolor *color)
{
    /* Complete X11 color database. */
    static const struct { const char *name; float r, g, b; } named[] = {
        /* Standard colors. */
        { "aliceBlue",              0.941f, 0.973f, 1.000f },
        { "antiqueWhite",           0.980f, 0.922f, 0.843f },
        { "antiqueWhite1",          1.000f, 0.937f, 0.859f },
        { "antiqueWhite2",          0.933f, 0.875f, 0.800f },
        { "antiqueWhite3",          0.804f, 0.753f, 0.690f },
        { "antiqueWhite4",          0.545f, 0.514f, 0.471f },
        { "aqua",                   0.000f, 1.000f, 1.000f },
        { "aquamarine",             0.498f, 1.000f, 0.831f },
        { "aquamarine1",            0.498f, 1.000f, 0.831f },
        { "aquamarine2",            0.463f, 0.933f, 0.776f },
        { "aquamarine3",            0.400f, 0.804f, 0.667f },
        { "aquamarine4",            0.271f, 0.545f, 0.455f },
        { "azure",                  0.941f, 1.000f, 1.000f },
        { "azure1",                 0.941f, 1.000f, 1.000f },
        { "azure2",                 0.878f, 0.933f, 0.933f },
        { "azure3",                 0.757f, 0.804f, 0.804f },
        { "azure4",                 0.514f, 0.545f, 0.545f },
        { "beige",                  0.961f, 0.961f, 0.863f },
        { "bisque",                 1.000f, 0.894f, 0.769f },
        { "bisque1",                1.000f, 0.894f, 0.769f },
        { "bisque2",                0.933f, 0.835f, 0.718f },
        { "bisque3",                0.804f, 0.718f, 0.620f },
        { "bisque4",                0.545f, 0.490f, 0.420f },
        { "black",                  0.000f, 0.000f, 0.000f },
        { "blanchedAlmond",         1.000f, 0.922f, 0.804f },
        { "blue",                   0.000f, 0.000f, 1.000f },
        { "blue1",                  0.000f, 0.000f, 1.000f },
        { "blue2",                  0.000f, 0.000f, 0.933f },
        { "blue3",                  0.000f, 0.000f, 0.804f },
        { "blue4",                  0.000f, 0.000f, 0.545f },
        { "blueViolet",             0.541f, 0.169f, 0.886f },
        { "brown",                  0.647f, 0.165f, 0.165f },
        { "brown1",                 1.000f, 0.251f, 0.251f },
        { "brown2",                 0.933f, 0.231f, 0.231f },
        { "brown3",                 0.804f, 0.200f, 0.200f },
        { "brown4",                 0.545f, 0.137f, 0.137f },
        { "burlywood",              0.871f, 0.722f, 0.529f },
        { "burlywood1",             1.000f, 0.827f, 0.608f },
        { "burlywood2",             0.933f, 0.773f, 0.569f },
        { "burlywood3",             0.804f, 0.667f, 0.490f },
        { "burlywood4",             0.545f, 0.451f, 0.333f },
        { "cadetBlue",              0.373f, 0.620f, 0.627f },
        { "cadetBlue1",             0.596f, 0.961f, 1.000f },
        { "cadetBlue2",             0.557f, 0.898f, 0.933f },
        { "cadetBlue3",             0.478f, 0.773f, 0.804f },
        { "cadetBlue4",             0.325f, 0.525f, 0.545f },
        { "chartreuse",             0.498f, 1.000f, 0.000f },
        { "chartreuse1",            0.498f, 1.000f, 0.000f },
        { "chartreuse2",            0.463f, 0.933f, 0.000f },
        { "chartreuse3",            0.400f, 0.804f, 0.000f },
        { "chartreuse4",            0.271f, 0.545f, 0.000f },
        { "chocolate",              0.824f, 0.412f, 0.118f },
        { "chocolate1",             1.000f, 0.498f, 0.141f },
        { "chocolate2",             0.933f, 0.463f, 0.129f },
        { "chocolate3",             0.804f, 0.400f, 0.114f },
        { "chocolate4",             0.545f, 0.271f, 0.075f },
        { "coral",                  1.000f, 0.498f, 0.314f },
        { "coral1",                 1.000f, 0.447f, 0.337f },
        { "coral2",                 0.933f, 0.416f, 0.314f },
        { "coral3",                 0.804f, 0.357f, 0.271f },
        { "coral4",                 0.545f, 0.243f, 0.184f },
        { "cornflowerBlue",         0.392f, 0.584f, 0.929f },
        { "cornsilk",               1.000f, 0.973f, 0.863f },
        { "cornsilk1",              1.000f, 0.973f, 0.863f },
        { "cornsilk2",              0.933f, 0.910f, 0.804f },
        { "cornsilk3",              0.804f, 0.784f, 0.694f },
        { "cornsilk4",              0.545f, 0.533f, 0.471f },
        { "crimson",                0.863f, 0.078f, 0.235f },
        { "cyan",                   0.000f, 1.000f, 1.000f },
        { "cyan1",                  0.000f, 1.000f, 1.000f },
        { "cyan2",                  0.000f, 0.933f, 0.933f },
        { "cyan3",                  0.000f, 0.804f, 0.804f },
        { "cyan4",                  0.000f, 0.545f, 0.545f },
        { "darkBlue",               0.000f, 0.000f, 0.545f },
        { "darkCyan",               0.000f, 0.545f, 0.545f },
        { "darkGoldenrod",          0.722f, 0.525f, 0.043f },
        { "darkGoldenrod1",         1.000f, 0.725f, 0.059f },
        { "darkGoldenrod2",         0.933f, 0.678f, 0.055f },
        { "darkGoldenrod3",         0.804f, 0.584f, 0.047f },
        { "darkGoldenrod4",         0.545f, 0.396f, 0.031f },
        { "darkGray",               0.663f, 0.663f, 0.663f },
        { "darkGreen",              0.000f, 0.392f, 0.000f },
        { "darkGrey",               0.663f, 0.663f, 0.663f },
        { "darkKhaki",              0.741f, 0.718f, 0.420f },
        { "darkMagenta",            0.545f, 0.000f, 0.545f },
        { "darkOliveGreen",         0.333f, 0.420f, 0.184f },
        { "darkOliveGreen1",        0.792f, 1.000f, 0.439f },
        { "darkOliveGreen2",        0.737f, 0.933f, 0.408f },
        { "darkOliveGreen3",        0.635f, 0.804f, 0.353f },
        { "darkOliveGreen4",        0.431f, 0.545f, 0.239f },
        { "darkOrange",             1.000f, 0.549f, 0.000f },
        { "darkOrange1",            1.000f, 0.498f, 0.000f },
        { "darkOrange2",            0.933f, 0.463f, 0.000f },
        { "darkOrange3",            0.804f, 0.400f, 0.000f },
        { "darkOrange4",            0.545f, 0.271f, 0.000f },
        { "darkOrchid",             0.600f, 0.196f, 0.800f },
        { "darkOrchid1",            0.749f, 0.243f, 1.000f },
        { "darkOrchid2",            0.698f, 0.227f, 0.933f },
        { "darkOrchid3",            0.604f, 0.196f, 0.804f },
        { "darkOrchid4",            0.408f, 0.133f, 0.545f },
        { "darkRed",                0.545f, 0.000f, 0.000f },
        { "darkSalmon",             0.914f, 0.588f, 0.478f },
        { "darkSeaGreen",           0.561f, 0.737f, 0.561f },
        { "darkSeaGreen1",          0.757f, 1.000f, 0.757f },
        { "darkSeaGreen2",          0.706f, 0.933f, 0.706f },
        { "darkSeaGreen3",          0.608f, 0.804f, 0.608f },
        { "darkSeaGreen4",          0.412f, 0.545f, 0.412f },
        { "darkSlateBlue",          0.282f, 0.239f, 0.545f },
        { "darkSlateGray",          0.184f, 0.310f, 0.310f },
        { "darkSlateGray1",         0.592f, 1.000f, 1.000f },
        { "darkSlateGray2",         0.553f, 0.933f, 0.933f },
        { "darkSlateGray3",         0.475f, 0.804f, 0.804f },
        { "darkSlateGray4",         0.322f, 0.545f, 0.545f },
        { "darkSlateGrey",          0.184f, 0.310f, 0.310f },
        { "darkTurquoise",          0.000f, 0.808f, 0.820f },
        { "darkViolet",             0.580f, 0.000f, 0.827f },
        { "deepPink",               1.000f, 0.078f, 0.576f },
        { "deepPink1",              1.000f, 0.078f, 0.576f },
        { "deepPink2",              0.933f, 0.071f, 0.537f },
        { "deepPink3",              0.804f, 0.063f, 0.463f },
        { "deepPink4",              0.545f, 0.039f, 0.314f },
        { "deepSkyBlue",            0.000f, 0.749f, 1.000f },
        { "deepSkyBlue1",           0.000f, 0.749f, 1.000f },
        { "deepSkyBlue2",           0.000f, 0.698f, 0.933f },
        { "deepSkyBlue3",           0.000f, 0.604f, 0.804f },
        { "deepSkyBlue4",           0.000f, 0.408f, 0.545f },
        { "dimGray",                0.412f, 0.412f, 0.412f },
        { "dimGrey",                0.412f, 0.412f, 0.412f },
        { "dodgerBlue",             0.118f, 0.565f, 1.000f },
        { "dodgerBlue1",            0.118f, 0.565f, 1.000f },
        { "dodgerBlue2",            0.110f, 0.525f, 0.933f },
        { "dodgerBlue3",            0.094f, 0.455f, 0.804f },
        { "dodgerBlue4",            0.063f, 0.306f, 0.545f },
        { "firebrick",              0.698f, 0.133f, 0.133f },
        { "firebrick1",             1.000f, 0.188f, 0.188f },
        { "firebrick2",             0.933f, 0.173f, 0.173f },
        { "firebrick3",             0.804f, 0.149f, 0.149f },
        { "firebrick4",             0.545f, 0.102f, 0.102f },
        { "floralWhite",            1.000f, 0.980f, 0.941f },
        { "forestGreen",            0.133f, 0.545f, 0.133f },
        { "fuchsia",                1.000f, 0.000f, 1.000f },
        { "gainsboro",              0.863f, 0.863f, 0.863f },
        { "ghostWhite",             0.973f, 0.973f, 1.000f },
        { "gold",                   1.000f, 0.843f, 0.000f },
        { "gold1",                  1.000f, 0.843f, 0.000f },
        { "gold2",                  0.933f, 0.788f, 0.000f },
        { "gold3",                  0.804f, 0.678f, 0.000f },
        { "gold4",                  0.545f, 0.459f, 0.000f },
        { "goldenrod",              0.855f, 0.647f, 0.125f },
        { "goldenrod1",             1.000f, 0.757f, 0.145f },
        { "goldenrod2",             0.933f, 0.706f, 0.133f },
        { "goldenrod3",             0.804f, 0.608f, 0.114f },
        { "goldenrod4",             0.545f, 0.412f, 0.078f },
        { "gray",                   0.502f, 0.502f, 0.502f },
        { "green",                  0.000f, 0.502f, 0.000f },
        { "green1",                 0.000f, 1.000f, 0.000f },
        { "green2",                 0.000f, 0.933f, 0.000f },
        { "green3",                 0.000f, 0.804f, 0.000f },
        { "green4",                 0.000f, 0.545f, 0.000f },
        { "greenYellow",            0.678f, 1.000f, 0.184f },
        { "grey",                   0.502f, 0.502f, 0.502f },
        { "honeydew",               0.941f, 1.000f, 0.941f },
        { "honeydew1",              0.941f, 1.000f, 0.941f },
        { "honeydew2",              0.878f, 0.933f, 0.878f },
        { "honeydew3",              0.757f, 0.804f, 0.757f },
        { "honeydew4",              0.514f, 0.545f, 0.514f },
        { "hotPink",                1.000f, 0.412f, 0.706f },
        { "hotPink1",               1.000f, 0.431f, 0.706f },
        { "hotPink2",               0.933f, 0.416f, 0.655f },
        { "hotPink3",               0.804f, 0.376f, 0.565f },
        { "hotPink4",               0.545f, 0.227f, 0.384f },
        { "indianRed",              0.804f, 0.361f, 0.361f },
        { "indianRed1",             1.000f, 0.416f, 0.416f },
        { "indianRed2",             0.933f, 0.388f, 0.388f },
        { "indianRed3",             0.804f, 0.333f, 0.333f },
        { "indianRed4",             0.545f, 0.227f, 0.227f },
        { "indigo",                 0.294f, 0.000f, 0.510f },
        { "ivory",                  1.000f, 1.000f, 0.941f },
        { "ivory1",                 1.000f, 1.000f, 0.941f },
        { "ivory2",                 0.933f, 0.933f, 0.878f },
        { "ivory3",                 0.804f, 0.804f, 0.757f },
        { "ivory4",                 0.545f, 0.545f, 0.514f },
        { "khaki",                  0.941f, 0.902f, 0.549f },
        { "khaki1",                 1.000f, 0.965f, 0.561f },
        { "khaki2",                 0.933f, 0.902f, 0.522f },
        { "khaki3",                 0.804f, 0.776f, 0.451f },
        { "khaki4",                 0.545f, 0.525f, 0.306f },
        { "lavender",               0.902f, 0.902f, 0.980f },
        { "lavenderBlush",          1.000f, 0.941f, 0.961f },
        { "lavenderBlush1",         1.000f, 0.941f, 0.961f },
        { "lavenderBlush2",         0.933f, 0.878f, 0.898f },
        { "lavenderBlush3",         0.804f, 0.757f, 0.773f },
        { "lavenderBlush4",         0.545f, 0.514f, 0.525f },
        { "lawnGreen",              0.486f, 0.988f, 0.000f },
        { "lemonChiffon",           1.000f, 0.980f, 0.804f },
        { "lemonChiffon1",          1.000f, 0.980f, 0.804f },
        { "lemonChiffon2",          0.933f, 0.914f, 0.749f },
        { "lemonChiffon3",          0.804f, 0.788f, 0.647f },
        { "lemonChiffon4",          0.545f, 0.537f, 0.439f },
        { "lightBlue",              0.678f, 0.847f, 0.902f },
        { "lightBlue1",             0.749f, 0.937f, 1.000f },
        { "lightBlue2",             0.698f, 0.875f, 0.933f },
        { "lightBlue3",             0.604f, 0.753f, 0.804f },
        { "lightBlue4",             0.408f, 0.514f, 0.545f },
        { "lightCoral",             0.941f, 0.502f, 0.502f },
        { "lightCyan",              0.878f, 1.000f, 1.000f },
        { "lightCyan1",             0.878f, 1.000f, 1.000f },
        { "lightCyan2",             0.820f, 0.933f, 0.933f },
        { "lightCyan3",             0.706f, 0.804f, 0.804f },
        { "lightCyan4",             0.478f, 0.545f, 0.545f },
        { "lightGoldenrod",         0.933f, 0.867f, 0.510f },
        { "lightGoldenrod1",        1.000f, 0.925f, 0.545f },
        { "lightGoldenrod2",        0.933f, 0.863f, 0.510f },
        { "lightGoldenrod3",        0.804f, 0.745f, 0.439f },
        { "lightGoldenrod4",        0.545f, 0.506f, 0.298f },
        { "lightGoldenrodYellow",   0.980f, 0.980f, 0.824f },
        { "lightGray",              0.827f, 0.827f, 0.827f },
        { "lightGreen",             0.565f, 0.933f, 0.565f },
        { "lightGrey",              0.827f, 0.827f, 0.827f },
        { "lightPink",              1.000f, 0.714f, 0.757f },
        { "lightPink1",             1.000f, 0.682f, 0.725f },
        { "lightPink2",             0.933f, 0.635f, 0.678f },
        { "lightPink3",             0.804f, 0.549f, 0.584f },
        { "lightPink4",             0.545f, 0.373f, 0.396f },
        { "lightSalmon",            1.000f, 0.627f, 0.478f },
        { "lightSalmon1",           1.000f, 0.627f, 0.478f },
        { "lightSalmon2",           0.933f, 0.584f, 0.447f },
        { "lightSalmon3",           0.804f, 0.506f, 0.384f },
        { "lightSalmon4",           0.545f, 0.341f, 0.259f },
        { "lightSeaGreen",          0.125f, 0.698f, 0.667f },
        { "lightSkyBlue",           0.529f, 0.808f, 0.980f },
        { "lightSkyBlue1",          0.690f, 0.886f, 1.000f },
        { "lightSkyBlue2",          0.643f, 0.827f, 0.933f },
        { "lightSkyBlue3",          0.553f, 0.714f, 0.804f },
        { "lightSkyBlue4",          0.376f, 0.482f, 0.545f },
        { "lightSlateBlue",         0.518f, 0.439f, 1.000f },
        { "lightSlateGray",         0.467f, 0.533f, 0.600f },
        { "lightSlateGrey",         0.467f, 0.533f, 0.600f },
        { "lightSteelBlue",         0.690f, 0.769f, 0.871f },
        { "lightSteelBlue1",        0.792f, 0.882f, 1.000f },
        { "lightSteelBlue2",        0.737f, 0.824f, 0.933f },
        { "lightSteelBlue3",        0.635f, 0.710f, 0.804f },
        { "lightSteelBlue4",        0.431f, 0.482f, 0.545f },
        { "lightYellow",            1.000f, 1.000f, 0.878f },
        { "lightYellow1",           1.000f, 1.000f, 0.878f },
        { "lightYellow2",           0.933f, 0.933f, 0.820f },
        { "lightYellow3",           0.804f, 0.804f, 0.706f },
        { "lightYellow4",           0.545f, 0.545f, 0.478f },
        { "lime",                   0.000f, 1.000f, 0.000f },
        { "limeGreen",              0.196f, 0.804f, 0.196f },
        { "linen",                  0.980f, 0.941f, 0.902f },
        { "magenta",                1.000f, 0.000f, 1.000f },
        { "magenta1",               1.000f, 0.000f, 1.000f },
        { "magenta2",               0.933f, 0.000f, 0.933f },
        { "magenta3",               0.804f, 0.000f, 0.804f },
        { "magenta4",               0.545f, 0.000f, 0.545f },
        { "maroon",                 0.502f, 0.000f, 0.000f },
        { "maroon1",                1.000f, 0.204f, 0.702f },
        { "maroon2",                0.933f, 0.188f, 0.655f },
        { "maroon3",                0.804f, 0.161f, 0.565f },
        { "maroon4",                0.545f, 0.110f, 0.384f },
        { "mediumAquamarine",       0.400f, 0.804f, 0.667f },
        { "mediumBlue",             0.000f, 0.000f, 0.804f },
        { "mediumOrchid",           0.729f, 0.333f, 0.827f },
        { "mediumOrchid1",          0.878f, 0.400f, 1.000f },
        { "mediumOrchid2",          0.820f, 0.373f, 0.933f },
        { "mediumOrchid3",          0.706f, 0.322f, 0.804f },
        { "mediumOrchid4",          0.478f, 0.216f, 0.545f },
        { "mediumPurple",           0.576f, 0.439f, 0.859f },
        { "mediumPurple1",          0.671f, 0.510f, 1.000f },
        { "mediumPurple2",          0.624f, 0.475f, 0.933f },
        { "mediumPurple3",          0.537f, 0.408f, 0.804f },
        { "mediumPurple4",          0.365f, 0.278f, 0.545f },
        { "mediumSeaGreen",         0.235f, 0.702f, 0.443f },
        { "mediumSlateBlue",        0.482f, 0.408f, 0.933f },
        { "mediumSpringGreen",      0.000f, 0.980f, 0.604f },
        { "mediumTurquoise",        0.282f, 0.820f, 0.800f },
        { "mediumVioletRed",        0.780f, 0.082f, 0.522f },
        { "midnightBlue",           0.098f, 0.098f, 0.439f },
        { "mintCream",              0.961f, 1.000f, 0.980f },
        { "mistyRose",              1.000f, 0.894f, 0.882f },
        { "mistyRose1",             1.000f, 0.894f, 0.882f },
        { "mistyRose2",             0.933f, 0.835f, 0.824f },
        { "mistyRose3",             0.804f, 0.718f, 0.710f },
        { "mistyRose4",             0.545f, 0.490f, 0.482f },
        { "moccasin",               1.000f, 0.894f, 0.710f },
        { "navajoWhite",            1.000f, 0.871f, 0.678f },
        { "navajoWhite1",           1.000f, 0.871f, 0.678f },
        { "navajoWhite2",           0.933f, 0.812f, 0.631f },
        { "navajoWhite3",           0.804f, 0.702f, 0.545f },
        { "navajoWhite4",           0.545f, 0.475f, 0.369f },
        { "navy",                   0.000f, 0.000f, 0.502f },
        { "navyBlue",               0.000f, 0.000f, 0.502f },
        { "oldLace",                0.992f, 0.961f, 0.902f },
        { "olive",                  0.502f, 0.502f, 0.000f },
        { "oliveDrab",              0.420f, 0.557f, 0.137f },
        { "oliveDrab1",             0.753f, 1.000f, 0.243f },
        { "oliveDrab2",             0.702f, 0.933f, 0.227f },
        { "oliveDrab3",             0.604f, 0.804f, 0.196f },
        { "oliveDrab4",             0.412f, 0.545f, 0.133f },
        { "orange",                 1.000f, 0.647f, 0.000f },
        { "orange1",                1.000f, 0.647f, 0.000f },
        { "orange2",                0.933f, 0.604f, 0.000f },
        { "orange3",                0.804f, 0.522f, 0.000f },
        { "orange4",                0.545f, 0.353f, 0.000f },
        { "orangeRed",              1.000f, 0.271f, 0.000f },
        { "orangeRed1",             1.000f, 0.271f, 0.000f },
        { "orangeRed2",             0.933f, 0.251f, 0.000f },
        { "orangeRed3",             0.804f, 0.216f, 0.000f },
        { "orangeRed4",             0.545f, 0.145f, 0.000f },
        { "orchid",                 0.855f, 0.439f, 0.839f },
        { "orchid1",                1.000f, 0.514f, 0.980f },
        { "orchid2",                0.933f, 0.478f, 0.914f },
        { "orchid3",                0.804f, 0.412f, 0.788f },
        { "orchid4",                0.545f, 0.278f, 0.537f },
        { "paleGoldenrod",          0.933f, 0.910f, 0.667f },
        { "paleGreen",              0.596f, 0.984f, 0.596f },
        { "paleGreen1",             0.604f, 1.000f, 0.604f },
        { "paleGreen2",             0.565f, 0.933f, 0.565f },
        { "paleGreen3",             0.486f, 0.804f, 0.486f },
        { "paleGreen4",             0.329f, 0.545f, 0.329f },
        { "paleTurquoise",          0.686f, 0.933f, 0.933f },
        { "paleTurquoise1",         0.733f, 1.000f, 1.000f },
        { "paleTurquoise2",         0.682f, 0.933f, 0.933f },
        { "paleTurquoise3",         0.588f, 0.804f, 0.804f },
        { "paleTurquoise4",         0.400f, 0.545f, 0.545f },
        { "paleVioletRed",          0.859f, 0.439f, 0.576f },
        { "paleVioletRed1",         1.000f, 0.510f, 0.671f },
        { "paleVioletRed2",         0.933f, 0.475f, 0.624f },
        { "paleVioletRed3",         0.804f, 0.408f, 0.537f },
        { "paleVioletRed4",         0.545f, 0.278f, 0.365f },
        { "papayaWhip",             1.000f, 0.937f, 0.835f },
        { "peachPuff",              1.000f, 0.855f, 0.725f },
        { "peachPuff1",             1.000f, 0.855f, 0.725f },
        { "peachPuff2",             0.933f, 0.796f, 0.678f },
        { "peachPuff3",             0.804f, 0.686f, 0.584f },
        { "peachPuff4",             0.545f, 0.467f, 0.396f },
        { "peru",                   0.804f, 0.522f, 0.247f },
        { "pink",                   1.000f, 0.753f, 0.796f },
        { "pink1",                  1.000f, 0.710f, 0.773f },
        { "pink2",                  0.933f, 0.663f, 0.722f },
        { "pink3",                  0.804f, 0.569f, 0.620f },
        { "pink4",                  0.545f, 0.388f, 0.424f },
        { "plum",                   0.867f, 0.627f, 0.867f },
        { "plum1",                  1.000f, 0.733f, 1.000f },
        { "plum2",                  0.933f, 0.682f, 0.933f },
        { "plum3",                  0.804f, 0.588f, 0.804f },
        { "plum4",                  0.545f, 0.400f, 0.545f },
        { "powderBlue",             0.690f, 0.878f, 0.902f },
        { "purple",                 0.502f, 0.000f, 0.502f },
        { "purple1",                0.608f, 0.188f, 1.000f },
        { "purple2",                0.569f, 0.173f, 0.933f },
        { "purple3",                0.490f, 0.149f, 0.804f },
        { "purple4",                0.333f, 0.102f, 0.545f },
        { "red",                    1.000f, 0.000f, 0.000f },
        { "red1",                   1.000f, 0.000f, 0.000f },
        { "red2",                   0.933f, 0.000f, 0.000f },
        { "red3",                   0.804f, 0.000f, 0.000f },
        { "red4",                   0.545f, 0.000f, 0.000f },
        { "rosyBrown",              0.737f, 0.561f, 0.561f },
        { "rosyBrown1",             1.000f, 0.757f, 0.757f },
        { "rosyBrown2",             0.933f, 0.706f, 0.706f },
        { "rosyBrown3",             0.804f, 0.608f, 0.608f },
        { "rosyBrown4",             0.545f, 0.412f, 0.412f },
        { "royalBlue",              0.255f, 0.412f, 0.882f },
        { "royalBlue1",             0.282f, 0.463f, 1.000f },
        { "royalBlue2",             0.263f, 0.431f, 0.933f },
        { "royalBlue3",             0.227f, 0.373f, 0.804f },
        { "royalBlue4",             0.153f, 0.251f, 0.545f },
        { "saddleBrown",            0.545f, 0.271f, 0.075f },
        { "salmon",                 0.980f, 0.502f, 0.447f },
        { "salmon1",                1.000f, 0.549f, 0.412f },
        { "salmon2",                0.933f, 0.510f, 0.384f },
        { "salmon3",                0.804f, 0.439f, 0.329f },
        { "salmon4",                0.545f, 0.298f, 0.224f },
        { "sandyBrown",             0.957f, 0.643f, 0.376f },
        { "seaGreen",               0.180f, 0.545f, 0.341f },
        { "seaGreen1",              0.329f, 1.000f, 0.624f },
        { "seaGreen2",              0.306f, 0.933f, 0.580f },
        { "seaGreen3",              0.263f, 0.804f, 0.502f },
        { "seaGreen4",              0.180f, 0.545f, 0.341f },
        { "seashell",               1.000f, 0.961f, 0.933f },
        { "seashell1",              1.000f, 0.961f, 0.933f },
        { "seashell2",              0.933f, 0.898f, 0.871f },
        { "seashell3",              0.804f, 0.773f, 0.749f },
        { "seashell4",              0.545f, 0.525f, 0.510f },
        { "sienna",                 0.627f, 0.322f, 0.176f },
        { "sienna1",                1.000f, 0.510f, 0.278f },
        { "sienna2",                0.933f, 0.475f, 0.259f },
        { "sienna3",                0.804f, 0.408f, 0.224f },
        { "sienna4",                0.545f, 0.278f, 0.149f },
        { "silver",                 0.753f, 0.753f, 0.753f },
        { "skyBlue",                0.529f, 0.808f, 0.922f },
        { "skyBlue1",               0.529f, 0.808f, 1.000f },
        { "skyBlue2",               0.494f, 0.753f, 0.933f },
        { "skyBlue3",               0.424f, 0.651f, 0.804f },
        { "skyBlue4",               0.290f, 0.439f, 0.545f },
        { "slateBlue",              0.416f, 0.353f, 0.804f },
        { "slateBlue1",             0.514f, 0.435f, 1.000f },
        { "slateBlue2",             0.478f, 0.404f, 0.933f },
        { "slateBlue3",             0.412f, 0.349f, 0.804f },
        { "slateBlue4",             0.278f, 0.235f, 0.545f },
        { "slateGray",              0.439f, 0.502f, 0.565f },
        { "slateGray1",             0.776f, 0.886f, 1.000f },
        { "slateGray2",             0.725f, 0.827f, 0.933f },
        { "slateGray3",             0.624f, 0.714f, 0.804f },
        { "slateGray4",             0.424f, 0.482f, 0.545f },
        { "slateGrey",              0.439f, 0.502f, 0.565f },
        { "snow",                   1.000f, 0.980f, 0.980f },
        { "snow1",                  1.000f, 0.980f, 0.980f },
        { "snow2",                  0.933f, 0.914f, 0.914f },
        { "snow3",                  0.804f, 0.788f, 0.788f },
        { "snow4",                  0.545f, 0.537f, 0.537f },
        { "springGreen",            0.000f, 1.000f, 0.498f },
        { "springGreen1",           0.000f, 1.000f, 0.498f },
        { "springGreen2",           0.000f, 0.933f, 0.463f },
        { "springGreen3",           0.000f, 0.804f, 0.400f },
        { "springGreen4",           0.000f, 0.545f, 0.271f },
        { "steelBlue",              0.275f, 0.510f, 0.706f },
        { "steelBlue1",             0.388f, 0.722f, 1.000f },
        { "steelBlue2",             0.361f, 0.675f, 0.933f },
        { "steelBlue3",             0.310f, 0.580f, 0.804f },
        { "steelBlue4",             0.212f, 0.392f, 0.545f },
        { "tan",                    0.824f, 0.706f, 0.549f },
        { "tan1",                   1.000f, 0.647f, 0.310f },
        { "tan2",                   0.933f, 0.604f, 0.286f },
        { "tan3",                   0.804f, 0.522f, 0.247f },
        { "tan4",                   0.545f, 0.353f, 0.169f },
        { "teal",                   0.000f, 0.502f, 0.502f },
        { "thistle",                0.847f, 0.749f, 0.847f },
        { "thistle1",               1.000f, 0.882f, 1.000f },
        { "thistle2",               0.933f, 0.824f, 0.933f },
        { "thistle3",               0.804f, 0.710f, 0.804f },
        { "thistle4",               0.545f, 0.482f, 0.545f },
        { "tomato",                 1.000f, 0.388f, 0.278f },
        { "tomato1",                1.000f, 0.388f, 0.278f },
        { "tomato2",                0.933f, 0.361f, 0.259f },
        { "tomato3",                0.804f, 0.310f, 0.224f },
        { "tomato4",                0.545f, 0.212f, 0.149f },
        { "turquoise",              0.251f, 0.878f, 0.816f },
        { "turquoise1",             0.000f, 0.961f, 1.000f },
        { "turquoise2",             0.000f, 0.898f, 0.933f },
        { "turquoise3",             0.000f, 0.773f, 0.804f },
        { "turquoise4",             0.000f, 0.525f, 0.545f },
        { "violet",                 0.933f, 0.510f, 0.933f },
        { "violetRed",              0.816f, 0.125f, 0.565f },
        { "violetRed1",             1.000f, 0.243f, 0.588f },
        { "violetRed2",             0.933f, 0.227f, 0.549f },
        { "violetRed3",             0.804f, 0.196f, 0.471f },
        { "violetRed4",             0.545f, 0.133f, 0.322f },
        { "wheat",                  0.961f, 0.871f, 0.702f },
        { "wheat1",                 1.000f, 0.906f, 0.729f },
        { "wheat2",                 0.933f, 0.847f, 0.682f },
        { "wheat3",                 0.804f, 0.729f, 0.588f },
        { "wheat4",                 0.545f, 0.494f, 0.400f },
        { "white",                  1.000f, 1.000f, 1.000f },
        { "whiteSmoke",             0.961f, 0.961f, 0.961f },
        { "yellow",                 1.000f, 1.000f, 0.000f },
        { "yellow1",                1.000f, 1.000f, 0.000f },
        { "yellow2",                0.933f, 0.933f, 0.000f },
        { "yellow3",                0.804f, 0.804f, 0.000f },
        { "yellow4",                0.545f, 0.545f, 0.000f },
        { "yellowGreen",            0.604f, 0.804f, 0.196f },

        /* System colors (platform-specific defaults) */
        { "SystemButtonFace",       0.878f, 0.878f, 0.878f },
        { "SystemButtonText",       0.000f, 0.000f, 0.000f },
        { "SystemHighlight",        0.000f, 0.475f, 0.843f },
        { "SystemHighlightText",    1.000f, 1.000f, 1.000f },
        { "SystemWindow",           1.000f, 1.000f, 1.000f },
        { "SystemWindowText",       0.000f, 0.000f, 0.000f },

        { NULL, 0, 0, 0 }
    };

    for (int i = 0; named[i].name != NULL; i++) {
        if (strcasecmp(name, named[i].name) == 0) {
            color->r = named[i].r;
            color->g = named[i].g;
            color->b = named[i].b;
            color->a = 1.0f;
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
 *      Supported formats (in order of checking):
 *        - Hexadecimal: #RGB, #RRGGBB, #RRGGBBAA, #RRRRGGGGBBBB
 *        - Gray scale: grayN, greyN (N = 0..100)
 *        - X11 named colors (from rgb.txt)
 *        - X11 colorN variants: <basename>1..4 (brightness variants)
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

static bool
ParseColorString(const char *name, NVGcolor *color)
{
    /* Hexadecimal strings. */
    if (name[0] == '#') {
        return ParseHexColor(name, color);
    }

    /* Gray/grey scale (grayN, greyN). */
    if (ParseGrayScale(name, color)) {
        return true;
    }

    /* Named colors from X11. */
    if (LookupNamedColor(name, color)) {
        return true;
    }

    /* X11 color variants (name1..name4). */
    if (ParseX11ColorVariant(name, color)) {
        return true;
    }

    return false;
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
