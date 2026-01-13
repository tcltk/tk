/*
 * tkWinAccessibility.c
 *
 *    This file implements the platform-native Microsoft Active
 *    Accessibility API for Tk on Windows and supports UI Automation
 *    through the MSAA-UIA bridge provided by Windows.
 *
 * Copyright (c) 2024-2025 Kevin Walzer
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
#include <tlhelp32.h>
#include <tchar.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <limits.h>

/*
 *----------------------------------------------------------------------
 *
 * Data definitions for MSAA-Tk integration.
 *
 *----------------------------------------------------------------------
 */

/* Define global lock constants. */
static CRITICAL_SECTION TkGlobalLock;
static INIT_ONCE TkInitOnce = INIT_ONCE_STATIC_INIT;
#define TkGlobalLock()   EnterCriticalSection(&TkGlobalLock)
#define TkGlobalUnlock() LeaveCriticalSection(&TkGlobalLock)

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
    LONG refCount;
} TkRootAccessible;

/*
 * Map script-level roles to C roles for MSAA.
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
    {"Notebook", ROLE_SYSTEM_PAGETABLIST},
    {"Progressbar", ROLE_SYSTEM_PROGRESSBAR},
    {"Radiobutton", ROLE_SYSTEM_RADIOBUTTON},
    {"Scale", ROLE_SYSTEM_SLIDER},
    {"Scrollbar", ROLE_SYSTEM_SCROLLBAR},
    {"Spinbox", ROLE_SYSTEM_SPINBUTTON},
    {"Table", ROLE_SYSTEM_TABLE},
    {"Text", ROLE_SYSTEM_TEXT},
    {"Tree", ROLE_SYSTEM_OUTLINE},
    {"Toggleswitch", ROLE_SYSTEM_CHECKBUTTON},
    {NULL, 0}
};

/* Hash table for managing accessibility attributes. */
extern Tcl_HashTable *TkAccessibilityObject;

/* Hash tables for linking Tk windows to accessibility object and HWND. */
static Tcl_HashTable *tkAccessibleTable;
static bool tkAccessibleTableInitialized = false;
static Tcl_HashTable *toplevelChildTables = NULL;

/* Data structures for managing execution on main thread. */
typedef void (*MainThreadFunc)(int num_args, void** args);

typedef struct {
    Tcl_Event header;
    MainThreadFunc func;
    int num_args;
    void* args[6];
    HANDLE doneEvent;
} MainThreadSyncEvent;

/*
 * Need main thread, main interp, and command struct
 * for accessible operations on main thread
 * defined here.
 */
static Tcl_ThreadId mainThreadId;
static volatile HRESULT mainThreadResult = E_FAIL;

typedef struct {
    Tcl_Event header;
    char *command;
    Tk_Window win;
} ActionEvent;

/*
 *----------------------------------------------------------------------
 *
 * Prototypes for toplevel MSAA objects. These will always run on background
 * threads. Any calls into Tcl/Tk functions must be guarded with a global
 * thread lock or explicitly pushed to the main thread because Tk is not
 * thread safe.
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

/* VTable for MSAA root accessible. */
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
 * Prototypes for child widgets using MSAA childId. These will be called
 * from a global lock or explicitly run on the main thread.
 *
 *----------------------------------------------------------------------
 */

static HRESULT TkAccRole(Tk_Window win, VARIANT *pvarRole);
static void ComputeAndCacheCheckedState(Tk_Window win, Tcl_Interp *interp);
static HRESULT TkAccState(Tk_Window win, VARIANT *pvarState);
static void TkAccFocus(int num_args, void **args);
static HRESULT TkAccDescription(Tk_Window win, BSTR *pDesc);
static HRESULT TkAccValue(Tk_Window win, BSTR *pValue);
static void TkDoDefaultAction(int num_args, void **args);
static HRESULT TkAccHelp(Tk_Window win, BSTR *pszHelp);
static int TkAccChildCount(Tk_Window win);
static int ActionEventProc(Tcl_Event *ev, int flags);
static HRESULT TkAccChild_GetRect(Tcl_Interp *interp, char *path, RECT *rect);


/*
 *----------------------------------------------------------------------
 *
 * Prototypes for functions to manage threading activities.
 *
 *----------------------------------------------------------------------
 */
int ExecuteOnMainThreadSync(Tcl_Event *ev, int flags);
void RunOnMainThreadSync(MainThreadFunc func, int num_args, ...);
void HandleWMGetObjectOnMainThread(int num_args, void **args);
BOOL CALLBACK InitGlobalLockOnce(PINIT_ONCE InitOnce, PVOID param, PVOID *Context);
void EnsureGlobalLockInitialized(void);

/*
 *----------------------------------------------------------------------
 *
 * Prototypes of Tk functions that support MSAA integration
 * and help implement the script-level API.
 *
 *----------------------------------------------------------------------
 */

void InitTkAccessibleTable(void);
void InitChildIdTable(void);
void ClearChildIdTableForToplevel(Tk_Window toplevel);
TkRootAccessible *GetTkAccessibleForWindow(Tk_Window win);
static TkRootAccessible *CreateRootAccessible(Tcl_Interp *interp, HWND hwnd, const char *pathName);
static void SetChildIdForTkWindow(Tk_Window win, int id, Tcl_HashTable *childIdTable);
static int GetChildIdForTkWindow(Tk_Window win, Tcl_HashTable *childIdTable);
Tk_Window GetToplevelOfWidget(Tk_Window tkwin);
static Tcl_HashTable *GetChildIdTableForToplevel(Tk_Window toplevel);
Tk_Window GetTkWindowForChildId(int id, Tk_Window toplevel);
int IsScreenReaderRunning(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const *objv);
static int EmitSelectionChanged(void *clientData,Tcl_Interp *ip, Tcl_Size objc, Tcl_Obj *const *objv);
static int EmitFocusChanged(void *cd, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const *objv);
void TkRootAccessible_RegisterForCleanup(Tk_Window tkwin, void *tkAccessible);
static void TkRootAccessible_DestroyHandler(void *clientData, XEvent *eventPtr);
static void AssignChildIdsRecursive(Tk_Window win, int *nextId, Tcl_Interp *interp, Tk_Window toplevel);
void InitAccessibilityMainThread(void);
int TkRootAccessibleObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const *objv);
int TkWinAccessiblity_Init(Tcl_Interp *interp);

/*
 *----------------------------------------------------------------------
 *
 * Glue functions to the IAccessible COM API - toplevels.
 *
 *----------------------------------------------------------------------
 */

/* Empty stub functions required by MSAA. */
HRESULT STDMETHODCALLTYPE
TkRootAccessible_get_accHelpTopic(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(BSTR *), /* pszHelpFile */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(long *)) /* pidTopic */
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE
TkRootAccessible_get_accKeyboardShortcut(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(BSTR *)) /* pszKeyboardShortcut */
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE
TkRootAccessible_get_accSelection(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT *)) /*pvarChildren */
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE
TkRootAccessible_accNavigate(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(long), /* navDir */
    TCL_UNUSED(VARIANT), /* varStart */
    TCL_UNUSED(VARIANT *)) /* pvarEndUpAt */
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE
TkRootAccessible_accHitTest(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(long), /* xLeft */
    TCL_UNUSED(long), /* yTop */
    TCL_UNUSED(VARIANT *)) /* pvarChild */
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE
TkRootAccessible_put_accName(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(BSTR)) /* szName */
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE
TkRootAccessible_put_accValue(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(BSTR)) /* szValue */
{
    return E_NOTIMPL;
}

/*
 *----------------------------------------------------------------------
 * Begin active MSAA functions. These are run on a background thread
 * and if they need to call into Tk to read data, a global thread lock
 * will be applied.
 *----------------------------------------------------------------------
 */

static HRESULT STDMETHODCALLTYPE
TkRootAccessible_QueryInterface(
    IAccessible *this,
    REFIID riid,
    void **ppvObject)
{
    if (!ppvObject) return E_INVALIDARG;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IAccessible)) {
	*ppvObject = this;
	TkRootAccessible_AddRef(this);
	return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}

