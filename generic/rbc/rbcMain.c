/*
 * rbcInt.c --
 *
 *      This file constructs the basic functionality of the
 *      rbc commands.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

/*
 * -----------------------------------------------------------------------
 *
 * Rbc_Init --
 *
 *      This procedure is invoked to initialize the "rbc" commands.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates the new commands and adds a new entry into a global Tcl
 *      associative array.
 *
 * ------------------------------------------------------------------------
 */
MODULE_SCOPE int
Rbc_Init(
    Tcl_Interp * interp)
{                               /* Base interpreter to return results to. */
    Tcl_Namespace  *nsPtr;

    nsPtr = Tcl_CreateNamespace(interp, "::rbc", NULL, NULL);
    if (nsPtr == NULL) {
        return TCL_ERROR;
    }

    if (Tcl_Export(interp, nsPtr, "vector", 0) != TCL_OK) {
        return TCL_ERROR;
    }

    if (Tcl_Export(interp, nsPtr, "graph", 0) != TCL_OK) {
        return TCL_ERROR;
    }

    if (Tcl_Export(interp, nsPtr, "stripchart", 0) != TCL_OK) {
        return TCL_ERROR;
    }

    if (Tcl_Export(interp, nsPtr, "barchart", 0) != TCL_OK) {
        return TCL_ERROR;
    }

    RbcVectorInit(interp);
    RbcGraphInit(interp);

    if (Tcl_PkgProvide(interp, "rbc", RBC_VERSION) == TCL_ERROR) {
        return TCL_ERROR;
    }

    return TCL_OK;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
