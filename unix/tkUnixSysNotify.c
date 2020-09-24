/* 
 * 	tkUnixSysNotify.c implements a "sysnotify" Tcl command which 
 permits one to post system notifications based on the libnotify API.
 * 
 * Copyright (c) 2020 Kevin Walzer/WordTech Communications LLC. 
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
                                        
 */

#include <tcl.h>
#include <tk.h>
#include "tkUnixInt.h"

#ifdef HAVE_LIBNOTIFY

#include <libnotify/notify.h>

/*
 * Forward declarations for procedures defined in this file.
 */

static void SysNotifyDeleteCmd ( ClientData cd );
static int SysNotifyCmd (ClientData clientData, Tcl_Interp * interp,
			 int argc, const char * argv[]);
int SysNotify_Init ( Tcl_Interp* interp );

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


static void SysNotifyDeleteCmd ( ClientData cd )
{
  (void) cd;
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


static int SysNotifyCmd (ClientData clientData, Tcl_Interp * interp,
			 int argc, const char * argv[])
{

    (void) clientData;

    char *title;
    char *message;
    NotifyNotification *notif;

    if (argc < 3) {
	Tcl_AppendResult(interp, "wrong # args,must be:",
			 argv[0], " title  message ", (char * ) NULL);
	return TCL_ERROR;
    }
 
    title = (char *) argv[1];
    message = (char *) argv[2];

    notif = notify_notification_new(title, message, NULL);
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

int SysNotify_Init ( Tcl_Interp* interp )
{

  notify_init("Wish");
  
  Tcl_CreateCommand(interp, "_sysnotify", SysNotifyCmd, (ClientData)interp,
		    (Tcl_CmdDeleteProc *) SysNotifyDeleteCmd);
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

