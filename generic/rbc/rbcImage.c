/*
 * rbcImage.c --
 *
 *      This module implements image processing procedures for the rbc
 *      toolkit.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

#define NC		256
enum ColorIndices { RED, GREEN, BLUE };

#define R0	(cubePtr->r0)
#define R1	(cubePtr->r1)
#define G0	(cubePtr->g0)
#define G1	(cubePtr->g1)
#define B0	(cubePtr->b0)
#define B1	(cubePtr->b1)

typedef struct {
    int             r0, r1;     /* min, max values:
                                 * min exclusive max inclusive */
    int             g0, g1;
    int             b0, b1;
    int             vol;
} Cube;

/*
 *----------------------------------------------------------------------
 *
 * Histogram is in elements 1..HISTSIZE along each axis,
 * element 0 is for base or marginal value
 * NB: these must start out 0!
 *----------------------------------------------------------------------
 */
typedef struct {
    long int        wt[33][33][33];     /* # pixels in voxel */
    long int        mR[33][33][33];     /* Sum over voxel of red pixel values */
    long int        mG[33][33][33];     /* Sum over voxel of green pixel values */
    long int        mB[33][33][33];     /* Sum over voxel of blue pixel values */
    long int        gm2[33][33][33];    /* Variance */
} ColorImageStatistics;

static void     ZoomImageVertically(
    RbcColorImage * src,
    RbcColorImage * dest,
    RbcResampleFilter * filterPtr);
static void     ZoomImageHorizontally(
    RbcColorImage * src,
    RbcColorImage * dest,
    RbcResampleFilter * filterPtr);
static void     ShearY(
    RbcColorImage * src,
    RbcColorImage * dest,
    int y,
    int offset,
    double frac,
    RbcPix32 bgColor);
static void     ShearX(
    RbcColorImage * src,
    RbcColorImage * dest,
    int x,
    int offset,
    double frac,
    RbcPix32 bgColor);
static RbcColorImage *Rotate45(
    RbcColorImage * src,
    double theta,
    RbcPix32 bgColor);
static RbcColorImage *CopyColorImage(
    RbcColorImage * src);
static RbcColorImage *Rotate180(
    RbcColorImage * src);
static RbcColorImage *Rotate270(
    RbcColorImage * src);
static ColorImageStatistics *GetColorImageStatistics(
    RbcColorImage * image);
static void     M3d(
    ColorImageStatistics * s);
static long int Volume(
    Cube * cubePtr,
    long int m[33][33][33]);
static long int Bottom(
    Cube * cubePtr,
    unsigned char dir,
    long int m[33][33][33]);
static long int Top(
    Cube * cubePtr,
    unsigned char dir,
    int pos,
    long int m[33][33][33]);
static double   Variance(
    Cube * cubePtr,
    ColorImageStatistics * s);
static double   Maximize(
    Cube * cubePtr,
    unsigned char dir,
    int first,
    int last,
    int *cut,
    long int rWhole,
    long int gWhole,
    long int bWhole,
    long int wWhole,
    ColorImageStatistics * s);
static int      Cut(
    Cube * set1,
    Cube * set2,
    ColorImageStatistics * s);
static int      SplitColorSpace(
    ColorImageStatistics * s,
    Cube * cubes,
    int nColors);
static void     Mark(
    Cube * cubePtr,
    int label,
    unsigned int lut[33][33][33]);
static void     CreateColorLookupTable(
    ColorImageStatistics * s,
    Cube * cubes,
    int nColors,
    unsigned int lut[33][33][33]);
static void     MapColors(
    RbcColorImage * src,
    RbcColorImage * dest,
    unsigned int lut[33][33][33]);
static RbcPix32 *ColorImagePixel(RbcColorImage *imagePtr, int x, int y);


RbcResampleFilter *rbcBoxFilterPtr;     /* The ubiquitous box filter */

/*
 *----------------------------------------------------------------------
 *
 * RbcCreateColorImage --
 *
 *      Allocates a color image of a designated height and width.
 *
 *      This routine will be augmented with other types of information
 *      such as a color table, etc.
 *
 * Results:
 *      Returns the new color image.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcColorImage  *
RbcCreateColorImage(
    int width,                  /* Dimensions of new image */
    int height)
{                               /* Dimensions of new image */
    RbcColorImage  *imagePtr;
    size_t          size;

    size = width * height;
    imagePtr = (RbcColorImage *) ckalloc(sizeof(RbcColorImage));
    assert(imagePtr);
    imagePtr->bits = (RbcPix32 *) ckalloc(sizeof(RbcPix32) * size);
    assert(imagePtr->bits);

    imagePtr->width = width;
    imagePtr->height = height;
    return imagePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcFreeColorImage --
 *
 *      Deallocates the given color image.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcFreeColorImage(
    RbcColorImage * imagePtr)
{
    ckfree((char *) imagePtr->bits);
    ckfree((char *) imagePtr);
}

/*
 *--------------------------------------------------------------
 *
 * RbcGetPlatformId --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcGammaCorrectColorImage(
    RbcColorImage * src,
    double newGamma)
{
    unsigned int    nPixels;
    register RbcPix32 *srcPtr, *endPtr;
    register unsigned int i;
    double          value;
    unsigned char   lut[256];
    double          invGamma;

    invGamma = 1.0 / newGamma;
    for (i = 0; i < 256; i++) {
        value = 255.0 * pow((double) i / 255.0, invGamma);
        lut[i] = CLAMP((unsigned char) value, 0, 255);
    }
    nPixels = src->width * src->height;
    srcPtr = src->bits;
    for (endPtr = srcPtr + nPixels; srcPtr < endPtr; srcPtr++) {
        srcPtr->rgba.red = lut[srcPtr->rgba.red];
        srcPtr->rgba.green = lut[srcPtr->rgba.green];
        srcPtr->rgba.blue = lut[srcPtr->rgba.blue];
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcColorImageToGreyscale --
 *
 *      Converts a color image to PostScript grey scale (1 component)
 *      output.  Luminosity isn't computed using the old NTSC formula,
 *
 *        Y = 0.299 * Red + 0.587 * Green + 0.114 * Blue
 *
 *      but the following
 *
 *        Y = 0.212671 * Red + 0.715160 * Green + 0.072169 * Blue
 *
 *      which better represents contemporary monitors.
 *
 * Results:
 *      The color image is converted to greyscale.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcColorImageToGreyscale(
    RbcColorImage * image)
{
    register RbcPix32 *srcPtr, *endPtr;
    double          Y;
    int             nPixels;
    int             width, height;

    width = image->width;
    height = image->height;
    nPixels = width * height;
    srcPtr = image->bits;
    for (endPtr = srcPtr + nPixels; srcPtr < endPtr; srcPtr++) {
        Y = ((0.212671 * (double) srcPtr->rgba.red) +
            (0.715160 * (double) srcPtr->rgba.green) +
            (0.072169 * (double) srcPtr->rgba.blue));
        srcPtr->rgba.red = srcPtr->rgba.green = srcPtr->rgba.blue =
            (unsigned char) CLAMP((unsigned char) Y, 0, 255);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcColorImageToPhoto --
 *
 *      Translates a color image into a Tk photo.
 *
 * Results:
 *      The photo is re-written with the new color image.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcColorImageToPhoto(
    Tcl_Interp * interp,
    RbcColorImage * src,        /* Image to use as source */
    Tk_PhotoHandle photo)
{                               /* Photo to write color image into */
    Tk_PhotoImageBlock dest;
    int             width, height;

    width = src->width;
    height = src->height;

    Tk_PhotoGetImage(photo, &dest);
    dest.pixelSize = sizeof(RbcPix32);
    dest.pitch = sizeof(RbcPix32) * width;
    dest.width = width;
    dest.height = height;
    dest.offset[0] = Tk_Offset(RbcPix32, rgba.red);
    dest.offset[1] = Tk_Offset(RbcPix32, rgba.green);
    dest.offset[2] = Tk_Offset(RbcPix32, rgba.blue);
    dest.offset[3] = Tk_Offset(RbcPix32, rgba.alpha);
    dest.pixelPtr = (unsigned char *) src->bits;
    Tk_PhotoSetSize(interp, photo, width, height);
    Tk_PhotoPutBlock(interp, photo, &dest, 0, 0, width, height,
        TK_PHOTO_COMPOSITE_OVERLAY);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcPhotoRegionToColorImage --
 *
 *      Create a photo to a color image.
 *
 * Results:
 *      The new color image is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcColorImage  *
RbcPhotoRegionToColorImage(
    Tk_PhotoHandle photo,       /* Source photo image to scale */
    int x,
    int y,
    int width,
    int height)
{
    Tk_PhotoImageBlock src;
    RbcColorImage  *image;
    register RbcPix32 *destPtr;
    register unsigned char *srcData;
    register int    offset;
    unsigned int    offR, offG, offB, offA;

    Tk_PhotoGetImage(photo, &src);
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (width < 0) {
        width = src.width;
    }
    if (height < 0) {
        height = src.height;
    }
    if ((x + width) > src.width) {
        width = src.width - x;
    }
    if ((height + y) > src.height) {
        height = src.width - y;
    }
    image = RbcCreateColorImage(width, height);
    destPtr = image->bits;

    offset = (x * src.pixelSize) + (y * src.pitch);

    offR = src.offset[0];
    offG = src.offset[1];
    offB = src.offset[2];
    offA = src.offset[3];

    if (src.pixelSize == 4) {
        for (y = 0; y < height; y++) {
            srcData = src.pixelPtr + offset;
            for (x = 0; x < width; x++) {
                destPtr->rgba.red = srcData[offR];
                destPtr->rgba.green = srcData[offG];
                destPtr->rgba.blue = srcData[offB];
                destPtr->rgba.alpha = srcData[offA];
                srcData += src.pixelSize;
                destPtr++;
            }
            offset += src.pitch;
        }
    } else if (src.pixelSize == 3) {
        for (y = 0; y < height; y++) {
            srcData = src.pixelPtr + offset;
            for (x = 0; x < width; x++) {
                destPtr->rgba.red = srcData[offR];
                destPtr->rgba.green = srcData[offG];
                destPtr->rgba.blue = srcData[offB];
                /* No transparency information */
                destPtr->rgba.alpha = (unsigned char) -1;
                srcData += src.pixelSize;
                destPtr++;
            }
            offset += src.pitch;
        }
    } else {
        for (y = 0; y < height; y++) {
            srcData = src.pixelPtr + offset;
            for (x = 0; x < width; x++) {
                destPtr->rgba.red = destPtr->rgba.green = destPtr->rgba.blue =
                    srcData[offA];
                /* No transparency information */
                destPtr->rgba.alpha = (unsigned char) -1;
                srcData += src.pixelSize;
                destPtr++;
            }
            offset += src.pitch;
        }
    }
    return image;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcPhotoToColorImage --
 *
 *      Create a photo to a color image.
 *
 * Results:
 *      The new color image is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcColorImage  *
RbcPhotoToColorImage(
    Tk_PhotoHandle photo)
{                               /* Source photo image to scale */
    RbcColorImage  *image;
    Tk_PhotoImageBlock src;
    int             width, height;
    register RbcPix32 *destPtr;
    register int    offset;
    register int    x, y;
    register unsigned char *srcData;

    Tk_PhotoGetImage(photo, &src);
    width = src.width;
    height = src.height;
    image = RbcCreateColorImage(width, height);
    destPtr = image->bits;
    offset = 0;
    if (src.pixelSize == 4) {
        for (y = 0; y < height; y++) {
            srcData = src.pixelPtr + offset;
            for (x = 0; x < width; x++) {
                destPtr->rgba.red = srcData[src.offset[0]];
                destPtr->rgba.green = srcData[src.offset[1]];
                destPtr->rgba.blue = srcData[src.offset[2]];
                destPtr->rgba.alpha = srcData[src.offset[3]];
                srcData += src.pixelSize;
                destPtr++;
            }
            offset += src.pitch;
        }
    } else if (src.pixelSize == 3) {
        for (y = 0; y < height; y++) {
            srcData = src.pixelPtr + offset;
            for (x = 0; x < width; x++) {
                destPtr->rgba.red = srcData[src.offset[0]];
                destPtr->rgba.green = srcData[src.offset[1]];
                destPtr->rgba.blue = srcData[src.offset[2]];
                /* No transparency information */
                destPtr->rgba.alpha = (unsigned char) -1;
                srcData += src.pixelSize;
                destPtr++;
            }
            offset += src.pitch;
        }
    } else {
        for (y = 0; y < height; y++) {
            srcData = src.pixelPtr + offset;
            for (x = 0; x < width; x++) {
                destPtr->rgba.red = destPtr->rgba.green = destPtr->rgba.blue =
                    srcData[src.offset[0]];
                /* No transparency information */
                destPtr->rgba.alpha = (unsigned char) -1;
                srcData += src.pixelSize;
                destPtr++;
            }
            offset += src.pitch;
        }
    }
    return image;
}

/*
 *	filter function definitions
 */

static ResampleFilterProc DefaultFilter;
static ResampleFilterProc BellFilter;
static ResampleFilterProc BesselFilter;
static ResampleFilterProc BoxFilter;
static ResampleFilterProc BSplineFilter;
static ResampleFilterProc CatRomFilter;
static ResampleFilterProc DummyFilter;
static ResampleFilterProc GaussianFilter;
static ResampleFilterProc GiFilter;
static ResampleFilterProc Lanczos3Filter;
static ResampleFilterProc MitchellFilter;
static ResampleFilterProc SincFilter;
static ResampleFilterProc TriangleFilter;
static Tk_ImageChangedProc TempImageChangedProc;

/*
 *--------------------------------------------------------------
 *
 * DefaultFilter --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
DefaultFilter(
    double x)
{
    if (x < 0.0) {
        x = -x;
    }
    if (x < 1.0) {
        /* f(x) = 2x^3 - 3x^2 + 1, -1 <= x <= 1 */
        return (2.0 * x - 3.0) * x * x + 1.0;
    }
    return 0.0;
}

