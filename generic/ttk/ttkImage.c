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
#include "ttkTheme.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))

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

/* NullImageChanged --
 *	Do-nothing Tk_ImageChangedProc.
 */
static void NullImageChanged(
    TCL_UNUSED(void *),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    /* No-op */
}

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
	    interp, tkwin, imageName, NullImageChanged, NULL);
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

#ifndef TK_NO_DOUBLE_BUFFERING
/* TtkSelectImageName --
 *	Return the name of the state-specific image selected by
 *	TtkSelectImage, so the corresponding photo can be looked up.
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
#endif /* !TK_NO_DOUBLE_BUFFERING */

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

#ifndef TK_NO_DOUBLE_BUFFERING
/*
 * Rendered-element cache (per element).  Opaque elements are cached as a plain
 * pixmap and blitted with XCopyArea -- fast, and correct because an opaque
 * element fully covers its box, so the cached pixels do not depend on the
 * background.  Translucent elements are composed into a background-free RGBA
 * photo and drawn with Tk_RedrawImage, which re-blends them against the live
 * destination every frame so the result is never stale.
 */
typedef struct {
    /* Opaque fast path: */
    Pixmap pixmap;		/* Cached composited pixmap, or None */
    Display *pixmapDisplay;	/* Display owning pixmap */
    /* Translucent path: */
    Tk_PhotoHandle composed;	/* Internal composed photo, or NULL */
    Tcl_Obj *composedName;	/* Name of the internal composed photo */
    Tk_Image composedImage;	/* Drawing instance of `composed`, or NULL */
    Tk_Window composedFor;	/* Window the instance was created for */
    /* Shared cache key: */
    int cachedWidth;		/* Width the element was cached at */
    int cachedHeight;		/* Height the element was cached at */
    Ttk_State cachedState;	/* State the element was cached for */
    int cachedValid;		/* Nonzero when the cache holds the key */
    int cachedOpaque;		/* Nonzero: use pixmap; else use photo */
} ElementImageCache;
#endif

typedef struct {		/* ClientData for image elements */
    Ttk_ImageSpec *imageSpec;	/* Image(s) to use */
    int minWidth;		/* Minimum width; overrides image width */
    int minHeight;		/* Minimum height; overrides image height */
    Ttk_Sticky sticky;		/* -stickiness specification */
    Ttk_Padding border;		/* Fixed border region */
    Ttk_Padding padding;	/* Internal padding */

#ifdef TILE_07_COMPAT
    Ttk_ResourceCache cache;	/* Resource cache for images */
    Ttk_StateMap imageMap;	/* State-based lookup table for images */
#endif

#ifndef TK_NO_DOUBLE_BUFFERING
    ElementImageCache *imageCache;	/* Rendered-element cache, or NULL */
#endif
} ImageData;

#ifndef TK_NO_DOUBLE_BUFFERING
/* GetImageCache --
 *	Return the element's render cache, allocating it on first use.
 */
static ElementImageCache *GetImageCache(ImageData *imageData)
{
    if (imageData->imageCache == NULL) {
	imageData->imageCache = (ElementImageCache *)
		Tcl_Alloc(sizeof(ElementImageCache));
	memset(imageData->imageCache, 0, sizeof(ElementImageCache));
    }
    return imageData->imageCache;
}

/* InvalidateImageCache --
 *	Mark the composed photo stale.  The photo and drawing instance are
 *	retained and reused on the next compose.  A no-op if there is no cache.
 */
static void InvalidateImageCache(ElementImageCache *cache)
{
    if (cache == NULL) {
	return;
    }
    cache->cachedValid = 0;
    cache->cachedWidth = 0;
    cache->cachedHeight = 0;
    cache->cachedState = 0;
}

/* ComposedWindowEventProc --
 *	Drop the cached drawing instance when the window it was created for is
 *	destroyed, so the cache never holds an image instance bound to a dead
 *	window.  Tk removes the window's event handlers itself as part of the
 *	destruction, so we do not delete this handler here.
 */
static void ComposedWindowEventProc(void *clientData, XEvent *eventPtr)
{
    ElementImageCache *cache = (ElementImageCache *)clientData;

    if (eventPtr->type == DestroyNotify && cache->composedImage != NULL) {
	Tk_FreeImage(cache->composedImage);
	cache->composedImage = NULL;
	cache->composedFor = NULL;
    }
}

/* FreeComposedInstance --
 *	Release the cached drawing instance (and its window event handler), if
 *	any.  composedFor is a live window whenever composedImage is non-NULL,
 *	since ComposedWindowEventProc clears both on window destruction.
 */
static void FreeComposedInstance(ElementImageCache *cache)
{
    if (cache->composedImage != NULL) {
	Tk_DeleteEventHandler(cache->composedFor, StructureNotifyMask,
		ComposedWindowEventProc, cache);
	Tk_FreeImage(cache->composedImage);
	cache->composedImage = NULL;
	cache->composedFor = NULL;
    }
}

/* FreeImageCache --
 *	Release the composed photo's drawing instance and the cache struct.
 *	This runs only during interpreter teardown (Ttk cleanups fire from
 *	Ttk_StylePkgFree), so the composed photo image command itself is left
 *	for the interpreter to reclaim -- an explicit "image delete" there is
 *	both unnecessary and unsafe.
 */
static void FreeImageCache(ImageData *imageData)
{
    ElementImageCache *cache = imageData->imageCache;

    if (cache == NULL) {
	return;
    }
    FreeComposedInstance(cache);
    if (cache->composedName != NULL) {
	Tcl_DecrRefCount(cache->composedName);
    }
    if (cache->pixmap != None) {
	Tk_FreePixmap(cache->pixmapDisplay, cache->pixmap);
    }
    Tcl_Free(cache);
    imageData->imageCache = NULL;
}

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
    InvalidateImageCache(imageData->imageCache);
}
#endif /* !TK_NO_DOUBLE_BUFFERING */

static void FreeImageData(void *clientData)
{
    ImageData *imageData = (ImageData *)clientData;
#ifndef TK_NO_DOUBLE_BUFFERING
    FreeImageCache(imageData);
#endif
    if (imageData->imageSpec)	{ TtkFreeImageSpec(imageData->imageSpec); }
#ifdef TILE_07_COMPAT
    if (imageData->imageMap)	{ Tcl_DecrRefCount(imageData->imageMap); }
#endif
    Tcl_Free(clientData);
}

#ifndef TK_NO_DOUBLE_BUFFERING
/* DiscardComposedPhoto --
 *	Tear down everything tied to the current composed photo: its drawing
 *	instance, the cached name, the (now invalid) handle, and the composed
 *	content key.  Used when the photo has vanished from under us.
 */
static void DiscardComposedPhoto(ElementImageCache *cache)
{
    FreeComposedInstance(cache);
    if (cache->composedName != NULL) {
	Tcl_DecrRefCount(cache->composedName);
	cache->composedName = NULL;
    }
    cache->composed = NULL;
    InvalidateImageCache(cache);
}

/* EnsureComposedPhoto --
 *	Make sure the private photo that holds the composed element exists and
 *	is still live, creating it on first use and recreating it if a script
 *	has deleted it.  Returns 1 on success, 0 if the photo is unavailable
 *	(in which case the caller falls back to direct tiling).
 */
static int EnsureComposedPhoto(ElementImageCache *cache, Tcl_Interp *interp)
{
    Tcl_Obj *cmd;
    int code;

    if (interp == NULL || Tcl_InterpDeleted(interp)) {
	return 0;
    }

    /*
     * The composed photo is a real image command, so a script could delete
     * (or replace) it out from under us.  Re-resolve it from its name every
     * time rather than trusting a cached handle, which would dangle after an
     * external "image delete".  Tk_FindPhoto is a cheap hash lookup.
     */
    if (cache->composedName != NULL) {
	cache->composed = Tk_FindPhoto(interp,
		Tcl_GetString(cache->composedName));
	if (cache->composed != NULL) {
	    return 1;
	}
	/* It vanished: discard the stale instance and key, then recreate. */
	DiscardComposedPhoto(cache);
    }

    cache->composedName = Tcl_ObjPrintf("::ttk::_elemcache_%p", (void *)cache);
    Tcl_IncrRefCount(cache->composedName);

    cmd = Tcl_ObjPrintf("image create photo %s",
	    Tcl_GetString(cache->composedName));
    Tcl_IncrRefCount(cmd);
    code = Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL|TCL_EVAL_DIRECT);
    Tcl_DecrRefCount(cmd);
    Tcl_ResetResult(interp);

    if (code != TCL_OK) {
	Tcl_DecrRefCount(cache->composedName);
	cache->composedName = NULL;
	return 0;
    }
    cache->composed = Tk_FindPhoto(interp, Tcl_GetString(cache->composedName));
    if (cache->composed == NULL) {
	Tcl_DecrRefCount(cache->composedName);
	cache->composedName = NULL;
	return 0;
    }
    return 1;
}

/* PutTile --
 *	Replicate one source sub-block (cell of the 9-slice) across the
 *	corresponding destination cell of the composed photo, tiling it the
 *	same way Ttk_Fill tiles into a drawable.
 */
static void PutTile(
    Tcl_Interp *interp,
    Tk_PhotoHandle dest,
    Tk_PhotoImageBlock *srcBlock,
    Ttk_Box sCell,
    Ttk_Box dCell)
{
    Tk_PhotoImageBlock sub;

    if (sCell.width <= 0 || sCell.height <= 0
	    || dCell.width <= 0 || dCell.height <= 0) {
	return;
    }
    sub = *srcBlock;
    sub.pixelPtr = srcBlock->pixelPtr
	    + sCell.y * srcBlock->pitch
	    + sCell.x * srcBlock->pixelSize;
    sub.width = sCell.width;
    sub.height = sCell.height;
    Tk_PhotoPutBlock(interp, dest, &sub, dCell.x, dCell.y,
	    dCell.width, dCell.height, TK_PHOTO_COMPOSITE_SET);
}

/* BuildComposedPhoto --
 *	(Re)compose the tiled element into the cache photo at size w x h,
 *	using the same 9-slice partition as Ttk_Tile.  Returns 1 on success.
 */
static int BuildComposedPhoto(
    ElementImageCache *cache,
    Tcl_Interp *interp,
    Tk_PhotoHandle src,
    Ttk_Box srcBox,
    int w, int h,
    Ttk_Padding p)
{
    Tk_PhotoHandle dest = cache->composed;
    Tk_PhotoImageBlock srcBlock;
    Ttk_Box dstBox = Ttk_MakeBox(0, 0, w, h);
    Ttk_Box sRow, dRow;

    if (!Tk_PhotoGetImage(src, &srcBlock)) {
	return 0;
    }
    if (Tk_PhotoSetSize(interp, dest, w, h) != TCL_OK) {
	return 0;
    }
    Tk_PhotoBlank(dest);

    sRow = TPadding(srcBox, p); dRow = TPadding(dstBox, p);
    PutTile(interp, dest, &srcBlock, LPadding(sRow,p), LPadding(dRow,p));
    PutTile(interp, dest, &srcBlock, CPadding(sRow,p), CPadding(dRow,p));
    PutTile(interp, dest, &srcBlock, RPadding(sRow,p), RPadding(dRow,p));

    sRow = MPadding(srcBox, p); dRow = MPadding(dstBox, p);
    PutTile(interp, dest, &srcBlock, LPadding(sRow,p), LPadding(dRow,p));
    PutTile(interp, dest, &srcBlock, CPadding(sRow,p), CPadding(dRow,p));
    PutTile(interp, dest, &srcBlock, RPadding(sRow,p), RPadding(dRow,p));

    sRow = BPadding(srcBox, p); dRow = BPadding(dstBox, p);
    PutTile(interp, dest, &srcBlock, LPadding(sRow,p), LPadding(dRow,p));
    PutTile(interp, dest, &srcBlock, CPadding(sRow,p), CPadding(dRow,p));
    PutTile(interp, dest, &srcBlock, RPadding(sRow,p), RPadding(dRow,p));

    return 1;
}

/* SourceIsOpaque --
 *	Return 1 if every pixel of the source photo is fully opaque (alpha
 *	255), so the tiled element can be cached as a plain pixmap.  A photo
 *	whose pixels cannot be read is treated as translucent (always-correct
 *	fallback).
 */
static int SourceIsOpaque(Tk_PhotoHandle photo)
{
    Tk_PhotoImageBlock block;
    int x, y, alphaOff;

    if (!Tk_PhotoGetImage(photo, &block)) {
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

/* BuildOpaquePixmap --
 *	(Re)tile an opaque element into the cache pixmap at size w x h.  No
 *	background seed is needed: an opaque element fully covers the pixmap.
 *	Returns 1 on success, 0 if a pixmap could not be allocated.
 */
static int BuildOpaquePixmap(
    ElementImageCache *cache,
    Tk_Window tkwin,
    Tk_Image image,
    Ttk_Box src,
    int w, int h,
    Ttk_Padding p)
{
    Display *display = Tk_Display(tkwin);
    Pixmap pixmap;

    if (Tk_WindowId(tkwin) == None) {
	return 0;		/* Window not realized. */
    }
    pixmap = Tk_GetPixmap(display, Tk_WindowId(tkwin), w, h, Tk_Depth(tkwin));
    if (pixmap == None) {
	return 0;
    }
    Ttk_Tile(tkwin, pixmap, image, src, Ttk_MakeBox(0, 0, w, h), p);

    if (cache->pixmap != None) {
	Tk_FreePixmap(cache->pixmapDisplay, cache->pixmap);
    }
    cache->pixmap = pixmap;
    cache->pixmapDisplay = display;
    return 1;
}

/* EnsureComposedImageInstance --
 *	Make sure we hold a drawing instance of the composed photo suitable
 *	for the given window.  Recreated when the window changes.
 */
static int EnsureComposedImageInstance(
    ElementImageCache *cache,
    Tcl_Interp *interp,
    Tk_Window tkwin)
{
    if (cache->composedImage != NULL && cache->composedFor == tkwin) {
	return 1;
    }
    /*
     * Switching to a different window (or first use).  If an instance exists,
     * composedFor is still a live window here: had it been destroyed,
     * ComposedWindowEventProc would have cleared composedImage/composedFor.
     */
    FreeComposedInstance(cache);
    cache->composedImage = Tk_GetImage(interp, tkwin,
	    Tcl_GetString(cache->composedName), NullImageChanged, NULL);
    if (cache->composedImage == NULL) {
	return 0;
    }
    cache->composedFor = tkwin;
    Tk_CreateEventHandler(tkwin, StructureNotifyMask,
	    ComposedWindowEventProc, cache);
    return 1;
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
    int imgWidth, imgHeight;
    Ttk_Box src, dst;
#ifndef TK_NO_DOUBLE_BUFFERING
    Tcl_Obj *imageName = NULL;
#endif

#ifdef TILE_07_COMPAT
    if (imageData->imageMap) {
	Tcl_Obj *imageObj = Ttk_StateMapLookup(NULL,imageData->imageMap,state);
	if (imageObj) {
	    image = Ttk_UseImage(imageData->cache, tkwin, imageObj);
#ifndef TK_NO_DOUBLE_BUFFERING
	    imageName = imageObj;
#endif
	}
    }
    if (!image) {
	image = TtkSelectImage(imageData->imageSpec, tkwin, state);
#ifndef TK_NO_DOUBLE_BUFFERING
	imageName = TtkSelectImageName(imageData->imageSpec, state);
#endif
    }
#else
    image = TtkSelectImage(imageData->imageSpec, tkwin, state);
#ifndef TK_NO_DOUBLE_BUFFERING
    imageName = TtkSelectImageName(imageData->imageSpec, state);
#endif
#endif

    if (!image) {
	return;
    }

    Tk_SizeOfImage(image, &imgWidth, &imgHeight);
    src = Ttk_MakeBox(0, 0, imgWidth, imgHeight);
    dst = Ttk_StickBox(b, imgWidth, imgHeight, imageData->sticky);

#ifndef TK_NO_DOUBLE_BUFFERING
    /*
     * Cached compose-and-blend path.  Compose the tiled element once into an
     * off-screen RGBA photo (keyed by width, height and state -- not by
     * position, so a moving element such as a scrollbar thumb still hits),
     * then draw it with a single Tk_RedrawImage.  Because no background is
     * baked in, Tk_RedrawImage re-composites it against the live destination
     * every frame: opaque elements take the cheap copy path internally and
     * translucent elements blend against current pixels, so the cache is
     * never stale.  Compiled out on macOS (uses the direct draw below).
     */
    if (imageName != NULL && dst.width > 0 && dst.height > 0) {
	Tcl_Interp *interp = Tk_Interp(tkwin);
	Ttk_Padding p = imageData->border;
	Tk_PhotoHandle srcPhoto = (interp != NULL)
		? Tk_FindPhoto(interp, Tcl_GetString(imageName)) : NULL;

	if (srcPhoto != NULL
		&& dst.width  >= p.left + p.right
		&& dst.height >= p.top + p.bottom) {
	    ElementImageCache *cache = GetImageCache(imageData);

	    if (!cache->cachedValid
		    || cache->cachedWidth  != dst.width
		    || cache->cachedHeight != dst.height
		    || cache->cachedState  != state) {
		cache->cachedValid = 0;
		cache->cachedOpaque = SourceIsOpaque(srcPhoto);
		if (cache->cachedOpaque) {
		    if (BuildOpaquePixmap(cache, tkwin, image, src,
			    dst.width, dst.height, p)) {
			cache->cachedValid = 1;
		    }
		} else if (EnsureComposedPhoto(cache, interp)
			&& BuildComposedPhoto(cache, interp, srcPhoto, src,
				dst.width, dst.height, p)) {
		    cache->cachedValid = 1;
		}
		if (cache->cachedValid) {
		    cache->cachedWidth  = dst.width;
		    cache->cachedHeight = dst.height;
		    cache->cachedState  = state;
		}
	    }

	    if (cache->cachedValid && cache->cachedOpaque) {
		XGCValues gcValues;
		GC gc;

		gcValues.function = GXcopy;
		gcValues.graphics_exposures = False;
		gc = Tk_GetGC(tkwin, GCFunction|GCGraphicsExposures, &gcValues);
		XCopyArea(Tk_Display(tkwin), cache->pixmap, d, gc,
			0, 0, (unsigned) dst.width, (unsigned) dst.height,
			dst.x, dst.y);
		Tk_FreeGC(Tk_Display(tkwin), gc);
		return;
	    }
	    if (cache->cachedValid
		    && EnsureComposedImageInstance(cache, interp, tkwin)) {
		Tk_RedrawImage(cache->composedImage, 0, 0,
			dst.width, dst.height, d, dst.x, dst.y);
		return;
	    }
	}
    }
#endif /* !TK_NO_DOUBLE_BUFFERING */

    /* Direct draw: cache unavailable or non-photo source image. */
    Ttk_Tile(tkwin, d, image, src, dst, imageData->border);
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

#ifndef TK_NO_DOUBLE_BUFFERING
    imageSpec = TtkGetImageSpecEx(interp, Tk_MainWindow(interp), objv[0],
	    ImageElementImageChanged, imageData);
#else
    imageSpec = TtkGetImageSpec(interp, Tk_MainWindow(interp), objv[0]);
#endif
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

    if (!Ttk_RegisterElement(interp, theme, elementName, &ImageElementSpec,
		imageData))
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

MODULE_SCOPE void
TtkImage_Init(Tcl_Interp *interp)
{
    Ttk_RegisterElementFactory(interp, "image", Ttk_CreateImageElement, NULL);
}

/*EOF*/
