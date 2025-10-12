/*
 * tkWinAccessibility.c --
 *
 *    This file implements the platform-native Microsoft Active
 *    Accessibility and the UI Automation APIs for Tk on Windows.
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

#ifndef DEBUG_ACCESSIBILITY
#   define DEBUG_ACCESSIBILITY 0
#endif

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

/* UI Automation Provider structure. */
typedef struct TkUiaProvider {
    IRawElementProviderSimpleVtbl *lpVtbl;
    IRawElementProviderFragmentVtbl *fragmentVtbl;
    TkRootAccessible *msaaProvider;
    Tk_Window tkwin;
    int childId; 
    LONG refCount;
} TkUiaProvider;

/* Fragment navigation directions. */
typedef enum FragmentNavigation {
    FragmentNavigation_Parent = 0,
    FragmentNavigation_NextSibling = 1,
    FragmentNavigation_PreviousSibling = 2,
    FragmentNavigation_FirstChild = 3,
    FragmentNavigation_LastChild = 4
} FragmentNavigation;

/*
 * Map script-level roles to C roles for both MSAA and UI Automation.
 * UI Automation control type values are from UIAutomation.h.
 */

struct WinRoleMap {
    const char *tkrole;
    LONG winrole;
    LONG uiaControlType;
};

/* Define constants for the role lookup type. */
#define ACC_ROLE_MSAA 0
#define ACC_ROLE_UIA 1

const struct WinRoleMap roleMap[] = {
    {"Button", ROLE_SYSTEM_PUSHBUTTON, 50000},       /* UIA_ButtonControlTypeId */
    {"Canvas", ROLE_SYSTEM_CLIENT, 50033},           /* UIA_PaneControlTypeId */
    {"Checkbutton", ROLE_SYSTEM_CHECKBUTTON, 50002}, /* UIA_CheckBoxControlTypeId */
    {"Combobox", ROLE_SYSTEM_COMBOBOX, 50003},       /* UIA_ComboBoxControlTypeId */
    {"Entry", ROLE_SYSTEM_TEXT, 50004},              /* UIA_EditControlTypeId */
    {"Label", ROLE_SYSTEM_STATICTEXT, 50020},        /* UIA_TextControlTypeId */
    {"Listbox", ROLE_SYSTEM_LIST, 50008},            /* UIA_ListControlTypeId */
    {"Notebook", ROLE_SYSTEM_PAGETABLIST, 50018},    /* UIA_TabControlTypeId */
    {"Progressbar", ROLE_SYSTEM_PROGRESSBAR, 50017}, /* UIA_ProgressBarControlTypeId */
    {"Radiobutton", ROLE_SYSTEM_RADIOBUTTON, 50012}, /* UIA_RadioButtonControlTypeId */
    {"Scale", ROLE_SYSTEM_SLIDER, 50015},            /* UIA_SliderControlTypeId */
    {"Scrollbar", ROLE_SYSTEM_SCROLLBAR, 50014},     /* UIA_ScrollBarControlTypeId */
    {"Spinbox", ROLE_SYSTEM_SPINBUTTON, 50016},      /* UIA_SpinnerControlTypeId */
    {"Table", ROLE_SYSTEM_TABLE, 50025},             /* UIA_TableControlTypeId */
    {"Text", ROLE_SYSTEM_TEXT, 50004},               /* UIA_EditControlTypeId */
    {"Tree", ROLE_SYSTEM_OUTLINE, 50023},            /* UIA_TreeControlTypeId */
    {NULL, 0, 0}
};

/* Virtual ID's for child elements in UIA API. */
typedef struct { 
    int startId; 
    int count; 
} VirtualRange;
 

/* Hash table for managing accessibility attributes. */
extern Tcl_HashTable *TkAccessibilityObject;

/* Hash tables for linking Tk windows to accessibility object and HWND. */
static Tcl_HashTable *tkAccessibleTable;
static int tkAccessibleTableInitialized = 0;
static Tcl_HashTable *toplevelChildTables = NULL;
static Tcl_HashTable *toplevelVirtualTables = NULL;


/* UI Automation provider tables and interfaces. */
static Tcl_HashTable *tkUiaProviderTable;
static int tkUiaProviderTableInitialized = 0;
static IUIAutomation *g_pUIAutomation = NULL;
static BOOL g_UIAutomationInitialized = FALSE;

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
 * Prototypes for UI Automation Provider. UI Automation is being implemented
 * as a layer above the MSAA implementation, using MSAA as the data provider
 * for the UI Automation functionality. This is to better support screen readers
 * such as Microsoft Narrator that rely more on UIA than MSAA.
 *
 *----------------------------------------------------------------------
 */

/* Prototypes of UI Automation Provider functions */
static HRESULT STDMETHODCALLTYPE TkUiaProvider_QueryInterface(IRawElementProviderSimple *this, REFIID riid, void **ppvObject);
static ULONG STDMETHODCALLTYPE TkUiaProvider_AddRef(IRawElementProviderSimple *this);
static ULONG STDMETHODCALLTYPE TkUiaProvider_Release(IRawElementProviderSimple *this);
static HRESULT STDMETHODCALLTYPE TkUiaProvider_get_ProviderOptions(IRawElementProviderSimple *this, enum ProviderOptions *pRetVal);
static HRESULT STDMETHODCALLTYPE TkUiaProvider_GetPatternProvider(IRawElementProviderSimple *this, PATTERNID patternId, IUnknown **pRetVal);
static HRESULT STDMETHODCALLTYPE TkUiaProvider_GetPropertyValue(IRawElementProviderSimple *this, PROPERTYID propertyId, VARIANT *pRetVal);
static HRESULT STDMETHODCALLTYPE TkUiaProvider_get_HostRawElementProvider(IRawElementProviderSimple *this, IRawElementProviderSimple **pRetVal);

/* UI Automation Provider VTable. */
static IRawElementProviderSimpleVtbl tkUiaProviderVtbl = {
    TkUiaProvider_QueryInterface,
    TkUiaProvider_AddRef,
    TkUiaProvider_Release,
    TkUiaProvider_get_ProviderOptions,
    TkUiaProvider_GetPatternProvider,
    TkUiaProvider_GetPropertyValue,
    TkUiaProvider_get_HostRawElementProvider
};

/*
 *----------------------------------------------------------------------
 *
 * Prototypes forIRawElementProviderFragment - these are accessible 
 * child widgets in the UI Automation API. They will be used primarily
 * for such child elements as list/table rows and tree nodes.
 * MSAA child elements can be tracked with child ID's and custom notfications
 * on selection events, but UIA needs a more formal implementation. 
 *
 *----------------------------------------------------------------------
 */

static HRESULT STDMETHODCALLTYPE TkUiaFragment_QueryInterface(IRawElementProviderFragment *this, REFIID riid, void **ppvObject);
static ULONG STDMETHODCALLTYPE TkUiaFragment_AddRef(IRawElementProviderFragment *this);
static ULONG STDMETHODCALLTYPE TkUiaFragment_Release(IRawElementProviderFragment *this);
static HRESULT STDMETHODCALLTYPE TkUiaFragment_Navigate(IRawElementProviderFragment *this, enum FragmentNavigation direction, IRawElementProviderFragment **pRetVal);
static HRESULT STDMETHODCALLTYPE TkUiaFragment_GetRuntimeId(IRawElementProviderFragment *this, SAFEARRAY **pRetVal);
static HRESULT STDMETHODCALLTYPE TkUiaFragment_get_BoundingRectangle(IRawElementProviderFragment *this, struct UiaRect *pRetVal);
static HRESULT STDMETHODCALLTYPE TkUiaFragment_GetEmbeddedFragmentRoots(IRawElementProviderFragment *this, SAFEARRAY **pRetVal);
static HRESULT STDMETHODCALLTYPE TkUiaFragment_SetFocus(IRawElementProviderFragment *this);
static HRESULT STDMETHODCALLTYPE TkUiaFragment_get_FragmentRoot(IRawElementProviderFragment *this, IRawElementProviderFragmentRoot **pRetVal);
static int GetFirstChildId(Tk_Window toplevel, Tk_Window container, enum FragmentNavigation direction);
static int GetNextSiblingId(Tk_Window toplevel, Tk_Window container, int currentId, enum FragmentNavigation direction);
static int GetPreviousSiblingId(Tk_Window toplevel, Tk_Window container, int currentId, enum FragmentNavigation direction);
static int GetLastChildId(Tk_Window toplevel, Tk_Window container, enum FragmentNavigation direction);
static BOOL IsChildOfContainer(Tk_Window child, Tk_Window container);
static TkUiaProvider *CreateUiaProviderForChild(TkRootAccessible *msaaProvider, Tk_Window container, int childId);
static BOOL IsVirtualChildContainer(LONG role);
static int GetVirtualChildIds(Tk_Window toplevel, Tk_Window container, int **childIds);

