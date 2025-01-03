/*
 * tkImgSVGnano.c
 *
 *	A photo file handler for SVG files.
 *
 * Copyright © 2013-14 Mikko Mononen memon@inside.org
 * Copyright © 2018 Christian Gollwitzer auriocus@gmx.de
 * Copyright © 2018 Christian Werner https://www.androwish.org/
 * Copyright © 2018 Rene Zaumseil r.zaumseil@freenet.de
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * This handler is build using the original nanosvg library files from
 * https://github.com/memononen/nanosvg
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

/* Additional parameters to nsvgRasterize() */

typedef struct {
    double scale;
    int scaleToHeight;
    int scaleToWidth;
} RastOpts;

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
    void *dataOrChan;
    Tcl_DString formatString;
    NSVGimage *nsvgImage;
    RastOpts ropts;
} NSVGcache;

static const void *	MemMem(const void *haystack, size_t haysize,
			       const void *needle, size_t needlen);
static int		FileMatchSVG(Tcl_Channel chan, const char *fileName,
			    Tcl_Obj *format, int *widthPtr, int *heightPtr,
			    Tcl_Interp *interp);
static int		FileReadSVG(Tcl_Interp *interp, Tcl_Channel chan,
			    const char *fileName, Tcl_Obj *format,
			    Tk_PhotoHandle imageHandle, int destX, int destY,
			    int width, int height, int srcX, int srcY);
static int		StringMatchSVG(Tcl_Obj *dataObj, Tcl_Obj *format,
			    int *widthPtr, int *heightPtr, Tcl_Interp *interp);
static int		StringReadSVG(Tcl_Interp *interp, Tcl_Obj *dataObj,
			    Tcl_Obj *format, Tk_PhotoHandle imageHandle,
			    int destX, int destY, int width, int height,
			    int srcX, int srcY);
static NSVGimage *	ParseSVGWithOptions(Tcl_Interp *interp,
			    const char *input, Tcl_Size length, Tcl_Obj *format,
			    RastOpts *ropts);
static int		RasterizeSVG(Tcl_Interp *interp,
			    Tk_PhotoHandle imageHandle, NSVGimage *nsvgImage,
			    int destX, int destY, int width, int height,
			    int srcX, int srcY, RastOpts *ropts);
static double		GetScaleFromParameters(NSVGimage *nsvgImage,
			    RastOpts *ropts, int *widthPtr, int *heightPtr);
static NSVGcache *	GetCachePtr(Tcl_Interp *interp);
static int		CacheSVG(Tcl_Interp *interp, void *dataOrChan,
			    Tcl_Obj *formatObj, NSVGimage *nsvgImage,
			    RastOpts *ropts);
static NSVGimage *	GetCachedSVG(Tcl_Interp *interp, void *dataOrChan,
			    Tcl_Obj *formatObj, RastOpts *ropts);
static void		CleanCache(Tcl_Interp *interp);
static void		FreeCache(void *clientData, Tcl_Interp *interp);

/*
 * The format record for the SVG nano file format:
 */

Tk_PhotoImageFormat tkImgFmtSVGnano = {
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
 * MemMem --
 *
 *	Like strstr() but operating on memory buffers with sizes.
 *
 *----------------------------------------------------------------------
 */

static const void *
MemMem(const void *haystack, size_t haylen,
       const void *needle, size_t needlen)
{
    const void *hayend, *second, *p;
    unsigned char first;

    if ((needlen <= 0) || (haylen < needlen)) {
	return NULL;
    }
    hayend = (const void *) ((char *) haystack + haylen - needlen);
    first = ((char *) needle)[0];
    second = (const void *) ((char *) needle + 1);
    needlen -= 1;
    while (haystack < hayend) {
	p = memchr(haystack, first, (char *) hayend - (char *) haystack);
	if (p == NULL) {
	    break;
	}
	if (needlen == 0) {
	    return p;
	}
	haystack = (const void *) ((char *) p + 1);
	if (memcmp(second, haystack, needlen) == 0) {
	    return p;
	}
    }
    return NULL;
}

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
    Tcl_Channel chan,
    TCL_UNUSED(const char *),
    Tcl_Obj *formatObj,
    int *widthPtr, int *heightPtr,
    Tcl_Interp *interp)
{
    Tcl_Size length;
    Tcl_Obj *dataObj = Tcl_NewObj();
    const char *data;
    RastOpts ropts;
    NSVGimage *nsvgImage;

    CleanCache(interp);
    if (Tcl_ReadChars(chan, dataObj, 4096, 0) == TCL_IO_FAILURE) {
	/* in case of an error reading the file */
	Tcl_DecrRefCount(dataObj);
	return 0;
    }
    data = Tcl_GetStringFromObj(dataObj, &length);
    /* should have a '<svg' and a '>' in the first 4k */
    if ((memchr(data, '>', length) == NULL) ||
	(MemMem(data, length, "<svg", 4) == NULL)) {
	Tcl_DecrRefCount(dataObj);
	return 0;
    }
    if (!Tcl_Eof(chan) && (Tcl_ReadChars(chan, dataObj, TCL_INDEX_NONE, 1) == TCL_IO_FAILURE)) {
	/* in case of an error reading the file */
	Tcl_DecrRefCount(dataObj);
	return 0;
    }
    data = Tcl_GetStringFromObj(dataObj, &length);
    nsvgImage = ParseSVGWithOptions(interp, data, length, formatObj, &ropts);
    Tcl_DecrRefCount(dataObj);
    if (nsvgImage != NULL) {
	GetScaleFromParameters(nsvgImage, &ropts, widthPtr, heightPtr);
	if ((*widthPtr <= 0.0) || (*heightPtr <= 0.0)) {
	    nsvgDelete(nsvgImage);
	    return 0;
	}
	if (!CacheSVG(interp, chan, formatObj, nsvgImage, &ropts)) {
	    nsvgDelete(nsvgImage);
	}
	return 1;
    }
    return 0;
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
    Tcl_Interp *interp,
    Tcl_Channel chan,
    TCL_UNUSED(const char *),
    Tcl_Obj *formatObj,
    Tk_PhotoHandle imageHandle,
    int destX, int destY,
    int width, int height,
    int srcX, int srcY)
{
    Tcl_Size length;
    const char *data;
    RastOpts ropts;
    NSVGimage *nsvgImage = GetCachedSVG(interp, chan, formatObj, &ropts);

    if (nsvgImage == NULL) {
	Tcl_Obj *dataObj = Tcl_NewObj();

	if (Tcl_ReadChars(chan, dataObj, TCL_INDEX_NONE, 0) == TCL_IO_FAILURE) {
	    /* in case of an error reading the file */
	    Tcl_DecrRefCount(dataObj);
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("read error", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "READ_ERROR", NULL);
	    return TCL_ERROR;
	}
	data = Tcl_GetStringFromObj(dataObj, &length);
	nsvgImage = ParseSVGWithOptions(interp, data, length, formatObj,
			    &ropts);
	Tcl_DecrRefCount(dataObj);
	if (nsvgImage == NULL) {
	    return TCL_ERROR;
	}
    }
    return RasterizeSVG(interp, imageHandle, nsvgImage, destX, destY,
		width, height, srcX, srcY, &ropts);
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
    Tcl_Obj *dataObj,
    Tcl_Obj *formatObj,
    int *widthPtr, int *heightPtr,
    Tcl_Interp *interp)
{
    Tcl_Size length, testLength;
    const char *data;
    RastOpts ropts;
    NSVGimage *nsvgImage;

    CleanCache(interp);
    data = Tcl_GetStringFromObj(dataObj, &length);
    /* should have a '<svg' and a '>' in the first 4k */
    testLength = (length > 4096) ? 4096 : length;
    if ((memchr(data, '>', testLength) == NULL) ||
	(MemMem(data, testLength, "<svg", 4) == NULL)) {
	return 0;
    }
    nsvgImage = ParseSVGWithOptions(interp, data, length, formatObj, &ropts);
    if (nsvgImage != NULL) {
	GetScaleFromParameters(nsvgImage, &ropts, widthPtr, heightPtr);
	if ((*widthPtr <= 0.0) || (*heightPtr <= 0.0)) {
	    nsvgDelete(nsvgImage);
	    return 0;
	}
	if (!CacheSVG(interp, dataObj, formatObj, nsvgImage, &ropts)) {
	    nsvgDelete(nsvgImage);
	}
	return 1;
    }
    return 0;
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
    Tcl_Interp *interp,
    Tcl_Obj *dataObj,
    Tcl_Obj *formatObj,
    Tk_PhotoHandle imageHandle,
    int destX, int destY,
    int width, int height,
    int srcX, int srcY)
{
    Tcl_Size length;
    const char *data;
    RastOpts ropts;
    NSVGimage *nsvgImage = GetCachedSVG(interp, dataObj, formatObj, &ropts);

    if (nsvgImage == NULL) {
	data = Tcl_GetStringFromObj(dataObj, &length);
	nsvgImage = ParseSVGWithOptions(interp, data, length, formatObj,
			    &ropts);
    }
    if (nsvgImage == NULL) {
	return TCL_ERROR;
    }
    return RasterizeSVG(interp, imageHandle, nsvgImage, destX, destY,
		width, height, srcX, srcY, &ropts);
}

/*
 *----------------------------------------------------------------------
 *
 * ParseSVGWithOptions --
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
ParseSVGWithOptions(
    Tcl_Interp *interp,
    const char *input,
    Tcl_Size length,
    Tcl_Obj *formatObj,
    RastOpts *ropts)
{
    Tcl_Obj **objv = NULL;
    Tcl_Size objc = 0;
    double dpi = 96.0;
    char *inputCopy = NULL;
    NSVGimage *nsvgImage;
    int parameterScaleSeen = 0;
    static const char *const fmtOptions[] = {
	"-dpi", "-scale", "-scaletoheight", "-scaletowidth", NULL
    };
    enum fmtOptionsEnum {
	OPT_DPI, OPT_SCALE, OPT_SCALE_TO_HEIGHT, OPT_SCALE_TO_WIDTH
    };

    /*
     * The parser destroys the original input string,
     * therefore first duplicate.
     */

    inputCopy = (char *)attemptckalloc(length+1);
    if (inputCopy == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("cannot alloc data buffer", TCL_INDEX_NONE));
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "OUT_OF_MEMORY", NULL);
	goto error;
    }
    memcpy(inputCopy, input, length);
    inputCopy[length] = '\0';

    /*
     * Process elements of format specification as a list.
     */

    ropts->scale = 1.0;
    ropts->scaleToHeight = 0;
    ropts->scaleToWidth = 0;
    if ((formatObj != NULL) &&
	    Tcl_ListObjGetElements(interp, formatObj, &objc, &objv) != TCL_OK) {
	goto error;
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
	    goto error;
	}

	if (objc < 2) {
	    ckfree(inputCopy);
	    inputCopy = NULL;
	    Tcl_WrongNumArgs(interp, 1, objv, "value");
	    goto error;
	}

	objc--;
	objv++;

	/*
	 * check that only one scale option is given
	 */
	switch ((enum fmtOptionsEnum)optIndex) {
	case OPT_SCALE:
	case OPT_SCALE_TO_HEIGHT:
	case OPT_SCALE_TO_WIDTH:
	    if ( parameterScaleSeen ) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"only one of -scale, -scaletoheight, -scaletowidth may be given", TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "BAD_SCALE",
			NULL);
		goto error;
	    }
	    parameterScaleSeen = 1;
	    break;
	default:
	    break;
	}

	/*
	 * Decode parameters
	 */
	switch ((enum fmtOptionsEnum) optIndex) {
	case OPT_DPI:
	    if (Tcl_GetDoubleFromObj(interp, objv[0], &dpi) == TCL_ERROR) {
		goto error;
	    }
	    if (dpi < 0.0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"-dpi value must be positive", TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "BAD_DPI",
			NULL);
		goto error;
	    }
	    break;
	case OPT_SCALE:
	    if (Tcl_GetDoubleFromObj(interp, objv[0], &ropts->scale) ==
		TCL_ERROR) {
		goto error;
	    }
	    if (ropts->scale <= 0.0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"-scale value must be positive", TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "BAD_SCALE",
			NULL);
		goto error;
	    }
	    break;
	case OPT_SCALE_TO_HEIGHT:
	    if (Tcl_GetIntFromObj(interp, objv[0], &ropts->scaleToHeight) ==
		TCL_ERROR) {
		goto error;
	    }
	    if (ropts->scaleToHeight <= 0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"-scaletoheight value must be positive", TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "BAD_SCALE",
			NULL);
		goto error;
	    }
	    break;
	case OPT_SCALE_TO_WIDTH:
	    if (Tcl_GetIntFromObj(interp, objv[0], &ropts->scaleToWidth) ==
		TCL_ERROR) {
		goto error;
	    }
	    if (ropts->scaleToWidth <= 0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"-scaletowidth value must be positive", TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "BAD_SCALE",
			NULL);
		goto error;
	    }
	    break;
	}
    }

    nsvgImage = nsvgParse(inputCopy, "px", (float) dpi);
    if (nsvgImage == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("cannot parse SVG image", TCL_INDEX_NONE));
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "PARSE_ERROR", NULL);
	goto error;
    }
    ckfree(inputCopy);
    return nsvgImage;

