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
#include "tkWaylandInt.h"
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

/* The root GLFWwindow, defined in TkWaylandInit.c. */
extern GLFWwindow *mainGlfwWindow;

/* Menu constants. */
#define MENU_MARGIN_WIDTH	2
#define MENU_DIVIDER_HEIGHT	2
#define ENTRY_HELP_MENU		ENTRY_PLATFORM_FLAG1

/* WmInfo flag from tkUnixWm.c */
#ifndef WM_NEVER_MAPPED
#define WM_NEVER_MAPPED (1<<0)
#endif

/* WM_UPDATE_SIZE_HINTS flag for geometry updates */
#ifndef WM_UPDATE_SIZE_HINTS
#define WM_UPDATE_SIZE_HINTS (1<<1)
#endif

/* Debug macro */
#define MENU_DEBUG 1
#if MENU_DEBUG
#define MENU_LOG(fmt, ...) fprintf(stderr, "MENU: " fmt "\n", ##__VA_ARGS__)
#else
#define MENU_LOG(fmt, ...) ((void)0)
#endif

/*
 * Forward declarations from other Wayland modules.
 */
MODULE_SCOPE void TkWaylandUpdateGeometryInfo(void *clientData);
MODULE_SCOPE int TkWaylandPopupBeginDraw(TkWaylandPopup *popup);
MODULE_SCOPE void TkWaylandPopupEndDraw(TkWaylandPopup *popup);
MODULE_SCOPE NVGcontext* TkWaylandPopupGetNVGContext(TkWaylandPopup *popup);
MODULE_SCOPE void TkWaylandPopupGetSize(TkWaylandPopup *popup, int *width, int *height);
MODULE_SCOPE int TkWaylandPopupResize(TkWaylandPopup *popup, int width, int height);
MODULE_SCOPE TkWaylandPopup* TkWaylandSubsurfaceCreate(GLFWwindow *window, int x, int y, int width, int height);
MODULE_SCOPE void TkWaylandSubsurfacePlaceAbove(TkWaylandPopup *popup, TkWaylandPopup *above);
MODULE_SCOPE void TkWaylandPopupDestroy(TkWaylandPopup *popup);
MODULE_SCOPE int EnsureNvgFont(WaylandFont *fontPtr, NVGcontext *vg);
MODULE_SCOPE GLFWwindow* TkWaylandGetGLFWwindow(TkWindow *winPtr);
MODULE_SCOPE TkWaylandPopup *TkWaylandFindMenubarPopup(Tk_Window menubarWin);
MODULE_SCOPE void TkpDisplayMenuButton(void *clientData);
MODULE_SCOPE NVGcontext* TkWaylandGetNVGContext(Drawable d);
MODULE_SCOPE NVGcolor TkWaylandXColorToNVG(XColor *xcolor);
MODULE_SCOPE NVGcolor TkWaylandPixelToNVG(unsigned long pixel);
MODULE_SCOPE void TkWaylandMenubarDestroy(TkWindow *winPtr);

/*
 * Menu popup stack
 */
#define TK_WAYLAND_MENU_STACK_MAX 16

typedef struct {
    TkMenu         *menuPtr;
    TkWaylandPopup *popup;
    int x, y, w, h;   /* toplevel-surface-local rect */
} MenuStackEntry;

static MenuStackEntry menuStack[TK_WAYLAND_MENU_STACK_MAX];
static int            menuStackDepth = 0;
static int menuDismissedByClick = 0;

/* Forward declarations for static functions. */
static void SetHelpMenu(TkMenu *menuPtr);
static void MenuDrawIntoPopup(TkMenu *menuPtr, TkWaylandPopup *popup);
static void MenuDrawMenubarIntoPopup(TkMenu *menuPtr, TkWaylandPopup *popup);
static void MenuStackPop(int toDepth);
static void MenuMouseClick(TkMenu *menuPtr, int x, int y, int button);
static void MenuMouseMotion(TkMenu *menuPtr, int x, int y);
static void MenuMouseLeave(TkMenu *menuPtr);
static void TkpDisplayMenu(void *clientData);
static void TkWaylandMenubarCreateOrResize(TkWindow *winPtr);
static void MenuBarDeferredSetup(void *clientData);
static void MenubarResizeIdleProc(void *clientData);
static void TkWaylandWmUpdateGeometryInfo(void *clientData);
static void TkWaylandWmUpdateGeom(WmInfo *wmPtr, TkWindow *winPtr);

/* Forward declarations for MODULE_SCOPE functions implemented at end of file. */
MODULE_SCOPE void TkWaylandWmUpdateGeom(WmInfo *wmPtr, TkWindow *winPtr);
MODULE_SCOPE void TkWaylandPostVirtualEvent(TkWindow *winPtr, const char *eventName);
MODULE_SCOPE int TkWaylandMenubarHandleClick(TkWindow *winPtr, int x, int y,
    int button);

/* Geometry helper functions. */
static void GetMenuIndicatorGeometry(TkMenu *menuPtr, TkMenuEntry *mePtr,
    Tk_Font tkfont, const Tk_FontMetrics *fmPtr, int *widthPtr, int *heightPtr);
static void GetMenuAccelGeometry(TkMenu *menuPtr, TkMenuEntry *mePtr,
    Tk_Font tkfont, const Tk_FontMetrics *fmPtr, int *widthPtr, int *heightPtr);
static void GetMenuSeparatorGeometry(TkMenu *menuPtr, TkMenuEntry *mePtr,
    Tk_Font tkfont, const Tk_FontMetrics *fmPtr, int *widthPtr, int *heightPtr);
static void GetTearoffEntryGeometry(TkMenu *menuPtr, TkMenuEntry *mePtr,
    Tk_Font tkfont, const Tk_FontMetrics *fmPtr, int *widthPtr, int *heightPtr);
