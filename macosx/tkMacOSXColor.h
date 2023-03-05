#ifndef MACOSXCOLOR_H
#define MACOSXCOLOR_H
/*
 * The generic Tk code uses the X11 GC type to describe a graphics context.
 * (A GC is a pointer to a struct XGCValues).  The foreground and background
 * colors in a GC are unsigned longs.  These are meant to be used as indexes
 * into a table of XColors, where an XColor is declared in Xlib.h as:
 * typedef struct {
 *       unsigned long pixel;
 *       unsigned short red, green, blue;
 *       char flags;
 *       char pad;
 * } XColor;
 *
 * The xlib function XParseColor creates XColors from strings.  It recognizes
 * literal hexadecimal color specifications such as "#RRGGBB" as well as the
 * standard X11 color names.  When XParseColor creates an XColor it fills in
 * all of the fields except for the pixel field, and then passes the XColor
 * to TkpGetPixel to get a value to use for the pixel field. Since TkpGetPixel
 * is platform specific, each platform is free to choose a value which can
 * be used to set the foreground or background color in the platform's graphics
 * context.
 *
 * Tk represents a color by a struct TkColor, which extends the XColor struct.
 * Tk provides a mapping from color names to TkColors which extends the mapping
 * provided by XParseColor but also allows for platform specific color names.
 * By convention, these platform specific color names begin with the string
 * "system".  The mapping from names to TkColors is implemented by the function
 * TkpGetColor defined for the Macintosh in this file.  The pixel field in the
 * XColor contained in a TkColor will be stored in the X11 graphics context.
 * In X11 the pixel field is used as an index into a colormap.  On the Mac
 * the high order byte of the pixel is used to indicate a color type and
 * the low 24 bits are either used as an rgb value (if the type is rgbColor)
 * or as an index into a table of color descriptions.
 */

enum colorType {
    rgbColor,      /* The 24 bit value is an rgb color. */
    clearColor,    /* The unique rgba color with all channels 0. */
    HIBrush,       /* A HITheme brush color.*/
    ttkBackground, /* A background color which indicates nesting level.*/
    semantic,      /* A semantic NSColor.*/
};

typedef struct xpixel_t {
    unsigned value: 24;     /* Either RGB or an index into systemColorData. */
    unsigned colortype: 8;
} xpixel;

typedef union MacPixel_t {
    unsigned long ulong;
    xpixel pixel;
} MacPixel;

/*
 * We maintain two colormaps, one for the LightAqua appearance and one for the
 * DarkAqua appearance.
 */

enum macColormap {
    noColormap,
    lightColormap,
    darkColormap,
};

/*
 * In TkMacOSXColor.c a Tk hash table is constructed from the static data
 * below to map system color names to CGColors.
 */

typedef struct {
    const char *name;
    enum colorType type;
    ThemeBrush value;
    const char *macName;
    /* Fields below are filled in after or during construction of the hash table. */
    size_t index;
    NSString *selector;
} SystemColorDatum;

/*
 * WARNING: Semantic colors which are not supported on all systems must be
 * preceded by a backup color with the same name which *is* supported.  Systems
 * which do support the color will replace the backup value when the table is
 * constructed.  Failing to ensure this will result in a Tcl_Panic abort.
 */

