/*
 * ttkMacOSXTheme.c --
 *
 *      Tk theme engine for Mac OSX, using the Appearance Manager API.
 *
 * Copyright © 2004 Joe English
 * Copyright © 2005 Neil Madden
 * Copyright © 2006-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2008-2009 Apple Inc.
 * Copyright © 2009 Kevin Walzer/WordTech Communications LLC.
 * Copyright © 2019 Marc Culler
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
#include "ttk/ttkThemeInt.h"
#include "ttkMacOSXTheme.h"
#include "tkColor.h"
#include <math.h>

MODULE_SCOPE NSColor *controlAccentColor(void) {
    static int accentPixel = -1;
    if (accentPixel == -1) {
	TkColor *temp = TkpGetColor(NULL, "systemControlAccentColor");
	accentPixel = temp->color.pixel;
	ckfree(temp);
    }
    return TkMacOSXGetNSColor(NULL, accentPixel);
}

/*
 * Values which depend on the OS version.  These are initialized
 * in Ttk_MacOSXInit.
 */

static Ttk_Padding entryElementPadding;
static CGFloat Ttk_ContrastDelta;

/*----------------------------------------------------------------------
 * +++ ComputeButtonDrawInfo --
 *
 *      Fill in an appearance manager HIThemeButtonDrawInfo record
 *      from a Ttk state and the ThemeButtonParams used as the
 *      clientData.
 */

static inline HIThemeButtonDrawInfo ComputeButtonDrawInfo(
    ThemeButtonParams *params,
    Ttk_State state,
    TCL_UNUSED(Tk_Window))
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

/*
 * When we draw simulated Apple widgets we use the Core Graphics framework.
 * Core Graphics uses CGColorRefs, not NSColors.  A CGColorRef must be retained
 * and released explicitly while an NSColor is autoreleased.  In version 10.8
 * of macOS Apple introduced a CGColor property of an NSColor which is guaranteed
 * to be a valid CGColorRef for (approximately) the same color and is released
 * when the NSColor is autoreleased.
 *
 * When building on systems earlier than 10.8 there is no painless way to
 * convert an NSColor to a CGColor. On the other hand, on those systems we use
 * the HIToolbox to draw all widgets, so we never need to call Core Graphics
 * drawing routines directly.  This means that the functions and macros below
 * which construct CGColorRefs can be defined to return nil on systems before
 * 10.8.
 *
 * Similarly, those older systems did not have CGPathCreateWithRoundedRect, but
 * since we never need to draw rounded rectangles on those systems we can just
 * define it to return nil.
 */

static CGColorRef
CGColorFromRGBA(
    CGFloat *rgba)
{
    NSColorSpace *colorSpace = [NSColorSpace sRGBColorSpace];
    NSColor *nscolor = [NSColor colorWithColorSpace: colorSpace
					 components: rgba
					      count: 4];
    return nscolor.CGColor;
}

static CGColorRef
CGColorFromGray(
    GrayColor g)
{
    CGFloat rgba[4] = {g.grayscale, g.grayscale, g.grayscale, g.alpha};
    NSColorSpace *colorSpace = [NSColorSpace sRGBColorSpace];
    NSColor *nscolor = [NSColor colorWithColorSpace: colorSpace
					 components: rgba
					      count: 4];
    return nscolor.CGColor;
}

#define CGCOLOR(nscolor) (nscolor).CGColor

/*----------------------------------------------------------------------
 * +++ Utilities.
 */

/*----------------------------------------------------------------------
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
    MacDrawable *md = (MacDrawable *)d;
    CGRect rect;

    rect.origin.y       = b.y + md->yOff;
    rect.origin.x       = b.x + md->xOff;
    rect.size.height    = b.height;
    rect.size.width     = b.width;

    return rect;
}

/*----------------------------------------------------------------------
 * LookupGrayPalette
 *
 * Retrieves the palette of grayscale colors needed to draw a particular
 * type of button, in a particular state, in light or dark mode.
 *
 */

static GrayPalette LookupGrayPalette(
    const ButtonDesign *design,
    Ttk_State state,
    int isDark)
{
    const PaletteStateTable *entry = design->palettes;
    while ((state & entry->onBits) != entry->onBits ||
	   (~state & entry->offBits) != entry->offBits)
    {
	++entry;
    }
    return isDark ? entry->dark : entry->light;
}

/*----------------------------------------------------------------------
 * NormalizeButtonBounds --
 *
 *      This function returns the actual bounding rectangle that will be used
 *      in drawing the button.
 *
 *      Apple only allows three specific heights for most buttons: regular,
 *      small and mini. We always use the regular size.  However, Ttk may
 *      provide a bounding rectangle with arbitrary height.  We draw the Mac
 *      button centered vertically in the Ttk rectangle, with the same width as
 *      the rectangle.  (But we take care to produce an integer y coordinate,
 *      to avoid unexpected anti-aliasing.)
 *
 *      In addition, the button types which are not known to HIToolbox need some
 *      adjustments to their bounds.
 *
 */

static CGRect NormalizeButtonBounds(
    ThemeButtonParams *params,
    CGRect bounds,
    int isDark)
{
    SInt32 height;

    if (params->heightMetric != NoThemeMetric) {
	ChkErr(GetThemeMetric, params->heightMetric, &height);
	height += 2;
	bounds.origin.y += round(1 + (bounds.size.height - height) / 2);
	bounds.size.height = height;
    }
    switch (params->kind) {
    case TkRoundedRectButton:
	bounds.size.height -= 1;
	break;
    case TkInlineButton:
	bounds.size.height -= 4;
	bounds.origin.y += 1;
	break;
    case TkRecessedButton:
	bounds.size.height -= 2;
	break;
    case TkSidebarButton:
	bounds.size.height += 8;
	break;
    case kThemeRoundButtonHelp:
	if (isDark) {
	    bounds.size.height = bounds.size.width = 22;
	} else {
	    bounds.size.height = bounds.size.width = 22;
	}
	break;
    default:
	break;
    }
    return bounds;
}

/*----------------------------------------------------------------------
 * +++ Background Colors
 *
 * Support for contrasting background colors when GroupBoxes or Tabbed
 * panes are nested inside each other.  Early versions of macOS used ridged
 * borders, so do not need contrasting backgrounds.
 */

/*
 * For systems older than 10.14, [NSColor windowBackgroundColor] generates
 * garbage when called from this function.  In 10.14 it works correctly, and
 * must be used in order to have a background color which responds to Dark
 * Mode.  So we use this hard-wired RGBA color on the older systems which don't
 * support Dark Mode anyway.
 */

RGBACOLOR windowBackground[4] = RGBA256(235.0, 235.0, 235.0, 1.0);

/*----------------------------------------------------------------------
 * GetBackgroundColor --
 *
 *      Fills the array rgba with the color coordinates for a background color.
 *      Start with the background color of a window's container, or the
 *      standard ttk window background if there is no container. If the
 *      contrast parameter is nonzero, modify this color to be darker, for the
 *      aqua appearance, or lighter for the DarkAqua appearance.  This is
 *      primarily used by the Fill and Background elements.  The save parameter
 *      is normally YES, so the contrasting color is saved in the private
 *      data of the widget.  This behavior can be disabled in special cases,
 *      such as when drawing notebook tabs in macOS 11.
 */

static void GetBackgroundColorRGBA(
    TCL_UNUSED(CGContextRef),
    Tk_Window tkwin,
    int contrast,
    Bool save,
    CGFloat *rgba)
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    TkWindow *containerPtr = (TkWindow *) TkGetContainer(tkwin);

    while (containerPtr && containerPtr->privatePtr) {
	if (containerPtr->privatePtr->flags & TTK_HAS_CONTRASTING_BG) {
	    break;
	}
	containerPtr = (TkWindow *)TkGetContainer(containerPtr);
    }
    if (containerPtr && containerPtr->privatePtr) {
	for (int i = 0; i < 4; i++) {
	    rgba[i] = containerPtr->privatePtr->fillRGBA[i];
	}
    } else {
	if ([NSApp macOSVersion] >= 101400) {
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
		rgba[i] += Ttk_ContrastDelta*contrast / 255.0;
	    }
	} else {
	    for (int i = 0; i < 3; i++) {
		rgba[i] -= Ttk_ContrastDelta*contrast / 255.0;
	    }
	}
	if (save && winPtr->privatePtr) {
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
    int contrast,
    Bool save)
{
    CGFloat rgba[4];
    GetBackgroundColorRGBA(context, tkwin, contrast, save, rgba);
    return CGColorFromRGBA(rgba);
}

/*----------------------------------------------------------------------
 * +++ Buttons
 */

/*----------------------------------------------------------------------
 * FillRoundedRectangle --
 *
 *      Fill a rounded rectangle with a specified solid color.
 */

static void FillRoundedRectangle(
    CGContextRef context,
    CGRect bounds,
    CGFloat radius,
    CGColorRef color)
{
    CGPathRef path;
    CHECK_RADIUS(radius, bounds)

    path = CGPathCreateWithRoundedRect(bounds, radius, radius, NULL);
    if (!path) {
	return;
    }
    CGContextSetFillColorWithColor(context, color);
    CGContextBeginPath(context);
    CGContextAddPath(context, path);
    CGContextFillPath(context);
    CFRelease(path);
}

/*----------------------------------------------------------------------
 * FillBorder --
 *
 *      Draw a 1-pixel border around a rounded rectangle using a 3-step
 *      gradient of shades of gray.
 */

static void FillBorder(
    CGContextRef context,
    CGRect bounds,
    GrayPalette palette,
    CGFloat radius)
{
    if (bounds.size.width < 2) {
	return;
    }
    NSColorSpace *sRGB = [NSColorSpace sRGBColorSpace];
    CGPoint end = CGPointMake(bounds.origin.x, bounds.origin.y + bounds.size.height);
    CGFloat corner = (radius > 0 ? radius : 2.0) / bounds.size.height;
    CGFloat locations[4] = {0.0, corner, 1.0 - corner, 1.0};
    CGPathRef path = CGPathCreateWithRoundedRect(bounds, radius, radius, NULL);
    CGFloat colors[16];
    colors[0] = colors[1] = colors[2] = palette.top / 255.0;
    colors[4] = colors[5] = colors[6] = palette.side / 255.0;
    colors[8] = colors[9] = colors[10] = palette.side / 255.0;
    colors[12] = colors[13] = colors[14] = palette.bottom / 255.0;
    colors[3] = colors[7] = colors[11] = colors[15] = 1.0;
    CGGradientRef gradient = CGGradientCreateWithColorComponents(
	 sRGB.CGColorSpace, colors, locations, 4);
    if (!gradient) {
	return;
    }
    CGContextSaveGState(context);
    CGContextBeginPath(context);
    CGContextAddPath(context, path);
    CGContextClip(context);
    CGContextDrawLinearGradient(context, gradient, bounds.origin, end, 0.0);
    CGContextRestoreGState(context);
    CFRelease(path);
    CFRelease(gradient);
}

/*----------------------------------------------------------------------
 * DrawFocusRing --
 *
 *      Draw a 4-pixel wide rounded focus ring enclosing a rounded
 *      rectangle, using the current system accent color.
 */

static void DrawFocusRing(
    CGContextRef context,
    CGRect bounds,
    const ButtonDesign *design)
{
    CGColorRef highlightColor;
    CGFloat highlight[4] = {1.0, 1.0, 1.0, 0.2};
    CGColorRef focusColor;

    focusColor = CGCOLOR([controlAccentColor() colorWithAlphaComponent:0.6]);
    FillRoundedRectangle(context, bounds, design->radius, focusColor);
    bounds = CGRectInset(bounds, 3, 3);
    highlightColor = CGColorFromRGBA(highlight);
    CGContextSetFillColorWithColor(context, highlightColor);
    CGContextFillRect(context, bounds);
}

/*----------------------------------------------------------------------
 * DrawGrayButton --
 *
 *      Draw a button in normal gray colors.
 *
 *      Aqua buttons are normally drawn in a grayscale color.  The buttons,
 *      which are shaped as rounded rectangles have a 1-pixel border which is
 *      drawn in a 3-step gradient and a solid gray face.
 *
 *      Note that this will produce a round button if length = width =
 *      2*radius.
 */

static void DrawGrayButton(
    CGContextRef context,
    CGRect bounds,
    const ButtonDesign *design,
    Ttk_State state,
    Tk_Window tkwin)
{
    int isDark = TkMacOSXInDarkMode(tkwin);
    GrayPalette palette = LookupGrayPalette(design, state, isDark);
    GrayColor faceGray = {.grayscale = 0.0, .alpha = 1.0};
    CGFloat radius = 2 * design->radius <= bounds.size.height ?
	design->radius : bounds.size.height / 2;
    if (palette.top <= 255.0) {
	FillBorder(context, bounds, palette, radius);
    }
    if (palette.face <= 255.0) {
	faceGray.grayscale = palette.face / 255.0;
    } else {

	/*
	 * Color values > 255 are "transparent" which really means that we
	 * fill with the background color.
	 */

	CGFloat rgba[4], gray;
	GetBackgroundColorRGBA(context, tkwin, 0, NO, rgba);
	gray = (rgba[0] + rgba[1] + rgba[2]) / 3.0;
	faceGray.grayscale = gray;
    }
    FillRoundedRectangle(context, CGRectInset(bounds, 1, 1), radius - 1,
			 CGColorFromGray(faceGray));
}

/*----------------------------------------------------------------------
 * DrawAccentedButton --
 *
 *      The accent color is only used when drawing buttons in the active
 *      window.  Push Buttons and segmented Arrow Buttons are drawn in color
 *      when in the pressed state.  Selected Check Buttons, Radio Buttons and
 *      notebook Tabs are also drawn in color.  The color is based on the
 *      user's current choice for the controlAccentColor, but is actually a
 *      linear gradient with a 1-pixel darker line at the top and otherwise
 *      changing from lighter at the top to darker at the bottom.  This
 *      function draws a colored rounded rectangular button.
 */

static void DrawAccentedButton(
    CGContextRef context,
    CGRect bounds,
    const ButtonDesign *design,
    int state,
    int isDark)
{
    NSColorSpace *sRGB = [NSColorSpace sRGBColorSpace];
    CGColorRef faceColor = CGCOLOR(controlAccentColor());
    CGFloat radius = design->radius;
    CGPathRef path = CGPathCreateWithRoundedRect(bounds, radius, radius, NULL);
    // This gradient should only be used for PushButtons and Tabs, and it needs
    // to be lighter at the top.
    static CGFloat components[12] = {1.0, 1.0, 1.0, 0.05,
				     1.0, 1.0, 1.0, 0.2,
				     1.0, 1.0, 1.0, 0.0};
    CGFloat locations[3] = {0.0, 0.05, 1.0};
    CGGradientRef gradient = CGGradientCreateWithColorComponents(
				 sRGB.CGColorSpace, components, locations, 3);
    CGPoint end;
    if (bounds.size.height > 2*radius) {
	bounds.size.height -= 1;
    }
    end = CGPointMake(bounds.origin.x, bounds.origin.y + bounds.size.height);
    CGContextSaveGState(context);
    CGContextBeginPath(context);
    CGContextAddPath(context, path);
    CGContextClip(context);
    FillRoundedRectangle(context, bounds, radius, faceColor);
    CGContextDrawLinearGradient(context, gradient, bounds.origin, end, 0.0);
    if (state & TTK_STATE_PRESSED &&
	state & TTK_STATE_ALTERNATE) {
	CGColorRef color = isDark ?
	    CGColorFromGray(darkPressedDefaultButton) :
	    CGColorFromGray(pressedDefaultButton);
	FillRoundedRectangle(context, bounds, radius, color);
    }
    CGContextRestoreGState(context);
    CFRelease(path);
    CFRelease(gradient);
}

/*----------------------------------------------------------------------
 * DrawAccentedSegment --
 *
 *      Draw the colored ends of widgets like popup buttons and combo buttons.
 */

static void DrawAccentedSegment(
    CGContextRef context,
    CGRect bounds,
    const ButtonDesign *design,
    Ttk_State state,
    Tk_Window tkwin)
{
    /*
     * Clip to the bounds and then draw an accented button which is extended so
     * that the rounded corners on the left will be clipped off.  This assumes
     * that the bounds include room for the focus ring.
     */
    int isDark = TkMacOSXInDarkMode(tkwin);
    GrayColor sepGray = isDark ? darkComboSeparator : lightComboSeparator;
    CGColorRef sepColor = CGColorFromGray(sepGray);
    CGRect clip = bounds;
    clip.size.height += 10;
    bounds.origin.x -= 10;
    bounds.size.width += 10;
    CGPoint separator[2] = {
	CGPointMake(clip.origin.x - 1, bounds.origin.y + 5),
	CGPointMake(clip.origin.x - 1,
		    bounds.origin.y + bounds.size.height - 3)};
    CGContextSaveGState(context);
    CGContextSetStrokeColorWithColor(context, sepColor);
    CGContextSetShouldAntialias(context, false);
    CGContextSetLineWidth(context, 0.5);
    CGContextAddLines(context, separator, 2);
    CGContextStrokePath(context);
    CGContextSetShouldAntialias(context, true);
    if (state & TTK_STATE_FOCUS) {
	CGRect focusClip = clip;
	clip.size.width += 4;
	CGContextClipToRect(context, focusClip);
	bounds = CGRectInset(bounds, 0, 1);
	DrawFocusRing(context, bounds, design);
    }
    bounds = CGRectInset(bounds, 4, 4);
    if (state & TTK_STATE_BACKGROUND) {
	bounds.size.height += 2;
    } else {
	bounds.size.height += 1;
    }
    CGContextClipToRect(context, clip);
    if ((state & TTK_STATE_BACKGROUND) || (state & TTK_STATE_DISABLED)) {
	DrawGrayButton(context, bounds, design, state, tkwin);
    } else {
	DrawAccentedButton(context, bounds, design, state | TTK_STATE_ALTERNATE,
			   isDark);
    }
    CGContextRestoreGState(context);
}

/*----------------------------------------------------------------------
 * +++ Entry boxes
 */

static void DrawEntry(
    CGContextRef context,
    CGRect bounds,
    const ButtonDesign *design,
    int state,
    Tk_Window tkwin)
{
    int isDark = TkMacOSXInDarkMode(tkwin);
    GrayPalette palette = LookupGrayPalette(design, state, isDark);
    CGColorRef backgroundColor;
    CGFloat bgRGBA[4];
    if (isDark) {
	GetBackgroundColorRGBA(context, tkwin, 0, NO, bgRGBA);

	/*
	 * Lighten the entry background to provide contrast.
	 */

	for (int i = 0; i < 3; i++) {
		bgRGBA[i] += 8.0 / 255.0;
	    }
	backgroundColor = CGColorFromRGBA(bgRGBA);
    } else {
	backgroundColor = CG_WHITE;
    }
    if (state & TTK_STATE_FOCUS) {
	DrawFocusRing(context, bounds, design);
    } else {
	FillBorder(context, CGRectInset(bounds,3,3), palette, design->radius);
    }
    bounds = CGRectInset(bounds, 4, 4);
    FillRoundedRectangle(context, bounds, design->radius, backgroundColor);
}

/*----------------------------------------------------------------------
 * +++ Chevrons, CheckMarks, etc. --
 */

static void DrawDownArrow(
    CGContextRef context,
    CGRect bounds,
    CGFloat inset,
    CGFloat size,
    int state)
{
    CGColorRef strokeColor;
    CGFloat x, y;


    if (state & TTK_STATE_DISABLED) {
	strokeColor = CGCOLOR([NSColor disabledControlTextColor]);
    } else if (state & TTK_STATE_IS_ACCENTED) {
	strokeColor = CG_WHITE;
    } else {
	strokeColor = CGCOLOR([NSColor controlTextColor]);
    }
    CGContextSetStrokeColorWithColor(context, strokeColor);
    CGContextSetLineWidth(context, 1.5);
    x = bounds.origin.x + inset;
    y = bounds.origin.y + trunc(bounds.size.height / 2) + 1;
    CGContextBeginPath(context);
    CGPoint arrow[3] = {
	{x, y - size / 4}, {x + size / 2, y + size / 4},
	{x + size, y - size / 4}
    };
    CGContextAddLines(context, arrow, 3);
    CGContextStrokePath(context);
}

/*----------------------------------------------------------------------
 * DrawUpArrow --
 *
 * Draws a single upward pointing arrow for ListHeaders and Disclosure Buttons.
 */

