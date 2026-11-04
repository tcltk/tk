/*
 * tkWaylandCursor.c --
 *
 *      This file contains platform-specific cursor manipulation routines
 *      for Wayland/GLFW/nanovg.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 * Copyright © 2026 Marc Culler
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkWaylandInt.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "stb_image.h"
#include "x11_cursors.h"

/*
 * We deliberately DO NOT include <X11/Xcursor/Xcursor.h> to avoid
 * type collisions (XcursorFileHeader). We use dlopen.
*/
#include <dlfcn.h>

/*
 * The following data structure is a superset of the TkCursor structure
 * defined in tkCursor.c. Each system specific cursor module will define a
 * different cursor structure. All of these structures must have the same
 * header consisting of the fields in TkCursor.
 */

typedef struct {
    TkCursor info;              /* Generic cursor info used by tkCursor.c */
    GLFWcursor *cursor;         /* GLFW cursor handle */
    GLFWwindow *glfwWindow;     /* GLFW window this cursor belongs to, captured
                                 * at allocation time by TkGetCursorByName. */
    int standardShape;          /* GLFW standard cursor shape, or -1 for custom */
    int width, height;          /* Dimensions for custom cursors */
} TkWaylandCursor;

/*
 * Struct for the built-in X11 bitmap cursor table.
 */

struct BuiltinCursor {
    const char *name;
    const unsigned char *bits;
    const unsigned char *mask;
    int width;
    int height;
    int xHot;
    int yHot;
};

/*
 * Table mapping Tk cursor names to GLFW standard cursor shapes.
 * For cursors not available in GLFW, we'll use bitmap data.
 */

#ifndef GLFW_RESIZE_ALL_CURSOR
#   define GLFW_RESIZE_ALL_CURSOR -1 /* Will use bitmap */
#endif
#ifndef GLFW_RESIZE_NWSE_CURSOR
#   define GLFW_RESIZE_NWSE_CURSOR -1 /* Will use bitmap */
#endif
#ifndef GLFW_RESIZE_NESW_CURSOR
#   define GLFW_RESIZE_NESW_CURSOR -1 /* Will use bitmap */
#endif
#ifndef GLFW_RESIZE_NS_CURSOR
#   define GLFW_RESIZE_NS_CURSOR -1 /* Will use bitmap */
#endif
#ifndef GLFW_RESIZE_EW_CURSOR
#   define GLFW_RESIZE_EW_CURSOR -1 /* Will use bitmap */
#endif

static const struct CursorName {
    const char *name;
    int shape;                  /* GLFW cursor shape enum, or -1 for bitmap */
} cursorNames[] = {
    {"X_cursor",                GLFW_RESIZE_ALL_CURSOR},
    {"arrow",                   GLFW_ARROW_CURSOR},
    {"based_arrow_down",        -1}, /* Will use bitmap */
    {"based_arrow_up",          -1}, /* Will use bitmap */
    {"boat",                    -1}, /* Will use bitmap */
    {"bogosity",                -1}, /* Will use bitmap */
    {"bottom_left_corner",      GLFW_RESIZE_NESW_CURSOR},
    {"bottom_right_corner",     GLFW_RESIZE_NWSE_CURSOR},
    {"bottom_side",             GLFW_RESIZE_NS_CURSOR},
    {"bottom_tee",              -1}, /* Will use bitmap */
    {"box_spiral",              -1}, /* Will use bitmap */
    {"center_ptr",              GLFW_ARROW_CURSOR},
    {"circle",                  -1}, /* Will use bitmap */
    {"clock",                   -1}, /* Will use bitmap */
    {"coffee_mug",              -1}, /* Will use bitmap */
    {"cross",                   GLFW_CROSSHAIR_CURSOR},
    {"cross_reverse",           -1}, /* Will use bitmap */
    {"crosshair",               GLFW_CROSSHAIR_CURSOR},
    {"diamond_cross",           -1}, /* Will use bitmap */
    {"dot",                     -1}, /* Will use bitmap */
    {"dotbox",                  -1}, /* Will use bitmap */
    {"double_arrow",            GLFW_RESIZE_EW_CURSOR},
    {"draft_large",             -1}, /* Will use bitmap */
    {"draft_small",             -1}, /* Will use bitmap */
    {"draped_box",              -1}, /* Will use bitmap */
    {"exchange",                -1}, /* Will use bitmap */
    {"fleur",                   GLFW_RESIZE_ALL_CURSOR},
    {"gobbler",                 -1}, /* Will use bitmap */
    {"gumby",                   -1}, /* Will use bitmap */
    {"hand1",                   GLFW_HAND_CURSOR},
    {"hand2",                   GLFW_HAND_CURSOR},
    {"heart",                   -1}, /* Will use bitmap */
    {"icon",                    GLFW_ARROW_CURSOR},
    {"iron_cross",              -1}, /* Will use bitmap */
    {"left_ptr",                GLFW_ARROW_CURSOR},
    {"left_side",               GLFW_RESIZE_EW_CURSOR},
    {"left_tee",                -1}, /* Will use bitmap */
    {"leftbutton",              -1}, /* Will use bitmap */
    {"ll_angle",                GLFW_RESIZE_NESW_CURSOR},
    {"lr_angle",                GLFW_RESIZE_NWSE_CURSOR},
    {"man",                     -1}, /* Will use bitmap */
    {"middlebutton",            -1}, /* Will use bitmap */
    {"mouse",                   -1}, /* Will use bitmap */
    {"pencil",                  -1}, /* Will use bitmap */
    {"pirate",                  -1}, /* Will use bitmap */
    {"plus",                    GLFW_CROSSHAIR_CURSOR},
    {"question_arrow",          GLFW_ARROW_CURSOR},
    {"right_ptr",               GLFW_ARROW_CURSOR},
    {"right_side",              GLFW_RESIZE_EW_CURSOR},
    {"right_tee",               -1}, /* Will use bitmap */
    {"rightbutton",             -1}, /* Will use bitmap */
    {"rtl_logo",                -1}, /* Will use bitmap */
    {"sailboat",                -1}, /* Will use bitmap */
    {"sb_down_arrow",           -1}, /* Will use bitmap */
    {"sb_h_double_arrow",       GLFW_RESIZE_EW_CURSOR},
    {"sb_left_arrow",           GLFW_ARROW_CURSOR},
    {"sb_right_arrow",          GLFW_ARROW_CURSOR},
    {"sb_up_arrow",             GLFW_ARROW_CURSOR},
    {"sb_v_double_arrow",       GLFW_RESIZE_NS_CURSOR},
    {"shuttle",                 -1}, /* Will use bitmap */
    {"sizing",                  GLFW_RESIZE_ALL_CURSOR},
    {"spider",                  -1}, /* Will use bitmap */
    {"spraycan",                -1}, /* Will use bitmap */
    {"star",                    -1}, /* Will use bitmap */
    {"target",                  GLFW_CROSSHAIR_CURSOR},
    {"tcross",                  GLFW_CROSSHAIR_CURSOR},
    {"top_left_arrow",          GLFW_ARROW_CURSOR},
    {"top_left_corner",         GLFW_RESIZE_NWSE_CURSOR},
    {"top_right_corner",        GLFW_RESIZE_NESW_CURSOR},
    {"top_side",                GLFW_RESIZE_NS_CURSOR},
    {"top_tee",                 -1}, /* Will use bitmap */
    {"trek",                    -1}, /* Will use bitmap */
    {"ul_angle",                GLFW_RESIZE_NWSE_CURSOR},
    {"umbrella",                -1}, /* Will use bitmap */
    {"ur_angle",                GLFW_RESIZE_NESW_CURSOR},
    {"watch",                   GLFW_ARROW_CURSOR},
    {"xterm",                   GLFW_IBEAM_CURSOR},
    {NULL,                      GLFW_ARROW_CURSOR}
};

/*
 * Built-in cursor database using data from x11_cursors.h.
 */

