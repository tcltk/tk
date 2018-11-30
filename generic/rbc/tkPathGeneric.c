/*
 * tkPath.c --
 *
 *        This file implements a path drawing model
 *      SVG counterpart. See http://www.w3.org/TR/SVG11/.
 *        It contains the generic parts that do not refer to the canvas.
 *
 * Copyright (c) 2005-2008  Mats Bengtsson
 *
 */

#include "tkPathInt.h"

MODULE_SCOPE int Tk_PathDepixelize;

static const char TK_PATH_SyntaxError[] = "syntax error in path definition";

enum {
    PATH_NEXT_ERROR,
    PATH_NEXT_INSTRUCTION,
    PATH_NEXT_OTHER
};

/*
 * A placeholder for the context we are working in.
 * The current and lastMove are always original untransformed coordinates.
 */

typedef struct TkPointsContext_ {
    double         current[2];
    double         lastMove[2];
    int            widthCode;
} TkPointsContext_;

/*---------------------------------------------------------------------------*/

static void    PathPdfMoveTo(Tcl_Obj *list,TkPointsContext_ *context,
            double x, double y);
static void    PathPdfLineTo(Tcl_Obj *list, TkPointsContext_ *context,
            double x, double y);
static void    PathPdfCurveTo(Tcl_Obj *list, TkPointsContext_ *context,
            double x1, double y1, double x2, double y2,
            double x, double y);
static void    PathPdfArcTo(Tcl_Obj *list, TkPointsContext_ *context,
            double rx, double ry, double phiDegrees,
            char largeArcFlag, char sweepFlag, double x, double y);
static void    PathPdfQuadBezier(Tcl_Obj *list, TkPointsContext_ *context,
            double ctrlX, double ctrlY, double x, double y);
static void    PathPdfArcToUsingBezier(Tcl_Obj *list,
            TkPointsContext_ *context,
            double rx, double ry, double phiDegrees,
            char largeArcFlag, char sweepFlag,
            double x2, double y2);
static void    PathPdfClosePath(Tcl_Obj *list, TkPointsContext_ *context);
static void    PathPdfOval(Tcl_Obj *list, TkPointsContext_ *context,
            double cx, double cy, double rx, double ry);
static void    PathPdfRect(Tcl_Obj *list, TkPointsContext_ *context,
            double x, double y, double width, double height);
static int    PathPdfGradFuncType2(Tcl_Interp *interp, Tcl_Obj *mkobj,
            int isAlpha, TkGradientStop *stop0, TkGradientStop *stop1);
static int    PathPdfGradSoftMask(Tcl_Interp *interp, Tcl_Obj *mkobj,
            TkPathRect *bbox, const char *gradName, long gradId,
            TkPathMatrix *tmPtr);
static int    PathPdfGradient(Tcl_Interp *interp, int isAlpha, Tcl_Obj *mkobj,
            Tcl_Obj *mkgrad, TkPathRect *bbox,
            TkPathGradientMaster *gradientPtr, char **gradName,
            long *gradId, TkPathMatrix *tmPtr);
static int    PathPdfLinearGradient(Tcl_Interp *interp, int isAlpha,
            Tcl_Obj *mkobj,    Tcl_Obj *mkgrad, TkPathRect *bbox,
            TkLinearGradientFill *fillPtr, TkPathMatrix *mPtr);
static int    PathPdfRadialGradient(Tcl_Interp *interp, int isAlpha,
            Tcl_Obj *mkobj,    Tcl_Obj *mkgrad, TkPathRect *bbox,
            TkRadialGradientFill *fillPtr, TkPathMatrix *mPtr,
            TkPathMatrix *tmPtr);

/*---------------------------------------------------------------------------*/

MODULE_SCOPE int
TkPathPixelAlignObjCmd(ClientData clientData, Tcl_Interp* interp,
        int objc, Tcl_Obj* const objv[])
{
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(TkPathPixelAlign()));
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * GetPathInstruction --
 *
 *        Gets the path instruction at position index of objv.
 *        If unrecognized instruction returns PATH_NEXT_ERROR.
 *
 * Results:
 *        A PATH_NEXT_* result.
 *
 * Side effects:
 *        None.
 *
 *--------------------------------------------------------------
 */

static int
GetPathInstruction(Tcl_Interp *interp, Tcl_Obj *const objv[], int index,
           char *c)
{
    int len;
    int result;
    char *str;

    *c = '\0';
    str = Tcl_GetStringFromObj(objv[index], &len);
    if (isalpha(str[0])) {
        if (len != 1) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(TK_PATH_SyntaxError, -1));
            result = PATH_NEXT_ERROR;
        } else {
            switch (str[0]) {
                case 'M': case 'm': case 'L': case 'l':
                case 'H': case 'h': case 'V': case 'v':
                case 'A': case 'a': case 'Q': case 'q':
                case 'T': case 't': case 'C': case 'c':
                case 'S': case 's': case 'Z': case 'z':
                    result = PATH_NEXT_INSTRUCTION;
                    *c = str[0];
                    break;
                default:
                    Tcl_SetObjResult(interp,
                     Tcl_NewStringObj(TK_PATH_SyntaxError, -1));
                    result = PATH_NEXT_ERROR;
                    break;
            }
        }
    } else {
        result = PATH_NEXT_OTHER;
    }
    return result;
}

/*
 *--------------------------------------------------------------
 *
 * GetPathDouble, GetPathBoolean, GetPathPoint, GetPathTwoPoints,
 * GetPathThreePoints, GetPathArcParameters --
 *
 *        Gets a certain number of numbers from objv.
 *        Increments indexPtr by the number of numbers extracted
 *        if succesful, else it is unchanged.
 *
 * Results:
 *        A standard tcl result.
 *
 * Side effects:
 *        None.
 *
 *--------------------------------------------------------------
 */

static int
GetPathDouble(Tcl_Interp *interp, Tcl_Obj *const objv[], int len,
          int *indexPtr, double *zPtr)
{
    int result;

    if (*indexPtr > len - 1) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(TK_PATH_SyntaxError, -1));
        result = TCL_ERROR;
    } else {
        result = Tcl_GetDoubleFromObj(interp, objv[*indexPtr], zPtr);
        if (result == TCL_OK) {
            (*indexPtr)++;
        }
    }
    return result;
}

static int
GetPathBoolean(Tcl_Interp *interp, Tcl_Obj *const objv[], int len,
           int *indexPtr, char *boolPtr)
{
    int result;
    int boolean;

    if (*indexPtr > len - 1) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(TK_PATH_SyntaxError, -1));
        result = TCL_ERROR;
    } else {
        result = Tcl_GetBooleanFromObj(interp, objv[*indexPtr], &boolean);
        if (result == TCL_OK) {
            (*indexPtr)++;
            *boolPtr = boolean;
        }
    }
    return result;
}

static int
GetPathPoint(Tcl_Interp *interp, Tcl_Obj *const objv[], int len,
         int *indexPtr, double *xPtr, double *yPtr)
{
    int result = TCL_OK;
    int indIn = *indexPtr;

    if (*indexPtr > len - 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(TK_PATH_SyntaxError, -1));
        result = TCL_ERROR;
    } else if (Tcl_GetDoubleFromObj(interp, objv[(*indexPtr)++], xPtr) != TCL_OK) {
        *indexPtr = indIn;
        result = TCL_ERROR;
    } else if (Tcl_GetDoubleFromObj(interp, objv[(*indexPtr)++], yPtr) != TCL_OK) {
        *indexPtr = indIn;
        result = TCL_ERROR;
    }
    return result;
}

static int
GetPathTwoPoints(Tcl_Interp *interp, Tcl_Obj *const objv[], int len,
         int *indexPtr, double *x1Ptr, double *y1Ptr,
         double *x2Ptr, double *y2Ptr)
{
    int result;
    int indIn = *indexPtr;

    result = GetPathPoint(interp, objv, len, indexPtr, x1Ptr, y1Ptr);
    if (result == TCL_OK) {
        if (GetPathPoint(interp, objv, len, indexPtr, x2Ptr, y2Ptr) != TCL_OK) {
            *indexPtr = indIn;
            result = TCL_ERROR;
        }
    }
    return result;
}

static int
GetPathThreePoints(Tcl_Interp *interp, Tcl_Obj *const objv[], int len,
           int *indexPtr, double *x1Ptr, double *y1Ptr,
           double *x2Ptr, double *y2Ptr, double *x3Ptr, double *y3Ptr)
{
    int result;
    int indIn = *indexPtr;

    result = GetPathPoint(interp, objv, len, indexPtr, x1Ptr, y1Ptr);
    if (result == TCL_OK) {
        if (GetPathPoint(interp, objv, len, indexPtr, x2Ptr, y2Ptr) != TCL_OK) {
            *indexPtr = indIn;
            result = TCL_ERROR;
        } else if (GetPathPoint(interp, objv, len, indexPtr,
                x3Ptr, y3Ptr) != TCL_OK) {
            *indexPtr = indIn;
            result = TCL_ERROR;
        }
    }
    return result;
}

static int
GetPathArcParameters(Tcl_Interp *interp, Tcl_Obj *const objv[], int len,
             int *indexPtr,
        double *radXPtr, double *radYPtr, double *anglePtr,
        char *largeArcFlagPtr, char *sweepFlagPtr,
        double *xPtr, double *yPtr)
{
    int result;
    int indIn = *indexPtr;

    result = GetPathPoint(interp, objv, len, indexPtr, radXPtr, radYPtr);
    if (result == TCL_OK) {
        if (GetPathDouble(interp, objv, len, indexPtr, anglePtr) != TCL_OK) {
            *indexPtr = indIn;
            result = TCL_ERROR;
        } else if (GetPathBoolean(interp, objv, len, indexPtr,
                  largeArcFlagPtr) != TCL_OK) {
            *indexPtr = indIn;
            result = TCL_ERROR;
        } else if (GetPathBoolean(interp, objv, len, indexPtr,
                  sweepFlagPtr) != TCL_OK) {
            *indexPtr = indIn;
            result = TCL_ERROR;
        } else if (GetPathPoint(interp, objv, len, indexPtr,
                xPtr, yPtr) != TCL_OK) {
            *indexPtr = indIn;
            result = TCL_ERROR;
        }
    }
    return result;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathNewMoveToAtom, TkPathNewLineToAtom, TkPathNewArcAtom, TkPathNewQuadBezierAtom,
 * TkPathNewCurveToAtom, TkPathNewCloseAtom --
 *
 *        Creates a TkPathAtom of the specified type using the given
 *        parameters. It updates the currentX and currentY.
 *
 * Results:
 *        A TkPathAtom pointer.
 *
 * Side effects:
 *        Memory allocated.
 *
 *--------------------------------------------------------------
 */

TkPathAtom *
TkPathNewMoveToAtom(double x, double y)
{
    TkPathAtom *atomPtr;
    TkMoveToAtom *moveToAtomPtr;

    moveToAtomPtr = (TkMoveToAtom *) ckalloc((unsigned) (sizeof(TkMoveToAtom)));
    atomPtr = (TkPathAtom *) moveToAtomPtr;
    atomPtr->type = TK_PATH_ATOM_M;
    atomPtr->nextPtr = NULL;
    moveToAtomPtr->x = x;
    moveToAtomPtr->y = y;
    return atomPtr;
}

TkPathAtom *
TkPathNewLineToAtom(double x, double y)
{
    TkPathAtom *atomPtr;
    TkLineToAtom *lineToAtomPtr;

    lineToAtomPtr = (TkLineToAtom *) ckalloc((unsigned) (sizeof(TkLineToAtom)));
    atomPtr = (TkPathAtom *) lineToAtomPtr;
    atomPtr->type = TK_PATH_ATOM_L;
    atomPtr->nextPtr = NULL;
    lineToAtomPtr->x = x;
    lineToAtomPtr->y = y;
    return atomPtr;
}

TkPathAtom *
TkPathNewArcAtom(double radX, double radY,
        double angle, char largeArcFlag, char sweepFlag, double x, double y)
{
    TkPathAtom *atomPtr;
    TkArcAtom *arcAtomPtr;

    arcAtomPtr = (TkArcAtom *) ckalloc((unsigned) (sizeof(TkArcAtom)));
    atomPtr = (TkPathAtom *) arcAtomPtr;
    atomPtr->type = TK_PATH_ATOM_A;
    atomPtr->nextPtr = NULL;
    arcAtomPtr->radX = radX;
    arcAtomPtr->radY = radY;
    arcAtomPtr->angle = angle;
    arcAtomPtr->largeArcFlag = largeArcFlag;
    arcAtomPtr->sweepFlag = sweepFlag;
    arcAtomPtr->x = x;
    arcAtomPtr->y = y;
    return atomPtr;
}

TkPathAtom *
TkPathNewQuadBezierAtom(double ctrlX, double ctrlY, double anchorX, double anchorY)
{
    TkPathAtom *atomPtr;
    TkQuadBezierAtom *quadBezierAtomPtr;

    quadBezierAtomPtr =
    (TkQuadBezierAtom *) ckalloc((unsigned) (sizeof(TkQuadBezierAtom)));
    atomPtr = (TkPathAtom *) quadBezierAtomPtr;
    atomPtr->type = TK_PATH_ATOM_Q;
    atomPtr->nextPtr = NULL;
    quadBezierAtomPtr->ctrlX = ctrlX;
    quadBezierAtomPtr->ctrlY = ctrlY;
    quadBezierAtomPtr->anchorX = anchorX;
    quadBezierAtomPtr->anchorY = anchorY;
    return atomPtr;
}

TkPathAtom *
TkPathNewCurveToAtom(double ctrlX1, double ctrlY1, double ctrlX2, double ctrlY2,
        double anchorX, double anchorY)
{
    TkPathAtom *atomPtr;
    TkCurveToAtom *curveToAtomPtr;

    curveToAtomPtr = (TkCurveToAtom *) ckalloc((unsigned) (sizeof(TkCurveToAtom)));
    atomPtr = (TkPathAtom *) curveToAtomPtr;
    atomPtr->type = TK_PATH_ATOM_C;
    atomPtr->nextPtr = NULL;
    curveToAtomPtr->ctrlX1 = ctrlX1;
    curveToAtomPtr->ctrlY1 = ctrlY1;
    curveToAtomPtr->ctrlX2 = ctrlX2;
    curveToAtomPtr->ctrlY2 = ctrlY2;
    curveToAtomPtr->anchorX = anchorX;
    curveToAtomPtr->anchorY = anchorY;
    return atomPtr;
}

TkPathAtom *
TkPathNewRectAtom(double pointsPtr[])
{
    TkPathAtom *atomPtr;
    TkRectAtom *rectAtomPtr;

    rectAtomPtr = (TkRectAtom *) ckalloc((unsigned) (sizeof(TkRectAtom)));
    atomPtr = (TkPathAtom *) rectAtomPtr;
    atomPtr->nextPtr = NULL;
    atomPtr->type = TK_PATH_ATOM_RECT;
    rectAtomPtr->x = pointsPtr[0];
    rectAtomPtr->y = pointsPtr[1];
    rectAtomPtr->width = pointsPtr[2] - pointsPtr[0];
    rectAtomPtr->height = pointsPtr[3] - pointsPtr[1];
    return atomPtr;
}

TkPathAtom *
TkPathNewCloseAtom(double x, double y)
{
    TkPathAtom *atomPtr;
    TkCloseAtom *closeAtomPtr;

    closeAtomPtr = (TkCloseAtom *) ckalloc((unsigned) (sizeof(TkCloseAtom)));
    atomPtr = (TkPathAtom *) closeAtomPtr;
    atomPtr->type = TK_PATH_ATOM_Z;
    atomPtr->nextPtr = NULL;
    closeAtomPtr->x = x;
    closeAtomPtr->y = y;
    return atomPtr;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathParseToAtoms
 *
 *        Takes a tcl list of values which defines the path item and
 *        parses them into a linked list of path atoms.
 *
 * Results:
 *        A standard Tcl result.
 *
 * Side effects:
 *        None
 *
 *--------------------------------------------------------------
 */

int
TkPathParseToAtoms(Tcl_Interp *interp, Tcl_Obj *listObjPtr,
           TkPathAtom **atomPtrPtr, int *lenPtr)
{
    char     currentInstr;    /* current instruction (M, l, c, etc.) */
    char     lastInstr;    /* previous instruction */
    int     len;
    int     currentInd;
    int     index;
    int     next;
    int     relative;
    double     currentX, currentY;    /* current point */
    double     startX, startY;    /* the current moveto point */
    double     ctrlX, ctrlY;    /* last control point, for s, S, t, T */
    double     x, y;
    Tcl_Obj **objv;
    TkPathAtom *atomPtr = NULL;
    TkPathAtom *currentAtomPtr = NULL;

    *atomPtrPtr = NULL;
    currentX = 0.0;
    currentY = 0.0;
    startX = 0.0;
    startY = 0.0;
    ctrlX = 0.0;
    ctrlY = 0.0;
    lastInstr = 'M';    /* If first instruction is missing it defaults to M ? */
    relative = 0;

    if (Tcl_ListObjGetElements(interp, listObjPtr, lenPtr, &objv) != TCL_OK) {
        return TCL_ERROR;
    }
    len = *lenPtr;

    /* First some error checking. Necessary??? */
    if (len < 3) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
                "path specification too short", -1));
        return TCL_ERROR;
    }
    if ((GetPathInstruction(interp, objv, 0, &currentInstr)
     != PATH_NEXT_INSTRUCTION) || (toupper(currentInstr) != 'M')) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(
                "path must start with M or m", -1));
        return TCL_ERROR;
    }
    currentInd = 1;
    if (GetPathPoint(interp, objv, len, &currentInd, &x, &y) != TCL_OK) {
        return TCL_ERROR;
    }
    currentInd = 0;

    while (currentInd < len) {

        next = GetPathInstruction(interp, objv, currentInd, &currentInstr);
        if (next == PATH_NEXT_ERROR) {
            goto error;
        } else if (next == PATH_NEXT_INSTRUCTION) {
            relative = islower(currentInstr);
            currentInd++;
        } else if (next == PATH_NEXT_OTHER) {

            /* Use rule to find instruction to use. */
            if (lastInstr == 'M') {
                currentInstr = 'L';
            } else if (lastInstr == 'm') {
                currentInstr = 'l';
            } else {
                currentInstr = lastInstr;
            }
            relative = islower(currentInstr);
        }
        index = currentInd;

        switch (currentInstr) {

            case 'M': case 'm': {
                if (GetPathPoint(interp, objv, len, &index, &x, &y) != TCL_OK) {
                    goto error;
                }
                if (relative) {
                    x += currentX;
                    y += currentY;
                }
                atomPtr = TkPathNewMoveToAtom(x, y);
                if (currentAtomPtr == NULL) {
                    *atomPtrPtr = atomPtr;
                } else {
                    currentAtomPtr->nextPtr = atomPtr;
                }
                currentAtomPtr = atomPtr;
                currentX = x;
                currentY = y;
                startX = x;
                startY = y;
                break;
            }

            case 'L': case 'l': {
                if (index > len - 2) {
                    Tcl_SetObjResult(interp,
                     Tcl_NewStringObj(TK_PATH_SyntaxError, -1));
                    goto error;
                }
                if (GetPathPoint(interp, objv, len, &index, &x, &y) == TCL_OK) {
                    if (relative) {
                        x += currentX;
                        y += currentY;
                    }
                    atomPtr = TkPathNewLineToAtom(x, y);
                    currentAtomPtr->nextPtr = atomPtr;
                    currentAtomPtr = atomPtr;
                    currentX = x;
                    currentY = y;
                } else {
                    goto error;
                }
                break;
            }

            case 'A': case 'a': {
                double radX, radY, angle;
                char largeArcFlag, sweepFlag;

                if (GetPathArcParameters(interp, objv, len, &index,
                        &radX, &radY, &angle, &largeArcFlag, &sweepFlag,
                        &x, &y) == TCL_OK) {
                    if (relative) {
                        x += currentX;
                        y += currentY;
                    }
                    atomPtr = TkPathNewArcAtom(radX, radY, angle,
                     largeArcFlag, sweepFlag, x, y);
                    currentAtomPtr->nextPtr = atomPtr;
                    currentAtomPtr = atomPtr;
                    currentX = x;
                    currentY = y;
                } else {
                    goto error;
                }
                break;
            }

            case 'C': case 'c': {
                double x1, y1, x2, y2;    /* The two control points. */

                if (index > len - 6) {
                    Tcl_SetObjResult(interp,
                     Tcl_NewStringObj(TK_PATH_SyntaxError, -1));
                    goto error;
                }
                if (GetPathThreePoints(interp, objv, len, &index,
                       &x1, &y1, &x2, &y2, &x, &y) == TCL_OK) {
                    if (relative) {
                        x1 += currentX;
                        y1 += currentY;
                        x2 += currentX;
                        y2 += currentY;
                        x  += currentX;
                        y  += currentY;
                    }
                    atomPtr = TkPathNewCurveToAtom(x1, y1, x2, y2, x, y);
                    currentAtomPtr->nextPtr = atomPtr;
                    currentAtomPtr = atomPtr;
                    ctrlX = x2; /* Keep track of the last control point. */
                    ctrlY = y2;
                    currentX = x;
                    currentY = y;
                } else {
                    goto error;
                }
                break;
            }

            case 'S': case 's': {
                double x1, y1;    /* The first control point. */
                double x2, y2;    /* The second control point. */

                if ((toupper(lastInstr) == 'C') ||
            (toupper(lastInstr) == 'S')) {
                    /*
             * The first controlpoint is the reflection
             * of the last one about the current point:
             */
                    x1 = 2 * currentX - ctrlX;
                    y1 = 2 * currentY - ctrlY;
                } else {
                    /* The first controlpoint is equal to the current point: */
                    x1 = currentX;
                    y1 = currentY;
                }
                if (index > len - 4) {
                    Tcl_SetObjResult(interp, Tcl_NewStringObj(TK_PATH_SyntaxError, -1));
                    goto error;
                }
                if (GetPathTwoPoints(interp, objv, len, &index,
                     &x2, &y2, &x, &y) == TCL_OK) {
                    if (relative) {
                        x2 += currentX;
                        y2 += currentY;
                        x  += currentX;
                        y  += currentY;
                    }
                    atomPtr = TkPathNewCurveToAtom(x1, y1, x2, y2, x, y);
                    currentAtomPtr->nextPtr = atomPtr;
                    currentAtomPtr = atomPtr;
                    ctrlX = x2; /* Keep track of the last control point. */
                    ctrlY = y2;
                    currentX = x;
                    currentY = y;
                } else {
                    goto error;
                }
                break;
            }

            case 'Q': case 'q': {
                double x1, y1;    /* The control point. */

                if (GetPathTwoPoints(interp, objv, len, &index,
                     &x1, &y1, &x, &y) == TCL_OK) {
                    if (relative) {
                        x1 += currentX;
                        y1 += currentY;
                        x  += currentX;
                        y  += currentY;
                    }
                    atomPtr = TkPathNewQuadBezierAtom(x1, y1, x, y);
                    currentAtomPtr->nextPtr = atomPtr;
                    currentAtomPtr = atomPtr;
                    ctrlX = x1; /* Keep track of the last control point. */
                    ctrlY = y1;
                    currentX = x;
                    currentY = y;
                } else {
                    goto error;
                }
                break;
            }

            case 'T': case 't': {
                double x1, y1;    /* The control point. */

                if ((toupper(lastInstr) == 'Q') ||
            (toupper(lastInstr) == 'T')) {
                    /*
             * The controlpoint is the reflection
             * of the last one about the current point:
             */
                    x1 = 2 * currentX - ctrlX;
                    y1 = 2 * currentY - ctrlY;
                } else {
                    /* The controlpoint is equal to the current point: */
                    x1 = currentX;
                    y1 = currentY;
                }
                if (GetPathPoint(interp, objv, len, &index, &x, &y) == TCL_OK) {
                    if (relative) {
                        x  += currentX;
                        y  += currentY;
                    }
                    atomPtr = TkPathNewQuadBezierAtom(x1, y1, x, y);
                    currentAtomPtr->nextPtr = atomPtr;
                    currentAtomPtr = atomPtr;
                    ctrlX = x1;    /* Keep track of the last control point. */
                    ctrlY = y1;
                    currentX = x;
                    currentY = y;
                } else {
                    goto error;
                }
                break;
            }

            case 'H': {
                while ((index < len) &&
                        (GetPathDouble(interp, objv, len, &index, &x)
             == TCL_OK))
                    ;
                atomPtr = TkPathNewLineToAtom(x, currentY);
                currentAtomPtr->nextPtr = atomPtr;
                currentAtomPtr = atomPtr;
                currentX = x;
                break;
            }

            case 'h': {
                double z;

                x = currentX;
                while ((index < len) &&
               (GetPathDouble(interp, objv, len, &index, &z)
            == TCL_OK)) {
                    x += z;
                }
                atomPtr = TkPathNewLineToAtom(x, currentY);
                currentAtomPtr->nextPtr = atomPtr;
                currentAtomPtr = atomPtr;
                currentX = x;
                break;
            }

            case 'V': {
                while ((index < len) &&
               (GetPathDouble(interp, objv, len, &index, &y)
            == TCL_OK))
                    ;
                atomPtr = TkPathNewLineToAtom(currentX, y);
                currentAtomPtr->nextPtr = atomPtr;
                currentAtomPtr = atomPtr;
                currentY = y;
                break;
            }

            case 'v': {
                double z;

                y = currentY;
                while ((index < len) &&
               (GetPathDouble(interp, objv, len, &index, &z)
            == TCL_OK)) {
                    y += z;
                }
                atomPtr = TkPathNewLineToAtom(currentX, y);
                currentAtomPtr->nextPtr = atomPtr;
                currentAtomPtr = atomPtr;
                currentY = y;
                break;
            }

            case 'Z': case 'z': {
                atomPtr = TkPathNewCloseAtom(startX, startY);
                currentAtomPtr->nextPtr = atomPtr;
                currentAtomPtr = atomPtr;
                currentX = startX;
                currentY = startY;
                break;
            }

            default: {
                Tcl_SetObjResult(interp, Tcl_NewStringObj(
                        "unrecognized path instruction", -1));
                goto error;
            }
        }
        currentInd = index;
        lastInstr = currentInstr;
    }

    /* When we parse coordinates there may be some junk result
     * left in the interpreter to be cleared out. */
    Tcl_ResetResult(interp);
    return TCL_OK;

error:

    TkPathFreeAtoms(*atomPtrPtr);
    *atomPtrPtr = NULL;
    return TCL_ERROR;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathFreeAtoms
 *
 *        Frees up all memory allocated for the path atoms.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        None.
 *
 *--------------------------------------------------------------
 */

void
TkPathFreeAtoms(TkPathAtom *pathAtomPtr)
{
    TkPathAtom *tmpAtomPtr;

    while (pathAtomPtr != NULL) {
        tmpAtomPtr = pathAtomPtr;
        pathAtomPtr = tmpAtomPtr->nextPtr;
        ckfree((char *) tmpAtomPtr);
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkPathNormalize
 *
 *        Takes a list of TkPathAtoms and creates a tcl list where
 *        elements have a standard form. All upper case instructions,
 *        no repeates.
 *
 * Results:
 *        A standard Tcl result.
 *
 * Side effects:
 *        New list returned in listObjPtrPtr.
 *
 *--------------------------------------------------------------
 */

int
TkPathNormalize(Tcl_Interp *interp, TkPathAtom *atomPtr, Tcl_Obj **listObjPtrPtr)
{
    Tcl_Obj *normObjPtr;

    normObjPtr = Tcl_NewListObj( 0, (Tcl_Obj **) NULL );

    while (atomPtr != NULL) {

        switch (atomPtr->type) {
            case TK_PATH_ATOM_M: {
                TkMoveToAtom *move = (TkMoveToAtom *) atomPtr;

                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewStringObj("M", -1));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(move->x));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(move->y));
                break;
            }
            case TK_PATH_ATOM_L: {
                TkLineToAtom *line = (TkLineToAtom *) atomPtr;

                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewStringObj("L", -1));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(line->x));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(line->y));
                break;
            }
            case TK_PATH_ATOM_A: {
                TkArcAtom *arc = (TkArcAtom *) atomPtr;

                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewStringObj("A", -1));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(arc->radX));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(arc->radY));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(arc->angle));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewBooleanObj(arc->largeArcFlag));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewBooleanObj(arc->sweepFlag));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(arc->x));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(arc->y));
                break;
            }
            case TK_PATH_ATOM_Q: {
                TkQuadBezierAtom *quad = (TkQuadBezierAtom *) atomPtr;

                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewStringObj("Q", -1));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(quad->ctrlX));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(quad->ctrlY));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(quad->anchorX));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(quad->anchorY));
                break;
            }
            case TK_PATH_ATOM_C: {
                TkCurveToAtom *curve = (TkCurveToAtom *) atomPtr;

                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewStringObj("C", -1));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(curve->ctrlX1));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(curve->ctrlY1));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(curve->ctrlX2));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(curve->ctrlY2));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(curve->anchorX));
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewDoubleObj(curve->anchorY));
                break;
            }
            case TK_PATH_ATOM_Z: {
                Tcl_ListObjAppendElement(interp, normObjPtr,
                     Tcl_NewStringObj("Z", -1));
                break;
            }
            case TK_PATH_ATOM_ELLIPSE:
            case TK_PATH_ATOM_RECT: {
                /* Empty. */
                break;
            }
        }
        atomPtr = atomPtr->nextPtr;
    }
    *listObjPtrPtr = normObjPtr;
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathMakePath
 *
 *        Defines the path using the TkPathAtom.
 *
 * Results:
 *        A standard Tcl result.
 *
 * Side effects:
 *        Defines the current path in drawable.
 *
 *--------------------------------------------------------------
 */

int
TkPathMakePath(
    TkPathContext context,
    TkPathAtom *atomPtr,
    Tk_PathStyle *stylePtr)
{
    TkPathBeginPath(context, stylePtr);

    while (atomPtr != NULL) {

        switch (atomPtr->type) {
            case TK_PATH_ATOM_M: {
                TkMoveToAtom *move = (TkMoveToAtom *) atomPtr;
                TkPathMoveTo(context, move->x, move->y);
                break;
            }
            case TK_PATH_ATOM_L: {
                TkLineToAtom *line = (TkLineToAtom *) atomPtr;
                TkPathLineTo(context, line->x, line->y);
                break;
            }
            case TK_PATH_ATOM_A: {
                TkArcAtom *arc = (TkArcAtom *) atomPtr;
                TkPathArcTo(context, arc->radX, arc->radY, arc->angle,
                        arc->largeArcFlag, arc->sweepFlag,
                        arc->x, arc->y);
                break;
            }
            case TK_PATH_ATOM_Q: {
                TkQuadBezierAtom *quad = (TkQuadBezierAtom *) atomPtr;
                TkPathQuadBezier(context,
                        quad->ctrlX, quad->ctrlY,
                        quad->anchorX, quad->anchorY);
                break;
            }
            case TK_PATH_ATOM_C: {
                TkCurveToAtom *curve = (TkCurveToAtom *) atomPtr;
                TkPathCurveTo(context,
                        curve->ctrlX1, curve->ctrlY1,
                        curve->ctrlX2, curve->ctrlY2,
                        curve->anchorX, curve->anchorY);
                break;
            }
            case TK_PATH_ATOM_Z: {
                TkPathClosePath(context);
                break;
            }
            case TK_PATH_ATOM_ELLIPSE: {
                TkEllipseAtom *ell = (TkEllipseAtom *) atomPtr;
                TkPathOval(context, ell->cx, ell->cy, ell->rx, ell->ry);
                break;
            }
            case TK_PATH_ATOM_RECT: {
                TkRectAtom *rect = (TkRectAtom *) atomPtr;
                TkPathRectangle(context, rect->x, rect->y,
               rect->width, rect->height);
                break;
            }
        }
        atomPtr = atomPtr->nextPtr;
    }
    TkPathEndPath(context);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathArcToUsingBezier
 *
 *        Translates an ArcTo drawing into a sequence of CurveTo.
 *        Helper function for the platform specific drawing code.
 *
 * Results:
 *        None.
 *
 * Side effects:
 *        None.
 *
 *--------------------------------------------------------------
 */

void
TkPathArcToUsingBezier(TkPathContext ctx,
        double rx, double ry,
        double phiDegrees,     /* The rotation angle in degrees! */
        char largeArcFlag, char sweepFlag, double x2, double y2)
{
    int result;
    int i, segments;
    double x1, y1;
    double cx, cy;
    double theta1, dtheta, phi;
    double sinPhi, cosPhi;
    double delta, t;
    TkPathPoint pt;

    TkPathGetCurrentPosition(ctx, &pt);
    x1 = pt.x;
    y1 = pt.y;

    /* All angles except phi is in radians! */
    phi = phiDegrees * DEGREES_TO_RADIANS;

    /* Check return value and take action. */
    result = TkPathEndpointToCentralArcParameters(x1, y1,
            x2, y2, rx, ry, phi, largeArcFlag, sweepFlag,
            &cx, &cy, &rx, &ry,
            &theta1, &dtheta);
    if (result == TK_PATH_ARC_Skip) {
    return;
    } else if (result == TK_PATH_ARC_Line) {
    TkPathLineTo(ctx, x2, y2);
    return;
    }
    sinPhi = sin(phi);
    cosPhi = cos(phi);

    /*
     * Convert into cubic bezier segments <= 90deg
     * (from mozilla/svg; not checked)
     */
    segments = (int) ceil(fabs(dtheta/(M_PI/2.0)));
    delta = dtheta/segments;
    t = 8.0/3.0 * sin(delta/4.0) * sin(delta/4.0) / sin(delta/2.0);

    for (i = 0; i < segments; ++i) {
        double cosTheta1 = cos(theta1);
        double sinTheta1 = sin(theta1);
        double theta2 = theta1 + delta;
        double cosTheta2 = cos(theta2);
        double sinTheta2 = sin(theta2);

        /* a) calculate endpoint of the segment: */
        double xe = cosPhi * rx*cosTheta2 - sinPhi * ry*sinTheta2 + cx;
        double ye = sinPhi * rx*cosTheta2 + cosPhi * ry*sinTheta2 + cy;

        /* b) calculate gradients at start/end points of segment: */
        double dx1 = t * ( - cosPhi * rx*sinTheta1 - sinPhi * ry*cosTheta1);
        double dy1 = t * ( - sinPhi * rx*sinTheta1 + cosPhi * ry*cosTheta1);

        double dxe = t * ( cosPhi * rx*sinTheta2 + sinPhi * ry*cosTheta2);
        double dye = t * ( sinPhi * rx*sinTheta2 - cosPhi * ry*cosTheta2);

        /* c) draw the cubic bezier: */
        TkPathCurveTo(ctx, x1+dx1, y1+dy1, xe+dxe, ye+dye, xe, ye);

        /* do next segment */
        theta1 = theta2;
        x1 = (float) xe;
        y1 = (float) ye;
    }
}

/*---------------------------------------------------------------------------*/

static int
PrintNumber(
    char *buffer,
    int fracDigits,
    double number)
{
    int len;

    sprintf(buffer, "%.*f", fracDigits, number);
    len = strlen(buffer);
    while (len > 0) {
        if (buffer[len-1] != '0') {
            break;
        }
        --len;
        buffer[len] = '\0';
    }
    if ((len > 0) && (buffer[len-1] == '.')) {
        --len;
        buffer[len] = '\0';
    }
    if ((len == 0) || (strcmp(buffer, "-0") == 0)) {
        strcpy(buffer, "0");
        len = 1;
    }
    return len;
}

/*---------------------------------------------------------------------------*/

MODULE_SCOPE int
TkPathPdfNumber(
    Tcl_Obj *ret,
    int fracDigits,
    double number,
    const char *append)
{
    char buffer[TCL_DOUBLE_SPACE*2];
    int len = PrintNumber(buffer, fracDigits, number);

    Tcl_AppendToObj(ret, buffer, len);
    if (append != NULL) {
        Tcl_AppendToObj(ret, append, -1);
    }
    return len;
}

/*---------------------------------------------------------------------------*/

MODULE_SCOPE int
TkPathPdfColor(
    Tcl_Obj *ret,
    XColor *colorPtr,
    const char *command)
{
    double red, green, blue;

    /*
     * No color map entry for this color. Grab the color's intensities and
     * output Postscript commands for them. Special note: X uses a range of
     * 0-65535 for intensities, but most displays only use a range of 0-255,
     * which maps to (0, 256, 512, ... 65280) in the X scale. This means that
     * there's no way to get perfect white, since the highest intensity is
     * only 65280 out of 65535. To work around this problem, rescale the X
     * intensity to a 0-255 scale and use that as the basis for the Postscript
     * colors. This scheme still won't work if the display only uses 4 bits
     * per color, but most diplays use at least 8 bits.
     */

    red = ((double) (((int) colorPtr->red) >> 8))/255.0;
    green = ((double) (((int) colorPtr->green) >> 8))/255.0;
    blue = ((double) (((int) colorPtr->blue) >> 8))/255.0;
    TkPathPdfNumber(ret, 3, red, " ");
    TkPathPdfNumber(ret, 3, green, " ");
    TkPathPdfNumber(ret, 3, blue, " ");
    Tcl_AppendToObj(ret, command, -1);
    Tcl_AppendToObj(ret, "\n", 1);
    return TCL_OK;
}

/*---------------------------------------------------------------------------*/

MODULE_SCOPE int
TkPathPdfArrow(
    Tcl_Interp *interp,
    TkPathArrowDescr *arrow,
    Tk_PathStyle *const style)
{
    Tk_PathStyle arrowStyle = *style;
    TkPathColor fc;
    TkPathAtom *atomPtr;

    if (arrow->arrowEnabled && arrow->arrowPointsPtr != NULL) {
    Tcl_Obj *ret;

    arrowStyle.matrixPtr = NULL;
    if (arrow->arrowFillRatio > 0.0 && arrow->arrowLength != 0.0) {
        arrowStyle.strokeWidth = 0.0;
        fc.color = arrowStyle.strokeColor;
        fc.gradientInstPtr = NULL;
        arrowStyle.fill = &fc;
        arrowStyle.fillOpacity = arrowStyle.strokeOpacity;
    } else {
        arrowStyle.fill = NULL;
        arrowStyle.fillOpacity = 1.0;
        arrowStyle.joinStyle = 1;
        arrowStyle.dashPtr = NULL;
    }
    atomPtr = TkPathMakePathAtomsFromArrow(arrow);
    ret = Tcl_GetObjResult(interp);
    Tcl_IncrRefCount(ret);
    Tcl_ResetResult(interp);
    if (TkPathPdf(interp, atomPtr, &arrowStyle, NULL, 0, NULL) != TCL_OK) {
        Tcl_DecrRefCount(ret);
        TkPathFreeAtoms(atomPtr);
        return TCL_ERROR;
    }
    Tcl_AppendObjToObj(ret, Tcl_GetObjResult(interp));
    Tcl_SetObjResult(interp, ret);
    TkPathFreeAtoms(atomPtr);
    }
    return TCL_OK;
}

/*---------------------------------------------------------------------------*/

MODULE_SCOPE int
TkPathPdf(
    Tcl_Interp *interp,     /* Used to return resulting info */
    TkPathAtom *atom0Ptr,     /* The actual path as a linked list
                             * of TkPathAtoms. */
    Tk_PathStyle *stylePtr, /* The path's style. */
    TkPathRect *bboxPtr,      /* The bounding box or NULL. */
    int objc,            /* Number of arguments of callback. */
    Tcl_Obj *const objv[])  /* Argument list of callback. */
{
    Tcl_Obj *ret = Tcl_NewObj();
    Tcl_Obj *mkextgs = (objc > 0) ? objv[0] : NULL;
    Tcl_Obj *mkobj = (objc > 1) ? objv[1] : NULL;
    Tcl_Obj *mkgrad = (objc > 2) ? objv[2] : NULL;
    Tcl_Obj *gsAlpha = NULL;
    int myZ = 0, f = 0, s = 0, isLinear = 0, i;
    TkPointsContext_ context;
    TkPathAtom *atomPtr;
    char *gradName = NULL;
    TkPathMatrix gm;

    context.current[0] = context.current[1] = 0.;
    context.lastMove[0] = context.lastMove[1] = 0.;
    context.widthCode = 0; /* TODO check */

    if (stylePtr != NULL) {
    TkPathGradientMaster *gradientPtr = GetGradientMasterFromPathColor(stylePtr->fill);

    if ((stylePtr->dashPtr != NULL) && (stylePtr->dashPtr->number > 0)) {
	    Tcl_AppendToObj(ret, "q [ ", 4);
	    for (i = 0; i < stylePtr->dashPtr->number; i++) {
		TkPathPdfNumber(ret, 6, stylePtr->dashPtr->array[i], " ");
	    }
	    Tcl_AppendToObj(ret, "] ", 2);
	    TkPathPdfNumber(ret, 6, stylePtr->offset, " d\n");
	}

    if (gradientPtr != NULL) {
        isLinear = (gradientPtr->type == TK_PATH_GRADIENTTYPE_LINEAR);
    }
    if (mkextgs != NULL) {
        int retc;
        long gradId, smaskId;
        char *gradAlpha = NULL;
        Tcl_Obj *gs, *cmd, **retv;

        if ((mkgrad != NULL) &&
        (gradientPtr != NULL) &&
        (bboxPtr != NULL)) {
        if (PathPdfGradient(interp, 1, mkobj, mkgrad, bboxPtr,
                    gradientPtr, &gradAlpha, &gradId, &gm)
            != TCL_OK) {
            return TCL_ERROR;
        }
        }
        if (gradAlpha != NULL) {
        if (PathPdfGradSoftMask(interp, mkobj, bboxPtr,
                    gradAlpha, gradId,
                    isLinear ? NULL : &gm) != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_GetLongFromObj(NULL, Tcl_GetObjResult(interp), &smaskId);
        gs = TkPathExtGS(stylePtr, &smaskId);
        cmd = Tcl_DuplicateObj(mkextgs);
        Tcl_IncrRefCount(cmd);
        if ((Tcl_ListObjAppendElement(interp, cmd, gs) != TCL_OK) ||
            (Tcl_ListObjAppendElement(interp, cmd,
                Tcl_NewLongObj(smaskId)) != TCL_OK)) {
            Tcl_DecrRefCount(cmd);
            Tcl_DecrRefCount(gs);
            Tcl_DecrRefCount(ret);
            return TCL_ERROR;
        }
        if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_DIRECT) != TCL_OK) {
            Tcl_DecrRefCount(cmd);
            Tcl_DecrRefCount(ret);
            return TCL_ERROR;
        }
        Tcl_DecrRefCount(cmd);
        /*
         * Get name of extended graphics state.
         */
        if (Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp),
                       &retc, &retv) != TCL_OK) {
            Tcl_DecrRefCount(ret);
            return TCL_ERROR;
        }
        if (retc < 2) {
            Tcl_SetResult(interp, (char *) "missing PDF id/name",
                  TCL_STATIC);
            Tcl_DecrRefCount(ret);
            return TCL_ERROR;
        }
        gsAlpha = retv[1];
        Tcl_IncrRefCount(gsAlpha);
        }
        gs = TkPathExtGS(stylePtr, NULL);
        if (gs != NULL) {
        cmd = Tcl_DuplicateObj(mkextgs);
        Tcl_IncrRefCount(cmd);
        if ((Tcl_ListObjAppendElement(interp, cmd, gs) != TCL_OK) ||
            ((gradAlpha != NULL) &&
             (Tcl_ListObjAppendElement(interp, cmd,
                Tcl_NewLongObj(smaskId)) != TCL_OK))) {
            Tcl_DecrRefCount(cmd);
            Tcl_DecrRefCount(gs);
            Tcl_DecrRefCount(ret);
            if (gsAlpha != NULL) {
            Tcl_DecrRefCount(gsAlpha);
            }
            return TCL_ERROR;
        }
        if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_DIRECT) != TCL_OK) {
            Tcl_DecrRefCount(cmd);
            Tcl_DecrRefCount(ret);
            if (gsAlpha != NULL) {
            Tcl_DecrRefCount(gsAlpha);
            }
            return TCL_ERROR;
        }
        Tcl_DecrRefCount(cmd);
        /*
         * Get name of extended graphics state.
         */
        if (Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp),
                       &retc, &retv) != TCL_OK) {
            Tcl_DecrRefCount(ret);
            if (gsAlpha != NULL) {
            Tcl_DecrRefCount(gsAlpha);
            }
            return TCL_ERROR;
        }
        if (retc < 2) {
            Tcl_SetResult(interp, (char *) "missing PDF id/name",
                  TCL_STATIC);
            Tcl_DecrRefCount(ret);
            if (gsAlpha != NULL) {
            Tcl_DecrRefCount(gsAlpha);
            }
            return TCL_ERROR;
        }
        Tcl_AppendPrintfToObj(ret, "/%s gs\n", Tcl_GetString(retv[1]));
        }
    }
    if (stylePtr->matrixPtr != NULL) {
        /* TkPathMatrix */
        TkPathPdfNumber(ret, 6, stylePtr->matrixPtr->a, " ");
        TkPathPdfNumber(ret, 6, stylePtr->matrixPtr->b, " ");
        TkPathPdfNumber(ret, 6, stylePtr->matrixPtr->c, " ");
        TkPathPdfNumber(ret, 6, stylePtr->matrixPtr->d, " ");
        TkPathPdfNumber(ret, 3, stylePtr->matrixPtr->tx, " ");
        TkPathPdfNumber(ret, 3, stylePtr->matrixPtr->ty, " cm\n");
    }
    if ((mkgrad != NULL) && (gradientPtr != NULL) && (bboxPtr != NULL)) {
        if (PathPdfGradient(interp, 0, mkobj, mkgrad, bboxPtr,
                gradientPtr, &gradName, NULL, &gm) != TCL_OK) {
        Tcl_DecrRefCount(ret);
        if (gsAlpha != NULL) {
            Tcl_DecrRefCount(gsAlpha);
        }
        return TCL_ERROR;
        }
    }
    TkPathPdfNumber(ret, 3, stylePtr->strokeWidth, " w\n");
    if (stylePtr->strokeColor) {
        TkPathPdfColor(ret, stylePtr->strokeColor, "RG");
        s = 1;
    }
    if (stylePtr->fill && stylePtr->fill->color) {
        TkPathPdfColor(ret, stylePtr->fill->color, "rg");
        f = 1;
    }
    if (stylePtr->capStyle == CapRound) {
               Tcl_AppendToObj(ret, "1 J\n", 4);
    } else if (stylePtr->capStyle == CapProjecting) {
        Tcl_AppendToObj(ret, "2 J\n", 4);
    }
    if (stylePtr->joinStyle == JoinRound) {
        Tcl_AppendToObj(ret, "1 j\n", 4);
    } else if (stylePtr->joinStyle == JoinBevel) {
        Tcl_AppendToObj(ret, "2 j\n", 4);
    }
    }
    if (gradName != NULL) {
    Tcl_AppendToObj(ret, "q\n", 2);
    if (isLinear && (gsAlpha != NULL)) {
        Tcl_AppendPrintfToObj(ret, "/%s gs\n", Tcl_GetString(gsAlpha));
        Tcl_DecrRefCount(gsAlpha);
        gsAlpha = NULL;
    }
    }
again:
    atomPtr = atom0Ptr;
    /* borrowed from TkPathMakePath() */
    while (atomPtr != NULL) {
        switch (atomPtr->type) {
            case TK_PATH_ATOM_M: {
                TkMoveToAtom *move = (TkMoveToAtom *) atomPtr;
                PathPdfMoveTo(ret, &context, move->x, move->y);
                myZ = 0;
                break;
            }
            case TK_PATH_ATOM_L: {
                TkLineToAtom *line = (TkLineToAtom *) atomPtr;
                PathPdfLineTo(ret, &context, line->x, line->y);
                myZ = 1;
                break;
            }
            case TK_PATH_ATOM_A: {
                TkArcAtom *arc = (TkArcAtom *) atomPtr;
                PathPdfArcTo(ret, &context, arc->radX, arc->radY, arc->angle,
                        arc->largeArcFlag, arc->sweepFlag,
                        arc->x, arc->y);
                myZ = 1;
                break;
            }
            case TK_PATH_ATOM_Q: {
                TkQuadBezierAtom *quad = (TkQuadBezierAtom *) atomPtr;
                PathPdfQuadBezier(ret, &context,
                        quad->ctrlX, quad->ctrlY,
                        quad->anchorX, quad->anchorY);
                myZ = 1;
                break;
            }
            case TK_PATH_ATOM_C: {
                TkCurveToAtom *curve = (TkCurveToAtom *) atomPtr;

                PathPdfCurveTo(ret, &context,
                        curve->ctrlX1, curve->ctrlY1,
                        curve->ctrlX2, curve->ctrlY2,
                        curve->anchorX, curve->anchorY);
                myZ = 1;
                break;
            }
            case TK_PATH_ATOM_Z: {
                if (myZ) {
                    PathPdfClosePath(ret, &context);
                    myZ = 0;
                }
                break;
            }
            case TK_PATH_ATOM_ELLIPSE: {
                TkEllipseAtom *ell = (TkEllipseAtom *) atomPtr;
                PathPdfOval(ret, &context, ell->cx, ell->cy, ell->rx, ell->ry);
                myZ = 0;
                break;
            }
            case TK_PATH_ATOM_RECT: {
                TkRectAtom *rect = (TkRectAtom *) atomPtr;
                PathPdfRect(ret, &context, rect->x, rect->y,
                        rect->width, rect->height);
                myZ = 0;
                break;
            }
        }
        atomPtr = atomPtr->nextPtr;
    }
    if (gradName != NULL) {
        f = 0;
        /* set clipping and fill using gradient */
        Tcl_AppendToObj(ret, "W n\n", 4);
        TkPathPdfNumber(ret, 6, gm.a, " ");
        TkPathPdfNumber(ret, 6, gm.b, " ");
        TkPathPdfNumber(ret, 6, gm.c, " ");
        TkPathPdfNumber(ret, 6, gm.d, " ");
        TkPathPdfNumber(ret, 3, gm.tx, " ");
        TkPathPdfNumber(ret, 3, gm.ty, " cm\n");
        if (gsAlpha != NULL) {
            Tcl_AppendPrintfToObj(ret, "/%s gs\n", Tcl_GetString(gsAlpha));
            Tcl_DecrRefCount(gsAlpha);
            gsAlpha = NULL;
        }
        Tcl_AppendPrintfToObj(ret, "/%s sh\nQ\n", gradName);
        gradName = NULL;
        if (s) {
            goto again;
        }
    }
    if (gsAlpha != NULL) {
        Tcl_DecrRefCount(gsAlpha);
        gsAlpha = NULL;
    }
    if (f && s) {
        Tcl_AppendToObj(ret, (stylePtr->fillRule == EvenOddRule) ?
            "B*\n" : "B\n", -1);
    } else if (f && !s) {
        Tcl_AppendToObj(ret, (stylePtr->fillRule == EvenOddRule) ?
            "f*\n" : "f\n", -1);
    } else if (s) {
        Tcl_AppendToObj(ret, "S\n", 2);
    } else {
        Tcl_AppendToObj(ret, "n\n", 2);
    }
    if ((stylePtr != NULL) && (stylePtr->dashPtr != NULL) &&
	(stylePtr->dashPtr->number > 1)) {
	Tcl_AppendToObj(ret, "Q\n", 2);
    }
    Tcl_SetObjResult(interp, ret);
    return TCL_OK;
}

/*---------------------------------------------------------------------------*/

static void
PathPdfMoveTo(
    Tcl_Obj *list,
    TkPointsContext_ *context,
    double x, double y)
{
    context->current[0] = x;
    context->current[1] = y;
    context->lastMove[0] = x;
    context->lastMove[1] = y;
    TkPathPdfNumber(list, 3, x, " ");
    TkPathPdfNumber(list, 3, y, " m\n");
}

/*---------------------------------------------------------------------------*/

static void
PathPdfLineTo(
    Tcl_Obj *list,
    TkPointsContext_ *context,
    double x, double y)
{
    context->current[0] = x;
    context->current[1] = y;
    TkPathPdfNumber(list, 3, x, " ");
    TkPathPdfNumber(list, 3, y, " l\n");
}

/*---------------------------------------------------------------------------*/

static void
PathPdfCurveTo(
    Tcl_Obj *list,
    TkPointsContext_ *context,
    double x1, double y1, double x2, double y2, double x, double y)
{
    double coordPtr[2*TK_PATH_NUMSEGEMENTS_CurveTo];
    double control[8];
    int i;

    control[0] = context->current[0];
    control[1] = context->current[1];
    control[2] = x1;
    control[3] = y1;
    control[4] = x2;
    control[5] = y2;
    control[6] = x;
    control[7] = y;
    TkPathCurveSegments(control, 0, TK_PATH_NUMSEGEMENTS_CurveTo, coordPtr);
    for (i = 0; i < 2 * TK_PATH_NUMSEGEMENTS_CurveTo; i = i + 2) {
    TkPathPdfNumber(list, 3, coordPtr[i], " ");
    TkPathPdfNumber(list, 3, coordPtr[i+1], " l\n");
    }
    context->current[0] = x;
    context->current[1] = y;
}

/*---------------------------------------------------------------------------*/

static void
PathPdfArcTo(
    Tcl_Obj *list,
    TkPointsContext_ *context,
    double rx, double ry,
    double phiDegrees,     /* The rotation angle in degrees! */
    char largeArcFlag, char sweepFlag, double x, double y)
{
    if (Tk_PathDepixelize) {
        x = TK_PATH_DEPIXELIZE(context->widthCode, x);
        y = TK_PATH_DEPIXELIZE(context->widthCode, y);
    }
    PathPdfArcToUsingBezier(list,context, rx, ry, phiDegrees,
        largeArcFlag, sweepFlag, x, y);
}

/*---------------------------------------------------------------------------*/

static void
PathPdfQuadBezier(
    Tcl_Obj *list,
    TkPointsContext_ *context,
    double ctrlX, double ctrlY, double x, double y)
{
    double cx, cy;
    double x31, y31, x32, y32;

    cx = context->current[0];
    cy = context->current[1];

    /*
     * Conversion of quadratic bezier curve to cubic bezier curve:
     * (mozilla/svg) Unchecked! Must be an approximation!
     */
    x31 = cx + (ctrlX - cx) * 2 / 3;
    y31 = cy + (ctrlY - cy) * 2 / 3;
    x32 = ctrlX + (x - ctrlX) / 3;
    y32 = ctrlY + (y - ctrlY) / 3;

    PathPdfCurveTo(list, context, x31, y31, x32, y32, x, y);
}

/*---------------------------------------------------------------------------*/

static void
PathPdfArcToUsingBezier(
    Tcl_Obj *list,
    TkPointsContext_ *context,
    double rx, double ry,
    double phiDegrees,     /* The rotation angle in degrees! */
    char largeArcFlag, char sweepFlag, double x2, double y2)
{
    int result;
    int i, segments;
    double x1, y1;
    double cx, cy;
    double theta1, dtheta, phi;
    double sinPhi, cosPhi;
    double delta, t;

    x1 = context->current[0];
    y1 = context->current[1];

    /* All angles except phi is in radians! */
    phi = phiDegrees * DEGREES_TO_RADIANS;

    /* Check return value and take action. */
    result = TkPathEndpointToCentralArcParameters(x1, y1,
            x2, y2, rx, ry, phi, largeArcFlag, sweepFlag,
            &cx, &cy, &rx, &ry,
            &theta1, &dtheta);
    if (result == TK_PATH_ARC_Skip) {
    return;
    } else if (result == TK_PATH_ARC_Line) {
    PathPdfLineTo(list, context, x2, y2);
    return;
    }
    sinPhi = sin(phi);
    cosPhi = cos(phi);

    /*
     * Convert into cubic bezier segments <= 90deg
     * (from mozilla/svg; not checked)
     */
    segments = (int) ceil(fabs(dtheta/(M_PI/2.0)));
    delta = dtheta/segments;
    t = 8.0/3.0 * sin(delta/4.0) * sin(delta/4.0) / sin(delta/2.0);

    for (i = 0; i < segments; ++i) {
        double cosTheta1 = cos(theta1);
        double sinTheta1 = sin(theta1);
        double theta2 = theta1 + delta;
        double cosTheta2 = cos(theta2);
        double sinTheta2 = sin(theta2);

        /* a) calculate endpoint of the segment: */
        double xe = cosPhi * rx*cosTheta2 - sinPhi * ry*sinTheta2 + cx;
        double ye = sinPhi * rx*cosTheta2 + cosPhi * ry*sinTheta2 + cy;

        /* b) calculate gradients at start/end points of segment: */
        double dx1 = t * ( - cosPhi * rx*sinTheta1 - sinPhi * ry*cosTheta1);
        double dy1 = t * ( - sinPhi * rx*sinTheta1 + cosPhi * ry*cosTheta1);

        double dxe = t * ( cosPhi * rx*sinTheta2 + sinPhi * ry*cosTheta2);
        double dye = t * ( sinPhi * rx*sinTheta2 - cosPhi * ry*cosTheta2);

        /* c) draw the cubic bezier: */
        PathPdfCurveTo(list, context, x1+dx1, y1+dy1, xe+dxe, ye+dye, xe, ye);

        /* do next segment */
        theta1 = theta2;
        x1 = (float) xe;
        y1 = (float) ye;
    }
}

/*---------------------------------------------------------------------------*/

static void
PathPdfClosePath(
    Tcl_Obj *list,
    TkPointsContext_ *context)
{
    double xy[2];

    xy[0] = context->current[0] = context->lastMove[0];
    xy[1] = context->current[1] = context->lastMove[1];
    TkPathPdfNumber(list, 3, xy[0], " ");
    TkPathPdfNumber(list, 3, xy[1], " l\n");
}

/*---------------------------------------------------------------------------*/

static void
PathPdfOval(
    Tcl_Obj *list,
    TkPointsContext_ *context,
    double cx, double cy, double rx, double ry)
{
    PathPdfMoveTo(list, context, cx + rx, cy);
    PathPdfArcToUsingBezier(list, context, rx, ry, 0.0, 1, 1, cx - rx, cy);
    PathPdfArcToUsingBezier(list, context, rx, ry, 0.0, 1, 1, cx + rx, cy);
    PathPdfClosePath(list, context);
}

/*---------------------------------------------------------------------------*/

static void
PathPdfRect(
    Tcl_Obj *list,
    TkPointsContext_ *context,
    double x, double y, double width, double height)
{
    context->current[0] = context->lastMove[0] = x;
    context->current[1] = context->lastMove[1] = y;
    TkPathPdfNumber(list, 3, x, " ");
    TkPathPdfNumber(list, 3, y, " ");
    TkPathPdfNumber(list, 3, width, " ");
    TkPathPdfNumber(list, 3, height, " re\n");
}

/*---------------------------------------------------------------------------*/

Tcl_Obj *
TkPathExtGS(
    Tk_PathStyle *stylePtr,
    long *smaskRef)
{
    Tcl_Obj *obj;
    char smaskBuf[128], cabuf[TCL_DOUBLE_SPACE*2], CAbuf[TCL_DOUBLE_SPACE*2];
    double ca = 1.0, CA = 1.0;

    if (smaskRef != NULL) {
        sprintf(smaskBuf, "\n/AIS false\n/SMask %ld 0 R", *smaskRef);
    } else {
        ca = stylePtr->fillOpacity;
        if (ca < 0.0) {
            ca = 0.0;
        } else if (ca > 1.0) {
            ca = 1.0;
        }
        CA = stylePtr->strokeOpacity;
        if (CA < 0.0) {
            CA = 0.0;
        } else if (CA > 1.0) {
            CA = 1.0;
        }
    }
    if ((ca >= 1.0) && (CA >= 1.0) && (smaskBuf[0] == '\0')) {
        return NULL;
    }
    PrintNumber(CAbuf, 3, CA);
    PrintNumber(cabuf, 3, ca);
    obj = Tcl_NewObj();
    Tcl_AppendPrintfToObj(obj, "<<\n/Type /ExtGState\n"
              "/BM /Normal\n/CA %s\n/ca %s%s\n>>",
              CAbuf, cabuf, (smaskRef != NULL) ? smaskBuf : "");
    return obj;
}

/*---------------------------------------------------------------------------*/

static int
PathPdfGradFuncType2(
    Tcl_Interp *interp,
    Tcl_Obj *mkobj,
    int isAlpha,
    TkGradientStop *stop0,
    TkGradientStop *stop1)
{
    Tcl_Obj *cmd, *obj = Tcl_NewObj();

    if (isAlpha) {
        Tcl_AppendToObj(obj, "<<\n/Domain [0 1]\n"
                "/FunctionType 2\n/N 1\n/C0 [", -1);
        TkPathPdfNumber(obj, 3, stop0->opacity, "]\n/C1 [");
        TkPathPdfNumber(obj, 3, stop1->opacity, "]\n>>");
        } else {
        Tcl_AppendToObj(obj, "<<\n/Domain [0 1]\n"
                "/FunctionType 2\n/N 1\n/C0 [", -1);
        TkPathPdfNumber(obj, 3, (stop0->color->red >> 8) / 255.0, " ");
        TkPathPdfNumber(obj, 3, (stop0->color->green >> 8) / 255.0, " ");
        TkPathPdfNumber(obj, 3, (stop0->color->blue >> 8) / 255.0,
                "]\n/C1 [");
        TkPathPdfNumber(obj, 3, (stop1->color->red >> 8) / 255.0, " ");
        TkPathPdfNumber(obj, 3, (stop1->color->green >> 8) / 255.0, " ");
        TkPathPdfNumber(obj, 3, (stop1->color->blue >> 8) / 255.0,
                "]\n>>");
    }
    cmd = Tcl_DuplicateObj(mkobj);
    Tcl_IncrRefCount(cmd);
    if (Tcl_ListObjAppendElement(interp, cmd, obj) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        Tcl_DecrRefCount(obj);
        return TCL_ERROR;
    }
    if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_DIRECT) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(cmd);
    return TCL_OK;
}

/*---------------------------------------------------------------------------*/

static int
PathPdfGradSoftMask(
    Tcl_Interp *interp,
    Tcl_Obj *mkobj,
    TkPathRect *bbox,
    const char *gradName,
    long gradId,
    TkPathMatrix *tmPtr)
{
    Tcl_Obj *cmd, *obj = Tcl_NewObj();
    long id;
    char fillBbox[128];
    TkPathRect r;

    /* form XObject with softmask */
    sprintf(fillBbox, "/%s sh", gradName);
    obj = Tcl_NewObj();
    r = *bbox;
    if (tmPtr != NULL) {
        r.x2 -= r.x1;
        r.x1 = 0;
        r.y2 -= r.y1;
        r.y1 = 0;
    }
    Tcl_AppendToObj(obj, "<<\n/Type /XObject\n/Subtype /Form\n"
            "/BBox [", -1);
    TkPathPdfNumber(obj, 3, r.x1, " ");
    TkPathPdfNumber(obj, 3, r.y1, " ");
    TkPathPdfNumber(obj, 3, r.x2, " ");
    TkPathPdfNumber(obj, 3, r.y2, "]\n");
    Tcl_AppendPrintfToObj(obj, "/Length %d\n"
              "/Group << /S /Transparency /CS /DeviceGray "
              "/I true /K false >>\n/Resources <<\n"
              "/Shading << /%s %ld 0 R >>\n"
              ">>\n>>\nstream\n", (int) strlen(fillBbox),
              gradName, gradId);
    Tcl_AppendPrintfToObj(obj, "%s\nendstream", fillBbox);
    cmd = Tcl_DuplicateObj(mkobj);
    Tcl_IncrRefCount(cmd);
    if (Tcl_ListObjAppendElement(interp, cmd, obj) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        Tcl_DecrRefCount(obj);
        return TCL_ERROR;
    }
    if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_DIRECT) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        return TCL_ERROR;
    }
    Tcl_GetLongFromObj(NULL, Tcl_GetObjResult(interp), &id);
    /* softmask for ExtGS */
    obj = Tcl_NewObj();
    Tcl_AppendPrintfToObj(obj, "<<\n/Type /Mask\n"
              "/S /Luminosity\n/G %ld 0 R\n>>", id);
    cmd = Tcl_DuplicateObj(mkobj);
    Tcl_IncrRefCount(cmd);
    if (Tcl_ListObjAppendElement(interp, cmd, obj) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        Tcl_DecrRefCount(obj);
        return TCL_ERROR;
    }
    if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_DIRECT) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(cmd);
    return TCL_OK;
}

/*---------------------------------------------------------------------------*/

static int
PathPdfGradient(
    Tcl_Interp *interp,
    int isAlpha,
    Tcl_Obj *mkobj,
    Tcl_Obj *mkgrad,
    TkPathRect *bboxPtr,
    TkPathGradientMaster *gradientPtr,
    char **gradName,
    long *gradId,
    TkPathMatrix *tmPtr)
{
    int retc, code;
    Tcl_Obj **retv;

    if (tmPtr != NULL) {
    tmPtr->a = tmPtr->d = 1.0;
    tmPtr->b = tmPtr->c = 0.0;
    tmPtr->tx = tmPtr->ty = 0.0;
    }
    *gradName = NULL;
    /* prepare shading/pattern/function for gradient */
    if (!TkPathObjectIsEmpty(gradientPtr->stopsObj)) {
        if (gradientPtr->type == TK_PATH_GRADIENTTYPE_LINEAR) {
            code = PathPdfLinearGradient(interp, isAlpha, mkobj, mkgrad,
                         bboxPtr, &gradientPtr->linearFill,
                         gradientPtr->matrixPtr);
        } else {
            code = PathPdfRadialGradient(interp, isAlpha, mkobj, mkgrad,
                         bboxPtr, &gradientPtr->radialFill,
                         gradientPtr->matrixPtr, tmPtr);
        }
        if (code != TCL_OK) {
            return (code == TCL_BREAK) ? TCL_OK : TCL_ERROR;
        }

        /*
         * Get name of shading/pattern/function for gradient
         */
        if (Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp),
                       &retc, &retv) != TCL_OK) {
            return TCL_ERROR;
        }
        if (retc < 2) {
            Tcl_SetResult(interp, (char *) "missing PDF id/name",
                  TCL_STATIC);
            return TCL_ERROR;
        }
        if (gradId != NULL) {
            *gradId = 0;
            Tcl_GetLongFromObj(NULL, retv[0], gradId);
        }
        *gradName = Tcl_GetString(retv[1]);
    }
    return TCL_OK;
}

/*---------------------------------------------------------------------------*/

static int
PathPdfLinearGradient(
    Tcl_Interp *interp,
    int isAlpha,
    Tcl_Obj *mkobj,
    Tcl_Obj *mkgrad,
    TkPathRect *bbox,
    TkLinearGradientFill *fillPtr,
    TkPathMatrix *mPtr)
{
    TkPathRect *tPtr = fillPtr->transitionPtr;
    double x1, y1, x2, y2;
    TkGradientStop *stop0, *stop1;
    long id;
    Tcl_Obj *obj, *cmd;

    if (isAlpha) {
        int i;

        for (i = 0; i < fillPtr->stopArrPtr->nstops; i++) {
            if (fillPtr->stopArrPtr->stops[i]->opacity < 1.0) {
            break;
            }
        }
        if (i >= fillPtr->stopArrPtr->nstops) {
            /* nothing to do */
            return TCL_BREAK;
        }
    }

    /*
     * We need to do like this since this is how SVG defines gradient drawing
     * in case the transition vector is in relative coordinates.
     */
    if (fillPtr->units == TK_PATH_GRADIENTUNITS_BoundingBox) {
        double x = bbox->x1;
        double y = bbox->y1;
        double width = bbox->x2 - bbox->x1;
        double height = bbox->y2 - bbox->y1;

        x1 = x + tPtr->x1 * width;
        y1 = y + tPtr->y1 * height;
        x2 = x + tPtr->x2 * width;
        y2 = y + tPtr->y2 * height;
    } else {
        x1 = tPtr->x1;
        y1 = tPtr->y1;
        x2 = tPtr->x2;
        y2 = tPtr->y2;
    }
    if (fillPtr->stopArrPtr->nstops < 2) {
        Tcl_SetResult(interp, (char *) "need two or more stops "
              "for linear gradient", TCL_STATIC);
        return TCL_ERROR;
    }
    if (fillPtr->stopArrPtr->nstops == 2) {
    stop0 = fillPtr->stopArrPtr->stops[0];
    stop1 = fillPtr->stopArrPtr->stops[1];
    if (PathPdfGradFuncType2(interp, mkobj, isAlpha, stop0, stop1)
        != TCL_OK) {
        return TCL_ERROR;
    }
        Tcl_GetLongFromObj(NULL, Tcl_GetObjResult(interp), &id);
    } else {
        int i;
        Tcl_DString stitchF, enc, bounds;
        char buffer[64];

        Tcl_DStringInit(&stitchF);
        Tcl_DStringInit(&enc);
        Tcl_DStringInit(&bounds);
        for (i = 1; i < fillPtr->stopArrPtr->nstops; i++) {
            stop0 = fillPtr->stopArrPtr->stops[i - 1];
            stop1 = fillPtr->stopArrPtr->stops[i];
            if (PathPdfGradFuncType2(interp, mkobj, isAlpha, stop0, stop1)
            != TCL_OK) {
            Tcl_DStringFree(&stitchF);
            Tcl_DStringFree(&enc);
            Tcl_DStringFree(&bounds);
            return TCL_ERROR;
            }
            Tcl_GetLongFromObj(NULL, Tcl_GetObjResult(interp), &id);
            sprintf(buffer, "%s%ld 0 R", (i > 1) ? " " : "", id);
            Tcl_DStringAppend(&stitchF, buffer, -1);
            sprintf(buffer, "%s0 1", (i > 1) ? " " : "");
            Tcl_DStringAppend(&enc, buffer, -1);
            if (i > 1) {
            Tcl_DStringAppend(&bounds, " ", 1);
            }
            PrintNumber(buffer, 3, stop1->offset);
            Tcl_DStringAppend(&bounds, buffer, -1);
        }
        stop1 = fillPtr->stopArrPtr->stops[fillPtr->stopArrPtr->nstops - 1];
        if (PathPdfGradFuncType2(interp, mkobj, isAlpha, stop1, stop1)
            != TCL_OK) {
            Tcl_DStringFree(&stitchF);
            Tcl_DStringFree(&enc);
            Tcl_DStringFree(&bounds);
            return TCL_ERROR;
        }
        Tcl_GetLongFromObj(NULL, Tcl_GetObjResult(interp), &id);
        sprintf(buffer, "%s%ld 0 R", (i > 1) ? " " : "", id);
        Tcl_DStringAppend(&stitchF, buffer, -1);
        sprintf(buffer, "%s0 1", (i > 1) ? " " : "");
        Tcl_DStringAppend(&enc, buffer, -1);
        obj = Tcl_NewObj();
        Tcl_AppendPrintfToObj(obj, "<<\n/Domain [0 1]\n"
                      "/FunctionType 3\n/Bounds [%s]\n"
                      "/Functions [%s]\n/Encode [%s]\n>>",
                      Tcl_DStringValue(&bounds),
                      Tcl_DStringValue(&stitchF),
                      Tcl_DStringValue(&enc));
        Tcl_DStringFree(&stitchF);
        Tcl_DStringFree(&enc);
        Tcl_DStringFree(&bounds);
        cmd = Tcl_DuplicateObj(mkobj);
        Tcl_IncrRefCount(cmd);
        if (Tcl_ListObjAppendElement(interp, cmd, obj) != TCL_OK) {
            Tcl_DecrRefCount(cmd);
            Tcl_DecrRefCount(obj);
            return TCL_ERROR;
        }
        if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_DIRECT) != TCL_OK) {
            Tcl_DecrRefCount(cmd);
            return TCL_ERROR;
        }
        Tcl_DecrRefCount(cmd);
        Tcl_GetLongFromObj(NULL, Tcl_GetObjResult(interp), &id);
    }
    obj = Tcl_NewObj();
    Tcl_AppendToObj(obj, "<<\n/ShadingType 2\n/Extend [true true]\n"
            "/Coords [", -1);
    TkPathPdfNumber(obj, 3, x1, " ");
    TkPathPdfNumber(obj, 3, y1, " ");
    TkPathPdfNumber(obj, 3, x2, " ");
    TkPathPdfNumber(obj, 3, y2, "]\n");
    Tcl_AppendPrintfToObj(obj, "/ColorSpace %s\n/Function %ld 0 R\n>>",
               isAlpha ? "/DeviceGray" : "/DeviceRGB", id);
    cmd = Tcl_DuplicateObj(mkobj);
    Tcl_IncrRefCount(cmd);
    if (Tcl_ListObjAppendElement(interp, cmd, obj) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        Tcl_DecrRefCount(obj);
        return TCL_ERROR;
    }
    if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_DIRECT) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(cmd);
    Tcl_GetLongFromObj(NULL, Tcl_GetObjResult(interp), &id);
    cmd = Tcl_DuplicateObj(mkgrad);
    Tcl_IncrRefCount(cmd);
    if (Tcl_ListObjAppendElement(interp, cmd, Tcl_NewLongObj(id))
            != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        Tcl_DecrRefCount(obj);
        return TCL_ERROR;
    }
    if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_DIRECT) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(cmd);
    return TCL_OK;
}

/*---------------------------------------------------------------------------*/

static int
PathPdfRadialGradient(
    Tcl_Interp *interp,
    int isAlpha,
    Tcl_Obj *mkobj,
    Tcl_Obj *mkgrad,
    TkPathRect *bbox,
    TkRadialGradientFill *fillPtr,
    TkPathMatrix *mPtr,
    TkPathMatrix *tmPtr)
{
    TkRadialTransition *tPtr = fillPtr->radialPtr;
    double centerX, centerY;
    double radiusX/*, radiusY*/;
    double focalX, focalY;
    double width, height;
    TkGradientStop *stop0, *stop1;
    long id;
    Tcl_Obj *obj, *cmd;

    if (isAlpha) {
        int i;

        for (i = 0; i < fillPtr->stopArrPtr->nstops; i++) {
            if (fillPtr->stopArrPtr->stops[i]->opacity < 1.0) {
            break;
            }
        }
        if (i >= fillPtr->stopArrPtr->nstops) {
            /* nothing to do */
            return TCL_BREAK;
        }
    }
    width = bbox->x2 - bbox->x1;
    height = bbox->y2 - bbox->y1;

    /*
     * We need to do like this since this is how SVG defines gradient drawing
     * in case the transition vector is in relative coordinates.
     */
    if (fillPtr->units == TK_PATH_GRADIENTUNITS_BoundingBox) {
        centerX = width * tPtr->centerX;
        centerY = height * tPtr->centerY;
        radiusX = width * tPtr->radius;
        /*radiusY = height * tPtr->radius;*/
        focalX = width * tPtr->focalX;
        focalY = height * tPtr->focalY;
        if (tmPtr == NULL) {
            centerX += bbox->x1;
            centerY += bbox->y1;
            focalX += bbox->x1;
            focalY += bbox->y1;
        }
    } else {
        centerX = tPtr->centerX;
        centerY = tPtr->centerY;
        radiusX = tPtr->radius;
        /*radiusY = tPtr->radius;*/
        focalX = tPtr->focalX;
        focalY = tPtr->focalY;
    }
    if (tmPtr != NULL) {
        tmPtr->tx = bbox->x1;
        tmPtr->ty = bbox->y1;
        tmPtr->b = tmPtr->c = 0.0;
        if (width > height) {
            tmPtr->a = 1.0;
            tmPtr->d = height / width;
            centerY /= tmPtr->d;
            focalY /= tmPtr->d;
        } else {
            tmPtr->a = width / height;
            tmPtr->d = 1.0;
            centerX /= tmPtr->a;
            focalX /= tmPtr->a;
        }
    }
    if (fillPtr->stopArrPtr->nstops < 2) {
        Tcl_SetResult(interp, (char *) "need two or more stops "
              "for radial gradient", TCL_STATIC);
        return TCL_ERROR;
    }
    if (fillPtr->stopArrPtr->nstops == 2) {
        stop0 = fillPtr->stopArrPtr->stops[0];
        stop1 = fillPtr->stopArrPtr->stops[fillPtr->stopArrPtr->nstops - 1];
        if (PathPdfGradFuncType2(interp, mkobj, isAlpha, stop0, stop1)
                != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_GetLongFromObj(NULL, Tcl_GetObjResult(interp), &id);
    } else {
        int i;
        Tcl_DString stitchF, enc, bounds;
        char buffer[64];

        Tcl_DStringInit(&stitchF);
        Tcl_DStringInit(&enc);
        Tcl_DStringInit(&bounds);
        for (i = 1; i < fillPtr->stopArrPtr->nstops; i++) {
            stop0 = fillPtr->stopArrPtr->stops[i - 1];
            stop1 = fillPtr->stopArrPtr->stops[i];
            if (PathPdfGradFuncType2(interp, mkobj, isAlpha, stop0, stop1)
            != TCL_OK) {
            Tcl_DStringFree(&stitchF);
            Tcl_DStringFree(&enc);
            Tcl_DStringFree(&bounds);
            return TCL_ERROR;
            }
            Tcl_GetLongFromObj(NULL, Tcl_GetObjResult(interp), &id);
            sprintf(buffer, "%s%ld 0 R", (i > 1) ? " " : "", id);
            Tcl_DStringAppend(&stitchF, buffer, -1);
            sprintf(buffer, "%s0 1", (i > 1) ? " " : "");
            Tcl_DStringAppend(&enc, buffer, -1);
            if (i > 1) {
            Tcl_DStringAppend(&bounds, " ", 1);
            }
            PrintNumber(buffer, 3, stop1->offset);
            Tcl_DStringAppend(&bounds, buffer, -1);
        }
        stop1 = fillPtr->stopArrPtr->stops[fillPtr->stopArrPtr->nstops - 1];
        if (PathPdfGradFuncType2(interp, mkobj, isAlpha, stop1, stop1)
            != TCL_OK) {
            Tcl_DStringFree(&stitchF);
            Tcl_DStringFree(&enc);
            Tcl_DStringFree(&bounds);
            return TCL_ERROR;
        }
        Tcl_GetLongFromObj(NULL, Tcl_GetObjResult(interp), &id);
        sprintf(buffer, "%s%ld 0 R", (i > 1) ? " " : "", id);
        Tcl_DStringAppend(&stitchF, buffer, -1);
        sprintf(buffer, "%s0 1", (i > 1) ? " " : "");
        Tcl_DStringAppend(&enc, buffer, -1);
        obj = Tcl_NewObj();
        Tcl_AppendPrintfToObj(obj, "<<\n/Domain [0 1]\n"
                      "/FunctionType 3\n/Bounds [%s]\n"
                      "/Functions [%s]\n/Encode [%s]\n>>",
                      Tcl_DStringValue(&bounds),
                      Tcl_DStringValue(&stitchF),
                      Tcl_DStringValue(&enc));
        Tcl_DStringFree(&stitchF);
        Tcl_DStringFree(&enc);
        Tcl_DStringFree(&bounds);
        cmd = Tcl_DuplicateObj(mkobj);
        Tcl_IncrRefCount(cmd);
        if (Tcl_ListObjAppendElement(interp, cmd, obj) != TCL_OK) {
            Tcl_DecrRefCount(cmd);
            Tcl_DecrRefCount(obj);
            return TCL_ERROR;
        }
        if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_DIRECT) != TCL_OK) {
            Tcl_DecrRefCount(cmd);
            return TCL_ERROR;
        }
        Tcl_DecrRefCount(cmd);
        Tcl_GetLongFromObj(NULL, Tcl_GetObjResult(interp), &id);
    }
    obj = Tcl_NewObj();
    Tcl_AppendToObj(obj, "<<\n/ShadingType 3\n/Extend [true true]\n"
            "/Coords [", -1);
    TkPathPdfNumber(obj, 3, focalX, " ");
    TkPathPdfNumber(obj, 3, focalY, " 0 ");
    TkPathPdfNumber(obj, 3, centerX, " ");
    TkPathPdfNumber(obj, 3, centerY, " ");
    TkPathPdfNumber(obj, 3, radiusX, "]\n");
    Tcl_AppendPrintfToObj(obj, "/ColorSpace %s\n/Function %ld 0 R\n>>",
              isAlpha ? "/DeviceGray" : "/DeviceRGB", id);
    cmd = Tcl_DuplicateObj(mkobj);
    Tcl_IncrRefCount(cmd);
    if (Tcl_ListObjAppendElement(interp, cmd, obj) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        Tcl_DecrRefCount(obj);
        return TCL_ERROR;
    }
    if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_DIRECT) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(cmd);
    Tcl_GetLongFromObj(NULL, Tcl_GetObjResult(interp), &id);
    cmd = Tcl_DuplicateObj(mkgrad);
    Tcl_IncrRefCount(cmd);
    if (Tcl_ListObjAppendElement(interp, cmd, Tcl_NewLongObj(id))
            != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        Tcl_DecrRefCount(obj);
        return TCL_ERROR;
    }
    if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_DIRECT) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(cmd);
    return TCL_OK;
}
/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