/*
 *--------------------------------------------------------------
 *
 * DummyFilter --
 *
 *      Just for testing...
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
DummyFilter(
    double x)
{
    return FABS(x);
}

/*
 *
 * Finite filters in increasing order:
 *
 *      Box (constant)
 *      Triangle (linear)
 *      Bell
 *      BSpline (cubic)
 *
 */

/*
 *--------------------------------------------------------------
 *
 * BoxFilter --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
BoxFilter(
    double x)
{
    if ((x < -0.5) || (x > 0.5)) {
        return 0.0;
    }
    return 1.0;
}

/*
 *--------------------------------------------------------------
 *
 * TriangleFilter --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
TriangleFilter(
    double x)
{
    if (x < 0.0) {
        x = -x;
    }
    if (x < 1.0) {
        return (1.0 - x);
    }
    return 0.0;
}

/*
 *--------------------------------------------------------------
 *
 * BellFilter --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
BellFilter(
    double x)
{
    if (x < 0.0) {
        x = -x;
    }
    if (x < 0.5) {
        return (0.75 - (x * x));
    }
    if (x < 1.5) {
        x = (x - 1.5);
        return (0.5 * (x * x));
    }
    return 0.0;
}

/*
 *--------------------------------------------------------------
 *
 * BSplineFilter --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
BSplineFilter(
    double x)
{
    double          x2;

    if (x < 0.0) {
        x = -x;
    }
    if (x < 1) {
        x2 = x * x;
        return ((.5 * x2 * x) - x2 + (2.0 / 3.0));
    } else if (x < 2) {
        x = 2 - x;
        return ((x * x * x) / 6.0);
    }
    return 0.0;
}

/*
 *
 * Infinite Filters:
 *      Sinc		perfect lowpass filter
 *      Bessel		circularly symmetric 2-D filter
 *      Gaussian
 *      Lanczos3
 *      Mitchell
 */

/*
 *--------------------------------------------------------------
 *
 * SincFilter --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
SincFilter(
    double x)
{
    x *= M_PI;
    if (x == 0.0) {
        return 1.0;
    }
    return (sin(x) / x);
}

/*
 *--------------------------------------------------------------
 *
 * BesselFilter --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
BesselFilter(
    double x)
{
    /*
     * See Pratt "Digital Image Processing" p. 97 for Bessel functions
     * zeros are at approx x=1.2197, 2.2331, 3.2383, 4.2411, 5.2428, 6.2439,
     * 7.2448, 8.2454
     */
#ifdef __BORLANDC__
    return 0.0;
#else
    return (x == 0.0) ? M_PI / 4.0 : j1(M_PI * x) / (x + x);
#endif
}

#define SQRT_2PI	0.79788456080286541     /* sqrt(2.0 / M_PI) */

/*
 *--------------------------------------------------------------
 *
 * GaussianFilter --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
GaussianFilter(
    double x)
{
    return exp(-2.0 * x * x) * SQRT_2PI;
}

/*
 *--------------------------------------------------------------
 *
 * Lanczos3Filter --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
Lanczos3Filter(
    double x)
{
    if (x < 0) {
        x = -x;
    }
    if (x < 3.0) {
        return (SincFilter(x) * SincFilter(x / 3.0));
    }
    return 0.0;
}

#define	B		0.3333333333333333      /* (1.0 / 3.0) */
#define	C		0.3333333333333333      /* (1.0 / 3.0) */

/*
 *--------------------------------------------------------------
 *
 * MitchellFilter --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
MitchellFilter(
    double x)
{
    double          x2;

    x2 = x * x;
    if (x < 0) {
        x = -x;
    }
    if (x < 1.0) {
        x = (((12.0 - 9.0 * B - 6.0 * C) * (x * x2)) +
            ((-18.0 + 12.0 * B + 6.0 * C) * x2) + (6.0 - 2 * B));
        return (x / 6.0);
    } else if (x < 2.0) {
        x = (((-1.0 * B - 6.0 * C) * (x * x2)) + ((6.0 * B + 30.0 * C) * x2) +
            ((-12.0 * B - 48.0 * C) * x) + (8.0 * B + 24 * C));
        return (x / 6.0);
    }
    return 0.0;
}

/*
 *--------------------------------------------------------------
 *
 * CatRomFilter --
 *
 *      Catmull-Rom spline
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
CatRomFilter(
    double x)
{
    if (x < -2.) {
        return 0.0;
    }
    if (x < -1.0) {
        return 0.5 * (4.0 + x * (8.0 + x * (5.0 + x)));
    }
    if (x < 0.0) {
        return 0.5 * (2.0 + x * x * (-5.0 + x * -3.0));
    }
    if (x < 1.0) {
        return 0.5 * (2.0 + x * x * (-5.0 + x * 3.0));
    }
    if (x < 2.0) {
        return 0.5 * (4.0 + x * (-8.0 + x * (5.0 - x)));
    }
    return 0.0;
}

/*
 *--------------------------------------------------------------
 *
 * GiFilter --
 *
 *      Approximation to the gaussian integral [x, inf)
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
GiFilter(
    double x)
{
    if (x > 1.5) {
        return 0.0;
    } else if (x < -1.5) {
        return 1.0;
    } else {
#define I6 0.166666666666667
#define I4 0.25
#define I3 0.333333333333333
        double          x2 = x * x;
        double          x3 = x2 * x;

        if (x > 0.5) {
            return .5625 - (x3 * I6 - 3 * x2 * I4 + 1.125 * x);
        } else if (x > -0.5) {
            return 0.5 - (0.75 * x - x3 * I3);
        } else {
            return 0.4375 + (-x3 * I6 - 3 * x2 * I4 - 1.125 * x);
        }
    }
}

static RbcResampleFilter filterTable[] = {
    /* name,     function,              support */
    {"bell", BellFilter, 1.5},
    {"bessel", BesselFilter, 3.2383},
    {"box", BoxFilter, 0.5},
    {"bspline", BSplineFilter, 2.0},
    {"catrom", CatRomFilter, 2.0},
    {"default", DefaultFilter, 1.0},
    {"dummy", DummyFilter, 0.5},
    {"gauss8", GaussianFilter, 8.0},
    {"gaussian", GaussianFilter, 1.25},
    {"gi", GiFilter, 1.25},
    {"lanczos3", Lanczos3Filter, 3.0},
    {"mitchell", MitchellFilter, 2.0},
    {"none", (ResampleFilterProc *) NULL, 0.0},
    {"sinc", SincFilter, 4.0},
    {"triangle", TriangleFilter, 1.0},
};

static int      nFilters = sizeof(filterTable) / sizeof(RbcResampleFilter);

RbcResampleFilter *rbcBoxFilterPtr = &(filterTable[1]);

/*
 *----------------------------------------------------------------------
 *
 * RbcGetResampleFilter --
 *
 *      Finds a 1-D filter associated by the given filter name.
 *
 * Results:
 *      A standard Tcl result.  Returns TCL_OK is the filter was
 *      found.  The filter information (proc and support) is returned
 *      via filterPtrPtr. Otherwise TCL_ERROR is returned and an error
 *      message is left in interp->result.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int
RbcGetResampleFilter(
    Tcl_Interp * interp,
    char *name,
    RbcResampleFilter ** filterPtrPtr)
{
    RbcResampleFilter *filterPtr, *endPtr;

    endPtr = filterTable + nFilters;
    for (filterPtr = filterTable; filterPtr < endPtr; filterPtr++) {
        if (strcmp(name, filterPtr->name) == 0) {
            *filterPtrPtr = (filterPtr->proc == NULL) ? NULL : filterPtr;
            return TCL_OK;
        }
    }
    Tcl_AppendResult(interp, "can't find filter \"", name, "\"", (char *) NULL);
    return TCL_ERROR;
}

/*
 * Scaled integers are fixed point values.  The upper 18 bits is the integer
 * portion, the lower 14 bits the fractional remainder.  Must be careful
 * not to overflow the values (especially during multiplication).
 *
 * The following operations are defined:
 *
 *	S * n		Scaled integer times an integer.
 *	S1 + S2		Scaled integer plus another scaled integer.
 *
 */

#define float2si(f)	(int)((f) * 16384.0 + 0.5)
#define uchar2si(b)	(((int)(b)) << 14)
#define si2int(s)	(((s) + 8192) >> 14)

typedef union {
    int             i;          /* Fixed point, scaled integer. */
    float           f;
} Weight;

typedef struct {
    int             count;      /* Number of samples. */
    int             start;
    Weight          weights[1]; /* Array of weights. */
} Sample;

/*
 *--------------------------------------------------------------
 *
 * ComputeWeights --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static          size_t
ComputeWeights(
    int srcWidth,
    int destWidth,
    RbcResampleFilter * filterPtr,
    Sample ** samplePtrPtr)
{
    Sample         *samples;
    double          scale;
    int             filterSize;
    double          center;
    register Sample *s;
    register Weight *weight;
    register int    x, i;
    register int    left, right;        /* filter bounds */
    double          factor, sum;
    size_t          size;

    /* Pre-calculate filter contributions for a row */
    scale = (double) destWidth / (double) srcWidth;

    if (scale < 1.0) {
        double          radius, fscale;

        /* Downsample */

        radius = filterPtr->support / scale;
        fscale = 1.0 / scale;
        filterSize = (int) (radius * 2 + 2);

        size = sizeof(Sample) + (filterSize - 1) * sizeof(Weight);
        samples = RbcCalloc(destWidth, size);
        assert(samples);

        s = samples;
        for (x = 0; x < destWidth; x++) {
            center = (double) x *fscale;

            /* Determine bounds of filter and its density */
            left = (int) (center - radius + 0.5);
            if (left < 0) {
                left = 0;
            }
            right = (int) (center + radius + 0.5);
            if (right >= srcWidth) {
                right = srcWidth - 1;
            }
            sum = 0.0;
            s->start = left;
            for (weight = s->weights, i = left; i <= right; i++, weight++) {
                weight->f = (float)
                    (*filterPtr->proc) (((double) i + 0.5 - center) * scale);
                sum += weight->f;
            }
            s->count = right - left + 1;

            factor = (sum == 0.0) ? 1.0 : (1.0 / sum);
            for (weight = s->weights, i = left; i <= right; i++, weight++) {
                weight->f = (float) (weight->f * factor);
                weight->i = float2si(weight->f);
            }
            s = (Sample *) ((char *) s + size);
        }
    } else {
        double          fscale;
        /* Upsample */

        filterSize = (int) (filterPtr->support * 2 + 2);
        size = sizeof(Sample) + (filterSize - 1) * sizeof(Weight);
        samples = RbcCalloc(destWidth, size);
        assert(samples);

        fscale = 1.0 / scale;

        s = samples;
        for (x = 0; x < destWidth; x++) {
            center = (double) x *fscale;
            left = (int) (center - filterPtr->support + 0.5);
            if (left < 0) {
                left = 0;
            }
            right = (int) (center + filterPtr->support + 0.5);
            if (right >= srcWidth) {
                right = srcWidth - 1;
            }
            sum = 0.0;
            s->start = left;
            for (weight = s->weights, i = left; i <= right; i++, weight++) {
                weight->f = (float)
                    (*filterPtr->proc) ((double) i - center + 0.5);
                sum += weight->f;
            }
            s->count = right - left + 1;
            factor = (sum == 0.0) ? 1.0 : (1.0 / sum);
            for (weight = s->weights, i = left; i <= right; i++, weight++) {
                weight->f = (float) (weight->f * factor);
                weight->i = float2si(weight->f);
            }
            s = (Sample *) ((char *) s + size);
        }
    }
    *samplePtrPtr = samples;
    return size;
}

/*
 * The following macro converts a fixed-point scaled integer to a
 * byte, clamping the value between 0 and 255.
 */
#define SICLAMP(s) \
    (unsigned char)(((s) < 0) ? 0 : ((s) > 4177920) ? 255 : (si2int(s)))

/*
 *--------------------------------------------------------------
 *
 * ZoomImageVertically --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void
ZoomImageVertically(
    RbcColorImage * src,
    RbcColorImage * dest,
    RbcResampleFilter * filterPtr)
{
    Sample         *samples, *s, *endPtr;
    int             destWidth, destHeight;
    int             red, green, blue, alpha;
    int             srcWidth, srcHeight;
    register RbcPix32 *srcColumnPtr;
    register RbcPix32 *srcPtr, *destPtr;
    register Weight *weight;
    int             x, i;
    size_t          size;       /* Size of sample. */

    srcWidth = src->width;
    srcHeight = src->height;
    destWidth = dest->width;
    destHeight = dest->height;

    /* Pre-calculate filter contributions for a row */
    size = ComputeWeights(srcHeight, destHeight, filterPtr, &samples);
    endPtr = (Sample *) ((char *) samples + (destHeight * size));

    /* Apply filter to zoom vertically from tmp to destination */
    for (x = 0; x < srcWidth; x++) {
        srcColumnPtr = src->bits + x;
        destPtr = dest->bits + x;
        for (s = samples; s < endPtr; s = (Sample *) ((char *) s + size)) {
            red = green = blue = alpha = 0;
            srcPtr = srcColumnPtr + (s->start * srcWidth);
            for (weight = s->weights, i = 0; i < s->count; i++, weight++) {
                red += srcPtr->rgba.red * weight->i;
                green += srcPtr->rgba.green * weight->i;
                blue += srcPtr->rgba.blue * weight->i;
                alpha += srcPtr->rgba.alpha * weight->i;
                srcPtr += srcWidth;
            }
            destPtr->rgba.red = SICLAMP(red);
            destPtr->rgba.green = SICLAMP(green);
            destPtr->rgba.blue = SICLAMP(blue);
            destPtr->rgba.alpha = SICLAMP(alpha);
            destPtr += destWidth;

        }
    }
    /* Free the memory allocated for filter weights */
    ckfree((char *) samples);
}

/*
 *--------------------------------------------------------------
 *
 * ZoomImageHorizontally --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void
ZoomImageHorizontally(
    RbcColorImage * src,
    RbcColorImage * dest,
    RbcResampleFilter * filterPtr)
{
    Sample         *samples, *s, *endPtr;
    Weight         *weight;
    int             destWidth;
    int             red, green, blue, alpha;
    int             srcWidth, srcHeight;
    int             y, i;
    register RbcPix32 *srcPtr, *destPtr;
    register RbcPix32 *srcRowPtr;
    size_t          size;       /* Size of sample. */

    srcWidth = src->width;
    srcHeight = src->height;
    destWidth = dest->width;

    /* Pre-calculate filter contributions for a row */
    size = ComputeWeights(srcWidth, destWidth, filterPtr, &samples);
    endPtr = (Sample *) ((char *) samples + (destWidth * size));

    /* Apply filter to zoom horizontally from srcPtr to tmpPixels */
    srcRowPtr = src->bits;
    destPtr = dest->bits;
    for (y = 0; y < srcHeight; y++) {
        for (s = samples; s < endPtr; s = (Sample *) ((char *) s + size)) {
            red = green = blue = alpha = 0;
            srcPtr = srcRowPtr + s->start;
            for (weight = s->weights, i = 0; i < s->count; i++, weight++) {
                red += srcPtr->rgba.red * weight->i;
                green += srcPtr->rgba.green * weight->i;
                blue += srcPtr->rgba.blue * weight->i;
                alpha += srcPtr->rgba.alpha * weight->i;
                srcPtr++;
            }
            destPtr->rgba.red = SICLAMP(red);
            destPtr->rgba.green = SICLAMP(green);
            destPtr->rgba.blue = SICLAMP(blue);
            destPtr->rgba.alpha = SICLAMP(alpha);
            destPtr++;
        }
        srcRowPtr += srcWidth;
    }
    /* free the memory allocated for horizontal filter weights */
    ckfree((char *) samples);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcResampleColorImage --
 *
 *      Resamples a given color image using 1-D filters and returns
 *      a new color image of the designated size.
 *
 * Results:
 *      Returns the resampled color image. The original color image
 *      is left intact.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcColorImage  *
