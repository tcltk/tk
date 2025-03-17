/*
 * tkUnixPrint.c --
 *
 *      tkUnixPrint.c implements a "::tk::print::cups" Tcl command which
 *      interfaces the libcups2 API with the [tk print] command.
 *
 * Copyright © 2024 Emiliano Gavilán.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkUnixInt.h"

#ifdef HAVE_CUPS
#include <cups/cups.h>

typedef int (CupsSubCmdOp)(Tcl_Interp *, int, Tcl_Obj *const []);

static Tcl_ObjCmdProc2 Cups_Cmd;
static CupsSubCmdOp DefaultPrinterOp;
static CupsSubCmdOp GetPrintersOp;
static CupsSubCmdOp PrintOp;
static Tcl_ArgvGenFuncProc ParseEnumOptions;
static Tcl_ArgvGenFuncProc ParseOptions;
static Tcl_ArgvGenFuncProc ParseMargins;
static Tcl_ArgvGenFuncProc ParseNup;
static cups_dest_t* GetPrinterFromObj(Tcl_Obj *);

static cups_dest_t *
GetPrinterFromObj(Tcl_Obj *nameObj)
{
    cups_dest_t *printer;
    Tcl_Size len;
    const char *nameStr = Tcl_GetStringFromObj(nameObj, &len);
    char *p;
    char *name, *instance = NULL;
    Tcl_DString ds;

    Tcl_DStringInit(&ds);
    name = Tcl_DStringAppend(&ds, nameStr, len);
    p = strchr(name, '/');
    if (p) {
	*p = '\0';
	instance = p+1;
    }

    printer = cupsGetNamedDest(CUPS_HTTP_DEFAULT, name, instance);
    Tcl_DStringFree(&ds);

    return printer;
}

static int
Cups_Cmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    static const struct CupsCmds {
	const char *subcmd;
	CupsSubCmdOp *subCmd;
    } cupsCmds[] = {
	{"defaultprinter"   , DefaultPrinterOp},
	{"getprinters"      , GetPrintersOp},
	{"print"            , PrintOp},
	{NULL, NULL}
    };
    int index;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "subcommand ?arg ...?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[1], cupsCmds,
	    sizeof(struct CupsCmds), "subcommand", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }

    return cupsCmds[index].subCmd(interp, objc, objv);
}

static int
DefaultPrinterOp(
    Tcl_Interp *interp,
    TCL_UNUSED(int),
    TCL_UNUSED(Tcl_Obj *const *))
{
    cups_dest_t *printer;
    Tcl_Obj *resultObj;

    printer = cupsGetNamedDest(CUPS_HTTP_DEFAULT, NULL, NULL);
    if (printer) {
	if (printer->instance) {
	    resultObj = Tcl_ObjPrintf("%s/%s", printer->name,
		printer->instance);
	} else {
	    resultObj = Tcl_NewStringObj(printer->name, -1);
	}
	Tcl_SetObjResult(interp, resultObj);
    }

    cupsFreeDests(1, printer);
    return TCL_OK;
}

static int
GetPrintersOp(
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    cups_dest_t *dests;
    cups_option_t *option;
    int num_dests, i, j;
    Tcl_Obj *keyPtr, *optPtr, *resultObj;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 2, objv, NULL);
	return TCL_ERROR;
    }

    num_dests = cupsGetDests2(CUPS_HTTP_DEFAULT, &dests);
    resultObj = Tcl_NewObj();

    for (i = 0; i < num_dests; i++) {
	if (dests[i].instance)
	    keyPtr = Tcl_ObjPrintf("%s/%s", dests[i].name, dests[i].instance);
	else
	    keyPtr = Tcl_NewStringObj(dests[i].name, -1);

	option = dests[i].options;
	optPtr = Tcl_NewObj();
	for(j = 0; j < dests[i].num_options; j++) {
	    Tcl_DictObjPut(NULL, optPtr,
		Tcl_NewStringObj(option[j].name, -1),
		Tcl_NewStringObj(option[j].value, -1));
	}

	Tcl_DictObjPut(NULL, resultObj, keyPtr, optPtr);
    }

    cupsFreeDests(num_dests, dests);
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

/* Information needed for parsing */
struct CupsOptions {
    const char *name;
    const char *cupsName;
};

static const struct CupsOptions colormodeOpts[] = {
    {"auto",       CUPS_PRINT_COLOR_MODE_AUTO},
    {"color",      CUPS_PRINT_COLOR_MODE_COLOR},
    {"monochrome", CUPS_PRINT_COLOR_MODE_MONOCHROME},
    {NULL, NULL}
};

static const struct CupsOptions formatOpts[] = {
    {"auto",       CUPS_FORMAT_AUTO},
    {"pdf",        CUPS_FORMAT_PDF},
    {"postscript", CUPS_FORMAT_POSTSCRIPT},
    {"text",       CUPS_FORMAT_TEXT},
    {NULL, NULL}
};

static const struct CupsOptions mediaOpts[] = {
    {"a4",     CUPS_MEDIA_A4},
    {"legal",  CUPS_MEDIA_LEGAL},
    {"letter", CUPS_MEDIA_LETTER},
    {NULL, NULL}
};

static const struct CupsOptions orientationOpts[] = {
    {"portrait",  CUPS_ORIENTATION_PORTRAIT},
    {"landscape", CUPS_ORIENTATION_LANDSCAPE},
    {NULL, NULL}
};

enum {PARSECOLORMODE, PARSEFORMAT, PARSEMEDIA, PARSEORIENTATION};

static const struct ParseData {
    const char *message;
    const struct CupsOptions *optionTable;
} parseData[] = {
    {"colormode",   colormodeOpts},
    {"format",      formatOpts},
    {"media",       mediaOpts},
    {"orientation", orientationOpts},
    {NULL, NULL}
};

static int
PrintOp(
    Tcl_Interp *interp,
    int objc,
    Tcl_Obj *const objv[])
{
    cups_dest_t *printer;
    cups_dinfo_t *info;
    int result = TCL_OK;
    int job_id;

    /* variables for Tcl_ParseArgsObjv */
    Tcl_Obj *const *parseObjv;
    Tcl_Size count;

    /* options related vaiables */
    cups_option_t *options = NULL;
    int num_options = 0;
    int copies = 0, pprint = 0;
    const char *media = NULL, *color = NULL, *orient = NULL, *format = NULL,
	*nup = NULL, *title = NULL;
    Tcl_Obj *marginsObj = NULL, *optionsObj = NULL;
    double tzoom = 1.0;

    /* Data to print
     * this is a binary buffer, since it can contain data such as
     * jpg or compressed pdf which might contain any bytes.
     * USE [encoding convertto] with a proper encoding when passing
     * text data to print.
     */
    const unsigned char *buffer; Tcl_Size buflen;

    const Tcl_ArgvInfo argTable[] = {
	{TCL_ARGV_GENFUNC,  "-colormode",   ParseEnumOptions, &color,
	    "color mode", (void *)&parseData[PARSECOLORMODE]},
	{TCL_ARGV_INT   ,   "-copies",                  NULL, &copies,
	    "number of copies", NULL},
	{TCL_ARGV_GENFUNC,  "-format",      ParseEnumOptions, &format,
	    "data format", (void *)&parseData[PARSEFORMAT]},
	{TCL_ARGV_GENFUNC,  "-margins",         ParseMargins, &marginsObj,
	    "media page size", NULL},
	{TCL_ARGV_GENFUNC,  "-media",       ParseEnumOptions, &media,
	    "media page size", (void *)&parseData[PARSEMEDIA]},
	{TCL_ARGV_GENFUNC,  "-nup",                 ParseNup, &nup,
	    "pages per sheet", NULL},
	{TCL_ARGV_GENFUNC,  "-options",         ParseOptions, &optionsObj,
	    "generic options", NULL},
	{TCL_ARGV_GENFUNC,  "-orientation", ParseEnumOptions, &orient,
	    "page orientation", (void *)&parseData[PARSEORIENTATION]},
	{TCL_ARGV_CONSTANT, "-prettyprint",        (void *)1, &pprint,
	    "print header", NULL},
	{TCL_ARGV_STRING,   "-title",                   NULL, &title,
	    "job title", NULL},
	{TCL_ARGV_FLOAT,    "-tzoom",                   NULL, &tzoom,
	    "text zoom", NULL},
	TCL_ARGV_TABLE_END
    };

    if (objc < 4) {
	Tcl_WrongNumArgs(interp, 2, objv, "printer data ?-opt arg ...?");
	return TCL_ERROR;
    }

    printer = GetPrinterFromObj(objv[2]);
    if (!printer) {
	Tcl_SetObjResult(interp,
	    Tcl_ObjPrintf("unknown printer or class \"%s\"",
		Tcl_GetString(objv[2])));
	return TCL_ERROR;
    }

    /* T_PAO discards the first arg, but we have 4 before the options */
    parseObjv = objv+3;
    count = objc-3;

    if (Tcl_ParseArgsObjv(interp, argTable, &count, parseObjv, NULL)!=TCL_OK) {
	return TCL_ERROR;
    }

    if (copies < 0 || copies > 100) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("copies must be an integer"
	    "between 0 and 100", -1));
	cupsFreeDests(1, printer);
	return TCL_ERROR;
    }
    if (tzoom < 0.5 || tzoom > 2.0) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("tzoom must be a number"
	    "between 0.5 and 2.0", -1));
	cupsFreeDests(1, printer);
	return TCL_ERROR;
    }

/*  Add options */
    if (copies != 0) {
	char copiesbuf[4];

	snprintf(copiesbuf, 4, "%d", copies);
	num_options = cupsAddOption(CUPS_COPIES, copiesbuf,
	    num_options, &options);
    }
    if (color) {
	num_options = cupsAddOption(CUPS_PRINT_COLOR_MODE, color,
	    num_options, &options);
    }
    if (media) {
	num_options = cupsAddOption(CUPS_MEDIA, media,
	    num_options, &options);
    }
    if (nup) {
	num_options = cupsAddOption(CUPS_NUMBER_UP, nup,
	    num_options, &options);
    }
    if (orient) {
	num_options = cupsAddOption(CUPS_ORIENTATION, orient,
	    num_options, &options);
    }
    if (pprint) {
	num_options = cupsAddOption("prettyprint", "yes",
	    num_options, &options);
    }
    if (marginsObj) {
	Tcl_Size n;
	Tcl_Obj **listArr;

	Tcl_ListObjGetElements(NULL, marginsObj, &n, &listArr);
	num_options = cupsAddOption("page-top",    Tcl_GetString(listArr[0]),
	    num_options, &options);
	num_options = cupsAddOption("page-left",   Tcl_GetString(listArr[1]),
	    num_options, &options);
	num_options = cupsAddOption("page-bottom", Tcl_GetString(listArr[2]),
	    num_options, &options);
	num_options = cupsAddOption("page-right",  Tcl_GetString(listArr[3]),
	    num_options, &options);
    }
    if (optionsObj) {
	Tcl_DictSearch search;
	int done = 0;
	Tcl_Obj *key, *value;

	for (Tcl_DictObjFirst(interp, optionsObj, &search, &key, &value, &done)
	    ; !done ; Tcl_DictObjNext(&search, &key, &value, &done))
	{
	    num_options = cupsAddOption(Tcl_GetString(key),
		Tcl_GetString(value), num_options, &options);
	}
    }
    /* prettyprint mess with the default values if set, so we force it */
    if (tzoom != 1.0 || pprint) {
	char cpibuf[TCL_DOUBLE_SPACE + 1];
	char lpibuf[TCL_DOUBLE_SPACE + 1];

	Tcl_PrintDouble(interp, 10.0 / tzoom, cpibuf);
	Tcl_PrintDouble(interp,  6.0 / tzoom, lpibuf);
	num_options = cupsAddOption("cpi", cpibuf,
	    num_options, &options);
	num_options = cupsAddOption("lpi", lpibuf,
	    num_options, &options);
    }

    /* set title and format */
    if (!title) {
	title = "Tk print job";
    }
    if (!format) {
	format = CUPS_FORMAT_AUTO;
    }

    info = cupsCopyDestInfo(CUPS_HTTP_DEFAULT, printer);

    if (cupsCreateDestJob(CUPS_HTTP_DEFAULT, printer, info, &job_id,
	    title, num_options, options) != IPP_STATUS_OK) {

	Tcl_SetObjResult(interp, Tcl_ObjPrintf("Error creating job: \"%s\"",
	    cupsLastErrorString()));
	result = TCL_ERROR;
	goto cleanup;
    }

    buffer = Tcl_GetByteArrayFromObj(objv[3], &buflen);

    if (cupsStartDestDocument(CUPS_HTTP_DEFAULT, printer, info, job_id,
	"(stdin)", format, 0, NULL, 1) != HTTP_STATUS_CONTINUE) {
	// Can't start document
	Tcl_SetObjResult(interp, Tcl_ObjPrintf("Error starting document: \"%s\"",
	    cupsLastErrorString()));
	result = TCL_ERROR;
	goto cleanup;
    }

    if (cupsWriteRequestData(CUPS_HTTP_DEFAULT,(char *) buffer, buflen) !=
	    HTTP_STATUS_CONTINUE) {
	// some error ocurred
	Tcl_SetObjResult(interp, Tcl_ObjPrintf("Error writing data: \"%s\"",
	    cupsLastErrorString()));
	result = TCL_ERROR;
	goto cleanup;
    }

    if (cupsFinishDestDocument(CUPS_HTTP_DEFAULT, printer, info) ==
	    IPP_STATUS_OK) {
	// all OK
	Tcl_SetObjResult(interp, Tcl_NewIntObj(job_id));
    } else {
	// some error ocurred
	Tcl_SetObjResult(interp, Tcl_ObjPrintf("Error finishing document: \"%s\"",
	    cupsLastErrorString()));
	result = TCL_ERROR;
	goto cleanup;
    }

cleanup:
    cupsFreeDestInfo(info);
    cupsFreeOptions(num_options, options);
    cupsFreeDests(1, printer);
    return result;
}

static Tcl_Size
ParseEnumOptions(
    void *clientData,
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size),
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    int index;
    const char **dest = (const char **) dstPtr;
    struct ParseData *pdata = (struct ParseData *)clientData;

    if (Tcl_GetIndexFromObjStruct(interp, objv[0], pdata->optionTable,
	    sizeof(struct CupsOptions), pdata->message, 0, &index) != TCL_OK) {
	return -1;
    }

    *dest = pdata->optionTable[index].cupsName;
    return 1;
}

static Tcl_Size
ParseOptions(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size),
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    Tcl_Obj **objPtr = (Tcl_Obj **) dstPtr;
    Tcl_Size n;

    /* check for a valid dictionary */
    if (Tcl_DictObjSize(NULL, objv[0], &n) != TCL_OK) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("options must be a proper"
	    "dictionary", -1));
	return -1;
    }

    *objPtr = objv[0];
    return 1;
}

static Tcl_Size
ParseMargins(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size),
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    Tcl_Obj **objPtr = (Tcl_Obj **) dstPtr;
    Tcl_Obj **listArr;
    Tcl_Size n;
    int i;

    if (Tcl_ListObjGetElements(NULL, objv[0], &n, &listArr) != TCL_OK ||
	n != 4 ||
	Tcl_GetIntFromObj(NULL, listArr[0], &i) != TCL_OK ||
	Tcl_GetIntFromObj(NULL, listArr[1], &i) != TCL_OK ||
	Tcl_GetIntFromObj(NULL, listArr[2], &i) != TCL_OK ||
	Tcl_GetIntFromObj(NULL, listArr[3], &i) != TCL_OK
    ) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("margins must be a list "
	    "of four integers: top left bottom right" , -1));
	return -1;
    }

    *objPtr = objv[0];
    return 1;
}

static Tcl_Size
ParseNup(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    TCL_UNUSED(Tcl_Size),
    Tcl_Obj *const *objv,
    void *dstPtr)
{
    const char **nup = (const char **) dstPtr;
    int n;

    if (Tcl_GetIntFromObj(NULL, objv[0], &n) != TCL_OK ||
	(n != 1 && n != 2 && n != 4 && n != 6 && n != 9 && n != 16)
    ) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj("wrong number-up value: "
	    "should be 1, 2, 4, 6, 9 or 16", -1));
	return -1;
    }

    *nup = Tcl_GetString(objv[0]);
    return 1;
}
#endif /*HAVE_CUPS*/

int
#ifdef HAVE_CUPS
Cups_Init(Tcl_Interp *interp)
{
    Tcl_Namespace *ns;
    ns = Tcl_FindNamespace(interp, "::tk::print", NULL, TCL_GLOBAL_ONLY);
    if (!ns)
	ns = Tcl_CreateNamespace(interp, "::tk::print", NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::print::cups", Cups_Cmd, NULL, NULL);
    Tcl_Export(interp, ns, "cups", 0);
#else
Cups_Init(TCL_UNUSED(Tcl_Interp *))
{
    /* Do nothing */
#endif
    return TCL_OK;
}
