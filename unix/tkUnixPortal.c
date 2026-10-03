/*
 * tkUnixPortal.c --
 *
 *	A minimal bridge to the XDG desktop portal (org.freedesktop.portal.*),
 *	through which Tk uses the desktop's own file chooser, print dialog and
 *	notifications.  The portal logic is in library/portal.tcl; this file
 *	implements the commands
 *
 *	    ::tk::portal::_call interface method ?arg ...?
 *	    ::tk::portal::_request window interface method ?arg ...?
 *	    ::tk::portal::_parent window
 *
 *	Each arg is a two-element list {signature value} (see Marshal).
 *
 *	libdbus is linked at runtime, so Tk does not depend on it.  If it, the
 *	session bus or the portal is missing, the Tcl library falls back to
 *	the dialogs implemented in Tcl.
 *
 * Copyright © 2026 Serhiy Storchaka
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkUnixInt.h"

/*
 * Minimal declarations from <dbus/dbus.h>.  The libdbus ABI is stable.
 */

typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;
typedef unsigned int dbus_bool_t;
typedef unsigned int dbus_uint32_t;

typedef struct {
    const char *name;
    const char *message;
    unsigned int dummy;		/* dummy1..dummy5 bit fields */
    void *padding1;
} DBusError;

typedef struct {
    void *dummy[16];		/* Larger than the real DBusMessageIter. */
} DBusMessageIter;

#define DBUS_BUS_SESSION	0
#define DBUS_TYPE_INVALID	0
#define DBUS_TYPE_BYTE		'y'
#define DBUS_TYPE_BOOLEAN	'b'
#define DBUS_TYPE_INT16		'n'
#define DBUS_TYPE_UINT16	'q'
#define DBUS_TYPE_INT32		'i'
#define DBUS_TYPE_UINT32	'u'
#define DBUS_TYPE_INT64		'x'
#define DBUS_TYPE_UINT64	't'
#define DBUS_TYPE_DOUBLE	'd'
#define DBUS_TYPE_STRING	's'
#define DBUS_TYPE_OBJECT_PATH	'o'
#define DBUS_TYPE_SIGNATURE	'g'
#define DBUS_TYPE_UNIX_FD	'h'
#define DBUS_TYPE_ARRAY		'a'
#define DBUS_TYPE_VARIANT	'v'
#define DBUS_TYPE_STRUCT	'r'
#define DBUS_TYPE_DICT_ENTRY	'e'
#define DBUS_TIMEOUT_USE_DEFAULT (-1)

#define DBUS_FUNCTIONS \
    FN(void, error_init, (DBusError *)) \
    FN(void, error_free, (DBusError *)) \
    FN(dbus_bool_t, error_is_set, (const DBusError *)) \
    FN(DBusConnection *, bus_get_private, (int, DBusError *)) \
    FN(const char *, bus_get_unique_name, (DBusConnection *)) \
    FN(void, bus_add_match, (DBusConnection *, const char *, DBusError *)) \
    FN(void, bus_remove_match, (DBusConnection *, const char *, DBusError *)) \
    FN(void, connection_set_exit_on_disconnect, (DBusConnection *, dbus_bool_t)) \
    FN(dbus_bool_t, connection_get_unix_fd, (DBusConnection *, int *)) \
    FN(dbus_bool_t, connection_get_is_connected, (DBusConnection *)) \
    FN(dbus_bool_t, connection_read_write, (DBusConnection *, int)) \
    FN(DBusMessage *, connection_pop_message, (DBusConnection *)) \
    FN(DBusMessage *, connection_send_with_reply_and_block, \
	    (DBusConnection *, DBusMessage *, int, DBusError *)) \
    FN(dbus_bool_t, connection_send, \
	    (DBusConnection *, DBusMessage *, dbus_uint32_t *)) \
    FN(void, connection_flush, (DBusConnection *)) \
    FN(void, connection_close, (DBusConnection *)) \
    FN(void, connection_unref, (DBusConnection *)) \
    FN(DBusMessage *, message_new_method_call, \
	    (const char *, const char *, const char *, const char *)) \
    FN(void, message_unref, (DBusMessage *)) \
    FN(dbus_bool_t, message_is_signal, \
	    (DBusMessage *, const char *, const char *)) \
    FN(const char *, message_get_path, (DBusMessage *)) \
    FN(dbus_bool_t, message_iter_init, (DBusMessage *, DBusMessageIter *)) \
    FN(void, message_iter_init_append, (DBusMessage *, DBusMessageIter *)) \
    FN(dbus_bool_t, message_iter_append_basic, \
	    (DBusMessageIter *, int, const void *)) \
    FN(dbus_bool_t, message_iter_append_fixed_array, \
	    (DBusMessageIter *, int, const void *, int)) \
    FN(dbus_bool_t, message_iter_open_container, \
	    (DBusMessageIter *, int, const char *, DBusMessageIter *)) \
    FN(dbus_bool_t, message_iter_close_container, \
	    (DBusMessageIter *, DBusMessageIter *)) \
    FN(void, message_iter_abandon_container, \
	    (DBusMessageIter *, DBusMessageIter *)) \
    FN(int, message_iter_get_arg_type, (DBusMessageIter *)) \
    FN(int, message_iter_get_element_type, (DBusMessageIter *)) \
    FN(void, message_iter_get_basic, (DBusMessageIter *, void *)) \
    FN(void, message_iter_get_fixed_array, \
	    (DBusMessageIter *, void *, int *)) \
    FN(dbus_bool_t, message_iter_next, (DBusMessageIter *)) \
    FN(void, message_iter_recurse, (DBusMessageIter *, DBusMessageIter *)) \
    FN(dbus_bool_t, signature_validate_single, (const char *, DBusError *))

static struct {
    int loaded;			/* 0: not tried, 1: OK, -1: unavailable. */
    Tcl_LoadHandle lib;
#define FN(ret, name, args) ret (*name) args;
    DBUS_FUNCTIONS
#undef FN
} dbus;

TCL_DECLARE_MUTEX(dbusMutex);

#define PORTAL_BUS_NAME		"org.freedesktop.portal.Desktop"
#define PORTAL_OBJECT_PATH	"/org/freedesktop/portal/desktop"
#define PORTAL_REQUEST_PATH	"/org/freedesktop/portal/desktop/request/"
#define REQUEST_INTERFACE	"org.freedesktop.portal.Request"

/*
 * A request waiting for its Response signal.
 */

typedef struct PendingRequest {
    char *path;			/* Object path of the Request. */
    char *altPath;		/* Path returned by the portal if it differs
				 * from the expected one, or NULL. */
    int done;			/* Response received or connection lost. */
    int parentGone;		/* The parent window was destroyed. */
    DBusMessage *response;	/* The Response signal, or NULL. */
    struct PendingRequest *nextPtr;
} PendingRequest;

typedef struct {
    DBusConnection *conn;	/* Private session bus connection. */
    int fd;
    PendingRequest *pendingPtr;	/* Requests waiting for a response. */
    unsigned int counter;	/* For generating request tokens. */
    int exitHandlerSet;		/* CloseConnection is a thread exit
				 * handler. */
} ThreadSpecificData;

static Tcl_ThreadDataKey dataKey;

static Tcl_ObjCmdProc2 CallCmd;
static Tcl_ObjCmdProc2 RequestCmd;
static Tcl_ObjCmdProc2 ParentCmd;

/*
 *----------------------------------------------------------------------
 *
 * LoadDBus --
 *
 *	Loads libdbus and resolves the functions used in this file.
 *
 * Results:
 *	1 on success, 0 if libdbus is not available.
 *
 *----------------------------------------------------------------------
 */

static int
LoadDBus(
    Tcl_Interp *interp)
{
    static const char *const libs[] = {
	"libdbus-1.so.3", "libdbus-1.so", NULL
    };
    int i;

    Tcl_MutexLock(&dbusMutex);
    if (dbus.loaded == 0) {
	dbus.loaded = -1;
	for (i = 0; libs[i] != NULL; i++) {
	    Tcl_Obj *nameObj = Tcl_NewStringObj(libs[i], TCL_INDEX_NONE);
	    int code;

	    Tcl_IncrRefCount(nameObj);
	    code = Tcl_LoadFile(interp, nameObj, NULL, 0, NULL, &dbus.lib);
	    Tcl_DecrRefCount(nameObj);
	    if (code == TCL_OK) {
		break;
	    }
	}
	Tcl_ResetResult(interp);
	if (dbus.lib != NULL) {
	    int ok = 1;

#define FN(ret, name, args) \
	    dbus.name = (ret (*) args) Tcl_FindSymbol(NULL, dbus.lib, "dbus_" #name); \
	    ok = ok && dbus.name != NULL;
	    DBUS_FUNCTIONS
#undef FN
	    if (ok) {
		dbus.loaded = 1;
	    } else {
		Tcl_FSUnloadFile(NULL, dbus.lib);
		dbus.lib = NULL;
	    }
	}
    }
    Tcl_MutexUnlock(&dbusMutex);
    return dbus.loaded == 1;
}

/*
 *----------------------------------------------------------------------
 *
 * DispatchMessages --
 *
 *	Takes all queued messages off the connection and hands Response
 *	signals over to the matching pending requests.
 *
 *----------------------------------------------------------------------
 */

static void
DispatchMessages(
    ThreadSpecificData *tsdPtr)
{
    DBusMessage *msg;
    PendingRequest *reqPtr;

    while ((msg = dbus.connection_pop_message(tsdPtr->conn)) != NULL) {
	if (dbus.message_is_signal(msg, REQUEST_INTERFACE, "Response")) {
	    const char *path = dbus.message_get_path(msg);

	    for (reqPtr = tsdPtr->pendingPtr; reqPtr != NULL;
		    reqPtr = reqPtr->nextPtr) {
		if (!reqPtr->done && path != NULL
			&& (strcmp(path, reqPtr->path) == 0
			|| (reqPtr->altPath != NULL
			&& strcmp(path, reqPtr->altPath) == 0))) {
		    reqPtr->done = 1;
		    reqPtr->response = msg;
		    msg = NULL;
		    break;
		}
	    }
	}
	if (msg != NULL) {
	    dbus.message_unref(msg);
	}
    }
    if (!dbus.connection_get_is_connected(tsdPtr->conn)) {
	for (reqPtr = tsdPtr->pendingPtr; reqPtr != NULL;
		reqPtr = reqPtr->nextPtr) {
	    reqPtr->done = 1;
	}
    }
}

static void
ConnectionFileProc(
    void *clientData,
    TCL_UNUSED(int))	/* mask */
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)clientData;

    dbus.connection_read_write(tsdPtr->conn, 0);
    DispatchMessages(tsdPtr);
}

static void
CloseConnection(
    void *clientData)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)clientData;

    if (tsdPtr->conn != NULL) {
	Tcl_DeleteFileHandler(tsdPtr->fd);
	dbus.connection_close(tsdPtr->conn);
	dbus.connection_unref(tsdPtr->conn);
	tsdPtr->conn = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetConnection --
 *
 *	Returns the private session bus connection of the current thread,
 *	opening it if necessary.
 *
 * Results:
 *	The connection, or NULL with an error message left in interp.
 *
 *----------------------------------------------------------------------
 */

static DBusConnection *
GetConnection(
    Tcl_Interp *interp)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    DBusError err;

    if (tsdPtr->conn != NULL) {
	if (dbus.connection_get_is_connected(tsdPtr->conn)) {
	    return tsdPtr->conn;
	}
	if (tsdPtr->pendingPtr != NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "lost connection to the session bus", TCL_INDEX_NONE));
	    return NULL;
	}
	CloseConnection(tsdPtr);
    }
    if (!tsdPtr->exitHandlerSet) {
	Tcl_CreateThreadExitHandler(CloseConnection, tsdPtr);
	tsdPtr->exitHandlerSet = 1;
    }

    dbus.error_init(&err);
    tsdPtr->conn = dbus.bus_get_private(DBUS_BUS_SESSION, &err);
    if (tsdPtr->conn == NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"cannot connect to the session bus: %s",
		dbus.error_is_set(&err) ? err.message : "unknown error"));
	dbus.error_free(&err);
	return NULL;
    }
    dbus.connection_set_exit_on_disconnect(tsdPtr->conn, 0);
    dbus.connection_get_unix_fd(tsdPtr->conn, &tsdPtr->fd);
    Tcl_CreateFileHandler(tsdPtr->fd, TCL_READABLE, ConnectionFileProc,
	    tsdPtr);
    return tsdPtr->conn;
}

/*
 *----------------------------------------------------------------------
 *
 * CallMethod --
 *
 *	Sends a method call and waits for the reply.  Other messages that
 *	arrive meanwhile are dispatched afterwards.
 *
 * Results:
 *	The reply, or NULL with an error message left in interp.
 *
 *----------------------------------------------------------------------
 */

static DBusMessage *
CallMethod(
    Tcl_Interp *interp,
    DBusMessage *msg)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    DBusError err;
    DBusMessage *reply;

    dbus.error_init(&err);
    reply = dbus.connection_send_with_reply_and_block(tsdPtr->conn, msg,
	    DBUS_TIMEOUT_USE_DEFAULT, &err);
    if (reply == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		dbus.error_is_set(&err) ? err.message : "D-Bus call failed",
		TCL_INDEX_NONE));
	Tcl_SetErrorCode(interp, "TK", "PORTAL", "DBUS",
		dbus.error_is_set(&err) ? err.name : "Failed", (char *)NULL);
	dbus.error_free(&err);
    }
    DispatchMessages(tsdPtr);
    return reply;
}

/*
 *----------------------------------------------------------------------
 *
 * SkipType --
 *
 *	Returns a pointer past the single complete type that starts at sig.
 *	The signature must have been validated.
 *
 *----------------------------------------------------------------------
 */

static const char *
SkipType(
    const char *sig)
{
    switch (*sig) {
    case 'a':
	return SkipType(sig + 1);
    case '(':
	sig++;
	while (*sig != ')') {
	    sig = SkipType(sig);
	}
	return sig + 1;
    case '{':
	sig = SkipType(SkipType(sig + 1));
	return sig + 1;
    default:
	return sig + 1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Marshal --
 *
 *	Appends the Tcl value valueObj as the D-Bus type that starts at sig.
 *	The representation of values:
 *
 *	    y n q i u x t	integer
 *	    b			boolean
 *	    d			double
 *	    s o g		string
 *	    h			name of a readable Tcl channel; its file
 *				descriptor is passed
 *	    ay			byte array
 *	    a{..}		dict
 *	    a.			list
 *	    (...)		list with one element per member
 *	    v			list {signature value}
 *
 *	If token is not NULL, the value must be an a{sv} and an entry
 *	"handle_token" with this value is added to it.
 *
 * Results:
 *	A standard Tcl result.  On error the message can not be sent.
 *
 *----------------------------------------------------------------------
 */

static int
Marshal(
    Tcl_Interp *interp,
    DBusMessageIter *iter,
    const char *sig,
    Tcl_Obj *valueObj,
    Tcl_Encoding utf8,
    const char *token)
{
    DBusMessageIter sub, entry, var;
    Tcl_DString ds;
    Tcl_Size n, i;
    Tcl_Obj **elems;
    int code = TCL_OK;

    switch (*sig) {
    case DBUS_TYPE_BYTE:
    case DBUS_TYPE_INT16:
    case DBUS_TYPE_UINT16:
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UINT32:
    case DBUS_TYPE_INT64:
    case DBUS_TYPE_UINT64: {
	Tcl_WideInt w;
	union {
	    unsigned char y;
	    short n;
	    unsigned short q;
	    int i;
	    unsigned int u;
	    long long x;
	} value;

	if (Tcl_GetWideIntFromObj(interp, valueObj, &w) != TCL_OK) {
	    return TCL_ERROR;
	}
	switch (*sig) {
	case DBUS_TYPE_BYTE:	value.y = (unsigned char)w; break;
	case DBUS_TYPE_INT16:	value.n = (short)w; break;
	case DBUS_TYPE_UINT16:	value.q = (unsigned short)w; break;
	case DBUS_TYPE_INT32:	value.i = (int)w; break;
	case DBUS_TYPE_UINT32:	value.u = (unsigned int)w; break;
	default:		value.x = w; break;
	}
	dbus.message_iter_append_basic(iter, *sig, &value);
	return TCL_OK;
    }
    case DBUS_TYPE_BOOLEAN: {
	int b;
	dbus_bool_t value;

	if (Tcl_GetBooleanFromObj(interp, valueObj, &b) != TCL_OK) {
	    return TCL_ERROR;
	}
	value = b;
	dbus.message_iter_append_basic(iter, DBUS_TYPE_BOOLEAN, &value);
	return TCL_OK;
    }
    case DBUS_TYPE_DOUBLE: {
	double d;

	if (Tcl_GetDoubleFromObj(interp, valueObj, &d) != TCL_OK) {
	    return TCL_ERROR;
	}
	dbus.message_iter_append_basic(iter, DBUS_TYPE_DOUBLE, &d);
	return TCL_OK;
    }
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE: {
	const char *s;

	Tcl_UtfToExternalDString(utf8, Tcl_GetString(valueObj),
		TCL_INDEX_NONE, &ds);
	s = Tcl_DStringValue(&ds);
	dbus.message_iter_append_basic(iter, *sig, &s);
	Tcl_DStringFree(&ds);
	return TCL_OK;
    }
    case DBUS_TYPE_UNIX_FD: {
	Tcl_Channel chan;
	void *handle;
	int mode, fd;

	chan = Tcl_GetChannel(interp, Tcl_GetString(valueObj), &mode);
	if (chan == NULL) {
	    return TCL_ERROR;
	}
	if (Tcl_GetChannelHandle(chan, TCL_READABLE, &handle) != TCL_OK) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "channel \"%s\" wasn't opened for reading",
		    Tcl_GetString(valueObj)));
	    return TCL_ERROR;
	}
	fd = (int)PTR2INT(handle);
	dbus.message_iter_append_basic(iter, DBUS_TYPE_UNIX_FD, &fd);
	return TCL_OK;
    }
    case DBUS_TYPE_VARIANT: {
	DBusError err;
	const char *varSig;

	if (Tcl_ListObjGetElements(interp, valueObj, &n, &elems) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (n != 2) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "bad variant \"%s\": should be \"signature value\"",
		    Tcl_GetString(valueObj)));
	    return TCL_ERROR;
	}
	varSig = Tcl_GetString(elems[0]);
	dbus.error_init(&err);
	if (!dbus.signature_validate_single(varSig, &err)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "bad signature \"%s\"", varSig));
	    dbus.error_free(&err);
	    return TCL_ERROR;
	}
	dbus.message_iter_open_container(iter, DBUS_TYPE_VARIANT, varSig,
		&var);
	code = Marshal(interp, &var, varSig, elems[1], utf8, NULL);
	break;
    }
    case DBUS_TYPE_ARRAY: {
	const char *elemSig = sig + 1;

	Tcl_DStringInit(&ds);
	Tcl_DStringAppend(&ds, elemSig, SkipType(elemSig) - elemSig);
	if (*elemSig == DBUS_TYPE_BYTE) {
	    const unsigned char *bytes = Tcl_GetBytesFromObj(interp,
		    valueObj, &n);

	    if (bytes == NULL) {
		Tcl_DStringFree(&ds);
		return TCL_ERROR;
	    }
	    dbus.message_iter_open_container(iter, DBUS_TYPE_ARRAY, "y",
		    &var);
	    dbus.message_iter_append_fixed_array(&var, DBUS_TYPE_BYTE,
		    &bytes, (int)n);
	} else if (*elemSig == '{') {
	    const char *keySig = elemSig + 1;
	    const char *valSig = SkipType(keySig);
	    Tcl_DictSearch search;
	    Tcl_Obj *keyObj, *valObj;
	    int done;

	    if (Tcl_DictObjFirst(interp, valueObj, &search, &keyObj, &valObj,
		    &done) != TCL_OK) {
		Tcl_DStringFree(&ds);
		return TCL_ERROR;
	    }
	    dbus.message_iter_open_container(iter, DBUS_TYPE_ARRAY,
		    Tcl_DStringValue(&ds), &var);
	    if (token != NULL) {
		const char *key = "handle_token";

		dbus.message_iter_open_container(&var, DBUS_TYPE_DICT_ENTRY,
			NULL, &entry);
		dbus.message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
		dbus.message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
			"s", &sub);
		dbus.message_iter_append_basic(&sub, DBUS_TYPE_STRING, &token);
		dbus.message_iter_close_container(&entry, &sub);
		dbus.message_iter_close_container(&var, &entry);
	    }
	    for (; !done && code == TCL_OK;
		    Tcl_DictObjNext(&search, &keyObj, &valObj, &done)) {
		dbus.message_iter_open_container(&var, DBUS_TYPE_DICT_ENTRY,
			NULL, &entry);
		code = Marshal(interp, &entry, keySig, keyObj, utf8, NULL);
		if (code == TCL_OK) {
		    code = Marshal(interp, &entry, valSig, valObj, utf8, NULL);
		}
		if (code == TCL_OK) {
		    dbus.message_iter_close_container(&var, &entry);
		} else {
		    dbus.message_iter_abandon_container(&var, &entry);
		}
	    }
	    Tcl_DictObjDone(&search);
	} else {
	    if (Tcl_ListObjGetElements(interp, valueObj, &n, &elems)
		    != TCL_OK) {
		Tcl_DStringFree(&ds);
		return TCL_ERROR;
	    }
	    dbus.message_iter_open_container(iter, DBUS_TYPE_ARRAY,
		    Tcl_DStringValue(&ds), &var);
	    for (i = 0; i < n && code == TCL_OK; i++) {
		code = Marshal(interp, &var, elemSig, elems[i], utf8, NULL);
	    }
	}
	Tcl_DStringFree(&ds);
	break;
    }
    case '(': {
	const char *p;
	Tcl_Size count = 0;

	if (Tcl_ListObjGetElements(interp, valueObj, &n, &elems) != TCL_OK) {
	    return TCL_ERROR;
	}
	for (p = sig + 1; *p != ')'; p = SkipType(p)) {
	    count++;
	}
	if (n != count) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "bad struct \"%s\": should have %" TCL_SIZE_MODIFIER
		    "d elements", Tcl_GetString(valueObj), count));
	    return TCL_ERROR;
	}
	dbus.message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL, &var);
	for (p = sig + 1, i = 0; *p != ')' && code == TCL_OK;
		p = SkipType(p), i++) {
	    code = Marshal(interp, &var, p, elems[i], utf8, NULL);
	}
	break;
    }
    default:
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"unsupported type \"%c\"", *sig));
	return TCL_ERROR;
    }

    if (code == TCL_OK) {
	dbus.message_iter_close_container(iter, &var);
    } else {
	dbus.message_iter_abandon_container(iter, &var);
    }
    return code;
}

/*
 *----------------------------------------------------------------------
 *
 * Unmarshal --
 *
 *	Converts the value at iter to a Tcl value, using the representation
 *	described for Marshal.  Variants are replaced by their values.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Obj *
Unmarshal(
    DBusMessageIter *iter,
    Tcl_Encoding utf8)
{
    DBusMessageIter sub, entry;
    Tcl_Obj *resultObj;
    Tcl_DString ds;
    union {
	unsigned char y;
	dbus_bool_t b;
	short n;
	unsigned short q;
	int i;
	unsigned int u;
	long long x;
	unsigned long long t;
	double d;
	const char *s;
    } value;

    switch (dbus.message_iter_get_arg_type(iter)) {
    case DBUS_TYPE_BYTE:
	dbus.message_iter_get_basic(iter, &value);
	return Tcl_NewWideIntObj(value.y);
    case DBUS_TYPE_BOOLEAN:
	dbus.message_iter_get_basic(iter, &value);
	return Tcl_NewBooleanObj(value.b != 0);
    case DBUS_TYPE_INT16:
	dbus.message_iter_get_basic(iter, &value);
	return Tcl_NewWideIntObj(value.n);
    case DBUS_TYPE_UINT16:
	dbus.message_iter_get_basic(iter, &value);
	return Tcl_NewWideIntObj(value.q);
    case DBUS_TYPE_INT32:
    case DBUS_TYPE_UNIX_FD:
	dbus.message_iter_get_basic(iter, &value);
	return Tcl_NewWideIntObj(value.i);
    case DBUS_TYPE_UINT32:
	dbus.message_iter_get_basic(iter, &value);
	return Tcl_NewWideIntObj(value.u);
    case DBUS_TYPE_INT64:
	dbus.message_iter_get_basic(iter, &value);
	return Tcl_NewWideIntObj(value.x);
    case DBUS_TYPE_UINT64:
	dbus.message_iter_get_basic(iter, &value);
	return Tcl_NewWideUIntObj(value.t);
    case DBUS_TYPE_DOUBLE:
	dbus.message_iter_get_basic(iter, &value);
	return Tcl_NewDoubleObj(value.d);
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE:
	dbus.message_iter_get_basic(iter, &value);
	Tcl_ExternalToUtfDString(utf8, value.s, TCL_INDEX_NONE, &ds);
	return Tcl_DStringToObj(&ds);
    case DBUS_TYPE_VARIANT:
	dbus.message_iter_recurse(iter, &sub);
	return Unmarshal(&sub, utf8);
    case DBUS_TYPE_ARRAY:
	resultObj = Tcl_NewObj();
	switch (dbus.message_iter_get_element_type(iter)) {
	case DBUS_TYPE_BYTE: {
	    const unsigned char *bytes;
	    int n;

	    dbus.message_iter_recurse(iter, &sub);
	    dbus.message_iter_get_fixed_array(&sub, &bytes, &n);
	    Tcl_DecrRefCount(resultObj);
	    return Tcl_NewByteArrayObj(bytes, n);
	}
	case DBUS_TYPE_DICT_ENTRY:
	    dbus.message_iter_recurse(iter, &sub);
	    while (dbus.message_iter_get_arg_type(&sub)
		    == DBUS_TYPE_DICT_ENTRY) {
		Tcl_Obj *keyObj;

		dbus.message_iter_recurse(&sub, &entry);
		keyObj = Unmarshal(&entry, utf8);
		dbus.message_iter_next(&entry);
		Tcl_DictObjPut(NULL, resultObj, keyObj,
			Unmarshal(&entry, utf8));
		dbus.message_iter_next(&sub);
	    }
	    return resultObj;
	default:
	    goto elements;
	}
    case DBUS_TYPE_STRUCT:
	resultObj = Tcl_NewObj();
    elements:
	dbus.message_iter_recurse(iter, &sub);
	while (dbus.message_iter_get_arg_type(&sub) != DBUS_TYPE_INVALID) {
	    Tcl_ListObjAppendElement(NULL, resultObj, Unmarshal(&sub, utf8));
	    dbus.message_iter_next(&sub);
	}
	return resultObj;
    default:
	return Tcl_NewObj();
    }
}

/*
 *----------------------------------------------------------------------
 *
 * NewPortalCall --
 *
 *	Creates a method call to the portal with arguments given as
 *	{signature value} lists.  If token is not NULL, it is added as
 *	"handle_token" to the last argument, which must be an a{sv}.
 *
 * Results:
 *	The message, or NULL with an error message left in interp.
 *
 *----------------------------------------------------------------------
 */

static DBusMessage *
NewPortalCall(
    Tcl_Interp *interp,
    const char *interface,
    const char *method,
    Tcl_Size objc,
    Tcl_Obj *const *objv,
    const char *token)
{
    DBusMessage *msg;
    DBusMessageIter iter;
    DBusError err;
    Tcl_Encoding utf8;
    Tcl_Size i, n;
    Tcl_Obj **elems, *sigObj = NULL;
    const char *sig;
    int code = TCL_OK;

    if (token != NULL && (objc == 0
	    || Tcl_ListObjIndex(interp, objv[objc - 1], 0, &sigObj) != TCL_OK
	    || sigObj == NULL || strcmp(Tcl_GetString(sigObj), "a{sv}"))) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"the last argument of a request must be an a{sv}",
		TCL_INDEX_NONE));
	return NULL;
    }
    msg = dbus.message_new_method_call(PORTAL_BUS_NAME, PORTAL_OBJECT_PATH,
	    interface, method);
    if (msg == NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"bad interface \"%s\" or method \"%s\"", interface, method));
	return NULL;
    }
    utf8 = Tcl_GetEncoding(NULL, "utf-8");
    dbus.message_iter_init_append(msg, &iter);
    dbus.error_init(&err);
    for (i = 0; i < objc && code == TCL_OK; i++) {
	code = Tcl_ListObjGetElements(interp, objv[i], &n, &elems);
	if (code != TCL_OK) {
	    break;
	}
	if (n != 2) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "bad argument \"%s\": should be \"signature value\"",
		    Tcl_GetString(objv[i])));
	    code = TCL_ERROR;
	    break;
	}
	sig = Tcl_GetString(elems[0]);
	if (!dbus.signature_validate_single(sig, &err)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad signature \"%s\"",
		    sig));
	    dbus.error_free(&err);
	    code = TCL_ERROR;
	    break;
	}
	code = Marshal(interp, &iter, sig, elems[1], utf8,
		i == objc - 1 ? token : NULL);
    }
    Tcl_FreeEncoding(utf8);
    if (code != TCL_OK) {
	dbus.message_unref(msg);
	return NULL;
    }
    return msg;
}

/*
 *----------------------------------------------------------------------
 *
 * CallCmd --
 *
 *	Implements "::tk::portal::_call interface method ?arg ...?".  Calls a
 *	method of the portal object and returns its result: the value if
 *	there is one, otherwise a list of values.
 *
 *----------------------------------------------------------------------
 */