static const struct BuiltinCursor builtinCursors[] = {
    /* mask BBX 16 16 -7  -9  → xHot= 7, yHot= 7 */
    {"X_cursor",           X_cursor_bits,           X_cursor_mask_bits,           16, 16,  7,  7},
    /* mask BBX 16 16 -14 -15 → xHot=14, yHot= 1 */
    {"arrow",              arrow_bits,              arrow_mask_bits,              16, 16, 14,  1},
    /* mask BBX 10 12  -4  -2 → xHot= 4, yHot=10 */
    {"based_arrow_down",   based_arrow_down_bits,   based_arrow_down_mask_bits,   10, 12,  4, 10},
    /* mask BBX 10 12  -4  -2 → xHot= 4, yHot=10 */
    {"based_arrow_up",     based_arrow_up_bits,     based_arrow_up_mask_bits,     10, 12,  4, 10},
    /* mask BBX 16  9 -14  -5 → xHot=14, yHot= 4 */
    {"boat",               boat_bits,               boat_mask_bits,               16,  9, 14,  4},
    /* mask BBX 15 16  -7  -9 → xHot= 7, yHot= 7 */
    {"bogosity",           bogosity_bits,           bogosity_mask_bits,           15, 16,  7,  7},
    /* mask BBX 16 16  -1  -2 → xHot= 1, yHot=14 */
    {"bottom_left_corner", bottom_left_corner_bits, bottom_left_corner_mask_bits, 16, 16,  1, 14},
    /* mask BBX 16 16 -14  -2 → xHot=14, yHot=14 */
    {"bottom_right_corner",bottom_right_corner_bits,bottom_right_corner_mask_bits,16, 16, 14, 14},
    /* mask BBX 15 16  -7  -2 → xHot= 7, yHot=14 */
    {"bottom_side",        bottom_side_bits,        bottom_side_mask_bits,        15, 16,  7, 14},
    /* mask BBX 16 12  -8  -2 → xHot= 8, yHot=10 */
    {"bottom_tee",         bottom_tee_bits,         bottom_tee_mask_bits,         16, 12,  8, 10},
    /* mask BBX 16 16  -8  -8 → xHot= 8, yHot= 8 */
    {"box_spiral",         box_spiral_bits,         box_spiral_mask_bits,         16, 16,  8,  8},
    /* mask BBX 12 16  -5 -15 → xHot= 5, yHot= 1 */
    {"center_ptr",         center_ptr_bits,         center_ptr_mask_bits,         12, 16,  5,  1},
    /* mask BBX 16 16  -8  -8 → xHot= 8, yHot= 8 */
    {"circle",             circle_bits,             circle_mask_bits,             16, 16,  8,  8},
    /* mask BBX 15 16  -6 -13 → xHot= 6, yHot= 3 */
    {"clock",              clock_bits,              clock_mask_bits,              15, 16,  6,  3},
    /* mask BBX 16 16  -7  -7 → xHot= 7, yHot= 9 */
    {"coffee_mug",         coffee_mug_bits,         coffee_mug_mask_bits,         16, 16,  7,  9},
    /* mask BBX 16 16  -7  -9 → xHot= 7, yHot= 7 */
    {"cross",              cross_bits,              cross_mask_bits,              16, 16,  7,  7},
    /* mask BBX 16 15  -7  -8 → xHot= 7, yHot= 7 */
    {"cross_reverse",      cross_reverse_bits,      cross_reverse_mask_bits,      16, 15,  7,  7},
    /* mask BBX 16 16  -7  -9 → xHot= 7, yHot= 7 */
    {"crosshair",          crosshair_bits,          crosshair_mask_bits,          16, 16,  7,  7},
    /* mask BBX 16 16  -7  -9 → xHot= 7, yHot= 7 */
    {"diamond_cross",      diamond_cross_bits,      diamond_cross_mask_bits,      16, 16,  7,  7},
    /* mask BBX 12 12  -6  -6 → xHot= 6, yHot= 6 */
    {"dot",                dot_bits,                dot_mask_bits,                12, 12,  6,  6},
    /* mask BBX 14 14  -7  -8 → xHot= 7, yHot= 6 */
    {"dotbox",             dotbox_bits,             dotbox_mask_bits,             14, 14,  7,  6},
    /* mask BBX 12 16  -6  -8 → xHot= 6, yHot= 8 */
    {"double_arrow",       double_arrow_bits,       double_arrow_mask_bits,       12, 16,  6,  8},
    /* mask BBX 15 16 -14 -16 → xHot=14, yHot= 0 */
    {"draft_large",        draft_large_bits,        draft_large_mask_bits,        15, 16, 14,  0},
    /* mask BBX 15 15 -14 -15 → xHot=14, yHot= 0 */
    {"draft_small",        draft_small_bits,        draft_small_mask_bits,        15, 15, 14,  0},
    /* mask BBX 14 14  -7  -8 → xHot= 7, yHot= 6 */
    {"draped_box",         draped_box_bits,         draped_box_mask_bits,         14, 14,  7,  6},
    /* mask BBX 16 16  -7  -9 → xHot= 7, yHot= 7 */
    {"exchange",           exchange_bits,           exchange_mask_bits,           16, 16,  7,  7},
    /* mask BBX 16 16  -8  -8 → xHot= 8, yHot= 8 */
    {"fleur",              fleur_bits,              fleur_mask_bits,              16, 16,  8,  8},
    /* mask BBX 16 16 -14 -13 → xHot=14, yHot= 3 */
    {"gobbler",            gobbler_bits,            gobbler_mask_bits,            16, 16, 14,  3},
    /* mask BBX 16 16  -2 -16 → xHot= 2, yHot= 0 */
    {"gumby",              gumby_bits,              gumby_mask_bits,              16, 16,  2,  0},
    /* mask BBX 13 16 -12 -16 → xHot=12, yHot= 0 */
    {"hand1",              hand1_bits,              hand1_mask_bits,              13, 16, 12,  0},
    /* mask BBX 16 16   0 -15 → xHot= 0, yHot= 1 */
    {"hand2",              hand2_bits,              hand2_mask_bits,              16, 16,  0,  1},
    /* mask BBX 15 14  -6  -6 → xHot= 6, yHot= 8 */
    {"heart",              heart_bits,              heart_mask_bits,              15, 14,  6,  8},
    /* mask BBX 16 16  -8  -8 → xHot= 8, yHot= 8 */
    {"icon",               icon_bits,               icon_mask_bits,               16, 16,  8,  8},
    /* mask BBX 16 16  -8  -9 → xHot= 8, yHot= 7 */
    {"iron_cross",         iron_cross_bits,         iron_cross_mask_bits,         16, 16,  8,  7},
    /* mask BBX 10 16  -1 -15 → xHot= 1, yHot= 1 */
    {"left_ptr",           left_ptr_bits,           left_ptr_mask_bits,           10, 16,  1,  1},
    /* mask BBX 16 15  -1  -8 → xHot= 1, yHot= 7 */
    {"left_side",          left_side_bits,          left_side_mask_bits,          16, 15,  1,  7},
    /* mask BBX 12 16  -1  -8 → xHot= 1, yHot= 8 */
    {"left_tee",           left_tee_bits,           left_tee_mask_bits,           12, 16,  1,  8},
    /* mask BBX 15 16  -8  -8 → xHot= 8, yHot= 8 */
    {"leftbutton",         leftbutton_bits,         leftbutton_mask_bits,         15, 16,  8,  8},
    /* mask BBX 12 12  -1  -2 → xHot= 1, yHot=10 */
    {"ll_angle",           ll_angle_bits,           ll_angle_mask_bits,           12, 12,  1, 10},
    /* mask BBX 12 12 -10  -2 → xHot=10, yHot=10 */
    {"lr_angle",           lr_angle_bits,           lr_angle_mask_bits,           12, 12, 10, 10},
    /* mask BBX 16 16 -14 -11 → xHot=14, yHot= 5 */
    {"man",                man_bits,                man_mask_bits,                16, 16, 14,  5},
    {"middlebutton",       middlebutton_bits,       middlebutton_mask_bits,       13, 14,  6,  7},
    {"mouse",              mouse_bits,              mouse_mask_bits,              15, 16,  7,  8},
    {"pencil",             pencil_bits,             pencil_mask_bits,             16, 16,  0, 15},
    {"pirate",             pirate_bits,             pirate_mask_bits,             16, 16,  3,  3},
    {"plus",               plus_bits,               plus_mask_bits,               16, 16,  8,  8},
    {"question_arrow",     question_arrow_bits,     question_arrow_mask_bits,     16, 16, 14, 14},
    {"right_ptr",          right_ptr_bits,          right_ptr_mask_bits,          10, 16,  8,  1},
    {"right_side",         right_side_bits,         right_side_mask_bits,        16, 15, 14,  7},
    {"right_tee",          right_tee_bits,          right_tee_mask_bits,         12, 16, 10,  8},
    {"rightbutton",        rightbutton_bits,        rightbutton_mask_bits,       14, 14,  7,  7},
    {"rtl_logo",           rtl_logo_bits,           rtl_logo_mask_bits,          16, 16,  8,  8},
    {"sailboat",           sailboat_bits,           sailboat_mask_bits,          16, 15, 14,  7},
    {"sb_down_arrow",      sb_down_arrow_bits,      sb_down_arrow_mask_bits,     16, 16,  8,  8},
    {"sb_h_double_arrow",  sb_h_double_arrow_bits,  sb_h_double_arrow_mask_bits, 14, 16,  7,  8},
    {"sb_left_arrow",      sb_left_arrow_bits,      sb_left_arrow_mask_bits,     16, 16,  1,  8},
    {"sb_right_arrow",     sb_right_arrow_bits,     sb_right_arrow_mask_bits,    16, 16, 14,  8},
    {"sb_up_arrow",        sb_up_arrow_bits,        sb_up_arrow_mask_bits,       16, 16,  8,  1},
    {"sb_v_double_arrow",  sb_v_double_arrow_bits,  sb_v_double_arrow_mask_bits, 16, 14,  8,  7},
    {"shuttle",            shuttle_bits,            shuttle_mask_bits,           16, 16,  8,  8},
    {"sizing",             sizing_bits,             sizing_mask_bits,            16, 16,  8,  8},
    {"spider",             spider_bits,             spider_mask_bits,            15, 16,  7,  7},
    {"spraycan",           spraycan_bits,           spraycan_mask_bits,          15, 16,  3,  3},
    {"star",               star_bits,               star_mask_bits,              15, 16,  7,  7},
    {"target",             target_bits,             target_mask_bits,            16, 16,  7,  7},
    {"tcross",             tcross_bits,             tcross_mask_bits,            16, 16,  8,  8},
    {"top_left_arrow",     top_left_arrow_bits,     top_left_arrow_mask_bits,    16, 16,  0,  0},
    {"top_left_corner",    top_left_corner_bits,    top_left_corner_mask_bits,   16, 16,  1,  1},
    {"top_right_corner",   top_right_corner_bits,   top_right_corner_mask_bits,  16, 16, 14,  1},
    {"top_side",           top_side_bits,           top_side_mask_bits,          15, 16,  7,  1},
    {"top_tee",            top_tee_bits,            top_tee_mask_bits,           16, 12,  8,  1},
    {"trek",               trek_bits,               trek_mask_bits,              16, 16,  8,  8},
    {"ul_angle",           ul_angle_bits,           ul_angle_mask_bits,          12, 12,  1,  1},
    {"umbrella",           umbrella_bits,           umbrella_mask_bits,          15, 16,  7, 15},
    {"ur_angle",           ur_angle_bits,           ur_angle_mask_bits,          12, 12, 10,  1},
    {"watch",              watch_bits,              watch_mask_bits,              16, 16, 15,  9},
    {"xterm",              xterm_bits,              xterm_mask_bits,               9, 16,  4,  8},
    {NULL, NULL, NULL, 0, 0, 0, 0}
};

/*
 * The table below is used to map from a cursor name to the data that defines
 * the cursor. This table is used for cursors defined by Tk that don't exist
 * in the standard cursor table.
 */

#define CURSOR_NONE_DATA \
"#define none_width 1\n" \
"#define none_height 1\n" \
"#define none_x_hot 0\n" \
"#define none_y_hot 0\n" \
"static unsigned char none_bits[] = {\n" \
"  0x00};"

static const struct TkCursorName {
    const char *name;
    const char *data;
    char *mask;
} tkCursorNames[] = {
    {"none",    CURSOR_NONE_DATA,       NULL},
    {NULL,      NULL,                   NULL}
};

/* Forward declarations. */
static GLFWcursor* CreateCursorFromBitmapData(const unsigned char* source,
    const unsigned char* mask, int width, int height, int xHot, int yHot,
    unsigned int fgColor, unsigned int bgColor);
static bool LoadImageFile(const char* filename, unsigned char** pixels,
    int* width, int* height, int* xHot, int* yHot);
static bool LoadXBMFile(const char* filename, unsigned char** pixels,
    int* width, int* height);
static unsigned char* ParseXBMData(const char* data, int* width, int* height,
    int* xHot, int* yHot);
static unsigned int ParseColor(const char* colorName);
static GLFWcursor* CreateCursorFromImageData(const unsigned char* rgba,
      int width, int height, int xHot, int yHot);

/* XCursor forward decls */
static bool LoadXcursorFile(const char* filename, unsigned char** pixels,
    int* width, int* height, int* xHot, int* yHot);
static bool TryLoadThemedXcursor(const char* name, unsigned char** pixels,
    int* width, int* height, int* xHot, int* yHot);

/*
 * Cursor application state.
 */

static GLFWwindow *pointerWindow = NULL;
static Tcl_HashTable appliedCursors;
static int appliedCursorsInit = 0;

/* ======================================================================
 * XCursor file format support
 * https://keithp.com/~keithp/render/Xcursor-spec.html
 * ====================================================================== */

#define XCURSOR_MAGIC           0x72756358  /* "Xcur" little endian */
#define XCURSOR_FILE_VERSION    0x00010000
#define XCURSOR_FILE_HEADER_LEN 4
#define XCURSOR_CHUNK_HEADER_LEN 4
#define XCURSOR_IMAGE_TYPE      0xfffd0002

typedef struct {
    uint32_t magic;
    uint32_t header;
    uint32_t version;
    uint32_t ntoc;
} TkXcursorFileHeader;

typedef struct {
    uint32_t type;
    uint32_t subtype;
    uint32_t position;
} TkXcursorFileToc;

typedef struct {
    uint32_t header;    /* chunk header len */
    uint32_t type;
    uint32_t subtype;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t xhot;
    uint32_t yhot;
    uint32_t delay;
    /* then width*height uint32 ARGB pixels */
} TkXcursorImageChunkHeader;

/*
 *----------------------------------------------------------------------
 *
 * ReadU32LE --
 *
 *	Read a 32-bit unsigned integer stored in little-endian byte order
 *	from a buffer. Used for parsing the Xcursor binary file format,
 *	which is defined as little-endian regardless of host byte order.
 *
 * Results:
 *	The 32-bit value assembled from the four bytes at p.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static uint32_t ReadU32LE(const unsigned char *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/*
 *----------------------------------------------------------------------
 *
 * TkIsXcursorMagic --
 *
 *	Check whether a buffer begins with the Xcursor file magic number
 *	("Xcur" in little-endian byte order). Used to identify .xcur
 *	content by inspection rather than trusting a file extension.
 *
 * Results:
 *	true if the buffer is at least 16 bytes long and starts with the
 *	Xcursor magic; false otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static bool TkIsXcursorMagic(const unsigned char *buf, size_t len) {
    if (len < 16) return false;
    uint32_t magic = ReadU32LE(buf);
    return magic == XCURSOR_MAGIC;
}

/*
 *----------------------------------------------------------------------
 *
 * LoadXcursorFile --
 *
 *	Native parser for .xcur files. Picks the largest image in the
 *	file (most sizes store 24, 32, 48 variants) or the first if
 *	several are equal in size. Converts ARGB (Xcursor's native pixel
 *	layout) to RGBA for GLFW. If libXcursor is compiled in
 *	(HAVE_XCURSOR_LIB), its higher-level loader is used first because
 *	it also handles animation frames; on failure the native parser is
 *	used as a fallback.
 *
 * Results:
 *	true on success, with *pixels set to a Tcl-allocated RGBA buffer
 *	(release with Tcl_Free) and *width, *height, *xHot, *yHot filled
 *	in from the chosen image. false on any error.
 *
 * Side effects:
 *	Allocates pixel data and reads the named file from disk.
 *
 *----------------------------------------------------------------------
 */

