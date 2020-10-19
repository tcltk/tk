/*
 * 	tkUnixSysNotify.c implements a "sysnotify" Tcl command which
 permits one to post system notifications based on the libnotify API.
 *
 * Copyright (c) 2020 Kevin Walzer/WordTech Communications LLC.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.

 */

#include "tkInt.h"
#include "tkUnixInt.h"

#ifdef HAVE_LIBNOTIFY

#include <libnotify/notify.h>

/*
 * Forward declarations for procedures defined in this file.
 */

static void SysNotifyDeleteCmd (void *);
static int SysNotifyCmd(void *, Tcl_Interp *, int, Tcl_Obj * const*);
int SysNotify_Init(Tcl_Interp *);

/*
 *----------------------------------------------------------------------
 *
 * SysNotifyDeleteCmd --
 *
 *      Delete notification and clean up.
 *
 * Results:
 *	Window destroyed.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------z---------------------------------------
 */


static void SysNotifyDeleteCmd (
    TCL_UNUSED(void *))
{
    notify_uninit();
}


/*
 *----------------------------------------------------------------------
 *
 * SysNotifyCreateCmd --
 *
 *      Create tray command and (unreal) window.
 *
 * Results:
 *	Icon tray and hidden window created.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------z---------------------------------------
 */


static int SysNotifyCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const *objv)
{
    const char *title;
    const char *message;
    GBytes *data;
    Tk_PhotoHandle tk_image;
    int width, height;
    Tk_PhotoImageBlock * imgData;
    NotifyNotification *notif;
    GdkPixbuf * notifyimage;

    if (objc < 4) {
	Tcl_WrongNumArgs(interp, 1, objv, "title  message image");
	return TCL_ERROR;
    }

    title = Tcl_GetString(objv[1]);
    message = Tcl_GetString(objv[2]);
 
    
    tk_image = Tk_FindPhoto(interp, Tcl_GetString(objv[3]));
    Tk_PhotoGetSize(tk_image, &width, &height);
    Tk_PhotoGetImage(tk_image, &imgData);
    data = g_bytes_new_take(&imgData, sizeof(imgData));
   // data = GBytes.get_data(&imgData);

    notifyimage = gdk_pixbuf_new_from_bytes(data, GDK_COLORSPACE_RGB, True, 8, width, height, 0);
    notif = notify_notification_new(title, message, NULL);
    notify_notification_set_image_from_pixbuf(notif, notifyimage);
    notify_notification_show(notif, NULL);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * SysNotify_Init --
 *
 *      Initialize the command.
 *
 * Results:
 *	Command initialized.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------z---------------------------------------
 */

int
SysNotify_Init(
    Tcl_Interp *interp)
{
    notify_init("Wish");

    Tcl_CreateObjCommand(interp, "_sysnotify", SysNotifyCmd, interp,
	    SysNotifyDeleteCmd);
    return TCL_OK;
}

#endif /* HAVE_LIBNOTIFY */
/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */

