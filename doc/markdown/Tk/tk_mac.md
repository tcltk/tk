---
CommandName: tk::mac
title: tk::mac
ManualSection: n
Version: 8.6
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - bind(n)
 - wm(n)
Keywords:
 - about dialog
 - antialiasing
 - Apple event
 - icon
 - NSImage
Copyright:
 - Copyright (c) 2011 Kevin Walzer
 - Copyright (c) 2011 Donal K. Fellows
---

# Name

tk::mac - Access Mac-Specific Functionality on macOS from Tk

# Synopsis

::: {.synopsis} :::
[::tk::mac::DoScriptFile]{.cmd}
[::tk::mac::DoScriptText]{.cmd}
[::tk::mac::ShowPreferences]{.cmd}
[::tk::mac::OpenApplication]{.cmd}
[::tk::mac::ReopenApplication]{.cmd}
[::tk::mac::OpenDocument]{.cmd} [file...]{.arg}
[::tk::mac::PrintDocument]{.cmd} [file...]{.arg}
[::tk::mac::Quit]{.cmd}
[::tk::mac::OnHide]{.cmd}
[::tk::mac::OnShow]{.cmd}
[::tk::mac::ShowHelp]{.cmd}
[::tk::mac::PerformService]{.cmd}
[::tk::mac::LaunchURL]{.cmd} [URL...]{.arg}
[::tk::mac::GetAppPath]{.cmd}
[::tk::mac::GetInfoAsJSON]{.cmd}

[::tk::mac::standardAboutPanel]{.cmd}

[::tk::mac::useCompatibilityMetrics]{.cmd} [boolean]{.arg}
[::tk::mac::CGAntialiasLimit]{.cmd} [limit]{.arg}
[::tk::mac::antialiasedtext]{.cmd} [number]{.arg}
[::tk::mac::useThemedToplevel]{.cmd} [boolean]{.arg}

[::tk::mac::iconBitmap]{.cmd} [name]{.arg} [width]{.arg} [height]{.arg} [-kind]{.arg} [value]{.arg}
:::

# Event handler callbacks

The Aqua/macOS application environment defines a number of additional events that applications should respond to. These events are mapped by Tk to calls to commands in the **::tk::mac** namespace; unless otherwise noted, if the command is absent, no action will be taken.

[::tk::mac::DoScriptFile]{.cmd}
: The default Apple Event handler for AEDoScriptHandler. This command executes a Tcl file when an AppleScript sends a "do script" command to Wish with a file path as a parameter.

[::tk::mac::DoScriptText]{.cmd}
: The default Apple Event handler for AEDoScriptHandler. This command executes Tcl code when an AppleScript sends a "do script" command to Wish with Tcl code or a Tcl procedure as a parameter.

[::tk::mac::ShowPreferences]{.cmd}
: The default Apple Event handler for kAEShowPreferences, "pref". The application menu "Preferences" menu item is only enabled when this proc is defined. Typically this command is used to wrap a specific own preferences command, which pops up a preferences window. Something like:

    ```
    proc ::tk::mac::ShowPreferences {} {
        setPref
    }
    ```

[::tk::mac::OpenApplication]{.cmd}
: If a proc of this name is defined, this proc fill fire when your application is initially opened. It is the default Apple Event handler for kAEOpenApplication, "oapp".

[::tk::mac::ReopenApplication]{.cmd}
: If a proc of this name is defined it is the default Apple Event handler for kAEReopenApplication, "rapp", the Apple Event sent when your application is opened when it is already running (e.g. by clicking its icon in the Dock). Here is a sample that raises a minimized window when the Dock icon is clicked:

    ```
    proc ::tk::mac::ReopenApplication {} {
        if {[wm state .] eq "withdrawn"} {
            wm state . normal
        } else {
            wm deiconify .
        }
        raise .
    }
    ```

[::tk::mac::OpenDocument]{.cmd} [file...]{.arg}
: If a proc of this name is defined it is the default Apple Event handler for kAEOpenDocuments, "odoc", the Apple Event sent when your application is asked to open one or more documents (e.g., by drag & drop onto the app or by opening a document of a type associated to the app). The proc should take as arguments paths to the files to be opened, like so:

    ```
    proc ::tk::mac::OpenDocument {args} {
        foreach f $args {my_open_document $f}
    }
    ```

[::tk::mac::PrintDocument]{.cmd} [file...]{.arg}
: If a proc of this name is defined it is the default Apple Event handler for kAEPrintDocuments, "pdoc", the Apple Event sent when your application is asked to print a document.  It takes a single absolute file path as an argument.

[::tk::mac::Quit]{.cmd}
: If a proc of this name is defined it is the default Apple Event handler for kAEQuitApplication, "quit", the Apple Event sent when your application is asked to be quit, e.g. via the quit menu item in the application menu, the quit menu item in the Dock menu, or during a logout/restart/shutdown etc. If this is not defined, [exit] is called instead.

[::tk::mac::OnHide]{.cmd}
: If defined, this is called when your application receives a kEventAppHidden event, e.g. via the hide menu item in the application or Dock menus.

[::tk::mac::OnShow]{.cmd}
: If defined, this is called when your application receives a kEventAppShown event, e.g. via the show all menu item in the application menu, or by clicking the Dock icon of a hidden application.

[::tk::mac::ShowHelp]{.cmd}
: Customizes behavior of Apple Help menu; if this procedure is not defined, the platform-specific standard Help menu item "YourApp Help" performs the default Cocoa action of showing the Help Book configured in the application's Info.plist (or displaying an alert if no Help Book is set).

[::tk::mac::PerformService]{.cmd}
: Executes a Tcl procedure called from the macOS "Services" menu in the Application menu item. The "Services" menu item allows for inter-application communication; data from one application, such as selected text, can be sent to another application for processing, for example to Safari as a search item for Google, or to TextEdit to be appended to a file. An example of the proc is below, and should be rewritten in an application script for customization:

    ```
    proc ::tk::mac::PerformService {} {
        set data [clipboard get]
        $w insert end $data
    }
    ```

    Note that the mechanism for retrieving the data is from the clipboard; there is no other supported way to obtain the data.  If the Services process is not desired, the NSServices keys can be deleted from the application's Info.plist file. The underlying code supporting this command also allows the text, entry and ttk::entry widgets to access services from other applications via the Services menu. The NSPortName key in Wish's Info.plist file is currently set as "Wish" ; if a developer changes the name of the Wish executable to something   else, this key should be modified with the same name.

[::tk::mac::LaunchURL]{.cmd} [URL...]{.arg}
: If defined, launches a URL within Tk. This would be used if a Tk application wants to handle a URL itself, such as displaying data from an RSS feed, rather than launching a default application to handle the URL, although it can defined as such. Wish includes a stub URL scheme of "foo://" in the CFBundleURLSchemes key of its Info.plist file; this should be customized for the specific URL scheme the developer wants to support.

**::tk::mac::GetAppPath**
: Returns the current applications's file path.

**::tk::mac::GetInfoAsJSON**
: Returns a JSON-encoded Tcl string which serializes the application's **mainBundle.infoDictionary** (defined by its *Info.plist* file).


# Additional dialogs

Aqua/macOS defines additional dialogs that applications should support.

[::tk::mac::standardAboutPanel]{.cmd}
: Brings the standard Cocoa about panel to the front with information filled in from the application bundle files. The panel displays the application icon and the values associated to the info.plist keys named CFBundleName, CFBundleShortVersionString, NSAboutPanelOptionVersion and NSHumanReadableCopyright.  If a file named *Credits.html* or *Credits.rtf* exists in the bundle's Resources directory then its contents will be displayed in a scrolling text box at the bottom of the dialog. See the documentation for -\[NSApplication orderFrontStandardAboutPanelWithOptions:\] for more details. A hook is also provided for a custom About dialog.  If a Tcl proc named tkAboutDialog is defined in the main interpreter then that procedure will be called instead of opening the standardAboutPanel.


# System configuration

There are a number of additional global configuration options that control the details of how Tk renders by default.

[::tk::mac::useCompatibilityMetrics]{.cmd} [boolean]{.arg}
: Preserves compatibility with older Tk/Aqua metrics; set to **false** for more native spacing.

[::tk::mac::CGAntialiasLimit]{.cmd} [limit]{.arg}
: Sets the antialiasing limit; lines thinner that *limit* pixels will not be antialiased. Integer, set to 0 by default, making all lines be antialiased.

[::tk::mac::antialiasedtext]{.cmd} [number]{.arg}
: Sets anti-aliased text.  Controls text antialiasing, possible values for *number* are -1 (default, use system default for text AA), 0 (no text AA), 1 (use text AA).

[::tk::mac::useThemedToplevel]{.cmd} [boolean]{.arg}
: Sets toplevel windows to draw with the modern grayish/ pinstripe Mac background. Equivalent to configuring the toplevel with "**-background systemWindowHeaderBackground**", or to using a [ttk::frame][ttk_frame].


# Support commands

[::tk::mac::iconBitmap]{.cmd} [name]{.arg} [width]{.arg} [height]{.arg} [-kind]{.arg} [value]{.arg}
: Renders native icons and bitmaps in Tk applications (including any image file readable by NSImage). A native bitmap name is interpreted as follows (in order):

-     predefined builtin 32x32 icon name (**stop**, **caution**, **document**, etc.)

-     *name*, as defined by **tk::mac::iconBitmap**

-     NSImage named image name

-     NSImage url string

-     4-char OSType of IconServices icon


    The *width* and *height* arguments to **tk::mac::iconBitmap** define the dimensions of the image to create, and *-kind* must be one of:

    [-file]{.lit}
    : icon of file at given path

    **-fileType**
    : icon of given file type

    **-osType**
    : icon of given 4-char OSType file type

    **-systemType**
    : icon for given IconServices 4-char OSType

    **-namedImage**
    : named NSImage for given name

    **-imageFile**
    : image at given path




[exit]: ../Tcl/exit.md
[ttk_frame]: ttk_frame.md

