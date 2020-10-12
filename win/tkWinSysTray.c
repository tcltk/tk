/*
 * 	tkWinSysTray.c implements a "systray" Tcl command which permits to
 * 	change the system tray/taskbar icon of a Tk toplevel window and
 * 	a "sysnotify" command to post system notifications.
 *
 * Copyright (c) 1995-1996 Microsoft Corp.
 * Copyright (c) 1998 Brueckner & Jarosch Ing.GmbH, Erfurt, Germany
 * Copyright (c) 2020 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.

*/


#include "tkInt.h"
#include <windows.h>
#include <shellapi.h>
#include "tkWin.h"
#include "tkWinIco.h"
#include "tkWinInt.h"

/*
 * Based extensively on the winico extension and sample code from Microsoft.
 * Some of the code was adapted into tkWinWM.c to implement the "wm iconphoto"
 * command (TIP 159), and here we are borrowing that code to use Tk images
 * to create system tray icons instead of ico files. Additionally, we are
 * removing obsolete parts of the winico extension, and implementing
 * more of the Shell_Notification API to add balloon/system notifications.
 */


#define GETHINSTANCE Tk_GetHINSTANCE()
static int isWin32s = -1;
#define ISWIN32S isWin32s

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#ifdef _MSC_VER
/*
 * Earlier versions of MSVC don't know snprintf, but _snprintf is compatible.
 * Note that sprintf is deprecated.
 */
# define snprintf _snprintf
#endif

static BOOL AdjustICONIMAGEPointers(LPICONIMAGE lpImage);
static int CreateIcoFromTkImage(Tcl_Interp *interp, const char * image);

typedef struct IcoInfo {
    HICON hIcon; /* icon handle returned by LoadIcon*/
    int itype;
    int id;
    /* Integer identifier for command;  used to
                 cancel it. */
    BlockOfIconImagesPtr lpIR; /* IconresourcePtr*/
    int iconpos; /* hIcon is the nth Icon*/
    char * taskbar_txt; /* malloced text to display in the taskbar*/
    Tcl_Interp * interp; /* interp which created the icon*/
    char * taskbar_command; /* command to eval if events in the taskbar arrive*/
    int taskbar_flags; /* taskbar related flags*/
    HWND hwndFocus;
    struct IcoInfo * nextPtr;
} IcoInfo;

static IcoInfo * firstIcoPtr = NULL;
#define ICO_LOAD 1
#define ICO_FILE 2

#define TASKBAR_ICON 1
#define ICON_MESSAGE WM_USER + 1234

#define HANDLER_CLASS "Wtk_TaskbarHandler"
static HWND CreateTaskbarHandlerWindow(void);

typedef BOOL(FAR WINAPI * LPFN_SHELLNOTIFYICONW)(DWORD, PNOTIFYICONDATAW);
typedef BOOL(FAR WINAPI * LPFN_SHELLNOTIFYICONA)(DWORD, PNOTIFYICONDATAA);

static LPFN_SHELLNOTIFYICONA notify_funcA = NULL;
static LPFN_SHELLNOTIFYICONW notify_funcW = NULL;
static HMODULE hmod = NULL;
static HWND handlerWindow = NULL;
static BlockOfIconImagesPtr iconBits = NULL;

/*
 *----------------------------------------------------------------------
 *
 * swaplines --
 *
 *	This function tries to swap the lines of the bitmap in various formats.
 *
 * Results:
 *	Swaps lines as needed.
 *
 * Side effects:
 *	Accurate rendering of bitmap.
 *
 *----------------------------------------------------------------------
 *
 */

