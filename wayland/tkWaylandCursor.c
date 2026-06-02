/*
 * tkWaylandCursor.c --
 *
 *	This file contains platform-specific cursor manipulation routines
 *	for Wayland/GLFW/nanovg.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 * Copyright © 2026 Marc Culler
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stb_image.h"
#include "x11_cursors.h"

/*
 * The following data structure is a superset of the TkCursor structure
 * defined in tkCursor.c. Each system specific cursor module will define a
 * different cursor structure. All of these structures must have the same
 * header consisting of the fields in TkCursor.
 */

typedef struct {
    TkCursor info;		/* Generic cursor info used by tkCursor.c */
    GLFWcursor *cursor;		/* GLFW cursor handle */
    GLFWwindow *glfwWindow;	/* GLFW window this cursor belongs to, captured
				 * at allocation time by TkGetCursorByName. */
    int standardShape;		/* GLFW standard cursor shape, or -1 for custom */
    int width, height;		/* Dimensions for custom cursors */
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
    int shape;			/* GLFW cursor shape enum, or -1 for bitmap */
} cursorNames[] = {
    {"X_cursor",		GLFW_RESIZE_ALL_CURSOR},
    {"arrow",			GLFW_ARROW_CURSOR},
    {"based_arrow_down",	-1}, /* Will use bitmap */
    {"based_arrow_up",		-1}, /* Will use bitmap */
    {"boat",			-1}, /* Will use bitmap */
    {"bogosity",		-1}, /* Will use bitmap */
    {"bottom_left_corner",	GLFW_RESIZE_NESW_CURSOR},
    {"bottom_right_corner",	GLFW_RESIZE_NWSE_CURSOR},
    {"bottom_side",		GLFW_RESIZE_NS_CURSOR},
    {"bottom_tee",		-1}, /* Will use bitmap */
    {"box_spiral",		-1}, /* Will use bitmap */
    {"center_ptr",		GLFW_ARROW_CURSOR},
    {"circle",			-1}, /* Will use bitmap */
    {"clock",			-1}, /* Will use bitmap */
    {"coffee_mug",		-1}, /* Will use bitmap */
    {"cross",			GLFW_CROSSHAIR_CURSOR},
    {"cross_reverse",		-1}, /* Will use bitmap */
    {"crosshair",		GLFW_CROSSHAIR_CURSOR},
    {"diamond_cross",		-1}, /* Will use bitmap */
    {"dot",			-1}, /* Will use bitmap */
    {"dotbox",			-1}, /* Will use bitmap */
    {"double_arrow",		GLFW_RESIZE_EW_CURSOR},
    {"draft_large",		-1}, /* Will use bitmap */
    {"draft_small",		-1}, /* Will use bitmap */
    {"draped_box",		-1}, /* Will use bitmap */
    {"exchange",		-1}, /* Will use bitmap */
    {"fleur",			GLFW_RESIZE_ALL_CURSOR},
    {"gobbler",			-1}, /* Will use bitmap */
    {"gumby",			-1}, /* Will use bitmap */
    {"hand1",			GLFW_HAND_CURSOR},
    {"hand2",			GLFW_HAND_CURSOR},
    {"heart",			-1}, /* Will use bitmap */
    {"icon",			GLFW_ARROW_CURSOR},
    {"iron_cross",		-1}, /* Will use bitmap */
    {"left_ptr",		GLFW_ARROW_CURSOR},
    {"left_side",		GLFW_RESIZE_EW_CURSOR},
    {"left_tee",		-1}, /* Will use bitmap */
    {"leftbutton",		-1}, /* Will use bitmap */
    {"ll_angle",		GLFW_RESIZE_NESW_CURSOR},
    {"lr_angle",		GLFW_RESIZE_NWSE_CURSOR},
    {"man",			-1}, /* Will use bitmap */
    {"middlebutton",		-1}, /* Will use bitmap */
    {"mouse",			-1}, /* Will use bitmap */
    {"pencil",			-1}, /* Will use bitmap */
    {"pirate",			-1}, /* Will use bitmap */
    {"plus",			GLFW_CROSSHAIR_CURSOR},
    {"question_arrow",		GLFW_ARROW_CURSOR},
    {"right_ptr",		GLFW_ARROW_CURSOR},
    {"right_side",		GLFW_RESIZE_EW_CURSOR},
    {"right_tee",		-1}, /* Will use bitmap */
    {"rightbutton",		-1}, /* Will use bitmap */
    {"rtl_logo",		-1}, /* Will use bitmap */
    {"sailboat",		-1}, /* Will use bitmap */
    {"sb_down_arrow",		-1}, /* Will use bitmap */
    {"sb_h_double_arrow",	GLFW_RESIZE_EW_CURSOR},
    {"sb_left_arrow",		GLFW_ARROW_CURSOR},
    {"sb_right_arrow",		GLFW_ARROW_CURSOR},
    {"sb_up_arrow",		GLFW_ARROW_CURSOR},
    {"sb_v_double_arrow",	GLFW_RESIZE_NS_CURSOR},
    {"shuttle",			-1}, /* Will use bitmap */
    {"sizing",			GLFW_RESIZE_ALL_CURSOR},
    {"spider",			-1}, /* Will use bitmap */
    {"spraycan",		-1}, /* Will use bitmap */
    {"star",			-1}, /* Will use bitmap */
    {"target",			GLFW_CROSSHAIR_CURSOR},
    {"tcross",			GLFW_CROSSHAIR_CURSOR},
    {"top_left_arrow",		GLFW_ARROW_CURSOR},
    {"top_left_corner",		GLFW_RESIZE_NWSE_CURSOR},
    {"top_right_corner",	GLFW_RESIZE_NESW_CURSOR},
    {"top_side",		GLFW_RESIZE_NS_CURSOR},
    {"top_tee",			-1}, /* Will use bitmap */
    {"trek",			-1}, /* Will use bitmap */
    {"ul_angle",		GLFW_RESIZE_NWSE_CURSOR},
    {"umbrella",		-1}, /* Will use bitmap */
    {"ur_angle",		GLFW_RESIZE_NESW_CURSOR},
    {"watch",			GLFW_ARROW_CURSOR},
    {"xterm",			GLFW_IBEAM_CURSOR},
    {NULL,			GLFW_ARROW_CURSOR}
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
    /* mask BBX 15 16  -8  -8 → xHot= 8, yHot= 8 */
    {"middlebutton",       middlebutton_bits,       middlebutton_mask_bits,       15, 16,  8,  8},
    /* mask BBX 16 16  -4 -15 → xHot= 4, yHot= 1 */
    {"mouse",              mouse_bits,              mouse_mask_bits,              16, 16,  4,  1},
    /* mask BBX 13 16 -11  -1 → xHot=11, yHot=15 */
    {"pencil",             pencil_bits,             pencil_mask_bits,             13, 16, 11, 15},
    /* mask BBX 16 16  -7  -4 → xHot= 7, yHot=12 */
    {"pirate",             pirate_bits,             pirate_mask_bits,             16, 16,  7, 12},
    /* mask BBX 12 12  -5  -6 → xHot= 5, yHot= 6 */
    {"plus",               plus_bits,               plus_mask_bits,               12, 12,  5,  6},
    /* mask BBX 11 16  -5  -8 → xHot= 5, yHot= 8 */
    {"question_arrow",     question_arrow_bits,     question_arrow_mask_bits,     11, 16,  5,  8},
    /* mask BBX 10 16  -8 -15 → xHot= 8, yHot= 1 */
    {"right_ptr",          right_ptr_bits,          right_ptr_mask_bits,          10, 16,  8,  1},
    /* mask BBX 16 15 -14  -8 → xHot=14, yHot= 7 */
    {"right_side",         right_side_bits,         right_side_mask_bits,         16, 15, 14,  7},
    /* mask BBX 12 16 -10  -8 → xHot=10, yHot= 8 */
    {"right_tee",          right_tee_bits,          right_tee_mask_bits,          12, 16, 10,  8},
    /* mask BBX 15 16  -8  -8 → xHot= 8, yHot= 8 */
    {"rightbutton",        rightbutton_bits,        rightbutton_mask_bits,        15, 16,  8,  8},
    /* mask BBX 16 16  -7  -9 → xHot= 7, yHot= 7 */
    {"rtl_logo",           rtl_logo_bits,           rtl_logo_mask_bits,           16, 16,  7,  7},
    /* mask BBX 16 16  -8 -16 → xHot= 8, yHot= 0 */
    {"sailboat",           sailboat_bits,           sailboat_mask_bits,           16, 16,  8,  0},
    /* mask BBX  9 16  -4  -1 → xHot= 4, yHot=15 */
    {"sb_down_arrow",      sb_down_arrow_bits,      sb_down_arrow_mask_bits,       9, 16,  4, 15},
    /* mask BBX 15  9  -7  -5 → xHot= 7, yHot= 4 */
    {"sb_h_double_arrow",  sb_h_double_arrow_bits,  sb_h_double_arrow_mask_bits,  15,  9,  7,  4},
    /* mask BBX 16  9   0  -5 → xHot= 0, yHot= 4 */
    {"sb_left_arrow",      sb_left_arrow_bits,      sb_left_arrow_mask_bits,      16,  9,  0,  4},
    /* mask BBX 16  9 -15  -5 → xHot=15, yHot= 4 */
    {"sb_right_arrow",     sb_right_arrow_bits,     sb_right_arrow_mask_bits,     16,  9, 15,  4},
    /* mask BBX  9 16  -4 -16 → xHot= 4, yHot= 0 */
    {"sb_up_arrow",        sb_up_arrow_bits,        sb_up_arrow_mask_bits,         9, 16,  4,  0},
    /* mask BBX  9 15  -4  -8 → xHot= 4, yHot= 7 */
    {"sb_v_double_arrow",  sb_v_double_arrow_bits,  sb_v_double_arrow_mask_bits,   9, 15,  4,  7},
    /* mask BBX 16 16 -11 -16 → xHot=11, yHot= 0 */
    {"shuttle",            shuttle_bits,            shuttle_mask_bits,            16, 16, 11,  0},
    /* mask BBX 16 16  -8  -8 → xHot= 8, yHot= 8 */
    {"sizing",             sizing_bits,             sizing_mask_bits,             16, 16,  8,  8},
    /* mask BBX 16 16  -6  -9 → xHot= 6, yHot= 7 */
    {"spider",             spider_bits,             spider_mask_bits,             16, 16,  6,  7},
    /* mask BBX 12 16 -10 -14 → xHot=10, yHot= 2 */
    {"spraycan",           spraycan_bits,           spraycan_mask_bits,           12, 16, 10,  2},
    /* mask BBX 16 16  -7  -9 → xHot= 7, yHot= 7 */
    {"star",               star_bits,               star_mask_bits,               16, 16,  7,  7},
    /* mask BBX 16 14  -7  -7 → xHot= 7, yHot= 7 */
    {"target",             target_bits,             target_mask_bits,             16, 14,  7,  7},
    /* mask BBX 15 15  -7  -8 → xHot= 7, yHot= 7 */
    {"tcross",             tcross_bits,             tcross_mask_bits,             15, 15,  7,  7},
    /* mask BBX 16 16  -1 -15 → xHot= 1, yHot= 1 */
    {"top_left_arrow",     top_left_arrow_bits,     top_left_arrow_mask_bits,     16, 16,  1,  1},
    /* mask BBX 16 16  -1 -15 → xHot= 1, yHot= 1 */
    {"top_left_corner",    top_left_corner_bits,    top_left_corner_mask_bits,    16, 16,  1,  1},
    /* mask BBX 16 16 -14 -15 → xHot=14, yHot= 1 */
    {"top_right_corner",   top_right_corner_bits,   top_right_corner_mask_bits,   16, 16, 14,  1},
    /* mask BBX 15 16  -7 -15 → xHot= 7, yHot= 1 */
    {"top_side",           top_side_bits,           top_side_mask_bits,           15, 16,  7,  1},
    /* mask BBX 16 12  -8 -11 → xHot= 8, yHot= 1 */
    {"top_tee",            top_tee_bits,            top_tee_mask_bits,            16, 12,  8,  1},
    /* mask BBX  9 16  -4 -16 → xHot= 4, yHot= 0 */
    {"trek",               trek_bits,               trek_mask_bits,                9, 16,  4,  0},
    /* mask BBX 12 12  -1 -11 → xHot= 1, yHot= 1 */
    {"ul_angle",           ul_angle_bits,           ul_angle_mask_bits,           12, 12,  1,  1},
    /* mask BBX 16 16  -8 -14 → xHot= 8, yHot= 2 */
    {"umbrella",           umbrella_bits,           umbrella_mask_bits,           16, 16,  8,  2},
    /* mask BBX 12 12 -10 -11 → xHot=10, yHot= 1 */
    {"ur_angle",           ur_angle_bits,           ur_angle_mask_bits,           12, 12, 10,  1},
    /* mask BBX 16 16 -15  -7 → xHot=15, yHot= 9 */
    {"watch",              watch_bits,              watch_mask_bits,              16, 16, 15,  9},
    /* mask BBX  9 16  -4  -8 → xHot= 4, yHot= 8 */
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
    {"none",	CURSOR_NONE_DATA,	NULL},
    {NULL,	NULL,			NULL}
};

