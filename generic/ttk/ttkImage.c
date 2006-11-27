/* $Id: ttkImage.c,v 1.3 2006/11/27 06:53:55 jenglish Exp $
 * 	Ttk widget set -- image element factory.
 *
 * Copyright (C) 2004 Pat Thoyts <patthoyts@users.sf.net>
 * Copyright (C) 2004 Joe English
 *
 */

#include <string.h>
#include <tk.h>
#include "ttkTheme.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))

/*------------------------------------------------------------------------
 * +++ Drawing utilities.
 */

/* LPadding, CPadding, RPadding --
 * 	Split a box+padding pair into left, center, and right boxes.
 */
static Ttk_Box LPadding(Ttk_Box b, Ttk_Padding p)
    { return Ttk_MakeBox(b.x, b.y, p.left, b.height); }

static Ttk_Box CPadding(Ttk_Box b, Ttk_Padding p)
    { return Ttk_MakeBox(b.x+p.left, b.y, b.width-p.left-p.right, b.height); }

static Ttk_Box RPadding(Ttk_Box b, Ttk_Padding p)
    { return  Ttk_MakeBox(b.x+b.width-p.right, b.y, p.right, b.height); }

/* TPadding, MPadding, BPadding --
 * 	Split a box+padding pair into top, middle, and bottom parts.
 */
static Ttk_Box TPadding(Ttk_Box b, Ttk_Padding p)
    { return Ttk_MakeBox(b.x, b.y, b.width, p.top); }

static Ttk_Box MPadding(Ttk_Box b, Ttk_Padding p)
    { return Ttk_MakeBox(b.x, b.y+p.top, b.width, b.height-p.top-p.bottom); }

static Ttk_Box BPadding(Ttk_Box b, Ttk_Padding p)
    { return Ttk_MakeBox(b.x, b.y+b.height-p.bottom, b.width, p.bottom); }

/* Ttk_Fill --
 *	Fill the destination area of the drawable by replicating
 *	the source area of the image.
 */
static void Ttk_Fill(
    Tk_Window tkwin, Drawable d, Tk_Image image, Ttk_Box src, Ttk_Box dst)
{
    int dr = dst.x + dst.width;
    int db = dst.y + dst.height;
    int x,y;

    if (!(src.width && src.height && dst.width && dst.height))
	return;

    for (x = dst.x; x < dr; x += src.width) {
	int cw = MIN(src.width, dr - x);
	for (y = dst.y; y <= db; y += src.height) {
	    int ch = MIN(src.height, db - y);
	    Tk_RedrawImage(image, src.x, src.y, cw, ch, d, x, y);
	}
    }
}

/* Ttk_Stripe --
 * 	Fill a horizontal stripe of the destination drawable.
 */
static void Ttk_Stripe(
    Tk_Window tkwin, Drawable d, Tk_Image image,
    Ttk_Box src, Ttk_Box dst, Ttk_Padding p)
{
    Ttk_Fill(tkwin, d, image, LPadding(src,p), LPadding(dst,p));
    Ttk_Fill(tkwin, d, image, CPadding(src,p), CPadding(dst,p));
    Ttk_Fill(tkwin, d, image, RPadding(src,p), RPadding(dst,p));
}

/* Ttk_Tile --
 * 	Fill successive horizontal stripes of the destination drawable.
 */
static void Ttk_Tile(
    Tk_Window tkwin, Drawable d, Tk_Image image,
    Ttk_Box src, Ttk_Box dst, Ttk_Padding p)
{
    Ttk_Stripe(tkwin, d, image, TPadding(src,p), TPadding(dst,p), p);
    Ttk_Stripe(tkwin, d, image, MPadding(src,p), MPadding(dst,p), p);
    Ttk_Stripe(tkwin, d, image, BPadding(src,p), BPadding(dst,p), p);
}

/*------------------------------------------------------------------------
 * +++ Image element definition.
 */

typedef struct {		/* ClientData for image elements */
    Ttk_ResourceCache cache;	/* Resource cache for images */
    Tcl_Obj *baseImage; 	/* Name of default image */
    Ttk_StateMap imageMap;	/* State-based lookup table for images */
    Tcl_Obj *stickyObj;	 	/* Stickiness specification, NWSE */
    Tcl_Obj *borderObj;		/* Border specification */
    Tcl_Obj *paddingObj;	/* Padding specification */
    int minWidth;		/* Minimum width; overrides image width */
    int minHeight;		/* Minimum width; overrides image width */
    unsigned sticky;
    Ttk_Padding border;		/* Fixed border region */
    Ttk_Padding padding;	/* Internal padding */
} ImageData;

static void FreeImageData(void *clientData)
{
    ImageData *imageData = clientData;
    Tcl_DecrRefCount(imageData->baseImage);
    if (imageData->imageMap)	{ Tcl_DecrRefCount(imageData->imageMap); }
    if (imageData->stickyObj)	{ Tcl_DecrRefCount(imageData->stickyObj); }
    if (imageData->borderObj)	{ Tcl_DecrRefCount(imageData->borderObj); }
    if (imageData->paddingObj)	{ Tcl_DecrRefCount(imageData->paddingObj); }
    ckfree(clientData);
}

static Tk_OptionSpec ImageOptionSpecs[] =
{
    { TK_OPTION_STRING, "-sticky", "sticky", "Sticky",
	"nswe", Tk_Offset(ImageData,stickyObj), -1,
	0,0,0 },
    { TK_OPTION_STRING, "-border", "border", "Border",
	"0", Tk_Offset(ImageData,borderObj), -1,
	0,0,0 },
    { TK_OPTION_STRING, "-padding", "padding", "Padding",
	NULL, Tk_Offset(ImageData,paddingObj), -1,
	TK_OPTION_NULL_OK,0,0 },
    { TK_OPTION_STRING, "-map", "map", "Map",
	"", Tk_Offset(ImageData,imageMap), -1,
	0,0,0 },
    { TK_OPTION_INT, "-width", "width", "Width",
	"-1", -1, Tk_Offset(ImageData, minWidth),
	0, 0, 0},
    { TK_OPTION_INT, "-height", "height", "Height",
	"-1", -1, Tk_Offset(ImageData, minHeight),
	0, 0, 0},
    { TK_OPTION_END }
};

static void ImageElementGeometry(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    int *widthPtr, int *heightPtr, Ttk_Padding *paddingPtr)
{
    ImageData *imageData = clientData;
    Tk_Image image = Ttk_UseImage(imageData->cache,tkwin,imageData->baseImage);

    if (image) {
	Tk_SizeOfImage(image, widthPtr, heightPtr);
    }
    if (imageData->minWidth >= 0) {
	*widthPtr = imageData->minWidth;
    }
    if (imageData->minHeight >= 0) {
	*heightPtr = imageData->minHeight;
    }

    *paddingPtr = imageData->padding;
    *widthPtr -= Ttk_PaddingWidth(imageData->padding);
    *heightPtr -= Ttk_PaddingHeight(imageData->padding);
}

static void ImageElementDraw(
    void *clientData, void *elementRecord, Tk_Window tkwin,
    Drawable d, Ttk_Box b, unsigned int state)
{
    ImageData *imageData = clientData;
    Tcl_Obj *imageObj = 0;
    Tk_Image image;
    int imgWidth, imgHeight;
    Ttk_Box src, dst;

    if (imageData->imageMap) {
	imageObj = Ttk_StateMapLookup(NULL, imageData->imageMap, state);
    }
    if (!imageObj) {
	imageObj = imageData->baseImage;
    }
    image = Ttk_UseImage(imageData->cache, tkwin, imageObj);

    if (!image) {
	return;
    }

    Tk_SizeOfImage(image, &imgWidth, &imgHeight);
    src = Ttk_MakeBox(0, 0, imgWidth, imgHeight);
    dst = Ttk_StickBox(b, imgWidth, imgHeight, imageData->sticky);

    Ttk_Tile(tkwin, d, image, src, dst, imageData->border);
}

static Ttk_ElementSpec ImageElementSpec =
{
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    ImageElementGeometry,
    ImageElementDraw
};

/*------------------------------------------------------------------------
 * +++ Image element factory.
 */
static int
Ttk_CreateImageElement(
    Tcl_Interp *interp,
    void *clientData,
    Ttk_Theme theme,
    const char *elementName,
    int objc, Tcl_Obj *CONST objv[])
{
    Tk_OptionTable imageOptionTable =
	Tk_CreateOptionTable(interp, ImageOptionSpecs);
    ImageData *imageData;

    imageData = (ImageData*)ckalloc(sizeof(*imageData));

    if (objc <= 0) {
	Tcl_AppendResult(interp, "Must supply a base image", NULL);
	return TCL_ERROR;
    }

    imageData->cache = Ttk_GetResourceCache(interp);
    imageData->imageMap = imageData->stickyObj
	= imageData->borderObj = imageData->paddingObj = 0;
    imageData->minWidth = imageData->minHeight = -1;
    imageData->sticky = TTK_FILL_BOTH;	/* ??? Is this sensible */
    imageData->border = imageData->padding = Ttk_UniformPadding(0);

    /* Can't use Tk_InitOptions() here, since we don't have a Tk_Window
     */
    if (TCL_OK != Tk_SetOptions(interp, (ClientData)imageData,
	    imageOptionTable, objc-1, objv+1,
	    NULL/*tkwin*/, NULL/*savedOptions*/, NULL/*mask*/))
    {
	ckfree((ClientData)imageData);
	return TCL_ERROR;
    }

    imageData->baseImage = Tcl_DuplicateObj(objv[0]);

    if (imageData->borderObj && Ttk_GetBorderFromObj(
		interp, imageData->borderObj, &imageData->border) != TCL_OK)
    {
	goto error;
    }

    imageData->padding = imageData->border;

    if (imageData->paddingObj && Ttk_GetBorderFromObj(
		interp, imageData->paddingObj, &imageData->padding) != TCL_OK)
    {
	goto error;
    }

    if (imageData->stickyObj && Ttk_GetStickyFromObj(
		interp, imageData->stickyObj, &imageData->sticky) != TCL_OK)
    {
	goto error;
    }

    if (!Ttk_RegisterElement(interp, theme,
				elementName, &ImageElementSpec, imageData))
    {
	goto error;
    }

    Ttk_RegisterCleanup(interp, imageData, FreeImageData);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(elementName, -1));
    return TCL_OK;

error:
    FreeImageData(imageData);
    return TCL_ERROR;
}

MODULE_SCOPE int Ttk_ImageInit(Tcl_Interp *);
int Ttk_ImageInit(Tcl_Interp *interp)
{
    return Ttk_RegisterElementFactory(interp, "image", Ttk_CreateImageElement, NULL);
}

/*EOF*/
