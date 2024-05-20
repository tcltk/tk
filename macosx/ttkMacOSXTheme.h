/*
 * ttkMacOSXTheme.h --
 *
 *      Static data and macros used in ttkMacOSXTheme.c
 *
 * Copyright 2019 Marc Culler
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

/*
 * Macros for handling drawing contexts.
 */

#define BEGIN_DRAWING(d) {				    \
    TkMacOSXDrawingContext dc;				    \
    if (!TkMacOSXSetupDrawingContext((d), NULL, &dc)) {	    \
	return;						    \
    }							    \

#define END_DRAWING				\
    TkMacOSXRestoreDrawingContext(&dc);}


#define HIOrientation kHIThemeOrientationNormal
#define NoThemeMetric 0xFFFFFFFF

/*
 *  A scale factor used to map a range of non-negative doubles into a large
 *  range of non-negative 32-bit integers without losing too much information.
 *  (Use 1073741824 == 2^30 so we don't get surprised by roundoff.)
 */

#define RangeToFactor(max) ((double) (1073741824.0) / (max < 1.0 ? 1.0 : max))

/*
 * Meanings of Ttk states represented by User1 and User2.
 */

#define TTK_STATE_IS_ACCENTED   TTK_STATE_USER2
#define TTK_TREEVIEW_STATE_SORTARROW    TTK_STATE_USER1

/*
 * Colors and gradients used when drawing buttons.
 */

typedef struct GrayColor {
    CGFloat grayscale;
    CGFloat alpha;
} GrayColor;

#define RGBACOLOR static CGFloat
#define RGBA256(r, g, b, a) {r / 255.0, g / 255.0, b / 255.0, a}
#define GRAYCOLOR static GrayColor
#define GRAY256(grayscale) {grayscale / 255.0, 1.0}

/*
 * Opaque Grays used for Gradient Buttons, Scrollbars and List Headers
 */

GRAYCOLOR darkDisabledIndicator = GRAY256(122.0);
GRAYCOLOR lightDisabledIndicator = GRAY256(152.0);

GRAYCOLOR darkGradientNormal = GRAY256(95.0);
GRAYCOLOR darkGradientPressed = GRAY256(118.0);
GRAYCOLOR darkGradientDisabled = GRAY256(82.0);
GRAYCOLOR darkGradientBorder = GRAY256(118.0);
GRAYCOLOR darkGradientBorderDisabled = GRAY256(94.0);
GRAYCOLOR lightGradientNormal = GRAY256(244.0);
GRAYCOLOR lightGradientPressed = GRAY256(175.0);
GRAYCOLOR lightGradientDisabled = GRAY256(235.0);
GRAYCOLOR lightGradientBorder = GRAY256(165.0);
GRAYCOLOR lightGradientBorderDisabled = GRAY256(204.0);

GRAYCOLOR lightTrough = GRAY256(250.0);
GRAYCOLOR darkTrough = GRAY256(47.0);
GRAYCOLOR lightInactiveThumb = GRAY256(200.0);
GRAYCOLOR lightActiveThumb = GRAY256(133.0);
GRAYCOLOR darkInactiveThumb = GRAY256(117.0);
GRAYCOLOR darkActiveThumb = GRAY256(158.0);

GRAYCOLOR lightListheaderBorder = GRAY256(200.0);
GRAYCOLOR listheaderSeparator = GRAY256(220.0);
GRAYCOLOR listheaderActiveBG = GRAY256(238.0);
GRAYCOLOR listheaderInactiveBG = GRAY256(246.0);

GRAYCOLOR lightComboSeparator = GRAY256(236.0);
GRAYCOLOR darkComboSeparator = GRAY256(66.0);

GRAYCOLOR darkTrack = GRAY256(84.0);
GRAYCOLOR darkInactiveTrack = GRAY256(107.0);
GRAYCOLOR lightTrack = GRAY256(177.0);
GRAYCOLOR lightInactiveTrack = GRAY256(139.0);

/*
 * Transparent Grays
 */

GRAYCOLOR boxBorder = {1.0, 0.20};
GRAYCOLOR darkSeparator = {1.0, 0.3};
GRAYCOLOR darkTabSeparator = {0.0, 0.25};
GRAYCOLOR darkListheaderBorder = {1.0, 0.125};
GRAYCOLOR pressedDefaultButton = {0.0, 0.1};
GRAYCOLOR darkPressedDefaultButton = {1.0, 0.1};

#define CG_WHITE CGColorGetConstantColor(kCGColorWhite)


/*
 * Structures which comprise a database of corner radii and state-dependent
 * colors used when drawing various types of buttons or entry widgets.
 */

typedef struct GrayPalette {
    CGFloat face;
    CGFloat top;
    CGFloat side;
    CGFloat bottom;
} GrayPalette;

typedef struct PaletteStateTable {
    GrayPalette light;          /* Light palette to use if this entry matches */
    GrayPalette dark;           /* dark palette to use if this entry matches */
    unsigned int onBits;        /* Bits which must be set */
    unsigned int offBits;       /* Bits which must be cleared */
} PaletteStateTable;

typedef struct ButtonDesign {
    CGFloat radius;
    PaletteStateTable palettes[];
} ButtonDesign;

/*
 * The data.
 */

static const ButtonDesign pushbuttonDesign = {
  .radius = 4.0,
  .palettes = {
    {
      .light = {.face = 242.0, .top = 213.0, .side = 210.0, .bottom = 200.0},
      .dark =  {.face = 94.0,  .top = 98.0,  .side = 94.0,  .bottom = 58.0},
      .onBits = TTK_STATE_DISABLED, .offBits = 0},
    {
      .light = {.face = 205.0, .top = 215.0, .side = 211.0, .bottom = 173.0},
      .dark =  {.face = 140.0, .top = 150.0, .side = 140.0, .bottom = 42.0},
      .onBits = TTK_STATE_PRESSED, .offBits = 0
    },
    {
      .light = {.face = 255.0, .top = 198.0, .side = 192.0, .bottom = 173.0},
      .dark =  {.face = 118.0, .top = 132.0, .side = 118.0, .bottom = 48.0},
      .onBits = 0, .offBits = 0
    }
  }
};

static const ButtonDesign helpDesign = {
  .radius = 11,
  .palettes = {
    {
      .light = {.face = 241.0, .top = 218.0, .side = 217.0, .bottom = 206.0},
      .dark =  {.face = 83.0,  .top = 89.0,  .side = 82.0,  .bottom = 82.0},
      .onBits = TTK_STATE_DISABLED, .offBits = 0},
    {
      .light = {.face = 240.0, .top = 208.0, .side = 205.0, .bottom = 188.0},
      .dark =  {.face = 129.0, .top = 121.0, .side = 106.0, .bottom = 106.0},
      .onBits = TTK_STATE_PRESSED, .offBits = 0
    },
    {
      .light = {.face = 255.0, .top = 208.0, .side = 205.0, .bottom = 188.0},
      .dark =  {.face = 107.0, .top = 121.0, .side = 106.0, .bottom = 106.0},
      .onBits = 0, .offBits = 0
    }
  }
};

static const ButtonDesign inlineDesign = {
  .radius = 8.0,
  .palettes = {
    {
      .light = {.face = 170.0, .top = 170.0, .side = 170.0, .bottom = 170.0},
      .dark =  {.face = 106.0,  .top = 106.0,  .side = 106.0,  .bottom = 106.0},
      .onBits = TTK_STATE_DISABLED, .offBits = 0},
    {
      .light = {.face = 34.0, .top = 34.0, .side = 34.0, .bottom = 34.0},
      .dark =  {.face = 225.0, .top = 225.0, .side = 225.0, .bottom = 225.0},
      .onBits = TTK_STATE_PRESSED, .offBits = 0
    },
    {
      .light = {.face = 113.0, .top = 113.0, .side = 113.0, .bottom = 113.0},
      .dark =  {.face = 165.0, .top = 165.0, .side = 165.0, .bottom = 165.0},
      .onBits = 0, .offBits = 0
    }
  }
};

