#include <tcl.h>
#include <tk.h>
#include "tkWinInt.h"
#include <oleacc.h>
#include <oaidl.h>
#include <oleauto.h>
#include <initguid.h>

/*
 *----------------------------------------------------------------------
 *
 * Data definitions for MSAA-Tk integration.  
 *
 *----------------------------------------------------------------------
 */


/* Define the GUID for the MSAA interface. */
DEFINE_GUID(IID_IAccessible, 0x618736e0, 0x3c3d, 0x11cf, 0x81, 0xc, 0x0, 0xaa, 0x0, 0x38, 0x9b, 0x71);

/* TkRootAccessible structure. */
typedef struct TkRootAccessible {
  IAccessibleVtbl *lpVtbl;
  Tk_Window win; 
  Tk_Window toplevel;
  Tcl_Interp *interp;
  HWND hwnd;
  char *pathName;
  IAccessible **children;
  int numChildren;
  Tk_Window focusedChildWin;
  int focusedChildIndex; 
  LONG refCount;
} TkRootAccessible;

/* TkChildAccessible structure. */
typedef struct TkChildAccessible {
  IAccessibleVtbl *lpVtbl;
  Tk_Window win; 
  Tcl_Interp *interp;
  HWND parenthwnd;
  char *pathName;
  RECT rect;
  LONG refCount;
} TkChildAccessible;


/* 
 * Map script-level roles to C roles. 
 */
struct WinRoleMap {
  const char *tkrole;
  LONG winrole;
};

const struct WinRoleMap roleMap[] = {
  {"Button", ROLE_SYSTEM_PUSHBUTTON},
  {"Canvas", ROLE_SYSTEM_CLIENT},
  {"Checkbutton", ROLE_SYSTEM_CHECKBUTTON},
  {"Combobox", ROLE_SYSTEM_COMBOBOX},
  {"Entry", ROLE_SYSTEM_TEXT},
  {"Label", ROLE_SYSTEM_STATICTEXT},
  {"Listbox", ROLE_SYSTEM_LIST},
  {"Menu", ROLE_SYSTEM_MENUPOPUP},
  {"Notebook", ROLE_SYSTEM_PAGETABLIST},
  {"Progressbar", ROLE_SYSTEM_PROGRESSBAR},
  {"Radiobutton", ROLE_SYSTEM_RADIOBUTTON},
  {"Scale", ROLE_SYSTEM_SLIDER},
  {"Scrollbar", ROLE_SYSTEM_SCROLLBAR},
  {"Spinbox", ROLE_SYSTEM_SPINBUTTON},
  {"Table", ROLE_SYSTEM_TABLE}, 
  {"Text", ROLE_SYSTEM_TEXT},
  {"Tree", ROLE_SYSTEM_OUTLINE},
  {NULL, 0}
};

/* Hash table for managing accessibility attributes. */
extern Tcl_HashTable *TkAccessibilityObject;

/* Hash tables for linking Tk windows to accessibility object and HWND. */
static Tcl_HashTable *tkAccessibleTable;
static int tkAccessibleTableInitialized = 0;
static Tcl_HashTable *hwndToTkWindowTable;
static int hwndToTkWindowTableInitialized = 0;

/* Map Tk windows to MSAA ID's. */
typedef struct {
  Tk_Window tkwin;
  LONG childId;
} WidgetMapEntry;

static WidgetMapEntry widgetMap[512];
static int widgetMapCount = 0;
static LONG nextChildId = 1;


/*
 *----------------------------------------------------------------------
 *
 * Prototypes for toplevel MSAA objects.   
 *
 *----------------------------------------------------------------------
 */


/* Protoypes of glue functions to the IAccessible COM API - toplevels. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_QueryInterface(IAccessible *this, REFIID riid, void **ppvObject);
static ULONG STDMETHODCALLTYPE TkRootAccessible_AddRef(IAccessible *this);
static ULONG STDMETHODCALLTYPE TkRootAccessible_Release(IAccessible *this);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_GetTypeInfoCount(IAccessible *this, UINT *pctinfo);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_GetTypeInfo(IAccessible *this, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_GetIDsOfNames(IAccessible *this, REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_Invoke(IAccessible *this, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr);

/* Prototypes of empty stub functions required by MSAA -toplevels. */
HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accHelpTopic(IAccessible *this, BSTR *pszHelpFile, VARIANT varChild, long *pidTopic);
HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accKeyboardShortcut(IAccessible *this, VARIANT varChild, BSTR *pszKeyboardShortcut);
HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accSelection(IAccessible *this, VARIANT *pvarChildren);
HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accDefaultAction(IAccessible *this, VARIANT varChild, BSTR *pszDefaultAction);
HRESULT STDMETHODCALLTYPE TkRootAccessible_accNavigate(IAccessible *this, long navDir, VARIANT varStart, VARIANT *pvarEndUpAt);
HRESULT STDMETHODCALLTYPE TkRootAccessible_accHitTest(IAccessible *this, long xLeft, long yTop, VARIANT *pvarChild);
HRESULT STDMETHODCALLTYPE TkRootAccessible_put_accName( IAccessible *this, VARIANT varChild, BSTR szName);
HRESULT STDMETHODCALLTYPE TkRootAccessible_put_accValue(IAccessible *this, VARIANT varChild, BSTR szValue);

/* Prototypes of the MSAA functions that actually implement accessibility for Tk widgets on Windows - toplevels. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accName(IAccessible *this, VARIANT varChild, BSTR *pszName);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accRole(IAccessible *this, VARIANT varChild, VARIANT *pvarRole);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accState(IAccessible *this, VARIANT varChild, VARIANT *pvarState);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accValue(IAccessible *this, VARIANT varChild, BSTR *pszValue);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accParent(IAccessible *this, IDispatch **ppdispParent);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accChildCount(IAccessible *this, LONG *pcChildren);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accChild(IAccessible *this, VARIANT varChild, IDispatch **ppdispChild);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_accLocation(IAccessible *this, LONG *pxLeft, LONG *pyTop, LONG *pcxWidth, LONG *pcyHeight, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_accSelect(IAccessible *this, long flags, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_accDoDefaultAction(IAccessible *this, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accHelp(IAccessible *this, VARIANT varChild, BSTR* pszHelp);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accDescription(IAccessible *this, VARIANT varChild, BSTR *pszDescription);
static HRESULT STDMETHODCALLTYPE  TkRootAccessible_get_accFocus(IAccessible *this, VARIANT *pvarChild);

/* VTable for root accessible. */
static IAccessibleVtbl tkRootAccessibleVtbl = {
  TkRootAccessible_QueryInterface,
  TkRootAccessible_AddRef,
  TkRootAccessible_Release,
  TkRootAccessible_GetTypeInfoCount,
  TkRootAccessible_GetTypeInfo,
  TkRootAccessible_GetIDsOfNames,
  TkRootAccessible_Invoke,
  TkRootAccessible_get_accParent,
  TkRootAccessible_get_accChildCount,
  TkRootAccessible_get_accChild,
  TkRootAccessible_get_accName,
  TkRootAccessible_get_accValue,
  TkRootAccessible_get_accDescription,
  TkRootAccessible_get_accRole,
  TkRootAccessible_get_accState,
  TkRootAccessible_get_accHelp, 
  TkRootAccessible_get_accHelpTopic,
  TkRootAccessible_get_accKeyboardShortcut,
  TkRootAccessible_get_accFocus,
  TkRootAccessible_get_accSelection,
  TkRootAccessible_get_accDefaultAction,
  TkRootAccessible_accSelect,
  TkRootAccessible_accLocation,
  TkRootAccessible_accNavigate,
  TkRootAccessible_accHitTest,
  TkRootAccessible_accDoDefaultAction,
  TkRootAccessible_put_accName,
  TkRootAccessible_put_accValue
};

/* VTable for child accessible. */
static IAccessibleVtbl tkChildAccessibleVtbl = {
  TkChildAccessible_QueryInterface,
  TkChildAccessible_AddRef,
  TkChildAccessible_Release,
  TkChildAccessible_GetTypeInfoCount,
  TkChildAccessible_GetTypeInfo,
  TkChildAccessible_GetIDsOfNames,
  TkChildAccessible_Invoke,
  TkChildAccessible_get_accParent,
  TkChildAccessible_get_accChildCount,
  TkChildAccessible_get_accChild,
  TkChildAccessible_get_accName,
  TkChildAccessible_get_accValue,
  TkChildAccessible_get_accDescription,
  TkChildAccessible_get_accRole,
  TkChildAccessible_get_accState,
  TkChildAccessible_get_accHelp, 
  TkChildAccessible_get_accHelpTopic,
  TkChildAccessible_get_accKeyboardShortcut,
  TkChildAccessible_get_accFocus,
  TkChildAccessible_get_accSelection,
  TkChildAccessible_get_accDefaultAction,
  TkChildAccessible_accSelect,
  TkChildAccessible_accLocation,
  TkChildAccessible_accNavigate,
  TkChildAccessible_accHitTest,
  TkChildAccessible_accDoDefaultAction,
  TkChildAccessible_put_accName,
  TkChildAccessible_put_accValue
};

/*
 *----------------------------------------------------------------------
 *
 * Prototypes for child MSAA objects.   
 *
 *----------------------------------------------------------------------
 */


/* Protoypes of glue functions to the IAccessible COM API - child widgets. */
static HRESULT STDMETHODCALLTYPE TkChildAccessible_QueryInterface(IAccessible *this, REFIID riid, void **ppvObject);
static ULONG STDMETHODCALLTYPE TkChildAccessible_AddRef(IAccessible *this);
static ULONG STDMETHODCALLTYPE TkChildAccessible_Release(IAccessible *this);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_GetTypeInfoCount(IAccessible *this, UINT *pctinfo);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_GetTypeInfo(IAccessible *this, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_GetIDsOfNames(IAccessible *this, REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_Invoke(IAccessible *this, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr);

/* Prototypes of empty stub functions required by MSAA -child widgets. */
HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accHelpTopic(IAccessible *this, BSTR *pszHelpFile, VARIANT varChild, long *pidTopic);
HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accKeyboardShortcut(IAccessible *this, VARIANT varChild, BSTR *pszKeyboardShortcut);
HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accSelection(IAccessible *this, VARIANT *pvarChildren);
HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accDefaultAction(IAccessible *this, VARIANT varChild, BSTR *pszDefaultAction);
HRESULT STDMETHODCALLTYPE TkChildAccessible_accNavigate(IAccessible *this, long navDir, VARIANT varStart, VARIANT *pvarEndUpAt);
HRESULT STDMETHODCALLTYPE TkChildAccessible_accHitTest(IAccessible *this, long xLeft, long yTop, VARIANT *pvarChild);
HRESULT STDMETHODCALLTYPE TkChildAccessible_put_accName( IAccessible *this, VARIANT varChild, BSTR szName);
HRESULT STDMETHODCALLTYPE TkChildAccessible_put_accValue(IAccessible *this, VARIANT varChild, BSTR szValue);

/* Prototypes of the MSAA functions that actually implement accessibility for Tk widgets on Windows - child widgets. */
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accName(IAccessible *this, VARIANT varChild, BSTR *pszName);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accRole(IAccessible *this, VARIANT varChild, VARIANT *pvarRole);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accState(IAccessible *this, VARIANT varChild, VARIANT *pvarState);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accValue(IAccessible *this, VARIANT varChild, BSTR *pszValue);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accParent(IAccessible *this, IDispatch **ppdispParent);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accChildCount(IAccessible *this, LONG *pcChildren);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accChild(IAccessible *this, VARIANT varChild, IDispatch **ppdispChild);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_accLocation(IAccessible *this, LONG *pxLeft, LONG *pyTop, LONG *pcxWidth, LONG *pcyHeight, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_accSelect(IAccessible *this, long flags, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_accDoDefaultAction(IAccessible *this, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accHelp(IAccessible *this, VARIANT varChild, BSTR* pszHelp);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accDescription(IAccessible *this, VARIANT varChild, BSTR *pszDescription);
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accFocus(IAccessible *this, VARIANT *pvarChild);


/*
 *----------------------------------------------------------------------
 *
 * Prototypes of Tk functions that support MSAA integration 
 * and help implement the script-level API.
 *
 *----------------------------------------------------------------------
 */


void InitTkAccessibleTable(void);
void InitHwndToTkWindowTable(void);
TkRootAccessible *GetTkAccessibleForWindow(Tk_Window win);
Tk_Window GetTkWindowForHwnd(HWND hwnd);
static TkRootAccessible *CreateRootAccessible(Tcl_Interp *interp, HWND hwnd, const char *pathName);
static TkChildAccessible *CreateChildAccessible(Tcl_Interp *interp, HWND parenthwnd, const char *pathName);
void ForceTkWidgetFocus(HWND hwnd, LONG childId);
LONG SetChildIDForWidget(Tk_Window tkwin);
LONG GetChildIdForTkWindow(Tk_Window tkwin);
Tk_Window GetToplevelOfWidget(Tk_Window tkwin);
Tk_Window GetTkWindowForChildId(LONG childId);
int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const argv[]);
int EmitSelectionChanged(ClientData clientData,Tcl_Interp *ip, int objc, Tcl_Obj *const objv[]);
void TkRootAccessible_RegisterForCleanup(Tk_Window tkwin, void *tkAccessible);
static void TkRootAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr);
void TkRootAccessible_RegisterForFocus(Tk_Window tkwin, void *tkAccessible);
static void TkRootAccessible_FocusEventHandler (ClientData clientData, XEvent *eventPtr);
int TkRootAccessibleObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int TkWinAccessiblity_Init(Tcl_Interp *interp);

