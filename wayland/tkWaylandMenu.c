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
#include <GLFW/glfw3.h>
#include "xdg-shell-client-protocol.h"

/* The root GLFWwindow, defined in tkGlfwInit.c. */
extern GLFWwindow *mainGlfwWindow;

/* Default font definitions for NanoVG */
#ifndef DEFAULT_FONT_SIZE
#define DEFAULT_FONT_SIZE 16.0f
#endif

#ifndef DEFAULT_FONT
#define DEFAULT_FONT "sans"
#endif

/* Menu constants. */
#define MENU_MARGIN_WIDTH	2
#define MENU_DIVIDER_HEIGHT	2
#define ENTRY_HELP_MENU		ENTRY_PLATFORM_FLAG1

#ifndef WM_NEVER_MAPPED
#define WM_NEVER_MAPPED (1<<0)
#endif

/*
 * Forward declarations.
 */

static void SetHelpMenu(TkMenu *menuPtr);
static void DrawMenuEntryAccelerator(TkMenu *menuPtr,
				     TkMenuEntry *mePtr, Drawable d, GC gc,
				     Tk_Font tkfont, const Tk_FontMetrics *fmPtr,
				     Tk_3DBorder activeBorder, Tk_3DBorder bgBorder,
				     int x, int y, int width, int height, bool drawArrow);
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
static void TkpDisplayMenu(void *clientData);
static void MenuMouseClick(TkMenu *menuPtr, int x, int y, int button);
static void MenuMouseMotion(TkMenu *menuPtr, int x, int y);
static void MenuMouseLeave(TkMenu *menuPtr);
void TkWaylandSetupMenuCallbacks(Tk_Window tkwin);

/* Popup-based posting support. */
static void MenuPopupDoneCallback(void *clientData);
static void MenuDrawIntoPopup(TkMenu *menuPtr, TkWaylandPopup *popup);
static void MenuDrawMenubarIntoPopup(TkMenu *menuPtr, TkWaylandPopup *popup);

/*
 * Module-level state for the currently posted top-level menu popup.
 * Set by TkpPostMenu / TkpMenuButtonPostMenu (tkWaylandMenubu.c) and
 * cleared by MenuPopupDoneCallback.  Used by the GLFW callback wrappers
 * to translate parent-surface coordinates into popup-surface
 * coordinates and to dispatch hover/click events to the correct menu.
 */
