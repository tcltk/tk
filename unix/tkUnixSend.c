/*
 * tkUnixSend.c --
 *
 *	This file provides functions that implement the "send" command,
 *	allowing commands to be passed from interpreter to interpreter.
 *
 * Copyright © 1989-1994 The Regents of the University of California.
 * Copyright © 1994-1996 Sun Microsystems, Inc.
 * Copyright © 1998-1999 Scriptics Corporation.
 * Copyright © 2025 Marc Culler
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkUnixInt.h"
#include <signal.h>
#include <mqueue.h>

/* The realtime signal that we use with mq_notify */
#define TK_MQUEUE_SIGNAL (SIGRTMIN + 0)

/*
 * The following structure is used to keep track of the interpreters
 * registered by this process.
 */

typedef struct RegisteredInterp {
    char *name;                 /* Interpreter's name (malloc'ed).*/
    Tcl_Interp *interp;		/* Interpreter associated with name. NULL
				 * means that the application was unregistered
				 * or deleted while a send was in progress to
				 * it. */
    TkDisplay *dispPtr;		/* Display for the application. Needed because
				 * we may need to unregister the interpreter
				 * after its main window has been deleted. */
    struct RegisteredInterp *nextPtr;
				/* Next in list of names associated with
				 * interps in this process. NULL means end of
				 * list. */
} RegisteredInterp;

/*
 * A registry of all Tk applications owned by the current user is maintained
 * in the file ~/Library/Caches/com.tcltk.appnames. The file contains the
 * string representatikon of a TclDictObj.  The dictionary keys are appname
 * strings and the value assigned to a key is a Tcl list containing two
 * Tcl_IntObj items whose integer values are, respectively, the pid of the
 * process which registered the interpreter and the Tk Window ID of the comm
 * window in that interpreter.
 */

static char *appNameRegistryPath;

/*
 * Information that we record about an application.
 * RegFindName returns a struct of this type.
 */

typedef struct AppInfo {
    pid_t pid;
    void *clientData;
} AppInfo;

/*
 * Construct an AppInfo from a ListObj value of the appNameDict.
 */

static AppInfo
ObjToAppInfo(
    Tcl_Obj *value)
{
    AppInfo result = {0};
    Tcl_Size objc, length;
    Tcl_Obj **objvPtr;
    static const char *failure = "AppName registry is corrupted. "
	                         "Try deleting %s";
    if (Tcl_ListObjLength(NULL, value, &length) != TCL_OK) {
	Tcl_Panic(failure, appNameRegistryPath);
    } else if (Tcl_ListObjGetElements(NULL, value, &objc, &objvPtr) == TCL_OK) {
	if (objc != 2) {
	    Tcl_Panic(failure, appNameRegistryPath);
	}
	Tcl_GetIntFromObj(NULL, objvPtr[0], (int *) &result.pid);
	Tcl_GetLongFromObj(NULL, objvPtr[1], (long *) &result.clientData);
    }
    return result;
}

/*
 * Construct a ListObj value for the appNameDict from an AppInfo.
 */

static Tcl_Obj*
AppInfoToObj(
    AppInfo info)
{
    Tcl_Obj *objv[2] = {Tcl_NewIntObj(info.pid),
			Tcl_NewLongObj(info.clientData)};
    return Tcl_NewListObj(2, objv);
}

/* When the AppName registry is being manipulated by an application (e.g. to
 * add or remove an entry), it is loaded into memory using a structure of the
 * following type:
 */

typedef struct NameRegistry {
    TkDisplay *dispPtr;		/* Display from which the registry was
				 * read. */
    int modified;		/* Non-zero means that the property has been
				 * modified, so it needs to be written out
				 * when the NameRegistry is closed. */
    Tcl_Obj *appNameDict;       /* Tcl dict mapping interpreter names to
				 * a Tcl list {pid, clientData}
				 */
} NameRegistry;

/*
 * The global data in the struct below is stored as thread specific data.
 * This means that the list of registered interpreters is per-thread (not
 * per-process as indicated above).  It is not clear to me that it makes sense
 * for a Tk application (i.e. a Tcl interpreter which has loaded the Tk
 * package) to run in a thread other than the main thread, since such an
 * application would not receive any X events.  However, the unix code has
 * used thread-specific data for a long time.  So I am leaving it that way for
 * the time being.
 */

typedef struct {
    RegisteredInterp *interpListPtr;  /* List of all interpreters process. */
    mqd_t qd;                         /* Descriptor for the mqueue. */
    char qname[NAME_MAX];             /* Path name of mqueue. */
    Tcl_AsyncHandler asyncToken;      /* Token for AsyncHandler. */
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

#define SET_QNAME(qname, pid)					\
    snprintf((qname), NAME_MAX, "/tksend_%d", (pid))

/*
 * Other miscellaneous per-process (not per-thread) data:
 */

static struct {
    int sendSerial;		/* The serial number that was used in the last
				 * "send" command. */
    int sendDebug;		/* This can be set while debugging to 
				 * add print statements, for example. */
    int initialized;
} localData = {0, 0, 0};


/*
 * Declarations of some static functions defined later in this file:
 */

static int		SendInit(Tcl_Interp *interp);
static NameRegistry*	RegOpen(Tcl_Interp *interp, TkDisplay *dispPtr);
static void		RegClose(NameRegistry *regPtr);
static void		RegAddName(NameRegistry *regPtr, const char *name,
                                   void *clientData);
static Tcl_Obj*         loadAppNameRegistry(const char *path);
static void             saveAppNameRegistry(Tcl_Obj *dict, const char *path);
static void		RegDeleteName(NameRegistry *regPtr, const char *name);
static AppInfo		RegFindName(NameRegistry *regPtr, const char *name);
static void             processMessage(void *clientData);
static void             mqueueHandler(int sig, siginfo_t *info, void *ucontext);
static int              mqueueAsyncProc(void *clientData, Tcl_Interp *interp,
					int code);
static Tcl_CmdDeleteProc DeleteProc;

/*
 *--------------------------------------------------------------
 *
 * SendInit --
 *
 *	This function is called to initialize the objects needed
 *	for sending commands and receiving results.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets up various data structures and windows.<
 *
 *--------------------------------------------------------------
 */

/*
 * These are typical default values, but the actual defaults can be configured
 * by the user. We set these values when opening the queue for consistency.
 */

#define TK_MQ_MSGSIZE 8192
#define TK_MQ_MAXMSG 10

static int
SendInit(
    Tcl_Interp *interp)
{
    /*
     * Intialize the path used for the appname registry.
     */

    const char *home = getenv("HOME");
    const char *dir = "/.cache/tksend";
    const char *file = "/appnames";
    appNameRegistryPath = ckalloc(strlen(home) + strlen(dir)
	+ strlen(file) + 1);
    strcpy(appNameRegistryPath, home);
    strcat(appNameRegistryPath, dir);
    mkdir(appNameRegistryPath, S_IRWXU);
    strcat(appNameRegistryPath, file);

    /*
     * Initialize the mqueue used by this thread to receive requests.
     */

    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    tsdPtr->asyncToken = Tcl_AsyncCreate(mqueueAsyncProc, NULL);
    SET_QNAME(tsdPtr->qname, getpid());
    struct mq_attr attr = {
	.mq_maxmsg = TK_MQ_MAXMSG,
	.mq_msgsize = TK_MQ_MSGSIZE,
    };

    /*
     * Open the mqueue. which will remain open until the thread exits.
     */

    tsdPtr->qd = mq_open(tsdPtr->qname, O_RDWR|O_CREAT, 0660, &attr);
    if (tsdPtr->qd == -1) {
	goto error;
    }

    /*
     * Install a signal handler which will use the asyncToken to set a flag
     * that causes the mqueueAsyncProc to be called when it is safe to do so.
     * The mqueueAsyncProc will unpack the message and execute the command
     * in its payload.
     */
    
    // Do we need to worry about the old action?
    // Presumably it is the default action which kills the process.
    struct sigaction oldaction, action = {
	.sa_sigaction = mqueueHandler,
	.sa_flags = SA_SIGINFO
    };
    if (sigaction(TK_MQUEUE_SIGNAL, &action, &oldaction) != 0) {
	goto error;
    }

    /*
     * Request that we be notified with TK_MQUEUE_SIGNAL when a message
     * arrives in our queue.
     */

    struct sigevent se = {
	.sigev_notify = SIGEV_SIGNAL,
	.sigev_signo = TK_MQUEUE_SIGNAL,
	.sigev_value.sival_ptr = (void *) &tsdPtr->qd,
	.sigev_notify_attributes = NULL,
    };
    if (mq_notify(tsdPtr->qd, &se) == -1) {
	goto error;
    }

    localData.initialized = 1;
    return TCL_OK;

error:
    Tcl_SetErrno(errno);
    Tcl_PosixError(interp);
    return TCL_ERROR;
}


/*
 *--------------------------------------------------------------
 *
 * TkSendCleanup --
 *
 *	This function is called to free resources used by the communication
 *	channels for sending commands and receiving results.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees various data structures and windows.
 *
 *--------------------------------------------------------------
 */

void
TkSendCleanup(
    TkDisplay *dispPtr)
{
    (void) dispPtr;  /* Specified in stub table. */
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    if (appNameRegistryPath) {
        ckfree(appNameRegistryPath);
    }
    mq_close(tsdPtr->qd);
    mq_unlink(tsdPtr->qname);
}


/*********************** AppName Registry ***********************/

static Tcl_Obj*
loadAppNameRegistry(
    const char *path)
{
    size_t length, bytesRead;
    char *bytes = NULL;
    Tcl_Obj *result;
    
    FILE *appNameFile = fopen(path, "ab+");
    if (appNameFile == NULL) {
	Tcl_Panic("fopen failed on %s", path);
    }
    if (flock(fileno(appNameFile), LOCK_EX)) {
	Tcl_Panic("flock failed on %s", path);
    }
    /*
     * In macOS, "ab+" sets read and write position at the end.
     * But this is not a posix requirement and does not happen
     * on linux.  So we seek to the end.
     */
    fseek(appNameFile, 0, SEEK_END);
    length = ftell(appNameFile);
    if (length > 0) {
	fseek(appNameFile, 0, SEEK_SET);
	bytes = ckalloc(length);
	bytesRead = fread(bytes, 1, length, appNameFile);
    }
    flock(fileno(appNameFile), LOCK_UN);
    fclose(appNameFile);
    if (length == 0) {
	return Tcl_NewDictObj();
    }
    if (bytesRead != length) {
	Tcl_Panic("read failed on %s: length %lu; read %lu", path,
		length, bytesRead);
    }
    result = Tcl_NewStringObj(bytes, length);
    ckfree(bytes);
    /*
     * Convert the string object to a dict. If that fails the file
     * must be corrupt, so all we can do is return an empty dict.
     */
    Tcl_Size size;
    if (TCL_OK != Tcl_DictObjSize(NULL, result, &size)){
	result = Tcl_NewDictObj();
    }
    return result;
}

static void
saveAppNameRegistry(
    Tcl_Obj *dict,
    const char *path)
{
    Tcl_Size length, bytesWritten;
    /* Open the file ab+ to avoid truncating it before flocking it. */
    FILE *appNameFile = fopen(path, "ab+");
    char *bytes;
    if (appNameFile == NULL) {
	Tcl_Panic("fopen failed on %s", path);
	return;
    }
    if (flock(fileno(appNameFile), LOCK_EX)) {
	Tcl_Panic("flock failed on %s", path);
    }
    /* Now we can truncate the file. */
    if (ftruncate(fileno(appNameFile), 0) != 0) {
	Tcl_Panic("ftruncate failed on %s", path);
    }
    bytes = Tcl_GetStringFromObj(dict, &length);
    bytesWritten = (Tcl_Size) fwrite(bytes, 1, length, appNameFile);
    flock(fileno(appNameFile), LOCK_UN);
    fclose(appNameFile);
    if (bytesWritten != length) {
	Tcl_Panic("write failed on %s: length: %lu wrote: %lu", path,
		length, bytesWritten);
	return;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * RegOpen --
 *
 *	This function loads the name registry for a display into memory so
 *	that it can be manipulated.  It reads a string representation of
 *      a Tcl dict from a file and constructs the dict.
 *
 * Results:
 *	The return value is a pointer to a NameRegistry structure.
 *
 *
 *
 *----------------------------------------------------------------------
 */

static NameRegistry *
RegOpen(
    Tcl_Interp *interp,		/* Interpreter to use for error reporting
				 * (errors cause a panic so in fact no error
				 * is ever returned, but the interpreter is
				 * needed anyway). */
    TkDisplay *dispPtr)		/* Display whose name registry is to be
				 * opened. */
{
    NameRegistry *regPtr;
    regPtr = (NameRegistry *)ckalloc(sizeof(NameRegistry));
    regPtr->dispPtr = dispPtr;
    regPtr->modified = 0;

    /*
     * Deserialize the registry file as a Tcl dict.
     */

    Tcl_Obj *dict = loadAppNameRegistry(appNameRegistryPath);
    if (dict) {
	regPtr->appNameDict = dict;
    } else {
	regPtr->appNameDict = Tcl_NewDictObj();
    }

    /*
     * Find and remove any interpreter name for which the process is no longer
     * running.  This cleans up after a crash of some other wish process.
     */

    Tcl_Size dictSize;
    Tcl_DictObjSize(NULL, regPtr->appNameDict, &dictSize);
    Tcl_Obj **deadinterps = (Tcl_Obj**) ckalloc(dictSize * sizeof(Tcl_Obj*));
    int count = 0;
    Tcl_DictSearch search;
    Tcl_Obj *key, *value;
    int done = 0, i;
    for (Tcl_DictObjFirst(
	  interp, regPtr->appNameDict, &search, &key, &value, &done) ;
	     !done ;
	     Tcl_DictObjNext(&search, &key, &value, &done)) {
	AppInfo info = ObjToAppInfo(value);
	if (kill(info.pid, 0)) {
	    deadinterps[count++] = key;
	}
    }
    for (i = 0; i < count; i++) {
	Tcl_DictObjRemove(NULL, regPtr->appNameDict, deadinterps[i]);
    }
    ckfree(deadinterps);
    return regPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * RegClose --
 *
 *	This function is called to end a series of operations on a name
 *	registry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *      The registry is written back if it has been modified, Memory for the
 *	registry is freed, so the caller should never use regPtr again.
 *
 *----------------------------------------------------------------------
 */

static void
RegClose(
    NameRegistry *regPtr)	/* Pointer to a registry opened with a
				 * previous call to RegOpen. */
{
    saveAppNameRegistry(regPtr->appNameDict, appNameRegistryPath);
    ckfree(regPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * RegFindName --
 *
 *	Given an open name registry, this function finds an entry with a given
 *	name, if there is one, and returns information about that entry.
 *
 * Results:
 *      The return value is an AppInfo struct containing the pid and the X
 *	identifier for the comm window for the application named "name", or
 *	nil if there is no such entry in the registry.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

AppInfo
RegFindName(
    NameRegistry *regPtr,	/* Pointer to a registry opened with a
				 * previous call to RegOpen. */
    const char *name)		/* Name of an application. */
{
    Tcl_Obj *valuePtr = NULL, *keyPtr = Tcl_NewStringObj(name, TCL_INDEX_NONE);
    Tcl_DictObjGet(NULL, regPtr->appNameDict, keyPtr, &valuePtr);
    // Maybe using pid 0 as the default is a bad idea?
    AppInfo resultTcl = {0};
    if (valuePtr) {
	resultTcl = ObjToAppInfo(valuePtr);
    }
    return resultTcl;
}


/*
 *----------------------------------------------------------------------
 *
 * RegDeleteName --
 *
 *	This function deletes the entry for a given name from an open
 *	registry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If there used to be an entry named "name" in the registry, then it is
 *	deleted and the registry is marked as modified so it will be written
 *	back when closed.
 *
 *----------------------------------------------------------------------
 */

static void
RegDeleteName(
    NameRegistry *regPtr,	/* Pointer to a registry opened with a
				 * previous call to RegOpen. */
    const char *name)		/* Name of an application. */
{
    Tcl_Obj *keyPtr = Tcl_NewStringObj(name, TCL_INDEX_NONE);
    Tcl_DictObjRemove(NULL, regPtr->appNameDict, keyPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * RegAddName --
 *
 *	Add a new entry to an open registry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The open registry is expanded; it is marked as modified so that it
 *	will be written back when closed.
 *
 *----------------------------------------------------------------------
 */

static void
RegAddName(
    NameRegistry *regPtr,	/* Pointer to a registry opened with a
				 * previous call to RegOpen. */
    const char *name,		/* Name of an application. The caller must
				 * ensure that this name isn't already
				 * registered. */
    void *clientData)           /* pointer to arbitrary data */
{
    Tcl_Obj *keyPtr = Tcl_NewStringObj(name, TCL_INDEX_NONE);
    AppInfo valueTcl = {getpid(), clientData};
    Tcl_Obj *valuePtr = AppInfoToObj(valueTcl);
    Tcl_DictObjPut(NULL, regPtr->appNameDict, keyPtr, valuePtr);
    regPtr->modified = 1;
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
 *	The return value is the name actually given to the application. This
 *	will normally be the same as name, but if name was already in use for
 *	an application then a name of the form "name #2" will be chosen, with
 *	a high enough number to make the name unique.
 *
 * Side effects:
 *	Registration info is saved, thereby allowing the "send" command to be
 *	used later to invoke commands in the application. In addition, the
 *	"send" command is created in the application's interpreter. The
 *	registration will be removed automatically if the interpreter is
 *	deleted or the "send" command is removed.
 *
 *----------------------------------------------------------------------
 */

const char *
Tk_SetAppName(
    Tk_Window tkwin,		/* Token for any window in the application to
				 * be named: it is just used to identify the
				 * application and the display. */
    const char *name)		/* The name that will be used to refer to the
				 * interpreter in later "send" commands. Must
				 * be globally unique. */
{
    RegisteredInterp *riPtr;
    TkWindow *winPtr = (TkWindow *) tkwin;
    NameRegistry *regPtr;
    Tcl_Interp *interp;
    const char *actualName;
    Tcl_DString dString;
    int offset, i;
    interp = winPtr->mainPtr->interp;
    if (!localData.initialized) {
	SendInit(interp);
    }
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    /*
     * See if the application is already registered; if so, remove its current
     * name from the registry.
     */

    regPtr = RegOpen(interp, winPtr->dispPtr);
    for (riPtr = tsdPtr->interpListPtr; ; riPtr = riPtr->nextPtr) {
	if (riPtr == NULL) {
	    /*
	     * This interpreter isn't currently registered; create the data
	     * structure that will be used to register it locally, plus add
	     * the "send" command to the interpreter.
	     */

	    riPtr = (RegisteredInterp *)ckalloc(sizeof(RegisteredInterp));
	    riPtr->interp = interp;
	    riPtr->dispPtr = winPtr->dispPtr;
	    riPtr->nextPtr = tsdPtr->interpListPtr;
	    tsdPtr->interpListPtr = riPtr;
	    riPtr->name = NULL;
	    Tcl_CreateObjCommand2(interp, "send", Tk_SendObjCmd, riPtr,
				  DeleteProc);
	    if (Tcl_IsSafe(interp)) {
		Tcl_HideCommand(interp, "send", "send");
	    }
	    break;
	}
	if (riPtr->interp == interp) {
	    /*
	     * The interpreter is currently registered; remove it from the
	     * name registry.
	     */

	    if (riPtr->name) {
		RegDeleteName(regPtr, riPtr->name);
		ckfree(riPtr->name);
	    }
	    break;
	}
    }

    /*
     * Pick a name to use for the application. Use "name" if it's not already
     * in use. Otherwise add a suffix such as " #2", trying larger and larger
     * numbers until we eventually find one that is unique.
     */

    actualName = name;
    offset = 0;				/* Needed only to avoid "used before
					 * set" compiler warnings. */
    for (i = 1; ; i++) {
	if (i > 1) {
	    if (i == 2) {
		Tcl_DStringInit(&dString);
		Tcl_DStringAppend(&dString, name, TCL_INDEX_NONE);
		Tcl_DStringAppend(&dString, " #", 2);
		offset = Tcl_DStringLength(&dString);
		Tcl_DStringSetLength(&dString, offset+TCL_INTEGER_SPACE);
		actualName = Tcl_DStringValue(&dString);
	    }
	    snprintf(Tcl_DStringValue(&dString) + offset, TCL_INTEGER_SPACE,
		     "%d", i);
	}
	AppInfo info = RegFindName(regPtr, actualName);
	if (info.pid == 0) {
	    break;
	}
    }

    /*
     * We've now got a name to use. Store it in the name registry and in the
     * local entry for this application.
     */

    RegAddName(regPtr, actualName, NULL);
    RegClose(regPtr);
    riPtr->name = (char *)ckalloc(strlen(actualName) + 1);
    strcpy(riPtr->name, actualName);
    if (actualName != name) {
	Tcl_DStringFree(&dString);
    }
    return riPtr->name;
}

/*************************** MQueue Interface ****************************/

/*
 * Our mqueue messages consist of a header followed by a payload, as described
 * by the following struct.  The payload is a byte sequence containing a
 * concatenation of null-terminated C strings, the number of strings being
 * specified by the count field in the header.  The strings are preceded in
 * the payload by an array of size_t values specifying the size of each
 * string, including its null terminator.
 */

typedef struct message {
    int serial;      /* serial number */
    int code;        /* only used for replys */
    int flags;       /* See below.    */
    int count;       /* Number of strings in the payload. */
    char payload[];
} message;

/*
 * The payload byte sequence can have two different forms.
 *
 * Usually the payload consists of an array of longs, specifying the sizes of
 * each of the strings, followed by the concatenation of the strings. The
 * size_t integers are serialized in the endian order of the host system, so
 * they may be deserialized by simply calling memcpy.
 *
 * A second format is intended to deal with the issue that mqueue messages
 * have a limited size, typically 8192 bytes, which might not be large enough.
 * To deal with the size limit, the payload strings need not be embedded in
 * the message as in the typical case.  Instead the message payload can
 * contain a single string which is the path to a temporary file containing
 * the actual payload strings in the format described above.  Since the path
 * is a null-terminated string, no size data is included in the message in
 * this case.
 */

/*
 * Flags:
 *
 * The flag bit PAYLOAD_IS_PATH indicates which of the two payload formats is
 * being used.  The flag bit MESSAGE_IS_REQUEST indicates whether the message
 * is a request, containing a command, or a reply, containing the result
 * of evalutating a command.
 */

#define PAYLOAD_IS_PATH    1
#define MESSAGE_IS_REQUEST 2

/*
 *----------------------------------------------------------------------
 * packMessage --
 *
 *     Creates a message with a payload consisting of an array of strCount
 *     null-terminated strings.  The message serial number is taken from
 *     the localData.  If the message size would exceed the maximum, the payload
 *     is stored in a temporary file, the PAYLOAD_IS_PATH flag is set, and the
 *     payload is replaced by an absolute path to the temporary file.
 *
 *     Results:
 *         Returns a pointer to a ckalloc'ed message with payload.
 *
 *     Side effects:
 
 *         The size of the message is stored in the size_t referenced by
 *         sizePtr.  A temporary file containing the payload may be created.
 *----------------------------------------------------------------------
 */

static message *packMessage (
    int code,
    int strCount,
    const char **strArray,
    size_t *sizePtr)
{
    int i;
    char *p;
    message *result;
    size_t sizesSize = strCount * sizeof(size_t);
    size_t payloadSize = sizesSize;
    size_t *sizes = (size_t *) ckalloc(sizesSize);
    message header = {
	.serial = localData.sendSerial,
	.code = code,
	.count = strCount
    };
    for(i = 0 ; i < strCount ; i++) {
	payloadSize += (sizes[i] = strlen(strArray[i]) + 1);
    }
    if (payloadSize + sizeof(message) > TK_MQ_MSGSIZE) {
	char tempName[] = "/tmp/tksend_XXXXXX";
	int tempNameSize = strlen(tempName) + 1;
	int fd = mkstemp(tempName);
	FILE *tempFile = fdopen(fd, "w"); 
	header.flags |= PAYLOAD_IS_PATH;
	fwrite((char *) sizes, 1, sizesSize, tempFile);
	for(i = 0 ; i < strCount ; i++) {
	    fwrite(strArray[i], 1, sizes[i], tempFile);
	}
	fclose(tempFile);
	p = ckalloc(sizeof(message) + tempNameSize);
	*sizePtr = sizeof(message) + tempNameSize;
	result = (message*) p;
	p = (char *) memcpy(p, (char *) &header, sizeof(message)) +
	            sizeof(message);
	memcpy(p, tempName, tempNameSize);
	ckfree(p);
    } else {
	p = ckalloc(sizeof(message) + payloadSize);
	*sizePtr = sizeof(message) + payloadSize;
	result = (message*) p;
	p = (char *) memcpy(p, (char *)&header, sizeof(message));
	p += sizeof(message);
	p = (char *) memcpy(p, sizes, sizesSize);
	p += sizesSize;
	for(i = 0 ; i < strCount ; i++) {
	    p = stpncpy(p, strArray[i], sizes[i]) + 1;
	}
    }
    if (sizes) {
	ckfree(sizes);
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 * unpackMessage --
 *
 *     Extracts the header and the array of strings stored in the payload from
 *     a message.  The number of strings and the message serial number can be
 *     found in the header.
 *
 *     Results:
 *         None 
 *
 *     Side effects:
 *         Allocates memory for an array of char pointers and for the strings
 *         whose pointers are in the array.  If the message payload was stored
 *         in a temporary file, that file will be unlinked.
 *         -------------------------------------------------------------------
 */

static void unpackMessage(
    message *msgPtr,
    message *header,
    char ***strArrayPtr)
{
    message *msgPtr2 = NULL;
    char *p;
    size_t *sizes;
    int strCount = msgPtr->count;
    memcpy((char *) header, (char *) msgPtr, sizeof(message));
    *strArrayPtr = (char **) ckalloc(strCount * sizeof(char *));
    char **strArray = *strArrayPtr;
    if (msgPtr->flags & PAYLOAD_IS_PATH) {
	FILE *payloadFile = fopen(msgPtr->payload, "r");
	fseek(payloadFile, 0, SEEK_END);
	size_t payloadSize = ftell(payloadFile);
	fseek(payloadFile, 0, SEEK_SET);
	msgPtr2 = (message *) ckalloc(sizeof(message) + payloadSize);
	memcpy((char *) msgPtr2, msgPtr, sizeof(message));
	size_t bytes_read = fread(msgPtr2->payload, 1, payloadSize,
				  payloadFile);
	if (bytes_read != payloadSize) {
	    Tcl_Panic("Read failed: %ld bytes of %ld read.",
		     bytes_read, payloadSize);
	}
	unlink(msgPtr->payload);
	sizes = (size_t *) msgPtr2->payload;
	p = msgPtr2->payload + strCount * sizeof(size_t);
    } else {
	sizes = (size_t *) msgPtr->payload;
	p = msgPtr->payload + strCount * sizeof(size_t);
    }
    for (int i = 0 ; i < strCount ; i++) {
	strArray[i] = ckalloc(sizes[i]);
	strncpy(strArray[i], p, sizes[i]);
	p += sizes[i];
    }
    if (msgPtr2) {
	ckfree(msgPtr2);
    }
}

/*
 *--------------------------------------------------------------
 *
 * sendRequest --
 *--------------------------------------------------------------
 */

static int
sendRequest(
    Tcl_Interp *interp,    /* interp running the send command */
    int pid,               /* pid of recipient process */
    const char *sender,    /* appName of sender; "" for async requests */
    const char *recipient, /* appName of recipient; must not be NULL */
    const char *request)   /* command to be evaluated by recipient */
{
    unsigned int priority = 1; /* Do we need different priorities? */
    int async = (sender[0] == '\0');
    /* Open the recipient message queue. */
    char qname[NAME_MAX];
    char qnameReply[NAME_MAX];
    SET_QNAME(qname, pid);
    mqd_t qd = mq_open(qname, O_RDWR, 0, NULL), qdReply;
    if (qd == -1) {
	goto error;
    }
    const char *strings[3] = {NULL, recipient, request};
    if (!async) {
	snprintf(qnameReply, NAME_MAX, "/tkreply_%s", sender);
	strings[0] = qnameReply;
    } else {
	strings[0] = sender;
    }
    size_t messageSize;
    message *m = packMessage(0, 3, strings, &messageSize);
    m->flags |= MESSAGE_IS_REQUEST;
    int status = mq_send(qd, (char *) m, messageSize, priority);
    ckfree(m);
    mq_close(qd);
    if (status == -1) {
	// This does not mean that the message was not sent.
	// What should we do about this?  We want to report an
	// error if the recipient dies.  But what about the
	// "interruped system call"?  Google says to retry
	// when EINTR is raised until it succeeds.  But it
	// seems to succeed even though EINTR is set.  Maybe
	// the issue is with some other mq call?
	goto error;
    }
    if (async) {
	return TCL_OK;
    }
    /* Open a new message queue for the reply. */
    struct mq_attr attr = {
	.mq_maxmsg = TK_MQ_MAXMSG,
	.mq_msgsize = TK_MQ_MSGSIZE,
    };

    struct timespec abs_timeout;
    if (clock_gettime(CLOCK_REALTIME, &abs_timeout) == -1) {
	goto error;
    }
    abs_timeout.tv_sec += 1;
    qdReply = mq_open(qnameReply, O_RDWR|O_CREAT, 0660, &attr);
    if (qdReply == -1) {
	goto error;
    }
    char msg[TK_MQ_MSGSIZE];
    
    // TODO
    // This is incomplete.
    // The code belose waits 1 second for a response to the request and gives
    // up (after checking whether the recipient process is still running.  We
    // need to deal with the possibility of sending a long-running command to
    // another application.  How long should we wait?  How should we interrupt
    // the other process if it is taking too long?

    status = mq_timedreceive(qdReply, msg, TK_MQ_MSGSIZE, NULL,
				 &abs_timeout);
    if (errno == ETIMEDOUT) {
	goto error;
    }
    if (status == -1) {
	if (kill(pid, 0)) {
	    Tcl_AddErrorInfo(interp, "Target application died.");
	}
	goto error;
    }
    message header;
    char **replyStrings;
    unpackMessage((message *) msg, &header, &replyStrings);
    if (mq_close(qdReply) == -1 || mq_unlink(qnameReply) == -1) {
	goto error;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(replyStrings[0],
	TCL_INDEX_NONE));
    return TCL_OK;
error:
    Tcl_SetErrno(errno);
    Tcl_PosixError(interp);
    return TCL_ERROR;
}

/*
 *--------------------------------------------------------------
 *
 * processMessage --
 *
 * Called by mqueueAsyncProc to process one message.  The message memory is
 * allocated by mqueueHandler, and freed by this task.
 * 
 *--------------------------------------------------------------
 */

enum requestParts {
    requestSender = 0,
    requestRecipient,
    requestCommand};

static void processMessage(
    void *clientData)
{
    message *msgPtr = (message *) clientData;
    message header;
    char **strings;
    RegisteredInterp *riPtr;
    int async = 0, code;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    unpackMessage(msgPtr, &header, &strings);
    ckfree(msgPtr);
    if (strings[requestSender][0] == 0) {
	async = 1;
    }
    if (header.flags & MESSAGE_IS_REQUEST) {

	/*
	 * Locate the application, then execute the script with its
	 * interpreter.
	 */

	for (riPtr = tsdPtr->interpListPtr ; ; riPtr = riPtr->nextPtr) {
	    if (riPtr == NULL) {
		/* This should be unreachable. */
		return;
	    }
	    if (strcmp(riPtr->name, strings[requestRecipient]) == 0) {
		break;
	    }
	}
	Tcl_Preserve(riPtr);
	code = Tcl_EvalEx(riPtr->interp, strings[requestCommand],
				TCL_INDEX_NONE, TCL_EVAL_GLOBAL);
	if (!async) {
	    /* Send a reply */
	    Tcl_Size resultLength;
	    const char *resultString = Tcl_GetStringFromObj(
		Tcl_GetObjResult(riPtr->interp), &resultLength);
	    size_t messageSize;
	    message *m = packMessage(code, 1, &resultString, &messageSize);
	    mqd_t qd = mq_open(strings[requestSender], O_RDWR);
	    if (qd == -1) {
		Tcl_SetErrno(errno);
		Tcl_PosixError(riPtr->interp);
	    }
	    int status = mq_send(qd, (char *) m, messageSize, 1);
	    mq_close(qd);
	    ckfree(m);
	    if (status == -1) {
		// Does this have any effect here?
		Tcl_SetErrno(errno);
		Tcl_PosixError(riPtr->interp);
	    }
	}
    }
    for (int i = 0 ; i < header.count ; i++) {
	ckfree(strings[i]);
    }
    ckfree(strings);
}

/*
 * Tcl_AsyncProc to use with the mqueue realtime signal.
 */

static int mqueueAsyncProc(
    void *clientData,
    Tcl_Interp *interp,
    int code)
{
    (void) clientData;
    (void) interp;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    unsigned int priority;
    struct mq_attr attr;
    message *msgPtr = NULL;

    /*
     * Process messages until the queue is empty.
     */

    while(True) {
	if (mq_getattr(tsdPtr->qd, &attr) == -1) {
	    // Can we handle this error?  Can we find an interp?
	    //Tcl_SetErrno(errno);
	    //Tcl_PosixError(riPtr->interp);
	    break;
	}
	if (attr.mq_curmsgs == 0) {
	    break;
	}
	msgPtr = (message *) ckalloc(attr.mq_msgsize);
	mq_receive(tsdPtr->qd, (char *) msgPtr, attr.mq_msgsize,
		   &priority);
	processMessage((void *) msgPtr);
    }
    return code;
}

/*
 *--------------------------------------------------------------
 *
 * mqueueHandler --
 *
 * This is a signal handler for the real-time signal generated by
 * the mqueue notification system.
 *
 * According to man 7 signal-safety, a signal handler can only
 * call async-signal-safe functions, which in particular must be
 * reentrant.  This handler calls:
 *     Tcl_GetThreadSpecificData
 *     mq_notify
 *     mq_getattr
 *     mq_receive
 *     ck_alloc
 *     Tcl_DoWhenIdle
 *
 *--------------------------------------------------------------
 */

static void mqueueHandler(
    int sig,
    siginfo_t *info,
    void *ucontext)
{
    (void) sig;
    (void) ucontext;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    if (!Tcl_AsyncMarkFromSignal(tsdPtr->asyncToken, TK_MQUEUE_SIGNAL)) {
	fprintf(stderr, "Tcl_GetThreadData returned false!!!");
    }
    mqd_t *qdPtr = (mqd_t*) info->si_value.sival_ptr;
    mqd_t qd = *qdPtr;

    /*
     * The current notification registration will be canceled when
     * this function returns.  The man page recommends renewing 
     * the registration before emptying the queue, as another process
     * is allowed to register as soon as the queue becomes empty.
     */
    
    struct sigevent se = {
	.sigev_notify = SIGEV_SIGNAL,
	.sigev_signo = TK_MQUEUE_SIGNAL,
	.sigev_value.sival_ptr = (void *) &tsdPtr->qd,
	.sigev_notify_attributes = NULL,
    };
    if (mq_notify(qd, &se) == -1) {
	// Can we handle this error?
	return;
    }
}


/*
 *--------------------------------------------------------------
 *
 * Tk_SendObjCmd --
 *
 *	This function is invoked to process the "send" Tcl command. See the
 *	user documentation for details on what it does.
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
    TCL_UNUSED(void *),	        /* Information about sender */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,		/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument strings. */
{
    enum {
	SEND_ASYNC, SEND_DISPLAYOF, SEND_LAST
    };
    static const char *const sendOptions[] = {
	"-async",   "-displayof",   "--",  NULL
    };
    const char *stringRep, *destName;
    int code = TCL_OK;
    TkWindow *winPtr;
    RegisteredInterp *riPtr;
    int result, async, i, firstArg, index;
    TkDisplay *dispPtr;
    NameRegistry *regPtr;
    Tcl_DString request, request2;
    Tcl_Interp *localInterp;	/* Used when the interpreter to send the
				 * command to is within the same process. */
    /*
     * Process options, if any.
     */

    async = 0;
    winPtr = (TkWindow *) Tk_MainWindow(interp);
    if (winPtr == NULL) {
	return TCL_ERROR;
    }

    /*
     * Process the command options.
     */

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
		winPtr = (TkWindow *) Tk_NameToWindow(interp,
			 Tcl_GetString(objv[++i]),
			(Tk_Window) winPtr);
		if (winPtr == NULL) {
		    return TCL_ERROR;
		}
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
    firstArg = i+1;

    dispPtr = winPtr->dispPtr;
    /*
     * See if the target interpreter is local. If so, execute the command
     * directly without sending messages. The only tricky thing is
     * passing the result from the target interpreter to the invoking
     * interpreter. Watch out: they could be the same!
     */
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    for (riPtr = tsdPtr->interpListPtr; riPtr != NULL; riPtr = riPtr->nextPtr) {
	if ((riPtr->dispPtr != dispPtr) ||
	    (strcmp(riPtr->name, destName) != 0)) {
	    continue;
	}
	/* We have found our target interpreter */
	Tcl_Preserve(riPtr);
	localInterp = riPtr->interp;
	Tcl_Preserve(localInterp);
	if (firstArg == (objc - 1)) {
	    result = Tcl_EvalEx(localInterp, Tcl_GetString(objv[firstArg]),
				TCL_INDEX_NONE, TCL_EVAL_GLOBAL);
	} else {
	    Tcl_DStringInit(&request);
	    Tcl_DStringAppend(&request, Tcl_GetString(objv[firstArg]),
			      TCL_INDEX_NONE);
	    for (i = firstArg+1; i < objc; i++) {
		Tcl_DStringAppend(&request, " ", 1);
		Tcl_DStringAppend(&request, Tcl_GetString(objv[i]),
				  TCL_INDEX_NONE);
	    }
	    result = Tcl_EvalEx(localInterp, Tcl_DStringValue(&request),
				TCL_INDEX_NONE, TCL_EVAL_GLOBAL);
	    Tcl_DStringFree(&request);

	}
	if (interp != localInterp) {
	    if (result == TCL_ERROR) {
		Tcl_Obj *errorObjPtr;

		/*
		 * An error occurred, so transfer error information from the
		 * destination interpreter back to our interpreter. Must clear
		 * interp's result before calling Tcl_AddErrorInfo, since
		 * Tcl_AddErrorInfo will store the interp's result in
		 * errorInfo before appending riPtr's $errorInfo; we've
		 * already got everything we need in riPtr's $errorInfo.
		 */

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

    /*
     * We are targetng an interpreter in another process.  First make sure the
     * interpreter is registered.
     */

    regPtr = RegOpen(interp, winPtr->dispPtr);
    AppInfo info = RegFindName(regPtr, destName);
    RegClose(regPtr);

    if (info.pid == 0 && info.clientData == NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"no application named \"%s\"", destName));
	Tcl_SetErrorCode(interp, "TK", "LOOKUP", "APPLICATION", destName,
		NULL);
	return TCL_ERROR;
    }

    /*
     * Send the command with args to the non-local target interpreter
     */

    Tcl_DStringInit(&request2);
    Tcl_DStringAppend(&request2, Tcl_GetString(objv[firstArg]),
		      TCL_INDEX_NONE);
    if (firstArg < objc - 1) {
	for (i = firstArg+1; i < objc; i++) {
	    Tcl_DStringAppend(&request2, " ", 1);
	    Tcl_DStringAppend(&request2, Tcl_GetString(objv[i]),
			      TCL_INDEX_NONE);
	}
    }

     // When async is 0, the call below blocks until a reply is received.
     // Perhaps we should run a background thread to process timer events?
    char *replyName = NULL;
    if (async) {	
	code = sendRequest(interp, info.pid, "", (const char*) destName,
			       Tcl_DStringValue(&request2));
    } else {
	/* Find the appName of the sending interpreter */
	for (riPtr = tsdPtr->interpListPtr; riPtr != NULL;
	     riPtr = riPtr->nextPtr) {
	    if (riPtr->interp == winPtr->mainPtr->interp) {
		replyName = riPtr->name;
		break;
	    }
	}
	code = sendRequest(interp, info.pid, replyName, destName,
			       Tcl_DStringValue(&request2));
    }
    if (code != TCL_OK) {
	// XXXX
	// With send wish9.1 {send {wish9.1 #2} puts hello}
	// we will end up here because the mq_send raised EINTR
	// meaning that an interrupt occurred while sending the
	// message.  It does not seem to mean that the message
	// was not sent, however.  The send command above works.
	if (replyName) {
	    /*
	     * If the send failed, make sure we don't leave the
	     * reply queue hanging around.
	     */
	    char qname[NAME_MAX];
	    snprintf(qname, NAME_MAX, "/tkreply_%s", replyName);
	    if (mq_unlink(qname)) {
		perror("mq_unlink");
	    }
	}
    }
    Tcl_DStringFree(&request2);
    localData.sendSerial++;
    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * TkGetInterpNames --
 *
 *	This function is invoked to fetch a list of all the interpreter names
 *	currently registered for the display of a particular window.
 *
 * Results:
 *	A standard Tcl return value. The interp's result will be set to hold a
 *	list of all the interpreter names defined for tkwin's display. If an
 *	error occurs, then TCL_ERROR is returned and the interp's result will
 *	hold an error message.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
TkGetInterpNames(
    Tcl_Interp *interp,		/* Interpreter for returning a result. */
    Tk_Window tkwin)		/* Window whose display is to be used for the
				 * lookup. */
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    NameRegistry *regPtr;
    Tcl_Obj *resultObj = Tcl_NewObj();
    regPtr = RegOpen(interp, winPtr->dispPtr);
    Tcl_DictSearch search;
    Tcl_Obj *keyTcl, *value;
    int done = 0;
    for (Tcl_DictObjFirst(
	  interp, regPtr->appNameDict, &search, &keyTcl, &value, &done) ;
	     !done ;
	     Tcl_DictObjNext(&search, &keyTcl, &value, &done)) {
	Tcl_ListObjAppendElement(NULL, resultObj, keyTcl);
    }
    RegClose(regPtr);
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}


/*
 *--------------------------------------------------------------
 *
 * DeleteProc --
 *
 *	This function is invoked by Tcl when the "send" command is deleted in
 *	an interpreter. It unregisters the interpreter.
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
DeleteProc(
    void *clientData)	/* Info about registration */
{
    RegisteredInterp *riPtr = (RegisteredInterp *)clientData;
    RegisteredInterp *riPtr2;
    NameRegistry *regPtr;

    regPtr = RegOpen(riPtr->interp, riPtr->dispPtr);
    RegDeleteName(regPtr, riPtr->name);
    RegClose(regPtr);
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

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
    ckfree(riPtr->name);
    riPtr->interp = NULL;
    Tcl_EventuallyFree(riPtr, TCL_DYNAMIC);
}


/*
 *----------------------------------------------------------------------
 *
 * TkpTestsendCmd --
 *
 *	This function implements the "testsend" command. It provides a set of
 *	functions for testing the "send" command and support function in
 *	tkSend.c.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Depends on option; see below.
 *
 *----------------------------------------------------------------------
 */

// We are not ready for the TestSendCmd yet.  The unix code is
// below.  Much of it seems to involve inspecting X properties
// which aren't being used here.
#if 0

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
    TkWindow *winPtr = (TkWindow *)clientData;
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
    if (index == TESTSEND_BOGUS) {
	handler = Tk_CreateErrorHandler(winPtr->dispPtr->display, -1, -1, -1,
		NULL, NULL);
	Tk_DeleteErrorHandler(handler);
    } else if (index == TESTSEND_PROP) {
	int result, actualFormat;
	unsigned long length, bytesAfter;
	Atom actualType, propName;
	char *property, **propertyPtr = &property, *p, *end;
	Window w;

	if ((objc != 4) && (objc != 5)) {
		Tcl_WrongNumArgs(interp, 1, objv,
			"prop window name ?value ?");
	    return TCL_ERROR;
	}
	if (strcmp(Tcl_GetString(objv[2]), "root") == 0) {
	    w = RootWindow(winPtr->dispPtr->display, 0);
	} else if (strcmp(Tcl_GetString(objv[2]), "comm") == 0) {
	    w = Tk_WindowId(winPtr->dispPtr->commTkwin);
	} else {
	    w = strtoul(Tcl_GetString(objv[2]), &end, 0);
	}
	propName = Tk_InternAtom((Tk_Window) winPtr, Tcl_GetString(objv[3]));
	if (objc == 4) {
	    property = NULL;
	    result = GetWindowProperty(winPtr->dispPtr->display, w, propName,
		    0, 100000, False, XA_STRING, &actualType, &actualFormat,
		    &length, &bytesAfter, (unsigned char **) propertyPtr);
	    if ((result == Success) && (actualType != None)
		    && (actualFormat == 8) && (actualType == XA_STRING)) {
		for (p = property; (unsigned long)(p-property) < length; p++) {
		    if (*p == 0) {
			*p = '\n';
		    }
		}
		Tcl_SetObjResult(interp, Tcl_NewStringObj(property, TCL_INDEX_NONE));
	    }
	    if (property != NULL) {
		XFree(property);
	    }
	} else if (Tcl_GetString(objv[4])[0] == 0) {
	    handler = Tk_CreateErrorHandler(winPtr->dispPtr->display,
		    -1, -1, -1, NULL, NULL);
	    DeleteProperty(winPtr->dispPtr->display, w, propName);
	    Tk_DeleteErrorHandler(handler);
	} else {
	    Tcl_DString tmp;

	    Tcl_DStringInit(&tmp);
	    for (p = Tcl_DStringAppend(&tmp, Tcl_GetString(objv[4]),
		    (int) strlen(Tcl_GetString(objv[4]))); *p != 0; p++) {
		if (*p == '\n') {
		    *p = 0;
		}
	    }
	    handler = Tk_CreateErrorHandler(winPtr->dispPtr->display,
		    -1, -1, -1, NULL, NULL);
	    ChangeProperty(winPtr->dispPtr->display, w, propName, XA_STRING,
		    8, PropModeReplace, (unsigned char*)Tcl_DStringValue(&tmp),
		    p-Tcl_DStringValue(&tmp));
	    Tk_DeleteErrorHandler(handler);
	    Tcl_DStringFree(&tmp);
	}
    } else if (index == TESTSEND_SERIAL) {
	Tcl_SetObjResult(interp, Tcl_NewWideIntObj(localData.sendSerial+1));
    }
    return TCL_OK;
}
#endif

int
TkpTestsendCmd(
    void *clientData,	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])		/* Argument strings. */
{
    (void) clientData;
    (void) interp;
    (void) objc;
    (void) objv;
    return TCL_OK;
}


/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * c-file-offsets: ((arglist-cont . 0))
 * fill-column: 78
 * End:
 */
