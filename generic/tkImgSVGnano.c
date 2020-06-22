/*
 * tkImgSVGnano.c
 *
 *	A photo file handler for SVG files.
 *
 * Copyright (c) 2013-14 Mikko Mononen memon@inside.org
 * Copyright (c) 2018 Christian Gollwitzer auriocus@gmx.de
 * Copyright (c) 2018 Rene Zaumseil r.zaumseil@freenet.de
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * This handler is build using the original nanosvg library files from
 * https://github.com/memononen/nanosvg and the tcl extension files from
 * https://github.com/auriocus/tksvg
 *
 */

#include "tkInt.h"
#define NANOSVG_malloc	ckalloc
#define NANOSVG_realloc	ckrealloc
#define NANOSVG_free	ckfree
#define NANOSVG_SCOPE MODULE_SCOPE
#define NANOSVG_ALL_COLOR_KEYWORDS
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

/*
 * Serialized data version
 * This consists of "svg" plus binary '1' at byte locations in an int.
 * It serves as indication, version and endinaess check
 */

#define STRUCTURE_VERSION ('s'+256*'v'+65535*'g'+16777216*1)

/*
 * Serialized image data header
 */

typedef struct {
    unsigned int structureVersion;
    float dpi;
    float width;				// Width of the image.
    float height;				// Height of the image.
    int shapeCount;
    int pathCount;
    int ptsCount;
    int gradientCount;
    int gradientStopCount;
} serializedHeader;

/*
 * Result of options parsing and first block in driver internal DString
 */

typedef struct {
    double scale;
    int scaleToHeight;
    int scaleToWidth;
    float dpi;
    int svgBlobFollows;
} optionsStruct;
    
/*
 * Serialized structures from NSCG
 * All pointers are replaced by array indexes
 */

typedef struct NSVGgradientSerialized {
    float xform[6];
    char spread;
    float fx, fy;
    int nstops;
    int stops;
} NSVGgradientSerialized;

typedef struct NSVGpaintSerialized {
    char type;
    union {
	unsigned int color;
	int gradient;
    };
} NSVGpaintSerialized;


typedef struct NSVGpathSerialized
{
    int pts;					// Cubic bezier points: x0,y0, [cpx1,cpx1,cpx2,cpy2,x1,y1], ...
    int npts;					// Total number of bezier points.
						// Caution: pair of floats
    char closed;				// Flag indicating if shapes should be treated as closed.
    float bounds[4];			// Tight bounding box of the shape [minx,miny,maxx,maxy].
    int next;		// Pointer to next path, or NULL if last element.
} NSVGpathSerialized;

typedef struct NSVGshapeSerialized
{
    char id[64];				// Optional 'id' attr of the shape or its group
    NSVGpaintSerialized fill;		// Fill paint
    NSVGpaintSerialized stroke;		// Stroke paint
    float opacity;				// Opacity of the shape.
    float strokeWidth;			// Stroke width (scaled).
    float strokeDashOffset;		// Stroke dash offset (scaled).
    float strokeDashArray[8];			// Stroke dash array (scaled).
    char strokeDashCount;				// Number of dash values in dash array.
    char strokeLineJoin;		// Stroke join type.
    char strokeLineCap;			// Stroke cap type.
    float miterLimit;			// Miter limit
    char fillRule;				// Fill rule, see NSVGfillRule.
    unsigned char flags;		// Logical or of NSVG_FLAGS_* flags
    float bounds[4];			// Tight bounding box of the shape [minx,miny,maxx,maxy].
    int paths;			// Linked list of paths in the image.
    int next;		// Pointer to next shape, or NULL if last element.
} NSVGshapeSerialized;


/*
 * Per interp cache of last NSVGimage which was matched to
 * be immediately rasterized after the match. This helps to
 * eliminate double parsing of the SVG file/string.
 */

typedef struct {
    /* A poiner to remember if it is the same svn image (data)
     * It is a Tcl_Channel if image created by -file option
     * or a Tcl_Obj, if image is created with the -data option
     */
    ClientData dataOrChan;
    Tcl_DString formatString;
    NSVGimage *nsvgImage;
} NSVGcache;

static int		FileMatchSVG(Tcl_Interp *interp, Tcl_Channel chan,
			    const char *fileName, Tcl_Obj *formatObj,
			    Tcl_Obj *metadataInObj, int *widthPtr,
			    int *heightPtr, Tcl_Obj *metadataOutObj,
			    int *closeChannelPtr,
			    Tcl_DString *driverInternal);
static int		FileReadSVG(Tcl_Interp *interp, Tcl_Channel chan,
			    const char *fileName, Tcl_Obj *format,
			    Tcl_Obj *metadataIn, Tk_PhotoHandle imageHandle,
			    int destX, int destY, int width, int height,
			    int srcX, int srcY,
			    Tcl_Obj *metadataOut, Tcl_DString *driverInternal);
static int		StringMatchSVG(Tcl_Interp *interp, Tcl_Obj *dataObj,
			    Tcl_Obj *format, Tcl_Obj *metadataIn, int *widthPtr,
			    int *heightPtr, Tcl_Obj *metadataOutObj,
			    Tcl_DString *driverInternal);
static int		StringReadSVG(Tcl_Interp *interp, Tcl_Obj *dataObj,
			    Tcl_Obj *format, Tcl_Obj *metadataIn,
			    Tk_PhotoHandle imageHandle,
			    int destX, int destY, int width, int height,
			    int srcX, int srcY,
			    Tcl_Obj *metadataOut, Tcl_DString *driverInternal);
static int		ParseOptions(Tcl_Interp *interp, Tcl_Obj *formatObj,
			    optionsStruct *optionsPtr);
static NSVGimage *	ParseSVG(Tcl_Interp *interp, Tcl_Obj *dataObj, float dpi);
static int		RasterizeSVG(Tcl_Interp *interp,
			    Tk_PhotoHandle imageHandle,
			    char *svgBlob, optionsStruct * optionsPtr,
			    int destX, int destY, int width, int height,
			    int srcX, int srcY);
static double		GetScaleFromParameters(
			    serializedHeader *serializedHeaderPtr,
			    optionsStruct * optionsPtr, int *widthPtr,
			    int *heightPtr);
static void		SerializeNSVGImage(Tcl_DString *driverInternalPtr,
			    NSVGimage *nsvgImage);
static void SerializePath(struct NSVGpath *pathPtr,
			    serializedHeader *serializedHeaderPtr,
			    Tcl_DString *pathDStringPtr,
			    Tcl_DString *ptrDStringPtr);
static struct NSVGpaintSerialized SerializePaint(struct NSVGpaint *paintPtr,
			    serializedHeader *serializedHeaderPtr,
			    Tcl_DString *gradientDStringPtr,
			    Tcl_DString *gradientStopDStringPtr);
static char * StringCheckMetadata(Tcl_Obj *dataObj, Tcl_Obj *metadataInObj,
			    float dpi, int *LengthPtr);