/* Function to add memory reference to the MSAA object. */
static ULONG STDMETHODCALLTYPE
TkRootAccessible_AddRef(
    IAccessible *this)
{
    if (!this) return E_INVALIDARG;
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    return InterlockedIncrement(&tkAccessible->refCount);
}

/* Function to free the MSAA object. */
static ULONG STDMETHODCALLTYPE
TkRootAccessible_Release(
    IAccessible *this)
{
    if (!this) return E_INVALIDARG;
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    ULONG count = InterlockedDecrement(&tkAccessible->refCount);
    if (count == 0) {
	TkGlobalLock();
	if (tkAccessible->win && tkAccessibleTableInitialized) {
	    Tcl_HashEntry *entry = Tcl_FindHashEntry(tkAccessibleTable, tkAccessible->win);
	    if (entry) {
		Tcl_DeleteHashEntry(entry);
	    }
	}
	if (tkAccessible->pathName) {
	    Tcl_Free(tkAccessible->pathName);
	    tkAccessible->pathName = NULL;
	}
	Tcl_Free(tkAccessible);
	TkGlobalUnlock();
    }
    return count;
}

/* The number of type information interfaces provided by the object. */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_GetTypeInfoCount(
    TCL_UNUSED(IAccessible *), /* this */
    UINT *pctinfo)
{
    if (!pctinfo) return E_INVALIDARG;
    *pctinfo = 1;
    return S_OK;
}

/*
 * Retrieves the type information for an object, which can then be used
 * to get the type information for an interface.
 */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_GetTypeInfo(
    TCL_UNUSED(IAccessible *), /* this */
    UINT iTInfo,
    LCID lcid,
    ITypeInfo **ppTInfo)
{
    if (!ppTInfo) return E_INVALIDARG;
    *ppTInfo = NULL;
    if (iTInfo != 0) return DISP_E_BADINDEX;

    ITypeLib *pTypeLib = NULL;
    HRESULT hr = LoadRegTypeLib(&LIBID_Accessibility, 1, 1, lcid, &pTypeLib);
    if (FAILED(hr)) return hr;

    ITypeInfo *pTypeInfo = NULL;
    hr = pTypeLib->lpVtbl->GetTypeInfoOfGuid(pTypeLib, &IID_IAccessible, &pTypeInfo);
    pTypeLib->lpVtbl->Release(pTypeLib);
    if (FAILED(hr)) return hr;

    *ppTInfo = pTypeInfo;
    return S_OK;
}

/*
 * Maps a single member and an optional set of argument names to a
 * corresponding set of integer DISPIDs, which can be used on subsequent calls
 * to Invoke.
 */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_GetIDsOfNames(
    IAccessible *this,
    TCL_UNUSED(REFIID), /* riid */
    LPOLESTR *rgszNames,
    UINT cNames,
    LCID lcid,
    DISPID *rgDispId)
{
    if (!rgszNames || !rgDispId) return E_INVALIDARG;
    ITypeInfo *pTypeInfo = NULL;
    HRESULT hr = TkRootAccessible_GetTypeInfo(this, 0, lcid, &pTypeInfo);
    if (FAILED(hr) || !pTypeInfo) return E_FAIL;

    hr = DispGetIDsOfNames(pTypeInfo, rgszNames, cNames, rgDispId);
    pTypeInfo->lpVtbl->Release(pTypeInfo);
    return hr;
}

/* Provides access to properties and methods exposed by an MSAA object. */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_Invoke(
    IAccessible *this,
    DISPID dispIdMember,
    TCL_UNUSED(REFIID), /* riid */
    TCL_UNUSED(LCID), /* lcid */
    TCL_UNUSED(WORD), /* wFlags */
    TCL_UNUSED(DISPPARAMS *), /* pDispParams */
    VARIANT *pVarResult,
    TCL_UNUSED(EXCEPINFO *), /* pExcepInfo */
    TCL_UNUSED(UINT *)) /* puArgErr */
{
    if (!pVarResult) return E_INVALIDARG;
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
    case DISPID_ACC_DEFAULTACTION:
	return TkRootAccessible_get_accDefaultAction(this, selfVar, &pVarResult->bstrVal);
    case DISPID_ACC_DODEFAULTACTION:
	return TkRootAccessible_accDoDefaultAction(this, selfVar);
    case DISPID_ACC_FOCUS:
	return TkRootAccessible_get_accFocus(this, pVarResult);
    }
    return S_OK;
}

/*
 * Function to map accessible name to MSAA. We pass the description string
 * to the name property so that we can get correct labeling on both NVDA
 * and Narrator.
 */

static HRESULT STDMETHODCALLTYPE
TkRootAccessible_get_accName(
    IAccessible *this,
    VARIANT varChild,
    BSTR *pszName)
{
    if (!pszName) return E_INVALIDARG;
    *pszName = NULL;

    TkGlobalLock();
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    if (!tkAccessible->toplevel) {
	TkGlobalUnlock();
	return E_INVALIDARG;
    }

    /* Toplevel. */
    if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
	HWND hwnd = Tk_GetHWND(Tk_WindowId(tkAccessible->toplevel));
	int wlen = GetWindowTextLengthW(hwnd);
	WCHAR *wbuf = (WCHAR *)Tcl_Alloc((wlen + 1) * sizeof(WCHAR));

	/* Read the actual UTF-16 title. */
	if (!GetWindowTextW(hwnd, wbuf, wlen + 1)) {
	    Tcl_Free(wbuf);
	    *pszName = SysAllocString(L"");
	    TkGlobalUnlock();
	    return S_OK;
	}
	*pszName = SysAllocString(wbuf);
	Tcl_Free(wbuf);
	TkGlobalUnlock();
	return S_OK;
    }


    /* Child widgets - return description. */
    if (varChild.vt == VT_I4 && varChild.lVal > 0) {
	Tk_Window child = GetTkWindowForChildId(varChild.lVal, tkAccessible->toplevel);
	if (child) {
	    HRESULT hr = TkAccDescription(child, pszName);
	    TkGlobalUnlock();
	    return hr;
	}
    }

    TkGlobalUnlock();
    return E_INVALIDARG;
}

/* Function to map accessible role to MSAA. For toplevels, return ROLE_SYSTEM_WINDOW. */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_get_accRole(
    IAccessible *this,
    VARIANT varChild,
    VARIANT *pvarRole)
{
    if (!pvarRole) return E_INVALIDARG;
    if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
	pvarRole->vt = VT_I4;
	pvarRole->lVal = ROLE_SYSTEM_WINDOW;
	return S_OK;
    }

    TkGlobalLock();
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    if (varChild.vt == VT_I4 && varChild.lVal > 0) {
	Tk_Window child = GetTkWindowForChildId(varChild.lVal, tkAccessible->toplevel);
	if (!child) {
	    TkGlobalUnlock();
	    return E_INVALIDARG;
	}
	HRESULT hr = TkAccRole(child, pvarRole);
	TkGlobalUnlock();
	return hr;
    }
    TkGlobalUnlock();
    return E_INVALIDARG;
}

/* Function to map accessible state to MSAA. For toplevel, return STATE_SYSTEM_FOCUSABLE. */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_get_accState(
    IAccessible *this,
    VARIANT varChild,
    VARIANT *pvarState)
{
    if (!pvarState) return E_INVALIDARG;
    if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
	pvarState->vt = VT_I4;
	pvarState->lVal = STATE_SYSTEM_FOCUSABLE;
	return S_OK;
    }

    TkGlobalLock();
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    if (varChild.vt == VT_I4 && varChild.lVal > 0) {
	Tk_Window child = GetTkWindowForChildId(varChild.lVal, tkAccessible->toplevel);
	if (!child) {
	    TkGlobalUnlock();
	    return E_INVALIDARG;
	}
	HRESULT hr = TkAccState(child, pvarState);
	TkGlobalUnlock();
	return hr;
    }
    TkGlobalUnlock();
    return DISP_E_MEMBERNOTFOUND;
}