static TkWaylandPopup *currentMenuPopup = NULL;
static TkMenu         *currentMenuPtr   = NULL;

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
TkpDestroyMenu(TCL_UNUSED(TkMenu *)) /* menuPtr */
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
TkpDestroyMenuEntry(TCL_UNUSED(TkMenuEntry *)) /* mePtr */
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
TkpMenuNewEntry(TCL_UNUSED(TkMenuEntry *)) /* mePtr */
{
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpSetWindowMenuBar --
 *
 *	Attach or detach a menubar for a toplevel.  On Wayland there is no
 *	native menubar protocol, so the menubar is rendered into a thin
 *	horizontal xdg_popup strip anchored to the top edge of the
 *	toplevel's surface.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates or destroys wmPtr->menubarPopup and renders the menubar
 *	into it.
 *
 *---------------------------------------------------------------------------
 */

void
TkpSetWindowMenuBar(
    Tk_Window tkwin,
    TkMenu   *menuPtr)
{
    TkWindow *winPtr = (TkWindow *)tkwin;
    WmInfo   *wmPtr  = (WmInfo *)winPtr->wmInfoPtr;

    if (!wmPtr) return;

    if (wmPtr->menubarPopup) {
        TkWaylandPopupDestroy(wmPtr->menubarPopup);
        wmPtr->menubarPopup = NULL;
    }

    if (!menuPtr) {
        wmPtr->menubar    = NULL;
        wmPtr->menuHeight = 0;
        return;
    }

    wmPtr->menubar = (Tk_Window)menuPtr->tkwin;

    TkRecomputeMenu(menuPtr);
    int mbH = menuPtr->totalHeight;
    if (mbH <= 0) mbH = 24;
    int mbW = Tk_Width(tkwin);
    if (mbW <= 0) mbW = Tk_ReqWidth(tkwin);
    if (mbW <= 0) mbW = 200;

    wmPtr->menuHeight = mbH;

    if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
        /*
         * Anchor rect: a 1px-tall strip across the full width at y=0 of
         * the toplevel surface.  Gravity BOTTOM_RIGHT makes the menubar
         * popup hang below that strip, i.e. occupy the top of the
         * window.
         */
        wmPtr->menubarPopup = TkWaylandPopupCreate(
            TkWaylandGetGLFWwindow(winPtr),
            0, 0,
            mbW, 1,
            mbW, mbH,
            XDG_POSITIONER_ANCHOR_BOTTOM_LEFT,
            XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT,
            0, 0);

        if (!wmPtr->menubarPopup) {
            fprintf(stderr,
                "TkpSetWindowMenuBar: failed to create menubar popup\n");
        } else {
            MenuDrawMenubarIntoPopup(menuPtr, wmPtr->menubarPopup);
            fprintf(stderr,
                "TkpSetWindowMenuBar: menubar popup %p created (%dx%d)\n",
                (void *)wmPtr->menubarPopup, mbW, mbH);
        }
    }
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
			 TCL_UNUSED(Tk_Font), /* tkfont */
			 TCL_UNUSED(const Tk_FontMetrics *), /* fmPtr */
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
		     TCL_UNUSED(const Tk_FontMetrics *), /* fmPtr */
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
			 TCL_UNUSED(TkMenu *), /* menuPtr */
			 TCL_UNUSED(TkMenuEntry *), /* mePtr */
			 Tk_Font tkfont,
			 TCL_UNUSED(const Tk_FontMetrics *), /* fmPtr */
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
			TCL_UNUSED(TkMenuEntry *), /* mePtr */
			Tk_Font tkfont,
			TCL_UNUSED(const Tk_FontMetrics *), /* fmPtr */
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
		     TCL_UNUSED(const Tk_FontMetrics *), /* fmPtr */
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
			TCL_UNUSED(GC), /* gc */
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
			 bool drawArrow)
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
	
	px = x + width - borderWidth - activeBorderWidth - CASCADE_ARROW_WIDTH; 
	py = y + (height - CASCADE_ARROW_HEIGHT)/2; 
	
	/* Draw cascade arrow as filled polygon */
	points[0].x = px;
	points[0].y = py;
	points[1].x = px;
	points[1].y = py + CASCADE_ARROW_HEIGHT;
	points[2].x = px + CASCADE_ARROW_WIDTH;
	points[2].y = py + CASCADE_ARROW_HEIGHT/2;
	
	/* Use appropriate color based on active state */
	if (mePtr->state == ENTRY_ACTIVE) {
	    /* Use active border color */
	    XSetForeground(menuPtr->display, gc, 
			   Tk_3DBorderColor(activeBorder)->pixel);
	} else {
	    /* Use normal border color */
	    XSetForeground(menuPtr->display, gc,
			   Tk_3DBorderColor(bgBorder)->pixel);
	}
	
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
		       TCL_UNUSED(Tk_3DBorder), /* border */
		       XColor *indicatorColor,
		       XColor *disableColor,
		       TCL_UNUSED(Tk_Font), /* tkfont */
		       TCL_UNUSED(const Tk_FontMetrics *), /* fmPtr */
		       int x,
		       int y,
		       TCL_UNUSED(int), /* width */
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
	
	Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, 
			    menuPtr->activeBorderWidthPtr, &activeBorderWidth); 
	top = y + height/2; 
	left = x + activeBorderWidth + 2 + mePtr->indicatorSpace/2; 
	radius = PTR2INT(mePtr->platformEntryData) / 2; 
	
	color = (mePtr->state == ENTRY_DISABLED) ? disableColor : indicatorColor;
	
	/* Draw radio circle using XDrawArc */
	XSetForeground(menuPtr->display, gc, color->pixel);
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
		  TCL_UNUSED(TkMenuEntry *), /* mePtr */
		  Drawable d,
		  GC gc,
		  TCL_UNUSED(Tk_Font), /* tkfont */
		  TCL_UNUSED(const Tk_FontMetrics *), /* fmPtr */
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
		  TCL_UNUSED(int), /* width */
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
		 TCL_UNUSED(TkMenuEntry *), /* mePtr */
		 Drawable d,
		 GC gc,
		 TCL_UNUSED(Tk_Font), /* tkfont */
		 TCL_UNUSED(const Tk_FontMetrics *), /* fmPtr */
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
 *	Post a popup menu (right-click context menu or cascade submenu) at
 *	the specified location using a grabbed xdg_popup surface.
 *
 *	Tearoff menus are not grabbed popups and continue to use
 *	TkpPostTearoffMenu.
 *
 * Results:
 *	A standard Tcl result code.
 *
 * Side effects:
 *	Creates an xdg_popup surface, renders the menu into it, and
 *	registers a dismissal callback that drives <<MenuDone>>.
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
    int result;

    if (menuPtr->menuType == TEAROFF_MENU) {
        return TkpPostTearoffMenu(interp, menuPtr, x, y, index);
    }

    TkActivateMenuEntry(menuPtr, -1);
    TkRecomputeMenu(menuPtr);

    result = TkPostCommand(menuPtr);
    if (result != TCL_OK) {
        return result;
    }
    if (!menuPtr->tkwin) {
        return TCL_OK;
    }

    if (index >= menuPtr->numEntries) {
        index = menuPtr->numEntries - 1;
    }
    if (index >= 0) {
        y -= menuPtr->entries[index]->y;
    }

    int popupW = menuPtr->totalWidth;
    int popupH = menuPtr->totalHeight;
    if (popupW <= 0) popupW = 1;
    if (popupH <= 0) popupH = 1;

    /*
     * Tear down any popup this menu window already owns (e.g. from a
     * previous post during keyboard traversal).
     */
    TkWindow *menuWin = (TkWindow *)menuPtr->tkwin;
    WmInfo   *wmPtr   = (WmInfo *)menuWin->wmInfoPtr;
    if (wmPtr && wmPtr->popup) {
        TkWaylandPopupDestroy(wmPtr->popup);
        wmPtr->popup = NULL;
    }
    if (currentMenuPtr == menuPtr) {
        currentMenuPopup = NULL;
        currentMenuPtr   = NULL;
    }

    /*
     * Use a 1x1 point anchor at (x, y) so the popup's top-left corner
     * lands exactly there (subject to compositor slide/flip if it would
     * go off-screen).
     */
    uint32_t serial   = TkWaylandPopupLastSerial();
    int      grabInput = (serial != 0) ? 1 : 0;

    TkWaylandPopup *popup = TkWaylandPopupCreate(
        mainGlfwWindow,
        x, y, 1, 1,
        popupW, popupH,
        XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT,
        XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT,
        grabInput,
        serial);

    if (!popup) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "TkpPostMenu: could not create xdg_popup surface", -1));
        return TCL_ERROR;
    }

    if (wmPtr) {
        wmPtr->popup            = popup;
        wmPtr->overrideRedirect = 1;
    }
    currentMenuPopup = popup;
    currentMenuPtr   = menuPtr;

    TkWaylandPopupSetDoneCallback(popup, MenuPopupDoneCallback, menuPtr);

    Tk_MoveResizeWindow(menuPtr->tkwin, x, y, popupW, popupH);
    menuWin->flags |= TK_MAPPED;

    MenuDrawIntoPopup(menuPtr, popup);

    fprintf(stderr, "TkpPostMenu: popup %p for %s at %d,%d size %dx%d\n",
            (void *)popup, Tk_PathName(menuPtr->tkwin),
            x, y, popupW, popupH);

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
		 Tk_Font tkfont,
		 const Tk_FontMetrics *fmPtr,
		 int x, 
		 int y, 
		 int width, 
		 int height,
		 DrawMenuFlags drawingParameters)	/* Flags */
{ 
    Tk_3DBorder bgBorder = NULL; 
    Tk_3DBorder activeBorder = NULL; 
    GC gc = NULL;  /* We'll use NanoVG directly, not X GC */
    int padY = (mePtr->menuPtr->menuType == MENUBAR) ? 3 : 0; 
    int adjustedY = y + padY; 
    int adjustedHeight = height - 2 * padY;
    
    /* Get the actual borders from the menu */
    if (mePtr->menuPtr->borderPtr != NULL) {
        bgBorder = Tk_Get3DBorderFromObj(mePtr->menuPtr->tkwin, 
                                          mePtr->menuPtr->borderPtr);
    }
    if (mePtr->menuPtr->activeBorderPtr != NULL) {
        activeBorder = Tk_Get3DBorderFromObj(mePtr->menuPtr->tkwin,
                                              mePtr->menuPtr->activeBorderPtr);
    }
    
    DrawMenuEntryBackground(mePtr->menuPtr, mePtr, d, gc, 
			    activeBorder, bgBorder, x, y, width, height); 
    
    if (mePtr->type == SEPARATOR_ENTRY) { 
	DrawMenuSeparator(mePtr->menuPtr, mePtr, d, gc, 
			  tkfont, fmPtr, x, adjustedY, width, adjustedHeight); 
    } else if (mePtr->type == TEAROFF_ENTRY) { 
	DrawTearoffEntry(mePtr->menuPtr, mePtr, d, gc, 
			 tkfont, fmPtr, x, adjustedY, width, adjustedHeight); 
    } else { 
	DrawMenuEntryLabel(mePtr->menuPtr, mePtr, d, gc, tkfont, fmPtr, 
			   x, adjustedY, width, adjustedHeight); 
	DrawMenuEntryAccelerator(mePtr->menuPtr, mePtr, d, gc, tkfont, fmPtr, 
				 activeBorder, bgBorder, x, adjustedY, width, adjustedHeight, 
				 (drawingParameters & DRAW_MENU_ENTRY_ARROW) != 0);
	if (!mePtr->hideMargin) { 
	    /* For indicator, we need proper colors - get them from the borders */
	    XColor *indicatorColor = NULL;
	    XColor *disableColor = NULL;
	    
	    DrawMenuEntryIndicator(mePtr->menuPtr, mePtr, d, gc, 
				   bgBorder, indicatorColor, disableColor,
				   tkfont, fmPtr, x, adjustedY, width, adjustedHeight); 
	} 
    } 
}


