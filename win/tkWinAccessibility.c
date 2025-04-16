/*
 * tkWinAccessibility.c --
 *
 *	  This file implements the platform-native Microsoft Active 
 *	  Accessibility API for Tk on Windows.  
 *
 * Copyright © 2024-2025 Kevin Walzer/WordTech Communications LLC.
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


/* Define the GUID for the MSAA interface. */
DEFINE_GUID(IID_IAccessible, 0x618736e0, 0x3c3d, 0x11cf, 0x81, 0xc, 0x0, 0xaa, 0x0, 0x38, 0x9b, 0x71);

/* Data declarations used in this file. */
typedef struct {
  IAccessibleVtbl lpVtbl;
  Tcl_Interp *interp;
  HWND hwnd;
  char *pathName;
  LONG refCount;
} TkWinAccessible;

/* Map script-level roles to C roles. */
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

/* Tk window with the accessibility attributes. */
Tk_Window accessible_win;

/* Tcl command passed to event procedure. */
char *callback_command;


/* Protoypes of functions used in this file. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_QueryInterface(IAccessible *this, REFIID riid, void **ppvObject);
static ULONG STDMETHODCALLTYPE TkWinAccessible_AddRef(IAccessible *this);
static ULONG STDMETHODCALLTYPE TkWinAccessible_Release(IAccessible *this);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_GetTypeInfoCount(IAccessible *this, UINT *pctinfo);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_GetTypeInfo(IAccessible *this, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_GetIDsOfNames(IAccessible *this, REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_Invoke(IAccessible *this, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr);

static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accName(IAccessible *this, VARIANT varChild, BSTR *pszName);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accRole(IAccessible *this, VARIANT varChild, VARIANT *pvarRole);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accState(IAccessible *this, VARIANT varChild, VARIANT *pvarState);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accValue(IAccessible *this, VARIANT varChild, BSTR *pszValue);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accParent(IAccessible *this, IDispatch **ppdispParent);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accValue(IAccessible *this, VARIANT varChild, BSTR *pszValue);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accChildCount(IAccessible *this, LONG *pcChildren);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accChild(IAccessible *this, VARIANT varChild, IDispatch **ppdispChild);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accLocation(IAccessible *this, LONG *pxLeft, LONG *pyTop, LONG *pcxWidth, LONG *pcyHeight, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accDoDefaultAction(IAccessible *this, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accHelp(IAccessible *this, VARIANT varChild, BSTR* pszHelp);
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accDescription(IAccessible *this, VARIANT varChild, BSTR *pszDescription);

static int ActionEventProc(XEvent *eventPtr, ClientData clientData);
static TkWinAccessible *create_tk_accessible(Tcl_Interp *interp, HWND hwnd, const char *pathName);
int IsScreenReaderRunning(ClientData clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const argv[]);
int EmitSelectionChanged(ClientData clientData,Tcl_Interp *ip, int objc, Tcl_Obj *const objv[]);
void TkWinAccessible_RegisterForCleanup(Tk_Window tkwin, void *tkAccessible);
static void TkWinAccessible_DestroyHandler(ClientData clientData, XEvent *eventPtr);
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
  NULL,/*get_accHelpTopic*/
  NULL,/*get_accKeyboardShortcut*/
  NULL,/*get_accFocus*/
  NULL,/*get_accSelection*/
  NULL,/*get_accDefaultAction*/
  NULL,/*accSelect*/
  TkWinAccessible_accLocation,
  NULL,/*accNavigate*/
  NULL,/*accHitTest*/
  TkWinAccessible_accDoDefaultAction,
  NULL,/*put_accName*/
  NULL/*put_AccValue*/
};

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

static ULONG STDMETHODCALLTYPE TkWinAccessible_AddRef(IAccessible *this)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
  return ++tkAccessible->refCount;
}

