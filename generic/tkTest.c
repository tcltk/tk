/*
 * tkTest.c --
 *
 *	This file contains C command functions for a bunch of additional Tcl
 *	commands that are used for testing out Tcl's C interfaces. These
 *	commands are not normally included in Tcl applications; they're only
 *	used for testing.
 *
 * Copyright © 1993-1994 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 * Copyright © 1998-1999 Scriptics Corporation.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#undef STATIC_BUILD
#ifndef USE_TCL_STUBS
#   define USE_TCL_STUBS
#endif
#ifndef USE_TK_STUBS
#   define USE_TK_STUBS
#endif
#include "tkInt.h"
#include "tkText.h"

#ifdef _WIN32
#include "tkWinInt.h"
#endif

#if defined(MAC_OSX_TK)
#include "tkMacOSXInt.h"
#include "tkScrollbar.h"
#define LOG_DISPLAY(drawable) TkTestLogDisplay(drawable)
#else
#define LOG_DISPLAY(drawable) 1
#endif

#ifdef __UNIX__
#include "tkUnixInt.h"
#endif

/*
 * TCL_STORAGE_CLASS is set unconditionally to DLLEXPORT because the
 * Tcltest_Init declaration is in the source file itself, which is only
 * accessed when we are building a library.
 */

#undef TCL_STORAGE_CLASS
#define TCL_STORAGE_CLASS DLLEXPORT
#ifdef __cplusplus
extern "C" {
#endif
EXTERN int		Tktest_Init(Tcl_Interp *interp);
#ifdef __cplusplus
}
#endif

#if TCL_MAJOR_VERSION < 9
#   undef Tcl_CreateObjCommand2
#   define Tcl_CreateObjCommand2 Tcl_CreateObjCommand
#endif
/*
 * The following data structure represents the model for a test image:
 */

typedef struct TImageModel {
    Tk_ImageModel model;	/* Tk's token for image model. */
    Tcl_Interp *interp;		/* Interpreter for application. */
    int width, height;		/* Dimensions of image. */
    char *imageName;		/* Name of image (malloc-ed). */
    char *varName;		/* Name of variable in which to log events for
				 * image (malloc-ed). */
} TImageModel;

/*
 * The following data structure represents a particular use of a particular
 * test image.
 */

typedef struct TImageInstance {
    TImageModel *modelPtr;	/* Pointer to model for image. */
    XColor *fg;			/* Foreground color for drawing in image. */
    GC gc;			/* Graphics context for drawing in image. */
    Bool displayFailed;         /* macOS display attempted out of drawRect. */
    char buffer[200 + TCL_INTEGER_SPACE * 6]; /* message to log on display. */
} TImageInstance;

/*
 * The type record for test images:
 */

static int		ImageCreate(Tcl_Interp *interp,
			    const char *name, Tcl_Size objc, Tcl_Obj *const objv[],
			    const Tk_ImageType *typePtr, Tk_ImageModel model,
			    void **clientDataPtr);
static void	*ImageGet(Tk_Window tkwin, void *clientData);
static void		ImageDisplay(void *clientData,
			    Display *display, Drawable drawable,
			    int imageX, int imageY, int width,
			    int height, int drawableX,
			    int drawableY);
static void		ImageFree(void *clientData, Display *display);
static void		ImageDelete(void *clientData);

static Tk_ImageType imageType = {
    "test",			/* name */
    ImageCreate,		/* createProc */
    ImageGet,			/* getProc */
    ImageDisplay,		/* displayProc */
    ImageFree,			/* freeProc */
    ImageDelete,		/* deleteProc */
    NULL,			/* postscriptPtr */
    NULL,			/* nextPtr */
    NULL
};

/*
 * One of the following structures describes each of the interpreters created
 * by the "testnewapp" command. This information is used by the
 * "testdeleteinterps" command to destroy all of those interpreters.
 */

typedef struct NewApp {
    Tcl_Interp *interp;		/* Token for interpreter. */
    struct NewApp *nextPtr;	/* Next in list of new interpreters. */
} NewApp;

static NewApp *newAppPtr = NULL;/* First in list of all new interpreters. */

/*
 * Header for trivial configuration command items.
 */

#define ODD	TK_CONFIG_USER_BIT
#define EVEN	(TK_CONFIG_USER_BIT << 1)

enum {
    NONE,
    ODD_TYPE,
    EVEN_TYPE
};

typedef struct TrivialCommandHeader {
    Tcl_Interp *interp;		/* The interp that this command lives in. */
    Tk_OptionTable optionTable;	/* The option table that go with this
				 * command. */
    Tk_Window tkwin;		/* For widgets, the window associated with
				 * this widget. */
    Tcl_Command widgetCmd;	/* For widgets, the command associated with
				 * this widget. */
} TrivialCommandHeader;

/*
 * Forward declarations for functions defined later in this file:
 */

static Tcl_ObjCmdProc2 ImageObjCmd;
static Tcl_ObjCmdProc2 TestbitmapObjCmd;
static Tcl_ObjCmdProc2 TestborderObjCmd;
static Tcl_ObjCmdProc2 TestcolorObjCmd;
static Tcl_ObjCmdProc2 TestcursorObjCmd;
static Tcl_ObjCmdProc2 TestdeleteappsObjCmd;
static Tcl_ObjCmdProc2 TestfontObjCmd;
static Tcl_ObjCmdProc2 TestmakeexistObjCmd;
#if !(defined(_WIN32) || defined(MAC_OSX_TK) || defined(__CYGWIN__))
static Tcl_ObjCmdProc2 TestmenubarObjCmd;
#endif
#if defined(_WIN32)
static Tcl_ObjCmdProc2 TestmetricsObjCmd;
#endif
static Tcl_ObjCmdProc2 TestobjconfigObjCmd;
static Tk_CustomOptionSetProc CustomOptionSet;
static Tk_CustomOptionGetProc CustomOptionGet;
static Tk_CustomOptionRestoreProc CustomOptionRestore;
static Tk_CustomOptionFreeProc CustomOptionFree;
static Tcl_ObjCmdProc2 TestpropObjCmd;
static Tcl_ObjCmdProc2 TestprintfObjCmd;
#if !(defined(_WIN32) || defined(MAC_OSX_TK) || defined(__CYGWIN__))
static Tcl_ObjCmdProc2 TestwrapperObjCmd;
#endif
static void		TrivialCmdDeletedProc(void *clientData);
static Tcl_ObjCmdProc2 TrivialConfigObjCmd;
static void		TrivialEventProc(void *clientData,
			    XEvent *eventPtr);
static Tcl_ObjCmdProc2 TestPhotoStringMatchCmd;

/*
 *----------------------------------------------------------------------
 *
 * Tktest_Init --
 *
 *	This function performs initialization for the Tk test suite extensions.
 *
 * Results:
 *	Returns a standard Tcl completion code, and leaves an error message in
 *	the interp's result if an error occurs.
 *
 * Side effects:
 *	Creates several test commands.
 *
 *----------------------------------------------------------------------
 */

int
Tktest_Init(
    Tcl_Interp *interp)		/* Interpreter for application. */
{
    static int initialized = 0;

    if (Tcl_InitStubs(interp, "8.7-", 0) == NULL) {
	return TCL_ERROR;
    }
    if (Tk_InitStubs(interp, TK_VERSION, 0) == NULL) {
	return TCL_ERROR;
    }

    /*
     * Create additional commands for testing Tk.
     */

    if (Tcl_PkgProvideEx(interp, "tk::test", TK_PATCH_LEVEL, NULL) == TCL_ERROR) {
	return TCL_ERROR;
    }

    Tcl_CreateObjCommand2(interp, "square", SquareObjCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "testbitmap", TestbitmapObjCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testborder", TestborderObjCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testcolor", TestcolorObjCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testcursor", TestcursorObjCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testdeleteapps", TestdeleteappsObjCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testembed", TkpTestembedCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testobjconfig", TestobjconfigObjCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testfont", TestfontObjCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testmakeexist", TestmakeexistObjCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testprop", TestpropObjCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testprintf", TestprintfObjCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "testtext", TkpTesttextCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testphotostringmatch",
            TestPhotoStringMatchCmd, Tk_MainWindow(interp),
            NULL);

#if defined(_WIN32)
    Tcl_CreateObjCommand2(interp, "testmetrics", TestmetricsObjCmd,
	    Tk_MainWindow(interp), NULL);
#elif !defined(__CYGWIN__) && !defined(MAC_OSX_TK)
    Tcl_CreateObjCommand2(interp, "testmenubar", TestmenubarObjCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testsend", TkpTestsendCmd,
	    Tk_MainWindow(interp), NULL);
    Tcl_CreateObjCommand2(interp, "testwrapper", TestwrapperObjCmd,
	    Tk_MainWindow(interp), NULL);
#endif /* _WIN32 */

    /*
     * Create test image type.
     */

    if (!initialized) {
	initialized = 1;
	Tk_CreateImageType(&imageType);
    }

    /*
     * And finally add any platform specific test commands.
     */

    return TkplatformtestInit(interp);
}

