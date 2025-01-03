/*
 * Copyright © 2005 Joe English.  Freely redistributable.
 *
 * Geometry manager utilities.
 */

#ifndef _TTKMANAGER
#define _TTKMANAGER

#include "ttkTheme.h"

typedef struct TtkManager_ Ttk_Manager;

/*
 * Geometry manager specification record:
 *
 * RequestedSize computes the requested size of the container window.
 *
 * PlaceContent sets the position and size of all managed content windows
 * by calling Ttk_PlaceContent().
 *
 * ContentRemoved() is called immediately before a content window is removed.
 * NB: the associated content window may have been destroyed when this
 * routine is called.
 *
 * ContentRequest() is called when a content window requests a size change.
 * It should return 1 if the request should propagate, 0 otherwise.
 */
typedef struct {			/* Manager hooks */
    Tk_GeomMgr tkGeomMgr;		/* "real" Tk Geometry Manager */

    int  (*RequestedSize)(void *managerData, int *widthPtr, int *heightPtr);
    void (*PlaceContent)(void *managerData);
    int  (*ContentRequest)(void *managerData, Tcl_Size index, int width, int height);
    void (*ContentRemoved)(void *managerData, Tcl_Size index);
} Ttk_ManagerSpec;

/*
 * Default implementations for Tk_GeomMgr hooks:
 */
#define Ttk_LostSlaveProc Ttk_LostContentProc
MODULE_SCOPE void Ttk_GeometryRequestProc(void *, Tk_Window window);
MODULE_SCOPE void Ttk_LostContentProc(void *, Tk_Window window);

/*
 * Public API:
 */
MODULE_SCOPE Ttk_Manager *Ttk_CreateManager(
	const Ttk_ManagerSpec *, void *managerData, Tk_Window window);
MODULE_SCOPE void Ttk_DeleteManager(Ttk_Manager *);

#define  Ttk_InsertSlave Ttk_InsertContent
MODULE_SCOPE void Ttk_InsertContent(
    Ttk_Manager *, Tcl_Size position, Tk_Window, void *clientData);

#define Ttk_ForgetSlave Ttk_ForgetContent
MODULE_SCOPE void Ttk_ForgetContent(Ttk_Manager *, Tcl_Size index);

#define Ttk_ReorderSlave Ttk_ReorderContent
MODULE_SCOPE void Ttk_ReorderContent(Ttk_Manager *, Tcl_Size fromIndex, Tcl_Size toIndex);
    /* Rearrange content window positions */

#define Ttk_PlaceSlave Ttk_PlaceContent
MODULE_SCOPE void Ttk_PlaceContent(
    Ttk_Manager *, Tcl_Size index, int x, int y, int width, int height);
    /* Position and map the content window */

#define Ttk_UnmapSlave Ttk_UnmapContent
MODULE_SCOPE void Ttk_UnmapContent(Ttk_Manager *, Tcl_Size index);
    /* Unmap the content window */

MODULE_SCOPE void Ttk_ManagerSizeChanged(Ttk_Manager *);
MODULE_SCOPE void Ttk_ManagerLayoutChanged(Ttk_Manager *);
    /* Notify manager that size (resp. layout) needs to be recomputed */

/* Utilities:
 */
#define Ttk_SlaveIndex Ttk_ContentIndex
MODULE_SCOPE Tcl_Size Ttk_ContentIndex(Ttk_Manager *, Tk_Window);
    /* Returns: index in content array of specified window, TCL_INDEX_NONE if not found */

#define Ttk_GetSlaveIndexFromObj Ttk_GetContentIndexFromObj
MODULE_SCOPE int Ttk_GetContentIndexFromObj(
    Tcl_Interp *, Ttk_Manager *, Tcl_Obj *, int lastOK, Tcl_Size *indexPtr);

/* Accessor functions:
 */
#define Ttk_NumberSlaves Ttk_NumberContent
MODULE_SCOPE Tcl_Size Ttk_NumberContent(Ttk_Manager *);
    /* Returns: number of managed content windows */

#define Ttk_SlaveData Ttk_ContentData
MODULE_SCOPE void *Ttk_ContentData(Ttk_Manager *, Tcl_Size index);
    /* Returns: client data associated with content window */

#define Ttk_SlaveWindow Ttk_ContentWindow
MODULE_SCOPE Tk_Window Ttk_ContentWindow(Ttk_Manager *, Tcl_Size index);
    /* Returns: content window */

MODULE_SCOPE int Ttk_Maintainable(Tcl_Interp *, Tk_Window content, Tk_Window container);
    /* Returns: 1 if container can manage content; 0 otherwise leaving error msg */

#endif /* _TTKMANAGER */
