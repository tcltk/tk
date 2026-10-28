---
CommandName: tk_getOpenFile
title: tk_getOpenFile
ManualSection: n
Version: 4.2
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - tk_chooseDirectory
Keywords:
 - file selection dialog
Copyright:
 - Copyright (c) 1996 Sun Microsystems, Inc.
---

# Name

tk\_getOpenFile, tk\_getSaveFile - pop up a dialog box for the user to select a file to open or save.

# Synopsis

::: {.synopsis} :::
[tk\_getOpenFile]{.cmd} [option value]{.optdot}
[tk\_getSaveFile]{.cmd} [option value]{.optdot}
:::

# Description

The procedures **tk\_getOpenFile** and **tk\_getSaveFile** pop up a dialog box for the user to select a file to open or save. The **tk\_getOpenFile** command is usually associated with the **Open** command in the **File** menu. Its purpose is for the user to select an existing file *only*. If the user enters a non-existent file, the dialog box gives the user an error prompt and requires the user to give an alternative selection. If an application allows the user to create new files, it should do so by providing a separate **New** menu command.

The **tk\_getSaveFile** command is usually associated with the **Save as** command in the **File** menu. If the user enters a file that already exists, the dialog box prompts the user for confirmation whether the existing file should be overwritten or not.

The following *option-value* pairs are possible as command line arguments to these two commands:

[-command]{.lit} [string]{.arg}
: Specifies the prefix of a Tcl command to invoke when the user closes the dialog after having selected an item. This callback is not called if the user cancelled the dialog. The actual command consists of *string* followed by a space and the value selected by the user in the dialog. This is only available on macOS.

[-confirmoverwrite]{.lit} [boolean]{.arg}
: Configures how the Save dialog reacts when the selected file already exists, and saving would overwrite it.  A true value requests a confirmation dialog be presented to the user.  A false value requests that the overwrite take place without confirmation.  Default value is true.

[-defaultextension]{.lit} [extension]{.arg}
: Specifies a string that will be appended to the filename if the user enters a filename without an extension. The default value is the empty string, which means no extension will be appended to the filename in any case. This option is ignored on macOS, which does not require extensions to filenames, and the UNIX implementation guesses reasonable values for this from the **-filetypes** option when this is not supplied.

[-filetypes]{.lit} [filePatternList]{.arg}
: If a **File types** listbox exists in the file dialog on the particular platform, this option gives the *filetype*s in this listbox. When the user choose a filetype in the listbox, only the files of that type are listed. If this option is unspecified, or if it is set to the empty list, or if the **File types** listbox is not supported by the particular platform then all files are listed regardless of their types. See the section [Specifying file patterns] below for a discussion on the contents of *filePatternList*.

[-initialdir]{.lit} [directory]{.arg}
: Specifies that the files in *directory* should be displayed when the dialog pops up. If this parameter is not specified, the initial directory defaults to the current working directory on non-Windows systems and on Windows systems prior to Vista. On Vista and later systems, the initial directory defaults to the last user-selected directory for the application. If the parameter specifies a relative path, the return value will convert the relative path to an absolute path.

[-initialfile]{.lit} [filename]{.arg}
: Specifies a filename to be displayed in the dialog when it pops up.

[-message]{.lit} [string]{.arg}
: Specifies a message to include in the client area of the dialog. This is only available on macOS.

[-multiple]{.lit} [boolean]{.arg}
: Allows the user to choose multiple files from the Open dialog.

[-parent]{.lit} [window]{.arg}
: Makes *window* the logical parent of the file dialog. The file dialog is displayed on top of its parent window. On macOS, this turns the file dialog into a sheet attached to the parent window.

[-title]{.lit} [titleString]{.arg}
: Specifies a string to display as the title of the dialog box. If this option is not specified, then a default title is displayed.

[-typevariable]{.lit} [variableName]{.arg}
: The global variable *variableName* is used to preselect which filter is used from *filterList* when the dialog box is opened and is updated when the dialog box is closed, to the last selected filter. The variable is read once at the beginning to select the appropriate filter. If the variable does not exist, or its value does not match any filter typename, or is empty (**{}**), the dialog box will revert to the default behavior of selecting the first filter in the list. If the dialog is canceled, the variable is not modified.


If the user selects a file, both **tk\_getOpenFile** and **tk\_getSaveFile** return the full pathname of this file. If the user cancels the operation, both commands return the empty string.

# Specifying file patterns

The *filePatternList* value given by the **-filetypes** option is a list of file patterns. Each file pattern is a list of the form

```
typeName {extension ?extension ...?} ?{macType ?macType ...?}?
```

*typeName* is the name of the file type described by this file pattern and is the text string that appears in the **File types** listbox. *extension* is a file extension for this file pattern. *macType* is a four-character Macintosh file type. The list of *macType*s is optional and may be omitted for applications that do not need to execute on the Macintosh platform.

Several file patterns may have the same *typeName,* in which case they refer to the same file type and share the same entry in the listbox. When the user selects an entry in the listbox, all the files that match at least one of the file patterns corresponding to that entry are listed. Usually, each file pattern corresponds to a distinct type of file. The use of more than one file pattern for one type of file is only necessary on the Macintosh platform.

On the Macintosh platform, a file matches a file pattern if its name matches at least one of the *extension*(s) AND it belongs to at least one of the *macType*(s) of the file pattern. For example, the **C Source Files** file pattern in the sample code matches with files that have a **\\.c** extension AND belong to the *macType* **TEXT**. To use the OR rule instead, you can use two file patterns, one with the *extensions* only and the other with the *macType* only. The **GIF Files** file type in the sample code matches files that *either* have a **\\.gif** extension OR belong to the *macType* **GIFF**.

On the Unix and Windows platforms, a file matches a file pattern if its name matches at least one of the *extension*(s) of the file pattern. The *macType*s are ignored.

# Specifying extensions

On the Unix and Macintosh platforms, extensions are matched using glob-style pattern matching. On the Windows platform, extensions are matched by the underlying operating system. The types of possible extensions are:

1. the special extension "\*" matches any file;

2. the special extension "" matches any files that do not have an extension (i.e., the filename contains no full stop character);

3. any character string that does not contain any wild card characters (\* and ?).


Due to the different pattern matching rules on the various platforms, to ensure portability, wild card characters are not allowed in the extensions, except as in the special extension "\*". Extensions without a full stop character (e.g. "~") are allowed but may not work on all platforms.

# Example

```
set types {
    {{Text Files}       {.txt}        }
    {{TCL Scripts}      {.tcl}        }
    {{C Source Files}   {.c}      TEXT}
    {{GIF Files}        {.gif}        }
    {{GIF Files}        {}        GIFF}
    {{All Files}        *             }
}
set filename [tk_getOpenFile -filetypes $types]

if {$filename ne ""} {
    # Open the file ...
}
```

