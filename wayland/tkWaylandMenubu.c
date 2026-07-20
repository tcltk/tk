/*
 * tkWaylandMenubu.c --
 *
 *	Wayland/GLFW-specific portion of the menubutton widget.
 *
 * Copyright © 1996-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkMenubutton.h"
#include "tkWaylandInt.h"
#include <GLFW/glfw3.h>

/* Debug macro */
#define MENU_DEBUG 1
#if MENU_DEBUG
#define MENU_LOG(fmt, ...) fprintf(stderr, "MENU: " fmt "\n", ##__VA_ARGS__)
#else
#define MENU_LOG(fmt, ...) ((void)0)
#endif

MODULE_SCOPE int TkWaylandPopupBeginDraw(TkWaylandPopup *popup);
MODULE_SCOPE void TkWaylandPopupEndDraw(TkWaylandPopup *popup);

/*
 *----------------------------------------------------------------------
 *
 * TkpCreateMenuButton --
 *
 *	Allocate a new TkMenuButton structure.
 *
 * Results:
 *	Returns a pointer to a newly allocated TkMenuButton structure.
 *
 * Side effects:
 *	Memory is allocated for the menubutton structure.
 *
 *----------------------------------------------------------------------
 */

TkMenuButton *
TkpCreateMenuButton(
    TCL_UNUSED(Tk_Window))  /* tkwin - Window for the menubutton */
{
    return (TkMenuButton *)Tcl_Alloc(sizeof(TkMenuButton));
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDestroyMenuButton --
 *
 *	Free data structures associated with the menubutton control.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Any resources associated with the menubutton are freed.
 *
 *----------------------------------------------------------------------
 */

void
TkpDestroyMenuButton(
    TCL_UNUSED(TkMenuButton *))  /* mbPtr - Menubutton to destroy */
{
    /* Nothing to do on Wayland - resources are freed by Tk */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpComputeMenuButtonGeometry --
 *
 *	Compute the menubutton's geometry based on its current state and
 *	configuration options.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the menubutton's geometry request via Tk_GeometryRequest.
 *
 *----------------------------------------------------------------------
 */

void
TkpComputeMenuButtonGeometry(
    TkMenuButton *mbPtr)  /* Menubutton to compute geometry for */
{
    int width, height, mm, pixels;
    int avgWidth, txtWidth, txtHeight;
    int haveImage = 0, haveText = 0;
    Tk_FontMetrics fm;
    int borderWidth, highlightWidth, wrapLength;
    int padX, padY;

    Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->padXObj,        &padX);
    Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->padYObj,        &padY);
    Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->borderWidthObj, &borderWidth);
    Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->highlightWidthObj,
                        &highlightWidth);
    Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->wrapLengthObj,  &wrapLength);
    mbPtr->inset = highlightWidth + borderWidth;

    width = height = txtWidth = txtHeight = avgWidth = 0;

    if (mbPtr->image != NULL) {
        Tk_SizeOfImage(mbPtr->image, &width, &height);
        haveImage = 1;
    } else if (mbPtr->bitmap != None) {
        Tk_SizeOfBitmap(mbPtr->display, mbPtr->bitmap, &width, &height);
        haveImage = 1;
    }

    if (haveImage == 0 || mbPtr->compound != COMPOUND_NONE) {
        Tk_FreeTextLayout(mbPtr->textLayout);
        mbPtr->textLayout = Tk_ComputeTextLayout(
            mbPtr->tkfont,
            mbPtr->textObj ? Tcl_GetString(mbPtr->textObj) : "",
            TCL_INDEX_NONE, wrapLength, mbPtr->justify, 0,
            &mbPtr->textWidth, &mbPtr->textHeight);
        txtWidth  = mbPtr->textWidth;
        txtHeight = mbPtr->textHeight;
        avgWidth  = Tk_TextWidth(mbPtr->tkfont, "0", 1);
        Tk_GetFontMetrics(mbPtr->tkfont, &fm);
        haveText  = (txtWidth != 0 && txtHeight != 0);
    }

    if (mbPtr->compound != COMPOUND_NONE && haveImage && haveText) {
        switch ((enum compound)mbPtr->compound) {
        case COMPOUND_TOP:
        case COMPOUND_BOTTOM:
            height += txtHeight + padY;
            width   = (width > txtWidth ? width : txtWidth);
            break;
        case COMPOUND_LEFT:
        case COMPOUND_RIGHT:
            width  += txtWidth + padX;
            height  = (height > txtHeight ? height : txtHeight);
            break;
        case COMPOUND_CENTER:
            width  = (width > txtWidth  ? width  : txtWidth);
            height = (height > txtHeight ? height : txtHeight);
            break;
        case COMPOUND_NONE:
            break;
        }
        if (mbPtr->width  > 0) width  = mbPtr->width;
        if (mbPtr->height > 0) height = mbPtr->height;
        width  += 2 * padX;
        height += 2 * padY;
    } else {
        if (haveImage) {
            if (mbPtr->width  > 0) width  = mbPtr->width;
            if (mbPtr->height > 0) height = mbPtr->height;
        } else {
            width  = txtWidth;
            height = txtHeight;
            if (mbPtr->width  > 0) width  = mbPtr->width  * avgWidth;
            if (mbPtr->height > 0) height = mbPtr->height * fm.linespace;
        }
    }

    if (!haveImage) {
        width  += 2 * padX;
        height += 2 * padY;
    }

    if (mbPtr->indicatorOn) {
        mm     = WidthMMOfScreen(Tk_Screen(mbPtr->tkwin));
        pixels = WidthOfScreen(Tk_Screen(mbPtr->tkwin));
        mbPtr->indicatorHeight = (INDICATOR_HEIGHT * pixels) / (10 * mm);
        mbPtr->indicatorWidth  = (INDICATOR_WIDTH  * pixels) / (10 * mm)
                                 + 2 * mbPtr->indicatorHeight;
        width += mbPtr->indicatorWidth;
    } else {
        mbPtr->indicatorHeight = 0;
        mbPtr->indicatorWidth  = 0;
    }

    Tk_GeometryRequest(mbPtr->tkwin,
                       (int)(width  + 2 * mbPtr->inset),
                       (int)(height + 2 * mbPtr->inset));
    Tk_SetInternalBorder(mbPtr->tkwin, mbPtr->inset);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayMenuButton --
 *
 *	Render the menubutton widget. Rendering is identical to the
 *	previous implementation; posting is now handled separately by
 *	TkpMenuButtonPostMenu.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws the menubutton widget on the display.
 *
 *----------------------------------------------------------------------
 */

