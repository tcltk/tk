/*
 * tkWinSysTray.c --
 *
 *	tkWinSysTray.c implements a "systray" Tcl command which permits to
 *	change the system tray/taskbar icon of a Tk toplevel window and
 *	a "sysnotify" command to post system notifications.
 *
 * Copyright © 1995-1996 Microsoft Corp.
 * Copyright © 1998 Brueckner & Jarosch Ing.GmbH, Erfurt, Germany
 * Copyright © 2020 Kevin Walzer/WordTech Communications LLC.
 * Copyright © 2020 Eric Boudaillier.
 * Copyright © 2020 Francois Vogel.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include <windows.h>
#include <shellapi.h>
#include "tkWin.h"
#include "tkWinInt.h"
#include "tkWinIco.h"

/*
 * Based extensively on the winico extension and sample code from Microsoft.
 * Some of the code was adapted into tkWinWM.c to implement the "wm iconphoto"
 * command (TIP 159), and here we are borrowing that code to use Tk images
 * to create system tray icons instead of ico files. Additionally, we are
 * removing obsolete parts of the winico extension, and implementing
 * more of the Shell_Notification API to add balloon/system notifications.
 */

#define GETHINSTANCE Tk_GetHINSTANCE()

typedef struct IcoInfo {
    HICON hIcon;                /* icon handle returned by LoadIcon. */
    unsigned id;                /* Identifier for command;  used to
				 * cancel it. */
    Tcl_Obj *taskbar_txt;       /* text to display in the taskbar */
    Tcl_Interp *interp;         /* interp which created the icon */
    Tcl_Obj *taskbar_command;   /* command to eval if events in the taskbar
				 * arrive */
    int taskbar_flags;          /* taskbar related flags*/
    HWND hwndFocus;
    struct IcoInfo *nextPtr;
} IcoInfo;

/* Per-interp struture */
typedef struct IcoInterpInfo {
    HWND hwnd;                  /* Handler window */
    int counter;                /* Counter for IcoInfo id generation */
    IcoInfo *firstIcoPtr;       /* List of created IcoInfo */
    struct IcoInterpInfo *nextPtr;
} IcoInterpInfo;

#define TASKBAR_ICON 1
#define ICON_MESSAGE WM_USER + 1234

#define HANDLER_CLASS "Wtk_TaskbarHandler"
static HWND CreateTaskbarHandlerWindow(void);

static IcoInterpInfo *firstIcoInterpPtr = NULL;
static Tk_EventProc WinIcoDestroy;

/*
 * If someone wants to see the several masks somewhere on the screen...
 * set the ICO_DRAW define and feel free to make some Tcl commands
 * for accessing it.  The normal drawing of an Icon to a DC is really easy:
 * DrawIcon(hdc,x,y,hIcon) or , more complicated
 * DrawIconEx32PlusMoreParameters ...
 */

/* #define ICO_DRAW */
#ifdef ICO_DRAW
#define RectWidth(r)((r).right - (r).left + 1)
#define RectHeight(r)((r).bottom - (r).top + 1)

/*
 *----------------------------------------------------------------------
 *
 * DrawXORMask --
 *
 *	Using DIB functions, draw XOR mask on hDC in Rect.
 *
 * Results:
 *	Icon is rendered.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static BOOL
DrawXORMask(
    HDC hDC,
    RECT Rect,
    LPLPICONIMAGE lpIcon)
{
    int x, y;

    /* Sanity checks */
    if (lpIcon == NULL)
	return FALSE;
    if (lpIcon->lpBits == NULL)
	return FALSE;

    /* Account for height*2 thing */
    lpIcon->lpbi->bmiHeader.biHeight /= 2;

    /* Locate it */
    x = Rect.left + ((RectWidth(Rect) - lpIcon->lpbi->bmiHeader.biWidth) / 2);
    y = Rect.top + ((RectHeight(Rect) - lpIcon->lpbi->bmiHeader.biHeight) / 2);

    /* Blast it to the screen */
    SetDIBitsToDevice(hDC, x, y,
	    lpIcon->lpbi->bmiHeader.biWidth,
	    lpIcon->lpbi->bmiHeader.biHeight,
	    0, 0, 0, lpIcon->lpbi->bmiHeader.biHeight,
	    lpIcon->lpXOR, lpIcon->lpbi, DIB_RGB_COLORS);

    /* UnAccount for height*2 thing */
    lpIcon->lpbi->bmiHeader.biHeight *= 2;

    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * DrawANDMask --
 *
 *	Using DIB functions, draw AND mask on hDC in Rect.
 *
 * Results:
 *	Icon is rendered.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

