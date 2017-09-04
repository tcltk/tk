/*
 * tkAppInit.c --
 *
 *	Provides a default version of the main program and Tcl_AppInit
 *	procedure for wish and other Tk-based applications.
 *
 * Copyright (c) 1993 The Regents of the University of California.
 * Copyright (c) 1994-1997 Sun Microsystems, Inc.
 * Copyright (c) 1998-1999 Scriptics Corporation.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tk.h"

#ifdef ANDROID
#include <time.h>
#include <sys/system_properties.h>
#include <jni.h>
#endif

#ifdef TK_TEST
extern Tcl_PackageInitProc Tktest_Init;
#endif /* TK_TEST */

#ifdef PLATFORM_SDL
#include <SDL.h>
#ifdef ANDROID
#include <android/log.h>
#undef  main
#define main SDL_main
#endif
#endif

#ifdef _WIN32
#include <windows.h>
#endif

/*
 * The following #if block allows you to change the AppInit function by using
 * a #define of TCL_LOCAL_APPINIT instead of rewriting this entire file. The
 * #if checks for that #define and uses Tcl_AppInit if it doesn't exist.
 */

#ifndef TK_LOCAL_APPINIT
#define TK_LOCAL_APPINIT Tcl_AppInit
#endif
#ifndef MODULE_SCOPE
#   define MODULE_SCOPE extern
#endif
MODULE_SCOPE int TK_LOCAL_APPINIT(Tcl_Interp *);
MODULE_SCOPE int main(int, char **);

/*
 * The following #if block allows you to change how Tcl finds the startup
 * script, prime the library or encoding paths, fiddle with the argv, etc.,
 * without needing to rewrite Tk_Main()
 */

#ifdef TK_LOCAL_MAIN_HOOK
MODULE_SCOPE int TK_LOCAL_MAIN_HOOK(int *argc, char ***argv);
#endif

/* Make sure the stubbed variants of those are never used. */
#undef Tcl_ObjSetVar2
#undef Tcl_NewStringObj

#ifdef __APPLE__
struct ThreadStartup {
    int argc;
    char **argv;
};
#endif


/*
 *----------------------------------------------------------------------
 *
 * AndroidPanic, WindowsPanic --
 *
 *	Panic proc when running on Android or Windows
 *
 *----------------------------------------------------------------------
 */

#ifdef ANDROID
TCL_NORETURN static void
AndroidPanic(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_FATAL, "AndroWish", fmt, args);
    va_end(args);
    abort();
}
#endif

#ifdef _WIN32
TCL_NORETURN static void
WindowsPanic(const char *fmt, ...)
{
    va_list args;
    char buf[1024];

    va_start(args, fmt);
    buf[sizeof (buf) - 2] = '\0';
    buf[sizeof (buf) - 1] = '\0';
    vsnprintf(buf, sizeof (buf) - 1, fmt, args);
    if (buf[sizeof (buf) - 2] != '\0') {
	memcpy(buf + sizeof (buf) - 5, " ...", 5);
    }
    MessageBeep(MB_ICONEXCLAMATION);
    MessageBoxA(NULL, buf, "Fatal Error",
	MB_ICONSTOP | MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
    va_end(args);
    abort();
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * GetPackageCodePath  --
 *
 *	Retrieve path name of APK file.
 *
 * Results:
 *	String with name of APK file, or NULL on error.
 *
 * Side effects:
 *	Memory is allocated.
 *
 *----------------------------------------------------------------------
 */

#ifdef ANDROID
static char *
GetPackageCodePath(void)
{
    jmethodID mid;
    jstring pathString;
    CONST char *path;
    char *result = NULL;
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject context = (jobject) SDL_AndroidGetActivity();

    /* pathString = context.getPackageCodePath(); */
    mid = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, context),
			      "getPackageCodePath", "()Ljava/lang/String;");
    pathString = (*env)->CallObjectMethod(env, context, mid);
    if (pathString) {
	path = (*env)->GetStringUTFChars(env, pathString, NULL);
	result = strdup(path);
	(*env)->ReleaseStringUTFChars(env, pathString, path);
	(*env)->DeleteLocalRef(env, pathString);
    }
    return result;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * GetPackageName  --
 *
 *	Retrieve Java package name of this application.
 *
 * Results:
 *	String with package name, or NULL on error.
 *
 * Side effects:
 *	Memory is allocated.
 *
 *----------------------------------------------------------------------
 */

#ifdef ANDROID
static char *
GetPackageName(void)
{
    jmethodID mid;
    jstring pkgString;
    CONST char *pkgName;
    char *result = NULL;
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject context = (jobject) SDL_AndroidGetActivity();

    /* pkgString = context.getPackageName(); */
    mid = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, context),
			      "getPackageName", "()Ljava/lang/String;");
    pkgString = (*env)->CallObjectMethod(env, context, mid);
    if (pkgString) {
	pkgName = (*env)->GetStringUTFChars(env, pkgString, NULL);
	result = strdup(pkgName);
	(*env)->ReleaseStringUTFChars(env, pkgString, pkgName);
	(*env)->DeleteLocalRef(env, pkgString);
    }
    return result;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * GetExternalStorageDirectory --
 *
 *	Retrieve external storage directory.
 *
 * Results:
 *	String with directory name, or NULL on error.
 *
 * Side effects:
 *	Memory is allocated.
 *
 *----------------------------------------------------------------------
 */

#ifdef ANDROID
static char *
GetExternalStorageDirectory(void)
{
    jmethodID mid;
    jstring pathString;
    CONST char *path;
    char *result = NULL;
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jclass clazz;
    jobject jfile;

    clazz = (*env)->FindClass(env, "android/os/Environment");
    mid = (*env)->GetStaticMethodID(env, clazz,
				    "getExternalStorageDirectory",
				    "()Ljava/io/File;");
    jfile = (*env)->CallStaticObjectMethod(env, clazz, mid);
    if (jfile) {
	mid = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, jfile),
				  "getAbsolutePath", "()Ljava/lang/String;");
	pathString = (*env)->CallObjectMethod(env, jfile, mid);
	if (pathString) {
	    path = (*env)->GetStringUTFChars(env, pathString, NULL);
	    result = strdup(path);
	    (*env)->ReleaseStringUTFChars(env, pathString, path);
	    (*env)->DeleteLocalRef(env, pathString);
	}
	(*env)->DeleteLocalRef(env, jfile);
    }
    return result;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * GetOBBDir --
 *
 *	Retrieve application's directory for OBB files.
 *
 * Results:
 *	String with directory name, or NULL on error.
 *
 * Side effects:
 *	Memory is allocated.
 *
 *----------------------------------------------------------------------
 */

#ifdef ANDROID
static char *
GetOBBDir(void)
{
    jmethodID mid;
    jstring pathString;
    CONST char *path;
    char *result = NULL;
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject context = (jobject) SDL_AndroidGetActivity();
    jobject jfile;
    jthrowable exc;

    mid = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, context),
			      "getObbDir", "()Ljava/io/File;");
    if (mid) {
	jfile = (*env)->CallObjectMethod(env, context, mid);
	if (jfile) {
	    mid = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, jfile),
				      "getAbsolutePath",
				      "()Ljava/lang/String;");
	    pathString = (*env)->CallObjectMethod(env, jfile, mid);
	    if (pathString) {
		path = (*env)->GetStringUTFChars(env, pathString, NULL);
		result = strdup(path);
		(*env)->ReleaseStringUTFChars(env, pathString, path);
		(*env)->DeleteLocalRef(env, pathString);
	    }
	    (*env)->DeleteLocalRef(env, jfile);
	}
    }
    exc = (*env)->ExceptionOccurred(env);
    if (exc) {
	(*env)->DeleteLocalRef(env, exc);
	(*env)->ExceptionClear(env);
    }
    return result;
}
#endif

#ifdef __APPLE__
/*
 *----------------------------------------------------------------------
 *
 * TkMainThread --
 *
 *	This is the thread body of the main program for the application.
 *
 *----------------------------------------------------------------------
 */

static Tcl_ThreadCreateType
TkMainThread(ClientData clientData)
{
    struct ThreadStartup *stPtr = (struct ThreadStartup *) clientData;

    Tk_MainEx(stPtr->argc, stPtr->argv, TK_LOCAL_APPINIT, Tcl_CreateInterp());
    exit(4);
    TCL_THREAD_CREATE_RETURN;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * main --
 *
 *	This is the main program for the application.
 *
 * Results:
 *	None: Tk_Main never returns here, so this procedure never returns
 *	either.
 *
 * Side effects:
 *	Just about anything, since from here we call arbitrary Tcl code.
 *
 *----------------------------------------------------------------------
 */

int
main(
    int argc,			/* Number of command-line arguments. */
    char **argv)		/* Values of command-line arguments. */
{
#if defined(ANDROID) && defined(PLATFORM_SDL)
    const char *path, *temp;
#endif
#ifdef __APPLE__
    Tcl_ThreadId thrId;
    struct ThreadStartup startup;
    extern void SdlTkEventThread(void);
#endif    

#ifdef TK_LOCAL_MAIN_HOOK
    TK_LOCAL_MAIN_HOOK(&argc, &argv);
#endif
#ifdef ANDROID
    Tcl_InitSubsystems(AndroidPanic);

    /* workaround to fix mktime(3) issues at DST boundaries */
    if (getenv("TZ") == NULL) {
	static char tzbuf[PROP_VALUE_MAX + 8];

	strcpy(tzbuf, "TZ=");
	if (__system_property_get("persist.sys.timezone", tzbuf + 3) > 0) {
	    putenv(tzbuf);
	    tzset();
	}
    }
#endif
#ifdef _WIN32
    Tcl_InitSubsystems(WindowsPanic);
#endif
#if defined(ANDROID) && defined(PLATFORM_SDL)
    path = SDL_AndroidGetInternalStoragePath();
    temp = SDL_AndroidGetTempStoragePath();
    if (temp != NULL) {
	/* to be able to write temporary files */
	setenv("TMPDIR", temp, 1);
    }
    if (path != NULL) {
	const char *npath;
	char *newpath;
	int len;

	setenv("INTERNAL_STORAGE", path, 1);
	if (temp == NULL) {
	    /* to be able to write temporary files */
	    setenv("TMPDIR", path, 1);
	}
	/* to have a home */
	setenv("HOME", path, 1);
	/* enhance LD_LIBRARY_PATH */
	len = 16 + strlen(path);
	npath = getenv("LD_LIBRARY_PATH");
	if ((npath != NULL) && (npath[0] != '\0')) {
	    len += strlen(npath);
	}
	newpath = malloc(len);
	if (newpath != NULL) {
	    newpath[0] = '\0';
	    if ((npath != NULL) && (npath[0] != '\0')) {
		strcpy(newpath, npath);
		strcat(newpath, ":");
	    }
	    strcat(newpath, path);
	    npath = strrchr(newpath, '/');
	    if (npath != NULL) {
		strcpy((char *) npath, "/lib");
	    }
	    setenv("LD_LIBRARY_PATH", newpath, 1);
	    free(newpath);
	}
	/* enhance PATH */
	len = 16 + strlen(path);
	npath = getenv("PATH");
	if ((npath != NULL) && (npath[0] != '\0')) {
	    len += strlen(npath);
	}
	newpath = malloc(len);
	if (newpath != NULL) {
	    newpath[0] = '\0';
	    if ((npath != NULL) && (npath[0] != '\0')) {
		strcpy(newpath, npath);
		strcat(newpath, ":");
	    }
	    strcat(newpath, path);
	    npath = strrchr(newpath, '/');
	    if (npath != NULL) {
		strcpy((char *) npath, "/lib");
	    }
	    setenv("PATH", newpath, 1);
	    free(newpath);
	}
    }
    /* SDL misnomer: this is the path to external files */
    path = SDL_AndroidGetExternalStoragePath();
    if (path != NULL) {
	setenv("EXTERNAL_FILES", path, 1);
    }
    path = GetPackageCodePath();
    if (path != NULL) {
	setenv("PACKAGE_CODE_PATH", path, 1);
    }
    path = GetPackageName();
    if (path != NULL) {
	setenv("PACKAGE_NAME", path, 1);
    }
    path = GetExternalStorageDirectory();
    if (path != NULL) {
	setenv("EXTERNAL_STORAGE", path, 1);
    }
    path = GetOBBDir();
    if (path != NULL) {
	setenv("OBB_DIR", path, 1);
    }

    /* On Android, argv[0] is not usable. */
    argv[0] = strdup("wish");
#else
    Tcl_FindExecutable(argv[0]);
#endif
#ifdef __APPLE__
    /*
     * We need the SDL event handling run in the main thread,
     * which seems to be a Cocoa requirement. Therefore Tk_MainEx()
     * is run in a seperate thread which is started now.
     */

    startup.argc = argc;
    startup.argv = argv;
    if (Tcl_CreateThread(&thrId, TkMainThread, &startup,
	    TCL_THREAD_STACK_DEFAULT, TCL_THREAD_NOFLAGS) != TCL_OK) {
	Tcl_Panic("unable to start Tk main thread");
    }

    /*
     * Perform SDL event handling, screen refresh, etc.
     */

    SdlTkEventThread();
#else
    Tk_MainEx(argc, argv, TK_LOCAL_APPINIT, Tcl_CreateInterp());
#endif
    return 0;			/* Needed only to prevent compiler warning. */
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_AppInit --
 *
 *	This procedure performs application-specific initialization. Most
 *	applications, especially those that incorporate additional packages,
 *	will have their own version of this procedure.
 *
 * Results:
 *	Returns a standard Tcl completion code, and leaves an error message in
 *	the interp's result if an error occurs.
 *
 * Side effects:
 *	Depends on the startup script.
 *
 *----------------------------------------------------------------------
 */

int
Tcl_AppInit(
    Tcl_Interp *interp)		/* Interpreter for application. */
{
#if defined(ANDROID) || defined(_WIN32)
    putenv("DISPLAY=:0.0");
#endif

    if ((Tcl_Init)(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }

    if (Tk_Init(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }
    Tcl_StaticPackage(interp, "Tk", Tk_Init, Tk_SafeInit);

#ifdef TK_TEST
    if (Tktest_Init(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }
    Tcl_StaticPackage(interp, "Tktest", Tktest_Init, 0);
#endif /* TK_TEST */

    /*
     * Call the init procedures for included packages. Each call should look
     * like this:
     *
     * if (Mod_Init(interp) == TCL_ERROR) {
     *     return TCL_ERROR;
     * }
     *
     * where "Mod" is the name of the module. (Dynamically-loadable packages
     * should have the same entry-point name.)
     */

#ifdef PLATFORM_SDL
    if (Tk_CreateConsoleWindow(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }
#endif

    /*
     * Call Tcl_CreateObjCommand for application-specific commands, if they
     * weren't already created by the init procedures called above.
     */

    /*
     * Specify a user-specific startup file to invoke if the application is
     * run interactively. Typically the startup file is "~/.apprc" where "app"
     * is the name of the application. If this line is deleted then no user-
     * specific startup file will be run under any conditions.
     */

    Tcl_ObjSetVar2(interp, Tcl_NewStringObj("tcl_rcFileName", -1), NULL,
	    Tcl_NewStringObj("~/.wishrc", -1), TCL_GLOBAL_ONLY);
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
