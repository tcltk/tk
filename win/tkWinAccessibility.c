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
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accSelect(IAccessible *this, long flagsSelect, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accHitTest(IAccessible *this, LONG xLeft, LONG yTop,VARIANT *pvarChild);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accNavigate(IAccessible *this, long navDir, VARIANT start, VARIANT *pvarEndUpAt);
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
void TkWinAccessible_BuildChildren(TkWinAccessible *parentAcc);
static void CoUninitializeExitHandler(ClientData clientData);
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
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
  return InterlockedIncrement(&tkAccessible->refCount);
}

/* Function to free the MSAA object. */
static ULONG STDMETHODCALLTYPE TkWinAccessible_Release(IAccessible *this)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
  ULONG count = InterlockedDecrement(&tkAccessible->refCount);
  if (count == 0) {
    if (tkAccessible->pathName) {
      free(tkAccessible->pathName);
    }
    ckfree(tkAccessible);
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
static HRESULT STDMETHODCALLTYPE TkWinAccessible_Invoke(IAccessible *this, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
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
	
  case DISPID_ACC_SELECT:
    if (wFlags & DISPATCH_METHOD) {
      if (pDispParams->cArgs == 2) {
	/* Arguments come in reverse order (last param is at index 0). */
	VARIANT varChild = pDispParams->rgvarg[0];
	VARIANT varFlags = pDispParams->rgvarg[1];
	long flags = varFlags.lVal;
	return TkWinAccessible_accSelect((IAccessible *)this, flags, varChild);
      }
    } else {
      return E_NOTIMPL;
    }

  default:
    return E_NOTIMPL;
  }
}

/* Function to map accessible name to MSAA.*/
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accName(
							     IAccessible *this,
							     VARIANT varChild,
							     BSTR *pszName)
{
  if (!pszName) return E_INVALIDARG;
  *pszName = NULL;

  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
  Tk_Window win = tkAccessible->win;
  Tcl_HashEntry *hPtr = NULL, *hPtr2 = NULL;
  const char *nameStr = NULL;
  Tcl_DString ds;


  /* Ensure children are built if needed. */
  if (!tkAccessible->children) {
    TkWinAccessible_BuildChildren(tkAccessible);
  }

  /* Handle CHILDID_SELF. */
  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (hPtr) {
      Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
      hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "name");
      if (hPtr2) {
	nameStr = Tcl_GetString(Tcl_GetHashValue(hPtr2));
      }
    }

    /* Fallback to pathName. */
    if (!nameStr) {
      nameStr = tkAccessible->pathName;
    }

    /* Still no name, bail. */
    if (!nameStr) {
      return S_FALSE;  
    }

    Tcl_DStringInit(&ds);
    *pszName = SysAllocString(Tcl_UtfToWCharDString(nameStr, -1, &ds));
    Tcl_DStringFree(&ds);
    return (*pszName ? S_OK : E_OUTOFMEMORY);
  }

  /* Handle VT_DISPATCH child. */
  if (varChild.vt == VT_DISPATCH && varChild.pdispVal) {
    IAccessible *childAccessible = NULL;
    HRESULT hr = varChild.pdispVal->lpVtbl->QueryInterface(
							   varChild.pdispVal, &IID_IAccessible, (void **)&childAccessible);
    if (SUCCEEDED(hr) && childAccessible) {
      VARIANT selfVar;
      VariantInit(&selfVar);
      selfVar.vt = VT_I4;
      selfVar.lVal = CHILDID_SELF;
      hr = childAccessible->lpVtbl->get_accName(childAccessible, selfVar, pszName);
      childAccessible->lpVtbl->Release(childAccessible);
      return hr;
    }
    return E_INVALIDARG;
  }

  /* Handle VT_I4 child index. */
  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    LONG childIndex = varChild.lVal - 1;
    if (childIndex >= 0 && childIndex < tkAccessible->numChildren) {
      IAccessible *childAcc = tkAccessible->children[childIndex];
      if (!childAcc) return E_INVALIDARG;

      VARIANT selfVar;
      VariantInit(&selfVar);
      selfVar.vt = VT_I4;
      selfVar.lVal = CHILDID_SELF;
      return childAcc->lpVtbl->get_accName(childAcc, selfVar, pszName);
    }
    return E_INVALIDARG;
  }

  return E_INVALIDARG;
}

/* Function to map accessible role to MSAA.*/
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accRole(IAccessible *this, VARIANT varChild, VARIANT *pvarRole)
{
  if (!pvarRole) return E_INVALIDARG;

  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
  Tk_Window win = tkAccessible->win;
  VariantInit(pvarRole);
  
  LONG role = 0;

#if 0
  /* Always return a non-interactive role for toplevels to keep screen reader traversal alive. */
  if (Tk_IsTopLevel(win) && (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF)) {
    pvarRole->vt = VT_I4;
    pvarRole->lVal = ROLE_SYSTEM_WINDOW;
    return S_OK;
  }
#endif
  /* Handle CHILDID_SELF. */
  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) return E_INVALIDARG;

    Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
    if (!AccessibleAttributes) return E_INVALIDARG;

    Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "role");
    if (!hPtr2) return E_INVALIDARG;

    Tcl_Obj *roleObj = Tcl_GetHashValue(hPtr2);
    if (!roleObj) return E_INVALIDARG;

    const char *tkrole = Tcl_GetString(roleObj);
    if (!tkrole) return E_INVALIDARG;
 
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

  /* Handle VT_DISPATCH child object. */
  if (varChild.vt == VT_DISPATCH && varChild.pdispVal) {
    IAccessible *childAcc = NULL;
    HRESULT hr = varChild.pdispVal->lpVtbl->QueryInterface(
							   varChild.pdispVal, &IID_IAccessible, (void **)&childAcc);
    if (SUCCEEDED(hr)) {
      VARIANT selfVar;
      VariantInit(&selfVar);
      selfVar.vt = VT_I4;
      selfVar.lVal = CHILDID_SELF;

      hr = childAcc->lpVtbl->get_accRole(childAcc, selfVar, pvarRole);
      childAcc->lpVtbl->Release(childAcc);
      return hr;
    }
    return E_INVALIDARG;
  }

  /* Handle VT_I4 child index (1-based). */
  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    LONG index = varChild.lVal - 1;
    if (index >= 0 && index < tkAccessible->numChildren) {
      IAccessible *child = tkAccessible->children[index];
      if (!child) return E_INVALIDARG;

      IAccessible *childAcc = child;
      VARIANT selfVar;
      VariantInit(&selfVar);
      selfVar.vt = VT_I4;
      selfVar.lVal = CHILDID_SELF;

      return childAcc->lpVtbl->get_accRole(childAcc, selfVar, pvarRole);
    }
    return E_INVALIDARG;
  }

  return E_INVALIDARG;
}

/* Function to map accessible state to MSAA. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accState(IAccessible *this, VARIANT varChild, VARIANT *pvarState)
{
    if (!pvarState) return E_INVALIDARG;

    TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
    if (!tkAccessible || !tkAccessible->win) return E_FAIL;

    VariantInit(pvarState);
    pvarState->vt = VT_I4;
    long state = 0;

    /* Ensure children are built. */
    if (!tkAccessible->children) {
        TkWinAccessible_BuildChildren(tkAccessible);
    }

    HWND hwnd = NULL;
    if (Tk_WindowId((TkWindow *)tkAccessible->win)) {
        hwnd = Tk_GetHWND(Tk_WindowId((TkWindow *)tkAccessible->win));
    }

    HWND focusHwnd = GetFocus();

    /* Handle SELF. */
    if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
        if (Tk_IsTopLevel(tkAccessible->win)) {
            state |= STATE_SYSTEM_FOCUSABLE;
        } else {
            /* Treat TkChild as a container. */
            state |= STATE_SYSTEM_READONLY;
        }

        if (hwnd && hwnd == focusHwnd) {
            char className[64] = {0};
            GetClassNameA(hwnd, className, sizeof(className));

            /* Only report focused if this is not just a container. */
            if (strcmp(className, "TkChild") != 0) {
                state |= STATE_SYSTEM_FOCUSED;
            }
        }

        pvarState->lVal = state;
        return S_OK;
    }

    /* Handle virtual children. */
    if (varChild.vt == VT_I4 &&
        varChild.lVal > 0 &&
        varChild.lVal <= tkAccessible->numChildren)
    {
        int index = varChild.lVal - 1;
        TkWinAccessible *childAcc = (TkWinAccessible *)tkAccessible->children[index];
        if (!childAcc || !childAcc->win) return E_FAIL;

        state |= STATE_SYSTEM_FOCUSABLE;

        /* If this child is the one we marked as focused. */
        if (childAcc->win == tkAccessible->focusedChildWin) {
            state |= STATE_SYSTEM_FOCUSED;
        } else {
            /* Try matching against the real HWND focus. */
            HWND childHwnd = NULL;
            if (Tk_WindowId((TkWindow *)childAcc->win)) {
                childHwnd = Tk_GetHWND(Tk_WindowId((TkWindow *)childAcc->win));
            }

            if (childHwnd && childHwnd == focusHwnd) {
                char className[64] = {0};
                GetClassNameA(childHwnd, className, sizeof(className));
                if (strcmp(className, "TkChild") != 0) {
                    state |= STATE_SYSTEM_FOCUSED;
                }
            }
        }

        pvarState->lVal = state;
        return S_OK;
    }

    return E_INVALIDARG;
}


/* Function to map accessible value to MSAA.*/
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accValue(IAccessible *this, VARIANT varChild, BSTR *pszValue)
{
  if (!pszValue) return E_INVALIDARG;
  *pszValue = NULL;

  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
  Tk_Window win = tkAccessible->win;

  /* Handle CHILDID_SELF. */
  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) return E_INVALIDARG;

    Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
    if (!AccessibleAttributes) return E_INVALIDARG;

    Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "value");
    if (!hPtr2) return E_INVALIDARG;

    const char *value = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    if (value) {
      Tcl_DString ds;
      Tcl_DStringInit(&ds);
      *pszValue = SysAllocString(Tcl_UtfToWCharDString(value, -1, &ds));
      Tcl_DStringFree(&ds);
    }

    return S_OK;
  }

  /* Handle VT_DISPATCH child. */
  if (varChild.vt == VT_DISPATCH && varChild.pdispVal) {
    IAccessible *childAcc = NULL;
    HRESULT hr = varChild.pdispVal->lpVtbl->QueryInterface(
							   varChild.pdispVal, &IID_IAccessible, (void **)&childAcc);
    if (SUCCEEDED(hr)) {
      VARIANT selfVar;
      VariantInit(&selfVar);
      selfVar.vt = VT_I4;
      selfVar.lVal = CHILDID_SELF;

      hr = childAcc->lpVtbl->get_accValue(childAcc, selfVar, pszValue);
      childAcc->lpVtbl->Release(childAcc);
      return hr;
    }
    return E_INVALIDARG;
  }

  /* Handle child index (1-based). */
  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    LONG index = varChild.lVal - 1;
    if (index >= 0 && index < tkAccessible->numChildren) {
      IAccessible *child = tkAccessible->children[index];
      if (!child) return E_INVALIDARG;

      IAccessible *childAcc = child;
      VARIANT selfVar;
      VariantInit(&selfVar);
      selfVar.vt = VT_I4;
      selfVar.lVal = CHILDID_SELF;

      return childAcc->lpVtbl->get_accValue(childAcc, selfVar, pszValue);
    }
    return E_INVALIDARG;
  }

  return E_INVALIDARG;
}
/*
 * Function to return the parent accessible object for MSAA.
 * This implementation maintains a flat accessibility hierarchy,
 * where the parent of a widget is always its toplevel window,
 * and toplevels return NULL (indicating no parent).
 */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accParent(IAccessible *this, IDispatch **ppdispParent)
{
  if (!this || !ppdispParent) {
    return E_INVALIDARG;
  }

  *ppdispParent = NULL;

  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;

  /* If the widget is a toplevel, it has no parent in MSAA.*/
  if (Tk_IsTopLevel(tkAccessible->win)) {
    return S_FALSE;
  }

  /* Get the parent toplevel window of the widget. */
  Tk_Window toplevel = GetToplevelOfWidget(tkAccessible->win);
  if (!toplevel) {
    return E_FAIL;
  }

  const char *parentPath = Tk_PathName(toplevel);
  TkWinAccessible *parentAcc = CreateTkAccessible(tkAccessible->interp, tkAccessible->hwnd, parentPath);
  if (!parentAcc) {
    return E_OUTOFMEMORY;
  }

  /* Return the IDispatch pointer to the parent accessible object. */
  *ppdispParent = (IDispatch *)parentAcc;
  ((IAccessible *)parentAcc)->lpVtbl->AddRef((IAccessible *)parentAcc);

  return S_OK;
}


/*
 * Function to get child count of toplevel.
 */
 
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accChildCount(IAccessible *this, long *pcountChildren)
{
  if (!this || !pcountChildren) return E_INVALIDARG;

  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;

  if (!tkAccessible->children) {
    TkWinAccessible_BuildChildren(tkAccessible);
  }

  *pcountChildren = tkAccessible->numChildren;
  return S_OK;
}


/*
 * Function to get accessible virtual children to MSAA.
 */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accChild(IAccessible *this, VARIANT varChild, IDispatch **ppdispChild)
{
  if (!this || !ppdispChild) return E_INVALIDARG;
  *ppdispChild = NULL;

  if (varChild.vt != VT_I4) return E_INVALIDARG;

  LONG index = varChild.lVal;
  if (index == CHILDID_SELF) return S_FALSE;

  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;

  if (!tkAccessible->children) {
    TkWinAccessible_BuildChildren(tkAccessible);
  }

  if (index < 1 || index > tkAccessible->numChildren) return E_INVALIDARG;

  IAccessible *child = tkAccessible->children[index - 1];
  if (!child) return E_FAIL;

  *ppdispChild = (IDispatch *)child;
  child->lpVtbl->AddRef(child);

  return S_OK;
}

/* Get specific screen coordinates to expose MSAA object at that position. */
HRESULT STDMETHODCALLTYPE TkWinAccessible_accHitTest(IAccessible *this, LONG xLeft, LONG yTop, VARIANT *pvarChild)
{
  if (!pvarChild) return E_INVALIDARG;

  TkWinAccessible *tkAccessible= (TkWinAccessible *)this;
  Tk_Window tkwin = tkAccessible->win;

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
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accLocation(
							     IAccessible *this,
							     LONG *pxLeft, LONG *pyTop,
							     LONG *pcxWidth, LONG *pcyHeight,
							     VARIANT varChild)
{
  if (!pxLeft || !pyTop || !pcxWidth || !pcyHeight) {
    return E_INVALIDARG;
  }

  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;

  /* Allow VT_EMPTY to default to CHILDID_SELF. */
  if (varChild.vt == VT_EMPTY) {
    varChild.vt = VT_I4;
    varChild.lVal = CHILDID_SELF;
  }

  if (varChild.vt != VT_I4) {
    return E_INVALIDARG;
  }

  int x = 0, y = 0, width = 0, height = 0;

  /* Toplevel window. */
  if (varChild.lVal == CHILDID_SELF) {
    Tk_Window win = Tk_NameToWindow(tkAccessible->interp, tkAccessible->pathName, Tk_MainWindow(tkAccessible->interp));
    if (!win) return E_FAIL;

    Tk_GetRootCoords(win, &x, &y);
    width = Tk_Width(win);
    height = Tk_Height(win);
  } else {
    /* Child widget. */
    LONG childIndex = varChild.lVal - 1;

    if (!tkAccessible->children) {
      TkWinAccessible_BuildChildren(tkAccessible);
    }

    if (childIndex < 0 || childIndex >= tkAccessible->numChildren) {
      return E_INVALIDARG;
    }

    TkWinAccessible *childAcc = (TkWinAccessible *)tkAccessible->children[childIndex];
    if (!childAcc || !childAcc->win) {
      return E_INVALIDARG;
    }

    Tk_Window childWin = childAcc->win;
    Tk_GetRootCoords(childWin, &x, &y);
    width = Tk_Width(childWin);
    height = Tk_Height(childWin);
  }

  *pxLeft = x;
  *pyTop = y;
  *pcxWidth = width;
  *pcyHeight = height;

  return S_OK;
}

/* Function to return default MSAA action for a widget. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accDefaultAction(IAccessible *this, VARIANT varChild, BSTR *pszDefaultAction)
{
 
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;

  /* Handle CHILDID_SELF (for this object). */
  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    /* Retrieve the role of the widget. */
    VARIANT roleVar;
    HRESULT hr = TkWinAccessible_get_accRole(this, varChild, &roleVar);
    if (FAILED(hr)) return hr;

    if (roleVar.vt != VT_I4) return E_FAIL;

    LPCWSTR action = NULL;

    /* Determine the default action based on the role. */
    switch (roleVar.lVal) {
    case ROLE_SYSTEM_PUSHBUTTON:
    case ROLE_SYSTEM_MENUITEM:
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

    /* Return the default action if available. */
    if (action) {
      *pszDefaultAction = SysAllocString(action);
      return S_OK;
    } else {
      return S_FALSE; 
    }
  }

  /* Handle VT_DISPATCH child (forward to the child object). */
  if (varChild.vt == VT_DISPATCH && varChild.pdispVal) {
    IAccessible *childAccessible = NULL;
    HRESULT hr = varChild.pdispVal->lpVtbl->QueryInterface(
							   varChild.pdispVal, &IID_IAccessible, (void **)&childAccessible);
    if (SUCCEEDED(hr) && childAccessible) {
      VARIANT selfVar;
      VariantInit(&selfVar);
      selfVar.vt = VT_I4;
      selfVar.lVal = CHILDID_SELF;

      hr = childAccessible->lpVtbl->get_accDefaultAction(childAccessible, selfVar, pszDefaultAction);
      childAccessible->lpVtbl->Release(childAccessible);
      return hr;
    }
    return E_INVALIDARG;
  }

  /* Handle child index (1-based). */
  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    LONG childIndex = varChild.lVal - 1;

    if (childIndex >= 0 && childIndex < tkAccessible->numChildren) {
      IAccessible *child = tkAccessible->children[childIndex];
      if (!child) return E_INVALIDARG;

      VARIANT selfVar;
      VariantInit(&selfVar);
      selfVar.vt = VT_I4;
      selfVar.lVal = CHILDID_SELF;

      HRESULT hr = child->lpVtbl->get_accDefaultAction(child, selfVar, pszDefaultAction);
      child->lpVtbl->Release(child);
      return hr;
    } else {
      return E_INVALIDARG;  /* Index out of range. */
    }
  }
  return E_INVALIDARG;
}


/* Function to process default widget action (e.g., button press) through MSAA. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accDoDefaultAction(IAccessible *this, VARIANT varChild)
{
  /* Handle CHILDID_SELF (this object). */
  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
    Tk_Window win = tkAccessible->win;

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
      return E_FAIL;
    }

    Tcl_HashTable *attrs = Tcl_GetHashValue(hPtr);
    Tcl_HashEntry *actionEntry = Tcl_FindHashEntry(attrs, "action");
    if (actionEntry) {
      const char *command = Tcl_GetString(Tcl_GetHashValue(actionEntry));
      if (command && *command) {
	TkWinAccessible_InvokeCommand(tkAccessible, command);
	return S_OK;
      }
    }
    return E_FAIL;
  }

  /* Handle VT_DISPATCH child (forward the call to the child object). */
  if (varChild.vt == VT_DISPATCH && varChild.pdispVal) {
    IAccessible *childAccessible = NULL;
    HRESULT hr = varChild.pdispVal->lpVtbl->QueryInterface(
							   varChild.pdispVal, &IID_IAccessible, (void **)&childAccessible);
    if (SUCCEEDED(hr) && childAccessible) {
      VARIANT selfVar;
      VariantInit(&selfVar);
      selfVar.vt = VT_I4;
      selfVar.lVal = CHILDID_SELF;

      hr = childAccessible->lpVtbl->accDoDefaultAction(childAccessible, selfVar);
      childAccessible->lpVtbl->Release(childAccessible);
      return hr;
    }
    return E_INVALIDARG;
  }

  /* Handle child index (1-based.) */
  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    LONG childIndex = varChild.lVal - 1;  /* Convert to 0-based index. */

    TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
    if (childIndex >= 0 && childIndex < tkAccessible->numChildren) {
      IAccessible *child = tkAccessible->children[childIndex];
      if (!child) return E_INVALIDARG;

      VARIANT selfVar;
      VariantInit(&selfVar);
      selfVar.vt = VT_I4;
      selfVar.lVal = CHILDID_SELF;

      HRESULT hr = child->lpVtbl->accDoDefaultAction(child, selfVar);
      child->lpVtbl->Release(child);
      return hr;
    }
    return E_INVALIDARG;
  }

  /* Invalid input for varChild. */
  return E_INVALIDARG;
}

/*
 * Helper proc which implements the action event procedure.
 */
static void TkWinAccessible_InvokeCommand(TkWinAccessible *tkAccessible, const char *command) {
  HWND hwnd = Tk_GetHWND(Tk_WindowId(tkAccessible->win));
  SetPropA(hwnd, "TK_A11Y_COMMAND", (HANDLE)command);
  PostMessageW(hwnd, WM_TKWINA11Y_INVOKE, (WPARAM)tkAccessible->interp, 0);
}


/* Function to get help text to MSAA. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accHelp(IAccessible *this, VARIANT varChild, BSTR* pszHelp)
{
  if (!pszHelp) return E_INVALIDARG;

  TkWinAccessible *tkAccessible = (TkWinAccessible *)this; 
  Tk_Window win = tkAccessible->win;
  Tcl_HashEntry *hPtr, *hPtr2;
  Tcl_HashTable *AccessibleAttributes;
  Tcl_DString ds;
  
  /* Handle CHILDID_SELF (the widget itself). */
  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) return E_INVALIDARG;

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "help");
    if (!hPtr2) return E_INVALIDARG;

    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    Tcl_DStringInit(&ds);
    *pszHelp = SysAllocString(Tcl_UtfToWCharDString(result, -1, &ds));
    Tcl_DStringFree(&ds);
    if (!*pszHelp) return E_OUTOFMEMORY;
    return S_OK;
  }

  /* Handle VT_DISPATCH (child object). */
  if (varChild.vt == VT_DISPATCH && varChild.pdispVal) {
    IAccessible *childAccessible = NULL;
    HRESULT hr = varChild.pdispVal->lpVtbl->QueryInterface(
							   varChild.pdispVal, &IID_IAccessible, (void **)&childAccessible);
    if (SUCCEEDED(hr) && childAccessible) {
      VARIANT selfVar;
      VariantInit(&selfVar);
      selfVar.vt = VT_I4;
      selfVar.lVal = CHILDID_SELF;

      /* Recursively call get_accHelp on the child. */
      HRESULT hr = childAccessible->lpVtbl->get_accHelp(childAccessible, selfVar, pszHelp);
      childAccessible->lpVtbl->Release(childAccessible);
      return hr;
    }
    return E_INVALIDARG;
  }

  /* Handle 1-based child index. */
  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    LONG childIndex = varChild.lVal - 1;  /* 1-based to 0-based index. */
    if (childIndex >= 0 && childIndex < tkAccessible->numChildren) {
      IAccessible *child = tkAccessible->children[childIndex];
      if (!child) return E_INVALIDARG;

      VARIANT selfVar;
      VariantInit(&selfVar);
      selfVar.vt = VT_I4;
      selfVar.lVal = CHILDID_SELF;

      /* Recursively call get_accHelp on the child. */
      HRESULT hr = child->lpVtbl->get_accHelp(child, selfVar, pszHelp);
      child->lpVtbl->Release(child);
      return hr;
    }
  }
  return E_INVALIDARG;
}

/* Function to get focus for accessible widget. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accFocus(IAccessible *this, VARIANT *pvarChild)
{
    if (!pvarChild) return E_INVALIDARG;

    TkWinAccessible *tkAccessible = (TkWinAccessible *)this;

    /* Ensure children are built. */
    if (!tkAccessible->children) {
        TkWinAccessible_BuildChildren(tkAccessible);
    }

    VariantInit(pvarChild);

    /* Check for explicitly registered focused child. */
    if (tkAccessible->focusedChildWin) {
        for (int i = 0; i < tkAccessible->numChildren; i++) {
            TkWinAccessible *childAcc = (TkWinAccessible *)tkAccessible->children[i];
            if (childAcc && childAcc->win == tkAccessible->focusedChildWin) {
                pvarChild->vt = VT_I4;
                pvarChild->lVal = i + 1;  /* 1-based child ID. */
                return S_OK;
            }
        }
    }

    /* Try to match focus HWND to child widgets. */
    HWND focusHwnd = GetFocus();
    if (focusHwnd) {
        for (int i = 0; i < tkAccessible->numChildren; i++) {
            TkWinAccessible *childAcc = (TkWinAccessible *)tkAccessible->children[i];
            if (childAcc && childAcc->win && Tk_WindowId((TkWindow *)childAcc->win)) {
                HWND childHwnd = Tk_GetHWND(Tk_WindowId((TkWindow *)childAcc->win));
                if (childHwnd == focusHwnd) {
                    pvarChild->vt = VT_I4;
                    pvarChild->lVal = i + 1;
                    return S_OK;
                }
            }
        }
    }

    /* If this container (top-level or TkChild) has focus, report CHILDID_SELF. */
    HWND hwnd = NULL;
    if (Tk_WindowId((TkWindow *)tkAccessible->win)) {
        hwnd = Tk_GetHWND(Tk_WindowId((TkWindow *)tkAccessible->win));
    }

    if (hwnd && hwnd == focusHwnd) {
        /* TkChilds shouldn't report themselves as focused. */
        char className[64] = {0};
        GetClassNameA(hwnd, className, sizeof(className));
        if (strcmp(className, "TkChild") != 0) {
            pvarChild->vt = VT_I4;
            pvarChild->lVal = CHILDID_SELF;
            return S_OK;
        }
    }

    /* No focus match found. */
    pvarChild->vt = VT_EMPTY;
    return S_FALSE;
}