/* Fragment VTable. */
static IRawElementProviderFragmentVtbl tkUiaFragmentVtbl = {
    TkUiaFragment_QueryInterface,
    TkUiaFragment_AddRef,
    TkUiaFragment_Release,
    TkUiaFragment_Navigate,
    TkUiaFragment_GetRuntimeId,
    TkUiaFragment_get_BoundingRectangle,
    TkUiaFragment_GetEmbeddedFragmentRoots,
    TkUiaFragment_SetFocus,
    TkUiaFragment_get_FragmentRoot
};

/*
 *----------------------------------------------------------------------
 *
 * Prototypes for child widgets using MSAA childId. These will be called
 * from a global lock or explicitly run on the main thread.
 *
 *----------------------------------------------------------------------
 */

static HRESULT TkAccRole(Tk_Window win, VARIANT *pvarRole, int format);
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
static Tcl_HashTable *GetVirtualTableForToplevel(Tk_Window toplevel);
TkRootAccessible *GetTkAccessibleForWindow(Tk_Window win);
static TkRootAccessible *CreateRootAccessible(Tcl_Interp *interp, HWND hwnd, const char *pathName);
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

HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accSelection(
    TCL_UNUSED(IAccessible *), /* this */
    TCL_UNUSED(VARIANT *)) /*pvarChildren */
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
    if (!ppvObject) return E_INVALIDARG; /* Add null check for ppvObject. */
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
    if (!this) return E_INVALIDARG; /* Add null check. */
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    return InterlockedIncrement(&tkAccessible->refCount);
}

/* Function to free the MSAA object. */
static ULONG STDMETHODCALLTYPE TkRootAccessible_Release(
    IAccessible *this)
{
    if (!this) return E_INVALIDARG; /* Add null check. */
    TkRootAccessible *tkAccessible = (TkRootAccessible *)this;
    ULONG count = InterlockedDecrement(&tkAccessible->refCount);
    if (count == 0) {
	TkGlobalLock();
	if (tkAccessible->win && tkAccessibleTableInitialized) { /* Check table initialization. */
	    Tcl_HashEntry *entry = Tcl_FindHashEntry(tkAccessibleTable, tkAccessible->win);
	    if (entry) {
		Tcl_DeleteHashEntry(entry);
	    }
	}
	if (tkAccessible->pathName) { /* Avoid double-free. */
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
    if (!pctinfo) return E_INVALIDARG; /* Add null check. */
    *pctinfo = 1; /* We provide one type information interface. */
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
    if (!ppTInfo) return E_INVALIDARG; /* Add null check. */
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
    if (!rgszNames || !rgDispId) return E_INVALIDARG; /* Add null check. */
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
    if (!pVarResult) return E_INVALIDARG; /* Add null check. */
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
 * Function to map accessible name to MSAA. 
 * We return the "description" value from the hash table
 * becuase UIA requires the name property to be populated -
 * but using the "name" value causes the same word to 
 * be spoken twice, because the name usually matches 
 * the role. 
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

    if (varChild.vt == VT_I4 && varChild.lVal == CHILDID_SELF) {
        /* For the toplevel itself, no name to avoid double-reading */
        TkGlobalUnlock();
        return S_OK;
    }

    if (varChild.vt == VT_I4 && varChild.lVal > 0) {
        Tk_Window child = GetTkWindowForChildId(varChild.lVal, tkAccessible->toplevel);
        if (!child) {
            TkGlobalUnlock();
            return E_INVALIDARG;
        }

        /* Retrieve the accessible name for the child */
        HRESULT hr = TkAccDescription(child, pszName);
        TkGlobalUnlock();
        return hr;
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
	HRESULT hr = TkAccRole(child, pvarRole, ACC_ROLE_MSAA);
	TkGlobalUnlock();
	return hr;
    }
    TkGlobalUnlock();
    return E_INVALIDARG;
}

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
    int count = TkAccChildCount(tkAccessible->toplevel);
    TkGlobalUnlock();

    *pcChildren = count < 0 ? 0 : count;
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
 * This function returns NULL because we are populating
 * the "name" property with the description data 
 * to support both MSAA and UIA. 
 */
static HRESULT STDMETHODCALLTYPE TkRootAccessible_get_accDescription(
       IAccessible *this,
       VARIANT varChild,
       BSTR *pszDescription)
{
    if (!pszDescription) return E_INVALIDARG;
    *pszDescription = NULL; /* No description to avoid double-reading. */
    return S_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Glue functions - child widgets.
 *
 *----------------------------------------------------------------------
 */

/* Function to map accessible role to MSAA or UIA. */

static HRESULT TkAccRole(
    Tk_Window win,
    VARIANT *pvarRole,
    int format)
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
    /* Fallback value. ROLE_SYSTEM_CLIENT for MSAA, 0 (Custom) for UIA. */
    LONG result = (format == ACC_ROLE_MSAA) ? ROLE_SYSTEM_CLIENT : 0; 

    for (int i = 0; roleMap[i].tkrole != NULL; i++) {
        if (strcmp(tkrole, roleMap[i].tkrole) == 0) {
            if (format == ACC_ROLE_MSAA) {
                result = roleMap[i].winrole;
            } else if (format == ACC_ROLE_UIA) {
                result = roleMap[i].uiaControlType;
            }
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
static void ComputeAndCacheCheckedState(
    Tk_Window win,
    Tcl_Interp *interp)
{
    if (!win || !interp) {
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "ComputeAndCacheCheckedState: Invalid win or interp\n");
#endif
	return;
    }

    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)win);
    if (!hPtr) {
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "ComputeAndCacheCheckedState: No accessibility object for %s\n", Tk_PathName(win));
#endif
    return;
    }
    Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);

    Tcl_HashEntry *rolePtr = Tcl_FindHashEntry(AccessibleAttributes, "role");
    const char *tkrole = NULL;
    if (rolePtr) {
	tkrole = Tcl_GetString(Tcl_GetHashValue(rolePtr));
    }
    if (!tkrole || (strcmp(tkrole, "Checkbutton") != 0 && strcmp(tkrole, "Radiobutton") != 0)) {
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "ComputeAndCacheCheckedState: Not a checkbutton or radiobutton: %s\n", tkrole ? tkrole : "null");
#endif
    return;
    }

    const char *path = Tk_PathName(win);
    Tcl_Obj *varCmd = Tcl_ObjPrintf("%s cget -variable", path);
    Tcl_IncrRefCount(varCmd);
    const char *varName = NULL;
    if (Tcl_EvalObjEx(interp, varCmd, TCL_EVAL_GLOBAL) == TCL_OK) {
	varName = Tcl_GetStringResult(interp);
    } else {
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "ComputeAndCacheCheckedState: Failed to get -variable for %s\n", path);
#endif
    }
    Tcl_DecrRefCount(varCmd);

    int isChecked = 0;
    if (varName && *varName) {
	const char *varVal = Tcl_GetVar(interp, varName, TCL_GLOBAL_ONLY);
	if (!varVal) {
#if DEBUG_ACCESSIBILITY
	    fprintf(stderr, "ComputeAndCacheCheckedState: Variable %s not found for %s\n", varName, path);
#endif
	} else {
	    const char *onValue = NULL;
	    Tcl_Obj *valueCmd = NULL;
	    if (strcmp(tkrole, "Checkbutton") == 0) {
		valueCmd = Tcl_ObjPrintf("%s cget -onvalue", path);
	    } else if (strcmp(tkrole, "Radiobutton") == 0) {
		valueCmd = Tcl_ObjPrintf("%s cget -value", path);
	    }
	    if (valueCmd) {
		Tcl_IncrRefCount(valueCmd);
		if (Tcl_EvalObjEx(interp, valueCmd, TCL_EVAL_GLOBAL) == TCL_OK) {
		    onValue = Tcl_GetStringResult(interp);
		} else {
#if DEBUG_ACCESSIBILITY
		    fprintf(stderr, "ComputeAndCacheCheckedState: Failed to get -onvalue/-value for %s\n", path);
#endif
		}
		Tcl_DecrRefCount(valueCmd);
	    }
	    if (varVal && onValue && strcmp(varVal, onValue) == 0) {
		isChecked = 1;
	    }
#if DEBUG_ACCESSIBILITY
	    fprintf(stderr, "ComputeAndCacheCheckedState: %s is %s (var=%s, onValue=%s)\n",
		    path, isChecked ? "checked" : "unchecked", varVal, onValue);
#endif
	}
    } else {
	/* Fallback: Check widget's internal state for ttk widgets */
	Tcl_Obj *stateCmd = Tcl_ObjPrintf("%s instate selected", path);
	Tcl_IncrRefCount(stateCmd);
	if (Tcl_EvalObjEx(interp, stateCmd, TCL_EVAL_GLOBAL) == TCL_OK) {
	    const char *result = Tcl_GetStringResult(interp);
	    if (result && strcmp(result, "1") == 0) {
		isChecked = 1;
	    }
#if DEBUG_ACCESSIBILITY
	    fprintf(stderr, "ComputeAndCacheCheckedState: %s instate selected = %s\n", path, result);
#endif
	}
	Tcl_DecrRefCount(stateCmd);
    }

    /* Cache the checked state */
    TkGlobalLock();
    Tcl_HashEntry *valuePtr;
    int newEntry;
    valuePtr = Tcl_CreateHashEntry(AccessibleAttributes, "value", &newEntry);
    char buf[2];
    snprintf(buf, sizeof(buf), "%d", isChecked);
    Tcl_Obj *valObj = Tcl_NewStringObj(buf, -1);
    Tcl_IncrRefCount(valObj);
    Tcl_SetHashValue(valuePtr, valObj);
#if DEBUG_ACCESSIBILITY
    fprintf(stderr, "ComputeAndCacheCheckedState: Cached value=%s for %s\n", buf, path);
#endif
    TkGlobalUnlock();

    /* Notify MSAA with both events */
    Tk_Window toplevel = GetToplevelOfWidget(win);
    if (toplevel) {
	Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
	LONG childId = GetChildIdForTkWindow(win, childIdTable);
	if (childId > 0) {
	    HWND hwnd = Tk_GetHWND(Tk_WindowId(toplevel));
	    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, hwnd, OBJID_CLIENT, childId);
	    NotifyWinEvent(EVENT_OBJECT_STATECHANGE, hwnd, OBJID_CLIENT, childId);
#if DEBUG_ACCESSIBILITY
	    fprintf(stderr, "ComputeAndCacheCheckedState: Notified MSAA for %s (childId=%ld)\n", path, childId);
#endif
	} else {
#if DEBUG_ACCESSIBILITY
	    fprintf(stderr, "ComputeAndCacheCheckedState: Invalid childId for %s\n", path);
#endif
	}
    } else {
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "ComputeAndCacheCheckedState: No toplevel for %s\n", path);
#endif
    }
}

/* Function to map accessible state to MSAA. */
static HRESULT TkAccState(
    Tk_Window win,
    VARIANT *pvarState)
{
	Tcl_Interp *interp = Tk_Interp(win);
	
    if (!win || !pvarState) {
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "TkAccState: Invalid win or pvarState\n");
#endif
	return E_INVALIDARG;
    }
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, win);
    if (!hPtr) {
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "TkAccState: No accessibility object for %s\n", Tk_PathName(win));
#endif
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

    /* Check for checked state using cached value */
    Tcl_HashEntry *rolePtr = Tcl_FindHashEntry(AccessibleAttributes, "role");
    if (rolePtr) {
	const char *tkrole = Tcl_GetString(Tcl_GetHashValue(rolePtr));
	if (strcmp(tkrole, "Checkbutton") == 0 || strcmp(tkrole, "Radiobutton") == 0) {
	    Tcl_HashEntry *valuePtr = Tcl_FindHashEntry(AccessibleAttributes, "value");
	    if (valuePtr) {
		const char *value = Tcl_GetString(Tcl_GetHashValue(valuePtr));
#if DEBUG_ACCESSIBILITY
		fprintf(stderr, "TkAccState: %s value=%s\n", Tk_PathName(win), value);
#endif
		if (value && strcmp(value, "1") == 0) {
		    state |= STATE_SYSTEM_CHECKED;
		}
	    } else {
#if DEBUG_ACCESSIBILITY
		fprintf(stderr, "TkAccState: No value cached for %s\n", Tk_PathName(win));
#endif
	    }
	}
	/* Check virtual children. */
	if (strcmp(tkrole, "Listbox") == 0 || strcmp(tkrole, "Tree") == 0 || strcmp(tkrole, "Table") == 0) {
	    Tcl_HashTable *virtualTable = GetVirtualTableForToplevel(GetToplevelOfWidget(win));
	    if (virtualTable) {
		Tcl_HashEntry *vEntry = Tcl_FindHashEntry(virtualTable, win);
		if (vEntry) {
		    VirtualRange *range = Tcl_GetHashValue(vEntry);
		    Tcl_Obj *selCmd = Tcl_NewStringObj(Tcl_GetString(Tcl_NewStringObj(Tk_PathName(win), -1)), -1);
		    Tcl_IncrRefCount(selCmd);
		    Tcl_AppendStringsToObj(selCmd, " curselection", NULL);
		    if (Tcl_EvalObjEx(interp, selCmd, TCL_EVAL_GLOBAL) == TCL_OK) {
			Tcl_Obj *selList = Tcl_GetObjResult(interp);
			Tcl_Size selCount;
			Tcl_ListObjLength(interp, selList, &selCount);
			for (int i = 0; i < selCount; i++) {
			    Tcl_Obj *selItem; 
				Tcl_ListObjIndex(interp, selList, i, &selItem);
			    int selIndex;
			    Tcl_GetIntFromObj(interp, selItem, &selIndex);
			    if (selIndex >= 0 && selIndex < range->count) {
				int childId = range->startId + selIndex;
				if (childId == pvarState->lVal) state |= STATE_SYSTEM_SELECTED;
			    }
			}
		    }
		    Tcl_DecrRefCount(selCmd);
		}
	    }
	}
    }

    TkWindow *focusPtr = TkGetFocusWin((TkWindow *)win);
    if (focusPtr == (TkWindow *)win) {
	state |= STATE_SYSTEM_FOCUSED;
    }

    pvarState->vt = VT_I4;
    pvarState->lVal = state;
#if DEBUG_ACCESSIBILITY
    fprintf(stderr, "TkAccState: %s state=0x%lx\n", Tk_PathName(win), state);
#endif
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
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "TkDoDefaultAction: Invalid childId\n");
#endif
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
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "TkDoDefaultAction: No toplevel for childId %d\n", childId);
#endif
	return;
    }
    Tk_Window win = GetTkWindowForChildId(childId, toplevel);
    if (!win) {
	TkGlobalUnlock();
	mainThreadResult = E_INVALIDARG;
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "TkDoDefaultAction: No window for childId %d\n", childId);
#endif
	return;
    }
    Tcl_HashEntry *hPtr = Tcl_FindHashEntry(TkAccessibilityObject, (char *)win);
    if (!hPtr) {
	TkGlobalUnlock();
	mainThreadResult = E_INVALIDARG;
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "TkDoDefaultAction: No accessibility object for %s\n", Tk_PathName(win));
#endif
	return;
    }
    Tcl_HashTable *AccessibleAttributes = (Tcl_HashTable *)Tcl_GetHashValue(hPtr);
    if (!AccessibleAttributes) {
	TkGlobalUnlock();
	mainThreadResult = E_INVALIDARG;
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "TkDoDefaultAction: No attributes for %s\n", Tk_PathName(win));
#endif
	return;
    }
    Tcl_HashEntry *hPtr2 = Tcl_FindHashEntry(AccessibleAttributes, "action");
    if (!hPtr2) {
	TkGlobalUnlock();
	mainThreadResult = E_INVALIDARG;
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "TkDoDefaultAction: No action for %s\n", Tk_PathName(win));
#endif
	return;
    }
    const char *action = Tcl_GetString(Tcl_GetHashValue(hPtr2));
    if (!action) {
	TkGlobalUnlock();
	mainThreadResult = E_INVALIDARG;
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "TkDoDefaultAction: Invalid action for %s\n", Tk_PathName(win));
#endif
	return;
    }
    event = (ActionEvent *)ckalloc(sizeof(ActionEvent));
    if (event == NULL) {
	TkGlobalUnlock();
	mainThreadResult = E_OUTOFMEMORY;
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "TkDoDefaultAction: Memory allocation failed\n");
#endif
	return;
    }
    event->header.proc = ActionEventProc;
    event->command = ckalloc(strlen(action) + 1);
    strcpy(event->command, action);
    event->win = win;

    /* Update checked state and notify MSAA */
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
/*
 * TkAccChildCount
 * Returns the number of accessible children for a given window.
 * Includes both mapped Tk_Window children and virtual children (e.g., listbox items, tree nodes).
 * Returns -1 on error (e.g., invalid window or toplevel).
 */
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

    /* Add virtual children (e.g., listbox items, tree nodes, table rows). */
    int virtualCount = 0;
    Tcl_HashTable *virtualTable = GetVirtualTableForToplevel(toplevel);
    if (virtualTable) {
        Tcl_HashSearch search;
        Tcl_HashEntry *vEntry;
        for (vEntry = Tcl_FirstHashEntry(virtualTable, &search); vEntry; vEntry = Tcl_NextHashEntry(&search)) {
            virtualCount += ((VirtualRange *)Tcl_GetHashValue(vEntry))->count;
        }
    }
    count += virtualCount; 

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
 * General/utility functions to help integrate the MSAA API
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
     * each one tracked sepaarately in this hash table.
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
    if (!toplevelVirtualTables) {
	toplevelVirtualTables = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
	if (toplevelVirtualTables) {
	    Tcl_InitHashTable(toplevelVirtualTables, TCL_ONE_WORD_KEYS);
	}
    }
}

/* Function to get a table of virutal child Id's for a toplevel. */
static Tcl_HashTable *GetVirtualTableForToplevel(Tk_Window toplevel) 
{
    if (!toplevel || !toplevelVirtualTables) return NULL;
    Tcl_HashEntry *entry; int newEntry; Tcl_HashTable *virtualTable;
    TkGlobalLock();
    entry = Tcl_CreateHashEntry(toplevelVirtualTables, toplevel, &newEntry);
    if (newEntry) {
        virtualTable = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
        Tcl_InitHashTable(virtualTable, TCL_ONE_WORD_KEYS);
        Tcl_SetHashValue(entry, virtualTable);
    } else {
        virtualTable = (Tcl_HashTable *)Tcl_GetHashValue(entry);
    }
    TkGlobalUnlock();
    return virtualTable;
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
    Tcl_HashTable *virtualTable = GetVirtualTableForToplevel(toplevel);
    if (virtualTable) {
	Tcl_HashSearch search; Tcl_HashEntry *vEntry;
	for (vEntry = Tcl_FirstHashEntry(virtualTable, &search); vEntry; vEntry = Tcl_NextHashEntry(&search)) {
	    ckfree(Tcl_GetHashValue(vEntry));
	}
	Tcl_DeleteHashTable(virtualTable);
	ckfree(virtualTable);
	Tcl_HashEntry *tlEntry = Tcl_FindHashEntry(toplevelVirtualTables, toplevel);
	if (tlEntry) Tcl_DeleteHashEntry(tlEntry);
    }
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
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "AssignChildIdsRecursive: Skipping %s (invalid or unmapped)\n",
		win ? Tk_PathName(win) : "null");
#endif
	return;
    }
    Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
    if (!childIdTable) {
#if DEBUG_ACCESSIBILITY
	fprintf(stderr, "AssignChildIdsRecursive: No childIdTable for toplevel %s\n", Tk_PathName(toplevel));
#endif
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
  /* Assign virtual iD's for non-window elements for UIA. */
    VARIANT varRole; 
    VariantInit(&varRole);
    TkAccRole(win, &varRole, ACC_ROLE_UIA); 
    if (varRole.vt == VT_I4 && (varRole.lVal == 50008 || varRole.lVal == 50023 || varRole.lVal == 50025)) { /* UIA_List, UIA_Tree, UIA_Table */
	Tcl_HashTable *virtualTable = GetVirtualTableForToplevel(toplevel);
	if (virtualTable) {
	    Tcl_Size count = 0; 
		char cmd[256];
	    if (varRole.lVal == 50008) {
		snprintf(cmd, sizeof(cmd), "%s size", Tk_PathName(win)); /* Listbox */
	    } else if (varRole.lVal == 50023 || varRole.lVal == 50025) {
		    snprintf(cmd, sizeof(cmd), "%s children {}", Tk_PathName(win)); /* Tree/Table */
		}
	    if (Tcl_Eval(interp, cmd) == TCL_OK) {
		Tcl_ListObjLength(interp, Tcl_GetObjResult(interp), &count);
		} else {
		int countInt;
		Tcl_GetIntFromObj(interp, Tcl_GetObjResult(interp), &countInt);
		count = (Tcl_Size)countInt;
	    }
	    if (count > 0) {
		int newEntry; Tcl_HashEntry *vEntry = Tcl_CreateHashEntry(virtualTable, win, &newEntry);
		VirtualRange *range = ckalloc(sizeof(VirtualRange));
		range->startId = *nextId; range->count = count;
		Tcl_SetHashValue(vEntry, range);
		*nextId += count;
	    }
	}
    }
    VariantClear(&varRole);
}

/*
 *----------------------------------------------------------------------
 *
 * UI Automation Provider implementation. This is driven by the MSAA accessible
 * object; the UIA functions query obtain properties and data via the MSAA
 * interface.
 *
 *----------------------------------------------------------------------
 */


/* UI Automation Provider QueryInterface. */
static HRESULT STDMETHODCALLTYPE TkUiaProvider_QueryInterface(
    IRawElementProviderSimple *this,
    REFIID riid,
    void **ppvObject)
{
    if (!ppvObject) return E_INVALIDARG;

    TkUiaProvider *provider = (TkUiaProvider *)this;

    /* Common provider IIDs */
    if (IsEqualIID(riid, &IID_IUnknown) ||
        IsEqualIID(riid, &IID_IRawElementProviderSimple) ||
        IsEqualIID(riid, &IID_IRawElementProviderFragment) ||
        IsEqualIID(riid, &IID_IRawElementProviderFragmentRoot)) {
        *ppvObject = this;
        TkUiaProvider_AddRef(this);
        return S_OK;
    }

    /* Forward to embedded MSAA provider for other interfaces */
    if (provider->msaaProvider) {
        return provider->msaaProvider->lpVtbl->QueryInterface(
            (IAccessible *)provider->msaaProvider, riid, ppvObject);
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}



/* UI Automation Provider AddRef. */
static ULONG STDMETHODCALLTYPE TkUiaProvider_AddRef(
    IRawElementProviderSimple *this)
{
    if (!this) return 0;
    TkUiaProvider *provider = (TkUiaProvider *)this;
    return InterlockedIncrement(&provider->refCount);
}

/* UI Automation Provider Release. */
static ULONG STDMETHODCALLTYPE TkUiaProvider_Release(
    IRawElementProviderSimple *this)
{
    if (!this) return 0;
    TkUiaProvider *provider = (TkUiaProvider *)this;
    ULONG count = InterlockedDecrement(&provider->refCount);
    if (count == 0) {
	TkGlobalLock();
	if (provider->msaaProvider) {
	    TkRootAccessible_Release((IAccessible *)provider->msaaProvider);
	}
	ckfree(provider);
	TkGlobalUnlock();
    }
    return count;
}

/* UI Automation Provider options. */
static HRESULT STDMETHODCALLTYPE TkUiaProvider_get_ProviderOptions(
    TCL_UNUSED(IRawElementProviderSimple *), /* this */
    enum ProviderOptions *pRetVal)
{
    if (!pRetVal) return E_INVALIDARG;
    *pRetVal = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

/* UI Automation Pattern Provider. */
static HRESULT STDMETHODCALLTYPE TkUiaProvider_GetPatternProvider(
    IRawElementProviderSimple *this,
    PATTERNID patternId,
    IUnknown **pRetVal)
{
    if (!pRetVal) return E_INVALIDARG;
    *pRetVal = NULL;

    TkUiaProvider *provider = (TkUiaProvider *)this;

    /*
     * For now, we return the provider itself for common patterns so that
     * UIA clients that call GetPatternProvider receive a valid object.
     *
     */
    if (patternId == UIA_InvokePatternId ||
        patternId == UIA_SelectionPatternId ||
        patternId == UIA_ValuePatternId ||
        patternId == UIA_ScrollPatternId) {
        *pRetVal = (IUnknown *)this;
        TkUiaProvider_AddRef(this);
        return S_OK;
    }

    /* Otherwise no pattern support provided here. */
    return S_OK;
}

/* UI Automation Property Provider. */
static HRESULT STDMETHODCALLTYPE TkUiaProvider_GetPropertyValue(
    IRawElementProviderSimple *this,
    PROPERTYID propertyId,
    VARIANT *pRetVal)
{
    if (!pRetVal) return E_INVALIDARG;

    VariantInit(pRetVal);
    TkUiaProvider *provider = (TkUiaProvider *)this;

    if (!provider || !provider->msaaProvider) {
        return E_FAIL;
    }

    /* Prepare a varChild that points to the correct MSAA child.
     * If provider->childId > 0 then we are a virtual child; otherwise SELF.
     */
    VARIANT varChild;
    VariantInit(&varChild);
    varChild.vt = VT_I4;
    varChild.lVal = (provider->childId > 0) ? provider->childId : CHILDID_SELF;

    TkGlobalLock();

    TkRootAccessible *msaa = provider->msaaProvider;

    switch (propertyId) {

case 30003: { /* UIA_ControlTypePropertyId */
    HRESULT hr = S_OK;
    VARIANT varRole; VariantInit(&varRole);
    TkAccRole(provider->tkwin, &varRole, ACC_ROLE_UIA);
    long role = (varRole.vt == VT_I4) ? varRole.lVal : 0;
    VariantClear(&varRole);
    pRetVal->vt = VT_I4;
    if (provider->childId > 0) {
        /* Virtual children: map container role to item role. */
        if (role == 50008) pRetVal->lVal = 50009; /* UIA_ListControlTypeId -> UIA_ListItemControlTypeId. */
        else if (role == 50023) pRetVal->lVal = 50024; /* UIA_TreeControlTypeId -> UIA_TreeItemControlTypeId. */
        else if (role == 50025) pRetVal->lVal = 50026; /* UIA_TableControlTypeId -> UIA_DataItemControlTypeId. */
        else pRetVal->lVal = 50032; /* UIA_CustomControlTypeId. */
    } else {
        pRetVal->lVal = (role == 50008 || role == 50023 || role == 50025) ? role : 50032;
    }
    break;
}

    case 30004: /* UIA_HelpTextPropertyId */
	{
	    BSTR desc = NULL;
	    HRESULT hrDesc = TkRootAccessible_get_accDescription((IAccessible *)msaa, varChild, &desc);
	    if (SUCCEEDED(hrDesc) && desc) {
		pRetVal->vt = VT_BSTR;
		pRetVal->bstrVal = desc; /* ownership transferred to caller */
	    }
	    break;
	}

    case 30005: /* UIA_NamePropertyId */
	{
	    BSTR name = NULL;
	    HRESULT hrName = TkRootAccessible_get_accName((IAccessible *)msaa, varChild, &name);
	    if (SUCCEEDED(hrName) && name && SysStringLen(name) > 0) {
		pRetVal->vt = VT_BSTR;
		pRetVal->bstrVal = name; /* ownership transferred to caller */
	    } else {
		if (name) SysFreeString(name);
		pRetVal->vt = VT_EMPTY;
	    }
	    if (FAILED(hrName) || !name || SysStringLen(name) == 0) {
		if (name) SysFreeString(name);
		Tcl_HashTable *virtualTable = GetVirtualTableForToplevel(msaa->toplevel);
		if (virtualTable) {
		    Tcl_HashSearch search; Tcl_HashEntry *vEntry;
		    for (vEntry = Tcl_FirstHashEntry(virtualTable, &search); vEntry; vEntry = Tcl_NextHashEntry(&search)) {
			VirtualRange *range = Tcl_GetHashValue(vEntry);
			if (varChild.lVal >= range->startId && varChild.lVal < range->startId + range->count) {
			    Tk_Window cont = (Tk_Window)Tcl_GetHashKey(virtualTable, vEntry);
			    int index = varChild.lVal - range->startId;
			    char cmd[256]; snprintf(cmd, sizeof(cmd), "%s get %d", Tk_PathName(cont), index);
			    if (Tcl_Eval(msaa->interp, cmd) == TCL_OK) {
				const char *text = Tcl_GetStringResult(msaa->interp);
				Tcl_DString ds; Tcl_DStringInit(&ds);
				pRetVal->vt = VT_BSTR;
				pRetVal->bstrVal = SysAllocString(Tcl_UtfToWCharDString(text, -1, &ds));
				Tcl_DStringFree(&ds);
				TkGlobalUnlock();
				return pRetVal->bstrVal ? S_OK : S_FALSE;
			    }
			}
		    }
		}
		pRetVal->vt = VT_EMPTY;
	    }
	    break;
	}
	
    case 30007: { /* UIA_ValuePropertyId. */
	BSTR value = NULL;
	HRESULT hrValue = TkRootAccessible_get_accValue((IAccessible *)msaa, varChild, &value);
	if (SUCCEEDED(hrValue) && value && SysStringLen(value) > 0) {
	    pRetVal->vt = VT_BSTR;
	    pRetVal->bstrVal = value;
	} else {
	    if (value) SysFreeString(value);
	    Tcl_HashTable *virtualTable = GetVirtualTableForToplevel(msaa->toplevel);
	    if (virtualTable) {
		Tcl_HashSearch search; Tcl_HashEntry *vEntry;
		for (vEntry = Tcl_FirstHashEntry(virtualTable, &search); vEntry; vEntry = Tcl_NextHashEntry(&search)) {
		    VirtualRange *range = Tcl_GetHashValue(vEntry);
		    if (varChild.lVal >= range->startId && varChild.lVal < range->startId + range->count) {
			Tk_Window cont = (Tk_Window)Tcl_GetHashKey(virtualTable, vEntry);
			int index = varChild.lVal - range->startId;
			char cmd[256]; snprintf(cmd, sizeof(cmd), "%s get %d", Tk_PathName(cont), index);
			if (Tcl_Eval(msaa->interp, cmd) == TCL_OK) {
			    const char *text = Tcl_GetStringResult(msaa->interp);
			    Tcl_DString ds; Tcl_DStringInit(&ds);
			    pRetVal->vt = VT_BSTR;
			    pRetVal->bstrVal = SysAllocString(Tcl_UtfToWCharDString(text, -1, &ds));
			    Tcl_DStringFree(&ds);
			    TkGlobalUnlock();
			    return pRetVal->bstrVal ? S_OK : S_FALSE;
			}
		    }
		}
	    }
	    pRetVal->vt = VT_EMPTY;
	}
	break;
    }

    case 30016: /* UIA_IsControlElementPropertyId */
    case 30017: /* UIA_IsContentElementPropertyId */
	{
	    /* For virtual children we want these to be TRUE so Narrator doesn't filter them out. */
	    VARIANT_BOOL isElement = (provider->childId > 0) ? VARIANT_TRUE : VARIANT_FALSE;
	    pRetVal->vt = VT_BOOL;
	    pRetVal->boolVal = isElement;
	    break;
	}

    case 30011: /* UIA_AutomationIdPropertyId */
	{
	    /* Provide a stable automation id if possible (use child id for virtual children). */
	    if (provider->childId > 0) {
		pRetVal->vt = VT_I4;
		pRetVal->lVal = provider->childId;
	    } else {
		pRetVal->vt = VT_EMPTY;
	    }
	    break;
	}

    default:
	{
	    break;
	}
    }

    TkGlobalUnlock();
    VariantClear(&varChild);

    return S_OK;
}



/* UI Automation Host Element Provider. */
static HRESULT STDMETHODCALLTYPE TkUiaProvider_get_HostRawElementProvider(
    TCL_UNUSED(IRawElementProviderSimple *), /* this */
    IRawElementProviderSimple **pRetVal)
{
    if (!pRetVal) return E_INVALIDARG;
    *pRetVal = NULL;
    return S_OK;
}

/*
 * Create UI Automation Provider.
 */

static TkUiaProvider *CreateUiaProvider(TkRootAccessible *msaaProvider, Tk_Window tkwin, int childId)
{
    if (!msaaProvider) return NULL;

    TkUiaProvider *provider = (TkUiaProvider *)ckalloc(sizeof(TkUiaProvider));
    if (!provider) return NULL;

    provider->lpVtbl = &tkUiaProviderVtbl;
    provider->fragmentVtbl = &tkUiaFragmentVtbl; 
    provider->msaaProvider = msaaProvider;
    provider->tkwin = tkwin;
    provider->childId = childId;
    provider->refCount = 1;

    TkRootAccessible_AddRef((IAccessible *)msaaProvider);

    return provider;
}

/*
 * Initialize UI Automation.
 */
static BOOL InitializeUIAutomation(void)
{
    if (g_UIAutomationInitialized) {
	return TRUE;
    }

    HRESULT hr = CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
				 &IID_IUIAutomation, (void**)&g_pUIAutomation);
    if (SUCCEEDED(hr)) {
	g_UIAutomationInitialized = TRUE;
	return TRUE;
    }

    return FALSE;
}

/*
 * Initialize UI Automation Provider table.
 */
void InitUiaProviderTable(void)
{
    if (!tkUiaProviderTableInitialized) {
	tkUiaProviderTable = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
	if (tkUiaProviderTable) {
	    Tcl_InitHashTable(tkUiaProviderTable, TCL_ONE_WORD_KEYS);
	    tkUiaProviderTableInitialized = 1;
	}
    }
}

/*
 * Get UI Automation Provider for window.
 */
static TkUiaProvider *GetUiaProviderForWindow(Tk_Window win)
{
    if (!win || !tkUiaProviderTableInitialized) return NULL;
    Tcl_HashEntry *entry = Tcl_FindHashEntry(tkUiaProviderTable, win);
    if (entry) return (TkUiaProvider *)Tcl_GetHashValue(entry);
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * IRawElementProviderFragment implementation. This allows child elements
 * like list/table rows to be visible to UIA.
 *
 *----------------------------------------------------------------------
 */

/* Fragment query interface. */
static HRESULT STDMETHODCALLTYPE TkUiaFragment_QueryInterface(
    IRawElementProviderFragment *this,
    REFIID riid,
    void **ppvObject)
{
    /* Just delegate to the main provider's QueryInterface. */
    return TkUiaProvider_QueryInterface((IRawElementProviderSimple *)this, riid, ppvObject);
}

/* Fragment add ref/release - reuse existing implementation. */
static ULONG STDMETHODCALLTYPE TkUiaFragment_AddRef(IRawElementProviderFragment *this) {
    return TkUiaProvider_AddRef((IRawElementProviderSimple *)this);
}

static ULONG STDMETHODCALLTYPE TkUiaFragment_Release(IRawElementProviderFragment *this) {
    return TkUiaProvider_Release((IRawElementProviderSimple *)this);
}

/* Fragment navigation - critical for listbox, tree, table items. */
static HRESULT STDMETHODCALLTYPE TkUiaFragment_Navigate(
    IRawElementProviderFragment *this,
    enum FragmentNavigation direction,
    IRawElementProviderFragment **pRetVal)
{
    if (!pRetVal) return E_INVALIDARG;
    *pRetVal = NULL;
    TkGlobalLock();

    TkUiaProvider *provider = (TkUiaProvider *)this;
    Tk_Window toplevel = GetToplevelOfWidget(provider->tkwin);

    if (!toplevel) {
        TkGlobalUnlock();
        return E_FAIL;
    }

    /* We need to determine the role of the provider's window. */
    long role = 0;
    VARIANT varRole;
    VariantInit(&varRole);
    HRESULT hrRole = TkAccRole(provider->tkwin, &varRole, ACC_ROLE_UIA);
    if (SUCCEEDED(hrRole) && varRole.vt == VT_I4) {
        role = varRole.lVal;
    }
    VariantClear(&varRole);


    switch (direction) {
    case FragmentNavigation_Parent: {
	if (provider->childId > 0) {
	    TkUiaProvider *parentProvider = GetUiaProviderForWindow(provider->tkwin);
	    if (parentProvider) {
		*pRetVal = (IRawElementProviderFragment *)parentProvider;
		TkUiaFragment_AddRef(*pRetVal);
	    }
	}
	break;
    }
    case FragmentNavigation_FirstChild: {
	int firstId = 0;
	if (provider->childId == 0 && (role == 50008 || role == 50023 || role == 50025)) { /* UIA_List, UIA_Tree, UIA_Table */
	    firstId = GetFirstChildId(toplevel, provider->tkwin, direction);
	} else if (role == 50023 && provider->childId > 0) { // Tree item may have children
	    firstId = GetFirstChildId(toplevel, provider->tkwin, direction);
	}
	if (firstId > 0) {
	    TkUiaProvider *childProvider = CreateUiaProviderForChild(provider->msaaProvider, provider->tkwin, firstId);
	    if (childProvider) {
		*pRetVal = (IRawElementProviderFragment *)childProvider;
		TkUiaFragment_AddRef(*pRetVal);
	    }
	}
	break;
    }
    case FragmentNavigation_LastChild: {
	int lastId = 0;
	if (provider->childId == 0 && (role == 50008 || role == 50023 || role == 50025)) {
	    lastId = GetLastChildId(toplevel, provider->tkwin, direction);
	} else if (role == 50023 && provider->childId > 0) {
	    lastId = GetLastChildId(toplevel, provider->tkwin, direction);
	}
	if (lastId > 0) {
	    TkUiaProvider *childProvider = CreateUiaProviderForChild(provider->msaaProvider, provider->tkwin, lastId);
	    if (childProvider) {
		*pRetVal = (IRawElementProviderFragment *)childProvider;
		TkUiaFragment_AddRef(*pRetVal);
	    }
	}
	break;
    }

    case FragmentNavigation_NextSibling:
    case FragmentNavigation_PreviousSibling: {
	if (provider->childId > 0) {
	    int siblingId = (direction == FragmentNavigation_NextSibling) ?
		GetNextSiblingId(toplevel, provider->tkwin, provider->childId, direction) :
		GetPreviousSiblingId(toplevel, provider->tkwin, provider->childId, direction);
	    if (siblingId > 0) {
		TkUiaProvider *siblingProvider = CreateUiaProviderForChild(provider->msaaProvider, provider->tkwin, siblingId);
		if (siblingProvider) {
		    *pRetVal = (IRawElementProviderFragment *)siblingProvider;
		    TkUiaFragment_AddRef(*pRetVal);
		}
	    }
	}
	break;
    }
    }

    TkGlobalUnlock();
    return *pRetVal ? S_OK : S_FALSE;
}


/* Get runtime ID. */
static HRESULT STDMETHODCALLTYPE TkUiaFragment_GetRuntimeId(
    IRawElementProviderFragment *this,
    SAFEARRAY **pRetVal)
{
    if (!pRetVal) return E_INVALIDARG;

    TkUiaProvider *provider = (TkUiaProvider *)this;
    
    /* Create runtime ID: [UiaAppendRuntimeId, hwnd, childId]. */
    int runtimeId[] = { UiaAppendRuntimeId, 
                       (int)(LONG_PTR)(provider->msaaProvider ? provider->msaaProvider->hwnd : NULL),
                       provider->childId };

    SAFEARRAY *psa = SafeArrayCreateVector(VT_I4, 0, 3);
    if (!psa) return E_OUTOFMEMORY;

    for (LONG i = 0; i < 3; i++) {
        SafeArrayPutElement(psa, &i, &runtimeId[i]);
    }

    *pRetVal = psa;
    return S_OK;
}

/* Get bounding rectangle. */
static HRESULT STDMETHODCALLTYPE TkUiaFragment_get_BoundingRectangle(
    IRawElementProviderFragment *this,
    struct UiaRect *pRetVal)
{
    if (!pRetVal) return E_INVALIDARG;

    TkUiaProvider *provider = (TkUiaProvider *)this;
    if (!provider->msaaProvider) return E_FAIL;

    memset(pRetVal, 0, sizeof(pRetVal));
    TkGlobalLock();

    VARIANT varChild;
    varChild.vt = VT_I4;
    varChild.lVal = provider->childId > 0 ? provider->childId : CHILDID_SELF;

    LONG xLeft, yTop, cxWidth, cyHeight;
    HRESULT hr = TkRootAccessible_accLocation(
        (IAccessible *)provider->msaaProvider, &xLeft, &yTop, &cxWidth, &cyHeight, varChild);

    if (SUCCEEDED(hr)) {
        /* Use left, top, width, height instead of x, y, width, height */
        pRetVal->left = (double)xLeft;
        pRetVal->top = (double)yTop;
        pRetVal->width = (double)cxWidth;
        pRetVal->height = (double)cyHeight;
    }

    TkGlobalUnlock();
    return hr;
}

/* Get Embedded Fragment Roots - not used for Tk widgets. */
static HRESULT STDMETHODCALLTYPE TkUiaFragment_GetEmbeddedFragmentRoots(
    TCL_UNUSED(IRawElementProviderFragment *), /* this */
    SAFEARRAY **pRetVal)
{
    if (!pRetVal) return E_INVALIDARG;
    *pRetVal = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    return S_OK;
}

/* Set UIA focus on fragment. */
static HRESULT STDMETHODCALLTYPE TkUiaFragment_SetFocus(
    IRawElementProviderFragment *this)
{
    TkUiaProvider *provider = (TkUiaProvider *)this;
    if (!provider->msaaProvider || !provider->tkwin) return E_FAIL;

    TkGlobalLock();

    if (provider->childId > 0) {
        /* Focus specific item. */
        Tk_Window toplevel = GetToplevelOfWidget(provider->tkwin);
        Tk_Window itemWin = GetTkWindowForChildId(provider->childId, toplevel);
        if (itemWin) {
            TkSetFocusWin((TkWindow *)itemWin, 1);  /* Use TkSetFocusWin. */
        }
    } else {
        /* Focus container. */
        TkSetFocusWin((TkWindow *)provider->tkwin, 1);  /* Use TkSetFocusWin. */
    }

    TkGlobalUnlock();
    return S_OK;
}

/* Get fragment root. */
static HRESULT STDMETHODCALLTYPE TkUiaFragment_get_FragmentRoot(
    IRawElementProviderFragment *this,
    IRawElementProviderFragmentRoot **pRetVal)
{
    if (!pRetVal) return E_INVALIDARG;

    TkUiaProvider *provider = (TkUiaProvider *)this;
    if (!provider->msaaProvider) return E_FAIL;

    /* The toplevel window is the fragment root. */
    Tk_Window toplevel = GetToplevelOfWidget(provider->tkwin);
    if (!toplevel) return E_FAIL;

    TkUiaProvider *rootProvider = GetUiaProviderForWindow(toplevel);
    if (!rootProvider) return E_FAIL;

    /* We need to implement IRawElementProviderFragmentRoot for the toplevel. */
    *pRetVal = (IRawElementProviderFragmentRoot *)rootProvider;
    TkUiaFragment_AddRef((IRawElementProviderFragment *)*pRetVal);

    return S_OK;
}

/* Functions to manage child ID's in a container window from UIA. */
static int GetFirstChildId(Tk_Window toplevel, Tk_Window container, enum FragmentNavigation direction) 
{
    if (!toplevel || !container || direction != FragmentNavigation_FirstChild) return -1;
    
    Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
    if (!childIdTable) return -1;

    Tcl_HashTable *virtualTable = GetVirtualTableForToplevel(toplevel);
    if (!virtualTable) return -1;

    TkGlobalLock();
    
    Tcl_HashEntry *vEntry = Tcl_FindHashEntry(virtualTable, container);
    if (vEntry) {
        VirtualRange *range = Tcl_GetHashValue(vEntry);
        if (range->count > 0) {
            TkGlobalUnlock();
            return range->startId;
        }
    }
    
    /* Find the smallest child ID for this container. */
    int firstId = INT_MAX;
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;
    
    for (entry = Tcl_FirstHashEntry(childIdTable, &search); entry != NULL; entry = Tcl_NextHashEntry(&search)) {
        Tk_Window win = (Tk_Window)Tcl_GetHashKey(childIdTable, entry);
        int childId = PTR2INT(Tcl_GetHashValue(entry));
        
        if (IsChildOfContainer(win, container) && childId < firstId) {
            firstId = childId;
        }
    }
    
    TkGlobalUnlock();
    return (firstId != INT_MAX) ? firstId : -1;
}


static int GetLastChildId(Tk_Window toplevel, Tk_Window container, enum FragmentNavigation direction) 
{
    if (!toplevel || !container || direction != FragmentNavigation_LastChild) return -1;
    
    Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
    if (!childIdTable) return -1;
    
    Tcl_HashTable *virtualTable = GetVirtualTableForToplevel(toplevel);
    if (!virtualTable) return -1;

    TkGlobalLock();
    
    Tcl_HashEntry *vEntry = Tcl_FindHashEntry(virtualTable, container);
    if (vEntry) {
        VirtualRange *range = Tcl_GetHashValue(vEntry);
        if (range->count > 0) {
            TkGlobalUnlock();
            return range->startId + range->count - 1; // Last virtual child ID
        }
    }
    
    int lastId = -1;
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;
    
    for (entry = Tcl_FirstHashEntry(childIdTable, &search); entry != NULL; entry = Tcl_NextHashEntry(&search)) {
        Tk_Window win = (Tk_Window)Tcl_GetHashKey(childIdTable, entry);
        int childId = PTR2INT(Tcl_GetHashValue(entry));
        
        if (IsChildOfContainer(win, container) && childId > lastId) {
            lastId = childId;
        }
    }
    
    TkGlobalUnlock();
    return lastId;
}


static int GetNextSiblingId(Tk_Window toplevel, Tk_Window container, int currentId, enum FragmentNavigation direction)
{		
    if (!toplevel || !container || direction != FragmentNavigation_NextSibling) return -1;
    
    Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
    if (!childIdTable) return -1;
    
    Tcl_HashTable *virtualTable = GetVirtualTableForToplevel(toplevel);
    if (!virtualTable) return -1;

    TkGlobalLock();
    
    Tcl_HashEntry *vEntry = Tcl_FindHashEntry(virtualTable, container);
    if (vEntry) {
        VirtualRange *range = Tcl_GetHashValue(vEntry);
        if (range->count > 0) {
            int index = currentId - range->startId + 1;
            if (index < range->count) {
                TkGlobalUnlock();
                return currentId + 1; // Next virtual child ID
            }
        }
    }
    
    int nextId = INT_MAX;
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;
    
    for (entry = Tcl_FirstHashEntry(childIdTable, &search); entry != NULL; entry = Tcl_NextHashEntry(&search)) {
        Tk_Window win = (Tk_Window)Tcl_GetHashKey(childIdTable, entry);
        int childId = PTR2INT(Tcl_GetHashValue(entry));
        
        if (IsChildOfContainer(win, container) && childId > currentId && childId < nextId) {
            nextId = childId;
        }
    }
    
    TkGlobalUnlock();
    return (nextId != INT_MAX) ? nextId : -1;
}

static int GetPreviousSiblingId(Tk_Window toplevel, Tk_Window container, int currentId, enum FragmentNavigation direction)
{	
    if (!toplevel || !container || direction != FragmentNavigation_PreviousSibling) return -1;
    
    Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
    if (!childIdTable) return -1;
    
    Tcl_HashTable *virtualTable = GetVirtualTableForToplevel(toplevel);
    if (!virtualTable) return -1;

    TkGlobalLock();
    
    Tcl_HashEntry *vEntry = Tcl_FindHashEntry(virtualTable, container);
    if (vEntry) {
        VirtualRange *range = Tcl_GetHashValue(vEntry);
        if (range->count > 0) {
            int index = currentId - range->startId + 1;
            if (index > 0) {
                TkGlobalUnlock();
                return currentId - 1; // Previous virtual child ID
            }
        }
    }
    
    int prevId = -1;
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;
    
    for (entry = Tcl_FirstHashEntry(childIdTable, &search); entry != NULL; entry = Tcl_NextHashEntry(&search)) {
        Tk_Window win = (Tk_Window)Tcl_GetHashKey(childIdTable, entry);
        int childId = PTR2INT(Tcl_GetHashValue(entry));
        
        if (IsChildOfContainer(win, container) && childId < currentId && childId > prevId) {
            prevId = childId;
        }
    }
    
    TkGlobalUnlock();
    return prevId;
}

/* Check if window is child of container. */
static BOOL IsChildOfContainer(Tk_Window child, Tk_Window container)
{
    if (!child || !container) return FALSE;
    
    Tk_Window parent = Tk_Parent(child);
    while (parent && parent != container) {
        parent = Tk_Parent(parent);
    }
    return (parent == container);
}

/* Create UIA provider for child items. */
static TkUiaProvider *CreateUiaProviderForChild(TkRootAccessible *msaaProvider, Tk_Window container, int childId)
{
    if (!msaaProvider || !container || childId <= 0) return NULL;

    TkUiaProvider *provider = (TkUiaProvider *)ckalloc(sizeof(TkUiaProvider));
    if (!provider) return NULL;

    provider->lpVtbl = &tkUiaProviderVtbl;
    provider->fragmentVtbl = &tkUiaFragmentVtbl;
    provider->msaaProvider = msaaProvider;
    provider->tkwin = container;
    provider->childId = childId;
    provider->refCount = 1;

    TkRootAccessible_AddRef((IAccessible *)msaaProvider);

    return provider;
}

/*
 * Check if a role represents a container with virtual children.
 */
static BOOL IsVirtualChildContainer(LONG role)
{
    return (role == 50008 ||           /* Listbox */
            role == 50023 ||        /* Tree */
            role == 50025);          /* Table */
}

/*
 * Get all virtual child IDs for a container window.
 */
static int GetVirtualChildIds(Tk_Window toplevel, Tk_Window container, int **childIds)
{
    if (!toplevel || !container || !childIds) return 0;
    
    Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
    if (!childIdTable) return 0;
    
    TkGlobalLock();
    
    /* Count children first. */
    int count = 0;
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;
    
    for (entry = Tcl_FirstHashEntry(childIdTable, &search); 
         entry != NULL; 
         entry = Tcl_NextHashEntry(&search)) {
        Tk_Window win = (Tk_Window)Tcl_GetHashKey(childIdTable, entry);
        if (win == container || Tk_Parent(win) == container) {
            count++;
        }
    }
    
    if (count == 0) {
        TkGlobalUnlock();
        *childIds = NULL;
        return 0;
    }
    
    /* Allocate and fill array. */
    *childIds = (int *)ckalloc(count * sizeof(int));
    int index = 0;
    
    for (entry = Tcl_FirstHashEntry(childIdTable, &search); 
         entry != NULL; 
         entry = Tcl_NextHashEntry(&search)) {
        Tk_Window win = (Tk_Window)Tcl_GetHashKey(childIdTable, entry);
        if (win == container || Tk_Parent(win) == container) {
            (*childIds)[index++] = PTR2INT(Tcl_GetHashValue(entry));
        }
    }
    
    TkGlobalUnlock();
    return count;
}

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
    

    /* For UIA requests (both Simple and Fragment interfaces) */
    if (lParam == (LPARAM)&IID_IRawElementProviderSimple || 
        lParam == (LPARAM)&IID_IRawElementProviderFragment) {
	InitializeUIAutomation();

	TkUiaProvider *uiaProvider = GetUiaProviderForWindow(tkwin);
	if (!uiaProvider) {
	    TkRootAccessible *msaaProvider = GetTkAccessibleForWindow(tkwin);
	    if (!msaaProvider) {
		/* Create MSAA provider for this window. */
		Tcl_Interp *interp = Tk_Interp(tkwin);
		if (!interp) return;

		msaaProvider = CreateRootAccessible(interp, hwnd, Tk_PathName(tkwin));
		if (msaaProvider) {
		    TkRootAccessible_RegisterForCleanup(tkwin, msaaProvider);
		}
	    }
	    if (msaaProvider) {
		uiaProvider = CreateUiaProvider(msaaProvider, tkwin, 0);
		if (uiaProvider) {
		    TkGlobalLock();
		    if (!tkUiaProviderTableInitialized) {
			InitUiaProviderTable();
		    }
		    Tcl_HashEntry *entry;
		    int newEntry;
		    entry = Tcl_CreateHashEntry(tkUiaProviderTable, tkwin, &newEntry);
		    Tcl_SetHashValue(entry, uiaProvider);
		    TkGlobalUnlock();
		}
	    }
	}
	if (uiaProvider && outResult) {
	  if (wParam > 0 && uiaProvider) {
            /* This is a request for a specific child element. */
            TkUiaProvider *childProvider = CreateUiaProviderForChild(
                uiaProvider->msaaProvider, tkwin, (int)wParam);
            if (childProvider) {
                *outResult = UiaReturnRawElementProvider(hwnd, wParam, lParam, 
                    (IRawElementProviderSimple *)childProvider);
                TkGlobalUnlock();
                return;
            }
        }
	}
	return;
    }

    /*
     * For MSAA requests, let the existing implementation handle it.
     * This includes native menu objects.
     */
    if ((LONG)lParam == OBJID_CLIENT) {
	TkRootAccessible *acc = GetTkAccessibleForWindow(tkwin);
	if (!acc) {
	    Tcl_Interp *interp = Tk_Interp(tkwin);
	    if (!interp) return;
	    acc = CreateRootAccessible(interp, hwnd, Tk_PathName(tkwin));
	    if (acc) {
		TkRootAccessible_RegisterForCleanup(tkwin, acc);
	    }
	}
	if (acc && outResult) {
	    *outResult = LresultFromObject(&IID_IAccessible, wParam, (IUnknown *)acc);
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
    TCL_UNUSED(void *), /* clientData */
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

    /* Update checked state. */
    ComputeAndCacheCheckedState(path, ip);

    TkGlobalLock();
    Tcl_HashTable *childIdTable = GetChildIdTableForToplevel(toplevel);
    LONG childId = GetChildIdForTkWindow(path, childIdTable);

    if (childId > 0) {
	HWND hwnd = Tk_GetHWND(Tk_WindowId(toplevel));

	/* Send comprehensive notifications. */
	NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, hwnd, OBJID_CLIENT, childId);
	NotifyWinEvent(EVENT_OBJECT_STATECHANGE, hwnd, OBJID_CLIENT, childId);
	NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, hwnd, OBJID_CLIENT, childId);

	/* Force UIA property change notification. */
	TkUiaProvider *tkProvider = GetUiaProviderForWindow(toplevel);
	if (tkProvider) {
	    IRawElementProviderSimple *provider = (IRawElementProviderSimple *)tkProvider;
	    VARIANT oldVal, newVal;
	    VariantInit(&oldVal);
	    VariantInit(&newVal);

	    /* Raise selection and name property change events for Narrator */
	    UiaRaiseAutomationEvent(provider, UIA_SelectionItem_ElementSelectedEventId);
	    UiaRaiseAutomationPropertyChangedEvent(provider,
						   UIA_NamePropertyId, oldVal, newVal);

	    VariantClear(&oldVal);
	    VariantClear(&newVal);
	}

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

    /* Clean up MSAA table */
    if (tkAccessibleTableInitialized) {
	Tcl_HashEntry *entry = Tcl_FindHashEntry(tkAccessibleTable, tkAccessible->toplevel);
	if (entry) {
	    Tcl_DeleteHashEntry(entry);
	}
    }

    /* Clean up UI Automation provider table */
    if (tkUiaProviderTableInitialized) {
	Tcl_HashEntry *entry = Tcl_FindHashEntry(tkUiaProviderTable, tkAccessible->toplevel);
	if (entry) {
	    TkUiaProvider *uiaProvider = (TkUiaProvider *)Tcl_GetHashValue(entry);
	    if (uiaProvider) {
		TkUiaProvider_Release((IRawElementProviderSimple *)uiaProvider);
	    }
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
 *      widgets on Windows using the Microsoft Active Accessibility API.
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
    InitUiaProviderTable();
    TkGlobalUnlock();

    /* Initialize UI Automation */
    InitializeUIAutomation();

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
