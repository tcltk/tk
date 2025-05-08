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

#include "tkWinAccessibility.h"

#define WM_TKWINA11Y_INVOKE (WM_USER + 1002)
static WNDPROC oldWndProc = NULL;

/* Hash table for managing accessibility attributes. */
extern Tcl_HashTable *TkAccessibilityObject;

/* Hash tables for linking Tk windows to accessibility object and HWND. */
static Tcl_HashTable *tkAccessibleTable;
static int tkAccessibleTableInitialized = 0;
static Tcl_HashTable *hwndToTkWindowTable;
static int hwndToTkWindowTableInitialized = 0;

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
HRESULT STDMETHODCALLTYPE TkWinAccessible_accSelect(IAccessible *this, long flagsSelect, VARIANT varChild);
HRESULT STDMETHODCALLTYPE TkWinAccessible_accNavigate(IAccessible *this, long navDir, VARIANT varStart, VARIANT *pvarEndUpAt);
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
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accHitTest(IAccessible *this, LONG xLeft, LONG yTop,VARIANT *pvarChild);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accDefaultAction(IAccessible *this, VARIANT varChild, BSTR *pszDefaultAction);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accDoDefaultAction(IAccessible *this, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accHelp(IAccessible *this, VARIANT varChild, BSTR* pszHelp);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accDescription(IAccessible *this, VARIANT varChild, BSTR *pszDescription);
static HRESULT STDMETHODCALLTYPE  TkWinAccessible_get_accFocus(IAccessible *this, VARIANT *pvarChild);

/* Prototypes of Tk functions that support MSAA integration and implement the script-level API. */
static TkWinAccessible *CreateTkAccessible(Tcl_Interp *interp, HWND hwnd, const char *pathName);
void InitTkAccessibleTable(void);
void InitHwndToTkWindowTable(void);
static HWND GetWidgetHWNDIfPresent(Tk_Window tkwin);
Tk_Window GetToplevelOfWidget(Tk_Window tkwin);
int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const argv[]);
int EmitSelectionChanged(ClientData clientData,Tcl_Interp *ip, int objc, Tcl_Obj *const objv[]);
void TkWinAccessible_RegisterForCleanup(Tk_Window tkwin, void *tkAccessible);
static void TkWinAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr);
void TkWinAccessible_RegisterForFocus(Tk_Window tkwin, void *tkAccessible);
static void TkWinAccessible_FocusEventHandler (ClientData clientData, XEvent *eventPtr);
static void TkWinAccessible_InvokeCommand(TkWinAccessible *acc, const char *command);
void TkWinAccessible_HookWindowProc(Tk_Window tkwin);
static LRESULT CALLBACK TkWinAccessible_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
int TkWinAccessibleObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int TkWinAccessiblity_Init(Tcl_Interp *interp);

/*Mapping Tk roles to MSAA roles.*/
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

/* Custom action struct for MSAA. */
typedef struct {
    Tcl_Event header;
    Tcl_Interp *interp;
    char *command;
} TkWinAccessibleActionEvent;


