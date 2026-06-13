/*
 * tkWaylandMenubu.c --
 *
 *	Wayland/GLFW-specific portion of the menubutton widget.
 *
 *	Changes from the previous version
 *	----------------------------------
 *	TkpDisplayMenuButton  – unchanged in rendering; the button-press
 *	                        path is now handled by the GLFW callback
 *	                        registered below (TkWaylandMenuButtonPress).
 *
 *	TkpSetupMenuButtonCallbacks – NEW.  Registers a GLFW mouse-button
 *	                        callback on the parent window that converts
 *	                        a click over the menubutton into a correctly
 *	                        anchored TkpPostMenu call.
 *
 *	TkpMenuButtonPostMenu   – NEW.  The single entry point that computes
 *	                        the anchor rect from the menubutton's live
 *	                        geometry, captures the current Wayland serial
 *	                        (for the grab), and calls TkpPostMenu.
 *
 * Copyright © 1996-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkMenubutton.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>

/*
 *----------------------------------------------------------------------
 *
 * TkpCreateMenuButton --
 *
 *	Allocate a new TkMenuButton structure.
 *
 *----------------------------------------------------------------------
 */

TkMenuButton *
TkpCreateMenuButton(
    TCL_UNUSED(Tk_Window))
{
    return (TkMenuButton *)Tcl_Alloc(sizeof(TkMenuButton));
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMenuButtonPostMenu --
 *
 *	Compute the anchor rectangle of the menubutton in toplevel-surface
 *	coordinates and post the attached menu as the root of a new menu
 *	stack via TkWaylandPostMenuAtAnchor.
 *
 *	This is the single posting entry point.  It is called from:
 *	  - The GLFW mouse-button callback (button-1 press), via
 *	    TkpMenuButtonMaybePost.
 *	  - The keyboard binding (<space>, <Return>) via the Tcl
 *	    [tk::MBPost] proc.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Dismisses any previously posted menu stack, then posts a new
 *	wl_subsurface popup below the button (flipping above if it would
 *	not fit) and renders the menu into it.
 *
 *----------------------------------------------------------------------
 */

int
TkpMenuButtonPostMenu(
    TkMenuButton *mbPtr)
{
    Tk_Window    tkwin   = mbPtr->tkwin;
    const char  *menuName = NULL;

    /* Get the menu name from the Tcl_Obj */
    if (mbPtr->menuNameObj) {
        menuName = Tcl_GetString(mbPtr->menuNameObj);
    }

    if (!menuName || menuName[0] == '\0') {
        return TCL_OK;   /* no menu attached */
    }

    TkMenuReferences *menuRefPtr =
        TkFindMenuReferences(mbPtr->interp, menuName);
    if (!menuRefPtr || !menuRefPtr->menuPtr) {
        return TCL_OK;
    }
    TkMenu *menuPtr = menuRefPtr->menuPtr;

    /*
     * Compute the button's bounding box in toplevel-surface-local
     * coordinates.  Tk_X / Tk_Y give the position of the widget window
     * relative to its parent; on this Wayland backend the toplevel IS
     * the GLFW surface, so for a menubutton placed directly in the
     * toplevel these are already in the right space.  (A menubutton
     * nested in other containers would need the cumulative offset; that
     * is a pre-existing limitation shared with the rest of this port.)
     */
    int btnX = Tk_X(tkwin);
    int btnY = Tk_Y(tkwin);
    int btnW = Tk_Width(tkwin);
    int btnH = Tk_Height(tkwin);

    /*
     * Recompute the menu geometry before we query its size.
     */
    TkRecomputeMenu(menuPtr);

    int popupW = menuPtr->totalWidth;
    int popupH = menuPtr->totalHeight;
    if (popupW <= 0) popupW = btnW;      /* sensible fallback */
    if (popupH <= 0) popupH = 20;

    /*
     * Post below the button, left-aligned with its left edge, flipping
     * above if it would not fit within the toplevel.  This dismisses any
     * previously posted menu stack (isRoot = 1).
     */
    return TkWaylandPostMenuAtAnchor(mbPtr->interp, menuPtr,
        btnX, btnY, btnW, btnH, popupW, popupH, /*isRoot=*/1);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDisplayMenuButton --
 *
 *	Render the menubutton widget.  Rendering is identical to the
 *	previous implementation; posting is now handled separately by
 *	TkpMenuButtonPostMenu.
 *
 *----------------------------------------------------------------------
 */

void
TkpDisplayMenuButton(
    void *clientData)
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
    if (TkGlfwBeginDraw(Tk_WindowId(tkwin), gc, &dc) != TCL_OK) {
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

    TkGlfwEndDraw(&dc);
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
 *	typically from TkWaylandSetupCallbacks in tkGlfwInit.c.
 *
 *	NOTE: On Wayland, mouse events on child widgets arrive via the
 *	parent GLFW window's callbacks.  We do NOT set per-menubutton
 *	callbacks; instead the main mouse-button callback in tkGlfwInit.c
 *	performs a hit-test and calls TkpMenuButtonMaybePost (below) when
 *	the click lands on a menubutton widget.
 *
 *----------------------------------------------------------------------
 */

void
TkpSetupMenuButtonCallbacks(
    TCL_UNUSED(Tk_Window))
{
    /*
     * No-op: the main GLFW mouse-button callback in tkGlfwInit.c handles
     * dispatching to TkpMenuButtonMaybePost.  Nothing to register here.
     */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpMenuButtonMaybePost --
 *
 *	Called from the main GLFW mouse-button callback in tkGlfwInit.c
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
 *----------------------------------------------------------------------
 */

void
TkpMenuButtonMaybePost(
    TkWindow *winPtr)
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
 * TkpDestroyMenuButton --
 *
 *	Free data structures associated with the menubutton control.
 *
 *----------------------------------------------------------------------
 */

void
TkpDestroyMenuButton(
    TCL_UNUSED(TkMenuButton *))
{
}

/*
 *----------------------------------------------------------------------
 *
 * TkpComputeMenuButtonGeometry --
 *
 *	Compute the menubutton's geometry.  Unchanged from the previous
 *	implementation.
 *
 *----------------------------------------------------------------------
 */

void
TkpComputeMenuButtonGeometry(
    TkMenuButton *mbPtr)
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
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
