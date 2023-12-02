/*
 *      Theme engine resource cache.
 *
 * Copyright (c) 2004, Joe English
 *
 * The problem:
 *
 * Tk maintains reference counts for fonts, colors, and images,
 * and deallocates them when the reference count goes to zero.
 * With the theme engine, resources are allocated right before
 * drawing an element and released immediately after.
 * This causes a severe performance penalty, and on PseudoColor
 * visuals it causes colormap cycling as colormap entries are
 * released and reused.
 *
 * Solution: Acquire fonts, colors, and objects from a
 * resource cache instead of directly from Tk; the cache
 * holds a semipermanent reference to the resource to keep
 * it from being deallocated.
 *
 * The plumbing and control flow here is quite contorted;
 * it would be better to address this problem in the core instead.
 *
 * @@@ Colormap flashing on PseudoColor visuals is still possible,
 * but this will be a transient effect.
 */

#include "tkInt.h"
#include "ttkTheme.h"

#if defined(_WIN32) || defined(MAC_OSX_TK)
#   define NEED_EXTRA_INFO 0
#else
 /*
  * Display, Screen, Visual, and Colormap need be tracked, too.
  * Required on X11 with multiple display connections or
  * special visuals/colormaps.
  */
#   define NEED_EXTRA_INFO 1
#endif

#if NEED_EXTRA_INFO
typedef struct {
    Tcl_Obj *objPtr;		/* The cached Tcl_Obj*. */
    Display *display;		/* Display of (Font|Border|Color)Obj */
    int screenNum;		/* Screen number of (Font|Border|Color)Obj */
    Visual *visual;		/* Visual of (Font|Border|Color)Obj */
    Colormap colormap;		/* Colormap of (Font|Border|Color)Obj */
} Ttk_Cached;
#endif

#ifdef _WIN32
#include "tkWinInt.h"
#endif

struct Ttk_ResourceCache_ {
    Tcl_Interp	  *interp;	/* Interpreter for error reporting */
    Tk_Window	  tkwin;	/* Cache window. */
    Tcl_HashTable fontTable;	/* Entries: Tcl_Obj* holding FontObjs */
    Tcl_HashTable borderTable;	/* Entries: Tcl_Obj* holding BorderObjs */
    Tcl_HashTable colorTable;	/* Entries: Tcl_Obj* holding ColorObjs */
    Tcl_HashTable imageTable;	/* Entries: Tk_Images */

    Tcl_HashTable namedColors;	/* Entries: RGB values as Tcl_StringObjs */
};

/*
 * Ttk_CreateResourceCache --
 * 	Initialize a new resource cache.
 */
Ttk_ResourceCache Ttk_CreateResourceCache(Tcl_Interp *interp)
{
    Ttk_ResourceCache cache = ckalloc(sizeof(*cache));

    cache->tkwin = NULL;	/* initialized later */
    cache->interp = interp;
    Tcl_InitHashTable(&cache->fontTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&cache->borderTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&cache->colorTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&cache->imageTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&cache->namedColors, TCL_STRING_KEYS);

    return cache;
}

/*
 * Ttk_ClearCache --
 * 	Release references to all cached resources.
 */
