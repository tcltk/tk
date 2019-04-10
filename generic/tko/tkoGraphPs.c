/*
 * rbcGrPs.c --
 *
 *      This module implements the "postscript" operation for rbc
 *      graph widget.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkoGraph.h"

#define PS_PREVIEW_EPSI	0
#define PS_PREVIEW_WMF	1
#define PS_PREVIEW_TIFF	2

static Tk_OptionParseProc StringToColorMode;
static Tk_OptionPrintProc ColorModeToString;
static Tk_CustomOption colorModeOption = {
    StringToColorMode, ColorModeToString, (ClientData) 0,
};

static Tk_OptionParseProc StringToFormat;
static Tk_OptionPrintProc FormatToString;
static Tk_CustomOption formatOption = {
    StringToFormat, FormatToString, (ClientData) 0,
};

extern Tk_CustomOption rbcDistanceOption;
extern Tk_CustomOption rbcPositiveDistanceOption;
extern Tk_CustomOption rbcPadOption;

#define DEF_PS_CENTER		"yes"
#define DEF_PS_COLOR_MAP	(char *)NULL
#define DEF_PS_COLOR_MODE	"color"
#define DEF_PS_DECORATIONS	"yes"
#define DEF_PS_FONT_MAP		(char *)NULL
#define DEF_PS_FOOTER		"no"
#define DEF_PS_HEIGHT		"0"
#define DEF_PS_LANDSCAPE	"no"
#define DEF_PS_MAXPECT		"no"
#define DEF_PS_PADX		"1.0i"
#define DEF_PS_PADY		"1.0i"
#define DEF_PS_PAPERHEIGHT	"11.0i"
#define DEF_PS_PAPERWIDTH	"8.5i"
#define DEF_PS_PREVIEW		"no"
#define DEF_PS_PREVIEW_FORMAT   "epsi"
#define DEF_PS_WIDTH		"0"

static Tk_ConfigSpec configSpecs[] = {
    {TK_CONFIG_BOOLEAN, "-center", "center", "Center",
            DEF_PS_CENTER, Tk_Offset(RbcPostScript, center),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_STRING, "-colormap", "colorMap", "ColorMap",
            DEF_PS_COLOR_MAP, Tk_Offset(RbcPostScript, colorVarName),
        TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-colormode", "colorMode", "ColorMode",
            DEF_PS_COLOR_MODE, Tk_Offset(RbcPostScript, colorMode),
        TK_CONFIG_DONT_SET_DEFAULT, &colorModeOption},
    {TK_CONFIG_BOOLEAN, "-decorations", "decorations", "Decorations",
            DEF_PS_DECORATIONS, Tk_Offset(RbcPostScript, decorations),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_STRING, "-fontmap", "fontMap", "FontMap",
            DEF_PS_FONT_MAP, Tk_Offset(RbcPostScript, fontVarName),
        TK_CONFIG_NULL_OK},
    {TK_CONFIG_BOOLEAN, "-footer", "footer", "Footer",
            DEF_PS_FOOTER, Tk_Offset(RbcPostScript, footer),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-height", "height", "Height",
            DEF_PS_HEIGHT, Tk_Offset(RbcPostScript, reqHeight),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_BOOLEAN, "-landscape", "landscape", "Landscape",
            DEF_PS_LANDSCAPE, Tk_Offset(RbcPostScript, landscape),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-maxpect", "maxpect", "Maxpect",
            DEF_PS_MAXPECT, Tk_Offset(RbcPostScript, maxpect),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-padx", "padX", "PadX",
        DEF_PS_PADX, Tk_Offset(RbcPostScript, padX), 0, &rbcPadOption},
    {TK_CONFIG_CUSTOM, "-pady", "padY", "PadY",
        DEF_PS_PADY, Tk_Offset(RbcPostScript, padY), 0, &rbcPadOption},
    {TK_CONFIG_CUSTOM, "-paperheight", "paperHeight", "PaperHeight",
            DEF_PS_PAPERHEIGHT, Tk_Offset(RbcPostScript, reqPaperHeight),
        0, &rbcPositiveDistanceOption},
    {TK_CONFIG_CUSTOM, "-paperwidth", "paperWidth", "PaperWidth",
            DEF_PS_PAPERWIDTH, Tk_Offset(RbcPostScript, reqPaperWidth),
        0, &rbcPositiveDistanceOption},
    {TK_CONFIG_BOOLEAN, "-preview", "preview", "Preview",
            DEF_PS_PREVIEW, Tk_Offset(RbcPostScript, addPreview),
        TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-previewformat", "previewFormat", "PreviewFormat",
            DEF_PS_PREVIEW_FORMAT, Tk_Offset(RbcPostScript, previewFormat),
        TK_CONFIG_DONT_SET_DEFAULT, &formatOption},
    {TK_CONFIG_CUSTOM, "-width", "width", "Width",
            DEF_PS_WIDTH, Tk_Offset(RbcPostScript, reqWidth),
        TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}
};

/* TODO: These do not belong here */
/*
extern void     RbcMarkersToPostScript(
    RbcGraph * graph,
    RbcPsToken * psToken,
    int under);
extern void     RbcElementsToPostScript(
    RbcGraph * graph,
    RbcPsToken * psToken);
extern void     RbcActiveElementsToPostScript(
    RbcGraph * graph,
    RbcPsToken * psToken);
extern void     RbcLegendToPostScript(
    RbcLegend * legendPtr,
    RbcPsToken * psToken);
extern void     RbcGridToPostScript(
    RbcGraph * graph,
    RbcPsToken * psToken);
extern void     RbcAxesToPostScript(
    RbcGraph * graph,
    RbcPsToken * psToken);
extern void     RbcAxisLimitsToPostScript(
    RbcGraph * graph,
    RbcPsToken * psToken);
*/
static const char *NameOfColorMode(
    RbcPsColorMode colorMode);
static int CgetOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char *argv[]);
static int ConfigureOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int OutputOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char **argv);
static int ComputeBoundingBox(
    RbcGraph * graph,
    RbcPostScript * psPtr);
static void PreviewImage(
    RbcGraph * graph,
    RbcPsToken * psToken);
static int PostScriptPreamble(
    RbcGraph * graph,
    const char *fileName,
    RbcPsToken * psToken);
static void MarginsToPostScript(
    RbcGraph * graph,
    RbcPsToken * psToken);
static int GraphToPostScript(
    RbcGraph * graph,
    const char *ident,
    RbcPsToken * psToken);

#ifdef _WIN32
static int CreateWindowsEPS(
    RbcGraph * graph,
    RbcPsToken * psToken,
    FILE * f);
#endif

/*
 *----------------------------------------------------------------------
 *
 * StringToColorMode --
 *
 *      Convert the string representation of a PostScript color mode
 *      into the enumerated type representing the color level:
 *
 *          PS_MODE_COLOR 	- Full color
 *          PS_MODE_GREYSCALE  	- Color converted to greyscale
 *          PS_MODE_MONOCHROME 	- Only black and white
 *
 * Results:
 *      A standard Tcl result.  The color level is written into the
 *      page layout information structure.
 *
 * Side Effects:
 *      Future invocations of the "postscript" option will use this
 *      variable to determine how color information will be displayed
 *      in the PostScript output it produces.
 *
 *----------------------------------------------------------------------
 */
