/*
 * tkImage.c --
 *
 *	This file contains code that allows images to be nested inside text
 *	widgets. It also implements the "image" widget command for texts.
 *
 * Copyright © 1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkPort.h"
#include "tkText.h"

#ifdef _WIN32
#include "tkWinInt.h"
#endif

/*
 * Macro that determines the size of an embedded image segment:
 */

#define EI_SEG_SIZE \
	(offsetof(TkTextSegment, body) + sizeof(TkTextEmbImage))

/*
 * Prototypes for functions defined in this file:
 */

static TkTextSegment *	EmbImageCleanupProc(TkTextSegment *segPtr,
			    TkTextLine *linePtr);
static void		EmbImageCheckProc(TkTextSegment *segPtr,
			    TkTextLine *linePtr);
static void		EmbImageBboxProc(TkText *textPtr,
			    TkTextDispChunk *chunkPtr, Tcl_Size index, int y,
			    int lineHeight, int baseline, int *xPtr, int *yPtr,
			    int *widthPtr, int *heightPtr);
static int		EmbImageConfigure(TkText *textPtr,
			    TkTextSegment *eiPtr, Tcl_Size objc,
			    Tcl_Obj *const objv[]);
static int		EmbImageDeleteProc(TkTextSegment *segPtr,
			    TkTextLine *linePtr, int treeGone);
static void		EmbImageDisplayProc(TkText *textPtr,
			    TkTextDispChunk *chunkPtr, int x, int y,
			    int lineHeight, int baseline, Display *display,
			    Drawable dst, int screenY);
static int		EmbImageLayoutProc(TkText *textPtr,
			    TkTextIndex *indexPtr, TkTextSegment *segPtr,
			    Tcl_Size offset, int maxX, Tcl_Size maxChars,
			    int noCharsYet, TkWrapMode wrapMode,
			    TkTextDispChunk *chunkPtr);
static void		EmbImageProc(void *clientData, int x, int y,
			    int width, int height, int imageWidth,
			    int imageHeight);

/*
 * The following structure declares the "embedded image" segment type.
 */

const Tk_SegType tkTextEmbImageType = {
    "image",			/* name */
    0,				/* leftGravity */
    NULL,			/* splitProc */
    EmbImageDeleteProc,		/* deleteProc */
    EmbImageCleanupProc,	/* cleanupProc */
    NULL,			/* lineChangeProc */
    EmbImageLayoutProc,		/* layoutProc */
    EmbImageCheckProc		/* checkProc */
};

/*
 * Definitions for alignment values:
 */

static const char *const alignStrings[] = {
    "baseline", "bottom", "center", "top", NULL
};

/*
 * Information used for parsing image configuration options:
 */

static const Tk_OptionSpec optionSpecs[] = {
    {TK_OPTION_STRING_TABLE, "-align", NULL, NULL,
	"center", TCL_INDEX_NONE, offsetof(TkTextEmbImage, align),
	TK_OPTION_ENUM_VAR, alignStrings, 0},
    {TK_OPTION_PIXELS, "-padx", NULL, NULL,
	"0", offsetof(TkTextEmbImage, padXObj), TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_PIXELS, "-pady", NULL, NULL,
	"0", offsetof(TkTextEmbImage, padYObj), TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_STRING, "-image", NULL, NULL,
	NULL, offsetof(TkTextEmbImage, imageObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-name", NULL, NULL,
	NULL, offsetof(TkTextEmbImage, imageNameObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0}
};

/*
 *--------------------------------------------------------------
 *
 * TkTextImageCmd --
 *
 *	This function implements the "image" widget command for text widgets.
 *	See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result or error.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

int
TkTextImageCmd(
    TkText *textPtr,	/* Information about text widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. Someone else has already
				 * parsed this command enough to know that
				 * objv[1] is "image". */
{
    int idx;
    TkTextSegment *eiPtr;
    TkTextIndex index;
    static const char *const optionStrings[] = {
	"cget", "configure", "create", "names", NULL
    };
    enum opts {
	CMD_CGET, CMD_CONF, CMD_CREATE, CMD_NAMES
    };

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "option ?arg ...?");
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObjStruct(interp, objv[2], optionStrings,
	    sizeof(char *), "option", 0, &idx) != TCL_OK) {
	return TCL_ERROR;
    }
    switch ((enum opts) idx) {
    case CMD_CGET: {
	Tcl_Obj *objPtr;

	if (objc != 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "index option");
	    return TCL_ERROR;
	}
	if (TkTextGetObjIndex(interp, textPtr, objv[3], &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	eiPtr = TkTextIndexToSeg(&index, NULL);
	if (eiPtr->typePtr != &tkTextEmbImageType) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "no embedded image at index \"%s\"",
		    Tcl_GetString(objv[3])));
	    Tcl_SetErrorCode(interp, "TK", "TEXT", "NO_IMAGE", (char *)NULL);
	    return TCL_ERROR;
	}
	objPtr = Tk_GetOptionValue(interp, &eiPtr->body.ei,
		eiPtr->body.ei.optionTable, objv[4], textPtr->tkwin);
	if (objPtr == NULL) {
	    return TCL_ERROR;
	} else {
	    Tcl_SetObjResult(interp, objPtr);
	    return TCL_OK;
	}
    }
    case CMD_CONF:
	if (objc < 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "index ?-option value ...?");
	    return TCL_ERROR;
	}
	if (TkTextGetObjIndex(interp, textPtr, objv[3], &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	eiPtr = TkTextIndexToSeg(&index, NULL);
	if (eiPtr->typePtr != &tkTextEmbImageType) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "no embedded image at index \"%s\"",
		    Tcl_GetString(objv[3])));
	    Tcl_SetErrorCode(interp, "TK", "TEXT", "NO_IMAGE", (char *)NULL);
	    return TCL_ERROR;
	}
	if (objc <= 5) {
	    Tcl_Obj *objPtr = Tk_GetOptionInfo(interp,
		    &eiPtr->body.ei, eiPtr->body.ei.optionTable,
		    (objc == 5) ? objv[4] : NULL, textPtr->tkwin);

	    if (objPtr == NULL) {
		return TCL_ERROR;
	    } else {
		Tcl_SetObjResult(interp, objPtr);
		return TCL_OK;
	    }
	} else {
	    TkTextChanged(textPtr->sharedTextPtr, NULL, &index, &index);

	    /*
	     * It's probably not true that all window configuration can change
	     * the line height, so we could be more efficient here and only
	     * call this when necessary.
	     */

	    TkTextInvalidateLineMetrics(textPtr->sharedTextPtr, NULL,
		    index.linePtr, 0, TK_TEXT_INVALIDATE_ONLY);
	    return EmbImageConfigure(textPtr, eiPtr, objc-4, objv+4);
	}
    case CMD_CREATE: {
	int lineIndex;

	/*
	 * Add a new image. Find where to put the new image, and mark that
	 * position for redisplay.
	 */

	if (objc < 4) {
	    Tcl_WrongNumArgs(interp, 3, objv, "index ?-option value ...?");
	    return TCL_ERROR;
	}
	if (TkTextGetObjIndex(interp, textPtr, objv[3], &index) != TCL_OK) {
	    return TCL_ERROR;
	}

	/*
	 * Don't allow insertions on the last (dummy) line of the text.
	 */

	lineIndex = TkBTreeLinesTo(textPtr, index.linePtr);
	if (lineIndex == TkBTreeNumLines(textPtr->sharedTextPtr->tree,
		textPtr)) {
	    lineIndex--;
	    TkTextMakeByteIndex(textPtr->sharedTextPtr->tree, textPtr,
		    lineIndex, 1000000, &index);
	}

	/*
	 * Create the new image segment and initialize it.
	 */

	eiPtr = (TkTextSegment *)ckalloc(EI_SEG_SIZE);
	eiPtr->typePtr = &tkTextEmbImageType;
	eiPtr->size = 1;
	eiPtr->body.ei.sharedTextPtr = textPtr->sharedTextPtr;
	eiPtr->body.ei.linePtr = NULL;
	eiPtr->body.ei.imageNameObj = NULL;
	eiPtr->body.ei.imageObj = NULL;
	eiPtr->body.ei.name = NULL;
	eiPtr->body.ei.image = NULL;
	eiPtr->body.ei.align = TK_ALIGN_CENTER;
	eiPtr->body.ei.padXObj = eiPtr->body.ei.padYObj = NULL;
	eiPtr->body.ei.chunkCount = 0;
	eiPtr->body.ei.optionTable = Tk_CreateOptionTable(interp, optionSpecs);

	/*
	 * Link the segment into the text widget, then configure it (delete it
	 * again if the configuration fails).
	 */

	TkTextChanged(textPtr->sharedTextPtr, NULL, &index, &index);
	TkBTreeLinkSegment(eiPtr, &index);
	if (EmbImageConfigure(textPtr, eiPtr, objc-4, objv+4) != TCL_OK) {
	    TkTextIndex index2;

	    TkTextIndexForwChars(NULL, &index, 1, &index2, COUNT_INDICES);
	    TkBTreeDeleteIndexRange(textPtr->sharedTextPtr->tree, &index, &index2);
	    return TCL_ERROR;
	}
	TkTextInvalidateLineMetrics(textPtr->sharedTextPtr, NULL,
		index.linePtr, 0, TK_TEXT_INVALIDATE_ONLY);
	return TCL_OK;
    }
    case CMD_NAMES: {
	Tcl_HashSearch search;
	Tcl_HashEntry *hPtr;
	Tcl_Obj *resultObj;

	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 3, objv, NULL);
	    return TCL_ERROR;
	}
	resultObj = Tcl_NewObj();
	for (hPtr = Tcl_FirstHashEntry(&textPtr->sharedTextPtr->imageTable,
		&search); hPtr != NULL; hPtr = Tcl_NextHashEntry(&search)) {
	    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(
		    (const char *)Tcl_GetHashKey(&textPtr->sharedTextPtr->markTable, hPtr),
		    -1));
	}
	if (resultObj == NULL) {
	    return TCL_ERROR;
	} else {
	    Tcl_SetObjResult(interp, resultObj);
	    return TCL_OK;
	}
    }
    default:
	Tcl_Panic("unexpected switch fallthrough");
    }
    return TCL_ERROR;
}

/*
 *--------------------------------------------------------------
 *
 * EmbImageConfigure --
 *
 *	This function is called to handle configuration options for an
 *	embedded image, using an objc/objv list.
 *
 * Results:
 *	The return value is a standard Tcl result. If TCL_ERROR is returned,
 *	then the interp's result contains an error message..
 *
 * Side effects:
 *	Configuration information for the embedded image changes, such as
 *	alignment, or name of the image.
 *
 *--------------------------------------------------------------
 */

static int
EmbImageConfigure(
    TkText *textPtr,		/* Information about text widget that contains
				 * embedded image. */
    TkTextSegment *eiPtr,	/* Embedded image to be configured. */
    Tcl_Size objc,			/* Number of strings in objv. */
    Tcl_Obj *const objv[])	/* Array of strings describing configuration
				 * options. */
{
    Tk_Image image;
    Tcl_DString newName;
    Tcl_HashEntry *hPtr;
    char *name;
    int dummy, length;

    if (Tk_SetOptions(textPtr->interp, &eiPtr->body.ei,
	    eiPtr->body.ei.optionTable,
	    objc, objv, textPtr->tkwin, NULL, NULL) != TCL_OK) {
	return TCL_ERROR;
    }

    /*
     * Create the image. Save the old image around and don't free it until
     * after the new one is allocated. This keeps the reference count from
     * going to zero so the image doesn't have to be recreated if it hasn't
     * changed.
     */

    if (eiPtr->body.ei.imageObj != NULL) {
	image = Tk_GetImage(textPtr->interp, textPtr->tkwin,
		Tcl_GetString(eiPtr->body.ei.imageObj), EmbImageProc, eiPtr);
	if (image == NULL) {
	    return TCL_ERROR;
	}
    } else {
	image = NULL;
    }
    if (eiPtr->body.ei.image != NULL) {
	Tk_FreeImage(eiPtr->body.ei.image);
    }
    eiPtr->body.ei.image = image;

    if (eiPtr->body.ei.name != NULL) {
	return TCL_OK;
    }

    /*
     * Find a unique name for this image. Use imageName (or imageString) if
     * available, otherwise tack on a #nn and use it. If a name is already
     * associated with this image, delete the name.
     */

    if (eiPtr->body.ei.imageNameObj) {
	name = Tcl_GetString(eiPtr->body.ei.imageNameObj);
    } else if (eiPtr->body.ei.imageObj) {
	name = Tcl_GetString(eiPtr->body.ei.imageObj);
    } else {
	Tcl_SetObjResult(textPtr->interp, Tcl_NewStringObj(
		"Either a \"-name\" or a \"-image\" argument must be"
		" provided to the \"image create\" subcommand", TCL_INDEX_NONE));
	Tcl_SetErrorCode(textPtr->interp, "TK", "TEXT", "IMAGE_CREATE_USAGE",
		(char *)NULL);
	return TCL_ERROR;
    }

    Tcl_DStringInit(&newName);
    while (Tcl_FindHashEntry(&textPtr->sharedTextPtr->imageTable, name)) {
	char buf[4 + TCL_INTEGER_SPACE];
	snprintf(buf, sizeof(buf), "#%d", ++textPtr->sharedTextPtr->imageCount);
	Tcl_DStringSetLength(&newName, 0);
	Tcl_DStringAppend(&newName, name, TCL_INDEX_NONE);
	Tcl_DStringAppend(&newName, buf, TCL_INDEX_NONE);
	name = Tcl_DStringValue(&newName);
    }
    length = strlen(name);

    hPtr = Tcl_CreateHashEntry(&textPtr->sharedTextPtr->imageTable, name,
	    &dummy);
    Tcl_SetHashValue(hPtr, eiPtr);
    eiPtr->body.ei.name = (char *)ckalloc(length + 1);
    memcpy(eiPtr->body.ei.name, name, length + 1);
    Tcl_SetObjResult(textPtr->interp, Tcl_NewStringObj(name, TCL_INDEX_NONE));
    Tcl_DStringFree(&newName);

    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * EmbImageDeleteProc --
 *
 *	This function is invoked by the text B-tree code whenever an embedded
 *	image lies in a range of characters being deleted.
 *
 * Results:
 *	Returns 0 to indicate that the deletion has been accepted.
 *
 * Side effects:
 *	The embedded image is deleted, if it exists, and any resources
 *	associated with it are released.
 *
 *--------------------------------------------------------------
 */

static int
EmbImageDeleteProc(
    TkTextSegment *eiPtr,	/* Segment being deleted. */
    TCL_UNUSED(TkTextLine *),	/* Line containing segment. */
    TCL_UNUSED(int))		/* Non-zero means the entire tree is being
				 * deleted, so everything must get cleaned
				 * up. */
{
    Tcl_HashEntry *hPtr;

    if (eiPtr->body.ei.image != NULL) {
	hPtr = Tcl_FindHashEntry(&eiPtr->body.ei.sharedTextPtr->imageTable,
		eiPtr->body.ei.name);
	if (hPtr != NULL) {
	    /*
	     * (It's possible for there to be no hash table entry for this
	     * image, if an error occurred while creating the image segment
	     * but before the image got added to the table)
	     */

	    Tcl_DeleteHashEntry(hPtr);
	}
	Tk_FreeImage(eiPtr->body.ei.image);
    }

    /*
     * No need to supply a tkwin argument, since we have no window-specific
     * options.
     */

    Tk_FreeConfigOptions(&eiPtr->body.ei, eiPtr->body.ei.optionTable,
	    NULL);
    if (eiPtr->body.ei.name) {
	ckfree(eiPtr->body.ei.name);
    }
    ckfree(eiPtr);
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * EmbImageCleanupProc --
 *
 *	This function is invoked by the B-tree code whenever a segment
 *	containing an embedded image is moved from one line to another.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The linePtr field of the segment gets updated.
 *
 *--------------------------------------------------------------
 */

static TkTextSegment *
EmbImageCleanupProc(
    TkTextSegment *eiPtr,	/* Mark segment that's being moved. */
    TkTextLine *linePtr)	/* Line that now contains segment. */
{
    eiPtr->body.ei.linePtr = linePtr;
    return eiPtr;
}

/*
 *--------------------------------------------------------------
 *
 * EmbImageLayoutProc --
 *
 *	This function is the "layoutProc" for embedded image segments.
 *
 * Results:
 *	1 is returned to indicate that the segment should be displayed. The
 *	chunkPtr structure is filled in.
 *
 * Side effects:
 *	None, except for filling in chunkPtr.
 *
 *--------------------------------------------------------------
 */

static int
EmbImageLayoutProc(
    TkText *textPtr,		/* Text widget being layed out. */
    TCL_UNUSED(TkTextIndex *),	/* Identifies first character in chunk. */
    TkTextSegment *eiPtr,	/* Segment corresponding to indexPtr. */
    Tcl_Size offset,			/* Offset within segPtr corresponding to
				 * indexPtr (always 0). */
    int maxX,			/* Chunk must not occupy pixels at this
				 * position or higher. */
    TCL_UNUSED(Tcl_Size),	/* Chunk must not include more than this many
				 * characters. */
    int noCharsYet,		/* Non-zero means no characters have been
				 * assigned to this line yet. */
    TCL_UNUSED(TkWrapMode),	/* Wrap mode to use for line:
				 * TEXT_WRAPMODE_CHAR, TEXT_WRAPMODE_NONE, or
				 * TEXT_WRAPMODE_WORD. */
    TkTextDispChunk *chunkPtr)
				/* Structure to fill in with information about
				 * this chunk. The x field has already been
				 * set by the caller. */
{
    int width, height;
    int padX = 0, padY = 0;

    if (offset != 0) {
	Tcl_Panic("Non-zero offset in EmbImageLayoutProc");
    }

    if (eiPtr->body.ei.padXObj) {
	Tk_GetPixelsFromObj(NULL, textPtr->tkwin, eiPtr->body.ei.padXObj, &padX);
    }
    if (eiPtr->body.ei.padYObj) {
	Tk_GetPixelsFromObj(NULL, textPtr->tkwin, eiPtr->body.ei.padYObj, &padY);
    }
    /*
     * See if there's room for this image on this line.
     */

    if (eiPtr->body.ei.image == NULL) {
	width = 0;
	height = 0;
    } else {
	Tk_SizeOfImage(eiPtr->body.ei.image, &width, &height);
	width += 2 * padX;
	height += 2 * padY;
    }
    if ((width > (maxX - chunkPtr->x))
	    && !noCharsYet && (textPtr->wrapMode != TEXT_WRAPMODE_NONE)) {
	return 0;
    }

    /*
     * Fill in the chunk structure.
     */

    chunkPtr->displayProc = EmbImageDisplayProc;
    chunkPtr->undisplayProc = NULL;
    chunkPtr->measureProc = NULL;
    chunkPtr->bboxProc = EmbImageBboxProc;
    chunkPtr->numBytes = 1;
    if (eiPtr->body.ei.align == TK_ALIGN_BASELINE) {
	chunkPtr->minAscent = height - padY;
	chunkPtr->minDescent = padY;
	chunkPtr->minHeight = 0;
    } else {
	chunkPtr->minAscent = 0;
	chunkPtr->minDescent = 0;
	chunkPtr->minHeight = height;
    }
    chunkPtr->width = width;
    chunkPtr->breakIndex = -1;
    chunkPtr->breakIndex = 1;
    chunkPtr->clientData = eiPtr;
    eiPtr->body.ei.chunkCount += 1;
    return 1;
}

/*
 *--------------------------------------------------------------
 *
 * EmbImageCheckProc --
 *
 *	This function is invoked by the B-tree code to perform consistency
 *	checks on embedded images.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The function panics if it detects anything wrong with the embedded
 *	image.
 *
 *--------------------------------------------------------------
 */

static void
EmbImageCheckProc(
    TkTextSegment *eiPtr,	/* Segment to check. */
    TCL_UNUSED(TkTextLine *))	/* Line containing segment. */
{
    if (eiPtr->nextPtr == NULL) {
	Tcl_Panic("EmbImageCheckProc: embedded image is last segment in line");
    }
    if (eiPtr->size != 1) {
	Tcl_Panic("EmbImageCheckProc: embedded image has size %d",
		(int)eiPtr->size);
    }
}

/*
 *--------------------------------------------------------------
 *
 * EmbImageDisplayProc --
 *
 *	This function is invoked by the text displaying code when it is time
 *	to actually draw an embedded image chunk on the screen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The embedded image gets moved to the correct location and drawn onto
 *	the display.
 *
 *--------------------------------------------------------------
 */

static void
EmbImageDisplayProc(
    TkText *textPtr,
    TkTextDispChunk *chunkPtr,	/* Chunk that is to be drawn. */
    int x,			/* X-position in dst at which to draw this
				 * chunk (differs from the x-position in the
				 * chunk because of scrolling). */
    int y,			/* Top of rectangular bounding box for line:
				 * tells where to draw this chunk in dst
				 * (x-position is in the chunk itself). */
    int lineHeight,		/* Total height of line. */
    int baseline,		/* Offset of baseline from y. */
    TCL_UNUSED(Display *),	/* Display to use for drawing. */
    Drawable dst,		/* Pixmap or window in which to draw */
    TCL_UNUSED(int))	/* Y-coordinate in text window that
				 * corresponds to y. */
{
    TkTextSegment *eiPtr = (TkTextSegment *)chunkPtr->clientData;
    int lineX, imageX, imageY, width, height;
    Tk_Image image;

    image = eiPtr->body.ei.image;
    if (image == NULL) {
	return;
    }
    if ((x + chunkPtr->width) <= 0) {
	return;
    }

    /*
     * Compute the image's location and size in the text widget, taking into
     * account the align value for the image.
     */

    EmbImageBboxProc(textPtr, chunkPtr, 0, y, lineHeight, baseline, &lineX,
	    &imageY, &width, &height);
    imageX = lineX - chunkPtr->x + x;

    Tk_RedrawImage(image, 0, 0, width, height, dst, imageX, imageY);
}

/*
 *--------------------------------------------------------------
 *
 * EmbImageBboxProc --
 *
 *	This function is called to compute the bounding box of the area
 *	occupied by an embedded image.
 *
 * Results:
 *	There is no return value. *xPtr and *yPtr are filled in with the
 *	coordinates of the upper left corner of the image, and *widthPtr and
 *	*heightPtr are filled in with the dimensions of the image in pixels.
 *	Note: not all of the returned bbox is necessarily visible on the
 *	screen (the rightmost part might be off-screen to the right, and the
 *	bottommost part might be off-screen to the bottom).
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

static void
EmbImageBboxProc(
    TkText *textPtr,
    TkTextDispChunk *chunkPtr,	/* Chunk containing desired char. */
    TCL_UNUSED(Tcl_Size),			/* Index of desired character within the
				 * chunk. */
    int y,			/* Topmost pixel in area allocated for this
				 * line. */
    int lineHeight,		/* Total height of line. */
    int baseline,		/* Location of line's baseline, in pixels
				 * measured down from y. */
    int *xPtr, int *yPtr,	/* Gets filled in with coords of character's
				 * upper-left pixel. */
    int *widthPtr,		/* Gets filled in with width of image, in
				 * pixels. */
    int *heightPtr)		/* Gets filled in with height of image, in
				 * pixels. */
{
    TkTextSegment *eiPtr = (TkTextSegment *)chunkPtr->clientData;
    Tk_Image image;
    int padX = 0, padY = 0;

    image = eiPtr->body.ei.image;
    if (image != NULL) {
	Tk_SizeOfImage(image, widthPtr, heightPtr);
    } else {
	*widthPtr = 0;
	*heightPtr = 0;
    }

    if (eiPtr->body.ei.padXObj) {
	Tk_GetPixelsFromObj(NULL, textPtr->tkwin, eiPtr->body.ei.padXObj, &padX);
    }
    if (eiPtr->body.ei.padYObj) {
	Tk_GetPixelsFromObj(NULL, textPtr->tkwin, eiPtr->body.ei.padYObj, &padY);
    }
    *xPtr = chunkPtr->x + padX;

    switch (eiPtr->body.ei.align) {
    case TK_ALIGN_BOTTOM:
	*yPtr = y + (lineHeight - *heightPtr - padY);
	break;
    case TK_ALIGN_CENTER:
	*yPtr = y + (lineHeight - *heightPtr)/2;
	break;
    case TK_ALIGN_TOP:
	*yPtr = y + padY;
	break;
    case TK_ALIGN_BASELINE:
	*yPtr = y + (baseline - *heightPtr);
	break;
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkTextImageIndex --
 *
 *	Given the name of an embedded image within a text widget, returns an
 *	index corresponding to the image's position in the text.
 *
 * Results:
 *	The return value is TCL_OK if there is an embedded image by the given
 *	name in the text widget, TCL_ERROR otherwise. If the image exists,
 *	*indexPtr is filled in with its index.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

int
TkTextImageIndex(
    TkText *textPtr,		/* Text widget containing image. */
    const char *name,		/* Name of image. */
    TkTextIndex *indexPtr)	/* Index information gets stored here. */
{
    Tcl_HashEntry *hPtr;
    TkTextSegment *eiPtr;

    if (textPtr == NULL) {
	return TCL_ERROR;
    }

    hPtr = Tcl_FindHashEntry(&textPtr->sharedTextPtr->imageTable, name);
    if (hPtr == NULL) {
	return TCL_ERROR;
    }
    eiPtr = (TkTextSegment *)Tcl_GetHashValue(hPtr);
    indexPtr->tree = textPtr->sharedTextPtr->tree;
    indexPtr->linePtr = eiPtr->body.ei.linePtr;
    indexPtr->byteIndex = TkTextSegToOffset(eiPtr, indexPtr->linePtr);

    /*
     * If indexPtr refers to somewhere outside the -startline/-endline
     * range limits of the widget, error out since the image indeed is not
     * reachable from this text widget (it may be reachable from a peer).
     */

    if (TkTextIndexAdjustToStartEnd(textPtr, indexPtr, 1) == TCL_ERROR) {
	return TCL_ERROR;
    }

    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * EmbImageProc --
 *
 *	This function is called by the image code whenever an image or its
 *	contents changes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The image will be redisplayed.
 *
 *--------------------------------------------------------------
 */

static void
EmbImageProc(
    void *clientData,	/* Pointer to widget record. */
    TCL_UNUSED(int),		/* Upper left pixel (within image) that must
				 * be redisplayed. */
    TCL_UNUSED(int),
    TCL_UNUSED(int),	/* Dimensions of area to redisplay (may be
				 * <= 0). */
    TCL_UNUSED(int),
    TCL_UNUSED(int),/* New dimensions of image. */
    TCL_UNUSED(int))

{
    TkTextSegment *eiPtr = (TkTextSegment *)clientData;
    TkTextIndex index;

    index.tree = eiPtr->body.ei.sharedTextPtr->tree;
    index.linePtr = eiPtr->body.ei.linePtr;
    index.byteIndex = TkTextSegToOffset(eiPtr, eiPtr->body.ei.linePtr);
    TkTextChanged(eiPtr->body.ei.sharedTextPtr, NULL, &index, &index);

    /*
     * It's probably not true that all image changes can change the line
     * height, so we could be more efficient here and only call this when
     * necessary.
     */

    TkTextInvalidateLineMetrics(eiPtr->body.ei.sharedTextPtr, NULL,
	    index.linePtr, 0, TK_TEXT_INVALIDATE_ONLY);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