/* Plumbing to the COM/MSAA machinery. */
static IAccessibleVtbl tkAccessibleVtbl = {
    /* IUnknown methods */
    TkWinAccessible_QueryInterface,
    TkWinAccessible_AddRef,
    TkWinAccessible_Release,

    /* IDispatch methods */
    TkWinAccessible_GetTypeInfoCount,
    TkWinAccessible_GetTypeInfo,
    TkWinAccessible_GetIDsOfNames,
    TkWinAccessible_Invoke,

    /* IAccessible methods */
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

HRESULT STDMETHODCALLTYPE TkWinAccessible_accSelect(IAccessible *this, long flagsSelect, VARIANT varChild)
{
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkWinAccessible_accNavigate(IAccessible *this, long navDir, VARIANT varStart, VARIANT *pvarEndUpAt)
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

/*
 * Begin active functions.
 */

/* Function to determine what COM/MSAA interface is supported. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_QueryInterface(IAccessible *this, REFIID riid, void **ppvObject)
{
    if (!ppvObject) {
        return E_POINTER;
    }

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IDispatch) ||
        IsEqualIID(riid, &IID_IAccessible)) {
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
    TkWinAccessible *obj = (TkWinAccessible *)this;
    return InterlockedIncrement(&obj->refCount);
}

/* Function to free the MSAA object. */
static ULONG STDMETHODCALLTYPE TkWinAccessible_Release(IAccessible *this)
{
    TkWinAccessible *obj = (TkWinAccessible *)this;
    ULONG count = InterlockedDecrement(&obj->refCount);
    if (count == 0) {
        ckfree(obj);
    }
    return count;
}

/* Function to return the number of interface types .*/
static HRESULT STDMETHODCALLTYPE TkWinAccessible_GetTypeInfoCount(IAccessible *this, UINT *pctinfo)
{
  *pctinfo = 0;
  return S_OK;
}

/* Function to return the interface types .*/
static HRESULT STDMETHODCALLTYPE TkWinAccessible_GetTypeInfo(IAccessible *this, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
  return E_NOTIMPL;
}

/* Function to return the IDs of interface names.*/
static HRESULT STDMETHODCALLTYPE TkWinAccessible_GetIDsOfNames(IAccessible *this, REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId)
{
    if (!rgszNames || !rgDispId) {
        return E_POINTER;
    }

    static struct {
        LPCOLESTR name;
        DISPID dispId;
    } dispMap[] = {
        { L"accName", DISPID_ACC_NAME },
        { L"accValue", DISPID_ACC_VALUE },
        { L"accDescription", DISPID_ACC_DESCRIPTION },
        { L"accRole", DISPID_ACC_ROLE },
        { L"accState", DISPID_ACC_STATE },
        { L"accHelp", DISPID_ACC_HELP },
        { L"accDefaultAction", DISPID_ACC_DEFAULTACTION },
        { L"accDoDefaultAction", DISPID_ACC_DODEFAULTACTION },
        { L"accFocus", DISPID_ACC_FOCUS },
    };

    for (UINT i = 0; i < cNames; ++i) {
        for (int j = 0; j < sizeof(dispMap)/sizeof(dispMap[0]); ++j) {
            if (_wcsicmp(rgszNames[i], dispMap[j].name) == 0) {
                rgDispId[i] = dispMap[j].dispId;
                return S_OK;
            }
        }
        rgDispId[i] = DISPID_UNKNOWN;
        return DISP_E_UNKNOWNNAME;
    }

    return S_OK;
}

/* Function to invoke different MSAA methods based on interface type. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_Invoke(IAccessible *this, DISPID dispIdMember,REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
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
        pVarResult->vt = VT_BSTR;
        return TkWinAccessible_get_accName(this, selfVar, &pVarResult->bstrVal);

    case DISPID_ACC_VALUE:
        pVarResult->vt = VT_BSTR;
        return TkWinAccessible_get_accValue(this, selfVar, &pVarResult->bstrVal);

    case DISPID_ACC_ROLE:
        return TkWinAccessible_get_accRole(this, selfVar, pVarResult);

    case DISPID_ACC_STATE:
        return TkWinAccessible_get_accState(this, selfVar, pVarResult);

 case DISPID_ACC_DESCRIPTION:
        pVarResult->vt = VT_BSTR;
        return TkWinAccessible_get_accDescription(this, selfVar, &pVarResult->bstrVal);

    case DISPID_ACC_HELP:
        pVarResult->vt = VT_BSTR;
        return TkWinAccessible_get_accHelp(this, selfVar, &pVarResult->bstrVal);

    case DISPID_ACC_DEFAULTACTION:
        pVarResult->vt = VT_BSTR;
        return TkWinAccessible_get_accDefaultAction(this, selfVar, &pVarResult->bstrVal);

    case DISPID_ACC_DODEFAULTACTION:
        return TkWinAccessible_accDoDefaultAction(this, selfVar);

    case DISPID_ACC_FOCUS:
        return TkWinAccessible_get_accFocus(this, &selfVar);

    default:
        return E_NOTIMPL;
    }
}

/* Function to map accessible name to MSAA.*/
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accName(IAccessible *this, VARIANT varChild, BSTR *pszName)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;

  if (varChild.vt != VT_I4 || varChild.lVal == CHILDID_SELF) {
		
    Tk_Window win = tkAccessible->win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_DString ds;
	
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
      return E_INVALIDARG;
    }

    Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "name");
    if (!hPtr2) {
      return E_INVALIDARG;
    }
    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    Tcl_DStringInit(&ds);
    if (result) {
      *pszName = SysAllocString(Tcl_UtfToWCharDString(result, -1, &ds));
    } else {
      *pszName = SysAllocString(Tcl_UtfToWCharDString(tkAccessible->pathName, -1, &ds));
    }
    Tcl_DStringFree(&ds);
    return S_OK;
  }
   return E_INVALIDARG;
}