static int SaveSVGBLOBToMetadata(Tcl_Interp *interp, Tcl_Obj *metadataOutObj,
			    Tcl_DString *driverInternalPtr);
static void nsvgRasterizeSerialized(NSVGrasterizer* r, char *svgBlobPtr, float tx,
			    float ty, float scale, unsigned char* dst, int w,
			    int h, int stride);

/*
 * The format record for the SVG nano file format:
 */

Tk_PhotoImageFormatVersion3 tkImgFmtSVGnano = {
    "svg",			/* name */
    FileMatchSVG,		/* fileMatchProc */
    StringMatchSVG,		/* stringMatchProc */
    FileReadSVG,		/* fileReadProc */
    StringReadSVG,		/* stringReadProc */
    NULL,			/* fileWriteProc */
    NULL,			/* stringWriteProc */
    NULL
};

/*
 *----------------------------------------------------------------------
 *
 * FileMatchSVG --
 *
 *	This function is invoked by the photo image type to see if a file
 *	contains image data in SVG format.
 *
 * Results:
 *	The return value is >0 if the file can be successfully parsed,
 *	and 0 otherwise.
 *
 * Side effects:
 *	The file is saved in the internal cache for further use.
 *
 *----------------------------------------------------------------------
 */

static int
FileMatchSVG(
    Tcl_Interp *interp,		/* interpreter pointer */
    Tcl_Channel chan,		/* The image file, open for reading. */
    const char *fileName,	/* The name of the image file. */
    Tcl_Obj *formatObj,		/* User-specified format object, or NULL. */
    Tcl_Obj *metadataInObj,	/* metadata input, may be NULL */
    int *widthPtr, int *heightPtr,
				/* The dimensions of the image are returned
				 * here if the file is a valid raw GIF file. */
    Tcl_Obj *metadataOut,	/* metadata return dict, may be NULL */
    int *closeChannelPtr,	/* Return if the channel may be closed */
    Tcl_DString *driverInternalPtr)
				/* memory passed to FileReadGIF */
{
    Tcl_Obj *dataObj;
    NSVGimage *nsvgImage;
    (void)fileName;
    serializedHeader *serializedHeaderPtr;
    optionsStruct options;

    /*
     * Parse the options. Unfortunately, any error can not be returned.
     */

    if (TCL_OK != ParseOptions(interp, formatObj, &options) ) {
	return 0;
    }

    /*
     * Read the file data into a TCL object
     */

    dataObj = Tcl_NewObj();
    if (Tcl_ReadChars(chan, dataObj, -1, 0) == TCL_IO_FAILURE) {
	/* in case of an error reading the file */
	Tcl_DecrRefCount(dataObj);
	return 0;
    }

    /*
     * Parse the SVG data
     */

    nsvgImage = ParseSVG(interp, dataObj, options.dpi);
    Tcl_DecrRefCount(dataObj);
    if (nsvgImage != NULL) {

	/*
	 * On successful parse, save the data in the header structure
	 */

	Tcl_DStringSetLength(driverInternalPtr,
		sizeof(optionsStruct)+sizeof(serializedHeader));
	
	options.svgBlobFollows = 1;
	memcpy( Tcl_DStringValue(driverInternalPtr), &options,
		sizeof(optionsStruct));
	
	serializedHeaderPtr = (serializedHeader *)
		(Tcl_DStringValue(driverInternalPtr) + sizeof(optionsStruct));
    
	serializedHeaderPtr->width = nsvgImage->width;
	serializedHeaderPtr->height = nsvgImage->height;
	serializedHeaderPtr->dpi = options.dpi;
        GetScaleFromParameters(serializedHeaderPtr, &options, widthPtr,
		heightPtr);
        if ((*widthPtr <= 0.0) || (*heightPtr <= 0.0)) {
	    nsvgDelete(nsvgImage);
	    return 0;
        }
	
	/*
	 * Serialize the NSVGImage structure
	 * As the DString is resized, serializedHeaderPtr may get invalid
	 */

	SerializeNSVGImage(driverInternalPtr, nsvgImage);
	nsvgDelete(nsvgImage);
        return 1;
    }
    return 0;

}

/*
 *----------------------------------------------------------------------
 *
 * SerializeNSVGImage --
 *
 *	This function saves the NSVGimage structure into the DString.
 *
 * Results:
 *	none.
 *
 * Side effects:
 *	The DString size is changed and thus, the value pointer may
 *	change.
 *
 *----------------------------------------------------------------------
 */

static void SerializeNSVGImage(Tcl_DString *driverInternalPtr,
	NSVGimage *nsvgImage) {
    serializedHeader *serializedHeaderPtr;
    Tcl_DString  shapeDString, pathDString, ptsDString, gradientDString,
	gradientStopDString;
    NSVGshape *shapePtr;

    serializedHeaderPtr = (serializedHeader *)
	    (Tcl_DStringValue(driverInternalPtr) + sizeof(optionsStruct));
    Tcl_DStringInit(&shapeDString);
    Tcl_DStringInit(&pathDString);
    Tcl_DStringInit(&ptsDString);
    Tcl_DStringInit(&gradientDString);
    Tcl_DStringInit(&gradientStopDString);

    serializedHeaderPtr->structureVersion = STRUCTURE_VERSION;
    serializedHeaderPtr->shapeCount = 0;
    serializedHeaderPtr->pathCount = 0;
    serializedHeaderPtr->ptsCount = 0;
    serializedHeaderPtr->gradientCount = 0;
    serializedHeaderPtr->gradientStopCount = 0;
    
    for ( shapePtr = nsvgImage->shapes; shapePtr != NULL;
	    shapePtr = shapePtr->next) {
	NSVGshapeSerialized shapeSerialized;

	/*
	 * Copy serialized shape fix data
	 */

	memcpy(shapeSerialized.id, shapePtr->id, 64 * sizeof(char));

	shapeSerialized.fill = SerializePaint(&(shapePtr->fill),
		serializedHeaderPtr, &gradientDString, &gradientStopDString);

	shapeSerialized.stroke = SerializePaint(&(shapePtr->stroke),
		serializedHeaderPtr, &gradientDString, &gradientStopDString);

	shapeSerialized.opacity = shapePtr->opacity;
	shapeSerialized.strokeWidth = shapePtr->strokeWidth;
	shapeSerialized.strokeDashOffset = shapePtr->strokeDashOffset;
	memcpy(shapeSerialized.strokeDashArray, shapePtr->strokeDashArray,
		8*sizeof(float));
	shapeSerialized.strokeDashCount = shapePtr->strokeDashCount;
	shapeSerialized.strokeLineJoin = shapePtr->strokeLineJoin;
	shapeSerialized.strokeLineCap = shapePtr->strokeLineCap;
	shapeSerialized.miterLimit = shapePtr->miterLimit;
	shapeSerialized.fillRule = shapePtr->fillRule;
	shapeSerialized.flags = shapePtr->flags;
	memcpy(shapeSerialized.bounds, shapePtr->bounds, 4*sizeof(float));
	
	/*
	 * Serialize the paths linked list
	 */

	if ( shapePtr->paths == NULL ) {
	    shapeSerialized.paths = -1;
	} else {
	    shapeSerialized.paths = serializedHeaderPtr->pathCount;
	    SerializePath(shapePtr->paths, serializedHeaderPtr, &pathDString,
		    &ptsDString);
	}

	/*
	 * generate next array position and save to DString
	 */
	
	serializedHeaderPtr->shapeCount++;
	shapeSerialized.next =
		shapePtr->next == NULL ? -1:
		serializedHeaderPtr->shapeCount;
	
	Tcl_DStringAppend(&shapeDString,
		(const char *)&shapeSerialized, sizeof(NSVGshapeSerialized));
    }
    
    /*
     * Write the DStrings into the driver memory one after the other
     * Note: serializedHeaderPtr may get invalid due to DString resize
     */
    
    if (Tcl_DStringLength(&shapeDString) > 0) {
    	Tcl_DStringAppend(driverInternalPtr,
		    Tcl_DStringValue(&shapeDString),
		    Tcl_DStringLength(&shapeDString));
    }
    Tcl_DStringFree(&shapeDString);

    if (Tcl_DStringLength(&pathDString) > 0) {
    	Tcl_DStringAppend(driverInternalPtr,
		    Tcl_DStringValue(&pathDString),
		    Tcl_DStringLength(&pathDString));
    }
    Tcl_DStringFree(&pathDString);
    
    if (Tcl_DStringLength(&ptsDString) > 0) {
    	Tcl_DStringAppend(driverInternalPtr,
		    Tcl_DStringValue(&ptsDString),
		    Tcl_DStringLength(&ptsDString));
    }
    Tcl_DStringFree(&ptsDString);

    if (Tcl_DStringLength(&gradientDString) > 0) {
    	Tcl_DStringAppend(driverInternalPtr,
		    Tcl_DStringValue(&gradientDString),
		    Tcl_DStringLength(&gradientDString));
    }
    Tcl_DStringFree(&gradientDString);

    if (Tcl_DStringLength(&gradientStopDString) > 0) {
    	Tcl_DStringAppend(driverInternalPtr,
		    Tcl_DStringValue(&gradientStopDString),
		    Tcl_DStringLength(&gradientStopDString));
    }
    Tcl_DStringFree(&gradientStopDString);
}