static bool
LoadXcursorFile(
    const char* filename,
    unsigned char** pixels,
    int* width,
    int* height,
    int* xHot,
    int* yHot)
{
#ifdef HAVE_XCURSOR_LIB
    /* Fast path via libXcursor if available - handles animated cursors etc. */
    {
        XcursorImages *images = XcursorFilenameLoadImages(filename, 0);
        if (images && images->nimage > 0) {
            XcursorImage *img = images->images[0];
            /* pick largest */
            size_t best = 0;
            size_t bestPixels = 0;
            for (int i = 0; i < images->nimage; i++) {
                size_t px = (size_t)images->images[i]->width * images->images[i]->height;
                if (px > bestPixels) { bestPixels = px; best = i; }
            }
            img = images->images[best];
            int w = img->width;
            int h = img->height;
            unsigned char *rgba = Tcl_Alloc(w * h * 4);
            if (rgba) {
                for (int i = 0; i < w*h; i++) {
                    uint32_t argb = img->pixels[i];
                    unsigned char a = (argb >> 24) & 0xFF;
                    unsigned char r = (argb >> 16) & 0xFF;
                    unsigned char g = (argb >> 8) & 0xFF;
                    unsigned char b = argb & 0xFF;
                    rgba[i*4+0] = r;
                    rgba[i*4+1] = g;
                    rgba[i*4+2] = b;
                    rgba[i*4+3] = a;
                }
                *pixels = rgba;
                *width = w;
                *height = h;
                *xHot = img->xhot;
                *yHot = img->yhot;
                XcursorImagesDestroy(images);
                return true;
            }
            XcursorImagesDestroy(images);
        }
        /* fall through to native parser on failure */
    }
#endif

    FILE *fp = fopen(filename, "rb");
    if (!fp) return false;

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
    long fsize = ftell(fp);
    if (fsize < 16 || fsize > 16*1024*1024) { fclose(fp); return false; }
    rewind(fp);

    unsigned char *fileData = Tcl_Alloc(fsize);
    if (!fileData) { fclose(fp); return false; }
    if (fread(fileData, 1, fsize, fp) != (size_t)fsize) {
        Tcl_Free(fileData); fclose(fp); return false;
    }
    fclose(fp);

    if (!TkIsXcursorMagic(fileData, fsize)) {
        Tcl_Free(fileData); return false;
    }

    TkXcursorFileHeader fh;
    fh.magic  = ReadU32LE(fileData + 0);
    fh.header = ReadU32LE(fileData + 4);
    fh.version= ReadU32LE(fileData + 8);
    fh.ntoc   = ReadU32LE(fileData + 12);

    if (fh.header < 16 || fh.ntoc > 100 || fh.ntoc == 0) {
        Tcl_Free(fileData); return false;
    }

    size_t tocOffset = fh.header;
    if (tocOffset + fh.ntoc * 12 > (size_t)fsize) {
        Tcl_Free(fileData); return false;
    }

    int bestW = 0, bestH = 0;
    int bestXHot = 0, bestYHot = 0;
    unsigned char *bestRGBA = NULL;
    size_t bestPixelsCount = 0;

    for (uint32_t i = 0; i < fh.ntoc; i++) {
        size_t off = tocOffset + i*12;
        uint32_t type    = ReadU32LE(fileData + off);
        uint32_t subtype = ReadU32LE(fileData + off + 4);
        uint32_t position= ReadU32LE(fileData + off + 8);

        if (type != XCURSOR_IMAGE_TYPE) continue;
        if (position + 36 > (size_t)fsize) continue; /* at least header */

        TkXcursorImageChunkHeader ch;
        ch.header  = ReadU32LE(fileData + position);
        ch.type    = ReadU32LE(fileData + position + 4);
        ch.subtype = ReadU32LE(fileData + position + 8);
        ch.version = ReadU32LE(fileData + position + 12);
        ch.width   = ReadU32LE(fileData + position + 16);
        ch.height  = ReadU32LE(fileData + position + 20);
        ch.xhot    = ReadU32LE(fileData + position + 24);
        ch.yhot    = ReadU32LE(fileData + position + 28);
        ch.delay   = ReadU32LE(fileData + position + 32);

        if (ch.width == 0 || ch.height == 0 || ch.width > 512 || ch.height > 512) continue;
        size_t pixelCount = (size_t)ch.width * ch.height;
        size_t dataOff = position + ch.header;
        if (dataOff + pixelCount*4 > (size_t)fsize) continue;

        /* Keep largest */
        if (bestRGBA && pixelCount <= bestPixelsCount) {
            /* prefer larger, but keep first if equal - skip smaller */
            if (pixelCount < bestPixelsCount) continue;
        }

        unsigned char *rgba = Tcl_Alloc(pixelCount * 4);
        if (!rgba) continue;

        const unsigned char *src = fileData + dataOff;
        for (size_t p = 0; p < pixelCount; p++) {
            uint32_t argb = ReadU32LE(src + p*4);
            unsigned char a = (argb >> 24) & 0xFF;
            unsigned char r = (argb >> 16) & 0xFF;
            unsigned char g = (argb >> 8) & 0xFF;
            unsigned char b = argb & 0xFF;
            rgba[p*4+0] = r;
            rgba[p*4+1] = g;
            rgba[p*4+2] = b;
            rgba[p*4+3] = a;
        }

        if (bestRGBA) Tcl_Free(bestRGBA);
        bestRGBA = rgba;
        bestPixelsCount = pixelCount;
        bestW = ch.width;
        bestH = ch.height;
        bestXHot = ch.xhot;
        bestYHot = ch.yhot;
        /* subtype is nominal size, we ignore for now */
        (void)subtype;
    }

    Tcl_Free(fileData);

    if (!bestRGBA) return false;

    *pixels = bestRGBA;
    *width = bestW;
    *height = bestH;
    *xHot = bestXHot;
    *yHot = bestYHot;
    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TryLoadThemedXcursor --
 *
 *	Search for a cursor by name in the user's current cursor theme
 *	and load it as RGBA pixel data. If libXcursor is compiled in
 *	(HAVE_XCURSOR_LIB), its theme inheritance and XCURSOR_THEME /
 *	XCURSOR_PATH resolution are used directly; otherwise this
 *	function walks a hand-built list of standard icon directories
 *	derived from XCURSOR_PATH, ~/.icons, ~/.local/share/icons, and
 *	/usr/share/icons (plus /usr/local/share/icons and
 *	/usr/share/pixmaps), falling back to the name treated as a path.
 *
 * Results:
 *	true on success, with *pixels set to a Tcl-allocated RGBA buffer
 *	(release with Tcl_Free) and *width, *height, *xHot, *yHot filled
 *	in from the loaded cursor. false if no matching cursor was found
 *	or loading failed.
 *
 * Side effects:
 *	Allocates pixel data, reads files, and (in the manual fallback
 *	path) allocates and frees temporary path strings via malloc/free.
 *
 *----------------------------------------------------------------------
 */

static bool
TryLoadThemedXcursor(
    const char* name,
    unsigned char** pixels,
    int* width,
    int* height,
    int* xHot,
    int* yHot)
{
    if (!name || !*name) return false;

#ifdef HAVE_XCURSOR_LIB
    /* libXcursor handles theme inheritance, XCURSOR_THEME, etc. */
    {
        XcursorImages *images = XcursorLibraryLoadImages(name, NULL, 0);
        if (images && images->nimage > 0) {
            XcursorImage *img = images->images[0];
            size_t best = 0;
            size_t bestPixels = 0;
            for (int i = 0; i < images->nimage; i++) {
                size_t px = (size_t)images->images[i]->width * images->images[i]->height;
                if (px > bestPixels) { bestPixels = px; best = i; }
            }
            img = images->images[best];
            int w = img->width;
            int h = img->height;
            unsigned char *rgba = Tcl_Alloc(w*h*4);
            if (rgba) {
                for (int i = 0; i < w*h; i++) {
                    uint32_t argb = img->pixels[i];
                    rgba[i*4+0] = (argb >> 16) & 0xFF;
                    rgba[i*4+1] = (argb >> 8) & 0xFF;
                    rgba[i*4+2] = argb & 0xFF;
                    rgba[i*4+3] = (argb >> 24) & 0xFF;
                }
                *pixels = rgba;
                *width = w;
                *height = h;
                *xHot = img->xhot;
                *yHot = img->yhot;
                XcursorImagesDestroy(images);
                return true;
            }
            XcursorImagesDestroy(images);
        }
    }
#endif

    /* Manual theme search fallback */
    const char *xcursorPath = getenv("XCURSOR_PATH");
    const char *home = getenv("HOME");
    const char *theme = getenv("XCURSOR_THEME");
    if (!theme) theme = "default";

    char candidate[PATH_MAX];
    const char *searchPaths[16];
    int nPaths = 0;

    /* Build search path list */
    if (xcursorPath) {
        /* XCURSOR_PATH is colon-separated */
        char *dup = strdup(xcursorPath);
        if (dup) {
            char *tok = strtok(dup, ":");
            while (tok && nPaths < 12) {
                snprintf(candidate, sizeof(candidate), "%s/%s", tok, name);
                searchPaths[nPaths++] = strdup(candidate);
                tok = strtok(NULL, ":");
            }
            free(dup);
        }
    }

    /* User icon dirs */
    if (home) {
        snprintf(candidate, sizeof(candidate), "%s/.icons/%s/cursors/%s", home, theme, name);
        if (nPaths < 16) searchPaths[nPaths++] = strdup(candidate);
        snprintf(candidate, sizeof(candidate), "%s/.icons/default/cursors/%s", home, name);
        if (nPaths < 16) searchPaths[nPaths++] = strdup(candidate);
        snprintf(candidate, sizeof(candidate), "%s/.local/share/icons/%s/cursors/%s", home, theme, name);
        if (nPaths < 16) searchPaths[nPaths++] = strdup(candidate);
        snprintf(candidate, sizeof(candidate), "%s/.local/share/icons/default/cursors/%s", home, name);
        if (nPaths < 16) searchPaths[nPaths++] = strdup(candidate);
    }

    /* System dirs */
    static const char *sysPrefixes[] = {
        "/usr/share/icons",
        "/usr/local/share/icons",
        "/usr/share/pixmaps",
        NULL
    };
    for (int i = 0; sysPrefixes[i] && nPaths < 14; i++) {
        snprintf(candidate, sizeof(candidate), "%s/%s/cursors/%s", sysPrefixes[i], theme, name);
        searchPaths[nPaths++] = strdup(candidate);
        if (strcmp(theme, "default") != 0) {
            snprintf(candidate, sizeof(candidate), "%s/default/cursors/%s", sysPrefixes[i], name);
            if (nPaths < 16) searchPaths[nPaths++] = strdup(candidate);
        }
        snprintf(candidate, sizeof(candidate), "%s/%s", sysPrefixes[i], name);
        if (nPaths < 16) searchPaths[nPaths++] = strdup(candidate);
    }

    bool found = false;
    for (int i = 0; i < nPaths && !found; i++) {
        const char *p = searchPaths[i];
        if (!p) continue;
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        unsigned char magic[16];
        size_t n = fread(magic, 1, 16, f);
        fclose(f);
        if (n >= 4 && TkIsXcursorMagic(magic, n)) {
            if (LoadXcursorFile(p, pixels, width, height, xHot, yHot)) {
                found = true;
            }
        }
    }

    for (int i = 0; i < nPaths; i++) free((void*)searchPaths[i]);

    /* Last resort: try absolute / named file as-is */
    if (!found) {
        if (LoadXcursorFile(name, pixels, width, height, xHot, yHot)) {
            found = true;
        }
    }

    return found;
}

/*
 *----------------------------------------------------------------------
 *
 * ConvertXBMToRGBA --
 *
 *      Convert X11 bitmap data to RGBA format for GLFW.
 *
 * Results:
 *      Returns allocated RGBA pixel data, or NULL on error.
 *
 * Side effects:
 *      Allocates memory.
 *
 *----------------------------------------------------------------------
 */

static unsigned char*
ConvertXBMToRGBA(
    const unsigned char* source,    /* X11 bitmap data */
    const unsigned char* mask,      /* Mask data (optional) */
    int width, int height,          /* Dimensions */
    unsigned int fgColor,           /* Foreground color 0xAABBGGRR */
    unsigned int bgColor)           /* Background color 0xAABBGGRR */
{
    int pixelCount = width * height;
    unsigned char* rgba = Tcl_Alloc(pixelCount * 4);

    if (!rgba) {
        return NULL;
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int byteIndex = y * ((width + 7) / 8) + (x / 8);
            int bitIndex = 7 - (x % 8);
            int srcBit = (source[byteIndex] >> bitIndex) & 1;
            int maskBit = mask ? ((mask[byteIndex] >> bitIndex) & 1) : 1;

            int rgbaIndex = (y * width + x) * 4;

            if (maskBit == 0) {
                rgba[rgbaIndex] = 0;
                rgba[rgbaIndex+1] = 0;
                rgba[rgbaIndex+2] = 0;
                rgba[rgbaIndex+3] = 0;
            } else if (srcBit == 1) {
                rgba[rgbaIndex] = (fgColor >> 16) & 0xFF;
                rgba[rgbaIndex+1] = (fgColor >> 8) & 0xFF;
                rgba[rgbaIndex+2] = fgColor & 0xFF;
                rgba[rgbaIndex+3] = (fgColor >> 24) & 0xFF;
            } else {
                rgba[rgbaIndex] = (bgColor >> 16) & 0xFF;
                rgba[rgbaIndex+1] = (bgColor >> 8) & 0xFF;
                rgba[rgbaIndex+2] = bgColor & 0xFF;
                rgba[rgbaIndex+3] = (bgColor >> 24) & 0xFF;
            }
        }
    }

    return rgba;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateCursorFromBitmapData --
 *
 *      Creates a cursor from X11 bitmap data.
 *
 * Results:
 *      Returns a new GLFW cursor, or NULL on error.
 *
 * Side effects:
 *      Allocates resources.
 *
 *----------------------------------------------------------------------
 */

static GLFWcursor*
CreateCursorFromBitmapData(
    const unsigned char* source,    /* X11 bitmap data */
    const unsigned char* mask,      /* Mask data (optional) */
    int width, int height,          /* Dimensions */
    int xHot, int yHot,             /* Hot spot */
    unsigned int fgColor,           /* Foreground color */
    unsigned int bgColor)           /* Background color */
{
    unsigned char* rgba = ConvertXBMToRGBA(source, mask, width, height,
                                          fgColor, bgColor);
    if (!rgba) {
        return NULL;
    }

    GLFWimage image;
    image.width = width;
    image.height = height;
    image.pixels = rgba;

    GLFWcursor* cursor = glfwCreateCursor(&image, xHot, yHot);

    if (!cursor) {
        const char* glfwError;
        int code = glfwGetError(&glfwError);
        if (code != 0) {
            fprintf(stderr, "  GLFW error %d: %s\n", code, glfwError);
        }
    }

    Tcl_Free(rgba);
    return cursor;
}


/*
 *----------------------------------------------------------------------
 *
 * StbiToTclBuffer --
 *
 *      Copy an RGBA buffer allocated by stb_image into Tcl-allocated
 *      memory and release the stb buffer, so that every pixel buffer
 *      handed back by the loaders in this file can be released with
 *      Tcl_Free.
 *
 * Results:
 *      Returns the Tcl-allocated copy.
 *
 *----------------------------------------------------------------------
 */

static unsigned char *
StbiToTclBuffer(
    unsigned char *src,
    int width,
    int height)
{
    size_t n = (size_t) width * (size_t) height * 4;
    unsigned char *out = (unsigned char *) Tcl_Alloc(n);

    memcpy(out, src, n);
    stbi_image_free(src);
    return out;
}

/*
 *----------------------------------------------------------------------
 *
 * IsWinCursorMagic --
 *
 *	Check whether a buffer begins with a Windows ICO/CUR file header:
 *	reserved field = 0, type = 1 (icon) or 2 (cursor).
 *
 * Results:
 *	true if the first six bytes match the Windows icon/cursor magic;
 *	false otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static bool
IsWinCursorMagic(
    const unsigned char *buf,
    size_t len)
{
    return len >= 6 && buf[0] == 0 && buf[1] == 0 &&
            (buf[2] == 1 || buf[2] == 2) && buf[3] == 0;
}

/*
 *----------------------------------------------------------------------
 *
 * RdLE16 --
 *
 *	Read a 16-bit unsigned integer stored in little-endian byte order
 *	from a buffer. Used for parsing the Windows ICO/CUR directory and
 *	DIB headers, which are little-endian.
 *
 * Results:
 *	The 16-bit value assembled from the two bytes at p.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static uint16_t
RdLE16(const unsigned char *p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}

/*
 *----------------------------------------------------------------------
 *
 * RdLE32 --
 *
 *	Read a 32-bit unsigned integer stored in little-endian byte order
 *	from a buffer. Used for parsing the Windows ICO/CUR directory and
 *	DIB headers, which are little-endian.
 *
 * Results:
 *	The 32-bit value assembled from the four bytes at p.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static uint32_t
RdLE32(const unsigned char *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
            ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

/*
 *----------------------------------------------------------------------
 *
 * LoadWinCursorFile --
 *
 *      Load a Windows .cur (or .ico) file. The largest image in the file
 *      is used. PNG-compressed images and uncompressed 1, 4, 8, 24 and
 *      32 bpp DIB images are supported.
 *
 * Results:
 *      Returns true on success, with *pixels set to a Tcl-allocated RGBA
 *      buffer, and the hot spot set from the directory entry (the image
 *      center for .ico files).
 *
 * Side effects:
 *      Allocates pixel data.
 *
 *----------------------------------------------------------------------
 */

static bool
LoadWinCursorFile(
    const char *filename,
    unsigned char **pixels,
    int *width,
    int *height,
    int *xHot,
    int *yHot)
{
    FILE *fp;
    unsigned char *d = NULL, *out = NULL;
    const unsigned char *e, *img;
    long size;
    bool ok = false;
    int type, count, i, best = -1, w = 0, h = 0;
    unsigned bestArea = 0;
    uint32_t imgLen;

    fp = fopen(filename, "rb");
    if (!fp) {
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 22 ||
            size > 16*1024*1024 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }
    d = (unsigned char *) Tcl_Alloc((size_t) size);
    if (fread(d, 1, (size_t) size, fp) != (size_t) size) {
        fclose(fp);
        Tcl_Free(d);
        return false;
    }
    fclose(fp);

    type = RdLE16(d + 2);
    count = RdLE16(d + 4);
    if (RdLE16(d) != 0 || (type != 1 && type != 2) || count < 1 ||
            size < 6 + 16 * (long) count) {
        goto done;
    }

    /* Pick the largest image. */

    for (i = 0; i < count; i++) {
        const unsigned char *ent = d + 6 + 16 * i;
        unsigned ew = ent[0] ? ent[0] : 256, eh = ent[1] ? ent[1] : 256;
        uint32_t bytes = RdLE32(ent + 8), off = RdLE32(ent + 12);

        if (off >= (unsigned long) size ||
                bytes > (unsigned long) size - off) {
            continue;
        }
        if (ew * eh > bestArea) {
            bestArea = ew * eh;
            best = i;
        }
    }
    if (best < 0) {
        goto done;
    }

    e = d + 6 + 16 * best;
    img = d + RdLE32(e + 12);
    imgLen = RdLE32(e + 8);

    if (imgLen >= 8 && memcmp(img, "\x89PNG", 4) == 0) {
        int ch;
        unsigned char *p = stbi_load_from_memory(img, (int) imgLen, &w, &h,
                &ch, 4);

        if (!p) {
            goto done;
        }
        out = StbiToTclBuffer(p, w, h);
    } else {
        uint32_t hdr = imgLen >= 40 ? RdLE32(img) : 0;
        int bw, bh, bpp, y, x;
        uint32_t comp, used, nPal;
        size_t xorOff, xorStride, andStride;
        bool hasMask, anyAlpha = false;
        const unsigned char *pal, *andBits;

        if (hdr < 40 || hdr > imgLen) {
            goto done;
        }
        bw = (int32_t) RdLE32(img + 4);
        bh = (int32_t) RdLE32(img + 8) / 2;     /* XOR image + AND mask */
        bpp = RdLE16(img + 14);
        comp = RdLE32(img + 16);
        used = RdLE32(img + 32);
        if (bw <= 0 || bh <= 0 || bw > 1024 || bh > 1024 || comp != 0 ||
                (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 &&
                bpp != 32)) {
            goto done;
        }
        w = bw;
        h = bh;
        nPal = (bpp <= 8) ? (used ? used : (1u << bpp)) : 0;
        if (nPal > 256) {
            goto done;
        }
        xorOff = hdr + (size_t) nPal * 4;
        xorStride = (((size_t) w * bpp + 31) / 32) * 4;
        andStride = (((size_t) w + 31) / 32) * 4;
        if (xorOff + xorStride * h > imgLen) {
            goto done;
        }
        hasMask = xorOff + xorStride * h + andStride * h <= imgLen;
        pal = img + hdr;
        andBits = img + xorOff + xorStride * h;

        out = (unsigned char *) Tcl_Alloc((size_t) w * h * 4);
        for (y = 0; y < h; y++) {
            const unsigned char *xr = img + xorOff + xorStride * (h - 1 - y);

            for (x = 0; x < w; x++) {
                unsigned char r, g, b, a = 255;
                unsigned char *o = out + ((size_t) y * w + x) * 4;

                if (bpp == 32) {
                    b = xr[x*4]; g = xr[x*4+1]; r = xr[x*4+2]; a = xr[x*4+3];
                    if (a) {
                        anyAlpha = true;
                    }
                } else if (bpp == 24) {
                    b = xr[x*3]; g = xr[x*3+1]; r = xr[x*3+2];
                } else {
                    uint32_t idx;

                    if (bpp == 8) {
                        idx = xr[x];
                    } else if (bpp == 4) {
                        idx = (x & 1) ? (xr[x/2] & 15) : (xr[x/2] >> 4);
                    } else {
                        idx = (xr[x/8] >> (7 - (x & 7))) & 1;
                    }
                    if (idx >= nPal) {
                        idx = 0;
                    }
                    b = pal[idx*4]; g = pal[idx*4+1]; r = pal[idx*4+2];
                }
                o[0] = r; o[1] = g; o[2] = b; o[3] = a;
            }
        }

        /* The AND mask supplies transparency unless 32bpp has real alpha. */

        if (hasMask && !(bpp == 32 && anyAlpha)) {
            for (y = 0; y < h; y++) {
                const unsigned char *ar = andBits + andStride * (h - 1 - y);

                for (x = 0; x < w; x++) {
                    int bit = (ar[x/8] >> (7 - (x & 7))) & 1;

                    out[((size_t) y * w + x) * 4 + 3] = bit ? 0 : 255;
                }
            }
        }
    }

    *width = w;
    *height = h;
    *xHot = (type == 2) ? RdLE16(e + 4) : w / 2;
    *yHot = (type == 2) ? RdLE16(e + 6) : h / 2;
    if (*xHot >= w) *xHot = w - 1;
    if (*yHot >= h) *yHot = h - 1;
    *pixels = out;
    out = NULL;
    ok = true;

  done:
    if (out) {
        Tcl_Free(out);
    }
    Tcl_Free(d);
    return ok;
}

/*
 *----------------------------------------------------------------------
 *
 * LoadImageFile --
 *
 *      Load a cursor image file for "@file" cursor specs. The file type
 *      is determined by content, not extension: Xcursor, Windows
 *      CUR/ICO, then (by extension) XBM, then anything stb_image can
 *      decode (PNG etc.), then XBM as a last resort.
 *
 * Results:
 *      Returns true on success, with *pixels set to a Tcl-allocated RGBA
 *      buffer (release with Tcl_Free). *xHot and *yHot are the file's
 *      hot spot, or -1 if the format has none.
 *
 * Side effects:
 *      Allocates pixel data.
 *
 *----------------------------------------------------------------------
 */

static bool
LoadImageFile(
    const char* filename,
    unsigned char** pixels,
    int* width,
    int* height,
    int* xHot,
    int* yHot)
{
    unsigned char magic[16];
    size_t n = 0;
    FILE *f;
    const char *ext;
    int channels;
    unsigned char *data;

    *xHot = *yHot = -1;

    f = fopen(filename, "rb");
    if (!f) {
        return false;
    }
    n = fread(magic, 1, sizeof(magic), f);
    fclose(f);

    if (n >= 4 && TkIsXcursorMagic(magic, n)) {
        return LoadXcursorFile(filename, pixels, width, height, xHot, yHot);
    }
    if (IsWinCursorMagic(magic, n)) {
        return LoadWinCursorFile(filename, pixels, width, height, xHot, yHot);
    }

    ext = strrchr(filename, '.');
    if (ext && strcasecmp(ext, ".xbm") == 0) {
        return LoadXBMFile(filename, pixels, width, height);
    }

    /* PNG and anything else stb_image decodes. */

    data = stbi_load(filename, width, height, &channels, 4);
    if (data) {
        *pixels = StbiToTclBuffer(data, *width, *height);
        return true;
    }

    /* Fallback: try as an XBM file. */

    return LoadXBMFile(filename, pixels, width, height);
}

/*
 *----------------------------------------------------------------------
 *
 * ParseXBMData --
 *
 *      Parse XBM format data to extract bitmap.
 *
 * Results:
 *      Returns allocated bitmap data, or NULL on error.
 *
 * Side effects:
 *      Allocates memory.
 *
 *----------------------------------------------------------------------
 */

static unsigned char*
ParseXBMData(
    const char* data,
    int* width,
    int* height,
    int* xHot,
    int* yHot)
{
    const char* ptr = data;
    char name[256];

    *width = *height = 16;
    *xHot = *yHot = 0;

    while (*ptr) {
        if (sscanf(ptr, "#define %255s %d", name, width) == 2) {
            if (strstr(name, "_width")) break;
        }
        ptr++;
    }

    ptr = data;
    while (*ptr) {
        if (sscanf(ptr, "#define %255s %d", name, height) == 2) {
            if (strstr(name, "_height")) break;
        }
        ptr++;
    }

    ptr = data;
    while (*ptr) {
        if (sscanf(ptr, "#define %255s %d", name, xHot) == 2) {
            if (strstr(name, "_x_hot")) break;
        }
        ptr++;
    }

    ptr = data;
    while (*ptr) {
        if (sscanf(ptr, "#define %255s %d", name, yHot) == 2) {
            if (strstr(name, "_y_hot")) break;
        }
        ptr++;
    }

    ptr = strstr(data, "static char");
    if (!ptr) ptr = strstr(data, "static unsigned char");
    if (!ptr) return NULL;

    ptr = strchr(ptr, '{');
    if (!ptr) return NULL;
    ptr++;

    int byteCount = ((*width + 7) / 8) * *height;
    unsigned char* bits = Tcl_Alloc(byteCount);
    if (!bits) return NULL;

    int index = 0;
    while (*ptr && *ptr != '}' && index < byteCount) {
        if (*ptr == '0' && (*(ptr+1) == 'x' || *(ptr+1) == 'X')) {
            unsigned int val;
            if (sscanf(ptr, "0x%x", &val) == 1) {
                bits[index++] = val & 0xFF;
            }
            ptr = strchr(ptr, ',');
            if (!ptr) break;
        }
        ptr++;
    }

    return bits;
}

/*
 *----------------------------------------------------------------------
 *
 * LoadXBMFile --
 *
 *      Load an XBM file specifically.
 *
 * Results:
 *      Returns true on success, false on failure.
 *
 * Side effects:
 *      Allocates pixel data.
 *
 *----------------------------------------------------------------------
 */

static bool
LoadXBMFile(
    const char* filename,
    unsigned char** pixels,
    int* width,
    int* height)
{
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        return false;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }

    long fileSizeLong = ftell(fp);
    if (fileSizeLong < 0) {
        fclose(fp);
        return false;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return false;
    }

    if ((unsigned long)fileSizeLong > SIZE_MAX - 1) {
        fclose(fp);
        return false;
    }

    size_t fileSize = (size_t)fileSizeLong;

    char *fileData = Tcl_Alloc(fileSize + 1);
    if (!fileData) {
        fclose(fp);
        return false;
    }

    size_t bytesRead = fread(fileData, 1, fileSize, fp);
    fclose(fp);

    if (bytesRead != fileSize) {
        Tcl_Free(fileData);
        return false;
    }

    fileData[fileSize] = '\0';

    int xHot, yHot;
    unsigned char* xbmBits = ParseXBMData(fileData, width, height, &xHot, &yHot);
    Tcl_Free(fileData);

    if (!xbmBits) {
        return false;
    }

    *pixels = ConvertXBMToRGBA(xbmBits, NULL, *width, *height,
                               0xFF000000, 0xFFFFFFFF);
    Tcl_Free(xbmBits);

    return (*pixels != NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * ParseColor --
 *
 *      Parse a color name or hex value to ARGB format.
 *
 * Results:
 *      Returns color in 0xAARRGGBB format.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static unsigned int
ParseColor(
    const char* colorName)
{
    static struct {
        const char* name;
        unsigned int value;
    } colors[] = {
        {"black", 0xFF000000},
        {"white", 0xFFFFFFFF},
        {"red", 0xFFFF0000},
        {"green", 0xFF00FF00},
        {"blue", 0xFF0000FF},
        {"yellow", 0xFFFFFF00},
        {"cyan", 0xFF00FFFF},
        {"magenta", 0xFFFF00FF},
        {NULL, 0}
    };

    for (int i = 0; colors[i].name; i++) {
        if (strcasecmp(colorName, colors[i].name) == 0) {
            return colors[i].value;
        }
    }

    if (colorName[0] == '#') {
        unsigned int rgb = 0;
        if (sscanf(colorName + 1, "%x", &rgb) == 1) {
            if (strlen(colorName) <= 7) {
                return 0xFF000000 | rgb;
            } else {
                return ((rgb & 0xFF) << 24) | ((rgb >> 8) & 0x00FFFFFF);
            }
        }
    }

    return 0xFF000000;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateCursorFromImageData --
 *
 *      Creates a cursor from RGBA image data.
 *
 * Results:
 *      Returns a new GLFW cursor, or NULL on error.
 *
 * Side effects:
 *      Allocates resources.
 *
 *----------------------------------------------------------------------
 */

static GLFWcursor*
CreateCursorFromImageData(
    const unsigned char* rgba,
    int width, int height,
    int xHot, int yHot)
{
    if (!rgba || width <= 0 || height <= 0) {
        return NULL;
    }

    GLFWimage image;
    image.width = width;
    image.height = height;
    image.pixels = (unsigned char*)rgba;

    return glfwCreateCursor(&image, xHot, yHot);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetCursorByName --
 *
 *      Retrieve a cursor by name. Parse the cursor name into fields and
 *      create a cursor. Now supports XCursor theme names.
 *
 * Results:
 *      Returns a new cursor, or NULL on errors.
 *
 * Side effects:
 *      Allocates a new cursor.
 *
 *----------------------------------------------------------------------
 */

TkCursor *
TkGetCursorByName(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    const char *string)

{
    TkWaylandCursor *cursorPtr = NULL;
    Tcl_Size argc;
    const char **argv = NULL;
    const struct TkCursorName *tkCursorPtr = NULL;
    const struct BuiltinCursor *builtinPtr = NULL;
    int standardShape = -1;
    GLFWcursor* glfwCursor = NULL;
    unsigned int fgColor = 0xFF000000;
    unsigned int bgColor = 0xFFFFFFFF;
    int fileWidth = 0, fileHeight = 0;
    int fileXHot = 0, fileYHot = 0;

    if (Tcl_SplitList(interp, string, &argc, &argv) != TCL_OK) {
        return NULL;
    }
    if (argc == 0) {
        goto badString;
    }

    if (argv[0][0] != '@') {
        for (tkCursorPtr = tkCursorNames; ; tkCursorPtr++) {
            if (tkCursorPtr->name == NULL) {
                tkCursorPtr = NULL;
                break;
            }
            if ((tkCursorPtr->name[0] == argv[0][0]) &&
                    (strcmp(tkCursorPtr->name, argv[0]) == 0)) {
                break;
            }
        }
    }

    if ((argv[0][0] != '@') && (tkCursorPtr == NULL)) {
        const struct CursorName *namePtr;
        for (namePtr = cursorNames; namePtr->name != NULL; namePtr++) {
            if ((namePtr->name[0] == argv[0][0]) &&
                    (strcmp(namePtr->name, argv[0]) == 0)) {
                standardShape = namePtr->shape;
                break;
            }
        }
    }

    if ((argv[0][0] != '@') && (tkCursorPtr == NULL) && (standardShape == -1)) {
        for (builtinPtr = builtinCursors; builtinPtr->name != NULL; builtinPtr++) {
            if (strcmp(builtinPtr->name, argv[0]) == 0) {
                break;
            }
        }
        if (builtinPtr->name == NULL) {
            builtinPtr = NULL;
        }
    }

    if (argc >= 2) {
        fgColor = ParseColor(argv[1]);
    }
    if (argc >= 3) {
        bgColor = ParseColor(argv[2]);
    }

    /* Dispatch */

    if (argv[0][0] == '@') {
        if (Tcl_IsSafe(interp)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(
                    "cannot get cursor from a file in a safe interpreter",
                    TCL_INDEX_NONE));
            Tcl_SetErrorCode(interp, "TK", "SAFE", "CURSOR_FILE", (char *)NULL);
            goto cleanup;
        }
        {
            const char *filePath = argv[0] + 1;
            unsigned char *px = NULL;
            int hx = -1, hy = -1;

            /*
             * The file type is detected by content (Xcursor, Windows
             * CUR/ICO, PNG, XBM), not by extension.
             */

            if (!LoadImageFile(filePath, &px, &fileWidth, &fileHeight,
                    &hx, &hy)) {
                goto badString;
            }
            if (hx < 0 || hy < 0) {
                hx = fileWidth / 2;
                hy = fileHeight / 2;
            }
            fileXHot = hx;
            fileYHot = hy;
            glfwCursor = CreateCursorFromImageData(px, fileWidth,
                    fileHeight, hx, hy);
            Tcl_Free(px);
        }
        standardShape = -1;

    } else if (tkCursorPtr != NULL) {
        if (strcmp(argv[0], "none") == 0) {
            unsigned char transparent[4] = {0, 0, 0, 0};
            GLFWimage image;
            image.width = 1;
            image.height = 1;
            image.pixels = transparent;
            glfwCursor = glfwCreateCursor(&image, 0, 0);
        } else {
            int width, height, xHot, yHot;
            unsigned char* bits = ParseXBMData(tkCursorPtr->data,
                                               &width, &height, &xHot, &yHot);
            if (bits) {
                unsigned char* mask = NULL;
                if (tkCursorPtr->mask) {
                    int maskWidth, maskHeight;
                    mask = ParseXBMData(tkCursorPtr->mask,
                                       &maskWidth, &maskHeight,
                                       &xHot, &yHot);
                }
                glfwCursor = CreateCursorFromBitmapData(bits, mask,
                                                       width, height,
                                                       xHot, yHot,
                                                       fgColor, bgColor);
                Tcl_Free(bits);
                if (mask) Tcl_Free(mask);
            }
        }
        standardShape = -1;

    } else if (standardShape != -1) {
        glfwCursor = glfwCreateStandardCursor(standardShape);

    } else if (builtinPtr != NULL) {
        glfwCursor = CreateCursorFromBitmapData(builtinPtr->bits,
                                                builtinPtr->mask,
                                                builtinPtr->width,
                                                builtinPtr->height,
                                                builtinPtr->xHot,
                                                builtinPtr->yHot,
                                                fgColor,
                                                bgColor);

    } else {
        /* Final attempt: try XCursor theme lookup by name (e.g. "left_ptr" 
         * from Adwaita/Breeze maps to themed cursor). This also handles
         * custom cursor names installed by user themes. */
        unsigned char *xPixels = NULL;
        int xW=0, xH=0, xHot=0, yHot=0;
        if (TryLoadThemedXcursor(argv[0], &xPixels, &xW, &xH, &xHot, &yHot)) {
            glfwCursor = CreateCursorFromImageData(xPixels, xW, xH, xHot, yHot);
            Tcl_Free(xPixels);
            fileWidth = xW;
            fileHeight = xH;
            fileXHot = xHot;
            fileYHot = yHot;
            standardShape = -1;
            builtinPtr = NULL;
        } else {
#ifdef TK_WAYLAND_CURSOR_SILENT_FALLBACK
            if (getenv("TK_WAYLAND_DEBUG_CURSOR") != NULL) {
                fprintf(stderr, "tkWaylandCursor: unknown cursor \"%s\", using arrow\n", string);
            }
            glfwCursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
            standardShape = GLFW_ARROW_CURSOR;
#else
            goto badString;
#endif
        }
    }

    if (glfwCursor == NULL) {
        const char *glfwErr = NULL;
        int glfwCode = glfwGetError(&glfwErr);

        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                    "couldn't create cursor \"%s\"%s%s", string,
                    glfwCode ? ": " : "", glfwCode ? glfwErr : ""));
            Tcl_SetErrorCode(interp, "TK", "VALUE", "CURSOR", (char *)NULL);
        }
        Tcl_Free(argv);
        return NULL;
    }

    if (glfwCursor != NULL) {
        cursorPtr = (TkWaylandCursor *)Tcl_Alloc(sizeof(TkWaylandCursor));
        memset(cursorPtr, 0, sizeof(TkWaylandCursor));

        cursorPtr->info.cursor = (Tk_Cursor) cursorPtr;
        cursorPtr->info.display = (tkwin != NULL) ? Tk_Display(tkwin) : NULL;
        cursorPtr->info.resourceRefCount = 1;
        cursorPtr->info.objRefCount = 0;
        cursorPtr->info.otherTable = NULL;
        cursorPtr->info.hashPtr = NULL;
        cursorPtr->info.idHashPtr = NULL;
        cursorPtr->info.nextPtr = NULL;

        cursorPtr->cursor = glfwCursor;
        cursorPtr->standardShape = standardShape;

        if (standardShape != -1) {
            cursorPtr->width = 16;
            cursorPtr->height = 16;
        } else if (builtinPtr != NULL && builtinPtr->name != NULL) {
            cursorPtr->width = builtinPtr->width;
            cursorPtr->height = builtinPtr->height;
        } else if (strcmp(argv[0], "none") == 0) {
            cursorPtr->width = 1;
            cursorPtr->height = 1;
        } else {
            cursorPtr->width = fileWidth;
            cursorPtr->height = fileHeight;
        }

        {
            TkWindow *w = (TkWindow *) tkwin;
            cursorPtr->glfwWindow = NULL;
            while (w != NULL) {
                GLFWwindow *gw = TkWaylandGetGLFWwindow(w);
                if (gw != NULL) {
                    cursorPtr->glfwWindow = gw;
                    break;
                }
                w = w->parentPtr;
            }
        }
    }

cleanup:
    if (argv != NULL) {
        Tcl_Free(argv);
    }

    return (TkCursor *) cursorPtr;

  badString:
    if (argv) {
        Tcl_Free(argv);
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad cursor spec \"%s\"", string));
    Tcl_SetErrorCode(interp, "TK", "VALUE", "CURSOR", (char *)NULL);
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkCreateCursorFromData --
 *
 *      Creates a cursor from the source and mask bits.
 *
 * Results:
 *      Returns a new cursor, or NULL on errors.
 *
 * Side effects:
 *      Allocates a new cursor.
 *
 *----------------------------------------------------------------------
 */

