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
    HIText,        /* A HITheme text color. */
    HIBackground,  /* A HITheme background color. */
    ttkBackground, /* A background color which indicates nesting level.*/
    semantic,      /* A semantic NSColor.*/
};

typedef struct xpixel_t {
    unsigned value: 24;     /* Either RGB or an index into systemColorMap. */
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
    int value;
    char *macName;
    /* Fields below are filled in after or during construction of the hash table. */
    int index;
    NSString *selector;
} SystemColorDatum;

/*
 * WARNING: Semantic colors which are not supported on all systems must be
 * preceded by a backup color with the same name which *is* supported.  Systems
 * which do support the color will replace the backup value when the table is
 * constructed.  Failing to ensure this will result in a Tcl_Panic abort.
 */

static SystemColorDatum systemColorData[] = {
{"Pixel",				rgbColor, 0 },
{"Transparent",				clearColor,   0 },

{"Highlight",				HIBrush,  kThemeBrushPrimaryHighlightColor },
{"HighlightSecondary",		    	HIBrush,  kThemeBrushSecondaryHighlightColor },
{"HighlightText",			HIBrush,  kThemeBrushBlack },
{"HighlightAlternate",			HIBrush,  kThemeBrushAlternatePrimaryHighlightColor },
{"PrimaryHighlightColor",		HIBrush,  kThemeBrushPrimaryHighlightColor },
{"ButtonFace",				HIBrush,  kThemeBrushButtonFaceActive },
{"SecondaryHighlightColor",		HIBrush,  kThemeBrushSecondaryHighlightColor },
{"ButtonFrame",				HIBrush,  kThemeBrushButtonFrameActive },
{"AlternatePrimaryHighlightColor",      HIBrush,  kThemeBrushAlternatePrimaryHighlightColor },
{"WindowBody",				HIBrush,  kThemeBrushDocumentWindowBackground },
{"SheetBackground",			HIBrush,  kThemeBrushSheetBackground },
{"MenuActive",				HIBrush,  kThemeBrushMenuBackgroundSelected },
{"Menu",				HIBrush,  kThemeBrushMenuBackground },
{"DialogBackgroundInactive",		HIBrush,  kThemeBrushDialogBackgroundInactive },
{"DialogBackgroundActive",		HIBrush,  kThemeBrushDialogBackgroundActive },
{"AlertBackgroundActive",		HIBrush,  kThemeBrushAlertBackgroundActive },
{"AlertBackgroundInactive",		HIBrush,  kThemeBrushAlertBackgroundInactive },
{"ModelessDialogBackgroundActive",	HIBrush,  kThemeBrushModelessDialogBackgroundActive },
{"ModelessDialogBackgroundInactive",	HIBrush,  kThemeBrushModelessDialogBackgroundInactive },
{"UtilityWindowBackgroundActive",	HIBrush,  kThemeBrushUtilityWindowBackgroundActive },
{"UtilityWindowBackgroundInactive",	HIBrush,  kThemeBrushUtilityWindowBackgroundInactive },
{"ListViewSortColumnBackground",	HIBrush,  kThemeBrushListViewSortColumnBackground },
{"ListViewBackground",			HIBrush,  kThemeBrushListViewBackground },
{"IconLabelBackground",			HIBrush,  kThemeBrushIconLabelBackground },
{"ListViewSeparator",			HIBrush,  kThemeBrushListViewSeparator },
{"ChasingArrows",			HIBrush,  kThemeBrushChasingArrows },
{"DragHilite",				HIBrush,  kThemeBrushDragHilite },
{"DocumentWindowBackground",		HIBrush,  kThemeBrushDocumentWindowBackground },
{"FinderWindowBackground",		HIBrush,  kThemeBrushFinderWindowBackground },
{"ScrollBarDelimiterActive",		HIBrush,  kThemeBrushScrollBarDelimiterActive },
{"ScrollBarDelimiterInactive",		HIBrush,  kThemeBrushScrollBarDelimiterInactive },
{"FocusHighlight",			HIBrush,  kThemeBrushFocusHighlight },
{"PopupArrowActive",			HIBrush,  kThemeBrushPopupArrowActive },
{"PopupArrowPressed",			HIBrush,  kThemeBrushPopupArrowPressed },
{"PopupArrowInactive",			HIBrush,  kThemeBrushPopupArrowInactive },
{"AppleGuideCoachmark",			HIBrush,  kThemeBrushAppleGuideCoachmark },
{"IconLabelBackgroundSelected",		HIBrush,  kThemeBrushIconLabelBackgroundSelected },
{"StaticAreaFill",			HIBrush,  kThemeBrushStaticAreaFill },
{"ActiveAreaFill",			HIBrush,  kThemeBrushActiveAreaFill },
{"ButtonFrameActive",			HIBrush,  kThemeBrushButtonFrameActive },
{"ButtonFrameInactive",			HIBrush,  kThemeBrushButtonFrameInactive },
{"ButtonFaceActive",			HIBrush,  kThemeBrushButtonFaceActive },
{"ButtonFaceInactive",			HIBrush,  kThemeBrushButtonFaceInactive },
{"ButtonFacePressed",			HIBrush,  kThemeBrushButtonFacePressed },
{"ButtonActiveDarkShadow",		HIBrush,  kThemeBrushButtonActiveDarkShadow },
{"ButtonActiveDarkHighlight",		HIBrush,  kThemeBrushButtonActiveDarkHighlight },
{"ButtonActiveLightShadow",		HIBrush,  kThemeBrushButtonActiveLightShadow },
{"ButtonActiveLightHighlight",		HIBrush,  kThemeBrushButtonActiveLightHighlight },
{"ButtonInactiveDarkShadow",		HIBrush,  kThemeBrushButtonInactiveDarkShadow },
{"ButtonInactiveDarkHighlight",		HIBrush,  kThemeBrushButtonInactiveDarkHighlight },
{"ButtonInactiveLightShadow",		HIBrush,  kThemeBrushButtonInactiveLightShadow },
{"ButtonInactiveLightHighlight",	HIBrush,  kThemeBrushButtonInactiveLightHighlight },
{"ButtonPressedDarkShadow",		HIBrush,  kThemeBrushButtonPressedDarkShadow },
{"ButtonPressedDarkHighlight",		HIBrush,  kThemeBrushButtonPressedDarkHighlight },
{"ButtonPressedLightShadow",		HIBrush,  kThemeBrushButtonPressedLightShadow },
{"ButtonPressedLightHighlight",		HIBrush,  kThemeBrushButtonPressedLightHighlight },
{"BevelActiveLight",			HIBrush,  kThemeBrushBevelActiveLight },
{"BevelActiveDark",			HIBrush,  kThemeBrushBevelActiveDark },
{"BevelInactiveLight",			HIBrush,  kThemeBrushBevelInactiveLight },
{"BevelInactiveDark",			HIBrush,  kThemeBrushBevelInactiveDark },
{"NotificationWindowBackground",	HIBrush,  kThemeBrushNotificationWindowBackground },
{"MovableModalBackground",		HIBrush,  kThemeBrushMovableModalBackground },
{"SheetBackgroundOpaque",		HIBrush,  kThemeBrushSheetBackgroundOpaque },
{"DrawerBackground",			HIBrush,  kThemeBrushDrawerBackground },
{"ToolbarBackground",			HIBrush,  kThemeBrushToolbarBackground },
{"SheetBackgroundTransparent",		HIBrush,  kThemeBrushSheetBackgroundTransparent },
{"MenuBackground",			HIBrush,  kThemeBrushMenuBackground },
{"MenuBackgroundSelected",		HIBrush,  kThemeBrushMenuBackgroundSelected },
{"ListViewOddRowBackground",		HIBrush,  kThemeBrushListViewOddRowBackground },
{"ListViewEvenRowBackground",		HIBrush,  kThemeBrushListViewEvenRowBackground },
{"ListViewColumnDivider",		HIBrush,  kThemeBrushListViewColumnDivider },

{"ButtonText",				HIText,   kThemeTextColorPushButtonActive },
{"MenuActiveText",			HIText,   kThemeTextColorMenuItemSelected },
{"MenuDisabled",			HIText,   kThemeTextColorMenuItemDisabled },
{"MenuText",				HIText,   kThemeTextColorMenuItemActive },
{"BlackText",				HIText,   kThemeTextColorBlack },
{"DialogActiveText",			HIText,   kThemeTextColorDialogActive },
{"DialogInactiveText",			HIText,   kThemeTextColorDialogInactive },
{"AlertActiveText",			HIText,   kThemeTextColorAlertActive },
{"AlertInactiveText",			HIText,   kThemeTextColorAlertInactive },
{"ModelessDialogActiveText",		HIText,   kThemeTextColorModelessDialogActive },
{"ModelessDialogInactiveText",		HIText,   kThemeTextColorModelessDialogInactive },
{"WindowHeaderActiveText",		HIText,   kThemeTextColorWindowHeaderActive },
{"WindowHeaderInactiveText",		HIText,   kThemeTextColorWindowHeaderInactive },
{"PlacardActiveText",			HIText,   kThemeTextColorPlacardActive },
{"PlacardInactiveText",			HIText,   kThemeTextColorPlacardInactive },
{"PlacardPressedText",			HIText,   kThemeTextColorPlacardPressed },
{"PushButtonActiveText",		HIText,   kThemeTextColorPushButtonActive },
{"PushButtonInactiveText",		HIText,   kThemeTextColorPushButtonInactive },
{"PushButtonPressedText",		HIText,   kThemeTextColorPushButtonPressed },
{"BevelButtonActiveText",		HIText,   kThemeTextColorBevelButtonActive },
{"BevelButtonInactiveText",		HIText,   kThemeTextColorBevelButtonInactive },
{"BevelButtonPressedText",		HIText,   kThemeTextColorBevelButtonPressed },
{"PopupButtonActiveText",		HIText,   kThemeTextColorPopupButtonActive },
{"PopupButtonInactiveText",		HIText,   kThemeTextColorPopupButtonInactive },
{"PopupButtonPressedText",		HIText,   kThemeTextColorPopupButtonPressed },
{"IconLabelText",			HIText,   kThemeTextColorIconLabel },
{"ListViewText",			HIText,   kThemeTextColorListView },
{"DocumentWindowTitleActiveText",	HIText,   kThemeTextColorDocumentWindowTitleActive },
{"DocumentWindowTitleInactiveText",	HIText,   kThemeTextColorDocumentWindowTitleInactive },
{"MovableModalWindowTitleActiveText",  	HIText,   kThemeTextColorMovableModalWindowTitleActive },
{"MovableModalWindowTitleInactiveText",	HIText,   kThemeTextColorMovableModalWindowTitleInactive },
{"UtilityWindowTitleActiveText",	HIText,   kThemeTextColorUtilityWindowTitleActive },
{"UtilityWindowTitleInactiveText",	HIText,   kThemeTextColorUtilityWindowTitleInactive },
{"PopupWindowTitleActiveText",		HIText,   kThemeTextColorPopupWindowTitleActive },
{"PopupWindowTitleInactiveText",	HIText,   kThemeTextColorPopupWindowTitleInactive },
{"RootMenuActiveText",			HIText,   kThemeTextColorRootMenuActive },
{"RootMenuSelectedText",		HIText,   kThemeTextColorRootMenuSelected },
{"RootMenuDisabledText",		HIText,   kThemeTextColorRootMenuDisabled },
{"MenuItemActiveText",			HIText,   kThemeTextColorMenuItemActive },
{"MenuItemSelectedText",		HIText,   kThemeTextColorMenuItemSelected },
{"MenuItemDisabledText",		HIText,   kThemeTextColorMenuItemDisabled },
{"PopupLabelActiveText",		HIText,   kThemeTextColorPopupLabelActive },
{"PopupLabelInactiveText",		HIText,   kThemeTextColorPopupLabelInactive },
{"TabFrontActiveText",			HIText,   kThemeTextColorTabFrontActive },
{"TabNonFrontActiveText",		HIText,   kThemeTextColorTabNonFrontActive },
{"TabNonFrontPressedText",		HIText,   kThemeTextColorTabNonFrontPressed },
{"TabFrontInactiveText",		HIText,   kThemeTextColorTabFrontInactive },
{"TabNonFrontInactiveText",		HIText,   kThemeTextColorTabNonFrontInactive },
{"IconLabelSelectedText",		HIText,   kThemeTextColorIconLabelSelected },
{"BevelButtonStickyActiveText",		HIText,   kThemeTextColorBevelButtonStickyActive },
{"BevelButtonStickyInactiveText",	HIText,   kThemeTextColorBevelButtonStickyInactive },
{"NotificationText",			HIText,   kThemeTextColorNotification },
{"SystemDetailText",			HIText,   kThemeTextColorSystemDetail },
{"PlacardBackground",			HIBackground, kThemeBackgroundPlacard },
{"WindowHeaderBackground",		HIBackground, kThemeBackgroundWindowHeader },
{"ListViewWindowHeaderBackground",	HIBackground, kThemeBackgroundListViewWindowHeader },
{"MetalBackground",			HIBackground, kThemeBackgroundMetal },

{"SecondaryGroupBoxBackground",		HIBackground, kThemeBackgroundSecondaryGroupBox },
{"TabPaneBackground",			HIBackground, kThemeBackgroundTabPane },
{"WhiteText",				HIText,   kThemeTextColorWhite },
{"Black",				HIBrush,  kThemeBrushBlack },
{"White",				HIBrush,  kThemeBrushWhite },

    /*
     * Dynamic Colors
     */

{"WindowBackgroundColor",	    ttkBackground, 0 },
{"WindowBackgroundColor1",	    ttkBackground, 1 },
{"WindowBackgroundColor2",	    ttkBackground, 2 },
{"WindowBackgroundColor3",	    ttkBackground, 3 },
{"WindowBackgroundColor4",	    ttkBackground, 4 },
{"WindowBackgroundColor5",	    ttkBackground, 5 },
{"WindowBackgroundColor6",	    ttkBackground, 6 },
{"WindowBackgroundColor7",	    ttkBackground, 7 },
/* Apple's SecondaryLabelColor is the same as their LabelColor so we roll our own. */
{"SecondaryLabelColor",		    ttkBackground, 14 },

{"TextColor",			    semantic, 0, "textColor" },
{"SelectedTextColor",		    semantic, 0, "selectedTextColor" },
{"LabelColor",			    semantic, 0, "textColor"},
{"LabelColor",			    semantic, 0, "labelColor"},
{"ControlTextColor",      	    semantic, 0, "controlTextColor" },
{"DisabledControlTextColor",	    semantic, 0, "disabledControlTextColor" },
#if MAC_OS_X_VERSION_MAX_ALLOWED > 1060
{"SelectedTabTextColor",	    semantic, 0, "whiteColor" },
#else
{"SelectedTabTextColor",	    semantic, 0, "blackColor" },
#endif
{"TextBackgroundColor",		    semantic, 0, "textBackgroundColor" },
{"SelectedTextBackgroundColor",	    semantic, 0, "selectedTextBackgroundColor" },
{"ControlAccentColor",		    semantic, 0, "controlAccentColor" },
{"LinkColor",			    semantic, 0, "blueColor" },
{"LinkColor",			    semantic, 0, "linkColor" },
{"PlaceholderTextColor",	    semantic, 0, "grayColor" },
{"PlaceholderTextColor",	    semantic, 0, "placeholderTextColor" },
{"SeparatorColor",		    semantic, 0, "grayColor" },
{"SeparatorColor",		    semantic, 0, "separatorColor" },
{NULL,				    0, 0 }
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
