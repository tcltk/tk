/*
 * tkIcu.c --
 *
 * 	tkIcu.c implements various Tk commands which can find
 * 	grapheme cluster and workchar bounderies in Unicode strings.
 *
 * Copyright © 2021 Jan Nijtmans
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

/*
 * Runtime linking of libicu.
 */
typedef enum UBreakIteratorTypex {
	  UBRK_CHARACTERX = 0,
	  UBRK_WORDX = 1
} UBreakIteratorTypex;

typedef enum UErrorCodex {
    U_ZERO_ERRORZ              =  0     /**< No error, no warning. */
} UErrorCodex;

typedef void *(*fn_icu_open)(UBreakIteratorTypex, const char *,
	const uint16_t *, int32_t, UErrorCodex *);
typedef void	(*fn_icu_close)(void *);
typedef int32_t	(*fn_icu_preceding)(void *, int32_t);
typedef int32_t	(*fn_icu_following)(void *, int32_t);
typedef int32_t	(*fn_icu_previous)(void *);
typedef int32_t	(*fn_icu_next)(void *);
typedef void	(*fn_icu_setText)(void *, const void *, int32_t, UErrorCodex *);

static struct {
    size_t				nopen;
    Tcl_LoadHandle		lib;
    fn_icu_open			open;
    fn_icu_close		close;
    fn_icu_preceding	preceding;
    fn_icu_following	following;
    fn_icu_previous	previous;
    fn_icu_next	next;
    fn_icu_setText	setText;
} icu_fns = {
    0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

#define FLAG_WORD 1
#define FLAG_FOLLOWING 4
#define FLAG_SPACE 8

#define icu_open			icu_fns.open
#define icu_close			icu_fns.close
#define icu_preceding		icu_fns.preceding
#define icu_following		icu_fns.following
#define icu_previous		icu_fns.previous
#define icu_next		icu_fns.next
#define icu_setText		icu_fns.setText

TCL_DECLARE_MUTEX(icu_mutex);

static int
startEndOfCmd(
    ClientData clientData,
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    Tcl_DString ds;
    TkSizeT len;
    const char *str;
    UErrorCodex errorCode = U_ZERO_ERRORZ;
    void *it;
    TkSizeT idx;
    int flags = PTR2INT(clientData);

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 1 , objv, "str start");
	return TCL_ERROR;
    }
    Tcl_DStringInit(&ds);
    str = Tcl_GetStringFromObj(objv[1], &len);
    Tcl_UtfToUniCharDString(str, len, &ds);
    len = Tcl_DStringLength(&ds)/2;
    if (TkGetIntForIndex(objv[2], len-1, 0, &idx) != TCL_OK) {
	Tcl_DStringFree(&ds);
	Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad index \"%s\"", Tcl_GetString(objv[2])));
	Tcl_SetErrorCode(interp, "TK", "ICU", "INDEX", NULL);
	return TCL_ERROR;
    }

    it = icu_open((UBreakIteratorTypex)(flags&3), NULL,
    		NULL, -1, &errorCode);
    if (it != NULL) {
	errorCode = U_ZERO_ERRORZ;
	icu_setText(it, (const uint16_t *)Tcl_DStringValue(&ds), len, &errorCode);
    }
    if (it == NULL || errorCode != U_ZERO_ERRORZ) {
    	Tcl_DStringFree(&ds);
    	Tcl_SetObjResult(interp, Tcl_ObjPrintf("cannot open ICU iterator, errocode: %d", (int)errorCode));
    	Tcl_SetErrorCode(interp, "TK", "ICU", "CANNOTOPEN", NULL);
    	return TCL_ERROR;
    }
    if (flags & FLAG_FOLLOWING) {
	idx = icu_following(it, idx);
    } else {
	idx = icu_preceding(it, idx + 1);
    }
    if ((flags & FLAG_WORD) && (idx != (TkSizeT)-1) && !(flags & FLAG_SPACE) ==
	    ((idx >= len) || Tcl_UniCharIsSpace(((const uint16_t *)Tcl_DStringValue(&ds))[idx]))) {
	if (flags & FLAG_FOLLOWING) {
	    idx = icu_next(it);
	} else {
	    idx = icu_previous(it);
	}
    }
    Tcl_SetObjResult(interp, TkNewIndexObj(idx));
    icu_close(it);
    Tcl_DStringFree(&ds);
    return TCL_OK;
}

