/*
 * tkWinAccessibility.c --
 *
 *	  This file implements the platform-native Microsoft Active 
 *	  Accessibility API for Tk on Windows.  
 *
 * Copyright (c) 2024-2025 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
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
  int focusChildId; 
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


/*
 *----------------------------------------------------------------------
 *
 * Glue functions to the IAccessible COM API - toplevels.  
 *
 *----------------------------------------------------------------------
 */


/*Empty stub functions required by MSAA. */
HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accHelpTopic( IAccessible *this, BSTR *pszHelpFile, VARIANT varChild, long *pidTopic)
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

HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accDefaultAction(IAccessible *this, VARIANT varChild, BSTR *pszDefaultAction)
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
  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IAccessible)) {
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
      return TkRootAccessible_get_accName(this, selfVar, &pVarResult->bstrVal);

    case DISPID_ACC_VALUE:
      return TkRootAccessible_get_accValue(this, selfVar, &pVarResult->bstrVal);

    case DISPID_ACC_ROLE:
      return TkRootAccessible_get_accRole(this, selfVar, pVarResult);

    case DISPID_ACC_STATE:
      return TkRootAccessible_get_accState(this, selfVar, pVarResult);

    case DISPID_ACC_DESCRIPTION:
      return TkRootAccessible_get_accDescription(this, selfVar, &pVarResult->bstrVal);

    case DISPID_ACC_HELP:
      return TkRootAccessible_get_accHelp(this, selfVar, &pVarResult->bstrVal);
      
    case DISPID_ACC_DODEFAULTACTION:
      return TkRootAccessible_accDoDefaultAction(this, selfVar);

    case DISPID_ACC_FOCUS:
      return TkRootAccessible_get_accFocus(this, &selfVar);   
	
    }
  }
  return S_OK;
}

/* Function to map accessible name to MSAA.*/
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accName(IAccessible *this, VARIANT varChild, BSTR *pszName)
{
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;

  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
		
    Tk_Window win = tkAccessible->win;
    char str1[500] = "Toplevel ";
    strcat(str1, tkAccessible->pathName);
    Tcl_DString ds;
    Tcl_DStringInit(&ds);
    SysAllocString(Tcl_UtfToWCharDString(str1, -1, &ds));
    return S_OK;
  }
  /* Name for children are handled by their own IAccessible (or via parent for VT_I4 children) .*/
  return DISP_E_MEMBERNOTFOUND;
}

/* Function to map accessible role to MSAA. For toplevels, return ROLE_SYSTEM_WINDOW.*/
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accRole(IAccessible *this, VARIANT varChild, VARIANT *pvarRole)
{
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
  LONG role = 0;
  if (!pvarRole) return E_INVALIDARG;
  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    role = ROLE_SYSTEM_WINDOW;
    pvarRole->vt = VT_I4;
    pvarRole->lVal = role;
    return S_OK;
  }
  return E_INVALIDARG;
}

/* Function to map accessible state to MSAA. For toplevel, return STATE_SYSTEM_NORMAL. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accState(IAccessible *this, VARIANT varChild, VARIANT *pvarState)
{
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
  LONG state = 0;
  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    pvarState->vt = VT_I4;
    pvarState->lVal = STATE_SYSTEM_FOCUSABLE;
    return S_OK;
  }
  return DISP_E_MEMBERNOTFOUND;
}

/* Function to map accessible value to MSAA. For toplevel, return NULL.*/
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accValue(IAccessible *this, VARIANT varChild, BSTR *pszValue)
{
  *pszValue = NULL;
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
  TkWindow *winPtr = (TkWindow *)Tk_MainWindow(tkAccessible->interp);
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
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    int count = 0;
    TkWindow *child;
    TkWindow *winPtr = (TkWindow* )Tk_MainWindow(tkAccessible->interp);
		
    for (child = winPtr->childList; child != NULL; child = child->nextPtr)  {
      if (Tk_IsMapped(child)) {
	if (count + 1 == varChild.lVal) {
	  Tk_Window childwin = ((Tk_Window) child);
	  HWND hwnd = Tk_GetHWND(Tk_WindowId(tkAccessible->win));
	  TkChildAccessible *childAccessible = CreateChildAccessible(tkAccessible->interp, hwnd, Tk_PathName(childwin));
	  if (childAccessible) {
	    *ppdispChild = (IDispatch *)childAccessible;
	    return S_OK;
	  }
	}
	count++;
      }
    }
  }
  *ppdispChild = NULL;
  return E_INVALIDARG;
}

