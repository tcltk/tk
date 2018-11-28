/*
 * rbcPs.c --
 *
 *      This module implements general PostScript conversion routines.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

#include <X11/Xatom.h>

#define PS_MAXPATH	1500    /* Maximum number of components in a PostScript
                                 * (level 1) path. */

static void     XColorToPostScript(
    RbcPsToken * tokenPtr,
    XColor * colorPtr);
static unsigned char ReverseBits(
    register unsigned char byte);
static void     ByteToHex(
    register unsigned char byte,
    char *string);
#ifndef  _WIN32
static const char *NameOfAtom(
    Tk_Window tkwin,
    Atom atom);
#endif
static void     TextLayoutToPostScript(
    RbcPsToken * tokenPtr,
    int x,
    int y,
    RbcTextLayout * textPtr);

/*
 *--------------------------------------------------------------
 *
 * RbcGetPsToken --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
RbcPsToken     *
RbcGetPsToken(
    Tcl_Interp * interp,
    Tk_Window tkwin)
{
    RbcPsToken     *tokenPtr;

    tokenPtr = (RbcPsToken *) ckalloc(sizeof(RbcPsToken));
    assert(tokenPtr);

    tokenPtr->fontVarName = tokenPtr->colorVarName = NULL;
    tokenPtr->interp = interp;
    tokenPtr->tkwin = tkwin;
    tokenPtr->colorMode = PS_MODE_COLOR;
    Tcl_DStringInit(&(tokenPtr->dString));
    return tokenPtr;
}

/*
 *--------------------------------------------------------------
 *
 * RbcReleasePsToken --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcReleasePsToken(
    RbcPsToken * tokenPtr)
{
    Tcl_DStringFree(&(tokenPtr->dString));
    ckfree((char *) tokenPtr);
}

/*
 *--------------------------------------------------------------
 *
 * RbcPostScriptFromToken --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
char           *
RbcPostScriptFromToken(
    RbcPsToken * tokenPtr)
{
    return Tcl_DStringValue(&(tokenPtr->dString));
}

/*
 *--------------------------------------------------------------
 *
 * RbcScratchBufferFromToken --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
char           *
RbcScratchBufferFromToken(
    RbcPsToken * tokenPtr)
{
    return tokenPtr->scratchArr;
}

/*
 *--------------------------------------------------------------
 *
 * RbcAppendToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcAppendToPostScript(
    RbcPsToken * psToken,
    ...)
{
    va_list         argList;
    RbcPsToken     *tokenPtr;
    char           *string;

    tokenPtr = psToken;
    va_start(argList, psToken);
    for (;;) {
        string = va_arg(argList, char *);
        if (string == NULL) {
            break;
        }
        Tcl_DStringAppend(&(tokenPtr->dString), string, -1);
    }
}

/*
 *--------------------------------------------------------------
 *
 * RbcFormatToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcFormatToPostScript(
    RbcPsToken * psToken,
    ...)
{
    va_list         argList;
    RbcPsToken     *tokenPtr;
    char           *fmt;

    tokenPtr = psToken;
    va_start(argList, psToken);
    fmt = va_arg(argList, char *);
    vsprintf(tokenPtr->scratchArr, fmt, argList);
    va_end(argList);
    Tcl_DStringAppend(&(tokenPtr->dString), tokenPtr->scratchArr, -1);
}

/*
 *----------------------------------------------------------------------
 *
 * XColorToPostScript --
 *
 *      Convert the a XColor (from its RGB values) to a PostScript
 *      command.  If a Tk color map variable exists, it will be
 *      consulted for a PostScript translation based upon the color
 *      name.
 *
 *      Maps an X color intensity (0 to 2^16-1) to a floating point
 *      value [0..1].  Many versions of Tk don't properly handle the
 *      the lower 8 bits of the color intensity, so we can only
 *      consider the upper 8 bits.
 *
 * Results:
 *      The string representing the color mode is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void
XColorToPostScript(
    RbcPsToken * tokenPtr,
    XColor * colorPtr)
{                               /* Color value to be converted */
    /*
     * Shift off the lower byte before dividing because some versions
     * of Tk don't fill the lower byte correctly.
     */
    RbcFormatToPostScript(tokenPtr, "%g %g %g",
        ((double) (colorPtr->red >> 8) / 255.0),
        ((double) (colorPtr->green >> 8) / 255.0),
        ((double) (colorPtr->blue >> 8) / 255.0));
}

/*
 *--------------------------------------------------------------
 *
 * RbcBackgroundToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcBackgroundToPostScript(
    RbcPsToken * tokenPtr,
    XColor * colorPtr)
{
    /* If the color name exists in Tcl array variable, use that translation */
    if (tokenPtr->colorVarName != NULL) {
        const char     *psColor;

        psColor = Tcl_GetVar2(tokenPtr->interp, tokenPtr->colorVarName,
            Tk_NameOfColor(colorPtr), 0);
        if (psColor != NULL) {
            RbcAppendToPostScript(tokenPtr, " ", psColor, "\n", (char *) NULL);
            return;
        }
    }
    XColorToPostScript(tokenPtr, colorPtr);
    RbcAppendToPostScript(tokenPtr, " SetBgColor\n", (char *) NULL);
}

/*
 *--------------------------------------------------------------
 *
 * RbcForegroundToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcForegroundToPostScript(
    RbcPsToken * tokenPtr,
    XColor * colorPtr)
{
    /* If the color name exists in Tcl array variable, use that translation */
    if (tokenPtr->colorVarName != NULL) {
        const char     *psColor;

        psColor = Tcl_GetVar2(tokenPtr->interp, tokenPtr->colorVarName,
            Tk_NameOfColor(colorPtr), 0);
        if (psColor != NULL) {
            RbcAppendToPostScript(tokenPtr, " ", psColor, "\n", (char *) NULL);
            return;
        }
    }
    XColorToPostScript(tokenPtr, colorPtr);
    RbcAppendToPostScript(tokenPtr, " SetFgColor\n", (char *) NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * ReverseBits --
 *
 *      Convert a byte from a X image into PostScript image order.
 *      This requires not only the nybbles to be reversed but also
 *      their bit values.
 *
 * Results:
 *      The converted byte is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static unsigned char
ReverseBits(
    register unsigned char byte)
{
    byte = ((byte >> 1) & 0x55) | ((byte << 1) & 0xaa);
    byte = ((byte >> 2) & 0x33) | ((byte << 2) & 0xcc);
    byte = ((byte >> 4) & 0x0f) | ((byte << 4) & 0xf0);
    return byte;
}

/*
 *----------------------------------------------------------------------
 *
 * ByteToHex --
 *
 *      Convert a byte to its ASCII hexidecimal equivalent.
 *
 * Results:
 *      The converted 2 ASCII character string is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void
ByteToHex(
    register unsigned char byte,
    char *string)
{
    static char     hexDigits[] = "0123456789ABCDEF";

    string[0] = hexDigits[byte >> 4];
    string[1] = hexDigits[byte & 0x0F];
}

#ifdef _WIN32

/*
 * -------------------------------------------------------------------------
 *
 * RbcBitmapDataToPostScript --
 *
 *      Output a PostScript image string of the given bitmap image.
 *      It is assumed the image is one bit deep and a zero value
 *      indicates an off-pixel.  To convert to PostScript, the bits
 *      need to be reversed from the X11 image order.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The PostScript image string is appended.
 *
 * -------------------------------------------------------------------------
 */
void
RbcBitmapDataToPostScript(
    RbcPsToken * tokenPtr,
    Display * display,
    Pixmap bitmap,
    int width,
    int height)
{
    register unsigned char byte;
    register int    x, y, bitPos;
    unsigned long   pixel;
    int             byteCount;
    char            string[10];
    unsigned char  *srcBits, *srcPtr;
    int             bytesPerRow;

    srcBits = RbcGetBitmapData(display, bitmap, width, height, &bytesPerRow);
    if (srcBits == NULL) {
        OutputDebugStringA("Can't get bitmap data");
        return;
    }
    RbcAppendToPostScript(tokenPtr, "\t<", (char *) NULL);
    byteCount = bitPos = 0;     /* Suppress compiler warning */
    for (y = height - 1; y >= 0; y--) {
        srcPtr = srcBits + (bytesPerRow * y);
        byte = 0;
        for (x = 0; x < width; x++) {
            bitPos = x % 8;
            pixel = (*srcPtr & (0x80 >> bitPos));
            if (pixel) {
                byte |= (unsigned char) (1 << bitPos);
            }
            if (bitPos == 7) {
                byte = ReverseBits(byte);
                ByteToHex(byte, string);
                string[2] = '\0';
                byteCount++;
                srcPtr++;
                byte = 0;
                if (byteCount >= 30) {
                    string[2] = '\n';
                    string[3] = '\t';
                    string[4] = '\0';
                    byteCount = 0;
                }
                RbcAppendToPostScript(tokenPtr, string, (char *) NULL);
            }
        }                       /* x */
        if (bitPos != 7) {
            byte = ReverseBits(byte);
            ByteToHex(byte, string);
            string[2] = '\0';
            RbcAppendToPostScript(tokenPtr, string, (char *) NULL);
            byteCount++;
        }
    }                           /* y */
    ckfree((char *) srcBits);
    RbcAppendToPostScript(tokenPtr, ">\n", (char *) NULL);
}

#else

/*
 * -------------------------------------------------------------------------
 *
 * RbcBitmapDataToPostScript --
 *
 *      Output a PostScript image string of the given bitmap image.
 *      It is assumed the image is one bit deep and a zero value
 *      indicates an off-pixel.  To convert to PostScript, the bits
 *      need to be reversed from the X11 image order.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The PostScript image string is appended to interp->result.
 *
 * -------------------------------------------------------------------------
 */
void
RbcBitmapDataToPostScript(
    RbcPsToken * psToken,
    Display * display,
    Pixmap bitmap,
    int width,
    int height)
{
    register unsigned char byte = 0;
    register int    x, y, bitPos;
    unsigned long   pixel;
    XImage         *imagePtr;
    int             byteCount;
    char            string[10];

    imagePtr = XGetImage(display, bitmap, 0, 0, width, height, 1, ZPixmap);
    RbcAppendToPostScript(psToken, "\t<", (char *) NULL);
    byteCount = bitPos = 0;     /* Suppress compiler warning */
    for (y = 0; y < height; y++) {
        byte = 0;
        for (x = 0; x < width; x++) {
            pixel = XGetPixel(imagePtr, x, y);
            bitPos = x % 8;
            byte |= (unsigned char) (pixel << bitPos);
            if (bitPos == 7) {
                byte = ReverseBits(byte);
                ByteToHex(byte, string);
                string[2] = '\0';
                byteCount++;
                byte = 0;
                if (byteCount >= 30) {
                    string[2] = '\n';
                    string[3] = '\t';
                    string[4] = '\0';
                    byteCount = 0;
                }
                RbcAppendToPostScript(psToken, string, (char *) NULL);
            }
        }                       /* x */
        if (bitPos != 7) {
            byte = ReverseBits(byte);
            ByteToHex(byte, string);
            string[2] = '\0';
            RbcAppendToPostScript(psToken, string, (char *) NULL);
            byteCount++;
        }
    }                           /* y */
    RbcAppendToPostScript(psToken, ">\n", (char *) NULL);
    XDestroyImage(imagePtr);
}

#endif /* _WIN32 */

/*
 *----------------------------------------------------------------------
 *
 * RbcColorImageToPsData --
 *
 *      Converts a color image to PostScript RGB (3 components)
 *      or Greyscale (1 component) output.  With 3 components, we
 *      assume the "colorimage" operator is available.
 *
 *      Note that the image converted from bottom to top, to conform
 *      to the PostScript coordinate system.
 *
 * Results:
 *      The PostScript data comprising the color image is written
 *      into the dynamic string.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int
RbcColorImageToPsData(
    RbcColorImage * image,
    int nComponents,
    Tcl_DString * resultPtr,
    const char *prefix)
{
    char            string[10];
    register int    count;
    register int    x, y;
    register RbcPix32 *pixelPtr;
    unsigned char   byte;
    int             width, height;
    int             offset;
    int             nLines;
    width = image->width;
    height = image->height;

    nLines = 0;
    count = 0;
    offset = (height - 1) * width;
    if (nComponents == 3) {
        for (y = (height - 1); y >= 0; y--) {
            pixelPtr = image->bits + offset;
            for (x = 0; x < width; x++, pixelPtr++) {
                if (count == 0) {
                    Tcl_DStringAppend(resultPtr, prefix, -1);
                    Tcl_DStringAppend(resultPtr, " ", -1);
                }
                count += 6;
                ByteToHex(pixelPtr->rgba.red, string);
                ByteToHex(pixelPtr->rgba.green, string + 2);
                ByteToHex(pixelPtr->rgba.blue, string + 4);
                string[6] = '\0';
                if (count >= 60) {
                    string[6] = '\n';
                    string[7] = '\0';
                    count = 0;
                    nLines++;
                }
                Tcl_DStringAppend(resultPtr, string, -1);
            }
            offset -= width;
        }
    } else if (nComponents == 1) {
        for (y = (height - 1); y >= 0; y--) {
            pixelPtr = image->bits + offset;
            for (x = 0; x < width; x++, pixelPtr++) {
                if (count == 0) {
                    Tcl_DStringAppend(resultPtr, prefix, -1);
                    Tcl_DStringAppend(resultPtr, " ", -1);
                }
                count += 2;
                byte = ~(pixelPtr->rgba.red);
                ByteToHex(byte, string);
                string[2] = '\0';
                if (count >= 60) {
                    string[2] = '\n';
                    string[3] = '\0';
                    count = 0;
                    nLines++;
                }
                Tcl_DStringAppend(resultPtr, string, -1);
            }
            offset -= width;
        }
    }
    if (count != 0) {
        Tcl_DStringAppend(resultPtr, "\n", -1);
        nLines++;
    }
    return nLines;
}

#ifndef  _WIN32

/*
 *----------------------------------------------------------------------
 *
 * NameOfAtom --
 *
 *      Wrapper routine for Tk_GetAtomName.  Returns NULL instead of
 *      "?bad atom?" if the atom can't be found.
 *
 * Results:
 *      The name of the atom is returned if found. Otherwise NULL.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
NameOfAtom(
    Tk_Window tkwin,
    Atom atom)
{
    char           *result;

    result = Tk_GetAtomName(tkwin, atom);
    if ((result[0] == '?') && (strcmp(result, "?bad atom?") == 0)) {
        return NULL;
    }
    return result;
}
#endif

typedef struct {
    const char     *alias;
    const char     *fontName;
} FontMap;

static FontMap  psFontMap[] = {
    {"Arial", "Helvetica",},
    {"AvantGarde", "AvantGarde",},
    {"Courier New", "Courier",},
    {"Courier", "Courier",},
    {"Geneva", "Helvetica",},
    {"Helvetica", "Helvetica",},
    {"Monaco", "Courier",},
    {"New Century Schoolbook", "NewCenturySchlbk",},
    {"New York", "Times",},
    {"Palatino", "Palatino",},
    {"Symbol", "Symbol",},
    {"Times New Roman", "Times",},
    {"Times Roman", "Times",},
    {"Times", "Times",},
    {"Utopia", "Utopia",},
    {"ZapfChancery", "ZapfChancery",},
    {"ZapfDingbats", "ZapfDingbats",},
};

static int      nFontNames = (sizeof(psFontMap) / sizeof(FontMap));

#ifndef  _WIN32

/*
 * -----------------------------------------------------------------
 *
 * XFontStructToPostScript --
 *
 *      Map X11 font to a PostScript font. Currently, only fonts whose
 *      FOUNDRY property are "Adobe" are converted. Simply gets the
 *      XA_FULL_NAME and XA_FAMILY properties and pieces together a
 *      PostScript fontname.
 *
 * Results:
 *      Returns the mapped PostScript font name if one is possible.
 *      Otherwise returns NULL.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
static char    *
XFontStructToPostScript(
    Tk_Window tkwin,            /* Window to query for atoms */
    XFontStruct * fontPtr)
{                               /* Font structure to map to name */
    Atom            atom;
    char           *fullName, *family, *foundry;
    register char  *src, *dest;
    int             familyLen;
    char           *start;
    static char     string[200];        /* What size? */

    if (XGetFontProperty(fontPtr, XA_FULL_NAME, &atom) == False) {
        return NULL;
    }
    fullName = NameOfAtom(tkwin, atom);
    if (fullName == NULL) {
        return NULL;
    }
    family = foundry = NULL;
    if (XGetFontProperty(fontPtr, Tk_InternAtom(tkwin, "FOUNDRY"), &atom)) {
        foundry = NameOfAtom(tkwin, atom);
    }
    if (XGetFontProperty(fontPtr, XA_FAMILY_NAME, &atom)) {
        family = NameOfAtom(tkwin, atom);
    }
    /*
     * Try to map the font only if the foundry is Adobe
     */
    if ((foundry == NULL) || (family == NULL)) {
        return NULL;
    }
    src = NULL;
    familyLen = strlen(family);
    if (strncasecmp(fullName, family, familyLen) == 0) {
        src = fullName + familyLen;
    }
    if (strcmp(foundry, "Adobe") != 0) {
        register int    i;

        if (strncasecmp(family, "itc ", 4) == 0) {
            family += 4;        /* Throw out the "itc" prefix */
        }
        for (i = 0; i < nFontNames; i++) {
            if (strcasecmp(family, psFontMap[i].alias) == 0) {
                family = psFontMap[i].fontName;
            }
        }
        if (i == nFontNames) {
            family = "Helvetica";       /* Default to a known font */
        }
    }
    /*
     * PostScript font name is in the form <family>-<type face>
     */
    sprintf(string, "%s-", family);
    dest = start = string + strlen(string);

    /*
     * Append the type face (part of the full name trailing the family name)
     * to the the PostScript font name, removing any spaces or dashes
     *
     * ex. " Bold Italic" ==> "BoldItalic"
     */
    if (src != NULL) {
        while (*src != '\0') {
            if ((*src != ' ') && (*src != '-')) {
                *dest++ = *src;
            }
            src++;
        }
    }
    if (dest == start) {
        --dest;                 /* Remove '-' to leave just the family name */
    }
    *dest = '\0';               /* Make a valid string */
    return string;
}

#endif /* !_WIN32 */

/*
 * -------------------------------------------------------------------
 * Routines to convert X drawing functions to PostScript commands.
 * -------------------------------------------------------------------
 */

/*
 *--------------------------------------------------------------
 *
 * RbcClearBackgroundToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcClearBackgroundToPostScript(
    RbcPsToken * tokenPtr)
{
    RbcAppendToPostScript(tokenPtr, " 1.0 1.0 1.0 SetBgColor\n", (char *) NULL);
}

/*
 *--------------------------------------------------------------
 *
 * CapStyleToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
CapStyleToPostScript(
    RbcPsToken * tokenPtr,
    int capStyle)
{
    /*
     * X11:not last = 0, butt = 1, round = 2, projecting = 3
     * PS: butt = 0, round = 1, projecting = 2
     */
    if (capStyle > 0) {
        capStyle--;
    }
    RbcFormatToPostScript(tokenPtr, "%d setlinecap\n", capStyle);
}

/*
 *--------------------------------------------------------------
 *
 * JoinStyleToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
JoinStyleToPostScript(
    RbcPsToken * tokenPtr,
    int joinStyle)
{
    /*
     * miter = 0, round = 1, bevel = 2
     */
    RbcFormatToPostScript(tokenPtr, "%d setlinejoin\n", joinStyle);
}

/*
 *--------------------------------------------------------------
 *
 * RbcLineWidthToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcLineWidthToPostScript(
    RbcPsToken * tokenPtr,
    int lineWidth)
{
    if (lineWidth < 1) {
        lineWidth = 1;
    }
    RbcFormatToPostScript(tokenPtr, "%d setlinewidth\n", lineWidth);
}

/*
 *--------------------------------------------------------------
 *
 * RbcLineDashesToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcLineDashesToPostScript(
    RbcPsToken * tokenPtr,
    RbcDashes * dashesPtr)
{

    RbcAppendToPostScript(tokenPtr, "[ ", (char *) NULL);
    if (dashesPtr != NULL) {
        char           *p;

        for (p = dashesPtr->values; *p != 0; p++) {
            RbcFormatToPostScript(tokenPtr, " %d", *p);
        }
    }
    RbcAppendToPostScript(tokenPtr, "] 0 setdash\n", (char *) NULL);
}

/*
 *--------------------------------------------------------------
 *
 * RbcLineAttributesToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcLineAttributesToPostScript(
    RbcPsToken * tokenPtr,
    XColor * colorPtr,
    int lineWidth,
    RbcDashes * dashesPtr,
    int capStyle,
    int joinStyle)
{
    JoinStyleToPostScript(tokenPtr, joinStyle);
    CapStyleToPostScript(tokenPtr, capStyle);
    RbcForegroundToPostScript(tokenPtr, colorPtr);
    RbcLineWidthToPostScript(tokenPtr, lineWidth);
    RbcLineDashesToPostScript(tokenPtr, dashesPtr);
    RbcAppendToPostScript(tokenPtr, "/DashesProc {} def\n", (char *) NULL);
}

/*
 *--------------------------------------------------------------
 *
 * RbcRectangleToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcRectangleToPostScript(
    RbcPsToken * tokenPtr,
    double x,
    double y,
    int width,
    int height)
{
    RbcFormatToPostScript(tokenPtr,
        "%g %g %d %d Box fill\n\n", x, y, width, height);
}

/*
 *--------------------------------------------------------------
 *
 * RbcRegionToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcRegionToPostScript(
    RbcPsToken * tokenPtr,
    double x,
    double y,
    int width,
    int height)
{
    RbcFormatToPostScript(tokenPtr, "%g %g %d %d Box\n\n", x, y, width, height);
}

/*
 *--------------------------------------------------------------
 *
 * RbcPathToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcPathToPostScript(
    RbcPsToken * tokenPtr,
    register RbcPoint2D * screenPts,
    int nScreenPts)
{
    register RbcPoint2D *pointPtr, *endPtr;

    pointPtr = screenPts;
    RbcFormatToPostScript(tokenPtr, "newpath %g %g moveto\n",
        pointPtr->x, pointPtr->y);
    pointPtr++;
    endPtr = screenPts + nScreenPts;
    while (pointPtr < endPtr) {
        RbcFormatToPostScript(tokenPtr, "%g %g lineto\n",
            pointPtr->x, pointPtr->y);
        pointPtr++;
    }
}

/*
 *--------------------------------------------------------------
 *
 * RbcPolygonToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcPolygonToPostScript(
    RbcPsToken * tokenPtr,
    RbcPoint2D * screenPts,
    int nScreenPts)
{
    RbcPathToPostScript(tokenPtr, screenPts, nScreenPts);
    RbcFormatToPostScript(tokenPtr, "%g %g ", screenPts[0].x, screenPts[0].y);
    RbcAppendToPostScript(tokenPtr, " lineto closepath Fill\n", (char *) NULL);
}

/*
 *--------------------------------------------------------------
 *
 * RbcSegmentsToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcSegmentsToPostScript(
    RbcPsToken * tokenPtr,
    register XSegment * segPtr,
    int nSegments)
{
    register int    i;

    for (i = 0; i < nSegments; i++, segPtr++) {
        RbcFormatToPostScript(tokenPtr, "%d %d moveto\n",
            segPtr->x1, segPtr->y1);
        RbcFormatToPostScript(tokenPtr, " %d %d lineto\n",
            segPtr->x2, segPtr->y2);
        RbcAppendToPostScript(tokenPtr, "DashesProc stroke\n", (char *) NULL);
    }
}

/*
 *--------------------------------------------------------------
 *
 * RbcRectanglesToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcRectanglesToPostScript(
    RbcPsToken * tokenPtr,
    XRectangle rectArr[],
    int nRects)
{
    register int    i;

    for (i = 0; i < nRects; i++) {
        RbcRectangleToPostScript(tokenPtr,
            (double) rectArr[i].x, (double) rectArr[i].y,
            (int) rectArr[i].width, (int) rectArr[i].height);
    }
}

#ifndef TK_RELIEF_SOLID
#define TK_RELIEF_SOLID		-1      /* Set the an impossible value. */
#endif /* TK_RELIEF_SOLID */

/*
 *--------------------------------------------------------------
 *
 * RbcDraw3DRectangleToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcDraw3DRectangleToPostScript(
    RbcPsToken * tokenPtr,
    Tk_3DBorder border,         /* Token for border to draw. */
    double x,                   /* Coordinates of rectangle */
    double y,                   /* Coordinates of rectangle */
    int width,                  /* Region to be drawn. */
    int height,                 /* Region to be drawn. */
    int borderWidth,            /* Desired width for border, in pixels. */
    int relief)
{                               /* Should be either TK_RELIEF_RAISED or
                                 * TK_RELIEF_SUNKEN;  indicates position of
                                 * interior of window relative to exterior. */
    TkBorder       *borderPtr = (TkBorder *) border;
    XColor          lightColor, darkColor;
    XColor         *lightColorPtr, *darkColorPtr;
    XColor         *topColor, *bottomColor;
    RbcPoint2D      points[7];
    int             twiceWidth = (borderWidth * 2);

    if ((width < twiceWidth) || (height < twiceWidth)) {
        return;
    }
    if ((relief == TK_RELIEF_SOLID) || (borderPtr->lightColorPtr == NULL)
        || (borderPtr->darkColorPtr == NULL)) {
        if (relief == TK_RELIEF_SOLID) {
            darkColor.red = darkColor.blue = darkColor.green = 0x00;
            lightColor.red = lightColor.blue = lightColor.green = 0x00;
            relief = TK_RELIEF_SUNKEN;
        } else {
            Screen         *screenPtr;

            lightColor = *borderPtr->bgColorPtr;
            screenPtr = Tk_Screen(tokenPtr->tkwin);
            if (lightColor.pixel == WhitePixelOfScreen(screenPtr)) {
                darkColor.red = darkColor.blue = darkColor.green = 0x00;
            } else {
                darkColor.red = darkColor.blue = darkColor.green = 0xFF;
            }
        }
        lightColorPtr = &lightColor;
        darkColorPtr = &darkColor;
    } else {
        lightColorPtr = borderPtr->lightColorPtr;
        darkColorPtr = borderPtr->darkColorPtr;
    }

    /*
     * Handle grooves and ridges with recursive calls.
     */

    if ((relief == TK_RELIEF_GROOVE) || (relief == TK_RELIEF_RIDGE)) {
        int             halfWidth, insideOffset;

        halfWidth = borderWidth / 2;
        insideOffset = borderWidth - halfWidth;
        RbcDraw3DRectangleToPostScript(tokenPtr, border, (double) x, (double) y,
            width, height, halfWidth,
            (relief == TK_RELIEF_GROOVE) ? TK_RELIEF_SUNKEN : TK_RELIEF_RAISED);
        RbcDraw3DRectangleToPostScript(tokenPtr, border,
            (double) (x + insideOffset), (double) (y + insideOffset),
            width - insideOffset * 2, height - insideOffset * 2, halfWidth,
            (relief == TK_RELIEF_GROOVE) ? TK_RELIEF_RAISED : TK_RELIEF_SUNKEN);
        return;
    }
    if (relief == TK_RELIEF_RAISED) {
        topColor = lightColorPtr;
        bottomColor = darkColorPtr;
    } else if (relief == TK_RELIEF_SUNKEN) {
        topColor = darkColorPtr;
        bottomColor = lightColorPtr;
    } else {
        topColor = bottomColor = borderPtr->bgColorPtr;
    }
    RbcBackgroundToPostScript(tokenPtr, bottomColor);
    RbcRectangleToPostScript(tokenPtr, x, y + height - borderWidth, width,
        borderWidth);
    RbcRectangleToPostScript(tokenPtr, x + width - borderWidth, y,
        borderWidth, height);
    points[0].x = points[1].x = points[6].x = x;
    points[0].y = points[6].y = y + height;
    points[1].y = points[2].y = y;
    points[2].x = x + width;
    points[3].x = x + width - borderWidth;
    points[3].y = points[4].y = y + borderWidth;
    points[4].x = points[5].x = x + borderWidth;
    points[5].y = y + height - borderWidth;
    if (relief != TK_RELIEF_FLAT) {
        RbcBackgroundToPostScript(tokenPtr, topColor);
    }
    RbcPolygonToPostScript(tokenPtr, points, 7);
}

