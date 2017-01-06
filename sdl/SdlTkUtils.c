#include "tkInt.h"
#include "SdlTkInt.h"
#include "tkFont.h"
#ifdef _WIN32
#include <windows.h>
#endif

#undef TRACE_FONTS

#ifdef TRACE_FONTS
#define FNTLOG(...) SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,__VA_ARGS__)
#else
#define FNTLOG(...)
#endif

#define XFT_XLFD		"xlfd"

#include <ft2build.h>
#include FT_FREETYPE_H

typedef struct FileFaceKey {
    Atom file;
    int index;
} FileFaceKey;

typedef struct FileFaceSizeKey {
    Atom file;
    int index;
    int size;
} FileFaceSizeKey;

static Tcl_HashTable xlfdHash;
static Tcl_HashTable fileFaceHash;
static Tcl_HashTable fileFaceSizeHash;

static const char *cursorFontName = "cursor";

TCL_DECLARE_MUTEX(fontMutex);

/*
 * The next 2 functions implement a pool of Regions to minimize
 * allocating/freeing Regions since they are used a lot. Any Region
 * can be passed to RgnPoolFree() whether it was allocated by
 * RgnPoolGet() or not. The big Xlib lock must be held when
 * these functions are called.
 */

typedef struct RgnEntry {
    Region r;
    struct RgnEntry *next;
} RgnEntry;

static int rgnCounts[2] = { 0, 0 }; /* Statistic counters */
static RgnEntry *rgnPool = NULL; /* Linked list of available Regions */
static RgnEntry *rgnFree = NULL; /* Linked list of free RgnEntrys */

Region
SdlTkRgnPoolGet(void)
{
    Region r;

    if (rgnPool != NULL) {
	RgnEntry *entry = rgnPool;

	r = entry->r;
	rgnPool = entry->next;

	entry->r = NULL;
	entry->next = rgnFree;
	rgnFree = entry;

	/*
	 * Empty the region. The good thing about Regions is
	 * they don't deallocate memory when getting smaller.
	 */
	XSetEmptyRegion(r);
	rgnCounts[0]--;
	return r;
    }

    return XCreateRegion();
}

void
SdlTkRgnPoolFree(Region r)
{
    RgnEntry *entry;

    if (r == NULL) {
	Tcl_Panic("called RgnPoolFree with a NULL Region");
    }
    if (rgnFree != NULL) {
	entry = rgnFree;
	rgnFree = entry->next;
    } else {
	entry = (RgnEntry *) ckalloc(sizeof (RgnEntry));
	rgnCounts[1]++;
    }
    entry->r = r;
    entry->next = rgnPool;
    rgnPool = entry;
    rgnCounts[0]++;
}

int *
SdlTkRgnPoolStat(void)
{
    return rgnCounts;
}

/*
 * Convert one XLFD numeric field.  Return -1 if the field is '*'.
 */

static const char *
XftGetInt(const char *ptr, int *val)
{
    if (*ptr == '*') {
	*val = -1;
	ptr++;
    } else {
	*val = '\0';
	while ((*ptr >= '0') && (*ptr <= '9')) {
	    *val = *val * 10 + *ptr++ - '0';
	}
    }
    if (*ptr == '-') {
	return ptr;
    }
    return (char *) 0;
}

char **
SdlTkListFonts(const char *name, int *count)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    int i, nmatch = 0;
    Tcl_DString ds;
    char **names = NULL;

    FNTLOG("FONTLIST: pattern '%s'", name);
    Tcl_DStringInit(&ds);
    Tcl_MutexLock(&fontMutex);
    hPtr = Tcl_FirstHashEntry(&fileFaceHash, &search);
    while (hPtr != NULL) {
	GlyphIndexHash *ghash;

	ghash = (GlyphIndexHash *) Tcl_GetHashValue(hPtr);
	if ((name[0] == '*') || (strcasecmp(ghash->familyName, name) == 0)) {
	    Tcl_DStringAppend(&ds, "-unknown-", -1);
	    Tcl_DStringAppend(&ds, ghash->familyName, -1);
	    if (ghash->styleFlags & FT_STYLE_FLAG_BOLD) {
	        Tcl_DStringAppend(&ds, "-bold", -1);
	    } else {
	        Tcl_DStringAppend(&ds, "-normal", -1);
	    }
	    if (ghash->styleFlags & FT_STYLE_FLAG_ITALIC) {
	        Tcl_DStringAppend(&ds, "-o", -1);
	    } else {
	        Tcl_DStringAppend(&ds, "-r", -1);
	    }
	    Tcl_DStringAppend(&ds, "-normal-*-0-*-*-*-*-*-ucs-4", -1);
	    Tcl_DStringAppend(&ds, "\0", 1);
	    nmatch++;
	}
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_MutexUnlock(&fontMutex);
    *count = 0;
    /* fallback for "fixed" */
    if ((nmatch == 0) && (strcmp(name, "fixed") == 0)) {
	const char *fbfont =
	    "-unknown-dejavu sans mono-bold-r-normal-*-14-*-*-*-*-*-ucs-4";

	Tcl_DStringAppend(&ds, fbfont, -1);
	Tcl_DStringAppend(&ds, "\0", 1);
	nmatch++;
    }
    if (nmatch > 0) {
	char *p = Tcl_DStringValue(&ds);

	names = (char **) ckalloc(sizeof (char *) * (nmatch + 1));
	for (i = 0; i < nmatch; i++) {
	    int len = strlen(p) + 1;

	    names[i] = ckalloc(len);
	    strcpy(names[i], p);
	    FNTLOG("FONTLIST:   '%s'", p);
	    p += len;
	}
	names[i] = 0;
	*count = nmatch;
    }
    Tcl_DStringFree(&ds);
    return names;
}

unsigned
SdlTkGetNthGlyphIndex(_Font *_f, const char *s, int n)
{
    return ((unsigned int *) s)[n];
}

static int
MatchFont(const char *xlfd, _Font *_f)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    GlyphIndexHash *ghash = NULL;
    FileFaceKey *ffKey = NULL;
    const char *p, *family, *weight, *slant;
    char buffer[128];
    int size;
    Tcl_DString ds;

    p = xlfd;
    if (p[0] != '-') {
	goto error;
    }
    if (!(p = strchr(++p, '-'))) {
	goto error;
    }
    if (!(p = strchr(family = ++p, '-'))) {
	goto error;
    }
    if (!(p = strchr(weight = ++p, '-'))) {
	goto error;
    }
    if (!(p = strchr(slant = ++p, '-'))) {
	goto error;
    }
    if (!(p = strchr(++p, '-'))) {
	goto error;
    }
    if (!(p = strchr (++p, '-'))) {
	goto error;
    }
    if (!(p = XftGetInt (++p, &size)) || (size < 0)) {
	goto error;
    }
    hPtr = Tcl_FirstHashEntry(&fileFaceHash, &search);
    while (hPtr != NULL) {
	ghash = (GlyphIndexHash *) Tcl_GetHashValue(hPtr);
	if (Tcl_StringCaseMatch(xlfd, ghash->xlfdPattern, 1)) {
	    ffKey = (FileFaceKey *) Tcl_GetHashKey(&fileFaceHash, hPtr);
	    break;
	}
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DStringInit(&ds);
    if (ffKey == NULL) {
	const char *const *aliases = NULL;
	char *q;

	q = ckalloc(strlen(family) + 1);
	strcpy(q, family);
	family = q;
	q = strchr(family, '-');
	if (q != NULL) {
	    *q = '\0';
	}
	aliases = TkFontGetAliasList(family);
	if (aliases != NULL) {
	    int i;

	    for (i = 0; aliases[i] != NULL; i++) {
		Tcl_DStringSetLength(&ds, 0);
		Tcl_DStringAppend(&ds, "-unknown-", -1);
		Tcl_DStringAppend(&ds, aliases[i], -1);
		if ((weight[0] == 'b') || (weight[0] == 'B')) {
		    Tcl_DStringAppend(&ds, "-bold", -1);
		} else {
		    Tcl_DStringAppend(&ds, "-normal", -1);
		}
		if ((slant[0] == 'i') || (slant[0] == 'I') ||
		    (slant[0] == 'o') || (slant[0] == 'O')) {
		    Tcl_DStringAppend(&ds, "-o", -1);
		} else {
		    Tcl_DStringAppend(&ds, "-r", -1);
		}
		Tcl_DStringAppend(&ds, "-normal-*-*-*-*-*-*-*-ucs-4", -1);
		hPtr = Tcl_FirstHashEntry(&fileFaceHash, &search);
		while (hPtr != NULL) {
		    ghash = (GlyphIndexHash *) Tcl_GetHashValue(hPtr);
		    if (Tcl_StringCaseMatch(Tcl_DStringValue(&ds),
					    ghash->xlfdPattern, 1)) {
		        ffKey = (FileFaceKey *)
			    Tcl_GetHashKey(&fileFaceHash, hPtr);
			break;
		    }
		    hPtr = Tcl_NextHashEntry(&search);
		}
		if (ffKey != NULL) {
		    break;
		}
	    }
	}
	ckfree((char *) family);
    }
    if (ffKey == NULL) {
	goto error;
    }
    _f->refCnt = 1;
    _f->file = (char *) ffKey->file;
    _f->file_size = 0;
    _f->index = ffKey->index;
    _f->size = size;
    _f->fixedWidth = (ghash->faceFlags & FT_FACE_FLAG_FIXED_WIDTH) ? 1 : 0;

    Tcl_DStringSetLength(&ds, 0);
    Tcl_DStringAppend(&ds, "-unknown-", -1);
    Tcl_DStringAppend(&ds, ghash->familyName, -1);
    if (ghash->styleFlags & FT_STYLE_FLAG_BOLD) {
	Tcl_DStringAppend(&ds, "-bold", -1);
    } else {
	Tcl_DStringAppend(&ds, "-normal", -1);
    }
    if (ghash->styleFlags & FT_STYLE_FLAG_ITALIC) {
	Tcl_DStringAppend(&ds, "-o", -1);
    } else {
	Tcl_DStringAppend(&ds, "-r", -1);
    }
    sprintf(buffer, "-normal-*-%d-*-*-*-*-*-ucs-4", _f->size);
    Tcl_DStringAppend(&ds, buffer, -1);
    _f->xlfd = ckalloc(Tcl_DStringLength(&ds) + 1);
    strcpy((char *) _f->xlfd, Tcl_DStringValue(&ds));
    Tcl_DStringFree(&ds);
    FNTLOG("FONTMATCH: xlfd(in) '%s', xlfd(out) '%s'", xlfd, _f->xlfd);
    FNTLOG("FONTMATCH: size %d, file '%s'", _f->size, _f->file);
    return TCL_OK;
error:
    FNTLOG("FONTMATCH: xlfd(in) '%s', ERROR", xlfd);
    return TCL_ERROR;
}

static void
SdlTkLoadGlyphHash(GlyphIndexHash *ghash, FileFaceKey *ffKey, int fsize)
{
    FT_Library ftlib;
    FT_Open_Args ftarg;
    FT_Face face;
    FT_Error fterr;
    FT_ULong charcode;
    FT_UInt gindex;
    Tcl_HashEntry *hPtr;
    int isNew;

    fterr = FT_Init_FreeType(&ftlib);
    if (fterr != 0) {
	Tcl_Panic("init of freetype failed");
    }
    memset(&ftarg, 0, sizeof (ftarg));
    ftarg.flags = FT_OPEN_STREAM;
    ftarg.stream =
	(FT_Stream) SdlTkXGetFTStream((const char *) ffKey->file, fsize);
    fterr = FT_Open_Face(ftlib, &ftarg, ffKey->index, &face);
    if (fterr != 0) {
	Tcl_Panic("loading freetype font failed");
    }
    charcode = FT_Get_First_Char(face, &gindex);
    while (gindex != 0) {
	hPtr = Tcl_CreateHashEntry(&ghash->hash,
				   (char *) charcode, &isNew);
	if (isNew) {
	    Tcl_SetHashValue(hPtr, (char *) charcode);
	}
	charcode = FT_Get_Next_Char(face, charcode, &gindex);
    }
    FT_Done_Face(face);
    FT_Done_FreeType(ftlib);
    ghash->hashLoaded = 1;
}

Font
SdlTkFontLoadXLFD(const char *xlfd)
{
    Tcl_HashEntry *hPtr;
    _Font fstorage, *_f;
    FileFaceKey ffKey;
    FileFaceSizeKey ffsKey;
    int isNew;
    struct stat stbuf;

    /* TkGetCursorByName() */
    if (!strcmp(xlfd, cursorFontName)) {
	_f = (_Font *) ckalloc (sizeof (_Font));
	memset(_f, 0, sizeof (_Font));
	_f->file = cursorFontName;
	_f->refCnt = 1;
	FNTLOG("FONTLOAD: '%s' -> '%s'", xlfd, cursorFontName);
	return (Font) _f;
    }

    Tcl_MutexLock(&fontMutex);

    /* See if this exact XLFD has already been loaded */
    hPtr = Tcl_FindHashEntry(&xlfdHash, xlfd);
    if (hPtr != NULL) {
	_f = (_Font *) Tcl_GetHashValue(hPtr);
	_f->refCnt++;
	_f->glyphIndexHash->refCnt++;
	Tcl_MutexUnlock(&fontMutex);
	FNTLOG("FONTLOAD: '%s' -> '%s'", xlfd, _f->file);
	return (Font) _f;
    }

    /* Look in the file_face_cache */
    if (MatchFont(xlfd, &fstorage) != TCL_OK) {
	Tcl_MutexUnlock(&fontMutex);
	FNTLOG("FONTLOAD: '%s' -> None", xlfd);
	return None;
    }

    /*
     * See if the file && face && size were already loaded. If so, it means
     * we were given an XLFD refering to a font already loaded, and we hadn't
     * seen the XLFD before (in practice this doesn't seem to happen).
     */
    memset(&ffsKey, 0, sizeof (ffsKey));
    ffsKey.file = XInternAtom(SdlTkX.display, fstorage.file, False);
    ffsKey.index = fstorage.index;
    ffsKey.size = fstorage.size;
    hPtr = Tcl_CreateHashEntry(&fileFaceSizeHash, (char *) &ffsKey, &isNew);
    if (fstorage.file != (char *) ffsKey.file) {
	ckfree((char *) fstorage.file);
    }
    if (!isNew) {
	ckfree((char *) fstorage.xlfd);
	_f = (_Font *) Tcl_GetHashValue(hPtr);
	_f->refCnt++;
	_f->glyphIndexHash->refCnt++;
	hPtr = Tcl_CreateHashEntry(&xlfdHash, (char *) xlfd, &isNew);
	Tcl_SetHashValue(hPtr, (char *) _f);
	Tcl_MutexUnlock(&fontMutex);
	FNTLOG("FONTLOAD: '%s' -> '%s'", xlfd, _f->file);
	return (Font) _f;
    }

    _f = (_Font *) ckalloc(sizeof (_Font));
    _f->refCnt = 1;
    _f->file = (char *) ffsKey.file;
    if (Tcl_Stat(_f->file, &stbuf) == 0) {
	_f->file_size = stbuf.st_size;
    } else {
	_f->file_size = -1;
    }
    _f->index = fstorage.index;
    _f->size = fstorage.size;
    _f->fixedWidth = fstorage.fixedWidth;
    _f->xlfd = fstorage.xlfd;
    _f->fontStruct = SdlTkGfxAllocFontStruct(_f);

    /* fileFaceSizeHash */
    Tcl_SetHashValue(hPtr, (char *) _f);

    /* Reuse existing GlyphIndexHash for this file && face */
    memset(&ffKey, 0, sizeof (ffKey));
    ffKey.file = XInternAtom(SdlTkX.display, _f->file, False);
    ffKey.index = fstorage.index;
    hPtr = Tcl_CreateHashEntry(&fileFaceHash, (char *) &ffKey, &isNew);
    if (isNew) {
	Tcl_MutexUnlock(&fontMutex);
	Tcl_Panic("no GlyphIndexHash");
	return None;
    } else {
	GlyphIndexHash *ghash = (GlyphIndexHash *) Tcl_GetHashValue(hPtr);

	_f->glyphIndexHash = ghash;
	ghash->refCnt++;
	if (!ghash->hashLoaded) {
	    SdlTkLoadGlyphHash(ghash, &ffKey, _f->file_size);
	}
    }

    hPtr = Tcl_CreateHashEntry(&xlfdHash, (char *) xlfd, &isNew);
    Tcl_SetHashValue(hPtr, (char *) _f);
    Tcl_MutexUnlock(&fontMutex);
    FNTLOG("FONTLOAD: '%s' -> '%s'", xlfd, _f->file);
    return (Font) _f;
}

int
SdlTkFontIsFixedWidth(XFontStruct *fontStructPtr)
{
    _Font *_f = (_Font *) fontStructPtr->fid;

    return _f->fixedWidth;
}

int
SdlTkFontHasChar(XFontStruct *fontStructPtr, char *buf)
{
    _Font *_f = (_Font *) fontStructPtr->fid;
    Tcl_HashEntry *hPtr;
    unsigned int ucs4 = ((unsigned int *) buf)[0];
    unsigned long lucs4 = ucs4;
    int ret = 0;

    Tcl_MutexLock(&fontMutex);
    hPtr = Tcl_FindHashEntry(&_f->glyphIndexHash->hash, (char *) lucs4);
    if (hPtr != NULL) {
	ret = (Tcl_GetHashValue(hPtr) != 0);
    }
    Tcl_MutexUnlock(&fontMutex);
    return ret;
}

int
SdlTkFontCanDisplayChar(char *xlfd, TkFontAttributes *faPtr, int ch)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr, *hPtr2;
    unsigned long lch = ch;

    Tcl_MutexLock(&fontMutex);
    hPtr = Tcl_FirstHashEntry(&fileFaceHash, &search);
    while (hPtr != NULL) {
	GlyphIndexHash *ghash;

	ghash = (GlyphIndexHash *) Tcl_GetHashValue(hPtr);
	if (strcasecmp((char *) faPtr->family, ghash->familyName) == 0) {
	    if (faPtr->weight & TK_FW_BOLD) {
	        if (!(ghash->styleFlags & FT_STYLE_FLAG_BOLD)) {
		    goto next;
		}
	    } else if (ghash->styleFlags & FT_STYLE_FLAG_BOLD) {
	        goto next;
	    }
	    if (faPtr->slant & TK_FS_ITALIC) {
	        if (!(ghash->styleFlags & FT_STYLE_FLAG_ITALIC)) {
		    goto next;
		}
	    } else if (ghash->styleFlags & FT_STYLE_FLAG_BOLD) {
	        goto next;
	    }
	    if (!ghash->hashLoaded) {
	        FileFaceKey *ffKey;

		ffKey = (FileFaceKey *) Tcl_GetHashKey(&fileFaceHash, hPtr);
		SdlTkLoadGlyphHash(ghash, ffKey, 0);
	    }
	    hPtr2 = Tcl_FindHashEntry(&ghash->hash, (char *) lch);
	    Tcl_MutexUnlock(&fontMutex);
	    return (hPtr2 == NULL) ? 0 : 1;
	}
next:
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_MutexUnlock(&fontMutex);
    return (ch >= 0) && (ch < 256);
}

/*
 *-------------------------------------------------------------------------
 *
 * Ucs4ToUtfProc --
 *
 *	Convert from UCS-4 (system-endian 32-bit Unicode) to UTF-8.
 *
 * Results:
 *	Returns TCL_OK if conversion was successful.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

static int
Ucs4ToUtfProc(clientData, src, srcLen, flags, statePtr, dst, dstLen,
	      srcReadPtr, dstWrotePtr, dstCharsPtr)
    ClientData clientData;	/* Not used. */
    const char *src;		/* Source string in Unicode. */
    int srcLen;			/* Source string length in bytes. */
    int flags;			/* Conversion control flags. */
    Tcl_EncodingState *statePtr;/* Place for conversion routine to store
				 * state information used during a piecewise
				 * conversion.  Contents of statePtr are
				 * initialized and/or reset by conversion
				 * routine under control of flags argument. */
    char *dst;			/* Output buffer in which converted string
				 * is stored. */
    int dstLen;			/* The maximum length of output buffer in
				 * bytes. */
    int *srcReadPtr;		/* Filled with the number of bytes from the
				 * source string that were converted.  This
				 * may be less than the original source length
				 * if there was a problem converting some
				 * source characters. */
    int *dstWrotePtr;		/* Filled with the number of bytes that were
				 * stored in the output buffer as a result of
				 * the conversion. */
    int *dstCharsPtr;		/* Filled with the number of characters that
				 * correspond to the bytes stored in the
				 * output buffer. */
{
    const unsigned int *wSrc, *wSrcStart, *wSrcEnd;
    char *dstEnd, *dstStart;
    int result, numChars;

    result = TCL_OK;
    if ((srcLen % sizeof (unsigned int)) != 0) {
	result = TCL_CONVERT_MULTIBYTE;
	srcLen /= sizeof (unsigned int);
	srcLen *= sizeof (unsigned int);
    }

    wSrc = (unsigned int *) src;

    wSrcStart = (unsigned int *) src;
    wSrcEnd = (unsigned int *) (src + srcLen);

    dstStart = dst;
    dstEnd = dst + dstLen - TCL_UTF_MAX;

    for (numChars = 0; wSrc < wSrcEnd; numChars++) {
        Tcl_UniChar ch;

	if (dst > dstEnd) {
	    result = TCL_CONVERT_NOSPACE;
	    break;
	}
	ch = *wSrc++;
	dst += Tcl_UniCharToUtf(ch, dst);
    }

    *srcReadPtr = (char *) wSrc - (char *) wSrcStart;
    *dstWrotePtr = dst - dstStart;
    *dstCharsPtr = numChars;
    return result;
}

/*
 *-------------------------------------------------------------------------
 *
 * UtfToUcs4Proc --
 *
 *	Convert from UTF-8 to UCS-4.
 *
 * Results:
 *	Returns TCL_OK if conversion was successful.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

static int
UtfToUcs4Proc(clientData, src, srcLen, flags, statePtr, dst, dstLen,
	      srcReadPtr, dstWrotePtr, dstCharsPtr)
    ClientData clientData;	/* TableEncodingData that specifies encoding. */
    const char *src;		/* Source string in UTF-8. */
    int srcLen;			/* Source string length in bytes. */
    int flags;			/* Conversion control flags. */
    Tcl_EncodingState *statePtr;/* Place for conversion routine to store
				 * state information used during a piecewise
				 * conversion.  Contents of statePtr are
				 * initialized and/or reset by conversion
				 * routine under control of flags argument. */
    char *dst;			/* Output buffer in which converted string
				 * is stored. */
    int dstLen;			/* The maximum length of output buffer in
				 * bytes. */
    int *srcReadPtr;		/* Filled with the number of bytes from the
				 * source string that were converted.  This
				 * may be less than the original source length
				 * if there was a problem converting some
				 * source characters. */
    int *dstWrotePtr;		/* Filled with the number of bytes that were
				 * stored in the output buffer as a result of
				 * the conversion. */
    int *dstCharsPtr;		/* Filled with the number of characters that
				 * correspond to the bytes stored in the
				 * output buffer. */
{
    const char *srcStart, *srcEnd, *srcClose;
    unsigned int *wDst, *wDstStart, *wDstEnd;
    int result, numChars;

    srcStart = src;
    srcEnd = src + srcLen;
    srcClose = srcEnd;
    if ((flags & TCL_ENCODING_END) == 0) {
	srcClose -= TCL_UTF_MAX;
    }

    wDst = (unsigned int *) dst;
    wDstStart = (unsigned int *) dst;
    wDstEnd = (unsigned int *) (dst + dstLen - sizeof (unsigned int));

    result = TCL_OK;
    for (numChars = 0; src < srcEnd; numChars++) {
	Tcl_UniChar ucs2;

	if ((src > srcClose) && (!Tcl_UtfCharComplete(src, srcEnd - src))) {
	    /*
	     * If there is more string to follow, this will ensure that the
	     * last UTF-8 character in the source buffer hasn't been cut off.
	     */

	    result = TCL_CONVERT_MULTIBYTE;
	    break;
	}
	if (wDst > wDstEnd) {
	    result = TCL_CONVERT_NOSPACE;
	    break;
        }
	src += Tcl_UtfToUniChar(src, &ucs2);
#ifdef USE_SYMBOLA_CTRL
	if ((ucs2 >= 0x00) && (ucs2 < 0x20)) {
	    ucs2 += 0x2400;
	} else if (ucs2 == 0x7F) {
	    ucs2 = 0x2421;
	}
#endif
	*wDst++ = ucs2;
    }
    *srcReadPtr = src - srcStart;
    *dstWrotePtr = (char *) wDst - (char *) wDstStart;
    *dstCharsPtr = numChars;
    return result;
}

void
SdlTkFontFreeFont(XFontStruct *fontStructPtr)
{
    _Font *_f = (_Font *) fontStructPtr->fid;

    if (_f->file == cursorFontName) {
	ckfree(_f);
	return;
    }
    Tcl_MutexLock(&fontMutex);
    _f->glyphIndexHash->refCnt--;
    --_f->refCnt;
    Tcl_MutexUnlock(&fontMutex);
}

static unsigned long
SdlTkReadFTStream(FT_Stream ftstr, unsigned long offs, unsigned char *buf,
		  unsigned long count)
{
    unsigned long ret = 0;
    Tcl_Channel chan;

    if (ftstr->descriptor.pointer == NULL) {
	chan = Tcl_OpenFileChannel(NULL, (char *) ftstr->pathname.pointer,
				   "r", 0);
	if (chan) {
	    Tcl_SetChannelOption(NULL, chan, "-encoding", "binary");
	    Tcl_SetChannelOption(NULL, chan, "-translation", "binary");
	    ftstr->descriptor.pointer = (void *) chan;
	    FNTLOG("FONTSTREAM: open '%s'", (char *) ftstr->pathname.pointer);
	}
    } else {
	/*
	 * Since there's a singleton font manager/font cache in
	 * the AGG based drawing functions which is shared among
	 * all threads, the channel is spliced into the current
	 * thread here.
	 */
	Tcl_SpliceChannel((Tcl_Channel) ftstr->descriptor.pointer);
    }
    if ((ftstr->descriptor.pointer != NULL) && count) {
	Tcl_WideInt wOffs;
	int n;

	chan = (Tcl_Channel) ftstr->descriptor.pointer;
	wOffs = offs;
	wOffs = Tcl_Seek(chan, wOffs, SEEK_SET);
	if (wOffs == -1) {
	    FNTLOG("FONTSTREAM: seek '%s' @%ld failed",
		   (char *) ftstr->pathname.pointer, offs);
	    goto done;
	}
	n = Tcl_Read(chan, (char *) buf, count);
	if (n != -1) {
	    FNTLOG("FONTSTREAM: read '%s' @%ld size %d (%ld)",
		   (char *) ftstr->pathname.pointer, offs, n, count);
	    ret = n;
	} else {
	    FNTLOG("FONTSTREAM: read '%s' @%ld failed (%ld)",
		   (char *) ftstr->pathname.pointer, offs, count);
	}
    }
done:
    if (ftstr->descriptor.pointer != NULL) {
	/*
	 * Since there's a singleton font manager/font cache in
	 * the AGG based drawing functions which is shared among
	 * all threads, the channel is cut from the current
	 * thread here.
	 */
	Tcl_CutChannel((Tcl_Channel) ftstr->descriptor.pointer);
    }
    return ret;
}

static void
SdlTkCloseFTStream(FT_Stream ftstr)
{
    if (!ftstr) {
	return;
    }
    if (ftstr->descriptor.pointer != NULL) {
	/*
	 * Since there's a singleton font manager/font cache in
	 * the AGG based drawing functions which is shared among
	 * all threads, the channel is spliced into the current
	 * thread here before it gets closed.
	 */
	Tcl_SpliceChannel((Tcl_Channel) ftstr->descriptor.pointer);
	Tcl_Close(NULL, (Tcl_Channel) ftstr->descriptor.pointer);
	ftstr->descriptor.pointer = NULL;
	FNTLOG("FONTSTREAM: close '%s'",
	       (char *) ftstr->pathname.pointer);
    }
    FNTLOG("FONTSTREAM: release stream '%s'",
	   (char *) ftstr->pathname.pointer);
    ftstr->pathname.pointer = NULL;
    ckfree((char *) ftstr);
}

void *
SdlTkXGetFTStream(const char *pathname, int size)
{
    FT_Stream ftstr = (FT_Stream) ckalloc(sizeof (*ftstr));
    struct stat stbuf;

    FNTLOG("FONTSTREAM: get stream '%s' size %d", pathname, size);
    memset(ftstr, 0, sizeof (*ftstr));
    ftstr->pathname.pointer = (char *) pathname;
    ftstr->read = SdlTkReadFTStream;
    ftstr->close = SdlTkCloseFTStream;
    if (size <= 0) {
	if (Tcl_Stat(pathname, &stbuf) == 0) {
	    ftstr->size = stbuf.st_size;
	    FNTLOG("FONTSTREAM: file size %ld", ftstr->size);
	}
    } else {
	ftstr->size = size;
    }
    return (void *) ftstr;
}

int
SdlTkXGetFontFile(const char *family, int size, int isBold, int isItalic,
	     const char **nameRet, int *filesizeRet)
{
    Tcl_DString ds;
    char *p, *q;
    int ret;
    char *fname = NULL;
    _Font fstorage;

    Tcl_DStringInit(&ds);
    Tcl_DStringSetLength(&ds, strlen(family) + 128);
    p = Tcl_DStringValue(&ds);
    sprintf(p, "-unknown-%s", family);
    q = strrchr(p, '-');
    if (q - p > 10) {
	p = q;
    } else {
	p += strlen(p);
    }
    sprintf(p, "-%s-%s-normal-*-%d-*-*-*-*-*-ucs-4",
	    isBold ? "bold" : "normal", isItalic ? "o" : "r", size);
    p = Tcl_DStringValue(&ds);
    Tcl_MutexLock(&fontMutex);
    ret = MatchFont(p, &fstorage);
    Tcl_MutexUnlock(&fontMutex);
    Tcl_DStringFree(&ds);
    if (ret == TCL_OK) {
	fname = (char *) XInternAtom(SdlTkX.display, fstorage.file, False);
    }
    if (nameRet != NULL) {
	*nameRet = fname;
    }
    if ((filesizeRet != NULL) && (fname != NULL)) {
	struct stat stbuf;

	*filesizeRet = 0;

	if (Tcl_Stat(fname, &stbuf) == 0) {
	    *filesizeRet = stbuf.st_size;
	}
    }
    if ((fname != NULL) && (fstorage.file != fname)) {
	ckfree(fstorage.file);
    }
    if (fstorage.xlfd != NULL) {
	ckfree(fstorage.xlfd);
    }
    return ret == TCL_OK;
}

#ifdef _WIN32
static void
AddSysFontFiles(Tcl_Obj *list)
{
    HKEY top;
    DWORD index = 0;
    int fntlen;
    char *p, *q;
    WCHAR key[256];
    Tcl_DString fntdir, ds;

    /*
     * Find out fonts folder from Windows system directory.
     */
    GetSystemDirectoryW(key, sizeof (key) / sizeof (WCHAR));
    Tcl_WinTCharToUtf((TCHAR *) key, -1, &fntdir);
    p = Tcl_DStringValue(&fntdir);
    q = strrchr(p, '\\');
    if (q != NULL) {
	Tcl_DStringSetLength(&fntdir, q - p);
	Tcl_DStringAppend(&fntdir, "\\Fonts\\", -1);
    } else {
	Tcl_DStringSetLength(&fntdir, 0);
    }
    p = Tcl_DStringValue(&fntdir);
    while (*p != '\0') {
	if (*p == '\\') {
	    *p = '/';
	}
	++p;
    }
    fntlen = Tcl_DStringLength(&fntdir);

    /*
     * Search registry for fonts.
     */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
	    L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0,
	    KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &top) !=
	ERROR_SUCCESS) {
	return;
    }
    Tcl_DStringInit(&ds);
    Tcl_DStringSetLength(&ds, 512 * sizeof (WCHAR));
    while (1) {
	DWORD ret, size, type;

	size = sizeof (key) / sizeof (WCHAR);
	ret = RegEnumValueW(top, index, key, &size, NULL, NULL, NULL, NULL);
	if (ret != ERROR_SUCCESS) {
	    break;
	}
	Tcl_DStringSetLength(&ds, 0);
	size = 512 * sizeof (WCHAR);
	if ((RegQueryValueExW(top, key, NULL, &type,
		(BYTE *) Tcl_DStringValue(&ds), &size) == ERROR_SUCCESS) &&
	    (type == REG_SZ)) {
	    int isabs = 0;
	    Tcl_DString fname;

	    Tcl_WinTCharToUtf(Tcl_DStringValue(&ds), -1, &fname);
	    p = Tcl_DStringValue(&fname);
	    q = strrchr(p, '.');
	    if ((q != NULL) && (strcasecmp(q, ".ttf") == 0)) {
		q = p;
		while (*q != '\0') {
		    if (*q == '\\') {
			*q = '/';
			isabs++;
		    }
		    ++q;
		}
		if (!isabs) {
		    Tcl_DStringSetLength(&fntdir, fntlen);
		    Tcl_DStringAppend(&fntdir, p, -1);
		    Tcl_ListObjAppendElement(NULL, list,
			Tcl_NewStringObj(Tcl_DStringValue(&fntdir),
			    Tcl_DStringLength(&fntdir)));
		} else {
		    Tcl_ListObjAppendElement(NULL, list,
			Tcl_NewStringObj(p, -1));
		}
	    }
	    Tcl_DStringFree(&fname);
	}
	++index;
    }
    Tcl_DStringFree(&ds);
    RegCloseKey(top);
    Tcl_DStringFree(&fntdir);
}
#endif

int
SdlTkFontInit(Tcl_Interp *interp)
{
    static int initialized = 0;
    int result, objc, i, nfonts;
    Tcl_Obj **objv;
    Tcl_EncodingType type;
    FT_Error fterr;
    FT_Library ftlib;

    /* font manager/engine init */
    SdlTkGfxInitFC();

    if (initialized) {
	return TCL_OK;
    }
    Tcl_MutexLock(&fontMutex);
    if (initialized) {
	goto success;
    }

    Tcl_InitHashTable(&xlfdHash, TCL_STRING_KEYS);
    Tcl_InitHashTable(&fileFaceHash, sizeof (FileFaceKey) / sizeof (int));
    Tcl_InitHashTable(&fileFaceSizeHash,
		      sizeof (FileFaceSizeKey) / sizeof (int));

    fterr = FT_Init_FreeType(&ftlib);
    if (fterr != 0) {
	Tcl_AppendResult(interp, "error initializing freetype", (char *) NULL);
	goto error;
    }
    /*
     * Search order:
     *    1. packaged/built-in fonts ($tk_library/fonts/...)
     *    2. $env(HOME)/.fonts directory
     *    3. system specific directories
     * On desktop platforms 2. and 3. are skipped when
     * the -sdlnosysfonts command line option is present.
     */
#ifdef ANDROID
    result = Tcl_EvalEx(interp, "concat [glob -nocomplain -directory "
		"[file join $tk_library fonts] *] "
		"[glob -nocomplain -directory [file normalize ~/.fonts] "
		"-types f *.ttf] "
		"[glob -nocomplain -directory /system/fonts *.ttf"
		" -types f]",
		-1, TCL_EVAL_GLOBAL);
#else
    if (SdlTkX.arg_nosysfonts) {
	result = Tcl_EvalEx(interp, "glob -nocomplain -directory "
		    "[file join $tk_library fonts] *",
		    -1, TCL_EVAL_GLOBAL);
    } else {
#ifdef _WIN32
	result = Tcl_EvalEx(interp, "concat [glob -nocomplain -directory "
		    "[file join $tk_library fonts] *] "
		    "[glob -nocomplain -directory [file normalize ~/.fonts] "
		    "-types f *.ttf]",
		    -1, TCL_EVAL_GLOBAL);
#else
	/*
	 * Needs more effort, currently tries /usr/share/fonts/... which
	 * should work on most modern Linuxen.
	 */
	result = Tcl_EvalEx(interp, "concat [glob -nocomplain -directory "
		    "[file join $tk_library fonts] *] "
		    "[glob -nocomplain -directory [file normalize ~/.fonts] "
		    "-types f *.ttf] "
		    "[glob -nocomplain -directory /usr/share/fonts"
		    " -types f */*.ttf] "
		    "[glob -nocomplain -directory /usr/share/fonts/truetype"
		    " -types f */*.ttf]",
		    -1, TCL_EVAL_GLOBAL);
#endif
    }
#endif
    if (result != TCL_OK) {
fonterr:
	Tcl_AppendResult(interp, "\n    (while initializing fonts)",
			 (char *) NULL);
	goto error;
    }
#ifdef _WIN32
    if (!SdlTkX.arg_nosysfonts) {
	AddSysFontFiles(Tcl_GetObjResult(interp));
    }
#endif
    if (Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp), &objc, &objv)
	!= TCL_OK) {
	goto fonterr;
    }

    for (i = nfonts = 0; i < objc; i++) {
	FT_Face face = 0;
	FT_Open_Args ftarg;
	const char *fname = Tcl_GetString(objv[i]);
	int k, nfaces, fsize;

	memset(&ftarg, 0, sizeof (ftarg));
	ftarg.flags = FT_OPEN_STREAM;
	ftarg.stream = (FT_Stream) SdlTkXGetFTStream(fname, 0);
	fterr = FT_Open_Face(ftlib, &ftarg, -1, &face);
	if (fterr != 0) {
	    continue;
	}
	fsize = ((FT_Stream) ftarg.stream)->size;
	nfaces = face->num_faces;
	FT_Done_Face(face);
	face = 0;
	for (k = 0; k < nfaces; k++) {
	    Tcl_HashEntry *hPtr;
	    FileFaceKey ffKey;
	    int isNew;

	    memset(&ftarg, 0, sizeof (ftarg));
	    ftarg.flags = FT_OPEN_STREAM;
	    ftarg.stream = (FT_Stream) SdlTkXGetFTStream(fname, fsize);
	    fterr = FT_Open_Face(ftlib, &ftarg, k, &face);
	    if (fterr != 0) {
	        continue;
	    }
	    if (!(face->face_flags & FT_FACE_FLAG_SCALABLE)) {
	        goto nextface;
	    }
	    if ((face->num_charmaps < 1) || !face->charmap ||
		(face->charmap->encoding != FT_ENCODING_UNICODE)) {
		goto nextface;
	    }
	    memset(&ffKey, 0, sizeof (ffKey));
	    ffKey.file = XInternAtom(SdlTkX.display, fname, False);
	    ffKey.index = k;
	    hPtr = Tcl_CreateHashEntry(&fileFaceHash, (char *) &ffKey,
				       &isNew);
	    if (isNew) {
	        GlyphIndexHash *ghash;
		Tcl_DString ds, ds2;
		char *style, *weight;

		ghash = (GlyphIndexHash *) ckalloc(sizeof (GlyphIndexHash));
		Tcl_InitHashTable(&ghash->hash, TCL_ONE_WORD_KEYS);
		ghash->refCnt = 1;
		ghash->familyName = ckalloc(strlen(face->family_name) + 32);
		strcpy(ghash->familyName, face->family_name);
		ghash->faceFlags = face->face_flags;
		ghash->styleFlags = face->style_flags;
		FNTLOG("FONTINIT: family '%s'", face->family_name);
		FNTLOG("FONTINIT: style  '%s'", face->style_name);
		Tcl_DStringInit(&ds);
		Tcl_DStringInit(&ds2);
		if (face->style_name != NULL) {
		    Tcl_DStringAppend(&ds2, face->style_name, -1);
		    style = Tcl_DStringValue(&ds2);
		    while (*style) {
			*style = tolower((UCHAR(*style)));
			++style;
		    }
		}
		weight = "-normal";
		style = Tcl_DStringValue(&ds2);
		if (strstr(style, "condensed")) {
		    strcat(ghash->familyName, " Condensed");
		}
		if (strstr(style, "black")) {
		    strcat(ghash->familyName, " Black");
		} else if (strstr(style, "extralight")) {
		    strcat(ghash->familyName, " ExtraLight");
		} else if (strstr(style, "light")) {
		    strcat(ghash->familyName, " Light");
		} else if (strstr(style, "thin")) {
		    strcat(ghash->familyName, " Thin");
		} else if (strstr(style, "medium")) {
		    strcat(ghash->familyName, " Medium");
		} else if (strstr(style, "semibold")) {
		    strcat(ghash->familyName, " SemiBold");
		}
		if (ghash->styleFlags & FT_STYLE_FLAG_BOLD) {
		    weight = "-bold";
		}
		Tcl_DStringAppend(&ds, "-*-", -1);
		Tcl_DStringAppend(&ds, ghash->familyName, -1);
		Tcl_DStringAppend(&ds, weight, -1);
		if (ghash->styleFlags & FT_STYLE_FLAG_ITALIC) {
		    Tcl_DStringAppend(&ds, "-o", -1);
		} else {
		    Tcl_DStringAppend(&ds, "-r", -1);
		}
		Tcl_DStringAppend(&ds, "-*-*-*-*-*-*-*-*-ucs-4", -1);
		ghash->xlfdPattern = ckalloc(Tcl_DStringLength(&ds) + 1);
		strcpy(ghash->xlfdPattern, Tcl_DStringValue(&ds));
		Tcl_DStringFree(&ds);
		Tcl_DStringFree(&ds2);
		ghash->hashLoaded = 0;
		Tcl_SetHashValue(hPtr, (char *) ghash);
		FNTLOG("FONTINIT: '%s' -> '%s' fflg=%lx sflg=%lx",
		       ghash->xlfdPattern, (char *) ffKey.file,
		       ghash->faceFlags, ghash->styleFlags);
		nfonts++;
	    }
nextface:
	    FT_Done_Face(face);
	}
    }
    FT_Done_FreeType(ftlib);

    if (nfonts == 0) {
	Tcl_ResetResult(interp);
	Tcl_AppendResult(interp, "no fonts installed", (char *) NULL);
	goto error;
    }

    /* Every font character set is in ucs-4 */
    type.encodingName	= "ucs-4";
    type.toUtfProc	= Ucs4ToUtfProc;
    type.fromUtfProc	= UtfToUcs4Proc;
    type.freeProc	= NULL;
    type.clientData	= NULL;
    type.nullSize	= 2;
    Tcl_CreateEncoding(&type);

    initialized = 1;
#ifndef ANDROID
    /* unhide root window now */
    SDL_ShowWindow(SdlTkX.sdlscreen);
#endif
success:
    Tcl_MutexUnlock(&fontMutex);
    return TCL_OK;

error:
    Tcl_MutexUnlock(&fontMutex);
    return TCL_ERROR;
}

int
SdlTkFontAdd(Tcl_Interp *interp, const char *fname)
{
    FT_Error fterr;
    FT_Library ftlib;
    FT_Face face;
    FT_Open_Args ftarg;
    int k, nfonts = 0, nfaces, fsize;
    Tcl_HashEntry *hPtr;
    Tcl_HashTable famHash;
    Tcl_HashSearch search;

    if (SdlTkFontInit(interp) != TCL_OK) {
	return TCL_ERROR;
    }

    Tcl_MutexLock(&fontMutex);

    fterr = FT_Init_FreeType(&ftlib);
    if (fterr != 0) {
	Tcl_AppendResult(interp, "error initializing freetype", (char *) NULL);
	goto error;
    }
    memset(&ftarg, 0, sizeof (ftarg));
    ftarg.flags = FT_OPEN_STREAM;
    ftarg.stream = (FT_Stream) SdlTkXGetFTStream(fname, 0);
    fterr = FT_Open_Face(ftlib, &ftarg, -1, &face);
    if (fterr != 0) {
	Tcl_AppendResult(interp, "cannot open font file", (char *) NULL);
	goto error;
    }
    fsize = ((FT_Stream) ftarg.stream)->size;
    nfaces = face->num_faces;
    FT_Done_Face(face);
    face = 0;
    Tcl_InitHashTable(&famHash, TCL_STRING_KEYS);
    for (k = 0; k < nfaces; k++) {
	FileFaceKey ffKey;
	int isNew;

	memset(&ftarg, 0, sizeof (ftarg));
	ftarg.flags = FT_OPEN_STREAM;
	ftarg.stream = (FT_Stream) SdlTkXGetFTStream(fname, fsize);
	fterr = FT_Open_Face(ftlib, &ftarg, k, &face);
	if (fterr != 0) {
	    continue;
	}
	if (!(face->face_flags & FT_FACE_FLAG_SCALABLE)) {
	    goto nextface;
	}
	if ((face->num_charmaps < 1) || !face->charmap ||
	    (face->charmap->encoding != FT_ENCODING_UNICODE)) {
	    goto nextface;
	}
	memset(&ffKey, 0, sizeof (ffKey));
	ffKey.file = XInternAtom(SdlTkX.display, fname, False);
	ffKey.index = k;
	hPtr = Tcl_CreateHashEntry(&fileFaceHash, (char *) &ffKey, &isNew);
	if (isNew) {
	    GlyphIndexHash *ghash;
	    Tcl_DString ds, ds2;
	    char *style;

	    ghash = (GlyphIndexHash *) ckalloc(sizeof (GlyphIndexHash));
	    Tcl_InitHashTable(&ghash->hash, TCL_ONE_WORD_KEYS);
	    ghash->refCnt = 1;
	    ghash->familyName = ckalloc(strlen(face->family_name) + 32);
	    strcpy(ghash->familyName, face->family_name);
	    ghash->faceFlags = face->face_flags;
	    ghash->styleFlags = face->style_flags;
	    FNTLOG("FONTADD: family '%s'", face->family_name);
	    FNTLOG("FONTADD: style  '%s'", face->style_name);
	    Tcl_DStringInit(&ds);
	    Tcl_DStringInit(&ds2);
	    if (face->style_name != NULL) {
		Tcl_DStringAppend(&ds2, face->style_name, -1);
		style = Tcl_DStringValue(&ds2);
		while (*style) {
		    *style = tolower((UCHAR(*style)));
		    ++style;
		}
	    }
	    style = Tcl_DStringValue(&ds2);
	    if (strstr(style, "condensed")) {
		strcat(ghash->familyName, " Condensed");
	    }
	    if (strstr(style, "black")) {
		strcat(ghash->familyName, " Black");
	    } else if (strstr(style, "extralight")) {
		strcat(ghash->familyName, " ExtraLight");
	    } else if (strstr(style, "light")) {
		strcat(ghash->familyName, " Light");
	    } else if (strstr(style, "thin")) {
		strcat(ghash->familyName, " Thin");
	    } else if (strstr(style, "medium")) {
		strcat(ghash->familyName, " Medium");
	    } else if (strstr(style, "semibold")) {
		strcat(ghash->familyName, " SemiBold");
	    }
	    Tcl_DStringAppend(&ds, "-*-", -1);
	    Tcl_DStringAppend(&ds, ghash->familyName, -1);
	    if (ghash->styleFlags & FT_STYLE_FLAG_BOLD) {
		Tcl_DStringAppend(&ds, "-bold", -1);
	    } else {
		Tcl_DStringAppend(&ds, "-normal", -1);
	    }
	    if (ghash->styleFlags & FT_STYLE_FLAG_ITALIC) {
		Tcl_DStringAppend(&ds, "-o", -1);
	    } else {
		Tcl_DStringAppend(&ds, "-r", -1);
	    }
	    Tcl_DStringAppend(&ds, "-*-*-*-*-*-*-*-*-ucs-4", -1);
	    ghash->xlfdPattern = ckalloc(Tcl_DStringLength(&ds) + 1);
	    strcpy(ghash->xlfdPattern, Tcl_DStringValue(&ds));
	    Tcl_DStringFree(&ds);
	    Tcl_DStringFree(&ds2);
	    ghash->hashLoaded = 0;
	    Tcl_SetHashValue(hPtr, (char *) ghash);
	    nfonts++;
	    Tcl_CreateHashEntry(&famHash, ghash->familyName, &isNew);
	    FNTLOG("FONTADD: '%s' -> '%s' fflg=%lx sflg=%lx",
		   ghash->xlfdPattern, (char *) ffKey.file,
		   ghash->faceFlags, ghash->styleFlags);
	}
nextface:
	FT_Done_Face(face);
    }
    FT_Done_FreeType(ftlib);

    if (nfonts == 0) {
	Tcl_DeleteHashTable(&famHash);
	Tcl_AppendResult(interp, "no fonts installed", (char *) NULL);
	goto error;
    }

    Tcl_MutexUnlock(&fontMutex);
    hPtr = Tcl_FirstHashEntry(&famHash, &search);
    while (hPtr != NULL) {
	Tcl_AppendElement(interp, Tcl_GetHashKey(&famHash, hPtr));
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&famHash);
    return TCL_OK;

error:
    Tcl_MutexUnlock(&fontMutex);
    return TCL_ERROR;
}

int
SdlTkFontList(Tcl_Interp *interp)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    Tcl_Obj *list;

    if (SdlTkFontInit(interp) != TCL_OK) {
	return TCL_ERROR;
    }

    list = Tcl_NewListObj(0, NULL);
    Tcl_MutexLock(&fontMutex);
    hPtr = Tcl_FirstHashEntry(&fileFaceHash, &search);
    while (hPtr != NULL) {
	GlyphIndexHash *ghash;
	FileFaceKey *ffKey;

	ghash = (GlyphIndexHash *) Tcl_GetHashValue(hPtr);
	ffKey = (FileFaceKey *) Tcl_GetHashKey(&fileFaceHash, hPtr);
	Tcl_ListObjAppendElement(NULL, list,
		Tcl_NewStringObj(ghash->xlfdPattern, -1));
	Tcl_ListObjAppendElement(NULL, list,
		Tcl_NewStringObj((char *) ffKey->file, -1));
	Tcl_ListObjAppendElement(NULL, list, Tcl_NewIntObj(ffKey->index));
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_MutexUnlock(&fontMutex);
    Tcl_SetObjResult(interp, list);
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
