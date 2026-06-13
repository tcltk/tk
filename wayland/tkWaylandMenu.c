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
#include <wayland-client.h>
#include <stdbool.h>

/*
 * wl_pointer.button reports Linux evdev button codes.  Avoid requiring
 * <linux/input-event-codes.h> for just this one constant.
 */
#ifndef BTN_LEFT
#define BTN_LEFT 0x110
#endif

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

/* WmInfo flag from tkUnixWm.c */
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
static void MenuDrawIntoPopup(TkMenu *menuPtr, TkWaylandPopup *popup);
static void MenuDrawMenubarIntoPopup(TkMenu *menuPtr, TkWaylandPopup *popup);
static void PlaceMenuPopup(int anchorX, int anchorY, int anchorW, int anchorH,
			    int popupW, int popupH, int *xOut, int *yOut);
static void MenuStackPop(int toDepth);

/*
 *----------------------------------------------------------------------
 *
 * Menu popup stack
 *
 *	Each posted menu (the root dropdown plus any open cascades) occupies
 *	one slot.  All popups are wl_subsurfaces parented directly to the
 *	root toplevel's wl_surface (TkWaylandSubsurfaceCreate), with empty
 *	input regions, so the rect stored here is in toplevel-surface-local
 *	coordinates -- the same space as lastPointerX/Y in tkGlfwInit.c and
 *	as GLFW's own per-toplevel callbacks.
 *
 *	Slot 0 is always the root menu (posted via TkpPostMenu /
 *	TkpMenuButtonPostMenu).  Slots 1..depth-1 are cascades, each one
 *	level deeper than its predecessor.  TkWaylandMenuHandlePointerMotion
 *	/ HandlePointerButton hit-test from the top of the stack down so
 *	that overlapping cascade regions resolve to the topmost (innermost)
 *	menu.
 *
 *----------------------------------------------------------------------
 */

#define TK_WAYLAND_MENU_STACK_MAX 16

typedef struct {
    TkMenu         *menuPtr;
    TkWaylandPopup *popup;
    int x, y, w, h;   /* toplevel-surface-local rect */
} MenuStackEntry;

static MenuStackEntry menuStack[TK_WAYLAND_MENU_STACK_MAX];
static int            menuStackDepth = 0;

/*
 * Set by TkWaylandMenuHandlePointerButton when an outside click dismisses
 * the stack, so the (unrelated) GLFW button callback for the same press
 * can choose to swallow the click rather than also activating whatever
 * widget is underneath.  Consumed via TkWaylandMenuConsumeDismissClick.
 */
static int menuDismissedByClick = 0;

/*
 *---------------------------------------------------------------------------
 *
 * MenuStackFindLevel --
 *
 *	Return the stack index of menuPtr, or -1 if it is not currently
 *	posted.
 *
 *---------------------------------------------------------------------------
 */

static int
MenuStackFindLevel(
    TkMenu *menuPtr)
{
    int i;
    for (i = 0; i < menuStackDepth; i++) {
        if (menuStack[i].menuPtr == menuPtr) return i;
    }
    return -1;
}

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
    if (mbH < 20) mbH = 24;
    int mbW = Tk_Width(tkwin);
    if (mbW <= 0) mbW = Tk_ReqWidth(tkwin);
    if (mbW <= 0) mbW = 200;

    wmPtr->menuHeight = mbH;

    if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
        /*
         * The menubar is a permanent part of the toplevel, not a
         * transient compositor-managed surface, so use a wl_subsurface
         * (no xdg_surface/configure handshake required) anchored to the
         * top-left corner of the toplevel at (0,0).
         */
        wmPtr->menubarPopup = TkWaylandSubsurfaceCreate(
            TkWaylandGetGLFWwindow(winPtr),
            0, 0, mbW, mbH);

        if (!wmPtr->menubarPopup) {
            fprintf(stderr,
                "TkpSetWindowMenuBar: failed to create menubar subsurface\n");
        } else {
            MenuDrawMenubarIntoPopup(menuPtr, wmPtr->menubarPopup);
            fprintf(stderr,
                "TkpSetWindowMenuBar: menubar subsurface %p (%dx%d)\n",
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
 *	Post a popup menu (right-click context menu) at the specified
 *	location as the root of a new menu stack.
 *
 *	Tearoff menus are not part of the menu stack and continue to use
 *	TkpPostTearoffMenu.
 *
 * Results:
 *	A standard Tcl result code.
 *
 * Side effects:
 *	Dismisses any previously posted menu stack, then posts menuPtr as
 *	the new root via TkWaylandPostMenuAtAnchor (see below).
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
     * A right-click context menu has a 1x1 "point" anchor at (x, y) with
     * zero size; PlaceMenuPopup will open the menu down-right from that
     * point by default, flipping up/left if it would not fit within the
     * toplevel.
     */
    return TkWaylandPostMenuAtAnchor(interp, menuPtr,
        x, y, 0, 0, popupW, popupH, /*isRoot=*/1);
}

/*
 *---------------------------------------------------------------------------
 *
 * PlaceMenuPopup --
 *
 *	Compute the top-left corner of a popupW x popupH menu so that it
 *	opens below-and-right of the given anchor rectangle, flipping
 *	above/left if it would otherwise extend past the right or bottom
 *	edge of the root toplevel's current size.
 *
 *	Coordinates are all in toplevel-surface-local logical pixels.
 *
 *	Caveat: Wayland deliberately does not expose a window's position on
 *	the physical screen, so this can only avoid overflowing the
 *	toplevel's own bounds, not the monitor's.  For windows positioned
 *	away from a screen edge this is sufficient; for a window whose edge
 *	coincides with a screen edge, a large menu may still be clipped by
 *	the compositor at the screen boundary (which is a graceful, if
 *	imperfect, fallback).
 *
 *---------------------------------------------------------------------------
 */

static void
PlaceMenuPopup(
    int anchorX, int anchorY, int anchorW, int anchorH,
    int popupW, int popupH,
    int *xOut, int *yOut)
{
    int toplevelW = 0, toplevelH = 0;

    TkWindow *toplevel = TkGlfwGetTkWindow(mainGlfwWindow);
    if (toplevel) {
        toplevelW = Tk_Width((Tk_Window)toplevel);
        toplevelH = Tk_Height((Tk_Window)toplevel);
    }
    if (toplevelW <= 0) toplevelW = 200;
    if (toplevelH <= 0) toplevelH = 200;

    int x = anchorX;
    int y = anchorY + anchorH;

    if (y + popupH > toplevelH) {
        /* Flip above the anchor. */
        int flippedY = anchorY - popupH;
        if (flippedY >= 0) {
            y = flippedY;
        }
        /* else: leave below; will be clamped/possibly clipped. */
    }

    if (x + popupW > toplevelW) {
        /* Flip so the popup's right edge aligns with the anchor's
         * right edge (or the anchor point itself for a point anchor). */
        int flippedX = anchorX + anchorW - popupW;
        if (flippedX >= 0) {
            x = flippedX;
        }
    }

    if (x < 0) x = 0;
    if (y < 0) y = 0;

    *xOut = x;
    *yOut = y;
}

/*
 *---------------------------------------------------------------------------
 *
 * MenuStackPop --
 *
 *	Destroy and remove all menu stack entries from index toDepth onward
 *	(i.e. keep entries [0, toDepth)).  Used both to close stale cascades
 *	when the hover moves to a different entry, and as part of
 *	TkWaylandMenuDismissAll (toDepth == 0).
 *
 *---------------------------------------------------------------------------
 */

static void
MenuStackPop(
    int toDepth)
{
    while (menuStackDepth > toDepth) {
        menuStackDepth--;
        MenuStackEntry *entry = &menuStack[menuStackDepth];

        if (entry->menuPtr && entry->menuPtr->postedCascade) {
            TkPostSubmenu(entry->menuPtr->interp, entry->menuPtr, NULL);
            entry->menuPtr->postedCascade = NULL;
        }

        if (entry->popup) {
            TkWaylandPopupDestroy(entry->popup);
        }
        if (entry->menuPtr && entry->menuPtr->tkwin) {
            TkWindow *win = (TkWindow *)entry->menuPtr->tkwin;
            if (win->wmInfoPtr) {
                ((WmInfo *)win->wmInfoPtr)->popup = NULL;
            }
            win->flags &= ~TK_MAPPED;
        }

        memset(entry, 0, sizeof(*entry));
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandMenuDismissAll --
 *
 *	Tear down the entire menu stack and post <<MenuDone>> on the root
 *	menu's window so Tk's generic unposting machinery (focus
 *	restoration, -postcommand cleanup, etc.) runs on the Tcl event loop.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandMenuDismissAll(void)
{
    if (menuStackDepth == 0) return;

    TkMenu *rootMenuPtr = menuStack[0].menuPtr;
    TkWindow *rootWin = rootMenuPtr ? (TkWindow *)rootMenuPtr->tkwin : NULL;

    MenuStackPop(0);

    if (rootWin) {
        TkWaylandPostVirtualEvent(rootWin, "<<MenuDone>>");
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandPostMenuAtAnchor --
 *
 *	Core menu posting routine, shared by TkpPostMenu (right-click
 *	context menus), TkpMenuButtonPostMenu (menubutton dropdowns), and
 *	the cascade-posting code in MenuMouseClick / MenuMouseMotion.
 *
 *	anchorX/Y/W/H describe the rectangle the menu should open relative
 *	to, in toplevel-surface-local coordinates (for a point anchor, pass
 *	anchorW = anchorH = 0).  PlaceMenuPopup computes the final top-left
 *	corner, flipping as needed to stay within the toplevel.
 *
 *	If isRoot is non-zero, any existing menu stack is dismissed first
 *	and this menu becomes stack[0].  Otherwise it is pushed as the next
 *	cascade level (stack entries beyond the current top are popped
 *	first) and placed above its parent in the surface stack.
 *
 * Results:
 *	A standard Tcl result code.
 *
 * Side effects:
 *	Creates a subsurface popup, renders the menu into it, and pushes a
 *	new entry onto menuStack.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandPostMenuAtAnchor(
    Tcl_Interp *interp,
    TkMenu     *menuPtr,
    int anchorX, int anchorY, int anchorW, int anchorH,
    int popupW, int popupH,
    int isRoot)
{
    if (popupW <= 0) popupW = 1;
    if (popupH <= 0) popupH = 1;

    if (isRoot) {
        TkWaylandMenuDismissAll();
    }
    /*
     * For a cascade (isRoot == 0), the caller is responsible for first
     * calling MenuStackPop(parentLevel + 1) to close any stale deeper
     * cascades, so that menuStackDepth == parentLevel + 1 on entry here
     * and the new entry is pushed at index parentLevel + 1.
     */

    if (menuStackDepth >= TK_WAYLAND_MENU_STACK_MAX) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "TkWaylandPostMenuAtAnchor: menu stack overflow", -1));
        return TCL_ERROR;
    }

    int x, y;
    PlaceMenuPopup(anchorX, anchorY, anchorW, anchorH, popupW, popupH, &x, &y);

    TkWaylandPopup *popup = TkWaylandSubsurfaceCreate(
        mainGlfwWindow, x, y, popupW, popupH);

    if (!popup) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "TkWaylandPostMenuAtAnchor: could not create subsurface", -1));
        return TCL_ERROR;
    }

    if (!isRoot && menuStackDepth > 0) {
        TkWaylandSubsurfacePlaceAbove(popup, menuStack[menuStackDepth - 1].popup);
    }

    TkWindow *menuWin = (TkWindow *)menuPtr->tkwin;
    WmInfo   *wmPtr   = (WmInfo *)menuWin->wmInfoPtr;
    if (wmPtr) {
        wmPtr->popup            = popup;
        wmPtr->overrideRedirect = 1;
    }

    Tk_MoveResizeWindow(menuPtr->tkwin, x, y, popupW, popupH);
    menuWin->flags |= TK_MAPPED;

    MenuStackEntry *entry = &menuStack[menuStackDepth++];
    entry->menuPtr = menuPtr;
    entry->popup   = popup;
    entry->x = x;
    entry->y = y;
    entry->w = popupW;
    entry->h = popupH;

    MenuDrawIntoPopup(menuPtr, popup);

    fprintf(stderr,
        "TkWaylandPostMenuAtAnchor: %s subsurface %p at %d,%d size %dx%d "
        "(depth=%d)\n",
        isRoot ? "root" : "cascade",
        (void *)popup, x, y, popupW, popupH, menuStackDepth);

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
                /* Invoke command, then dismiss the entire menu stack. */
                TkInvokeMenu(menuPtr->interp, menuPtr, i);
                TkWaylandMenuDismissAll();
                break;

            case CASCADE_ENTRY:
                /* Post the cascade menu. */
                if (mePtr->namePtr != NULL) {
                    TkMenuReferences *menuRefPtr;
                    int cascadeAnchorX, cascadeAnchorY;
                    int cascadeW, cascadeH;
                    int level = MenuStackFindLevel(menuPtr);

                    menuRefPtr = TkFindMenuReferencesObj(
							 menuPtr->interp, mePtr->namePtr);

                    if (level >= 0 && menuRefPtr && menuRefPtr->menuPtr) {
                        TkMenu *cascadePtr = menuRefPtr->menuPtr;

                        /*
                         * Close any deeper cascades first, then anchor
                         * the new cascade to the right edge of this
                         * menu's popup, top-aligned with the clicked
                         * entry.  All coordinates are toplevel-surface-
                         * local, taken directly from the stack entry.
                         */
                        MenuStackPop(level + 1);

                        cascadeAnchorX = menuStack[level].x + menuStack[level].w;
                        cascadeAnchorY = menuStack[level].y + mePtr->y;

                        TkRecomputeMenu(cascadePtr);
                        cascadeW = cascadePtr->totalWidth;
                        cascadeH = cascadePtr->totalHeight;

                        menuPtr->postedCascade = mePtr;
                        TkPostSubmenu(menuPtr->interp, menuPtr, mePtr);

                        TkWaylandPostMenuAtAnchor(menuPtr->interp, cascadePtr,
                            cascadeAnchorX, cascadeAnchorY, 0, 0,
                            cascadeW, cascadeH, /*isRoot=*/0);

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

                    int level = MenuStackFindLevel(menuPtr);
                    if (level >= 0) {
                        MenuStackPop(level + 1);
                    }
                }

                TkActivateMenuEntry(menuPtr, i);

                /* Auto-post cascade on hover. */
                if (mePtr->type == CASCADE_ENTRY && mePtr->namePtr != NULL) {
                    TkMenuReferences *menuRefPtr;
                    int cascadeAnchorX, cascadeAnchorY;
                    int cascadeW, cascadeH;
                    int level = MenuStackFindLevel(menuPtr);

                    menuRefPtr = TkFindMenuReferencesObj(
							 menuPtr->interp, mePtr->namePtr);

                    if (level >= 0 && menuRefPtr && menuRefPtr->menuPtr) {
                        TkMenu *cascadePtr = menuRefPtr->menuPtr;

                        MenuStackPop(level + 1);

                        cascadeAnchorX = menuStack[level].x + menuStack[level].w;
                        cascadeAnchorY = menuStack[level].y + mePtr->y;

                        TkRecomputeMenu(cascadePtr);
                        cascadeW = cascadePtr->totalWidth;
                        cascadeH = cascadePtr->totalHeight;

                        menuPtr->postedCascade = mePtr;
                        TkPostSubmenu(menuPtr->interp, menuPtr, mePtr);

                        TkWaylandPostMenuAtAnchor(menuPtr->interp, cascadePtr,
                            cascadeAnchorX, cascadeAnchorY, 0, 0,
                            cascadeW, cascadeH, /*isRoot=*/0);
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
 *	Menu popups are wl_subsurfaces with empty input regions (see
 *	TkWaylandSubsurfaceCreate); they have no GLFWwindow and never
 *	receive input directly.  All menu input is driven by the raw
 *	wl_pointer / wl_keyboard listeners registered in tkGlfwInit.c
 *	(TkWaylandRegisterPointerListener), which call
 *	TkWaylandMenuHandlePointerMotion / HandlePointerButton /
 *	HandleEscape below whenever TkWaylandMenuPopupActive() is true.
 *	Nothing to register here.
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
 *	Query whether one or more menu popups are currently posted.
 *
 * Results:
 *	Non-zero if the menu stack is non-empty.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandMenuPopupActive(void)
{
    return menuStackDepth > 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandMenuConsumeDismissClick --
 *
 *	Returns non-zero exactly once if the most recent button press
 *	dismissed the menu stack via an outside click (set by
 *	TkWaylandMenuHandlePointerButton).  Intended for the GLFW button
 *	callback (in whatever file registers it) to swallow that click
 *	rather than also activating a widget underneath.  The flag is
 *	cleared on read.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandMenuConsumeDismissClick(void)
{
    int v = menuDismissedByClick;
    menuDismissedByClick = 0;
    return v;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandMenuHandlePointerMotion --
 *
 *	Called from the raw wl_pointer listener (tkGlfwInit.c) on every
 *	motion event while the menu stack is non-empty.  (x, y) are
 *	toplevel-surface-local logical pixels.
 *
 *	Hit-tests the menu stack from the topmost (innermost cascade) entry
 *	down to the root, dispatching to the first entry whose rect
 *	contains (x, y).  If none match, deactivates the topmost menu's
 *	current entry (standard "moved off the menu" behaviour) without
 *	closing the stack -- closing only happens via
 *	HandlePointerButton / HandleEscape.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandMenuHandlePointerMotion(
    int x,
    int y)
{
    int i;

    for (i = menuStackDepth - 1; i >= 0; i--) {
        MenuStackEntry *entry = &menuStack[i];
        if (x >= entry->x && x < entry->x + entry->w &&
            y >= entry->y && y < entry->y + entry->h) {
            MenuMouseMotion(entry->menuPtr, x - entry->x, y - entry->y);
            return;
        }
    }

    /* Cursor is over none of the posted menus. */
    if (menuStackDepth > 0) {
        TkMenu *topMenu = menuStack[menuStackDepth - 1].menuPtr;
        if (topMenu && topMenu->active != -1) {
            TkActivateMenuEntry(topMenu, -1);
            TkEventuallyRedrawMenu(topMenu, NULL);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandMenuHandlePointerButton --
 *
 *	Called from the raw wl_pointer listener (tkGlfwInit.c) on every
 *	button press/release while the menu stack is non-empty.  (x, y)
 *	are toplevel-surface-local logical pixels; button/state follow the
 *	wl_pointer enums (button: BTN_LEFT=0x110 etc. as reported by
 *	Wayland; state: 0=released, 1=pressed).
 *
 *	On a press inside one of the posted menus, dispatches a click to
 *	that menu (left button only, matching the original
 *	MenuMouseClick(..., 1) contract).  On a press outside all posted
 *	menus, dismisses the entire stack and sets the
 *	dismiss-click flag so the click does not also activate a widget
 *	underneath.
 *
 *	Button releases are ignored; Tk menu interaction is click-driven on
 *	press, matching the original implementation.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandMenuHandlePointerButton(
    int x,
    int y,
    int button,
    int state)
{
    int i;

    if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
        return;
    }

    for (i = menuStackDepth - 1; i >= 0; i--) {
        MenuStackEntry *entry = &menuStack[i];
        if (x >= entry->x && x < entry->x + entry->w &&
            y >= entry->y && y < entry->y + entry->h) {
            int tkButton = (button == BTN_LEFT) ? 1 : 3;
            MenuMouseClick(entry->menuPtr, x - entry->x, y - entry->y,
                           tkButton);
            return;
        }
    }

    /* Outside every posted menu: dismiss and swallow this click. */
    menuDismissedByClick = 1;
    TkWaylandMenuDismissAll();
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandMenuHandleEscape --
 *
 *	Called from the raw wl_keyboard listener (tkGlfwInit.c) when
 *	Escape is pressed while the menu stack is non-empty.  Dismisses the
 *	entire stack.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandMenuHandleEscape(void)
{
    TkWaylandMenuDismissAll();
}


/* 
 * Local Variables: 
 * mode: c 
 * c-basic-offset: 4 
 * fill-column: 78 
 * End: 
 */
