/*
 * tkWinAccessibility.c --
 *
 *	  This file implements the platform-native Microsoft Active 
 *	  Accessibility API for Tk on Windows.  
 *
 * Copyright (C) 2024-2025 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

#include "tkWinAccessibility.h"

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

/* Tcl command passed to event procedure. */
char *callback_command;

/* Focused accessible widget object. */
static LONG g_focusedChildId = 0;

/* Map Tk windows to MSAA ID's. */
typedef struct {
  Tk_Window tkwin;
  LONG childId;
} WidgetMapEntry;

static WidgetMapEntry widgetMap[512];
static int widgetMapCount = 0;
static LONG nextChildId = 1;

/* Protoypes of glue functions to the IAccessible COM API. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_QueryInterface(IAccessible *this, REFIID riid, void **ppvObject);
static ULONG STDMETHODCALLTYPE TkWinAccessible_AddRef(IAccessible *this);
static ULONG STDMETHODCALLTYPE TkWinAccessible_Release(IAccessible *this);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_GetTypeInfoCount(IAccessible *this, UINT *pctinfo);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_GetTypeInfo(IAccessible *this, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_GetIDsOfNames(IAccessible *this, REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_Invoke(IAccessible *this, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr);

/* Prototypes of empty stub functions required by MSAA. */
HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accHelpTopic(IAccessible *this, BSTR *pszHelpFile, VARIANT varChild, long *pidTopic);
HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accKeyboardShortcut(IAccessible *this, VARIANT varChild, BSTR *pszKeyboardShortcut);
HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accSelection(IAccessible *this, VARIANT *pvarChildren);
HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accDefaultAction(IAccessible *this, VARIANT varChild, BSTR *pszDefaultAction);
HRESULT STDMETHODCALLTYPE TkWinAccessible_accNavigate(IAccessible *this, long navDir, VARIANT varStart, VARIANT *pvarEndUpAt);
HRESULT STDMETHODCALLTYPE TkWinAccessible_accHitTest(IAccessible *this, long xLeft, long yTop, VARIANT *pvarChild);
HRESULT STDMETHODCALLTYPE TkWinAccessible_put_accName( IAccessible *this, VARIANT varChild, BSTR szName);
HRESULT STDMETHODCALLTYPE TkWinAccessible_put_accValue(IAccessible *this, VARIANT varChild, BSTR szValue);

/* Prototypes of the MSAA functions that actually implement accessibility for Tk widgets on Windows. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accName(IAccessible *this, VARIANT varChild, BSTR *pszName);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accRole(IAccessible *this, VARIANT varChild, VARIANT *pvarRole);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accState(IAccessible *this, VARIANT varChild, VARIANT *pvarState);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accValue(IAccessible *this, VARIANT varChild, BSTR *pszValue);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accParent(IAccessible *this, IDispatch **ppdispParent);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accChildCount(IAccessible *this, LONG *pcChildren);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accChild(IAccessible *this, VARIANT varChild, IDispatch **ppdispChild);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accLocation(IAccessible *this, LONG *pxLeft, LONG *pyTop, LONG *pcxWidth, LONG *pcyHeight, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accSelect(IAccessible *this, long flags, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accDoDefaultAction(IAccessible *this, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accHelp(IAccessible *this, VARIANT varChild, BSTR* pszHelp);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accDescription(IAccessible *this, VARIANT varChild, BSTR *pszDescription);
static HRESULT STDMETHODCALLTYPE  TkWinAccessible_get_accFocus(IAccessible *this, VARIANT *pvarChild);

/* Prototypes of Tk functions that support MSAA integration and implement the script-level API. */
void InitTkAccessibleTable(void);
void InitHwndToTkWindowTable(void);
TkWinAccessible *GetTkAccessibleForWindow(Tk_Window win);
Tk_Window GetTkWindowForHwnd(HWND hwnd);
static int TkWinAccessible_ActionEventHandler(Tcl_Event *event, int flags);
static TkWinAccessible *CreateTkAccessible(Tcl_Interp *interp, HWND hwnd, const char *pathName);
void ForceTkWidgetFocus(HWND hwnd, LONG childId);
LONG RegisterTkWidget(Tk_Window tkwin);
Tk_Window GetToplevelOfWidget(Tk_Window tkwin);
Tk_Window GetTkWindowForChildId(LONG childId);
int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const argv[]);
int EmitSelectionChanged(ClientData clientData,Tcl_Interp *ip, int objc, Tcl_Obj *const objv[]);
void TkWinAccessible_RegisterForCleanup(Tk_Window tkwin, void *tkAccessible);
static void TkWinAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr);
void TkWinAccessible_RegisterForFocus(Tk_Window tkwin, void *tkAccessible);
static void TkWinAccessible_FocusEventHandler (ClientData clientData, XEvent *eventPtr);
int TkWinAccessibleObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int TkWinAccessiblity_Init(Tcl_Interp *interp);

/* Plumbing to the COM/MSAA machinery. */
static IAccessibleVtbl tkAccessibleVtbl = {
  TkWinAccessible_QueryInterface,
  TkWinAccessible_AddRef,
  TkWinAccessible_Release,
  TkWinAccessible_GetTypeInfoCount,
  TkWinAccessible_GetTypeInfo,
  TkWinAccessible_GetIDsOfNames,
  TkWinAccessible_Invoke,
  TkWinAccessible_get_accParent,
  TkWinAccessible_get_accChildCount,
  TkWinAccessible_get_accChild,
  TkWinAccessible_get_accName,
  TkWinAccessible_get_accValue,
  TkWinAccessible_get_accDescription,
  TkWinAccessible_get_accRole,
  TkWinAccessible_get_accState,
  TkWinAccessible_get_accHelp, 
  TkWinAccessible_get_accHelpTopic,
  TkWinAccessible_get_accKeyboardShortcut,
  TkWinAccessible_get_accFocus,
  TkWinAccessible_get_accSelection,
  TkWinAccessible_get_accDefaultAction,
  TkWinAccessible_accSelect,
  TkWinAccessible_accLocation,
  TkWinAccessible_accNavigate,
  TkWinAccessible_accHitTest,
  TkWinAccessible_accDoDefaultAction,
  TkWinAccessible_put_accName,
  TkWinAccessible_put_accValue
};

/*Empty stub functions required by MSAA. */
HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accHelpTopic( IAccessible *this, BSTR *pszHelpFile, VARIANT varChild, long *pidTopic)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accKeyboardShortcut(IAccessible *this, VARIANT varChild, BSTR *pszKeyboardShortcut)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accSelection(IAccessible *this, VARIANT *pvarChildren)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accDefaultAction(IAccessible *this, VARIANT varChild, BSTR *pszDefaultAction)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkWinAccessible_accNavigate(IAccessible *this, long navDir, VARIANT varStart, VARIANT *pvarEndUpAt)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkWinAccessible_accHitTest(IAccessible *this, long xLeft, long yTop, VARIANT *pvarChild)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkWinAccessible_put_accName(IAccessible *this, VARIANT varChild, BSTR szName)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkWinAccessible_put_accValue(IAccessible *this, VARIANT varChild, BSTR szValue)
{
  return E_NOTIMPL;
}

/*Begin active functions.*/

static HRESULT STDMETHODCALLTYPE TkWinAccessible_QueryInterface(IAccessible *this, REFIID riid, void **ppvObject)
{
  if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IAccessible)) {
    *ppvObject = this;
    TkWinAccessible_AddRef(this);
    return S_OK;
  }
  *ppvObject = NULL;
  return E_NOINTERFACE;
}

/* Function to add memory reference to the MSAA object. */
static ULONG STDMETHODCALLTYPE TkWinAccessible_AddRef(IAccessible *this)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
  return InterlockedIncrement(&tkAccessible->refCount);
}

/* Function to free the MSAA object. */
static ULONG STDMETHODCALLTYPE TkWinAccessible_Release(IAccessible *this)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
  ULONG count = InterlockedDecrement(&tkAccessible->refCount);
  if (count == 0) {
    ckfree(tkAccessible);
  }
  return count;
}

static HRESULT STDMETHODCALLTYPE TkWinAccessible_GetTypeInfoCount(IAccessible *this, UINT *pctinfo)
{
  *pctinfo = 0;
  return S_OK;
}

static HRESULT STDMETHODCALLTYPE TkWinAccessible_GetTypeInfo(IAccessible *this, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
  return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkWinAccessible_GetIDsOfNames(IAccessible *this, REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
  ITypeInfo *pTypeInfo = NULL;
  HRESULT hr;

  hr = TkWinAccessible_GetTypeInfo(this, 0, lcid, &pTypeInfo);
  if (FAILED(hr)) {
    return hr;
  }

  hr = DispGetIDsOfNames(pTypeInfo, rgszNames, cNames, rgDispId);
  pTypeInfo->lpVtbl->Release(pTypeInfo);

  return hr;
}

static HRESULT STDMETHODCALLTYPE TkWinAccessible_Invoke(IAccessible *this, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
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
      return TkWinAccessible_get_accName(this, selfVar, &pVarResult->bstrVal);

    case DISPID_ACC_VALUE:
      return TkWinAccessible_get_accValue(this, selfVar, &pVarResult->bstrVal);

    case DISPID_ACC_ROLE:
      return TkWinAccessible_get_accRole(this, selfVar, pVarResult);

    case DISPID_ACC_STATE:
      return TkWinAccessible_get_accState(this, selfVar, pVarResult);

    case DISPID_ACC_DESCRIPTION:
      return TkWinAccessible_get_accDescription(this, selfVar, &pVarResult->bstrVal);

    case DISPID_ACC_HELP:
      return TkWinAccessible_get_accHelp(this, selfVar, &pVarResult->bstrVal);
      
    case DISPID_ACC_DODEFAULTACTION:
      return TkWinAccessible_accDoDefaultAction(this, selfVar);

    case DISPID_ACC_FOCUS:
      return TkWinAccessible_get_accFocus(this, &selfVar);   
	
    case DISPID_ACC_SELECT:
      if (!pDispParams || pDispParams->cArgs < 2) {
        return E_INVALIDARG;
      }

      VARIANT varChild = pDispParams->rgvarg[0];  
      VARIANT varFlags = pDispParams->rgvarg[1]; 

      if (varFlags.vt != VT_I4) return E_INVALIDARG;

      return TkWinAccessible_accSelect(this, varFlags.lVal, varChild);
      
    default:
      return E_NOTIMPL;
    }
  }
}

/* Function to map accessible name to MSAA.*/
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accName(IAccessible *this, VARIANT varChild, BSTR *pszName)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;

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
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accRole(IAccessible *this, VARIANT varChild, VARIANT *pvarRole)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
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
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accState(IAccessible *this, VARIANT varChild, VARIANT *pvarState)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
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
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accValue(IAccessible *this, VARIANT varChild, BSTR *pszValue)
{
  if (!pszValue) return E_INVALIDARG;
	
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
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
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accParent(IAccessible *this, IDispatch **ppdispParent) 
{
  /* Validate ppdispParent to avoid dereferencing a NULL pointer. */
  if (!ppdispParent) return E_INVALIDARG;

  /* Cast this to TkWinAccessible, assuming 'this' is of that type. */
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;

  /* Get the top-level Tk window. */
  Tk_Window tkwin = Tk_MainWindow(tkAccessible->interp);
  HWND hwndTopLevel = Tk_GetHWND(Tk_WindowId(tkwin));

  if (!hwndTopLevel) {
    /* If no top-level window exists, set ppdispParent to NULL and return S_OK. */
    *ppdispParent = NULL;
    return S_OK;
  }

  /* Create an accessible object for the top-level window. */
  TkWinAccessible *topLevelAccessible = CreateTkAccessible(tkAccessible->interp, hwndTopLevel, Tk_PathName(tkwin));
  if (!topLevelAccessible) {
    /* If no accessible object is created, set ppdispParent to NULL and return S_OK. */
    *ppdispParent = NULL;
    return S_OK;
  }

  /* Set ppdispParent to the new accessible object. */
  *ppdispParent = (IDispatch *)topLevelAccessible;

  return S_OK;
}

/* Function to get number of accessible children to MSAA. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accChildCount(IAccessible *this, LONG *pcChildren)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
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
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accChild(IAccessible *this, VARIANT varChild, IDispatch **ppdispChild)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    int count = 0;
    TkWindow *child;
    TkWindow *winPtr = (TkWindow* )Tk_MainWindow(tkAccessible->interp);
		
    for  (child = winPtr->childList; child != NULL; child = child->nextPtr)  {
      if (Tk_IsMapped(child)) {
	if (count + 1 == varChild.lVal) {
	  HWND child_hwnd = Tk_GetHWND(Tk_WindowId(child));
	  TkWinAccessible *childAccessible = CreateTkAccessible(tkAccessible->interp, child_hwnd, Tk_PathName(child));
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
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accLocation(IAccessible *this, LONG *pxLeft, LONG *pyTop, LONG *pcxWidth, LONG *pcyHeight, VARIANT varChild)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
  
  /*Cover both toplevel and child ID widgets. */
  if (varChild.vt == VT_I4 && varChild.lVal >= 0) {
    int x, y, width, height;
    Tk_Window win = Tk_NameToWindow(tkAccessible->interp, tkAccessible->pathName,  Tk_MainWindow(tkAccessible->interp));
    x = Tk_X(win);
    y = Tk_Y(win);
    width = Tk_Width(win);
    height = Tk_Height(win);
    *pxLeft = x;
    *pyTop = y;
    *pcxWidth = width;
    *pcyHeight = height;
    return S_OK;
  }
  return E_INVALIDARG;
}

/*Function to set accessible focus on Tk widget. */
HRESULT STDMETHODCALLTYPE TkWinAccessible_accSelect(IAccessible *thisPtr, long flags, VARIANT varChild) 
{
	
  TkWinAccessible *this = (TkWinAccessible *)thisPtr;
  if (flags & SELFLAG_TAKEFOCUS) {
    Tk_Window tkwin = this->win;
    if (tkwin) {
      Tcl_Interp *interp = Tk_Interp(tkwin);
      if (interp) {
	const char *pathName = Tk_PathName(tkwin);
	Tcl_Obj *result = Tcl_NewObj();
	Tcl_Obj *cmd = Tcl_NewObj();
	Tcl_AppendToObj(cmd, "focus ", -1);
	Tcl_AppendToObj(cmd, pathName, -1);
	if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL) != TCL_OK) {
	  return S_FALSE;
	}
	Tcl_DecrRefCount(cmd);
      }
      return S_OK;
    }
  }
  return S_FALSE; 
}


/* Function to get button press to MSAA. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accDoDefaultAction(IAccessible *this, VARIANT varChild)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;  
  Tk_Window win = tkAccessible->win;
  Tcl_HashEntry *hPtr, *hPtr2;
  Tcl_HashTable *AccessibleAttributes;
  Tcl_Event *event; 

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
  callback_command = action;
  event = (Tcl_Event *)ckalloc(sizeof(Tcl_Event));
  event->proc = TkWinAccessible_ActionEventHandler;
  Tcl_QueueEvent((Tcl_Event *)event, TCL_QUEUE_TAIL);
  return S_OK;  
}

/*
 * Event proc which implements the action event procedure.
 */

static int TkWinAccessible_ActionEventHandler(Tcl_Event *event, int flags)
{
  /*
   * MSVC complains if these parameters are stubbed out
   * with TCL_UNUSED.
   */
  (void) event;
  (void) flags;
  
  TkMainInfo *info = TkGetMainInfoList();
  Tcl_GlobalEval(info->interp, callback_command);
  return 1;
}

/* Function to get accessible help to MSAA. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accHelp(IAccessible *this, VARIANT varChild, BSTR* pszHelp)
{
  if (varChild.vt != VT_I4 || varChild.lVal != CHILDID_SELF) {
    return E_INVALIDARG;
  }

  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;  
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

static STDMETHODIMP TkWinAccessible_get_accFocus(IAccessible *this, VARIANT *pvarChild)
{
  if (!pvarChild) return E_INVALIDARG;
  VariantInit(pvarChild);

  /* Return the currently focused child ID.*/
  LONG focusedId  = g_focusedChildId;
  
  if (focusedId <= 0) {
    /* Either return VARIANT with VT_EMPTY or VT_I4 and lVal = 0 (self). */
    pvarChild->vt = VT_I4;
    pvarChild->lVal = 0; 
  } else {
    pvarChild->vt = VT_I4;
    pvarChild->lVal = focusedId;
  }

  return S_OK;
}

static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accDescription(IAccessible *this, VARIANT varChild, BSTR *pszDescription)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;  
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
  Tcl_DStringInit(&ds);
  *pszDescription = SysAllocString(Tcl_UtfToWCharDString(result, -1, &ds));

  if (!*pszDescription) {
    return E_OUTOFMEMORY;
  }
  Tcl_DStringFree(&ds);
  return S_OK;
}

/* Function to map Tk window to MSAA attributes. */
static TkWinAccessible *CreateTkAccessible(Tcl_Interp *interp, HWND hwnd, const char *pathName)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)ckalloc(sizeof(TkWinAccessible));
  Tk_Window win = Tk_NameToWindow(interp, pathName, Tk_MainWindow(interp));
  if (tkAccessible) {
    tkAccessible->lpVtbl = &tkAccessibleVtbl;
    tkAccessible->interp = interp;
    tkAccessible->hwnd = hwnd;
    tkAccessible->pathName = strdup(pathName);
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
  
  TkWinAccessible_AddRef((IAccessible*)tkAccessible);  
  return tkAccessible;
}

/* Function to map Tk window to MSAA ID's. */
LONG RegisterTkWidget(Tk_Window tkwin) 
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
TkWinAccessible *GetTkAccessibleForWindow(Tk_Window win) {
  if (!tkAccessibleTableInitialized) {
    return NULL;
  }
  Tcl_HashEntry *entry = Tcl_FindHashEntry(tkAccessibleTable, (ClientData)win);
  if (entry) {
    return (TkWinAccessible *)Tcl_GetHashValue(entry);
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
 * TkWinAccessible_RegisterForCleanup --
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

void TkWinAccessible_RegisterForCleanup(Tk_Window tkwin, void *tkAccessible)
{
  Tk_CreateEventHandler(tkwin, StructureNotifyMask, 
			TkWinAccessible_DestroyHandler, tkAccessible);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinAccessible_DestroyHandler --
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

static void TkWinAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr)
{
  if (eventPtr->type == DestroyNotify) {
    TkWinAccessible *tkAccessible = (TkWinAccessible *)clientData;
    if (tkAccessible) {
      TkWinAccessible_Release((IAccessible *)tkAccessible);
    }
  }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinAccessible_FocusHandler --
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

static void TkWinAccessible_FocusEventHandler (ClientData clientData, XEvent *eventPtr)
{

  if (!eventPtr || eventPtr->type != FocusIn) return;
  
  TkWinAccessible *tkAccessible = (TkWinAccessible *)clientData;
  
  Tk_Window tkwin = tkAccessible->win;
  
  Tk_Window parent = GetToplevelOfWidget(tkwin); 
  HWND hwnd = Tk_GetHWND(Tk_WindowId(parent));
    
  if (!hwnd) return;

  /* Look up the unique child ID for this widget.*/ 
  LONG childId = GetChildIdForTkWindow(tkwin); 

  if (childId > 0) {
    /* Notify MSAA that this control has focus. */
    NotifyWinEvent(EVENT_OBJECT_FOCUS, hwnd, OBJID_CLIENT, childId);
  }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinAccessible_RegisterForFocus --
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

void TkWinAccessible_RegisterForFocus(Tk_Window tkwin, void *tkAccessible)
{
  Tk_CreateEventHandler(tkwin, FocusChangeMask, 
			TkWinAccessible_FocusEventHandler, tkAccessible);
}


/*
 *----------------------------------------------------------------------
 *
 * TkWinAccessibleObjCmd --
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


int TkWinAccessibleObjCmd(
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
   * The root widget/toplevel must have an associated HWND 
   * for accessibility. Child widgets are "virtual" accessible
   * objects without an associatewd HWND.
   */

  if (Tk_IsTopLevel(tkwin)) { 
    hwnd = Tk_GetHWND(Tk_WindowId(tkwin)); 
  } else {
    hwnd = NULL;
  }	

  /*Create accessible object and add to hash table. */
  TkWinAccessible *accessible = CreateTkAccessible(interp, hwnd, windowName);
  accessible->win = tkwin;
  TkWinAccessible_RegisterForCleanup(tkwin, accessible);
  TkWinAccessible_RegisterForFocus(tkwin, accessible);
 
  /* Notify screen readers of creation. */
  NotifyWinEvent(EVENT_OBJECT_CREATE, hwnd, OBJID_CLIENT, CHILDID_SELF);
  NotifyWinEvent(EVENT_OBJECT_SHOW, hwnd, OBJID_CLIENT, CHILDID_SELF);
  NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, hwnd, OBJID_CLIENT, CHILDID_SELF);
	
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

  InitTkAccessibleTable();
  InitHwndToTkWindowTable();
  
  Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", TkWinAccessibleObjCmd, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", EmitSelectionChanged, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader", IsScreenReaderRunning, NULL, NULL);
  return TCL_OK;
}