static void Ttk_ClearCache(Ttk_ResourceCache cache)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *entryPtr;

    /*
     * Free fonts:
     */
    entryPtr = Tcl_FirstHashEntry(&cache->fontTable, &search);
    while (entryPtr != NULL) {
#if !NEED_EXTRA_INFO
	Tcl_Obj *fontObj = Tcl_GetHashValue(entryPtr);
	if (fontObj) {
	    Tk_FreeFontFromObj(cache->tkwin, fontObj);
	    Tcl_DecrRefCount(fontObj);
	}
#else
	Ttk_Cached *cachedPtr = Tcl_GetHashValue(entryPtr);
	if (cachedPtr) {
	    TkWindow fakeWin;
	    Tcl_Obj *fontObj = cachedPtr->objPtr;
	    fakeWin = *((TkWindow *) cache->tkwin);
	    fakeWin.display = cachedPtr->display;
	    fakeWin.screenNum = cachedPtr->screenNum;
	    fakeWin.visual = cachedPtr->visual;
	    fakeWin.atts.colormap = cachedPtr->colormap;
	    Tk_FreeFontFromObj((Tk_Window) &fakeWin, fontObj);
	    Tcl_DecrRefCount(fontObj);
	    ckfree(cachedPtr);
	}
#endif
	entryPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&cache->fontTable);
    Tcl_InitHashTable(&cache->fontTable, TCL_STRING_KEYS);

    /*
     * Free borders:
     */
    entryPtr = Tcl_FirstHashEntry(&cache->borderTable, &search);
    while (entryPtr != NULL) {
#if !NEED_EXTRA_INFO
	Tcl_Obj *borderObj = Tcl_GetHashValue(entryPtr);
	if (borderObj) {
	    Tk_Free3DBorderFromObj(cache->tkwin, borderObj);
	    Tcl_DecrRefCount(borderObj);
	}
#else
	Ttk_Cached *cachedPtr = Tcl_GetHashValue(entryPtr);
	if (cachedPtr) {
	    TkWindow fakeWin;
	    Tcl_Obj *borderObj = cachedPtr->objPtr;
	    fakeWin = *((TkWindow *) cache->tkwin);
	    fakeWin.display = cachedPtr->display;
	    fakeWin.screenNum = cachedPtr->screenNum;
	    fakeWin.visual = cachedPtr->visual;
	    fakeWin.atts.colormap = cachedPtr->colormap;
	    Tk_Free3DBorderFromObj((Tk_Window) &fakeWin, borderObj);
	    Tcl_DecrRefCount(borderObj);
	    ckfree(cachedPtr);
	}
#endif
	entryPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&cache->borderTable);
    Tcl_InitHashTable(&cache->borderTable, TCL_STRING_KEYS);

    /*
     * Free colors:
     */
    entryPtr = Tcl_FirstHashEntry(&cache->colorTable, &search);
    while (entryPtr != NULL) {
#if !NEED_EXTRA_INFO
	Tcl_Obj *colorObj = Tcl_GetHashValue(entryPtr);
	if (colorObj) {
	    Tk_FreeColorFromObj(cache->tkwin, colorObj);
	    Tcl_DecrRefCount(colorObj);
	}
#else
	Ttk_Cached *cachedPtr = Tcl_GetHashValue(entryPtr);
	if (cachedPtr) {
	    TkWindow fakeWin;
	    Tcl_Obj *colorObj = cachedPtr->objPtr;
	    fakeWin = *((TkWindow *) cache->tkwin);
	    fakeWin.display = cachedPtr->display;
	    fakeWin.screenNum = cachedPtr->screenNum;
	    fakeWin.visual = cachedPtr->visual;
	    fakeWin.atts.colormap = cachedPtr->colormap;
	    Tk_FreeColorFromObj((Tk_Window) &fakeWin, colorObj);
	    Tcl_DecrRefCount(colorObj);
	    ckfree(cachedPtr);
	}
#endif
	entryPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&cache->colorTable);
    Tcl_InitHashTable(&cache->colorTable, TCL_STRING_KEYS);

    /*
     * Free images:
     */
    entryPtr = Tcl_FirstHashEntry(&cache->imageTable, &search);
    while (entryPtr != NULL) {
	Tk_Image image = Tcl_GetHashValue(entryPtr);
	if (image) {
	    Tk_FreeImage(image);
	}
	entryPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&cache->imageTable);
    Tcl_InitHashTable(&cache->imageTable, TCL_STRING_KEYS);

    return;
}

/*
 * Ttk_FreeResourceCache --
 * 	Release references to all cached resources, delete the cache.
 */

void Ttk_FreeResourceCache(Ttk_ResourceCache cache)
{
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch search;

    Ttk_ClearCache(cache);

    Tcl_DeleteHashTable(&cache->colorTable);
    Tcl_DeleteHashTable(&cache->fontTable);
    Tcl_DeleteHashTable(&cache->imageTable);

    /*
     * Free named colors:
     */
    entryPtr = Tcl_FirstHashEntry(&cache->namedColors, &search);
    while (entryPtr != NULL) {
	Tcl_Obj *colorNameObj = Tcl_GetHashValue(entryPtr);
	Tcl_DecrRefCount(colorNameObj);
	entryPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&cache->namedColors);

    ckfree(cache);
}

/*
 * CacheWinEventHandler --
 * 	Detect when the cache window is destroyed, clear cache.
 */
static void CacheWinEventHandler(ClientData clientData, XEvent *eventPtr)
{
    Ttk_ResourceCache cache = clientData;

    if (eventPtr->type != DestroyNotify) {
	return;
    }
    Tk_DeleteEventHandler(cache->tkwin, StructureNotifyMask,
	    CacheWinEventHandler, clientData);
    Ttk_ClearCache(cache);
    cache->tkwin = NULL;
}

/*
 * InitCacheWindow --
 * 	Specify the cache window if not already set.
 */
static void InitCacheWindow(Ttk_ResourceCache cache, Tk_Window tkwin)
{
    if (cache->tkwin == NULL) {
	cache->tkwin = Tk_MainWindow(cache->interp);
	Tk_CreateEventHandler(cache->tkwin, StructureNotifyMask,
		CacheWinEventHandler, cache);
    }
}

/*
 * Ttk_RegisterNamedColor --
 *	Specify an RGB triplet as a named color.
 *	Overrides any previous named color specification.
 */
void Ttk_RegisterNamedColor(
    Ttk_ResourceCache cache,
    const char *colorName,
    XColor *colorPtr)
{
    int newEntry;
    Tcl_HashEntry *entryPtr;
    char nameBuf[14];
    Tcl_Obj *colorNameObj;

    snprintf(nameBuf, sizeof(nameBuf), "#%04X%04X%04X",
    	colorPtr->red, colorPtr->green, colorPtr->blue);
    colorNameObj = Tcl_NewStringObj(nameBuf, -1);
    Tcl_IncrRefCount(colorNameObj);

    entryPtr = Tcl_CreateHashEntry(&cache->namedColors, colorName, &newEntry);
    if (!newEntry) {
    	Tcl_Obj *oldColor = Tcl_GetHashValue(entryPtr);
	Tcl_DecrRefCount(oldColor);
    }

    Tcl_SetHashValue(entryPtr, colorNameObj);
}

/*
 * CheckNamedColor(objPtr) --
 *	If objPtr is a registered color name, return a Tcl_Obj *
 *	containing the registered color value specification.
 *	Otherwise, return the input argument.
 */
static Tcl_Obj *CheckNamedColor(Ttk_ResourceCache cache, Tcl_Obj *objPtr)
{
    Tcl_HashEntry *entryPtr =
    	Tcl_FindHashEntry(&cache->namedColors, Tcl_GetString(objPtr));
    if (entryPtr) {	/* Use named color instead */
    	objPtr = Tcl_GetHashValue(entryPtr);
    }
    return objPtr;
}

/*
 * Template for allocation routines:
 */
typedef void *(*Allocator)(Tcl_Interp *, Tk_Window, Tcl_Obj *);

static void *AllocFont(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr) {
	return Tk_AllocFontFromObj(interp, tkwin, objPtr);
}
static void *AllocColor(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr) {
	return Tk_AllocColorFromObj(interp, tkwin, objPtr);
}
static void *AllocBorder(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr) {
	return Tk_Alloc3DBorderFromObj(interp, tkwin, objPtr);
}

static Tcl_Obj *Ttk_Use(
    Tcl_Interp *interp,
    Tcl_HashTable *table,
    Allocator allocate,
    Tk_Window tkwin,
    Tcl_Obj *objPtr)
{
    int newEntry;
    Tcl_HashEntry *entryPtr;
    Tcl_Obj *cacheObj;
#if !NEED_EXTRA_INFO
    entryPtr = Tcl_CreateHashEntry(table, Tcl_GetString(objPtr), &newEntry);
#else
    Tcl_DString ds;
    char buffer[64];

    Tcl_DStringInit(&ds);
    Tcl_DStringAppend(&ds, Tcl_GetString(objPtr), -1);
    snprintf(buffer, 64, ",%d,%lu,%lu", ConnectionNumber(Tk_Display(tkwin)),
	    Tk_Visual(tkwin)->visualid, (unsigned long)Tk_Colormap(tkwin));
    Tcl_DStringAppend(&ds, buffer, -1);
    entryPtr = Tcl_CreateHashEntry(table, Tcl_DStringValue(&ds), &newEntry);
    Tcl_DStringFree(&ds);
#endif

    if (!newEntry) {
#if !NEED_EXTRA_INFO
	return Tcl_GetHashValue(entryPtr);
#else
	Ttk_Cached *cachedPtr = Tcl_GetHashValue(entryPtr);
	return cachedPtr ? cachedPtr->objPtr : NULL;
#endif
    }

    cacheObj = Tcl_DuplicateObj(objPtr);
    Tcl_IncrRefCount(cacheObj);

    if (allocate(interp, tkwin, cacheObj)) {
#if !NEED_EXTRA_INFO
	Tcl_SetHashValue(entryPtr, cacheObj);
#else
	Ttk_Cached *cachedPtr = ckalloc(sizeof(*cachedPtr));
	cachedPtr->objPtr = cacheObj;
	cachedPtr->display = Tk_Display(tkwin);
	cachedPtr->screenNum = Tk_ScreenNumber(tkwin);
	cachedPtr->visual = Tk_Visual(tkwin);
	cachedPtr->colormap = Tk_Colormap(tkwin);
	Tcl_SetHashValue(entryPtr, cachedPtr);
#endif
	return cacheObj;
    } else {
	Tcl_DecrRefCount(cacheObj);
	Tcl_SetHashValue(entryPtr, NULL);
	Tcl_BackgroundException(interp, TCL_ERROR);
	return NULL;
    }
}

/*
 * Ttk_UseFont --
 * 	Acquire a font from the cache.
 */
Tcl_Obj *Ttk_UseFont(Ttk_ResourceCache cache, Tk_Window tkwin, Tcl_Obj *objPtr)
{
    InitCacheWindow(cache, tkwin);
    return Ttk_Use(cache->interp,
	&cache->fontTable, AllocFont, tkwin, objPtr);
}

/*
 * Ttk_UseColor --
 * 	Acquire a color from the cache.
 */
Tcl_Obj *Ttk_UseColor(Ttk_ResourceCache cache, Tk_Window tkwin, Tcl_Obj *objPtr)
{
    objPtr = CheckNamedColor(cache, objPtr);
    InitCacheWindow(cache, tkwin);
    return Ttk_Use(cache->interp,
	&cache->colorTable, AllocColor, tkwin, objPtr);
}

/*
 * Ttk_UseBorder --
 * 	Acquire a Tk_3DBorder from the cache.
 */
Tcl_Obj *Ttk_UseBorder(
    Ttk_ResourceCache cache, Tk_Window tkwin, Tcl_Obj *objPtr)
{
    objPtr = CheckNamedColor(cache, objPtr);
    InitCacheWindow(cache, tkwin);
    return Ttk_Use(cache->interp,
	&cache->borderTable, AllocBorder, tkwin, objPtr);
}

/* NullImageChanged --
 * 	Tk_ImageChangedProc for Ttk_UseImage
 */

static void NullImageChanged(ClientData clientData,
    int x, int y, int width, int height, int imageWidth, int imageHeight)
{ /* No-op */ }

/*
 * Ttk_UseImage --
 * 	Acquire a Tk_Image from the cache.
 */
Tk_Image Ttk_UseImage(Ttk_ResourceCache cache, Tk_Window tkwin, Tcl_Obj *objPtr)
{
    const char *imageName = Tcl_GetString(objPtr);
    int newEntry;
    Tcl_HashEntry *entryPtr;
    Tk_Image image;

    InitCacheWindow(cache, tkwin);
#if !NEED_EXTRA_INFO
    entryPtr = Tcl_CreateHashEntry(&cache->imageTable, imageName, &newEntry);
#else
    Tcl_DString ds;
    char buffer[64];

    Tcl_DStringInit(&ds);
    Tcl_DStringAppend(&ds, imageName, -1);
    snprintf(buffer, 64, ",%d,%lu,%lu", ConnectionNumber(Tk_Display(tkwin)),
	Tk_Visual(tkwin)->visualid, (unsigned long)Tk_Colormap(tkwin));
    Tcl_DStringAppend(&ds, buffer, -1);
    entryPtr = Tcl_CreateHashEntry(&cache->imageTable,
	Tcl_DStringValue(&ds), &newEntry);
    Tcl_DStringFree(&ds);
#endif

    if (!newEntry) {
	return Tcl_GetHashValue(entryPtr);
    }

    image = Tk_GetImage(cache->interp, tkwin, imageName, NullImageChanged,0);
    Tcl_SetHashValue(entryPtr, image);

    if (!image) {
	Tcl_BackgroundException(cache->interp, TCL_ERROR);
    }

    return image;
}

/*EOF*/