error:
    if (inputCopy != NULL) {
	ckfree(inputCopy);
    }
    return NULL;
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
    NSVGimage *nsvgImage,
    int destX, int destY,
    int width, int height,
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    RastOpts *ropts)
{
    int w, h, c;
    NSVGrasterizer *rast;
    unsigned char *imgData;
    Tk_PhotoImageBlock svgblock;
    double scale;
    Tcl_WideUInt wh;

    scale = GetScaleFromParameters(nsvgImage, ropts, &w, &h);

    rast = nsvgCreateRasterizer();
    if (rast == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("cannot initialize rasterizer", TCL_INDEX_NONE));
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "RASTERIZER_ERROR",
		NULL);
	goto cleanAST;
    }

    /* Tk Ticket [822330269b] Check potential int overflow in following ckalloc */
    wh = (Tcl_WideUInt)w * (Tcl_WideUInt)h;
    if ( w < 0 || h < 0 || wh > INT_MAX / 4) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("image size overflow", TCL_INDEX_NONE));
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "IMAGE_SIZE_OVERFLOW", NULL);
	goto cleanRAST;
    }

    imgData = (unsigned char *)attemptckalloc(wh * 4);
    if (imgData == NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("cannot alloc image buffer", TCL_INDEX_NONE));
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "OUT_OF_MEMORY", NULL);
	goto cleanRAST;
    }
    nsvgRasterize(rast, nsvgImage, 0, 0,
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
    nsvgDelete(nsvgImage);
    return TCL_OK;

cleanimg:
    ckfree(imgData);

cleanRAST:
    nsvgDeleteRasterizer(rast);

cleanAST:
    nsvgDelete(nsvgImage);
    return TCL_ERROR;
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
    NSVGimage *nsvgImage,
    RastOpts *ropts,
    int *widthPtr,
    int *heightPtr)
{
    double scale;
    int width, height;

    if ((nsvgImage->width == 0.0) || (nsvgImage->height == 0.0)) {
	width = height = 0;
	scale = 1.0;
    } else if (ropts->scaleToHeight > 0) {
	/*
	 * Fixed height
	 */
	height = ropts->scaleToHeight;
	scale = height / nsvgImage->height;
	width = (int) ceil(nsvgImage->width * scale);
    } else if (ropts->scaleToWidth > 0) {
	/*
	 * Fixed width
	 */
	width = ropts->scaleToWidth;
	scale = width / nsvgImage->width;
	height = (int) ceil(nsvgImage->height * scale);
    } else {
	/*
	 * Scale factor
	 */
	scale = ropts->scale;
	width = (int) ceil(nsvgImage->width * scale);
	height = (int) ceil(nsvgImage->height * scale);
    }

    *heightPtr = height;
    *widthPtr = width;
    return scale;
}

/*
 *----------------------------------------------------------------------
 *
 * GetCachePtr --
 *
 *	This function is called to get the per interpreter used
 *	svg image cache.
 *
 * Results:
 *	Return a pointer to the used cache.
 *
 * Side effects:
 *	Initialize the cache on the first call.
 *
 *----------------------------------------------------------------------
 */