/*
 *----------------------------------------------------------------------
 *
 * SerializePaint --
 *
 *	This function transforms the NSVGpaint structure to a serialize
 *	version.
 *	The child structures gradient and gradientStop are serialized into
 *	their DString memory.
 *
 * Results:
 *	NSVGPaintSerialized structure.
 *
 * Side effects:
 *	The DString size is changed and thus, the value pointer may
 *	change.
 *
 *----------------------------------------------------------------------
 */

static struct NSVGpaintSerialized SerializePaint(struct NSVGpaint *paintPtr,
	serializedHeader *serializedHeaderPtr,
	Tcl_DString *gradientDStringPtr,
	Tcl_DString *gradientStopDStringPtr)
{
    struct NSVGpaintSerialized paintSerialized;
    
    paintSerialized.type = paintPtr->type;

    if (paintPtr->type == NSVG_PAINT_LINEAR_GRADIENT
	    || paintPtr->type == NSVG_PAINT_RADIAL_GRADIENT) {
	
	/*
	 * Gradient union pointer present
	 */

	NSVGgradient* gradientPtr;
	NSVGgradientSerialized gradientSerialized;
	
	gradientPtr = paintPtr->gradient;
	memcpy(&(gradientSerialized.xform),  gradientPtr->xform,
		6 * sizeof(float) );
	gradientSerialized.spread = gradientPtr->spread;
	gradientSerialized.fx = gradientPtr->fx;
	gradientSerialized.fy = gradientPtr->fy;
	
	/*
	 * Copy gradient stop array to DString
	 */

	gradientSerialized.nstops = gradientPtr->nstops;
	if ( gradientPtr->nstops == 0 ) {
	    gradientSerialized.stops = -1;
	} else {
	    gradientSerialized.stops = serializedHeaderPtr->gradientStopCount;
	    Tcl_DStringAppend(gradientStopDStringPtr,
		    (const char *)gradientPtr->stops,
		    gradientPtr->nstops * sizeof(NSVGgradientStop));

	    (serializedHeaderPtr->gradientStopCount) += gradientPtr->nstops;
	}
	(serializedHeaderPtr->gradientStopCount) += gradientPtr->nstops;

	paintSerialized.gradient = serializedHeaderPtr->gradientCount;
	Tcl_DStringAppend(gradientDStringPtr,
		(const char *) & gradientSerialized,
		sizeof(NSVGgradientSerialized));
	(serializedHeaderPtr->gradientCount) ++;

    } else {
	
	/*
	 * Color union or nothing present
	 */

	paintSerialized.color = paintPtr->color;
    }
    return paintSerialized;
}
/*
 *----------------------------------------------------------------------
 *
 * SerializePath --
 *
 *	This function serializes a linked list of NSVGpath structure into
 *	the corresponding DString array.
 *
 * Results:
 *	none
 *
 * Side effects:
 *	The DString size is changed and thus, the value pointer may
 *	change.
 *
 *----------------------------------------------------------------------
 */

static void SerializePath(struct NSVGpath *pathPtr,
	serializedHeader *serializedHeaderPtr,
	Tcl_DString *pathDStringPtr,
	Tcl_DString *ptsDStringPtr)
{
 
    /*
     * loop over path linked list
     */
    
    for (;pathPtr != NULL; pathPtr = pathPtr->next) {
	NSVGpathSerialized pathSerialized;
	int index;

	/*
	 * Save points in the ptr dstring.
	 * The first index and the count is saved.
	 */

	pathSerialized.npts = pathPtr->npts;
	if (pathPtr->npts == 0) {
	    pathSerialized.pts = -1;
	} else {

	    /*
	     * Attention: npts is a pair of floats
	     */

	    pathSerialized.pts = serializedHeaderPtr->ptsCount;
	    for (index = 0; index < (pathPtr->npts) * 2; index++) {
	        float ptCurrent;
	        ptCurrent = pathPtr->pts[index];
	        Tcl_DStringAppend(ptsDStringPtr,
	    	    (const char *)&ptCurrent, sizeof(float));
	        (serializedHeaderPtr->ptsCount)++;
	    }
	}
    
	/*
	 * Copy the other items of the path structure
	 */
    
	pathSerialized.closed = pathPtr->closed;
	memcpy(pathSerialized.bounds, pathPtr->bounds, 4*sizeof(float));
    
	/*
	 * Build the next item and add to DString
	 */

	serializedHeaderPtr->pathCount++;
	pathSerialized.next = (pathPtr->next == NULL) ? -1 :
		serializedHeaderPtr->pathCount;

	Tcl_DStringAppend(pathDStringPtr,
	        (const char *)&pathSerialized, sizeof(NSVGpathSerialized));
    }
}
/*
 *----------------------------------------------------------------------
 *
 * FileReadSVG --
 *
 *	This function is called by the photo image type to read SVG format
 *	data from a file and write it into a given photo image.
 *
 * Results:
 *	A standard TCL completion code. If TCL_ERROR is returned then an error
 *	message is left in the interp's result.
 *
 * Side effects:
 *	The access position in file f is changed, and new data is added to the
 *	image given by imageHandle.
 *
 *----------------------------------------------------------------------
 */

