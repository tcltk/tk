/*
 * tkWinIco.h --
 *
 *	This file contains declarations for icon-manipulation routines
 *      in Windows. 
 *
 * Copyright (c) 1995-1996 Microsoft Corp.
 * Copyright (c) 1998 Brueckner & Jarosch Ing.GmbH, Erfurt, Germany
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkWinIco.h"


/*
 *----------------------------------------------------------------------
 *
 * DIBNumColors --
 *
 *	Calculates the number of entries in the color table, given by LPSTR
 *	lpbi - pointer to the CF_DIB memory block. Used by titlebar icon code.
 *
 * Results:
 *	WORD - Number of entries in the color table.
 *
 *----------------------------------------------------------------------
 */

static WORD
DIBNumColors(
    LPSTR lpbi)
{
    WORD wBitCount;
    DWORD dwClrUsed;

    dwClrUsed = ((LPBITMAPINFOHEADER) lpbi)->biClrUsed;

    if (dwClrUsed) {
	return (WORD) dwClrUsed;
    }

    wBitCount = ((LPBITMAPINFOHEADER) lpbi)->biBitCount;

    switch (wBitCount) {
    case 1:
	return 2;
    case 4:
	return 16;
    case 8:
	return 256;
    default:
	return 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PaletteSize --
 *
 *	Calculates the number of bytes in the color table, as given by LPSTR
 *	lpbi - pointer to the CF_DIB memory block. Used by titlebar icon code.
 *
 * Results:
 *	Number of bytes in the color table
 *
 *----------------------------------------------------------------------
 */
static WORD
PaletteSize(
    LPSTR lpbi)
{
    return (WORD) (DIBNumColors(lpbi) * sizeof(RGBQUAD));
}

/*
 *----------------------------------------------------------------------
 *
 * FindDIBits --
 *
 *	Locate the image bits in a CF_DIB format DIB, as given by LPSTR lpbi -
 *	pointer to the CF_DIB memory block. Used by titlebar icon code.
 *
 * Results:
 *	pointer to the image bits
 *
 * Side effects: None
 *
 *
 *----------------------------------------------------------------------
 */

static LPSTR
FindDIBBits(
    LPSTR lpbi)
{
    return lpbi + *((LPDWORD) lpbi) + PaletteSize(lpbi);
}

/*
 *----------------------------------------------------------------------
 *
 * BytesPerLine --
 *
 *	Calculates the number of bytes in one scan line, as given by
 *	LPBITMAPINFOHEADER lpBMIH - pointer to the BITMAPINFOHEADER that
 *	begins the CF_DIB block. Used by titlebar icon code.
 *
 * Results:
 *	number of bytes in one scan line (DWORD aligned)
 *
 *----------------------------------------------------------------------
 */

static DWORD
BytesPerLine(
    LPBITMAPINFOHEADER lpBMIH)
{
    return WIDTHBYTES(lpBMIH->biWidth * lpBMIH->biPlanes * lpBMIH->biBitCount);
}

/*
 *----------------------------------------------------------------------
 *
 * AdjustIconImagePointers --
 *
 *	Adjusts internal pointers in icon resource struct, as given by
 *	LPICONIMAGE lpImage - the resource to handle. Used by titlebar icon
 *	code.
 *
 * Results:
 *	BOOL - TRUE for success, FALSE for failure
 *
 *----------------------------------------------------------------------
 */

static BOOL
AdjustIconImagePointers(
    LPICONIMAGE lpImage)
{
    /*
     * Sanity check.
     */

    if (lpImage == NULL) {
	return FALSE;
    }

    /*
     * BITMAPINFO is at beginning of bits.
     */

    lpImage->lpbi = (LPBITMAPINFO) lpImage->lpBits;

    /*
     * Width - simple enough.
     */

    lpImage->Width = lpImage->lpbi->bmiHeader.biWidth;

    /*
     * Icons are stored in funky format where height is doubled so account for
     * that.
     */

    lpImage->Height = (lpImage->lpbi->bmiHeader.biHeight)/2;

    /*
     * How many colors?
     */

    lpImage->Colors = lpImage->lpbi->bmiHeader.biPlanes
	    * lpImage->lpbi->bmiHeader.biBitCount;

    /*
     * XOR bits follow the header and color table.
     */

    lpImage->lpXOR = (LPBYTE) FindDIBBits((LPSTR) lpImage->lpbi);

    /*
     * AND bits follow the XOR bits.
     */

    lpImage->lpAND = lpImage->lpXOR +
	    lpImage->Height*BytesPerLine((LPBITMAPINFOHEADER) lpImage->lpbi);
    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * MakeIconOrCursorFromResource --
 *
 *	Construct an actual HICON structure from the information in a
 *	resource.
 *
 * Results:
 *	Icon
 *
 *----------------------------------------------------------------------
 */

static HICON
MakeIconOrCursorFromResource(
    LPICONIMAGE lpIcon,
    BOOL isIcon)
{
    HICON hIcon;

    /*
     * Sanity Check
     */

    if (lpIcon == NULL || lpIcon->lpBits == NULL) {
	return NULL;
    }

    /*
     * Let the OS do the real work :)
     */

    hIcon = (HICON) CreateIconFromResourceEx(lpIcon->lpBits,
	    lpIcon->dwNumBytes, isIcon, 0x00030000,
	    (*(LPBITMAPINFOHEADER) lpIcon->lpBits).biWidth,
	    (*(LPBITMAPINFOHEADER) lpIcon->lpBits).biHeight/2, 0);

    /*
     * It failed, odds are good we're on NT so try the non-Ex way.
     */

    if (hIcon == NULL) {
	/*
	 * We would break on NT if we try with a 16bpp image.
	 */

	if (lpIcon->lpbi->bmiHeader.biBitCount != 16) {
	    hIcon = CreateIconFromResource(lpIcon->lpBits, lpIcon->dwNumBytes,
		    isIcon, 0x00030000);
	}
    }
    return hIcon;
}

/*
 *----------------------------------------------------------------------
 *
 * ReadICOHeader --
 *
 *	Reads the header from an ICO file, as specfied by channel.
 *
 * Results:
 *	UINT - Number of images in file, -1 for failure. If this succeeds,
 *	there is a decent chance this is a valid icon file.
 *
 *----------------------------------------------------------------------
 */

static int
ReadICOHeader(
    Tcl_Channel channel)
{
    union {
	WORD word;
	char bytes[sizeof(WORD)];
    } input;

    /*
     * Read the 'reserved' WORD, which should be a zero word.
     */

    if (Tcl_Read(channel, input.bytes, sizeof(WORD)) != sizeof(WORD)) {
	return -1;
    }
    if (input.word != 0) {
	return -1;
    }

    /*
     * Read the type WORD, which should be of type 1.
     */

    if (Tcl_Read(channel, input.bytes, sizeof(WORD)) != sizeof(WORD)) {
	return -1;
    }
    if (input.word != 1) {
	return -1;
    }

    /*
     * Get and return the count of images.
     */

    if (Tcl_Read(channel, input.bytes, sizeof(WORD)) != sizeof(WORD)) {
	return -1;
    }
    return (int) input.word;
}

/*
 *----------------------------------------------------------------------
 *
 * ReadIconFromFile --
 *
 *	Read the contents of a file (usually .ico, .icr) and extract an icon
 *	resource, if possible, otherwise check if the shell has an icon
 *	assigned to the given file and use that. If both of those fail, then
 *	NULL is returned, and an error message will already be in the
 *	interpreter.
 *
 * Results:
 *	A WinIconPtr structure containing the icons in the file, with its ref
 *	count already incremented. The calling function should either place
 *	this structure inside a WmInfo structure, or it should pass it on to
 *	DecrIconRefCount() to ensure no memory leaks occur.
 *
 *	If the given fileName did not contain a valid icon structure,
 *	return NULL.
 *
 * Side effects:
 *	Memory is allocated for the returned structure and the icons it
 *	contains. If the structure is not wanted, it should be passed to
 *	DecrIconRefCount, and in any case a valid ref count should be ensured
 *	to avoid memory leaks.
 *
 *	Currently icon resources are not shared, so the ref count of one of
 *	these structures will always be 0 or 1. However all we need do is
 *	implement some sort of lookup function between filenames and
 *	WinIconPtr structures and no other code will need to be changed. The
 *	pseudo-code for this is implemented below in the 'if (0)' branch. It
 *	did not seem necessary to implement this optimisation here, since
 *	moving to icon<->image conversions will probably make it obsolete.
 *
 *----------------------------------------------------------------------
 */

static WinIconPtr
ReadIconFromFile(
    Tcl_Interp *interp,
    Tcl_Obj *fileName)
{
    WinIconPtr titlebaricon = NULL;
    BlockOfIconImagesPtr lpIR;

#if 0 /* TODO: Dead code? */
    if (0 /* If we already have an icon for this filename */) {
	titlebaricon = NULL; /* Get the real value from a lookup */
	titlebaricon->refCount++;
	return titlebaricon;
    }
#endif

    /*
     * First check if it is a .ico file.
     */

    lpIR = ReadIconOrCursorFromFile(interp, fileName, TRUE);

    /*
     * Then see if we can ask the shell for the icon for this file. We
     * want both the regular and small icons so that the Alt-Tab (task-
     * switching) display uses the right icon.
     */

    if (lpIR == NULL) {
	SHFILEINFOW sfiSM;
	Tcl_DString ds, ds2;
	DWORD *res;
	const char *file;

	file = Tcl_TranslateFileName(interp, Tcl_GetString(fileName), &ds);
	if (file == NULL) {
	    return NULL;
	}
	Tcl_DStringInit(&ds2);
	res = (DWORD *)SHGetFileInfoW(Tcl_UtfToWCharDString(file, -1, &ds2), 0, &sfiSM,
		sizeof(SHFILEINFO), SHGFI_SMALLICON|SHGFI_ICON);
	Tcl_DStringFree(&ds);

	if (res != 0) {
	    SHFILEINFOW sfi;
	    unsigned size;

	    Tcl_ResetResult(interp);
	    res = (DWORD *)SHGetFileInfoW((WCHAR *)Tcl_DStringValue(&ds2), 0, &sfi,
		    sizeof(SHFILEINFO), SHGFI_ICON);

	    /*
	     * Account for extra icon, if necessary.
	     */

	    size = sizeof(BlockOfIconImages)
		    + ((res != 0) ? sizeof(ICONIMAGE) : 0);
	    lpIR = (BlockOfIconImagesPtr)ckalloc(size);
	    if (lpIR == NULL) {
		if (res != 0) {
		    DestroyIcon(sfi.hIcon);
		}
		DestroyIcon(sfiSM.hIcon);
		Tcl_DStringFree(&ds2);
		return NULL;
	    }
	    ZeroMemory(lpIR, size);

	    lpIR->nNumImages		= ((res != 0) ? 2 : 1);
	    lpIR->IconImages[0].Width	= 16;
	    lpIR->IconImages[0].Height	= 16;
	    lpIR->IconImages[0].Colors	= 4;
	    lpIR->IconImages[0].hIcon	= sfiSM.hIcon;

	    /*
	     * All other IconImages fields are ignored.
	     */

	    if (res != 0) {
		lpIR->IconImages[1].Width	= 32;
		lpIR->IconImages[1].Height	= 32;
		lpIR->IconImages[1].Colors	= 4;
		lpIR->IconImages[1].hIcon	= sfi.hIcon;
	    }
	}
	Tcl_DStringFree(&ds2);
    }
    if (lpIR != NULL) {
	titlebaricon = (WinIconPtr)ckalloc(sizeof(WinIconInstance));
	titlebaricon->iconBlock = lpIR;
	titlebaricon->refCount = 1;
    }
    return titlebaricon;
}

/*
 *----------------------------------------------------------------------
 *
 * GetIconFromPixmap --
 *
 *	Turn a Tk Pixmap (i.e. a bitmap) into an icon resource, if possible,
 *	otherwise NULL is returned.
 *
 * Results:
 *	A WinIconPtr structure containing a conversion of the given bitmap
 *	into an icon, with its ref count already incremented. The calling
 *	function should either place this structure inside a WmInfo structure,
 *	or it should pass it on to DecrIconRefCount() to ensure no memory
 *	leaks occur.
 *
 *	If the given pixmap did not contain a valid icon structure, return
 *	NULL.
 *
 * Side effects:
 *	Memory is allocated for the returned structure and the icons it
 *	contains. If the structure is not wanted, it should be passed to
 *	DecrIconRefCount, and in any case a valid ref count should be ensured
 *	to avoid memory leaks.
 *
 *	Currently icon resources are not shared, so the ref count of one of
 *	these structures will always be 0 or 1. However all we need do is
 *	implement some sort of lookup function between pixmaps and WinIconPtr
 *	structures and no other code will need to be changed.
 *
 *----------------------------------------------------------------------
 */

static WinIconPtr
GetIconFromPixmap(
    Display *dsPtr,
    Pixmap pixmap)
{
    WinIconPtr titlebaricon = NULL;
    TkWinDrawable *twdPtr = (TkWinDrawable *) pixmap;
    BlockOfIconImagesPtr lpIR;
    ICONINFO icon;
    HICON hIcon;
    int width, height;

    if (twdPtr == NULL) {
	return NULL;
    }

#if 0 /* TODO: Dead code?*/
    if (0 /* If we already have an icon for this pixmap */) {
	titlebaricon = NULL; /* Get the real value from a lookup */
	titlebaricon->refCount++;
	return titlebaricon;
    }
#endif

    Tk_SizeOfBitmap(dsPtr, pixmap, &width, &height);

    icon.fIcon = TRUE;
    icon.xHotspot = 0;
    icon.yHotspot = 0;
    icon.hbmMask = twdPtr->bitmap.handle;
    icon.hbmColor = twdPtr->bitmap.handle;

    hIcon = CreateIconIndirect(&icon);
    if (hIcon == NULL) {
	return NULL;
    }

    lpIR = (BlockOfIconImagesPtr)ckalloc(sizeof(BlockOfIconImages));
    if (lpIR == NULL) {
	DestroyIcon(hIcon);
	return NULL;
    }

    lpIR->nNumImages = 1;
    lpIR->IconImages[0].Width = width;
    lpIR->IconImages[0].Height = height;
    lpIR->IconImages[0].Colors = 1 << twdPtr->bitmap.depth;
    lpIR->IconImages[0].hIcon = hIcon;

    /*
     * These fields are ignored.
     */

    lpIR->IconImages[0].lpBits = 0;
    lpIR->IconImages[0].dwNumBytes = 0;
    lpIR->IconImages[0].lpXOR = 0;
    lpIR->IconImages[0].lpAND = 0;

    titlebaricon = (WinIconPtr)ckalloc(sizeof(WinIconInstance));
    titlebaricon->iconBlock = lpIR;
    titlebaricon->refCount = 1;
    return titlebaricon;
}

/*
 *----------------------------------------------------------------------
 *
 * DecrIconRefCount --
 *
 *	Reduces the reference count.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If the ref count falls to zero, free the memory associated with the
 *	icon resource structures. In this case the pointer passed into this
 *	function is no longer valid.
 *
 *----------------------------------------------------------------------
 */

static void
DecrIconRefCount(
    WinIconPtr titlebaricon)
{
    if (titlebaricon->refCount-- <= 1) {
	if (titlebaricon->iconBlock != NULL) {
	    FreeIconBlock(titlebaricon->iconBlock);
	}
	titlebaricon->iconBlock = NULL;

	ckfree(titlebaricon);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FreeIconBlock --
 *
 *	Frees all memory associated with a previously loaded titlebaricon.
 *	The icon block pointer is no longer valid once this function returns.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *
 *
 *----------------------------------------------------------------------
 */

static void
FreeIconBlock(
    BlockOfIconImagesPtr lpIR)
{
    int i;

    /*
     * Free all the bits.
     */

    for (i=0 ; i<lpIR->nNumImages ; i++) {
	if (lpIR->IconImages[i].lpBits != NULL) {
	    ckfree(lpIR->IconImages[i].lpBits);
	}
	if (lpIR->IconImages[i].hIcon != NULL) {
	    DestroyIcon(lpIR->IconImages[i].hIcon);
	}
    }
    ckfree(lpIR);
}

/*
 *----------------------------------------------------------------------
 *
 * GetIcon --
 *
 *	Extracts an icon of a given size from an icon resource
 *
 * Results:
 *	Returns the icon, if found, else NULL.
 *
 *----------------------------------------------------------------------
 */

static HICON
GetIcon(
    WinIconPtr titlebaricon,
    int icon_size)
{
    BlockOfIconImagesPtr lpIR;
    unsigned int size = (icon_size == 0 ? 16 : 32);
    int i;

    if (titlebaricon == NULL) {
	return NULL;
    }

    lpIR = titlebaricon->iconBlock;
    if (lpIR == NULL) {
	return NULL;
    }

    for (i=0 ; i<lpIR->nNumImages ; i++) {
	/*
	 * Take the first or a 32x32 16 color icon
	 */

	if ((lpIR->IconImages[i].Height == size)
		&& (lpIR->IconImages[i].Width == size)
		&& (lpIR->IconImages[i].Colors >= 4)) {
	    return lpIR->IconImages[i].hIcon;
	}
    }

    /*
     * If we get here, then just return the first one, it will have to do!
     */

    if (lpIR->nNumImages >= 1) {
	return lpIR->IconImages[0].hIcon;
    }
    return NULL;
}

#if 0 /* UNUSED */
static HCURSOR
TclWinReadCursorFromFile(
    Tcl_Interp* interp,
    Tcl_Obj* fileName)
{
    BlockOfIconImagesPtr lpIR;
    HICON res = NULL;

    lpIR = ReadIconOrCursorFromFile(interp, fileName, FALSE);
    if (lpIR == NULL) {
	return NULL;
    }
    if (lpIR->nNumImages >= 1) {
	res = CopyImage(lpIR->IconImages[0].hIcon, IMAGE_CURSOR, 0, 0, 0);
    }
    FreeIconBlock(lpIR);
    return res;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * ReadIconOrCursorFromFile --
 *
 *	Reads an Icon Resource from an ICO file, as given by char* fileName -
 *	Name of the ICO file. This name should be in Utf format.
 *
 * Results:
 *	Returns an icon resource, if found, else NULL.
 *
 * Side effects:
 *	May leave error messages in the Tcl interpreter.
 *
 *----------------------------------------------------------------------
 */

static BlockOfIconImagesPtr
ReadIconOrCursorFromFile(
    Tcl_Interp *interp,
    Tcl_Obj *fileName,
    BOOL isIcon)
{
    BlockOfIconImagesPtr lpIR;
    Tcl_Channel channel;
    int i;
    DWORD dwBytesRead;
    LPICONDIRENTRY lpIDE;

    /*
     * Open the file.
     */

    channel = Tcl_FSOpenFileChannel(interp, fileName, "r", 0);
    if (channel == NULL) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"error opening file \"%s\" for reading: %s",
		Tcl_GetString(fileName), Tcl_PosixError(interp)));
	return NULL;
    }
    if (Tcl_SetChannelOption(interp, channel, "-translation", "binary")
	    != TCL_OK) {
	Tcl_Close(NULL, channel);
	return NULL;
    }
    if (Tcl_SetChannelOption(interp, channel, "-encoding", "binary")
	    != TCL_OK) {
	Tcl_Close(NULL, channel);
	return NULL;
    }

    /*
     * Allocate memory for the resource structure
     */

    lpIR = (BlockOfIconImagesPtr)ckalloc(sizeof(BlockOfIconImages));

    /*
     * Read in the header
     */

    lpIR->nNumImages = ReadICOHeader(channel);
    if (lpIR->nNumImages == -1) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("Invalid file header", -1));
	Tcl_Close(NULL, channel);
	ckfree(lpIR);
	return NULL;
    }

    /*
     * Adjust the size of the struct to account for the images.
     */

    lpIR = (BlockOfIconImagesPtr)ckrealloc(lpIR, sizeof(BlockOfIconImages)
	    + (lpIR->nNumImages - 1) * sizeof(ICONIMAGE));

    /*
     * Allocate enough memory for the icon directory entries.
     */

    lpIDE = (LPICONDIRENTRY)ckalloc(lpIR->nNumImages * sizeof(ICONDIRENTRY));

    /*
     * Read in the icon directory entries.
     */

    dwBytesRead = Tcl_Read(channel, (char *) lpIDE,
	    (int) (lpIR->nNumImages * sizeof(ICONDIRENTRY)));
    if (dwBytesRead != lpIR->nNumImages * sizeof(ICONDIRENTRY)) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"error reading file: %s", Tcl_PosixError(interp)));
	Tcl_SetErrorCode(interp, "TK", "WM", "ICON", "READ", NULL);
	Tcl_Close(NULL, channel);
	ckfree(lpIDE);
	ckfree(lpIR);
	return NULL;
    }

    /*
     * NULL-out everything to make memory management easier.
     */

    for (i = 0; i < lpIR->nNumImages; i++) {
	lpIR->IconImages[i].lpBits = NULL;
    }

    /*
     * Loop through and read in each image.
     */

    for (i=0 ; i<lpIR->nNumImages ; i++) {
	/*
	 * Allocate memory for the resource.
	 */

	lpIR->IconImages[i].lpBits = (LPBYTE)ckalloc(lpIDE[i].dwBytesInRes);
	lpIR->IconImages[i].dwNumBytes = lpIDE[i].dwBytesInRes;

	/*
	 * Seek to beginning of this image.
	 */

	if (Tcl_Seek(channel, lpIDE[i].dwImageOffset, FILE_BEGIN) == -1) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "error seeking in file: %s", Tcl_PosixError(interp)));
	    goto readError;
	}

	/*
	 * Read it in.
	 */

	dwBytesRead = Tcl_Read(channel, (char *)lpIR->IconImages[i].lpBits,
		(int) lpIDE[i].dwBytesInRes);
	if (dwBytesRead != lpIDE[i].dwBytesInRes) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "error reading file: %s", Tcl_PosixError(interp)));
	    goto readError;
	}

	/*
	 * Set the internal pointers appropriately.
	 */

	if (!AdjustIconImagePointers(&lpIR->IconImages[i])) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(
		    "Error converting to internal format", -1));
	    Tcl_SetErrorCode(interp, "TK", "WM", "ICON", "FORMAT", NULL);
	    goto readError;
	}
	lpIR->IconImages[i].hIcon =
		MakeIconOrCursorFromResource(&lpIR->IconImages[i], isIcon);
    }

    /*
     * Clean up
     */

    ckfree(lpIDE);
    Tcl_Close(NULL, channel);
    return lpIR;

  readError:
    Tcl_Close(NULL, channel);
    for (i = 0; i < lpIR->nNumImages; i++) {
	if (lpIR->IconImages[i].lpBits != NULL) {
	    ckfree(lpIR->IconImages[i].lpBits);
	}
    }
    ckfree(lpIDE);
    ckfree(lpIR);
    return NULL;
}
