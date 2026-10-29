---
CommandName: text
title: text
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - entry(n)
 - scrollbar(n)
Keywords:
 - text
 - widget
 - tkvars
Copyright:
 - Copyright (c) 1992 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

text, tk\_textCopy, tk\_textCut, tk\_textPaste - Create and manipulate 'text' hypertext editing widgets

# Synopsis

::: {.synopsis} :::
[text]{.cmd} [pathName]{.arg} [options]{.optarg}
[tk\_textCopy]{.cmd} [pathName]{.arg}
[tk\_textCut]{.cmd} [pathName]{.arg}
[tk\_textPaste]{.cmd} [pathName]{.arg}
:::

# Standard options

::: {.standardoptions} :::
-background
-borderwidth
-cursor
-exportselection
-font
-foreground
-highlightbackground
-highlightcolor
-highlightthickness
-insertbackground
-insertborderwidth
-insertofftime
-insertontime
-insertwidth
-padx
-pady
-relief
-selectbackground
-selectborderwidth
-selectforeground
-setgrid
-takefocus
-xscrollcommand
-yscrollcommand
:::

See the [options] manual entry for details on the standard options.

# Widget-specific options

[-autoseparators]{.cmdline} [autoSeparators]{.dbname} [AutoSeparators]{.dbclass}
: Specifies a boolean that says whether separators are automatically inserted in the undo stack. Only meaningful when the **-undo** option is true.

[-blockcursor]{.cmdline} [blockCursor]{.dbname} [BlockCursor]{.dbclass}
: Specifies a boolean that says whether the blinking insertion cursor should be drawn as a character-sized rectangular block. If false (the default) a thin vertical line is used for the insertion cursor.

[-endline]{.cmdline} [endLine]{.dbname} [EndLine]{.dbclass}
: Specifies an integer line index representing the line of the underlying textual data store that should be just after the last line contained in the widget. This allows a text widget to reflect only a portion of a larger piece of text. Instead of an integer, the empty string can be provided to this configuration option, which will configure the widget to end at the very last line in the textual data store.

[-height]{.cmdline} [height]{.dbname} [Height]{.dbclass}
: Specifies the desired height for the window, in units of characters in the font given by the **-font** option. Must be at least one.

[-inactiveselectbackground]{.cmdline} [inactiveSelectBackground]{.dbname} [Foreground]{.dbclass}
: Specifies the colour to use for the selection (the **sel** tag) when the window does not have the input focus. If empty, **{}**, then no selection is shown when the window does not have the focus.

[-insertunfocussed]{.cmdline} [insertUnfocussed]{.dbname} [InsertUnfocussed]{.dbclass}
: [Specifies how to display the insertion cursor when the widget does not have the focus. Must be **none** (the default) which means to not display the cursor, **hollow** which means to display a hollow box, or **solid** which means to display a solid box. Note that **hollow** and **solid** will appear very similar when the **-blockcursor** option is false.]{version="8.6"}

[-maxundo]{.cmdline} [maxUndo]{.dbname} [MaxUndo]{.dbclass}
: Specifies the maximum number of compound undo actions on the undo stack. A zero or a negative value imply an unlimited undo stack.

[-spacing1]{.cmdline} [spacing1]{.dbname} [Spacing1]{.dbclass}
: Requests additional space above each text line in the widget, using any of the standard forms for screen distances. If a line wraps, this option only applies to the first line on the display. This option may be overridden with **-spacing1** options in tags.

[-spacing2]{.cmdline} [spacing2]{.dbname} [Spacing2]{.dbclass}
: For lines that wrap (so that they cover more than one line on the display) this option specifies additional space to provide between the display lines that represent a single line of text. The value may have any of the standard forms for screen distances. This option may be overridden with **-spacing2** options in tags.

[-spacing3]{.cmdline} [spacing3]{.dbname} [Spacing3]{.dbclass}
: Requests additional space below each text line in the widget, using any of the standard forms for screen distances. If a line wraps, this option only applies to the last line on the display. This option may be overridden with **-spacing3** options in tags.

[-startline]{.cmdline} [startLine]{.dbname} [StartLine]{.dbclass}
: Specifies an integer line index representing the first line of the underlying textual data store that should be contained in the widget. This allows a text widget to reflect only a portion of a larger piece of text. Instead of an integer, the empty string can be provided to this configuration option, which will configure the widget to start at the very first line in the textual data store.

[-state]{.cmdline} [state]{.dbname} [State]{.dbclass}
: Specifies one of two states for the text: **normal** or **disabled**. If the text is disabled then characters may not be inserted or deleted and no insertion cursor will be displayed, even if the input focus is in the widget.

[-tabs]{.cmdline} [tabs]{.dbname} [Tabs]{.dbclass}
: Specifies a set of tab stops for the window. The option's value consists of a list of screen distances giving the positions of the tab stops, each of which is a distance relative to the left edge of the widget (excluding borders, padding, etc). Each position may optionally be followed in the next list element by one of the keywords **left**, **right**, **center**, or **numeric**, which specifies how to justify text relative to the tab stop. **Left** is the default; it causes the text following the tab character to be positioned with its left edge at the tab position. **Right** means that the right edge of the text following the tab character is positioned at the tab position, and **center** means that the text is centered at the tab position. **Numeric** means that the decimal point in the text is positioned at the tab position; if there is no decimal point then the least significant digit of the number is positioned just to the left of the tab position; if there is no number in the text then the text is right-justified at the tab position. For example, "**-tabs {2c left 4c 6c center}**" creates three tab stops at two-centimeter intervals; the first two use left justification and the third uses center justification.

    If the list of tab stops does not have enough elements to cover all of the tabs in a text line, then Tk extrapolates new tab stops using the spacing and alignment from the last tab stop in the list. Tab distances must be strictly positive, and must always increase from one tab stop to the next (if not, an error is thrown). The value of the **-tabs** option may be overridden by **-tabs** options in tags.

    If no **-tabs** option is specified, or if it is specified as an empty list, then Tk uses default tabs spaced every eight (average size) characters. To achieve a different standard spacing, for example every 4 characters, simply configure the widget with "**-tabs \\"\[expr {4 \* \[font measure $font 0\]}\] left\\" -tabstyle wordprocessor**".

[-tabstyle]{.cmdline} [tabStyle]{.dbname} [TabStyle]{.dbclass}
: Specifies how to interpret the relationship between tab stops on a line and tabs in the text of that line. The value must be **tabular** (the default) or **wordprocessor**. Note that tabs are interpreted as they are encountered in the text. If the tab style is **tabular** then the *n*'th tab character in the line's text will be associated with the *n*'th tab stop defined for that line. If the tab character's x coordinate falls to the right of the *n*'th tab stop, then a gap of a single space will be inserted as a fallback. If the tab style is **wordprocessor** then any tab character being laid out will use (and be defined by) the first tab stop to the right of the preceding characters already laid out on that line. The value of the **-tabstyle** option may be overridden by **-tabstyle** options in tags.

[-undo]{.cmdline} [undo]{.dbname} [Undo]{.dbclass}
: Specifies a boolean that says whether the undo mechanism is active or not.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: Specifies the desired width for the window in units of characters in the font given by the **-font** option. If the font does not have a uniform width then the width of the character "0" is used in translating from character units to screen units.

[-wrap]{.cmdline} [wrap]{.dbname} [Wrap]{.dbclass}
: Specifies how to handle lines in the text that are too long to be displayed in a single line of the text's window. The value must be **none** or **char** or **word**. A wrap mode of **none** means that each line of text appears as exactly one line on the screen; extra characters that do not fit on the screen are not displayed. In the other modes each line of text will be broken up into several screen lines if necessary to keep all the characters visible. In **char** mode a screen line break may occur after any character; in **word** mode a line break will only be made at word boundaries.


# Description

The **text** command creates a new window (given by the *pathName* argument) and makes it into a text widget. Additional options, described above, may be specified on the command line or in the option database to configure aspects of the text such as its default background color and relief. The **text** command returns the path name of the new window.

A text widget displays one or more lines of text and allows that text to be edited. Text widgets support four different kinds of annotations on the text, called tags, marks, embedded windows or embedded images. Tags allow different portions of the text to be displayed with different fonts and colors. In addition, Tcl commands can be associated with tags so that scripts are invoked when particular actions such as keystrokes and mouse button presses occur in particular ranges of the text. See [Tags] below for more details.

The second form of annotation consists of floating markers in the text called "marks". Marks are used to keep track of various interesting positions in the text as it is edited. See [Marks] below for more details.

The third form of annotation allows arbitrary windows to be embedded in a text widget. See [Embedded windows] below for more details.

The fourth form of annotation allows Tk images to be embedded in a text widget. See [Embedded images] below for more details.

The text widget also has a built-in undo/redo mechanism. See [The undo mechanism] below for more details.

The text widget allows for the creation of peer widgets. These are other text widgets which share the same underlying data (text, marks, tags, images, etc). See [Peer widgets] below for more details.

# Indices

Many of the widget commands for texts take one or more indices as arguments. An index is a string used to indicate a particular place within a text, such as a place to insert characters or one endpoint of a range of characters to delete. Indices have the syntax

```
base modifier modifier modifier ...
```

Where *base* gives a starting point and the *modifier*s adjust the index from the starting point (e.g. move forward or backward one character). Every index must contain a *base*, but the *modifier*s are optional. Most modifiers (as documented below) allow an optional submodifier. Valid submodifiers are **any** and **display**. If the submodifier is abbreviated, then it must be followed by whitespace, but otherwise there need be no space between the submodifier and the following *modifier*. Typically the **display** submodifier adjusts the meaning of the following *modifier* to make it refer to visual or non-elided units rather than logical units, but this is explained for each relevant case below. Lastly, where *count* is used as part of a modifier, it can be positive or negative, so "*base* - -3 lines" is perfectly valid (and equivalent to "*base* +3lines").

The *base* for an index must have one of the following forms:

*line***.***char*
: Indicates *char*'th character on line *line*. Lines are numbered from 1 for consistency with other UNIX programs that use this numbering scheme. Within a line, characters are numbered from 0. If *char* is **end** then it refers to the newline character that ends the line.

**@***x***,***y*
: Indicates the character that covers the pixel whose x and y coordinates within the text's window are *x* and *y*.

**end**
: Indicates the end of the text (the character just after the last newline).

*mark*
: Indicates the character just after the mark whose name is *mark* (see [Marks] for details).

*tag***.first**
: Indicates the first character in the text that has been tagged with *tag*. This form generates an error if no characters are currently tagged with *tag*.

*tag***.last**
: Indicates the character just after the last one in the text that has been tagged with *tag*. This form generates an error if no characters are currently tagged with *tag*.

*pathName*
: Indicates the position of the embedded window whose name is *pathName*. This form generates an error if there is no embedded window by the given name.

*imageName*
: Indicates the position of the embedded image whose name is *imageName*. This form generates an error if there is no embedded image by the given name.


If the *base* could match more than one of the above forms, such as a *mark* and *imageName* both having the same value, then the form earlier in the above list takes precedence. If modifiers follow the base index, each one of them must have one of the forms listed below. Keywords such as **chars** and **wordend** may be abbreviated as long as the abbreviation is unambiguous.

**+** *count* ?*submodifier*? **chars**
: Adjust the index forward by *count* characters, moving to later lines in the text if necessary. If there are fewer than *count* characters in the text after the current index, then set the index to the last index in the text. Spaces on either side of *count* are optional. If the **display** submodifier is given, elided characters are skipped over without being counted. If **any** is given, then all characters are counted. For historical reasons, if neither modifier is given then the count actually takes place in units of index positions (see [Indices] for details). This behaviour may be changed in a future major release, so if you need an index count, you are encouraged to use **indices** instead wherever possible.

**-** *count* ?*submodifier*? **chars**
: Adjust the index backward by *count* characters, moving to earlier lines in the text if necessary. If there are fewer than *count* characters in the text before the current index, then set the index to the first index in the text (1.0). Spaces on either side of *count* are optional. If the **display** submodifier is given, elided characters are skipped over without being counted. If **any** is given, then all characters are counted. For historical reasons, if neither modifier is given then the count actually takes place in units of index positions (see [Indices] for details). This behavior may be changed in a future major release, so if you need an index count, you are encouraged to use **indices** instead wherever possible.

**+** *count* ?*submodifier*? **indices**
: Adjust the index forward by *count* index positions, moving to later lines in the text if necessary. If there are fewer than *count* index positions in the text after the current index, then set the index to the last index position in the text. Spaces on either side of *count* are optional. Note that an index position is either a single character or a single embedded image or embedded window. If the **display** submodifier is given, elided indices are skipped over without being counted. If **any** is given, then all indices are counted; this is also the default behaviour if no modifier is given.

**-** *count* ?*submodifier*? **indices**
: Adjust the index backward by *count* index positions, moving to earlier lines in the text if necessary. If there are fewer than *count* index positions in the text before the current index, then set the index to the first index position (1.0) in the text. Spaces on either side of *count* are optional. If the **display** submodifier is given, elided indices are skipped over without being counted. If **any** is given, then all indices are counted; this is also the default behaviour if no modifier is given.

**+** *count* ?*submodifier*? **lines**
: Adjust the index forward by *count* lines, retaining the same character position within the line. If there are fewer than *count* lines after the line containing the current index, then set the index to refer to the same character position on the last line of the text. Then, if the line is not long enough to contain a character at the indicated character position, adjust the character position to refer to the last character of the line (the newline). Spaces on either side of *count* are optional. If the **display** submodifier is given, then each visual display line is counted separately. Otherwise, if **any** (or no modifier) is given, then each logical line (no matter how many times it is visually wrapped) counts just once. If the relevant lines are not wrapped, then these two methods of counting are equivalent.

**-** *count* ?*submodifier*? **lines**
: Adjust the index backward by *count* logical lines, retaining the same character position within the line. If there are fewer than *count* lines before the line containing the current index, then set the index to refer to the same character position on the first line of the text. Then, if the line is not long enough to contain a character at the indicated character position, adjust the character position to refer to the last character of the line (the newline). Spaces on either side of *count* are optional. If the **display** submodifier is given, then each visual display line is counted separately. Otherwise, if **any** (or no modifier) is given, then each logical line (no matter how many times it is visually wrapped) counts just once. If the relevant lines are not wrapped, then these two methods of counting are equivalent.

?*submodifier*? **linestart**
: Adjust the index to refer to the first index on the line. If the **display** submodifier is given, this is the first index on the display line, otherwise on the logical line.

?*submodifier*? **lineend**
: Adjust the index to refer to the last index on the line (the newline). If the **display** submodifier is given, this is the last index on the display line, otherwise on the logical line.

?*submodifier*? **wordstart**
: Adjust the index to refer to the first character of the word containing the current index. A word consists of any number of adjacent characters that are letters, digits, or underscores, or a single character that is not one of these. If the **display** submodifier is given, this only examines non-elided characters, otherwise all characters (elided or not) are examined.

?*submodifier*? **wordend**
: Adjust the index to refer to the character just after the last one of the word containing the current index. If the current index refers to the last character of the text then it is not modified. If the **display** submodifier is given, this only examines non-elided characters, otherwise all characters (elided or not) are examined.


If more than one modifier is present then they are applied in left-to-right order. For example, the index "**end - 1 chars**" refers to the next-to-last character in the text and "**insert wordstart - 1 c**" refers to the character just before the first one in the word containing the insertion cursor. Modifiers are applied one by one in this left to right order, and after each step the resulting index is constrained to be a valid index in the text widget. So, for example, the index "**1.0 -1c +1c**" refers to the index "**2.0**".

Where modifiers result in index changes by display lines, display chars or display indices, and the *base* refers to an index inside an elided tag, that base index is considered to be equivalent to the first following non-elided index.

# Tags

The first form of annotation in text widgets is a tag. A tag is a textual string that is associated with some of the characters in a text. Tags may contain arbitrary characters, but it is probably best to avoid using the characters " " (space), **+**, or **-**: these characters have special meaning in indices, so tags containing them cannot be used as indices. There may be any number of tags associated with characters in a text. Each tag may refer to a single character, a range of characters, or several ranges of characters. An individual character may have any number of tags associated with it.

A priority order is defined among tags, and this order is used in implementing some of the tag-related functions described below. When a tag is defined (by associating it with characters or setting its display options or binding commands to it), it is given a priority higher than any existing tag. The priority order of tags may be redefined using the "*pathName* **tag raise**" and "*pathName* **tag lower**" widget commands.

Tags serve three purposes in text widgets. First, they control the way information is displayed on the screen. By default, characters are displayed as determined by the **-background**, **-font**, and **-foreground** options for the text widget. However, display options may be associated with individual tags using the "*pathName* **tag configure**" widget command. If a character has been tagged, then the display options associated with the tag override the default display style. The following options are currently supported for tags:

[-background]{.lit} [color]{.arg}
: *Color* specifies the background color to use for characters associated with the tag. It may have any of the forms accepted by [Tk\_GetColor][GetColor].

[-bgstipple]{.lit} [bitmap]{.arg}
: *Bitmap* specifies a bitmap that is used as a stipple pattern for the background. It may have any of the forms accepted by [Tk\_GetBitmap][GetBitmap]. If *bitmap* has not been specified, or if it is specified as an empty string, then a solid fill will be used for the background.

[-borderwidth]{.lit} [pixels]{.arg}
: *Pixels* specifies the width of a border to draw around the tag using any of the forms accepted by [Tk\_GetPixels][GetPixels]. This option should be used in conjunction with the **-relief** option to provide the desired border.

[-elide]{.lit} [boolean]{.arg}
: *Elide* specifies whether the data should be elided. Elided data (characters, images, embedded windows, etc.) is not displayed and takes no space on screen, but further on behaves just as normal data.

[-fgstipple]{.lit} [bitmap]{.arg}
: *Bitmap* specifies a bitmap that is used as a stipple pattern when drawing text and other foreground information such as underlines. It may have any of the forms accepted by [Tk\_GetBitmap][GetBitmap]. If *bitmap* has not been specified, or if it is specified as an empty string, then a solid fill will be used.

[-font]{.lit} [fontName]{.arg}
: *FontName* is the name of a font to use for drawing characters. It may have any of the forms accepted by [Tk\_GetFont][GetFont].

[-foreground]{.lit} [color]{.arg}
: *Color* specifies the color to use when drawing text and other foreground information such as underlines. It may have any of the forms accepted by [Tk\_GetColor][GetColor].

[-justify]{.lit} [justify]{.arg}
: If the first non-elided character of a display line has a tag for which this option has been specified, then *justify* determines how to justify the line. It must be one of **left**, **right**, or **center**. If a line wraps, then the justification for each line on the display is determined by the first non-elided character of that display line.

[-lmargin1]{.lit} [pixels]{.arg}
: If the first non-elided character of a text line has a tag for which this option has been specified, then *pixels* specifies how much the line should be indented from the left edge of the window. *Pixels* may have any of the standard forms for screen distances. If a line of text wraps, this option only applies to the first line on the display; the **-lmargin2** option controls the indentation for subsequent lines.

[-lmargin2]{.lit} [pixels]{.arg}
: If the first non-elided character of a display line has a tag for which this option has been specified, and if the display line is not the first for its text line (i.e., the text line has wrapped), then *pixels* specifies how much the line should be indented from the left edge of the window. *Pixels* may have any of the standard forms for screen distances. This option is only used when wrapping is enabled, and it only applies to the second and later display lines for a text line.

[-lmargincolor]{.lit} [color]{.arg}
: *Color* specifies the background color to use in regions that do not contain characters because they are indented by **-lmargin1** or **-lmargin2**. It may have any of the forms accepted by [Tk\_GetColor][GetColor]. If *color* has not been specified, or if it is specified as an empty string, then the color used is specified by the **-background** tag option (or, if this is also unspecified, by the **-background** widget option).

[-locale]{.lit} [locale]{.arg}
: The locale which is used for determining word and character boundaries within the text. The default is "C".

[-offset]{.lit} [pixels]{.arg}
: *Pixels* specifies an amount by which the text's baseline should be offset vertically from the baseline of the overall line, in pixels. For example, a positive offset can be used for superscripts and a negative offset can be used for subscripts. *Pixels* may have any of the standard forms for screen distances.

[-overstrike]{.lit} [boolean]{.arg}
: Specifies whether or not to draw a horizontal rule through the middle of characters. *Boolean* may have any of the forms accepted by [Tcl\_GetBoolean][GetInt].

[-overstrikefg]{.lit} [color]{.arg}
: *Color* specifies the color to use when displaying the overstrike. It may have any of the forms accepted by [Tk\_GetColor][GetColor]. If *color* has not been specified, or if it is specified as an empty string, then the color specified by the **-foreground** tag option is used.

[-relief]{.lit} [relief]{.arg}
: *Relief* specifies the relief style to use for drawing the border, in any of the forms accepted by [Tk\_GetRelief][GetRelief]. This option is used in conjunction with the **-borderwidth** option to enable to the desired border appearance.

[-rmargin]{.lit} [pixels]{.arg}
: If the first non-elided character of a display line has a tag for which this option has been specified, then *pixels* specifies how wide a margin to leave between the end of the line and the right edge of the window. *Pixels* may have any of the standard forms for screen distances. This option is only used when wrapping is enabled. If a text line wraps, the right margin for each line on the display is determined by the first non-elided character of that display line.