BOOL
DrawANDMask(
    HDC hDC,
    RECT Rect,
    LPLPICONIMAGE lpIcon)
{
    LPBITMAPINFO lpbi;
    int x, y;

    /* Sanity checks */
    if (lpIcon == NULL)
	return FALSE;
    if (lpIcon->lpBits == NULL)
	return FALSE;

    /* Need a bitmap header for the mono mask */
    lpbi = ckalloc(sizeof(BITMAPINFO) + (2 * sizeof(RGBQUAD)));
    lpbi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    lpbi->bmiHeader.biWidth = lpIcon->lpbi->bmiHeader.biWidth;
    lpbi->bmiHeader.biHeight = lpIcon->lpbi->bmiHeader.biHeight / 2;
    lpbi->bmiHeader.biPlanes = 1;
    lpbi->bmiHeader.biBitCount = 1;
    lpbi->bmiHeader.biCompression = BI_RGB;
    lpbi->miHeader.biSizeImage = 0;
    lpbi->bmiHeader.biXPelsPerMeter = 0;
    lpbi->bmiHeader.biYPelsPerMeter = 0;
    lpbi->bmiHeader.biClrUsed = 0;
    lpbi->bmiHeader.biClrImportant = 0;
    lpbi->bmiColors[0].rgbRed = 0;
    lpbi->bmiColors[0].rgbGreen = 0;
    lpbi->bmiColors[0].rgbBlue = 0;
    lpbi->bmiColors[0].rgbReserved = 0;
    lpbi->bmiColors[1].rgbRed = 255;
    lpbi->bmiColors[1].rgbGreen = 255;
    lpbi->bmiColors[1].rgbBlue = 255;
    lpbi->bmiColors[1].rgbReserved = 0;

    /* Locate it */
    x = Rect.left + ((RectWidth(Rect) - lpbi->bmiHeader.biWidth) / 2);
    y = Rect.top + ((RectHeight(Rect) - lpbi->bmiHeader.biHeight) / 2);

    /* Blast it to the screen */
    SetDIBitsToDevice(hDC, x, y,
	    lpbi->bmiHeader.biWidth,
	    lpbi->bmiHeader.biHeight,
	    0, 0, 0, lpbi->bmiHeader.biHeight,
	    lpIcon->lpAND, lpbi, DIB_RGB_COLORS);

    /* clean up */
    ckfree(lpbi);

    return TRUE;
}
#endif /* ICO_DRAW */

/*
 *----------------------------------------------------------------------
 *
 * TaskbarOperation --
 *
 *	Management of icon display.
 *
 * Results:
 *	Icon is displayed or deleted.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TaskbarOperation(
    IcoInterpInfo *icoInterpPtr,
    IcoInfo *icoPtr,
    int oper)
{
    NOTIFYICONDATAW ni;
    WCHAR *str;

    ni.cbSize = sizeof(NOTIFYICONDATAW);
    ni.hWnd = icoInterpPtr->hwnd;
    ni.uID = icoPtr->id;
    ni.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    ni.uCallbackMessage = ICON_MESSAGE;
    ni.hIcon = icoPtr->hIcon;

    if (icoPtr->taskbar_txt != NULL) {
	Tcl_DString dst;
	Tcl_DStringInit(&dst);
	str = (WCHAR *)Tcl_UtfToWCharDString(Tcl_GetString(icoPtr->taskbar_txt), TCL_INDEX_NONE, &dst);
	wcsncpy(ni.szTip, str, (Tcl_DStringLength(&dst) + 2) / 2);
	Tcl_DStringFree(&dst);
    } else {
	ni.szTip[0] = 0;
    }

    if (Shell_NotifyIconW(oper, &ni) == 1) {
	if (oper == NIM_ADD || oper == NIM_MODIFY) {
	    icoPtr->taskbar_flags |= TASKBAR_ICON;
	}
	if (oper == NIM_DELETE) {
	    icoPtr->taskbar_flags &= ~TASKBAR_ICON;
	}
    }
    /* Silently ignore error? */
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * NewIcon --
 *
 *	Create icon for display in system tray.
 *
 * Results:
 *	Icon is created for display.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static IcoInfo *
