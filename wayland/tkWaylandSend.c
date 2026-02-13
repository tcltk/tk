/*
 * tkWaylandSend.c --
 *
 *      This file provides functions that implement the "send" command,
 *      allowing commands to be passed from interpreter to interpreter.
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

typedef struct RegisteredInterp {
    char *name;                 /* Interpreter's name (malloc-ed). */
    Tcl_Interp *interp;         /* Interpreter associated with name. */
    int sockfd;                 /* Unix socket descriptor for receiving commands. */
    char *socketPath;           /* Path to Unix socket file (malloc-ed). */
    struct RegisteredInterp *nextPtr;
} RegisteredInterp;

typedef struct PendingCommand {
    int serial;
    const char *target;
    char *socketPath;
    Tcl_Interp *interp;
    int code;
    char *result;
    char *errorInfo;
    char *errorCode;
    int gotResponse;
    struct PendingCommand *nextPtr;
} PendingCommand;

typedef struct {
    PendingCommand *pendingCommands;
    RegisteredInterp *interpListPtr;
    Tcl_Interp *interp;
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

static struct {
    int sendSerial;
    int sendDebug;
} localData = {0, 0};

/* Forward declarations. */
static char *   GetRegistryDir(void);
static char *   GetSocketPathFromRegistry(const char *name);
static int      AddToRegistry(const char *name, const char *socketPath);
static int      RemoveFromRegistry(const char *name);
static void     SocketEventProc(ClientData clientData, int mask);
static int      ValidateSocket(const char *socketPath);
static int      SendViaSocket(const char *socketPath, const char *data, int length);
static char *   CreateUniqueSocketPath(const char *baseName);
static char *   GetMySocketPath(void);
static void     DeleteProc(ClientData clientData);

/*
 *----------------------------------------------------------------------
 *
 * GetRegistryDir --
 *
 *      Get the path to the registry directory.
 *
 * Results:
 *      Returns malloc'ed path or NULL on error.
 *
 * Side effects:
 *      Creates directory with 0700 permissions if needed.
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
        runtimeDir = "/tmp";
    }

    registryDir = Tcl_Alloc(strlen(runtimeDir) + 20);
    sprintf(registryDir, "%s/tk-send-registry", runtimeDir);

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
 *      Retrieve socket path from registry file.
 *
 * Results:
 *      Malloc'ed string or NULL if not found/error.
 *
 *----------------------------------------------------------------------
 */

static char *
GetSocketPathFromRegistry(const char *name)
{
    char *registryDir, *filePath;
    Tcl_Channel chan;
    Tcl_Obj *contentObj = NULL;
    char *socketPath = NULL;
    Tcl_Size len;
    const char *str;

    registryDir = GetRegistryDir();
    if (registryDir == NULL) {
        return NULL;
    }

    filePath = Tcl_Alloc(strlen(registryDir) + strlen(name) + 2);
    sprintf(filePath, "%s/%s", registryDir, name);

    chan = Tcl_OpenFileChannel(NULL, filePath, "r", 0);
    if (chan == NULL) {
        Tcl_Free(filePath);
        return NULL;
    }

    contentObj = Tcl_NewObj();
    if (Tcl_ReadChars(chan, contentObj, -1, 0) == TCL_OK) {
        str = Tcl_GetStringFromObj(contentObj, &len);
        if (len > 0) {
            socketPath = Tcl_Alloc(len + 1);
            memcpy(socketPath, str, len);
            socketPath[len] = '\0';
        }
    }

    if (contentObj) {
        Tcl_DecrRefCount(contentObj);
    }
    Tcl_Close(NULL, chan);
    Tcl_Free(filePath);

    return socketPath;
}

/*
 *----------------------------------------------------------------------
 *
 * AddToRegistry --
 *
 *      Write socket path to registry file.
 *
 * Results:
 *      TCL_OK or TCL_ERROR.
 *
 *----------------------------------------------------------------------
 */

static int
AddToRegistry(const char *name, const char *socketPath)
{
    char *registryDir, *filePath;
    Tcl_Channel chan;
    int result;

    registryDir = GetRegistryDir();
    if (registryDir == NULL) {
        return TCL_ERROR;
    }

    filePath = Tcl_Alloc(strlen(registryDir) + strlen(name) + 2);
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
 *      Delete registry file entry.
 *
 * Results:
 *      TCL_OK or TCL_ERROR.
 *
 *----------------------------------------------------------------------
 */

static int
RemoveFromRegistry(const char *name)
{
    char *registryDir, *filePath;
    int result;

    registryDir = GetRegistryDir();
    if (registryDir == NULL) {
        return TCL_ERROR;
    }

    filePath = Tcl_Alloc(strlen(registryDir) + strlen(name) + 2);
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
 *      Generate unique socket filename using PID.
 *
 * Results:
 *      Malloc'ed path.
 *
 * Side effects:
 *      Unlinks any existing file at that path.
 *
 *----------------------------------------------------------------------
 */

static char *
CreateUniqueSocketPath(const char *baseName)
{
    const char *runtimeDir;
    char *socketPath;
    pid_t pid = getpid();

    runtimeDir = getenv("XDG_RUNTIME_DIR");
    if (runtimeDir == NULL) {
        runtimeDir = "/tmp";
    }

    socketPath = Tcl_Alloc(strlen(runtimeDir) + strlen(baseName) + 20);
    sprintf(socketPath, "%s/tk-%s-%d.sock", runtimeDir, baseName, pid);

    unlink(socketPath);

    return socketPath;
}

/*
 *----------------------------------------------------------------------
 *
 * GetMySocketPath --
 *
 *      Find socket path of the current interpreter.
 *
 * Results:
 *      Socket path or NULL.
 *
 *----------------------------------------------------------------------
 */

static char *
GetMySocketPath(void)
{
    ThreadSpecificData *tsdPtr = Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (tsdPtr->interp == NULL) {
        return NULL;
    }

    RegisteredInterp *riPtr;
    for (riPtr = tsdPtr->interpListPtr; riPtr != NULL; riPtr = riPtr->nextPtr) {
        if (riPtr->interp == tsdPtr->interp) {
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
 *      Test whether socket appears alive.
 *
 * Results:
 *      1 if valid, 0 otherwise.
 *
 *----------------------------------------------------------------------
 */

static int
ValidateSocket(const char *socketPath)
{
    int sock;
    struct sockaddr_un addr;
    struct stat st;

    if (stat(socketPath, &st) != 0) {
        return 0;
    }

    sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0) {
        return 0;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);

    fcntl(sock, F_SETFL, O_NONBLOCK);
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        if (errno != EINPROGRESS) {
            close(sock);
            return 0;
        }

        fd_set set;
        struct timeval tv = { .tv_sec = 0, .tv_usec = 10000 };

        FD_ZERO(&set);
        FD_SET(sock, &set);

        if (select(sock + 1, NULL, &set, NULL, &tv) <= 0) {
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
 *      Send datagram to Unix domain socket.
 *
 * Results:
 *      0 on success, -1 on error.
 *
 *----------------------------------------------------------------------
 */

static int
SendViaSocket(const char *socketPath, const char *data, int length)
{
    int sock;
    struct sockaddr_un addr;
    ssize_t sent;

    sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);

    sent = sendto(sock, data, (size_t)length, 0,
                  (struct sockaddr *)&addr, sizeof(addr));

    close(sock);
    return (sent == length) ? 0 : -1;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetAppName --
 *
 *      Associate a name with the Tk application.
 *
 * Results:
 *      The actual name used (may have #N suffix if needed).
 *
 * Side effects:
 *      Registers interpreter, creates socket, adds to registry.
 *
 *----------------------------------------------------------------------
 */

const char *
Tk_SetAppName(Tk_Window tkwin, const char *name)
{
    RegisteredInterp *riPtr;
    TkWindow *winPtr = (TkWindow *)tkwin;
    Tcl_Interp *interp = winPtr->mainPtr->interp;
    const char *actualName = name;
    Tcl_DString dString;
    int i;
    char *socketPath;
    ThreadSpecificData *tsdPtr = Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    /* Check if already registered. */
    for (riPtr = tsdPtr->interpListPtr; riPtr != NULL; riPtr = riPtr->nextPtr) {
        if (riPtr->interp == interp) {
            if (riPtr->name != NULL) {
                RemoveFromRegistry(riPtr->name);
                Tcl_Free(riPtr->name);
            }
            riPtr->name = Tcl_Alloc(strlen(name) + 1);
            strcpy(riPtr->name, name);
            AddToRegistry(name, riPtr->socketPath);
            return riPtr->name;
        }
    }

    /* New registration. */
    riPtr = Tcl_Alloc(sizeof(RegisteredInterp));
    riPtr->interp = interp;
    riPtr->nextPtr = tsdPtr->interpListPtr;
    tsdPtr->interpListPtr = riPtr;

    socketPath = CreateUniqueSocketPath(name);
    riPtr->socketPath = socketPath;

    riPtr->sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (riPtr->sockfd < 0) {
        tsdPtr->interpListPtr = riPtr->nextPtr;
        Tcl_Free(riPtr);
        Tcl_Free(socketPath);
        return name;
    }

    {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);
        unlink(socketPath);

        if (bind(riPtr->sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            close(riPtr->sockfd);
            tsdPtr->interpListPtr = riPtr->nextPtr;
            Tcl_Free(riPtr);
            Tcl_Free(socketPath);
            return name;
        }
    }

    Tcl_CreateFileHandler(riPtr->sockfd, TCL_READABLE, SocketEventProc, (ClientData)riPtr);

    /* Find unique name. */
    Tcl_DStringInit(&dString);
    for (i = 1; ; i++) {
        char *existing = GetSocketPathFromRegistry(actualName);
        if (existing == NULL) {
            break;
        }
        if (!ValidateSocket(existing)) {
            RemoveFromRegistry(actualName);
            Tcl_Free(existing);
            break;
        }
        Tcl_Free(existing);

        if (i == 1) {
            Tcl_DStringAppend(&dString, name, -1);
            Tcl_DStringAppend(&dString, " #", 2);
        } else {
            Tcl_DStringSetLength(&dString, Tcl_DStringLength(&dString) - 3);
        }
        Tcl_DStringAppend(&dString, Tcl_GetString(Tcl_NewIntObj(i)), -1);
        actualName = Tcl_DStringValue(&dString);
    }

    riPtr->name = Tcl_Alloc(strlen(actualName) + 1);
    strcpy(riPtr->name, actualName);
    AddToRegistry(actualName, socketPath);

    Tcl_CreateObjCommand(interp, "send", Tk_SendObjCmd, (ClientData)riPtr, DeleteProc);
    if (Tcl_IsSafe(interp)) {
        Tcl_HideCommand(interp, "send", "send");
    }

    Tcl_DStringFree(&dString);
    return riPtr->name;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SendObjCmd --
 *
 *      Implements the Tcl "send" command.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      Sends command to another interpreter via Unix socket.
 *
 *----------------------------------------------------------------------
 */

int
Tk_SendObjCmd(
    TCL_UNUSED(ClientData),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    enum { SEND_ASYNC, SEND_DISPLAYOF, SEND_LAST };
    static const char *const sendOptions[] = {
        "-async", "-displayof", "--", NULL
    };

    const char *destName;
    PendingCommand pending;
    RegisteredInterp *riPtr;
    int async = 0, index;
    Tcl_Size i, firstArg;
    Tcl_DString request;
    char *socketPath;

    ThreadSpecificData *tsdPtr = Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    /* Parse options. */
    for (i = 1; i < objc - 1; i++) {
        const char *opt = Tcl_GetString(objv[i]);
        if (opt[0] != '-') {
            break;
        }
        if (Tcl_GetIndexFromObjStruct(interp, objv[i], sendOptions,
                sizeof(char *), "option", 0, &index) != TCL_OK) {
            return TCL_ERROR;
        }
        if (index == SEND_ASYNC) {
            async = 1;
        } else if (index == SEND_DISPLAYOF) {
            i++; /* ignored under Wayland. */
        } else {
            i++;
            break;
        }
    }

    if (objc < i + 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "?-option value ...? interpName arg ?arg ...?");
        return TCL_ERROR;
    }

    destName = Tcl_GetString(objv[i]);
    firstArg = i + 1;

    /* Local send optimization */
    for (riPtr = tsdPtr->interpListPtr; riPtr != NULL; riPtr = riPtr->nextPtr) {
        if (riPtr->name && strcmp(riPtr->name, destName) == 0) {
            Tcl_Preserve(riPtr);
            Tcl_Preserve(riPtr->interp);

            Tcl_Obj *scriptObj = Tcl_ConcatObj(objc - firstArg, objv + firstArg);
            int result = Tcl_EvalObjEx(riPtr->interp, scriptObj, TCL_EVAL_GLOBAL);

            if (interp != riPtr->interp) {
                if (result == TCL_ERROR) {
                    Tcl_AddErrorInfo(interp, Tcl_GetVar2(riPtr->interp,
                            "errorInfo", NULL, TCL_GLOBAL_ONLY));
                    Tcl_SetObjErrorCode(interp, Tcl_GetVar2Ex(riPtr->interp,
                            "errorCode", NULL, TCL_GLOBAL_ONLY));
                }
                Tcl_SetObjResult(interp, Tcl_GetObjResult(riPtr->interp));
                Tcl_ResetResult(riPtr->interp);
            }

            Tcl_Release(riPtr->interp);
            Tcl_Release(riPtr);
            return result;
        }
    }

    /* Remote send. */
    socketPath = GetSocketPathFromRegistry(destName);
    if (socketPath == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("no application named \"%s\"", destName));
        Tcl_SetErrorCode(interp, "TK", "LOOKUP", "APPLICATION", destName, NULL);
        return TCL_ERROR;
    }

    if (!ValidateSocket(socketPath)) {
        RemoveFromRegistry(destName);
        Tcl_Free(socketPath);
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("application \"%s\" is no longer running", destName));
        Tcl_SetErrorCode(interp, "TK", "LOOKUP", "APPLICATION", destName, NULL);
        return TCL_ERROR;
    }

    localData.sendSerial++;

    Tcl_DStringInit(&request);
    Tcl_DStringAppend(&request, "\0c\0-n ", 6);
    Tcl_DStringAppend(&request, destName, -1);

    if (!async) {
        char buf[PATH_MAX + 32];
        char *myPath = GetMySocketPath();
        if (myPath == NULL) {
            Tcl_Free(socketPath);
            Tcl_DStringFree(&request);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("current interpreter not registered", -1));
            return TCL_ERROR;
        }
        snprintf(buf, sizeof(buf), "%s %d", myPath, localData.sendSerial);
        Tcl_DStringAppend(&request, "\0-r ", 4);
        Tcl_DStringAppend(&request, buf, -1);
    }

    Tcl_DStringAppend(&request, "\0-s ", 4);
    for (i = firstArg; i < objc; i++) {
        if (i > firstArg) Tcl_DStringAppend(&request, " ", 1);
        Tcl_DStringAppend(&request, Tcl_GetString(objv[i]), -1);
    }
    Tcl_DStringAppend(&request, "\0", 1);

    if (!async) {
        memset(&pending, 0, sizeof(pending));
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

    if (SendViaSocket(socketPath, Tcl_DStringValue(&request),
                      Tcl_DStringLength(&request) + 1) != 0) {
        Tcl_DStringFree(&request);
        Tcl_Free(socketPath);
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot send to application \"%s\"", destName));
        return TCL_ERROR;
    }

    Tcl_DStringFree(&request);
    Tcl_Free(socketPath);

    if (async) {
        return TCL_OK;
    }

    /* Wait for reply. */
    while (!pending.gotResponse) {
        Tcl_DoOneEvent(TCL_ALL_EVENTS);
    }

    tsdPtr->pendingCommands = pending.nextPtr;

    if (pending.errorInfo) {
        Tcl_AddErrorInfo(interp, pending.errorInfo);
        Tcl_Free(pending.errorInfo);
    }
    if (pending.errorCode) {
        Tcl_SetObjErrorCode(interp, Tcl_NewStringObj(pending.errorCode, -1));
        Tcl_Free(pending.errorCode);
    }
    if (pending.result) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(pending.result, -1));
        Tcl_Free(pending.result);
    }

    return pending.code;
}

/*
 *----------------------------------------------------------------------
 *
 * SocketEventProc --
 *
 *      Handle incoming datagrams on registered socket.
 *
 * Side effects:
 *      Executes commands or stores results for pending sends.
 *
 *----------------------------------------------------------------------
 */

static void
SocketEventProc(ClientData clientData, 
	TCL_UNUSED(int)) /* mask */
{

    RegisteredInterp *riPtr = (RegisteredInterp *)clientData;
    char buffer[65536];
    ssize_t n = recv(riPtr->sockfd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        return;
    }
    buffer[n] = '\0';

    const char *p = buffer;
    while (p - buffer < n) {
        if (*p == '\0') {
            p++;
            continue;
        }

        if (p[0] == 'c' && p[1] == '\0') {
            /* Command received. */
            const char *interpName = NULL, *script = NULL;
            const char *replySocket = NULL, *serial = NULL;
            const char *q = p + 2;

            while (q - buffer < n && *q == '-') {
                if (strncmp(q, "-n ", 3) == 0) interpName  = q + 3;
                if (strncmp(q, "-s ", 3) == 0) script     = q + 3;
                if (strncmp(q, "-r ", 3) == 0) {
                    replySocket = q + 3;
                    while (*q) q++;
                    q++;
                    serial = q;
                }
                while (*q) q++;
                q++;
            }

            if (interpName == NULL || script == NULL ||
                riPtr->name == NULL || strcmp(riPtr->name, interpName) != 0) {
                p = q;
                continue;
            }

            Tcl_Preserve(riPtr->interp);
            int code = Tcl_EvalEx(riPtr->interp, script, -1, TCL_EVAL_GLOBAL);
            const char *resultStr = Tcl_GetString(Tcl_GetObjResult(riPtr->interp));

            if (replySocket != NULL && serial != NULL) {
                Tcl_DString reply;
                Tcl_DStringInit(&reply);

                Tcl_DStringAppend(&reply, "\0r\0-s ", 6);
                Tcl_DStringAppend(&reply, serial, -1);

                if (code != TCL_OK) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%d", code);
                    Tcl_DStringAppend(&reply, "\0-c ", 4);
                    Tcl_DStringAppend(&reply, buf, -1);
                }

                Tcl_DStringAppend(&reply, "\0-r ", 4);
                Tcl_DStringAppend(&reply, resultStr, -1);

                if (code == TCL_ERROR) {
                    const char *ei = Tcl_GetVar2(riPtr->interp, "errorInfo", NULL, TCL_GLOBAL_ONLY);
                    if (ei) {
                        Tcl_DStringAppend(&reply, "\0-i ", 4);
                        Tcl_DStringAppend(&reply, ei, -1);
                    }
                    const char *ec = Tcl_GetVar2(riPtr->interp, "errorCode", NULL, TCL_GLOBAL_ONLY);
                    if (ec) {
                        Tcl_DStringAppend(&reply, "\0-e ", 4);
                        Tcl_DStringAppend(&reply, ec, -1);
                    }
                }

                Tcl_DStringAppend(&reply, "\0", 1);

                SendViaSocket(replySocket, Tcl_DStringValue(&reply),
                              Tcl_DStringLength(&reply) + 1);

                Tcl_DStringFree(&reply);
            }

            Tcl_Release(riPtr->interp);
            p = q;
        }
        else if (p[0] == 'r' && p[1] == '\0') {
            /* Result received. */
            int serial = 0, code = TCL_OK;
            const char *resultStr = NULL, *errorInfo = NULL, *errorCode = NULL;
            int gotSerial = 0;
            const char *q = p + 2;

            while (q - buffer < n && *q == '-') {
                if (strncmp(q, "-s ", 3) == 0) {
                    if (sscanf(q + 3, "%d", &serial) == 1) gotSerial = 1;
                }
                if (strncmp(q, "-c ", 3) == 0) sscanf(q + 3, "%d", &code);
                if (strncmp(q, "-r ", 3) == 0) resultStr  = q + 3;
                if (strncmp(q, "-i ", 3) == 0) errorInfo  = q + 3;
                if (strncmp(q, "-e ", 3) == 0) errorCode  = q + 3;
                while (*q) q++;
                q++;
            }

            if (!gotSerial) {
                p = q;
                continue;
            }

            ThreadSpecificData *tsdPtr = Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
            PendingCommand *pcPtr;
            for (pcPtr = tsdPtr->pendingCommands; pcPtr != NULL; pcPtr = pcPtr->nextPtr) {
                if (pcPtr->serial == serial && pcPtr->result == NULL) {
                    pcPtr->code = code;
                    if (resultStr) {
                        pcPtr->result = Tcl_Alloc(strlen(resultStr) + 1);
                        strcpy(pcPtr->result, resultStr);
                    }
                    if (code == TCL_ERROR) {
                        if (errorInfo) {
                            pcPtr->errorInfo = Tcl_Alloc(strlen(errorInfo) + 1);
                            strcpy(pcPtr->errorInfo, errorInfo);
                        }
                        if (errorCode) {
                            pcPtr->errorCode = Tcl_Alloc(strlen(errorCode) + 1);
                            strcpy(pcPtr->errorCode, errorCode);
                        }
                    }
                    pcPtr->gotResponse = 1;
                    break;
                }
            }

            p = q;
        }
        else {
            while (*p && p - buffer < n) p++;
            p++;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DeleteProc --
 *
 *      Cleanup when "send" command is deleted.
 *
 * Side effects:
 *      Unregisters interpreter, removes socket and registry entry.
 *
 *----------------------------------------------------------------------
 */

static void
DeleteProc(ClientData clientData)
{
    RegisteredInterp *riPtr = (RegisteredInterp *)clientData;
    RegisteredInterp **prevPtr;
    ThreadSpecificData *tsdPtr = Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (riPtr->name) {
        RemoveFromRegistry(riPtr->name);
        Tcl_Free(riPtr->name);
    }

    if (riPtr->sockfd >= 0) {
        Tcl_DeleteFileHandler(riPtr->sockfd);
        close(riPtr->sockfd);
    }

    if (riPtr->socketPath) {
        unlink(riPtr->socketPath);
        Tcl_Free(riPtr->socketPath);
    }

    for (prevPtr = &tsdPtr->interpListPtr; *prevPtr != NULL; prevPtr = &(*prevPtr)->nextPtr) {
        if (*prevPtr == riPtr) {
            *prevPtr = riPtr->nextPtr;
            break;
        }
    }

    riPtr->interp = NULL;
    Tcl_EventuallyFree(riPtr, TCL_DYNAMIC);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetInterpNames --
 *
 *      Return list of all known interpreter names.
 *
 * Results:
 *      TCL_OK with list in interp result.
 *
 *----------------------------------------------------------------------
 */

int
TkGetInterpNames(Tcl_Interp *interp, 
	TCL_UNUSED(Tk_Window)) /* tkwin */
{

    char *registryDir;
    DIR *dir;
    struct dirent *entry;
    Tcl_Obj *listObj = Tcl_NewListObj(0, NULL);

    registryDir = GetRegistryDir();
    if (registryDir == NULL) {
        Tcl_SetObjResult(interp, listObj);
        return TCL_OK;
    }

    dir = opendir(registryDir);
    if (dir == NULL) {
        Tcl_Free(registryDir);
        Tcl_SetObjResult(interp, listObj);
        return TCL_OK;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char *path = GetSocketPathFromRegistry(entry->d_name);
        if (path != NULL) {
            if (ValidateSocket(path)) {
                Tcl_ListObjAppendElement(interp, listObj,
                        Tcl_NewStringObj(entry->d_name, -1));
            } else {
                RemoveFromRegistry(entry->d_name);
            }
            Tcl_Free(path);
        }
    }

    closedir(dir);
    Tcl_Free(registryDir);

    Tcl_SetObjResult(interp, listObj);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkSendCleanup --
 *
 *      Cleanup all send-related resources (called at exit).
 *
 *----------------------------------------------------------------------
 */

void
TkSendCleanup(
	TCL_UNUSED(TkDisplay *))  /*dispPtr */
{
	
    ThreadSpecificData *tsdPtr = Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    RegisteredInterp *riPtr, *next;

    for (riPtr = tsdPtr->interpListPtr; riPtr != NULL; riPtr = next) {
        next = riPtr->nextPtr;

        if (riPtr->sockfd >= 0) {
            Tcl_DeleteFileHandler(riPtr->sockfd);
            close(riPtr->sockfd);
        }

        if (riPtr->socketPath) {
            unlink(riPtr->socketPath);
            Tcl_Free(riPtr->socketPath);
        }

        if (riPtr->name) {
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
 *      Test support command for send internals.
 *
 * Results:
 *      Standard Tcl result.
 *
 *----------------------------------------------------------------------
 */

int
TkpTestsendCmd(
    TCL_UNUSED(ClientData),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    static const char *const options[] = { "serial", NULL };
    int index;

    if (objc < 2 ||
        Tcl_GetIndexFromObjStruct(interp, objv[1], options,
                sizeof(char *), "option", 0, &index) != TCL_OK) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
        return TCL_ERROR;
    }

    if (index == 0) { /* serial */
        Tcl_SetObjResult(interp, Tcl_NewIntObj(localData.sendSerial + 1));
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