/* Function to map accessible value to MSAA. For toplevel, return NULL. */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_get_accValue(
    IAccessible *this,
    VARIANT varChild,
    BSTR *pszValue)
{
    if (!pszValue) return E_INVALIDARG;
    if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
	*pszValue = NULL;
	return DISP_E_MEMBERNOTFOUND;
    }

    TkGlobalLock();
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    if (varChild.vt == VT_I4 && varChild.lVal > 0) {
	Tk_Window child = GetTkWindowForChildId(varChild.lVal, tkAccessible->toplevel);
	if (!child) {
	    TkGlobalUnlock();
	    return E_INVALIDARG;
	}
	HRESULT hr = TkAccValue(child, pszValue);
	TkGlobalUnlock();
	return hr;
    }
    TkGlobalUnlock();
    return DISP_E_MEMBERNOTFOUND;
}

/* Function to get accessible parent. For toplevel, return NULL. */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_get_accParent(
    TCL_UNUSED(IAccessible *), /* this */
    IDispatch **ppdispParent)
{
    if (!ppdispParent) return E_INVALIDARG;
    *ppdispParent = NULL;
    return S_OK;
}

/* Function to get number of accessible children to MSAA. */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_get_accChildCount(
    IAccessible *this,
    LONG *pcChildren)
{
    if (!pcChildren) return E_INVALIDARG;
    TkGlobalLock();
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    if (!tkAccessible->toplevel) {
	TkGlobalUnlock();
	*pcChildren = 0;
	return S_FALSE;
    }
    int count = TkAccChildCount(tkAccessible->toplevel);
    TkGlobalUnlock();

    *pcChildren = count < 0 ? 0 : count;
    return S_OK;
}

/* Function to get accessible children to MSAA. */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_get_accChild(
    IAccessible *this,
    VARIANT varChild,
    IDispatch **ppdispChild)
{
    if (!ppdispChild) return E_INVALIDARG;
    *ppdispChild = NULL;
    if (varChild.vt != VT_I4 || varChild.lVal <= 0) return E_INVALIDARG;

    TkGlobalLock();
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    if (!tkAccessible->toplevel) {
	TkGlobalUnlock();
	return E_INVALIDARG;
    }
    ClearChildIdTableForToplevel(tkAccessible->toplevel);
    int nextId = 1;
    AssignChildIdsRecursive(tkAccessible->toplevel, &nextId, tkAccessible->interp, tkAccessible->toplevel);
    Tk_Window childWin = GetTkWindowForChildId(varChild.lVal, tkAccessible->toplevel);
    if (!childWin) {
	TkGlobalUnlock();
	return E_INVALIDARG;
    }
    TkGlobalUnlock();
    return S_OK;
}

/* Function to get accessible frame to MSAA. */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_accLocation(
    IAccessible *this,
    LONG *pxLeft,
    LONG *pyTop,
    LONG *pcxWidth,
    LONG *pcyHeight,
    VARIANT varChild)
{
    if (!pxLeft || !pyTop || !pcxWidth || !pcyHeight) return E_INVALIDARG;
    TkGlobalLock();
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    if (!tkAccessible->toplevel || !tkAccessible->hwnd) {
	TkGlobalUnlock();
	return E_INVALIDARG;
    }
    if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
	RECT clientRect;
	GetClientRect(tkAccessible->hwnd, &clientRect);
	POINT screenCoords = { clientRect.left, clientRect.top };
	MapWindowPoints(tkAccessible->hwnd, HWND_DESKTOP, &screenCoords, 1);
	*pxLeft = screenCoords.x;
	*pyTop = screenCoords.y;
	*pcxWidth = clientRect.right - clientRect.left;
	*pcyHeight = clientRect.bottom - clientRect.top;
	TkGlobalUnlock();
	return S_OK;
    }
    if (varChild.vt == VT_I4 && varChild.lVal > 0) {
	Tk_Window child = GetTkWindowForChildId(varChild.lVal, tkAccessible->toplevel);
	if (!child) {
	    TkGlobalUnlock();
	    return E_INVALIDARG;
	}
	RECT rect = { 0 };
	HRESULT hr = TkAccChild_GetRect(tkAccessible->interp, Tk_PathName(child), &rect);
	if (hr == S_OK) {
	    *pxLeft = rect.left;
	    *pyTop = rect.top;
	    *pcxWidth = rect.right - rect.left;
	    *pcyHeight = rect.bottom - rect.top;
	    TkGlobalUnlock();
	    return S_OK;
	}
    }
    TkGlobalUnlock();
    return E_INVALIDARG;
}

/* Function to set accessible selection on Tk widget. */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_accSelect(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(long), /* flags */
    TCL_UNUSED(VARIANT)) /* varChild */
{
    return E_NOTIMPL;
}

/* Function to return default action for role. */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_get_accDefaultAction(
    IAccessible *this,
    VARIANT varChild,
    BSTR *pszDefaultAction)
{
    if (!pszDefaultAction) return E_INVALIDARG;
    *pszDefaultAction = NULL;
    if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) return S_FALSE; /* Top-level window has no default action. */

    VARIANT varRole;
    VariantInit(&varRole);
    TkGlobalLock();
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    HRESULT hr = tkAccessible->lpVtbl->get_accRole(this, varChild, &varRole);
    if (FAILED(hr) || varRole.vt != VT_I4) {
	VariantClear(&varRole);
	TkGlobalUnlock();
	return S_FALSE;
    }
    const wchar_t *action = NULL;
    switch (varRole.lVal) {
    case ROLE_SYSTEM_PUSHBUTTON:
    case ROLE_SYSTEM_RADIOBUTTON:
    case ROLE_SYSTEM_CHECKBUTTON:
	action = L"Press";
	break;
    case ROLE_SYSTEM_TEXT:
	action = L"Edit";
	break;
    case ROLE_SYSTEM_OUTLINE:
    case ROLE_SYSTEM_TABLE:
	action = L"Select";
	break;
    default:
	break;
    }
    VariantClear(&varRole);
    if (action) {
	*pszDefaultAction = SysAllocString(action);
	TkGlobalUnlock();
	return S_OK;
    }
    TkGlobalUnlock();
    return S_FALSE;
}


/* Function to get button press to MSAA. */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_accDoDefaultAction(
    TCL_UNUSED(IAccessible *), /* this */
    VARIANT varChild)
{
    if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) return S_OK;
    if (varChild.vt == VT_I4 && varChild.lVal > 0) {
	mainThreadResult = E_FAIL;
	MainThreadFunc func = (MainThreadFunc)TkDoDefaultAction;
	int childId = varChild.lVal;
	RunOnMainThreadSync(func, 1, INT2PTR(childId));
	return mainThreadResult;
    }
    return E_INVALIDARG;
}

/* Function to get accessible help to MSAA. */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_get_accHelp(
    IAccessible *this,
    VARIANT varChild,
    BSTR* pszHelp)
{
    if (!pszHelp) return E_INVALIDARG;
    TkGlobalLock();
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    if (!tkAccessible->toplevel) {
	TkGlobalUnlock();
	return E_INVALIDARG;
    }
    if (varChild.vt == VT_I4 && varChild.lVal > 0) {
	Tk_Window child = GetTkWindowForChildId(varChild.lVal, tkAccessible->toplevel);
	if (!child) {
	    TkGlobalUnlock();
	    return E_INVALIDARG;
	}
	HRESULT hr = TkAccHelp(child, pszHelp);
	TkGlobalUnlock();
	return hr;
    }
    TkGlobalUnlock();
    return E_INVALIDARG;
}

/* Function to get accessible focus to MSAA. */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_get_accFocus(
    IAccessible *this,
    VARIANT *pvarChild)
{
    if (!pvarChild) return E_INVALIDARG;
    VariantInit(pvarChild);
    TkGlobalLock();
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    if (!tkAccessible->toplevel || !tkAccessible->hwnd) {
	TkGlobalUnlock();
	return E_INVALIDARG;
    }
    HWND hwnd = tkAccessible->hwnd;
    TkGlobalUnlock();
    MainThreadFunc func = (MainThreadFunc)TkAccFocus;
    RunOnMainThreadSync(func, 2, (void*)hwnd, (void*)pvarChild);
    return S_OK;
}