/*
 *--------------------------------------------------------------
 *
 * RbcFill3DRectangleToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcFill3DRectangleToPostScript(
    RbcPsToken * tokenPtr,
    Tk_3DBorder border,         /* Token for border to draw. */
    double x,                   /* Coordinates of top-left of border area */
    double y,                   /* Coordinates of top-left of border area */
    int width,                  /* Dimension of border to be drawn. */
    int height,                 /* Dimension of border to be drawn. */
    int borderWidth,            /* Desired width for border, in pixels. */
    int relief)
{                               /* Should be either TK_RELIEF_RAISED or
                                 * TK_RELIEF_SUNKEN;  indicates position of
                                 * interior of window relative to exterior. */
    TkBorder       *borderPtr = (TkBorder *) border;

    /*
     * I'm assuming that the rectangle is to be drawn as a background.
     * Setting the pen color as foreground or background only affects
     * the plot when the colormode option is "monochrome".
     */
    RbcBackgroundToPostScript(tokenPtr, borderPtr->bgColorPtr);
    RbcRectangleToPostScript(tokenPtr, x, y, width, height);
    RbcDraw3DRectangleToPostScript(tokenPtr, border, x, y, width, height,
        borderWidth, relief);
}

/*
 *--------------------------------------------------------------
 *
 * RbcStippleToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcStippleToPostScript(
    RbcPsToken * tokenPtr,
    Display * display,
    Pixmap bitmap)
{
    int             width, height;

    Tk_SizeOfBitmap(display, bitmap, &width, &height);
    RbcFormatToPostScript(tokenPtr, "gsave\n  clip\n  %d %d\n", width, height);
    RbcBitmapDataToPostScript(tokenPtr, display, bitmap, width, height);
    RbcAppendToPostScript(tokenPtr, "  StippleFill\ngrestore\n", (char *) NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcColorImageToPostScript --
 *
 *      Translates a color image into 3 component RGB PostScript output.
 *      Uses PS Language Level 2 operator "colorimage".
 *
 * Results:
 *      The dynamic string will contain the PostScript output.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcColorImageToPostScript(
    RbcPsToken * tokenPtr,
    RbcColorImage * image,
    double x,
    double y)
{
    int             width, height;
    int             tmpSize;

    width = image->width;
    height = image->height;

    tmpSize = width;
    if (tokenPtr->colorMode == PS_MODE_COLOR) {
        tmpSize *= 3;
    }
    RbcFormatToPostScript(tokenPtr, "\n/tmpStr %d string def\n", tmpSize);
    RbcAppendToPostScript(tokenPtr, "gsave\n", (char *) NULL);
    RbcFormatToPostScript(tokenPtr, "  %g %g translate\n", x, y);
    RbcFormatToPostScript(tokenPtr, "  %d %d scale\n", width, height);
    RbcFormatToPostScript(tokenPtr, "  %d %d 8\n", width, height);
    RbcFormatToPostScript(tokenPtr, "  [%d 0 0 %d 0 %d] ", width, -height,
        height);
    RbcAppendToPostScript(tokenPtr,
        "{\n    currentfile tmpStr readhexstring pop\n  } ", (char *) NULL);
    if (tokenPtr->colorMode != PS_MODE_COLOR) {
        RbcAppendToPostScript(tokenPtr, "image\n", (char *) NULL);
        RbcColorImageToGreyscale(image);
        RbcColorImageToPsData(image, 1, &(tokenPtr->dString), " ");
    } else {
        RbcAppendToPostScript(tokenPtr, "false 3 colorimage\n", (char *) NULL);
        RbcColorImageToPsData(image, 3, &(tokenPtr->dString), " ");
    }
    RbcAppendToPostScript(tokenPtr, "\ngrestore\n\n", (char *) NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * RbcWindowToPostScript --
 *
 *      Converts a Tk window to PostScript.  If the window could not
 *      be "snapped", then a grey rectangle is drawn in its place.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void
RbcWindowToPostScript(
    RbcPsToken * tokenPtr,
    Tk_Window tkwin,
    double x,
    double y)
{
    RbcColorImage  *image;
    int             width, height;

    width = Tk_Width(tkwin);
    height = Tk_Height(tkwin);
    image =
        RbcDrawableToColorImage(tkwin, Tk_WindowId(tkwin), 0, 0, width, height,
        1.0 /*gamma */ );
    if (image == NULL) {
        /* Can't grab window image so paint the window area grey */
        RbcAppendToPostScript(tokenPtr, "% Can't grab window \"",
            Tk_PathName(tkwin), "\"\n", (char *) NULL);
        RbcAppendToPostScript(tokenPtr, "0.5 0.5 0.5 SetBgColor\n",
            (char *) NULL);
        RbcRectangleToPostScript(tokenPtr, x, y, width, height);
        return;
    }
    RbcColorImageToPostScript(tokenPtr, image, x, y);
    RbcFreeColorImage(image);
}

/*
 * -------------------------------------------------------------------------
 *
 * RbcPhotoToPostScript --
 *
 *      Output a PostScript image string of the given photo image.
 *      The photo is first converted into a color image and then
 *      translated into PostScript.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The PostScript output representing the photo is appended to
 *      the tokenPtr's dynamic string.
 *
 * -------------------------------------------------------------------------
 */
void
RbcPhotoToPostScript(
    RbcPsToken * tokenPtr,
    Tk_PhotoHandle photo,
    double x,                   /* Origin of photo image */
    double y)
{                               /* Origin of photo image */
    RbcColorImage  *image;

    image = RbcPhotoToColorImage(photo);
    RbcColorImageToPostScript(tokenPtr, image, x, y);
    RbcFreeColorImage(image);
}

/*
 * -----------------------------------------------------------------
 *
 * RbcFontToPostScript --
 *
 *      Map the Tk font to a PostScript font and point size.
 *
 *      If a Tcl array variable was specified, each element should be
 *      indexed by the X11 font name and contain a list of 1-2
 *      elements; the PostScript font name and the desired point size.
 *      The point size may be omitted and the X font point size will
 *      be used.
 *
 *      Otherwise, if the foundry is "Adobe", we try to do a plausible
 *      mapping looking at the full name of the font and building a
 *      string in the form of "Family-TypeFace".
 *
 * Returns:
 *      None.
 *
 * Side Effects:
 *      PostScript commands are output to change the type and the
 *      point size of the current font.
 *
 * -----------------------------------------------------------------
 */
void
RbcFontToPostScript(
    RbcPsToken * tokenPtr,
    Tk_Font font)
{                               /* Tk font to query about */
    XFontStruct    *fontPtr = (XFontStruct *) font;
    Tcl_Interp     *interp = tokenPtr->interp;
    const char     *fontName;
    double          pointSize;
    Tk_Uid          family;
    register int    i;

    fontName = Tk_NameOfFont(font);
    pointSize = 12.0;
    /*
     * Use the font variable information if it exists.
     */
    if (tokenPtr->fontVarName != NULL) {
        char           *fontInfo;

        fontInfo = (char *) Tcl_GetVar2(interp, tokenPtr->fontVarName, fontName,
            0);
        if (fontInfo != NULL) {
            int             nProps;
            const char    **propArr = NULL;

            if (Tcl_SplitList(interp, fontInfo, &nProps, &propArr) == TCL_OK) {
                int             newSize;

                fontName = propArr[0];
                if ((nProps == 2) &&
                    (Tcl_GetInt(interp, propArr[1], &newSize) == TCL_OK)) {
                    pointSize = (double) newSize;
                }
            }
            RbcFormatToPostScript(tokenPtr,
                "%g /%s SetFont\n", pointSize, fontName);
            if (propArr != (const char **) NULL) {
                ckfree((char *) propArr);       /*TODO really delete here? */
            }
            return;
        }
    }

    /*
     * Otherwise do a quick test to see if it's a PostScript font.
     * Tk_PostScriptFontName will silently generate a bogus PostScript
     * font description, so we have to check to see if this is really a
     * PostScript font.
     */
    family = ((TkFont *) fontPtr)->fa.family;
    for (i = 0; i < nFontNames; i++) {
        if (strncasecmp(psFontMap[i].alias, family, strlen(psFontMap[i].alias))
            == 0) {
            Tcl_DString     dString;

            Tcl_DStringInit(&dString);
            pointSize = (double) Tk_PostscriptFontName(font, &dString);
            fontName = Tcl_DStringValue(&dString);
            RbcFormatToPostScript(tokenPtr, "%g /%s SetFont\n", pointSize,
                fontName);
            Tcl_DStringFree(&dString);
            return;
        }
    }

    /*
     * Can't find it. Try to use the current point size.
     */
    fontName = NULL;
    pointSize = 12.0;

#ifndef  _WIN32
    /* Can you believe what I have to go through to get an XFontStruct? */
    fontPtr = XLoadQueryFont(Tk_Display(tokenPtr->tkwin), Tk_NameOfFont(font));
    if (fontPtr != NULL) {
        unsigned long   fontProp;

        if (XGetFontProperty(fontPtr, XA_POINT_SIZE, &fontProp) != False) {
            pointSize = (double) fontProp / 10.0;
        }
        fontName = XFontStructToPostScript(tokenPtr->tkwin, fontPtr);
        XFreeFont(Tk_Display(tokenPtr->tkwin), fontPtr);
    }
#endif /* !_WIN32 */
    if ((fontName == NULL) || (fontName[0] == '\0')) {
        fontName = "Helvetica-Bold";    /* Defaulting to a known PS font */
    }
    RbcFormatToPostScript(tokenPtr, "%g /%s SetFont\n", pointSize, fontName);
}

/*
 *--------------------------------------------------------------
 *
 * TextLayoutToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void
TextLayoutToPostScript(
    RbcPsToken * tokenPtr,
    int x,
    int y,
    RbcTextLayout * textPtr)
{
    char           *src, *dst, *end;
    int             count;      /* Counts the # of bytes written to
                                 * the intermediate scratch buffer. */
    RbcTextFragment *fragPtr;
    int             i;
    unsigned char   c;
#if HAVE_UTF
    Tcl_UniChar     ch;
#endif
    int             limit;

    limit = (BUFSIZ*2) -5;     /* High water mark for the scratch
                                         * buffer. */
    fragPtr = textPtr->fragArr;
    for (i = 0; i < textPtr->nFrags; i++, fragPtr++) {
        if (fragPtr->count < 1) {
            continue;
        }
        RbcAppendToPostScript(tokenPtr, "(", (char *) NULL);
        count = 0;
        dst = tokenPtr->scratchArr;
        src = fragPtr->text;
        end = fragPtr->text + fragPtr->count;
        while (src < end) {
            if (count > limit) {
                /* Don't let the scatch buffer overflow */
                dst = tokenPtr->scratchArr;
                dst[count] = '\0';
                RbcAppendToPostScript(tokenPtr, dst, (char *) NULL);
                count = 0;
            }
#if HAVE_UTF
            /*
             * INTL: For now we just treat the characters as binary
             * data and display the lower byte.  Eventually this should
             * be revised to handle international postscript fonts.
             */
            src += Tcl_UtfToUniChar(src, &ch);
            c = (unsigned char) (ch & 0xff);
#else
            c = *src++;
#endif

            if ((c == '\\') || (c == '(') || (c == ')')) {
                /*
                 * If special PostScript characters characters "\", "(",
                 * and ")" are contained in the text string, prepend
                 * backslashes to them.
                 */
                *dst++ = '\\';
                *dst++ = c;
                count += 2;
            } else if ((c < ' ') || (c > '~')) {
                /*
                 * Present non-printable characters in their octal
                 * representation.
                 */
                sprintf(dst, "\\%03o", c);
                dst += 4;
                count += 4;
            } else {
                *dst++ = c;
                count++;
            }
        }
        tokenPtr->scratchArr[count] = '\0';
        RbcAppendToPostScript(tokenPtr, tokenPtr->scratchArr, (char *) NULL);
        RbcFormatToPostScript(tokenPtr, ") %d %d %d DrawAdjText\n",
            fragPtr->width, x + fragPtr->x, y + fragPtr->y);
    }
}

