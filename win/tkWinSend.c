/* 
 * tkWinSend.c --
 *
 *	This file provides procedures that implement the "send"
 *	command, allowing commands to be passed from interpreter
 *	to interpreter.
 *
 * Copyright (c) 1997 by Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * SCCS: @(#) tkWinSend.c 1.15 98/02/19 17:04:54
 */

#include "tkWinInt.h"
#include <ddeml.h>

/* 
 * The following structure is used to keep track of the interpreters
 * registered by this process.
 */

typedef struct RegisteredInterp {
    struct RegisteredInterp *nextPtr;
				/* The next interp this application knows
				 * about. */
    char *name;			/* Interpreter's name (malloc-ed). */
    Tcl_Interp *interp;		/* The interpreter attached to this name. */
} RegisteredInterp;

/*
 * Used to keep track of conversations.
 */

typedef struct Conversation {
    struct Conversation *nextPtr;
				/* The next conversation in the list. */
    RegisteredInterp *riPtr;	/* The info we know about the conversation. */
    HCONV hConv;		/* The DDE handle for this conversation. */
    Tcl_Obj *returnPackagePtr;	/* The result package for this conversation. */
} Conversation;

/*
 * Static variables used by the registration process. Most of these
 * are allocated in RegOpen and freed in RegClose.
 */

static Conversation *currentConversations;
				/* A list of conversations currently
				 * being processed. */
static DWORD ddeInstance = 0;	/* The application instance handle given
				 * to us by DdeInitialize. */
static RegisteredInterp *interpListPtr;
				/* The list of interps that this particular
				 * application knows about. */

/*
 * Forward declarations for procedures defined later in this file.
 */

static void		    RemoveDdeServerExitProc _ANSI_ARGS_((ClientData clientData));
static void		    DeleteProc _ANSI_ARGS_((ClientData clientData));
static Tcl_Obj *	    ExecuteRemoteObject _ANSI_ARGS_((
				RegisteredInterp *riPtr, 
				Tcl_Obj *ddeObjectPtr));
static int		    MakeDdeConnection _ANSI_ARGS_((Tcl_Interp *interp,
				char *name, HCONV *ddeConvPtr));
static HDDEDATA CALLBACK    TkDdeServerProc _ANSI_ARGS_((UINT uType,
				UINT uFmt, HCONV hConv, HSZ ddeTopic,
				HSZ ddeItem, HDDEDATA hData, DWORD dwData1, 
				DWORD dwData2));
static void		    SetDdeError _ANSI_ARGS_((Tcl_Interp *interp));


/*
 *--------------------------------------------------------------
 *
 * Tk_SetAppName --
 *
 *	This procedure is called to associate an ASCII name with a Tk
 *	application.  If the application has already been named, the
 *	name replaces the old one.
 *
 * Results:
 *	The return value is the name actually given to the application.
 *	This will normally be the same as name, but if name was already
 *	in use for an application then a name of the form "name #2" will
 *	be chosen,  with a high enough number to make the name unique.
 *
 * Side effects:
 *	Registration info is saved, thereby allowing the "send" command
 *	to be used later to invoke commands in the application.  In
 *	addition, the "send" command is created in the application's
 *	interpreter.  The registration will be removed automatically
 *	if the interpreter is deleted or the "send" command is removed.
 *
 *--------------------------------------------------------------
 */

char *
Tk_SetAppName(tkwin, name)
    Tk_Window tkwin;		/* Token for any window in the application
				 * to be named:  it is just used to identify
				 * the application and the display.  */
    char *name;			/* The name that will be used to
				 * refer to the interpreter in later
				 * "send" commands.  Must be globally
				 * unique. */
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    Tcl_Interp *interp = winPtr->mainPtr->interp;
    int i, suffix, offset;
    RegisteredInterp *riPtr, *prevPtr;
    char *actualName;
    Tcl_DString dString;
    Tcl_Obj *resultObjPtr, *interpNamePtr;
    char *interpName;

    /*
     * Make sure that the DDE server is there. This is done only once,
     * add an exit handler tear it down.
     */

    if (ddeInstance == 0) {
	HSZ ddeService;

	if (DdeInitialize(&ddeInstance, TkDdeServerProc,
		CBF_SKIP_REGISTRATIONS|CBF_SKIP_UNREGISTRATIONS
		|CBF_FAIL_POKES, 0) 
		!= DMLERR_NO_ERROR) {
	    DdeUninitialize(ddeInstance);
	    return NULL;
	}
	Tcl_CreateExitHandler(RemoveDdeServerExitProc, NULL);
	ddeService = DdeCreateStringHandle(ddeInstance, "Tk", 0);
	DdeNameService(ddeInstance, ddeService, 0L, DNS_REGISTER);
    }

    /*
     * See if the application is already registered; if so, remove its
     * current name from the registry. The deletion of the command
     * will take care of disposing of this entry.
     */

    for (riPtr = interpListPtr, prevPtr = NULL; riPtr != NULL; 
	    prevPtr = riPtr, riPtr = riPtr->nextPtr) {
	if (riPtr->interp == interp) {
	    if (prevPtr == NULL) {
		interpListPtr = interpListPtr->nextPtr;
	    } else {
		prevPtr->nextPtr = riPtr->nextPtr;
	    }
	    break;
	}
    }

    /*
     * Pick a name to use for the application.  Use "name" if it's not
     * already in use.  Otherwise add a suffix such as " #2", trying
     * larger and larger numbers until we eventually find one that is
     * unique.
     */

    actualName = name;
    suffix = 1;
    offset = 0;
    Tcl_DStringInit(&dString);

    TkGetInterpNames(interp, tkwin);
    resultObjPtr = Tcl_GetObjResult(interp);
    Tcl_IncrRefCount(resultObjPtr);
    for (i = 0; ; ) {
	(void) Tcl_ListObjIndex(NULL, resultObjPtr, i, &interpNamePtr);
	if (interpNamePtr == NULL) {
	    break;
	}
	interpName = Tcl_GetString(interpNamePtr);
	if (stricmp(actualName, interpName) == 0) {
	    if (suffix == 1) {
		Tcl_DStringAppend(&dString, name, -1);
		Tcl_DStringAppend(&dString, " #", 2);
		offset = Tcl_DStringLength(&dString);
		Tcl_DStringSetLength(&dString, offset + 10);
		actualName = Tcl_DStringValue(&dString);
	    }
	    suffix++;
	    sprintf(actualName + offset, "%d", suffix);
	    i = 0;
	} else {
	    i++;
	}
    }

    Tcl_DecrRefCount(resultObjPtr);
    Tcl_ResetResult(interp);

    /*
     * We have found a unique name. Now add it to the registry.
     */

    riPtr = (RegisteredInterp *) ckalloc(sizeof(RegisteredInterp));
    riPtr->interp = interp;
    riPtr->name = ckalloc(strlen(actualName) + 1);
    riPtr->nextPtr = interpListPtr;
    interpListPtr = riPtr;
    strcpy(riPtr->name, actualName);

    Tcl_CreateObjCommand(interp, "send", Tk_SendObjCmd, 
	    (ClientData) riPtr, DeleteProc);
    Tcl_CreateObjCommand(interp, "dde", Tk_DdeObjCmd,
	    (ClientData) NULL, NULL);
    if (Tcl_IsSafe(interp)) {
	Tcl_HideCommand(interp, "send", "send");
	Tcl_HideCommand(interp, "dde", "dde");
    }
    Tcl_DStringFree(&dString);

    return riPtr->name;
}

/*
 *--------------------------------------------------------------
 *
 * Tk_SendObjCmd --
 *
 *	This procedure is invoked to process the "send" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

int
Tk_SendObjCmd(
    ClientData clientData,	/* Used only for deletion */
    Tcl_Interp *interp,		/* The interp we are sending from */
    int objc,			/* Number of arguments */
    Tcl_Obj *CONST objv[])	/* The arguments */
{
    char *string, *sendName;
    int async, i, result, length;
    RegisteredInterp *riPtr;
    Tcl_Interp *sendInterp;
    Tcl_Obj *objPtr;
    static char *options[] = {
	"-async",	"-displayof",	    "--",	(char *) NULL
    };
    enum options {
	SEND_ASYNC,	SEND_DISPLAYOF,	    SEND_LAST
    };

    async = 0;
    for (i = 1; i < objc; i++) {
	int index;

	string = Tcl_GetString(objv[i]);
	if (string[0] != '-') {
	    break;
	}
	if (Tcl_GetIndexFromObj(interp, objv[i], options, "option", TCL_EXACT, 
		&index) != TCL_OK) {
	    return TCL_ERROR;
	}
	switch ((enum options) index) {
	    case SEND_ASYNC: {
		async = 1;
		break;
	    }
	    case SEND_DISPLAYOF: {
		/*
		 * Don't care about -displayof option.  Skip the
		 * (ignored) window argument.
		 */
		 
		i++;
		break;
	    }
	    case SEND_LAST: {
		i++;
		/* break 2; */
		goto endOfOptionLoop;
	    }
	}
    }

    endOfOptionLoop:
    if (objc - i < 2) {
	Tcl_WrongNumArgs(interp, 1, objv,
		"?options? interpName arg ?arg ...?");
	return TCL_ERROR;
    }

    sendName = Tcl_GetString(objv[i]);
    objc -= i + 1;
    ((Tcl_Obj **)objv) += i + 1;

    /*
     * See if the target interpreter is local.  If so, execute
     * the command directly without going through the DDE server.
     * Don't exchange objects between interps.  The target interp could
     * compile an object, producing a bytecode structure that refers to 
     * other objects owned by the target interp.  If the target interp 
     * is then deleted, the bytecode structure would be referring to 
     * deallocated objects.
     */

    for (riPtr = interpListPtr; riPtr != NULL; riPtr = riPtr->nextPtr) {
	if (stricmp(sendName, riPtr->name) == 0) {
	    break;
	}
    }

    if (riPtr != NULL) {
	/*
	 * This command is to a local interp. No need to go through
	 * the server.
	 */

	Tcl_Preserve((ClientData) riPtr);
	sendInterp = riPtr->interp;
	Tcl_Preserve((ClientData) sendInterp);

	/*
	 * Don't exchange objects between interps.  The target interp would
	 * compile an object, producing a bytecode structure that refers to 
	 * other objects owned by the target interp.  If the target interp 
	 * is then deleted, the bytecode structure would be referring to 
	 * deallocated objects.
	 */

	if (objc == 1) {
	    result = Tcl_EvalObj(sendInterp, objv[0], TCL_EVAL_GLOBAL);
	} else {
	    objPtr = Tcl_ConcatObj(objc, objv);
	    Tcl_IncrRefCount(objPtr);
	    result = Tcl_EvalObj(sendInterp, objPtr, TCL_EVAL_GLOBAL);
	    Tcl_DecrRefCount(objPtr);
	}
	if (interp != sendInterp) {
	    if (result == TCL_ERROR) {
		/*
		 * An error occurred, so transfer error information from the
		 * destination interpreter back to our interpreter.  
		 */

		Tcl_ResetResult(interp);
		objPtr = Tcl_GetObjVar2(sendInterp, "errorInfo", NULL, 
			TCL_GLOBAL_ONLY);
		string = Tcl_GetStringFromObj(objPtr, &length);
		Tcl_AddObjErrorInfo(interp, string, length);

		objPtr = Tcl_GetObjVar2(sendInterp, "errorCode", NULL,
			TCL_GLOBAL_ONLY);
		Tcl_SetObjErrorCode(interp, objPtr);
	    }
	    Tcl_SetObjResult(interp, Tcl_GetObjResult(sendInterp));
	}
	Tcl_Release((ClientData) riPtr);
	Tcl_Release((ClientData) sendInterp);
    } else {
	/*
	 * This is a non-local request. Send the script to the server and poll
	 * it for a result.
	 */

	HCONV hConv;
	HDDEDATA ddeItem;
	HDDEDATA ddeData;
	DWORD ddeResult;

	if (MakeDdeConnection(interp, sendName, &hConv) != TCL_OK) {
	    return TCL_ERROR;
	}

	objPtr = Tcl_ConcatObj(objc, objv);
	string = Tcl_GetStringFromObj(objPtr, &length);
	ddeItem = DdeCreateDataHandle(ddeInstance, string, length, 0, 0,
		CF_TEXT, 0);
	if (async) {
	    ddeData = DdeClientTransaction((LPBYTE) ddeItem, 0xFFFFFFFF, hConv, 0,
		    CF_TEXT, XTYP_EXECUTE, TIMEOUT_ASYNC, &ddeResult);
	    DdeAbandonTransaction(ddeInstance, hConv, ddeResult);
	} else {
	    ddeData = DdeClientTransaction((LPBYTE) ddeItem, 0xFFFFFFFF, hConv, 0,
		    CF_TEXT, XTYP_EXECUTE, 7200000, NULL);
	    if (ddeData != 0) {
		HSZ ddeCookie;

		ddeCookie = DdeCreateStringHandle(ddeInstance, 
			"$TK$EXECUTE$RESULT", CP_WINANSI);
		ddeData = DdeClientTransaction(NULL, 0, hConv, ddeCookie,
			CF_TEXT, XTYP_REQUEST, 7200000, NULL);
		DdeFreeStringHandle(ddeInstance, ddeCookie);
	    }
	}

	DdeFreeDataHandle(ddeItem);
	Tcl_DecrRefCount(objPtr);

	if (ddeData == 0) {
	    SetDdeError(interp);
	    DdeDisconnect(hConv);
	    return TCL_ERROR;
	}

	if (async == 0) {
	    Tcl_Obj *resultPtr;

	    /*
	     * The return handle has a two or four element list in it. The first
	     * element is the return code (TCL_OK, TCL_ERROR, etc.). The
	     * second is the result of the script. If the return code is TCL_ERROR,
	     * then the third element is the value of the variable "errorCode",
	     * and the fourth is the value of the variable "errorInfo".
	     */

	    length = DdeGetData(ddeData, NULL, 0, 0);
	    resultPtr = Tcl_NewObj();
	    Tcl_SetObjLength(resultPtr, length);
	    string = Tcl_GetString(resultPtr);
	    DdeGetData(ddeData, string, length, 0);
	    DdeFreeDataHandle(ddeData);
	    DdeDisconnect(hConv);

	    if (Tcl_ListObjIndex(NULL, resultPtr, 0, &objPtr) != TCL_OK) {
		goto error;
	    }
	    if (Tcl_GetIntFromObj(NULL, objPtr, &result) != TCL_OK) {
		goto error;
	    }
	    if (result == TCL_ERROR) {
		Tcl_ResetResult(interp);

		if (Tcl_ListObjIndex(NULL, resultPtr, 3, &objPtr) != TCL_OK) {
		    goto error;
		}
		string = Tcl_GetStringFromObj(objPtr, &length);
		Tcl_AddObjErrorInfo(interp, string, length);

		Tcl_ListObjIndex(NULL, resultPtr, 2, &objPtr);
		Tcl_SetObjErrorCode(interp, objPtr);
	    }
	    if (Tcl_ListObjIndex(NULL, resultPtr, 1, &objPtr) != TCL_OK) {
		goto error;
	    }
	    Tcl_SetObjResult(interp, objPtr);
	    Tcl_DecrRefCount(resultPtr);
	    return result;

	    error:
	    Tcl_SetStringObj(Tcl_GetObjResult(interp),
		"invalid data returned from server", -1);
	    Tcl_DecrRefCount(resultPtr);
	    return TCL_ERROR;
	}
    }

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetInterpNames --
 *
 *	This procedure is invoked to fetch a list of all the
 *	interpreter names currently registered for the display
 *	of a particular window.
 *
 * Results:
 *	A standard Tcl return value.  The interp's result will be set
 *	to hold a list of all the interpreter names defined for
 *	tkwin's display.  If an error occurs, then TCL_ERROR
 *	is returned and the interp's result will hold an error message.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkGetInterpNames(interp, tkwin)
    Tcl_Interp *interp;		/* Interpreter for returning a result. */
    Tk_Window tkwin;		/* Window whose display is to be used
				 * for the lookup. */
{
    Tcl_Obj *listObjPtr;
    HCONVLIST hConvList;
    HCONV hConv;
    HSZ ddeService;
    CONVINFO convInfo;
    Tcl_DString dString;
    char *topicName;
    int len;

    convInfo.cb = sizeof(CONVINFO);
    ddeService = DdeCreateStringHandle(ddeInstance, "Tk", CP_WINANSI);
    hConvList = DdeConnectList(ddeInstance, ddeService, NULL,
	    0, NULL);
    hConv = 0;

    Tcl_DStringInit(&dString);
    listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
    while (hConv = DdeQueryNextServer(hConvList, hConv), hConv != 0) {
	DdeQueryConvInfo(hConv, QID_SYNC, &convInfo);
	len = DdeQueryString(ddeInstance, convInfo.hszTopic,
		NULL, 0, CP_WINANSI);
	Tcl_DStringSetLength(&dString, len);
	topicName = Tcl_DStringValue(&dString);
	DdeQueryString(ddeInstance, convInfo.hszTopic, topicName,
		len + 1, CP_WINANSI);
	Tcl_ListObjAppendElement(interp, listObjPtr, 
		Tcl_NewStringObj(topicName, len));
    }
    
    DdeDisconnectList(hConvList);
    Tcl_SetObjResult(interp, listObjPtr);
    Tcl_DStringFree(&dString);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * DeleteProc --
 *
 *	This procedure is invoked by Tcl when the "send" command
 *	is deleted in an interpreter.  It unregisters the interpreter.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The interpreter given by riPtr is unregistered.
 *
 *--------------------------------------------------------------
 */

static void
DeleteProc(clientData)
    ClientData clientData;	/* The interp we are deleting passed
				 * as ClientData. */
{
    RegisteredInterp *riPtr = (RegisteredInterp *) clientData;
    RegisteredInterp *searchPtr, *prevPtr;

    for (searchPtr = interpListPtr, prevPtr = NULL;
	    (searchPtr != NULL) && (searchPtr != riPtr);
	    prevPtr = searchPtr, searchPtr = searchPtr->nextPtr) {
	/*
	 * Empty loop body.
	 */
    }

    Tcl_DeleteCommand(riPtr->interp, "dde");

    if (searchPtr != NULL) {
	if (prevPtr == NULL) {
	    interpListPtr = interpListPtr->nextPtr;
	} else {
	    prevPtr->nextPtr = searchPtr->nextPtr;
	}
    }
    ckfree(riPtr->name);
    Tcl_EventuallyFree(clientData, TCL_DYNAMIC);
}

/*
 *--------------------------------------------------------------
 *
 * ExecuteRemoteObject --
 *
 *	Takes the package delivered by DDE and executes it in
 *	the server's interpreter.
 *
 * Results:
 *	A list Tcl_Obj * that describes what happened. The first
 *	element is the numerical return code (TCL_ERROR, etc.).
 *	The second element is the result of the script. If the
 *	return result was TCL_ERROR, then the third element
 *	will be the value of the global "errorCode", and the
 *	fourth will be the value of the global "errorInfo".
 *	The return result will have a refCount of 0.
 *
 * Side effects:
 *	A Tcl script is run, which can cause all kinds of other
 *	things to happen.
 *
 *--------------------------------------------------------------
 */

static Tcl_Obj *
ExecuteRemoteObject(
    RegisteredInterp *riPtr,	    /* Info about this server. */
    Tcl_Obj *ddeObjectPtr)	    /* The object to execute. */
{
    Tcl_Obj *errorObjPtr;
    Tcl_Obj *returnPackagePtr;
    int result;

    result = Tcl_EvalObj(riPtr->interp, ddeObjectPtr, TCL_EVAL_GLOBAL);
    returnPackagePtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
    Tcl_ListObjAppendElement(NULL, returnPackagePtr,
	    Tcl_NewIntObj(result));
    Tcl_ListObjAppendElement(NULL, returnPackagePtr,
	    Tcl_GetObjResult(riPtr->interp));
    if (result == TCL_ERROR) {
	errorObjPtr = Tcl_GetObjVar2(riPtr->interp, "errorCode", NULL,
		TCL_GLOBAL_ONLY);
	Tcl_ListObjAppendElement(NULL, returnPackagePtr, errorObjPtr);
	errorObjPtr = Tcl_GetObjVar2(riPtr->interp, "errorInfo", NULL,
		TCL_GLOBAL_ONLY);
        Tcl_ListObjAppendElement(NULL, returnPackagePtr, errorObjPtr);
    }

    return returnPackagePtr;
}

/*
 *--------------------------------------------------------------
 *
 * TkDdeServerProc --
 *
 *	Handles all transactions for this server. Can handle
 *	execute, request, and connect protocols. Dde will
 *	call this routine when a client attempts to run a dde
 *	command using this server.
 *
 * Results:
 *	A DDE Handle with the result of the dde command.
 *
 * Side effects:
 *	Depending on which command is executed, arbitrary
 *	Tcl scripts can be run.
 *
 *--------------------------------------------------------------
 */

static HDDEDATA CALLBACK
TkDdeServerProc (
    UINT uType,			/* The type of DDE transaction we
				 * are performing. */
    UINT uFmt,			/* The format that data is sent or
				 * received. */
    HCONV hConv,		/* The conversation associated with the 
				 * current transaction. */
    HSZ ddeTopic,		/* A string handle. Transaction-type 
				 * dependent. */
    HSZ ddeItem,		/* A string handle. Transaction-type 
				 * dependent. */
    HDDEDATA hData,		/* DDE data. Transaction-type dependent. */
    DWORD dwData1,		/* Transaction-dependent data. */
    DWORD dwData2)		/* Transaction-dependent data. */
{
    Tcl_DString dString;
    int len;
    char *utilString;
    Tcl_Obj *ddeObjectPtr;
    HDDEDATA ddeReturn = NULL;
    RegisteredInterp *riPtr;
    Conversation *convPtr, *prevConvPtr;

    switch(uType) {
	case XTYP_CONNECT:

	    /*
	     * Dde is trying to initialize a conversation with us. Check
	     * and make sure we have a valid topic.
	     */

	    len = DdeQueryString(ddeInstance, ddeTopic, NULL, 0, 0);
	    Tcl_DStringInit(&dString);
	    Tcl_DStringSetLength(&dString, len);
	    utilString = Tcl_DStringValue(&dString);
	    DdeQueryString(ddeInstance, ddeTopic, utilString, len + 1,
		    CP_WINANSI);

	    for (riPtr = interpListPtr; riPtr != NULL;
		    riPtr = riPtr->nextPtr) {
		if (stricmp(utilString, riPtr->name) == 0) {
		    Tcl_DStringFree(&dString);
		    return (HDDEDATA) TRUE;
		}
	    }

	    Tcl_DStringFree(&dString);
	    return (HDDEDATA) FALSE;

	case XTYP_CONNECT_CONFIRM:

	    /*
	     * Dde has decided that we can connect, so it gives us a 
	     * conversation handle. We need to keep track of it
	     * so we know which execution result to return in an
	     * XTYP_REQUEST.
	     */

	    len = DdeQueryString(ddeInstance, ddeTopic, NULL, 0, 0);
	    Tcl_DStringInit(&dString);
	    Tcl_DStringSetLength(&dString, len);
	    utilString = Tcl_DStringValue(&dString);
	    DdeQueryString(ddeInstance, ddeTopic, utilString, len + 1, 
		    CP_WINANSI);
	    for (riPtr = interpListPtr; riPtr != NULL; 
		    riPtr = riPtr->nextPtr) {
		if (stricmp(riPtr->name, utilString) == 0) {
		    convPtr = (Conversation *) ckalloc(sizeof(Conversation));
		    convPtr->nextPtr = currentConversations;
		    convPtr->returnPackagePtr = NULL;
		    convPtr->hConv = hConv;
		    convPtr->riPtr = riPtr;
		    currentConversations = convPtr;
		    break;
		}
	    }
	    Tcl_DStringFree(&dString);
	    return (HDDEDATA) TRUE;

	case XTYP_DISCONNECT:

	    /*
	     * The client has disconnected from our server. Forget this
	     * conversation.
	     */

	    for (convPtr = currentConversations, prevConvPtr = NULL;
		    convPtr != NULL; 
		    prevConvPtr = convPtr, convPtr = convPtr->nextPtr) {
		if (hConv == convPtr->hConv) {
		    if (prevConvPtr == NULL) {
			currentConversations = convPtr->nextPtr;
		    } else {
			prevConvPtr->nextPtr = convPtr->nextPtr;
		    }
		    if (convPtr->returnPackagePtr != NULL) {
			Tcl_DecrRefCount(convPtr->returnPackagePtr);
		    }
		    ckfree((char *) convPtr);
		    break;
		}
	    }
	    return (HDDEDATA) TRUE;

	case XTYP_REQUEST:

	    /*
	     * This could be either a request for a value of a Tcl variable,
	     * or it could be the send command requesting the results of the
	     * last execute.
	     */

	    if (uFmt != CF_TEXT) {
		return (HDDEDATA) FALSE;
	    }

	    ddeReturn = (HDDEDATA) FALSE;
	    for (convPtr = currentConversations; (convPtr != NULL)
		    && (convPtr->hConv != hConv); convPtr = convPtr->nextPtr) {
		/*
		 * Empty loop body.
		 */
	    }

	    if (convPtr != NULL) {
		char *returnString;

		len = DdeQueryString(ddeInstance, ddeItem, NULL, 0,
			CP_WINANSI);
		Tcl_DStringInit(&dString);
		Tcl_DStringSetLength(&dString, len);
		utilString = Tcl_DStringValue(&dString);
		DdeQueryString(ddeInstance, ddeItem, utilString, len + 1,
			CP_WINANSI);
		if (stricmp(utilString, "$TK$EXECUTE$RESULT") == 0) {
		    returnString =
		        Tcl_GetStringFromObj(convPtr->returnPackagePtr, &len);
		    ddeReturn = DdeCreateDataHandle(ddeInstance,
			    returnString, len, 0, ddeItem, CF_TEXT,
			    0);
		} else {
		    Tcl_Obj *variableObjPtr = Tcl_GetObjVar2(
			    convPtr->riPtr->interp, utilString, NULL, 
			    TCL_GLOBAL_ONLY);
		    if (variableObjPtr != NULL) {
			returnString = Tcl_GetStringFromObj(variableObjPtr,
				&len);
			ddeReturn = DdeCreateDataHandle(ddeInstance,
				returnString, len, 0, ddeItem, CF_TEXT, 0);
		    } else {
			ddeReturn = NULL;
		    }
		}
		Tcl_DStringFree(&dString);
	    }
	    return ddeReturn;

	case XTYP_EXECUTE: {

	    /*
	     * Execute this script. The results will be saved into
	     * a list object which will be retreived later. See
	     * ExecuteRemoteObject.
	     */

	    Tcl_Obj *returnPackagePtr;

	    for (convPtr = currentConversations; (convPtr != NULL)
		    && (convPtr->hConv != hConv); convPtr = convPtr->nextPtr) {
		/*
		 * Empty loop body.
		 */

	    }

	    if (convPtr == NULL) {
		return (HDDEDATA) DDE_FNOTPROCESSED;
	    }

	    utilString = (char *) DdeAccessData(hData, &len);
	    ddeObjectPtr = Tcl_NewStringObj(utilString, len);
	    Tcl_IncrRefCount(ddeObjectPtr);
	    DdeUnaccessData(hData);
	    if (convPtr->returnPackagePtr != NULL) {
		Tcl_DecrRefCount(convPtr->returnPackagePtr);
	    }
	    convPtr->returnPackagePtr = NULL;
	    returnPackagePtr = 
		    ExecuteRemoteObject(convPtr->riPtr, ddeObjectPtr);
	    for (convPtr = currentConversations; (convPtr != NULL)
 		    && (convPtr->hConv != hConv); convPtr = convPtr->nextPtr) {
		/*
		 * Empty loop body.
		 */

	    }
	    if (convPtr != NULL) {
		Tcl_IncrRefCount(returnPackagePtr);
		convPtr->returnPackagePtr = returnPackagePtr;
	    }
	    Tcl_DecrRefCount(ddeObjectPtr);
	    if (returnPackagePtr == NULL) {
		return (HDDEDATA) DDE_FNOTPROCESSED;
	    } else {
		return (HDDEDATA) DDE_FACK;
	    }
	}
	    
	case XTYP_WILDCONNECT: {

	    /*
	     * Dde wants a list of services and topics that we support.
	     */

	    HSZPAIR *returnPtr;
	    int i;
	    int numItems;

	    for (i = 0, riPtr = interpListPtr; riPtr != NULL;
		    i++, riPtr = riPtr->nextPtr) {
		/*
		 * Empty loop body.
		 */

	    }

	    numItems = i;
	    ddeReturn = DdeCreateDataHandle(ddeInstance, NULL,
		    (numItems + 1) * sizeof(HSZPAIR), 0, 0, 0, 0);
	    returnPtr = (HSZPAIR *) DdeAccessData(ddeReturn, &len);
	    for (i = 0, riPtr = interpListPtr; i < numItems; 
		    i++, riPtr = riPtr->nextPtr) {
		returnPtr[i].hszSvc = DdeCreateStringHandle(ddeInstance,
			"Tk", CP_WINANSI);
		returnPtr[i].hszTopic = DdeCreateStringHandle(ddeInstance,
			riPtr->name, CP_WINANSI);
	    }
	    returnPtr[i].hszSvc = NULL;
	    returnPtr[i].hszTopic = NULL;
	    DdeUnaccessData(ddeReturn);
	    return ddeReturn;
	}

    }
    return NULL;
}


/*
 *--------------------------------------------------------------
 *
 * RemoveDdeServerExitProc --
 *
 *	Gets rid of our DDE server when we go away.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The DDE server is deleted.
 *
 *--------------------------------------------------------------
 */

static void
RemoveDdeServerExitProc(
    ClientData clientData)	    /* Not used in this handler. */
{
    DdeNameService(ddeInstance, NULL, 0, DNS_UNREGISTER);
    DdeUninitialize(ddeInstance);
    ddeInstance = 0;
}

/*
 *--------------------------------------------------------------
 *
 * MakeDdeConnection --
 *
 *	This procedure is a utility used to connect to a DDE
 *	server when given a server name and a topic name.
 *
 * Results:
 *	A standard Tcl result.
 *	
 *
 * Side effects:
 *	Passes back a conversation through ddeConvPtr
 *
 *--------------------------------------------------------------
 */

static int
MakeDdeConnection(
    Tcl_Interp *interp,		/* Used to report errors. */
    char *name,			/* The connection to use. */
    HCONV *ddeConvPtr)
{
    HSZ ddeTopic, ddeService;
    HCONV ddeConv;
    
    ddeService = DdeCreateStringHandle(ddeInstance, "Tk", 0);
    ddeTopic = DdeCreateStringHandle(ddeInstance, name, 0);

    ddeConv = DdeConnect(ddeInstance, ddeService, ddeTopic, NULL);
    DdeFreeStringHandle(ddeInstance, ddeService);
    DdeFreeStringHandle(ddeInstance, ddeTopic);

    if (ddeConv == (HCONV) NULL) {
	if (interp != NULL) {
	    Tcl_AppendResult(interp, "no registered server named \"",
		    name, "\"", (char *) NULL);
	}
	return TCL_ERROR;
    }

    *ddeConvPtr = ddeConv;
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * SetDdeError --
 *
 *	Sets the interp result to a cogent error message
 *	describing the last DDE error.
 *
 * Results:
 *	None.
 *	
 *
 * Side effects:
 *	The interp's result object is changed.
 *
 *--------------------------------------------------------------
 */

static void
SetDdeError(
    Tcl_Interp *interp)	    /* The interp to put the message in.*/
{
    Tcl_Obj *resultPtr = Tcl_GetObjResult(interp);
    int err;

    err = DdeGetLastError(ddeInstance);
    switch (err) {
	case DMLERR_DATAACKTIMEOUT:
	case DMLERR_EXECACKTIMEOUT:
	case DMLERR_POKEACKTIMEOUT:
	    Tcl_SetStringObj(resultPtr,
		    "remote interpreter did not respond", -1);
	    break;

	case DMLERR_BUSY:
	    Tcl_SetStringObj(resultPtr, "remote server is busy", -1);
	    break;

	case DMLERR_NOTPROCESSED:
	    Tcl_SetStringObj(resultPtr, 
		    "remote server cannot handle this command", -1);
	    break;

	default:
	    Tcl_SetStringObj(resultPtr, "dde command failed", -1);
    }
}

/*
 *--------------------------------------------------------------
 *
 * Tk_DdeObjCmd --
 *
 *	This procedure is invoked to process the "dde" Tcl command.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

int
Tk_DdeObjCmd(
    ClientData clientData,	/* Used only for deletion */
    Tcl_Interp *interp,		/* The interp we are sending from */
    int objc,			/* Number of arguments */
    Tcl_Obj *CONST objv[])	/* The arguments */
{
    enum {
	DDE_EXECUTE,
	DDE_REQUEST,
	DDE_SERVICES
    };

    static char *ddeCommands[] = {"execute", "request", "services", 
	    (char *) NULL};
    static char *ddeOptions[] = {"-async", (char *) NULL};
    int index, argIndex;
    int async = 0;
    int result = TCL_OK;
    HSZ ddeService = NULL;
    HSZ ddeTopic = NULL;
    HSZ ddeItem = NULL;
    HDDEDATA ddeData = NULL;
    HCONV hConv;
    char *serviceName, *topicName, *itemString, *dataString;
    int firstArg, length, dataLength;
    DWORD ddeResult;
    HDDEDATA ddeReturn;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, 
		"?-async? serviceName topicName value");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], ddeCommands, "command", 0,
	    &index) != TCL_OK) {
	return TCL_ERROR;
    }

    switch (index) {
	case DDE_EXECUTE:
	    if ((objc < 5) || (objc > 6)) {
		Tcl_WrongNumArgs(interp, 1, objv, 
			"execute ?-async? serviceName topicName value");
		return TCL_ERROR;
	    }
	    if (Tcl_GetIndexFromObj(NULL, objv[2], ddeOptions, "option", 0,
		    &argIndex) != TCL_OK) {
		if (objc != 5) {
		    Tcl_WrongNumArgs(interp, 1, objv,
			    "execute ?-async? serviceName topicName value");
		    return TCL_ERROR;
		}
		async = 0;
		firstArg = 2;
	    } else {
		if (objc != 6) {
		    Tcl_WrongNumArgs(interp, 1, objv,
			    "execute ?-async? serviceName topicName value");
		    return TCL_ERROR;
		}
		async = 1;
		firstArg = 3;
	    }
	    break;
	case DDE_REQUEST:
	    if (objc != 5) {
		Tcl_WrongNumArgs(interp, 1, objv, 
			"request serviceName topicName value");
		return TCL_ERROR;
	    }
	    firstArg = 2;
	    break;
	case DDE_SERVICES:
	    if (objc != 4) {
		Tcl_WrongNumArgs(interp, 1, objv,
			"services serviceName topicName");
		return TCL_ERROR;
	    }
	    firstArg = 2;
	    break;
    }

    serviceName = Tcl_GetStringFromObj(objv[firstArg], &length);
    if (length == 0) {
	serviceName = NULL;
    } else {
	ddeService = DdeCreateStringHandle(ddeInstance, serviceName,
		CP_WINANSI);
    }
    topicName = Tcl_GetStringFromObj(objv[firstArg + 1], &length);
    if (length == 0) {
	topicName = NULL;
    } else {
	ddeTopic = DdeCreateStringHandle(ddeInstance, topicName, CP_WINANSI);
    }

    switch (index) {
	case DDE_EXECUTE: {
	    dataString = Tcl_GetStringFromObj(objv[firstArg + 2], &dataLength);
	    if (dataLength == 0) {
		Tcl_SetStringObj(Tcl_GetObjResult(interp),
			"cannot execute null data", -1);
		result = TCL_ERROR;
		break;
	    }
	    hConv = DdeConnect(ddeInstance, ddeService, ddeTopic, NULL);

	    if (hConv == NULL) {
		SetDdeError(interp);
		result = TCL_ERROR;
		break;
	    }

	    ddeData = DdeCreateDataHandle(ddeInstance, dataString,
		    dataLength, 0, 0, CF_TEXT, 0);
	    if (ddeData != NULL) {
		if (async) {
		    DdeClientTransaction((LPBYTE) ddeData, 0xFFFFFFFF, hConv, 0, 
			    CF_TEXT, XTYP_EXECUTE, TIMEOUT_ASYNC, &ddeResult);
		    DdeAbandonTransaction(ddeInstance, hConv, ddeResult);
		} else {
		    ddeReturn = DdeClientTransaction((LPBYTE) ddeData, 0xFFFFFFFF,
			    hConv, 0, CF_TEXT, XTYP_EXECUTE, 7200000, NULL);
		    if (ddeReturn == 0) {
			SetDdeError(interp);
			result = TCL_ERROR;
		    }
		}
		DdeFreeDataHandle(ddeData);
	    } else {
		SetDdeError(interp);
		result = TCL_ERROR;
	    }
	    DdeDisconnect(hConv);
	    break;
	}
	case DDE_REQUEST: {
	    itemString = Tcl_GetStringFromObj(objv[firstArg + 2], &length);
	    if (length == 0) {
		Tcl_SetStringObj(Tcl_GetObjResult(interp),
			"cannot request value of null data", -1);
		return TCL_ERROR;
	    }
	    hConv = DdeConnect(ddeInstance, ddeService, ddeTopic, NULL);
	    
	    if (hConv == NULL) {
		SetDdeError(interp);
		result = TCL_ERROR;
	    } else {
		Tcl_Obj *returnObjPtr;
		ddeItem = DdeCreateStringHandle(ddeInstance, itemString,
			CP_WINANSI);
		if (ddeItem != NULL) {
		    ddeData = DdeClientTransaction(NULL, 0, hConv, ddeItem,
			    CF_TEXT, XTYP_REQUEST, 5000, NULL);
		    if (ddeData == NULL) {
			SetDdeError(interp);
			result = TCL_ERROR;
		    } else {
			dataString = DdeAccessData(ddeData, &dataLength);
			returnObjPtr = Tcl_NewStringObj(dataString, dataLength);
			DdeUnaccessData(ddeData);
			DdeFreeDataHandle(ddeData);
			Tcl_SetObjResult(interp, returnObjPtr);
		    }
		} else {
		    SetDdeError(interp);
		    result = TCL_ERROR;
		}
		DdeDisconnect(hConv);
	    }

	    break;
	}
	case DDE_SERVICES: {
	    HCONVLIST hConvList;
	    CONVINFO convInfo;
	    Tcl_Obj *convListObjPtr, *elementObjPtr;
	    Tcl_DString dString;
	    char *name;
	    
	    convInfo.cb = sizeof(CONVINFO);
	    hConvList = DdeConnectList(ddeInstance, ddeService, ddeTopic,
		    0, NULL);
	    hConv = 0;
	    convListObjPtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
	    Tcl_DStringInit(&dString);

	    while (hConv = DdeQueryNextServer(hConvList, hConv), hConv != 0) {
		elementObjPtr = Tcl_NewListObj(0, (Tcl_Obj **) NULL);
		DdeQueryConvInfo(hConv, QID_SYNC, &convInfo);
		length = DdeQueryString(ddeInstance, convInfo.hszSvcPartner,
			NULL, 0, CP_WINANSI);
		Tcl_DStringSetLength(&dString, length);
		name = Tcl_DStringValue(&dString);
		DdeQueryString(ddeInstance, convInfo.hszSvcPartner, name,
			length + 1, CP_WINANSI);
		Tcl_ListObjAppendElement(interp, elementObjPtr,
			Tcl_NewStringObj(name, length));
		length = DdeQueryString(ddeInstance, convInfo.hszTopic,
			NULL, 0, CP_WINANSI);
		Tcl_DStringSetLength(&dString, length);
		name = Tcl_DStringValue(&dString);
		DdeQueryString(ddeInstance, convInfo.hszTopic, name,
			length + 1, CP_WINANSI);
		Tcl_ListObjAppendElement(interp, elementObjPtr,
			Tcl_NewStringObj(name, length));
		Tcl_ListObjAppendElement(interp, convListObjPtr, elementObjPtr);
	    }
	    DdeDisconnectList(hConvList);
	    Tcl_SetObjResult(interp, convListObjPtr);
	    Tcl_DStringFree(&dString);
	    break;
	}
    }
    if (ddeService != NULL) {
	DdeFreeStringHandle(ddeInstance, ddeService);
    }
    if (ddeTopic != NULL) {
	DdeFreeStringHandle(ddeInstance, ddeTopic);
    }

    return result;
}