/* Function to map accessible role to MSAA.*/
static HRESULT STDMETHODCALLTYPE
TkWinAccessible_get_accRole(IAccessible *this, VARIANT varChild, VARIANT *pvarRole)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;

  if (!pvarRole) return E_INVALIDARG;
  if (varChild.vt != VT_I4 || varChild.lVal != CHILDID_SELF) return E_INVALIDARG;

  Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, tkAccessible->win);
  
  if (!hPtr) {
    return E_INVALIDARG;
  }

  Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
  if (!AccessibleAttributes) {
    return E_INVALIDARG;
  }
 
  Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "role");
  if (!hPtr2) {
    return E_INVALIDARG;
  } 

  Tcl_Obj *roleObj = Tcl_GetHashValue(hPtr2);
  if (!roleObj) return E_INVALIDARG;

  const char *tkrole = Tcl_GetString(roleObj);
  if (!tkrole) return E_INVALIDARG;

  LONG role = 0;
  for (int i = 0; roleMap[i].tkrole != NULL; i++) {
    if (strcmp(roleMap[i].tkrole, tkrole) == 0) {
      role = roleMap[i].winrole;
      break;
    }
  }
  
  pvarRole->vt = VT_I4;
  pvarRole->lVal = role;
  return S_OK;
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
		
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
      return E_INVALIDARG;
    }
		
    Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
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
    Tcl_DString ds;
		
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
      return E_INVALIDARG;
    }
		
    Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "value");
    if (!hPtr2) {
      return E_INVALIDARG;
    }
		
    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    if (result) {
      Tcl_DStringInit(&ds);
      *pszValue = SysAllocString(Tcl_UtfToWCharDString(result, -1, &ds));
      Tcl_DStringFree(&ds);
    }
    return S_OK;
  }
  return E_INVALIDARG;
}

/*
 * Function to set parent of Tk widgets as NULL to prevent MSAA from placing
 * focus on toplevel window client area.   We want a flat hierarchy. 
 */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accParent(IAccessible *this, IDispatch **ppdispParent)
{
  if (!this || !ppdispParent) return E_INVALIDARG;
  
  *ppdispParent = NULL;
  return S_OK;
}
  
/*
 * Function to set count of child widgets to zero. We do not
 * want Tk widgets to have child widgets.
 */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accChildCount(IAccessible *this, LONG *pcChildren)
{
  if (!pcChildren || !this) return E_INVALIDARG;
  *pcChildren = 0;
  return S_OK;
}

/*
 * Function to get accessible children to MSAA.
 * With flat hierarchy, return only self.
 */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accChild(IAccessible *this, VARIANT varChild, IDispatch **ppdispChild)
{
  if (!ppdispChild) return E_INVALIDARG;
  *ppdispChild = NULL;
  
  /* Return the IAccessible object itself for CHILDID_SELF. */
  if (varChild.vt != VT_I4 || varChild.lVal != CHILDID_SELF) return E_INVALIDARG;

  *ppdispChild = (IDispatch *)this;
  ((IAccessible *)this)->lpVtbl->AddRef(this);

  return S_OK;
}