/*
 *----------------------------------------------------------------------
 *
 * MenuDrawIntoPopup --
 *
 *	Render all entries of menuPtr into the given TkWaylandPopup using
 *	the popup's own NanoVG context.  Entry coordinates (mePtr->x,
 *	mePtr->y) are already relative to the menu window's origin, which
 *	is the same as the popup surface's origin, so no translation is
 *	needed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Issues NanoVG drawing commands and swaps the popup's buffer.
 *
 *----------------------------------------------------------------------
 */

static void
MenuDrawIntoPopup(
    TkMenu         *menuPtr,
    TkWaylandPopup *popup)
{
    int i, menuW, menuH;

    if (!popup || !menuPtr) return;

    TkWaylandPopupGetSize(popup, &menuW, &menuH);

    if (TkWaylandPopupBeginDraw(popup) != TCL_OK) {
        fprintf(stderr, "MenuDrawIntoPopup: BeginDraw failed\n");
        return;
    }

    NVGcontext *vg = TkWaylandPopupGetNVGContext(popup);
    if (!vg) {
        TkWaylandPopupEndDraw(popup);
        return;
    }

    /* Background */
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, (float)menuW, (float)menuH);
    nvgFillColor(vg, nvgRGB(240, 240, 240));
    nvgFill(vg);

    /* Border */
    nvgBeginPath(vg);
    nvgRect(vg, 0.5f, 0.5f, (float)menuW - 1.0f, (float)menuH - 1.0f);
    nvgStrokeColor(vg, nvgRGB(160, 160, 160));
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    Drawable d = Tk_WindowId(menuPtr->tkwin);
    for (i = 0; i < menuPtr->numEntries; i++) {
        TkMenuEntry *mePtr = menuPtr->entries[i];
        TkpDrawMenuEntry(mePtr, d, NULL, NULL,
            mePtr->x, mePtr->y,
            mePtr->width, mePtr->height,
            DRAW_MENU_ENTRY_ARROW);
    }

    TkWaylandPopupEndDraw(popup);
}