/*
 * Function to get accessible description to MSAA.
 */
static HRESULT STDMETHODCALLTYPE
TkRootAccessible_get_accDescription(
	IAccessible *this,
	VARIANT varChild,
	BSTR *pszDescription)
{
    if (!pszDescription) return E_INVALIDARG;
    TkGlobalLock();
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    if (!tkAccessible->toplevel) {
	TkGlobalUnlock();
	return E_INVALIDARG;
    }
    if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
	*pszDescription = SysAllocString(L"Window");
	TkGlobalUnlock();
	return S_OK;
    }
    if (varChild.vt == VT_I4 && varChild.lVal > 0) {
	Tk_Window child = GetTkWindowForChildId(varChild.lVal, tkAccessible->toplevel);
	if (!child) {
	    TkGlobalUnlock();
	    return E_INVALIDARG;
	}
	HRESULT hr = TkAccDescription(child, pszDescription);
	TkGlobalUnlock();
	return hr;
    }
    TkGlobalUnlock();
    return E_INVALIDARG;
}

/*
 *----------------------------------------------------------------------
 *
 * Glue functions - child widgets.
 *
 *----------------------------------------------------------------------
 */

/* Function to map accessible role to MSAA. */
static HRESULT
TkAccRole(
    Tk_Window win,
    VARIANT *pvarRole)
{
    if (!win || !pvarRole) return E_INVALIDARG;
    TkGlobalLock();

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (ClientData)win);
    if (!hPtr) {
	TkGlobalUnlock();
	return S_FALSE;
    }

    Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
    Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "role");
    if (!hPtr2) {
	TkGlobalUnlock();
	return S_FALSE;
    }

    const char *tkrole = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    LONG result = ROLE_SYSTEM_CLIENT; /* Fallback value */

    for (int i = 0; roleMap[i].tkrole != NULL; i++) {
	if (strcmp(tkrole, roleMap[i].tkrole) == 0) {
	    result = roleMap[i].winrole;
	    break;
	}
    }

    pvarRole->vt = VT_I4;
    pvarRole->lVal = result;

    TkGlobalUnlock();
    return S_OK;
}

/*
 * Helper function to get selected state on check/radiobuttons.
 */

static void
ComputeAndCacheCheckedState(
    Tk_Window win,
    Tcl_Interp *interp)
{
    if (!win || !interp) {
	return;
    }

    /* Look up accessibility attributes table for this window. */
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)win);
    if (!hPtr) {
	return;
    }
    Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);

    /* Find role */
    Tcl_HashEntry *rolePtr = Tcl_FindHashEntry(AccessibleAttributes, "role");
    const char *tkrole = NULL;
    if (rolePtr) {
	tkrole = Tcl_GetString(Tcl_GetHashValue(rolePtr));
    }
    if (!tkrole) {
	return;
    }

    /* Only handle check-like widgets */
    if (strcmp(tkrole, "Checkbutton") != 0 &&
	strcmp(tkrole, "Radiobutton") != 0 &&
	strcmp(tkrole, "Toggleswitch") != 0) {
	return;
    }

    int isChecked = 0;
    const char *path = Tk_PathName(win);

    /* Special-case: ttk::toggleswitch — ALWAYS use instate selected. */
    if (strcmp(tkrole, "Toggleswitch") == 0) {
	Tcl_Obj *stateCmd = Tcl_ObjPrintf("%s instate selected", path);
	if (!stateCmd) return;
	Tcl_IncrRefCount(stateCmd);
	if (Tcl_EvalObjEx(interp, stateCmd, TCL_EVAL_GLOBAL) == TCL_OK) {
	    const char *result = Tcl_GetStringResult(interp);
	    if (result && strcmp(result, "1") == 0) {
		isChecked = 1;
	    }
	}
	Tcl_DecrRefCount(stateCmd);

	/* Proceed to cache/notify below. */
	goto cache_and_notify;
    }

    /*
	 * For Checkbutton and Radiobutton: prefer -variable based detection if present.
     * Note: ttk widgets sometimes auto-create variables — but toggleswitch was handled above.
     */

    Tcl_Obj *varCmd = Tcl_ObjPrintf("%s cget -variable", path);
    if (!varCmd) return;
    Tcl_IncrRefCount(varCmd);

    const char *varName = NULL;
    int haveVarName = 0;
    if (Tcl_EvalObjEx(interp, varCmd, TCL_EVAL_GLOBAL) == TCL_OK) {
	varName = Tcl_GetStringResult(interp);
	if (varName && *varName) {
	    haveVarName = 1;
	}
    } else {
	/* evaluation failed; clean up and return */
	Tcl_DecrRefCount(varCmd);
	return;
    }
    Tcl_DecrRefCount(varCmd);

    if (haveVarName) {
	/* Grab the variable value (global). */
	const char *varVal = Tcl_GetVar(interp, varName, TCL_GLOBAL_ONLY);
	if (varVal) {
	    /* Determine which cget to use: -onvalue for checkbutton, -value for radiobutton. */
	    Tcl_Obj *valueCmd = NULL;
	    if (strcmp(tkrole, "Checkbutton") == 0) {
		valueCmd = Tcl_ObjPrintf("%s cget -onvalue", path);
	    } else if (strcmp(tkrole, "Radiobutton") == 0) {
		valueCmd = Tcl_ObjPrintf("%s cget -value", path);
	    }

	    if (valueCmd) {
		Tcl_IncrRefCount(valueCmd);
		const char *onValue = NULL;
		if (Tcl_EvalObjEx(interp, valueCmd, TCL_EVAL_GLOBAL) == TCL_OK) {
		    onValue = Tcl_GetStringResult(interp);
		}
		Tcl_DecrRefCount(valueCmd);

		if (onValue && varVal && strcmp(varVal, onValue) == 0) {
		    isChecked = 1;
		}
	    }
	} else {
	    /* variable exists but has no value — fall back to instate selected. */
	    Tcl_Obj *stateCmd = Tcl_ObjPrintf("%s instate selected", path);
	    if (!stateCmd) return;
	    Tcl_IncrRefCount(stateCmd);
	    if (Tcl_EvalObjEx(interp, stateCmd, TCL_EVAL_GLOBAL) == TCL_OK) {
		const char *result = Tcl_GetStringResult(interp);
		if (result && strcmp(result, "1") == 0) {
		    isChecked = 1;
		}
	    }
	    Tcl_DecrRefCount(stateCmd);
	}
    } else {
	/* No variable: fall back to widget state (works for ttk and classic when variable not used). */
	Tcl_Obj *stateCmd = Tcl_ObjPrintf("%s instate selected", path);
	if (!stateCmd) return;
	Tcl_IncrRefCount(stateCmd);
	if (Tcl_EvalObjEx(interp, stateCmd, TCL_EVAL_GLOBAL) == TCL_OK) {
	    const char *result = Tcl_GetStringResult(interp);
	    if (result && strcmp(result, "1") == 0) {
		isChecked = 1;
	    }
	}
	Tcl_DecrRefCount(stateCmd);
    }

cache_and_notify:
    /* Cache the checked state as a Tcl_Obj string "0" or "1" in AccessibleAttributes->"value". */
    TkGlobalLock();
    Tcl_HashEntry *valuePtr;
    int newEntry;
    valuePtr = Tcl_CreateHashEntry(AccessibleAttributes, "value", &newEntry);

    char buf[2];
    snprintf(buf, sizeof(buf), "%d", isChecked);
    Tcl_Obj *valObj = Tcl_NewStringObj(buf, -1);
    Tcl_IncrRefCount(valObj);

    if (!newEntry) {
	/* Replace existing value: free previous Tcl_Obj if present. */
	Tcl_Obj *old = (Tcl_Obj *)Tcl_GetHashValue(valuePtr);
	if (old) {
	    Tcl_DecrRefCount(old);
	}
    }
    Tcl_SetHashValue(valuePtr, valObj);
    TkGlobalUnlock();

    /* Notify MSAA about both value and state changes. */
    {
	Tk_Window toplevel = GetToplevelOfWidget(win);
	if (!toplevel) {
	    return;
	}
	Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
	LONG childId = GetChildIdForTkWindow(win, childIdTable);
	if (childId > 0) {
	    HWND hwnd = Tk_GetHWND(Tk_WindowId(toplevel));
	    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, hwnd, OBJID_CLIENT, childId);
	    NotifyWinEvent(EVENT_OBJECT_STATECHANGE, hwnd, OBJID_CLIENT, childId);
	}
    }
}

/* Function to map accessible state to MSAA. */
static HRESULT
TkAccState(
    Tk_Window win,
    VARIANT *pvarState)
{
    if (!win || !pvarState) {
	return E_INVALIDARG;
    }
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	return S_FALSE;
    }
    Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);

    long state = STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_SELECTABLE; /* Reasonable default. */

    Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "state");
    if (hPtr2) {
	const char *stateresult = Tcl_GetString(Tcl_GetHashValue(hPtr2));
	if (strcmp(stateresult, "disabled") == 0) {
	    state = STATE_SYSTEM_UNAVAILABLE;
	}
    }

    /* Check for checked state using cached value. */
    Tcl_HashEntry *rolePtr = Tcl_FindHashEntry(AccessibleAttributes, "role");
    if (rolePtr) {
	const char *tkrole = Tcl_GetString(Tcl_GetHashValue(rolePtr));
	if (strcmp(tkrole, "Checkbutton") == 0 ||
	    strcmp(tkrole, "Radiobutton") == 0 ||
	    strcmp(tkrole, "Toggleswitch") == 0) {
	    Tcl_HashEntry *valuePtr = Tcl_FindHashEntry(AccessibleAttributes, "value");
	    if (valuePtr) {
		const char *value = Tcl_GetString(Tcl_GetHashValue(valuePtr));
		if (value && strcmp(value, "1") == 0) {
		    state |= STATE_SYSTEM_CHECKED;
		}
	    } else {
	    }
	}
    }

    TkWindow *focusPtr = TkGetFocusWin((TkWindow *)win);
    if (focusPtr == (TkWindow *)win) {
	state |= STATE_SYSTEM_FOCUSED;
    }

    pvarState->vt = VT_I4;
    pvarState->lVal = state;
    return S_OK;
}

/* Function to map accessible value to MSAA. */
static HRESULT
TkAccValue(
    Tk_Window win,
    BSTR *pValue)
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

/* Event proc which calls the ActionEventProc procedure. */
static int
ActionEventProc(
    Tcl_Event *ev,
    TCL_UNUSED(int)) /* flags */
{
    ActionEvent *event = (ActionEvent *)ev;
    if (!event || !event->win || !event->command) return 1;
    Tcl_Interp *interp = Tk_Interp(event->win);
    if (!interp) return 1;
    int code = Tcl_EvalEx(interp, event->command, -1, TCL_EVAL_GLOBAL);
    if (code != TCL_OK) return TCL_ERROR;
    Tcl_Free(event->command);
    return 1;
}

/* Function to get button press to MSAA. */
static void
TkDoDefaultAction(
    TCL_UNUSED(int), /* num_args */
    void **args)
{
    int childId = (int)PTR2INT(args[0]);
    ActionEvent *event = NULL;
    if (!childId) {
	mainThreadResult = E_INVALIDARG;
	return;
    }
    TkGlobalLock();
    Tk_Window toplevel = NULL;
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;
    for (entry = Tcl_FirstHashEntry(tkAccessibleTable, &search); entry != NULL; entry = Tcl_NextHashEntry(&search)) {
	TkRootAccessible *acc = (TkRootAccessible *)Tcl_GetHashValue(entry);
	Tk_Window win = GetTkWindowForChildId(childId, acc->toplevel);
	if (win) {
	    toplevel = acc->toplevel;
	    break;
	}
    }
    if (!toplevel) {
	TkGlobalUnlock();
	mainThreadResult = E_INVALIDARG;
	return;
    }
    Tk_Window win = GetTkWindowForChildId(childId, toplevel);
    if (!win) {
	TkGlobalUnlock();
	mainThreadResult = E_INVALIDARG;
	return;
    }
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)win);
    if (!hPtr) {
	TkGlobalUnlock();
	mainThreadResult = E_INVALIDARG;
	return;
    }
    Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
    if (!AccessibleAttributes) {
	TkGlobalUnlock();
	mainThreadResult = E_INVALIDARG;
	return;
    }
    Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "action");
    if (!hPtr2) {
	TkGlobalUnlock();
	mainThreadResult = E_INVALIDARG;
	return;
    }
    const char *action = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    if (!action) {
	TkGlobalUnlock();
	mainThreadResult = E_INVALIDARG;
	return;
    }
    event = (ActionEvent *)Tcl_Alloc(sizeof(ActionEvent));
    if (event == NULL) {
	TkGlobalUnlock();
	mainThreadResult = E_OUTOFMEMORY;
	return;
    }
    event->header.proc = ActionEventProc;
    event->command = (char *)Tcl_Alloc(strlen(action) + 1);
    strcpy(event->command, action);
    event->win = win;

    /* Update checked state and notify MSAA. */
    ComputeAndCacheCheckedState(win, Tk_Interp(win));

    TkGlobalUnlock();
    Tcl_QueueEvent((Tcl_Event *)event, TCL_QUEUE_TAIL);
    mainThreadResult = S_OK;
    return;
}

/* Function to get MSAA focus. */
static void
TkAccFocus(
    TCL_UNUSED(int), /* num_args */
    void **args)
{
    HWND hwnd = (HWND)args[0];
    VARIANT *pvarChild = (VARIANT*)args[1];
    if (!hwnd || !pvarChild) return;
    Tk_Window win = Tk_HWNDToWindow(hwnd);
    if (!win) return;
    Tk_Window toplevel = GetToplevelOfWidget(win);
    if (!toplevel) return;
    TkWindow *focusPtr = TkGetFocusWin((TkWindow *)win);
    Tk_Window focusWin = (Tk_Window)focusPtr;
    if (!focusWin || focusWin == win) {
	pvarChild->vt = VT_I4;
	pvarChild->lVal = CHILDID_SELF;
	return;
    }
    TkGlobalLock();
    ClearChildIdTableForToplevel(toplevel);
    int nextId = 1;
    AssignChildIdsRecursive(toplevel, &nextId, Tk_Interp(win), toplevel);
    int childId = GetChildIdForTkWindow(focusWin, GetChildIdTableForToplevel(toplevel));
    TkGlobalUnlock();
    if (childId > 0) {
	pvarChild->vt = VT_I4;
	pvarChild->lVal = childId;
	return;
    } else {
	pvarChild->vt = VT_I4;
	pvarChild->lVal = CHILDID_SELF;
	return;
    }
}

/* Function to get MSAA description. */
static HRESULT
TkAccDescription(
    Tk_Window win,
    BSTR *pDesc)
{
    if (!win || !pDesc) return E_INVALIDARG;
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) return S_FALSE;
    Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
    Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "description");
    if (!hPtr2) return S_FALSE;
    const char *desc = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    Tcl_DString ds;
    Tcl_DStringInit(&ds);
    *pDesc = SysAllocString(Tcl_UtfToWCharDString(desc, -1, &ds));
    Tcl_DStringFree(&ds);
    return S_OK;
}

/* Function to get MSAA help. */
static HRESULT
TkAccHelp(
    Tk_Window win,
    BSTR *pszHelp)
{
    if (!win || !pszHelp) return E_INVALIDARG;
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) return S_FALSE;
    Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);
    Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "help");
    if (!hPtr2) return S_FALSE;
    const char *help = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    Tcl_DString ds;
    Tcl_DStringInit(&ds);
    *pszHelp = SysAllocString(Tcl_UtfToWCharDString(help, -1, &ds));
    Tcl_DStringFree(&ds);
    return S_OK;
}

