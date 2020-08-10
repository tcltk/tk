/*
 * Copyright (c) 2005, Joe English.  Freely redistributable.
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
 * Place sets the position and size of all managed content windows
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
    void (*Place)(void *managerData);
    int  (*ContentRequest)(void *managerData, int index, int w, int h);
    void (*ContentRemoved)(void *managerData, int index);
} Ttk_ManagerSpec;

/*
 * Default implementations for Tk_GeomMgr hooks:
 */
MODULE_SCOPE void Ttk_GeometryRequestProc(ClientData, Tk_Window window);
MODULE_SCOPE void Ttk_LostContentProc(ClientData, Tk_Window window);

/*
 * Public API:
 */
MODULE_SCOPE Ttk_Manager *Ttk_CreateManager(
	Ttk_ManagerSpec *, void *managerData, Tk_Window containerWindow);
MODULE_SCOPE void Ttk_DeleteManager(Ttk_Manager *);

MODULE_SCOPE void Ttk_InsertContent(
    Ttk_Manager *, int position, Tk_Window, void *contentData);

MODULE_SCOPE void Ttk_ForgetContent(Ttk_Manager *, int index);

MODULE_SCOPE void Ttk_ReorderContent(Ttk_Manager *, int fromIndex, int toIndex);
    /* Rearrange content window positions */

MODULE_SCOPE void Ttk_PlaceContent(
    Ttk_Manager *, int index, int x, int y, int width, int height);
    /* Position and map the content window */

MODULE_SCOPE void Ttk_UnmapContent(Ttk_Manager *, int index);
    /* Unmap the content window */

MODULE_SCOPE void Ttk_ManagerSizeChanged(Ttk_Manager *);
MODULE_SCOPE void Ttk_ManagerLayoutChanged(Ttk_Manager *);
    /* Notify manager that size (resp. layout) needs to be recomputed */

/* Utilities:
 */
MODULE_SCOPE int Ttk_ContentIndex(Ttk_Manager *, Tk_Window);
    /* Returns: index in content window array of specified window, -1 if not found */

MODULE_SCOPE int Ttk_GetContentIndexFromObj(
    Tcl_Interp *, Ttk_Manager *, Tcl_Obj *, int *indexPtr);

/* Accessor functions:
 */
MODULE_SCOPE int Ttk_NumberContent(Ttk_Manager *);
    /* Returns: number of managed content windows */

MODULE_SCOPE void *Ttk_ContentData(Ttk_Manager *, int index);
    /* Returns: client data associated with content */

MODULE_SCOPE Tk_Window Ttk_ContentWindow(Ttk_Manager *, int index);
    /* Returns: content window */

MODULE_SCOPE int Ttk_Maintainable(Tcl_Interp *, Tk_Window content, Tk_Window container);
    /* Returns: 1 if container can manage content; 0 otherwise leaving error msg */

#endif /* _TTKMANAGER */