/* Function to get description for accessible widget. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accDescription(IAccessible *this, VARIANT varChild, BSTR *pszDescription)
{
  if (!pszDescription) return E_INVALIDARG;

  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;  
  Tk_Window win = tkAccessible->win;
  Tcl_HashEntry *hPtr, *hPtr2;
  Tcl_HashTable *AccessibleAttributes;
  Tcl_DString ds;

  /* Handle CHILDID_SELF (the widget itself). */
  if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    /* Make sure we do not return a value for a toplevel window. */
    if (Tk_IsTopLevel(tkAccessible->win)) {
      *pszDescription = NULL;
      return S_FALSE;
    }

    hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) return E_INVALIDARG;

    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "description");
    if (!hPtr2) return E_INVALIDARG;

    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    Tcl_DStringInit(&ds);
    *pszDescription = SysAllocString(Tcl_UtfToWCharDString(result, -1, &ds));
    Tcl_DStringFree(&ds);
    if (!*pszDescription) return E_OUTOFMEMORY;
    return S_OK;
  }

  /* Handle VT_DISPATCH (child object). */
  if (varChild.vt == VT_DISPATCH && varChild.pdispVal) {
    IAccessible *childAccessible = NULL;
    HRESULT hr = varChild.pdispVal->lpVtbl->QueryInterface(
							   varChild.pdispVal, &IID_IAccessible, (void **)&childAccessible);
    if (SUCCEEDED(hr) && childAccessible) {
      VARIANT selfVar;
      VariantInit(&selfVar);
      selfVar.vt = VT_I4;
      selfVar.lVal = CHILDID_SELF;

      /* Recursively call get_accDescription on the child. */
      HRESULT hr = childAccessible->lpVtbl->get_accDescription(childAccessible, selfVar, pszDescription);
      childAccessible->lpVtbl->Release(childAccessible);
      return hr;
    }
    return E_INVALIDARG;
  }

  /* Handle 1-based child index. */
  if (varChild.vt == VT_I4 && varChild.lVal > 0) {
    LONG childIndex = varChild.lVal - 1; /* 1-based to 0-based index.*/
    if (childIndex >= 0 && childIndex < tkAccessible->numChildren) {
      IAccessible *child = tkAccessible->children[childIndex];
      if (!child) return E_INVALIDARG;

      VARIANT selfVar;
      VariantInit(&selfVar);
      selfVar.vt = VT_I4;
      selfVar.lVal = CHILDID_SELF;

      /* Recursively call get_accDescription on the child. */
      HRESULT hr = child->lpVtbl->get_accDescription(child, selfVar, pszDescription);
      child->lpVtbl->Release(child);
      return hr;
    }
    return E_INVALIDARG;
  }

  return E_INVALIDARG;
}


/* Custom WndProc that process Tcl commands from MSAA. */
static LRESULT CALLBACK TkWinAccessible_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_GETOBJECT && (LONG)lParam == OBJID_CLIENT) {
    /* Filter: only respond if this is a TkTopLevel HWND. */
    char className[64] = {0};
    GetClassNameA(hwnd, className, sizeof(className));
    //if (strcmp(className, "TkTopLevel") != 0) {
    //    return 0; /* Block MSAA fallback for TkChild. */
    // }

    Tk_Window tkwin = GetTkWindowForHwnd(hwnd);
    TkWinAccessible *acc = tkwin ? GetTkAccessibleForWindow(tkwin) : NULL;

    if (acc) {
      return LresultFromObject(&IID_IAccessible, wParam, (IUnknown *)acc);
    }

  } else if (msg == WM_TKWINA11Y_INVOKE) {
    Tcl_Interp *interp = (Tcl_Interp *)wParam;
    const char *command = (const char *)GetPropA(hwnd, "TK_A11Y_COMMAND");
    if (command && interp) {
      Tcl_Eval(interp, command);
    }
    RemovePropA(hwnd, "TK_A11Y_COMMAND");
    return 0;
  }

  /* Forward to original window proc stored per-window */
  WNDPROC oldProc = (WNDPROC)GetPropA(hwnd, "TK_OLD_WNDPROC");
  return CallWindowProc(oldProc, hwnd, msg, wParam, lParam);
}

/* Helper function to install the custom WndProc for Tk top-level windows. */
void TkWinAccessible_HookWindowProc(Tk_Window tkwin) {
  if (!tkwin || !Tk_IsTopLevel(tkwin)) return;

  HWND hwnd = Tk_GetHWND(Tk_WindowId(tkwin));
  if (!hwnd) return;

  char className[64] = {0};
  GetClassNameA(hwnd, className, sizeof(className));

#if 0
  /* Only hook top-levels . */
  if (strcmp(className, "TkTopLevel") != 0) {
    return;
  }
#endif

  if ((WNDPROC)GetPropA(hwnd, "TK_OLD_WNDPROC") == NULL) {
    WNDPROC oldProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
    SetPropA(hwnd, "TK_OLD_WNDPROC", (HANDLE)oldProc);
    SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)TkWinAccessible_WndProc);
  }
}

/* Function to set focus on selected widget. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accSelect(IAccessible *this, long flags, VARIANT varChild)
{
  /* Ensure valid arguments. */
  if (varChild.vt != VT_I4 || varChild.lVal < 0) {
    return E_INVALIDARG; /* Invalid child identifier. */
  }

  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;

  if (!tkAccessible || !tkAccessible->win || !tkAccessible->children) {
    return E_FAIL; /* Invalid or missing accessible widget. */
  }

  /* Handle selection logic. */
  long childId = varChild.lVal;
  TkWinAccessible *childAcc = (TkWinAccessible *)tkAccessible->children[childId - 1]; /* Assuming 1-based indexing. */

  if (!childAcc || !childAcc->win) {
    return E_FAIL; /* No valid child found. */
  }

  /* Perform the selection action on the widget. */
  if (flags & SELFLAG_TAKEFOCUS) {
    /* Set focus to the selected child widget. */
    TkSetFocusWin((TkWindow*)childAcc->win, 0);

    /* Notify MSAA about the focus change (send event). */
    NotifyWinEvent(EVENT_OBJECT_FOCUS, tkAccessible->hwnd, OBJID_CLIENT, childId);

    /* Optionally: Mark the widget as selected if applicable. */
    NotifyWinEvent(EVENT_OBJECT_SELECTION, tkAccessible->hwnd, OBJID_CLIENT, childId);
  }

  /* Mark the selection (you can add more logic depending on the widget type). */
  NotifyWinEvent(EVENT_OBJECT_SELECTION, tkAccessible->hwnd, OBJID_CLIENT, childId);

  return S_OK;
}
 

/* Helper function to navigate accessibility tree. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accNavigate(IAccessible *this, long navDir, VARIANT start, VARIANT *pvarEndUpAt)
{
  TkWinAccessible *acc = (TkWinAccessible *)this;

  if (!pvarEndUpAt || !acc->children || acc->numChildren == 0)
    return S_FALSE;

  VariantInit(pvarEndUpAt);

  if (start.vt != VT_I4 || start.lVal != CHILDID_SELF)
    return E_INVALIDARG;

  if (navDir == NAVDIR_FIRSTCHILD) {
    pvarEndUpAt->vt = VT_DISPATCH;
    pvarEndUpAt->pdispVal = (IDispatch *)acc->children[0];
    acc->children[0]->lpVtbl->AddRef(acc->children[0]);
    return S_OK;
  } else if (navDir == NAVDIR_LASTCHILD) {
    pvarEndUpAt->vt = VT_DISPATCH;
    pvarEndUpAt->pdispVal = (IDispatch *)acc->children[acc->numChildren - 1];
    acc->children[acc->numChildren - 1]->lpVtbl->AddRef(acc->children[acc->numChildren - 1]);
    return S_OK;
  }

  return S_FALSE;
}

/* Helper function to build array of child widgets.*/
void TkWinAccessible_BuildChildren(TkWinAccessible *parentAcc) 
{
  if (!parentAcc || !parentAcc->win) return;

  TkWindow *parentWin = (TkWindow *)parentAcc->win;
  TkWindow *childWin = parentWin->childList;
  int count = 0;

  /* First pass: count the children. */
  for (TkWindow *win = childWin; win != NULL; win = win->nextPtr) {
    if (!(win->flags & TK_CONTAINER)) {
      count++;
    }
  }
  
  if (count == 0) return;

  /* Allocate the array. */
  parentAcc->children = (IAccessible **)ckalloc(sizeof(IAccessible *) * count);

  /* Second pass: create accessible wrappers. */
  int i = 0;
  for (TkWindow *win = childWin; win != NULL; win = win->nextPtr) {
    if (win->flags & TK_CONTAINER) {
      continue; /* Skip embedded containers. */
    }

    const char *childPath = Tk_PathName((Tk_Window)win);
    Tk_MakeWindowExist((Tk_Window)win);
    TkWinAccessible *childAcc = CreateTkAccessible(parentAcc->interp, parentAcc->hwnd, childPath);
    if (childAcc) {
      childAcc->win = (Tk_Window)win;  
      parentAcc->children[i++] = (IAccessible *)childAcc;
      NotifyWinEvent(EVENT_OBJECT_CREATE, parentAcc->hwnd, OBJID_CLIENT, i + 1);
      NotifyWinEvent(EVENT_OBJECT_SHOW, parentAcc->hwnd, OBJID_CLIENT, i + 1);
    }
  }

  /* In case some children were skipped. */
  parentAcc->numChildren = i;
}

