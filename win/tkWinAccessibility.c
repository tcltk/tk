
/*
 * tkWinAccessibility.c --
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
#include <UIAutomation.h>

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

/* TkVirtualChildAccessible structure for virtual children. */
typedef struct TkVirtualChildAccessible {
    IAccessibleVtbl *lpVtbl;
    Tk_Window container;      
    LONG childId;             
    LONG role;               
    const char *label;       
    int index;             
    LONG refCount;
} TkVirtualChildAccessible;

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
static int tkAccessibleTableInitialized = 0;
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
HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accSelection(IAccessible *this, VARIANT *pvarChildren);
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
 * Prototypes for virtual child widgets. These have their own 
 * IAccessible implementation. This implementation is necessary
 * for Narrator to pick up virtual objects, like listbox rows,
 * across the MSAA-UIA bridge built into Windows.
 *
 *----------------------------------------------------------------------
 */
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_QueryInterface(IAccessible *this, REFIID riid, void **ppvObject);
static ULONG STDMETHODCALLTYPE TkVirtualChildAccessible_AddRef(IAccessible *this);
static ULONG STDMETHODCALLTYPE TkVirtualChildAccessible_Release(IAccessible *this);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_GetTypeInfoCount(IAccessible *this, UINT *pctinfo);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_GetTypeInfo(IAccessible *this, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_GetIDsOfNames(IAccessible *this, REFIID riid, LPOLESTR *rgszNames, UINT cNames, LCID lcid, DISPID *rgDispId);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_Invoke(IAccessible *this, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accParent(IAccessible *this, IDispatch **ppdispParent);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accChildCount(IAccessible *this, LONG *pcChildren);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accChild(IAccessible *this, VARIANT varChild, IDispatch **ppdispChild);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accName(IAccessible *this, VARIANT varChild, BSTR *pszName);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accValue(IAccessible *this, VARIANT varChild, BSTR *pszValue);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accDescription(IAccessible *this, VARIANT varChild, BSTR *pszDescription);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accRole(IAccessible *this, VARIANT varChild, VARIANT *pvarRole);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accState(IAccessible *this, VARIANT varChild, VARIANT *pvarState);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accHelp(IAccessible *this, VARIANT varChild, BSTR *pszHelp);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accHelpTopic(IAccessible *this, BSTR *pszHelpFile, VARIANT varChild, long *pidTopic);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accKeyboardShortcut(IAccessible *this, VARIANT varChild, BSTR *pszKeyboardShortcut);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accFocus(IAccessible *this, VARIANT *pvarChild);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accSelection(IAccessible *this, VARIANT *pvarChildren);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accDefaultAction(IAccessible *this, VARIANT varChild, BSTR *pszDefaultAction);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_accSelect(IAccessible *this, long flags, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_accLocation(IAccessible *this, LONG *pxLeft, LONG *pyTop, LONG *pcxWidth, LONG *pcyHeight, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_accNavigate(IAccessible *this, long navDir, VARIANT varStart, VARIANT *pvarEndUpAt);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_accHitTest(IAccessible *this, long xLeft, long yTop, VARIANT *pvarChild);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_accDoDefaultAction(IAccessible *this, VARIANT varChild);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_put_accName(IAccessible *this, VARIANT varChild, BSTR szName);
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_put_accValue(IAccessible *this, VARIANT varChild, BSTR szValue);
static TkVirtualChildAccessible *CreateVirtualChildAccessible(Tk_Window container, LONG childId, LONG role, const char *label, int index);
static HRESULT GetVirtualItemRect(Tcl_Interp *interp, Tk_Window container, int index, RECT *rect);
static void EnsureVirtualChildrenCreated(Tcl_Interp *interp, Tk_Window container);

/* VTable for virtual child accessible objects. */
static IAccessibleVtbl tkVirtualChildAccessibleVtbl = {
    TkVirtualChildAccessible_QueryInterface,
    TkVirtualChildAccessible_AddRef,
    TkVirtualChildAccessible_Release,
    TkVirtualChildAccessible_GetTypeInfoCount,
    TkVirtualChildAccessible_GetTypeInfo,
    TkVirtualChildAccessible_GetIDsOfNames,
    TkVirtualChildAccessible_Invoke,
    TkVirtualChildAccessible_get_accParent,
    TkVirtualChildAccessible_get_accChildCount,
    TkVirtualChildAccessible_get_accChild,
    TkVirtualChildAccessible_get_accName,
    TkVirtualChildAccessible_get_accValue,
    TkVirtualChildAccessible_get_accDescription,
    TkVirtualChildAccessible_get_accRole,
    TkVirtualChildAccessible_get_accState,
    TkVirtualChildAccessible_get_accHelp,
    TkVirtualChildAccessible_get_accHelpTopic,
    TkVirtualChildAccessible_get_accKeyboardShortcut,
    TkVirtualChildAccessible_get_accFocus,
    TkVirtualChildAccessible_get_accSelection,
    TkVirtualChildAccessible_get_accDefaultAction,
    TkVirtualChildAccessible_accSelect,
    TkVirtualChildAccessible_accLocation,
    TkVirtualChildAccessible_accNavigate,
    TkVirtualChildAccessible_accHitTest,
    TkVirtualChildAccessible_accDoDefaultAction,
    TkVirtualChildAccessible_put_accName,
    TkVirtualChildAccessible_put_accValue
}; 
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
static LONG TkCreateVirtualChildId(Tcl_Interp *interp, Tk_Window parent, int index, LONG role);
static LONG TkCreateVirtualAccessible(Tcl_Interp *interp, Tk_Window parent, int index, LONG msaaRole);
static BOOL ResolveVirtualChild(Tcl_Interp *interp, Tk_Window container, LONG childId, LONG *outRole, const char **outLabel, int *outIndex);
static void SetChildIdForTkWindow(Tk_Window win, int id, Tcl_HashTable *childIdTable);
static int GetChildIdForTkWindow(Tk_Window win, Tcl_HashTable *childIdTable);
Tk_Window GetToplevelOfWidget(Tk_Window tkwin);
static Tcl_HashTable *GetChildIdTableForToplevel(Tk_Window toplevel);
Tk_Window GetTkWindowForChildId(int id, Tk_Window toplevel);
int IsScreenReaderRunning(void *clientData, Tcl_Interp *interp, int argc, Tcl_Obj *const argv[]);
static int EmitSelectionChanged(void *clientData,Tcl_Interp *ip, int objc, Tcl_Obj *const objv[]);
static int EmitFocusChanged(void *cd, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
void TkRootAccessible_RegisterForCleanup(Tk_Window tkwin, void *tkAccessible);
static void TkRootAccessible_DestroyHandler(void *clientData, XEvent *eventPtr);
static void AssignChildIdsRecursive(Tk_Window win, int *nextId, Tcl_Interp *interp, Tk_Window toplevel);
void InitAccessibilityMainThread(void);
int TkRootAccessibleObjCmd(void *clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int TkWinAccessiblity_Init(Tcl_Interp *interp);

/*
 *----------------------------------------------------------------------
 *
 * Glue functions to the IAccessible COM API - toplevels.
 *
 *----------------------------------------------------------------------
 */

/* Empty stub functions required by MSAA. */
HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accHelpTopic(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(BSTR *), /* pszHelpFile */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(long *)) /* pidTopic */
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accKeyboardShortcut(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(BSTR *)) /* pszKeyboardShortcut */
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkRootAccessible_accNavigate(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(long), /* navDir */
    TCL_UNUSED(VARIANT), /* varStart */
    TCL_UNUSED(VARIANT *)) /* pvarEndUpAt */
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkRootAccessible_accHitTest(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(long), /* xLeft */
    TCL_UNUSED(long), /* yTop */
    TCL_UNUSED(VARIANT *)) /* pvarChild */
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkRootAccessible_put_accName(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(BSTR)) /* szName */
{
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE TkRootAccessible_put_accValue(
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

static HRESULT STDMETHODCALLTYPE TkRootAccessible_QueryInterface(
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
static ULONG STDMETHODCALLTYPE TkRootAccessible_AddRef(
    IAccessible *this)
{
    if (!this) return E_INVALIDARG;
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    return InterlockedIncrement(&tkAccessible->refCount);
}

/* Function to free the MSAA object. */
static ULONG STDMETHODCALLTYPE TkRootAccessible_Release(
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
	   ckfree(tkAccessible->pathName);
	   tkAccessible->pathName = NULL;
	}
	ckfree(tkAccessible);
	TkGlobalUnlock();
    }
    return count;
}

/* The number of type information interfaces provided by the object. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_GetTypeInfoCount(
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
static HRESULT STDMETHODCALLTYPE TkRootAccessible_GetTypeInfo(
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
static HRESULT STDMETHODCALLTYPE TkRootAccessible_GetIDsOfNames(
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
static HRESULT STDMETHODCALLTYPE TkRootAccessible_Invoke(
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
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accName(
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
        const char *title = Tk_PathName(tkAccessible->toplevel);
        Tcl_DString ds;
        Tcl_DStringInit(&ds);
        *pszName = SysAllocString(Tcl_UtfToWCharDString(title, -1, &ds));
        Tcl_DStringFree(&ds);
        TkGlobalUnlock();
        return S_OK;
    }

    /* Check for virtual child FIRST before regular widgets. */
    if (varChild.vt == VT_I4 && varChild.lVal > 0) {
        Tcl_HashSearch search;
        Tcl_HashEntry *h = Tcl_FirstHashEntry(TkAccessibilityObject, &search);
        while (h) {
            Tk_Window container = (Tk_Window)Tcl_GetHashKey(TkAccessibilityObject, h);
            LONG role;
            const char *label;
            int idx;

            if (ResolveVirtualChild(tkAccessible->interp, container, varChild.lVal,
                                    &role, &label, &idx)) {
                /* Found a virtual child - return its specific label. */
                if (label && *label) {
                    Tcl_DString ds;
                    Tcl_DStringInit(&ds);
                    *pszName = SysAllocString(Tcl_UtfToWCharDString(label, -1, &ds));
                    Tcl_DStringFree(&ds);
                } else {
                    wchar_t buf[64];
                    swprintf(buf, _countof(buf), L"Item %d", idx);
                    *pszName = SysAllocString(buf);
                }
                TkGlobalUnlock();
                return *pszName ? S_OK : E_OUTOFMEMORY;
            }
            h = Tcl_NextHashEntry(&search);
        }
    }

    /* Not a virtual child - check for regular widget. */
    if (varChild.vt == VT_I4 && varChild.lVal > 0) {
        Tk_Window child = GetTkWindowForChildId(varChild.lVal, tkAccessible->toplevel);
        if (child) {
            /* For containers with virtual children, return their description/label. */
            VARIANT roleVar;
            VariantInit(&roleVar);
            if (TkAccRole(child, &roleVar) == S_OK && roleVar.vt == VT_I4) {
                LONG role = roleVar.lVal;
                if (role == ROLE_SYSTEM_LIST || 
                    role == ROLE_SYSTEM_TABLE || 
                    role == ROLE_SYSTEM_OUTLINE) {
                    /* This is a container - return its description if it has one. */
                    HRESULT hr = TkAccDescription(child, pszName);
                    VariantClear(&roleVar);
                    TkGlobalUnlock();
                    return hr;
                }
                VariantClear(&roleVar);
            }
            
            /* For other widgets, return description. */
            HRESULT hr = TkAccDescription(child, pszName);
            TkGlobalUnlock();
            return hr;
        }
    }

    TkGlobalUnlock();
    return E_INVALIDARG;
}



/* Function to map accessible role to MSAA. For toplevels, return ROLE_SYSTEM_WINDOW. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accRole(
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
/* Function to map accessible state to MSAA. For toplevel, return STATE_SYSTEM_FOCUSABLE. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accState(
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
        /* Check if it's a virtual child first. */
        Tcl_HashSearch search;
        Tcl_HashEntry *h = Tcl_FirstHashEntry(TkAccessibilityObject, &search);
        while (h) {
            Tk_Window container = (Tk_Window)Tcl_GetHashKey(TkAccessibilityObject, h);
            LONG role;
            const char *label;
            int idx;

            if (ResolveVirtualChild(tkAccessible->interp, container, varChild.lVal,
                                    &role, &label, &idx)) {
                /* Found virtual child - determine if selected. */
                long state = STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_FOCUSABLE;
        
                const char *pathStr = Tk_PathName(container);
                char cmd[512];
        
                /* Determine correct selection command and role using MSAA role - CHECK CONTAINER NOT TOPLEVEL. */
                VARIANT varRole;
                VariantInit(&varRole);

                int isTree = 0;
                int isList = 0;
                int isTable = 0;

                /* CRITICAL FIX: Check container role, not toplevel. */
                if (TkAccRole(container, &varRole) == S_OK && varRole.vt == VT_I4) {
                    LONG containerRole = varRole.lVal;

                    switch (containerRole) {
                    case ROLE_SYSTEM_OUTLINE:       /* Treeview. */
                        isTree = 1;
                        break;

                    case ROLE_SYSTEM_LIST:          /* Listbox. */
                        isList = 1;
                        break;

                    case ROLE_SYSTEM_TABLE:         /* Table widget. */
                        isTable = 1;
                        break;

                    default:
                        break;
                    }
                }  
        
                int selIdx = -1;
                if (isList) {
                    snprintf(cmd, sizeof(cmd), "%s curselection", pathStr);
                } else if (isTable || isTree) {
                    snprintf(cmd, sizeof(cmd), "%s selection", pathStr);
                } else {
                    h = Tcl_NextHashEntry(&search);
                    VariantClear(&varRole);
                    continue;
                }
        
                if (Tcl_Eval(tkAccessible->interp, cmd) == TCL_OK) {
                    Tcl_Obj *res = Tcl_GetObjResult(tkAccessible->interp);
                    Tcl_Size len;
                    if (Tcl_ListObjLength(tkAccessible->interp, res, &len) == TCL_OK && len > 0) {
                        Tcl_Obj *obj;
                        Tcl_ListObjIndex(tkAccessible->interp, res, 0, &obj);
                        Tcl_GetIntFromObj(NULL, obj, &selIdx);
                    }
                }

                if (selIdx == idx) {
                    state |= STATE_SYSTEM_SELECTED;

                    /* Only add FOCUSED if container has keyboard focus. */
                    TkWindow *focusPtr = TkGetFocusWin((TkWindow*)container);
                    if (focusPtr && (Tk_Window)focusPtr == container) {
                        state |= STATE_SYSTEM_FOCUSED;
                    }
                }

                VariantClear(&varRole);

                pvarState->vt = VT_I4;
                pvarState->lVal = state;
                TkGlobalUnlock();
                return S_OK;
            }
            h = Tcl_NextHashEntry(&search);
        }
        
        /* Not a virtual child, check regular widget. */
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
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accValue(
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
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accParent(
    TCL_UNUSED(IAccessible *), /* this */
    IDispatch **ppdispParent)
{
    if (!ppdispParent) return E_INVALIDARG;
    *ppdispParent = NULL;
    return S_OK;
}

/* Function to get number of accessible children to MSAA. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accChildCount(
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

    /* Count regular child widgets. */
    int regularCount = TkAccChildCount(tkAccessible->toplevel);
    int virtualCount = 0;

    /* Count virtual children from containers that support them. */
    TkWindow *winPtr = (TkWindow *)tkAccessible->toplevel;
    for (TkWindow *child = winPtr->childList; child != NULL; child = child->nextPtr) {
        if (Tk_IsMapped((Tk_Window)child)) {
            /* Check if this widget has virtual children. */
            VARIANT roleVar;
            VariantInit(&roleVar);
            if (TkAccRole((Tk_Window)child, &roleVar) == S_OK && roleVar.vt == VT_I4) {
                LONG role = roleVar.lVal;
                if (role == ROLE_SYSTEM_LIST ||
                    role == ROLE_SYSTEM_TABLE ||
                    role == ROLE_SYSTEM_OUTLINE) {
                    /* Get item count from widget. */
                    const char *path = Tk_PathName((Tk_Window)child);
                    char cmd[512];

                    if (role == ROLE_SYSTEM_LIST) {
                        snprintf(cmd, sizeof(cmd), "%s size", path);
                        if (Tcl_Eval(tkAccessible->interp, cmd) == TCL_OK) {
                            int count;
                            if (Tcl_GetIntFromObj(NULL, Tcl_GetObjResult(tkAccessible->interp), &count) == TCL_OK) {
                                virtualCount += count;
                            }
                        }
                    } else {
                        /* For tree/table, count children. */
                        snprintf(cmd, sizeof(cmd), "llength [%s children {}]", path);
                        if (Tcl_Eval(tkAccessible->interp, cmd) == TCL_OK) {
                            int count;
                            if (Tcl_GetIntFromObj(NULL, Tcl_GetObjResult(tkAccessible->interp), &count) == TCL_OK) {
                                virtualCount += count;
                            }
                        }
                    }
                }
                VariantClear(&roleVar);
            }
        }
    }

    TkGlobalUnlock();
    *pcChildren = (regularCount + virtualCount) < 0 ? 0 : (regularCount + virtualCount);
    return S_OK;
}


/* Function to get accessible children to MSAA. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accChild(
    IAccessible *this,
    VARIANT varChild,
    IDispatch **ppdispChild)
{
    if (!ppdispChild) return E_INVALIDARG;
    *ppdispChild = NULL;

    if (varChild.vt != VT_I4 || varChild.lVal <= 0) {
        return E_INVALIDARG;
    }

    TkGlobalLock();
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;

    if (!tkAccessible->toplevel) {
        TkGlobalUnlock();
        return E_INVALIDARG;
    }

    LONG childId = varChild.lVal;

    /* First try to find a regular widget. */
    Tk_Window childWin = GetTkWindowForChildId(childId, tkAccessible->toplevel);
    if (childWin) {
        /* Return the container's IAccessible for regular widgets. */
        TkRootAccessible *childAccessible = GetTkAccessibleForWindow(childWin);
        if (childAccessible) {
            *ppdispChild = (IDispatch *)childAccessible;
            childAccessible->lpVtbl->AddRef((IAccessible *)childAccessible);
            TkGlobalUnlock();
            return S_OK;
        }
    }

    /* If not found as regular widget, try virtual children. */
    Tcl_HashSearch search;
    Tcl_HashEntry *h = Tcl_FirstHashEntry(TkAccessibilityObject, &search);
    while (h) {
        Tk_Window container = (Tk_Window)Tcl_GetHashKey(TkAccessibilityObject, h);
        LONG role;
        const char *label;
        int index;

        if (ResolveVirtualChild(tkAccessible->interp, container, childId, &role, &label, &index)) {
            /* Create virtual child accessible object. */
            TkVirtualChildAccessible *virtualChild =
                CreateVirtualChildAccessible(container, childId, role, label, index);

            if (virtualChild) {
                *ppdispChild = (IDispatch *)virtualChild;
                TkGlobalUnlock();
                return S_OK;
            }
        }
        h = Tcl_NextHashEntry(&search);
    }

    TkGlobalUnlock();
    return E_INVALIDARG;
}


/* Function to get accessible frame to MSAA. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_accLocation(
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
static HRESULT STDMETHODCALLTYPE TkRootAccessible_accSelect(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(long), /* flags */
    TCL_UNUSED(VARIANT)) /* varChild */
{
    return E_NOTIMPL;
}

/* Function to get accessible selection on Tk widget. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accSelection(
	IAccessible *this, 
	VARIANT *pvarChildren)
{
    VariantInit(pvarChildren);
    pvarChildren->vt = VT_EMPTY;

    TkRootAccessible *tkAcc = (TkRootAccessible *)this;
    TkGlobalLock();

    TkWindow *focusPtr = TkGetFocusWin((TkWindow*)tkAcc->win);
    Tk_Window focused = (Tk_Window)focusPtr;
    if (!focused) {
        TkGlobalUnlock();
        return S_FALSE;
    }

    /* Determine correct selection command and role using MSAA role. */
    VARIANT varRole;
    VariantInit(&varRole);

    int isTree = 0;
    int isList = 0;
    int isTable = 0;

    if (TkAccRole(focused, &varRole) == S_OK && varRole.vt == VT_I4) {
	LONG role = varRole.lVal;

	switch (role) {
        case ROLE_SYSTEM_OUTLINE:       /* Treeview. */
            isTree = 1;
            break;

        case ROLE_SYSTEM_LIST:          /* Listbox. */
            isList = 1;
            break;

        case ROLE_SYSTEM_TABLE:         /* Table widget. */
            isTable = 1;
            break;

        default:
            break;
	}
    }

    if (!isList && !isTree && !isTable) {
        TkGlobalUnlock();
        return S_FALSE;
    }

    const char *path = Tk_PathName(focused);
    char cmd[512];
    int index = -1;

    if (isList || isTable) {
        snprintf(cmd, sizeof(cmd), "%s curselection", path);
    } else if (isTree) {
        snprintf(cmd, sizeof(cmd), "%s selection", path);
    }

    if (Tcl_Eval(tkAcc->interp, cmd) == TCL_OK) {
        Tcl_Obj *res = Tcl_GetObjResult(tkAcc->interp);
        Tcl_Size len;
        if (Tcl_ListObjLength(tkAcc->interp, res, &len) == TCL_OK && len > 0) {
            Tcl_Obj *obj;
            Tcl_ListObjIndex(tkAcc->interp, res, 0, &obj);
            Tcl_GetIntFromObj(NULL, obj, &index);
        }
    }

    if (index >= 0) {
        LONG role = isList ? ROLE_SYSTEM_LISTITEM :
	    isTree ? ROLE_SYSTEM_OUTLINEITEM : ROLE_SYSTEM_ROW;

        LONG virtId = TkCreateVirtualAccessible(tkAcc->interp, focused, index, role);
        if (virtId > 0) {
            pvarChildren->vt = VT_I4;
            pvarChildren->lVal = virtId;
            TkGlobalUnlock();
            return S_OK;
        }
    }

    TkGlobalUnlock();
    return S_FALSE;
}

/* Function to return default action for role. */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accDefaultAction(
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
static HRESULT STDMETHODCALLTYPE TkRootAccessible_accDoDefaultAction(
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
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accHelp(
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
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accFocus(
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
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accDescription(
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
static HRESULT TkAccRole(
    Tk_Window win,
    VARIANT *pvarRole)
{
       if (!win || !pvarRole) return E_INVALIDARG;

    VariantInit(pvarRole);
    pvarRole->vt = VT_I4;
    pvarRole->lVal = ROLE_SYSTEM_CLIENT;

    /* Virtual child? */
    LONG role;
    const char *label;
    int idx;
    Tcl_Interp *interp = Tk_Interp(win);  // Get interp from window
    if (interp && ResolveVirtualChild(interp, win, 0, &role, &label, &idx)) {
        pvarRole->lVal = role;
        return S_OK;
    }

    TkGlobalLock();
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (ClientData)win);
    if (!hPtr) {
        TkGlobalUnlock();
        return S_FALSE;
    }

    Tcl_HashTable *attrs = Tcl_GetHashValue(hPtr);
    Tcl_HashEntry *hRole = Tcl_FindHashEntry(attrs, "role");
    if (!hRole) {
        TkGlobalUnlock();
        return S_FALSE;
    }

    const char *tkrole = Tcl_GetString(Tcl_GetHashValue(hRole));
    LONG result = ROLE_SYSTEM_CLIENT;

    for (int i = 0; roleMap[i].tkrole != NULL; i++) {
        if (strcmp(tkrole, roleMap[i].tkrole) == 0) {
            result = roleMap[i].winrole;
            break;
        }
    }

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
	/* evaluation failed; clean up and return. */
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
	   /* Variable exists but has no value — fall back to instate selected. */
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
static HRESULT TkAccState(
    Tk_Window win,
    VARIANT *pvarState)
{
	
    if (!win || !pvarState) {
        return E_INVALIDARG;
    }
    
    long state = STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_SELECTABLE;
    
    /* Check if this is a virtual child. */
    Tcl_Interp *interp = Tk_Interp(win);
    if (interp) {
        LONG role;
        const char *label;
        int idx;
        if (ResolveVirtualChild(interp, win, 0, &role, &label, &idx)) {
            /* Virtual child - check if it's selected. */
            state = STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_FOCUSABLE;
            
            /* Check selection state from the container widget. */
            const char *pathStr = Tk_PathName(win);;
            char cmd[512];
	    
	    /* Determine correct selection command and role using MSAA role. */
	    VARIANT varRole;
	    VariantInit(&varRole);

	    int isTree = 0;
	    int isList = 0;
	    int isTable = 0;

	    if (TkAccRole(win, &varRole) == S_OK && varRole.vt == VT_I4) {
		LONG role = varRole.lVal;

		switch (role) {
		case ROLE_SYSTEM_OUTLINE:       /* Treeview. */
		    isTree = 1;
		    break;

		case ROLE_SYSTEM_LIST:          /* Listbox. */
		    isList = 1;
		    break;

		case ROLE_SYSTEM_TABLE:         /* Table widget. */
		    isTable = 1;
		    break;

		default:
		    break;
		}
	    }
            
            if (isList) {
                snprintf(cmd, sizeof(cmd), "%s curselection", pathStr);
            } else if (isTable || isTree) {
                snprintf(cmd, sizeof(cmd), "%s selection", pathStr);
            } else {
                goto check_regular_widget;
            }
            
            if (Tcl_Eval(interp, cmd) == TCL_OK) {
                Tcl_Obj *res = Tcl_GetObjResult(interp);
                Tcl_Size len;
                if (Tcl_ListObjLength(interp, res, &len) == TCL_OK && len > 0) {
                    Tcl_Obj *obj;
                    Tcl_ListObjIndex(interp, res, 0, &obj);
                    int selIdx;
                    if (Tcl_GetIntFromObj(NULL, obj, &selIdx) == TCL_OK && selIdx == idx) {
                        state |= STATE_SYSTEM_SELECTED | STATE_SYSTEM_FOCUSED;
                    }
                }
            }
            
            pvarState->vt = VT_I4;
            pvarState->lVal = state;
            return S_OK;
        }
    }
    
 check_regular_widget:
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
	return S_FALSE;
    }
    Tcl_HashTable *AccessibleAttributes = Tcl_GetHashValue(hPtr);

    state = STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_SELECTABLE; /* Reasonable default. */

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
static HRESULT TkAccValue(
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
static int ActionEventProc(
    Tcl_Event *ev,
    TCL_UNUSED(int)) /* flags */
{
    ActionEvent *event = (ActionEvent *)ev;
    if (!event || !event->win || !event->command) return 1;
    Tcl_Interp *interp = Tk_Interp(event->win);
    if (!interp) return 1;
    int code = Tcl_EvalEx(interp, event->command, -1, TCL_EVAL_GLOBAL);
    if (code != TCL_OK) return TCL_ERROR;
    ckfree(event->command);
    return 1;
}

/* Function to get button press to MSAA. */
static void TkDoDefaultAction(
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
    event = (ActionEvent *)ckalloc(sizeof(ActionEvent));
    if (event == NULL) {
	TkGlobalUnlock();
	mainThreadResult = E_OUTOFMEMORY;
	return;
    }
    event->header.proc = ActionEventProc;
    event->command = ckalloc(strlen(action) + 1);
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
static void TkAccFocus(
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
static HRESULT TkAccDescription(
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
static HRESULT TkAccHelp(
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
static int TkAccChildCount(Tk_Window win)
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
    TkRootAccessible *tkAccessible = (TkRootAccessible *)ckalloc(sizeof(TkRootAccessible));
    if (!tkAccessible) {
	Tcl_SetResult(interp, "Memory allocation failed for TkRootAccessible", TCL_STATIC);
	return NULL;
    }
    tkAccessible->pathName = ckalloc(strlen(pathName) + 1);
    if (!tkAccessible->pathName) {
	ckfree(tkAccessible);
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

/* Function to create childID for virtual elements like listbox/table/tree rows. */
static LONG TkCreateVirtualChildId(
	Tcl_Interp *interp, 
	Tk_Window parent, 
	int index, 
	LONG role)
{
    if (!interp || !parent) return 0;

    Tk_Window toplevel = GetToplevelOfWidget(parent);
    if (!toplevel) return 0;

    TkGlobalLock();
    Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
    if (!childIdTable) {
        TkGlobalUnlock();
        return 0;
    }

    LONG containerId = GetChildIdForTkWindow(parent, childIdTable);
    LONG virtualId = containerId + index + 1;  /* virtual IDs start after container */

    /* Store in TkAccessibilityObject under parent → "virtual" → index. */
    Tcl_HashEntry *hParent = Tcl_FindHashEntry(TkAccessibilityObject, parent);
    if (!hParent) {
        TkGlobalUnlock();
        return virtualId;
    }

    Tcl_HashTable *attrs = Tcl_GetHashValue(hParent);
    Tcl_HashEntry *hVirt;
    int isNew;
    hVirt = Tcl_CreateHashEntry(attrs, "virtual", &isNew);

    Tcl_HashTable *virtTab;
    if (isNew) {
        virtTab = ckalloc(sizeof(Tcl_HashTable));
        Tcl_InitHashTable(virtTab, TCL_STRING_KEYS);
        Tcl_SetHashValue(hVirt, virtTab);
    } else {
        virtTab = Tcl_GetHashValue(hVirt);
    }

    char key[32];
    snprintf(key, sizeof(key), "%d", index);
    Tcl_HashEntry *hItem = Tcl_CreateHashEntry(virtTab, key, &isNew);

    Tcl_Obj *info = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, info, Tcl_NewLongObj(virtualId));
    Tcl_ListObjAppendElement(interp, info, Tcl_NewLongObj(role));
    /* Label will be added later by TkCreateVirtualAccessible. */
    Tcl_SetHashValue(hItem, info);

    TkGlobalUnlock();
    return virtualId;
}

/* Function to create accessible objects for virtual elements like listbox/table/tree rows. */
/* Function to create accessible objects for virtual elements like listbox/table/tree rows. */
static LONG TkCreateVirtualAccessible(
    Tcl_Interp *interp, 
    Tk_Window parent, 
    int index, 
    LONG msaaRole)
{
    if (!interp || !parent) return 0;

    const char *parent_path = Tk_PathName(parent);
    char cmd[512];
    const char *label = NULL;

    /* Determine correct selection command and role using MSAA role. */
    VARIANT varRole;
    VariantInit(&varRole);

    int isTree = 0;
    int isList = 0;
    int isTable = 0;

    if (TkAccRole(parent, &varRole) == S_OK && varRole.vt == VT_I4) {
        LONG role = varRole.lVal;

        switch (role) {
        case ROLE_SYSTEM_OUTLINE:       /* Treeview. */
            isTree = 1;
            break;

        case ROLE_SYSTEM_LIST:          /* Listbox. */
            isList = 1;
            break;

        case ROLE_SYSTEM_TABLE:         /* Table widget. */
            isTable = 1;
            break;

        default:
            break;
        }
    }

    /* Get label. */
    if (isList) {
        snprintf(cmd, sizeof(cmd), "%s get %d", parent_path, index);
        if (Tcl_Eval(interp, cmd) == TCL_OK) {
            label = Tcl_GetString(Tcl_GetObjResult(interp));
        }
    } else if (isTree || isTable) {
        /* Get ALL item IDs first. */
        snprintf(cmd, sizeof(cmd), "%s children {}", parent_path);
        if (Tcl_Eval(interp, cmd) == TCL_OK) {
            Tcl_Obj *childrenList = Tcl_GetObjResult(interp);
            Tcl_Size count;
            Tcl_Obj **items;
            
            if (Tcl_ListObjGetElements(interp, childrenList, &count, &items) == TCL_OK && 
                index < count) {
                const char *itemid = Tcl_GetString(items[index]);
                
                /* For tree/table, get the text from the item. */
                /* First try -text option. */
                snprintf(cmd, sizeof(cmd), "%s item %s -text", parent_path, itemid);
                if (Tcl_Eval(interp, cmd) == TCL_OK) {
                    const char *text = Tcl_GetString(Tcl_GetObjResult(interp));
                    if (text && *text) {
                        label = text;
                    }
                }
                
                /* If -text is empty, try getting values from first column. */
                if (!label || !*label) {
                    snprintf(cmd, sizeof(cmd), "%s item %s -values", parent_path, itemid);
                    if (Tcl_Eval(interp, cmd) == TCL_OK) {
                        Tcl_Obj *valuesList = Tcl_GetObjResult(interp);
                        Tcl_Size valCount;
                        Tcl_Obj **values;
                        
                        if (Tcl_ListObjGetElements(interp, valuesList, &valCount, &values) == TCL_OK &&
                            valCount > 0) {
                            label = Tcl_GetString(values[0]);
                        }
                    }
                }
            }
        }
    }

    if (!label || !*label) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Item %d", index);
        label = buf;
    }

    LONG virtId = TkCreateVirtualChildId(interp, parent, index, msaaRole);

    /* Store label in virtual hash. */
    Tcl_HashEntry *hParent = Tcl_FindHashEntry(TkAccessibilityObject, parent);
    if (hParent && virtId > 0) {
        Tcl_HashTable *attrs = Tcl_GetHashValue(hParent);
        Tcl_HashEntry *hVirt = Tcl_FindHashEntry(attrs, "virtual");
        if (hVirt) {
            Tcl_HashTable *virtTab = Tcl_GetHashValue(hVirt);
            char key[32];
            snprintf(key, sizeof(key), "%d", index);
            Tcl_HashEntry *hItem = Tcl_FindHashEntry(virtTab, key);
            if (hItem) {
                Tcl_Obj *info = Tcl_GetHashValue(hItem);
                /* Make a copy of the label since it might be in interpreter result. */
                Tcl_ListObjAppendElement(interp, info, Tcl_NewStringObj(label, -1));
            }
        }
    }

    VariantClear(&varRole);
    return virtId;
}

/* Function to resolve virtual child elements and ID's. */
static BOOL ResolveVirtualChild(
	Tcl_Interp *interp,
	Tk_Window container, 
	LONG childId, 
	LONG *outRole, 
	const char **outLabel, 
	int *outIndex)
{
    if (!container || !outRole || !outLabel || !outIndex) return FALSE;

    Tcl_HashEntry *h = Tcl_FindHashEntry(TkAccessibilityObject, container);
    if (!h) return FALSE;

    Tcl_HashTable *attrs = Tcl_GetHashValue(h);
    Tcl_HashEntry *hVirt = Tcl_FindHashEntry(attrs, "virtual");
    if (!hVirt) return FALSE;

    Tcl_HashTable *virtTab = Tcl_GetHashValue(hVirt);
    Tk_Window toplevel = GetToplevelOfWidget(container);
	Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
	if (!childIdTable) return FALSE;
	LONG baseId = GetChildIdForTkWindow(container, childIdTable) + 1;
    int index = (int)(childId - baseId);
    if (index < 0) return FALSE;

    char key[32];
    snprintf(key, sizeof(key), "%d", index);
    Tcl_HashEntry *hItem = Tcl_FindHashEntry(virtTab, key);
    if (!hItem) return FALSE;

    Tcl_Obj *info = Tcl_GetHashValue(hItem);
    Tcl_Size len;

    if (Tcl_ListObjLength(interp, info, &len) != TCL_OK || len < 3) return FALSE;

    Tcl_Obj *obj = NULL;

    /* Index 0: virtual ID. */
    if (Tcl_ListObjIndex(interp, info, 0, &obj) == TCL_OK && obj) {
        long virtId;
        Tcl_GetLongFromObj(interp, obj, &virtId);
    }

    /* Index 1: role. */
    if (Tcl_ListObjIndex(interp, info, 1, &obj) == TCL_OK && obj) {
        long role;
        if (Tcl_GetLongFromObj(interp, obj, &role) == TCL_OK) {
            *outRole = (LONG)role;
        }
    }

    /* Index 2: label. */
    if (Tcl_ListObjIndex(interp, info, 2, &obj) == TCL_OK && obj) {
        *outLabel = Tcl_GetString(obj);
    } else {
        *outLabel = NULL;
    }

    *outIndex = index;
    return TRUE;
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
	childIdTable = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
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
	tkAccessibleTable = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
	if (tkAccessibleTable) {
	   Tcl_InitHashTable(tkAccessibleTable, TCL_ONE_WORD_KEYS);
	   tkAccessibleTableInitialized = 1;
	}
    }
}

/* Function to initialize childId hash table. */
void InitChildIdTable(void)
{
    if (!toplevelChildTables) {
	toplevelChildTables = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
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
    ckfree(childIdTable);
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

/*----------------------------------------------------------------------
 *
 * Functions to create and manage accessibility for virtual child widgets -
 * listbox rows, table rows, and tree nodes. Actual IAccessible objects 
 * are required by Narrator to be spoken. Many of these functions are stubbed
 * out/no-op but are required to be present.
 * 
 *----------------------------------------------------------------------
 */
 
 /* Stubbed functions that return E_NOTIMPL. */

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_GetTypeInfoCount(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(UINT *)) /* pctinfo */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_GetTypeInfo(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(UINT), /* iTInfo */
    TCL_UNUSED(LCID), /* lcid */
    TCL_UNUSED(ITypeInfo **)) /* ppTInfo */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_GetIDsOfNames(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(REFIID), /* riid */
    TCL_UNUSED(LPOLESTR *), /* rgszNames */
    TCL_UNUSED(UINT), /* cNames */
    TCL_UNUSED(LCID), /* lcid */
    TCL_UNUSED(DISPID *)) /* rgDispId */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_Invoke(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(DISPID), /* dispIdMember */
    TCL_UNUSED(REFIID), /* riid */
    TCL_UNUSED(LCID), /* lcid */
    TCL_UNUSED(WORD), /* wFlags */
    TCL_UNUSED(DISPPARAMS *), /* pDispParams */
    TCL_UNUSED(VARIANT *), /* pVarResult */
    TCL_UNUSED(EXCEPINFO *), /* pExcepInfo */
    TCL_UNUSED(UINT *)) /* puArgErr */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accChildCount(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(LONG *)) /* pcChildren */
{
    /* Virtual children have no children of their own. */
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accChild(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(IDispatch **)) /* ppdispChild */
{
    /* Virtual children have no children of their own. */
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accDescription(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(BSTR *)) /* pszDescription */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accHelp(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(BSTR *)) /* pszHelp */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accHelpTopic(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(BSTR *), /* pszHelpFile */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(long *)) /* pidTopic */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accKeyboardShortcut(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(BSTR *)) /* pszKeyboardShortcut */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accFocus(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT *)) /* pvarChild */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accSelection(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT *)) /* pvarChildren */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accDefaultAction(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(BSTR *)) /* pszDefaultAction */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_accSelect(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(long), /* flags */
    TCL_UNUSED(VARIANT)) /* varChild */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_accHitTest(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(long), /* xLeft */
    TCL_UNUSED(long), /* yTop */
    TCL_UNUSED(VARIANT *)) /* pvarChild */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_accDoDefaultAction(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT)) /* varChild */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_put_accName(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(BSTR)) /* szName */
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_put_accValue(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT), /* varChild */
    TCL_UNUSED(BSTR)) /* szValue */
{
    return E_NOTIMPL;
} 

/*
 * Begin actual TkVirtualChildAccessible functions. 
 */
 
 /* QueryInterface for virtual children. */
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_QueryInterface(
    IAccessible *this,
    REFIID riid,
    void **ppvObject)
{
    if (!ppvObject) return E_INVALIDARG;

    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IDispatch) ||
        IsEqualIID(riid, &IID_IAccessible)) {
        *ppvObject = this;
        TkVirtualChildAccessible_AddRef(this);
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

/* AddRef for virtual children. */
static ULONG STDMETHODCALLTYPE TkVirtualChildAccessible_AddRef(IAccessible *this)
{
    if (!this) return 0;
    TkVirtualChildAccessible *virtualChild = (TkVirtualChildAccessible *)this;
    return InterlockedIncrement(&virtualChild->refCount);
}

/* Release for virtual children. */
static ULONG STDMETHODCALLTYPE TkVirtualChildAccessible_Release(IAccessible *this)
{
    if (!this) return 0;
    TkVirtualChildAccessible *virtualChild = (TkVirtualChildAccessible *)this;
    ULONG count = InterlockedDecrement(&virtualChild->refCount);

    if (count == 0) {
        TkGlobalLock();
        /* Clean up any allocated resources. */
        if (virtualChild->label) {
            /* Note: label points to Tcl object string, don't free it. */
        }
        ckfree(virtualChild);
        TkGlobalUnlock();
    }
    return count;
}

/* Get parent - returns the container widget's IAccessible. */
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accParent(
    IAccessible *this,
    IDispatch **ppdispParent)
{
    if (!ppdispParent) return E_INVALIDARG;
    *ppdispParent = NULL;

    TkVirtualChildAccessible *virtualChild = (TkVirtualChildAccessible *)this;
    TkRootAccessible *containerAccessible = GetTkAccessibleForWindow(virtualChild->container);

    if (containerAccessible) {
        *ppdispParent = (IDispatch *)containerAccessible;
        containerAccessible->lpVtbl->AddRef((IAccessible *)containerAccessible);
        return S_OK;
    }

    return E_FAIL;
}

/* Get name for virtual child. */
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accName(
    IAccessible *this,
    VARIANT varChild,
    BSTR *pszName)
{
    if (!pszName) return E_INVALIDARG;
    *pszName = NULL;

    /* Virtual children only support CHILDID_SELF. */
    if (varChild.vt != VT_I4 || varChild.lVal != CHILDID_SELF) {
        return E_INVALIDARG;
    }

    TkVirtualChildAccessible *virtualChild = (TkVirtualChildAccessible *)this;

    if (virtualChild->label && *virtualChild->label) {
        Tcl_DString ds;
        Tcl_DStringInit(&ds);
        *pszName = SysAllocString(Tcl_UtfToWCharDString(virtualChild->label, -1, &ds));
        Tcl_DStringFree(&ds);
        return *pszName ? S_OK : E_OUTOFMEMORY;
    } else {
        /* Fallback name. */
        wchar_t buf[64];
        swprintf(buf, _countof(buf), L"Item %d", virtualChild->index);
        *pszName = SysAllocString(buf);
        return *pszName ? S_OK : E_OUTOFMEMORY;
    }
}

/* Get role for virtual child. */
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accRole(
    IAccessible *this,
    VARIANT varChild,
    VARIANT *pvarRole)
{
    if (!pvarRole) return E_INVALIDARG;

    /* Virtual children only support CHILDID_SELF. */
    if (varChild.vt != VT_I4 || varChild.lVal != CHILDID_SELF) {
        return E_INVALIDARG;
    }

    TkVirtualChildAccessible *virtualChild = (TkVirtualChildAccessible *)this;
    pvarRole->vt = VT_I4;
    pvarRole->lVal = virtualChild->role;
    return S_OK;
}

/* Get state for virtual child. */
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accState(
    IAccessible *this,
    VARIANT varChild,
    VARIANT *pvarState)
{
    if (!pvarState) return E_INVALIDARG;

    /* Virtual children only support CHILDID_SELF. */
    if (varChild.vt != VT_I4 || varChild.lVal != CHILDID_SELF) {
        return E_INVALIDARG;
    }

    TkVirtualChildAccessible *virtualChild = (TkVirtualChildAccessible *)this;
    pvarState->vt = VT_I4;

    /* Base states for virtual children */
    long state = STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_FOCUSABLE;

    /* Check if this virtual child is selected. */
    Tcl_Interp *interp = Tk_Interp(virtualChild->container);
    if (interp) {
        const char *pathStr = Tk_PathName(virtualChild->container);
        char cmd[512];

        /* Determine selection command based on container role.*/
        VARIANT containerRole;
        VariantInit(&containerRole);

        if (TkAccRole(virtualChild->container, &containerRole) == S_OK &&
            containerRole.vt == VT_I4) {

            LONG role = containerRole.lVal;
            int selIdx = -1;

            switch (role) {
                case ROLE_SYSTEM_LIST:
                    snprintf(cmd, sizeof(cmd), "%s curselection", pathStr);
                    break;
                case ROLE_SYSTEM_OUTLINE:
                case ROLE_SYSTEM_TABLE:
                    snprintf(cmd, sizeof(cmd), "%s selection", pathStr);
                    break;
                default:
                    goto set_state;
            }

            TkGlobalLock();
            if (Tcl_Eval(interp, cmd) == TCL_OK) {
                Tcl_Obj *res = Tcl_GetObjResult(interp);
                Tcl_Size len;
                if (Tcl_ListObjLength(interp, res, &len) == TCL_OK && len > 0) {
                    Tcl_Obj *obj;
                    Tcl_ListObjIndex(interp, res, 0, &obj);
                    Tcl_GetIntFromObj(NULL, obj, &selIdx);
                }
            }
            TkGlobalUnlock();

            if (selIdx == virtualChild->index) {
                state |= STATE_SYSTEM_SELECTED;
				/* Only add FOCUSED if container has keyboard focus. */
                TkWindow *focusPtr = TkGetFocusWin((TkWindow*)virtualChild->container);
                if (focusPtr && (Tk_Window)focusPtr == virtualChild->container) {
                    state |= STATE_SYSTEM_FOCUSED;
                }
            }
        }
        VariantClear(&containerRole);
    }

set_state:
    pvarState->lVal = state;
    return S_OK;
}
/* Get value for virtual child (typically same as name for list items. */
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_get_accValue(
    IAccessible *this,
    VARIANT varChild,
    BSTR *pszValue)
{
    /* For virtual children, value is typically the same as name. */
    return TkVirtualChildAccessible_get_accName(this, varChild, pszValue);
}

/* Get location for virtual child. */
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_accLocation(
    IAccessible *this,
    LONG *pxLeft, 
	LONG *pyTop, 
	LONG *pcxWidth, 
	LONG *pcyHeight,
    VARIANT varChild)
{
    if (varChild.vt != VT_I4 || varChild.lVal != CHILDID_SELF) return E_INVALIDARG;
    if (!pxLeft || !pyTop || !pcxWidth || !pcyHeight) return E_INVALIDARG;

    TkVirtualChildAccessible *virtualChild = (TkVirtualChildAccessible *)this;
    Tk_Window container = virtualChild->container;
    Tcl_Interp *interp = Tk_Interp(container);

    RECT rect;
    TkGlobalLock();
    HRESULT hr = GetVirtualItemRect(interp, container, virtualChild->index, &rect);
    TkGlobalUnlock();

    if (hr != S_OK) {
        /* Fallback: container bounds. */
        int x, y;
        Tk_GetRootCoords(container, &x, &y);
        int w = Tk_Width(container);
        int h = Tk_Height(container) / 10;
        int itemY = y + virtualChild->index * h;

        *pxLeft = x;
        *pyTop = itemY;
        *pcxWidth = w;
        *pcyHeight = h;
        return S_OK;
    }

    *pxLeft = rect.left;
    *pyTop = rect.top;
    *pcxWidth = rect.right - rect.left;
    *pcyHeight = rect.bottom - rect.top;

    return S_OK;
}
/* Create a virtual child accessible object. */
static TkVirtualChildAccessible *CreateVirtualChildAccessible(
    Tk_Window container,
    LONG childId,
    LONG role,
    const char *label,
    int index)
{
    TkVirtualChildAccessible *virtualChild =
        (TkVirtualChildAccessible *)ckalloc(sizeof(TkVirtualChildAccessible));

    if (!virtualChild) return NULL;

    virtualChild->lpVtbl = &tkVirtualChildAccessibleVtbl;
    virtualChild->container = container;
    virtualChild->childId = childId;
    virtualChild->role = role;
    virtualChild->label = label;
    virtualChild->index = index;
    virtualChild->refCount = 1;

    return virtualChild;
}

/* Get bounding box for virtual children. */

static HRESULT GetVirtualItemRect(
    Tcl_Interp *interp,
    Tk_Window container,
    int index,
    RECT *rect)
{
    const char *path = Tk_PathName(container);
    char cmdBuf[512];
    VARIANT roleVar;
    VariantInit(&roleVar);

    /* Determine role. */
    if (TkAccRole(container, &roleVar) != S_OK || roleVar.vt != VT_I4) {
        VariantClear(&roleVar);
        return E_FAIL;
    }

    LONG role = roleVar.lVal;
    VariantClear(&roleVar);

    Tcl_Obj *resultObj;
    int x = 0, y = 0, w = 0, h = 0;
    Tcl_Size listLen;

    switch (role) {
    /* Listbox. */
    case ROLE_SYSTEM_LIST: {
        snprintf(cmdBuf, sizeof(cmdBuf), "%s bbox %d", path, index);
        if (Tcl_Eval(interp, cmdBuf) != TCL_OK) return E_FAIL;

        resultObj = Tcl_GetObjResult(interp);
        if (Tcl_ListObjLength(interp, resultObj, &listLen) != TCL_OK || listLen != 4) {
            return E_FAIL;
        }

        Tcl_Obj *elem;

        Tcl_ListObjIndex(interp, resultObj, 0, &elem);
        Tcl_GetIntFromObj(interp, elem, &x);

        Tcl_ListObjIndex(interp, resultObj, 1, &elem);
        Tcl_GetIntFromObj(interp, elem, &y);

        Tcl_ListObjIndex(interp, resultObj, 2, &elem);
        Tcl_GetIntFromObj(interp, elem, &w);

        Tcl_ListObjIndex(interp, resultObj, 3, &elem);
        Tcl_GetIntFromObj(interp, elem, &h);

        break;
    }

    /* Treeview (outline/table). */
    case ROLE_SYSTEM_OUTLINE:
    case ROLE_SYSTEM_TABLE: {
        Tcl_Obj *childrenList, *itemIdObj;
        Tcl_Size count, llen;
        Tcl_Obj *bboxObj, *elem;
        const char *itemId;
        int rowX, rowY, rowW, rowH;

        /* Get item ID for this row. */
        Tcl_Obj *cmd = Tcl_ObjPrintf("%s children {}", path);
        if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL) != TCL_OK) {
            Tcl_DecrRefCount(cmd);
            return E_FAIL;
        }
        Tcl_DecrRefCount(cmd);

        childrenList = Tcl_GetObjResult(interp);
        if (Tcl_ListObjLength(interp, childrenList, &count) != TCL_OK ||
            index < 0 || index >= count) {
            return E_FAIL;
        }

        Tcl_ListObjIndex(interp, childrenList, index, &itemIdObj);
        itemId = Tcl_GetString(itemIdObj);

        /* Get bbox of the row in tree column (#0). */
        cmd = Tcl_ObjPrintf("%s bbox %s", path, itemId);
        if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL) != TCL_OK) {
            Tcl_DecrRefCount(cmd);
            return E_FAIL;
        }
        Tcl_DecrRefCount(cmd);

        bboxObj = Tcl_GetObjResult(interp);
        if (Tcl_ListObjLength(interp, bboxObj, &llen) != TCL_OK || llen != 4) {
            return E_FAIL;
        }

        Tcl_ListObjIndex(interp, bboxObj, 0, &elem); Tcl_GetIntFromObj(interp, elem, &rowX);
        Tcl_ListObjIndex(interp, bboxObj, 1, &elem); Tcl_GetIntFromObj(interp, elem, &rowY);
        Tcl_ListObjIndex(interp, bboxObj, 2, &elem); Tcl_GetIntFromObj(interp, elem, &rowW);
        Tcl_ListObjIndex(interp, bboxObj, 3, &elem); Tcl_GetIntFromObj(interp, elem, &rowH);

        /* Header height adjustment  (critical for Narrator). */
        int headerHeight = 0;
        Tcl_Obj *hdrCmd = Tcl_ObjPrintf("%s bbox heading", path);
        if (Tcl_EvalObjEx(interp, hdrCmd, TCL_EVAL_GLOBAL) == TCL_OK) {
            Tcl_Obj *hdrBox = Tcl_GetObjResult(interp);
            Tcl_Size hlen;
            if (Tcl_ListObjLength(interp, hdrBox, &hlen) == TCL_OK && hlen == 4) {
                Tcl_Obj *hElem;
                Tcl_ListObjIndex(interp, hdrBox, 3, &hElem);
                Tcl_GetIntFromObj(interp, hElem, &headerHeight);
            }
        }
        Tcl_DecrRefCount(hdrCmd);

        /* Full row spans widget width. */
        Tk_Window tkwin = Tk_NameToWindow(interp, path, Tk_MainWindow(interp));
        if (tkwin == NULL) {
            return E_FAIL;
        }

        x = 0;
        y = rowY + headerHeight;
        w = Tk_Width(tkwin);
        h = rowH;

        break;
    }

    default:
        return E_FAIL;
    }

    /* Convert to screen coordinates. */
    int rootX, rootY;
    Tk_GetRootCoords(container, &rootX, &rootY);

    rect->left   = rootX + x;
    rect->top    = rootY + y;
    rect->right  = rect->left + w;
    rect->bottom = rect->top  + h;

    return S_OK;
}


/* Navigate virtual children. */
static HRESULT STDMETHODCALLTYPE TkVirtualChildAccessible_accNavigate(
    IAccessible *this,
    long navDir,
    VARIANT varStart,
    VARIANT *pvarEndUpAt)
{
    if (!pvarEndUpAt) return E_INVALIDARG;
    VariantInit(pvarEndUpAt);

    /* Virtual children only support CHILDID_SELF. */
    if (varStart.vt != VT_I4 || varStart.lVal != CHILDID_SELF) {
        return E_INVALIDARG;
    }

    TkVirtualChildAccessible *virtualChild = (TkVirtualChildAccessible *)this;
    Tcl_Interp *interp = Tk_Interp(virtualChild->container);
    if (!interp) return E_FAIL;

    const char *path = Tk_PathName(virtualChild->container);
    char cmd[512];
    int totalItems = 0;

    /* Get total number of items. */
    VARIANT roleVar;
    VariantInit(&roleVar);
    if (TkAccRole(virtualChild->container, &roleVar) != S_OK || roleVar.vt != VT_I4) {
        VariantClear(&roleVar);
        return E_FAIL;
    }

    LONG role = roleVar.lVal;
    VariantClear(&roleVar);

    if (role == ROLE_SYSTEM_LIST) {
        snprintf(cmd, sizeof(cmd), "%s size", path);
    } else {
        snprintf(cmd, sizeof(cmd), "llength [%s children {}]", path);
    }

    TkGlobalLock();
    if (Tcl_Eval(interp, cmd) == TCL_OK) {
        Tcl_GetIntFromObj(NULL, Tcl_GetObjResult(interp), &totalItems);
    }
    TkGlobalUnlock();

    int targetIndex = -1;

    switch (navDir) {
    case NAVDIR_NEXT:
        if (virtualChild->index + 1 < totalItems) {
            targetIndex = virtualChild->index + 1;
        }
        break;

    case NAVDIR_PREVIOUS:
        if (virtualChild->index > 0) {
            targetIndex = virtualChild->index - 1;
        }
        break;

    case NAVDIR_FIRSTCHILD:
        targetIndex = 0;
        break;

    case NAVDIR_LASTCHILD:
        if (totalItems > 0) {
            targetIndex = totalItems - 1;
        }
        break;

    default:
        return E_NOTIMPL;
    }

    if (targetIndex >= 0 && targetIndex < totalItems) {
        /* Create virtual child for target. */
        LONG itemRole = (role == ROLE_SYSTEM_LIST) ? ROLE_SYSTEM_LISTITEM :
                        (role == ROLE_SYSTEM_OUTLINE) ? ROLE_SYSTEM_OUTLINEITEM :
                        ROLE_SYSTEM_ROW;

        TkGlobalLock();
        LONG virtId = TkCreateVirtualAccessible(interp, virtualChild->container, targetIndex, itemRole);
        TkGlobalUnlock();

        if (virtId > 0) {
            pvarEndUpAt->vt = VT_I4;
            pvarEndUpAt->lVal = virtId;
            return S_OK;
        }
    }

    return S_FALSE;
}

/* Ensure all virtual children are pre-created for a container. */
static void EnsureVirtualChildrenCreated(
	Tcl_Interp *interp, 
	Tk_Window container)
{
    if (!interp || !container) return;

    VARIANT roleVar;
    VariantInit(&roleVar);
    
    if (TkAccRole(container, &roleVar) != S_OK || roleVar.vt != VT_I4) {
        VariantClear(&roleVar);
        return;
    }

    LONG role = roleVar.lVal;
    VariantClear(&roleVar);

    if (role != ROLE_SYSTEM_LIST && 
        role != ROLE_SYSTEM_TABLE && 
        role != ROLE_SYSTEM_OUTLINE) {
        return;
    }

    const char *path = Tk_PathName(container);
    char cmd[512];
    int itemCount = 0;

    /* Get item count. */
    if (role == ROLE_SYSTEM_LIST) {
        snprintf(cmd, sizeof(cmd), "%s size", path);
    } else {
        snprintf(cmd, sizeof(cmd), "llength [%s children {}]", path);
    }

    if (Tcl_Eval(interp, cmd) == TCL_OK) {
        Tcl_GetIntFromObj(NULL, Tcl_GetObjResult(interp), &itemCount);
    }

    /* Create virtual children for all items. */
    LONG itemRole = (role == ROLE_SYSTEM_LIST) ? ROLE_SYSTEM_LISTITEM :
                    (role == ROLE_SYSTEM_OUTLINE) ? ROLE_SYSTEM_OUTLINEITEM :
                    ROLE_SYSTEM_ROW;

    for (int i = 0; i < itemCount; i++) {
        TkCreateVirtualAccessible(interp, container, i, itemRole);
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

    /* For MSAA requests, create the MSAA provider. */
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

	if (msaaProvider) {
	   if ((LONG)lParam == OBJID_CLIENT) {
		/* MSAA request. */
		if (outResult) {
		   *outResult = LresultFromObject(&IID_IAccessible, wParam, (IUnknown *)msaaProvider);
		}
	   }
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
    ckfree(event);
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
    MainThreadSyncEvent *event = (MainThreadSyncEvent *)ckalloc(sizeof(MainThreadSyncEvent));
    if (!event) return;
    event->header.proc = ExecuteOnMainThreadSync;
    event->func = func;
    event->num_args = num_args;
    event->doneEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!event->doneEvent) {
	ckfree(event);
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
	ckfree(event);
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
    TCL_UNUSED(int), /* objc */
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
    TCL_UNUSED(void *),
    Tcl_Interp *ip,
    int objc,
    Tcl_Obj *const objv[])
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
    ComputeAndCacheCheckedState(path, ip);

    TkGlobalLock();
    Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
    LONG childId = GetChildIdForTkWindow(path, childIdTable);
    HWND hwnd = Tk_GetHWND(Tk_WindowId(toplevel));
    UpdateWindow(hwnd);

    /* Determine correct selection command and role using MSAA role - CHECK PATH NOT TOPLEVEL. */
    VARIANT varRole;
    VariantInit(&varRole);

    int isTree = 0;
    int isList = 0;
    int isTable = 0;

    /* Check path widget's role, not toplevel. */
    if (TkAccRole(path, &varRole) == S_OK && varRole.vt == VT_I4) {
        LONG role = varRole.lVal;

        switch (role) {
        case ROLE_SYSTEM_OUTLINE:       /* Treeview. */
            isTree = 1;
            break;

        case ROLE_SYSTEM_LIST:          /* Listbox. */
            isList = 1;
            break;

        case ROLE_SYSTEM_TABLE:         /* Table widget. */
            isTable = 1;
            break;

        default:
            break;
        }
    }

    /* Ensure virtual children exist. */
    EnsureVirtualChildrenCreated(ip, path);
    if (isList || isTree || isTable) {
        const char *pathStr = Tk_PathName(path);
        char cmd[512];
        if (isList || isTable) {
            snprintf(cmd, sizeof(cmd), "%s curselection", pathStr);
        } else {
            snprintf(cmd, sizeof(cmd), "%s selection", pathStr);
        }

        int index = -1;
        if (Tcl_Eval(ip, cmd) == TCL_OK) {
            Tcl_Obj *res = Tcl_GetObjResult(ip);
            Tcl_Size len;
            if (Tcl_ListObjLength(ip, res, &len) == TCL_OK && len > 0) {
                Tcl_Obj *obj;
                Tcl_ListObjIndex(ip, res, 0, &obj);
                Tcl_GetIntFromObj(NULL, obj, &index);
            }
        }

        if (index >= 0) {
            LONG role = isList ? ROLE_SYSTEM_LISTITEM :
                        isTree ? ROLE_SYSTEM_OUTLINEITEM : ROLE_SYSTEM_ROW;
            LONG virtId = TkCreateVirtualAccessible(ip, path, index, role);
            if (virtId > 0) {
                /* Narrator needs these events in this specific order for virtual children. */
                NotifyWinEvent(EVENT_OBJECT_FOCUS, hwnd, OBJID_CLIENT, virtId);
                NotifyWinEvent(EVENT_OBJECT_SELECTION, hwnd, OBJID_CLIENT, virtId);
                NotifyWinEvent(EVENT_OBJECT_STATECHANGE, hwnd, OBJID_CLIENT, virtId);
                NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, hwnd, OBJID_CLIENT, virtId);
            }
        }
    }

    /* Container events for NVDA and other screen readers. */
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, hwnd, OBJID_CLIENT, childId);
    NotifyWinEvent(EVENT_OBJECT_STATECHANGE, hwnd, OBJID_CLIENT, childId);

    VariantClear(&varRole);
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
    int objc,
    Tcl_Obj *const objv[])
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
    
    HWND hwnd = Tk_GetHWND(Tk_WindowId(toplevel));
    
    /* Check if this is a container with virtual children. */
    VARIANT roleVar;
    VariantInit(&roleVar);
    
    if (TkAccRole(win, &roleVar) == S_OK && roleVar.vt == VT_I4) {
        LONG role = roleVar.lVal;
        
        if (role == ROLE_SYSTEM_LIST || 
            role == ROLE_SYSTEM_TABLE || 
            role == ROLE_SYSTEM_OUTLINE) {
	    /* Ensure virtual children exist. */
	    EnsureVirtualChildrenCreated(interp, win);

	    /*
	     * Do NOT send focus to the container for virtual-item widgets.
	     * Treeview/Table require focus on the *virtual row item*,
	     * which EmitSelectionChanged() handles.
	     */
	    if (role == ROLE_SYSTEM_LIST) {
		/* Native-style listbox behavior. */
		NotifyWinEvent(EVENT_OBJECT_FOCUS, hwnd, OBJID_CLIENT, childId);
	    }


	    /* Announce selection if exists. */
	    const char *pathStr = Tk_PathName(win);
	    char cmd[512];
	    if (role == ROLE_SYSTEM_LIST || 
		role == ROLE_SYSTEM_TABLE) {
		snprintf(cmd, sizeof(cmd), "%s curselection", pathStr);
	    } else {
		snprintf(cmd, sizeof(cmd), "%s selection", pathStr);
	    }

	    int selIndex = -1;
	    if (Tcl_Eval(interp, cmd) == TCL_OK) {
		Tcl_Obj *res = Tcl_GetObjResult(interp);
		Tcl_Size len;
		if (Tcl_ListObjLength(interp, res, &len) == TCL_OK && len > 0) {
		    Tcl_Obj *obj;
		    Tcl_ListObjIndex(interp, res, 0, &obj);
		    Tcl_GetIntFromObj(NULL, obj, &selIndex);
		}
	    }

	    if (selIndex >= 0) {
		LONG itemRole = (role == ROLE_SYSTEM_LIST) ? ROLE_SYSTEM_LISTITEM :
		    (role == ROLE_SYSTEM_OUTLINE|| role == ROLE_SYSTEM_TABLE) ? ROLE_SYSTEM_OUTLINEITEM : ROLE_SYSTEM_ROW;

		LONG virtId = TkCreateVirtualAccessible(interp, win, selIndex, itemRole);
		if (virtId > 0) {
		    /* Only send selection event — NOT focus. */
		    NotifyWinEvent(EVENT_OBJECT_SELECTION, hwnd, OBJID_CLIENT, virtId);
		    /* Optional: some screen readers like the explicit state change. */
		    NotifyWinEvent(EVENT_OBJECT_STATECHANGE, hwnd, OBJID_CLIENT, virtId);
		}
	    }
	}
    }

    /* Optional but helpful: let screen readers know the container state changed. */
    NotifyWinEvent(EVENT_OBJECT_STATECHANGE, hwnd, OBJID_CLIENT, childId);

    VariantClear(&roleVar);
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
    int objc,
    Tcl_Obj *const objv[])
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
    Tcl_CreateObjCommand(interp, "::tk::accessible::add_acc_object", TkRootAccessibleObjCmd, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_selection_change", EmitSelectionChanged, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::emit_focus_change", EmitFocusChanged, NULL, NULL);
    Tcl_CreateObjCommand(interp, "::tk::accessible::check_screenreader", IsScreenReaderRunning, NULL, NULL);
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
