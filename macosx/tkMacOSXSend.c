/*
 * tkMacOSXSend.c --
 *
 * This file implements the "send" command, which allows commands to be passed
 * from interpreter to interpreter.  This implementation uses Apple for
 * interprocess communication, in place of the XProperties in the original
 * unix implementation.
 *
 * Copyright © 1989-1994 The Regents of the University of California.
 * Copyright © 1994-1996 Sun Microsystems, Inc.
 * Copyright © 1998-1999 Scriptics Corporation.
 * Copyright © 2025 Marc Culler
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXInt.h"

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
    struct RegisteredInterp *nextPtr;
				/* Next in list of names associated with
				 * interps in this process. NULL means end of
				 * list. */
} RegisteredInterp;

/*
 * A registry of all interpreters owned by the current user is maintained in
 * the file ~/Library/Caches/com.tcltk.appnames. The file contains the string
 * representation of a TclDictObj.  The dictionary keys are appname strings
 * and the value assigned to a key is a Tcl list containing two Tcl_IntObj
 * items whose integer values are, respectively, the pid of the process which
 * registered the interpreter and a currently unused void *.
 */

static char *appNameRegistryPath = "/private/tmp/tkappnames";

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
    Tcl_Size objc;
    Tcl_Obj **objvPtr;
    static const char *failure = "AppName registry is corrupted.  Try deleting %s";
    if (TCL_OK != Tcl_ConvertToType(NULL, value, Tcl_GetObjType("list"))) {
	Tcl_Panic(failure, appNameRegistryPath);
    } else if (Tcl_ListObjGetElements(NULL, value, &objc, &objvPtr) == TCL_OK) {
	if (objc != 2) {
	    Tcl_Panic(failure, appNameRegistryPath);
	}
	Tcl_GetIntFromObj(NULL, objvPtr[0], &result.pid);
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
    int modified;		/* Non-zero means that the property has been
				 * modified, so it needs to be written out
				 * when the NameRegistry is closed. */
    Tcl_Obj *appNameDict;       /* Tcl dict mapping interpreter names to
				 * a Tcl list {pid, clientData}
				 */
} NameRegistry;

/*
 * When sending to a different process we use the AppleEvent DoScript handler
 * to evaluate the command in the target interpreter.  (Note: The AppleEvent
 * tools are part of the Carbon framework, so we need to deal with status
 * codes of type OSStatus.)
 */

/* Translate some common OSStatus values to strings. */
static const char *getError(OSStatus status) {
    static char aeErrorString[30];
    const char *errorName;
    switch (status) {
    case -50:
      errorName = "paramError";
      break;
    case -600:
      errorName = "procNotFound";
      break;
    case -609:
      errorName = "connectionInvalid";
    case -1700:
      errorName = "errAECoercionFail";
      break;
    case -1701:
      errorName = "errAEDescNotFound";
      break;
    case -1704:
      errorName = "errAENotAEDesc";
      break;
    case -1708:
      errorName = "errAEEventNotHandled";
      break;
    case -1712:
      errorName = "errAETimeout";
      break;
    default:
      errorName = aeErrorString;
      snprintf(aeErrorString, 30, "%d", status);
      break;
    }
    return errorName;
}

/* Macros for checking OSStatus values. */
#define CHECK(func)							\
    if (status != noErr) {						\
	char msg[512];							\
	snprintf(msg, 512, "%s returned error %s",			\
		   func, getError(status));				\
	Tcl_AddErrorInfo(interp, msg);					\
	Tcl_AppendResult(interp, msg, (char *)NULL);			\
	return TCL_ERROR;						\
    }

#define CHECK2(func)							\
    if (status != noErr && status != errAEDescNotFound) {		\
	char msg[512];							\
	snprintf(msg, 512, "%s returned error %s",			\
		   func, getError(status));				\
	Tcl_AddErrorInfo(interp, msg);					\
	Tcl_AppendResult(interp, msg, (char *)NULL);			\
	return TCL_ERROR;						\
    }

/*
 * For the implementation of the synchronous send command we need to be able
 * to process incoming Apple Events while we are waiting for a reply to an
 * Apple Event sent earlier.  For example, if we have two Tk applications
 * Wish and Wish #2 running in different processes and we issue the command:
 * `send Wish {send {Wish #2} set a 5}` on Wish #2, then Wish #2 must wait for
 * a reply to the full send command, but Wish cannot issue that reply until
 * Wish $2 has run the command `set a 5` sent to it by Wish.  So Wish #2 needs
 * to respond to the message from Wish concurrently with waiting for a
 * reply from Wish.
 *
 * To deal with this we create a separate thread to send the command while
 * condurrently running the Tk event loop in the main thread.  The following
 * Objective C class implements the thread.
 */

@interface AEReplyThread: NSThread
{
}
@property AppleEvent *eventPtr;
@property AppleEvent *replyPtr;
@property int *statusPtr;
@end

@implementation AEReplyThread
- (void) main
{
    /*
     * The original unix implementation of the send command would wait
     * arbitrarily long for the command to return, unless the target
     * interpreter died.  So we request no timeout in our call to
     * AESendMessage.  If the call returns a non-zero status we know that the
     * target process has died.
     */

    *_statusPtr = AESendMessage(_eventPtr, _replyPtr, kAEWaitReply,
				    kNoTimeOut);
    [NSThread exit];
}
@end


/*
 * Sends an AppleEvent of type DoScript to a Tk app identified by its pid.
 */

static int
sendAEDoScript(
    Tcl_Interp *interp,
    const pid_t pid,
    const char *command,
    int async)
{
    AppleEvent event, reply;
    OSStatus status;

    // Build an AppleEvent targeting the provided pid.
    status = AEBuildAppleEvent(kAEMiscStandards, // NOT kAECoreSuite!!!
			       kAEDoScript,
			       typeKernelProcessID, &pid, sizeof(pid_t),
			       kAutoGenerateReturnID,
			       kAnyTransactionID,
			       &event,
			       NULL,             // No error struct is needed.
			       "'----':utf8(@)", // direct parameter: utf8 bytes
			       strlen(command),
			       command);
    CHECK("AEBuildAppleEvent")

    // Send the event.
    if (async) {

	/*
	 * If the async parameter is true then no result is produced and
	 * errors are ignored.  So we do not need a reply to our AppleEvent.
	 */
	status = AESendMessage(&event, nil, kAENoReply, 0);
	CHECK("AESendMessage")
    } else {

	/*
	 * Otherwise we call AESendMessage from an NSThread and run
	 * an event loop until the status changes.
	 */

	status = 1;  /* impossible OSStatus */
	AEReplyThread *replyThread = [[AEReplyThread alloc] init];
	replyThread.eventPtr = &event;
	replyThread.replyPtr = &reply;
	replyThread.statusPtr = &status;
	[replyThread start];
	while (status == 1) {
	    // check if the target interpreter is alive.
	    Tcl_DoOneEvent(TCL_ALL_EVENTS);
	}
	if (status) {
	    const char *msg = "target application died";
	    Tcl_AddErrorInfo(interp, msg);
	    Tcl_SetResult(interp, msg, NULL);
	    return TCL_ERROR;
	}
    }
    int result = TCL_OK;
    if (async == 0) {
	// Read the reply and extract relevant info.
	int code = 0;
	DescType actualType = 0;
	AEGetParamPtr(&reply, keyErrorNumber, typeSInt32, &actualType,
		      &code, 4, NULL);
	CHECK2("AEGetParamPtr")
	if (code == TCL_OK) {
	    // Get the result string.
	    Size resultSize = 0;
	    status = AESizeOfParam(&reply, keyDirectObject, &actualType,
				   &resultSize);
	    CHECK2("AESizeOfParam")
	    char *resultBuffer = ckalloc(resultSize + 1);
	    AEGetParamPtr(&reply, keyDirectObject, typeUTF8Text, &actualType,
			  resultBuffer, resultSize, NULL);
	    CHECK2("AEGetParamPtr")
	    if (resultSize > 0) {
		resultBuffer[resultSize] = '\0';
		Tcl_SetObjResult(interp, Tcl_NewStringObj(resultBuffer,
							  TCL_INDEX_NONE));
	    }

	    result = TCL_OK;
	    ckfree(resultBuffer);
	    AEDisposeDesc(&reply);
	} else {
	    // Get the error string.
	    Size errorSize;
	    status = AESizeOfParam(&reply, keyErrorString,
				   &actualType, &errorSize);
	    CHECK2("AESizeOfParam")
	    char *errorBuffer = ckalloc(errorSize + 1);
	    AEGetParamPtr(&reply, keyErrorString, typeUTF8Text, &actualType,
			  errorBuffer, errorSize, NULL);
	    CHECK2("AEGetParamPtr")
	    if (errorSize > 0) {
		errorBuffer[errorSize] = '\0';
		Tcl_AddErrorInfo(interp, errorBuffer);
	    }
	    Tcl_SetObjErrorCode(interp, Tcl_NewStringObj(errorBuffer,
						      TCL_INDEX_NONE));
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(errorBuffer,
						      TCL_INDEX_NONE));
	    result = TCL_ERROR;
	    ckfree(errorBuffer);
	}
	AEDisposeDesc(&reply);
    }
    AEDisposeDesc(&event);
    return result;
}

/*
 * The following struct contains  per-process data:
 */

static struct {
    int sendSerial;		      /* The serial number that was used * in
				       * the last "send" command. */
    int sendDebug;		      /* This can be set while debugging to
				       * add print statements, for example. */
    int initialized;                  /* Set when SendInit is called. */
    RegisteredInterp *interpListPtr;  /* List of all interpreters registered
				       * in this process. */

} staticData = {0, 0, 0, 0};

/*
 * Forward declarations for static functions defined later in this file:
 */

static Tcl_CmdDeleteProc DeleteProc;
static NameRegistry*	RegOpen(Tcl_Interp *interp);
static void		RegClose(NameRegistry *regPtr);
static void		RegAddName(NameRegistry *regPtr, const char *name,
				    Window commWindow);
static void		RegDeleteName(NameRegistry *regPtr, const char *name);
static AppInfo		RegFindName(NameRegistry *regPtr, const char *name);
static int		SendInit();

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
     * on linux.  So we seek to the end anyway.
     */
    fseek(appNameFile, 0, SEEK_END);
    length = ftell(appNameFile);
    if (length > 0) {
	bytes = ckalloc(length);
	if (bytes) {
	    fseek(appNameFile, 0, SEEK_SET);
	    bytesRead = fread(bytes, 1, length, appNameFile);
	} else {
	    Tcl_Panic("Out of memory");
	}
    }
    flock(fileno(appNameFile), LOCK_UN);
    fclose(appNameFile);
    if (length == 0) {
	return Tcl_NewDictObj();
    }
    if (bytesRead != length) {
	Tcl_Panic("read failed on %s: length %lu; read %lu,\n", path,
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
 *	Sets up various data structures and windows.
 *
 *--------------------------------------------------------------
 */

static int
SendInit()
{
    /*
     * Intialize the path used for the appname registry.
     */
#if 0
    NSArray *searchPaths = NSSearchPathForDirectoriesInDomains(
		 NSCachesDirectory, NSUserDomainMask, YES);
    NSString *cachesDirectory = [searchPaths objectAtIndex:0];
    NSString *RegistryPath = [cachesDirectory
	stringByAppendingPathComponent:@"com.tcltk.appnames"];
    size_t length = 1 + strlen(RegistryPath.UTF8String);
    appNameRegistryPath = ckalloc(length);
    strlcpy(appNameRegistryPath, RegistryPath.UTF8String, length);
#endif
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * RegOpen --
 *
 *	This function loads the name registry into memory so that it can be
 *	manipulated.  It reads a string representation of a Tcl dict from a
 *	file and constructs the dict.
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
    Tcl_Interp *interp)		/* Interpreter to use for error reporting
				 * (errors cause a panic so in fact no error
				 * is ever returned, but the interpreter is
				 * needed anyway). */
{
    NameRegistry *regPtr;
    regPtr = (NameRegistry *)ckalloc(sizeof(NameRegistry));
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
    if (regPtr->modified) {
	saveAppNameRegistry(regPtr->appNameDict, appNameRegistryPath);
    }
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
 *      The return value is an AppInfo struct containing the pid and the
 *	clientData for the application named "name", or nil if there is no
 *	such entry in the registry.
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
    regPtr->modified = 1;
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
    Window commWindow)		/* X identifier for comm. window of
				 * application. */
{
    (void) commWindow; /////
    Tcl_Obj *keyPtr = Tcl_NewStringObj(name, TCL_INDEX_NONE);
    AppInfo valueTcl = {getpid(), 0};
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
 *      An crucial exception to the behavior described above arises when Tk is
 *      being run on a Continuous Integration runner.  The file-based App
 *      registry which is used to ensure uniqueness cannot be used on CI
 *      runners because macOS will post a system privacy dialog requesing
 *      permission to write files.  This causes Wish to hang, since it is not
 *      possible to dismiss the dialog without user interaction.  We use the
 *      environment variable CI to determine whether we are being run on a CI
 *      runner.  If so, this command always returns the name provided as an
 *      argument.  Note that this makes the command winfo interps ignore all
 *      other interpreteters.
 *
 * Side effects:
 *	In normal usage (i.e. being run interactively) the app name is saved
 *	in a registry file, thereby allowing the "send" command to be used
 *	later to invoke commands in the application. In addition, the "send"
 *	command is created in the application's interpreter. The registration
 *	will be removed automatically if the interpreter is deleted or the
 *	"send" command is removed.
 *
 *----------------------------------------------------------------------
 */

const char *
Tk_SetAppName(
    Tk_Window tkwin,		/* Token for any window in the application to
				 * be named: it is just used to identify the
				 * application. */
    const char *name)		/* The name that will be used to refer to the
				 * interpreter in later "send" commands. Must
				 * be globally unique. */
{
    if (getenv("CI")) {
	return name;
    }
    RegisteredInterp *riPtr;
    TkWindow *winPtr = (TkWindow *) tkwin;
    NameRegistry *regPtr;
    Tcl_Interp *interp;
    const char *actualName;
    Tcl_DString dString;
    int offset, i;
    interp = winPtr->mainPtr->interp;
    if (!staticData.initialized) {
	SendInit();
    }

    /*
     * See if the application is already registered; if so, remove its current
     * name from the registry.
     */

    regPtr = RegOpen(interp);
    for (riPtr = staticData.interpListPtr; ; riPtr = riPtr->nextPtr) {
	if (riPtr == NULL) {
	    /*
	     * This interpreter isn't currently registered; create the data
	     * structure that will be used to register it, plus add
	     * the "send" command to the interpreter.  The name gets added
	     * to the structure later.
	     */

	    riPtr = (RegisteredInterp *)ckalloc(sizeof(RegisteredInterp));
	    riPtr->interp = interp;
	    riPtr->nextPtr = staticData.interpListPtr;
	    staticData.interpListPtr = riPtr;
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

    RegAddName(regPtr, actualName, None);
    RegClose(regPtr);
    riPtr->name = (char *)ckalloc(strlen(actualName) + 1);
    strcpy(riPtr->name, actualName);
    if (actualName != name) {
	Tcl_DStringFree(&dString);
    }
    return riPtr->name;
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
    TCL_UNUSED(void *),	    /* Information about sender */
    Tcl_Interp *interp,	    /* Current interpreter. */
    Tcl_Size objc,	    /* Number of arguments. */
    Tcl_Obj *const objv[])  /* Argument strings. */
{
    enum {
	SEND_ASYNC, SEND_DISPLAYOF, SEND_LAST
    };
    static const char *const sendOptions[] = {
	"-async",   "-displayof",   "--",  NULL
    };
    const char *stringRep, *destName;
    TkWindow *winPtr;
    //    Window commWindow;
    RegisteredInterp *riPtr;
    int result, async, i, firstArg, index;
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

    /*
     * See if the target interpreter is local. If so, execute the command
     * directly without going through the X server. The only tricky thing is
     * passing the result from the target interpreter to the invoking
     * interpreter. Watch out: they could be the same!
     */

    for (riPtr = staticData.interpListPtr; riPtr != NULL;
	    riPtr = riPtr->nextPtr) {
	if (strcmp(riPtr->name, destName) != 0) {
	    continue;
	}
	/* We have found our target interpreter */
	Tcl_Preserve(riPtr);
	localInterp = riPtr->interp;
	Tcl_Preserve(localInterp);
	if (firstArg + 1 == objc) {
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

    regPtr = RegOpen(interp);
    AppInfo info = RegFindName(regPtr, destName);
    RegClose(regPtr);

    if (info.pid == 0) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"no application named \"%s\"", destName));
	Tcl_SetErrorCode(interp, "TK", "LOOKUP", "APPLICATION", destName,
		NULL);
	return TCL_ERROR;
    }

    /*
     * Send the command with args to the target interpreter
     */

    Tcl_DStringInit(&request2);
    Tcl_DStringAppend(&request2, Tcl_GetString(objv[firstArg]), TCL_INDEX_NONE);
    if (firstArg < objc - 1) {
	for (i = firstArg+1; i < objc; i++) {
	    Tcl_DStringAppend(&request2, " ", 1);
	    Tcl_DStringAppend(&request2, Tcl_GetString(objv[i]), TCL_INDEX_NONE);
	}
    }

     // When async is 0, the call below blocks until a reply is received.
     // Perhaps we should run a background thread to process timer events?

    int code = sendAEDoScript(interp, info.pid, Tcl_DStringValue(&request2),
			      async);
    staticData.sendSerial++;
    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * TkGetInterpNames --
 *
 *	This function is invoked to fetch a list of all the interpreter names
 *	currently registered on this host.
 *
 * Results:
 *	A standard Tcl return value. The interp's result will be set to hold a
 *	list of all the interpreter names defined on this host. If an
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
    TCL_UNUSED(Tk_Window))
{
    NameRegistry *regPtr;
    Tcl_Obj *resultObj = Tcl_NewObj();
    regPtr = RegOpen(interp);
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
    TCL_UNUSED(TkDisplay *)) /* dispPtr */
{
	ckfree((char *) appNameRegistryPath);
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
    regPtr = RegOpen(riPtr->interp);
    RegDeleteName(regPtr, riPtr->name);
    RegClose(regPtr);

    if (staticData.interpListPtr == riPtr) {
	staticData.interpListPtr = riPtr->nextPtr;
    } else {
	for (riPtr2 = staticData.interpListPtr; riPtr2 != NULL;
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
	Tcl_SetObjResult(interp, Tcl_NewWideIntObj(staticData.sendSerial+1));
    }
    return TCL_OK;
}
#endif


/*
 * Local Variables:
 * mode: objc
 * c-file-style: "k&r"
 * c-basic-offset: 4
 * c-file-offsets: ((arglist-cont . 0))
 * fill-column: 78
 * End:
 */