/* Function to get number of child window objects. */
static int
TkAccChildCount(
    Tk_Window win)
{
    if (!win) return -1;
    int count = 0;
    TkWindow *child = (TkWindow*)win;
    Tk_Window toplevel = GetToplevelOfWidget(win);
    if (!toplevel) return -1;
    TkWindow *winPtr = (TkWindow *)toplevel;

    /* Step through all child widgets of toplevel to get child count. */
    for (child = winPtr->childList; child != NULL; child = child->nextPtr) {
	if (Tk_IsMapped(child)) count++;
    }

    return count;
}

/* Function to get child rect. */
static HRESULT TkAccChild_GetRect(
    Tcl_Interp *interp,
    char *path,
    RECT *rect)
{
    if (!interp || !path || !rect) return S_FALSE;
    Tk_Window child = Tk_NameToWindow(interp, path, Tk_MainWindow(interp));
    if (!child || !Tk_IsMapped(child)) return S_FALSE;
    int x, y;
    Tk_GetRootCoords(child, &x, &y);
    int w = Tk_Width(child);
    int h = Tk_Height(child);
    rect->left = x;
    rect->top = y;
    rect->right = x + w;
    rect->bottom = y + h;
    return S_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * General/utility functions to help integrate the MSAA API to
 * the Tcl script-level API. We are using HWND's for toplevel windows
 * and unique childIDs for all accessible child widgets. We are using
 * several hash tables to maintain bidrectional mappings between widgets
 * and childIDs.
 *
 *----------------------------------------------------------------------
 */

/* Function to map Tk window to MSAA attributes. */
static TkRootAccessible *CreateRootAccessible(
    Tcl_Interp *interp,
    HWND hwnd,
    const char *pathName)
{
    if (!interp || !hwnd || !pathName) {
	Tcl_SetResult(interp, "Invalid arguments to CreateRootAccessible", TCL_STATIC);
	return NULL;
    }
    Tk_Window win = Tk_NameToWindow(interp, pathName, Tk_MainWindow(interp));
    if (!win) {
	Tcl_SetResult(interp, "Window not found", TCL_STATIC);
	return NULL;
    }
    if (!Tk_IsTopLevel(win)) {
	Tcl_SetResult(interp, "Window is not a toplevel", TCL_STATIC);
	return NULL;
    }
    Tk_MakeWindowExist(win);
    TkRootAccessible *tkAccessible = (TkRootAccessible *)Tcl_Alloc(sizeof(TkRootAccessible));
    if (!tkAccessible) {
	Tcl_SetResult(interp, "Memory allocation failed for TkRootAccessible", TCL_STATIC);
	return NULL;
    }
    tkAccessible->pathName = (char *)Tcl_Alloc(strlen(pathName) + 1);
    if (!tkAccessible->pathName) {
	Tcl_Free(tkAccessible);
	Tcl_SetResult(interp, "Memory allocation failed for pathName", TCL_STATIC);
	return NULL;
    }
    strcpy(tkAccessible->pathName, pathName);
    tkAccessible->lpVtbl = &tkRootAccessibleVtbl;
    tkAccessible->interp = interp;
    tkAccessible->hwnd = hwnd;
    tkAccessible->refCount = 1;
    tkAccessible->win = win;
    tkAccessible->toplevel = win;
    tkAccessible->children = NULL;
    tkAccessible->numChildren = 0;
    Tcl_HashEntry *entry;
    int newEntry;
    TkGlobalLock();
    if (!tkAccessibleTableInitialized) {
	InitTkAccessibleTable();
    }
    entry = Tcl_CreateHashEntry(tkAccessibleTable, win, &newEntry);
    Tcl_SetHashValue(entry, tkAccessible);
    TkGlobalUnlock();
    TkRootAccessible_AddRef((IAccessible*)tkAccessible);
    NotifyWinEvent(EVENT_OBJECT_CREATE, hwnd, OBJID_CLIENT, CHILDID_SELF);
    NotifyWinEvent(EVENT_OBJECT_SHOW, hwnd, OBJID_CLIENT, CHILDID_SELF);
    NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, hwnd, OBJID_CLIENT, CHILDID_SELF);
    return tkAccessible;
}

/* Function to map Tk window to MSAA ID's. */
static void SetChildIdForTkWindow(
    Tk_Window win,
    int id,
    Tcl_HashTable *childIdTable)
{
    if (!win || !childIdTable) return;
    Tcl_HashEntry *entry;
    int newEntry;
    TkGlobalLock();
    entry = Tcl_CreateHashEntry(childIdTable, win, &newEntry);
    Tcl_SetHashValue(entry, INT2PTR(id));
    TkGlobalUnlock();
}

/* Function to retrieve MSAA ID for a specifc Tk window. */
static int GetChildIdForTkWindow(
    Tk_Window win,
    Tcl_HashTable *childIdTable)
{
    if (!win || !childIdTable) return -1;
    Tcl_HashEntry *entry;
    TkGlobalLock();
    entry = Tcl_FindHashEntry(childIdTable, win);
    if (!entry) {
	TkGlobalUnlock();
	return -1;
    }
    int id = PTR2INT(Tcl_GetHashValue(entry));
    TkGlobalUnlock();
    return id;
}

/* Function to retrieve Tk window for a specifc MSAA ID. */
Tk_Window GetTkWindowForChildId(
    int id,
    Tk_Window toplevel)
{
    if (!toplevel) return NULL;
    /*
     * We are looking for a specific child ID within a specific toplevel,
     * each one tracked separately in this hash table.
     */
    Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
    if (!childIdTable) return NULL;
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;
    TkGlobalLock();
    for (entry = Tcl_FirstHashEntry(childIdTable, &search); entry != NULL; entry = Tcl_NextHashEntry(&search)) {
	if (PTR2INT(Tcl_GetHashValue(entry)) == id) {
	    Tk_Window win = (Tk_Window)Tcl_GetHashKey(childIdTable, entry);
	    TkGlobalUnlock();
	    return win;
	}
    }
    TkGlobalUnlock();
    return NULL;
}

/* Function to get child ID table for a toplevel. We are using a separate table for each toplevel. */
static Tcl_HashTable *GetChildIdTableForToplevel(
    Tk_Window toplevel)
{
    if (!toplevel || !toplevelChildTables) return NULL;
    Tcl_HashEntry *entry;
    int newEntry;
    Tcl_HashTable *childIdTable;
    TkGlobalLock();
    entry = Tcl_CreateHashEntry(toplevelChildTables, toplevel, &newEntry);
    if (newEntry) {
	childIdTable = (Tcl_HashTable *)Tcl_Alloc(sizeof(Tcl_HashTable));
	if (!childIdTable) {
	    TkGlobalUnlock();
	    return NULL;
	}
	Tcl_InitHashTable(childIdTable, TCL_ONE_WORD_KEYS);
	Tcl_SetHashValue(entry, childIdTable);
    } else {
	childIdTable = (Tcl_HashTable *)Tcl_GetHashValue(entry);
    }
    TkGlobalUnlock();
    return childIdTable;
}

/* Function to return the toplevel window that contains a given Tk widget. */
Tk_Window GetToplevelOfWidget(
    Tk_Window tkwin)
{
    if (!tkwin) return NULL;
    Tk_Window current = tkwin;
    if (Tk_IsTopLevel(current)) return current;
    while (current != NULL && Tk_WindowId(current) != None) {
	Tk_Window parent = Tk_Parent(current);
	if (parent == NULL || Tk_IsTopLevel(current)) break;
	current = parent;
    }
    return Tk_IsTopLevel(current) ? current : NULL;
}

