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
#include <math.h>
#include <stdio.h>

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

/* Debug macro */
#define MENU_DEBUG 1
#if MENU_DEBUG
#define MENU_LOG(fmt, ...) fprintf(stderr, "MENU: " fmt "\n", ##__VA_ARGS__)
#else
#define MENU_LOG(fmt, ...) ((void)0)
#endif

/*
 * Forward declarations.
 */

static void SetHelpMenu(TkMenu *menuPtr);
static void DrawMenuEntryAccelerator(TkMenu *menuPtr,
				     TkMenuEntry *mePtr, NVGcontext *vg,
				     Tk_Font tkfont, const Tk_FontMetrics *fmPtr,
				     Tk_3DBorder activeBorder, Tk_3DBorder bgBorder,
				     int x, int y, int width, int height, bool drawArrow,
				     NVGcolor textColor);
static void DrawMenuEntryBackground(TkMenu *menuPtr,
				    TkMenuEntry *mePtr, NVGcontext *vg,
				    Tk_3DBorder activeBorder, Tk_3DBorder bgBorder,
				    int x, int y, int width, int height);
static void DrawMenuEntryIndicator(TkMenu *menuPtr,
				   TkMenuEntry *mePtr, NVGcontext *vg,
				   Tk_3DBorder border, XColor *indicatorColor,
				   XColor *disableColor, Tk_Font tkfont,
				   const Tk_FontMetrics *fmPtr, int x, int y,
				   int width, int height, NVGcolor textColor);
static void DrawMenuEntryLabel(TkMenu * menuPtr,
			       TkMenuEntry *mePtr, NVGcontext *vg,
			       Tk_Font tkfont, const Tk_FontMetrics *fmPtr,
			       int x, int y, int width, int height, NVGcolor textColor);
static void DrawMenuSeparator(TkMenu *menuPtr,
			      TkMenuEntry *mePtr, NVGcontext *vg,
			      Tk_Font tkfont, const Tk_FontMetrics *fmPtr,
			      int x, int y, int width, int height);
static void DrawTearoffEntry(TkMenu *menuPtr,
			     TkMenuEntry *mePtr, NVGcontext *vg,
			     Tk_Font tkfont, const Tk_FontMetrics *fmPtr,
			     int x, int y, int width, int height);
static void DrawMenuUnderline(TkMenu *menuPtr,
			      TkMenuEntry *mePtr, NVGcontext *vg,
			      Tk_Font tkfont, const Tk_FontMetrics *fmPtr,
			      int x, int y, int width, int height, NVGcolor textColor);
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
static void MenuStackPop(int toDepth);
static void RealizeMenubarPopup(TkWindow *winPtr, WmInfo *wmPtr, TkMenu *menuPtr);
MODULE_SCOPE void TkWaylandMenuBarRealizeIfPending(TkWindow *winPtr);
MODULE_SCOPE void TkWaylandMenubarEntriesChanged(TkMenu *menuPtr);

/*
 * Implemented in tkWaylandPopup.c.  Declared here defensively in case it
 * is not already exposed by tkGlfwInt.h.
 */
MODULE_SCOPE void TkWaylandSubsurfaceReconfigure(TkWaylandPopup *popup,
	int x, int y, int width, int height);

/* Helper function to convert Tk colors to NVGcolor. */
static NVGcolor TkColorToNVGColor(XColor *color) {
    if (!color) return nvgRGBA(0, 0, 0, 255);
    return nvgRGBA(color->red >> 8, color->green >> 8, 
                   color->blue >> 8, 255);
}

/* Helper function to setup NanoVG font from Tk_Font. */
static void SetupNanoVGFont(NVGcontext *vg, Tk_Font tkfont) {
    if (!vg) return;
    
    if (tkfont) {
        Tk_FontMetrics fm;
        Tk_GetFontMetrics(tkfont, &fm);
        float fontSize = (float)fm.linespace;
        if (fontSize <= 0) fontSize = DEFAULT_FONT_SIZE;
        
        /* For now, use a default font face since we can't easily get the
         * actual font family name from Tk_Font in a portable way */
        nvgFontFace(vg, DEFAULT_FONT);
        nvgFontSize(vg, fontSize);
    } else {
        nvgFontFace(vg, DEFAULT_FONT);
        nvgFontSize(vg, DEFAULT_FONT_SIZE);
    }
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
}

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
 * Results:
 *	Stack index or -1.
 *
 * Side effects:
 *	None.
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
    MENU_LOG("TkpNewMenu called for menu %p", (void*)menuPtr);
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
TkpDestroyMenu(TCL_UNUSED(TkMenu *))  /* menuPtr */
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

    /*
     * If this entry belongs to a menubar that is already attached to a
     * toplevel (TkpSetWindowMenuBar), the menubar's popup may have been
     * created/drawn before this entry existed.  Recompute geometry and
     * redraw (and resize the subsurface / toplevel margin if the
     * menubar's height changed).
     */
    TkWaylandMenubarEntriesChanged(mePtr->menuPtr);

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
 *	If the entry's menu is an attached menubar, may trigger a recompute
 *	and redraw of the menubar popup.
 *
 *---------------------------------------------------------------------------
 */

int
TkpMenuNewEntry(TkMenuEntry *mePtr)
{
    TkWaylandMenubarEntriesChanged(mePtr->menuPtr);
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * Menubar realization registry
 *
 *	TkpSetWindowMenuBar is very often called while the toplevel still has
 *	WM_NEVER_MAPPED set -- "<toplevel> configure -menu .menubar" normally
 *	runs during script setup, before the toplevel's GLFWwindow/wl_surface
 *	exist.  In that case there is nothing to create a subsurface in yet.
 *	We remember the (menubar Tk_Window -> TkMenu *) association here so
 *	that TkWmMapWindow (tkWaylandWm.c) can finish the job via
 *	TkWaylandMenuBarRealizeIfPending once a real surface exists.
 *
 *	Likewise, menu entries are very often added/configured *after*
 *	TkpSetWindowMenuBar has already run (or after the deferred
 *	realization above), so any popup created at that point was drawn with
 *	zero entries and never refreshed.  TkpMenuNewEntry and
 *	TkpConfigureMenuEntry call TkWaylandMenubarEntriesChanged, which
 *	consults this registry, to recompute geometry, resize the subsurface
 *	and the toplevel's reserved top margin if needed, and redraw.
 *
 *---------------------------------------------------------------------------
 */

#define TK_WAYLAND_MENUBAR_MAX 16

typedef struct {
    Tk_Window menubarWin;	/* The menu's own tkwin (== wmPtr->menubar). */
    TkMenu   *menuPtr;
} MenubarRegEntry;

static MenubarRegEntry menubarRegistry[TK_WAYLAND_MENUBAR_MAX];

static void
MenubarRegistryRemove(
    Tk_Window menubarWin)
{
    int i;
    if (!menubarWin) return;
    for (i = 0; i < TK_WAYLAND_MENUBAR_MAX; i++) {
        if (menubarRegistry[i].menubarWin == menubarWin) {
            menubarRegistry[i].menubarWin = NULL;
            menubarRegistry[i].menuPtr    = NULL;
        }
    }
}

static void
MenubarRegistryAdd(
    Tk_Window menubarWin,
    TkMenu   *menuPtr)
{
    int i, slot = -1;
    for (i = 0; i < TK_WAYLAND_MENUBAR_MAX; i++) {
        if (menubarRegistry[i].menubarWin == menubarWin) {
            menubarRegistry[i].menuPtr = menuPtr;
            return;
        }
        if (slot < 0 && menubarRegistry[i].menubarWin == NULL) {
            slot = i;
        }
    }
    if (slot >= 0) {
        menubarRegistry[slot].menubarWin = menubarWin;
        menubarRegistry[slot].menuPtr    = menuPtr;
    }
}

static TkMenu *
MenubarRegistryLookup(
    Tk_Window menubarWin)
{
    int i;
    if (!menubarWin) return NULL;
    for (i = 0; i < TK_WAYLAND_MENUBAR_MAX; i++) {
        if (menubarRegistry[i].menubarWin == menubarWin) {
            return menubarRegistry[i].menuPtr;
        }
    }
    return NULL;
}

/*
 *---------------------------------------------------------------------------
 *
 * RealizeMenubarPopup --
 *
 *	Create wmPtr->menubarPopup -- a wl_subsurface anchored to the
 *	top-left corner of the toplevel -- and render menuPtr's current
 *	entries into it.  Also reserves space for the menubar via
 *	Tk_SetInternalBorderEx, so that pack/grid/place position the
 *	toplevel's other children starting below the menubar instead of
 *	underneath it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates wmPtr->menubarPopup and sets wmPtr->popup, wmPtr->menuHeight,
 *	and the toplevel's internal border.
 *
 *---------------------------------------------------------------------------
 */

static void
RealizeMenubarPopup(
    TkWindow *winPtr,
    WmInfo   *wmPtr,
    TkMenu   *menuPtr)
{
    int mbW, mbH;

    if (!winPtr || !wmPtr || !menuPtr || wmPtr->menubarPopup) return;

    TkRecomputeMenu(menuPtr);
    mbH = menuPtr->totalHeight;
    if (mbH < 20) mbH = 24;
    mbW = Tk_Width((Tk_Window)winPtr);
    if (mbW <= 0) mbW = Tk_ReqWidth((Tk_Window)winPtr);
    if (mbW <= 0) mbW = 200;

    wmPtr->menuHeight = mbH;

    wmPtr->menubarPopup = TkWaylandSubsurfaceCreate(
        TkWaylandGetGLFWwindow(winPtr), 0, 0, mbW, mbH);

    if (!wmPtr->menubarPopup) {
        fprintf(stderr,
            "RealizeMenubarPopup: failed to create menubar subsurface\n");
        return;
    }

    wmPtr->popup = wmPtr->menubarPopup;
    Tk_SetInternalBorderEx((Tk_Window)winPtr, 0, 0, mbH, 0);
    MenuDrawMenubarIntoPopup(menuPtr, wmPtr->menubarPopup);
    MENU_LOG("RealizeMenubarPopup: menubar subsurface %p (%dx%d)",
        (void *)wmPtr->menubarPopup, mbW, mbH);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandMenuBarRealizeIfPending --
 *
 *	Called from TkWmMapWindow (tkWaylandWm.c) immediately after a
 *	toplevel's GLFWwindow/wl_surface have just been created.  If a
 *	menubar was attached via TkpSetWindowMenuBar while the toplevel was
 *	still WM_NEVER_MAPPED, the subsurface could not be created at that
 *	time; finish that job now.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May create wmPtr->menubarPopup and draw the menubar.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandMenuBarRealizeIfPending(
    TkWindow *winPtr)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    TkMenu *menuPtr;

    if (!wmPtr || !wmPtr->menubar || wmPtr->menubarPopup) return;

    menuPtr = MenubarRegistryLookup(wmPtr->menubar);
    if (menuPtr) {
        RealizeMenubarPopup(winPtr, wmPtr, menuPtr);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandMenubarEntriesChanged --
 *
 *	Called whenever a menubar's entries are created or (re)configured.
 *	This commonly happens after TkpSetWindowMenuBar already ran (and
 *	either drew a popup with zero entries, or could not create one yet
 *	because the toplevel was WM_NEVER_MAPPED).  Recomputes the menubar's
 *	geometry; if its size changed, resizes/repositions the subsurface and
 *	updates the toplevel's reserved top margin; then redraws.  If the
 *	subsurface does not exist yet but the toplevel is now mapped, creates
 *	it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May create, resize, or redraw wmPtr->menubarPopup, and may update the
 *	toplevel's internal border.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandMenubarEntriesChanged(
    TkMenu *menuPtr)
{
    TkWindow *mwin, *winPtr;
    WmInfo   *wmPtr = NULL;
    int mbW, mbH, curW, curH;

    if (!menuPtr || !menuPtr->tkwin || menuPtr->menuType != MENUBAR) return;

    mwin = (TkWindow *)menuPtr->tkwin;
    for (winPtr = mwin; winPtr->parentPtr && !Tk_IsTopLevel(winPtr);
            winPtr = winPtr->parentPtr) {
        /* walk up to the containing toplevel */
    }
    if (winPtr->wmInfoPtr) {
        wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    }
    if (!wmPtr || wmPtr->menubar != (Tk_Window)mwin) return;

    if (!wmPtr->menubarPopup) {
        if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
            RealizeMenubarPopup(winPtr, wmPtr, menuPtr);
        }
        return;
    }

    TkRecomputeMenu(menuPtr);
    mbH = menuPtr->totalHeight;
    if (mbH < 20) mbH = 24;
    mbW = Tk_Width((Tk_Window)winPtr);
    TkWaylandPopupGetSize(wmPtr->menubarPopup, &curW, &curH);
    if (mbW <= 0) mbW = curW;

    if (mbH != curH || mbW != curW) {
        wmPtr->menuHeight = mbH;
        TkWaylandSubsurfaceReconfigure(wmPtr->menubarPopup, 0, 0, mbW, mbH);
        Tk_SetInternalBorderEx((Tk_Window)winPtr, 0, 0, mbH, 0);
    }

    MenuDrawMenubarIntoPopup(menuPtr, wmPtr->menubarPopup);
}


/*
 * TkpSetWindowMenuBar -
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

    MENU_LOG("TkpSetWindowMenuBar called");

    if (!wmPtr) return;

    if (wmPtr->menubarPopup) {
        if (wmPtr->popup == wmPtr->menubarPopup) {
            wmPtr->popup = NULL;
        }
        TkWaylandPopupDestroy(wmPtr->menubarPopup);
        wmPtr->menubarPopup = NULL;
    }

    if (wmPtr->menubar) {
        MenubarRegistryRemove(wmPtr->menubar);
    }

    if (!menuPtr) {
        wmPtr->menubar    = NULL;
        wmPtr->menuHeight = 0;
        Tk_SetInternalBorderEx(tkwin, 0, 0, 0, 0);
        return;
    }

    wmPtr->menubar = (Tk_Window)menuPtr->tkwin;
    MenubarRegistryAdd(wmPtr->menubar, menuPtr);

    if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
        /*
         * The menubar is a permanent part of the toplevel, not a
         * transient compositor-managed surface, so use a wl_subsurface
         * (no xdg_surface/configure handshake required) anchored to the
         * top-left corner of the toplevel at (0,0).
         */
        RealizeMenubarPopup(winPtr, wmPtr, menuPtr);
    } else {
        /*
         * No GLFWwindow/wl_surface exists yet to create a subsurface in.
         * TkWmMapWindow will call TkWaylandMenuBarRealizeIfPending once
         * the toplevel is mapped.  Still record the requested height so
         * that anything consulting wmPtr->menuHeight before then sees a
         * reasonable value.
         */
        TkRecomputeMenu(menuPtr);
        int mbH = menuPtr->totalHeight;
        if (mbH < 20) mbH = 24;
        wmPtr->menuHeight = mbH;
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
			 TCL_UNUSED(Tk_Font),  /* tkfont */
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
			NVGcontext *vg,
			Tk_3DBorder activeBorder,
			Tk_3DBorder bgBorder,
			int x,
			int y,
			int width,
			int height)
{
    Tk_3DBorder border = bgBorder;
    NVGcolor fillColor;
    
    if (mePtr->state == ENTRY_ACTIVE) {
	border = activeBorder;
	fillColor = TkColorToNVGColor(Tk_3DBorderColor(activeBorder));
    } else {
	fillColor = TkColorToNVGColor(Tk_3DBorderColor(bgBorder));
    }
    
    /* Fill background. */
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, fillColor);
    nvgFill(vg);
    
    /* Draw border if needed. */
    if (menuPtr->menuType != MENUBAR || mePtr->state == ENTRY_ACTIVE) {
	int relief = TK_RELIEF_FLAT;
	int activeBorderWidth;
	
	Tk_GetPixelsFromObj(NULL, menuPtr->tkwin,
			    menuPtr->activeBorderWidthPtr, &activeBorderWidth);
	
	if ((menuPtr->menuType == MENUBAR)
	    && ((menuPtr->postedCascade == NULL)
		|| (menuPtr->postedCascade != mePtr))) {
	    relief = TK_RELIEF_FLAT;
	} else {
	    relief = menuPtr->activeRelief;
	}
	
	if (relief != TK_RELIEF_FLAT && activeBorderWidth > 0) {
	    nvgBeginPath(vg);
	    nvgRect(vg, x, y, width, height);
	    nvgStrokeWidth(vg, activeBorderWidth);
	    nvgStrokeColor(vg, TkColorToNVGColor(Tk_3DBorderColor(border)));
	    nvgStroke(vg);
	}
    }
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
			 NVGcontext *vg,
			 Tk_Font tkfont,
			 const Tk_FontMetrics *fmPtr,
			 Tk_3DBorder activeBorder,
			 Tk_3DBorder bgBorder,
			 int x,
			 int y,
			 int width,
			 int height,
			 bool drawArrow,
			 NVGcolor textColor)
{
    int borderWidth;
    int activeBorderWidth;

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
	
	/* Draw cascade arrow as filled triangle using NanoVG. */
	nvgBeginPath(vg);
	nvgMoveTo(vg, px, py);
	nvgLineTo(vg, px, py + CASCADE_ARROW_HEIGHT);
	nvgLineTo(vg, px + CASCADE_ARROW_WIDTH, py + CASCADE_ARROW_HEIGHT/2);
	nvgClosePath(vg);
	
	/* Use appropriate color based on active state. */
	if (mePtr->state == ENTRY_ACTIVE) {
	    nvgFillColor(vg, TkColorToNVGColor(Tk_3DBorderColor(activeBorder)));
	} else {
	    nvgFillColor(vg, TkColorToNVGColor(Tk_3DBorderColor(bgBorder)));
	}
	nvgFill(vg);
	
    } else if (mePtr->accelPtr != NULL) { 
	const char *accel = Tcl_GetString(mePtr->accelPtr); 
	int left = x + mePtr->labelWidth + activeBorderWidth 
	    + mePtr->indicatorSpace; 
	int ty; 
	
	if (menuPtr->menuType == MENUBAR) { 
	    left += 5; 
	} 
	
	ty = y + (height + fmPtr->ascent - fmPtr->descent) / 2; 
	
	/* Setup font from Tk font. */
	SetupNanoVGFont(vg, tkfont);
	nvgFillColor(vg, textColor);
	nvgText(vg, left, ty, accel, NULL);
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
		       NVGcontext *vg,
		       TCL_UNUSED(Tk_3DBorder),  /* border */
		       XColor *indicatorColor,
		       XColor *disableColor,
		       TCL_UNUSED(Tk_Font), /* tkfont */
		       TCL_UNUSED(const Tk_FontMetrics *), /* fmPtr */
		       int x,
		       int y,
		       TCL_UNUSED(int), /* width */
		       int height,
		       NVGcolor textColor)
{
    /* Draw check-button indicator. */
    if ((mePtr->type == CHECK_BUTTON_ENTRY) && mePtr->indicatorOn) {
	int top, left, size;
	int activeBorderWidth;
	NVGcolor color;

	Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, 
			    menuPtr->activeBorderWidthPtr, &activeBorderWidth); 
	top = y + height/2; 
	left = x + activeBorderWidth + 2 + mePtr->indicatorSpace/2; 
	size = PTR2INT(mePtr->platformEntryData); 
	
	color = (mePtr->state == ENTRY_DISABLED) ? 
	    TkColorToNVGColor(disableColor) : TkColorToNVGColor(indicatorColor);
	
	/* Draw checkbox square. */
	nvgBeginPath(vg);
	nvgRect(vg, left - size/2, top - size/2, size, size);
	nvgStrokeWidth(vg, 1.0f);
	nvgStrokeColor(vg, color);
	nvgStroke(vg);
	
	if (mePtr->entryFlags & ENTRY_SELECTED) { 
	    /* Draw check mark using NanoVG lines. */
	    nvgBeginPath(vg);
	    nvgMoveTo(vg, left - size/3, top);
	    nvgLineTo(vg, left - size/6, top + size/3);
	    nvgLineTo(vg, left + size/3, top - size/3);
	    nvgStrokeWidth(vg, 1.5f);
	    nvgStrokeColor(vg, color);
	    nvgStroke(vg);
	} 
    } 

    /* Draw radio-button indicator. */ 
    if ((mePtr->type == RADIO_BUTTON_ENTRY) && mePtr->indicatorOn) { 
	int top, left, radius; 
	int activeBorderWidth; 
	NVGcolor color;
	
	Tk_GetPixelsFromObj(NULL, menuPtr->tkwin, 
			    menuPtr->activeBorderWidthPtr, &activeBorderWidth); 
	top = y + height/2; 
	left = x + activeBorderWidth + 2 + mePtr->indicatorSpace/2; 
	radius = PTR2INT(mePtr->platformEntryData) / 2; 
	
	color = (mePtr->state == ENTRY_DISABLED) ? 
	    TkColorToNVGColor(disableColor) : TkColorToNVGColor(indicatorColor);
	
	/* Draw radio circle. */
	nvgBeginPath(vg);
	nvgCircle(vg, left, top, radius);
	nvgStrokeWidth(vg, 1.0f);
	nvgStrokeColor(vg, color);
	nvgStroke(vg);
	
	if (mePtr->entryFlags & ENTRY_SELECTED) { 
	    /* Fill inner circle. */
	    nvgBeginPath(vg);
	    nvgCircle(vg, left, top, radius/2);
	    nvgFillColor(vg, color);
	    nvgFill(vg);
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
		  NVGcontext *vg,
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

    nvgBeginPath(vg);
    nvgMoveTo(vg, x, y + height/2);
    nvgLineTo(vg, x + width - 1, y + height/2);
    nvgStrokeWidth(vg, 1.0f);
    nvgStrokeColor(vg, nvgRGBA(160, 160, 160, 255));
    nvgStroke(vg);
}

/*
 *---------------------------------------------------------------------------
 *
 * DrawMenuEntryLabel --
 *
 *	Draw the label (text and/or image) for a menu entry.
 *	Uses X11 drawing functions for image rendering via NanoVG.
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
		   NVGcontext *vg,
		   Tk_Font tkfont,
		   const Tk_FontMetrics *fmPtr,
		   int x,
		   int y,
		   int width,
		   int height,
		   NVGcolor textColor)
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
	MENU_LOG("DrawMenuEntryLabel: has image %dx%d", imageWidth, imageHeight);
    } else if (mePtr->bitmapPtr != NULL) { 
	Pixmap bitmap = Tk_GetBitmapFromObj(menuPtr->tkwin, mePtr->bitmapPtr); 
	Tk_SizeOfBitmap(menuPtr->display, bitmap, &imageWidth, &imageHeight); 
	haveImage = 1; 
	MENU_LOG("DrawMenuEntryLabel: has bitmap %dx%d", imageWidth, imageHeight);
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

    /* Draw image using X11 functions */
    if (haveImage) {
	int imageX = leftEdge + imageXOffset;
	int imageY = y + (height - imageHeight) / 2 + imageYOffset;
	
	/* Ensure image stays within menu bounds */
	if (imageX < x) imageX = x;
	if (imageY < y) imageY = y;
	if (imageX + imageWidth > x + width) imageX = x + width - imageWidth;
	if (imageY + imageHeight > y + height) imageY = y + height - imageHeight;
	
	if (mePtr->image != NULL) {
	    TkWindow *winPtr = (TkWindow *)menuPtr->tkwin;
	    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
	    if (wmPtr && wmPtr->popup) {
		Drawable drawable = Tk_WindowId(menuPtr->tkwin);
		if (drawable) {
		    GC gc = Tk_GetGC(menuPtr->tkwin, 0, NULL);
		    if (gc) {
			Tk_RedrawImage(mePtr->image, 0, 0, imageWidth, imageHeight,
				       drawable, imageX, imageY);
			Tk_FreeGC(menuPtr->display, gc);
		    }
		}
	    }
	    MENU_LOG("DrawMenuEntryLabel: rendered image at (%d,%d)", imageX, imageY);
	} else if (mePtr->bitmapPtr != NULL) {
	    TkWindow *winPtr = (TkWindow *)menuPtr->tkwin;
	    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
	    if (wmPtr && wmPtr->popup) {
		Drawable drawable = Tk_WindowId(menuPtr->tkwin);
		if (drawable) {
		    Pixmap bitmap = Tk_GetBitmapFromObj(menuPtr->tkwin, mePtr->bitmapPtr);
		    if (bitmap) {
			GC gc = Tk_GetGC(menuPtr->tkwin, 0, NULL);
			if (gc) {
			    XCopyArea(menuPtr->display, bitmap, drawable, gc,
				      0, 0, imageWidth, imageHeight, imageX, imageY);
			    Tk_FreeGC(menuPtr->display, gc);
			}
		    }
		}
	    }
	    MENU_LOG("DrawMenuEntryLabel: rendered bitmap at (%d,%d)", imageX, imageY);
	}
    }

    /* Draw text label using NanoVG */
    if ((mePtr->compound != COMPOUND_NONE) || !haveImage) { 
	int baseline = y + (height + fmPtr->ascent - fmPtr->descent) / 2; 
	
	if (mePtr->labelLength > 0) { 
	    const char *label = Tcl_GetString(mePtr->labelPtr); 
	    
	    /* Setup font from Tk font */
	    SetupNanoVGFont(vg, tkfont);
	    nvgFillColor(vg, textColor);
	    nvgText(vg, leftEdge + textXOffset, baseline + textYOffset, 
		    label, NULL);
	    
	    DrawMenuUnderline(menuPtr, mePtr, vg, tkfont, fmPtr, 
			      x + textXOffset, y + textYOffset, width, height, 
			      textColor); 
	} 
    } 

    /* Draw disabled overlay using stippling */
    if (mePtr->state == ENTRY_DISABLED) { 
	nvgBeginPath(vg);
	nvgRect(vg, x, y, width, height);
	nvgFillColor(vg, nvgRGBA(200, 200, 200, 128));
	nvgFill(vg);
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
		  NVGcontext *vg,
		  Tk_Font tkfont,
		  const Tk_FontMetrics *fmPtr,
		  int x,
		  int y,
		  TCL_UNUSED(int), /* width */
		  int height,
		  NVGcolor textColor)
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
	    
	    /* Calculate underline position */
	    underlineX = leftEdge + Tk_TextWidth(tkfont, label, start - label);
	    underlineWidth = Tk_TextWidth(tkfont, start, end - start);
	    
	    /* Draw underline using NanoVG */
	    nvgBeginPath(vg);
	    nvgMoveTo(vg, underlineX, ty + 2);
	    nvgLineTo(vg, underlineX + underlineWidth, ty + 2);
	    nvgStrokeWidth(vg, 1.0f);
	    nvgStrokeColor(vg, textColor);
	    nvgStroke(vg);
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
		 NVGcontext *vg,
		 TCL_UNUSED(Tk_Font ),
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

    /* Draw dashed line using NanoVG */
    nvgStrokeWidth(vg, 1.0f);
    nvgStrokeColor(vg, nvgRGBA(100, 100, 100, 255));
    
    while (px < x + width - 1) { 
	int ex = px + segmentWidth; 
	if (ex > x + width - 1) { 
	    ex = x + width - 1; 
	} 
	nvgBeginPath(vg);
	nvgMoveTo(vg, px, py);
	nvgLineTo(vg, ex, py);
	nvgStroke(vg);
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

    /* Safety checks */
    if (!interp || !menuPtr) {
        return TCL_ERROR;
    }

    MENU_LOG("TkpPostMenu: menu=%p, x=%d, y=%d", (void*)menuPtr, x, y);

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
    if (index >= 0 && index < menuPtr->numEntries && menuPtr->entries[index]) {
        y -= menuPtr->entries[index]->y;
    }

    int popupW = menuPtr->totalWidth;
    int popupH = menuPtr->totalHeight;
    if (popupW <= 0) popupW = 1;
    if (popupH <= 0) popupH = 1;

    MENU_LOG("TkpPostMenu: popup size %dx%d", popupW, popupH);

    /* Verify we have a valid main window before posting */
    if (!mainGlfwWindow) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "Cannot post menu: no main GLFW window", -1));
        return TCL_ERROR;
    }

    /*
     * For xdg_popup menus (context menus, cascades), we let the Wayland
     * compositor handle all positioning and flipping. We provide the
     * raw anchor rectangle relative to the parent surface, and the
     * positioner constraints handle boundary avoidance automatically.
     */
    return TkWaylandPostMenuAtAnchor(interp, menuPtr,
        x, y, 1, 1, popupW, popupH, 1);
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
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys popups and cleans up menu state.
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
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys all menu popups and sends virtual event.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandMenuDismissAll(void)
{
    MENU_LOG("TkWaylandMenuDismissAll called");
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
 *	For xdg_popup menus, the anchor rectangle is passed directly to the
 *	Wayland positioner. The compositor handles flipping and sliding
 *	automatically via the constraint adjustment flags.
 *
 *	For subsurfaces (menubar), we use the simple coordinate placement.
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
    /* Safety checks */
    if (!interp || !menuPtr || !menuPtr->tkwin) {
        if (interp) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(
                "TkWaylandPostMenuAtAnchor: invalid menu or window", -1));
        }
        return TCL_ERROR;
    }

    if (popupW <= 0) popupW = 1;
    if (popupH <= 0) popupH = 1;

    MENU_LOG("TkWaylandPostMenuAtAnchor: menu=%p, anchor=(%d,%d,%d,%d), size=%dx%d, isRoot=%d",
        (void*)menuPtr, anchorX, anchorY, anchorW, anchorH, popupW, popupH, isRoot);

    if (isRoot) {
        TkWaylandMenuDismissAll();
    }

    if (menuStackDepth >= TK_WAYLAND_MENU_STACK_MAX) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "TkWaylandPostMenuAtAnchor: menu stack overflow", -1));
        return TCL_ERROR;
    }

    /* For popup menus, we use subsurface positioning */
    TkWaylandPopup *popup = TkWaylandSubsurfaceCreate(
        mainGlfwWindow, anchorX, anchorY + anchorH, popupW, popupH);

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

    Tk_MoveResizeWindow(menuPtr->tkwin, anchorX, anchorY + anchorH, popupW, popupH);

    MenuStackEntry *entry = &menuStack[menuStackDepth++];
    entry->menuPtr = menuPtr;
    entry->popup   = popup;
    entry->x = anchorX;
    entry->y = anchorY + anchorH;
    entry->w = popupW;
    entry->h = popupH;

    MenuDrawIntoPopup(menuPtr, popup);

    MENU_LOG("TkWaylandPostMenuAtAnchor: success, depth=%d", menuStackDepth);

    return TCL_OK;
}

/* 
 *---------------------------------------------------------------------------
 *
 * TkpComputeMenubarGeometry --
 *
 *     Computes the size and layout of a menubar.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Updates geometry fields of menu entries.
 *
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
 *
 * TkpComputeStandardMenuGeometry --
 *
 *     Computes the size and layout of a standard (popup) menu.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Updates geometry fields of menu entries.
 *
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
 *
 * TkpDrawMenuEntry --
 *
 *     Renders a complete menu entry.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Calls sub-drawing routines to render into the NanoVG context.
 *
 *---------------------------------------------------------------------------
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
    NVGcontext *vg = NULL;
    int padY = (mePtr->menuPtr->menuType == MENUBAR) ? 3 : 0; 
    int adjustedY = y + padY; 
    int adjustedHeight = height - 2 * padY;
    
    /* Safety check */
    if (!mePtr || !mePtr->menuPtr || !mePtr->menuPtr->tkwin) return;
    
    /* Get the actual borders from the menu */
    if (mePtr->menuPtr->borderPtr != NULL) {
        bgBorder = Tk_Get3DBorderFromObj(mePtr->menuPtr->tkwin, 
                                          mePtr->menuPtr->borderPtr);
    }
    if (mePtr->menuPtr->activeBorderPtr != NULL) {
        activeBorder = Tk_Get3DBorderFromObj(mePtr->menuPtr->tkwin,
                                              mePtr->menuPtr->activeBorderPtr);
    }
    
    /* Get NanoVG context from the menu's popup */
    TkWindow *winPtr = (TkWindow *)mePtr->menuPtr->tkwin;
    if (winPtr && winPtr->wmInfoPtr && ((WmInfo *)winPtr->wmInfoPtr)->popup) {
        vg = TkWaylandPopupGetNVGContext(((WmInfo *)winPtr->wmInfoPtr)->popup);
    }
    
    if (!vg) return;
    
    /* Get the font for this entry - mirroring geometry code */
    Tk_Font entryFont = tkfont;
    Tk_FontMetrics entryMetrics;
    const Tk_FontMetrics *entryFmPtr = fmPtr;
    
    if (mePtr->fontPtr != NULL) {
        entryFont = Tk_GetFontFromObj(mePtr->menuPtr->tkwin, mePtr->fontPtr);
        if (entryFont) {
            Tk_GetFontMetrics(entryFont, &entryMetrics);
            entryFmPtr = &entryMetrics;
        }
    }
    
    /* Determine text color - use a safe default if border not available */
    XColor *fgColor = NULL;
    Tk_3DBorder defaultBorder = Tk_Get3DBorder(mePtr->menuPtr->interp,
                                                mePtr->menuPtr->tkwin,
                                                Tk_GetUid("background"));
    if (bgBorder) {
        fgColor = Tk_3DBorderColor(bgBorder);
    } else if (defaultBorder) {
        fgColor = Tk_3DBorderColor(defaultBorder);
    }
    NVGcolor textColor = fgColor ? TkColorToNVGColor(fgColor) : nvgRGBA(0, 0, 0, 255);
    
    if (mePtr->state == ENTRY_DISABLED) {
        textColor = nvgRGBA(128, 128, 128, 255);
    }
    
    DrawMenuEntryBackground(mePtr->menuPtr, mePtr, vg, 
			    activeBorder, bgBorder, x, y, width, height); 
    
    if (mePtr->type == SEPARATOR_ENTRY) { 
	DrawMenuSeparator(mePtr->menuPtr, mePtr, vg, 
			  entryFont, entryFmPtr, x, adjustedY, width, adjustedHeight); 
    } else if (mePtr->type == TEAROFF_ENTRY) { 
	DrawTearoffEntry(mePtr->menuPtr, mePtr, vg, 
			 entryFont, entryFmPtr, x, adjustedY, width, adjustedHeight); 
    } else { 
	DrawMenuEntryLabel(mePtr->menuPtr, mePtr, vg, entryFont, entryFmPtr, 
			   x, adjustedY, width, adjustedHeight, textColor); 
	DrawMenuEntryAccelerator(mePtr->menuPtr, mePtr, vg, entryFont, entryFmPtr, 
				 activeBorder, bgBorder, x, adjustedY, width, adjustedHeight, 
				 (drawingParameters & DRAW_MENU_ENTRY_ARROW) != 0, textColor);
	if (!mePtr->hideMargin) { 
	    /* For indicator, we need proper colors - get them from the borders */
	    XColor *indicatorColor = NULL;
	    XColor *disableColor = NULL;
	    Tk_3DBorder disabledBorder = NULL;
	    
	    if (bgBorder) {
	        indicatorColor = Tk_3DBorderColor(bgBorder);
	    } else if (defaultBorder) {
	        indicatorColor = Tk_3DBorderColor(defaultBorder);
	    }
	    
	    disabledBorder = Tk_Get3DBorder(mePtr->menuPtr->interp,
	                                     mePtr->menuPtr->tkwin,
	                                     Tk_GetUid("disabledForeground"));
	    if (disabledBorder) {
	        disableColor = Tk_3DBorderColor(disabledBorder);
	    }
	    
	    DrawMenuEntryIndicator(mePtr->menuPtr, mePtr, vg, 
				   bgBorder, indicatorColor, disableColor,
				   entryFont, entryFmPtr, x, adjustedY, width, adjustedHeight, 
				   textColor); 
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
    Tk_Font menuFont;
    Tk_FontMetrics menuMetrics;
    Tk_Font entryFont;
    Tk_FontMetrics entryMetrics;
    const Tk_FontMetrics *fmPtr;

    if (!popup || !menuPtr || !menuPtr->tkwin) {
        MENU_LOG("MenuDrawIntoPopup: invalid parameters");
        return;
    }

    TkWaylandPopupGetSize(popup, &menuW, &menuH);
    MENU_LOG("MenuDrawIntoPopup: menu %p, popup %p, size %dx%d", 
        (void*)menuPtr, (void*)popup, menuW, menuH);
    
    if (menuW <= 0 || menuH <= 0) {
        MENU_LOG("MenuDrawIntoPopup: invalid size");
        return;
    }

    if (TkWaylandPopupBeginDraw(popup) != TCL_OK) {
        MENU_LOG("MenuDrawIntoPopup: BeginDraw failed");
        return;
    }

    NVGcontext *vg = TkWaylandPopupGetNVGContext(popup);
    if (!vg) {
        MENU_LOG("MenuDrawIntoPopup: no NVG context");
        TkWaylandPopupEndDraw(popup);
        return;
    }

    /* Get menu's default font - mirroring geometry code */
    menuFont = Tk_GetFontFromObj(menuPtr->tkwin, menuPtr->fontPtr);
    if (menuFont) {
        Tk_GetFontMetrics(menuFont, &menuMetrics);
        MENU_LOG("MenuDrawIntoPopup: menu font size=%d", menuMetrics.linespace);
    } else {
        MENU_LOG("MenuDrawIntoPopup: no menu font, using default");
        menuMetrics.linespace = DEFAULT_FONT_SIZE;
        menuMetrics.ascent = DEFAULT_FONT_SIZE * 0.75;
        menuMetrics.descent = DEFAULT_FONT_SIZE * 0.25;
    }

    /* Draw background */
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, (float)menuW, (float)menuH);
    nvgFillColor(vg, nvgRGB(240, 240, 240));
    nvgFill(vg);
    MENU_LOG("MenuDrawIntoPopup: drew background");

    /* Draw border */
    nvgBeginPath(vg);
    nvgRect(vg, 0.5f, 0.5f, (float)menuW - 1.0f, (float)menuH - 1.0f);
    nvgStrokeColor(vg, nvgRGB(160, 160, 160));
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    Drawable d = Tk_WindowId(menuPtr->tkwin);
    MENU_LOG("MenuDrawIntoPopup: drawing %d entries", menuPtr->numEntries);
    
    for (i = 0; i < menuPtr->numEntries; i++) {
        TkMenuEntry *mePtr = menuPtr->entries[i];
        if (!mePtr) continue;
        
        /* Determine font for this entry - mirroring geometry code */
        if (mePtr->fontPtr == NULL) {
            entryFont = menuFont;
            fmPtr = &menuMetrics;
        } else {
            entryFont = Tk_GetFontFromObj(menuPtr->tkwin, mePtr->fontPtr);
            if (entryFont) {
                Tk_GetFontMetrics(entryFont, &entryMetrics);
                fmPtr = &entryMetrics;
            } else {
                entryFont = menuFont;
                fmPtr = &menuMetrics;
            }
        }
        
        MENU_LOG("MenuDrawIntoPopup: drawing entry %d at (%d,%d) size %dx%d, label='%s'",
            i, mePtr->x, mePtr->y, mePtr->width, mePtr->height,
            mePtr->labelPtr ? Tcl_GetString(mePtr->labelPtr) : "(null)");
        
        TkpDrawMenuEntry(mePtr, d, entryFont, fmPtr,
            mePtr->x, mePtr->y,
            mePtr->width, mePtr->height,
            DRAW_MENU_ENTRY_ARROW);
    }

    TkWaylandPopupEndDraw(popup);
    MENU_LOG("MenuDrawIntoPopup: completed");
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
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders the menubar into the popup surface.
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

    MENU_LOG("TkpDisplayMenu called");

    if (!menuPtr || !menuPtr->tkwin) {
        MENU_LOG("TkpDisplayMenu: invalid menu");
        return;
    }

    winPtr = (TkWindow *)menuPtr->tkwin;
    wmPtr  = (WmInfo *)winPtr->wmInfoPtr;

    if (!wmPtr || !wmPtr->popup) {
        MENU_LOG("TkpDisplayMenu: no popup surface");
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
    MENU_LOG("TkWaylandMenuInit called");
    TkWaylandPopupInit();
}

/*
 *---------------------------------------------------------------------------
 *
 * SetHelpMenu --
 *
 *     Marks the Help cascade in a menubar when "useMotifHelp" is enabled.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Sets or clears the ENTRY_HELP_MENU flag.
 *
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
 *
 * TkpInitializeMenuBindings --
 *
 *     Initializes platform-specific menu bindings.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     No-op on Wayland.
 *
 *---------------------------------------------------------------------------
 */

void 
TkpInitializeMenuBindings( 
			  TCL_UNUSED(Tcl_Interp *), /* interp */
			  TCL_UNUSED(Tk_BindingTable)) /* bindingTable */
{ 
    /* Nothing to do on Wayland. */ 
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpMenuNotifyToplevelCreate --
 *
 *     Handles toplevel-creation notifications that affect menus.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     No-op on Wayland.
 *
 *---------------------------------------------------------------------------
 */

void 
TkpMenuNotifyToplevelCreate( 
			    TCL_UNUSED(Tcl_Interp *), /* interp */
			    TCL_UNUSED(const char *)) /* name */
{ 
    /* Nothing to do on Wayland. */ 
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpMenuInit --
 *
 *     Performs platform-specific menu initialization.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Assumes NanoVG context and fonts are initialized elsewhere.
 *
 *---------------------------------------------------------------------------
 */

void 
TkpMenuInit(void) 
{ 
    /* NanoVG context and fonts assumed to be set up elsewhere. */ 
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpMenuThreadInit --
 *
 *     Initializes thread-specific menu state.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     No-op on Wayland.
 *
 *---------------------------------------------------------------------------
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
		   TCL_UNUSED(Tcl_Interp *), /* interp */
		   TkMenu *menuPtr,
		   int x,
		   int y,
		   Tcl_Size index)
{
    int result;
    int reqW, reqH;
    GLFWmonitor *monitor;
    const GLFWvidmode *mode;
    int screenW = 1920;
    int screenH = 1080;
    
    MENU_LOG("TkpPostTearoffMenu called");
    
    TkActivateMenuEntry(menuPtr, -1);
    TkRecomputeMenu(menuPtr);
    
    result = TkPostCommand(menuPtr);
    if (result != TCL_OK) {
        return result;
    }

    if (menuPtr->tkwin == NULL) {
        return TCL_OK;
    }

    if (index >= menuPtr->numEntries) {
        index = menuPtr->numEntries - 1;
    }
    if (index >= 0 && index < menuPtr->numEntries && menuPtr->entries[index]) {
        y -= menuPtr->entries[index]->y;
    }

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

    Tk_MoveWindow(menuPtr->tkwin, x, y);
    Tk_ResizeWindow(menuPtr->tkwin, reqW, reqH);
    Tk_MapWindow(menuPtr->tkwin);
    
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
    
    if (!menuPtr) return;
    
    if (button != 1) {
        return;
    }
    
    for (i = 0; i < menuPtr->numEntries; i++) {
        TkMenuEntry *mePtr = menuPtr->entries[i];
        if (!mePtr) continue;

        if (x >= mePtr->x && x < mePtr->x + mePtr->width &&
            y >= mePtr->y && y < mePtr->y + mePtr->height) {
            
            if (mePtr->state == ENTRY_DISABLED || 
                mePtr->type == SEPARATOR_ENTRY ||
                mePtr->type == TEAROFF_ENTRY) {
                return;
            }
            
            switch (mePtr->type) {
            case COMMAND_ENTRY:
                TkInvokeMenu(menuPtr->interp, menuPtr, i);
                TkWaylandMenuDismissAll();
                break;

            case CASCADE_ENTRY:
                if (mePtr->namePtr != NULL) {
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
                            cascadeW, cascadeH, 0);

                        TkEventuallyRedrawMenu(menuPtr, NULL);
                    }
                }
                break;
                
            case CHECK_BUTTON_ENTRY:
                if (mePtr->entryFlags & ENTRY_SELECTED) {
                    mePtr->entryFlags &= ~ENTRY_SELECTED;
                } else {
                    mePtr->entryFlags |= ENTRY_SELECTED;
                }
                TkInvokeMenu(menuPtr->interp, menuPtr, i);
                TkEventuallyRedrawMenu(menuPtr, NULL);
                break;
                
            case RADIO_BUTTON_ENTRY:
                if (!(mePtr->entryFlags & ENTRY_SELECTED)) {
                    if (mePtr->namePtr != NULL) {
                        int j;
                        for (j = 0; j < menuPtr->numEntries; j++) {
                            TkMenuEntry *otherPtr = menuPtr->entries[j];
                            if (otherPtr && otherPtr->type == RADIO_BUTTON_ENTRY &&
                                otherPtr->namePtr != NULL &&
                                strcmp(Tcl_GetString(otherPtr->namePtr),
				       Tcl_GetString(mePtr->namePtr)) == 0) {
                                otherPtr->entryFlags &= ~ENTRY_SELECTED;
                            }
                        }
                    }
                    mePtr->entryFlags |= ENTRY_SELECTED;
                    TkInvokeMenu(menuPtr->interp, menuPtr, i);
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
    
    if (!menuPtr) return;
    
    for (i = 0; i < menuPtr->numEntries; i++) {
        TkMenuEntry *mePtr = menuPtr->entries[i];
        if (!mePtr) continue;

        if (x >= mePtr->x && x < mePtr->x + mePtr->width &&
            y >= mePtr->y && y < mePtr->y + mePtr->height) {
            
            foundEntry = 1;
            
            if (mePtr->state == ENTRY_DISABLED ||
                mePtr->type == SEPARATOR_ENTRY ||
                mePtr->type == TEAROFF_ENTRY) {
                continue;
            }
            
            if (menuPtr->active != i) {

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
                            cascadeW, cascadeH, 0);
                    }
                }

                TkEventuallyRedrawMenu(menuPtr, NULL);
            }
            return;
        }
    }
    
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
    if (!menuPtr) return;
    
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
			    TCL_UNUSED(Tk_Window)) /* tkwin */
{
    /* No-op */
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
 * Results:
 *	1 if a dismiss click was consumed, 0 otherwise.
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
 * Results:
 *	None.
 *
 * Side effects:
 *	Activates/deactivates menu entries and manages cascade popups.
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
 * Results:
 *	None.
 *
 * Side effects:
 *	May invoke menu commands or dismiss menu stack.
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
 * Results:
 *	None.
 *
 * Side effects:
 *	Dismisses all posted menus.
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
