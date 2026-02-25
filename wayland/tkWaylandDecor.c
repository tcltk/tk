/*
 *
 *  tkWaylandDecor.c – 
 * 
 * Client-side window decorations for Tcl/Tk on Wayland/GLFW using NanoVG.
 * Includes policy management for CSD/SSD priority and automatic detection.
 * 
 *  Copyright © 2026 Kevin Walzer
 *
 */

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>
#include <nanovg.h>
#include <string.h>
#include <stdlib.h>

/* Decoration modes. */
typedef enum {
    DECOR_AUTO,         /* Prefer SSD, fallback to CSD */
    DECOR_SERVER_ONLY,  /* SSD only */
    DECOR_CLIENT_ONLY,  /* CSD only */
    DECOR_NONE          /* No decorations */
} TkWaylandDecorMode;

static TkWaylandDecorMode decorationMode = DECOR_AUTO;
static int ssdAvailable = 0;

/* Forward declarations. */
static void DrawTitleBar(NVGcontext *vg, TkWaylandDecoration *decor, int width, int height);
static void DrawBorder(NVGcontext *vg, TkWaylandDecoration *decor, int width, int height);
static void DrawButton(NVGcontext *vg, ButtonType type, ButtonState state, float x, float y, float w, float h);
static void HandleButtonClick(TkWaylandDecoration *decor, ButtonType button);
static int GetButtonAtPosition(TkWaylandDecoration *decor, double x, double y, int width);
static int GetResizeEdge(double x, double y, int width, int height);
static void UpdateButtonStates(TkWaylandDecoration *decor, double x, double y, int width);
 
/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDetectServerDecorations --
 *
 *      Detect whether the Wayland compositor supports server‑side
 *      decorations (SSD).  The result is cached in the static variable
 *      'ssdAvailable'.
 *
 * Results:
 *      1 if SSD is available, 0 otherwise.
 *
 * Side effects:
 *      Sets the global 'ssdAvailable' flag.
 *
 *----------------------------------------------------------------------
 */
 