/* Function to initialize Tk -> MSAA hash table. */
void InitTkAccessibleTable(void)
{
    if (!tkAccessibleTableInitialized) {
	tkAccessibleTable = (Tcl_HashTable *)Tcl_Alloc(sizeof(Tcl_HashTable));
	if (tkAccessibleTable) {
	    Tcl_InitHashTable(tkAccessibleTable, TCL_ONE_WORD_KEYS);
	    tkAccessibleTableInitialized = true;
	}
    }
}

/* Function to initialize childId hash table. */
void InitChildIdTable(void)
{
    if (!toplevelChildTables) {
	toplevelChildTables = (Tcl_HashTable *)Tcl_Alloc(sizeof(Tcl_HashTable));
	if (toplevelChildTables) {
	    Tcl_InitHashTable(toplevelChildTables, TCL_ONE_WORD_KEYS);
	}
    }
}

/* Function to clear childId hash table for a toplevel. */
void ClearChildIdTableForToplevel(
				  Tk_Window toplevel)
{
    if (!toplevel || !toplevelChildTables) return;
    Tcl_HashEntry *entry = Tcl_FindHashEntry(toplevelChildTables, toplevel);
    if (!entry) return;
    Tcl_HashTable *childIdTable = (Tcl_HashTable *)Tcl_GetHashValue(entry);
    Tcl_HashSearch search;
    Tcl_HashEntry *childEntry;
    TkGlobalLock();
    for (childEntry = Tcl_FirstHashEntry(childIdTable, &search); childEntry != NULL; childEntry = Tcl_NextHashEntry(&search)) {
	Tcl_DeleteHashEntry(childEntry);
    }
    Tcl_DeleteHashEntry(entry); /* Remove toplevel entry to prevent memory leaks. */
    Tcl_Free(childIdTable);
    TkGlobalUnlock();
}

/* Function to retrieve accessible object associated with Tk window. */
TkRootAccessible *GetTkAccessibleForWindow(
    Tk_Window win)
{
    if (!win || !tkAccessibleTableInitialized) return NULL;
    Tcl_HashEntry *entry = Tcl_FindHashEntry(tkAccessibleTable, win);
    if (entry) return (TkRootAccessible *)Tcl_GetHashValue(entry);
    return NULL;
}

/* Function to assign childId's dynamically. */
static void AssignChildIdsRecursive(
    Tk_Window win,
    int *nextId,
    Tcl_Interp *interp,
    Tk_Window toplevel)
{
    if (!win || !interp || !toplevel || !Tk_IsMapped(win)) {
	return;
    }
    Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
    if (!childIdTable) {
	return;
    }
    SetChildIdForTkWindow(win, *nextId, childIdTable);
    (*nextId)++;

    /* Initialize checked state for checkbuttons and radiobuttons. */
    ComputeAndCacheCheckedState(win, interp);

    TkWindow *winPtr = (TkWindow *)win;
    for (TkWindow *child = winPtr->childList; child != NULL; child = child->nextPtr) {
	AssignChildIdsRecursive((Tk_Window)child, nextId, interp, toplevel);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Threading functions. These manage the integration of accessibility operations
 * on background threads and Tk execution on the main thread.
 *
 *----------------------------------------------------------------------
 */

/* Handle WM_GETOBJECT call on main thread. */
void HandleWMGetObjectOnMainThread(
    TCL_UNUSED(int), /* num_args */
    void **args)
{
    HWND hwnd = (HWND)args[0];
    WPARAM wParam = (WPARAM)args[1];
    LPARAM lParam = (LPARAM)args[2];
    LRESULT *outResult = (LRESULT *)args[3];
    if (outResult) *outResult = 0;

    Tk_Window tkwin = Tk_HWNDToWindow(hwnd);

    /* Handle MSAA requests. */
    if ((LONG)lParam == OBJID_CLIENT) {
	TkRootAccessible *msaaProvider = GetTkAccessibleForWindow(tkwin);
	if (!msaaProvider) {
	    Tcl_Interp *interp = Tk_Interp(tkwin);
	    if (!interp) return;

	    msaaProvider = CreateRootAccessible(interp, hwnd, Tk_PathName(tkwin));
	    if (msaaProvider) {
		TkRootAccessible_RegisterForCleanup(tkwin, msaaProvider);
	    }
	}

	if (msaaProvider && outResult) {
	    *outResult = LresultFromObject(&IID_IAccessible, wParam, (IUnknown *)msaaProvider);
	}
    }
}

/* Event handler that executes on main thread. */
int ExecuteOnMainThreadSync(
    Tcl_Event *ev,
    TCL_UNUSED(int)) /*flags */
{
    MainThreadSyncEvent *event = (MainThreadSyncEvent *)ev;
    if (!event) return 1;
    switch(event->num_args) {
    case 0: event->func(0, NULL); break;
    case 1: event->func(1, event->args); break;
    case 2: event->func(2, event->args); break;
    case 3: event->func(3, event->args); break;
    case 4: event->func(4, event->args); break;
    case 5: event->func(5, event->args); break;
    }
    SetEvent(event->doneEvent);
    Tcl_Free(event);
    return 1;
}

/* Synchronous execution with variable arguments. */
void RunOnMainThreadSync(
    MainThreadFunc func,
    int num_args, ...)
{
    if (Tcl_GetCurrentThread() == mainThreadId) {
	void *args[6];
	va_list ap;
	va_start(ap, num_args);
	for (int i = 0; i < num_args; i++) {
	    args[i] = va_arg(ap, void*);
	}
	va_end(ap);
	func(num_args, args);
	return;
    }
    MainThreadSyncEvent *event = (MainThreadSyncEvent *)Tcl_Alloc(sizeof(MainThreadSyncEvent));
    if (!event) return;
    event->header.proc = ExecuteOnMainThreadSync;
    event->func = func;
    event->num_args = num_args;
    event->doneEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!event->doneEvent) {
	Tcl_Free(event);
	return;
    }
    va_list ap;
    va_start(ap, num_args);
    for (int i = 0; i < num_args; i++) {
	event->args[i] = va_arg(ap, void*);
    }
    va_end(ap);
    Tcl_ThreadQueueEvent(mainThreadId, (Tcl_Event *)event, TCL_QUEUE_TAIL);
    Tcl_ThreadAlert(mainThreadId);
    DWORD result = WaitForSingleObject(event->doneEvent, 500);
    if (result == WAIT_TIMEOUT) {
	CloseHandle(event->doneEvent);
	Tcl_Free(event);
    }
    CloseHandle(event->doneEvent);
}

/* Initialize during Tcl startup. */
void InitAccessibilityMainThread(void)
{
    mainThreadId = Tcl_GetCurrentThread();
}

/* Initiate global thread lock. */
BOOL CALLBACK InitGlobalLockOnce(
    TCL_UNUSED(PINIT_ONCE), /* InitOnce */
    TCL_UNUSED(PVOID), /* param */
    TCL_UNUSED(PVOID *)) /* Context */
{
    InitializeCriticalSection(&TkGlobalLock);
    return TRUE;
}

/* Wrapper for global thread lock. */
void EnsureGlobalLockInitialized(void)
{
    InitOnceExecuteOnce(&TkInitOnce, InitGlobalLockOnce, NULL, NULL);
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
 *    Returns if screen reader is active or not.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */
int IsScreenReaderRunning(
    TCL_UNUSED(void *), /* clientData */
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size), /* objc */
    TCL_UNUSED(Tcl_Obj *const *)) /* objv */
{
    BOOL screenReader = FALSE;

    /* First check the system-wide flag (covers NVDA, JAWS, etc.) */
    SystemParametersInfo(SPI_GETSCREENREADER, 0, &screenReader, 0);

    if (!screenReader) {
	/* Fallback: explicitly check for Narrator.exe */
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot != INVALID_HANDLE_VALUE) {
	    PROCESSENTRY32 pe;
	    pe.dwSize = sizeof(PROCESSENTRY32);
	    if (Process32First(hSnapshot, &pe)) {
		do {
		    if (_tcsicmp(pe.szExeFile, TEXT("Narrator.exe")) == 0) {
			screenReader = TRUE;
			break;
		    }
		} while (Process32Next(hSnapshot, &pe));
	    }
	    CloseHandle(hSnapshot);
	}
    }

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
 *    Accessibility system is made aware when a selection is changed.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static int EmitSelectionChanged(
    TCL_UNUSED(void *), /* clientData */
    Tcl_Interp *ip,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    if (objc < 2) {
	Tcl_WrongNumArgs(ip, 1, objv, "window?");
	return TCL_ERROR;
    }

    Tk_Window path = Tk_NameToWindow(ip, Tcl_GetString(objv[1]), Tk_MainWindow(ip));
    if (!path) {
	Tcl_SetResult(ip, "Invalid window name", TCL_STATIC);
	return TCL_ERROR;
    }

    Tk_Window toplevel = GetToplevelOfWidget(path);
    if (!toplevel || !Tk_IsTopLevel(toplevel)) {
	Tcl_SetResult(ip, "Window must be in a toplevel", TCL_STATIC);
	return TCL_ERROR;
    }

    Tk_MakeWindowExist(path);

    /* Update checked state. */
    ComputeAndCacheCheckedState(path, ip);

    TkGlobalLock();
    Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
    LONG childId = GetChildIdForTkWindow(path, childIdTable);

    if (childId > 0) {
	HWND hwnd = Tk_GetHWND(Tk_WindowId(toplevel));

	/* Send comprehensive notifications for Narrator compatibility. */
	NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, hwnd, OBJID_CLIENT, childId);
	NotifyWinEvent(EVENT_OBJECT_STATECHANGE, hwnd, OBJID_CLIENT, childId);
	NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, hwnd, OBJID_CLIENT, childId);
	NotifyWinEvent(EVENT_OBJECT_SELECTION, hwnd, OBJID_CLIENT, childId);
    }

    TkGlobalUnlock();
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
 *    None.
 *
 *----------------------------------------------------------------------
 */