RbcResampleColorImage(
    RbcColorImage * src,
    int width,
    int height,
    RbcResampleFilter * horzFilterPtr,
    RbcResampleFilter * vertFilterPtr)
{
    RbcColorImage  *tmp, *dest;

    /*
     * It's usually faster to zoom vertically last.  This has to do
     * with the fact that images are stored in contiguous rows.
     */

    tmp = RbcCreateColorImage(width, src->height);
    ZoomImageHorizontally(src, tmp, horzFilterPtr);
    dest = RbcCreateColorImage(width, height);
    ZoomImageVertically(tmp, dest, vertFilterPtr);
    RbcFreeColorImage(tmp);
    return dest;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcResamplePhoto --
 *
 *      Resamples a Tk photo image using 1-D filters and writes the
 *      image into another Tk photo.  It is possible for the
 *      source and destination to be the same photo.
 *
 * Results:
 *      The designated destination photo will contain the resampled
 *      color image. The original photo is left intact.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcResamplePhoto(
    Tcl_Interp * interp,
    Tk_PhotoHandle srcPhoto,    /* Source photo image to scale */
    int x,
    int y,
    int width,
    int height,
    Tk_PhotoHandle destPhoto,   /* Resulting scaled photo image */
    RbcResampleFilter * horzFilterPtr,
    RbcResampleFilter * vertFilterPtr)
{
    RbcColorImage  *srcImage, *destImage;
    Tk_PhotoImageBlock dest;

    Tk_PhotoGetImage(destPhoto, &dest);
    srcImage = RbcPhotoRegionToColorImage(srcPhoto, x, y, width, height);
    destImage = RbcResampleColorImage(srcImage, dest.width, dest.height,
        horzFilterPtr, vertFilterPtr);
    RbcFreeColorImage(srcImage);
    RbcColorImageToPhoto(interp, destImage, destPhoto);
    RbcFreeColorImage(destImage);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcResizePhoto --
 *
 *      Scales the region of the source image to the size of the
 *      destination image.  This routine performs raw scaling of
 *      the image and unlike RbcResamplePhoto does not handle
 *      aliasing effects from subpixel sampling. It is possible
 *      for the source and destination to be the same photo.
 *
 * Results:
 *      The designated destination photo will contain the resampled
 *      color image. The original photo is left intact.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcResizePhoto(
    Tcl_Interp * interp,
    Tk_PhotoHandle srcPhoto,    /* Source photo image to scaled. */
    register int x,             /* Region of source photo to be * scaled. */
    register int y,             /* Region of source photo to be * scaled. */
    int width,
    int height,
    Tk_PhotoHandle destPhoto)
{                               /* (out) Resulting scaled photo image.
                                 * Scaling factors are derived from
                                 * the destination photo's
                                 * dimensions. */
    double          xScale, yScale;
    RbcColorImage  *destImage;
    RbcPix32       *destPtr;
    Tk_PhotoImageBlock src, dest;
    unsigned char  *srcPtr, *srcRowPtr;
    int            *mapX, *mapY;
    register int    sx, sy;
    int             left, right, top, bottom;

    Tk_PhotoGetImage(srcPhoto, &src);
    Tk_PhotoGetImage(destPhoto, &dest);

    left = x, top = y, right = x + width - 1, bottom = y + height - 1;
    destImage = RbcCreateColorImage(dest.width, dest.height);
    xScale = (double) width / (double) dest.width;
    yScale = (double) height / (double) dest.height;
    mapX = (int *) ckalloc(sizeof(int) * dest.width);
    mapY = (int *) ckalloc(sizeof(int) * dest.height);
    for (x = 0; x < dest.width; x++) {
        sx = (int) (xScale * (double) (x + left));
        if (sx > right) {
            sx = right;
        }
        mapX[x] = sx;
    }
    for (y = 0; y < dest.height; y++) {
        sy = (int) (yScale * (double) (y + top));
        if (sy > bottom) {
            sy = bottom;
        }
        mapY[y] = sy;
    }
    destPtr = destImage->bits;
    if (src.pixelSize == 4) {
        for (y = 0; y < dest.height; y++) {
            srcRowPtr = src.pixelPtr + (mapY[y] * src.pitch);
            for (x = 0; x < dest.width; x++) {
                srcPtr = srcRowPtr + (mapX[x] * src.pixelSize);
                destPtr->rgba.red = srcPtr[src.offset[0]];
                destPtr->rgba.green = srcPtr[src.offset[1]];
                destPtr->rgba.blue = srcPtr[src.offset[2]];
                destPtr->rgba.alpha = srcPtr[src.offset[3]];
                destPtr++;
            }
        }
    } else if (src.pixelSize == 3) {
        for (y = 0; y < dest.height; y++) {
            srcRowPtr = src.pixelPtr + (mapY[y] * src.pitch);
            for (x = 0; x < dest.width; x++) {
                srcPtr = srcRowPtr + (mapX[x] * src.pixelSize);
                destPtr->rgba.red = srcPtr[src.offset[0]];
                destPtr->rgba.green = srcPtr[src.offset[1]];
                destPtr->rgba.blue = srcPtr[src.offset[2]];
                destPtr->rgba.alpha = (unsigned char) -1;
                destPtr++;
            }
        }
    } else {
        for (y = 0; y < dest.height; y++) {
            srcRowPtr = src.pixelPtr + (mapY[y] * src.pitch);
            for (x = 0; x < dest.width; x++) {
                srcPtr = srcRowPtr + (mapX[x] * src.pixelSize);
                destPtr->rgba.red = destPtr->rgba.green = destPtr->rgba.blue =
                    srcPtr[src.offset[0]];
                destPtr->rgba.alpha = (unsigned char) -1;
                destPtr++;
            }
        }
    }
    ckfree((char *) mapX);
    ckfree((char *) mapY);
    RbcColorImageToPhoto(interp, destImage, destPhoto);
    RbcFreeColorImage(destImage);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcResizeColorImage --
 *
 *      Scales the region of the source image to the size of the
 *      destination image.  This routine performs raw scaling of
 *      the image and unlike RbcResamplePhoto does not perform
 *      any antialiasing.
 *
 * Results:
 *      Returns the new resized color image.  The original image
 *      is left intact.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcColorImage  *
RbcResizeColorImage(
    RbcColorImage * src,        /* Source color image to be scaled. */
    register int x,             /* Region of source image to scaled. */
    register int y,             /* Region of source image to scaled. */
    int width,
    int height,
    int destWidth,              /* Requested dimensions of the scaled image. */
    int destHeight)
{                               /* Requested dimensions of the scaled image. */
    register int    sx, sy;
    double          xScale, yScale;
    RbcColorImage  *dest;
    RbcPix32       *srcPtr, *srcRowPtr, *destPtr;
    int            *mapX, *mapY;
    int             left, right, top, bottom;

    left = x, top = y;
    right = x + width - 1, bottom = y + height - 1;

    dest = RbcCreateColorImage(destWidth, destHeight);
    xScale = (double) width / (double) destWidth;
    yScale = (double) height / (double) destHeight;
    mapX = (int *) ckalloc(sizeof(int) * destWidth);
    mapY = (int *) ckalloc(sizeof(int) * destHeight);
    for (x = 0; x < destWidth; x++) {
        sx = (int) (xScale * (double) (x + left));
        if (sx > right) {
            sx = right;
        }
        mapX[x] = sx;
    }
    for (y = 0; y < destHeight; y++) {
        sy = (int) (yScale * (double) (y + top));
        if (sy > bottom) {
            sy = bottom;
        }
        mapY[y] = sy;
    }
    destPtr = dest->bits;
    for (y = 0; y < destHeight; y++) {
        srcRowPtr = src->bits +
            (src->width * mapY[y]);
        for (x = 0; x < destWidth; x++) {
            srcPtr = srcRowPtr + mapX[x];
            destPtr->value = srcPtr->value;     /* Copy the pixel. */
            destPtr++;
        }
    }
    ckfree((char *) mapX);
    ckfree((char *) mapY);
    return dest;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcResizeColorSubimage --
 *
 *      Scales the region of the source image to the size of the
 *      destination image.  This routine performs raw scaling of
 *      the image and unlike RbcResamplePhoto does not perform
 *      any antialiasing.
 *
 * Results:
 *      Returns the new resized color image.  The original image
 *      is left intact.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcColorImage  *
RbcResizeColorSubimage(
    RbcColorImage * src,        /* Source color image to be scaled. */
    int regionX,
    int regionY,                /* Offset of subimage in destination. */
    int regionWidth,            /* Dimension of subimage. */
    int regionHeight,           /* Dimension of subimage. */
    int destWidth,              /* Dimensions of the entire scaled image. */
    int destHeight)
{                               /* Dimensions of the entire scaled image. */
    RbcColorImage  *dest;
    RbcPix32       *srcPtr, *srcRowPtr, *destPtr;
    double          xScale, yScale;
    int            *mapX, *mapY;
    int             srcWidth, srcHeight;
    register int    sx, sy;
    register int    x, y;

    srcWidth = src->width;
    srcHeight = src->height;

    xScale = (double) srcWidth / (double) destWidth;
    yScale = (double) srcHeight / (double) destHeight;
    mapX = (int *) ckalloc(sizeof(int) * regionWidth);
    mapY = (int *) ckalloc(sizeof(int) * regionHeight);

    /* Precompute scaling factors for each row and column. */
    for (x = 0; x < regionWidth; x++) {
        sx = (int) (xScale * (double) (x + regionX));
        if (sx >= srcWidth) {
            sx = srcWidth - 1;
        }
        mapX[x] = sx;
    }
    for (y = 0; y < regionHeight; y++) {
        sy = (int) (yScale * (double) (y + regionY));
        if (sy > srcHeight) {
            sy = srcHeight - 1;
        }
        mapY[y] = sy;
    }

    dest = RbcCreateColorImage(regionWidth, regionHeight);
    destPtr = dest->bits;
    for (y = 0; y < regionHeight; y++) {
        srcRowPtr = src->bits +
            (src->width * mapY[y]);
        for (x = 0; x < regionWidth; x++) {
            srcPtr = srcRowPtr + mapX[x];
            destPtr->value = srcPtr->value;     /* Copy the pixel. */
            destPtr++;
        }
    }
    ckfree((char *) mapX);
    ckfree((char *) mapY);
    return dest;
}

/*
 *--------------------------------------------------------------
 *
 * RbcConvolveColorImage --
 *
 *      FIXME: Boundary handling could be better (pixels
 *             are replicated). It's slow. Take boundary
 *             tests out of inner loop.
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
RbcColorImage  *
RbcConvolveColorImage(
    RbcColorImage * src,
    RbcFilter2D * filterPtr)
{
    RbcColorImage  *dest;
    register RbcPix32 *srcPtr, *destPtr;
#define MAXROWS	24
    register int    sx, sy, dx, dy;
    register int    x, y;
    double          red, green, blue;
    int             width, height;
    int             radius;
    register double *valuePtr;

    width = src->width;
    height = src->height;

    dest = RbcCreateColorImage(width, height);
    radius = (int) filterPtr->support;
    if (radius < 1) {
        radius = 1;
    }
    destPtr = dest->bits;
    for (dy = 0; dy < height; dy++) {
        for (dx = 0; dx < width; dx++) {
            red = green = blue = 0.0;
            valuePtr = filterPtr->kernel;
            for (sy = (dy - radius); sy <= (dy + radius); sy++) {
                y = sy;
                if (y < 0) {
                    y = 0;
                } else if (y >= height) {
                    y = height - 1;
                }
                for (sx = (dx - radius); sx <= (dx + radius); sx++) {
                    x = sx;
                    if (x < 0) {
                        x = 0;
                    } else if (sx >= width) {
                        x = width - 1;
                    }
                    srcPtr = ColorImagePixel(src, x, y);
                    red += *valuePtr * (double) srcPtr->rgba.red;
                    green += *valuePtr * (double) srcPtr->rgba.green;
                    blue += *valuePtr * (double) srcPtr->rgba.blue;
                    valuePtr++;
                }
            }
            red /= filterPtr->sum;
            green /= filterPtr->sum;
            blue /= filterPtr->sum;
            destPtr->rgba.red = CLAMP((unsigned char) red, 0, 255);
            destPtr->rgba.green = CLAMP((unsigned char) green, 0, 255);
            destPtr->rgba.blue = CLAMP((unsigned char) blue, 0, 255);
            destPtr->rgba.alpha = (unsigned char) -1;
            destPtr++;
        }
    }
    return dest;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcSnapPhoto --
 *
 *      Takes a snapshot of an X drawable (pixmap or window) and
 *      writes it to an existing Tk photo image.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      The named Tk photo is updated with the snapshot.
 *
 *----------------------------------------------------------------------
 */
int
RbcSnapPhoto(
    Tcl_Interp * interp,        /* Interpreter to report errors back to */
    Tk_Window tkwin,
    Drawable drawable,          /* Window or pixmap to be snapped */
    int x,                      /* Offset of image from drawable origin. */
    int y,                      /* Offset of image from drawable origin. */
    int width,                  /* Dimension of the drawable */
    int height,                 /* Dimension of the drawable */
    int destWidth,              /* Desired size of the Tk photo */
    int destHeight,             /* Desired size of the Tk photo */
    const char *photoName,      /* Name of an existing Tk photo image. */
    double inputGamma)
{
    Tk_PhotoHandle  photo;      /* The photo image to write into. */
    RbcColorImage  *image;

    photo = Tk_FindPhoto(interp, photoName);
    if (photo == NULL) {
        Tcl_AppendResult(interp, "can't find photo \"", photoName, "\"",
            (char *) NULL);
        return TCL_ERROR;
    }
    image =
        RbcDrawableToColorImage(tkwin, drawable, x, y, width, height,
        inputGamma);
    if (image == NULL) {
        Tcl_AppendResult(interp,
            "can't grab window or pixmap (possibly obscured?)", (char *) NULL);
        return TCL_ERROR;       /* Can't grab window image */
    }
    if ((destWidth != width) || (destHeight != height)) {
        RbcColorImage  *destImage;

        /*
         * The requested size for the destination image is different than
         * that of the source snapshot.  Resample the image as necessary.
         * We'll use a cheap box filter. I'm assuming that the destination
         * image will typically be smaller than the original.
         */
        destImage = RbcResampleColorImage(image, destWidth, destHeight,
            rbcBoxFilterPtr, rbcBoxFilterPtr);
        RbcFreeColorImage(image);
        image = destImage;
    }
    RbcColorImageToPhoto(interp, image, photo);
    RbcFreeColorImage(image);
    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * ShearY --
 *
 *      Shears a row horizontally. Antialiasing limited to filtering
 *      two adjacent pixels.  So the shear angle must be between +-45
 *      degrees.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The sheared image is drawn into the destination color image.
 *
 * --------------------------------------------------------------------------
 */
static void
ShearY(
    RbcColorImage * src,
    RbcColorImage * dest,
    int y,                      /* Designates the row to be sheared */
    int offset,                 /* Difference between  of  */
    double frac,
    RbcPix32 bgColor)
{
    RbcPix32       *srcPtr, *destPtr;
    RbcPix32       *srcRowPtr, *destRowPtr;
    register int    x, dx;
    int             destWidth;
    int             srcWidth;
    int             red, blue, green, alpha;
    int             leftRed, leftGreen, leftBlue, leftAlpha;
    int             oldLeftRed, oldLeftGreen, oldLeftBlue, oldLeftAlpha;
    int             ifrac;

    srcWidth = src->width;
    destWidth = dest->width;

    destRowPtr = dest->bits + (y * destWidth);
    srcRowPtr = src->bits + (y * srcWidth);

    destPtr = destRowPtr;
    for (x = 0; x < offset; x++) {
        *destPtr++ = bgColor;
    }
    destPtr = destRowPtr + offset;
    srcPtr = srcRowPtr;
    dx = offset;

    oldLeftRed = uchar2si(bgColor.rgba.red);
    oldLeftGreen = uchar2si(bgColor.rgba.green);
    oldLeftBlue = uchar2si(bgColor.rgba.blue);
    oldLeftAlpha = uchar2si(bgColor.rgba.alpha);

    ifrac = float2si(frac);
    for (x = 0; x < srcWidth; x++, dx++) {      /* Loop through row pixels */
        leftRed = srcPtr->rgba.red * ifrac;
        leftGreen = srcPtr->rgba.green * ifrac;
        leftBlue = srcPtr->rgba.blue * ifrac;
        leftAlpha = srcPtr->rgba.alpha * ifrac;
        if ((dx >= 0) && (dx < destWidth)) {
            red = uchar2si(srcPtr->rgba.red) - (leftRed - oldLeftRed);
            green = uchar2si(srcPtr->rgba.green) - (leftGreen - oldLeftGreen);
            blue = uchar2si(srcPtr->rgba.blue) - (leftBlue - oldLeftBlue);
            alpha = uchar2si(srcPtr->rgba.alpha) - (leftAlpha - oldLeftAlpha);
            destPtr->rgba.red = SICLAMP(red);
            destPtr->rgba.green = SICLAMP(green);
            destPtr->rgba.blue = SICLAMP(blue);
            destPtr->rgba.alpha = SICLAMP(alpha);
        }
        oldLeftRed = leftRed;
        oldLeftGreen = leftGreen;
        oldLeftBlue = leftBlue;
        oldLeftAlpha = leftAlpha;
        srcPtr++, destPtr++;
    }
    x = srcWidth + offset;
    destPtr = dest->bits + (y * destWidth) + x;
    if (x < destWidth) {
        leftRed = uchar2si(bgColor.rgba.red);
        leftGreen = uchar2si(bgColor.rgba.green);
        leftBlue = uchar2si(bgColor.rgba.blue);
        leftAlpha = uchar2si(bgColor.rgba.alpha);

        red = leftRed + oldLeftRed - (bgColor.rgba.red * ifrac);
        green = leftGreen + oldLeftGreen - (bgColor.rgba.green * ifrac);
        blue = leftBlue + oldLeftBlue - (bgColor.rgba.blue * ifrac);
        alpha = leftAlpha + oldLeftAlpha - (bgColor.rgba.alpha * ifrac);
        destPtr->rgba.red = SICLAMP(red);
        destPtr->rgba.green = SICLAMP(green);
        destPtr->rgba.blue = SICLAMP(blue);
        destPtr->rgba.alpha = SICLAMP(alpha);
        destPtr++;
    }
    for (x++; x < destWidth; x++) {
        *destPtr++ = bgColor;
    }
}

/*
 * --------------------------------------------------------------------------
 *
 * ShearX --
 *
 *      Shears a column. Antialiasing is limited to filtering two
 *      adjacent pixels.  So the shear angle must be between +-45
 *      degrees.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The sheared image is drawn into the destination color image.
 *
 * --------------------------------------------------------------------------
 */
static void
ShearX(
    RbcColorImage * src,
    RbcColorImage * dest,
    int x,                      /* Column in source image to be sheared. */
    int offset,                 /* Offset of */
    double frac,                /* Fraction of subpixel. */
    RbcPix32 bgColor)
{
    RbcPix32       *srcPtr, *destPtr;
    register int    y, dy;
    int             destHeight;
    int             srcHeight;
    int             red, blue, green, alpha;
    int             leftRed, leftGreen, leftBlue, leftAlpha;
    int             oldLeftRed, oldLeftGreen, oldLeftBlue, oldLeftAlpha;
    int             ifrac;

    srcHeight = src->height;
    destHeight = dest->height;
    for (y = 0; y < offset; y++) {
        destPtr = ColorImagePixel(dest, x, y);
        *destPtr = bgColor;
    }

    oldLeftRed = uchar2si(bgColor.rgba.red);
    oldLeftGreen = uchar2si(bgColor.rgba.green);
    oldLeftBlue = uchar2si(bgColor.rgba.blue);
    oldLeftAlpha = uchar2si(bgColor.rgba.alpha);
    dy = offset;
    ifrac = float2si(frac);
    for (y = 0; y < srcHeight; y++, dy++) {
        srcPtr = ColorImagePixel(src, x, y);
        leftRed = srcPtr->rgba.red * ifrac;
        leftGreen = srcPtr->rgba.green * ifrac;
        leftBlue = srcPtr->rgba.blue * ifrac;
        leftAlpha = srcPtr->rgba.alpha * ifrac;
        if ((dy >= 0) && (dy < destHeight)) {
            destPtr = ColorImagePixel(dest, x, dy);
            red = uchar2si(srcPtr->rgba.red) - (leftRed - oldLeftRed);
            green = uchar2si(srcPtr->rgba.green) - (leftGreen - oldLeftGreen);
            blue = uchar2si(srcPtr->rgba.blue) - (leftBlue - oldLeftBlue);
            alpha = uchar2si(srcPtr->rgba.alpha) - (leftAlpha - oldLeftAlpha);
            destPtr->rgba.red = SICLAMP(red);
            destPtr->rgba.green = SICLAMP(green);
            destPtr->rgba.blue = SICLAMP(blue);
            destPtr->rgba.alpha = SICLAMP(alpha);
        }
        oldLeftRed = leftRed;
        oldLeftGreen = leftGreen;
        oldLeftBlue = leftBlue;
        oldLeftAlpha = leftAlpha;
    }
    y = srcHeight + offset;
    if (y < destHeight) {
        leftRed = uchar2si(bgColor.rgba.red);
        leftGreen = uchar2si(bgColor.rgba.green);
        leftBlue = uchar2si(bgColor.rgba.blue);
        leftAlpha = uchar2si(bgColor.rgba.alpha);

        destPtr = ColorImagePixel(dest, x, y);
        red = leftRed + oldLeftRed - (bgColor.rgba.red * ifrac);
        green = leftGreen + oldLeftGreen - (bgColor.rgba.green * ifrac);
        blue = leftBlue + oldLeftBlue - (bgColor.rgba.blue * ifrac);
        alpha = leftAlpha + oldLeftAlpha - (bgColor.rgba.alpha * ifrac);
        destPtr->rgba.red = SICLAMP(red);
        destPtr->rgba.green = SICLAMP(green);
        destPtr->rgba.blue = SICLAMP(blue);
        destPtr->rgba.alpha = SICLAMP(alpha);
    }

    for (y++; y < destHeight; y++) {
        destPtr = ColorImagePixel(dest, x, y);
        *destPtr = bgColor;
    }
}

/*
 * ---------------------------------------------------------------------------
 *
 * Rotate45 --
 *
 *      Rotates an image by a given angle.  The angle must be in the
 *      range -45.0 to 45.0 inclusive.  Anti-aliasing filtering is
 *      performed on two adjacent pixels, so the angle can't be so
 *      great as to force a sheared pixel to occupy 3 destination
 *      pixels.  Performs a three shear rotation described below.
 *
 *      Reference: Alan W. Paeth, "A Fast Algorithm for General Raster
 *               Rotation", Graphics Gems, pp 179-195.
 *
 *
 * Results:
 *      Returns a newly allocated rotated image.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ---------------------------------------------------------------------------
 */
static RbcColorImage *
Rotate45(
    RbcColorImage * src,
    double theta,
    RbcPix32 bgColor)
{
    int             tmpWidth, tmpHeight;
    int             srcWidth, srcHeight;
    double          sinTheta, cosTheta, tanTheta;
    double          skewf;
    int             skewi;
    RbcColorImage  *tmp1, *tmp2, *dest;
    register int    x, y;

    sinTheta = sin(theta);
    cosTheta = cos(theta);
    tanTheta = tan(theta * 0.5);

    srcWidth = src->width;
    srcHeight = src->height;

    tmpWidth = srcWidth + (int) (srcHeight * FABS(tanTheta));
    tmpHeight = srcHeight;

    /* 1st shear */

    tmp1 = RbcCreateColorImage(tmpWidth, tmpHeight);
    assert(tmp1);

    if (tanTheta >= 0.0) {      /* Positive angle */
        for (y = 0; y < tmpHeight; y++) {
            skewf = (y + 0.5) * tanTheta;
            skewi = (int) floor(skewf);
            ShearY(src, tmp1, y, skewi, skewf - skewi, bgColor);
        }
    } else {                    /* Negative angle */
        for (y = 0; y < tmpHeight; y++) {
            skewf = ((y - srcHeight) + 0.5) * tanTheta;
            skewi = (int) floor(skewf);
            ShearY(src, tmp1, y, skewi, skewf - skewi, bgColor);
        }
    }
    tmpHeight = (int) (srcWidth * FABS(sinTheta) + srcHeight * cosTheta) + 1;

    tmp2 = RbcCreateColorImage(tmpWidth, tmpHeight);
    assert(tmp2);

    /* 2nd shear */

    if (sinTheta > 0.0) {       /* Positive angle */
        skewf = (srcWidth - 1) * sinTheta;
    } else {                    /* Negative angle */
        skewf = (srcWidth - tmpWidth) * -sinTheta;
    }
    for (x = 0; x < tmpWidth; x++) {
        skewi = (int) floor(skewf);
        ShearX(tmp1, tmp2, x, skewi, skewf - skewi, bgColor);
        skewf -= sinTheta;
    }

    RbcFreeColorImage(tmp1);

    /* 3rd shear */

    tmpWidth = (int) (srcHeight * FABS(sinTheta) + srcWidth * cosTheta) + 1;

    dest = RbcCreateColorImage(tmpWidth, tmpHeight);
    assert(dest);

    if (sinTheta >= 0.0) {      /* Positive angle */
        skewf = (srcWidth - 1) * sinTheta * -tanTheta;
    } else {                    /* Negative angle */
        skewf = tanTheta * ((srcWidth - 1) * -sinTheta - (tmpHeight - 1));
    }
    for (y = 0; y < tmpHeight; y++) {
        skewi = (int) floor(skewf);
        ShearY(tmp2, dest, y, skewi, skewf - skewi, bgColor);
        skewf += tanTheta;
    }
    RbcFreeColorImage(tmp2);
    return dest;
}

/*
 * ---------------------------------------------------------------------------
 *
 * CopyColorImage --
 *
 *      Creates a copy of the given color image.
 *
 * Results:
 *      Returns the new copy.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ---------------------------------------------------------------------------
 */
static RbcColorImage *
CopyColorImage(
    RbcColorImage * src)
{
    unsigned int    width, height;
    RbcPix32       *srcPtr, *destPtr;
    RbcColorImage  *dest;

    width = src->width;
    height = src->height;
    dest = RbcCreateColorImage(width, height);
    srcPtr = src->bits;
    destPtr = dest->bits;
    memcpy(destPtr, srcPtr, width * height * sizeof(RbcPix32));
    return dest;
}

/*
 * ---------------------------------------------------------------------------
 *
 * Rotate90 --
 *
 *      Rotates the given color image by 90 degrees.  This is part
 *      of the special case right-angle rotations that do not create
 *      subpixel aliasing.
 *
 * Results:
 *      Returns a newly allocated, rotated color image.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ---------------------------------------------------------------------------
 */
static RbcColorImage *
Rotate90(
    RbcColorImage * src)
{
    int             width, height, offset;
    RbcPix32       *srcPtr, *destPtr;
    RbcColorImage  *dest;
    register int    x, y;

    height = src->width;
    width = src->height;

    srcPtr = src->bits;
    dest = RbcCreateColorImage(width, height);
    offset = (height - 1) * width;

    for (x = 0; x < width; x++) {
        destPtr = dest->bits + offset + x;
        for (y = 0; y < height; y++) {
            *destPtr = *srcPtr++;
            destPtr -= width;
        }
    }
    return dest;
}

/*
 * ---------------------------------------------------------------------------
 *
 * Rotate180 --
 *
 *      Rotates the given color image by 180 degrees.  This is one of
 *      the special case orthogonal rotations that do not create
 *      subpixel aliasing.
 *
 * Results:
 *      Returns a newly allocated, rotated color image.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ---------------------------------------------------------------------------
 */
static RbcColorImage *
Rotate180(
    RbcColorImage * src)
{
    int             width, height, offset;
    RbcPix32       *srcPtr, *destPtr;
    RbcColorImage  *dest;
    register int    x, y;

    width = src->width;
    height = src->height;
    dest = RbcCreateColorImage(width, height);

    srcPtr = src->bits;
    offset = (height - 1) * width;
    for (y = 0; y < height; y++) {
        destPtr = dest->bits + offset + width - 1;
        for (x = 0; x < width; x++) {
            *destPtr-- = *srcPtr++;
        }
        offset -= width;
    }
    return dest;
}

/*
 * ---------------------------------------------------------------------------
 *
 * Rotate270 --
 *
 *      Rotates the given color image by 270 degrees.  This is part
 *      of the special case right-angle rotations that do not create
 *      subpixel aliasing.
 *
 * Results:
 *      Returns a newly allocated, rotated color image.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ---------------------------------------------------------------------------
 */
static RbcColorImage *
Rotate270(
    RbcColorImage * src)
{
    int             width, height;
    RbcPix32       *srcPtr, *destPtr;
    RbcColorImage  *dest;
    register int    x, y;

    height = src->width;
    width = src->height;
    dest = RbcCreateColorImage(width, height);

    srcPtr = src->bits;
    for (x = width - 1; x >= 0; x--) {
        destPtr = dest->bits + x;
        for (y = 0; y < height; y++) {
            *destPtr = *srcPtr++;
            destPtr += width;
        }
    }
    return dest;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcRotateColorImage --
 *
 *      Rotates a color image by a given # of degrees.
 *
 * Results:
 *      Returns a newly allocated, rotated color image.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
RbcColorImage  *
RbcRotateColorImage(
    RbcColorImage * src,
    double angle)
{
    RbcColorImage  *dest, *tmp;
    int             quadrant;

    tmp = src;                  /* Suppress compiler warning. */

    /* Make the angle positive between 0 and 360 degrees. */
    angle = FMOD(angle, 360.0);
    if (angle < 0.0) {
        angle += 360.0;
    }
    quadrant = RBC_ROTATE_0;
    if ((angle > 45.0) && (angle <= 135.0)) {
        quadrant = RBC_ROTATE_90;
        angle -= 90.0;
    } else if ((angle > 135.0) && (angle <= 225.0)) {
        quadrant = RBC_ROTATE_180;
        angle -= 180.0;
    } else if ((angle > 225.0) && (angle <= 315.0)) {
        quadrant = RBC_ROTATE_270;
        angle -= 270.0;
    } else if (angle > 315.0) {
        angle -= 360.0;
    }
    /*
     * If necessary, create a temporary image that's been rotated
     * by a right-angle.  We'll then rotate this color image between
     * -45 to 45 degrees to arrive at its final angle.
     */
    switch (quadrant) {
    case RBC_ROTATE_270:       /* 270 degrees */
        tmp = Rotate270(src);
        break;

    case RBC_ROTATE_90:        /* 90 degrees */
        tmp = Rotate90(src);
        break;

    case RBC_ROTATE_180:       /* 180 degrees */
        tmp = Rotate180(src);
        break;

    case RBC_ROTATE_0:         /* 0 degrees */
        if (angle == 0.0) {
            tmp = CopyColorImage(src);  /* Make a copy of the source. */
        }
        break;
    }

    assert((angle >= -45.0) && (angle <= 45.0));

    dest = tmp;
    if (angle != 0.0) {
        double          theta;
        RbcPix32       *srcPtr;
        RbcPix32        bgColor;

        /* FIXME: pick up background blending color from somewhere */
        srcPtr = src->bits;
        bgColor = *srcPtr;
        bgColor.rgba.red = bgColor.rgba.green = bgColor.rgba.blue = 0xFF;
        bgColor.rgba.alpha = 0x00;      /* Transparent background */
        theta = (angle / 180.0) * M_PI;
        dest = Rotate45(tmp, theta, bgColor);
        if (tmp != src) {
            RbcFreeColorImage(tmp);
        }
    }
    return dest;
}

/*
 *--------------------------------------------------------------
 *
 * GetColorImageStatistics --
 *
 *      Build 3-D color histogram of counts, R/G/B, c^2
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static ColorImageStatistics *
GetColorImageStatistics(
    RbcColorImage * image)
{
    register int    r, g, b;
#define MAX_INTENSITIES	256
    unsigned int    sqr[MAX_INTENSITIES];
    int             numPixels;
    RbcPix32       *srcPtr, *endPtr;
    register int    i;
    ColorImageStatistics *s;

    s = RbcCalloc(1, sizeof(ColorImageStatistics));
    assert(s);

    /* Precompute table of squares. */
    for (i = 0; i < MAX_INTENSITIES; i++) {
        sqr[i] = i * i;
    }
    numPixels = image->width * image->height;

    for (srcPtr = image->bits, endPtr = srcPtr + numPixels;
        srcPtr < endPtr; srcPtr++) {
        /*
         * Reduce the number of bits (5) per color component. This
         * will keep the table size (2^15) reasonable without perceptually
         * affecting the final image.
         */
        r = (srcPtr->rgba.red >> 3) + 1;
        g = (srcPtr->rgba.green >> 3) + 1;
        b = (srcPtr->rgba.blue >> 3) + 1;
        s->wt[r][g][b] += 1;
        s->mR[r][g][b] += srcPtr->rgba.red;
        s->mG[r][g][b] += srcPtr->rgba.green;
        s->mB[r][g][b] += srcPtr->rgba.blue;
        s->gm2[r][g][b] += sqr[srcPtr->rgba.red] + sqr[srcPtr->rgba.green] +
            sqr[srcPtr->rgba.blue];
    }
    return s;
}

/*
 *----------------------------------------------------------------------
 * At conclusion of the histogram step, we can interpret
 *   wt[r][g][b] = sum over voxel of P(c)
 *   mR[r][g][b] = sum over voxel of r*P(c)  ,  similarly for mG, mB
 *   m2[r][g][b] = sum over voxel of c^2*P(c)
 * Actually each of these should be divided by 'size' to give the usual
 * interpretation of P() as ranging from 0 to 1, but we needn't do that here.
 *----------------------------------------------------------------------
 */

/*
 *--------------------------------------------------------------
 *
 * M3d --
 *
 *      We now convert histogram into moments so that we
 *      can rapidly calculate the sums of the above
 *      quantities over any desired box.
 *
 *      compute cumulative moments
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void
M3d(
    ColorImageStatistics * s)
{
    register unsigned char i, r, g, b, r0;
    long int        line, rLine, gLine, bLine;
    long int        area[33], rArea[33], gArea[33], bArea[33];
    unsigned int    line2, area2[33];

    for (r = 1, r0 = 0; r <= 32; r++, r0++) {
        for (i = 0; i <= 32; ++i) {
            area2[i] = area[i] = rArea[i] = gArea[i] = bArea[i] = 0;
        }
        for (g = 1; g <= 32; g++) {
            line2 = line = rLine = gLine = bLine = 0;
            for (b = 1; b <= 32; b++) {
                /* ind1 = RGBIndex(r, g, b); */

                line += s->wt[r][g][b];
                rLine += s->mR[r][g][b];
                gLine += s->mG[r][g][b];
                bLine += s->mB[r][g][b];
                line2 += s->gm2[r][g][b];

                area[b] += line;
                rArea[b] += rLine;
                gArea[b] += gLine;
                bArea[b] += bLine;
                area2[b] += line2;

                /* ind2 = ind1 - 1089; [r0][g][b] */
                s->wt[r][g][b] = s->wt[r0][g][b] + area[b];
                s->mR[r][g][b] = s->mR[r0][g][b] + rArea[b];
                s->mG[r][g][b] = s->mG[r0][g][b] + gArea[b];
                s->mB[r][g][b] = s->mB[r0][g][b] + bArea[b];
                s->gm2[r][g][b] = s->gm2[r0][g][b] + area2[b];
            }
        }
    }
}

/*
 *--------------------------------------------------------------
 *
 * Volume --
 *
 *      Compute sum over a box of any given statistic
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static long int
Volume(
    Cube * cubePtr,
    long int m[33][33][33])
{
    return (m[R1][G1][B1] - m[R1][G1][B0] - m[R1][G0][B1] + m[R1][G0][B0] -
        m[R0][G1][B1] + m[R0][G1][B0] + m[R0][G0][B1] - m[R0][G0][B0]);
}

/*
 *----------------------------------------------------------------------
 *
 *      The next two routines allow a slightly more efficient
 *      calculation of Volume() for a proposed subbox of a given box.
 *      The sum of Top() and Bottom() is the Volume() of a subbox split
 *      in the given direction and with the specified new upper
 *      bound.
 *
 *----------------------------------------------------------------------
 */

/*
 *--------------------------------------------------------------
 *
 * Bottom --
 *
 *      Compute part of Volume(cubePtr, mmt) that doesn't
 *      depend on r1, g1, or b1 (depending on dir)
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static long int
Bottom(
    Cube * cubePtr,
    unsigned char dir,
    long int m[33][33][33])
{                               /* Moment */
    switch (dir) {
    case RED:
        return -m[R0][G1][B1] + m[R0][G1][B0] + m[R0][G0][B1] - m[R0][G0][B0];
    case GREEN:
        return -m[R1][G0][B1] + m[R1][G0][B0] + m[R0][G0][B1] - m[R0][G0][B0];
    case BLUE:
        return -m[R1][G1][B0] + m[R1][G0][B0] + m[R0][G1][B0] - m[R0][G0][B0];
    }
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * Top --
 *
 *      Compute remainder of Volume(cubePtr, mmt),
 *      substituting pos for r1, g1, or b1 (depending on dir)
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static long int
Top(
    Cube * cubePtr,
    unsigned char dir,
    int pos,
    long int m[33][33][33])
{
    switch (dir) {
    case RED:
        return (m[pos][G1][B1] - m[pos][G1][B0] -
            m[pos][G0][B1] + m[pos][G0][B0]);

    case GREEN:
        return (m[R1][pos][B1] - m[R1][pos][B0] -
            m[R0][pos][B1] + m[R0][pos][B0]);

    case BLUE:
        return (m[R1][G1][pos] - m[R1][G0][pos] -
            m[R0][G1][pos] + m[R0][G0][pos]);
    }
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * Variance --
 *
 *      Compute the weighted variance of a box NB: as with
 *      the raw statistics, this is really the (variance * size)
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
Variance(
    Cube * cubePtr,
    ColorImageStatistics * s)
{
    double          dR, dG, dB, xx;

    dR = Volume(cubePtr, s->mR);
    dG = Volume(cubePtr, s->mG);
    dB = Volume(cubePtr, s->mB);
    xx = (s->gm2[R1][G1][B1] - s->gm2[R1][G1][B0] -
        s->gm2[R1][G0][B1] + s->gm2[R1][G0][B0] -
        s->gm2[R0][G1][B1] + s->gm2[R0][G1][B0] +
        s->gm2[R0][G0][B1] - s->gm2[R0][G0][B0]);
    return (xx - (dR * dR + dG * dG + dB * dB) / Volume(cubePtr, s->wt));
}

/*
 *--------------------------------------------------------------
 *
 * Maximize --
 *
 *      We want to minimize the sum of the variances of two
 *      subboxes. The sum(c^2) terms can be ignored since
 *      their sum over both subboxes is the same (the sum
 *      for the whole box) no matter where we split.  The
 *      remaining terms have a minus sign in the variance
 *      formula, so we drop the minus sign and MAXIMIZE the
 *      sum of the two terms.
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double
Maximize(
    Cube * cubePtr,
    unsigned char dir,
    int first,
    int last,
    int *cut,
    long int rWhole,
    long int gWhole,
    long int bWhole,
    long int wWhole,
    ColorImageStatistics * s)
{
    register long int rHalf, gHalf, bHalf, wHalf;
    long int        rBase, gBase, bBase, wBase;
    register int    i;
    register double temp, max;

    rBase = Bottom(cubePtr, dir, s->mR);
    gBase = Bottom(cubePtr, dir, s->mG);
    bBase = Bottom(cubePtr, dir, s->mB);
    wBase = Bottom(cubePtr, dir, s->wt);
    max = 0.0;
    *cut = -1;
    for (i = first; i < last; i++) {
        rHalf = rBase + Top(cubePtr, dir, i, s->mR);
        gHalf = gBase + Top(cubePtr, dir, i, s->mG);
        bHalf = bBase + Top(cubePtr, dir, i, s->mB);
        wHalf = wBase + Top(cubePtr, dir, i, s->wt);

        /* Now half_x is sum over lower half of box, if split at i */
        if (wHalf == 0) {       /* subbox could be empty of pixels! */
            continue;           /* never split into an empty box */
        } else {
            temp = ((double) rHalf * rHalf + (float) gHalf * gHalf +
                (double) bHalf * bHalf) / wHalf;
        }
        rHalf = rWhole - rHalf;
        gHalf = gWhole - gHalf;
        bHalf = bWhole - bHalf;
        wHalf = wWhole - wHalf;
        if (wHalf == 0) {       /* Subbox could be empty of pixels! */
            continue;           /* never split into an empty box */
        } else {
            temp += ((double) rHalf * rHalf + (float) gHalf * gHalf +
                (double) bHalf * bHalf) / wHalf;
        }
        if (temp > max) {
            max = temp;
            *cut = i;
        }
    }
    return max;
}

/*
 *--------------------------------------------------------------
 *
 * Cut --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int
Cut(
    Cube * set1,
    Cube * set2,
    ColorImageStatistics * s)
{
    unsigned char   dir;
    int             rCut, gCut, bCut;
    double          rMax, gMax, bMax;
    long int        rWhole, gWhole, bWhole, wWhole;

    rWhole = Volume(set1, s->mR);
    gWhole = Volume(set1, s->mG);
    bWhole = Volume(set1, s->mB);
    wWhole = Volume(set1, s->wt);

    rMax = Maximize(set1, RED, set1->r0 + 1, set1->r1, &rCut,
        rWhole, gWhole, bWhole, wWhole, s);
    gMax = Maximize(set1, GREEN, set1->g0 + 1, set1->g1, &gCut,
        rWhole, gWhole, bWhole, wWhole, s);
    bMax = Maximize(set1, BLUE, set1->b0 + 1, set1->b1, &bCut,
        rWhole, gWhole, bWhole, wWhole, s);

    if ((rMax >= gMax) && (rMax >= bMax)) {
        dir = RED;
        if (rCut < 0) {
            return 0;           /* can't split the box */
        }
    } else {
        dir = ((gMax >= rMax) && (gMax >= bMax)) ? GREEN : BLUE;
    }
    set2->r1 = set1->r1;
    set2->g1 = set1->g1;
    set2->b1 = set1->b1;

    switch (dir) {
    case RED:
        set2->r0 = set1->r1 = rCut;
        set2->g0 = set1->g0;
        set2->b0 = set1->b0;
        break;

    case GREEN:
        set2->g0 = set1->g1 = gCut;
        set2->r0 = set1->r0;
        set2->b0 = set1->b0;
        break;

    case BLUE:
        set2->b0 = set1->b1 = bCut;
        set2->r0 = set1->r0;
        set2->g0 = set1->g0;
        break;
    }
    set1->vol = (set1->r1 - set1->r0) * (set1->g1 - set1->g0) *
        (set1->b1 - set1->b0);
    set2->vol = (set2->r1 - set2->r0) * (set2->g1 - set2->g0) *
        (set2->b1 - set2->b0);
    return 1;
}

/*
 *--------------------------------------------------------------
 *
 * SplitColorSpace --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int
SplitColorSpace(
    ColorImageStatistics * s,
    Cube * cubes,
    int nColors)
{
    double         *vv, temp;
    register int    i;
    register int    n, k;

    vv = (double *) ckalloc(sizeof(double) * nColors);
    assert(vv);

    cubes[0].r0 = cubes[0].g0 = cubes[0].b0 = 0;
    cubes[0].r1 = cubes[0].g1 = cubes[0].b1 = 32;
    for (i = 1, n = 0; i < nColors; i++) {
        if (Cut(cubes + n, cubes + i, s)) {
            /*
             * Volume test ensures we won't try to cut one-cell box
             */
            vv[n] = vv[i] = 0.0;
            if (cubes[n].vol > 1) {
                vv[n] = Variance(cubes + n, s);
            }
            if (cubes[i].vol > 1) {
                vv[i] = Variance(cubes + i, s);
            }
        } else {
            vv[n] = 0.0;        /* don't try to split this box again */
            i--;                /* didn't create box i */
        }

        n = 0;
        temp = vv[0];
        for (k = 1; k <= i; k++) {
            if (vv[k] > temp) {
                temp = vv[k];
                n = k;
            }
        }
        if (temp <= 0.0) {
            i++;
            fprintf(stderr, "Only got %d boxes\n", i);
            break;
        }
    }
    ckfree((char *) vv);
    return i;
}

/*
 *--------------------------------------------------------------
 *
 * Mark --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void
Mark(
    Cube * cubePtr,
    int label,
    unsigned int tag[33][33][33])
{
    register int    r, g, b;

    for (r = R0 + 1; r <= R1; r++) {
        for (g = G0 + 1; g <= G1; g++) {
            for (b = B0 + 1; b <= B1; b++) {
                tag[r][g][b] = label;
            }
        }
    }
}

/*
 *--------------------------------------------------------------
 *
 * CreateColorLookupTable --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void
CreateColorLookupTable(
    ColorImageStatistics * s,
    Cube * cubes,
    int nColors,
    unsigned int lut[33][33][33])
{
    RbcPix32        color;
    unsigned int    red, green, blue;
    unsigned int    weight;
    register Cube  *cubePtr;
    register int    i;

    color.rgba.alpha = (unsigned char) -1;
    for (cubePtr = cubes, i = 0; i < nColors; i++, cubePtr++) {
        weight = Volume(cubePtr, s->wt);
        if (weight) {
            red = (Volume(cubePtr, s->mR) / weight) * (NC + 1);
            green = (Volume(cubePtr, s->mG) / weight) * (NC + 1);
            blue = (Volume(cubePtr, s->mB) / weight) * (NC + 1);
        } else {
            fprintf(stderr, "bogus box %d\n", i);
            red = green = blue = 0;
        }
        color.rgba.red = red >> 8;
        color.rgba.green = green >> 8;
        color.rgba.blue = blue >> 8;
        Mark(cubePtr, color.value, lut);
    }
}

/*
 *--------------------------------------------------------------
 *
 * MapColors --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void
MapColors(
    RbcColorImage * src,
    RbcColorImage * dest,
    unsigned int lut[33][33][33])
{
    /* Apply the color lookup table against the original image */
    int             width, height;
    int             count;
    RbcPix32       *srcPtr, *destPtr, *endPtr;
    unsigned char   alpha;

    width = src->width;
    height = src->height;
    count = width * height;

    srcPtr = src->bits;
    destPtr = dest->bits;
    for (endPtr = destPtr + count; destPtr < endPtr; srcPtr++, destPtr++) {
        alpha = srcPtr->rgba.alpha;
        destPtr->value =
            lut[srcPtr->rgba.red >> 3][srcPtr->rgba.green >> 3][srcPtr->
            rgba.blue >> 3];
        destPtr->rgba.alpha = alpha;
    }
}

/*
 *--------------------------------------------------------------
 *
 * ColorImagePixel --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static RbcPix32 *ColorImagePixel(
        RbcColorImage *imagePtr,
        int x,
        int y)
{
    return (imagePtr->bits + (imagePtr->width * y) + x);
}


/*
 *--------------------------------------------------------------
 *
 * RbcQuantizeColorImage --
 *
 *      C Implementation of Wu's Color Quantizer (v. 2) (see Graphics Gems
 *      vol. II, pp. 126-133)
 *
 *      Author: Xiaolin Wu
 *            Dept. of Computer Science Univ. of Western
 *            Ontario London, Ontario
 *            N6A 5B7
 *            wu@csd.uwo.ca
 *
 *      Algorithm:
 *            Greedy orthogonal bipartition of RGB space for variance
 *            minimization aided by inclusion-exclusion tricks.  For
 *            speed no nearest neighbor search is done. Slightly
 *            better performance can be expected by more
 *            sophisticated but more expensive versions.
 *
 *      The author thanks Tom Lane at Tom_Lane@G.GP.CS.CMU.EDU for much of
 *      additional documentation and a cure to a previous bug.
 *
 *      Free to distribute, comments and suggestions are appreciated.
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int
RbcQuantizeColorImage(
    RbcColorImage * src,        /* Source and destination images. */
    RbcColorImage * dest,       /* Source and destination images. */
    int reduceColors)
{                               /* Reduced number of colors. */
    Cube           *cubes;
    ColorImageStatistics *statistics;
    int             nColors;
    unsigned int    lut[33][33][33];
    /*
     * Allocated a structure to hold color statistics.
     */
    statistics = GetColorImageStatistics(src);
    M3d(statistics);

    cubes = (Cube *) ckalloc(sizeof(Cube) * reduceColors);
    assert(cubes);

    nColors = SplitColorSpace(statistics, cubes, reduceColors);
    assert(nColors <= reduceColors);

    CreateColorLookupTable(statistics, cubes, nColors, lut);
    ckfree((char *) statistics);
    ckfree((char *) cubes);
    MapColors(src, dest, lut);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * RbcSetRegion --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
RbcRegion2D    *
RbcSetRegion(
    int x,
    int y,
    int width,
    int height,
    RbcRegion2D * regionPtr)
{
    regionPtr->left = x;
    regionPtr->top = y;
    regionPtr->right = x + width - 1;
    regionPtr->bottom = y + height - 1;
    return regionPtr;
}

/*
 * Each call to Tk_GetImage returns a pointer to one of the following
 * structures, which is used as a token by clients (widgets) that
 * display images.
 */
typedef struct TkImageStruct {
    Tk_Window       tkwin;      /* Window passed to Tk_GetImage (needed to
                                 * "re-get" the image later if the manager
                                 * changes). */
    Display        *display;    /* Display for tkwin.  Needed because when
                                 * the image is eventually freed tkwin may
                                 * not exist anymore. */
    struct TkImageMasterStruct *masterPtr;
    /* Master for this image (identifiers image
     * manager, for example). */
    ClientData      instanceData;
    /* One word argument to pass to image manager
     * when dealing with this image instance. */
    Tk_ImageChangedProc *changeProc;
    /* Code in widget to call when image changes
     * in a way that affects redisplay. */
    ClientData      widgetClientData;
    /* Argument to pass to changeProc. */
    struct Image   *nextPtr;    /* Next in list of all image instances
                                 * associated with the same name. */

} TkImage;

/*
 * For each image master there is one of the following structures,
 * which represents a name in the image table and all of the images
 * instantiated from it.  Entries in mainPtr->imageTable point to
 * these structures.
 */
typedef struct TkImageMasterStruct {
    Tk_ImageType   *typePtr;    /* Information about image type.  NULL means
                                 * that no image manager owns this image:  the
                                 * image was deleted. */
    ClientData      masterData; /* One-word argument to pass to image mgr
                                 * when dealing with the master, as opposed
                                 * to instances. */
    int             width, height;      /* Last known dimensions for image. */
    Tcl_HashTable  *tablePtr;   /* Pointer to hash table containing image
                                 * (the imageTable field in some TkMainInfo
                                 * structure). */
    Tcl_HashEntry  *hPtr;       /* Hash entry in mainPtr->imageTable for
                                 * this structure (used to delete the hash
                                 * entry). */
    TkImage        *instancePtr;        /* Pointer to first in list of instances
                                         * derived from this name. */
} TkImageMaster;

typedef struct TkPhotoMasterStruct TkPhotoMaster;
typedef struct TkColorTableStruct TkColorTable;

typedef struct TkPhotoInstanceStruct {
    TkPhotoMaster  *masterPtr;  /* Pointer to master for image. */
    Display        *display;    /* Display for windows using this instance. */
    Colormap        colormap;   /* The image may only be used in windows with
                                 * this particular colormap. */
    struct TkPhotoInstanceStruct *nextPtr;
    /* Pointer to the next instance in the list
     * of instances associated with this master. */
    int             refCount;   /* Number of instances using this structure. */
    Tk_Uid          palette;    /* Palette for these particular instances. */
    double          outputGamma;        /* Gamma value for these instances. */
    Tk_Uid          defaultPalette;     /* Default palette to use if a palette
                                         * is not specified for the master. */
    TkColorTable   *colorTablePtr;      /* Pointer to information about colors
                                         * allocated for image display in windows
                                         * like this one. */
    Pixmap          pixels;     /* X pixmap containing dithered image. */
    int             width, height;      /* Dimensions of the pixmap. */
    char           *error;      /* Error image, used in dithering. */
    XImage         *imagePtr;   /* Image structure for converted pixels. */
    XVisualInfo     visualInfo; /* Information about the visual that these
                                 * windows are using. */
    GC              gc;         /* Graphics context for writing images
                                 * to the pixmap. */
} TkPhotoInstance;

/*
 * ----------------------------------------------------------------------
 *
 * Tk_ImageDeleted --
 *
 *      Is there any other way to determine if an image has been
 *      deleted?
 *
 * Results:
 *      Returns 1 if the image has been deleted, 0 otherwise.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
int
Tk_ImageIsDeleted(
    Tk_Image tkImage)
{                               /* Token for image. */
    TkImage        *imagePtr = (TkImage *) tkImage;

    if (imagePtr->masterPtr == NULL) {
        return TRUE;
    }
    return (imagePtr->masterPtr->typePtr == NULL);
}

/*
 *--------------------------------------------------------------
 *
 * Tk_ImageGetMaster --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Tk_ImageMaster
Tk_ImageGetMaster(
    Tk_Image tkImage)
{                               /* Token for image. */
    TkImage        *imagePtr = (TkImage *) tkImage;

    return (Tk_ImageMaster) imagePtr->masterPtr;
}

/*
 *--------------------------------------------------------------
 *
 * Tk_ImageGetType --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Tk_ImageType   *
Tk_ImageGetType(
    Tk_Image tkImage)
{                               /* Token for image. */
    TkImage        *imagePtr = (TkImage *) tkImage;

    return imagePtr->masterPtr->typePtr;
}

/*
 *--------------------------------------------------------------
 *
 * Tk_ImageGetPhotoPixmap --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Pixmap
Tk_ImageGetPhotoPixmap(
    Tk_Image tkImage)
{                               /* Token for image. */
    TkImage        *imagePtr = (TkImage *) tkImage;

    if (strcmp(imagePtr->masterPtr->typePtr->name, "photo") == 0) {
        TkPhotoInstance *instPtr = (TkPhotoInstance *) imagePtr->instanceData;
        return instPtr->pixels;
    }
    return None;
}

/*
 *--------------------------------------------------------------
 *
 * Tk_ImageGetPhotoGC --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
GC
Tk_ImageGetPhotoGC(
    Tk_Image photoImage)
{                               /* Token for image. */
    TkImage        *imagePtr = (TkImage *) photoImage;
    if (strcmp(imagePtr->masterPtr->typePtr->name, "photo") == 0) {
        TkPhotoInstance *instPtr = (TkPhotoInstance *) imagePtr->instanceData;
        return instPtr->gc;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TempImageChangedProc
 *
 *      The image is over-written each time it's resized.  We always
 *      resample from the color image we saved when the photo image
 *      was specified (-image option). So we only worry if the image
 *      is deleted.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void
TempImageChangedProc(
    ClientData clientData,
    int x,                      /* Not used. */
    int y,                      /* Not used. */
    int width,                  /* Not used. */
    int height,                 /* Not used. */
    int imageWidth,             /* Not used. */
    int imageHeight)
{                               /* Not used. */
}

/*
 *--------------------------------------------------------------
 *
 * RbcCreateTemporaryImage --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Tk_Image
RbcCreateTemporaryImage(
    Tcl_Interp * interp,
    Tk_Window tkwin,
    ClientData clientData)
{
    Tk_Image        token;
    char           *name;       /* Contains image name. */

    if (Tcl_Eval(interp, "image create photo") != TCL_OK) {
        return NULL;
    }
    name = (char *) Tcl_GetStringResult(interp);
    token = Tk_GetImage(interp, tkwin, name, TempImageChangedProc, clientData);
    if (token == NULL) {
        return NULL;
    }
    return token;
}

/*
 *--------------------------------------------------------------
 *
 * RbcDestroyTemporaryImage --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int
RbcDestroyTemporaryImage(
    Tcl_Interp * interp,
    Tk_Image tkImage)
{
    if (tkImage != NULL) {
        if (Tcl_VarEval(interp, "image delete ", RbcNameOfImage(tkImage),
                (char *) NULL) != TCL_OK) {
            return TCL_ERROR;
        }
        Tk_FreeImage(tkImage);
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * RbcNameOfImage --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
const char     *
RbcNameOfImage(
    Tk_Image tkImage)
{
    Tk_ImageMaster  master;

    master = Tk_ImageGetMaster(tkImage);
    return Tk_NameOfImage(master);
}


/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
