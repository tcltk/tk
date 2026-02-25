/*
 * tkUnixCursor.c --
 *
 *	This file contains platform-specific cursor manipulation routines
 *	for Wayland/GLFW/nanovg.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stb_image.h"

/*
 * The following data structure is a superset of the TkCursor structure
 * defined in tkCursor.c. Each system specific cursor module will define a
 * different cursor structure. All of these structures must have the same
 * header consisting of the fields in TkCursor.
 */

typedef struct {
    TkCursor info;		/* Generic cursor info used by tkCursor.c */
    GLFWcursor *cursor;		/* GLFW cursor handle */
    int standardShape;		/* GLFW standard cursor shape, or -1 for custom */
    int width, height;		/* Dimensions for custom cursors */
} TkUnixCursor;

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
 * Built-in bitmap data for Tk cursors that don't have GLFW equivalents.
 * These are X11 XBM format strings converted to binary data.
 */

/* X_cursor bitmap (16x16). */
static unsigned char x_cursor_bits[] = {
    0x00, 0x00, 0x18, 0x18, 0x3c, 0x3c, 0x7e, 0x7e,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e, 0x7e,
    0x3c, 0x3c, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00
};

/* based_arrow_down bitmap (16x16). */
static unsigned char based_arrow_down_bits[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* cross_reverse bitmap (16x16). */
static unsigned char cross_reverse_bits[] = {
    0x80, 0x01, 0x40, 0x02, 0x20, 0x04, 0x10, 0x08,
    0x08, 0x10, 0x04, 0x20, 0x02, 0x40, 0x01, 0x80,
    0x80, 0x01, 0x40, 0x02, 0x20, 0x04, 0x10, 0x08,
    0x08, 0x10, 0x04, 0x20, 0x02, 0x40, 0x01, 0x80
};

/* watch bitmap (16x16). */
static unsigned char watch_bits[] = {
    0x00, 0x00, 0xf0, 0x0f, 0x08, 0x10, 0x04, 0x20,
    0x04, 0x20, 0x02, 0x40, 0x02, 0x40, 0x01, 0x80,
    0x01, 0x80, 0x02, 0x40, 0x02, 0x40, 0x04, 0x20,
    0x04, 0x20, 0x08, 0x10, 0xf0, 0x0f, 0x00, 0x00
};

/* Mask for watch cursor. */
static unsigned char watch_mask_bits[] = {
    0xf0, 0x0f, 0xf8, 0x1f, 0xfc, 0x3f, 0xfe, 0x7f,
    0xfe, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x7f,
    0xfe, 0x7f, 0xfc, 0x3f, 0xf8, 0x1f, 0xf0, 0x0f
};

/* Built-in cursor database. */
static const struct BuiltinCursor {
    const char *name;
    unsigned char *bits;
    unsigned char *mask;
    int width;
    int height;
    int xHot;
    int yHot;
} builtinCursors[] = {
    {"X_cursor", x_cursor_bits, NULL, 16, 16, 8, 8},
    {"based_arrow_down", based_arrow_down_bits, NULL, 16, 16, 8, 15},
    {"based_arrow_up", based_arrow_down_bits, NULL, 16, 16, 8, 0},
    {"cross_reverse", cross_reverse_bits, NULL, 16, 16, 8, 8},
    {"watch", watch_bits, watch_mask_bits, 16, 16, 8, 8},
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
            int bitIndex = 7 - (x % 8);  /* Fixed: Changed from 7 to 8 */
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
    Tcl_Interp *interp,		/* Interpreter to use for error reporting. */
    TCL_UNUSED(Tk_Window),	/* Window in which cursor will be used. */
    const char *string)		/* Description of cursor. See manual entry for
				 * details on legal syntax. */
{
    TkUnixCursor *cursorPtr = NULL;
    Tcl_Size argc;
    const char **argv = NULL;
    const struct TkCursorName *tkCursorPtr = NULL;
    const struct BuiltinCursor *builtinPtr = NULL;
    int standardShape = -1;
    GLFWcursor* glfwCursor = NULL;
    unsigned int fgColor = 0xFF000000; /* Black */
    unsigned int bgColor = 0xFFFFFFFF; /* White */

    if (Tcl_SplitList(interp, string, &argc, &argv) != TCL_OK) {
	return NULL;
    }
    if (argc == 0) {
	goto badString;
    }

    /*
     * Check Tk specific table of cursor names for custom cursors.
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

    /* Check built-in cursor database. */
    for (builtinPtr = builtinCursors; builtinPtr->name; builtinPtr++) {
        if (strcmp(builtinPtr->name, argv[0]) == 0) {
            break;
        }
    }

    if ((argv[0][0] != '@') && (tkCursorPtr == NULL) && (!builtinPtr->name)) {
        /* Look for standard cursor shape. */
        const struct CursorName *namePtr;

        if (argc > 3) {
            goto badString;
        }

        for (namePtr = cursorNames; ; namePtr++) {
            if (namePtr->name == NULL) {
                /* Not found, use default arrow cursor. */
                standardShape = GLFW_ARROW_CURSOR;
                break;
            }
            if ((namePtr->name[0] == argv[0][0])
                    && (strcmp(namePtr->name, argv[0]) == 0)) {
                standardShape = namePtr->shape;
                break;
            }
        }

        /* Parse colors if provided. */
        if (argc >= 2) {
            fgColor = ParseColor(argv[1]);
        }
        if (argc >= 3) {
            bgColor = ParseColor(argv[2]);
        }

        if (standardShape != -1) {
            /* Create standard cursor (GLFW standard cursors ignore colors). */
            glfwCursor = glfwCreateStandardCursor(standardShape);
        } else {
            /* Should have been caught by builtin check. */
            goto badString;
        }
    } else if (builtinPtr->name) {
        /* Create from built-in bitmap data. */
        if (argc >= 2) {
            fgColor = ParseColor(argv[1]);
        }
        if (argc >= 3) {
            bgColor = ParseColor(argv[2]);
        }

        glfwCursor = CreateCursorFromBitmapData(builtinPtr->bits,
                                                builtinPtr->mask,
                                                builtinPtr->width,
                                                builtinPtr->height,
                                                builtinPtr->xHot,
                                                builtinPtr->yHot,
                                                fgColor,
                                                bgColor);
    } else if (argv[0][0] == '@') {
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

        /* Parse colors. */
        if (argc == 2) {
            fgColor = ParseColor(argv[1]);
            bgColor = fgColor;
        } else if (argc == 4) {
            fgColor = ParseColor(argv[2]);
            bgColor = ParseColor(argv[3]);
        }

        /* Load image file. */
        unsigned char* pixels = NULL;
        int width, height;

        if (!LoadImageFile(argv[0] + 1, &pixels, &width, &height)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                    "could not load cursor file \"%s\"", argv[0] + 1));
            Tcl_SetErrorCode(interp, "TK", "CURSOR", "FILE", (char *)NULL);
            goto cleanup;
        }

        /* Create cursor using CreateCursorFromImageData (hotspot defaults to center). */
        glfwCursor = CreateCursorFromImageData(pixels, width, height, width/2, height/2);
        Tcl_Free(pixels);
    } else if (tkCursorPtr != NULL) {
        /* Custom cursor from embedded XBM data. */
        if (strcmp(argv[0], "none") == 0) {
            /* Create invisible cursor */
            unsigned char transparent[4] = {0, 0, 0, 0};
            GLFWimage image;
            image.width = 1;
            image.height = 1;
            image.pixels = transparent;
            glfwCursor = glfwCreateCursor(&image, 0, 0);
        } else {
            /* Parse XBM data. */
            int width, height, xHot, yHot;
            unsigned char* bits = ParseXBMData(tkCursorPtr->data, 
                                              &width, &height, &xHot, &yHot);
            if (bits) {
                /* Parse colors if provided. */
                if (argc >= 2) {
                    fgColor = ParseColor(argv[1]);
                }
                if (argc >= 3) {
                    bgColor = ParseColor(argv[2]);
                } else {
                    bgColor = 0; /* Transparent background. */
                }

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
    }

    if (glfwCursor != NULL) {
        cursorPtr = (TkUnixCursor *)Tcl_Alloc(sizeof(TkUnixCursor));
        cursorPtr->info.cursor = (Tk_Cursor) glfwCursor;
        cursorPtr->cursor = glfwCursor;
        cursorPtr->standardShape = standardShape;
        cursorPtr->width = 0;
        cursorPtr->height = 0;

        /* Store dimensions for built-in cursors. */
        if (builtinPtr->name) {
            cursorPtr->width = builtinPtr->width;
            cursorPtr->height = builtinPtr->height;
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
        TkUnixCursor *cursorPtr = (TkUnixCursor *)Tcl_Alloc(sizeof(TkUnixCursor));
        cursorPtr->info.cursor = (Tk_Cursor) glfwCursor;
        cursorPtr->cursor = glfwCursor;
        cursorPtr->standardShape = -1; /* Custom cursor */
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
    TkUnixCursor *unixCursorPtr = (TkUnixCursor *) cursorPtr;

    if (unixCursorPtr->cursor != NULL) {
        glfwDestroyCursor(unixCursorPtr->cursor);
    }
    Tcl_Free(cursorPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetCursor --
 *
 *	Set the cursor for a window. This is called from tkCursor.c.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the window's cursor.
 *
 *----------------------------------------------------------------------
 */

void
TkpSetCursor(
    TkWindow *winPtr,		/* Window to set cursor for. */
    TkCursor *cursorPtr)	/* New cursor, or NULL for default. */
{
    TkUnixCursor *unixCursorPtr = (TkUnixCursor *) cursorPtr;
    GLFWwindow* window = (GLFWwindow*)Tk_WindowId((Tk_Window)winPtr);

    if (window != NULL) {
        if (unixCursorPtr != NULL && unixCursorPtr->cursor != NULL) {
            glfwSetCursor(window, unixCursorPtr->cursor);
        } else {
            /* Set to default arrow cursor. */
            glfwSetCursor(window, NULL);
        }
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
