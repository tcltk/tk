/*
 * tkWayland.c --
 *
 *	This file contains procedures that are Wayland-specific
 *
 * Copyright © 1995 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include <GLFW/glfw3.h>

/*
 *----------------------------------------------------------------------
 *
 * TkGetServerInfo --
 *
 *	Given a window, this procedure returns information about the window
 *	server for that window. This procedure provides the guts of the "winfo
 *	server" command.
 *
 * Results:
 *	Sets the interpreter result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkGetServerInfo(
    Tcl_Interp *interp,		/* The server information is returned in this
				 * interpreter's result. */
    Tk_Window tkwin)		/* Token for window; this selects a particular
				 * display and server. */
{
    const char *backend = "GLFW";
    const char *platform = "Wayland";
    
    /* Try to detect if we're actually running on X11 through GLFW */
    if (glfwGetCurrentContext()) {
        if (glfwGetPlatform() == GLFW_PLATFORM_X11) {
            platform = "X11";
        } else if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
            platform = "Wayland";
        }
    }
    
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s %s (via GLFW)", backend, platform));
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetDefaultScreenName --
 *
 *	Returns the name of the screen that Tk should use during
 *	initialization.
 *
 * Results:
 *	Returns the argument or a string that should not be freed by the
 *	caller.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
TkGetDefaultScreenName(
    Tcl_Interp *interp,		/* Interp used to find environment
				 * variables. */
    const char *screenName)	/* Screen name from command line, or NULL. */
{
    if ((screenName == NULL) || (screenName[0] == '\0')) {
        /* For GLFW, we might want to use monitor selection instead of DISPLAY */
        screenName = Tcl_GetVar2(interp, "env", "WAYLAND_DISPLAY", TCL_GLOBAL_ONLY);
        if (screenName == NULL) {
            screenName = Tcl_GetVar2(interp, "env", "DISPLAY", TCL_GLOBAL_ONLY);
        }
        if (screenName == NULL) {
            /* Return default screen/monitor for GLFW */
            screenName = ":0.0";  /* Default X11-style screen name */
        }
    }
    return screenName;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_UpdatePointer --
 *
 *	Unused function in UNIX
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
Tk_UpdatePointer(
    Tk_Window tkwin,		/* Window to which pointer event is reported.
				 * May be NULL. */
    int x, int y,		/* Pointer location in root coords. */
    int state)			/* Modifier state mask. */
{
  /* In GLFW, pointer position is managed by GLFW callbacks.
   * This function might be used to manually update cursor position
   * in some edge cases. */
  
  if (tkwin && Tk_WindowId(tkwin)) {
      /* If we have a GLFW window handle, we could potentially set cursor position */
      GLFWwindow *window = (GLFWwindow *)Tk_WindowId(tkwin);
      /* Note: glfwSetCursorPos sets window-relative coordinates, not root */
  }
  
  (void)x;
  (void)y;
  (void)state;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpCopyRegion --
 *
 *	Makes the destination region a copy of the source region.
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
TkpCopyRegion(
    TkRegion dst,
    TkRegion src)
{
    /* For GLFW, we just copy the region data */
    if (dst != src) {
        memcpy(dst, src, sizeof(TkRegion));
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpBuildRegionFromAlphaData --
 *
 *	Set up a rectangle of the given region based on the supplied alpha
 *	data.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	The region is updated, with extra pixels added to it.
 *
 *----------------------------------------------------------------------
 */

void
TkpBuildRegionFromAlphaData(
    TkRegion region,		/* Region to be updated. */
    unsigned x, unsigned y,	/* Where in region to update. */
    unsigned width, unsigned height,
				/* Size of rectangle to update. */
    unsigned char *dataPtr,	/* Data to read from. */
    unsigned pixelStride,	/* Num bytes from one piece of alpha data to
				 * the next in the line. */
    unsigned lineStride)	/* Num bytes from one line of alpha data to
				 * the next line. */
{
    unsigned char *lineDataPtr;
    unsigned int x1, y1, end;
    /* Define a rectangle structure for region operations */
    typedef struct {
        short x;
        short y;
        unsigned short width;
        unsigned short height;
    } Rect;
    
    Rect rect;

    for (y1 = 0; y1 < height; y1++) {
	lineDataPtr = dataPtr;
	for (x1 = 0; x1 < width; x1 = end) {
	    /*
	     * Search for first non-transparent pixel.
	     */

	    while ((x1 < width) && !*lineDataPtr) {
		x1++;
		lineDataPtr += pixelStride;
	    }
	    end = x1;

	    /*
	     * Search for first transparent pixel.
	     */

	    while ((end < width) && *lineDataPtr) {
		end++;
		lineDataPtr += pixelStride;
	    }
	    if (end > x1) {
		rect.x = (short)(x + x1);
		rect.y = (short)(y + y1);
		rect.width = (unsigned short)(end - x1);
		rect.height = 1;
		/* Use the existing TkUnionRectWithRegion function */
		TkUnionRectWithRegion((XRectangle*)&rect, region, region);
	    }
	}
	dataPtr += lineStride;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetUserInactiveTime --
 *
 *	Return the number of milliseconds the user was inactive.
 *
 * Results:
 *	The number of milliseconds since the user's latest interaction with
 *	the system on the given display.
 *
 * Side effects:
 *	None.
 *----------------------------------------------------------------------
 */

long
Tk_GetUserInactiveTime(
    TCL_UNUSED(Display *dpy))	/* Unused with GLFW */
{
    long inactiveTime = -1;
    
    /* With GLFW, we need platform-specific idle time detection */
#if defined(__linux__)
    /* On Linux with GLFW, we might use DBus or read from /proc */
    FILE *fp = fopen("/proc/uptime", "r");
    if (fp) {
        double uptime, idle_time;
        if (fscanf(fp, "%lf %lf", &uptime, &idle_time) == 2) {
            inactiveTime = (long)(idle_time * 1000); /* Convert to milliseconds */
        }
        fclose(fp);
    }
#elif defined(__APPLE__)
    /* macOS specific idle time detection */
    /* This would require IOKit or CoreGraphics APIs */
#elif defined(_WIN32)
    /* Windows specific idle time detection */
    /* This would require GetLastInputInfo() */
#endif
    
    return inactiveTime;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_ResetUserInactiveTime --
 *
 *	Reset the user inactivity timer
 *
 * Results:
 *	none
 *
 * Side effects:
 *	The user inactivity timer of the underlaying windowing system is reset
 *	to zero.
 *
 *----------------------------------------------------------------------
 */

void
Tk_ResetUserInactiveTime(
    TCL_UNUSED(Display *dpy))
{
    /* With GLFW, there's no direct way to reset system idle time.
     * This could be a no-op or we could simulate user activity. */
    
    /* On X11 systems, we could still call XResetScreenSaver if available */
#ifdef HAVE_X11
    if (dpy) {
        /* Check if we're running on X11 */
        Display *xdisplay = (Display *)dpy;
        XResetScreenSaver(xdisplay);
    }
#endif
    
    /* Alternative: simulate user activity by moving mouse slightly */
    /* This is hacky and may not work on all systems */
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetDisplay --
 *
 *	GLFW-specific helper to get display from window
 *
 * Results:
 *	Returns display pointer (in GLFW context, this might be the monitor)
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/* This is a new helper function for GLFW integration */
void *
Tk_GetDisplay(Tk_Window tkwin)
{
    if (!tkwin) return NULL;
    
    /* In GLFW context, we might return the monitor or window handle */
    GLFWwindow *window = (GLFWwindow *)Tk_WindowId(tkwin);
    if (window) {
        return glfwGetWindowMonitor(window);
    }
    return NULL;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */