/*
 * ttkMacOSXTheme.c --
 *
 *      Tk theme engine for Mac OSX, using the Appearance Manager API.
 *
 * Copyright (c) 2004 Joe English
 * Copyright (c) 2005 Neil Madden
 * Copyright (c) 2006-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright 2008-2009, Apple Inc.
 * Copyright 2009 Kevin Walzer/WordTech Communications LLC.
 * Copyright 2019 Marc Culler
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * See also:
 *
 * <URL: http://developer.apple.com/documentation/Carbon/Reference/
 *      Appearance_Manager/appearance_manager/APIIndex.html >
 *
 * Notes:
 *      "Active" means different things in Mac and Tk terminology --
 *      On Aqua, widgets are "Active" if they belong to the foreground window,
 *      "Inactive" if they are in a background window.  Tk uses the term
 *      "active" to mean that the mouse cursor is over a widget; aka "hover",
 *      "prelight", or "hot-tracked".  Aqua doesn't use this kind of feedback.
 *
 *      The QuickDraw/Carbon coordinate system is relative to the top-level
 *      window, not to the Tk_Window.  BoxToRect() accounts for this.
 */

#include "tkMacOSXPrivate.h"
#include "ttk/ttkTheme.h"
#include <math.h>

/*
 * Macros for handling drawing contexts.
 */

#define BEGIN_DRAWING(d) {	   \
	TkMacOSXDrawingContext dc; \
	if (!TkMacOSXSetupDrawingContext((d), NULL, 1, &dc)) {return;}
#define END_DRAWING \
    TkMacOSXRestoreDrawingContext(&dc);}

#define HIOrientation kHIThemeOrientationNormal
#define NoThemeMetric 0xFFFFFFFF

#ifdef __LP64__
#define RangeToFactor(maximum) (((double) (INT_MAX >> 1)) / (maximum))
#else
#define RangeToFactor(maximum) (((double) (LONG_MAX >> 1)) / (maximum))
#endif /* __LP64__ */

#define TTK_STATE_FIRST_TAB     TTK_STATE_USER1
#define TTK_STATE_LAST_TAB      TTK_STATE_USER2
#define TTK_TREEVIEW_STATE_SORTARROW    TTK_STATE_USER1

/*
 * Colors and gradients used in Dark Mode.
 */

typedef struct GrayColor {
    CGFloat grayscale;
    CGFloat alpha;
} GrayColor;

#define RGBACOLOR static CGFloat
#define RGBA256(r, g, b, a) {r / 255.0, g / 255.0, b / 255.0, a}
#define GRAYCOLOR static GrayColor
#define GRAY256(grayscale) {grayscale / 255.0, 1.0}

/* Opaque Grays */
GRAYCOLOR darkButtonFace = GRAY256(86.0);
GRAYCOLOR darkDisabledButtonFace = GRAY256(86.0);
GRAYCOLOR darkPressedButtonFace = GRAY256(115.0);
GRAYCOLOR darkSelectedButtonFace = GRAY256(134.0);
GRAYCOLOR darkInactiveSelectedTab = GRAY256(134.0);

GRAYCOLOR darkGradientNormal = GRAY256(62.0);
GRAYCOLOR darkGradientPressed = GRAY256(92.0);
GRAYCOLOR darkGradientBorder = GRAY256(80.0);
GRAYCOLOR lightGradientNormal = GRAY256(244.0);
GRAYCOLOR lightGradientPressed = GRAY256(175.0);
GRAYCOLOR lightGradientBorder = GRAY256(165.0);

GRAYCOLOR lightTrough = GRAY256(250.0);
GRAYCOLOR darkTrough = GRAY256(47.0);
GRAYCOLOR lightInactiveThumb = GRAY256(200.0);
GRAYCOLOR lightActiveThumb = GRAY256(133.0);
GRAYCOLOR darkInactiveThumb = GRAY256(117.0);
GRAYCOLOR darkActiveThumb = GRAY256(158.0);

GRAYCOLOR listheaderBorder = GRAY256(200.0);
GRAYCOLOR listheaderSeparator = GRAY256(220.0);
GRAYCOLOR listheaderActiveBG = GRAY256(238.0);
GRAYCOLOR listheaderInactiveBG = GRAY256(246.0);

/* Transparent Grays */
GRAYCOLOR boxBorder = {1.0, 0.20};
GRAYCOLOR darkTrack = {1.0, 0.25};
GRAYCOLOR darkFrameTop = {1.0, 0.0625};
GRAYCOLOR darkFrameBottom = {1.0, 0.125};
GRAYCOLOR darkSeparator = {1.0, 0.3};
GRAYCOLOR darkTabSeparator = {0.0, 0.25};
GRAYCOLOR darkFrameAccent = {0.0, 0.0625};

/* Focus rings */
RGBACOLOR darkFocusRing[4] = RGBA256(38.0, 113.0, 159.0, 1.0);
RGBACOLOR darkFocusRingTop[4] = RGBA256(50.0, 124.0, 171.0, 1.0);
RGBACOLOR darkFocusRingBottom[4] = RGBA256(57.0, 130.0, 176.0, 1.0);

#define GRAD256(r0, g0, b0, a0, r1, g1, b1, a1) { \
	r0 / 255, g0 / 255, b0 / 255, a0, \
	r1 / 255, g1 / 255, b1 / 255, a1 };

RGBACOLOR darkTopGradient[8] = {1.0, 1.0, 1.0, 0.3, \
				     1.0, 1.0, 1.0, 0.0};
RGBACOLOR darkBackgroundGradient[8] = {0.0, 0.0, 0.0, 0.1, \
					    0.0, 0.0, 0.0, 0.25};
RGBACOLOR darkInactiveGradient[8] = GRAD256(89.0, 90.0, 93.0, 1.0, \
						 119.0, 120.0, 122.0, 1.0);
RGBACOLOR darkSelectedGradient[8] = GRAD256(23.0, 111.0, 232.0, 1.0, \
						  20.0, 94.0,  206.0, 1.0);
RGBACOLOR pressedPushButtonGradient[8] = GRAD256(35.0, 123.0, 244.0, 1.0, \
						       30.0, 114.0, 235.0, 1.0);

/*
 * When building on systems earlier than 10.8 there is no reasonable way to
 * convert an NSColor to a CGColor.  We do run-time checking of the OS version,
 * and never need the CGColor property on older systems, so we can just define
 * a macro CGColorFromRGBA which evaluates to NULL without raising compiler
 * warnings.  Similarly, we never draw rounded rectangles on older systems
 * which did not have CGPathCreateWithRoundedRect, so we just redefine it to
 * return NULL.
 */

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1080
static CGColorRef
CGColorFromRGBA(
    CGFloat *rgba)
{
    NSColorSpace *colorSpace = [NSColorSpace deviceRGBColorSpace];
    NSColor *nscolor = [NSColor colorWithColorSpace: colorSpace
					 components: rgba
					      count: 4];
    return nscolor.CGColor;
}

static CGColorRef
CGColorFromGray(
    GrayColor g)
{
    NSColor *nscolor = [NSColor colorWithCalibratedWhite: g.grayscale
						   alpha: g.alpha];
    return nscolor.CGColor;
}

/*
 * Returns a piecwise linear gradient.  Expects an array of size 4 * numColors
 */
static CGGradientRef
CGGradientFromRGBA(
    CGFloat *data,
    int numColors)
{
    NSColorSpace *deviceRGB = [NSColorSpace deviceRGBColorSpace];
    return CGGradientCreateWithColorComponents(deviceRGB.CGColorSpace,
					       data, NULL, numColors);
}

#define CGCOLOR(nscolor) nscolor.CGColor
#else
#define CGCOLOR nil
#define CGColorFromRGBA(rgba) NULL
#define CGColorFromGray(gray) NULL
#define CGPathCreateWithRoundedRect(w, x, y, z) NULL
#endif


/*
 * If we try to draw a rounded rectangle with too large of a radius
 * CoreGraphics will raise a fatal exception.  This macro returns if
 * the width or height is less than twice the radius.  Presumably this
 * only happens when a widget has not yet been configured and has size
 * 1x1.
 */

#define CHECK_RADIUS(radius, bounds)                                         \
    if (radius > bounds.size.width / 2 || radius > bounds.size.height / 2) { \
        return;                                                              \
    }

/*----------------------------------------------------------------------
 * +++ Utilities.
 */

/*
 * BoxToRect --
 *
 *    Convert a Ttk_Box in Tk coordinates relative to the given Drawable to a
 *    native CGRect relative to the containing NSView.  (The coordinate system
 *    is the one used by CGContextRef, which has origin at the upper left
 *    corner, and y increasing downward.)
 */

static inline CGRect BoxToRect(
    Drawable d,
    Ttk_Box b)
{
    MacDrawable *md = (MacDrawable *) d;
    CGRect rect;

    rect.origin.y       = b.y + md->yOff;
    rect.origin.x       = b.x + md->xOff;
    rect.size.height    = b.height;
    rect.size.width     = b.width;

    return rect;
}

/*
 * Table mapping Tk states to Appearance manager ThemeStates
 */

static Ttk_StateTable ThemeStateTable[] = {
    {kThemeStateActive, TTK_STATE_ALTERNATE | TTK_STATE_BACKGROUND},
    {kThemeStateUnavailable, TTK_STATE_DISABLED, 0},
    {kThemeStatePressed, TTK_STATE_PRESSED, 0},
    {kThemeStateInactive, TTK_STATE_BACKGROUND, 0},
    {kThemeStateActive, 0, 0}

    /* Others: Not sure what these are supposed to mean.  Up/Down have
     * something to do with "little arrow" increment controls...  Dunno what
     * a "Rollover" is.
     * NEM: Rollover is TTK_STATE_ACTIVE... but we don't handle that yet, by
     * the looks of things
     *
     * {kThemeStateRollover, 0, 0},
     * {kThemeStateUnavailableInactive, 0, 0}
     * {kThemeStatePressedUp, 0, 0},
     * {kThemeStatePressedDown, 0, 0}
     */
};

/*----------------------------------------------------------------------
 * NormalizeButtonBounds --
 *
 *      Apple only allows three specific heights for most buttons: regular,
 *      small and mini. We always use the regular size.  However, Ttk may
 *      provide a bounding rectangle with arbitrary height.  We draw the Mac
 *      button centered vertically in the Ttk rectangle, with the same width as
 *      the rectangle.  This function returns the actual bounding rectangle
 *      that will be used in drawing the button.
 */

static CGRect NormalizeButtonBounds(
    SInt32 heightMetric,
    CGRect bounds)
{
    SInt32 height;

    if (heightMetric != (SInt32) NoThemeMetric) {
	ChkErr(GetThemeMetric, heightMetric, &height);
	bounds.origin.y += (bounds.size.height - height) / 2;
	bounds.size.height = height;
    }
    return bounds;
}

/*----------------------------------------------------------------------
 * +++ Backgrounds
 *
 * Support for contrasting background colors when GroupBoxes or Tabbed
 * panes are nested inside each other.  Early versions of macOS used ridged
 * borders, so do not need contrasting backgrounds.
 */

/*
 * For systems older than 10.14, [NSColor windowBackGroundColor] generates
 * garbage when called from this function.  In 10.14 it works correctly, and
 * must be used in order to have a background color which responds to Dark
 * Mode.  So we use this hard-wired RGBA color on the older systems which don't
 * support Dark Mode anyway.
 */

RGBACOLOR windowBackground[4] = RGBA256(235.0, 235.0, 235.0, 1.0);
RGBACOLOR whiteRGBA[4] = {1.0, 1.0, 1.0, 1.0};
RGBACOLOR blackRGBA[4] = {0.0, 0.0, 0.0, 1.0};

/*----------------------------------------------------------------------
 * GetBackgroundColor --
 *
 *      Fills the array rgba with the color coordinates for a background color.
 *      Start with the background color of a window's geometry master, or the
 *      standard ttk window background if there is no master. If the contrast
 *      parameter is nonzero, modify this color to be darker, for the aqua
 *      appearance, or lighter for the DarkAqua appearance.  This is primarily
 *      used by the Fill and Background elements.
 */

static void GetBackgroundColorRGBA(
    CGContextRef context,
    Tk_Window tkwin,
    int contrast,
    CGFloat *rgba)
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    TkWindow *masterPtr = (TkWindow *) TkGetGeomMaster(tkwin);

    while (masterPtr && masterPtr->privatePtr) {
	if (masterPtr->privatePtr->flags & TTK_HAS_CONTRASTING_BG) {
	    break;
	}
	masterPtr = (TkWindow *) TkGetGeomMaster(masterPtr);
    }
    if (masterPtr && masterPtr->privatePtr) {
	for (int i = 0; i < 4; i++) {
	    rgba[i] = masterPtr->privatePtr->fillRGBA[i];
	}
    } else {
	if ([NSApp macMinorVersion] > 13) {
	    NSColorSpace *deviceRGB = [NSColorSpace deviceRGBColorSpace];
	    NSColor *windowColor = [[NSColor windowBackgroundColor]
		colorUsingColorSpace: deviceRGB];
	    [windowColor getComponents: rgba];
	} else {
	    for (int i = 0; i < 4; i++) {
		rgba[i] = windowBackground[i];
	    }
	}
    }
    if (contrast) {
	int isDark = (rgba[0] + rgba[1] + rgba[2] < 1.5);

	if (isDark) {
	    for (int i = 0; i < 3; i++) {
		rgba[i] += 8.0 / 255.0;
	    }
	} else {
	    for (int i = 0; i < 3; i++) {
		rgba[i] -= 8.0 / 255.0;
	    }
	}
        if (winPtr->privatePtr) {
            winPtr->privatePtr->flags |= TTK_HAS_CONTRASTING_BG;
            for (int i = 0; i < 4; i++) {
                winPtr->privatePtr->fillRGBA[i] = rgba[i];
            }
        }
    }
}

static CGColorRef GetBackgroundCGColor(
    CGContextRef context,
    Tk_Window tkwin,
    int contrast)
{
    CGFloat rgba[4];
    GetBackgroundColorRGBA(context, tkwin, contrast, rgba);
    return CGColorFromRGBA(rgba);
}

/*----------------------------------------------------------------------
 * +++ Single Arrow Buttons --
 *
 * The chevrons used in ListHeaders, Comboboxes and Disclosure Buttons.
 */

static void DrawDownArrow(
    CGContextRef context,
    CGRect bounds,
    CGFloat inset,
    CGFloat size,
    CGFloat *rgba)
{
    CGFloat x, y;

    CGContextSetRGBStrokeColor(context, rgba[0], rgba[1], rgba[2], rgba[3]);
    CGContextSetLineWidth(context, 1.5);
    x = bounds.origin.x + inset;
    y = bounds.origin.y + trunc(bounds.size.height / 2);
    CGContextBeginPath(context);
    CGPoint arrow[3] = {
	{x, y - size / 4}, {x + size / 2, y + size / 4},
	{x + size, y - size / 4}
    };
    CGContextAddLines(context, arrow, 3);
    CGContextStrokePath(context);
}

static void DrawUpArrow(
    CGContextRef context,
    CGRect bounds,
    CGFloat inset,
    CGFloat size,
    CGFloat *rgba)
{
    CGFloat x, y;

    CGContextSetRGBStrokeColor(context, rgba[0], rgba[1], rgba[2], rgba[3]);
    CGContextSetLineWidth(context, 1.5);
    x = bounds.origin.x + inset;
    y = bounds.origin.y + trunc(bounds.size.height / 2);
    CGContextBeginPath(context);
    CGPoint arrow[3] = {
	{x, y + size / 4}, {x + size / 2, y - size / 4},
	{x + size, y + size / 4}
    };
    CGContextAddLines(context, arrow, 3);
    CGContextStrokePath(context);
}

/*----------------------------------------------------------------------
 * +++ Double Arrow Buttons --
 *
 * Draws two chevrons, as used in MenuButtons and SpinButtons.
 */

static void DrawUpDownArrows(
    CGContextRef context,
    CGRect bounds,
    CGFloat inset,
    CGFloat size,
    CGFloat *rgba)
{
    CGFloat x, y;

    CGContextSetRGBStrokeColor(context, rgba[0], rgba[1], rgba[2], rgba[3]);
    CGContextSetLineWidth(context, 1.5);
    x = bounds.origin.x + inset;
    y = bounds.origin.y + trunc(bounds.size.height / 2);
    CGContextBeginPath(context);
    CGPoint bottomArrow[3] =
    {{x, y + 2}, {x + size / 2, y + 2 + size / 2}, {x + size, y + 2}};
    CGContextAddLines(context, bottomArrow, 3);
    CGPoint topArrow[3] =
    {{x, y - 2}, {x + size / 2, y - 2 - size / 2}, {x + size, y - 2}};
    CGContextAddLines(context, topArrow, 3);
    CGContextStrokePath(context);
}

/*----------------------------------------------------------------------
 * +++ FillButtonBackground --
 *
 *      Fills a rounded rectangle with a transparent black gradient.
 *      This is a no-op if building on 10.8 or older.
 */

static void FillButtonBackground(
    CGContextRef context,
    CGRect bounds,
    CGFloat radius)
{
    CHECK_RADIUS(radius, bounds)

    CGPathRef path;
    CGGradientRef backgroundGradient = CGGradientFromRGBA(
	darkBackgroundGradient, 2);
    CGPoint backgroundEnd = CGPointMake(bounds.origin.x,
	bounds.origin.y + bounds.size.height);
    CGContextBeginPath(context);
    path = CGPathCreateWithRoundedRect(bounds, radius, radius, NULL);
    CGContextAddPath(context, path);
    CGContextClip(context);
    CGContextDrawLinearGradient(context, backgroundGradient,
	bounds.origin, backgroundEnd, 0);
    CFRelease(path);
    CFRelease(backgroundGradient);
}

/*----------------------------------------------------------------------
 * +++ HighlightButtonBorder --
 *
 * Accent the top border of a rounded rectangle with a transparent
 * white gradient.
 */

static void HighlightButtonBorder(
    CGContextRef context,
    CGRect bounds)
{
    CGPoint topEnd = CGPointMake(bounds.origin.x, bounds.origin.y + 3);
    CGGradientRef topGradient = CGGradientFromRGBA(darkTopGradient, 2);

    CGContextSaveGState(context);
    CGContextBeginPath(context);
    CGContextAddArc(context, bounds.origin.x + 4, bounds.origin.y + 4,
	4, PI, 3 * PI / 2, 0);
    CGContextAddArc(context, bounds.origin.x + bounds.size.width - 4,
	bounds.origin.y + 4, 4, 3 * PI / 2, 0, 0);
    CGContextReplacePathWithStrokedPath(context);
    CGContextClip(context);
    CGContextDrawLinearGradient(context, topGradient, bounds.origin, topEnd,
	0.0);
    CGContextRestoreGState(context);
    CFRelease(topGradient);
}

/*----------------------------------------------------------------------
 * DrawGroupBox --
 *
 *      This is a standalone drawing procedure which draws the contrasting
 *      rounded rectangular box for LabelFrames and Notebook panes used in
 *      more recent versions of macOS.
 */

static void DrawGroupBox(
    CGRect bounds,
    CGContextRef context,
    Tk_Window tkwin)
{
    CHECK_RADIUS(4, bounds)

    CGPathRef path;
    CGColorRef backgroundColor, borderColor;

    backgroundColor = GetBackgroundCGColor(context, tkwin, 1);
    borderColor = CGColorFromGray(boxBorder);
    CGContextSetFillColorWithColor(context, backgroundColor);
    path = CGPathCreateWithRoundedRect(bounds, 4, 4, NULL);
    CGContextClipToRect(context, bounds);
    CGContextBeginPath(context);
    CGContextAddPath(context, path);
    CGContextFillPath(context);
    CGContextSetFillColorWithColor(context, borderColor);
    CGContextBeginPath(context);
    CGContextAddPath(context, path);
    CGContextReplacePathWithStrokedPath(context);
    CGContextFillPath(context);
    CFRelease(path);
}

/*----------------------------------------------------------------------
 * SolidFillRoundedRectangle --
 *
 *      Fill a rounded rectangle with a specified solid color.
 */

static void SolidFillRoundedRectangle(
    CGContextRef context,
    CGRect bounds,
    CGFloat radius,
    CGColorRef color)
{
    CGPathRef path;
    CHECK_RADIUS(radius, bounds)

    CGContextSetFillColorWithColor(context, color);
    path = CGPathCreateWithRoundedRect(bounds, radius, radius, NULL);
    CGContextBeginPath(context);
    CGContextAddPath(context, path);
    CGContextFillPath(context);
    CFRelease(path);
}

/*----------------------------------------------------------------------
 * +++ DrawListHeader --
 *
 *      This is a standalone drawing procedure which draws column headers for
 *      a Treeview in the Aqua appearance.  The HITheme headers have not
 *      matched the native ones since OSX 10.8.  Note that the header image is
 *      ignored, but we draw arrows according to the state.
 */

static void DrawListHeader(
    CGRect bounds,
    CGContextRef context,
    Tk_Window tkwin,
    int state)
{

    /*
     * Apple changes the background of a list header when the window is not
     * active.  But Ttk does not indicate that in the state of a TreeHeader.
     * So we have to query the Apple window manager.
     */

    NSWindow *win = TkMacOSXDrawableWindow(Tk_WindowId(tkwin));
    GrayColor bgGray = [win isKeyWindow] ?
	listheaderActiveBG : listheaderInactiveBG;
    CGColorRef strokeColor, backgroundColor = CGColorFromGray(bgGray);
    CGFloat x = bounds.origin.x, y = bounds.origin.y;
    CGFloat w = bounds.size.width, h = bounds.size.height;
    CGPoint top[2] = {{x, y + 1}, {x + w, y + 1}};
    CGPoint bottom[2] = {{x, y + h}, {x + w, y + h}};
    CGPoint separator[2] = {{x + w - 1, y + 3}, {x + w - 1, y + h - 3}};

    CGContextSaveGState(context);
    CGContextSetShouldAntialias(context, false);
    CGContextBeginPath(context);
    CGContextSetFillColorWithColor(context, backgroundColor);
    CGContextAddRect(context, bounds);
    CGContextFillPath(context);
    strokeColor = CGColorFromGray(listheaderSeparator);
    CGContextSetStrokeColorWithColor(context, strokeColor);
    CGContextAddLines(context, separator, 2);
    CGContextStrokePath(context);
    strokeColor = CGColorFromGray(listheaderBorder);
    CGContextSetStrokeColorWithColor(context, strokeColor);
    CGContextAddLines(context, top, 2);
    CGContextStrokePath(context);
    CGContextAddLines(context, bottom, 2);
    CGContextStrokePath(context);
    CGContextRestoreGState(context);

    if (state & TTK_TREEVIEW_STATE_SORTARROW) {
	CGRect arrowBounds = bounds;
	arrowBounds.origin.x = bounds.origin.x + bounds.size.width - 16;
	arrowBounds.size.width = 16;
	if (state & TTK_STATE_ALTERNATE) {
	    DrawUpArrow(context, arrowBounds, 3, 8, blackRGBA);
	} else if (state & TTK_STATE_SELECTED) {
	    DrawDownArrow(context, arrowBounds, 3, 8, blackRGBA);
	}
    }
}

/*----------------------------------------------------------------------
 * +++ Drawing procedures for widgets in Apple's "Dark Mode" (10.14 and up).
 *
 *      The HIToolbox does not support Dark Mode, and apparently never will,
 *      so to make widgets look "native" we have to provide analogues of the
 *      HITheme drawing functions to be used in DarkAqua.  We continue to use
 *      HITheme in Aqua, since it understands earlier versions of the OS.
 *
 *      Drawing the dark widgets requires NSColors that were introduced in OSX
 *      10.14, so we make some of these functions be no-ops when building on
 *      systems older than 10.14.
 */

/*----------------------------------------------------------------------
 * GradientFillRoundedRectangle --
 *
 *      Fill a rounded rectangle with a specified gradient.
 */

static void GradientFillRoundedRectangle(
    CGContextRef context,
    CGRect bounds,
    CGFloat radius,
    CGFloat *colors,
    int numColors)
{
    CHECK_RADIUS(radius, bounds)

    CGPathRef path;
    CGPoint end = {
	bounds.origin.x,
	bounds.origin.y + bounds.size.height
    };
    CGGradientRef gradient = CGGradientFromRGBA(colors, numColors);

    path = CGPathCreateWithRoundedRect(bounds, radius, radius, NULL);
    CGContextBeginPath(context);
    CGContextAddPath(context, path);
    CGContextClip(context);
    CGContextDrawLinearGradient(context, gradient, bounds.origin, end, 0);
    CFRelease(path);
    CFRelease(gradient);
}

/*----------------------------------------------------------------------
 * +++ DrawDarkButton --
 *
 *      This is a standalone drawing procedure which draws PushButtons and
 *      PopupButtons in the Dark Mode style.
 */

static void DrawDarkButton(
    CGRect bounds,
    ThemeButtonKind kind,
    Ttk_State state,
    CGContextRef context)
{
    CGColorRef faceColor;

    /*
     * To match the appearance of Apple's buttons we need to increase the
     * height by 1 pixel.
     */

    bounds.size.height += 1;

    CGContextClipToRect(context, bounds);
    FillButtonBackground(context, bounds, 5);

    /*
     * Fill the button face with the appropriate color.
     */

    bounds = CGRectInset(bounds, 1, 1);
    if (kind == kThemePushButton && (state & TTK_STATE_PRESSED)) {
	GradientFillRoundedRectangle(context, bounds, 4,
	    pressedPushButtonGradient, 2);
    } else if (kind == kThemePushButton &&
	       (state & TTK_STATE_ALTERNATE) &&
	       !(state & TTK_STATE_BACKGROUND)) {
	GradientFillRoundedRectangle(context, bounds, 4,
	    darkSelectedGradient, 2);
    } else {
	if (state & TTK_STATE_DISABLED) {
	    faceColor = CGColorFromGray(darkDisabledButtonFace);
	} else {
	    faceColor = CGColorFromGray(darkButtonFace);
	}
	SolidFillRoundedRectangle(context, bounds, 4, faceColor);
    }

    /*
     * If this is a popup, draw the arrow button.
     */

    if ((kind == kThemePopupButton) | (kind == kThemeComboBox)) {
	CGRect arrowBounds = bounds;
	arrowBounds.size.width = 16;
	arrowBounds.origin.x += bounds.size.width - 16;

        /*
         * If the toplevel is front, paint the button blue.
         */

	if (!(state & TTK_STATE_BACKGROUND) &&
	    !(state & TTK_STATE_DISABLED)) {
	    GradientFillRoundedRectangle(context, arrowBounds, 4,
		darkSelectedGradient, 2);
	}
	if (kind == kThemePopupButton) {
	    DrawUpDownArrows(context, arrowBounds, 3, 7, whiteRGBA);
	} else {
	    DrawDownArrow(context, arrowBounds, 4, 8, whiteRGBA);
	}
    }

    HighlightButtonBorder(context, bounds);
}

/*----------------------------------------------------------------------
 * +++ DrawDarkIncDecButton --
 *
 *      This is a standalone drawing procedure which draws an IncDecButton
 *      (as used in a Spinbox) in the Dark Mode style.
 */

static void DrawDarkIncDecButton(
    CGRect bounds,
    ThemeDrawState drawState,
    Ttk_State state,
    CGContextRef context)
{
    CGColorRef faceColor;

    bounds = CGRectInset(bounds, 0, -1);
    CGContextClipToRect(context, bounds);
    FillButtonBackground(context, bounds, 6);

    /*
     * Fill the button face with the appropriate color.
     */

    bounds = CGRectInset(bounds, 1, 1);
    if (state & TTK_STATE_DISABLED) {
	faceColor = CGColorFromGray(darkDisabledButtonFace);
    } else {
	faceColor = CGColorFromGray(darkButtonFace);
    }
    SolidFillRoundedRectangle(context, bounds, 4, faceColor);

    /*
     * If pressed, paint the appropriate half blue.
     */

    if (state & TTK_STATE_PRESSED) {
	CGRect clip = bounds;
	clip.size.height /= 2;
	CGContextSaveGState(context);
	if (drawState == kThemeStatePressedDown) {
	    clip.origin.y += clip.size.height;
	}
	CGContextClipToRect(context, clip);
	GradientFillRoundedRectangle(context, bounds, 5,
	    darkSelectedGradient, 2);
	CGContextRestoreGState(context);
    }
    DrawUpDownArrows(context, bounds, 3, 5, whiteRGBA);
    HighlightButtonBorder(context, bounds);
}

static void DrawDarkArrowButton(
    CGRect bounds,
    ThemeDrawState drawState,
    Ttk_State state,
    CGContextRef context)
{
    CGColorRef faceColor;
    NSRect arrowBounds;

    bounds.origin.x -= 1;
    bounds.size.width += 1;
    CGContextClipToRect(context, bounds);
    FillButtonBackground(context, bounds, 6);

    /*
     * Fill the button face with the appropriate color.
     */

    bounds = CGRectInset(bounds, 1, 1);
    if (state & TTK_STATE_DISABLED) {
	faceColor = CGColorFromGray(darkDisabledButtonFace);
    } else if (state & TTK_STATE_PRESSED) {
	faceColor = CGColorFromGray(darkPressedButtonFace);
    } else {
	faceColor = CGColorFromGray(darkButtonFace);
    }
    SolidFillRoundedRectangle(context, bounds, 4, faceColor);

    /*
     * If pressed, paint the appropriate half blue.
     */
    arrowBounds.origin.x = bounds.origin.x + bounds.size.width - 17;
    arrowBounds.size.width = 16;
    if (state & TTK_STATE_SELECTED) {
	DrawUpArrow(context, arrowBounds, 3, 8, whiteRGBA);
    } else {
	DrawDownArrow(context, arrowBounds, 3, 8, whiteRGBA);
    }
    HighlightButtonBorder(context, bounds);
}

/*----------------------------------------------------------------------
 * +++ DrawDarkBevelButton --
 *
 *      This is a standalone drawing procedure which draws RoundedBevelButtons
 *      in the Dark Mode style.
 */

static void DrawDarkBevelButton(
    CGRect bounds,
    Ttk_State state,
    CGContextRef context)
{
    CGColorRef faceColor;

    CGContextClipToRect(context, bounds);
    FillButtonBackground(context, bounds, 5);

    /*
     * Fill the button face with the appropriate color.
     */

    bounds = CGRectInset(bounds, 1, 1);
    if (state & TTK_STATE_PRESSED) {
	faceColor = CGColorFromGray(darkPressedButtonFace);
    } else if ((state & TTK_STATE_DISABLED) ||
	(state & TTK_STATE_ALTERNATE)) {
	faceColor = CGColorFromGray(darkDisabledButtonFace);
    } else if (state & TTK_STATE_SELECTED) {
	faceColor = CGColorFromGray(darkSelectedButtonFace);
    } else {
	faceColor = CGColorFromGray(darkButtonFace);
    }
    SolidFillRoundedRectangle(context, bounds, 4, faceColor);
    HighlightButtonBorder(context, bounds);
}

/*----------------------------------------------------------------------
 * +++ DrawDarkCheckBox --
 *
 *      This is a standalone drawing procedure which draws Checkboxes in the
 *      Dark Mode style.
 */

static void DrawDarkCheckBox(
    CGRect bounds,
    Ttk_State state,
    CGContextRef context)
{
    CGRect checkbounds = {{0, bounds.size.height / 2 - 8}, {16, 16}};
    NSColorSpace *deviceRGB = [NSColorSpace deviceRGBColorSpace];
    NSColor *stroke;
    CGFloat x, y;

    bounds = CGRectOffset(checkbounds, bounds.origin.x, bounds.origin.y);
    x = bounds.origin.x;
    y = bounds.origin.y;

    CGContextClipToRect(context, bounds);
    FillButtonBackground(context, bounds, 4);
    bounds = CGRectInset(bounds, 1, 1);
    if (!(state & TTK_STATE_BACKGROUND) &&
	!(state & TTK_STATE_DISABLED) &&
	((state & TTK_STATE_SELECTED) || (state & TTK_STATE_ALTERNATE))) {
	GradientFillRoundedRectangle(context, bounds, 3,
	    darkSelectedGradient, 2);
    } else {
	GradientFillRoundedRectangle(context, bounds, 3,
	    darkInactiveGradient, 2);
    }
    HighlightButtonBorder(context, bounds);
    if ((state & TTK_STATE_SELECTED) || (state & TTK_STATE_ALTERNATE)) {
	CGContextSetStrokeColorSpace(context, deviceRGB.CGColorSpace);
	if (state & TTK_STATE_DISABLED) {
	    stroke = [NSColor disabledControlTextColor];
	} else {
	    stroke = [NSColor controlTextColor];
	}
	CGContextSetStrokeColorWithColor(context, CGCOLOR(stroke));
    }
    if (state & TTK_STATE_SELECTED) {
	CGContextSetLineWidth(context, 1.5);
	CGContextBeginPath(context);
	CGPoint check[3] = {{x + 4, y + 8}, {x + 7, y + 11}, {x + 11, y + 4}};
	CGContextAddLines(context, check, 3);
	CGContextStrokePath(context);
    } else if (state & TTK_STATE_ALTERNATE) {
	CGContextSetLineWidth(context, 2.0);
	CGContextBeginPath(context);
	CGPoint bar[2] = {{x + 4, y + 8}, {x + 12, y + 8}};
	CGContextAddLines(context, bar, 2);
	CGContextStrokePath(context);
    }
}

/*----------------------------------------------------------------------
 * +++ DrawDarkRadioButton --
 *
 *    This is a standalone drawing procedure which draws RadioButtons
 *    in the Dark Mode style.
 */

static void DrawDarkRadioButton(
    CGRect bounds,
    Ttk_State state,
    CGContextRef context)
{
    CGRect checkbounds = {{0, bounds.size.height / 2 - 9}, {18, 18}};
    NSColorSpace *deviceRGB = [NSColorSpace deviceRGBColorSpace];
    NSColor *fill;
    CGFloat x, y;

    bounds = CGRectOffset(checkbounds, bounds.origin.x, bounds.origin.y);
    x = bounds.origin.x;
    y = bounds.origin.y;

    CGContextClipToRect(context, bounds);
    FillButtonBackground(context, bounds, 9);
    bounds = CGRectInset(bounds, 1, 1);
    if (!(state & TTK_STATE_BACKGROUND) &&
	!(state & TTK_STATE_DISABLED) &&
	((state & TTK_STATE_SELECTED) || (state & TTK_STATE_ALTERNATE))) {
	GradientFillRoundedRectangle(context, bounds, 8,
	    darkSelectedGradient, 2);
    } else {
	GradientFillRoundedRectangle(context, bounds, 8,
	    darkInactiveGradient, 2);
    }
    HighlightButtonBorder(context, bounds);
    if ((state & TTK_STATE_SELECTED) || (state & TTK_STATE_ALTERNATE)) {
	CGContextSetStrokeColorSpace(context, deviceRGB.CGColorSpace);
	if (state & TTK_STATE_DISABLED) {
	    fill = [NSColor disabledControlTextColor];
	} else {
	    fill = [NSColor controlTextColor];
	}
	CGContextSetFillColorWithColor(context, CGCOLOR(fill));
    }
    if (state & TTK_STATE_SELECTED) {
	CGContextBeginPath(context);
	CGRect dot = {{x + 6, y + 6}, {6, 6}};
	CGContextAddEllipseInRect(context, dot);
	CGContextFillPath(context);
    } else if (state & TTK_STATE_ALTERNATE) {
	CGRect bar = {{x + 5, y + 8}, {8, 2}};
	CGContextFillRect(context, bar);
    }
}

/*----------------------------------------------------------------------
 * +++ DrawDarkTab --
 *
 *      This is a standalone drawing procedure which draws Tabbed Pane
 *      Tabs in the Dark Mode style.
 */

static void DrawDarkTab(
    CGRect bounds,
    Ttk_State state,
    CGContextRef context)
{
    CGColorRef faceColor, strokeColor;
    CGRect originalBounds = bounds;

    CGContextSetLineWidth(context, 1.0);
    CGContextClipToRect(context, bounds);

    /*
     * Extend the bounds to one or both sides so the rounded part will be
     * clipped off.
     */

    if (!(state & TTK_STATE_FIRST_TAB)) {
	bounds.origin.x -= 10;
	bounds.size.width += 10;
    }

    if (!(state & TTK_STATE_LAST_TAB)) {
	bounds.size.width += 10;
    }

    /*
     * Fill the tab face with the appropriate color or gradient.  Use a solid
     * color if the tab is not selected, otherwise use a blue or gray
     * gradient.
     */

    bounds = CGRectInset(bounds, 1, 1);
    if (!(state & TTK_STATE_SELECTED)) {
	if (state & TTK_STATE_DISABLED) {
	    faceColor = CGColorFromGray(darkDisabledButtonFace);
	} else {
	    faceColor = CGColorFromGray(darkButtonFace);
	}
	SolidFillRoundedRectangle(context, bounds, 4, faceColor);

        /*
         * Draw a separator line on the left side of the tab if it
         * not first.
         */

	if (!(state & TTK_STATE_FIRST_TAB)) {
	    CGContextSaveGState(context);
	    CGContextSetShouldAntialias(context, false);
	    strokeColor = CGColorFromGray(darkTabSeparator);
	    CGContextSetStrokeColorWithColor(context, strokeColor);
	    CGContextBeginPath(context);
	    CGContextMoveToPoint(context, originalBounds.origin.x,
		originalBounds.origin.y + 1);
	    CGContextAddLineToPoint(context, originalBounds.origin.x,
		originalBounds.origin.y + originalBounds.size.height - 1);
	    CGContextStrokePath(context);
	    CGContextRestoreGState(context);
	}
    } else {

        /*
         * This is the selected tab; paint it blue.  If it is first, cover up
         * the separator line drawn by the second one.  (The selected tab is
         * always drawn last.)
         */

	if ((state & TTK_STATE_FIRST_TAB) && !(state & TTK_STATE_LAST_TAB)) {
	    bounds.size.width += 1;
	}
	if (!(state & TTK_STATE_BACKGROUND)) {
	    GradientFillRoundedRectangle(context, bounds, 4,
		darkSelectedGradient, 2);
	} else {
	    faceColor = CGColorFromGray(darkInactiveSelectedTab);
	    SolidFillRoundedRectangle(context, bounds, 4, faceColor);
	}
	HighlightButtonBorder(context, bounds);
    }
}

/*----------------------------------------------------------------------
 * +++ DrawDarkSeparator --
 *
 *      This is a standalone drawing procedure which draws a separator widget
 *      in Dark Mode.
 */

static void DrawDarkSeparator(
    CGRect bounds,
    CGContextRef context,
    Tk_Window tkwin)
{
    CGColorRef sepColor = CGColorFromGray(darkSeparator);

    CGContextSetFillColorWithColor(context, sepColor);
    CGContextFillRect(context, bounds);
}

/*----------------------------------------------------------------------
 * +++ DrawDarkFocusRing --
 *
 *      This is a standalone drawing procedure which draws a focus ring around
 *      an Entry widget in Dark Mode.
 */

static void DrawDarkFocusRing(
    CGRect bounds,
    CGContextRef context)
{
    CGRect insetBounds = CGRectInset(bounds, -3, -3);
    CHECK_RADIUS(4, insetBounds)

	CGColorRef strokeColor, fillColor = CGColorFromRGBA(darkFocusRing);
    CGFloat x = bounds.origin.x, y = bounds.origin.y;
    CGFloat w = bounds.size.width, h = bounds.size.height;
    CGPoint topPart[4] = {
	{x, y + h}, {x, y + 1}, {x + w - 1, y + 1}, {x + w - 1, y + h}
    };
    CGPoint bottom[2] = {{x, y + h}, {x + w, y + h}};

    CGContextSaveGState(context);
    CGContextSetShouldAntialias(context, false);
    CGContextBeginPath(context);
    strokeColor = CGColorFromRGBA(darkFocusRingTop);
    CGContextSetStrokeColorWithColor(context, strokeColor);
    CGContextAddLines(context, topPart, 4);
    CGContextStrokePath(context);
    strokeColor = CGColorFromRGBA(darkFocusRingBottom);
    CGContextSetStrokeColorWithColor(context, strokeColor);
    CGContextAddLines(context, bottom, 2);
    CGContextStrokePath(context);
    CGContextSetShouldAntialias(context, true);
    CGContextSetFillColorWithColor(context, fillColor);
    CGPathRef path = CGPathCreateWithRoundedRect(insetBounds, 4, 4, NULL);
    CGContextBeginPath(context);
    CGContextAddPath(context, path);
    CGContextAddRect(context, bounds);
    CGContextEOFillPath(context);
    CGContextRestoreGState(context);
}
/*----------------------------------------------------------------------
 * +++ DrawDarkFrame --
 *
 *      This is a standalone drawing procedure which draws various
 *      types of borders in Dark Mode.
 */

static void DrawDarkFrame(
    CGRect bounds,
    CGContextRef context,
    HIThemeFrameKind kind)
{
    CGColorRef stroke;
    CGFloat x = bounds.origin.x, y = bounds.origin.y;
    CGFloat w = bounds.size.width, h = bounds.size.height;
    CGPoint topPart[4] = {
	{x, y + h - 1}, {x, y + 1}, {x + w, y + 1}, {x + w, y + h - 1}
    };
    CGPoint bottom[2] = {{x, y + h}, {x + w, y + h}};
    CGPoint accent[2] = {{x, y + 1}, {x + w, y + 1}};

    switch (kind) {
    case kHIThemeFrameTextFieldSquare:
	CGContextSaveGState(context);
	CGContextSetShouldAntialias(context, false);
	CGContextBeginPath(context);
	stroke = CGColorFromGray(darkFrameTop);
	CGContextSetStrokeColorWithColor(context, stroke);
	CGContextAddLines(context, topPart, 4);
	CGContextStrokePath(context);
	stroke = CGColorFromGray(darkFrameBottom);
	CGContextSetStrokeColorWithColor(context, stroke);
	CGContextAddLines(context, bottom, 2);
	CGContextStrokePath(context);
	stroke = CGColorFromGray(darkFrameAccent);
	CGContextSetStrokeColorWithColor(context, stroke);
	CGContextAddLines(context, accent, 2);
	CGContextStrokePath(context);
	CGContextRestoreGState(context);
	break;
    default:
	break;
    }
}

/*----------------------------------------------------------------------
 * +++ DrawDarkListHeader --
 *
 *      This is a standalone drawing procedure which draws column
 *      headers for a Treeview in the Dark Mode.
 */

static void DrawDarkListHeader(
    CGRect bounds,
    CGContextRef context,
    Tk_Window tkwin,
    int state)
{
    CGColorRef stroke;
    CGFloat x = bounds.origin.x, y = bounds.origin.y;
    CGFloat w = bounds.size.width, h = bounds.size.height;
    CGPoint top[2] = {{x, y}, {x + w, y}};
    CGPoint bottom[2] = {{x, y + h}, {x + w, y + h}};
    CGPoint separator[2] = {{x + w, y + 3}, {x + w, y + h - 3}};

    CGContextSaveGState(context);
    CGContextSetShouldAntialias(context, false);
    stroke = CGColorFromGray(darkFrameBottom);
    CGContextSetStrokeColorWithColor(context, stroke);
    CGContextBeginPath(context);
    CGContextAddLines(context, top, 2);
    CGContextStrokePath(context);
    CGContextAddLines(context, bottom, 2);
    CGContextStrokePath(context);
    CGContextAddLines(context, separator, 2);
    CGContextStrokePath(context);
    CGContextRestoreGState(context);

    if (state & TTK_TREEVIEW_STATE_SORTARROW) {
	CGRect arrowBounds = bounds;

	arrowBounds.origin.x = bounds.origin.x + bounds.size.width - 16;
	arrowBounds.size.width = 16;
	if (state & TTK_STATE_ALTERNATE) {
	    DrawUpArrow(context, arrowBounds, 3, 8, whiteRGBA);
	} else if (state & TTK_STATE_SELECTED) {
	    DrawDownArrow(context, arrowBounds, 3, 8, whiteRGBA);
	}
    }
}

/*----------------------------------------------------------------------
 * +++ DrawGradientButton --
 *
 *      This is a standalone drawing procedure which draws a
 *      a Gradient Button.
 */

static void DrawGradientBorder(
    CGRect bounds,
    CGContextRef context,
    Tk_Window tkwin,
    Ttk_State state)
{
    CGColorRef faceColor, borderColor;
    GrayColor faceGray, borderGray;
    CGRect inside = CGRectInset(bounds, 1, 1);

    if (TkMacOSXInDarkMode(tkwin)) {
	faceGray = state & TTK_STATE_PRESSED ?
	    darkGradientPressed : darkGradientNormal;
	borderGray = darkGradientBorder;
    } else {
	faceGray = state & TTK_STATE_PRESSED ?
	    lightGradientPressed : lightGradientNormal;
	borderGray = lightGradientBorder;
    }
    faceColor = CGColorFromGray(faceGray);
    borderColor = CGColorFromGray(borderGray);
    CGContextSetFillColorWithColor(context, faceColor);
    CGContextFillRect(context, inside);
    CGContextSetFillColorWithColor(context, borderColor);
    CGContextAddRect(context, bounds);
    CGContextAddRect(context, inside);
    CGContextEOFillPath(context);
}

/*----------------------------------------------------------------------
 * +++ Button element: Used for elements drawn with DrawThemeButton.
 */

/*
 * When Ttk draws the various types of buttons, a pointer to one of these
 * is passed as the clientData.
 */

#define TkGradientButton 0x8001

typedef struct {
    ThemeButtonKind kind;
    ThemeMetric heightMetric;
    ThemeMetric widthMetric;
} ThemeButtonParams;
static ThemeButtonParams
    PushButtonParams =  {kThemePushButton, kThemeMetricPushButtonHeight, NoThemeMetric},
    CheckBoxParams =    {kThemeCheckBox, kThemeMetricCheckBoxHeight, NoThemeMetric},
    RadioButtonParams = {kThemeRadioButton, kThemeMetricRadioButtonHeight, NoThemeMetric},
    BevelButtonParams = {kThemeRoundedBevelButton, NoThemeMetric, NoThemeMetric},
    PopupButtonParams = {kThemePopupButton, kThemeMetricPopupButtonHeight, NoThemeMetric},
    DisclosureParams =  {kThemeDisclosureButton, kThemeMetricDisclosureTriangleHeight,
			 kThemeMetricDisclosureTriangleWidth},
    DisclosureButtonParams = {kThemeArrowButton, kThemeMetricSmallDisclosureButtonHeight,
			      kThemeMetricSmallDisclosureButtonWidth},
    HelpButtonParams = {kThemeRoundButtonHelp, kThemeMetricRoundButtonSize,
			kThemeMetricRoundButtonSize},
    ListHeaderParams = {kThemeListHeaderButton, kThemeMetricListHeaderHeight, NoThemeMetric},
    GradientButtonParams = {TkGradientButton, NoThemeMetric, NoThemeMetric};

static Ttk_StateTable ButtonValueTable[] = {
    {kThemeButtonOff, TTK_STATE_ALTERNATE | TTK_STATE_BACKGROUND, 0},
    {kThemeButtonMixed, TTK_STATE_ALTERNATE, 0},
    {kThemeButtonOn, TTK_STATE_SELECTED, 0},
    {kThemeButtonOff, 0, 0}
};

    /*
     * Others: kThemeDisclosureRight, kThemeDisclosureDown,
     * kThemeDisclosureLeft
     */

static Ttk_StateTable ButtonAdornmentTable[] = {
    {kThemeAdornmentNone, TTK_STATE_ALTERNATE | TTK_STATE_BACKGROUND, 0},
    {kThemeAdornmentDefault | kThemeAdornmentFocus,
     TTK_STATE_ALTERNATE | TTK_STATE_FOCUS, 0},
    {kThemeAdornmentFocus, TTK_STATE_FOCUS, 0},
    {kThemeAdornmentDefault, TTK_STATE_ALTERNATE, 0},
    {kThemeAdornmentNone, 0, 0}
};

/*----------------------------------------------------------------------
 * +++ computeButtonDrawInfo --
 *
 *      Fill in an appearance manager HIThemeButtonDrawInfo record.
 */

static inline HIThemeButtonDrawInfo computeButtonDrawInfo(
    ThemeButtonParams *params,
    Ttk_State state,
    Tk_Window tkwin)
{
    /*
     * See ButtonElementDraw for the explanation of why we always draw
     * some buttons in the active state.
     */

    SInt32 HIThemeState;
    int adornment = 0;

    HIThemeState = Ttk_StateTableLookup(ThemeStateTable, state);

    /*
     * HITheme uses the adornment to decide the direction of the
     * arrow on a Disclosure Button.  Also HITheme draws inactive
     * (TTK_STATE_BACKGROUND) buttons in a gray color but macOS
     * no longer does that.  So we adjust the HIThemeState.
     */

    switch (params->kind) {
    case kThemeArrowButton:
	adornment = kThemeAdornmentDrawIndicatorOnly;
	if (state & TTK_STATE_SELECTED) {
	    adornment |= kThemeAdornmentArrowUpArrow;
	}
	/* Fall through. */
    case kThemeRadioButton:
	/*
	 * The gray color is better than the blue color for a
	 * background selected Radio Button.
	 */

	if (state & TTK_STATE_SELECTED) {
	    break;
	}
    default:
	if (state & TTK_STATE_BACKGROUND) {
	    HIThemeState |= kThemeStateActive;
	}
	break;
    }

    const HIThemeButtonDrawInfo info = {
	.version = 0,
	.state = HIThemeState,
	.kind = params ? params->kind : 0,
	.value = Ttk_StateTableLookup(ButtonValueTable, state),
	.adornment = Ttk_StateTableLookup(ButtonAdornmentTable, state) | adornment,
    };
    return info;
}

/*----------------------------------------------------------------------
 * +++ Button elements.
 */

static void ButtonElementMinSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    ThemeButtonParams *params = clientData;

    if (params->heightMetric != NoThemeMetric) {
	ChkErr(GetThemeMetric, params->heightMetric, minHeight);

        /*
         * The theme height does not include the 1-pixel border around
         * the button, although it does include the 1-pixel shadow at
         * the bottom.
         */

	*minHeight += 2;

        /*
         * For buttons with labels the minwidth must be 0 to force the
         * correct text layout.  For example, a non-zero value will cause the
         * text to be left justified, no matter what -anchor setting is used in
         * the style.
         */

	if (params->widthMetric != NoThemeMetric) {
	    ChkErr(GetThemeMetric, params->widthMetric, minWidth);
	    *minWidth += 2;
	    *minHeight += 2;
	} else {
	    *minWidth = 0;
	}
    }
}

static void ButtonElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    ThemeButtonParams *params = clientData;
    const HIThemeButtonDrawInfo info =
	computeButtonDrawInfo(params, 0, tkwin);
    static const CGRect scratchBounds = {{0, 0}, {100, 100}};
    CGRect contentBounds, backgroundBounds;
    int verticalPad;

    ButtonElementMinSize(clientData, elementRecord, tkwin,
	minWidth, minHeight, paddingPtr);

    switch (info.kind) {
    case TkGradientButton:
        paddingPtr->left = paddingPtr->right = 1;
        paddingPtr->top = paddingPtr->bottom = 1;
        /* Fall through. */
    case kThemeArrowButton:
    case kThemeRoundButtonHelp:
        return;
    default:
        break;
    }

    /*
     * Given a hypothetical bounding rectangle for a button, HIToolbox will
     * compute a bounding rectangle for the button contents and a bounding
     * rectangle for the button background.  The background bounds are large
     * enough to contain the image of the button in any state, which might
     * include highlight borders, shadows, etc.  The content rectangle is not
     * centered vertically within the background rectangle, presumably because
     * shadows only appear on the bottom.  Nonetheless, when HITools is asked
     * to draw a button with a certain bounding rectangle it draws the button
     * centered within the rectangle.
     *
     * To compute the effective padding around a button we request the
     * content and bounding rectangles for a 100x100 button and use the
     * padding between those.  However, we symmetrize the padding on the
     * top and bottom, because that is how the button will be drawn.
     */

    ChkErr(HIThemeGetButtonContentBounds,
	&scratchBounds, &info, &contentBounds);
    ChkErr(HIThemeGetButtonBackgroundBounds,
	&scratchBounds, &info, &backgroundBounds);
    paddingPtr->left = contentBounds.origin.x - backgroundBounds.origin.x;
    paddingPtr->right =
	CGRectGetMaxX(backgroundBounds) - CGRectGetMaxX(contentBounds);
    verticalPad = backgroundBounds.size.height - contentBounds.size.height;
    paddingPtr->top = paddingPtr->bottom = verticalPad / 2;
}

static void ButtonElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    ThemeButtonParams *params = clientData;
    CGRect bounds = BoxToRect(d, b);
    HIThemeButtonDrawInfo info = computeButtonDrawInfo(params, state, tkwin);

    switch (info.kind) {

    /*
     * A Gradient Button should have an image and no text.  The size is set to
     * that of the image.  All we need to do is draw a 1-pixel border.
     */

    case TkGradientButton:
	BEGIN_DRAWING(d)
	    DrawGradientBorder(bounds, dc.context, tkwin, state);
	END_DRAWING
	return;
    /*
     * Buttons with no height restrictions are ready to draw.
     */

    case kThemeArrowButton:
    case kThemeRoundButtonHelp:
    case kThemeCheckBox:
    case kThemeRadioButton:
    	break;

    /*
     * Other buttons have a maximum height.   We have to deal with that.
     */

    default:
	bounds = NormalizeButtonBounds(params->heightMetric, bounds);
	break;
    }

    /*
     * Tweak for PopupButtons to vertically center the text.
     */

    if (info.kind == kThemePopupButton) {
	bounds.origin.y -= 1;
    }

    BEGIN_DRAWING(d)
    if (TkMacOSXInDarkMode(tkwin)) {
	switch (info.kind) {
	case kThemePushButton:
	case kThemePopupButton:
	    DrawDarkButton(bounds, info.kind, state, dc.context);
	    break;
	case kThemeArrowButton:
	    DrawDarkArrowButton(bounds, info.kind, state, dc.context);
	    break;
	case kThemeCheckBox:
	    DrawDarkCheckBox(bounds, state, dc.context);
	    break;
	case kThemeRadioButton:
	    DrawDarkRadioButton(bounds, state, dc.context);
	    break;
	case kThemeRoundedBevelButton:
	    DrawDarkBevelButton(bounds, state, dc.context);
	    break;
	case kThemeRoundButtonHelp:
	    /* TO DO: draw a help button for Dark Mode. */
	default:
	    ChkErr(HIThemeDrawButton, &bounds, &info, dc.context,
		HIOrientation, NULL);
	}
    } else if ((info.kind == kThemePushButton) &&
	       (state & TTK_STATE_PRESSED)) {
	bounds.size.height += 2;
	GradientFillRoundedRectangle(dc.context, bounds, 4,
	    pressedPushButtonGradient, 2);
    } else {

        /*
         * Apple's PushButton and PopupButton do not change their fill color
         * when the window is inactive.  However, except in 10.7 (Lion), the
         * color of the arrow button on a PopupButton does change.  For some
         * reason HITheme fills inactive buttons with a transparent color that
         * allows the window background to show through, leading to
         * inconsistent behavior.  We work around this by filling behind an
         * inactive PopupButton with a text background color before asking
         * HIToolbox to draw it. For PushButtons, we simply draw them in the
         * active state.
         */

	if (info.kind == kThemePopupButton  &&
	    (state & TTK_STATE_BACKGROUND)) {
	    CGRect innerBounds = CGRectInset(bounds, 1, 1);
	    SolidFillRoundedRectangle(dc.context, innerBounds, 4,
				      CGColorFromRGBA(whiteRGBA));
	}

        /*
         * A BevelButton with mixed value is drawn borderless, which does make
         * much sense for us.
         */

	if (info.kind == kThemeRoundedBevelButton &&
	    info.value == kThemeButtonMixed) {
	    info.value = kThemeButtonOff;
	    info.state = kThemeStateInactive;
	}
	ChkErr(HIThemeDrawButton, &bounds, &info, dc.context, HIOrientation,
	    NULL);
    }
    END_DRAWING
}

static Ttk_ElementSpec ButtonElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    ButtonElementSize,
    ButtonElementDraw
};

/*----------------------------------------------------------------------
 * +++ Notebook elements.
 */