static void GetMenuLabelGeometry(TkMenuEntry *mePtr, Tk_Font tkfont,
    const Tk_FontMetrics *fmPtr, int *widthPtr, int *heightPtr);

/* Drawing helper functions. */
static void DrawMenuEntryBackground(TkMenu *menuPtr, TkMenuEntry *mePtr,
    NVGcontext *vg, Tk_3DBorder activeBorder, Tk_3DBorder bgBorder,
    int x, int y, int width, int height);
static void DrawMenuEntryAccelerator(TkMenu *menuPtr, TkMenuEntry *mePtr,
    NVGcontext *vg, Tk_Font tkfont, const Tk_FontMetrics *fmPtr,
    Tk_3DBorder activeBorder, Tk_3DBorder bgBorder,
    int x, int y, int width, int height, bool drawArrow,
    NVGcolor textColor, Drawable d);
static void DrawMenuEntryIndicator(TkMenu *menuPtr, TkMenuEntry *mePtr,
    NVGcontext *vg, Tk_3DBorder border, XColor *indicatorColor,
    XColor *disableColor, Tk_Font tkfont, const Tk_FontMetrics *fmPtr,
    int x, int y, int width, int height, NVGcolor textColor);
static void DrawMenuSeparator(TkMenu *menuPtr, TkMenuEntry *mePtr,
    NVGcontext *vg, Tk_Font tkfont, const Tk_FontMetrics *fmPtr,
    int x, int y, int width, int height);
static void DrawMenuEntryLabel(TkMenu *menuPtr, TkMenuEntry *mePtr,
    NVGcontext *vg, Tk_Font tkfont, const Tk_FontMetrics *fmPtr,
    int x, int y, int width, int height, NVGcolor textColor, Drawable d);
static void DrawMenuUnderline(TkMenu *menuPtr, TkMenuEntry *mePtr,
    NVGcontext *vg, Tk_Font tkfont, const Tk_FontMetrics *fmPtr,
    int x, int y, int width, int height, NVGcolor textColor, Drawable d);
static void DrawTearoffEntry(TkMenu *menuPtr, TkMenuEntry *mePtr,
    NVGcontext *vg, Tk_Font tkfont, const Tk_FontMetrics *fmPtr,
    int x, int y, int width, int height);

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
 *	Only destroys when the menu is actually being destroyed.
 *	Prevents cascading destruction of the main window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Cleans up menu resources and removes from stack.
 *
 *---------------------------------------------------------------------------
 */