/* Core function to create accessible object mapped to a Tk widget. */
TkWinAccessible *CreateTkAccessible(Tcl_Interp *interp, HWND hwndInput, const char *pathName)
{
  if (!interp || !pathName) return NULL;

  Tk_Window tkwin = Tk_NameToWindow(interp, pathName, Tk_MainWindow(interp));
  if (!tkwin) return NULL;
  
  /* Do not create an accessible object for the TkChild window class. */
  const char *className = Tk_Class(tkwin);
  if (className && strcmp(className, "TkChild") == 0) {
    return NULL; // Exclude TkChild class
  }

  Tk_Window toplevel = GetToplevelOfWidget(tkwin);
  HWND hwnd = Tk_GetHWND(Tk_WindowId(toplevel));
  if (!hwnd) return NULL;

  InitTkAccessibleTable();
  InitHwndToTkWindowTable();

  /* Create and initialize object. */
  TkWinAccessible *tkAccessible = (TkWinAccessible *)ckalloc(sizeof(TkWinAccessible));
  ZeroMemory(tkAccessible, sizeof(TkWinAccessible));

  tkAccessible->interp = interp;
  tkAccessible->win = tkwin;
  tkAccessible->refCount = 1;
  tkAccessible->pathName = strdup(pathName);
  tkAccessible->hwnd = hwnd;
  tkAccessible->lpVtbl = &tkAccessibleVtbl;
  tkAccessible->focusedIndex = -1;

  /* Register in hash table. */
  int newEntry;
  Tcl_HashEntry *entry = Tcl_CreateHashEntry(tkAccessibleTable, tkwin, &newEntry);
  Tcl_SetHashValue(entry, tkAccessible);

  entry = Tcl_CreateHashEntry(hwndToTkWindowTable, hwnd, &newEntry);
  Tcl_SetHashValue(entry, toplevel);

  /* Hook WndProc on toplevel.*/
  TkWinAccessible_HookWindowProc(toplevel);

  return tkAccessible;
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

/* Function to clean up COM interaction. */
static void CoUninitializeExitHandler(ClientData clientData) {
  (void)clientData;  /* Suppress unused parameter warning */
  CoUninitialize();
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

void TkWinAccessible_FocusEventHandler(ClientData clientData, XEvent *eventPtr)
{
    TkWinAccessible *tkAccessible = (TkWinAccessible *)clientData;
    if (!tkAccessible || !eventPtr || eventPtr->type != FocusIn) return;

    /* Get X11 window from event and map it back to a Tk window. */
    Display *display = Tk_Display(tkAccessible->win);
    Window xwin = eventPtr->xfocus.window;
    Tk_Window win = Tk_IdToWindow(display, xwin);
    if (!win) return;

    /* Get the HWND for the target window. */
    HWND hwnd = NULL;
    if (Tk_WindowId((TkWindow *)win)) {
        hwnd = Tk_GetHWND(Tk_WindowId((TkWindow *)win));
    }

    /* Ignore TkChild fallback HWNDs. */
    if (hwnd) {
        char className[64] = {0};
        GetClassNameA(hwnd, className, sizeof(className));
        if (strcmp(className, "TkChild") == 0) {
            return;
        }
    }

    /* Ensure children are built for this accessible object. */
    if (!tkAccessible->children) {
        TkWinAccessible_BuildChildren(tkAccessible);
    }

    /* Look for the focused widget among virtual children. */
    for (int i = 0; i < tkAccessible->numChildren; i++) {
        TkWinAccessible *childAcc = (TkWinAccessible *)tkAccessible->children[i];
        if (childAcc && childAcc->win == win) {
            /* Report virtual child focus. */
            NotifyWinEvent(EVENT_OBJECT_FOCUS, tkAccessible->hwnd, OBJID_CLIENT, i + 1);
            tkAccessible->focusedIndex = i;
            tkAccessible->focusedChildWin = win;
            return;
        }
    }

    /* If focus is on the container itself, report CHILDID_SELF. */
    if (win == tkAccessible->win) {
        NotifyWinEvent(EVENT_OBJECT_FOCUS, tkAccessible->hwnd, OBJID_CLIENT, CHILDID_SELF);
        tkAccessible->focusedIndex = -1;
        tkAccessible->focusedChildWin = NULL;
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
  HWND hwnd = NULL;

  if (objc != 2) {
    Tcl_WrongNumArgs(interp, 1, objv, "window");
    return TCL_ERROR;
  }

  InitTkAccessibleTable();
  InitHwndToTkWindowTable();
  
  char *windowName = Tcl_GetString(objv[1]);
  Tk_Window tkwin = Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));

  if (tkwin == NULL) {
    Tcl_SetResult(interp, "Invalid window name.", TCL_STATIC);
    return TCL_ERROR;
  }
  
  /* 
   * The root widget/toplevel must have an associated HWND 
   * for accessibility. Child widgets are "virtual" accessible
   * objects without an associated HWND.
   */

  if (Tk_IsTopLevel(tkwin)) { 
    hwnd = Tk_GetHWND(Tk_WindowId(tkwin)); 
  } else {
    hwnd = NULL;
  }	
  TkWinAccessible *accessible = CreateTkAccessible(interp, hwnd, windowName);
  if (accessible == NULL) {
    Tcl_SetResult(interp, "Failed to create accessible object.", TCL_STATIC);
    return TCL_OK;
  }

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
  /*Initialize COM interaction. */
  HRESULT hr = CoInitialize(NULL);
  if (FAILED(hr)) {
    return TCL_ERROR;
  }
	
  Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", TkWinAccessibleObjCmd, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", EmitSelectionChanged, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader", IsScreenReaderRunning, NULL, NULL);
  
  /* Clean up COM interaction. */
  Tcl_CreateExitHandler(CoUninitializeExitHandler, NULL);
	
  return TCL_OK;
}