static void DrawUpArrow(
    CGContextRef context,
    CGRect bounds,
    CGFloat inset,
    CGFloat size,
    int state)
{
    NSColor *strokeColor;
    CGFloat x, y;

    if (state & TTK_STATE_DISABLED) {
	strokeColor = [NSColor disabledControlTextColor];
    } else {
	strokeColor = [NSColor controlTextColor];
    }
    CGContextSetStrokeColorWithColor(context, CGCOLOR(strokeColor));
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
 * DrawUpDownArrows --
 *
 * Draws the double arrows used in menu buttons and spin buttons.
 */

static void DrawUpDownArrows(
    CGContextRef context,
    CGRect bounds,
    CGFloat inset,
    CGFloat size,
    CGFloat gap,
    int state,
    ThemeDrawState drawState)
{
    CGFloat x, y;
    NSColor *topStrokeColor, *bottomStrokeColor;
    if (drawState == BOTH_ARROWS && !(state & TTK_STATE_BACKGROUND)) {
	topStrokeColor = bottomStrokeColor = [NSColor whiteColor];
    } else if (drawState == kThemeStatePressedDown) {
	topStrokeColor = [NSColor controlTextColor];
	bottomStrokeColor = [NSColor whiteColor];
    } else if (drawState == kThemeStatePressedUp) {
	topStrokeColor = [NSColor whiteColor];
	bottomStrokeColor = [NSColor controlTextColor];
    } else if (state & TTK_STATE_DISABLED) {
	topStrokeColor = bottomStrokeColor = [NSColor disabledControlTextColor];
    } else {
	topStrokeColor = bottomStrokeColor = [NSColor controlTextColor];
    }
    CGContextSetLineWidth(context, 1.5);
    x = bounds.origin.x + inset;
    y = bounds.origin.y + trunc(bounds.size.height / 2);
    CGContextBeginPath(context);
    CGPoint bottomArrow[3] =
	{{x, y + gap}, {x + size / 2, y + gap + size / 2}, {x + size, y + gap}};
    CGContextAddLines(context, bottomArrow, 3);
    CGContextSetStrokeColorWithColor(context, CGCOLOR(bottomStrokeColor));
    CGContextStrokePath(context);
    CGContextBeginPath(context);
    CGPoint topArrow[3] =
	{{x, y - gap}, {x + size / 2, y - gap - size / 2}, {x + size, y - gap}};
    CGContextAddLines(context, topArrow, 3);
    CGContextSetStrokeColorWithColor(context, CGCOLOR(topStrokeColor));
    CGContextStrokePath(context);
}

/*----------------------------------------------------------------------
 * DrawClosedDisclosure --
 *
 * Draws a disclosure chevron in the Big Sur style, for Treeviews.
 */

static void DrawClosedDisclosure(
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
	{x, y - size / 4 - 1}, {x + size / 2, y}, {x, y + size / 4 + 1}
    };
    CGContextAddLines(context, arrow, 3);
    CGContextStrokePath(context);
}

/*----------------------------------------------------------------------
 * DrawOpenDisclosure --
 *
 * Draws an open disclosure chevron in the Big Sur style, for Treeviews.
 */

static void DrawOpenDisclosure(
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
	{x, y - size / 4}, {x + size / 2, y + size / 2}, {x + size, y - size / 4}
    };
    CGContextAddLines(context, arrow, 3);
    CGContextStrokePath(context);
}

/*----------------------------------------------------------------------
 * IndicatorColor --
 *
 * Returns a CGColorRef of the appropriate shade for a check button or
 * radio button in a given state.
 */

static CGColorRef IndicatorColor(
   int state,
   int isDark)
{
    if (state & TTK_STATE_DISABLED) {
	return isDark ?
	    CGColorFromGray(darkDisabledIndicator) :
	    CGColorFromGray(lightDisabledIndicator);
    } else if ((state & TTK_STATE_SELECTED || state & TTK_STATE_ALTERNATE) &&
	       !(state & TTK_STATE_BACKGROUND)) {
	return CG_WHITE;
    } else {
	return CGCOLOR([NSColor controlTextColor]);
    }
}

/*----------------------------------------------------------------------
 * DrawCheckIndicator --
 *
 * Draws the checkmark or horizontal bar in a check box.
 */

static void DrawCheckIndicator(
    CGContextRef context,
    CGRect bounds,
    int state,
    int isDark)
{
    CGFloat x = bounds.origin.x, y = bounds.origin.y;
    CGColorRef strokeColor = IndicatorColor(state, isDark);

    CGContextSetStrokeColorWithColor(context, strokeColor);
    if (state & TTK_STATE_SELECTED) {
	CGContextSetLineWidth(context, 1.5);
	CGContextBeginPath(context);
	CGPoint check[3] = {{x + 3, y + 7}, {x + 6, y + 10}, {x + 10, y + 3}};
	CGContextAddLines(context, check, 3);
	CGContextStrokePath(context);
    } else if (state & TTK_STATE_ALTERNATE) {
	CGContextSetLineWidth(context, 2.0);
	CGContextBeginPath(context);
	CGPoint bar[2] = {{x + 3, y + 7}, {x + 11, y + 7}};
	CGContextAddLines(context, bar, 2);
	CGContextStrokePath(context);
    }
}

/*----------------------------------------------------------------------
 * DrawRadioIndicator --
 *
 * Draws the dot in the middle of a selected radio button.
 */

static void DrawRadioIndicator(
    CGContextRef context,
    CGRect bounds,
    int state,
    int isDark)
{
    CGFloat x = bounds.origin.x, y = bounds.origin.y;
    CGColorRef fillColor = IndicatorColor(state, isDark);

    CGContextSetFillColorWithColor(context, fillColor);
    if (state & TTK_STATE_SELECTED) {
	CGContextBeginPath(context);
	CGRect dot = {{x + 5, y + 5}, {6, 6}};
	CGContextAddEllipseInRect(context, dot);
	CGContextFillPath(context);
    } else if (state & TTK_STATE_ALTERNATE) {
	CGRect bar = {{x + 4, y + 7}, {8, 2}};
	CGContextFillRect(context, bar);
    }
}

static void
DrawHelpSymbol(
    CGContextRef context,
    CGRect bounds,
    int state)
{
    NSFont *font = [NSFont controlContentFontOfSize:15];
    NSColor *foreground = state & TTK_STATE_DISABLED ?
	[NSColor disabledControlTextColor] : [NSColor controlTextColor];
    NSDictionary *attrs = @{
	NSForegroundColorAttributeName : foreground,
	NSFontAttributeName : font
    };
    NSAttributedString *attributedString = [[NSAttributedString alloc]
						      initWithString:@"?"
							  attributes:attrs];
    CTTypesetterRef typesetter = CTTypesetterCreateWithAttributedString(
	       (CFAttributedStringRef)attributedString);
    CTLineRef line = CTTypesetterCreateLine(typesetter, CFRangeMake(0, 1));
    CGAffineTransform t = CGAffineTransformMake(
			      1.0, 0.0, 0.0, -1.0, 0.0, bounds.size.height);
    CGContextSaveGState(context);
    CGContextSetTextMatrix(context, t);
    CGContextSetTextPosition(context,
			     bounds.origin.x + 6.5,
			     bounds.origin.y + bounds.size.height - 5);
    CTLineDraw(line, context);
    CGContextRestoreGState(context);
    CFRelease(line);
    CFRelease(typesetter);
    [attributedString release];
}



/*----------------------------------------------------------------------
 * +++ Progress bars.
 */

/*----------------------------------------------------------------------
 * DrawProgressBar --
 *
 * Draws a progress bar, with parameters supplied by a HIThemeTrackDrawInfo
 * struct.  Draws a rounded rectangular track overlayed by a colored
 * rounded rectangular indicator.  An indeterminate progress bar is
 * animated.
 */

static void DrawProgressBar(
    CGContextRef context,
    CGRect bounds,
    HIThemeTrackDrawInfo info,
    int state,
    Tk_Window tkwin)
{
    CGRect colorBounds;
    CGFloat rgba[4];
    CGColorRef trackColor, highlightColor, fillColor;
    NSColor *accent;
    CGFloat ratio = (CGFloat) info.value / (CGFloat) (info.max - info.min);

    GetBackgroundColorRGBA(context, tkwin, 0, NO, rgba);

    /*
     * Compute the bounds for the track and indicator.  The track is 6 pixels
     * wide in the center of the widget bounds.
     */

    if (info.attributes & kThemeTrackHorizontal) {
	bounds = CGRectInset(bounds, 1, bounds.size.height / 2 - 3);
	colorBounds = bounds;
	if (info.kind == kThemeIndeterminateBar) {
	    CGFloat width = 0.25*bounds.size.width;
	    CGFloat travel = 0.75*bounds.size.width;
	    CGFloat center = bounds.origin.x + (width / 2) + ratio*travel;
	    colorBounds.origin.x = center - width / 2;
	    colorBounds.size.width = width;
	} else {
	    colorBounds.size.width = ratio*bounds.size.width;
	}
	if (colorBounds.size.width > 0 && colorBounds.size.width < 6) {
	    colorBounds.size.width = 6;
	}
    } else {
	bounds = CGRectInset(bounds, bounds.size.width / 2 - 3, 1);
	colorBounds = bounds;
	if (info.kind == kThemeIndeterminateBar) {
	    CGFloat height = 0.25*bounds.size.height;
	    CGFloat travel = 0.75*bounds.size.height;
	    CGFloat center = bounds.origin.y + (height / 2) + ratio*travel;
	    colorBounds.origin.y = center - height / 2;
	    colorBounds.size.height = height;
	} else {
	    colorBounds.size.height = ratio*(bounds.size.height);
	}
	if (colorBounds.size.height > 0 && colorBounds.size.height < 6) {
	    colorBounds.size.height = 6;
	}
	colorBounds.origin.y += bounds.size.height - colorBounds.size.height;
    }

    /*
     * Compute the colors for the track and indicator.
     */

    if (TkMacOSXInDarkMode(tkwin)) {
	for(int i=0; i < 3; i++) {
	    rgba[i] += 30.0 / 255.0;
	}
	trackColor = CGColorFromRGBA(rgba);
	for(int i=0; i < 3; i++) {
	    rgba[i] -= 5.0 / 255.0;
	}
	highlightColor = CGColorFromRGBA(rgba);
	FillRoundedRectangle(context, bounds, 3, trackColor);
    } else {
	for(int i=0; i < 3; i++) {
	    rgba[i] -= 14.0 / 255.0;
	}
	trackColor = CGColorFromRGBA(rgba);
	for(int i=0; i < 3; i++) {
	    rgba[i] += 3.0 / 255.0;
	}
	highlightColor = CGColorFromRGBA(rgba);
	bounds.size.height -= 1;
	bounds = CGRectInset(bounds, 0, -1);
    }
    if (state & TTK_STATE_BACKGROUND) {
	accent = [NSColor colorWithRed:0.72 green:0.72 blue:0.72 alpha:0.72];
    } else {
	accent = controlAccentColor();
    }

    /*
     * Draw the track, with highlighting around the edge.
     */

    FillRoundedRectangle(context, bounds, 3, trackColor);
    bounds = CGRectInset(bounds, 0, 0.5);
    FillRoundedRectangle(context, bounds, 2.5, highlightColor);
    bounds = CGRectInset(bounds, 0.5, 0.5);
    FillRoundedRectangle(context, bounds, 2, trackColor);
    bounds = CGRectInset(bounds, -0.5, -1);

    /*
     * Draw the indicator.  Make it slightly transparent around the
     * edge so the highlightng shows through.
     */

    if (info.kind == kThemeIndeterminateBar &&
	(state & TTK_STATE_SELECTED) == 0) {
	return;
    }

    fillColor = CGCOLOR([accent colorWithAlphaComponent:0.9]);
    FillRoundedRectangle(context, colorBounds, 3, fillColor);
    colorBounds = CGRectInset(colorBounds, 1, 1);
    fillColor = CGCOLOR([accent colorWithAlphaComponent:1.0]);
    FillRoundedRectangle(context, colorBounds, 2.5, fillColor);
}

