/*
 * rbcText.c --
 *
 *      This module implements multi-line, rotate-able text for the rbc toolkit.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

#define WINDEBUG	0

static Tcl_HashTable bitmapGCTable;
static int      initialized;

static void     DrawTextLayout(
    Display * display,
    Drawable drawable,
    GC gc,
    Tk_Font font,
    register int x,
    register int y,
    RbcTextLayout * textPtr);
static Pixmap CreateTextBitmap(
    Tk_Window tkwin,
    RbcTextLayout * textPtr,
    RbcTextStyle * stylePtr,
    int *widthPtr,
    int *heightPtr);

/*
 *--------------------------------------------------------------
 *
 * DrawTextLayout --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void
DrawTextLayout(
    Display * display,
    Drawable drawable,
    GC gc,
    Tk_Font font,
    register int x,             /* Origin of text */
    register int y,             /* Origin of text */
    RbcTextLayout * textPtr)
{
    register RbcTextFragment *fragPtr;
    register int    i;

    fragPtr = textPtr->fragArr;
    for (i = 0; i < textPtr->nFrags; i++, fragPtr++) {
        Tk_DrawChars(display, drawable, gc, font, fragPtr->text, fragPtr->count,
            x + fragPtr->x, y + fragPtr->y);
    }
}

/*
 * -----------------------------------------------------------------
 *
 * RbcGetTextLayout --
 *
 *      Get the extents of a possibly multiple-lined text string.
 *
 * Results:
 *      Returns via *widthPtr* and *heightPtr* the dimensions of
 *      the text string.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
RbcTextLayout  *
RbcGetTextLayout(
    char string[],
    RbcTextStyle * tsPtr)
{
    int             maxHeight, maxWidth;
    int             count;      /* Count # of characters on each line */
    int             nFrags;
    int             width;      /* Running dimensions of the text */
    RbcTextFragment *fragPtr;
    RbcTextLayout  *textPtr;
    int             lineHeight;
    int             size;
    register char  *p;
    register int    i;
    Tk_FontMetrics  fontMetrics;

    Tk_GetFontMetrics(tsPtr->font, &fontMetrics);
    lineHeight = fontMetrics.linespace + tsPtr->leader + tsPtr->shadow.offset;
    nFrags = 0;
    for (p = string; *p != '\0'; p++) {
        if (*p == '\n') {
            nFrags++;
        }
    }
    if ((p != string) && (*(p - 1) != '\n')) {
        nFrags++;
    }
    size = sizeof(RbcTextLayout) + (sizeof(RbcTextFragment) * (nFrags - 1));
    textPtr = RbcCalloc(1, size);
    textPtr->nFrags = nFrags;
    nFrags = count = 0;
    width = maxWidth = 0;
    maxHeight = tsPtr->padY.side1;
    fragPtr = textPtr->fragArr;
    for (p = string; *p != '\0'; p++) {
        if (*p == '\n') {
            if (count > 0) {
                width = Tk_TextWidth(tsPtr->font, string, count) +
                    tsPtr->shadow.offset;
                if (width > maxWidth) {
                    maxWidth = width;
                }
            }
            fragPtr->width = width;
            fragPtr->count = count;
            fragPtr->y = maxHeight + fontMetrics.ascent;
            fragPtr->text = string;
            fragPtr++;
            nFrags++;
            maxHeight += lineHeight;
            string = p + 1;     /* Start the string on the next line */
            count = 0;          /* Reset to indicate the start of a new line */
            continue;
        }
        count++;
    }
    if (nFrags < textPtr->nFrags) {
        width = Tk_TextWidth(tsPtr->font, string, count) + tsPtr->shadow.offset;
        if (width > maxWidth) {
            maxWidth = width;
        }
        fragPtr->width = width;
        fragPtr->count = count;
        fragPtr->y = maxHeight + fontMetrics.ascent;
        fragPtr->text = string;
        maxHeight += lineHeight;
        nFrags++;
    }
    maxHeight += tsPtr->padY.side2;
    maxWidth += RbcPadding(tsPtr->padX);
    fragPtr = textPtr->fragArr;
    for (i = 0; i < nFrags; i++, fragPtr++) {
        switch (tsPtr->justify) {
        default:
        case TK_JUSTIFY_LEFT:
            /* No offset for left justified text strings */
            fragPtr->x = tsPtr->padX.side1;
            break;
        case TK_JUSTIFY_RIGHT:
            fragPtr->x = (maxWidth - fragPtr->width) - tsPtr->padX.side2;
            break;
        case TK_JUSTIFY_CENTER:
            fragPtr->x = (maxWidth - fragPtr->width) / 2;
            break;
        }
    }
    textPtr->width = maxWidth;
    textPtr->height = maxHeight - tsPtr->leader;
    return textPtr;
}

/*
 * -----------------------------------------------------------------
 *
 * RbcGetTextExtents --
 *
 *      Get the extents of a possibly multiple-lined text string.
 *
 * Results:
 *      Returns via *widthPtr* and *heightPtr* the dimensions of
 *      the text string.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
void
RbcGetTextExtents(
    RbcTextStyle * tsPtr,
    char *string,
    int *widthPtr,
    int *heightPtr)
{
    int             count;      /* Count # of characters on each line */
    int             width, height;
    int             w, lineHeight;
    register char  *p;
    Tk_FontMetrics  fontMetrics;

    if (string == NULL) {
        return;                 /* NULL string? */
    }
    Tk_GetFontMetrics(tsPtr->font, &fontMetrics);
    lineHeight = fontMetrics.linespace + tsPtr->leader + tsPtr->shadow.offset;
    count = 0;
    width = height = 0;
    for (p = string; *p != '\0'; p++) {
        if (*p == '\n') {
            if (count > 0) {
                w = Tk_TextWidth(tsPtr->font, string, count) +
                    tsPtr->shadow.offset;
                if (w > width) {
                    width = w;
                }
            }
            height += lineHeight;
            string = p + 1;     /* Start the string on the next line */
            count = 0;          /* Reset to indicate the start of a new line */
            continue;
        }
        count++;
    }
    if ((count > 0) && (*(p - 1) != '\n')) {
        height += lineHeight;
        w = Tk_TextWidth(tsPtr->font, string, count) + tsPtr->shadow.offset;
        if (w > width) {
            width = w;
        }
    }
    *widthPtr = width + RbcPadding(tsPtr->padX);
    *heightPtr = height + RbcPadding(tsPtr->padY);
}

/*
 * -----------------------------------------------------------------
 *
 * RbcGetBoundingBox
 *
 *      Computes the dimensions of the bounding box surrounding a
 *      rectangle rotated about its center.  If pointArr isn't NULL,
 *      the coordinates of the rotated rectangle are also returned.
 *
 *      The dimensions are determined by rotating the rectangle, and
 *      doubling the maximum x-coordinate and y-coordinate.
 *
 *        w = 2 * maxX,  h = 2 * maxY
 *
 *      Since the rectangle is centered at 0,0, the coordinates of
 *      the bounding box are (-w/2,-h/2 w/2,-h/2, w/2,h/2 -w/2,h/2).
 *
 *        0 ------- 1
 *        |         |
 *        |    x    |
 *        |         |
 *        3 ------- 2
 *
 * Results:
 *      The width and height of the bounding box containing the
 *      rotated rectangle are returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
void
RbcGetBoundingBox(
    int width,                  /* Unrotated region */
    int height,                 /* Unrotated region */
    double theta,               /* Rotation of box */
    double *rotWidthPtr,        /* (out) Bounding box region */
    double *rotHeightPtr,       /* (out) Bounding box region */
    RbcPoint2D * bbox)
{                               /* (out) Points of the rotated box */
    register int    i;
    double          sinTheta, cosTheta;
    double          xMax, yMax;
    register double x, y;
    RbcPoint2D      corner[4];

    theta = FMOD(theta, 360.0);
    if (FMOD(theta, (double) 90.0) == 0.0) {
        int             ll, ur, ul, lr;
        double          rotWidth, rotHeight;
        int             quadrant;

        /* Handle right-angle rotations specifically */

        quadrant = (int) (theta / 90.0);
        switch (quadrant) {
        case RBC_ROTATE_270:   /* 270 degrees */
            ul = 3, ur = 0, lr = 1, ll = 2;
            rotWidth = (double) height;
            rotHeight = (double) width;
            break;
        case RBC_ROTATE_90:    /* 90 degrees */
            ul = 1, ur = 2, lr = 3, ll = 0;
            rotWidth = (double) height;
            rotHeight = (double) width;
            break;
        case RBC_ROTATE_180:   /* 180 degrees */
            ul = 2, ur = 3, lr = 0, ll = 1;
            rotWidth = (double) width;
            rotHeight = (double) height;
            break;
        default:
        case RBC_ROTATE_0:     /* 0 degrees */
            ul = 0, ur = 1, lr = 2, ll = 3;
            rotWidth = (double) width;
            rotHeight = (double) height;
            break;
        }
        if (bbox != NULL) {
            x = rotWidth * 0.5;
            y = rotHeight * 0.5;
            bbox[ll].x = bbox[ul].x = -x;
            bbox[ur].y = bbox[ul].y = -y;
            bbox[lr].x = bbox[ur].x = x;
            bbox[ll].y = bbox[lr].y = y;
        }
        *rotWidthPtr = rotWidth;
        *rotHeightPtr = rotHeight;
        return;
    }
    /* Set the four corners of the rectangle whose center is the origin */

    corner[1].x = corner[2].x = (double) width *0.5;
    corner[0].x = corner[3].x = -corner[1].x;
    corner[2].y = corner[3].y = (double) height *0.5;
    corner[0].y = corner[1].y = -corner[2].y;

    theta = (-theta / 180.0) * M_PI;
    sinTheta = sin(theta), cosTheta = cos(theta);
    xMax = yMax = 0.0;

    /* Rotate the four corners and find the maximum X and Y coordinates */

    for (i = 0; i < 4; i++) {
        x = (corner[i].x * cosTheta) - (corner[i].y * sinTheta);
        y = (corner[i].x * sinTheta) + (corner[i].y * cosTheta);
        if (x > xMax) {
            xMax = x;
        }
        if (y > yMax) {
            yMax = y;
        }
        if (bbox != NULL) {
            bbox[i].x = x;
            bbox[i].y = y;
        }
    }

    /*
     * By symmetry, the width and height of the bounding box are
     * twice the maximum x and y coordinates.
     */
    *rotWidthPtr = xMax + xMax;
    *rotHeightPtr = yMax + yMax;
}

/*
 * -----------------------------------------------------------------
 *
 * RbcTranslateAnchor --
 *
 *      Translate the coordinates of a given bounding box based
 *      upon the anchor specified.  The anchor indicates where
 *      the given xy position is in relation to the bounding box.
 *
 *        nw --- n --- ne
 *        |            |
 *        w   center   e
 *        |            |
 *        sw --- s --- se
 *
 *      The coordinates returned are translated to the origin of the
 *      bounding box (suitable for giving to XCopyArea, XCopyPlane, etc.)
 *
 * Results:
 *      The translated coordinates of the bounding box are returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
void
RbcTranslateAnchor(
    int x,                      /* Window coordinates of anchor */
    int y,                      /* Window coordinates of anchor */
    int width,                  /* Extents of the bounding box */
    int height,                 /* Extents of the bounding box */
    Tk_Anchor anchor,           /* Direction of the anchor */
    int *transXPtr,
    int *transYPtr)
{
    switch (anchor) {
    case TK_ANCHOR_NW:         /* Upper left corner */
        break;
    case TK_ANCHOR_W:          /* Left center */
        y -= (height / 2);
        break;
    case TK_ANCHOR_SW:         /* Lower left corner */
        y -= height;
        break;
    case TK_ANCHOR_N:          /* Top center */
        x -= (width / 2);
        break;
    case TK_ANCHOR_CENTER:     /* Center */
        x -= (width / 2);
        y -= (height / 2);
        break;
    case TK_ANCHOR_S:          /* Bottom center */
        x -= (width / 2);
        y -= height;
        break;
    case TK_ANCHOR_NE:         /* Upper right corner */
        x -= width;
        break;
    case TK_ANCHOR_E:          /* Right center */
        x -= width;
        y -= (height / 2);
        break;
    case TK_ANCHOR_SE:         /* Lower right corner */
        x -= width;
        y -= height;
        break;
    }
    *transXPtr = x;
    *transYPtr = y;
}

/*
 * -----------------------------------------------------------------
 *
 * RbcTranslatePoint --
 *
 *      Translate the coordinates of a given bounding box based
 *      upon the anchor specified.  The anchor indicates where
 *      the given xy position is in relation to the bounding box.
 *
 *        nw --- n --- ne
 *        |            |
 *        w   center   e
 *        |            |
 *        sw --- s --- se
 *
 *      The coordinates returned are translated to the origin of the
 *      bounding box (suitable for giving to XCopyArea, XCopyPlane, etc.)
 *
 * Results:
 *      The translated coordinates of the bounding box are returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
RbcPoint2D
RbcTranslatePoint(
    RbcPoint2D * pointPtr,      /* Window coordinates of anchor */
    int width,                  /* Extents of the bounding box */
    int height,                 /* Extents of the bounding box */
    Tk_Anchor anchor)
{                               /* Direction of the anchor */
    RbcPoint2D      trans;

    trans = *pointPtr;
    switch (anchor) {
    case TK_ANCHOR_NW:         /* Upper left corner */
        break;
    case TK_ANCHOR_W:          /* Left center */
        trans.y -= (height * 0.5);
        break;
    case TK_ANCHOR_SW:         /* Lower left corner */
        trans.y -= height;
        break;
    case TK_ANCHOR_N:          /* Top center */
        trans.x -= (width * 0.5);
        break;
    case TK_ANCHOR_CENTER:     /* Center */
        trans.x -= (width * 0.5);
        trans.y -= (height * 0.5);
        break;
    case TK_ANCHOR_S:          /* Bottom center */
        trans.x -= (width * 0.5);
        trans.y -= height;
        break;
    case TK_ANCHOR_NE:         /* Upper right corner */
        trans.x -= width;
        break;
    case TK_ANCHOR_E:          /* Right center */
        trans.x -= width;
        trans.y -= (height * 0.5);
        break;
    case TK_ANCHOR_SE:         /* Lower right corner */
        trans.x -= width;
        trans.y -= height;
        break;
    }
    return trans;
}

/*
 * -----------------------------------------------------------------
 *
 * CreateTextBitmap --
 *
 *      Draw a bitmap, using the the given window coordinates
 *      as an anchor for the text bounding box.
 *
 * Results:
 *      Returns the bitmap representing the text string.
 *
 * Side Effects:
 *      Bitmap is drawn using the given font and GC in the
 *      drawable at the given coordinates, anchor, and rotation.
 *
 * -----------------------------------------------------------------
 */
static Pixmap
CreateTextBitmap(
    Tk_Window tkwin,
    RbcTextLayout * textPtr,    /* Text string to draw */
    RbcTextStyle * tsPtr,       /* Text attributes: rotation, color, font,
                                 * linespacing, justification, etc. */
    int *bmWidthPtr,
    int *bmHeightPtr)
{                               /* Extents of rotated text string */
    int             width, height;
    Pixmap          bitmap;
    Display        *display;
    Window          root;
    GC              gc;
#ifdef _WIN32
    HDC             hDC;
    TkWinDCState    state;
#endif
    display = Tk_Display(tkwin);

    width = textPtr->width;
    height = textPtr->height;

    /* Create a temporary bitmap to contain the text string */
    root = RootWindow(display, Tk_ScreenNumber(tkwin));
    bitmap = Tk_GetPixmap(display, root, width, height, 1);
    assert(bitmap != None);
    if (bitmap == None) {
        return None;            /* Can't allocate pixmap. */
    }
    /* Clear the pixmap and draw the text string into it */
    gc = RbcGetBitmapGC(tkwin);
#ifdef _WIN32
    hDC = TkWinGetDrawableDC(display, bitmap, &state);
    PatBlt(hDC, 0, 0, width, height, WHITENESS);
    TkWinReleaseDrawableDC(bitmap, hDC, &state);
#else
    XSetForeground(display, gc, 0);
    XFillRectangle(display, bitmap, gc, 0, 0, width, height);
#endif /* _WIN32 */

    XSetFont(display, gc, Tk_FontId(tsPtr->font));
    XSetForeground(display, gc, 1);
    DrawTextLayout(display, bitmap, gc, tsPtr->font, 0, 0, textPtr);

#ifdef _WIN32
    /*
     * Under Win32 when drawing into a bitmap, the bits are
     * reversed. Which is why we are inverting the bitmap here.
     */
    hDC = TkWinGetDrawableDC(display, bitmap, &state);
    PatBlt(hDC, 0, 0, width, height, DSTINVERT);
    TkWinReleaseDrawableDC(bitmap, hDC, &state);
#endif
    if (tsPtr->theta != 0.0) {
        Pixmap          rotBitmap;

        /* Replace the text pixmap with a rotated one */

        rotBitmap = RbcRotateBitmap(tkwin, bitmap, width, height,
            tsPtr->theta, bmWidthPtr, bmHeightPtr);
        assert(rotBitmap);
        if (rotBitmap != None) {
            Tk_FreePixmap(display, bitmap);
            return rotBitmap;
        }
    }
    *bmWidthPtr = textPtr->width, *bmHeightPtr = textPtr->height;
    return bitmap;
}

/*
 *--------------------------------------------------------------
 *
 * RbcInitTextStyle --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcInitTextStyle(
    RbcTextStyle * tsPtr)
{
    /* Initialize these attributes to zero */
    tsPtr->activeColor = (XColor *) NULL;
    tsPtr->anchor = TK_ANCHOR_CENTER;
    tsPtr->color = (XColor *) NULL;
    tsPtr->font = NULL;
    tsPtr->justify = TK_JUSTIFY_CENTER;
    tsPtr->leader = 0;
    tsPtr->padX.side1 = tsPtr->padX.side2 = 0;  /*x-padding */
    tsPtr->padY.side1 = tsPtr->padY.side2 = 0;  /*y-padding */
    tsPtr->shadow.color = (XColor *) NULL;
    tsPtr->shadow.offset = 0;
    tsPtr->state = 0;
    tsPtr->theta = 0.0;
}

