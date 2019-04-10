/*
* tkoWidget.h --
*
*    This file contains the tko widget class.
*
* Copyright (c) 2019 Rene Zaumseil
*
*/
#include "tcl.h"
#include "tclOO.h"
#include "tk.h"
#include "tkInt.h"

#include "tkoWidget.h"

/*
* Widget structure data used in objects.
*/
typedef struct tkoWidget {
    Tcl_Interp *interp;        /* Interpreter associated with widget. */
    Tcl_Object object;         /* our own object */
    Tk_Window tkWin;           /* Window that embodies the canvas. NULL means
                                * that the window has been destroyed but the
                                * data structures haven't yet been cleaned
                                * up.*/
    Tcl_Obj *myCmd;            /* Objects "my" command. Needed to call internal methods. */
    Tcl_Command widgetCmd;     /* Token for canvas's widget command. */
    Tcl_Obj *optionsArray;     /* Name of option array variable */
    Tcl_HashTable optionsTable; /* Hash table containing all used options */
} tkoWidget;

/*
 * Widget option.
 */
typedef struct WidgetOption {
    Tcl_Obj *option;           /* Name of option */
    Tcl_Obj *dbname;           /* Database name or name of synonym option */
    Tcl_Obj *dbclass;          /* Class name or NULL for synonym options */
    Tcl_Obj *defvalue;         /* Default value from initialization */
    Tcl_Obj *value;            /* Contain last known value of option */
    int flags;
} WidgetOption;

/*
 * Static tcl objects.
 */
Tk_Uid TkoUid_class = NULL;
tkoObj TkoObj =
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
/*
* Methods
*/
static int WidgetConstructor(
	ClientData clientData,
	Tcl_Interp * interp,
	Tcl_ObjectContext context,
	int objc,
	Tcl_Obj * const objv[]);
static int WidgetDestructor(
	ClientData clientData,
	Tcl_Interp * interp,
	Tcl_ObjectContext context,
	int objc,
	Tcl_Obj * const objv[]);
static int WidgetMethod_cget(
	ClientData clientData,
	Tcl_Interp * interp,
	Tcl_ObjectContext context,
	int objc,
	Tcl_Obj * const objv[]);
static int WidgetMethod_configure(
	ClientData clientData,
	Tcl_Interp * interp,
	Tcl_ObjectContext context,
	int objc,
	Tcl_Obj * const objv[]);
static int
WidgetMethod_tko_configure(
	ClientData clientData,
	Tcl_Interp * interp,
	Tcl_ObjectContext context,
	int objc,
	Tcl_Obj * const objv[])
{              /* virtual method */
	return TCL_OK;
}
static int
WidgetMethod_(
	ClientData clientData,
	Tcl_Interp * interp,
	Tcl_ObjectContext context,
	int objc,
	Tcl_Obj * const objv[])
{              /* common option set method */
	tkoWidgetOptionDefine *define = (tkoWidgetOptionDefine *)clientData;
	return TkoWidgetOptionSet(interp, context, objv[objc - 1],
		define->type, define->meta, define->offset);
}

/*
 * Functions
 */
static char *WidgetOptionTrace(
    ClientData clientData,
    Tcl_Interp * interp,
    const char *name1,
    const char *name2,
    int flags);
static void WidgetOptionDelEntry(
    Tcl_HashEntry * entry);
static void WidgetEventProc(
    ClientData clientData,
    XEvent * eventPtr);
static int WidgetOptionAdd(
    Tcl_Interp * interp,
    tkoWidget * widget,
    Tcl_Obj * option,
    Tcl_Obj * dbname,
    Tcl_Obj * dbclass,
    Tcl_Obj * defvalue,
    Tcl_Obj * flags,
    Tcl_Obj * value,
    int initmode);
static int WidgetOptionConfigure(
    Tcl_Interp * interp,
    tkoWidget * widget,
    int objc,
    Tcl_Obj * const objv[]);
static int WidgetOptionDel(
    Tcl_Interp * interp,
    tkoWidget * widget,
    Tcl_Obj * option);
static int WidgetOptionGet(
    Tcl_Interp * interp,
    tkoWidget * widget,
    Tcl_Obj * option);
static int WidgetOptionSet(
    Tcl_Interp * interp,
    tkoWidget * widget,
    Tcl_Obj * option,
    Tcl_Obj * value);
static void WidgetMetaDestroy(
    tkoWidget * widget);
static void
WidgetMetaDelete(
    ClientData clientData)
{
    Tcl_EventuallyFree(clientData, (Tcl_FreeProc *) WidgetMetaDestroy);
}

/*
 * tkoWidgetMeta --
 */
Tcl_ObjectMetadataType tkoWidgetMeta = {
    TCL_OO_METADATA_VERSION_CURRENT,
    "tkoWidgetMeta",
    WidgetMetaDelete,
    NULL
};

/*
 * widgetMethods --
 *
 */
static Tcl_MethodType widgetMethods[] = {
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, WidgetConstructor, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, NULL, WidgetDestructor, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "cget", WidgetMethod_cget, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "configure", WidgetMethod_configure, NULL,
            NULL},
    {-1, NULL, NULL, NULL, NULL},
    {TCL_OO_METHOD_VERSION_CURRENT, "_tko_configure",
            WidgetMethod_tko_configure, NULL, NULL},
    {-1, NULL, NULL, NULL, NULL}
};

/*
 * TkoWidgetWindow --
 *
 * Results:
 *
 * Side effects:
 */
Tk_Window *
TkoWidgetWindow(
    Tcl_Object object)
{
tkoWidget *widget = (tkoWidget *) Tcl_ObjectGetMetadata(object, &tkoWidgetMeta);
    if(widget)
        return &widget->tkWin;
    return NULL;
}

/*
 * TkoWidgetOptionVar --
 *
 * Results:
 *
 * Side effects:
 */
Tcl_Obj *
TkoWidgetOptionVar(
    Tcl_Object object)
{
tkoWidget *widget = (tkoWidget *) Tcl_ObjectGetMetadata(object, &tkoWidgetMeta);
    if(widget)
        return widget->optionsArray;
    return NULL;
}

/*
 * TkoWidgetClassDefine --
 *
 * Results:
 *
 * Side effects:
 */
int
TkoWidgetClassDefine(
    Tcl_Interp * interp,
    Tcl_Class clazz,
    Tcl_Obj * classname,
    const Tcl_MethodType * methods,
    const tkoWidgetOptionDefine * options)
{
    Tcl_Obj *listPtr;
    int i;
    Tcl_Obj *option;
    Tcl_MethodType *methodPtr;
    Tcl_Obj *myObjv[6];
    Tcl_Obj *retPtr;
    Tcl_Obj *tmpObj;

    if(classname == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing class name"));
        return TCL_ERROR;
    }
    /*
     * Add methods
     */
    if(methods) {
        if(methods[0].name == NULL && methods[0].callProc) {
            Tcl_ClassSetConstructor(interp, clazz,
                Tcl_NewMethod(interp, clazz, NULL, 1, &methods[0], NULL));
        }
        if(methods[1].name == NULL && methods[1].callProc) {
            Tcl_ClassSetDestructor(interp, clazz,
                Tcl_NewMethod(interp, clazz, NULL, 1, &methods[1], NULL));
        }
        for(i = 2; methods[i].name != NULL; i++) {
            tmpObj = Tcl_NewStringObj(methods[i].name, -1);
            Tcl_IncrRefCount(tmpObj);
            Tcl_NewMethod(interp, clazz, tmpObj, 1, &methods[i], NULL);
            Tcl_DecrRefCount(tmpObj);
        }
        i++;
        for(; methods[i].name != NULL; i++) {
            tmpObj = Tcl_NewStringObj(methods[i].name, -1);
            Tcl_IncrRefCount(tmpObj);
            Tcl_NewMethod(interp, clazz, tmpObj, 0, &methods[i], NULL);
            Tcl_DecrRefCount(tmpObj);
        }
    }
    /*
     *Add options
     */
    if(options) {
        retPtr =
            Tcl_ObjGetVar2(interp, TkoObj.tko_options, classname,
            TCL_GLOBAL_ONLY);
        if(retPtr == NULL) {
            retPtr = Tcl_NewObj();
        } else {
            if(Tcl_ListObjLength(interp, retPtr, &i) != TCL_OK) {
                Tcl_SetObjResult(interp,
                    Tcl_ObjPrintf("::tko::options(%s) variable is not list",
                        Tcl_GetString(classname)));
                goto error;
            }
        }
        Tcl_IncrRefCount(retPtr);
        for(i = 0;; i++) {
            if(options[i].option == NULL)
                break;  /* end of options */
            if(options[i].dbname == NULL) {
                Tcl_SetObjResult(interp,
                    Tcl_ObjPrintf("wrong option definition: %d", i));
                goto error;
            }
            listPtr = Tcl_NewObj();
            option = Tcl_NewStringObj(options[i].option, -1);
            Tcl_ListObjAppendElement(interp, listPtr, option);
            Tcl_ListObjAppendElement(interp, listPtr,
                Tcl_NewStringObj(options[i].dbname, -1));
            /* synonym option, ignore rest */
            if(options[i].dbclass == NULL) {
                Tcl_ListObjAppendElement(interp, retPtr, listPtr);
                continue;
            }
            /* normal option */
            Tcl_ListObjAppendElement(interp, listPtr,
                Tcl_NewStringObj(options[i].dbclass, -1));
            if(options[i].defvalue == NULL) {
                Tcl_ListObjAppendElement(interp, listPtr, TkoObj.empty);
            } else {
                Tcl_ListObjAppendElement(interp, listPtr,
                    Tcl_NewStringObj(options[i].defvalue, -1));
            }
            Tcl_ListObjAppendElement(interp, listPtr,
                Tcl_NewIntObj(options[i].flags));
            Tcl_ListObjAppendElement(interp, retPtr, listPtr);
            if(options[i].proc != NULL) {
                myObjv[0] = TkoObj.oo_define;
                myObjv[1] = classname;
                myObjv[2] = TkoObj.method;
                myObjv[3] = option;
                myObjv[4] = TkoObj.empty;
                myObjv[5] = Tcl_NewStringObj(options[i].proc, -1);
                Tcl_IncrRefCount(myObjv[5]);
                if(Tcl_EvalObjv(interp, 6, myObjv, TCL_EVAL_GLOBAL) != TCL_OK) {
                    Tcl_DecrRefCount(myObjv[5]);
                    goto error;
                }
                Tcl_DecrRefCount(myObjv[5]);
            } else if(options[i].method != NULL) {
                methodPtr = (Tcl_MethodType *) ckalloc(sizeof(Tcl_MethodType));
                methodPtr->version = TCL_OO_METHOD_VERSION_CURRENT;
                methodPtr->name = options[i].option;
                methodPtr->callProc = options[i].method;
                methodPtr->deleteProc = NULL;
                methodPtr->cloneProc = NULL;
                Tcl_NewMethod(interp, clazz, option, 0, methodPtr, NULL);
            } else if(options[i].type >= 0) {
                methodPtr = (Tcl_MethodType *) ckalloc(sizeof(Tcl_MethodType));
                methodPtr->version = TCL_OO_METHOD_VERSION_CURRENT;
                methodPtr->name = options[i].option;
                methodPtr->callProc = WidgetMethod_;
                methodPtr->deleteProc = NULL;
                methodPtr->cloneProc = NULL;
                Tcl_NewMethod(interp, clazz, option, 0, methodPtr,
                    (ClientData) & options[i]);
            }
        }
        Tcl_IncrRefCount(retPtr);
        Tcl_ObjSetVar2(interp, TkoObj.tko_options, classname, retPtr,
            TCL_GLOBAL_ONLY);
        Tcl_SetObjResult(interp, retPtr);
        Tcl_DecrRefCount(retPtr);
    }
    return TCL_OK;
  error:
    Tcl_DecrRefCount(retPtr);
    return TCL_ERROR;
}

/*
 * Tko_Init --
 *
 * Results:
 *
 * Side effects:
 */
int
Tko_Init(
    Tcl_Interp * interp /* Tcl interpreter. */)
{             
    /* Needed oo extension */
    if(Tcl_OOInitStubs(interp) == NULL) {
        return TCL_ERROR;
    }

    if(Tko_WidgetInit(interp) != TCL_OK) {
        return TCL_ERROR;
    }
    if(Tko_FrameInit(interp) != TCL_OK) {
        return TCL_ERROR;
    }
    if(Tko_VectorInit(interp) != TCL_OK) {
        return TCL_ERROR;
    }
    if(Tko_GraphInit(interp) != TCL_OK) {
        return TCL_ERROR;
    }
    if(Tko_PathInit(interp) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * Tko_WidgetInit --
 *
 * Results:
 *
 * Side effects:
 */
int
Tko_WidgetInit(
    Tcl_Interp * interp /* Tcl interpreter. */)
{             
Tcl_Class clazz;
Tcl_Object object;

    /* Create class like tk command and remove oo functions from widget commands */
static const char *initScript =
    "namespace eval ::tko {variable options}\n"
    "array set ::tko::options {}\n"
    "::oo::class create ::tko::widget {\n"
    " variable tko\n"
    " self unexport new destroy\n"
    " unexport new create destroy\n"
    "}\n"
	"set ::tko::unknown [list self method unknown args {\n"
    " if {[set w [lindex $args 0]] eq {}} {return -code error \"wrong # args: should be \\\"[self] pathName ?-option value ...?\\\"\"}\n"
    " uplevel #0 [list [self] create $w {} [lrange $args 1 end]]\n"
    " $w configure init\n"
    " return $w\n"
    "}]";
    /*
     * Internal variables and constants.
     */
    TkoUid_class = Tk_GetUid("-class");
    if(TkoObj.empty == NULL) {
        Tcl_IncrRefCount((TkoObj.empty = Tcl_NewStringObj("", -1)));
        Tcl_IncrRefCount((TkoObj.point = Tcl_NewStringObj(".", -1)));
        Tcl_IncrRefCount((TkoObj.next = Tcl_NewStringObj("next", -1)));
        Tcl_IncrRefCount((TkoObj.uplevel = Tcl_NewStringObj("::uplevel", -1)));
        Tcl_IncrRefCount((TkoObj.oo_define =
                Tcl_NewStringObj("::oo::define", -1)));
        Tcl_IncrRefCount((TkoObj.oo_objdefine =
                Tcl_NewStringObj("::oo::objdefine", -1)));
        Tcl_IncrRefCount((TkoObj.method = Tcl_NewStringObj("method", -1)));
        Tcl_IncrRefCount((TkoObj._tko_configure =
                Tcl_NewStringObj("_tko_configure", -1)));
        Tcl_IncrRefCount((TkoObj.tko = Tcl_NewStringObj("::tko", -1)));
        Tcl_IncrRefCount((TkoObj.tko_options =
                Tcl_NewStringObj("::tko::options", -1)));
        Tcl_IncrRefCount((TkoObj.lsort = Tcl_NewStringObj("::lsort", -1)));
        Tcl_IncrRefCount((TkoObj.tko_widget =
                Tcl_NewStringObj("::tko::widget", -1)));
        Tcl_IncrRefCount((TkoObj.tko_frame =
                Tcl_NewStringObj("::tko::frame", -1)));
        Tcl_IncrRefCount((TkoObj.tko_labelframe =
                Tcl_NewStringObj("::tko::labelframe", -1)));
        Tcl_IncrRefCount((TkoObj.tko_toplevel =
                Tcl_NewStringObj("::tko::toplevel", -1)));
        Tcl_IncrRefCount((TkoObj.path = Tcl_NewStringObj("::path", -1)));
        Tcl_IncrRefCount((TkoObj.graph = Tcl_NewStringObj("::graph", -1)));
        Tcl_IncrRefCount((TkoObj._screen = Tcl_NewStringObj("-screen", -1)));
        Tcl_IncrRefCount((TkoObj._labelwidget =
                Tcl_NewStringObj("-labelwidget", -1)));
        Tcl_IncrRefCount((TkoObj._0 = Tcl_NewIntObj(0)));
        Tcl_IncrRefCount((TkoObj._1 = Tcl_NewIntObj(1)));
    }

    /* Create widget class. */
    if(Tcl_Eval(interp, initScript) != TCL_OK) {
        return TCL_ERROR;
    }
    /* Get class object */
    if((object = Tcl_GetObjectFromObj(interp, TkoObj.tko_widget)) == NULL
        || (clazz = Tcl_GetObjectAsClass(object)) == NULL) {
        return TCL_ERROR;
    }
    /*
     * Add methods and options
     */
    if(TkoWidgetClassDefine(interp, clazz, Tcl_GetObjectName(interp, object),
            widgetMethods, NULL) != TCL_OK) {
        return TCL_ERROR;
    }

    return TCL_OK;
}

/*
 * WidgetConstructor --
 *
 * class create path optiondefs optionargs
 * -screen ""	-- special arg to place toplevel widgets
 *
 * Results:
 *
 * Side effects:
 */
static int
WidgetConstructor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Object object;
    tkoWidget *widget;
    Tk_Window tkWin;
    Tcl_Namespace *nsPtr;
    Tcl_Obj *argPtr;
    int argSize;
    Tcl_Obj **optionObjv;
    int optionObjc;
    Tcl_Obj **argObjv;
    int argObjc;
    int i;
    Tcl_Obj *dbclass;
    Tcl_Obj *defvalue;
    Tcl_Obj *flags;
    Tcl_Obj *value;
    const char *screenName = NULL;
    Tcl_Obj *win;
    char *ch = NULL;
    int length;

    /* Get current object. Should not fail? */
    if((object = Tcl_ObjectContextObject(context)) == NULL) {
        return TCL_ERROR;
    }
    /* Check objv[] arguments: ... optionlist arglist */
    if(objc - Tcl_ObjectContextSkippedArgs(context) != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "optionlist arglist");
        return TCL_ERROR;
    }
    /* check arguments */
    if(Tcl_ListObjGetElements(interp, objv[objc - 2], &optionObjc,
            &optionObjv) != TCL_OK) {
        Tcl_SetObjResult(interp,
            Tcl_ObjPrintf("could not get list of options"));
        return TCL_ERROR;
    }
    argPtr = objv[objc - 1];
    if(Tcl_DictObjSize(interp, argPtr, &argSize) != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("could not get arglist"));
        return TCL_ERROR;
    }
    /* Check on -screen option indicating creation of toplevel window
     */
    Tcl_DictObjGet(interp, argPtr, TkoObj._screen, &value);
    if(value) {
        for(i = 0; i < optionObjc; i++) {
            Tcl_ListObjGetElements(interp, optionObjv[i], &argObjc, &argObjv);
            if(strcmp(Tcl_GetString(argObjv[0]), "-screen") == 0) {
                screenName = Tcl_GetString(value);
                break;
            }
        }
    }
    /* check widget name */
    if((win = Tcl_GetObjectName(interp, object)) == NULL
        || (ch = TclGetStringFromObj(win, &length)) == NULL
        || length < 4 || ch[0] != ':' || ch[1] != ':' || ch[2] != '.') {
		if (ch) {
			Tcl_SetObjResult(interp, Tcl_ObjPrintf("no pathName"));
		}
		else {
			Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong pathName: %s", ch));
		}
        return TCL_ERROR;
    }
    tkWin =
        Tk_CreateWindowFromPath(interp, Tk_MainWindow(interp), &ch[2],
        screenName);
    if(tkWin == NULL) {
        return TCL_ERROR;
    }
    Tk_MakeWindowExist(tkWin);
    widget = (tkoWidget *)ckalloc(sizeof(tkoWidget));
    widget->interp = interp;
    widget->object = object;
    widget->tkWin = tkWin;
    widget->widgetCmd = Tcl_GetObjectCommand(object);
    Tcl_InitHashTable(&widget->optionsTable, TCL_ONE_WORD_KEYS);
    widget->optionsArray = NULL;
    /* Create option array variable */
    nsPtr = Tcl_GetObjectNamespace(object);
    widget->optionsArray = Tcl_NewStringObj(nsPtr->fullName, -1);
    Tcl_IncrRefCount(widget->optionsArray);
    Tcl_AppendToObj(widget->optionsArray, "::tko", -1);
    /* set tko(.) to name of widget */
    win = Tcl_NewStringObj(&ch[2], length - 2);
    Tcl_IncrRefCount(win);
    if(Tcl_ObjSetVar2(interp, widget->optionsArray, TkoObj.point, win,
            TCL_GLOBAL_ONLY) == NULL) {
        Tcl_DecrRefCount(win);
        goto error;
    }
    Tcl_DecrRefCount(win);
    /* Create my command */
    widget->myCmd = Tcl_NewStringObj(nsPtr->fullName, -1);
    Tcl_IncrRefCount(widget->myCmd);
    Tcl_AppendToObj(widget->myCmd, "::my", -1);

    Tcl_ObjectSetMetadata(object, &tkoWidgetMeta, (ClientData) widget);
    /*
     * Add options
     */
    for(i = 0; i < optionObjc; i++) {
        Tcl_ListObjGetElements(interp, optionObjv[i], &argObjc, &argObjv);
        dbclass = defvalue = flags = value = NULL;
        switch (argObjc) {
        case 2:
            break;
        case 3:
            dbclass = argObjv[2];
            break;
        case 4:
            dbclass = argObjv[2];
            defvalue = argObjv[3];
            break;
        case 5:
            dbclass = argObjv[2];
            defvalue = argObjv[3];
            flags = argObjv[4];
            break;
        default:
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong option def: %s",
                    Tcl_GetString(optionObjv[i])));
            goto error;
        }
        Tcl_DictObjGet(interp, argPtr, argObjv[0], &value);
        if(value) {
            Tcl_IncrRefCount(value);
            Tcl_DictObjRemove(interp, argPtr, argObjv[0]);
            argSize--;
        }
        if(WidgetOptionAdd(interp, widget, argObjv[0], argObjv[1],
                dbclass, defvalue, flags, value, 1) != TCL_OK) {
            goto error;
        }
    }
    if(argSize) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown options: %s",
                Tcl_GetString(argPtr)));
        goto error;
    }

    Tcl_TraceVar2(interp, Tcl_GetString(widget->optionsArray), NULL,
        TCL_TRACE_WRITES | TCL_TRACE_RESULT_OBJECT, WidgetOptionTrace, widget);

    Tk_CreateEventHandler(tkWin, StructureNotifyMask,
        WidgetEventProc, (ClientData) widget);

    return TCL_OK;

  error:
    Tcl_DeleteCommandFromToken(interp, Tcl_GetObjectCommand(object));
    return TCL_ERROR;
}

/*
 * WidgetDestructor --
 *
 * Results:
 *
 * Side effects:
 */
static int
WidgetDestructor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Object object;
    tkoWidget *widget;

    /* Get current object. Should not fail? */
    if((object = Tcl_ObjectContextObject(context)) == NULL)
        return TCL_ERROR;
    if((widget =
            (tkoWidget *) Tcl_ObjectGetMetadata(object,
                &tkoWidgetMeta)) == NULL)
        return TCL_ERROR;
    Tcl_Preserve(widget);
    if(widget->tkWin) {
        Tk_DeleteEventHandler(widget->tkWin, StructureNotifyMask,
            WidgetEventProc, (ClientData) widget);
        Tk_DestroyWindow(widget->tkWin);
        widget->tkWin = NULL;
    }
    Tcl_ObjectSetMetadata(object, &tkoWidgetMeta, NULL);
    Tcl_Release(widget);
    return TCL_OK;
}

/*
 * WidgetMetaDestroy --
 *
 * Results:
 *
 * Side effects:
 */
static void
WidgetMetaDestroy(
    tkoWidget * widget)
{
Tcl_HashSearch search;
Tcl_HashEntry *entryPtr;

    entryPtr = Tcl_FirstHashEntry(&widget->optionsTable, &search);
    while(entryPtr != NULL) {
        WidgetOptionDelEntry(entryPtr);
        entryPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&widget->optionsTable);
    if(widget->optionsArray != NULL) {
        Tcl_DecrRefCount((widget->optionsArray));
        widget->optionsArray = NULL;
    }
    if(widget->myCmd) {
        Tcl_DecrRefCount(widget->myCmd);
        widget->myCmd = NULL;
    }
    ckfree(widget);
}

/*
* WidgetEventProc --
*
*    This function is invoked by the Tk dispatcher for various events on
*    canvases.
*
* Results:
*    None.
*
* Side effects:
*    When the window gets deleted, internal structures get cleaned up.
*    When it gets exposed, it is redisplayed.
*/
static void
WidgetEventProc(
    ClientData clientData,     /* Information about window. */
    XEvent * eventPtr)
{              /* Information about event. */
tkoWidget *widget = (tkoWidget *) clientData;

    if(eventPtr->type == DestroyNotify) {
        if(widget->tkWin) {
            Tk_DeleteEventHandler(widget->tkWin, StructureNotifyMask,
                WidgetEventProc, widget);
            Tk_DestroyWindow(widget->tkWin);
            widget->tkWin = NULL;
            Tcl_DeleteCommandFromToken(widget->interp, widget->widgetCmd);
        }
    }
}

/*
 * WidgetMethod_cget --
 *
 * cget "-option"
 *
 * Results:
 *
 * Side effects:
 */
static int
WidgetMethod_cget(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Object object;
    tkoWidget *widget;         /* widget. */
    int skip;

    if((object = Tcl_ObjectContextObject(context)) == NULL)
        return TCL_ERROR;
    if((widget =
            (tkoWidget *) Tcl_ObjectGetMetadata(object,
                &tkoWidgetMeta)) == NULL)
        return TCL_ERROR;
    skip = Tcl_ObjectContextSkippedArgs(context);

    if(objc - skip != 1) {
        Tcl_WrongNumArgs(interp, skip, objv, "option");
        return TCL_ERROR;
    }
    return WidgetOptionGet(interp, widget, objv[skip]);
}

/*
 * WidgetMethod_configure --
 *
 * configure
 * configure "-option"
 * configure "-option value .."
 * configure "add option dbname dbclass ?default?"
 * configure "del option"
 * configure "after"
 *
 * set tk(-option) -> WidgetTraceOption() -> my -option $v ..
 * Results:
 *
 * Side effects:
 */
static int
WidgetMethod_configure(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    int result, index;
    Tcl_Object object;
    tkoWidget *widget;         /* widget. */
    int skip;
    Tcl_Obj *myObjv[2];
	static const char *const commandNames[] =
	{ "init", "optionadd", "optiondel", "optionvar", NULL };
	enum command {
		COMMAND_INIT, COMMAND_OPTIONADD, COMMAND_OPTIONDEL,
		COMMAND_OPTIONVAR
	};
    Tcl_Obj *dbclass = NULL;
    Tcl_Obj *defvalue = NULL;
    Tcl_Obj *flags = NULL;
	Tcl_HashSearch search;
	Tcl_HashEntry *entryPtr;
	WidgetOption *optionPtr;
	Tcl_Obj *retPtr;
	Tcl_Obj *listPtr;

    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (widget = (tkoWidget *)Tcl_ObjectGetMetadata(object,
		&tkoWidgetMeta)) == NULL) {
		return TCL_ERROR;
	}
    skip = Tcl_ObjectContextSkippedArgs(context);

    if(widget->tkWin == NULL) {
        return TCL_ERROR;
    }
    /* configure */
    if(objc - skip == 0) {
	    retPtr = Tcl_NewObj();
        entryPtr = Tcl_FirstHashEntry(&widget->optionsTable, &search);
        while(entryPtr != NULL) {       /* TODO Tcl_DuplicateObj()? */
            optionPtr = (WidgetOption *) Tcl_GetHashValue(entryPtr);
            listPtr = Tcl_NewObj();
            Tcl_ListObjAppendElement(interp, listPtr, optionPtr->option);
            Tcl_ListObjAppendElement(interp, listPtr, optionPtr->dbname);
            if(optionPtr->dbclass != NULL) {
                Tcl_ListObjAppendElement(interp, listPtr, optionPtr->dbclass);
                Tcl_ListObjAppendElement(interp, listPtr, optionPtr->defvalue);
                Tcl_ListObjAppendElement(interp, listPtr, optionPtr->value);
            }
            Tcl_ListObjAppendElement(interp, retPtr, listPtr);
            entryPtr = Tcl_NextHashEntry(&search);
        }
        /* Return sorted list */
        myObjv[0] = TkoObj.lsort;
        myObjv[1] = retPtr;
        return (Tcl_EvalObjv(interp, 2, myObjv, TCL_EVAL_GLOBAL));
    }
    /* configure "-option ?value? .." */
    if(Tcl_GetString(objv[skip])[0] == '-') {
        if(objc - skip == 1) {  /* configure -option */
            entryPtr =
                Tcl_FindHashEntry(&widget->optionsTable,
                Tk_GetUid(Tcl_GetString(objv[skip])));
            if(entryPtr == NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown option \"%s\"",
                        Tcl_GetString(objv[skip])));
                return TCL_ERROR;
            }
            optionPtr = (WidgetOption *) Tcl_GetHashValue(entryPtr);
            if(optionPtr->dbclass == NULL) {
                entryPtr =
                    Tcl_FindHashEntry(&widget->optionsTable,
                    Tk_GetUid(Tcl_GetString(optionPtr->dbname)));
                if(entryPtr == NULL) {
                    Tcl_SetObjResult(interp,
                        Tcl_ObjPrintf("unknown option \"%s\"",
                            Tcl_GetString(objv[skip])));
                    return TCL_ERROR;
                }
                optionPtr = (WidgetOption *) Tcl_GetHashValue(entryPtr);
                if(optionPtr->dbclass == NULL) {
                    Tcl_SetObjResult(interp,
                        Tcl_ObjPrintf("unknown option \"%s\"",
                            Tcl_GetString(objv[skip])));
                    return TCL_ERROR;
                }
            }
            listPtr = Tcl_NewObj();
            Tcl_ListObjAppendElement(interp, listPtr, optionPtr->option);
            Tcl_ListObjAppendElement(interp, listPtr, optionPtr->dbname);
            Tcl_ListObjAppendElement(interp, listPtr, optionPtr->dbclass);
            Tcl_ListObjAppendElement(interp, listPtr, optionPtr->defvalue);
            Tcl_ListObjAppendElement(interp, listPtr, optionPtr->value);
            Tcl_SetObjResult(interp, listPtr);
            return TCL_OK;
        }
        if((objc - skip) % 2 == 0) {    /* configure "-option value .." */
            return WidgetOptionConfigure(interp, widget, objc - skip,
                &objv[skip]);
        }
        Tcl_WrongNumArgs(interp, skip, objv, "?-option value ..?");
        return TCL_ERROR;
    }
    /* configure "command .." */
    result =
        Tcl_GetIndexFromObj(interp, objv[skip], commandNames, "option", 0,
        &index);
    if(result != TCL_OK) {
        return result;
    }
    switch (index) {
    case COMMAND_OPTIONADD:
        dbclass = NULL;
        defvalue = NULL;
        flags = NULL;
        if(objc - skip == 3) {  /* configure add option dbname */
            ;
        } else if(objc - skip == 4) {   /* configure add option dbname dbclass */
            dbclass = objv[skip + 3];
        } else if(objc - skip == 5) {   /* configure add option dbname dbclass defvalue */
            dbclass = objv[skip + 3];
            defvalue = objv[skip + 4];
        } else if(objc - skip == 6) {   /* configure add option dbname dbclass defvalue flags */
            dbclass = objv[skip + 3];
            defvalue = objv[skip + 4];
            flags = objv[skip + 5];
        } else {
            Tcl_WrongNumArgs(interp, skip + 1, objv,
                "option ?synonym?|?dbname dbclass ?default? ?flags??");
            return TCL_ERROR;
        }
        return (WidgetOptionAdd(interp, widget, objv[skip + 1], objv[skip + 2],
                dbclass, defvalue, flags, NULL, 0));
    case COMMAND_INIT:
        // collect all not readonly options and configure
        Tcl_Preserve(widget);
        myObjv[0] = widget->myCmd;
        entryPtr = Tcl_FirstHashEntry(&widget->optionsTable, &search);
        while(entryPtr != NULL) {
            optionPtr = Tcl_GetHashValue(entryPtr);
            if(optionPtr->dbclass == NULL) {    /* synonym option */
                if(optionPtr->value) {
                    Tcl_ObjSetVar2(interp, widget->optionsArray,
                        optionPtr->dbname, optionPtr->value, TCL_GLOBAL_ONLY);
                    Tcl_DecrRefCount(optionPtr->value);
                    optionPtr->value = NULL;
                }
            } else {    /* normal option */
                if(optionPtr->flags == 0) {
                    myObjv[1] = optionPtr->option;
                    if(Tcl_EvalObjv(interp, 2, myObjv,
                            TCL_EVAL_GLOBAL) != TCL_OK) {
                        retPtr = Tcl_GetObjResult(interp);
                        Tcl_IncrRefCount(retPtr);
                        Tcl_Release(widget);
                        Tcl_DeleteCommandFromToken(interp, widget->widgetCmd);
                        Tcl_SetObjResult(interp, retPtr);
                        Tcl_DecrRefCount(retPtr);
                        return TCL_ERROR;
                    }
                }
            }
            entryPtr = Tcl_NextHashEntry(&search);
        }
        myObjv[1] = TkoObj._tko_configure;
        if(Tcl_EvalObjv(interp, 2, myObjv, TCL_EVAL_GLOBAL) != TCL_OK) {
            retPtr = Tcl_GetObjResult(interp);
            Tcl_IncrRefCount(retPtr);
            Tcl_Release(widget);
            Tcl_DeleteCommandFromToken(interp, widget->widgetCmd);
            Tcl_SetObjResult(interp, retPtr);
            Tcl_DecrRefCount(retPtr);
            return TCL_ERROR;
        }
        Tcl_Release(widget);
        return TCL_OK;
    case COMMAND_OPTIONDEL:
        if(objc - skip == 2) {
            return (WidgetOptionDel(interp, widget, objv[skip + 1]));
        }
        Tcl_WrongNumArgs(interp, skip + 1, objv, "option");
        return TCL_ERROR;
    case COMMAND_OPTIONVAR:
        if(objc - skip == 1) {
            Tcl_SetObjResult(interp, widget->optionsArray);
            return TCL_OK;
        }
        Tcl_WrongNumArgs(interp, skip + 1, objv, "");
        return TCL_ERROR;
    }
    return TCL_ERROR;   /* supress compiler warning */
}

/*
 * WidgetOptionAdd --
 *
 * Results:
 *
 * Side effects:
 */
static int
WidgetOptionAdd(
    Tcl_Interp * interp,
    tkoWidget * widget,
    Tcl_Obj * option,
    Tcl_Obj * dbname,
    Tcl_Obj * dbclass,
    Tcl_Obj * defvalue,
    Tcl_Obj * flags,
    Tcl_Obj * value,
    int initmode)
{
    Tcl_HashEntry *entryPtr;
    WidgetOption *optionPtr;
    Tk_Uid valueUid;
    int isNew;
    Tk_Uid optionUid;
    Tk_Uid dbnameUid;
    Tk_Uid dbclassUid;
    int intFlags;
    Tcl_Obj *myObjv[2];
    const char *ch;

    if(option == NULL || (ch = Tcl_GetString(option))[0] != '-') {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("missing or wrong option",
                -1));
        return TCL_ERROR;
    }
    if(dbname == NULL) {
        Tcl_SetObjResult(interp,
            Tcl_ObjPrintf("missing dbname for option \"%s\"", ch));
        return TCL_ERROR;
    }
    /* synonym option check */
    if(dbclass == NULL) {
        if(Tcl_GetString(dbname)[0] != '-') {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("wrong synonym name for option \"%s\"", ch));
            return TCL_ERROR;
        }
    }
    /* int flag */
    intFlags = 0;
    if(flags != NULL && Tcl_GetIntFromObj(interp, flags, &intFlags) != TCL_OK) {
        Tcl_SetObjResult(interp,
            Tcl_ObjPrintf("wrong flags \"%s\" for option \"%s\"",
                Tcl_GetString(flags), ch));
        return TCL_ERROR;
    }
    /* return if no widget given, all class checks are done */
    if(widget == NULL) {
        return TCL_OK;
    }
    optionUid = Tk_GetUid(ch);
    dbnameUid = Tk_GetUid(Tcl_GetString(dbname));
    entryPtr = Tcl_CreateHashEntry(&widget->optionsTable, optionUid, &isNew);
    if(isNew == 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("option \"%s\" exists", ch));
        return TCL_ERROR;
    }
    /* create option */
    optionPtr = (WidgetOption *) ckalloc(sizeof(WidgetOption));
    optionPtr->option = option;
    Tcl_IncrRefCount(optionPtr->option);
    optionPtr->dbname = dbname;
    Tcl_IncrRefCount(optionPtr->dbname);
    optionPtr->dbclass = NULL;
    optionPtr->defvalue = NULL;
    optionPtr->value = NULL;
    optionPtr->flags = intFlags;
    Tcl_SetHashValue(entryPtr, (char *)optionPtr);
    /* synonym options can have init value */
    if(dbclass == NULL) {
        if(value) {
            optionPtr->value = value;
            Tcl_IncrRefCount(optionPtr->value);
        }
        /* normal option */
    } else {
        dbclassUid = Tk_GetUid(Tcl_GetString(dbclass));
        optionPtr->dbclass = dbclass;
        Tcl_IncrRefCount(optionPtr->dbclass);
        if(defvalue) {
            optionPtr->defvalue = defvalue;
        } else {
            optionPtr->defvalue = TkoObj.empty;
        }
        Tcl_IncrRefCount(optionPtr->defvalue);
        /*
         * If value is given use it.
         */
        if(value) {
            optionPtr->value = value;
        } else {
            /*
             * Get value from option database
             */
            if(optionPtr->value == NULL) {
                valueUid = Tk_GetOption(widget->tkWin, dbnameUid, dbclassUid);
                if(valueUid != NULL) {
                    optionPtr->value = Tcl_NewStringObj(valueUid, -1);
                }
            }
            /*
             * Check for a system-specific default value.
             * Do not for -class because Tcl_SetClass was not called.
             * When -class is not first option (after -screen) we get a crash!
             */
            if(optionPtr->value == NULL && optionUid != TkoUid_class) {
                optionPtr->value =
                    TkpGetSystemDefault(widget->tkWin, dbnameUid, dbclassUid);
            }
            /*
             * Use default value.
             */
            if(optionPtr->value == NULL) {
                optionPtr->value = defvalue;
            }
        }
        /*
         * No given value defaults to empty string.
         */
        if(optionPtr->value == NULL) {
            optionPtr->value = TkoObj.empty;
        }
        Tcl_IncrRefCount(optionPtr->value);
        /* 
         * Set default value of option in array variable.
         */
        if(Tcl_ObjSetVar2(interp, widget->optionsArray, option,
                optionPtr->value, TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG) == NULL) {
            goto error;
        }
        if(intFlags & TKO_WIDGETOPTIONREADONLY) {
            /*
             * Readonly options can only be set on creation time.
             * All other attempts will be refused in the trace function.
             * Therefore we do not have to remove the -option method.
             */
            if(initmode) {
                myObjv[0] = widget->myCmd;
                myObjv[1] = option;
                if(Tcl_EvalObjv(interp, 2, myObjv, TCL_EVAL_GLOBAL) != TCL_OK) {
                    goto error;
                }
            }
            /*
             * We set the value again because the -option method may have changed it.
             */
            if(optionPtr->value) {
                Tcl_DecrRefCount(optionPtr->value);
            }
            optionPtr->value = Tcl_ObjGetVar2(interp, widget->optionsArray, option, TCL_GLOBAL_ONLY);   /*TODO flags? */
            Tcl_IncrRefCount(optionPtr->value);
        }
    }
    return TCL_OK;
  error:
    WidgetOptionDelEntry(entryPtr);
    return TCL_ERROR;
}

/*
 * WidgetOptionConfigure --
 *
 * Results:
 *
 * Side effects:
 */
static int
WidgetOptionConfigure(
    Tcl_Interp * interp,
    tkoWidget * widget,
    int objc,
    Tcl_Obj * const objv[])
{
    int i;
    Tcl_Obj *myObjv[2];

    if(objc % 2) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing value"));
        return TCL_ERROR;
    }
    Tcl_Preserve(widget);
    for(i = 0; i < objc; i = i + 2) {
        if(WidgetOptionSet(interp, widget, objv[i], objv[i + 1]) != TCL_OK) {
            Tcl_Release(widget);
            return TCL_ERROR;
        }
    }
    myObjv[0] = widget->myCmd;
    myObjv[1] = TkoObj._tko_configure;
    if(Tcl_EvalObjv(interp, 2, myObjv, TCL_EVAL_GLOBAL) != TCL_OK) {
        Tcl_Release(widget);
        return TCL_ERROR;
    }
    Tcl_Release(widget);
    return TCL_OK;
}

/*
 * WidgetOptionDel --
 *
 * Results:
 *
 * Side effects:
 */
static int
WidgetOptionDel(
    Tcl_Interp * interp,
    tkoWidget * widget,
    Tcl_Obj * option)
{
Tcl_HashEntry *entryPtr;

    if(option == NULL) {
        return TCL_ERROR;
    }
    /* delete single option */
    entryPtr =
        Tcl_FindHashEntry(&widget->optionsTable,
        Tk_GetUid(Tcl_GetString(option)));
    if(entryPtr == NULL) {
        Tcl_SetObjResult(interp,
            Tcl_ObjPrintf("unknown option \"%s\"", Tcl_GetString(option)));
        return TCL_ERROR;
    }
    /* delete with no additional check on synonym option */
    Tcl_UnsetVar2(interp, Tcl_GetString(widget->optionsArray),
        Tcl_GetString(option), TCL_GLOBAL_ONLY);
    WidgetOptionDelEntry(entryPtr);

    return TCL_OK;
}

/*
 * WidgetOptionGet --
 *
 * Results:
 *
 * Side effects:
 */
static int
WidgetOptionGet(
    Tcl_Interp * interp,
    tkoWidget * widget,
    Tcl_Obj * option)
{
Tcl_Obj *retPtr;
Tcl_HashEntry *entryPtr;
WidgetOption *optionPtr;

    if(option == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("no option given"));
        return TCL_ERROR;
    }
    entryPtr =
        Tcl_FindHashEntry(&widget->optionsTable,
        Tk_GetUid(Tcl_GetString(option)));
    if(entryPtr == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown option \"%s\"",
                Tcl_GetString(option)));
        return TCL_ERROR;
    }
    optionPtr = Tcl_GetHashValue(entryPtr);
    /* synonym option */
    if(optionPtr->dbclass == NULL) {
        entryPtr =
            Tcl_FindHashEntry(&widget->optionsTable,
            Tk_GetUid(Tcl_GetString(optionPtr->dbname)));
        if(entryPtr == NULL) {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("unknown synonym option \"%s\"",
                    Tcl_GetString(option)));
            return TCL_ERROR;
        }
        optionPtr = Tcl_GetHashValue(entryPtr);
        if(optionPtr->dbclass == NULL) {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("synonym option is synonym \"%s\"",
                    Tcl_GetString(option)));
            return TCL_ERROR;
        }
    }
    retPtr = optionPtr->value;
    Tcl_SetObjResult(interp, retPtr);
    return TCL_OK;
}

/*
 * WidgetOptionSet --
 *
 * Results:
 *
 * Side effects:
 */
