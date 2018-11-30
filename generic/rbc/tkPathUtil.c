/*
 * tkPathUtil.h --
 *
 *    This file contains support functions for tkpath.
 *
 * Copyright (c) 2005-2008  Mats Bengtsson
 *
 */

//#include <float.h>
#include "tkPathInt.h"

/*
 * For wider strokes we must make a more detailed analysis
 * when doing hit tests and area tests.
 */
#define kPathStrokeThicknessLimit     4.0

#define MAX_NUM_STATIC_SEGMENTS  2000

typedef struct CentralArcPars {
    double cx;
    double cy;
    double rx;
    double ry;
    double theta1;
    double dtheta;
    double phi;
} CentralArcPars;

static void        MakeSubPathSegments(TkPathAtom **atomPtrPtr, double *polyPtr,
                        int *numPointsPtr, int *numStrokesPtr, TkPathMatrix *matrixPtr);
static int        SubPathToArea(Tk_PathStyle *stylePtr, double *polyPtr, int numPoints,
                        int    numStrokes,    double *rectPtr, int inside);
static void   PathApplyTMatrix(TkPathMatrix *m, double *x, double *y);
static void   PathApplyTMatrixToPoint(TkPathMatrix *m, double in[2],
            double out[2]);
static void   PathInverseTMatrix(TkPathMatrix *m, TkPathMatrix *mi);
static int    PathPolyLineToArea(double *polyPtr, int numPoints,
                double *rectPtr);
static double    PathThickPolygonToPoint(int joinStyle, int capStyle,
                double width, int isclosed, double *polyPtr,
                int numPoints, double *pointPtr);
static double    PathPolygonToPointEx(double *polyPtr, int numPoints,
                double *pointPtr, int *intersectionsPtr,
                int *nonzerorulePtr);
static int    IsPathRectEmpty(TkPathRect *r);


/*
 *--------------------------------------------------------------
 *
 * TkPathMakePrectAtoms --
 *
 *    Makes the path atoms for a rounded rectangle, prect.
 *
 * Results:
 *    None. Path atoms in atomPtrPtr.
 *
 * Side effects:
 *    Path atom memory allocated.
 *
 *--------------------------------------------------------------
 */

void
TkPathMakePrectAtoms(double *pointsPtr, double rx, double ry, TkPathAtom **atomPtrPtr)
{
    TkPathAtom *atomPtr = NULL;
    TkPathAtom *firstAtomPtr = NULL;
    int round = 1;
    double epsilon = 1e-6;
    double x = MIN(pointsPtr[0], pointsPtr[2]);
    double y = MIN(pointsPtr[1], pointsPtr[3]);
    double width = fabs(pointsPtr[0] - pointsPtr[2]);
    double height = fabs(pointsPtr[1] - pointsPtr[3]);

    /* If only one of rx or ry is zero this implies that both shall be nonzero. */
    if (rx < epsilon && ry < epsilon) {
        round = 0;
    } else if (rx < epsilon) {
        rx = ry;
    } else if (ry < epsilon) {
        ry = rx;
    }
    if (round) {

        /* There are certain constraints on rx and ry. */
        rx = MIN(rx, width/2.0);
        ry = MIN(ry, height/2.0);

        atomPtr = TkPathNewMoveToAtom(x+rx, y);
        firstAtomPtr = atomPtr;
        atomPtr->nextPtr = TkPathNewLineToAtom(x+width-rx, y);
        atomPtr = atomPtr->nextPtr;
        atomPtr->nextPtr = TkPathNewArcAtom(rx, ry, 0.0, 0, 1, x+width, y+ry);
        atomPtr = atomPtr->nextPtr;
        atomPtr->nextPtr = TkPathNewLineToAtom(x+width, y+height-ry);
        atomPtr = atomPtr->nextPtr;
        atomPtr->nextPtr = TkPathNewArcAtom(rx, ry, 0.0, 0, 1, x+width-rx, y+height);
        atomPtr = atomPtr->nextPtr;
        atomPtr->nextPtr = TkPathNewLineToAtom(x+rx, y+height);
        atomPtr = atomPtr->nextPtr;
        atomPtr->nextPtr = TkPathNewArcAtom(rx, ry, 0.0, 0, 1, x, y+height-ry);
        atomPtr = atomPtr->nextPtr;
        atomPtr->nextPtr = TkPathNewLineToAtom(x, y+ry);
        atomPtr = atomPtr->nextPtr;
        atomPtr->nextPtr = TkPathNewArcAtom(rx, ry, 0.0, 0, 1, x+rx, y);
        atomPtr = atomPtr->nextPtr;
        atomPtr->nextPtr = TkPathNewCloseAtom(x, y);
        *atomPtrPtr = firstAtomPtr;
    } else {
        atomPtr = TkPathNewRectAtom(pointsPtr);
        *atomPtrPtr = atomPtr;
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkPathDrawPath --
 *
 *    This procedure is invoked to draw a line item in a given
 *    drawable.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    ItemPtr is drawn in drawable using the transformation
 *    information in canvas.
 *
 *--------------------------------------------------------------
 */

void
TkPathDrawPath(
    TkPathContext context,  /* Context. */
    TkPathAtom *atomPtr,      /* The actual path as a linked list
                             * of TkPathAtoms. */
    Tk_PathStyle *stylePtr, /* The paths style. */
    TkPathMatrix *mPtr,          /* Typically used for canvas offsets. */
    TkPathRect *bboxPtr)      /* The bare (untransformed) bounding box
                             * (assuming zero stroke width) */
{
    /*
     * Define the path in the drawable using the path drawing functions.
     * Any transform matrix need to be considered and canvas drawable
     * offset must always be taken into account. Note the order!
     */

    if (mPtr != NULL) {
        TkPathPushTMatrix(context, mPtr);
    }
    if (stylePtr->matrixPtr != NULL) {
        TkPathPushTMatrix(context, stylePtr->matrixPtr);
    }
    if (TkPathMakePath(context, atomPtr, stylePtr) != TCL_OK) {
        return;
    }
    TkPathPaintPath(context, atomPtr, stylePtr,    bboxPtr);
}

/*
 *--------------------------------------------------------------
 *
 * TkPathPaintPath --
 *
 *    This procedure is invoked to paint a path in a given context.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Any path defined in the context is painted.
 *
 *--------------------------------------------------------------
 */

void
TkPathPaintPath(
    TkPathContext context,
    TkPathAtom *atomPtr,      /* The actual path as a linked list
                             * of TkPathAtoms. */
    Tk_PathStyle *stylePtr, /* The paths style. */
    TkPathRect *bboxPtr)
{
    TkPathGradientMaster *gradientPtr = GetGradientMasterFromPathColor(stylePtr->fill);
    if (gradientPtr != NULL) {
        TkPathClipToPath(context, stylePtr->fillRule);
        TkPathGradientPaint(context, bboxPtr, gradientPtr, stylePtr->fillRule, stylePtr->fillOpacity);

        /* NB: Both CoreGraphics on MacOSX and Win32 GDI (and cairo from 1.0)
         *     clear the current path when setting clipping. Need therefore
         *     to redo the path.
         */
        if (TkPathDrawingDestroysPath()) {
            TkPathMakePath(context, atomPtr, stylePtr);
        }

        /* We shall remove the path clipping here! */
        TkPathReleaseClipToPath(context);
    }

    if ((stylePtr->fill != NULL) && (stylePtr->fill->color != NULL) && (stylePtr->strokeColor != NULL)) {
        TkPathFillAndStroke(context, stylePtr);
    } else if ((stylePtr->fill != NULL) && (stylePtr->fill->color != NULL)) {
        TkPathFill(context, stylePtr);
    } else if (stylePtr->strokeColor != NULL) {
        TkPathStroke(context, stylePtr);
    }
}

TkPathRect
TkPathGetTotalBbox(TkPathAtom *atomPtr, Tk_PathStyle *stylePtr)
{
    TkPathRect bare, total;

    bare = TkPathGetGenericBarePathBbox(atomPtr);
    total = TkPathGetGenericPathTotalBboxFromBare(atomPtr, stylePtr, &bare);
    return total;
}

/* Return NULL on error and leave error message */

/*
 * @@@ OBSOLETE SOON!!!
 * As a temporary mean before trashing it we ignore gradients.
 */

TkPathColor *
TkPathNewPathColor(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *nameObj)
{
    char *name;
    TkPathColor *colorPtr;
    XColor *color = NULL;

    name = Tcl_GetStringFromObj(nameObj, NULL);
    colorPtr = (TkPathColor *) ckalloc(sizeof(TkPathColor));
    colorPtr->color = NULL;
    colorPtr->gradientInstPtr = NULL;

    color = Tk_AllocColorFromObj(interp, tkwin, nameObj);
    if (color == NULL) {
        char tmp[256];
        ckfree((char *) colorPtr);
        sprintf(tmp, "unrecognized color or gradient name \"%s\"", name);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(tmp, -1));
        return NULL;
    }
    colorPtr->color = color;
    return colorPtr;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathGetPathColor --
 *
 *    Parses a string in nameObj to either a valid XColor or
 *      looks up a gradient name for the hash table tablePtr.
 *      Makes a new TkPathColor struct from a string value.
 *      Like Tk_GetImage() but for TkPathColor instead of Tk_Image.
 *
 * Results:
 *    Pointer to a TkPathColor struct or returns NULL on error
 *      and leaves an error message.
 *
 * Side effects:
 *    TkPathColor malloced if OK.
 *
 *--------------------------------------------------------------
 */

TkPathColor *
TkPathGetPathColor(Tcl_Interp *interp, Tk_Window tkwin,
    Tcl_Obj *nameObj, Tcl_HashTable *tablePtr,
    TkPathGradientChangedProc *changeProc, ClientData clientData)
{
    char *name;
    TkPathColor *colorPtr;
    XColor *color = NULL;
    TkPathGradientInst *gradientInstPtr;

    name = Tcl_GetString(nameObj);
    colorPtr = (TkPathColor *) ckalloc(sizeof(TkPathColor));

    /*
     * Only one of them can be non NULL.
     */
    colorPtr->color = NULL;
    colorPtr->gradientInstPtr = NULL;

    gradientInstPtr = TkPathGetGradient(interp, name, tablePtr, changeProc, clientData);
    if (gradientInstPtr != NULL) {
        colorPtr->gradientInstPtr = gradientInstPtr;
    } else {
        Tcl_ResetResult(interp);
        color = Tk_AllocColorFromObj(interp, tkwin, nameObj);
        if (color == NULL) {
            Tcl_Obj *resultObj;
            ckfree((char *) colorPtr);
            resultObj = Tcl_NewStringObj("unrecognized color or gradient name \"", -1);
            Tcl_AppendStringsToObj(resultObj, name, "\"", (char *) NULL);
            Tcl_SetObjResult(interp, resultObj);
            return NULL;
        }
        colorPtr->color = color;
    }
    return colorPtr;
}

void
TkPathFreePathColor(TkPathColor *colorPtr)
{
    if (colorPtr != NULL) {
        if (colorPtr->color != NULL) {
            Tk_FreeColor(colorPtr->color);
        } else if (colorPtr->gradientInstPtr != NULL) {
            TkPathFreeGradient(colorPtr->gradientInstPtr);
        }
        ckfree((char *) colorPtr);
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkPathCopyBitsARGB, TkPathCopyBitsBGRA --
 *
 *    Copies bitmap data from these formats to RGBA.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

void
TkPathCopyBitsARGB(unsigned char *from, unsigned char *to,
        int width, int height, int bytesPerRow)
{
    unsigned char *src, *dst;
    int i, j;

    /* Copy XRGB to RGBX in one shot, alphas in a loop. */
    memcpy(to, from+1, height*bytesPerRow-1);

    for (i = 0; i < height; i++) {
        src = from + i*bytesPerRow;
        dst = to + i*bytesPerRow;
        /* @@@ Keep ARGB format in photo? */
        for (j = 0; j < width; j++, src += 4, dst += 4) {
            *(dst+3) = *src;
        }
    }
}

void
TkPathCopyBitsBGRA(unsigned char *from, unsigned char *to,
        int width, int height, int bytesPerRow)
{
    unsigned char *src, *dst;
    int i, j;

    /* Copy BGRA -> RGBA */
    for (i = 0; i < height; i++) {
        src = from + i*bytesPerRow;
        dst = to + i*bytesPerRow;
        for (j = 0; j < width; j++, src += 4) {
            /* RED */
            *dst++ = *(src+2);
            /* GREEN */
            *dst++ = *(src+1);
            /* BLUE */
            *dst++ = *src;
            /* ALPHA */
            *dst++ = *(src+3);
        }
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkPathCopyBitsPremultipliedAlphaRGBA, TkPathCopyBitsPremultipliedAlphaARGB --
 *
 *    Copies bitmap data that have alpha premultiplied into a bitmap
 *    with "true" RGB values need for Tk_Photo. The source format is
 *    either RGBA or ARGB, but destination always RGBA used for photos.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

void
TkPathCopyBitsPremultipliedAlphaRGBA(unsigned char *from, unsigned char *to,
        int width, int height, int bytesPerRow)
{
    unsigned char *src, *dst, alpha;
    int i, j;

    /* Copy src RGBA with premulitplied alpha to "plain" RGBA. */
    for (i = 0; i < height; i++) {
        src = from + i*bytesPerRow;
        dst = to + i*bytesPerRow;
        for (j = 0; j < width; j++) {
            alpha = *(src+3);
            if (alpha == 0xFF || alpha == 0x00) {
                memcpy(dst, src, 4);
                src += 4;
                dst += 4;
            } else {
                /* dst = 255*src/alpha */
                *dst++ = (*src++*255)/alpha;
                *dst++ = (*src++*255)/alpha;
                *dst++ = (*src++*255)/alpha;
                *dst++ = alpha;
                src++;
            }
        }
    }
}

/* UNTESTED! */
void
TkPathCopyBitsPremultipliedAlphaARGB(unsigned char *from, unsigned char *to,
        int width, int height, int bytesPerRow)
{
    unsigned char *src, *dst, alpha;
    int i, j;

    /* Copy src ARGB with premulitplied alpha to "plain" RGBA. */
    for (i = 0; i < height; i++) {
        src = from + i*bytesPerRow;
        dst = to + i*bytesPerRow;
        for (j = 0; j < width; j++) {
            alpha = *src;
            if (alpha == 0xFF || alpha == 0x00) {
                memcpy(dst, src+1, 3);
                *(dst+3) = alpha;
                src += 4;
                dst += 4;
            } else {
                /* dst = 255*src/alpha */
                *(dst+3) = alpha;
                src++;
                *dst = ((*src << 8) - *src)/alpha;
                dst++, src++;
                *dst = ((*src << 8) - *src)/alpha;
                dst++, src++;
                *dst = ((*src << 8) - *src)/alpha;
                dst++, dst++, src++;
            }
        }
    }
}

void
TkPathCopyBitsPremultipliedAlphaBGRA(unsigned char *from, unsigned char *to,
        int width, int height, int bytesPerRow)
{
    unsigned char *src, *dst, alpha;
    int i, j;

    /* Copy src BGRA with premulitplied alpha to "plain" RGBA. */
    for (i = 0; i < height; i++) {
        src = from + i*bytesPerRow;
        dst = to + i*bytesPerRow;
        for (j = 0; j < width; j++, src += 4) {
            alpha = *(src+3);
            if (alpha == 0xFF || alpha == 0x00) {
                /* RED */
                *dst++ = *(src+2);
                /* GREEN */
                *dst++ = *(src+1);
                /* BLUE */
                *dst++ = *src;
                /* ALPHA */
                *dst++ = *(src+3);
            } else {
                /* dst = 255*src/alpha */
                /* RED */
                *dst++ = (*(src+2)*255)/alpha;
                /* GREEN */
                *dst++ = (*(src+1)*255)/alpha;
                /* BLUE */
                *dst++ = (*(src+0)*255)/alpha;
                /* ALPHA */
                *dst++ = alpha;
            }
        }
    }
}

/* from mozilla */
static double
CalcVectorAngle(double ux, double uy, double vx, double vy)
{
    double ta = atan2(uy, ux);
    double tb = atan2(vy, vx);
    if (tb >= ta) {
        return tb-ta;
    } else {
        return 2.0*M_PI - (ta-tb);
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkPathEndpointToCentralArcParameters
 *
 *    Conversion from endpoint to center parameterization.
 *    All angles in radians!
 *    From: http://www.w3.org/TR/2003/REC-SVG11-20030114
 *
 * Results:
 *    Arc specific return code.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

int
TkPathEndpointToCentralArcParameters(
        double x1, double y1, double x2, double y2,    /* The endpoints. */
        double rx, double ry,                /* Radius. */
        double phi, char largeArcFlag, char sweepFlag,
        double *cxPtr, double *cyPtr,             /* Out. */
        double *rxPtr, double *ryPtr,
        double *theta1Ptr, double *dthetaPtr)
{
    double sinPhi, cosPhi;
    double dx, dy;
    double x1dash, y1dash;
    double cxdash, cydash;
    double cx, cy;
    double numerator, root;
    double theta1, dtheta;

    /* 1. Treat out-of-range parameters as described in
     * http://www.w3.org/TR/SVG/implnote.html#ArcImplementationNotes
     *
     * If the endpoints (x1, y1) and (x2, y2) are identical, then this
     * is equivalent to omitting the elliptical arc segment entirely
     */
    if (fabs(x1-x2)<DBL_EPSILON && fabs(y1-y2) < DBL_EPSILON) {
        return TK_PATH_ARC_Skip;
    }

    /* If rx = 0 or ry = 0 then this arc is treated as a straight line
     * segment (a "lineto") joining the endpoints.
     */
    if (rx == 0.0f || ry == 0.0f) {
        return TK_PATH_ARC_Line;
    }

    /* If rx or ry have negative signs, these are dropped; the absolute
     * value is used instead.
     */
    if (rx < 0.0) rx = -rx;
    if (ry < 0.0) ry = -ry;

    if (largeArcFlag != 0) largeArcFlag = 1;
    if (sweepFlag != 0) sweepFlag = 1;

    /* 2. convert to center parameterization as shown in
     * http://www.w3.org/TR/SVG/implnote.html
     */
    sinPhi = sin(phi);
    cosPhi = cos(phi);
    dx = (x1-x2)/2.0;
    dy = (y1-y2)/2.0;
    x1dash =  cosPhi * dx + sinPhi * dy;
    y1dash = -sinPhi * dx + cosPhi * dy;

    /* Compute cx' and cy'. */
    numerator = rx*rx*ry*ry - rx*rx*y1dash*y1dash - ry*ry*x1dash*x1dash;
    if (numerator < 0.0) {

        /* If rx , ry and are such that there is no solution (basically,
         * the ellipse is not big enough to reach from (x1, y1) to (x2,
         * y2)) then the ellipse is scaled up uniformly until there is
         * exactly one solution (until the ellipse is just big enough).
         *     -> find factor s, such that numerator' with rx'=s*rx and
         *    ry'=s*ry becomes 0 :
         */
        float s = (float) sqrt(1.0 - numerator/(rx*rx*ry*ry));

        rx *= s;
        ry *= s;
        root = 0.0;
    } else {
        root = (largeArcFlag == sweepFlag ? -1.0 : 1.0) *
                sqrt( numerator/(rx*rx*y1dash*y1dash + ry*ry*x1dash*x1dash) );
    }

    cxdash =  root*rx*y1dash/ry;
    cydash = -root*ry*x1dash/rx;

    /* Compute cx and cy from cx' and cy'. */
    cx = cosPhi * cxdash - sinPhi * cydash + (x1+x2)/2.0;
    cy = sinPhi * cxdash + cosPhi * cydash + (y1+y2)/2.0;

    /* Compute start angle and extent. */
    theta1 = CalcVectorAngle(1.0, 0.0, (x1dash-cxdash)/rx, (y1dash-cydash)/ry);
    dtheta = CalcVectorAngle(
            (x1dash-cxdash)/rx,  (y1dash-cydash)/ry,
            (-x1dash-cxdash)/rx, (-y1dash-cydash)/ry);
    if (!sweepFlag && (dtheta > 0.0)) {
        dtheta -= 2.0*M_PI;
    } else if (sweepFlag && (dtheta < 0.0)) {
        dtheta += 2.0*M_PI;
    }
    *cxPtr = cx;
    *cyPtr = cy;
    *rxPtr = rx;
    *ryPtr = ry;
    *theta1Ptr = theta1;
    *dthetaPtr = dtheta;

    return TK_PATH_ARC_OK;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathTableLooup
 *
 *    Look up an index from a statically allocated table of ints.
 *
 * Results:
 *    integer
 *
 * Side effects:
 *    None
 *
 *--------------------------------------------------------------
 */

int
TkPathTableLookup(TkLookupTable *map, int n, int from)
{
    int i = 0;

    while ((i < n) && (from != map[i].from))
        i++;
    if (i == n) {
        return map[0].to;
    } else {
        return map[i].to;
    }
}

/*
 * Miscellaneous matrix utilities.
 */

static void
PathApplyTMatrix(TkPathMatrix *m, double *x, double *y)
{
    if (m != NULL) {
        double tmpx = *x;
        double tmpy = *y;
        *x = tmpx*m->a + tmpy*m->c + m->tx;
        *y = tmpx*m->b + tmpy*m->d + m->ty;
    }
}

static void
PathApplyTMatrixToPoint(TkPathMatrix *m, double in[2], double out[2])
{
    if (m == NULL) {
        out[0] = in[0];
        out[1] = in[1];
    } else {
        out[0] = in[0]*m->a + in[1]*m->c + m->tx;
        out[1] = in[0]*m->b + in[1]*m->d + m->ty;
    }
}

static void
PathInverseTMatrix(TkPathMatrix *m, TkPathMatrix *mi)
{
    double det;

    /* @@@ We need error checking for det = 0 */
    det = m->a * m->d - m->b * m->c;
    mi->a  =  m->d/det;
    mi->b  = -m->b/det;
    mi->c  = -m->c/det;
    mi->d  =  m->a/det;
    mi->tx = (m->c * m->ty - m->d * m->tx)/det;
    mi->ty = (m->b * m->tx - m->a * m->ty)/det;
}

/*
 *----------------------------------------------------------------------
 *
 * TkPathMMulTMatrix --
 *
 *    Multiplies (concatenates) two matrices together and puts the
 *      result in m2.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    TkPathMatrix m2 modified
 *
 *----------------------------------------------------------------------
 */

void
TkPathMMulTMatrix(TkPathMatrix *m1, TkPathMatrix *m2)
{
    if (m1 == NULL) {
        return;
    }
    if (m2 == NULL) {
        /* Panic! */
    } else {
        TkPathMatrix tmp = *m2;
        TkPathMatrix *p = m2;

        p->a  = m1->a*tmp.a  + m1->b*tmp.c;
        p->b  = m1->a*tmp.b  + m1->b*tmp.d;
        p->c  = m1->c*tmp.a  + m1->d*tmp.c;
        p->d  = m1->c*tmp.b  + m1->d*tmp.d;
        p->tx = m1->tx*tmp.a + m1->ty*tmp.c + tmp.tx;
        p->ty = m1->tx*tmp.b + m1->ty*tmp.d + tmp.ty;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkPathGetTMatrix --
 *
 *    Parses a Tcl list (in string) into a TkPathMatrix record.
 *
 * Results:
 *    Standard Tcl result
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */

int
TkPathGetTMatrix(
        Tcl_Interp* interp,
        const char *list,     /* Object containg the lists for the matrix. */
        TkPathMatrix *matrixPtr)    /* Where to store TkPathMatrix corresponding
                                 * to list. Must be allocated! */
{
    const char **argv = NULL;
    int i, argc;
    int result = TCL_OK;
    double tmp[6];

    /* Check matrix consistency. */
    if (Tcl_SplitList(interp, list, &argc, &argv) != TCL_OK) {
        result = TCL_ERROR;
        goto bail;
    }
    if (argc != 6) {
        Tcl_AppendResult(interp, "matrix \"", list, "\" is inconsistent",
                (char *) NULL);
        result = TCL_ERROR;
        goto bail;
    }

    /* Take arguments in turn. */
    for (i = 0; i < 6; i++) {
        if (Tcl_GetDouble(interp, argv[i], &(tmp[i])) != TCL_OK) {
            Tcl_AppendResult(interp, "matrix \"", list, "\" is inconsistent",
                    (char *) NULL);
            result = TCL_ERROR;
            goto bail;
        }
    }

    /* Check that the matrix is not close to being singular. */
    if (fabs(tmp[0]*tmp[3] - tmp[1]*tmp[2]) < 1e-6) {
        Tcl_AppendResult(interp, "matrix \"", list, "\" is close to singular",
                (char *) NULL);
            result = TCL_ERROR;
            goto bail;
    }

    /* Matrix. */
    matrixPtr->a  = tmp[0];
    matrixPtr->b  = tmp[1];
    matrixPtr->c  = tmp[2];
    matrixPtr->d  = tmp[3];
    matrixPtr->tx = tmp[4];
    matrixPtr->ty = tmp[5];

bail:
    if (argv != NULL) {
        Tcl_Free((char *) argv);
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TkPathGetTclObjFromTMatrix --
 *
 *    Parses a TkPathMatrix record into a list object.
 *
 * Results:
 *    Standard Tcl result
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */

int
TkPathGetTclObjFromTMatrix(
        Tcl_Interp* interp,
        TkPathMatrix *matrixPtr,
        Tcl_Obj **listObjPtrPtr)
{
    Tcl_Obj        *listObj;

    /* @@@ Error handling remains. */

    listObj = Tcl_NewListObj( 0, (Tcl_Obj **) NULL );
    if (matrixPtr != NULL) {
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(matrixPtr->a));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(matrixPtr->b));

        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(matrixPtr->c));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(matrixPtr->d));

        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(matrixPtr->tx));
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(matrixPtr->ty));
    }
    *listObjPtrPtr = listObj;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ObjectIsEmpty --
 *
 *    This procedure tests whether the string value of an object is
 *    empty.
 *
 * Results:
 *    The return value is 1 if the string value of objPtr has length
 *    zero, and 0 otherwise.
 *
 * Side effects:
 *    May cause object shimmering, since this function can force a
 *    conversion to a string object.
 *
 *----------------------------------------------------------------------
 */

int
TkPathObjectIsEmpty(
        Tcl_Obj *objPtr)    /* Object to test.  May be NULL. */
{
    int length;

    if (objPtr == NULL) {
        return 1;
    }
    if (objPtr->bytes != NULL) {
        return (objPtr->length == 0);
    }
    Tcl_GetStringFromObj(objPtr, &length);
    return (length == 0);
}

/*
 *--------------------------------------------------------------
 *
 * TkPathIncludePoint --
 *
 *    Given a point and a generic canvas item header, expand the item's
 *    bounding box if needed to include the point.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The boudn.
 *
 *--------------------------------------------------------------
 */

    /* ARGSUSED */
void
TkPathIncludePoint(
    Tk_PathItem *itemPtr,    /* Item whose bounding box is being
                 * calculated. */
    double *pointPtr)        /* Address of two doubles giving x and y
                 * coordinates of point. */
{
    int tmp;

    tmp = (int) (pointPtr[0] + 0.5);
    if (tmp < itemPtr->x1) {
    itemPtr->x1 = tmp;
    }
    if (tmp > itemPtr->x2) {
    itemPtr->x2 = tmp;
    }
    tmp = (int) (pointPtr[1] + 0.5);
    if (tmp < itemPtr->y1) {
    itemPtr->y1 = tmp;
    }
    if (tmp > itemPtr->y2) {
    itemPtr->y2 = tmp;
    }
}


/*
 *--------------------------------------------------------------
 *
 * TkPathBezierScreenPoints --
 *
 *    Given four control points, create a larger set of XPoints for a Bezier
 *    curve based on the points.
 *
 * Results:
 *    The array at *xPointPtr gets filled in with numSteps XPoints
 *    corresponding to the Bezier spline defined by the four control points.
 *    Note: no output point is generated for the first input point, but an
 *    output point *is* generated for the last input point.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

void
TkPathBezierScreenPoints(
    Tk_PathCanvas canvas,    /* Canvas in which curve is to be drawn. */
    double control[],        /* Array of coordinates for four control
                 * points: x0, y0, x1, y1, ... x3 y3. */
    int numSteps,        /* Number of curve points to generate. */
    XPoint *xPointPtr)        /* Where to put new points. */
{
    int i;
    double u, u2, u3, t, t2, t3;

    for (i = 1; i <= numSteps; i++, xPointPtr++) {
    t = ((double) i)/((double) numSteps);
    t2 = t*t;
    t3 = t2*t;
    u = 1.0 - t;
    u2 = u*u;
    u3 = u2*u;
    Tk_PathCanvasDrawableCoords(canvas,
        (control[0]*u3 + 3.0 * (control[2]*t*u2 + control[4]*t2*u)
            + control[6]*t3),
        (control[1]*u3 + 3.0 * (control[3]*t*u2 + control[5]*t2*u)
            + control[7]*t3),
        &xPointPtr->x, &xPointPtr->y);
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkPathBezierPoints --
 *
 *    Given four control points, create a larger set of points for a Bezier
 *    curve based on the points.
 *
 * Results:
 *    The array at *coordPtr gets filled in with 2*numSteps coordinates,
 *    which correspond to the Bezier spline defined by the four control
 *    points. Note: no output point is generated for the first input point,
 *    but an output point *is* generated for the last input point.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

void
TkPathBezierPoints(
    double control[],        /* Array of coordinates for four control
                 * points: x0, y0, x1, y1, ... x3 y3. */
    int numSteps,        /* Number of curve points to generate. */
    double *coordPtr)        /* Where to put new points. */
{
    int i;
    double u, u2, u3, t, t2, t3;

    for (i = 1; i <= numSteps; i++, coordPtr += 2) {
    t = ((double) i)/((double) numSteps);
    t2 = t*t;
    t3 = t2*t;
    u = 1.0 - t;
    u2 = u*u;
    u3 = u2*u;
    coordPtr[0] = control[0]*u3
        + 3.0 * (control[2]*t*u2 + control[4]*t2*u) + control[6]*t3;
    coordPtr[1] = control[1]*u3
        + 3.0 * (control[3]*t*u2 + control[5]*t2*u) + control[7]*t3;
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkPathMakeBezierCurve --
 *
 *    Given a set of points, create a new set of points that fit parabolic
 *    splines to the line segments connecting the original points. Produces
 *    output points in either of two forms.
 *
 *    Note: the name of this function should *not* be taken to mean that it
 *    interprets the input points as directly defining Bezier curves.
 *    Rather, it internally computes a Bezier curve representation of each
 *    parabolic spline segment. (These Bezier curves are then flattened to
 *    produce the points filled into the output arrays.)
 *
 * Results:
 *    Either or both of the xPoints or dblPoints arrays are filled in. The
 *    return value is the number of points placed in the arrays. Note: if
 *    the first and last points are the same, then a closed curve is
 *    generated.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

int
TkPathMakeBezierCurve(
    Tk_PathCanvas canvas,    /* Canvas in which curve is to be drawn. */
    double *pointPtr,        /* Array of input coordinates: x0, y0, x1, y1,
                 * etc.. */
    int numPoints,        /* Number of points at pointPtr. */
    int numSteps,        /* Number of steps to use for each spline
                 * segments (determines smoothness of
                 * curve). */
    XPoint xPoints[],        /* Array of XPoints to fill in (e.g. for
                 * display). NULL means don't fill in any
                 * XPoints. */
    double dblPoints[])        /* Array of points to fill in as doubles, in
                 * the form x0, y0, x1, y1, .... NULL means
                 * don't fill in anything in this form. Caller
                 * must make sure that this array has enough
                 * space. */
{
    int closed, outputPoints, i;
    int numCoords = numPoints*2;
    double control[8];

    /*
     * If the curve is a closed one then generate a special spline that spans
     * the last points and the first ones. Otherwise just put the first point
     * into the output.
     */

    if (!pointPtr) {
    /*
     * Of pointPtr == NULL, this function returns an upper limit of the
     * array size to store the coordinates. This can be used to allocate
     * storage, before the actual coordinates are calculated.
     */

    return 1 + numPoints * numSteps;
    }

    outputPoints = 0;
    if ((pointPtr[0] == pointPtr[numCoords-2])
        && (pointPtr[1] == pointPtr[numCoords-1])) {
    closed = 1;
    control[0] = 0.5*pointPtr[numCoords-4] + 0.5*pointPtr[0];
    control[1] = 0.5*pointPtr[numCoords-3] + 0.5*pointPtr[1];
    control[2] = 0.167*pointPtr[numCoords-4] + 0.833*pointPtr[0];
    control[3] = 0.167*pointPtr[numCoords-3] + 0.833*pointPtr[1];
    control[4] = 0.833*pointPtr[0] + 0.167*pointPtr[2];
    control[5] = 0.833*pointPtr[1] + 0.167*pointPtr[3];
    control[6] = 0.5*pointPtr[0] + 0.5*pointPtr[2];
    control[7] = 0.5*pointPtr[1] + 0.5*pointPtr[3];
    if (xPoints != NULL) {
        Tk_PathCanvasDrawableCoords(canvas, control[0], control[1],
            &xPoints->x, &xPoints->y);
        TkPathBezierScreenPoints(canvas, control, numSteps, xPoints+1);
        xPoints += numSteps+1;
    }
    if (dblPoints != NULL) {
        dblPoints[0] = control[0];
        dblPoints[1] = control[1];
        TkPathBezierPoints(control, numSteps, dblPoints+2);
        dblPoints += 2*(numSteps+1);
    }
    outputPoints += numSteps+1;
    } else {
    closed = 0;
    if (xPoints != NULL) {
        Tk_PathCanvasDrawableCoords(canvas, pointPtr[0], pointPtr[1],
            &xPoints->x, &xPoints->y);
        xPoints += 1;
    }
    if (dblPoints != NULL) {
        dblPoints[0] = pointPtr[0];
        dblPoints[1] = pointPtr[1];
        dblPoints += 2;
    }
    outputPoints += 1;
    }

    for (i = 2; i < numPoints; i++, pointPtr += 2) {
    /*
     * Set up the first two control points. This is done differently for
     * the first spline of an open curve than for other cases.
     */

    if ((i == 2) && !closed) {
        control[0] = pointPtr[0];
        control[1] = pointPtr[1];
        control[2] = 0.333*pointPtr[0] + 0.667*pointPtr[2];
        control[3] = 0.333*pointPtr[1] + 0.667*pointPtr[3];
    } else {
        control[0] = 0.5*pointPtr[0] + 0.5*pointPtr[2];
        control[1] = 0.5*pointPtr[1] + 0.5*pointPtr[3];
        control[2] = 0.167*pointPtr[0] + 0.833*pointPtr[2];
        control[3] = 0.167*pointPtr[1] + 0.833*pointPtr[3];
    }

    /*
     * Set up the last two control points. This is done differently for
     * the last spline of an open curve than for other cases.
     */

    if ((i == (numPoints-1)) && !closed) {
        control[4] = .667*pointPtr[2] + .333*pointPtr[4];
        control[5] = .667*pointPtr[3] + .333*pointPtr[5];
        control[6] = pointPtr[4];
        control[7] = pointPtr[5];
    } else {
        control[4] = .833*pointPtr[2] + .167*pointPtr[4];
        control[5] = .833*pointPtr[3] + .167*pointPtr[5];
        control[6] = 0.5*pointPtr[2] + 0.5*pointPtr[4];
        control[7] = 0.5*pointPtr[3] + 0.5*pointPtr[5];
    }

    /*
     * If the first two points coincide, or if the last two points
     * coincide, then generate a single straight-line segment by
     * outputting the last control point.
     */

    if (((pointPtr[0] == pointPtr[2]) && (pointPtr[1] == pointPtr[3]))
        || ((pointPtr[2] == pointPtr[4])
        && (pointPtr[3] == pointPtr[5]))) {
        if (xPoints != NULL) {
        Tk_PathCanvasDrawableCoords(canvas, control[6], control[7],
            &xPoints[0].x, &xPoints[0].y);
        xPoints++;
        }
        if (dblPoints != NULL) {
        dblPoints[0] = control[6];
        dblPoints[1] = control[7];
        dblPoints += 2;
        }
        outputPoints += 1;
        continue;
    }

    /*
     * Generate a Bezier spline using the control points.
     */


    if (xPoints != NULL) {
        TkPathBezierScreenPoints(canvas, control, numSteps, xPoints);
        xPoints += numSteps;
    }
    if (dblPoints != NULL) {
        TkPathBezierPoints(control, numSteps, dblPoints);
        dblPoints += 2*numSteps;
    }
    outputPoints += numSteps;
    }
    return outputPoints;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathMakeRawCurve --
 *
 *    Interpret the given set of points as the raw knots and control points
 *    defining a sequence of cubic Bezier curves. Create a new set of points
 *    that fit these Bezier curves. Output points are produced in either of
 *    two forms.
 *
 * Results:
 *    Either or both of the xPoints or dblPoints arrays are filled in. The
 *    return value is the number of points placed in the arrays.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

int
TkPathMakeRawCurve(
    Tk_PathCanvas canvas,    /* Canvas in which curve is to be drawn. */
    double *pointPtr,        /* Array of input coordinates: x0, y0, x1, y1,
                 * etc.. */
    int numPoints,        /* Number of points at pointPtr. */
    int numSteps,        /* Number of steps to use for each curve
                 * segment (determines smoothness of
                 * curve). */
    XPoint xPoints[],        /* Array of XPoints to fill in (e.g. for
                 * display). NULL means don't fill in any
                 * XPoints. */
    double dblPoints[])        /* Array of points to fill in as doubles, in
                 * the form x0, y0, x1, y1, .... NULL means
                 * don't fill in anything in this form.
                 * Caller must make sure that this array has
                 * enough space. */
{
    int outputPoints, i;
    int numSegments = (numPoints+1)/3;
    double *segPtr;

    /*
     * The input describes a curve with s Bezier curve segments if there are
     * 3s+1, 3s, or 3s-1 input points. In the last two cases, 1 or 2 initial
     * points from the first curve segment are reused as defining points also
     * for the last curve segment. In the case of 3s input points, this will
     * automatically close the curve.
     */

    if (!pointPtr) {
    /*
     * If pointPtr == NULL, this function returns an upper limit of the
     * array size to store the coordinates. This can be used to allocate
     * storage, before the actual coordinates are calculated.
     */

    return 1 + numSegments * numSteps;
    }

    outputPoints = 0;
    if (xPoints != NULL) {
    Tk_PathCanvasDrawableCoords(canvas, pointPtr[0], pointPtr[1],
        &xPoints->x, &xPoints->y);
    xPoints += 1;
    }
    if (dblPoints != NULL) {
    dblPoints[0] = pointPtr[0];
    dblPoints[1] = pointPtr[1];
    dblPoints += 2;
    }
    outputPoints += 1;

    /*
     * The next loop handles all curve segments except one that overlaps the
     * end of the list of coordinates.
     */

    for (i=numPoints,segPtr=pointPtr ; i>=4 ; i-=3,segPtr+=6) {
    if (segPtr[0]==segPtr[2] && segPtr[1]==segPtr[3] &&
        segPtr[4]==segPtr[6] && segPtr[5]==segPtr[7]) {
        /*
         * The control points on this segment are equal to their
         * neighbouring knots, so this segment is just a straight line. A
         * single point is sufficient.
         */

        if (xPoints != NULL) {
        Tk_PathCanvasDrawableCoords(canvas, segPtr[6], segPtr[7],
            &xPoints->x, &xPoints->y);
        xPoints += 1;
        }
        if (dblPoints != NULL) {
        dblPoints[0] = segPtr[6];
        dblPoints[1] = segPtr[7];
        dblPoints += 2;
        }
        outputPoints += 1;
    } else {
        /*
         * This is a generic Bezier curve segment.
         */

        if (xPoints != NULL) {
        TkPathBezierScreenPoints(canvas, segPtr, numSteps, xPoints);
        xPoints += numSteps;
        }
        if (dblPoints != NULL) {
        TkPathBezierPoints(segPtr, numSteps, dblPoints);
        dblPoints += 2*numSteps;
        }
        outputPoints += numSteps;
    }
    }

    /*
     * If at this point i>1, then there is some point which has not yet been
     * used. Make another curve segment.
     */

    if (i > 1) {
    int j;
    double control[8];

    /*
     * Copy the relevant coordinates to control[], so that it can be
     * passed as a unit to e.g. TkPathBezierPoints.
     */

    for (j=0; j<2*i; j++) {
        control[j] = segPtr[j];
    }
    for (; j<8; j++) {
        control[j] = pointPtr[j-2*i];
    }

    /*
     * Then we just do the same things as above.
     */

    if (control[0]==control[2] && control[1]==control[3] &&
        control[4]==control[6] && control[5]==control[7]) {
        /*
         * The control points on this segment are equal to their
         * neighbouring knots, so this segment is just a straight line. A
         * single point is sufficient.
         */

        if (xPoints != NULL) {
        Tk_PathCanvasDrawableCoords(canvas, control[6], control[7],
            &xPoints->x, &xPoints->y);
        xPoints += 1;
        }
        if (dblPoints != NULL) {
        dblPoints[0] = control[6];
        dblPoints[1] = control[7];
        dblPoints += 2;
        }
        outputPoints += 1;
    } else {
        /*
         * This is a generic Bezier curve segment.
         */

        if (xPoints != NULL) {
        TkPathBezierScreenPoints(canvas, control, numSteps, xPoints);
        xPoints += numSteps;
        }
        if (dblPoints != NULL) {
        TkPathBezierPoints(control, numSteps, dblPoints);
        dblPoints += 2*numSteps;
        }
        outputPoints += numSteps;
    }
    }

    return outputPoints;
}


static int
GetOffset(Tcl_Interp *interp, ClientData clientData,
    Tcl_Obj *offsetObj, Tk_Window tkwin, Tk_TSOffset *offsetPtr)
{
    char *value = Tcl_GetString(offsetObj);
    Tk_TSOffset tsoffset;
    const char *q, *p;
    int result;

    if ((value == NULL) || (*value == 0)) {
    tsoffset.flags = TK_OFFSET_CENTER|TK_OFFSET_MIDDLE;
    goto goodTSOffset;
    }
    tsoffset.flags = 0;
    p = value;

    switch(value[0]) {
    case '#':
    if (PTR2INT(clientData) & TK_OFFSET_RELATIVE) {
        tsoffset.flags = TK_OFFSET_RELATIVE;
        p++;
        break;
    }
    goto badTSOffset;
    case 'e':
    switch(value[1]) {
    case '\0':
        tsoffset.flags = TK_OFFSET_RIGHT|TK_OFFSET_MIDDLE;
        goto goodTSOffset;
    case 'n':
        if (value[2]!='d' || value[3]!='\0') {
        goto badTSOffset;
        }
        tsoffset.flags = INT_MAX;
        goto goodTSOffset;
    }
    case 'w':
    if (value[1] != '\0') {goto badTSOffset;}
    tsoffset.flags = TK_OFFSET_LEFT|TK_OFFSET_MIDDLE;
    goto goodTSOffset;
    case 'n':
    if ((value[1] != '\0') && (value[2] != '\0')) {
        goto badTSOffset;
    }
    switch(value[1]) {
    case '\0':
        tsoffset.flags = TK_OFFSET_CENTER|TK_OFFSET_TOP;
        goto goodTSOffset;
    case 'w':
        tsoffset.flags = TK_OFFSET_LEFT|TK_OFFSET_TOP;
        goto goodTSOffset;
    case 'e':
        tsoffset.flags = TK_OFFSET_RIGHT|TK_OFFSET_TOP;
        goto goodTSOffset;
    }
    goto badTSOffset;
    case 's':
    if ((value[1] != '\0') && (value[2] != '\0')) {
        goto badTSOffset;
    }
    switch(value[1]) {
    case '\0':
        tsoffset.flags = TK_OFFSET_CENTER|TK_OFFSET_BOTTOM;
        goto goodTSOffset;
    case 'w':
        tsoffset.flags = TK_OFFSET_LEFT|TK_OFFSET_BOTTOM;
        goto goodTSOffset;
    case 'e':
        tsoffset.flags = TK_OFFSET_RIGHT|TK_OFFSET_BOTTOM;
        goto goodTSOffset;
    }
    goto badTSOffset;
    case 'c':
    if (strncmp(value, "center", strlen(value)) != 0) {
        goto badTSOffset;
    }
    tsoffset.flags = TK_OFFSET_CENTER|TK_OFFSET_MIDDLE;
    goto goodTSOffset;
    }
    if ((q = strchr(p,',')) == NULL) {
    if (PTR2INT(clientData) & TK_OFFSET_INDEX) {
        if (Tcl_GetInt(interp, (char *) p, &tsoffset.flags) != TCL_OK) {
        Tcl_ResetResult(interp);
        goto badTSOffset;
        }
        tsoffset.flags |= TK_OFFSET_INDEX;
        goto goodTSOffset;
    }
    goto badTSOffset;
    }
    *((char *) q) = 0;
    result = Tk_GetPixels(interp, tkwin, (char *) p, &tsoffset.xoffset);
    *((char *) q) = ',';
    if (result != TCL_OK) {
    return TCL_ERROR;
    }
    if (Tk_GetPixels(interp, tkwin, (char*)q+1, &tsoffset.yoffset) != TCL_OK) {
    return TCL_ERROR;
    }

goodTSOffset:
    /*
     * Below is a hack to allow the stipple/tile offset to be stored in the
     * internal tile structure. Most of the times, offsetPtr is a pointer to
     * an already existing tile structure. However if this structure is not
     * already created, we must do it with Tk_GetTile()!!!!;
     */

    memcpy(offsetPtr, &tsoffset, sizeof(Tk_TSOffset));
    return TCL_OK;

badTSOffset:
    Tcl_AppendResult(interp, "bad offset \"", value,
        "\": expected \"x,y\"", NULL);
    if (PTR2INT(clientData) & TK_OFFSET_RELATIVE) {
    Tcl_AppendResult(interp, ", \"#x,y\"", NULL);
    }
    if (PTR2INT(clientData) & TK_OFFSET_INDEX) {
    Tcl_AppendResult(interp, ", <index>", NULL);
    }
    Tcl_AppendResult(interp, ", n, ne, e, se, s, sw, w, nw, or center", NULL);
    return TCL_ERROR;
}

/* Return NULL on error and leave error message */

static Tk_TSOffset *
PathOffsetNew(Tcl_Interp *interp, ClientData clientData, Tk_Window tkwin, Tcl_Obj *offsetObj)
{
    Tk_TSOffset *offsetPtr;

    offsetPtr = (Tk_TSOffset *) ckalloc(sizeof(Tk_TSOffset));
    if (GetOffset(interp, clientData, offsetObj, tkwin, offsetPtr) != TCL_OK) {
    ckfree((char *) offsetPtr);
    return NULL;;
    }
    return offsetPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkPathOffsetOptionSetProc --
 *
 *    Converts the offset of a stipple or tile into the Tk_TSOffset
 *    structure.
 *
 *----------------------------------------------------------------------
 */

int
TkPathOffsetOptionSetProc(
    ClientData clientData,
    Tcl_Interp *interp,        /* Current interp; may be used for errors. */
    Tk_Window tkwin,        /* Window for which option is being set. */
    Tcl_Obj **value,        /* Pointer to the pointer to the value object.
                             * We use a pointer to the pointer because
                             * we may need to return a value (NULL). */
    char *recordPtr,        /* Pointer to storage for the widget record. */
    int internalOffset,        /* Offset within *recordPtr at which the
                               internal value is to be stored. */
    char *oldInternalPtr,   /* Pointer to storage for the old value. */
    int flags)            /* Flags for the option, set Tk_SetOptions. */
{
    char *internalPtr;        /* Points to location in record where
                             * internal representation of value should
                             * be stored, or NULL. */
    Tcl_Obj *valuePtr;
    Tk_TSOffset *newPtr = NULL;

    valuePtr = *value;
    if (internalOffset >= 0) {
        internalPtr = recordPtr + internalOffset;
    } else {
        internalPtr = NULL;
    }
    if ((flags & TK_OPTION_NULL_OK) && TkPathObjectIsEmpty(valuePtr)) {
    valuePtr = NULL;
    newPtr = NULL;
    }
    if (internalPtr != NULL) {
    if (valuePtr != NULL) {
        newPtr = PathOffsetNew(interp, clientData, tkwin, valuePtr);
        if (newPtr == NULL) {
        return TCL_ERROR;
        }
        }
    *((Tk_TSOffset **) oldInternalPtr) = *((Tk_TSOffset **) internalPtr);
    *((Tk_TSOffset **) internalPtr) = newPtr;
    }
    return TCL_OK;
}

Tcl_Obj *
TkPathOffsetOptionGetProc(
    ClientData clientData,
    Tk_Window tkwin,
    char *recordPtr,        /* Pointer to widget record. */
    int internalOffset)        /* Offset within *recordPtr containing the
                 * value. */
{
    Tk_TSOffset *offsetPtr;
    char buffer[32], *p;

    offsetPtr = *((Tk_TSOffset **) (recordPtr + internalOffset));
    buffer[0] = '\0';
    if (offsetPtr->flags & TK_OFFSET_INDEX) {
    if (offsetPtr->flags >= INT_MAX) {
        strcat(buffer, "end");
    } else {
        sprintf(buffer, "%d", offsetPtr->flags & ~TK_OFFSET_INDEX);
    }
    goto end;
    }
    if (offsetPtr->flags & TK_OFFSET_TOP) {
    if (offsetPtr->flags & TK_OFFSET_LEFT) {
        strcat(buffer, "nw");
        goto end;
    } else if (offsetPtr->flags & TK_OFFSET_CENTER) {
        strcat(buffer, "n");
        goto end;
    } else if (offsetPtr->flags & TK_OFFSET_RIGHT) {
        strcat(buffer, "ne");
        goto end;
    }
    } else if (offsetPtr->flags & TK_OFFSET_MIDDLE) {
    if (offsetPtr->flags & TK_OFFSET_LEFT) {
        strcat(buffer, "w");
        goto end;
    } else if (offsetPtr->flags & TK_OFFSET_CENTER) {
        strcat(buffer, "center");
        goto end;
    } else if (offsetPtr->flags & TK_OFFSET_RIGHT) {
        strcat(buffer, "e");
        goto end;
    }
    } else if (offsetPtr->flags & TK_OFFSET_BOTTOM) {
    if (offsetPtr->flags & TK_OFFSET_LEFT) {
        strcat(buffer, "sw");
        goto end;
    } else if (offsetPtr->flags & TK_OFFSET_CENTER) {
        strcat(buffer, "s");
        goto end;
    } else if (offsetPtr->flags & TK_OFFSET_RIGHT) {
        strcat(buffer, "se");
        goto end;
    }
    }
    p = buffer;
    if (offsetPtr->flags & TK_OFFSET_RELATIVE) {
    strcat(buffer , "#");
    p++;
    }
    sprintf(p, "%d,%d", offsetPtr->xoffset, offsetPtr->yoffset);

end:
    return Tcl_NewStringObj(buffer, -1);
}

void
TkPathOffsetOptionRestoreProc(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr,        /* Pointer to storage for value. */
    char *oldInternalPtr)    /* Pointer to old value. */
{
    *(Tk_TSOffset **)internalPtr = *(Tk_TSOffset **)oldInternalPtr;
}

void
TkPathOffsetOptionFreeProc(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr)        /* Pointer to storage for value. */
{
    if (*((char **) internalPtr) != NULL) {
    ckfree((char *) *((char **) internalPtr));
    }
}

/*
 *--------------------------------------------------------------
 *
 * GetDoublePixels --
 *
 *    Given a string, returns the number of pixels corresponding
 *    to that string.
 *
 * Results:
 *    The return value is a standard Tcl return result.  If
 *    TCL_OK is returned, then everything went well and the
 *    pixel distance is stored at *doublePtr;  otherwise
 *    TCL_ERROR is returned and an error message is left in
 *    interp->result.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

static int
GetDoublePixels(
    Tcl_Interp *interp,        /* Use this for error reporting. */
    Tk_Window tkwin,        /* Window whose screen determines conversion
                 * from centimeters and other absolute
                 * units. */
    const char *string,        /* String describing a number of pixels. */
    double *doublePtr)        /* Place to store converted result. */
{
    char *end;
    double d;
    int widthM, widthS;
#ifdef PLATFORM_SDL
    double dW, dH;
#endif

    d = strtod((char *) string, &end);
    if (end == string) {
    error:
    Tcl_AppendResult(interp, "bad screen distance \"", string,
        "\"", (char *) NULL);
    return TCL_ERROR;
    }
    while ((*end != '\0') && isspace(UCHAR(*end))) {
    end++;
    }
#ifdef PLATFORM_SDL
    dW = WidthOfScreen(Tk_Screen(tkwin));
    dW /= WidthMMOfScreen(Tk_Screen(tkwin));
    dH = HeightOfScreen(Tk_Screen(tkwin));
    dH /= HeightMMOfScreen(Tk_Screen(tkwin));
    if (dH > dW) {
    widthS = HeightOfScreen(Tk_Screen(tkwin));
    widthM = HeightMMOfScreen(Tk_Screen(tkwin));
    } else
#endif
    {
    widthS = WidthOfScreen(Tk_Screen(tkwin));
    widthM = WidthMMOfScreen(Tk_Screen(tkwin));
    }
    switch (*end) {
    case 0:
        break;
    case 'c':
        d *= 10*widthS;
        d /= widthM;
        end++;
        break;
    case 'i':
        d *= 25.4*widthS;
        d /= widthM;
        end++;
        break;
    case 'm':
        d *= widthS;
        d /= widthM;
        end++;
        break;
    case 'p':
        d *= (25.4/72.0)*widthS;
        d /= widthM;
        end++;
        break;
    default:
        goto error;
    }
    while ((*end != '\0') && isspace(UCHAR(*end))) {
    end++;
    }
    if (*end != 0) {
    goto error;
    }
    *doublePtr = d;
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * Tk_PathPixelOptionSetProc --
 *
 *    As TK_OPTION_PIXELS but for double value instead of int.
 *
 * Results:
 *    The return value is a standard Tcl return result.  If
 *    TCL_OK is returned, then everything went well and the
 *    pixel distance is stored at *doublePtr;  otherwise
 *    TCL_ERROR is returned and an error message is left in
 *    interp->result.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

int
Tk_PathPixelOptionSetProc(
    ClientData clientData,
    Tcl_Interp *interp,        /* Current interp; may be used for errors. */
    Tk_Window tkwin,        /* Window for which option is being set. */
    Tcl_Obj **value,        /* Pointer to the pointer to the value object.
                             * We use a pointer to the pointer because
                             * we may need to return a value (NULL). */
    char *recordPtr,        /* Pointer to storage for the widget record. */
    int internalOffset,        /* Offset within *recordPtr at which the
                               internal value is to be stored. */
    char *oldInternalPtr,   /* Pointer to storage for the old value. */
    int flags)            /* Flags for the option, set Tk_SetOptions. */
{
    char *internalPtr;        /* Points to location in record where
                             * internal representation of value should
                             * be stored, or NULL. */
    Tcl_Obj *valuePtr;
    double newPixels;
    int result;

    valuePtr = *value;
    if (internalOffset >= 0) {
        internalPtr = recordPtr + internalOffset;
    } else {
        internalPtr = NULL;
    }
    if ((flags & TK_OPTION_NULL_OK) && TkPathObjectIsEmpty(valuePtr)) {
    valuePtr = NULL;
    newPixels = 0.0;
    }
    if (internalPtr != NULL) {
    if (valuePtr != NULL) {
        result = GetDoublePixels(interp, tkwin, Tcl_GetString(valuePtr), &newPixels);
        if (result != TCL_OK) {
        return TCL_ERROR;
        } else if (newPixels < 0.0) {
        Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
            "bad screen distance \"", Tcl_GetString(valuePtr), "\"", NULL);
        return TCL_ERROR;
        }
        }
    *((double *) oldInternalPtr) = *((double *) internalPtr);
    *((double *) internalPtr) = newPixels;
    }
    return TCL_OK;
}

Tcl_Obj *
Tk_PathPixelOptionGetProc(
    ClientData clientData,
    Tk_Window tkwin,
    char *recordPtr,        /* Pointer to widget record. */
    int internalOffset)        /* Offset within *recordPtr containing the
                 * value. */
{
    return Tcl_NewDoubleObj(*((double *) (recordPtr + internalOffset)));
}

void
Tk_PathPixelOptionRestoreProc(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr,        /* Pointer to storage for value. */
    char *oldInternalPtr)    /* Pointer to old value. */
{
    *(double **)internalPtr = *(double **)oldInternalPtr;
}

/*
 * Structures defined only in this file.
 */

typedef struct SmoothAssocData {
    struct SmoothAssocData *nextPtr;
                /* Pointer to next SmoothAssocData. */
    Tk_PathSmoothMethod smooth;    /* Name and functions associated with this
                 * option. */
} SmoothAssocData;

Tk_PathSmoothMethod tkPathBezierSmoothMethod = {
    "true",
    TkPathMakeBezierCurve
};
static Tk_PathSmoothMethod tkPathRawSmoothMethod = {
    "raw",
    TkPathMakeRawCurve
};

/*
 * Function forward-declarations.
 */

static void            SmoothMethodCleanupProc(ClientData clientData,
                Tcl_Interp *interp);
static SmoothAssocData *    InitSmoothMethods(Tcl_Interp *interp);
static int            FindSmoothMethod(Tcl_Interp *interp, Tcl_Obj *valueObj,
                Tk_PathSmoothMethod **smoothPtr);
static int            DashConvert(char *l, const char *p, int n,
                double width);
static void            TranslateAndAppendCoords(TkPathCanvas *canvPtr,
                double x, double y, XPoint *outArr, int numOut);

static Tk_Dash *        DashNew(Tcl_Interp *interp, Tcl_Obj *dashObj);
static void            DashFree(Tk_Dash *dashPtr);

#ifndef ABS
#    define ABS(a)        (((a) >= 0)  ? (a) : -1*(a))
#endif

/*
 *----------------------------------------------------------------------
 *
 * Tk_PathCanvasTkwin --
 *
 *    Given a token for a canvas, this function returns the widget that
 *    represents the canvas.
 *
 * Results:
 *    The return value is a handle for the widget.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

Tk_Window
Tk_PathCanvasTkwin(
    Tk_PathCanvas canvas)        /* Token for the canvas. */
{
    TkPathCanvas *canvasPtr = (TkPathCanvas *) canvas;
    return canvasPtr->tkwin;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_PathCanvasDrawableCoords --
 *
 *    Given an (x,y) coordinate pair within a canvas, this function
 *    returns the corresponding coordinates at which the point should
 *    be drawn in the drawable used for display.
 *
 * Results:
 *    There is no return value. The values at *drawableXPtr and
 *    *drawableYPtr are filled in with the coordinates at which x and y
 *    should be drawn. These coordinates are clipped to fit within a
 *    "short", since this is what X uses in most cases for drawing.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

void
Tk_PathCanvasDrawableCoords(
    Tk_PathCanvas canvas,        /* Token for the canvas. */
    double x,            /* Coordinates in canvas space. */
    double y,
    short *drawableXPtr,    /* Screen coordinates are stored here. */
    short *drawableYPtr)
{
    TkPathCanvas *canvasPtr = (TkPathCanvas *) canvas;
    double tmp;

    tmp = x - canvasPtr->drawableXOrigin;
    if (tmp > 0) {
    tmp += 0.5;
    } else {
    tmp -= 0.5;
    }
    if (tmp > 32767) {
    *drawableXPtr = 32767;
    } else if (tmp < -32768) {
    *drawableXPtr = -32768;
    } else {
    *drawableXPtr = (short) tmp;
    }

    tmp = y - canvasPtr->drawableYOrigin;
    if (tmp > 0) {
    tmp += 0.5;
    } else {
    tmp -= 0.5;
    }
    if (tmp > 32767) {
    *drawableYPtr = 32767;
    } else if (tmp < -32768) {
    *drawableYPtr = -32768;
    } else {
    *drawableYPtr = (short) tmp;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_PathCanvasWindowCoords --
 *
 *    Given an (x,y) coordinate pair within a canvas, this function returns
 *    the corresponding coordinates in the canvas's window.
 *
 * Results:
 *    There is no return value. The values at *screenXPtr and *screenYPtr
 *    are filled in with the coordinates at which (x,y) appears in the
 *    canvas's window. These coordinates are clipped to fit within a
 *    "short", since this is what X uses in most cases for drawing.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

void
Tk_PathCanvasWindowCoords(
    Tk_PathCanvas canvas,        /* Token for the canvas. */
    double x,            /* Coordinates in canvas space. */
    double y,
    short *screenXPtr,        /* Screen coordinates are stored here. */
    short *screenYPtr)
{
    TkPathCanvas *canvasPtr = (TkPathCanvas *) canvas;
    double tmp;

    tmp = x - canvasPtr->xOrigin;
    if (tmp > 0) {
    tmp += 0.5;
    } else {
    tmp -= 0.5;
    }
    if (tmp > 32767) {
    *screenXPtr = 32767;
    } else if (tmp < -32768) {
    *screenXPtr = -32768;
    } else {
    *screenXPtr = (short) tmp;
    }

    tmp = y - canvasPtr->yOrigin;
    if (tmp > 0) {
    tmp += 0.5;
    } else {
    tmp -= 0.5;
    }
    if (tmp > 32767) {
    *screenYPtr = 32767;
    } else if (tmp < -32768) {
    *screenYPtr = -32768;
    } else {
    *screenYPtr = (short) tmp;
    }
}

/*
 *--------------------------------------------------------------
 *
 * Tk_PathCanvasGetCoord --
 *
 *    Given a string, returns a floating-point canvas coordinate
 *    corresponding to that string.
 *
 * Results:
 *    The return value is a standard Tcl return result. If TCL_OK is
 *    returned, then everything went well and the canvas coordinate is
 *    stored at *doublePtr; otherwise TCL_ERROR is returned and an error
 *    message is left in the interp's result.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

int
Tk_PathCanvasGetCoord(
    Tcl_Interp *interp,        /* Interpreter for error reporting. */
    Tk_PathCanvas canvas,        /* Canvas to which coordinate applies. */
    const char *string,        /* Describes coordinate (any screen coordinate
                 * form may be used here). */
    double *doublePtr)        /* Place to store converted coordinate. */
{
    TkPathCanvas *canvasPtr = (TkPathCanvas *) canvas;

    if (Tk_GetScreenMM(canvasPtr->interp, canvasPtr->tkwin, string,
        doublePtr) != TCL_OK) {
    return TCL_ERROR;
    }
    *doublePtr *= canvasPtr->pixelsPerMM;
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * Tk_PathCanvasGetCoordFromObj --
 *
 *    Given a string, returns a floating-point canvas coordinate
 *    corresponding to that string.
 *
 * Results:
 *    The return value is a standard Tcl return result. If TCL_OK is
 *    returned, then everything went well and the canvas coordinate is
 *    stored at *doublePtr; otherwise TCL_ERROR is returned and an error
 *    message is left in interp->result.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

int
Tk_PathCanvasGetCoordFromObj(
    Tcl_Interp *interp,        /* Interpreter for error reporting. */
    Tk_PathCanvas canvas,    /* Canvas to which coordinate applies. */
    Tcl_Obj *obj,        /* Describes coordinate (any screen coordinate
                 * form may be used here). */
    double *doublePtr)        /* Place to store converted coordinate. */
{
    TkPathCanvas *canvasPtr = (TkPathCanvas *) canvas;

#ifndef USE_TK_STUBS
    return Tk_GetDoublePixelsFromObj(canvasPtr->interp,
        canvasPtr->tkwin, obj, doublePtr);
#else
    int pixels;

    if (Tk_GetMMFromObj(canvasPtr->interp, canvasPtr->tkwin, obj,
        doublePtr) != TCL_OK) {
    return TCL_ERROR;
    }
    *doublePtr *= canvasPtr->pixelsPerMM;

    /*
     * Unfortunately, Tcl_GetDoublePixelsFromObj() is not a public
     * interface, so we try here to overcome rounding errors.
     */
    pixels = *doublePtr;
    Tk_GetPixelsFromObj(canvasPtr->interp, canvasPtr->tkwin, obj, &pixels);
    if (fabs(*doublePtr - pixels) < 1e-9) {
    *doublePtr = pixels;
    }
    return TCL_OK;
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_PathCanvasSetStippleOrigin --
 *
 *    This function sets the stipple origin in a graphics context so that
 *    stipples drawn with the GC will line up with other stipples previously
 *    drawn in the canvas.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The graphics context is modified.
 *
 *----------------------------------------------------------------------
 */

void
Tk_PathCanvasSetStippleOrigin(
    Tk_PathCanvas canvas,        /* Token for a canvas. */
    GC gc)            /* Graphics context that is about to be used
                 * to draw a stippled pattern as part of
                 * redisplaying the canvas. */
{
    TkPathCanvas *canvasPtr = (TkPathCanvas *) canvas;

    XSetTSOrigin(canvasPtr->display, gc, -canvasPtr->drawableXOrigin,
        -canvasPtr->drawableYOrigin);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_PathCanvasSetOffset --
 *
 *    This function sets the stipple offset in a graphics context so that
 *    stipples drawn with the GC will line up with other stipples with the
 *    same offset.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The graphics context is modified.
 *
 *----------------------------------------------------------------------
 */

void
Tk_PathCanvasSetOffset(
    Tk_PathCanvas canvas,    /* Token for a canvas. */
    GC gc,            /* Graphics context that is about to be used
                 * to draw a stippled pattern as part of
                 * redisplaying the canvas. */
    Tk_TSOffset *offset)    /* Offset (may be NULL pointer)*/
{
    TkPathCanvas *canvasPtr = (TkPathCanvas *) canvas;
    int flags = 0;
    int x = - canvasPtr->drawableXOrigin;
    int y = - canvasPtr->drawableYOrigin;

    if (offset != NULL) {
    flags = offset->flags;
    x += offset->xoffset;
    y += offset->yoffset;
    }
    if ((flags & TK_OFFSET_RELATIVE) && !(flags & TK_OFFSET_INDEX)) {
    Tk_SetTSOrigin(canvasPtr->tkwin, gc, x - canvasPtr->xOrigin,
        y - canvasPtr->yOrigin);
    } else {
    XSetTSOrigin(canvasPtr->display, gc, x, y);
    }
}

int
TkPathCanvasGetDepth(Tk_PathItem *itemPtr)
{
    int depth = 0;
    Tk_PathItem *walkPtr = itemPtr;

    while (walkPtr->parentPtr != NULL) {
    depth++;
    walkPtr = walkPtr->parentPtr;
    }
    return depth;
}

/*
 *----------------------------------------------------------------------
 *
 * TkPathCanvasInheritStyle --
 *
 *    This function returns the style which is inherited from the
 *      parents of the itemPtr using cascading from the root item.
 *    Must use TkPathCanvasFreeInheritedStyle when done.
 *
 * Results:
 *    Tk_PathStyle.
 *
 * Side effects:
 *    May allocate memory for matrix.
 *
 *----------------------------------------------------------------------
 */

Tk_PathStyle
TkPathCanvasInheritStyle(Tk_PathItem *itemPtr, long flags)
{
    int depth, i, anyMatrix = 0;
    Tk_PathItem *walkPtr;
    Tk_PathItemEx *itemExPtr;
    Tk_PathItemEx **parents;
    Tk_PathStyle style;
    TkPathMatrix matrix = TK_PATH_UNIT_TMATRIX;

    depth = TkPathCanvasGetDepth(itemPtr);
    parents = (Tk_PathItemEx **) ckalloc(depth*sizeof(Tk_PathItemEx *));

    walkPtr = itemPtr, i = 0;
    while (walkPtr->parentPtr != NULL) {
    parents[i] = (Tk_PathItemEx *) walkPtr->parentPtr;
    walkPtr = walkPtr->parentPtr, i++;
    }

    /*
     * Cascade the style from the root item to the closest parent.
     * Start by just making a copy of the root's style.
     */
    itemExPtr = parents[depth-1];
    style = itemExPtr->style;

    for (i = depth-1; i >= 0; i--) {
    itemExPtr = parents[i];

    /* The order of these two merges decides which take precedence. */
    if (i < depth-1) {
        TkPathStyleMergeStyles(&itemExPtr->style, &style, flags);
    }
    if (itemExPtr->styleInst != NULL) {
        TkPathStyleMergeStyles(itemExPtr->styleInst->masterPtr, &style, flags);
    }
    if (style.matrixPtr != NULL) {
        anyMatrix = 1;
        TkPathMMulTMatrix(style.matrixPtr, &matrix);
    }
    /*
     * We set matrix to NULL to detect if set in group.
     */
    style.matrixPtr = NULL;
    }

    /*
     * Merge the parents style with the actual items style.
     * The order of these two merges decides which take precedence.
     */
    itemExPtr = (Tk_PathItemEx *) itemPtr;
    TkPathStyleMergeStyles(&itemExPtr->style, &style, flags);
    if (itemExPtr->styleInst != NULL) {
    TkPathStyleMergeStyles(itemExPtr->styleInst->masterPtr, &style, flags);
    }
    if (style.matrixPtr != NULL) {
    anyMatrix = 1;
    TkPathMMulTMatrix(style.matrixPtr, &matrix);
    }
    if (anyMatrix) {
        style.matrixPtr = (TkPathMatrix *) ckalloc(sizeof(TkPathMatrix));
    memcpy(style.matrixPtr, &matrix, sizeof(TkPathMatrix));
    }
    ckfree((char *) parents);
    return style;
}

void
TkPathCanvasFreeInheritedStyle(Tk_PathStyle *stylePtr)
{
    if (stylePtr->matrixPtr != NULL) {
    ckfree((char *) stylePtr->matrixPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkPathCanvasInheritTMatrix --
 *
 *    Does the same job as TkPathCanvasInheritStyle but for the
 *    TkPathMatrix only. No memory allocated.
 *    Note that we don't do the last step of concatenating the items
 *    own TkPathMatrix since that depends on its specific storage.
 *
 * Results:
 *    TkPathMatrix.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

TkPathMatrix
TkPathCanvasInheritTMatrix(Tk_PathItem *itemPtr)
{
    int depth, i;
    Tk_PathItem *walkPtr;
    Tk_PathItemEx *itemExPtr;
    Tk_PathItemEx **parents;
    Tk_PathStyle *stylePtr;
    TkPathMatrix matrix = TK_PATH_UNIT_TMATRIX, *matrixPtr = NULL;

    depth = TkPathCanvasGetDepth(itemPtr);
    parents = (Tk_PathItemEx **) ckalloc(depth*sizeof(Tk_PathItemEx *));

    walkPtr = itemPtr, i = 0;
    while (walkPtr->parentPtr != NULL) {
    parents[i] = (Tk_PathItemEx *) walkPtr->parentPtr;
    walkPtr = walkPtr->parentPtr, i++;
    }

    for (i = depth-1; i >= 0; i--) {
    itemExPtr = parents[i];

    /* The order of these two merges decides which take precedence. */
    matrixPtr = itemExPtr->style.matrixPtr;
    if (itemExPtr->styleInst != NULL) {
        stylePtr = itemExPtr->styleInst->masterPtr;
        if (stylePtr->mask & TK_PATH_STYLE_OPTION_MATRIX) {
        matrixPtr = stylePtr->matrixPtr;
        }
    }
    if (matrixPtr != NULL) {
        TkPathMMulTMatrix(matrixPtr, &matrix);
    }
    }
    ckfree((char *) parents);
    return matrix;
}

/* TkPathCanvasGradientTable etc.: this is just accessor functions to hide
   the internals of the TkPathCanvas */

Tcl_HashTable *
TkPathCanvasGradientTable(Tk_PathCanvas canvas)
{
    return &((TkPathCanvas *)canvas)->gradientTable;
}

Tcl_HashTable *
TkPathCanvasStyleTable(Tk_PathCanvas canvas)
{
    return &((TkPathCanvas *)canvas)->styleTable;
}

Tk_PathState
TkPathCanvasState(Tk_PathCanvas canvas)
{
    return ((TkPathCanvas *)canvas)->canvas_state;
}

Tk_PathItem *
TkPathCanvasCurrentItem(Tk_PathCanvas canvas)
{
    return ((TkPathCanvas *)canvas)->currentItemPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_PathCanvasGetTextInfo --
 *
 *    This function returns a pointer to a structure containing information
 *    about the selection and insertion cursor for a canvas widget. Items
 *    such as text items save the pointer and use it to share access to the
 *    information with the generic canvas code.
 *
 * Results:
 *    The return value is a pointer to the structure holding text
 *    information for the canvas. Most of the fields should not be modified
 *    outside the generic canvas code; see the user documentation for
 *    details.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

Tk_PathCanvasTextInfo *
Tk_PathCanvasGetTextInfo(
    Tk_PathCanvas canvas)    /* Token for the canvas widget. */
{
    return &((TkPathCanvas *) canvas)->textInfo;
}

/*
 *----------------------------------------------------------------------
 *
 * TkPathAllocTagsFromObj --
 *
 *    Create a new Tk_PathTags record and fill it with a tag object list.
 *
 * Results:
 *    A pointer to Tk_PathTags record or NULL if failed.
 *
 * Side effects:
 *    New Tk_PathTags possibly allocated.
 *
 *----------------------------------------------------------------------
 */

Tk_PathTags *
TkPathAllocTagsFromObj(
    Tcl_Interp *interp,
    Tcl_Obj *valuePtr)    /* If NULL we just create an empty Tk_PathTags struct. */
{
    Tk_PathTags *tagsPtr;
    int objc, i, len;
    Tcl_Obj **objv;

    if (TkPathObjectIsEmpty(valuePtr)) {
    objc = 0;
    } else if (Tcl_ListObjGetElements(interp, valuePtr, &objc, &objv) != TCL_OK) {
    return NULL;
    }
    len = MAX(objc, TK_PATHTAG_SPACE);
    tagsPtr = (Tk_PathTags *) ckalloc(sizeof(Tk_PathTags));
    tagsPtr->tagSpace = len;
    tagsPtr->numTags = objc;
    tagsPtr->tagPtr = (Tk_Uid *) ckalloc((unsigned) (len * sizeof(Tk_Uid)));
    for (i = 0; i < objc; i++) {
    tagsPtr->tagPtr[i] = Tk_GetUid(Tcl_GetStringFromObj(objv[i], NULL));
    }
    return tagsPtr;
}

static void
PathFreeTags(Tk_PathTags *tagsPtr)
{
    if (tagsPtr->tagPtr != NULL) {
    ckfree((char *) tagsPtr->tagPtr);
    }
}

/*
 *--------------------------------------------------------------
 *
 * Tk_PathCanvasTagsOptionSetProc --
 *
 *    This function is invoked during option processing to handle "-tags"
 *    options for canvas items.
 *
 * Results:
 *    A standard Tcl return value.
 *
 * Side effects:
 *    The tags for a given item get replaced by those indicated in the value
 *    argument.
 *
 *--------------------------------------------------------------
 */

int
Tk_PathCanvasTagsOptionSetProc(
    ClientData clientData,
    Tcl_Interp *interp,        /* Current interp; may be used for errors. */
    Tk_Window tkwin,        /* Window for which option is being set. */
    Tcl_Obj **value,        /* Pointer to the pointer to the value object.
                             * We use a pointer to the pointer because
                             * we may need to return a value (NULL). */
    char *recordPtr,        /* Pointer to storage for the widget record. */
    int internalOffset,        /* Offset within *recordPtr at which the
                               internal value is to be stored. */
    char *oldInternalPtr,   /* Pointer to storage for the old value. */
    int flags)            /* Flags for the option, set Tk_SetOptions. */
{
    char *internalPtr;        /* Points to location in record where
                             * internal representation of value should
                             * be stored, or NULL. */
    Tcl_Obj *valuePtr;
    Tk_PathTags *newPtr = NULL;

    valuePtr = *value;
    if (internalOffset >= 0) {
        internalPtr = recordPtr + internalOffset;
    } else {
        internalPtr = NULL;
    }
    if ((flags & TK_OPTION_NULL_OK) && TkPathObjectIsEmpty(valuePtr)) {
    valuePtr = NULL;
    newPtr = NULL;
    }
    if (internalPtr != NULL) {
    if (valuePtr != NULL) {
        newPtr = TkPathAllocTagsFromObj(interp, valuePtr);
        if (newPtr == NULL) {
        return TCL_ERROR;
        }
        }
    *((Tk_PathTags **) oldInternalPtr) = *((Tk_PathTags **) internalPtr);
    *((Tk_PathTags **) internalPtr) = newPtr;
    }
    return TCL_OK;
}

Tcl_Obj *
Tk_PathCanvasTagsOptionGetProc(
    ClientData clientData,
    Tk_Window tkwin,
    char *recordPtr,        /* Pointer to widget record. */
    int internalOffset)        /* Offset within *recordPtr containing the
                 * value. */
{
    Tk_PathTags    *tagsPtr;
    Tcl_Obj     *listObj;
    int        i;

    tagsPtr = *((Tk_PathTags **) (recordPtr + internalOffset));
    listObj = Tcl_NewListObj( 0, (Tcl_Obj **) NULL );
    if (tagsPtr != NULL) {
    for (i = 0; i < tagsPtr->numTags; i++) {
        Tcl_ListObjAppendElement(NULL, listObj,
                     Tcl_NewStringObj((char *) tagsPtr->tagPtr[i], -1));
    }
    }
    return listObj;
}

void
Tk_PathCanvasTagsOptionRestoreProc(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr,        /* Pointer to storage for value. */
    char *oldInternalPtr)    /* Pointer to old value. */
{
    *(Tk_PathTags **)internalPtr = *(Tk_PathTags **)oldInternalPtr;
}

void
Tk_PathCanvasTagsOptionFreeProc(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr)        /* Pointer to storage for value. */
{
    Tk_PathTags    *tagsPtr;

    tagsPtr = *((Tk_PathTags **) internalPtr);
    if (tagsPtr != NULL) {
    PathFreeTags(tagsPtr);
        ckfree(*((char **) internalPtr));
        *((char **) internalPtr) = NULL;
    }
}

/* Return NULL on error and leave error message */

static Tk_Dash *
DashNew(Tcl_Interp *interp, Tcl_Obj *dashObj)
{
    Tk_Dash *dashPtr;

    dashPtr = (Tk_Dash *) ckalloc(sizeof(Tk_Dash));
    /*
     * NB: Tk_GetDash tries to free any existing pattern unless we zero this.
     */
    dashPtr->number = 0;
    if (Tk_GetDash(interp, Tcl_GetString(dashObj), dashPtr) != TCL_OK) {
    goto error;
    }
    return dashPtr;

error:
    DashFree(dashPtr);
    return NULL;
}

static void
DashFree(Tk_Dash *dashPtr)
{
    if (dashPtr != NULL) {
    if (ABS(dashPtr->number) > sizeof(char *)) {
        ckfree((char *) dashPtr->pattern.pt);
    }
    ckfree((char *) dashPtr);
    }
}

/*
 *--------------------------------------------------------------
 *
 * Tk_DashOptionSetProc, Tk_DashOptionGetProc,
 *    Tk_DashOptionRestoreProc, Tk_DashOptionRestoreProc --
 *
 *    These functions are invoked during option processing to handle
 *    "-dash", "-activedash" and "-disableddash"
 *    options for canvas objects.
 *
 * Results:
 *    According to the Tk_ObjCustomOption struct.
 *
 * Side effects:
 *    Memory allocated or freed.
 *
 *--------------------------------------------------------------
 */

int
Tk_DashOptionSetProc(
    ClientData clientData,
    Tcl_Interp *interp,        /* Current interp; may be used for errors. */
    Tk_Window tkwin,        /* Window for which option is being set. */
    Tcl_Obj **value,        /* Pointer to the pointer to the value object.
                             * We use a pointer to the pointer because
                             * we may need to return a value (NULL). */
    char *recordPtr,        /* Pointer to storage for the widget record. */
    int internalOffset,        /* Offset within *recordPtr at which the
                               internal value is to be stored. */
    char *oldInternalPtr,   /* Pointer to storage for the old value. */
    int flags)            /* Flags for the option, set Tk_SetOptions. */
{
    char *internalPtr;        /* Points to location in record where
                             * internal representation of value should
                             * be stored, or NULL. */
    Tcl_Obj *valuePtr;
    Tk_Dash *newPtr = NULL;

    valuePtr = *value;
    if (internalOffset >= 0) {
        internalPtr = recordPtr + internalOffset;
    } else {
        internalPtr = NULL;
    }
    if ((flags & TK_OPTION_NULL_OK) && TkPathObjectIsEmpty(valuePtr)) {
    valuePtr = NULL;
    newPtr = NULL;
    }
    if (internalPtr != NULL) {
    if (valuePtr != NULL) {
        newPtr = DashNew(interp, valuePtr);
        if (newPtr == NULL) {
        return TCL_ERROR;
        }
        }
    *((Tk_Dash **) oldInternalPtr) = *((Tk_Dash **) internalPtr);
    *((Tk_Dash **) internalPtr) = newPtr;
    }
    return TCL_OK;
}

Tcl_Obj *
Tk_DashOptionGetProc(
    ClientData clientData,
    Tk_Window tkwin,
    char *recordPtr,        /* Pointer to widget record. */
    int internalOffset)        /* Offset within *recordPtr containing the
                 * value. */
{
    Tk_Dash *dashPtr;
    Tcl_Obj *objPtr = NULL;
    char *buffer = NULL;
    char *p;
    int i;

    dashPtr = *((Tk_Dash **) (recordPtr + internalOffset));

    if (dashPtr != NULL) {
    i = dashPtr->number;
    if (i < 0) {
        i = -i;
        buffer = (char *) ckalloc((unsigned int) (i+1));
        p = (i > (int)sizeof(char *)) ? dashPtr->pattern.pt : dashPtr->pattern.array;
        memcpy(buffer, p, (unsigned int) i);
        buffer[i] = 0;
    } else if (!i) {
        buffer = (char *) ckalloc(1);
        buffer[0] = '\0';
    } else {
        buffer = (char *)ckalloc((unsigned int) (4*i));
        p = (i > (int)sizeof(char *)) ? dashPtr->pattern.pt : dashPtr->pattern.array;
        sprintf(buffer, "%d", *p++ & 0xff);
        while (--i) {
        sprintf(buffer+strlen(buffer), " %d", *p++ & 0xff);
        }
    }
    objPtr = Tcl_NewStringObj(buffer, -1);
    }
    if (buffer != NULL) {
    ckfree((char *) buffer);
    }
    return objPtr;
}

void
Tk_DashOptionRestoreProc(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr,        /* Pointer to storage for value. */
    char *oldInternalPtr)    /* Pointer to old value. */
{
    *(Tk_Dash **)internalPtr = *(Tk_Dash **)oldInternalPtr;
}

void
Tk_DashOptionFreeProc(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr)        /* Pointer to storage for value. */
{
    if (*((char **) internalPtr) != NULL) {
        DashFree(*(Tk_Dash **) internalPtr);
    }
}

/*
 *--------------------------------------------------------------
 *
 * InitSmoothMethods --
 *
 *    This function is invoked to set up the initial state of the list of
 *    "-smooth" methods. It should only be called when the list installed
 *    in the interpreter is NULL.
 *
 * Results:
 *    Pointer to the start of the list of default smooth methods.
 *
 * Side effects:
 *    A linked list of smooth methods is created and attached to the
 *    interpreter's association key "smoothPathMethod"
 *
 *--------------------------------------------------------------
 */

static SmoothAssocData *
InitSmoothMethods(
    Tcl_Interp *interp)
{
    SmoothAssocData *methods, *ptr;

    methods = (SmoothAssocData *) ckalloc(sizeof(SmoothAssocData));
    methods->smooth.name = tkPathRawSmoothMethod.name;
    methods->smooth.coordProc = tkPathRawSmoothMethod.coordProc;
    methods->nextPtr = (SmoothAssocData *) ckalloc(sizeof(SmoothAssocData));

    ptr = methods->nextPtr;
    ptr->smooth.name = tkPathBezierSmoothMethod.name;
    ptr->smooth.coordProc = tkPathBezierSmoothMethod.coordProc;
    ptr->nextPtr = NULL;

    Tcl_SetAssocData(interp, "smoothPathMethod", SmoothMethodCleanupProc,
        (ClientData) methods);
    return methods;
}

/*
 *--------------------------------------------------------------
 *
 * Tk_PathCreateSmoothMethod --
 *
 *    This function is invoked to add additional values for the "-smooth"
 *    option to the list.
 *
 * Results:
 *    A standard Tcl return value.
 *
 * Side effects:
 *    In the future "-smooth <name>" will be accepted as smooth method for
 *    the line and polygon.
 *
 *--------------------------------------------------------------
 */

void
Tk_PathCreateSmoothMethod(
    Tcl_Interp *interp,
    Tk_PathSmoothMethod *smooth)
{
    SmoothAssocData *methods, *typePtr2, *prevPtr, *ptr;
    methods = (SmoothAssocData *) Tcl_GetAssocData(interp, "smoothPathMethod",
        NULL);

    /*
     * Initialize if we were not previously initialized.
     */

    if (methods == NULL) {
    methods = InitSmoothMethods(interp);
    }

    /*
     * If there's already a smooth method with the given name, remove it.
     */

    for (typePtr2 = methods, prevPtr = NULL; typePtr2 != NULL;
        prevPtr = typePtr2, typePtr2 = typePtr2->nextPtr) {
    if (!strcmp(typePtr2->smooth.name, smooth->name)) {
        if (prevPtr == NULL) {
        methods = typePtr2->nextPtr;
        } else {
        prevPtr->nextPtr = typePtr2->nextPtr;
        }
        ckfree((char *) typePtr2);
        break;
    }
    }
    ptr = (SmoothAssocData *) ckalloc(sizeof(SmoothAssocData));
    ptr->smooth.name = smooth->name;
    ptr->smooth.coordProc = smooth->coordProc;
    ptr->nextPtr = methods;
    Tcl_SetAssocData(interp, "smoothPathMethod", SmoothMethodCleanupProc,
        (ClientData) ptr);
}

/*
 *----------------------------------------------------------------------
 *
 * SmoothMethodCleanupProc --
 *
 *    This function is invoked whenever an interpreter is deleted to
 *    cleanup the smooth methods.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Smooth methods are removed.
 *
 *----------------------------------------------------------------------
 */

static void
SmoothMethodCleanupProc(
    ClientData clientData,    /* Points to "smoothPathMethod" AssocData for the
                 * interpreter. */
    Tcl_Interp *interp)        /* Interpreter that is being deleted. */
{
    SmoothAssocData *ptr, *methods = (SmoothAssocData *) clientData;

    while (methods != NULL) {
    methods = (ptr = methods)->nextPtr;
    ckfree((char *) ptr);
    }
}

static int
FindSmoothMethod(Tcl_Interp *interp,
    Tcl_Obj *valueObj,
    Tk_PathSmoothMethod **smoothPtr)    /* Place to store converted result. */
{
    Tk_PathSmoothMethod *smooth = NULL;
    int b;
    char *value;
    size_t length;
    SmoothAssocData *methods;

    value = Tcl_GetString(valueObj);
    length = strlen(value);
    methods = (SmoothAssocData *) Tcl_GetAssocData(interp, "smoothPathMethod",
        NULL);

    /*
     * Not initialized yet; fix that now.
     */

    if (methods == NULL) {
    methods = InitSmoothMethods(interp);
    }

    /*
     * Backward compatability hack.
     */

    if (strncmp(value, "bezier", length) == 0) {
    smooth = &tkPathBezierSmoothMethod;
    }

    /*
     * Search the list of installed smooth methods.
     */

    while (methods != NULL) {
    if (strncmp(value, methods->smooth.name, length) == 0) {
        if (smooth != NULL) {
        Tcl_AppendResult(interp, "ambiguous smooth method \"", value,
            "\"", NULL);
        return TCL_ERROR;
        }
        smooth = &methods->smooth;
    }
    methods = methods->nextPtr;
    }
    if (smooth) {
    *smoothPtr = smooth;
    return TCL_OK;
    }

    /*
     * Did not find it. Try parsing as a boolean instead.
     */

    if (Tcl_GetBooleanFromObj(interp, valueObj, &b) != TCL_OK) {
    return TCL_ERROR;
    }
    *smoothPtr = b ? &tkPathBezierSmoothMethod : NULL;
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathSmoothOptionSetProc --
 *
 *    This function is invoked during option processing to handle "-smooth"
 *    options for canvas items.
 *
 * Results:
 *    A standard Tcl return value.
 *
 * Side effects:
 *    The smooth option for a given item gets replaced by the value
 *    indicated in the value argument.
 *
 *--------------------------------------------------------------
 */

int
TkPathSmoothOptionSetProc(
    ClientData clientData,
    Tcl_Interp *interp,        /* Current interp; may be used for errors. */
    Tk_Window tkwin,        /* Window for which option is being set. */
    Tcl_Obj **value,        /* Pointer to the pointer to the value object.
                             * We use a pointer to the pointer because
                             * we may need to return a value (NULL). */
    char *recordPtr,        /* Pointer to storage for the widget record. */
    int internalOffset,        /* Offset within *recordPtr at which the
                               internal value is to be stored. */
    char *oldInternalPtr,   /* Pointer to storage for the old value. */
    int flags)            /* Flags for the option, set Tk_SetOptions. */
{
    char *internalPtr;        /* Points to location in record where
                             * internal representation of value should
                             * be stored, or NULL. */
    Tcl_Obj *valuePtr;
    Tk_PathSmoothMethod *newPtr = NULL;

    valuePtr = *value;
    if (internalOffset >= 0) {
        internalPtr = recordPtr + internalOffset;
    } else {
        internalPtr = NULL;
    }
    if ((flags & TK_OPTION_NULL_OK) && TkPathObjectIsEmpty(valuePtr)) {
    valuePtr = NULL;
    newPtr = NULL;
    }
    if (internalPtr != NULL) {
    if (valuePtr != NULL) {
        if (FindSmoothMethod(interp, valuePtr, &newPtr) != TCL_OK) {
        return TCL_ERROR;
        }
        }
    *((Tk_PathSmoothMethod **) oldInternalPtr) = *((Tk_PathSmoothMethod **) internalPtr);
    *((Tk_PathSmoothMethod **) internalPtr) = newPtr;
    }
    return TCL_OK;
}

Tcl_Obj *
TkPathSmoothOptionGetProc(
    ClientData clientData,
    Tk_Window tkwin,
    char *recordPtr,        /* Pointer to widget record. */
    int internalOffset)        /* Offset within *recordPtr containing the
                 * value. */
{
    Tk_PathSmoothMethod *smooth;

    smooth = *((Tk_PathSmoothMethod **) (recordPtr + internalOffset));
    return (smooth) ? Tcl_NewStringObj(smooth->name, -1) : Tcl_NewBooleanObj(0);
}

void
TkPathSmoothOptionRestoreProc(
    ClientData clientData,
    Tk_Window tkwin,
    char *internalPtr,        /* Pointer to storage for value. */
    char *oldInternalPtr)    /* Pointer to old value. */
{
    *(Tk_PathSmoothMethod **)internalPtr = *(Tk_PathSmoothMethod **)oldInternalPtr;
}

/*
 *--------------------------------------------------------------
 *
 * Tk_PathCreateOutline
 *
 *    This function initializes the Tk_PathOutline structure with default
 *    values.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 *--------------------------------------------------------------
 */

void
Tk_PathCreateOutline(
    Tk_PathOutline *outline)    /* Outline structure to be filled in. */
{
    outline->gc = None;
    outline->width = 1.0;
    outline->activeWidth = 0.0;
    outline->disabledWidth = 0.0;
    outline->offset = 0;
    outline->dashPtr = NULL;
    outline->activeDashPtr = NULL;
    outline->disabledDashPtr = NULL;
    outline->tsoffsetPtr = NULL;
    outline->color = NULL;
    outline->activeColor = NULL;
    outline->disabledColor = NULL;
    outline->stipple = None;
    outline->activeStipple = None;
    outline->disabledStipple = None;
}

/*
 *--------------------------------------------------------------
 *
 * Tk_PathDeleteOutline
 *
 *    This function frees all memory that might be allocated and referenced
 *    in the Tk_PathOutline structure.
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 *--------------------------------------------------------------
 */

/* @@@ I don't belive this should ever be called since the memory is handled by Option! */

void
Tk_PathDeleteOutline(
    Display *display,        /* Display containing window. */
    Tk_PathOutline *outline)
{
    if (outline->gc != None) {
    Tk_FreeGC(display, outline->gc);
        outline->gc = None;
    }
    if (outline->color != NULL) {
    Tk_FreeColor(outline->color);
        outline->color = NULL;
    }
    if (outline->activeColor != NULL) {
    Tk_FreeColor(outline->activeColor);
        outline->activeColor = NULL;
    }
    if (outline->disabledColor != NULL) {
    Tk_FreeColor(outline->disabledColor);
        outline->disabledColor = NULL;
    }
    if (outline->stipple != None) {
    Tk_FreeBitmap(display, outline->stipple);
        outline->stipple = None;
    }
    if (outline->activeStipple != None) {
    Tk_FreeBitmap(display, outline->activeStipple);
        outline->activeStipple = None;
    }
    if (outline->disabledStipple != None) {
    Tk_FreeBitmap(display, outline->disabledStipple);
        outline->disabledStipple = None;
    }
}

/*
 *--------------------------------------------------------------
 *
 * Tk_PathConfigOutlineGC
 *
 *    This function should be called in the canvas object during the
 *    configure command. The graphics context description in gcValues is
 *    updated according to the information in the dash structure, as far as
 *    possible.
 *
 * Results:
 *    The return-value is a mask, indicating which elements of gcValues have
 *    been updated. 0 means there is no outline.
 *
 * Side effects:
 *    GC information in gcValues is updated.
 *
 *--------------------------------------------------------------
 */

int
Tk_PathConfigOutlineGC(
    XGCValues *gcValues,
    Tk_PathCanvas canvas,
    Tk_PathItem *item,
    Tk_PathOutline *outline)
{
    int mask = 0;
    double width;
    Tk_Dash *dashPtr;
    XColor *color;
    Pixmap stipple;
    Tk_PathState state = item->state;

    if (outline->width < 0.0) {
    outline->width = 0.0;
    }
    if (outline->activeWidth < 0.0) {
    outline->activeWidth = 0.0;
    }
    if (outline->disabledWidth < 0) {
    outline->disabledWidth = 0.0;
    }
    if (state == TK_PATHSTATE_HIDDEN) {
    return 0;
    }

    width = outline->width;
    if (width < 1.0) {
    width = 1.0;
    }
    dashPtr = outline->dashPtr;
    color = outline->color;
    stipple = outline->stipple;
    if (state == TK_PATHSTATE_NULL) {
    state = TkPathCanvasState(canvas);
    }
    if (((TkPathCanvas *)canvas)->currentItemPtr == item) {
    if (outline->activeWidth>width) {
        width = outline->activeWidth;
    }
    if (outline->activeDashPtr != NULL) {
        dashPtr = outline->activeDashPtr;
    }
    if (outline->activeColor!=NULL) {
        color = outline->activeColor;
    }
    if (outline->activeStipple!=None) {
        stipple = outline->activeStipple;
    }
    } else if (state == TK_PATHSTATE_DISABLED) {
    if (outline->disabledWidth>0) {
        width = outline->disabledWidth;
    }
    if (outline->disabledDashPtr != NULL) {
        dashPtr = outline->disabledDashPtr;
    }
    if (outline->disabledColor!=NULL) {
        color = outline->disabledColor;
    }
    if (outline->disabledStipple!=None) {
        stipple = outline->disabledStipple;
    }
    }

    if (color==NULL) {
    return 0;
    }

    gcValues->line_width = (int) (width + 0.5);
    if (color != NULL) {
    gcValues->foreground = color->pixel;
    mask = GCForeground|GCLineWidth;
    if (stipple != None) {
        gcValues->stipple = stipple;
        gcValues->fill_style = FillStippled;
        mask |= GCStipple|GCFillStyle;
    }
    }
    if (mask && (dashPtr != NULL)) {
    gcValues->line_style = LineOnOffDash;
    gcValues->dash_offset = outline->offset;
    if (dashPtr->number >= 2) {
        gcValues->dashes = 4;
    } else if (dashPtr->number > 0) {
        gcValues->dashes = dashPtr->pattern.array[0];
    } else {
        gcValues->dashes = (char) (4 * width);
    }
    mask |= GCLineStyle|GCDashList|GCDashOffset;
    }
    return mask;
}

/*
 *--------------------------------------------------------------
 *
 * Tk_PathChangeOutlineGC
 *
 *    Updates the GC to represent the full information of the dash
 *    structure. Partly this is already done in Tk_PathConfigOutlineGC(). This
 *    function should be called just before drawing the dashed item.
 *
 * Results:
 *    1 if there is a stipple pattern, and 0 otherwise.
 *
 * Side effects:
 *    GC is updated.
 *
 *--------------------------------------------------------------
 */

int
Tk_PathChangeOutlineGC(
    Tk_PathCanvas canvas,
    Tk_PathItem *item,
    Tk_PathOutline *outline)
{
    const char *p;
    double width;
    Tk_Dash *dashPtr;
    XColor *color;
    Pixmap stipple;
    Tk_PathState state = item->state;

    width = outline->width;
    if (width < 1.0) {
    width = 1.0;
    }
    dashPtr = outline->dashPtr;
    color = outline->color;
    stipple = outline->stipple;
    if (state == TK_PATHSTATE_NULL) {
    state = TkPathCanvasState(canvas);
    }
    if (((TkPathCanvas *)canvas)->currentItemPtr == item) {
    if (outline->activeWidth > width) {
        width = outline->activeWidth;
    }
    if (outline->activeDashPtr != NULL) {
        dashPtr = outline->activeDashPtr;
    }
    if (outline->activeColor != NULL) {
        color = outline->activeColor;
    }
    if (outline->activeStipple != None) {
        stipple = outline->activeStipple;
    }
    } else if (state == TK_PATHSTATE_DISABLED) {
    if (outline->disabledWidth > width) {
        width = outline->disabledWidth;
    }
    if (outline->disabledDashPtr != NULL) {
        dashPtr = outline->disabledDashPtr;
    }
    if (outline->disabledColor != NULL) {
        color = outline->disabledColor;
    }
    if (outline->disabledStipple != None) {
        stipple = outline->disabledStipple;
    }
    }
    if (color == NULL) {
    return 0;
    }
    if (dashPtr != NULL) {
    if ((dashPtr->number <- 1) ||
        ((dashPtr->number == -1) && (dashPtr->pattern.array[1] != ','))) {
        char *q;
        int i = -dashPtr->number;

        p = (i > (int)sizeof(char *)) ? dashPtr->pattern.pt : dashPtr->pattern.array;
        q = (char *) ckalloc(2*(unsigned int)i);
        i = DashConvert(q, p, i, width);
        XSetDashes(((TkPathCanvas *)canvas)->display, outline->gc,
            outline->offset, q, i);
        ckfree(q);
    } else if (dashPtr->number > 2 || (dashPtr->number == 2 &&
        (dashPtr->pattern.array[0] != dashPtr->pattern.array[1]))) {
        p = (dashPtr->number > (int)sizeof(char *))
            ? dashPtr->pattern.pt : dashPtr->pattern.array;
        XSetDashes(((TkPathCanvas *)canvas)->display, outline->gc,
            outline->offset, p, dashPtr->number);
    }
    }
    if (stipple != None) {
    int w=0; int h=0;
    Tk_TSOffset *tsoffset = outline->tsoffsetPtr;
    int flags = tsoffset->flags;
    if (!(flags & TK_OFFSET_INDEX) &&
        (flags & (TK_OFFSET_CENTER|TK_OFFSET_MIDDLE))) {
        Tk_SizeOfBitmap(((TkPathCanvas *)canvas)->display, stipple, &w, &h);
        if (flags & TK_OFFSET_CENTER) {
        w /= 2;
        } else {
        w = 0;
        }
        if (flags & TK_OFFSET_MIDDLE) {
        h /= 2;
        } else {
        h = 0;
        }
    }
    tsoffset->xoffset -= w;
    tsoffset->yoffset -= h;
    Tk_PathCanvasSetOffset(canvas, outline->gc, tsoffset);
    tsoffset->xoffset += w;
    tsoffset->yoffset += h;
    return 1;
    }
    return 0;
}


/*
 *--------------------------------------------------------------
 *
 * Tk_PathResetOutlineGC
 *
 *    Restores the GC to the situation before Tk_ChangeDashGC() was called.
 *    This function should be called just after the dashed item is drawn,
 *    because the GC is supposed to be read-only.
 *
 * Results:
 *    1 if there is a stipple pattern, and 0 otherwise.
 *
 * Side effects:
 *    GC is updated.
 *
 *--------------------------------------------------------------
 */

int
Tk_PathResetOutlineGC(
    Tk_PathCanvas canvas,
    Tk_PathItem *item,
    Tk_PathOutline *outline)
{
    char dashList;
    double width;
    Tk_Dash *dashPtr;
    XColor *color;
    Pixmap stipple;
    Tk_PathState state = item->state;

    width = outline->width;
    if (width < 1.0) {
    width = 1.0;
    }
    dashPtr = outline->dashPtr;
    color = outline->color;
    stipple = outline->stipple;
    if (state == TK_PATHSTATE_NULL) {
    state = TkPathCanvasState(canvas);
    }
    if (((TkPathCanvas *)canvas)->currentItemPtr == item) {
    if (outline->activeWidth > width) {
        width = outline->activeWidth;
    }
    if (outline->activeDashPtr != NULL) {
        dashPtr = outline->activeDashPtr;
    }
    if (outline->activeColor != NULL) {
        color = outline->activeColor;
    }
    if (outline->activeStipple != None) {
        stipple = outline->activeStipple;
    }
    } else if (state == TK_PATHSTATE_DISABLED) {
    if (outline->disabledWidth > width) {
        width = outline->disabledWidth;
    }
    if (outline->disabledDashPtr != NULL) {
        dashPtr = outline->disabledDashPtr;
    }
    if (outline->disabledColor != NULL) {
        color = outline->disabledColor;
    }
    if (outline->disabledStipple != None) {
        stipple = outline->disabledStipple;
    }
    }
    if (color == NULL) {
    return 0;
    }

    if (dashPtr != NULL) {
    if ((dashPtr->number > 2) || (dashPtr->number < -1) || (dashPtr->number == 2 &&
        (dashPtr->pattern.array[0] != dashPtr->pattern.array[1])) ||
        ((dashPtr->number == -1) && (dashPtr->pattern.array[1] != ','))) {
        if (dashPtr->number < 0) {
        dashList = (int) (4 * width + 0.5);
        } else if (dashPtr->number < 3) {
        dashList = dashPtr->pattern.array[0];
        } else {
        dashList = 4;
        }
        XSetDashes(((TkPathCanvas *)canvas)->display, outline->gc,
            outline->offset, &dashList , 1);
    }
    }
    if (stipple != None) {
    XSetTSOrigin(((TkPathCanvas *)canvas)->display, outline->gc, 0, 0);
    return 1;
    }
    return 0;
}


/*
 *--------------------------------------------------------------
 *
 * DashConvert
 *
 *    Converts a character-like dash-list (e.g. "-..") into an X11-style. l
 *    must point to a string that holds room to at least 2*n characters. If
 *    l == NULL, this function can be used for syntax checking only.
 *
 * Results:
 *    The length of the resulting X11 compatible dash-list. -1 if failed.
 *
 * Side effects:
 *    None
 *
 *--------------------------------------------------------------
 */

static int
DashConvert(
    char *l,            /* Must be at least 2*n chars long, or NULL to
                 * indicate "just check syntax". */
    const char *p,        /* String to parse. */
    int n,            /* Length of string to parse, or -1 to
                 * indicate that strlen() should be used. */
    double width)        /* Width of line. */
{
    int result = 0;
    int size, intWidth;

    if (n<0) {
    n = (int) strlen(p);
    }
    intWidth = (int) (width + 0.5);
    if (intWidth < 1) {
    intWidth = 1;
    }
    while (n-- && *p) {
    switch (*p++) {
    case ' ':
        if (result) {
        if (l) {
            l[-1] += intWidth + 1;
        }
        continue;
        }
        return 0;
    case '_':
        size = 8;
        break;
    case '-':
        size = 6;
        break;
    case ',':
        size = 4;
        break;
    case '.':
        size = 2;
        break;
    default:
        return -1;
    }
    if (l) {
        *l++ = size * intWidth;
        *l++ = 4 * intWidth;
    }
    result += 2;
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TranslateAndAppendCoords --
 *
 *    This is a helper routine for TkPathCanvTranslatePath() below.
 *
 *    Given an (x,y) coordinate pair within a canvas, this function computes
 *    the corresponding coordinates at which the point should be drawn in
 *    the drawable used for display. Those coordinates are then written into
 *    outArr[numOut*2] and outArr[numOut*2+1].
 *
 * Results:
 *    There is no return value.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static void
TranslateAndAppendCoords(
    TkPathCanvas *canvPtr,        /* The canvas. */
    double x,            /* Coordinates in canvas space. */
    double y,
    XPoint *outArr,        /* Write results into this array */
    int numOut)            /* Num of prior entries in outArr[] */
{
    double tmp;

    tmp = x - canvPtr->drawableXOrigin;
    if (tmp > 0) {
    tmp += 0.5;
    } else {
    tmp -= 0.5;
    }
    outArr[numOut].x = (short) tmp;

    tmp = y - canvPtr->drawableYOrigin;
    if (tmp > 0) {
    tmp += 0.5;
    } else {
    tmp -= 0.5;
    }
    outArr[numOut].y = (short) tmp;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathCanvTranslatePath
 *
 *    Translate a line or polygon path so that all vertices are within a
 *    rectangle that is 1000 pixels larger than the total size of the canvas
 *    window. This will prevent pixel coordinates from overflowing the
 *    16-bit integer size limitation imposed by most windowing systems.
 *
 *    coordPtr must point to an array of doubles, two doubles per vertex.
 *    There are a total of numVertex vertices, or 2*numVertex entries in
 *    coordPtr. The result vertices written into outArr have their
 *    coordinate origin shifted to canvPtr->drawableXOrigin by
 *    canvPtr->drawableYOrigin. There might be as many as 3 times more
 *    output vertices than there are input vertices. The calling function
 *    should allocate space accordingly.
 *
 *    This routine limits the width and height of a canvas window to 31767
 *    pixels. At the highest resolution display devices available today (210
 *    ppi in Jan 2003) that's a window that is over 13 feet wide and tall.
 *    Should be enough for the near future.
 *
 * Results:
 *    Clipped and translated path vertices are written into outArr[]. There
 *    might be as many as twice the vertices in outArr[] as there are in
 *    coordPtr[]. The return value is the number of vertices actually
 *    written into outArr[].
 *
 * Side effects:
 *    None
 *
 *--------------------------------------------------------------
 */

int
TkPathCanvTranslatePath(
    TkPathCanvas *canvPtr,        /* The canvas */
    int numVertex,        /* Number of vertices specified by
                 * coordArr[] */
    double *coordArr,        /* X and Y coordinates for each vertex */
    int closedPath,        /* True if this is a closed polygon */
    XPoint *outArr)        /* Write results here, if not NULL */
{
    int numOutput = 0;        /* Number of output coordinates */
    double lft, rgh;        /* Left and right sides of the bounding box */
    double top, btm;        /* Top and bottom sizes of the bounding box */
    double *tempArr;        /* Temporary storage used by the clipper */
    double *a, *b, *t;        /* Pointers to parts of the temporary
                 * storage */
    int i, j;            /* Loop counters */
    int maxOutput;        /* Maximum number of outputs that we will
                 * allow */
    double limit[4];        /* Boundries at which clipping occurs */
    double staticSpace[480];    /* Temp space from the stack */

    /*
     * Constrain all vertices of the path to be within a box that is no larger
     * than 32000 pixels wide or height. The top-left corner of this clipping
     * box is 1000 pixels above and to the left of the top left corner of the
     * window on which the canvas is displayed.
     *
     * This means that a canvas will not display properly on a canvas window
     * that is larger than 31000 pixels wide or high. That is not a problem
     * today, but might someday become a factor for ultra-high resolutions
     * displays.
     *
     * The X11 protocol allows us (in theory) to expand the size of the
     * clipping box to 32767 pixels. But we have found experimentally that
     * XFree86 sometimes fails to draw lines correctly if they are longer than
     * about 32500 pixels. So we have left a little margin in the size to mask
     * that bug.
     */

    lft = canvPtr->xOrigin - 1000.0;
    top = canvPtr->yOrigin - 1000.0;
    rgh = lft + 32000.0;
    btm = top + 32000.0;

    /*
     * Try the common case first - no clipping. Loop over the input
     * coordinates and translate them into appropriate output coordinates.
     * But if a vertex outside of the bounding box is seen, break out of the
     * loop.
     *
     * Most of the time, no clipping is needed, so this one loop is sufficient
     * to do the translation.
     */

    for (i=0; i<numVertex; i++){
    double x, y;

    x = coordArr[i*2];
    y = coordArr[i*2+1];
    if (x<lft || x>rgh || y<top || y>btm) {
        break;
    }
    TranslateAndAppendCoords(canvPtr, x, y, outArr, numOutput++);
    }
    if (i == numVertex){
    assert(numOutput == numVertex);
    return numOutput;
    }

    /*
     * If we reach this point, it means that some clipping is required. Begin
     * by allocating some working storage - at least 6 times as much space as
     * coordArr[] requires. Divide this space into two separate arrays a[] and
     * b[]. Initialize a[] to be equal to coordArr[].
     */

    if (numVertex*12 <= (int)(sizeof(staticSpace)/sizeof(staticSpace[0]))) {
    tempArr = staticSpace;
    } else {
    tempArr = (double *)ckalloc(numVertex*12*sizeof(tempArr[0]));
    }
    for (i=0; i<numVertex*2; i++){
    tempArr[i] = coordArr[i];
    }
    a = tempArr;
    b = &tempArr[numVertex*6];

    /*
     * We will make four passes through the input data. On each pass, we copy
     * the contents of a[] over into b[]. As we copy, we clip any line
     * segments that extend to the right past xClip then we rotate the
     * coordinate system 90 degrees clockwise. After each pass is complete, we
     * interchange a[] and b[] in preparation for the next pass.
     *
     * Each pass clips line segments that extend beyond a single side of the
     * bounding box, and four passes rotate the coordinate system back to its
     * original value. I'm not an expert on graphics algorithms, but I think
     * this is called Cohen-Sutherland polygon clipping.
     *
     * The limit[] array contains the xClip value used for each of the four
     * passes.
     */

    limit[0] = rgh;
    limit[1] = -top;
    limit[2] = -lft;
    limit[3] = btm;

    /*
     * This is the loop that makes the four passes through the data.
     */

    maxOutput = numVertex*3;
    for (j=0; j<4; j++){
    double xClip = limit[j];
    int inside = a[0]<xClip;
    double priorY = a[1];
    numOutput = 0;

    /*
     * Clip everything to the right of xClip. Store the results in b[]
     * rotated by 90 degrees clockwise.
     */

    for (i=0; i<numVertex; i++){
        double x = a[i*2];
        double y = a[i*2+1];

        if (x >= xClip) {
        /*
         * The current vertex is to the right of xClip.
         */

        if (inside) {
            /*
             * If the current vertex is to the right of xClip but the
             * previous vertex was left of xClip, then draw a line
             * segment from the previous vertex to until it intersects
             * the vertical at xClip.
             */

            double x0, y0, yN;

            assert(i > 0);
            x0 = a[i*2-2];
            y0 = a[i*2-1];
            yN = y0 + (y - y0)*(xClip-x0)/(x-x0);
            b[numOutput*2] = -yN;
            b[numOutput*2+1] = xClip;
            numOutput++;
            if (numOutput > maxOutput) assert(0);
            priorY = yN;
            inside = 0;
        } else if (i == 0) {
            /*
             * If the first vertex is to the right of xClip, add a
             * vertex that is the projection of the first vertex onto
             * the vertical xClip line.
             */

            b[0] = -y;
            b[1] = xClip;
            numOutput = 1;
            priorY = y;
        }
        } else {
        /*
         * The current vertex is to the left of xClip
         */
        if (!inside) {
            /* If the current vertex is on the left of xClip and one
             * or more prior vertices where to the right, then we have
             * to draw a line segment along xClip that extends from
             * the spot where we first crossed from left to right to
             * the spot where we cross back from right to left.
             */

            double x0, y0, yN;

            assert(i > 0);
            x0 = a[i*2-2];
            y0 = a[i*2-1];
            yN = y0 + (y - y0)*(xClip-x0)/(x-x0);
            if (yN != priorY) {
            b[numOutput*2] = -yN;
            b[numOutput*2+1] = xClip;
            numOutput++;
            assert(numOutput <= maxOutput);
            }
            inside = 1;
        }
        b[numOutput*2] = -y;
        b[numOutput*2+1] = x;
        numOutput++;
        assert(numOutput <= maxOutput);
        }
    }

    /*
     * Interchange a[] and b[] in preparation for the next pass.
     */

    t = a;
    a = b;
    b = t;
    numVertex = numOutput;
    }

    /*
     * All clipping is now finished. Convert the coordinates from doubles into
     * XPoints and translate the origin for the drawable.
     */

    for (i=0; i<numVertex; i++){
    TranslateAndAppendCoords(canvPtr, a[i*2], a[i*2+1], outArr, i);
    }
    if (tempArr != staticSpace) {
    ckfree((char *) tempArr);
    }
    return numOutput;
}


/*
 *--------------------------------------------------------------
 *
 * TkPathCoordsForPointItems --
 *
 *    Used as coordProc for items that have plain single point coords.
 *
 * Results:
 *    Standard tcl result.
 *
 * Side effects:
 *    May store new coords in rectPtr.
 *
 *--------------------------------------------------------------
 */

int
TkPathCoordsForPointItems(
        Tcl_Interp *interp,
        Tk_PathCanvas canvas,
        double *pointPtr,         /* Sets or gets the point here. */
        int objc,
        Tcl_Obj *const objv[])
{
    if (objc == 0) {
        Tcl_Obj *obj = Tcl_NewObj();
        Tcl_Obj *subobj = Tcl_NewDoubleObj(pointPtr[0]);
        Tcl_ListObjAppendElement(interp, obj, subobj);
        subobj = Tcl_NewDoubleObj(pointPtr[1]);
        Tcl_ListObjAppendElement(interp, obj, subobj);
        Tcl_SetObjResult(interp, obj);
    } else if ((objc == 1) || (objc == 2)) {
        double x, y;

        if (objc==1) {
            if (Tcl_ListObjGetElements(interp, objv[0], &objc,
                    (Tcl_Obj ***) &objv) != TCL_OK) {
                return TCL_ERROR;
            } else if (objc != 2) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("wrong # coordinates: expected 0 or 2", -1));
                return TCL_ERROR;
            }
        }
        if ((Tk_PathCanvasGetCoordFromObj(interp, canvas, objv[0], &x) != TCL_OK)
            || (Tk_PathCanvasGetCoordFromObj(interp, canvas, objv[1], &y) != TCL_OK)) {
            return TCL_ERROR;
        }
        pointPtr[0] = x;
        pointPtr[1] = y;
    } else {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("wrong # coordinates: expected 0 or 2", -1));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathCoordsForRectangularItems --
 *
 *    Used as coordProc for items that have rectangular coords.
 *
 * Results:
 *    Standard tcl result.
 *
 * Side effects:
 *    May store new coords in rectPtr.
 *
 *--------------------------------------------------------------
 */

int
TkPathCoordsForRectangularItems(
        Tcl_Interp *interp,
        Tk_PathCanvas canvas,
        TkPathRect *rectPtr,         /* Sets or gets the box here. */
        int objc,
        Tcl_Obj *const objv[])
{
    if (objc == 0) {
        Tcl_Obj *obj = Tcl_NewObj();
        Tcl_Obj *subobj = Tcl_NewDoubleObj(rectPtr->x1);
        Tcl_ListObjAppendElement(interp, obj, subobj);
        subobj = Tcl_NewDoubleObj(rectPtr->y1);
        Tcl_ListObjAppendElement(interp, obj, subobj);
        subobj = Tcl_NewDoubleObj(rectPtr->x2);
        Tcl_ListObjAppendElement(interp, obj, subobj);
        subobj = Tcl_NewDoubleObj(rectPtr->y2);
        Tcl_ListObjAppendElement(interp, obj, subobj);
        Tcl_SetObjResult(interp, obj);
    } else if ((objc == 1) || (objc == 4)) {
        double x1, y1, x2, y2;

        if (objc==1) {
            if (Tcl_ListObjGetElements(interp, objv[0], &objc,
                    (Tcl_Obj ***) &objv) != TCL_OK) {
                return TCL_ERROR;
            } else if (objc != 4) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("wrong # coordinates: expected 0 or 4", -1));
                return TCL_ERROR;
            }
        }
        if ((Tk_PathCanvasGetCoordFromObj(interp, canvas, objv[0], &x1) != TCL_OK)
            || (Tk_PathCanvasGetCoordFromObj(interp, canvas, objv[1], &y1) != TCL_OK)
            || (Tk_PathCanvasGetCoordFromObj(interp, canvas, objv[2], &x2) != TCL_OK)
            || (Tk_PathCanvasGetCoordFromObj(interp, canvas, objv[3], &y2) != TCL_OK)) {
            return TCL_ERROR;
        }

        /*
         * Get an approximation of the path's bounding box
         * assuming zero width outline (stroke).
         * Normalize the corners!
         */
        rectPtr->x1 = MIN(x1, x2);
        rectPtr->y1 = MIN(y1, y2);
        rectPtr->x2 = MAX(x1, x2);
        rectPtr->y2 = MAX(y1, y2);
    } else {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("wrong # coordinates: expected 0 or 4", -1));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * GetBareArcBbox
 *
 *    Gets an overestimate of the bounding box rectangle of
 *     an arc defined using central parametrization assuming
 *    zero stroke width.
 *     Untransformed coordinates!
 *    Note: 1) all angles clockwise direction!
 *          2) all angles in radians.
 *
 * Results:
 *    A PathRect.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

static TkPathRect
GetBareArcBbox(double cx, double cy, double rx, double ry,
        double theta1, double dtheta, double phi)
{
    TkPathRect r = {1.0e36, 1.0e36, -1.0e36, -1.0e36};    /* Empty rect. */
    double start, extent, stop, stop2PI;
    double cosStart, sinStart, cosStop, sinStop;

    /* Keep 0 <= start, extent < 2pi
     * and 0 <= stop < 4pi */
    if (dtheta >= 0.0) {
        start = theta1;
        extent = dtheta;
    } else {
        start = theta1 + dtheta;
        extent = -1.0*dtheta;
    }
    if (start < 0.0) {
        start += 2.0*M_PI;
        if (start < 0.0) {
            start += 2.0*M_PI;
        }
    }
    if (start >= 2.0*M_PI) {
        start -= 2.0*M_PI;
    }
    stop = start + extent;
    stop2PI = stop - 2.0*M_PI;
    cosStart = cos(start);
    sinStart = sin(start);
    cosStop = cos(stop);
    sinStop = sin(stop);

    /*
     * Compute bbox for phi = 0.
     * Put everything at (0,0) and shift to (cx,cy) at the end.
     * Look for extreme points of arc:
     *     1) start and stop points
     *    2) any intersections of x and y axes
     * Count both first and second "turns".
     */

    TkPathIncludePointInRect(&r, rx*cosStart, ry*sinStart);
    TkPathIncludePointInRect(&r, rx*cosStop,  ry*sinStop);
    if (((start < M_PI/2.0) && (stop > M_PI/2.0)) || (stop2PI > M_PI/2.0)) {
        TkPathIncludePointInRect(&r, 0.0, ry);
    }
    if (((start < M_PI) && (stop > M_PI)) || (stop2PI > M_PI)) {
        TkPathIncludePointInRect(&r, -rx, 0.0);
    }
    if (((start < 3.0*M_PI/2.0) && (stop > 3.0*M_PI/2.0)) || (stop2PI > 3.0*M_PI/2.0)) {
        TkPathIncludePointInRect(&r, 0.0, -ry);
    }
    if (stop > 2.0*M_PI) {
        TkPathIncludePointInRect(&r, rx, 0.0);
    }

    /*
     * Rotate the bbox above to get an overestimate of extremas.
     */
    if (fabs(phi) > 1e-6) {
        double cosPhi, sinPhi;
        double x, y;
        TkPathRect rrot = {1.0e36, 1.0e36, -1.0e36, -1.0e36};

        cosPhi = cos(phi);
        sinPhi = sin(phi);
        x = r.x1*cosPhi - r.y1*sinPhi;
        y = r.x1*sinPhi + r.y1*cosPhi;
        TkPathIncludePointInRect(&rrot, x, y);

        x = r.x2*cosPhi - r.y1*sinPhi;
        y = r.x2*sinPhi + r.y1*cosPhi;
        TkPathIncludePointInRect(&rrot, x, y);

        x = r.x1*cosPhi - r.y2*sinPhi;
        y = r.x1*sinPhi + r.y2*cosPhi;
        TkPathIncludePointInRect(&rrot, x, y);

        x = r.x2*cosPhi - r.y2*sinPhi;
        y = r.x2*sinPhi + r.y2*cosPhi;
        TkPathIncludePointInRect(&rrot, x, y);

        r = rrot;
    }

    /* Shift rect to arc center. */
    r.x1 += cx;
    r.y1 += cy;
    r.x2 += cx;
    r.y2 += cy;
    return r;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathGetGenericBarePathBbox
 *
 *    Gets an overestimate of the bounding box rectangle of
 *     a path assuming zero stroke width.
 *     Untransformed coordinates!
 *
 * Results:
 *    A TkPathRect.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

TkPathRect
TkPathGetGenericBarePathBbox(TkPathAtom *atomPtr)
{
    double x1, y1, x2, y2, x3, y3, x4, y4, x5, y5;
    double currentX, currentY;
    TkPathRect r = {1.0e36, 1.0e36, -1.0e36, -1.0e36};

    currentX = 0.0;
    currentY = 0.0;

    while (atomPtr != NULL) {

        switch (atomPtr->type) {
            case TK_PATH_ATOM_M: {
                TkMoveToAtom *move = (TkMoveToAtom *) atomPtr;

                TkPathIncludePointInRect(&r, move->x, move->y);
                currentX = move->x;
                currentY = move->y;
                break;
            }
            case TK_PATH_ATOM_L: {
                TkLineToAtom *line = (TkLineToAtom *) atomPtr;

                TkPathIncludePointInRect(&r, line->x, line->y);
                currentX = line->x;
                currentY = line->y;
                break;
            }
            case TK_PATH_ATOM_A: {
                TkArcAtom *arc = (TkArcAtom *) atomPtr;
                int result;
                double cx, cy, rx, ry;
                double theta1, dtheta;

                result = TkPathEndpointToCentralArcParameters(
                        currentX, currentY,
                        arc->x, arc->y, arc->radX, arc->radY,
                        DEGREES_TO_RADIANS * arc->angle,
                        arc->largeArcFlag, arc->sweepFlag,
                        &cx, &cy, &rx, &ry,
                        &theta1, &dtheta);
                if (result == TK_PATH_ARC_Line) {
                    TkPathIncludePointInRect(&r, arc->x, arc->y);
                } else if (result == TK_PATH_ARC_OK) {
                    TkPathRect arcRect;

                    arcRect = GetBareArcBbox(cx, cy, rx, ry, theta1, dtheta,
                            DEGREES_TO_RADIANS * arc->angle);
                    TkPathIncludePointInRect(&r, arcRect.x1, arcRect.y1);
                    TkPathIncludePointInRect(&r, arcRect.x2, arcRect.y2);
                }
                currentX = arc->x;
                currentY = arc->y;
                break;
            }
            case TK_PATH_ATOM_Q: {
                TkQuadBezierAtom *quad = (TkQuadBezierAtom *) atomPtr;

                x1 = (currentX + quad->ctrlX)/2.0;
                y1 = (currentY + quad->ctrlY)/2.0;
                x2 = (quad->ctrlX + quad->anchorX)/2.0;
                y2 = (quad->ctrlY + quad->anchorY)/2.0;
                TkPathIncludePointInRect(&r, x1, y1);
                TkPathIncludePointInRect(&r, x2, y2);
                currentX = quad->anchorX;
                currentY = quad->anchorY;
                TkPathIncludePointInRect(&r, currentX, currentY);
                break;
            }
            case TK_PATH_ATOM_C: {
                TkCurveToAtom *curve = (TkCurveToAtom *) atomPtr;

                x1 = (currentX + curve->ctrlX1)/2.0;
                y1 = (currentY + curve->ctrlY1)/2.0;
                x2 = (curve->ctrlX1 + curve->ctrlX2)/2.0;
                y2 = (curve->ctrlY1 + curve->ctrlY2)/2.0;
                x3 = (curve->ctrlX2 + curve->anchorX)/2.0;
                y3 = (curve->ctrlY2 + curve->anchorY)/2.0;
                TkPathIncludePointInRect(&r, x1, y1);
                TkPathIncludePointInRect(&r, x3, y3);
                x4 = (x1 + x2)/2.0;
                y4 = (y1 + y2)/2.0;
                x5 = (x2 + x3)/2.0;
                y5 = (y2 + y3)/2.0;
                TkPathIncludePointInRect(&r, x4, y4);
                TkPathIncludePointInRect(&r, x5, y5);
                currentX = curve->anchorX;
                currentY = curve->anchorY;
                TkPathIncludePointInRect(&r, currentX, currentY);
                break;
            }
            case TK_PATH_ATOM_Z: {
                /* empty */
                break;
            }
            case TK_PATH_ATOM_ELLIPSE: {
                TkEllipseAtom *ell = (TkEllipseAtom *) atomPtr;
                TkPathIncludePointInRect(&r, ell->cx - ell->rx, ell->cy - ell->ry);
                TkPathIncludePointInRect(&r, ell->cx + ell->rx, ell->cy + ell->ry);
                break;
            }
            case TK_PATH_ATOM_RECT: {
                TkRectAtom *rect = (TkRectAtom *) atomPtr;
                TkPathIncludePointInRect(&r, rect->x, rect->y);
                TkPathIncludePointInRect(&r, rect->x + rect->width, rect->y + rect->height);
                break;
            }
        }
        atomPtr = atomPtr->nextPtr;
    }
    return r;
}

static void
CopyPoint(double ptSrc[2], double ptDst[2])
{
    ptDst[0] = ptSrc[0];
    ptDst[1] = ptSrc[1];
}

/*
 *--------------------------------------------------------------
 *
 * PathGetMiterPoint --
 *
 *    Given three points forming an angle, compute the
 *    coordinates of the outside point of the mitered corner
 *    formed by a line of a given width at that angle.
 *
 * Results:
 *    If the angle formed by the three points is less than
 *    11 degrees then 0 is returned and m isn't modified.
 *    Otherwise 1 is returned and the point of the "sharp"
 *    edge is returned.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

static int
PathGetMiterPoint(
    double p1[],    /* Points to x- and y-coordinates of point
                         * before vertex. */
    double p0[],    /* Points to x- and y-coordinates of vertex
                         * for mitered joint. */
    double p2[],    /* Points to x- and y-coordinates of point
                         * after vertex. */
    double width,    /* Width of line.  */
    double sinThetaLimit,/* Sinus of theta miter limit. */
    double m[])        /* The miter point; the sharp edge. */
{
    double n1[2], n2[2];    /* The normalized vectors. */
    double len1, len2;
    double sinTheta;

    /*
     * A little geometry:
     *          p0
     *          /\
     *     n1  /  \ n2
     *        /    \
     *       p1    p2
     *
     * n1 = (p0-p1)/|p0-p1|
     * n2 = (p0-p2)/|p0-p2|
     *
     * theta is the angle between n1 and n2 which is identical
     * to the angle of the corner. We keep 0 <= theta <= PI so
     * that sin(theta) is never negative. If you consider the triangle
     * formed by the bisection (mid line) and any of the lines,
     * then theta/2 is the angle of that triangle.
     * Define:
     *
     * n = (n1+n2)/|n1+n2|
     *
     * Simple geometry gives:
     *
     * |n1+n2| = 2cos(theta/2)
     *
     * and similar if d is the distance from p0 to the miter point:
     *
     * d = (w/2)/(sin(theta/2)
     *
     * where w is the line width.
     * For the miter point p we then get:
     *                   n1+n2            w/2
     * p = p0 + n*d = ------------- . ------------
     *                2cos(theta/2)   sin(theta/2)
     *
     * Using sin(2a) = 2sin(a)cos(a) we get:
     *
     * p = p0 + w/(2sin(theta)) * (n1 + n2)
     *
     * Use the cross product to get theta as: a x b = |a| |b| sin(angle) as:
     *
     * sin(theta) = |n1x*n2y - n1y*n2x|
     */

    /* n1 points from p1 to p0. */
    n1[0] = p0[0] - p1[0];
    n1[1] = p0[1] - p1[1];
    len1 = hypot(n1[0], n1[1]);
    if (len1 < 1e-6) {
        return 0;
    }
    n1[0] /= len1;
    n1[1] /= len1;

    /* n2 points from p2 to p0. */
    n2[0] = p0[0] - p2[0];
    n2[1] = p0[1] - p2[1];
    len2 = hypot(n2[0], n2[1]);
    if (len2 < 1e-6) {
        return 0;
    }
    n2[0] /= len2;
    n2[1] /= len2;

    sinTheta = fabs(n1[0]*n2[1] - n1[1]*n2[0]);
    if (sinTheta < sinThetaLimit) {
        return 0;
    }
    m[0] = p0[0] + width/(2.0*sinTheta) * (n1[0] + n2[0]);
    m[1] = p0[1] + width/(2.0*sinTheta) * (n1[1] + n2[1]);

    return 1;
}

static void
IncludeMiterPointsInRect(double p1[2], double p2[2], double p3[2], TkPathRect *bounds,
        double width, double sinThetaLimit)
{
    double    m[2];

    if (PathGetMiterPoint(p1, p2, p3, width, sinThetaLimit, m)) {
        TkPathIncludePointInRect(bounds, m[0], m[1]);
    }
}

static TkPathRect
GetMiterBbox(TkPathAtom *atomPtr, double width, double miterLimit)
{
    int        npts;
    double     p1[2], p2[2], p3[2];
    double    current[2], second[2];
    double     sinThetaLimit;
    TkPathRect    bounds = {1.0e36, 1.0e36, -1.0e36, -1.0e36};

    npts = 0;
    current[0] = 0.0;
    current[1] = 0.0;
    second[0] = 0.0;
    second[1] = 0.0;

    /* Find sin(thetaLimit) which is needed to get miter points:
     * miterLimit = 1/sin(theta/2) =approx 2/theta
     */
    if (miterLimit > 8) {
        /* theta:
         * Exact:  0.250655662336
         * Approx: 0.25
         */
        sinThetaLimit = 2.0/miterLimit;
    } else if (miterLimit > 2) {
        sinThetaLimit = sin(2*asin(1.0/miterLimit));
    } else {
        return bounds;
    }

    while (atomPtr != NULL) {

        switch (atomPtr->type) {
            case TK_PATH_ATOM_M: {
                TkMoveToAtom *move = (TkMoveToAtom *) atomPtr;
                current[0] = move->x;
                current[1] = move->y;
                p1[0] = move->x;
                p1[1] = move->y;
                npts = 1;
                break;
            }
            case TK_PATH_ATOM_L: {
                TkLineToAtom *line = (TkLineToAtom *) atomPtr;
                current[0] = line->x;
                current[1] = line->y;
                CopyPoint(p2, p3);
                CopyPoint(p1, p2);
                p1[0] = line->x;
                p1[1] = line->y;
                npts++;
                if (npts >= 3) {
                    IncludeMiterPointsInRect(p1, p2, p3, &bounds, width, sinThetaLimit);
                }
                break;
            }
            case TK_PATH_ATOM_A: {
                TkArcAtom *arc = (TkArcAtom *) atomPtr;
                current[0] = arc->x;
                current[1] = arc->y;
                /* @@@ TODO */
                break;
            }
            case TK_PATH_ATOM_Q: {
                TkQuadBezierAtom *quad = (TkQuadBezierAtom *) atomPtr;
                current[0] = quad->anchorX;
                current[1] = quad->anchorY;
                /* The control point(s) form the tangent lines at ends. */
                CopyPoint(p2, p3);
                CopyPoint(p1, p2);
                p1[0] = quad->ctrlX;
                p1[1] = quad->ctrlY;
                npts++;
                if (npts >= 3) {
                    IncludeMiterPointsInRect(p1, p2, p3, &bounds, width, sinThetaLimit);
                }
                CopyPoint(p1, p2);
                p1[0] = quad->anchorX;
                p1[1] = quad->anchorY;
                npts += 2;
                break;
            }
            case TK_PATH_ATOM_C: {
                TkCurveToAtom *curve = (TkCurveToAtom *) atomPtr;
                current[0] = curve->anchorX;
                current[1] = curve->anchorY;
                /* The control point(s) form the tangent lines at ends. */
                CopyPoint(p2, p3);
                CopyPoint(p1, p2);
                p1[0] = curve->ctrlX1;
                p1[1] = curve->ctrlY1;
                npts++;
                if (npts >= 3) {
                    IncludeMiterPointsInRect(p1, p2, p3, &bounds, width, sinThetaLimit);
                }
                p1[0] = curve->ctrlX2;
                p1[1] = curve->ctrlY2;
                p1[0] = curve->anchorX;
                p1[1] = curve->anchorX;
                npts += 2;
                break;
            }
            case TK_PATH_ATOM_Z: {
                TkCloseAtom *close = (TkCloseAtom *) atomPtr;
                current[0] = close->x;
                current[1] = close->y;
                CopyPoint(p2, p3);
                CopyPoint(p1, p2);
                p1[0] = close->x;
                p1[1] = close->y;
                npts++;
                if (npts >= 3) {
                    IncludeMiterPointsInRect(p1, p2, p3, &bounds, width, sinThetaLimit);
                }
                /* Check also the joint of first segment with the last segment. */
                CopyPoint(p2, p3);
                CopyPoint(p1, p2);
                CopyPoint(second, p1);
                if (npts >= 3) {
                    IncludeMiterPointsInRect(p1, p2, p3, &bounds, width, sinThetaLimit);
                }
                break;
            }
            case TK_PATH_ATOM_ELLIPSE:
            case TK_PATH_ATOM_RECT: {
                /* Empty. */
                break;
            }
        }
        if (npts == 2) {
            CopyPoint(current, second);
        }
        atomPtr = atomPtr->nextPtr;
    }

    return bounds;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathGetGenericPathTotalBboxFromBare --
 *
 *    This procedure calculates the items total bbox from the
 *    bare bbox. Untransformed coords!
 *
 * Results:
 *    TkPathRect.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

TkPathRect
TkPathGetGenericPathTotalBboxFromBare(TkPathAtom *atomPtr, Tk_PathStyle *stylePtr, TkPathRect *bboxPtr)
{
    double fudge = 1.0;
    double width = 0.0;
    TkPathRect rect = *bboxPtr;

    if (stylePtr->strokeColor != NULL) {
        width = stylePtr->strokeWidth;
        if (width < 1.0) {
            width = 1.0;
        }
        rect.x1 -= width;
        rect.x2 += width;
        rect.y1 -= width;
        rect.y2 += width;
    }

    /* Add the miter corners if necessary. */
    if (atomPtr && (stylePtr->joinStyle == JoinMiter)
            && (stylePtr->strokeWidth > 1.0)) {
        TkPathRect miterBox;
        miterBox = GetMiterBbox(atomPtr, width, stylePtr->miterLimit);
        if (!IsPathRectEmpty(&miterBox)) {
            TkPathIncludePointInRect(&rect, miterBox.x1, miterBox.y1);
            TkPathIncludePointInRect(&rect, miterBox.x2, miterBox.y2);
        }
    }

    /*
     * Add one (or two if antialiasing) more pixel of fudge factor just to be safe
     * (e.g. X may round differently than we do).
     */

    if (Tk_PathAntiAlias) {
        fudge = 2;
    }
    rect.x1 -= fudge;
    rect.x2 += fudge;
    rect.y1 -= fudge;
    rect.y2 += fudge;

    return rect;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathSetGenericPathHeaderBbox --
 *
 *    This procedure sets the (transformed) bbox in the items header.
 *    It is a (too?) conservative measure.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    The fields x1, y1, x2, and y2 are updated in the header
 *    for itemPtr.
 *
 *--------------------------------------------------------------
 */

void
TkPathSetGenericPathHeaderBbox(
        Tk_PathItem *headerPtr,
        TkPathMatrix *mPtr,
        TkPathRect *totalBboxPtr)
{
    TkPathRect rect;

    rect = *totalBboxPtr;

    if (mPtr != NULL) {
        double x, y;
        TkPathRect r = TkPathNewEmptyPathRect();

        /* Take each four corners in turn. */
        x = rect.x1, y = rect.y1;
        PathApplyTMatrix(mPtr, &x, &y);
        TkPathIncludePointInRect(&r, x, y);

        x = rect.x2, y = rect.y1;
        PathApplyTMatrix(mPtr, &x, &y);
        TkPathIncludePointInRect(&r, x, y);

        x = rect.x1, y = rect.y2;
        PathApplyTMatrix(mPtr, &x, &y);
        TkPathIncludePointInRect(&r, x, y);

        x = rect.x2, y = rect.y2;
        PathApplyTMatrix(mPtr, &x, &y);
        TkPathIncludePointInRect(&r, x, y);
        rect = r;
    }
    headerPtr->x1 = (int) rect.x1;
    headerPtr->x2 = (int) rect.x2;
    headerPtr->y1 = (int) rect.y1;
    headerPtr->y2 = (int) rect.y2;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathGenericPathToPoint --
 *
 *    Computes the distance from a given point to a given
 *    line, in canvas units.
 *
 * Results:
 *    The return value is 0 if the point whose x and y coordinates
 *    are pointPtr[0] and pointPtr[1] is inside the line.  If the
 *    point isn't inside the line then the return value is the
 *    distance from the point to the line.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

double
TkPathGenericPathToPoint(
    Tk_PathCanvas canvas,    /* Canvas containing item. */
    Tk_PathItem *itemPtr,    /* Item to check against point. */
    Tk_PathStyle *stylePtr,
    TkPathAtom *atomPtr,
    int maxNumSegments,
    double *pointPtr)        /* Pointer to x and y coordinates. */
{
    int            numPoints, numStrokes;
    int            isclosed;
    int            intersections, nonzerorule;
    int            sumIntersections = 0, sumNonzerorule = 0;
    double        *polyPtr;
    double        bestDist, radius, width, dist;
    Tk_PathState    state = itemPtr->state;
    TkPathMatrix        *matrixPtr = stylePtr->matrixPtr;
    double        staticSpace[2*MAX_NUM_STATIC_SEGMENTS];

    bestDist = 1.0e36;

    if (state == TK_PATHSTATE_HIDDEN) {
        return bestDist;
    }
    if (!HaveAnyFillFromPathColor(stylePtr->fill) && (stylePtr->strokeColor == NULL)) {
        return bestDist;
    }
    if (atomPtr == NULL) {
        return bestDist;
    }

    /*
     * Do we need more memory or can we use static space?
     */
    if (maxNumSegments > MAX_NUM_STATIC_SEGMENTS) {
        polyPtr = (double *) ckalloc((unsigned) (2*maxNumSegments*sizeof(double)));
    } else {
        polyPtr = staticSpace;
    }
    width = stylePtr->strokeWidth;
    if (width < 1.0) {
        width = 1.0;
    }
    radius = width/2.0;

    /*
     * Loop through each subpath, creating the approximate polyline,
     * and do the *ToPoint functions.
     *
     * Note: Strokes can be treated independently for each subpath,
     *         but fills cannot since subpaths may intersect creating
     *         "holes".
     */

    while (atomPtr != NULL) {
        MakeSubPathSegments(&atomPtr, polyPtr, &numPoints, &numStrokes, matrixPtr);
        isclosed = 0;
        if (numStrokes == numPoints) {
            isclosed = 1;
        }

        /*
         * This gives the min distance to the *stroke* AND the
         * number of intersections of the two types.
         */
        dist = PathPolygonToPointEx(polyPtr, numPoints, pointPtr,
                &intersections, &nonzerorule);
        sumIntersections += intersections;
        sumNonzerorule += nonzerorule;
        if ((stylePtr->strokeColor != NULL) && (stylePtr->strokeWidth <= kPathStrokeThicknessLimit)) {

            /*
             * This gives the distance to a zero width polyline.
             * Use a simple scheme to adjust for a small width.
             */
            dist -= radius;
        }
        if (dist < bestDist) {
            bestDist = dist;
        }
        if (bestDist <= 0.0) {
            bestDist = 0.0;
            goto done;
        }

        /*
         * For wider strokes we must make a more detailed analysis.
         * Yes, there is an infinitesimal overlap to the above just
         * to be on the safe side.
         */
        if ((stylePtr->strokeColor != NULL) && (stylePtr->strokeWidth >= kPathStrokeThicknessLimit)) {
            dist = PathThickPolygonToPoint(stylePtr->joinStyle, stylePtr->capStyle,
                    width, isclosed, polyPtr, numPoints, pointPtr);
            if (dist < bestDist) {
                bestDist = dist;
            }
            if (bestDist <= 0.0) {
                bestDist = 0.0;
                goto done;
            }
        }
    }

    /*
     * We've processed all of the points.
     * EvenOddRule: If the number of intersections is odd,
     *            the point is inside the polygon.
     * WindingRule (nonzero): If the number of directed intersections
     *            are nonzero, then inside.
     */
    if (HaveAnyFillFromPathColor(stylePtr->fill)) {
        if ((stylePtr->fillRule == EvenOddRule) && (sumIntersections & 0x1)) {
            bestDist = 0.0;
        } else if ((stylePtr->fillRule == WindingRule) && (sumNonzerorule != 0)) {
            bestDist = 0.0;
        }
    }

done:
    if (polyPtr != staticSpace) {
        ckfree((char *) polyPtr);
    }
    return bestDist;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathGenericPathToArea --
 *
 *    This procedure is called to determine whether an item
 *    lies entirely inside, entirely outside, or overlapping
 *    a given rectangular area.
 *
 *    Each subpath is treated in turn. Generate straight line
 *    segments for each subpath and treat it as a polygon.
 *
 * Results:
 *    -1 is returned if the item is entirely outside the
 *    area, 0 if it overlaps, and 1 if it is entirely
 *    inside the given area.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

int
TkPathGenericPathToArea(
    Tk_PathCanvas canvas,   /* Canvas containing item. */
    Tk_PathItem *itemPtr,   /* Item to check against line. */
    Tk_PathStyle *stylePtr,
    TkPathAtom *atomPtr,
    int maxNumSegments,
    double *areaPtr)        /* Pointer to array of four coordinates
                             * (x1, y1, x2, y2) describing rectangular
                             * area.  */
{
    int inside;            /* Tentative guess about what to return,
                             * based on all points seen so far:  one
                             * means everything seen so far was
                             * inside the area;  -1 means everything
                             * was outside the area.  0 means overlap
                             * has been found. */
    int            numPoints = 0;
    int            numStrokes = 0;
    double        *polyPtr;
    double        currentT[2];
    Tk_PathState    state = itemPtr->state;
    TkPathMatrix        *matrixPtr = stylePtr->matrixPtr;
    double        staticSpace[2*MAX_NUM_STATIC_SEGMENTS];

    if (state == TK_PATHSTATE_HIDDEN) {
        return -1;
    }
    if (atomPtr == NULL) {
        return -1;
    }
    if ((stylePtr->fill != NULL) && (stylePtr->fill->color != NULL) &&
        (stylePtr->strokeColor == NULL)) {
        if (stylePtr->fill->gradientInstPtr == NULL) {
            return -1;
        }
    }

    /*
     * Do we need more memory or can we use static space?
     */
    if (maxNumSegments > MAX_NUM_STATIC_SEGMENTS) {
        polyPtr = (double *) ckalloc((unsigned) (2*maxNumSegments*sizeof(double)));
    } else {
        polyPtr = staticSpace;
    }

    /* A 'M' atom must be first, may show up later as well. */
    if (atomPtr->type == TK_PATH_ATOM_M) {
    TkMoveToAtom *move = (TkMoveToAtom *) atomPtr;

    PathApplyTMatrixToPoint(matrixPtr, &(move->x), currentT);
    } else if (atomPtr->type == TK_PATH_ATOM_ELLIPSE) {
    TkEllipseAtom *ellipse = (TkEllipseAtom *) atomPtr;

    PathApplyTMatrixToPoint(matrixPtr, &(ellipse->cx), currentT);
    } else if (atomPtr->type == TK_PATH_ATOM_RECT) {
    TkRectAtom *rect = (TkRectAtom *) atomPtr;

    PathApplyTMatrixToPoint(matrixPtr, &(rect->x), currentT);
    } else {
        return -1;
    }

    /*
     * This defines the starting point. It is either -1 or 1.
     * If any subseqent segment has a different 'inside'
     * then return 0 since one port (in|out)side and another
     * (out|in)side
     */
    inside = -1;
    if ((currentT[0] >= areaPtr[0]) && (currentT[0] <= areaPtr[2])
            && (currentT[1] >= areaPtr[1]) && (currentT[1] <= areaPtr[3])) {
        inside = 1;
    }

    while (atomPtr != NULL) {
        MakeSubPathSegments(&atomPtr, polyPtr, &numPoints, &numStrokes, matrixPtr);
        if (SubPathToArea(stylePtr, polyPtr, numPoints, numStrokes,
                areaPtr, inside) != inside) {
            inside = 0;
            goto done;
        }
    }

done:
    if (polyPtr != staticSpace) {
        ckfree((char *) polyPtr);
    }
    return inside;
}

/*
 *--------------------------------------------------------------
 *
 * ArcSegments --
 *
 *    Given the arc parameters it makes a sequence if line segments.
 *    All angles in radians!
 *    Note that segments are transformed!
 *
 * Results:
 *    The array at *coordPtr gets filled in with 2*numSteps
 *    coordinates, which correspond to the arc.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

static void
ArcSegments(
    CentralArcPars *arcPars,
    TkPathMatrix *matrixPtr,
    int includeFirst,        /* Should the first point be included? */
    int numSteps,        /* Number of curve segments to
                                 * generate.  */
    double *coordPtr)        /* Where to put new points. */
{
    int i;
    int istart = 1 - includeFirst;
    double cosPhi, sinPhi;
    double cosAlpha, sinAlpha;
    double alpha, dalpha, theta1;
    double cx, cy, rx, ry;

    cosPhi = cos(arcPars->phi);
    sinPhi = sin(arcPars->phi);
    cx = arcPars->cx;
    cy = arcPars->cy;
    rx = arcPars->rx;
    ry = arcPars->ry;
    theta1 = arcPars->theta1;
    dalpha = arcPars->dtheta/numSteps;

    for (i = istart; i <= numSteps; i++, coordPtr += 2) {
        alpha = theta1 + i*dalpha;
        cosAlpha = cos(alpha);
        sinAlpha = sin(alpha);
        coordPtr[0] = cx + rx*cosAlpha*cosPhi - ry*sinAlpha*sinPhi;
        coordPtr[1] = cy + rx*cosAlpha*sinPhi + ry*sinAlpha*cosPhi;
        PathApplyTMatrix(matrixPtr, coordPtr, coordPtr+1);
    }
}

/*
 * Get maximum number of segments needed to describe path.
 * Needed to see if we can use static space or need to allocate more.
 */

static int
GetArcNumSegments(double currentX, double currentY, TkArcAtom *arc)
{
    int result;
    int ntheta, nlength;
    int numSteps;            /* Number of curve points to
                     * generate.  */
    double cx, cy, rx, ry;
    double theta1, dtheta;

    result = TkPathEndpointToCentralArcParameters(
            currentX, currentY,
            arc->x, arc->y, arc->radX, arc->radY,
            DEGREES_TO_RADIANS * arc->angle,
            arc->largeArcFlag, arc->sweepFlag,
            &cx, &cy, &rx, &ry,
            &theta1, &dtheta);
    if (result == TK_PATH_ARC_Line) {
        return 2;
    } else if (result == TK_PATH_ARC_Skip) {
        return 0;
    }

    /* Estimate the number of steps needed.
     * Max 10 degrees or length 50.
     */
    ntheta = (int) (dtheta/5.0 + 0.5);
    nlength = (int) (0.5*(rx + ry)*dtheta/50 + 0.5);
    numSteps = MAX(4, MAX(ntheta, nlength));;
    return numSteps;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathCurveSegments --
 *
 *    Given four control points, create a larger set of points
 *    for a cubic Bezier spline based on the points.
 *
 * Results:
 *    The array at *coordPtr gets filled in with 2*numSteps
 *    coordinates, which correspond to the Bezier spline defined
 *    by the four control points.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

void
TkPathCurveSegments(
    double control[],        /* Array of coordinates for four
                 * control points:  x0, y0, x1, y1,
                 * ... x3 y3. */
    int includeFirst,        /* Should the first point be included? */
    int numSteps,        /* Number of curve segments to generate. */
    double *coordPtr)        /* Where to put new points. */
{
    int i;
    int istart = 1 - includeFirst;
    double u, u2, u3, t, t2, t3;

    /*
     * We should use the 'de Castlejau' algorithm to iterate
     * line segments until a certain tolerance.
     */

    for (i = istart; i <= numSteps; i++, coordPtr += 2) {
        t = ((double) i)/((double) numSteps);
        t2 = t*t;
        t3 = t2*t;
        u = 1.0 - t;
        u2 = u*u;
        u3 = u2*u;
        coordPtr[0] = control[0]*u3
                + 3.0 * (control[2]*t*u2 + control[4]*t2*u) + control[6]*t3;
        coordPtr[1] = control[1]*u3
                + 3.0 * (control[3]*t*u2 + control[5]*t2*u) + control[7]*t3;
    }
}

/*
 *--------------------------------------------------------------
 *
 * QuadBezierSegments --
 *
 *    Given three control points, create a larger set of points
 *    for a quadratic Bezier spline based on the points.
 *
 * Results:
 *    The array at *coordPtr gets filled in with 2*numSteps
 *    coordinates, which correspond to the quadratic Bezier spline defined
 *    by the control points.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

static void
QuadBezierSegments(
    double control[],        /* Array of coordinates for three
                 * control points:  x0, y0, x1, y1,
                 * x2, y2. */
    int includeFirst,        /* Should the first point be included? */
    int numSteps,        /* Number of curve segments to generate. */
    double *coordPtr)        /* Where to put new points. */
{
    int i;
    int istart = 1 - includeFirst;
    double u, u2, t, t2;

    for (i = istart; i <= numSteps; i++, coordPtr += 2) {
        t = ((double) i)/((double) numSteps);
        t2 = t*t;
        u = 1.0 - t;
        u2 = u*u;
        coordPtr[0] = control[0]*u2 + 2.0 * control[2]*t*u + control[4]*t2;
        coordPtr[1] = control[1]*u2 + 2.0 * control[3]*t*u + control[5]*t2;
    }
}

static void
EllipseSegments(
    double center[],
    double rx, double ry,
    double angle,        /* Angle of rotated ellipse. */
    int numSteps,        /* Number of curve segments to generate. */
    double *coordPtr)        /* Where to put new points. */
{
    double phi, delta;
    double cosA, sinA;
    double cosPhi, sinPhi;

    cosA = cos(angle);
    sinA = sin(angle);
    delta = 2*M_PI/(numSteps-1);

    for (phi = 0.0; phi <= 2*M_PI+1e-6; phi += delta, coordPtr += 2) {
        cosPhi = cos(phi);
        sinPhi = sin(phi);
        coordPtr[0] = center[0] + rx*cosA*cosPhi - ry*sinA*sinPhi;
        coordPtr[1] = center[1] + rx*sinA*cosPhi + ry*cosA*sinPhi;
    }
}

/*
 *--------------------------------------------------------------
 *
 * AddArcSegments, AddQuadBezierSegments, AddCurveToSegments,
 *   AddEllipseToSegments --
 *
 *    Adds a number of points along the arc (curve) to coordPtr
 *    representing straight line segments.
 *
 * Results:
 *    Number of points added.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

static int
AddArcSegments(
    TkPathMatrix *matrixPtr,
    double current[2],        /* Current point. */
    TkArcAtom *arc,
    double *coordPtr)        /* Where to put the points. */
{
    int result;
    int numPoints;
    CentralArcPars arcPars;
    double cx, cy, rx, ry;
    double theta1, dtheta;

    /*
     * Note: The arc parametrization used cannot generally
     * be transformed. Need to transform each line segment separately!
     */

    result = TkPathEndpointToCentralArcParameters(
            current[0], current[1],
            arc->x, arc->y, arc->radX, arc->radY,
            DEGREES_TO_RADIANS * arc->angle,
            arc->largeArcFlag, arc->sweepFlag,
            &cx, &cy, &rx, &ry,
            &theta1, &dtheta);
    if (result == TK_PATH_ARC_Line) {
        double pts[2];

        pts[0] = arc->x;
        pts[1] = arc->y;
        PathApplyTMatrix(matrixPtr, pts, pts+1);
        coordPtr[0] = pts[0];
        coordPtr[1] = pts[1];
        return 1;
    } else if (result == TK_PATH_ARC_Skip) {
        return 0;
    }

    arcPars.cx = cx;
    arcPars.cy = cy;
    arcPars.rx = rx;
    arcPars.ry = ry;
    arcPars.theta1 = theta1;
    arcPars.dtheta = dtheta;
    arcPars.phi = arc->angle;

    numPoints = GetArcNumSegments(current[0], current[1], arc);
    ArcSegments(&arcPars, matrixPtr, 0, numPoints, coordPtr);

    return numPoints;
}

static int
AddQuadBezierSegments(
    TkPathMatrix *matrixPtr,
    double current[2],        /* Current point. */
    TkQuadBezierAtom *quad,
    double *coordPtr)        /* Where to put the points. */
{
    int numPoints;            /* Number of curve points to
                             * generate.  */
    double control[6];

    PathApplyTMatrixToPoint(matrixPtr, current, control);
    PathApplyTMatrixToPoint(matrixPtr, &(quad->ctrlX), control+2);
    PathApplyTMatrixToPoint(matrixPtr, &(quad->anchorX), control+4);

    numPoints = TK_PATH_NUMSEGEMENTS_QuadBezier;
    QuadBezierSegments(control, 0, numPoints, coordPtr);

    return numPoints;
}

static int
AddCurveToSegments(
    TkPathMatrix *matrixPtr,
    double current[2],            /* Current point. */
    TkCurveToAtom *curve,
    double *coordPtr)
{
    int numSteps;                /* Number of curve points to
                                 * generate.  */
    double control[8];

    PathApplyTMatrixToPoint(matrixPtr, current, control);
    PathApplyTMatrixToPoint(matrixPtr, &(curve->ctrlX1), control+2);
    PathApplyTMatrixToPoint(matrixPtr, &(curve->ctrlX2), control+4);
    PathApplyTMatrixToPoint(matrixPtr, &(curve->anchorX), control+6);

    numSteps = TK_PATH_NUMSEGEMENTS_CurveTo;
    TkPathCurveSegments(control, 1, numSteps, coordPtr);

    return numSteps;
}

static int
AddEllipseToSegments(
    TkPathMatrix *matrixPtr,
    TkEllipseAtom *ellipse,
    double *coordPtr)
{
    int numSteps;
    double rx, ry, angle;
    double c[2], crx[2], cry[2];
    double p[2];

    /*
     * We transform the three points: c, c+rx, c+ry
     * and then compute the parameters for the transformed ellipse.
     * This is because an affine transform of an ellipse is still an ellipse.
     */
    p[0] = ellipse->cx;
    p[1] = ellipse->cy;
    PathApplyTMatrixToPoint(matrixPtr, p, c);
    p[0] = ellipse->cx + ellipse->rx;
    p[1] = ellipse->cy;
    PathApplyTMatrixToPoint(matrixPtr, p, crx);
    p[0] = ellipse->cx;
    p[1] = ellipse->cy + ellipse->ry;
    PathApplyTMatrixToPoint(matrixPtr, p, cry);
    rx = hypot(crx[0]-c[0], crx[1]-c[1]);
    ry = hypot(cry[0]-c[0], cry[1]-c[1]);
    angle = atan2(crx[1]-c[1], crx[0]-c[0]);

    /* Note we add 1 here since we need both start and stop points.
     * Small things wont need so many segments.
     * Approximate circumference: 4(rx+ry)
     */
    if (rx+ry < 2.1) {
        numSteps = 1;
    } else if (rx+ry < 4) {
        numSteps = 3;
    } else if (rx+ry < TK_PATH_NUMSEGEMENTS_Ellipse) {
        numSteps = (int)(rx+ry+2);
    } else {
        numSteps = TK_PATH_NUMSEGEMENTS_Ellipse + 1;
    }
    EllipseSegments(c, rx, ry, angle, numSteps, coordPtr);

    return numSteps;
}

static int
AddRectToSegments(
    TkPathMatrix *matrixPtr,
    TkRectAtom *rect,
    double *coordPtr)
{
    int i;
    double p[8];

    p[0] = rect->x;
    p[1] = rect->y;
    p[2] = rect->x + rect->width;
    p[3] = rect->y;
    p[4] = rect->x + rect->width;
    p[5] = rect->y + rect->height;
    p[6] = rect->x;
    p[7] = rect->y + rect->height;

    for (i = 0; i < 8; i += 2, coordPtr += 2) {
        PathApplyTMatrix(matrixPtr, p+i, p+i+1);
        coordPtr[0] = p[i];
        coordPtr[1] = p[i+1];
    }
    return 4;
}

/*
 *--------------------------------------------------------------
 *
 * MakeSubPathSegments --
 *
 *    Supposed to be a generic segment generator that can be used
 *    by both Area and Point functions.
 *
 * Results:
 *    Points filled into polyPtr...
 *
 * Side effects:
 *    Pointer *atomPtrPtr may be updated.
 *
 *--------------------------------------------------------------
 */

static void
MakeSubPathSegments(TkPathAtom **atomPtrPtr, double *polyPtr,
        int *numPointsPtr, int *numStrokesPtr, TkPathMatrix *matrixPtr)
{
    int     first = 1;
    int        numPoints;
    int        numStrokes;
    int        numAdded;
    int        isclosed = 0;
    double     current[2];    /* Current untransformed point. */
    double    *coordPtr;
    TkPathAtom     *atomPtr;

    /* @@@     Note that for unfilled paths we could have made a progressive
     *         area (point) check which may be faster since we may stop when 0 (overlapping).
     *           For filled paths we cannot rely on this since the area rectangle
     *        may be entirely enclosed in the path and still overlapping.
     *        (Need better explanation!)
     */

    /*
     * Check each segment of the path.
     * Any transform matrix is applied at the last stage when comparing to rect.
     * 'current' is always untransformed coords.
     */

    current[0] = 0.0;
    current[1] = 0.0;
    numPoints = 0;
    numStrokes = 0;
    isclosed = 0;
    atomPtr = *atomPtrPtr;
    coordPtr = NULL;

    while (atomPtr != NULL) {

        switch (atomPtr->type) {
            case TK_PATH_ATOM_M: {
                TkMoveToAtom *move = (TkMoveToAtom *) atomPtr;

                /* A 'M' atom must be first, may show up later as well. */

                if (first) {
                    coordPtr = polyPtr;
                    current[0] = move->x;
                    current[1] = move->y;
                    PathApplyTMatrixToPoint(matrixPtr, current, coordPtr);
                    coordPtr += 2;
                    numPoints = 1;
                } else {

                    /*
                     * We have finalized a subpath.
                     */
                    goto done;
                }
                first = 0;
                break;
            }
            case TK_PATH_ATOM_L: {
                TkLineToAtom *line = (TkLineToAtom *) atomPtr;

                PathApplyTMatrixToPoint(matrixPtr, &(line->x), coordPtr);
                current[0] = line->x;
                current[1] = line->y;
                coordPtr += 2;
                numPoints++;;
                break;
            }
            case TK_PATH_ATOM_A: {
                TkArcAtom *arc = (TkArcAtom *) atomPtr;

                numAdded = AddArcSegments(matrixPtr, current, arc, coordPtr);
                coordPtr += 2 * numAdded;
                numPoints += numAdded;
                current[0] = arc->x;
                current[1] = arc->y;
                break;
            }
            case TK_PATH_ATOM_Q: {
                TkQuadBezierAtom *quad = (TkQuadBezierAtom *) atomPtr;

                numAdded = AddQuadBezierSegments(matrixPtr, current,
                        quad, coordPtr);
                coordPtr += 2 * numAdded;
                numPoints += numAdded;
                current[0] = quad->anchorX;
                current[1] = quad->anchorY;
                break;
            }
            case TK_PATH_ATOM_C: {
                TkCurveToAtom *curve = (TkCurveToAtom *) atomPtr;

                numAdded = AddCurveToSegments(matrixPtr, current,
                        curve, coordPtr);
                coordPtr += 2 * numAdded;
                numPoints += numAdded;
                current[0] = curve->anchorX;
                current[1] = curve->anchorY;
                break;
            }
            case TK_PATH_ATOM_Z: {
                TkCloseAtom *close = (TkCloseAtom *) atomPtr;

                /* Just add the first point to the end. */
                coordPtr[0] = polyPtr[0];
                coordPtr[1] = polyPtr[1];
                coordPtr += 2;
                numPoints++;
                current[0]  = close->x;
                current[1]  = close->y;
                isclosed = 1;
                break;
            }
            case TK_PATH_ATOM_ELLIPSE: {
                TkEllipseAtom *ellipse = (TkEllipseAtom *) atomPtr;

                if (first) {
                    coordPtr = polyPtr;
                }
                numAdded = AddEllipseToSegments(matrixPtr, ellipse, coordPtr);
                coordPtr += 2 * numAdded;
                numPoints += numAdded;
                if (first) {
                    /* Not sure about this. Never used anyway! */
                    current[0]  = ellipse->cx + ellipse->rx;
                    current[1]  = ellipse->cy;
                }
                break;
            }
            case TK_PATH_ATOM_RECT: {
                TkRectAtom *rect = (TkRectAtom *) atomPtr;

                if (first) {
                    coordPtr = polyPtr;
                }
                numAdded = AddRectToSegments(matrixPtr, rect, coordPtr);
                coordPtr += 2 * numAdded;
                numPoints += numAdded;
                current[0] = rect->x;
                current[1] = rect->y;
                break;
            }
        }
        atomPtr = atomPtr->nextPtr;
    }

done:
    if (numPoints > 1) {
        if (isclosed) {
            numStrokes = numPoints;
        } else {
            numStrokes = numPoints - 1;
        }
    }
    *numPointsPtr = numPoints;
    *numStrokesPtr = numStrokes;
    *atomPtrPtr = atomPtr;

    return;
}

/*
 *--------------------------------------------------------------
 *
 * SubPathToArea --
 *
 *    This procedure is called to determine whether a subpath
 *    lies entirely inside, entirely outside, or overlapping
 *    a given rectangular area.
 *
 * Results:
 *    -1 is returned if the item is entirely outside the
 *    area, 0 if it overlaps, and 1 if it is entirely
 *    inside the given area.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

static int
SubPathToArea(
    Tk_PathStyle *stylePtr,
    double     *polyPtr,
    int     numPoints,     /* Total number of points. First one
                                 * is duplicated in the last. */
    int        numStrokes,    /* The number of strokes which is one less
                                 * than numPoints if path not closed. */
    double     *rectPtr,
    int     inside)        /* This is the current inside status. */
{
    double width;

    /* @@@ There is an open question how a closed unfilled polygon
     *    completely enclosing the area rect should be counted.
     *    While the tk canvas polygon item counts it as intersecting (0),
     *    the line item counts it as outside (-1).
     */

    if (HaveAnyFillFromPathColor(stylePtr->fill)) {

        /* This checks a closed polygon with zero width for inside.
         * If area rect completely enclosed it returns intersecting (0).
         */
        if (TkPolygonToArea(polyPtr, numPoints, rectPtr) != inside) {
            return 0;
        }
    }
    if (stylePtr->strokeColor != NULL) {
        width = stylePtr->strokeWidth;
        if (width < 1.0) {
            width = 1.0;
        }
        if (stylePtr->strokeWidth > kPathStrokeThicknessLimit) {
            if (TkThickPolyLineToArea(polyPtr, numPoints,
                    width, stylePtr->capStyle,
                    stylePtr->joinStyle, rectPtr) != inside) {
                return 0;
            }
        } else {
        if (PathPolyLineToArea(polyPtr, numPoints, rectPtr) != inside) {
                return 0;
            }
        }
    }
    return inside;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathTranslatePathAtoms --
 *
 *    This procedure is called to translate a linked list of path atoms.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Path atoms changed.
 *
 *--------------------------------------------------------------
 */

void
TkPathTranslatePathAtoms(
    TkPathAtom *atomPtr,
    double deltaX,                /* Amount by which item is to be */
    double deltaY)              /* moved. */
{
    while (atomPtr != NULL) {
        switch (atomPtr->type) {
            case TK_PATH_ATOM_M: {
                TkMoveToAtom *move = (TkMoveToAtom *) atomPtr;

                move->x += deltaX;
                move->y += deltaY;
                break;
            }
            case TK_PATH_ATOM_L: {
                TkLineToAtom *line = (TkLineToAtom *) atomPtr;

                line->x += deltaX;
                line->y += deltaY;
                break;
            }
            case TK_PATH_ATOM_A: {
                TkArcAtom *arc = (TkArcAtom *) atomPtr;

                arc->x += deltaX;
                arc->y += deltaY;
                break;
            }
            case TK_PATH_ATOM_Q: {
                TkQuadBezierAtom *quad = (TkQuadBezierAtom *) atomPtr;

                quad->ctrlX += deltaX;
                quad->ctrlY += deltaY;
                quad->anchorX += deltaX;
                quad->anchorY += deltaY;
                break;
            }
            case TK_PATH_ATOM_C: {
                TkCurveToAtom *curve = (TkCurveToAtom *) atomPtr;

                curve->ctrlX1 += deltaX;
                curve->ctrlY1 += deltaY;
                curve->ctrlX2 += deltaX;
                curve->ctrlY2 += deltaY;
                curve->anchorX += deltaX;
                curve->anchorY += deltaY;
                break;
            }
            case TK_PATH_ATOM_Z: {
                TkCloseAtom *close = (TkCloseAtom *) atomPtr;

                close->x += deltaX;
                close->y += deltaY;
                break;
            }
            case TK_PATH_ATOM_ELLIPSE:
            case TK_PATH_ATOM_RECT: {
                Tcl_Panic("TK_PATH_ATOM_ELLIPSE TK_PATH_ATOM_RECT are not supported for TkPathTranslatePathAtoms");
                break;
            }
        }
        atomPtr = atomPtr->nextPtr;
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkPathScalePathAtoms --
 *
 *    This procedure is called to scale a linked list of path atoms.
 *    The following transformation is applied to all point
 *    coordinates:
 *    x' = originX + scaleX*(x-originX)
 *    y' = originY + scaleY*(y-originY)
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Path atoms changed.
 *
 *--------------------------------------------------------------
 */

void
TkPathScalePathAtoms(
    TkPathAtom *atomPtr,
    double originX, double originY,    /* Origin about which to scale rect. */
    double scaleX,            /* Amount to scale in X direction. */
    double scaleY)            /* Amount to scale in Y direction. */
{
    while (atomPtr != NULL) {
        switch (atomPtr->type) {
            case TK_PATH_ATOM_M: {
                TkMoveToAtom *move = (TkMoveToAtom *) atomPtr;

                move->x = originX + scaleX*(move->x - originX);
                move->y = originY + scaleY*(move->y - originY);
                break;
            }
            case TK_PATH_ATOM_L: {
                TkLineToAtom *line = (TkLineToAtom *) atomPtr;

                line->x = originX + scaleX*(line->x - originX);
                line->y = originY + scaleY*(line->y - originY);
                break;
            }
            case TK_PATH_ATOM_A: {
                TkArcAtom *arc = (TkArcAtom *) atomPtr;
        /*
         * @@@ TODO: This is a very much simplified math which is WRONG!
         */
        if (fabs(fmod(arc->angle, 180.0)) < 0.001) {
            arc->radX = scaleX*arc->radX;
            arc->radY = scaleY*arc->radY;
        } else if (fabs(fmod(arc->angle, 90.0)) < 0.001) {
            arc->radX = scaleY*arc->radX;
            arc->radY = scaleX*arc->radY;
        } else {
            double angle;
            double nx, ny;

            if (scaleX == 0.0) Tcl_Panic("singularity when scaling arc atom");
            angle = atan(scaleY/scaleX * tan(arc->angle * DEGREES_TO_RADIANS));
            nx = cos(arc->angle * DEGREES_TO_RADIANS);
            ny = sin(arc->angle * DEGREES_TO_RADIANS);

            arc->angle = angle * RADIANS_TO_DEGREES;
            arc->radX = arc->radX * hypot( scaleX*nx, scaleY*ny);
            arc->radY = arc->radY * hypot(-scaleX*ny, scaleY*nx);
        }
        arc->x = originX + scaleX*(arc->x - originX);
        arc->y = originY + scaleY*(arc->y - originY);
                break;
            }
            case TK_PATH_ATOM_Q: {
                TkQuadBezierAtom *quad = (TkQuadBezierAtom *) atomPtr;

                quad->ctrlX = originX + scaleX*(quad->ctrlX - originX);
                quad->ctrlY = originY + scaleY*(quad->ctrlY - originY);
                quad->anchorX = originX + scaleX*(quad->anchorX - originX);
                quad->anchorY = originY + scaleY*(quad->anchorY - originY);
                break;
            }
            case TK_PATH_ATOM_C: {
                TkCurveToAtom *curve = (TkCurveToAtom *) atomPtr;

                curve->ctrlX1 = originX + scaleX*(curve->ctrlX1 - originX);
                curve->ctrlY1 = originY + scaleY*(curve->ctrlY1 - originY);
                curve->ctrlX2 = originX + scaleX*(curve->ctrlX2 - originX);
                curve->ctrlY2 = originY + scaleY*(curve->ctrlY2 - originY);
                curve->anchorX = originX + scaleX*(curve->anchorX - originX);
                curve->anchorY = originY + scaleY*(curve->anchorY - originY);
                break;
            }
            case TK_PATH_ATOM_Z: {
                TkCloseAtom *close = (TkCloseAtom *) atomPtr;

                close->x = originX + scaleX*(close->x - originX);
                close->y = originY + scaleY*(close->y - originY);
                break;
            }
            case TK_PATH_ATOM_ELLIPSE:
            case TK_PATH_ATOM_RECT: {
                Tcl_Panic("TK_PATH_ATOM_ELLIPSE TK_PATH_ATOM_RECT are not supported for TkPathScalePathAtoms");
                break;
            }
        }
        atomPtr = atomPtr->nextPtr;
    }
}

/*------------------*/

TkPathMatrix
TkPathGetCanvasTMatrix(Tk_PathCanvas canvas)
{
    TkPathMatrix m = TK_PATH_UNIT_TMATRIX;
    TkPathCanvas *canvasPtr = (TkPathCanvas *) canvas;

    /* @@@ Any scaling involved as well??? */
    m.tx = -canvasPtr->drawableXOrigin;
    m.ty = -canvasPtr->drawableYOrigin;
    return m;
}

TkPathRect
TkPathNewEmptyPathRect(void)
{
    TkPathRect r;

    r.x1 = 1.0e36;
    r.y1 = 1.0e36;
    r.x2 = -1.0e36;
    r.y2 = -1.0e36;
    return r;
}

static int
IsPathRectEmpty(TkPathRect *r)
{
    if ((r->x2 >= r->x1) && (r->y2 >= r->y1)) {
        return 0;
    } else {
        return 1;
    }
}

void
TkPathIncludePointInRect(TkPathRect *r, double x, double y)
{
    r->x1 = MIN(r->x1, x);
    r->y1 = MIN(r->y1, y);
    r->x2 = MAX(r->x2, x);
    r->y2 = MAX(r->y2, y);
}

void
TkPathTranslatePathRect(TkPathRect *r, double deltaX, double deltaY)
{
    r->x1 += deltaX;
    r->x2 += deltaX;
    r->y1 += deltaY;
    r->y2 += deltaY;
}

void
TkPathScalePathRect(TkPathRect *r, double originX, double originY,
        double scaleX, double scaleY)
{
    r->x1 = originX + scaleX*(r->x1 - originX);
    r->x2 = originX + scaleX*(r->x2 - originX);
    r->y1 = originY + scaleY*(r->y1 - originY);
    r->y2 = originY + scaleY*(r->y2 - originY);
}

void
TkPathTranslateItemHeader(Tk_PathItem *itemPtr, double deltaX, double deltaY)
{
    TkPathTranslatePathRect(&itemPtr->totalBbox, deltaX, deltaY);

    /* @@@ TODO: Beware for cumulated round-off errors! */
    /* If all coords == -1 the item is hidden. */
    if ((itemPtr->x1 != -1) || (itemPtr->x2 != -1) ||
        (itemPtr->y1 != -1) || (itemPtr->y2 != -1)) {
    Tk_PathStyle style;

    style = TkPathCanvasInheritStyle(itemPtr, TK_PATH_MERGESTYLE_NOTFILL);
    TkPathSetGenericPathHeaderBbox(itemPtr, style.matrixPtr, &itemPtr->totalBbox);
    TkPathCanvasFreeInheritedStyle(&style);
    }
}

void
TkPathScaleItemHeader(Tk_PathItem *itemPtr, double originX, double originY,
        double scaleX, double scaleY)
{
    TkPathScalePathRect(&itemPtr->totalBbox, originX, originY, scaleX, scaleY);

    /* @@@ TODO: Beware for cumulated round-off errors! */
    /* If all coords == -1 the item is hidden. */
    if ((itemPtr->x1 != -1) || (itemPtr->x2 != -1) ||
        (itemPtr->y1 != -1) || (itemPtr->y2 != -1)) {
    int min, max;
    Tk_PathStyle style;

    style = TkPathCanvasInheritStyle(itemPtr, TK_PATH_MERGESTYLE_NOTFILL);
    TkPathSetGenericPathHeaderBbox(itemPtr, style.matrixPtr, &itemPtr->totalBbox);
    TkPathCanvasFreeInheritedStyle(&style);

    min = MIN(itemPtr->x1, itemPtr->x2);
    max = MAX(itemPtr->x1, itemPtr->x2);
    itemPtr->x1 = min;
    itemPtr->x2 = max;
    min = MIN(itemPtr->y1, itemPtr->y2);
    max = MAX(itemPtr->y1, itemPtr->y2);
    itemPtr->y1 = min;
    itemPtr->y2 = max;
    }
}

/*
 *--------------------------------------------------------------
 *
 * PathPolyLineToArea --
 *
 *    Determine whether an open polygon lies entirely inside, entirely
 *    outside, or overlapping a given rectangular area.
 *     Identical to TkPolygonToArea except that it returns outside (-1)
 *    if completely encompassing the area rect.
 *
 * Results:
 *    -1 is returned if the polygon given by polyPtr and numPoints
 *    is entirely outside the rectangle given by rectPtr.  0 is
 *    returned if the polygon overlaps the rectangle, and 1 is
 *    returned if the polygon is entirely inside the rectangle.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

static int
PathPolyLineToArea(
    double *polyPtr,        /* Points to an array coordinates for
                             * closed polygon:  x0, y0, x1, y1, ...
                             * The polygon may be self-intersecting. */
    int numPoints,        /* Total number of points at *polyPtr. */
    double *rectPtr)        /* Points to coords for rectangle, in the
                             * order x1, y1, x2, y2.  X1 and y1 must
                             * be lower-left corner. */
{
    int state;            /* State of all edges seen so far (-1 means
                             * outside, 1 means inside, won't ever be
                             * 0). */
    int count;
    double *pPtr;

    /*
     * Iterate over all of the edges of the polygon and test them
     * against the rectangle.  Can quit as soon as the state becomes
     * "intersecting".
     */

    state = TkLineToArea(polyPtr, polyPtr+2, rectPtr);
    if (state == 0) {
        return 0;
    }
    for (pPtr = polyPtr+2, count = numPoints-1; count >= 2;
            pPtr += 2, count--) {
        if (TkLineToArea(pPtr, pPtr+2, rectPtr) != state) {
            return 0;
        }
    }
    return state;
}

/*
 *--------------------------------------------------------------
 *
 * PathThickPolygonToPoint --
 *
 *    Computes the distance from a given point to a given
 *    thick polyline (open or closed), in canvas units.
 *
 * Results:
 *    The return value is 0 if the point whose x and y coordinates
 *    are pointPtr[0] and pointPtr[1] is inside the line.  If the
 *    point isn't inside the line then the return value is the
 *    distance from the point to the line.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

static double
PathThickPolygonToPoint(
    int joinStyle, int capStyle,
    double width,
    int isclosed,
    double *polyPtr,    /* Points to an array coordinates for
                         * the polygon:  x0, y0, x1, y1, ...
                         * The polygon may be self-intersecting. */
    int numPoints,    /* Total number of points at *polyPtr. */
    double *pointPtr)    /* Points to coords for point. */
{
    int count;
    int project;
    int testrounding;
    int changedMiterToBevel;    /* Non-zero means that a mitered corner
                                 * had to be treated as beveled after all
                                 * because the angle was < 11 degrees. */
    double bestDist;        /* Closest distance between point and
                                 * any edge in polygon. */
    double dist, radius;
    double *coordPtr;
    double poly[10];

    bestDist = 1.0e36;
    radius = width/2.0;
    project = 0;
    if (!isclosed) {
        project = (capStyle == CapProjecting);
    }

    /*
     * The overall idea is to iterate through all of the edges of
     * the line, computing a polygon for each edge and testing the
     * point against that polygon.  In addition, there are additional
     * tests to deal with rounded joints and caps.
     */

    changedMiterToBevel = 0;
    for (count = numPoints, coordPtr = polyPtr; count >= 2;
            count--, coordPtr += 2) {

        /*
         * If rounding is done around the first point then compute
         * the distance between the point and the point.
         */
        testrounding = 0;
        if (isclosed) {
            testrounding = (joinStyle == JoinRound);
        } else {
            testrounding = (((capStyle == CapRound) && (count == numPoints))
                    || ((joinStyle == JoinRound) && (count != numPoints)));
        }
        if (testrounding) {
            dist = hypot(coordPtr[0] - pointPtr[0], coordPtr[1] - pointPtr[1])
                    - radius;
            if (dist <= 0.0) {
                bestDist = 0.0;
                goto donepoint;
            } else if (dist < bestDist) {
                bestDist = dist;
            }
        }

        /*
         * Compute the polygonal shape corresponding to this edge,
         * consisting of two points for the first point of the edge
         * and two points for the last point of the edge.
         */

        if (count == numPoints) {
            TkGetButtPoints(coordPtr+2, coordPtr, (double) width,
                    project, poly, poly+2);
        } else if ((joinStyle == JoinMiter) && !changedMiterToBevel) {
            poly[0] = poly[6];
            poly[1] = poly[7];
            poly[2] = poly[4];
            poly[3] = poly[5];
        } else {
            TkGetButtPoints(coordPtr+2, coordPtr, (double) width, 0,
                    poly, poly+2);

            /*
             * If this line uses beveled joints, then check the distance
             * to a polygon comprising the last two points of the previous
             * polygon and the first two from this polygon;  this checks
             * the wedges that fill the mitered joint.
             */

            if ((joinStyle == JoinBevel) || changedMiterToBevel) {
                poly[8] = poly[0];
                poly[9] = poly[1];
                dist = TkPolygonToPoint(poly, 5, pointPtr);
                if (dist <= 0.0) {
                    bestDist = 0.0;
                    goto donepoint;
                } else if (dist < bestDist) {
                    bestDist = dist;
                }
                changedMiterToBevel = 0;
            }
        }
        if (count == 2) {
            TkGetButtPoints(coordPtr, coordPtr+2, (double) width,
                    project, poly+4, poly+6);
        } else if (joinStyle == JoinMiter) {
            if (TkGetMiterPoints(coordPtr, coordPtr+2, coordPtr+4,
                    (double) width, poly+4, poly+6) == 0) {
                changedMiterToBevel = 1;
                TkGetButtPoints(coordPtr, coordPtr+2, (double) width,
                        0, poly+4, poly+6);
            }
        } else {
            TkGetButtPoints(coordPtr, coordPtr+2, (double) width, 0,
                    poly+4, poly+6);
        }
        poly[8] = poly[0];
        poly[9] = poly[1];
        dist = TkPolygonToPoint(poly, 5, pointPtr);
        if (dist <= 0.0) {
            bestDist = 0.0;
            goto donepoint;
        } else if (dist < bestDist) {
            bestDist = dist;
        }
    }

    /*
     * If caps are rounded, check the distance to the cap around the
     * final end point of the line.
     */
    if (!isclosed && (capStyle == CapRound)) {
        dist = hypot(coordPtr[0] - pointPtr[0], coordPtr[1] - pointPtr[1])
                - width/2.0;
        if (dist <= 0.0) {
            bestDist = 0.0;
            goto donepoint;
        } else if (dist < bestDist) {
            bestDist = dist;
        }
    }

donepoint:

    return bestDist;
}

/*
 *--------------------------------------------------------------
 *
 * PathPolygonToPointEx --
 *
 *    Compute the distance from a point to a polygon. This is
 *    essentially identical to TkPolygonToPoint with two exceptions:
 *    1)     It returns the closest distance to the *stroke*,
 *        any fill unrecognized.
 *    2)    It returns both number of total intersections, and
 *        the number of directed crossings, nonzerorule.
 *
 * Results:
 *    The return value is 0.0 if the point referred to by
 *    pointPtr is within the polygon referred to by polyPtr
 *    and numPoints.  Otherwise the return value is the
 *    distance of the point from the polygon.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

static double
PathPolygonToPointEx(
    double *polyPtr,    /* Points to an array coordinates for
                         * the polygon:  x0, y0, x1, y1, ...
                         * The polygon may be self-intersecting.
                         * If a fillRule is used the last point
                         * must duplicate the first one. */
    int numPoints,    /* Total number of points at *polyPtr. */
    double *pointPtr,    /* Points to coords for point. */
    int *intersectionsPtr,/* (out) The number of intersections. */
    int *nonzerorulePtr)/* (out) The number of intersections
             * considering crossing direction. */
{
    double bestDist;    /* Closest distance between point and
                         * any edge in polygon. */
    int intersections;    /* Number of edges in the polygon that
                         * intersect a ray extending vertically
                         * upwards from the point to infinity. */
    int nonzerorule;    /* As 'intersections' except that it adds
                         * one if crossing right to left, and
                         * subtracts one if crossing left to right. */
    int count;
    double *pPtr;

    /*
     * Iterate through all of the edges in the polygon, updating
     * bestDist and intersections.
     *
     * TRICKY POINT:  when computing intersections, include left
     * x-coordinate of line within its range, but not y-coordinate.
     * Otherwise if the point lies exactly below a vertex we'll
     * count it as two intersections.
     */

    bestDist = 1.0e36;
    intersections = 0;
    nonzerorule = 0;

    for (count = numPoints, pPtr = polyPtr; count > 1; count--, pPtr += 2) {
        double x, y, dist;

        /*
         * Compute the point on the current edge closest to the point
         * and update the intersection count.  This must be done
         * separately for vertical edges, horizontal edges, and
         * other edges.
         */

        if (pPtr[2] == pPtr[0]) {

            /*
             * Vertical edge.
             */

            x = pPtr[0];
            if (pPtr[1] >= pPtr[3]) {
                y = MIN(pPtr[1], pointPtr[1]);
                y = MAX(y, pPtr[3]);
            } else {
                y = MIN(pPtr[3], pointPtr[1]);
                y = MAX(y, pPtr[1]);
            }
        } else if (pPtr[3] == pPtr[1]) {

            /*
             * Horizontal edge.
             */

            y = pPtr[1];
            if (pPtr[0] >= pPtr[2]) {
                x = MIN(pPtr[0], pointPtr[0]);
                x = MAX(x, pPtr[2]);
                if ((pointPtr[1] < y) && (pointPtr[0] < pPtr[0])
                        && (pointPtr[0] >= pPtr[2])) {
                    intersections++;
                    nonzerorule++;
                }
            } else {
                x = MIN(pPtr[2], pointPtr[0]);
                x = MAX(x, pPtr[0]);
                if ((pointPtr[1] < y) && (pointPtr[0] < pPtr[2])
                        && (pointPtr[0] >= pPtr[0])) {
                    intersections++;
                    nonzerorule--;
                }
            }
        } else {
            double m1, b1, m2, b2;
            int lower;        /* Non-zero means point below line. */

            /*
             * The edge is neither horizontal nor vertical.  Convert the
             * edge to a line equation of the form y = m1*x + b1.  Then
             * compute a line perpendicular to this edge but passing
             * through the point, also in the form y = m2*x + b2.
             */

            m1 = (pPtr[3] - pPtr[1])/(pPtr[2] - pPtr[0]);
            b1 = pPtr[1] - m1*pPtr[0];
            m2 = -1.0/m1;
            b2 = pointPtr[1] - m2*pointPtr[0];
            x = (b2 - b1)/(m1 - m2);
            y = m1*x + b1;
            if (pPtr[0] > pPtr[2]) {
                if (x > pPtr[0]) {
                    x = pPtr[0];
                    y = pPtr[1];
                } else if (x < pPtr[2]) {
                    x = pPtr[2];
                    y = pPtr[3];
                }
            } else {
                if (x > pPtr[2]) {
                    x = pPtr[2];
                    y = pPtr[3];
                } else if (x < pPtr[0]) {
                    x = pPtr[0];
                    y = pPtr[1];
                }
            }
            lower = (m1*pointPtr[0] + b1) > pointPtr[1];
            if (lower && (pointPtr[0] >= MIN(pPtr[0], pPtr[2]))
                    && (pointPtr[0] < MAX(pPtr[0], pPtr[2]))) {
                intersections++;
                if (pPtr[0] >= pPtr[2]) {
                    nonzerorule++;
                } else {
                    nonzerorule--;
                }
            }
        }

        /*
         * Compute the distance to the closest point, and see if that
         * is the best distance seen so far.
         */

        dist = hypot(pointPtr[0] - x, pointPtr[1] - y);
        if (dist < bestDist) {
            bestDist = dist;
        }
    }
    *intersectionsPtr = intersections;
    *nonzerorulePtr = nonzerorule;

    return bestDist;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathRectToPoint --
 *
 *    Computes the distance from a given point to a given
 *    rectangle, in canvas units.
 *
 * Results:
 *    The return value is 0 if the point whose x and y coordinates
 *    are pointPtr[0] and pointPtr[1] is inside the rectangle.  If the
 *    point isn't inside the rectangle then the return value is the
 *    distance from the point to the rectangle.  If item is filled,
 *    then anywhere in the interior is considered "inside"; if
 *    item isn't filled, then "inside" means only the area
 *    occupied by the outline.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

double
TkPathRectToPoint(
    double rectPtr[],     /* Bare rectangle. */
    double width,     /* Width of stroke, or 0. */
    int filled,     /* Is rectangle filled. */
    double pointPtr[])    /* Pointer to x and y coordinates. */
{
    double xDiff, yDiff, x1, y1, x2, y2, inc, tmp;

    /*
     * Generate a new larger rectangle that includes the border
     * width, if there is one.
     */

    inc = width/2.0;
    x1 = rectPtr[0] - inc;
    y1 = rectPtr[1] - inc;
    x2 = rectPtr[2] + inc;
    y2 = rectPtr[3] + inc;

    /*
     * If the point is inside the rectangle, handle specially:
     * distance is 0 if rectangle is filled, otherwise compute
     * distance to nearest edge of rectangle and subtract width
     * of edge.
     */

    if ((pointPtr[0] >= x1) && (pointPtr[0] < x2)
            && (pointPtr[1] >= y1) && (pointPtr[1] < y2)) {
        /* if (filled || (rectPtr->outline.gc == None)) */
        if (filled) {
            return 0.0;
        }
        xDiff = pointPtr[0] - x1;
        tmp = x2 - pointPtr[0];
        if (tmp < xDiff) {
            xDiff = tmp;
        }
        yDiff = pointPtr[1] - y1;
        tmp = y2 - pointPtr[1];
        if (tmp < yDiff) {
            yDiff = tmp;
        }
        if (yDiff < xDiff) {
            xDiff = yDiff;
        }
        xDiff -= width;
        if (xDiff < 0.0) {
            return 0.0;
        }
        return xDiff;
    }

    /*
     * Point is outside rectangle.
     */

    if (pointPtr[0] < x1) {
        xDiff = x1 - pointPtr[0];
    } else if (pointPtr[0] > x2)  {
        xDiff = pointPtr[0] - x2;
    } else {
        xDiff = 0;
    }

    if (pointPtr[1] < y1) {
        yDiff = y1 - pointPtr[1];
    } else if (pointPtr[1] > y2)  {
        yDiff = pointPtr[1] - y2;
    } else {
        yDiff = 0;
    }

    return hypot(xDiff, yDiff);
}

/*
 *--------------------------------------------------------------
 *
 * TkPathRectToArea --
 *
 *    This procedure is called to determine whether an rectangle
 *    lies entirely inside, entirely outside, or overlapping
 *    another given rectangle.
 *
 * Results:
 *    -1 is returned if the rectangle is entirely outside the area
 *    given by rectPtr, 0 if it overlaps, and 1 if it is entirely
 *    inside the given area.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

int
TkPathRectToArea(
    double rectPtr[],     /* Bare rectangle. */
    double width,     /* Width of stroke, or 0. */
    int filled,     /* Is rectangle filled. */
    double *areaPtr)    /* Pointer to array of four coordinates
                         * (x1, y1, x2, y2) describing rectangular
                         * area.  */
{
    double halfWidth = width/2.0;

    if ((areaPtr[2] <= (rectPtr[0] - halfWidth))
            || (areaPtr[0] >= (rectPtr[2] + halfWidth))
            || (areaPtr[3] <= (rectPtr[1] - halfWidth))
            || (areaPtr[1] >= (rectPtr[3] + halfWidth))) {
        return -1;
    }
    if (!filled && (width > 0.0)
            && (areaPtr[0] >= (rectPtr[0] + halfWidth))
            && (areaPtr[1] >= (rectPtr[1] + halfWidth))
            && (areaPtr[2] <= (rectPtr[2] - halfWidth))
            && (areaPtr[3] <= (rectPtr[3] - halfWidth))) {
        return -1;
    }
    if ((areaPtr[0] <= (rectPtr[0] - halfWidth))
            && (areaPtr[1] <= (rectPtr[1] - halfWidth))
            && (areaPtr[2] >= (rectPtr[2] + halfWidth))
            && (areaPtr[3] >= (rectPtr[3] + halfWidth))) {
        return 1;
    }
    return 0;
}

int
TkPathRectToAreaWithMatrix(TkPathRect bbox, TkPathMatrix *mPtr, double *areaPtr)
{
    int rectiLinear = 0;
    double rect[4];

    if (mPtr == NULL) {
        rectiLinear = 1;
        rect[0] = bbox.x1;
        rect[1] = bbox.y1;
        rect[2] = bbox.x2;
        rect[3] = bbox.y2;
    } else if ((fabs(mPtr->b) == 0.0) && (fabs(mPtr->c) == 0.0)) {
        rectiLinear = 1;
        rect[0] = mPtr->a * bbox.x1 + mPtr->tx;
        rect[1] = mPtr->d * bbox.y1 + mPtr->ty;
        rect[2] = mPtr->a * bbox.x2 + mPtr->tx;
        rect[3] = mPtr->d * bbox.y2 + mPtr->ty;
    }
    if (rectiLinear) {
        return TkPathRectToArea(rect, 0.0, 1, areaPtr);
    } else {
        double polyPtr[10];

        /* polyPtr: Points to an array coordinates for closed polygon:  x0, y0, x1, y1, ... */
        /* Construct all four corners. */
        polyPtr[0] = bbox.x1, polyPtr[1] = bbox.y1;
        polyPtr[2] = bbox.x2, polyPtr[3] = bbox.y1;
        polyPtr[4] = bbox.x2, polyPtr[5] = bbox.y2;
        polyPtr[6] = bbox.x1, polyPtr[7] = bbox.y2;
        PathApplyTMatrix(mPtr, polyPtr, polyPtr+1);
        PathApplyTMatrix(mPtr, polyPtr+2, polyPtr+3);
        PathApplyTMatrix(mPtr, polyPtr+4, polyPtr+5);
        PathApplyTMatrix(mPtr, polyPtr+6, polyPtr+7);

        return TkPolygonToArea(polyPtr, 4, areaPtr);
    }
}

double
TkPathRectToPointWithMatrix(TkPathRect bbox, TkPathMatrix *mPtr, double *pointPtr)
{
    int rectiLinear = 0;
    double dist;
    double rect[4];

    if (mPtr == NULL) {
        rectiLinear = 1;
        rect[0] = bbox.x1;
        rect[1] = bbox.y1;
        rect[2] = bbox.x2;
        rect[3] = bbox.y2;
    } else if ((fabs(mPtr->b) == 0.0) && (fabs(mPtr->c) == 0.0)) {
        rectiLinear = 1;
        rect[0] = mPtr->a * bbox.x1 + mPtr->tx;
        rect[1] = mPtr->d * bbox.y1 + mPtr->ty;
        rect[2] = mPtr->a * bbox.x2 + mPtr->tx;
        rect[3] = mPtr->d * bbox.y2 + mPtr->ty;
    }
    if (rectiLinear) {
        dist = TkPathRectToPoint(rect, 0.0, 1, pointPtr);
    } else {
        int intersections, rule;
        double polyPtr[10];

        /* Construct all four corners.
         * First and last must be identical since closed.
         */
        polyPtr[0] = bbox.x1, polyPtr[1] = bbox.y1;
        polyPtr[2] = bbox.x2, polyPtr[3] = bbox.y1;
        polyPtr[4] = bbox.x2, polyPtr[5] = bbox.y2;
        polyPtr[6] = bbox.x1, polyPtr[7] = bbox.y2;
        PathApplyTMatrix(mPtr, polyPtr, polyPtr+1);
        PathApplyTMatrix(mPtr, polyPtr+2, polyPtr+3);
        PathApplyTMatrix(mPtr, polyPtr+4, polyPtr+5);
        PathApplyTMatrix(mPtr, polyPtr+6, polyPtr+7);
        polyPtr[8] = polyPtr[0], polyPtr[9] = polyPtr[1];

        dist = PathPolygonToPointEx(polyPtr, 5, pointPtr, &intersections, &rule);
        if (intersections % 2 == 1) {
            dist = 0.0;
        }
    }
    return dist;
}

/*
 *--------------------------------------------------------------
 *
 * TkPathCanvasItemExConfigure --
 *
 *      Takes care of the custom item configuration of the Tk_PathItemEx
 *    part of any item with style.
 *
 * Results:
 *    Standard Tcl result.
 *
 * Side effects:
 *    None.
 *
 *--------------------------------------------------------------
 */

int
TkPathCanvasItemExConfigure(Tcl_Interp *interp, Tk_PathCanvas canvas, Tk_PathItemEx *itemExPtr, int mask)
{
    Tk_Window tkwin;
    Tk_PathItem *parentPtr;
    Tk_PathItem *itemPtr = (Tk_PathItem *) itemExPtr;
    Tk_PathStyle *stylePtr = &itemExPtr->style;

    tkwin = Tk_PathCanvasTkwin(canvas);
    if (mask & TK_PATH_CORE_OPTION_PARENT) {
    if (TkPathCanvasFindGroup(interp, canvas, itemPtr->parentObj, &parentPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    TkPathCanvasSetParent(parentPtr, itemPtr);
    } else if ((itemPtr->id != 0) && (itemPtr->parentPtr == NULL)) {
    /*
     * If item not root and parent not set we must set it to root by default.
     */
    TkPathCanvasSetParentToRoot(itemPtr);
    }

    /*
     * If we have got a style name it's options take precedence
     * over the actual path configuration options. This is how SVG does it.
     * Good or bad?
     */
    if (mask & TK_PATH_CORE_OPTION_STYLENAME) {
    TkPathStyleInst *styleInst = NULL;

    if (itemExPtr->styleObj != NULL) {
        styleInst = TkPathGetStyle(interp, Tcl_GetString(itemExPtr->styleObj),
            TkPathCanvasStyleTable(canvas), TkPathStyleChangedPrc,
            (ClientData) itemExPtr);
        if (styleInst == NULL) {
        return TCL_ERROR;
        }
    } else {
        styleInst = NULL;
    }
    if (itemExPtr->styleInst != NULL) {
        TkPathFreeStyle(itemExPtr->styleInst);
    }
    itemExPtr->styleInst = styleInst;
    }

    /*
     * Just translate the 'fillObj' (string) to a TkPathColor.
     * We MUST have this last in the chain of custom option checks!
     */
    if (mask & TK_PATH_STYLE_OPTION_FILL) {
    TkPathColor *fillPtr = NULL;

    if (stylePtr->fillObj != NULL) {
        fillPtr = TkPathGetPathColor(interp, tkwin, stylePtr->fillObj,
            TkPathCanvasGradientTable(canvas), TkPathGradientChangedPrc,
            (ClientData) itemExPtr);
        if (fillPtr == NULL) {
        return TCL_ERROR;
        }
    } else {
        fillPtr = NULL;
    }
    /* Free any old and store the new. */
    if (stylePtr->fill != NULL) {
        TkPathFreePathColor(stylePtr->fill);
    }
    stylePtr->fill = fillPtr;
    }
    return TCL_OK;
}

void
TkPathGradientChangedPrc(ClientData clientData, int flags)
{
    Tk_PathItemEx *itemExPtr = (Tk_PathItemEx *)clientData;
    Tk_PathItem *itemPtr = (Tk_PathItem *) itemExPtr;
    Tk_PathStyle *stylePtr = &(itemExPtr->style);

    if (flags) {
    if (flags & TK_PATH_GRADIENT_FLAG_DELETE) {
        TkPathFreePathColor(stylePtr->fill);
        stylePtr->fill = NULL;
        Tcl_DecrRefCount(stylePtr->fillObj);
        stylePtr->fillObj = NULL;
    }
    if (itemPtr->typePtr == &tkPathTypeGroup) {
        TkPathGroupItemConfigured(itemExPtr->canvas, itemPtr,
            TK_PATH_STYLE_OPTION_FILL);
    } else {
        Tk_PathCanvasEventuallyRedraw(itemExPtr->canvas,
            itemExPtr->header.x1, itemExPtr->header.y1,
            itemExPtr->header.x2, itemExPtr->header.y2);
        }
    }
}

void
TkPathStyleChangedPrc(ClientData clientData, int flags)
{
    Tk_PathItemEx *itemExPtr = (Tk_PathItemEx *)clientData;
    Tk_PathItem *itemPtr = (Tk_PathItem *) itemExPtr;

    if (flags) {
    if (flags & TK_PATH_STYLE_FLAG_DELETE) {
        TkPathFreeStyle(itemExPtr->styleInst);
        itemExPtr->styleInst = NULL;
        Tcl_DecrRefCount(itemExPtr->styleObj);
        itemExPtr->styleObj = NULL;
    }
    if (itemPtr->typePtr == &tkPathTypeGroup) {
        TkPathGroupItemConfigured(itemExPtr->canvas, itemPtr,
            TK_PATH_CORE_OPTION_STYLENAME);
        /* Not completely correct... */
    } else {
        Tk_PathCanvasEventuallyRedraw(itemExPtr->canvas,
            itemExPtr->header.x1, itemExPtr->header.y1,
            itemExPtr->header.x2, itemExPtr->header.y2);
        }
    }
}

void
TkPathCompensateScale(Tk_PathItem *itemPtr, int compensate,
    double *originX, double *originY, double *scaleX, double *scaleY)
{
    if (compensate) {
    Tk_PathStyle style;

    style = TkPathCanvasInheritStyle(itemPtr, TK_PATH_MERGESTYLE_NOTFILL);
    if (style.matrixPtr != NULL) {
        TkPathMatrix m;
        PathInverseTMatrix(style.matrixPtr, &m);
        PathApplyTMatrix(&m, originX, originY);
        m.tx = m.ty = 0;
        PathApplyTMatrix(&m, scaleX, scaleY);
    }
    TkPathCanvasFreeInheritedStyle(&style);
    }
}

void
TkPathCompensateTranslate(Tk_PathItem *itemPtr, int compensate,
    double *deltaX, double *deltaY)
{
    if (compensate) {
    Tk_PathStyle style;

    style = TkPathCanvasInheritStyle(itemPtr, TK_PATH_MERGESTYLE_NOTFILL);
    if (style.matrixPtr != NULL) {
        TkPathMatrix m;

        PathInverseTMatrix(style.matrixPtr, &m);
        m.tx = m.ty = 0;
        PathApplyTMatrix(&m, deltaX, deltaY);
    }
    TkPathCanvasFreeInheritedStyle(&style);
    }
}
/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