/*
 *--------------------------------------------------------------
 *
 * RbcSetDrawTextStyle --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcSetDrawTextStyle(
    RbcTextStyle * tsPtr,
    Tk_Font font,
    GC gc,
    XColor * normalColor,
    XColor * activeColor,
    XColor * shadowColor,
    double theta,
    Tk_Anchor anchor,
    Tk_Justify justify,
    int leader,
    int shadowOffset)
{
    RbcInitTextStyle(tsPtr);
    tsPtr->activeColor = activeColor;
    tsPtr->anchor = anchor;
    tsPtr->color = normalColor;
    tsPtr->font = font;
    tsPtr->gc = gc;
    tsPtr->justify = justify;
    tsPtr->leader = leader;
    tsPtr->shadow.color = shadowColor;
    tsPtr->shadow.offset = shadowOffset;
    tsPtr->theta = theta;
}

/*
 *--------------------------------------------------------------
 *
 * RbcSetPrintTextStyle --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcSetPrintTextStyle(
    RbcTextStyle * tsPtr,
    Tk_Font font,
    XColor * fgColor,
    XColor * activeColor,
    XColor * shadowColor,
    double theta,
    Tk_Anchor anchor,
    Tk_Justify justify,
    int leader,
    int shadowOffset)
{
    RbcInitTextStyle(tsPtr);
    tsPtr->color = fgColor;
    tsPtr->activeColor = activeColor;
    tsPtr->shadow.color = shadowColor;
    tsPtr->font = font;
    tsPtr->theta = theta;
    tsPtr->anchor = anchor;
    tsPtr->justify = justify;
    tsPtr->leader = leader;
    tsPtr->shadow.offset = shadowOffset;
}

/*
 * -----------------------------------------------------------------
 *
 * RbcDrawTextLayout --
 *
 *      Draw a text string, possibly rotated, using the the given
 *      window coordinates as an anchor for the text bounding box.
 *      If the text is not rotated, simply use the X text drawing
 *      routines. Otherwise, generate a bitmap of the rotated text.
 *
 * Results:
 *      Returns the x-coordinate to the right of the text.
 *
 * Side Effects:
 *      Text string is drawn using the given font and GC at the
 *      the given window coordinates.
 *
 *      The Stipple, FillStyle, and TSOrigin fields of the GC are
 *      modified for rotated text.  This assumes the GC is private,
 *      *not* shared (via Tk_GetGC)
 *
 * -----------------------------------------------------------------
 */