/* Get specific screen coordinates to expose MSAA object at that position. */
HRESULT STDMETHODCALLTYPE TkWinAccessible_accHitTest(IAccessible *this, LONG xLeft, LONG yTop, VARIANT *pvarChild)
{
    if (!pvarChild) return E_INVALIDARG;

    TkWinAccessible *acc = (TkWinAccessible *)this;
    Tk_Window tkwin = acc->win;

    int rootX, rootY, width, height;
    Tk_GetRootCoords(tkwin, &rootX, &rootY);
    width = Tk_Width(tkwin);
    height = Tk_Height(tkwin);

    if (xLeft >= rootX && xLeft <= (rootX + width) &&
        yTop >= rootY && yTop <= (rootY + height)) {
        VariantInit(pvarChild);
        pvarChild->vt = VT_I4;
        pvarChild->lVal = CHILDID_SELF; 
        return S_OK;
    }

    return S_FALSE;
}


/* Function to get accessible frame to MSAA. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accLocation(IAccessible *this, LONG *pxLeft, LONG *pyTop, LONG *pcxWidth, LONG *pcyHeight, VARIANT varChild)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
  if (varChild.vt != VT_I4 || varChild.lVal == CHILDID_SELF) {
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

/* Function to return default MSAA action for a widget. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accDefaultAction(IAccessible *this, VARIANT varChild, BSTR *pszDefaultAction)
{
  if (!pszDefaultAction || varChild.vt != VT_I4 || varChild.lVal != CHILDID_SELF) {
    return E_INVALIDARG;
  }

  TkWinAccessible *acc = (TkWinAccessible *)this;

  VARIANT roleVar;
  HRESULT hr = TkWinAccessible_get_accRole(this, varChild, &roleVar);
  if (FAILED(hr)) return hr;

  if (roleVar.vt != VT_I4) return E_FAIL;

  LPCWSTR action = NULL;

  switch (roleVar.lVal) {
  case ROLE_SYSTEM_PUSHBUTTON:
  case ROLE_SYSTEM_MENUITEM:
  case ROLE_SYSTEM_LINK:
  case ROLE_SYSTEM_OUTLINEITEM:
  case ROLE_SYSTEM_LISTITEM:
    action = L"Press";
    break;
  case ROLE_SYSTEM_CHECKBUTTON:
    action = L"Check";
    break;
  case ROLE_SYSTEM_RADIOBUTTON:
    action = L"Select";
    break;
  default:
    action = NULL;
    break;
  }

  if (action) {
    *pszDefaultAction = SysAllocString(action);
    return S_OK;
  } else {
    return S_FALSE;
  }
}


/* Function to get process default widget actiona (i.e. button press) through MSAA. */
static HRESULT STDMETHODCALLTYPE
TkWinAccessible_accDoDefaultAction(IAccessible *this, VARIANT varChild)
{
  if (varChild.vt != VT_I4 || varChild.lVal != CHILDID_SELF) {
    return E_INVALIDARG;
  }

  TkWinAccessible *acc = (TkWinAccessible *)this;
  Tk_Window win = acc->win;

  Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr){
    return E_FAIL;
  }

  Tcl_HashTable *attrs = Tcl_GetHashValue(hPtr);
  Tcl_HashEntry *actionEntry = Tcl_FindHashEntry(attrs, "action");
  if (actionEntry) {
    const char *command = Tcl_GetString(Tcl_GetHashValue(actionEntry));
    if (command && *command) {
      TkWinAccessible_InvokeCommand(acc, command);
      return S_OK;
    }
  }
  return E_FAIL;

}
/*
 * Helper proc which implements the action event procedure.
 */

static void TkWinAccessible_InvokeCommand(TkWinAccessible *acc, const char *command) {
  HWND hwnd = Tk_GetHWND(Tk_WindowId(acc->win));
  SetPropA(hwnd, "TK_A11Y_COMMAND", (HANDLE)command);
  PostMessageW(hwnd, WM_TKWINA11Y_INVOKE, (WPARAM)acc->interp, 0);
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
		
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    return E_INVALIDARG;
  }
		
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "help");
  if (!hPtr2) {
    return E_INVALIDARG;
  }
		
  char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  Tcl_DStringInit(&ds);
  *pszHelp = SysAllocString(Tcl_UtfToWCharDString(result, -1, &ds));
  Tcl_DStringFree(&ds);
  if (!*pszHelp) {
    return E_OUTOFMEMORY;
  }

  return S_OK;
}

/* Function to get accessible focus to MSAA. */
static HRESULT STDMETHODCALLTYPE  TkWinAccessible_get_accFocus(IAccessible *this, VARIANT *pvarChild)
{
  if (!pvarChild) return E_INVALIDARG;

  VariantInit(pvarChild);

  /* Flat hierarchy: always return self.*/
  pvarChild->vt = VT_I4;
  pvarChild->lVal = CHILDID_SELF;

  return S_OK;
}

/* Function to get accessible description to MSAA. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accDescription(IAccessible *this, VARIANT varChild, BSTR *pszDescription)
{

  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;  
  Tk_Window win = tkAccessible->win;
  Tcl_HashEntry *hPtr, *hPtr2;
  Tcl_HashTable *AccessibleAttributes;
  Tcl_DString ds;
		
  hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
  if (!hPtr) {
    return E_INVALIDARG;
  }
		
  AccessibleAttributes = Tcl_GetHashValue(hPtr);
  hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "description");
  if (!hPtr2) {
    return E_INVALIDARG;
  }
		
  char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
  Tcl_DStringInit(&ds);
  *pszDescription = SysAllocString(Tcl_UtfToWCharDString(result, -1, &ds));
  Tcl_DStringFree(&ds);
  if (!*pszDescription) {
    return E_OUTOFMEMORY;
  }
  return S_OK;
}

/* Custom WndProc that process Tcl commands from MSAA. */
static LRESULT CALLBACK TkWinAccessible_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_TKWINA11Y_INVOKE) {
    Tcl_Interp *interp = (Tcl_Interp *)wParam;
    const char *command = (const char *)GetPropA(hwnd, "TK_A11Y_COMMAND");
    if (command && interp) {
      Tcl_Eval(interp, command);
    }
    RemovePropA(hwnd, "TK_A11Y_COMMAND");
    return 0;
  }
  return CallWindowProc(oldWndProc, hwnd, msg, wParam, lParam);
}

/* Helper function to integrate custom WndProc. */
void TkWinAccessible_HookWindowProc(Tk_Window tkwin) {
  HWND hwnd = Tk_GetHWND(Tk_WindowId(tkwin));
  if (!oldWndProc) {
    oldWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)TkWinAccessible_WndProc);
  }
}

/* Core function to create accessible object mapped to a Tk widget. */
static TkWinAccessible *CreateTkAccessible(Tcl_Interp *interp, HWND hwnd, const char *pathName)
{
  /*Check for NULL values to guard against crashes. */
  if (!tkAccessibleTable || !hwndToTkWindowTable) {
    return NULL;
  }

  if (!pathName || !*pathName) {
    return NULL;
  }
    
  Tk_Window win = Tk_NameToWindow(interp, pathName, Tk_MainWindow(interp));
  if (!win) {
    return NULL;
  }
  Tk_Window toplevel = GetToplevelOfWidget(win);
  
  TkWinAccessible *tkAccessible = (TkWinAccessible *)ckalloc(sizeof(TkWinAccessible));  
  if (!tkAccessible) {
    return NULL;
  }
  
  if (tkAccessible) {
    tkAccessible->lpVtbl = &tkAccessibleVtbl;
    tkAccessible->interp = interp;
    tkAccessible->toplevel = toplevel;
    tkAccessible->hwnd = hwnd;
    tkAccessible->pathName = strdup(pathName);
    tkAccessible->win = win;
    tkAccessible->refCount = 1;
  }
  
  Tcl_HashEntry *entry;
  int newEntry;
  entry = Tcl_CreateHashEntry(tkAccessibleTable, win, &newEntry);;
  if (!entry) {
    ckfree(tkAccessible);
    return NULL;
  }
  Tcl_SetHashValue(entry, tkAccessible);
  
  entry = Tcl_CreateHashEntry(hwndToTkWindowTable, hwnd, &newEntry);
  if (!entry) {
    /* Remove previous entry to avoid leaks. */
    Tcl_DeleteHashEntry(Tcl_FindHashEntry(tkAccessibleTable, win));
    ckfree(tkAccessible);
    return NULL;
  }
  Tcl_SetHashValue(entry, win);
  
  return tkAccessible;
}