/*
 * -----------------------------------------------------------------
 *
 * RbcTextToPostScript --
 *
 *      Output PostScript commands to print a text string. The string
 *      may be rotated at any arbitrary angle, and placed according
 *      the anchor type given. The anchor indicates how to interpret
 *      the window coordinates as an anchor for the text bounding box.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Text string is drawn using the given font and GC on the graph
 *      window at the given coordinates, anchor, and rotation
 *
 * -----------------------------------------------------------------
 */
void
RbcTextToPostScript(
    RbcPsToken * tokenPtr,
    char *string,               /* String to convert to PostScript */
    RbcTextStyle * tsPtr,       /* Text attribute information */
    double x,                   /* Window coordinates where to print text */
    double y)
{                               /* Window coordinates where to print text */
    double          theta;
    double          rotWidth, rotHeight;
    RbcTextLayout  *textPtr;
    RbcPoint2D      anchorPos;

    if ((string == NULL) || (*string == '\0')) {        /* Empty string, do nothing */
        return;
    }
    theta = FMOD(tsPtr->theta, (double) 360.0);
    textPtr = RbcGetTextLayout(string, tsPtr);
    RbcGetBoundingBox(textPtr->width, textPtr->height, theta, &rotWidth,
        &rotHeight, (RbcPoint2D *) NULL);
    /*
     * Find the center of the bounding box
     */
    anchorPos.x = x, anchorPos.y = y;
    anchorPos =
        RbcTranslatePoint(&anchorPos, ROUND(rotWidth), ROUND(rotHeight),
        tsPtr->anchor);
    anchorPos.x += (rotWidth * 0.5);
    anchorPos.y += (rotHeight * 0.5);

    /* Initialize text (sets translation and rotation) */
    RbcFormatToPostScript(tokenPtr, "%d %d %g %g %g BeginText\n",
        textPtr->width, textPtr->height, tsPtr->theta, anchorPos.x,
        anchorPos.y);

    RbcFontToPostScript(tokenPtr, tsPtr->font);

    /* All coordinates are now relative to what was set by BeginText */
    if ((tsPtr->shadow.offset > 0) && (tsPtr->shadow.color != NULL)) {
        RbcForegroundToPostScript(tokenPtr, tsPtr->shadow.color);
        TextLayoutToPostScript(tokenPtr, tsPtr->shadow.offset,
            tsPtr->shadow.offset, textPtr);
    }
    RbcForegroundToPostScript(tokenPtr,
        (tsPtr->state & RBC_STATE_ACTIVE) ? tsPtr->activeColor : tsPtr->color);
    TextLayoutToPostScript(tokenPtr, 0, 0, textPtr);
    ckfree((char *) textPtr);
    RbcAppendToPostScript(tokenPtr, "EndText\n", (char *) NULL);
}

/*
 * -----------------------------------------------------------------
 *
 * RbcLineToPostScript --
 *
 *      Outputs PostScript commands to print a multi-segmented line.
 *      It assumes a procedure DashesProc was previously defined.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Segmented line is printed.
 *
 * -----------------------------------------------------------------
 */
void
RbcLineToPostScript(
    RbcPsToken * tokenPtr,
    register XPoint * pointPtr,
    int nPoints)
{
    register int    i;

    if (nPoints <= 0) {
        return;
    }
    RbcFormatToPostScript(tokenPtr, " newpath %d %d moveto\n",
        pointPtr->x, pointPtr->y);
    pointPtr++;
    for (i = 1; i < (nPoints - 1); i++, pointPtr++) {
        RbcFormatToPostScript(tokenPtr, " %d %d lineto\n",
            pointPtr->x, pointPtr->y);
        if ((i % PS_MAXPATH) == 0) {
            RbcFormatToPostScript(tokenPtr,
                "DashesProc stroke\n newpath  %d %d moveto\n",
                pointPtr->x, pointPtr->y);
        }
    }
    RbcFormatToPostScript(tokenPtr, " %d %d lineto\n",
        pointPtr->x, pointPtr->y);
    RbcAppendToPostScript(tokenPtr, "DashesProc stroke\n", (char *) NULL);
}

/*
 *--------------------------------------------------------------
 *
 * RbcBitmapToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
RbcBitmapToPostScript(
    RbcPsToken * tokenPtr,
    Display * display,
    Pixmap bitmap,              /* Bitmap to be converted to PostScript */
    double scaleX,
    double scaleY)
{
    int             width, height;
    double          scaledWidth, scaledHeight;

    Tk_SizeOfBitmap(display, bitmap, &width, &height);
    scaledWidth = (double) width *scaleX;
    scaledHeight = (double) height *scaleY;
    RbcAppendToPostScript(tokenPtr, "  gsave\n", (char *) NULL);
    RbcFormatToPostScript(tokenPtr, "    %g %g translate\n",
        scaledWidth * -0.5, scaledHeight * 0.5);
    RbcFormatToPostScript(tokenPtr, "    %g %g scale\n",
        scaledWidth, -scaledHeight);
    RbcFormatToPostScript(tokenPtr, "    %d %d true [%d 0 0 %d 0 %d] {",
        width, height, width, -height, height);
    RbcBitmapDataToPostScript(tokenPtr, display, bitmap, width, height);
    RbcAppendToPostScript(tokenPtr, "    } imagemask\n  grestore\n",
        (char *) NULL);
}

/*
 *--------------------------------------------------------------
 *
 * Rbc2DSegmentsToPostScript --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void
Rbc2DSegmentsToPostScript(
    RbcPsToken * psToken,
    register RbcSegment2D * segPtr,
    int nSegments)
{
    register RbcSegment2D *endPtr;

    for (endPtr = segPtr + nSegments; segPtr < endPtr; segPtr++) {
        RbcFormatToPostScript(psToken, "%g %g moveto\n",
            segPtr->p.x, segPtr->p.y);
        RbcFormatToPostScript(psToken, " %g %g lineto\n",
            segPtr->q.x, segPtr->q.y);
        RbcAppendToPostScript(psToken, "DashesProc stroke\n", (char *) NULL);
    }
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
