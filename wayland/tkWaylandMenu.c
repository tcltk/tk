/*
 * tkWaylandMenu.c --
 *
 *	This module implements the Wayland/GLFW/nanoVG platform-specific features of menus.
 *      Ported from original tkUnixMenu.c for X11.
 *
 * Copyright © 1996-1998 Sun Microsystems, Inc.
 * Ported to Wayland/GLFW/nanoVG in 2026 by AI assistance.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include <GLFW/glfw3.h>
#include "nanovg.h"
#ifdef NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"
#endif
#include "tkMenu.h"

// Assumptions:
// - Global NVGcontext *vg is initialized elsewhere (e.g., in main app).
// - We draw menus as overlays in the main GLFW window.
// - No separate windows for tearoffs/menubars; simulate in main canvas.
// - Fonts: Use a default font; load in init.
// - Colors: Map Tk colors to NVGcolor.
// - Ignore some X11-specific like Drawables, GC, XColor; use nanoVG directly.
// - 3D borders: Simulate with gradients and shadows.

// Global nanoVG context (set externally)
extern NVGcontext *vg;

// Default font (assume "sans" is loaded)
#define DEFAULT_FONT "sans"
#define DEFAULT_FONT_SIZE 14.0f

// Helper to map Tk_3DBorder to nanoVG colors/gradients (simplified)
typedef struct {
    NVGcolor light;
    NVGcolor dark;
    NVGcolor bg;
} Simple3DBorder;

Simple3DBorder GetSimple3DBorder(Tk_3DBorder border) {
    Simple3DBorder s;
    // Dummy colors; in real impl, extract from Tk_3DBorder
    s.light = nvgRGB(200, 200, 200);
    s.dark = nvgRGB(100, 100, 100);
    s.bg = nvgRGB(150, 150, 150);
    return s;
}

// Simulate relief drawing with nanoVG (raised/flat/sunken)
void Draw3DRect(NVGcontext *vg, float x, float y, float w, float h, int borderWidth, int relief, Simple3DBorder bord) {
    nvgSave(vg);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillColor(vg, bord.bg);
    nvgFill(vg);

    if (relief != TK_RELIEF_FLAT) {
        // Simple bevel
        nvgBeginPath(vg);
        nvgMoveTo(vg, x, y + h);
        nvgLineTo(vg, x, y);
        nvgLineTo(vg, x + w, y);
        nvgStrokeWidth(vg, (float)borderWidth);
        nvgStrokeColor(vg, (relief == TK_RELIEF_RAISED) ? bord.light : bord.dark);
        nvgStroke(vg);

        nvgBeginPath(vg);
        nvgMoveTo(vg, x + w, y);
        nvgLineTo(vg, x + w, y + h);
        nvgLineTo(vg, x, y + h);
        nvgStrokeColor(vg, (relief == TK_RELIEF_RAISED) ? bord.dark : bord.light);
        nvgStroke(vg);
    }
    nvgRestore(vg);
}

// Simulate text drawing
void DrawChars(NVGcontext *vg, const char *text, int len, float x, float y, NVGcolor color) {
    nvgFillColor(vg, color);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgText(vg, x, y, text, text + len);
}

// Simulate underline
void UnderlineChars(NVGcontext *vg, const char *text, int start, int end, float x, float y, float ascent, float descent, NVGcolor color) {
    float bounds[4];
    nvgTextBounds(vg, x, y, text + start, text + end, bounds);
    nvgBeginPath(vg);
    nvgMoveTo(vg, bounds[0], y + ascent + 1); // Approximate underline position
    nvgLineTo(vg, bounds[2], y + ascent + 1);
    nvgStrokeWidth(vg, 1.0f);
    nvgStrokeColor(vg, color);
    nvgStroke(vg);
}

// Ported constants
#define MENU_MARGIN_WIDTH	2
#define MENU_DIVIDER_HEIGHT	2

#define ENTRY_HELP_MENU		ENTRY_PLATFORM_FLAG1

MODULE_SCOPE void	TkpDrawCheckIndicator(Tk_Window tkwin,
			    Display *display_unused, Drawable d_unused, int x, int y,
			    Tk_3DBorder bgBorder, XColor *indicatorColor_unused,
			    XColor *selectColor_unused, XColor *disColor_unused, int on,
			    int disabled, int mode);
// Indicator modes remain the same

/*
 * Procedures used internally. (Signatures kept, impl changed)
 */

static void		SetHelpMenu(TkMenu *menuPtr);
static void		DrawMenuEntryAccelerator(TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Drawable d_unused, GC gc_unused,
			    Tk_Font tkfont_unused, const Tk_FontMetrics *fmPtr_unused,
			    Tk_3DBorder activeBorder, Tk_3DBorder bgBorder,
			    int x, int y, int width, int height, int drawArrow);
static void		DrawMenuEntryBackground(TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Drawable d_unused,
			    Tk_3DBorder activeBorder, Tk_3DBorder bgBorder,
			    int x, int y, int width, int height);
static void		DrawMenuEntryIndicator(TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Drawable d_unused,
			    Tk_3DBorder border, XColor *indicatorColor_unused,
			    XColor *disableColor_unused, Tk_Font tkfont_unused,
			    const Tk_FontMetrics *fmPtr_unused, int x, int y,
			    int width, int height);
static void		DrawMenuEntryLabel(TkMenu * menuPtr,
			    TkMenuEntry *mePtr, Drawable d_unused, GC gc_unused,
			    Tk_Font tkfont_unused, const Tk_FontMetrics *fmPtr_unused,
			    int x, int y, int width, int height);
static void		DrawMenuSeparator(TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Drawable d_unused, GC gc_unused,
			    Tk_Font tkfont_unused, const Tk_FontMetrics *fmPtr_unused,
			    int x, int y, int width, int height);
static void		DrawTearoffEntry(TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Drawable d_unused, GC gc_unused,
			    Tk_Font tkfont_unused, const Tk_FontMetrics *fmPtr_unused,
			    int x, int y, int width, int height);
static void		DrawMenuUnderline(TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Drawable d_unused, GC gc_unused,
			    Tk_Font tkfont_unused, const Tk_FontMetrics *fmPtr_unused,
			    int x, int y, int width, int height);
static void		GetMenuAccelGeometry(TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Tk_Font tkfont_unused,
			    const Tk_FontMetrics *fmPtr_unused, int *widthPtr,
			    int *heightPtr);
static void		GetMenuLabelGeometry(TkMenuEntry *mePtr,
			    Tk_Font tkfont_unused, const Tk_FontMetrics *fmPtr_unused,
			    int *widthPtr, int *heightPtr);
static void		GetMenuIndicatorGeometry(TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Tk_Font tkfont_unused,
			    const Tk_FontMetrics *fmPtr_unused,
			    int *widthPtr, int *heightPtr);
static void		GetMenuSeparatorGeometry(TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Tk_Font tkfont_unused,
			    const Tk_FontMetrics *fmPtr_unused,
			    int *widthPtr, int *heightPtr);