/*Function to check if a Tk widget (i.e. ttk widget) has a HWND.*/
static HWND GetWidgetHWNDIfPresent(Tk_Window tkwin) {
  if (!tkwin || !Tk_IsMapped(tkwin)) return NULL;

  Window winId = Tk_WindowId(tkwin);
  if (winId == None) return NULL;

  HWND hwnd = Tk_GetHWND(winId);  
  if (hwnd && IsWindow(hwnd)) {
    return hwnd;
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
      /* No calls to Tk_IsMapped, Tk_WindowId, Tk_Parent etc. */
      tkAccessible->win = NULL;
      tkAccessible->toplevel = NULL;
      tkAccessible->hwnd = NULL;
      TkWinAccessible_Release((IAccessible *)tkAccessible);
    }
  }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinAccessible_FocusEventHandler --
 *
 * Force accessibility focus when Tk receives a FocusIn event.
 *
 * Results:
 *	Accessibility focus is set 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void TkWinAccessible_FocusEventHandler(ClientData clientData, XEvent *eventPtr)
{
  if (!clientData || !eventPtr || eventPtr->type != FocusIn) {
    return;
  }

  TkWinAccessible *tkAccessible = (TkWinAccessible *)clientData;
  if (!tkAccessible || !tkAccessible->win || !tkAccessible->toplevel || !tkAccessible->hwnd) {
    return;
  }
    
  if (!Tk_IsMapped(tkAccessible->win)) {
    return;
  }

  if (tkAccessible->hwnd && IsWindow(tkAccessible->hwnd) && tkAccessible->win && tkAccessible->toplevel && Tk_WindowId(tkAccessible->toplevel) != None) {
    NotifyWinEvent(EVENT_OBJECT_FOCUS, tkAccessible->hwnd, OBJID_CLIENT, CHILDID_SELF);
  }
}


/*---------------------------------------------------------------------
 *
 * TkWinAccessible_RegisterForFocus --
 *
 * Register event handler for focusing accessibility element.
 *
 * Results:
 *      Event handler is registered.
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

  HWND hwnd = Tk_GetHWND(Tk_WindowId(tkwin));   
  TkWinAccessible *accessible = CreateTkAccessible(interp, hwnd, windowName);
  TkWinAccessible_RegisterForCleanup(tkwin, accessible);
  TkWinAccessible_RegisterForFocus(tkwin, accessible);
  InitTkAccessibleTable();
  InitHwndToTkWindowTable();
  TkWinAccessible_HookWindowProc(tkwin);
  
  /* Notify screen readers of creation. */
  NotifyWinEvent(EVENT_OBJECT_CREATE, hwnd, OBJID_CLIENT, CHILDID_SELF);
  NotifyWinEvent(EVENT_OBJECT_SHOW, hwnd, OBJID_CLIENT, CHILDID_SELF);
  NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, hwnd, OBJID_CLIENT, CHILDID_SELF);

  if (accessible == NULL) {		
    Tcl_SetResult(interp, "Failed to create accessible object.", TCL_STATIC);
    return TCL_OK;
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

  Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", TkWinAccessibleObjCmd, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", EmitSelectionChanged, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader", IsScreenReaderRunning, NULL, NULL);
  return TCL_OK;
}