#ifdef MAC_OSX_TCL
/* Hack, since homebrew doesn't have ICU 68 yet */
#define ICU_VERSION "64"
#else
#define ICU_VERSION "68"
#endif

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
icuCleanup(
    TCL_UNUSED(void *))
{
    Tcl_MutexLock(&icu_mutex);
    if (icu_fns.nopen-- <= 1) {
	if (icu_fns.lib != NULL) {
	    Tcl_FSUnloadFile(NULL, icu_fns.lib);
	}
	memset(&icu_fns, 0, sizeof(icu_fns));
    }
    Tcl_MutexUnlock(&icu_mutex);
}

void
Icu_Init(
    Tcl_Interp *interp)
{
    Tcl_MutexLock(&icu_mutex);

    if (icu_fns.nopen == 0) {
	int i = 0;
	Tcl_Obj *nameobj;
	static const char *iculibs[] = {
#if defined(_WIN32)
	    //"cygicuuc" ICU_VERSION ".dll",
	    "icuuc" ICU_VERSION ".dll",
#elif defined(__CYGWIN__)
	    "cygicuuc" ICU_VERSION ".dll",
#elif defined(MAC_OSX_TCL)
	    "libicuuc." ICU_VERSION ".dylib",
#else
	    "libicuuc.so." ICU_VERSION "",
#endif
	    NULL
	};

#if defined(_WIN32) && !defined(STATIC_BUILD)
	if (!tclStubsPtr->tcl_CreateFileHandler) {
	    /* Not running on Cygwin, so don't try to load the cygwin icu dll */
	    //i++;
	}
#endif
	while (iculibs[i] != NULL) {
	    Tcl_ResetResult(interp);
	    nameobj = Tcl_NewStringObj(iculibs[i], -1);
	    Tcl_IncrRefCount(nameobj);
	    if (Tcl_LoadFile(interp, nameobj, NULL, 0, NULL, &icu_fns.lib)
		    == TCL_OK) {
		Tcl_DecrRefCount(nameobj);
		break;
	    }
	    Tcl_DecrRefCount(nameobj);
	    ++i;
	}
	if (icu_fns.lib != NULL) {
#define ICU_SYM(name)							\
	    icu_fns.name = (fn_icu_ ## name)				\
		Tcl_FindSymbol(NULL, icu_fns.lib, "ubrk_" #name "_" ICU_VERSION);
	    ICU_SYM(open);
	    ICU_SYM(close);
	    ICU_SYM(preceding);
	    ICU_SYM(following);
	    ICU_SYM(previous);
	    ICU_SYM(next);
	    ICU_SYM(setText);
#undef ICU_SYM
	}
    }
    Tcl_MutexUnlock(&icu_mutex);

    if (icu_fns.lib != NULL) {
	Tcl_CreateObjCommand(interp, "::tk::startOfCluster", startEndOfCmd,
		INT2PTR(0), icuCleanup);
	Tcl_CreateObjCommand(interp, "::tk::startOfNextWord", startEndOfCmd,
		INT2PTR(FLAG_WORD|FLAG_FOLLOWING), icuCleanup);
	Tcl_CreateObjCommand(interp, "::tk::startOfPreviousWord", startEndOfCmd,
		INT2PTR(FLAG_WORD), icuCleanup);
	Tcl_CreateObjCommand(interp, "::tk::endOfCluster", startEndOfCmd,
		INT2PTR(FLAG_FOLLOWING), icuCleanup);
	Tcl_CreateObjCommand(interp, "::tk::endOfWord", startEndOfCmd,
		INT2PTR(FLAG_WORD|FLAG_FOLLOWING|FLAG_SPACE), icuCleanup);
    icu_fns.nopen += 5;
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */

