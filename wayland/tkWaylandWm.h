/*
 * tkMacOSXWm.h --
 *
 *      Declarations of Wayland-specific window manager structure.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKWAYLANDWM
#define _TKWAYLANDWM

#include "tkWaylandInt.h"
#include "tkMenu.h"
#include "tkMenubutton.h"

/*
 *----------------------------------------------------------------------
 *
 * ProtocolHandler – per-protocol Tcl command binding.
 *
 *----------------------------------------------------------------------
 */

typedef struct ProtocolHandler {
    int                    protocol;  /* Protocol identifier. */
    struct ProtocolHandler *nextPtr;
    Tcl_Interp            *interp;
    char                   command[TKFLEXARRAY];
} ProtocolHandler;

#define HANDLER_SIZE(cmdLength) \
    (offsetof(ProtocolHandler, command) + 1 + (cmdLength))

/*
 *----------------------------------------------------------------------
 *
 * TkWmInfo – per-toplevel window manager state.
 *
 *----------------------------------------------------------------------
 */

typedef struct TkWmInfo {
    TkWindow    *winPtr;        /* Tk window. */
    GLFWwindow  *glfwWindow;    /* GLFW handle (NULL until first map). */
    char        *title;
    char        *iconName;
    char        *leaderName;
    TkWindow    *containerPtr;  /* Transient-for container. */
    Tk_Window    icon;
    Tk_Window    iconFor;
    int          withdrawn;
    int          initialState;  /* NormalState, IconicState, WithdrawnState */

    /* Wrapper / menubar. */
    TkWindow    *wrapperPtr;
    Tk_Window    menubar;
    int          menuHeight;
    

    /* Subsurface popup for OR / menu windows. */
    TkWaylandPopup *popup;          /* Active subsurface popup for OR
                                      * / menu windows; NULL otherwise. */
    TkWaylandPopup *menubarPopup;   /* Subsurface popup for the menubar
                                      * strip, if any. */
    int          overrideRedirect;  /* Mirrors wm overrideredirect /
                                      * TkpMakeMenuWindow. */
    TkMenu *menubarMenuPtr;

    /* Size hints. */
    int          sizeHintsFlags;
    int          minWidth, minHeight;
    int          maxWidth, maxHeight;
    Tk_Window    gridWin;
    int          widthInc, heightInc;
    struct { int x; int y; } minAspect, maxAspect;
    int          reqGridWidth, reqGridHeight;
    int          gravity;

    /* Position / size. */
    int          width, height;
    int          x, y;
    int          parentWidth, parentHeight;
    int          xInParent, yInParent;
    int          configWidth, configHeight;

    /* Virtual root (compatibility). */
    int          vRootX, vRootY;
    int          vRootWidth, vRootHeight;

    /* Misc. */
    WmAttributes  attributes;
    WmAttributes  reqState;
    ProtocolHandler *protPtr;
    Tcl_Size      cmdArgc;
    Tcl_Obj     **cmdArgv;
    char         *clientMachine;
    int           flags;
    int           numTransients;
    int           iconDataSize;
    unsigned char *iconDataPtr;
    GLFWimage    *glfwIcon;
    int           glfwIconCount;
    int           isMapped;
    int           lastX, lastY;
    int           lastWidth, lastHeight;
    struct TkWmInfo *nextPtr;
} WmInfo;

/*
 * WmInfo flag bits.
 */
#define WM_NEVER_MAPPED             (1<<0)
#define WM_UPDATE_PENDING           (1<<1)
#define WM_NEGATIVE_X               (1<<2)
#define WM_NEGATIVE_Y               (1<<3)
#define WM_UPDATE_SIZE_HINTS        (1<<4)
#define WM_SYNC_PENDING             (1<<5)
#define WM_CREATE_PENDING           (1<<6)
#define WM_ABOUT_TO_MAP             (1<<9)
#define WM_MOVE_PENDING             (1<<10)
#define WM_COLORMAPS_EXPLICIT       (1<<11)
#define WM_ADDED_TOPLEVEL_COLORMAP  (1<<12)
#define WM_WIDTH_NOT_RESIZABLE      (1<<13)
#define WM_HEIGHT_NOT_RESIZABLE     (1<<14)
#define WM_WITHDRAWN                (1<<15)
#define WM_FULLSCREEN_PENDING       (1<<16)

/* Size-hint flags. */
#define WM_USPosition   (1<<0)
#define WM_USSize       (1<<1)
#define WM_PPosition    (1<<2)
#define WM_PSize        (1<<3)
#define WM_PMinSize     (1<<4)
#define WM_PMaxSize     (1<<5)
#define WM_PResizeInc   (1<<6)
#define WM_PAspect      (1<<7)
#define WM_PBaseSize    (1<<8)
#define WM_PWinGravity  (1<<9)

#endif /* _TKWAYLANDWM */

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
