/*
 * tkMacOSXColor.c --
 *
 *	This file maintains a database of color values for the Tk
 *	toolkit, in order to avoid round-trips to the server to
 *	map color names to pixel values.
 *
 * Copyright (c) 1990-1994 The Regents of the University of California.
 * Copyright (c) 1994-1996 Sun Microsystems, Inc.
 * Copyright 2001-2009, Apple Inc.
 * Copyright (c) 2006-2009 Daniel A. Steffen <das@users.sourceforge.net>
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkColor.h"
#include "tkMacOSXColor.h"

static Tcl_HashTable systemColorMap;
static int systemColorMapSize;
SystemColorMapEntry **systemColorIndex;

void initColorTable()
{
    Tcl_InitHashTable(&systemColorMap, TCL_STRING_KEYS);
    SystemColorMapEntry *entry, *oldEntry;
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    int newPtr, index = 0;

    /*
     * Build a hash table for looking up a color by its name.
     */
    
    for (entry = systemColorMapData; entry->name != NULL; entry++) {
	hPtr = Tcl_CreateHashEntry(&systemColorMap, entry->name, &newPtr);
	if (entry->type == semantic) {
	    NSString *selector = [[NSString alloc]
				   initWithCString:entry->macName
					  encoding:NSUTF8StringEncoding];
	    /*
	     * Ignore this entry if NSColor does not recognize it.
	     */
	    
	    if (![NSColor respondsToSelector: NSSelectorFromString(selector)]) {
		continue;
	    }
	    [selector retain];
	    entry->selector = selector;
	}
	if (newPtr == 0) {
	    oldEntry = (SystemColorMapEntry *) Tcl_GetHashValue(hPtr);
	    entry->index = oldEntry->index;
	} else {
	    entry->index = index++;
	}
	Tcl_SetHashValue(hPtr, entry);
    }
    
    /*
     * Build an array for looking up a color by its index.
     */
    
    systemColorMapSize = index;
    systemColorIndex = ckalloc(systemColorMapSize * sizeof(SystemColorMapEntry*));
    for (hPtr = Tcl_FirstHashEntry(&systemColorMap, &search); hPtr != NULL;
	 hPtr = Tcl_NextHashEntry(&search)) {
	entry = (SystemColorMapEntry *) Tcl_GetHashValue(hPtr);
	systemColorIndex[entry->index] = entry;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXRGBPixel --
 *
 *	Return an unsigned long value suitable for use in the pixel
 *	field of an XColor with the specified red, green and blue
 *	intensities.  The inputs are cast as unsigned longs but are
 *      expected to have values representable by an unsigned short
 *      as used in the XColor struct.  These values are divided by
 *      256 tp generate a 24-bit RGB pixel value.
 *
 *      This is called by the TkpGetPixel macro, used in xcolor.c.
 *
 * Results:
 *	An unsigned long that can be used as the pixel field of an XColor.
 *
 * Side effects:
 *	None.
 *----------------------------------------------------------------------
 */
MODULE_SCOPE
unsigned long
TkMacOSXRGBPixel(
    unsigned long red,
    unsigned long green,
    unsigned long blue)
{
    MacPixel p;
    p.pixel.colortype = rgbColor;
    p.pixel.value = (((red >> 8) & 0xff) << 16)  |
	            (((green >> 8) & 0xff) << 8) |
	            ((blue >> 8) & 0xff);
    return p.ulong;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXClearPixel --
 *
 *	Return the unsigned long value that appears in the pixel
 *	field of the XColor for systemTransparentColor.
 *
 *      This is used in tkMacOSXImage.c.
 *
 * Results:
 *	The unsigned long that appears in the pixel field of the XColor
 *      for systemTransparentPixel.
 *
 * Side effects:
 *	None.
 *----------------------------------------------------------------------
 */
MODULE_SCOPE
unsigned long TkMacOSXClearPixel(
    void)
{
    MacPixel p;
    p.pixel.value = 0;
    p.pixel.colortype = clearColor;
    return p.ulong;
}


/*
 *----------------------------------------------------------------------
 *
 * GetEntryFromPixel --
 *
 *	Extract a SystemColorMapEntry from the table.
 *
 * Results:

 *	Returns false if the code is out of bounds.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static bool
GetEntryFromPixel(
    unsigned long pixel,
    SystemColorMapEntry *entry)
{
    MacPixel p;
 // Should make sure this is the rgbColor index, even if the data gets shuffled.
    unsigned int index = 0;

    p.ulong = pixel;
    if (p.pixel.colortype != rgbColor) {
	index = p.pixel.value;
    }
    if (index < systemColorMapSize) {
	*entry = *systemColorIndex[index];
	return true;
    } else {
	return false;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SetCGColorComponents --
 *
 *	Set the components of a CGColorRef from an XColor pixel value and a
 *      system color map entry.  The pixel value is only used in the case where
 *      the color is of type rgbColor.  In that case the normalized XColor RGB
 *      values are copied into the CGColorRef.
 *
 * Results:
 *	OSStatus
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/*
 * Definitions to prevent compiler warnings about nonexistent properties of NSColor.
 */

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101000
 #define LABEL_COLOR labelColor
#else
 #define LABEL_COLOR textColor
#endif
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101200
 #define LINK_COLOR linkColor
#else
 #define LINK_COLOR blueColor
#endif
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
 #define CONTROL_ACCENT_COLOR controlAccentColor
#else
 #define CONTROL_ACCENT_COLOR colorForControlTint:[NSColor currentControlTint]
#endif

/*
 * Apple claims that linkColor is available in 10.10 but the declaration
 * does not appear in NSColor.h until later.  Declaring it in a category
 * appears to be harmless and stops the compiler warnings. 
 */

@interface NSColor(TkColor)
#if MAC_OS_X_VERSION_MAX_ALLOWED > 101200
@property(class, strong, readonly) NSColor *linkColor;
#elif MAC_OS_X_VERSION_MAX_ALLOWED > 1080
@property(strong, readonly) NSColor *linkColor;
#else
@property(assign, readonly) NSColor *linkColor;
#endif
@end

static NSColorSpace* sRGB = NULL;
static CGFloat windowBackground[4] =
    {236.0 / 255, 236.0 / 255, 236.0 / 255, 1.0};

static OSStatus
SetCGColorComponents(
    SystemColorMapEntry entry,
    unsigned long pixel,
    CGColorRef *c)
{
    OSStatus err = noErr;
    NSColor *bgColor, *color = nil;
    CGFloat rgba[4] = {0, 0, 0, 1};
    static Bool initialized = 0;
    if (!sRGB) {
	sRGB = [NSColorSpace sRGBColorSpace];
    }

    /*
     * This function is called before our autorelease pool is set up,
     * so it needs its own pool.
     */

    NSAutoreleasePool *pool = [NSAutoreleasePool new];
    switch (entry.type) {
    case HIBrush:
	err = ChkErr(HIThemeBrushCreateCGColor, entry.value, c);
	return err;
    case rgbColor:
	rgba[0] = ((pixel >> 16) & 0xff) / 255.0;
	rgba[1] = ((pixel >>  8) & 0xff) / 255.0;
	rgba[2] = ((pixel      ) & 0xff) / 255.0;
	break;
    case ttkBackground:

	/*
	 * Prior to OSX 10.14, getComponents returns black when applied to
	 * windowBackGroundColor.
	 */

	if ([NSApp macOSVersion] < 101400) {
	    for (int i=0; i<3; i++) {
		rgba[i] = windowBackground[i];
	    }
	} else {
	    bgColor = [[NSColor windowBackgroundColor] colorUsingColorSpace:sRGB];
	    [bgColor getComponents: rgba];
	}
	if (rgba[0] + rgba[1] + rgba[2] < 1.5) {
	    for (int i=0; i<3; i++) {
		rgba[i] += entry.value*8.0 / 255.0;
	    }
	} else {
	    for (int i=0; i<3; i++) {
		rgba[i] -= entry.value*8.0 / 255.0;
	    }
	}
	break;
    case semantic:
	color = [[NSColor valueForKey:entry.selector] colorUsingColorSpace:sRGB];	
	[color getComponents: rgba];
	break;
    case clearColor:
	rgba[3]	= 0.0;
	break;

    /*
     * There are no HITheme functions which convert Text or background colors
     * to CGColors.  (GetThemeTextColor has been removed, and it was never
     * possible with backgrounds.)  If we get one of these we return black.
     */

    case HIText:
    case HIBackground:
    default:
	break;
    }
    *c = CGColorCreate(sRGB.CGColorSpace, rgba);
    [pool drain];
    return err;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXInDarkMode --
 *
 *      Tests whether the given window's NSView has a DarkAqua Appearance.
 *
 * Results:
 *      Returns true if the NSView is in DarkMode, false if not.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE Bool
TkMacOSXInDarkMode(Tk_Window tkwin)
{
    int result = false;

#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400
    static NSAppearanceName darkAqua = @"NSAppearanceNameDarkAqua";

    if ([NSApp macOSVersion] >= 101400) {
        TkWindow *winPtr = (TkWindow*) tkwin;
	NSView *view = nil;
	if (winPtr && winPtr->privatePtr) {
	    view = TkMacOSXDrawableView(winPtr->privatePtr);
	}
	if (view) {
	    result = [view.effectiveAppearance.name isEqualToString:darkAqua];
	} else {
	    result = [[NSAppearance currentAppearance].name
			 isEqualToString:darkAqua];
	}
    }
#endif

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TkSetMacColor --
 *
 *	Sets the components of a CGColorRef from an XColor pixel value.
 *      XXXX The high order byte of the pixel value is used as an index into
 *      the system color table, and then SetCGColorComponents is called
 *      with the table entry and the pixel value.
 *
 * Results:
 *      Returns false if the high order byte is not a valid index, true
 *	otherwise.
 *
 * Side effects:
 *	The variable macColor is set to a new CGColorRef, the caller is
 *	responsible for releasing it!
 *
 *----------------------------------------------------------------------
 */

int
TkSetMacColor(
    unsigned long pixel,		/* Pixel value to convert. */
    void *macColor)			/* CGColorRef to modify. */
{
    CGColorRef *color = (CGColorRef*)macColor;
    OSStatus err = -1;
    SystemColorMapEntry entry;

    if (GetEntryFromPixel(pixel, &entry)) {
	err = ChkErr(SetCGColorComponents, entry, pixel, color);
    }
    return (err == noErr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpInitGCCache, TkpFreeGCCache, CopyCachedColor, SetCachedColor --
 *
 *	Maintain a per-GC cache of previously converted CGColorRefs
 *
 * Results:
 *	None resp. retained CGColorRef for CopyCachedColor()
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkpInitGCCache(
    GC gc)
{
    bzero(TkpGetGCCache(gc), sizeof(TkpGCCache));
}

void
TkpFreeGCCache(
    GC gc)
{
    TkpGCCache *gcCache = TkpGetGCCache(gc);

    if (gcCache->cachedForegroundColor) {
	CFRelease(gcCache->cachedForegroundColor);
    }
    if (gcCache->cachedBackgroundColor) {
	CFRelease(gcCache->cachedBackgroundColor);
    }
}

static CGColorRef
CopyCachedColor(
    GC gc,
    unsigned long pixel)
{
    TkpGCCache *gcCache = TkpGetGCCache(gc);
    CGColorRef cgColor = NULL;

    if (gcCache) {
	if (gcCache->cachedForeground == pixel) {
	    cgColor = gcCache->cachedForegroundColor;
	} else if (gcCache->cachedBackground == pixel) {
	    cgColor = gcCache->cachedBackgroundColor;
	}
	if (cgColor) {
	    CFRetain(cgColor);
	}
    }
    return cgColor;
}

static void
SetCachedColor(
    GC gc,
    unsigned long pixel,
    CGColorRef cgColor)
{
    TkpGCCache *gcCache = TkpGetGCCache(gc);

    if (gcCache && cgColor) {
	if (gc->foreground == pixel) {
	    if (gcCache->cachedForegroundColor) {
		CFRelease(gcCache->cachedForegroundColor);
	    }
	    gcCache->cachedForegroundColor = (CGColorRef) CFRetain(cgColor);
	    gcCache->cachedForeground = pixel;
	} else if (gc->background == pixel) {
	    if (gcCache->cachedBackgroundColor) {
		CFRelease(gcCache->cachedBackgroundColor);
	    }
	    gcCache->cachedBackgroundColor = (CGColorRef) CFRetain(cgColor);
	    gcCache->cachedBackground = pixel;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXCreateCGColor --
 *
 *	Creates a CGColorRef from a X style pixel value.
 *
 * Results:
 *	Returns NULL if not a real pixel, CGColorRef otherwise.
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

CGColorRef
TkMacOSXCreateCGColor(
    GC gc,
    unsigned long pixel)		/* Pixel value to convert. */
{
    CGColorRef cgColor = CopyCachedColor(gc, pixel);

    if (!cgColor && TkSetMacColor(pixel, &cgColor)) {
	SetCachedColor(gc, pixel, cgColor);
    }
    return cgColor;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXGetNSColor --
 *
 *	Creates an autoreleased NSColor from a X style pixel value.
 *
 * Results:
 *	Returns nil if not a real pixel, NSColor* otherwise.
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

NSColor*
TkMacOSXGetNSColor(
    GC gc,
    unsigned long pixel)		/* Pixel value to convert. */
{
    CGColorRef cgColor = TkMacOSXCreateCGColor(gc, pixel);
    NSColor *nsColor = nil;

    if (cgColor) {
	NSColorSpace *colorSpace = [[NSColorSpace alloc]
		initWithCGColorSpace:CGColorGetColorSpace(cgColor)];

	nsColor = [NSColor colorWithColorSpace:colorSpace
		components:CGColorGetComponents(cgColor)
		count:CGColorGetNumberOfComponents(cgColor)];
	[colorSpace release];
	CFRelease(cgColor);
    }
    return nsColor;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXSetColorInContext --
 *
 *	Sets fill and stroke color in the given CG context from an X
 *	pixel value, or if the pixel code indicates a system color,
 *	sets the corresponding brush, textColor or background via
 *	HITheme APIs if available or Appearance mgr APIs.
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
TkMacOSXSetColorInContext(
    GC gc,
    unsigned long pixel,
    CGContextRef context)
{
    OSStatus err = noErr;
    CGColorRef cgColor = nil;
    SystemColorMapEntry entry;
    CGRect rect;
    HIThemeBackgroundDrawInfo info = {0, kThemeStateActive, 0};;

    if (!cgColor && GetEntryFromPixel(pixel, &entry)) {
	switch (entry.type) {
	case HIBrush:
	    err = ChkErr(HIThemeSetFill, entry.value, NULL, context,
		    kHIThemeOrientationNormal);
	    if (err == noErr) {
		err = ChkErr(HIThemeSetStroke, entry.value, NULL, context,
			kHIThemeOrientationNormal);
	    }
	    break;
	case HIText:
	    err = ChkErr(HIThemeSetTextFill, entry.value, NULL, context,
		    kHIThemeOrientationNormal);
	    break;
	case HIBackground:
	    info.kind = entry.value;
	    rect = CGContextGetClipBoundingBox(context);
	    err = ChkErr(HIThemeApplyBackground, &rect, &info,
		    context, kHIThemeOrientationNormal);
	    break;
	default:
	    err = ChkErr(SetCGColorComponents, entry, pixel, &cgColor);
	    if (err == noErr) {
		SetCachedColor(gc, pixel, cgColor);
	    }
	    break;
	}
    }
    if (cgColor) {
	CGContextSetFillColorWithColor(context, cgColor);
	CGContextSetStrokeColorWithColor(context, cgColor);
	CGColorRelease(cgColor);
    }
    if (err != noErr) {
	TkMacOSXDbgMsg("Ignored unknown pixel value 0x%lx", pixel);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetColor --
 *
 *	Allocate a new TkColor for the color with the given name.
 *
 * Results:
 *	Returns a newly allocated TkColor, or NULL on failure.
 *
 * Side effects:
 *	May invalidate the colormap cache associated with tkwin upon
 *	allocating a new colormap entry. Allocates a new TkColor
 *	structure.
 *
 *----------------------------------------------------------------------
 */

TkColor *
TkpGetColor(
    Tk_Window tkwin,		/* Window in which color will be used. */
    Tk_Uid name)		/* Name of color to be allocated (in form
				 * suitable for passing to XParseColor). */
{
    Display *display = tkwin != None ? Tk_Display(tkwin) : NULL;
    Colormap colormap = tkwin!= None ? Tk_Colormap(tkwin) : None;
    TkColor *tkColPtr;
    XColor color;
    static Bool initialized = NO;
    static NSColorSpace* sRGB = NULL;
    if (!initialized) {
	initialized = YES;
	sRGB = [NSColorSpace sRGBColorSpace];
	initColorTable();
    }

    /*
     * Check to see if this is a system color. Otherwise, XParseColor
     * will do all the work.
     */

    if (strncasecmp(name, "system", 6) == 0) {
	Tcl_HashEntry *hPtr = NULL;
	SystemColorMapEntry *entry;
	hPtr = Tcl_FindHashEntry(&systemColorMap, name + 6);
	if (hPtr != NULL) {
	    entry = (SystemColorMapEntry *)Tcl_GetHashValue(hPtr);
	    OSStatus err;
	    CGColorRef c;
	    unsigned char pixelCode = entry->index;
	    err = ChkErr(SetCGColorComponents, *entry, 0, &c);
	    if (err == noErr) {
		MacPixel p;

		const size_t n = CGColorGetNumberOfComponents(c);
		const CGFloat *rgba = CGColorGetComponents(c);

		switch (n) {
		case 4:
		    color.red   = rgba[0] * 65535.0;
		    color.green = rgba[1] * 65535.0;
		    color.blue  = rgba[2] * 65535.0;
		    break;
		case 2:
		    color.red = color.green = color.blue = rgba[0] * 65535.0;
		    break;
		default:
		    Tcl_Panic("CGColor with %d components", (int) n);
		}
		p.pixel.value = pixelCode;
		p.pixel.colortype = entry->type;
		color.pixel = p.ulong;
		CGColorRelease(c);
		goto validXColor;
	    }
	    CGColorRelease(c);
	}
    }

    if (TkParseColor(display, colormap, name, &color) == 0) {
	return NULL;
    }

validXColor:
    tkColPtr = ckalloc(sizeof(TkColor));
    tkColPtr->color = color;

    return tkColPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetColorByValue --
 *
 *	Given a desired set of red-green-blue intensities for a color,
 *	locate a pixel value to use to draw that color in a given
 *	window.
 *
 * Results:
 *	The return value is a pointer to an TkColor structure that
 *	indicates the closest red, blue, and green intensities available
 *	to those specified in colorPtr, and also specifies a pixel
 *	value to use to draw in that color.
 *
 * Side effects:
 *	May invalidate the colormap cache for the specified window.
 *	Allocates a new TkColor structure.
 *
 *----------------------------------------------------------------------
 */

TkColor *
TkpGetColorByValue(
    Tk_Window tkwin,		/* Window in which color will be used. */
    XColor *colorPtr)		/* Red, green, and blue fields indicate
				 * desired color. */
{
    TkColor *tkColPtr = ckalloc(sizeof(TkColor));

    tkColPtr->color.red = colorPtr->red;
    tkColPtr->color.green = colorPtr->green;
    tkColPtr->color.blue = colorPtr->blue;
    tkColPtr->color.pixel = TkpGetPixel(colorPtr);
    return tkColPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Stub functions --
 *
 *	These functions are just stubs for functions that either
 *	don't make sense on the Mac or have yet to be implemented.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	These calls do nothing - which may not be expected.
 *
 *----------------------------------------------------------------------
 */

Status
XAllocColor(
    Display *display,		/* Display. */
    Colormap map,		/* Not used. */
    XColor *colorPtr)		/* XColor struct to modify. */
{
    display->request++;
    colorPtr->pixel = TkpGetPixel(colorPtr);
    return 1;
}

Colormap
XCreateColormap(
    Display *display,		/* Display. */
    Window window,		/* X window. */
    Visual *visual,		/* Not used. */
    int alloc)			/* Not used. */
{
    static Colormap index = 1;

    /*
     * Just return a new value each time.
     */
    return index++;
}

int
XFreeColormap(
    Display* display,		/* Display. */
    Colormap colormap)		/* Colormap. */
{
    return Success;
}

int
XFreeColors(
    Display* display,		/* Display. */
    Colormap colormap,		/* Colormap. */
    unsigned long* pixels,	/* Array of pixels. */
    int npixels,		/* Number of pixels. */
    unsigned long planes)	/* Number of pixel planes. */
{
    /*
     * The Macintosh version of Tk uses TrueColor. Nothing
     * needs to be done to release colors as there really is
     * no colormap in the Tk sense.
     */
    return Success;
}

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