static const ButtonDesign roundedrectDesign = {
  .radius = 3.0,
  .palettes = {
    {
      .light = {.face = 256.0, .top = 192.0, .side = 192.0, .bottom = 192.0},
      .dark =  {.face = 256.0, .top = 165.0, .side = 163.0, .bottom = 42.0},
      .onBits = TTK_STATE_DISABLED, .offBits = 0
    },
    {
      .light = {.face = 204.0, .top = 158.0, .side = 158.0, .bottom = 158.0},
      .dark =  {.face = 85.0,  .top = 115.0, .side = 115.0, .bottom = 115.0},
      .onBits = TTK_STATE_PRESSED, .offBits = 0
    },
    {
      .light = {.face = 205.0, .top = 215.0, .side = 211.0, .bottom = 173.0},
      .dark =  {.face = 140.0, .top = 150.0, .side = 140.0, .bottom = 42.0},
      .onBits = TTK_STATE_ALTERNATE, .offBits = TTK_STATE_BACKGROUND
    },

    /*
     * Gray values > 255 are replaced by the background color.
     */

    {
      .light = {.face = 256.0, .top = 158.0, .side = 158.0, .bottom = 158.0},
      .dark =  {.face = 256.0, .top = 115.0, .side = 115.0, .bottom = 115.0},
      .onBits = 0, .offBits = 0
    }
  }
};

static const ButtonDesign popupDesign = {
  .radius = 4.0,
  .palettes = {
    {
      .light = {.face = 242.0, .top = 213.0, .side = 210.0, .bottom = 200.0},
      .dark =  {.face = 94.0,  .top = 98.0,  .side = 94.0,  .bottom = 58.0},
      .onBits = TTK_STATE_DISABLED, .offBits = 0
    },
    {
      .light = {.face = 255.0, .top = 198.0, .side = 192.0, .bottom = 173.0},
      .dark =  {.face = 118.0, .top = 132.0, .side = 118.0, .bottom = 48.0},
      .onBits = 0, .offBits = 0
    }
  }
};

static const ButtonDesign checkDesign = {
  .radius = 4.0,
  .palettes = {
    {
      .light = {.face = 242.0, .top = 192.0, .side = 199.0, .bottom = 199.0},
      .dark =  {.face = 80.0,  .top = 90.0,  .side = 80.0,  .bottom = 49.0},
      .onBits = TTK_STATE_DISABLED, .offBits = 0
    },
    {
      .light = {.face = 255.0, .top = 165.0, .side = 184.0, .bottom = 184.0},
      .dark =  {.face = 118.0, .top = 132.0, .side = 118.0, .bottom = 48.0},
      .onBits = 0, .offBits = 0
    }
  }
};

static const ButtonDesign radioDesign = {
  .radius = 8.0,
  .palettes = {
    {
      .light = {.face = 242.0, .top = 189.0, .side = 198.0, .bottom = 199.0},
      .dark =  {.face = 80.0,  .top = 84.0,  .side = 88.0,  .bottom = 60.0},
      .onBits = TTK_STATE_DISABLED, .offBits = 0
    },
    {
      .light = {.face = 255.0, .top = 165.0, .side = 184.0, .bottom = 184.0},
      .dark =  {.face = 118.0, .top = 132.0, .side = 118.0, .bottom = 48.0},
      .onBits = 0, .offBits = 0
    }
  }
};

static const ButtonDesign recessedDesign = {
  .radius = 4.0,
  .palettes = {
    {
      .light = {.face = 117.0, .top = 117.0, .side = 117.0, .bottom = 117.0},
      .dark =  {.face = 129.0, .top = 129.0, .side = 129.0, .bottom = 129.0},
      .onBits = TTK_STATE_PRESSED, .offBits = 0
    },
    {
      .light = {.face = 182.0, .top = 182.0, .side = 182.0, .bottom = 182.0},
      .dark =  {.face = 105.0,  .top = 105.0, .side = 105.0, .bottom = 105.0},
      .onBits = TTK_STATE_ACTIVE, .offBits = TTK_STATE_SELECTED
    },
    {
      .light = {.face = 145.0, .top = 145.0, .side = 145.0, .bottom = 145.0},
      .dark =  {.face = 166.0, .top = 166.0, .side = 166.0, .bottom = 166.0},
      .onBits = TTK_STATE_SELECTED, .offBits = 0
    },
    {
      .light = {.face = 256.0, .top = 256.0, .side = 256.0, .bottom = 256.0},
      .dark =  {.face = 256.0, .top = 256.0, .side = 256.0, .bottom = 256.0},
      .onBits = 0, .offBits = 0
    }
  }
};