void
TkpDisplayMenuButton(
    void *clientData)  /* Menubutton to display */
{
    TkMenuButton *mbPtr = (TkMenuButton *)clientData;
    GC gc;
    Tk_3DBorder border;
    int x = 0, y = 0;
    Tk_Window tkwin = mbPtr->tkwin;
    int fullWidth, fullHeight;
    int textXOffset, textYOffset;
    int imageWidth, imageHeight;
    int imageXOffset, imageYOffset;
    int width = 0, height = 0;
    int haveImage = 0, haveText = 0;
    int padX, padY;
    int mbPtrBorderWidth, highlightWidth;

    mbPtr->flags &= ~REDRAW_PENDING;
    if ((mbPtr->tkwin == NULL) || !Tk_IsMapped(tkwin)) {
        return;
    }

    if ((mbPtr->state == STATE_DISABLED) && (mbPtr->disabledFg != NULL)) {
        gc     = mbPtr->disabledGC;
        border = mbPtr->normalBorder;
    } else if ((mbPtr->state == STATE_ACTIVE)
               && !Tk_StrictMotif(mbPtr->tkwin)) {
        gc     = mbPtr->activeTextGC;
        border = mbPtr->activeBorder;
    } else {
        gc     = mbPtr->normalTextGC;
        border = mbPtr->normalBorder;
    }

    if (mbPtr->image != NULL) {
        Tk_SizeOfImage(mbPtr->image, &width, &height);
        haveImage = 1;
    } else if (mbPtr->bitmap != None) {
        Tk_SizeOfBitmap(mbPtr->display, mbPtr->bitmap, &width, &height);
        haveImage = 1;
    }
    imageWidth  = width;
    imageHeight = height;
    haveText    = (mbPtr->textWidth != 0 && mbPtr->textHeight != 0);

    Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->padXObj, &padX);
    Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->padYObj, &padY);

    TkWaylandDrawingContext dc;
    if (TkWaylandBeginDraw(Tk_WindowId(tkwin), gc, &dc) != TCL_OK) {
        return;
    }

    Tk_Fill3DRectangle(tkwin, Tk_WindowId(tkwin), border, 0, 0,
                       Tk_Width(tkwin), Tk_Height(tkwin), 0, TK_RELIEF_FLAT);

    imageXOffset = 0;
    imageYOffset = 0;
    textXOffset  = 0;
    textYOffset  = 0;
    fullWidth    = 0;
    fullHeight   = 0;

    if (mbPtr->compound != COMPOUND_NONE && haveImage && haveText) {
        switch ((enum compound)mbPtr->compound) {
        case COMPOUND_TOP:
        case COMPOUND_BOTTOM:
            if (mbPtr->compound == COMPOUND_TOP) {
                textYOffset = height + padY;
            } else {
                imageYOffset = mbPtr->textHeight + padY;
            }
            fullHeight   = height + mbPtr->textHeight + padY;
            fullWidth    = (width > mbPtr->textWidth ? width : mbPtr->textWidth);
            textXOffset  = (fullWidth - mbPtr->textWidth) / 2;
            imageXOffset = (fullWidth - width) / 2;
            break;
        case COMPOUND_LEFT:
        case COMPOUND_RIGHT:
            if (mbPtr->compound == COMPOUND_LEFT) {
                textXOffset = width + padX;
            } else {
                imageXOffset = mbPtr->textWidth + padX;
            }
            fullWidth    = mbPtr->textWidth + padX + width;
            fullHeight   = (height > mbPtr->textHeight ? height : mbPtr->textHeight);
            textYOffset  = (fullHeight - mbPtr->textHeight) / 2;
            imageYOffset = (fullHeight - height) / 2;
            break;
        case COMPOUND_CENTER:
            fullWidth    = (width > mbPtr->textWidth ? width : mbPtr->textWidth);
            fullHeight   = (height > mbPtr->textHeight ? height : mbPtr->textHeight);
            textXOffset  = (fullWidth - mbPtr->textWidth) / 2;
            imageXOffset = (fullWidth - width) / 2;
            textYOffset  = (fullHeight - mbPtr->textHeight) / 2;
            imageYOffset = (fullHeight - height) / 2;
            break;
        case COMPOUND_NONE:
            break;
        }

        TkComputeAnchor(mbPtr->anchor, tkwin, 0, 0,
                        mbPtr->indicatorWidth + fullWidth, fullHeight, &x, &y);
        imageXOffset += x;
        imageYOffset += y;

        if (mbPtr->image != NULL) {
            Tk_RedrawImage(mbPtr->image, 0, 0, width, height,
                           Tk_WindowId(tkwin), imageXOffset, imageYOffset);
        } else if (mbPtr->bitmap != None) {
            XSetClipOrigin(mbPtr->display, gc, imageXOffset, imageYOffset);
            XCopyPlane(mbPtr->display, mbPtr->bitmap, Tk_WindowId(tkwin),
                       gc, 0, 0, (unsigned)width, (unsigned)height,
                       imageXOffset, imageYOffset, 1);
            XSetClipOrigin(mbPtr->display, gc, 0, 0);
        }
        Tk_DrawTextLayout(mbPtr->display, Tk_WindowId(tkwin), gc,
                          mbPtr->textLayout,
                          x + textXOffset, y + textYOffset, 0, -1);
        Tk_UnderlineTextLayout(mbPtr->display, Tk_WindowId(tkwin), gc,
                               mbPtr->textLayout,
                               x + textXOffset, y + textYOffset,
                               mbPtr->underline);
    } else if (haveImage) {
        TkComputeAnchor(mbPtr->anchor, tkwin, 0, 0,
                        width + mbPtr->indicatorWidth, height, &x, &y);
        imageXOffset += x;
        imageYOffset += y;
        if (mbPtr->image != NULL) {
            Tk_RedrawImage(mbPtr->image, 0, 0, width, height,
                           Tk_WindowId(tkwin), imageXOffset, imageYOffset);
        } else if (mbPtr->bitmap != None) {
            XSetClipOrigin(mbPtr->display, gc, x, y);
            XCopyPlane(mbPtr->display, mbPtr->bitmap, Tk_WindowId(tkwin),
                       gc, 0, 0, (unsigned)width, (unsigned)height,
                       x, y, 1);
            XSetClipOrigin(mbPtr->display, gc, 0, 0);
        }
    } else {
        TkComputeAnchor(mbPtr->anchor, tkwin, padX, padY,
                        mbPtr->textWidth + mbPtr->indicatorWidth,
                        mbPtr->textHeight, &x, &y);
        Tk_DrawTextLayout(mbPtr->display, Tk_WindowId(tkwin), gc,
                          mbPtr->textLayout,
                          x + textXOffset, y + textYOffset, 0, -1);
        Tk_UnderlineTextLayout(mbPtr->display, Tk_WindowId(tkwin), gc,
                               mbPtr->textLayout,
                               x + textXOffset, y + textYOffset,
                               mbPtr->underline);
    }

    if ((mbPtr->state == STATE_DISABLED)
        && ((mbPtr->disabledFg == NULL) || (mbPtr->image != NULL))) {
        if (mbPtr->disabledFg == NULL) {
            XFillRectangle(mbPtr->display, Tk_WindowId(tkwin),
                           mbPtr->stippleGC,
                           mbPtr->inset, mbPtr->inset,
                           (unsigned)(Tk_Width(tkwin) - 2 * mbPtr->inset),
                           (unsigned)(Tk_Height(tkwin) - 2 * mbPtr->inset));
        } else {
            XFillRectangle(mbPtr->display, Tk_WindowId(tkwin),
                           mbPtr->stippleGC,
                           imageXOffset, imageYOffset,
                           (unsigned)imageWidth, (unsigned)imageHeight);
        }
    }

    if (mbPtr->indicatorOn) {
        int borderWidth;
        int mm     = WidthMMOfScreen(Tk_Screen(mbPtr->tkwin));
        int pixels = WidthOfScreen(Tk_Screen(mbPtr->tkwin));
        mbPtr->indicatorHeight = (INDICATOR_HEIGHT * pixels) / (10 * mm);
        mbPtr->indicatorWidth  = (INDICATOR_WIDTH  * pixels) / (10 * mm)
                                 + 2 * mbPtr->indicatorHeight;
        borderWidth = (mbPtr->indicatorHeight + 1) / 3;
        if (borderWidth < 1) borderWidth = 1;
        Tk_Fill3DRectangle(tkwin, Tk_WindowId(tkwin), border,
                           Tk_Width(tkwin) - mbPtr->inset
                               - mbPtr->indicatorWidth
                               + mbPtr->indicatorHeight,
                           ((int)(Tk_Height(tkwin) - mbPtr->indicatorHeight)) / 2,
                           mbPtr->indicatorWidth - 2 * mbPtr->indicatorHeight,
                           mbPtr->indicatorHeight, borderWidth,
                           TK_RELIEF_RAISED);
    }

    Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->borderWidthObj,
                        &mbPtrBorderWidth);
    Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->highlightWidthObj,
                        &highlightWidth);

    if (mbPtr->relief != TK_RELIEF_FLAT) {
        Tk_Draw3DRectangle(tkwin, Tk_WindowId(tkwin), border,
                           highlightWidth, highlightWidth,
                           Tk_Width(tkwin) - 2 * highlightWidth,
                           Tk_Height(tkwin) - 2 * highlightWidth,
                           mbPtrBorderWidth, mbPtr->relief);
    }
    if (highlightWidth > 0) {
        if (mbPtr->flags & GOT_FOCUS) {
            gc = Tk_GCForColor(mbPtr->highlightColorPtr, Tk_WindowId(tkwin));
        } else {
            gc = Tk_GCForColor(mbPtr->highlightBgColorPtr, Tk_WindowId(tkwin));
        }
        Tk_DrawFocusHighlight(tkwin, gc, highlightWidth, Tk_WindowId(tkwin));
    }

    TkWaylandEndDraw(&dc);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetupMenuButtonCallbacks --
 *
 *	Register the GLFW cursor-position and mouse-button callbacks that
 *	route input events to TkpMenuButtonPostMenu.
 *
 *	This must be called once after the parent GLFWwindow is created,
 *	typically from TkWaylandSetupCallbacks in TkWaylandInit.c.
 *
 *	NOTE: On Wayland, mouse events on child widgets arrive via the
 *	parent GLFW window's callbacks.  We do NOT set per-menubutton
 *	callbacks; instead the main mouse-button callback in TkWaylandInit.c
 *	performs a hit-test and calls TkpMenuButtonMaybePost (below) when
 *	the click lands on a menubutton widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	No-op on Wayland.
 *
 *----------------------------------------------------------------------
 */

