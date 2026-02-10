/*
 *
 * tkWaylandMenu.c –
 * 
 * This module implements the Wayland/GLFW/NanoVG platform-specific
 * features of menus. 
 * 
 * Copyright © 1996-1998 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 * 
 * See the file “license.terms” for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */


#include “tkInt.h” 
#include “tkGlfwInt.h” 
#include “tkMenu.h” 
#include <nanovg.h> 
/* Default font settings. */ 
#define DEFAULT_FONT “sans” 
#define DEFAULT_FONT_SIZE 14.0f 
/* Menu constants. */ 
#define MENU_MARGIN_WIDTH	2 
#define MENU_DIVIDER_HEIGHT	2 
#define CASCADE_ARROW_WIDTH	10 
#define CASCADE_ARROW_HEIGHT	8 
#define ENTRY_HELP_MENU		ENTRY_PLATFORM_FLAG1

/* 
 * Helper structure to represent 3D border colors for NanoVG. 
 */ 
typedef struct { 
 NVGcolor light; 
 NVGcolor dark; 
 NVGcolor bg; 
} Simple3DBorder; 

/* 
 * Forward declarations. 
 */ 

static void SetHelpMenu(TkMenu *menuPtr); 
static void DrawMenuEntryAccelerator(TkMenu *menuPtr, 
				 TkMenuEntry *mePtr, Drawable d, GC gc, 
				 Tk_Font tkfont, const Tk_FontMetrics *fmPtr, 
				 Tk_3DBorder activeBorder, Tk_3DBorder bgBorder, 
				 int x, int y, int width, int height, int drawArrow); 
static void DrawMenuEntryBackground(TkMenu *menuPtr, 
				 TkMenuEntry *mePtr, Drawable d, 
				 Tk_3DBorder activeBorder, Tk_3DBorder bgBorder, 
				 int x, int y, int width, int height); 
static void DrawMenuEntryIndicator(TkMenu *menuPtr, 
				 TkMenuEntry *mePtr, Drawable d, 
				 Tk_3DBorder border, XColor *indicatorColor, 
				 XColor *disableColor, Tk_Font tkfont, 
				 const Tk_FontMetrics *fmPtr, int x, int y, 
				 int width, int height); 
static void DrawMenuEntryLabel(TkMenu * menuPtr, 
			 TkMenuEntry *mePtr, Drawable d, GC gc, 
			 Tk_Font tkfont, const Tk_FontMetrics *fmPtr, 
			 int x, int y, int width, int height); 
static void DrawMenuSeparator(TkMenu *menuPtr, 
			 TkMenuEntry *mePtr, Drawable d, GC gc, 
			 Tk_Font tkfont, const Tk_FontMetrics *fmPtr, 
			 int x, int y, int width, int height); 
static void DrawTearoffEntry(TkMenu *menuPtr, 
			 TkMenuEntry *mePtr, Drawable d, GC gc, 
			 Tk_Font tkfont, const Tk_FontMetrics *fmPtr, 
			 int x, int y, int width, int height); 
static void DrawMenuUnderline(TkMenu *menuPtr, 
			 TkMenuEntry *mePtr, Drawable d, GC gc, 
			 Tk_Font tkfont, const Tk_FontMetrics *fmPtr, 
			 int x, int y, int width, int height); 
static void GetMenuAccelGeometry(TkMenu *menuPtr, 
				 TkMenuEntry *mePtr, Tk_Font tkfont, 
				 const Tk_FontMetrics *fmPtr, int *widthPtr, 
				 int *heightPtr); 
static void GetMenuLabelGeometry(TkMenuEntry *mePtr, 
				 Tk_Font tkfont, const Tk_FontMetrics *fmPtr, 
				 int *widthPtr, int *heightPtr); 
static void GetMenuIndicatorGeometry(TkMenu *menuPtr, 
				 TkMenuEntry *mePtr, Tk_Font tkfont, 
				 const Tk_FontMetrics *fmPtr, 
				 int *widthPtr, int *heightPtr); 
static void GetMenuSeparatorGeometry(TkMenu *menuPtr, 
				 TkMenuEntry *mePtr, Tk_Font tkfont, 
				 const Tk_FontMetrics *fmPtr, 
				 int *widthPtr, int *heightPtr); 
static void GetTearoffEntryGeometry(TkMenu *menuPtr, 
				 TkMenuEntry *mePtr, Tk_Font tkfont, 
				 const Tk_FontMetrics *fmPtr, int *widthPtr, 
				 int *heightPtr); 
/* 
 *––––––––––––––––––––––––––––––––––– 
 * 
 * 
 * Helper Functions for NanoVG Drawing 
 * 
 * 
 *––––––––––––––––––––––––––––––––––– 
 */

/*
 * --------------------------------------------------------------------------------
 * GetSimple3DBorder –
 *
 *     Extracts colors from Tk_3DBorder for NanoVG rendering.
 *
 * Results:
 *     Returns a Simple3DBorder with light/dark/bg colors.
 *
 * Side effects:
 *     None.
 * --------------------------------------------------------------------------------
 */

static Simple3DBorder 
GetSimple3DBorder( 
		 TCL_UNUSED(Tk_3DBorder) border) 
{ 
 Simple3DBorder s; 
 /* Dummy colors; in real implementation, extract from Tk_3DBorder. */ 
 s.light = nvgRGB(200, 200, 200); 
 s.dark = nvgRGB(100, 100, 100); 
 s.bg = nvgRGB(150, 150, 150); 
 return s; 
}

/*
 * --------------------------------------------------------------------------------
 * Draw3DRect –
 *
 *     Simulates a 3D beveled rectangle using NanoVG.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Renders a beveled border into the NanoVG context.
 * --------------------------------------------------------------------------------
 */

static void 
Draw3DRect( 
	 NVGcontext *vg, 
	 float x, 
	 float y, 
	 float w, 
	 float h, 
	 int borderWidth, 
	 int relief, 
	 Simple3DBorder bord) 
{ 
 nvgSave(vg); 
 /* Fill background. */ 
 nvgBeginPath(vg); 
 nvgRect(vg, x, y, w, h); 
 nvgFillColor(vg, bord.bg); 
 nvgFill(vg); 
 if (relief != TK_RELIEF_FLAT) { 
	/* Draw top-left bevel. */ 
	nvgBeginPath(vg); 
	nvgMoveTo(vg, x, y + h); 
	nvgLineTo(vg, x, y); 
	nvgLineTo(vg, x + w, y); 
	nvgStrokeWidth(vg, (float)borderWidth); 
	nvgStrokeColor(vg, (relief == TK_RELIEF_RAISED) ? bord.light : bord.dark); 
	nvgStroke(vg); 
	/* Draw bottom-right bevel. */ 
	nvgBeginPath(vg); 
	nvgMoveTo(vg, x + w, y); 
	nvgLineTo(vg, x + w, y + h); 
	nvgLineTo(vg, x, y + h); 
	nvgStrokeColor(vg, (relief == TK_RELIEF_RAISED) ? bord.dark : bord.light); 
	nvgStroke(vg); 
 } 
 nvgRestore(vg); 
}

/*
 * --------------------------------------------------------------------------------
 * DrawChars –
 *
 *     Renders a text run at the specified position using NanoVG.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Draws glyphs into the NanoVG context.
 * --------------------------------------------------------------------------------
 */

static void 
DrawChars( 
	 NVGcontext *vg, 
	 const char *text, 
	 int len, 
	 float x, 
	 float y, 
	 NVGcolor color) 
{ 
 nvgFillColor(vg, color); 
 nvgTextAlign(vg, NVG_ALIGN_LEFT || NVG_ALIGN_TOP); 
 nvgText(vg, x, y, text, text + len); 
}

/*
 * --------------------------------------------------------------------------------
 * UnderlineChars –
 *
 *     Draws an underline beneath a substring within a text run.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Renders a line into the NanoVG context.
 * --------------------------------------------------------------------------------
 */

static void 
UnderlineChars( 
	 NVGcontext *vg, 
	 const char *text, 
	 int start, 
	 int end, 
	 float x, 
	 float y, 
	 TCL_UNUSED(float) ascent, 
	 TCL_UNUSED(float) descent, 
	 NVGcolor color) 
{ 
 float bounds[4]; 
 nvgTextBounds(vg, x, y, text + start, text + end, bounds); 
 nvgBeginPath(vg); 
 nvgMoveTo(vg, bounds[0], y + DEFAULT_FONT_SIZE); 
 nvgLineTo(vg, bounds[2], y + DEFAULT_FONT_SIZE); 
 nvgStrokeWidth(vg, 1.0f); 
 nvgStrokeColor(vg, color); 
 nvgStroke(vg); 
}

/* 
 *––––––––––––––––––––––––––––––––––– 
 * 
 - Platform Menu Functions 
 - 
 *––––––––––––––––––––––––––––––––––– 
 */

/*
 * --------------------------------------------------------------------------------
 * TkpNewMenu –
 *
 *     Initializes platform-specific state for a new menu.
 *
 * Results:
 *     Standard Tcl result.
 *
 * Side effects:
 *     May configure help-menu linkage.
 * --------------------------------------------------------------------------------
 */

int 
TkpNewMenu( 
	 TkMenu *menuPtr) 
{ 
 SetHelpMenu(menuPtr); 
 return TCL_OK; 
}

/*
 * --------------------------------------------------------------------------------
 * TkpDestroyMenu –
 *
 *     Releases platform-specific menu structures.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Frees platform allocations (no-op on Wayland).
 * --------------------------------------------------------------------------------
 */

void 
TkpDestroyMenu( 
	 TCL_UNUSED(TkMenu *)) 
{ 
 /* Nothing to do on Wayland. */ 
}

/*
 * --------------------------------------------------------------------------------
 * TkpDestroyMenuEntry –
 *
 *     Cleans up platform-specific data for a menu entry.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Frees platform allocations (no-op on Wayland).
 * --------------------------------------------------------------------------------
 */

void 
TkpDestroyMenuEntry( 
		 TCL_UNUSED(TkMenuEntry *)) 
{ 
 /* Nothing to do on Wayland. */ 
}

/*
 * --------------------------------------------------------------------------------
 * TkpConfigureMenuEntry –
 *
 *     Applies platform-specific configuration to a menu entry.
 *
 * Results:
 *     Standard Tcl result.
 *
 * Side effects:
 *     May mark help-menu cascades.
 * --------------------------------------------------------------------------------
 */

int 
TkpConfigureMenuEntry( 
		 TkMenuEntry *mePtr) 
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
 * --------------------------------------------------------------------------------
 * TkpMenuNewEntry –
 *
 *     Notifies the platform layer that a new menu entry was created.
 *
 * Results:
 *     Standard Tcl result.
 *
 * Side effects:
 *     None on Wayland.
 * --------------------------------------------------------------------------------
 */

int 
TkpMenuNewEntry( 
		TCL_UNUSED(TkMenuEntry *)) 
{ 
 return TCL_OK; 
}

/*
 * --------------------------------------------------------------------------------
 * TkpSetWindowMenuBar –
 *
 *     Associates a menu as the window’s menubar.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     GLFW has no native menubar; the application must draw it.
 * --------------------------------------------------------------------------------
 */

void 
TkpSetWindowMenuBar( 
		 TCL_UNUSED(Tk_Window) tkwin, 
		 TCL_UNUSED(TkMenu *) menuPtr) 
{ 
 /* 
 * In GLFW, no native menubar support. 
 * Application must handle rendering. 
 */ 
}

/* 
 *––––––––––––––––––––––––––––––––––– 
 * 
 - Geometry Calculation Functions 
 - 
 *––––––––––––––––––––––––––––––––––– 
 */

/*
 * --------------------------------------------------------------------------------
 * GetMenuIndicatorGeometry –
 *
 *     Computes the geometry of the indicator (check/radio) area.
 *
 * Results:
 *     Sets *widthPtr and *heightPtr.
 *
 * Side effects:
 *     None.
 * --------------------------------------------------------------------------------
 */

static void
GetMenuIndicatorGeometry(
			 TkMenu *menuPtr,
			 TkMenuEntry *mePtr,
			 TCL_UNUSED(Tk_Font),
			 TCL_UNUSED(const Tk_FontMetrics *),
			 int *widthPtr,
			 int *heightPtr)
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
 * --------------------------------------------------------------------------------
 * GetMenuAccelGeometry –
 *
 *     Computes the geometry for the accelerator or cascade-arrow area.
 *
 * Results:
 *     Sets *widthPtr and *heightPtr.
 *
 * Side effects:
 *     None.
 * --------------------------------------------------------------------------------
 */ 
static void 
GetMenuAccelGeometry( 
		 TkMenu *menuPtr, 
		 TkMenuEntry *mePtr, 
		 TCL_UNUSED(Tk_Font), 
		 TCL_UNUSED(const Tk_FontMetrics *), 
		 int *widthPtr, 
		 int *heightPtr) 
{ 
 NVGcontext *vg = TkGlfwGetNVGContext(); 
 double scalingLevel = 1.0; 
 *heightPtr = DEFAULT_FONT_SIZE; 
 if (mePtr->type == CASCADE_ENTRY) { 
	*widthPtr = (int)(2 * CASCADE_ARROW_WIDTH * scalingLevel); 
 } else if ((menuPtr->menuType != MENUBAR) && (mePtr->accelPtr != NULL)) { 
	const char *accel = Tcl_GetString(mePtr->accelPtr); 
	float bw[4]; 
	nvgFontSize(vg, DEFAULT_FONT_SIZE); 
	nvgFontFace(vg, DEFAULT_FONT); 
	*widthPtr = (int)nvgTextBounds(vg, 0, 0, accel, 
				 accel + mePtr->accelLength, bw); 
 } else { 
	*widthPtr = 0; 
 } 
}

/*
 * --------------------------------------------------------------------------------
 * GetMenuSeparatorGeometry –
 *
 *     Computes the geometry of a separator entry.
 *
 * Results:
 *     Sets *widthPtr and *heightPtr.
 *
 * Side effects:
 *     None.
 * --------------------------------------------------------------------------------
 */

static void 
GetMenuSeparatorGeometry( 
			 TCL_UNUSED(TkMenu *), 
			 TCL_UNUSED(TkMenuEntry *), 
			 TCL_UNUSED(Tk_Font), 
			 TCL_UNUSED(const Tk_FontMetrics *), 
			 int *widthPtr, 
			 int *heightPtr) 
{ 
 *widthPtr = 0; 
 *heightPtr = DEFAULT_FONT_SIZE; 
}

/*
 * --------------------------------------------------------------------------------
 * GetTearoffEntryGeometry –
 *
 *     Computes the geometry of a tearoff entry.
 *
 * Results:
 *     Sets *widthPtr and *heightPtr.
 *
 * Side effects:
 *     None.
 * --------------------------------------------------------------------------------
 */ 
static void 
GetTearoffEntryGeometry( 
			TkMenu *menuPtr, 
			TCL_UNUSED(TkMenuEntry *), 
			TCL_UNUSED(Tk_Font), 
			TCL_UNUSED(const Tk_FontMetrics *), 
			int *widthPtr, 
			int *heightPtr) 
{ 
 NVGcontext *vg = TkGlfwGetNVGContext(); 
 if (menuPtr->menuType != MAIN_MENU) { 
	*heightPtr = 0; 
	*widthPtr = 0; 
 } else { 
	float bw[4]; 
	*heightPtr = DEFAULT_FONT_SIZE; 
	nvgFontSize(vg, DEFAULT_FONT_SIZE); 
	nvgFontFace(vg, DEFAULT_FONT); 
	nvgTextBounds(vg, 0, 0, "W", NULL, bw); 
	*widthPtr = (int)(bw[2] - bw[0]); 
 } 
}

/*
 * --------------------------------------------------------------------------------
 * GetMenuLabelGeometry –
 *
 *     Computes the size of the label area (text/image/compound).
 *
 * Results:
 *     Sets *widthPtr and *heightPtr.
 *
 * Side effects:
 *     None.
 * --------------------------------------------------------------------------------
 */