static const ButtonDesign sidebarDesign = {
  .radius = 8.0,
  .palettes = {
    {
      .light = {.face = 210.0, .top = 210.0, .side = 210.0, .bottom = 210.0},
      .dark =  {.face = 129.0, .top = 129.0, .side = 129.0, .bottom = 129.0},
      .onBits = TTK_STATE_SELECTED, .offBits = 0
    },
    {
      .light = {.face = 210.0, .top = 210.0, .side = 210.0, .bottom = 210.0},
      .dark =  {.face = 129.0, .top = 129.0, .side = 129.0, .bottom = 129.0},
      .onBits = TTK_STATE_PRESSED, .offBits = 0
    },
    {
      .light = {.face = 256.0, .top = 256.0, .side = 256.0, .bottom = 256.0},
      .dark =  {.face = 256.0, .top = 256.0, .side = 256.0, .bottom = 256.0},
      .onBits = 0, .offBits = 0
    }
  }
};

static const ButtonDesign incdecDesign = {
  .radius = 5.0,
  .palettes = {
    {
      .light = {.face = 246.0, .top = 236.0, .side = 227.0, .bottom = 213.0},
      .dark =  {.face = 80.0,  .top = 90.0,  .side = 80.0,  .bottom = 49.0},
      .onBits = TTK_STATE_DISABLED, .offBits = 0
    },
    {
      .light = {.face = 255.0, .top = 198.0, .side = 192.0, .bottom = 173.0},
      .dark =  {.face = 118.0, .top = 132.0, .side = 118.0, .bottom = 48.0},
      .onBits = 0, .offBits = 0
    }
  }
};

static const ButtonDesign bevelDesign = {
  .radius = 4.0,
  .palettes = {
    {
      .light = {.face = 242.0, .top = 213.0, .side = 210.0, .bottom = 200.0},
      .dark =  {.face = 94.0,  .top = 98.0,  .side = 94.0,  .bottom = 58.0},
      .onBits = TTK_STATE_DISABLED, .offBits = 0
    },
    {
      .light = {.face = 205.0, .top = 215.0, .side = 211.0, .bottom = 173.0},
      .dark =  {.face = 140.0, .top = 150.0, .side = 140.0, .bottom = 42.0},
      .onBits = TTK_STATE_PRESSED, .offBits = 0
    },
    {
      .light = {.face = 228.0, .top = 215.0, .side = 211.0, .bottom = 173.0},
      .dark =  {.face = 163.0, .top = 150.0, .side = 140.0, .bottom = 42.0},
      .onBits = TTK_STATE_SELECTED, .offBits = 0
    },
    {
      .light = {.face = 255.0, .top = 198.0, .side = 192.0, .bottom = 173.0},
      .dark =  {.face = 118.0, .top = 132.0, .side = 118.0, .bottom = 48.0},
      .onBits = 0, .offBits = 0
    }
  }
};

static const ButtonDesign tabDesign = {
  .radius = 4.0,
  .palettes = {

    /*
     * Apple does not have such a thing as a disabled tab.  If it is
     * disabled, it should be removed.  But we provide one based on the
     * disabled button.
     */

    {
      .light = {.face = 229.0, .top = 213.0, .side = 242.0, .bottom = 200.0},
      .dark =  {.face = 163.0,  .top = 90.0,  .side = 80.0,  .bottom = 49.0},
      .onBits = TTK_STATE_DISABLED, .offBits = 0
    },
    {
      .light = {.face = 229.0, .top = 205.0, .side = 211.0, .bottom = 183.0},
      .dark =  {.face = 163.0, .top = 165.0, .side = 163.0, .bottom = 42.0},
      .onBits = TTK_STATE_SELECTED, .offBits = 0
    },
    {
      .light = {.face = 255.0, .top = 215.0, .side = 211.0, .bottom = 183.0},
      .dark =  {.face = 108.0, .top = 129.0, .side = 108.0, .bottom = 47.0},
      .onBits = 0, .offBits = 0
    },
  }
};

static const ButtonDesign entryDesign = {
  .radius = 0.0,
  .palettes = {
    {
      .light = {.face = 256.0, .top = 198.0, .side = 198.0, .bottom = 198.0},
      .dark =  {.face = 256.0,  .top = 66.0,  .side = 66.0,  .bottom = 84.0},
      .onBits = 0, .offBits = 0
    }
  }
};

static const ButtonDesign searchDesign = {
  .radius = 3.5,
  .palettes = {
    {
      .light = {.face = 256.0, .top = 198.0, .side = 198.0, .bottom = 198.0},
      .dark =  {.face = 256.0,  .top = 66.0,  .side = 66.0,  .bottom = 84.0},
      .onBits = 0, .offBits = 0
    }
  }
};

static const ButtonDesign comboDesign = {
  .radius = 4.0,
  .palettes = {
    {
      .light = {.face = 256.0, .top = 190.0, .side = 190.0, .bottom = 190.0},
      .dark =  {.face = 256.0,  .top = 66.0,  .side = 66.0,  .bottom = 90.0},
      .onBits = 0, .offBits = 0
    }
  }
};

static const ButtonDesign sliderDesign = {
  .radius = 8.0,
  .palettes = {
    {
      .light = {.face = 242.0, .top = 189.0, .side = 198.0, .bottom = 199.0},
      .dark =  {.face = 80.0,  .top = 84.0,  .side = 88.0,  .bottom = 60.0},
      .onBits = TTK_STATE_DISABLED, .offBits = 0
    },
    {
      .light = {.face = 255.0, .top = 165.0, .side = 184.0, .bottom = 184.0},
      .dark =  {.face = 205.0, .top = 205.0, .side = 205.0, .bottom = 198.0},
      .onBits = 0, .offBits = 0
    }
  }
};


/*
 * Table mapping Tk states to Appearance manager ThemeStates
 */

static const Ttk_StateTable ThemeStateTable[] = {
    {kThemeStateActive, TTK_STATE_ALTERNATE | TTK_STATE_BACKGROUND, 0},
    {kThemeStateUnavailable, TTK_STATE_DISABLED, 0},
    {kThemeStatePressed, TTK_STATE_PRESSED, 0},
    {kThemeStateInactive, TTK_STATE_BACKGROUND, 0},
    {kThemeStateUnavailableInactive, TTK_STATE_DISABLED | TTK_STATE_BACKGROUND, 0},
    {kThemeStateActive, 0, 0}

    /* Others:
     * The kThemeStatePressedUp and kThemeStatePressedDown bits indicate
     * which of the two segments of an IncDec button is being pressed.
     * We don't use these. kThemeStateRollover roughly corresponds to
     * TTK_STATE_ACTIVE, but does not do what we want with the help button.
     *
     * {kThemeStatePressedUp, 0, 0},
     * {kThemeStatePressedDown, 0, 0}
     * {kThemeStateRollover, TTK_STATE_ACTIVE, 0},
     */
};

/*
 * Translation between Ttk and HIToolbox.
 */

static const Ttk_StateTable ButtonValueTable[] = {
    {kThemeButtonOff, TTK_STATE_ALTERNATE | TTK_STATE_BACKGROUND, 0},
    {kThemeButtonMixed, TTK_STATE_ALTERNATE, 0},
    {kThemeButtonOn, TTK_STATE_SELECTED, 0},
    {kThemeButtonOff, 0, 0}
};

static const Ttk_StateTable ButtonAdornmentTable[] = {
    {kThemeAdornmentNone, TTK_STATE_ALTERNATE | TTK_STATE_BACKGROUND, 0},
    {kThemeAdornmentDefault | kThemeAdornmentFocus,
     TTK_STATE_ALTERNATE | TTK_STATE_FOCUS, 0},
    {kThemeAdornmentFocus, TTK_STATE_FOCUS, 0},
    {kThemeAdornmentDefault, TTK_STATE_ALTERNATE, 0},
    {kThemeAdornmentNone, 0, 0}
};

/*
 * Our enums for button styles not known to HIToolbox.
 */

#define TkGradientButton    0x8001
#define TkRoundedRectButton 0x8002
#define TkRecessedButton    0x8003
#define TkInlineButton      0x8004
#define TkSidebarButton     0x8005
/*
 * The struct passed as clientData when drawing Ttk buttons.
 */

typedef struct {
    ThemeButtonKind kind;
    ThemeMetric heightMetric;
    ThemeMetric widthMetric;
} ThemeButtonParams;

static ThemeButtonParams
    PushButtonParams =  {kThemePushButton, kThemeMetricPushButtonHeight,
			 NoThemeMetric},
    CheckBoxParams =    {kThemeCheckBox, kThemeMetricCheckBoxHeight,
			 NoThemeMetric},
    RadioButtonParams = {kThemeRadioButton, kThemeMetricRadioButtonHeight,
			 NoThemeMetric},
    BevelButtonParams = {kThemeRoundedBevelButton, NoThemeMetric, NoThemeMetric},
    PopupButtonParams = {kThemePopupButton, kThemeMetricPopupButtonHeight,
			 NoThemeMetric},
    DisclosureParams =  {kThemeDisclosureButton,
			 kThemeMetricDisclosureTriangleHeight,
			 kThemeMetricDisclosureTriangleWidth},
    DisclosureButtonParams = {kThemeArrowButton,
			      kThemeMetricSmallDisclosureButtonHeight,
			      kThemeMetricSmallDisclosureButtonWidth},
    HelpButtonParams = {kThemeRoundButtonHelp, kThemeMetricRoundButtonSize,
			kThemeMetricRoundButtonSize},
    ListHeaderParams = {kThemeListHeaderButton, kThemeMetricListHeaderHeight,
			NoThemeMetric},
    GradientButtonParams = {TkGradientButton, NoThemeMetric, NoThemeMetric},
    RoundedRectButtonParams = {TkRoundedRectButton, kThemeMetricPushButtonHeight,
			       NoThemeMetric},
    RecessedButtonParams = {TkRecessedButton, kThemeMetricPushButtonHeight,
 			    NoThemeMetric},
    SidebarButtonParams = {TkSidebarButton, NoThemeMetric, NoThemeMetric},
    InlineButtonParams = {TkInlineButton,  kThemeMetricPushButtonHeight, NoThemeMetric};

    /*
     * Others: kThemeDisclosureRight, kThemeDisclosureDown,
     * kThemeDisclosureLeft
     */

/*
 *  The struct passed as clientData when drawing Ttk Entry widgets.
 */

typedef struct {
    HIThemeFrameKind kind;
    ThemeMetric heightMetric;
    ThemeMetric widthMetric;
} ThemeFrameParams;

static ThemeFrameParams
    EntryFieldParams = {kHIThemeFrameTextFieldSquare, NoThemeMetric, NoThemeMetric},
    SearchboxFieldParams = {kHIThemeFrameTextFieldRound, NoThemeMetric, NoThemeMetric};

/*
 * If we try to draw a rounded rectangle with too large of a radius, the Core
 * Graphics library will sometimes raise a fatal exception.  This macro
 * protects against this by returning if the width or height is less than
 * twice the radius.  Presumably this only happens when a widget has not yet
 * been configured and has size 1x1, so there is nothing to draw anyway.
 */

#define CHECK_RADIUS(radius, bounds)                                                 \
    if ((radius) > (bounds).size.width / 2 || (radius) > (bounds).size.height / 2) { \
        return;                                                                      \
    }

/*
 * The spinbox widget needs to draw the two arrows in different colors when
 * one half or the other is being pressed, but the menubutton always draws
 * them in the same color.  This constant is used to distinguish those two
 * situations.
 */

#define BOTH_ARROWS 1 << 30

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
