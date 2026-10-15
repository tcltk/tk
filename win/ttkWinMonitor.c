#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#endif

#include "tkWinInt.h"
#include "ttk/ttkTheme.h"
#include <windows.h>
#include <uxtheme.h>

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

typedef struct {
    HWND hwnd;
    Tcl_Interp *interp;
    Tcl_TimerToken timer;
} MonitorInfo;

/*
 * RegisterSystemColors --
 *	Register all known Windows system colors (as per GetSysColor) as Tk
 *	named colors.
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
	{ "SystemMenubart",		COLOR_MENUBAR },
	{ "SystemMenuText",		COLOR_MENUTEXT },
	{ "SystemScrollbar",		COLOR_SCROLLBAR },
	{ "SystemWindow",		COLOR_WINDOW },
	{ "SystemWindowFrame",		COLOR_WINDOWFRAME },
	{ "SystemWindowText",		COLOR_WINDOWTEXT },
	{ NULL, 0 }
};

static void RegisterSystemColors(Tcl_Interp *interp, HWND hwnd) {
    Ttk_ResourceCache cache = Ttk_GetResourceCache(interp);
    const SystemColorEntry *sysColor;
    HTHEME hTheme = OpenThemeData(hwnd, L"WINDOW");

    for (sysColor = sysColors; sysColor->name; ++sysColor) {
	DWORD pixel = GetThemeSysColor(hTheme, sysColor->index);
	XColor colorSpec;

	colorSpec.red = GetRValue(pixel) * 257;
	colorSpec.green = GetGValue(pixel) * 257;
	colorSpec.blue = GetBValue(pixel) * 257;
	Ttk_RegisterNamedColor(cache, sysColor->name, &colorSpec);
    }
    CloseThemeData(hTheme);
}

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

void
themeUpdateHandler(void *clientData) {
    MonitorInfo *info = (MonitorInfo *)clientData;
    Ttk_Theme theme;
    info->timer = NULL;

    /* Update colors used by theme */
    RegisterSystemColors(info->interp, info->hwnd);

    /* Reload the application theme due to color changes. */
    theme = Ttk_GetCurrentTheme(info->interp);
    if (theme) {
	Ttk_UseTheme(info->interp, theme);

	/* @@@ What to do about errors here? */
    }
}

static LRESULT WINAPI
WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MonitorInfo *info = (MonitorInfo *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_DESTROY:
	break;

    case WM_SETTINGCHANGE:
	/* SystemParametersInfo changed a system-wide setting or
	   when policy settings have changed. */
	if (wp != SPI_GETNONCLIENTMETRICS) {
	    break;
	}
    case WM_SYSCOLORCHANGE:
	/* Change has been made to a system color setting. */
    case WM_STYLECHANGED:
	/* Window style has changed. */
    case WM_THEMECHANGED:
	/* Window theme has changed. */
    case WM_DWMCOLORIZATIONCOLORCHANGED:
	/* DWM Colorization color has changed. */

	/* Throttle updates */
	if (info->timer == NULL) {
	    info->timer = Tcl_CreateTimerHandler(1000, themeUpdateHandler, (void *) info);
	}
	return 0;
	break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/*
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

    RegisterSystemColors(interp, hwnd);
    TtkWinTheme_Init(interp, hwnd);
    TtkWinVistaTheme_Init(interp, hwnd);

    return TCL_OK;
}