/* Tab position logic, c.f. ttkNotebook.c TabState() */
static Ttk_StateTable TabStyleTable[] = {
    {kThemeTabFrontInactive, TTK_STATE_SELECTED | TTK_STATE_BACKGROUND},
    {kThemeTabNonFrontInactive, TTK_STATE_BACKGROUND},
    {kThemeTabFrontUnavailable, TTK_STATE_DISABLED | TTK_STATE_SELECTED},
    {kThemeTabNonFrontUnavailable, TTK_STATE_DISABLED},
    {kThemeTabFront, TTK_STATE_SELECTED},
    {kThemeTabNonFrontPressed, TTK_STATE_PRESSED},
    {kThemeTabNonFront, 0}
};
static Ttk_StateTable TabAdornmentTable[] = {
    {kHIThemeTabAdornmentNone, TTK_STATE_FIRST_TAB | TTK_STATE_LAST_TAB},
    {kHIThemeTabAdornmentTrailingSeparator, TTK_STATE_FIRST_TAB},
    {kHIThemeTabAdornmentNone, TTK_STATE_LAST_TAB},
    {kHIThemeTabAdornmentTrailingSeparator, 0},
};
static Ttk_StateTable TabPositionTable[] = {
    {kHIThemeTabPositionOnly, TTK_STATE_FIRST_TAB | TTK_STATE_LAST_TAB},
    {kHIThemeTabPositionFirst, TTK_STATE_FIRST_TAB},
    {kHIThemeTabPositionLast, TTK_STATE_LAST_TAB},
    {kHIThemeTabPositionMiddle, 0},
};

/*
 * Apple XHIG Tab View Specifications:
 *
 * Control sizes: Tab views are available in regular, small, and mini sizes.
 * The tab height is fixed for each size, but you control the size of the pane
 * area. The tab heights for each size are listed below:
 *  - Regular size: 20 pixels.
 *  - Small: 17 pixels.
 *  - Mini: 15 pixels.
 *
 * Label spacing and fonts: The tab labels should be in a font that’s
 * proportional to the size of the tab view control. In addition, the label
 * should be placed so that there are equal margins of space before and after
 * it. The guidelines below provide the specifications you should use for tab
 * labels:
 *  - Regular size: System font. Center in tab, leaving 12 pixels on each
 *side.
 *  - Small: Small system font. Center in tab, leaving 10 pixels on each side.
 *  - Mini: Mini system font. Center in tab, leaving 8 pixels on each side.
 *
 * Control spacing: Whether you decide to inset a tab view in a window or
 * extend its edges to the window sides and bottom, you should place the top
 * edge of the tab view 12 or 14 pixels below the bottom edge of the title bar
 * (or toolbar, if there is one). If you choose to inset a tab view in a
 * window, you should leave a margin of 20 pixels between the sides and bottom
 * of the tab view and the sides and bottom of the window (although 16 pixels
 * is also an acceptable margin-width). If you need to provide controls below
 * the tab view, leave enough space below the tab view so the controls are 20
 * pixels above the bottom edge of the window and 12 pixels between the tab
 * view and the controls.
 *
 * If you choose to extend the tab view sides and bottom so that they meet the
 * window sides and bottom, you should leave a margin of at least 20 pixels
 * between the content in the tab view and the tab-view edges.
 *
 * <URL: http://developer.apple.com/documentation/userexperience/Conceptual/
 *       AppleHIGuidelines/XHIGControls/XHIGControls.html#//apple_ref/doc/uid/
 *       TP30000359-TPXREF116>
 */

static void TabElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    GetThemeMetric(kThemeMetricLargeTabHeight, (SInt32 *) minHeight);
    *paddingPtr = Ttk_MakePadding(0, 0, 0, 2);

}

static void TabElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    CGRect bounds = BoxToRect(d, b);
    HIThemeTabDrawInfo info = {
	.version = 1,
	.style = Ttk_StateTableLookup(TabStyleTable, state),
	.direction = kThemeTabNorth,
	.size = kHIThemeTabSizeNormal,
	.adornment = Ttk_StateTableLookup(TabAdornmentTable, state),
	.kind = kHIThemeTabKindNormal,
	.position = Ttk_StateTableLookup(TabPositionTable, state),
    };

    BEGIN_DRAWING(d)
    if (TkMacOSXInDarkMode(tkwin)) {
	DrawDarkTab(bounds, state, dc.context);
    } else {
	ChkErr(HIThemeDrawTab, &bounds, &info, dc.context, HIOrientation,
	    NULL);
    }
    END_DRAWING
}

static Ttk_ElementSpec TabElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    TabElementSize,
    TabElementDraw
};

/*
 * Notebook panes:
 */

static void PaneElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    *paddingPtr = Ttk_MakePadding(9, 5, 9, 9);
}

static void PaneElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    CGRect bounds = BoxToRect(d, b);

    bounds.origin.y -= kThemeMetricTabFrameOverlap;
    bounds.size.height += kThemeMetricTabFrameOverlap;
    BEGIN_DRAWING(d)
    if ([NSApp macMinorVersion] > 8) {
	DrawGroupBox(bounds, dc.context, tkwin);
    } else {
	HIThemeTabPaneDrawInfo info = {
	    .version = 1,
	    .state = Ttk_StateTableLookup(ThemeStateTable, state),
	    .direction = kThemeTabNorth,
	    .size = kHIThemeTabSizeNormal,
	    .kind = kHIThemeTabKindNormal,
	    .adornment = kHIThemeTabPaneAdornmentNormal,
	    };
	bounds.origin.y -= kThemeMetricTabFrameOverlap;
	bounds.size.height += kThemeMetricTabFrameOverlap;
	ChkErr(HIThemeDrawTabPane, &bounds, &info, dc.context, HIOrientation);
    }
    END_DRAWING
}

static Ttk_ElementSpec PaneElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    PaneElementSize,
    PaneElementDraw
};

/*----------------------------------------------------------------------
 * +++ Labelframe elements --
 *
 * Labelframe borders: Use "primary group box ..."  Quoth
 * DrawThemePrimaryGroup reference: "The primary group box frame is drawn
 * inside the specified rectangle and is a maximum of 2 pixels thick."
 *
 * "Maximum of 2 pixels thick" is apparently a lie; looks more like 4 to me
 * with shading.
 */

static void GroupElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    *paddingPtr = Ttk_UniformPadding(4);
}

static void GroupElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    CGRect bounds = BoxToRect(d, b);

    BEGIN_DRAWING(d)
    if ([NSApp macMinorVersion] > 8) {
	DrawGroupBox(bounds, dc.context, tkwin);
    } else {
	const HIThemeGroupBoxDrawInfo info = {
	    .version = 0,
	    .state = Ttk_StateTableLookup(ThemeStateTable, state),
	    .kind = kHIThemeGroupBoxKindPrimaryOpaque,
	    };
	ChkErr(HIThemeDrawGroupBox, &bounds, &info, dc.context, HIOrientation);
    }
    END_DRAWING
}

static Ttk_ElementSpec GroupElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    GroupElementSize,
    GroupElementDraw
};

/*----------------------------------------------------------------------
 * +++ Entry elements --
 *
 *    3 pixels padding for focus rectangle
 *    2 pixels padding for EditTextFrame
 */

typedef struct {
    Tcl_Obj     *backgroundObj;
    Tcl_Obj     *fieldbackgroundObj;
} EntryElement;

#define ENTRY_DEFAULT_BACKGROUND "systemTextBackgroundColor"

static Ttk_ElementOptionSpec EntryElementOptions[] = {
    {"-background", TK_OPTION_BORDER,
     Tk_Offset(EntryElement, backgroundObj), ENTRY_DEFAULT_BACKGROUND},
    {"-fieldbackground", TK_OPTION_BORDER,
     Tk_Offset(EntryElement, fieldbackgroundObj), ENTRY_DEFAULT_BACKGROUND},
    {0}
};

static void EntryElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    *paddingPtr = Ttk_MakePadding(7, 5, 7, 6);
}

static void EntryElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    EntryElement *e = elementRecord;
    Ttk_Box inner = Ttk_PadBox(b, Ttk_UniformPadding(3));
    CGRect bounds = BoxToRect(d, inner);
    CGColorRef background;
    Tk_3DBorder backgroundPtr = NULL;
    static const char *defaultBG = ENTRY_DEFAULT_BACKGROUND;
    CGFloat rgba[4];

    if (TkMacOSXInDarkMode(tkwin)) {
	BEGIN_DRAWING(d)
	GetBackgroundColorRGBA(dc.context, tkwin, 1, rgba);

	/*
	 * Lighten the background to provide contrast.
	 */

	for (int i = 0; i < 3; i++) {
		rgba[i] += 9.0 / 255.0;
	    }
	background = CGColorFromRGBA(rgba);
	CGContextSetFillColorWithColor(dc.context, background);
	CGContextFillRect(dc.context, bounds);
	if (state & TTK_STATE_FOCUS) {
	    DrawDarkFocusRing(bounds, dc.context);
	} else {
	    DrawDarkFrame(bounds, dc.context, kHIThemeFrameTextFieldSquare);
	}
	END_DRAWING
    } else {
	const HIThemeFrameDrawInfo info = {
	    .version = 0,
	    .kind = kHIThemeFrameTextFieldSquare,
	    .state = Ttk_StateTableLookup(ThemeStateTable, state),
	    .isFocused = state & TTK_STATE_FOCUS,
	};

        /*
         * Earlier versions of the Aqua theme ignored the -fieldbackground
         * option and used the -background as if it were -fieldbackground.
         * Here we are enabling -fieldbackground.  For backwards
         * compatibility, if -fieldbackground is set to the default color and
         * -background is set to a different color then we use -background as
         * -fieldbackground.
         */

	if (0 != strcmp(Tcl_GetString(e->fieldbackgroundObj), defaultBG)) {
	    backgroundPtr =
		Tk_Get3DBorderFromObj(tkwin, e->fieldbackgroundObj);
	} else if (0 != strcmp(Tcl_GetString(e->backgroundObj), defaultBG)) {
	    backgroundPtr = Tk_Get3DBorderFromObj(tkwin, e->backgroundObj);
	}
	if (backgroundPtr != NULL) {
	    XFillRectangle(Tk_Display(tkwin), d,
		Tk_3DBorderGC(tkwin, backgroundPtr, TK_3D_FLAT_GC),
		inner.x, inner.y, inner.width, inner.height);
	}
	BEGIN_DRAWING(d)
	if (backgroundPtr == NULL) {
	    if ([NSApp macMinorVersion] > 8) {
		background = CGCOLOR([NSColor textBackgroundColor]);
		CGContextSetFillColorWithColor(dc.context, background);
	    } else {
		CGContextSetRGBFillColor(dc.context, 1.0, 1.0, 1.0, 1.0);
	    }
	    CGContextFillRect(dc.context, bounds);
	}
	ChkErr(HIThemeDrawFrame, &bounds, &info, dc.context, HIOrientation);
	END_DRAWING
    }
}

static Ttk_ElementSpec EntryElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(EntryElement),
    EntryElementOptions,
    EntryElementSize,
    EntryElementDraw
};

/*----------------------------------------------------------------------
 * +++ Combobox elements --
 *
 * NOTES:
 *      The HIToolbox has incomplete and inconsistent support for ComboBoxes.
 *      There is no constant available to get the height of a ComboBox with
 *      GetThemeMetric. In fact, ComboBoxes are the same (fixed) height as
 *      PopupButtons and PushButtons, but they have no shadow at the bottom.
 *      As a result, they are drawn 1 pixel above the center of the bounds
 *      rectangle rather than being centered like the other buttons.  One can
 *      request background bounds for a ComboBox, and it is reported with
 *      height 23, while the actual button face, including its 1-pixel border
 *      has height 21. Attempting to request the content bounds returns a 0x0
 *      rectangle.  Measurement indicates that the arrow button has width 18.
 *
 *      With no help available from HIToolbox, we have to use hard-wired
 *      constants for the padding. We shift the bounding rectangle downward by
 *      1 pixel to account for the fact that the button is not centered.
 */

static Ttk_Padding ComboboxPadding = {4, 2, 20, 2};

static void ComboboxElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    *minWidth = 24;
    *minHeight = 23;
    *paddingPtr = ComboboxPadding;
}

static void ComboboxElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    CGRect bounds = BoxToRect(d, b);
    const HIThemeButtonDrawInfo info = {
	.version = 0,
	.state = Ttk_StateTableLookup(ThemeStateTable, state),
	.kind = kThemeComboBox,
	.value = Ttk_StateTableLookup(ButtonValueTable, state),
	.adornment = Ttk_StateTableLookup(ButtonAdornmentTable, state),
    };

    BEGIN_DRAWING(d)
    bounds.origin.y += 1;
    if (TkMacOSXInDarkMode(tkwin)) {
	    bounds.size.height += 1;
	DrawDarkButton(bounds, info.kind, state, dc.context);
	} else if ([NSApp macMinorVersion] > 8) {
	    if ((state & TTK_STATE_BACKGROUND) &&
		!(state & TTK_STATE_DISABLED)) {
	    NSColor *background = [NSColor textBackgroundColor];
	    CGRect innerBounds = CGRectInset(bounds, 1, 2);
	    SolidFillRoundedRectangle(dc.context, innerBounds, 4,
				      CGCOLOR(background));
	}
    ChkErr(HIThemeDrawButton, &bounds, &info, dc.context, HIOrientation,
		NULL);
    }
    END_DRAWING
}

static Ttk_ElementSpec ComboboxElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    ComboboxElementSize,
    ComboboxElementDraw
};

/*----------------------------------------------------------------------
 * +++ Spinbutton elements --
 *
 *      From Apple HIG, part III, section "Controls", "The Stepper Control":
 *      there should be 2 pixels of space between the stepper control (AKA
 *      IncDecButton, AKA "little arrows") and the text field it modifies.
 *
 *      Ttk expects the up and down arrows to be distinct elements but
 *      HIToolbox draws them as one widget with two different pressed states.
 *      We work around this by defining them as separate elements in the
 *      layout, but making each one have a drawing method which also draws the
 *      other one.  The down button does no drawing when not pressed, and when
 *      pressed draws the entire IncDecButton in its "pressed down" state.
 *      The up button draws the entire IncDecButton when not pressed and when
 *      pressed draws the IncDecButton in its "pressed up" state.  NOTE: This
 *      means that when the down button is pressed the IncDecButton will be
 *      drawn twice, first in unpressed state by the up arrow and then in
 *      "pressed down" state by the down button.  The drawing must be done in
 *      that order.  So the up button must be listed first in the layout.
 */