void
RbcDrawTextLayout(
    Tk_Window tkwin,
    Drawable drawable,
    RbcTextLayout * textPtr,
    RbcTextStyle * tsPtr,       /* Text attribute information */
    int x,                      /* Window coordinates to draw text */
    int y)
{                               /* Window coordinates to draw text */
    int             width, height;
    double          theta;
    Display        *display;
    Pixmap          bitmap;
    int             active;

    display = Tk_Display(tkwin);
    theta = FMOD(tsPtr->theta, (double) 360.0);
    if (theta < 0.0) {
        theta += 360.0;
    }
    active = tsPtr->state & RBC_STATE_ACTIVE;
    if (theta == 0.0) {

        /*
         * This is the easy case of no rotation. Simply draw the text
         * using the standard drawing routines.  Handle offset printing
         * for engraved (disabled) and shadowed text.
         */
        width = textPtr->width, height = textPtr->height;
        RbcTranslateAnchor(x, y, width, height, tsPtr->anchor, &x, &y);
        if (tsPtr->state & (RBC_STATE_DISABLED | RBC_STATE_EMPHASIS)) {
            TkBorder       *borderPtr = (TkBorder *) tsPtr->border;
            XColor         *color1, *color2;

            color1 = borderPtr->lightColorPtr, color2 = borderPtr->darkColorPtr;
            if (tsPtr->state & RBC_STATE_EMPHASIS) {
                XColor         *hold;

                hold = color1, color1 = color2, color2 = hold;
            }
            if (color1 != NULL) {
                XSetForeground(display, tsPtr->gc, color1->pixel);
            }
            DrawTextLayout(display, drawable, tsPtr->gc, tsPtr->font, x + 1,
                y + 1, textPtr);
            if (color2 != NULL) {
                XSetForeground(display, tsPtr->gc, color2->pixel);
            }
            DrawTextLayout(display, drawable, tsPtr->gc, tsPtr->font, x, y,
                textPtr);

            /* Reset the foreground color back to its original setting,
             * so not to invalidate the GC cache. */
            XSetForeground(display, tsPtr->gc, tsPtr->color->pixel);

            return;             /* Done */
        }
        if ((tsPtr->shadow.offset > 0) && (tsPtr->shadow.color != NULL)) {
            XSetForeground(display, tsPtr->gc, tsPtr->shadow.color->pixel);
            DrawTextLayout(display, drawable, tsPtr->gc, tsPtr->font,
                x + tsPtr->shadow.offset, y + tsPtr->shadow.offset, textPtr);
            XSetForeground(display, tsPtr->gc, tsPtr->color->pixel);
        }
        if (active) {
            XSetForeground(display, tsPtr->gc, tsPtr->activeColor->pixel);
        }
        DrawTextLayout(display, drawable, tsPtr->gc, tsPtr->font, x, y,
            textPtr);
        if (active) {
            XSetForeground(display, tsPtr->gc, tsPtr->color->pixel);
        }
        return;                 /* Done */
    }
#ifdef _WIN32
    if (RbcDrawRotatedText(display, drawable, x, y, theta, tsPtr, textPtr)) {
        return;
    }
#endif
    /*
     * Rotate the text by writing the text into a bitmap and rotating
     * the bitmap.  Set the clip mask and origin in the GC first.  And
     * make sure we restore the GC because it may be shared.
     */
    tsPtr->theta = theta;
    bitmap = CreateTextBitmap(tkwin, textPtr, tsPtr, &width, &height);
    if (bitmap == None) {
        return;
    }
    RbcTranslateAnchor(x, y, width, height, tsPtr->anchor, &x, &y);
#ifdef notdef
    theta = FMOD(theta, (double) 90.0);
#endif
    XSetClipMask(display, tsPtr->gc, bitmap);

    if (tsPtr->state & (RBC_STATE_DISABLED | RBC_STATE_EMPHASIS)) {
        TkBorder       *borderPtr = (TkBorder *) tsPtr->border;
        XColor         *color1, *color2;

        color1 = borderPtr->lightColorPtr, color2 = borderPtr->darkColorPtr;
        if (tsPtr->state & RBC_STATE_EMPHASIS) {
            XColor         *hold;

            hold = color1, color1 = color2, color2 = hold;
        }
        if (color1 != NULL) {
            XSetForeground(display, tsPtr->gc, color1->pixel);
        }
        XSetClipOrigin(display, tsPtr->gc, x + 1, y + 1);
        XCopyPlane(display, bitmap, drawable, tsPtr->gc, 0, 0, width,
            height, x + 1, y + 1, 1);
        if (color2 != NULL) {
            XSetForeground(display, tsPtr->gc, color2->pixel);
        }
        XSetClipOrigin(display, tsPtr->gc, x, y);
        XCopyPlane(display, bitmap, drawable, tsPtr->gc, 0, 0, width,
            height, x, y, 1);
        XSetForeground(display, tsPtr->gc, tsPtr->color->pixel);
    } else {
        if ((tsPtr->shadow.offset > 0) && (tsPtr->shadow.color != NULL)) {
            XSetClipOrigin(display, tsPtr->gc, x + tsPtr->shadow.offset,
                y + tsPtr->shadow.offset);
            XSetForeground(display, tsPtr->gc, tsPtr->shadow.color->pixel);
            XCopyPlane(display, bitmap, drawable, tsPtr->gc, 0, 0, width,
                height, x + tsPtr->shadow.offset, y + tsPtr->shadow.offset, 1);
            XSetForeground(display, tsPtr->gc, tsPtr->color->pixel);
        }
        if (active) {
            XSetForeground(display, tsPtr->gc, tsPtr->activeColor->pixel);
        }
        XSetClipOrigin(display, tsPtr->gc, x, y);
        XCopyPlane(display, bitmap, drawable, tsPtr->gc, 0, 0, width, height,
            x, y, 1);
        if (active) {
            XSetForeground(display, tsPtr->gc, tsPtr->color->pixel);
        }
    }
    XSetClipMask(display, tsPtr->gc, None);
    Tk_FreePixmap(display, bitmap);
}