/*
 *----------------------------------------------------------------------
 *
 * MenuDrawMenubarIntoPopup --
 *
 *	Render a menubar (horizontal top-level menu) into its strip popup.
 *	Geometry for MENUBAR-type menus is computed by
 *	TkpComputeMenubarGeometry, which lays entries out left-to-right;
 *	the rendering itself is identical to a vertical popup menu.
 *
 *----------------------------------------------------------------------
 */

static void
MenuDrawMenubarIntoPopup(
    TkMenu         *menuPtr,
    TkWaylandPopup *popup)
{
    MenuDrawIntoPopup(menuPtr, popup);
}

/*
 *----------------------------------------------------------------------
 *
 * MenuPopupDoneCallback --
 *
 *	Invoked by tkWaylandPopup.c when the compositor sends
 *	xdg_popup.popup_done (user clicked outside, pressed Escape, the
 *	popup lost the grab, etc.).
 *
 *	The popup itself is destroyed by the popup module immediately
 *	after this callback returns, so we must not call
 *	TkWaylandPopupDestroy here.  We clear our module-level pointers and
 *	post <<MenuDone>> so Tk's generic menu unposting machinery runs on
 *	the Tcl event loop.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Posts a <<MenuDone>> virtual event to the menu's Tk window.
 *
 *----------------------------------------------------------------------
 */