/*
 *----------------------------------------------------------------------
 *
 * TestbitmapObjCmd --
 *
 *	This function implements the "testbitmap" command, which is used to
 *	test color resource handling in tkBitmap tmp.c.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TestbitmapObjCmd(
    TCL_UNUSED(void *),	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "bitmap");
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, TkDebugBitmap(Tk_MainWindow(interp),
	    Tcl_GetString(objv[1])));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TestborderObjCmd --
 *
 *	This function implements the "testborder" command, which is used to
 *	test color resource handling in tkBorder.c.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TestborderObjCmd(
    TCL_UNUSED(void *),	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "border");
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, TkDebugBorder(Tk_MainWindow(interp),
	    Tcl_GetString(objv[1])));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TestcolorObjCmd --
 *
 *	This function implements the "testcolor" command, which is used to
 *	test color resource handling in tkColor.c.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TestcolorObjCmd(
    TCL_UNUSED(void *),	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "color");
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, TkDebugColor(Tk_MainWindow(interp),
	    Tcl_GetString(objv[1])));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TestcursorObjCmd --
 *
 *	This function implements the "testcursor" command, which is used to
 *	test color resource handling in tkCursor.c.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TestcursorObjCmd(
    TCL_UNUSED(void *),	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "cursor");
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, TkDebugCursor(Tk_MainWindow(interp),
	    Tcl_GetString(objv[1])));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TestdeleteappsObjCmd --
 *
 *	This function implements the "testdeleteapps" command. It cleans up
 *	all the interpreters left behind by the "testnewapp" command.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	All the interpreters created by previous calls to "testnewapp" get
 *	deleted.
 *
 *----------------------------------------------------------------------
 */

static int
TestdeleteappsObjCmd(
    TCL_UNUSED(void *),	/* Main window for application. */
    TCL_UNUSED(Tcl_Interp *),		/* Current interpreter. */
    TCL_UNUSED(Tcl_Size),			/* Number of arguments. */
    TCL_UNUSED(Tcl_Obj *const *))		/* Argument strings. */
{
    NewApp *nextPtr;

    while (newAppPtr != NULL) {
	nextPtr = newAppPtr->nextPtr;
	Tcl_DeleteInterp(newAppPtr->interp);
	ckfree(newAppPtr);
	newAppPtr = nextPtr;
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TestobjconfigObjCmd --
 *
 *	This function implements the "testobjconfig" command, which is used to
 *	test the functions in tkConfig.c.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TestobjconfigObjCmd(
    void *clientData,	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    static const char *const options[] = {
	"alltypes", "chain1", "chain2", "chain3", "configerror", "delete", "info",
	"internal", "new", "notenoughparams", "twowindows", NULL
    };
    enum {
	ALL_TYPES, CHAIN1, CHAIN2, CHAIN3, CONFIG_ERROR,
	DEL,			/* Can't use DELETE: VC++ compiler barfs. */
	INFO, INTERNAL, NEW, NOT_ENOUGH_PARAMS, TWO_WINDOWS
    };
    static Tk_OptionTable tables[11];
				/* Holds pointers to option tables created by
				 * commands below; indexed with same values as
				 * "options" array. */
    static const Tk_ObjCustomOption CustomOption = {
	"custom option",
	CustomOptionSet,
	CustomOptionGet,
	CustomOptionRestore,
	CustomOptionFree,
	INT2PTR(1)
    };
    Tk_Window mainWin = (Tk_Window)clientData;
    Tk_Window tkwin;
    int index, result = TCL_OK;

    /*
     * Structures used by the "chain1" subcommand and also shared by the
     * "chain2" subcommand:
     */

    typedef struct {
	TrivialCommandHeader header;
	Tcl_Obj *base1ObjPtr;
	Tcl_Obj *base2ObjPtr;
	Tcl_Obj *extension3ObjPtr;
	Tcl_Obj *extension4ObjPtr;
	Tcl_Obj *extension5ObjPtr;
    } ExtensionWidgetRecord;
    static const Tk_OptionSpec baseSpecs[] = {
	{TK_OPTION_STRING, "-one", "one", "One", "one",
		offsetof(ExtensionWidgetRecord, base1ObjPtr), TCL_INDEX_NONE, 0, NULL, 0},
	{TK_OPTION_STRING, "-two", "two", "Two", "two",
		offsetof(ExtensionWidgetRecord, base2ObjPtr), TCL_INDEX_NONE, 0, NULL, 0},
	{TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}
    };

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "command");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[1], options,
	    sizeof(char *), "command", 0, &index)!= TCL_OK) {
	return TCL_ERROR;
    }

    switch (index) {
    case ALL_TYPES: {
	typedef struct {
	    TrivialCommandHeader header;
	    Tcl_Obj *booleanPtr;
	    Tcl_Obj *integerPtr;
	    Tcl_Obj *doublePtr;
	    Tcl_Obj *stringPtr;
	    Tcl_Obj *stringTablePtr;
	    Tcl_Obj *stringTablePtr2;
	    Tcl_Obj *colorPtr;
	    Tcl_Obj *fontPtr;
	    Tcl_Obj *bitmapPtr;
	    Tcl_Obj *borderPtr;
	    Tcl_Obj *reliefPtr;
	    Tcl_Obj *cursorPtr;
	    Tcl_Obj *activeCursorPtr;
	    Tcl_Obj *justifyPtr;
	    Tcl_Obj *anchorPtr;
	    Tcl_Obj *pixelPtr;
	    Tcl_Obj *mmPtr;
	    Tcl_Obj *customPtr;
	} TypesRecord;
	TypesRecord *recordPtr;
	static const char *const stringTable[] = {
	    "one", "two", "three", "four", NULL
	};
	static const char *const stringTable2[] = {
	    "one", "two", NULL
	};
	static const Tk_OptionSpec typesSpecs[] = {
	    {TK_OPTION_BOOLEAN, "-boolean", "boolean", "Boolean", NULL,
		offsetof(TypesRecord, booleanPtr), TCL_INDEX_NONE, TK_CONFIG_NULL_OK, 0, 0x1},
	    {TK_OPTION_INT, "-integer", "integer", "Integer", "7",
		offsetof(TypesRecord, integerPtr), TCL_INDEX_NONE, 0, 0, 0x2},
	    {TK_OPTION_DOUBLE, "-double", "double", "Double", "3.14159",
		offsetof(TypesRecord, doublePtr), TCL_INDEX_NONE, 0, 0, 0x4},
	    {TK_OPTION_STRING, "-string", "string", "String",
		"foo", offsetof(TypesRecord, stringPtr), TCL_INDEX_NONE,
		TK_CONFIG_NULL_OK, 0, 0x8},
	    {TK_OPTION_STRING_TABLE,
		"-stringtable", "StringTable", "stringTable",
		"one", offsetof(TypesRecord, stringTablePtr), TCL_INDEX_NONE,
		TK_CONFIG_NULL_OK, stringTable, 0x10},
	    {TK_OPTION_STRING_TABLE,
		"-stringtable2", "StringTable2", "stringTable2",
		"two", offsetof(TypesRecord, stringTablePtr2), TCL_INDEX_NONE,
		TK_CONFIG_NULL_OK, stringTable2, 0x10},
	    {TK_OPTION_COLOR, "-color", "color", "Color",
		"red", offsetof(TypesRecord, colorPtr), TCL_INDEX_NONE,
		TK_CONFIG_NULL_OK, "black", 0x20},
	    {TK_OPTION_FONT, "-font", "font", "Font", "Helvetica 12",
		offsetof(TypesRecord, fontPtr), TCL_INDEX_NONE,
		TK_CONFIG_NULL_OK, 0, 0x40},
	    {TK_OPTION_BITMAP, "-bitmap", "bitmap", "Bitmap", "gray50",
		offsetof(TypesRecord, bitmapPtr), TCL_INDEX_NONE,
		TK_CONFIG_NULL_OK, 0, 0x80},
	    {TK_OPTION_BORDER, "-border", "border", "Border",
		"blue", offsetof(TypesRecord, borderPtr), TCL_INDEX_NONE,
		TK_CONFIG_NULL_OK, "white", 0x100},
	    {TK_OPTION_RELIEF, "-relief", "relief", "Relief", NULL,
		offsetof(TypesRecord, reliefPtr), TCL_INDEX_NONE,
		TK_CONFIG_NULL_OK, 0, 0x200},
	    {TK_OPTION_CURSOR, "-cursor", "cursor", "Cursor", "xterm",
		offsetof(TypesRecord, cursorPtr), TCL_INDEX_NONE,
		TK_CONFIG_NULL_OK, 0, 0x400},
	    {TK_OPTION_JUSTIFY, "-justify", NULL, NULL, "left",
		offsetof(TypesRecord, justifyPtr), TCL_INDEX_NONE,
		TK_CONFIG_NULL_OK, 0, 0x800},
	    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor", "center",
		offsetof(TypesRecord, anchorPtr), TCL_INDEX_NONE,
		TK_CONFIG_NULL_OK, 0, 0x1000},
	    {TK_OPTION_PIXELS, "-pixel", "pixel", "Pixel",
		"1", offsetof(TypesRecord, pixelPtr), TCL_INDEX_NONE,
		TK_CONFIG_NULL_OK, 0, 0x2000},
	    {TK_OPTION_CUSTOM, "-custom", NULL, NULL,
		"", offsetof(TypesRecord, customPtr), TCL_INDEX_NONE,
		TK_CONFIG_NULL_OK, &CustomOption, 0x4000},
	    {TK_OPTION_SYNONYM, "-synonym", NULL, NULL,
		NULL, 0, TCL_INDEX_NONE, 0, "-color", 0x8000},
	    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}
	};
	Tk_OptionTable optionTable;

	optionTable = Tk_CreateOptionTable(interp, typesSpecs);
	tables[index] = optionTable;
	tkwin = Tk_CreateWindowFromPath(interp, (Tk_Window)clientData,
		Tcl_GetString(objv[2]), NULL);
	if (tkwin == NULL) {
	    return TCL_ERROR;
	}
	Tk_SetClass(tkwin, "Test");

	recordPtr = (TypesRecord *)ckalloc(sizeof(TypesRecord));
	recordPtr->header.interp = interp;
	recordPtr->header.optionTable = optionTable;
	recordPtr->header.tkwin = tkwin;
	recordPtr->booleanPtr = NULL;
	recordPtr->integerPtr = NULL;
	recordPtr->doublePtr = NULL;
	recordPtr->stringPtr = NULL;
	recordPtr->colorPtr = NULL;
	recordPtr->fontPtr = NULL;
	recordPtr->bitmapPtr = NULL;
	recordPtr->borderPtr = NULL;
	recordPtr->reliefPtr = NULL;
	recordPtr->cursorPtr = NULL;
	recordPtr->justifyPtr = NULL;
	recordPtr->anchorPtr = NULL;
	recordPtr->pixelPtr = NULL;
	recordPtr->mmPtr = NULL;
	recordPtr->stringTablePtr = NULL;
	recordPtr->stringTablePtr2 = NULL;
	recordPtr->customPtr = NULL;
	result = Tk_InitOptions(interp, recordPtr, optionTable,
		tkwin);
	if (result == TCL_OK) {
	    recordPtr->header.widgetCmd = Tcl_CreateObjCommand2(interp,
		    Tcl_GetString(objv[2]), TrivialConfigObjCmd,
		    recordPtr, TrivialCmdDeletedProc);
	    Tk_CreateEventHandler(tkwin, StructureNotifyMask,
		    TrivialEventProc, recordPtr);
	    result = Tk_SetOptions(interp, recordPtr, optionTable,
		    objc-3, objv+3, tkwin, NULL, NULL);
	    if (result != TCL_OK) {
		Tk_DestroyWindow(tkwin);
	    }
	} else {
	    Tk_DestroyWindow(tkwin);
	    ckfree(recordPtr);
	}
	if (result == TCL_OK) {
	    Tcl_SetObjResult(interp, objv[2]);
	}
	break;
    }

    case CHAIN1: {
	ExtensionWidgetRecord *recordPtr;
	Tk_OptionTable optionTable;

	tkwin = Tk_CreateWindowFromPath(interp, (Tk_Window)clientData,
		Tcl_GetString(objv[2]), NULL);
	if (tkwin == NULL) {
	    return TCL_ERROR;
	}
	Tk_SetClass(tkwin, "Test");
	optionTable = Tk_CreateOptionTable(interp, baseSpecs);
	tables[index] = optionTable;

	recordPtr = (ExtensionWidgetRecord *)ckalloc(sizeof(ExtensionWidgetRecord));
	recordPtr->header.interp = interp;
	recordPtr->header.optionTable = optionTable;
	recordPtr->header.tkwin = tkwin;
	recordPtr->base1ObjPtr = recordPtr->base2ObjPtr = NULL;
	recordPtr->extension3ObjPtr = recordPtr->extension4ObjPtr = NULL;
	result = Tk_InitOptions(interp, recordPtr, optionTable, tkwin);
	if (result == TCL_OK) {
	    result = Tk_SetOptions(interp, recordPtr, optionTable,
		    objc-3, objv+3, tkwin, NULL, NULL);
	    if (result != TCL_OK) {
		Tk_FreeConfigOptions(recordPtr, optionTable, tkwin);
	    }
	}
	if (result == TCL_OK) {
	    recordPtr->header.widgetCmd = Tcl_CreateObjCommand2(interp,
		    Tcl_GetString(objv[2]), TrivialConfigObjCmd,
		    recordPtr, TrivialCmdDeletedProc);
	    Tk_CreateEventHandler(tkwin, StructureNotifyMask,
		    TrivialEventProc, recordPtr);
	    Tcl_SetObjResult(interp, objv[2]);
	}
	break;
    }

    case CHAIN2:
    case CHAIN3: {
	ExtensionWidgetRecord *recordPtr;
	static const Tk_OptionSpec extensionSpecs[] = {
	    {TK_OPTION_STRING, "-three", "three", "Three", "three",
		offsetof(ExtensionWidgetRecord, extension3ObjPtr), TCL_INDEX_NONE, 0, NULL, 0},
	    {TK_OPTION_STRING, "-four", "four", "Four", "four",
		offsetof(ExtensionWidgetRecord, extension4ObjPtr), TCL_INDEX_NONE, 0, NULL, 0},
	    {TK_OPTION_STRING, "-two", "two", "Two", "two and a half",
		offsetof(ExtensionWidgetRecord, base2ObjPtr), TCL_INDEX_NONE, 0, NULL, 0},
	    {TK_OPTION_STRING,
		"-oneAgain", "oneAgain", "OneAgain", "one again",
		offsetof(ExtensionWidgetRecord, extension5ObjPtr), TCL_INDEX_NONE, 0, NULL, 0},
	    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, TCL_INDEX_NONE, 0,
		baseSpecs, 0}
	};
	Tk_OptionTable optionTable;

	tkwin = Tk_CreateWindowFromPath(interp, (Tk_Window)clientData,
		Tcl_GetString(objv[2]), NULL);
	if (tkwin == NULL) {
	    return TCL_ERROR;
	}
	Tk_SetClass(tkwin, "Test");
	optionTable = Tk_CreateOptionTable(interp, extensionSpecs);
	tables[index] = optionTable;

	recordPtr = (ExtensionWidgetRecord *)ckalloc(sizeof(ExtensionWidgetRecord));
	recordPtr->header.interp = interp;
	recordPtr->header.optionTable = optionTable;
	recordPtr->header.tkwin = tkwin;
	recordPtr->base1ObjPtr = recordPtr->base2ObjPtr = NULL;
	recordPtr->extension3ObjPtr = recordPtr->extension4ObjPtr = NULL;
	recordPtr->extension5ObjPtr = NULL;
	result = Tk_InitOptions(interp, recordPtr, optionTable, tkwin);
	if (result == TCL_OK) {
	    result = Tk_SetOptions(interp, recordPtr, optionTable,
		    objc-3, objv+3, tkwin, NULL, NULL);
	    if (result != TCL_OK) {
		Tk_FreeConfigOptions(recordPtr, optionTable, tkwin);
	    }
	}
	if (result == TCL_OK) {
	    recordPtr->header.widgetCmd = Tcl_CreateObjCommand2(interp,
		    Tcl_GetString(objv[2]), TrivialConfigObjCmd,
		    recordPtr, TrivialCmdDeletedProc);
	    Tk_CreateEventHandler(tkwin, StructureNotifyMask,
		    TrivialEventProc, recordPtr);
	    Tcl_SetObjResult(interp, objv[2]);
	}
	break;
    }

    case CONFIG_ERROR: {
	typedef struct {
	    Tcl_Obj *intPtr;
	} ErrorWidgetRecord;
	ErrorWidgetRecord widgetRecord;
	static const Tk_OptionSpec errorSpecs[] = {
	    {TK_OPTION_INT, "-int", "integer", "Integer", "bogus",
		offsetof(ErrorWidgetRecord, intPtr), 0, TK_OPTION_NULL_OK, NULL, 0},
	    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}
	};
	Tk_OptionTable optionTable;

	widgetRecord.intPtr = NULL;
	optionTable = Tk_CreateOptionTable(interp, errorSpecs);
	tables[index] = optionTable;
	return Tk_InitOptions(interp, &widgetRecord, optionTable,
		(Tk_Window) NULL);
    }

    case DEL:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "tableName");
	    return TCL_ERROR;
	}
	if (Tcl_GetIndexFromObjStruct(interp, objv[2], options,
		sizeof(char *), "table", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (tables[index] != NULL) {
	    Tk_DeleteOptionTable(tables[index]);
	    /* Make sure that Tk_DeleteOptionTable() is never done
	     * twice for the same table. */
	    tables[index] = NULL;
	}
	break;

    case INFO:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "tableName");
	    return TCL_ERROR;
	}
	if (Tcl_GetIndexFromObjStruct(interp, objv[2], options,
		sizeof(char *), "table", 0, &index) != TCL_OK) {
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, TkDebugConfig(interp, tables[index]));
	break;

    case INTERNAL: {
	/*
	 * This command is similar to the "alltypes" command except that it
	 * stores all the configuration options as internal forms instead of
	 * objects.
	 */

	typedef struct {
	    TrivialCommandHeader header;
	    int boolean;
	    int integer;
	    double doubleValue;
	    char *string;
	    int index;
	    XColor *colorPtr;
	    Tk_Font tkfont;
	    Pixmap bitmap;
	    Tk_3DBorder border;
	    int relief;
	    Tk_Cursor cursor;
	    Tk_Justify justify;
	    Tk_Anchor anchor;
	    int pixels;
	    double mm;
	    Tk_Window tkwin;
	    char *custom;
	} InternalRecord;
	InternalRecord *recordPtr;
	static const char *const internalStringTable[] = {
	    "one", "two", "three", "four", NULL
	};
	static const Tk_OptionSpec internalSpecs[] = {
	    {TK_OPTION_BOOLEAN, "-boolean", "boolean", "Boolean", "1",
		TCL_INDEX_NONE, offsetof(InternalRecord, boolean), TK_CONFIG_NULL_OK, 0, 0x1},
	    {TK_OPTION_INT, "-integer", "integer", "Integer", "148962237",
		TCL_INDEX_NONE, offsetof(InternalRecord, integer), 0, 0, 0x2},
	    {TK_OPTION_DOUBLE, "-double", "double", "Double", "3.14159",
		TCL_INDEX_NONE, offsetof(InternalRecord, doubleValue), 0, 0, 0x4},
	    {TK_OPTION_STRING, "-string", "string", "String", "foo",
		TCL_INDEX_NONE, offsetof(InternalRecord, string),
		TK_CONFIG_NULL_OK, 0, 0x8},
	    {TK_OPTION_STRING_TABLE,
		"-stringtable", "StringTable", "stringTable", "one",
		TCL_INDEX_NONE, offsetof(InternalRecord, index),
		TK_CONFIG_NULL_OK, internalStringTable, 0x10},
	    {TK_OPTION_COLOR, "-color", "color", "Color", "red",
		TCL_INDEX_NONE, offsetof(InternalRecord, colorPtr),
		TK_CONFIG_NULL_OK, "black", 0x20},
	    {TK_OPTION_FONT, "-font", "font", "Font", "Helvetica 12",
		TCL_INDEX_NONE, offsetof(InternalRecord, tkfont),
		TK_CONFIG_NULL_OK, 0, 0x40},
	    {TK_OPTION_BITMAP, "-bitmap", "bitmap", "Bitmap", "gray50",
		TCL_INDEX_NONE, offsetof(InternalRecord, bitmap),
		TK_CONFIG_NULL_OK, 0, 0x80},
	    {TK_OPTION_BORDER, "-border", "border", "Border", "blue",
		TCL_INDEX_NONE, offsetof(InternalRecord, border),
		TK_CONFIG_NULL_OK, "white", 0x100},
	    {TK_OPTION_RELIEF, "-relief", "relief", "Relief", NULL,
		TCL_INDEX_NONE, offsetof(InternalRecord, relief),
		TK_CONFIG_NULL_OK, 0, 0x200},
	    {TK_OPTION_CURSOR, "-cursor", "cursor", "Cursor", "xterm",
		TCL_INDEX_NONE, offsetof(InternalRecord, cursor),
		TK_CONFIG_NULL_OK, 0, 0x400},
	    {TK_OPTION_JUSTIFY, "-justify", NULL, NULL, "left",
		TCL_INDEX_NONE, offsetof(InternalRecord, justify),
		TK_CONFIG_NULL_OK|TK_OPTION_ENUM_VAR, 0, 0x800},
	    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor", "center",
		TCL_INDEX_NONE, offsetof(InternalRecord, anchor),
		TK_CONFIG_NULL_OK|TK_OPTION_ENUM_VAR, 0, 0x1000},
	    {TK_OPTION_PIXELS, "-pixel", "pixel", "Pixel", "1",
		TCL_INDEX_NONE, offsetof(InternalRecord, pixels),
		TK_CONFIG_NULL_OK, 0, 0x2000},
	    {TK_OPTION_WINDOW, "-window", "window", "Window", NULL,
		TCL_INDEX_NONE, offsetof(InternalRecord, tkwin),
		TK_CONFIG_NULL_OK, 0, 0},
	    {TK_OPTION_CUSTOM, "-custom", NULL, NULL, "",
		TCL_INDEX_NONE, offsetof(InternalRecord, custom),
		TK_CONFIG_NULL_OK, &CustomOption, 0x4000},
	    {TK_OPTION_SYNONYM, "-synonym", NULL, NULL,
		NULL, TCL_INDEX_NONE, TCL_INDEX_NONE, 0, "-color", 0x8000},
	    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}
	};
	Tk_OptionTable optionTable;

	optionTable = Tk_CreateOptionTable(interp, internalSpecs);
	tables[index] = optionTable;
	tkwin = Tk_CreateWindowFromPath(interp, (Tk_Window)clientData,
		Tcl_GetString(objv[2]), NULL);
	if (tkwin == NULL) {
	    return TCL_ERROR;
	}
	Tk_SetClass(tkwin, "Test");

	recordPtr = (InternalRecord *)ckalloc(sizeof(InternalRecord));
	recordPtr->header.interp = interp;
	recordPtr->header.optionTable = optionTable;
	recordPtr->header.tkwin = tkwin;
	recordPtr->boolean = 0;
	recordPtr->integer = 0;
	recordPtr->doubleValue = 0.0;
	recordPtr->string = NULL;
	recordPtr->index = 0;
	recordPtr->colorPtr = NULL;
	recordPtr->tkfont = NULL;
	recordPtr->bitmap = None;
	recordPtr->border = NULL;
	recordPtr->relief = TK_RELIEF_FLAT;
	recordPtr->cursor = NULL;
	recordPtr->justify = TK_JUSTIFY_LEFT;
	recordPtr->anchor = TK_ANCHOR_CENTER;
	recordPtr->pixels = 0;
	recordPtr->mm = 0.0;
	recordPtr->tkwin = NULL;
	recordPtr->custom = NULL;
	result = Tk_InitOptions(interp, recordPtr, optionTable,
		tkwin);
	if (result == TCL_OK) {
	    recordPtr->header.widgetCmd = Tcl_CreateObjCommand2(interp,
		    Tcl_GetString(objv[2]), TrivialConfigObjCmd,
		    recordPtr, TrivialCmdDeletedProc);
	    Tk_CreateEventHandler(tkwin, StructureNotifyMask,
		    TrivialEventProc, recordPtr);
	    result = Tk_SetOptions(interp, recordPtr, optionTable,
		    objc - 3, objv + 3, tkwin, NULL, NULL);
	    if (result != TCL_OK) {
		Tk_DestroyWindow(tkwin);
	    }
	} else {
	    Tk_DestroyWindow(tkwin);
	    ckfree(recordPtr);
	}
	if (result == TCL_OK) {
	    Tcl_SetObjResult(interp, objv[2]);
	}
	break;
    }

    case NEW: {
	typedef struct {
	    TrivialCommandHeader header;
	    Tcl_Obj *one;
	    Tcl_Obj *two;
	    Tcl_Obj *three;
	    Tcl_Obj *four;
	    Tcl_Obj *five;
	} FiveRecord;
	FiveRecord *recordPtr;
	static const Tk_OptionSpec smallSpecs[] = {
	    {TK_OPTION_INT, "-one", "one", "One", "1",
		offsetof(FiveRecord, one), TCL_INDEX_NONE, 0, NULL, 0},
	    {TK_OPTION_INT, "-two", "two", "Two", "2",
		offsetof(FiveRecord, two), TCL_INDEX_NONE, 0, NULL, 0},
	    {TK_OPTION_INT, "-three", "three", "Three", "3",
		offsetof(FiveRecord, three), TCL_INDEX_NONE, 0, NULL, 0},
	    {TK_OPTION_INT, "-four", "four", "Four", "4",
		offsetof(FiveRecord, four), TCL_INDEX_NONE, 0, NULL, 0},
	    {TK_OPTION_STRING, "-five", NULL, NULL, NULL,
		offsetof(FiveRecord, five), TCL_INDEX_NONE, 0, NULL, 0},
	    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}
	};

	if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 1, objv, "new name ?-option value ...?");
	    return TCL_ERROR;
	}

	recordPtr = (FiveRecord *)ckalloc(sizeof(FiveRecord));
	recordPtr->header.interp = interp;
	recordPtr->header.optionTable = Tk_CreateOptionTable(interp,
		smallSpecs);
	tables[index] = recordPtr->header.optionTable;
	recordPtr->header.tkwin = NULL;
	recordPtr->one = recordPtr->two = recordPtr->three = NULL;
	recordPtr->four = recordPtr->five = NULL;
	Tcl_SetObjResult(interp, objv[2]);
	result = Tk_InitOptions(interp, recordPtr,
		recordPtr->header.optionTable, (Tk_Window) NULL);
	if (result == TCL_OK) {
	    result = Tk_SetOptions(interp, recordPtr,
		    recordPtr->header.optionTable, objc - 3, objv + 3,
		    (Tk_Window) NULL, NULL, NULL);
	    if (result == TCL_OK) {
		recordPtr->header.widgetCmd = Tcl_CreateObjCommand2(interp,
			Tcl_GetString(objv[2]), TrivialConfigObjCmd,
			recordPtr, TrivialCmdDeletedProc);
	    } else {
		Tk_FreeConfigOptions(recordPtr,
			recordPtr->header.optionTable, (Tk_Window) NULL);
	    }
	}
	if (result != TCL_OK) {
	    ckfree(recordPtr);
	}

	break;
    }
    case NOT_ENOUGH_PARAMS: {
	typedef struct {
	    Tcl_Obj *fooObjPtr;
	} NotEnoughRecord;
	NotEnoughRecord record;
	static const Tk_OptionSpec errorSpecs[] = {
	    {TK_OPTION_INT, "-foo", "foo", "Foo", "0",
		offsetof(NotEnoughRecord, fooObjPtr), 0, 0, NULL, 0},
	    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}
	};
	Tcl_Obj *newObjPtr = Tcl_NewStringObj("-foo", TCL_INDEX_NONE);
	Tk_OptionTable optionTable;

	record.fooObjPtr = NULL;

	tkwin = Tk_CreateWindowFromPath(interp, mainWin, ".config", NULL);
	Tk_SetClass(tkwin, "Config");
	optionTable = Tk_CreateOptionTable(interp, errorSpecs);
	tables[index] = optionTable;
	Tk_InitOptions(interp, &record, optionTable, tkwin);
	if (Tk_SetOptions(interp, &record, optionTable, 1,
		&newObjPtr, tkwin, NULL, NULL) != TCL_OK) {
	    result = TCL_ERROR;
	}
	Tcl_DecrRefCount(newObjPtr);
	Tk_FreeConfigOptions( (char *) &record, optionTable, tkwin);
	Tk_DestroyWindow(tkwin);
	return result;
    }

    case TWO_WINDOWS: {
	typedef struct {
	    TrivialCommandHeader header;
	    Tcl_Obj *windowPtr;
	} ContentRecord;
	ContentRecord *recordPtr;
	static const Tk_OptionSpec contentSpecs[] = {
	    {TK_OPTION_WINDOW, "-window", "window", "Window", ".bar",
		offsetof(ContentRecord, windowPtr), TCL_INDEX_NONE, TK_CONFIG_NULL_OK, NULL, 0},
	    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}
	};
	tkwin = Tk_CreateWindowFromPath(interp,
		(Tk_Window)clientData, Tcl_GetString(objv[2]), NULL);

	if (tkwin == NULL) {
	    return TCL_ERROR;
	}
	Tk_SetClass(tkwin, "Test");

	recordPtr = (ContentRecord *)ckalloc(sizeof(ContentRecord));
	recordPtr->header.interp = interp;
	recordPtr->header.optionTable = Tk_CreateOptionTable(interp,
		contentSpecs);
	tables[index] = recordPtr->header.optionTable;
	recordPtr->header.tkwin = tkwin;
	recordPtr->windowPtr = NULL;

	result = Tk_InitOptions(interp, recordPtr,
		recordPtr->header.optionTable, tkwin);
	if (result == TCL_OK) {
	    result = Tk_SetOptions(interp, recordPtr,
		    recordPtr->header.optionTable, objc - 3, objv + 3,
		    tkwin, NULL, NULL);
	    if (result == TCL_OK) {
		recordPtr->header.widgetCmd = Tcl_CreateObjCommand2(interp,
			Tcl_GetString(objv[2]), TrivialConfigObjCmd,
			recordPtr, TrivialCmdDeletedProc);
		Tk_CreateEventHandler(tkwin, StructureNotifyMask,
			TrivialEventProc, recordPtr);
		Tcl_SetObjResult(interp, objv[2]);
	    } else {
		Tk_FreeConfigOptions(recordPtr,
			recordPtr->header.optionTable, tkwin);
	    }
	}
	if (result != TCL_OK) {
	    Tk_DestroyWindow(tkwin);
	    ckfree(recordPtr);
	}
    }
    }

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TrivialConfigObjCmd --
 *
 *	This command is used to test the configuration package. It only
 *	handles the "configure" and "cget" subcommands.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TrivialConfigObjCmd(
    void *clientData,	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    int result = TCL_OK;
    static const char *const options[] = {
	"cget", "configure", "csave", NULL
    };
    enum {
	CGET, CONFIGURE, CSAVE
    };
    Tcl_Obj *resultObjPtr;
    int index, mask;
    TrivialCommandHeader *headerPtr = (TrivialCommandHeader *)clientData;
    Tk_Window tkwin = headerPtr->tkwin;
    Tk_SavedOptions saved;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[1], options,
	    sizeof(char *), "command", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }

    Tcl_Preserve(clientData);

    switch (index) {
    case CGET:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option");
	    result = TCL_ERROR;
	    goto done;
	}
	resultObjPtr = Tk_GetOptionValue(interp, clientData,
		headerPtr->optionTable, objv[2], tkwin);
	if (resultObjPtr != NULL) {
	    Tcl_SetObjResult(interp, resultObjPtr);
	    result = TCL_OK;
	} else {
	    result = TCL_ERROR;
	}
	break;
    case CONFIGURE:
	if (objc == 2) {
	    resultObjPtr = Tk_GetOptionInfo(interp, clientData,
		    headerPtr->optionTable, NULL, tkwin);
	    if (resultObjPtr == NULL) {
		result = TCL_ERROR;
	    } else {
		Tcl_SetObjResult(interp, resultObjPtr);
	    }
	} else if (objc == 3) {
	    resultObjPtr = Tk_GetOptionInfo(interp, clientData,
		    headerPtr->optionTable, objv[2], tkwin);
	    if (resultObjPtr == NULL) {
		result = TCL_ERROR;
	    } else {
		Tcl_SetObjResult(interp, resultObjPtr);
	    }
	} else {
	    result = Tk_SetOptions(interp, clientData,
		    headerPtr->optionTable, objc - 2, objv + 2,
		    tkwin, NULL, &mask);
	    if (result == TCL_OK) {
		Tcl_SetObjResult(interp, Tcl_NewWideIntObj(mask));
	    }
	}
	break;
    case CSAVE:
	result = Tk_SetOptions(interp, clientData,
		headerPtr->optionTable, objc - 2, objv + 2,
		tkwin, &saved, &mask);
	Tk_FreeSavedOptions(&saved);
	if (result == TCL_OK) {
	    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(mask));
	}
	break;
    }
  done:
    Tcl_Release(clientData);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TrivialCmdDeletedProc --
 *
 *	This function is invoked when a widget command is deleted. If the
 *	widget isn't already in the process of being destroyed, this command
 *	destroys it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The widget is destroyed.
 *
 *----------------------------------------------------------------------
 */

static void
TrivialCmdDeletedProc(
    void *clientData)	/* Pointer to widget record for widget. */
{
    TrivialCommandHeader *headerPtr = (TrivialCommandHeader *)clientData;
    Tk_Window tkwin = headerPtr->tkwin;

    if (tkwin != NULL) {
	Tk_DestroyWindow(tkwin);
    } else if (headerPtr->optionTable != NULL) {
	/*
	 * This is a "new" object, which doesn't have a window, so we can't
	 * depend on cleaning up in the event function. Free its resources
	 * here.
	 */

	Tk_FreeConfigOptions(clientData,
		headerPtr->optionTable, NULL);
	Tcl_EventuallyFree(clientData, TCL_DYNAMIC);
    }
}

/*
 *--------------------------------------------------------------
 *
 * TrivialEventProc --
 *
 *	A dummy event proc.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	When the window gets deleted, internal structures get cleaned up.
 *
 *--------------------------------------------------------------
 */

static void
TrivialEventProc(
    void *clientData,	/* Information about window. */
    XEvent *eventPtr)		/* Information about event. */
{
    TrivialCommandHeader *headerPtr = (TrivialCommandHeader *)clientData;

    if (eventPtr->type == DestroyNotify) {
	if (headerPtr->tkwin != NULL) {
	    Tk_FreeConfigOptions(clientData,
		    headerPtr->optionTable, headerPtr->tkwin);
	    headerPtr->optionTable = NULL;
	    headerPtr->tkwin = NULL;
	    Tcl_DeleteCommandFromToken(headerPtr->interp,
		    headerPtr->widgetCmd);
	}
	Tcl_EventuallyFree(clientData, TCL_DYNAMIC);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TestfontObjCmd --
 *
 *	This function implements the "testfont" command, which is used to test
 *	TkFont objects.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TestfontObjCmd(
    void *clientData,	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    static const char *const options[] = {"counts", "subfonts", NULL};
    enum option {COUNTS, SUBFONTS};
    int index;
    Tk_Window tkwin;
    Tk_Font tkfont;

    tkwin = (Tk_Window)clientData;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "option fontName");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[1], options,
	    sizeof(char *), "command", 0, &index)!= TCL_OK) {
	return TCL_ERROR;
    }

    switch ((enum option) index) {
    case COUNTS:
	Tcl_SetObjResult(interp,
		TkDebugFont(Tk_MainWindow(interp), Tcl_GetString(objv[2])));
	break;
    case SUBFONTS:
	tkfont = Tk_AllocFontFromObj(interp, tkwin, objv[2]);
	if (tkfont == NULL) {
	    return TCL_ERROR;
	}
	TkpGetSubFonts(interp, tkfont);
	Tk_FreeFont(tkfont);
	break;
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ImageCreate --
 *
 *	This function is called by the Tk image code to create "test" images.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	The data structure for a new image is allocated.
 *
 *----------------------------------------------------------------------
 */

static int
ImageCreate(
    Tcl_Interp *interp,		/* Interpreter for application containing
				 * image. */
    const char *name,			/* Name to use for image. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[],	/* Argument strings for options (doesn't
				 * include image name or type). */
    TCL_UNUSED(const Tk_ImageType *),	/* Pointer to our type record (not used). */
	Tk_ImageModel model,	/* Token for image, to be used by us in later
				 * callbacks. */
    void **clientDataPtr)	/* Store manager's token for image here; it
				 * will be returned in later callbacks. */
{
    TImageModel *timPtr;
    const char *varName;
    Tcl_Size i;

    varName = "log";
    for (i = 0; i < objc; i += 2) {
	if (strcmp(Tcl_GetString(objv[i]), "-variable") != 0) {
	    Tcl_AppendResult(interp, "bad option name \"",
		    Tcl_GetString(objv[i]), "\"", NULL);
	    return TCL_ERROR;
	}
	if ((i+1) == objc) {
	    Tcl_AppendResult(interp, "no value given for \"",
		    Tcl_GetString(objv[i]), "\" option", NULL);
	    return TCL_ERROR;
	}
	varName = Tcl_GetString(objv[i+1]);
    }

    timPtr = (TImageModel *)ckalloc(sizeof(TImageModel));
    timPtr->model = model;
    timPtr->interp = interp;
    timPtr->width = 30;
    timPtr->height = 15;
    timPtr->imageName = (char *)ckalloc(strlen(name) + 1);
    strcpy(timPtr->imageName, name);
    timPtr->varName = (char *)ckalloc(strlen(varName) + 1);
    strcpy(timPtr->varName, varName);
    Tcl_CreateObjCommand2(interp, name, ImageObjCmd, timPtr, NULL);
    *clientDataPtr = timPtr;
    Tk_ImageChanged(model, 0, 0, 30, 15, 30, 15);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ImageObjCmd --
 *
 *	This function implements the commands corresponding to individual
 *	images.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Forces windows to be created.
 *
 *----------------------------------------------------------------------
 */

static int
ImageObjCmd(
    void *clientData,	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])		/* Argument strings. */
{
    TImageModel *timPtr = (TImageModel *)clientData;
    int x, y, width, height;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
	return TCL_ERROR;
    }
    if (strcmp(Tcl_GetString(objv[1]), "changed") == 0) {
	if (objc != 8) {
		Tcl_WrongNumArgs(interp, 1, objv, "changed x y width height"
			" imageWidth imageHeight");
	    return TCL_ERROR;
	}
	if ((Tcl_GetIntFromObj(interp, objv[2], &x) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[3], &y) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[4], &width) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[5], &height) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[6], &timPtr->width) != TCL_OK)
		|| (Tcl_GetIntFromObj(interp, objv[7], &timPtr->height) != TCL_OK)) {
	    return TCL_ERROR;
	}
	Tk_ImageChanged(timPtr->model, x, y, width, height, timPtr->width,
		timPtr->height);
    } else {
	Tcl_AppendResult(interp, "bad option \"", Tcl_GetString(objv[1]),
		"\": must be changed", NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ImageGet --
 *
 *	This function is called by Tk to set things up for using a test image
 *	in a particular widget.
 *
 * Results:
 *	The return value is a token for the image instance, which is used in
 *	future callbacks to ImageDisplay and ImageFree.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void *
ImageGet(
    Tk_Window tkwin,		/* Token for window in which image will be
				 * used. */
    void *clientData)	/* Pointer to TImageModel for image. */
{
    TImageModel *timPtr = (TImageModel *)clientData;
    TImageInstance *instPtr;
    char buffer[100];
    XGCValues gcValues;

    snprintf(buffer, sizeof(buffer), "%s get", timPtr->imageName);
    Tcl_SetVar2(timPtr->interp, timPtr->varName, NULL, buffer,
	    TCL_GLOBAL_ONLY|TCL_APPEND_VALUE|TCL_LIST_ELEMENT);

    instPtr = (TImageInstance *)ckalloc(sizeof(TImageInstance));
    instPtr->modelPtr = timPtr;
    instPtr->fg = Tk_GetColor(timPtr->interp, tkwin, "#ff0000");
    gcValues.foreground = instPtr->fg->pixel;
    instPtr->gc = Tk_GetGC(tkwin, GCForeground, &gcValues);
    instPtr->displayFailed = False;
    return instPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * ImageDisplay --
 *
 *	This function is invoked to redisplay part or all of an image in a
 *	given drawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The image gets partially redrawn, as an "X" that shows the exact
 *	redraw area.
 *
 *----------------------------------------------------------------------
 */

static void
ImageDisplay(
    void *clientData,	/* Pointer to TImageInstance for image. */
    Display *display,		/* Display to use for drawing. */
    Drawable drawable,		/* Where to redraw image. */
    int imageX, int imageY,	/* Origin of area to redraw, relative to
				 * origin of image. */
    int width, int height,	/* Dimensions of area to redraw. */
    int drawableX, int drawableY)
				/* Coordinates in drawable corresponding to
				 * imageX and imageY. */
{
    TImageInstance *instPtr = (TImageInstance *)clientData;

    /*
     * The purpose of the test image type is to track the calls to an image
     * display proc and record the parameters passed in each call.  On macOS a
     * display proc must be run inside of the drawRect method of an NSView in
     * order for the graphics operations to have any effect.  To deal with
     * this, whenever a display proc is called outside of any drawRect method
     * it schedules a redraw of the NSView.
     *
     * In an attempt to work around this, each image instance maintains it own
     * copy of the log message which gets written on the first call to the
     * display proc.  This usually means that the message created on macOS is
     * the same as that created on other platforms.  However it is possible
     * for the messages to differ for other reasons, namely differences in
     * how damage regions are computed.
     */

    if (LOG_DISPLAY(drawable)) {
	if (instPtr->displayFailed == False) {

	    /*
	     * Drawing is possible on the first call to DisplayImage.
	     * Log the message.
	     */

	    snprintf(instPtr->buffer, sizeof(instPtr->buffer), "%s display %d %d %d %d",
	    instPtr->modelPtr->imageName, imageX, imageY, width, height);
	}
	Tcl_SetVar2(instPtr->modelPtr->interp, instPtr->modelPtr->varName,
		    NULL, instPtr->buffer,
		    TCL_GLOBAL_ONLY|TCL_APPEND_VALUE|TCL_LIST_ELEMENT);
	instPtr->displayFailed = False;
    } else {

	/*
         * Drawing is not possible on the first call to DisplayImage.
	 * Save the message, but do not log it until the actual display.
	 */

	if (instPtr->displayFailed == False) {
	    snprintf(instPtr->buffer, sizeof(instPtr->buffer), "%s display %d %d %d %d",
		    instPtr->modelPtr->imageName, imageX, imageY, width, height);
	}
	instPtr->displayFailed = True;
    }
    if (width > (instPtr->modelPtr->width - imageX)) {
	width = instPtr->modelPtr->width - imageX;
    }
    if (height > (instPtr->modelPtr->height - imageY)) {
	height = instPtr->modelPtr->height - imageY;
    }

    XDrawRectangle(display, drawable, instPtr->gc, drawableX, drawableY,
	    (unsigned) (width-1), (unsigned) (height-1));
    XDrawLine(display, drawable, instPtr->gc, drawableX, drawableY,
	    (int) (drawableX + width - 1), (int) (drawableY + height - 1));
    XDrawLine(display, drawable, instPtr->gc, drawableX,
	    (int) (drawableY + height - 1),
	    (int) (drawableX + width - 1), drawableY);
}

/*
 *----------------------------------------------------------------------
 *
 * ImageFree --
 *
 *	This function is called when an instance of an image is no longer
 *	used.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information related to the instance is freed.
 *
 *----------------------------------------------------------------------
 */

static void
ImageFree(
    void *clientData,	/* Pointer to TImageInstance for instance. */
    Display *display)		/* Display where image was to be drawn. */
{
    TImageInstance *instPtr = (TImageInstance *)clientData;
    char buffer[200];

    snprintf(buffer, sizeof(buffer), "%s free", instPtr->modelPtr->imageName);
    Tcl_SetVar2(instPtr->modelPtr->interp, instPtr->modelPtr->varName, NULL,
	    buffer, TCL_GLOBAL_ONLY|TCL_APPEND_VALUE|TCL_LIST_ELEMENT);
    Tk_FreeColor(instPtr->fg);
    Tk_FreeGC(display, instPtr->gc);
    ckfree(instPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * ImageDelete --
 *
 *	This function is called to clean up a test image when an application
 *	goes away.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information about the image is deleted.
 *
 *----------------------------------------------------------------------
 */

static void
ImageDelete(
    void *clientData)	/* Pointer to TImageModel for image. When
				 * this function is called, no more instances
				 * exist. */
{
    TImageModel *timPtr = (TImageModel *)clientData;
    char buffer[100];

    snprintf(buffer, sizeof(buffer), "%s delete", timPtr->imageName);
    Tcl_SetVar2(timPtr->interp, timPtr->varName, NULL, buffer,
	    TCL_GLOBAL_ONLY|TCL_APPEND_VALUE|TCL_LIST_ELEMENT);

    Tcl_DeleteCommand(timPtr->interp, timPtr->imageName);
    ckfree(timPtr->imageName);
    ckfree(timPtr->varName);
    ckfree(timPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TestmakeexistObjCmd --
 *
 *	This function implements the "testmakeexist" command. It calls
 *	Tk_MakeWindowExist on each of its arguments to force the windows to be
 *	created.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Forces windows to be created.
 *
 *----------------------------------------------------------------------
 */

static int
TestmakeexistObjCmd(
    void *clientData,	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])		/* Argument strings. */
{
    Tk_Window mainWin = (Tk_Window)clientData;
    Tcl_Size i;
    Tk_Window tkwin;

    for (i = 1; i < objc; i++) {
	tkwin = Tk_NameToWindow(interp, Tcl_GetString(objv[i]), mainWin);
	if (tkwin == NULL) {
	    return TCL_ERROR;
	}
	Tk_MakeWindowExist(tkwin);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TestmenubarObjCmd --
 *
 *	This function implements the "testmenubar" command. It is used to test
 *	the Unix facilities for creating space above a toplevel window for a
 *	menubar.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Changes menubar related stuff.
 *
 *----------------------------------------------------------------------
 */

#if !(defined(_WIN32) || defined(MAC_OSX_TK) || defined(__CYGWIN__))
static int
TestmenubarObjCmd(
    void *clientData,	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])		/* Argument strings. */
{
#ifdef __UNIX__
    Tk_Window mainWin = (Tk_Window)clientData;
    Tk_Window tkwin, menubar;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
	return TCL_ERROR;
    }

    if (strcmp(Tcl_GetString(objv[1]), "window") == 0) {
	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 1, objv, "windows toplevel menubar");
	    return TCL_ERROR;
	}
	tkwin = Tk_NameToWindow(interp, Tcl_GetString(objv[2]), mainWin);
	if (tkwin == NULL) {
	    return TCL_ERROR;
	}
	if (Tcl_GetString(objv[3])[0] == 0) {
	    TkUnixSetMenubar(tkwin, NULL);
	} else {
	    menubar = Tk_NameToWindow(interp, Tcl_GetString(objv[3]), mainWin);
	    if (menubar == NULL) {
		return TCL_ERROR;
	    }
	    TkUnixSetMenubar(tkwin, menubar);
	}
    } else {
	Tcl_AppendResult(interp, "bad option \"", Tcl_GetString(objv[1]),
		"\": must be  window", NULL);
	return TCL_ERROR;
    }

    return TCL_OK;
#else
    Tcl_AppendResult(interp, "testmenubar is supported only under Unix", NULL);
    return TCL_ERROR;
#endif
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * TestmetricsObjCmd --
 *
 *	This function implements the testmetrics command. It provides a way to
 *	determine the size of various widget components.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#if defined(_WIN32)
static int
TestmetricsObjCmd(
    TCL_UNUSED(void *),	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])		/* Argument strings. */
{
    char buf[TCL_INTEGER_SPACE];
    int val;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
	return TCL_ERROR;
    }

    if (strcmp(Tcl_GetString(objv[1]), "cyvscroll") == 0) {
	val = GetSystemMetrics(SM_CYVSCROLL);
    } else  if (strcmp(Tcl_GetString(objv[1]), "cxhscroll") == 0) {
	val = GetSystemMetrics(SM_CXHSCROLL);
    } else {
	Tcl_AppendResult(interp, "bad option \"", Tcl_GetString(objv[1]),
		"\": must be cxhscroll or cyvscroll", NULL);
	return TCL_ERROR;
    }
    snprintf(buf, sizeof(buf), "%d", val);
    Tcl_AppendResult(interp, buf, NULL);
    return TCL_OK;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * TestpropObjCmd --
 *
 *	This function implements the "testprop" command. It fetches and prints
 *	the value of a property on a window.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TestpropObjCmd(
    void *clientData,	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])		/* Argument strings. */
{
    Tk_Window mainWin = (Tk_Window)clientData;
    int result, actualFormat;
    unsigned long bytesAfter, length, value;
    Atom actualType, propName;
    unsigned char *property, *p;
    char *end;
    Window w;
    char buffer[30];

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "window property");
	return TCL_ERROR;
    }

    w = strtoul(Tcl_GetString(objv[1]), &end, 0);
    propName = Tk_InternAtom(mainWin, Tcl_GetString(objv[2]));
    property = NULL;
    result = XGetWindowProperty(Tk_Display(mainWin),
	    w, propName, 0, 100000, False, AnyPropertyType,
	    &actualType, &actualFormat, &length,
	    &bytesAfter, &property);
    if ((result == Success) && (actualType != None)) {
	if ((actualFormat == 8) && (actualType == XA_STRING)) {
	    for (p = property; ((unsigned long)(p-property)) < length; p++) {
		if (*p == 0) {
		    *p = '\n';
		}
	    }
	    Tcl_SetObjResult(interp, Tcl_NewStringObj((/*!unsigned*/char*)property, TCL_INDEX_NONE));
	} else {
	    for (p = property; length > 0; length--) {
		if (actualFormat == 32) {
		    value = *((long *) p);
		    p += sizeof(long);
		} else if (actualFormat == 16) {
		    value = 0xffff & (*((short *) p));
		    p += sizeof(short);
		} else {
		    value = 0xff & *p;
		    p += 1;
		}
		snprintf(buffer, sizeof(buffer), "0x%lx", value);
		Tcl_AppendElement(interp, buffer);
	    }
	}
    }
    if (property != NULL) {
	XFree(property);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TestpropObjCmd --
 *
 *	This function implements the "testprop" command. It fetches and prints
 *	the value of a property on a window.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TestprintfObjCmd(
    TCL_UNUSED(void *),	/* Not used */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument strings. */
{
    char buffer[256];
    Tcl_WideInt wideInt;
    long long longLongInt;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "wideint");
	return TCL_ERROR;
    }
    if (Tcl_GetWideIntFromObj(interp, objv[1], &wideInt) != TCL_OK) {
	return TCL_ERROR;
    }
    longLongInt = wideInt;

    /* Just add a lot of arguments to snprintf. Reason: on AMD64, the first
     * 4 or 6 arguments (we assume 8, just in case) might be put in registers,
     * which still woudn't tell if the assumed size is correct: We want this
     * test-case to fail if the 64-bit value is printed as truncated to 32-bit.
     */
    snprintf(buffer, sizeof(buffer), "%s%s%s%s%s%s%s%s%" TCL_LL_MODIFIER "d %"
	    TCL_LL_MODIFIER "u", "", "", "", "", "", "", "", "",
	    longLongInt, (unsigned long long)longLongInt);
    Tcl_AppendResult(interp, buffer, NULL);
    return TCL_OK;
}

#if !(defined(_WIN32) || defined(MAC_OSX_TK) || defined(__CYGWIN__))
/*
 *----------------------------------------------------------------------
 *
 * TestwrapperObjCmd --
 *
 *	This function implements the "testwrapper" command. It provides a way
 *	from Tcl to determine the extra window Tk adds in between the toplevel
 *	window and the window decorations.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TestwrapperObjCmd(
    void *clientData,	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])		/* Argument strings. */
{
    TkWindow *winPtr, *wrapperPtr;
    Tk_Window tkwin;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "window");
	return TCL_ERROR;
    }

    tkwin = (Tk_Window)clientData;
    winPtr = (TkWindow *) Tk_NameToWindow(interp, Tcl_GetString(objv[1]), tkwin);
    if (winPtr == NULL) {
	return TCL_ERROR;
    }

    wrapperPtr = TkpGetWrapperWindow(winPtr);
    if (wrapperPtr != NULL) {
	char buf[TCL_INTEGER_SPACE];

	TkpPrintWindowId(buf, Tk_WindowId(wrapperPtr));
	Tcl_SetObjResult(interp, Tcl_NewStringObj(buf, TCL_INDEX_NONE));
    }
    return TCL_OK;
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * CustomOptionSet, CustomOptionGet, CustomOptionRestore, CustomOptionFree --
 *
 *	Handlers for object-based custom configuration options. See
 *	Testobjconfigcommand.
 *
 * Results:
 *	See user documentation for expected results from these functions.
 *		CustomOptionSet		Standard Tcl Result.
 *		CustomOptionGet		Tcl_Obj * containing value.
 *		CustomOptionRestore	None.
 *		CustomOptionFree	None.
 *
 * Side effects:
 *	Depends on the function.
 *		CustomOptionSet		Sets option value to new setting.
 *		CustomOptionGet		Creates a new Tcl_Obj.
 *		CustomOptionRestore	Resets option value to original value.
 *		CustomOptionFree	Free storage for internal rep of option.
 *
 *----------------------------------------------------------------------
 */

static int
CustomOptionSet(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    TCL_UNUSED(Tk_Window),
    Tcl_Obj **value,
    char *recordPtr,
    Tcl_Size internalOffset,
    char *saveInternalPtr,
    int flags)
{
    int objEmpty;
    char *newStr, *string, *internalPtr;

    objEmpty = 0;

    if (internalOffset != TCL_INDEX_NONE) {
	internalPtr = recordPtr + internalOffset;
    } else {
	internalPtr = NULL;
    }

    /*
     * See if the object is empty.
     */

    if (value == NULL) {
	objEmpty = 1;
	CLANG_ASSERT(value);
    } else if ((*value)->bytes != NULL) {
	objEmpty = ((*value)->length == 0);
    } else {
	(void)Tcl_GetString(*value);
	objEmpty = ((*value)->length == 0);
    }

    if ((flags & TK_OPTION_NULL_OK) && objEmpty) {
	*value = NULL;
    } else {
	string = Tcl_GetString(*value);
	Tcl_UtfToUpper(string);
	if (strcmp(string, "BAD") == 0) {
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("expected good value, got \"BAD\"", TCL_INDEX_NONE));
	    return TCL_ERROR;
	}
    }
    if (internalPtr != NULL) {
	if (*value != NULL) {
	    string = Tcl_GetString(*value);
	    newStr = (char *)ckalloc((*value)->length + 1);
	    strcpy(newStr, string);
	} else {
	    newStr = NULL;
	}
	*((char **) saveInternalPtr) = *((char **) internalPtr);
	*((char **) internalPtr) = newStr;
    }

    return TCL_OK;
}

static Tcl_Obj *
CustomOptionGet(
    TCL_UNUSED(void *),
    TCL_UNUSED(Tk_Window),
    char *recordPtr,
    Tcl_Size internalOffset)
{
    return (Tcl_NewStringObj(*(char **)(recordPtr + internalOffset), TCL_INDEX_NONE));
}

static void
CustomOptionRestore(
    TCL_UNUSED(void *),
    TCL_UNUSED(Tk_Window),
    char *internalPtr,
    char *saveInternalPtr)
{
    *(char **)internalPtr = *(char **)saveInternalPtr;
    return;
}

static void
CustomOptionFree(
    TCL_UNUSED(void *),
    TCL_UNUSED(Tk_Window),
    char *internalPtr)
{
    if (*(char **)internalPtr != NULL) {
	ckfree(*(char **)internalPtr);
    }
}
/*
 *----------------------------------------------------------------------
 *
 * TestPhotoStringMatchCmd --
 *
 *	This function implements the "testphotostringmatch" command. It
 *	provides a way from Tcl to call the string match function for the
 *	default image handler directly.
 *
 * Results:
 *	A standard Tcl result. If data is in the proper format, the result in
 *	interp will contain width and height as a list. If the data cannot be
 *	parsed as default image format, returns TCL_ERROR and leaves an
 *	appropriate error message in interp.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TestPhotoStringMatchCmd(
    TCL_UNUSED(void *),	/* Main window for application. */
    Tcl_Interp *interp,		/* Current interpreter. */
    Tcl_Size objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])		/* Argument strings. */
{
    Tcl_Obj *dummy = NULL;
    Tcl_Obj *resultObj[2];
    int width, height;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "imageData");
        return TCL_ERROR;
    }
    if (TkDebugPhotoStringMatchDef(interp, objv[1], dummy, &width, &height)) {
        resultObj[0] = Tcl_NewWideIntObj(width);
        resultObj[1] = Tcl_NewWideIntObj(height);
        Tcl_SetObjResult(interp, Tcl_NewListObj(2, resultObj));
        return TCL_OK;
    } else {
        return TCL_ERROR;
    }
}



/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