/*
 *--------------------------------------------------------------
 *
 * RbcDrawText2 --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcDrawText2(
    Tk_Window tkwin,
    Drawable drawable,
    char string[],
    RbcTextStyle * tsPtr,       /* Text attribute information */
    int x,                      /* Window coordinates to draw text */
    int y,                      /* Window coordinates to draw text */
    RbcDim2D * areaPtr)
{
    RbcTextLayout  *textPtr;
    int             width, height;
    double          theta;

    if ((string == NULL) || (*string == '\0')) {
        return;                 /* Empty string, do nothing */
    }
    textPtr = RbcGetTextLayout(string, tsPtr);
    RbcDrawTextLayout(tkwin, drawable, textPtr, tsPtr, x, y);
    theta = FMOD(tsPtr->theta, (double) 360.0);
    if (theta < 0.0) {
        theta += 360.0;
    }
    width = textPtr->width;
    height = textPtr->height;
    if (theta != 0.0) {
        double          rotWidth, rotHeight;

        RbcGetBoundingBox(width, height, theta, &rotWidth, &rotHeight,
            (RbcPoint2D *) NULL);
        width = ROUND(rotWidth);
        height = ROUND(rotHeight);
    }
    areaPtr->width = width;
    areaPtr->height = height;
    ckfree((char *) textPtr);
}

/*
 *--------------------------------------------------------------
 *
 * RbcDrawText --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcDrawText(
    Tk_Window tkwin,
    Drawable drawable,
    char string[],
    RbcTextStyle * tsPtr,       /* Text attribute information */
    int x,                      /* Window coordinates to draw text */
    int y)
{                               /* Window coordinates to draw text */
    RbcTextLayout  *textPtr;

    if ((string == NULL) || (*string == '\0')) {
        return;                 /* Empty string, do nothing */
    }
    textPtr = RbcGetTextLayout(string, tsPtr);
    RbcDrawTextLayout(tkwin, drawable, textPtr, tsPtr, x, y);
    ckfree((char *) textPtr);
}

/*
 *--------------------------------------------------------------
 *
 * RbcGetBitmapGC --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
GC
RbcGetBitmapGC(
    Tk_Window tkwin)
{
    int             isNew;
    GC              gc;
    Display        *display;
    Tcl_HashEntry  *hPtr;

    if (!initialized) {
        Tcl_InitHashTable(&bitmapGCTable, TCL_ONE_WORD_KEYS);
        initialized = TRUE;
    }
    display = Tk_Display(tkwin);
    hPtr = Tcl_CreateHashEntry(&bitmapGCTable, (char *) display, &isNew);
    if (isNew) {
        Pixmap          bitmap;
        XGCValues       gcValues;
        unsigned long   gcMask;
        Window          root;

        root = RootWindow(display, Tk_ScreenNumber(tkwin));
        bitmap = Tk_GetPixmap(display, root, 1, 1, 1);
        gcValues.foreground = gcValues.background = 0;
        gcMask = (GCForeground | GCBackground);
        gc = RbcGetPrivateGCFromDrawable(display, bitmap, gcMask, &gcValues);
        Tk_FreePixmap(display, bitmap);
        Tcl_SetHashValue(hPtr, gc);
    } else {
        gc = (GC) Tcl_GetHashValue(hPtr);
    }
    return gc;
}

/*
 *--------------------------------------------------------------
 *
 * RbcResetTextStyle --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcResetTextStyle(
    Tk_Window tkwin,
    RbcTextStyle * tsPtr)
{
    GC              newGC;
    XGCValues       gcValues;
    unsigned long   gcMask;

    gcMask = GCFont;
    gcValues.font = Tk_FontId(tsPtr->font);
    if (tsPtr->color != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = tsPtr->color->pixel;
    }
    newGC = Tk_GetGC(tkwin, gcMask, &gcValues);
    if (tsPtr->gc != NULL) {
        Tk_FreeGC(Tk_Display(tkwin), tsPtr->gc);
    }
    tsPtr->gc = newGC;
}

/*
 *--------------------------------------------------------------
 *
 * RbcFreeTextStyle --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcFreeTextStyle(
    Display * display,
    RbcTextStyle * tsPtr)
{
    if (tsPtr->gc != NULL) {
        Tk_FreeGC(display, tsPtr->gc);
    }
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
