/* 
 * tkBitmap.c --
 *
 *	This file maintains a database of read-only bitmaps for the Tk
 *	toolkit.  This allows bitmaps to be shared between widgets and
 *	also avoids interactions with the X server.
 *
 * Copyright (c) 1990-1994 The Regents of the University of California.
 * Copyright (c) 1994-1998 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * SCCS: @(#) tkBitmap.c 1.56 98/01/19 11:47:55
 */

#include "tkPort.h"
#include "tkInt.h"

/*
 * The includes below are for pre-defined bitmaps.
 *
 * Platform-specific issue: Windows complains when the bitmaps are
 * included, because an array of characters is being initialized with
 * integers as elements.  For lint purposes, the following pragmas
 * temporarily turn off that warning message.
 */

#if defined(__WIN32__) || defined(_WIN32)
#pragma warning (disable : 4305)
#endif

#include "error.bmp"
#include "gray12.bmp"
#include "gray25.bmp"
#include "gray50.bmp"
#include "gray75.bmp"
#include "hourglass.bmp"
#include "info.bmp"
#include "questhead.bmp"
#include "question.bmp"
#include "warning.bmp"

#if defined(__WIN32__) || defined(_WIN32)
#pragma warning (default : 4305)
#endif

/*
 * One of the following data structures exists for each bitmap that is
 * currently in use.  Each structure is indexed with both "idTable" and
 * "nameTable".
 */

typedef struct TkBitmap {
    Pixmap bitmap;		/* X identifier for bitmap.  None means this
				 * bitmap was created by Tk_DefineBitmap
				 * and it isn't currently in use. */
    int width, height;		/* Dimensions of bitmap. */
    Display *display;		/* Display for which bitmap is valid. */
    int resourceRefCount;	/* Number of active uses of this bitmap (each
				 * active use corresponds to a call to
				 * Tk_AllocBitmapFromObj or Tk_GetBitmap).
				 * If this count is 0, then this TkBitmap
				 * structure is no longer valid and it isn't
				 * present in nameTable: it is being kept
				 * around only because there are objects
				 * referring to it.  The structure is freed
				 * when resourceRefCount and objRefCount
				 * are both 0. */
    int objRefCount;		/* Number of Tcl_Obj's that reference
				 * this structure. */
    Tcl_HashEntry *nameHashPtr;	/* Entry in nameTable for this structure
				 * (needed when deleting). */
    Tcl_HashEntry *idHashPtr;	/* Entry in idTable for this structure
				 * (needed when deleting). */
    struct TkBitmap *nextPtr;	/* Points to the next TkBitmap structure with
				 * the same name.  All bitmaps with the
				 * same name (but different displays) are
				 * chained together off a single entry in
				 * nameTable. */
} TkBitmap;

/*
 * Hash table to map from a textual name for a bitmap to the
 * first TkBitmap record for that name:
 */

static Tcl_HashTable nameTable;

/*
 * Hash table that maps from <display + bitmap id> to the TkBitmap structure
 * for the bitmap.  This table is used by Tk_FreeBitmap.
 */

static Tcl_HashTable idTable;
typedef struct {
    Display *display;		/* Display for which bitmap was allocated. */
    Pixmap pixmap;		/* X identifier for pixmap. */
} IdKey;

/*
 * Hash table created by Tk_DefineBitmap to map from a name to a
 * collection of in-core data about a bitmap.  The table is
 * indexed by the address of the data for the bitmap, and the entries
 * contain pointers to TkPredefBitmap structures.
 */

Tcl_HashTable tkPredefBitmapTable;

/*
 * Hash table used by Tk_GetBitmapFromData to map from a collection
 * of in-core data about a bitmap to a reference giving an automatically-
 * generated name for the bitmap:
 */

static Tcl_HashTable dataTable;
typedef struct {
    char *source;		/* Bitmap bits. */
    int width, height;		/* Dimensions of bitmap. */
} DataKey;

static int initialized = 0;	/* 0 means static structures haven't been
				 * initialized yet. */

/*
 * Forward declarations for procedures defined in this file:
 */

static void		BitmapInit _ANSI_ARGS_((void));
static void		DupBitmapObjProc _ANSI_ARGS_((Tcl_Obj *srcObjPtr,
			    Tcl_Obj *dupObjPtr));
static void		FreeBitmap _ANSI_ARGS_((TkBitmap *bitmapPtr));
static void		FreeBitmapObjProc _ANSI_ARGS_((Tcl_Obj *objPtr));
static TkBitmap *	GetBitmap _ANSI_ARGS_((Tcl_Interp *interp,
			    Tk_Window tkwin, char *name));
static TkBitmap *	GetBitmapFromObj _ANSI_ARGS_((Tk_Window tkwin,
			    Tcl_Obj *objPtr));
static void		InitBitmapObj _ANSI_ARGS_((Tcl_Obj *objPtr));

/*
 * The following structure defines the implementation of the "bitmap" Tcl
 * object, which maps a string bitmap name to a TkBitmap object.  The
 * ptr1 field of the Tcl_Obj points to a TkBitmap object.
 */

static Tcl_ObjType bitmapObjType = {
    "bitmap",			/* name */
    FreeBitmapObjProc,		/* freeIntRepProc */
    DupBitmapObjProc,		/* dupIntRepProc */
    NULL,			/* updateStringProc */
    NULL			/* setFromAnyProc */
};

/*
 *----------------------------------------------------------------------
 *
 * Tk_AllocBitmapFromObj --
 *
 *	Given a Tcl_Obj *, map the value to a corresponding
 *	Pixmap structure based on the tkwin given.
 *
 * Results:
 *	The return value is the X identifer for the desired bitmap
 *	(i.e. a Pixmap with a single plane), unless string couldn't be
 *	parsed correctly.  In this case, None is returned and an error
 *	message is left in the interp's result.  The caller should never
 *	modify the bitmap that is returned, and should eventually call
 *	Tk_FreeBitmapFromObj when the bitmap is no longer needed.
 *
 * Side effects:
 *	The bitmap is added to an internal database with a reference count.
 *	For each call to this procedure, there should eventually be a call
 *	to Tk_FreeBitmapFromObj, so that the database can be cleaned up 
 *	when bitmaps aren't needed anymore.
 *
 *----------------------------------------------------------------------
 */

Pixmap
Tk_AllocBitmapFromObj(interp, tkwin, objPtr)
    Tcl_Interp *interp;		/* Interp for error results. This may 
				 * be NULL. */
    Tk_Window tkwin;		/* Need the screen the bitmap is used on.*/
    Tcl_Obj *objPtr;		/* Object describing bitmap; see manual
				 * entry for legal syntax of string value. */
{
    TkBitmap *bitmapPtr;

    if (objPtr->typePtr != &bitmapObjType) {
	InitBitmapObj(objPtr);
    }
    bitmapPtr = (TkBitmap *) objPtr->internalRep.twoPtrValue.ptr1;

    /*
     * If the object currently points to a TkBitmap, see if it's the
     * one we want.  If so, increment its reference count and return.
     */

    if (bitmapPtr != NULL) {
	if (bitmapPtr->resourceRefCount == 0) {
	    /*
	     * This is a stale reference: it refers to a TkBitmap that's
	     * no longer in use.  Clear the reference.
	     */

	    FreeBitmapObjProc(objPtr);
	    bitmapPtr = NULL;
	} else if (Tk_Display(tkwin) == bitmapPtr->display) {
	    bitmapPtr->resourceRefCount++;
	    return bitmapPtr->bitmap;
	}
    }

    /*
     * The object didn't point to the TkBitmap that we wanted.  Search
     * the list of TkBitmaps with the same name to see if one of the
     * others is the right one.
     */

    if (bitmapPtr != NULL) {
	TkBitmap *firstBitmapPtr =
		(TkBitmap *) Tcl_GetHashValue(bitmapPtr->nameHashPtr);
	FreeBitmapObjProc(objPtr);
	for (bitmapPtr = firstBitmapPtr; bitmapPtr != NULL;
		bitmapPtr = bitmapPtr->nextPtr) {
	    if (Tk_Display(tkwin) == bitmapPtr->display) {
		bitmapPtr->resourceRefCount++;
		bitmapPtr->objRefCount++;
		objPtr->internalRep.twoPtrValue.ptr1 = (VOID *) bitmapPtr;
		return bitmapPtr->bitmap;
	    }
	}
    }

    /*
     * Still no luck.  Call GetBitmap to allocate a new TkBitmap object.
     */

    bitmapPtr = GetBitmap(interp, tkwin, Tcl_GetString(objPtr));
    objPtr->internalRep.twoPtrValue.ptr1 = (VOID *) bitmapPtr;
    if (bitmapPtr == NULL) {
	return None;
    }
    bitmapPtr->objRefCount++;
    return bitmapPtr->bitmap;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetBitmap --
 *
 *	Given a string describing a bitmap, locate (or create if necessary)
 *	a bitmap that fits the description.
 *
 * Results:
 *	The return value is the X identifer for the desired bitmap
 *	(i.e. a Pixmap with a single plane), unless string couldn't be
 *	parsed correctly.  In this case, None is returned and an error
 *	message is left in the interp's result.  The caller should never
 *	modify the bitmap that is returned, and should eventually call
 *	Tk_FreeBitmap when the bitmap is no longer needed.
 *
 * Side effects:
 *	The bitmap is added to an internal database with a reference count.
 *	For each call to this procedure, there should eventually be a call
 *	to Tk_FreeBitmap, so that the database can be cleaned up when bitmaps
 *	aren't needed anymore.
 *
 *----------------------------------------------------------------------
 */

Pixmap
Tk_GetBitmap(interp, tkwin, string)
    Tcl_Interp *interp;		/* Interpreter to use for error reporting,
				 * this may be NULL. */
    Tk_Window tkwin;		/* Window in which bitmap will be used. */
    char *string;		/* Description of bitmap.  See manual entry
				 * for details on legal syntax. */
{
    TkBitmap *bitmapPtr = GetBitmap(interp, tkwin, string);
    if (bitmapPtr == NULL) {
	return None;
    }
    return bitmapPtr->bitmap;
}

/*
 *----------------------------------------------------------------------
 *
 * GetBitmap --
 *
 *	Given a string describing a bitmap, locate (or create if necessary)
 *	a bitmap that fits the description. This routine returns the
 *	internal data structure for the bitmap. This avoids extra
 *	hash table lookups in Tk_AllocBitmapFromObj.
 *
 * Results:
 *	The return value is the X identifer for the desired bitmap
 *	(i.e. a Pixmap with a single plane), unless string couldn't be
 *	parsed correctly.  In this case, None is returned and an error
 *	message is left in the interp's result.  The caller should never
 *	modify the bitmap that is returned, and should eventually call
 *	Tk_FreeBitmap when the bitmap is no longer needed.
 *
 * Side effects:
 *	The bitmap is added to an internal database with a reference count.
 *	For each call to this procedure, there should eventually be a call
 *	to Tk_FreeBitmap or Tk_FreeBitmapFromObj, so that the database can
 *	be cleaned up when bitmaps aren't needed anymore.
 *
 *----------------------------------------------------------------------
 */

static TkBitmap *
GetBitmap(interp, tkwin, string)
    Tcl_Interp *interp;		/* Interpreter to use for error reporting,
				 * this may be NULL. */
    Tk_Window tkwin;		/* Window in which bitmap will be used. */
    char *string;		/* Description of bitmap.  See manual entry
				 * for details on legal syntax. */
{
    IdKey idKey;
    Tcl_HashEntry *nameHashPtr, *predefHashPtr;
    TkBitmap *bitmapPtr, *existingBitmapPtr;
    TkPredefBitmap *predefPtr;
    int new;
    Pixmap bitmap;
    int width, height;
    int dummy2;

    if (!initialized) {
	BitmapInit();
    }

    nameHashPtr = Tcl_CreateHashEntry(&nameTable, string, &new);
    if (!new) {
	existingBitmapPtr = (TkBitmap *) Tcl_GetHashValue(nameHashPtr);
	for (bitmapPtr = existingBitmapPtr; bitmapPtr != NULL;
		bitmapPtr = bitmapPtr->nextPtr) {
	    if (Tk_Display(tkwin) == bitmapPtr->display) {
		bitmapPtr->resourceRefCount++;
		return bitmapPtr;
	    }
	}
    } else {
	existingBitmapPtr = NULL;
    }

    /*
     * No suitable bitmap exists.  Create a new bitmap from the
     * information contained in the string.  If the string starts
     * with "@" then the rest of the string is a file name containing
     * the bitmap.  Otherwise the string must refer to a bitmap
     * defined by a call to Tk_DefineBitmap.
     */

    if (*string == '@') {
	Tcl_DString buffer;
	int result;

        if (Tcl_IsSafe(interp)) {
            Tcl_AppendResult(interp, "can't specify bitmap with '@' in a",
                    " safe interpreter", (char *) NULL);
            goto error;
        }
        
	string = Tcl_TranslateFileName(interp, string + 1, &buffer);
	if (string == NULL) {
	    goto error;
	}
	result = XReadBitmapFile(Tk_Display(tkwin),
		RootWindowOfScreen(Tk_Screen(tkwin)), string,
		(unsigned int *) &width, (unsigned int *) &height,
		&bitmap, &dummy2, &dummy2);
	if (result != BitmapSuccess) {
	    if (interp != NULL) {
		Tcl_AppendResult(interp, "error reading bitmap file \"", string,
		    "\"", (char *) NULL);
	    }
	    Tcl_DStringFree(&buffer);
	    goto error;
	}
	Tcl_DStringFree(&buffer);
    } else {
	predefHashPtr = Tcl_FindHashEntry(&tkPredefBitmapTable, string);
	if (predefHashPtr == NULL) {
	    /*
	     * The following platform specific call allows the user to
	     * define bitmaps that may only exist during run time.  If
	     * it returns None nothing was found and we return the error.
	     */
	    bitmap = TkpGetNativeAppBitmap(Tk_Display(tkwin), string,
		    &width, &height);
	    
	    if (bitmap == None) {
		if (interp != NULL) {
		    Tcl_AppendResult(interp, "bitmap \"", string,
			"\" not defined", (char *) NULL);
		}
		goto error;
	    }
	} else {
	    predefPtr = (TkPredefBitmap *) Tcl_GetHashValue(predefHashPtr);
	    width = predefPtr->width;
	    height = predefPtr->height;
	    if (predefPtr->native) {
		bitmap = TkpCreateNativeBitmap(Tk_Display(tkwin),
		    predefPtr->source);
		if (bitmap == None) {
		    panic("native bitmap creation failed");
		}
	    } else {
		bitmap = XCreateBitmapFromData(Tk_Display(tkwin),
		    RootWindowOfScreen(Tk_Screen(tkwin)), 
		    predefPtr->source,
		    (unsigned) width, (unsigned) height);
	    }
	}
    }

    /*
     * Add information about this bitmap to our database.
     */

    bitmapPtr = (TkBitmap *) ckalloc(sizeof(TkBitmap));
    bitmapPtr->bitmap = bitmap;
    bitmapPtr->width = width;
    bitmapPtr->height = height;
    bitmapPtr->display = Tk_Display(tkwin);
    bitmapPtr->resourceRefCount = 1;
    bitmapPtr->objRefCount = 0;
    bitmapPtr->nameHashPtr = nameHashPtr;
    idKey.display = bitmapPtr->display;
    idKey.pixmap = bitmap;
    bitmapPtr->idHashPtr = Tcl_CreateHashEntry(&idTable, (char *) &idKey,
	    &new);
    if (!new) {
	panic("bitmap already registered in Tk_GetBitmap");
    }
    bitmapPtr->nextPtr = existingBitmapPtr;
    Tcl_SetHashValue(nameHashPtr, bitmapPtr);
    Tcl_SetHashValue(bitmapPtr->idHashPtr, bitmapPtr);
    return bitmapPtr;

    error:
    if (new) {
	Tcl_DeleteHashEntry(nameHashPtr);
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_DefineBitmap --
 *
 *	This procedure associates a textual name with a binary bitmap
 *	description, so that the name may be used to refer to the
 *	bitmap in future calls to Tk_GetBitmap.
 *
 * Results:
 *	A standard Tcl result.  If an error occurs then TCL_ERROR is
 *	returned and a message is left in the interp's result.
 *
 * Side effects:
 *	"Name" is entered into the bitmap table and may be used from
 *	here on to refer to the given bitmap.
 *
 *----------------------------------------------------------------------
 */

int
Tk_DefineBitmap(interp, name, source, width, height)
    Tcl_Interp *interp;		/* Interpreter to use for error reporting. */
    char *name;			/* Name to use for bitmap.  Must not already
				 * be defined as a bitmap. */
    char *source;		/* Address of bits for bitmap. */
    int width;			/* Width of bitmap. */
    int height;			/* Height of bitmap. */
{
    int new;
    Tcl_HashEntry *predefHashPtr;
    TkPredefBitmap *predefPtr;

    if (!initialized) {
	BitmapInit();
    }

    predefHashPtr = Tcl_CreateHashEntry(&tkPredefBitmapTable, name, &new);
    if (!new) {
        Tcl_AppendResult(interp, "bitmap \"", name,
		"\" is already defined", (char *) NULL);
	return TCL_ERROR;
    }
    predefPtr = (TkPredefBitmap *) ckalloc(sizeof(TkPredefBitmap));
    predefPtr->source = source;
    predefPtr->width = width;
    predefPtr->height = height;
    predefPtr->native = 0;
    Tcl_SetHashValue(predefHashPtr, predefPtr);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * Tk_NameOfBitmap --
 *
 *	Given a bitmap, return a textual string identifying the
 *	bitmap.
 *
 * Results:
 *	The return value is the string name associated with bitmap.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

char *
Tk_NameOfBitmap(display, bitmap)
    Display *display;			/* Display for which bitmap was
					 * allocated. */
    Pixmap bitmap;			/* Bitmap whose name is wanted. */
{
    IdKey idKey;
    Tcl_HashEntry *idHashPtr;
    TkBitmap *bitmapPtr;

    if (!initialized) {
	unknown:
	panic("Tk_NameOfBitmap received unknown bitmap argument");
    }

    idKey.display = display;
    idKey.pixmap = bitmap;
    idHashPtr = Tcl_FindHashEntry(&idTable, (char *) &idKey);
    if (idHashPtr == NULL) {
	goto unknown;
    }
    bitmapPtr = (TkBitmap *) Tcl_GetHashValue(idHashPtr);
    return bitmapPtr->nameHashPtr->key.string;
}

/*
 *--------------------------------------------------------------
 *
 * Tk_SizeOfBitmap --
 *
 *	Given a bitmap managed by this module, returns the width
 *	and height of the bitmap.
 *
 * Results:
 *	The words at *widthPtr and *heightPtr are filled in with
 *	the dimenstions of bitmap.
 *
 * Side effects:
 *	If bitmap isn't managed by this module then the procedure
 *	panics..
 *
 *--------------------------------------------------------------
 */

void
Tk_SizeOfBitmap(display, bitmap, widthPtr, heightPtr)
    Display *display;			/* Display for which bitmap was
					 * allocated. */
    Pixmap bitmap;			/* Bitmap whose size is wanted. */
    int *widthPtr;			/* Store bitmap width here. */
    int *heightPtr;			/* Store bitmap height here. */
{
    IdKey idKey;
    Tcl_HashEntry *idHashPtr;
    TkBitmap *bitmapPtr;

    if (!initialized) {
	unknownBitmap:
	panic("Tk_SizeOfBitmap received unknown bitmap argument");
    }

    idKey.display = display;
    idKey.pixmap = bitmap;
    idHashPtr = Tcl_FindHashEntry(&idTable, (char *) &idKey);
    if (idHashPtr == NULL) {
	goto unknownBitmap;
    }
    bitmapPtr = (TkBitmap *) Tcl_GetHashValue(idHashPtr);
    *widthPtr = bitmapPtr->width;
    *heightPtr = bitmapPtr->height;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeBitmap --
 *
 *	This procedure does all the work of releasing a bitmap allocated by
 *	Tk_GetBitmap or TkGetBitmapFromData.  It is invoked by both
 *	Tk_FreeBitmap and Tk_FreeBitmapFromObj
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The reference count associated with bitmap is decremented, and
 *	it is officially deallocated if no-one is using it anymore.
 *
 *----------------------------------------------------------------------
 */

static void
FreeBitmap(bitmapPtr)
    TkBitmap *bitmapPtr;			/* Bitmap to be released. */
{
    TkBitmap *prevPtr;

    bitmapPtr->resourceRefCount--;
    if (bitmapPtr->resourceRefCount > 0) {
	return;
    }

    Tk_FreePixmap(bitmapPtr->display, bitmapPtr->bitmap);
    Tcl_DeleteHashEntry(bitmapPtr->idHashPtr);
    prevPtr = (TkBitmap *) Tcl_GetHashValue(bitmapPtr->nameHashPtr);
    if (prevPtr == bitmapPtr) {
	if (bitmapPtr->nextPtr == NULL) {
	    Tcl_DeleteHashEntry(bitmapPtr->nameHashPtr);
	} else {
	    Tcl_SetHashValue(bitmapPtr->nameHashPtr, bitmapPtr->nextPtr);
	}
    } else {
	while (prevPtr->nextPtr != bitmapPtr) {
	    prevPtr = prevPtr->nextPtr;
	}
	prevPtr->nextPtr = bitmapPtr->nextPtr;
    }
    if (bitmapPtr->objRefCount == 0) {
	ckfree((char *) bitmapPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_FreeBitmap --
 *
 *	This procedure is called to release a bitmap allocated by
 *	Tk_GetBitmap or TkGetBitmapFromData.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The reference count associated with bitmap is decremented, and
 *	it is officially deallocated if no-one is using it anymore.
 *
 *----------------------------------------------------------------------
 */

void
Tk_FreeBitmap(display, bitmap)
    Display *display;			/* Display for which bitmap was
					 * allocated. */
    Pixmap bitmap;			/* Bitmap to be released. */
{
    Tcl_HashEntry *idHashPtr;
    IdKey idKey;

    if (!initialized) {
	panic("Tk_FreeBitmap called before Tk_GetBitmap");
    }

    idKey.display = display;
    idKey.pixmap = bitmap;
    idHashPtr = Tcl_FindHashEntry(&idTable, (char *) &idKey);
    if (idHashPtr == NULL) {
	panic("Tk_FreeBitmap received unknown bitmap argument");
    }
    FreeBitmap((TkBitmap *) Tcl_GetHashValue(idHashPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_FreeBitmapFromObj --
 *
 *	This procedure is called to release a bitmap allocated by
 *	Tk_AllocBitmapFromObj. It does not throw away the Tcl_Obj *;
 *	it only gets rid of the hash table entry for this bitmap
 *	and clears the cached value that is normally stored in the object.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The reference count associated with the bitmap represented by
 *	objPtr is decremented, and the bitmap is released to X if there are 
 *	no remaining uses for it.
 *
 *----------------------------------------------------------------------
 */

void
Tk_FreeBitmapFromObj(tkwin, objPtr)
    Tk_Window tkwin;		/* The window this bitmap lives in. Needed
				 * for the display value. */
    Tcl_Obj *objPtr;		/* The Tcl_Obj * to be freed. */
{
    FreeBitmap(GetBitmapFromObj(tkwin, objPtr));
}

/*
 *---------------------------------------------------------------------------
 *
 * FreeBitmapObjProc -- 
 *
 *	This proc is called to release an object reference to a bitmap.
 *	Called when the object's internal rep is released or when
 *	the cached bitmapPtr needs to be changed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The object reference count is decremented. When both it
 *	and the hash ref count go to zero, the color's resources
 *	are released.
 *
 *---------------------------------------------------------------------------
 */

static void
FreeBitmapObjProc(objPtr)
    Tcl_Obj *objPtr;		/* The object we are releasing. */
{
    TkBitmap *bitmapPtr = (TkBitmap *) objPtr->internalRep.twoPtrValue.ptr1;

    if (bitmapPtr != NULL) {
	bitmapPtr->objRefCount--;
	if ((bitmapPtr->objRefCount == 0)
		&& (bitmapPtr->resourceRefCount == 0)) {
	    ckfree((char *) bitmapPtr);
	}
	objPtr->internalRep.twoPtrValue.ptr1 = (VOID *) NULL;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * DupBitmapObjProc -- 
 *
 *	When a cached bitmap object is duplicated, this is called to
 *	update the internal reps.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The color's objRefCount is incremented and the internal rep
 *	of the copy is set to point to it.
 *
 *---------------------------------------------------------------------------
 */

static void
DupBitmapObjProc(srcObjPtr, dupObjPtr)
    Tcl_Obj *srcObjPtr;		/* The object we are copying from. */
    Tcl_Obj *dupObjPtr;		/* The object we are copying to. */
{
    TkBitmap *bitmapPtr = (TkBitmap *) srcObjPtr->internalRep.twoPtrValue.ptr1;
    
    dupObjPtr->typePtr = srcObjPtr->typePtr;
    dupObjPtr->internalRep.twoPtrValue.ptr1 = (VOID *) bitmapPtr;

    if (bitmapPtr != NULL) {
	bitmapPtr->objRefCount++;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetBitmapFromData --
 *
 *	Given a description of the bits for a bitmap, make a bitmap that
 *	has the given properties. *** NOTE:  this procedure is obsolete
 *	and really shouldn't be used anymore. ***
 *
 * Results:
 *	The return value is the X identifer for the desired bitmap
 *	(a one-plane Pixmap), unless it couldn't be created properly.
 *	In this case, None is returned and an error message is left in
 *	the interp's result.  The caller should never modify the bitmap that
 *	is returned, and should eventually call Tk_FreeBitmap when the
 *	bitmap is no longer needed.
 *
 * Side effects:
 *	The bitmap is added to an internal database with a reference count.
 *	For each call to this procedure, there should eventually be a call
 *	to Tk_FreeBitmap, so that the database can be cleaned up when bitmaps
 *	aren't needed anymore.
 *
 *----------------------------------------------------------------------
 */

	/* ARGSUSED */
Pixmap
Tk_GetBitmapFromData(interp, tkwin, source, width, height)
    Tcl_Interp *interp;		/* Interpreter to use for error reporting. */
    Tk_Window tkwin;		/* Window in which bitmap will be used. */
    char *source;		/* Bitmap data for bitmap shape. */
    int width, height;		/* Dimensions of bitmap. */
{
    DataKey nameKey;
    Tcl_HashEntry *dataHashPtr;
    int new;
    char string[16 + TCL_INTEGER_SPACE];
    char *name;
    static int autoNumber = 0;

    if (!initialized) {
	BitmapInit();
    }

    nameKey.source = source;
    nameKey.width = width;
    nameKey.height = height;
    dataHashPtr = Tcl_CreateHashEntry(&dataTable, (char *) &nameKey, &new);
    if (!new) {
	name = (char *) Tcl_GetHashValue(dataHashPtr);
    } else {
	autoNumber++;
	sprintf(string, "_tk%d", autoNumber);
	name = string;
	Tcl_SetHashValue(dataHashPtr, name);
	if (Tk_DefineBitmap(interp, name, source, width, height) != TCL_OK) {
	    Tcl_DeleteHashEntry(dataHashPtr);
	    return TCL_ERROR;
	}
    }
    return Tk_GetBitmap(interp, tkwin, name);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetBitmapFromObj --
 *
 *	Returns the bitmap referred to by a Tcl object.  The bitmap must
 *	already have been allocated via a call to Tk_AllocBitmapFromObj
 *	or Tk_GetBitmap.
 *
 * Results:
 *	Returns the Pixmap that matches the tkwin and the string rep
 *	of objPtr.
 *
 * Side effects:
 *	If the object is not already a bitmap, the conversion will free
 *	any old internal representation. 
 *
 *----------------------------------------------------------------------
 */

Pixmap
Tk_GetBitmapFromObj(tkwin, objPtr)
    Tk_Window tkwin;
    Tcl_Obj *objPtr;		/* The object from which to get pixels. */
{
    TkBitmap *bitmapPtr = GetBitmapFromObj(tkwin, objPtr);
    return bitmapPtr->bitmap;
}

/*
 *----------------------------------------------------------------------
 *
 * GetBitmapFromObj --
 *
 *	Returns the bitmap referred to by a Tcl object.  The bitmap must
 *	already have been allocated via a call to Tk_AllocBitmapFromObj
 *	or Tk_GetBitmap.
 *
 * Results:
 *	Returns the TkBitmap * that matches the tkwin and the string rep
 *	of  objPtr.
 *
 * Side effects:
 *	If the object is not already a bitmap, the conversion will free
 *	any old internal representation. 
 *
 *----------------------------------------------------------------------
 */

static TkBitmap *
GetBitmapFromObj(tkwin, objPtr)
    Tk_Window tkwin;		/* Window in which the bitmap will be used. */
    Tcl_Obj *objPtr;		/* The object that describes the desired
				 * bitmap. */
{
    TkBitmap *bitmapPtr; 
    Tcl_HashEntry *hashPtr;

    if (objPtr->typePtr != &bitmapObjType) {
	InitBitmapObj(objPtr);
    }

    bitmapPtr = (TkBitmap *) objPtr->internalRep.twoPtrValue.ptr1;
    if (bitmapPtr != NULL) { 
	if ((bitmapPtr->resourceRefCount > 0)
		&& (Tk_Display(tkwin) == bitmapPtr->display)) {
	    return bitmapPtr;
	}
	hashPtr = bitmapPtr->nameHashPtr;
	FreeBitmapObjProc(objPtr);
    } else {
	hashPtr = Tcl_FindHashEntry(&nameTable, Tcl_GetString(objPtr));
	if (hashPtr == NULL) {
	    goto error;
	}
    } 

    /*
     * At this point we've got a hash table entry, off of which hang
     * one or more TkBitmap structures.  See if any of them will work.
     */

    for (bitmapPtr = (TkBitmap *) Tcl_GetHashValue(hashPtr);
	    bitmapPtr != NULL;  bitmapPtr = bitmapPtr->nextPtr) {
	if (Tk_Display(tkwin) == bitmapPtr->display) {
	    objPtr->internalRep.twoPtrValue.ptr1 = (VOID *) bitmapPtr;
	    bitmapPtr->objRefCount++;
	    return bitmapPtr;
	}
    }

    error:
    panic("GetBitmapFromObj called with non-existent bitmap!");
    /*
     * The following code isn't reached; it's just there to please compilers.
     */
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * InitBitmapObj --
 *
 *	Bookeeping procedure to change an objPtr to a bitmap type.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The old internal rep of the object is freed. The internal
 *	rep is cleared. The final form of the object is set
 *	by either Tk_AllocBitmapFromObj or GetBitmapFromObj.
 *
 *----------------------------------------------------------------------
 */

static void
InitBitmapObj(objPtr)
    Tcl_Obj *objPtr;		/* The object to convert. */
{
    Tcl_ObjType *typePtr;

    /*
     * Free the old internalRep before setting the new one. 
     */

    Tcl_GetString(objPtr);
    typePtr = objPtr->typePtr;
    if ((typePtr != NULL) && (typePtr->freeIntRepProc != NULL)) {
	(*typePtr->freeIntRepProc)(objPtr);
    }
    objPtr->typePtr = &bitmapObjType;
    objPtr->internalRep.twoPtrValue.ptr1 = (VOID *) NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * BitmapInit --
 *
 *	Initialize the structures used for bitmap management.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Read the code.
 *
 *----------------------------------------------------------------------
 */

static void
BitmapInit()
{
    Tcl_Interp *dummy;

    dummy = Tcl_CreateInterp();
    initialized = 1;
    Tcl_InitHashTable(&nameTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&dataTable, sizeof(DataKey)/sizeof(int));
    Tcl_InitHashTable(&tkPredefBitmapTable, TCL_STRING_KEYS);

    /*
     * The call below is tricky:  can't use sizeof(IdKey) because it
     * gets padded with extra unpredictable bytes on some 64-bit
     * machines.
     */

    Tcl_InitHashTable(&idTable, (sizeof(Display *) + sizeof(Pixmap))
	    /sizeof(int));

    Tk_DefineBitmap(dummy, Tk_GetUid("error"), (char *) error_bits,
	    error_width, error_height);
    Tk_DefineBitmap(dummy, Tk_GetUid("gray75"), (char *) gray75_bits,
	    gray75_width, gray75_height);
    Tk_DefineBitmap(dummy, Tk_GetUid("gray50"), (char *) gray50_bits,
	    gray50_width, gray50_height);
    Tk_DefineBitmap(dummy, Tk_GetUid("gray25"), (char *) gray25_bits,
	    gray25_width, gray25_height);
    Tk_DefineBitmap(dummy, Tk_GetUid("gray12"), (char *) gray12_bits,
	    gray12_width, gray12_height);
    Tk_DefineBitmap(dummy, Tk_GetUid("hourglass"), (char *) hourglass_bits,
	    hourglass_width, hourglass_height);
    Tk_DefineBitmap(dummy, Tk_GetUid("info"), (char *) info_bits,
	    info_width, info_height);
    Tk_DefineBitmap(dummy, Tk_GetUid("questhead"), (char *) questhead_bits,
	    questhead_width, questhead_height);
    Tk_DefineBitmap(dummy, Tk_GetUid("question"), (char *) question_bits,
	    question_width, question_height);
    Tk_DefineBitmap(dummy, Tk_GetUid("warning"), (char *) warning_bits,
	    warning_width, warning_height);

    TkpDefineNativeBitmaps();

    Tcl_DeleteInterp(dummy);
}

/*
 *----------------------------------------------------------------------
 *
 * TkDebugBitmap --
 *
 *	This procedure returns debugging information about a bitmap.
 *
 * Results:
 *	The return value is a list with one sublist for each TkBitmap
 *	corresponding to "name".  Each sublist has two elements that
 *	contain the resourceRefCount and objRefCount fields from the
 *	TkBitmap structure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *
TkDebugBitmap(tkwin, name)
    Tk_Window tkwin;		/* The window in which the bitmap will be
				 * used (not currently used). */
    char *name;			/* Name of the desired color. */
{
    TkBitmap *bitmapPtr;
    Tcl_HashEntry *hashPtr;
    Tcl_Obj *resultPtr, *objPtr;

    resultPtr = Tcl_NewObj();
    hashPtr = Tcl_FindHashEntry(&nameTable, name);
    if (hashPtr != NULL) {
	bitmapPtr = (TkBitmap *) Tcl_GetHashValue(hashPtr);
	if (bitmapPtr == NULL) {
	    panic("TkDebugBitmap found empty hash table entry");
	}
	for ( ; (bitmapPtr != NULL); bitmapPtr = bitmapPtr->nextPtr) {
	    objPtr = Tcl_NewObj();
	    Tcl_ListObjAppendElement(NULL, objPtr,
		    Tcl_NewIntObj(bitmapPtr->resourceRefCount));
	    Tcl_ListObjAppendElement(NULL, objPtr,
		    Tcl_NewIntObj(bitmapPtr->objRefCount)); 
	    Tcl_ListObjAppendElement(NULL, resultPtr, objPtr);
	}
    }
    return resultPtr;
}
