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

static struct {
    int				nopen;
    Tcl_LoadHandle		lib;
    fn_icu_open			open;
    fn_icu_close		close;
    fn_icu_preceding	preceding;
    fn_icu_following	following;
} icu_fns = {
    0, NULL, NULL, NULL, NULL, NULL
};

#define icu_open			icu_fns.open
#define icu_close			icu_fns.close
#define icu_preceding		icu_fns.preceding
#define icu_following		icu_fns.following

TCL_DECLARE_MUTEX(icu_mutex);

int
Icu_Init(
    Tcl_Interp *interp)
{
    Tcl_MutexLock(&icu_mutex);
    if (icu_fns.nopen == 0) {
	int i = 0;
	Tcl_Obj *nameobj;
	static const char *iculibs[] = {
	    "libicuuc68.so",
	    NULL
	};

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
		Tcl_FindSymbol(NULL, icu_fns.lib, "ubrk_" #name "_86")
	    ICU_SYM(open);
	    ICU_SYM(close);
	    ICU_SYM(preceding);
	    ICU_SYM(following);
#undef ICU_SYM
	}
    }
    icu_fns.nopen++;
    Tcl_MutexUnlock(&icu_mutex);

    //Tcl_CreateObjCommand(interp, "::tk::endOfCluster", endOfClusterCmd,
	//    interp, NULL);
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