NewIcon(
    Tcl_Interp *interp,
    IcoInterpInfo *icoInterpPtr,
    HICON hIcon)
{
    IcoInfo *icoPtr;

    icoPtr = (IcoInfo *)ckalloc(sizeof(IcoInfo));
    memset(icoPtr, 0, sizeof(IcoInfo));
    icoPtr->id = ++icoInterpPtr->counter;
    icoPtr->hIcon = hIcon;
    icoPtr->taskbar_txt = NULL;
    icoPtr->interp = interp;
    icoPtr->taskbar_command = NULL;
    icoPtr->taskbar_flags = 0;
    icoPtr->hwndFocus = NULL;
    icoPtr->nextPtr = icoInterpPtr->firstIcoPtr;
    icoInterpPtr->firstIcoPtr = icoPtr;
    return icoPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeIcoPtr --
 *
 *	Delete icon and free memory.
 *
 * Results:
 *	Icon is removed from display.
 *
 * Side effects:
 *	Memory/resources freed.
 *
 *----------------------------------------------------------------------
 */

static void
FreeIcoPtr(
    IcoInterpInfo *icoInterpPtr,
    IcoInfo *icoPtr)
{
    IcoInfo *prevPtr;
    if (icoInterpPtr->firstIcoPtr == icoPtr) {
	icoInterpPtr->firstIcoPtr = icoPtr->nextPtr;
    } else {
	for (prevPtr = icoInterpPtr->firstIcoPtr; prevPtr->nextPtr != icoPtr;
		prevPtr = prevPtr->nextPtr) {
	    /* Empty loop body. */
	}
	prevPtr->nextPtr = icoPtr->nextPtr;
    }
    if (icoPtr->taskbar_flags & TASKBAR_ICON) {
	TaskbarOperation(icoInterpPtr, icoPtr, NIM_DELETE);
    }
    if (icoPtr->taskbar_txt != NULL) {
	Tcl_DecrRefCount(icoPtr->taskbar_txt);
    }
    if (icoPtr->taskbar_command != NULL) {
	Tcl_DecrRefCount(icoPtr->taskbar_command);
    }
    ckfree(icoPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * GetIcoPtr --
 *
 *	Get pointer to icon for display.
 *
 * Results:
 *	Icon is obtained for display.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static IcoInfo *
GetIcoPtr(
    Tcl_Interp *interp,
    IcoInterpInfo *icoInterpPtr,
    const char *string)
{
    IcoInfo *icoPtr;
    unsigned id;
    const char *start;
    char *end;

    if (strncmp(string, "ico#", 4) != 0) {
	goto notfound;
    }
    start = string + 4;
    id = strtoul(start, &end, 10);
    if ((end == start) || (*end != 0)) {
	goto notfound;
    }
    for (icoPtr = icoInterpPtr->firstIcoPtr; icoPtr != NULL; icoPtr = icoPtr->nextPtr) {
	if (icoPtr->id == id) {
	    return icoPtr;
	}
    }

notfound:
    Tcl_AppendResult(interp, "icon \"", string,
	"\" does not exist", NULL);
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * GetInt --
 *
 * Utility function for calculating buffer length.
 *
 * Results:
 *	Length.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
GetInt(
    long theint,
    char *buffer,
    size_t len)
{
    snprintf(buffer, len, "0x%lx", theint);
    buffer[len - 1] = 0;
    return (int) strlen(buffer);
}

/*
 *----------------------------------------------------------------------
 *
 * GetIntDec --
 *
 * Utility function for calculating buffer length.
 *
 * Results:
 *	Length.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
GetIntDec(
    long theint,
    char *buffer,
    size_t len)
{
    snprintf(buffer, len - 1, "%ld", theint);
    buffer[len - 1] = 0;
    return (int) strlen(buffer);
}

/*
 *----------------------------------------------------------------------
 *
 * TaskbarExpandPercents --
 *
 * Parse strings in taskbar display.
 *
 * Results:
 *	Strings.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static char*
TaskbarExpandPercents(
    IcoInfo *icoPtr,
    const char *msgstring,
    WPARAM wParam,
    LPARAM lParam,
    char *before,
    char *after,
    int *aftersize)
{
#define SPACELEFT (*aftersize-(dst-after)-1)
#define AFTERLEN ((*aftersize>0)?(*aftersize*2):1024)
#define ALLOCLEN ((len>AFTERLEN)?(len*2):AFTERLEN)
    char buffer[TCL_INTEGER_SPACE + 5];
    char* dst;
    dst = after;
    while (*before) {
	const char *ptr = before;
	int len = 1;
	if(*before == '%') {
	    switch(before[1]){
		case 'M':
		case 'm': {
		    before++;
		    len = strlen(msgstring);
		    ptr = msgstring;
		    break;
		}
		/* case 'W': {
		   before++;
		   len = (int)strlen(winstring);
		   ptr = winstring;
		   break;
		   }
		*/
		case 'i': {
		    before++;
		    snprintf(buffer, sizeof(buffer) - 1, "ico#%d", icoPtr->id);
		    len = strlen(buffer);
		    ptr = buffer;
		    break;
		}
		case 'w': {
		    before++;
		    len = GetInt((long)wParam,buffer, sizeof(buffer));
		    ptr = buffer;
		    break;
		}
		case 'l': {
		    before++;
		    len = GetInt((long)lParam,buffer, sizeof(buffer));
		    ptr = buffer;
		    break;
		}
		case 't': {
		    before++;
		    len = GetInt((long)GetTickCount(), buffer, sizeof(buffer));
		    ptr = buffer;
		    break;
		}
		case 'x': {
		    POINT pt;
		    GetCursorPos(&pt);
		    before++;
		    len = GetIntDec((long)pt.x, buffer, sizeof(buffer));
		    ptr = buffer;
		    break;
		}
		case 'y': {
		    POINT pt;
		    GetCursorPos(&pt);
		    before++;
		    len = GetIntDec((long)pt.y,buffer, sizeof(buffer));
		    ptr = buffer;
		    break;
		}
		case 'X': {
		    DWORD dw;
		    dw = GetMessagePos();
		    before++;
		    len = GetIntDec((long)LOWORD(dw),buffer, sizeof(buffer));
		    ptr = buffer;
		    break;
		}
		case 'Y': {
		    DWORD dw;
		    dw = GetMessagePos();
		    before++;
		    len = GetIntDec((long)HIWORD(dw),buffer, sizeof(buffer));
		    ptr = buffer;
		    break;
		}
		case 'H': {
		    before++;
		    len = GetInt(PTR2INT(icoPtr->hwndFocus), buffer, sizeof(buffer));
		    ptr = buffer;
		    break;
		}
		case '%': {
		    before++;
		    len = 1;
		    ptr = "%";
		    break;
		}
	    }
	}
	if (SPACELEFT < len) {
	    char *newspace;
	    ptrdiff_t dist = dst - after;
	    int alloclen = ALLOCLEN;
	    newspace = (char *)ckalloc(alloclen);
	    if (dist>0)
		memcpy(newspace, after, dist);
	    if (after && *aftersize) {
		ckfree(after);
	    }
	    *aftersize =alloclen;
	    after = newspace;
	    dst = after + dist;
	}
	if (len > 0) {
	    memcpy(dst, ptr, len);
	}
	dst += len;
	if ((dst-after)>(*aftersize-1)) {
	    printf("oops\n");
	}
	before++;
    }
    *dst = 0;
    return after;
}