static void		GetTearoffEntryGeometry(TkMenu *menuPtr,
			    TkMenuEntry *mePtr, Tk_Font tkfont_unused,
			    const Tk_FontMetrics *fmPtr_unused, int *widthPtr,
			    int *heightPtr);

/*
 *----------------------------------------------------------------------
 *
 * TkpNewMenu --
 *
 *	Gets the platform-specific piece of the menu. Invoked during idle
 *	after the generic part of the menu has been created.
 *
 * Results:
 *	Standard TCL error.
 *
 * Side effects:
 *	Allocates any platform specific allocations and places them in the
 *	platformData field of the menuPtr.
 *
 *----------------------------------------------------------------------
 */

int
TkpNewMenu(
    TkMenu *menuPtr)
{
    SetHelpMenu(menuPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDestroyMenu --
 *
 *	Destroys platform-specific menu structures. Called when the generic
 *	menu structure is destroyed for the menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All platform-specific allocations are freed up.
 *
 *----------------------------------------------------------------------
 */

void
TkpDestroyMenu(
    TCL_UNUSED(TkMenu *))
{
    /*
     * Nothing to do.
     */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDestroyMenuEntry --
 *
 *	Cleans up platform-specific menu entry items. Called when entry is
 *	destroyed in the generic code.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All platform specific allocations are freed up.
 *
 *----------------------------------------------------------------------
 */

void
TkpDestroyMenuEntry(
    TCL_UNUSED(TkMenuEntry *))
{
    /*
     * Nothing to do.
     */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpConfigureMenuEntry --
 *
 *	Processes configuration options for menu entries. Called when the
 *	generic options are processed for the menu.
 *
 * Results:
 *	Returns standard TCL result. If TCL_ERROR is returned, then the
 *	interp's result contains an error message.
 *
 * Side effects:
 *	Configuration information get set for mePtr; old resources get freed,
 *	if any need it.
 *
 *----------------------------------------------------------------------
 */

int
TkpConfigureMenuEntry(
    TkMenuEntry *mePtr)/* Information about menu entry; may or may
				 * not already have values for some fields. */
{
    /*
     * If this is a cascade menu, and the child menu exists, check to see if
     * the child menu is a help menu.
     */

    if ((mePtr->type == CASCADE_ENTRY) && (mePtr->namePtr != NULL)) {
	TkMenuReferences *menuRefPtr;

	menuRefPtr = TkFindMenuReferencesObj(mePtr->menuPtr->interp,
		mePtr->namePtr);
	if ((menuRefPtr != NULL) && (menuRefPtr->menuPtr != NULL)) {
	    SetHelpMenu(menuRefPtr->menuPtr);
	}
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMenuNewEntry --
 *
 *	Called when a new entry is created in a menu. Fills in platform
 *	specific data for the entry. The platformEntryData field is used to
 *	store the indicator diameter for radio button and check box entries.
 *
 * Results:
 *	Standard TCL error.
 *
 * Side effects:
 *	None on Wayland.
 *
 *----------------------------------------------------------------------
 */

int
TkpMenuNewEntry(
    TCL_UNUSED(TkMenuEntry *))
{
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetWindowMenuBar --
 *
 *	Sets up the menu as a menubar in the given window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Recomputes geometry of given window. (Simulate in main render)
 *
 *----------------------------------------------------------------------
 */

void
TkpSetWindowMenuBar(
    Tk_Window tkwin,		/* The window we are setting */
    TkMenu *menuPtr)		/* The menu we are setting */
{
    // In GLFW, no native menubar; app must draw it itself.
    // Set some global state for rendering.
    (void)tkwin;
    if (menuPtr == NULL) {
        // Clear menubar
    } else {
        // Set menubar menu
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetMenuIndicatorGeometry --
 *
 *	Fills out the geometry of the indicator in a menu item. Note that the
 *	mePtr->height field must have already been filled in by
 *	GetMenuLabelGeometry since this height depends on the label height.
 *
 * Results:
 *	widthPtr and heightPtr point to the new geometry values.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
GetMenuIndicatorGeometry(
    TkMenu *menuPtr,			/* The menu we are drawing. */
    TkMenuEntry *mePtr,			/* The entry we are interested in. */
    TCL_UNUSED(Tk_Font),		/* The precalculated font */
    TCL_UNUSED(const Tk_FontMetrics *),	/* The precalculated metrics */
    int *widthPtr,			/* The resulting width */
    int *heightPtr)			/* The resulting height */
{
    int borderWidth;

    if ((mePtr->type == CHECK_BUTTON_ENTRY)
	    || (mePtr->type == RADIO_BUTTON_ENTRY)) {
	if (!mePtr->hideMargin && mePtr->indicatorOn) {
	    if ((mePtr->image != NULL) || (mePtr->bitmapPtr != NULL)) {
		*widthPtr = (14 * mePtr->height) / 10;
		*heightPtr = mePtr->height;
		if (mePtr->type == CHECK_BUTTON_ENTRY) {
		    mePtr->platformEntryData = (TkMenuPlatformEntryData)
			    INT2PTR((65 * mePtr->height) / 100);
		} else {
		    mePtr->platformEntryData = (TkMenuPlatformEntryData)
			    INT2PTR((75 * mePtr->height) / 100);
		}
	    } else {
		*widthPtr = *heightPtr = mePtr->height;
		if (mePtr->type == CHECK_BUTTON_ENTRY) {
		    mePtr->platformEntryData = (TkMenuPlatformEntryData)
			    INT2PTR((80 * mePtr->height) / 100);
		} else {
		    mePtr->platformEntryData = (TkMenuPlatformEntryData)
			    INT2PTR(mePtr->height);
		}
	    }
	} else {
	    Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, menuPtr->borderWidthObj,
		    &borderWidth);
	    *heightPtr = 0;
	    *widthPtr = borderWidth;
	}
    } else {
	Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, menuPtr->borderWidthObj,
		&borderWidth);
	*heightPtr = 0;
	*widthPtr = borderWidth;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetMenuAccelGeometry --
 *
 *	Get the geometry of the accelerator area of a menu item.
 *
 * Results:
 *	heightPtr and widthPtr are set.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
GetMenuAccelGeometry(
    TkMenu *menuPtr,		/* The menu was are drawing */
    TkMenuEntry *mePtr,		/* The entry we are getting the geometry for */
    Tk_Font tkfont_unused,		/* The precalculated font */
    const Tk_FontMetrics *fmPtr_unused,/* The precalculated font metrics */
    int *widthPtr,		/* The width of the acclerator area */
    int *heightPtr)		/* The height of the accelerator area */
{
    double scalingLevel = 1.0; // TkScalingLevel(menuPtr->tkwin); Stub

    *heightPtr = DEFAULT_FONT_SIZE; // Approximate fmPtr->linespace
    if (mePtr->type == CASCADE_ENTRY) {
	*widthPtr = 2 * CASCADE_ARROW_WIDTH * scalingLevel;
    } else if ((menuPtr->menuType != MENUBAR) && (mePtr->accelPtr != NULL)) {
	const char *accel = Tcl_GetString(mePtr->accelPtr);
        float bw[4];
        nvgFontSize(vg, DEFAULT_FONT_SIZE);
        nvgFontFace(vg, DEFAULT_FONT);
	*widthPtr = (int)nvgTextBounds(vg, 0, 0, accel, accel + mePtr->accelLength, bw);
    } else {
	*widthPtr = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DrawMenuEntryBackground --
 *
 *	This procedure draws the background part of a menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Commands are output to nanoVG to display the menu in its current mode.
 *
 *----------------------------------------------------------------------
 */

static void
DrawMenuEntryBackground(
    TkMenu *menuPtr,		/* The menu we are drawing */
    TkMenuEntry *mePtr,		/* The entry we are drawing. */
    Drawable d_unused,			/* Ignored */
    Tk_3DBorder activeBorder,	/* The border for an active item */
    Tk_3DBorder bgBorder,	/* The background border */
    int x, int y, int width, int height)
{
    Simple3DBorder bord = GetSimple3DBorder(bgBorder);
    int relief = TK_RELIEF_FLAT;

    if (mePtr->state == ENTRY_ACTIVE) {
        bord = GetSimple3DBorder(activeBorder);

	if ((menuPtr->menuType == MENUBAR)
		&& ((menuPtr->postedCascade == NULL)
		|| (menuPtr->postedCascade != mePtr))) {
	    relief = TK_RELIEF_FLAT;
	} else {
	    relief = menuPtr->activeRelief;
	}
    }

    int activeBorderWidth;
    Tk_GetPixelsFromObj(NULL, menuPtr->tkwin,
		menuPtr->activeBorderWidthPtr, &activeBorderWidth);

    Draw3DRect(vg, (float)x, (float)y, (float)width, (float)height, activeBorderWidth, relief, bord);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawMenuEntryAccelerator --
 *
 *	This procedure draws the accelerator or cascade arrow.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	nanoVG commands.
 *
 *----------------------------------------------------------------------
 */

static void
DrawMenuEntryAccelerator(
    TkMenu *menuPtr,		/* The menu we are drawing */
    TkMenuEntry *mePtr,		/* The entry we are drawing */
    Drawable d_unused,			/* Ignored */
    GC gc_unused,			/* Ignored */
    Tk_Font tkfont_unused,		/* Ignored */
    const Tk_FontMetrics *fmPtr_unused,/* Ignored */
    Tk_3DBorder activeBorder,	/* The border for an active item */
    Tk_3DBorder bgBorder,	/* The background border */
    int x, int y, int width, int height,
    int drawArrow)		/* Whether or not to draw arrow. */
{
    int borderWidth, activeBorderWidth;
    float arrowWidth = CASCADE_ARROW_WIDTH, arrowHeight = CASCADE_ARROW_HEIGHT;
    double scalingLevel = 1.0; // Stub

    if (menuPtr->menuType == MENUBAR) {
	return;
    }

    Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, menuPtr->borderWidthObj,
	    &borderWidth);
    Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, menuPtr->activeBorderWidthPtr,
	    &activeBorderWidth);
    if ((mePtr->type == CASCADE_ENTRY) && drawArrow) {
	arrowWidth *= scalingLevel;
	arrowHeight *= scalingLevel;

        float px = x + width - borderWidth - activeBorderWidth - arrowWidth;
        float py = y + (height - arrowHeight)/2;

        nvgSave(vg);
	nvgBeginPath(vg);
	nvgMoveTo(vg, px, py);
	nvgLineTo(vg, px, py + arrowHeight);
	nvgLineTo(vg, px + arrowWidth, py + arrowHeight/2);
	nvgClosePath(vg);
        Simple3DBorder bord = (mePtr->state == ENTRY_ACTIVE) ? GetSimple3DBorder(activeBorder) : GetSimple3DBorder(bgBorder);
	nvgFillColor(vg, bord.bg);
	nvgFill(vg);
        // Simulate relief
        int relief = (menuPtr->postedCascade == mePtr) ? TK_RELIEF_SUNKEN : TK_RELIEF_RAISED;
        // Add bevel lines if needed
        nvgRestore(vg);
    } else if (mePtr->accelPtr != NULL) {
	const char *accel = Tcl_GetString(mePtr->accelPtr);
	int left = x + mePtr->labelWidth + activeBorderWidth
		+ mePtr->indicatorSpace;

	if (menuPtr->menuType == MENUBAR) {
	    left += 5;
	}
        float ty = (float)(y + (height + DEFAULT_FONT_SIZE - (DEFAULT_FONT_SIZE/3)) / 2);
        NVGcolor color = nvgRGB(0,0,0); // From gc
        nvgFontSize(vg, DEFAULT_FONT_SIZE);
        nvgFontFace(vg, DEFAULT_FONT);
	DrawChars(vg, accel, mePtr->accelLength, (float)left, ty, color);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DrawMenuEntryIndicator --
 *
 *	This procedure draws the indicator for check/radio.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	nanoVG commands.
 *
 *----------------------------------------------------------------------
 */

static void
DrawMenuEntryIndicator(
    TkMenu *menuPtr,			/* The menu we are drawing */
    TkMenuEntry *mePtr,			/* The entry we are drawing */
    Drawable d_unused,				/* Ignored */
    Tk_3DBorder border,			/* The background color */
    XColor *indicatorColor_unused,		/* Ignored */
    XColor *disableColor_unused,		/* Ignored */
    TCL_UNUSED(Tk_Font),		/* Ignored */
    TCL_UNUSED(const Tk_FontMetrics *),	/* Ignored */
    int x,				/* The left of the entry rect */
    int y,				/* The top of the entry rect */
    TCL_UNUSED(int),			/* Width of menu entry */
    int height)				/* Height of menu entry */
{
    /*
     * Draw check-button indicator.
     */

    if ((mePtr->type == CHECK_BUTTON_ENTRY) && mePtr->indicatorOn) {
	int top, left, activeBorderWidth;
	int disabled = (mePtr->state == ENTRY_DISABLED);
        NVGcolor bgColor = nvgRGB(150,150,150); // From border
        NVGcolor indColor = nvgRGB(0,0,0); // indicatorColor
        NVGcolor disColor = nvgRGB(200,200,200); // disableColor
        NVGcolor selColor = nvgRGB(0,128,0); // selectColor

	Tk_GetPixelsFromObj(NULL, menuPtr->tkwin,
		menuPtr->activeBorderWidthPtr, &activeBorderWidth);
	top = y + height/2;
	left = x + activeBorderWidth + 2 // DECORATION_BORDER_WIDTH
		+ mePtr->indicatorSpace/2;

        // Draw check (square for menu)
        float cx = (float)left;
        float cy = (float)top;
        float r = (float)PTR2INT(mePtr->platformEntryData); // Diameter from original

        nvgSave(vg);
        nvgBeginPath(vg);
        nvgRect(vg, cx - r/2, cy - r/2, r, r);
        nvgFillColor(vg, disabled ? disColor : bgColor);
        nvgFill(vg);
        nvgStrokeColor(vg, indColor);
        nvgStroke(vg);

        if (mePtr->entryFlags & ENTRY_SELECTED) {
            // Draw check mark
            nvgBeginPath(vg);
            nvgMoveTo(vg, cx - r/3, cy - r/3);
            nvgLineTo(vg, cx, cy + r/3);
            nvgLineTo(vg, cx + r/3, cy - r/3);
            nvgStrokeColor(vg, selColor);
            nvgStrokeWidth(vg, 2.0f);
            nvgStroke(vg);
        }
        nvgRestore(vg);
    }

    /*
     * Draw radio-button indicator.
     */

    if ((mePtr->type == RADIO_BUTTON_ENTRY) && mePtr->indicatorOn) {
	int top, left, activeBorderWidth;
	int disabled = (mePtr->state == ENTRY_DISABLED);
	NVGcolor bgColor = nvgRGB(150,150,150);
        NVGcolor indColor = nvgRGB(0,0,0);
        NVGcolor disColor = nvgRGB(200,200,200);
        NVGcolor selColor = nvgRGB(0,128,0);

	Tk_GetPixelsFromObj(NULL, menuPtr->tkwin,
		menuPtr->activeBorderWidthPtr, &activeBorderWidth);
	top = y + height/2;
	left = x + activeBorderWidth + 2
		+ mePtr->indicatorSpace/2;

        // Draw radio (circle)
        float cx = (float)left;
        float cy = (float)top;
        float r = (float)PTR2INT(mePtr->platformEntryData) / 2;

        nvgSave(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, r);
        nvgFillColor(vg, disabled ? disColor : bgColor);
        nvgFill(vg);
        nvgStrokeColor(vg, indColor);
        nvgStroke(vg);

        if (mePtr->entryFlags & ENTRY_SELECTED) {
            nvgBeginPath(vg);
            nvgCircle(vg, cx, cy, r/2);
            nvgFillColor(vg, selColor);
            nvgFill(vg);
        }
        nvgRestore(vg);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DrawMenuSeparator --
 *
 *	This procedure draws a separator menu item.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	nanoVG commands.
 *
 *----------------------------------------------------------------------
 */

static void
DrawMenuSeparator(
    TkMenu *menuPtr,			/* The menu we are drawing */
    TCL_UNUSED(TkMenuEntry *),		/* The entry we are drawing */
    Drawable d_unused,				/* Ignored */
    GC gc_unused,			/* Ignored */
    TCL_UNUSED(Tk_Font),		/* Ignored */
    TCL_UNUSED(const Tk_FontMetrics *),	/* Ignored */
    int x, int y,
    int width, int height)
{
    if (menuPtr->menuType == MENUBAR) {
	return;
    }

    nvgSave(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, (float)x, (float)(y + height/2));
    nvgLineTo(vg, (float)(x + width - 1), (float)(y + height/2));
    nvgStrokeWidth(vg, 1.0f);
    nvgStrokeColor(vg, nvgRGB(100,100,100));
    nvgStroke(vg);
    nvgRestore(vg);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawMenuEntryLabel --
 *
 *	This procedure draws the label part of a menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	nanoVG commands.
 *
 *----------------------------------------------------------------------
 */

static void
DrawMenuEntryLabel(
    TkMenu *menuPtr,		/* The menu we are drawing. */
    TkMenuEntry *mePtr,		/* The entry we are drawing. */
    Drawable d_unused,			/* Ignored */
    GC gc_unused,			/* Ignored */
    Tk_Font tkfont_unused,		/* Ignored */
    const Tk_FontMetrics *fmPtr_unused,/* Ignored */
    int x,			/* Left edge. */
    int y,			/* Top edge. */
    int width,			/* width of entry. */
    int height)			/* height of entry. */
{
    int indicatorSpace = mePtr->indicatorSpace;
    int activeBorderWidth, leftEdge, imageHeight, imageWidth;
    int textHeight = 0, textWidth = 0;	/* stop GCC warning */
    int haveImage = 0, haveText = 0;
    int imageXOffset = 0, imageYOffset = 0;
    int textXOffset = 0, textYOffset = 0;

    Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, menuPtr->activeBorderWidthPtr,
	    &activeBorderWidth);
    leftEdge = x + indicatorSpace + activeBorderWidth;
    if (menuPtr->menuType == MENUBAR) {
	leftEdge += 5;
    }

    // Work out what to draw (same as original)
    if (mePtr->image != NULL) {
	// Tk_SizeOfImage(mePtr->image, &imageWidth, &imageHeight);
	// In nanoVG, images are textures; assume loaded as int id = nvgCreateImage...
	// For port, stub: haveImage = 1; imageWidth = 16; imageHeight = 16;
	haveImage = 1;
	imageWidth = 16;
	imageHeight = 16;
    } else if (mePtr->bitmapPtr != NULL) {
	// Pixmap bitmap = Tk_GetBitmapFromObj(menuPtr->tkwin, mePtr->bitmapPtr);
	// Tk_SizeOfBitmap(menuPtr->display, bitmap, &imageWidth, &imageHeight);
	// Stub
	haveImage = 1;
	imageWidth = 16;
	imageHeight = 16;
    }
    if (!haveImage || (mePtr->compound != COMPOUND_NONE)) {
	if (mePtr->labelLength > 0) {
	    const char *label = Tcl_GetString(mePtr->labelPtr);

	    float bw[4];
            nvgFontSize(vg, DEFAULT_FONT_SIZE);
            nvgFontFace(vg, DEFAULT_FONT);
	    textWidth = (int)nvgTextBounds(vg, 0, 0, label, label + mePtr->labelLength, bw);
	    textHeight = DEFAULT_FONT_SIZE;
	    haveText = 1;
	}
    }

    // Relative positions (same as original)
    if (haveImage && haveText) {
	int fullWidth = (imageWidth > textWidth ? imageWidth : textWidth);

	switch ((enum compound) mePtr->compound) {
	case COMPOUND_TOP:
	    textXOffset = (fullWidth - textWidth)/2;
	    textYOffset = imageHeight/2 + 2;
	    imageXOffset = (fullWidth - imageWidth)/2;
	    imageYOffset = -textHeight/2;
	    break;
	case COMPOUND_BOTTOM:
	    textXOffset = (fullWidth - textWidth)/2;
	    textYOffset = -imageHeight/2;
	    imageXOffset = (fullWidth - imageWidth)/2;
	    imageYOffset = textHeight/2 + 2;
	    break;
	case COMPOUND_LEFT:
	    textXOffset = imageWidth + 2;
	    textYOffset = 0;
	    imageXOffset = 0;
	    imageYOffset = 0;
	    break;
	case COMPOUND_RIGHT:
	    textXOffset = 0;
	    textYOffset = 0;
	    imageXOffset = textWidth + 2;
	    imageYOffset = 0;
	    break;
	case COMPOUND_CENTER:
	    textXOffset = (fullWidth - textWidth)/2;
	    textYOffset = 0;
	    imageXOffset = (fullWidth - imageWidth)/2;
	    imageYOffset = 0;
	    break;
	case COMPOUND_NONE:
	    break;
	}
    } else {
	textXOffset = 0;
	textYOffset = 0;
	imageXOffset = 0;
	imageYOffset = 0;
    }

    // Draw image or bitmap (stub - in real, nvgImagePattern or draw pixels)
    if (mePtr->image != NULL) {
        // Tk_RedrawImage...
        // Stub: draw rect as placeholder
        nvgBeginPath(vg);
        nvgRect(vg, leftEdge + imageXOffset, y + (mePtr->height-imageHeight)/2 + imageYOffset, imageWidth, imageHeight);
        nvgFillColor(vg, nvgRGB(255,0,0));
        nvgFill(vg);
    } else if (mePtr->bitmapPtr != NULL) {
        // XCopyPlane... Stub similar
        nvgBeginPath(vg);
        nvgRect(vg, leftEdge + imageXOffset, y + (mePtr->height-imageHeight)/2 + imageYOffset, imageWidth, imageHeight);
        nvgFillColor(vg, nvgRGB(128,128,128));
        nvgFill(vg);
    }

    if ((mePtr->compound != COMPOUND_NONE) || !haveImage) {
	float baseline = y + (height + DEFAULT_FONT_SIZE - (DEFAULT_FONT_SIZE/3)) / 2;

	if (mePtr->labelLength > 0) {
	    const char *label = Tcl_GetString(mePtr->labelPtr);
            NVGcolor color = nvgRGB(0,0,0);

            nvgFontSize(vg, DEFAULT_FONT_SIZE);
            nvgFontFace(vg, DEFAULT_FONT);
	    DrawChars(vg, label, mePtr->labelLength, (float)(leftEdge + textXOffset), baseline + textYOffset, color);
	    DrawMenuUnderline(menuPtr, mePtr, d_unused, gc_unused, tkfont_unused, fmPtr_unused,
		    x + textXOffset, y + textYOffset,
		    width, height);
	}
    }

    if (mePtr->state == ENTRY_DISABLED) {
        // Overlay gray or something
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFillColor(vg, nvgRGBA(200,200,200,128));
        nvgFill(vg);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DrawMenuUnderline --
 *
 *	On appropriate platforms, draw the underline character for the menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	nanoVG commands.
 *
 *----------------------------------------------------------------------
 */

static void
DrawMenuUnderline(
    TkMenu *menuPtr,		/* The menu to draw into */
    TkMenuEntry *mePtr,		/* The entry we are drawing */
    Drawable d_unused,			/* Ignored */
    GC gc_unused,			/* Ignored */
    Tk_Font tkfont_unused,		/* Ignored */
    const Tk_FontMetrics *fmPtr_unused,/* Ignored */
    int x, int y,
    TCL_UNUSED(int), int height)
{
    if (mePtr->labelPtr != NULL) {
	int len;

	len = Tcl_GetCharLength(mePtr->labelPtr);
	if (mePtr->underline < len && mePtr->underline >= -len) {
	    int activeBorderWidth, leftEdge, ch;
	    const char *label, *start, *end;

	    label = Tcl_GetString(mePtr->labelPtr);
	    start = Tcl_UtfAtIndex(label, (mePtr->underline < 0) ? mePtr->underline + len : mePtr->underline);
	    end = start + Tcl_UtfToUniChar(start, &ch);

	    Tk_GetPixelsFromObj(NULL, menuPtr->tkwin,
		    menuPtr->activeBorderWidthPtr, &activeBorderWidth);
	    leftEdge = x + mePtr->indicatorSpace + activeBorderWidth;
	    if (menuPtr->menuType == MENUBAR) {
		leftEdge += 5;
	    }

            float ty = y + (height + DEFAULT_FONT_SIZE - (DEFAULT_FONT_SIZE/3)) / 2;
            NVGcolor color = nvgRGB(0,0,0);
            nvgFontSize(vg, DEFAULT_FONT_SIZE);
            nvgFontFace(vg, DEFAULT_FONT);
	    UnderlineChars(vg, label, start - label, end - label, (float)leftEdge, ty, DEFAULT_FONT_SIZE, DEFAULT_FONT_SIZE/3, color);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpPostMenu --
 *
 *	Posts a menu on the screen so that the top left corner of the
 *      specified entry is located at the point (x, y) in screen coordinates.
 *      If the entry parameter is negative, the upper left corner of the
 *      menu itself is placed at the point.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The menu is "posted" - in GLFW, simulate by setting state for overlay draw.
 *
 *----------------------------------------------------------------------
 */

int
TkpPostMenu(
    Tcl_Interp *interp,
    TkMenu *menuPtr,
    int x, int y, Tcl_Size index)
{
    return TkpPostTearoffMenu(interp, menuPtr, x, y, index);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpPostTearoffMenu --
 *
 *	Posts a tearoff menu on the screen. In GLFW, create a new window or overlay.
 *
 * Results:
 *	Returns a standard Tcl Error.
 *
 * Side effects:
 *	The menu is posted.
 *
 *----------------------------------------------------------------------
 */

int
TkpPostTearoffMenu(
    TCL_UNUSED(Tcl_Interp *),	/* The interpreter of the menu */
    TkMenu *menuPtr,		/* The menu we are posting */
    int x, int y, Tcl_Size index)	/* The root X,Y coordinates where the
				 * specified entry will be posted */
{
    // In GLFW, creating a new window for popup
    // But for simplicity, set global state for drawing overlay at (x,y)
    // Assume app handles rendering the menu at position.
    // Stub: compute geometry, set menu->postX = x; menu->postY = y; menu->posted = 1;
    TkActivateMenuEntry(menuPtr, TCL_INDEX_NONE);
    TkRecomputeMenu(menuPtr);
    // Result from post command
    int result = TkPostCommand(menuPtr);
    if (result != TCL_OK) {
        return result;
    }

    if (menuPtr->tkwin == NULL) {
        return TCL_OK;
    }

    // Adjust position (simplified, no vroot)
    if (index >= menuPtr->numEntries) {
	index = menuPtr->numEntries - 1;
    }
    if (index >= 0) {
	y -= menuPtr->entries[index]->y;
    }

    // Clamp to screen (assume screen size known)
    int screenW = 1920, screenH = 1080; // Stub
    int reqW = Tk_ReqWidth(menuPtr->tkwin);
    int reqH = Tk_ReqHeight(menuPtr->tkwin);
    if (x + reqW > screenW) x = screenW - reqW;
    if (x < 0) x = 0;
    if (y + reqH > screenH) y = screenH - reqH;
    if (y < 0) y = 0;

    // Set position for drawing
    // menuPtr->postX = x; menuPtr->postY = y; (add fields if needed)

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GetMenuSeparatorGeometry --
 *
 *	Gets the width and height of the indicator area of a menu.
 *
 * Results:
 *	widthPtr and heightPtr are set.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
GetMenuSeparatorGeometry(
    TCL_UNUSED(TkMenu *),		/* The menu we are measuring */
    TCL_UNUSED(TkMenuEntry *),		/* The entry we are measuring */
    TCL_UNUSED(Tk_Font),		/* The precalculated font */
    const Tk_FontMetrics *fmPtr_unused,	/* The precalcualted font metrics */
    int *widthPtr,			/* The resulting width */
    int *heightPtr)			/* The resulting height */
{
    *widthPtr = 0;
    *heightPtr = DEFAULT_FONT_SIZE;
}

/*
 *----------------------------------------------------------------------
 *
 * GetTearoffEntryGeometry --
 *
 *	Gets the width and height of the indicator area of a menu.
 *
 * Results:
 *	widthPtr and heightPtr are set.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
GetTearoffEntryGeometry(
    TkMenu *menuPtr,			/* The menu we are drawing */
    TCL_UNUSED(TkMenuEntry *),		/* The entry we are measuring */
    Tk_Font tkfont_unused,			/* The precalculated font */
    const Tk_FontMetrics *fmPtr_unused,	/* The precalculated font metrics */
    int *widthPtr,			/* The resulting width */
    int *heightPtr)			/* The resulting height */
{
    if (menuPtr->menuType != MAIN_MENU) {
	*heightPtr = 0;
	*widthPtr = 0;
    } else {
	*heightPtr = DEFAULT_FONT_SIZE;
	float bw[4];
	nvgFontSize(vg, DEFAULT_FONT_SIZE);
	nvgFontFace(vg, DEFAULT_FONT);
	nvgTextBounds(vg, 0, 0, "W", NULL, bw);
	*widthPtr = (int)(bw[2] - bw[0]);
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkpComputeMenubarGeometry --
 *
 *	This procedure is invoked to recompute the size and layout of a menu
 *	that is a menubar clone.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fields of menu entries are changed to reflect their current positions,
 *	and the size of the menu "window" itself may be changed. (No real window)
 *
 *--------------------------------------------------------------
 */

void
TkpComputeMenubarGeometry(
    TkMenu *menuPtr)		/* Structure describing menu. */
{
    Tk_Font tkfont;
    Tk_FontMetrics menuMetrics, entryMetrics, *fmPtr;
    int x, y, height, width, i, j, labelWidth, indicatorSpace;
    int borderWidth, activeBorderWidth;
    TkMenuEntry *mePtr;
    
    if (menuPtr->tkwin == NULL) {
	return;
    }
    
    tkfont = Tk_GetFontFromObj(menuPtr->tkwin, menuPtr->fontPtr);
    Tk_GetFontMetrics(tkfont, &menuMetrics);
    x = y = borderWidth = activeBorderWidth = 0;
    indicatorSpace = labelWidth = 0;
    
    Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, menuPtr->borderWidthObj,
	    &borderWidth);
    Tk_GetPixelsFromObj(NULL, menuPtr->tkwin,
	    menuPtr->activeBorderWidthPtr, &activeBorderWidth);
    
    x = y = borderWidth;
    height = 0;
    
    for (i = 0; i < menuPtr->numEntries; i++) {
	mePtr = menuPtr->entries[i];
	if (mePtr->fontPtr == NULL) {
	    fmPtr = &menuMetrics;
	} else {
	    tkfont = Tk_GetFontFromObj(menuPtr->tkwin, mePtr->fontPtr);
	    Tk_GetFontMetrics(tkfont, &entryMetrics);
	    fmPtr = &entryMetrics;
	}
	
	GetMenuLabelGeometry(mePtr, tkfont, fmPtr, &width, &height);
	mePtr->height = height + 2 * activeBorderWidth + 10;
	
	if (mePtr->type == SEPARATOR_ENTRY) {
	    mePtr->width = 10;
	} else {
	    mePtr->width = width + 2 * activeBorderWidth + 10;
	}
	
	mePtr->x = x;
	mePtr->y = y;
	x += mePtr->width;
	
	if (mePtr->entryFlags & ENTRY_LAST_COLUMN) {
	    x = borderWidth;
	    y += mePtr->height;
	}
    }
    
    menuPtr->totalWidth = 0;
    for (i = 0; i < menuPtr->numEntries; i++) {
	x = menuPtr->entries[i]->x + menuPtr->entries[i]->width;
	if (x > menuPtr->totalWidth) {
	    menuPtr->totalWidth = x;
	}
    }
    
    height = 0;
    for (i = 0; i < menuPtr->numEntries; i++) {
	y = menuPtr->entries[i]->y + menuPtr->entries[i]->height;
	if (y > height) {
	    height = y;
	}
    }
    
    menuPtr->totalWidth += borderWidth;
    menuPtr->totalHeight = height + borderWidth;
}

/*
 *----------------------------------------------------------------------
 *
 * DrawTearoffEntry --
 *
 *	This procedure draws the tearoff entry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	nanoVG commands.
 *
 *----------------------------------------------------------------------
 */

static void
DrawTearoffEntry(
    TkMenu *menuPtr,			/* The menu we are drawing */
    TCL_UNUSED(TkMenuEntry *),		/* The entry we are drawing */
    Drawable d_unused,				/* Ignored */
    GC gc_unused,			/* Ignored */
    TCL_UNUSED(Tk_Font),		/* Ignored */
    TCL_UNUSED(const Tk_FontMetrics *),	/* Ignored */
    int x, int y,
    int width, int height)
{
    if (menuPtr->menuType != MAIN_MENU) {
	return;
    }

    float segmentWidth = 6.0f;
    float maxX = x + width - 1;

    nvgSave(vg);
    float px = (float)x;
    float py = (float)(y + height/2);

    while (px < maxX) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, px, py);
        float ex = px + segmentWidth;
        if (ex > maxX) ex = maxX;
        nvgLineTo(vg, ex, py);
        nvgStrokeWidth(vg, 1.0f);
        nvgStrokeColor(vg, nvgRGB(100,100,100));
        nvgStroke(vg);
        px += 2 * segmentWidth;
    }
    nvgRestore(vg);
}

/*
 *--------------------------------------------------------------
 *
 * TkpInitializeMenuBindings --
 *
 *	For every interp, initializes the bindings for Windows menus. Does
 *	nothing on Mac or XWindows.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	C-level bindings are setup for the interp which will handle Alt-key
 *	sequences for menus without beeping or interfering with user-defined
 *	Alt-key bindings.
 *
 *--------------------------------------------------------------
 */

void
TkpInitializeMenuBindings(
    TCL_UNUSED(Tcl_Interp *),		/* The interp to set. */
    TCL_UNUSED(Tk_BindingTable))	/* The table to add to. */
{
    /*
     * Nothing to do on Wayland.
     */
}

/*
 *----------------------------------------------------------------------
 *
 * SetHelpMenu --
 *
 *	Given a menu, check to see whether or not it is a help menu cascade in
 *	a menubar. If it is, the entry that points to this menu will be
 *	marked.
 *
 * RESULTS:
 *	None.
 *
 * Side effects:
 *	Will set the ENTRY_HELP_MENU flag appropriately.
 *
 *----------------------------------------------------------------------
 */

static void
SetHelpMenu(
    TkMenu *menuPtr)		/* The menu we are checking */
{
    TkMenuEntry *cascadeEntryPtr;
    int useMotifHelp = 0;
    const char *option = NULL;
    if (menuPtr->tkwin) {
	option = Tk_GetOption(menuPtr->tkwin, "useMotifHelp", "UseMotifHelp");
	if (option != NULL) {
	    Tcl_GetBoolean(NULL, option, &useMotifHelp);
	}
    }

    if (!useMotifHelp) {
	return;
    }

    for (cascadeEntryPtr = menuPtr->menuRefPtr->parentEntryPtr;
	    cascadeEntryPtr != NULL;
	    cascadeEntryPtr = cascadeEntryPtr->nextCascadePtr) {
	if ((cascadeEntryPtr->menuPtr->menuType == MENUBAR)
		&& (cascadeEntryPtr->menuPtr->mainMenuPtr->tkwin != NULL)
		&& (menuPtr->mainMenuPtr->tkwin != NULL)) {
	    TkMenu *mainMenuPtr = cascadeEntryPtr->menuPtr->mainMenuPtr;
	    char *helpMenuName = (char *)Tcl_Alloc(strlen(Tk_PathName(
		    mainMenuPtr->tkwin)) + strlen(".help") + 1);

	    strcpy(helpMenuName, Tk_PathName(mainMenuPtr->tkwin));
	    strcat(helpMenuName, ".help");
	    if (strcmp(helpMenuName,
		    Tk_PathName(menuPtr->mainMenuPtr->tkwin)) == 0) {
		cascadeEntryPtr->entryFlags |= ENTRY_HELP_MENU;
	    } else {
		cascadeEntryPtr->entryFlags &= ~ENTRY_HELP_MENU;
	    }
	    Tcl_Free(helpMenuName);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDrawMenuEntry --
 *
 *	Draws the given menu entry at the given coordinates with the given
 *	attributes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	nanoVG commands to display the menu entry.
 *
 *----------------------------------------------------------------------
 */

void
TkpDrawMenuEntry(
    TkMenuEntry *mePtr,		/* The entry to draw */
    Drawable d_unused,			/* Ignored, use vg */
    Tk_Font tkfont_unused,		/* Ignored */
    const Tk_FontMetrics *menuMetricsPtr_unused,
				/* Ignored */
    int x, int y,
    int width, int height,
    int strictMotif,		/* Boolean flag */
    int drawArrow)		/* Whether or not to draw the cascade arrow
				 * for cascade items. */
{
    // GC, indicatorGC, colors stubbed to NVGcolors
    NVGcolor fgColor = nvgRGB(0,0,0);
    NVGcolor indColor = nvgRGB(0,0,0);
    NVGcolor disColor = nvgRGB(200,200,200);

    Tk_3DBorder bgBorder = NULL; // Stub from obj
    Tk_3DBorder activeBorder = NULL; // Stub

    int padY = (mePtr->menuPtr->menuType == MENUBAR) ? 3 : 0;
    int adjustedY = y + padY;
    int adjustedHeight = height - 2 * padY;

    nvgFontSize(vg, DEFAULT_FONT_SIZE);
    nvgFontFace(vg, DEFAULT_FONT);

    DrawMenuEntryBackground(mePtr->menuPtr, mePtr, NULL, activeBorder,
	    bgBorder, x, y, width, height);

    if (mePtr->type == SEPARATOR_ENTRY) {
	DrawMenuSeparator(mePtr->menuPtr, mePtr, NULL, NULL, NULL,
		NULL, x, adjustedY, width, adjustedHeight);
    } else if (mePtr->type == TEAROFF_ENTRY) {
	DrawTearoffEntry(mePtr->menuPtr, mePtr, NULL, NULL, NULL, NULL, x, adjustedY,
		width, adjustedHeight);
    } else {
	DrawMenuEntryLabel(mePtr->menuPtr, mePtr, NULL, NULL, NULL, NULL, x, adjustedY,
		width, adjustedHeight);
	DrawMenuEntryAccelerator(mePtr->menuPtr, mePtr, NULL, NULL, NULL, NULL,
		activeBorder, bgBorder, x, adjustedY, width, adjustedHeight,
		drawArrow);
	if (!mePtr->hideMargin) {
	    if (mePtr->state == ENTRY_ACTIVE) {
		bgBorder = activeBorder;
	    }
	    DrawMenuEntryIndicator(mePtr->menuPtr, mePtr, NULL, bgBorder, NULL,
		    NULL, NULL, NULL, x, adjustedY, width,
		    adjustedHeight);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetMenuLabelGeometry --
 *
 *	Figures out the size of the label portion of a menu item.
 *
 * Results:
 *	widthPtr and heightPtr are filled in with the correct geometry
 *	information.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
GetMenuLabelGeometry(
    TkMenuEntry *mePtr,		/* The entry we are computing */
    Tk_Font tkfont_unused,		/* The precalculated font */
    const Tk_FontMetrics *fmPtr_unused,/* The precalculated metrics */
    int *widthPtr,		/* The resulting width of the label portion */
    int *heightPtr)		/* The resulting height of the label
				 * portion */
{
    TkMenu *menuPtr = mePtr->menuPtr;
    int haveImage = 0, imageWidth = 0, imageHeight = 0;

    if (mePtr->image != NULL) {
	// Tk_SizeOfImage...
	// Stub
	*widthPtr = 16;
	*heightPtr = 16;
	haveImage = 1;
	imageWidth = 16;
	imageHeight = 16;
    } else if (mePtr->bitmapPtr != NULL) {
	// Stub
	haveImage = 1;
	imageWidth = 16;
	imageHeight = 16;
	*widthPtr = imageWidth;
	*heightPtr = imageHeight;
    } else {
	*heightPtr = 0;
	*widthPtr = 0;
    }

    if (haveImage && (mePtr->compound == COMPOUND_NONE)) {
	// No text, already set above
    } else {
	int textWidth = 0, textHeight = 0;
	
	if (mePtr->labelPtr != NULL) {
	    const char *label = Tcl_GetString(mePtr->labelPtr);

	    float bw[4];
            nvgFontSize(vg, DEFAULT_FONT_SIZE);
            nvgFontFace(vg, DEFAULT_FONT);
	    textWidth = (int)nvgTextBounds(vg, 0, 0, label, label + mePtr->labelLength, bw);
	    textHeight = DEFAULT_FONT_SIZE;
	    
	    if ((mePtr->compound != COMPOUND_NONE) && haveImage) {
		switch ((enum compound) mePtr->compound) {
		case COMPOUND_TOP:
		case COMPOUND_BOTTOM:
		    *heightPtr = imageHeight + textHeight + 2;
		    *widthPtr = (imageWidth > textWidth ? imageWidth : textWidth);
		    break;
		case COMPOUND_LEFT:
		case COMPOUND_RIGHT:
		    *heightPtr = (imageHeight > textHeight ? imageHeight : textHeight);
		    *widthPtr = imageWidth + textWidth + 2;
		    break;
		case COMPOUND_CENTER:
		    *heightPtr = (imageHeight > textHeight ? imageHeight : textHeight);
		    *widthPtr = (imageWidth > textWidth ? imageWidth : textWidth);
		    break;
		case COMPOUND_NONE:
		    break;
		}
	    } else {
		*heightPtr = textHeight;
		*widthPtr = textWidth;
	    }
	} else {
	    *heightPtr = DEFAULT_FONT_SIZE;
	}
    }
    *heightPtr += 1;
}

/*
 *--------------------------------------------------------------
 *
 * TkpComputeStandardMenuGeometry --
 *
 *	This procedure is invoked to recompute the size and layout of a menu
 *	that is not a menubar clone.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fields of menu entries are changed to reflect their current positions,
 *	and the size of the menu "window" itself may be changed.
 *
 *--------------------------------------------------------------
 */

void
TkpComputeStandardMenuGeometry(
    TkMenu *menuPtr)		/* Structure describing menu. */
{
    Tk_Font tkfont, menuFont;
    Tk_FontMetrics menuMetrics, entryMetrics, *fmPtr;
    int x, y, height, width, indicatorSpace, labelWidth, accelWidth;
    int borderWidth, activeBorderWidth;
    int i, j, lastColumnBreak = 0;
    TkMenuEntry *mePtr;
    
    if (menuPtr->tkwin == NULL) {
	return;
    }
    
    x = y = borderWidth = activeBorderWidth = 0;
    indicatorSpace = labelWidth = accelWidth = 0;
    
    Tk_GetPixelsFromObj(menuPtr->interp, menuPtr->tkwin,
	    menuPtr->borderWidthObj, &borderWidth);
    x = y = borderWidth;
    
    Tk_GetPixelsFromObj(menuPtr->interp, menuPtr->tkwin,
	    menuPtr->activeBorderWidthPtr, &activeBorderWidth);
    
    menuFont = Tk_GetFontFromObj(menuPtr->tkwin, menuPtr->fontPtr);
    Tk_GetFontMetrics(menuFont, &menuMetrics);
    
    for (i = 0; i < menuPtr->numEntries; i++) {
	mePtr = menuPtr->entries[i];
	if (mePtr->fontPtr == NULL) {
	    tkfont = menuFont;
	    fmPtr = &menuMetrics;
	} else {
	    tkfont = Tk_GetFontFromObj(menuPtr->tkwin, mePtr->fontPtr);
	    Tk_GetFontMetrics(tkfont, &entryMetrics);
	    fmPtr = &entryMetrics;
	}
	
	if ((i == 0) || mePtr->entryFlags & ENTRY_LAST_COLUMN) {
	    if (i != 0) {
		for (j = lastColumnBreak; j < i; j++) {
		    menuPtr->entries[j]->indicatorSpace = indicatorSpace;
		    menuPtr->entries[j]->labelWidth = labelWidth;
		    menuPtr->entries[j]->width = indicatorSpace + labelWidth
			    + accelWidth + 2 * activeBorderWidth;
		}
	    }
	    indicatorSpace = labelWidth = accelWidth = 0;
	    lastColumnBreak = i;
	    y = borderWidth;
	}
	
	if (mePtr->type == SEPARATOR_ENTRY) {
	    GetMenuSeparatorGeometry(menuPtr, mePtr, tkfont, fmPtr,
		    &width, &height);
	    mePtr->height = height;
	} else if (mePtr->type == TEAROFF_ENTRY) {
	    GetTearoffEntryGeometry(menuPtr, mePtr, tkfont, fmPtr,
		    &width, &height);
	    mePtr->height = height;
	} else {
	    GetMenuLabelGeometry(mePtr, tkfont, fmPtr, &width, &height);
	    mePtr->height = height;
	    if (width > labelWidth) {
		labelWidth = width;
	    }
	    
	    GetMenuIndicatorGeometry(menuPtr, mePtr, tkfont, fmPtr,
		    &width, &height);
	    if (width > indicatorSpace) {
		indicatorSpace = width;
	    }
	    
	    GetMenuAccelGeometry(menuPtr, mePtr, tkfont, fmPtr,
		    &width, &height);
	    if (width > accelWidth) {
		accelWidth = width;
	    }
	    
	    mePtr->height += 2 * activeBorderWidth + MENU_DIVIDER_HEIGHT;
	}
	mePtr->x = x;
	mePtr->y = y;
	y += mePtr->height;
    }
    
    for (j = lastColumnBreak; j < menuPtr->numEntries; j++) {
	menuPtr->entries[j]->indicatorSpace = indicatorSpace;
	menuPtr->entries[j]->labelWidth = labelWidth;
	menuPtr->entries[j]->width = indicatorSpace + labelWidth
		+ accelWidth + 2 * activeBorderWidth;
    }
    
    width = x + indicatorSpace + labelWidth + accelWidth
	    + 2 * activeBorderWidth + 2 * borderWidth;
    height = y + borderWidth;
    
    menuPtr->totalWidth = width;
    menuPtr->totalHeight = height;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMenuNotifyToplevelCreate --
 *
 *	This routine reconfigures the menu and the clones indicated by
 *	menuName because a toplevel has been created and any system menus need
 *	to be created. Not applicable to Wayland/GLFW.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	An idle handler is set up to do the reconfiguration.
 *
 *----------------------------------------------------------------------
 */

void
TkpMenuNotifyToplevelCreate(
    TCL_UNUSED(Tcl_Interp *),	/* The interp the menu lives in. */
    TCL_UNUSED(const char *))	/* The name of the menu to reconfigure. */
{
    /*
     * Nothing to do.
     */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMenuInit --
 *
 *	Does platform-specific initialization of menus.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initialize nanoVG if not done.
 *
 *----------------------------------------------------------------------
 */

void
TkpMenuInit(void)
{
    // Assume vg is set elsewhere
    // nvgCreateFont(vg, DEFAULT_FONT, "path/to/font.ttf");
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMenuThreadInit --
 *
 *	Does platform-specific initialization of thread-specific menu state.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkpMenuThreadInit(void)
{
    /*
     * Nothing to do.
     */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDrawCheckIndicator --
 *
 *	Ported to nanoVG, but signature kept.
 *
 *----------------------------------------------------------------------
 */

void
TkpDrawCheckIndicator(Tk_Window tkwin_unused,
			    Display *display_unused, Drawable d_unused, int x, int y,
			    Tk_3DBorder bgBorder, XColor *indicatorColor_unused,
			    XColor *selectColor_unused, XColor *disColor_unused, int on,
			    int disabled, int mode)
{
    // Called from DrawMenuEntryIndicator, already ported there.
    // Stub if needed
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
