/*
 * tkImgPhoto.c --
 *
 *	Implements images of type "photo" for Tk. Photo images are stored in
 *	full color (32 bits per pixel including alpha channel) and displayed
 *	using dithering if necessary.
 *
 * Copyright © 1994 The Australian National University.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 * Copyright © 2002-2003 Donal K. Fellows
 * Copyright © 2003 ActiveState Corporation.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * Author: Paul Mackerras (paulus@cs.anu.edu.au),
 *	   Department of Computer Science,
 *	   Australian National University.
 */

#include "tkImgPhoto.h"

/*
 * The following data structure is used to return information from
 * ParseSubcommandOptions:
 */

struct SubcommandOptions {
    int options;		/* Individual bits indicate which options were
				 * specified - see below. */
    Tcl_Obj *name;		/* Name specified without an option. */
    int fromX, fromY;		/* Values specified for -from option. */
    int fromX2, fromY2;		/* Second coordinate pair for -from option. */
    int toX, toY;		/* Values specified for -to option. */
    int toX2, toY2;		/* Second coordinate pair for -to option. */
    int zoomX, zoomY;		/* Values specified for -zoom option. */
    int subsampleX, subsampleY;	/* Values specified for -subsample option. */
    Tcl_Obj *format;		/* Value specified for -format option. */
    XColor *background;		/* Value specified for -background option. */
    int compositingRule;	/* Value specified for -compositingrule
				 * option. */
    Tcl_Obj *metadata;		/* Value specified for -metadata option. */
};

/*
 * Bit definitions for use with ParseSubcommandOptions: each bit is set in the
 * allowedOptions parameter on a call to ParseSubcommandOptions if that option
 * is allowed for the current photo image subcommand. On return, the bit is
 * set in the options field of the SubcommandOptions structure if that option
 * was specified.
 *
 * OPT_ALPHA:			Set if -alpha option allowed/specified.
 * OPT_BACKGROUND:		Set if -format option allowed/specified.
 * OPT_COMPOSITE:		Set if -compositingrule option allowed/spec'd.
 * OPT_FORMAT:			Set if -format option allowed/specified.
 * OPT_FROM:			Set if -from option allowed/specified.
 * OPT_GRAYSCALE:		Set if -grayscale option allowed/specified.
 * OPT_METADATA:		Set if -metadata option allowed/specified.
 * OPT_SHRINK:			Set if -shrink option allowed/specified.
 * OPT_SUBSAMPLE:		Set if -subsample option allowed/spec'd.
 * OPT_TO:			Set if -to option allowed/specified.
 * OPT_WITHALPHA:		Set if -withalpha option allowed/specified.
 * OPT_ZOOM:			Set if -zoom option allowed/specified.
 */

#define OPT_ALPHA	1
#define OPT_BACKGROUND	2
#define OPT_COMPOSITE	4
#define OPT_FORMAT	8
#define OPT_FROM	0x10
#define OPT_GRAYSCALE	0x20
#define OPT_METADATA	0x40
#define OPT_SHRINK	0x80
#define OPT_SUBSAMPLE	0x100
#define OPT_TO		0x200
#define OPT_WITHALPHA	0x400
#define OPT_ZOOM	0x800

/*
 * List of option names. The order here must match the order of declarations
 * of the OPT_* constants above.
 */

static const char *const optionNames[] = {
    "-alpha",
    "-background",
    "-compositingrule",
    "-format",
    "-from",
    "-grayscale",
    "-metadata",
    "-shrink",
    "-subsample",
    "-to",
    "-withalpha",
    "-zoom",
    NULL
};

/*
 * Message to generate when an attempt to resize an image fails due to memory
 * problems.
 */

#define TK_PHOTO_ALLOC_FAILURE_MESSAGE \
	"not enough free memory for image buffer"

/*
 * Functions used in the type record for photo images.
 */

static int		ImgPhotoCreate(Tcl_Interp *interp, const char *name,
			    Tcl_Size objc, Tcl_Obj *const objv[],
			    const Tk_ImageType *typePtr, Tk_ImageModel model,
			    void **clientDataPtr);
static void		ImgPhotoDelete(void *clientData);
static int		ImgPhotoPostscript(void *clientData,
			    Tcl_Interp *interp, Tk_Window tkwin,
			    Tk_PostscriptInfo psInfo, int x, int y, int width,
			    int height, int prepass);

/*
 * The type record itself for photo images:
 */

Tk_ImageType tkPhotoImageType = {
    "photo",			/* name */
    ImgPhotoCreate,		/* createProc */
    TkImgPhotoGet,		/* getProc */
    TkImgPhotoDisplay,		/* displayProc */
    TkImgPhotoFree,		/* freeProc */
    ImgPhotoDelete,		/* deleteProc */
    ImgPhotoPostscript,		/* postscriptProc */
    NULL,			/* nextPtr */
    NULL
};

typedef struct {
    Tk_PhotoImageFormat *formatList;
				/* Pointer to the first in the list of known
				 * photo image formats.*/
    Tk_PhotoImageFormatVersion3 *formatListVersion3;
				/* Pointer to the first in the list of known
				 * photo image formats in Version3 format.*/
    int initialized;		/* Set to 1 if we've initialized the
				 * structure. */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * Default configuration
 */

#define DEF_PHOTO_GAMMA		"1"
#define DEF_PHOTO_HEIGHT	"0"
#define DEF_PHOTO_PALETTE	""
#define DEF_PHOTO_WIDTH		"0"

/*
 * Information used for parsing configuration specifications:
 */

static const Tk_ConfigSpec configSpecs[] = {
    {TK_CONFIG_STRING, "-data", NULL, NULL,
	 NULL, TCL_INDEX_NONE, TK_CONFIG_OBJS|TK_CONFIG_NULL_OK, NULL},
    {TK_CONFIG_STRING, "-file", NULL, NULL,
	 NULL, offsetof(PhotoModel, fileObj), TK_CONFIG_OBJS|TK_CONFIG_NULL_OK, NULL},
    {TK_CONFIG_STRING, "-format", NULL, NULL,
	 NULL, TCL_INDEX_NONE, TK_CONFIG_OBJS|TK_CONFIG_NULL_OK, NULL},
    {TK_CONFIG_DOUBLE, "-gamma", NULL, NULL,
	 DEF_PHOTO_GAMMA, offsetof(PhotoModel, gamma), 0, NULL},
    {TK_CONFIG_INT, "-height", NULL, NULL,
	 DEF_PHOTO_HEIGHT, offsetof(PhotoModel, userHeight), 0, NULL},
    {TK_CONFIG_STRING, "-metadata", NULL, NULL,
	 NULL, TCL_INDEX_NONE, TK_CONFIG_OBJS|TK_CONFIG_NULL_OK, NULL},
    {TK_CONFIG_UID, "-palette", NULL, NULL,
	 DEF_PHOTO_PALETTE, offsetof(PhotoModel, palette), 0, NULL},
    {TK_CONFIG_INT, "-width", NULL, NULL,
	 DEF_PHOTO_WIDTH, offsetof(PhotoModel, userWidth), 0, NULL},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0, NULL}
};

/*
 * Forward declarations
 */

static void		PhotoFormatThreadExitProc(void *clientData);
static Tcl_ObjCmdProc2 ImgPhotoCmd;
static int		ParseSubcommandOptions(
			    struct SubcommandOptions *optPtr,
			    Tcl_Interp *interp, int allowedOptions,
			    Tcl_Size *indexPtr, Tcl_Size objc, Tcl_Obj *const objv[]);
static void		ImgPhotoCmdDeletedProc(void *clientData);
static int		ImgPhotoConfigureModel(Tcl_Interp *interp,
			    PhotoModel *modelPtr, Tcl_Size objc,
			    Tcl_Obj *const objv[], int flags);
static int		ToggleComplexAlphaIfNeeded(PhotoModel *mPtr);
static int		ImgPhotoSetSize(PhotoModel *modelPtr, int width,
			    int height);
static char *		ImgGetPhoto(PhotoModel *modelPtr,
			    Tk_PhotoImageBlock *blockPtr,
			    struct SubcommandOptions *optPtr);
static int		MatchFileFormat(Tcl_Interp *interp, Tcl_Channel chan,
			    const char *fileName, Tcl_Obj *formatString,
			    Tcl_Obj *metadataInObj,
			    Tcl_Obj *metadataOutObj,
			    Tk_PhotoImageFormat **imageFormatPtr,
			    Tk_PhotoImageFormatVersion3 **imageFormatVersion3Ptr,
			    int *widthPtr, int *heightPtr, int *oldformat);
static int		MatchStringFormat(Tcl_Interp *interp, Tcl_Obj *data,
			    Tcl_Obj *formatString,
			    Tcl_Obj *metadataInObj,
			    Tcl_Obj *metadataOutObj,
			    Tk_PhotoImageFormat **imageFormatPtr,
			    Tk_PhotoImageFormatVersion3 **imageFormatVersion3Ptr,
			    int *widthPtr, int *heightPtr, int *oldformat);
static const char *	GetExtension(const char *path);

/*
 *----------------------------------------------------------------------
 *
 * PhotoFormatThreadExitProc --
 *
 *	Clean up the registered list of photo formats.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The thread's linked lists of photo image formats is deleted.
 *
 *----------------------------------------------------------------------
 */

static void
PhotoFormatThreadExitProc(
    TCL_UNUSED(void *))	/* not used */
{
    Tk_PhotoImageFormat *freePtr;
    Tk_PhotoImageFormatVersion3 *freePtrVersion3;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    while (tsdPtr->formatList != NULL) {
	freePtr = tsdPtr->formatList;
	tsdPtr->formatList = tsdPtr->formatList->nextPtr;
	ckfree((void *)freePtr->name);
	ckfree(freePtr);
    }
    while (tsdPtr->formatListVersion3 != NULL) {
	freePtrVersion3 = tsdPtr->formatListVersion3;
	tsdPtr->formatListVersion3 = tsdPtr->formatListVersion3->nextPtr;
	ckfree((void *)freePtrVersion3->name);
	ckfree(freePtrVersion3);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_CreatePhotoImageFormat,
 * Tk_CreatePhotoImageFormatVersion3 --
 *
 *	This function is invoked by an image file handler to register a new
 *	photo image format and the functions that handle the new format. The
 *	function is typically invoked during Tcl_AppInit.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The new image file format is entered into a table used in the photo
 *	image "read" and "write" subcommands.
 *
 *----------------------------------------------------------------------
 */

void
Tk_CreatePhotoImageFormat(
    const Tk_PhotoImageFormat *formatPtr)
				/* Structure describing the format. All of the
				 * fields except "nextPtr" must be filled in
				 * by caller. */
{
    Tk_PhotoImageFormat *copyPtr;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (!tsdPtr->initialized) {
	tsdPtr->initialized = 1;
	Tcl_CreateThreadExitHandler(PhotoFormatThreadExitProc, NULL);
    }
    copyPtr = (Tk_PhotoImageFormat *)ckalloc(sizeof(Tk_PhotoImageFormat));
    *copyPtr = *formatPtr;
    {
	/* for compatibility with aMSN: make a copy of formatPtr->name */
	char *name = (char *)ckalloc(strlen(formatPtr->name) + 1);
	strcpy(name, formatPtr->name);
	copyPtr->name = name;
	copyPtr->nextPtr = tsdPtr->formatList;
	tsdPtr->formatList = copyPtr;
    }
}
void
Tk_CreatePhotoImageFormatVersion3(
    const Tk_PhotoImageFormatVersion3 *formatPtr)
				/* Structure describing the format. All of the
				 * fields except "nextPtr" must be filled in
				 * by caller. */
{
    Tk_PhotoImageFormatVersion3 *copyPtr;
    char *name;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (!tsdPtr->initialized) {
	tsdPtr->initialized = 1;
	Tcl_CreateThreadExitHandler(PhotoFormatThreadExitProc, NULL);
    }
    copyPtr = (Tk_PhotoImageFormatVersion3 *)
	    ckalloc(sizeof(Tk_PhotoImageFormatVersion3));
    *copyPtr = *formatPtr;
    /* for compatibility with aMSN: make a copy of formatPtr->name */
    name = (char *)ckalloc(strlen(formatPtr->name) + 1);
    strcpy(name, formatPtr->name);
    copyPtr->name = name;
    copyPtr->nextPtr = tsdPtr->formatListVersion3;
    tsdPtr->formatListVersion3 = copyPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * ImgPhotoCreate --
 *
 *	This function is called by the Tk image code to create a new photo
 *	image.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	The data structure for a new photo image is allocated and initialized.
 *
 *----------------------------------------------------------------------
 */

static int
ImgPhotoCreate(
    Tcl_Interp *interp,		/* Interpreter for application containing
				 * image. */
    const char *name,		/* Name to use for image. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[],	/* Argument objects for options (doesn't
				 * include image name or type). */
    TCL_UNUSED(const Tk_ImageType *),/* Pointer to our type record (not used). */
    Tk_ImageModel model,	/* Token for image, to be used by us in later
				 * callbacks. */
    void **clientDataPtr)	/* Store manager's token for image here; it
				 * will be returned in later callbacks. */
{
    PhotoModel *modelPtr;

    /*
     * Allocate and initialize the photo image model record.
     */

    modelPtr = (PhotoModel *)ckalloc(sizeof(PhotoModel));
    memset(modelPtr, 0, sizeof(PhotoModel));
    modelPtr->tkModel = model;
    modelPtr->interp = interp;
    modelPtr->imageCmd = Tcl_CreateObjCommand2(interp, name, ImgPhotoCmd,
	    modelPtr, ImgPhotoCmdDeletedProc);
    modelPtr->palette = NULL;
    modelPtr->pix32 = NULL;
    modelPtr->instancePtr = NULL;
    modelPtr->validRegion = TkCreateRegion();

    /*
     * Process configuration options given in the image create command.
     */

    if (ImgPhotoConfigureModel(interp, modelPtr, objc, objv, 0) != TCL_OK) {
	ImgPhotoDelete(modelPtr);
	return TCL_ERROR;
    }

    *clientDataPtr = modelPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ImgPhotoCmd --
 *
 *	This function is invoked to process the Tcl command that corresponds
 *	to a photo image. See the user documentation for details on what it
 *	does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

static int
ImgPhotoCmd(
    void *clientData,	/* Information about photo model. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    static const char *const photoOptions[] = {
	"blank", "cget", "configure", "copy", "data", "get", "put",
	"read", "redither", "transparency", "write", NULL
    };
    enum PhotoOptions {
	PHOTO_BLANK, PHOTO_CGET, PHOTO_CONFIGURE, PHOTO_COPY, PHOTO_DATA,
	PHOTO_GET, PHOTO_PUT, PHOTO_READ, PHOTO_REDITHER, PHOTO_TRANS,
	PHOTO_WRITE
    };

    PhotoModel *modelPtr = (PhotoModel *)clientData;
    int result, x, y, width, height;
    Tcl_Size index;
    struct SubcommandOptions options;
    unsigned char *pixelPtr;
    Tk_PhotoImageBlock block;
    Tk_PhotoImageFormat *imageFormat;
    Tk_PhotoImageFormatVersion3 *imageFormatVersion3;
    Tcl_Size length;
    int imageWidth, imageHeight, matched, oldformat = 0;
    Tcl_Channel chan;
    Tk_PhotoHandle srcHandle;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObj(interp, objv[1], photoOptions, "option", 0,
	    &index) != TCL_OK) {
	return TCL_ERROR;
    }

    switch ((enum PhotoOptions) index) {
    case PHOTO_BLANK:
	/*
	 * photo blank command - just call Tk_PhotoBlank.
	 */

	if (objc == 2) {
	    Tk_PhotoBlank(modelPtr);
	    return TCL_OK;
	} else {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}

    case PHOTO_CGET: {
	const char *arg;

	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option");
	    return TCL_ERROR;
	}
	arg = Tcl_GetStringFromObj(objv[2], &length);
	if (strncmp(arg,"-data", length) == 0) {
	    if (modelPtr->dataObj) {
		Tcl_SetObjResult(interp, modelPtr->dataObj);
	    }
	} else if (strncmp(arg,"-format", length) == 0) {
	    if (modelPtr->format) {
		Tcl_SetObjResult(interp, modelPtr->format);
	    }
	} else if (strncmp(arg, "-metadata", length) == 0) {
	    if (modelPtr->metadata) {
		Tcl_SetObjResult(interp, modelPtr->metadata);
	    }
	} else {
	    Tk_ConfigureValue(interp, Tk_MainWindow(interp), configSpecs,
		    modelPtr, Tcl_GetString(objv[2]), 0);
	}
	return TCL_OK;
    }

    case PHOTO_CONFIGURE:
	/*
	 * photo configure command - handle this in the standard way.
	 */

	if (objc == 2) {
	    Tcl_Obj *obj, *subobj;

	    result = Tk_ConfigureInfo(interp, Tk_MainWindow(interp),
		    configSpecs, modelPtr, NULL, 0);
	    if (result != TCL_OK) {
		return result;
	    }
	    obj = Tcl_NewObj();
	    subobj = Tcl_NewStringObj("-data {} {} {}", 14);
	    if (modelPtr->dataObj) {
		Tcl_ListObjAppendElement(NULL, subobj, modelPtr->dataObj);
	    } else {
		Tcl_AppendStringsToObj(subobj, " {}", (char *)NULL);
	    }
	    Tcl_ListObjAppendElement(interp, obj, subobj);
	    subobj = Tcl_NewStringObj("-format {} {} {}", 16);
	    if (modelPtr->format) {
		Tcl_ListObjAppendElement(NULL, subobj, modelPtr->format);
	    } else {
		Tcl_AppendStringsToObj(subobj, " {}", (char *)NULL);
	    }
	    Tcl_ListObjAppendElement(interp, obj, subobj);
	    subobj = Tcl_NewStringObj("-metadata {} {} {}", 18);
	    if (modelPtr->metadata) {
		Tcl_ListObjAppendElement(NULL, subobj, modelPtr->metadata);
	    } else {
		Tcl_AppendStringsToObj(subobj, " {}", (char *)NULL);
	    }
	    Tcl_ListObjAppendElement(interp, obj, subobj);
	    Tcl_ListObjAppendList(interp, obj, Tcl_GetObjResult(interp));
	    Tcl_SetObjResult(interp, obj);
	    return TCL_OK;

	} else if (objc == 3) {
	    const char *arg = Tcl_GetStringFromObj(objv[2], &length);

	    if (length > 1 && !strncmp(arg, "-data", length)) {
		Tcl_AppendResult(interp, "-data {} {} {}", (char *)NULL);
		if (modelPtr->dataObj) {
		    /*
		     * TODO: Modifying result is bad!
		     */

		    Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp),
			    modelPtr->dataObj);
		} else {
		    Tcl_AppendResult(interp, " {}", (char *)NULL);
		}
		return TCL_OK;
	    } else if (length > 1 &&
		    !strncmp(arg, "-format", length)) {
		Tcl_AppendResult(interp, "-format {} {} {}", (char *)NULL);
		if (modelPtr->format) {
		    /*
		     * TODO: Modifying result is bad!
		     */

		    Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp),
			    modelPtr->format);
		} else {
		    Tcl_AppendResult(interp, " {}", (char *)NULL);
		}
		return TCL_OK;
	    } else if (length > 1 &&
		!strncmp(arg, "-metadata", length)) {
		Tcl_AppendResult(interp, "-metadata {} {} {}", (char *)NULL);
		if (modelPtr->metadata) {
		    /*
		     * TODO: Modifying result is bad!
		     */

		    Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp),
			modelPtr->metadata);
		} else {
		    Tcl_AppendResult(interp, " {}", (char *)NULL);
		}
		return TCL_OK;
	    } else {
		return Tk_ConfigureInfo(interp, Tk_MainWindow(interp),
			configSpecs, modelPtr, arg, 0);
	    }
	} else {
	    return ImgPhotoConfigureModel(interp, modelPtr, objc-2, objv+2,
		    TK_CONFIG_ARGV_ONLY);
	}

    case PHOTO_COPY:
	/*
	 * photo copy command - first parse options.
	 */

	index = 2;
	memset(&options, 0, sizeof(options));
	options.zoomX = options.zoomY = 1;
	options.subsampleX = options.subsampleY = 1;
	options.name = NULL;
	options.compositingRule = TK_PHOTO_COMPOSITE_OVERLAY;
	if (ParseSubcommandOptions(&options, interp,
		OPT_FROM | OPT_TO | OPT_ZOOM | OPT_SUBSAMPLE | OPT_SHRINK |
		OPT_COMPOSITE, &index, objc, objv) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (options.name == NULL || index < objc) {
	    Tcl_WrongNumArgs(interp, 2, objv,
		    "source-image ?-compositingrule rule? ?-from x1 y1 x2 y2? ?-to x1 y1 x2 y2? ?-zoom x y? ?-subsample x y?");
	    return TCL_ERROR;
	}

	/*
	 * Look for the source image and get a pointer to its image data.
	 * Check the values given for the -from option.
	 */

	srcHandle = Tk_FindPhoto(interp, Tcl_GetString(options.name));
	if (srcHandle == NULL) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "image \"%s\" does not exist or is not a photo image",
		    Tcl_GetString(options.name)));
	    Tcl_SetErrorCode(interp, "TK", "LOOKUP", "PHOTO",
		    Tcl_GetString(options.name), (char *)NULL);
	    return TCL_ERROR;
	}
	Tk_PhotoGetImage(srcHandle, &block);
	if ((options.fromX > block.width) || (options.fromY > block.height)
		|| (options.fromX2 > block.width)
		|| (options.fromY2 > block.height)) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "coordinates for -from option extend outside source image",
		    -1));
	    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "BAD_FROM", (char *)NULL);
	    return TCL_ERROR;
	}

	/*
	 * Hack to pass through the message that the place we're coming from
	 * has a simple alpha channel.
	 */

	if (!(((PhotoModel *) srcHandle)->flags & COMPLEX_ALPHA)) {
	    options.compositingRule |= SOURCE_IS_SIMPLE_ALPHA_PHOTO;
	}

	/*
	 * Fill in default values for unspecified parameters.
	 */

	if (!(options.options & OPT_FROM) || (options.fromX2 < 0)) {
	    options.fromX2 = block.width;
	    options.fromY2 = block.height;
	}
	if (!(options.options & OPT_TO) || (options.toX2 < 0)) {
	    width = options.fromX2 - options.fromX;
	    if (options.subsampleX > 0) {
		width = (width + options.subsampleX - 1) / options.subsampleX;
	    } else if (options.subsampleX == 0) {
		width = 0;
	    } else {
		width = (width - options.subsampleX - 1) / -options.subsampleX;
	    }
	    options.toX2 = options.toX + width * options.zoomX;

	    height = options.fromY2 - options.fromY;
	    if (options.subsampleY > 0) {
		height = (height + options.subsampleY - 1)
			/ options.subsampleY;
	    } else if (options.subsampleY == 0) {
		height = 0;
	    } else {
		height = (height - options.subsampleY - 1)
			/ -options.subsampleY;
	    }
	    options.toY2 = options.toY + height * options.zoomY;
	}

	/*
	 * Copy the image data over using Tk_PhotoPutZoomedBlock.
	 */

	if (block.pixelPtr) {
	    block.pixelPtr += options.fromX * block.pixelSize
		    + options.fromY * block.pitch;
	    block.width = options.fromX2 - options.fromX;
	    block.height = options.fromY2 - options.fromY;
	    result = Tk_PhotoPutZoomedBlock(interp, (Tk_PhotoHandle) modelPtr,
		    &block, options.toX, options.toY, options.toX2 - options.toX,
		    options.toY2 - options.toY, options.zoomX, options.zoomY,
		    options.subsampleX, options.subsampleY,
		    options.compositingRule);
	} else {
	    result = TCL_OK;
	}

	/*
	 * Set the destination image size if the -shrink option was specified.
	 * This has to be done _after_ copying the data. Otherwise, if source
	 * and destination are the same image, block.pixelPtr would point to
	 * an invalid memory block (bug [5239fd749b]).
	 */

	if (options.options & OPT_SHRINK) {
	    if (ImgPhotoSetSize(modelPtr, options.toX2,
		    options.toY2) != TCL_OK) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			TK_PHOTO_ALLOC_FAILURE_MESSAGE, TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "MALLOC", (char *)NULL);
		return TCL_ERROR;
	    }
	}
	if (block.pixelPtr || (options.options & OPT_SHRINK)) {
	    Tk_ImageChanged(modelPtr->tkModel, 0, 0, 0, 0,
		    modelPtr->width, modelPtr->height);
	}
	return result;

    case PHOTO_DATA: {
	char *data = NULL;
	Tcl_Obj *freeObj = NULL;
	Tcl_Obj *metadataIn;

	/*
	 * photo data command - first parse and check any options given.
	 */

	Tk_ImageStringWriteProc *stringWriteProc = NULL;
	Tk_ImageStringWriteProcVersion3 *stringWriteProcVersion3 = NULL;

	index = 1;
	memset(&options, 0, sizeof(options));
	options.name = NULL;
	options.format = NULL;
	options.metadata = NULL;
	options.fromX = 0;
	options.fromY = 0;
	if (ParseSubcommandOptions(&options, interp,
		OPT_FORMAT | OPT_FROM | OPT_GRAYSCALE | OPT_BACKGROUND
		| OPT_METADATA,
		&index, objc, objv) != TCL_OK) {
	    return TCL_ERROR;
	}
	if ((options.name == NULL) || (index < objc)) {
	    Tcl_WrongNumArgs(interp, 2, objv, "?-option value ...?");
	    if (options.background) {
		Tk_FreeColor(options.background);
	    }
	    return TCL_ERROR;
	}
	if ((options.fromX > modelPtr->width)
		|| (options.fromY > modelPtr->height)
		|| (options.fromX2 > modelPtr->width)
		|| (options.fromY2 > modelPtr->height)) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "coordinates for -from option extend outside image", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "BAD_FROM", (char *)NULL);
	    if (options.background) {
		Tk_FreeColor(options.background);
	    }
	    return TCL_ERROR;
	}

	/*
	 * Fill in default values for unspecified parameters.
	 */

	if (!(options.options & OPT_FROM) || (options.fromX2 < 0)) {
	    options.fromX2 = modelPtr->width;
	    options.fromY2 = modelPtr->height;
	}
	if (!(options.options & OPT_FORMAT)) {
	    options.format = Tcl_NewStringObj("default", TCL_INDEX_NONE);
	    freeObj = options.format;
	}

	/*
	 * Use argument metadata if specified, otherwise the master metadata
	 */

	if (NULL != options.metadata) {
	    metadataIn = options.metadata;
	} else {
	    metadataIn = modelPtr->metadata;
	}

	/*
	 * Search for an appropriate image string format handler.
	 */

	matched = 0;
	for (imageFormat = tsdPtr->formatList; imageFormat != NULL;
		imageFormat = imageFormat->nextPtr) {
	    if ((strncasecmp(Tcl_GetString(options.format),
		    imageFormat->name, strlen(imageFormat->name)) == 0)) {
		matched = 1;
		if (imageFormat->stringWriteProc != NULL) {
		    stringWriteProc = imageFormat->stringWriteProc;
		    break;
		}
	    }
	}
	if (stringWriteProc == NULL) {
	    oldformat = 0;
	    for (imageFormatVersion3 = tsdPtr->formatListVersion3;
		    imageFormatVersion3 != NULL;
		    imageFormatVersion3 = imageFormatVersion3->nextPtr) {
		if ((strncasecmp(Tcl_GetString(options.format),
			imageFormatVersion3->name,
			strlen(imageFormatVersion3->name)) == 0)) {
		    matched = 1;
		    if (imageFormatVersion3->stringWriteProc != NULL) {
			stringWriteProcVersion3 =
				imageFormatVersion3->stringWriteProc;
			break;
		    }
		}
	    }
	}
	if (stringWriteProc == NULL && stringWriteProcVersion3 == NULL) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "image string format \"%s\" is %s",
		    Tcl_GetString(options.format),
		    (matched ? "not supported" : "unknown")));
	    Tcl_SetErrorCode(interp, "TK", "LOOKUP", "PHOTO_FORMAT",
		    Tcl_GetString(options.format), (char *)NULL);
	    goto dataErrorExit;
	}

	/*
	 * Call the handler's string write function to write out the image.
	 */

	data = ImgGetPhoto(modelPtr, &block, &options);

	if (stringWriteProc == NULL) {
	    result = (stringWriteProcVersion3)(interp,
		    options.format, metadataIn, &block);
	} else if (oldformat) {
	    Tcl_DString buffer;
	    typedef int (*OldStringWriteProc)(Tcl_Interp *interp,
		    Tcl_DString *dataPtr, const char *formatString,
		    Tk_PhotoImageBlock *blockPtr);

	    Tcl_DStringInit(&buffer);
	    result = ((OldStringWriteProc)(void *)stringWriteProc)(interp, &buffer,
		    Tcl_GetString(options.format), &block);
	    if (result == TCL_OK) {
		Tcl_DStringResult(interp, &buffer);
	    } else {
		Tcl_DStringFree(&buffer);
	    }
	} else {
	    typedef int (*NewStringWriteProc)(Tcl_Interp *interp,
		    Tcl_Obj *formatString, Tk_PhotoImageBlock *blockPtr);

	    result = ((NewStringWriteProc)(void *)stringWriteProc)(interp,
		    options.format, &block);
	}
	if (options.background) {
	    Tk_FreeColor(options.background);
	}
	if (data) {
	    ckfree(data);
	}
	if (freeObj != NULL) {
	    Tcl_DecrRefCount(freeObj);
	}
	return result;

      dataErrorExit:
	if (options.background) {
	    Tk_FreeColor(options.background);
	}
	if (data) {
	    ckfree(data);
	}
	if (freeObj != NULL) {
	    Tcl_DecrRefCount(freeObj);
	}
	return TCL_ERROR;
    }

    case PHOTO_GET: {
	/*
	 * photo get command - first parse and check parameters.
	 */

	Tcl_Obj *channels[4];
	int i, channelCount = 3;

	index = 3;
	memset(&options, 0, sizeof(options));
	options.name = NULL;
	if (ParseSubcommandOptions(&options, interp, OPT_WITHALPHA,
		&index, objc, objv) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (options.name == NULL || index < objc) {
	    Tcl_WrongNumArgs(interp, 2, objv, "x y ?-withalpha?");
	    return TCL_ERROR;
	}
	if (options.options & OPT_WITHALPHA) {
	    channelCount = 4;
	}

	if ((Tcl_GetIntFromObj(interp, objv[2], &x) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[3], &y) != TCL_OK)) {
	    return TCL_ERROR;
	}
	if ((x < 0) || (x >= modelPtr->width)
		|| (y < 0) || (y >= modelPtr->height)) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "%s get: coordinates out of range",
		    Tcl_GetString(objv[0])));
	    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "COORDINATES",
		    (char *)NULL);
	    return TCL_ERROR;
	}

	/*
	 * Extract the value of the desired pixel and format it as a list.
	 */

	pixelPtr = modelPtr->pix32 + (y * modelPtr->width + x) * 4;
	for (i = 0; i < channelCount; i++) {
	    channels[i] = Tcl_NewWideIntObj(pixelPtr[i]);
	}
	Tcl_SetObjResult(interp, Tcl_NewListObj(channelCount, channels));
	return TCL_OK;
    }

    case PHOTO_PUT: {
	Tcl_Obj *format, *data;

	/*
	 * photo put command - first parse the options.
	 */

	index = 2;
	memset(&options, 0, sizeof(options));
	options.name = NULL;
	options.format = NULL;
	options.metadata = NULL;
	if (ParseSubcommandOptions(&options, interp,
		OPT_TO|OPT_FORMAT|OPT_METADATA,
		&index, objc, objv) != TCL_OK) {
	    return TCL_ERROR;
	}
	if ((options.name == NULL) || (index < objc)) {
	    Tcl_WrongNumArgs(interp, 2, objv, "data ?-option value ...?");
	    return TCL_ERROR;
	}

	/*
	 * See if there's a format that can read the data
	 */

	if (MatchStringFormat(interp, objv[2], options.format,
		options.metadata, NULL, &imageFormat,
		&imageFormatVersion3, &imageWidth, &imageHeight, &oldformat)
		!= TCL_OK) {
	    return TCL_ERROR;
	}

	if (!(options.options & OPT_TO) || (options.toX2 < 0)) {
	    options.toX2 = options.toX + imageWidth;
	    options.toY2 = options.toY + imageHeight;
	}
	if (imageWidth > options.toX2 - options.toX) {
	    imageWidth = options.toX2 - options.toX;
	}
	if (imageHeight > options.toY2 - options.toY) {
	    imageHeight = options.toY2 - options.toY;
	}
	format = options.format;
	data = objv[2];
	if (oldformat) {
	    if (format) {
		format = (Tcl_Obj *) Tcl_GetString(format);
	    }
	    data = (Tcl_Obj *) Tcl_GetString(data);
	}

	if (imageFormat != NULL) {
	    if (imageFormat->stringReadProc(interp, data, format,
		    (Tk_PhotoHandle) modelPtr, options.toX, options.toY,
		    options.toX2 - options.toX,
		    options.toY2 - options.toY, 0, 0) != TCL_OK) {
		return TCL_ERROR;
	    }
	} else {
	    if (imageFormatVersion3->stringReadProc(interp, data, format,
		    options.metadata,
		    (Tk_PhotoHandle) modelPtr, options.toX, options.toY,
		    options.toX2 - options.toX,
		    options.toY2 - options.toY, 0, 0,
		    NULL)
		    != TCL_OK) {
		return TCL_ERROR;
	    }
	}

	/*
	 * SB: is the next line really needed? The stringReadProc
	 * writes image data with Tk_PhotoPutBlock(), which in turn
	 * takes care to notify the changed image and to set/unset the
	 * IMAGE_CHANGED bit.
	 */
	modelPtr->flags |= IMAGE_CHANGED;

	return TCL_OK;
    }
    case PHOTO_READ: {
	Tcl_Obj *format;

	/*
	 * photo read command - first parse the options specified.
	 */

	index = 2;
	memset(&options, 0, sizeof(options));
	options.name = NULL;
	options.format = NULL;
	options.metadata = NULL;
	if (ParseSubcommandOptions(&options, interp,
		OPT_FORMAT | OPT_FROM | OPT_TO | OPT_SHRINK | OPT_METADATA,
		&index, objc, objv) != TCL_OK) {
	    return TCL_ERROR;
	}
	if ((options.name == NULL) || (index < objc)) {
	    Tcl_WrongNumArgs(interp, 2, objv, "fileName ?-option value ...?");
	    return TCL_ERROR;
	}

	/*
	 * Prevent file system access in safe interpreters.
	 */

	if (Tcl_IsSafe(interp)) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "can't get image from a file in a safe interpreter", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "SAFE", "PHOTO_FILE", (char *)NULL);
	    return TCL_ERROR;
	}

	/*
	 * Open the image file and look for a handler for it.
	 */

	chan = Tcl_OpenFileChannel(interp,
		Tcl_GetString(options.name), "r", 0);
	if (chan == NULL) {
	    return TCL_ERROR;
	}
	if (Tcl_SetChannelOption(interp, chan, "-translation", "binary")
		!= TCL_OK) {
	    Tcl_Close(NULL, chan);
	    return TCL_ERROR;
	}

	if (MatchFileFormat(interp, chan,
		Tcl_GetString(options.name), options.format,
		options.metadata, NULL, &imageFormat,
		&imageFormatVersion3, &imageWidth, &imageHeight, &oldformat)
		!= TCL_OK) {
	    result = TCL_ERROR;
	    goto readCleanup;
	}

	/*
	 * Check the values given for the -from option.
	 */

	if ((options.fromX > imageWidth) || (options.fromY > imageHeight)
		|| (options.fromX2 > imageWidth)
		|| (options.fromY2 > imageHeight)) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "coordinates for -from option extend outside source image",
		    -1));
	    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "BAD_FROM", (char *)NULL);
	    result = TCL_ERROR;
	    goto readCleanup;
	}
	if (!(options.options & OPT_FROM) || (options.fromX2 < 0)) {
	    width = imageWidth - options.fromX;
	    height = imageHeight - options.fromY;
	} else {
	    width = options.fromX2 - options.fromX;
	    height = options.fromY2 - options.fromY;
	}

	/*
	 * If the -shrink option was specified, set the size of the image.
	 */

	if (options.options & OPT_SHRINK) {
	    if (ImgPhotoSetSize(modelPtr, options.toX + width,
		    options.toY + height) != TCL_OK) {
		Tcl_ResetResult(interp);
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			TK_PHOTO_ALLOC_FAILURE_MESSAGE, TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "MALLOC", (char *)NULL);
		result = TCL_ERROR;
		goto readCleanup;
	    }
	}

	/*
	 * Call the handler's file read function to read the data into the
	 * image.
	 */

	format = options.format;
	if (oldformat && format) {
	    format = (Tcl_Obj *) Tcl_GetString(format);
	}
	if (imageFormat != NULL) {
	    result = imageFormat->fileReadProc(interp, chan,
		    Tcl_GetString(options.name),
		    format, (Tk_PhotoHandle) modelPtr, options.toX,
		    options.toY, width, height, options.fromX, options.fromY);
	} else {
	    result = imageFormatVersion3->fileReadProc(interp, chan,
		    Tcl_GetString(options.name),
		    format, options.metadata, (Tk_PhotoHandle) modelPtr,
		    options.toX, options.toY, width, height, options.fromX,
		    options.fromY, NULL);
	}
readCleanup:
	if (chan != NULL) {
	    Tcl_Close(NULL, chan);
	}
	return result;
    }

    case PHOTO_REDITHER:
	if (objc != 2) {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}

	/*
	 * Call Dither if any part of the image is not correctly dithered at
	 * present.
	 */

	x = modelPtr->ditherX;
	y = modelPtr->ditherY;
	if (modelPtr->ditherX != 0) {
	    Tk_DitherPhoto((Tk_PhotoHandle) modelPtr, x, y,
		    modelPtr->width - x, 1);
	}
	if (modelPtr->ditherY < modelPtr->height) {
	    x = 0;
	    Tk_DitherPhoto((Tk_PhotoHandle)modelPtr, 0,
		    modelPtr->ditherY, modelPtr->width,
		    modelPtr->height - modelPtr->ditherY);
	}

	if (y < modelPtr->height) {
	    /*
	     * Tell the core image code that part of the image has changed.
	     */

	    Tk_ImageChanged(modelPtr->tkModel, x, y,
		    (modelPtr->width - x), (modelPtr->height - y),
		    modelPtr->width, modelPtr->height);
	}
	return TCL_OK;

    case PHOTO_TRANS: {
	static const char *const photoTransOptions[] = {
	    "get", "set", NULL
	};
	enum transOptions {
	    PHOTO_TRANS_GET, PHOTO_TRANS_SET
	};

	if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option ?arg ...?");
	    return TCL_ERROR;
	}
	if (Tcl_GetIndexFromObj(interp, objv[2], photoTransOptions, "option",
		0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}

	switch ((enum transOptions) index) {
	case PHOTO_TRANS_GET: {
	    int boolMode;

	    /*
	     * parse fixed args and option
	     */

	    if (objc > 6 || objc < 5) {
		Tcl_WrongNumArgs(interp, 3, objv, "x y ?-option?");
		return TCL_ERROR;
	    }
	    if ((Tcl_GetIntFromObj(interp, objv[3], &x) != TCL_OK)
		    || (Tcl_GetIntFromObj(interp, objv[4], &y) != TCL_OK)) {
		return TCL_ERROR;
	    }

	    index = 4;
	    memset(&options, 0, sizeof(options));
	    if (ParseSubcommandOptions(&options, interp,
		    OPT_ALPHA, &index, objc, objv) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (index < objc) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"unknown option \"%s\": must be -alpha",
			Tcl_GetString(objv[index])));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "BAD_OPTION",
			(char *)NULL);
		return TCL_ERROR;
	    }
	    boolMode = 1;
	    if (options.options & OPT_ALPHA) {
		boolMode = 0;
	    }

	    if ((x < 0) || (x >= modelPtr->width)
		    || (y < 0) || (y >= modelPtr->height)) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"%s transparency get: coordinates out of range",
			Tcl_GetString(objv[0])));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "COORDINATES",
			(char *)NULL);
		return TCL_ERROR;
	    }

	    /*
	     * Extract and return the desired value
	     */
	    pixelPtr = modelPtr->pix32 + (y * modelPtr->width + x) * 4;
	    if (boolMode) {
		Tcl_SetObjResult(interp, Tcl_NewBooleanObj(pixelPtr[3] == 0));
	    } else {
		Tcl_SetObjResult(interp, Tcl_NewWideIntObj(pixelPtr[3]));
	    }
	    return TCL_OK;
	}

	case PHOTO_TRANS_SET: {
	    int newVal, boolMode;
	    XRectangle setBox;
	    TkRegion modRegion;

	    /*
	     * Parse args and option, check for valid values
	     */

	    if (objc < 6 || objc > 7) {
		Tcl_WrongNumArgs(interp, 3, objv, "x y newVal ?-option?");
		return TCL_ERROR;
	    }
	    if ((Tcl_GetIntFromObj(interp, objv[3], &x) != TCL_OK)
		    || (Tcl_GetIntFromObj(interp, objv[4], &y) != TCL_OK)) {
		return TCL_ERROR;
	    }

	    index = 5;
	    memset(&options, 0, sizeof(options));
	    if (ParseSubcommandOptions(&options, interp,
		    OPT_ALPHA, &index, objc, objv) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (index < objc) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"unknown option \"%s\": must be -alpha",
			Tcl_GetString(objv[index])));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "BAD_OPTION",
			(char *)NULL);
		return TCL_ERROR;
	    }
	    boolMode = 1;
	    if (options.options & OPT_ALPHA) {
		boolMode = 0;
	    }

	    if ((x < 0) || (x >= modelPtr->width)
		|| (y < 0) || (y >= modelPtr->height)) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"%s transparency set: coordinates out of range",
			Tcl_GetString(objv[0])));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "COORDINATES",
			(char *)NULL);
		return TCL_ERROR;
	    }

	    if (boolMode) {
		if (Tcl_GetBooleanFromObj(interp, objv[5], &newVal) != TCL_OK) {
		    return TCL_ERROR;
		}
	    } else {
		if (Tcl_GetIntFromObj(interp, objv[5], &newVal) != TCL_OK) {
		    return TCL_ERROR;
		}
		if (newVal < 0 || newVal > 255) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			    "invalid alpha value \"%d\": "
			    "must be integer between 0 and 255", newVal));
		    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
			    "BAD_VALUE", (char *)NULL);
		    return TCL_ERROR;
		}
	    }

	    /*
	     * Set new alpha value for the pixel
	     */

	    pixelPtr = modelPtr->pix32 + (y * modelPtr->width + x) * 4;
	    if (boolMode) {
		pixelPtr[3] = newVal ? 0 : 255;
	    } else {
		pixelPtr[3] = newVal;
	    }

	    /*
	     * Update the validRegion of the image
	     */

	    setBox.x = x;
	    setBox.y = y;
	    setBox.width = 1;
	    setBox.height = 1;
	    modRegion = TkCreateRegion();
	    TkUnionRectWithRegion(&setBox, modRegion, modRegion);
	    if (pixelPtr[3]) {
		TkUnionRectWithRegion(&setBox, modelPtr->validRegion,
			modelPtr->validRegion);
	    } else {
		TkSubtractRegion(modelPtr->validRegion, modRegion,
			modelPtr->validRegion);
	    }
	    TkDestroyRegion(modRegion);

	    /*
	     * Inform the generic image code that the image
	     * has (potentially) changed.
	     */

	    Tk_ImageChanged(modelPtr->tkModel, x, y, 1, 1,
		    modelPtr->width, modelPtr->height);
	    modelPtr->flags &= ~IMAGE_CHANGED;
	    return TCL_OK;
	}

	}
	Tcl_Panic("unexpected fallthrough");
	break;
    }

    case PHOTO_WRITE: {
	char *data;
	const char *fmtString;
	Tcl_Obj *format, *metadataIn;
	int usedExt;

	/*
	 * Prevent file system access in safe interpreters.
	 */

	if (Tcl_IsSafe(interp)) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "can't write image to a file in a safe interpreter", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "SAFE", "PHOTO_FILE", (char *)NULL);
	    return TCL_ERROR;
	}

	/*
	 * photo write command - first parse and check any options given.
	 */

	index = 2;
	memset(&options, 0, sizeof(options));
	options.name = NULL;
	options.format = NULL;
	options.metadata = NULL;
	if (ParseSubcommandOptions(&options, interp,
		OPT_FORMAT | OPT_FROM | OPT_GRAYSCALE | OPT_BACKGROUND
		| OPT_METADATA,
		&index, objc, objv) != TCL_OK) {
	    return TCL_ERROR;
	}
	if ((options.name == NULL) || (index < objc)) {
	    Tcl_WrongNumArgs(interp, 2, objv, "fileName ?-option value ...?");
	    if (options.background) {
		Tk_FreeColor(options.background);
	    }
	    return TCL_ERROR;
	}
	if ((options.fromX > modelPtr->width)
		|| (options.fromY > modelPtr->height)
		|| (options.fromX2 > modelPtr->width)
		|| (options.fromY2 > modelPtr->height)) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "coordinates for -from option extend outside image", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "BAD_FROM", (char *)NULL);
	    if (options.background) {
		Tk_FreeColor(options.background);
	    }
	    return TCL_ERROR;
	}

	/*
	 * Fill in default values for unspecified parameters. Note that a
	 * missing -format flag results in us having a guess from the file
	 * extension. [Bug 2983824]
	 */

	if (!(options.options & OPT_FROM) || (options.fromX2 < 0)) {
	    options.fromX2 = modelPtr->width;
	    options.fromY2 = modelPtr->height;
	}
	if (options.format == NULL) {
	    fmtString = GetExtension(Tcl_GetString(options.name));
	    usedExt = (fmtString != NULL);
	} else {
	    fmtString = Tcl_GetString(options.format);
	    usedExt = 0;
	}


	/*
	 * Use argument metadata if specified, otherwise the master metadata
	 */

	if (NULL != options.metadata) {
	    metadataIn = options.metadata;
	} else {
	    metadataIn = modelPtr->metadata;
	}

	/*
	 * Search for an appropriate image file format handler, and give an
	 * error if none is found.
	 */

	matched = 0;
    redoFormatLookup:
	imageFormatVersion3 = NULL;
	for (imageFormat = tsdPtr->formatList; imageFormat != NULL;
		imageFormat = imageFormat->nextPtr) {
	    if ((fmtString == NULL)
		    || (strncasecmp(fmtString, imageFormat->name,
			    strlen(imageFormat->name)) == 0)) {
		matched = 1;
		if (imageFormat->fileWriteProc != NULL) {
		    break;
		}
	    }
	}
	if (imageFormat == NULL) {
	    oldformat = 0;
	    for (imageFormatVersion3 = tsdPtr->formatListVersion3;
		    imageFormatVersion3 != NULL;
		    imageFormatVersion3 = imageFormatVersion3->nextPtr) {
		if ((fmtString == NULL)
			|| (strncasecmp(fmtString, imageFormatVersion3->name,
				strlen(imageFormatVersion3->name)) == 0)) {
		    matched = 1;
		    if (imageFormatVersion3->fileWriteProc != NULL) {
			break;
		    }
		}
	    }
	}
	if (usedExt && !matched) {
	    /*
	     * If we didn't find one and we're using file extensions as the
	     * basis for the guessing, go back and look again without
	     * prejudice. Supports old broken code.
	     */

	    usedExt = 0;
	    fmtString = NULL;
	    goto redoFormatLookup;
	}
	if (imageFormat == NULL && imageFormatVersion3 == NULL) {
	    if (fmtString == NULL) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"no available image file format has file writing"
			" capability", TCL_INDEX_NONE));
	    } else if (!matched) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"image file format \"%s\" is unknown", fmtString));
	    } else {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"image file format \"%s\" has no file writing capability",
			fmtString));
	    }
	    Tcl_SetErrorCode(interp, "TK", "LOOKUP", "PHOTO_FORMAT",
		    fmtString, (char *)NULL);
	    if (options.background) {
		Tk_FreeColor(options.background);
	    }
	    return TCL_ERROR;
	}

	/*
	 * Call the handler's file write function to write out the image.
	 */

	data = ImgGetPhoto(modelPtr, &block, &options);
	format = options.format;
	if (oldformat && format) {
	    format = (Tcl_Obj *) Tcl_GetString(options.format);
	}
	if (imageFormat != NULL) {
	    result = imageFormat->fileWriteProc(interp,
		    Tcl_GetString(options.name), format, &block);
	} else {
	    result = imageFormatVersion3->fileWriteProc(interp,
		    Tcl_GetString(options.name), format, metadataIn,
		    &block);
	}
	if (options.background) {
	    Tk_FreeColor(options.background);
	}
	if (data) {
	    ckfree(data);
	}
	return result;
    }

    }
    Tcl_Panic("unexpected fallthrough");
    return TCL_ERROR; /* NOT REACHED */
}

/*
 *----------------------------------------------------------------------
 *
 * GetExtension --
 *
 *	Return the extension part of a path, or NULL if there is no extension.
 *	The returned string will be a substring of the argument string, so
 *	should not be ckfree()d directly. No side effects.
 *
 *----------------------------------------------------------------------
 */

static const char *
GetExtension(
    const char *path)
{
    char c;
    const char *extension = NULL;

    for (; (c=*path++) != '\0' ;) {
	if (c == '.') {
	    extension = path;
	}
    }
    if (extension != NULL && extension[0] == '\0') {
	extension = NULL;
    }
    return extension;
}

/*
 *----------------------------------------------------------------------
 *
 * ParseSubcommandOptions --
 *
 *	This function is invoked to process one of the options which may be
 *	specified for the photo image subcommands, namely, -from, -to, -zoom,
 *	-subsample, -format, -shrink, -compositingrule, -alpha, -boolean,
 *	-withalpha and -metadata.
 *	Parsing starts at the index in *optIndexPtr and stops at the end of
 *	objv[] or at the first value that does not belong to an option.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Fields in *optPtr get filled in. The value of optIndexPtr is updated
 *	to contain the index of the first element in argv[] that was not
 *	parsed, or objc if the end of objv[] was reached.
 *
 *----------------------------------------------------------------------
 */

static int
ParseSubcommandOptions(
    struct SubcommandOptions *optPtr,
				/* Information about the options specified and
				 * the values given is returned here. */
    Tcl_Interp *interp,		/* Interpreter to use for reporting errors. */
    int allowedOptions,		/* Indicates which options are valid for the
				 * current command. */
    Tcl_Size *optIndexPtr,		/* Points to a variable containing the current
				 * index in objv; this variable is updated by
				 * this function. */
    Tcl_Size objc,			/* Number of arguments in objv[]. */
    Tcl_Obj *const objv[])	/* Arguments to be parsed. */
{
    static const char *const compositingRules[] = {
	"overlay", "set",	/* Note that these must match the
				 * TK_PHOTO_COMPOSITE_* constants. */
	NULL
    };
    Tcl_Size index, length, argIndex;
    int c, bit, currentBit;
    int values[4], numValues, maxValues;
    const char *option, *expandedOption, *needed;
    const char *const *listPtr;
    Tcl_Obj *msgObj;

    for (index = *optIndexPtr; index < objc; *optIndexPtr = ++index) {
	/*
	 * We can have one value specified without an option; it goes into
	 * optPtr->name.
	 */

	expandedOption = option = Tcl_GetStringFromObj(objv[index], &length);
	if (option[0] != '-') {
	    if (optPtr->name == NULL) {
		optPtr->name = objv[index];
		continue;
	    }
	    break;
	}

	/*
	 * Work out which option this is.
	 */

	c = option[0];
	bit = 0;
	currentBit = 1;
	for (listPtr = optionNames; *listPtr != NULL; ++listPtr) {
	    if ((c == *listPtr[0])
		    && (strncmp(option, *listPtr, length) == 0)) {
		expandedOption = *listPtr;
		if (bit != 0) {
		    goto unknownOrAmbiguousOption;
		}
		bit = currentBit;
	    }
	    currentBit <<= 1;
	}

	/*
	 * If this option is not recognized and allowed, put an error message
	 * in the interpreter and return.
	 */

	if (!(allowedOptions & bit)) {
	    if (optPtr->name != NULL) {
		goto unknownOrAmbiguousOption;
	    }
	    optPtr->name = objv[index];
	    continue;
	}

	/*
	 * For the -from, -to, -zoom and -subsample options, parse the values
	 * given. Report an error if too few or too many values are given.
	 */

	if (bit == OPT_BACKGROUND) {
	    /*
	     * The -background option takes a single XColor value.
	     */

	    if (index + 1 >= objc) {
		goto oneValueRequired;
	    }
	    *optIndexPtr = ++index;
	    optPtr->background = Tk_AllocColorFromObj(interp, Tk_MainWindow(interp),
		    objv[index]);
	    if (!optPtr->background) {
		return TCL_ERROR;
	    }
	} else if (bit == OPT_FORMAT) {
	    /*
	     * The -format option takes a single string value. Note that
	     * parsing this is outside the scope of this function.
	     */

	    if (index + 1 >= objc) {
		goto oneValueRequired;
	    }
	    *optIndexPtr = ++index;
	    optPtr->format = objv[index];
	} else if (bit == OPT_METADATA) {
	    /*
	    * The -metadata option takes a single dict value. Note that
	    * parsing this is outside the scope of this function.
	    */

	    if (index + 1 >= objc) {
		goto oneValueRequired;
	    }
	    *optIndexPtr = ++index;
	    optPtr->metadata = objv[index];
	} else if (bit == OPT_COMPOSITE) {
	    /*
	     * The -compositingrule option takes a single value from a
	     * well-known set.
	     */

	    if (index + 1 >= objc) {
		goto oneValueRequired;
	    }
	    index++;
	    if (Tcl_GetIndexFromObj(interp, objv[index], compositingRules,
		    "compositing rule", 0, &optPtr->compositingRule)
		    != TCL_OK) {
		return TCL_ERROR;
	    }
	    *optIndexPtr = index;
	} else if (bit == OPT_TO || bit == OPT_FROM
		|| bit == OPT_SUBSAMPLE || bit == OPT_ZOOM) {
	    const char *val;

	    maxValues = ((bit == OPT_FROM) || (bit == OPT_TO)) ? 4 : 2;
	    argIndex = index + 1;
	    for (numValues = 0; numValues < maxValues; ++numValues) {
		if (argIndex >= objc) {
		    break;
		}
		val = Tcl_GetString(objv[argIndex]);
		if ((argIndex < objc) && (isdigit(UCHAR(val[0]))
			|| ((val[0] == '-') && isdigit(UCHAR(val[1]))))) {
		    if (Tcl_GetInt(interp, val, &values[numValues])
			    != TCL_OK) {
			return TCL_ERROR;
		    }
		} else {
		    break;
		}
		argIndex++;
	    }

	    if (numValues == 0) {
		goto manyValuesRequired;
	    }
	    *optIndexPtr = (index += numValues);

	    /*
	     * Y values default to the corresponding X value if not specified.
	     */

	    if (numValues == 1) {
		values[1] = values[0];
	    }
	    if (numValues == 3) {
		values[3] = values[2];
	    }

	    /*
	     * Check the values given and put them in the appropriate field of
	     * the SubcommandOptions structure.
	     */

	    switch (bit) {
	    case OPT_FROM:
		if ((values[0] < 0) || (values[1] < 0) || ((numValues > 2)
			&& ((values[2] < 0) || (values[3] < 0)))) {
		    needed = "non-negative";
		    goto numberOutOfRange;
		}
		if (numValues <= 2) {
		    optPtr->fromX = values[0];
		    optPtr->fromY = values[1];
		    optPtr->fromX2 = -1;
		    optPtr->fromY2 = -1;
		} else {
		    optPtr->fromX = MIN(values[0], values[2]);
		    optPtr->fromY = MIN(values[1], values[3]);
		    optPtr->fromX2 = MAX(values[0], values[2]);
		    optPtr->fromY2 = MAX(values[1], values[3]);
		}
		break;
	    case OPT_SUBSAMPLE:
		optPtr->subsampleX = values[0];
		optPtr->subsampleY = values[1];
		break;
	    case OPT_TO:
		if ((values[0] < 0) || (values[1] < 0) || ((numValues > 2)
			&& ((values[2] < 0) || (values[3] < 0)))) {
		    needed = "non-negative";
		    goto numberOutOfRange;
		}
		if (numValues <= 2) {
		    optPtr->toX = values[0];
		    optPtr->toY = values[1];
		    optPtr->toX2 = -1;
		    optPtr->toY2 = -1;
		} else {
		    optPtr->toX = MIN(values[0], values[2]);
		    optPtr->toY = MIN(values[1], values[3]);
		    optPtr->toX2 = MAX(values[0], values[2]);
		    optPtr->toY2 = MAX(values[1], values[3]);
		}
		break;
	    case OPT_ZOOM:
		if ((values[0] <= 0) || (values[1] <= 0)) {
		    needed = "positive";
		    goto numberOutOfRange;
		}
		optPtr->zoomX = values[0];
		optPtr->zoomY = values[1];
		break;
	    }
	}

	/*
	 * Remember that we saw this option.
	 */

	optPtr->options |= bit;
    }
    return TCL_OK;

    /*
     * Exception generation.
     */

  oneValueRequired:
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "the \"%s\" option requires a value", expandedOption));
    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "MISSING_VALUE", (char *)NULL);
    return TCL_ERROR;

  manyValuesRequired:
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "the \"%s\" option requires one %s integer values",
	    expandedOption, (maxValues == 2) ? "or two": "to four"));
    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "MISSING_VALUE", (char *)NULL);
    return TCL_ERROR;

  numberOutOfRange:
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "value(s) for the %s option must be %s", expandedOption, needed));
    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "BAD_VALUE", (char *)NULL);
    return TCL_ERROR;

  unknownOrAmbiguousOption:
    msgObj = Tcl_ObjPrintf("unrecognized option \"%s\": must be ", option);
    bit = 1;
    for (listPtr = optionNames; *listPtr != NULL; ++listPtr) {
	if (allowedOptions & bit) {
	    if (allowedOptions & (bit - 1)) {
		if (allowedOptions & ~((bit << 1) - 1)) {
		    Tcl_AppendToObj(msgObj, ", ", TCL_INDEX_NONE);
		} else {
		    Tcl_AppendToObj(msgObj, ", or ", TCL_INDEX_NONE);
		}
	    }
	    Tcl_AppendToObj(msgObj, *listPtr, TCL_INDEX_NONE);
	}
	bit <<= 1;
    }
    Tcl_SetObjResult(interp, msgObj);
    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "BAD_OPTION", (char *)NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * ImgPhotoConfigureModel --
 *
 *	This function is called when a photo image is created or reconfigured.
 *	It processes configuration options and resets any instances of the
 *	image.
 *
 * Results:
 *	A standard Tcl return value. If TCL_ERROR is returned then an error
 *	message is left in the modelPtr->interp's result.
 *
 * Side effects:
 *	Existing instances of the image will be redisplayed to match the new
 *	configuration options.
 *
 *----------------------------------------------------------------------
 */

static int
ImgPhotoConfigureModel(
    Tcl_Interp *interp,		/* Interpreter to use for reporting errors. */
    PhotoModel *modelPtr,	/* Pointer to data structure describing
				 * overall photo image to (re)configure. */
    Tcl_Size objc,			/* Number of entries in objv. */
    Tcl_Obj *const objv[],	/* Pairs of configuration options for image. */
    int flags)			/* Flags to pass to Tk_ConfigureWidget, such
				 * as TK_CONFIG_ARGV_ONLY. */
{
    PhotoInstance *instancePtr;
    Tcl_Obj *oldFileObj;
    const char *oldPaletteString;
    Tcl_Obj *oldData, *data = NULL, *oldFormat, *format = NULL,
	    *metadataInObj = NULL, *metadataOutObj = NULL;
    Tcl_Obj *tempdata, *tempformat;
    Tcl_Size i, length;
    int result, imageWidth, imageHeight, oldformat;
    double oldGamma;
    Tcl_Channel chan;
    Tk_PhotoImageFormat *imageFormat;
    Tk_PhotoImageFormatVersion3 *imageFormatVersion3;

    for (i = 0; i < objc; i++) {
	const char *arg = Tcl_GetStringFromObj(objv[i], &length);
	if ((length > 1) && (arg[0] == '-')) {
	    if ((arg[1] == 'd') &&
		    !strncmp(arg, "-data", length)) {
		if (++i < objc) {
		    data = objv[i];
		} else {
		    Tcl_SetObjResult(interp, Tcl_NewStringObj(
			    "value for \"-data\" missing", TCL_INDEX_NONE));
		    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
			    "MISSING_VALUE", (char *)NULL);
		    return TCL_ERROR;
		}
	    } else if ((arg[1] == 'f') &&
		    !strncmp(arg, "-format", length)) {
		if (++i < objc) {
		    format = objv[i];
		} else {
		    Tcl_SetObjResult(interp, Tcl_NewStringObj(
			    "value for \"-format\" missing", TCL_INDEX_NONE));
		    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
			    "MISSING_VALUE", (char *)NULL);
		    return TCL_ERROR;
		}
	    } else if ((arg[1] == 'm') &&
		!strncmp(arg, "-metadata", length)) {
		if (++i < objc) {
		    metadataInObj = objv[i];
		} else {
		    Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"value for \"-metadata\" missing", TCL_INDEX_NONE));
		    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
			"MISSING_VALUE", (char *)NULL);
		    return TCL_ERROR;
		}
	    }
	}
    }

    /*
     * Save the current values for fileString and dataString, so we can tell
     * if the user specifies them anew. IMPORTANT: if the format changes we
     * have to interpret "-file" and "-data" again as well! It might be that
     * the format string influences how "-data" or "-file" is interpreted.
     */

    oldFileObj = modelPtr->fileObj;
    if (oldFileObj == NULL) {
	oldData = modelPtr->dataObj;
	if (oldData != NULL) {
	    Tcl_IncrRefCount(oldData);
	}
    } else {
	oldData = NULL;
    }
    oldFormat = modelPtr->format;
    if (oldFormat != NULL) {
	Tcl_IncrRefCount(oldFormat);
    }
    oldPaletteString = modelPtr->palette;
    oldGamma = modelPtr->gamma;

    /*
     * Process the configuration options specified.
     */

    if (Tk_ConfigureWidget(interp, Tk_MainWindow(interp), configSpecs,
	    objc, objv, modelPtr, flags) != TCL_OK) {
	goto errorExit;
    }

    /*
     * Regard the empty string for -file, -data, -format or -metadata as the null value.
     */

    if ((modelPtr->fileObj != NULL) && (Tcl_GetString(modelPtr->fileObj)[0] == 0)) {
	Tcl_DecrRefCount(modelPtr->fileObj);
	modelPtr->fileObj = NULL;
    }
    if (data) {
	/*
	 * Force into ByteArray format, which most (all) image handlers will
	 * use anyway. Empty length means ignore the -data option.
	 */
	Tcl_Size bytesize;

	(void) Tcl_GetByteArrayFromObj(data, &bytesize);
	if (bytesize) {
	    Tcl_IncrRefCount(data);
	} else {
	    data = NULL;
	}
	if (modelPtr->dataObj) {
	    Tcl_DecrRefCount(modelPtr->dataObj);
	}
	modelPtr->dataObj = data;
    }
    if (format) {
	/*
	 * Stringify to ignore -format "". It may come in as a list or other
	 * object.
	 */

	(void) Tcl_GetString(format);
	if (format->length) {
	    Tcl_IncrRefCount(format);
	} else {
	    format = NULL;
	}
	if (modelPtr->format) {
	    Tcl_DecrRefCount(modelPtr->format);
	}
	modelPtr->format = format;
    }
    if (metadataInObj) {
	/*
	 * Make -metadata a dict.
	 * Take also empty metadatas as this may be a sign to replace
	 * existing metadata.
	 */
	Tcl_Size dictSize;

	if (TCL_OK != Tcl_DictObjSize(interp,metadataInObj, &dictSize)) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "value for \"-metadata\" not a dict", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
		    "UNRECOGNIZED_DATA", (char *)NULL);
	    return TCL_ERROR;
	}

	if (dictSize > 0) {
	    Tcl_IncrRefCount(metadataInObj);
	} else {
	    metadataInObj = NULL;
	}
	if (modelPtr->metadata) {
	    Tcl_DecrRefCount(modelPtr->metadata);
	}
	modelPtr->metadata = metadataInObj;
    }
    /*
     * Set the image to the user-requested size, if any, and make sure storage
     * is correctly allocated for this image.
     */

    if (ImgPhotoSetSize(modelPtr, modelPtr->width,
	    modelPtr->height) != TCL_OK) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		TK_PHOTO_ALLOC_FAILURE_MESSAGE, TCL_INDEX_NONE));
	Tcl_SetErrorCode(interp, "TK", "MALLOC", (char *)NULL);
	goto errorExit;
    }

    /*
     * Read in the image from the file or string if the user has specified the
     * -file or -data option.
     */

    if ((modelPtr->fileObj != NULL)
	    && ((modelPtr->fileObj != oldFileObj)
	    || (modelPtr->format != oldFormat))) {
	/*
	 * Prevent file system access in a safe interpreter.
	 */

	if (Tcl_IsSafe(interp)) {
	    Tcl_ResetResult(interp);
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "can't get image from a file in a safe interpreter",
		    -1));
	    Tcl_SetErrorCode(interp, "TK", "SAFE", "PHOTO_FILE", (char *)NULL);
	    goto errorExit;
	}

	chan = Tcl_OpenFileChannel(interp, Tcl_GetString(modelPtr->fileObj), "rb", 0);
	if (chan == NULL) {
	    goto errorExit;
	}

	/*
	 * Flag that we want the metadata result dict
	 */

	metadataOutObj = Tcl_NewDictObj();
	Tcl_IncrRefCount(metadataOutObj);

	if ((MatchFileFormat(interp, chan, (modelPtr->fileObj ? Tcl_GetString(modelPtr->fileObj) : NULL),
			modelPtr->format, modelPtr->metadata, metadataOutObj,
			&imageFormat, &imageFormatVersion3,
			&imageWidth, &imageHeight, &oldformat) != TCL_OK)) {
	    Tcl_Close(NULL, chan);
	    goto errorExit;
	}
	result = ImgPhotoSetSize(modelPtr, imageWidth, imageHeight);
	if (result != TCL_OK) {
	    Tcl_Close(NULL, chan);
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    TK_PHOTO_ALLOC_FAILURE_MESSAGE, TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "MALLOC", (char *)NULL);
	    goto errorExit;
	}
	tempformat = modelPtr->format;
	if (oldformat && tempformat) {
	    tempformat = (Tcl_Obj *) Tcl_GetString(tempformat);
	}
	if (imageFormat != NULL) {
	    result = imageFormat->fileReadProc(interp, chan,
		    (modelPtr->fileObj ? Tcl_GetString(modelPtr->fileObj) : NULL), tempformat,
		    (Tk_PhotoHandle) modelPtr,
		    0, 0, imageWidth, imageHeight, 0, 0);
	} else {
	    result = imageFormatVersion3->fileReadProc(interp, chan,
		    (modelPtr->fileObj ? Tcl_GetString(modelPtr->fileObj) : NULL), tempformat, modelPtr->metadata,
		    (Tk_PhotoHandle) modelPtr,
		    0, 0, imageWidth, imageHeight, 0, 0,
		    metadataOutObj);
	}

	Tcl_Close(NULL, chan);
	if (result != TCL_OK) {
	    goto errorExit;
	}

	Tcl_ResetResult(interp);
	modelPtr->flags |= IMAGE_CHANGED;
    }

    if ((modelPtr->fileObj == NULL) && (modelPtr->dataObj != NULL)
	    && ((modelPtr->dataObj != oldData)
		    || (modelPtr->format != oldFormat))) {

	/*
	 * Flag that we want the metadata result dict
	 */

	metadataOutObj = Tcl_NewDictObj();
	Tcl_IncrRefCount(metadataOutObj);

	if (MatchStringFormat(interp, modelPtr->dataObj,
		modelPtr->format, modelPtr->metadata, metadataOutObj,
		&imageFormat, &imageFormatVersion3, &imageWidth,
		&imageHeight, &oldformat) != TCL_OK) {
	    goto errorExit;
	}
	if (ImgPhotoSetSize(modelPtr, imageWidth, imageHeight) != TCL_OK) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    TK_PHOTO_ALLOC_FAILURE_MESSAGE, TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "MALLOC", (char *)NULL);
	    goto errorExit;
	}
	tempformat = modelPtr->format;
	tempdata = modelPtr->dataObj;
	if (oldformat) {
	    if (tempformat) {
		tempformat = (Tcl_Obj *) Tcl_GetString(tempformat);
	    }
	    tempdata = (Tcl_Obj *) Tcl_GetString(tempdata);
	}
	if (imageFormat != NULL) {
	    if (imageFormat->stringReadProc(interp, tempdata, tempformat,
		    (Tk_PhotoHandle) modelPtr, 0, 0, imageWidth, imageHeight,
		    0, 0) != TCL_OK) {
		goto errorExit;
	    }
	} else {
	    if (imageFormatVersion3->stringReadProc(interp, tempdata, tempformat,
		    modelPtr->metadata, (Tk_PhotoHandle) modelPtr, 0, 0,
		    imageWidth, imageHeight, 0, 0, metadataOutObj) != TCL_OK) {
		goto errorExit;
	    }
	}

	Tcl_ResetResult(interp);
	modelPtr->flags |= IMAGE_CHANGED;
    }

    /*
     * Merge driver returned metadata and master metadata
     */
    if (metadataOutObj != NULL) {
	Tcl_Size dictSize;
	if (TCL_OK != Tcl_DictObjSize(interp,metadataOutObj, &dictSize)) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "driver metadata not a dict", TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
		    "UNRECOGNIZED_DATA", (char *)NULL);
	    goto errorExit;
	}
	if (dictSize > 0) {

	    /*
	     * We have driver return metadata
	     */

	    if (modelPtr->metadata == NULL) {
		modelPtr->metadata = metadataOutObj;
		metadataOutObj = NULL;
	    } else {
		Tcl_DictSearch search;
		Tcl_Obj *key, *value;
		int done;

		if (Tcl_IsShared(modelPtr->metadata)) {
		    Tcl_DecrRefCount(modelPtr->metadata);
		    modelPtr->metadata = Tcl_DuplicateObj(modelPtr->metadata);
		    Tcl_IncrRefCount(modelPtr->metadata);
		}

		if (Tcl_DictObjFirst(interp, metadataOutObj, &search, &key,
			&value, &done) != TCL_OK) {
		    goto errorExit;
		}
		for (; !done ; Tcl_DictObjNext(&search, &key, &value, &done)) {
		    Tcl_DictObjPut(interp, modelPtr->metadata, key, value);
		}
	    }
	}
    }

    /*
     * Enforce a reasonable value for gamma.
     */

    if (modelPtr->gamma <= 0) {
	modelPtr->gamma = 1.0;
    }

    if ((modelPtr->gamma != oldGamma)
	    || (modelPtr->palette != oldPaletteString)) {
	modelPtr->flags |= IMAGE_CHANGED;
    }

    /*
     * Cycle through all of the instances of this image, regenerating the
     * information for each instance. Then force the image to be redisplayed
     * everywhere that it is used.
     */

    for (instancePtr = modelPtr->instancePtr; instancePtr != NULL;
	    instancePtr = instancePtr->nextPtr) {
	TkImgPhotoConfigureInstance(instancePtr);
    }

    /*
     * Inform the generic image code that the image has (potentially) changed.
     */

    Tk_ImageChanged(modelPtr->tkModel, 0, 0, modelPtr->width,
	    modelPtr->height, modelPtr->width, modelPtr->height);
    modelPtr->flags &= ~IMAGE_CHANGED;

    if (oldData != NULL) {
	Tcl_DecrRefCount(oldData);
    }
    if (oldFormat != NULL) {
	Tcl_DecrRefCount(oldFormat);
    }
    if (metadataOutObj != NULL) {
	Tcl_DecrRefCount(metadataOutObj);
    }

    ToggleComplexAlphaIfNeeded(modelPtr);

    return TCL_OK;

  errorExit:
    if (oldData != NULL) {
	Tcl_DecrRefCount(oldData);
    }
    if (oldFormat != NULL) {
	Tcl_DecrRefCount(oldFormat);
    }
    if (metadataOutObj != NULL) {
	Tcl_DecrRefCount(metadataOutObj);
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * ToggleComplexAlphaIfNeeded --
 *
 *	This function is called when an image is modified to check if any
 *	partially transparent pixels exist, which requires blending instead of
 *	straight copy.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	(Re)sets COMPLEX_ALPHA flag of model.
 *
 *----------------------------------------------------------------------
 */

static int
ToggleComplexAlphaIfNeeded(
    PhotoModel *mPtr)
{
    size_t len = (size_t)MAX(mPtr->userWidth, mPtr->width) *
	    (size_t)MAX(mPtr->userHeight, mPtr->height) * 4;
    unsigned char *c = mPtr->pix32;
    unsigned char *end;

    /*
     * Set the COMPLEX_ALPHA flag if we have an image with partially
     * transparent bits.
     */

    mPtr->flags &= ~COMPLEX_ALPHA;
    if (c == NULL) {
	return 0;
    }
    end = c + len;
    c += 3;			/* Start at first alpha byte. */
    for (; c < end; c += 4) {
	if (*c && *c != 255) {
	    mPtr->flags |= COMPLEX_ALPHA;
	    break;
	}
    }
    return (mPtr->flags & COMPLEX_ALPHA);
}

/*
 *----------------------------------------------------------------------
 *
 * ImgPhotoDelete --
 *
 *	This function is called by the image code to delete the model
 *	structure for an image.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Resources associated with the image get freed.
 *
 *----------------------------------------------------------------------
 */

static void
ImgPhotoDelete(
    void *modelData)	/* Pointer to PhotoModel structure for image.
				 * Must not have any more instances. */
{
    PhotoModel *modelPtr = (PhotoModel *)modelData;
    PhotoInstance *instancePtr;

    while ((instancePtr = modelPtr->instancePtr) != NULL) {
	if (instancePtr->refCount > 0) {
	    Tcl_Panic("tried to delete photo image when instances still exist");
	}
	Tcl_CancelIdleCall(TkImgDisposeInstance, instancePtr);
	TkImgDisposeInstance(instancePtr);
    }
    modelPtr->tkModel = NULL;
    if (modelPtr->imageCmd != NULL) {
	Tcl_DeleteCommandFromToken(modelPtr->interp, modelPtr->imageCmd);
    }
    if (modelPtr->pix32 != NULL) {
	ckfree(modelPtr->pix32);
    }
    if (modelPtr->validRegion != NULL) {
	TkDestroyRegion(modelPtr->validRegion);
    }
    if (modelPtr->dataObj != NULL) {
	Tcl_DecrRefCount(modelPtr->dataObj);
    }
    if (modelPtr->format != NULL) {
	Tcl_DecrRefCount(modelPtr->format);
    }
    if (modelPtr->metadata != NULL) {
	Tcl_DecrRefCount(modelPtr->metadata);
    }
    Tk_FreeOptions(configSpecs, modelPtr, NULL, 0);
    ckfree(modelPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * ImgPhotoCmdDeletedProc --
 *
 *	This function is invoked when the image command for an image is
 *	deleted. It deletes the image.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The image is deleted.
 *
 *----------------------------------------------------------------------
 */

static void
ImgPhotoCmdDeletedProc(
    void *clientData)	/* Pointer to PhotoModel structure for
				 * image. */
{
    PhotoModel *modelPtr = (PhotoModel *)clientData;

    modelPtr->imageCmd = NULL;
    if (modelPtr->tkModel != NULL) {
	Tk_DeleteImage(modelPtr->interp, Tk_NameOfImage(modelPtr->tkModel));
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ImgPhotoSetSize --
 *
 *	This function reallocates the image storage and instance pixmaps for a
 *	photo image, as necessary, to change the image's size to `width' x
 *	`height' pixels.
 *
 * Results:
 *	TCL_OK if successful, TCL_ERROR if failure occurred (currently just
 *	with memory allocation.)
 *
 * Side effects:
 *	Storage gets reallocated, for the model and all its instances.
 *
 *----------------------------------------------------------------------
 */

static int
ImgPhotoSetSize(
    PhotoModel *modelPtr,
    int width, int height)
{
    unsigned char *newPix32 = NULL;
    int h, offset, pitch;
    unsigned char *srcPtr, *destPtr;
    XRectangle validBox, clipBox;
    TkRegion clipRegion;
    PhotoInstance *instancePtr;

    if (modelPtr->userWidth > 0) {
	width = modelPtr->userWidth;
    }
    if (modelPtr->userHeight > 0) {
	height = modelPtr->userHeight;
    }

    if (width > INT_MAX / 4) {
	/* Pitch overflows int */
	return TCL_ERROR;
    }
    pitch = width * 4;

    /*
     * Test if we're going to (re)allocate the main buffer now, so that any
     * failures will leave the photo unchanged.
     */

    if ((width != modelPtr->width) || (height != modelPtr->height)
	    || (modelPtr->pix32 == NULL)) {
	unsigned newPixSize;

	if (pitch && height > (int)(UINT_MAX / pitch)) {
	    return TCL_ERROR;
	}
	newPixSize = height * pitch;

	/*
	 * Some mallocs() really hate allocating zero bytes. [Bug 619544]
	 */

	if (newPixSize == 0) {
	    newPix32 = NULL;
	} else {
	    newPix32 = (unsigned char *)attemptckalloc(newPixSize);
	    if (newPix32 == NULL) {
		return TCL_ERROR;
	    }
	}
    }

    /*
     * We have to trim the valid region if it is currently larger than the new
     * image size.
     */

    TkClipBox(modelPtr->validRegion, &validBox);
    if ((validBox.x + validBox.width > width)
	    || (validBox.y + validBox.height > height)) {
	clipBox.x = 0;
	clipBox.y = 0;
	clipBox.width = width;
	clipBox.height = height;
	clipRegion = TkCreateRegion();
	TkUnionRectWithRegion(&clipBox, clipRegion, clipRegion);
	TkIntersectRegion(modelPtr->validRegion, clipRegion,
		modelPtr->validRegion);
	TkDestroyRegion(clipRegion);
	TkClipBox(modelPtr->validRegion, &validBox);
    }

    /*
     * Use the reallocated storage (allocation above) for the 32-bit image and
     * copy over valid regions. Note that this test is true precisely when the
     * allocation has already been done.
     */

    if (newPix32 != NULL) {
	/*
	 * Zero the new array. The dithering code shouldn't read the areas
	 * outside validBox, but they might be copied to another photo image
	 * or written to a file.
	 */

	if ((modelPtr->pix32 != NULL)
	    && ((width == modelPtr->width) || (width == validBox.width))) {
	    if (validBox.y > 0) {
		memset(newPix32, 0, ((size_t) validBox.y * pitch));
	    }
	    h = validBox.y + validBox.height;
	    if (h < height) {
		memset(newPix32 + h*pitch, 0, ((size_t) (height - h) * pitch));
	    }
	} else {
	    memset(newPix32, 0, ((size_t)height * pitch));
	}

	if (modelPtr->pix32 != NULL) {
	    /*
	     * Copy the common area over to the new array array and free the
	     * old array.
	     */

	    if (width == modelPtr->width) {

		/*
		 * The region to be copied is contiguous.
		 */

		offset = validBox.y * pitch;
		memcpy(newPix32 + offset, modelPtr->pix32 + offset,
			((size_t)validBox.height * pitch));

	    } else if ((validBox.width > 0) && (validBox.height > 0)) {
		/*
		 * Area to be copied is not contiguous - copy line by line.
		 */

		destPtr = newPix32 + (validBox.y * width + validBox.x) * 4;
		srcPtr = modelPtr->pix32 + (validBox.y * modelPtr->width
			+ validBox.x) * 4;
		for (h = validBox.height; h > 0; h--) {
		    memcpy(destPtr, srcPtr, ((size_t)validBox.width * 4));
		    destPtr += width * 4;
		    srcPtr += modelPtr->width * 4;
		}
	    }

	    ckfree(modelPtr->pix32);
	}

	modelPtr->pix32 = newPix32;
	modelPtr->width = width;
	modelPtr->height = height;

	/*
	 * Dithering will be correct up to the end of the last pre-existing
	 * complete scanline.
	 */

	if ((validBox.x > 0) || (validBox.y > 0)) {
	    modelPtr->ditherX = 0;
	    modelPtr->ditherY = 0;
	} else if (validBox.width == width) {
	    if ((int) validBox.height < modelPtr->ditherY) {
		modelPtr->ditherX = 0;
		modelPtr->ditherY = validBox.height;
	    }
	} else if ((modelPtr->ditherY > 0)
		|| ((int) validBox.width < modelPtr->ditherX)) {
	    modelPtr->ditherX = validBox.width;
	    modelPtr->ditherY = 0;
	}
    }

    ToggleComplexAlphaIfNeeded(modelPtr);

    /*
     * Now adjust the sizes of the pixmaps for all of the instances.
     */

    for (instancePtr = modelPtr->instancePtr; instancePtr != NULL;
	    instancePtr = instancePtr->nextPtr) {
	TkImgPhotoInstanceSetSize(instancePtr);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * MatchFileFormat --
 *
 *	This function is called to find a photo image file format handler
 *	which can parse the image data in the given file. If a user-specified
 *	format string is provided, only handlers whose names match a prefix of
 *	the format string are tried.
 *
 * Results:
 *	A standard TCL return value. If the return value is TCL_OK, a pointer
 *	to the image format record is returned in *imageFormatPtr or
 *	*imageFormatVersion3Ptr, and the width and height of the image are
 *	returned in *widthPtr and *heightPtr.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
MatchFileFormat(
    Tcl_Interp *interp,		/* Interpreter to use for reporting errors. */
    Tcl_Channel chan,		/* The image file, open for reading. */
    const char *fileName,	/* The name of the image file. */
    Tcl_Obj *formatObj,		/* User-specified format string, or NULL. */
    Tcl_Obj *metadataInObj,	/* User-specified metadata, may be NULL */
    Tcl_Obj *metadataOutObj,	/* metadata to return, may be NULL */
    Tk_PhotoImageFormat **imageFormatPtr,
				/* A pointer to the photo image format record
				 * is returned here. For formatVersion3, this is
				 * set to NULL */
    Tk_PhotoImageFormatVersion3 **imageFormatVersion3Ptr,
				/* A pointer to the photo image formatVersion3
				 * record is returned here. For non
				 * formatVersion3, this is set to NULL*/
    int *widthPtr, int *heightPtr,
				/* The dimensions of the image are returned
				 * here. */
    int *oldformat)		/* Returns 1 if the old image API is used. */
{
    int matched = 0;
    int useoldformat = 0;
    Tk_PhotoImageFormat *formatPtr;
    Tk_PhotoImageFormatVersion3 *formatVersion3Ptr;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    const char *formatString = NULL;

    if (formatObj) {
	formatString = Tcl_GetString(formatObj);
    }

    /*
     * Scan through the table of file format handlers to find one which can
     * handle the image.
     */

    for (formatPtr = tsdPtr->formatList; formatPtr != NULL;
	    formatPtr = formatPtr->nextPtr) {
	if (formatObj != NULL) {
	    if (strncasecmp(formatString,
		    formatPtr->name, strlen(formatPtr->name)) != 0) {
		continue;
	    }
	    matched = 1;
	    if (formatPtr->fileMatchProc == NULL) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"-file option isn't supported for %s images",
			formatString));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
			"NOT_FILE_FORMAT", (char *)NULL);
		return TCL_ERROR;
	    }
	}
	if (formatPtr->fileMatchProc != NULL) {
	    (void) Tcl_Seek(chan, Tcl_LongAsWide(0L), SEEK_SET);

	    if (formatPtr->fileMatchProc(chan, fileName, formatObj,
		    widthPtr, heightPtr, interp)) {
		if (*widthPtr < 1) {
		    *widthPtr = 1;
		}
		if (*heightPtr < 1) {
		    *heightPtr = 1;
		}
		break;
	    }
	}
    }

    /*
     * For old and not version 3 format, exit now with success
     */

    if (formatPtr != NULL) {
	*imageFormatPtr = formatPtr;
	*imageFormatVersion3Ptr = NULL;
	*oldformat = useoldformat;
	(void) Tcl_Seek(chan, Tcl_LongAsWide(0L), SEEK_SET);
	return TCL_OK;
    }

    /*
     * Scan through the table of file format version 3 handlers to find one
     * which can handle the image.
     */

    for (formatVersion3Ptr = tsdPtr->formatListVersion3;
	    formatVersion3Ptr != NULL;
	    formatVersion3Ptr = formatVersion3Ptr->nextPtr) {
	if (formatObj != NULL) {
	    if (strncasecmp(formatString,
		    formatVersion3Ptr->name, strlen(formatVersion3Ptr->name))
		    != 0) {
		continue;
	    }
	    matched = 1;
	    if (formatVersion3Ptr->fileMatchProc == NULL) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"-file option isn't supported for %s images",
			formatString));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
			"NOT_FILE_FORMAT", (char *)NULL);
		return TCL_ERROR;
	    }
	}
	if (formatVersion3Ptr->fileMatchProc != NULL) {
	    (void) Tcl_Seek(chan, Tcl_LongAsWide(0L), SEEK_SET);

	    if (formatVersion3Ptr->fileMatchProc(interp, chan, fileName,
		    formatObj, metadataInObj, widthPtr, heightPtr,
		    metadataOutObj)) {
		if (*widthPtr < 1) {
		    *widthPtr = 1;
		}
		if (*heightPtr < 1) {
		    *heightPtr = 1;
		}
		*imageFormatVersion3Ptr = formatVersion3Ptr;
		*imageFormatPtr = NULL;
		*oldformat = 0;
		(void) Tcl_Seek(chan, Tcl_LongAsWide(0L), SEEK_SET);
		return TCL_OK;
	    }

	    /*
	     * Check if driver has shared or changed the metadata Tcl object.
	     * In this case, release and recreate it.
	     */

	    if (metadataOutObj != NULL) {
		Tcl_Size dictSize;
		if (Tcl_IsShared(metadataOutObj)
			|| TCL_OK != Tcl_DictObjSize(interp,metadataOutObj, &dictSize)
			|| dictSize > 0) {
		    Tcl_DecrRefCount(metadataOutObj);
		    metadataOutObj = Tcl_NewDictObj();
		    Tcl_IncrRefCount(metadataOutObj);
		}
	    }
	}
    }

    /*
     * No matching format found
     */

    if ((formatObj != NULL) && !matched) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"image file format \"%s\" is not supported",
		formatString));
	Tcl_SetErrorCode(interp, "TK", "LOOKUP", "PHOTO_FORMAT",
		formatString, (char *)NULL);
    } else {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"couldn't recognize data in image file \"%s\"",
		fileName));
	Tcl_SetErrorCode(interp, "TK", "PHOTO", "IMAGE",
		"UNRECOGNIZED_DATA", (char *)NULL);
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * MatchStringFormat --
 *
 *	This function is called to find a photo image file format handler
 *	which can parse the image data in the given string. If a
 *	user-specified format string is provided, only handlers whose names
 *	match a prefix of the format string are tried.
 *
 * Results:
 *	A standard TCL return value. If the return value is TCL_OK, a pointer
 *	to the image format record is returned in *imageFormatPtr or
 *	*imageFormatVersion3Ptr, and the width and height of the image are
 *	returned in *widthPtr and *heightPtr.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
MatchStringFormat(
    Tcl_Interp *interp,		/* Interpreter to use for reporting errors. */
    Tcl_Obj *data,		/* Object containing the image data. */
    Tcl_Obj *formatObj,		/* User-specified format string, or NULL. */
    Tcl_Obj *metadataInObj,	/* User-specified metadata, may be NULL */
    Tcl_Obj *metadataOutObj,	/* metadata output dict, may be NULL */
    Tk_PhotoImageFormat **imageFormatPtr,
				/* A pointer to the photo image format record
				 * is returned here. For formatVersion3, this is
				 * set to NULL*/
    Tk_PhotoImageFormatVersion3 **imageFormatVersion3Ptr,
				/* A pointer to the photo image formatVersion3
				 * record is returned here. For non
				 * formatVersion3, this is set to NULL*/
    int *widthPtr, int *heightPtr,
				/* The dimensions of the image are returned
				 * here. */
    int *oldformat)		/* Returns 1 if the old image API is used. */
{
    int matched = 0, useoldformat = 0;
    Tk_PhotoImageFormat *formatPtr, *defaultFormatPtr = NULL;
    Tk_PhotoImageFormatVersion3 *formatVersion3Ptr = NULL;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	    Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    const char *formatString = NULL;

    if (formatObj) {
	formatString = Tcl_GetString(formatObj);
    }

    /*
     * Scan through the table of file format handlers to find one which can
     * handle the image.
     */

    for (formatPtr = tsdPtr->formatList; formatPtr != NULL;
	    formatPtr = formatPtr->nextPtr) {
	/*
	 * To keep the behaviour of older versions (Tk <= 8.6), the default
	 * list-of-lists string format is checked last. Remember its position.
	 */

	if (strncasecmp("default", formatPtr->name, strlen(formatPtr->name))
		== 0) {
	    defaultFormatPtr = formatPtr;
	}

	if (formatObj != NULL) {
	    if (strncasecmp(formatString,
		    formatPtr->name, strlen(formatPtr->name)) != 0) {
		continue;
	    }
	    matched = 1;
	    if (formatPtr->stringMatchProc == NULL) {
		Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			"-data option isn't supported for %s images",
			formatString));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
			"NOT_DATA_FORMAT", (char *)NULL);
		return TCL_ERROR;
	    }
	}

	/*
	 * If this is the default format, and it was not passed as -format
	 * option, skip the stringMatchProc test. It'll be done later
	 */

	if (formatObj == NULL && formatPtr == defaultFormatPtr) {
	    continue;
	}

	if ((formatPtr->stringMatchProc != NULL)
		&& (formatPtr->stringReadProc != NULL)
		&& formatPtr->stringMatchProc(data, formatObj,
			widthPtr, heightPtr, interp)) {
	    break;
	}
    }

    if (formatPtr == NULL) {
	useoldformat = 0;
	for (formatVersion3Ptr = tsdPtr->formatListVersion3;
		formatVersion3Ptr != NULL;
		formatVersion3Ptr = formatVersion3Ptr->nextPtr) {
	    if (formatObj != NULL) {
		if (strncasecmp(formatString,
			formatVersion3Ptr->name, strlen(formatVersion3Ptr->name)
			) != 0) {
		    continue;
		}
		matched = 1;
		if (formatVersion3Ptr->stringMatchProc == NULL) {
		    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
			    "-data option isn't supported for %s images",
			    formatString));
		    Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
			    "NOT_DATA_FORMAT", (char *)NULL);
		    return TCL_ERROR;
		}
	    }
	    if ((formatVersion3Ptr->stringMatchProc != NULL)
		    && (formatVersion3Ptr->stringReadProc != NULL)
		    && formatVersion3Ptr->stringMatchProc(interp, data,
			    formatObj, metadataInObj, widthPtr, heightPtr,
			    metadataOutObj)) {
		break;
	    }

	    /*
	     * Check if driver has shared or changed the metadata tcl object.
	     * In this case, release and recreate it.
	     */

	    if (metadataOutObj != NULL) {
		Tcl_Size dictSize;
		if (Tcl_IsShared(metadataOutObj)
			|| TCL_OK != Tcl_DictObjSize(interp,metadataOutObj, &dictSize)
			|| dictSize > 0) {
		    Tcl_DecrRefCount(metadataOutObj);
		    metadataOutObj = Tcl_NewDictObj();
		    Tcl_IncrRefCount(metadataOutObj);
		}
	    }
	}
    }

    if (formatPtr == NULL && formatVersion3Ptr == NULL) {
	/*
	 * Try the default format as last resort (only if no -format option
	 * was passed).
	 */

	if ( formatObj == NULL && defaultFormatPtr == NULL) {
	    Tcl_Panic("default image format handler not registered");
	}
	if ( formatObj == NULL
		&& defaultFormatPtr->stringMatchProc != NULL
		&& defaultFormatPtr->stringReadProc != NULL
		&& defaultFormatPtr->stringMatchProc(data, formatObj,
		widthPtr, heightPtr, interp) != 0) {
	    useoldformat = 0;
	    formatPtr = defaultFormatPtr;
	} else if ((formatObj != NULL) && !matched) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "image format \"%s\" is not supported", formatString));
	    Tcl_SetErrorCode(interp, "TK", "LOOKUP", "PHOTO_FORMAT",
		    formatString, (char *)NULL);
	    return TCL_ERROR;
	} else {

	    /*
	     * Some lower level routine (stringMatchProc) may have already set
	     * a specific error message, so just return this. Otherwise return
	     * a generic image data error.
	     */

	    if (Tcl_GetString(Tcl_GetObjResult(interp))[0] == '\0') {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			"couldn't recognize image data", TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
			"UNRECOGNIZED_DATA", (char *)NULL);
	    }
	    return TCL_ERROR;
	}
    }

    *imageFormatPtr = formatPtr;
    *imageFormatVersion3Ptr = formatVersion3Ptr;
    *oldformat = useoldformat;

    /*
     * Some stringMatchProc might have left error messages and error codes in
     * interp.	Clear them before return.
     */
    Tcl_ResetResult(interp);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_FindPhoto --
 *
 *	This function is called to get an opaque handle (actually a
 *	PhotoModel *) for a given image, which can be used in subsequent
 *	calls to Tk_PhotoPutBlock, etc. The `name' parameter is the name of
 *	the image.
 *
 * Results:
 *	The handle for the photo image, or NULL if there is no photo image
 *	with the name given.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tk_PhotoHandle
Tk_FindPhoto(
    Tcl_Interp *interp,		/* Interpreter (application) in which image
				 * exists. */
    const char *imageName)	/* Name of the desired photo image. */
{
    const Tk_ImageType *typePtr;
    void *clientData =
	    Tk_GetImageModelData(interp, imageName, &typePtr);

    if ((typePtr == NULL) || (typePtr->name != tkPhotoImageType.name)) {
	return NULL;
    }
    return clientData;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_PhotoPutBlock --
 *
 *	This function is called to put image data into a photo image.
 *
 * Results:
 *	A standard Tcl result code.
 *
 * Side effects:
 *	The image data is stored. The image may be expanded. The Tk image code
 *	is informed that the image has changed. If the result code is
 *	TCL_ERROR, an error message will be placed in the interpreter (if
 *	non-NULL).
 *
 *----------------------------------------------------------------------
 */

int
Tk_PhotoPutBlock(
    Tcl_Interp *interp,		/* Interpreter for passing back error
				 * messages, or NULL. */
    Tk_PhotoHandle handle,	/* Opaque handle for the photo image to be
				 * updated. */
    Tk_PhotoImageBlock *blockPtr,
				/* Pointer to a structure describing the pixel
				 * data to be copied into the image. */
    int x, int y,		/* Coordinates of the top-left pixel to be
				 * updated in the image. */
    int width, int height,	/* Dimensions of the area of the image to be
				 * updated. */
    int compRule)		/* Compositing rule to use when processing
				 * transparent pixels. */
{
    PhotoModel *modelPtr = (PhotoModel *) handle;
    Tk_PhotoImageBlock sourceBlock;
    unsigned char *memToFree;
    int xEnd, yEnd, greenOffset, blueOffset, alphaOffset;
    int wLeft, hLeft, wCopy, hCopy, pitch;
    unsigned char *srcPtr, *srcLinePtr, *destPtr, *destLinePtr;
    int sourceIsSimplePhoto = compRule & SOURCE_IS_SIMPLE_ALPHA_PHOTO;
    XRectangle rect;

    /*
     * Zero-sized blocks never cause any changes. [Bug 3078902]
     */

    if (blockPtr->height == 0 || blockPtr->width == 0) {
	return TCL_OK;
    }

    compRule &= ~SOURCE_IS_SIMPLE_ALPHA_PHOTO;

    if ((modelPtr->userWidth != 0) && ((x + width) > modelPtr->userWidth)) {
	width = modelPtr->userWidth - x;
    }
    if ((modelPtr->userHeight != 0)
	    && ((y + height) > modelPtr->userHeight)) {
	height = modelPtr->userHeight - y;
    }
    if ((width <= 0) || (height <= 0)) {
	return TCL_OK;
    }

    /*
     * Fix for bug e4336bef5d:
     *
     * Make a local copy of *blockPtr, as we might have to change some
     * of its fields and don't want to interfere with the caller's data.
     *
     * If source and destination are the same image, create a copy  of the
     * source data in our local sourceBlock.
     *
     * To find out, just comparing the pointers is not enough - they might have
     * different values and still point to the same block of memory. (e.g.
     * if the -from option was passed to [imageName copy])
     */
    sourceBlock = *blockPtr;
    memToFree = NULL;
    if (modelPtr->pix32 && (sourceBlock.pixelPtr >= modelPtr->pix32)
	    && (sourceBlock.pixelPtr < modelPtr->pix32 + modelPtr->width
	    * modelPtr->height * 4)) {
	/*
	 * Fix 5c51be6411: avoid reading
	 *
	 *	(sourceBlock.pitch - sourceBlock.width * sourceBlock.pixelSize)
	 *
	 * bytes past the end of modelPtr->pix32[] when
	 *
	 *	blockPtr->pixelPtr > (modelPtr->pix32 +
	 *		4 * modelPtr->width * modelPtr->height -
	 *		sourceBlock.height * sourceBlock.pitch)
	 */
	unsigned int cpyLen = (sourceBlock.height - 1) * sourceBlock.pitch +
		sourceBlock.width * sourceBlock.pixelSize;

	sourceBlock.pixelPtr = (unsigned char *)attemptckalloc(cpyLen);
	if (sourceBlock.pixelPtr == NULL) {
	    if (interp != NULL) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			TK_PHOTO_ALLOC_FAILURE_MESSAGE, TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "MALLOC", (char *)NULL);
	    }
	    return TCL_ERROR;
	}
	memToFree = sourceBlock.pixelPtr;
	memcpy(sourceBlock.pixelPtr, blockPtr->pixelPtr, cpyLen);
    }


    xEnd = x + width;
    yEnd = y + height;
    if ((xEnd > modelPtr->width) || (yEnd > modelPtr->height)) {
	if (ImgPhotoSetSize(modelPtr, MAX(xEnd, modelPtr->width),
		MAX(yEnd, modelPtr->height)) == TCL_ERROR) {
	    if (interp != NULL) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			TK_PHOTO_ALLOC_FAILURE_MESSAGE, TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "MALLOC", (char *)NULL);
	    }
	    goto errorExit;
	}
    }

    if ((y < modelPtr->ditherY) || ((y == modelPtr->ditherY)
	    && (x < modelPtr->ditherX))) {
	/*
	 * The dithering isn't correct past the start of this block.
	 */

	modelPtr->ditherX = x;
	modelPtr->ditherY = y;
    }

    /*
     * If this image block could have different red, green and blue
     * components, mark it as a color image.
     */

    greenOffset = sourceBlock.offset[1] - sourceBlock.offset[0];
    blueOffset = sourceBlock.offset[2] - sourceBlock.offset[0];
    alphaOffset = sourceBlock.offset[3];
    if ((alphaOffset >= sourceBlock.pixelSize) || (alphaOffset < 0)) {
	alphaOffset = 0;
	sourceIsSimplePhoto = 1;
    } else {
	alphaOffset -= sourceBlock.offset[0];
    }
    if ((greenOffset != 0) || (blueOffset != 0)) {
	modelPtr->flags |= COLOR_IMAGE;
    }

    /*
     * Copy the data into our local 32-bit/pixel array. If we can do it with a
     * single memmove, we do.
     */

    destLinePtr = modelPtr->pix32 + (y * modelPtr->width + x) * 4;
    pitch = modelPtr->width * 4;

    /*
     * Test to see if we can do the whole write in a single copy. This test is
     * probably too restrictive. We should also be able to do a memmove if
     * pixelSize == 3 and alphaOffset == 0. Maybe other cases too.
     */

    if ((sourceBlock.pixelSize == 4)
	    && (greenOffset == 1) && (blueOffset == 2) && (alphaOffset == 3)
	    && (width <= sourceBlock.width) && (height <= sourceBlock.height)
	    && ((height == 1) || ((x == 0) && (width == modelPtr->width)
		&& (sourceBlock.pitch == pitch)))
	    && (compRule == TK_PHOTO_COMPOSITE_SET)) {
	memmove(destLinePtr, sourceBlock.pixelPtr + sourceBlock.offset[0],
		((size_t)height * width * 4));

	/*
	 * We know there's an alpha offset and we're setting the data, so skip
	 * directly to the point when we recompute the photo validity region.
	 */

	goto recalculateValidRegion;
    }

    /*
     * Copy and merge pixels according to the compositing rule.
     */

    for (hLeft = height; hLeft > 0;) {
	int pixelSize = sourceBlock.pixelSize;
	int compRuleSet = (compRule == TK_PHOTO_COMPOSITE_SET);

	srcLinePtr = sourceBlock.pixelPtr + sourceBlock.offset[0];
	hCopy = MIN(hLeft, sourceBlock.height);
	hLeft -= hCopy;
	for (; hCopy > 0; --hCopy) {
	    /*
	     * If the layout of the source line matches our memory layout and
	     * we're setting, we can just copy the bytes directly, which is
	     * much faster.
	     */

	    if ((pixelSize == 4) && (greenOffset == 1)
		    && (blueOffset == 2) && (alphaOffset == 3)
		    && (width <= sourceBlock.width)
		    && compRuleSet) {
		memcpy(destLinePtr, srcLinePtr, ((size_t)width * 4));
		srcLinePtr += sourceBlock.pitch;
		destLinePtr += pitch;
		continue;
	    }

	    /*
	     * Have to copy the slow way.
	     */

	    destPtr = destLinePtr;
	    for (wLeft = width; wLeft > 0;) {
		wCopy = MIN(wLeft, sourceBlock.width);
		wLeft -= wCopy;
		srcPtr = srcLinePtr;

		/*
		 * But we might be lucky and be able to use fairly fast loops.
		 * It's worth checking...
		 */

		if (alphaOffset == 0) {
		    /*
		     * This is the non-alpha case, so can still be fairly
		     * fast. Note that in the non-alpha-source case, the
		     * compositing rule doesn't apply.
		     */

		    for (; wCopy>0 ; --wCopy, srcPtr+=pixelSize) {
			*destPtr++ = srcPtr[0];
			*destPtr++ = srcPtr[greenOffset];
			*destPtr++ = srcPtr[blueOffset];
			*destPtr++ = 255;
		    }
		    continue;
		} else if (compRuleSet) {
		    /*
		     * This is the SET compositing rule, which just replaces
		     * what was there before with the new data. This is
		     * another fairly fast case. No point in doing a memcpy();
		     * the order of channels is probably wrong.
		     */

		    for (; wCopy>0 ; --wCopy, srcPtr+=pixelSize) {
			*destPtr++ = srcPtr[0];
			*destPtr++ = srcPtr[greenOffset];
			*destPtr++ = srcPtr[blueOffset];
			*destPtr++ = srcPtr[alphaOffset];
		    }
		    continue;
		}

		/*
		 * Bother; need to consider the alpha value of each pixel to
		 * know what to do.
		 */

		for (; wCopy>0 ; --wCopy, srcPtr+=pixelSize) {
		    int alpha = srcPtr[alphaOffset];

		    if (alpha == 255 || !destPtr[3]) {
			/*
			 * Either the source is 100% opaque, or the
			 * destination is entirely blank. In all cases, we
			 * just set the destination to the source.
			 */

			*destPtr++ = srcPtr[0];
			*destPtr++ = srcPtr[greenOffset];
			*destPtr++ = srcPtr[blueOffset];
			*destPtr++ = alpha;
			continue;
		    }

		    /*
		     * Can still skip doing work if the source is 100%
		     * transparent at this point.
		     */

		    if (alpha) {
			int Alpha = destPtr[3];

			/*
			 * OK, there's real work to be done. Luckily, there's
			 * a substantial literature on what to do in this
			 * case. In particular, Porter and Duff have done a
			 * taxonomy of compositing rules, and the right one is
			 * the "Source Over" rule. This code implements that.
			 */

			destPtr[0] = PD_SRC_OVER(srcPtr[0], alpha, destPtr[0],
				Alpha);
			destPtr[1] = PD_SRC_OVER(srcPtr[greenOffset], alpha,
				destPtr[1], Alpha);
			destPtr[2] = PD_SRC_OVER(srcPtr[blueOffset], alpha,
				destPtr[2], Alpha);
			destPtr[3] = PD_SRC_OVER_ALPHA(alpha, Alpha);
		    }

		    destPtr += 4;
		}
	    }
	    srcLinePtr += sourceBlock.pitch;
	    destLinePtr += pitch;
	}
    }

    /*
     * Add this new block to the region which specifies which data is valid.
     */

    if (alphaOffset) {
	/*
	 * This block is grossly inefficient. For each row in the image, it
	 * finds each contiguous string of nontransparent pixels, then marks
	 * those areas as valid in the validRegion mask. This makes drawing
	 * very efficient, because of the way we use X: we just say, here's
	 * your mask, and here's your data. We need not worry about the
	 * current background color, etc. But this costs us a lot on the image
	 * setup. Still, image setup only happens once, whereas the drawing
	 * happens many times, so this might be the best way to go.
	 *
	 * An alternative might be to not set up this mask, and instead, at
	 * drawing time, for each transparent pixel, set its color to the
	 * color of the background behind that pixel. This is what I suspect
	 * most of programs do. However, they don't have to deal with the
	 * canvas, which could have many different background colors.
	 * Determining the correct bg color for a given pixel might be
	 * expensive.
	 */

	if (compRule != TK_PHOTO_COMPOSITE_OVERLAY) {
	    TkRegion workRgn;

	    /*
	     * Don't need this when using the OVERLAY compositing rule, which
	     * always strictly increases the valid region.
	     */

	recalculateValidRegion:
	    workRgn = TkCreateRegion();
	    rect.x = x;
	    rect.y = y;
	    rect.width = width;
	    rect.height = height;
	    TkUnionRectWithRegion(&rect, workRgn, workRgn);
	    TkSubtractRegion(modelPtr->validRegion, workRgn,
		    modelPtr->validRegion);
	    TkDestroyRegion(workRgn);
	}

	/*
	 * Factorize out the main part of the building of the region data to
	 * allow for more efficient per-platform implementations. [Bug 919066]
	 */

	TkpBuildRegionFromAlphaData(modelPtr->validRegion, (unsigned) x,
		(unsigned) y, (unsigned) width, (unsigned) height,
		modelPtr->pix32 + (y * modelPtr->width + x) * 4 + 3,
		4, (unsigned) modelPtr->width * 4);
    } else {
	rect.x = x;
	rect.y = y;
	rect.width = width;
	rect.height = height;
	TkUnionRectWithRegion(&rect, modelPtr->validRegion,
		modelPtr->validRegion);
    }

    /*
     * Check if display code needs alpha blending...
     */

    if (!sourceIsSimplePhoto && (height == 1)) {
	/*
	 * Optimize the single span case if we can. This speeds up code that
	 * builds up large simple-alpha images by scan-lines or individual
	 * pixels. We don't negate COMPLEX_ALPHA in this case. [Bug 1409140]
	 * [Patch 1539990]
	 */

	if (!(modelPtr->flags & COMPLEX_ALPHA)) {
	    int x1;

	    for (x1=x ; x1<x+width ; x1++) {
		unsigned char newAlpha;

		destLinePtr = modelPtr->pix32 + (y*modelPtr->width + x1)*4;
		newAlpha = destLinePtr[3];
		if (newAlpha && newAlpha != 255) {
		    modelPtr->flags |= COMPLEX_ALPHA;
		    break;
		}
	    }
	}
    } else if ((alphaOffset != 0) || (modelPtr->flags & COMPLEX_ALPHA)) {
	/*
	 * Check for partial transparency if alpha pixels are specified, or
	 * rescan if we already knew such pixels existed. To restrict this
	 * Toggle to only checking the changed pixels requires knowing where
	 * the alpha pixels are.
	 */

	ToggleComplexAlphaIfNeeded(modelPtr);
    }

    /*
     * Update each instance.
     */

    Tk_DitherPhoto((Tk_PhotoHandle)modelPtr, x, y, width, height);

    /*
     * Tell the core image code that this image has changed.
     */

    Tk_ImageChanged(modelPtr->tkModel, x, y, width, height,
	    modelPtr->width, modelPtr->height);

    if (memToFree) ckfree(memToFree);

    return TCL_OK;

  errorExit:
    if (memToFree) ckfree(memToFree);

    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_PhotoPutZoomedBlock --
 *
 *	This function is called to put image data into a photo image, with
 *	possible subsampling and/or zooming of the pixels.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The image data is stored. The image may be expanded. The Tk image code
 *	is informed that the image has changed.
 *
 *----------------------------------------------------------------------
 */

int
Tk_PhotoPutZoomedBlock(
    Tcl_Interp *interp,		/* Interpreter for passing back error
				 * messages, or NULL. */
    Tk_PhotoHandle handle,	/* Opaque handle for the photo image to be
				 * updated. */
    Tk_PhotoImageBlock *blockPtr,
				/* Pointer to a structure describing the pixel
				 * data to be copied into the image. */
    int x, int y,		/* Coordinates of the top-left pixel to be
				 * updated in the image. */
    int width, int height,	/* Dimensions of the area of the image to be
				 * updated. */
    int zoomX, int zoomY,	/* Zoom factors for the X and Y axes. */
    int subsampleX, int subsampleY,
				/* Subsampling factors for the X and Y
				 * axes. */
    int compRule)		/* Compositing rule to use when processing
				 * transparent pixels. */
{
    PhotoModel *modelPtr = (PhotoModel *) handle;
    Tk_PhotoImageBlock sourceBlock;
    unsigned char *memToFree;
    int xEnd, yEnd, greenOffset, blueOffset, alphaOffset;
    int wLeft, hLeft, wCopy, hCopy, blockWid, blockHt;
    unsigned char *srcPtr, *srcLinePtr, *srcOrigPtr, *destPtr, *destLinePtr;
    int pitch, xRepeat, yRepeat, blockXSkip, blockYSkip, sourceIsSimplePhoto;
    XRectangle rect;

    /*
     * Zero-sized blocks never cause any changes. [Bug 3078902]
     */

    if (blockPtr->height == 0 || blockPtr->width == 0) {
	return TCL_OK;
    }

    if (zoomX==1 && zoomY==1 && subsampleX==1 && subsampleY==1) {
	return Tk_PhotoPutBlock(interp, handle, blockPtr, x, y, width, height,
		compRule);
    }

    sourceIsSimplePhoto = compRule & SOURCE_IS_SIMPLE_ALPHA_PHOTO;
    compRule &= ~SOURCE_IS_SIMPLE_ALPHA_PHOTO;

    if (zoomX <= 0 || zoomY <= 0) {
	return TCL_OK;
    }
    if ((modelPtr->userWidth != 0) && ((x + width) > modelPtr->userWidth)) {
	width = modelPtr->userWidth - x;
    }
    if ((modelPtr->userHeight != 0)
	    && ((y + height) > modelPtr->userHeight)) {
	height = modelPtr->userHeight - y;
    }
    if (width <= 0 || height <= 0) {
	return TCL_OK;
    }

    /*
     * Fix for Bug e4336bef5d:
     * Make a local copy of *blockPtr, as we might have to change some
     * of its fields and don't want to interfere with the caller's data.
     *
     * If source and destination are the same image, create a copy  of the
     * source data in our local sourceBlock.
     *
     * To find out, just comparing the pointers is not enough - they might have
     * different values and still point to the same block of memory. (e.g.
     * if the -from option was passed to [imageName copy])
     */
    sourceBlock = *blockPtr;
    memToFree = NULL;
    if (modelPtr->pix32 && (sourceBlock.pixelPtr >= modelPtr->pix32)
	    && (sourceBlock.pixelPtr < modelPtr->pix32 + modelPtr->width
	    * modelPtr->height * 4)) {
	/*
	 * Fix 5c51be6411: avoid reading
	 *
	 *	(sourceBlock.pitch - sourceBlock.width * sourceBlock.pixelSize)
	 *
	 * bytes past the end of modelPtr->pix32[] when
	 *
	 *	blockPtr->pixelPtr > (modelPtr->pix32 +
	 *		4 * modelPtr->width * modelPtr->height -
	 *		sourceBlock.height * sourceBlock.pitch)
	 */
	unsigned int cpyLen = (sourceBlock.height - 1) * sourceBlock.pitch +
		sourceBlock.width * sourceBlock.pixelSize;

	sourceBlock.pixelPtr = (unsigned char *)attemptckalloc(cpyLen);
	if (sourceBlock.pixelPtr == NULL) {
	    if (interp != NULL) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			TK_PHOTO_ALLOC_FAILURE_MESSAGE, TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "MALLOC", (char *)NULL);
	    }
	    return TCL_ERROR;
	}
	memToFree = sourceBlock.pixelPtr;
	memcpy(sourceBlock.pixelPtr, blockPtr->pixelPtr, cpyLen);
    }

    xEnd = x + width;
    yEnd = y + height;
    if ((xEnd > modelPtr->width) || (yEnd > modelPtr->height)) {
	if (ImgPhotoSetSize(modelPtr, MAX(xEnd, modelPtr->width),
		MAX(yEnd, modelPtr->height)) == TCL_ERROR) {
	    if (interp != NULL) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			TK_PHOTO_ALLOC_FAILURE_MESSAGE, TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "MALLOC", (char *)NULL);
	    }
	    goto errorExit;
	}
    }

    if ((y < modelPtr->ditherY) || ((y == modelPtr->ditherY)
	    && (x < modelPtr->ditherX))) {
	/*
	 * The dithering isn't correct past the start of this block.
	 */

	modelPtr->ditherX = x;
	modelPtr->ditherY = y;
    }

    /*
     * If this image block could have different red, green and blue
     * components, mark it as a color image.
     */

    greenOffset = sourceBlock.offset[1] - sourceBlock.offset[0];
    blueOffset = sourceBlock.offset[2] - sourceBlock.offset[0];
    alphaOffset = sourceBlock.offset[3];
    if ((alphaOffset >= sourceBlock.pixelSize) || (alphaOffset < 0)) {
	alphaOffset = 0;
	sourceIsSimplePhoto = 1;
    } else {
	alphaOffset -= sourceBlock.offset[0];
    }
    if ((greenOffset != 0) || (blueOffset != 0)) {
	modelPtr->flags |= COLOR_IMAGE;
    }

    /*
     * Work out what area the pixel data in the block expands to after
     * subsampling and zooming.
     */

    blockXSkip = subsampleX * sourceBlock.pixelSize;
    blockYSkip = subsampleY * sourceBlock.pitch;
    if (subsampleX > 0) {
	blockWid = ((sourceBlock.width + subsampleX - 1) / subsampleX) * zoomX;
    } else if (subsampleX == 0) {
	blockWid = width;
    } else {
	blockWid = ((sourceBlock.width - subsampleX - 1) / -subsampleX) * zoomX;
    }
    if (subsampleY > 0) {
	blockHt = ((sourceBlock.height + subsampleY - 1) / subsampleY) * zoomY;
    } else if (subsampleY == 0) {
	blockHt = height;
    } else {
	blockHt = ((sourceBlock.height - subsampleY - 1) / -subsampleY) * zoomY;
    }

    /*
     * Copy the data into our local 32-bit/pixel array.
     */

    destLinePtr = modelPtr->pix32 + (y * modelPtr->width + x) * 4;
    srcOrigPtr = sourceBlock.pixelPtr + sourceBlock.offset[0];
    if (subsampleX < 0) {
	srcOrigPtr += (sourceBlock.width - 1) * sourceBlock.pixelSize;
    }
    if (subsampleY < 0) {
	srcOrigPtr += (sourceBlock.height - 1) * sourceBlock.pitch;
    }

    pitch = modelPtr->width * 4;
    for (hLeft = height; hLeft > 0; ) {
	hCopy = MIN(hLeft, blockHt);
	hLeft -= hCopy;
	yRepeat = zoomY;
	srcLinePtr = srcOrigPtr;
	for (; hCopy > 0; --hCopy) {
	    destPtr = destLinePtr;
	    for (wLeft = width; wLeft > 0;) {
		wCopy = MIN(wLeft, blockWid);
		wLeft -= wCopy;
		srcPtr = srcLinePtr;
		for (; wCopy > 0; wCopy -= zoomX) {
		    for (xRepeat = MIN(wCopy, zoomX); xRepeat > 0; xRepeat--) {
			int alpha = srcPtr[alphaOffset];/* Source alpha. */

			/*
			 * Common case (solid pixels) first
			 */

			if (!alphaOffset || (alpha == 255)) {
			    *destPtr++ = srcPtr[0];
			    *destPtr++ = srcPtr[greenOffset];
			    *destPtr++ = srcPtr[blueOffset];
			    *destPtr++ = 255;
			    continue;
			}

			if (compRule==TK_PHOTO_COMPOSITE_SET || !destPtr[3]) {
			    /*
			     * Either this is the SET rule (we overwrite
			     * whatever is there) or the destination is
			     * entirely blank. In both cases, we just set the
			     * destination to the source.
			     */

			    *destPtr++ = srcPtr[0];
			    *destPtr++ = srcPtr[greenOffset];
			    *destPtr++ = srcPtr[blueOffset];
			    *destPtr++ = alpha;
			} else if (alpha) {
			    int Alpha = destPtr[3];	/* Destination
							 * alpha. */

			    destPtr[0] = PD_SRC_OVER(srcPtr[0], alpha,
				    destPtr[0], Alpha);
			    destPtr[1] = PD_SRC_OVER(srcPtr[greenOffset],alpha,
				    destPtr[1], Alpha);
			    destPtr[2] = PD_SRC_OVER(srcPtr[blueOffset], alpha,
				    destPtr[2], Alpha);
			    destPtr[3] = PD_SRC_OVER_ALPHA(alpha, Alpha);

			    destPtr += 4;
			} else {
			    destPtr += 4;
			}
		    }
		    srcPtr += blockXSkip;
		}
	    }
	    destLinePtr += pitch;
	    yRepeat--;
	    if (yRepeat <= 0) {
		srcLinePtr += blockYSkip;
		yRepeat = zoomY;
	    }
	}
    }

    /*
     * Recompute the region of data for which we have valid pixels to plot.
     */

    if (alphaOffset) {
	if (compRule != TK_PHOTO_COMPOSITE_OVERLAY) {
	    /*
	     * Don't need this when using the OVERLAY compositing rule, which
	     * always strictly increases the valid region.
	     */

	    TkRegion workRgn = TkCreateRegion();

	    rect.x = x;
	    rect.y = y;
	    rect.width = width;
	    rect.height = 1;
	    TkUnionRectWithRegion(&rect, workRgn, workRgn);
	    TkSubtractRegion(modelPtr->validRegion, workRgn,
		    modelPtr->validRegion);
	    TkDestroyRegion(workRgn);
	}

	TkpBuildRegionFromAlphaData(modelPtr->validRegion,
		(unsigned)x, (unsigned)y, (unsigned)width, (unsigned)height,
		&modelPtr->pix32[(y * modelPtr->width + x) * 4 + 3], 4,
		(unsigned) modelPtr->width * 4);
    } else {
	rect.x = x;
	rect.y = y;
	rect.width = width;
	rect.height = height;
	TkUnionRectWithRegion(&rect, modelPtr->validRegion,
		modelPtr->validRegion);
    }

    /*
     * Check if display code needs alpha blending...
     */

    if (!sourceIsSimplePhoto && (width == 1) && (height == 1)) {
	/*
	 * Optimize the single pixel case if we can. This speeds up code that
	 * builds up large simple-alpha images by single pixels. We don't
	 * negate COMPLEX_ALPHA in this case. [Bug 1409140]
	 */
	if (!(modelPtr->flags & COMPLEX_ALPHA)) {
	    unsigned char newAlpha;

	    destLinePtr = modelPtr->pix32 + (y * modelPtr->width + x) * 4;
	    newAlpha = destLinePtr[3];

	    if (newAlpha && newAlpha != 255) {
		modelPtr->flags |= COMPLEX_ALPHA;
	    }
	}
    } else if ((alphaOffset != 0) || (modelPtr->flags & COMPLEX_ALPHA)) {
	/*
	 * Check for partial transparency if alpha pixels are specified, or
	 * rescan if we already knew such pixels existed. To restrict this
	 * Toggle to only checking the changed pixels requires knowing where
	 * the alpha pixels are.
	 */
	ToggleComplexAlphaIfNeeded(modelPtr);
    }

    /*
     * Update each instance.
     */

    Tk_DitherPhoto((Tk_PhotoHandle) modelPtr, x, y, width, height);

    /*
     * Tell the core image code that this image has changed.
     */

    Tk_ImageChanged(modelPtr->tkModel, x, y, width, height, modelPtr->width,
	    modelPtr->height);

    if (memToFree) ckfree(memToFree);

    return TCL_OK;

  errorExit:
    if (memToFree) ckfree(memToFree);

    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_DitherPhoto --
 *
 *	This function is called to update an area of each instance's pixmap by
 *	dithering the corresponding area of the image model.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The pixmap of each instance of this image gets updated. The fields in
 *	*modelPtr indicating which area of the image is correctly dithered
 *	get updated.
 *
 *----------------------------------------------------------------------
 */

void
Tk_DitherPhoto(
    Tk_PhotoHandle photo,	/* Image model whose instances are to be
				 * updated. */
    int x, int y,		/* Coordinates of the top-left pixel in the
				 * area to be dithered. */
    int width, int height)	/* Dimensions of the area to be dithered. */
{
    PhotoModel *modelPtr = (PhotoModel *) photo;
    PhotoInstance *instancePtr;

    if ((width <= 0) || (height <= 0)) {
	return;
    }

    for (instancePtr = modelPtr->instancePtr; instancePtr != NULL;
	    instancePtr = instancePtr->nextPtr) {
	TkImgDitherInstance(instancePtr, x, y, width, height);
    }

    /*
     * Work out whether this block will be correctly dithered and whether it
     * will extend the correctly dithered region.
     */

    if (((y < modelPtr->ditherY)
	    || ((y == modelPtr->ditherY) && (x <= modelPtr->ditherX)))
	    && ((y + height) > (modelPtr->ditherY))) {
	/*
	 * This block starts inside (or immediately after) the correctly
	 * dithered region, so the first scan line at least will be right.
	 * Furthermore this block extends into scanline modelPtr->ditherY.
	 */

	if ((x == 0) && (width == modelPtr->width)) {
	    /*
	     * We are doing the full width, therefore the dithering will be
	     * correct to the end.
	     */

	    modelPtr->ditherX = 0;
	    modelPtr->ditherY = y + height;
	} else {
	    /*
	     * We are doing partial scanlines, therefore the
	     * correctly-dithered region will be extended by at most one scan
	     * line.
	     */

	    if (x <= modelPtr->ditherX) {
		modelPtr->ditherX = x + width;
		if (modelPtr->ditherX >= modelPtr->width) {
		    modelPtr->ditherX = 0;
		    modelPtr->ditherY++;
		}
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_PhotoBlank --
 *
 *	This function is called to clear an entire photo image.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The valid region for the image is set to the null region. The generic
 *	image code is notified that the image has changed.
 *
 *----------------------------------------------------------------------
 */

void
Tk_PhotoBlank(
    Tk_PhotoHandle handle)	/* Handle for the image to be blanked. */
{
    PhotoModel *modelPtr = (PhotoModel *) handle;
    PhotoInstance *instancePtr;

    modelPtr->ditherX = modelPtr->ditherY = 0;
    modelPtr->flags = 0;

    /*
     * The image has valid data nowhere.
     */

    if (modelPtr->validRegion != NULL) {
	TkDestroyRegion(modelPtr->validRegion);
    }
    modelPtr->validRegion = TkCreateRegion();

    /*
     * Clear out the 32-bit pixel storage array. Clear out the dithering error
     * arrays for each instance.
     */

    if (modelPtr->pix32) {
	memset(modelPtr->pix32, 0,
		((size_t)modelPtr->width * modelPtr->height * 4));
    }
    for (instancePtr = modelPtr->instancePtr; instancePtr != NULL;
	    instancePtr = instancePtr->nextPtr) {
	TkImgResetDither(instancePtr);
    }

    /*
     * Tell the core image code that this image has changed.
     */

    Tk_ImageChanged(modelPtr->tkModel, 0, 0, modelPtr->width,
	    modelPtr->height, modelPtr->width, modelPtr->height);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_PhotoExpand --
 *
 *	This function is called to request that a photo image be expanded if
 *	necessary to be at least `width' pixels wide and `height' pixels high.
 *	If the user has declared a definite image size (using the -width and
 *	-height configuration options) then this call has no effect.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The size of the photo image may change; if so the generic image code
 *	is informed.
 *
 *----------------------------------------------------------------------
 */

int
Tk_PhotoExpand(
    Tcl_Interp *interp,		/* Interpreter for passing back error
				 * messages, or NULL. */
    Tk_PhotoHandle handle,	/* Handle for the image to be expanded. */
    int width, int height)	/* Desired minimum dimensions of the image. */
{
    PhotoModel *modelPtr = (PhotoModel *) handle;

    if (width <= modelPtr->width) {
	width = modelPtr->width;
    }
    if (height <= modelPtr->height) {
	height = modelPtr->height;
    }
    if ((width != modelPtr->width) || (height != modelPtr->height)) {
	if (ImgPhotoSetSize(modelPtr, MAX(width, modelPtr->width),
		MAX(height, modelPtr->height)) == TCL_ERROR) {
	    if (interp != NULL) {
		Tcl_SetObjResult(interp, Tcl_NewStringObj(
			TK_PHOTO_ALLOC_FAILURE_MESSAGE, TCL_INDEX_NONE));
		Tcl_SetErrorCode(interp, "TK", "MALLOC", (char *)NULL);
	    }
	    return TCL_ERROR;
	}
	Tk_ImageChanged(modelPtr->tkModel, 0, 0, 0, 0, modelPtr->width,
		modelPtr->height);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_PhotoGetSize --
 *
 *	This function is called to obtain the current size of a photo image.
 *
 * Results:
 *	The image's width and height are returned in *widthp and *heightp.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Tk_PhotoGetSize(
    Tk_PhotoHandle handle,	/* Handle for the image whose dimensions are
				 * requested. */
    int *widthPtr, int *heightPtr)
				/* The dimensions of the image are returned
				 * here. */
{
    PhotoModel *modelPtr = (PhotoModel *) handle;

    *widthPtr = modelPtr->width;
    *heightPtr = modelPtr->height;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_PhotoSetSize --
 *
 *	This function is called to set size of a photo image. This call is
 *	equivalent to using the -width and -height configuration options.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The size of the image may change; if so the generic image code is
 *	informed.
 *
 *----------------------------------------------------------------------
 */

int
Tk_PhotoSetSize(
    Tcl_Interp *interp,		/* Interpreter for passing back error
				 * messages, or NULL. */
    Tk_PhotoHandle handle,	/* Handle for the image whose size is to be
				 * set. */
    int width, int height)	/* New dimensions for the image. */
{
    PhotoModel *modelPtr = (PhotoModel *) handle;

    modelPtr->userWidth = width;
    modelPtr->userHeight = height;
    if (ImgPhotoSetSize(modelPtr, ((width > 0) ? width: modelPtr->width),
	    ((height > 0) ? height: modelPtr->height)) == TCL_ERROR) {
	if (interp != NULL) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    TK_PHOTO_ALLOC_FAILURE_MESSAGE, TCL_INDEX_NONE));
	    Tcl_SetErrorCode(interp, "TK", "MALLOC", (char *)NULL);
	}
	return TCL_ERROR;
    }
    Tk_ImageChanged(modelPtr->tkModel, 0, 0, 0, 0,
	    modelPtr->width, modelPtr->height);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGetPhotoValidRegion --
 *
 *	This function is called to get the part of the photo where there is
 *	valid data. Or, conversely, the part of the photo which is
 *	transparent.
 *
 * Results:
 *	A TkRegion value that indicates the current area of the photo that is
 *	valid. This value should not be used after any modification to the
 *	photo image.
 *
 * Side Effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

TkRegion
TkPhotoGetValidRegion(
    Tk_PhotoHandle handle)	/* Handle for the image whose valid region is
				 * to obtained. */
{
    PhotoModel *modelPtr = (PhotoModel *) handle;

    return modelPtr->validRegion;
}

/*
 *----------------------------------------------------------------------
 *
 * ImgGetPhoto --
 *
 *	This function is called to obtain image data from a photo image. This
 *	function fills in the Tk_PhotoImageBlock structure pointed to by
 *	`blockPtr' with details of the address and layout of the image data in
 *	memory.
 *
 * Results:
 *	A pointer to the allocated data which should be freed later. NULL if
 *	there is no need to free data because blockPtr->pixelPtr points
 *	directly to the image data.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static char *
ImgGetPhoto(
    PhotoModel *modelPtr,	/* Handle for the photo image from which image
				 * data is desired. */
    Tk_PhotoImageBlock *blockPtr,
				/* Information about the address and layout of
				 * the image data is returned here. */
    struct SubcommandOptions *optPtr)
{
    unsigned char *pixelPtr;
    int x, y, greenOffset, blueOffset, alphaOffset;

    Tk_PhotoGetImage((Tk_PhotoHandle) modelPtr, blockPtr);
    blockPtr->pixelPtr += optPtr->fromY * blockPtr->pitch
	    + optPtr->fromX * blockPtr->pixelSize;
    blockPtr->width = optPtr->fromX2 - optPtr->fromX;
    blockPtr->height = optPtr->fromY2 - optPtr->fromY;

    if (!(modelPtr->flags & COLOR_IMAGE) &&
	    (!(optPtr->options & OPT_BACKGROUND)
	    || ((optPtr->background->red == optPtr->background->green)
	    && (optPtr->background->red == optPtr->background->blue)))) {
	blockPtr->offset[0] = blockPtr->offset[1] = blockPtr->offset[2];
    }
    alphaOffset = 0;
    for (y = 0; y < blockPtr->height; y++) {
	pixelPtr = blockPtr->pixelPtr + (y * blockPtr->pitch)
		+ blockPtr->pixelSize - 1;
	for (x = 0; x < blockPtr->width; x++) {
	    if (*pixelPtr != 255) {
		alphaOffset = 3;
		break;
	    }
	    pixelPtr += blockPtr->pixelSize;
	}
	if (alphaOffset) {
	    break;
	}
    }
    if (!alphaOffset) {
	blockPtr->offset[3]= -1; /* Tell caller alpha need not be read */
    }
    greenOffset = blockPtr->offset[1] - blockPtr->offset[0];
    blueOffset = blockPtr->offset[2] - blockPtr->offset[0];
    if (((optPtr->options & OPT_BACKGROUND) && alphaOffset) ||
	    ((optPtr->options & OPT_GRAYSCALE) && (greenOffset||blueOffset))) {
	int newPixelSize;
	unsigned char *srcPtr, *destPtr;
	char *data;

	newPixelSize = (!(optPtr->options & OPT_BACKGROUND) && alphaOffset)
		? 2 : 1;
	if ((greenOffset||blueOffset) && !(optPtr->options & OPT_GRAYSCALE)) {
	    newPixelSize += 2;
	}

	if (blockPtr->height > (int)((UINT_MAX/newPixelSize)/blockPtr->width)) {
	    return NULL;
	}
	data = (char *)attemptckalloc(newPixelSize*blockPtr->width*blockPtr->height);
	if (data == NULL) {
	    return NULL;
	}
	srcPtr = blockPtr->pixelPtr + blockPtr->offset[0];
	destPtr = (unsigned char *) data;
	if (!greenOffset && !blueOffset) {
	    for (y = blockPtr->height; y > 0; y--) {
		for (x = blockPtr->width; x > 0; x--) {
		    *destPtr = *srcPtr;
		    srcPtr += blockPtr->pixelSize;
		    destPtr += newPixelSize;
		}
		srcPtr += blockPtr->pitch -
			blockPtr->width * blockPtr->pixelSize;
	    }
	} else if (optPtr->options & OPT_GRAYSCALE) {
	    for (y = blockPtr->height; y > 0; y--) {
		for (x = blockPtr->width; x > 0; x--) {
		    *destPtr = (unsigned char) ((srcPtr[0]*11 + srcPtr[1]*16
			    + srcPtr[2]*5 + 16) >> 5);
		    srcPtr += blockPtr->pixelSize;
		    destPtr += newPixelSize;
		}
		srcPtr += blockPtr->pitch -
			blockPtr->width * blockPtr->pixelSize;
	    }
	} else {
	    for (y = blockPtr->height; y > 0; y--) {
		for (x = blockPtr->width; x > 0; x--) {
		    destPtr[0] = srcPtr[0];
		    destPtr[1] = srcPtr[1];
		    destPtr[2] = srcPtr[2];
		    srcPtr += blockPtr->pixelSize;
		    destPtr += newPixelSize;
		}
		srcPtr += blockPtr->pitch -
			blockPtr->width * blockPtr->pixelSize;
	    }
	}
	srcPtr = blockPtr->pixelPtr + alphaOffset;
	destPtr = (unsigned char *) data;
	if (!alphaOffset) {
	    /*
	     * Nothing to be done.
	     */
	} else if (optPtr->options & OPT_BACKGROUND) {
	    if (newPixelSize > 2) {
		int red = optPtr->background->red>>8;
		int green = optPtr->background->green>>8;
		int blue = optPtr->background->blue>>8;

		for (y = blockPtr->height; y > 0; y--) {
		    for (x = blockPtr->width; x > 0; x--) {
			destPtr[0] += (unsigned char) (((255 - *srcPtr) *
				(red-destPtr[0])) / 255);
			destPtr[1] += (unsigned char) (((255 - *srcPtr) *
				(green-destPtr[1])) / 255);
			destPtr[2] += (unsigned char) (((255 - *srcPtr) *
				(blue-destPtr[2])) / 255);
			srcPtr += blockPtr->pixelSize;
			destPtr += newPixelSize;
		    }
		    srcPtr += blockPtr->pitch -
			    blockPtr->width * blockPtr->pixelSize;
		}
	    } else {
		int gray = (unsigned char) (((optPtr->background->red>>8) * 11
			+ (optPtr->background->green>>8) * 16
			+ (optPtr->background->blue>>8) * 5 + 16) >> 5);

		for (y = blockPtr->height; y > 0; y--) {
		    for (x = blockPtr->width; x > 0; x--) {
			destPtr[0] += ((255 - *srcPtr) *
				(gray-destPtr[0])) / 255;
			srcPtr += blockPtr->pixelSize;
			destPtr += newPixelSize;
		    }
		    srcPtr += blockPtr->pitch -
			    blockPtr->width * blockPtr->pixelSize;
		}
	    }
	} else {
	    destPtr += newPixelSize-1;
	    for (y = blockPtr->height; y > 0; y--) {
		for (x = blockPtr->width; x > 0; x--) {
		    *destPtr = *srcPtr;
		    srcPtr += blockPtr->pixelSize;
		    destPtr += newPixelSize;
		}
		srcPtr += blockPtr->pitch -
			blockPtr->width * blockPtr->pixelSize;
	    }
	}
	blockPtr->pixelPtr = (unsigned char *) data;
	blockPtr->pixelSize = newPixelSize;
	blockPtr->pitch = newPixelSize * blockPtr->width;
	blockPtr->offset[0] = 0;
	if (newPixelSize > 2) {
	    blockPtr->offset[1] = 1;
	    blockPtr->offset[2] = 2;
	    blockPtr->offset[3]= 3;
	} else {
	    blockPtr->offset[1] = 0;
	    blockPtr->offset[2] = 0;
	    blockPtr->offset[3]= 1;
	}
	return data;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_PhotoGetImage --
 *
 *	This function is called to obtain image data from a photo image. This
 *	function fills in the Tk_PhotoImageBlock structure pointed to by
 *	`blockPtr' with details of the address and layout of the image data in
 *	memory.
 *
 * Results:
 *	TRUE (1) indicating that image data is available, for backwards
 *	compatibility with the old photo widget.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_PhotoGetImage(
    Tk_PhotoHandle handle,	/* Handle for the photo image from which image
				 * data is desired. */
    Tk_PhotoImageBlock *blockPtr)
				/* Information about the address and layout of
				 * the image data is returned here. */
{
    PhotoModel *modelPtr = (PhotoModel *) handle;

    blockPtr->pixelPtr = modelPtr->pix32;
    blockPtr->width = modelPtr->width;
    blockPtr->height = modelPtr->height;
    blockPtr->pitch = modelPtr->width * 4;
    blockPtr->pixelSize = 4;
    blockPtr->offset[0] = 0;
    blockPtr->offset[1] = 1;
    blockPtr->offset[2] = 2;
    blockPtr->offset[3] = 3;
    return 1;
}

/*
 *--------------------------------------------------------------
 *
 * ImgPostscriptPhoto --
 *
 *	This function is called to output the contents of a photo image in
 *	Postscript by calling the Tk_PostscriptPhoto function.
 *
 * Results:
 *	Returns a standard Tcl return value.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static int
ImgPhotoPostscript(
    void *clientData,	/* Handle for the photo image. */
    Tcl_Interp *interp,		/* Interpreter. */
    TCL_UNUSED(Tk_Window),		/* (unused) */
    Tk_PostscriptInfo psInfo,	/* Postscript info. */
    int x, int y,		/* First pixel to output. */
    int width, int height,	/* Width and height of area. */
    TCL_UNUSED(int))		/* (unused) */
{
    Tk_PhotoImageBlock block;

    Tk_PhotoGetImage(clientData, &block);
    block.pixelPtr += y * block.pitch + x * block.pixelSize;

    return Tk_PostscriptPhoto(interp, &block, psInfo, width, height);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * tab-width: 8
 * End:
 */
