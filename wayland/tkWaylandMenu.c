/*
 * tkWaylandMenu.c --
 *
 * This module implements the Wayland/GLFW platform-specific features of menus.
 *
 * Copyright © 1996-1998 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include <tcl.h>
#include "tkInt.h"
#include "tkGlfwInt.h"
#include "tkMenu.h"

/* Menu constants. */
#define MENU_MARGIN_WIDTH	2
#define MENU_DIVIDER_HEIGHT	2
#define ENTRY_HELP_MENU		ENTRY_PLATFORM_FLAG1

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
				    TkMenuEntry *mePtr, Drawable d, GC gc,
				    Tk_3DBorder activeBorder, Tk_3DBorder bgBorder,
				    int x, int y, int width, int height);
static void DrawMenuEntryIndicator(TkMenu *menuPtr,
				   TkMenuEntry *mePtr, Drawable d, GC gc,
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
static void TkpDisplayMenu(ClientData clientData);
static void MenuMouseClick(TkMenu *menuPtr, int x, int y, int button);
static void MenuMouseMotion(TkMenu *menuPtr, int x, int y);
static void MenuMouseLeave(TkMenu *menuPtr);
static void MenuCursorPosCallback(GLFWwindow *glfwWindow, double xpos, double ypos);
static void MenuMouseButtonCallback(GLFWwindow *glfwWindow, int button, int action, int mods);
static void MenuCursorEnterCallback(GLFWwindow *glfwWindow, int entered);
void TkWaylandSetupMenuCallbacks(Tk_Window tkwin);

/*
 *---------------------------------------------------------------------------
 *
 * TkpNewMenu --
 *
 *	Initialize a new menu for Wayland/GLFW platform.
 *
 * Results:
 *	TCL_OK always.
 *
 * Side effects:
 *	Sets up help menu if applicable.
 *
 *---------------------------------------------------------------------------
 */

int
TkpNewMenu(TkMenu *menuPtr)
{
    SetHelpMenu(menuPtr);
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpDestroyMenu --
 *
 *	Clean up platform-specific menu resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None (nothing to do on Wayland).
 *
 *---------------------------------------------------------------------------
 */

void
TkpDestroyMenu(TCL_UNUSED(TkMenu *))
{
    /* Nothing to do on Wayland. */
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpDestroyMenuEntry --
 *
 *	Clean up platform-specific menu entry resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None (nothing to do on Wayland).
 *
 *---------------------------------------------------------------------------
 */

void
TkpDestroyMenuEntry(TCL_UNUSED(TkMenuEntry *))
{
    /* Nothing to do on Wayland. */
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpConfigureMenuEntry --
 *
 *	Configure a menu entry with platform-specific settings.
 *
 * Results:
 *	TCL_OK always.
 *
 * Side effects:
 *	May set up help menu for cascade entries.
 *
 *---------------------------------------------------------------------------
 */

int
TkpConfigureMenuEntry(TkMenuEntry *mePtr)
{
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
 *---------------------------------------------------------------------------
 *
 * TkpMenuNewEntry --
 *
 *	Create a new platform-specific menu entry.
 *
 * Results:
 *	TCL_OK always.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

int
TkpMenuNewEntry(TCL_UNUSED(TkMenuEntry *))
{
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpSetWindowMenuBar --
 *
 *	Set the menubar for a window (not supported on Wayland/GLFW).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

void
TkpSetWindowMenuBar(TCL_UNUSED(Tk_Window), TCL_UNUSED(TkMenu *))
{
    /* In GLFW, no native menubar support. */
}

/*
 *---------------------------------------------------------------------------
 *
 * GetMenuIndicatorGeometry --
 *
 *	Calculate geometry for menu entry indicators (check/radio buttons).
 *
 * Results:
 *	Sets widthPtr and heightPtr to calculated dimensions.
 *
 * Side effects:
 *	Stores indicator size in platformEntryData.
 *
 *---------------------------------------------------------------------------
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

    if ((mePtr->type == CHECK_BUTTON_ENTRY) || (mePtr->type == RADIO_BUTTON_ENTRY)) {
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
 *---------------------------------------------------------------------------
 *
 * GetMenuAccelGeometry --
 *
 *	Calculate geometry for menu entry accelerator (keyboard shortcut).
 *
 * Results:
 *	Sets widthPtr and heightPtr to calculated dimensions.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static void
GetMenuAccelGeometry(
		     TkMenu *menuPtr,
		     TkMenuEntry *mePtr,
		     Tk_Font tkfont,
		     TCL_UNUSED(const Tk_FontMetrics *),
		     int *widthPtr,
		     int *heightPtr)
{
    Tk_FontMetrics fm;

    Tk_GetFontMetrics(tkfont, &fm);
    *heightPtr = fm.linespace;

    if (mePtr->type == CASCADE_ENTRY) { 
	*widthPtr = 2 * CASCADE_ARROW_WIDTH; 
    } else if ((menuPtr->menuType != MENUBAR) && (mePtr->accelPtr != NULL)) { 
	const char *accel = Tcl_GetString(mePtr->accelPtr); 
	*widthPtr = Tk_TextWidth(tkfont, accel, mePtr->accelLength); 
    } else { 
	*widthPtr = 0; 
    } 
}

/*
 *---------------------------------------------------------------------------
 *
 * GetMenuSeparatorGeometry --
 *
 *	Calculate geometry for menu separator.
 *
 * Results:
 *	Sets widthPtr and heightPtr to calculated dimensions.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static void
GetMenuSeparatorGeometry(
			 TCL_UNUSED(TkMenu *),
			 TCL_UNUSED(TkMenuEntry *),
			 Tk_Font tkfont,
			 TCL_UNUSED(const Tk_FontMetrics *),
			 int *widthPtr,
			 int *heightPtr)
{
    Tk_FontMetrics fm;

    Tk_GetFontMetrics(tkfont, &fm);
    *widthPtr = 0; 
    *heightPtr = fm.linespace; 
}

/*
 *---------------------------------------------------------------------------
 *
 * GetTearoffEntryGeometry --
 *
 *	Calculate geometry for tearoff entry.
 *
 * Results:
 *	Sets widthPtr and heightPtr to calculated dimensions.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static void
GetTearoffEntryGeometry(
			TkMenu *menuPtr,
			TCL_UNUSED(TkMenuEntry *),
			Tk_Font tkfont,
			TCL_UNUSED(const Tk_FontMetrics *),
			int *widthPtr,
			int *heightPtr)
{
    Tk_FontMetrics fm;

    if (menuPtr->menuType != MAIN_MENU) { 
	*heightPtr = 0; 
	*widthPtr = 0; 
    } else { 
	Tk_GetFontMetrics(tkfont, &fm);
	*heightPtr = fm.linespace; 
	*widthPtr = Tk_TextWidth(tkfont, "W", 1); 
    } 
}

/*
 *---------------------------------------------------------------------------
 *
 * GetMenuLabelGeometry --
 *
 *	Calculate geometry for menu entry label (text and/or image).
 *
 * Results:
 *	Sets widthPtr and heightPtr to calculated dimensions.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static void
GetMenuLabelGeometry(
		     TkMenuEntry *mePtr,
		     Tk_Font tkfont,
		     TCL_UNUSED(const Tk_FontMetrics *),
		     int *widthPtr,
		     int *heightPtr)
{
    Tk_FontMetrics fm;
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
	    Tk_GetFontMetrics(tkfont, &fm);
	    textWidth = Tk_TextWidth(tkfont, label, mePtr->labelLength); 
	    textHeight = fm.linespace; 
	    
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
	    Tk_GetFontMetrics(tkfont, &fm);
	    *heightPtr = fm.linespace; 
	} 
    } 
    *heightPtr += 1; 
}

/*
 *---------------------------------------------------------------------------
 *
 * DrawMenuEntryBackground --
 *
 *	Draw the background for a menu entry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the background rectangle with appropriate border and relief.
 *
 *---------------------------------------------------------------------------
 */

static void
DrawMenuEntryBackground(
			TkMenu *menuPtr,
			TkMenuEntry *mePtr,
			Drawable d,
			GC gc,
			Tk_3DBorder activeBorder,
			Tk_3DBorder bgBorder,
			int x,
			int y,
			int width,
			int height)
{
    Tk_3DBorder border = bgBorder;
    int relief = TK_RELIEF_FLAT;
    int activeBorderWidth;

    if (mePtr->state == ENTRY_ACTIVE) {
	border = activeBorder;

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

    /* Use Tk_Fill3DRectangle from X11 emulation layer */
    Tk_Fill3DRectangle(menuPtr->tkwin, d, border,
		       x, y, width, height, activeBorderWidth, relief);
}

/*
 *---------------------------------------------------------------------------
 *
 * DrawMenuEntryAccelerator --
 *
 *	Draw the accelerator (keyboard shortcut) or cascade arrow for a menu entry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the accelerator text or cascade arrow.
 *
 *---------------------------------------------------------------------------
 */

static void
DrawMenuEntryAccelerator(
			 TkMenu *menuPtr,
			 TkMenuEntry *mePtr,
			 Drawable d,
			 GC gc,
			 Tk_Font tkfont,
			 const Tk_FontMetrics *fmPtr,
			 Tk_3DBorder activeBorder,
			 Tk_3DBorder bgBorder,
			 int x,
			 int y,
			 int width,
			 int height,
			 int drawArrow)
{
    int borderWidth;
    int activeBorderWidth;
    XPoint points[3];

    if (menuPtr->menuType == MENUBAR) { 
	return; 
    } 

    Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, menuPtr->borderWidthObj, 
			&borderWidth); 
    Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, menuPtr->activeBorderWidthPtr, 
			&activeBorderWidth); 

    if ((mePtr->type == CASCADE_ENTRY) && drawArrow) { 
	int px, py;
	Tk_3DBorder border;
	
	px = x + width - borderWidth - activeBorderWidth - CASCADE_ARROW_WIDTH; 
	py = y + (height - CASCADE_ARROW_HEIGHT)/2; 
	
	/* Draw cascade arrow as filled polygon */
	points[0].x = px;
	points[0].y = py;
	points[1].x = px;
	points[1].y = py + CASCADE_ARROW_HEIGHT;
	points[2].x = px + CASCADE_ARROW_WIDTH;
	points[2].y = py + CASCADE_ARROW_HEIGHT/2;
	
	border = (mePtr->state == ENTRY_ACTIVE) ? activeBorder : bgBorder; 
	
	/* Use XFillPolygon from X11 emulation */
	XFillPolygon(menuPtr->display, d, gc, points, 3, Convex, CoordModeOrigin);
	
    } else if (mePtr->accelPtr != NULL) { 
	const char *accel = Tcl_GetString(mePtr->accelPtr); 
	int left = x + mePtr->labelWidth + activeBorderWidth 
	    + mePtr->indicatorSpace; 
	int ty; 
	
	if (menuPtr->menuType == MENUBAR) { 
	    left += 5; 
	} 
	
	ty = y + (height + fmPtr->ascent - fmPtr->descent) / 2; 
	
	/* Use Tk_DrawChars from X11 emulation */
	Tk_DrawChars(menuPtr->display, d, gc, tkfont, 
		     accel, mePtr->accelLength, left, ty);
    } 
}

/*
 *---------------------------------------------------------------------------
 *
 * DrawMenuEntryIndicator --
 *
 *	Draw check button or radio button indicator for a menu entry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the check/radio indicator and selection mark.
 *
 *---------------------------------------------------------------------------
 */

static void
DrawMenuEntryIndicator(
		       TkMenu *menuPtr,
		       TkMenuEntry *mePtr,
		       Drawable d,
		       GC gc,
		       TCL_UNUSED(Tk_3DBorder),
		       XColor *indicatorColor,
		       XColor *disableColor,
		       TCL_UNUSED(Tk_Font),
		       TCL_UNUSED(const Tk_FontMetrics *),
		       int x,
		       int y,
		       TCL_UNUSED(int),
		       int height)
{
    /* Draw check-button indicator. */
    if ((mePtr->type == CHECK_BUTTON_ENTRY) && mePtr->indicatorOn) {
	int top, left, size;
	int activeBorderWidth;
	XColor *color;
	XPoint check[3];

	Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, 
			    menuPtr->activeBorderWidthPtr, &activeBorderWidth); 
	top = y + height/2; 
	left = x + activeBorderWidth + 2 + mePtr->indicatorSpace/2; 
	size = PTR2INT(mePtr->platformEntryData); 
	
	color = (mePtr->state == ENTRY_DISABLED) ? disableColor : indicatorColor;
	
	/* Draw checkbox square using XDrawRectangle */
	XSetForeground(menuPtr->display, gc, color->pixel);
	XDrawRectangle(menuPtr->display, d, gc, 
		       left - size/2, top - size/2, size, size);
	
	if (mePtr->entryFlags & ENTRY_SELECTED) { 
	    /* Draw check mark using XDrawLines */
	    check[0].x = left - size/3;
	    check[0].y = top;
	    check[1].x = left - size/6;
	    check[1].y = top + size/3;
	    check[2].x = left + size/3;
	    check[2].y = top - size/3;
	    
	    XDrawLines(menuPtr->display, d, gc, check, 3, CoordModeOrigin);
	} 
    } 

    /* Draw radio-button indicator. */ 
    if ((mePtr->type == RADIO_BUTTON_ENTRY) && mePtr->indicatorOn) { 
	int top, left, radius; 
	int activeBorderWidth; 
	XColor *color;
	XArc arc;
	
	Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, 
			    menuPtr->activeBorderWidthPtr, &activeBorderWidth); 
	top = y + height/2; 
	left = x + activeBorderWidth + 2 + mePtr->indicatorSpace/2; 
	radius = PTR2INT(mePtr->platformEntryData) / 2; 
	
	color = (mePtr->state == ENTRY_DISABLED) ? disableColor : indicatorColor;
	
	/* Draw radio circle using XDrawArc */
	XSetForeground(menuPtr->display, gc, color->pixel);
	arc.x = left - radius;
	arc.y = top - radius;
	arc.width = radius * 2;
	arc.height = radius * 2;
	arc.angle1 = 0;
	arc.angle2 = 360 * 64;
	
	XDrawArc(menuPtr->display, d, gc, 
		 left - radius, top - radius, radius * 2, radius * 2,
		 0, 360 * 64);
	
	if (mePtr->entryFlags & ENTRY_SELECTED) { 
	    /* Fill inner circle using XFillArc */
	    XFillArc(menuPtr->display, d, gc, 
		     left - radius/2, top - radius/2, radius, radius,
		     0, 360 * 64);
	} 
    } 
}

/*
 *---------------------------------------------------------------------------
 *
 * DrawMenuSeparator --
 *
 *	Draw a separator line in a menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders a horizontal line.
 *
 *---------------------------------------------------------------------------
 */

static void
DrawMenuSeparator(
		  TkMenu *menuPtr,
		  TCL_UNUSED(TkMenuEntry *),
		  Drawable d,
		  GC gc,
		  TCL_UNUSED(Tk_Font),
		  TCL_UNUSED(const Tk_FontMetrics *),
		  int x,
		  int y,
		  int width,
		  int height)
{
    if (menuPtr->menuType == MENUBAR) {
	return;
    }

    /* Use XDrawLine from X11 emulation */
    XDrawLine(menuPtr->display, d, gc, 
	      x, y + height/2, 
	      x + width - 1, y + height/2);
}

/*
 *---------------------------------------------------------------------------
 *
 * DrawMenuEntryLabel --
 *
 *	Draw the label (text and/or image) for a menu entry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the label and handles compound positioning, disabled stippling.
 *
 *---------------------------------------------------------------------------
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

    if (!haveImage || (mePtr->compound != COMPOUND_NONE)) { 
	if (mePtr->labelLength > 0) { 
	    const char *label = Tcl_GetString(mePtr->labelPtr); 
	    textWidth = Tk_TextWidth(tkfont, label, mePtr->labelLength); 
	    textHeight = fmPtr->linespace; 
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

    /* Draw image using Tk_RedrawImage */
    if (mePtr->image != NULL) { 
	Tk_RedrawImage(mePtr->image, 0, 0, imageWidth, imageHeight, d, 
		       leftEdge + imageXOffset, 
		       y + (mePtr->height-imageHeight)/2 + imageYOffset); 
    } else if (mePtr->bitmapPtr != NULL) { 
	Pixmap bitmap = Tk_GetBitmapFromObj(menuPtr->tkwin, mePtr->bitmapPtr);
	/* Use XCopyPlane for bitmap */
	XCopyPlane(menuPtr->display, bitmap, d, gc,
		   0, 0, imageWidth, imageHeight,
		   leftEdge + imageXOffset,
		   y + (mePtr->height-imageHeight)/2 + imageYOffset,
		   1);
    } 

    /* Draw text label using Tk_DrawChars */
    if ((mePtr->compound != COMPOUND_NONE) || !haveImage) { 
	int baseline = y + (height + fmPtr->ascent - fmPtr->descent) / 2; 
	
	if (mePtr->labelLength > 0) { 
	    const char *label = Tcl_GetString(mePtr->labelPtr); 
	    
	    Tk_DrawChars(menuPtr->display, d, gc, tkfont, 
			 label, mePtr->labelLength, 
			 leftEdge + textXOffset, baseline + textYOffset);
	    
	    DrawMenuUnderline(menuPtr, mePtr, d, gc, tkfont, fmPtr, 
			      x + textXOffset, y + textYOffset, width, height); 
	} 
    } 

    /* Draw disabled overlay using stippling */
    if (mePtr->state == ENTRY_DISABLED) { 
	XGCValues gcValues;
	XGetGCValues(menuPtr->display, gc, GCForeground | GCBackground, &gcValues);
	XSetForeground(menuPtr->display, gc, gcValues.background);
	XSetStipple(menuPtr->display, gc, Tk_GetBitmap(NULL, menuPtr->tkwin, "gray50"));
	XSetFillStyle(menuPtr->display, gc, FillStippled);
	XFillRectangle(menuPtr->display, d, gc, x, y, width, height);
	XSetFillStyle(menuPtr->display, gc, FillSolid);
	XSetForeground(menuPtr->display, gc, gcValues.foreground);
    } 
}

/*
 *---------------------------------------------------------------------------
 *
 * DrawMenuUnderline --
 *
 *	Draw the underline for a menu entry's mnemonic character.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders a line under the specified character.
 *
 *---------------------------------------------------------------------------
 */

static void
DrawMenuUnderline(
		  TkMenu *menuPtr,
		  TkMenuEntry *mePtr,
		  Drawable d,
		  GC gc,
		  Tk_Font tkfont,
		  const Tk_FontMetrics *fmPtr,
		  int x,
		  int y,
		  TCL_UNUSED(int),
		  int height)
{
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
	    int underlineX, underlineWidth;
	    int ty; 
	    
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
	    
	    ty = y + (height + fmPtr->ascent - fmPtr->descent) / 2; 
	    
	    /* Calculate underline position using Tk_TextWidth */
	    underlineX = leftEdge + Tk_TextWidth(tkfont, label, start - label);
	    underlineWidth = Tk_TextWidth(tkfont, start, end - start);
	    
	    /* Draw underline using XDrawLine */
	    XDrawLine(menuPtr->display, d, gc,
		      underlineX, ty + 2,
		      underlineX + underlineWidth, ty + 2);
	} 
    } 
}

/*
 *---------------------------------------------------------------------------
 *
 * DrawTearoffEntry --
 *
 *	Draw the tearoff entry (dashed line) at the top of a menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders a dashed line for tearoff functionality.
 *
 *---------------------------------------------------------------------------
 */

static void
DrawTearoffEntry(
		 TkMenu *menuPtr,
		 TCL_UNUSED(TkMenuEntry *),
		 Drawable d,
		 GC gc,
		 TCL_UNUSED(Tk_Font),
		 TCL_UNUSED(const Tk_FontMetrics *),
		 int x,
		 int y,
		 int width,
		 int height)
{
    int segmentWidth = 6;
    int px;
    int py;

    if (menuPtr->menuType != MAIN_MENU) { 
	return; 
    } 

    py = y + height/2; 
    px = x;

    /* Draw dashed line using XDrawLine */
    while (px < x + width - 1) { 
	int ex = px + segmentWidth; 
	if (ex > x + width - 1) { 
	    ex = x + width - 1; 
	} 
	XDrawLine(menuPtr->display, d, gc, px, py, ex, py);
	px += 2 * segmentWidth; 
    } 
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpPostMenu --
 *
 *	Post a menu at the specified location.
 *
 * Results:
 *	Returns result of TkpPostTearoffMenu.
 *
 * Side effects:
 *	Displays the menu.
 *
 *---------------------------------------------------------------------------
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
    int borderWidth; 
    int activeBorderWidth; 
    TkMenuEntry *mePtr; 
    if (menuPtr->tkwin == NULL) { 
	return; 
    } 
    tkfont = Tk_GetFontFromObj(menuPtr->tkwin, menuPtr->fontPtr); 
    Tk_GetFontMetrics(tkfont, &menuMetrics); 
    x = y = borderWidth = activeBorderWidth = 0; 
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
		 TCL_UNUSED(Tk_Font),
		 TCL_UNUSED(const Tk_FontMetrics *), /*menuMetricsPtr */ 
		 int x, 
		 int y, 
		 int width, 
		 int height, 
		 TCL_UNUSED(int), /* strictMotif */
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
 *----------------------------------------------------------------------
 *
 * TkpDisplayMenu --
 *
 *	Called by Tk's display system to render a posted menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders menu using NanoVG.
 *
 *----------------------------------------------------------------------
 */

static void
TkpDisplayMenu(
	       ClientData clientData)
{
    TkMenu *menuPtr = (TkMenu *)clientData;
    TkWindow *winPtr;
    GLFWwindow *glfwWindow;
    Drawable drawable;
    int i;
    
    if (!menuPtr || !menuPtr->tkwin) {
        return;
    }
    
    winPtr = (TkWindow *)menuPtr->tkwin;
    
    /* Get parent window's GLFW window (menus are toplevels). */
    if (winPtr->parentPtr) {
        glfwWindow = TkGlfwGetGLFWWindow((Tk_Window)winPtr->parentPtr);
    } else {
        glfwWindow = TkGlfwGetGLFWWindow((Tk_Window)winPtr);
    }
    
    if (!glfwWindow) {
        return;
    }
    
    drawable = Tk_WindowId(menuPtr->tkwin);
    if (!drawable) {
        return;
    }
    
    TkWaylandDrawingContext dc;
    
    /* Begin NanoVG drawing using unified API. */
    if (TkGlfwBeginDraw(drawable, NULL, &dc) != TCL_OK) {
        return;
    }

    
    /* Get menu position (already computed by TkpPostMenu). */
    int menuX = Tk_X(menuPtr->tkwin);
    int menuY = Tk_Y(menuPtr->tkwin);
    int menuW = menuPtr->totalWidth;
    int menuH = menuPtr->totalHeight;
    
    /* Draw menu background/border. */
    nvgSave(dc.vg);
    nvgBeginPath(dc.vg);
    nvgRect(dc.vg, menuX, menuY, menuW, menuH);
    nvgFillColor(dc.vg, nvgRGB(240, 240, 240));
    nvgFill(dc.vg);
    nvgStrokeColor(dc.vg, nvgRGB(0, 0, 0));
    nvgStroke(dc.vg);
    nvgRestore(dc.vg);
    
    /* Draw each menu entry. */
    for (i = 0; i < menuPtr->numEntries; i++) {
        TkMenuEntry *mePtr = menuPtr->entries[i];
        
        TkpDrawMenuEntry(mePtr, drawable, NULL, NULL,
			 menuX + mePtr->x, menuY + mePtr->y,
			 mePtr->width, mePtr->height,
			 0, 1);
    }
    
    TkGlfwEndDraw(&dc);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandMenuInit --
 *
 *	Initialize menu display handling.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets up display procedure.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandMenuInit(void)
{
    /* Menu display is handled via TkpDisplayMenu which is called
     * from the generic Tk menu code. No additional setup needed. */
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
		cascadeEntryPtr->entryFlags |= ENTRY_HELP_MENU; 
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
		      TCL_UNUSED(int), /* x */ 
		      TCL_UNUSED(int), /* y */
		      TCL_UNUSED(Tk_3DBorder), 
		      TCL_UNUSED(XColor *), 
		      TCL_UNUSED(XColor *), 
		      TCL_UNUSED(XColor *), 
		      TCL_UNUSED(int), /* on */ 
		      TCL_UNUSED(int), /* disabled */
		      TCL_UNUSED(int)) /* mode */ 
{ 
    /* Already handled in DrawMenuEntryIndicator. */ 
}

/*
 *----------------------------------------------------------------------
 *
 * TkpPostTearoffMenu --
 *
 *	Posts a tearoff menu on the screen at the specified coordinates.
 *	This is the platform-specific implementation for Wayland/GLFW.
 *
 * Results:
 *	Returns a standard Tcl result code. TCL_OK if the menu was
 *	successfully posted, TCL_ERROR otherwise.
 *
 * Side effects:
 *	The menu window is mapped at the computed screen position.
 *	Menu entries are recomputed for proper layout. Mouse event
 *	callbacks are registered for the menu window. The menu is
 *	scheduled for display via the NanoVG rendering system.
 *
 *----------------------------------------------------------------------
 */

int
TkpPostTearoffMenu(
		   TCL_UNUSED(Tcl_Interp *),	/* Interpreter for error reporting. */
		   TkMenu *menuPtr,		/* The menu to post. */
		   int x,			/* Screen X coordinate. */
		   int y,			/* Screen Y coordinate. */
		   Tcl_Size index)		/* Index of entry to position at x,y.
						 * Use -1 to position menu's top-left
						 * corner at x,y. */
{
    int result;
    int reqW, reqH;
    GLFWmonitor *monitor;
    const GLFWvidmode *mode;
    int screenW = 1920;  /* Fallback values. */
    int screenH = 1080;
    
    /* Deactivate any currently active entry. */
    TkActivateMenuEntry(menuPtr, -1);
    
    /* Recompute menu geometry. */
    TkRecomputeMenu(menuPtr);
    
    /* Execute the post command if one exists. */
    result = TkPostCommand(menuPtr);
    if (result != TCL_OK) {
        return result;
    }

    /* Menu window must exist. */
    if (menuPtr->tkwin == NULL) {
        return TCL_OK;
    }

    /* Adjust y coordinate if posting relative to a specific entry. */
    if (index >= menuPtr->numEntries) {
        index = menuPtr->numEntries - 1;
    }
    if (index >= 0) {
        y -= menuPtr->entries[index]->y;
    }

    /* Get actual screen dimensions from GLFW. */
    monitor = glfwGetPrimaryMonitor();
    if (monitor) {
        mode = glfwGetVideoMode(monitor);
        if (mode) {
            screenW = mode->width;
            screenH = mode->height;
        }
    }

    /* Get menu's requested size. */
    reqW = Tk_ReqWidth(menuPtr->tkwin);
    reqH = Tk_ReqHeight(menuPtr->tkwin);

    /* Clamp menu position to screen boundaries. */
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

    /* Position and size the menu window. */
    Tk_MoveWindow(menuPtr->tkwin, x, y);
    Tk_ResizeWindow(menuPtr->tkwin, reqW, reqH);
    
    /* Map the window to make it visible. */
    Tk_MapWindow(menuPtr->tkwin);
    
    /* Set up mouse event callbacks for menu interaction. */
    TkWaylandSetupMenuCallbacks(menuPtr->tkwin);
    
    /* Schedule the menu for display via NanoVG. */
    Tcl_DoWhenIdle((Tcl_IdleProc *)TkpDisplayMenu, menuPtr);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * MenuMouseClick --
 *
 *	Handle mouse click events in a menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May invoke menu entry, post cascade, or toggle check/radio.
 *
 *----------------------------------------------------------------------
 */

static void
MenuMouseClick(
	       TkMenu *menuPtr,
	       int x,
	       int y,
	       int button)
{
    int i;
    
    if (button != 1) {
        return;  /* Only handle left-click. */
    }
    
    /* Find which entry was clicked. */
    for (i = 0; i < menuPtr->numEntries; i++) {
        TkMenuEntry *mePtr = menuPtr->entries[i];
        int entryX = Tk_X(menuPtr->tkwin) + mePtr->x;
        int entryY = Tk_Y(menuPtr->tkwin) + mePtr->y;
        
        if (x >= entryX && x < entryX + mePtr->width &&
            y >= entryY && y < entryY + mePtr->height) {
            
            /* Skip disabled entries and separators. */
            if (mePtr->state == ENTRY_DISABLED || 
                mePtr->type == SEPARATOR_ENTRY ||
                mePtr->type == TEAROFF_ENTRY) {
                return;
            }
            
            /* Handle different entry types. */
            switch (mePtr->type) {
            case COMMAND_ENTRY:
                /* Invoke command and unpost menu. */
                TkInvokeMenu(menuPtr->interp, menuPtr, i);
                TkPostTearoffMenu(menuPtr->interp, menuPtr, 0, 0);
                break;
                
            case CASCADE_ENTRY:
                /* Post the cascade menu. */
                if (mePtr->namePtr != NULL) {
                    TkMenuReferences *menuRefPtr;
                    int cascadeX, cascadeY;
                    
                    menuRefPtr = TkFindMenuReferencesObj(
							 menuPtr->interp, mePtr->namePtr);
                    
                    if (menuRefPtr && menuRefPtr->menuPtr) {
                        TkMenu *cascadePtr = menuRefPtr->menuPtr;
                        
                        /* Calculate cascade position.
                         * Position to the right of the parent entry. */
                        cascadeX = Tk_X(menuPtr->tkwin) + 
			    Tk_Width(menuPtr->tkwin);
                        cascadeY = Tk_Y(menuPtr->tkwin) + mePtr->y;
                        
                        /* Mark this as the posted cascade. */
                        menuPtr->postedCascade = mePtr;
                        
                        /* Post the cascade menu. */
                        TkPostSubmenu(menuPtr->interp, menuPtr, mePtr);
                        TkpPostMenu(menuPtr->interp, cascadePtr, 
				    cascadeX, cascadeY, 0);
                        
                        /* Redraw parent to show active state. */
                        TkEventuallyRedrawMenu(menuPtr, NULL);
                    }
                }
                break;
                
            case CHECK_BUTTON_ENTRY:
                /* Toggle check state. */
                if (mePtr->entryFlags & ENTRY_SELECTED) {
                    mePtr->entryFlags &= ~ENTRY_SELECTED;
                } else {
                    mePtr->entryFlags |= ENTRY_SELECTED;
                }
                
                /* Invoke the entry to run its command. */
                TkInvokeMenu(menuPtr->interp, menuPtr, i);
                
                /* Redraw to show new state. */
                TkEventuallyRedrawMenu(menuPtr, NULL);
                break;
                
            case RADIO_BUTTON_ENTRY:
                /* Select this radio button, deselect others. */
                if (!(mePtr->entryFlags & ENTRY_SELECTED)) {
                    /* Deselect all other radio buttons with same variable. */
                    if (mePtr->namePtr != NULL) {
                        int j;
                        for (j = 0; j < menuPtr->numEntries; j++) {
                            TkMenuEntry *otherPtr = menuPtr->entries[j];
                            if (otherPtr->type == RADIO_BUTTON_ENTRY &&
                                otherPtr->namePtr != NULL &&
                                strcmp(Tcl_GetString(otherPtr->namePtr),
				       Tcl_GetString(mePtr->namePtr)) == 0) {
                                otherPtr->entryFlags &= ~ENTRY_SELECTED;
                            }
                        }
                    }
                    
                    /* Select this one. */
                    mePtr->entryFlags |= ENTRY_SELECTED;
                    
                    /* Invoke the entry. */
                    TkInvokeMenu(menuPtr->interp, menuPtr, i);
                    
                    /* Redraw to show new state. */
                    TkEventuallyRedrawMenu(menuPtr, NULL);
                }
                break;
            }
            
            return;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MenuMouseMotion --
 *
 *	Handle mouse motion in a menu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May activate/deactivate entries, post/unpost cascades.
 *
 *----------------------------------------------------------------------
 */

static void
MenuMouseMotion(
		TkMenu *menuPtr,
		int x,
		int y)
{
    int i;
    int foundEntry = 0;
    
    /* Find which entry the mouse is over. */
    for (i = 0; i < menuPtr->numEntries; i++) {
        TkMenuEntry *mePtr = menuPtr->entries[i];
        int entryX = Tk_X(menuPtr->tkwin) + mePtr->x;
        int entryY = Tk_Y(menuPtr->tkwin) + mePtr->y;
        
        if (x >= entryX && x < entryX + mePtr->width &&
            y >= entryY && y < entryY + mePtr->height) {
            
            foundEntry = 1;
            
            /* Skip disabled entries and separators. */
            if (mePtr->state == ENTRY_DISABLED ||
                mePtr->type == SEPARATOR_ENTRY ||
                mePtr->type == TEAROFF_ENTRY) {
                continue;
            }
            
            /* Activate this entry if not already active. */
            if (menuPtr->active != i) {
				
                /* Unpost any existing cascade if moving to different entry. */
                if (menuPtr->postedCascade != NULL && 
                    menuPtr->postedCascade != mePtr) {
		    TkPostSubmenu(menuPtr->interp, menuPtr, NULL);
                    menuPtr->postedCascade = NULL;
                }
                
                TkActivateMenuEntry(menuPtr, i);
                
                /* Auto-post cascade on hover. */
                if (mePtr->type == CASCADE_ENTRY && mePtr->namePtr != NULL) {
                    TkMenuReferences *menuRefPtr;
                    int cascadeX, cascadeY;
                    
                    menuRefPtr = TkFindMenuReferencesObj(
							 menuPtr->interp, mePtr->namePtr);
                    
                    if (menuRefPtr && menuRefPtr->menuPtr) {
                        TkMenu *cascadePtr = menuRefPtr->menuPtr;
                        
                        /* Position cascade to the right. */
                        cascadeX = Tk_X(menuPtr->tkwin) + 
			    Tk_Width(menuPtr->tkwin);
                        cascadeY = Tk_Y(menuPtr->tkwin) + mePtr->y;
                        
                        menuPtr->postedCascade = mePtr;
                        
                        TkPostSubmenu(menuPtr->interp, menuPtr, mePtr);
                        TkpPostMenu(menuPtr->interp, cascadePtr,
				    cascadeX, cascadeY, 0);
                    }
                }
                
                TkEventuallyRedrawMenu(menuPtr, NULL);
            }
            return;
        }
    }
    
    /* Mouse not over any entry. */
    if (!foundEntry && menuPtr->active != -1) {
        TkActivateMenuEntry(menuPtr, -1);
        TkEventuallyRedrawMenu(menuPtr, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MenuMouseLeave --
 *
 *	Handle mouse leaving a menu window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deactivates current entry, may unpost cascade.
 *
 *----------------------------------------------------------------------
 */

static void
MenuMouseLeave(
	       TkMenu *menuPtr)
{
    /* Only deactivate if not moving into a cascade. */
    if (menuPtr->postedCascade == NULL) {
        if (menuPtr->active != -1) {
            TkActivateMenuEntry(menuPtr, -1);
            TkEventuallyRedrawMenu(menuPtr, NULL);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSetupMenuCallbacks --
 *
 *	Register mouse event callbacks for a menu window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets up GLFW callbacks for the menu window.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandSetupMenuCallbacks(
			    Tk_Window tkwin)
{
    GLFWwindow *glfwWindow;
    
    glfwWindow = TkGlfwGetGLFWWindow(tkwin);
    if (!glfwWindow) {
        return;
    }
    
    /* Store tkwin in GLFW window user pointer for callbacks. */
    glfwSetWindowUserPointer(glfwWindow, tkwin);
    
    /* Set up mouse callbacks. */
    glfwSetCursorPosCallback(glfwWindow, MenuCursorPosCallback);
    glfwSetMouseButtonCallback(glfwWindow, MenuMouseButtonCallback);
    glfwSetCursorEnterCallback(glfwWindow, MenuCursorEnterCallback);
}

/*
 *----------------------------------------------------------------------
 *
 * GLFW Callback Wrappers --
 *
 *	These translate GLFW events to menu operations.
 *
 *----------------------------------------------------------------------
 */

static void
MenuCursorPosCallback(
		      GLFWwindow *glfwWindow,
		      double xpos,
		      double ypos)
{
    Tk_Window tkwin = (Tk_Window)glfwGetWindowUserPointer(glfwWindow);
    TkWindow *winPtr = (TkWindow *)tkwin;
    
    if (winPtr && winPtr->instanceData) {
        TkMenu *menuPtr = (TkMenu *)winPtr->instanceData;
        MenuMouseMotion(menuPtr, (int)xpos, (int)ypos);
    }
}

static void
MenuMouseButtonCallback(
			GLFWwindow *glfwWindow,
			int button,
			int action,
			TCL_UNUSED(int)) /* mods */
{
    Tk_Window tkwin = (Tk_Window)glfwGetWindowUserPointer(glfwWindow);
    TkWindow *winPtr = (TkWindow *)tkwin;
    
    if (action == GLFW_PRESS && winPtr && winPtr->instanceData) {
        TkMenu *menuPtr = (TkMenu *)winPtr->instanceData;
        double xpos, ypos;
        
        glfwGetCursorPos(glfwWindow, &xpos, &ypos);
        MenuMouseClick(menuPtr, (int)xpos, (int)ypos, 
		       button == GLFW_MOUSE_BUTTON_LEFT ? 1 : 3);
    }
}

static void
MenuCursorEnterCallback(
			GLFWwindow *glfwWindow,
			int entered)
{
    Tk_Window tkwin = (Tk_Window)glfwGetWindowUserPointer(glfwWindow);
    TkWindow *winPtr = (TkWindow *)tkwin;
    
    if (!entered && winPtr && winPtr->instanceData) {
        TkMenu *menuPtr = (TkMenu *)winPtr->instanceData;
        MenuMouseLeave(menuPtr);
    }
}


/* 
 * Local Variables: 
 * mode: c 
 * c-basic-offset: 4 
 * fill-column: 78 
 * End: 
 */