void
TkpDestroyMenu(TkMenu *menuPtr)
{
    TkWindow *winPtr;
    WmInfo *wmPtr;
    int i;
    
    if (!menuPtr) return;
    
    MENU_LOG("TkpDestroyMenu called for menu %p", (void*)menuPtr);
    
    winPtr = (TkWindow *)menuPtr->tkwin;
    if (!winPtr) return;
    
    wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    if (!wmPtr) return;
    
    /* Check if this is the menubar menu - if so, just clean up without destroying the parent. */
    if (wmPtr->menubarMenuPtr == menuPtr) {
        MENU_LOG("TkpDestroyMenu: destroying menubar menu, cleaning up popup only");
        if (wmPtr->menubarPopup) {
            if (wmPtr->popup == wmPtr->menubarPopup) {
                wmPtr->popup = NULL;
            }
            TkWaylandPopupDestroy(wmPtr->menubarPopup);
            wmPtr->menubarPopup = NULL;
        }
        wmPtr->menubar = NULL;
        wmPtr->menubarMenuPtr = NULL;
        wmPtr->menuHeight = 0;
        if (winPtr) {
            winPtr->internalBorderTop = 0;
        }
        /* Do NOT destroy the main window - just clean up the menubar resources. */
        return;
    }
    
    /* Clean up this menu from the menu stack. */
    for (i = 0; i < menuStackDepth; i++) {
        if (menuStack[i].menuPtr == menuPtr) {
            if (menuStack[i].popup) {
                TkWaylandPopupDestroy(menuStack[i].popup);
            }
            menuStack[i].menuPtr = NULL;
            menuStack[i].popup = NULL;
            break;
        }
    }
    
    /* For non-menubar menus, just clean up the popup. */
    if (wmPtr->popup) {
        TkWaylandPopupDestroy(wmPtr->popup);
        wmPtr->popup = NULL;
    }
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
 *	Nothing to do on Wayland.
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
    *heightPtr += 4;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpSetWindowMenuBar --
 *
 *	Attach or detach a menubar for a toplevel.
 *	Preserves the menubar popup if it already exists.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates or updates wmPtr->menubarPopup.
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

    MENU_LOG("TkpSetWindowMenuBar called for %s", Tk_PathName(tkwin));

    if (!wmPtr) return;

    /* Only destroy menubar popup if we're actually removing the menubar */
    if (!menuPtr) {
        if (wmPtr->menubarPopup) {
            if (wmPtr->popup == wmPtr->menubarPopup) {
                wmPtr->popup = NULL;
            }
            TkWaylandPopupDestroy(wmPtr->menubarPopup);
            wmPtr->menubarPopup = NULL;
        }
        wmPtr->menubar        = NULL;
        wmPtr->menubarMenuPtr = NULL;
        wmPtr->menuHeight     = 0;
        winPtr->internalBorderTop = 0;
        TkWaylandWmUpdateGeom(wmPtr, winPtr);
        return;
    }

    /* If we already have a menubar, just update it without destroying */
    if (wmPtr->menubarMenuPtr == menuPtr && wmPtr->menubarPopup) {
        MENU_LOG("TkpSetWindowMenuBar: updating existing menubar");
        TkRecomputeMenu(menuPtr);
        wmPtr->menuHeight = menuPtr->totalHeight;
        if (wmPtr->menuHeight < 20) wmPtr->menuHeight = 24;
        winPtr->internalBorderTop = wmPtr->menuHeight;
        TkWaylandWmUpdateGeom(wmPtr, winPtr);
        
        /* Just resize/redraw the existing popup */
        if (!(wmPtr->flags & WM_NEVER_MAPPED)) {
            TkWaylandMenubarCreateOrResize(winPtr);
        }
        return;
    }

    /* New menubar - create it */
    wmPtr->menubar        = (Tk_Window)menuPtr->tkwin;
    wmPtr->menubarMenuPtr = menuPtr;

    TkRecomputeMenu(menuPtr);
    wmPtr->menuHeight = menuPtr->totalHeight;
    if (wmPtr->menuHeight < 20) wmPtr->menuHeight = 24;

    winPtr->internalBorderTop = wmPtr->menuHeight;
    TkWaylandWmUpdateGeom(wmPtr, winPtr);

    if (wmPtr->flags & WM_NEVER_MAPPED) {
        MENU_LOG("TkpSetWindowMenuBar: deferring menubar setup "
            "(toplevel not yet mapped)");
        Tcl_DoWhenIdle(MenuBarDeferredSetup, (void *)winPtr);
        return;
    }

    TkWaylandMenubarCreateOrResize(winPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandMenubarCreateOrResize --
 *
 *	Create or resize the menubar subsurface.
 *	Preserves the existing popup if size hasn't changed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May resize or recreate wmPtr->menubarPopup.
 *
 *---------------------------------------------------------------------------
 */

static void
TkWaylandMenubarCreateOrResize(
    TkWindow *winPtr)
{
    WmInfo     *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    TkMenu     *menuPtr;
    GLFWwindow *glfwWindow;
    int         width = 0;
    int         mbW, mbH;

    if (!wmPtr || !wmPtr->menubar || !wmPtr->menubarMenuPtr) {
        return;
    }
    menuPtr = wmPtr->menubarMenuPtr;

    glfwWindow = TkWaylandGetGLFWwindow(winPtr);
    if (!glfwWindow) {
        MENU_LOG("TkWaylandMenubarCreateOrResize: no GLFW window");
        return;
    }

    int gw = 0, gh = 0;
    glfwGetWindowSize(glfwWindow, &gw, &gh);
    if (gw <= 0) {
        gw = Tk_Width((Tk_Window)winPtr);
    }
    if (gw <= 0) {
        gw = Tk_ReqWidth((Tk_Window)winPtr);
    }
    if (gw <= 0) {
        gw = 200;
    }
    width = gw;

    TkRecomputeMenu(menuPtr);
    mbH = menuPtr->totalHeight;
    if (mbH < 20) mbH = 24;
    wmPtr->menuHeight = mbH;
    mbW = width;

    if (winPtr->internalBorderTop != mbH) {
        winPtr->internalBorderTop = mbH;
        TkWaylandWmUpdateGeom(wmPtr, winPtr);
    }

    /* Check if we already have a valid popup */
    if (wmPtr->menubarPopup) {
        int curW, curH;
        TkWaylandPopupGetSize(wmPtr->menubarPopup, &curW, &curH);
        
        /* If size matches, just redraw and return */
        if (curW == mbW && curH == mbH) {
            MenuDrawMenubarIntoPopup(menuPtr, wmPtr->menubarPopup);
            wmPtr->popup = wmPtr->menubarPopup;
            return;
        }
        
        /* Size changed - try to resize the popup */
        MENU_LOG("TkWaylandMenubarCreateOrResize: resizing popup from %dx%d to %dx%d",
                 curW, curH, mbW, mbH);
        
        if (TkWaylandPopupResize(wmPtr->menubarPopup, mbW, mbH) == TCL_OK) {
            MenuDrawMenubarIntoPopup(menuPtr, wmPtr->menubarPopup);
            wmPtr->popup = wmPtr->menubarPopup;
            return;
        }
        
        /* If resize failed, destroy and recreate */
        MENU_LOG("TkWaylandMenubarCreateOrResize: resize failed, recreating popup");
        if (wmPtr->popup == wmPtr->menubarPopup) {
            wmPtr->popup = NULL;
        }
        TkWaylandPopupDestroy(wmPtr->menubarPopup);
        wmPtr->menubarPopup = NULL;
    }

    /* Create new popup */
    wmPtr->menubarPopup = TkWaylandSubsurfaceCreate(
        glfwWindow, 0, 0, mbW, mbH);

    if (!wmPtr->menubarPopup) {
        fprintf(stderr, "TkWaylandMenubarCreateOrResize: failed to create menubar subsurface\n");
        return;
    }
    wmPtr->popup = wmPtr->menubarPopup;

    MenuDrawMenubarIntoPopup(menuPtr, wmPtr->menubarPopup);
}

/*
 *---------------------------------------------------------------------------
 *
 * MenuBarDeferredSetup --
 *
 *	Tcl_DoWhenIdle callback for deferred menubar setup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May create or update wmPtr->menubarPopup.
 *
 *---------------------------------------------------------------------------
 */

static void
MenuBarDeferredSetup(
    void *clientData)
{
    TkWindow *winPtr = (TkWindow *)clientData;
    WmInfo   *wmPtr  = (WmInfo *)winPtr->wmInfoPtr;

    if (!wmPtr || !wmPtr->menubarMenuPtr) {
        return;
    }

    if (wmPtr->flags & WM_NEVER_MAPPED) {
        MENU_LOG("MenuBarDeferredSetup: still not mapped, rescheduling");
        Tcl_DoWhenIdle(MenuBarDeferredSetup, clientData);
        return;
    }

    TkWaylandMenubarCreateOrResize(winPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * MenubarResizeIdleProc --
 *
 *	Tcl_DoWhenIdle callback for menubar resize.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May resize or recreate wmPtr->menubarPopup.
 *
 *---------------------------------------------------------------------------
 */

static void
MenubarResizeIdleProc(
    void *clientData)
{
    TkWindow *winPtr = (TkWindow *)clientData;
    WmInfo   *wmPtr  = (WmInfo *)winPtr->wmInfoPtr;

    if (!wmPtr || !wmPtr->menubarMenuPtr) {
        return;
    }
    
    if (wmPtr->flags & WM_NEVER_MAPPED) {
        Tcl_DoWhenIdle(MenubarResizeIdleProc, clientData);
        return;
    }
    TkWaylandMenubarCreateOrResize(winPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandMenubarResize --
 *
 *	Called when a toplevel's size has changed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Schedules an idle call to resize the menubar.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandMenubarResize(
    TkWindow *winPtr)
{
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;

    if (!wmPtr || !wmPtr->menubarMenuPtr) {
        return;
    }
    if (wmPtr->flags & WM_NEVER_MAPPED) {
        return;
    }

    Tcl_CancelIdleCall(MenubarResizeIdleProc, (void *)winPtr);
    Tcl_DoWhenIdle(MenubarResizeIdleProc, (void *)winPtr);
}

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
    
    /* Safety check. */
    if (!mePtr || !mePtr->menuPtr || !mePtr->menuPtr->tkwin) return;
    
    /* Get the actual borders from the menu. */
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
    if (!winPtr || !winPtr->wmInfoPtr) return;
    
    WmInfo *wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    TkWaylandPopup *popup = NULL;
    
    if (mePtr->menuPtr->menuType == MENUBAR) {
        TkWindow *tw = (TkWindow *)Tk_Parent((Tk_Window)winPtr);
        while (tw && !(tw->flags & TK_TOP_LEVEL))
            tw = (TkWindow *)Tk_Parent((Tk_Window)tw);
        if (tw && tw->wmInfoPtr)
            popup = ((WmInfo *)tw->wmInfoPtr)->menubarPopup;
    } else {
        popup = wmPtr->popup;
    }
    
    if (!popup) return;
    
    vg = TkWaylandPopupGetNVGContext(popup);
    if (!vg) return;
    
    /* Get the font for this entry. */
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
    
    /* Get text color from the menu's border. */
    NVGcolor textColor = nvgRGBA(0, 0, 0, 255);
    if (bgBorder) {
        XColor *fgX = Tk_3DBorderColor(bgBorder);
        if (fgX) {
            textColor = TkWaylandXColorToNVG(fgX);
        }
    } else if (mePtr->menuPtr->fgPtr) {
        XColor *fgX = Tk_GetColorFromObj(mePtr->menuPtr->tkwin, mePtr->menuPtr->fgPtr);
        if (fgX) {
            textColor = TkWaylandXColorToNVG(fgX);
        }
    }

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
			   x, adjustedY, width, adjustedHeight, textColor, d); 
	DrawMenuEntryAccelerator(mePtr->menuPtr, mePtr, vg, entryFont, entryFmPtr, 
				 activeBorder, bgBorder, x, adjustedY, width, adjustedHeight, 
				 (drawingParameters & DRAW_MENU_ENTRY_ARROW) != 0, textColor, d);
	if (!mePtr->hideMargin) { 
	    XColor *indicatorColor = NULL;
	    XColor *disableColor = NULL;
	    
	    if (bgBorder) {
	        indicatorColor = Tk_3DBorderColor(bgBorder);
	    }
	    
	    if (mePtr->menuPtr->disabledFgPtr) {
	        disableColor = Tk_GetColorFromObj(mePtr->menuPtr->tkwin, 
	                                          mePtr->menuPtr->disabledFgPtr);
	    }
	    
	    DrawMenuEntryIndicator(mePtr->menuPtr, mePtr, vg, 
				   bgBorder, indicatorColor, disableColor,
				   entryFont, entryFmPtr, x, adjustedY, width, adjustedHeight, 
				   textColor); 
	} 
    } 
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
	fillColor = TkWaylandXColorToNVG(Tk_3DBorderColor(activeBorder));
    } else {
	fillColor = TkWaylandXColorToNVG(Tk_3DBorderColor(bgBorder));
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
	    nvgStrokeColor(vg, TkWaylandXColorToNVG(Tk_3DBorderColor(border)));
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
			 NVGcolor textColor,
			 Drawable d)
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
	
	if (mePtr->state == ENTRY_ACTIVE) {
	    nvgFillColor(vg, TkWaylandXColorToNVG(Tk_3DBorderColor(activeBorder)));
	} else {
	    nvgFillColor(vg, TkWaylandXColorToNVG(Tk_3DBorderColor(bgBorder)));
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
	
	if (d) {
	    /* Use Tk_DrawCharsInContext for proper font rendering when we have a drawable */
	    GC gc = Tk_GetGC(menuPtr->tkwin, 0, NULL);
	    if (gc) {
		XColor xc;
		xc.red = (unsigned short)(textColor.r * 65535.0f);
		xc.green = (unsigned short)(textColor.g * 65535.0f);
		xc.blue = (unsigned short)(textColor.b * 65535.0f);
		xc.flags = DoRed | DoGreen | DoBlue;
		XSetForeground(menuPtr->display, gc, xc.pixel);
		
		Tk_DrawCharsInContext(menuPtr->display, d, gc, tkfont,
		                      accel, strlen(accel), 0, strlen(accel),
		                      left, ty);
		Tk_FreeGC(menuPtr->display, gc);
	    }
	} else {
	    /* For popup menus (d==0), use the NVG context directly */
	    WaylandFont *fontPtr = (WaylandFont *)tkfont;
	    
	    /* Ensure fonts are loaded into this NVG context */
	    int fontId = EnsureNvgFont(fontPtr, vg);
	    if (fontId >= 0) {
		nvgFontFaceId(vg, fontId);
		nvgFontSize(vg, (float)fontPtr->pixelSize);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		nvgFillColor(vg, textColor);
		nvgText(vg, (float)left, (float)ty, accel, NULL);
	    } else {
		nvgFontSize(vg, (float)fontPtr->pixelSize);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		nvgFillColor(vg, textColor);
		nvgText(vg, (float)left, (float)ty, accel, NULL);
	    }
	}
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
	    TkWaylandXColorToNVG(disableColor) : TkWaylandXColorToNVG(indicatorColor);
	
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
	    TkWaylandXColorToNVG(disableColor) : TkWaylandXColorToNVG(indicatorColor);
	
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
 *	Uses Tk_DrawCharsInContext for text via the proper font pipeline.
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
		   NVGcolor textColor,
		   Drawable d)
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

    /* Draw image using X11 functions. */
    if (haveImage) {
	int imageX = leftEdge + imageXOffset;
	int imageY = y + (height - imageHeight) / 2 + imageYOffset;
	
	if (imageX < x) imageX = x;
	if (imageY < y) imageY = y;
	if (imageX + imageWidth > x + width) imageX = x + width - imageWidth;
	if (imageY + imageHeight > y + height) imageY = y + height - imageHeight;
	
	if (mePtr->image != NULL) {
	    /* Only draw images if we have a valid drawable */
	    if (d) {
		GC gc = Tk_GetGC(menuPtr->tkwin, 0, NULL);
		if (gc) {
		    Tk_RedrawImage(mePtr->image, 0, 0, imageWidth, imageHeight,
				   d, imageX, imageY);
		    Tk_FreeGC(menuPtr->display, gc);
		}
	    }
	} else if (mePtr->bitmapPtr != NULL) {
	    if (d) {
		Pixmap bitmap = Tk_GetBitmapFromObj(menuPtr->tkwin, mePtr->bitmapPtr);
		if (bitmap) {
		    GC gc = Tk_GetGC(menuPtr->tkwin, 0, NULL);
		    if (gc) {
			XCopyArea(menuPtr->display, bitmap, d, gc,
				  0, 0, imageWidth, imageHeight, imageX, imageY);
			Tk_FreeGC(menuPtr->display, gc);
		    }
		}
	    }
	}
    }

    /* Draw text label. */
    if ((mePtr->compound != COMPOUND_NONE) || !haveImage) { 
	int textY = y + height / 2 + textYOffset + (fmPtr->ascent - fmPtr->descent) / 2;
	
	if (mePtr->labelLength > 0) { 
	    const char *label = Tcl_GetString(mePtr->labelPtr); 
	    
	    if (d) {
		/* Use Tk_DrawCharsInContext for proper font rendering when we have a drawable */
		GC gc = Tk_GetGC(menuPtr->tkwin, 0, NULL);
		if (gc) {
		    XColor xc;
		    xc.red = (unsigned short)(textColor.r * 65535.0f);
		    xc.green = (unsigned short)(textColor.g * 65535.0f);
		    xc.blue = (unsigned short)(textColor.b * 65535.0f);
		    xc.flags = DoRed | DoGreen | DoBlue;
		    XSetForeground(menuPtr->display, gc, xc.pixel);
		    
		    Tk_DrawCharsInContext(menuPtr->display, d, gc, tkfont,
		                          label, mePtr->labelLength,
		                          0, mePtr->labelLength,
		                          leftEdge + textXOffset, textY);
		    Tk_FreeGC(menuPtr->display, gc);
		}
	    } else {
		/* For popup menus (d==0), use the NVG context directly */
		WaylandFont *fontPtr = (WaylandFont *)tkfont;
		
		/* Ensure fonts are loaded into this NVG context */
		int fontId = EnsureNvgFont(fontPtr, vg);
		if (fontId >= 0) {
		    nvgFontFaceId(vg, fontId);
		    nvgFontSize(vg, (float)fontPtr->pixelSize);
		    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		    nvgFillColor(vg, textColor);
		    nvgText(vg, (float)(leftEdge + textXOffset), (float)textY, label, NULL);
		} else {
		    /* Fallback: just use the font as-is */
		    nvgFontSize(vg, (float)fontPtr->pixelSize);
		    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		    nvgFillColor(vg, textColor);
		    nvgText(vg, (float)(leftEdge + textXOffset), (float)textY, label, NULL);
		}
	    }
	    
	    DrawMenuUnderline(menuPtr, mePtr, vg, tkfont, fmPtr, 
			      x + textXOffset, y + textYOffset, width, height, 
			      textColor, d); 
	} 
    } 

    /* Draw disabled overlay using stippling. */
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
 *	Renders a line under the specified character using NanoVG.
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
		  NVGcolor textColor,
		  Drawable d)
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
	    
	    /* Calculate underline position. */
	    underlineX = leftEdge + Tk_TextWidth(tkfont, label, start - label);
	    underlineWidth = Tk_TextWidth(tkfont, start, end - start);
	    
	    /* Draw underline using NanoVG. */
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

    /* Draw dashed line using NanoVG. */
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
 * Results:
 *	A standard Tcl result code.
 *
 * Side effects:
 *	Dismisses any previously posted menu stack, then posts menuPtr as
 *	the new root.
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
    int popupW, popupH;

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

    popupW = menuPtr->totalWidth;
    popupH = menuPtr->totalHeight;
    if (popupW <= 0) popupW = 1;
    if (popupH <= 0) popupH = 1;

    MENU_LOG("TkpPostMenu: popup size %dx%d", popupW, popupH);

    if (!mainGlfwWindow) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
            "Cannot post menu: no main GLFW window", -1));
        return TCL_ERROR;
    }

    return TkWaylandPostMenuAtAnchor(interp, menuPtr,
        x, y, 1, 1, popupW, popupH, 1);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpMenuButtonPostMenu --
 *
 *     Post a menu from a menubutton at the appropriate position.
 *
 * Results:
 *     Returns TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *     Posts the menu as a popup anchored to the menubutton.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE int