static int
WidgetOptionSet(
    Tcl_Interp * interp,
    tkoWidget * widget,
    Tcl_Obj * option,
    Tcl_Obj * value)
{
Tcl_HashEntry *entryPtr;
WidgetOption *optionPtr;

    if(option == NULL || value == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing option and/or value"));
        return TCL_ERROR;
    }
    entryPtr =
        Tcl_FindHashEntry(&widget->optionsTable,
        Tk_GetUid(Tcl_GetString(option)));
    if(entryPtr == NULL) {
        Tcl_SetObjResult(interp,
            Tcl_ObjPrintf("unknown option \"%s\"", Tcl_GetString(option)));
        return TCL_ERROR;
    }
    optionPtr = Tcl_GetHashValue(entryPtr);
    /* synonym option */
    if(optionPtr->dbclass == NULL) {
        entryPtr =
            Tcl_FindHashEntry(&widget->optionsTable,
            Tk_GetUid(Tcl_GetString(optionPtr->dbname)));
        if(entryPtr == NULL) {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("unknown synonym option \"%s\"",
                    Tcl_GetString(option)));
            return TCL_ERROR;
        }
        optionPtr = Tcl_GetHashValue(entryPtr);
        if(optionPtr->dbclass == NULL) {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("synonym option is synonym \"%s\"",
                    Tcl_GetString(option)));
            return TCL_ERROR;
        }
        if(Tcl_ObjSetVar2(interp, widget->optionsArray, optionPtr->option,
                value, TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG) == NULL) {
            return TCL_ERROR;
        }
    } else {
        if(Tcl_ObjSetVar2(interp, widget->optionsArray, option, value,
                TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG) == NULL) {
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

/*
 * TkoWidgetOptionGet --
 *
 * Results:
 *
 * Side effects:
 */
Tcl_Obj *
TkoWidgetOptionGet(
    Tcl_Interp * interp,
    Tcl_Object object,
    Tcl_Obj * option)
{
tkoWidget *widget = (tkoWidget *) Tcl_ObjectGetMetadata(object, &tkoWidgetMeta);
    if(widget == NULL) {
        return NULL;
    }
    return Tcl_ObjGetVar2(interp, widget->optionsArray, option,
        TCL_GLOBAL_ONLY);
}

/*
 * TkoWidgetOptionSet --
 *
 * Results:
 *
 * Side effects:
 */
int
TkoWidgetOptionSet(
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    Tcl_Obj * option,
    tkoWidgetOptionType type,
    Tcl_ObjectMetadataType * meta,
    size_t offset)
{
    Tcl_Object object;
    tkoWidget *widget;
    Tcl_Obj *value;
    char *address = NULL;
    int intVal;
    double dblVal;
    Colormap colormap;
    int *intPtr;
    const char *str;
    int length;
    int pixels[4] = { 0, 0, 0, 0 };
    int objc;
    Tcl_Obj **objv;
    Visual * visual;
	XColor * color;
    Tk_3DBorder border;
    Tk_Anchor anchor;
    Tk_Cursor cursor;
    Tk_Window newWin;
    Tk_Font newFont;
    Tk_Justify justify;

    if((object = Tcl_ObjectContextObject(context)) == NULL
        || (widget =
            (tkoWidget *) Tcl_ObjectGetMetadata(object, &tkoWidgetMeta)) == NULL
        || (value =
            Tcl_ObjGetVar2(interp, widget->optionsArray, option,
                TCL_GLOBAL_ONLY)) == NULL || widget->tkWin == NULL) {
        return TCL_ERROR;
    }
    if(meta) {
        if((address = Tcl_ObjectGetMetadata(object, meta)) == NULL) {
            return TCL_ERROR;
        }
        address += offset;
    }

    switch (type) {
    case TKO_SET_CLASS:        /* (Tcl_Obj **)address */
        Tk_SetClass(widget->tkWin, Tcl_GetString(value));
        if(address) {
            if(*((Tcl_Obj **) address) != NULL)
                Tcl_DecrRefCount(*((Tcl_Obj **) address));
            *((Tcl_Obj **) address) = value;
            Tcl_IncrRefCount(value);
        }
        return TCL_OK;
    case TKO_SET_VISUAL:       /* (Tcl_Obj **)address */
        visual =
            Tk_GetVisual(interp, widget->tkWin, Tcl_GetString(value), &intVal,
            &colormap);
        if(visual == NULL)
            return TCL_ERROR;
        Tk_SetWindowVisual(widget->tkWin, visual, intVal, colormap);
        if(address) {
            if(*((Tcl_Obj **) address) != NULL)
                Tcl_DecrRefCount(*((Tcl_Obj **) address));
            *((Tcl_Obj **) address) = value;
            Tcl_IncrRefCount(value);
        }
        return TCL_OK;
    case TKO_SET_COLORMAP:     /* (Tcl_Obj **)address */
        str = Tcl_GetStringFromObj(value, &length);
        if(str && length) {
            colormap = Tk_GetColormap(interp, widget->tkWin, str);
            if(colormap == None)
                return TCL_ERROR;
            Tk_SetWindowColormap(widget->tkWin, colormap);
        }
        if(address) {
            if(*((Tcl_Obj **) address) != NULL)
                Tcl_DecrRefCount(*((Tcl_Obj **) address));
            *((Tcl_Obj **) address) = value;
            Tcl_IncrRefCount(value);
        }
        return TCL_OK;
    case TKO_SET_USENULL:      /* (Tcl_Obj **)address */
        str = Tcl_GetStringFromObj(value, &length);
        if(str && length) {
            if(TkpUseWindow(interp, widget->tkWin, str) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        if(address) {
            if(*((Tcl_Obj **) address) != NULL)
                Tcl_DecrRefCount(*((Tcl_Obj **) address));
            if(length) {
                *((Tcl_Obj **) address) = value;
                Tcl_IncrRefCount(value);
            } else {
                *((Tcl_Obj **) address) = NULL;
            }
        }
        return TCL_OK;
    case TKO_SET_CONTAINER:    /* (int *)address */
        if(Tcl_GetBooleanFromObj(interp, value, &intVal) != TCL_OK)
            return TCL_ERROR;
        if(intVal) {
            TkpMakeContainer(widget->tkWin);
            Tcl_ObjSetVar2(interp, widget->optionsArray, option, TkoObj._1,
                TCL_GLOBAL_ONLY);
        } else {
            Tcl_ObjSetVar2(interp, widget->optionsArray, option, TkoObj._0,
                TCL_GLOBAL_ONLY);
        }
        if(address) {
            *(int *)address = intVal;
        }
        return TCL_OK;
    case TKO_SET_TCLOBJ:       /* (Tcl_Obj **)address */
        if(address) {
            if(*((Tcl_Obj **) address) != NULL)
                Tcl_DecrRefCount(*((Tcl_Obj **) address));
            *((Tcl_Obj **) address) = value;
            Tcl_IncrRefCount(value);
        }
        return TCL_OK;
    case TKO_SET_XCOLOR:       /* (Xcolor **)address */
        color = Tk_AllocColorFromObj(interp, widget->tkWin, value);
        if(color == NULL)
            return TCL_ERROR;
        if(address) {
            if(*((XColor **) address) != NULL) {
                Tk_FreeColor(*((XColor **) address));
            }
            *((XColor **) address) = color;
        } else {
            Tk_FreeColor(color);
        }
        return TCL_OK;
    case TKO_SET_3DBORDER:     /* (Tk_3DBorder *)address */
        border = Tk_Alloc3DBorderFromObj(interp, widget->tkWin, value);
        if(border == NULL)
            return TCL_ERROR;
        if(address) {
            if(*(Tk_3DBorder *) address != NULL) {
                Tk_Free3DBorder(*(Tk_3DBorder *) address);
            }
            *(Tk_3DBorder *) address = border;
        } else {
            Tk_Free3DBorder(border);
        }
        return TCL_OK;
    case TKO_SET_PIXEL:        /* (int *)address */
        if(Tk_GetPixelsFromObj(interp, widget->tkWin, value, &intVal) != TCL_OK) {
            return TCL_ERROR;
        }
        if(address) {
            *(int *)address = intVal;
        }
        Tcl_ObjSetVar2(interp, widget->optionsArray, option,
            Tcl_NewIntObj(intVal), TCL_GLOBAL_ONLY);
        return TCL_OK;
    case TKO_SET_PIXELNONEGATIV:       /* (int *)address */
        if(Tk_GetPixelsFromObj(interp, widget->tkWin, value, &intVal) != TCL_OK) {
            return TCL_ERROR;
        }
        if(intVal >= SHRT_MAX) {
            Tcl_AppendResult(interp, "bad distance \"", Tcl_GetString(value),
                "\": ", "too big to represent", (char *)NULL);
            return TCL_ERROR;
        }
        if(intVal < 0) {
            Tcl_AppendResult(interp, "bad distance \"", Tcl_GetString(value),
                "\": ", "can't be negative", (char *)NULL);
            return TCL_ERROR;
        }
        if(address) {
            *(int *)address = intVal;
        }
        Tcl_ObjSetVar2(interp, widget->optionsArray, option,
            Tcl_NewIntObj(intVal), TCL_GLOBAL_ONLY);
        return TCL_OK;
    case TKO_SET_PIXELPOSITIV: /* (int *)address */
        if(Tk_GetPixelsFromObj(interp, widget->tkWin, value, &intVal) != TCL_OK) {
            return TCL_ERROR;
        }
        if(intVal >= SHRT_MAX) {
            Tcl_AppendResult(interp, "bad distance \"", Tcl_GetString(value),
                "\": ", "too big to represent", (char *)NULL);
            return TCL_ERROR;
        }
        if(intVal <= 0) {
            Tcl_AppendResult(interp, "bad distance \"", Tcl_GetString(value),
                "\": ", "must be positive", (char *)NULL);
            return TCL_ERROR;
        }
        if(address) {
            *(int *)address = intVal;
        }
        Tcl_ObjSetVar2(interp, widget->optionsArray, option,
            Tcl_NewIntObj(intVal), TCL_GLOBAL_ONLY);
        return TCL_OK;
    case TKO_SET_DOUBLE:       /* (double *)address */
        if(Tcl_GetDoubleFromObj(interp, value, &dblVal) != TCL_OK) {
            return TCL_ERROR;
        }
        if(address) {
            *(double *)address = dblVal;
        }
        Tcl_ObjSetVar2(interp, widget->optionsArray, option,
            Tcl_NewDoubleObj(dblVal), TCL_GLOBAL_ONLY);
        return TCL_OK;
    case TKO_SET_BOOLEAN:      /* (int *)address */
        if(Tcl_GetBooleanFromObj(interp, value, &intVal) != TCL_OK) {
            return TCL_ERROR;
        }
        if(intVal) {
            Tcl_ObjSetVar2(interp, widget->optionsArray, option, TkoObj._1,
                TCL_GLOBAL_ONLY);
        } else {
            Tcl_ObjSetVar2(interp, widget->optionsArray, option, TkoObj._0,
                TCL_GLOBAL_ONLY);
        }
        if(address) {
            *(int *)address = intVal;
        }
        Tcl_ObjSetVar2(interp, widget->optionsArray, option,
            Tcl_NewIntObj(intVal), TCL_GLOBAL_ONLY);
        return TCL_OK;
    case TKO_SET_CURSOR:       /* (Tk_Cursor *)address */
        cursor = None;
        if(Tcl_GetString(value)[0] != '\0') {
            cursor = Tk_AllocCursorFromObj(interp, widget->tkWin, value);
            if(cursor == None) {
                return TCL_ERROR;
            }
            Tk_DefineCursor(widget->tkWin, cursor);
        }
        if(address) {
            if(*(Tk_Cursor *) address != None) {
                Tk_FreeCursor(Tk_Display(widget->tkWin),
                    *(Tk_Cursor *) address);
            }
            *(Tk_Cursor *) address = cursor;
        } else {
            if(cursor != None) {
                Tk_FreeCursor(Tk_Display(widget->tkWin), cursor);       /*TODO necessary? */
            }
        }
        return TCL_OK;
    case TKO_SET_INT:  /* (int *)address */
        if(Tcl_GetIntFromObj(interp, value, &intVal) != TCL_OK) {
            return TCL_ERROR;
        }
        if(address) {
            *(int *)address = intVal;
        }
        Tcl_ObjSetVar2(interp, widget->optionsArray, option,
            Tcl_NewIntObj(intVal), TCL_GLOBAL_ONLY);
        return TCL_OK;
    case TKO_SET_RELIEF:       /* (int *)address */
        if(Tk_GetReliefFromObj(interp, value, &intVal) != TCL_OK) {
            return TCL_ERROR;
        }
        if(address) {
            *(int *)address = intVal;
        }
        return TCL_OK;
    case TKO_SET_ANCHOR:       /* (Tk_Anchor *)address */
        if(Tk_GetAnchorFromObj(interp, value, &anchor) != TCL_OK) {
            return TCL_ERROR;
        }
        if(address) {
            *(Tk_Anchor *) address = anchor;
        }
        return TCL_OK;
    case TKO_SET_WINDOW:       /* (Tk_Window *)address */
        if(value == NULL || Tcl_GetCharLength(value) == 0) {
            newWin = None;
        } else {
            if(TkGetWindowFromObj(interp, widget->tkWin, value,
                    &newWin) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        if(address) {
            *(Tk_Window *) address = newWin;
        }
        return TCL_OK;
    case TKO_SET_FONT: /* (Tk_Font *)address */
        newFont = Tk_AllocFontFromObj(interp, widget->tkWin, value);
        if(newFont == NULL) {
            return TCL_ERROR;
        }
        if(address) {
            if(*(Tk_Font *) address != NULL) {
                Tk_FreeFont(*(Tk_Font *) address);
            }
            *(Tk_Font *) address = newFont;
        } else {
            Tk_FreeFont(newFont);
        }
        return TCL_OK;
    case TKO_SET_STRING:       /* (char **)address */
        if(address) {
            str = Tcl_GetStringFromObj(value, &length);
            if(*(char **)address != NULL) {
                ckfree(*(char **)address);
            }
            *(char **)address = ckalloc(length + 1);
            memcpy(*(char **)address, str, length + 1);
        }
        return TCL_OK;
    case TKO_SET_STRINGNULL:   /* (char **)address */
        if(address) {
            str = Tcl_GetStringFromObj(value, &length);
            if(*(char **)address != NULL) {
                ckfree(*(char **)address);
            }
            if(length == 0) {
                *(char **)address = NULL;
            } else {
                *(char **)address = ckalloc(length + 1);
                memcpy(*(char **)address, str, length + 1);
            }
        }
        return TCL_OK;
    case TKO_SET_SCROLLREGION: /* (int *[4])address */
        if(Tcl_ListObjGetElements(interp, value, &objc, &objv) != TCL_OK) {
            return TCL_ERROR;
        }
        if(objc == 4) {
            if(Tk_GetPixelsFromObj(interp, widget->tkWin, objv[0],
                    &pixels[0]) != TCL_OK
                || Tk_GetPixelsFromObj(interp, widget->tkWin, objv[1],
                    &pixels[1]) != TCL_OK
                || Tk_GetPixelsFromObj(interp, widget->tkWin, objv[2],
                    &pixels[2]) != TCL_OK
                || Tk_GetPixelsFromObj(interp, widget->tkWin, objv[3],
                    &pixels[3]) != TCL_OK) {
                return TCL_ERROR;
            }
        } else if(objc != 0) {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("found %d instead of 4 values", objc));
            return TCL_ERROR;
        }
        if(address) {
            intPtr = (int *)address;
            intPtr[0] = pixels[0];
            intPtr[1] = pixels[1];
            intPtr[2] = pixels[2];
            intPtr[3] = pixels[3];
        }
        return TCL_OK;
    case TKO_SET_JUSTIFY:      /* (Tk_Justify *)address */
        if(Tk_GetJustify(interp, Tk_GetUid(Tcl_GetString(value)),
                &justify) != TCL_OK) {
            return TCL_ERROR;
        }
        if(address) {
            *(Tk_Justify *) address = justify;
        }
        return TCL_OK;
    }

    Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown type \"%d\"", type));
    return TCL_ERROR;
}

/*
 * WidgetOptionTrace --
 *
 * Write trace on option array variable
 *
 * Results:
 *
 * Side effects:
 */
static char *
WidgetOptionTrace(
    ClientData clientData,
    Tcl_Interp * interp,
    const char *name1,
    const char *name2,
    int flags)
{
    tkoWidget *widget = (tkoWidget *) clientData;
    Tcl_HashEntry *entryPtr;
    Tcl_Obj *valuePtr;
    //    const char *result;
    WidgetOption *optionPtr;
    Tcl_Obj *myObjv[2];
    Tcl_Obj *myRet;

    /* get new value */
    entryPtr = Tcl_FindHashEntry(&widget->optionsTable, Tk_GetUid(name2));
    if(entryPtr == NULL) {
        myRet = Tcl_ObjPrintf("option \"%s\" not found", name2);
        Tcl_IncrRefCount(myRet);
        return (char *)myRet;
    }
    optionPtr = (WidgetOption *) Tcl_GetHashValue(entryPtr);
    if(optionPtr->flags & TKO_WIDGETOPTIONREADONLY) {
        myRet = Tcl_ObjPrintf("option \"%s\" is readonly", name2);
        Tcl_IncrRefCount(myRet);
        return (char *)myRet;
    }
    myObjv[0] = widget->myCmd;
    myObjv[1] = optionPtr->option;
    if(Tcl_EvalObjv(interp, 2, myObjv, TCL_EVAL_GLOBAL) != TCL_OK) {
        myRet = Tcl_GetObjResult(interp);
        Tcl_IncrRefCount(myRet);
        /* reset to old value TODO checks? */
        if(optionPtr->value != NULL) {
            Tcl_ObjSetVar2(interp, widget->optionsArray, optionPtr->option,
                optionPtr->value, TCL_GLOBAL_ONLY);
            Tcl_EvalObjv(interp, 2, myObjv, TCL_EVAL_GLOBAL);
        }
        return (char *)myRet;
    }
    if(optionPtr->value != NULL) {
        Tcl_DecrRefCount(optionPtr->value);
    }
    valuePtr = Tcl_ObjGetVar2(interp, widget->optionsArray, optionPtr->option, TCL_GLOBAL_ONLY);        /*TODO flags? */
    optionPtr->value = valuePtr;
    Tcl_IncrRefCount(optionPtr->value);
    return NULL;
}

/*
 * WidgetOptionDelEntry --
 *
 * Results:
 *
 * Side effects:
 */
static void
WidgetOptionDelEntry(
    Tcl_HashEntry * entry)
{
WidgetOption *optionPtr = Tcl_GetHashValue(entry);
    if(optionPtr->option)
        Tcl_DecrRefCount(optionPtr->option);
    if(optionPtr->dbname)
        Tcl_DecrRefCount(optionPtr->dbname);
    if(optionPtr->dbclass)
        Tcl_DecrRefCount(optionPtr->dbclass);
    if(optionPtr->defvalue)
        Tcl_DecrRefCount(optionPtr->defvalue);
    if(optionPtr->value)
        Tcl_DecrRefCount(optionPtr->value);
    ckfree(optionPtr);
    Tcl_DeleteHashEntry(entry);
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