static void
swaplines(unsigned char * bits, int width, int height, int bpp) {
    #define MAX_LINE 512
    unsigned char line[MAX_LINE];
    unsigned char * pline;
    int i, BytesPerLine, middle, end;
    if (bpp > 8) {
        BytesPerLine = width * (bpp / 8);
        /* for size 16  bpp 16 BytesPerLine 32 */
        /* for size 32  bpp 16 BytesPerLine 64 */
        /* for size 16  bpp 24 BytesPerLine 48 */
        /* for size 32  bpp 24 BytesPerLine 96 */
    } else {
        BytesPerLine = width / (8 / bpp);
        /* for size 16  bpp 8  BytesPerLine 16 */
        /* for size 32  bpp 8  BytesPerLine 32 */
        /* for size 16  bpp 4  BytesPerLine 8 */
        /* for size 32  bpp 4  BytesPerLine 16 */
        /* for size 16  bpp 1  BytesPerLine 2 */
        /* for size 32  bpp 1  BytesPerLine 4 */
    }
    if (BytesPerLine < MAX_LINE)
        pline = line;
    else
        pline = (unsigned char * ) ckalloc(BytesPerLine);
    middle = (height * BytesPerLine) / 2;
    end = (height - 1) * BytesPerLine;
    for (i = 0; i < middle; i += BytesPerLine) {
        memcpy(pline, bits + i, BytesPerLine);
        memcpy(bits + i, bits + (end - i), BytesPerLine);
        memcpy(bits + (end - i), pline, BytesPerLine);
    }
    if (pline != line) {
        ckfree((char * ) pline);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * FreeIconResource --
 *
 * 	Frees memory from icon.
 *
 * Results:
 *	Memory released.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */


static void
FreeIconResource(BlockOfIconImagesPtr lpIR) {
    int i;
    if (lpIR == NULL)
        return;
    /* Free all the bits */
    for (i = 0; i < lpIR -> nNumImages; i++) {
        if (lpIR -> IconImages[i].lpBits != NULL)
            ckfree((char * ) lpIR -> IconImages[i].lpBits);
        if (lpIR -> IconImages[i].hIcon != NULL)
            DestroyIcon(lpIR -> IconImages[i].hIcon);
    }
    ckfree((char * ) lpIR);
}


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
 * 	Using DIB functions, draw XOR mask on hDC in Rect.
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
DrawXORMask(HDC hDC, RECT Rect, LPLPICONIMAGE lpIcon) {
    int x, y;

    /* Sanity checks */
    if (lpIcon == NULL)
        return FALSE;
    if (lpIcon -> lpBits == NULL)
        return FALSE;

    /* Account for height*2 thing */
    lpIcon -> lpbi -> bmiHeader.biHeight /= 2;

    /* Locate it */
    x = Rect.left + ((RectWidth(Rect) - lpIcon -> lpbi -> bmiHeader.biWidth) / 2);
    y = Rect.top + ((RectHeight(Rect) - lpIcon -> lpbi -> bmiHeader.biHeight) / 2);

    /* Blast it to the screen */
    SetDIBitsToDevice(hDC, x, y,
        lpIcon -> lpbi -> bmiHeader.biWidth,
        lpIcon -> lpbi -> bmiHeader.biHeight,
        0, 0, 0, lpIcon -> lpbi -> bmiHeader.biHeight,
        lpIcon -> lpXOR, lpIcon -> lpbi, DIB_RGB_COLORS);

    /* UnAccount for height*2 thing */
    lpIcon -> lpbi -> bmiHeader.biHeight *= 2;

    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * DrawANDMask --
 *
 * 	Using DIB functions, draw AND mask on hDC in Rect.
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
DrawANDMask(HDC hDC, RECT Rect, LPLPICONIMAGE lpIcon) {
    LPBITMAPINFO lpbi;
    int x, y;

    /* Sanity checks */
    if (lpIcon == NULL)
        return FALSE;
    if (lpIcon -> lpBits == NULL)
        return FALSE;

    /* Need a bitmap header for the mono mask */
    lpbi = ckalloc(sizeof(BITMAPINFO) + (2 * sizeof(RGBQUAD)));
    lpbi -> bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    lpbi -> bmiHeader.biWidth = lpIcon -> lpbi -> bmiHeader.biWidth;
    lpbi -> bmiHeader.biHeight = lpIcon -> lpbi -> bmiHeader.biHeight / 2;
    lpbi -> bmiHeader.biPlanes = 1;
    lpbi -> bmiHeader.biBitCount = 1;
    lpbi -> bmiHeader.biCompression = BI_RGB;
    lpbi -> bmiHeader.biSizeImage = 0;
    lpbi -> bmiHeader.biXPelsPerMeter = 0;
    lpbi -> bmiHeader.biYPelsPerMeter = 0;
    lpbi -> bmiHeader.biClrUsed = 0;
    lpbi -> bmiHeader.biClrImportant = 0;
    lpbi -> bmiColors[0].rgbRed = 0;
    lpbi -> bmiColors[0].rgbGreen = 0;
    lpbi -> bmiColors[0].rgbBlue = 0;
    lpbi -> bmiColors[0].rgbReserved = 0;
    lpbi -> bmiColors[1].rgbRed = 255;
    lpbi -> bmiColors[1].rgbGreen = 255;
    lpbi -> bmiColors[1].rgbBlue = 255;
    lpbi -> bmiColors[1].rgbReserved = 0;

    /* Locate it */
    x = Rect.left + ((RectWidth(Rect) - lpbi -> bmiHeader.biWidth) / 2);
    y = Rect.top + ((RectHeight(Rect) - lpbi -> bmiHeader.biHeight) / 2);

    /* Blast it to the screen */
    SetDIBitsToDevice(hDC, x, y, lpbi -> bmiHeader.biWidth, lpbi -> bmiHeader.biHeight, 0, 0, 0, lpbi -> bmiHeader.biHeight, lpIcon -> lpAND, lpbi, DIB_RGB_COLORS);

    /* clean up */
    ckfree((char * ) lpbi);

    return TRUE;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * FindDIBBits --
 *
 * 	Locate the image bits in a CF_DIB format DIB.
 *
 * Results:
 *	Icon is rendered.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static LPSTR
FindDIBBits(LPSTR lpbi) {
    return (lpbi + * (LPDWORD) lpbi + PaletteSize(lpbi));
}

/*
 *----------------------------------------------------------------------
 *
 * NotifyA --
 *
 * 	Display icon in system tray on older ANSI-encoded systems.
 *
 * Results:
 *	Icon is displayed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */


static BOOL
NotifyA(IcoInfo * icoPtr, int oper, HICON hIcon, char * txt) {
    NOTIFYICONDATAA ni;
    Tcl_DString dst;
    CHAR * str;

    ni.cbSize = sizeof(NOTIFYICONDATAA);
    ni.hWnd = CreateTaskbarHandlerWindow();
    ni.uID = icoPtr -> id;
    ni.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    ni.uCallbackMessage = ICON_MESSAGE;
    ni.hIcon = (HICON) hIcon;

    /* Tooltip text for system tray icon.*/
    str = (CHAR * ) Tcl_UtfToExternalDString(NULL, txt, -1, & dst);
    strncpy(ni.szTip, str, 63);
    ni.szTip[63] = 0;
    Tcl_DStringFree( & dst);

    return notify_funcA(oper, & ni);
}

/*
 *----------------------------------------------------------------------
 *
 * NotifyW --
 *
 * 	Display icon in system tray on more recent systems supporting Unicode.
 *
 * Results:
 *	Icon is displayed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static BOOL
NotifyW(IcoInfo * icoPtr, int oper, HICON hIcon, char * txt) {
    NOTIFYICONDATAW ni;
    Tcl_DString dst;
    WCHAR * str;
    Tcl_Encoding Encoding;

    ni.cbSize = sizeof(NOTIFYICONDATAW);
    ni.hWnd = CreateTaskbarHandlerWindow();
    ni.uID = icoPtr -> id;
    ni.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    ni.uCallbackMessage = ICON_MESSAGE;
    ni.hIcon = (HICON) hIcon;

    Encoding = Tcl_GetEncoding(NULL, "unicode");
    str = (WCHAR * ) Tcl_UtfToExternalDString(Encoding, txt, -1, & dst);
    wcsncpy(ni.szTip, str, 63);
    ni.szTip[63] = 0;
    Tcl_DStringFree( & dst);
    Tcl_FreeEncoding(Encoding);
    return notify_funcW(oper, & ni);
}

/*
 *----------------------------------------------------------------------
 *
 * TaskbarOperation --
 *
 * 	Management of icon display.
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
TaskbarOperation(IcoInfo * icoPtr, int oper, HICON hIcon, char * txt) {
    int result;

    if (notify_funcA == NULL && notify_funcW == NULL && hmod == NULL) {
        hmod = GetModuleHandle("SHELL32.DLL");
        if (hmod == NULL)
            hmod = LoadLibrary("SHELL32.DLL");
        if (hmod == NULL) {
            Tcl_AppendResult(icoPtr -> interp, " Could not Load SHELL32.DLL", (char * ) NULL);
            return TCL_ERROR;
        }
        notify_funcW = (LPFN_SHELLNOTIFYICONW) GetProcAddress(hmod, "Shell_NotifyIconW");
        notify_funcA = (LPFN_SHELLNOTIFYICONA) GetProcAddress(hmod, "Shell_NotifyIconA");
        if (notify_funcW == NULL && notify_funcA == NULL) {
            Tcl_AppendResult(icoPtr -> interp,
                " Could not get address of Shell_NotifyIconW or Shell_NotifyIconA",
                (char * ) NULL);
            return TCL_ERROR;
        }
    } else if (notify_funcA == NULL && notify_funcW == NULL) {
        Tcl_AppendResult(icoPtr -> interp,
            " You probably don't have a Windows shell", (char * ) NULL);
        return TCL_ERROR;
    }

    if (notify_funcW != NULL) {
        result = NotifyW(icoPtr, oper, hIcon, txt) ||
            NotifyA(icoPtr, oper, hIcon, txt);
    } else {
        result = NotifyA(icoPtr, oper, hIcon, txt);
    }

    Tcl_SetObjResult(icoPtr -> interp, Tcl_NewIntObj(result));
    if (result == 1) {
        if (oper == NIM_ADD || oper == NIM_MODIFY) {
            icoPtr -> taskbar_flags |= TASKBAR_ICON;
        }
        if (oper == NIM_DELETE) {
            icoPtr -> taskbar_flags &= ~TASKBAR_ICON;
        }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * NewIcon --
 *
 * 	Create icon for display in system tray.
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
    NewIcon(Tcl_Interp * interp, HICON hIcon,
        int itype, BlockOfIconImagesPtr lpIR, int iconpos) {
        static int nextId = 1;
        int n;
        char buffer[5 + TCL_INTEGER_SPACE];
        IcoInfo * icoPtr;

        icoPtr = (IcoInfo * ) ckalloc((unsigned)(sizeof(IcoInfo)));
        memset(icoPtr, 0, sizeof(IcoInfo));
        icoPtr -> id = nextId;
        icoPtr -> hIcon = hIcon;
        icoPtr -> itype = itype;
        icoPtr -> lpIR = lpIR;
        icoPtr -> iconpos = iconpos;
        n = _snprintf(buffer, sizeof(buffer) - 1, "ico#%d", icoPtr -> id);
        buffer[n] = 0;
        icoPtr -> taskbar_txt = ckalloc((int) strlen(buffer) + 1);
        strcpy(icoPtr -> taskbar_txt, buffer);
        icoPtr -> interp = interp;
        icoPtr -> taskbar_command = NULL;
        icoPtr -> taskbar_flags = 0;
        icoPtr -> hwndFocus = NULL;
        if (itype == ICO_LOAD) {
            icoPtr -> lpIR = (BlockOfIconImagesPtr) NULL;
            icoPtr -> iconpos = 0;
        }
        nextId += 1;
        icoPtr -> nextPtr = firstIcoPtr;
        firstIcoPtr = icoPtr;
        Tcl_SetObjResult(interp, Tcl_NewStringObj(buffer, -1));
        return icoPtr;
    }

/*
 *----------------------------------------------------------------------
 *
 * FreeIcoPtr --
 *
 * 	Delete icon and free memory.
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
FreeIcoPtr(Tcl_Interp * interp, IcoInfo * icoPtr) {
    IcoInfo * prevPtr;
    if (firstIcoPtr == icoPtr) {
        firstIcoPtr = icoPtr -> nextPtr;
    } else {
        for (prevPtr = firstIcoPtr; prevPtr -> nextPtr != icoPtr; prevPtr = prevPtr -> nextPtr) {
            /* Empty loop body. */
        }
        prevPtr -> nextPtr = icoPtr -> nextPtr;
    }
    if (icoPtr -> taskbar_flags & TASKBAR_ICON) {
        TaskbarOperation(icoPtr, NIM_DELETE, NULL, "");
        Tcl_ResetResult(interp);
    }
    if (icoPtr -> itype == ICO_FILE) {
		ckfree(icoPtr->lpIR);
    }
    if (icoPtr -> taskbar_txt != NULL) {
        ckfree((char * ) icoPtr -> taskbar_txt);
    }

    if (icoPtr -> taskbar_command != NULL) {
        ckfree((char * ) icoPtr -> taskbar_command);
    }
    ckfree((char * ) icoPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * GetIcoPtr --
 *
 * 	Get pointer to icon for display.
 *
 * Results:
 *	Icon is obtained for display.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static IcoInfo * GetIcoPtr(Tcl_Interp * interp, char * string) {
    IcoInfo * icoPtr;
    int id;
    char * end;

    if (strncmp(string, "ico#", 4) != 0) {
        return NULL;
    }
    string += 4;
    id = strtoul(string, & end, 10);
    if ((end == string) || ( * end != 0)) {
        return NULL;
    }
    for (icoPtr = firstIcoPtr; icoPtr != NULL; icoPtr = icoPtr -> nextPtr) {
        if (icoPtr -> id == id) {
            return icoPtr;
        }
    }
    Tcl_AppendResult(interp, " icon \"", string,
        "\" doesn't exist", (char * ) NULL);
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
GetInt(long theint, char * buffer, size_t len) {
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
GetIntDec(long theint, char * buffer, size_t len) {
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
TaskbarExpandPercents(IcoInfo *icoPtr, char *msgstring,
    WPARAM wParam, LPARAM lParam, char *before, char *after, int *aftersize)
{
#define SPACELEFT (*aftersize-(dst-after)-1)
#define AFTERLEN ((*aftersize>0)?(*aftersize*2):1024)
#define ALLOCLEN ((len>AFTERLEN)?(len*2):AFTERLEN)
    char buffer[TCL_INTEGER_SPACE + 5];
    char* dst;
    dst = after;
    while (*before) {
        char *ptr=before;
        int len=1;
        if(*before=='%') {
            switch(before[1]){
                case 'M':
                case 'm': {
                    before++;
                    len=(int)strlen(msgstring);
                    ptr=msgstring;
                    break;
                }
                /* case 'W': {
                   before++;
                   len=(int)strlen(winstring);
                   ptr=winstring;
                   break;
                   }
                */
                case 'i': {
                    before++;
                    snprintf(buffer, sizeof(buffer) - 1, "ico#%d", icoPtr->id);
                    len=(int)strlen(buffer);
                    ptr=buffer;
                    break;
                }
                case 'w': {
                    before++;
                    len=GetInt((long)wParam,buffer, sizeof(buffer));
                    ptr=buffer;
                    break;
                }
                case 'l': {
                    before++;
                    len=GetInt((long)lParam,buffer, sizeof(buffer));
                    ptr=buffer;
                    break;
                }
                case 't': {
                    before++;
                    len=GetInt((long)GetTickCount(), buffer, sizeof(buffer));
                    ptr=buffer;
                    break;
                }
                case 'x': {
                    POINT pt;
                    GetCursorPos(&pt);
                    before++;
                    len=GetIntDec((long)pt.x, buffer, sizeof(buffer));
                    ptr=buffer;
                    break;
                }
                case 'y': {
                    POINT pt;
                    GetCursorPos(&pt);
                    before++;
                    len=GetIntDec((long)pt.y,buffer, sizeof(buffer));
                    ptr=buffer;
                    break;
                }
                case 'X': {
                    DWORD dw;
                    dw=GetMessagePos();
                    before++;
                    len=GetIntDec((long)LOWORD(dw),buffer, sizeof(buffer));
                    ptr=buffer;
                    break;
                }
                case 'Y': {
                    DWORD dw;
                    dw=GetMessagePos();
                    before++;
                    len=GetIntDec((long)HIWORD(dw),buffer, sizeof(buffer));
                    ptr=buffer;
                    break;
                }
                case 'H': {
                    before++;
                    len = GetInt((long)icoPtr->hwndFocus, buffer, sizeof(buffer));
                    ptr = buffer;
                    break;
                }
                case '%': {
                    before++;
                    len=1;
                    ptr="%";
                    break;
                }
            }
        }
        if (SPACELEFT < len) {
            char *newspace;
            ptrdiff_t dist = dst - after;
            int alloclen = ALLOCLEN;
            newspace = (char *) ckalloc(alloclen);
            if(dist>0)
                memcpy((VOID *) newspace, (VOID *) after, dist);
            if(after && *aftersize)
                ckfree(after);
            *aftersize =alloclen;
            after = newspace;
            dst=after+dist;
        }
        if(len>0)
            memcpy(dst,ptr,len);
        dst+=len;
        if((dst-after)>(*aftersize-1))
            printf("oops\n");
        before++;
    }
    *dst=0;
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
TaskbarEval(IcoInfo * icoPtr, WPARAM wParam, LPARAM lParam) {
    char * msgstring = "none";
    char evalspace[200];
    int evalsize = 200;
    char * expanded;
    int fixup = 0;

    switch (lParam) {
    case WM_MOUSEMOVE:
        msgstring = "WM_MOUSEMOVE";
        icoPtr -> hwndFocus = GetFocus();
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
        icoPtr -> taskbar_command, evalspace, & evalsize);
    if (icoPtr -> interp != NULL) {
        int result;
        HWND hwnd = NULL;

        /* See http//:support.microsoft.com/kb/q135788 */
        if (fixup) {
            if (icoPtr -> hwndFocus != NULL && IsWindow(icoPtr -> hwndFocus)) {
                hwnd = icoPtr -> hwndFocus;
            } else {
                Tk_Window tkwin = Tk_MainWindow(icoPtr -> interp);
                hwnd = Tk_GetHWND(Tk_WindowId(tkwin));
            }
            SetForegroundWindow(hwnd);
        }

        result = Tcl_GlobalEval(icoPtr -> interp, expanded);

        if (hwnd != NULL) {
            /* See http:/ /support.microsoft.com/kb/q135788/ */
            PostMessage(hwnd, WM_NULL, 0, 0);
        }
        if (result != TCL_OK) {
            char buffer[100];
            sprintf(buffer, "\n  (command bound to taskbar-icon ico#%d)", icoPtr -> id);
            Tcl_AddErrorInfo(icoPtr -> interp, buffer);
            Tcl_BackgroundError(icoPtr -> interp);
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
 * 	Windows callback procedure, if ICON_MESSAGE arrives, try to execute
 * 	the taskbar_command.
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
TaskbarHandlerProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static UINT msgTaskbarCreated = 0;
    IcoInfo * icoPtr = NULL;

    switch (message) {
    case WM_CREATE:
        msgTaskbarCreated = RegisterWindowMessage(TEXT("TaskbarCreated"));
        break;

    case ICON_MESSAGE:
        for (icoPtr = firstIcoPtr; icoPtr != NULL; icoPtr = icoPtr -> nextPtr) {
            if (wParam == (WPARAM) icoPtr -> id) {
                if (icoPtr -> taskbar_command != NULL) {
                    TaskbarEval(icoPtr, wParam, lParam);
                }
            }
        }
        break;

    default:
        /*
         * Check to see if explorer has been restarted and we ned to
         * re-add our icons.
         */
        if (message == msgTaskbarCreated) {
            for (icoPtr = firstIcoPtr; icoPtr != NULL; icoPtr = icoPtr -> nextPtr) {
                if (icoPtr -> taskbar_flags & TASKBAR_ICON) {
                    HICON hIcon = icoPtr -> hIcon;
                    if (icoPtr -> iconpos != 0 && icoPtr -> lpIR != NULL) {
                        hIcon = icoPtr -> lpIR -> IconImages[icoPtr -> iconpos].hIcon;
                    }
                    TaskbarOperation(icoPtr, NIM_ADD, hIcon, icoPtr -> taskbar_txt);
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
 * 	Registers the handler window class.
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
RegisterHandlerClass(HINSTANCE hInstance) {
    WNDCLASS wndclass;
    memset( & wndclass, 0, sizeof(WNDCLASS));
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
    return RegisterClass( & wndclass);
}

/*
 *----------------------------------------------------------------------
 *
 * CreateTaskbarHandlerWindow --
 *
 * 	Creates a hidden window to handle taskbar messages.
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
    if (handlerWindow)
        return handlerWindow;
    if (!registered) {
        if (!RegisterHandlerClass(hInstance))
            return 0;
        registered = 1;
    }
    return (handlerWindow = CreateWindow(HANDLER_CLASS, "", WS_OVERLAPPED, 0, 0,
        CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL));
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyHandlerWindow --
 *
 * 	Destroys hidden window.
 *
 * Results:
 *	Hidden window deleted.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
DestroyHandlerWindow(void) {
    if (handlerWindow)
        DestroyWindow(handlerWindow);
}


/*
 *----------------------------------------------------------------------
 *
 * StandardIcon --
 *
 * 	Returns standard Windows icons.
 *
 * Results:
 *	Icons displayed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static char *
StandardIcon(char * arg) {
	if (!stricmp(arg, "application"))
		return IDI_APPLICATION;
	if (!stricmp(arg, "asterisk"))
		return IDI_ASTERISK;
	if (!stricmp(arg, "error"))
		return IDI_ERROR;
	if (!stricmp(arg, "exclamation"))
		return IDI_EXCLAMATION;
	if (!stricmp(arg, "hand"))
		return IDI_HAND;
	if (!stricmp(arg, "question"))
		return IDI_QUESTION;
	if (!stricmp(arg, "information"))
		return IDI_INFORMATION;
	if (!stricmp(arg, "warning"))
		return IDI_WARNING;
	if (!stricmp(arg, "winlogo"))
		return IDI_WINLOGO;
	return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * NameOrHandle --
 *
 * 	Tries to get a valid window handle from a Tk-pathname for a toplevel.
 *
 * Results:
 *	Window handle obtained.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
NameOrHandle(Tcl_Interp * interp, char * arg, HWND * hwndPtr) {
    #define WINFO_FRAME "wm frame "
    Tk_Window tkwin;
    size_t limit = 0;
    char * cmd;

    if (Tcl_GetInt(interp, arg, (int * ) hwndPtr) == TCL_OK) {
        return TCL_OK;
    }
    Tcl_ResetResult(interp);
    tkwin = Tk_NameToWindow(interp, arg, Tk_MainWindow(interp));
    if (tkwin == NULL) {
        Tcl_AppendResult(interp, arg, " is no valid windowpath", (char * ) NULL);
        return TCL_ERROR;
    }
    if (!Tk_IsTopLevel(tkwin)) {
        Tcl_AppendResult(interp, arg,
            " is not a toplevel valid windowpath", (char * ) NULL);
        return TCL_ERROR;
    }
    limit = strlen(arg) + strlen(WINFO_FRAME);
    cmd = ckalloc((int) limit + 1);
    strcpy(cmd, WINFO_FRAME);
    strcat(cmd, arg);
    if (Tcl_Eval(interp, cmd) == TCL_ERROR) {
        return TCL_ERROR;
    }
    strncpy(cmd, Tcl_GetStringResult(interp), limit);
    cmd[limit] = 0;
    if (sscanf(cmd, "0x%x", (int * ) hwndPtr) != 1) {
        Tcl_AppendResult(interp, "couldn't scan ", cmd, (char * ) NULL);
        return TCL_ERROR;
    }
    if ( * hwndPtr == NULL) {
        Tcl_AppendResult(interp, "couldn't get windowid from ",
            cmd, (char * ) NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WinIcoDestroy --
 *
 * 	Deletes icon and hidden window from display.
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
WinIcoDestroy(ClientData clientData) {
    IcoInfo * icoPtr;
    IcoInfo * nextPtr;
    Tcl_Interp * interp = (Tcl_Interp * ) clientData;
    DestroyHandlerWindow();
    for (icoPtr = firstIcoPtr; icoPtr != NULL; icoPtr = nextPtr) {
        nextPtr = icoPtr -> nextPtr;
        FreeIcoPtr(interp, icoPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CreateIcoFromTkImage --
 *
 *	Create ico pointer from Tk image for display in system tray. Adapted
 *      from "wm iconphoto" code in tkWinWm.c.

 * Results:
 *	Icon image is created from a valid Tk photo image.
 *
 * Side effects:
 *	Icon is created.
 *
 *----------------------------------------------------------------------
 */

static int
CreateIcoFromTkImage(
    Tcl_Interp *interp,		/* Current interpreter. */
    const char * image          /* Image to convert. */
                     )
{

    Tk_PhotoHandle photo;
    Tk_PhotoImageBlock block;
    int width, height, idx, bufferSize, startObj = 3;
    union {unsigned char *ptr; void *voidPtr;} bgraPixel;
    union {unsigned char *ptr; void *voidPtr;} bgraMask;
    HICON hIcon;
    unsigned size;
    BITMAPINFO bmInfo;
    ICONINFO iconInfo;

    /*
     * Iterate over all images to validate their existence.
     */

	photo = Tk_FindPhoto(interp, image);
	if (photo == NULL) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "can't use \"%s\" as icon: not a photo image",
		    image));
	    return TCL_ERROR;
	}

    /*
     * We have calculated the size of the data. Try to allocate the needed
     * memory space.
     */

    size = sizeof(BlockOfIconImages) + (sizeof(ICONIMAGE));
    iconBits = (BlockOfIconImagesPtr)attemptckalloc(size);
    if (iconBits == NULL) {
	return TCL_ERROR;
    }
    ZeroMemory(iconBits, size);

    iconBits->nNumImages = 1;

	photo = Tk_FindPhoto(interp, image);
	Tk_PhotoGetSize(photo, &width, &height);
	Tk_PhotoGetImage(photo, &block);

	/*
	 * Don't use CreateIcon to create the icon, as it requires color
	 * bitmap data in device-dependent format. Instead we use
	 * CreateIconIndirect which takes device-independent bitmaps and
	 * converts them as required. Initialise icon info structure.
	 */

	ZeroMemory(&iconInfo, sizeof(iconInfo));
	iconInfo.fIcon = TRUE;

	/*
	 * Create device-independent color bitmap.
	 */

	ZeroMemory(&bmInfo, sizeof bmInfo);
	bmInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmInfo.bmiHeader.biWidth = width;
	bmInfo.bmiHeader.biHeight = -height;
	bmInfo.bmiHeader.biPlanes = 1;
	bmInfo.bmiHeader.biBitCount = 32;
	bmInfo.bmiHeader.biCompression = BI_RGB;

	iconInfo.hbmColor = CreateDIBSection(NULL, &bmInfo, DIB_RGB_COLORS,
		&bgraPixel.voidPtr, NULL, 0);
	if (!iconInfo.hbmColor) {
	    FreeIconResource(iconBits);
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "failed to create an iconphoto with image \"%s\"",
		    image));
	    return TCL_ERROR;
	}

	/*
	 * Convert the photo image data into BGRA format (RGBQUAD).
	 */

	bufferSize = height * width * 4;
	for (idx = 0 ; idx < bufferSize ; idx += 4) {
	    bgraPixel.ptr[idx] = block.pixelPtr[idx+2];
	    bgraPixel.ptr[idx+1] = block.pixelPtr[idx+1];
	    bgraPixel.ptr[idx+2] = block.pixelPtr[idx+0];
	    bgraPixel.ptr[idx+3] = block.pixelPtr[idx+3];
	}

	/*
	 * Create a dummy mask bitmap. The contents of this don't appear to
	 * matter, as CreateIconIndirect will setup the icon mask based on the
	 * alpha channel in our color bitmap.
	 */

	bmInfo.bmiHeader.biBitCount = 1;

	iconInfo.hbmMask = CreateDIBSection(NULL, &bmInfo, DIB_RGB_COLORS,
		&bgraMask.voidPtr, NULL, 0);
	if (!iconInfo.hbmMask) {
	    DeleteObject(iconInfo.hbmColor);
	    FreeIconResource(iconBits);
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "failed to create mask bitmap for \"%s\"",
		    image));
	    return TCL_ERROR;
	}

	ZeroMemory(bgraMask.ptr, width*height/8);

	/*
	 * Create an icon from the bitmaps.
	 */

	hIcon = CreateIconIndirect(&iconInfo);
	DeleteObject(iconInfo.hbmColor);
	DeleteObject(iconInfo.hbmMask);
	if (hIcon == NULL) {
	    /*
	     * XXX should free up created icons.
	     */

	    FreeIconResource(iconBits);
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "failed to create icon for \"%s\"",
		    image));
	    return TCL_ERROR;
	}
	iconBits->IconImages[0].Width = width;
	iconBits->IconImages[0].Height = height;
	iconBits->IconImages[0].Colors = 4;
	iconBits->IconImages[0].hIcon = hIcon;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WinIcoCmd --
 *
 * 	Main command for creating, displaying, and removing icons from taskbar.
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
WinIcoCmd(ClientData clientData, Tcl_Interp * interp,
    int argc, const char * argv[]) {
    size_t length;
    HICON hIcon;
    int i;
    IcoInfo * icoPtr;
    BlockOfIconImagesPtr lpIR = NULL;
    if (argc < 2) {
        Tcl_AppendResult(interp, " wrong # args: should be \"",
            argv[0], " option ?arg arg ...?\"", (char * ) NULL);
        return TCL_ERROR;
    }

    length = strlen(argv[1]);
    if ((strncmp(argv[1], "createfrom", length) == 0) && (length >= 2)) {

        int pos = 0;
        if (argc < 3) {
            Tcl_AppendResult(interp, " wrong # args,must be:",
                argv[0], " createfrom <Tk image> ", (char * ) NULL);
            return TCL_ERROR;
        }

        CreateIcoFromTkImage(interp, argv[2]);
        lpIR = iconBits;

        if (lpIR == NULL) {
            Tcl_AppendResult(interp, " reading of ", argv[2], " failed!", (char * ) NULL);
            return TCL_ERROR;
        }
        hIcon = NULL;
        for (i = 0; i < lpIR -> nNumImages; i++) {
            /*take the first or a 32x32 16 color icon*/
            if (i == 0 ||
                (lpIR -> IconImages[i].Height == 32 && lpIR -> IconImages[i].Width == 32 &&
                    lpIR -> IconImages[i].Colors == 4)) {
                hIcon = lpIR -> IconImages[i].hIcon;
                pos = i;
            }
        }
        if (hIcon == NULL) {
			FreeIconResource(lpIR);
            Tcl_AppendResult(interp, " Could not find an icon in ", argv[2], (char * ) NULL);
            return TCL_ERROR;
        }
        NewIcon(interp, hIcon, ICO_FILE, lpIR, pos);
    } else if ((strncmp(argv[1], "delete", length) == 0) &&
        (length >= 2)) {
        if (argc != 3) {
            Tcl_AppendResult(interp, " wrong # args: should be \"",
                argv[0], " delete ?id?\"", (char * ) NULL);
            return TCL_ERROR;
        }
        icoPtr = GetIcoPtr(interp, (char * ) argv[2]);
        if (icoPtr == NULL) {
            Tcl_ResetResult(interp);
            return TCL_OK;
        }
        FreeIcoPtr(interp, icoPtr);
        return TCL_OK;
    } else if ((strncmp(argv[1], "text", length) == 0) && (length >= 2)) {
        if (argc < 3) {
            Tcl_AppendResult(interp, " wrong # args: should be \"",
                argv[0], " text <id> ?newtext?\"", (char * ) NULL);
            return TCL_ERROR;
        }
        if ((icoPtr = GetIcoPtr(interp, (char * ) argv[2])) == NULL) return TCL_ERROR;
        if (argc > 3) {
            char * newtxt = (char * ) argv[3];
            if (icoPtr -> taskbar_txt != NULL) {
                ckfree(icoPtr -> taskbar_txt);
            }
            icoPtr -> taskbar_txt = ckalloc((int) strlen(newtxt) + 1);
            strcpy(icoPtr -> taskbar_txt, newtxt);
        }
        Tcl_AppendResult(interp, icoPtr -> taskbar_txt, (char * ) NULL);
        return TCL_OK;
    } else if ((strncmp(argv[1], "taskbar", length) == 0) && (length >= 2)) {
        char * callback = NULL;
        int oper;
        char ** args;
        int c;
        int length;
        int count;
        char * txt;
        if (argc < 4) {
            Tcl_AppendResult(interp, " wrong # args: should be \"",
                argv[0], " taskbar <add/delete/modify> <id> ?-callback <callback>? \"", (char * ) NULL);
            return TCL_ERROR;
        }
        if (strcmp(argv[2], "add") == 0) {
            oper = NIM_ADD;
        } else if (strncmp(argv[2], "del", 3) == 0) {
            oper = NIM_DELETE;
        } else if (strncmp(argv[2], "mod", 3) == 0) {
            oper = NIM_MODIFY;
        } else {
            Tcl_AppendResult(interp, " bad argument ", argv[2], "should be add, delete or modify", (char * ) NULL);
            return TCL_ERROR;
        }
        if ((icoPtr = GetIcoPtr(interp, (char * ) argv[3])) == NULL)
            return TCL_ERROR;
        hIcon = icoPtr -> hIcon;
        txt = icoPtr -> taskbar_txt;
        if (argc > 4) {
            for (count = argc - 4, args = (char ** ) argv + 4; count > 1; count -= 2, args += 2) {
                if (args[0][0] != '-')
                    goto wrongargs2;
                c = args[0][1];
                length = (int) strlen(args[0]);
                if ((c == '-') && (length == 2)) {
                    break;
                }
                if ((c == 'c') && (strncmp(args[0], "-callback", length) == 0)) {
                    callback = args[1];
                } else if ((c == 't') && (strncmp(args[0], "-text", length) == 0)) {
                    txt = args[1];
                } else {
                    goto wrongargs2;
                }
            }
            if (count == 1)
                goto wrongargs2;
        }
        if (callback != NULL) {
            if (icoPtr -> taskbar_command != NULL) {
                ckfree((char * ) icoPtr -> taskbar_command);
            }
            icoPtr -> taskbar_command = ckalloc((int) strlen(callback) + 1);
            strcpy(icoPtr -> taskbar_command, callback);
        }
        return TaskbarOperation(icoPtr, oper, hIcon, txt);
        wrongargs2:
            Tcl_AppendResult(interp, " unknown option \"", args[0], "\",valid are:",
                "-callback <tcl-callback>  -text <tooltiptext>", (char * ) NULL);
        return TCL_ERROR;
    } else {
        Tcl_AppendResult(interp, " bad argument \"", argv[1],
            "\": must be  createfrom, info, hicon, pos, text, taskbar",
            (char * ) NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * WinSystrayCmd --
 *
 * 	Main command for creating and displaying notifications/balloons from system tray.
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
WinSystrayCmd(ClientData clientData, Tcl_Interp * interp,
    int argc, const char * argv[]) {
    size_t length;
    HICON hIcon = NULL;
    IcoInfo * icoPtr;
    Tcl_DString infodst;
    Tcl_DString titledst;
    NOTIFYICONDATAA ni;
    char * msgtitle;
    char * msginfo;
    char * title;
    char * info;

    icoPtr = GetIcoPtr(interp, (char * ) argv[2]);
    if (icoPtr == NULL) {
        Tcl_ResetResult(interp);
        return TCL_OK;
    }

    ni.cbSize = sizeof(NOTIFYICONDATAA);
    ni.hWnd = CreateTaskbarHandlerWindow();
    ni.uID = icoPtr -> id;
    ni.uFlags = NIF_INFO;
    ni.uCallbackMessage = ICON_MESSAGE;
    ni.hIcon = icoPtr -> hIcon;
	ni.dwInfoFlags = NIIF_INFO;

    if (argc < 2) {
        Tcl_AppendResult(interp, " wrong # args: should be \"",
            argv[0], " option ?arg arg ...?\"", (char * ) NULL);
        return TCL_ERROR;
    }

    length = strlen(argv[1]);
    if ((strncmp(argv[1], "notify", length) == 0) &&
        (length >= 2)) {
        if (argc != 5) {
            Tcl_AppendResult(interp, " wrong # args: should be \"",
                argv[0], " notify ?id? ?title? ?detail?\"", (char * ) NULL);
            return TCL_ERROR;
        }

        msgtitle = (char * ) argv[3];
        msginfo = (char * ) argv[4];

        /* Balloon notification for system tray icon. */
        if (msgtitle != NULL) {
            title = (CHAR * ) Tcl_UtfToExternalDString(NULL, msgtitle, -1, & titledst);
            strncpy(ni.szInfoTitle, msgtitle, Tcl_DStringLength( & titledst) + 1);
            Tcl_DStringFree( & titledst);
        }
        if (msginfo != NULL) {
            info = (CHAR * ) Tcl_UtfToExternalDString(NULL, msginfo, -1, & infodst);
            strncpy(ni.szInfo, msginfo, Tcl_DStringLength( & infodst) + 1);
            Tcl_DStringFree( & infodst);
        }

        Shell_NotifyIcon(NIM_MODIFY, & ni);
        return TCL_OK;
    }
	return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * WinIcoInit --
 *
 * 	Initialize this package and create script-level commands.
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
WinIcoInit(Tcl_Interp * interp) {
    OSVERSIONINFO info;
    #ifdef USE_TCL_STUBS
    if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) {
        return TCL_ERROR;
    }
    #endif
    #ifdef USE_TK_STUBS
    if (Tk_InitStubs(interp, TK_VERSION, 0) == NULL) {
        return TCL_ERROR;
    }
    #endif

    info.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx( & info);
    isWin32s = (info.dwPlatformId == VER_PLATFORM_WIN32s);

    Tcl_CreateCommand(interp, "_systray", WinIcoCmd, (ClientData)interp,
        (Tcl_CmdDeleteProc *) WinIcoDestroy);
    Tcl_CreateCommand(interp, "_sysnotify", WinSystrayCmd, NULL, NULL);
    return TCL_OK;
}

/*
 * Local variables:
 * mode: c
 * indent-tabs-mode: nil
 * End:
 */