TkpMenuButtonPostMenu(
    TkMenuButton *mbPtr)
{
    Tcl_Interp *interp;
    TkMenu *menuPtr;
    TkWindow *buttonWin;
    TkMenuReferences *menuRefPtr;
    int x, y, btnW, btnH, popupW, popupH;
    
    if (!mbPtr) {
        return TCL_ERROR;
    }
    
    interp = mbPtr->interp;
    buttonWin = (TkWindow *)mbPtr->tkwin;
    
    if (!interp || !buttonWin) {
        return TCL_ERROR;
    }

    MENU_LOG("TkpMenuButtonPostMenu: button=%s", Tk_PathName((Tk_Window)buttonWin));

    if (!mbPtr->menuNameObj) {
        MENU_LOG("TkpMenuButtonPostMenu: no menu name");
        return TCL_ERROR;
    }
    
    menuRefPtr = TkFindMenuReferencesObj(interp, mbPtr->menuNameObj);
    if (!menuRefPtr || !menuRefPtr->menuPtr) {
        MENU_LOG("TkpMenuButtonPostMenu: menu '%s' not found", 
                 Tcl_GetString(mbPtr->menuNameObj));
        return TCL_ERROR;
    }
    
    menuPtr = menuRefPtr->menuPtr;
    if (!menuPtr) {
        MENU_LOG("TkpMenuButtonPostMenu: menuPtr is NULL");
        return TCL_ERROR;
    }

    int rootX, rootY;
    Tk_GetRootCoords((Tk_Window)buttonWin, &rootX, &rootY);
    x    = rootX;
    y    = rootY;
    btnW = Tk_Width((Tk_Window)buttonWin);
    btnH = Tk_Height((Tk_Window)buttonWin);

    TkRecomputeMenu(menuPtr);
    popupW = menuPtr->totalWidth;
    popupH = menuPtr->totalHeight;
    if (popupW <= 0) popupW = 1;
    if (popupH <= 0) popupH = 1;

    MENU_LOG("TkpMenuButtonPostMenu: button at (%d,%d) %dx%d, popup %dx%d",
             x, y, btnW, btnH, popupW, popupH);

    return TkWaylandPostMenuAtAnchor(interp, menuPtr,
        x, y + btnH, btnW, 1,
        popupW, popupH, 1);
}

/*
 *---------------------------------------------------------------------------
 *
 * MenuStackPop --
 *
 *	Destroy and remove all menu stack entries from index toDepth onward.
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
 *	Tear down the entire menu stack.
 *	Does NOT destroy the menubar popup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys all popup menus.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandMenuDismissAll(void)
{
    MENU_LOG("TkWaylandMenuDismissAll called");

    if (menuStackDepth == 0) {
        return;
    }

    MenuStackPop(0);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandMenubarDestroy --
 *
 *	Explicitly destroy the menubar popup.
 *	This should only be called when the toplevel is actually being destroyed.
 *	Preserves the main window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys wmPtr->menubarPopup.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandMenubarDestroy(
    TkWindow *winPtr)
{
    WmInfo *wmPtr;
    
    if (!winPtr) return;
    wmPtr = (WmInfo *)winPtr->wmInfoPtr;
    if (!wmPtr) return;
    
    MENU_LOG("TkWaylandMenubarDestroy: destroying menubar popup for %s", 
             Tk_PathName((Tk_Window)winPtr));
    
    if (wmPtr->menubarPopup) {
        if (wmPtr->popup == wmPtr->menubarPopup) {
            wmPtr->popup = NULL;
        }
        TkWaylandPopupDestroy(wmPtr->menubarPopup);
        wmPtr->menubarPopup = NULL;
    }
    wmPtr->menubar = NULL;
    wmPtr->menubarMenuPtr = NULL;
    wmPtr->menuHeight = 0;
    if (winPtr) {
        winPtr->internalBorderTop = 0;
    }
    /* Do NOT destroy the main window. */
}
/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandPostMenuAtAnchor --
 *
 *	Core menu posting routine.
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
 *----------------------------------------------------------------------
 *
 * MenuDrawIntoPopup --
 *
 *	Render all entries of menuPtr into the given TkWaylandPopup.
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

    /* Get menu's default font */
    menuFont = Tk_GetFontFromObj(menuPtr->tkwin, menuPtr->fontPtr);
    if (menuFont) {
        Tk_GetFontMetrics(menuFont, &menuMetrics);
        MENU_LOG("MenuDrawIntoPopup: menu font size=%d", menuMetrics.linespace);
    } else {
        MENU_LOG("MenuDrawIntoPopup: no menu font, using default");
        menuMetrics.linespace = 16;
        menuMetrics.ascent = 12;
        menuMetrics.descent = 4;
    }

    /* Draw background using real Tk configuration */
    Tk_3DBorder bgBorder = NULL;
    if (menuPtr->borderPtr != NULL) {
        bgBorder = Tk_Get3DBorderFromObj(menuPtr->tkwin, menuPtr->borderPtr);
    }
    if (!bgBorder) {
        bgBorder = Tk_Get3DBorder(menuPtr->interp, menuPtr->tkwin, Tk_GetUid("Menu"));
    }

    NVGcolor bgColor = nvgRGB(240, 240, 240);
    NVGcolor borderColor = nvgRGB(160, 160, 160);

    if (bgBorder) {
        XColor *bgColorX = Tk_3DBorderColor(bgBorder);
        bgColor = TkWaylandXColorToNVG(bgColorX);
        borderColor = nvgRGB(
            (bgColorX->red >> 8) * 0.7,
            (bgColorX->green >> 8) * 0.7,
            (bgColorX->blue >> 8) * 0.7
        );
    }

    /* Draw background */
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, (float)menuW, (float)menuH);
    nvgFillColor(vg, bgColor);
    nvgFill(vg);
    MENU_LOG("MenuDrawIntoPopup: drew background");

    /* Draw border */
    if (menuPtr->menuType == MENUBAR) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, 0, (float)menuH - 0.5f);
        nvgLineTo(vg, (float)menuW, (float)menuH - 0.5f);
        nvgStrokeColor(vg, borderColor);
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);
    } else {
        nvgBeginPath(vg);
        nvgRect(vg, 0.5f, 0.5f, (float)menuW - 1.0f, (float)menuH - 1.0f);
        nvgStrokeColor(vg, borderColor);
        nvgStrokeWidth(vg, 1.0f);
        nvgStroke(vg);
    }

    Drawable d = Tk_WindowId(menuPtr->tkwin);
    MENU_LOG("MenuDrawIntoPopup: drawing %d entries", menuPtr->numEntries);
    
    for (i = 0; i < menuPtr->numEntries; i++) {
        TkMenuEntry *mePtr = menuPtr->entries[i];
        if (!mePtr) continue;
        
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
 *	Render a menubar into its strip popup.
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
 *	Called by Tk's display system to redraw a posted menu.
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

    if (!wmPtr || !wmPtr->menubarPopup) {
        MENU_LOG("TkpDisplayMenu: no menubar popup surface");
        return;
    }

    Tcl_CancelIdleCall((Tcl_IdleProc *)TkpDisplayMenu, clientData);
    MenuDrawIntoPopup(menuPtr, wmPtr->menubarPopup);
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
 *
 * Results:
 *	Returns a standard Tcl result code.
 *
 * Side effects:
 *	The menu window is mapped at the computed screen position.
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
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
 *
 * TkWaylandSetupMenuCallbacks --
 *
 *	Setup menu input callbacks (no-op on Wayland).
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
TkWaylandSetupMenuCallbacks(
			    TCL_UNUSED(Tk_Window)) /* tkwin */
{
    /* No-op */
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandMenuPopupActive --
 *
 *	Query whether one or more menu popups are currently posted.
 *
 * Results:
 *	Non-zero if the menu stack is non-empty.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandMenuPopupActive(void)
{
    return menuStackDepth > 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandMenuConsumeDismissClick --
 *
 *	Returns non-zero exactly once if the most recent button press
 *	dismissed the menu stack.
 *
 * Results:
 *	1 if a dismiss click was consumed, 0 otherwise.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandMenuConsumeDismissClick(void)
{
    int v = menuDismissedByClick;
    menuDismissedByClick = 0;
    return v;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandMenubarHandleClick --
 *
 *	Hit-test a button press against the menubar of winPtr and, if it
 *	lands on a cascade entry, post the corresponding top-level menu.
 *
 *	This is intentionally independent of menuStack[]/MenuStackFindLevel:
 *	the menubar itself is never pushed onto the popup stack (it is not
 *	a popup, it's always-visible chrome owned by the toplevel), so the
 *	stack-relative cascade-posting logic used by MenuMouseClick /
 *	MenuMouseMotion for submenu-of-submenu clicks does not apply here.
 *	Posting with isRoot=1 makes the clicked top-level menu become
 *	menuStack[0], so everything below it (submenus of submenus) is
 *	handled normally by the existing stack-based logic afterward.
 *
 * Results:
 *	1 if the click landed on the menubar (whether or not it hit a
 *	postable cascade entry), 0 if the click was outside the menubar
 *	and should be handled as an ordinary Tk event.
 *
 * Side effects:
 *	May post a new root menu (TkWaylandPostMenuAtAnchor), which
 *	dismisses any previously posted menu stack.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandMenubarHandleClick(
    TkWindow *winPtr,
    int x, int y,
    int button)
{
    WmInfo *wmPtr;
    TkMenu *menuPtr;
    int i;

    if (!winPtr) {
        return 0;
    }
    wmPtr = (WmInfo *)winPtr->wmInfoPtr;

    if (!wmPtr || !wmPtr->menubarMenuPtr || !wmPtr->menubarPopup) {
        return 0;
    }
    if (button != 1) {
        return 0;
    }
    if (x < 0 || y < 0 || y >= wmPtr->menuHeight) {
        return 0;
    }

    menuPtr = wmPtr->menubarMenuPtr;

    MENU_LOG("TkWaylandMenubarHandleClick: x=%d y=%d menuHeight=%d",
             x, y, wmPtr->menuHeight);

    for (i = 0; i < menuPtr->numEntries; i++) {
        TkMenuEntry *mePtr = menuPtr->entries[i];
        if (!mePtr) {
            continue;
        }
        if (x < mePtr->x || x >= mePtr->x + mePtr->width) {
            continue;
        }

        /* Click landed on entry i of the menubar. */
        if (mePtr->state == ENTRY_DISABLED) {
            return 1;
        }

        if (mePtr->type == CASCADE_ENTRY && mePtr->namePtr != NULL) {
            TkMenuReferences *menuRefPtr = TkFindMenuReferencesObj(
                menuPtr->interp, mePtr->namePtr);

            if (menuRefPtr && menuRefPtr->menuPtr) {
                TkMenu *cascadePtr = menuRefPtr->menuPtr;
                int cascadeW, cascadeH;

                TkRecomputeMenu(cascadePtr);
                cascadeW = cascadePtr->totalWidth;
                cascadeH = cascadePtr->totalHeight;
                if (cascadeW <= 0) cascadeW = 1;
                if (cascadeH <= 0) cascadeH = 1;

                TkActivateMenuEntry(menuPtr, i);
                menuPtr->postedCascade = mePtr;

                MENU_LOG("TkWaylandMenubarHandleClick: posting cascade "
                         "'%s' at (%d,%d) %dx%d",
                         Tcl_GetString(mePtr->namePtr),
                         mePtr->x, wmPtr->menuHeight, cascadeW, cascadeH);

                /*
                 * isRoot=1: dismisses any existing stack and pushes
                 * cascadePtr as menuStack[0].
                 */
                TkWaylandPostMenuAtAnchor(menuPtr->interp, cascadePtr,
                    mePtr->x, wmPtr->menuHeight, 0, 0,
                    cascadeW, cascadeH, /*isRoot=*/1);

                TkEventuallyRedrawMenu(menuPtr, NULL);
            }
        }

        return 1;
    }

    return 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandMenuHandlePointerMotion --
 *
 *	Called from the raw wl_pointer listener on motion events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Activates/deactivates menu entries and manages cascade popups.
 *
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
 *
 * TkWaylandMenuHandlePointerButton --
 *
 *	Called from the raw wl_pointer listener on button events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May invoke menu commands or dismiss menu stack.
 *
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
 *
 * TkWaylandMenuHandleEscape --
 *
 *	Called from the raw wl_keyboard listener when Escape is pressed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Dismisses all posted menus.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandMenuHandleEscape(void)
{
    TkWaylandMenuDismissAll();
}

/* Helper functions for menus. */

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmUpdateGeom --
 *
 *	Notify the WM layer that geometry has changed and schedule an
 *	UpdateGeometryInfo idle pass.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets WM_UPDATE_SIZE_HINTS and schedules geometry update.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandWmUpdateGeom(
    WmInfo *wmPtr,
    TkWindow *winPtr)
{
    if (!wmPtr || !winPtr) return;
    
    MENU_LOG("TkWaylandWmUpdateGeom called for %s", Tk_PathName((Tk_Window)winPtr));
    
    wmPtr->flags |= WM_UPDATE_SIZE_HINTS;
    
    /* Validate config values before applying. */
    int width = wmPtr->configWidth;
    int height = wmPtr->configHeight;
    
    if (width <= 0 || height <= 0 || width > 10000 || height > 10000) {
        width = Tk_ReqWidth((Tk_Window)winPtr);
        height = Tk_ReqHeight((Tk_Window)winPtr);
        if (width <= 0 || height <= 0 || width > 10000 || height > 10000) {
            width = 200;
            height = 200;
        }
        wmPtr->configWidth = width;
        wmPtr->configHeight = height;
    }
    
    /* Apply geometry changes immediately if possible */
    if (width > 0 && height > 0 && width <= 10000 && height <= 10000) {
        GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
        if (glfwWindow) {
            glfwSetWindowSize(glfwWindow, width, height);
            MENU_LOG("TkWaylandWmUpdateGeom: set size to %dx%d", 
                     width, height);
        }
    }
    
    /* Schedule the idle callback for any other updates */
    Tcl_CancelIdleCall((Tcl_IdleProc *)TkWaylandWmUpdateGeometryInfo, (void *)wmPtr);
    Tcl_DoWhenIdle((Tcl_IdleProc *)TkWaylandWmUpdateGeometryInfo, (void *)wmPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmUpdateGeometryInfo --
 *
 *	Idle callback to update window geometry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the window geometry.
 *
 *----------------------------------------------------------------------
 */

static void
TkWaylandWmUpdateGeometryInfo(
    void *clientData)
{
    WmInfo *wmPtr = (WmInfo *)clientData;
    TkWindow *winPtr;
    
    if (!wmPtr) return;
    
    winPtr = (TkWindow *)wmPtr->winPtr;
    if (!winPtr) return;
    
    wmPtr->flags &= ~WM_UPDATE_SIZE_HINTS;
    
    /* Apply the geometry changes. */
    if (wmPtr->configWidth > 0 && wmPtr->configHeight > 0) {
        GLFWwindow *glfwWindow = TkWaylandGetGLFWwindow(winPtr);
        if (glfwWindow) {
            glfwSetWindowSize(glfwWindow, wmPtr->configWidth, wmPtr->configHeight);
            MENU_LOG("TkWaylandWmUpdateGeometryInfo: set size to %dx%d", 
                     wmPtr->configWidth, wmPtr->configHeight);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPostVirtualEvent --
 *
 *	Post a virtual event (like <<MenuDone>>) on the given window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Posts the virtual event to Tk's event system.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPostVirtualEvent(
    TkWindow *winPtr,
    const char *eventName)
{
    Tcl_Interp *interp;
    TkMainInfo *info;
    char *eventScript;
    const char *eventNameWithoutBrackets;
    size_t len;
    int result;
    
    if (!winPtr || !eventName) {
        MENU_LOG("TkWaylandPostVirtualEvent: invalid parameters");
        return;
    }
    
    info = TkGetMainInfoList();
    if (!info || !info->interp) {
        MENU_LOG("TkWaylandPostVirtualEvent: no interpreter found");
        return;
    }
    interp = info->interp;
    
    /* Remove leading "<<" and trailing ">>" from event name */
    eventNameWithoutBrackets = eventName + 2;
    len = strlen(eventNameWithoutBrackets);
    if (len > 0 && eventNameWithoutBrackets[len-1] == '>') {
        len--;
    }
    
    /* Build the Tcl script to post the virtual event */
    eventScript = (char *)ckalloc(len + 64);
    if (!eventScript) {
        MENU_LOG("TkWaylandPostVirtualEvent: memory allocation failed");
        return;
    }
    
    sprintf(eventScript, "event generate %s <%*s>", 
            Tk_PathName((Tk_Window)winPtr), (int)len, eventNameWithoutBrackets);
    
    MENU_LOG("TkWaylandPostVirtualEvent: posting %s via '%s'", 
             eventName, eventScript);
    
    result = Tcl_EvalEx(interp, eventScript, -1, TCL_EVAL_GLOBAL);
    if (result != TCL_OK) {
        MENU_LOG("TkWaylandPostVirtualEvent: Tcl_Eval failed: %s", 
                 Tcl_GetStringResult(interp));
    }
    
    ckfree(eventScript);
}

/* 
 * Local Variables: 
 * mode: c 
 * c-basic-offset: 4 
 * fill-column: 78 
 * End: 
 */
