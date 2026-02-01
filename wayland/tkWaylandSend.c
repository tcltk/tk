/*
 * tkUnixSend.c --
 *
 *	This file provides functions that implement the "send" command,
 *	allowing commands to be passed from interpreter to interpreter.
 *
 * Copyright © 1989-1994 The Regents of the University of California.
 * Copyright © 1994-1996 Sun Microsystems, Inc.
 * Copyright © 1998-1999 Scriptics Corporation.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkUnixInt.h"
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <pwd.h>

/*
 * The following structure is used to keep track of the interpreters
 * registered by this process.
 */

typedef struct RegisteredInterp {
    char *name;			/* Interpreter's name (malloc-ed). */
    Tcl_Interp *interp;		/* Interpreter associated with name. NULL
				 * means that the application was unregistered
				 * or deleted while a send was in progress to
				 * it. */
    int sockfd;			/* Unix socket descriptor for receiving
				 * commands. */
    char *socketPath;		/* Path to Unix socket file (malloc-ed). */
    struct RegisteredInterp *nextPtr;
				/* Next in list of names associated with
				 * interps in this process. NULL means end of
				 * list. */
} RegisteredInterp;

/*
 * A registry of all interpreters is kept in a directory
 * "$XDG_RUNTIME_DIR/tk-send-registry/". Each file is named after
 * the interpreter name and contains the socket path.
 *
 * When the registry is being manipulated by an application (e.g. to add or
 * remove an entry), we operate directly on the filesystem.
 */

/*
 * When a result is being awaited from a sent command, one of the following
 * structures is present on a list of all outstanding sent commands. The
 * information in the structure is used to process the result when it arrives.
 */

typedef struct PendingCommand {
    int serial;			/* Serial number expected in result. */
    const char *target;		/* Name of interpreter command is being sent
				 * to. */
    char *socketPath;		/* Path to target's Unix socket. */
    Tcl_Interp *interp;		/* Interpreter from which the send was
				 * invoked. */
    int code;			/* Tcl return code for command will be stored
				 * here. */
    char *result;		/* String result for command (malloc'ed), or
				 * NULL. */
    char *errorInfo;		/* Information for "errorInfo" variable, or
				 * NULL (malloc'ed). */
    char *errorCode;		/* Information for "errorCode" variable, or
				 * NULL (malloc'ed). */
    int gotResponse;		/* 1 means a response has been received, 0
				 * means the command is still outstanding. */
    struct PendingCommand *nextPtr;
				/* Next in list of all outstanding commands.
				 * NULL means end of list. */
} PendingCommand;

typedef struct {
    PendingCommand *pendingCommands;
				/* List of all commands currently being waited
				 * for. */
    RegisteredInterp *interpListPtr;
				/* List of all interpreters registered in the
				 * current process. */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * Communication protocol:
 *
 * Each command and each result is sent as a single datagram over Unix socket.
 * The format is the same as before: a zero byte followed by null-terminated
 * strings.
 *
 * Command:
 *   \0 c \0 -n name \0 -s script \0 [-r socket_path serial] \0
 *
 * Result:
 *   \0 r \0 -s serial \0 -c code \0 -r result \0 [-i errorInfo] \0 [-e errorCode] \0
 */

/*
 * Other miscellaneous per-process data:
 */

static struct {
    int sendSerial;		/* The serial number that was used in the last
				 * "send" command. */
    int sendDebug;		/* This can be set while debugging. */
} localData = {0, 0};

/*
 * Forward declarations for functions defined later in this file:
 */

static void		DeleteProc(void *clientData);
static char *		GetRegistryDir(void);
static char *		GetSocketPathFromRegistry(const char *name);
static int		AddToRegistry(const char *name, const char *socketPath);
static int		RemoveFromRegistry(const char *name);
static void		SocketEventProc(ClientData clientData, int mask);
static int		SendInit(Tcl_Interp *interp, const char *name);
static int		ValidateSocket(const char *socketPath);
static int		SendViaSocket(const char *socketPath,
			    const char *data, int length);
static char *		CreateUniqueSocketPath(const char *baseName);
static char *		GetMySocketPath(void);

/*
 *----------------------------------------------------------------------
 *
 * GetRegistryDir --
 *
 *	Get the path to the registry directory.
 *
 * Results:
 *	Returns the path (malloc'ed), or NULL on error.
 *
 * Side effects:
 *	Creates directory if it doesn't exist.
 *
 *----------------------------------------------------------------------
 */

static char *
GetRegistryDir(void)
{
    static char *registryDir = NULL;
    const char *runtimeDir;
    struct stat st;
    
    if (registryDir != NULL) {
	return registryDir;
    }
    
    runtimeDir = getenv("XDG_RUNTIME_DIR");
    if (runtimeDir == NULL) {
	/* Fallback to /tmp if XDG_RUNTIME_DIR is not set */
	runtimeDir = "/tmp";
    }
    
    registryDir = (char *)Tcl_Alloc(strlen(runtimeDir) + 20);
    sprintf(registryDir, "%s/tk-send-registry", runtimeDir);
    
    /* Create directory if it doesn't exist */
    if (stat(registryDir, &st) != 0) {
	if (mkdir(registryDir, 0700) != 0) {
	    Tcl_Free(registryDir);
	    registryDir = NULL;
	    return NULL;
	}
    }
    
    return registryDir;
}

/*
 *----------------------------------------------------------------------
 *
 * GetSocketPathFromRegistry --
 *
 *	Look up a socket path for a given interpreter name.
 *
 * Results:
 *	Returns the socket path (malloc'ed), or NULL if not found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static char *
GetSocketPathFromRegistry(
    const char *name)		/* Name of interpreter to look up. */
{
    char *registryDir, *filePath;
    Tcl_Channel chan;
    char *socketPath = NULL;
    Tcl_DString ds;
    
    registryDir = GetRegistryDir();
    if (registryDir == NULL) {
	return NULL;
    }
    
    filePath = (char *)Tcl_Alloc(strlen(registryDir) + strlen(name) + 2);
    sprintf(filePath, "%s/%s", registryDir, name);
    
    chan = Tcl_OpenFileChannel(NULL, filePath, "r", 0);
    if (chan != NULL) {
	Tcl_DStringInit(&ds);
	Tcl_ReadChars(chan, &ds, -1, 0);
	Tcl_Close(NULL, chan);
	
	if (Tcl_DStringLength(&ds) > 0) {
	    socketPath = (char *)Tcl_Alloc(Tcl_DStringLength(&ds) + 1);
	    strcpy(socketPath, Tcl_DStringValue(&ds));
	}
	Tcl_DStringFree(&ds);
    }
    
    Tcl_Free(filePath);
    return socketPath;
}

/*
 *----------------------------------------------------------------------
 *
 * AddToRegistry --
 *
 *	Add an entry to the registry.
 *
 * Results:
 *	Returns TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Creates a file in the registry directory.
 *
 *----------------------------------------------------------------------
 */

static int
AddToRegistry(
    const char *name,		/* Name of interpreter. */
    const char *socketPath)	/* Path to its socket. */
{
    char *registryDir, *filePath;
    Tcl_Channel chan;
    int result;
    
    registryDir = GetRegistryDir();
    if (registryDir == NULL) {
	return TCL_ERROR;
    }
    
    filePath = (char *)Tcl_Alloc(strlen(registryDir) + strlen(name) + 2);
    sprintf(filePath, "%s/%s", registryDir, name);
    
    chan = Tcl_OpenFileChannel(NULL, filePath, "w", 0600);
    if (chan == NULL) {
	Tcl_Free(filePath);
	return TCL_ERROR;
    }
    
    result = Tcl_WriteChars(chan, socketPath, -1);
    Tcl_Close(NULL, chan);
    Tcl_Free(filePath);
    
    return (result >= 0) ? TCL_OK : TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * RemoveFromRegistry --
 *
 *	Remove an entry from the registry.
 *
 * Results:
 *	Returns TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Removes a file from the registry directory.
 *
 *----------------------------------------------------------------------
 */

static int
RemoveFromRegistry(
    const char *name)		/* Name of interpreter to remove. */
{
    char *registryDir, *filePath;
    int result;
    
    registryDir = GetRegistryDir();
    if (registryDir == NULL) {
	return TCL_ERROR;
    }
    
    filePath = (char *)Tcl_Alloc(strlen(registryDir) + strlen(name) + 2);
    sprintf(filePath, "%s/%s", registryDir, name);
    
    result = (unlink(filePath) == 0) ? TCL_OK : TCL_ERROR;
    Tcl_Free(filePath);
    
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateUniqueSocketPath --
 *
 *	Create a unique socket path for an interpreter.
 *
 * Results:
 *	Returns the socket path (malloc'ed).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static char *
CreateUniqueSocketPath(
    const char *baseName)	/* Base name for the socket. */
{
    const char *runtimeDir;
    char *socketPath;
    pid_t pid = getpid();
    
    runtimeDir = getenv("XDG_RUNTIME_DIR");
    if (runtimeDir == NULL) {
	runtimeDir = "/tmp";
    }
    
    /* Create a unique socket path */
    socketPath = (char *)Tcl_Alloc(strlen(runtimeDir) + strlen(baseName) + 20);
    sprintf(socketPath, "%s/tk-%s-%d.sock", runtimeDir, baseName, pid);
    
    /* Make sure it doesn't exist */
    unlink(socketPath);
    
    return socketPath;
}

/*
 *----------------------------------------------------------------------
 *
 * GetMySocketPath --
 *
 *	Get the socket path for the current interpreter.
 *
 * Results:
 *	Returns the socket path (malloc'ed), or NULL if not registered.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static char *
GetMySocketPath(void)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    RegisteredInterp *riPtr;
    
    for (riPtr = tsdPtr->interpListPtr; riPtr != NULL; riPtr = riPtr->nextPtr) {
	if (riPtr->interp == Tcl_GetCurrentThread()) {
	    return riPtr->socketPath;
	}
    }
    
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * ValidateSocket --
 *
 *	Check if a socket is still valid and reachable.
 *
 * Results:
 *	Returns 1 if valid, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ValidateSocket(
    const char *socketPath)	/* Path to socket to validate. */
{
    int sock;
    struct sockaddr_un addr;
    struct stat st;
    
    /* Check if socket file exists */
    if (stat(socketPath, &st) != 0) {
	return 0;
    }
    
    /* Try to connect to the socket */
    sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0) {
	return 0;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);
    
    /* Non-blocking connect to test if socket is alive */
    fcntl(sock, F_SETFL, O_NONBLOCK);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
	if (errno != EINPROGRESS) {
	    close(sock);
	    return 0;
	}
	
	/* Wait a short time for connection */
	fd_set set;
	struct timeval timeout;
	FD_ZERO(&set);
	FD_SET(sock, &set);
	timeout.tv_sec = 0;
	timeout.tv_usec = 10000; /* 10ms */
	
	if (select(sock + 1, NULL, &set, NULL, &timeout) <= 0) {
	    close(sock);
	    return 0;
	}
    }
    
    close(sock);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * SendViaSocket --
 *
 *	Send data to a Unix socket.
 *
 * Results:
 *	Returns 0 on success, -1 on error.
 *
 * Side effects:
 *	Data is sent to the socket.
 *
 *----------------------------------------------------------------------
 */

static int
SendViaSocket(
    const char *socketPath,	/* Path to target socket. */
    const char *data,		/* Data to send. */
    int length)			/* Length of data. */
{
    int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    struct sockaddr_un addr;
    ssize_t result;
    
    if (sock < 0) {
	return -1;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);
    
    result = sendto(sock, data, length, 0,
	    (struct sockaddr *)&addr, sizeof(addr));
    
    close(sock);
    return (result == length) ? 0 : -1;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetAppName --
 *
 *	This function is called to associate an ASCII name with a Tk
 *	application. If the application has already been named, the name
 *	replaces the old one.
 *
 * Results:
 *	The return value is the name actually given to the application.
 *
 * Side effects:
 *	Registration info is saved, thereby allowing the "send" command to be
 *	used later.
 *
 *----------------------------------------------------------------------
 */

const char *
Tk_SetAppName(
    Tk_Window tkwin,		/* Token for any window in the application. */
    const char *name)		/* The name that will be used to refer to the
				 * interpreter in later "send" commands. */
{
    RegisteredInterp *riPtr, *riPtr2;
    TkWindow *winPtr = (TkWindow *) tkwin;
    Tcl_Interp *interp;
    const char *actualName;
    Tcl_DString dString;
    int i;
    char *socketPath;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    interp = winPtr->mainPtr->interp;

    /* Check if already registered */
    for (riPtr = tsdPtr->interpListPtr; riPtr != NULL; riPtr = riPtr->nextPtr) {
	if (riPtr->interp == interp) {
	    /* Already registered, update name */
	    if (riPtr->name != NULL) {
		RemoveFromRegistry(riPtr->name);
		Tcl_Free(riPtr->name);
	    }
	    riPtr->name = (char *)Tcl_Alloc(strlen(name) + 1);
	    strcpy(riPtr->name, name);
	    AddToRegistry(name, riPtr->socketPath);
	    return riPtr->name;
	}
    }

    /* Create new registration */
    riPtr = (RegisteredInterp *)Tcl_Alloc(sizeof(RegisteredInterp));
    riPtr->interp = interp;
    riPtr->nextPtr = tsdPtr->interpListPtr;
    tsdPtr->interpListPtr = riPtr;

    /* Create socket */
    socketPath = CreateUniqueSocketPath(name);
    riPtr->socketPath = socketPath;
    
    riPtr->sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (riPtr->sockfd < 0) {
	Tcl_Free(riPtr);
	Tcl_Free(socketPath);
	return name;
    }

    /* Bind socket */
    {
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);
	unlink(socketPath); /* Remove if exists */
	
	if (bind(riPtr->sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
	    close(riPtr->sockfd);
	    Tcl_Free(riPtr);
	    Tcl_Free(socketPath);
	    return name;
	}
    }

    /* Set up file handler for incoming messages */
    Tcl_CreateFileHandler(riPtr->sockfd, TCL_READABLE, SocketEventProc, riPtr);

    /* Pick unique name */
    actualName = name;
    for (i = 1; ; i++) {
	char *existingPath = GetSocketPathFromRegistry(actualName);
	
	if (existingPath == NULL) {
	    /* Name is available */
	    break;
	}
	
	/* Check if existing registration is still valid */
	if (!ValidateSocket(existingPath)) {
	    /* Stale entry, remove it */
	    RemoveFromRegistry(actualName);
	    Tcl_Free(existingPath);
	    break;
	}
	
	Tcl_Free(existingPath);
	
	/* Name is taken, try next variant */
	if (i == 1) {
	    Tcl_DStringInit(&dString);
	    Tcl_DStringAppend(&dString, name, -1);
	    Tcl_DStringAppend(&dString, " #", 2);
	    actualName = Tcl_DStringValue(&dString);
	} else {
	    Tcl_DStringSetLength(&dString, Tcl_DStringLength(&dString) - TCL_INTEGER_SPACE);
	}
	
	Tcl_DStringAppend(&dString, "", 0);
	sprintf(Tcl_DStringValue(&dString) + Tcl_DStringLength(&dString) - TCL_INTEGER_SPACE,
		"%d", i);
    }

    /* Store name and register */
    riPtr->name = (char *)Tcl_Alloc(strlen(actualName) + 1);
    strcpy(riPtr->name, actualName);
    AddToRegistry(actualName, socketPath);

    /* Create send command */
    Tcl_CreateObjCommand(interp, "send", Tk_SendObjCmd, riPtr, DeleteProc);
    if (Tcl_IsSafe(interp)) {
	Tcl_HideCommand(interp, "send", "send");
    }

    if (actualName != name) {
	Tcl_DStringFree(&dString);
    }

    return riPtr->name;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SendObjCmd --
 *
 *	This function is invoked to process the "send" Tcl command.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Sends command to another interpreter.
 *
 *----------------------------------------------------------------------
 */

int
Tk_SendObjCmd(
    void *clientData,	/* Information about sender. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument strings. */
{
    enum {
	SEND_ASYNC, SEND_DISPLAYOF, SEND_LAST
    };
    static const char *const sendOptions[] = {
	"-async",   "-displayof",   "--",  NULL
    };
    const char *stringRep, *destName;
    PendingCommand pending;
    RegisteredInterp *riPtr;
    int result, async, index;
    Tcl_Size i, firstArg;
    Tcl_DString request;
    char *socketPath;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    Tcl_Interp *localInterp;

    /* Process options */
    async = 0;
    for (i = 1; i < (objc - 1); i++) {
	stringRep = Tcl_GetString(objv[i]);
	if (stringRep[0] == '-') {
	    if (Tcl_GetIndexFromObjStruct(interp, objv[i], sendOptions,
		    sizeof(char *), "option", 0, &index) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (index == SEND_ASYNC) {
		++async;
	    } else if (index == SEND_DISPLAYOF) {
		/* Ignored under Wayland */
		i++;
	    } else /* if (index == SEND_LAST) */ {
		i++;
		break;
	    }
	} else {
	    break;
	}
    }

    if (objc < (i + 2)) {
	Tcl_WrongNumArgs(interp, 1, objv,
		"?-option value ...? interpName arg ?arg ...?");
	return TCL_ERROR;
    }
    destName = Tcl_GetString(objv[i]);
    firstArg = i + 1;

    /* Check for local interpreter */
    for (riPtr = tsdPtr->interpListPtr; riPtr != NULL;
	    riPtr = riPtr->nextPtr) {
	if (riPtr->name && strcmp(riPtr->name, destName) == 0) {
	    Tcl_Preserve(riPtr);
	    localInterp = riPtr->interp;
	    Tcl_Preserve(localInterp);
	    
	    if (firstArg == (objc - 1)) {
		result = Tcl_EvalEx(localInterp, Tcl_GetString(objv[firstArg]), 
			TCL_INDEX_NONE, TCL_EVAL_GLOBAL);
	    } else {
		Tcl_DStringInit(&request);
		Tcl_DStringAppend(&request, Tcl_GetString(objv[firstArg]), TCL_INDEX_NONE);
		for (i = firstArg + 1; i < objc; i++) {
		    Tcl_DStringAppend(&request, " ", 1);
		    Tcl_DStringAppend(&request, Tcl_GetString(objv[i]), TCL_INDEX_NONE);
		}
		result = Tcl_EvalEx(localInterp, Tcl_DStringValue(&request), 
			TCL_INDEX_NONE, TCL_EVAL_GLOBAL);
		Tcl_DStringFree(&request);
	    }
	    
	    if (interp != localInterp) {
		if (result == TCL_ERROR) {
		    Tcl_Obj *errorObjPtr;
		    Tcl_ResetResult(interp);
		    Tcl_AddErrorInfo(interp, Tcl_GetVar2(localInterp,
			    "errorInfo", NULL, TCL_GLOBAL_ONLY));
		    errorObjPtr = Tcl_GetVar2Ex(localInterp, "errorCode", NULL,
			    TCL_GLOBAL_ONLY);
		    Tcl_SetObjErrorCode(interp, errorObjPtr);
		}
		Tcl_SetObjResult(interp, Tcl_GetObjResult(localInterp));
		Tcl_ResetResult(localInterp);
	    }
	    
	    Tcl_Release(riPtr);
	    Tcl_Release(localInterp);
	    return result;
	}
    }

    /* Look up socket path in registry */
    socketPath = GetSocketPathFromRegistry(destName);
    if (socketPath == NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"no application named \"%s\"", destName));
	Tcl_SetErrorCode(interp, "TK", "LOOKUP", "APPLICATION", destName,
		NULL);
	return TCL_ERROR;
    }

    /* Validate socket is still alive */
    if (!ValidateSocket(socketPath)) {
	Tcl_Free(socketPath);
	RemoveFromRegistry(destName); /* Clean up stale entry */
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"application \"%s\" is no longer running", destName));
	Tcl_SetErrorCode(interp, "TK", "LOOKUP", "APPLICATION", destName,
		NULL);
	return TCL_ERROR;
    }

    /* Build request */
    localData.sendSerial++;
    Tcl_DStringInit(&request);
    Tcl_DStringAppend(&request, "\0c\0-n ", 6);
    Tcl_DStringAppend(&request, destName, TCL_INDEX_NONE);
    
    if (!async) {
	char buffer[TCL_INTEGER_SPACE * 2 + PATH_MAX];
	char *mySocketPath = GetMySocketPath();
	if (mySocketPath == NULL) {
	    Tcl_Free(socketPath);
	    Tcl_DStringFree(&request);
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "cannot send: current interpreter not registered", -1));
	    return TCL_ERROR;
	}
	snprintf(buffer, sizeof(buffer), "%s %d", mySocketPath, localData.sendSerial);
	Tcl_DStringAppend(&request, "\0-r ", 4);
	Tcl_DStringAppend(&request, buffer, TCL_INDEX_NONE);
    }
    
    Tcl_DStringAppend(&request, "\0-s ", 4);
    Tcl_DStringAppend(&request, Tcl_GetString(objv[firstArg]), TCL_INDEX_NONE);
    for (i = firstArg + 1; i < objc; i++) {
	Tcl_DStringAppend(&request, " ", 1);
	Tcl_DStringAppend(&request, Tcl_GetString(objv[i]), TCL_INDEX_NONE);
    }
    Tcl_DStringAppend(&request, "\0", 1); /* Final null terminator */

    if (!async) {
	/* Setup pending command structure */
	pending.serial = localData.sendSerial;
	pending.target = destName;
	pending.socketPath = socketPath;
	pending.interp = interp;
	pending.result = NULL;
	pending.errorInfo = NULL;
	pending.errorCode = NULL;
	pending.gotResponse = 0;
	pending.nextPtr = tsdPtr->pendingCommands;
	tsdPtr->pendingCommands = &pending;
    }

    /* Send via socket */
    if (SendViaSocket(socketPath, Tcl_DStringValue(&request),
	    Tcl_DStringLength(&request) + 1) != 0) {
	Tcl_DStringFree(&request);
	Tcl_Free(socketPath);
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"cannot send to application \"%s\"", destName));
	return TCL_ERROR;
    }
    
    Tcl_DStringFree(&request);
    Tcl_Free(socketPath);

    if (async) {
	/* Asynchronous send - return immediately */
	return TCL_OK;
    }

    /* Synchronous send - wait for response */
    while (!pending.gotResponse) {
	Tcl_DoOneEvent(TCL_ALL_EVENTS);
    }

    /* Clean up pending command */
    if (tsdPtr->pendingCommands != &pending) {
	Tcl_Panic("Tk_SendObjCmd: corrupted send stack");
    }
    tsdPtr->pendingCommands = pending.nextPtr;
    
    /* Set result in interpreter */
    if (pending.errorInfo != NULL) {
	Tcl_ResetResult(interp);
	Tcl_AddErrorInfo(interp, pending.errorInfo);
	Tcl_Free(pending.errorInfo);
    }
    if (pending.errorCode != NULL) {
	Tcl_SetObjErrorCode(interp, Tcl_NewStringObj(pending.errorCode, TCL_INDEX_NONE));
	Tcl_Free(pending.errorCode);
    }
    if (pending.result != NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(pending.result, TCL_INDEX_NONE));
	Tcl_Free(pending.result);
    }
    
    return pending.code;
}

/*
 *----------------------------------------------------------------------
 *
 * SocketEventProc --
 *
 *	Handle incoming data on a Unix socket.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Executes commands or processes responses.
 *
 *----------------------------------------------------------------------
 */

static void
SocketEventProc(
    ClientData clientData,	/* RegisteredInterp pointer. */
    int mask)			/* Event mask (TCL_READABLE). */
{
    RegisteredInterp *riPtr = (RegisteredInterp *)clientData;
    char buffer[65536];
    ssize_t n;
    const char *p;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    /* Read data from socket */
    n = recv(riPtr->sockfd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
	return;
    }
    buffer[n] = '\0';

    p = buffer;
    while ((p - buffer) < n) {
	/* Skip leading null bytes */
	if (*p == 0) {
	    p++;
	    continue;
	}

	if ((*p == 'c') && (p[1] == 0)) {
	    /* Incoming command */
	    const char *interpName = NULL;
	    const char *script = NULL;
	    const char *replySocket = NULL;
	    const char *serial = "";
	    const char *q;
	    
	    p += 2; /* Skip "c\0" */
	    q = p;
	    
	    /* Parse options */
	    while ((q - buffer) < n && *q == '-') {
		switch (q[1]) {
		    case 'r': /* Reply socket and serial */
			if (q[2] == ' ') {
			    replySocket = q + 3;
			    while (*q != 0) q++;
			    q++;
			    serial = q;
			}
			break;
		    case 'n': /* Interpreter name */
			if (q[2] == ' ') {
			    interpName = q + 3;
			}
			break;
		    case 's': /* Script */
			if (q[2] == ' ') {
			    script = q + 3;
			}
			break;
		}
		while (*q != 0) q++;
		q++;
	    }

	    if (interpName == NULL || script == NULL) {
		/* Malformed command */
		continue;
	    }

	    /* Check if this command is for us */
	    if (riPtr->name == NULL || strcmp(riPtr->name, interpName) != 0) {
		/* Not for us */
		continue;
	    }

	    /* Execute the script */
	    Tcl_Preserve(riPtr);
	    Tcl_Preserve(riPtr->interp);
	    
	    int evalResult = Tcl_EvalEx(riPtr->interp, script, TCL_INDEX_NONE, TCL_EVAL_GLOBAL);
	    const char *evalResultStr = Tcl_GetString(Tcl_GetObjResult(riPtr->interp));
	    
	    /* Send reply if requested */
	    if (replySocket != NULL) {
		Tcl_DString reply;
		Tcl_DStringInit(&reply);
		
		/* Result header */
		Tcl_DStringAppend(&reply, "\0r\0-s ", 6);
		Tcl_DStringAppend(&reply, serial, TCL_INDEX_NONE);
		
		/* Result code */
		if (evalResult != TCL_OK) {
		    char codeBuf[TCL_INTEGER_SPACE];
		    snprintf(codeBuf, sizeof(codeBuf), "%d", evalResult);
		    Tcl_DStringAppend(&reply, "\0-c ", 4);
		    Tcl_DStringAppend(&reply, codeBuf, TCL_INDEX_NONE);
		}
		
		/* Result string */
		Tcl_DStringAppend(&reply, "\0-r ", 4);
		Tcl_DStringAppend(&reply, evalResultStr, TCL_INDEX_NONE);
		
		/* Error info if applicable */
		if (evalResult == TCL_ERROR) {
		    const char *errorInfo = Tcl_GetVar2(riPtr->interp, 
			    "errorInfo", NULL, TCL_GLOBAL_ONLY);
		    if (errorInfo != NULL) {
			Tcl_DStringAppend(&reply, "\0-i ", 4);
			Tcl_DStringAppend(&reply, errorInfo, TCL_INDEX_NONE);
		    }
		    
		    const char *errorCode = Tcl_GetVar2(riPtr->interp,
			    "errorCode", NULL, TCL_GLOBAL_ONLY);
		    if (errorCode != NULL) {
			Tcl_DStringAppend(&reply, "\0-e ", 4);
			Tcl_DStringAppend(&reply, errorCode, TCL_INDEX_NONE);
		    }
		}
		
		Tcl_DStringAppend(&reply, "\0", 1); /* Final null */
		
		/* Send reply */
		SendViaSocket(replySocket, Tcl_DStringValue(&reply),
			Tcl_DStringLength(&reply) + 1);
		
		Tcl_DStringFree(&reply);
	    }
	    
	    Tcl_Release(riPtr->interp);
	    Tcl_Release(riPtr);
	    p = q;
	    
	} else if ((*p == 'r') && (p[1] == 0)) {
	    /* Incoming response */
	    int serial = 0;
	    int code = TCL_OK;
	    const char *resultStr = "";
	    const char *errorInfo = NULL;
	    const char *errorCode = NULL;
	    const char *q;
	    int gotSerial = 0;
	    PendingCommand *pcPtr;
	    
	    p += 2; /* Skip "r\0" */
	    q = p;
	    
	    /* Parse options */
	    while ((q - buffer) < n && *q == '-') {
		switch (q[1]) {
		    case 's': /* Serial number */
			if (sscanf(q + 2, " %d", &serial) == 1) {
			    gotSerial = 1;
			}
			break;
		    case 'c': /* Result code */
			sscanf(q + 2, " %d", &code);
			break;
		    case 'r': /* Result string */
			if (q[2] == ' ') {
			    resultStr = q + 3;
			}
			break;
		    case 'i': /* Error info */
			if (q[2] == ' ') {
			    errorInfo = q + 3;
			}
			break;
		    case 'e': /* Error code */
			if (q[2] == ' ') {
			    errorCode = q + 3;
			}
			break;
		}
		while (*q != 0) q++;
		q++;
	    }
	    
	    if (!gotSerial) {
		p = q;
		continue;
	    }
	    
	    /* Find matching pending command */
	    for (pcPtr = tsdPtr->pendingCommands; pcPtr != NULL;
		    pcPtr = pcPtr->nextPtr) {
		if (pcPtr->serial == serial && pcPtr->result == NULL) {
		    /* Found it */
		    pcPtr->code = code;
		    
		    if (resultStr != NULL) {
			pcPtr->result = (char *)Tcl_Alloc(strlen(resultStr) + 1);
			strcpy(pcPtr->result, resultStr);
		    }
		    
		    if (code == TCL_ERROR) {
			if (errorInfo != NULL) {
			    pcPtr->errorInfo = (char *)Tcl_Alloc(strlen(errorInfo) + 1);
			    strcpy(pcPtr->errorInfo, errorInfo);
			}
			if (errorCode != NULL) {
			    pcPtr->errorCode = (char *)Tcl_Alloc(strlen(errorCode) + 1);
			    strcpy(pcPtr->errorCode, errorCode);
			}
		    }
		    
		    pcPtr->gotResponse = 1;
		    break;
		}
	    }
	    
	    p = q;
	} else {
	    /* Unknown message type, skip to next null */
	    while (*p != 0 && (p - buffer) < n) p++;
	    p++;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DeleteProc --
 *
 *	Clean up when "send" command is deleted.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Unregisters interpreter and cleans up resources.
 *
 *----------------------------------------------------------------------
 */

static void
DeleteProc(
    void *clientData)	/* Info about registration */
{
    RegisteredInterp *riPtr = (RegisteredInterp *)clientData;
    RegisteredInterp *riPtr2;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    /* Remove from registry */
    if (riPtr->name != NULL) {
	RemoveFromRegistry(riPtr->name);
    }

    /* Close socket and remove socket file */
    if (riPtr->sockfd >= 0) {
	Tcl_DeleteFileHandler(riPtr->sockfd);
	close(riPtr->sockfd);
    }
    
    if (riPtr->socketPath != NULL) {
	unlink(riPtr->socketPath);
	Tcl_Free(riPtr->socketPath);
    }

    /* Remove from list */
    if (tsdPtr->interpListPtr == riPtr) {
	tsdPtr->interpListPtr = riPtr->nextPtr;
    } else {
	for (riPtr2 = tsdPtr->interpListPtr; riPtr2 != NULL;
		riPtr2 = riPtr2->nextPtr) {
	    if (riPtr2->nextPtr == riPtr) {
		riPtr2->nextPtr = riPtr->nextPtr;
		break;
	    }
	}
    }

    /* Free memory */
    if (riPtr->name != NULL) {
	Tcl_Free(riPtr->name);
    }
    riPtr->interp = NULL;
    Tcl_EventuallyFree(riPtr, TCL_DYNAMIC);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetInterpNames --
 *
 *	Fetch a list of all interpreter names.
 *
 * Results:
 *	TCL_OK with list result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkGetInterpNames(
    Tcl_Interp *interp,		/* Interpreter for returning result. */
    Tk_Window tkwin)		/* Window (unused). */
{
    char *registryDir;
    DIR *dir;
    struct dirent *entry;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);
    
    registryDir = GetRegistryDir();
    if (registryDir == NULL) {
	Tcl_SetObjResult(interp, resultObj);
	return TCL_OK;
    }
    
    dir = opendir(registryDir);
    if (dir == NULL) {
	Tcl_Free(registryDir);
	Tcl_SetObjResult(interp, resultObj);
	return TCL_OK;
    }
    
    while ((entry = readdir(dir)) != NULL) {
	/* Skip . and .. */
	if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
	    continue;
	}
	
	char *socketPath = GetSocketPathFromRegistry(entry->d_name);
	if (socketPath != NULL) {
	    if (ValidateSocket(socketPath)) {
		Tcl_ListObjAppendElement(interp, resultObj,
			Tcl_NewStringObj(entry->d_name, TCL_INDEX_NONE));
	    } else {
		/* Remove stale entry */
		RemoveFromRegistry(entry->d_name);
	    }
	    Tcl_Free(socketPath);
	}
    }
    
    closedir(dir);
    Tcl_Free(registryDir);
    
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkSendCleanup --
 *
 *	Clean up send resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Closes sockets and cleans up registry.
 *
 *----------------------------------------------------------------------
 */

void
TkSendCleanup(
    TkDisplay *dispPtr)		/* Unused. */
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    RegisteredInterp *riPtr, *next;

    for (riPtr = tsdPtr->interpListPtr; riPtr != NULL; riPtr = next) {
	next = riPtr->nextPtr;
	
	if (riPtr->sockfd >= 0) {
	    Tcl_DeleteFileHandler(riPtr->sockfd);
	    close(riPtr->sockfd);
	}
	
	if (riPtr->socketPath != NULL) {
	    unlink(riPtr->socketPath);
	    Tcl_Free(riPtr->socketPath);
	}
	
	if (riPtr->name != NULL) {
	    RemoveFromRegistry(riPtr->name);
	    Tcl_Free(riPtr->name);
	}
	
	Tcl_Free(riPtr);
    }
    
    tsdPtr->interpListPtr = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpTestsendCmd --
 *
 *	This function implements the "testsend" command for testing.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Depends on option.
 *
 *----------------------------------------------------------------------
 */

int
TkpTestsendCmd(
    void *clientData,	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])		/* Argument strings. */
{
    enum {
	TESTSEND_BOGUS, TESTSEND_PROP, TESTSEND_SERIAL
    };
    static const char *const testsendOptions[] = {
	"bogus",   "prop",   "serial",  NULL
    };
    int index;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv,
		"option ?arg ...?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[1], testsendOptions,
		sizeof(char *), "option", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }
    
    if (index == TESTSEND_SERIAL) {
	Tcl_SetObjResult(interp, Tcl_NewWideIntObj(localData.sendSerial + 1));
    } else {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Not implemented", -1));
    }
    
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */