---
CommandName: ttk::treeview
title: ttk::treeview
ManualSection: n
Version: 9.1
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - listbox(n)
 - image(n)
 - bind(n)
Keywords:
 - tree
 - treeview
 - widget
Copyright:
 - Copyright (c) 2004 Joe English
---

# Name

ttk::treeview - hierarchical multicolumn data display widget

# Synopsis

::: {.synopsis} :::
[ttk::treeview]{.cmd} [pathname]{.arg} [options]{.optarg}
:::

# Description

The **ttk::treeview** widget displays a hierarchical collection of items. Each item has a textual label, an optional image, and an optional list of data values. The data values are displayed in successive columns after the tree label as cells. The order in which data values are displayed may be controlled by setting the **-displaycolumns** widget option. The tree widget can also display column headings. Columns may be accessed by number or by symbolic names listed in the **-columns** widget option; see [Column identifiers]. Treeview widgets support horizontal and vertical scrolling with the standard **-**\[**xy**\]**scrollcommand** options and \[**xy**\]**view** widget commands.

Each item is identified by a unique name. The widget will generate item IDs if they are not supplied by the caller. There is a distinguished root item, named **{}**. The root item itself is not displayed; only its children appear at the top level of the hierarchy. Cell ids are a 2-value list consisting of the item id and column id. Items and cells may also be associated with 0 or more tags. Tags are used to associate event bindings with individual items/cells and to control their appearance. Items and cells can be selected depending on the **-selectmode** and **-selecttype** widget options. The item and cell focus determines which item or cell receives the keyboard focus for navigation between items and cells and for selection changes.

# Standard options

::: {.standardoptions} :::
-class
-cursor
-padding
-style
-takefocus
-xscrollcommand
-yscrollcommand
:::

See the [ttk\_widget][ttk_widget] manual entry for details on the standard options.

# Widget-specific options

[-columns]{.cmdline} [columns]{.dbname} [Columns]{.dbclass}
: A list of column identifiers, specifying the number of columns and their names.

[-displaycolumns]{.cmdline} [displayColumns]{.dbname} [DisplayColumns]{.dbclass}
: A list of column identifiers (either symbolic names or integer indices) specifying which columns are to be displayed and the order in which they appear, or the string **#all** to display all columns in **-columns** order. The default is **#all**.

[-headingheight]{.cmdline} [headingHeight]{.dbname} [Height]{.dbclass}
: Specifies the heading height in pixels. The default value is auto calculated based on the theme and font. It can be overridden by this option until the next theme or font change. It can be permanently overridden via ttk::style option: **ttk::style configure Treeview -headingheight** *pixels*.

[-height]{.cmdline} [height]{.dbname} [Height]{.dbclass}
: Specifies the number of rows which should be visible. Note that the requested width is determined from the sum of the column widths.

[-rowheight]{.cmdline} [rowHeight]{.dbname} [Height]{.dbclass}
: Specifies the row height in pixels. The default value is auto calculated based on the theme and font. It can be overridden by this option until the next theme or font change. It can be permanently overridden via ttk::style option: **ttk::style configure Treeview -rowheight** *pixels*.

[-selectmode]{.cmdline} [selectMode]{.dbname} [SelectMode]{.dbclass}
: Controls how the built-in class bindings manage the selection. Options are:

    **none**
    : The selection cannot be changed. Focus and item/cell navigation is still allowed.

    **browse**
    : Only one item or cell will be selected at a time. Items and cells are selected via Button-1 press. Selecting an item/cell deselects all other items/cells. The selection follows the mouse when Button 1 is kept pressed. The selection follows the focus during keyboard traversal. In this mode, the selection cannot be removed via the keyboard bindings.

    **single**
    : Only a single item or cell can be selected at a time. Items and cells are selected via Button-1 press. Selecting an item/cell deselects all other items/cells. The selection follows the focus during keyboard traversal. The selection can be cleared via the keyboard bindings.

    **multiple**
    : Multiple items or cells can be selected. The selection state of items and cells is toggled via a Button-1 press. Selecting an item/cell does not change the selection state of other items/cells. In this mode, the focus and selection are independent which allows for keyboard traversal to change the focus without changing the selection. The selection can be cleared via the keyboard bindings.

    **extended**
    : Multiple items or cells can be selected, but the default is for only one item/cell to be selected at a time. More items can be selected by extending the selection using the Shift and Control/Command keys in combination with Button 1 press or keyboard traversal. The selection follows the focus during keyboard traversal. The selection can be cleared via the keyboard bindings. This is the default.


    Note that application code and tag bindings can set the selection however they wish, regardless of the value of **-selectmode**.

[-selecttype]{.cmdline} [selectType]{.dbname} [SelectType]{.dbclass}
: Controls how the built-in class bindings manage the selection. Options are **item** or **cell**. The selection should be cleared before switching modes. The **CheckTreeview** style works best with **item** select type.

[-show]{.cmdline} [show]{.dbname} [Show]{.dbclass}
: Controls which elements of the tree to display. May be a list containing zero or more of the following values. The default is **tree headings**.

    **tree**
    : Display tree labels in column #0.

    **headings**
    : Display the heading row.


    **NOTE:** Column #0 always refers to the tree column, even if **-show tree** is not specified.

[-striped]{.cmdline} [striped]{.dbname} [Striped]{.dbclass}
: Boolean specifying zebra striped item coloring. Note that striped items uses the **-stripedbackground** option if set by the theme or a tag. If not supported by the current theme, it will not show.

[-titlecolumns]{.cmdline} [titleColumns]{.dbname} [TitleColumns]{.dbclass}
: Number of display columns at the left that should not be scrolled. The tree column counts, even if **-show tree** is not specified. Thus for value N of this option, column #N is the first one that is scrollable. The default is 0.

[-titleitems]{.cmdline} [titleItems]{.dbname} [TitleItems]{.dbclass}
: Number of items at the top that should not be vertically scrolled. The default is 0.


# Indices

Several of the widget subcommands use an index as their argument. An index specifies a particular child item within it's parent, using any of the following forms:

*number*
: Specifies the numerical index of a child within its parent, where 0 corresponds to the first item.

**end**
: Indicates the end of the list of children. For most subcommands this refers to the last child, but for a few such as **index**, **insert**, and **move** it refers to the item just after the last one.

**first**
: Indicates the first child in the list of children.

**last**
: Indicates the last child in the list of children.

**end-***n*
: Indicates the item at position *n* relative to the end where **end-1** is the last item in the list of children.

*m***-***n* or *m***+***n*
: Indicates the item at position *n* relative to index *m*. See [string index][string] for valid forms.


# Column identifiers

Column identifiers take any of the following forms:

- A symbolic name from the list defined by **-columns** option.

- An integer *n*, specifying the *n*th data column. This is an index corresponding to the columns in the **-columns** option and the value is taken from the item's **-values** option.

- A string of the form **#***n*, where *n* is an integer, specifying the *n*th display column. Column **#0** is always the tree column even if not shown. The subsequent columns are defined by the **-displaycolumns** option. For example, **#1** is the first column in **-displaycolumns**.

- In the form of **end-***n*, specifying the *n*th display column from the end, where **end-1** is the last displayed column.