static Ttk_Padding SpinbuttonMargins = {0, 0, 2, 0};

static void SpinButtonUpElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    SInt32 s;

    ChkErr(GetThemeMetric, kThemeMetricLittleArrowsWidth, &s);
    *minWidth = s + Ttk_PaddingWidth(SpinbuttonMargins);
    ChkErr(GetThemeMetric, kThemeMetricLittleArrowsHeight, &s);
    *minHeight = (s + Ttk_PaddingHeight(SpinbuttonMargins)) / 2;
}

static void SpinButtonUpElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    CGRect bounds = BoxToRect(d, Ttk_PadBox(b, SpinbuttonMargins));
    int infoState;

    bounds.size.height *= 2;
    if (state & TTK_STATE_PRESSED) {
	infoState = kThemeStatePressedUp;
    } else {
	infoState = Ttk_StateTableLookup(ThemeStateTable, state);
    }
    const HIThemeButtonDrawInfo info = {
	.version = 0,
	.state = infoState,
	.kind = kThemeIncDecButton,
	.value = Ttk_StateTableLookup(ButtonValueTable, state),
	.adornment = kThemeAdornmentNone,
    };
    BEGIN_DRAWING(d)
    if (TkMacOSXInDarkMode(tkwin)) {
	DrawDarkIncDecButton(bounds, infoState, state, dc.context);
    } else {
	ChkErr(HIThemeDrawButton, &bounds, &info, dc.context, HIOrientation,
	       NULL);
    }
    END_DRAWING
}

static Ttk_ElementSpec SpinButtonUpElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    SpinButtonUpElementSize,
    SpinButtonUpElementDraw
};
static void SpinButtonDownElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    SInt32 s;

    ChkErr(GetThemeMetric, kThemeMetricLittleArrowsWidth, &s);
    *minWidth = s + Ttk_PaddingWidth(SpinbuttonMargins);
    ChkErr(GetThemeMetric, kThemeMetricLittleArrowsHeight, &s);
    *minHeight = (s + Ttk_PaddingHeight(SpinbuttonMargins)) / 2;
}

static void SpinButtonDownElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    CGRect bounds = BoxToRect(d, Ttk_PadBox(b, SpinbuttonMargins));
    int infoState = 0;

    bounds.origin.y -= bounds.size.height;
    bounds.size.height *= 2;
    if (state & TTK_STATE_PRESSED) {
	infoState = kThemeStatePressedDown;
    } else {
	return;
    }
    const HIThemeButtonDrawInfo info = {
	.version = 0,
	.state = infoState,
	.kind = kThemeIncDecButton,
	.value = Ttk_StateTableLookup(ButtonValueTable, state),
	.adornment = kThemeAdornmentNone,
    };

    BEGIN_DRAWING(d)
    if (TkMacOSXInDarkMode(tkwin)) {
	DrawDarkIncDecButton(bounds, infoState, state, dc.context);
    } else {
	ChkErr(HIThemeDrawButton, &bounds, &info, dc.context, HIOrientation,
	       NULL);
    }
    END_DRAWING
}

static Ttk_ElementSpec SpinButtonDownElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    SpinButtonDownElementSize,
    SpinButtonDownElementDraw
};

/*----------------------------------------------------------------------
 * +++ DrawThemeTrack-based elements --
 *
 *    Progress bars and scales. (See also: <<NOTE-TRACKS>>)
 */

/*
 * Apple does not change the appearance of a slider when the window becomes
 * inactive.  So we shouldn't either.
 */

static Ttk_StateTable ThemeTrackEnableTable[] = {
    {kThemeTrackDisabled, TTK_STATE_DISABLED, 0},
    {kThemeTrackActive, TTK_STATE_BACKGROUND, 0},
    {kThemeTrackActive, 0, 0}
    /* { kThemeTrackNothingToScroll, ?, ? }, */
};

typedef struct {        /* TrackElement client data */
    ThemeTrackKind kind;
    SInt32 thicknessMetric;
} TrackElementData;

static TrackElementData ScaleData = {
    kThemeSlider, kThemeMetricHSliderHeight
};

typedef struct {
    Tcl_Obj *fromObj;           /* minimum value */
    Tcl_Obj *toObj;             /* maximum value */
    Tcl_Obj *valueObj;          /* current value */
    Tcl_Obj *orientObj;         /* horizontal / vertical */
} TrackElement;

static Ttk_ElementOptionSpec TrackElementOptions[] = {
    {"-from", TK_OPTION_DOUBLE, Tk_Offset(TrackElement, fromObj)},
    {"-to", TK_OPTION_DOUBLE, Tk_Offset(TrackElement, toObj)},
    {"-value", TK_OPTION_DOUBLE, Tk_Offset(TrackElement, valueObj)},
    {"-orient", TK_OPTION_STRING, Tk_Offset(TrackElement, orientObj)},
    {0, 0, 0}
};
static void TrackElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    TrackElementData *data = clientData;
    SInt32 size = 24;   /* reasonable default ... */

    ChkErr(GetThemeMetric, data->thicknessMetric, &size);
    *minWidth = *minHeight = size;
}

static void TrackElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    TrackElementData *data = clientData;
    TrackElement *elem = elementRecord;
    int orientation = TTK_ORIENT_HORIZONTAL;
    double from = 0, to = 100, value = 0, factor;

    Ttk_GetOrientFromObj(NULL, elem->orientObj, &orientation);
    Tcl_GetDoubleFromObj(NULL, elem->fromObj, &from);
    Tcl_GetDoubleFromObj(NULL, elem->toObj, &to);
    Tcl_GetDoubleFromObj(NULL, elem->valueObj, &value);
    factor = RangeToFactor(to);

    HIThemeTrackDrawInfo info = {
	.version = 0,
	.kind = data->kind,
	.bounds = BoxToRect(d, b),
	.min = from * factor,
	.max = to * factor,
	.value = value * factor,
	.attributes = kThemeTrackShowThumb |
	    (orientation == TTK_ORIENT_HORIZONTAL ?
	    kThemeTrackHorizontal : 0),
	.enableState = Ttk_StateTableLookup(ThemeTrackEnableTable, state),
	.trackInfo.progress.phase = 0,
    };

    if (info.kind == kThemeSlider) {
	info.trackInfo.slider.pressState = state & TTK_STATE_PRESSED ?
	    kThemeThumbPressed : 0;
	if (state & TTK_STATE_ALTERNATE) {
	    info.trackInfo.slider.thumbDir = kThemeThumbDownward;
	} else {
	    info.trackInfo.slider.thumbDir = kThemeThumbPlain;
	}
    }
    BEGIN_DRAWING(d)
    if (TkMacOSXInDarkMode(tkwin)) {
	CGRect bounds = BoxToRect(d, b);
	CGColorRef trackColor = CGColorFromGray(darkTrack);
	if (orientation == TTK_ORIENT_HORIZONTAL) {
	    bounds = CGRectInset(bounds, 1, bounds.size.height / 2 - 2);
	} else {
	    bounds = CGRectInset(bounds, bounds.size.width / 2 - 3, 2);
	}
	SolidFillRoundedRectangle(dc.context, bounds, 2, trackColor);
    }
    ChkErr(HIThemeDrawTrack, &info, NULL, dc.context, HIOrientation);
    END_DRAWING
}

static Ttk_ElementSpec TrackElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(TrackElement),
    TrackElementOptions,
    TrackElementSize,
    TrackElementDraw
};

/*----------------------------------------------------------------------
 * Slider elements -- <<NOTE-TRACKS>>
 *
 * Has geometry only. The Scale widget adjusts the position of this element,
 * and uses it for hit detection. In the Aqua theme, the slider is actually
 * drawn as part of the trough element.
 *
 */

static void SliderElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    *minWidth = *minHeight = 24;
}

static Ttk_ElementSpec SliderElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    SliderElementSize,
    TtkNullElementDraw
};

/*----------------------------------------------------------------------
 * +++ Progress bar elements --
 *
 * @@@ NOTE: According to an older revision of the Aqua reference docs,
 * @@@ the 'phase' field is between 0 and 4. Newer revisions say
 * @@@ that it can be any UInt8 value.
 */

typedef struct {
    Tcl_Obj *orientObj;         /* horizontal / vertical */
    Tcl_Obj *valueObj;          /* current value */
    Tcl_Obj *maximumObj;        /* maximum value */
    Tcl_Obj *phaseObj;          /* animation phase */
    Tcl_Obj *modeObj;           /* progress bar mode */
} PbarElement;

static Ttk_ElementOptionSpec PbarElementOptions[] = {
    {"-orient", TK_OPTION_STRING,
     Tk_Offset(PbarElement, orientObj), "horizontal"},
    {"-value", TK_OPTION_DOUBLE,
     Tk_Offset(PbarElement, valueObj), "0"},
    {"-maximum", TK_OPTION_DOUBLE,
     Tk_Offset(PbarElement, maximumObj), "100"},
    {"-phase", TK_OPTION_INT,
     Tk_Offset(PbarElement, phaseObj), "0"},
    {"-mode", TK_OPTION_STRING,
     Tk_Offset(PbarElement, modeObj), "determinate"},
    {0, 0, 0, 0}
};
static void PbarElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    SInt32 size = 24;           /* @@@ Check HIG for correct default */

    ChkErr(GetThemeMetric, kThemeMetricLargeProgressBarThickness, &size);
    *minWidth = *minHeight = size;
}

static void PbarElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    PbarElement *pbar = elementRecord;
    int orientation = TTK_ORIENT_HORIZONTAL, phase = 0;
    double value = 0, maximum = 100, factor;

    Ttk_GetOrientFromObj(NULL, pbar->orientObj, &orientation);
    Tcl_GetDoubleFromObj(NULL, pbar->valueObj, &value);
    Tcl_GetDoubleFromObj(NULL, pbar->maximumObj, &maximum);
    Tcl_GetIntFromObj(NULL, pbar->phaseObj, &phase);
    factor = RangeToFactor(maximum);

    HIThemeTrackDrawInfo info = {
	.version = 0,
	.kind =
	    (!strcmp("indeterminate",
	    Tcl_GetString(pbar->modeObj)) && value) ?
	    kThemeIndeterminateBar : kThemeProgressBar,
	.bounds = BoxToRect(d, b),
	.min = 0,
	.max = maximum * factor,
	.value = value * factor,
	.attributes = kThemeTrackShowThumb |
	    (orientation == TTK_ORIENT_HORIZONTAL ?
	    kThemeTrackHorizontal : 0),
	.enableState = Ttk_StateTableLookup(ThemeTrackEnableTable, state),
	.trackInfo.progress.phase = phase,
    };

    BEGIN_DRAWING(d)
    if (TkMacOSXInDarkMode(tkwin)) {
	CGRect bounds = BoxToRect(d, b);
	CGColorRef trackColor = CGColorFromGray(darkTrack);
	if (orientation == TTK_ORIENT_HORIZONTAL) {
	    bounds = CGRectInset(bounds, 1, bounds.size.height / 2 - 3);
	} else {
	    bounds = CGRectInset(bounds, bounds.size.width / 2 - 3, 1);
	}
	SolidFillRoundedRectangle(dc.context, bounds, 3, trackColor);
    }
    ChkErr(HIThemeDrawTrack, &info, NULL, dc.context, HIOrientation);
    END_DRAWING
}

static Ttk_ElementSpec PbarElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(PbarElement),
    PbarElementOptions,
    PbarElementSize,
    PbarElementDraw
};

/*----------------------------------------------------------------------
 * +++ Scrollbar elements
 */

typedef struct
{
    Tcl_Obj *orientObj;
} ScrollbarElement;

static Ttk_ElementOptionSpec ScrollbarElementOptions[] = {
    {"-orient", TK_OPTION_STRING,
     Tk_Offset(ScrollbarElement, orientObj), "horizontal"},
    {0, 0, 0, 0}
};
static void TroughElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    ScrollbarElement *scrollbar = elementRecord;
    int orientation = TTK_ORIENT_HORIZONTAL;
    SInt32 thickness = 15;

    Ttk_GetOrientFromObj(NULL, scrollbar->orientObj, &orientation);
    ChkErr(GetThemeMetric, kThemeMetricScrollBarWidth, &thickness);
    if (orientation == TTK_ORIENT_HORIZONTAL) {
	*minHeight = thickness;
	if ([NSApp macMinorVersion] > 7) {
	    *paddingPtr = Ttk_MakePadding(4, 4, 4, 3);
	}
    } else {
	*minWidth = thickness;
	if ([NSApp macMinorVersion] > 7) {
	    *paddingPtr = Ttk_MakePadding(4, 4, 3, 4);
	}
    }
}

static void TroughElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    ScrollbarElement *scrollbar = elementRecord;
    int orientation = TTK_ORIENT_HORIZONTAL;
    CGRect bounds = BoxToRect(d, b);
    GrayColor bgGray;

    Ttk_GetOrientFromObj(NULL, scrollbar->orientObj, &orientation);
    if (orientation == TTK_ORIENT_HORIZONTAL) {
	bounds = CGRectInset(bounds, 0, 1);
    } else {
	bounds = CGRectInset(bounds, 1, 0);
    }
    BEGIN_DRAWING(d)
    if ([NSApp macMinorVersion] > 8) {
	bgGray = TkMacOSXInDarkMode(tkwin) ? darkTrough : lightTrough;
	CGContextSetFillColorWithColor(dc.context, CGColorFromGray(bgGray));
    } else {
	ChkErr(HIThemeSetFill, kThemeBrushDocumentWindowBackground, NULL,
	    dc.context, HIOrientation);
    }
    CGContextFillRect(dc.context, bounds);
    END_DRAWING
}

static Ttk_ElementSpec TroughElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(ScrollbarElement),
    ScrollbarElementOptions,
    TroughElementSize,
    TroughElementDraw
};
static void ThumbElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    ScrollbarElement *scrollbar = elementRecord;
    int orientation = TTK_ORIENT_HORIZONTAL;

    Ttk_GetOrientFromObj(NULL, scrollbar->orientObj, &orientation);
    if (orientation == TTK_ORIENT_VERTICAL) {
	*minHeight = 18;
	*minWidth = 8;
    } else {
	*minHeight = 8;
	*minWidth = 18;
    }
}

