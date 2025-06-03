/*
 * tkWinAccessibility.c --
 *
 * This file implements the platform-native Microsoft C
 * Accessibility API for Tk on Windows.
 *
 * Copyright (c) 2024-2025 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution
 of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

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
DEFINE_GUID(IID_IAccessible, 0x618736e0, 0x3c3d, 0x11cf, 0x81, 0xc, 0x0,
	    0xaa, 0x0, 0x38, 0x9b, 0x71);

/* TkRootAccessible structure. */
typedef struct TkRootAccessible {
  IAccessibleVtbl *lpVtbl;
  Tk_Window win;
  Tk_Window toplevel;
  Tcl_Interp *interp;
  HWND hwnd;
  IAccessible **children;
  int numChildren;
  LONG refCount;
} TkRootAccessible;


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
static Tcl_HashTable *hwndToTkWindowTable;
static int hwndToTkWindowTableInitialized = 0;
static Tcl_HashTable *childIdTable = NULL;
static Tcl_ThreadId tkMainThreadId;
static int nextId = 1;
static Tcl_HashTable *accObjectTable = NULL;
static int accObjectTableInitialized = 0;


/*
 * Event structure for queueing tasks to the main Tk thread.
 */
typedef struct TkAccessibleEvent {
  Tcl_Event event;
  enum {
    ACCESSIBLE_DO_DEFAULT_ACTION,
    ACCESSIBLE_EMIT_FOCUS_CHANGE,
    ACCESSIBLE_EMIT_SELECTION_CHANGE
  } type;
  union {
    struct {
      Tk_Window win;
      Tcl_Interp *interp;
    } doDefaultAction;
    struct {
      Tk_Window win;
      HWND hwnd;
      LONG childId;
    } emitFocusChange;
    struct {
      Tk_Window win;
      HWND hwnd;
    } emitSelectionChange;
  } data;
} TkAccessibleEvent;

/* Structs for deferring execution of data. */
typedef struct {
  Tcl_Interp *interp;
  char *cmd;
} DeferredActionData;

typedef struct {
  HWND hwnd;
  LONG childId;
} DeferredFocusInfo;

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
static HRESULT STDMETHODCALLTYPE TkRootAccessible_GetIDsOfNames(IAccessible*this, REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_Invoke(IAccessible *this, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr);

/* Prototypes of empty stub functions required by MSAA-toplevels. */
HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accHelpTopic(IAccessible *this, BSTR *pszHelpFile, VARIANT varChild, long *pidTopic);
HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accKeyboardShortcut(IAccessible *this, VARIANT varChild, BSTR *pszKeyboardShortcut);
HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accSelection(IAccessible *this, VARIANT *pvarChildren);
HRESULT STDMETHODCALLTYPE TkRootAccessible_accNavigate(IAccessible *this, long navDir, VARIANT varStart, VARIANT *pvarEndUpAt);
HRESULT STDMETHODCALLTYPE TkRootAccessible_accHitTest(IAccessible *this, long xLeft, long yTop, VARIANT *pvarChild);
HRESULT STDMETHODCALLTYPE TkRootAccessible_put_accName(IAccessible *this, VARIANT varChild, BSTR szName);
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
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accDefaultAction(IAccessible *this, VARIANT varChild, BSTR *pszDefaultAction);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_accDoDefaultAction(IAccessible *this, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accHelp(IAccessible *this, VARIANT varChild, BSTR* pszHelp);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accDescription(IAccessible *this, VARIANT varChild, BSTR *pszDescription);
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accFocus(IAccessible *this, VARIANT *pvarChild);

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

/*
 *----------------------------------------------------------------------
 *
 * Prototypes for child widgets using MSAA childId.
 *
 *----------------------------------------------------------------------
 */

static HRESULT GetAccRoleForChild(Tk_Window win, VARIANT *pvarRole);
static HRESULT GetAccNameForChild(Tk_Window win, BSTR *pName);
static HRESULT GetAccStateForChild(Tk_Window win, VARIANT *pvarState);
static HRESULT GetAccFocusForChild(Tk_Window win, VARIANT *pvarChild);
static HRESULT GetAccDescriptionForChild(Tk_Window win, BSTR *pDesc);
static HRESULT GetAccValueForChild(Tk_Window win, BSTR *pValue);
static void DoDefaultActionForChildOnMainThread(Tcl_Interp *interp, Tk_Window win);
						

/*
 *----------------------------------------------------------------------
 *
 * Prototypes of Tk functions that support MSAA integration
 * and help implement the script-level API.
 *
 *----------------------------------------------------------------------
 */

void InitHwndToTkWindowTable(void);
void InitTkRootAccesibleTable(void);
void InitChildIdTable(void);
void ClearChildIdTable(void);
Tk_Window GetTkWindowForHwnd(HWND hwnd);
TkRootAccessible *CreateRootAccessibleFromWindow(Tk_Window win, HWND hwnd);
static void SetChildIdForTkWindow(Tk_Window win, int id);
static int GetChildIdForTkWindow(Tk_Window win);
Tk_Window GetToplevelOfWidget(Tk_Window tkwin);
Tk_Window GetTkWindowForChildId(int id);
static void DeferredDoDefaultAction(ClientData clientData);
static void DoDefaultActionInternal(Tcl_Interp *interp, Tk_Window win);
int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const argv[]);
int EmitSelectionChanged(ClientData clientData,Tcl_Interp *ip, int objc, Tcl_Obj *const objv[]);
int EmitFocusChanged(ClientData clientData,Tcl_Interp *ip, int objc, Tcl_Obj *const objv[]);
void TkRootAccessible_RegisterForCleanup(Tk_Window tkwin, void *tkAccessible);
static void TkRootAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr);
static void TkWidgetFocusHandler(ClientData clientData, XEvent *eventPtr);
static void DeferredNotifyFocus(ClientData clientData);
static void AssignChildIdsRecursive(Tk_Window win, Tcl_Interp *interp);
static int MainThreadAccessibleProc(Tcl_Event *eventPtr, int flags);
int TkRootAccessibleObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int TkWinAccessiblity_Init(Tcl_Interp *interp);

/*
 *----------------------------------------------------------------------
 *
 * Glue functions to the IAccessible COM API - toplevels.
 *
 *----------------------------------------------------------------------
 */

/*Empty stub functions required by MSAA. */
HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accHelpTopic(IAccessible *this, BSTR *pszHelpFile, VARIANT varChild, long *pidTopic) 
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accKeyboardShortcut(IAccessible *this, VARIANT varChild, BSTR *pszKeyboardShortcut) 
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accSelection(IAccessible *this, VARIANT *pvarChildren) 
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkRootAccessible_accNavigate(IAccessible *this, long navDir, VARIANT varStart, VARIANT *pvarEndUpAt) 
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkRootAccessible_accHitTest(IAccessible *this, long xLeft, long yTop, VARIANT *pvarChild)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkRootAccessible_put_accName(IAccessible *this, VARIANT varChild, BSTR szName) 
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkRootAccessible_put_accValue(IAccessible *this, VARIANT varChild, BSTR szValue) 
{
  return E_NOTIMPL;
}

/*Begin active functions.*/
static HRESULT STDMETHODCALLTYPE TkRootAccessible_QueryInterface(IAccessible *this, REFIID riid, void **ppvObject) 
{
  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch)
      || IsEqualIID(riid, &IID_IAccessible)) {
    *ppvObject = this;
    TkRootAccessible_AddRef(this);
    return S_OK;
  }
  *ppvObject = NULL;
  return E_NOINTERFACE;
}

/* Function to add memory reference to the MSAA object. */
static ULONG STDMETHODCALLTYPE TkRootAccessible_AddRef(IAccessible *this) 
{
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
  return InterlockedIncrement(&tkAccessible->refCount);
}

/* Function to free the MSAA object. */
static ULONG STDMETHODCALLTYPE TkRootAccessible_Release(IAccessible *this) 
{
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
  ULONG count = InterlockedDecrement(&tkAccessible->refCount);

  if (count == 0) {
    ckfree(tkAccessible);
  }
  return count;
}

static HRESULT STDMETHODCALLTYPE TkRootAccessible_GetTypeInfoCount(IAccessible *this, UINT *pctinfo) 
{
  *pctinfo = 0;
  return S_OK;
}

static HRESULT STDMETHODCALLTYPE TkRootAccessible_GetTypeInfo(IAccessible *this, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo) 
{
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkRootAccessible_GetIDsOfNames(IAccessible *this, REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId) 
{
  ITypeInfo *pTypeInfo = NULL;
  HRESULT hr;

  hr = TkRootAccessible_GetTypeInfo(this, 0, lcid, &pTypeInfo);
  if (FAILED(hr)) {
    return hr;
  }

  hr = DispGetIDsOfNames(pTypeInfo, rgszNames, cNames, rgDispId);
  pTypeInfo->lpVtbl->Release(pTypeInfo);

  return hr;
}

static HRESULT STDMETHODCALLTYPE TkRootAccessible_Invoke(IAccessible *this, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr) 
{
  IDispatch* pDisp = NULL;

  {
    if (!pVarResult) {
      return E_POINTER;
    }

    VariantInit(pVarResult);

    VARIANT selfVar;
    selfVar.vt = VT_I4;
    selfVar.lVal = CHILDID_SELF;

    switch (dispIdMember) {
    case DISPID_ACC_NAME:
      return TkRootAccessible_get_accName(this, selfVar,
					  &pVarResult->bstrVal);

    case DISPID_ACC_VALUE:
      return TkRootAccessible_get_accValue(this, selfVar,
					   &pVarResult->bstrVal);

    case DISPID_ACC_ROLE:
      return TkRootAccessible_get_accRole(this, selfVar, pVarResult);

    case DISPID_ACC_STATE:
      return TkRootAccessible_get_accState(this, selfVar, pVarResult);

    case DISPID_ACC_DESCRIPTION:
      return TkRootAccessible_get_accDescription(this, selfVar,
						 &pVarResult->bstrVal);

    case DISPID_ACC_HELP:
      return TkRootAccessible_get_accHelp(this, selfVar,
					  &pVarResult->bstrVal);

    case DISPID_ACC_DEFAULTACTION:
      return TkRootAccessible_get_accDefaultAction(this, selfVar,
						   &pVarResult->bstrVal);

    case DISPID_ACC_DODEFAULTACTION:
      return TkRootAccessible_accDoDefaultAction(this, selfVar);

    case DISPID_ACC_FOCUS:
      return TkRootAccessible_get_accFocus(this, pVarResult);
    }
  }
  return S_OK;
}

/* Function to map accessible name to MSAA.*/
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accName(IAccessible *this, VARIANT varChild, BSTR *pName) 
{
  if (!pName) return E_INVALIDARG;

  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
  
  /* Return NULL because the name matches the role. */
  *pName = NULL;
  return S_FALSE;
}
  

/* Function to map accessible role to MSAA. For toplevels, return ROLE_SYSTEM_WINDOW.*/
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accRole(IAccessible *this, VARIANT varChild, VARIANT *pvarRole) 
{
  if (!pvarRole) return E_INVALIDARG;
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;

  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    pvarRole->vt = VT_I4;
    pvarRole->lVal = ROLE_SYSTEM_WINDOW;
    return S_OK;
  }

  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    Tk_Window child = GetTkWindowForChildId(varChild.lVal);
    if (!child) return E_INVALIDARG;
    return GetAccRoleForChild(child, pvarRole);
  }
  return E_INVALIDARG;
}

/* Function to map accessible state to MSAA. For toplevel, return STATE_SYSTEM_FOCUSABLE. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accState(IAccessible *this, VARIANT varChild, VARIANT *pvarState) 
{
  if (!pvarState) return E_INVALIDARG;
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;

  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    pvarState->vt = VT_I4;
    pvarState->lVal = STATE_SYSTEM_FOCUSABLE;
    return S_OK;
  }

  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    Tk_Window child = GetTkWindowForChildId(varChild.lVal);
    if (!child) return E_INVALIDARG;
    return GetAccStateForChild(child, pvarState);
  }

  return DISP_E_MEMBERNOTFOUND;
}

/* Function to map accessible value to MSAA. For toplevel, return NULL.*/
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accValue(IAccessible *this, VARIANT varChild, BSTR *pszValue) 
{
  if (!pszValue) return E_INVALIDARG;
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;

  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    *pszValue = NULL;
    return DISP_E_MEMBERNOTFOUND;
  }

  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    Tk_Window child = GetTkWindowForChildId(varChild.lVal);
    if (!child) return E_INVALIDARG;
    return GetAccValueForChild(child, pszValue);
  }

  return DISP_E_MEMBERNOTFOUND;
}

/* Function to get accessible parent. For toplevel, return NULL. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accParent(IAccessible *this, IDispatch **ppdispParent) 
{
  /* Cast this to TkRootAccessible, assuming 'this' is of that type. */
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;

  /* For toplevel, set ppdispParent to NULL and return S_OK. */
  *ppdispParent = NULL;
  return S_OK;
}

/* Function to get number of accessible children to MSAA. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accChildCount(IAccessible *this, LONG *pcChildren) 
{
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
  int count = 0;
  TkWindow *child;
  Tk_Window toplevel = tkAccessible->win; 
  TkWindow *winPtr = (TkWindow *)toplevel;
  for (child = winPtr->childList; child != NULL; child = child->nextPtr) {
    if (Tk_IsMapped(child)) {
      count++;
    }
  }
  *pcChildren = count;
  return S_OK;
}

/* Function to get accessible children to MSAA. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accChild(IAccessible *this, VARIANT varChild, IDispatch **ppdispChild) 
{
  if (!ppdispChild) return E_INVALIDARG;
  *ppdispChild = NULL;

  /* We only support VT_I4 child IDs. */
  if (varChild.vt != VT_I4 || varChild.lVal <= 0) {
    return E_INVALIDARG;
  }

  /* Lookup child Tk_Window for this ID. */
  Tk_Window childWin = GetTkWindowForChildId(varChild.lVal);
  if (!childWin) {
    return E_INVALIDARG;
  }

  /*
   * MSAA expects only VT_DISPATCH if the child is *an object* — we don’t
   * return that.  So we just say "this ID is valid but not an object".
   * MSAA will then call get_accName/Role/etc. with that ID).
   */

  return S_FALSE;  /* Child exists but is not an object (per MSAA spec). */
}

/* Function to get accessible frame to MSAA. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_accLocation(IAccessible *this, LONG *pxLeft,LONG *pyTop, LONG *pcxWidth, LONG *pcyHeight, VARIANT varChild) 
{
  if (!pxLeft || !pyTop || !pcxWidth || !pcyHeight)
    return E_INVALIDARG;

  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;

  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    RECT clientRect;
    GetClientRect(tkAccessible->hwnd, &clientRect);

    POINT screenCoords = { clientRect.left, clientRect.top };
    MapWindowPoints(tkAccessible->hwnd, HWND_DESKTOP, &screenCoords, 1);

    *pxLeft = screenCoords.x;
    *pyTop = screenCoords.y;
    *pcxWidth = clientRect.right - clientRect.left;
    *pcyHeight = clientRect.bottom - clientRect.top;
    return S_OK;
  }

  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    Tk_Window child = GetTkWindowForChildId(varChild.lVal);
    if (!child) return E_INVALIDARG;

    Tk_MakeWindowExist(child);
    int x, y;
    Tk_GetRootCoords(child, &x, &y);
    int width = Tk_Width(child);
    int height = Tk_Height(child);

    *pxLeft = x;
    *pyTop = y;
    *pcxWidth = width;
    *pcyHeight = height;
    return S_OK;
  }

  return E_INVALIDARG;
}


/*Function to set accessible focus on Tk widget. For toplevel, just return. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_accSelect(IAccessible *thisPtr, long flags, VARIANT varChild) 
{
  return E_NOTIMPL;
}

/* Function to return default action for role. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accDefaultAction(IAccessible *this, VARIANT varChild, BSTR *pszDefaultAction) 
{
  if (!pszDefaultAction) return E_INVALIDARG;
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;

  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    *pszDefaultAction = NULL;
    return S_FALSE;
  }

  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    Tk_Window child = GetTkWindowForChildId(varChild.lVal);
    if (!child) return E_INVALIDARG;

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, child);
    if (!hPtr) return S_FALSE;

    Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
    Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "role");

    if (!hPtr2) return S_FALSE;

    const char *tkrole = Tcl_GetString(Tcl_GetHashValue(hPtr2));

    /* Return standard default action string for known roles. */
    const wchar_t *action = NULL;
    if (strcmp(tkrole, "Button") == 0 ||
	strcmp(tkrole, "Checkbutton") == 0 ||
	strcmp(tkrole, "Radiobutton") == 0) {
      action = L"Press";
    } else if (strcmp(tkrole, "Entry") == 0 || strcmp(tkrole, "Text") == 0)
      {
	action = L"Edit";
      } else if (strcmp(tkrole, "Table") == 0 || strcmp(tkrole, "Tree") == 0)
      {
	action = L"Select";
      }

    if (action) {
      *pszDefaultAction = SysAllocString(action);
      return S_OK;
    }

    return S_FALSE; /* No default action known. */
  }

  return E_INVALIDARG;
}


/* Function to get button press to MSAA. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_accDoDefaultAction(IAccessible *this, VARIANT varChild) 
{
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;

  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    return S_OK;
  }

  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    Tk_Window child = GetTkWindowForChildId(varChild.lVal);
    if (!child) return E_INVALIDARG;

    /* Queue the default action to be executed on the main 
     * thread. Callbacks into the Tcl interpreter from 
     * the MSAA thread can cause Wish to hang. 
     */
    DoDefaultActionForChildOnMainThread(tkAccessible->interp, child);
    return S_OK; /* Return immediately, the action will happen asynchronously. */
  }

  return E_INVALIDARG;
}

/* Function to get accessible help to MSAA. For toplevel, just return. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accHelp(IAccessible *this, VARIANT varChild, BSTR* pszHelp) 
{
  return E_NOTIMPL;
}

/* Function to get accessible focus to MSAA. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accFocus(IAccessible *this, VARIANT *pvarChild) 
{
  if (!pvarChild) return E_INVALIDARG;
  VariantInit(pvarChild);  /* Initialize to VT_EMPTY. */

  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
  Tk_Window focusWin = NULL;
  TkWindow *focusPtr = TkGetFocusWin((TkWindow *)tkAccessible->win);
  if (focusPtr && focusPtr->window != None) {
    focusWin = (Tk_Window)focusPtr;
  }

  if (!focusWin || focusWin == tkAccessible->win) {
    pvarChild->vt = VT_I4;
    pvarChild->lVal = CHILDID_SELF;
    return S_OK;
  }

  /* Lookup the assigned child ID (no rebuild here). */
  int childId = GetChildIdForTkWindow(focusWin);
  if (childId > 0) {
    pvarChild->vt = VT_I4;
    pvarChild->lVal = childId;
    return S_OK;
  }

  /* Fallback: child is focused but wasn't assigned an ID. */
  pvarChild->vt = VT_I4;
  pvarChild->lVal = CHILDID_SELF;
  return S_OK;
}


/* Function to get accessible description to MSAA. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accDescription(IAccessible *this, VARIANT varChild, BSTR *pszDescription) 
{
  if (!pszDescription) return E_INVALIDARG;
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;

  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    /* Toplevels do not need a description. */
    *pszDescription = NULL;
    return S_FALSE;
  }

  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    Tk_Window child = GetTkWindowForChildId(varChild.lVal);
    if (!child) return E_INVALIDARG;
    return GetAccDescriptionForChild(child, pszDescription);
  }

  return E_INVALIDARG;
}


/*
 *----------------------------------------------------------------------
 *
 * Glue functions - child widgets.
 *
 *----------------------------------------------------------------------
 */



/* Function to map accessible name to MSAA.*/
static HRESULT GetAccNameForChild(Tk_Window win, BSTR *pName) 
{
  if (!win || !pName) return E_INVALIDARG;

  Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) return S_FALSE;

  Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
  Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "name");
  if (!hPtr2) return S_FALSE;

  const char *name = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  Tcl_DString ds;
  Tcl_DStringInit(&ds);
  *pName = SysAllocString(Tcl_UtfToWCharDString(name, -1, &ds));
  Tcl_DStringFree(&ds);
  return S_OK;
}


/* Function to map accessible role to MSAA.*/
static HRESULT GetAccRoleForChild(Tk_Window win, VARIANT *pvarRole) 
{
  if (!win || !pvarRole) return E_INVALIDARG;

  Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) return S_FALSE;

  Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
  Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "role");
  if (!hPtr2) return S_FALSE;

  const char *tkrole = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  int role = ROLE_SYSTEM_CLIENT; /* Fallback. */

  for (int i = 0; roleMap[i].tkrole != NULL; i++) {
    if (strcmp(tkrole, roleMap[i].tkrole) == 0) {
      role = roleMap[i].winrole;
      break;
    }
  }

  pvarRole->vt = VT_I4;
  pvarRole->lVal = role;
  return S_OK;
}


/* Function to map accessible state to MSAA. */
static HRESULT GetAccStateForChild(Tk_Window win, VARIANT *pvarState) 
{
  if (!win || !pvarState) return E_INVALIDARG;

  Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) return S_FALSE;

  Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
  Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "state");

  long state = STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_SELECTABLE;  /* Reasonable default. */

  if (hPtr2) {
    const char *stateresult = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    if (strcmp(stateresult, "disabled") == 0) {
      state = STATE_SYSTEM_UNAVAILABLE;
    }
  }

  /* Check if this widget has focus. */
  TkWindow *focusPtr = TkGetFocusWin((TkWindow *)win);
  if (focusPtr == (TkWindow *)win) {
    state |= STATE_SYSTEM_FOCUSED;
  }

  pvarState->vt = VT_I4;
  pvarState->lVal = state;
  return S_OK;
}

/* Function to map accessible value to MSAA.*/
static HRESULT GetAccValueForChild(Tk_Window win, BSTR *pValue) 
{
  if (!win || !pValue) return E_INVALIDARG;

  Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) return S_FALSE;

  Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
  Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "value");
  if (!hPtr2) return S_FALSE;

  const char *val = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  Tcl_DString ds;
  Tcl_DStringInit(&ds);
  *pValue = SysAllocString(Tcl_UtfToWCharDString(val, -1, &ds));
  Tcl_DStringFree(&ds);
  return S_OK;
}

/* Function to queue a request to execute a default action on the main Tk thread. */

static void DoDefaultActionForChildOnMainThread(Tcl_Interp *interp, Tk_Window win) 
{
  TkAccessibleEvent *eventPtr = (TkAccessibleEvent *)
    Tcl_Alloc(sizeof(TkAccessibleEvent));
  eventPtr->event.proc = MainThreadAccessibleProc;
  eventPtr->type = ACCESSIBLE_DO_DEFAULT_ACTION;
  eventPtr->data.doDefaultAction.interp = interp;
  eventPtr->data.doDefaultAction.win = win;
  Tcl_ThreadQueueEvent(tkMainThreadId, (Tcl_Event *)eventPtr,
		       TCL_QUEUE_TAIL);
}

/* Function to execute DoDefaultAction on deferred basis. */
static void DeferredDoDefaultAction(ClientData clientData) {
  DeferredActionData *data = (DeferredActionData *)clientData;
  if (data->interp && data->cmd) {
    Tcl_EvalEx(data->interp, data->cmd, -1, TCL_EVAL_GLOBAL);
  }
  ckfree(data->cmd);
  ckfree(data);
}

/* Function to execute DoDefaultAction on main thread. */
static void DoDefaultActionInternal(Tcl_Interp *interp, Tk_Window win) 
{
  if (!interp || !win) return;

  Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) return;

  Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
  Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "action");
  if (!hPtr2) return;

  const char *cmd = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  if (!cmd) return;

  /* Defer Tcl_EvalEx with a timer to avoid COM reentrancy issues. */
  DeferredActionData *data = (DeferredActionData *)ckalloc(sizeof(DeferredActionData));
  data->interp = interp;
  data->cmd = ckalloc(strlen(cmd) + 1);
  strcpy(data->cmd, cmd);

  Tcl_CreateTimerHandler(0, DeferredDoDefaultAction, (ClientData)data);
}
/* Function to get MSAA focus. */
static HRESULT GetAccFocusForChild(Tk_Window win, VARIANT *pvarChild) 
{
  if (!win || !pvarChild) return E_INVALIDARG;

  VariantInit(pvarChild);
  pvarChild->vt = VT_I4;
  pvarChild->lVal = CHILDID_SELF;
  return S_OK;
}

/* Function to get MSAA description. */
static HRESULT GetAccDescriptionForChild(Tk_Window win, BSTR *pDesc) 
{
  if (!win || !pDesc) return E_INVALIDARG;

  Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) return S_FALSE;

  Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
  Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes,
					   "description");
  if (!hPtr2) return S_FALSE;

  const char *desc = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  Tcl_DString ds;
  Tcl_DStringInit(&ds);
  *pDesc = SysAllocString(Tcl_UtfToWCharDString(desc, -1, &ds));
  Tcl_DStringFree(&ds);
  return S_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * General/utility functions to help integrate the MSAA API
 * the Tcl script-level API.
 *
 *----------------------------------------------------------------------
 */

/* Function to map Tk window to MSAA attributes. */
TkRootAccessible *CreateRootAccessibleFromWindow(Tk_Window win, HWND hwnd) {
  if (!win) return NULL;
  
  /*Add objects to hash tables. */
  Tcl_HashEntry *entry;
  int newEntry;
  entry = Tcl_CreateHashEntry(hwndToTkWindowTable, hwnd, &newEntry);
  Tcl_SetHashValue(entry, win);
  
  /* 
   * Guard against race condition for accessible object creation.
   *Only create new object if not already found in hash table. 
   */
  entry = Tcl_FindHashEntry(accObjectTable, hwnd);
  if (!entry) {
    entry = Tcl_CreateHashEntry(accObjectTable, hwnd, &newEntry);
    Tcl_SetHashValue(entry, win);
  }

  TkRootAccessible *tkAccessible = (TkRootAccessible *)ckalloc(sizeof(TkRootAccessible));
  if (!tkAccessible) return NULL;

 
  tkAccessible->lpVtbl = &tkRootAccessibleVtbl;
  tkAccessible->interp = Tk_Interp(win);
  tkAccessible->hwnd = hwnd;
  tkAccessible->refCount = 1;
  tkAccessible->win = win;



  /* Notify screen readers of creation. */
  NotifyWinEvent(EVENT_OBJECT_CREATE, hwnd, OBJID_CLIENT, CHILDID_SELF);
  NotifyWinEvent(EVENT_OBJECT_SHOW, hwnd, OBJID_CLIENT, CHILDID_SELF);
  NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, hwnd, OBJID_CLIENT, CHILDID_SELF);
  
  return tkAccessible;
}


/* Function to map Tk window to MSAA ID's. */
static void SetChildIdForTkWindow(Tk_Window win, int id) 
{
  if (!childIdTable) {
    childIdTable = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(childIdTable, TCL_ONE_WORD_KEYS);
  }

  Tcl_HashEntry *entry;
  int newEntry;

  entry = Tcl_CreateHashEntry(childIdTable, (ClientData)win, &newEntry);
  Tcl_SetHashValue(entry, INT2PTR(id));
}

/* Function to retrieve MSAA ID for a specifc Tk window. */
static int GetChildIdForTkWindow(Tk_Window win) 
{
  if (!childIdTable) return -1;

  Tcl_HashEntry *entry = Tcl_FindHashEntry(childIdTable, (ClientData)win);
  if (!entry) return -1;
  return PTR2INT(Tcl_GetHashValue(entry));
}

/* Function to retrieve Tk window for a specifc MSAA ID. */
Tk_Window GetTkWindowForChildId(int id) 
{
  if (!childIdTable) return NULL;

  Tcl_HashSearch search;
  Tcl_HashEntry *entry;
  for (entry = Tcl_FirstHashEntry(childIdTable, &search);
       entry != NULL;
       entry = Tcl_NextHashEntry(&search)) {
    if (PTR2INT(Tcl_GetHashValue(entry)) == id) {
      return (Tk_Window)Tcl_GetHashKey(childIdTable, entry);
    }
  }
  return NULL;
}

/* Function to return the Tk toplevel window that contains a given Tk widget. */
Tk_Window GetToplevelOfWidget(Tk_Window tkwin) 
{
  /* First check if the tkwin is NULL (destroyed). If yes, exit. */
  if (tkwin == NULL) {
    return NULL;
  }

  /*Tk window exists. Now look for toplevel. If no parent found, return null. */
  Tk_Window current = tkwin;
  while (current != NULL && Tk_WindowId(current) != None) {
    Tk_Window parent = Tk_Parent(current);
    if (parent == NULL) {
      break;
    }
    current = parent;
  }
  return current;
}

/* Function to initialize HWND -> Tk hash table. */
void InitHwndToTkWindowTable(void) 
{
  if (!hwndToTkWindowTableInitialized) {
    hwndToTkWindowTable = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(hwndToTkWindowTable, TCL_ONE_WORD_KEYS);
    hwndToTkWindowTableInitialized = 1;
  }
}

/* Function to TkRootAccessible hash table. */
void InitTkRootAccesibleTable(void) 
{
  if (!accObjectTableInitialized) {
    accObjectTable = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(accObjectTable, TCL_ONE_WORD_KEYS);
    accObjectTableInitialized = 1;
  }

}

/* Function to initialize childId hash table. */
void InitChildIdTable(void) 
{
  if (!childIdTable) {
    childIdTable = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(childIdTable, TCL_ONE_WORD_KEYS);
  }
}

/* Function to clear childId hash table. */
void ClearChildIdTable(void) 
{
  if (!childIdTable) return;

  Tcl_HashSearch search;
  Tcl_HashEntry *entry;

  for (entry = Tcl_FirstHashEntry(childIdTable, &search);
       entry != NULL;
       entry = Tcl_NextHashEntry(&search)) {
    Tcl_DeleteHashEntry(entry);
  }
}

/* Function to retrieve Tk window associated with HWND. */
Tk_Window GetTkWindowForHwnd(HWND hwnd) 
{
  if (!hwndToTkWindowTableInitialized) {
    return NULL;
  }
  Tcl_HashEntry *entry = Tcl_FindHashEntry(hwndToTkWindowTable,
					   (ClientData)hwnd);
  if (entry) {
    return (Tk_Window)Tcl_GetHashValue(entry);
  }
  return NULL;
}

/* Function to assign childId's dynamically. */
static void AssignChildIdsRecursive(Tk_Window win, Tcl_Interp *interp) 
{	
  if (!win || !Tk_IsMapped(win)) return;

  SetChildIdForTkWindow(win, nextId++);

  TkWindow *winPtr = (TkWindow *)win;
  for (TkWindow *child = winPtr->childList; child != NULL; child = child->nextPtr) {
    AssignChildIdsRecursive((Tk_Window)child, interp);
  }
}

/* Function to defer focus events to the main thread. */
static void DeferredNotifyFocus(ClientData clientData) {
  DeferredFocusInfo *info = (DeferredFocusInfo *)clientData;
  NotifyWinEvent(EVENT_OBJECT_FOCUS, info->hwnd, OBJID_CLIENT, info->childId);
  ckfree(info);
}

/*
 * Main thread event handler for accessibility tasks.
 * This is where Tcl_* calls should happen.
 */
static int MainThreadAccessibleProc(Tcl_Event *eventPtr, int flags) 
{
  TkAccessibleEvent *accEvent = (TkAccessibleEvent *)eventPtr;

  if (accEvent->type == ACCESSIBLE_DO_DEFAULT_ACTION) {
    DoDefaultActionInternal(accEvent->data.doDefaultAction.interp,
			    accEvent->data.doDefaultAction.win);
  } else if (accEvent->type == ACCESSIBLE_EMIT_FOCUS_CHANGE) {
    NotifyWinEvent(EVENT_OBJECT_FOCUS,
		   accEvent->data.emitFocusChange.hwnd, OBJID_CLIENT,
		   accEvent->data.emitFocusChange.childId);
  } else if (accEvent->type == ACCESSIBLE_EMIT_SELECTION_CHANGE) {
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE,
		   accEvent->data.emitSelectionChange.hwnd, OBJID_CLIENT, 0);
  }
  return 1;
}


/*
 * Functions to implement direct script-level
 * Tcl commands.
 */

/*
 *----------------------------------------------------------------------
 *
 * IsScreenReaderRunning --
 *
 * Runtime check to see if screen reader is running.
 *
 * Results:
 * Returns if screen reader is active or not.
 *
 * Side effects:
 * None.
 *
 *----------------------------------------------------------------------
 */

int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const argv[]) 
{
  /*
   * MSVC complains if this parameter is stubbed out
   * with TCL_UNUSED.
   */
  (void) clientData;

  BOOL screenReader = FALSE;
  SystemParametersInfo(SPI_GETSCREENREADER, 0, &screenReader, 0);
  Tcl_SetObjResult(interp, Tcl_NewBooleanObj(screenReader));
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * EmitSelectionChanged --
 *
 * Accessibility system notification when selection changed.
 *
 * Results:
 * Accessibility system is made aware when a selection is changed.
 *
 * Side effects:
 * None.
 *
 *----------------------------------------------------------------------
 */

static int EmitSelectionChanged(ClientData clientData, Tcl_Interp *ip, int objc, Tcl_Obj *const objv[]) 
{
  /*
   * MSVC complains if this parameter is stubbed out
   * with TCL_UNUSED.
   */
  (void) clientData;

  if (objc < 2) {
    Tcl_WrongNumArgs(ip, 1, objv, "window?");
    return TCL_ERROR;
  }
  Tk_Window path;

  path = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
  if (path == NULL) {
    Tcl_SetResult(ip, "Invalid window name.", TCL_STATIC);
    return TCL_ERROR;
  }
  Tk_MakeWindowExist(path); /* Ensure window exists for HWND. */

  /* Queue event to main thread. */
  TkAccessibleEvent *eventPtr = (TkAccessibleEvent *)
    Tcl_Alloc(sizeof(TkAccessibleEvent));
  eventPtr->event.proc = MainThreadAccessibleProc;
  eventPtr->type = ACCESSIBLE_EMIT_SELECTION_CHANGE;
  eventPtr->data.emitSelectionChange.win = path;
  eventPtr->data.emitSelectionChange.hwnd = Tk_GetHWND(Tk_WindowId(path));
  Tcl_ThreadQueueEvent(tkMainThreadId, (Tcl_Event *)eventPtr,
		       TCL_QUEUE_TAIL);

  return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * TkRootAccessible_RegisterForCleanup --
 *
 * Register event handler for destroying accessibility element.
 *
 * Results:
 * Event handler is registered.
 *
 * Side effects:
 * None.
 *
 *----------------------------------------------------------------------
 */

void TkRootAccessible_RegisterForCleanup(Tk_Window tkwin, void *tkAccessible) 
{
  Tk_CreateEventHandler(tkwin, StructureNotifyMask,
			TkRootAccessible_DestroyHandler, tkAccessible);
}

/*
 *----------------------------------------------------------------------
 *
 * TkRootAccessible_DestroyHandler --
 *
 * Clean up accessibility element structures when window is destroyed.
 *
 * Results:
 * Accessibility element is deallocated.
 *
 * Side effects:
 * None.
 *
 *----------------------------------------------------------------------
 */

static void TkRootAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr) 
{
  if (eventPtr->type == DestroyNotify) {
    TkRootAccessible *tkAccessible = (TkRootAccessible *)clientData;
    if (tkAccessible) {
      TkRootAccessible_Release((IAccessible *)tkAccessible);
    }
  }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWidgetStructureHandler --
 *
 * Rebuild the  childID table when a widget is created or destroyed. 
 *
 * Results:
 * Table rebuilt. 
 *
 * Side effects:
 * None.
 *
 *----------------------------------------------------------------------
 */
static void TkWidgetStructureHandler(ClientData clientData, XEvent *eventPtr) {
  TkRootAccessible *rootAccessible = (TkRootAccessible *)clientData;

  if (eventPtr->type == CreateNotify) {
    /* Assign a child ID to the new widget. */
    Window childWinId = eventPtr->xcreatewindow.window;
    Tk_Window root = rootAccessible->win;

    /* Walk children to find matching Tk window by ID. */
    TkWindow *rootPtr = (TkWindow *)root;
    for (TkWindow *child = rootPtr->childList; child != NULL; child = child->nextPtr) {
      if (Tk_WindowId((Tk_Window)child) == childWinId) {
        SetChildIdForTkWindow((Tk_Window)child, nextId++);
        break;
      }
    }
  } else if (eventPtr->type == DestroyNotify) {
    Window destroyedWinId = eventPtr->xdestroywindow.window;

    /* Look for matching Tk window and remove from childIdTable. */
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;
    for (entry = Tcl_FirstHashEntry(childIdTable, &search);
         entry != NULL;
         entry = Tcl_NextHashEntry(&search)) {
      Tk_Window win = (Tk_Window)Tcl_GetHashKey(childIdTable, entry);
      if (Tk_WindowId(win) == destroyedWinId) {
        Tcl_DeleteHashEntry(entry);
        break;
      }
    }
  }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWidgetFocusHandler --
 *
 * Notifies MSAA of focus updates during tab navigation.
 *
 * Results:
 * Focus notification.
 *
 * Side effects:
 * None.
 *
 *----------------------------------------------------------------------
 */
 
static void TkWidgetFocusHandler(ClientData clientData, XEvent *eventPtr) 
{
  if (eventPtr->type != FocusIn) return;

  Tk_Window win = (Tk_Window)clientData;
  Tk_Window toplevel = GetToplevelOfWidget(win);
  if (!toplevel) return;

  HWND hwnd = Tk_GetHWND(Tk_WindowId(toplevel));
  LONG childId = GetChildIdForTkWindow(win);
  if (childId < 0) childId = CHILDID_SELF;

  DeferredFocusInfo *info = (DeferredFocusInfo *)ckalloc(sizeof(DeferredFocusInfo));
  info->hwnd = hwnd;
  info->childId = childId;
  Tcl_DoWhenIdle(DeferredNotifyFocus, (ClientData)info);
}


/*
 *----------------------------------------------------------------------
 *
 * EmitFocusChanged --
 *
 * Accessibility system notification when focus changed.
 *
 * Results:
 * Accessibility system is made aware when focus is changed.
 *
 * Side effects:
 * None.
 *
 *----------------------------------------------------------------------
 */

static int EmitFocusChanged(ClientData cd, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) 
{
  if (objc < 2) {
    Tcl_WrongNumArgs(interp, 1, objv, "widgetPath");
    return TCL_ERROR;
  }

  const char *path = Tcl_GetString(objv[1]);
  Tk_Window win = Tk_NameToWindow(interp, path, Tk_MainWindow(interp));
  if (!win) return TCL_ERROR;

  Tk_MakeWindowExist(win); /* Ensure window exists for HWND. */
  Tk_Window toplevel = GetToplevelOfWidget(win);
  HWND hwnd = Tk_GetHWND(Tk_WindowId(toplevel));

  LONG childId = GetChildIdForTkWindow(win);
  if (childId < 0) childId = CHILDID_SELF;

  NotifyWinEvent(EVENT_OBJECT_FOCUS, hwnd, OBJID_CLIENT, childId);
  TkSetFocusWin((TkWindow *)win, 1);
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkRootAccessibleObjCmd --
 *
 * Main command for adding and managing accessibility objects to Tk
 * widgets on Windows using the Microsoft Active Accessibility API.
 *
 * Results:
 *
 * A standard Tcl result.
 *
 * Side effects:
 *
 * Tk widgets are now accessible to screen readers.
 *
 *----------------------------------------------------------------------
 */

int TkRootAccessibleObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) 
{
  (void) clientData;
  HWND hwnd = NULL;

  if (objc != 2) {
    Tcl_WrongNumArgs(interp, 1, objv, "window");
    return TCL_ERROR;
  }

  char *windowName = Tcl_GetString(objv[1]);
  Tk_Window tkwin = Tk_NameToWindow(interp, windowName,
				    Tk_MainWindow(interp));

  if (tkwin == NULL) {
    Tcl_SetResult(interp, "Invalid window name.", TCL_STATIC);
    return TCL_ERROR;
  }

  if (Tk_IsTopLevel(tkwin)) {
    hwnd = Tk_GetHWND(Tk_WindowId(tkwin));
  } else {
    /*
     * The accessible root is typically the toplevel window, but support
     * child widgets just in case.
     */
    Tk_Window toplevel = GetToplevelOfWidget(tkwin);
    hwnd = Tk_GetHWND(Tk_WindowId(toplevel));
  }
  
  /*Look up accessible in hash table. */
  if (!accObjectTableInitialized) {
    Tcl_SetResult(interp, "Accessibility not initialized", TCL_STATIC);
    return TCL_ERROR;
  }
  
  /* 
   * Guard against race condition for accessible object creation.
   *Only create new object if not already found in hash table. 
   */
  Tcl_HashEntry *entry = Tcl_FindHashEntry(accObjectTable, hwnd);
  if (!entry) {
    int newEntry;
    entry = Tcl_CreateHashEntry(accObjectTable, hwnd, &newEntry);
    Tcl_SetHashValue(entry, tkwin);
  }

  TkRootAccessible *accessible = Tcl_GetHashValue(entry);
  TkRootAccessible_RegisterForCleanup(tkwin, accessible);
  Tk_CreateEventHandler(tkwin, StructureNotifyMask,
			TkWidgetStructureHandler, accessible);			
  Tk_CreateEventHandler(tkwin, FocusChangeMask, TkWidgetFocusHandler, tkwin);
  AssignChildIdsRecursive(tkwin, interp); 

  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinAccessibility_Init --
 *
 * Initializes the accessibility module.
 *
 * Results:
 *
 * A standard Tcl result.
 *
 * Side effects:
 *
 * Accessibility module is now activated.
 *
 *----------------------------------------------------------------------
 */

int TkWinAccessiblity_Init(Tcl_Interp *interp)
{
  /* Initialize COM machinery. */
  CoInitialize(NULL);
  
  /* Store the thread ID of the main Tcl thread. */
  tkMainThreadId = Tcl_GetCurrentThread();

  /*Initialize object-tracking hash tables. */
  InitHwndToTkWindowTable();
  InitTkRootAccesibleTable();
  
  /*Create Tcl commands. */

  Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object",
		       TkRootAccessibleObjCmd, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change",
		       EmitSelectionChanged, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::emit_focus_change",
		       EmitFocusChanged, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader",
		       IsScreenReaderRunning, NULL, NULL);
  return TCL_OK;
}