static int
FileReadSVG(
    Tcl_Interp *interp,		/* Interpreter to use for reporting errors. */
    Tcl_Channel chan,		/* The image file, open for reading. */
    const char *fileName,	/* The name of the image file. */
    Tcl_Obj *formatObj,		/* User-specified format object, or NULL. */
    Tcl_Obj *metadataInObj,	/* metadata input, may be NULL */
    Tk_PhotoHandle imageHandle,	/* The photo image to write into. */
    int destX, int destY,	/* Coordinates of top-left pixel in photo
				 * image to be written to. */
    int width, int height,	/* Dimensions of block of photo image to be
				 * written to. */
    int srcX, int srcY,		/* Coordinates of top-left pixel to be used in
				 * image being read. */
    Tcl_Obj *metadataOutObj,	/* metadata return dict, may be NULL */
    Tcl_DString *driverInternalPtr)
				/* memory passed from FileMatchGIF */
{
    int result;
    optionsStruct * optionsPtr;
    char * svgBlob;
    (void)fileName;
    
    
    optionsPtr = (optionsStruct *) Tcl_DStringValue(driverInternalPtr);
    svgBlob = Tcl_DStringValue(driverInternalPtr) + sizeof(optionsStruct);

    /*
     * Raster the parsed SVG from the SVGBLOB in the driver internal DString
     */

    result = RasterizeSVG(interp, imageHandle, svgBlob, optionsPtr, destX,
	    destY, width, height, srcX, srcY);

    /*
     * On success, output the SVGBLOB as metadata
     */

    if (result == TCL_OK) {
	result =  SaveSVGBLOBToMetadata(interp, metadataOutObj,
		driverInternalPtr);
    }

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringMatchSVG --
 *
 *	This function is invoked by the photo image type to see if a string
 *	contains image data in SVG format.
 *
 * Results:
 *	The return value is >0 if the file can be successfully parsed,
 *	and 0 otherwise.
 *
 * Side effects:
 *	The file is saved in the internal cache for further use.
 *
 *----------------------------------------------------------------------
 */

static int
StringMatchSVG(
    Tcl_Interp *interp,		/* interpreter to report errors */
    Tcl_Obj *dataObj,		/* the object containing the image data */
    Tcl_Obj *formatObj,		/* the image format object, or NULL */
    Tcl_Obj *metadataInObj,	/* metadata input, may be NULL */
    int *widthPtr,		/* where to put the string width */
    int *heightPtr,		/* where to put the string height */
    Tcl_Obj *metadataOut,	/* metadata return dict, may be NULL */
    Tcl_DString *driverInternalPtr)
				/* memory to pass to StringReadGIF */
{
    TkSizeT length;
    NSVGimage *nsvgImage;
    serializedHeader *serializedHeaderPtr;
    char * svgBlobPtr;
    optionsStruct options;

    /*
     * Parse the options. Unfortunately, any error can not be returned.
     */
    
    if (TCL_OK != ParseOptions(interp, formatObj, &options) ) {
	return 0;
    }
    
    /*
     * Check for the special data to indicate that the metadata should be used.
     * On special data, get the serialized header structure and check it.
     */
    
    svgBlobPtr = StringCheckMetadata( dataObj, metadataInObj, options.dpi,
	    &length);

    if (NULL != svgBlobPtr) {
	serializedHeaderPtr = (serializedHeader *) svgBlobPtr;
    } else {
	Tcl_DStringSetLength(driverInternalPtr,
		sizeof(optionsStruct)+sizeof(serializedHeader));
	
	options.svgBlobFollows = 1;
	memcpy( Tcl_DStringValue(driverInternalPtr), &options,
		sizeof(optionsStruct));
	
	serializedHeaderPtr = (serializedHeader *)
		(Tcl_DStringValue(driverInternalPtr) + sizeof(optionsStruct));
    }

    /*
     * Use the metadata svg blob if present to return width and height
     */
    
    if (NULL != svgBlobPtr) {
	/*
	 * Save the options struct in the driver internal DString
	 */
	options.svgBlobFollows = 0;
	Tcl_DStringSetLength(driverInternalPtr, sizeof(optionsStruct));
	memcpy(Tcl_DStringValue(driverInternalPtr), &options,
		sizeof(optionsStruct));
	
	GetScaleFromParameters(serializedHeaderPtr, &options, widthPtr,
		heightPtr);
	return 1;
    }

    /*
     * Check the passed data object and serialize it on success.
     */

    nsvgImage = ParseSVG(interp, dataObj, options.dpi);

    if (nsvgImage != NULL) {
	serializedHeaderPtr->width = nsvgImage->width;
	serializedHeaderPtr->height = nsvgImage->height;
	serializedHeaderPtr->dpi = options.dpi;
        GetScaleFromParameters(serializedHeaderPtr, &options, widthPtr,
		heightPtr);
        if ((*widthPtr <= 0.0) || (*heightPtr <= 0.0)) {
	    nsvgDelete(nsvgImage);
	    return 0;
        }

	/*
	 * Serialize the NSVGImage structure
	 * As the DString is resized, serializedHeaderPtr may get invalid
	 */

	SerializeNSVGImage(driverInternalPtr, nsvgImage);
	
	nsvgDelete(nsvgImage);
        return 1;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * StringCheckMetadata --
 *
 *	Check the passed tring for a metadata serialized structure.
 *
 * Results:
 *	The svg blob pointer on success, NULL otherwise.
 *
 * Side effects:
 *	The file is saved in the internal cache for further use.
 *
 *----------------------------------------------------------------------
 */

static char * StringCheckMetadata(
    Tcl_Obj *dataObj,		/* the object containing the image data */
    Tcl_Obj *metadataInObj,	/* metadata input, may be NULL */
    float dpi,			/* options dpi must match svgblob */
    TkSizeT *lengthOutPtr)	/* output data length on success */
{
    TkSizeT length;
    const char *data;
    serializedHeader *serializedHeaderPtr;
    char * svgBlobPtr;
    Tcl_Obj *itemData;

    /*
     * Check for the special data to indicate that the metadata should be used.
     * On special data, get the serialized header structure and check it.
     */

    if (NULL == metadataInObj) {
	return NULL;
    }

    data = TkGetStringFromObj(dataObj, &length);
    if (0 != strcmp(data, "<svg data=\"metadata\" />") ) {
	return NULL;
    }
    if (TCL_ERROR == Tcl_DictObjGet(NULL, metadataInObj,
	    Tcl_NewStringObj("SVGBLOB",-1), &itemData)) {
	return NULL;
    }
    if (itemData == NULL) {
	return NULL;
    }
    svgBlobPtr = Tcl_GetByteArrayFromObj(itemData, &length);
    if (length < sizeof(serializedHeader) ) {
	return NULL;
    }
    serializedHeaderPtr = (serializedHeader *)svgBlobPtr;
    if (serializedHeaderPtr->structureVersion != STRUCTURE_VERSION
	    || serializedHeaderPtr->dpi != dpi
    ) {
	return NULL;
    }
    *lengthOutPtr = length;
    return svgBlobPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * StringReadSVG --
 *
 *	This function is called by the photo image type to read SVG format
 *	data from a string and write it into a given photo image.
 *
 * Results:
 *	A standard TCL completion code. If TCL_ERROR is returned then an error
 *	message is left in the interp's result.
 *
 * Side effects:
 *	New data is added to the image given by imageHandle.
 *
 *----------------------------------------------------------------------
 */

static int
StringReadSVG(
    Tcl_Interp *interp,		/* interpreter for reporting errors in */
    Tcl_Obj *dataObj,		/* object containing the image */
    Tcl_Obj *formatObj,		/* format object, or NULL */
    Tcl_Obj *metadataInObj,	/* metadata input, may be NULL */
    Tk_PhotoHandle imageHandle,	/* the image to write this data into */
    int destX, int destY,	/* The rectangular region of the */
    int width, int height,	/* image to copy */
    int srcX, int srcY,
    Tcl_Obj *metadataOutObj,	/* metadata return dict, may be NULL */
    Tcl_DString *driverInternalPtr)
				/* memory passed from StringReadSVG */
{
    int result;
    TkSizeT length;
    char * svgBlobPtr;
    optionsStruct * optionsPtr;
    Tcl_Obj *itemData;
    
    optionsPtr = (optionsStruct *) Tcl_DStringValue(driverInternalPtr);
    if (optionsPtr->svgBlobFollows) {
	svgBlobPtr = Tcl_DStringValue(driverInternalPtr)+ sizeof(optionsStruct);
    } else {
	if (NULL == metadataInObj
		|| TCL_ERROR == Tcl_DictObjGet(NULL, metadataInObj,
		    Tcl_NewStringObj("SVGBLOB",-1), &itemData)
		|| itemData == NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "internal error: -metadata missing", -1));
	    Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "BAD_SCALE",
		    NULL);
	    return TCL_ERROR;
        }

	svgBlobPtr = Tcl_GetByteArrayFromObj(itemData, &length);
    }

    result = RasterizeSVG(interp, imageHandle,
	    svgBlobPtr, optionsPtr, destX, destY, width, height, srcX, srcY);
    if (result != TCL_OK) {
	return result;
    }

    if (!optionsPtr->svgBlobFollows) {
	return TCL_OK;
    }
    
    return SaveSVGBLOBToMetadata(interp, metadataOutObj, driverInternalPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * SaveSVGBLOBToMetadata --
 *
 *	Copy the driver internal DString into the metadata key SVGBLOB.
 *
 * Results:
 *	A TCL result value.
 *
 * Side effects:
 *	Change the output metadata.
 *
 *----------------------------------------------------------------------
 */

static int SaveSVGBLOBToMetadata(
    Tcl_Interp *interp,		/* interpreter for reporting errors in */
    Tcl_Obj *metadataOutObj,	/* metadata return dict, may be NULL */
    Tcl_DString *driverInternalPtr)
				/* memory passed from xxxReadSVG */
{
    if (metadataOutObj == NULL) {
	return TCL_OK;
    }
    return Tcl_DictObjPut(interp, metadataOutObj,
	Tcl_NewStringObj("SVGBLOB",-1),
	Tcl_NewByteArrayObj(
		Tcl_DStringValue(driverInternalPtr) + sizeof(optionsStruct),
		Tcl_DStringLength(driverInternalPtr) - sizeof(optionsStruct)));
}

/*
 *----------------------------------------------------------------------
 *
 * ParseOptions --
 *
 *	Parse the options given in the -format parameter.
 *
 * Results:
 *	A normal tcl result
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static int
ParseOptions(
    Tcl_Interp *interp,
    Tcl_Obj *formatObj,
    optionsStruct * optionsPtr)
{
    Tcl_Obj **objv = NULL;
    int objc = 0;
    double dpi;
    char *inputCopy = NULL;
    int parameterScaleSeen = 0;
    static const char *const fmtOptions[] = {
        "-dpi", "-scale", "-scaletoheight", "-scaletowidth", NULL
    };
    enum fmtOptions {
	OPT_DPI, OPT_SCALE, OPT_SCALE_TO_HEIGHT, OPT_SCALE_TO_WIDTH
    };

    /*
     * Process elements of format specification as a list.
     */

    optionsPtr->dpi = 96.0;
    optionsPtr->scale = 1.0;
    optionsPtr->scaleToHeight = 0;
    optionsPtr->scaleToWidth = 0;
    if ((formatObj != NULL) &&
	    Tcl_ListObjGetElements(interp, formatObj, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;;
    }
    for (; objc > 0 ; objc--, objv++) {
	int optIndex;

	/*
	 * Ignore the "svg" part of the format specification.
	 */

	if (!strcasecmp(Tcl_GetString(objv[0]), "svg")) {
	    continue;
	}

	if (Tcl_GetIndexFromObjStruct(interp, objv[0], fmtOptions,
		sizeof(char *), "option", 0, &optIndex) == TCL_ERROR) {
	    return TCL_ERROR;;
	}

	if (objc < 2) {
	    Tcl_WrongNumArgs(interp, 1, objv, "value");
	    return TCL_ERROR;;
	}

	objc--;
	objv++;

	/*
	 * check that only one scale option is given
	 */
	switch ((enum fmtOptions) optIndex) {
	case OPT_SCALE:
	case OPT_SCALE_TO_HEIGHT:
	case OPT_SCALE_TO_WIDTH:
	    if ( parameterScaleSeen ) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"only one of -scale, -scaletoheight, -scaletowidth may be given", -1));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "BAD_SCALE",
			NULL);
		return TCL_ERROR;;
	    }
	    parameterScaleSeen = 1;
	    break;
	default:
	    break;
	}

	/*
	 * Decode parameters
	 */
	switch ((enum fmtOptions) optIndex) {
	case OPT_DPI:
	    if (Tcl_GetDoubleFromObj(interp, objv[0], &dpi) == TCL_ERROR) {
	        return TCL_ERROR;;
	    }
	    if (dpi < 0.0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"-dpi value must be positive", -1));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "BAD_DPI",
			NULL);
		return TCL_ERROR;;
	    }
	    optionsPtr->dpi = (float)dpi;
	    break;
	case OPT_SCALE:
	    if (Tcl_GetDoubleFromObj(interp, objv[0],
		    &(optionsPtr->scale)) ==
		TCL_ERROR) {
	        return TCL_ERROR;;
	    }
	    if (optionsPtr->scale <= 0.0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"-scale value must be positive", -1));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "BAD_SCALE",
			NULL);
		return TCL_ERROR;;
	    }
	    break;
	case OPT_SCALE_TO_HEIGHT:
	    if (Tcl_GetIntFromObj(interp, objv[0],
		&(optionsPtr->scaleToHeight)) == TCL_ERROR) {
	        return TCL_ERROR;;
	    }
	    if (optionsPtr->scaleToHeight <= 0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"-scaletoheight value must be positive", -1));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "BAD_SCALE",
			NULL);
		return TCL_ERROR;;
	    }
	    break;
	case OPT_SCALE_TO_WIDTH:
	    if (Tcl_GetIntFromObj(interp, objv[0],
		    &(optionsPtr->scaleToWidth)) == TCL_ERROR) {
	        return TCL_ERROR;;
	    }
	    if (optionsPtr->scaleToWidth <= 0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"-scaletowidth value must be positive", -1));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "BAD_SCALE",
			NULL);
		return TCL_ERROR;;
	    }
	    break;
	}
    }


    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ParseSVG --
 *
 *	This function is called to parse the given input string as SVG.
 *
 * Results:
 *	Return a newly create NSVGimage on success, and NULL otherwise.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static NSVGimage *
ParseSVG(
    Tcl_Interp *interp,
    Tcl_Obj *dataObj,
    float dpi)
{
    const char *input;
    char *inputCopy = NULL;
    NSVGimage *nsvgImage;
    TkSizeT length;

    /*
     * The parser destroys the original input string,
     * therefore first duplicate.
     */

    input = TkGetStringFromObj(dataObj, &length);
    inputCopy = (char *)attemptckalloc(length+1);
    if (inputCopy == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("cannot alloc data buffer", -1));
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "OUT_OF_MEMORY", NULL);
	return NULL;
    }
    memcpy(inputCopy, input, length);
    inputCopy[length] = '\0';

    nsvgImage = nsvgParse(inputCopy, "px", dpi);
    if (nsvgImage == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("cannot parse SVG image", -1));
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "PARSE_ERROR", NULL);
        ckfree(inputCopy);
	return NULL;
    }
    ckfree(inputCopy);
    return nsvgImage;
}

/*
 *----------------------------------------------------------------------
 *
 * GetScaleFromParameters --
 *
 *	Get the scale value from the already parsed parameters -scale,
 *	-scaletoheight and -scaletowidth.
 *
 *	The image width and height is also returned.
 *
 * Results:
 *	The evaluated or configured scale value, or 0.0 on failure
 *
 * Side effects:
 *	heightPtr and widthPtr are set to height and width of the image.
 *
 *----------------------------------------------------------------------
 */