static void ThumbElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    ScrollbarElement *scrollbar = elementRecord;
    int orientation = TTK_ORIENT_HORIZONTAL;

    Ttk_GetOrientFromObj(NULL, scrollbar->orientObj, &orientation);

    /*
     * In order to make ttk scrollbars work correctly it is necessary to be
     * able to display the thumb element at the size and location which the ttk
     * scrollbar widget requests.  The algorithm that HIToolbox uses to
     * determine the thumb geometry from the input values of min, max, value
     * and viewSize is undocumented.  A seemingly natural algorithm is
     * implemented below.  This code uses that algorithm for older OS versions,
     * because using HITools also handles drawing the buttons and 3D thumb used
     * on those systems.  For newer systems the cleanest approach is to just
     * draw the thumb directly.
     */

    if ([NSApp macMinorVersion] > 8) {
	CGRect thumbBounds = BoxToRect(d, b);
	CGColorRef thumbColor;
	GrayColor bgGray;

	/*
	 * Apple does not draw the thumb when scrolling is not possible.
	 */

	if ((orientation == TTK_ORIENT_HORIZONTAL &&
	    thumbBounds.size.width >= Tk_Width(tkwin) - 8) ||
	    (orientation == TTK_ORIENT_VERTICAL &&
	    thumbBounds.size.height >= Tk_Height(tkwin) - 8)) {
	    return;
	}
	int isDark = TkMacOSXInDarkMode(tkwin);
	if ((state & TTK_STATE_PRESSED) ||
	    (state & TTK_STATE_HOVER)) {
	    bgGray = isDark ? darkActiveThumb : lightActiveThumb;
	} else {
	    bgGray = isDark ? darkInactiveThumb : lightInactiveThumb;
	}
	thumbColor = CGColorFromGray(bgGray);
	BEGIN_DRAWING(d)
	SolidFillRoundedRectangle(dc.context, thumbBounds, 4, thumbColor);
	END_DRAWING
    } else {
	double thumbSize, trackSize, visibleSize, factor, fraction;
	MacDrawable *macWin = (MacDrawable *) Tk_WindowId(tkwin);
	CGRect troughBounds = {{macWin->xOff, macWin->yOff},
			       {Tk_Width(tkwin), Tk_Height(tkwin)}};

        /*
         * The info struct has integer fields, which will be converted to
         * floats in the drawing routine.  All of values provided in the info
         * struct, namely min, max, value, and viewSize are only defined up to
         * an arbitrary scale factor.  To avoid roundoff error we scale so
         * that the viewSize is a large float which is smaller than the
         * largest int.
         */

	HIThemeTrackDrawInfo info = {
	    .version = 0,
	    .bounds = troughBounds,
	    .min = 0,
	    .attributes = kThemeTrackShowThumb |
		kThemeTrackThumbRgnIsNotGhost,
	    .enableState = kThemeTrackActive
	};
	factor = RangeToFactor(100.0);
	if (orientation == TTK_ORIENT_HORIZONTAL) {
	    trackSize = troughBounds.size.width;
	    thumbSize = b.width;
	    fraction = b.x / trackSize;
	} else {
	    trackSize = troughBounds.size.height;
	    thumbSize = b.height;
	    fraction = b.y / trackSize;
	}
	visibleSize = (thumbSize / trackSize) * factor;
	info.max = factor - visibleSize;
	info.trackInfo.scrollbar.viewsize = visibleSize;
	if ([NSApp macMinorVersion] < 8 ||
	    orientation == TTK_ORIENT_HORIZONTAL) {
	    info.value = factor * fraction;
	} else {
	    info.value = info.max - factor * fraction;
	}
	if ((state & TTK_STATE_PRESSED) ||
	    (state & TTK_STATE_HOVER)) {
	    info.trackInfo.scrollbar.pressState = kThemeThumbPressed;
	} else {
	    info.trackInfo.scrollbar.pressState = 0;
	}
	if (orientation == TTK_ORIENT_HORIZONTAL) {
	    info.attributes |= kThemeTrackHorizontal;
	} else {
	    info.attributes &= ~kThemeTrackHorizontal;
	}
	BEGIN_DRAWING(d)
	HIThemeDrawTrack(&info, 0, dc.context, kHIThemeOrientationNormal);
	END_DRAWING
    }
}

static Ttk_ElementSpec ThumbElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(ScrollbarElement),
    ScrollbarElementOptions,
    ThumbElementSize,
    ThumbElementDraw
};
static void ArrowElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    if ([NSApp macMinorVersion] < 8) {
	*minHeight = *minWidth = 14;
    } else {
	*minHeight = *minWidth = -1;
    }
}

static Ttk_ElementSpec ArrowElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(ScrollbarElement),
    ScrollbarElementOptions,
    ArrowElementSize,
    TtkNullElementDraw
};

/*----------------------------------------------------------------------
 * +++ Separator element.
 *
 *    DrawThemeSeparator() guesses the orientation of the line from the width
 *    and height of the rectangle, so the same element can can be used for
 *    horizontal, vertical, and general separators.
 */

static void SeparatorElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    *minWidth = *minHeight = 1;
}

static void SeparatorElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    unsigned int state)
{
    CGRect bounds = BoxToRect(d, b);
    const HIThemeSeparatorDrawInfo info = {
	.version = 0,
        /* Separator only supports kThemeStateActive, kThemeStateInactive */
	.state = Ttk_StateTableLookup(ThemeStateTable,
	    state & TTK_STATE_BACKGROUND),
    };

    BEGIN_DRAWING(d)
    if (TkMacOSXInDarkMode(tkwin)) {
	DrawDarkSeparator(bounds, dc.context, tkwin);
    } else {
	ChkErr(HIThemeDrawSeparator, &bounds, &info, dc.context,
	    HIOrientation);
    }
    END_DRAWING
}

static Ttk_ElementSpec SeparatorElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    SeparatorElementSize,
    SeparatorElementDraw
};

/*----------------------------------------------------------------------
 * +++ Size grip elements -- (obsolete)
 */

static const ThemeGrowDirection sizegripGrowDirection
    = kThemeGrowRight | kThemeGrowDown;

static void SizegripElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    HIThemeGrowBoxDrawInfo info = {
	.version = 0,
	.state = kThemeStateActive,
	.kind = kHIThemeGrowBoxKindNormal,
	.direction = sizegripGrowDirection,
	.size = kHIThemeGrowBoxSizeNormal,
    };
    CGRect bounds = CGRectZero;

    ChkErr(HIThemeGetGrowBoxBounds, &bounds.origin, &info, &bounds);
    *minWidth = bounds.size.width;
    *minHeight = bounds.size.height;
}

static void SizegripElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    unsigned int state)
{
    CGRect bounds = BoxToRect(d, b);
    HIThemeGrowBoxDrawInfo info = {
	.version = 0,
        /* Grow box only supports kThemeStateActive, kThemeStateInactive */
	.state = Ttk_StateTableLookup(ThemeStateTable,
	    state & TTK_STATE_BACKGROUND),
	.kind = kHIThemeGrowBoxKindNormal,
	.direction = sizegripGrowDirection,
	.size = kHIThemeGrowBoxSizeNormal,
    };

    BEGIN_DRAWING(d)
    ChkErr(HIThemeDrawGrowBox, &bounds.origin, &info, dc.context,
	HIOrientation);
    END_DRAWING
}

static Ttk_ElementSpec SizegripElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    SizegripElementSize,
    SizegripElementDraw
};

/*----------------------------------------------------------------------
 * +++ Background and fill elements --
 *
 *      Before drawing any ttk widget, its bounding rectangle is filled with a
 *      background color.  This color must match the background color of the
 *      containing widget to avoid looking ugly. The need for care when doing
 *      this is exacerbated by the fact that ttk enforces its "native look" by
 *      not allowing user control of the background or highlight colors of ttk
 *      widgets.
 *
 *      This job is made more complicated in recent versions of macOS by the
 *      fact that the Appkit GroupBox (used for ttk LabelFrames) and
 *      TabbedPane (used for the Notebook widget) both place their content
 *      inside a rectangle with rounded corners that has a color which
 *      contrasts with the dialog background color.  Moreover, although the
 *      Apple human interface guidelines recommend against doing so, there are
 *      times when one wants to nest these widgets, for example placing a
 *      GroupBox inside of a TabbedPane.  To have the right contrast, each
 *      level of nesting requires a different color.
 *
 *      Previous Tk releases used the HIThemeDrawGroupBox routine to draw
 *      GroupBoxes and TabbedPanes. This meant that the best that could be
 *      done was to set the GroupBox to be of kind
 *      kHIThemeGroupBoxKindPrimaryOpaque, and set its fill color to be the
 *      system background color.  If widgets inside the box were drawn with
 *      the system background color the backgrounds would match.  But this
 *      produces a GroupBox with no contrast, the only visual clue being a
 *      faint highlighting around the top of the GroupBox.  Moreover, the
 *      TabbedPane does not have an Opaque version, so while it is drawn
 *      inside a contrasting rounded rectangle, the widgets inside the pane
 *      needed to be enclosed in a frame with the system background
 *      color. This added a visual artifact since the frame's background color
 *      does not match the Pane's background color.  That code has now been
 *      replaced with the standalone drawing procedure macOSXDrawGroupBox,
 *      which draws a rounded rectangle with an appropriate contrasting
 *      background color.
 *
 *      Patterned backgrounds, which are now obsolete, should be aligned with
 *      the coordinate system of the top-level window.  Apparently failing to
 *      do this used to cause graphics anomalies when drawing into an
 *      off-screen graphics port.  The code for handling this is currently
 *      commented out.
 */

static void FillElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    CGRect bounds = BoxToRect(d, b);

    if ([NSApp macMinorVersion] > 8) {
	CGColorRef bgColor;
	BEGIN_DRAWING(d)
	bgColor = GetBackgroundCGColor(dc.context, tkwin, 0);
	CGContextSetFillColorWithColor(dc.context, bgColor);
	CGContextFillRect(dc.context, bounds);
	END_DRAWING
    } else {
	ThemeBrush brush = (state & TTK_STATE_BACKGROUND)
	    ? kThemeBrushModelessDialogBackgroundInactive
	    : kThemeBrushModelessDialogBackgroundActive;
	BEGIN_DRAWING(d)
	ChkErr(HIThemeSetFill, brush, NULL, dc.context, HIOrientation);
	CGContextFillRect(dc.context, bounds);
	END_DRAWING
    }
}

static void BackgroundElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    unsigned int state)
{
    FillElementDraw(clientData, elementRecord, tkwin, d, Ttk_WinBox(tkwin),
	state);
}

static Ttk_ElementSpec FillElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    TtkNullElementSize,
    FillElementDraw
};
static Ttk_ElementSpec BackgroundElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    TtkNullElementSize,
    BackgroundElementDraw
};

/*----------------------------------------------------------------------
 * +++ ToolbarBackground element -- toolbar style for frames.
 *
 *    This is very similar to the normal background element, but uses a
 *    different ThemeBrush in order to get the lighter pinstripe effect
 *    used in toolbars. We use SetThemeBackground() rather than
 *    ApplyThemeBackground() in order to get the right style.
 *
 *    <URL: http://developer.apple.com/documentation/Carbon/Reference/
 *    Appearance_Manager/appearance_manager/constant_7.html#/
 *    /apple_ref/doc/uid/TP30000243/C005321>
 *
 */

static void ToolbarBackgroundElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    ThemeBrush brush = kThemeBrushToolbarBackground;
    CGRect bounds = BoxToRect(d, Ttk_WinBox(tkwin));

    BEGIN_DRAWING(d)
    ChkErr(HIThemeSetFill, brush, NULL, dc.context, HIOrientation);
    //QDSetPatternOrigin(PatternOrigin(tkwin, d));
    CGContextFillRect(dc.context, bounds);
    END_DRAWING
}

static Ttk_ElementSpec ToolbarBackgroundElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    TtkNullElementSize,
    ToolbarBackgroundElementDraw
};

/*----------------------------------------------------------------------
 * +++ Field elements --
 *
 *      Used for the Treeview widget. This is like the BackgroundElement
 *      except that the fieldbackground color is configureable.
 */

typedef struct {
    Tcl_Obj     *backgroundObj;
} FieldElement;

static Ttk_ElementOptionSpec FieldElementOptions[] = {
    {"-fieldbackground", TK_OPTION_BORDER,
     Tk_Offset(FieldElement, backgroundObj), "white"},
    {NULL, 0, 0, NULL}
};

static void FieldElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    FieldElement *e = elementRecord;
    Tk_3DBorder backgroundPtr =
	Tk_Get3DBorderFromObj(tkwin, e->backgroundObj);

    XFillRectangle(Tk_Display(tkwin), d,
	Tk_3DBorderGC(tkwin, backgroundPtr, TK_3D_FLAT_GC),
	b.x, b.y, b.width, b.height);
}

static Ttk_ElementSpec FieldElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(FieldElement),
    FieldElementOptions,
    TtkNullElementSize,
    FieldElementDraw
};

/*----------------------------------------------------------------------
 * +++ Treeview headers --
 *
 *    On systems older than 10.9 The header is a kThemeListHeaderButton drawn
 *    by HIToolbox.  On newer systems those buttons do not match the Apple
 *    buttons, so we draw them from scratch.
 */

static Ttk_StateTable TreeHeaderValueTable[] = {
    {kThemeButtonOn, TTK_STATE_ALTERNATE},
    {kThemeButtonOn, TTK_STATE_SELECTED},
    {kThemeButtonOff, 0}
};

static Ttk_StateTable TreeHeaderAdornmentTable[] = {
    {kThemeAdornmentHeaderButtonSortUp,
     TTK_STATE_ALTERNATE | TTK_TREEVIEW_STATE_SORTARROW},
    {kThemeAdornmentDefault,
     TTK_STATE_SELECTED | TTK_TREEVIEW_STATE_SORTARROW},
    {kThemeAdornmentHeaderButtonNoSortArrow, TTK_STATE_ALTERNATE},
    {kThemeAdornmentHeaderButtonNoSortArrow, TTK_STATE_SELECTED},
    {kThemeAdornmentFocus, TTK_STATE_FOCUS},
    {kThemeAdornmentNone, 0}
};

static void TreeAreaElementSize (
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{

    /*
     * Padding is needed to get the heading text to align correctly, since the
     * widget expects the heading to be the same height as a row.
     */

    if ([NSApp macMinorVersion] > 8) {
	paddingPtr->top = 4;
    }
}

static Ttk_ElementSpec TreeAreaElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    TreeAreaElementSize,
    TtkNullElementDraw
};
static void TreeHeaderElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    if ([NSApp macMinorVersion] > 8) {
	*minHeight = 24;
    } else {
	ButtonElementSize(clientData, elementRecord, tkwin, minWidth,
	    minHeight, paddingPtr);
    }
}

static void TreeHeaderElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    ThemeButtonParams *params = clientData;
    CGRect bounds = BoxToRect(d, b);
    const HIThemeButtonDrawInfo info = {
	.version = 0,
	.state = Ttk_StateTableLookup(ThemeStateTable, state),
	.kind = params->kind,
	.value = Ttk_StateTableLookup(TreeHeaderValueTable, state),
	.adornment = Ttk_StateTableLookup(TreeHeaderAdornmentTable, state),
    };

    BEGIN_DRAWING(d)
    if ([NSApp macMinorVersion] > 8) {

        /*
         * Compensate for the padding added in TreeHeaderElementSize, so
         * the larger heading will be drawn at the top of the widget.
         */

	bounds.origin.y -= 4;
	if (TkMacOSXInDarkMode(tkwin)) {
	    DrawDarkListHeader(bounds, dc.context, tkwin, state);
	} else {
	    DrawListHeader(bounds, dc.context, tkwin, state);
	}
    } else {
	ChkErr(HIThemeDrawButton, &bounds, &info, dc.context, HIOrientation,
	    NULL);
    }
    END_DRAWING
}

static Ttk_ElementSpec TreeHeaderElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    TreeHeaderElementSize,
    TreeHeaderElementDraw
};

/*----------------------------------------------------------------------
 * +++ Disclosure triangles --
 */

#define TTK_TREEVIEW_STATE_OPEN         TTK_STATE_USER1
#define TTK_TREEVIEW_STATE_LEAF         TTK_STATE_USER2
static Ttk_StateTable DisclosureValueTable[] = {
    {kThemeDisclosureDown, TTK_TREEVIEW_STATE_OPEN, 0},
    {kThemeDisclosureRight, 0, 0},
};
static void DisclosureElementSize(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    SInt32 s;

    ChkErr(GetThemeMetric, kThemeMetricDisclosureTriangleWidth, &s);
    *minWidth = s;
    ChkErr(GetThemeMetric, kThemeMetricDisclosureTriangleHeight, &s);
    *minHeight = s;
}

static void DisclosureElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    if (!(state & TTK_TREEVIEW_STATE_LEAF)) {
	int triangleState = TkMacOSXInDarkMode(tkwin) ?
	    kThemeStateInactive : kThemeStateActive;
	CGRect bounds = BoxToRect(d, b);
	const HIThemeButtonDrawInfo info = {
	    .version = 0,
	    .state = triangleState,
	    .kind = kThemeDisclosureTriangle,
	    .value = Ttk_StateTableLookup(DisclosureValueTable, state),
	    .adornment = kThemeAdornmentDrawIndicatorOnly,
	};

	BEGIN_DRAWING(d)
	ChkErr(HIThemeDrawButton, &bounds, &info, dc.context, HIOrientation,
	    NULL);
	END_DRAWING
    }
}

static Ttk_ElementSpec DisclosureElementSpec = {
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    DisclosureElementSize,
    DisclosureElementDraw
};

/*----------------------------------------------------------------------
 * +++ Widget layouts --
 */

TTK_BEGIN_LAYOUT_TABLE(LayoutTable)

TTK_LAYOUT("Toolbar",
    TTK_NODE("Toolbar.background", TTK_FILL_BOTH))

TTK_LAYOUT("TButton",
    TTK_GROUP("Button.button", TTK_FILL_BOTH,
    TTK_GROUP("Button.padding", TTK_FILL_BOTH,
    TTK_NODE("Button.label", TTK_FILL_BOTH))))

TTK_LAYOUT("TRadiobutton",
    TTK_GROUP("Radiobutton.button", TTK_FILL_BOTH,
    TTK_GROUP("Radiobutton.padding", TTK_FILL_BOTH,
    TTK_NODE("Radiobutton.label", TTK_PACK_LEFT))))

TTK_LAYOUT("TCheckbutton",
    TTK_GROUP("Checkbutton.button", TTK_FILL_BOTH,
    TTK_GROUP("Checkbutton.padding", TTK_FILL_BOTH,
    TTK_NODE("Checkbutton.label", TTK_PACK_LEFT))))

TTK_LAYOUT("TMenubutton",
    TTK_GROUP("Menubutton.button", TTK_FILL_BOTH,
    TTK_GROUP("Menubutton.padding", TTK_FILL_BOTH,
    TTK_NODE("Menubutton.label", TTK_PACK_LEFT))))

TTK_LAYOUT("TCombobox",
    TTK_GROUP("Combobox.button", TTK_FILL_BOTH,
    TTK_GROUP("Combobox.padding", TTK_FILL_BOTH,
    TTK_NODE("Combobox.textarea", TTK_FILL_BOTH))))

/* Image Button - no button */
TTK_LAYOUT("ImageButton",
    TTK_GROUP("Button.padding", TTK_FILL_BOTH,
    TTK_NODE("Button.label", TTK_FILL_BOTH)))

/* Gradient Button */
TTK_LAYOUT("GradientButton",
    TTK_GROUP("GradientButton.button", TTK_FILL_BOTH,
    TTK_GROUP("Button.padding", TTK_FILL_BOTH,
    TTK_NODE("Button.label", TTK_FILL_BOTH))))

/* DisclosureButton (not a triangle) -- No label, no border*/
TTK_LAYOUT("DisclosureButton",
    TTK_NODE("DisclosureButton.button", TTK_FILL_BOTH))

/* HelpButton -- No label, no border*/
TTK_LAYOUT("HelpButton",
    TTK_NODE("HelpButton.button", TTK_FILL_BOTH))

/* Notebook tabs -- no focus ring */
TTK_LAYOUT("Tab",
    TTK_GROUP("Notebook.tab", TTK_FILL_BOTH,
    TTK_GROUP("Notebook.padding", TTK_EXPAND | TTK_FILL_BOTH,
    TTK_NODE("Notebook.label", TTK_EXPAND | TTK_FILL_BOTH))))

/* Spinbox -- buttons 2px to the right of the field. */
TTK_LAYOUT("TSpinbox",
    TTK_GROUP("Spinbox.buttons", TTK_PACK_RIGHT,
    TTK_NODE("Spinbox.uparrow", TTK_PACK_TOP | TTK_STICK_E)
    TTK_NODE("Spinbox.downarrow", TTK_PACK_BOTTOM | TTK_STICK_E))
    TTK_GROUP("Spinbox.field", TTK_EXPAND | TTK_FILL_X,
    TTK_NODE("Spinbox.textarea", TTK_EXPAND | TTK_FILL_X)))

/* Progress bars -- track only */
TTK_LAYOUT("TProgressbar",
    TTK_NODE("Progressbar.track", TTK_EXPAND | TTK_FILL_BOTH))

/* Treeview -- no border. */
TTK_LAYOUT("Treeview",
    TTK_GROUP("Treeview.field", TTK_FILL_BOTH,
    TTK_GROUP("Treeview.padding", TTK_FILL_BOTH,
    TTK_NODE("Treeview.treearea", TTK_FILL_BOTH))))

/* Tree heading -- no border, fixed height */
TTK_LAYOUT("Heading",
    TTK_NODE("Treeheading.cell", TTK_FILL_BOTH)
    TTK_NODE("Treeheading.image", TTK_PACK_RIGHT)
    TTK_NODE("Treeheading.text", TTK_PACK_TOP))

/* Tree items -- omit focus ring */
TTK_LAYOUT("Item",
    TTK_GROUP("Treeitem.padding", TTK_FILL_BOTH,
    TTK_NODE("Treeitem.indicator", TTK_PACK_LEFT)
    TTK_NODE("Treeitem.image", TTK_PACK_LEFT)
    TTK_NODE("Treeitem.text", TTK_PACK_LEFT)))

/* Scrollbar Layout -- Buttons at the bottom (Snow Leopard and Lion only) */

TTK_LAYOUT("Vertical.TScrollbar",
    TTK_GROUP("Vertical.Scrollbar.trough", TTK_FILL_Y,
    TTK_NODE("Vertical.Scrollbar.thumb",
    TTK_PACK_TOP | TTK_EXPAND | TTK_FILL_BOTH)
    TTK_NODE("Vertical.Scrollbar.downarrow", TTK_PACK_BOTTOM)
    TTK_NODE("Vertical.Scrollbar.uparrow", TTK_PACK_BOTTOM)))

TTK_LAYOUT("Horizontal.TScrollbar",
    TTK_GROUP("Horizontal.Scrollbar.trough", TTK_FILL_X,
    TTK_NODE("Horizontal.Scrollbar.thumb",
    TTK_PACK_LEFT | TTK_EXPAND | TTK_FILL_BOTH)
    TTK_NODE("Horizontal.Scrollbar.rightarrow", TTK_PACK_RIGHT)
    TTK_NODE("Horizontal.Scrollbar.leftarrow", TTK_PACK_RIGHT)))

TTK_END_LAYOUT_TABLE

/*----------------------------------------------------------------------
 * +++ Initialization --
 */

static int AquaTheme_Init(
    Tcl_Interp *interp)
{
    Ttk_Theme themePtr = Ttk_CreateTheme(interp, "aqua", NULL);

    if (!themePtr) {
	return TCL_ERROR;
    }

    /*
     * Elements:
     */

    Ttk_RegisterElementSpec(themePtr, "background", &BackgroundElementSpec,
	0);
    Ttk_RegisterElementSpec(themePtr, "fill", &FillElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "field", &FieldElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "Toolbar.background",
	&ToolbarBackgroundElementSpec, 0);

    Ttk_RegisterElementSpec(themePtr, "Button.button",
	&ButtonElementSpec, &PushButtonParams);
    Ttk_RegisterElementSpec(themePtr, "Checkbutton.button",
	&ButtonElementSpec, &CheckBoxParams);
    Ttk_RegisterElementSpec(themePtr, "Radiobutton.button",
	&ButtonElementSpec, &RadioButtonParams);
    Ttk_RegisterElementSpec(themePtr, "Toolbutton.border",
	&ButtonElementSpec, &BevelButtonParams);
    Ttk_RegisterElementSpec(themePtr, "Menubutton.button",
	&ButtonElementSpec, &PopupButtonParams);
    Ttk_RegisterElementSpec(themePtr, "DisclosureButton.button",
	&ButtonElementSpec, &DisclosureButtonParams);
    Ttk_RegisterElementSpec(themePtr, "HelpButton.button",
	&ButtonElementSpec, &HelpButtonParams);
    Ttk_RegisterElementSpec(themePtr, "GradientButton.button",
	&ButtonElementSpec, &GradientButtonParams);
    Ttk_RegisterElementSpec(themePtr, "Spinbox.uparrow",
	&SpinButtonUpElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "Spinbox.downarrow",
	&SpinButtonDownElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "Combobox.button",
	&ComboboxElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "Treeitem.indicator",
	&DisclosureElementSpec, &DisclosureParams);
    Ttk_RegisterElementSpec(themePtr, "Treeheading.cell",
	&TreeHeaderElementSpec, &ListHeaderParams);

    Ttk_RegisterElementSpec(themePtr, "Treeview.treearea",
	&TreeAreaElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "Notebook.tab", &TabElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "Notebook.client", &PaneElementSpec, 0);

    Ttk_RegisterElementSpec(themePtr, "Labelframe.border", &GroupElementSpec,
	0);
    Ttk_RegisterElementSpec(themePtr, "Entry.field", &EntryElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "Spinbox.field", &EntryElementSpec, 0);

    Ttk_RegisterElementSpec(themePtr, "separator", &SeparatorElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "hseparator", &SeparatorElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "vseparator", &SeparatorElementSpec, 0);

    Ttk_RegisterElementSpec(themePtr, "sizegrip", &SizegripElementSpec, 0);

    /*
     * <<NOTE-TRACKS>>
     * In some themes the Layouts for a progress bar has a trough element and a
     * pbar element.  But in our case the appearance manager draws both parts
     * of the progress bar, so we just have a single element called ".track".
     */

    Ttk_RegisterElementSpec(themePtr, "Progressbar.track", &PbarElementSpec,
	0);

    Ttk_RegisterElementSpec(themePtr, "Scale.trough", &TrackElementSpec,
	&ScaleData);
    Ttk_RegisterElementSpec(themePtr, "Scale.slider", &SliderElementSpec, 0);

    Ttk_RegisterElementSpec(themePtr, "Vertical.Scrollbar.trough",
	&TroughElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "Vertical.Scrollbar.thumb",
	&ThumbElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "Horizontal.Scrollbar.trough",
	&TroughElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "Horizontal.Scrollbar.thumb",
	&ThumbElementSpec, 0);

    /*
     * If we are not in Snow Leopard or Lion the arrows won't actually be
     * displayed.
     */

    Ttk_RegisterElementSpec(themePtr, "Vertical.Scrollbar.uparrow",
	&ArrowElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "Vertical.Scrollbar.downarrow",
	&ArrowElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "Horizontal.Scrollbar.leftarrow",
	&ArrowElementSpec, 0);
    Ttk_RegisterElementSpec(themePtr, "Horizontal.Scrollbar.rightarrow",
	&ArrowElementSpec, 0);

    /*
     * Layouts:
     */

    Ttk_RegisterLayouts(themePtr, LayoutTable);

    Tcl_PkgProvide(interp, "ttk::theme::aqua", TTK_VERSION);
    return TCL_OK;
}

MODULE_SCOPE
int Ttk_MacOSXPlatformInit(
    Tcl_Interp *interp)
{
    return AquaTheme_Init(interp);
}

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