static ULONG STDMETHODCALLTYPE TkWinAccessible_Release(IAccessible *this)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
  if (--tkAccessible->refCount == 0) {
    free(tkAccessible->pathName);
    tkAccessible->hwnd = NULL;
    free(tkAccessible);
    return 0;
  }
  return tkAccessible->refCount;
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
		
    Tk_Window win = accessible_win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
    Tcl_DString ds;
	
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
      return E_INVALIDARG;
    }
	
    /* 
     * Assign the "description" attribute to the name because it is 
     * more detailed - MSAA generally does not provide both the 
     * name and description. 
     */
    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "description");
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
static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accRole(IAccessible *this, VARIANT varChild, VARIANT *pvarRole)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;
  LONG role;

  if (!pvarRole) return E_INVALIDARG;

  /* Check for special cases - menu entries and submenus. */
  if (varChild.vt == VT_I4 && varChild.lVal >= 1) {
    /* Menu item. */
    pvarRole->lVal = ROLE_SYSTEM_MENUITEM;
    return S_OK;
  } else if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
    /* Submenu or popup menu. */
    pvarRole->lVal = ROLE_SYSTEM_MENUPOPUP;
    return S_OK;
  }

  /* Other widgets. */
  if (varChild.vt != VT_I4 || varChild.lVal == CHILDID_SELF) {
    Tk_Window win = accessible_win;
		
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
		
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
      return E_INVALIDARG;
    }
		
    AccessibleAttributes = Tcl_GetHashValue(hPtr);
    hPtr2=Tcl_FindHashEntry(AccessibleAttributes, "role");
    if (!hPtr2) {
      return E_INVALIDARG;
    }
    char *result = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    for (int i = 0; i < sizeof(roleMap); i++) {
      if (strcmp(roleMap[i].tkrole, result) != 0) {
	continue;
      }
      role = roleMap[i].winrole;
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
    Tk_Window win = accessible_win;
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
		
    Tk_Window win = accessible_win;
    Tcl_HashEntry *hPtr, *hPtr2;
    Tcl_HashTable *AccessibleAttributes;
    Tcl_DString ds;
		
    hPtr=Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
      return E_INVALIDARG;
    }
		
    AccessibleAttributes = Tcl_GetHashValue(hPtr);
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
  TkWinAccessible *topLevelAccessible = create_tk_accessible(tkAccessible->interp, hwndTopLevel, Tk_PathName(tkwin));
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
      const char *className = Tk_Class(child);
      if (className && strcmp(className, "Menu") == 0) {
	continue; 
      }
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
	  TkWinAccessible *childAccessible = create_tk_accessible(tkAccessible->interp, child_hwnd, Tk_PathName(child));
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

/* Function to get button press to MSAA. */
static HRESULT STDMETHODCALLTYPE TkWinAccessible_accDoDefaultAction(IAccessible *this, VARIANT varChild)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)this;  
  Tk_Window win = accessible_win;
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
  event->proc = ActionEventProc;
  Tcl_QueueEvent((Tcl_Event *)event, TCL_QUEUE_TAIL);
  return S_OK;  
}

/*
 * Event proc which calls the ActionEventProc procedure.
 */

static int
ActionEventProc(XEvent *eventPtr,
		ClientData clientData)
{
  /*
   * MSVC complains if these parameters are stubbed out
   * with TCL_UNUSED.
   */
  (void) eventPtr;
  (void) clientData;
  
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

  Tk_Window win = accessible_win;
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

static HRESULT STDMETHODCALLTYPE TkWinAccessible_get_accDescription(IAccessible *this, VARIANT varChild, BSTR *pszDescription)
{

  Tk_Window win = accessible_win;
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


/* Function to map Tk window to MSAA attributes. */
static TkWinAccessible *create_tk_accessible(Tcl_Interp *interp, HWND hwnd, const char *pathName)
{
  TkWinAccessible *tkAccessible = (TkWinAccessible *)malloc(sizeof(TkWinAccessible));
  if (tkAccessible) {
    tkAccessible->lpVtbl = tkAccessibleVtbl;
    tkAccessible->interp = interp;
    tkAccessible->hwnd = hwnd;
    tkAccessible->pathName = strdup(pathName);
    tkAccessible->refCount = 1;
  }
  return tkAccessible;
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
	
  accessible_win = path;
	
  HWND hwnd = Tk_GetHWND(Tk_WindowId(accessible_win));
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

  accessible_win = tkwin;

  HWND hwnd = Tk_GetHWND(Tk_WindowId(accessible_win));
  TkWinAccessible *accessible = create_tk_accessible(interp, hwnd, windowName);
  TkWinAccessible_RegisterForCleanup(tkwin, accessible);
	
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
  Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", TkWinAccessibleObjCmd, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", EmitSelectionChanged, NULL, NULL);
  Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader", IsScreenReaderRunning, NULL, NULL);
  return TCL_OK;
}

