/*
 * Tk widget state utilities.
 *
 * Copyright © 2003 Joe English.  Freely redistributable.
 *
 */

#include "tkInt.h"
#include "ttkTheme.h"

/*
 * Table of state names.
 */
static const struct {
    char name[12];
    int value;
} stateNames[] = {
    {"active", TTK_STATE_ACTIVE},		/* Mouse cursor is over widget or element */
    {"alternate", TTK_STATE_ALTERNATE},	/* Widget-specific alternate display style */
    {"background", TTK_STATE_BACKGROUND},	/* Top-level window lost focus (Mac,Win "inactive") */
    {"disabled", TTK_STATE_DISABLED},		/* Widget is disabled */
    {"focus", TTK_STATE_FOCUS},		/* Widget has keyboard focus */
    {"hover", TTK_STATE_HOVER},		/* Mouse cursor is over widget */
    {"invalid", TTK_STATE_INVALID},		/* Bad value */
    {"pressed", TTK_STATE_PRESSED},		/* Pressed or "armed" */
    {"readonly", TTK_STATE_READONLY},		/* Editing/modification disabled */
    {"selected", TTK_STATE_SELECTED},		/* "on", "true", "current", etc. */
    {"user1", TTK_STATE_USER1},		/* User-definable state */
    {"user2", TTK_STATE_USER2},		/* User-definable state */
    {"user3", TTK_STATE_USER3},		/* User-definable state */
    {"user4", TTK_STATE_USER4},		/* User-definable state */
    {"user5", TTK_STATE_USER5},		/* User-definable state */
    {"user6", TTK_STATE_USER6},		/* User-definable state */
    {"", 0}
};

/*------------------------------------------------------------------------
 * +++ StateSpec object type:
 *
 * The string representation consists of a list of state names,
 * each optionally prefixed by an exclamation point (!).
 *
 * The internal representation uses the upper half of the wideValue
 * to store the on bits and the lower half to store the off bits.
 * If we ever get more than 32 states, this will need to be reconsidered...
 */

static int  StateSpecSetFromAny(Tcl_Interp *interp, Tcl_Obj *obj);
static void StateSpecDupIntRep(Tcl_Obj *, Tcl_Obj *);
static void StateSpecUpdateString(Tcl_Obj *);

static const
TkObjType StateSpecObjType =
{
    {"StateSpec",
    0,
    StateSpecDupIntRep,
    StateSpecUpdateString,
    StateSpecSetFromAny,
    TCL_OBJTYPE_V0},
    0
};

static void StateSpecDupIntRep(Tcl_Obj *srcPtr, Tcl_Obj *copyPtr)
{
    copyPtr->internalRep.wideValue = srcPtr->internalRep.wideValue;
    copyPtr->typePtr = &StateSpecObjType.objType;
}

static int StateSpecSetFromAny(Tcl_Interp *interp, Tcl_Obj *objPtr)
{
    int status;
    Tcl_Size i, objc;
    Tcl_Obj **objv;
    unsigned int onbits = 0, offbits = 0;

    status = Tcl_ListObjGetElements(interp, objPtr, &objc, &objv);
    if (status != TCL_OK)
	return status;

    for (i = 0; i < objc; ++i) {
	const char *stateName = Tcl_GetString(objv[i]);
	int on, j;

	if (*stateName == '!') {
	    ++stateName;
	    on = 0;
	} else {
	    on = 1;
	}

	for (j = 0; stateNames[j].value; ++j) {
	    if (strcmp(stateName, stateNames[j].name) == 0)
		break;
	}

    	if (stateNames[j].value == 0) {
	    if (interp) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"Invalid state name %s", stateName));
		Tcl_SetErrorCode(interp, "TTK", "VALUE", "STATE", NULL);
	    }
	    return TCL_ERROR;
	}

	if (on) {
	    onbits |= stateNames[j].value;
	} else {
	    offbits |= stateNames[j].value;
	}
    }

    /* Invalidate old intrep:
     */
    if (objPtr->typePtr && objPtr->typePtr->freeIntRepProc) {
	objPtr->typePtr->freeIntRepProc(objPtr);
    }

    objPtr->typePtr = &StateSpecObjType.objType;
    objPtr->internalRep.wideValue = ((Tcl_WideInt)onbits << 32) | offbits;

    return TCL_OK;
}

static void StateSpecUpdateString(Tcl_Obj *objPtr)
{
    unsigned int onbits = objPtr->internalRep.wideValue >> 32;
    unsigned int offbits = objPtr->internalRep.wideValue & 0xFFFFFFFFLL;
    unsigned int mask = onbits | offbits;
    Tcl_DString result;
    int i;
    int len;

    Tcl_DStringInit(&result);

    for (i=0; stateNames[i].value; ++i) {
	if (mask & stateNames[i].value) {
	    if (offbits & stateNames[i].value) {
		Tcl_DStringAppend(&result, "!", 1);
	    }
	    Tcl_DStringAppend(&result, stateNames[i].name, TCL_INDEX_NONE);
	    Tcl_DStringAppend(&result, " ", 1);
	}
    }

    len = Tcl_DStringLength(&result);
    if (len) {
	/* 'len' includes extra trailing ' ' */
	objPtr->bytes = (char *)ckalloc(len);
	objPtr->length = len-1;
	strncpy(objPtr->bytes, Tcl_DStringValue(&result), len-1);
	objPtr->bytes[len-1] = '\0';
    } else {
	/* empty string */
	objPtr->length = 0;
	objPtr->bytes = (char *)ckalloc(1);
	*objPtr->bytes = '\0';
    }

    Tcl_DStringFree(&result);
}

Tcl_Obj *Ttk_NewStateSpecObj(unsigned int onbits, unsigned int offbits)
{
    Tcl_Obj *objPtr = Tcl_NewObj();

    Tcl_InvalidateStringRep(objPtr);
    objPtr->typePtr = &StateSpecObjType.objType;
    objPtr->internalRep.wideValue = ((Tcl_WideInt)onbits << 32) | offbits;

    return objPtr;
}

int Ttk_GetStateSpecFromObj(
    Tcl_Interp *interp,
    Tcl_Obj *objPtr,
    Ttk_StateSpec *spec)
{
    if (objPtr->typePtr != &StateSpecObjType.objType) {
	int status = StateSpecSetFromAny(interp, objPtr);
	if (status != TCL_OK)
	    return status;
    }

    spec->onbits = objPtr->internalRep.wideValue >> 32;
    spec->offbits = objPtr->internalRep.wideValue & 0xFFFFFFFFLL;
    return TCL_OK;
}


/*
 * Tk_StateMapLookup --
 *
 * 	A state map is a paired list of StateSpec / value pairs.
 *	Returns the value corresponding to the first matching state
 *	specification, or NULL if not found or an error occurs.
 */
Tcl_Obj *Ttk_StateMapLookup(
    Tcl_Interp *interp,		/* Where to leave error messages; may be NULL */
    Ttk_StateMap map,		/* State map */
    Ttk_State state)    	/* State to look up */
{
    Tcl_Obj **specs;
    Tcl_Size j, nSpecs;
    int status;

    status = Tcl_ListObjGetElements(interp, map, &nSpecs, &specs);
    if (status != TCL_OK)
	return NULL;

    for (j = 0; j < nSpecs; j += 2) {
	Ttk_StateSpec spec;
	status = Ttk_GetStateSpecFromObj(interp, specs[j], &spec);
	if (status != TCL_OK)
	    return NULL;
	if (Ttk_StateMatches(state, &spec))
	    return specs[j+1];
    }
    if (interp) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("No match in state map", -1));
	Tcl_SetErrorCode(interp, "TTK", "STATE", "UNMATCHED", NULL);
    }
    return NULL;
}

/* Ttk_GetStateMapFromObj --
 * 	Returns a Ttk_StateMap from a Tcl_Obj*.
 * 	Since a Ttk_StateMap is just a specially-formatted Tcl_Obj,
 * 	this basically just checks for errors.
 */
Ttk_StateMap Ttk_GetStateMapFromObj(
    Tcl_Interp *interp,		/* Where to leave error messages; may be NULL */
    Tcl_Obj *mapObj)		/* State map */
{
    Tcl_Obj **specs;
    Tcl_Size j, nSpecs;
    int status;

    status = Tcl_ListObjGetElements(interp, mapObj, &nSpecs, &specs);
    if (status != TCL_OK)
	return NULL;

    if (nSpecs % 2 != 0) {
	if (interp) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "State map must have an even number of elements", -1));
	    Tcl_SetErrorCode(interp, "TTK", "VALUE", "STATEMAP", NULL);
	}
	return 0;
    }

    for (j = 0; j < nSpecs; j += 2) {
	Ttk_StateSpec spec;
	if (Ttk_GetStateSpecFromObj(interp, specs[j], &spec) != TCL_OK)
	    return NULL;
    }

    return mapObj;
}

/*
 * Ttk_StateTableLooup --
 * 	Look up an index from a statically allocated state table.
 */
int Ttk_StateTableLookup(const Ttk_StateTable *map, Ttk_State state)
{
    while ((state & map->onBits) != map->onBits
	    || (~state & map->offBits) != map->offBits)
    {
	++map;
    }
    return map->index;
}

/*EOF*/
