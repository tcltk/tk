/* 
 * tkMacOSXCursor.c --
 *
 *        This file contains Macintosh specific cursor related routines.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright 2001, Apple Computer, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkMacOSXCursor.c,v 1.1.2.1 2001/10/15 09:22:00 wolfsuit Exp $
 */

#include "tkPort.h"
#include "tkInt.h"
#include "tkMacOSXInt.h"

#include <Carbon/Carbon.h>

/*
 * There are three different ways to set the cursor on the Mac.
 */
#define ARROW        0        /* The arrow cursor. */
#define COLOR        1        /* Cursors of type crsr. */
#define NORMAL        2        /* Cursors of type CURS. */

/*
 * The following data structure contains the system specific data
 * necessary to control Windows cursors.
 */

typedef struct {
    TkCursor info;                /* Generic cursor info used by tkCursor.c */
    Handle macCursor;                /* Resource containing Macintosh cursor. */
    int type;                        /* Type of Mac cursor: arrow, crsr, CURS */
} TkMacOSXCursor;

/*
 * The table below is used to map from the name of a predefined cursor
 * to its resource identifier.
 */

static struct CursorName {
    char *name;
    int id;
} cursorNames[] = {
    {"ibeam",                1},
    {"text",                1},
    {"xterm",                1},
    {"cross",                2},
    {"crosshair",        2},
    {"cross-hair",        2},
    {"plus",                3},
    {"watch",                4},
    {"arrow",                5},
    {NULL,                0}
};

/*
 * Declarations of static variables used in this file.
 */

static TkMacOSXCursor * gCurrentCursor = NULL;  /* A pointer to the current
                                              * cursor. */
static int gResizeOverride = false;             /* A boolean indicating whether
                                              * we should use the resize
                                              * cursor during installations. */
static int gTkOwnsCursor = true;             /* A boolean indicating whether
                                                Tk owns the cursor.  If not (for
                                                instance, in the case where a Tk 
                                                window is embedded in another app's
                                                window, and the cursor is out of
                                                the tk window, we will not attempt
                                                to adjust the cursor */

/*
 * Declarations of procedures local to this file
 */

static  void FindCursorByName _ANSI_ARGS_ ((TkMacOSXCursor *macCursorPtr,
                     char *string));

/*
 *----------------------------------------------------------------------
 *
 * FindCursorByName --
 *
 *        Retrieve a system cursor by name, and fill the macCursorPtr
 *        structure.  If the cursor cannot be found, the macCursor field
 *        will be NULL.  The function first attempts to load a color
 *        cursor.  If that fails it will attempt to load a black & white
 *        cursor.
 *
 * Results:
 *        Fills the macCursorPtr record.  
 *
 * Side effects:
 *        None
 *
 *----------------------------------------------------------------------
 */
 
void 
FindCursorByName(
    TkMacOSXCursor *macCursorPtr,
    char *string)
{
    Handle resource;
    Str255 curName;
    int destWrote, inCurLen;

    inCurLen = strlen(string);
    if (inCurLen > 255) {
        return;
    }

    /*
     * macRoman is the encoding that the resource fork uses.
     */

    Tcl_UtfToExternal(NULL, Tcl_GetEncoding(NULL, "macRoman"), string,
            inCurLen, 0, NULL, 
            (char *) &curName[1],
            255, NULL, &destWrote, NULL); /* Internalize native */
    curName[0] = destWrote;

    resource = GetNamedResource('crsr', curName);

    if (resource != NULL) {
        short id;
        Str255 theName;
        ResType        theType;

        HLock(resource);
        GetResInfo(resource, &id, &theType, theName);
        HUnlock(resource);
        macCursorPtr->macCursor = (Handle) GetCCursor(id);
        macCursorPtr->type = COLOR;
    }

    if (resource == NULL) {
        macCursorPtr->macCursor = GetNamedResource('CURS', curName);
        macCursorPtr->type = NORMAL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetCursorByName --
 *
 *        Retrieve a system cursor by name.  
 *
 * Results:
 *        Returns a new cursor, or NULL on errors.  
 *
 * Side effects:
 *        Allocates a new cursor.
 *
 *----------------------------------------------------------------------
 */

TkCursor *
TkGetCursorByName(
    Tcl_Interp *interp,                /* Interpreter to use for error reporting. */
    Tk_Window tkwin,                /* Window in which cursor will be used. */
    Tk_Uid string)                /* Description of cursor.  See manual entry
                                 * for details on legal syntax. */
{
    struct CursorName *namePtr;
    TkMacOSXCursor *macCursorPtr;

    macCursorPtr = (TkMacOSXCursor *) ckalloc(sizeof(TkMacOSXCursor));
    macCursorPtr->info.cursor = (Tk_Cursor) macCursorPtr;

    /*
     * To find a cursor we must first determine if it is one of the
     * builtin cursors or the standard arrow cursor. Otherwise, we
     * attempt to load the cursor as a named Mac resource.
     */

    for (namePtr = cursorNames; namePtr->name != NULL; namePtr++) {
        if (strcmp(namePtr->name, string) == 0) {
            break;
        }
    }


    if (namePtr->name != NULL) {
            if (namePtr->id == 5) {
            macCursorPtr->macCursor = (Handle) -1;
            macCursorPtr->type = ARROW;
            } else {
            macCursorPtr->macCursor = (Handle) GetCursor(namePtr->id);
            macCursorPtr->type = NORMAL;
        }
    } else {
        FindCursorByName(macCursorPtr, string);

        if (macCursorPtr->macCursor == NULL) {
            char **argv;
            int argc, err;
            
            /*
             * The user may be trying to specify an XCursor with fore
             * & back colors. We don't want this to be an error, so pick 
             * off the first word, and try again. 
             */
             
            err = Tcl_SplitList(interp, string, &argc, &argv);
            if (err == TCL_OK ) {
                if (argc > 1) {
                    FindCursorByName(macCursorPtr, argv[0]);
                }

                ckfree((char *) argv);
            }
        }
    }

    if (macCursorPtr->macCursor == NULL) {
        ckfree((char *)macCursorPtr);
        Tcl_AppendResult(interp, "bad cursor spec \"", string, "\"",
                (char *) NULL);
        return NULL;
    } else {
        return (TkCursor *) macCursorPtr;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkCreateCursorFromData --
 *
 *        Creates a cursor from the source and mask bits.
 *
 * Results:
 *        Returns a new cursor, or NULL on errors.
 *
 * Side effects:
 *        Allocates a new cursor.
 *
 *----------------------------------------------------------------------
 */

TkCursor *
TkCreateCursorFromData(
    Tk_Window tkwin,                /* Window in which cursor will be used. */
    char *source,                /* Bitmap data for cursor shape. */
    char *mask,                        /* Bitmap data for cursor mask. */
    int width, int height,        /* Dimensions of cursor. */
    int xHot, int yHot,                /* Location of hot-spot in cursor. */
    XColor fgColor,                /* Foreground color for cursor. */
    XColor bgColor)                /* Background color for cursor. */
{
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpFreeCursor --
 *
 *        This procedure is called to release a cursor allocated by
 *        TkGetCursorByName.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        The cursor data structure is deallocated.
 *
 *----------------------------------------------------------------------
 */

void
TkpFreeCursor(
    TkCursor *cursorPtr)
{
    TkMacOSXCursor *macCursorPtr = (TkMacOSXCursor *) cursorPtr;

    switch (macCursorPtr->type) {
        case COLOR:
            DisposeCCursor((CCrsrHandle) macCursorPtr->macCursor);
            break;
        case NORMAL:
            ReleaseResource(macCursorPtr->macCursor);
            break;
    }

    if (macCursorPtr == gCurrentCursor) {
        gCurrentCursor = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXInstallCursor --
 *
 *        Installs either the current cursor as defined by TkpSetCursor
 *        or a resize cursor as the cursor the Macintosh should currently
 *        display.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        Changes the Macintosh mouse cursor.
 *
 *----------------------------------------------------------------------
 */

void
TkMacOSXInstallCursor(
    int resizeOverride)
{
    TkMacOSXCursor *macCursorPtr = gCurrentCursor;
    CCrsrHandle ccursor;
    CursHandle cursor;
    
    gResizeOverride = resizeOverride;

    if (resizeOverride) {
        cursor = (CursHandle) GetNamedResource('CURS', "\presize");
        if (cursor) {
            SetCursor(*cursor);
        } else {
            /*
            fprintf(stderr,"Resize cursor failed, %d\n", ResError());
             */
        }
    } else if (macCursorPtr == NULL || macCursorPtr->type == ARROW) {
        SetThemeCursor(kThemeArrowCursor);
    } else {
        switch (macCursorPtr->type) {
            case COLOR:
                ccursor = (CCrsrHandle) macCursorPtr->macCursor;
                SetCCursor(ccursor);
                break;
            case NORMAL:
                cursor = (CursHandle) macCursorPtr->macCursor;
                SetCursor(*cursor);
                break;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetCursor --
 *
 *        Set the current cursor and install it.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        Changes the current cursor.
 *
 *----------------------------------------------------------------------
 */

void
TkpSetCursor(
    TkpCursor cursor)
{
    if (!gTkOwnsCursor) {
        return;
    }
    if (cursor == None) {
        gCurrentCursor = NULL;
    } else {
        gCurrentCursor = (TkMacOSXCursor *) cursor;
    }

    if (Tk_MacOSXIsAppInFront()) {
        TkMacOSXInstallCursor(gResizeOverride);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MacOSXTkOwnsCursor --
 *
 *        Sets whether Tk has the right to adjust the cursor.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        May keep Tk from changing the cursor.
 *
 *----------------------------------------------------------------------
 */

void
Tk_MacOSXTkOwnsCursor(
    int tkOwnsIt)
{
    gTkOwnsCursor = tkOwnsIt;
}
