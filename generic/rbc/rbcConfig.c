/*
 * rbcConfig.c --
 *
 *      This module implements custom configuration options for the rbc
 *      toolkit.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

#define PIXELS_NONNEGATIVE	0
#define PIXELS_POSITIVE		1
#define PIXELS_ANY		2

#define COUNT_NONNEGATIVE	0
#define COUNT_POSITIVE		1
#define COUNT_ANY		2

/*
 * ----------------------------------------------------------------------
 *
 * The following enumerated values are used as bit flags.
 *	FILL_NONE		Neither coordinate plane is specified
 *	FILL_X			Horizontal plane.
 *	FILL_Y			Vertical plane.
 *	FILL_BOTH		Both vertical and horizontal planes.
 *
 * ----------------------------------------------------------------------
 */
#define FILL_NONE	0
#define FILL_X		1
#define FILL_Y		2
#define FILL_BOTH	3

static int      StringToFill(
    ClientData clientData,
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *string,
    char *widgRec,
    int flags);
static const char *FillToString(
    ClientData clientData,
    Tk_Window tkwin,
    char *widgRec,
    int offset,
    Tcl_FreeProc ** freeProcPtr);

Tk_CustomOption rbcFillOption = {
    StringToFill, FillToString, (ClientData) 0
};

static int      StringToPad(
    ClientData clientData,
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *string,
    char *widgRec,
    int offset);
static const char *PadToString(
    ClientData clientData,
    Tk_Window tkwin,
    char *widgRec,
    int offset,
    Tcl_FreeProc ** freeProcPtr);

Tk_CustomOption rbcPadOption = {
    StringToPad, PadToString, (ClientData) 0
};

static int      StringToDistance(
    ClientData clientData,
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *string,
    char *widgRec,
    int flags);
static const char *DistanceToString(
    ClientData clientData,
    Tk_Window tkwin,
    char *widgRec,
    int offset,
    Tcl_FreeProc ** freeProcPtr);

Tk_CustomOption rbcDistanceOption = {
    StringToDistance, DistanceToString, (ClientData) PIXELS_NONNEGATIVE
};

Tk_CustomOption rbcPositiveDistanceOption = {
    StringToDistance, DistanceToString, (ClientData) PIXELS_POSITIVE
};

Tk_CustomOption rbcAnyDistanceOption = {
    StringToDistance, DistanceToString, (ClientData) PIXELS_ANY
};

static int      StringToCount(
    ClientData clientData,
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *string,
    char *widgRec,
    int flags);
static const char *CountToString(
    ClientData clientData,
    Tk_Window tkwin,
    char *widgRec,
    int offset,
    Tcl_FreeProc ** freeProcPtr);

Tk_CustomOption rbcCountOption = {
    StringToCount, CountToString, (ClientData) COUNT_NONNEGATIVE
};

Tk_CustomOption rbcPositiveCountOption = {
    StringToCount, CountToString, (ClientData) COUNT_POSITIVE
};

static int      StringToDashes(
    ClientData clientData,
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *string,
    char *widgRec,
    int flags);
static const char *DashesToString(
    ClientData clientData,
    Tk_Window tkwin,
    char *widgRec,
    int offset,
    Tcl_FreeProc ** freeProcPtr);

Tk_CustomOption rbcDashesOption = {
    StringToDashes, DashesToString, (ClientData) 0
};

static int      StringToShadow(
    ClientData clientData,
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *string,
    char *widgRec,
    int flags);
static const char *ShadowToString(
    ClientData clientData,
    Tk_Window tkwin,
    char *widgRec,
    int offset,
    Tcl_FreeProc ** freeProcPtr);

Tk_CustomOption rbcShadowOption = {
    StringToShadow, ShadowToString, (ClientData) 0
};

static int      StringToUid(
    ClientData clientData,
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *string,
    char *widgRec,
    int flags);
static const char *UidToString(
    ClientData clientData,
    Tk_Window tkwin,
    char *widgRec,
    int offset,
    Tcl_FreeProc ** freeProcPtr);

Tk_CustomOption rbcUidOption = {
    StringToUid, UidToString, (ClientData) 0
};

static int      StringToState(
    ClientData clientData,
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *string,
    char *widgRec,
    int flags);
static const char *StateToString(
    ClientData clientData,
    Tk_Window tkwin,
    char *widgRec,
    int offset,
    Tcl_FreeProc ** freeProcPtr);

Tk_CustomOption rbcStateOption = {
    StringToState, StateToString, (ClientData) 0
};

static int      StringToList(
    ClientData clientData,
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *string,
    char *widgRec,
    int flags);
static const char *ListToString(
    ClientData clientData,
    Tk_Window tkwin,
    char *widgRec,
    int offset,
    Tcl_FreeProc ** freeProcPtr);

Tk_CustomOption rbcListOption = {
    StringToList, ListToString, (ClientData) 0
};

static int      StringToTile(
    ClientData clientData,
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *value,
    char *widgRec,
    int flags);
static const char *TileToString(
    ClientData clientData,
    Tk_Window tkwin,
    char *widgRec,
    int offset,
    Tcl_FreeProc ** freeProcPtr);

Tk_CustomOption rbcTileOption = {
    StringToTile, TileToString, (ClientData) 0
};

static int      GetInt(
    Tcl_Interp * interp,
    const char *string,
    int check,
    int *valuePtr);

/*
 *----------------------------------------------------------------------
 *
 * StringToFill --
 *
 *      Converts the fill style string into its numeric representation.
 *
 *      Valid style strings are:
 *
 *         "none"   Use neither plane.
 *         "x"      X-coordinate plane.
 *         "y"	    Y-coordinate plane.
 *         "both"   Use both coordinate planes.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToFill(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Not used. */
    const char *string,         /* Fill style string */
    char *widgRec,              /* Cubicle structure record */
    int offset)
{                               /* Offset of style in record */
    int            *fillPtr = (int *) (widgRec + offset);
    unsigned int    length;
    char            c;

    c = string[0];
    length = strlen(string);
    if ((c == 'n') && (strncmp(string, "none", length) == 0)) {
        *fillPtr = FILL_NONE;
    } else if ((c == 'x') && (strncmp(string, "x", length) == 0)) {
        *fillPtr = FILL_X;
    } else if ((c == 'y') && (strncmp(string, "y", length) == 0)) {
        *fillPtr = FILL_Y;
    } else if ((c == 'b') && (strncmp(string, "both", length) == 0)) {
        *fillPtr = FILL_BOTH;
    } else {
        Tcl_AppendResult(interp, "bad argument \"", string,
            "\": should be \"none\", \"x\", \"y\", or \"both\"", (char *) NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * FillToString --
 *
 *      Returns the fill style string based upon the fill flags.
 *
 * Results:
 *      The fill style string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
FillToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Widget structure record */
    int offset,                 /* Offset of fill in widget record */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Not used. */
    int             fill = *(int *) (widgRec + offset);

    switch (fill) {
    case FILL_X:
        return "x";
    case FILL_Y:
        return "y";
    case FILL_NONE:
        return "none";
    case FILL_BOTH:
        return "both";
    default:
        return "unknown value";
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcGetPixels --
 *
 *      Like Tk_GetPixels, but checks for negative, zero.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int
RbcGetPixels(
    Tcl_Interp * interp,
    Tk_Window tkwin,
    const char *string,
    int check,                  /* Can be PIXELS_POSITIVE, PIXELS_NONNEGATIVE,
                                 * or PIXELS_ANY, */
    int *valuePtr)
{
    int             length;

    if (Tk_GetPixels(interp, tkwin, string, &length) != TCL_OK) {
        return TCL_ERROR;
    }
    if (length >= SHRT_MAX) {
        Tcl_AppendResult(interp, "bad distance \"", string, "\": ",
            "too big to represent", (char *) NULL);
        return TCL_ERROR;
    }
    switch (check) {
    case PIXELS_NONNEGATIVE:
        if (length < 0) {
            Tcl_AppendResult(interp, "bad distance \"", string, "\": ",
                "can't be negative", (char *) NULL);
            return TCL_ERROR;
        }
        break;
    case PIXELS_POSITIVE:
        if (length <= 0) {
            Tcl_AppendResult(interp, "bad distance \"", string, "\": ",
                "must be positive", (char *) NULL);
            return TCL_ERROR;
        }
        break;
    case PIXELS_ANY:
        break;
    }
    *valuePtr = length;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToDistance --
 *
 *      Like TK_CONFIG_PIXELS, but adds an extra check for negative
 *      values.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToDistance(
    ClientData clientData,      /* Indicated how to check distance */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Window */
    const char *string,         /* Pixel value string */
    char *widgRec,              /* Widget record */
    int offset)
{                               /* Offset of pixel size in record */
    int            *valuePtr = (int *) (widgRec + offset);
    return RbcGetPixels(interp, tkwin, string, (int) clientData, valuePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * DistanceToString --
 *
 *      Returns the string representing the positive pixel size.
 *
 * Results:
 *      The pixel size string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
DistanceToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Widget structure record */
    int offset,                 /* Offset in widget record */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Not used. */
    int             value = *(int *) (widgRec + offset);
    char           *result;
    char            stringInt[200];

    sprintf(stringInt, "%d", value);
    result = RbcStrdup(stringInt);
    assert(result);
    *freeProcPtr = (Tcl_FreeProc *) Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * GetInt --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
GetInt(
    Tcl_Interp * interp,
    const char *string,
    int check,                  /* Can be COUNT_POSITIVE, COUNT_NONNEGATIVE,
                                 * or COUNT_ANY, */
    int *valuePtr)
{
    int             count;

    if (Tcl_GetInt(interp, string, &count) != TCL_OK) {
        return TCL_ERROR;
    }
    switch (check) {
    case COUNT_NONNEGATIVE:
        if (count < 0) {
            Tcl_AppendResult(interp, "bad value \"", string, "\": ",
                "can't be negative", (char *) NULL);
            return TCL_ERROR;
        }
        break;
    case COUNT_POSITIVE:
        if (count <= 0) {
            Tcl_AppendResult(interp, "bad value \"", string, "\": ",
                "must be positive", (char *) NULL);
            return TCL_ERROR;
        }
        break;
    case COUNT_ANY:
        break;
    }
    *valuePtr = count;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToCount --
 *
 *      Like TK_CONFIG_PIXELS, but adds an extra check for negative
 *      values.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToCount(
    ClientData clientData,      /* Indicated how to check distance */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Not used. */
    const char *string,         /* Pixel value string */
    char *widgRec,              /* Widget record */
    int offset)
{                               /* Offset of pixel size in record */
    int            *valuePtr = (int *) (widgRec + offset);
    return GetInt(interp, string, (int) clientData, valuePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * CountToString --
 *
 *      Returns the string representing the positive pixel size.
 *
 * Results:
 *      The pixel size string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
CountToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Widget structure record */
    int offset,                 /* Offset in widget record */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Not used. */
    int             value = *(int *) (widgRec + offset);
    char           *result;
    char            stringInt[200];

    sprintf(stringInt, "%d", value);
    result = RbcStrdup(stringInt);
    assert(result);
    *freeProcPtr = (Tcl_FreeProc *) Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToPad --
 *
 *      Convert a string into two pad values.  The string may be in one of
 *      the following forms:
 *
 *          n      - n is a non-negative integer. This sets both
 *                   pad values to n.
 *          {n m}  - both n and m are non-negative integers. side1
 *                   is set to n, side2 is set to m.
 *
 * Results:
 *      If the string is successfully converted, TCL_OK is returned.
 *      Otherwise, TCL_ERROR is returned and an error message is left in
 *      interp->result.
 *
 * Side Effects:
 *      The padding structure passed is updated with the new values.
 *
 *----------------------------------------------------------------------
 */
static int
StringToPad(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Window */
    const char *string,         /* Pixel value string */
    char *widgRec,              /* Widget record */
    int offset)
{                               /* Offset of pad in widget */
    RbcPad         *padPtr = (RbcPad *) (widgRec + offset);
    int             nElem;
    int             pad, result;
    const char    **padArr;

    if (Tcl_SplitList(interp, string, &nElem, &padArr) != TCL_OK) {
        return TCL_ERROR;
    }
    result = TCL_ERROR;
    if ((nElem < 1) || (nElem > 2)) {
        Tcl_AppendResult(interp, "wrong # elements in padding list",
            (char *) NULL);
        goto error;
    }
    if (RbcGetPixels(interp, tkwin, padArr[0], PIXELS_NONNEGATIVE,
            &pad) != TCL_OK) {
        goto error;
    }
    padPtr->side1 = pad;
    if ((nElem > 1)
        && (RbcGetPixels(interp, tkwin, padArr[1], PIXELS_NONNEGATIVE,
                &pad) != TCL_OK)) {
        goto error;
    }
    padPtr->side2 = pad;
    result = TCL_OK;

  error:
    ckfree((char *) padArr);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * PadToString --
 *
 *     Converts the two pad values into a Tcl list.  Each pad has two
 *     pixel values.  For vertical pads, they represent the top and bottom
 *     margins.  For horizontal pads, they're the left and right margins.
 *     All pad values are non-negative integers.
 *
 * Results:
 *     The padding list is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
PadToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Structure record */
    int offset,                 /* Offset of pad in record */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Not used. */
    RbcPad         *padPtr = (RbcPad *) (widgRec + offset);
    char           *result;
    char            string[200];

    sprintf(string, "%d %d", padPtr->side1, padPtr->side2);
    result = RbcStrdup(string);
    if (result == NULL) {
        return "out of memory";
    }
    *freeProcPtr = (Tcl_FreeProc *) Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToShadow --
 *
 *      Convert a string into two pad values.  The string may be in one of
 *      the following forms:
 *
 *          n      - n is a non-negative integer. This sets both
 *                   pad values to n.
 *          {n m}  - both n and m are non-negative integers. side1
 *                   is set to n, side2 is set to m.
 *
 * Results:
 *      If the string is successfully converted, TCL_OK is returned.
 *      Otherwise, TCL_ERROR is returned and an error message is left in
 *      interp->result.
 *
 * Side Effects:
 *      The padding structure passed is updated with the new values.
 *
 *----------------------------------------------------------------------
 */
static int
StringToShadow(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Window */
    const char *string,         /* Pixel value string */
    char *widgRec,              /* Widget record */
    int offset)
{                               /* Offset of pad in widget */
    RbcShadow      *shadowPtr = (RbcShadow *) (widgRec + offset);
    XColor         *colorPtr;
    int             dropOffset;

    colorPtr = NULL;
    dropOffset = 0;
    if ((string != NULL) && (string[0] != '\0')) {
        int             nElem;
        const char    **elemArr;

        if (Tcl_SplitList(interp, string, &nElem, &elemArr) != TCL_OK) {
            return TCL_ERROR;
        }
        if ((nElem < 1) || (nElem > 2)) {
            Tcl_AppendResult(interp, "wrong # elements in drop shadow value",
                (char *) NULL);
            ckfree((char *) elemArr);
            return TCL_ERROR;
        }
        colorPtr = Tk_GetColor(interp, tkwin, Tk_GetUid(elemArr[0]));
        if (colorPtr == NULL) {
            ckfree((char *) elemArr);
            return TCL_ERROR;
        }
        dropOffset = 1;
        if (nElem == 2) {
            if (RbcGetPixels(interp, tkwin, elemArr[1], PIXELS_NONNEGATIVE,
                    &dropOffset) != TCL_OK) {
                Tk_FreeColor(colorPtr);
                ckfree((char *) elemArr);
                return TCL_ERROR;
            }
        }
        ckfree((char *) elemArr);
    }
    if (shadowPtr->color != NULL) {
        Tk_FreeColor(shadowPtr->color);
    }
    shadowPtr->color = colorPtr;
    shadowPtr->offset = dropOffset;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ShadowToString --
 *
 *      Converts the two pad values into a Tcl list.  Each pad has two
 *      pixel values.  For vertical pads, they represent the top and bottom
 *      margins.  For horizontal pads, they're the left and right margins.
 *      All pad values are non-negative integers.
 *
 * Results:
 *      The padding list is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
ShadowToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Structure record */
    int offset,                 /* Offset of pad in record */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Not used. */
    RbcShadow      *shadowPtr = (RbcShadow *) (widgRec + offset);
    const char     *result;

    result = "";
    if (shadowPtr->color != NULL) {
        char            string[200];

        sprintf(string, "%s %d", Tk_NameOfColor(shadowPtr->color),
            shadowPtr->offset);
        result = RbcStrdup(string);
        *freeProcPtr = (Tcl_FreeProc *) Tcl_Free;
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * GetDashes --
 *
 *      Converts a Tcl list of dash values into a dash list ready for
 *      use with XSetDashes.
 *
 *      A valid list dash values can have zero through 11 elements
 *      (PostScript limit).  Values must be between 1 and 255. Although
 *      a list of 0 (like the empty string) means no dashes.
 *
 * Results:
 *      A standard Tcl result. If the list represented a valid dash
 *      list TCL_OK is returned and *dashesPtr* will contain the
 *      valid dash list. Otherwise, TCL_ERROR is returned and
 *      interp->result will contain an error message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
GetDashes(
    Tcl_Interp * interp,
    const char *string,
    RbcDashes * dashesPtr)
{
    if ((string == NULL) || (*string == '\0')) {
        dashesPtr->values[0] = 0;
    } else if (strcmp(string, "dash") == 0) {
        dashesPtr->values[0] = 5;
        dashesPtr->values[1] = 2;
        dashesPtr->values[2] = 0;
    } else if (strcmp(string, "dot") == 0) {
        dashesPtr->values[0] = 1;
        dashesPtr->values[1] = 0;
    } else if (strcmp(string, "dashdot") == 0) {
        dashesPtr->values[0] = 2;
        dashesPtr->values[1] = 4;
        dashesPtr->values[2] = 2;
        dashesPtr->values[3] = 0;
    } else if (strcmp(string, "dashdotdot") == 0) {
        dashesPtr->values[0] = 2;
        dashesPtr->values[1] = 4;
        dashesPtr->values[2] = 2;
        dashesPtr->values[3] = 2;
        dashesPtr->values[4] = 0;
    } else {
        int             nValues;
        const char    **strArr;
        long int        value;
        register int    i;

        if (Tcl_SplitList(interp, string, &nValues, &strArr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (nValues > 11) {
            /* This is the postscript limit */
            Tcl_AppendResult(interp, "too many values in dash list \"", string,
                "\"", (char *) NULL);
            ckfree((char *) strArr);
            return TCL_ERROR;
        }
        for (i = 0; i < nValues; i++) {
            if (Tcl_ExprLong(interp, strArr[i], &value) != TCL_OK) {
                ckfree((char *) strArr);
                return TCL_ERROR;
            }
            /*
             * Backward compatibility:
             * Allow list of 0 to turn off dashes
             */
            if ((value == 0) && (nValues == 1)) {
                break;
            }
            if ((value < 1) || (value > 255)) {
                Tcl_AppendResult(interp, "dash value \"", strArr[i],
                    "\" is out of range", (char *) NULL);
                ckfree((char *) strArr);
                return TCL_ERROR;
            }
            dashesPtr->values[i] = (unsigned char) value;
        }
        /* Make sure the array ends with a NUL byte  */
        dashesPtr->values[i] = 0;
        ckfree((char *) strArr);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToDashes --
 *
 *      Convert the list of dash values into a dashes array.
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      The Dashes structure is updated with the new dash list.
 *
 *----------------------------------------------------------------------
 */
static int
StringToDashes(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Not used. */
    const char *string,         /* New dash value list */
    char *widgRec,              /* Widget record */
    int offset)
{                               /* offset to Dashes structure */
    RbcDashes      *dashesPtr = (RbcDashes *) (widgRec + offset);

    return GetDashes(interp, string, dashesPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * DashesToString --
 *
 *      Convert the dashes array into a list of values.
 *
 * Results:
 *      The string representing the dashes returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
DashesToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Widget record */
    int offset,                 /* offset of Dashes in record */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Memory deallocation scheme to use */
    RbcDashes      *dashesPtr = (RbcDashes *) (widgRec + offset);
    Tcl_DString     dString;
    char           *p;
    char           *result;
    char            stringInt[200];

    if (dashesPtr->values[0] == 0) {
        return "";
    }
    Tcl_DStringInit(&dString);
    for (p = dashesPtr->values; *p != 0; p++) {
        sprintf(stringInt, "%d", *p);
        Tcl_DStringAppendElement(&dString, stringInt);
    }
    result = Tcl_DStringValue(&dString);
    if (result == dString.staticSpace) {
        result = RbcStrdup(result);
    }
    *freeProcPtr = (Tcl_FreeProc *) Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToUid --
 *
 *      Converts the string to a RbcUid. RbcUid's are hashed, reference
 *      counted strings.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToUid(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Not used. */
    const char *string,         /* Fill style string */
    char *widgRec,              /* Cubicle structure record */
    int offset)
{                               /* Offset of style in record */
    RbcUid         *uidPtr = (RbcUid *) (widgRec + offset);
    RbcUid          newId;

    newId = NULL;
    if ((string != NULL) && (*string != '\0')) {
        newId = RbcGetUid(string);
    }
    if (*uidPtr != NULL) {
        RbcFreeUid(*uidPtr);
    }
    *uidPtr = newId;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * UidToString --
 *
 *      Returns the fill style string based upon the fill flags.
 *
 * Results:
 *      The fill style string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
UidToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Widget structure record */
    int offset,                 /* Offset of fill in widget record */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Not used. */
    RbcUid          uid = *(RbcUid *) (widgRec + offset);

    return (uid == NULL) ? "" : uid;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToState --
 *
 *      Converts the string to a state value. Valid states are
 *      disabled, normal.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToState(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Not used. */
    const char *string,         /* String representation of option value */
    char *widgRec,              /* Widget structure record */
    int offset)
{                               /* Offset of field in record */
    int            *statePtr = (int *) (widgRec + offset);

    if (strcmp(string, "normal") == 0) {
        *statePtr = RBC_STATE_NORMAL;
    } else if (strcmp(string, "disabled") == 0) {
        *statePtr = RBC_STATE_DISABLED;
    } else if (strcmp(string, "active") == 0) {
        *statePtr = RBC_STATE_ACTIVE;
    } else {
        Tcl_AppendResult(interp, "bad state \"", string,
            "\": should be normal, active, or disabled", (char *) NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StateToString --
 *
 *      Returns the string representation of the state configuration field
 *
 * Results:
 *      The string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
StateToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Widget structure record */
    int offset,                 /* Offset of fill in widget record */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Not used. */
    int             state = *(int *) (widgRec + offset);

    switch (state) {
    case RBC_STATE_ACTIVE:
        return "active";
    case RBC_STATE_DISABLED:
        return "disabled";
    case RBC_STATE_NORMAL:
        return "normal";
    default:
        return "???";
    }
}

/*
 *----------------------------------------------------------------------
 *
 * StringToList --
 *
 *      Converts the string to a list.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToList(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Not used. */
    const char *string,         /* String representation of option value */
    char *widgRec,              /* Widget structure record */
    int offset)
{                               /* Offset of field in record */
    const char   ***listPtr = (const char ***) (widgRec + offset);
    const char    **elemArr;
    int             nElem;

    if (*listPtr != NULL) {
        ckfree((char *) *listPtr);
        *listPtr = NULL;
    }
    if ((string == NULL) || (*string == '\0')) {
        return TCL_OK;
    }
    if (Tcl_SplitList(interp, string, &nElem, &elemArr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (nElem > 0) {
        *listPtr = elemArr;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ListToString --
 *
 *      Returns the string representation of the state configuration field
 *
 * Results:
 *      The string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
ListToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Widget structure record. */
    int offset,                 /* Offset of fill in widget record. */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Not used. */
    char          **list = *(char ***) (widgRec + offset);
    register char **p;
    char           *result;
    Tcl_DString     dString;

    if (list == NULL) {
        return "";
    }
    Tcl_DStringInit(&dString);
    for (p = list; *p != NULL; p++) {
        Tcl_DStringAppendElement(&dString, *p);
    }
    result = Tcl_DStringValue(&dString);
    if (result == dString.staticSpace) {
        result = RbcStrdup(result);
    }
    Tcl_DStringFree(&dString);
    *freeProcPtr = (Tcl_FreeProc *) Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToTile --
 *
 *      Converts the name of an image into a tile.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int
StringToTile(
    ClientData clientData,      /* Not used. */
    Tcl_Interp * interp,        /* Interpreter to send results back to */
    Tk_Window tkwin,            /* Window on same display as tile */
    const char *string,         /* Name of image */
    char *widgRec,              /* Widget structure record */
    int offset)
{                               /* Offset of tile in record */
    RbcTile        *tilePtr = (RbcTile *) (widgRec + offset);
    RbcTile         tile, oldTile;

    oldTile = *tilePtr;
    tile = NULL;
    if ((string != NULL) && (*string != '\0')) {
        if (RbcGetTile(interp, tkwin, string, &tile) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    /* Don't delete the information for the old tile, until we know
     * that we successfully allocated a new one. */
    if (oldTile != NULL) {
        RbcFreeTile(oldTile);
    }
    *tilePtr = tile;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TileToString --
 *
 *      Returns the name of the tile.
 *
 * Results:
 *      The name of the tile is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
TileToString(
    ClientData clientData,      /* Not used. */
    Tk_Window tkwin,            /* Not used. */
    char *widgRec,              /* Widget structure record */
    int offset,                 /* Offset of tile in record */
    Tcl_FreeProc ** freeProcPtr)
{                               /* Not used. */
    RbcTile         tile = *(RbcTile *) (widgRec + offset);

    return RbcNameOfTile(tile);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcConfigModified --
 *
 *      Given the configuration specifications and one or more option
 *      patterns (terminated by a NULL), indicate if any of the matching
 *      configuration options has been reset.
 *
 * Results:
 *      Returns 1 if one of the options has changed, 0 otherwise.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int
RbcConfigModified(
    Tk_ConfigSpec * specs,
    ...)
{
    va_list         argList;
    register Tk_ConfigSpec *specPtr;
    register char  *option;

    va_start(argList, specs);
    while ((option = va_arg(argList, char *)) != NULL) {
        for (specPtr = specs; specPtr->type != TK_CONFIG_END; specPtr++) {
            if ((Tcl_StringMatch(specPtr->argvName, option))
                && (specPtr->specFlags & TK_CONFIG_OPTION_SPECIFIED)) {
                va_end(argList);
                return 1;
            }
        }
    }
    va_end(argList);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcConfigureWidgetComponent --
 *
 *      Configures a component of a widget.  This is useful for
 *      widgets that have multiple components which aren't uniquely
 *      identified by a Tk_Window. It allows us, for example, set
 *      resources for axes of the graph widget. The graph really has
 *      only one window, but its convenient to specify components in a
 *      hierarchy of options.
 *
 *          *graph.x.logScale yes
 *          *graph.Axis.logScale yes
 *          *graph.temperature.scaleSymbols yes
 *          *graph.Element.scaleSymbols yes
 *
 *      This is really a hack to work around the limitations of the Tk
 *      resource database.  It creates a temporary window, needed to
 *      call Tk_ConfigureWidget, using the name of the component.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      A temporary window is created merely to pass to Tk_ConfigureWidget.
 *
 *----------------------------------------------------------------------
 */
int
RbcConfigureWidgetComponent(
    Tcl_Interp * interp,
    Tk_Window parent,           /* Window to associate with component */
    const char resName[],       /* Name of component */
    const char className[],
    const Tk_ConfigSpec * specsPtr,
    int argc,
    const char *argv[],
    char *widgRec,
    int flags)
{
    Tk_Window       tkwin;
    int             result;
    char           *tempName;
    int             isTemporary = FALSE;

    tempName = RbcStrdup(resName);

    /* Window name can't start with an upper case letter */
    tempName[0] = tolower(resName[0]);

    /*
     * Create component if a child window by the component's name
     * doesn't already exist.
     */
    tkwin = RbcFindChild(parent, tempName);
    if (tkwin == NULL) {
        tkwin = Tk_CreateWindow(interp, parent, tempName, (char *) NULL);
        isTemporary = TRUE;
    }
    if (tkwin == NULL) {
        Tcl_AppendResult(interp, "can't find window in \"", Tk_PathName(parent),
            "\"", (char *) NULL);
        return TCL_ERROR;
    }
    assert(Tk_Depth(tkwin) == Tk_Depth(parent));
    ckfree((char *) tempName);

    Tk_SetClass(tkwin, className);
    result =
        Tk_ConfigureWidget(interp, tkwin, specsPtr, argc, argv, widgRec, flags);
    if (isTemporary) {
        Tk_DestroyWindow(tkwin);
    }
    return result;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
