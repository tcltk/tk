/*
 * tkWinX.c --
 *
 *	This file contains Windows emulation procedures for X routines.
 *
 * Copyright © 1995-1996 Sun Microsystems, Inc.
 * Copyright © 1994 Software Research Associates, Inc.
 * Copyright © 1998-2000 Scriptics Corporation.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#define XLIB_ILLEGAL_ACCESS
#include "tkWinInt.h"

#include <commctrl.h>
#ifdef _MSC_VER
#   pragma comment (lib, "comctl32.lib")
#   pragma comment (lib, "advapi32.lib")
#endif


/*
 * The zmouse.h file includes the definition for WM_MOUSEWHEEL.
 */

#include <zmouse.h>

/*
 * WM_MOUSEHWHEEL is normally defined by Winuser.h for Vista/2008 or later,
 * but is also usable on 2000/XP if IntelliPoint drivers are installed.
 */

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

/* A WM_MOUSEWHEEL message sent by a trackpad contains the number of pixels as
 * the delta value, while low precision scrollwheels always send an integer
 * multiple of WHEELDELTA (= 120) as the delta value.
 */

#define WHEELDELTA 120

/*
 * Our heuristic for deciding whether a WM_MOUSEWHEEL message
 * comes from a high resolution scrolling device is that we
 * assume it is high resolution unless there are two consecutive
 * delta values that are both multiples of 120.  This is static,
 * rather than thread-specific, since input devices are shared
 * by all threads.
 */

static int lastMod = 0;

/*
 * The serial field of TouchpadScroll events is a counter for
 * events of this type only.
 */

static unsigned long scrollCounter = 0;

/*
 * imm.h is needed by HandleIMEComposition
 */

#include <imm.h>
#ifdef _MSC_VER
#   pragma comment (lib, "imm32.lib")
#endif

/*
 * WM_UNICHAR is a message for Unicode input on all windows systems.
 * Perhaps this definition should be moved in another file.
 */
#ifndef WM_UNICHAR
#define WM_UNICHAR     0x0109
#define UNICODE_NOCHAR 0xFFFF
#endif

/*
 * Declarations of static variables used in this file.
 */

static const char winScreenName[] = ":0"; /* Default name of windows display. */
static HINSTANCE tkInstance = NULL;	/* Application instance handle. */
static int childClassInitialized;	/* Registered child class? */
static WNDCLASSW childClass;		/* Window class for child windows. */
static int tkWinTheme = 0;		/* See TkWinGetPlatformTheme */
static Tcl_Encoding keyInputEncoding = NULL;
					/* The current character encoding for
					 * keyboard input */
static int keyInputCharset = -1;	/* The Win32 CHARSET for the keyboard
					 * encoding */
static Tcl_Encoding unicodeEncoding = NULL;
					/* The UNICODE encoding */

/*
 * Thread local storage. Notice that now each thread must have its own
 * TkDisplay structure, since this structure contains most of the thread-
 * specific date for threads.
 */

typedef struct {
    TkDisplay *winDisplay;	/* TkDisplay structure that represents Windows
				 * screen. */
    int updatingClipboard;	/* If 1, we are updating the clipboard. */
    int surrogateBuffer;	/* Buffer for first of surrogate pair. */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * Forward declarations of functions used in this file.
 */

static void		GenerateXEvent(HWND hwnd, UINT message,
			    WPARAM wParam, LPARAM lParam);
static unsigned int	GetState(UINT message, WPARAM wParam, LPARAM lParam);
static void		GetTranslatedKey(TkKeyEvent *xkey, UINT type);
static void		UpdateInputLanguage(int charset);
static int		HandleIMEComposition(HWND hwnd, LPARAM lParam);

/*
 *----------------------------------------------------------------------
 *
 * TkGetServerInfo --
 *
 *	Given a window, this function returns information about the window
 *	server for that window. This function provides the guts of the "winfo
 *	server" command.
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
TkGetServerInfo(
    Tcl_Interp *interp,		/* The server information is returned in this
				 * interpreter's result. */
    TCL_UNUSED(Tk_Window))		/* Token for window; this selects a particular
				 * display and server. */
{
    static char buffer[32]; /* Empty string means not initialized yet. */
    OSVERSIONINFOW os;

    if (!buffer[0]) {
	GetVersionExW(&os);
	/* Write the first character last, preventing multi-thread issues. */
	snprintf(buffer+1, sizeof(buffer)-1, "indows %d.%d %d %s", (int)os.dwMajorVersion,
		(int)os.dwMinorVersion, (int)os.dwBuildNumber,
#ifdef _WIN64
		"Win64"
#else
		"Win32"
#endif
	);
	buffer[0] = 'W';
    }
    Tcl_AppendResult(interp, buffer, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetHINSTANCE --
 *
 *	Retrieves the global instance handle used by the Tk library.
 *
 * Results:
 *	Returns the global instance handle.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

HINSTANCE
Tk_GetHINSTANCE(void)
{
    if (tkInstance == NULL) {
	tkInstance = GetModuleHandleW(NULL);
    }
    return tkInstance;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinSetHINSTANCE --
 *
 *	Sets the global instance handle used by the Tk library. This should be
 *	called by DllMain.
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
TkWinSetHINSTANCE(
    HINSTANCE hInstance)
{
    tkInstance = hInstance;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinXInit --
 *
 *	Initialize Xlib emulation layer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets up various data structures.
 *
 *----------------------------------------------------------------------
 */

void
TkWinXInit(
    HINSTANCE hInstance)
{
    INITCOMMONCONTROLSEX comctl;
    CHARSETINFO lpCs;
    DWORD lpCP;

    if (childClassInitialized != 0) {
	return;
    }
    childClassInitialized = 1;

    comctl.dwSize = sizeof(INITCOMMONCONTROLSEX);
    comctl.dwICC = ICC_WIN95_CLASSES;
    if (!InitCommonControlsEx(&comctl)) {
	Tcl_Panic("Unable to load common controls?!");
    }

    childClass.style = CS_HREDRAW | CS_VREDRAW;
    childClass.cbClsExtra = 0;
    childClass.cbWndExtra = 0;
    childClass.hInstance = hInstance;
    childClass.hbrBackground = NULL;
    childClass.lpszMenuName = NULL;

    /*
     * Register the Child window class.
     */

    childClass.lpszClassName = TK_WIN_CHILD_CLASS_NAME;
    childClass.lpfnWndProc = TkWinChildProc;
    childClass.hIcon = NULL;
    childClass.hCursor = NULL;

    if (!RegisterClassW(&childClass)) {
	Tcl_Panic("Unable to register TkChild class");
    }

    /*
     * Initialize input language info
     */

    if (GetLocaleInfoW(LANGIDFROMLCID(PTR2INT(GetKeyboardLayout(0))),
	       LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
	       (LPWSTR) &lpCP, sizeof(lpCP)/sizeof(WCHAR))
	    && TranslateCharsetInfo((DWORD *)INT2PTR(lpCP), &lpCs, TCI_SRCCODEPAGE)) {
	UpdateInputLanguage((int) lpCs.ciCharset);
    }

    /*
     * Make sure we cleanup on finalize.
     */

    TkCreateExitHandler(TkWinXCleanup, hInstance);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinXCleanup --
 *
 *	Removes the registered classes for Tk.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes window classes from the system.
 *
 *----------------------------------------------------------------------
 */

void
TkWinXCleanup(
    void *clientData)
{
    HINSTANCE hInstance = (HINSTANCE)clientData;

    /*
     * Clean up our own class.
     */

    if (childClassInitialized) {
	childClassInitialized = 0;
	UnregisterClassW(TK_WIN_CHILD_CLASS_NAME, hInstance);
    }

    if (unicodeEncoding != NULL) {
	Tcl_FreeEncoding(unicodeEncoding);
	unicodeEncoding = NULL;
    }

    /*
     * And let the window manager clean up its own class(es).
     */

    TkWinWmCleanup(hInstance);
    TkWinCleanupContainerList();
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinGetPlatformTheme --
 *
 *	Return the Windows drawing style we should be using.
 *
 * Results:
 *	The return value is one of:
 *	    TK_THEME_WIN_CLASSIC	95/98/NT or XP in classic mode
 *	    TK_THEME_WIN_XP		XP not in classic mode
 *	    TK_THEME_WIN_VISTA	Vista or higher
 *
 *----------------------------------------------------------------------
 */

int
TkWinGetPlatformTheme(void)
{
    if (tkWinTheme == 0) {
	OSVERSIONINFOW os;

	os.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
	GetVersionExW(&os);

	if (os.dwPlatformId != VER_PLATFORM_WIN32_NT) {
	    Tcl_Panic("Windows NT is the only supported platform");
	}

	/*
	 * Set tkWinTheme to be TK_THEME_WIN_(CLASSIC|XP|VISTA). The
	 * TK_THEME_WIN_CLASSIC could be set even when running under XP if the
	 * windows classic theme was selected.
	 */
	if (os.dwMajorVersion == 5 && os.dwMinorVersion >= 1) {
	    HKEY hKey;
	    LPCWSTR szSubKey = L"Control Panel\\Appearance";
	    LPCWSTR szCurrent = L"Current";
	    DWORD dwSize = 200;
	    WCHAR pBuffer[200];

	    memset(pBuffer, 0, dwSize);
	    if (RegOpenKeyExW(HKEY_CURRENT_USER, szSubKey, 0L,
		    KEY_READ, &hKey) != ERROR_SUCCESS) {
		tkWinTheme = TK_THEME_WIN_XP;
	    } else {
		RegQueryValueExW(hKey, szCurrent, NULL, NULL, (LPBYTE) pBuffer, &dwSize);
		RegCloseKey(hKey);
		if (wcscmp(pBuffer, L"Windows Standard") == 0) {
		    tkWinTheme = TK_THEME_WIN_CLASSIC;
		} else {
		    tkWinTheme = TK_THEME_WIN_XP;
		}
	    }
	} else if (os.dwMajorVersion > 5) {
	    tkWinTheme = TK_THEME_WIN_VISTA;
	} else {
	    tkWinTheme = TK_THEME_WIN_CLASSIC;
	}
    }
    return tkWinTheme;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetDefaultScreenName --
 *
 *	Returns the name of the screen that Tk should use during
 *	initialization.
 *
 * Results:
 *	Returns a statically allocated string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
TkGetDefaultScreenName(
    TCL_UNUSED(Tcl_Interp *),
    const char *screenName)	/* If NULL, use default string. */
{
    if ((screenName == NULL) || (screenName[0] == '\0')) {
	screenName = winScreenName;
    }
    return screenName;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinDisplayChanged --
 *
 *	Called to set up initial screen info or when an event indicated
 *	display (screen) change.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May change info regarding the screen.
 *
 *----------------------------------------------------------------------
 */

void
TkWinDisplayChanged(
    Display *display)
{
    HDC dc;
    Screen *screen;

    if (display == NULL || (((_XPrivDisplay)(display))->screens) == NULL) {
	return;
    }
    screen = (((_XPrivDisplay)(display))->screens);

    dc = GetDC(NULL);
    WidthOfScreen(screen) = GetDeviceCaps(dc, HORZRES);
    HeightOfScreen(screen) = GetDeviceCaps(dc, VERTRES);
    WidthMMOfScreen(screen) = MulDiv(WidthOfScreen(screen), 254,
	    GetDeviceCaps(dc, LOGPIXELSX) * 10);
    HeightMMOfScreen(screen) = MulDiv(HeightOfScreen(screen), 254,
	    GetDeviceCaps(dc, LOGPIXELSY) * 10);

    /*
     * On windows, when creating a color bitmap, need two pieces of
     * information: the number of color planes and the number of pixels per
     * plane. Need to remember both quantities so that when constructing an
     * HBITMAP for offscreen rendering, we can specify the correct value for
     * the number of planes. Otherwise the HBITMAP won't be compatible with
     * the HWND and we'll just get blank spots copied onto the screen.
     */

    screen->ext_data = (XExtData *)INT2PTR(GetDeviceCaps(dc, PLANES));
    screen->root_depth = GetDeviceCaps(dc, BITSPIXEL) * PTR2INT(screen->ext_data);

    if (screen->root_visual != NULL) {
	ckfree(screen->root_visual);
    }
    screen->root_visual = (Visual *)ckalloc(sizeof(Visual));
    screen->root_visual->visualid = 0;
    if (GetDeviceCaps(dc, RASTERCAPS) & RC_PALETTE) {
	DefaultVisualOfScreen(screen)->map_entries = GetDeviceCaps(dc, SIZEPALETTE);
	DefaultVisualOfScreen(screen)->c_class = PseudoColor;
	DefaultVisualOfScreen(screen)->red_mask = 0x0;
	DefaultVisualOfScreen(screen)->green_mask = 0x0;
	DefaultVisualOfScreen(screen)->blue_mask = 0x0;
    } else if (DefaultDepthOfScreen(screen) == 4) {
	DefaultVisualOfScreen(screen)->c_class = StaticColor;
	DefaultVisualOfScreen(screen)->map_entries = 16;
    } else if (DefaultDepthOfScreen(screen) == 8) {
	DefaultVisualOfScreen(screen)->c_class = StaticColor;
	DefaultVisualOfScreen(screen)->map_entries = 256;
    } else if (DefaultDepthOfScreen(screen) == 12) {
	DefaultVisualOfScreen(screen)->c_class = TrueColor;
	DefaultVisualOfScreen(screen)->map_entries = 32;
	DefaultVisualOfScreen(screen)->red_mask = 0xf0;
	DefaultVisualOfScreen(screen)->green_mask = 0xf000;
	DefaultVisualOfScreen(screen)->blue_mask = 0xf00000;
    } else if (DefaultDepthOfScreen(screen) == 16) {
	DefaultVisualOfScreen(screen)->c_class = TrueColor;
	DefaultVisualOfScreen(screen)->map_entries = 64;
	DefaultVisualOfScreen(screen)->red_mask = 0xf8;
	DefaultVisualOfScreen(screen)->green_mask = 0xfc00;
	DefaultVisualOfScreen(screen)->blue_mask = 0xf80000;
    } else if (DefaultDepthOfScreen(screen) >= 24) {
	DefaultVisualOfScreen(screen)->c_class = TrueColor;
	DefaultVisualOfScreen(screen)->map_entries = 256;
	DefaultVisualOfScreen(screen)->red_mask = 0xff;
	DefaultVisualOfScreen(screen)->green_mask = 0xff00;
	DefaultVisualOfScreen(screen)->blue_mask = 0xff0000;
    }
    DefaultVisualOfScreen(screen)->bits_per_rgb = DefaultDepthOfScreen(screen);
    ReleaseDC(NULL, dc);

    if (DefaultColormapOfScreen(screen) != None) {
	XFreeColormap(display, DefaultColormapOfScreen(screen));
    }
    DefaultColormapOfScreen(screen) = XCreateColormap(display, None, DefaultVisualOfScreen(screen),
	    AllocNone);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpOpenDisplay/XkbOpenDisplay --
 *
 *	Create the Display structure and fill it with device specific
 *	information.
 *
 * Results:
 *	Returns a TkDisplay structure on success or NULL on failure.
 *
 * Side effects:
 *	Allocates a new TkDisplay structure.
 *
 *----------------------------------------------------------------------
 */

TkDisplay *
TkpOpenDisplay(
    const char *display_name)
{
    Display *display;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (tsdPtr->winDisplay != NULL) {
	if (!strcmp(DisplayString(tsdPtr->winDisplay->display), display_name)) {
	    return tsdPtr->winDisplay;
	} else {
	    return NULL;
	}
    }

    display = XkbOpenDisplay(display_name, NULL, NULL, NULL, NULL, NULL);
    TkWinDisplayChanged(display);

    tsdPtr->winDisplay =(TkDisplay *) ckalloc(sizeof(TkDisplay));
    memset(tsdPtr->winDisplay, 0, sizeof(TkDisplay));
    tsdPtr->winDisplay->display = display;
    tsdPtr->updatingClipboard = FALSE;

    /*
     * Key map info must be available immediately, because of "send event".
     */
    TkpInitKeymapInfo(tsdPtr->winDisplay);

    /*
     * Key map info must be available immediately, because of "send event".
     */
    TkpInitKeymapInfo(tsdPtr->winDisplay);

    return tsdPtr->winDisplay;
}

Display *
XkbOpenDisplay(
	const char *name,
	int *ev_rtrn,
	int *err_rtrn,
	int *major_rtrn,
	int *minor_rtrn,
	int *reason)
{
    _XPrivDisplay display = (_XPrivDisplay)ckalloc(sizeof(Display));
    Screen *screen = (Screen *)ckalloc(sizeof(Screen));
    TkWinDrawable *twdPtr = (TkWinDrawable *)ckalloc(sizeof(TkWinDrawable));

    memset(screen, 0, sizeof(Screen));
    memset(display, 0, sizeof(Display));

    /*
     * Note that these pixel values are not palette relative.
     */

    WhitePixelOfScreen(screen) = RGB(255, 255, 255);
    BlackPixelOfScreen(screen) = RGB(0, 0, 0);
    DefaultColormapOfScreen(screen) = None;

    display->screens		= screen;
    display->nscreens		= 1;
    display->default_screen	= 0;

    twdPtr->type = TWD_WINDOW;
    twdPtr->window.winPtr = NULL;
    twdPtr->window.handle = NULL;
    screen->root = (Window)twdPtr;
    screen->display = (Display *)display;

    display->display_name = (char  *)ckalloc(strlen(name) + 1);
    strcpy(display->display_name, name);

    display->nscreens = 1;
    display->request = 1;
    display->qlen = 0;

    if (ev_rtrn) *ev_rtrn = 0;
    if (err_rtrn) *err_rtrn = 0;
    if (major_rtrn) *major_rtrn = 0;
    if (minor_rtrn) *minor_rtrn = 0;
    if (reason) *reason = 0;

    return (Display *)display;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpCloseDisplay --
 *
 *	Closes and deallocates a Display structure created with the
 *	TkpOpenDisplay function.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees up memory.
 *
 *----------------------------------------------------------------------
 */

void
TkpCloseDisplay(
    TkDisplay *dispPtr)
{
    _XPrivDisplay display = (_XPrivDisplay)dispPtr->display;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (dispPtr != tsdPtr->winDisplay) {
	Tcl_Panic("TkpCloseDisplay: tried to call TkpCloseDisplay on another display");
	return; /* not reached */
    }

    tsdPtr->winDisplay = NULL;

    if (display->display_name != NULL) {
	ckfree(display->display_name);
    }
    if (ScreenOfDisplay(display, 0) != NULL) {
	if (DefaultVisualOfScreen(ScreenOfDisplay(display, 0)) != NULL) {
	    ckfree(DefaultVisualOfScreen(ScreenOfDisplay(display, 0)));
	}
	if (RootWindowOfScreen(ScreenOfDisplay(display, 0)) != None) {
	    ckfree((void *)RootWindowOfScreen(ScreenOfDisplay(display, 0)));
	}
	if (DefaultColormapOfScreen(ScreenOfDisplay(display, 0)) != None) {
	    XFreeColormap(display, DefaultColormapOfScreen(ScreenOfDisplay(display, 0)));
	}
	ckfree(ScreenOfDisplay(display, 0));
    }
    ckfree(display);
}

/*
 *----------------------------------------------------------------------
 *
 * TkClipCleanup --
 *
 *	This function is called to cleanup resources associated with claiming
 *	clipboard ownership and for receiving selection get results. This
 *	function is called in tkWindow.c. This has to be called by the display
 *	cleanup function because we still need the access display elements.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Resources are freed - the clipboard may no longer be used.
 *
 *----------------------------------------------------------------------
 */

void
TkClipCleanup(
    TkDisplay *dispPtr)		/* Display associated with clipboard. */
{
    if (dispPtr->clipWindow != NULL) {
	Tk_DeleteSelHandler(dispPtr->clipWindow, dispPtr->clipboardAtom,
		dispPtr->applicationAtom);
	Tk_DeleteSelHandler(dispPtr->clipWindow, dispPtr->clipboardAtom,
		dispPtr->windowAtom);

	Tk_DestroyWindow(dispPtr->clipWindow);
	Tcl_Release(dispPtr->clipWindow);
	dispPtr->clipWindow = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XBell --
 *
 *	Generate a beep.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Plays a sounds out the system speakers.
 *
 *----------------------------------------------------------------------
 */

int
XBell(
    TCL_UNUSED(Display *),
    TCL_UNUSED(int))
{
    MessageBeep(MB_OK);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinChildProc --
 *
 *	Callback from Windows whenever an event occurs on a child window.
 *
 * Results:
 *	Standard Windows return value.
 *
 * Side effects:
 *	May process events off the Tk event queue.
 *
 *----------------------------------------------------------------------
 */

LRESULT CALLBACK
TkWinChildProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    LRESULT result;

    switch (message) {
    case WM_INPUTLANGCHANGE:
	UpdateInputLanguage((int) wParam);
	result = 1;
	break;

    case WM_IME_COMPOSITION:
	result = 0;
	if (HandleIMEComposition(hwnd, lParam) == 0) {
	    result = DefWindowProcW(hwnd, message, wParam, lParam);
	}
	break;

    case WM_SETCURSOR:
	/*
	 * Short circuit the WM_SETCURSOR message since we set the cursor
	 * elsewhere.
	 */

	result = TRUE;
	break;

    case WM_CREATE:
    case WM_ERASEBKGND:
	result = 0;
	break;

    case WM_PAINT:
	GenerateXEvent(hwnd, message, wParam, lParam);
	result = DefWindowProcW(hwnd, message, wParam, lParam);
	break;

    case TK_CLAIMFOCUS:
    case TK_GEOMETRYREQ:
    case TK_ATTACHWINDOW:
    case TK_DETACHWINDOW:
    case TK_ICONIFY:
    case TK_DEICONIFY:
    case TK_MOVEWINDOW:
    case TK_WITHDRAW:
    case TK_RAISEWINDOW:
    case TK_GETFRAMEWID:
    case TK_OVERRIDEREDIRECT:
    case TK_SETMENU:
    case TK_STATE:
    case TK_INFO:
	result = TkWinEmbeddedEventProc(hwnd, message, wParam, lParam);
	break;

    case WM_UNICHAR:
	if (wParam == UNICODE_NOCHAR) {
	    /* If wParam is UNICODE_NOCHAR and the application processes
	     * this message, then return TRUE. */
	    result = 1;
	} else {
	    /* If the event was translated, we must return 0 */
	    if (TkTranslateWinEvent(hwnd, message, wParam, lParam, &result)) {
		result = 0;
	    } else {
		result = 1;
	    }
	}
	break;

    default:
	if (!TkTranslateWinEvent(hwnd, message, wParam, lParam, &result)) {
	    result = DefWindowProcW(hwnd, message, wParam, lParam);
	}
	break;
    }

    /*
     * Handle any newly queued events before returning control to Windows.
     */

    Tcl_ServiceAll();
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTranslateWinEvent --
 *
 *	This function is called by widget window functions to handle the
 *	translation from Win32 events to Tk events.
 *
 * Results:
 *	Returns 1 if the event was handled, else 0.
 *
 * Side effects:
 *	Depends on the event.
 *
 *----------------------------------------------------------------------
 */

int
TkTranslateWinEvent(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    LRESULT *resultPtr)
{
    *resultPtr = 0;
    switch (message) {
    case WM_RENDERFORMAT: {
	TkWindow *winPtr = (TkWindow *) Tk_HWNDToWindow(hwnd);

	if (winPtr) {
	    TkWinClipboardRender(winPtr->dispPtr, wParam);
	}
	return 1;
    }

    case WM_RENDERALLFORMATS: {
	TkWindow *winPtr = (TkWindow *) Tk_HWNDToWindow(hwnd);

	if (winPtr && OpenClipboard(hwnd)) {
	    /*
	     * Make sure that nobody had taken ownership of the clipboard
	     * before we opened it.
	     */

	    if (GetClipboardOwner() == hwnd) {
		TkWinClipboardRender(winPtr->dispPtr, CF_TEXT);
	    }
	    CloseClipboard();
	}
	return 1;
    }

    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_VSCROLL:
    case WM_HSCROLL: {
	/*
	 * Reflect these messages back to the sender so that they can be
	 * handled by the window proc for the control. Note that we need to be
	 * careful not to reflect a message that is targeted to this window,
	 * or we will loop.
	 */

	HWND target = (message == WM_NOTIFY)
		? ((NMHDR*)lParam)->hwndFrom : (HWND) lParam;

	if (target && target != hwnd) {
	    *resultPtr = SendMessageW(target, message, wParam, lParam);
	    return 1;
	}
	break;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
    case WM_XBUTTONUP:
    case WM_MOUSEMOVE:
	TkWinPointerEvent(hwnd, (short) LOWORD(lParam), (short) HIWORD(lParam));
	return 1;

    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
	if (wParam == VK_PACKET) {
	    /*
	     * This will trigger WM_CHAR event(s) with unicode data.
	     */
	    *resultPtr =
		PostMessageW(hwnd, message, HIWORD(lParam), LOWORD(lParam));
	    return 1;
	}
	/* else fall through */
    case WM_CLOSE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_DESTROYCLIPBOARD:
    case WM_UNICHAR:
    case WM_CHAR:
    case WM_SYSKEYUP:
    case WM_KEYUP:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
	GenerateXEvent(hwnd, message, wParam, lParam);
	return 1;
    case WM_MENUCHAR:
	GenerateXEvent(hwnd, message, wParam, lParam);

	/*
	 * MNC_CLOSE is the only one that looks right. This is a hack.
	 */

	*resultPtr = MAKELONG (0, MNC_CLOSE);
	return 1;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * GenerateXEvent --
 *
 *	This routine generates an X event from the corresponding Windows
 *	event.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues one or more X events.
 *
 *----------------------------------------------------------------------
 */

static void
GenerateXEvent(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    union {XEvent x; TkKeyEvent key;} event;
    TkWindow *winPtr;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if ((message == WM_MOUSEWHEEL) || (message == WM_MOUSEHWHEEL)) {
	union {LPARAM lParam; POINTS point;} root;
	POINT pos;
	root.lParam = lParam;

	/*
	 * Redirect mousewheel events to the window containing the cursor.
	 * That feels much less strange to users, and is how all the other
	 * platforms work.
	 */

	pos.x = root.point.x;
	pos.y = root.point.y;
	hwnd = WindowFromPoint(pos);
    }

    winPtr = (TkWindow *) Tk_HWNDToWindow(hwnd);
    if (!winPtr || winPtr->window == None) {
	return;
    }

    memset(&event.x, 0, sizeof(XEvent));
    event.x.xany.serial = LastKnownRequestProcessed(winPtr->display)++;
    event.x.xany.send_event = False;
    event.x.xany.display = winPtr->display;
    event.x.xany.window = winPtr->window;

    switch (message) {
    case WM_PAINT: {
	PAINTSTRUCT ps;

	event.x.type = Expose;
	BeginPaint(hwnd, &ps);
	event.x.xexpose.x = ps.rcPaint.left;
	event.x.xexpose.y = ps.rcPaint.top;
	event.x.xexpose.width = ps.rcPaint.right - ps.rcPaint.left;
	event.x.xexpose.height = ps.rcPaint.bottom - ps.rcPaint.top;
	EndPaint(hwnd, &ps);
	event.x.xexpose.count = 0;
	break;
    }

    case WM_CLOSE:
	event.x.type = ClientMessage;
	event.x.xclient.message_type =
		Tk_InternAtom((Tk_Window) winPtr, "WM_PROTOCOLS");
	event.x.xclient.format = 32;
	event.x.xclient.data.l[0] =
		Tk_InternAtom((Tk_Window) winPtr, "WM_DELETE_WINDOW");
	break;

    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
	TkWindow *otherWinPtr = (TkWindow *) Tk_HWNDToWindow((HWND) wParam);

	/*
	 * Compare toplevel windows to avoid reporting focus changes within
	 * the same toplevel.
	 */

	while (!(winPtr->flags & TK_TOP_LEVEL)) {
	    winPtr = winPtr->parentPtr;
	    if (winPtr == NULL) {
		return;
	    }
	}
	while (otherWinPtr && !(otherWinPtr->flags & TK_TOP_LEVEL)) {
	    otherWinPtr = otherWinPtr->parentPtr;
	}

	/*
	 * Do a catch-all Tk_SetCaretPos here to make sure that the window
	 * receiving focus sets the caret at least once.
	 */

	if (message == WM_SETFOCUS) {
	    Tk_SetCaretPos((Tk_Window) winPtr, 0, 0, 0);
	}

	if (otherWinPtr == winPtr) {
	    return;
	}

	event.x.xany.window = winPtr->window;
	event.x.type = (message == WM_SETFOCUS) ? FocusIn : FocusOut;
	event.x.xfocus.mode = NotifyNormal;
	event.x.xfocus.detail = NotifyNonlinear;

	/*
	 * Destroy the caret if we own it. If we are moving to another Tk
	 * window, it will reclaim and reposition it with Tk_SetCaretPos.
	 */

	if (message == WM_KILLFOCUS) {
	    DestroyCaret();
	}
	break;
    }

    case WM_DESTROYCLIPBOARD:
	if (tsdPtr->updatingClipboard == TRUE) {
	    /*
	     * We want to avoid this event if we are the ones that caused this
	     * event.
	     */

	    return;
	}
	event.x.type = SelectionClear;
	event.x.xselectionclear.selection =
		Tk_InternAtom((Tk_Window)winPtr, "CLIPBOARD");
	event.x.xselectionclear.time = TkpGetMS();
	break;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_CHAR:
    case WM_UNICHAR:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_KEYDOWN:
    case WM_KEYUP: {
	unsigned int state = GetState(message, wParam, lParam);
	Time time = TkpGetMS();
	POINT clientPoint;
	union {DWORD msgpos; POINTS point;} root;	/* Note: POINT and POINTS are different */

	/*
	 * Compute the screen and window coordinates of the event.
	 */

	root.msgpos = GetMessagePos();
	clientPoint.x = root.point.x;
	clientPoint.y = root.point.y;
	ScreenToClient(hwnd, &clientPoint);

	/*
	 * Set up the common event fields.
	 */

	event.x.xbutton.root = RootWindow(winPtr->display, winPtr->screenNum);
	event.x.xbutton.subwindow = None;
	event.x.xbutton.x = clientPoint.x;
	event.x.xbutton.y = clientPoint.y;
	event.x.xbutton.x_root = root.point.x;
	event.x.xbutton.y_root = root.point.y;
	event.x.xbutton.state = state;
	event.x.xbutton.time = time;
	event.x.xbutton.same_screen = True;

	/*
	 * Now set up event specific fields.
	 */

	switch (message) {
	case WM_MOUSEWHEEL: {

	    /*
	     * Send an Xevent using a KeyPress struct, but with the type field
	     * set to MouseWheelEvent for low resolution scrolls and to
	     * TouchpadScroll for high resolution scroll events. The Y delta
	     * is stored in the low order 16 bits of the keycode field.  Set
	     * nbytes to 0 to prevent conversion of the keycode to a keysym in
	     * TkpGetString. [Bug 1118340].
	     */

	    int delta = (short) HIWORD(wParam);
	    int mod = delta % WHEELDELTA;
	    if ( mod != 0 || lastMod != 0) {
		/* High resolution. */
		event.x.type = TouchpadScroll;
		event.x.xany.send_event = -1;
		event.key.nbytes = 0;
		event.x.xkey.state = state;
		event.x.xany.serial = scrollCounter++;
		event.x.xkey.keycode = (unsigned int) (delta & 0xffff);
	    } else {
		event.x.type = MouseWheelEvent;
		event.x.xany.send_event = -1;
		event.key.nbytes = 0;
		event.x.xkey.keycode = (unsigned int) delta;
	    }
	    lastMod = mod;
	    break;
	}
	case WM_MOUSEHWHEEL: {

	    /*
	     * Send an Xevent using a KeyPress struct, but with the type field
	     * set to MouseWheelEvent for low resolution scrolls and to
	     * TouchpadScroll for high resolution scroll events.  For low
	     * resolution scrolls the X delta is stored in the keycode field
	     * and For high resolution scrolls the X delta is in the high word
	     * of the keycode.  Set nbytes to 0 to prevent conversion of the
	     * keycode to a keysym in TkpGetString. [Bug 1118340].
	     */

	    int delta = (short) HIWORD(wParam);
	    int mod = delta % WHEELDELTA;
	    if ( mod != 0 || lastMod != 0) {
		/* High resolution. */
		event.x.type = TouchpadScroll;
		event.x.xany.send_event = -1;
		event.key.nbytes = 0;
		event.x.xkey.state = state;
		event.x.xany.serial = scrollCounter++;
		event.x.xkey.keycode = (unsigned int)(-(delta << 16));
	    } else {
		event.x.type = MouseWheelEvent;
		event.x.xany.send_event = -1;
		event.key.nbytes = 0;
		event.x.xkey.state |= ShiftMask;
		event.x.xkey.keycode = delta;
	    }
	    lastMod = mod;
	    break;
	}
	case WM_SYSKEYDOWN:
	case WM_KEYDOWN:
	    /*
	     * Check for translated characters in the event queue. Setting
	     * xany.send_event to -1 indicates to the Windows implementation
	     * of TkpGetString() that this event was generated by windows and
	     * that the Windows extension xkey.trans_chars is filled with the
	     * MBCS characters that came from the TranslateMessage call.
	     */

	    event.x.type = KeyPress;
	    event.x.xany.send_event = -1;
	    event.x.xkey.keycode = wParam;
	    GetTranslatedKey(&event.key, (message == WM_KEYDOWN) ? WM_CHAR :
		    WM_SYSCHAR);
	    break;

	case WM_SYSKEYUP:
	case WM_KEYUP:
	    /*
	     * We don't check for translated characters on keyup because Tk
	     * won't know what to do with them. Instead, we wait for the
	     * WM_CHAR messages which will follow.
	     */

	    event.x.type = KeyRelease;
	    event.x.xkey.keycode = wParam;
	    event.key.nbytes = 0;
	    break;

	case WM_CHAR:
	    /*
	     * Synthesize both a KeyPress and a KeyRelease. Strings generated
	     * by Input Method Editor are handled in the following manner:
	     * 1. A series of WM_KEYDOWN & WM_KEYUP messages that cause
	     *    GetTranslatedKey() to be called and return immediately
	     *    because the WM_KEYDOWNs have no associated WM_CHAR messages
	     *    -- the IME window is accumulating the characters and
	     *    translating them itself. In the "bind" command, you get an
	     *    event with a mystery keysym and %A == "" for each WM_KEYDOWN
	     *    that actually was meant for the IME.
	     * 2. A WM_KEYDOWN corresponding to the "confirm typing"
	     *    character. This causes GetTranslatedKey() to be called.
	     * 3. A WM_IME_NOTIFY message saying that the IME is done. A side
	     *	  effect of this message is that GetTranslatedKey() thinks
	     *	  this means that there are no WM_CHAR messages and returns
	     *	  immediately. In the "bind" command, you get an another event
	     *	  with a mystery keysym and %A == "".
	     * 4. A sequence of WM_CHAR messages that correspond to the
	     *	  characters in the IME window. A bunch of simulated
	     *	  KeyPress/KeyRelease events will be generated, one for each
	     *	  character. Adjacent WM_CHAR messages may actually specify
	     *	  the high and low bytes of a multi-byte character -- in that
	     *	  case the two WM_CHAR messages will be combined into one
	     *	  event. It is the event-consumer's responsibility to convert
	     *	  the string returned from XLookupString from system encoding
	     *	  to UTF-8.
	     * 5. And finally we get the WM_KEYUP for the "confirm typing"
	     *    character.
	     */

	    event.x.type = KeyPress;
	    event.x.xany.send_event = -1;
	    event.x.xkey.keycode = 0;
	    if ((int)wParam & 0xff00) {
		int ch1 = wParam & 0xffff;

		if ((ch1 & 0xfc00) == 0xd800) {
		    tsdPtr->surrogateBuffer = ch1;
		    return;
		}
		if ((ch1 & 0xfc00) == 0xdc00) {
		    ch1 = ((tsdPtr->surrogateBuffer & 0x3ff) << 10) |
			    (ch1 & 0x3ff) | 0x10000;
		    tsdPtr->surrogateBuffer = 0;
		}
		event.x.xany.send_event = -3;
		event.key.nbytes = 0;
		event.x.xkey.keycode = ch1;
	    } else {
		event.key.nbytes = 1;
		event.key.trans_chars[0] = (char) wParam;

		if (IsDBCSLeadByte((BYTE) wParam)) {
		    MSG msg;

		    if ((PeekMessageW(&msg, NULL, WM_CHAR, WM_CHAR,
			    PM_NOREMOVE) != 0)
			    && (msg.message == WM_CHAR)) {
			GetMessageW(&msg, NULL, WM_CHAR, WM_CHAR);
			event.key.nbytes = 2;
			event.key.trans_chars[1] = (char) msg.wParam;
		   }
		}
	    }
	    Tk_QueueWindowEvent(&event.x, TCL_QUEUE_TAIL);
	    event.x.type = KeyRelease;
	    break;

	case WM_UNICHAR: {
	    event.x.type = KeyPress;
	    event.x.xany.send_event = -3;
	    event.x.xkey.keycode = wParam;
	    event.key.nbytes = 0;
	    Tk_QueueWindowEvent(&event.x, TCL_QUEUE_TAIL);
	    event.x.type = KeyRelease;
	    break;
	}

	}
	break;
    }

    default:
	/*
	 * Don't know how to translate this event, so ignore it. (It probably
	 * should not have got here, but ignoring it should be harmless.)
	 */

	return;
    }

    /*
     * Post the translated event to the main Tk event queue.
     */

    Tk_QueueWindowEvent(&event.x, TCL_QUEUE_TAIL);
}

/*
 *----------------------------------------------------------------------
 *
 * GetState --
 *
 *	This function constructs a state mask for the mouse buttons and
 *	modifier keys as they were before the event occurred.
 *
 * Results:
 *	Returns a composite value of all the modifier and button state flags
 *	that were set at the time the event occurred.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static unsigned int
GetState(
    UINT message,		/* Win32 message type */
    WPARAM wParam,		/* wParam of message, used if key message */
    LPARAM lParam)		/* lParam of message, used if key message */
{
    int mask;
    int prevState;		/* 1 if key was previously down */
    unsigned int state = TkWinGetModifierState();

    /*
     * If the event is a key press or release, we check for modifier keys so
     * we can report the state of the world before the event.
     */

    if (message == WM_SYSKEYDOWN || message == WM_KEYDOWN
	    || message == WM_SYSKEYUP || message == WM_KEYUP) {
	mask = 0;
	prevState = HIWORD(lParam) & KF_REPEAT;
	switch(wParam) {
	case VK_SHIFT:
	    mask = ShiftMask;
	    break;
	case VK_CONTROL:
	    mask = ControlMask;
	    break;
	case VK_MENU:
	    mask = ALT_MASK;
	    break;
	case VK_CAPITAL:
	    if (message == WM_SYSKEYDOWN || message == WM_KEYDOWN) {
		mask = LockMask;
		prevState = ((state & mask) ^ prevState) ? 0 : 1;
	    }
	    break;
	case VK_NUMLOCK:
	    if (message == WM_SYSKEYDOWN || message == WM_KEYDOWN) {
		mask = Mod1Mask;
		prevState = ((state & mask) ^ prevState) ? 0 : 1;
	    }
	    break;
	case VK_SCROLL:
	    if (message == WM_SYSKEYDOWN || message == WM_KEYDOWN) {
		mask = Mod3Mask;
		prevState = ((state & mask) ^ prevState) ? 0 : 1;
	    }
	    break;
	}
	if (prevState) {
	    state |= mask;
	} else {
	    state &= ~mask;
	}
	if (HIWORD(lParam) & KF_EXTENDED) {
	    state |= EXTENDED_MASK;
	}
    }
    return state;
}

/*
 *----------------------------------------------------------------------
 *
 * GetTranslatedKey --
 *
 *	Retrieves WM_CHAR messages that are placed on the system queue by the
 *	TranslateMessage system call and places them in the given KeyPress
 *	event.
 *
 * Results:
 *	Sets the trans_chars and nbytes member of the key event.
 *
 * Side effects:
 *	Removes any WM_CHAR messages waiting on the top of the system event
 *	queue.
 *
 *----------------------------------------------------------------------
 */

static void
GetTranslatedKey(
    TkKeyEvent *xkey,
    UINT type)
{
    MSG msg;

    xkey->nbytes = 0;

    while ((xkey->nbytes < sizeof(xkey->trans_chars))
	    && (PeekMessageA(&msg, NULL, type, type, PM_NOREMOVE) != 0)) {
	if (msg.message != type) {
	    break;
	}

	GetMessageA(&msg, NULL, type, type);

	/*
	 * If this is a normal character message, we may need to strip off the
	 * Alt modifier (e.g. Alt-digits). Note that we don't want to do this
	 * for system messages, because those were presumably generated as an
	 * Alt-char sequence (e.g. accelerator keys).
	 */

	if ((msg.message == WM_CHAR) && (msg.lParam & 0x20000000)) {
	    xkey->keyEvent.state = 0;
	}
	xkey->trans_chars[xkey->nbytes++] = (char) msg.wParam;

	if (((unsigned short) msg.wParam) > ((unsigned short) 0xff)) {
	    /*
	     * Some "addon" input devices, such as the popular PenPower
	     * Chinese writing pad, generate 16 bit values in WM_CHAR messages
	     * (instead of passing them in two separate WM_CHAR messages
	     * containing two 8-bit values.
	     */

	    xkey->trans_chars[xkey->nbytes] = (char) (msg.wParam >> 8);
	    xkey->nbytes ++;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateInputLanguage --
 *
 *	Gets called when a WM_INPUTLANGCHANGE message is received by the Tk
 *	child window function. This message is sent by the Input Method Editor
 *	system when the user chooses a different input method. All subsequent
 *	WM_CHAR messages will contain characters in the new encoding. We
 *	record the new encoding so that TkpGetString() knows how to correctly
 *	translate the WM_CHAR into unicode.
 *
 * Results:
 *	Records the new encoding in keyInputEncoding.
 *
 * Side effects:
 *	Old value of keyInputEncoding is freed.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateInputLanguage(
    int charset)
{
    CHARSETINFO charsetInfo;
    Tcl_Encoding encoding;
    char codepage[4 + TCL_INTEGER_SPACE];

    if (keyInputCharset == charset) {
	return;
    }
    if (TranslateCharsetInfo((DWORD*)INT2PTR(charset), &charsetInfo,
	    TCI_SRCCHARSET) == 0) {
	/*
	 * Some mysterious failure.
	 */

	return;
    }

    if (charsetInfo.ciACP == CP_UTF8) {
	strcpy(codepage, "utf-8");
    } else {
	snprintf(codepage, sizeof(codepage), "cp%d", charsetInfo.ciACP);
    }

    if ((encoding = Tcl_GetEncoding(NULL, codepage)) == NULL) {
	/*
	 * The encoding is not supported by Tcl.
	 */

	return;
    }

    if (keyInputEncoding != NULL) {
	Tcl_FreeEncoding(keyInputEncoding);
    }

    keyInputEncoding = encoding;
    keyInputCharset = charset;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinGetKeyInputEncoding --
 *
 *	Returns the current keyboard input encoding selected by the user (with
 *	WM_INPUTLANGCHANGE events).
 *
 * Results:
 *	The current keyboard input encoding.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tcl_Encoding
TkWinGetKeyInputEncoding(void)
{
    return keyInputEncoding;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinGetUnicodeEncoding --
 *
 *	Returns the cached unicode encoding.
 *
 * Results:
 *	The unicode encoding.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tcl_Encoding
TkWinGetUnicodeEncoding(void)
{
    if (unicodeEncoding == NULL) {
	unicodeEncoding = Tcl_GetEncoding(NULL, "utf-16");
	if (unicodeEncoding == NULL) {
	    unicodeEncoding = Tcl_GetEncoding(NULL, "unicode");
	}
    }
    return unicodeEncoding;
}

/*
 *----------------------------------------------------------------------
 *
 * HandleIMEComposition --
 *
 *	This function works around a deficiency in some versions of Windows
 *	2000 to make it possible to entry multi-lingual characters under all
 *	versions of Windows 2000.
 *
 *	When an Input Method Editor (IME) is ready to send input characters to
 *	an application, it sends a WM_IME_COMPOSITION message with the
 *	GCS_RESULTSTR. However, The DefWindowProcW() on English Windows 2000
 *	arbitrarily converts all non-Latin-1 characters in the composition to
 *	"?".
 *
 *	This function correctly processes the composition data and sends the
 *	UNICODE values of the composed characters to TK's event queue.
 *
 * Results:
 *	If this function has processed the composition data, returns 1.
 *	Otherwise returns 0.
 *
 * Side effects:
 *	Key events are put into the TK event queue.
 *
 *----------------------------------------------------------------------
 */

static int
HandleIMEComposition(
    HWND hwnd,			/* Window receiving the message. */
    LPARAM lParam)		/* Flags for the WM_IME_COMPOSITION message */
{
    HIMC hIMC;
    int n;
    int high = 0;

    if ((lParam & GCS_RESULTSTR) == 0) {
	/*
	 * Composition is not finished yet.
	 */

	return 0;
    }

    hIMC = ImmGetContext(hwnd);
    if (!hIMC) {
	return 0;
    }

    n = ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, NULL, 0);

    if (n > 0) {
	WCHAR *buff = (WCHAR *) ckalloc(n);
	TkWindow *winPtr;
	XEvent event;
	int i;

	n = ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, buff, (unsigned) n) / 2;

	/*
	 * Set up the fields pertinent to key event.
	 *
	 * We set send_event to the special value of -3, so that TkpGetString
	 * in tkWinKey.c knows that keycode already contains a UNICODE
	 * char and there's no need to do encoding conversion.
	 *
	 * Note that the event *must* be zeroed out first; Tk plays cunning
	 * games with the overalls structure. [Bug 2992129]
	 */

	winPtr = (TkWindow *) Tk_HWNDToWindow(hwnd);

	memset(&event, 0, sizeof(XEvent));
	event.xkey.serial = LastKnownRequestProcessed(winPtr->display)++;
	event.xkey.send_event = -3;
	event.xkey.display = winPtr->display;
	event.xkey.window = winPtr->window;
	event.xkey.root = RootWindow(winPtr->display, winPtr->screenNum);
	event.xkey.subwindow = None;
	event.xkey.state = TkWinGetModifierState();
	event.xkey.time = TkpGetMS();
	event.xkey.same_screen = True;

	for (i=0; i<n; ) {
	    /*
	     * Simulate a pair of KeyPress and KeyRelease events for each
	     * UNICODE character in the composition.
	     */

	    event.xkey.keycode = buff[i++];

	    if ((event.xkey.keycode & 0xfc00) == 0xd800) {
		high = ((event.xkey.keycode & 0x3ff) << 10) + 0x10000;
		break;
	    } else if (high && (event.xkey.keycode & 0xfc00) == 0xdc00) {
		event.xkey.keycode &= 0x3ff;
		event.xkey.keycode += high;
		high = 0;
	    }
	    event.type = KeyPress;
	    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

	    event.type = KeyRelease;
	    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
	}

	ckfree(buff);
    }
    ImmReleaseContext(hwnd, hIMC);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinResendEvent --
 *
 *	This function converts an X event into a Windows event and invokes the
 *	specified window function.
 *
 * Results:
 *	A standard Windows result.
 *
 * Side effects:
 *	Invokes the window function
 *
 *----------------------------------------------------------------------
 */

LRESULT
TkWinResendEvent(
    WNDPROC wndproc,
    HWND hwnd,
    XEvent *eventPtr)
{
    UINT msg;
    WPARAM wparam;
    LPARAM lparam;

    if (eventPtr->type != ButtonPress) {
	return 0;
    }

    switch (eventPtr->xbutton.button) {
    case Button1:
	msg = WM_LBUTTONDOWN;
	wparam = MK_LBUTTON;
	break;
    case Button2:
	msg = WM_MBUTTONDOWN;
	wparam = MK_MBUTTON;
	break;
    case Button3:
	msg = WM_RBUTTONDOWN;
	wparam = MK_RBUTTON;
	break;
    case Button8:
	msg = WM_XBUTTONDOWN;
	wparam = MAKEWPARAM(MK_XBUTTON1, XBUTTON1);
	break;
    case Button9:
	msg = WM_XBUTTONDOWN;
	wparam = MAKEWPARAM(MK_XBUTTON2, XBUTTON2);
	break;
    default:
	return 0;
    }

    if (eventPtr->xbutton.state & Button1Mask) {
	wparam |= MK_LBUTTON;
    }
    if (eventPtr->xbutton.state & Button2Mask) {
	wparam |= MK_MBUTTON;
    }
    if (eventPtr->xbutton.state & Button3Mask) {
	wparam |= MK_RBUTTON;
    }
    if (eventPtr->xbutton.state & Button4Mask) {
	wparam |= MK_XBUTTON1;
    }
    if (eventPtr->xbutton.state & Button5Mask) {
	wparam |= MK_XBUTTON2;
    }
    if (eventPtr->xbutton.state & ShiftMask) {
	wparam |= MK_SHIFT;
    }
    if (eventPtr->xbutton.state & ControlMask) {
	wparam |= MK_CONTROL;
    }
    lparam = MAKELPARAM((short) eventPtr->xbutton.x,
	    (short) eventPtr->xbutton.y);
    return CallWindowProcW(wndproc, hwnd, msg, wparam, lparam);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetMS --
 *
 *	Return a relative time in milliseconds. It doesn't matter when the
 *	epoch was.
 *
 * Results:
 *	Number of milliseconds.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned long
TkpGetMS(void)
{
    return GetTickCount();
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinUpdatingClipboard --
 *
 *
 * Results:
 *	Number of milliseconds.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkWinUpdatingClipboard(
    int mode)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    tsdPtr->updatingClipboard = mode;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetCaretPos --
 *
 *	This enables correct movement of focus in the MS Magnifier, as well as
 *	allowing us to correctly position the IME Window. The following Win32
 *	APIs are used to work with MS caret:
 *
 *	CreateCaret	DestroyCaret	SetCaretPos	GetCaretPos
 *
 *	Only one instance of caret can be active at any time (e.g.
 *	DestroyCaret API does not take any argument such as handle). Since
 *	do-it-right approach requires to track the create/destroy caret status
 *	all the time in a global scope among windows (or widgets), we just
 *	implement this minimal setup to get the job done.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Sets the global Windows caret position.
 *
 *----------------------------------------------------------------------
 */

void
Tk_SetCaretPos(
    Tk_Window tkwin,
    int x, int y,
    int height)
{
    static HWND caretHWND = NULL;
    TkCaret *caretPtr = &(((TkWindow *) tkwin)->dispPtr->caret);
    Window win;

    /*
     * Prevent processing anything if the values haven't changed. Windows only
     * has one display, so we can do this with statics.
     */

    if ((caretPtr->winPtr == ((TkWindow *) tkwin))
	    && (caretPtr->x == x) && (caretPtr->y == y)) {
	return;
    }

    caretPtr->winPtr = ((TkWindow *) tkwin);
    caretPtr->x = x;
    caretPtr->y = y;
    caretPtr->height = height;

    /*
     * We adjust to the toplevel to get the coords right, as setting the IME
     * composition window is based on the toplevel hwnd, so ignore height.
     */

    while (!Tk_IsTopLevel(tkwin)) {
	x += Tk_X(tkwin);
	y += Tk_Y(tkwin);
	tkwin = Tk_Parent(tkwin);
	if (tkwin == NULL) {
	    return;
	}
    }

    win = Tk_WindowId(tkwin);
    if (win) {
	HIMC hIMC;
	HWND hwnd = Tk_GetHWND(win);

	if (hwnd != caretHWND) {
	    DestroyCaret();
	    if (CreateCaret(hwnd, NULL, 0, 0)) {
		caretHWND = hwnd;
	    }
	}

	if (!SetCaretPos(x, y) && CreateCaret(hwnd, NULL, 0, 0)) {
	    caretHWND = hwnd;
	    SetCaretPos(x, y);
	}

	/*
	 * The IME composition window should be updated whenever the caret
	 * position is changed because a clause of the composition string may
	 * be converted to the final characters and the other clauses still
	 * stay on the composition window. -- yamamoto
	 */

	hIMC = ImmGetContext(hwnd);
	if (hIMC) {
	    COMPOSITIONFORM cform;

	    cform.dwStyle = CFS_POINT;
	    cform.ptCurrentPos.x = x;
	    cform.ptCurrentPos.y = y;
	    ImmSetCompositionWindow(hIMC, &cform);
	    ImmReleaseContext(hwnd, hIMC);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetUserInactiveTime --
 *
 *	Return the number of milliseconds the user was inactive.
 *
 * Results:
 *	Milliseconds of user inactive time or -1 if GetLastInputInfo
 *	returns an error.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

long
Tk_GetUserInactiveTime(
     TCL_UNUSED(Display *))
{
    LASTINPUTINFO li;

    li.cbSize = sizeof(li);
    if (!GetLastInputInfo(&li)) {
	return -1;
    }

    /*
     * Last input info is in milliseconds, since restart time.
     */

    return (GetTickCount()-li.dwTime);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_ResetUserInactiveTime --
 *
 *	Reset the user inactivity timer
 *
 * Results:
 *	none
 *
 * Side effects:
 *	The user inactivity timer of the underlying windowing system is reset
 *	to zero.
 *
 *----------------------------------------------------------------------
 */

void
Tk_ResetUserInactiveTime(
    TCL_UNUSED(Display *))
{
    INPUT inp;

    inp.type = INPUT_MOUSE;
    inp.mi.dx = 0;
    inp.mi.dy = 0;
    inp.mi.mouseData = 0;
    inp.mi.dwFlags = MOUSEEVENTF_MOVE;
    inp.mi.time = 0;
    inp.mi.dwExtraInfo = (DWORD) 0;

    SendInput(1, &inp, sizeof(inp));
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