static void 
GetMenuLabelGeometry( 
		 TkMenuEntry *mePtr, 
		 TCL_UNUSED(Tk_Font), 
		 TCL_UNUSED(const Tk_FontMetrics *), 
		 int *widthPtr, 
		 int *heightPtr) 
{ 
 NVGcontext *vg = TkGlfwGetNVGContext(); 
 int haveImage = 0; 
 int imageWidth = 0; 
 int imageHeight = 0; 
 if (mePtr->image != NULL) { 
	Tk_SizeOfImage(mePtr->image, &imageWidth, &imageHeight); 
	*widthPtr = imageWidth; 
	*heightPtr = imageHeight; 
	haveImage = 1; 
 } else if (mePtr->bitmapPtr != NULL) { 
	Pixmap bitmap = Tk_GetBitmapFromObj(mePtr->menuPtr->tkwin, 
					 mePtr->bitmapPtr); 
	Tk_SizeOfBitmap(mePtr->menuPtr->display, bitmap, 
			&imageWidth, &imageHeight); 
	haveImage = 1; 
	*widthPtr = imageWidth; 
	*heightPtr = imageHeight; 
 } else { 
	*heightPtr = 0; 
	*widthPtr = 0; 
 } 
 if (haveImage && (mePtr->compound == COMPOUND_NONE)) { 
	/* Already set above. */ 
 } else { 
	int textWidth = 0; 
	int textHeight = 0; 
	if (mePtr->labelPtr != NULL) { 
	 const char *label = Tcl_GetString(mePtr->labelPtr); 
	 float bw[4]; 
	 nvgFontSize(vg, DEFAULT_FONT_SIZE); 
	 nvgFontFace(vg, DEFAULT_FONT); 
	 textWidth = (int)nvgTextBounds(vg, 0, 0, label, 
					 label + mePtr->labelLength, bw); 
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
 *––––––––––––––––––––––––––––––––––– 
 * 
 - Drawing Functions 
 - 
 *––––––––––––––––––––––––––––––––––– 
 */

/*
 * --------------------------------------------------------------------------------
 * DrawMenuEntryBackground –
 *
 *     Draws the background and relief for a menu entry.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Renders into the NanoVG context.
 * --------------------------------------------------------------------------------
 */

static void
DrawMenuEntryBackground(
			TkMenu *menuPtr,
			TkMenuEntry *mePtr,
			TCL_UNUSED(Drawable),
			Tk_3DBorder activeBorder,
			Tk_3DBorder bgBorder,
			int x,
			int y,
			int width,
			int height)
{
    NVGcontext *vg = TkGlfwGetNVGContext();
    Simple3DBorder bord = GetSimple3DBorder(bgBorder);
    int relief = TK_RELIEF_FLAT;
    int activeBorderWidth;


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

    Tk_GetPixelsFromObj(NULL, menuPtr->tkwin,
			menuPtr->activeBorderWidthPtr, &activeBorderWidth);

    Draw3DRect(vg, (float)x, (float)y, (float)width, (float)height, 
	       activeBorderWidth, relief, bord);


}

/*
 * --------------------------------------------------------------------------------
 * DrawMenuEntryAccelerator –
 *
 *     Draws the accelerator text or the cascade arrow.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Renders into the NanoVG context.
 * --------------------------------------------------------------------------------
 */

static void 
DrawMenuEntryAccelerator( 
			 TkMenu *menuPtr, 
			 TkMenuEntry *mePtr, 
			 TCL_UNUSED(Drawable), 
			 TCL_UNUSED(GC), 
			 TCL_UNUSED(Tk_Font), 
			 TCL_UNUSED(const Tk_FontMetrics *), 
			 Tk_3DBorder activeBorder, 
			 Tk_3DBorder bgBorder, 
			 int x, 
			 int y, 
			 int width, 
			 int height, 
			 int drawArrow) 
{ 
 NVGcontext *vg = TkGlfwGetNVGContext(); 
 int borderWidth; 
 int activeBorderWidth; 
 float arrowWidth = CASCADE_ARROW_WIDTH; 
 float arrowHeight = CASCADE_ARROW_HEIGHT; 
 double scalingLevel = 1.0; 
 if (menuPtr->menuType == MENUBAR) { 
	return; 
 } 
 Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, menuPtr->borderWidthObj, 
			&borderWidth); 
 Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, menuPtr->activeBorderWidthPtr, 
			&activeBorderWidth); 
 if ((mePtr->type == CASCADE_ENTRY) && drawArrow) { 
	float px; 
	float py; 
	Simple3DBorder bord; 
	arrowWidth *= scalingLevel; 
	arrowHeight *= scalingLevel; 
	px = x + width - borderWidth - activeBorderWidth - arrowWidth; 
	py = y + (height - arrowHeight)/2; 
	nvgSave(vg); 
	nvgBeginPath(vg); 
	nvgMoveTo(vg, px, py); 
	nvgLineTo(vg, px, py + arrowHeight); 
	nvgLineTo(vg, px + arrowWidth, py + arrowHeight/2); 
	nvgClosePath(vg); 
	bord = (mePtr->state == ENTRY_ACTIVE) ? 
	 GetSimple3DBorder(activeBorder) : GetSimple3DBorder(bgBorder); 
	nvgFillColor(vg, bord.bg); 
	nvgFill(vg); 
	nvgRestore(vg); 
 } else if (mePtr->accelPtr != NULL) { 
	const char *accel = Tcl_GetString(mePtr->accelPtr); 
	int left = x + mePtr->labelWidth + activeBorderWidth 
	 + mePtr->indicatorSpace; 
	float ty; 
	NVGcolor color = nvgRGB(0,0,0); 
	if (menuPtr->menuType == MENUBAR) { 
	 left += 5; 
	} 
	ty = (float)(y + (height + DEFAULT_FONT_SIZE - (DEFAULT_FONT_SIZE/3)) / 2); 
	nvgFontSize(vg, DEFAULT_FONT_SIZE); 
	nvgFontFace(vg, DEFAULT_FONT); 
	DrawChars(vg, accel, mePtr->accelLength, (float)left, ty, color); 
 } 
}

/*
 * --------------------------------------------------------------------------------
 * DrawMenuEntryIndicator –
 *
 *     Draws checkbox/radiobutton indicators and selection state.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Renders into the NanoVG context.
 * --------------------------------------------------------------------------------
 */

static void 
DrawMenuEntryIndicator( 
		 TkMenu *menuPtr, 
		 TkMenuEntry *mePtr, 
		 TCL_UNUSED(Drawable), 
		 TCL_UNUSED(Tk_3DBorder), 
		 TCL_UNUSED(XColor *), 
		 TCL_UNUSED(XColor *), 
		 TCL_UNUSED(Tk_Font), 
		 TCL_UNUSED(const Tk_FontMetrics *), 
		 int x, 
		 int y, 
		 TCL_UNUSED(int), 
		 int height) 
{ 
 NVGcontext *vg = TkGlfwGetNVGContext(); 
 /* Draw check-button indicator. */ 
 if ((mePtr->type == CHECK_BUTTON_ENTRY) && mePtr->indicatorOn) { 
	int top; 
	int left; 
	int activeBorderWidth; 
	int disabled = (mePtr->state == ENTRY_DISABLED); 
	NVGcolor bgColor = nvgRGB(150,150,150); 
	NVGcolor indColor = nvgRGB(0,0,0); 
	NVGcolor disColor = nvgRGB(200,200,200); 
	NVGcolor selColor = nvgRGB(0,128,0); 
	float cx; 
	float cy; 
	float r; 
	Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, 
			 menuPtr->activeBorderWidthPtr, &activeBorderWidth); 
	top = y + height/2; 
	left = x + activeBorderWidth + 2 + mePtr->indicatorSpace/2; 
	cx = (float)left; 
	cy = (float)top; 
	r = (float)PTR2INT(mePtr->platformEntryData); 
	nvgSave(vg); 
	nvgBeginPath(vg); 
	nvgRect(vg, cx - r/2, cy - r/2, r, r); 
	nvgFillColor(vg, disabled ? disColor : bgColor); 
	nvgFill(vg); 
	nvgStrokeColor(vg, indColor); 
	nvgStroke(vg); 
	if (mePtr->entryFlags & ENTRY_SELECTED) { 
	 /* Draw check mark. */ 
	 nvgBeginPath(vg); 
	 nvgMoveTo(vg, cx - r/3, cy); 
	 nvgLineTo(vg, cx - r/6, cy + r/3); 
	 nvgLineTo(vg, cx + r/3, cy - r/3); 
	 nvgStrokeColor(vg, selColor); 
	 nvgStrokeWidth(vg, 2.0f); 
	 nvgStroke(vg); 
	} 
	nvgRestore(vg); 
 } 
 /* Draw radio-button indicator. */ 
 if ((mePtr->type == RADIO_BUTTON_ENTRY) && mePtr->indicatorOn) { 
	int top; 
	int left; 
	int activeBorderWidth; 
	int disabled = (mePtr->state == ENTRY_DISABLED); 
	NVGcolor bgColor = nvgRGB(150,150,150); 
	NVGcolor indColor = nvgRGB(0,0,0); 
	NVGcolor disColor = nvgRGB(200,200,200); 
	NVGcolor selColor = nvgRGB(0,128,0); 
	float cx; 
	float cy; 
	float r; 
	Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, 
			 menuPtr->activeBorderWidthPtr, &activeBorderWidth); 
	top = y + height/2; 
	left = x + activeBorderWidth + 2 + mePtr->indicatorSpace/2; 
	cx = (float)left; 
	cy = (float)top; 
	r = (float)PTR2INT(mePtr->platformEntryData) / 2; 
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
 * --------------------------------------------------------------------------------
 * DrawMenuSeparator –
 *
 *     Draws a horizontal separator line.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Renders into the NanoVG context.
 * --------------------------------------------------------------------------------
 */ 
static void 
DrawMenuSeparator( 
		 TkMenu *menuPtr, 
		 TCL_UNUSED(TkMenuEntry *), 
		 TCL_UNUSED(Drawable), 
		 TCL_UNUSED(GC), 
		 TCL_UNUSED(Tk_Font), 
		 TCL_UNUSED(const Tk_FontMetrics *), 
		 int x, 
		 int y, 
		 int width, 
		 int height) 
{ 
 NVGcontext *vg = TkGlfwGetNVGContext(); 
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
 * --------------------------------------------------------------------------------
 * DrawMenuEntryLabel –
 *
 *     Draws the label text and/or image for a menu entry.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Renders into the NanoVG context; may overlay disabled state.
 * --------------------------------------------------------------------------------
 */

static void 
DrawMenuEntryLabel( 
		 TkMenu *menuPtr, 
		 TkMenuEntry *mePtr, 
		 Drawable d, 
		 GC gc, 
		 Tk_Font tkfont, 
		 const Tk_FontMetrics *fmPtr, 
		 int x, 
		 int y, 
		 int width, 
		 int height) 
{ 
 NVGcontext *vg = TkGlfwGetNVGContext(); 
 int indicatorSpace = mePtr->indicatorSpace; 
 int activeBorderWidth; 
 int leftEdge; 
 int imageHeight = 0; 
 int imageWidth = 0; 
 int textHeight = 0; 
 int textWidth = 0; 
 int haveImage = 0; 
 int haveText = 0; 
 int imageXOffset = 0; 
 int imageYOffset = 0; 
 int textXOffset = 0; 
 int textYOffset = 0; 
 Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, menuPtr->activeBorderWidthPtr, 
			&activeBorderWidth); 
 leftEdge = x + indicatorSpace + activeBorderWidth; 
 if (menuPtr->menuType == MENUBAR) { 
	leftEdge += 5; 
 } 
 /* Determine what to draw. */ 
 if (mePtr->image != NULL) { 
	Tk_SizeOfImage(mePtr->image, &imageWidth, &imageHeight); 
	haveImage = 1; 
 } else if (mePtr->bitmapPtr != NULL) { 
	Pixmap bitmap = Tk_GetBitmapFromObj(menuPtr->tkwin, mePtr->bitmapPtr); 
	Tk_SizeOfBitmap(menuPtr->display, bitmap, &imageWidth, &imageHeight); 
	haveImage = 1; 
 } 
 if (!haveImage ||
 (mePtr->compound != COMPOUND_NONE)) { 
	if (mePtr->labelLength > 0) { 
	 const char *label = Tcl_GetString(mePtr->labelPtr); 
	 float bw[4]; 
	 nvgFontSize(vg, DEFAULT_FONT_SIZE); 
	 nvgFontFace(vg, DEFAULT_FONT); 
	 textWidth = (int)nvgTextBounds(vg, 0, 0, label, 
					 label + mePtr->labelLength, bw); 
	 textHeight = DEFAULT_FONT_SIZE; 
	 haveText = 1; 
	} 
 } 
 /* Calculate relative positions for compound display. */ 
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
 } 
 /* Draw image or bitmap. */ 
 if (mePtr->image != NULL) { 
	Tk_RedrawImage(mePtr->image, 0, 0, imageWidth, imageHeight, d, 
		 leftEdge + imageXOffset, 
		 y + (mePtr->height-imageHeight)/2 + imageYOffset); 
 } else if (mePtr->bitmapPtr != NULL) { 
	/* Draw bitmap placeholder. */ 
	nvgBeginPath(vg); 
	nvgRect(vg, leftEdge + imageXOffset, 
		y + (mePtr->height-imageHeight)/2 + imageYOffset, 
		imageWidth, imageHeight); 
	nvgFillColor(vg, nvgRGB(128,128,128)); 
	nvgFill(vg); 
 } 
 /* Draw text label. */ 
 if ((mePtr->compound != COMPOUND_NONE) ||
 !haveImage) { 
	float baseline = y + (height + DEFAULT_FONT_SIZE - (DEFAULT_FONT_SIZE/3)) / 2; 
	if (mePtr->labelLength > 0) { 
	 const char *label = Tcl_GetString(mePtr->labelPtr); 
	 NVGcolor color = nvgRGB(0,0,0); 
	 nvgFontSize(vg, DEFAULT_FONT_SIZE); 
	 nvgFontFace(vg, DEFAULT_FONT); 
	 DrawChars(vg, label, mePtr->labelLength, 
		 (float)(leftEdge + textXOffset), baseline + textYOffset, color); 
	 DrawMenuUnderline(menuPtr, mePtr, d, gc, tkfont, fmPtr, 
			 x + textXOffset, y + textYOffset, width, height); 
	} 
 } 
 /* Draw disabled overlay. */ 
 if (mePtr->state == ENTRY_DISABLED) { 
	nvgBeginPath(vg); 
	nvgRect(vg, x, y, width, height); 
	nvgFillColor(vg, nvgRGBA(200,200,200,128)); 
	nvgFill(vg); 
 } 
} 
/*
 * --------------------------------------------------------------------------------
 * DrawMenuUnderline –
 *
 *     Draws the mnemonic underline within a menu label.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Renders into the NanoVG context.
 * --------------------------------------------------------------------------------
 */

static void 
DrawMenuUnderline( 
		 TkMenu *menuPtr, 
		 TkMenuEntry *mePtr, 
		 TCL_UNUSED(Drawable), 
		 TCL_UNUSED(GC), 
		 TCL_UNUSED(Tk_Font), 
		 TCL_UNUSED(const Tk_FontMetrics *), 
		 int x, 
		 int y, 
		 TCL_UNUSED(int), 
		 int height) 
{ 
 NVGcontext *vg = TkGlfwGetNVGContext(); 
 if (mePtr->labelPtr != NULL) { 
	int len; 
	len = Tcl_GetCharLength(mePtr->labelPtr); 
	if (mePtr->underline < len && mePtr->underline >= -len) { 
	 int activeBorderWidth; 
	 int leftEdge; 
	 int ch; 
	 const char *label; 
	 const char *start; 
	 const char *end; 
	 float ty; 
	 NVGcolor color = nvgRGB(0,0,0); 
	 label = Tcl_GetString(mePtr->labelPtr); 
	 start = Tcl_UtfAtIndex(label, 
			 (mePtr->underline < 0) ? mePtr->underline + len : mePtr->underline); 
	 end = start + Tcl_UtfToUniChar(start, &ch); 
	 Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, 
				menuPtr->activeBorderWidthPtr, &activeBorderWidth); 
	 leftEdge = x + mePtr->indicatorSpace + activeBorderWidth; 
	 if (menuPtr->menuType == MENUBAR) { 
		leftEdge += 5; 
	 } 
	 ty = y + (height + DEFAULT_FONT_SIZE - (DEFAULT_FONT_SIZE/3)) / 2; 
	 nvgFontSize(vg, DEFAULT_FONT_SIZE); 
	 nvgFontFace(vg, DEFAULT_FONT); 
	 UnderlineChars(vg, label, start - label, end - label, 
			 (float)leftEdge, ty, DEFAULT_FONT_SIZE, DEFAULT_FONT_SIZE/3, color); 
	} 
 } 
}

/*
 * --------------------------------------------------------------------------------
 * DrawTearoffEntry –
 *
 *     Draws the tearoff bar for tearoff menus.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Renders into the NanoVG context.
 * --------------------------------------------------------------------------------
 */ 
static void 
DrawTearoffEntry( 
		 TkMenu *menuPtr, 
		 TCL_UNUSED(TkMenuEntry *), 
		 TCL_UNUSED(Drawable), 
		 TCL_UNUSED(GC), 
		 TCL_UNUSED(Tk_Font), 
		 TCL_UNUSED(const Tk_FontMetrics *), 
		 int x, 
		 int y, 
		 int width, 
		 int height) 
{ 
 NVGcontext *vg = TkGlfwGetNVGContext(); 
 float segmentWidth = 6.0f; 
 float maxX; 
 float px; 
 float py; 
 if (menuPtr->menuType != MAIN_MENU) { 
	return; 
 } 
 maxX = x + width - 1; 
 nvgSave(vg); 
 px = (float)x; 
 py = (float)(y + height/2); 
 while (px < maxX) { 
	float ex = px + segmentWidth; 
	if (ex > maxX) { 
	 ex = maxX; 
	} 
	nvgBeginPath(vg); 
	nvgMoveTo(vg, px, py); 
	nvgLineTo(vg, ex, py); 
	nvgStrokeWidth(vg, 1.0f); 
	nvgStrokeColor(vg, nvgRGB(100,100,100)); 
	nvgStroke(vg); 
	px += 2 * segmentWidth; 
 } 
 nvgRestore(vg); 
}

