/*
 *	Image specifications and image element factory.
 *
 * Copyright © 2004 Pat Thoyts <patthoyts@users.sf.net>
 * Copyright © 2004 Joe English
 *
 * An imageSpec is a multi-element list; the first element
 * is the name of the default image to use, the remainder of the
 * list is a sequence of statespec/imagename options as per
 * [style map].
 */

#include "tkInt.h"
#include "ttkThemeInt.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))

/*
 * Batched tiling (see Ttk_Fill): available where a platform RGBA composite
 * exists -- TK_CAN_RENDER_RGBA covers macOS and Windows, HAVE_XRENDER X11.
 */

#if defined(TK_CAN_RENDER_RGBA) || defined(HAVE_XRENDER)
#define TTK_TILE_BATCH 1
#define TILE_BATCH_MIN_TILES	16	/* Fewer tiles: the loop is fine. */
#define TILE_BATCH_MIN_AREA	(64*64)	/* Match the X11 composite floor. */
#endif

/*------------------------------------------------------------------------
 * +++ ImageSpec management.
 */

struct TtkImageSpec {
    Tk_Image		baseImage;	/* Base image to use */
    Tcl_Obj		*baseName;	/* Name of base image */
    Tcl_Size		mapCount;	/* #state-specific overrides */
    Ttk_StateSpec	*states;	/* array[mapCount] of states ... */
    Tk_Image		*images;	/* ... per-state images to use */
    Tcl_Obj		**names;	/* array[mapCount] of image names */
    Tk_ImageChangedProc *imageChanged;
    void		*imageChangedClientData;
};

/* ImageSpecImageChanged --
 *     Image changes should trigger a repaint.
 */
static void ImageSpecImageChanged(void *clientData,
    int x, int y, int width, int height, int imageWidth, int imageHeight)
{
    Ttk_ImageSpec *imageSpec = (Ttk_ImageSpec *)clientData;
    if (imageSpec->imageChanged != NULL) {
	imageSpec->imageChanged(imageSpec->imageChangedClientData,
		x, y, width, height,
		imageWidth, imageHeight);
    }
}

/* TtkGetImageSpec --
 *	Constructs a Ttk_ImageSpec * from a Tcl_Obj *.
 *	Result must be released using TtkFreeImageSpec.
 *
 */
Ttk_ImageSpec *
TtkGetImageSpec(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr)
{
    return TtkGetImageSpecEx(interp, tkwin, objPtr, NULL, NULL);
}

/* TtkGetImageSpecEx --
 *	Constructs a Ttk_ImageSpec * from a Tcl_Obj *.
 *	Result must be released using TtkFreeImageSpec.
 *	imageChangedProc will be called when not NULL when
 *	the image changes to allow widgets to repaint.
 */
Ttk_ImageSpec *
TtkGetImageSpecEx(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr,
    Tk_ImageChangedProc *imageChangedProc, void *imageChangedClientData)
{
    Ttk_ImageSpec *imageSpec = 0;
    Tcl_Size i = 0, n = 0;
    Tcl_Size objc;
    Tcl_Obj **objv;

    imageSpec = (Ttk_ImageSpec *)Tcl_Alloc(sizeof(*imageSpec));
    imageSpec->baseImage = 0;
    imageSpec->baseName = 0;
    imageSpec->mapCount = 0;
    imageSpec->states = 0;
    imageSpec->images = 0;
    imageSpec->names = 0;
    imageSpec->imageChanged = imageChangedProc;
    imageSpec->imageChangedClientData = imageChangedClientData;

    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
	goto error;
    }

    if ((objc % 2) != 1) {
	if (interp) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"image specification must contain an odd number of elements",
		-1));
	    Tcl_SetErrorCode(interp, "TTK", "IMAGE", "SPEC", (char *)NULL);
	}
	goto error;
    }

    n = (objc - 1) / 2;
    imageSpec->states = (Ttk_StateSpec *)Tcl_Alloc(n * sizeof(Ttk_StateSpec));
    imageSpec->images = (Tk_Image *)Tcl_Alloc(n * sizeof(Tk_Image));
    imageSpec->names = (Tcl_Obj **)Tcl_Alloc(n * sizeof(Tcl_Obj *));

    /* Get base image:
    */
    imageSpec->baseImage = Tk_GetImage(
	    interp, tkwin, Tcl_GetString(objv[0]), ImageSpecImageChanged, imageSpec);
    if (!imageSpec->baseImage) {
	goto error;
    }
    imageSpec->baseName = objv[0];
    Tcl_IncrRefCount(imageSpec->baseName);

    /* Extract state and image specifications:
     */
    for (i = 0; i < n; ++i) {
	Tcl_Obj *stateSpec = objv[2*i + 1];
	const char *imageName = Tcl_GetString(objv[2*i + 2]);
	Ttk_StateSpec state;

	if (Ttk_GetStateSpecFromObj(interp, stateSpec, &state) != TCL_OK) {
	    goto error;
	}
	imageSpec->states[i] = state;

	imageSpec->images[i] = Tk_GetImage(
	    interp, tkwin, imageName, ImageSpecImageChanged, imageSpec);
	if (imageSpec->images[i] == NULL) {
	    goto error;
	}
	imageSpec->names[i] = objv[2*i + 2];
	Tcl_IncrRefCount(imageSpec->names[i]);
	imageSpec->mapCount = i+1;
    }

    return imageSpec;

error:
    TtkFreeImageSpec(imageSpec);
    return NULL;
}

/* TtkFreeImageSpec --
 *	Dispose of an image specification.
 */
void TtkFreeImageSpec(Ttk_ImageSpec *imageSpec)
{
    int i;

    for (i=0; i < imageSpec->mapCount; ++i) {
	Tk_FreeImage(imageSpec->images[i]);
	if (imageSpec->names && imageSpec->names[i]) {
	    Tcl_DecrRefCount(imageSpec->names[i]);
	}
    }

    if (imageSpec->baseImage) { Tk_FreeImage(imageSpec->baseImage); }
    if (imageSpec->baseName) { Tcl_DecrRefCount(imageSpec->baseName); }
    if (imageSpec->states) { Tcl_Free(imageSpec->states); }
    if (imageSpec->images) { Tcl_Free(imageSpec->images); }
    if (imageSpec->names) { Tcl_Free(imageSpec->names); }

    Tcl_Free(imageSpec);
}

/* TtkSelectImage --
 *	Return a state-specific image from an ImageSpec
 */
Tk_Image TtkSelectImage(
    Ttk_ImageSpec *imageSpec,
    TCL_UNUSED(Tk_Window),
    Ttk_State state)
{
    int i;
    for (i = 0; i < imageSpec->mapCount; ++i) {
	if (Ttk_StateMatches(state, imageSpec->states+i)) {
	    return imageSpec->images[i];
	}
    }
    return imageSpec->baseImage;
}

#if !defined(TK_NO_DOUBLE_BUFFERING) || defined(TTK_TILE_BATCH)
/* TtkSelectImageName --
 *	Name of the image TtkSelectImage would pick, for photo lookup.
 */
static Tcl_Obj *TtkSelectImageName(
    Ttk_ImageSpec *imageSpec,
    Ttk_State state)
{
    int i;
    for (i = 0; i < imageSpec->mapCount; ++i) {
	if (Ttk_StateMatches(state, imageSpec->states+i)) {
	    return imageSpec->names[i];
	}
    }
    return imageSpec->baseName;
}
#endif /* !TK_NO_DOUBLE_BUFFERING || TTK_TILE_BATCH */

/*------------------------------------------------------------------------
 * +++ Drawing utilities.
 */

/* LPadding, CPadding, RPadding --
 *	Split a box+padding pair into left, center, and right boxes.
 */
static Ttk_Box LPadding(Ttk_Box b, Ttk_Padding p)
    { return Ttk_MakeBox(b.x, b.y, p.left, b.height); }

static Ttk_Box CPadding(Ttk_Box b, Ttk_Padding p)
    { return Ttk_MakeBox(b.x+p.left, b.y, b.width-p.left-p.right, b.height); }

static Ttk_Box RPadding(Ttk_Box b, Ttk_Padding p)
    { return  Ttk_MakeBox(b.x+b.width-p.right, b.y, p.right, b.height); }

/* TPadding, MPadding, BPadding --
 *	Split a box+padding pair into top, middle, and bottom parts.
 */
static Ttk_Box TPadding(Ttk_Box b, Ttk_Padding p)
    { return Ttk_MakeBox(b.x, b.y, b.width, p.top); }

static Ttk_Box MPadding(Ttk_Box b, Ttk_Padding p)
    { return Ttk_MakeBox(b.x, b.y+p.top, b.width, b.height-p.top-p.bottom); }

static Ttk_Box BPadding(Ttk_Box b, Ttk_Padding p)
    { return Ttk_MakeBox(b.x, b.y+b.height-p.bottom, b.width, p.bottom); }

#ifdef TTK_TILE_BATCH
/* BlockIsPackedRGBA --
 *	True if blk is tightly packed 4-byte pixels in R,G,B,A byte order, i.e.
 *	offset[i] == i for every channel.  This is the one layout AssembleFill
 *	can memcpy without reordering channels.
 */
static bool BlockIsPackedRGBA(const Tk_PhotoImageBlock *blk)
{
    int i;

    if (blk->pixelSize != 4) {
	return false;
    }
    for (i = 0; i < 4; i++) {
	if (blk->offset[i] != i) {
	    return false;
	}
    }
    return true;
}

/* SrcWithinBlock --
 *	True if the src sub-rectangle lies entirely within blk's pixels, so it
 *	can be read without running past the block.
 */
static bool SrcWithinBlock(Ttk_Box src, const Tk_PhotoImageBlock *blk)
{
    return src.x >= 0 && src.y >= 0
	    && src.x + src.width <= blk->width
	    && src.y + src.height <= blk->height;
}

/* NineSlice --
 *	Split a box into the nine regions Ttk_Tile/Ttk_Stripe would draw:
 *	three rows (top/middle/bottom), each cut into three columns
 *	(left/center/right), stored row-major.
 */
static void NineSlice(Ttk_Box b, Ttk_Padding p, Ttk_Box out[9])
{
    Ttk_Box row[3];
    int r;

    row[0] = TPadding(b, p);		/* top */
    row[1] = MPadding(b, p);		/* middle */
    row[2] = BPadding(b, p);		/* bottom */
    for (r = 0; r < 3; r++) {
	out[3*r + 0] = LPadding(row[r], p);	/* left */
	out[3*r + 1] = CPadding(row[r], p);	/* center */
	out[3*r + 2] = RPadding(row[r], p);	/* right */
    }
}

/* AssembleFill --
 *	Client-side analogue of Ttk_Fill: tile the photo's src sub-rectangle
 *	over the dst sub-rectangle of an RGBA buffer that is bufW x bufH
 *	pixels, dst relative to the buffer origin.  Degenerate or
 *	out-of-bounds regions are skipped, like the drawing path skips them.
 */
static void AssembleFill(
    Tk_PhotoImageBlock *blk,
    unsigned char *buf, int bufW, int bufH,
    Ttk_Box src, Ttk_Box dst)
{
    int x, y;

    if (src.width <= 0 || src.height <= 0
	    || dst.width <= 0 || dst.height <= 0
	    || !SrcWithinBlock(src, blk)
	    || dst.x < 0 || dst.y < 0
	    || dst.x + dst.width > bufW
	    || dst.y + dst.height > bufH) {
	return;
    }

    /*
     * Assemble the first band (src.height rows) by horizontal tiling, then
     * replicate whole rows for the remaining bands.
     */

    for (y = 0; y < dst.height; y++) {
	unsigned char *o = buf + ((size_t) (dst.y + y) * bufW + dst.x) * 4;

	if (y < src.height) {
	    unsigned char *s = blk->pixelPtr
		    + (size_t) (src.y + y) * blk->pitch
		    + (size_t) src.x * 4;

	    for (x = 0; x < dst.width; x += src.width) {
		int cw = MIN(src.width, dst.width - x);
		memcpy(o + (size_t) x * 4, s, (size_t) cw * 4);
	    }
	} else {
	    memcpy(o, buf + ((size_t) (dst.y + y % src.height) * bufW
		    + dst.x) * 4, (size_t) dst.width * 4);
	}
    }
}

/* RegionTiles --
 *	Number of tiles Ttk_Fill would draw for one region.
 */
static int RegionTiles(Ttk_Box src, Ttk_Box dst)
{
    if (src.width <= 0 || src.height <= 0
	    || dst.width <= 0 || dst.height <= 0) {
	return 0;
    }
    return ((dst.width + src.width - 1) / src.width)
	    * ((dst.height + src.height - 1) / src.height);
}

/* TileBatchElement --
 *	Draw a whole 9-slice element in one operation: assemble all nine
 *	regions client-side into a single RGBA buffer and composite it with
 *	one TkpPutRGBAImage call, instead of one Tk_RedrawImage per tile.
 *	Returns 1 on success, 0 to fall back to the per-tile loop.
 *
 *	Note: composites the photo's raw pix32, like the partial-alpha
 *	display path does; a non-default -gamma/-palette is not applied.
 */
static int TileBatchElement(
    Tk_Window tkwin,
    Drawable d,
    Tk_PhotoHandle photo,
    Ttk_Box src,
    Ttk_Box dst,
    Ttk_Padding p)
{
    Tk_PhotoImageBlock blk;
    Display *display = Tk_Display(tkwin);
    Ttk_Box src9[9], dst9[9];
    XImage *ximg;
    unsigned char *buf;
    GC gc;
    XGCValues gcv;
    int i, tiles, result;

    if (dst.width <= 0 || dst.height <= 0
	    || dst.width * dst.height < TILE_BATCH_MIN_AREA) {
	return 0;
    }

    Tk_PhotoGetImage(photo, &blk);
    if (!BlockIsPackedRGBA(&blk) || !SrcWithinBlock(src, &blk)) {
	return 0;
    }

    /*
     * Decompose source and destination into the same nine regions
     * Ttk_Tile/Ttk_Stripe would draw.  The destination is taken relative to
     * the buffer origin (0,0) so each region indexes straight into buf.
     */

    NineSlice(src, p, src9);
    NineSlice(Ttk_MakeBox(0, 0, dst.width, dst.height), p, dst9);

    tiles = 0;
    for (i = 0; i < 9; i++) {
	tiles += RegionTiles(src9[i], dst9[i]);
    }
    if (tiles < TILE_BATCH_MIN_TILES) {
	return 0;
    }

    /*
     * Zeroed (fully transparent) so any pixels no region covers composite
     * as a no-op, matching the drawing path leaving them untouched.
     */

    buf = (unsigned char *)
	    Tcl_AttemptAlloc((size_t) dst.width * dst.height * 4);
    if (buf == NULL) {
	return 0;
    }
    memset(buf, 0, (size_t) dst.width * dst.height * 4);

    for (i = 0; i < 9; i++) {
	AssembleFill(&blk, buf, dst.width, dst.height, src9[i], dst9[i]);
    }

    ximg = XCreateImage(display, NULL, 32, ZPixmap, 0, (char *) buf,
	    (unsigned) dst.width, (unsigned) dst.height, 32, 4 * dst.width);
    if (ximg == NULL) {
	Tcl_Free(buf);
	return 0;
    }

    gcv.graphics_exposures = False;
    gc = Tk_GetGC(tkwin, GCGraphicsExposures, &gcv);
    result = TkpPutRGBAImage(display, d, gc, ximg, 0, 0, dst.x, dst.y,
	    (unsigned) dst.width, (unsigned) dst.height);
    Tk_FreeGC(display, gc);
    ximg->data = NULL;
    XDestroyImage(ximg);
    Tcl_Free(buf);
    return result == Success;
}
#endif /* TTK_TILE_BATCH */

/* Ttk_Fill --
 *	Fill the destination area of the drawable by replicating
 *	the source area of the image.
 */
static void Ttk_Fill(
    TCL_UNUSED(Tk_Window),
    Drawable d,
    Tk_Image image,
    Ttk_Box src,
    Ttk_Box dst)
{
    int dr = dst.x + dst.width;
    int db = dst.y + dst.height;
    int x,y;

    if (!(src.width && src.height && dst.width && dst.height)) {
	return;
    }

    for (x = dst.x; x < dr; x += src.width) {
	int cw = MIN(src.width, dr - x);
	for (y = dst.y; y < db; y += src.height) {
	    int ch = MIN(src.height, db - y);
	    Tk_RedrawImage(image, src.x, src.y, cw, ch, d, x, y);
	}
    }
}

/* Ttk_Stripe --
 *	Fill a horizontal stripe of the destination drawable.
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
 *	Fill successive horizontal stripes of the destination drawable.
 */
static void Ttk_Tile(
    Tk_Window tkwin, Drawable d, Tk_Image image, Tk_PhotoHandle photo,
    Ttk_Box src, Ttk_Box dst, Ttk_Padding p)
{
#ifdef TTK_TILE_BATCH
    if (photo != NULL && TileBatchElement(tkwin, d, photo, src, dst, p)) {
	return;
    }
#else
    (void) photo;
#endif
    Ttk_Stripe(tkwin, d, image, TPadding(src,p), TPadding(dst,p), p);
    Ttk_Stripe(tkwin, d, image, MPadding(src,p), MPadding(dst,p), p);
    Ttk_Stripe(tkwin, d, image, BPadding(src,p), BPadding(dst,p), p);
}

/*------------------------------------------------------------------------
 * +++ Image element definition.
 *
 * The image element is a pure renderer (ImageElementDraw tiles into the given
 * drawable); the per-node cache owns the composited result.  This element only
 * advertises that it is cacheable and reports opacity + epoch via
 * ImageElementCacheInfo.
 */

typedef struct {		/* ClientData for image elements */
    Ttk_ImageSpec *imageSpec;	/* Image(s) to use */
    int minWidth;		/* Minimum width; overrides image width */
    int minHeight;		/* Minimum height; overrides image height */
    Ttk_Sticky sticky;		/* -stickiness specification */
    Ttk_Padding border;		/* Fixed border region */
    Ttk_Padding padding;	/* Internal padding */
    unsigned epoch;		/* Bumps when a source image's pixels change */

#ifdef TILE_07_COMPAT
    Ttk_ResourceCache cache;	/* Resource cache for images */
    Ttk_StateMap imageMap;	/* State-based lookup table for images */
#endif
} ImageData;

/* ImageElementImageChanged --
 *	A source image's pixels changed; bump the content epoch.
 */
static void ImageElementImageChanged(
    void *clientData,
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    ImageData *imageData = (ImageData *)clientData;
    imageData->epoch++;
}

static void FreeImageData(void *clientData)
{
    ImageData *imageData = (ImageData *)clientData;
    if (imageData->imageSpec)	{ TtkFreeImageSpec(imageData->imageSpec); }
#ifdef TILE_07_COMPAT
    if (imageData->imageMap)	{ Tcl_DecrRefCount(imageData->imageMap); }
#endif
    Tcl_Free(clientData);
}

#ifndef TK_NO_DOUBLE_BUFFERING
/* ImageIsOpaque --
 *	Return 1 if the named photo is fully opaque (every pixel alpha 255).
 *	Anything not a readable photo is treated as translucent.
 */
static int ImageIsOpaque(Tcl_Interp *interp, Tcl_Obj *imageName)
{
    Tk_PhotoHandle photo;
    Tk_PhotoImageBlock block;
    int x, y, alphaOff;

    if (interp == NULL || imageName == NULL) {
	return 0;
    }
    photo = Tk_FindPhoto(interp, Tcl_GetString(imageName));
    if (photo == NULL || !Tk_PhotoGetImage(photo, &block)) {
	return 0;
    }
    if (block.pixelSize < 4) {
	return 1;		/* No alpha channel: opaque. */
    }
    alphaOff = block.offset[3];
    for (y = 0; y < block.height; y++) {
	unsigned char *pixPtr = block.pixelPtr + y * block.pitch + alphaOff;
	for (x = 0; x < block.width; x++) {
	    if (*pixPtr != 255) {
		return 0;
	    }
	    pixPtr += block.pixelSize;
	}
    }
    return 1;
}

/* ImageElementCacheInfo --
 *	Report the element's content epoch and whether it opaquely covers the
 *	parcel -- the selected image is opaque AND -sticky fills the box.  A
 *	non-filling -sticky leaves margins, so it counts as translucent.
 */
static void ImageElementCacheInfo(
    void *clientData,
    TCL_UNUSED(void *), /* elementRecord */
    Tk_Window tkwin,
    Ttk_Box b,
    Ttk_State state,
    Ttk_ElementCacheInfo *info)
{
    ImageData *imageData = (ImageData *)clientData;
    Tk_Image image = TtkSelectImage(imageData->imageSpec, tkwin, state);
    int imgWidth, imgHeight;
    Ttk_Box dst;

    info->epoch = imageData->epoch;
    info->opaque = 0;

    if (image == NULL) {
	return;
    }
    Tk_SizeOfImage(image, &imgWidth, &imgHeight);
    dst = Ttk_StickBox(b, imgWidth, imgHeight, imageData->sticky);
    if (dst.x == b.x && dst.y == b.y
	    && dst.width == b.width && dst.height == b.height) {
	info->opaque = ImageIsOpaque(Tk_Interp(tkwin),
		TtkSelectImageName(imageData->imageSpec, state));
    }
}
#endif /* !TK_NO_DOUBLE_BUFFERING */

static void ImageElementSize(
    void *clientData,
    TCL_UNUSED(void *), /* elementRecord */
    TCL_UNUSED(Tk_Window),
    TCL_UNUSED(Ttk_State), /* state */
    int *widthPtr,
    int *heightPtr,
    Ttk_Padding *paddingPtr)
{
    ImageData *imageData = (ImageData *)clientData;
    Tk_Image image = imageData->imageSpec->baseImage;

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
}

static void ImageElementDraw(
    void *clientData,
    TCL_UNUSED(void *), /* elementRecord */
    Tk_Window tkwin,
    Drawable d,
    Ttk_Box b,
    Ttk_State state)
{
    ImageData *imageData = (ImageData *)clientData;
    Tk_Image image = 0;
    Tk_PhotoHandle photo = NULL;
    int imgWidth, imgHeight;
    Ttk_Box src, dst;

#ifdef TILE_07_COMPAT
    if (imageData->imageMap) {
	Tcl_Obj *imageObj = Ttk_StateMapLookup(NULL,imageData->imageMap,state);
	if (imageObj) {
	    image = Ttk_UseImage(imageData->cache, tkwin, imageObj);
	}
    }
    if (!image) {
	image = TtkSelectImage(imageData->imageSpec, tkwin, state);
    }
#else
    image = TtkSelectImage(imageData->imageSpec, tkwin, state);
#endif

    if (!image) {
	return;
    }

#ifdef TTK_TILE_BATCH
    /*
     * Resolve the photo behind the spec-selected image for batched tiling.
     * NULL (non-photo image, or a TILE_07_COMPAT map image whose name is
     * not in the spec) keeps the per-tile loop.
     */
#ifdef TILE_07_COMPAT
    if (!imageData->imageMap)
#endif
    {
	Tcl_Obj *nameObj = TtkSelectImageName(imageData->imageSpec, state);

	if (nameObj != NULL) {
	    photo = Tk_FindPhoto(Tk_Interp(tkwin), Tcl_GetString(nameObj));
	}
    }
#endif /* TTK_TILE_BATCH */

    Tk_SizeOfImage(image, &imgWidth, &imgHeight);
    src = Ttk_MakeBox(0, 0, imgWidth, imgHeight);
    dst = Ttk_StickBox(b, imgWidth, imgHeight, imageData->sticky);

    Ttk_Tile(tkwin, d, image, photo, src, dst, imageData->border);
}

static const Ttk_ElementSpec ImageElementSpec =
{
    TK_STYLE_VERSION_2,
    sizeof(NullElement),
    TtkNullElementOptions,
    ImageElementSize,
    ImageElementDraw
};

/*------------------------------------------------------------------------
 * +++ Image element factory.
 */
static int
Ttk_CreateImageElement(
    Tcl_Interp *interp,
    TCL_UNUSED(void *),
    Ttk_Theme theme,
    const char *elementName,
    Tcl_Size objc, Tcl_Obj *const objv[])
{
    static const char *const optionStrings[] =
	 { "-border","-height","-padding","-sticky","-width",NULL };
    enum { O_BORDER, O_HEIGHT, O_PADDING, O_STICKY, O_WIDTH };

    Ttk_ImageSpec *imageSpec = 0;
    ImageData *imageData = 0;
    Ttk_ElementClass *elementClass;
    int padding_specified = 0;
    Tcl_Size i;

    if (objc + 1 < 2) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
		"Must supply a base image", -1));
	Tcl_SetErrorCode(interp, "TTK", "IMAGE", "BASE", (char *)NULL);
	return TCL_ERROR;
    }

    imageData = (ImageData *)Tcl_Alloc(sizeof(*imageData));
    memset(imageData, 0, sizeof(*imageData));

    imageSpec = TtkGetImageSpecEx(interp, Tk_MainWindow(interp), objv[0],
	    ImageElementImageChanged, imageData);
    if (!imageSpec) {
	Tcl_Free(imageData);
	return TCL_ERROR;
    }

    imageData->imageSpec = imageSpec;
    imageData->minWidth = imageData->minHeight = -1;
    imageData->sticky = TTK_FILL_BOTH;
    imageData->border = imageData->padding = Ttk_UniformPadding(0);
#ifdef TILE_07_COMPAT
    imageData->cache = Ttk_GetResourceCache(interp);
    imageData->imageMap = 0;
#endif

    for (i = 1; i < objc; i += 2) {
	int option;

	if (i == objc - 1) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "Value for %s missing", Tcl_GetString(objv[i])));
	    Tcl_SetErrorCode(interp, "TTK", "IMAGE", "VALUE", (char *)NULL);
	    goto error;
	}

#ifdef TILE_07_COMPAT
	if (!strcmp("-map", Tcl_GetString(objv[i]))) {
	    imageData->imageMap = objv[i+1];
	    Tcl_IncrRefCount(imageData->imageMap);
	    continue;
	}
#endif

	if (Tcl_GetIndexFromObjStruct(interp, objv[i], optionStrings,
		sizeof(char *), "option", 0, &option) != TCL_OK) {
	    goto error;
	}

	switch (option) {
	    case O_BORDER:
		if (Ttk_GetBorderFromObj(interp, objv[i+1], &imageData->border)
			!= TCL_OK) {
		    goto error;
		}
		if (!padding_specified) {
		    imageData->padding = imageData->border;
		}
		break;
	    case O_PADDING:
		if (Ttk_GetBorderFromObj(interp, objv[i+1], &imageData->padding)
			!= TCL_OK) { goto error; }
		padding_specified = 1;
		break;
	    case O_WIDTH:
		if (Tcl_GetIntFromObj(interp, objv[i+1], &imageData->minWidth)
			!= TCL_OK) { goto error; }
		break;
	    case O_HEIGHT:
		if (Tcl_GetIntFromObj(interp, objv[i+1], &imageData->minHeight)
			!= TCL_OK) { goto error; }
		break;
	    case O_STICKY:
		if (Ttk_GetStickyFromObj(interp, objv[i+1], &imageData->sticky)
			!= TCL_OK) { goto error; }
	}
    }

    elementClass = Ttk_RegisterElement(interp, theme, elementName,
	    &ImageElementSpec, imageData);
    if (!elementClass) {
	goto error;
    }
#ifndef TK_NO_DOUBLE_BUFFERING
    TtkSetElementCachePolicy(elementClass, TTK_ELEMENT_CACHEABLE,
	    ImageElementCacheInfo);
#endif

    Ttk_RegisterCleanup(interp, imageData, FreeImageData);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(elementName, -1));
    return TCL_OK;

error:
    FreeImageData(imageData);
    return TCL_ERROR;
}

MODULE_SCOPE void
TtkImage_Init(Tcl_Interp *interp)
{
    Ttk_RegisterElementFactory(interp, "image", Ttk_CreateImageElement, NULL);
}

/*EOF*/