/* Forward declarations. */
static GLFWcursor* CreateCursorFromBitmapData(const unsigned char* source,
    const unsigned char* mask, int width, int height, int xHot, int yHot,
    unsigned int fgColor, unsigned int bgColor);
static int LoadImageFile(const char* filename, unsigned char** pixels,
    int* width, int* height);
static int LoadXBMFile(const char* filename, unsigned char** pixels,
    int* width, int* height);
static unsigned char* ParseXBMData(const char* data, int* width, int* height,
    int* xHot, int* yHot);
static unsigned int ParseColor(const char* colorName);
static GLFWcursor* CreateCursorFromImageData(const unsigned char* rgba,
      int width, int height, int xHot, int yHot);
void TkpSetCursor(Cursor cursor);

/*
 *----------------------------------------------------------------------
 *
 * ConvertXBMToRGBA --
 *
 *	Convert X11 bitmap data to RGBA format for GLFW.
 *
 * Results:
 *	Returns allocated RGBA pixel data, or NULL on error.
 *
 * Side effects:
 *	Allocates memory.
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
                /* Transparent. */
                rgba[rgbaIndex] = 0;     /* R */
                rgba[rgbaIndex+1] = 0;   /* G */
                rgba[rgbaIndex+2] = 0;   /* B */
                rgba[rgbaIndex+3] = 0;   /* A */
            } else if (srcBit == 1) {
                /* Foreground color. */
                rgba[rgbaIndex] = (fgColor >> 16) & 0xFF;   /* R */
                rgba[rgbaIndex+1] = (fgColor >> 8) & 0xFF;  /* G */
                rgba[rgbaIndex+2] = fgColor & 0xFF;         /* B */
                rgba[rgbaIndex+3] = (fgColor >> 24) & 0xFF; /* A */
            } else {
                /* Background color. */
                rgba[rgbaIndex] = (bgColor >> 16) & 0xFF;   /* R */
                rgba[rgbaIndex+1] = (bgColor >> 8) & 0xFF;  /* G */
                rgba[rgbaIndex+2] = bgColor & 0xFF;         /* B */
                rgba[rgbaIndex+3] = (bgColor >> 24) & 0xFF; /* A */
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
 *	Creates a cursor from X11 bitmap data.
 *
 * Results:
 *	Returns a new GLFW cursor, or NULL on error.
 *
 * Side effects:
 *	Allocates resources.
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
 * LoadImageFile --
 *
 *	Load an image file (PNG or XBM) for cursor creation.
 *
 * Results:
 *	Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *	Allocates pixel data.
 *
 *----------------------------------------------------------------------
 */

static int
LoadImageFile(
    const char* filename,
    unsigned char** pixels,
    int* width,
    int* height)
{
    const char* ext = strrchr(filename, '.');
    if (ext) {
        if (strcasecmp(ext, ".png") == 0) {
            /* Use stb_image for PNG files. */
            int channels;
            *pixels = stbi_load(filename, width, height, &channels, 4);
            if (*pixels) {
                /* stbi_load returns RGBA, which is what we need. */
                return 1;
            }
            return 0;
        } else if (strcasecmp(ext, ".xbm") == 0) {
            /* Use LoadXBMFile for .xbm files. */
            return LoadXBMFile(filename, pixels, width, height);
        }
    }

    /* Try as XBM file by default */
    return LoadXBMFile(filename, pixels, width, height);
}

/*
 *----------------------------------------------------------------------
 *
 * ParseXBMData --
 *
 *	Parse XBM format data to extract bitmap.
 *
 * Results:
 *	Returns allocated bitmap data, or NULL on error.
 *
 * Side effects:
 *	Allocates memory.
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
    /* Simple XBM parser - looks for width, height, and data. */
    const char* ptr = data;
    char name[256];

    *width = *height = 16; /* Default */
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

    /* Look for hotspot. */
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

    /* Find bitmap data. */
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
 *	Load an XBM file specifically.
 *
 * Results:
 *	Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *	Allocates pixel data.
 *
 *----------------------------------------------------------------------
 */

static int
LoadXBMFile(
    const char* filename,
    unsigned char** pixels,
    int* width,
    int* height)
{
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        return 0;
    }

    /* Read entire file. */
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }

    long fileSizeLong = ftell(fp);
    if (fileSizeLong < 0) {
        fclose(fp);
        return 0;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    /* Prevent overflow. */
    if ((unsigned long)fileSizeLong > SIZE_MAX - 1) {
        fclose(fp);
        return 0;
    }

    size_t fileSize = (size_t)fileSizeLong;

    char *fileData = Tcl_Alloc(fileSize + 1);
    if (!fileData) {
        fclose(fp);
        return 0;
    }

    size_t bytesRead = fread(fileData, 1, fileSize, fp);
    fclose(fp);

    if (bytesRead != fileSize) {
        Tcl_Free(fileData);
        return 0;
    }

    fileData[fileSize] = '\0';

    /* Parse XBM data. */
    int xHot, yHot;
    unsigned char* xbmBits = ParseXBMData(fileData, width, height, &xHot, &yHot);
    Tcl_Free(fileData);

    if (!xbmBits) {
        return 0;
    }

    /* Convert XBM to RGBA (black on white by default for XBM). */
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
 *	Parse a color name or hex value to ARGB format.
 *
 * Results:
 *	Returns color in 0xAARRGGBB format.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static unsigned int
ParseColor(
    const char* colorName)
{
    /* Simple color parser - supports named colors and hex. */
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

    /* Try hex format. */
    if (colorName[0] == '#') {
        unsigned int rgb = 0;
        if (sscanf(colorName + 1, "%x", &rgb) == 1) {
            if (strlen(colorName) <= 7) { /* #RRGGBB */
                return 0xFF000000 | rgb;
            } else { /* #AARRGGBB */
                return ((rgb & 0xFF) << 24) | ((rgb >> 8) & 0x00FFFFFF);
            }
        }
    }

    /* Default to black. */
    return 0xFF000000;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateCursorFromImageData --
 *
 *	Creates a cursor from RGBA image data.
 *
 * Results:
 *	Returns a new GLFW cursor, or NULL on error.
 *
 * Side effects:
 *	Allocates resources.
 *
 *----------------------------------------------------------------------
 */

static GLFWcursor*
CreateCursorFromImageData(
    const unsigned char* rgba,      /* RGBA pixel data */
    int width, int height,          /* Dimensions */
    int xHot, int yHot)             /* Hot spot */
{
    if (!rgba || width <= 0 || height <= 0) {
        return NULL;
    }

    GLFWimage image;
    image.width = width;
    image.height = height;
    image.pixels = (unsigned char*)rgba; /* GLFW will copy the data */

    return glfwCreateCursor(&image, xHot, yHot);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetCursorByName --
 *
 *	Retrieve a cursor by name. Parse the cursor name into fields and
 *	create a cursor.
 *
 * Results:
 *	Returns a new cursor, or NULL on errors.
 *
 * Side effects:
 *	Allocates a new cursor.
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
    unsigned int fgColor = 0xFF000000; /* Black */
    unsigned int bgColor = 0xFFFFFFFF; /* White */
    int fileWidth = 0, fileHeight = 0; /* Captured for '@' file cursors. */

    if (Tcl_SplitList(interp, string, &argc, &argv) != TCL_OK) {
        return NULL;
    }
    if (argc == 0) {
        goto badString;
    }

    /*
     * Check Tk-specific table first (currently only "none").
     */
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

    /*
     * Check GLFW standard cursor table NEXT — these take priority over
     * the X11 bitmap fallbacks in builtinCursors[].
     */
    if ((argv[0][0] != '@') && (tkCursorPtr == NULL)) {
        const struct CursorName *namePtr;
        for (namePtr = cursorNames; namePtr->name != NULL; namePtr++) {
            if ((namePtr->name[0] == argv[0][0]) &&
                    (strcmp(namePtr->name, argv[0]) == 0)) {
                standardShape = namePtr->shape;
                break;
            }
        }
        /* standardShape stays -1 if not found in cursorNames[] */
    }

    /*
     * Only fall through to builtinCursors[] when:
     *  - not a '@' file spec
     *  - not a tkCursorNames match
     *  - not a GLFW standard shape match (standardShape == -1) OR
     *    the matched GLFW shape is -1 (meaning the cursorNames entry
     *    explicitly has no GLFW equivalent and wants the bitmap).
     */
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

    /* Parse optional color arguments. */
    if (argc >= 2) {
        fgColor = ParseColor(argv[1]);
    }
    if (argc >= 3) {
        bgColor = ParseColor(argv[2]);
    }

    /* Dispatch to the appropriate cursor-creation path. */

    if (argv[0][0] == '@') {
        /* File-based cursor. */
        if (Tcl_IsSafe(interp)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(
                    "cannot get cursor from a file in a safe interpreter",
                    TCL_INDEX_NONE));
            Tcl_SetErrorCode(interp, "TK", "SAFE", "CURSOR_FILE", (char *)NULL);
            goto cleanup;
        }
        if ((argc != 2) && (argc != 4)) {
            goto badString;
        }
        unsigned char* pixels = NULL;
        if (!LoadImageFile(argv[0] + 1, &pixels, &fileWidth, &fileHeight)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                    "could not load cursor file \"%s\"", argv[0] + 1));
            Tcl_SetErrorCode(interp, "TK", "CURSOR", "FILE", (char *)NULL);
            goto cleanup;
        }
        glfwCursor = CreateCursorFromImageData(pixels, fileWidth, fileHeight,
                                               fileWidth/2, fileHeight/2);
        Tcl_Free(pixels);
        standardShape = -1; /* bitmap path */

    } else if (tkCursorPtr != NULL) {
        /* Tk built-in (currently only "none"). */
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
        standardShape = -1; /* bitmap path */

    } else if (standardShape != -1) {
        /*
         * GLFW standard cursor. standardShape was set from cursorNames[].
         * These use the compositor's native cursor; do NOT hide it.
         */
        glfwCursor = glfwCreateStandardCursor(standardShape);

    } else if (builtinPtr != NULL) {
        /*
         * X11 bitmap fallback for cursors that have no GLFW equivalent
         * (shape == -1 in cursorNames[]). standardShape stays -1 so
         * TkpSetCursor knows to hide the compositor cursor.
         */
        glfwCursor = CreateCursorFromBitmapData(builtinPtr->bits,
                                                builtinPtr->mask,
                                                builtinPtr->width,
                                                builtinPtr->height,
                                                builtinPtr->xHot,
                                                builtinPtr->yHot,
                                                fgColor,
                                                bgColor);
        /* standardShape == -1: TkpSetCursor hides compositor cursor. */

    } else {
        /* Unknown cursor name — fall back to arrow and log it. */
        fprintf(stderr, "tkWaylandCursor: unknown cursor \"%s\", using arrow\n", string);
        glfwCursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        standardShape = GLFW_ARROW_CURSOR;
    }

    if (glfwCursor != NULL) {
        cursorPtr = (TkWaylandCursor *)Tcl_Alloc(sizeof(TkWaylandCursor));
        memset(cursorPtr, 0, sizeof(TkWaylandCursor));

        /*
         * Use the struct pointer itself as the Tk_Cursor XID. 
         * TkpSetCursor casts it straight back — no hash
         * lookup needed.
         */
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
            /*
             * GLFW standard shape: the compositor owns the actual cursor
             * image so there are no local bitmap dimensions to record.
             * Use a nominal nonzero size so that no downstream caller
             * misinterprets width==0 as an invalid or empty cursor.
             */
            cursorPtr->width = 16;
            cursorPtr->height = 16;
        } else if (builtinPtr != NULL && builtinPtr->name != NULL) {
            /* X11 bitmap fallback: record the true bitmap dimensions. */
            cursorPtr->width = builtinPtr->width;
            cursorPtr->height = builtinPtr->height;
        } else if (strcmp(argv[0], "none") == 0) {
            /* Invisible 1×1 cursor. */
            cursorPtr->width = 1;
            cursorPtr->height = 1;
        } else {
            cursorPtr->width = fileWidth;
            cursorPtr->height = fileHeight;
        }

        /*
         * Capture the GLFW window at allocation time by walking up to the
         * toplevel. TkpSetCursor receives only a bare Cursor XID with no
         * window argument, so this is the only opportunity to record which
         * GLFW window the cursor belongs to.
         */
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

    /* 
     * We have the cursor data. Force the updating and 
     * display of the cursor.
     */
    if (cursorPtr != NULL && tkwin != NULL) {
        TkpSetCursor( (Cursor) cursorPtr->info.cursor );
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
 *	Creates a cursor from the source and mask bits.
 *
 * Results:
 *	Returns a new cursor, or NULL on errors.
 *
 * Side effects:
 *	Allocates a new cursor.
 *
 *----------------------------------------------------------------------
 */

TkCursor *
TkCreateCursorFromData(
    TCL_UNUSED(Tk_Window),	/* Window in which cursor will be used. */
    const char *source,		/* Bitmap data for cursor shape. */
    const char *mask,		/* Bitmap data for cursor mask. */
    int width, int height,	/* Dimensions of cursor. */
    int xHot, int yHot,		/* Location of hot-spot in cursor. */
    XColor fgColor,		/* Foreground color for cursor. */
    XColor bgColor)		/* Background color for cursor. */
{
    /* Convert XColor to ARGB format. */
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

        /* Initialize core header fields. */
        cursorPtr->info.cursor = (Tk_Cursor) cursorPtr;
        cursorPtr->info.display = NULL; /* Will be overwritten/assigned by core TkcGetCursor. */
        cursorPtr->info.resourceRefCount = 1;
        cursorPtr->info.objRefCount = 0;
        cursorPtr->info.otherTable = NULL;
        cursorPtr->info.hashPtr = NULL;
        cursorPtr->info.idHashPtr = NULL;
        cursorPtr->info.nextPtr = NULL;

        /* Initialize custom platform properties. */
        cursorPtr->cursor = glfwCursor;
        cursorPtr->glfwWindow = NULL;	/* No tkwin available; set by caller if needed. */
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
 *	This function is called to release a cursor allocated by
 *	TkGetCursorByName.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The cursor data structure is deallocated.
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
    /*
     * Do NOT free cursorPtr here. The generic FreeCursor in tkCursor.c
     * owns the struct lifetime and calls Tcl_Free(cursorPtr) itself after
     * TkpFreeCursor returns, when objRefCount reaches zero. Freeing here
     * causes a double-free and heap corruption.
     */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetCursor --
 *
 *	Set the current cursor and install it. Called by UpdateCursor in
 *	tkPointer.c with the Tk_Cursor XID extracted from winPtr->atts.cursor.
 *
 *	Since info.cursor is set to the TkWaylandCursor pointer itself
 *	(matching the macOS pattern), we cast directly back with no hash
 *	table lookup. The GLFW window was captured at cursor-creation time
 *	in TkGetCursorByName, so no window argument or generic-layer access
 *	is needed here.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the cursor displayed in the GLFW window.
 *
 *----------------------------------------------------------------------
 */

void
TkpSetCursor(
    Cursor cursor)		/* Tk_Cursor or None */
{
    static Tcl_HashTable gCursorCache;
    static int          gCursorCacheInitialized = 0;

    TkWaylandCursor *waylandCursorPtr;
    GLFWwindow *window;
    Tcl_HashEntry *entry;
    int isNew;

    /* Initialize cursor cache on first use. */
    if (!gCursorCacheInitialized) {
        Tcl_InitHashTable(&gCursorCache, TCL_ONE_WORD_KEYS);
        gCursorCacheInitialized = 1;
    }

    if (cursor == None) {
        Tcl_DeleteHashTable(&gCursorCache);
        Tcl_InitHashTable(&gCursorCache, TCL_ONE_WORD_KEYS);
        return;
    }

    waylandCursorPtr = (TkWaylandCursor *)(uintptr_t)cursor;

    window = waylandCursorPtr->glfwWindow;
    if (window == NULL) {
        return;
    }

    /* Check cache for this specific window. */
    entry = Tcl_FindHashEntry(&gCursorCache, (char *)window);
    if (entry != NULL) {
        TkWaylandCursor *cached = (TkWaylandCursor *)Tcl_GetHashValue(entry);
        if (cached == waylandCursorPtr) {
            return;                    /* No change — best case. */
        }
    }

    /* If necessary, update cache. */
    if (entry == NULL) {
        entry = Tcl_CreateHashEntry(&gCursorCache, (char *)window, &isNew);
    }
    Tcl_SetHashValue(entry, waylandCursorPtr);

    /* Apply cursor. */
    if (waylandCursorPtr->cursor == NULL) {
        glfwSetCursor(window, NULL);
    } else {
        glfwSetCursor(window, waylandCursorPtr->cursor);
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