/* Function to get accessible frame to MSAA. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_accLocation(IAccessible *this, LONG *pxLeft, LONG *pyTop, LONG *pcxWidth, LONG *pcyHeight,VARIANT varChild)
{
  if (!pxLeft || !pyTop || !pcxWidth || !pcyHeight)
    return E_INVALIDARG;

  if (varChild.vt != VT_I4 || varChild.lVal != CHILDID_SELF)
    return E_INVALIDARG;

  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;

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

/*Function to set accessible focus on Tk widget. For toplevel, just return. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_accSelect(IAccessible *thisPtr, long flags, VARIANT varChild) 
{
  return E_NOTIMPL;
}


/* Function to get button press to MSAA. For toplevel, just return. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_accDoDefaultAction(IAccessible *this, VARIANT varChild)
{
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;  
  return S_OK;  
}


/* Function to get accessible help to MSAA. For toplevel, just return. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accHelp(IAccessible *this, VARIANT varChild, BSTR* pszHelp)
{
  return E_NOTIMPL;
}

/* Function to get accessible help to MSAA. For toplevel, just return. */
static STDMETHODIMP TkRootAccessible_get_accFocus(IAccessible *this, VARIANT *pvarChild)
{

  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;  
 
  if (!pvarChild) return E_INVALIDARG;
  VariantInit(pvarChild);

  if (tkAccessible->focusChildId > 0) {
    pvarChild->vt = VT_I4;
    pvarChild->lVal = tkAccessible->focusChildId;
    return S_OK;
  }

  if (tkAccessible->focusChildId == -1) {
    pvarChild->vt = VT_I4;
    pvarChild->lVal = CHILDID_SELF;
    return S_OK;
  }

  return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accDescription(IAccessible *this, VARIANT varChild, BSTR *pszDescription)
{
  TkRootAccessible *tkAccessible = (TkRootAccessible *)this;  
  Tk_Window win = tkAccessible->win;
  
  const char *pathName = Tk_PathName(win);
  Tcl_Obj *cmd = Tcl_NewObj();
  Tcl_AppendToObj(cmd, "wm title ", -1);
  Tcl_AppendToObj(cmd, pathName, -1);
	
  Tcl_EvalObjEx(tkAccessible->interp, cmd, TCL_EVAL_GLOBAL);
  char *result = Tcl_GetString(Tcl_GetObjResult(tkAccessible->interp));
  Tcl_DString ds;
  Tcl_DStringInit(&ds);
		
  *pszDescription = SysAllocString(Tcl_UtfToWCharDString(result, -1, &ds));
  Tcl_DStringFree(&ds);
  return S_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Glue functions to the IAccessible COM API - child widgets.  
 *
 *----------------------------------------------------------------------
 */


/*Empty stub functions required by MSAA. */
HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accHelpTopic( IAccessible *this, BSTR *pszHelpFile, VARIANT varChild, long *pidTopic)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accKeyboardShortcut(IAccessible *this, VARIANT varChild, BSTR *pszKeyboardShortcut)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accSelection(IAccessible *this, VARIANT *pvarChildren)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accDefaultAction(IAccessible *this, VARIANT varChild, BSTR *pszDefaultAction)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkChildAccessible_accNavigate(IAccessible *this, long navDir, VARIANT varStart, VARIANT *pvarEndUpAt)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkChildAccessible_accHitTest(IAccessible *this, long xLeft, long yTop, VARIANT *pvarChild)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkChildAccessible_put_accName(IAccessible *this, VARIANT varChild, BSTR szName)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkChildAccessible_put_accValue(IAccessible *this, VARIANT varChild, BSTR szValue)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkChildAccessible_accSelect(IAccessible *thisPtr, long flags, VARIANT varChild) 
{
  return E_NOTIMPL;
}


/*Begin active functions.*/

static HRESULT STDMETHODCALLTYPE TkChildAccessible_QueryInterface(IAccessible *this, REFIID riid, void **ppvObject)
{
  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IAccessible)) {
    *ppvObject = this;
    TkChildAccessible_AddRef(this);
    return S_OK;
  }
  *ppvObject = NULL;
  return E_NOINTERFACE;
}

/* Function to add memory reference to the MSAA object. */
static ULONG STDMETHODCALLTYPE TkChildAccessible_AddRef(IAccessible *this)
{
  TkChildAccessible *tkAccessible = (TkChildAccessible *)this;
  return InterlockedIncrement(&tkAccessible->refCount);
}

/* Function to free the MSAA object. */
static ULONG STDMETHODCALLTYPE TkChildAccessible_Release(IAccessible *this)
{
  TkChildAccessible *tkAccessible = (TkChildAccessible *)this;
  ULONG count = InterlockedDecrement(&tkAccessible->refCount);
  if (count == 0) {
    ckfree(tkAccessible);
  }
  return count;
}

static HRESULT STDMETHODCALLTYPE TkChildAccessible_GetTypeInfoCount(IAccessible *this, UINT *pctinfo)
{
  *pctinfo = 0;
  return S_OK;
}

static HRESULT STDMETHODCALLTYPE TkChildAccessible_GetTypeInfo(IAccessible *this, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkChildAccessible_GetIDsOfNames(IAccessible *this, REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
  ITypeInfo *pTypeInfo = NULL;
  HRESULT hr;

  hr = TkChildAccessible_GetTypeInfo(this, 0, lcid, &pTypeInfo);
  if (FAILED(hr)) {
    return hr;
  }

  hr = DispGetIDsOfNames(pTypeInfo, rgszNames, cNames, rgDispId);
  pTypeInfo->lpVtbl->Release(pTypeInfo);

  return hr;
}

static HRESULT STDMETHODCALLTYPE TkChildAccessible_Invoke(IAccessible *this, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
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
      return TkChildAccessible_get_accName(this, selfVar, &pVarResult->bstrVal);

    case DISPID_ACC_VALUE:
      return TkChildAccessible_get_accValue(this, selfVar, &pVarResult->bstrVal);

    case DISPID_ACC_ROLE:
      return TkChildAccessible_get_accRole(this, selfVar, pVarResult);

    case DISPID_ACC_STATE:
      return TkChildAccessible_get_accState(this, selfVar, pVarResult);

    case DISPID_ACC_DESCRIPTION:
      return TkChildAccessible_get_accDescription(this, selfVar, &pVarResult->bstrVal);

    case DISPID_ACC_HELP:
      return TkChildAccessible_get_accHelp(this, selfVar, &pVarResult->bstrVal);
      
    case DISPID_ACC_DODEFAULTACTION:
      return TkChildAccessible_accDoDefaultAction(this, selfVar);

    case DISPID_ACC_FOCUS:
      return TkChildAccessible_get_accFocus(this, &selfVar);   
	
    }
  }
  return S_OK;
}

/* Function to map accessible name to MSAA.*/
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accName(IAccessible *this, VARIANT varChild, BSTR *pszName)
{
  TkChildAccessible *tkAccessible = (TkChildAccessible *)this;

  if (varChild.vt != VT_I4 || varChild.lVal == CHILDID_SELF) {
		
    Tk_Window win = tkAccessible->win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
	
    Tcl_DString ds;
    Tcl_DStringInit(&ds);
	
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
      return E_INVALIDARG;
    }
	
    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "name");
    if (!hPtr2) {
      SysAllocString(Tcl_UtfToWCharDString(tkAccessible->pathName, -1, &ds));
      return S_OK;
    }
	
    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    if (result) {
      *pszName = SysAllocString(Tcl_UtfToWCharDString(result, -1, &ds));
    }
    Tcl_DStringFree(&ds);
    return S_OK;
  }
  return E_INVALIDARG;
}

/* Function to map accessible role to MSAA.*/
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accRole(IAccessible *this, VARIANT varChild, VARIANT *pvarRole)
{
  TkChildAccessible *tkAccessible = (TkChildAccessible *)this;
  LONG role;

  if (!pvarRole) return E_INVALIDARG;

  if (varChild.vt != VT_I4 || varChild.lVal == CHILDID_SELF) {
    Tk_Window win = tkAccessible->win;
		
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
		
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
      return E_INVALIDARG;
    }
	
    Tcl_DString ds;
    Tcl_DStringInit(&ds);
		
    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "role");
    if (!hPtr2) {
      SysAllocString(Tcl_UtfToWCharDString(tkAccessible->pathName, -1, &ds));
      return S_OK;
    }

    char *tkrole = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    role = ROLE_SYSTEM_CLIENT; /* Default if no match. */
    for (int i = 0; roleMap[i].tkrole !=NULL; i++) {
      if (strcmp(roleMap[i].tkrole, tkrole) == 0) {
	role = roleMap[i].winrole;
	break;
      }
    }

    pvarRole->vt = VT_I4;
    pvarRole->lVal = role;
    return S_OK;
  }
  return E_INVALIDARG;
}

/* Function to map accessible state to MSAA. */
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accState(IAccessible *this, VARIANT varChild, VARIANT *pvarState)
{
  TkChildAccessible *tkAccessible = (TkChildAccessible *)this;
  LONG state;
	
  if (varChild.vt != VT_I4 || varChild.lVal == CHILDID_SELF) {
    /* Check if Tk state is disabled. If so, ignore accessible atribute. */
    Tk_Window win = tkAccessible->win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
		
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
      return E_INVALIDARG;
    }
		
    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "state");
    if (!hPtr2) {
      return E_INVALIDARG;
    }
		
    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    if (strcmp(result, "disabled") == 0) {
      state |= STATE_SYSTEM_UNAVAILABLE;
    } else {
      state |= STATE_SYSTEM_FOCUSABLE;
    }		
    pvarState->vt = VT_I4;
    pvarState->lVal = state;
    return S_OK;
  }
  return E_INVALIDARG;
}

/* Function to map accessible value to MSAA.*/
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accValue(IAccessible *this, VARIANT varChild, BSTR *pszValue)
{
  if (!pszValue) return E_INVALIDARG;
	
  TkChildAccessible *tkAccessible = (TkChildAccessible *)this;
  if (varChild.vt != VT_I4 || varChild.lVal == CHILDID_SELF) {
		
    Tk_Window win = tkAccessible->win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
	
    Tcl_DString ds;
    Tcl_DStringInit(&ds);
		
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
      return E_INVALIDARG;
    }
		
    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "value");
    if (!hPtr2) {
      SysAllocString(Tcl_UtfToWCharDString(tkAccessible->pathName, -1, &ds));
      return S_OK;
    }
		
    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    if (result) {
      *pszValue = SysAllocString(Tcl_UtfToWCharDString(result, -1, &ds));
    }
    Tcl_DStringFree(&ds);
    return S_OK;
  }
  return E_INVALIDARG;
}

/*
 * Function to set Tk toplevel as the primary parent of acccessible 
 * widgets. We want a flat hierarchy to simplify implementation. 
 */
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accParent(IAccessible *this, IDispatch **ppdispParent) 
{
  /* Validate ppdispParent to avoid dereferencing a NULL pointer. */
  if (!ppdispParent) return E_INVALIDARG;

  /* Cast this to TkChildAccessible, assuming 'this' is of that type. */
  TkChildAccessible *tkAccessible = (TkChildAccessible *)this;

  /* Get the top-level Tk window. */
  Tk_Window toplevel = GetToplevelOfWidget(tkAccessible->win);
  HWND hwndTopLevel = Tk_GetHWND(Tk_WindowId(toplevel));

  if (!hwndTopLevel) {
    /* If no top-level window exists, set ppdispParent to NULL and return S_OK. */
    *ppdispParent = NULL;
    return S_OK;
  }

  /* Create an accessible object for the top-level window. */
  TkRootAccessible *topLevelAccessible = CreateRootAccessible(tkAccessible->interp, hwndTopLevel, Tk_PathName(toplevel));
  if (!topLevelAccessible) {
    /* If no accessible object is created, set ppdispParent to NULL and return S_OK. */
    *ppdispParent = NULL;
    return S_OK;
  }

  /* Set ppdispParent to the new accessible object. */
  *ppdispParent = (IDispatch *)topLevelAccessible;

  return S_OK;
}

/* Function to get number of accessible children to MSAA. For child widgets, return 0.*/
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accChildCount(IAccessible *this, LONG *pcChildren)
{
  TkChildAccessible *tkAccessible = (TkChildAccessible *)this;
  *pcChildren = 0;
  return S_OK;
}

/* Function to get accessible children to MSAA. For child widgets, return NULL. */
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accChild(IAccessible *this, VARIANT varChild, IDispatch **ppdispChild)
{
  TkChildAccessible *tkAccessible = (TkChildAccessible *)this;
  *ppdispChild = NULL;
  return DISP_E_MEMBERNOTFOUND;
}

/* Function to get accessible frame to MSAA. */
static HRESULT STDMETHODCALLTYPE TkChildAccessible_accLocation(IAccessible *this, LONG *pxLeft, LONG *pyTop, LONG *pcxWidth, LONG *pcyHeight, VARIANT varChild)
{
  if (!pxLeft || !pyTop || !pcxWidth || !pcyHeight)
    return E_INVALIDARG;

  if (varChild.vt != VT_I4 || varChild.lVal != CHILDID_SELF)
    return E_INVALIDARG;

  TkChildAccessible *tkAccessible = (TkChildAccessible *)this;
  Tk_Window win = tkAccessible->win;

  int rootX = 0, rootY = 0;
  Tk_GetRootCoords(win, &rootX, &rootY);

  *pxLeft = rootX;
  *pyTop = rootY;
  *pcxWidth = Tk_Width(win);
  *pcyHeight = Tk_Height(win);

  return S_OK;
}




/* Function to get button press to MSAA. */
static HRESULT STDMETHODCALLTYPE TkChildAccessible_accDoDefaultAction(IAccessible *this, VARIANT varChild)
{
  TkChildAccessible *tkAccessible = (TkChildAccessible *)this;  
  Tk_Window win = tkAccessible->win;
  Tcl_HashEntry *hPtr, *hPtr2;
  Tcl_HashTable *AccessibleAttributes;

  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    return E_INVALIDARG;
  }

  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "action");
  if (!hPtr2) {
    return E_INVALIDARG;
  }

  char *action= Tcl_GetString(Tcl_GetHashValue(hPtr2));
  if (Tcl_Eval(tkAccessible->interp, action) != TCL_OK) {
    return S_FALSE;
  }
  return S_OK;  
}


/* Function to get accessible help to MSAA. */
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accHelp(IAccessible *this, VARIANT varChild, BSTR* pszHelp)
{
  if (varChild.vt != VT_I4 || varChild.lVal != CHILDID_SELF) {
    return E_INVALIDARG;
  }

  TkChildAccessible *tkAccessible = (TkChildAccessible *)this;  
  Tk_Window win = tkAccessible->win;
  Tcl_HashEntry *hPtr, *hPtr2;
  Tcl_HashTable *AccessibleAttributes;
  
  Tcl_DString ds;
  Tcl_DStringInit(&ds);
		
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    return E_INVALIDARG;
  }
		
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "help");
  if (!hPtr2) {
    SysAllocString(Tcl_UtfToWCharDString(tkAccessible->pathName, -1, &ds));
    return S_OK;
  }
	
  char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  *pszHelp = SysAllocString(Tcl_UtfToWCharDString(result, -1, &ds));
  if (!*pszHelp) {
    return E_OUTOFMEMORY;
  }
  Tcl_DStringFree(&ds);
  return S_OK;
}

/* Function to get MSAA focus. For child widgets, just return self. */
static STDMETHODIMP TkChildAccessible_get_accFocus(IAccessible *this, VARIANT *pvarChild)
{
  if (!pvarChild) return E_INVALIDARG;
  VariantInit(pvarChild);

  pvarChild->vt = VT_I4;
  pvarChild->lVal = CHILDID_SELF;
  return S_OK;
}