static SystemColorDatum systemColorData[] = {
{"Pixel",				rgbColor, 0, NULL, 0, NULL },
{"Transparent",			       	clearColor,   0, NULL, 0, NULL },

{"Highlight",				HIBrush,  kThemeBrushPrimaryHighlightColor, NULL, 0, NULL },
{"HighlightSecondary",		    	HIBrush,  kThemeBrushSecondaryHighlightColor, NULL, 0, NULL },
{"HighlightText",			HIBrush,  kThemeBrushBlack, NULL, 0, NULL },
{"HighlightAlternate",			HIBrush,  kThemeBrushAlternatePrimaryHighlightColor, NULL, 0, NULL },
{"PrimaryHighlightColor",		HIBrush,  kThemeBrushPrimaryHighlightColor, NULL, 0, NULL },
{"ButtonFace",				HIBrush,  kThemeBrushButtonFaceActive, NULL, 0, NULL },
{"SecondaryHighlightColor",		HIBrush,  kThemeBrushSecondaryHighlightColor, NULL, 0, NULL },
{"ButtonFrame",				HIBrush,  kThemeBrushButtonFrameActive, NULL, 0, NULL },
{"AlternatePrimaryHighlightColor",      HIBrush,  kThemeBrushAlternatePrimaryHighlightColor, NULL, 0, NULL },
{"WindowBody",				HIBrush,  kThemeBrushDocumentWindowBackground, NULL, 0, NULL },
{"SheetBackground",			HIBrush,  kThemeBrushSheetBackground, NULL, 0, NULL },
{"MenuActive",				HIBrush,  kThemeBrushMenuBackgroundSelected, NULL, 0, NULL },
{"Menu",				HIBrush,  kThemeBrushMenuBackground, NULL, 0, NULL },
{"DialogBackgroundInactive",		HIBrush,  kThemeBrushDialogBackgroundInactive, NULL, 0, NULL },
{"DialogBackgroundActive",		HIBrush,  kThemeBrushDialogBackgroundActive, NULL, 0, NULL },
{"AlertBackgroundActive",		HIBrush,  kThemeBrushAlertBackgroundActive, NULL, 0, NULL },
{"AlertBackgroundInactive",		HIBrush,  kThemeBrushAlertBackgroundInactive, NULL, 0, NULL },
{"ModelessDialogBackgroundActive",	HIBrush,  kThemeBrushModelessDialogBackgroundActive, NULL, 0, NULL },
{"ModelessDialogBackgroundInactive",	HIBrush,  kThemeBrushModelessDialogBackgroundInactive, NULL, 0, NULL },
{"UtilityWindowBackgroundActive",	HIBrush,  kThemeBrushUtilityWindowBackgroundActive, NULL, 0, NULL },
{"UtilityWindowBackgroundInactive",	HIBrush,  kThemeBrushUtilityWindowBackgroundInactive, NULL, 0, NULL },
{"ListViewSortColumnBackground",	HIBrush,  kThemeBrushListViewSortColumnBackground, NULL, 0, NULL },
{"ListViewBackground",			HIBrush,  kThemeBrushListViewBackground, NULL, 0, NULL },
{"IconLabelBackground",			HIBrush,  kThemeBrushIconLabelBackground, NULL, 0, NULL },
{"ListViewSeparator",			HIBrush,  kThemeBrushListViewSeparator, NULL, 0, NULL },
{"ChasingArrows",			HIBrush,  kThemeBrushChasingArrows, NULL, 0, NULL },
{"DragHilite",				HIBrush,  kThemeBrushDragHilite, NULL, 0, NULL },
{"DocumentWindowBackground",		HIBrush,  kThemeBrushDocumentWindowBackground, NULL, 0, NULL },
{"FinderWindowBackground",		HIBrush,  kThemeBrushFinderWindowBackground, NULL, 0, NULL },
{"ScrollBarDelimiterActive",		HIBrush,  kThemeBrushScrollBarDelimiterActive, NULL, 0, NULL },
{"ScrollBarDelimiterInactive",		HIBrush,  kThemeBrushScrollBarDelimiterInactive, NULL, 0, NULL },
{"FocusHighlight",			HIBrush,  kThemeBrushFocusHighlight, NULL, 0, NULL },
{"PopupArrowActive",			HIBrush,  kThemeBrushPopupArrowActive, NULL, 0, NULL },
{"PopupArrowPressed",			HIBrush,  kThemeBrushPopupArrowPressed, NULL, 0, NULL },
{"PopupArrowInactive",			HIBrush,  kThemeBrushPopupArrowInactive, NULL, 0, NULL },
{"AppleGuideCoachmark",			HIBrush,  kThemeBrushAppleGuideCoachmark, NULL, 0, NULL },
{"IconLabelBackgroundSelected",		HIBrush,  kThemeBrushIconLabelBackgroundSelected, NULL, 0, NULL },
{"StaticAreaFill",			HIBrush,  kThemeBrushStaticAreaFill, NULL, 0, NULL },
{"ActiveAreaFill",			HIBrush,  kThemeBrushActiveAreaFill, NULL, 0, NULL },
{"ButtonFrameActive",			HIBrush,  kThemeBrushButtonFrameActive, NULL, 0, NULL },
{"ButtonFrameInactive",			HIBrush,  kThemeBrushButtonFrameInactive, NULL, 0, NULL },
{"ButtonFaceActive",			HIBrush,  kThemeBrushButtonFaceActive, NULL, 0, NULL },
{"ButtonFaceInactive",			HIBrush,  kThemeBrushButtonFaceInactive, NULL, 0, NULL },
{"ButtonFacePressed",			HIBrush,  kThemeBrushButtonFacePressed, NULL, 0, NULL },
{"ButtonActiveDarkShadow",		HIBrush,  kThemeBrushButtonActiveDarkShadow, NULL, 0, NULL },
{"ButtonActiveDarkHighlight",		HIBrush,  kThemeBrushButtonActiveDarkHighlight, NULL, 0, NULL },
{"ButtonActiveLightShadow",		HIBrush,  kThemeBrushButtonActiveLightShadow, NULL, 0, NULL },
{"ButtonActiveLightHighlight",		HIBrush,  kThemeBrushButtonActiveLightHighlight, NULL, 0, NULL },
{"ButtonInactiveDarkShadow",		HIBrush,  kThemeBrushButtonInactiveDarkShadow, NULL, 0, NULL },
{"ButtonInactiveDarkHighlight",		HIBrush,  kThemeBrushButtonInactiveDarkHighlight, NULL, 0, NULL },
{"ButtonInactiveLightShadow",		HIBrush,  kThemeBrushButtonInactiveLightShadow, NULL, 0, NULL },
{"ButtonInactiveLightHighlight",	HIBrush,  kThemeBrushButtonInactiveLightHighlight, NULL, 0, NULL },
{"ButtonPressedDarkShadow",		HIBrush,  kThemeBrushButtonPressedDarkShadow, NULL, 0, NULL },
{"ButtonPressedDarkHighlight",		HIBrush,  kThemeBrushButtonPressedDarkHighlight, NULL, 0, NULL },
{"ButtonPressedLightShadow",		HIBrush,  kThemeBrushButtonPressedLightShadow, NULL, 0, NULL },
{"ButtonPressedLightHighlight",		HIBrush,  kThemeBrushButtonPressedLightHighlight, NULL, 0, NULL },
{"BevelActiveLight",			HIBrush,  kThemeBrushBevelActiveLight, NULL, 0, NULL },
{"BevelActiveDark",			HIBrush,  kThemeBrushBevelActiveDark, NULL, 0, NULL },
{"BevelInactiveLight",			HIBrush,  kThemeBrushBevelInactiveLight, NULL, 0, NULL },
{"BevelInactiveDark",			HIBrush,  kThemeBrushBevelInactiveDark, NULL, 0, NULL },
{"NotificationWindowBackground",	HIBrush,  kThemeBrushNotificationWindowBackground, NULL, 0, NULL },
{"MovableModalBackground",		HIBrush,  kThemeBrushMovableModalBackground, NULL, 0, NULL },
{"SheetBackgroundOpaque",		HIBrush,  kThemeBrushSheetBackgroundOpaque, NULL, 0, NULL },
{"DrawerBackground",			HIBrush,  kThemeBrushDrawerBackground, NULL, 0, NULL },
{"ToolbarBackground",			HIBrush,  kThemeBrushToolbarBackground, NULL, 0, NULL },
{"SheetBackgroundTransparent",		HIBrush,  kThemeBrushSheetBackgroundTransparent, NULL, 0, NULL },
{"MenuBackground",			HIBrush,  kThemeBrushMenuBackground, NULL, 0, NULL },
{"MenuBackgroundSelected",		HIBrush,  kThemeBrushMenuBackgroundSelected, NULL, 0, NULL },
{"ListViewOddRowBackground",		HIBrush,  kThemeBrushListViewOddRowBackground, NULL, 0, NULL },
{"ListViewEvenRowBackground",		HIBrush,  kThemeBrushListViewEvenRowBackground, NULL, 0, NULL },
{"ListViewColumnDivider",		HIBrush,  kThemeBrushListViewColumnDivider, NULL, 0, NULL },

    /*
     * Dynamic Colors
     */

{"WindowBackgroundColor",	    ttkBackground, 0, NULL, 0, NULL },
{"WindowBackgroundColor1",	    ttkBackground, 1, NULL, 0, NULL },
{"WindowBackgroundColor2",	    ttkBackground, 2, NULL, 0, NULL },
{"WindowBackgroundColor3",	    ttkBackground, 3, NULL, 0, NULL },
{"WindowBackgroundColor4",	    ttkBackground, 4, NULL, 0, NULL },
{"WindowBackgroundColor5",	    ttkBackground, 5, NULL, 0, NULL },
{"WindowBackgroundColor6",	    ttkBackground, 6, NULL, 0, NULL },
{"WindowBackgroundColor7",	    ttkBackground, 7, NULL, 0, NULL },
/* Apple's SecondaryLabelColor is the same as their LabelColor so we roll our own. */
{"SecondaryLabelColor",		    ttkBackground, 14, NULL, 0, NULL },
/* Color to use for notebook tab label text -- depends on OS version. */
{"SelectedTabTextColor",	    semantic, 0, "textColor", 0, NULL },
/* Color to use for selected button labels -- depends on OS version. */
{"PressedButtonTextColor",	    semantic, 0, "textColor", 0, NULL },
/* Semantic colors that we simulate on older systems which don't supoort them. */
{"SelectedMenuItemTextColor",       semantic, 0, "selectedMenuItemTextColor", 0, NULL },
{"ControlAccentColor",		    semantic, 0, "controlAccentColor", 0, NULL },
{"LabelColor",                      semantic, 0, "blackColor", 0, NULL },
{"LinkColor",			    semantic, 0, "blueColor", 0, NULL },
{"PlaceholderTextColor",	    semantic, 0, "grayColor", 0, NULL },
{"SeparatorColor",		    semantic, 0, "grayColor", 0, NULL },
{"UnemphasizedSelectedTextBackgroundColor", semantic, 0, "grayColor", 0, NULL },
{NULL,				    rgbColor, 0, NULL, 0, NULL }
};

#endif
/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