/*----------------------------------------------------------------------
 * +++ Sliders.
 */

/*----------------------------------------------------------------------
 * DrawSlider --
 *
 * Draws a slider track and round thumb for a Ttk scale widget.  The accent
 * color is used on the left or top part of the track, so the fraction of
 * the track which is colored is equal to (value - from) / (to - from).
 *
 */

static void DrawSlider(
    CGContextRef context,
    CGRect bounds,
    HIThemeTrackDrawInfo info,
    int state,
    Tk_Window tkwin)
{
    CGColorRef trackColor;
    CGRect clipBounds, trackBounds, thumbBounds;
    CGPoint thumbPoint;
    CGFloat position;
    CGColorRef accentColor;
    Bool fromIsSmaller = info.reserved;
    double from = info.min, to = fabs((double) info.max), value = info.value;

    /*
     * info.min, info.max and info.value are integers.  When this is called
     * we will have arranged that min = 0 and max is a large positive integer.
     */

    double fraction = (from < to) ? (value - from) / (to - from) : 0.5;
    int isDark = TkMacOSXInDarkMode(tkwin);

    if (info.attributes & kThemeTrackHorizontal) {
	trackBounds = CGRectInset(bounds, 0, bounds.size.height / 2 - 3);
	trackBounds.size.height = 3;
	position = 8 + fraction * (trackBounds.size.width - 16);
	clipBounds = trackBounds;
	if (fromIsSmaller) {
	    clipBounds.size.width = position;
	} else {
	    clipBounds.origin.x += position;
	    clipBounds.size.width -= position;
	}
	thumbPoint = CGPointMake(trackBounds.origin.x + position,
				 trackBounds.origin.y + 1);
    } else {
	trackBounds = CGRectInset(bounds, bounds.size.width / 2 - 3, 0);
	trackBounds.size.width = 3;
	position = 8 + fraction * (trackBounds.size.height - 16);
	clipBounds = trackBounds;
	if (fromIsSmaller) {
	    clipBounds.size.height = position;
	} else {
	    clipBounds.origin.y += position;
	    clipBounds.size.height -= position;
	}
	thumbPoint = CGPointMake(trackBounds.origin.x + 1,
				 trackBounds.origin.y + position);
    }
    trackColor = isDark ? CGColorFromGray(darkTrack):
	CGColorFromGray(lightTrack);
    thumbBounds = CGRectMake(thumbPoint.x - 8, thumbPoint.y - 8, 17, 17);
    CGContextSaveGState(context);
    FillRoundedRectangle(context, trackBounds, 1.5, trackColor);
    CGContextClipToRect(context, clipBounds);
    if (state & (TTK_STATE_BACKGROUND | TTK_STATE_DISABLED)) {
	accentColor = isDark ? CGColorFromGray(darkInactiveTrack) :
	    CGColorFromGray(lightInactiveTrack);
    } else {
	accentColor = CGCOLOR(controlAccentColor());
    }
    FillRoundedRectangle(context, trackBounds, 1.5, accentColor);
    CGContextRestoreGState(context);
    DrawGrayButton(context, thumbBounds, &sliderDesign, state, tkwin);
}

/*----------------------------------------------------------------------
 * +++ Drawing procedures for native widgets.
 *
 *      The HIToolbox does not support Dark Mode, and apparently never will.
 *      It also draws some widgets in discontinued older styles even when used
 *      on new OS releases.  So to make widgets look "native" we have to provide
 *      analogues of the HIToolbox drawing functions to be used on newer systems.
 *      We continue to use NIToolbox for older versions of the OS.
 *
 *      Drawing the dark widgets requires NSColors that were introduced in OSX
 *      10.14, so we make some of these functions be no-ops when building on
 *      systems older than 10.14.
 */

/*----------------------------------------------------------------------
 * DrawButton --
 *
 * This is a standalone drawing procedure which draws most types of macOS
 * buttons for newer OS releases.  The button style is specified in the
 * "kind" field of a HIThemeButtonDrawInfo struct, although some of the
 * identifiers are not recognized by HIToolbox.
 */

static void DrawButton(
    CGRect bounds,
    HIThemeButtonDrawInfo info,
    Ttk_State state,
    CGContextRef context,
    Tk_Window tkwin)
{
    ThemeButtonKind kind = info.kind;
    ThemeDrawState drawState = info.state;
    CGRect arrowBounds = bounds = CGRectInset(bounds, 1, 1);
    int hasIndicator, isDark = TkMacOSXInDarkMode(tkwin);

    switch (kind) {
    case TkRoundedRectButton:
	DrawGrayButton(context, bounds, &roundedrectDesign, state, tkwin);
	break;
    case TkInlineButton:
	DrawGrayButton(context, bounds, &inlineDesign, state, tkwin);
	break;
    case TkRecessedButton:
	DrawGrayButton(context, bounds, &recessedDesign, state, tkwin);
	break;
    case TkSidebarButton:
	DrawGrayButton(context, bounds, &sidebarDesign, state, tkwin);
	break;
    case kThemeRoundedBevelButton:
	DrawGrayButton(context, bounds, &bevelDesign, state, tkwin);
	break;
    case kThemePushButton:

	/*
	 * The TTK_STATE_ALTERNATE bit means -default active.  Apple only
	 * indicates the default state (which means that the key equivalent is
	 * "\n") for Push Buttons.
	 */

	if ((state & TTK_STATE_PRESSED || state & TTK_STATE_ALTERNATE) &&
	    !(state & TTK_STATE_BACKGROUND)) {
	    DrawAccentedButton(context, bounds, &pushbuttonDesign, state, isDark);
	} else {
	    DrawGrayButton(context, bounds, &pushbuttonDesign, state, tkwin);
	}
	break;
    case kThemeRoundButtonHelp:
	DrawGrayButton(context, bounds, &helpDesign, state, tkwin);
	DrawHelpSymbol(context, bounds, state);
	break;
    case kThemePopupButton:
	drawState = 0;
	DrawGrayButton(context, bounds, &popupDesign, state, tkwin);
	arrowBounds.size.width = 17;
	arrowBounds.origin.x += bounds.size.width - 17;
	if (!(state & TTK_STATE_BACKGROUND) &&
	    !(state & TTK_STATE_DISABLED)) {
	    CGRect popupBounds = arrowBounds;

	    /*
	     * Allow room for nonexistent focus ring.
	     */

	    popupBounds.size.width += 4;
	    popupBounds.origin.y -= 4;
	    popupBounds.size.height += 8;
	    DrawAccentedSegment(context, popupBounds, &popupDesign, state, tkwin);
	    drawState = BOTH_ARROWS;
	}
	arrowBounds.origin.x += 2;
	DrawUpDownArrows(context, arrowBounds, 3, 7, 2, state, drawState);
	break;
    case kThemeComboBox:
	if (state & TTK_STATE_DISABLED) {
	    // Need to add the disabled case to entryDesign.
	    DrawEntry(context, bounds, &entryDesign, state, tkwin);
	} else {
	    DrawEntry(context, bounds, &entryDesign, state, tkwin);
	}
	arrowBounds.size.width = 17;
	if (state & TTK_STATE_BACKGROUND) {
	    arrowBounds.origin.x += bounds.size.width - 20;
	    arrowBounds.size.width += 4;
	    arrowBounds.origin.y -= 1;
	} else {
	    arrowBounds.origin.y -= 1;
	    arrowBounds.origin.x += bounds.size.width - 20;
	    arrowBounds.size.width += 4;
	    arrowBounds.size.height += 2;
	}
	DrawAccentedSegment(context, arrowBounds, &comboDesign, state, tkwin);
	if (!(state & TTK_STATE_BACKGROUND)) {
	    state |= TTK_STATE_IS_ACCENTED;
	}
	DrawDownArrow(context, arrowBounds, 6, 6, state);
	break;
    case kThemeCheckBox:
	bounds = CGRectOffset(CGRectMake(0, bounds.size.height / 2 - 8, 16, 16),
			      bounds.origin.x, bounds.origin.y);
	bounds = CGRectInset(bounds, 1, 1);
	hasIndicator = state & TTK_STATE_SELECTED || state & TTK_STATE_ALTERNATE;
	if (hasIndicator &&
	    !(state & TTK_STATE_BACKGROUND) &&
	    !(state & TTK_STATE_DISABLED)) {
	    DrawAccentedButton(context, bounds, &checkDesign, 0, isDark);
	} else {
	    DrawGrayButton(context, bounds, &checkDesign, state, tkwin);
	}
	if (hasIndicator) {
	    DrawCheckIndicator(context, bounds, state, isDark);
	}
	break;
    case kThemeRadioButton:
	bounds = CGRectOffset(CGRectMake(0, bounds.size.height / 2 - 9, 18, 18),
					 bounds.origin.x, bounds.origin.y);
	bounds = CGRectInset(bounds, 1, 1);
	hasIndicator = state & TTK_STATE_SELECTED || state & TTK_STATE_ALTERNATE;
	if (hasIndicator &&
	    !(state & TTK_STATE_BACKGROUND) &&
	    !(state & TTK_STATE_DISABLED)) {
	    DrawAccentedButton(context, bounds, &radioDesign, 0, isDark);
	} else {
	    DrawGrayButton(context, bounds, &radioDesign, state, tkwin);
	}
	if (hasIndicator) {
	    DrawRadioIndicator(context, bounds, state, isDark);
	}
	break;
    case kThemeArrowButton:
	DrawGrayButton(context, bounds, &pushbuttonDesign, state, tkwin);
	arrowBounds.origin.x = bounds.origin.x + bounds.size.width - 17;
	arrowBounds.size.width = 16;
	arrowBounds.origin.y -= 1;
	if (state & TTK_STATE_SELECTED) {
	    DrawUpArrow(context, arrowBounds, 5, 6, state);
	} else {
	    DrawDownArrow(context, arrowBounds, 5, 6, state);
	}
	break;
    case kThemeIncDecButton:
	DrawGrayButton(context, bounds, &incdecDesign, state, tkwin);
	if (state & TTK_STATE_PRESSED) {
	    CGRect clip;
	    if (drawState == kThemeStatePressedDown) {
		clip = bounds;
		clip.size.height /= 2;
		clip.origin.y += clip.size.height;
		bounds.size.height += 1;
		clip.size.height += 1;
	    } else {
		clip = bounds;
		clip.size.height /= 2;
	    }
	    CGContextSaveGState(context);
	    CGContextClipToRect(context, clip);
	    DrawAccentedButton(context, bounds, &incdecDesign, 0, isDark);
	    CGContextRestoreGState(context);
	}
	{
	    CGFloat inset = (bounds.size.width - 5) / 2;
	    DrawUpDownArrows(context, bounds, inset, 5, 3, state, drawState);
	}
	break;
    default:
	break;
    }
}

/*----------------------------------------------------------------------
 * DrawGroupBox --
 *
 * This is a standalone drawing procedure which draws the contrasting rounded
 * rectangular box for LabelFrames and Notebook panes used in more recent
 * versions of macOS.  Normally the contrast is set to one, since the nesting
 * level of the Group Box is higher by 1 compared to its container.  But we
 * allow higher contrast for special cases, notably notebook tabs in macOS 11.
 * The save parameter is passed to GetBackgroundColor and should probably be
 * NO in such special cases.
 */

static void DrawGroupBox(
    CGRect bounds,
    CGContextRef context,
    Tk_Window tkwin,
    int contrast,
    Bool save)
{
    CHECK_RADIUS(5, bounds)

    CGPathRef path;
    CGColorRef backgroundColor, borderColor;

    backgroundColor = GetBackgroundCGColor(context, tkwin, contrast, save);
    borderColor = CGColorFromGray(boxBorder);
    CGContextSetFillColorWithColor(context, backgroundColor);
    path = CGPathCreateWithRoundedRect(bounds, 5, 5, NULL);
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
 * DrawListHeader --
 *
 * This is a standalone drawing procedure which draws column headers for a
 * Treeview in the Aqua appearance.  (The HIToolbox headers have not matched the
 * native ones since OSX 10.8)  Note that the header image is ignored, but we
 * draw arrows according to the state.
 */

static void DrawListHeader(
    CGRect bounds,
    CGContextRef context,
    Tk_Window tkwin,
    int state)
{
    int isDark = TkMacOSXInDarkMode(tkwin);
    CGFloat x = bounds.origin.x, y = bounds.origin.y;
    CGFloat w = bounds.size.width, h = bounds.size.height;
    CGPoint top[2] = {{x, y + 1}, {x + w, y + 1}};
    CGPoint bottom[2] = {{x, y + h}, {x + w, y + h}};
    CGPoint separator[2] = {{x + w - 1, y + 3}, {x + w - 1, y + h - 3}};
    CGColorRef strokeColor, backgroundColor;

    /*
     * Apple changes the background color of a list header when the window is
     * not active.  But Ttk does not indicate that in the state of a
     * TreeHeader.  So we have to query the Apple window manager.
     */

    NSWindow *win = TkMacOSXGetNSWindowForDrawable(Tk_WindowId(tkwin));
    if (!isDark) {
	GrayColor bgGray = [win isKeyWindow] ?
	    listheaderActiveBG : listheaderInactiveBG;
	backgroundColor = CGColorFromGray(bgGray);
    }

    CGContextSaveGState(context);
    CGContextSetShouldAntialias(context, false);
    if (!isDark) {
	CGContextBeginPath(context);
	CGContextSetFillColorWithColor(context, backgroundColor);
	CGContextAddRect(context, bounds);
	CGContextFillPath(context);
    }
    strokeColor = isDark ?
	CGColorFromGray(darkListheaderBorder) :
	CGColorFromGray(listheaderSeparator);
    CGContextSetStrokeColorWithColor(context, strokeColor);
    CGContextAddLines(context, separator, 2);
    CGContextStrokePath(context);
    strokeColor = isDark ?
	CGColorFromGray(darkListheaderBorder) :
	CGColorFromGray(lightListheaderBorder);
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
	    DrawUpArrow(context, arrowBounds, 3, 8, state);
	} else if (state & TTK_STATE_SELECTED) {
	    DrawDownArrow(context, arrowBounds, 3, 8, state);
	}
    }
}

/*----------------------------------------------------------------------
 * DrawTab --
 *
 * This is a standalone drawing procedure which draws Tabbed Pane Tabs for the
 * notebook widget.
 */

static void
DrawTab(
    CGRect bounds,
    Ttk_State state,
    CGContextRef context,
    Tk_Window tkwin)
{
    CGRect originalBounds = bounds;
    CGColorRef strokeColor;
    int OSVersion = [NSApp macOSVersion];

    /*
     * Extend the bounds to one or both sides so the rounded part will be
     * clipped off if the right of the left tab, the left of the right tab,
     * and both sides of the middle tabs.
     */

    CGContextClipToRect(context, bounds);
    if (OSVersion < 110000 || !(state & TTK_STATE_SELECTED)) {
	if (!(state & TTK_STATE_FIRST)) {
	    bounds.origin.x -= 10;
	    bounds.size.width += 10;
	}
	if (!(state & TTK_STATE_LAST)) {
	    bounds.size.width += 10;
	}
    }
    /*
     * Fill the tab face with the appropriate color or gradient.  Use a solid
     * color if the tab is not selected, otherwise use the accent color with
     * highlights
     */

    if (!(state & TTK_STATE_SELECTED)) {
	DrawGrayButton(context, bounds, &tabDesign, state, tkwin);

	/*
	 * Draw a separator line on the left side of the tab if it
	 * not first.
	 */

	if (!(state & TTK_STATE_FIRST)) {
	    CGContextSaveGState(context);
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
	 * This is the selected tab; paint it with the current accent color.
	 * If it is first, cover up the separator line drawn by the second one.
	 * (The selected tab is always drawn last.)
	 */

	if ((state & TTK_STATE_FIRST) && !(state & TTK_STATE_LAST)) {
	    bounds.size.width += 1;
	}
	if (!(state & TTK_STATE_BACKGROUND)) {
	    DrawAccentedButton(context, bounds, &tabDesign, 0, 0);
	} else {
	    DrawGrayButton(context, bounds, &tabDesign, state, tkwin);
	}
    }
}

static void
DrawTab11(
    CGRect bounds,
    Ttk_State state,
    CGContextRef context,
    Tk_Window tkwin)
{

    if (state & TTK_STATE_SELECTED) {
	DrawGrayButton(context, bounds, &pushbuttonDesign, state, tkwin);
    } else {
	CGRect clipRect = bounds;
	/*
	 * Draw a segment of a Group Box as a background for non-selected tabs.
	 * Clip the Group Box so that the segments fit together to form a long
	 * rounded rectangle behind the entire tab bar.
	 */

	if (!(state & TTK_STATE_FIRST)) {
	    clipRect.origin.x -= 5;
	    bounds.origin.x -= 5;
	    bounds.size.width += 5;
	}
	if (!(state & TTK_STATE_LAST)) {
	    clipRect.size.width += 5;
	    bounds.size.width += 5;
	}
	CGContextSaveGState(context);
	CGContextClipToRect(context, clipRect);
	DrawGroupBox(bounds, context, tkwin, 3, NO);
	CGContextRestoreGState(context);
    }
}

/*----------------------------------------------------------------------
 * DrawDarkSeparator --
 *
 * This is a standalone drawing procedure which draws a separator widget
 * in Dark Mode.  HIToolbox is used in light mode.
 */

static void DrawDarkSeparator(
    CGRect bounds,
    CGContextRef context,
    TCL_UNUSED(Tk_Window))
{
    CGColorRef sepColor = CGColorFromGray(darkSeparator);

    CGContextSetFillColorWithColor(context, sepColor);
    CGContextFillRect(context, bounds);
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
	if (state & TTK_STATE_DISABLED) {
	    faceGray = darkGradientDisabled;
	    borderGray = darkGradientBorderDisabled;
	} else {
	    faceGray = state & TTK_STATE_PRESSED ?
		darkGradientPressed : darkGradientNormal;
	    borderGray = darkGradientBorder;
	}
    } else {
	if (state & TTK_STATE_DISABLED) {
	    faceGray = lightGradientDisabled;
	    borderGray = lightGradientBorderDisabled;
	} else {
	    faceGray = state & TTK_STATE_PRESSED ?
		lightGradientPressed : lightGradientNormal;
	    borderGray = lightGradientBorder;
	}
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
 * +++ Button elements.
 */

static void ButtonElementMinSize(
    void *clientData,
    int *minWidth,
    int *minHeight)
{
    ThemeButtonParams *params = (ThemeButtonParams *)clientData;

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
    TCL_UNUSED(void *), /* elementRecord */
    Tk_Window tkwin,
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    ThemeButtonParams *params = (ThemeButtonParams *)clientData;
    HIThemeButtonDrawInfo info =
	ComputeButtonDrawInfo(params, 0, tkwin);
    static const CGRect scratchBounds = {{0, 0}, {100, 100}};
    CGRect contentBounds, backgroundBounds;
    int verticalPad;

    ButtonElementMinSize(clientData, minWidth, minHeight);
    switch (info.kind) {
    case TkSidebarButton:
	*paddingPtr = Ttk_MakePadding(30, 10, 30, 10);
	return;
    case TkGradientButton:
	*paddingPtr = Ttk_MakePadding(1, 1, 1, 1);
	/* Fall through. */
    case kThemeArrowButton:
    case kThemeRoundButtonHelp:
	return;
	/* Buttons which are sized like PushButtons but unknown to HITheme. */
    case TkRoundedRectButton:
    case TkRecessedButton:
    case TkInlineButton:
	info.kind = kThemePushButton;
	break;
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
     * shadows only appear on the bottom.  Nonetheless, when HIToolbox is asked
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
    if (info.kind == kThemePopupButton) {
	paddingPtr->top += 1;
	paddingPtr->bottom -= 1;
    }
}

static void ButtonElementDraw(
    void *clientData,
    TCL_UNUSED(void *), /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    ThemeButtonParams *params = (ThemeButtonParams *)clientData;
    CGRect bounds = BoxToRect(d, b);
    HIThemeButtonDrawInfo info = ComputeButtonDrawInfo(params, state, tkwin);
    int isDark = TkMacOSXInDarkMode(tkwin);

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
    case kThemeCheckBox:
    case kThemeRadioButton:
    case TkSidebarButton:
	break;

    /*
     * Other buttons have a maximum height.   We have to deal with that.
     */

    default:
	bounds = NormalizeButtonBounds(params, bounds, isDark);
	break;
    }

    /* We do our own drawing on new systems.*/

    if ([NSApp macOSVersion] > 100800) {
	BEGIN_DRAWING(d)
	DrawButton(bounds, info, state, dc.context, tkwin);
	END_DRAWING
	return;
    }

    /*
     * If execution reaches here it means we should use HIToolbox to draw the
     * button.  Buttons that HIToolbox doesn't know are rendered as
     * PushButtons.
     */

    switch (info.kind) {
    case TkRoundedRectButton:
    case TkRecessedButton:
	info.kind = kThemePushButton;
	break;
    default:
	break;
    }

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

    BEGIN_DRAWING(d)
    if (info.kind == kThemePopupButton  &&
	(state & TTK_STATE_BACKGROUND)) {
	CGRect innerBounds = CGRectInset(bounds, 1, 1);
	FillRoundedRectangle(dc.context, innerBounds, 4, CG_WHITE);
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
    if (info.kind == kThemePushButton) {
	bounds.origin.y -= 2;
    }
    ChkErr(HIThemeDrawButton, &bounds, &info, dc.context, HIOrientation,
	   NULL);
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
static const Ttk_StateTable TabStyleTable[] = {
    {kThemeTabFrontInactive, TTK_STATE_SELECTED | TTK_STATE_BACKGROUND, 0},
    {kThemeTabNonFrontInactive, TTK_STATE_BACKGROUND, 0},
    {kThemeTabFrontUnavailable, TTK_STATE_DISABLED | TTK_STATE_SELECTED, 0},
    {kThemeTabNonFrontUnavailable, TTK_STATE_DISABLED, 0},
    {kThemeTabFront, TTK_STATE_SELECTED, 0},
    {kThemeTabNonFrontPressed, TTK_STATE_PRESSED, 0},
    {kThemeTabNonFront, 0, 0}
};
static const Ttk_StateTable TabAdornmentTable[] = {
    {kHIThemeTabAdornmentNone, TTK_STATE_FIRST | TTK_STATE_LAST, 0},
    {kHIThemeTabAdornmentTrailingSeparator, TTK_STATE_FIRST, 0},
    {kHIThemeTabAdornmentNone, TTK_STATE_LAST, 0},
    {kHIThemeTabAdornmentTrailingSeparator, 0, 0},
};
static const Ttk_StateTable TabPositionTable[] = {
    {kHIThemeTabPositionOnly, TTK_STATE_FIRST | TTK_STATE_LAST, 0},
    {kHIThemeTabPositionFirst, TTK_STATE_FIRST, 0},
    {kHIThemeTabPositionLast, TTK_STATE_LAST, 0},
    {kHIThemeTabPositionMiddle, 0, 0},
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
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    TCL_UNUSED(Tk_Window), /* tkwin */
    TCL_UNUSED(int *),     /* minWidth */
    TCL_UNUSED(int *),     /* minHeight */
    Ttk_Padding *paddingPtr)
{
    *paddingPtr = Ttk_MakePadding(0, -2, 0, 1);
}

static void TabElementDraw(
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    CGRect bounds = BoxToRect(d, b);
    BEGIN_DRAWING(d)
    if ([NSApp macOSVersion] >= 110000) {
	DrawTab11(bounds, state, dc.context, tkwin);
    } else if ([NSApp macOSVersion] > 100800) {
	DrawTab(bounds, state, dc.context, tkwin);
    } else {
	HIThemeTabDrawInfo info = {
	    .version = 1,
	    .style = Ttk_StateTableLookup(TabStyleTable, state),
	    .direction = kThemeTabNorth,
	    .size = kHIThemeTabSizeNormal,
	    .adornment = Ttk_StateTableLookup(TabAdornmentTable, state),
	    .kind = kHIThemeTabKindNormal,
	    .position = Ttk_StateTableLookup(TabPositionTable, state),
	};
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
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    TCL_UNUSED(Tk_Window), /* tkwin */
    TCL_UNUSED(int *),     /* minWidth */
    TCL_UNUSED(int *),     /* minHeight */
    Ttk_Padding *paddingPtr)
{
    *paddingPtr = Ttk_MakePadding(9, 5, 9, 9);
}

static void PaneElementDraw(
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    CGRect bounds = BoxToRect(d, b);

    bounds.origin.y -= kThemeMetricTabFrameOverlap;
    bounds.size.height += kThemeMetricTabFrameOverlap;
    BEGIN_DRAWING(d)
    if ([NSApp macOSVersion] > 100800) {
	DrawGroupBox(bounds, dc.context, tkwin, 1, YES);
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
 */

static void GroupElementSize(
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    TCL_UNUSED(Tk_Window), /* tkwin */
    TCL_UNUSED(int *),     /* minWidth */
    TCL_UNUSED(int *),     /* minHeight */
    Ttk_Padding *paddingPtr)
{
    *paddingPtr = Ttk_MakePadding(0, 0, 0, 0);
}

static void GroupElementDraw(
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    CGRect bounds = BoxToRect(d, b);

    BEGIN_DRAWING(d)
    if ([NSApp macOSVersion] > 100800) {
	DrawGroupBox(bounds, dc.context, tkwin, 1, YES);
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
     offsetof(EntryElement, backgroundObj), ENTRY_DEFAULT_BACKGROUND},
    {"-fieldbackground", TK_OPTION_BORDER,
     offsetof(EntryElement, fieldbackgroundObj), ENTRY_DEFAULT_BACKGROUND},
    {NULL, TK_OPTION_BOOLEAN, 0, NULL}
};

static void EntryElementSize(
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    TCL_UNUSED(Tk_Window), /* tkwin */
    TCL_UNUSED(int *),     /* minWidth */
    TCL_UNUSED(int *),     /* minHeight */
    Ttk_Padding *paddingPtr)
{
    *paddingPtr = entryElementPadding;
}

static void EntryElementDraw(
    void *clientData,
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    EntryElement *e = (EntryElement *)elementRecord;
    ThemeFrameParams *params = (ThemeFrameParams *)clientData;
    HIThemeFrameKind kind = params ? params->kind :
	kHIThemeFrameTextFieldSquare;
    CGRect bounds = BoxToRect(d, b);
    CGColorRef background;
    Tk_3DBorder backgroundPtr = NULL;
    static const char *defaultBG = ENTRY_DEFAULT_BACKGROUND;

    if ([NSApp macOSVersion] > 100800) {
	BEGIN_DRAWING(d)
	    switch(kind) {
	    case kHIThemeFrameTextFieldRound:
		DrawEntry(dc.context, bounds, &searchDesign, state, tkwin);
		break;
	    case kHIThemeFrameTextFieldSquare:
		DrawEntry(dc.context, bounds, &entryDesign, state, tkwin);
		break;
	    default:
		return;
	    }
	END_DRAWING
    } else {
	const HIThemeFrameDrawInfo info = {
	    .version = 0,
	    .kind = params->kind,
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
		b.x, b.y, b.width, b.height);
	}
	BEGIN_DRAWING(d)
	if (backgroundPtr == NULL) {
	    if ([NSApp macOSVersion] > 100800) {
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

// OS dependent ???
static Ttk_Padding ComboboxPadding = {7, 5, 24, 5};

static void ComboboxElementSize(
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    TCL_UNUSED(Tk_Window), /* tkwin */
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    *minWidth = 24;
    *minHeight = 0;
    *paddingPtr = ComboboxPadding;
}

static void ComboboxElementDraw(
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
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
    if ([NSApp macOSVersion] > 100800) {
	bounds = CGRectInset(bounds, -1, -1);
	DrawButton(bounds, info, state, dc.context, tkwin);
    } else {
	bounds.origin.y += 1;
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

static Ttk_Padding SpinbuttonMargins = {2, 0, 0, 0};

static void SpinButtonReBounds(
    Tk_Window tkwin,
    CGRect *bounds)
{
    if (TkMacOSXInDarkMode(tkwin)) {
	bounds->origin.x -= 2;
	bounds->origin.y += 1;
	bounds->size.height -= 0.5;
    } else {
	bounds->origin.x -= 3;
	bounds->origin.y += 1;
	bounds->size.width += 1;
    }
}

static void SpinButtonElementSize(
    TCL_UNUSED(void *),       /* clientdata */
    TCL_UNUSED(void *),       /* elementRecord */
    TCL_UNUSED(Tk_Window),    /* tkwin */
    int *minWidth,
    int *minHeight,
    TCL_UNUSED(Ttk_Padding *)) /* PaddingPtr */
{
    SInt32 s;

    ChkErr(GetThemeMetric, kThemeMetricLittleArrowsWidth, &s);
    *minWidth = s + Ttk_PaddingWidth(SpinbuttonMargins);
    ChkErr(GetThemeMetric, kThemeMetricLittleArrowsHeight, &s);
    *minHeight = 2 + (s + Ttk_PaddingHeight(SpinbuttonMargins)) / 2;
}

static void SpinButtonUpElementDraw(
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    CGRect bounds = BoxToRect(d, Ttk_PadBox(b, SpinbuttonMargins));
    int infoState;

    SpinButtonReBounds(tkwin, &bounds);
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
    if ([NSApp macOSVersion] > 100800) {
	DrawButton(bounds, info, state, dc.context, tkwin);
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
    SpinButtonElementSize,
    SpinButtonUpElementDraw
};

static void SpinButtonDownElementDraw(
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    CGRect bounds = BoxToRect(d, Ttk_PadBox(b, SpinbuttonMargins));
    int infoState = 0;

    SpinButtonReBounds(tkwin, &bounds);
    bounds.origin.y -= bounds.size.height;
    bounds.size.height += bounds.size.height;
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
    if ([NSApp macOSVersion] > 100800) {
	DrawButton(bounds, info, state, dc.context, tkwin);
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
    SpinButtonElementSize,
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

static const Ttk_StateTable ThemeTrackEnableTable[] = {
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
    {"-from", TK_OPTION_DOUBLE, offsetof(TrackElement, fromObj), NULL},
    {"-to", TK_OPTION_DOUBLE, offsetof(TrackElement, toObj), NULL},
    {"-value", TK_OPTION_DOUBLE, offsetof(TrackElement, valueObj), NULL},
    {"-orient", TK_OPTION_STRING, offsetof(TrackElement, orientObj), NULL},
    {NULL, TK_OPTION_BOOLEAN, 0, NULL}
};
static void TrackElementSize(
    void *clientData,
    TCL_UNUSED(void *),       /* elementRecord */
    TCL_UNUSED(Tk_Window),    /* tkwin */
    int *minWidth,
    int *minHeight,
    TCL_UNUSED(Ttk_Padding *)) /* paddingPtr */
{
    TrackElementData *data = (TrackElementData *)clientData;
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
    TrackElementData *data = (TrackElementData *)clientData;
    TrackElement *elem = (TrackElement *)elementRecord;
    Ttk_Orient orientation = TTK_ORIENT_HORIZONTAL;
    double from = 0, to = 100, value = 0, fraction, max;
    CGRect bounds = BoxToRect(d, b);

    Ttk_GetOrientFromObj(NULL, elem->orientObj, &orientation);
    Tcl_GetDoubleFromObj(NULL, elem->fromObj, &from);
    Tcl_GetDoubleFromObj(NULL, elem->toObj, &to);
    Tcl_GetDoubleFromObj(NULL, elem->valueObj, &value);

    fraction = (value - from) / (to - from);
    max = RangeToFactor(fabs(to - from));
    HIThemeTrackDrawInfo info = {
	.version = 0,
	.kind = data->kind,
	.bounds = bounds,
	.min = 0,
	.max = max,
	.value = fraction * max,
	.attributes = kThemeTrackShowThumb |
	    (orientation == TTK_ORIENT_HORIZONTAL ?
	    kThemeTrackHorizontal : 0),
	.enableState = Ttk_StateTableLookup(ThemeTrackEnableTable, state),
	.trackInfo.progress.phase = 0
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
    if (([NSApp macOSVersion] > 100800) && !(state & TTK_STATE_ALTERNATE)) {

	/*
	 * We use the reserved field to indicate whether "from" is less than
	 * "to".  It should be 0 if passing the info to HIThemeDrawInfo, but
	 * we aren't doing that.
	 */

	info.reserved = (from < to);
	DrawSlider(dc.context, bounds, info, state, tkwin);
    } else {
	ChkErr(HIThemeDrawTrack, &info, NULL, dc.context, HIOrientation);
    }
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
    TCL_UNUSED(void *),        /* clientData */
    TCL_UNUSED(void *),        /* elementRecord */
    TCL_UNUSED(Tk_Window),     /* tkwin */
    int *minWidth,
    int *minHeight,
    TCL_UNUSED(Ttk_Padding *)) /* paddingPtr */
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
     offsetof(PbarElement, orientObj), "horizontal"},
    {"-value", TK_OPTION_DOUBLE,
     offsetof(PbarElement, valueObj), "0.0"},
    {"-maximum", TK_OPTION_DOUBLE,
     offsetof(PbarElement, maximumObj), "100.0"},
    {"-phase", TK_OPTION_INT,
     offsetof(PbarElement, phaseObj), "0"},
    {"-mode", TK_OPTION_STRING,
     offsetof(PbarElement, modeObj), "determinate"},
    {NULL, TK_OPTION_BOOLEAN, 0, NULL}
};
static void PbarElementSize(
    TCL_UNUSED(void *),        /* clientData */
    TCL_UNUSED(void *),        /* elementRecord */
    TCL_UNUSED(Tk_Window),     /* tkwin */
    int *minWidth,
    int *minHeight,
    TCL_UNUSED(Ttk_Padding *)) /* paddingPtr */
{
    SInt32 size = 24;           /* @@@ Check HIG for correct default */

    ChkErr(GetThemeMetric, kThemeMetricLargeProgressBarThickness, &size);
    *minWidth = *minHeight = size;
}

static void PbarElementDraw(
    TCL_UNUSED(void *),    /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    PbarElement *pbar = (PbarElement *)elementRecord;
    Ttk_Orient orientation = TTK_ORIENT_HORIZONTAL;
    int phase;
    double value = 0, maximum = 100, factor;
    CGRect bounds = BoxToRect(d, b);
    int isIndeterminate = !strcmp("indeterminate",
				  Tcl_GetString(pbar->modeObj));

    Ttk_GetOrientFromObj(NULL, pbar->orientObj, &orientation);
    Tcl_GetDoubleFromObj(NULL, pbar->valueObj, &value);
    Tcl_GetDoubleFromObj(NULL, pbar->maximumObj, &maximum);
    Tcl_GetIntFromObj(NULL, pbar->phaseObj, &phase);

    if (isIndeterminate) {

	/*
	 * When an indeterminate progress bar is animated the phase is
	 * (currently) always 0 and the value increases from min to max
	 * and then decreases back to min.  We scale the value by 3 to
	 * speed the animation up a bit.
	 */

	double remainder = fmod(3*value, 2*maximum);
	value = remainder > maximum ? 2*maximum - remainder : remainder;
    }
    factor = RangeToFactor(maximum);
    HIThemeTrackDrawInfo info = {
	.version = 0,
	.kind = isIndeterminate? kThemeIndeterminateBar : kThemeProgressBar,
	.bounds = bounds,
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
    if ([NSApp macOSVersion] > 100800) {
	DrawProgressBar(dc.context, bounds, info, state, tkwin);
    } else {
	ChkErr(HIThemeDrawTrack, &info, NULL, dc.context, HIOrientation);
    }
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
     offsetof(ScrollbarElement, orientObj), "horizontal"},
    {NULL, TK_OPTION_BOOLEAN, 0, NULL}
};
static void TroughElementSize(
    TCL_UNUSED(void *),    /* clientData */
    void *elementRecord,
    TCL_UNUSED(Tk_Window), /* tkwin */
    int *minWidth,
    int *minHeight,
    Ttk_Padding *paddingPtr)
{
    ScrollbarElement *scrollbar = (ScrollbarElement *)elementRecord;
    Ttk_Orient orientation = TTK_ORIENT_HORIZONTAL;
    SInt32 thickness = 15;

    Ttk_GetOrientFromObj(NULL, scrollbar->orientObj, &orientation);
    ChkErr(GetThemeMetric, kThemeMetricScrollBarWidth, &thickness);
    if (orientation == TTK_ORIENT_HORIZONTAL) {
	*minHeight = thickness;
	if ([NSApp macOSVersion] > 100700) {
	    *paddingPtr = Ttk_MakePadding(4, 4, 4, 3);
	}
    } else {
	*minWidth = thickness;
	if ([NSApp macOSVersion] > 100700) {
	    *paddingPtr = Ttk_MakePadding(4, 4, 3, 4);
	}
    }
}

static void TroughElementDraw(
    TCL_UNUSED(void *),    /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State)) /* state */
{
    ScrollbarElement *scrollbar = (ScrollbarElement *)elementRecord;
    Ttk_Orient orientation = TTK_ORIENT_HORIZONTAL;
    CGRect bounds = BoxToRect(d, b);
    GrayColor bgGray;

    Ttk_GetOrientFromObj(NULL, scrollbar->orientObj, &orientation);
    if (orientation == TTK_ORIENT_HORIZONTAL) {
	bounds = CGRectInset(bounds, 0, 1);
    } else {
	bounds = CGRectInset(bounds, 1, 0);
    }
    BEGIN_DRAWING(d)
    if ([NSApp macOSVersion] > 100800) {
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
    TCL_UNUSED(void *),        /* clientData */
    void *elementRecord,
    TCL_UNUSED(Tk_Window),     /* tkwin */
    int *minWidth,
    int *minHeight,
    TCL_UNUSED(Ttk_Padding *)) /* paddingPtr */
{
    ScrollbarElement *scrollbar = (ScrollbarElement *)elementRecord;
    Ttk_Orient orientation = TTK_ORIENT_HORIZONTAL;

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
    TCL_UNUSED(void *),        /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    ScrollbarElement *scrollbar = (ScrollbarElement *)elementRecord;
    Ttk_Orient orientation = TTK_ORIENT_HORIZONTAL;

    Ttk_GetOrientFromObj(NULL, scrollbar->orientObj, &orientation);

    /*
     * In order to make ttk scrollbars work correctly it is necessary to be
     * able to display the thumb element at the size and location which the ttk
     * scrollbar widget requests.  The algorithm that HIToolbox uses to
     * determine the thumb geometry from the input values of min, max, value
     * and viewSize is undocumented.  A seemingly natural algorithm is
     * implemented below.  This code uses that algorithm for older OS versions,
     * because using HIToolbox also handles drawing the buttons and 3D thumb used
     * on those systems.  For newer systems the cleanest approach is to just
     * draw the thumb directly.
     */

    if ([NSApp macOSVersion] > 100800) {
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
	FillRoundedRectangle(dc.context, thumbBounds, 4, thumbColor);
	END_DRAWING
    } else {
	double thumbSize, trackSize, visibleSize, factor, fraction;
	MacDrawable *macWin = (MacDrawable *)Tk_WindowId(tkwin);
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
	if ([NSApp macOSVersion] < 100800 ||
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
    TCL_UNUSED(void *),        /* clientData */
    TCL_UNUSED(void *),        /* elementRecord */
    TCL_UNUSED(Tk_Window),     /* tkwin */
    int *minWidth,
    int *minHeight,
    TCL_UNUSED(Ttk_Padding *)) /* paddingPtr */
{
    if ([NSApp macOSVersion] < 100800) {
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
    TCL_UNUSED(void *),       /* clientData */
    TCL_UNUSED(void *),       /* elementRecord */
    TCL_UNUSED(Tk_Window),    /* tkwin */
    int *minWidth,
    int *minHeight,
    TCL_UNUSED(Ttk_Padding *)) /* paddingPtr */
{
    *minWidth = *minHeight = 1;
}

static void SeparatorElementDraw(
    TCL_UNUSED(void *),       /* clientData */
    TCL_UNUSED(void *),       /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
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
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    TCL_UNUSED(Tk_Window), /* tkwin */
    int *minWidth,
    int *minHeight,
    TCL_UNUSED(Ttk_Padding *)) /* paddingPtr */
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
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    TCL_UNUSED(Tk_Window), /* tkwin */
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
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
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    CGRect bounds = BoxToRect(d, b);

    if ([NSApp macOSVersion] > 100800) {
	CGColorRef bgColor;
	BEGIN_DRAWING(d)
	bgColor = GetBackgroundCGColor(dc.context, tkwin, NO, 0);
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
    TCL_UNUSED(Ttk_Box),
    Ttk_State state)
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
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    TCL_UNUSED(Ttk_Box),
    TCL_UNUSED(Ttk_State))
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
 *      except that the fieldbackground color is configurable.
 */

typedef struct {
    Tcl_Obj     *backgroundObj;
} FieldElement;

static Ttk_ElementOptionSpec FieldElementOptions[] = {
    {"-fieldbackground", TK_OPTION_BORDER,
     offsetof(FieldElement, backgroundObj), "white"},
    {NULL, TK_OPTION_BOOLEAN, 0, NULL}
};

static void FieldElementDraw(
    TCL_UNUSED(void *),    /* clientData */
    void *elementRecord,
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    TCL_UNUSED(Ttk_State))
{
    FieldElement *e = (FieldElement *)elementRecord;
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
 *    On systems older than 10.9 the header is a kThemeListHeaderButton drawn
 *    by HIToolbox.  On newer systems those buttons do not match the Apple
 *    buttons, so we draw them from scratch.
 */

static const Ttk_StateTable TreeHeaderValueTable[] = {
    {kThemeButtonOn, TTK_STATE_ALTERNATE, 0},
    {kThemeButtonOn, TTK_STATE_SELECTED, 0},
    {kThemeButtonOff, 0, 0}
};

static const Ttk_StateTable TreeHeaderAdornmentTable[] = {
    {kThemeAdornmentHeaderButtonSortUp,
     TTK_STATE_ALTERNATE | TTK_TREEVIEW_STATE_SORTARROW, 0},
    {kThemeAdornmentDefault,
     TTK_STATE_SELECTED | TTK_TREEVIEW_STATE_SORTARROW, 0},
    {kThemeAdornmentHeaderButtonNoSortArrow, TTK_STATE_ALTERNATE, 0},
    {kThemeAdornmentHeaderButtonNoSortArrow, TTK_STATE_SELECTED, 0},
    {kThemeAdornmentFocus, TTK_STATE_FOCUS, 0},
    {kThemeAdornmentNone, 0, 0}
};

static void TreeAreaElementSize (
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    TCL_UNUSED(Tk_Window), /* tkwin */
    TCL_UNUSED(int *),     /* minWidth */
    TCL_UNUSED(int *),     /* minHeight */
    Ttk_Padding *paddingPtr)
{

    /*
     * Padding is needed to get the heading text to align correctly, since the
     * widget expects the heading to be the same height as a row.
     */

    if ([NSApp macOSVersion] > 100800) {
	*paddingPtr = Ttk_MakePadding(0, 4, 0, 0);
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
    if ([NSApp macOSVersion] > 100800) {
	*minHeight = 24;
    } else {
	ButtonElementSize(clientData, elementRecord, tkwin, minWidth,
	    minHeight, paddingPtr);
    }
}

static void TreeHeaderElementDraw(
    void *clientData,
    TCL_UNUSED(void *),    /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    ThemeButtonParams *params = (ThemeButtonParams *)clientData;
    CGRect bounds = BoxToRect(d, b);
    const HIThemeButtonDrawInfo info = {
	.version = 0,
	.state = Ttk_StateTableLookup(ThemeStateTable, state),
	.kind = params->kind,
	.value = Ttk_StateTableLookup(TreeHeaderValueTable, state),
	.adornment = Ttk_StateTableLookup(TreeHeaderAdornmentTable, state),
    };

    BEGIN_DRAWING(d)
    if ([NSApp macOSVersion] > 100800) {

	/*
	 * Compensate for the padding added in TreeHeaderElementSize, so
	 * the larger heading will be drawn at the top of the widget.
	 */

	bounds.origin.y -= 4;
	DrawListHeader(bounds, dc.context, tkwin, state);
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

static const Ttk_StateTable DisclosureValueTable[] = {
    {kThemeDisclosureDown, TTK_STATE_OPEN, 0},
    {kThemeDisclosureRight, 0, 0},
};
static void DisclosureElementSize(
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    TCL_UNUSED(Tk_Window), /* tkwin */
    int *minWidth,
    int *minHeight,
    TCL_UNUSED(Ttk_Padding *)) /* paddingPtr */
{
    SInt32 s;

    ChkErr(GetThemeMetric, kThemeMetricDisclosureTriangleWidth, &s);
    *minWidth = s;
    ChkErr(GetThemeMetric, kThemeMetricDisclosureTriangleHeight, &s);
    *minHeight = s;
}

static void DisclosureElementDraw(
    TCL_UNUSED(void *),    /* clientData */
    TCL_UNUSED(void *),    /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    if (!(state & TTK_STATE_LEAF)) {
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
	if ([NSApp macOSVersion] >= 110000) {
	    CGFloat rgba[4];
	    NSColorSpace *deviceRGB = [NSColorSpace deviceRGBColorSpace];
	    NSColor *stroke = [[NSColor textColor]
		colorUsingColorSpace: deviceRGB];
	    [stroke getComponents: rgba];
	    if (state & TTK_STATE_OPEN) {
		DrawOpenDisclosure(dc.context, bounds, 2, 8, rgba);
	    } else {
		DrawClosedDisclosure(dc.context, bounds, 2, 12, rgba);
	    }
	} else {
	    ChkErr(HIThemeDrawButton, &bounds, &info, dc.context, HIOrientation,
	    NULL);
	}
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

/* Inline Button */
TTK_LAYOUT("InlineButton",
    TTK_GROUP("InlineButton.button", TTK_FILL_BOTH,
    TTK_GROUP("Button.padding", TTK_FILL_BOTH,
    TTK_NODE("Button.label", TTK_FILL_BOTH))))

/* Rounded Rect Button -- transparent face */
TTK_LAYOUT("RoundedRectButton",
    TTK_GROUP("RoundedRectButton.button", TTK_FILL_BOTH,
    TTK_GROUP("Button.padding", TTK_FILL_BOTH,
    TTK_NODE("Button.label", TTK_FILL_BOTH))))

/* Gradient Button */
TTK_LAYOUT("GradientButton",
    TTK_GROUP("GradientButton.button", TTK_FILL_BOTH,
    TTK_GROUP("Button.padding", TTK_FILL_BOTH,
    TTK_NODE("Button.label", TTK_FILL_BOTH))))

/* Recessed Button - text only radio button */

TTK_LAYOUT("RecessedButton",
    TTK_GROUP("RecessedButton.button", TTK_FILL_BOTH,
    TTK_GROUP("Button.padding", TTK_FILL_BOTH,
    TTK_NODE("Button.label", TTK_FILL_BOTH))))

/* Sidebar Button - text only radio button for sidebars */

TTK_LAYOUT("SidebarButton",
    TTK_GROUP("SidebarButton.button", TTK_FILL_BOTH,
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
    TTK_GROUP("Notebook.padding", TTK_FILL_BOTH,
    TTK_NODE("Notebook.label", TTK_FILL_BOTH))))

/* Spinbox -- buttons 2px to the right of the field. */
TTK_LAYOUT("TSpinbox",
    TTK_GROUP("Spinbox.buttons", TTK_PACK_RIGHT,
    TTK_NODE("Spinbox.uparrow", TTK_PACK_TOP | TTK_STICK_E)
    TTK_NODE("Spinbox.downarrow", TTK_PACK_BOTTOM | TTK_STICK_E))
    TTK_GROUP("Spinbox.field", TTK_FILL_X,
    TTK_NODE("Spinbox.textarea", TTK_FILL_X)))

TTK_LAYOUT("TEntry",
    TTK_GROUP("Entry.field", TTK_FILL_BOTH|TTK_BORDER,
	TTK_GROUP("Entry.padding", TTK_FILL_BOTH,
	    TTK_NODE("Entry.textarea", TTK_FILL_BOTH))))

/* Searchbox */
TTK_LAYOUT("Searchbox",
    TTK_GROUP("Searchbox.field", TTK_FILL_BOTH|TTK_BORDER,
	TTK_GROUP("Entry.padding", TTK_FILL_BOTH,
	    TTK_NODE("Entry.textarea", TTK_FILL_BOTH))))

/* Progress bars -- track only */
TTK_LAYOUT("TProgressbar",
    TTK_NODE("Progressbar.track", TTK_FILL_BOTH))

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
    TTK_NODE("Vertical.Scrollbar.thumb", TTK_FILL_BOTH)
    TTK_NODE("Vertical.Scrollbar.downarrow", TTK_PACK_BOTTOM)
    TTK_NODE("Vertical.Scrollbar.uparrow", TTK_PACK_BOTTOM)))

TTK_LAYOUT("Horizontal.TScrollbar",
    TTK_GROUP("Horizontal.Scrollbar.trough", TTK_FILL_X,
    TTK_NODE("Horizontal.Scrollbar.thumb", TTK_FILL_BOTH)
    TTK_NODE("Horizontal.Scrollbar.rightarrow", TTK_PACK_RIGHT)
    TTK_NODE("Horizontal.Scrollbar.leftarrow", TTK_PACK_RIGHT)))

TTK_END_LAYOUT_TABLE

/*----------------------------------------------------------------------
 * +++ Initialization --
 */

/*----------------------------------------------------------------------
 * +++ Ttk_MacOSXInit --
 *
 *    Initialize variables which depend on [NSApp macOSVersion].  Called from
 *    [NSApp applicationDidFinishLaunching].
 */

MODULE_SCOPE void
Ttk_MacOSXInit(void)
{
    if ([NSApp macOSVersion] < 101400) {
	entryElementPadding = Ttk_MakePadding(7, 6, 7, 5);
    } else {
	entryElementPadding = Ttk_MakePadding(7, 5, 7, 6);
    }
    if ([NSApp macOSVersion] < 110000) {
	Ttk_ContrastDelta = 8.0;
    } else {

	/*
	 * The subtle contrast became event more subtle in 11.0.
	 */

	Ttk_ContrastDelta = 5.0;
    }
}

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
    Ttk_RegisterElementSpec(themePtr, "InlineButton.button",
	&ButtonElementSpec, &InlineButtonParams);
    Ttk_RegisterElementSpec(themePtr, "RoundedRectButton.button",
	&ButtonElementSpec, &RoundedRectButtonParams);
    Ttk_RegisterElementSpec(themePtr, "Checkbutton.button",
	&ButtonElementSpec, &CheckBoxParams);
    Ttk_RegisterElementSpec(themePtr, "Radiobutton.button",
	&ButtonElementSpec, &RadioButtonParams);
    Ttk_RegisterElementSpec(themePtr, "RecessedButton.button",
	&ButtonElementSpec, &RecessedButtonParams);
    Ttk_RegisterElementSpec(themePtr, "SidebarButton.button",
	&ButtonElementSpec, &SidebarButtonParams);
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
    Ttk_RegisterElementSpec(themePtr, "Entry.field", &EntryElementSpec,
			    &EntryFieldParams);
    Ttk_RegisterElementSpec(themePtr, "Searchbox.field", &EntryElementSpec,
			    &SearchboxFieldParams);
    Ttk_RegisterElementSpec(themePtr, "Spinbox.field", &EntryElementSpec,
			    &EntryFieldParams);

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

MODULE_SCOPE int
Ttk_MacOSXPlatformInit(
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