/* Function to get MSAA description. */
static HRESULT STDMETHODCALLTYPE TkChildAccessible_get_accDescription(IAccessible *this, VARIANT varChild, BSTR *pszDescription)
{
  TkChildAccessible *tkAccessible = (TkChildAccessible *)this;  
  Tk_Window win = tkAccessible->win;
  Tcl_HashEntry *hPtr, *hPtr2;
  Tcl_HashTable *AccessibleAttributes;
  
  Tcl_DString ds;
  Tcl_DStringInit(&ds);
		
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    return E_INVALIDARG;
  }
		
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "description");
  if (!hPtr2) {
    SysAllocString(Tcl_UtfToWCharDString(tkAccessible->pathName, -1, &ds));
    return S_OK;
  }
	
  char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  *pszDescription = SysAllocString(Tcl_UtfToWCharDString(result, -1, &ds));

  if (!*pszDescription) {
    return E_OUTOFMEMORY;
  }
  
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
static TkRootAccessible *CreateRootAccessible(Tcl_Interp *interp, HWND hwnd, const char *pathName)
{
  TkRootAccessible *tkAccessible = (TkRootAccessible *)ckalloc(sizeof(TkRootAccessible));
  Tk_Window win = Tk_NameToWindow(interp, pathName, Tk_MainWindow(interp));
  if (tkAccessible) {
    tkAccessible->lpVtbl = &tkRootAccessibleVtbl;
    tkAccessible->interp = interp;
    tkAccessible->hwnd = hwnd;
    tkAccessible->pathName = pathName;
    tkAccessible->refCount = 1;
    tkAccessible->win = win;
  }
  
  /*Add objects to hash tables. */
  Tcl_HashEntry *entry;
  int newEntry;
  entry = Tcl_CreateHashEntry(tkAccessibleTable, win, &newEntry);
  Tcl_SetHashValue(entry, tkAccessible);
  
  entry = Tcl_CreateHashEntry(hwndToTkWindowTable, hwnd, &newEntry);
  Tcl_SetHashValue(entry, win);
  
  TkRootAccessible_AddRef((IAccessible*)tkAccessible); 

  /* Notify screen readers of creation. */
  NotifyWinEvent(EVENT_OBJECT_CREATE, hwnd, OBJID_CLIENT, CHILDID_SELF);
  NotifyWinEvent(EVENT_OBJECT_SHOW, hwnd, OBJID_CLIENT, CHILDID_SELF);
  NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, hwnd, OBJID_CLIENT, CHILDID_SELF);
  
  return tkAccessible;
}

/* Function to map Tk window to MSAA attributes. */
static TkChildAccessible *CreateChildAccessible(Tcl_Interp *interp, HWND parenthwnd, const char *pathName)
{
  TkChildAccessible *tkAccessible = (TkChildAccessible *)ckalloc(sizeof(TkChildAccessible));
  Tk_Window win = Tk_NameToWindow(interp, pathName, Tk_MainWindow(interp));
  parenthwnd = Tk_GetHWND(Tk_WindowId(Tk_Parent(win)));   
  if (tkAccessible) {
    tkAccessible->lpVtbl = &tkChildAccessibleVtbl;
    tkAccessible->interp = interp;
    tkAccessible->parenthwnd = parenthwnd;
    tkAccessible->pathName = pathName;
    tkAccessible->refCount = 1;
    tkAccessible->win = win;
  }
  
  /*Add objects to hash tables. */
  Tcl_HashEntry *entry;
  int newEntry;
  entry = Tcl_CreateHashEntry(tkAccessibleTable, win, &newEntry);
  Tcl_SetHashValue(entry, tkAccessible);
  
  entry = Tcl_CreateHashEntry(hwndToTkWindowTable, tkAccessible->parenthwnd, &newEntry);
  Tcl_SetHashValue(entry, win);
  
  TkRootAccessible_AddRef((IAccessible*)tkAccessible);  
  
  /* Notify screen readers of creation. */
  NotifyWinEvent(EVENT_OBJECT_CREATE, tkAccessible->parenthwnd, OBJID_CLIENT, CHILDID_SELF);
  NotifyWinEvent(EVENT_OBJECT_SHOW, tkAccessible->parenthwnd, OBJID_CLIENT, CHILDID_SELF);
  NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, tkAccessible->parenthwnd, OBJID_CLIENT, CHILDID_SELF);
  
  
  return tkAccessible;
}

/* Function to map Tk window to MSAA ID's. */
LONG SetChildIDForTkWindow(Tk_Window tkwin) 
{
  if (widgetMapCount >= 512) return -1;

  /* Is it already registered? */
  for (int i = 0; i < widgetMapCount; ++i) {
    if (widgetMap[i].tkwin == tkwin) {
      return widgetMap[i].childId;
    }
  }

  LONG childId = nextChildId++;

  widgetMap[widgetMapCount].tkwin = tkwin;
  widgetMap[widgetMapCount].childId = childId;
  widgetMapCount++;

  return childId;
}

/* Function to retrieve MSAA ID for a specifc Tk window. */
LONG GetChildIdForTkWindow(Tk_Window tkwin) 
{
  for (int i = 0; i < widgetMapCount; ++i) {
    if (widgetMap[i].tkwin == tkwin) {
      return widgetMap[i].childId;
    }
  }
  return -1; /* Not found. */
}

/* Function to retrieve Tk window for a specifc MSAA ID. */
Tk_Window GetTkWindowForChildId(LONG childId) 
{
  for (int i = 0; i < widgetMapCount; ++i) {
    if (widgetMap[i].childId == childId) {
      return widgetMap[i].tkwin;
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

/* Function to initialize Tk -> MSAA hash table. */
void InitTkAccessibleTable(void) {
  if (!tkAccessibleTableInitialized) {
    tkAccessibleTable = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(tkAccessibleTable, TCL_ONE_WORD_KEYS);
    tkAccessibleTableInitialized = 1;
  }
}

/* Function to initialize HWND -> Tk hash table. */
void InitHwndToTkWindowTable(void) {
  if (!hwndToTkWindowTableInitialized) {
    hwndToTkWindowTable = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(hwndToTkWindowTable, TCL_ONE_WORD_KEYS);
    hwndToTkWindowTableInitialized = 1;
  }
}
/* Function to retrieve accessible object associated with Tk window. */
TkRootAccessible *GetTkAccessibleForWindow(Tk_Window win) {
  if (!tkAccessibleTableInitialized) {
    return NULL;
  }
  Tcl_HashEntry *entry = Tcl_FindHashEntry(tkAccessibleTable, (ClientData)win);
  if (entry) {
    return (TkRootAccessible *)Tcl_GetHashValue(entry);
  }
  return NULL;
}

/* Function to retrieve Tk window associated with HWND. */
Tk_Window GetTkWindowForHwnd(HWND hwnd) {
  if (!hwndToTkWindowTableInitialized) {
    return NULL;
  }
  Tcl_HashEntry *entry = Tcl_FindHashEntry(hwndToTkWindowTable, (ClientData)hwnd);
  if (entry) {
    return (Tk_Window)Tcl_GetHashValue(entry);
  }
  return NULL;
}

/* Functions to implement direct script-level Tcl commands. */


/*
 *----------------------------------------------------------------------
 *
 * IsScreenReaderRunning --
 *
 * Runtime check to see if screen reader is running. 
 *
 * Results:
 *	Returns if screen reader is active or not. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int IsScreenReaderRunning(
			  ClientData clientData,
			  Tcl_Interp *interp, /* Current interpreter. */
			  int argc, /* Number of arguments. */
			  Tcl_Obj *const argv[]) /* Argument objects. */
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
 *	Accessibility system is made aware when a selection is changed. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
EmitSelectionChanged(
		     ClientData clientData,
		     Tcl_Interp *ip,		/* Current interpreter. */
		     int objc,			/* Number of arguments. */
		     Tcl_Obj *const objv[])	/* Argument objects. */
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
    Tk_MakeWindowExist(path);
  }
	
  /* Fire notification of new value. */
  HWND hwnd = Tk_GetHWND(Tk_WindowId(path));   
  NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, hwnd, OBJID_CLIENT, 0);
	
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
 *      Event handler is registered.
 *
 * Side effects:
 *	None.
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
 *	Accessibility element is deallocated. 
 *
 * Side effects:
 *	None.
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
 * TkRootAccessible_FocusHandler --
 *
 * Force accessibility focus when Tk receives a FocusIn event.
 *
 * Results:
 *	Accessibility element is deallocated. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void TkRootAccessible_FocusEventHandler(ClientData clientData, XEvent *eventPtr) {
  if (!eventPtr || eventPtr->type != FocusIn) return;

  TkRootAccessible *tkAccessible = (TkRootAccessible *)clientData;
  Tk_Window tkwin = tkAccessible->win;

  /* Get the top-level window and HWND. */
  Tk_Window parent = GetToplevelOfWidget(tkwin);
  HWND hwnd = Tk_GetHWND(Tk_WindowId(parent));
  if (!hwnd) return;

  /* Determine child ID to report. */
  LONG childId = GetChildIdForTkWindow(tkwin);

  if (childId > 0) {
    /* Store focused child ID for accFocus support. */
    tkAccessible->focusChildId = childId;

    /* Fire the accessibility focus event. */
    NotifyWinEvent(EVENT_OBJECT_FOCUS, hwnd, OBJID_CLIENT, childId);
  } else if (childId == -1) {
    /* Fallback: widget is itself focusable. */
    tkAccessible->focusChildId = -1;
    NotifyWinEvent(EVENT_OBJECT_FOCUS, hwnd, OBJID_CLIENT, CHILDID_SELF);
  } else {
    /* No focus target; clear .*/
    tkAccessible->focusChildId = 0;
  }
}