static NSVGcache *
GetCachePtr(
    Tcl_Interp *interp
) {
    NSVGcache *cachePtr = (NSVGcache *)Tcl_GetAssocData(interp, "tksvgnano", NULL);
    if (cachePtr == NULL) {
	cachePtr = (NSVGcache *)ckalloc(sizeof(NSVGcache));
	cachePtr->dataOrChan = NULL;
	Tcl_DStringInit(&cachePtr->formatString);
	cachePtr->nsvgImage = NULL;
	Tcl_SetAssocData(interp, "tksvgnano", FreeCache, cachePtr);
    }
    return cachePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * CacheSVG --
 *
 *	Add the given svg image informations to the cache for further usage.
 *
 * Results:
 *	Return 1 on success, and 0 otherwise.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static int
CacheSVG(
    Tcl_Interp *interp,
    void *dataOrChan,
    Tcl_Obj *formatObj,
    NSVGimage *nsvgImage,
    RastOpts *ropts)
{
    Tcl_Size length;
    const char *data;
    NSVGcache *cachePtr = GetCachePtr(interp);

    if (cachePtr != NULL) {
	cachePtr->dataOrChan = dataOrChan;
	if (formatObj != NULL) {
	    data = Tcl_GetStringFromObj(formatObj, &length);
	    Tcl_DStringAppend(&cachePtr->formatString, data, length);
	}
	cachePtr->nsvgImage = nsvgImage;
	cachePtr->ropts = *ropts;
	return 1;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * GetCachedSVG --
 *
 *	Try to get the NSVGimage from the internal cache.
 *
 * Results:
 *	Return the found NSVGimage on success, and NULL otherwise.
 *
 * Side effects:
 *	Calls the CleanCache() function.
 *
 *----------------------------------------------------------------------
 */

static NSVGimage *
GetCachedSVG(
    Tcl_Interp *interp,
    void *dataOrChan,
    Tcl_Obj *formatObj,
    RastOpts *ropts)
{
    Tcl_Size length;
    const char *data;
    NSVGcache *cachePtr = GetCachePtr(interp);
    NSVGimage *nsvgImage = NULL;

    if ((cachePtr != NULL) && (cachePtr->nsvgImage != NULL) &&
	(cachePtr->dataOrChan == dataOrChan)) {
	if (formatObj != NULL) {
	    data = Tcl_GetStringFromObj(formatObj, &length);
	    if (strcmp(data, Tcl_DStringValue(&cachePtr->formatString)) == 0) {
		nsvgImage = cachePtr->nsvgImage;
		*ropts = cachePtr->ropts;
		cachePtr->nsvgImage = NULL;
	    }
	} else if (Tcl_DStringLength(&cachePtr->formatString) == 0) {
	    nsvgImage = cachePtr->nsvgImage;
	    *ropts = cachePtr->ropts;
	    cachePtr->nsvgImage = NULL;
	}
    }
    CleanCache(interp);
    return nsvgImage;
}

/*
 *----------------------------------------------------------------------
 *
 * CleanCache --
 *
 *	Reset the cache and delete the saved image in it.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static void
CleanCache(Tcl_Interp *interp)
{
    NSVGcache *cachePtr = GetCachePtr(interp);

    if (cachePtr != NULL) {
	cachePtr->dataOrChan = NULL;
	Tcl_DStringSetLength(&cachePtr->formatString, 0);
	if (cachePtr->nsvgImage != NULL) {
	    nsvgDelete(cachePtr->nsvgImage);
	    cachePtr->nsvgImage = NULL;
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FreeCache --
 *
 *	This function is called to clean up the internal cache data.
 *
 * Results:
 *
 * Side effects:
 *	Existing image data in the cache and the cache will be deleted.
 *
 *----------------------------------------------------------------------
 */

static void
FreeCache(void *clientData, TCL_UNUSED(Tcl_Interp *))
{
    NSVGcache *cachePtr = (NSVGcache *)clientData;

    Tcl_DStringFree(&cachePtr->formatString);
    if (cachePtr->nsvgImage != NULL) {
	nsvgDelete(cachePtr->nsvgImage);
    }
    ckfree(cachePtr);
}

