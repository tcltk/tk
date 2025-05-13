#include <tcl.h>
#include <tk.h>
#include "tkWinInt.h"
#include <oleacc.h>
#include <oaidl.h>
#include <oleauto.h>
#include <initguid.h>


/* Define the GUID for the MSAA interface. */
DEFINE_GUID(IID_IAccessible, 0x618736e0, 0x3c3d, 0x11cf, 0x81, 0xc, 0x0, 0xaa, 0x0, 0x38, 0x9b, 0x71);

/* Data declarations used in this file. */
typedef struct TkWinAccessible {
  IAccessibleVtbl *lpVtbl;
  Tk_Window win; 
  Tk_Window toplevel;
  Tcl_Interp *interp;
  HWND hwnd;
  char *pathName;
  IAccessible **children;
  int numChildren;
  Tk_Window focusedChildWin;
  int focusedIndex; 
  LONG refCount;
} TkWinAccessible;

/* Map script-level roles to C roles. */
struct WinRoleMap {
  const char *tkrole;
  LONG winrole;
};

extern const struct WinRoleMap roleMap[];

TkWinAccessible *GetTkAccessibleForWindow(Tk_Window win);
Tk_Window GetTkWindowForHwnd(HWND hwnd);
