/*
 * Copyright 2005, Joe English.  Freely redistributable.
 *
 * Support routines for geometry managers.
 */

#include "tkInt.h"
#include "ttkManager.h"

/*------------------------------------------------------------------------
 * +++ The Geometry Propagation Dance.
 *
 * When a slave window requests a new size or some other parameter changes,
 * the manager recomputes the required size for the container window and calls
 * Tk_GeometryRequest().  This is scheduled as an idle handler so multiple
 * updates can be processed as a single batch.
 *
 * If all goes well, the container's manager will process the request
 * (and so on up the chain to the toplevel window), and the container
 * window will eventually receive a <Configure> event.  At this point
 * it recomputes the size and position of all slaves and places them.
 *
 * If all does not go well, however, the container's request may be ignored
 * (typically because the top-level window has a fixed, user-specified size).
 * Tk doesn't provide any notification when this happens; to account for this,
 * we also schedule an idle handler to call the layout procedure
 * after making a geometry request.
 *
 * +++ Content window removal <<NOTE-LOSTCONTENT>>.
 *
 * There are three conditions under which a content window is removed:
 *
 * (1) Another GM claims control
 * (2) Manager voluntarily relinquishes control
 * (3) Content window is destroyed
 *
 * In case (1), Tk calls the manager's lostSlaveProc.
 * Case (2) is performed by calling Tk_ManageGeometry(slave,NULL,0);
 * in this case Tk does _not_ call the LostSlaveProc (documented behavior).
 * Tk doesn't handle case (3) either; to account for that we
 * register an event handler on the slave widget to track <Destroy> events.
 */

/* ++ Data structures.
 */
typedef struct
{
    Tk_Window 		contentWindow;
    Ttk_Manager 	*manager;
    void 		*data;
    unsigned		flags;
} Ttk_Content;

/* slave->flags bits:
 */
#define CONTENT_MAPPED 	0x1	/* content windows to be mapped when container is */

struct TtkManager_
{
    Ttk_ManagerSpec	*managerSpec;
    void 		*managerData;
    Tk_Window   	window;
    unsigned		flags;
    TkSizeT 	 	nManaged;
    Ttk_Content 		**content;
};

/* manager->flags bits:
 */
#define MGR_UPDATE_PENDING	0x1
#define MGR_RESIZE_REQUIRED	0x2
#define MGR_RELAYOUT_REQUIRED	0x4

static void ManagerIdleProc(void *);	/* forward */

/* ++ ScheduleUpdate --
 * 	Schedule a call to recompute the size and/or layout,
 *	depending on flags.
 */
static void ScheduleUpdate(Ttk_Manager *mgr, unsigned flags)
{
    if (!(mgr->flags & MGR_UPDATE_PENDING)) {
	Tcl_DoWhenIdle(ManagerIdleProc, mgr);
	mgr->flags |= MGR_UPDATE_PENDING;
    }
    mgr->flags |= flags;
}

/* ++ RecomputeSize --
 * 	Recomputes the required size of the container window,
 * 	makes geometry request.
 */
static void RecomputeSize(Ttk_Manager *mgr)
{
    int width = 1, height = 1;

    if (mgr->managerSpec->RequestedSize(mgr->managerData, &width, &height)) {
	Tk_GeometryRequest(mgr->window, width, height);
	ScheduleUpdate(mgr, MGR_RELAYOUT_REQUIRED);
    }
    mgr->flags &= ~MGR_RESIZE_REQUIRED;
}

/* ++ RecomputeLayout --
 * 	Recompute geometry of all slaves.
 */
static void RecomputeLayout(Ttk_Manager *mgr)
{
    mgr->managerSpec->PlaceSlaves(mgr->managerData);
    mgr->flags &= ~MGR_RELAYOUT_REQUIRED;
}

/* ++ ManagerIdleProc --
 * 	DoWhenIdle procedure for deferred updates.
 */
static void ManagerIdleProc(ClientData clientData)
{
    Ttk_Manager *mgr = (Ttk_Manager *)clientData;
    mgr->flags &= ~MGR_UPDATE_PENDING;

    if (mgr->flags & MGR_RESIZE_REQUIRED) {
	RecomputeSize(mgr);
    }
    if (mgr->flags & MGR_RELAYOUT_REQUIRED) {
	if (mgr->flags & MGR_UPDATE_PENDING) {
	    /* RecomputeSize has scheduled another update; relayout later */
	    return;
	}
	RecomputeLayout(mgr);
    }
}

/*------------------------------------------------------------------------
 * +++ Event handlers.
 */

/* ++ ManagerEventHandler --
 * 	Recompute slave layout when container widget is resized.
 * 	Keep the slave's map state in sync with the container's.
 */
static const int ManagerEventMask = StructureNotifyMask;
static void ManagerEventHandler(ClientData clientData, XEvent *eventPtr)
{
    Ttk_Manager *mgr = (Ttk_Manager *)clientData;
    TkSizeT i;

    switch (eventPtr->type)
    {
	case ConfigureNotify:
	    RecomputeLayout(mgr);
	    break;
	case MapNotify:
	    for (i = 0; i < mgr->nManaged; ++i) {
		Ttk_Content *slave = mgr->content[i];
		if (slave->flags & CONTENT_MAPPED) {
		    Tk_MapWindow(slave->contentWindow);
		}
	    }
	    break;
	case UnmapNotify:
	    for (i = 0; i < mgr->nManaged; ++i) {
		Ttk_Content *slave = mgr->content[i];
		Tk_UnmapWindow(slave->contentWindow);
	    }
	    break;
    }
}

/* ++ ContentLostEventHandler --
 * 	Notifies manager when a content window is destroyed
 * 	(see <<NOTE-LOSTCONTENT>>).
 */
static void ContentLostEventHandler(void *clientData, XEvent *eventPtr)
{
    Ttk_Content *slave = (Ttk_Content *)clientData;
    if (eventPtr->type == DestroyNotify) {
	slave->manager->managerSpec->tkGeomMgr.lostSlaveProc(
	    slave->manager, slave->contentWindow);
    }
}

/*------------------------------------------------------------------------
 * +++ Slave initialization and cleanup.
 */

static Ttk_Content *NewContent(
    Ttk_Manager *mgr, Tk_Window contentWindow, void *data)
{
    Ttk_Content *content = (Ttk_Content *)ckalloc(sizeof(Ttk_Content));

    content->contentWindow = contentWindow;
    content->manager = mgr;
    content->flags = 0;
    content->data = data;

    return content;
}

static void DeleteSlave(Ttk_Content *content)
{
    ckfree(content);
}

/*------------------------------------------------------------------------
 * +++ Manager initialization and cleanup.
 */

Ttk_Manager *Ttk_CreateManager(
    Ttk_ManagerSpec *managerSpec, void *managerData, Tk_Window window)
{
    Ttk_Manager *mgr = (Ttk_Manager *)ckalloc(sizeof(*mgr));

    mgr->managerSpec 	= managerSpec;
    mgr->managerData	= managerData;
    mgr->window	= window;
    mgr->nManaged 	= 0;
    mgr->content 	= NULL;
    mgr->flags  	= 0;

    Tk_CreateEventHandler(
	mgr->window, ManagerEventMask, ManagerEventHandler, mgr);

    return mgr;
}

void Ttk_DeleteManager(Ttk_Manager *mgr)
{
    Tk_DeleteEventHandler(
	mgr->window, ManagerEventMask, ManagerEventHandler, mgr);

    while (mgr->nManaged > 0) {
	Ttk_ForgetSlave(mgr, mgr->nManaged - 1);
    }
    if (mgr->content) {
	ckfree(mgr->content);
    }

    Tcl_CancelIdleCall(ManagerIdleProc, mgr);

    ckfree(mgr);
}

/*------------------------------------------------------------------------
 * +++ Slave management.
 */

/* ++ InsertContent --
 * 	Adds content to the list of managed windows.
 */
static void InsertContent(Ttk_Manager *mgr, Ttk_Content *content, TkSizeT index)
{
    TkSizeT endIndex = mgr->nManaged++;
    mgr->content = (Ttk_Content **)ckrealloc(mgr->content, mgr->nManaged * sizeof(Ttk_Content *));

    while (endIndex > index) {
	mgr->content[endIndex] = mgr->content[endIndex - 1];
	--endIndex;
    }

    mgr->content[index] = content;

    Tk_ManageGeometry(content->contentWindow,
	&mgr->managerSpec->tkGeomMgr, mgr);

    Tk_CreateEventHandler(content->contentWindow,
	StructureNotifyMask, ContentLostEventHandler, content);

    ScheduleUpdate(mgr, MGR_RESIZE_REQUIRED);
}

/* RemoveSlave --
 * 	Unmanage and delete the slave.
 *
 * NOTES/ASSUMPTIONS:
 *
 * [1] It's safe to call Tk_UnmapWindow / Tk_UnmaintainGeometry even if this
 * routine is called from the slave's DestroyNotify event handler.
 */