static int
StringToColorMode(
    ClientData clientData,     /* Not used. */
    Tcl_Interp * interp,       /* Interpreter to send results back to */
    Tk_Window tkwin,           /* Not used. */
    const char *string,        /* New value. */
    char *widgRec,             /* Widget record */
    int offset)
{              /* Offset of field in record */
    RbcPsColorMode *modePtr = (RbcPsColorMode *) (widgRec + offset);
    unsigned int length;
    char c;

    c = string[0];
    length = strlen(string);
    if((c == 'c') && (strncmp(string, "color", length) == 0)) {
        *modePtr = PS_MODE_COLOR;
    } else if((c == 'g') && (strncmp(string, "grayscale", length) == 0)) {
        *modePtr = PS_MODE_GREYSCALE;
    } else if((c == 'g') && (strncmp(string, "greyscale", length) == 0)) {
        *modePtr = PS_MODE_GREYSCALE;
    } else if((c == 'm') && (strncmp(string, "monochrome", length) == 0)) {
        *modePtr = PS_MODE_MONOCHROME;
    } else {
        Tcl_AppendResult(interp, "bad color mode \"", string, "\": should be \
\"color\", \"greyscale\", or \"monochrome\"", (char *)NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * NameOfColorMode --
 *
 *      Convert the PostScript mode value into the string representing
 *      a valid color mode.
 *
 * Results:
 *      The static string representing the color mode is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
NameOfColorMode(
    RbcPsColorMode colorMode)
{
    switch (colorMode) {
    case PS_MODE_COLOR:
        return "color";
    case PS_MODE_GREYSCALE:
        return "greyscale";
    case PS_MODE_MONOCHROME:
        return "monochrome";
    default:
        return "unknown color mode";
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ColorModeToString --
 *
 *      Convert the current color mode into the string representing a
 *      valid color mode.
 *
 * Results:
 *      The string representing the color mode is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
ColorModeToString(
    ClientData clientData,     /* Not used. */
    Tk_Window tkwin,           /* Not used. */
    char *widgRec,             /* Widget record. */
    int offset,                /* field of colorMode in record */
    Tcl_FreeProc ** freeProcPtr)
{              /* Not used. */
    RbcPsColorMode mode = *(RbcPsColorMode *) (widgRec + offset);

    return NameOfColorMode(mode);
}

/*
 *----------------------------------------------------------------------
 *
 * StringToFormat --
 *
 *      Convert the string of the PostScript preview format into
 *      an enumerated type representing the desired format.  The
 *      available formats are:
 *
 *        PS_PREVIEW_WMF 	- Windows Metafile.
 *        PS_PREVIEW_TIFF  	- TIFF bitmap image.
 *        PS_PREVIEW_EPSI 	- Device independent ASCII preview
 *
 * Results:
 *      A standard Tcl result.  The format is written into the
 *      page layout information structure.
 *
 * Side Effects:
 *      Future invocations of the "postscript" option will use this
 *      variable to determine how to format a preview image (if one
 *      is selected) when the PostScript output is produced.
 *
 *----------------------------------------------------------------------
 */
static int
StringToFormat(
    ClientData clientData,     /* Not used. */
    Tcl_Interp * interp,       /* Interpreter to send results back to */
    Tk_Window tkwin,           /* Not used. */
    const char *string,        /* New value. */
    char *widgRec,             /* Widget record */
    int offset)
{              /* Offset of field in record */
    int *formatPtr = (int *)(widgRec + offset);
    unsigned int length;
    char c;

    c = string[0];
    length = strlen(string);
    if((c == 'c') && (strncmp(string, "epsi", length) == 0)) {
        *formatPtr = PS_PREVIEW_EPSI;
#ifdef _WIN32
#ifdef HAVE_TIFF_H
    } else if((c == 't') && (strncmp(string, "tiff", length) == 0)) {
        *formatPtr = PS_PREVIEW_TIFF;
#endif /* HAVE_TIFF_H */
    } else if((c == 'w') && (strncmp(string, "wmf", length) == 0)) {
        *formatPtr = PS_PREVIEW_WMF;
#endif /* _WIN32 */
    } else {
        Tcl_AppendResult(interp, "bad format \"", string, "\": should be ",
#ifdef _WIN32
#ifdef HAVE_TIFF_H
            "\"tiff\" or ",
#endif /* HAVE_TIFF_H */
            "\"wmf\" or ",
#endif /* _WIN32 */
            "\"epsi\"", (char *)NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * FormatToString --
 *
 *      Convert the preview format into the string representing its
 *      type.
 *
 * Results:
 *      The string representing the preview format is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *
FormatToString(
    ClientData clientData,     /* Not used. */
    Tk_Window tkwin,           /* Not used. */
    char *widgRec,             /* PostScript structure record */
    int offset,                /* field of colorMode in record */
    Tcl_FreeProc ** freeProcPtr)
{              /* Not used. */
    int format = *(int *)(widgRec + offset);

    switch (format) {
    case PS_PREVIEW_EPSI:
        return "epsi";
    case PS_PREVIEW_WMF:
        return "wmf";
    case PS_PREVIEW_TIFF:
        return "tiff";
    }
    return "?unknown preview format?";
}

/*
 *--------------------------------------------------------------
 *
 * RbcDestroyPostScript --
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
RbcDestroyPostScript(
    RbcGraph * graph)
{
    Tk_FreeOptions(configSpecs, (char *)graph->postscript, graph->display, 0);
    ckfree((char *)graph->postscript);
}

/*
 *--------------------------------------------------------------
 *
 * CgetOp --
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
static int
CgetOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,
    const char *argv[])
{
    RbcPostScript *psPtr = (RbcPostScript *) graph->postscript;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    if(Tk_ConfigureValue(interp, *(graph->win), configSpecs, (char *)psPtr,
            argv[3], 0) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      This procedure is invoked to print the graph in a file.
 *
 * Results:
 *      A standard TCL result.
 *
 * Side effects:
 *      A new PostScript file is created.
 *
 * ----------------------------------------------------------------------
 */
static int
ConfigureOp(
    RbcGraph * graph,
    Tcl_Interp * interp,
    int argc,                  /* Number of options in argv vector */
    const char **argv)
{              /* Option vector */
    int flags = TK_CONFIG_ARGV_ONLY;
    RbcPostScript *psPtr = (RbcPostScript *) graph->postscript;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    if(argc == 3) {
        return Tk_ConfigureInfo(interp, *(graph->win), configSpecs,
            (char *)psPtr, (char *)NULL, flags);
    } else if(argc == 4) {
        return Tk_ConfigureInfo(interp, *(graph->win), configSpecs,
            (char *)psPtr, argv[3], flags);
    }
    if(Tk_ConfigureWidget(interp, *(graph->win), configSpecs, argc - 3,
            argv + 3, (char *)psPtr, flags) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * ComputeBoundingBox --
 *
 *      Computes the bounding box for the PostScript plot.  First get
 *      the size of the plot (by default, it's the size of graph's X
 *      window).  If the plot plus the page border is bigger than the
 *      designated paper size, or if the "-maxpect" option is turned
 *      on, scale the plot to the page.
 *
 *      Note: All coordinates/sizes are in screen coordinates, not
 *            PostScript coordinates.  This includes the computed
 *            bounding box and paper size.  They will be scaled to
 *            printer points later.
 *
 * Results:
 *      Returns the height of the paper in screen coordinates.
 *
 * Side Effects:
 *      The graph dimensions (width and height) are changed to the
 *      requested PostScript plot size.
 *
 * --------------------------------------------------------------------------
 */
static int
ComputeBoundingBox(
    RbcGraph * graph,
    RbcPostScript * psPtr)
{
int paperWidth, paperHeight;
int x, y, hSize, vSize, hBorder, vBorder;
double hScale, vScale, scale;

    x = psPtr->padX.side1;      /*left */
    y = psPtr->padY.side1;      /*top */
    hBorder = RbcPadding(psPtr->padX);
    vBorder = RbcPadding(psPtr->padY);

    if(psPtr->reqWidth > 0) {
        graph->width = psPtr->reqWidth;
    }
    if(psPtr->reqHeight > 0) {
        graph->height = psPtr->reqHeight;
    }
    if(psPtr->landscape) {
        hSize = graph->height;
        vSize = graph->width;
    } else {
        hSize = graph->width;
        vSize = graph->height;
    }
    /*
     * If the paper size wasn't specified, set it to the graph size plus
     * the paper border.
     */
    paperWidth = psPtr->reqPaperWidth;
    paperHeight = psPtr->reqPaperHeight;
    if(paperWidth < 1) {
        paperWidth = hSize + hBorder;
    }
    if(paperHeight < 1) {
        paperHeight = vSize + vBorder;
    }
    hScale = vScale = 1.0;
    /*
     * Scale the plot size (the graph itself doesn't change size) if
     * it's bigger than the paper or if -maxpect was set.
     */
    if((psPtr->maxpect) || ((hSize + hBorder) > paperWidth)) {
        hScale = (double)(paperWidth - hBorder) / (double)hSize;
    }
    if((psPtr->maxpect) || ((vSize + vBorder) > paperHeight)) {
        vScale = (double)(paperHeight - vBorder) / (double)vSize;
    }
    scale = MIN(hScale, vScale);
    if(scale != 1.0) {
        hSize = (int)((hSize * scale) + 0.5);
        vSize = (int)((vSize * scale) + 0.5);
    }
    psPtr->pageScale = scale;
    if(psPtr->center) {
        if(paperWidth > hSize) {
            x = (paperWidth - hSize) / 2;
        }
        if(paperHeight > vSize) {
            y = (paperHeight - vSize) / 2;
        }
    }
    psPtr->left = x;
    psPtr->bottom = y;
    psPtr->right = x + hSize - 1;
    psPtr->top = y + vSize - 1;

    graph->flags |= RBC_LAYOUT_NEEDED | RBC_MAP_WORLD;
    RbcLayoutGraph(graph);
    return paperHeight;
}

/*
 * --------------------------------------------------------------------------
 *
 * PreviewImage --
 *
 *      Generates a EPSI thumbnail of the graph.  The thumbnail is
 *      restricted to a certain size.  This is to keep the size of the
 *      PostScript file small and the processing time low.
 *
 *      The graph is drawn into a pixmap.  We then take a snapshot
 *      of that pixmap, and rescale it to a smaller image.  Finally,
 *       the image is dumped to PostScript.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * --------------------------------------------------------------------------
 */
static void
PreviewImage(
    RbcGraph * graph,
    RbcPsToken * psToken)
{
RbcPostScript *psPtr = (RbcPostScript *) graph->postscript;
int noBackingStore = 0;
Pixmap drawable;
RbcColorImage *image;
int nLines;
Tcl_DString dString;
    if(graph->win == NULL || *(graph->win) == NULL)
        return;

    /* Create a pixmap and draw the graph into it. */

    drawable = Tk_GetPixmap(graph->display, Tk_WindowId(*(graph->win)),
        graph->width, graph->height, Tk_Depth(*(graph->win)));
    RbcDrawGraph(graph, drawable, noBackingStore);

    /* Get a color image from the pixmap */
    image =
        RbcDrawableToColorImage(*(graph->win), drawable, 0, 0,
        graph->width, graph->height, 1.0);
    Tk_FreePixmap(graph->display, drawable);
    if(image == NULL) {
        return; /* Can't grab pixmap? */
    }
#ifdef THUMBNAIL_PREVIEW
    {
double scale, xScale, yScale;
int width, height;
RbcColorImage *destImage;

        /* Scale the source image into a size appropriate for a thumbnail. */
#define PS_MAX_PREVIEW_WIDTH	300.0
#define PS_MAX_PREVIEW_HEIGHT	300.0
        xScale = PS_MAX_PREVIEW_WIDTH / (double)graph->width;
        yScale = PS_MAX_PREVIEW_HEIGHT / (double)graph->height;
        scale = MIN(xScale, yScale);

        width = (int)(scale * graph->width + 0.5);
        height = (int)(scale * graph->height + 0.5);
        destImage = RbcResampleColorImage(image, width, height,
            rbcBoxFilterPtr, rbcBoxFilterPtr);
        RbcFreeColorImage(image);
        image = destImage;
    }
#endif /* THUMBNAIL_PREVIEW */
    RbcColorImageToGreyscale(image);
    if(psPtr->landscape) {
RbcColorImage *oldImage;

        oldImage = image;
        image = RbcRotateColorImage(image, 90.0);
        RbcFreeColorImage(oldImage);
    }
    Tcl_DStringInit(&dString);
    /* Finally, we can generate PostScript for the image */
    nLines = RbcColorImageToPsData(image, 1, &dString, "%");

    RbcAppendToPostScript(psToken, "%%BeginPreview: ", (char *)NULL);
    RbcFormatToPostScript(psToken, "%d %d 8 %d\n", image->width,
        image->height, nLines);
    RbcAppendToPostScript(psToken, Tcl_DStringValue(&dString), (char *)NULL);
    RbcAppendToPostScript(psToken, "%%EndPreview\n\n", (char *)NULL);
    Tcl_DStringFree(&dString);
    RbcFreeColorImage(image);
}

#ifdef TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif /* HAVE_SYS_TIME_H */
#endif /* TIME_WITH_SYS_TIME */

/*
 *--------------------------------------------------------------
 *
 * PostScriptPreamble --
 *
 *      The PostScript preamble calculates the needed
 *      translation and scaling to make X11 coordinates
 *      compatible with PostScript.
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int
PostScriptPreamble(
    RbcGraph * graph,
    const char *fileName,
    RbcPsToken * psToken)
{
    RbcPostScript *psPtr = (RbcPostScript *) graph->postscript;
    time_t ticks;
    char date[200];            /* Hold the date string from ctime() */
    const char *version;
    double dpiX, dpiY;
    double xPixelsToPica, yPixelsToPica;        /* Scales to convert pixels to pica */
    Screen *screenPtr;
    char *nl;
    int paperHeightPixels;
    Tcl_Obj *preambleObj;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    paperHeightPixels = ComputeBoundingBox(graph, psPtr);
    if(fileName == NULL) {
        fileName = Tk_PathName(*(graph->win));
    }
    RbcAppendToPostScript(psToken, "%!PS-Adobe-3.0 EPSF-3.0\n", (char *)NULL);

    /*
     * Compute the scale factors to convert PostScript to X11 coordinates.
     * Round the pixels per inch (dpi) to an integral value before computing
     * the scale.
     */
#define MM_INCH		25.4
#define PICA_INCH	72.0
    screenPtr = Tk_Screen(*(graph->win));
    dpiX = (WidthOfScreen(screenPtr) * MM_INCH) / WidthMMOfScreen(screenPtr);
    xPixelsToPica = PICA_INCH / dpiX;
    dpiY = (HeightOfScreen(screenPtr) * MM_INCH) / HeightMMOfScreen(screenPtr);
    yPixelsToPica = PICA_INCH / dpiY;

    /*
     * The "BoundingBox" comment is required for EPS files. The box
     * coordinates are integers, so we need round away from the
     * center of the box.
     */
    RbcFormatToPostScript(psToken, "%%%%BoundingBox: %d %d %d %d\n",
        (int)floor(psPtr->left * xPixelsToPica),
        (int)floor((paperHeightPixels - psPtr->top) * yPixelsToPica),
        (int)ceil(psPtr->right * xPixelsToPica),
        (int)ceil((paperHeightPixels - psPtr->bottom) * yPixelsToPica));

    RbcAppendToPostScript(psToken, "%%Pages: 0\n", (char *)NULL);

    version = Tcl_GetVar(graph->interp, "rbc_version", TCL_GLOBAL_ONLY);
    if(version == NULL) {
        version = "???";
    }
    RbcFormatToPostScript(psToken, "%%%%Creator: (Rbc %s %s)\n", version,
        Tk_Class(*(graph->win)));

    ticks = time((time_t *) NULL);
    strcpy(date, ctime(&ticks));
    nl = date + strlen(date) - 1;
    if(*nl == '\n') {
        *nl = '\0';
    }
    RbcFormatToPostScript(psToken, "%%%%CreationDate: (%s)\n", date);
    RbcFormatToPostScript(psToken, "%%%%Title: (%s)\n", fileName);
    RbcAppendToPostScript(psToken, "%%DocumentData: Clean7Bit\n", (char *)NULL);
    if(psPtr->landscape) {
        RbcAppendToPostScript(psToken, "%%Orientation: Landscape\n",
            (char *)NULL);
    } else {
        RbcAppendToPostScript(psToken, "%%Orientation: Portrait\n",
            (char *)NULL);
    }
    RbcAppendToPostScript(psToken,
        "%%DocumentNeededResources: font Helvetica Courier\n", (char *)NULL);
    RbcAppendToPostScript(psToken, "%%EndComments\n\n", (char *)NULL);
    if((psPtr->addPreview) && (psPtr->previewFormat == PS_PREVIEW_EPSI)) {
        PreviewImage(graph, psToken);
    }
    preambleObj = Tcl_GetVar2Ex(graph->interp, "::graph::ps_preamble", NULL,
        TCL_LEAVE_ERR_MSG);
    if(preambleObj == NULL) {
        return TCL_ERROR;
    }
    RbcAppendToPostScript(psToken, Tcl_GetString(preambleObj), (char *)NULL);
    if(psPtr->footer) {
    const char *who;

        who = getenv("LOGNAME");
        if(who == NULL) {
            who = "???";
        }
        RbcAppendToPostScript(psToken,
            "8 /Helvetica SetFont\n",
            "10 30 moveto\n",
            "(Date: ", date, ") show\n",
            "10 20 moveto\n",
            "(File: ", fileName, ") show\n",
            "10 10 moveto\n",
            "(Created by: ", who, "@", Tcl_GetHostName(), ") show\n",
            "0 0 moveto\n", (char *)NULL);
    }
    /*
     * Set the conversion from PostScript to X11 coordinates.  Scale
     * pica to pixels and flip the y-axis (the origin is the upperleft
     * corner).
     */
    RbcAppendToPostScript(psToken,
        "% Transform coordinate system to use X11 coordinates\n\n",
        "% 1. Flip y-axis over by reversing the scale,\n",
        "% 2. Translate the origin to the other side of the page,\n",
        "%    making the origin the upper left corner\n", (char *)NULL);
    RbcFormatToPostScript(psToken, "%g -%g scale\n", xPixelsToPica,
        yPixelsToPica);
    /* Papersize is in pixels.  Translate the new origin *after*
     * changing the scale. */
    RbcFormatToPostScript(psToken, "0 %d translate\n\n", -paperHeightPixels);
    RbcAppendToPostScript(psToken, "% User defined page layout\n\n",
        "% Set color level\n", (char *)NULL);
    RbcFormatToPostScript(psToken, "/CL %d def\n\n", psPtr->colorMode);
    RbcFormatToPostScript(psToken, "%% Set origin\n%d %d translate\n\n",
        psPtr->left, psPtr->bottom);
    if(psPtr->landscape) {
        RbcFormatToPostScript(psToken,
            "%% Landscape orientation\n0 %g translate\n-90 rotate\n",
            ((double)graph->width * psPtr->pageScale));
    }
    if(psPtr->pageScale != 1.0) {
        RbcAppendToPostScript(psToken, "\n% Setting graph scale factor\n",
            (char *)NULL);
        RbcFormatToPostScript(psToken, " %g %g scale\n", psPtr->pageScale,
            psPtr->pageScale);
    }
    RbcAppendToPostScript(psToken, "\n%%EndSetup\n\n", (char *)NULL);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * MarginsToPostScript --
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
MarginsToPostScript(
    RbcGraph * graph,
    RbcPsToken * psToken)
{
RbcPostScript *psPtr = (RbcPostScript *) graph->postscript;
XRectangle margin[4];

    margin[0].x = margin[0].y = margin[3].x = margin[1].x = 0;
    margin[0].width = margin[3].width = graph->width;
    margin[0].height = graph->top;
    margin[3].y = graph->bottom;
    margin[3].height = graph->height - graph->bottom;
    margin[2].y = margin[1].y = graph->top;
    margin[1].width = graph->left;
    margin[2].height = margin[1].height = graph->bottom - graph->top;
    margin[2].x = graph->right;
    margin[2].width = graph->width - graph->right;

    /* Clear the surrounding margins and clip the plotting surface */
    if(psPtr->decorations) {
        RbcBackgroundToPostScript(psToken, Tk_3DBorderColor(graph->border));
    } else {
        RbcClearBackgroundToPostScript(psToken);
    }
    RbcRectanglesToPostScript(psToken, margin, 4);

    /* Interior 3D border */
    if((psPtr->decorations) && (graph->plotBorderWidth > 0)) {
int x, y, width, height;

        x = graph->left - graph->plotBorderWidth;
        y = graph->top - graph->plotBorderWidth;
        width = (graph->right - graph->left) + (2 * graph->plotBorderWidth);
        height = (graph->bottom - graph->top) + (2 * graph->plotBorderWidth);
        RbcDraw3DRectangleToPostScript(psToken, graph->border,
            (double)x, (double)y, width, height, graph->plotBorderWidth,
            graph->plotRelief);
    }
    if(RbcLegendSite(graph->legend) & RBC_LEGEND_IN_MARGIN) {
        /*
         * Print the legend if we're using a site which lies in one
         * of the margins (left, right, top, or bottom) of the graph.
         */
        RbcLegendToPostScript(graph->legend, psToken);
    }
    if(graph->title != NULL) {
        RbcTextToPostScript(psToken, graph->title,
            &graph->titleTextStyle, (double)graph->titleX,
            (double)graph->titleY);
    }
    RbcAxesToPostScript(graph, psToken);
}

/*
 *--------------------------------------------------------------
 *
 * GraphToPostScript --
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
static int
GraphToPostScript(
    RbcGraph * graph,
    const char *ident,         /* Identifier string (usually the filename) */
    RbcPsToken * psToken)
{
    int x, y, width, height;
    int result;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    /*
     * We need to know how big a graph to print.  If the graph hasn't
     * been drawn yet, the width and height will be 1.  Instead use
     * the requested size of the widget.  The user can still override
     * this with the -width and -height postscript options.
     */
    if(graph->height <= 1) {
        graph->height = Tk_ReqHeight(*(graph->win));
    }
    if(graph->width <= 1) {
        graph->width = Tk_ReqWidth(*(graph->win));
    }
    result = PostScriptPreamble(graph, ident, psToken);
    if(result != TCL_OK) {
        goto error;
    }
    /*
     * Determine rectangle of the plotting area for the graph window
     */
    x = graph->left - graph->plotBorderWidth;
    y = graph->top - graph->plotBorderWidth;

    width = (graph->right - graph->left + 1) + (2 * graph->plotBorderWidth);
    height = (graph->bottom - graph->top + 1) + (2 * graph->plotBorderWidth);

    RbcFontToPostScript(psToken, graph->titleTextStyle.font);
    RbcRegionToPostScript(psToken, (double)x, (double)y, width, height);
    if(graph->postscript->decorations) {
        RbcBackgroundToPostScript(psToken, graph->plotBg);
    } else {
        RbcClearBackgroundToPostScript(psToken);
    }
    RbcAppendToPostScript(psToken, "Fill\n", (char *)NULL);
    RbcAppendToPostScript(psToken, "gsave clip\n\n", (char *)NULL);
    /* Draw the grid, elements, and markers in the plotting area. */
    if(!graph->gridPtr->hidden) {
        RbcGridToPostScript(graph, psToken);
    }
    RbcMarkersToPostScript(graph, psToken, TRUE);
    if((RbcLegendSite(graph->legend) & RBC_LEGEND_IN_PLOT) &&
        (!RbcLegendIsRaised(graph->legend))) {
        /* Print legend underneath elements and markers */
        RbcLegendToPostScript(graph->legend, psToken);
    }
    RbcAxisLimitsToPostScript(graph, psToken);
    RbcElementsToPostScript(graph, psToken);
    if((RbcLegendSite(graph->legend) & RBC_LEGEND_IN_PLOT) &&
        (RbcLegendIsRaised(graph->legend))) {
        /* Print legend above elements (but not markers) */
        RbcLegendToPostScript(graph->legend, psToken);
    }
    RbcMarkersToPostScript(graph, psToken, FALSE);
    RbcActiveElementsToPostScript(graph, psToken);
    RbcAppendToPostScript(psToken, "\n",
        "% Unset clipping\n", "grestore\n\n", (char *)NULL);
    MarginsToPostScript(graph, psToken);
    RbcAppendToPostScript(psToken,
        "showpage\n",
        "%Trailer\n", "grestore\n", "end\n", "%EOF\n", (char *)NULL);
  error:
    /* Reset height and width of graph window */
    graph->width = Tk_Width(*(graph->win));
    graph->height = Tk_Height(*(graph->win));
    graph->flags = RBC_MAP_WORLD;

    /*
     * Redraw the graph in order to re-calculate the layout as soon as
     * possible. This is in the case the crosshairs are active.
     */
    RbcEventuallyRedrawGraph(graph);
    return result;
}

#ifdef _WIN32

/*
 * TODO: Determine if neccessary
 *
 *static void
 *InitAPMHeader(tkwin, width, height, headerPtr)
 *    Tk_Window tkwin;
 *    int width, height;
 *    APMHEADER *headerPtr;
 *{
 *    unsigned int *p;
 *    unsigned int sum;
 *    Screen *screen;
 *#define MM_INCH		25.4
 *    double dpiX, dpiY;
 *
 *    headerPtr->key = 0x9ac6cdd7L;
 *    headerPtr->hmf = 0;
 *    headerPtr->inch = 1440;
 *
 *    screen = Tk_Screen(tkwin);
 *    dpiX = (WidthOfScreen(screen) * MM_INCH) / WidthMMOfScreen(screen);
 *    dpiY = (HeightOfScreen(screen) * MM_INCH) / HeightMMOfScreen(screen);
 *
 *    headerPtr->bbox.Left = headerPtr->bbox.Top = 0;
 *    headerPtr->bbox.Bottom = (SHORT)((width * 1440) / dpiX);
 *    headerPtr->bbox.Right = (SHORT)((height * 1440) / dpiY);
 *    headerPtr->reserved = 0;
 *    sum = 0;
 *    for (p = (unsigned int *)headerPtr;
 *            p < (unsigned int *)&(headerPtr->checksum); p++) {
 *        sum ^= *p;
 *    }
 *    headerPtr->checksum = sum;
 *}
 */

/*
 * --------------------------------------------------------------------------
 *
 * CreateWindowEPS --
 *
 *      Generates an EPS file with a Window metafile preview.
 *
 *      Windows metafiles aren't very robust.  Including exactly the
 *      same metafile (one embedded in a DOS EPS, the other as .wmf
 *      file) will play back differently.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * --------------------------------------------------------------------------
 */
static int
CreateWindowsEPS(
    RbcGraph * graph,
    RbcPsToken * psToken,
    FILE * f)
{
DWORD size;
DOSEPSHEADER epsHeader;
HANDLE hMem;
HDC hRefDC, hDC;
HENHMETAFILE hMetaFile;
Tcl_DString dString;
TkWinDC drawableDC;
TkWinDCState state;
int result;
unsigned char *buffer;
char *psBuffer;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    RbcAppendToPostScript(psToken, "\n", (char *)NULL);
    psBuffer = RbcPostScriptFromToken(psToken);
    /*
     * Fill out as much information as we can into the DOS EPS header.
     * We won't know the start of the length of the WMF segment until
     * we create the metafile.
     */
    epsHeader.magic[0] = 0xC5;
    epsHeader.magic[1] = 0xD0;
    epsHeader.magic[2] = 0xD3;
    epsHeader.magic[3] = 0xC6;
    epsHeader.psStart = sizeof(epsHeader);
    epsHeader.psLength = strlen(psBuffer) + 1;
    epsHeader.wmfStart = epsHeader.psStart + epsHeader.psLength;
    epsHeader.wmfLength = 0L;   /* Fill in later. */
    epsHeader.tiffStart = 0L;
    epsHeader.tiffLength = 0L;
    epsHeader.checksum = 0xFFFF;

    result = TCL_ERROR;
    hRefDC = TkWinGetDrawableDC(graph->display,
        Tk_WindowId(*(graph->win)), &state);

    /* Build a description string. */
    Tcl_DStringInit(&dString);
    Tcl_DStringAppend(&dString, "Rbc Graph ", -1);
    Tcl_DStringAppend(&dString, "\0", -1);
    Tcl_DStringAppend(&dString, Tk_PathName(*(graph->win)), -1);
    Tcl_DStringAppend(&dString, "\0", -1);

    hDC = CreateEnhMetaFileA(hRefDC, NULL, NULL, Tcl_DStringValue(&dString));
    Tcl_DStringFree(&dString);

    if(hDC == NULL) {
        Tcl_AppendResult(graph->interp, "can't create metafile: ",
            RbcLastError(), (char *)NULL);
        return TCL_ERROR;
    }
    /* Assemble a Tk drawable that points to the metafile and let the
     * graph's drawing routine draw into it. */
    drawableDC.hdc = hDC;
    drawableDC.type = TWD_WINDC;

    graph->width = Tk_Width(*(graph->win));
    graph->height = Tk_Height(*(graph->win));
    graph->flags |= RBC_RESET_WORLD;
    RbcLayoutGraph(graph);
    RbcDrawGraph(graph, (Drawable) & drawableDC, FALSE);
    GdiFlush();
    hMetaFile = CloseEnhMetaFile(hDC);

    size = GetWinMetaFileBits(hMetaFile, 0, NULL, MM_ANISOTROPIC, hRefDC);
    hMem = GlobalAlloc(GHND, size);
    if(hMem == NULL) {
        Tcl_AppendResult(graph->interp, "can't allocate global memory:",
            RbcLastError(), (char *)NULL);
        goto error;
    }
    buffer = (LPVOID) GlobalLock(hMem);
    if(!GetWinMetaFileBits(hMetaFile, size, buffer, MM_ANISOTROPIC, hRefDC)) {
        Tcl_AppendResult(graph->interp, "can't get metafile data:",
            RbcLastError(), (char *)NULL);
        goto error;
    }

    /*
     * Fix up the EPS header with the correct metafile length and PS
     * offset (now that we what they are).
     */
    epsHeader.wmfLength = size;
    epsHeader.wmfStart = epsHeader.psStart + epsHeader.psLength;

    /* Write out the eps header, */
    if(fwrite(&epsHeader, 1, sizeof(epsHeader), f) != sizeof(epsHeader)) {
        Tcl_AppendResult(graph->interp, "error writing eps header:",
            RbcLastError(), (char *)NULL);
        goto error;
    }
    /* the PostScript, */
    if(fwrite(psBuffer, 1, epsHeader.psLength, f) != epsHeader.psLength) {
        Tcl_AppendResult(graph->interp, "error writing PostScript data:",
            RbcLastError(), (char *)NULL);
        goto error;
    }
    /* and finally the metadata itself. */
    if(fwrite(buffer, 1, size, f) != size) {
        Tcl_AppendResult(graph->interp, "error writing metafile data:",
            RbcLastError(), (char *)NULL);
        goto error;
    }
    result = TCL_OK;

  error:
    DeleteEnhMetaFile(hMetaFile);
    TkWinReleaseDrawableDC(Tk_WindowId(*(graph->win)), hRefDC, &state);
    fclose(f);
    if(hMem != NULL) {
        GlobalUnlock(hMem);
        GlobalFree(hMem);
    }
    graph->flags = RBC_MAP_WORLD;
    RbcEventuallyRedrawGraph(graph);
    return result;
}

#endif /*_WIN32*/

/*
 *----------------------------------------------------------------------
 *
 * OutputOp --
 *
 *      This procedure is invoked to print the graph in a file.
 *
 * Results:
 *      Standard TCL result.  TCL_OK if plot was successfully printed,
 *      TCL_ERROR otherwise.
 *
 * Side effects:
 *      A new PostScript file is created.
 *
 *----------------------------------------------------------------------
 */
static int
OutputOp(
    RbcGraph * graph,          /* Graph widget record */
    Tcl_Interp * interp,
    int argc,                  /* Number of options in argv vector */
    const char **argv)
{              /* Option vector */
    RbcPostScript *psPtr = (RbcPostScript *) graph->postscript;
    FILE *f = NULL;
    RbcPsToken *psToken;
    const char *fileName;      /* Name of file to write PostScript output
                                * If NULL, output is returned via
                                * interp->result. */
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    fileName = NULL;
    if(argc > 3) {
        if(argv[3][0] != '-') {
            fileName = argv[3]; /* First argument is the file name. */
            argv++, argc--;
        }
        if(Tk_ConfigureWidget(interp, *(graph->win), configSpecs, argc - 3,
                argv + 3, (char *)psPtr, TK_CONFIG_ARGV_ONLY) != TCL_OK) {
            return TCL_ERROR;
        }
        if(fileName != NULL) {
#ifdef _WIN32
            f = fopen(fileName, "wb");
#else
            f = fopen(fileName, "w");
#endif
            if(f == NULL) {
                Tcl_AppendResult(interp, "can't create \"", fileName, "\": ",
                    Tcl_PosixError(interp), (char *)NULL);
                return TCL_ERROR;
            }
        }
    }
    psToken = RbcGetPsToken(graph->interp, *(graph->win));
    psToken->fontVarName = psPtr->fontVarName;
    psToken->colorVarName = psPtr->colorVarName;
    psToken->colorMode = psPtr->colorMode;

    if(GraphToPostScript(graph, fileName, psToken) != TCL_OK) {
        goto error;
    }
    /*
     * If a file name was given, write the results to that file
     */
    if(f != NULL) {
#ifdef _WIN32
        if((psPtr->addPreview) && (psPtr->previewFormat != PS_PREVIEW_EPSI)) {
            if(CreateWindowsEPS(graph, psToken, f)) {
                return TCL_ERROR;
            }
        } else {
            fputs(RbcPostScriptFromToken(psToken), f);
            if(ferror(f)) {
                Tcl_AppendResult(interp, "error writing file \"", fileName,
                    "\": ", Tcl_PosixError(interp), (char *)NULL);
                goto error;
            }
        }
#else
        fputs(RbcPostScriptFromToken(psToken), f);
        if(ferror(f)) {
            Tcl_AppendResult(interp, "error writing file \"", fileName, "\": ",
                Tcl_PosixError(interp), (char *)NULL);
            goto error;
        }
#endif /* _WIN32 */
        fclose(f);
    } else {
        Tcl_SetObjResult(interp,
            Tcl_NewStringObj(RbcPostScriptFromToken(psToken), -1));
    }
    RbcReleasePsToken(psToken);
    return TCL_OK;

  error:
    if(f != NULL) {
        fclose(f);
    }
    RbcReleasePsToken(psToken);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcCreatePostScript --
 *
 *      Creates a postscript structure.
 *
 * Results:
 *      Always TCL_OK.
 *
 * Side effects:
 *      A new PostScript structure is created.
 *
 *----------------------------------------------------------------------
 */
int
RbcCreatePostScript(
    RbcGraph * graph)
{
RbcPostScript *psPtr;
    if(graph->win == NULL || *(graph->win) == NULL)
        return TCL_ERROR;

    psPtr = RbcCalloc(1, sizeof(RbcPostScript));
    assert(psPtr);
    psPtr->colorMode = PS_MODE_COLOR;
    psPtr->center = TRUE;
    psPtr->decorations = TRUE;
    graph->postscript = psPtr;

    if(RbcConfigureWidgetComponent(graph->interp, *(graph->win),
            "postscript", "Postscript", configSpecs, 0, (const char **)NULL,
            (char *)psPtr, 0) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * RbcPostScriptOp --
 *
 *      This procedure is invoked to process the Tcl command
 *      that corresponds to a widget managed by this module.
 *      See the user documentation for details on what it does.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 *--------------------------------------------------------------
 */
static RbcOpSpec psOps[] = {
    {"cget", 2, (RbcOp) CgetOp, 4, 4, "option",},
    {"configure", 2, (RbcOp) ConfigureOp, 3, 0, "?option value?...",},
    {"output", 1, (RbcOp) OutputOp, 3, 0,
        "?fileName? ?option value?...",},
};

static int nPsOps = sizeof(psOps) / sizeof(RbcOpSpec);

/*
 *--------------------------------------------------------------
 *
 * RbcPostScriptOp --
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
int
RbcPostScriptOp(
    RbcGraph * graph,          /* Graph widget record */
    Tcl_Interp * interp,
    int argc,                  /* # arguments */
    const char *argv[])
{              /* Argument list */
    RbcOp proc;
    int result;

    proc = RbcGetOp(interp, nPsOps, psOps, RBC_OP_ARG2, argc, argv, 0);
    if(proc == NULL) {
        return TCL_ERROR;
    }
    result = (*proc) (graph, interp, argc, argv);
    return result;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
