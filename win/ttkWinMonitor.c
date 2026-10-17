#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#endif

#include "tkWinInt.h"
#include "ttk/ttkTheme.h"
#include <windows.h>
#include <uxtheme.h>

/* Prototype definitions */
static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

typedef struct {
    HWND hwnd;
    Tcl_Interp *interp;
    Tcl_TimerToken timer;
} MonitorInfo;

/*
 *	Define Window system colors for use by TTk themes.
 */

typedef struct {
    const char *name;
    int index;
} SystemColorEntry;

static const SystemColorEntry sysColors[] = {
	{ "System3dDarkShadow",		COLOR_3DDKSHADOW },
	{ "System3dFace",		COLOR_3DFACE },
	{ "System3dHighlight",		COLOR_3DHIGHLIGHT },
	{ "System3dLight",		COLOR_3DLIGHT },
	{ "System3dShadow",		COLOR_3DSHADOW },
	{ "SystemActiveBorder",		COLOR_ACTIVEBORDER },
	{ "SystemActiveCaption",	COLOR_ACTIVECAPTION },
	{ "SystemAppWorkspace",		COLOR_APPWORKSPACE },
	{ "SystemBackground",		COLOR_BACKGROUND },
	{ "SystemButtonFace",		COLOR_BTNFACE },
	{ "SystemButtonHighlight",	COLOR_BTNHIGHLIGHT },
	{ "SystemButtonShadow",		COLOR_BTNSHADOW },
	{ "SystemButtonText",		COLOR_BTNTEXT },
	{ "SystemCaptionText",		COLOR_CAPTIONTEXT },
	{ "SystemDesktop",		COLOR_DESKTOP },
	{ "SystemDisabledText",		COLOR_GRAYTEXT },
	{ "SystemGrayText",		COLOR_GRAYTEXT },
	{ "SystemHighlight",		COLOR_HIGHLIGHT },
	{ "SystemHighlightText",	COLOR_HIGHLIGHTTEXT },
	{ "SystemHotLight",		COLOR_HOTLIGHT },
	{ "SystemInactiveBorder",	COLOR_INACTIVEBORDER },
	{ "SystemInactiveCaption",	COLOR_INACTIVECAPTION },
	{ "SystemInactiveCaptionText",	COLOR_INACTIVECAPTIONTEXT },
	{ "SystemInfoBackground",	COLOR_INFOBK },
	{ "SystemInfoText",		COLOR_INFOTEXT },
	{ "SystemMenu",			COLOR_MENU },
	{ "SystemMenuHighlight",	COLOR_MENUHILIGHT },
	{ "SystemMenubar",		COLOR_MENUBAR },
	{ "SystemMenuText",		COLOR_MENUTEXT },
	{ "SystemPlaceHolderText",	COLOR_GRAYTEXT},
	{ "SystemScrollbar",		COLOR_SCROLLBAR },
	{ "SystemWindow",		COLOR_WINDOW },
	{ "SystemWindowFrame",		COLOR_WINDOWFRAME },
	{ "SystemWindowText",		COLOR_WINDOWTEXT },
	{ NULL, 0 }
};

/*
 * RegisterSystemColors --
 *
 *	Register all known Windows system colors as Tk named colors.
 */

static void RegisterSystemColors(Tcl_Interp *interp, HWND hwnd) {
    Ttk_ResourceCache cache = Ttk_GetResourceCache(interp);
    const SystemColorEntry *sysColor;
    HTHEME hTheme = OpenThemeData(hwnd, L"WINDOW");

    for (sysColor = sysColors; sysColor->name; ++sysColor) {
	DWORD pixel = GetThemeSysColor(hTheme, sysColor->index);
	XColor colorSpec;

	colorSpec.pixel = pixel;
	colorSpec.red = GetRValue(pixel) * 257;
	colorSpec.green = GetGValue(pixel) * 257;
	colorSpec.blue = GetBValue(pixel) * 257;
	Ttk_RegisterNamedColor(cache, sysColor->name, &colorSpec);
    }
    CloseThemeData(hTheme);
}

/*
 * CreateThemeMonitorWindow and DestroyThemeMonitorWindow--
 *
 *	Create or destroy window to serve as handler for OS window events.
 */
static MonitorInfo*
CreateThemeMonitorWindow(HINSTANCE hinst, Tcl_Interp *interp) {
    WNDCLASSEXW wc;
    HWND       hwnd = NULL;
    MonitorInfo *info;
    WCHAR      title[32] = L"TtkMonitorWindow";
    WCHAR      name[32] = L"TtkMonitorClass";

    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = (WNDPROC)WndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hinst;
    wc.hIcon         = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.hIconSm       = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wc.lpszMenuName  = name;
    wc.lpszClassName = name;
    
    info = (MonitorInfo *)Tcl_Alloc(sizeof(MonitorInfo));
    if (info) {
	info->hwnd = hwnd;
	info->interp = interp;
	info->timer = NULL;
    } else {
	return NULL;
    }

    if (RegisterClassExW(&wc)) {
	hwnd = CreateWindowW( name, title, WS_OVERLAPPEDWINDOW,
	    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
	    NULL, NULL, hinst, NULL );
	SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR) info);
	ShowWindow(hwnd, SW_HIDE);
	UpdateWindow(hwnd);
    }
    return info;
}

static void
DestroyThemeMonitorWindow(void *clientData) {
    MonitorInfo *info = (MonitorInfo *)clientData;

    if (info->timer) {
	Tcl_DeleteTimerHandler(info->timer);
    }
    DestroyWindow(info->hwnd);
    Tcl_Free(clientData);
}

/*
 * TkWinUpdateThemeColors --
 *
 *	Update system colors after they are changed by Windows. This is the
 *	themed version of the TkWinUpdateColors for WM_SYSCOLORCHANGE events.
 */

void
TkWinUpdateThemeColors(HWND hwnd, TkWindow *winPtr) {
    HTHEME hTheme = GetWindowTheme(hwnd);
    TkDisplay *dispPtr;
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch search;

    /* Returns NULL for monitor window */
    if (!winPtr) return;
    dispPtr = winPtr->dispPtr;

    entryPtr = Tcl_FirstHashEntry(&dispPtr->colorNameTable, &search);
    while (entryPtr) {
	TkColor *tkColPtr = (TkColor *)Tcl_GetHashValue(entryPtr);

	while (tkColPtr) {
	    XColor *colorPtr = &tkColPtr->color;
	    int index = TkWinIndexOfColor(colorPtr);

	    /* Update system color */
	    if (index > -1) {
		/* If hTheme is NULL same as GetSysColor. */
		colorPtr->pixel = GetThemeSysColor(hTheme, index);
		colorPtr->red = GetRValue(colorPtr->pixel) * 257;
		colorPtr->green = GetGValue(colorPtr->pixel) * 257;
		colorPtr->blue = GetBValue(colorPtr->pixel) * 257;
		colorPtr->flags = DoRed|DoGreen|DoBlue;
	    }

	    /* Get next screen/display for color */
	    tkColPtr = tkColPtr->nextPtr;
	}

	/* Get next color */
	entryPtr = Tcl_NextHashEntry(&search);
    }
}

/*
 * WndProc --
 *
 * Handler for Windows events
 */
static LRESULT WINAPI
WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MonitorInfo *info = (MonitorInfo *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    LRESULT res = 0; /* success */

    switch (msg) {
    case WM_DESTROY:
	break;

    case WM_SYSCOLORCHANGE:
	/* Change has been made to a system color setting. No params. */

	RegisterSystemColors(minfo->interp, hwnd);
	/* The system sends a WM_PAINT message to all affected windows. */
	break;

    case WM_THEMECHANGED: {
	/* Windows theme has changed from SetWindowTheme call. No params. */
	/* Will receive a WM_SETTINGCHANGE if changed via a settings call. */
	Ttk_Theme theme = Ttk_GetCurrentTheme(minfo->interp);

	if (theme) {
	    if (Ttk_UseTheme(minfo->interp, theme) != TCL_OK) {
		/* Should not happen since using same theme */
		res = 1;
	    }
	}
	break;
    }

    default:
	res = DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return res;
}

/*
 * Ttk_WinPlatformInit --
 *
 * Windows-specific platform initialization:
 */

MODULE_SCOPE int TtkWinTheme_Init(Tcl_Interp *, HWND hwnd);
MODULE_SCOPE int TtkWinVistaTheme_Init(Tcl_Interp *, HWND hwnd);
MODULE_SCOPE int Ttk_WinPlatformInit(Tcl_Interp *interp);

MODULE_SCOPE int Ttk_WinPlatformInit(Tcl_Interp *interp) {
    HWND hwnd;
    MonitorInfo *info;

    info = CreateThemeMonitorWindow(Tk_GetHINSTANCE(), interp);
    if (!info) return TCL_ERROR;
    hwnd = info->hwnd;
    Ttk_RegisterCleanup(interp, info, DestroyThemeMonitorWindow);

    /* Cache Windows theme system colors */
    RegisterSystemColors(interp, hwnd);

    /* Init Ttk themes */
    TtkWinTheme_Init(interp, hwnd);
    TtkWinVistaTheme_Init(interp, hwnd);

    return TCL_OK;
}