void TkRootAccessible_RegisterForCleanup(
    Tk_Window tkwin,
    void *tkAccessible)
{
    if (!tkwin || !tkAccessible) return;
    Tk_CreateEventHandler(tkwin, StructureNotifyMask, TkRootAccessible_DestroyHandler, tkAccessible);
}

/*
 *----------------------------------------------------------------------
 *
 * TkRootAccessible_DestroyHandler --
 *
 * Clean up accessibility element structures when window is destroyed.
 *
 * Results:
 *    Accessibility element is deallocated.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */
static void TkRootAccessible_DestroyHandler(
    void *clientData,
    XEvent *eventPtr)
{
    if (!clientData|| eventPtr->type != DestroyNotify) return;
    TkRootAccessible *tkAccessible = (TkRootAccessible *)clientData;
    if (!tkAccessible || !tkAccessible->toplevel) return;
    TkGlobalLock();

    /* Clean up MSAA table. */
    if (tkAccessibleTableInitialized) {
	Tcl_HashEntry *entry = Tcl_FindHashEntry(tkAccessibleTable, tkAccessible->toplevel);
	if (entry) {
	    Tcl_DeleteHashEntry(entry);
	}
    }

    ClearChildIdTableForToplevel(tkAccessible->toplevel);
    TkRootAccessible_Release((IAccessible *)tkAccessible);
    TkGlobalUnlock();
}

/*
 *----------------------------------------------------------------------
 *
 * EmitFocusChanged --
 *
 * Accessibility system notification when focus changed.
 *
 * Results:
 *    Accessibility system is made aware when focus is changed.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */
static int EmitFocusChanged(
    TCL_UNUSED(void *), /* cd */
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "window");
	return TCL_ERROR;
    }
    const char *path = Tcl_GetString(objv[1]);
    Tk_Window win = Tk_NameToWindow(interp, path, Tk_MainWindow(interp));
    if (!win) {
	Tcl_SetResult(interp, "Invalid window name", TCL_STATIC);
	return TCL_OK;
    }
    Tk_MakeWindowExist(win);
    Tk_Window toplevel = GetToplevelOfWidget(win);
    if (!toplevel || !Tk_IsTopLevel(toplevel)) {
	Tcl_SetResult(interp, "Window must be in a toplevel", TCL_STATIC);
	return TCL_OK;
    }
    TkGlobalLock();
    Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
    if (!childIdTable) {
	Tcl_SetResult(interp, "Failed to get child ID table for toplevel", TCL_STATIC);
	TkGlobalUnlock();
	return TCL_OK;
    }
    ClearChildIdTableForToplevel(toplevel);
    int nextId = 1;
    AssignChildIdsRecursive(toplevel, &nextId, interp, toplevel);
    LONG childId = GetChildIdForTkWindow(win, childIdTable);
    if (childId <= 0) {
	Tcl_AppendResult(interp, "Failed to find child ID for ", path, NULL);
	TkGlobalUnlock();
	return TCL_OK;
    }
    NotifyWinEvent(EVENT_OBJECT_FOCUS, Tk_GetHWND(Tk_WindowId(toplevel)), OBJID_CLIENT, childId);
    TkGlobalUnlock();
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkRootAccessibleObjCmd --
 *
 *    Main command for adding and managing accessibility objects to Tk
 *    widgets on Windows using the Microsoft Active Accessibility API.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *    Tk widgets are now accessible to screen readers.
 *
 *----------------------------------------------------------------------
 */
int TkRootAccessibleObjCmd(
    TCL_UNUSED(void *), /* clientData */
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "window");
	return TCL_ERROR;
    }
    char *windowName = Tcl_GetString(objv[1]);
    Tk_Window tkwin = Tk_NameToWindow(interp, windowName, Tk_MainWindow(interp));
    if (!tkwin) {
	Tcl_SetResult(interp, "Invalid window name", TCL_STATIC);
	return TCL_OK;
    }
    Tk_Window toplevel = GetToplevelOfWidget(tkwin);
    if (!toplevel || !Tk_IsTopLevel(toplevel)) {
	Tcl_SetResult(interp, "Window must be a toplevel", TCL_STATIC);
	return TCL_OK;
    }
    Tk_MakeWindowExist(toplevel);
    HWND hwnd = Tk_GetHWND(Tk_WindowId(toplevel));
    if (!hwnd) {
	Tcl_SetResult(interp, "Failed to get HWND for toplevel", TCL_STATIC);
	return TCL_OK;
    }
    TkGlobalLock();
    TkRootAccessible *accessible = CreateRootAccessible(interp, hwnd, windowName);
    if (!accessible) {
	TkGlobalUnlock();
	Tcl_SetResult(interp, "Unable to create accessible object", TCL_STATIC);
	return TCL_OK;
    }
    TkRootAccessible_RegisterForCleanup(toplevel, accessible);
    Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
    if (!childIdTable) {
	TkRootAccessible_Release((IAccessible *)accessible);
	Tcl_SetResult(interp, "Failed to create child ID table for toplevel", TCL_STATIC);
	TkGlobalUnlock();
	return TCL_OK;
    }
    ClearChildIdTableForToplevel(toplevel);
    int nextId = 1;
    AssignChildIdsRecursive(toplevel, &nextId, interp, toplevel);
    TkGlobalUnlock();
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWinAccessiblity_Init --
 *
 *    Initializes the accessibility module.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *    Accessibility module is now activated.
 *
 *----------------------------------------------------------------------
 */
int TkWinAccessiblity_Init(
    Tcl_Interp *interp)
{
    /* Initialize global lock, hash tables, and main thread. */
    EnsureGlobalLockInitialized();
    TkGlobalLock();
    InitAccessibilityMainThread();
    InitTkAccessibleTable();
    InitChildIdTable();
    TkGlobalUnlock();

    /* Initialize Tcl commands. */
    Tcl_CreateObjCommand2(interp, "::tk::accessible::add_acc_object", TkRootAccessibleObjCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::accessible::emit_selection_change", EmitSelectionChanged, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::accessible::emit_focus_change", EmitFocusChanged, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::accessible::check_screenreader", IsScreenReaderRunning, NULL, NULL);
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