/*
 *----------------------------------------------------------------------
 *
 * TaskbarEval --
 *
 * Parse mouse and keyboard events over taskbar.
 *
 * Results:
 *	Event processing.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
TaskbarEval(
    IcoInfo *icoPtr,
    WPARAM wParam,
    LPARAM lParam)
{
    const char *msgstring = "none";
    char evalspace[200];
    int evalsize = 200;
    char *expanded;
    int fixup = 0;

    switch (lParam) {
    case WM_MOUSEMOVE:
	msgstring = "WM_MOUSEMOVE";
	icoPtr->hwndFocus = GetFocus();
	break;
    case WM_LBUTTONDOWN:
	msgstring = "WM_LBUTTONDOWN";
	fixup = 1;
	break;
    case WM_LBUTTONUP:
	msgstring = "WM_LBUTTONUP";
	fixup = 1;
	break;
    case WM_LBUTTONDBLCLK:
	msgstring = "WM_LBUTTONDBLCLK";
	fixup = 1;
	break;
    case WM_RBUTTONDOWN:
	msgstring = "WM_RBUTTONDOWN";
	fixup = 1;
	break;
    case WM_RBUTTONUP:
	msgstring = "WM_RBUTTONUP";
	fixup = 1;
	break;
    case WM_RBUTTONDBLCLK:
	msgstring = "WM_RBUTTONDBLCLK";
	fixup = 1;
	break;
    case WM_MBUTTONDOWN:
	msgstring = "WM_MBUTTONDOWN";
	fixup = 1;
	break;
    case WM_MBUTTONUP:
	msgstring = "WM_MBUTTONUP";
	fixup = 1;
	break;
    case WM_MBUTTONDBLCLK:
	msgstring = "WM_MBUTTONDBLCLK";
	fixup = 1;
	break;
    default:
	msgstring = "WM_NULL";
	fixup = 0;
    }
    expanded = TaskbarExpandPercents(icoPtr, msgstring, wParam, lParam,
	    Tcl_GetString(icoPtr->taskbar_command), evalspace, &evalsize);
    if (icoPtr->interp != NULL) {
	int result;
	HWND hwnd = NULL;

	/* See http://support.microsoft.com/kb/q135788/
	 * Seems to have moved to https://www.betaarchive.com/wiki/index.php/Microsoft_KB_Archive/135788 */
	if (fixup) {
	    if (icoPtr->hwndFocus != NULL && IsWindow(icoPtr->hwndFocus)) {
		hwnd = icoPtr->hwndFocus;
	    } else {
		Tk_Window tkwin = Tk_MainWindow(icoPtr->interp);
		if (tkwin != NULL) {
		    hwnd = Tk_GetHWND(Tk_WindowId(tkwin));
		}
	    }
	    if (hwnd != NULL) {
		SetForegroundWindow(hwnd);
	    }
	}

	result = Tcl_GlobalEval(icoPtr->interp, expanded);

	if (hwnd != NULL) {
	    /* See http://support.microsoft.com/kb/q135788/
	     * Seems to have moved to https://www.betaarchive.com/wiki/index.php/Microsoft_KB_Archive/135788 */
	    PostMessageW(hwnd, WM_NULL, 0, 0);
	}
	if (result != TCL_OK) {
	    char buffer[100];
	    snprintf(buffer, 100, "\n  (command bound to taskbar-icon ico#%d)", icoPtr->id);
	    Tcl_AddErrorInfo(icoPtr->interp, buffer);
	    Tcl_BackgroundError(icoPtr->interp);
	}
    }
    if (expanded != evalspace) {
	ckfree(expanded);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TaskbarHandlerProc --
 *
 *	Windows callback procedure, if ICON_MESSAGE arrives, try to execute
 *	the taskbar_command.
 *
 * Results:
 *	Command execution.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static LRESULT CALLBACK
TaskbarHandlerProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    static UINT msgTaskbarCreated = 0;
    IcoInterpInfo *icoInterpPtr;
    IcoInfo *icoPtr;

    switch (message) {
    case WM_CREATE:
	msgTaskbarCreated = RegisterWindowMessage(TEXT("TaskbarCreated"));
	break;

    case ICON_MESSAGE:
	for (icoInterpPtr = firstIcoInterpPtr; icoInterpPtr != NULL; icoInterpPtr = icoInterpPtr->nextPtr) {
	    if (icoInterpPtr->hwnd == hwnd) {
		for (icoPtr = icoInterpPtr->firstIcoPtr; icoPtr != NULL; icoPtr = icoPtr->nextPtr) {
		    if (icoPtr->id == wParam) {
			if (icoPtr->taskbar_command != NULL) {
			    TaskbarEval(icoPtr, wParam, lParam);
			}
			break;
		    }
		}
		break;
	    }
	}
	break;

    default:
	/*
	 * Check to see if explorer has been restarted and we need to
	 * re-add our icons.
	 */
	if (message == msgTaskbarCreated) {
	    for (icoInterpPtr = firstIcoInterpPtr; icoInterpPtr != NULL; icoInterpPtr = icoInterpPtr->nextPtr) {
		if (icoInterpPtr->hwnd == hwnd) {
		    for (icoPtr = icoInterpPtr->firstIcoPtr; icoPtr != NULL; icoPtr = icoPtr->nextPtr) {
			if (icoPtr->taskbar_flags & TASKBAR_ICON) {
			    TaskbarOperation(icoInterpPtr, icoPtr, NIM_ADD);
			}
		    }
		    break;
		}
	    }
	}
	return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * RegisterHandlerClass --
 *
 *	Registers the handler window class.
 *
 * Results:
 *	Handler class registered.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
RegisterHandlerClass(
    HINSTANCE hInstance)
{
    WNDCLASS wndclass;
    memset(&wndclass, 0, sizeof(WNDCLASS));
    wndclass.style = 0;
    wndclass.lpfnWndProc = TaskbarHandlerProc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = 0;
    wndclass.hInstance = hInstance;
    wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndclass.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
    wndclass.lpszMenuName = NULL;
    wndclass.lpszClassName = HANDLER_CLASS;
    return RegisterClass(&wndclass);
}

/*
 *----------------------------------------------------------------------
 *
 * CreateTaskbarHandlerWindow --
 *
 *	Creates a hidden window to handle taskbar messages.
 *
 * Results:
 *	Hidden window created.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static HWND
CreateTaskbarHandlerWindow(void) {
    static int registered = 0;
    HINSTANCE hInstance = GETHINSTANCE;
    if (!registered) {
	if (!RegisterHandlerClass(hInstance))
	    return 0;
	registered = 1;
    }
    return CreateWindow(HANDLER_CLASS, "", WS_OVERLAPPED, 0, 0,
	    CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * WinIcoDestroy --
 *
 *	Event handler to delete systray icons when interp main window
 *	is deleted, either by destroy, interp deletion or application
 *	exit.
 *
 * Results:
 *	Icon/window removed and memory freed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
WinIcoDestroy(
    void *clientData,
    XEvent *eventPtr)
{
    IcoInterpInfo *icoInterpPtr = (IcoInterpInfo*) clientData;
    IcoInterpInfo *prevIcoInterpPtr;
    IcoInfo *icoPtr;
    IcoInfo *nextPtr;

    if (eventPtr->type != DestroyNotify) {
	return;
    }

    if (firstIcoInterpPtr == icoInterpPtr) {
	firstIcoInterpPtr = icoInterpPtr->nextPtr;
    } else {
	for (prevIcoInterpPtr = firstIcoInterpPtr; prevIcoInterpPtr->nextPtr != icoInterpPtr;
		prevIcoInterpPtr = prevIcoInterpPtr->nextPtr) {
	    /* Empty loop body. */
	}
	prevIcoInterpPtr->nextPtr = icoInterpPtr->nextPtr;
    }

    DestroyWindow(icoInterpPtr->hwnd);
    for (icoPtr = icoInterpPtr->firstIcoPtr; icoPtr != NULL; icoPtr = nextPtr) {
	    nextPtr = icoPtr->nextPtr;
	FreeIcoPtr(icoInterpPtr, icoPtr);
    }
    ckfree(icoInterpPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * WinSystrayCmd --
 *
 *	Main command for creating, displaying, and removing icons from taskbar.
 *
 * Results:
 *	Management of icon display in taskbar/system tray.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
WinSystrayCmd(
    void *clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    static const char *const cmdStrings[] = {
	"add", "delete", "modify", NULL
    };
    enum { CMD_ADD, CMD_DELETE, CMD_MODIFY };
    static const char *const optStrings[] = {
	"-callback", "-image", "-text", NULL
    };
    enum { OPT_CALLBACK, OPT_IMAGE, OPT_TEXT };
    int cmd, opt;

    HICON hIcon;
    int i;
    IcoInterpInfo *icoInterpPtr = (IcoInterpInfo*) clientData;
    IcoInfo *icoPtr = NULL;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "command ...");
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], cmdStrings, "command",
	    0, &cmd) == TCL_ERROR) {
	return TCL_ERROR;
    }
    switch (cmd) {
	case CMD_ADD:
	case CMD_MODIFY: {
	    Tcl_Obj *imageObj = NULL, *textObj = NULL, *callbackObj = NULL;
	    int optStart;
	    int oper;
	    if (cmd == CMD_ADD) {
		optStart = 2;
		oper = NIM_ADD;
	    } else {
		optStart = 3;
		oper = NIM_MODIFY;
		if (objc != 5) {
		    Tcl_WrongNumArgs(interp, 2, objv, "id option value");
		    return TCL_ERROR;
		}
		icoPtr = GetIcoPtr(interp, icoInterpPtr, Tcl_GetString(objv[2]));
		if (icoPtr == NULL) {
		    return TCL_ERROR;
		}
	    }
	    for (i = optStart; i < objc; i += 2) {
		if (Tcl_GetIndexFromObj(interp, objv[i], optStrings, "option",
			0, &opt) == TCL_ERROR) {
		    return TCL_ERROR;
		}
		if (i+1 >= objc) {
		    Tcl_AppendResult(interp,
			    "missing value for option \"", Tcl_GetString(objv[i]),
			    "\"", NULL);
		    return TCL_ERROR;
		}
		switch (opt) {
		    case OPT_IMAGE:
			imageObj = objv[i+1];
			break;
		    case OPT_TEXT:
			textObj = objv[i+1];
			break;
		    case OPT_CALLBACK:
			callbackObj = objv[i+1];
			break;
		}
	    }
	    if (cmd == CMD_ADD && imageObj == NULL) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj("missing required option \"-image\"", TCL_INDEX_NONE));
		return TCL_ERROR;
	    }
	    if (imageObj != NULL) {
		Tk_PhotoHandle photo;
		int width, height;
		Tk_PhotoImageBlock block;

		photo = Tk_FindPhoto(interp, Tcl_GetString(imageObj));
		if (photo == NULL) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			    "image \"%s\" does not exist", Tcl_GetString(imageObj)));
		    return TCL_ERROR;
		}
		Tk_PhotoGetSize(photo, &width, &height);
		Tk_PhotoGetImage(photo, &block);
		hIcon = CreateIcoFromPhoto(width, height, block);
		if (hIcon == NULL) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			    "failed to create an iconphoto with image \"%s\"", Tcl_GetString(imageObj)));
		    return TCL_ERROR;
		}
	    }
	    if (cmd == CMD_ADD) {
		icoPtr = NewIcon(interp, icoInterpPtr, hIcon);
	    } else {
		if (imageObj != NULL) {
		    DestroyIcon(icoPtr->hIcon);
		    icoPtr->hIcon = hIcon;
		}
	    }
	    if (callbackObj != NULL) {
		if (icoPtr->taskbar_command != NULL) {
		    Tcl_DecrRefCount(icoPtr->taskbar_command);
		}
		icoPtr->taskbar_command = callbackObj;
		Tcl_IncrRefCount(icoPtr->taskbar_command);
	    }
	    if (textObj != NULL) {
		if (icoPtr->taskbar_txt != NULL) {
		    Tcl_DecrRefCount(icoPtr->taskbar_txt);
		}
		icoPtr->taskbar_txt = textObj;
		Tcl_IncrRefCount(icoPtr->taskbar_txt);
	    }
	    TaskbarOperation(icoInterpPtr, icoPtr, oper);
	    if (cmd == CMD_ADD) {
		char buffer[5 + TCL_INTEGER_SPACE];
		int n;
		n = snprintf(buffer, sizeof(buffer) - 1, "ico#%d", icoPtr->id);
		buffer[n] = 0;
		Tcl_SetObjResult(interp, Tcl_NewStringObj(buffer, n));
	    }
	    return TCL_OK;
	}
	case CMD_DELETE:
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "id");
		return TCL_ERROR;
	    }
	    icoPtr = GetIcoPtr(interp, icoInterpPtr, Tcl_GetString(objv[2]));
	    if (icoPtr == NULL) {
		return TCL_ERROR;
	    }
	    FreeIcoPtr(icoInterpPtr, icoPtr);
	    return TCL_OK;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WinSysNotifyCmd --
 *
 *	Main command for creating and displaying notifications/balloons from system tray.
 *
 * Results:
 *	Display of notifications.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
WinSysNotifyCmd(
    void *clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    IcoInterpInfo *icoInterpPtr = (IcoInterpInfo*) clientData;
    IcoInfo *icoPtr;
    Tcl_DString infodst;
    Tcl_DString titledst;
    NOTIFYICONDATAW ni;
    char *msgtitle;
    char *msginfo;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "command ...");
	return TCL_ERROR;
    }
    if (strcmp(Tcl_GetString(objv[1]), "notify") != 0) {
	Tcl_AppendResult(interp, "unknown subcommand \"", Tcl_GetString(objv[1]),
		"\": must be notify", NULL);
	return TCL_ERROR;
    }
    if (objc != 5) {
	Tcl_WrongNumArgs(interp, 2, objv, "id title detail");
	return TCL_ERROR;
    }

    icoPtr = GetIcoPtr(interp, icoInterpPtr, Tcl_GetString(objv[2]));
    if (icoPtr == NULL) {
	return TCL_ERROR;
    }

    ni.cbSize = sizeof(NOTIFYICONDATAW);
    ni.hWnd = icoInterpPtr->hwnd;
    ni.uID = icoPtr->id;
    ni.uFlags = NIF_INFO;
    ni.uCallbackMessage = ICON_MESSAGE;
    ni.hIcon = icoPtr->hIcon;
    ni.dwInfoFlags = NIIF_INFO; /* Use a sane platform-specific icon here.*/

    msgtitle = Tcl_GetString(objv[3]);
    msginfo = Tcl_GetString(objv[4]);

    /* Balloon notification for system tray icon. */
    if (msgtitle != NULL) {
	WCHAR *title;
	Tcl_DStringInit(&titledst);
	title = Tcl_UtfToWCharDString(msgtitle, TCL_INDEX_NONE, &titledst);
	wcsncpy(ni.szInfoTitle, title, (Tcl_DStringLength(&titledst) + 2) / 2);
	Tcl_DStringFree(&titledst);
    }
    if (msginfo != NULL) {
	WCHAR *info;
	Tcl_DStringInit(&infodst);
	info = Tcl_UtfToWCharDString(msginfo, TCL_INDEX_NONE, &infodst);
	wcsncpy(ni.szInfo, info, (Tcl_DStringLength(&infodst) + 2) / 2);
	Tcl_DStringFree(&infodst);
    }

    Shell_NotifyIconW(NIM_MODIFY, &ni);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WinIcoInit --
 *
 *	Initialize this package and create script-level commands.
 *
 * Results:
 *	Initialization of code.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
WinIcoInit(
    Tcl_Interp *interp)
{
    IcoInterpInfo *icoInterpPtr;
    Tk_Window mainWindow;

    mainWindow = Tk_MainWindow(interp);
    if (mainWindow == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("main window has been destroyed", TCL_INDEX_NONE));
	return TCL_ERROR;
    }

    icoInterpPtr = (IcoInterpInfo*) ckalloc(sizeof(IcoInterpInfo));
    icoInterpPtr->counter = 0;
    icoInterpPtr->firstIcoPtr = NULL;
    icoInterpPtr->hwnd = CreateTaskbarHandlerWindow();
    icoInterpPtr->nextPtr = firstIcoInterpPtr;
    firstIcoInterpPtr = icoInterpPtr;
    Tcl_CreateObjCommand(interp, "::tk::systray::_systray", WinSystrayCmd,
	    icoInterpPtr, NULL);
    Tcl_CreateObjCommand(interp, "::tk::sysnotify::_sysnotify", WinSysNotifyCmd,
	    icoInterpPtr, NULL);

    Tk_CreateEventHandler(mainWindow, StructureNotifyMask,
	    WinIcoDestroy, icoInterpPtr);

    return TCL_OK;
}

/*
 * Local variables:
 * mode: c
 * indent-tabs-mode: nil
 * End:
 */