/* 
 *––––––––––––––––––––––––––––––––––– 
 * 
 - Menu Posting and Management 
 - 
 *––––––––––––––––––––––––––––––––––– 
 */ 
/*
 * --------------------------------------------------------------------------------
 * TkpPostMenu –
 *
 *     Posts a menu at the given screen location.
 *
 * Results:
 *     Standard Tcl result.
 *
 * Side effects:
 *     Delegates to the tearoff posting routine as needed.
 * --------------------------------------------------------------------------------
 */

int 
TkpPostMenu( 
	 Tcl_Interp *interp, 
	 TkMenu *menuPtr, 
	 int x, 
	 int y, 
	 Tcl_Size index) 
{ 
 return TkpPostTearoffMenu(interp, menuPtr, x, y, index); 
}

/*
 * --------------------------------------------------------------------------------
 * TkpPostTearoffMenu –
 *
 *     Posts a tearoff menu at the given screen location.
 *
 * Results:
 *     Standard Tcl result.
 *
 * Side effects:
 *     Computes position, clamps to screen, and records placement.
 * --------------------------------------------------------------------------------
 */

int 
TkpPostTearoffMenu( 
		 TCL_UNUSED(Tcl_Interp *), 
		 TkMenu *menuPtr, 
		 int x, 
		 int y, 
		 Tcl_Size index) 
{ 
 int result; 
 int reqW; 
 int reqH; 
 int screenW = 1920; 
 int screenH = 1080; 
 TkActivateMenuEntry(menuPtr, -1); 
 TkRecomputeMenu(menuPtr); 
 result = TkPostCommand(menuPtr); 
 if (result != TCL_OK) { 
	return result; 
 } 
 if (menuPtr->tkwin == NULL) { 
	return TCL_OK; 
 } 
 /* Adjust position. */ 
 if (index >= menuPtr->numEntries) { 
	index = menuPtr->numEntries - 1; 
 } 
 if (index >= 0) { 
	y -= menuPtr->entries[index]->y; 
 } 
 /* Clamp to screen. */ 
 reqW = Tk_ReqWidth(menuPtr->tkwin); 
 reqH = Tk_ReqHeight(menuPtr->tkwin); 
 if (x + reqW > screenW) { 
	x = screenW - reqW; 
 } 
 if (x < 0) { 
	x = 0; 
 } 
 if (y + reqH > screenH) { 
	y = screenH - reqH; 
 } 
 if (y < 0) { 
	y = 0; 
 } 
 /* Set position for drawing (application must handle). */ 
 return TCL_OK; 
} 
/* 
 *––––––––––––––––––––––––––––––––––– 
 * 
 - Geometry Computation 
 - 
 *––––––––––––––––––––––––––––––––––– 
 */

/*
 * --------------------------------------------------------------------------------
 * TkpComputeMenubarGeometry –
 *
 *     Computes the size and layout of a menubar.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Updates geometry fields of menu entries.
 * --------------------------------------------------------------------------------
 */ 
void 
TkpComputeMenubarGeometry( 
			 TkMenu *menuPtr) 
{ 
 Tk_Font tkfont; 
 Tk_FontMetrics menuMetrics; 
 Tk_FontMetrics entryMetrics; 
 Tk_FontMetrics *fmPtr; 
 int x; 
 int y; 
 int height; 
 int width; 
 int i; 
 int labelWidth; 
 int indicatorSpace; 
 int borderWidth; 
 int activeBorderWidth; 
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
 * --------------------------------------------------------------------------------
 * TkpComputeStandardMenuGeometry –
 *
 *     Computes the size and layout of a standard (popup) menu.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Updates geometry fields of menu entries.
 * --------------------------------------------------------------------------------
 */

void 
TkpComputeStandardMenuGeometry( 
			 TkMenu *menuPtr) 
{ 
 Tk_Font tkfont; 
 Tk_Font menuFont; 
 Tk_FontMetrics menuMetrics; 
 Tk_FontMetrics entryMetrics; 
 Tk_FontMetrics *fmPtr; 
 int x; 
 int y; 
 int height; 
 int width; 
 int indicatorSpace; 
 int labelWidth; 
 int accelWidth; 
 int borderWidth; 
 int activeBorderWidth; 
 int i; 
 int j; 
 int lastColumnBreak = 0; 
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
 *––––––––––––––––––––––––––––––––––– 
 * 
 - Menu Drawing Entry Point 
 - 
 *––––––––––––––––––––––––––––––––––– 
 */ 
/*
 * --------------------------------------------------------------------------------
 * TkpDrawMenuEntry –
 *
 *     Renders a complete menu entry.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Calls sub-drawing routines to render into the NanoVG context.
 * --------------------------------------------------------------------------------
 */

void 
TkpDrawMenuEntry( 
		 TkMenuEntry *mePtr, 
		 Drawable d, 
		 Tk_Font tkfont, 
		 const Tk_FontMetrics *menuMetricsPtr, 
		 int x, 
		 int y, 
		 int width, 
		 int height, 
		 TCL_UNUSED(int) strictMotif, 
		 int drawArrow) 
{ 
 NVGcontext *vg = TkGlfwGetNVGContext(); 
 Tk_3DBorder bgBorder = NULL; 
 Tk_3DBorder activeBorder = NULL; 
 int padY = (mePtr->menuPtr->menuType == MENUBAR) ? 3 : 0; 
 int adjustedY = y + padY; 
 int adjustedHeight = height - 2 * padY; 
 nvgFontSize(vg, DEFAULT_FONT_SIZE); 
 nvgFontFace(vg, DEFAULT_FONT); 
 DrawMenuEntryBackground(mePtr->menuPtr, mePtr, d, activeBorder, 
			 bgBorder, x, y, width, height); 
 if (mePtr->type == SEPARATOR_ENTRY) { 
	DrawMenuSeparator(mePtr->menuPtr, mePtr, d, NULL, NULL, 
			 NULL, x, adjustedY, width, adjustedHeight); 
 } else if (mePtr->type == TEAROFF_ENTRY) { 
	DrawTearoffEntry(mePtr->menuPtr, mePtr, d, NULL, NULL, NULL, 
			 x, adjustedY, width, adjustedHeight); 
 } else { 
	DrawMenuEntryLabel(mePtr->menuPtr, mePtr, d, NULL, NULL, NULL, 
			 x, adjustedY, width, adjustedHeight); 
	DrawMenuEntryAccelerator(mePtr->menuPtr, mePtr, d, NULL, NULL, NULL, 
				 activeBorder, bgBorder, x, adjustedY, width, adjustedHeight, 
				 drawArrow);	 
	if (!mePtr->hideMargin) { 
	 if (mePtr->state == ENTRY_ACTIVE) { 
		bgBorder = activeBorder; 
	 } 
	 DrawMenuEntryIndicator(mePtr->menuPtr, mePtr, d, bgBorder, NULL, 
				 NULL, NULL, NULL, x, adjustedY, width, adjustedHeight); 
	} 
 } 
}

/* 
 *––––––––––––––––––––––––––––––––––– 
 * 
 - Initialization and Utility Functions 
 - 
 *––––––––––––––––––––––––––––––––––– 
 */ 
/*
 * --------------------------------------------------------------------------------
 * SetHelpMenu –
 *
 *     Marks the Help cascade in a menubar when "useMotifHelp" is enabled.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Sets or clears the ENTRY_HELP_MENU flag.
 * --------------------------------------------------------------------------------
 */ 
static void 
SetHelpMenu( 
	 TkMenu *menuPtr) 
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
	 char *helpMenuName = (char *)ckalloc(strlen(Tk_PathName( 
									 mainMenuPtr->tkwin)) + strlen(".help") + 1); 
	 strcpy(helpMenuName, Tk_PathName(mainMenuPtr->tkwin)); 
	 strcat(helpMenuName, ".help"); 
	 if (strcmp(helpMenuName, 
		 Tk_PathName(menuPtr->mainMenuPtr->tkwin)) == 0) { 
		cascadeEntryPtr->entryFlags ||= ENTRY_HELP_MENU; 
	 } else { 
		cascadeEntryPtr->entryFlags &= ~ENTRY_HELP_MENU; 
	 } 
	 ckfree(helpMenuName); 
	} 
 } 
}

/*
 * --------------------------------------------------------------------------------
 * TkpInitializeMenuBindings –
 *
 *     Initializes platform-specific menu bindings.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     No-op on Wayland.
 * --------------------------------------------------------------------------------
 */ 
void 
TkpInitializeMenuBindings( 
			 TCL_UNUSED(Tcl_Interp *), 
			 TCL_UNUSED(Tk_BindingTable)) 
{ 
 /* Nothing to do on Wayland. */ 
} 
/*
 * --------------------------------------------------------------------------------
 * TkpMenuNotifyToplevelCreate –
 *
 *     Handles toplevel-creation notifications that affect menus.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     No-op on Wayland.
 * --------------------------------------------------------------------------------
 */ 
void 
TkpMenuNotifyToplevelCreate( 
			 TCL_UNUSED(Tcl_Interp *), 
			 TCL_UNUSED(const char *)) 
{ 
 /* Nothing to do on Wayland. */ 
} 
/*
 * --------------------------------------------------------------------------------
 * TkpMenuInit –
 *
 *     Performs platform-specific menu initialization.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Assumes NanoVG context and fonts are initialized elsewhere.
 * --------------------------------------------------------------------------------
 */ 
void 
TkpMenuInit(void) 
{ 
 /* NanoVG context and fonts assumed to be set up elsewhere. */ 
} 
/*
 * --------------------------------------------------------------------------------
 * TkpMenuThreadInit –
 *
 *     Initializes thread-specific menu state.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     No-op on Wayland.
 * --------------------------------------------------------------------------------
 */

void 
TkpMenuThreadInit(void) 
{ 
 /* Nothing to do on Wayland. */ 
} 
/*
 * --------------------------------------------------------------------------------
 * TkpDrawCheckIndicator –
 *
 *     Legacy hook to draw check indicators.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     No-op; indicators are drawn in DrawMenuEntryIndicator.
 * --------------------------------------------------------------------------------
 */

void 
TkpDrawCheckIndicator( 
		 TCL_UNUSED(Tk_Window), 
		 TCL_UNUSED(Display *), 
		 TCL_UNUSED(Drawable), 
		 TCL_UNUSED(int) x, 
		 TCL_UNUSED(int) y, 
		 TCL_UNUSED(Tk_3DBorder), 
		 TCL_UNUSED(XColor *), 
		 TCL_UNUSED(XColor *), 
		 TCL_UNUSED(XColor *), 
		 TCL_UNUSED(int) on, 
		 TCL_UNUSED(int) disabled, 
		 TCL_UNUSED(int) mode) 
{ 
 /* Already handled in DrawMenuEntryIndicator. */ 
} 
/* 
 * Local Variables: 
 * mode: c 
 * c-basic-offset: 4 
 * fill-column: 78 
 * End: 
*/
