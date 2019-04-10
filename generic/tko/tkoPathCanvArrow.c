/*
 * tkoPathCanvArrow.c --
 *
 *    This file implements arrow heads
 *
 * Copyright (c) 2014 OpenSim Ltd., author:Zoltan Bojthe
 *
 * $Id$
 */

#include "tkoPath.h"

#ifdef _MSC_VER
#ifndef isnan
#define isnan(x) ((x) != (x))
#endif
#endif

/*
 * Points in an arrowHead:
 */
#define PTS_IN_ARROW 6
#define DRAWABLE_PTS_IN_ARROW 5
#define ORIG_PT_IN_ARROW 2
#define LINE_PT_IN_ARROW 5

static const double zero = 0.0; // just for NaN
#define NaN (zero/zero)

TkPathAtom *
TkPathMakePathAtomsFromArrow(
    TkPathArrowDescr * arrowDescr)
{
TkPathPoint *coords = arrowDescr->arrowPointsPtr;
TkPathAtom *atomPtr = NULL, *ret = NULL;
    if(coords) {
int i = 0;
        if(isnan(coords[0].x) || isnan(coords[0].y)) {
            i = 1;
        }
        ret = atomPtr = TkPathNewMoveToAtom(coords[i].x, coords[i].y);
        for(i++; i < DRAWABLE_PTS_IN_ARROW; i++) {
            if(isnan(coords[i].x) || isnan(coords[i].y))
                continue;
            atomPtr->nextPtr = TkPathNewLineToAtom(coords[i].x, coords[i].y);
            atomPtr = atomPtr->nextPtr;
        }
    }
    return ret;
}

void
TkPathDisplayArrow(
    Tk_PathCanvas canvas,
    TkPathArrowDescr * arrowDescr,
    Tk_PathStyle * const style,
    TkPathMatrix * mPtr,
    TkPathRect * bboxPtr)
{
    if(arrowDescr->arrowEnabled && arrowDescr->arrowPointsPtr != NULL) {
    Tk_PathStyle arrowStyle = *style;
    TkPathColor fc;
    TkPathAtom *atomPtr;

        TkPathResetTMatrix(ContextOfCanvas(canvas));
        if(arrowDescr->arrowFillRatio > 0.0 && arrowDescr->arrowLength != 0.0) {
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
        atomPtr = TkPathMakePathAtomsFromArrow(arrowDescr);
        TkPathDrawPath(ContextOfCanvas(canvas), atomPtr, &arrowStyle,
            mPtr, bboxPtr);
        TkPathFreeAtoms(atomPtr);
    }
}

void
TkPathPaintArrow(
    TkPathContext context,
    TkPathArrowDescr * arrowDescr,
    Tk_PathStyle * const style,
    TkPathRect * bboxPtr)
{
    if(arrowDescr->arrowEnabled && arrowDescr->arrowPointsPtr != NULL) {
    Tk_PathStyle arrowStyle = *style;
    TkPathColor fc;
    TkPathAtom *atomPtr;

        arrowStyle.matrixPtr = NULL;
        if(arrowDescr->arrowFillRatio > 0.0 && arrowDescr->arrowLength != 0.0) {
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
        atomPtr = TkPathMakePathAtomsFromArrow(arrowDescr);
        if(TkPathMakePath(context, atomPtr, &arrowStyle) == TCL_OK) {
            TkPathPaintPath(context, atomPtr, &arrowStyle, bboxPtr);
        }
        TkPathFreeAtoms(atomPtr);
    }
}

void
TkPathArrowDescrInit(
    TkPathArrowDescr * descrPtr)
{
    descrPtr->arrowEnabled = TK_PATH_ARROWS_OFF;
    descrPtr->arrowLength = (float)8.0;
    descrPtr->arrowWidth = (float)4.0;
    descrPtr->arrowFillRatio = (float)1.0;
    descrPtr->arrowPointsPtr = NULL;
}

void
TkPathIncludeArrowPointsInRect(
    TkPathRect * bbox,
    TkPathArrowDescr * arrowDescrPtr)
{
    if(arrowDescrPtr->arrowEnabled && arrowDescrPtr->arrowPointsPtr) {
int i;

        for(i = 0; i < PTS_IN_ARROW; i++)
            if(!isnan(arrowDescrPtr->arrowPointsPtr[i].x) &&
                !isnan(arrowDescrPtr->arrowPointsPtr[i].y))
                TkPathIncludePointInRect(bbox,
                    arrowDescrPtr->arrowPointsPtr[i].x,
                    arrowDescrPtr->arrowPointsPtr[i].y);
    }
}

void
TkPathIncludeArrowPoints(
    Tk_PathItem * itemPtr,
    TkPathArrowDescr * arrowDescrPtr)
{
    if(arrowDescrPtr->arrowEnabled) {
int i;

        for(i = 0; i < PTS_IN_ARROW; i++)
            if(!isnan(arrowDescrPtr->arrowPointsPtr[i].x) &&
                !isnan(arrowDescrPtr->arrowPointsPtr[i].y))
                TkPathIncludePoint(itemPtr,
                    (double *)&arrowDescrPtr->arrowPointsPtr[i]);
    }
}

void
TkPathPreconfigureArrow(
    TkPathPoint * pf,
    TkPathArrowDescr * arrowDescr)
{
    if(arrowDescr->arrowPointsPtr == NULL) {
        if(arrowDescr->arrowEnabled) {
            arrowDescr->arrowPointsPtr = (TkPathPoint *)
                ckalloc((unsigned)(PTS_IN_ARROW * sizeof(TkPathPoint)));
            arrowDescr->arrowPointsPtr[LINE_PT_IN_ARROW] = *pf;
            arrowDescr->arrowPointsPtr[ORIG_PT_IN_ARROW] = *pf;
        }
    } else {
        if(pf->x == arrowDescr->arrowPointsPtr[LINE_PT_IN_ARROW].x &&
            pf->y == arrowDescr->arrowPointsPtr[LINE_PT_IN_ARROW].y) {
            *pf = arrowDescr->arrowPointsPtr[ORIG_PT_IN_ARROW];
        }
        if(!arrowDescr->arrowEnabled) {
            ckfree((char *)arrowDescr->arrowPointsPtr);
            arrowDescr->arrowPointsPtr = NULL;
        }
    }
}

TkPathPoint
TkPathConfigureArrow(
    TkPathPoint pf,
    TkPathPoint pl,
    TkPathArrowDescr * arrowDescr,
    Tk_PathStyle * lineStyle,
    int updateFirstPoint)
{
    if(arrowDescr->arrowEnabled) {
    TkPathPoint p0;
    double lineWidth = lineStyle->strokeWidth;
    double shapeLength = arrowDescr->arrowLength;
    double shapeWidth = arrowDescr->arrowWidth;
    double shapeFill = arrowDescr->arrowFillRatio;
    double dx, dy, length, sinTheta, cosTheta;
    double backup;             /* Distance to backup end points so the line
                                * ends in the middle of the arrowhead. */
    double minsShapeFill;
    TkPathPoint *poly = arrowDescr->arrowPointsPtr;
    int capStyle = lineStyle->capStyle;
        /*  CapButt, CapProjecting, or CapRound. */

        if(!poly) {
            Tcl_Panic("Internal error: TkPathPoint list is NULL pointer\n");
        }
        if(shapeWidth < lineWidth) {
            shapeWidth = lineWidth;
        }
        minsShapeFill = lineWidth * shapeLength / shapeWidth;
        if(shapeFill > 0.0 &&
            fabs(shapeLength * shapeFill) < fabs(minsShapeFill))
            shapeFill = 1.1 * minsShapeFill / shapeLength;

        backup = 0.0;
        if(lineWidth > 1.0) {
            backup = (capStyle == CapProjecting) ? 0.5 * lineWidth : 0.0;
            if(shapeFill > 0.0 && shapeLength != 0.0) {
                backup += 0.5 * lineWidth * shapeLength / shapeWidth;
            }
        }

        dx = pf.x - pl.x;
        dy = pf.y - pl.y;
        length = hypot(dx, dy);
        if(length == 0) {
            sinTheta = cosTheta = 0.0;
        } else {
            sinTheta = dy / length;
            cosTheta = dx / length;
        }

        p0.x = pf.x - shapeLength * cosTheta;
        p0.y = pf.y - shapeLength * sinTheta;
        if(shapeFill > 0.0 && shapeLength != 0.0) {
            poly[0].x = pf.x - shapeLength * shapeFill * cosTheta;
            poly[0].y = pf.y - shapeLength * shapeFill * sinTheta;
            poly[4] = poly[0];
        } else {
            poly[0].x = poly[0].y = poly[4].x = poly[4].y = NaN;
        }
        poly[1].x = p0.x - shapeWidth * sinTheta;
        poly[1].y = p0.y + shapeWidth * cosTheta;
        poly[2].x = pf.x;
        poly[2].y = pf.y;
        poly[3].x = p0.x + shapeWidth * sinTheta;
        poly[3].y = p0.y - shapeWidth * cosTheta;
        /*
         * Polygon done. Now move the first point towards the second so that
         * the corners at the end of the line are inside the arrowhead.
         */

        poly[LINE_PT_IN_ARROW] = poly[ORIG_PT_IN_ARROW];
        if(updateFirstPoint) {
            poly[LINE_PT_IN_ARROW].x -= backup * cosTheta;
            poly[LINE_PT_IN_ARROW].y -= backup * sinTheta;
        }

        return poly[LINE_PT_IN_ARROW];
    }
    return pf;
}

void
TkPathTranslateArrow(
    TkPathArrowDescr * arrowDescr,
    double deltaX,
    double deltaY)
{
    if(arrowDescr->arrowPointsPtr != NULL) {
    int i;
        for(i = 0; i < PTS_IN_ARROW; i++) {
            arrowDescr->arrowPointsPtr[i].x += deltaX;
            arrowDescr->arrowPointsPtr[i].y += deltaY;
        }
    }
}

void
TkPathScaleArrow(
    TkPathArrowDescr * arrowDescr,
    double originX,
    double originY,
    double scaleX,
    double scaleY)
{
    if(arrowDescr->arrowPointsPtr != NULL) {
    int i;
    TkPathPoint *pt;
        for(i = 0, pt = arrowDescr->arrowPointsPtr; i < PTS_IN_ARROW; i++, pt++) {
            pt->x = originX + scaleX * (pt->x - originX);
            pt->y = originX + scaleX * (pt->y - originX);
        }
    }
}

void
TkPathFreeArrow(
    TkPathArrowDescr * arrowDescr)
{
    if(arrowDescr->arrowPointsPtr != NULL) {
        ckfree((char *)arrowDescr->arrowPointsPtr);
        arrowDescr->arrowPointsPtr = NULL;
    }
}

typedef TkPathPoint *PathPointPtr;

int
TkPathGetSegmentsFromPathAtomList(
    TkPathAtom * firstAtom,
    TkPathPoint ** firstPt,
    TkPathPoint * secondPt,
    TkPathPoint * penultPt,
    TkPathPoint ** lastPt)
{
TkPathAtom *atom;
int i;

    *firstPt = *lastPt = NULL;
    secondPt->x = secondPt->y = penultPt->x = penultPt->y = NaN;

    if(firstAtom && firstAtom->type != TK_PATH_ATOM_M) {
        Tcl_Panic("Invalid path! Path must start with M(move) atom");
    }
    for(i = 0, atom = firstAtom; atom; atom = atom->nextPtr) {
        switch (atom->type) {
        case TK_PATH_ATOM_M:
        {
TkMoveToAtom *moveto = (TkMoveToAtom *) atom;
            if(i == 0) {
                *firstPt = (PathPointPtr) & moveto->x;
                i++;
            } else if(i == 1) {
                secondPt->x = moveto->x;
                secondPt->y = moveto->y;
                i++;
            }
            penultPt->x = penultPt->y = NaN;
            *lastPt = (PathPointPtr) & moveto->x;
            break;
        }
        case TK_PATH_ATOM_L:
        {
TkLineToAtom *lineto = (TkLineToAtom *) atom;
            if(i == 1) {
                secondPt->x = lineto->x;
                secondPt->y = lineto->y;
                i++;
            }
            *penultPt = **lastPt;
            *lastPt = (PathPointPtr) & lineto->x;
            break;
        }
        case TK_PATH_ATOM_A:{
TkArcAtom *arc = (TkArcAtom *) atom;

            /*
             * Draw an elliptical arc from the current point to (x, y).
             * The points are on an ellipse with x-radius <radX> and
             * y-radius <radY>. The ellipse is rotated by <angle> degrees.
             * If the arc is less than 180 degrees, <largeArcFlag> is
             * zero, else it is one. If the arc is to be drawn in cw
             * direction, sweepFlag is one, and zero for the ccw
             * direction.
             * NB: the start and end points may not coincide else the
             * result is undefined. If you want to make a circle just
             * do two 180 degree arcs.
             */
int result;
double cx, cy, rx, ry;
double theta1, dtheta;
TkPathPoint startPt = **lastPt;
double phi = DEGREES_TO_RADIANS * arc->angle;

            result = TkPathEndpointToCentralArcParameters(startPt.x, startPt.y,
                arc->x, arc->y, arc->radX, arc->radY,
                phi,
                arc->largeArcFlag, arc->sweepFlag,
                &cx, &cy, &rx, &ry, &theta1, &dtheta);
            if(result == TK_PATH_ARC_OK) {
double sinTheta2, cosTheta2;
double sinPhi = sin(phi);
double cosPhi = cos(phi);
double theta2 = theta1 + dtheta;

                if(dtheta > 0.0) {
                    theta1 += M_PI * 0.01;
                    theta2 -= M_PI * 0.01;
                } else {
                    theta1 -= M_PI * 0.01;
                    theta2 += M_PI * 0.01;
                }

                sinTheta2 = sin(theta2);
                cosTheta2 = cos(theta2);

                if(i == 1) {
double sinTheta1 = sin(theta1);
double cosTheta1 = cos(theta1);
                    /* auxiliary point 1 */
                    secondPt->x = cx + rx * cosTheta1 * cosPhi -
                        ry * sinTheta1 * sinPhi;
                    secondPt->y = cy + rx * cosTheta1 * sinPhi +
                        ry * sinTheta1 * cosPhi;
                    i++;
                }
                /* auxiliary point 2 */
                penultPt->x = cx + rx * cosTheta2 * cosPhi -
                    ry * sinTheta2 * sinPhi;
                penultPt->y = cy + rx * cosTheta2 * sinPhi +
                    ry * sinTheta2 * cosPhi;
            } else {
                /* arc is line */
                if(i == 1) {
                    secondPt->x = arc->x;
                    secondPt->y = arc->y;
                    i++;
                }
                *penultPt = **lastPt;
            }

            *lastPt = (PathPointPtr) & arc->x;
            break;
        }
        case TK_PATH_ATOM_Q:{
TkQuadBezierAtom *quad = (TkQuadBezierAtom *) atom;
            if(i == 1) {
                secondPt->x = quad->ctrlX;
                secondPt->y = quad->ctrlY;
                i++;
            }
            penultPt->x = quad->ctrlX;
            penultPt->y = quad->ctrlY;
            *lastPt = (PathPointPtr) & quad->anchorX;
            break;
        }
        case TK_PATH_ATOM_C:{
TkCurveToAtom *curve = (TkCurveToAtom *) atom;
            if(i == 1) {
                secondPt->x = curve->ctrlX1;
                secondPt->y = curve->ctrlY1;
                i++;
            }
            penultPt->x = curve->ctrlX2;
            penultPt->y = curve->ctrlY2;
            *lastPt = (PathPointPtr) & curve->anchorX;
            break;
        }
        case TK_PATH_ATOM_Z:{
TkCloseAtom *closeAtom = (TkCloseAtom *) atom;
            *penultPt = **lastPt;
            *lastPt = (PathPointPtr) & closeAtom->x;
            break;
        }
        case TK_PATH_ATOM_ELLIPSE:
        case TK_PATH_ATOM_RECT:{
            /* Empty. */
            break;
        }
        default:
            break;
        }
    }
    return (i >= 2) ? TCL_OK : TCL_ERROR;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