static void RemoveSlave(Ttk_Manager *mgr, TkSizeT index)
{
    Ttk_Content *slave = mgr->content[index];
    TkSizeT i;

    /* Notify manager:
     */
    mgr->managerSpec->SlaveRemoved(mgr->managerData, index);

    /* Remove from array:
     */
    --mgr->nManaged;
    for (i = index ; i < mgr->nManaged; ++i) {
	mgr->content[i] = mgr->content[i+1];
    }

    /* Clean up:
     */
    Tk_DeleteEventHandler(
	slave->contentWindow, StructureNotifyMask, ContentLostEventHandler, slave);

    /* Note [1] */
    Tk_UnmaintainGeometry(slave->contentWindow, mgr->window);
    Tk_UnmapWindow(slave->contentWindow);

    DeleteSlave(slave);

    ScheduleUpdate(mgr, MGR_RESIZE_REQUIRED);
}

/*------------------------------------------------------------------------
 * +++ Tk_GeomMgr hooks.
 */

void Ttk_GeometryRequestProc(ClientData clientData, Tk_Window contentWindow)
{
    Ttk_Manager *mgr = (Ttk_Manager *)clientData;
    TkSizeT index = Ttk_ContentIndex(mgr, contentWindow);
    int reqWidth = Tk_ReqWidth(contentWindow);
    int reqHeight= Tk_ReqHeight(contentWindow);

    if (mgr->managerSpec->SlaveRequest(
		mgr->managerData, index, reqWidth, reqHeight))
    {
	ScheduleUpdate(mgr, MGR_RESIZE_REQUIRED);
    }
}

void Ttk_LostContentProc(ClientData clientData, Tk_Window contentWindow)
{
    Ttk_Manager *mgr = (Ttk_Manager *)clientData;
    TkSizeT index = Ttk_ContentIndex(mgr, contentWindow);

    /* ASSERT: index >= 0 */
    RemoveSlave(mgr, index);
}

/*------------------------------------------------------------------------
 * +++ Public API.
 */

/* ++ Ttk_InsertContent --
 * 	Add a new content window at the specified index.
 */
void Ttk_InsertContent(
    Ttk_Manager *mgr, TkSizeT index, Tk_Window tkwin, void *data)
{
    Ttk_Content *slave = NewContent(mgr, tkwin, data);
    InsertContent(mgr, slave, index);
}

/* ++ Ttk_ForgetContent --
 * 	Unmanage the specified content window.
 */
void Ttk_ForgetSlave(Ttk_Manager *mgr, TkSizeT index)
{
    Tk_Window contentWindow = mgr->content[index]->contentWindow;
    RemoveSlave(mgr, index);
    Tk_ManageGeometry(contentWindow, NULL, 0);
}

/* ++ Ttk_PlaceContent --
 * 	Set the position and size of the specified content window.
 *
 * NOTES:
 * 	Contrary to documentation, Tk_MaintainGeometry doesn't always
 * 	map the content window.
 */
void Ttk_PlaceSlave(
    Ttk_Manager *mgr, TkSizeT index, int x, int y, int width, int height)
{
    Ttk_Content *slave = mgr->content[index];
    Tk_MaintainGeometry(slave->contentWindow,mgr->window,x,y,width,height);
    slave->flags |= CONTENT_MAPPED;
    if (Tk_IsMapped(mgr->window)) {
	Tk_MapWindow(slave->contentWindow);
    }
}

/* ++ Ttk_UnmapContent --
 * 	Unmap the specified content window, but leave it managed.
 */
void Ttk_UnmapSlave(Ttk_Manager *mgr, TkSizeT index)
{
    Ttk_Content *slave = mgr->content[index];
    Tk_UnmaintainGeometry(slave->contentWindow, mgr->window);
    slave->flags &= ~CONTENT_MAPPED;
    /* Contrary to documentation, Tk_UnmaintainGeometry doesn't always
     * unmap the content window:
     */
    Tk_UnmapWindow(slave->contentWindow);
}

/* LayoutChanged, SizeChanged --
 * 	Schedule a relayout, resp. resize request.
 */
void Ttk_ManagerLayoutChanged(Ttk_Manager *mgr)
{
    ScheduleUpdate(mgr, MGR_RELAYOUT_REQUIRED);
}

void Ttk_ManagerSizeChanged(Ttk_Manager *mgr)
{
    ScheduleUpdate(mgr, MGR_RESIZE_REQUIRED);
}

/* +++ Accessors.
 */
TkSizeT Ttk_NumberSlaves(Ttk_Manager *mgr)
{
    return mgr->nManaged;
}
void *Ttk_ContentData(Ttk_Manager *mgr, TkSizeT index)
{
    return mgr->content[index]->data;
}
Tk_Window Ttk_ContentWindow(Ttk_Manager *mgr, TkSizeT index)
{
    return mgr->content[index]->contentWindow;
}

/*------------------------------------------------------------------------
 * +++ Utility routines.
 */

/* ++ Ttk_ContentIndex --
 * 	Returns the index of specified content window, -1 if not found.
 */
TkSizeT Ttk_ContentIndex(Ttk_Manager *mgr, Tk_Window contentWindow)
{
    TkSizeT index;
    for (index = 0; index < mgr->nManaged; ++index)
	if (mgr->content[index]->contentWindow == contentWindow)
	    return index;
    return TCL_INDEX_NONE;
}

/* ++ Ttk_GetSlaveIndexFromObj(interp, mgr, objPtr, indexPtr) --
 * 	Return the index of the content window specified by objPtr.
 * 	Content windows may be specified as an integer index or
 * 	as the name of the managed window.
 *
 * Returns:
 * 	Standard Tcl completion code.  Leaves an error message in case of error.
 */

int Ttk_GetContentIndexFromObj(
    Tcl_Interp *interp, Ttk_Manager *mgr, Tcl_Obj *objPtr, TkSizeT *indexPtr)
{
    const char *string = Tcl_GetString(objPtr);
    TkSizeT index = 0;
    Tk_Window tkwin;

    /* Try interpreting as an integer first:
     */
    if (TkGetIntForIndex(objPtr, mgr->nManaged - 1, 1, &index) == TCL_OK) {
	if (index + 1 > mgr->nManaged + 1) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"Managed window index %d out of bounds", (int)index));
	    Tcl_SetErrorCode(interp, "TTK", "MANAGED", "INDEX", NULL);
	    return TCL_ERROR;
	}
	*indexPtr = index;
	return TCL_OK;
    }

    /* Try interpreting as a slave window name;
     */
    if ((*string == '.') &&
	    (tkwin = Tk_NameToWindow(interp, string, mgr->window))) {
	index = Ttk_ContentIndex(mgr, tkwin);
	if (index == TCL_INDEX_NONE) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "%s is not managed by %s", string,
		    Tk_PathName(mgr->window)));
	    Tcl_SetErrorCode(interp, "TTK", "MANAGED", "MANAGER", NULL);
	    return TCL_ERROR;
	}
	*indexPtr = index;
	return TCL_OK;
    }

    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "Invalid managed window specification %s", string));
    Tcl_SetErrorCode(interp, "TTK", "MANAGED", "SPEC", NULL);
    return TCL_ERROR;
}

/* ++ Ttk_ReorderContent(mgr, fromIndex, toIndex) --
 * 	Change content window order.
 */
void Ttk_ReorderSlave(Ttk_Manager *mgr, TkSizeT fromIndex, TkSizeT toIndex)
{
    Ttk_Content *moved = mgr->content[fromIndex];

    /* Shuffle down: */
    while (fromIndex > toIndex) {
	mgr->content[fromIndex] = mgr->content[fromIndex - 1];
	--fromIndex;
    }
    /* Or, shuffle up: */
    while (fromIndex < toIndex) {
	mgr->content[fromIndex] = mgr->content[fromIndex + 1];
	++fromIndex;
    }
    /* ASSERT: fromIndex == toIndex */
    mgr->content[fromIndex] = moved;

    /* Schedule a relayout.  In general, rearranging slaves
     * may also change the size:
     */
    ScheduleUpdate(mgr, MGR_RESIZE_REQUIRED);
}

/* ++ Ttk_Maintainable(interp, slave, container) --
 * 	Utility routine.  Verifies that 'container' may be used to maintain
 *	the geometry of 'slave' via Tk_MaintainGeometry:
 *
 * 	+ 'container' is either 'slave's parent -OR-
 * 	+ 'container is a descendant of 'slave's parent.
 * 	+ 'slave' is not a toplevel window
 * 	+ 'slave' belongs to the same toplevel as 'container'
 *
 * Returns: 1 if OK; otherwise 0, leaving an error message in 'interp'.
 */
int Ttk_Maintainable(Tcl_Interp *interp, Tk_Window slave, Tk_Window container)
{
    Tk_Window ancestor = container, parent = Tk_Parent(slave);

    if (Tk_IsTopLevel(slave) || slave == container) {
	goto badWindow;
    }

    while (ancestor != parent) {
	if (Tk_IsTopLevel(ancestor)) {
	    goto badWindow;
	}
	ancestor = Tk_Parent(ancestor);
    }

    return 1;

badWindow:
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't add %s as slave of %s",
	    Tk_PathName(slave), Tk_PathName(container)));
    Tcl_SetErrorCode(interp, "TTK", "GEOMETRY", "MAINTAINABLE", NULL);
    return 0;
}

