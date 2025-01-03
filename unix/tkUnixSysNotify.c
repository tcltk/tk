/*
 * tkUnixSysNotify.c --
 *
 *	tkUnixSysNotify.c implements a "sysnotify" Tcl command which
 *	permits one to post system notifications based on the libnotify API.
 *
 * Copyright © 2020 Kevin Walzer/WordTech Communications LLC.
 * Copyright © 2020 Christian Werner for runtime linking
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkUnixInt.h"

/*
 * Runtime linking of libnotify.
 */

typedef int	(*fn_ln_init)(const char *);
typedef void	(*fn_ln_uninit)(void);
typedef void *	(*fn_ln_notification_new)(const char *, const char *,
			const char *, void *);
typedef int	(*fn_ln_notification_show)(void *, int *);

static struct {
    int				nopen;
    Tcl_LoadHandle		lib;
    fn_ln_init			init;
    fn_ln_uninit		uninit;
    fn_ln_notification_new	notification_new;
    fn_ln_notification_show	notification_show;
} ln_fns = {
    0, NULL, NULL, NULL, NULL, NULL
};

#define notify_init			ln_fns.init
#define notify_uninit			ln_fns.uninit
#define notify_notification_new		ln_fns.notification_new
#define notify_notification_show	ln_fns.notification_show

TCL_DECLARE_MUTEX(ln_mutex);

/*
 * Forward declarations for procedures defined in this file.
 */

static Tcl_CmdDeleteProc SysNotifyDeleteCmd;
static Tcl_ObjCmdProc SysNotifyCmd;

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
 *----------------------------------------------------------------------
 */

static void
SysNotifyDeleteCmd (
    TCL_UNUSED(void *))
{
    Tcl_MutexLock(&ln_mutex);
    if (--ln_fns.nopen == 0) {
	if (notify_uninit) {
	    notify_uninit();
	}
	if (ln_fns.lib != NULL) {
	    Tcl_FSUnloadFile(NULL, ln_fns.lib);
	}
	memset(&ln_fns, 0, sizeof(ln_fns));
    }
    Tcl_MutexUnlock(&ln_mutex);
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
 *----------------------------------------------------------------------
 */

static int
SysNotifyCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const *objv)
{
    const char *title;
    const char *message;
    const char *icon;
    const char *appname = "Wish";
    void *notif;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "title message");
	return TCL_ERROR;
    }

    /*
     * Pass strings to notification, and use a sane platform-specific
     * icon in the alert.
     */

    title = Tcl_GetString(objv[1]);
    message = Tcl_GetString(objv[2]);
    icon = "dialog-information";


    /* Use the appname for libnotify initialization
     * See bug f63c37be3a for a discussion whether this should be
     * allowed at all on safe interpreters
     */
    if (!Tcl_IsSafe(interp)) {
	appname = ((TkWindow *)Tk_MainWindow(interp))->nameUid;
    }

    /*
     * Call to notify_init should go here to prevent test suite failure.
     */

    if (notify_init && notify_notification_new && notify_notification_show) {
	Tcl_Encoding enc;
	Tcl_DString dst, dsm;

	Tcl_DStringInit(&dst);
	Tcl_DStringInit(&dsm);
	enc = Tcl_GetEncoding(NULL, "utf-8");
	char *dstStr = Tcl_UtfToExternalDString(enc, title, TCL_INDEX_NONE, &dst);
	char *dsmStr = Tcl_UtfToExternalDString(enc, message, TCL_INDEX_NONE, &dsm);
	notify_init(appname);
	notif = notify_notification_new(dstStr, dsmStr, icon, NULL);
	notify_notification_show(notif, NULL);
	Tcl_DStringFree(&dsm);
	Tcl_DStringFree(&dst);
	Tcl_FreeEncoding(enc);
    }

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
 *----------------------------------------------------------------------
 */

int
SysNotify_Init(
    Tcl_Interp *interp)
{
    Tcl_MutexLock(&ln_mutex);
    if (ln_fns.nopen == 0) {
	int i = 0;
	Tcl_Obj *nameobj;
	static const char *lnlibs[] = {
	    "libnotify.so.4",
	    "libnotify.so.3",
	    "libnotify.so.2",
	    "libnotify.so.1",
	    "libnotify.so",
	    NULL
	};

	while (lnlibs[i] != NULL) {
	    Tcl_ResetResult(interp);
	    nameobj = Tcl_NewStringObj(lnlibs[i], TCL_INDEX_NONE);
	    Tcl_IncrRefCount(nameobj);
	    if (Tcl_LoadFile(interp, nameobj, NULL, 0, NULL, &ln_fns.lib)
		    == TCL_OK) {
		Tcl_DecrRefCount(nameobj);
		break;
	    }
	    Tcl_DecrRefCount(nameobj);
	    ++i;
	}
	if (ln_fns.lib != NULL) {
#define LN_SYM(name)							\
	    ln_fns.name = (fn_ln_ ## name)				\
		Tcl_FindSymbol(NULL, ln_fns.lib, "notify_" #name)
	    LN_SYM(init);
	    LN_SYM(uninit);
	    LN_SYM(notification_new);
	    LN_SYM(notification_show);
#undef LN_SYM
	}
    }
    ln_fns.nopen++;
    Tcl_MutexUnlock(&ln_mutex);
    if (!notify_init) return TCL_OK;

    Tcl_CreateObjCommand(interp, "::tk::sysnotify::_sysnotify", SysNotifyCmd,
	    interp, SysNotifyDeleteCmd);
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
