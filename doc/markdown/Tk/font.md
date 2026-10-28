---
CommandName: font
title: font
ManualSection: n
Version: 8.0
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - options(n)
Keywords:
 - font
Copyright:
 - Copyright (c) 1996 Sun Microsystems, Inc.
 - Copyright (c) 2006-2007 Daniel A. Steffen <das@users.sourceforge.net>
---

# Name

font - Create and inspect fonts.

# Synopsis

::: {.synopsis} :::
[font]{.cmd} [option]{.arg} [arg]{.optdot}
:::

# Description

The **font** command provides several facilities for dealing with fonts, such as defining named fonts and inspecting the actual attributes of a font.  The command has several different forms, determined by the first argument.  The following forms are currently supported:

[font]{.cmd} [actual]{.sub} [font]{.arg} [[-displayof]{.lit} [window]{.arg}]{.optarg} [option]{.optarg} [--]{.optlit} [char]{.optarg}
: Returns information about the actual attributes that are obtained when *font* is used on *window*'s display; the actual attributes obtained may differ from the attributes requested due to platform-dependent limitations, such as the availability of font families and point sizes. *font* is a font description; see [Font descriptions] below.  If the *window* argument is omitted, it defaults to the main window.  If *option* is specified, returns the value of that attribute; if it is omitted, the return value is a list of all the attributes and their values. See [Font options] below for a list of the possible attributes.  If the *char* argument is supplied, it must be a single character. The font attributes returned will be those of the specific font used to render that character, which will be different from the base font if the base font does not contain the given character.  If *char* may be a hyphen, it should be preceded by **--** to distinguish it from a misspelled *option*.

[font]{.cmd} [configure]{.sub} [fontname]{.arg} [option]{.optarg} [value option value]{.optdot}
: Query or modify the desired attributes for the named font called *fontname*.  If no *option* is specified, returns a list describing all the options and their values for *fontname*.  If a single *option* is specified with no *value*, then returns the current value of that attribute.  If one or more *option*-*value* pairs are specified, then the command modifies the given named font to have the given values; in this case, all widgets using that font will redisplay themselves using the new attributes for the font.  See [Font options] below for a list of the possible attributes.

    Note that on Aqua/macOS, the system fonts (see [Platform specific fonts] below) may not be actually altered because they are implemented by the system theme. To achieve the effect of modification, use **font actual** to get their configuration and **font create** to synthesize a copy of the font which can be modified.

[font]{.cmd} [create]{.sub} [fontname]{.optarg} [option value]{.optdot}
: Creates a new named font and returns its name.  *fontname* specifies the name for the font; if it is omitted, then Tk generates a new name of the form **font***x*, where *x* is an integer.  There may be any number of *option*-*value* pairs, which provide the desired attributes for the new named font.  See [Font options] below for a list of the possible attributes.

[font]{.cmd} [delete]{.sub} [fontname]{.arg} [fontname]{.optdot}
: Delete the specified named fonts.  If there are widgets using the named font, the named font will not actually be deleted until all the instances are released.  Those widgets will continue to display using the last known values for the named font.  If a deleted named font is subsequently recreated with another call to **font create**, the widgets will use the new named font and redisplay themselves using the new attributes of that font.

[font]{.cmd} [families]{.sub} [[-displayof]{.lit} [window]{.arg}]{.optarg}
: The return value is a list of the case-insensitive names of all font families that exist on *window*'s display.  If the *window* argument is omitted, it defaults to the main window.

[font]{.cmd} [measure]{.sub} [font]{.arg} [[-displayof]{.lit} [window]{.arg}]{.optarg} [text]{.arg}
: Measures the amount of space the string *text* would use in the given *font* when displayed in *window*.  *font* is a font description; see [Font descriptions] below.  If the *window* argument is omitted, it defaults to the main window.  The return value is the total width in pixels of *text*, not including the extra pixels used by highly exaggerated characters such as cursive "f". If the string contains newlines or tabs, those characters are not expanded or treated specially when measuring the string.

[font]{.cmd} [metrics]{.sub} [font]{.arg} [[-displayof]{.lit} [window]{.arg}]{.optarg} [option]{.optarg}
: Returns information about the metrics (the font-specific data), for *font* when it is used on *window*'s display.  *font* is a font description; see [Font descriptions] below.  If the *window* argument is omitted, it defaults to the main window.  If *option* is specified, returns the value of that metric; if it is omitted, the return value is a list of all the metrics and their values.  See [Font metrics] below for a list of the possible metrics.

[font]{.cmd} [names]{.sub}
: The return value is a list of all the named fonts that are currently defined.


# Font descriptions

The following formats are accepted as a font description anywhere *font* is specified as an argument above; these same forms are also permitted when specifying the **-font** option for widgets.

\[1\] *fontname*
: The name of a named font, created using the **font create** command.  When a widget uses a named font, it is guaranteed that this will never cause an error, as long as the named font exists, no matter what potentially invalid or meaningless set of attributes the named font has.  If the named font cannot be displayed with exactly the specified attributes, some other close font will be substituted automatically.

\[2\] *systemfont*
: The platform-specific name of a font, interpreted by the graphics server. This also includes, under X, an XLFD (see \[4\]) for which a single "**\***" character was used to elide more than one field in the middle of the name.  See [Platform specific fonts] for a list of the system fonts.

\[3\] *family* ?*size*? ?*style*? ?*style ...*?
: A properly formed list whose first element is the desired font *family* and whose optional second element is the desired *size*. The interpretation of the *size* attribute follows the same rules described for **-size** in [Font options] below.  Any additional optional arguments following the *size* are font *style*s.  Possible values for the *style* arguments are as follows:

    **normal**	**bold**	**roman**	**italic** **underline**	**overstrike**
:::

\[4\] X-font names (XLFD)
: A Unix-centric font name of the form *-foundry-family-weight-slant-setwidth-addstyle-pixel-point-resx-resy-spacing-width-charset-encoding*. The "**\***" character may be used to skip individual fields that the user does not care about.  There must be exactly one "**\***" for each field skipped, except that a "**\***" at the end of the XLFD skips any remaining fields; the shortest valid XLFD is simply "**\***", signifying all fields as defaults.  Any fields that were skipped are given default values.  For compatibility, an XLFD always chooses a font of the specified pixel size (not point size); although this interpretation is not strictly correct, all existing applications using XLFDs assumed that one "point" was in fact one pixel and would display incorrectly (generally larger) if the correct size font were actually used.

\[5\] *option value* ?*option value ...*?
: A properly formed list of *option*-*value* pairs that specify the desired attributes of the font, in the same format used when defining a named font; see [Font options] below.


When font description *font* is used, the system attempts to parse the description according to each of the above five rules, in the order specified. Cases \[1\] and \[2\] must match the name of an existing named font or of a system font.  Cases \[3\], \[4\], and \[5\] are accepted on all platforms and the closest available font will be used.  In some situations it may not be possible to find any close font (e.g., the font family was a garbage value); in that case, some system-dependent default font is chosen.  If the font description does not match any of the above patterns, an error is generated.

# Font metrics

The following options are used by the **font metrics** command to query font-specific data determined when the font was created.  These properties are for the whole font itself and not for individual characters drawn in that font.  In the following definitions, the "baseline" of a font is the horizontal line where the bottom of most letters line up; certain letters, such as lower-case "g" stick below the baseline.

[-ascent]{.lit}
: The amount in pixels that the tallest letter sticks up above the baseline of the font, plus any extra blank space added by the designer of the font.

[-descent]{.lit}
: The largest amount in pixels that any letter sticks down below the baseline of the font, plus any extra blank space added by the designer of the font.

[-linespace]{.lit}
: Returns how far apart vertically in pixels two lines of text using the same font should be placed so that none of the characters in one line overlap any of the characters in the other line.  This is generally the sum of the ascent above the baseline line plus the descent below the baseline.

[-fixed]{.lit}
: Returns a boolean flag that is "**1**" if this is a fixed-width font, where each normal character is the same width as all the other characters, or is "**0**" if this is a proportionally-spaced font, where individual characters have different widths.  The widths of control characters, tab characters, and other non-printing characters are not included when calculating this value.


# Font options

The following options are supported on all platforms, and are used when constructing a named font or when specifying a font using style \[5\] as above:

[-family]{.lit} [name]{.arg}
: The case-insensitive font family name.  Tk guarantees to support the font families named **Courier** (a monospaced "typewriter" font), **Times** (a serifed "newspaper" font), and **Helvetica** (a sans-serif "European" font).  The most closely matching native font family will automatically be substituted when one of the above font families is used. The *name* may also be the name of a native, platform-specific font family; in that case it will work as desired on one platform but may not display correctly on other platforms.  If the family is unspecified or unrecognized, a platform-specific default font will be chosen.

[-size]{.lit} [size]{.arg}
: The desired size of the font.  If the *size* argument is a positive number, it is interpreted as a size in points.  If *size* is a negative number, its absolute value is interpreted as a size in pixels.  If a font cannot be displayed at the specified size, a nearby size will be chosen.  If *size* is unspecified or zero, a platform-dependent default size will be chosen.

    Sizes should normally be specified in points so the application will remain the same ruler size on the screen, even when changing screen resolutions or moving scripts across platforms.  However, specifying pixels is useful in certain circumstances such as when a piece of text must line up with respect to a fixed-size bitmap.  The mapping between points and pixels is set when the application starts, based on properties of the installed monitor, but it can be overridden by calling the [tk scaling][tk] command.

[-weight]{.lit} [weight]{.arg}
: The nominal thickness of the characters in the font.  The value **normal** specifies a normal weight font, while **bold** specifies a bold font.  The closest available weight to the one specified will be chosen.  The default weight is **normal**.

[-slant]{.lit} [slant]{.arg}
: The amount the characters in the font are slanted away from the vertical.  Valid values for slant are **roman** and **italic**. A roman font is the normal, upright appearance of a font, while an italic font is one that is tilted some number of degrees from upright. The closest available slant to the one specified will be chosen. The default slant is **roman**.

[-underline]{.lit} [boolean]{.arg}
: The value is a boolean flag that specifies whether characters in this font should be underlined.  The default value for underline is **false**.

[-overstrike]{.lit} [boolean]{.arg}
: The value is a boolean flag that specifies whether a horizontal line should be drawn through the middle of characters in this font.  The default value for overstrike is **false**.


# Standard fonts

The following named fonts are supported on all systems, and default to values that match appropriate system defaults.

**TkDefaultFont**
: This font is the default for all GUI items not otherwise specified.

**TkTextFont**
: This font should be used for user text in entry widgets, listboxes etc.

**TkFixedFont**
: This font is the standard fixed-width font.

**TkMenuFont**
: This font is used for menu items.

**TkHeadingFont**
: This font should be used for column headings in lists and tables.

**TkCaptionFont**
: This font should be used for window and dialog caption bars.

**TkSmallCaptionFont**
: This font should be used for captions on contained windows or tool dialogs.

**TkIconFont**
: This font should be used for icon captions.

**TkTooltipFont**
: This font should be used for tooltip windows (transient information windows).


It is *not* advised to change these fonts, as they may be modified by Tk itself in response to system changes. Instead, make a copy of the font and modify that.

# Platform specific fonts

The following system fonts are supported:

**X Windows**
: All valid X font names, including those listed by xlsfonts(1), are available.

**MS Windows**
: The following fonts are supported, and are mapped to the user's style defaults.

    **system**	**ansi**	**device** **systemfixed**	**ansifixed**	**oemfixed**
:::

**macOS**
: The following fonts are supported, and are mapped to the user's style defaults.

    **system**	**application**	**menu**
:::

    Additionally, the following named fonts provide access to the Aqua theme fonts:

    **systemSystemFont**	**systemEmphasizedSystemFont** **systemSmallSystemFont**	**systemSmallEmphasizedSystemFont** **systemApplicationFont**	**systemLabelFont** **systemViewsFont**	**systemMenuTitleFont** **systemMenuItemFont**	**systemMenuItemMarkFont** **systemMenuItemCmdKeyFont**	**systemWindowTitleFont** **systemPushButtonFont**	**systemUtilityWindowTitleFont** **systemAlertHeaderFont**	**systemToolbarFont** **systemMiniSystemFont**	**systemDetailSystemFont** **systemDetailEmphasizedSystemFont**
:::


# Example

Fill a text widget with lots of font demonstrators, one for every font family installed on your system:

```
pack [text .t -wrap none] -fill both -expand 1
set count 0
set tabwidth 0
foreach family [lsort -dictionary [font families]] {
    .t tag configure f[incr count] -font [list $family 10]
    .t insert end ${family}:\t {} \
            "This is a simple sampler\n" f$count
    set w [font measure [.t cget -font] ${family}:]
    if {$w + 5 > $tabwidth} {
        set tabwidth [expr {$w + 5}]
        .t configure -tabs $tabwidth
    }
}
```


[tk]: tk.md