void
TkpSetupMenuButtonCallbacks(
    TCL_UNUSED(Tk_Window))  /* tkwin - Window to set up callbacks for */
{
    /*
     * No-op: the main GLFW mouse-button callback in TkWaylandInit.c handles
     * dispatching to TkpMenuButtonMaybePost.  Nothing to register here.
     */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMenuButtonMaybePost --
 *
 *	Called from the main GLFW mouse-button callback in TkWaylandInit.c
 *	when a button-1 press has been hit-tested to a menubutton widget.
 *
 *	The caller must have already called TkWaylandPopupSetSerial()
 *	with the button-event serial BEFORE calling this function so that
 *	TkpMenuButtonPostMenu can retrieve it.
 *
 * Parameters:
 *	winPtr  – the TkWindow corresponding to the hit-tested widget.
 *	          The caller has already verified it is a menubutton.
 *
 * Results:
 *	None.  Errors are silently ignored (widget may not be a menubutton,
 *	may be disabled, etc.).
 *
 * Side effects:
 *	Posts the menubutton's menu if the button is enabled.
 *
 *----------------------------------------------------------------------
 */

void
TkpMenuButtonMaybePost(
    TkWindow *winPtr)  /* Window to check for menubutton click */
{
    /*
     * Verify the window is actually a menubutton by checking its class.
     */
    if (!winPtr || !winPtr->classProcsPtr) return;
    if (strcmp(Tk_Class((Tk_Window)winPtr), "Menubutton") != 0) return;

    TkMenuButton *mbPtr = (TkMenuButton *)winPtr->instanceData;
    if (!mbPtr) return;
    if (mbPtr->state == STATE_DISABLED) return;

    /*
     * Activate the button visually - schedule a redraw.
     */
    mbPtr->state = STATE_ACTIVE;
    mbPtr->flags |= REDRAW_PENDING;
    Tcl_DoWhenIdle((Tcl_IdleProc *)TkpDisplayMenuButton, (ClientData)mbPtr);

    /*
     * Post the menu.  Serial was stored by the caller.
     */
    TkpMenuButtonPostMenu(mbPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * TkpMenuButtonFocus --
 *
 *	Handle focus changes for a menubutton.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the menubutton's display state based on focus.
 *
 *----------------------------------------------------------------------
 */

void
TkpMenuButtonFocus(
    TkMenuButton *mbPtr,  /* Menubutton to update */
    int gotFocus)         /* Non-zero if focus was gained, 0 if lost */
{
    if (!mbPtr || !mbPtr->tkwin) return;
    
    if (gotFocus) {
        mbPtr->flags |= GOT_FOCUS;
    } else {
        mbPtr->flags &= ~GOT_FOCUS;
    }
    
    mbPtr->flags |= REDRAW_PENDING;
    Tcl_DoWhenIdle((Tcl_IdleProc *)TkpDisplayMenuButton, (ClientData)mbPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMenuButtonState --
 *
 *	Update the state of a menubutton.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the menubutton's state and schedules a redraw.
 *
 *----------------------------------------------------------------------
 */

void
TkpMenuButtonState(
    TkMenuButton *mbPtr,   /* Menubutton to update */
    enum state newState)   /* New state for the menubutton */
{
    if (!mbPtr || !mbPtr->tkwin) return;
    
    if (mbPtr->state != newState) {
        mbPtr->state = newState;
        mbPtr->flags |= REDRAW_PENDING;
        Tcl_DoWhenIdle((Tcl_IdleProc *)TkpDisplayMenuButton, (ClientData)mbPtr);
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