/*
 *----------------------------------------------------------------------
 *
 * TkRootAccessible_RegisterForFocus --
 *
 * Register event handler for destroying accessibility element.
 *
 * Results:
 *      Event handler is registered.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void TkRootAccessible_RegisterForFocus(Tk_Window tkwin, void *tkAccessible)
{
  Tk_CreateEventHandler(tkwin, FocusChangeMask, 
			TkRootAccessible_FocusEventHandler, tkAccessible);
}


/*
 *----------------------------------------------------------------------
 *
 * TkRootAccessibleObjCmd --
 *
 *	Main command for adding and managing accessibility objects to Tk
 *      widgets on Windows using the Microsoft Active Accessibility API.
 *
 * Results:
 *
 *      A standard Tcl result.
 *
 * Side effects:
 *
 *	Tk widgets are now accessible to screen readers.
 *
 *----------------------------------------------------------------------
 */


int TkRootAccessibleObjCmd(
			   ClientData clientData,
			   Tcl_Interp *interp, /* Current interpreter. */
			   int objc, /* Number of arguments. */
			   Tcl_Obj *const objv[]) /* Argument objects. */
{
  /*
   * MSVC complains if this parameter is stubbed out
   * with TCL_UNUSED.
   */
  (void) clientData;
  HWND hwnd = NULL;

  if (objc != 2) {
    Tcl_WrongNumArgs(interp, 1, objv, "window");
    return TCL_ERROR;
  }

  char *windowName = Tcl_GetString(objv[1]);
  Tk_Window tkwin = Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));

  if (tkwin == NULL) {
    Tcl_SetResult(interp, "Invalid window name.", TCL_STATIC);
    return TCL_ERROR;
  }
  
  /* 
   * The root/toplevel accessible is created with this command. 
   * Child accessibles are created dynamically when the associated 
   * Tk child widget gets focus. 
   */

  if (Tk_IsTopLevel(tkwin)) { 
    hwnd = Tk_GetHWND(Tk_WindowId(tkwin)); 
  } else {
    Tk_Window toplevel = GetToplevelOfWidget(tkwin);
    hwnd = Tk_GetHWND(Tk_WindowId(toplevel)); 
  }
  TkRootAccessible *accessible = CreateRootAccessible(interp, hwnd, windowName);


  // accessible->win = tkwin;
  TkRootAccessible_RegisterForCleanup(tkwin, accessible);
  TkRootAccessible_RegisterForFocus(tkwin, accessible);
 

	
  if (accessible == NULL) {		
    Tcl_SetResult(interp, "Failed to create accessible object.", TCL_STATIC);
    return TCL_ERROR;
  }
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinAccessibility_Init --
 *
 *	Initializes the accessibility module.
 *
 * Results:
 *
 *      A standard Tcl result.
 *
 * Side effects:
 *
 *	Accessibility module is now activated.
 *
 *----------------------------------------------------------------------
 */

int TkWinAccessiblity_Init(Tcl_Interp *interp)
{	

  /* Initialize COM machinery. */
  CoInitialize(NULL);

  /*Initialize object-tracking hash tables. */
  InitTkAccessibleTable();
  InitHwndToTkWindowTable();
  
  /*Create Tcl commands. */
  
  Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", TkRootAccessibleObjCmd, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", EmitSelectionChanged, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader", IsScreenReaderRunning, NULL, NULL);
  return TCL_OK;
}

