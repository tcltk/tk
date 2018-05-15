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

/* Additional parameters to nsvgRasterize() */

typedef struct {
    double x;
    double y;
    double scale;
} RastOpts;

/*
 * The format record for the SVG nano file format:
 */

Tk_PhotoImageFormat tkImgFmtSVGnano = {
    "svgnano",			/* name */
    FileMatchSVG,		/* fileMatchProc */
    StringMatchSVG,		/* stringMatchProc */
    FileReadSVG,		/* fileReadProc */
    StringReadSVG,		/* stringReadProc */
    NULL,			/* fileWriteProc */
    NULL,			/* stringWriteProc */
    NULL
};

/*
 * Per interp cache of last NSVGimage which was matched to
 * be immediately rasterized after the match. This helps to
 * eliminate double parsing of the SVG file/string.
 */

typedef struct {
    ClientData dataOrChan;
    Tcl_DString formatString;
    NSVGimage *nsvgImage;
    RastOpts ropts;
} NSVGcache;

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
			    const char *input, int length, Tcl_Obj *format,
			    RastOpts *ropts);
static int		RasterizeSVG(Tcl_Interp *interp,
			    Tk_PhotoHandle imageHandle, NSVGimage *nsvgImage,
			    int destX, int destY, int width, int height,
			    int srcX, int srcY, RastOpts *ropts);
static NSVGcache *	GetCachePtr(Tcl_Interp *interp);
static int		CacheSVG(Tcl_Interp *interp, ClientData dataOrChan,
			    Tcl_Obj *formatObj, NSVGimage *nsvgImage,
			    RastOpts *ropts);
static NSVGimage *	GetCachedSVG(Tcl_Interp *interp, ClientData dataOrChan,
			    Tcl_Obj *formatObj, RastOpts *ropts);
static void		CleanCache(Tcl_Interp *interp);
static void		FreeCache(ClientData clientData, Tcl_Interp *interp);

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
    const char *fileName,
    Tcl_Obj *formatObj,
    int *widthPtr, int *heightPtr,
    Tcl_Interp *interp)
{
    int length;
    Tcl_Obj *dataObj = Tcl_NewObj();
    const char *data;
    RastOpts ropts;
    NSVGimage *nsvgImage;

    CleanCache(interp);
    if (Tcl_ReadChars(chan, dataObj, -1, 0) == -1) {
	/* in case of an error reading the file */
	Tcl_DecrRefCount(dataObj);
	return 0;
    }
    data = Tcl_GetStringFromObj(dataObj, &length);
    nsvgImage = ParseSVGWithOptions(interp, data, length, formatObj, &ropts);
    Tcl_DecrRefCount(dataObj);
    if (nsvgImage != NULL) {
	*widthPtr = ceil(nsvgImage->width * ropts.scale);
	*heightPtr = ceil(nsvgImage->height * ropts.scale);
        if ((*widthPtr <= 0) || (*heightPtr <= 0)) {
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
    const char *fileName,
    Tcl_Obj *formatObj,
    Tk_PhotoHandle imageHandle,
    int destX, int destY,
    int width, int height,
    int srcX, int srcY)
{
    int length;
    const char *data;
    RastOpts ropts;
    NSVGimage *nsvgImage = GetCachedSVG(interp, chan, formatObj, &ropts);

    if (nsvgImage == NULL) {
        Tcl_Obj *dataObj = Tcl_NewObj();

	if (Tcl_ReadChars(chan, dataObj, -1, 0) == -1) {
	    /* in case of an error reading the file */
	    Tcl_DecrRefCount(dataObj);
	    Tcl_SetResult(interp, "read error", TCL_STATIC);
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
    int length;
    const char *data;
    RastOpts ropts;
    NSVGimage *nsvgImage;

    CleanCache(interp);
    data = Tcl_GetStringFromObj(dataObj, &length);
    nsvgImage = ParseSVGWithOptions(interp, data, length, formatObj, &ropts);
    if (nsvgImage != NULL) {
	*widthPtr = ceil(nsvgImage->width * ropts.scale);
	*heightPtr = ceil(nsvgImage->height * ropts.scale);
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
    int length;
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
    int length,
    Tcl_Obj *formatObj,
    RastOpts *ropts)
{
    Tcl_Obj **objv = NULL;
    int objc = 0;
    double dpi = 96.0;
    char unit[3], *p;
    char *inputCopy = NULL;
    NSVGimage *nsvgImage;
    static const char *const fmtOptions[] = {
        "-dpi", "-scale", "-unit", "-x", "-y", NULL
    };
    enum fmtOptions {
	OPT_DPI, OPT_SCALE, OPT_UNIT, OPT_X, OPT_Y
    };

    /*
     * The parser destroys the original input string,
     * therefore first duplicate.
     */

    inputCopy = attemptckalloc(length+1);
    if (inputCopy == NULL) {
	Tcl_SetResult(interp, "cannot alloc data buffer", TCL_STATIC);
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "OUT_OF_MEMORY", NULL);
	goto error;
    }
    memcpy(inputCopy, input, length);
    inputCopy[length] = '\0';

    /*
     * Process elements of format specification as a list.
     */

    strcpy(unit, "px");
    ropts->x = ropts->y = 0.0;
    ropts->scale = 1.0;
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
	    Tcl_WrongNumArgs(interp, 1, objv, "value");
	    goto error;
	}

	objc--;
	objv++;

	switch ((enum fmtOptions) optIndex) {
	case OPT_DPI:
	    if (Tcl_GetDoubleFromObj(interp, objv[0], &dpi) == TCL_ERROR) {
	        goto error;
	    }
	    if (dpi < 0.0) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"-dpi value must be positive", -1));
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
			"-scale value must be positive", -1));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "BAD_SCALE",
			NULL);
		goto error;
	    }
	    break;
	case OPT_UNIT:
	    p = Tcl_GetString(objv[0]);
	    if ((p != NULL) && (p[0])) {
	        strncpy(unit, p, 3);
		unit[2] = '\0';
	    }
	    break;
	case OPT_X:
	    if (Tcl_GetDoubleFromObj(interp, objv[0], &ropts->x) == TCL_ERROR) {
	        goto error;
	    }
	    break;
	case OPT_Y:
	    if (Tcl_GetDoubleFromObj(interp, objv[0], &ropts->y) == TCL_ERROR) {
	        goto error;
	    }
	    break;
	}
    }

    nsvgImage = nsvgParse(inputCopy, unit, dpi);
    if (nsvgImage == NULL) {
        Tcl_SetResult(interp, "cannot parse SVG image", TCL_STATIC);
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
    int srcX, int srcY,
    RastOpts *ropts)
{
    int w, h, c;
    NSVGrasterizer *rast;
    unsigned char *imgData;
    Tk_PhotoImageBlock svgblock;

    w = ceil(nsvgImage->width * ropts->scale);
    h = ceil(nsvgImage->height * ropts->scale);
    rast = nsvgCreateRasterizer();
    if (rast == NULL) {
	Tcl_SetResult(interp, "cannot initialize rasterizer", TCL_STATIC);
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "RASTERIZER_ERROR",
		NULL);
	goto cleanAST;
    }
    imgData = attemptckalloc(w * h *4);
    if (imgData == NULL) {
	Tcl_SetResult(interp, "cannot alloc image buffer", TCL_STATIC);
	Tcl_SetErrorCode(interp, "TK", "IMAGE", "SVG", "OUT_OF_MEMORY", NULL);
	goto cleanRAST;
    }
    nsvgRasterize(rast, nsvgImage, ropts->x, ropts->y,
	    ropts->scale, imgData, w, h, w * 4);
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
 * GetCachePtr --
 *
 *	This function is called to get the per interpreter used
 *	svg image cache.
 *
 * Results:
 * 	Return a pointer to the used cache.
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
    NSVGcache *cachePtr = Tcl_GetAssocData(interp, "tksvgnano", NULL);
    if (cachePtr == NULL) {
	cachePtr = ckalloc(sizeof(NSVGcache));
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
    ClientData dataOrChan,
    Tcl_Obj *formatObj,
    NSVGimage *nsvgImage,
    RastOpts *ropts)
{
    int length;
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
    ClientData dataOrChan,
    Tcl_Obj *formatObj,
    RastOpts *ropts)
{
    int length;
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
FreeCache(ClientData clientData, Tcl_Interp *interp)
{
    NSVGcache *cachePtr = clientData;

    Tcl_DStringFree(&cachePtr->formatString);
    if (cachePtr->nsvgImage != NULL) {
        nsvgDelete(cachePtr->nsvgImage);
    }
    ckfree(cachePtr);
}