- In the form of *m***-***n* or *m***+***n* for the *n*th data column relative to column *m*. See [string index][string] for valid forms.


# Widget command

In addition to the standard **cget**, **configure**, **instate**, **state**, **style**, **xview** and **yview** subcommands (see [ttk::widget][ttk_widget]), treeview widgets support the following additional subcommands:

[pathname]{.ins} [after]{.sub} [-hidden]{.optlit} [-norecurse]{.optlit} [item]{.arg}
: Returns the identifier of the item after *item* in the current view. It can be the first child, next sibling, next sibling of an ancestor, or {} for none. Use **-hidden** to include hidden items and **-nohidden** (default) to exclude them. Use **-recurse** to include descendants/ancestors of *item* (default) and **-norecurse** to exclude them. If both **-hidden** and **-recurse** are used, descendants of closed items will be included. The [next] method is equivalent to "**after -hidden -norecurse**"

[pathname]{.ins} [bbox]{.sub} [item]{.arg} [column]{.optarg}
: Returns the bounding box (relative to the treeview widget's window) of the specified *item* in the form *x y width height*. If the *item* is not visible (i.e., if it is a descendant of a closed item or is vertically scrolled offscreen), returns the empty list. If *column* is specified and is not hidden (by the **-displaycolumns** option), returns the bounding box of that cell within *item* (even if the cell is horizontally scrolled offscreen). Hint: if you only have the *cell*, use the expand operator "{\*}" to convert it into item and column components.

[pathname]{.ins} [before]{.sub} [-hidden]{.optlit} [-norecurse]{.optlit} [item]{.arg}
: Returns the identifier of the item before *item* in the current view. It can be the previous sibling, parent of *item*, or {} for none. Use **-hidden** to include hidden items and **-nohidden** (default) to exclude them. Use **-recurse** to include descendants/ancestors of *item* (default) and **-norecurse** to exclude them. If both **-hidden** and **-recurse** are used, descendants of closed items will be included. The **prev** method is equivalent to "**before -hidden -norecurse**"

[pathname]{.ins} [cellfocus]{.sub} [cell]{.optarg}
: Sets the cell focus to *cell*. Use **{}** to clear the focus cell. Without *cell*, returns the current focus cell id or **{}** if none.

[pathname]{.ins} [cellselection]{.sub} [selop arg]{.optdot}
: Manages cell selection. The cell selection is independent from the item selection handled by the **selection** subcommand. A cell is specified by a list of two elements, the item and the column. For the rectangle versions of these subcommands, the cells must be in displayed columns. Any change to **-columns** clears the cell selection. A *cellList* argument may be a single cell or a list of cells. If *selop* is not specified, returns a list of selected cells. Otherwise, *selop* is one of the following:

    [pathname]{.ins} [cellselection]{.sub} [add]{.lit} [cellList]{.arg}
    : Adds cells *cellList* to the cell selection.

    [pathname]{.ins} [cellselection]{.sub} [add]{.lit} [-nohidden]{.optlit} [-norecurse]{.optlit} [cellFirst]{.arg} [cellLast]{.arg}
    : Add the cells within the rectangle defined by *cellFirst* and *cellLlast* to the cell selection. With **-nohidden**, hidden cells will be excluded and with **-norecurse**, cell in descendant items will be excluded.

    [pathname]{.ins} [cellselection]{.sub} [anchor]{.lit} [cell]{.optarg}
    : Sets the cell selection anchor to *cell*. The anchor is used as the cellFirst for the selection extend bindings. Without *cell*, the current cell selection anchor is returned. Use empty string **{}** to unset the anchor.

    [pathname]{.ins} [cellselection]{.sub} [has]{.lit} [cellList]{.arg}
    : Returns 1 if cell selection includes all of the cells in *cellList* or 0 if any are not selected.

    [pathname]{.ins} [cellselection]{.sub} [includes]{.lit} [cellList]{.arg}
    : Returns 1 if cell selection includes all of the cells in *cellList* or 0 if any are not selected.

    [pathname]{.ins} [cellselection]{.sub} [present]{.lit}
    : Returns 1 if there are selected cells or 0 if none.

    [pathname]{.ins} [cellselection]{.sub} [remove]{.lit} [cellList]{.arg}
    : Remove each cell in *cellList* from the cell selection.

    [pathname]{.ins} [cellselection]{.sub} [remove]{.lit} [-nohidden]{.optlit} [-norecurse]{.optlit} [cellFirst]{.arg} [cellLast]{.arg}
    : Remove the cells within the rectangle defined by *cellFirst* and *cellLast* from the cell selection. With **-nohidden**, hidden cells will be excluded and with **-norecurse**, cell in descendant items will be excluded.

    [pathname]{.ins} [cellselection]{.sub} [set]{.lit} [cellList]{.arg}
    : Sets the selection to the cells in *cellList*. All other cells are unselected. Use an empty set **{}** to clear the selection.

    [pathname]{.ins} [cellselection]{.sub} [set]{.lit} [-nohidden]{.optlit} [-norecurse]{.optlit} [cellFirst]{.arg} [cellLast]{.arg}
    : Set the selection to the cells within the rectangle defined by *cellFirst* and *cellLast*. All other cells are unselected. With **-nohidden**, hidden cells will be excluded and with **-norecurse**, cell in descendant items will be excluded.

    [pathname]{.ins} [cellselection]{.sub} [size]{.lit}
    : Returns the count of selected cells.

    [pathname]{.ins} [cellselection]{.sub} [toggle]{.lit} [cellList]{.arg}
    : Toggle the cell selection state of each cell in *cellList*. If the element is partially visible, the result gives the full area of the element, including any parts that are not visible.

    [pathname]{.ins} [cellselection]{.sub} [toggle]{.lit} [-nohidden]{.optlit} [-norecurse]{.optlit} [cellFirst]{.arg} [cellLast]{.arg}
    : Toggle the selection state of the cells within the rectangle defined by *cellFirst* and *cellLast*. With **-nohidden**, hidden cells will be excluded and with **-norecurse**, cell in descendant items will be excluded.


[pathname]{.ins} [children]{.sub} [item]{.arg} [newchildren]{.optarg}
: If *newchildren* is not specified, returns the list of children belonging to *item*. If *newchildren* is specified, replaces *item*'s child list with *newchildren*. Items in the old child list not present in the new child list are detached from the tree. None of the items in *newchildren* may be an ancestor of *item*.

[pathname]{.ins} [collapse]{.sub} [-recurse]{.optarg} [itemList]{.arg}
: Sets all of the items in *itemList* to closed state. Same as "*pathname* **item** *item* **-open 0**" for each item in *itemList*. With the *-recurse* option, will also close all of their descendants. Use **{}** for root item.

[pathname]{.ins} [column]{.sub} [column]{.arg} [-option= ?+value -option value...]{.optarg}
: Query or modify the options for the specified *column*. If no *-option* is specified, returns a dictionary of option/value pairs. If a single *-option* is specified, returns the value of that option. Otherwise, the options are updated with the specified values. The following options may be set on each column:

    [-id]{.lit} [name]{.arg}
    : The column name. This is a read-only option. For example, \[*$pathname* **column #***n* **-id**\] returns the data column associated with display column *n*. The tree column has **-id #0**.

    [-anchor]{.lit} [anchor]{.arg}
    : Specifies how the text in this column should be aligned with respect to the cell. *Anchor* is one of **n**, **ne**, **e**, **se**, **s**, **sw**, **w**, **nw**, or **center**. See [Tk\_GetAnchor][GetAnchor] for more information.

    [-minwidth]{.lit} [minwidth]{.arg}
    : The minimum width of the column in pixels. The treeview widget will not make the column any smaller than **-minwidth** when the widget is resized or the user drags a heading column separator. The default is 20 pixels.

    [-separator]{.lit} [boolean]{.arg}
    : Specifies whether or not a column separator should be drawn to the right of the column. The default is false.

    [-stretch]{.lit} [boolean]{.arg}
    : Specifies whether or not the column width should be adjusted when the widget is resized or the user drags a heading column separator. *Boolean* may have any of the forms accepted by [Tcl\_GetBoolean][GetInt]. By default columns are stretchable.

    [-width]{.lit} [width]{.arg}
    : The width of the column in pixels. The default is 200 pixels. The specified column width may be changed by Tk in order to honor **-stretch** and/or **-minwidth**, or when the widget is resized or the user drags a heading column separator.


    Use *pathname* **column #0** to configure the tree column.

[pathname]{.ins} [current]{.sub}
: Returns the current item id and column id, if any, or {} if none. The current item is the item under the mouse pointer. It can be used in the **<Enter>** and **<Leave>** event handlers to get the item and column for the event.

[pathname]{.ins} [delete]{.sub} [itemList]{.arg}
: Deletes each of the items in *itemList* and all of their descendants. The root item may not be deleted. See also: **detach**.

[pathname]{.ins} [depth]{.sub} [item]{.arg}
: Returns the number of levels between *item* and the root item.

[pathname]{.ins} [detach]{.sub} [itemList]{.arg}
: Unlinks all of the specified items in *itemList* from the tree. The items and all of their descendants are still present and may be reinserted at another point in the tree with the **children** or **move** operations, but will not be displayed until that is done. The root item may not be detached. See also: **delete**.

[pathname]{.ins} [detached]{.sub} [-all=|+item]{.optlit}
: Without any arguments, returns a list of all detached items, but not their descendants, in an arbitrary order. If *item* is provided, returns a Boolean value indicating whether *item* or an ancestor is detached or not. If **-all** is specified, returns a list of all the detached items and their descendants in an arbitrary order. The root item is never detached.

[pathname]{.ins} [exists]{.sub} [item]{.arg}
: Returns 1 if the specified *item* is present in the tree, 0 if not.

[pathname]{.ins} [expand]{.sub} [-recurse]{.optarg} [itemList]{.arg}
: Set all of the items in *itemList* to open state. Same as "*pathname* **item** *item* **-open 1**" for each item in *itemList*. With the *-recurse* option, will also open all of their descendants. Use **{}** for root item.

[pathname]{.ins} [focus]{.sub} [item]{.optarg}
: Sets the focus item to *item*. Use **{}** to clear focus item. Without *item*, returns the current focus item id or **{}** if none.

[pathname]{.ins} [haschildren]{.sub} [item]{.arg}
: Returns 1 if *item* has children, 0 if not.

[pathname]{.ins} [heading]{.sub} [column]{.arg} [-option= ?+value -option value...]{.optarg}
: Query or modify the heading options for the specified *column*. Valid options are:

    [-anchor]{.lit} [anchor]{.arg}
    : Specifies how the heading text should be aligned. See [Tk\_GetAnchor][GetAnchor] for valid values.

    [-command]{.lit} [script]{.arg}
    : A script to evaluate when the heading label is pressed.


    Use *pathname heading #0* to configure the tree column heading.

    [-image]{.lit} [imageName]{.arg}
    : Specifies an image to display to the right of the column heading. Ignored by Mac Aqua theme.

    [-text]{.lit} [text]{.arg}
    : The text to display in the column heading.

    [state]{.cmd} [stateSpec]{.arg}
    : Modify or inquire about heading state. If *stateSpec* is present, sets the widget state: for each flag in stateSpec, sets the corresponding flag or clears it if prefixed by an exclamation point. To use the default sort direction indicator, set the heading to **user1** state plus either **selected** for descending sorts or **alternate** for ascending sorts. Remember to clear the states when a new heading is to be used as the sort column. Themes may set and use other states as needed for visual effects.


[pathname]{.ins} [hide]{.sub} [-recurse]{.optarg} [itemList]{.arg}
: Hide all of the items in *itemList* and all child items. Same as "*pathname* **item** *item* **-hidden 1**" for each item in *itemList*. With the *-recurse* option, will also hide all of their descendants. Use **{}** for root item.

[pathname]{.ins} [identifier]{.sub} [item]{.arg} [index]{.arg}
: Returns the identifier of the item at *index* within *item*'s list of children. See [Indices] for valid forms of *index*.

[pathname]{.ins} [identify]{.sub} [component]{.arg} [x]{.arg} [y]{.arg}
: Returns a description of the specified *component* under the point given by *x* and *y*, or the empty string if no such *component* is present at that position. The values *x* and *y* may have any of the forms acceptable to [Tk\_GetPixels][GetPixels]. The following subcommands are supported:

    [pathname]{.ins} [identify]{.sub} [region]{.lit} [x]{.arg} [y]{.arg}
    : Returns one of:

    heading
    : Tree heading area; use \[**pathname identify column** *x y*\] to determine the heading number.

    separator
    : Space between two column headings; \[**pathname identify column** *x y*\] will return the display column identifier of the heading to left of the separator.

    tree
    : The tree area.

    cell
    : A data cell.


[pathname]{.ins} [identify]{.sub} [item]{.lit} [x]{.arg} [y]{.arg}
: Returns the identifier of the item at position *x,y*.

[pathname]{.ins} [identify]{.sub} [column]{.lit} [x]{.arg} [y]{.arg}
: Returns the display column identifier of the cell at position *x,y*. The tree column has ID **#0**.

[pathname]{.ins} [identify]{.sub} [cell]{.lit} [x]{.arg} [y]{.arg}
: Returns the cell identifier of the cell at position *x,y*. A cell identifier is a list of item ID and column ID.

[pathname]{.ins} [identify]{.sub} [element]{.lit} [x]{.arg} [y]{.arg}
: The element at position *x,y*.

[pathname]{.ins} [identify]{.sub} [row]{.lit} [x]{.arg} [y]{.arg}
: Obsolescent synonym for *pathname* **identify item**.


See [Column identifiers] for a discussion of display columns and data columns.

[pathname]{.ins} [index]{.sub} [item]{.arg} [?index?]{.arg}
: Returns the numeric index of *item* within its parent's list of children or if *index* is specified, the index of the item at *index* in *item*'s list of children. See [Indices] for valid forms of *index*.

[pathname]{.ins} [insert]{.sub} [parent]{.arg} [index]{.arg} [[-id]{.lit} [id]{.arg}]{.optarg} [options...]{.arg}
: Creates a new item, inserts it in *parent*'s children at position *index*, and returns its identifier. See [Item options] for the list of available options. Use the empty string **{}** for *parent* to create a new top-level item. See [Indices] for valid forms of *index*. If *index* is negative or zero, the new item is inserted at the beginning; if *index* is greater than or equal to the current number of children, it is inserted at the end. If **-id** is specified, then *id* is used as the identifier of the item; *id* must not already exist in the tree. Otherwise, a new unique identifier is generated.

[pathname]{.ins} [insert]{.sub} [before=|§after]{.lit} [item]{.arg} [[-id]{.lit} [id]{.arg}]{.optarg} [options...]{.arg}
: Creates a new item, inserts it in *item*'s parent before or after *item*, and returns its identifier. See [Item options] for the list of available options. If **-id** is specified, it is used as the identifier of the item; *id* must not already exist in the tree. Otherwise, a new unique identifier is generated.

[pathname]{.ins} [item]{.sub} [item]{.arg} [-option= ?+value -option value...]{.optarg}
: Query or modify the options for the specified *item*. If no *-option* is specified, returns a dictionary of option/value pairs. If a single *-option* is specified, returns the value of that option. Otherwise, the item's options are updated with the specified values. See [Item options] for the list of available options.

[pathname]{.ins} [move]{.sub} [item]{.arg} [parent]{.arg} [index]{.arg}
: Moves *item* to position *index* in *parent*'s list of children. If *index* is negative or zero, *item* is moved to the beginning; if greater than or equal to the number of children, it is moved to the end. See [Indices] for valid forms of *index*. It is illegal to move an item under one of its descendants.

[pathname]{.ins} [move]{.sub} [item]{.arg} [before=|§after]{.lit} [otherItem]{.arg}
: Moves *item* to before or after *otherItem* in *otherItem*'s parent's children. It is illegal to move an item under one of its descendants.

[pathname]{.ins} [next]{.sub} [item]{.arg}
: Returns the identifier of *item*'s next sibling, or **{}** if *item* is the last child of its parent.

[pathname]{.ins} [parent]{.sub} [item]{.arg}
: Returns the ID of the parent of *item*, or **{}** if *item* is at the top level of the hierarchy.

[pathname]{.ins} [prev]{.sub} [item]{.arg}
: Returns the identifier of *item*'s previous sibling, or **{}** if *item* is the first child of its parent.

[pathname]{.ins} [range]{.sub} [-hidden]{.optlit} [-norecurse]{.optlit} [first]{.arg} [last]{.arg}
: Returns a list of items from *first* through *last*, inclusive. Use **-hidden** to include hidden items and **-nohidden** (default) to exclude them. Use **-recurse** to include descendants/ancestors of *item* (default) and **-norecurse** to exclude them. If both **-hidden** and **-recurse** are used, descendants of closed items will be included.

[pathname]{.ins} [search]{.sub} [parent]{.arg} [-option]{.optdot} [pattern]{.arg}
: Searches within *parent*'s children for *pattern* and returns the identifier of the first match. Valid options are: Comparisons

    [-ascii]{.lit}
    : The values are to be compared to *pattern* as Unicode strings (the name is for backward-compatibility reasons.) This is the default.

    [-dictionary]{.lit}
    : The values are to be compared to *pattern* using dictionary-style comparisons. For search, this is the same as **-ascii** but also sets **-nocase**. NOTE: This means I18n and I018N are not the same.

    [-integer]{.lit}
    : The values are to be compared to *pattern* as integers. Empty values will be ignored, but non-numeric values will generate an error.

    [-nocase]{.lit}
    : Comparisons are to be performed in a case-insensitive manner. Only used with **-ascii** and **-dictionary** options for the **-exact**, **-glob**, and **-regexp** matching.

    [-real]{.lit}
    : The values are to be compared to *pattern* as floating-point values. Empty values will be ignored, but non-numeric values will generate an error.

    [-unicode]{.lit}
    : Alias for **-ascii**. Direction

    [-backwards]{.lit}
    : The search is to be performed backwards starting from **-start** item or cell.

    [-forwards]{.lit}
    : The search is to be performed forwards starting from **-start** item or cell. This is the default. Pattern Matching

    [-exact]{.lit}
    : *Pattern* is a literal string that is compared for exact equality against each cell value. Used with **-ascii** and **-dictionary** options. This is the default.

    [-glob]{.lit}
    : *Pattern* is a glob-style pattern which is matched against each cell value using the same rules as the [string match][string] command. Used with **-ascii** and **-dictionary** options.

    [-regexp]{.lit}
    : *Pattern* is an advanced regular expression that is matched against each cell value using the rules described in the **re\_syntax** reference page. Used with **-ascii** and **-dictionary** options. Search Modifiers

    [-columns]{.lit} [columns]{.arg}
    : Limit search to only the columns specified in *columns* list. The default is to search in all displayed columns. See [Column identifiers] for valid forms of *column*.

    [-hidden]{.lit}
    : Search within hidden and closed items too. The default is to exclude hidden and descendants of closed items from the search.

    [-recurse]{.lit}
    : Search within all descendants of *parent*. The default is to only search within the children of *parent*.

    [-start]{.lit} [item=|+cell]{.arg}
    : Start the search at *item* or if the **-cell** option is also specified, then start the search at *cell*. The default start item and cell when used with the **-forwards** option is the first child of *parent* and the first display column. For the **-backwards** option, the default is the last child of *parent* (or descendant when used with **-recurse**) and the last display column. The *item* or *cell* must be a descendant of *parent*. When resuming a search, use the **before** or [after] subcommands to get the previous or next item to start the search at. All other options should be the same as the initial search including *parent*.

    [-stop]{.lit} [item=|+cell]{.arg}
    : Stop the search at *item* or if the **-cell** option is also specified, then stop the search at *cell*. The default stop item and cell when used with the **-forwards** option is the last child of *parent* (or descendant when used with **-recurse**) and the last display column. For the **-backwards** option, the default is the first child of *parent* and the first display column. When used with the **-wraparound** option, the default is just before the start item or cell (in other words, the start item or cell won't be searched again). This means, to wrap-around and stop at the start item, only use the **-start** and **-wraparound** options. If the **-start** and **-stop** options both use the same item, then only that item will be searched. The *item* or *cell* must be a descendant of *parent*.

    [-wraparound]{.lit}
    : When used with the **-start** option, enables the search to wrap-round to the first/last item in *parent* to continue to the **-stop** item or cell. Result Modifiers

    [-all]{.lit}
    : Return the identifiers of all of the items that match *pattern* rather than only the first one.

    [-cell]{.lit}
    : Return cell ids instead of item ids. The default is to return item ids. Also switches the **-start** and **-stop** options to use cell values.

    [-not]{.lit}
    : Negates the match, so only the items that do not match *pattern*, will be returned.


[pathname]{.ins} [see]{.sub} [item]{.arg} [column]{.optarg}
: Ensure that *item* is visible by setting all of *item*'s ancestors to **-open true**, and scroll the widget in the vertical direction, if necessary, so that *item* is within the visible portion of the tree. If *column* is specified, then the widget is scrolled in the horizontal direction, if necessary, so that *column* is within the visible portion of the tree. Only non-title columns and rows will be scrolled. Hint: if you only have the *cell*, use the expand operator "{\*}" to convert it into item and column components.

[pathname]{.ins} [selection]{.sub} [selop itemList|first last]{.optarg}
: Manages item selection. The item selection is independent from the cell selection handled by the **cellselection** subcommand. If *selop* is not specified, returns a list of selected items. Otherwise, *selop* is one of the following:

    [pathname]{.ins} [selection]{.sub} [add]{.lit} [itemList]{.arg}
    : Add items in *itemList* to the selection.

    [pathname]{.ins} [selection]{.sub} [add]{.lit} [-nohidden]{.optlit} [-norecurse]{.optlit} [first]{.arg} [last]{.arg}
    : Add the items from *first* to *last* (inclusive) to the selection. Use **-hidden** to include hidden items (default) and **-nohidden** to exclude them. Use **-recurse** to include descendants and ancestors (default) and **-norecurse** to exclude them. If **-hidden** and **-recurse** are used, all descendants of closed items will be included.

    [pathname]{.ins} [selection]{.sub} [anchor]{.lit} [item]{.optarg}
    : Sets the selection anchor to *item*. The anchor is used as the from item for the selection extend bindings. Without *item*, the current selection anchor is returned. Use empty string {} to unset the anchor.

    [pathname]{.ins} [selection]{.sub} [has]{.lit} [itemList]{.arg}
    : Returns 1 if selection includes all items in *itemList* or 0 if any are not selected.

    [pathname]{.ins} [selection]{.sub} [includes]{.lit} [itemList]{.arg}
    : Returns 1 if selection includes all items in *itemList* or 0 if any are not selected.

    [pathname]{.ins} [selection]{.sub} [present]{.lit}
    : Returns 1 if there are selected items or 0 if none.

    [pathname]{.ins} [selection]{.sub} [remove]{.lit} [itemList]{.arg}
    : Remove each item in *itemList* from the selection.

    [pathname]{.ins} [selection]{.sub} [remove]{.lit} [-nohidden]{.optlit} [-norecurse]{.optlit} [first]{.arg} [last]{.arg}
    : Remove the items from *first* to *last* (inclusive) from the selection. Use **-hidden** to include hidden items (default) and **-nohidden** to exclude them. Use **-recurse** to include descendants and ancestors (default) and **-norecurse** to exclude them. If **-hidden** and **-recurse** are used, descendants of closed items will be included.

    [pathname]{.ins} [selection]{.sub} [set]{.lit} [itemList]{.arg}
    : Sets selection to the items in *itemList*. All other items are unselected. Use an empty set **{}** to clear the selection.

    [pathname]{.ins} [selection]{.sub} [set]{.lit} [-nohidden]{.optlit} [-norecurse]{.optlit} [first]{.arg} [last]{.arg}
    : Sets selection to the items from *first* to *last* (inclusive). Use **-hidden** to include hidden items (default) and **-nohidden** to exclude them. Use **-recurse** to include descendants and ancestors (default) and **-norecurse** to exclude them. If **-hidden** and **-recurse** are used, descendants of closed items will be included.

    [pathname]{.ins} [selection]{.sub} [size]{.lit}
    : Returns the count of selected items.

    [pathname]{.ins} [selection]{.sub} [toggle]{.lit} [itemList]{.arg}
    : Toggle the selection state of each item in *itemList*.

    [pathname]{.ins} [selection]{.sub} [toggle]{.lit} [-nohidden]{.optlit} [-norecurse]{.optlit} [first]{.arg} [last]{.arg}
    : Toggle the selection state of the items from *first* to *last* (inclusive). Use **-hidden** to include hidden items (default) and **-nohidden** to exclude them. Use **-recurse** to include descendants and ancestors (default) and **-norecurse** to exclude them. If **-hidden** and **-recurse** are used, descendants of closed items will be included.


[pathname]{.ins} [set]{.sub} [item]{.arg} [column]{.optarg} [value]{.optarg} [column value]{.optdot}
: Set or get *item* column values. With no *column* or *value* arguments, returns a dictionary of all column/value pairs. With only the *column* argument, returns the current value for *column*. With one or more *column* and *value* pair arguments, sets the value of each *column* to the associated *value*. Use column **#0** for the tree column. See [Column identifiers] for valid forms of *column*.

[pathname]{.ins} [size]{.sub} [-hidden]{.optlit} [-recurse]{.optlit} [item]{.arg}
: Returns the count of children belonging to *item*. Use **-hidden** to include hidden items and **-nohidden** (default) to exclude them. Use **-recurse** to include descendants of *item* and **-norecurse** (default) to exclude them. If both **-hidden** and **-recurse** are used, descendants of closed items will be included.

[pathname]{.ins} [sort]{.sub} [parent]{.arg} [-option]{.optdot}
: Sort the children of *parent*. Valid options are:

    [-ascii]{.lit}
    : The values are to be sorted in Unicode character code order (the name is for backward-compatibility reasons.) This is the default.

    [-dictionary]{.lit}
    : The values are to be sorted using dictionary-style comparisons. This is the same as **-ascii** except (a) case is ignored except as a tie-breaker and (b) if two strings contain embedded numbers, the numbers compare as integers, not characters. For example, in **-dictionary** mode, "bigBoy" sorts between "bigbang" and "bigboy", and "x10y" sorts between "x9y" and "x11y". Overrides the **-nocase** option.

    [-integer]{.lit}
    : The values are to be sorted as integers. Non-integer values will generate an error.

    [-nocase]{.lit}
    : Comparisons are to be performed in a case-insensitive manner. Only used with **-ascii** option.

    [-real]{.lit}
    : The values are to be sorted as floating-point values. Non-numeric values will generate an error.

    [-unicode]{.lit}
    : Alias for **-ascii**.

    [-command]{.lit} [command]{.arg}
    : Use *command* as a comparison command. To compare two elements, evaluate a Tcl script consisting of command with the two elements appended as additional arguments. The script should return an integer less than, equal to, or greater than zero if the first element is to be considered less than, equal to, or greater than the second, respectively.

    [-increasing]{.lit}
    : Sort the list in increasing order ("smallest" items first). This is the default.

    [-decreasing]{.lit}
    : Sort the list in decreasing order ("largest" items first).

    [-column]{.lit} [column]{.arg}
    : Sort based on values in column *column*. See [Column identifiers] for valid forms of *column*. The default is the first display column (**#0** if tree in -show or **#1** if not).

    [-ignoreempty]{.lit}
    : Used with **-integer** or **-real** to ignore empty entries. Normally these would trigger an error and abort the sort. Empty entries will be sorted first for **-increasing** and last for **-decreasing**.

    [-recurse]{.lit}
    : Sort all descendants of *parent*. The default is to only sort the children of *parent*.


[pathName]{.ins} [tag]{.sub} [args...]{.arg}
: Manages tags. Tags can be set on items as well as on cells. The set of tags is shared between items and cells. However item tagging is independent from cell tagging (for instance adding a tag on an item does not also add this tag on the cells in that item). Cell tags take precedence over item tags when drawing. The following subcommands are supported:

    [pathName]{.ins} [tag]{.sub} [add]{.lit} [tagName]{.arg} [itemList]{.arg}
    : Adds tag *tagName* to each of the items in *itemList*. If *tagName* is already present for a particular item, then the **-tags** for that item are unchanged.

    [pathName]{.ins} [tag]{.sub} [bind]{.lit} [tagName]{.arg} [sequence]{.optarg} [script]{.optarg}
    : Add a Tk binding script for the event sequence *sequence* to the tag *tagName*. When an X event is delivered to an item, binding scripts for each of the item's **-tags** are evaluated in order as per *bindtags(n)*. If the event can be associated with a cell (i.e. mouse events) any bindings for the cell's **-tags** are evaluated as well.


    **<Key>**, **<KeyRelease>**, and virtual events are sent to the focus item. The **<Button>**, **<ButtonRelease>**, and **<Motion>** events are sent to the current item under the mouse pointer. The **<Enter>** and **<Leave>** events are sent to the current item under the mouse pointer or the last item under the mouse pointer, respectively. They are used to detect when the mouse pointer enters or leaves a cell. No other event types are supported.

    The binding *script* undergoes **%**-substitutions before evaluation; see [bind](n) for details.

[pathName]{.ins} [tag]{.sub} [cell]{.lit} [subcommand...]{.arg}
: Manages tags on individual cells. A *cellList* argument may be a single cell or a list of cells.

    [pathName]{.ins} [tag]{.sub} [cell]{.lit} [add]{.lit} [tagName]{.arg} [cellList]{.arg}
    : Adds tag *tagName* to each of the cells in *cellList*. If *tagName* is already present for a particular cell, then the tag list for that cell is not changed.

    [pathName]{.ins} [tag]{.sub} [cell]{.lit} [has]{.lit} [tagName]{.arg} [cell]{.optarg}
    : If *cell* is specified, returns a boolean value with whether *cell* has tag *tagName*. Without *cell*, a list of all cells which have tag *tagName* is returned.

    [pathName]{.ins} [tag]{.sub} [cell]{.lit} [remove]{.lit} [tagName]{.arg} [cellList]{.optarg}
    : Removes tag *tagName* from each of the cells in *cellList*. If *cellList* is omitted, tag *tagName* is removed from all cells in the tree.


[pathName]{.ins} [tag]{.sub} [configure]{.lit} [tagName]{.arg} [option]{.optarg} [value option value...]{.optarg}
: Query or modify the options for tag *tagName*. If one or more *option* and *value* pairs are specified, each option is set to the following value. If a single *option* is specified, the value of that option is returned (or the empty string if the option has not been specified for *tagName*). With no arguments, a dictionary of the option settings for tag *tagName* is returned. See [Tag options] for the list of available options.

[pathName]{.ins} [tag]{.sub} [delete]{.lit} [tagName]{.arg}
: Deletes tag *tagName* and any associated information. The tag is deleted from all items and cells in the widget and also any other information associated with the tag is deleted, such as bindings and display information.

[pathName]{.ins} [tag]{.sub} [has]{.lit} [tagName]{.arg} [item]{.optarg}
: If *item* is specified, returns a boolean value with whether *item* has tag *tagName* or not. Without *item*, returns a list of all items which have tag *tagName*.

[pathName]{.ins} [tag]{.sub} [names]{.lit}
: Returns a list of all tags used by the widget.

[pathName]{.ins} [tag]{.sub} [remove]{.lit} [tagName]{.arg} [itemList]{.optarg}
: Removes tag *tagName* from each of the items in *itemList*. If *itemList* is omitted, then tag *tagName* is removed from all items in the tree. If tag *tagName* is not present for an item, then the **-tags** for that item are not changed.

[pathname]{.ins} [unhide]{.sub} [-recurse]{.optarg} [itemList]{.arg}
: Unhide all of the items in *itemList*. Same as "*pathname* **item** *item* **-hidden 0**" for each item in *itemList*. With the *-recurse* option, will also unhide all of their descendants. Use **{}** for root item.

[pathname]{.ins} [visible]{.sub} [item]{.arg}
: Returns whether *item* is visible or not. An item is visible if it is not detached, hidden, and all ancestors are open and not hidden.


# Item options

The following item options can be set or get with the **insert** and **item** subcommands:

**-height** *integer*
: The height of the item in multiples of **-rowheight**. The default is 1.

**-image** *name*
: Display image *name* next to the label in the tree column, placed according to **-imageanchor**.

**-imageanchor** *anchor*
: Specifies how the **-image** is to be displayed relative to the text. The default is **w**. See [Tk\_GetAnchor][GetAnchor] for valid values.

**-hidden** *boolean*
: A Boolean value indicating whether the item should be displayed (**false**) or hidden (**true**). If a parent is hidden, then all its descendants are hidden too.

**-open** *boolean*
: A Boolean value indicating whether the item's children should be displayed (**true**) or hidden (**false**).

**-tags** *list*
: A list of tags associated with this item.

**-text** *text*
: The textual label to display for the item in the tree column.

**-values** *list*
: The list of values associated with the item. Each item should have the same number of values as the **-columns** widget option. If there are fewer values than columns, the remaining values are assumed empty. If there are more values than columns, the extra values are ignored.

**state** *stateSpec*
: Modify or query item state. If *stateSpec* is present, sets the widget state: for each flag in stateSpec, sets the corresponding flag or clears it if prefixed by an exclamation point. Without *stateSpec*, the current states are returned. The **focus**, **selected**, and **open** states can only be set or cleared via the associated subcommands.  


# Tag options

The following tag options can be set or get with the **tag configure** subcommand:

**-foreground** *color*
: Specifies the text foreground color.

**-background** *color*
: Specifies the cell or item background color.

**-font** *font*
: Specifies the font to use when drawing text.

**-image** *name*
: Specifies the cell or item image.

**-imageanchor** *anchor*
: Specifies the cell or item image anchor. See [Tk\_GetAnchor][GetAnchor] for valid values.

**-padding** *amount*
: Specifies the cell padding. A data cell will have a default padding of {4 0}. The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels].

**-stripedbackground** *boolean*
: Specifies the cell or item background color for alternate lines, if **-striped** is true.


Tags on cells have precedence over tags on items. Then, tag priority is decided by the creation order: tags created first receive higher priority. An item's options, like **-image** and **-imageanchor**, have priority over tags.

# Images

The -image option on an item, and on an item tag, controls the image next to the label in the tree column. Other cells can have images through the cell tag -image option.

# Default bindings

The following bindings are implemented by the Treeview class. The current item is the item with the focus and the current cell is the cell with the cell focus. Unless otherwise stated, on the Mac the Option key is used in place of the Control key in the descriptions below.

When **-selecttype** is set to **item**:

- The **<Left>** key will close an item if it has children or if not, move the focus to the parent item and select it.

- The **<Right>** key will open an item if it has children.

- The **<Up>** key will move the focus to the previous visible item and select it. The **<Shift-Up>** will extend the selection to the previous visible item (multiple and extended select modes only). The **<Control-Up>** will move the focus to the top-most on-screen item and select it. The **<Control-Shift-Up>** will extend the selection to the top-most on-screen item (multiple and extended select modes only).

- The **<Down>** key will move the focus to the next visible item and select it. The **<Shift-Down>** will extend the selection to the next visible item (multiple and extended select modes only). The **<Control-Down>** will move the focus to the bottom-most on-screen item and select it. The **<Control-Shift-Down>** will extend the selection to the bottom-most on-screen item (multiple and extended select modes only).

- The **<Home>** key will move the focus to the first child of the current item's parent. The **<Shift-Home>** will extend the selection to the first child of the current item's parent (multiple and extended select modes only). The **<Control-Home>** (or **<Command-Home>** on Mac) will move the focus to the first item in the tree. The **<Control-Shift-Home>** (or **<Command-Shift Home>** on Mac) will extend the selection to the first item in the tree (multiple and extended select modes only).

- The **<End>** key will move the focus to the last child of the current item's parent. The **<Shift-End>** will extend the selection to the last child of the current item's parent (multiple and extended select modes only). The **<Control-End>** (or **<Command-End>** on Mac) will move the focus to the last item in the tree. The **<Control-Shift-End>** (or **<Command-Shift-End>** on Mac) will extend the selection to the last item in the tree (multiple and extended select modes only).

- The **<Prior>** key will scroll up 1 screenful and set the focus and selection to the item at the same location. The **<Shift-Prior>** will scroll left 1 screenful. The **<Control-Prior>** (or **<Command-Prior>** on Mac) will move to the top-most on-screen item and select it.

- The **<Next>** key will scroll down 1 screenful and set the focus and selection to the item at the same location. The **<Shift-Next>** will scroll right 1 screenful. The **<Control-Next>** (or **<Command-Next>** on Mac) will move to the bottom-most on-screen item and select it.

- The **<Return>** key will invoke the current item. For an item with children, will toggle open/close state or if no children toggle the selection state. The **<Shift-Return>** is the same as the **<Return>** key. The **<Control-Return>** and **F2** keys (or **space** and **<Option-Return>** on Mac) will activate the current item by calling **::ttk::treeview::ActivateItem**, if it exists, with the widget name, item, and column id arguments. If not and item has children, the open state of the current item will be toggled. Else, the item will be selected.

- The **<space>** key will toggle the selection state of the current item (single, multiple, and extended select modes only). The **<Shift-space>** will select the current item (single, multiple, and extended select modes only). The **<Control-Shift-space>** will select all items (multiple and extended select modes only).

- The **<Tab>** and **<Shift-Tab>** keys will move the focus to the next and previous widgets, respectively. The **<Control-Tab>** and **<Control-Shift-Tab>** keys (including Mac) will move the focus to the next and previous widgets, respectively.

- The **<plus>** keys will open the current item, if it has children. The **<minus>** keys will close the current item, if it has children. The **<asterisk>** key will open the current item and all of its descendants, it it has children.


When **-selecttype** is set to **cell**:

- The **<Left>** key will move the focus to the previous cell in the row and select it. **<Shift-Left>** will extend the selection to the previous cell (multiple and extended select modes only). **<Control-Left>** will move the focus to the left-most on-screen cell in the row and select it. **<Control-Shift-Left>** will extend the selection to the left-most on-screen cell in the row (multiple and extended select modes only).

- The **<Right>** key will move move the focus to the next cell in the row and select it. **<Shift-Right>** will extend the selection to the next cell (multiple and extended select modes only). **<Control-Right>** will move the focus to the right-most on-screen cell in the row and select it. **<Control-Shift-Left>** will extend the selection to the right-most on-screen cell in the row (multiple and extended select modes only).

- The **<Up>** key will move the focus to the above cell in the column and select it. **<Shift-Up>** will extend the selection to the above cell (multiple and extended select modes only). **<Control-Up>** will move the focus to the cell at the top of the screen in the same column and select it. **<Control-Shift-Up>** will extend the selection to the cell at the top of the screen in the same column (multiple and extended select modes only).

- The **<Down>** key will move the focus to the below cell in the column and select it. **<Shift-Down>** will extend the selection to the below cell (multiple and extended select modes only). **<Control-Down>** will move the focus to the cell at the bottom of the screen in the same column and select it. **<Control-Shift-Down>** will extend the selection to the cell at the bottom of the screen in the same column (multiple and extended select modes only).

- The **<Home>** key (and **<Command-Left>** on Mac) will move the focus to the first cell in the row and select it. **<Shift-Home>** (and **<Command-Shift-Left>** on Mac) will extend the selection to the first cell in the row (multiple and extended select modes only). **<Control-Home>** (or **<Command-Home>** on Mac) will move the focus to the top-left cell in the tree and select it. **<Control-Shift-Home>** (or **<Command-Shift-Home>** on Mac) will extend the selection to the top-left cell in the tree (multiple and extended select modes only).

- The **<End>** key (and (**<Command-Right>** on Mac) will move the focus to the last cell in the row and select it. **<Shift-End>** (and **<Command-Shift-Right>** on Mac) will extend the selection to the last cell in the row (multiple and extended select modes only). **<Control-End>** (or **<Command-End>** on Mac) will move the focus to the bottom-right cell in the tree and select it. **<Control-Shift-End>** (or **<Command-Shift-End>** on Mac) will extend the selection to the bottom-right cell in the tree (multiple and extended select modes only).

- The **<Prior>** key will scroll up 1 screenful and set the focus and selection to the cell at the same location. **<Shift-Prior>** will scroll left 1 screenful and set the focus and selection to the cell at the same location. **<Control-Prior>** (or **<Command-Prior>** on Mac) will move the focus to the cell at the top of the screen in the same column and select it. **<Control-Shift-Prior>** (or **<Command-Shift-Prior>** on Mac) will move the focus to the cell at the right side of the screen in the same row and select it.

- The **<Next>** key will scroll down 1 screenful and set the focus and selection to the cell at the same location. **<Shift-Next>** will scroll right 1 screenful and set the focus and selection to the cell at the same location. **<Control-Next>** (or **<Command-Next>** on Mac) will move the focus to the cell at the bottom of the screen in the same column and select it. **<Control-Shift-Next>** (or **<Command-Shift-Next>** on Mac) will move the focus to the cell at the left side of the screen in the same row and select it.

- The **<Return>** and **F2** keys will activate the current cell by calling **::ttk::treeview::ActivateItem**, if it exists, with the widget name, item, and column id arguments. If not, same as **<Down>**. **<Shift-Return>** is same as **Up**.

- The **<space>** key will toggle the selection state of the current cell (single, multiple, and extended select modes only). **<Shift-space>** will select all cells in the row (multiple and extended select modes only). **<Control-space>** (or **<Command-space>** on Mac) will select all cells in the column (multiple and extended select modes only). **<Control-Shift-space>** (or **<Command-Shift-space>** on Mac) will select all cells (multiple and extended select modes only).

- **<Tab>** is same as **Right**. **<Shift-Tab>** is same as **Left**. The **<Control-Tab>** and **<Control-Shift-Tab>** keys (including Mac) will move the focus to the next and previous widgets, respectively.


Applies to all:

- **<Button-1>** will set the focus (all modes) and selection (all except none select mode) to the item or cell under the mouse pointer.

- **<B1-Motion>** will drag a column separator for resize, a column heading for a column move, selected item/cell when in browse select mode, or extend the selection when in extended select mode.

- **<ButtonRelease-1>** will drop a column separator for resize, column heading for a move, or set the selection.

- **<Double-Button-1>** will activate an item or cell by calling ::ttk::treeview::ActivateItem. If pressed over a column divider, will auto size the column to its contents. This works best if columnm **-stretch** is 0. Auto size will honor the column **-minwidth** value.

- **<Alt-Double-Button-1>** (or **<Option-Double-Button-1>** for a Mac) on a column divider will auto size all columns to their contents. This works best if all columns have **-stretch** set to 0. Auto size will honor the **-minwidth** value for each column.

- **<Shift-Button-1>** will select a range of items or cells from the anchor to the selected item/cell (multiple and extended select modes only). If pressed in a heading when **-selecttype** is set to **cell** and in multiple or extended select mode, then all of the column cells will be selected.

- **<Control-Button-1>** will toggle the selection state of an item or cell (single, multiple, and extended select modes only). If pressed in a heading when **-selecttype** is set to **cell** and in multiple or extended select mode, then all of the column cells will be selected.

- **<<SelectAll>>** virtual event will select all items or cells in the widget (multiple and extended select modes only).

- **<<SelectNone>>** virtual event will unselect all items or cells (except browse mode).

- **<<SelectInvert>>** virtual event will invert or toggle the selection state of all items or cells (multiple or extended select modes only).

- The **<<Copy>>** virtual event will copy the contents of the selected items or cells to the clipboard.


# Virtual events

The treeview widget generates the following virtual events.

**<<TreeviewSelect>>**
: Generated whenever the selection or cell selection changes. It may also be generated when the selection is affected but not actually changed. Furthermore, multiple selection changes could happen before events can be processed leading to multiple events with the same visible selection. Use the **selection** or **cellselection** subcommands to get the current selection.

**<<TreeviewOpen>>**
: Only triggered when the tree closed indicator is pressed and generated just before setting the current item to **-open true**. To get the current item, use the detail/user\_data field (**%d**) in the binding script (see event **-detail** option) or the **current** subcommand.

**<<TreeviewClose>>**
: Only triggered when the tree open indicator is pressed and generated just after setting the current item to **-open false**. To get the current item, use the detail/user\_data field (**%d**) in the binding script (see event **-detail** option) or the **current** subcommand.

**<<TreeviewFocus>>**
: Triggered when the focus item or cell changes including when unset. To get the focus item and column, use the detail/user\_data field (**%d**) in the binding script (see event **-detail** option) or the use the **focus** or **cellfocus** subcommands to get the current focus. The former can return either a 1 or 2 item list with just the focus item or both the item and column.


# Standard styles

**Ttk::treeview** widget also support the **CheckTreeview** style in all standard themes. This style adds a checkbutton after the open/close indicator and before the item image to better show which items are selected. Combine this style with the **-selectmode multiple** option to create a check list widget where the user can easily select multiple entries without the need to use the keyboard. This style works best with the **item** select type, not **cell**.

# Styling options

The following are the widget components for the [ttk::style][ttk_style] command:

- The class name for a **ttk::treeview** is **Treeview**.

- The treeview heading class name is **Heading**.

- The treeview row class name is **Row**.

- The treeview item class name is **Item**.

- The treeview cell class name is **Cell**.


Dynamic states: **active**, **background**, **disabled**, **focus**, and **selected**.

The **Treeview** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color*
: Specifies the background color of rows that contain data values.

**-columnseparatorwidth** *amount*
: Specifies the width of column separators. The default is 1 pixel. The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels].

**-fieldbackground** *color*
: Specifies the background color of the unused portion of the widget.

**-font** *font*
: Specifies the the font for the contents of the tree (not heading).

**-foreground** *color*
: Specifies the foreground color of rows that contain data values.

**-headingheight** *amount*
: This is the default height for heading cells. If set to 0, the value will be auto calculated by measuring the heading layout with the style's settings. This includes the font size and any padding from the theme's layout.

**-indent** *amount*
: Specifies the indentation amount of child elements below parent elements within the tree column. The default is 20 pixels. The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels].

**-padding** *amount*
: Specifies the additional padding to include inside the border of the widget body, including the heading. The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels].

**-stripedbackground** *color*
: This is the background color to use for the **-striped** option.

**-rowheight** *amount*
: This is the default height for items and cells. If set to 0, the value will be auto calculated by measuring the item and cell layout with the style's settings. This includes the font size(s), focus ring, and padding from the theme's layout.


Example of how to set **-rowheight**, adapting to a font in a similar way to how the default value is set:

```
ttk::style configure Treeview \
     -rowheight [expr {[font metrics font -linespace] + 2}]
ttk::style configure Treeview \
     -headingheight [expr {[font metrics font -linespace] + 2}]
```

The **Heading** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color*
: Specifies the background color of the heading.

**-font** *font*
: Specifies the the font for the heading.

**-foreground** *color*
: Specifies the foreground color of the heading.

**-indicatormargin** *amount*
: Specifies the additional padding to include around the indicator. The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels]. The default is "2 2 4 2".

**-indicatorsize** *pixels*
: Specifies the size of the indicator in pixels. The default is 3.5 points.

**-padding** *amount*
: Specifies the additional padding to include inside the border of the heading. The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels].

**-relief** *relief*
: Specifies the relief to use for the heading.


Example of how to set the current sorted column heading font to bold black and all other column heading fonts to normal gray similar to how the Mac Finder works:

```
ttk::style map Heading -foreground {selected black alternate black}
```

The **Row** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color*
: Specifies the background color of the tree entries. The default is same as Treeview.

**-focuscolor** *color*
: Specifies the color of the focus ring for tree entries. The default is #000000. Some themes may change the default focus color.

**-focussolid** *boolean*
: Specifies whether to draw the focus ring as a dotbox (false) or solid ring (true). The default is true.

**-focusthickness** *amount*
: Specifies the thickness of the focus ring for tree entries. The default is 1 for the focus element. The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels].

**-padding** *amount*
: Specifies the additional padding to include between the focus ring and row contents. The default is 0. The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels].


Example of how to set focus options for a spreadsheet-style focus ring that is only shown when a cell has the focus:

```
ttk::style configure Row -focuscolor #008000 -focussolid 1 -focusthickness 0
ttk::style map Row -focusthickness [list focus 2]
```

The **Item** (tree column only) styling options configurable with [ttk::style][ttk_style] are:

**-foreground** *color*
: Specifies the foreground color of the tree items. The default is same as Treeview.

**-indicatormargin** *amount*
: Specifies the additional padding to include around the indicator. The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels].

**-indicatorsize** *pixels*
: Specifies the size of the indicator in pixels.

**-padding** *amount*
: Specifies the additional padding to include inside the border of the tree items. The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels].


The **Cell** (non-tree columns) styling options configurable with [ttk::style][ttk_style] are:

**-padding** *amount*
: Specifies the additional padding to include inside the border of the cell. The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels].


Some options are only available for specific themes.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.


[after]: ../Tcl/after.md
[bind]: bind.md
[GetAnchor]: ../TkCAPI/GetAnchor.md
[GetInt]: ../TclCAPI/GetInt.md
[GetPixels]: ../TkCAPI/GetPixels.md
[next]: ../Tcl/next.md
[string]: ../Tcl/string.md
[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