static int
TkWaylandDetectServerDecorations(void)
{
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    const char *session = getenv("XDG_SESSION_TYPE");

    if (session == NULL || strcmp(session, "wayland") != 0) {
	ssdAvailable = 0;
	return 0;
    }

    if (desktop != NULL) {
	if (strstr(desktop, "GNOME") != NULL) {
	    ssdAvailable = 0;
	    return 0;
	}
	if (strstr(desktop, "KDE") != NULL) {
	    ssdAvailable = 1;
	    return 1;
	}
	if (strstr(desktop, "sway") != NULL || strstr(desktop, "Sway") != NULL) {
	    ssdAvailable = 1;
	    return 1;
	}
    }

    ssdAvailable = 0;
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSetDecorationMode --
 *
 *      Set the global decoration policy from a string.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Changes the 'decorationMode' static variable.
 *
 *----------------------------------------------------------------------
 */
void
TkWaylandSetDecorationMode(const char *mode)
{
    if (mode == NULL) {
	decorationMode = DECOR_AUTO;
	return;
    }

    if (strcmp(mode, "auto") == 0) {
	decorationMode = DECOR_AUTO;
    } else if (strcmp(mode, "server") == 0 || strcmp(mode, "ssd") == 0) {
	decorationMode = DECOR_SERVER_ONLY;
    } else if (strcmp(mode, "client") == 0 || strcmp(mode, "csd") == 0) {
	decorationMode = DECOR_CLIENT_ONLY;
    } else if (strcmp(mode, "none") == 0 || strcmp(mode, "borderless") == 0) {
	decorationMode = DECOR_NONE;
    } else {
	decorationMode = DECOR_AUTO;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetDecorationMode --
 *
 *      Return the current decoration mode as a string.
 *
 * Results:
 *      Constant string describing the mode ("auto", "server", "client", "none").
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
const char *
TkWaylandGetDecorationMode(void)
{
    switch (decorationMode) {
    case DECOR_AUTO:        return "auto";
    case DECOR_SERVER_ONLY: return "server";
    case DECOR_CLIENT_ONLY: return "client";
    case DECOR_NONE:        return "none";
    default:                return "auto";
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandShouldUseCSD --
 *
 *      Determine, based on the current policy and detected SSD
 *      availability, whether client‑side decorations should be used.
 *
 * Results:
 *      1 if CSD should be used, 0 otherwise.
 *
 * Side effects:
 *      May trigger the one‑time SSD detection.
 *
 *----------------------------------------------------------------------
 */
 
int
TkWaylandShouldUseCSD(void)
{
    static int detected = 0;

    if (!detected) {
	TkWaylandDetectServerDecorations();
	detected = 1;
    }

    switch (decorationMode) {
    case DECOR_AUTO:
	return 1;
    case DECOR_SERVER_ONLY:
	return 0;
    case DECOR_CLIENT_ONLY:
	return 1;
    case DECOR_NONE:
	return 0;
    default:
	return 1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandConfigureWindowDecorations --
 *
 *      Set GLFW window hints according to the current decoration policy.
 *      This function should be called before creating a window.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Modifies the GLFW hint state.
 *
 *----------------------------------------------------------------------
 */
 
void
TkWaylandConfigureWindowDecorations(void)
{
    int useCSD = TkWaylandShouldUseCSD();

    if (decorationMode == DECOR_NONE) {
	glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    } else if (useCSD) {
	glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    } else {
	glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCreateDecoration --
 *
 *      Allocate and initialise a decoration structure for a Tk window.
 *      The wmPtr is taken from winPtr->wmInfoPtr (must be valid).
 *
 * Results:
 *      Pointer to a new TkWaylandDecoration, or NULL on failure.
 *
 * Side effects:
 *      Memory is allocated; the window title is set from the Tk path name.
 *
 *----------------------------------------------------------------------
 */
 
TkWaylandDecoration *
TkWaylandCreateDecoration(TkWindow *winPtr,
			  GLFWwindow *glfwWindow)
{
    TkWaylandDecoration *decor;

    if (winPtr == NULL || glfwWindow == NULL) {
	return NULL;
    }

    decor = (TkWaylandDecoration *)calloc(1, sizeof(TkWaylandDecoration));
    if (decor == NULL) {
	return NULL;
    }

    decor->winPtr = winPtr;
    decor->glfwWindow = glfwWindow;
    decor->wmPtr = (WmInfo *)winPtr->wmInfoPtr;  /* Link to WM info */
    decor->enabled = 1;
    decor->maximized = 0;

    decor->title = (char *)malloc(256);
    if (decor->title != NULL) {
	const char *name = Tk_PathName((Tk_Window)winPtr);
	strncpy(decor->title, name ? name : "Tk", 255);
	decor->title[255] = '\0';
    }

    decor->closeState = BUTTON_NORMAL;
    decor->maxState = BUTTON_NORMAL;
    decor->minState = BUTTON_NORMAL;
    decor->dragging = 0;
    decor->resizing = RESIZE_NONE;

    return decor;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDestroyDecoration --
 *
 *      Free the resources associated with a decoration structure.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is freed.
 *
 *----------------------------------------------------------------------
 */
 
void
TkWaylandDestroyDecoration(TkWaylandDecoration *decor)
{
    if (decor == NULL) {
	return;
    }

    if (decor->title != NULL) {
	free(decor->title);
    }

    free(decor);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDrawDecoration --
 *
 *      Draw the complete window decoration (shadow, border, title bar)
 *      using the NanoVG context.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Issues NanoVG drawing commands.
 *
 *----------------------------------------------------------------------
 */
 
void
TkWaylandDrawDecoration(TkWaylandDecoration *decor,
                        NVGcontext *vg)
{
    int width, height;
    WindowMapping *mapping;

    if (decor == NULL || !decor->enabled || vg == NULL) {
        return;
    }

    glfwGetWindowSize(decor->glfwWindow, &width, &height);

    /* Get the client area size from mapping */
    mapping = FindMappingByGLFW(decor->glfwWindow);
    if (!mapping) return;

    nvgSave(vg);

    /* Draw shadow (outside window bounds). */
    NVGpaint shadowPaint = nvgBoxGradient(vg,
                                          -BORDER_WIDTH, -TITLE_BAR_HEIGHT,
                                          width + 2*BORDER_WIDTH,
                                          height + TITLE_BAR_HEIGHT + BORDER_WIDTH,
                                          CORNER_RADIUS, SHADOW_BLUR,
                                          nvgRGBA(0, 0, 0, 64), nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, -SHADOW_BLUR - BORDER_WIDTH, -SHADOW_BLUR - TITLE_BAR_HEIGHT,
            width + 2*(SHADOW_BLUR + BORDER_WIDTH),
            height + 2*SHADOW_BLUR + TITLE_BAR_HEIGHT + BORDER_WIDTH);
    nvgFillPaint(vg, shadowPaint);
    nvgFill(vg);

    /* Draw border and title bar. */
    DrawBorder(vg, decor, width, height);
    DrawTitleBar(vg, decor, width, height);

    /* Set scissor to client area for widget drawing */
    nvgIntersectScissor(vg, BORDER_WIDTH, TITLE_BAR_HEIGHT,
                        mapping->width, mapping->height);

    nvgRestore(vg);
}


/*
 *----------------------------------------------------------------------
 *
 * DrawTitleBar --
 *
 *      Draw the title bar background, title text, and window control
 *      buttons.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Issues NanoVG drawing commands.
 *
 *----------------------------------------------------------------------
 */
static void
DrawTitleBar(NVGcontext *vg,
	     TkWaylandDecoration *decor,
	     int width,
	     TCL_UNUSED(int)) /* height */

{
    float buttonX;
    int focused;
    NVGcolor bgColor, textColor;

    focused = glfwGetWindowAttrib(decor->glfwWindow, GLFW_FOCUSED);
    bgColor = focused ? nvgRGB(45, 45, 48) : nvgRGB(60, 60, 60);

    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, 0, 0, width, TITLE_BAR_HEIGHT,
			  CORNER_RADIUS, CORNER_RADIUS, 0, 0);
    nvgFillColor(vg, bgColor);
    nvgFill(vg);

    if (decor->title != NULL) {
	textColor = focused ? nvgRGB(255, 255, 255) : nvgRGB(180, 180, 180);
	nvgFontSize(vg, 14.0f);
	nvgFontFace(vg, "sans");
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	nvgFillColor(vg, textColor);
	nvgText(vg, 15, TITLE_BAR_HEIGHT / 2, decor->title, NULL);
    }

    buttonX = width - BUTTON_SPACING - BUTTON_WIDTH;
    DrawButton(vg, BUTTON_CLOSE, decor->closeState,
	       buttonX, (TITLE_BAR_HEIGHT - BUTTON_HEIGHT) / 2,
	       BUTTON_WIDTH, BUTTON_HEIGHT);

    buttonX -= (BUTTON_WIDTH + BUTTON_SPACING);
    DrawButton(vg, BUTTON_MAXIMIZE, decor->maxState,
	       buttonX, (TITLE_BAR_HEIGHT - BUTTON_HEIGHT) / 2,
	       BUTTON_WIDTH, BUTTON_HEIGHT);

    buttonX -= (BUTTON_WIDTH + BUTTON_SPACING);
    DrawButton(vg, BUTTON_MINIMIZE, decor->minState,
	       buttonX, (TITLE_BAR_HEIGHT - BUTTON_HEIGHT) / 2,
	       BUTTON_WIDTH, BUTTON_HEIGHT);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawBorder --
 *
 *      Draw the outer border of the window.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Issues NanoVG drawing commands.
 *
 *----------------------------------------------------------------------
 */
 
static void
DrawBorder(NVGcontext *vg,
	   TkWaylandDecoration *decor,
	   int width,
	   int height)
{
    int focused;
    NVGcolor borderColor;

    focused = glfwGetWindowAttrib(decor->glfwWindow, GLFW_FOCUSED);
    borderColor = focused ? nvgRGB(30, 30, 30) : nvgRGB(80, 80, 80);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0, 0, width, height, CORNER_RADIUS);
    nvgStrokeColor(vg, borderColor);
    nvgStrokeWidth(vg, BORDER_WIDTH);
    nvgStroke(vg);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawButton --
 *
 *      Draw one window control button (close, maximise, minimise) with
 *      appropriate background and icon based on its state.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Issues NanoVG drawing commands.
 *
 *----------------------------------------------------------------------
 */
 
static void
DrawButton(NVGcontext *vg,
	   ButtonType type,
	   ButtonState state,
	   float x,
	   float y,
	   float w,
	   float h)
{
    NVGcolor bgColor, iconColor;
    float iconSize = 10.0f;
    float cx = x + w/2;
    float cy = y + h/2;

    switch (state) {
    case BUTTON_HOVER:
	bgColor = (type == BUTTON_CLOSE) ? nvgRGB(232, 17, 35) : nvgRGB(80, 80, 80);
	break;
    case BUTTON_PRESSED:
	bgColor = (type == BUTTON_CLOSE) ? nvgRGB(196, 43, 28) : nvgRGB(100, 100, 100);
	break;
    case BUTTON_NORMAL:
    default:
	bgColor = nvgRGBA(0, 0, 0, 0);
	break;
    }

    if (state != BUTTON_NORMAL) {
	nvgBeginPath(vg);
	nvgRoundedRect(vg, x, y, w, h, 3.0f);
	nvgFillColor(vg, bgColor);
	nvgFill(vg);
    }

    iconColor = (state == BUTTON_HOVER || state == BUTTON_PRESSED) ?
	nvgRGB(255, 255, 255) : nvgRGB(200, 200, 200);

    nvgStrokeColor(vg, iconColor);
    nvgStrokeWidth(vg, 1.5f);

    switch (type) {
    case BUTTON_CLOSE:
	nvgBeginPath(vg);
	nvgMoveTo(vg, cx - iconSize/2, cy - iconSize/2);
	nvgLineTo(vg, cx + iconSize/2, cy + iconSize/2);
	nvgMoveTo(vg, cx + iconSize/2, cy - iconSize/2);
	nvgLineTo(vg, cx - iconSize/2, cy + iconSize/2);
	nvgStroke(vg);
	break;
    case BUTTON_MAXIMIZE:
	nvgBeginPath(vg);
	nvgRect(vg, cx - iconSize/2, cy - iconSize/2, iconSize, iconSize);
	nvgStroke(vg);
	break;
    case BUTTON_MINIMIZE:
	nvgBeginPath(vg);
	nvgMoveTo(vg, cx - iconSize/2, cy);
	nvgLineTo(vg, cx + iconSize/2, cy);
	nvgStroke(vg);
	break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDecorationMouseButton --
 *
 *      Process mouse button events for the decoration area.
 *
 * Results:
 *      1 if the event was handled (i.e. occurred in the decoration area),
 *      0 otherwise.
 *
 * Side effects:
 *      May initiate window dragging, resizing, or button state changes.
 *      May trigger window close, maximise, or minimise actions.
 *
 *----------------------------------------------------------------------
 */
 
int
TkWaylandDecorationMouseButton(TkWaylandDecoration *decor,
			       int button,
			       int action,
			       double x,
			       double y)
{
    int width, height;
    int buttonType;
    int resizeEdge;

    if (decor == NULL || !decor->enabled) {
	return 0;
    }

    glfwGetWindowSize(decor->glfwWindow, &width, &height);

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
	if (action == GLFW_PRESS) {
	    buttonType = GetButtonAtPosition(decor, x, y, width);
	    if (buttonType >= 0) {
		if (buttonType == BUTTON_CLOSE) decor->closeState = BUTTON_PRESSED;
		else if (buttonType == BUTTON_MAXIMIZE) decor->maxState = BUTTON_PRESSED;
		else if (buttonType == BUTTON_MINIMIZE) decor->minState = BUTTON_PRESSED;
		return 1;
	    }
        
	    if (y < TITLE_BAR_HEIGHT) {
		decor->dragging = 1;
		decor->dragStartX = x;
		decor->dragStartY = y;
		glfwGetWindowPos(decor->glfwWindow, &decor->windowStartX, &decor->windowStartY);
		return 1;
	    }
        
	    resizeEdge = GetResizeEdge(x, y, width, height);
	    if (resizeEdge != RESIZE_NONE) {
		decor->resizing = resizeEdge;
		decor->resizeStartX = x;
		decor->resizeStartY = y;
		glfwGetWindowSize(decor->glfwWindow, &decor->resizeStartWidth, &decor->resizeStartHeight);
		return 1;
	    }
        
	} else if (action == GLFW_RELEASE) {
	    buttonType = GetButtonAtPosition(decor, x, y, width);
	    if (buttonType >= 0) {
		if (buttonType == BUTTON_CLOSE && decor->closeState == BUTTON_PRESSED) {
		    HandleButtonClick(decor, BUTTON_CLOSE);
		} else if (buttonType == BUTTON_MAXIMIZE && decor->maxState == BUTTON_PRESSED) {
		    HandleButtonClick(decor, BUTTON_MAXIMIZE);
		} else if (buttonType == BUTTON_MINIMIZE && decor->minState == BUTTON_PRESSED) {
		    HandleButtonClick(decor, BUTTON_MINIMIZE);
		}
	    }
        
	    decor->closeState = BUTTON_NORMAL;
	    decor->maxState = BUTTON_NORMAL;
	    decor->minState = BUTTON_NORMAL;
	    decor->dragging = 0;
	    decor->resizing = RESIZE_NONE;
	    UpdateButtonStates(decor, x, y, width);
	    return 1;
	}
    }

    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDecorationMouseMove --
 *
 *      Process mouse motion events for the decoration area.
 *
 * Results:
 *      1 if the motion caused a window operation (drag/resize) or a
 *      button state change, 0 otherwise.
 *
 * Side effects:
 *      May move the window (dragging) or resize it, or update button
 *      hover states.
 *
 *----------------------------------------------------------------------
 */
 
int
TkWaylandDecorationMouseMove(TkWaylandDecoration *decor,
			     double x,
			     double y)
{
    int width, height;
    int newX, newY;
    int newWidth, newHeight;

    if (decor == NULL || !decor->enabled) {
	return 0;
    }

    glfwGetWindowSize(decor->glfwWindow, &width, &height);

    if (decor->dragging) {
	newX = decor->windowStartX + (int)(x - decor->dragStartX);
	newY = decor->windowStartY + (int)(y - decor->dragStartY);
	glfwSetWindowPos(decor->glfwWindow, newX, newY);
	return 1;
    }

    if (decor->resizing != RESIZE_NONE) {
	newWidth = decor->resizeStartWidth;
	newHeight = decor->resizeStartHeight;
    
	if (decor->resizing & RESIZE_RIGHT) {
	    newWidth = decor->resizeStartWidth + (int)(x - decor->resizeStartX);
	}
	if (decor->resizing & RESIZE_LEFT) {
	    newWidth = decor->resizeStartWidth - (int)(x - decor->resizeStartX);
	}
	if (decor->resizing & RESIZE_BOTTOM) {
	    newHeight = decor->resizeStartHeight + (int)(y - decor->resizeStartY);
	}
	if (decor->resizing & RESIZE_TOP) {
	    newHeight = decor->resizeStartHeight - (int)(y - decor->resizeStartY);
	}
    
	if (newWidth < 100) newWidth = 100;
	if (newHeight < 100) newHeight = 100;
    
	glfwSetWindowSize(decor->glfwWindow, newWidth, newHeight);
	return 1;
    }

    UpdateButtonStates(decor, x, y, width);
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * HandleButtonClick --
 *
 *      Perform the action associated with a window control button.
 *      For maximize, update the WM's zoomed attribute to stay in sync.
 *      For minimize, update the WM's iconic state.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May close, maximise/restore, or minimise the window via GLFW.
 *      Updates the WmInfo zoomed/iconic flags.
 *
 *----------------------------------------------------------------------
 */
 
static void
HandleButtonClick(TkWaylandDecoration *decor,
		  ButtonType button)
{
    switch (button) {
    case BUTTON_CLOSE:
	glfwSetWindowShouldClose(decor->glfwWindow, GLFW_TRUE);
	break;
    case BUTTON_MAXIMIZE:
	if (decor->maximized) {
	    glfwRestoreWindow(decor->glfwWindow);
	    decor->maximized = 0;
	    /* Update WM's zoomed attribute. */
	    if (decor->wmPtr != NULL) {
		decor->wmPtr->attributes.zoomed = 0;
		decor->wmPtr->reqState.zoomed = 0;
	    }
	} else {
	    glfwMaximizeWindow(decor->glfwWindow);
	    decor->maximized = 1;
	    if (decor->wmPtr != NULL) {
		decor->wmPtr->attributes.zoomed = 1;
		decor->wmPtr->reqState.zoomed = 1;
	    }
	}
	break;
    case BUTTON_MINIMIZE:
	glfwIconifyWindow(decor->glfwWindow);
	/* Update Tk's internal state to IconicState. */
	if (decor->winPtr != NULL) {
	    TkpWmSetState(decor->winPtr, IconicState);
	    /* GLFW may not send an UnmapNotify, so clear mapped flag manually. */
	    decor->winPtr->flags &= ~TK_MAPPED;
	}
	break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetButtonAtPosition --
 *
 *      Determine which window control button, if any, is under the given
 *      coordinates.
 *
 * Results:
 *      The ButtonType of the button, or -1 if none.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
 
static int
GetButtonAtPosition(TkWaylandDecoration *decor,
		    double x,
		    double y,
		    int width)
{
    float buttonX;

    (void)decor;

    if (y < 0 || y > TITLE_BAR_HEIGHT) {
	return -1;
    }

    buttonX = width - BUTTON_SPACING - BUTTON_WIDTH;
    if (x >= buttonX && x < buttonX + BUTTON_WIDTH) return BUTTON_CLOSE;

    buttonX -= (BUTTON_WIDTH + BUTTON_SPACING);
    if (x >= buttonX && x < buttonX + BUTTON_WIDTH) return BUTTON_MAXIMIZE;

    buttonX -= (BUTTON_WIDTH + BUTTON_SPACING);
    if (x >= buttonX && x < buttonX + BUTTON_WIDTH) return BUTTON_MINIMIZE;

    return -1;
}

/*
 *----------------------------------------------------------------------
 *
 * GetResizeEdge --
 *
 *      Determine which edges (if any) are being approached for resizing,
 *      based on the cursor position relative to the window borders.
 *
 * Results:
 *      Bitmask of RESIZE_* flags.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
 
static int
GetResizeEdge(double x,
	      double y,
	      int width,
	      int height)
{
    int edge = RESIZE_NONE;
    int margin = 5;

    if (x < margin) edge |= RESIZE_LEFT;
    else if (x > width - margin) edge |= RESIZE_RIGHT;

    if (y < margin) edge |= RESIZE_TOP;
    else if (y > height - margin) edge |= RESIZE_BOTTOM;

    return edge;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateButtonStates --
 *
 *      Update the hover state of the three window buttons based on the
 *      current cursor position.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Modifies the button state fields in the decoration structure.
 *
 *----------------------------------------------------------------------
 */
 
static void
UpdateButtonStates(TkWaylandDecoration *decor,
		   double x,
		   double y,
		   int width)
{
    int button = GetButtonAtPosition(decor, x, y, width);

    decor->closeState = BUTTON_NORMAL;
    decor->maxState = BUTTON_NORMAL;
    decor->minState = BUTTON_NORMAL;

    if (button == BUTTON_CLOSE) decor->closeState = BUTTON_HOVER;
    else if (button == BUTTON_MAXIMIZE) decor->maxState = BUTTON_HOVER;
    else if (button == BUTTON_MINIMIZE) decor->minState = BUTTON_HOVER;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSetDecorationTitle --
 *
 *      Change the title displayed in the window decoration.
 *      This function should be called by the window manager (tkWaylandWm.c)
 *      whenever the window title changes (e.g., via "wm title").
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The title string is duplicated and stored; next redraw uses it.
 *
 *----------------------------------------------------------------------
 */
void
TkWaylandSetDecorationTitle(TkWaylandDecoration *decor,
			    const char *title)
{
    if (decor == NULL || title == NULL) {
	return;
    }

    if (decor->title != NULL) {
	free(decor->title);
    }

    decor->title = (char *)malloc(strlen(title) + 1);
    if (decor->title != NULL) {
	strcpy(decor->title, title);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSetWindowMaximized --
 *
 *      Update the decoration's internal maximized state to match the
 *      WM's zoomed attribute.  Called by the WM when the window is
 *      maximized or restored programmatically.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates decor->maximized; next redraw will show correct button.
 *
 *----------------------------------------------------------------------
 */
void
TkWaylandSetWindowMaximized(TkWaylandDecoration *decor,
			    int maximized)
{
    if (decor == NULL) {
	return;
    }
    decor->maximized = maximized ? 1 : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetDecorationContentArea --
 *
 *      Return the rectangle (relative to the window) that is available
 *      for application content, i.e. excluding the decoration areas.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The output parameters are filled with the content area geometry.
 *
 *----------------------------------------------------------------------
 */
 
void
TkWaylandGetDecorationContentArea(TkWaylandDecoration *decor,
				  int *x,
				  int *y,
				  int *width,
				  int *height)
{
    int winWidth, winHeight;

    if (decor == NULL) {
	return;
    }

    glfwGetWindowSize(decor->glfwWindow, &winWidth, &winHeight);

    if (decor->enabled) {
	*x = BORDER_WIDTH;
	*y = TITLE_BAR_HEIGHT;
	*width = winWidth - 2 * BORDER_WIDTH;
	*height = winHeight - TITLE_BAR_HEIGHT - BORDER_WIDTH;
    } else {
	*x = 0;
	*y = 0;
	*width = winWidth;
	*height = winHeight;
    }
}


/*
 *----------------------------------------------------------------------
 * TkWaylandInitDecorationPolicy --
 *
 *	Initialize the Wayland decoration system. Detects compositor
 *	capabilities, and sets policy from environment.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Detects SSD availability, and sets decoration mode.
 *  
 *----------------------------------------------------------------------
 */

void
TkWaylandInitDecorationPolicy(TCL_UNUSED(Tcl_Interp *))
{
    const char *decorEnv;
    
    /* Detect whether compositor supports server-side decorations. */
    TkWaylandDetectServerDecorations();
    
    /* Check for environment variable override */
    decorEnv = getenv("TK_WAYLAND_DECORATIONS");
    if (decorEnv != NULL) {
        TkWaylandSetDecorationMode(decorEnv);
    }
}


/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