[-rmargincolor]{.lit} [color]{.arg}
: *Color* specifies the background color to use in regions that do not contain characters because they are indented by **-rmargin**. It may have any of the forms accepted by [Tk\_GetColor][GetColor]. If *color* has not been specified, or if it is specified as an empty string, then the color used is specified by the **-background** tag option (or, if this is also unspecified, by the **-background** widget option).

[-selectbackground]{.lit} [color]{.arg}
: *Color* specifies the background color to use when displaying selected items. It may have any of the forms accepted by [Tk\_GetColor][GetColor]. If *color* has not been specified, or if it is specified as an empty string, then the color specified by the **-background** tag option is used.

[-selectforeground]{.lit} [color]{.arg}
: *Color* specifies the foreground color to use when displaying selected items. It may have any of the forms accepted by [Tk\_GetColor][GetColor]. If *color* has not been specified, or if it is specified as an empty string, then the color specified by the **-foreground** tag option is used.

[-spacing1]{.lit} [pixels]{.arg}
: *Pixels* specifies how much additional space should be left above each text line, using any of the standard forms for screen distances. If a line wraps, this option only applies to the first line on the display.

[-spacing2]{.lit} [pixels]{.arg}
: For lines that wrap, this option specifies how much additional space to leave between the display lines for a single text line. *Pixels* may have any of the standard forms for screen distances.

[-spacing3]{.lit} [pixels]{.arg}
: *Pixels* specifies how much additional space should be left below each text line, using any of the standard forms for screen distances. If a line wraps, this option only applies to the last line on the display.

[-tabs]{.lit} [tabList]{.arg}
: *TabList* specifies a set of tab stops in the same form as for the **-tabs** option for the text widget. This option only applies to a display line if it applies to the first non-elided character on that display line. If this option is specified as an empty string, it cancels the option, leaving it unspecified for the tag (the default). If the option is specified as a non-empty string that is an empty list, such as **-tags { }**, then it requests default 8-character tabs as described for the **-tags** widget option.

[-tabstyle]{.lit} [style]{.arg}
: *Style* specifies either the *tabular* or *wordprocessor* style of tabbing to use for the text widget. This option only applies to a display line if it applies to the first non-elided character on that display line. If this option is specified as an empty string, it cancels the option, leaving it unspecified for the tag (the default).

[-underline]{.lit} [boolean]{.arg}
: *Boolean* specifies whether or not to draw an underline underneath characters. It may have any of the forms accepted by [Tcl\_GetBoolean][GetInt].

[-underlinefg]{.lit} [color]{.arg}
: *Color* specifies the color to use when displaying the underline. It may have any of the forms accepted by [Tk\_GetColor][GetColor]. If *color* has not been specified, or if it is specified as an empty string, then the color specified by the **-foreground** tag option is used.

[-wrap]{.lit} [mode]{.arg}
: *Mode* specifies how to handle lines that are wider than the text's window. This option only applies to a display line if it applies to the first non-elided character on that display line. It has the same legal values as the **-wrap** option for the text widget: **none**, **char**, or **word**. If this tag option is specified, it overrides the **-wrap** option for the text widget.


If a character has several tags associated with it, and if their display options conflict, then the options of the highest priority tag are used. If a particular display option has not been specified for a particular tag, or if it is specified as an empty string, then that option will never be used; the next-highest-priority tag's option will used instead. If no tag specifies a particular display option, then the default style for the widget will be used.

The second purpose for tags is event bindings. You can associate bindings with a tag in much the same way you can associate bindings with a widget class: whenever particular X events occur on characters with the given tag, a given Tcl command will be executed. Tag bindings can be used to give behaviors to ranges of characters; among other things, this allows hypertext-like features to be implemented. For details, see the description of the "*pathName* **tag bind**" widget command below. Tag bindings are shared between all peer widgets (including any bindings for the special **sel** tag).

The third use for tags is in managing the selection. See [The selection] below. With the exception of the special **sel** tag, all tags are shared between peer text widgets, and may be manipulated on an equal basis from any such widget. The **sel** tag exists separately and independently in each peer text widget (but any tag bindings to **sel** are shared).

# Marks

The second form of annotation in text widgets is a mark. Marks are used for remembering particular places in a text. They are something like tags, in that they have names and they refer to places in the file, but a mark is not associated with particular characters. Instead, a mark is associated with the gap between two characters. Only a single position may be associated with a mark at any given time. If the characters around a mark are deleted the mark will still remain; it will just have new neighbor characters. In contrast, if the characters containing a tag are deleted then the tag will no longer have an association with characters in the file. Marks may be manipulated with the "*pathName* **mark**" widget command, and their current locations may be determined by using the mark name as an index in widget commands.

Each mark also has a "gravity", which is either **left** or **right**. The gravity for a mark specifies what happens to the mark when text is inserted at the point of the mark. If a mark has left gravity, then the mark is treated as if it were attached to the character on its left, so the mark will remain to the left of any text inserted at the mark position. If the mark has right gravity, new text inserted at the mark position will appear to the left of the mark (so that the mark remains rightmost). The gravity for a mark defaults to **right**.

The name space for marks is different from that for tags: the same name may be used for both a mark and a tag, but they will refer to different things.

Two marks have special significance. First, the mark **insert** is associated with the insertion cursor, as described under [The insertion cursor] below. Second, the mark **current** is associated with the character closest to the mouse and is adjusted automatically to track the mouse position and any changes to the text in the widget (one exception: **current** is not updated in response to mouse motions if a mouse button is down; the update will be deferred until all mouse buttons have been released). Neither of these special marks may be deleted. With the exception of these two special marks, all marks are shared between peer text widgets, and may be manipulated on an equal basis from any peer.

# Embedded windows

The third form of annotation in text widgets is an embedded window. Each embedded window annotation causes a window to be displayed at a particular point in the text. There may be any number of embedded windows in a text widget, and any widget may be used as an embedded window (subject to the usual rules for geometry management, which require the text window to be the parent of the embedded window or a descendant of its parent).

The embedded window's position on the screen will be updated as the text is modified or scrolled, and it will be mapped and unmapped as it moves into and out of the visible area of the text widget. Each embedded window occupies one unit's worth of index space in the text widget, and it may be referred to either by the name of its embedded window or by its position in the widget's index space. If the range of text containing the embedded window is deleted then the window is destroyed. Similarly if the text widget as a whole is deleted, then the window is destroyed.

Eliding an embedded window immediately after scheduling it for creation via *pathName* **window create** *index* **-create** will prevent it from being effectively created. Uneliding an elided embedded window scheduled for creation via *pathName* **window create** *index* **-create** will automatically trigger the associated creation script. After destroying an elided embedded window, the latter won't get automatically recreated.

When an embedded window is added to a text widget with the *pathName* **window create** widget command, several configuration options may be associated with it. These options may be modified later with the *pathName* **window configure** widget command. The following options are currently supported:

[-align]{.lit} [where]{.arg}
: If the window is not as tall as the line in which it is displayed, this option determines where the window is displayed in the line. *Where* must have one of the values **top** (align the top of the window with the top of the line), **center** (center the window within the range of the line), **bottom** (align the bottom of the window with the bottom of the line's area), or **baseline** (align the bottom of the window with the baseline of the line).

[-create]{.lit} [script]{.arg}
: Specifies a Tcl script that may be evaluated to create the window for the annotation. If no **-window** option has been specified for the annotation this script will be evaluated when the annotation is about to be displayed on the screen. *Script* must create a window for the annotation and return the name of that window as its result. Two substitutions will be performed in *script* before evaluation. *%W* will be substituted by the name of the parent text widget, and *%%* will be substituted by a single *%*. If the annotation's window should ever be deleted, *script* will be evaluated again the next time the annotation is displayed.

[-padx]{.lit} [pixels]{.arg}
: *Pixels* specifies the amount of extra space to leave on each side of the embedded window. It may have any of the usual forms defined for a screen distance.

[-pady]{.lit} [pixels]{.arg}
: *Pixels* specifies the amount of extra space to leave on the top and on the bottom of the embedded window. It may have any of the usual forms defined for a screen distance.

[-stretch]{.lit} [boolean]{.arg}
: If the requested height of the embedded window is less than the height of the line in which it is displayed, this option can be used to specify whether the window should be stretched vertically to fill its line. If the **-pady** option has been specified as well, then the requested padding will be retained even if the window is stretched.

[-window]{.lit} [pathName]{.arg}
: Specifies the name of a window to display in the annotation. Note that if a *pathName* has been set, then later configuring a window to the empty string will not delete the widget corresponding to the old *pathName*. Rather it will remove the association between the old *pathName* and the text widget. If multiple peer widgets are in use, it is usually simpler to use the **-create** option if embedded windows are desired in each peer.


# Embedded images

The final form of annotation in text widgets is an embedded image. Each embedded image annotation causes an image to be displayed at a particular point in the text. There may be any number of embedded images in a text widget, and a particular image may be embedded in multiple places in the same text widget.

The embedded image's position on the screen will be updated as the text is modified or scrolled. Each embedded image occupies one unit's worth of index space in the text widget, and it may be referred to either by its position in the widget's index space, or the name it is assigned when the image is inserted into the text widget with *pathName* **image create**. If the range of text containing the embedded image is deleted then that copy of the image is removed from the screen.

Eliding an embedded image immediately after scheduling it for creation via *pathName* **image create** *index* **-create** will prevent it from being effectively created. Uneliding an elided embedded image scheduled for creation via *pathName* **image create** *index* **-create** will automatically trigger the associated creation script. After destroying an elided embedded image, the latter won't get automatically recreated.

When an embedded image is added to a text widget with the *pathName* **image create** widget command, a name unique to this instance of the image is returned. This name may then be used to refer to this image instance. The name is taken to be the value of the **-name** option (described below). If the **-name** option is not provided, the **-image** name is used instead. If the *imageName* is already in use in the text widget, then **#***nn* is added to the end of the *imageName*, where *nn* is an arbitrary integer. This insures the *imageName* is unique. Once this name is assigned to this instance of the image, it does not change, even though the **-image** or **-name** values can be changed with *pathName* **image configure**.

When an embedded image is added to a text widget with the *pathName* **image create** widget command, several configuration options may be associated with it. These options may be modified later with the *pathName* **image configure** widget command. The following options are currently supported:

[-align]{.lit} [where]{.arg}
: If the image is not as tall as the line in which it is displayed, this option determines where the image is displayed in the line. *Where* must have one of the values **top** (align the top of the image with the top of the line), **center** (center the image within the range of the line), **bottom** (align the bottom of the image with the bottom of the line's area), or **baseline** (align the bottom of the image with the baseline of the line).

[-image]{.lit} [image]{.arg}
: Specifies the name of the Tk image to display in the annotation. If *image* is not a valid Tk image, then an error is returned.

[-name]{.lit} [ImageName]{.arg}
: Specifies the name by which this image instance may be referenced in the text widget. If *ImageName* is not supplied, then the name of the Tk image is used instead. If the *imageName* is already in use, *#nn* is appended to the end of the name as described above.

[-padx]{.lit} [pixels]{.arg}
: *Pixels* specifies the amount of extra space to leave on each side of the embedded image. It may have any of the usual forms defined for a screen distance.

[-pady]{.lit} [pixels]{.arg}
: *Pixels* specifies the amount of extra space to leave on the top and on the bottom of the embedded image. It may have any of the usual forms defined for a screen distance.


# The selection

Selection support is implemented via tags. If the **-exportselection** option for the text widget is true then the **sel** tag will be associated with the selection:

1. Whenever characters are tagged with **sel** the text widget will claim ownership of the selection.

2. Attempts to retrieve the selection will be serviced by the text widget, returning all the characters with the **sel** tag.

3. If the selection is claimed away by another application or by another window within this application, then the **sel** tag will be removed from all characters in the text.

4. Whenever the **sel** tag range changes a virtual event **<<Selection>>** is generated. It might also be generated when selection is affected but not actually changed. Further, multiple selection changes could happen before events can be processed leading to multiple events with the same visible selection.


The **sel** tag is automatically defined when a text widget is created, and it may not be deleted with the "*pathName* **tag delete**" widget command. Furthermore, the **-selectbackground**, **-selectborderwidth**, and **-selectforeground** options for the text widget are tied to the **-background**, **-borderwidth**, and **-foreground** options for the **sel** tag: changes in either will automatically be reflected in the other. Also the **-inactiveselectbackground** option for the text widget is used instead of **-selectbackground** when the text widget does not have the focus. This allows programmatic control over the visualization of the **sel** tag for foreground and background windows, or to have **sel** not shown at all (when **-inactiveselectbackground** is empty) for background windows. Each peer text widget has its own **sel** tag which can be separately configured and set.

# The insertion cursor

The mark named **insert** has special significance in text widgets. It is defined automatically when a text widget is created and it may not be unset with the "*pathName* **mark unset**" widget command. The **insert** mark represents the position of the insertion cursor, and the insertion cursor will automatically be drawn at this point whenever the text widget has the input focus.

# The modified flag

The text widget can keep track of changes to the content of the widget by means of the modified flag. Inserting or deleting text will set this flag. The flag can be queried, set and cleared programmatically as well. Whenever the flag changes state a **<<Modified>>** virtual event is generated. See the *pathName* **edit modified** widget command for more details.

# The undo mechanism

The text widget has an unlimited undo and redo mechanism (when the **-undo** widget option is true) which records every insert and delete action on a stack.

Boundaries (called "separators") are inserted between edit actions. The purpose of these separators is to group inserts, deletes and replaces into one compound edit action. When undoing a change everything between two separators will be undone. The undone changes are then moved to the redo stack, so that an undone edit can be redone again. The redo stack is cleared whenever new edit actions are recorded on the undo stack. The undo and redo stacks can be cleared to keep their depth under control.

Separators are inserted automatically when the **-autoseparators** widget option is true. You can insert separators programmatically as well. If a separator is already present at the top of the undo stack no other will be inserted. That means that two separators on the undo stack are always separated by at least one insert or delete action.

The **<<UndoStack>>** virtual event is generated every time the undo stack or the redo stack becomes empty or unempty.

The undo mechanism is also linked to the modified flag. This means that undoing or redoing changes can take a modified text widget back to the unmodified state or vice versa. The modified flag will be set automatically to the appropriate state. This automatic coupling does not work when the modified flag has been set by the user, until the flag has been reset again.

See below for the *pathName* **edit** widget command that controls the undo mechanism.

# Peer widgets

The text widget has a separate store of all its data concerning each line's textual contents, marks, tags, images and windows, and the undo stack.

While this data store cannot be accessed directly (i.e. without a text widget as an intermediary), multiple text widgets can be created, each of which present different views on the same underlying data. Such text widgets are known as peer text widgets.

As text is added, deleted, edited and coloured in any one widget, and as images, marks, tags are adjusted, all such changes will be reflected in all peers.

All data and markup is shared, except for a few small details. First, the **sel** tag may be set and configured (in its display style) differently for each peer. Second, each peer has its own **insert** and **current** mark positions (but all other marks are shared). Third, embedded windows, which are arbitrary other widgets, cannot be shared between peers. This means the **-window** option of embedded windows is independently set for each peer (it is advisable to use the **-create** script capabilities to allow each peer to create its own embedded windows as needed). Fourth, all of the configuration options of each peer (e.g. **-font**, etc) can be set independently, with the exception of **-undo**, **-maxundo**, **-autoseparators** (i.e. all undo, redo and modified state issues are shared).

Finally any single peer need not contain all lines from the underlying data store. When creating a peer, a contiguous range of lines (e.g. only lines 52 through 125) may be specified. This allows a peer to contain just a small portion of the overall text. The range of lines will expand and contract as text is inserted or deleted. The peer will only ever display complete lines of text (one cannot share just part of a line). If the peer's contents contracts to nothing (i.e. all complete lines in the peer widget have been deleted from another widget), then it is impossible for new lines to be inserted. The peer will simply become an empty shell on which the background can be configured, but which will never show any content (without manual reconfiguration of the start and end lines). Note that a peer which does not contain all of the underlying data store still has indices numbered from "1.0" to "end". It is simply that those indices reflect a subset of the total data, and data outside the contained range is not accessible to the peer. This means that the command *peerName* **index end** may return quite different values in different peers. Similarly, commands like *peerName* **tag ranges** will not return index ranges outside that which is meaningful to the peer. The configuration options **-startline** and **-endline** may be used to control how much of the underlying data is contained in any given text widget.

Note that peers are really peers. Deleting the "original" text widget will not cause any other peers to be deleted, or otherwise affected.

See below for the *pathName* **peer** widget command that controls the creation of peer widgets.

# Asynchronous update of line heights

In order to maintain a responsive user-experience, the text widget calculates lines metrics (line heights in pixels) asynchronously. Because of this, some commands of the text widget may return wrong results if the asynchronous calculations are not finished at the time of calling. This applies to *pathName* **count -ypixels** and *pathName* **yview**.

Again for performance reasons, it would not be appropriate to let these commands always wait for the end of the update calculation each time they are called. In most use cases of these commands a more or less inaccurate result does not really matter compared to execution speed.

In case accurate result is needed (and if the text widget is managed by a geometry manager), one can resort to *pathName* **sync** and *pathName* **pendingsync** to control the synchronization of the view of text widgets.

The **<<WidgetViewSync>>** virtual event fires when the line heights of the text widget become obsolete (due to some editing command or configuration change), and again when the internal data of the text widget are back in sync with the widget view. The detail field (%d substitution) is either true (when the widget is in sync) or false (when it is not).

*pathName* **sync**, *pathName* **pendingsync** and **<<WidgetViewSync>>** apply to each text widget independently of its peers.

Examples of use:

```
## Example 1:
# immediately complete line metrics at any cost (GUI unresponsive)
$w sync
$w yview moveto $fraction

## Example 2:
# synchronously wait for up-to-date line metrics (GUI responsive)
# before executing the scheduled command, but don't block execution flow
$w sync -command [list $w yview moveto $fraction]

## Example 3:
# init
set yud($w) 0
proc updateaction w {
set ::yud($w) 1
# any other update action here...
}
# runtime, synchronously wait for up-to-date line metrics (GUI responsive)
$w sync -command [list updateaction $w]
vwait yud($w)
$w yview moveto $fraction

## Example 4:
# init
set todo($w) {}
proc updateaction w {
foreach cmd $::todo($w) {uplevel #0 $cmd}
set todo($w) {}
}
# runtime
lappend todo($w) [list $w yview moveto $fraction]
$w sync -command [list updateaction $w]

## Example 5:
# init
set todo($w) {}
bind $w <<WidgetViewSync>> {
if {%d} {
foreach cmd $todo(%W) {eval $cmd}
set todo(%W) {}
}
}
# runtime
if {![$w pendingsync]} {
$w yview moveto $fraction
} else {
lappend todo($w) [list $w yview moveto $fraction]
}
```

# Widget command

The **text** command creates a new Tcl command whose name is the same as the path name of the text's window. This command may be used to invoke various operations on the widget. It has the following general form:

```
pathName option ?arg ...?
```

*PathName* is the name of the command, which is the same as the text widget's path name. *Option* and the *arg*s determine the exact behavior of the command. The following commands are possible for text widgets:

[pathName]{.ins} [bbox]{.sub} [index]{.arg}
: Returns a list of four elements describing the screen area of the character given by *index*. The first two elements of the list give the x and y coordinates of the upper-left corner of the area occupied by the character, and the last two elements give the width and height of the area. If the character is only partially visible on the screen, then the return value reflects just the visible part. If the character is not visible on the screen then the return value is an empty list.

[pathName]{.ins} [cget]{.sub} [option]{.arg}
: Returns the current value of the configuration option given by *option*. *Option* may have any of the values accepted by the **text** command.

[pathName]{.ins} [compare]{.sub} [index1]{.arg} [op]{.arg} [index2]{.arg}
: Compares the indices given by *index1* and *index2* according to the relational operator given by *op*, and returns 1 if the relationship is satisfied and 0 if it is not. *Op* must be one of the operators <, <=, ==, >=, >, or !=. If *op* is == then 1 is returned if the two indices refer to the same character, if *op* is < then 1 is returned if *index1* refers to an earlier character in the text than *index2*, and so on.

[pathName]{.ins} [configure]{.sub} [option]{.optarg} [value option value]{.optdot}
: Query or modify the configuration options of the widget. If no *option* is specified, returns a list describing all of the available options for *pathName* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list). If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified). If one or more *option-value* pairs are specified, then the command modifies the given widget option(s) to have the given value(s); in this case the command returns an empty string. *Option* may have any of the values accepted by the **text** command.

[pathName]{.ins} [count]{.sub} [options]{.optarg} [index1]{.arg} [index2]{.arg}
: Counts the number of relevant things between the two indices. If *index1* is after *index2*, the result will be a negative number (and this holds for each of the possible options). The actual items which are counted depend on the options given. The result is a list of integers, one for the result of each counting option given. Valid counting options are **-chars**, **-displaychars**, **-displayindices**, **-displaylines**, **-indices**, **-lines**, **-xpixels** and **-ypixels**. The default value, if no option is specified, is **-indices**. There is an additional possible option **-update** which is a modifier. If given (and if the text widget is managed by a geometry manager), then all subsequent options ensure that any possible out of date information is recalculated. This currently only has any effect for the **-ypixels** count (which, if **-update** is not given, will use the text widget's current cached value for each line). This **-update** option is obsoleted by *pathName* **sync**, *pathName* **pendingsync** and **<<WidgetViewSync>>**.  The count options are interpreted as follows:

    **-chars**
    : count all characters, whether elided or not. Do not count embedded windows or images.

    **-displaychars**
    : count all non-elided characters.

    **-displayindices**
    : count all non-elided characters, windows and images.

    **-displaylines**
    : count all display lines (i.e. counting one for each time a line wraps) from the line of the first index up to, but not including the display line of the second index. Therefore if they are both on the same display line, zero will be returned. By definition displaylines are visible and therefore this only counts portions of actual visible lines.

    **-indices**
    : count all characters and embedded windows or images (i.e. everything which counts in text-widget index space), whether they are elided or not.

    **-lines**
    : count all logical lines (irrespective of wrapping) from the line of the first index up to, but not including the line of the second index. Therefore if they are both on the same line, zero will be returned. Logical lines are counted whether they are currently visible (non-elided) or not.

    **-xpixels**
    : count the number of horizontal pixels from the first pixel of the first index to (but not including) the first pixel of the second index. To count the total desired width of the text widget (assuming wrapping is not enabled), first find the longest line and then use ".text count -xpixels \\"${line}.0\\" \\"${line}.0 lineend\\"".

    **-ypixels**
    : count the number of vertical pixels from the first pixel of the first index to (but not including) the first pixel of the second index. If both indices are on the same display line, zero will be returned. To count the total number of vertical pixels in the text widget, use ".text count -ypixels 1.0 end", and to ensure this is up to date, use ".text count -update -ypixels 1.0 end".


    The command returns a positive or negative integer corresponding to the number of items counted between the two indices. One such integer is returned for each counting option given, so a list is returned if more than one option was supplied. For example ".text count -xpixels -ypixels 1.3 4.5" is perfectly valid and will return a list of two elements.

[pathName]{.ins} [debug]{.sub} [boolean]{.optarg}
: If *boolean* is specified, then it must have one of the true or false values accepted by [Tcl\_GetBoolean][GetInt]. If the value is a true one then internal consistency checks will be turned on in the B-tree code associated with text widgets. If *boolean* has a false value then the debugging checks will be turned off. In either case the command returns an empty string. If *boolean* is not specified then the command returns **on** or **off** to indicate whether or not debugging is turned on. There is a single debugging switch shared by all text widgets: turning debugging on or off in any widget turns it on or off for all widgets. For widgets with large amounts of text, the consistency checks may cause a noticeable slow-down.

    When debugging is turned on, the drawing routines of the text widget set the global variables **tk\_textRedraw** and **tk\_textRelayout** to the lists of indices that are redrawn. The values of these variables are tested by Tk's test suite.

[pathName]{.ins} [delete]{.sub} [index1]{.arg} [index2]{.optdot}
: Delete a range of characters from the text. If both *index1* and *index2* are specified, then delete all the characters starting with the one given by *index1* and stopping just before *index2* (i.e. the character at *index2* is not deleted). If *index2* does not specify a position later in the text than *index1* then no characters are deleted. If *index2* is not specified then the single character at *index1* is deleted. Attempts to delete characters in a way that would leave the text without a newline as the last character will be tweaked by the text widget to avoid this. In particular, deletion of complete lines of text up to the end of the text will also delete the newline character just before the deleted block so that it is replaced by the new final newline of the text widget. The command returns an empty string. If more indices are given, multiple ranges of text will be deleted. All indices are first checked for validity before any deletions are made. They are sorted and the text is removed from the last range to the first range so deleted text does not cause an undesired index shifting side-effects.  If multiple ranges with the same start index are given, then the longest range is used.  If overlapping ranges are given, then they will be merged into spans that do not cause deletion of text outside the given ranges due to text shifted during deletion.

[pathName]{.ins} [dlineinfo]{.sub} [index]{.arg}
: Returns a list with five elements describing the area occupied by the display line containing *index*. The first two elements of the list give the x and y coordinates of the upper-left corner of the area occupied by the line, the third and fourth elements give the width and height of the area, and the fifth element gives the position of the baseline for the line, measured down from the top of the area. All of this information is measured in pixels. If the current wrap mode is **none** and the line extends beyond the boundaries of the window, the area returned reflects the entire area of the line, including the portions that are out of the window. If the line is shorter than the full width of the window then the area returned reflects just the portion of the line that is occupied by characters and embedded windows. If the display line containing *index* is not visible on the screen then the return value is an empty list.

[pathName]{.ins} [dump]{.sub} [switches]{.optarg} [index1]{.arg} [index2]{.optarg}
: Return the contents of the text widget from *index1* up to, but not including *index2*, including the text and information about marks, tags, and embedded windows. If *index2* is not specified, then it defaults to one character past *index1*. The information is returned in the following format:

    *key1 value1 index1 key2 value2 index2* ...

    The possible *key* values are **text**, **mark**, **tagon**, **tagoff**, **image**, and **window**. The corresponding *value* is the text, mark name, tag name, image name, or window name. The *index* information is the index of the start of the text, mark, tag transition, image or window. One or more of the following switches (or abbreviations thereof) may be specified to control the dump:

    [-all]{.lit}
    : Return information about all elements: text, marks, tags, images and windows. This is the default.

    **-command** *command*
    : Instead of returning the information as the result of the dump operation, invoke the *command* on each element of the text widget within the range. The command has three arguments appended to it before it is evaluated: the *key*, *value*, and *index*.

    **-image**
    : Include information about images in the dump results.

    **-mark**
    : Include information about marks in the dump results.

    **-tag**
    : Include information about tag transitions in the dump results. Tag information is returned as **tagon** and **tagoff** elements that indicate the begin and end of each range of each tag, respectively.

    **-text**
    : Include information about text in the dump results. The value is the text up to the next element or the end of range indicated by *index2*. A text element does not span newlines. A multi-line block of text that contains no marks or tag transitions will still be dumped as a set of text segments that each end with a newline. The newline is part of the value.

    **-window**
    : Include information about embedded windows in the dump results. The value of a window is its Tk pathname, unless the window has not been created yet. (It must have a create script.) In this case an empty string is returned, and you must query the window by its index position to get more information.


*pathName* **edit** *option* ?*arg ...*?
: This command controls the undo mechanism and the modified flag. The exact behavior of the command depends on the *option* argument that follows the **edit** argument. The following forms of the command are currently supported:

    [pathName]{.ins} [edit]{.sub} [canredo]{.lit}
    : Returns a boolean true if redo is possible, i.e. when the redo stack is not empty. Otherwise returns false.

    *pathName* **edit canundo**
    : Returns a boolean true if undo is possible, i.e. when the undo stack is not empty. Otherwise returns false.

    *pathName* **edit modified** ?*boolean*?
    : If *boolean* is not specified, returns the modified flag of the widget. The insert, delete, edit undo and edit redo commands or the user can set or clear the modified flag. If *boolean* is specified, sets the modified flag of the widget to *boolean*.

    *pathName* **edit redo**
    : When the **-undo** option is true, reapplies the last undone edits provided no other edits were done since then, and returns a list of indices indicating what ranges were changed by the redo operation. Generates an error when the redo stack is empty. Does nothing when the **-undo** option is false.

    *pathName* **edit reset**
    : Clears the undo and redo stacks.

    *pathName* **edit separator**
    : Inserts a separator (boundary) on the undo stack. Does nothing when the **-undo** option is false.

    *pathName* **edit undo**
    : Undoes the last edit action when the **-undo** option is true, and returns a list of indices indicating what ranges were changed by the undo operation. An edit action is defined as all the insert and delete commands that are recorded on the undo stack in between two separators. Generates an error when the undo stack is empty. Does nothing when the **-undo** option is false.


*pathName* **get** ?**-displaychars**? ?**--**? *index1* ?*index2 ...*?
: Return a range of characters from the text. The return value will be all the characters in the text starting with the one whose index is *index1* and ending just before the one whose index is *index2* (the character at *index2* will not be returned). If *index2* is omitted then the single character at *index1* is returned. If there are no characters in the specified range (e.g. *index1* is past the end of the file or *index2* is less than or equal to *index1*) then an empty string is returned. If the specified range contains embedded windows, no information about them is included in the returned string. If multiple index pairs are given, multiple ranges of text will be returned in a list. Invalid ranges will not be represented with empty strings in the list. The ranges are returned in the order passed to *pathName* **get**. If the **-displaychars** option is given, then, within each range, only those characters which are not elided will be returned. This may have the effect that some of the returned ranges are empty strings.

[pathName]{.ins} [image]{.sub} [option]{.arg} [arg]{.optdot}
: This command is used to manipulate embedded images. The behavior of the command depends on the *option* argument that follows the **image** argument. The following forms of the command are currently supported:

    [pathName]{.ins} [image]{.sub} [cget]{.lit} [index]{.arg} [option]{.arg}
    : Returns the value of a configuration option for an embedded image. *Index* identifies the embedded image, and *option* specifies a particular configuration option, which must be one of the ones listed in the section [Embedded images].

    *pathName* **image configure** *index* ?*option value ...*?
    : Query or modify the configuration options for an embedded image. If no *option* is specified, returns a list describing all of the available options for the embedded image at *index* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list). If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified). If one or more *option-value* pairs are specified, then the command modifies the given option(s) to have the given value(s); in this case the command returns an empty string. See [Embedded images] for information on the options that are supported.

    *pathName* **image create** *index* ?*option value ...*?
    : This command creates a new image annotation, which will appear in the text at the position given by *index*. Any number of *option-value* pairs may be specified to configure the annotation. Returns a unique identifier that may be used as an index to refer to this image. See [Embedded images] for information on the options that are supported, and a description of the identifier returned.

    *pathName* **image names**
    : Returns a list whose elements are the names of all image instances currently embedded in *window*.


*pathName* **index** *index*
: Returns the position corresponding to *index* in the form *line.char* where *line* is the line number and *char* is the character number. *Index* may have any of the forms described under [Indices] above.

[pathName]{.ins} [insert]{.sub} [index]{.arg} [chars]{.arg} [tagList chars tagList]{.optdot}
: Inserts all of the *chars* arguments just before the character at *index*. If *index* refers to the end of the text (the character after the last newline) then the new text is inserted just before the last newline instead. If there is a single *chars* argument and no *tagList*, then the new text will receive any tags that are present on both the character before and the character after the insertion point; if a tag is present on only one of these characters then it will not be applied to the new text. If *tagList* is specified then it consists of a list of tag names; the new characters will receive all of the tags in this list and no others, regardless of the tags present around the insertion point. If multiple *chars*-*tagList* argument pairs are present, they produce the same effect as if a separate *pathName* **insert** widget command had been issued for each pair, in order. The last *tagList* argument may be omitted.

[pathName]{.ins} [locale]{.sub} [index]{.arg}
: Returns the locale belonging to this *index*.

[pathName]{.ins} [mark]{.sub} [option]{.arg} [arg]{.optdot}
: This command is used to manipulate marks. The exact behavior of the command depends on the *option* argument that follows the **mark** argument. The following forms of the command are currently supported:

    [pathName]{.ins} [mark]{.sub} [gravity]{.lit} [markName]{.arg} [direction]{.optarg}
    : If *direction* is not specified, returns **left** or **right** to indicate which of its adjacent characters *markName* is attached to. If *direction* is specified, it must be **left** or **right**; the gravity of *markName* is set to the given value.

    *pathName* **mark names**
    : Returns a list whose elements are the names of all the marks that are currently set.

    *pathName* **mark next** *index*
    : Returns the name of the next mark at or after *index*. If *index* is specified in numerical form, then the search for the next mark begins at that index. If *index* is the name of a mark, then the search for the next mark begins immediately after that mark. This can still return a mark at the same position if there are multiple marks at the same index. These semantics mean that the **mark next** operation can be used to step through all the marks in a text widget in the same order as the mark information returned by the *pathName* **dump** operation. If a mark has been set to the special **end** index, then it appears to be *after* **end** with respect to the *pathName* **mark next** operation. An empty string is returned if there are no marks after *index*.

    *pathName* **mark previous** *index*
    : Returns the name of the mark at or before *index*. If *index* is specified in numerical form, then the search for the previous mark begins with the character just before that index. If *index* is the name of a mark, then the search for the next mark begins immediately before that mark. This can still return a mark at the same position if there are multiple marks at the same index. These semantics mean that the *pathName* **mark previous** operation can be used to step through all the marks in a text widget in the reverse order as the mark information returned by the *pathName* **dump** operation. An empty string is returned if there are no marks before *index*.

    *pathName* **mark set** *markName index*
    : Sets the mark named *markName* to a position just before the character at *index*. If *markName* already exists, it is moved from its old position; if it does not exist, a new mark is created. This command returns an empty string.

    *pathName* **mark unset** *markName* ?*markName markName ...*?
    : Remove the mark corresponding to each of the *markName* arguments. The removed marks will not be usable in indices and will not be returned by future calls to "*pathName* **mark names**". This command returns an empty string.


*pathName* **peer** *option args*
: This command is used to create and query widget peers. It has two forms, depending on *option*:

    [pathName]{.ins} [peer]{.sub} [create]{.lit} [newPathName]{.arg} [options]{.optarg}
    : Creates a peer text widget with the given *newPathName*, and any optional standard configuration options (as for the *text* command). By default the peer will have the same start and end line as the parent widget, but these can be overridden with the standard configuration options.

    *pathName* **peer names**
    : Returns a list of peers of this widget (this does not include the widget itself). The order within this list is undefined.


*pathName* **pendingsync**
: Returns 1 if the line heights calculations are not up-to-date, 0 otherwise.

[pathName]{.ins} [replace]{.sub} [index1]{.arg} [index2]{.arg} [chars]{.arg} [tagList chars tagList]{.optdot}
: Replaces the range of characters between *index1* and *index2* with the given characters and tags.  See the section on *pathName* **insert** for an explanation of the handling of the *tagList...* arguments, and the section on *pathName* **delete** for an explanation of the handling of the indices.  If *index2* corresponds to an index earlier in the text than *index1*, an error will be generated.

    The deletion and insertion are arranged so that no unnecessary scrolling of the window or movement of insertion cursor occurs. In addition the undo/redo stack are correctly modified, if undo operations are active in the text widget. The command returns an empty string.

[pathName]{.ins} [scan]{.sub} [option]{.arg} [args]{.arg}
: This command is used to implement scanning on texts. It has two forms, depending on *option*:

    [pathName]{.ins} [scan]{.sub} [mark]{.lit} [x]{.arg} [y]{.arg}
    : Records *x* and *y* and the current view in the text window, for use in conjunction with later *pathName* [scan dragto][scan] commands. Typically this command is associated with a mouse button press in the widget. It returns an empty string.

    *pathName* [scan dragto][scan] *x y*
    : This command computes the difference between its *x* and *y* arguments and the *x* and *y* arguments to the last *pathName* [scan mark][scan] command for the widget. It then adjusts the view by 10 times the difference in coordinates. This command is typically associated with mouse motion events in the widget, to produce the effect of dragging the text at high speed through the window. The return value is an empty string.


*pathName* **search** ?*switches*? *pattern index* ?*stopIndex*?
: Searches the text in *pathName* starting at *index* for a range of characters that matches *pattern*. If a match is found, the index of the first character in the match is returned as result; otherwise an empty string is returned. One or more of the following switches (or abbreviations thereof) may be specified to control the search:

    [-forwards]{.lit}
    : The search will proceed forward through the text, finding the first matching range starting at or after the position given by *index*. This is the default.

    **-backwards**
    : The search will proceed backward through the text, finding the matching range closest to *index* whose first character is before *index* (it is not allowed to be at *index*). Note that, for a variety of reasons, backwards searches can be substantially slower than forwards searches (particularly when using **-regexp**), so it is recommended that performance-critical code use forward searches.

    **-exact**
    : Use exact matching: the characters in the matching range must be identical to those in *pattern*. This is the default.

    **-regexp**
    : Treat *pattern* as a regular expression and match it against the text using the rules for regular expressions (see the [regexp] command and the **re\_syntax** page for details). The default matching automatically passes both the **-lineanchor** and **-linestop** options to the regexp engine (unless **-nolinestop** is used), so that *^$* match beginning and end of line, and *.*, *\[^* sequences will never match the newline character *\\n*.

    **-nolinestop**
    : This allows *.* and *\[^* sequences to match the newline character *\\n*, which they will otherwise not do (see the [regexp] command for details). This option is only meaningful if **-regexp** is also given, and an error will be thrown otherwise. For example, to match the entire text, use "*pathName* **search -nolinestop -regexp** \\".\*\\" 1.0".

    **-nocase**
    : Ignore case differences between the pattern and the text.

    **-count** *varName*
    : The argument following **-count** gives the name of a variable; if a match is found, the number of index positions between beginning and end of the matching range will be stored in the variable. If there are no embedded images or windows in the matching range (and there are no elided characters if **-elide** is not given), this is equivalent to the number of characters matched. In either case, the range *matchIdx* to *matchIdx + $count chars* will return the entire matched text.

    **-all**
    : Find all matches in the given range and return a list of the indices of the first character of each match. If a **-count** *varName* switch is given, then *varName* is also set to a list containing one element for each successful match. Note that, even for exact searches, the elements of this list may be different, if there are embedded images, windows or hidden text. Searches with **-all** behave very similarly to the Tcl command [regexp -all][regexp], in that overlapping matches are not normally returned. For example, applying an **-all** search of the pattern "\\w+" against "hello there" will just match twice, once for each word, and matching "Z\[a-z\]+Z" against "ZooZooZoo" will just match once.

    **-overlap**
    : When performing **-all** searches, the normal behaviour is that matches which overlap an already-found match will not be returned. This switch changes that behaviour so that all matches which are not totally enclosed within another match are returned. For example, applying an **-overlap** search of the pattern "\\w+" against "hello there" will just match twice (i.e. no different to just **-all**), but matching "Z\[a-z\]+Z" against "ZooZooZoo" will now match twice. An error will be thrown if this switch is used without **-all**.

    **-strictlimits**
    : When performing any search, the normal behaviour is that the start and stop limits are checked with respect to the start of the matching text. With the **-strictlimits** flag, the entire matching range must lie inside the start and stop limits specified for the match to be valid.

    **-elide**
    : Find elided (hidden) text as well. By default only displayed text is searched.

    **--**
    : This switch has no effect except to terminate the list of switches: the next argument will be treated as *pattern* even if it starts with **-**.


    The matching range may be within a single line of text, or run across multiple lines (if parts of the pattern can match a new-line). For regular expression matching one can use the various newline-matching features such as **$** to match the end of a line, **^** to match the beginning of a line, and to control whether **.** is allowed to match a new-line. If *stopIndex* is specified, the search stops at that index: for forward searches, no match at or after *stopIndex* will be considered; for backward searches, no match earlier in the text than *stopIndex* will be considered. If *stopIndex* is omitted, the entire text will be searched: when the beginning or end of the text is reached, the search continues at the other end until the starting location is reached again; if *stopIndex* is specified, no wrap-around will occur. This means that, for example, if the search is **-forwards** but *stopIndex* is earlier in the text than *startIndex*, nothing will ever be found. See [Known bugs] below for a number of minor limitations of the *pathName* **search** command.

*pathName* **see** *index*
: Adjusts the view in the window so that the character given by *index* is completely visible. If *index* is already visible then the command does nothing. If *index* is a short distance out of view, the command adjusts the view just enough to make *index* visible at the edge of the window. If *index* is far out of view, then the command centers *index* in the window.

[pathName]{.ins} [sync]{.sub} [[-command]{.lit} [command]{.arg}]{.optarg}
: Controls the synchronization of the view of the text widget.

    [pathName]{.ins} [sync]{.sub}
    : Immediately brings the line metrics up-to-date by forcing computation of any outdated line heights. The command returns immediately if there is no such outdated line heights, otherwise it returns only at the end of the computation. The command returns an empty string.

    *pathName* **sync -command** *command*
    : Schedules *command* to be executed (by the event loop) exactly once as soon as all line heights are up-to-date. If there are no pending line metrics calculations, the scheduling is immediate. The command returns the empty string. [bgerror] is called on *command* failure.


*pathName* **tag** *option* ?*arg ...*?
: This command is used to manipulate tags. The exact behavior of the command depends on the *option* argument that follows the **tag** argument. The following forms of the command are currently supported:

    *pathName* **tag add** *tagName index1* ?*index2 index1 index2 ...*?
    : Associate the tag *tagName* with all of the characters starting with *index1* and ending just before *index2* (the character at *index2* is not tagged). A single command may contain any number of *index1*-*index2* pairs. If the last *index2* is omitted then the single character at *index1* is tagged. If there are no characters in the specified range (e.g. *index1* is past the end of the file or *index2* is less than or equal to *index1*) then the command has no effect.

    *pathName* **tag bind** *tagName* ?*sequence*? ?*script*?
    : This command associates *script* with the tag given by *tagName*. Whenever the event sequence given by *sequence* occurs for a character that has been tagged with *tagName*, the script will be invoked. This widget command is similar to the **bind** command except that it operates on characters in a text rather than entire widgets. See the **bind** manual entry for complete details on the syntax of *sequence* and the substitutions performed on *script* before invoking it. If all arguments are specified then a new binding is created, replacing any existing binding for the same *sequence* and *tagName* (if the first character of *script* is "+" then *script* augments an existing binding rather than replacing it). In this case the return value is an empty string. If *script* is omitted then the command returns the *script* associated with *tagName* and *sequence* (an error occurs if there is no such binding). If both *script* and *sequence* are omitted then the command returns a list of all the sequences for which bindings have been defined for *tagName*.


    The only events for which bindings may be specified are those related to the mouse and keyboard (such as **Enter**, **Leave**, **Button**, **Motion**, and **Key**) or virtual events. Mouse and keyboard event bindings for a text widget respectively use the **current** and **insert** marks described under [Marks] above. An **Enter** event triggers for a tag when the tag first becomes present on the current character, and a **Leave** event triggers for a tag when it ceases to be present on the current character. **Enter** and **Leave** events can happen either because the **current** mark moved or because the character at that position changed. Note that these events are different than **Enter** and **Leave** events for windows. Mouse events are directed to the current character, while keyboard events are directed to the insert character. If a virtual event is used in a binding, that binding can trigger only if the virtual event is defined by an underlying mouse-related or keyboard-related event.

    It is possible for the current character to have multiple tags, and for each of them to have a binding for a particular event sequence. When this occurs, one binding is invoked for each tag, in order from lowest-priority to highest priority. If there are multiple matching bindings for a single tag, then the most specific binding is chosen (see the manual entry for the **bind** command for details). [continue] and [break] commands within binding scripts are processed in the same way as for bindings created with the **bind** command.

    If bindings are created for the widget as a whole using the **bind** command, then those bindings will supplement the tag bindings. The tag bindings will be invoked first, followed by bindings for the window as a whole.

*pathName* **tag cget** *tagName option*
: This command returns the current value of the option named *option* associated with the tag given by *tagName*. *Option* may have any of the values accepted by the *pathName* **tag configure** widget command.

*pathName* **tag configure** *tagName* ?*option*? ?*value*? ?*option value ...*?
: This command is similar to the *pathName* **configure** widget command except that it modifies options associated with the tag given by *tagName* instead of modifying options for the overall text widget. If no *option* is specified, the command returns a list describing all of the available options for *tagName* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list). If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified). If one or more *option-value* pairs are specified, then the command modifies the given option(s) to have the given value(s) in *tagName*; in this case the command returns an empty string. See [Tags] above for details on the options available for tags.

*pathName* **tag delete** *tagName* ?*tagName ...*?
: Deletes all tag information for each of the *tagName* arguments. The command removes the tags from all characters in the file and also deletes any other information associated with the tags, such as bindings and display information. The command returns an empty string.

*pathName* **tag lower** *tagName* ?*belowThis*?
: Changes the priority of tag *tagName* so that it is just lower in priority than the tag whose name is *belowThis*. If *belowThis* is omitted, then *tagName*'s priority is changed to make it lowest priority of all tags.

*pathName* **tag names** ?*index*?
: Returns a list whose elements are the names of all the tags that are active at the character position given by *index*. If *index* is omitted, then the return value will describe all of the tags that exist for the text (this includes all tags that have been named in a "*pathName* **tag**" widget command but have not been deleted by a "*pathName* **tag delete**" widget command, even if no characters are currently marked with the tag). The list will be sorted in order from lowest priority to highest priority.

*pathName* **tag nextrange** *tagName index1* ?*index2*?
: This command searches the text for a range of characters tagged with *tagName* where the first character of the range is no earlier than the character at *index1* and no later than the character just before *index2* (a range starting at *index2* will not be considered). If several matching ranges exist, the first one is chosen. The command's return value is a list containing two elements, which are the index of the first character of the range and the index of the character just after the last one in the range. If no matching range is found then the return value is an empty string. If *index2* is not given then it defaults to the end of the text.

*pathName* **tag prevrange** *tagName index1* ?*index2*?
: This command searches the text for a range of characters tagged with *tagName* where the first character of the range is before the character at *index1* and no earlier than the character at *index2* (a range starting at *index2* will be considered). If several matching ranges exist, the one closest to *index1* is chosen. The command's return value is a list containing two elements, which are the index of the first character of the range and the index of the character just after the last one in the range. If no matching range is found then the return value is an empty string. If *index2* is not given then it defaults to the beginning of the text.

*pathName* **tag raise** *tagName* ?*aboveThis*?
: Changes the priority of tag *tagName* so that it is just higher in priority than the tag whose name is *aboveThis*. If *aboveThis* is omitted, then *tagName*'s priority is changed to make it highest priority of all tags.

*pathName* **tag ranges** *tagName*
: Returns a list describing all of the ranges of text that have been tagged with *tagName*. The first two elements of the list describe the first tagged range in the text, the next two elements describe the second range, and so on. The first element of each pair contains the index of the first character of the range, and the second element of the pair contains the index of the character just after the last one in the range. If there are no characters tagged with *tag* then an empty string is returned.

*pathName* **tag remove** *tagName index1* ?*index2 index1 index2 ...*?
: Remove the tag *tagName* from all of the characters starting at *index1* and ending just before *index2* (the character at *index2* is not affected). A single command may contain any number of *index1*-*index2* pairs. If the last *index2* is omitted then the tag is removed from the single character at *index1*. If there are no characters in the specified range (e.g. *index1* is past the end of the file or *index2* is less than or equal to *index1*) then the command has no effect. This command returns an empty string.

[pathName]{.ins} [window]{.sub} [option]{.arg} [arg]{.optdot}
: This command is used to manipulate embedded windows. The behavior of the command depends on the *option* argument that follows the **window** argument. The following forms of the command are currently supported:

    [pathName]{.ins} [window]{.sub} [cget]{.lit} [index]{.arg} [option]{.arg}
    : Returns the value of a configuration option for an embedded window. *Index* identifies the embedded window, and *option* specifies a particular configuration option, which must be one of the ones listed in the section [Embedded windows].

    *pathName* **window configure** *index* ?*option value ...*?
    : Query or modify the configuration options for an embedded window. If no *option* is specified, returns a list describing all of the available options for the embedded window at *index* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list). If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified). If one or more *option-value* pairs are specified, then the command modifies the given option(s) to have the given value(s); in this case the command returns an empty string. See [Embedded windows] for information on the options that are supported.

    *pathName* **window create** *index* ?*option value ...*?
    : This command creates a new window annotation, which will appear in the text at the position given by *index*. Any number of *option-value* pairs may be specified to configure the annotation. See [Embedded windows] for information on the options that are supported. Returns an empty string.

    *pathName* **window names**
    : Returns a list whose elements are the names of all windows currently embedded in *window*.


*pathName* **xview** *option args*
: This command is used to query and change the horizontal position of the text in the widget's window. It can take any of the following forms:

    [pathName]{.ins} [xview]{.sub}
    : Returns a list containing two elements. Each element is a real fraction between 0 and 1; together they describe the portion of the document's horizontal span that is visible in the window. For example, if the first element is .2 and the second element is .6, 20% of the text is off-screen to the left, the middle 40% is visible in the window, and 40% of the text is off-screen to the right. The fractions refer only to the lines that are actually visible in the window: if the lines in the window are all very short, so that they are entirely visible, the returned fractions will be 0 and 1, even if there are other lines in the text that are much wider than the window. These are the same values passed to scrollbars via the **-xscrollcommand** option.

    *pathName* **xview moveto** *fraction*
    : Adjusts the view in the window so that *fraction* of the horizontal span of the text is off-screen to the left. *Fraction* is a fraction between 0 and 1.

    *pathName* **xview scroll** *number what*
    : This command shifts the view in the window left or right according to *number* and *what*. *What* must be **pages**, **pixels**, or **units**. If *what* is **pages** or **units** then *number* must be an integer, otherwise number may be specified in any of the forms acceptable to [Tk\_GetPixels][GetPixels], such as "2.0c" or "1i" (the result is rounded to the nearest integer value. If no units are given, pixels are assumed). If *what* is **pages** then the view adjusts by *number* screenfuls; if it is **pixels** then the view adjusts by *number* pixels; if it is **units**, the view adjusts left or right by *number* average-width characters on the display. If *number* is negative then characters farther to the left become visible; if it is positive then characters farther to the right become visible.


*pathName* **yview** ?*args*?
: This command is used to query and change the vertical position of the text in the widget's window. It can take any of the following forms:

    *pathName* **yview**
    : Returns a list containing two elements, both of which are real fractions between 0 and 1. The first element gives the position of the first visible pixel of the first character (or image, etc) in the top line in the window, relative to the text as a whole (0.5 means it is halfway through the text, for example). The second element gives the position of the first pixel just after the last visible one in the bottom line of the window, relative to the text as a whole. These are the same values passed to scrollbars via the **-yscrollcommand** option.

    *pathName* **yview moveto** *fraction*
    : Adjusts the view in the window so that the pixel given by *fraction* appears at the top of the top line of the window. *Fraction* is a fraction between 0 and 1; 0 indicates the first pixel of the first character in the text, 0.33 indicates the pixel that is one-third the way through the text; and so on. Values close to 1 will indicate values close to the last pixel in the text (1 actually refers to one pixel beyond the last pixel), but in such cases the widget will never scroll beyond the last pixel, and so a value of 1 will effectively be rounded back to whatever fraction ensures the last pixel is at the bottom of the window, and some other pixel is at the top.

    *pathName* **yview scroll** *number what*
    : This command adjust the view in the window up or down according to *number* and *what*. *What* must be **pages**, **pixels**, or **units**. If *what* is **units** or **pages** then *number* must be an integer, otherwise number may be specified in any of the forms acceptable to [Tk\_GetPixels][GetPixels], such as "2.0c" or "1i" (the result is rounded to the nearest integer value. If no units are given, pixels are assumed). If *what* is **units**, the view adjusts up or down by *number* lines on the display; if it is **pages** then the view adjusts by *number* screenfuls; if it is **pixels** then the view adjusts by *number* pixels. If *number* is negative then earlier positions in the text become visible; if it is positive then later positions in the text become visible.

    *pathName* **yview** ?**-pickplace**? *index*
    : Changes the view in the widget's window to make *index* visible. If the **-pickplace** option is not specified then *index* will appear at the top of the window. If **-pickplace** is specified then the widget chooses where *index* appears in the window:

    \[1\]
    : If *index* is already visible somewhere in the window then the command does nothing.

    \[2\]
    : If *index* is only a few lines off-screen above the window then it will be positioned at the top of the window.

    \[3\]
    : If *index* is only a few lines off-screen below the window then it will be positioned at the bottom of the window.

    \[4\]
    : Otherwise, *index* will be centered in the window.


    The **-pickplace** option has been obsoleted by the *pathName* **see** widget command (*pathName* **see** handles both x- and y-motion to make a location visible, whereas the **-pickplace** mode only handles motion in y).

*pathName* **yview** *number*
: This command makes the first character on the line after the one given by *number* visible at the top of the window. *Number* must be an integer. This command used to be used for scrolling, but now it is obsolete.


# Bindings

Tk automatically creates class bindings for texts that give them the following default behavior. In the descriptions below, "word" is dependent on the value of the **tcl\_wordchars** variable.  See [tclvars](n).

1. Clicking mouse button 1 positions the insertion cursor at the closest edge of the character underneath the mouse cursor, sets the input focus to this widget, and clears any selection in the widget. Dragging with mouse button 1 strokes out a selection between the insertion cursor and the character under the mouse.

2. Double-clicking with mouse button 1 selects the word under the mouse and positions the insertion cursor at the start of the word. Dragging after a double click will stroke out a selection consisting of whole words.

3. Triple-clicking with mouse button 1 selects the line under the mouse and positions the insertion cursor at the start of the line. Dragging after a triple click will stroke out a selection consisting of whole lines.

4. The ends of the selection can be adjusted by dragging with mouse button 1 while the Shift key is down; this will adjust the end of the selection that was nearest to the mouse cursor when button 1 was pressed. If the button is double-clicked before dragging then the selection will be adjusted in units of whole words; if it is triple-clicked then the selection will be adjusted in units of whole lines.

5. Clicking mouse button 1 with the Control key down will reposition the insertion cursor without affecting the selection.

6. If any normal printing characters are typed, they are inserted at the point of the insertion cursor.

7. The view in the widget can be adjusted by dragging with the middle mouse button (button 2, or button 3 in TkAqua). If the middle mouse button is clicked without moving the mouse, the selection is copied into the text at the position of the mouse cursor. The Insert key also inserts the selection, but at the position of the insertion cursor.

8. If the mouse is dragged out of the widget while button 1 is pressed, the entry will automatically scroll to make more text visible (if there is more text off-screen on the side where the mouse left the window).

9. The Left and Right keys move the insertion cursor one character to the left or right; they also clear any selection in the text. If Left or Right is typed with the Shift key down, then the insertion cursor moves and the selection is extended to include the new character. Control-Left and Control-Right move the insertion cursor by words, and Control-Shift-Left and Control-Shift-Right move the insertion cursor by words and also extend the selection. Control-b and Control-f behave the same as Left and Right, respectively. Meta-b and Meta-f behave the same as Control-Left and Control-Right, respectively.

10. The Up and Down keys move the insertion cursor one line up or down and clear any selection in the text. If Up or Right is typed with the Shift key down, then the insertion cursor moves and the selection is extended to include the new character. Control-Up and Control-Down move the insertion cursor by paragraphs (groups of lines separated by blank lines), and Control-Shift-Up and Control-Shift-Down move the insertion cursor by paragraphs and also extend the selection. Control-p and Control-n behave the same as Up and Down, respectively.

11. The Next and Prior keys move the insertion cursor forward or backwards by one screenful and clear any selection in the text. If the Shift key is held down while Next or Prior is typed, then the selection is extended to include the new character.

12. Control-Next and Control-Prior scroll the view right or left by one page without moving the insertion cursor or affecting the selection.

13. Home and Control-a move the insertion cursor to the beginning of its display line and clear any selection in the widget. Shift-Home moves the insertion cursor to the beginning of the display line and also extends the selection to that point.

14. End and Control-e move the insertion cursor to the end of the display line and clear any selection in the widget. Shift-End moves the cursor to the end of the display line and extends the selection to that point.

15. Control-Home and Meta-< move the insertion cursor to the beginning of the text and clear any selection in the widget. Control-Shift-Home moves the insertion cursor to the beginning of the text and also extends the selection to that point.

16. Control-End and Meta-> move the insertion cursor to the end of the text and clear any selection in the widget. Control-Shift-End moves the cursor to the end of the text and extends the selection to that point.

17. The Select key and Control-Space set the selection anchor to the position of the insertion cursor. They do not affect the current selection. Shift-Select and Control-Shift-Space adjust the selection to the current position of the insertion cursor, selecting from the anchor to the insertion cursor if there was not any selection previously.

18. Control-/ selects the entire contents of the widget.

19. Control-\\ clears any selection in the widget.

20. The F16 key (labelled Copy on many Sun workstations) or Meta-w copies the selection in the widget to the clipboard, if there is a selection. This action is carried out by the command **tk\_textCopy**.

21. The F20 key (labelled Cut on many Sun workstations) or Control-w copies the selection in the widget to the clipboard and deletes the selection. This action is carried out by the command **tk\_textCut**. If there is no selection in the widget then these keys have no effect.

22. The F18 key (labelled Paste on many Sun workstations) or Control-y inserts the contents of the clipboard at the position of the insertion cursor. This action is carried out by the command **tk\_textPaste**.

23. The Delete key deletes the selection, if there is one in the widget. If there is no selection, it deletes the character to the right of the insertion cursor.

24. Backspace and Control-h delete the selection, if there is one in the widget. If there is no selection, they delete the character to the left of the insertion cursor.

25. Control-d deletes the character to the right of the insertion cursor.

26. Meta-d deletes the word to the right of the insertion cursor.

27. Control-k deletes from the insertion cursor to the end of its line; if the insertion cursor is already at the end of a line, then Control-k deletes the newline character.

28. Control-o opens a new line by inserting a newline character in front of the insertion cursor without moving the insertion cursor.

29. Meta-backspace and Meta-Delete delete the word to the left of the insertion cursor.

30. Control-t reverses the order of the two characters to the right of the insertion cursor.

31. Control-x deletes whatever is selected in the text widget after copying it to the clipboard.

32. Control-z undoes the last edit action if the **-undo** option is true. Does nothing otherwise.

33. Control-Z (or Control-y on Windows) reapplies the last undone edit action if the **-undo** option is true. Does nothing otherwise.


If the widget is disabled using the **-state** option, then its view can still be adjusted and text can still be selected, but no insertion cursor will be displayed and no text modifications will take place.

The behavior of texts can be changed by defining new bindings for individual widgets or by redefining the class bindings.

# Known issues

## Issues concerning chars and indices

Before Tk 8.5, the widget used the string "chars" to refer to index positions (which included characters, embedded windows and embedded images). As of Tk 8.5 the text widget deals separately and correctly with "chars" and "indices". For backwards compatibility, however, the index modifiers "+N chars" and "-N chars" continue to refer to indices. One must use any of the full forms "+N any chars" or "-N any chars" etc. to refer to actual character indices. This confusion may be fixed in a future release by making the widget correctly interpret "+N chars" as a synonym for "+N any chars".

## Performance issues

Text widgets should run efficiently under a variety of conditions. The text widget uses about 2-3 bytes of main memory for each byte of text, so texts containing a megabyte or more should be practical on most workstations. Text is represented internally with a modified B-tree structure that makes operations relatively efficient even with large texts. Tags are included in the B-tree structure in a way that allows tags to span large ranges or have many disjoint smaller ranges without loss of efficiency. Marks are also implemented in a way that allows large numbers of marks. In most cases it is fine to have large numbers of unique tags, or a tag that has many distinct ranges.

One performance problem can arise if you have hundreds or thousands of different tags that all have the following characteristics: the first and last ranges of each tag are near the beginning and end of the text, respectively, or a single tag range covers most of the text widget. The cost of adding and deleting tags like this is proportional to the number of other tags with the same properties. In contrast, there is no problem with having thousands of distinct tags if their overall ranges are localized and spread uniformly throughout the text.

Very long text lines can be expensive, especially if they have many marks and tags within them.

The display line with the insert cursor is redrawn each time the cursor blinks, which causes a steady stream of graphics traffic. Set the **-insertofftime** attribute to 0 avoid this.

## Known bugs

The *pathName* **search -regexp** sub-command attempts to perform sophisticated regexp matching across multiple lines in an efficient fashion (since Tk 8.5), examining each line individually, and then in small groups of lines, whether searching forwards or backwards. Under certain conditions the search result might differ from that obtained by applying the same regexp to the entire text from the widget in one go. For example, when searching with a greedy regexp, the widget will continue to attempt to add extra lines to the match as long as one of two conditions are true: either Tcl's regexp library returns a code to indicate a longer match is possible (but there are known bugs in Tcl which mean this code is not always correctly returned); or if each extra line added results in at least a partial match with the pattern. This means in the case where the first extra line added results in no match and Tcl's regexp system returns the incorrect code and adding a second extra line would actually match, the text widget will return the wrong result. In practice this is a rare problem, but it can occur, for example:

```
pack [text .t]
.t insert 1.0 "aaaa\nbbbb\ncccc\nbbbb\naaaa\n"
.t search -regexp -- {(a+|b+\nc+\nb+)+\na+} 1.0
```

will not find a match when one exists of 19 characters starting from the first "b".

Whenever one possible match is fully enclosed in another, the search command will attempt to ensure only the larger match is returned. When performing backwards regexp searches it is possible that Tcl will not always achieve this, in the case where a match is preceded by one or more short, non-overlapping matches, all of which are preceded by a large match which actually encompasses all of them. The search algorithm used by the widget does not look back arbitrarily far for a possible match which might cover large portions of the widget. For example:

```
pack [text .t]
.t insert 1.0 "aaaa\nbbbb\nbbbb\nbbbb\nbbbb\n"
.t search -regexp -backward -- {b+\n|a+\n(b+\n)+} end
```

matches at "5.0" when a true greedy match would match at "1.0". Similarly if we add **-all** to this case, it matches at all of "5.0", "4.0", "3.0" and "1.0", when really it should only match at "1.0" since that match encloses all the others.


[bgerror]: ../Tcl/bgerror.md
[break]: ../Tcl/break.md
[ConfigWidg]: ../TkCAPI/ConfigWidg.md
[continue]: ../Tcl/continue.md
[GetBitmap]: ../TkCAPI/GetBitmap.md
[GetColor]: ../TkCAPI/GetColor.md
[GetFont]: ../TkCAPI/GetFont.md
[GetInt]: ../TclCAPI/GetInt.md
[GetPixels]: ../TkCAPI/GetPixels.md
[GetRelief]: ../TkCAPI/GetRelief.md
[options]: options.md
[regexp]: ../Tcl/regexp.md
[scan]: ../Tcl/scan.md
[tclvars]: ../Tcl/tclvars.md