TkCursor *
TkCreateCursorFromData(
    TCL_UNUSED(Tk_Window),
    const char *source,
    const char *mask,
    int width, int height,
    int xHot, int yHot,
    XColor fgColor,
    XColor bgColor)
{
    unsigned int fgARGB = 0xFF000000 |
                         ((fgColor.red >> 8) << 16) |
                         ((fgColor.green >> 8) << 8) |
                         (fgColor.blue >> 8);

    unsigned int bgARGB = 0xFF000000 |
                         ((bgColor.red >> 8) << 16) |
                         ((bgColor.green >> 8) << 8) |
                         (bgColor.blue >> 8);

    GLFWcursor* glfwCursor = CreateCursorFromBitmapData(
        (const unsigned char*)source,
        (const unsigned char*)mask,
        width, height,
        xHot, yHot,
        fgARGB,
        bgARGB);

    if (glfwCursor != NULL) {
        TkWaylandCursor *cursorPtr = (TkWaylandCursor *)Tcl_Alloc(sizeof(TkWaylandCursor));
        memset(cursorPtr, 0, sizeof(TkWaylandCursor));

        cursorPtr->info.cursor = (Tk_Cursor) cursorPtr;
        cursorPtr->info.display = NULL;
        cursorPtr->info.resourceRefCount = 1;
        cursorPtr->info.objRefCount = 0;
        cursorPtr->info.otherTable = NULL;
        cursorPtr->info.hashPtr = NULL;
        cursorPtr->info.idHashPtr = NULL;
        cursorPtr->info.nextPtr = NULL;

        cursorPtr->cursor = glfwCursor;
        cursorPtr->glfwWindow = NULL;
        cursorPtr->standardShape = -1;
        cursorPtr->width = width;
        cursorPtr->height = height;
        return (TkCursor *) cursorPtr;
    }

    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpFreeCursor --
 *
 *      This function is called to release a cursor allocated by
 *      TkGetCursorByName.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The cursor data structure is deallocated.
 *
 *----------------------------------------------------------------------
 */

void
TkpFreeCursor(
    TkCursor *cursorPtr)
{
    TkWaylandCursor *waylandCursorPtr = (TkWaylandCursor *) cursorPtr;

    if (waylandCursorPtr->cursor != NULL) {
        glfwDestroyCursor(waylandCursorPtr->cursor);
        waylandCursorPtr->cursor = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetCursor --
 *
 *	Set the current cursor on the active GLFW window and remember
 *	which cursor was last installed on that window so redundant
 *	glfwSetCursor calls are avoided. The active window is
 *	pointerWindow if set, otherwise the window captured by
 *	TkGetCursorByName at allocation time. Passing None clears the
 *	cursor on every window that has had one applied.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initialises the appliedCursors hash table on first use, updates
 *	it, and calls glfwSetCursor on the target window.
 *
 *----------------------------------------------------------------------
 */

void
TkpSetCursor(
    Cursor cursor)
{
    TkWaylandCursor *waylandCursorPtr;
    GLFWwindow *window;
    Tcl_HashEntry *entry;
    Tcl_HashSearch search;
    int isNew;

    if (!appliedCursorsInit) {
        Tcl_InitHashTable(&appliedCursors, TCL_ONE_WORD_KEYS);
        appliedCursorsInit = 1;
    }

    if (cursor == None) {
        for (entry = Tcl_FirstHashEntry(&appliedCursors, &search);
                entry != NULL; entry = Tcl_NextHashEntry(&search)) {
            GLFWwindow *w = (GLFWwindow *)Tcl_GetHashKey(&appliedCursors,
                    entry);

            glfwSetCursor(w, NULL);
        }
        Tcl_DeleteHashTable(&appliedCursors);
        Tcl_InitHashTable(&appliedCursors, TCL_ONE_WORD_KEYS);
        return;
    }

    waylandCursorPtr = (TkWaylandCursor *)(uintptr_t)cursor;
    if (waylandCursorPtr->cursor == NULL) {
        return;
    }

    window = pointerWindow ? pointerWindow : waylandCursorPtr->glfwWindow;
    if (window == NULL) {
        return;
    }

    entry = Tcl_CreateHashEntry(&appliedCursors, (char *)window, &isNew);
    if (!isNew &&
            (TkWaylandCursor *)Tcl_GetHashValue(entry) == waylandCursorPtr) {
        return;
    }
    Tcl_SetHashValue(entry, waylandCursorPtr);
    glfwSetCursor(window, waylandCursorPtr->cursor);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCursorPointerWindow --
 *
 *	Record which GLFW window is currently under the pointer, so that
 *	TkpSetCursor installs subsequent cursors on that window rather
 *	than the one captured when the cursor was created.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the module-static pointerWindow variable.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandCursorPointerWindow(
    GLFWwindow *window)
{
    pointerWindow = window;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCursorForgetWindow --
 *
 *	Forget any cursor bookkeeping associated with a GLFW window that
 *	is being destroyed: drop it from the appliedCursors table and
 *	clear pointerWindow if it pointed at this window. Called by the
 *	window teardown path so stale window pointers don't linger in the
 *	cursor module.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes an entry from appliedCursors and may clear pointerWindow.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandCursorForgetWindow(
    GLFWwindow *window)
{
    if (appliedCursorsInit) {
        Tcl_HashEntry *entry = Tcl_FindHashEntry(&appliedCursors,
                (char *)window);

        if (entry != NULL) {
            Tcl_DeleteHashEntry(entry);
        }
    }
    if (pointerWindow == window) {
        pointerWindow = NULL;
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