static void
MenuPopupDoneCallback(
    void *clientData)
{
    TkMenu *menuPtr = (TkMenu *)clientData;
    if (!menuPtr || !menuPtr->tkwin) return;

    fprintf(stderr, "MenuPopupDoneCallback: unposting %s\n",
            Tk_PathName(menuPtr->tkwin));

    TkWindow *menuWin = (TkWindow *)menuPtr->tkwin;
    WmInfo   *wmPtr   = (WmInfo *)menuWin->wmInfoPtr;
    if (wmPtr) {
        /* The popup module frees this pointer right after we return. */
        wmPtr->popup = NULL;
    }

    if (currentMenuPtr == menuPtr) {
        currentMenuPopup = NULL;
        currentMenuPtr   = NULL;
    }

    TkWaylandPostVirtualEvent(menuWin, "<<MenuDone>>");
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayMenu --
 *
 *	Called by Tk's display system (via Tcl_DoWhenIdle) to redraw a
 *	posted menu.  If the menu has a live popup surface, render into it;
 *	otherwise the menu is not currently posted and there is nothing to
 *	draw.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders menu using NanoVG into the menu's xdg_popup surface.
 *
 *----------------------------------------------------------------------
 */

static void
TkpDisplayMenu(
	       void *clientData)
{
    TkMenu   *menuPtr = (TkMenu *)clientData;
    TkWindow *winPtr;
    WmInfo   *wmPtr;

    if (!menuPtr || !menuPtr->tkwin) {
        return;
    }

    winPtr = (TkWindow *)menuPtr->tkwin;
    wmPtr  = (WmInfo *)winPtr->wmInfoPtr;

    if (!wmPtr || !wmPtr->popup) {
        /* Menu is not currently posted via a popup surface. */
        return;
    }

    MenuDrawIntoPopup(menuPtr, wmPtr->popup);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandMenuInit --
 *
 *	Initialize menu display handling.  Ensures the native popup module
 *	(tkWaylandPopup.c) has bound the Wayland globals it needs
 *	(wl_compositor, xdg_wm_base, wl_seat) before the first menu is
 *	posted.  Normally TkGlfwInitialize() already performs this; calling
 *	it again here is a harmless no-op due to the module's internal
 *	initialized flag.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May perform Wayland registry round-trips on first call.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandMenuInit(void)
{
    TkWaylandPopupInit();
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
			  TCL_UNUSED(Tcl_Interp *), /* interp */
			  TCL_UNUSED(Tk_BindingTable)) /* bindingTable */
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
			    TCL_UNUSED(Tcl_Interp *), /* interp */
			    TCL_UNUSED(const char *)) /* name */
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
		   Tcl_Size index)		/* Index of entry to position at x,y. */
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

    /*
     * Get actual screen dimensions from GLFW.  Prefer the monitor that
     * the parent toplevel is currently on, falling back to the primary
     * monitor and finally to the 1920x1080 default above.
     */
    monitor = NULL;
    if (menuPtr->tkwin) {
        TkWindow *menuWin = (TkWindow *)menuPtr->tkwin;
        TkWindow *toplevel = menuWin;
        while (toplevel->parentPtr && !Tk_IsTopLevel(toplevel)) {
            toplevel = toplevel->parentPtr;
        }
        GLFWwindow *gw = TkWaylandGetGLFWwindow(toplevel);
        if (gw) {
            monitor = glfwGetWindowMonitor(gw);
        }
    }
    if (!monitor) {
        monitor = glfwGetPrimaryMonitor();
    }
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
 *	Coordinates x, y are in the menu's popup-surface space (i.e.
 *	relative to the popup's own origin), which is the same space as
 *	mePtr->x / mePtr->y, so no translation against Tk_X/Tk_Y is needed.
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

        if (x >= mePtr->x && x < mePtr->x + mePtr->width &&
            y >= mePtr->y && y < mePtr->y + mePtr->height) {
            
            /* Skip disabled entries and separators. */
            if (mePtr->state == ENTRY_DISABLED || 
                mePtr->type == SEPARATOR_ENTRY ||
                mePtr->type == TEAROFF_ENTRY) {
                return;
            }
            
            /* Handle different entry types. */
            switch (mePtr->type) {
            case COMMAND_ENTRY:
                /* Invoke command, then unpost the popup. */
                TkInvokeMenu(menuPtr->interp, menuPtr, i);
                if (currentMenuPtr == menuPtr && currentMenuPopup) {
                    TkWaylandPopupDestroy(currentMenuPopup);
                    /* MenuPopupDoneCallback is NOT called by an
                     * explicit destroy; clear state here. */
                    if (((TkWindow *)menuPtr->tkwin)->wmInfoPtr) {
                        ((WmInfo *)((TkWindow *)menuPtr->tkwin)->wmInfoPtr)
                            ->popup = NULL;
                    }
                    currentMenuPopup = NULL;
                    currentMenuPtr   = NULL;
                    TkWaylandPostVirtualEvent((TkWindow *)menuPtr->tkwin,
                                              "<<MenuDone>>");
                }
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
                        int pw, px, py;

                        /*
                         * Position the cascade to the right of this
                         * menu's popup, aligned with the entry's y.
                         * cascadeX/Y are passed to TkpPostMenu as
                         * parent-surface-relative anchor coordinates,
                         * so convert from this popup's surface space
                         * back to parent space using the popup's
                         * compositor-confirmed position.
                         */
                        TkWaylandPopupGetSize(currentMenuPopup, &pw, NULL);
                        TkWaylandPopupGetPosition(currentMenuPopup, &px, &py);

                        cascadeX = px + pw;
                        cascadeY = py + mePtr->y;
                        
                        menuPtr->postedCascade = mePtr;
                        
                        TkPostSubmenu(menuPtr->interp, menuPtr, mePtr);
                        TkpPostMenu(menuPtr->interp, cascadePtr, 
				    cascadeX, cascadeY, 0);
                        
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
 *	Coordinates x, y are in the menu's popup-surface space, matching
 *	mePtr->x / mePtr->y directly.
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

        if (x >= mePtr->x && x < mePtr->x + mePtr->width &&
            y >= mePtr->y && y < mePtr->y + mePtr->height) {
            
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
                        int pw, px, py;

                        /*
                         * Position cascade to the right of this menu's
                         * popup, in parent-surface coordinates.
                         */
                        TkWaylandPopupGetSize(currentMenuPopup, &pw, NULL);
                        TkWaylandPopupGetPosition(currentMenuPopup, &px, &py);

                        cascadeX = px + pw;
                        cascadeY = py + mePtr->y;
                        
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
 *	Historically this registered GLFW cursor/button/enter callbacks on
 *	the menu's own GLFWwindow.  Popup menus no longer have their own
 *	GLFWwindow -- they are bare xdg_popup surfaces -- so there is
 *	nothing to register here.
 *
 *	Input on popup surfaces arrives via the parent toplevel's GLFW
 *	callbacks.  Those callbacks (registered once per toplevel by
 *	TkGlfwSetupCallbacks) detect that a menu popup is currently posted
 *	via currentMenuPopup / currentMenuPtr and forward events to
 *	TkWaylandMenuCursorPosCallback / TkWaylandMenuMouseButtonCallback /
 *	TkWaylandMenuCursorEnterCallback below, after converting parent-surface
 *	coordinates to popup-surface coordinates.
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
TkWaylandSetupMenuCallbacks(
			    TCL_UNUSED(Tk_Window))
{
    /* No-op: see comment above. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandMenuPopupActive --
 *
 *	Query whether a grabbed menu popup is currently posted.  Called
 *	from the parent toplevel's GLFW cursor/button/enter callbacks
 *	(tkGlfwInit.c / tkWaylandEvent.c) to decide whether to forward the
 *	event to the menu dispatch functions below instead of (or in
 *	addition to) normal Tk event synthesis.
 *
 * Results:
 *	Non-zero if a menu popup is posted, 0 otherwise.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandMenuPopupActive(void)
{
    return (currentMenuPtr != NULL && currentMenuPopup != NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandMenuCursorPosCallback / TkWaylandMenuMouseButtonCallback /
 * TkWaylandMenuCursorEnterCallback --
 *
 *	Dispatch entry points called from the parent toplevel's GLFW
 *	cursor/button/enter callbacks whenever TkWaylandMenuPopupActive()
 *	returns non-zero.  Parent-surface coordinates are converted to
 *	popup-surface coordinates using the popup's compositor-confirmed
 *	position before being passed to MenuMouseMotion / MenuMouseClick.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandMenuCursorPosCallback(
    GLFWwindow *glfwWindow,
    double xpos,
    double ypos)
{
    (void)glfwWindow;

    if (!currentMenuPtr || !currentMenuPopup) return;

    int px, py;
    TkWaylandPopupGetPosition(currentMenuPopup, &px, &py);

    int lx = (int)xpos - px;
    int ly = (int)ypos - py;

    MenuMouseMotion(currentMenuPtr, lx, ly);
}

MODULE_SCOPE void
TkWaylandMenuMouseButtonCallback(
    GLFWwindow *glfwWindow,
    int button,
    int action,
    int mods)
{
    (void)mods;

    if (action != GLFW_PRESS) return;
    if (!currentMenuPtr || !currentMenuPopup) return;

    double xpos, ypos;
    glfwGetCursorPos(glfwWindow, &xpos, &ypos);

    int px, py;
    TkWaylandPopupGetPosition(currentMenuPopup, &px, &py);

    int lx = (int)xpos - px;
    int ly = (int)ypos - py;

    MenuMouseClick(currentMenuPtr, lx, ly,
                   button == GLFW_MOUSE_BUTTON_LEFT ? 1 : 3);
}

MODULE_SCOPE void
TkWaylandMenuCursorEnterCallback(
    GLFWwindow *glfwWindow,
    int entered)
{
    (void)glfwWindow;

    if (!entered && currentMenuPtr) {
        MenuMouseLeave(currentMenuPtr);
    }
}


/* 
 * Local Variables: 
 * mode: c 
 * c-basic-offset: 4 
 * fill-column: 78 
 * End: 
 */
