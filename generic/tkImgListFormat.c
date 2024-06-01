/*
 * tkImgListFormat.c --
 *
 *      Implements the default image data format. I.e. the format used for
 *      [imageName data] and [imageName put] if no other format is specified.
 *
 *      The default format consits of a list of scan lines (rows) with each
 *      list element being itself a list of pixels (or columns). For details,
 *      see the manpage photo.n
 *
 *      This image format cannot read/write files, it is meant for string
 *      data only.
 *
 *
 * Copyright © 1994 The Australian National University.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 * Copyright © 2002-2003 Donal K. Fellows
 * Copyright © 2003 ActiveState Corporation.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * Authors:
 *      Paul Mackerras (paulus@cs.anu.edu.au),
 *              Department of Computer Science,
 *              Australian National University.
 *
 *      Simon Bachmann (simonbachmann@bluewin.ch)
 */


#include "tkImgPhoto.h"

#ifdef _WIN32
#include "tkWinInt.h"
#endif

/*
 * Message to generate when an attempt to allocate memory for an image fails.
 */

#define TK_PHOTO_ALLOC_FAILURE_MESSAGE \
        "not enough free memory for image buffer"


/*
 * Color name length limit: do not attempt to parse as color strings that are
 * longer than this limit
 */

#define TK_PHOTO_MAX_COLOR_LENGTH 99

/*
 * Symbols for the different formats of a color string.
 */

enum ColorFormatType {
    COLORFORMAT_TKCOLOR,
    COLORFORMAT_EMPTYSTRING,
    COLORFORMAT_LIST,
    COLORFORMAT_RGB1,
    COLORFORMAT_RGB2,
    COLORFORMAT_RGBA1,
    COLORFORMAT_RGBA2
};

/*
 * Names for the color format types above.
 * Order must match the one in enum ColorFormatType
 */

static const char *const colorFormatNames[] = {
    "tkcolor",
    "emptystring",
    "list",
    "rgb-short",
    "rgb",
    "rgba-short",
    "rgba",
    NULL
};

/*
 * The following data structure is used to return information from
 * ParseFormatOptions:
 */

struct FormatOptions {
    int options;         /* Individual bits indicate which options were
                          * specified - see below. */
    Tcl_Obj *formatName; /* Name specified without an option. */
    enum ColorFormatType colorFormat;
                         /* The color format type given with the
                          * -colorformat option */
};

/*
 * Bit definitions for use with ParseFormatOptions: each bit is set in the
 * allowedOptions parameter on a call to ParseFormatOptions if that option
 * is allowed for the current photo image subcommand. On return, the bit is
 * set in the options field of the FormatOptions structure if that option
 * was specified.
 *
 * OPT_COLORFORMAT:         Set if -alpha option allowed/specified.
 */

#define OPT_COLORFORMAT     1

/*
 * List of format option names. The order here must match the order of
 * declarations of the FMT_OPT_* constants above.
 */

static const char *const formatOptionNames[] = {
    "-colorformat",
    NULL
};

/*
 * Forward declarations
 */

static int      ParseFormatOptions(Tcl_Interp *interp, int allowedOptions,
                    Tcl_Size objc, Tcl_Obj *const objv[], Tcl_Size *indexPtr,
                    struct FormatOptions *optPtr);
static Tcl_Obj  *GetBadOptMsg(const char *badValue, int allowedOpts);
static int      StringMatchDef(Tcl_Obj *data, Tcl_Obj *formatString,
                    int *widthPtr, int *heightPtr, Tcl_Interp *interp);
static int      StringReadDef(Tcl_Interp *interp, Tcl_Obj *data,
                    Tcl_Obj *formatString, Tk_PhotoHandle imageHandle,
                    int destX, int destY, int width, int height,
                    int srcX, int srcY);
static int      StringWriteDef(Tcl_Interp *interp,
                    Tcl_Obj *formatString,
                    Tk_PhotoImageBlock *blockPtr);
static int      ParseColor(Tcl_Interp *interp, Tcl_Obj *specObj,
                    Display *display, Colormap colormap, unsigned char *redPtr,
                    unsigned char *greenPtr, unsigned char *bluePtr,
                    unsigned char *alphaPtr);
static int      ParseColorAsList(const char *colorString, unsigned char *redPtr,
                    unsigned char *greenPtr, unsigned char *bluePtr,
                    unsigned char *alphaPtr);
static int      ParseColorAsHex(Tcl_Interp *interp, const char *colorString,
                    int colorStrLen, Display *display, Colormap colormap,
                    unsigned char *redPtr, unsigned char *greenPtr,
                    unsigned char *bluePtr, unsigned char *alphaPtr);
static int      ParseColorAsStandard(Tcl_Interp *interp,
                    const char *colorString, int colorStrLen,
                    Display *display, Colormap colormap,
                    unsigned char *redPtr, unsigned char *greenPtr,
                    unsigned char *bluePtr, unsigned char *alphaPtr);

/*
 * The format record for the default image handler
 */

Tk_PhotoImageFormat tkImgFmtDefault = {
    "default",      /* name */
    NULL,           /* fileMatchProc: format doesn't support file ops */
    StringMatchDef, /* stringMatchProc */
    NULL,           /* fileReadProc: format doesn't support file read */
    StringReadDef,  /* stringReadProc */
    NULL,           /* fileWriteProc: format doesn't support file write */
    StringWriteDef, /* stringWriteProc */
    NULL            /* nextPtr */
};

/*
 *----------------------------------------------------------------------
 *
 * ParseFormatOptions --
 *
 *      Parse the options passed to the image format handler.
 *
 * Results:
 *      On success, the structure pointed to by optPtr is filled with the
 *      values passed or with the defaults and TCL_OK returned.
 *      If an error occurs, leaves an error message in interp and returns
 *      TCL_ERROR.
 *
 * Side effects:
 *      The value in *indexPtr is updated to the index of the fist
 *      element in argv[] that does not look like an option/value, or to
 *      argc if parsing reached the end of argv[].
 *
 *----------------------------------------------------------------------
 */
static int
ParseFormatOptions(
    Tcl_Interp *interp,               /* For error messages */
    int allowedOptions,               /* Bitfield specifying which options are
                                       * to be considered allowed */
    Tcl_Size objc,                         /* Number of elements in argv[] */
    Tcl_Obj *const objv[],            /* The arguments to parse */
    Tcl_Size *indexPtr,                    /* Index giving the first element to
                                       * parse. The value is updated to the
                                       * index where parsing ended */
    struct FormatOptions *optPtr)     /* Parsed option values are written to
                                       * this struct */

{
    Tcl_Size optIndex, index;
    int first, typeIndex;
    const char *option;

    first = 1;

    /*
     * Fill in default values
     */
    optPtr->options = 0;
    optPtr->formatName = NULL;
    optPtr->colorFormat = COLORFORMAT_RGB2;
    for (index = *indexPtr; index < objc; *indexPtr = ++index) {
        int optionExists;

        /*
         * The first value can be the format handler's name. It goes to
         * optPtr->name.
         */
        option = Tcl_GetString(objv[index]);
        if (option[0] != '-') {
            if (first) {
                optPtr->formatName = objv[index];
                first = 0;
                continue;
            } else {
                break;
            }
        }
        first = 0;

        /*
         * Check if option is known and allowed
         */

        optionExists = 1;
        if (Tcl_GetIndexFromObj(NULL, objv[index], formatOptionNames,
                "format option", 0, &optIndex) != TCL_OK) {
            optionExists = 0;
        }
        if (!optionExists || !((1 << optIndex) & allowedOptions)) {
            Tcl_SetObjResult(interp, GetBadOptMsg(Tcl_GetString(objv[index]),
                    allowedOptions));
            Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "BAD_OPTION", NULL);
            return TCL_ERROR;
        }

        /*
         * Option-specific checks
         */

        switch (1 << optIndex) {
        case OPT_COLORFORMAT:
            *indexPtr = ++index;
            if (index >= objc) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("the \"%s\" option "
                        "requires a value", Tcl_GetString(objv[index - 1])));
                Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
                        "MISSING_VALUE", NULL);
                return TCL_ERROR;
            }
            if (Tcl_GetIndexFromObj(NULL, objv[index], colorFormatNames, "",
                    TCL_EXACT, &typeIndex) != TCL_OK
                    || (typeIndex != COLORFORMAT_LIST
                    && typeIndex != COLORFORMAT_RGB2
                    && typeIndex != COLORFORMAT_RGBA2) ) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad color format "
                        "\"%s\": must be rgb, rgba, or list",
                        Tcl_GetString(objv[index])));
                Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
                        "BAD_COLOR_FORMAT", NULL);
                return TCL_ERROR;
            }
            optPtr->colorFormat = (enum ColorFormatType)typeIndex;
            break;
        default:
            Tcl_Panic("ParseFormatOptions: unexpected switch fallthrough");
        }

        /*
         * Add option to bitfield in optPtr
         */
        optPtr->options |= (1 << optIndex);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 *  GetBadOptMsg --
 *
 *      Build a Tcl_Obj containing an error message in the form "bad option
 *      "xx": must be y, or z", based on the bits set in allowedOpts.
 *
 * Results:
 *      A Tcl Object containig the error message.
 *
 * Side effects:
 *      None
 *----------------------------------------------------------------------
 */
static Tcl_Obj *
GetBadOptMsg(
    const char *badValue,   /* the erroneous option */
    int allowedOpts)        /* bitfield specifying the allowed options */
{
    int i, bit;
    Tcl_Obj *resObj = Tcl_ObjPrintf("bad format option \"%s\": ", badValue);

    if (allowedOpts == 0) {
        Tcl_AppendToObj(resObj, "no options allowed", TCL_INDEX_NONE);
    } else {
        Tcl_AppendToObj(resObj, "must be ", TCL_INDEX_NONE);
        bit = 1;
        for (i = 0; formatOptionNames[i] != NULL; i++) {
            if (allowedOpts & bit) {
                if (allowedOpts & (bit -1)) {
                    /*
                     * not the first option
                     */
                    if (allowedOpts & ~((bit << 1) - 1)) {
                        /*
                         * not the last option
                         */
                        Tcl_AppendToObj(resObj, ", ", TCL_INDEX_NONE);
                    } else {
                        Tcl_AppendToObj(resObj, ", or ", TCL_INDEX_NONE);
                    }
                }
                Tcl_AppendToObj(resObj, formatOptionNames[i], TCL_INDEX_NONE);
            }
            bit <<=1;
        }
    }
    return resObj;
}

/*
 *----------------------------------------------------------------------
 *
 * StringMatchDef --
 *
 *      Default string match function. Test if image data in string form
 *      appears to be in the default list-of-list-of-pixel-data format
 *      accepted by the "<img> put" command.
 *
 * Results:
 *      If thte data is in the default format, writes the size of the image
 *      to widthPtr and heightPtr and returns 1. Otherwise, leaves an error
 *      message in interp (if not NULL) and returns 0.
 *      Note that this function does not parse all data points. A return
 *      value of 1 does not guarantee that the data can be read without
 *      errors.
 *
 * Side effects:
 *      None
 *----------------------------------------------------------------------
 */
static int
StringMatchDef(
    Tcl_Obj *data,          /* The data to check */
    TCL_UNUSED(Tcl_Obj *),  /* Value of the -format option, not used here */
    int *widthPtr,          /* Width of image is written to this location */
    int *heightPtr,         /* Height of image is written to this location */
    Tcl_Interp *interp)     /* Error messages are left in this interpreter */
{
    Tcl_Size y, rowCount, colCount, curColCount;
    unsigned char dummy;
    Tcl_Obj **rowListPtr, *pixelData;

    /*
     * See if data can be parsed as a list, if every element is itself a valid
     * list and all sublists have the same length.
     */

    if (Tcl_ListObjGetElements(interp, data, &rowCount, &rowListPtr)
            != TCL_OK) {
        return 0;
    }
    if (rowCount == 0) {
        /*
         * empty list is valid data
         */

        *widthPtr = 0;
        *heightPtr = 0;
        return 1;
    }
    colCount = -1;
    for (y = 0; y < rowCount; y++) {
        if (Tcl_ListObjLength(interp, rowListPtr[y], &curColCount) != TCL_OK) {
            return 0;
        }
        if (colCount < 0) {
            colCount = curColCount;
        } else if (curColCount != colCount) {
            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("invalid row # %" TCL_SIZE_MODIFIER "d: "
                        "all rows must have the same number of elements", y));
                Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
                        "INVALID_DATA", NULL);
            }
            return 0;
        }
    }

    /*
     * Data in base64 encoding (or even binary data), might actually pass
     * these tests. To avoid parsing it as list of lists format, check one
     * pixel for validity.
     */
    if (Tcl_ListObjIndex(interp, rowListPtr[0], 0, &pixelData) != TCL_OK) {
        return 0;
    }
    (void)Tcl_GetString(pixelData);
    if (pixelData->length > TK_PHOTO_MAX_COLOR_LENGTH) {
        return 0;
    }
    if (ParseColor(interp, pixelData, Tk_Display(Tk_MainWindow(interp)),
            Tk_Colormap(Tk_MainWindow(interp)), &dummy, &dummy, &dummy, &dummy)
            != TCL_OK) {
        return 0;
    }

    /*
     * Looks like we have valid data for this format.
     * We do not check any pixel values - that's the job of ImgStringRead()
     */

    *widthPtr = colCount;
    *heightPtr = rowCount;

    return 1;

}

/*
 *----------------------------------------------------------------------
 *
 * StringReadDef --
 *
 *      String read function for default format. (see manpage for details on
 *      the format).
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      If the data has valid format, write it to the image identified by
 *      imageHandle.
 *      If the image data cannot be parsed, an error message is left in
 *      interp.
 *
 *----------------------------------------------------------------------
*/

static int
StringReadDef(
    Tcl_Interp *interp,         /* leave error messages here */
    Tcl_Obj *data,              /* the data to parse */
    Tcl_Obj *formatString,      /* value of the -format option */
    Tk_PhotoHandle imageHandle, /* write data to this image */
    int destX, int destY,       /* start writing data at this point
                                 * in destination image*/
    int width, int height,      /* dimensions of area to write to */
    int srcX, int srcY)         /* start reading source data at these
                                 * coordinates */
{
    Tcl_Obj **rowListPtr, **colListPtr;
    Tcl_Obj **objv;
    Tcl_Size objc, rowCount, colCount, curColCount;
    unsigned char *curPixelPtr;
    int x, y;
    Tk_PhotoImageBlock srcBlock;
    Display *display;
    Colormap colormap;
    struct FormatOptions opts;
    Tcl_Size optIndex;

    /*
     * Parse format suboptions
     * We don't use any format suboptions, but we still need to provide useful
     * error messages if suboptions were specified.
     */

    memset(&opts, 0, sizeof(opts));
    if (formatString != NULL) {
        if (Tcl_ListObjGetElements(interp, formatString, &objc, &objv)
                != TCL_OK) {
            return TCL_ERROR;
        }
        optIndex = 0;
        if (ParseFormatOptions(interp, 0, objc, objv, &optIndex, &opts)
                != TCL_OK) {
            return TCL_ERROR;
        }
        if (optIndex < objc) {
            Tcl_SetObjResult(interp,
                    GetBadOptMsg(Tcl_GetString(objv[optIndex]), 0));
            Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "BAD_OPTION", NULL);
            return TCL_ERROR;
        }
    }

    /*
     * Check input data
     */

    if (Tcl_ListObjGetElements(interp, data, &rowCount, &rowListPtr)
            != TCL_OK ) {
        return TCL_ERROR;
    }
    if ( rowCount > 0 && Tcl_ListObjLength(interp, rowListPtr[0], &colCount)
            != TCL_OK) {
        return TCL_ERROR;
    }
    if (width <= 0 || height <= 0 || rowCount == 0 || colCount == 0) {
        /*
         * No changes with zero sized input or zero sized output region
         */

        return TCL_OK;
    }
    if (srcX < 0 || srcY < 0 || srcX >= rowCount || srcY >= colCount) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("source coordinates out of range"));
        Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "COORDINATES", NULL);
        return TCL_ERROR;
    }

    /*
     * Memory allocation overflow protection.
     * May not be able to trigger/ demo / test this.
     */

    if (colCount > (int)(UINT_MAX / 4 / rowCount)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                        "photo image dimensions exceed Tcl memory limits"));
        Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
                "OVERFLOW", NULL);
        return TCL_OK;
    }

    /*
     * Read data and put it to imageHandle
     */

    srcBlock.width = colCount - srcX;
    srcBlock.height = rowCount - srcY;
    srcBlock.pixelSize = 4;
    srcBlock.pitch = srcBlock.width * 4;
    srcBlock.offset[0] = 0;
    srcBlock.offset[1] = 1;
    srcBlock.offset[2] = 2;
    srcBlock.offset[3] = 3;
    srcBlock.pixelPtr = (unsigned char *)attemptckalloc(srcBlock.pitch * srcBlock.height);
    if (srcBlock.pixelPtr == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf(TK_PHOTO_ALLOC_FAILURE_MESSAGE));
        Tcl_SetErrorCode(interp, "TK", "MALLOC", NULL);
        return TCL_ERROR;
    }
    curPixelPtr = srcBlock.pixelPtr;
    display = Tk_Display(Tk_MainWindow(interp));
    colormap = Tk_Colormap(Tk_MainWindow(interp));
    for (y = srcY; y < rowCount; y++) {
        /*
         * We don't test the length of row, as that's been done in
         * ImgStringMatch()
         */

        if (Tcl_ListObjGetElements(interp, rowListPtr[y], &curColCount,
                &colListPtr) != TCL_OK) {
            goto errorExit;
        }
        for (x = srcX; x < colCount; x++) {
            if (ParseColor(interp, colListPtr[x], display, colormap,
                    curPixelPtr, curPixelPtr + 1, curPixelPtr + 2,
                    curPixelPtr + 3) != TCL_OK) {
                goto errorExit;
            }
            curPixelPtr += 4;
        }
    }

    /*
     * Write image data to destHandle
     */
    if (Tk_PhotoPutBlock(interp, imageHandle, &srcBlock, destX, destY,
            width, height, TK_PHOTO_COMPOSITE_SET) != TCL_OK) {
        goto errorExit;
    }

    ckfree(srcBlock.pixelPtr);

    return TCL_OK;

  errorExit:
    ckfree(srcBlock.pixelPtr);

    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * StringWriteDef --
 *
 *      String write function for default image data format. See the user
 *      documentation for details.
 *
 * Results:
 *      The converted data is set as the result of interp. Returns a standard
 *      Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
StringWriteDef(
    Tcl_Interp *interp,                 /* For the result and errors */
    Tcl_Obj *formatString,              /* The value of the -format option */
    Tk_PhotoImageBlock *blockPtr)       /* The image data to convert */
{
    int greenOffset, blueOffset, alphaOffset, hasAlpha;
    Tcl_Obj *result, **objv = NULL;
    Tcl_Size objc, allowedOpts, optIndex;
    struct FormatOptions opts;

    /*
     * Parse format suboptions
     */
    if (Tcl_ListObjGetElements(interp, formatString, &objc, &objv)
            != TCL_OK) {
        return TCL_ERROR;
    }
    allowedOpts = OPT_COLORFORMAT;
    optIndex = 0;
    if (ParseFormatOptions(interp, allowedOpts, objc, objv, &optIndex, &opts)
            != TCL_OK) {
        return TCL_ERROR;
    }
    if (optIndex < objc) {
        Tcl_SetObjResult(interp,
                GetBadOptMsg(Tcl_GetString(objv[optIndex]), allowedOpts));
        Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO", "BAD_OPTION", NULL);
        return TCL_ERROR;
    }

    greenOffset = blockPtr->offset[1] - blockPtr->offset[0];
    blueOffset = blockPtr->offset[2] - blockPtr->offset[0];

    /*
     * A negative alpha offset signals that the image is fully opaque.
     * That's not really documented anywhere, but it's the way it is!
     */

    if (blockPtr->offset[3] < 0) {
        hasAlpha = 0;
        alphaOffset = 0;
    } else {
        hasAlpha = 1;
        alphaOffset = blockPtr->offset[3] - blockPtr->offset[0];
    }

    if ((blockPtr->width > 0) && (blockPtr->height > 0)) {
        int row, col;
        Tcl_DString data, line;
        char colorBuf[11];
        unsigned char *pixelPtr;
        unsigned char alphaVal = 255;

        Tcl_DStringInit(&data);
        for (row=0; row<blockPtr->height; row++) {
            pixelPtr = blockPtr->pixelPtr + blockPtr->offset[0]
                    + row * blockPtr->pitch;
            Tcl_DStringInit(&line);
            for (col=0; col<blockPtr->width; col++) {
                if (hasAlpha) {
                    alphaVal = pixelPtr[alphaOffset];
                }

                /*
                 * We don't build lines as a list for #RGBA and #RGB. Since
                 * these color formats look like comments, the first element
                 * of the list would get quoted with an additional {} .
                 * While this is not a problem if the data is used as
                 * a list, it would cause problems if someone decides to parse
                 * it as a string (and it looks kinda strange)
                 */

                switch (opts.colorFormat) {
                case COLORFORMAT_RGB2:
                    snprintf(colorBuf, sizeof(colorBuf), "#%02x%02x%02x ",  pixelPtr[0],
                            pixelPtr[greenOffset], pixelPtr[blueOffset]);
                    Tcl_DStringAppend(&line, colorBuf, TCL_INDEX_NONE);
                    break;
                case COLORFORMAT_RGBA2:
                    snprintf(colorBuf, sizeof(colorBuf), "#%02x%02x%02x%02x ",
                            pixelPtr[0], pixelPtr[greenOffset],
                            pixelPtr[blueOffset], alphaVal);
                    Tcl_DStringAppend(&line, colorBuf, TCL_INDEX_NONE);
                    break;
                case COLORFORMAT_LIST:
                    Tcl_DStringStartSublist(&line);
                    snprintf(colorBuf, sizeof(colorBuf), "%d", pixelPtr[0]);
                    Tcl_DStringAppendElement(&line, colorBuf);
                    snprintf(colorBuf, sizeof(colorBuf), "%d", pixelPtr[greenOffset]);
                    Tcl_DStringAppendElement(&line, colorBuf);
                    snprintf(colorBuf, sizeof(colorBuf), "%d", pixelPtr[blueOffset]);
                    Tcl_DStringAppendElement(&line, colorBuf);
                    snprintf(colorBuf, sizeof(colorBuf), "%d", alphaVal);
                    Tcl_DStringAppendElement(&line, colorBuf);
                    Tcl_DStringEndSublist(&line);
                    break;
                default:
                    Tcl_Panic("unexpected switch fallthrough");
                }
                pixelPtr += blockPtr->pixelSize;
            }
            if (opts.colorFormat != COLORFORMAT_LIST) {
                /*
                 * For the #XXX formats, we need to remove the last
                 * whitespace.
                 */

                *(Tcl_DStringValue(&line) + Tcl_DStringLength(&line) - 1)
                        = '\0';
            }
            Tcl_DStringAppendElement(&data, Tcl_DStringValue(&line));
            Tcl_DStringFree(&line);
        }
        result = Tcl_NewStringObj(Tcl_DStringValue(&data), TCL_INDEX_NONE);
        Tcl_DStringFree(&data);
    } else {
        result = Tcl_NewObj();
    }

    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ParseColor --
 *
 *      This function extracts color and alpha values from a string. It
 *      understands standard Tk color formats, alpha suffixes and the color
 *      formats specific to photo images, which include alpha data.
 *
 * Results:
 *      On success, writes red, green, blue and alpha values to the
 *      corresponding pointers. If the color spec contains no alpha
 *      information, 255 is taken as transparency value.
 *      If the input cannot be parsed, leaves an error message in
 *      interp. Returns a standard Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static int
ParseColor(
    Tcl_Interp *interp,         /* error messages go there */
    Tcl_Obj *specObj,           /* the color data to parse */
    Display *display,           /* display of main window, needed to parse
                                 * standard Tk colors */
    Colormap colormap,          /* colormap of current display */
    unsigned char *redPtr,      /* the result is written to these pointers */
    unsigned char *greenPtr,
    unsigned char *bluePtr,
    unsigned char *alphaPtr)
{
    const char *specString;
    Tcl_Size length;

    /*
     * Find out which color format we have
     */

    specString = Tcl_GetStringFromObj(specObj, &length);

    if (length == 0) {
        /* Empty string */
        *redPtr = *greenPtr = *bluePtr = *alphaPtr = 0;
        return TCL_OK;
    }
    if (length > TK_PHOTO_MAX_COLOR_LENGTH) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("invalid color"));
        Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
                "INVALID_COLOR", NULL);
        return TCL_ERROR;
    }
    if (specString[0] == '#') {
        return ParseColorAsHex(interp, specString, length, display,
                colormap, redPtr, greenPtr, bluePtr, alphaPtr);
    }
    if (ParseColorAsList(specString,
            redPtr, greenPtr, bluePtr, alphaPtr) == TCL_OK) {
        return TCL_OK;
    }

    /*
     * Parsing the color as standard Tk color always is the last option tried
     * because TkParseColor() is very slow with values it cannot parse.
     */

    Tcl_ResetResult(interp);
    return ParseColorAsStandard(interp, specString, length, display,
            colormap, redPtr, greenPtr, bluePtr, alphaPtr);

}

/*
 *----------------------------------------------------------------------
 *
 * ParseColorAsList --
 *
 *      This function extracts color and alpha values from a list of 3 or 4
 *      integers (the list color format).
 *
 * Results:
 *      On success, writes red, green, blue and alpha values to the
 *      corresponding pointers. If the color spec contains no alpha
 *      information, 255 is taken as transparency value.
 *      Returns a standard Tcl result.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
static int
ParseColorAsList(
    const char *colorString,    /* the color data to parse */
    unsigned char *redPtr,      /* the result is written to these pointers */
    unsigned char *greenPtr,
    unsigned char *bluePtr,
    unsigned char *alphaPtr)
{
    /*
     * This is kinda ugly. The code would be certainly nicer if it
     * used Tcl_ListObjGetElements() and Tcl_GetIntFromObj(). But with
     * strtol() it's *much* faster.
     */

    const char *curPos;
    int values[4];
    int i;

    curPos = colorString;
    i = 0;

    /*
     * strtol can give false positives with a sequence of space chars.
     * To avoid that, advance the pointer to the next non-blank char.
     */

    while(isspace(UCHAR(*curPos))) {
        ++curPos;
    }
    while (i < 4 && *curPos != '\0') {
        values[i] = strtol(curPos, (char **)&curPos, 0);
        if (values[i] < 0 || values[i] > 255) {
            return TCL_ERROR;
        }
        while(isspace(UCHAR(*curPos))) {
            ++curPos;
        }
        ++i;
    }

    if (i < 3 || *curPos != '\0') {
        return TCL_ERROR;
    }
    if (i < 4) {
        values[3] = 255;
    }

    *redPtr = (unsigned char) values[0];
    *greenPtr = (unsigned char) values[1];
    *bluePtr = (unsigned char) values[2];
    *alphaPtr = (unsigned char) values[3];

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ParseColorAsHex --
 *
 *      This function extracts color and alpha values from a string
 *      starting with '#', followed by hex digits. It undestands both
 *      the #RGBA form and the #RBG (with optional suffix)
 *
 * Results:
 *      On success, writes red, green, blue and alpha values to the
 *      corresponding pointers. If the color spec contains no alpha
 *      information, 255 is taken as transparency value.
 *      Returns a standard Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static int
ParseColorAsHex(
    Tcl_Interp *interp,         /* error messages are left here */
    const char *colorString,    /* the color data to parse */
    int colorStrLen,            /* length of the color string */
    Display *display,           /* display of main window */
    Colormap colormap,          /* colormap of current display */
    unsigned char *redPtr,      /* the result is written to these pointers */
    unsigned char *greenPtr,
    unsigned char *bluePtr,
    unsigned char *alphaPtr)
{
    int i;
    unsigned long int colorValue = 0;

    if (colorStrLen - 1 != 4 && colorStrLen - 1 != 8) {
        return ParseColorAsStandard(interp, colorString, colorStrLen,
                display, colormap, redPtr, greenPtr, bluePtr, alphaPtr);
    }
    for (i = 1; i < colorStrLen; i++) {
        if (!isxdigit(UCHAR(colorString[i]))) {
            /*
             * There still is a chance that this is a Tk color with
             * an alpha suffix
             */

            return ParseColorAsStandard(interp, colorString, colorStrLen,
                    display, colormap, redPtr, greenPtr, bluePtr, alphaPtr);
        }
    }

    colorValue = strtoul(colorString + 1, NULL, 16);
    switch (colorStrLen - 1) {
    case 4:
        /* #RGBA format */
        *redPtr = (unsigned char) ((colorValue >> 12) * 0x11);
        *greenPtr = (unsigned char) (((colorValue >> 8) & 0xf) * 0x11);
        *bluePtr = (unsigned char) (((colorValue >> 4) & 0xf) * 0x11);
        *alphaPtr = (unsigned char) ((colorValue & 0xf) * 0x11);
        return TCL_OK;
    case 8:
        /* #RRGGBBAA format */
        *redPtr = (unsigned char) (colorValue >> 24);
        *greenPtr = (unsigned char) ((colorValue >> 16) & 0xff);
        *bluePtr = (unsigned char) ((colorValue >> 8) & 0xff);
        *alphaPtr = (unsigned char) (colorValue & 0xff);
        return TCL_OK;
    default:
        Tcl_Panic("unexpected switch fallthrough");
    }

    /* Shouldn't get here */
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * ParseColorAsStandard --
 *
 *      This function tries to split a color stirng in a color and a
 *      suffix part and to extract color and alpha values from them. The
 *      color part is treated as regular Tk color.
 *
 * Results:
 *      On success, writes red, green, blue and alpha values to the
 *      corresponding pointers. If the color spec contains no alpha
 *      information, 255 is taken as transparency value.
 *      Returns a standard Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static int
ParseColorAsStandard(
    Tcl_Interp *interp,         /* error messages are left here */
    const char *specString,    /* the color data to parse */
    int specStrLen,            /* length of the color string */
    Display *display,           /* display of main window */
    Colormap colormap,          /* colormap of current display */
    unsigned char *redPtr,      /* the result is written to these pointers */
    unsigned char *greenPtr,
    unsigned char *bluePtr,
    unsigned char *alphaPtr)
{
    XColor parsedColor;
    const char *suffixString, *colorString;
    char colorBuffer[TK_PHOTO_MAX_COLOR_LENGTH + 1];
    double fracAlpha;
    unsigned int suffixAlpha = 0;
    int i;

    /*
     * Split color data string in color and suffix parts
     */

    if ((suffixString = strrchr(specString, '@')) == NULL
            && ((suffixString = strrchr(specString, '#')) == NULL
                    || suffixString == specString)) {
        suffixString = specString + specStrLen;
        colorString = specString;
    } else {
        strncpy(colorBuffer, specString, suffixString - specString);
        colorBuffer[suffixString - specString] = '\0';
        colorString = (const char*)colorBuffer;
    }

    /*
     * Try to parse as standard Tk color.
     *
     * We don't use Tk_GetColor() et al. here, as those functions
     * migth return a color that does not exaxtly match the given name
     * if the colormap is full. Also, we don't really want the color to be
     * added to the colormap.
     */

    if ( ! TkParseColor(display, colormap, colorString, &parsedColor)) {
         Tcl_SetObjResult(interp, Tcl_ObjPrintf(
            "invalid color name \"%s\"", specString));
         Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
                 "INVALID_COLOR", NULL);
         return TCL_ERROR;
    }

    /*
     * parse the Suffix
     */

    switch (suffixString[0]) {
    case '\0':
        suffixAlpha = 255;
        break;
    case '@':
        if (Tcl_GetDouble(NULL, suffixString + 1, &fracAlpha) != TCL_OK) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("invalid alpha "
                    "suffix \"%s\": expected floating-point value",
                    suffixString));
            Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
                    "INVALID COLOR", NULL);
            return TCL_ERROR;
        }
        if (fracAlpha < 0 || fracAlpha > 1) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("invalid alpha suffix"
                    " \"%s\": value must be in the range from 0 to 1",
                    suffixString));
            Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
                    "INVALID_COLOR", NULL);
            return TCL_ERROR;
        }
        suffixAlpha = (unsigned int) floor(fracAlpha * 255 + 0.5);
        break;
    case '#':
        if (strlen(suffixString + 1) < 1 || strlen(suffixString + 1)> 2) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                    "invalid alpha suffix \"%s\"", suffixString));
            Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
                    "INVALID_COLOR", NULL);
            return TCL_ERROR;
        }
        for (i = 1; i <= (int)strlen(suffixString + 1); i++) {
            if ( ! isxdigit(UCHAR(suffixString[i]))) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf(
                        "invalid alpha suffix \"%s\": expected hex digit",
                        suffixString));
                Tcl_SetErrorCode(interp, "TK", "IMAGE", "PHOTO",
                        "INVALID_COLOR", NULL);
                return TCL_ERROR;
            }
        }
        if (strlen(suffixString + 1) == 1) {
            sscanf(suffixString, "#%1x", &suffixAlpha);
            suffixAlpha *= 0x11;
        } else {
            sscanf(suffixString, "#%2x", &suffixAlpha);
        }
        break;
    default:
        Tcl_Panic("unexpected switch fallthrough");
    }

    *redPtr = (unsigned char) (parsedColor.red >> 8);
    *greenPtr = (unsigned char) (parsedColor.green >> 8);
    *bluePtr = (unsigned char) (parsedColor.blue >> 8);
    *alphaPtr = (unsigned char) suffixAlpha;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkDebugStringMatchDef --
 *
 *      Debugging function for StringMatchDef. Basically just an alias for
 *      that function, intended to expose it directly to tests, as
 *      StirngMatchDef cannot be sufficiently tested otherwise.
 *
 * Results:
 *      See StringMatchDef.
 *
 * Side effects:
 *      None
 *----------------------------------------------------------------------
 */
int
TkDebugPhotoStringMatchDef(
    Tcl_Interp *interp,     /* Error messages are left in this interpreter */
    Tcl_Obj *data,          /* The data to check */
    Tcl_Obj *formatString,  /* Value of the -format option, not used here */
    int *widthPtr,          /* Width of image is written to this location */
    int *heightPtr)         /* Height of image is written to this location */
{
    return StringMatchDef(data, formatString, widthPtr, heightPtr, interp);
}


/* Local Variables: */
/* mode: c */
/* fill-column: 78 */
/* c-basic-offset: 4 */
/* tab-width: 8 */
/* indent-tabs-mode: nil */
/* End: */