static int
CallCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    DBusMessage *msg, *reply;
    DBusMessageIter iter;
    Tcl_Encoding utf8;
    Tcl_Obj *resultObj;
    Tcl_Size n;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "interface method ?arg ...?");
	return TCL_ERROR;
    }
    if (GetConnection(interp) == NULL) {
	return TCL_ERROR;
    }
    msg = NewPortalCall(interp, Tcl_GetString(objv[1]),
	    Tcl_GetString(objv[2]), objc - 3, objv + 3, NULL);
    if (msg == NULL) {
	return TCL_ERROR;
    }
    reply = CallMethod(interp, msg);
    dbus.message_unref(msg);
    if (reply == NULL) {
	return TCL_ERROR;
    }
    utf8 = Tcl_GetEncoding(NULL, "utf-8");
    resultObj = Tcl_NewObj();
    if (dbus.message_iter_init(reply, &iter)) {
	do {
	    Tcl_ListObjAppendElement(NULL, resultObj, Unmarshal(&iter, utf8));
	} while (dbus.message_iter_next(&iter));
    }
    Tcl_FreeEncoding(utf8);
    dbus.message_unref(reply);
    Tcl_ListObjLength(NULL, resultObj, &n);
    if (n == 1) {
	Tcl_Obj *elemObj;

	Tcl_ListObjIndex(NULL, resultObj, 0, &elemObj);
	Tcl_IncrRefCount(elemObj);
	Tcl_DecrRefCount(resultObj);
	Tcl_SetObjResult(interp, elemObj);
	Tcl_DecrRefCount(elemObj);
    } else {
	Tcl_SetObjResult(interp, resultObj);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GetParentHandle --
 *
 *	Returns the portal identifier ("x11:XID") of the toplevel containing
 *	tkwin, so that a dialog is made transient for it.  The XID is that of
 *	the wrapper window, which carries the window manager properties.
 *	Returns "" if there is no such mapped toplevel.
 *
 *----------------------------------------------------------------------
 */

static void
GetParentHandle(
    Tk_Window tkwin,
    char *buf,
    size_t size)
{
    TkWindow *winPtr = (TkWindow *)tkwin;

    buf[0] = '\0';
    while (winPtr != NULL && !(winPtr->flags & TK_TOP_HIERARCHY)) {
	winPtr = winPtr->parentPtr;
    }
    if (winPtr != NULL && Tk_IsMapped(winPtr)) {
	TkWindow *wrapperPtr = TkpGetWrapperWindow(winPtr);

	if (wrapperPtr != NULL && wrapperPtr->window != None) {
	    snprintf(buf, size, "x11:%lx", (unsigned long)wrapperPtr->window);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ParentCmd --
 *
 *	Implements "::tk::portal::_parent window".  Returns the identifier of
 *	the toplevel of window to be passed as the parent_window argument.
 *
 *----------------------------------------------------------------------
 */

static int
ParentCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    Tk_Window tkwin;
    char handle[32];

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "window");
	return TCL_ERROR;
    }
    tkwin = Tk_NameToWindow(interp, Tcl_GetString(objv[1]),
	    Tk_MainWindow(interp));
    if (tkwin == NULL) {
	return TCL_ERROR;
    }
    GetParentHandle(tkwin, handle, sizeof(handle));
    Tcl_SetObjResult(interp, Tcl_NewStringObj(handle, TCL_INDEX_NONE));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ParentEventProc --
 *
 *	Notices the destruction of the window passed to _request, so that
 *	the request can be closed.
 *
 *----------------------------------------------------------------------
 */

static void
ParentEventProc(
    void *clientData,
    XEvent *eventPtr)
{
    PendingRequest *reqPtr = (PendingRequest *)clientData;

    if (eventPtr->type == DestroyNotify) {
	reqPtr->parentGone = 1;
    }
}

static void
AddResponseMatch(
    DBusConnection *conn,
    Tcl_DString *dsPtr,
    const char *path,
    DBusError *errPtr)
{
    Tcl_DStringSetLength(dsPtr, 0);
    Tcl_DStringAppend(dsPtr, "type='signal',sender='" PORTAL_BUS_NAME
	    "',interface='" REQUEST_INTERFACE "',member='Response',path='",
	    TCL_INDEX_NONE);
    Tcl_DStringAppend(dsPtr, path, TCL_INDEX_NONE);
    Tcl_DStringAppend(dsPtr, "'", 1);
    dbus.bus_add_match(conn, Tcl_DStringValue(dsPtr), errPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * RequestCmd --
 *
 *	Implements "::tk::portal::_request window interface method ?arg ...?".
 *	Calls a portal method that returns a Request handle, and processes
 *	events until the Response signal arrives.  The last argument must be
 *	the options a{sv}; "handle_token" is added to it.  If window (which
 *	can be "") is destroyed meanwhile, the request is closed.
 *
 * Results:
 *	A dict with keys "response" (0: success, 1: cancelled, 2: other) and
 *	"results".
 *
 *----------------------------------------------------------------------
 */

static int
RequestCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    DBusConnection *conn;
    DBusMessage *msg, *reply;
    DBusMessageIter iter;
    DBusError err;
    PendingRequest req, **prevPtrPtr;
    Tcl_DString pathDs, matchDs;
    Tk_Window window = NULL;
    const char *p, *replyPath;
    char token[32];
    int code = TCL_ERROR;

    if (objc < 4) {
	Tcl_WrongNumArgs(interp, 1, objv,
		"window interface method ?arg ...?");
	return TCL_ERROR;
    }
    if (Tcl_GetString(objv[1])[0] != '\0') {
	window = Tk_NameToWindow(interp, Tcl_GetString(objv[1]),
		Tk_MainWindow(interp));
	if (window == NULL) {
	    return TCL_ERROR;
	}
    }
    conn = GetConnection(interp);
    if (conn == NULL) {
	return TCL_ERROR;
    }

    snprintf(token, sizeof(token), "tk%u", ++tsdPtr->counter);
    msg = NewPortalCall(interp, Tcl_GetString(objv[2]),
	    Tcl_GetString(objv[3]), objc - 4, objv + 4, token);
    if (msg == NULL) {
	return TCL_ERROR;
    }

    /*
     * Subscribe to the Response signal before making the call.  The request
     * path is derived from our unique bus name and the handle token.
     */

    Tcl_DStringInit(&pathDs);
    Tcl_DStringAppend(&pathDs, PORTAL_REQUEST_PATH, TCL_INDEX_NONE);
    for (p = dbus.bus_get_unique_name(conn) + 1; *p; p++) {
	Tcl_DStringAppend(&pathDs, *p == '.' ? "_" : p, 1);
    }
    Tcl_DStringAppend(&pathDs, "/", 1);
    Tcl_DStringAppend(&pathDs, token, TCL_INDEX_NONE);

    memset(&req, 0, sizeof(req));
    req.path = Tcl_DStringValue(&pathDs);
    req.nextPtr = tsdPtr->pendingPtr;
    tsdPtr->pendingPtr = &req;

    Tcl_DStringInit(&matchDs);
    dbus.error_init(&err);
    AddResponseMatch(conn, &matchDs, req.path, &err);
    if (dbus.error_is_set(&err)) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(err.message,
		TCL_INDEX_NONE));
	dbus.error_free(&err);
	dbus.message_unref(msg);
	goto done;
    }

    reply = CallMethod(interp, msg);
    dbus.message_unref(msg);
    if (reply == NULL) {
	goto removeMatch;
    }

    /*
     * Old portals (before 0.9) ignore handle_token.
     */

    if (dbus.message_iter_init(reply, &iter)
	    && dbus.message_iter_get_arg_type(&iter) == DBUS_TYPE_OBJECT_PATH) {
	dbus.message_iter_get_basic(&iter, &replyPath);
	if (strcmp(replyPath, req.path) != 0) {
	    req.altPath = (char *)Tcl_Alloc(strlen(replyPath) + 1);
	    strcpy(req.altPath, replyPath);
	}
    }
    dbus.message_unref(reply);
    if (req.altPath != NULL) {
	Tcl_DString altMatchDs;

	Tcl_DStringInit(&altMatchDs);
	AddResponseMatch(conn, &altMatchDs, req.altPath, NULL);
	Tcl_DStringFree(&altMatchDs);
	DispatchMessages(tsdPtr);
    }

    /*
     * Wait for the response, processing events meanwhile.
     */

    if (window != NULL) {
	Tk_CreateEventHandler(window, StructureNotifyMask, ParentEventProc,
		&req);
    }
    while (!req.done && !req.parentGone) {
	Tcl_DoOneEvent(TCL_ALL_EVENTS);
    }
    if (req.parentGone) {
	msg = dbus.message_new_method_call(PORTAL_BUS_NAME,
		req.altPath ? req.altPath : req.path, REQUEST_INTERFACE,
		"Close");
	dbus.connection_send(conn, msg, NULL);
	dbus.connection_flush(conn);
	dbus.message_unref(msg);
    } else if (window != NULL) {
	Tk_DeleteEventHandler(window, StructureNotifyMask, ParentEventProc,
		&req);
    }

    if (req.response != NULL || req.parentGone) {
	Tcl_Obj *resultObj = Tcl_NewObj();
	Tcl_Obj *responseObj = NULL, *resultsObj = NULL;

	if (req.response != NULL) {
	    Tcl_Encoding utf8 = Tcl_GetEncoding(NULL, "utf-8");

	    if (dbus.message_iter_init(req.response, &iter)) {
		responseObj = Unmarshal(&iter, utf8);
		if (dbus.message_iter_next(&iter)) {
		    resultsObj = Unmarshal(&iter, utf8);
		}
	    }
	    Tcl_FreeEncoding(utf8);
	    dbus.message_unref(req.response);
	}
	Tcl_DictObjPut(NULL, resultObj,
		Tcl_NewStringObj("response", TCL_INDEX_NONE),
		responseObj ? responseObj : Tcl_NewWideIntObj(2));
	Tcl_DictObjPut(NULL, resultObj,
		Tcl_NewStringObj("results", TCL_INDEX_NONE),
		resultsObj ? resultsObj : Tcl_NewObj());
	Tcl_SetObjResult(interp, resultObj);
	code = TCL_OK;
    } else {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"lost connection to the session bus", TCL_INDEX_NONE));
    }

  removeMatch:
    if (dbus.connection_get_is_connected(conn)) {
	dbus.bus_remove_match(conn, Tcl_DStringValue(&matchDs), NULL);
    }

  done:
    for (prevPtrPtr = &tsdPtr->pendingPtr; *prevPtrPtr != &req;
	    prevPtrPtr = &(*prevPtrPtr)->nextPtr) {
	/* Empty loop body. */
    }
    *prevPtrPtr = req.nextPtr;
    if (req.altPath != NULL) {
	Tcl_Free(req.altPath);
    }
    Tcl_DStringFree(&matchDs);
    Tcl_DStringFree(&pathDs);
    return code;
}

/*
 *----------------------------------------------------------------------
 *
 * Portal_Init --
 *
 *	Creates the ::tk::portal::_* commands if libdbus is available.
 *
 *----------------------------------------------------------------------
 */

int
Portal_Init(
    Tcl_Interp *interp)
{
    if (!Tcl_IsSafe(interp) && LoadDBus(interp)) {
	Tcl_CreateObjCommand2(interp, "::tk::portal::_call", CallCmd,
		NULL, NULL);
	Tcl_CreateObjCommand2(interp, "::tk::portal::_request", RequestCmd,
		NULL, NULL);
	Tcl_CreateObjCommand2(interp, "::tk::portal::_parent", ParentCmd,
		NULL, NULL);
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
