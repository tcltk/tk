/*
 * tkImgGIF.c --
 *
 *	A photo image file handler for GIF files. Reads 87a and 89a GIF
 *	files. At present there is no write function.  GIF images may be
 *	read using the -data option of the photo image.  The data may be
 *	given as a binary string in a Tcl_Obj or by representing
 *	the data as BASE64 encoded ascii.  Derived from the giftoppm code
 *	found in the pbmplus package and tkImgFmtPPM.c in the tk4.0b2
 *	distribution.
 *
 * Copyright (c) Reed Wade (wade@cs.utk.edu), University of Tennessee
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 * Copyright (c) 1997 Australian National University
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * This file also contains code from the giftoppm program, which is
 * copyrighted as follows:
 *
 * +-------------------------------------------------------------------+
 * | Copyright 1990, David Koblas.                                     |
 * |   Permission to use, copy, modify, and distribute this software   |
 * |   and its documentation for any purpose and without fee is hereby |
 * |   granted, provided that the above copyright notice appear in all |
 * |   copies and that both that copyright notice and this permission  |
 * |   notice appear in supporting documentation.  This software is    |
 * |   provided "as is" without express or implied warranty.           |
 * +-------------------------------------------------------------------+
 *
 * RCS: @(#) $Id: tkImgGIF.c,v 1.5 1999/10/29 03:57:56 hobbs Exp $
 */

/*
 * GIF's are represented as data in base64 format.
 * base64 strings consist of 4 6-bit characters -> 3 8 bit bytes.
 * A-Z, a-z, 0-9, + and / represent the 64 values (in order).
 * '=' is a trailing padding char when the un-encoded data is not a
 * multiple of 3 bytes.  We'll ignore white space when encountered.
 * Any other invalid character is treated as an EOF
 */

#define GIF_SPECIAL	 (256)
#define GIF_PAD		(GIF_SPECIAL+1)
#define GIF_SPACE	(GIF_SPECIAL+2)
#define GIF_BAD		(GIF_SPECIAL+3)
#define GIF_DONE	(GIF_SPECIAL+4)

/*
 * structure to "mimic" FILE for Mread, so we can look like fread.
 * The decoder state keeps track of which byte we are about to read,
 * or EOF.
 */

typedef struct mFile {
    unsigned char *data;	/* mmencoded source string */
    int c;			/* bits left over from previous character */
    int state;			/* decoder state (0-4 or GIF_DONE) */
} MFile;

#include "tkInt.h"
#include "tkPort.h"

/*
 * 			 HACK ALERT!!  HACK ALERT!!  HACK ALERT!!
 * This code is hard-wired for reading from files.  In order to read
 * from a data stream, we'll trick fread so we can reuse the same code.
 * 0==from file; 1==from base64 encoded data; 2==from binary data
 */

typedef struct ThreadSpecificData {
    int fromData;
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * The format record for the GIF file format:
 */

static int      FileMatchGIF _ANSI_ARGS_((Tcl_Channel chan, char *fileName,
		    Tcl_Obj *format, int *widthPtr, int *heightPtr,
		    Tcl_Interp *interp));
static int      FileReadGIF  _ANSI_ARGS_((Tcl_Interp *interp,
		    Tcl_Channel chan, char *fileName, Tcl_Obj *format,
		    Tk_PhotoHandle imageHandle, int destX, int destY,
		    int width, int height, int srcX, int srcY));
static int	StringMatchGIF _ANSI_ARGS_(( Tcl_Obj *dataObj,
		    Tcl_Obj *format, int *widthPtr, int *heightPtr,
		    Tcl_Interp *interp));
static int	StringReadGIF _ANSI_ARGS_((Tcl_Interp *interp, Tcl_Obj *dataObj,
		    Tcl_Obj *format, Tk_PhotoHandle imageHandle,
		    int destX, int destY, int width, int height,
		    int srcX, int srcY));

Tk_PhotoImageFormat tkImgFmtGIF = {
	"GIF",			/* name */
	FileMatchGIF,   /* fileMatchProc */
	StringMatchGIF, /* stringMatchProc */
	FileReadGIF,    /* fileReadProc */
	StringReadGIF,  /* stringReadProc */
	NULL,           /* fileWriteProc */
	NULL,           /* stringWriteProc */
};

#define INTERLACE		0x40
#define LOCALCOLORMAP		0x80
#define BitSet(byte, bit)	(((byte) & (bit)) == (bit))
#define MAXCOLORMAPSIZE		256
#define CM_RED			0
#define CM_GREEN		1
#define CM_BLUE			2
#define CM_ALPHA		3
#define MAX_LWZ_BITS		12
#define LM_to_uint(a,b)         (((b)<<8)|(a))
#define ReadOK(file,buffer,len)	(Fread(buffer, len, 1, file) != 0)

/*
 * Prototypes for local procedures defined in this file:
 */

static int		DoExtension _ANSI_ARGS_((Tcl_Channel chan, int label,
			    int *transparent));
static int		GetCode _ANSI_ARGS_((Tcl_Channel chan, int code_size,
			    int flag));
static int		GetDataBlock _ANSI_ARGS_((Tcl_Channel chan,
			    unsigned char *buf));
static int		LWZReadByte _ANSI_ARGS_((Tcl_Channel chan, int flag,
			    int input_code_size));
static int		ReadColorMap _ANSI_ARGS_((Tcl_Channel chan, int number,
			    unsigned char buffer[MAXCOLORMAPSIZE][4]));
static int		ReadGIFHeader _ANSI_ARGS_((Tcl_Channel chan,
			    int *widthPtr, int *heightPtr));
static int		ReadImage _ANSI_ARGS_((Tcl_Interp *interp,
			    char *imagePtr, Tcl_Channel chan,
			    int len, int rows,
			    unsigned char cmap[MAXCOLORMAPSIZE][4],
			    int width, int height, int srcX, int srcY,
			    int interlace, int transparent));

/*
 * these are for the BASE64 image reader code only
 */

static int		Fread _ANSI_ARGS_((unsigned char *dst, size_t size,
			    size_t count, Tcl_Channel chan));
static int		Mread _ANSI_ARGS_((unsigned char *dst, size_t size,
			    size_t count, MFile *handle));
static int		Mgetc _ANSI_ARGS_((MFile *handle));
static int		char64 _ANSI_ARGS_((int c));
static void		mInit _ANSI_ARGS_((unsigned char *string,
			    MFile *handle));

/*
 *----------------------------------------------------------------------
 *
 * FileMatchGIF --
 *
 *	This procedure is invoked by the photo image type to see if
 *	a file contains image data in GIF format.
 *
 * Results:
 *	The return value is 1 if the first characters in file f look
 *	like GIF data, and 0 otherwise.
 *
 * Side effects:
 *	The access position in f may change.
 *
 *----------------------------------------------------------------------
 */

static int
FileMatchGIF(chan, fileName, format, widthPtr, heightPtr, interp)
    Tcl_Channel chan;		/* The image file, open for reading. */
    char *fileName;		/* The name of the image file. */
    Tcl_Obj *format;		/* User-specified format object, or NULL. */
    int *widthPtr, *heightPtr;	/* The dimensions of the image are
				 * returned here if the file is a valid
				 * raw GIF file. */
    Tcl_Interp *interp;		/* not used */
{
	return ReadGIFHeader(chan, widthPtr, heightPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * FileReadGIF --
 *
 *	This procedure is called by the photo image type to read
 *	GIF format data from a file and write it into a given
 *	photo image.
 *
 * Results:
 *	A standard TCL completion code.  If TCL_ERROR is returned
 *	then an error message is left in the interp's result.
 *
 * Side effects:
 *	The access position in file f is changed, and new data is
 *	added to the image given by imageHandle.
 *
 *----------------------------------------------------------------------
 */

static int
FileReadGIF(interp, chan, fileName, format, imageHandle, destX, destY,
	width, height, srcX, srcY)
    Tcl_Interp *interp;		/* Interpreter to use for reporting errors. */
    Tcl_Channel chan;		/* The image file, open for reading. */
    char *fileName;		/* The name of the image file. */
    Tcl_Obj *format;		/* User-specified format object, or NULL. */
    Tk_PhotoHandle imageHandle;	/* The photo image to write into. */
    int destX, destY;		/* Coordinates of top-left pixel in
				 * photo image to be written to. */
    int width, height;		/* Dimensions of block of photo image to
				 * be written to. */
    int srcX, srcY;		/* Coordinates of top-left pixel to be used
				 * in image being read. */
{
    int fileWidth, fileHeight;
    int nBytes, index = 0, argc = 0, i;
    Tcl_Obj **objv;
    Tk_PhotoImageBlock block;
    unsigned char buf[100];
    int bitPixel;
    unsigned char colorMap[MAXCOLORMAPSIZE][4];
    int transparent = -1;
    static char *optionStrings[] = {
	"-index",	NULL
    };

    if (format && Tcl_ListObjGetElements(interp, format,
	    &argc, &objv) != TCL_OK) {
	return TCL_ERROR;
    }
    for (i = 1; i < argc; i++) {
	if (Tcl_GetIndexFromObj(interp, objv[i], optionStrings, "option name", 0,
		&nBytes) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (i == (argc-1)) {
	    Tcl_AppendResult(interp, "no value given for \"",
		    Tcl_GetStringFromObj(objv[i], NULL),
		    "\" option", (char *) NULL);
	    return TCL_ERROR;
	}
	if (Tcl_GetIntFromObj(interp, objv[++i], &index) != TCL_OK) {
	    return TCL_ERROR;
	}
    }
    if (!ReadGIFHeader(chan, &fileWidth, &fileHeight)) {
    	Tcl_AppendResult(interp, "couldn't read GIF header from file \"",
		fileName, "\"", NULL);
	return TCL_ERROR;
    }
    if ((fileWidth <= 0) || (fileHeight <= 0)) {
	Tcl_AppendResult(interp, "GIF image file \"", fileName,
		"\" has dimension(s) <= 0", (char *) NULL);
	return TCL_ERROR;
    }

    if (Fread(buf, 1, 3, chan) != 3) {
	return TCL_OK;
    }
    bitPixel = 2<<(buf[0]&0x07);

    if (BitSet(buf[0], LOCALCOLORMAP)) {    /* Global Colormap */
	if (!ReadColorMap(chan, bitPixel, colorMap)) {
	    Tcl_AppendResult(interp, "error reading color map",
		    (char *) NULL);
	    return TCL_ERROR;
	}
    }

    if ((srcX + width) > fileWidth) {
	width = fileWidth - srcX;
    }
    if ((srcY + height) > fileHeight) {
	height = fileHeight - srcY;
    }
    if ((width <= 0) || (height <= 0)
	    || (srcX >= fileWidth) || (srcY >= fileHeight)) {
	return TCL_OK;
    }

    Tk_PhotoExpand(imageHandle, destX + width, destY + height);

    block.width = width;
    block.height = height;
    block.pixelSize = 4;
    block.pitch = block.pixelSize * block.width;
    block.offset[0] = 0;
    block.offset[1] = 1;
    block.offset[2] = 2;
    block.offset[3] = 3;
    block.pixelPtr = NULL;

    while (1) {
	if (Fread(buf, 1, 1, chan) != 1) {
	    /*
	     * Premature end of image.  We should really notify
	     * the user, but for now just show garbage.
	     */

	    break;
	}

	if (buf[0] == ';') {
	    /*
	     * GIF terminator.
	     */

	    Tcl_AppendResult(interp,"no image data for this index",
		    (char *) NULL);
	    goto error;
	}

	if (buf[0] == '!') {
	    /*
	     * This is a GIF extension.
	     */

	    if (Fread(buf, 1, 1, chan) != 1) {
		Tcl_SetResult(interp,
			"error reading extension function code in GIF image",
			TCL_STATIC);
		goto error;
	    }
	    if (DoExtension(chan, buf[0], &transparent) < 0) {
		Tcl_SetResult(interp, "error reading extension in GIF image",
			TCL_STATIC);
		goto error;
	    }
	    continue;
	}

	if (buf[0] != ',') {
	    /*
	     * Not a valid start character; ignore it.
	     */
	    continue;
	}

	if (Fread(buf, 1, 9, chan) != 9) {
	    Tcl_SetResult(interp,
		    "couldn't read left/top/width/height in GIF image",
		    TCL_STATIC);
	    goto error;
	}

	fileWidth = LM_to_uint(buf[4],buf[5]);
	fileHeight = LM_to_uint(buf[6],buf[7]);

	bitPixel = 1<<((buf[8]&0x07)+1);

	if (index--) {
	    int x,y;
	    unsigned char c;
	    /* this is not the image we want to read: skip it. */

	    if (BitSet(buf[8], LOCALCOLORMAP)) {
		if (!ReadColorMap(chan, bitPixel, 0)) {
		    Tcl_AppendResult(interp,
			    "error reading color map", (char *) NULL);
		    goto error;
		}
	    }

	    /* read data */
	    if (!ReadOK(chan,&c,1)) {
		goto error;
	    }

	    LWZReadByte(chan, 1, c);

	    for (y=0; y<fileHeight; y++) {
		for (x=0; x<fileWidth; x++) {
		    if (LWZReadByte(chan, 0, c) < 0) {
			Tcl_AppendResult(interp,
				"error reading image data", (char *) NULL);
			goto error;
		    }
		}
	    }
	    continue;
	}

	if (BitSet(buf[8], LOCALCOLORMAP)) {
	    if (!ReadColorMap(chan, bitPixel, colorMap)) {
		    Tcl_AppendResult(interp, "error reading color map", 
			    (char *) NULL);
		    goto error;
	    }
	}

	index = LM_to_uint(buf[0],buf[1]);
	srcX -= index;
	if (srcX<0) {
	    destX -= srcX; width += srcX;
	    srcX = 0;
	}

	if (width > fileWidth) {
	    width = fileWidth;
	}

	index = LM_to_uint(buf[2],buf[3]);
	srcY -= index;
	if (index > srcY) {
	    destY -= srcY; height += srcY;
	    srcY = 0;
	}
	if (height > fileHeight) {
	    height = fileHeight;
	}

	if ((width <= 0) || (height <= 0)) {
	    block.pixelPtr = 0;
	    goto noerror;
	}

	block.width = width;
	block.height = height;
	block.pixelSize = (transparent>=0) ? 4 : 3;
	block.offset[3] = (transparent>=0) ? 3 : 0;
	block.pitch = block.pixelSize * width;
	nBytes = block.pitch * height;
	block.pixelPtr = (unsigned char *) ckalloc((unsigned) nBytes);

	if (ReadImage(interp, (char *) block.pixelPtr, chan, width,
		height, colorMap, fileWidth, fileHeight, srcX, srcY,
		BitSet(buf[8], INTERLACE), transparent) != TCL_OK) {
	    goto error;
	}
	break;
    }

    Tk_PhotoPutBlock(imageHandle, &block, destX, destY, width, height);

    noerror:
    if (block.pixelPtr) {
	ckfree((char *) block.pixelPtr);
    }
    Tcl_AppendResult(interp, tkImgFmtGIF.name, (char *) NULL);
    return TCL_OK;

    error:
    if (block.pixelPtr) {
	ckfree((char *) block.pixelPtr);
    }
    return TCL_ERROR;

}

/*
 *----------------------------------------------------------------------
 *
 * StringMatchGIF --
 *
 *  This procedure is invoked by the photo image type to see if
 *  an object contains image data in GIF format.
 *
 * Results:
 *  The return value is 1 if the first characters in the data are
 *  like GIF data, and 0 otherwise.
 *
 * Side effects:
 *  the size of the image is placed in widthPre and heightPtr.
 *
 *----------------------------------------------------------------------
 */

static int
StringMatchGIF(dataObj, format, widthPtr, heightPtr, interp)
    Tcl_Obj *dataObj;		/* the object containing the image data */
    Tcl_Obj *format;		/* the image format object, or NULL */
    int *widthPtr;		/* where to put the string width */
    int *heightPtr;		/* where to put the string height */
    Tcl_Interp *interp;		/* not used */
{
    unsigned char *data, header[10];
    int got, length;
    MFile handle;

    data = Tcl_GetByteArrayFromObj(dataObj, &length);

    /* Header is a minimum of 10 bytes */
    if (length < 10) {
      return 0;
    }

    /* Check whether the data is Base64 encoded */

    if ((strncmp("\107\111\106\70\67\141", data, 6) != 0) && 
	(strncmp("\107\111\106\70\71\141", data, 6) != 0)) {
      /* Try interpreting the data as Base64 encoded */
      mInit((unsigned char *) data, &handle);
      got = Mread(header, 10, 1, &handle);
      if (got != 10
	      || ((strncmp("\107\111\106\70\67\141", (char *) header, 6) != 0)
	      && (strncmp("\107\111\106\70\71\141", (char *) header, 6) != 0))) {
	  return 0;
      }
    } else {
      memcpy((VOID *) header, (VOID *) data, 10);
    }
    *widthPtr = LM_to_uint(header[6],header[7]);
    *heightPtr = LM_to_uint(header[8],header[9]);
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * StringReadGif -- --
 *
 *	This procedure is called by the photo image type to read
 *	GIF format data from an object, optionally base64 encoded, 
 *	and give it to the photo image.
 *
 * Results:
 *	A standard TCL completion code.  If TCL_ERROR is returned
 *	then an error message is left in the interp's result.
 *
 * Side effects:
 *	new data is added to the image given by imageHandle.  This
 *	procedure calls FileReadGif by redefining the operation of
 *	fprintf temporarily.
 *
 *----------------------------------------------------------------------
 */

static int
StringReadGIF(interp, dataObj, format, imageHandle,
	destX, destY, width, height, srcX, srcY)
    Tcl_Interp *interp;		/* interpreter for reporting errors in */
    Tcl_Obj *dataObj;		/* object containing the image */
    Tcl_Obj *format;		/* format object, or NULL */
    Tk_PhotoHandle imageHandle;	/* the image to write this data into */
    int destX, destY;		/* The rectangular region of the  */
    int  width, height;		/*   image to copy */
    int srcX, srcY;
{
    int result;
    MFile handle;
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *) 
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    Tcl_Channel dataSrc;
    char *data;
    /* Check whether the data is Base64 encoded */
    data = Tcl_GetByteArrayFromObj(dataObj, NULL);
    if ((strncmp("\107\111\106\70\67\141", data, 6) != 0) && 
	    (strncmp("\107\111\106\70\71\141", data, 6) != 0)) {
	mInit((unsigned char *)data,&handle);
	tsdPtr->fromData = 1;
	dataSrc = (Tcl_Channel) &handle;
    } else {
	tsdPtr->fromData = 2;
	mInit((unsigned char *)data,&handle);
	dataSrc = (Tcl_Channel) &handle;
    }
    result = FileReadGIF(interp, dataSrc, "inline data",
	    format, imageHandle, destX, destY, width, height,
            srcX, srcY);
    tsdPtr->fromData = 0;
    return(result);
}

/*
 *----------------------------------------------------------------------
 *
 * ReadGIFHeader --
 *
 *	This procedure reads the GIF header from the beginning of a
 *	GIF file and returns the dimensions of the image.
 *
 * Results:
 *	The return value is 1 if file "f" appears to start with
 *	a valid GIF header, 0 otherwise.  If the header is valid,
 *	then *widthPtr and *heightPtr are modified to hold the
 *	dimensions of the image.
 *
 * Side effects:
 *	The access position in f advances.
 *
 *----------------------------------------------------------------------
 */

static int
ReadGIFHeader(chan, widthPtr, heightPtr)
    Tcl_Channel chan;		/* Image file to read the header from */
    int *widthPtr, *heightPtr;	/* The dimensions of the image are
				 * returned here. */
{
    unsigned char buf[7];

    if ((Fread(buf, 1, 6, chan) != 6)
	    || ((strncmp("\107\111\106\70\67\141", (char *) buf, 6) != 0)
	    && (strncmp("\107\111\106\70\71\141", (char *) buf, 6) != 0))) {
	return 0;
    }

    if (Fread(buf, 1, 4, chan) != 4) {
	return 0;
    }

    *widthPtr = LM_to_uint(buf[0],buf[1]);
    *heightPtr = LM_to_uint(buf[2],buf[3]);
    return 1;
}

/*
 *-----------------------------------------------------------------
 * The code below is copied from the giftoppm program and modified
 * just slightly.
 *-----------------------------------------------------------------
 */

static int
ReadColorMap(chan, number, buffer)
     Tcl_Channel chan;
     int number;
     unsigned char buffer[MAXCOLORMAPSIZE][4];
{
	int i;
	unsigned char rgb[3];

	for (i = 0; i < number; ++i) {
	    if (! ReadOK(chan, rgb, sizeof(rgb))) {
		return 0;
	    }
	    
	    if (buffer) {
		buffer[i][CM_RED] = rgb[0] ;
		buffer[i][CM_GREEN] = rgb[1] ;
		buffer[i][CM_BLUE] = rgb[2] ;
		buffer[i][CM_ALPHA] = 255 ;
	    }
	}
	return 1;
}



static int
DoExtension(chan, label, transparent)
     Tcl_Channel chan;
     int label;
     int *transparent;
{
    static unsigned char buf[256];
    int count;

    switch (label) {
	case 0x01:      /* Plain Text Extension */
	    break;
	    
	case 0xff:      /* Application Extension */
	    break;

	case 0xfe:      /* Comment Extension */
	    do {
		count = GetDataBlock(chan, (unsigned char*) buf);
	    } while (count > 0);
	    return count;

	case 0xf9:      /* Graphic Control Extension */
	    count = GetDataBlock(chan, (unsigned char*) buf);
	    if (count < 0) {
		return 1;
	    }
	    if ((buf[0] & 0x1) != 0) {
		*transparent = buf[3];
	    }

	    do {
		count = GetDataBlock(chan, (unsigned char*) buf);
	    } while (count > 0);
	    return count;
    }

    do {
	count = GetDataBlock(chan, (unsigned char*) buf);
    } while (count > 0);
    return count;
}

static int ZeroDataBlock = 0;

static int
GetDataBlock(chan, buf)
     Tcl_Channel chan;
     unsigned char *buf;
{
    unsigned char count;

    if (! ReadOK(chan, &count,1)) {
	return -1;
    }

    ZeroDataBlock = count == 0;

    if ((count != 0) && (! ReadOK(chan, buf, count))) {
	return -1;
    }

    return count;
}


static int
ReadImage(interp, imagePtr, chan, len, rows, cmap,
	width, height, srcX, srcY, interlace, transparent)
     Tcl_Interp *interp;
     char *imagePtr;
     Tcl_Channel chan;
     int len, rows;
     unsigned char cmap[MAXCOLORMAPSIZE][4];
     int width, height;
     int srcX, srcY;
     int interlace;
     int transparent;
{
    unsigned char c;
    int v;
    int xpos = 0, ypos = 0, pass = 0;
    char *pixelPtr;


    /*
     *  Initialize the Compression routines
     */
    if (! ReadOK(chan, &c, 1))  {
	Tcl_AppendResult(interp, "error reading GIF image: ",
		Tcl_PosixError(interp), (char *) NULL);
	return TCL_ERROR;
    }

    if (LWZReadByte(chan, 1, c) < 0) {
	Tcl_SetResult(interp, "format error in GIF image", TCL_STATIC);
	return TCL_ERROR;
    }

    if (transparent!=-1) {
	cmap[transparent][CM_RED] = 0;
	cmap[transparent][CM_GREEN] = 0;
	cmap[transparent][CM_BLUE] = 0;
	cmap[transparent][CM_ALPHA] = 0;
    }

    pixelPtr = imagePtr;
    while ((v = LWZReadByte(chan, 0, c)) >= 0 ) {

	if ((xpos>=srcX) && (xpos<srcX+len) &&
		(ypos>=srcY) && (ypos<srcY+rows)) {
	    *pixelPtr++ = cmap[v][CM_RED];
	    *pixelPtr++ = cmap[v][CM_GREEN];
	    *pixelPtr++ = cmap[v][CM_BLUE];
	    if (transparent >= 0) {
		*pixelPtr++ = cmap[v][CM_ALPHA];
	    }
	}

	++xpos;
	if (xpos == width) {
	    xpos = 0;
	    if (interlace) {
		switch (pass) {
		    case 0:
		    case 1:
			ypos += 8; break;
		    case 2:
			ypos += 4; break;
		    case 3:
			ypos += 2; break;
		}
		
		while (ypos >= height) {
		    ++pass;
		    switch (pass) {
			case 1:
			    ypos = 4; break;
			case 2:
			    ypos = 2; break;
			case 3:
			    ypos = 1; break;
			default:
			    return TCL_OK;
		    }
		}
	    } else {
		++ypos;
	    }
	    pixelPtr = imagePtr + (ypos-srcY) * len * ((transparent>=0)?4:3);
	}
	if (ypos >= height)
	    break;
    }
    return TCL_OK;
}

static int
LWZReadByte(chan, flag, input_code_size)
     Tcl_Channel chan;
     int flag;
     int input_code_size;
{
    static int  fresh = 0;
    int code, incode;
    static int code_size, set_code_size;
    static int max_code, max_code_size;
    static int firstcode, oldcode;
    static int clear_code, end_code;
    static int table[2][(1<< MAX_LWZ_BITS)];
    static int stack[(1<<(MAX_LWZ_BITS))*2], *sp;
    register int    i;

    if (flag) {
	set_code_size = input_code_size;
	code_size = set_code_size+1;
	clear_code = 1 << set_code_size ;
	end_code = clear_code + 1;
	max_code_size = 2*clear_code;
	max_code = clear_code+2;

	GetCode(chan, 0, 1);

	fresh = 1;

	for (i = 0; i < clear_code; ++i) {
	    table[0][i] = 0;
	    table[1][i] = i;
	}
	for (; i < (1<<MAX_LWZ_BITS); ++i) {
	    table[0][i] = table[1][0] = 0;
	}

	sp = stack;

	return 0;
    } else if (fresh) {
	fresh = 0;
	do {
	    firstcode = oldcode = GetCode(chan, code_size, 0);
	} while (firstcode == clear_code);
	return firstcode;
    }

    if (sp > stack) {
	return *--sp;
    }

    while ((code = GetCode(chan, code_size, 0)) >= 0) {
	if (code == clear_code) {
	    for (i = 0; i < clear_code; ++i) {
		table[0][i] = 0;
		table[1][i] = i;
	    }
	    
	    for (; i < (1<<MAX_LWZ_BITS); ++i) {
		table[0][i] = table[1][i] = 0;
	    }

	    code_size = set_code_size+1;
	    max_code_size = 2*clear_code;
	    max_code = clear_code+2;
	    sp = stack;
	    firstcode = oldcode = GetCode(chan, code_size, 0);
	    return firstcode;

	} else if (code == end_code) {
	    int count;
	    unsigned char buf[260];

	    if (ZeroDataBlock) {
		return -2;
	    }
	    
	    while ((count = GetDataBlock(chan, buf)) > 0)
		/* Empty body */;

	    if (count != 0) {
		return -2;
	    }
	}

	incode = code;

	if (code >= max_code) {
	    *sp++ = firstcode;
	    code = oldcode;
	}

	while (code >= clear_code) {
	    *sp++ = table[1][code];
	    if (code == table[0][code]) {
		return -2;

		/*
		 * Used to be this instead, Steve Ball suggested
		 * the change to just return.
		 printf("circular table entry BIG ERROR\n");
		 */
	    }
	    code = table[0][code];
	}

	*sp++ = firstcode = table[1][code];

	if ((code = max_code) <(1<<MAX_LWZ_BITS)) {
	    table[0][code] = oldcode;
	    table[1][code] = firstcode;
	    ++max_code;
	    if ((max_code>=max_code_size) && (max_code_size < (1<<MAX_LWZ_BITS))) {
		max_code_size *= 2;
		++code_size;
	    }
	}

	oldcode = incode;

	if (sp > stack)
	    return *--sp;
	}
	return code;
}


static int
GetCode(chan, code_size, flag)
     Tcl_Channel chan;
     int code_size;
     int flag;
{
    static unsigned char buf[280];
    static int curbit, lastbit, done, last_byte;
    int i, j, ret;
    unsigned char count;

    if (flag) {
	curbit = 0;
	lastbit = 0;
	done = 0;
	return 0;
    }


    if ( (curbit+code_size) >= lastbit) {
	if (done) {
	    /* ran off the end of my bits */
	    return -1;
	}
	if (last_byte >= 2) {
	    buf[0] = buf[last_byte-2];
	}
	if (last_byte >= 1) {
	    buf[1] = buf[last_byte-1];
	}

	if ((count = GetDataBlock(chan, &buf[2])) == 0) {
	    done = 1;
	}

	last_byte = 2 + count;
	curbit = (curbit - lastbit) + 16;
	lastbit = (2+count)*8 ;
    }

    ret = 0;
    for (i = curbit, j = 0; j < code_size; ++i, ++j) {
	ret |= ((buf[ i / 8 ] & (1 << (i % 8))) != 0) << j;
    }

    curbit += code_size;

    return ret;
}

/*
 *----------------------------------------------------------------------
 *
 * Minit -- --
 *
 *  This procedure initializes a base64 decoder handle
 *
 * Results:
 *  none
 *
 * Side effects:
 *  the base64 handle is initialized
 *
 *----------------------------------------------------------------------
 */

static void
mInit(string, handle)
   unsigned char *string;	/* string containing initial mmencoded data */
   MFile *handle;		/* mmdecode "file" handle */
{
   handle->data = string;
   handle->state = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * Mread --
 *
 *	This procedure is invoked by the GIF file reader as a 
 *	temporary replacement for "fread", to get GIF data out
 *	of a string (using Mgetc).
 *
 * Results:
 *	The return value is the number of characters "read"
 *
 * Side effects:
 *	The base64 handle will change state.
 *
 *----------------------------------------------------------------------
 */

static int
Mread(dst, chunkSize, numChunks, handle)  
   unsigned char *dst;	/* where to put the result */
   size_t chunkSize;	/* size of each transfer */
   size_t numChunks;	/* number of chunks */
   MFile *handle;	/* mmdecode "file" handle */
{
   register int i, c;
   int count = chunkSize * numChunks;

   for(i=0; i<count && (c=Mgetc(handle)) != GIF_DONE; i++) {
	*dst++ = c;
   }
   return i;
}

/*
 * get the next decoded character from an mmencode handle
 * This causes at least 1 character to be "read" from the encoded string
 */

/*
 *----------------------------------------------------------------------
 *
 * Mgetc --
 *
 *  This procedure decodes and returns the next byte from a base64
 *  encoded string.
 *
 * Results:
 *  The next byte (or GIF_DONE) is returned.
 *
 * Side effects:
 *  The base64 handle will change state.
 *
 *----------------------------------------------------------------------
 */

static int
Mgetc(handle)
   MFile *handle;		/* Handle containing decoder data and state. */
{
    int c;
    int result = 0;		/* Initialization needed only to prevent
				 * gcc compiler warning. */
     
    if (handle->state == GIF_DONE) {
	return(GIF_DONE);
    }

    do {
	c = char64(*handle->data);
	handle->data++;
    } while (c==GIF_SPACE);

    if (c>GIF_SPECIAL) {
	handle->state = GIF_DONE;
	return(handle->state ? handle->c : GIF_DONE);
    }

    switch (handle->state++) {
	case 0:
	   handle->c = c<<2;
	   result = Mgetc(handle);
	   break;
	case 1:
	   result = handle->c | (c>>4);
	   handle->c = (c&0xF)<<4;
	   break;
	case 2:
	   result = handle->c | (c>>2);
	   handle->c = (c&0x3) << 6;
	   break;
	case 3:
	   result = handle->c | c;
	   handle->state = 0;
	   break;
    }
    return(result);
}

/*
 *----------------------------------------------------------------------
 *
 * char64 --
 *
 *	This procedure converts a base64 ascii character into its binary
 *	equivalent.  This code is a slightly modified version of the
 *	char64 proc in N. Borenstein's metamail decoder.
 *
 * Results:
 *	The binary value, or an error code.
 *
 * Side effects:
 *	None.
 *----------------------------------------------------------------------
 */

static int
char64(c)
int c;
{
    switch(c) {
        case 'A': return(0);  case 'B': return(1);  case 'C': return(2);
        case 'D': return(3);  case 'E': return(4);  case 'F': return(5);
        case 'G': return(6);  case 'H': return(7);  case 'I': return(8);
        case 'J': return(9);  case 'K': return(10); case 'L': return(11);
        case 'M': return(12); case 'N': return(13); case 'O': return(14);
        case 'P': return(15); case 'Q': return(16); case 'R': return(17);
        case 'S': return(18); case 'T': return(19); case 'U': return(20);
        case 'V': return(21); case 'W': return(22); case 'X': return(23);
        case 'Y': return(24); case 'Z': return(25); case 'a': return(26);
        case 'b': return(27); case 'c': return(28); case 'd': return(29);
        case 'e': return(30); case 'f': return(31); case 'g': return(32);
        case 'h': return(33); case 'i': return(34); case 'j': return(35);
        case 'k': return(36); case 'l': return(37); case 'm': return(38);
        case 'n': return(39); case 'o': return(40); case 'p': return(41);
        case 'q': return(42); case 'r': return(43); case 's': return(44);
        case 't': return(45); case 'u': return(46); case 'v': return(47);
        case 'w': return(48); case 'x': return(49); case 'y': return(50);
        case 'z': return(51); case '0': return(52); case '1': return(53);
        case '2': return(54); case '3': return(55); case '4': return(56);
        case '5': return(57); case '6': return(58); case '7': return(59);
        case '8': return(60); case '9': return(61); case '+': return(62);
        case '/': return(63);

	case ' ': case '\t': case '\n': case '\r': case '\f': return(GIF_SPACE);
	case '=':  return(GIF_PAD);
	case '\0': return(GIF_DONE);
	default: return(GIF_BAD);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Fread --
 *
 *  This procedure calls either fread or Mread to read data
 *  from a file or a base64 encoded string.
 *
 * Results: - same as fread
 *
 *----------------------------------------------------------------------
 */

static int
Fread(dst, hunk, count, chan)
    unsigned char *dst;		/* where to put the result */
    size_t hunk,count;		/* how many */
    Tcl_Channel chan;
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *) 
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    MFile *handle;

    switch (tsdPtr->fromData) {
      case 1:
	return(Mread(dst, hunk, count, (MFile *) chan));
      case 2:
	handle = (MFile *) chan;
	memcpy((VOID *)dst, (VOID *) handle->data, (size_t) (hunk * count));
	handle->data += hunk * count;
	return((int) (hunk * count));
      default:
	return Tcl_Read(chan, (char *) dst, (int) (hunk * count));
    }
}