static double
GetScaleFromParameters(
    serializedHeader *serializedHeaderPtr,
    optionsStruct * optionsPtr,
    int *widthPtr,
    int *heightPtr)
{
    double scale;
    int width, height;

    if ((serializedHeaderPtr->width == 0.0) || (serializedHeaderPtr->height == 0.0)) {
        width = height = 0;
        scale = 1.0;
    } else if (optionsPtr->scaleToHeight > 0) {
	/*
	 * Fixed height
	 */
	height = optionsPtr->scaleToHeight;
	scale = height / serializedHeaderPtr->height;
	width = (int) ceil(serializedHeaderPtr->width * scale);
    } else if (optionsPtr->scaleToWidth > 0) {
	/*
	 * Fixed width
	 */
	width = optionsPtr->scaleToWidth;
	scale = width / serializedHeaderPtr->width;
	height = (int) ceil(serializedHeaderPtr->height * scale);
    } else {
	/*
	 * Scale factor
	 */
	scale = optionsPtr->scale;
	width = (int) ceil(serializedHeaderPtr->width * scale);
	height = (int) ceil(serializedHeaderPtr->height * scale);
    }

    *heightPtr = height;
    *widthPtr = width;
    return scale;
}

/*
 *----------------------------------------------------------------------
 *
 * RasterizeSVG --
 *
 *	This function is called to rasterize the given nsvgImage and
 *	fill the imageHandle with data.
 *
 * Results:
 *	A standard TCL completion code. If TCL_ERROR is returned then an error
 *	message is left in the interp's result.
 *
 *
 * Side effects:
 *	On error the given nsvgImage will be deleted.
 *
 *----------------------------------------------------------------------
 */

static int
RasterizeSVG(
    Tcl_Interp *interp,
    Tk_PhotoHandle imageHandle,
    char *svgBlobPtr,
    optionsStruct * optionsPtr,
    int destX, int destY,
    int width, int height,
    int srcX, int srcY)
{
    int w, h, c;
    NSVGrasterizer *rast;
    unsigned char *imgData;
    Tk_PhotoImageBlock svgblock;
    double scale;
    (void)srcX;
    (void)srcY;
    
    scale = GetScaleFromParameters((serializedHeader *) svgBlobPtr, optionsPtr,
	    &w, &h);

    rast = nsvgCreateRasterizer();
    if (rast == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("cannot initialize rasterizer", -1));
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "RASTERIZER_ERROR",
		NULL);
	return TCL_ERROR;
    }
    imgData = (unsigned char *)attemptckalloc(w * h *4);
    if (imgData == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("cannot alloc image buffer", -1));
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "OUT_OF_MEMORY", NULL);
	goto cleanRAST;
    }

    nsvgRasterizeSerialized(rast, svgBlobPtr, 0, 0,
	    (float) scale, imgData, w, h, w * 4);
    /* transfer the data to a photo block */
    svgblock.pixelPtr = imgData;
    svgblock.width = w;
    svgblock.height = h;
    svgblock.pitch = w * 4;
    svgblock.pixelSize = 4;
    for (c = 0; c <= 3; c++) {
	svgblock.offset[c] = c;
    }
    if (Tk_PhotoExpand(interp, imageHandle,
		destX + width, destY + height) != TCL_OK) {
	goto cleanRAST;
    }
    if (Tk_PhotoPutBlock(interp, imageHandle, &svgblock, destX, destY,
		width, height, TK_PHOTO_COMPOSITE_SET) != TCL_OK) {
	goto cleanimg;
    }
    ckfree(imgData);
    nsvgDeleteRasterizer(rast);
    return TCL_OK;

cleanimg:
    ckfree(imgData);

cleanRAST:
    nsvgDeleteRasterizer(rast);

    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Rasterize Serialized --
 *
 *	Fiunctions of svgnrast.h which requires modification due to the
 *	serialized data structure.
 *
 * Results:
 *
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static void nsvg__flattenShapeSerialized(NSVGrasterizer* r, int pathIndex,
	NSVGpathSerialized *pathSerializedPtr, float *ptsSerializedPtr,
	float scale)
{
    int i, j;
    NSVGpathSerialized* path;

    for (; pathIndex != -1; pathIndex = pathSerializedPtr[pathIndex].next) {
	path = &(pathSerializedPtr[pathIndex]);
	r->npoints = 0;
	// Flatten path
	nsvg__addPathPoint(r,
		ptsSerializedPtr[path->pts]*scale,
		ptsSerializedPtr[path->pts+1]*scale, 0);
	for (i = 0; i < path->npts-1; i += 3) {
	    float* p = &ptsSerializedPtr[path->pts+i*2];
	    nsvg__flattenCubicBez(r, p[0]*scale,p[1]*scale, p[2]*scale,p[3]*scale, p[4]*scale,p[5]*scale, p[6]*scale,p[7]*scale, 0, 0);
	}
	// Close path
	nsvg__addPathPoint(r, ptsSerializedPtr[path->pts]*scale,
		ptsSerializedPtr[path->pts+1]*scale, 0);
	// Build edges
	for (i = 0, j = r->npoints-1; i < r->npoints; j = i++)
	    nsvg__addEdge(r, r->points[j].x, r->points[j].y, r->points[i].x, r->points[i].y);
    }
}

static void nsvg__initPaintSerialized(NSVGcachedPaint* cache,
	NSVGpaintSerialized* paint, float opacity,
	NSVGgradientSerialized *gradientSerializedPtr,
	NSVGgradientStop *gradientStopPtr)
{
    int i, j;
    NSVGgradientSerialized* grad;

    cache->type = paint->type;

    if (paint->type == NSVG_PAINT_COLOR) {
	cache->colors[0] = nsvg__applyOpacity(paint->color, opacity);
	return;
    }

    grad = &(gradientSerializedPtr[paint->gradient]);

    cache->spread = grad->spread;
    memcpy(cache->xform, grad->xform, sizeof(float)*6);

    if (grad->nstops == 0) {
	for (i = 0; i < 256; i++)
	    cache->colors[i] = 0;
    } if (grad->nstops == 1) {
	for (i = 0; i < 256; i++)
	    cache->colors[i] = nsvg__applyOpacity(
		    gradientStopPtr[grad->stops+i].color, opacity);
    } else {
	unsigned int ca, cb = 0;
	float ua, ub, du, u;
	int ia, ib, count;

	ca = nsvg__applyOpacity(gradientStopPtr[grad->stops].color, opacity);
	ua = nsvg__clampf(gradientStopPtr[grad->stops].offset, 0, 1);
	ub = nsvg__clampf(gradientStopPtr[grad->stops+grad->nstops-1].offset,
		ua, 1);
	ia = (int)(ua * 255.0f);
	ib = (int)(ub * 255.0f);
	for (i = 0; i < ia; i++) {
	    cache->colors[i] = ca;
	}

	for (i = 0; i < grad->nstops-1; i++) {
	    ca = nsvg__applyOpacity(gradientStopPtr[grad->stops+i].color,
		    opacity);
	    cb = nsvg__applyOpacity(gradientStopPtr[grad->stops+i+1].color,
		    opacity);
	    ua = nsvg__clampf(gradientStopPtr[grad->stops+i].offset, 0, 1);
	    ub = nsvg__clampf(gradientStopPtr[grad->stops+i+1].offset, 0, 1);
	    ia = (int)(ua * 255.0f);
	    ib = (int)(ub * 255.0f);
	    count = ib - ia;
	    if (count <= 0) continue;
	    u = 0;
	    du = 1.0f / (float)count;
	    for (j = 0; j < count; j++) {
		cache->colors[ia+j] = nsvg__lerpRGBA(ca,cb,u);
		u += du;
	    }
	}

	for (i = ib; i < 256; i++)
	    cache->colors[i] = cb;
    }

}

static void nsvg__flattenShapeStrokeSerialized(NSVGrasterizer* r,
	NSVGshapeSerialized* shape, NSVGpathSerialized *pathSerializedPtr,
	float *ptsSerializedPtr, float scale)
{
    int i, j, closed, pathIndex;
    NSVGpathSerialized* path;
    NSVGpoint* p0, *p1;
    float miterLimit = shape->miterLimit;
    int lineJoin = shape->strokeLineJoin;
    int lineCap = shape->strokeLineCap;
    float lineWidth = shape->strokeWidth * scale;

    for (pathIndex = shape->paths; pathIndex != -1;
	    pathIndex = pathSerializedPtr[pathIndex].next) {
	path = &(pathSerializedPtr[pathIndex]);
	// Flatten path
	r->npoints = 0;
	nsvg__addPathPoint(r, ptsSerializedPtr[path->pts]*scale,
		ptsSerializedPtr[path->pts+1]*scale, NSVG_PT_CORNER);
	for (i = 0; i < path->npts-1; i += 3) {
	    float* p = &ptsSerializedPtr[path->pts+i*2];
	    nsvg__flattenCubicBez(r, p[0]*scale,p[1]*scale, p[2]*scale,
		    p[3]*scale, p[4]*scale,p[5]*scale, p[6]*scale,p[7]*scale, 0,
		    NSVG_PT_CORNER);
	}
	if (r->npoints < 2)
	    continue;

	closed = path->closed;

	// If the first and last points are the same, remove the last, mark as closed path.
	p0 = &r->points[r->npoints-1];
	p1 = &r->points[0];
	if (nsvg__ptEquals(p0->x,p0->y, p1->x,p1->y, r->distTol)) {
	    r->npoints--;
	    p0 = &r->points[r->npoints-1];
	    closed = 1;
	}

	if (shape->strokeDashCount > 0) {
	    int idash = 0, dashState = 1;
	    float totalDist = 0, dashLen, allDashLen, dashOffset;
	    NSVGpoint cur;

	    if (closed)
		nsvg__appendPathPoint(r, r->points[0]);

	    // Duplicate points -> points2.
	    nsvg__duplicatePoints(r);
	    r->npoints = 0;
	    cur = r->points2[0];
	    nsvg__appendPathPoint(r, cur);

	    // Figure out dash offset.
	    allDashLen = 0;
	    for (j = 0; j < shape->strokeDashCount; j++)
		allDashLen += shape->strokeDashArray[j];
	    if (shape->strokeDashCount & 1)
		allDashLen *= 2.0f;
	    // Find location inside pattern
	    dashOffset = fmodf(shape->strokeDashOffset, allDashLen);
	    if (dashOffset < 0.0f)
		dashOffset += allDashLen;

	    while (dashOffset > shape->strokeDashArray[idash]) {
		dashOffset -= shape->strokeDashArray[idash];
		idash = (idash + 1) % shape->strokeDashCount;
	    }
	    dashLen = (shape->strokeDashArray[idash] - dashOffset) * scale;

	    for (j = 1; j < r->npoints2; ) {
		float dx = r->points2[j].x - cur.x;
		float dy = r->points2[j].y - cur.y;
		float dist = sqrtf(dx*dx + dy*dy);

		if ((totalDist + dist) > dashLen) {
		    // Calculate intermediate point
		    float d = (dashLen - totalDist) / dist;
		    float x = cur.x + dx * d;
		    float y = cur.y + dy * d;
		    nsvg__addPathPoint(r, x, y, NSVG_PT_CORNER);

		    // Stroke
		    if (r->npoints > 1 && dashState) {
			    nsvg__prepareStroke(r, miterLimit, lineJoin);
			    nsvg__expandStroke(r, r->points, r->npoints, 0,
				    lineJoin, lineCap, lineWidth);
		    }
		    // Advance dash pattern
		    dashState = !dashState;
		    idash = (idash+1) % shape->strokeDashCount;
		    dashLen = shape->strokeDashArray[idash] * scale;
		    // Restart
		    cur.x = x;
		    cur.y = y;
		    cur.flags = NSVG_PT_CORNER;
		    totalDist = 0.0f;
		    r->npoints = 0;
		    nsvg__appendPathPoint(r, cur);
		} else {
		    totalDist += dist;
		    cur = r->points2[j];
		    nsvg__appendPathPoint(r, cur);
		    j++;
		}
	    }
	    // Stroke any leftover path
	    if (r->npoints > 1 && dashState)
		nsvg__expandStroke(r, r->points, r->npoints, 0, lineJoin,
			lineCap, lineWidth);
	} else {
	    nsvg__prepareStroke(r, miterLimit, lineJoin);
	    nsvg__expandStroke(r, r->points, r->npoints, closed, lineJoin,
		    lineCap, lineWidth);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * RasterizeSVGSerialized --
 *
 *	This function is called to rasterize the given nsvgImage and
 *	fill the imageHandle with data.
 *
 * Results:
 *	A standard TCL completion code. If TCL_ERROR is returned then an error
 *	message is left in the interp's result.
 *
 *
 * Side effects:
 *	On error the given nsvgImage will be deleted.
 *
 *----------------------------------------------------------------------
 */

static void nsvgRasterizeSerialized(NSVGrasterizer* r,
	char *svgBlobPtr, float tx, float ty, float scale,
	unsigned char* dst, int w, int h, int stride)
{
    NSVGshapeSerialized *shape = NULL;
    NSVGedge *e = NULL;
    NSVGcachedPaint cache;
    int i, shapeIndex;
    serializedHeader * serializedHeaderPtr;
    NSVGshapeSerialized *shapeSerializedPtr;
    NSVGpathSerialized *pathSerializedPtr;
    float *ptsSerializedPtr;
    NSVGgradientSerialized *gradientSerializedPtr;
    NSVGgradientStop *gradientStopPtr;

    /*
     * Prepare the array pointers of the data array placed after serializedHeader
     */
    serializedHeaderPtr = (serializedHeader *) svgBlobPtr;
    svgBlobPtr += sizeof (serializedHeader);
    shapeSerializedPtr = (NSVGshapeSerialized *) svgBlobPtr;
    svgBlobPtr += serializedHeaderPtr->shapeCount * sizeof(NSVGshapeSerialized);
    pathSerializedPtr = (NSVGpathSerialized *) svgBlobPtr;
    svgBlobPtr += serializedHeaderPtr->pathCount * sizeof(NSVGpathSerialized);
    ptsSerializedPtr = (float *) svgBlobPtr;
    svgBlobPtr += serializedHeaderPtr->ptsCount * sizeof(float);
    gradientSerializedPtr = (NSVGgradientSerialized *) svgBlobPtr;
    svgBlobPtr += serializedHeaderPtr->gradientCount *
	    sizeof(NSVGgradientSerialized);
    gradientStopPtr = (NSVGgradientStop *) svgBlobPtr;

    r->bitmap = dst;
    r->width = w;
    r->height = h;
    r->stride = stride;

    if (w > r->cscanline) {
	r->cscanline = w;
	r->scanline = (unsigned char*)NANOSVG_realloc(r->scanline, w);
	if (r->scanline == NULL) return;
    }

    for (i = 0; i < h; i++)
	memset(&dst[i*stride], 0, w*4);

    for (shapeIndex = 0 ; shapeIndex < serializedHeaderPtr->shapeCount;
	    shapeIndex++) {
	shape = &(shapeSerializedPtr[shapeIndex]);
	if (!(shape->flags & NSVG_FLAGS_VISIBLE))
	    continue;

	if (shape->fill.type != NSVG_PAINT_NONE) {
	    nsvg__resetPool(r);
	    r->freelist = NULL;
	    r->nedges = 0;

	    nsvg__flattenShapeSerialized(r, shape->paths, pathSerializedPtr, ptsSerializedPtr,
		    scale);

	    // Scale and translate edges
	    for (i = 0; i < r->nedges; i++) {
		e = &r->edges[i];
		e->x0 = tx + e->x0;
		e->y0 = (ty + e->y0) * NSVG__SUBSAMPLES;
		e->x1 = tx + e->x1;
		e->y1 = (ty + e->y1) * NSVG__SUBSAMPLES;
	    }

	    // Rasterize edges
	    qsort(r->edges, r->nedges, sizeof(NSVGedge), nsvg__cmpEdge);

	    // now, traverse the scanlines and find the intersections on each scanline, use non-zero rule
	    nsvg__initPaintSerialized(&cache, &shape->fill, shape->opacity,
		    gradientSerializedPtr, gradientStopPtr);

	    nsvg__rasterizeSortedEdges(r, tx,ty,scale, &cache, shape->fillRule);
	}
	if (shape->stroke.type != NSVG_PAINT_NONE && (shape->strokeWidth * scale) > 0.01f) {
	    nsvg__resetPool(r);
	    r->freelist = NULL;
	    r->nedges = 0;

	    nsvg__flattenShapeStrokeSerialized(r, shape, pathSerializedPtr,
		    ptsSerializedPtr, scale);

	    //			dumpEdges(r, "edge.svg");

	    // Scale and translate edges
	    for (i = 0; i < r->nedges; i++) {
		e = &r->edges[i];
		e->x0 = tx + e->x0;
		e->y0 = (ty + e->y0) * NSVG__SUBSAMPLES;
		e->x1 = tx + e->x1;
		e->y1 = (ty + e->y1) * NSVG__SUBSAMPLES;
	    }

	    // Rasterize edges
	    qsort(r->edges, r->nedges, sizeof(NSVGedge), nsvg__cmpEdge);

	    // now, traverse the scanlines and find the intersections on each scanline, use non-zero rule
	    nsvg__initPaintSerialized(&cache, &shape->stroke, shape->opacity,
		    gradientSerializedPtr, gradientStopPtr);

	    nsvg__rasterizeSortedEdges(r, tx,ty,scale, &cache, NSVG_FILLRULE_NONZERO);
	}
    }

    nsvg__unpremultiplyAlpha(dst, w, h, stride);

    r->bitmap = NULL;
    r->width = 0;
    r->height = 0;
    r->stride = 0;
}

