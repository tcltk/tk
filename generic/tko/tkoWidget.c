/*
 * tkoWidget.c --
 *
 *    This file contains the tko widget class.
 *
 * Copyright (c) 2019 Rene Zaumseil
 *
 */

#include "tkoWidget.h"
#include "tclOOInt.h" /*TODO needed for Widget_GetClassName() below */

/*
 * Widget_GetClassName --
 *    Return class name of object.
 *    Should be OO core function.
 *
 * Results:
 *    Name of class or NULL on error.
 *
 * Side effects:
 *    Use internal OO structures!!!
 */
Tcl_Obj *
Widget_GetClassName(
    Tcl_Interp * interp,
    Tcl_Object object)
{
    Tcl_Object classPtr;
    classPtr = (Tcl_Object)(((Object *)object)->selfCls->thisPtr);
    if (classPtr == NULL) return NULL;

    return Tcl_GetObjectName(interp, classPtr);
}

/*
 * Widget option.
 */
typedef struct WidgetOption {
    Tcl_Obj *option;           /* Name of option */
    Tcl_Obj *dbname;           /* Database name or name of synonym option */
    Tcl_Obj *dbclass;          /* Class name or NULL for synonym options */
    Tcl_Obj *defvalue;         /* Default value from initialization */
    Tcl_Obj *flags;            /* Default value from initialization */
    Tcl_Obj *value;            /* Contain last known value of option */
    int flagbits;               /* see flags in struct Tko_WidgetOptionDefine */
} WidgetOption;

/*
 * Clientdata of option methods.
 */
typedef struct WidgetClientdata {
    Tcl_MethodType method;
    Tcl_Obj *option;
    int offset;
    int type;
    int flags;
} WidgetClientdata;

typedef struct TkoThreadData {
    /* UID of class sctring */
    Tk_Uid Uid_class;
    Tk_Uid Uid_empty;
    /* Static string objects.  */
    Tcl_Obj *Obj_empty; /* "" */
    Tcl_Obj *Obj_tko__option; /* "::tko::_option" */
    Tcl_Obj *Obj_tko__eventoption; /* "::tko::_eventoption" */
    Tcl_Obj *Obj_next; /* "next" */
    Tcl_Obj *Obj_uplevel; /* "::uplevel" */
    Tcl_Obj *Obj_oo_define; /* "::oo::define" */
    Tcl_Obj *Obj_oo_objdefine; /* "::oo::objdefine" */
    Tcl_Obj *Obj_method; /* "method" */
    Tcl_Obj *Obj__tko_configure; /* "_tko_configure" */
    Tcl_Obj *Obj__tko; /* "_tko" */
    Tcl_Obj *Obj_cget; /* "cget" */
    Tcl_Obj *Obj_configure; /* "configure" */
    Tcl_Obj *Obj_tko; /* "::tko" */
    Tcl_Obj *Obj_tko_widget; /* "::tko::widget" */
    Tcl_Obj *Obj_lsort; /* "::lsort" */
    Tcl_Obj *Obj_point; /* "." */
    Tcl_Obj *Obj_point2; /* ".." */
    Tcl_Obj *Obj__screen; /* "-screen" */
    Tcl_Obj *Obj_flags_r; /* "r" */
    Tcl_Obj *Obj_flags_rh; /* "rh" */
    Tcl_Obj *Obj_flags_h; /* "h" */
    Tcl_Obj *Obj_rename; /* "rename" */
    Tcl_Obj *Obj_tko__self; /* "::tko::_self" */
} TkoThreadData;
static Tcl_ThreadDataKey tkoKey;

/*
 * Methods
 */
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
static int WidgetMethod_tko_configure(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int WidgetMethod_tko(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);

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
static void WidgetEventChanged(
    Tko_Widget *widget);
static int WidgetOptionAdd(
    Tcl_Interp * interp,
    Tko_Widget * widget,
    Tcl_Obj * option,
    Tcl_Obj * dbname,
    Tcl_Obj * dbclass,
    Tcl_Obj * defvalue,
    Tcl_Obj * flags,
    Tcl_Obj * value,
    int initmode);
static int WidgetOptionGet(
    Tcl_Interp * interp,
    Tko_Widget * widget,
    Tcl_Obj * option);
static int WidgetOptionSet(
    Tcl_Interp * interp,
    Tko_Widget * widget,
    Tcl_Obj * option,
    Tcl_Obj * value);
static void WidgetMetaDestroy(
    Tko_Widget * widget);
static void WidgetMetaDelete(
    ClientData clientData);
static int WidgetMethod_(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int WidgetFlagsObj(
    Tcl_Obj *flagsPtr,
    int *flags);
static int WidgetFlagsHideGet(
    Tcl_Obj *flags);
static Tcl_Obj *WidgetFlagsHideSet(
    Tcl_Obj *flags);
static Tcl_Obj *WidgetFlagsHideUnset(
    Tcl_Obj *flags);
static void WidgetClientdataDelete(
    ClientData clientdata);
static int WidgetClientdataClone(
    Tcl_Interp *interp,
    ClientData clientdata,
    ClientData *newPtr);
static int WidgetDestructor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int WidgetWrapConstructor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static int WidgetClassConstructor(
    ClientData clientData,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[]);
static void WidgetDeleteTkwin(
    Tko_Widget *widget);

/* List of all internally defined public and private methods. */
#define TKO_1 TCL_OO_METHOD_VERSION_CURRENT
static Tcl_MethodType tkoWidgetMethods[] = {
    { TKO_1, NULL, WidgetClassConstructor, NULL, NULL },
    { TKO_1, NULL, WidgetWrapConstructor, NULL, NULL },
    { TKO_1, NULL, WidgetDestructor, NULL, NULL },
    { TKO_1, "cget", WidgetMethod_cget, NULL, NULL },
    { TKO_1, "configure", WidgetMethod_configure, NULL, NULL },
    { TKO_1, "_tko_configure", WidgetMethod_tko_configure, NULL, NULL },
    { TKO_1, "_tko", WidgetMethod_tko, NULL, NULL },
};

/*
 * tkoWidgetMeta --
 *    Identifier for attached tko widget data.
 */
Tcl_ObjectMetadataType tkoWidgetMeta = {
    TCL_OO_METADATA_VERSION_CURRENT,
    "tkoWidgetMeta",
    WidgetMetaDelete,
    NULL
};

/*
* Tko_TkoObjCmd --
*    Implementation of the "::tko" command.
*    Initialization of new widgets.
*    Configuration of widget class options.
*
* Results:
*    A standard Tcl result.
*
* Side effects:
*    Create available oo::class tko widgets.
*    Add, delete return, hide and show options.
*/
int
Tko_TkoObjCmd(
    ClientData dummy,    /* Not used. */
    Tcl_Interp *interp,        /* Current interpreter. */
    int objc,            /* Number of arguments. */
    Tcl_Obj *const objv[])    /* Argument objects. */
{
    static const char *const myOptions[] = {
        "initclass", "initfrom", "initwrap",
        "eventoption",
        "optiondef", "optiondel","optionget",
        "optionhide","optionshow",NULL
    };
    enum options {
        MY_INITCLASS, MY_INITFROM, MY_INITWRAP,
        MY_EVENTOPTION,
        MY_OPTIONDEF, MY_OPTIONDEL, MY_OPTIONGET,
        MY_OPTIONHIDE, MY_OPTIONSHOW
    };
    int index;
    Tcl_Obj *dictPtr;
    Tcl_Obj *namePtr;
    Tcl_Obj *listPtr;
    int ret;
    int i;
    Tcl_DictSearch search;
    Tcl_Obj *key, *value;
    int argObjc;
    Tcl_Obj **argObjv;
    int done;
    Tcl_Obj *myCmd[6];
    const char *ch, *ch1;
    int length;
    Tcl_Obj *tmpPtr;
    Tcl_Class clazz;
    Tcl_Object object;
    TkoThreadData *tkoPtr = (TkoThreadData *)Tcl_GetThreadData(&tkoKey, sizeof(TkoThreadData));
    (void)dummy;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObjStruct(interp, objv[1], myOptions,
        sizeof(char *), "option", 0, &index) != TCL_OK) {
        return TCL_ERROR;
    }
    switch ((enum options) index) {
    case MY_INITCLASS: /* Add cget/configure functionalite to current class */
        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, "");
            return TCL_ERROR;
        }
        ret = Tcl_Eval(interp, "set ::tko::_option([self]) {} ; variable tko ; self");
        if (ret != TCL_OK) {
            return TCL_ERROR;
        }
        /* Get class object */
        tmpPtr = Tcl_GetObjResult(interp);
        if ((object = Tcl_GetObjectFromObj(interp, tmpPtr)) == NULL
            || (clazz = Tcl_GetObjectAsClass(object)) == NULL) {
            return TCL_ERROR;
        }
        /*
        * Add methods
        */
        Tcl_ClassSetConstructor(interp, clazz,
            Tcl_NewMethod(interp, clazz, NULL, 1, &tkoWidgetMethods[0], NULL));
        Tcl_ClassSetDestructor(interp, clazz,
            Tcl_NewMethod(interp, clazz, NULL, 1, &tkoWidgetMethods[2], NULL));
        Tcl_NewMethod(interp, clazz, tkoPtr->Obj_cget, 1, &tkoWidgetMethods[3], NULL);
        Tcl_NewMethod(interp, clazz, tkoPtr->Obj_configure, 1, &tkoWidgetMethods[4], NULL);
        Tcl_NewMethod(interp, clazz, tkoPtr->Obj__tko_configure, 0, &tkoWidgetMethods[5], NULL);
        Tcl_NewMethod(interp, clazz, tkoPtr->Obj__tko, 0, &tkoWidgetMethods[6], NULL);

        return TCL_OK;
    case MY_INITFROM: /* Initialize new tko class */
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tkoclass");
            return TCL_ERROR;
        }
        /* Use fqn superclass  and get all options from it */
        ch = Tcl_GetStringFromObj(objv[2], &length);
        if (length < 2 || ch[0] != ':') {
            tmpPtr = Tcl_ObjPrintf(
                "set ::tko::_option([self]) {} ; unexport destroy; variable tko; {*}$::tko::_unknown\n"
                "superclass ::%s ; set ::tko::_option([self]) [::tko optionget ::%s]",
                ch,ch);
        }
        else{
            tmpPtr = Tcl_ObjPrintf(
                "set ::tko::_option([self]) {} ; unexport destroy; variable tko; {*}$::tko::_unknown\n"
                "superclass %s ; set ::tko::_option([self]) [::tko optionget %s]",
                ch,ch);
        }
        Tcl_IncrRefCount(tmpPtr);
        ret = Tcl_Eval(interp, Tcl_GetString(tmpPtr));
        Tcl_DecrRefCount(tmpPtr);
        if (ret != TCL_OK) {
            return TCL_ERROR;
        }
        return TCL_OK;
    case MY_INITWRAP: /* Wrap widget in new class */
        if (objc != 5) {
            Tcl_WrongNumArgs(interp, 2, objv, "widget readonlyoptionlist methodlist");
            return TCL_ERROR;
        }
        /* Create fqn widgetname */
        ch = Tcl_GetStringFromObj(objv[2], &length);
        if (length < 2 || ch[0] != ':') {
            namePtr = Tcl_ObjPrintf("::%s", Tcl_GetString(objv[2]));
        }
        else {
            namePtr = objv[2];
        }
        Tcl_IncrRefCount(namePtr);
        ch = Tcl_GetString(namePtr);
        ch1 = Tcl_GetString(objv[3]);
        tmpPtr = Tcl_ObjPrintf("set ::tko::_option([self]) {}\n"
            "unexport destroy ; variable tko\n"
            "::tko::_initwrap [self] %s {%s} {%s}\n"
            "self method unknown {pathName args} {\n"
            " set a {}; foreach {o v} $args {if {$o in {%s}} {lappend a $o $v}}\n"
            " rename [%s $pathName {*}$a] ::tko::$pathName\n"
            " tailcall [[self] create ::$pathName {*}$args] configure .\n"
            "}\n"
            "self",
            ch,ch1,Tcl_GetString(objv[4]),ch1,ch);
        Tcl_IncrRefCount(tmpPtr);
        ret = Tcl_Eval(interp, Tcl_GetString(tmpPtr));
        Tcl_DecrRefCount(namePtr);
        Tcl_DecrRefCount(tmpPtr);
        if (ret != TCL_OK) {
            return TCL_ERROR;
        }
        /* Get class object */
        tmpPtr = Tcl_GetObjResult(interp);
        if ((object = Tcl_GetObjectFromObj(interp, tmpPtr)) == NULL
            || (clazz = Tcl_GetObjectAsClass(object)) == NULL) {
            return TCL_ERROR;
        }
        /*
        * Add methods
        */
        Tcl_ClassSetConstructor(interp, clazz,
            Tcl_NewMethod(interp, clazz, NULL, 1, &tkoWidgetMethods[1], NULL));
        Tcl_ClassSetDestructor(interp, clazz,
            Tcl_NewMethod(interp, clazz, NULL, 1, &tkoWidgetMethods[2], NULL));
        Tcl_NewMethod(interp, clazz, tkoPtr->Obj_cget, 1, &tkoWidgetMethods[3], NULL);
        Tcl_NewMethod(interp, clazz, tkoPtr->Obj_configure, 1, &tkoWidgetMethods[4], NULL);
        Tcl_NewMethod(interp, clazz, tkoPtr->Obj__tko_configure, 0, &tkoWidgetMethods[5], NULL);
        Tcl_NewMethod(interp, clazz, tkoPtr->Obj__tko, 0, &tkoWidgetMethods[6], NULL);

        return TCL_OK;
    case MY_EVENTOPTION: /* Call proc ::tko::_eventoption */
        return Tcl_EvalObjEx(interp, tkoPtr->Obj_tko__eventoption, TCL_EVAL_GLOBAL);
    case MY_OPTIONDEF: /* Add or replace option definitions and return new state */
        if (objc != 3 && objc < 5) {
            Tcl_WrongNumArgs(interp, 2, objv, "::classname ?-option definition? .. ?body?");
            return TCL_ERROR;
        }
        /* Create fqn classname */
        ch = Tcl_GetStringFromObj(objv[2], &length);
        if (length < 2 || ch[0] != ':') {
            namePtr = Tcl_ObjPrintf("::%s", Tcl_GetString(objv[2]));
        }
        else {
            namePtr = objv[2];
        }
        Tcl_IncrRefCount(namePtr);
        /* get current value or create new one */
        dictPtr = Tcl_ObjGetVar2(interp, tkoPtr->Obj_tko__option, namePtr, TCL_GLOBAL_ONLY);
        if (dictPtr == NULL) {
            dictPtr = Tcl_NewObj();
        }
        else {
            dictPtr = Tcl_DuplicateObj(dictPtr);
        }
        Tcl_IncrRefCount(dictPtr);
        /* if no options then return current state */
        if (objc == 3) {
            Tcl_SetObjResult(interp, dictPtr);
            Tcl_DecrRefCount(dictPtr);
            Tcl_DecrRefCount(namePtr);
            return TCL_OK;
        }
        /* Add or replace options */
        for (i = 3; i < objc-1; i = i + 2) {
            /* check definition list */
            if (Tcl_ListObjGetElements(interp, objv[i + 1], &argObjc, &argObjv) != TCL_OK) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("no definition list: %s {%s}",
                    Tcl_GetString(objv[i]), Tcl_GetString(objv[i + 1])));
                Tcl_DecrRefCount(dictPtr);
                Tcl_DecrRefCount(namePtr);
                return TCL_ERROR;
            }
            /* Check definition list */
            switch (argObjc) {
            case 2: /* synonym flags */
                ret = WidgetOptionAdd(interp, NULL, objv[i], argObjv[0], NULL, NULL, argObjv[1], NULL, 0);
                if (ret == TCL_OK) {
                    ret = Tcl_DictObjPut(interp, dictPtr, objv[i], objv[i + 1]);
                }
                break;
            case 4: /* dbname dbclass default flags */
                ret = WidgetOptionAdd(interp, NULL, objv[i], argObjv[0], argObjv[1], argObjv[2], argObjv[3], NULL, 0);
                if (ret == TCL_OK) {
                    ret = Tcl_DictObjPut(interp, dictPtr, objv[i], objv[i + 1]);
                }
                break;
            default:
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong definition: %s {%s}",
                    Tcl_GetString(objv[i]), Tcl_GetString(objv[i + 1])));
                ret = TCL_ERROR;
            }
            if (ret != TCL_OK) {
                Tcl_DecrRefCount(dictPtr);
                Tcl_DecrRefCount(namePtr);
                return TCL_ERROR;
            }

        }
        /* Add body to last definition. */
        if (objc % 2 == 0) {
            myCmd[0] = tkoPtr->Obj_oo_define;
            myCmd[1] = namePtr;
            myCmd[2] = tkoPtr->Obj_method;
            myCmd[3] = objv[objc - 3];
            myCmd[4] = tkoPtr->Obj_empty;
            myCmd[5] = objv[objc - 1];
            ret = Tcl_EvalObjv(interp, 6, myCmd, TCL_EVAL_GLOBAL);
            if (ret != TCL_OK) {
                Tcl_DecrRefCount(dictPtr);
                Tcl_DecrRefCount(namePtr);
                return TCL_ERROR;
            }
        }        tmpPtr = Tcl_ObjSetVar2(interp, tkoPtr->Obj_tko__option, namePtr, dictPtr, TCL_GLOBAL_ONLY);
        Tcl_DecrRefCount(dictPtr);
        Tcl_DecrRefCount(namePtr);
        if (tmpPtr == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, tmpPtr);
        return TCL_OK;
    case MY_OPTIONDEL: /* Delete option definitions and return new state */
        if (objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "::classname ?-option? ..");
            return TCL_ERROR;
        }
        /* Create fqn classname */
        ch = Tcl_GetStringFromObj(objv[2], &length);
        if (length < 2 || ch[0] != ':') {
            namePtr = Tcl_ObjPrintf("::%s", Tcl_GetString(objv[2]));
        }
        else {
            namePtr = objv[2];
        }
        Tcl_IncrRefCount(namePtr);
        /* if no options then remove all options */
        if (objc == 3) {
            tmpPtr = Tcl_ObjSetVar2(interp, tkoPtr->Obj_tko__option, namePtr,tkoPtr->Obj_empty,TCL_GLOBAL_ONLY);
            Tcl_DecrRefCount(namePtr);
            if (tmpPtr == NULL) {
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, tmpPtr);
            return TCL_OK;
        }
        /* remove given options from dictionary */
        dictPtr = Tcl_ObjGetVar2(interp, tkoPtr->Obj_tko__option, namePtr, TCL_GLOBAL_ONLY);
        if (dictPtr == NULL) {
            Tcl_DecrRefCount(namePtr);
            return TCL_ERROR;
        }
        dictPtr = Tcl_DuplicateObj(dictPtr);
        Tcl_IncrRefCount(dictPtr);
        /* remove with error check */
        for (i = 3; i < objc; i++) {
            if (Tcl_DictObjRemove(interp, dictPtr, objv[i]) != TCL_OK) {
                Tcl_DecrRefCount(dictPtr);
                Tcl_DecrRefCount(namePtr);
                return TCL_ERROR;
            }
        }
        tmpPtr = Tcl_ObjSetVar2(interp, tkoPtr->Obj_tko__option, namePtr,dictPtr,TCL_GLOBAL_ONLY);
        Tcl_DecrRefCount(dictPtr);
        Tcl_DecrRefCount(namePtr);
        if (tmpPtr == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, tmpPtr);
        return TCL_OK;
    case MY_OPTIONGET: /* Return all or selected option definitions */
        if (objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "::classname ?-option? ..");
            return TCL_ERROR;
        }
        /* Create fqn classname */
        ch = Tcl_GetStringFromObj(objv[2], &length);
        if (length < 2 || ch[0] != ':') {
            namePtr = Tcl_ObjPrintf("::%s", Tcl_GetString(objv[2]));
        }
        else {
            namePtr = objv[2];
        }
        Tcl_IncrRefCount(namePtr);
        /* return all definitions */
        dictPtr = Tcl_ObjGetVar2(interp, tkoPtr->Obj_tko__option, namePtr, TCL_GLOBAL_ONLY);
        Tcl_DecrRefCount(namePtr);
        if (dictPtr == NULL) {
            Tcl_DecrRefCount(namePtr);
            return TCL_ERROR;
        }
        if (objc == 3) {
            Tcl_SetObjResult(interp, dictPtr);
            return TCL_OK;
        }
        /* return only selected definitions */
        listPtr = Tcl_NewListObj(0, NULL);
        Tcl_IncrRefCount(listPtr);
        /* get with error checks */
        for (i = 3; i < objc; i++) {
            if (Tcl_DictObjGet(interp, dictPtr, objv[i], &tmpPtr) != TCL_OK
                || tmpPtr == NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown option: %s",
                    Tcl_GetString(objv[i])));
                Tcl_DecrRefCount(listPtr);
                return TCL_ERROR;
            }
            Tcl_ListObjAppendElement(interp, listPtr, objv[i]);
            Tcl_ListObjAppendElement(interp, listPtr, tmpPtr);
        }
        Tcl_SetObjResult(interp, listPtr);
        Tcl_DecrRefCount(listPtr);
        return TCL_OK;
    case MY_OPTIONHIDE: /* Hide given options or return all hide'able options */
        if (objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "::classname ?-option? ..");
            return TCL_ERROR;
        }
        /* Create fqn classname */
        ch = Tcl_GetStringFromObj(objv[2], &length);
        if (length < 2 || ch[0] != ':') {
            namePtr = Tcl_ObjPrintf("::%s", Tcl_GetString(objv[2]));
        }
        else {
            namePtr = objv[2];
        }
        Tcl_IncrRefCount(namePtr);
        dictPtr = Tcl_ObjGetVar2(interp, tkoPtr->Obj_tko__option, namePtr, TCL_GLOBAL_ONLY);
        if (dictPtr == NULL) {
            Tcl_DecrRefCount(namePtr);
            return TCL_ERROR;
        }
        /* return list of hide'able options */
        if (objc == 3) {
            /* return list of visible options */
            if (Tcl_DictObjFirst(interp, dictPtr, &search,
                &key, &value, &done) != TCL_OK) {
                Tcl_DecrRefCount(namePtr);
                return TCL_ERROR;
            }
            listPtr = Tcl_NewListObj(0, NULL);
            for (; !done; Tcl_DictObjNext(&search, &key, &value, &done)) {
                Tcl_ListObjGetElements(interp, value, &argObjc, &argObjv);
                switch (argObjc) {
                case 1:
                case 3:
                    Tcl_ListObjAppendElement(interp, listPtr, key);
                    break;
                case 2:
                    if (WidgetFlagsHideGet(argObjv[1]) == 0) {
                        Tcl_ListObjAppendElement(interp, listPtr, key);
                    }
                    break;
                case 4:
                    if (WidgetFlagsHideGet(argObjv[3]) == 0) {
                        Tcl_ListObjAppendElement(interp, listPtr, key);
                    }
                    break;
                }
                /* ignore internal error on wrong definition lists */
            }
            Tcl_DictObjDone(&search);
            Tcl_SetObjResult(interp, listPtr);
            Tcl_DecrRefCount(namePtr);
            return TCL_OK;
        }
        /* hide given options */
        dictPtr = Tcl_DuplicateObj(dictPtr);
        Tcl_IncrRefCount(dictPtr);
        for (i = 3; i < objc; i++) {
            if (Tcl_DictObjGet(interp, dictPtr, objv[i], &listPtr) != TCL_OK
                || listPtr == NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown option: %s",
                    Tcl_GetString(objv[i])));
                Tcl_DecrRefCount(dictPtr);
                Tcl_DecrRefCount(namePtr);
                return TCL_ERROR;
            }
            Tcl_ListObjGetElements(interp, listPtr, &argObjc, &argObjv);
            listPtr = NULL;
            switch (argObjc) {
            case 1:
                listPtr = Tcl_NewListObj(0, NULL);
                Tcl_ListObjAppendElement(interp, listPtr, argObjv[0]);
                Tcl_ListObjAppendElement(interp, listPtr, tkoPtr->Obj_flags_h);
                break;
            case 2:
                listPtr = Tcl_NewListObj(0, NULL);
                Tcl_ListObjAppendElement(interp, listPtr, argObjv[0]);
                Tcl_ListObjAppendElement(interp, listPtr, WidgetFlagsHideSet(argObjv[1]));
                break;
            case 3:
                listPtr = Tcl_NewListObj(0, NULL);
                Tcl_ListObjAppendElement(interp, listPtr, argObjv[0]);
                Tcl_ListObjAppendElement(interp, listPtr, argObjv[1]);
                Tcl_ListObjAppendElement(interp, listPtr, argObjv[2]);
                Tcl_ListObjAppendElement(interp, listPtr, tkoPtr->Obj_flags_h);
                break;
            case 4:
                listPtr = Tcl_NewListObj(0, NULL);
                Tcl_ListObjAppendElement(interp, listPtr, argObjv[0]);
                Tcl_ListObjAppendElement(interp, listPtr, argObjv[1]);
                Tcl_ListObjAppendElement(interp, listPtr, argObjv[2]);
                Tcl_ListObjAppendElement(interp, listPtr, WidgetFlagsHideSet(argObjv[3]));
                break;
            default: /* ignore internal error */
                continue;
            }
            if (Tcl_DictObjPut(interp, dictPtr, objv[i], listPtr) != TCL_OK) {
                Tcl_DecrRefCount(dictPtr);
                Tcl_DecrRefCount(namePtr);
                return TCL_ERROR;
            }
        }
        tmpPtr = Tcl_ObjSetVar2(interp, tkoPtr->Obj_tko__option, namePtr, dictPtr, TCL_GLOBAL_ONLY);
        Tcl_DecrRefCount(dictPtr);
        Tcl_DecrRefCount(namePtr);
        if (tmpPtr == NULL) {
            return TCL_ERROR;
        }
        return TCL_OK;
    case MY_OPTIONSHOW: /* Show given options or return all hidden options */
        if (objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "::classname ?-option? ..");
            return TCL_ERROR;
        }
        /* Create fqn classname */
        ch = Tcl_GetStringFromObj(objv[2], &length);
        if (length < 2 || ch[0] != ':') {
            namePtr = Tcl_ObjPrintf("::%s", Tcl_GetString(objv[2]));
        }
        else {
            namePtr = objv[2];
        }
        Tcl_IncrRefCount(namePtr);
        dictPtr = Tcl_ObjGetVar2(interp, tkoPtr->Obj_tko__option, namePtr, TCL_GLOBAL_ONLY);
        if (dictPtr == NULL) {
            Tcl_DecrRefCount(namePtr);
            return TCL_ERROR;
        }
        /* return list of show'able options */
        if (objc == 3) {
            /* return list of visible options */
            if (Tcl_DictObjFirst(interp, dictPtr, &search,
                &key, &value, &done) != TCL_OK) {
                Tcl_DecrRefCount(namePtr);
                return TCL_ERROR;
            }
            listPtr = Tcl_NewListObj(0, NULL);
            for (; !done; Tcl_DictObjNext(&search, &key, &value, &done)) {
                Tcl_ListObjGetElements(interp, value, &argObjc, &argObjv);
                if (argObjc == 2) {
                    if (WidgetFlagsHideGet(argObjv[1]) == 1) {
                        Tcl_ListObjAppendElement(interp, listPtr, key);
                    }
                } else if (argObjc == 4) {
                    if (WidgetFlagsHideGet(argObjv[3]) == 1) {
                        Tcl_ListObjAppendElement(interp, listPtr, key);
                    }
                }
            }
            Tcl_DictObjDone(&search);
            Tcl_SetObjResult(interp, listPtr);
            Tcl_DecrRefCount(namePtr);
            return TCL_OK;
        }
        /* show given options */
        dictPtr = Tcl_DuplicateObj(dictPtr);
        Tcl_IncrRefCount(dictPtr);
        for (i = 3; i < objc; i++) {
            if (Tcl_DictObjGet(interp, dictPtr, objv[i], &listPtr) != TCL_OK
                || listPtr == NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown option: %s",
                    Tcl_GetString(objv[i])));
                Tcl_DecrRefCount(dictPtr);
                Tcl_DecrRefCount(namePtr);
                return TCL_ERROR;
            }
            Tcl_ListObjGetElements(interp, listPtr, &argObjc, &argObjv);
            switch (argObjc) {
            case 1: /* already visible */
                continue;
            case 2:
                listPtr = Tcl_NewListObj(0, NULL);
                Tcl_ListObjAppendElement(interp, listPtr, argObjv[0]);
                Tcl_ListObjAppendElement(interp, listPtr, WidgetFlagsHideUnset(argObjv[1]));
                break;
            case 3: /* already visible */
                continue;
            case 4:
                listPtr = Tcl_NewListObj(0, NULL);
                Tcl_ListObjAppendElement(interp, listPtr, argObjv[0]);
                Tcl_ListObjAppendElement(interp, listPtr, argObjv[1]);
                Tcl_ListObjAppendElement(interp, listPtr, argObjv[2]);
                Tcl_ListObjAppendElement(interp, listPtr, WidgetFlagsHideUnset(argObjv[3]));
                continue;
            default: /* ignore internal error */
                continue;
            }
            if (Tcl_DictObjPut(interp, dictPtr, objv[i], listPtr) != TCL_OK) {
                Tcl_DecrRefCount(dictPtr);
                Tcl_DecrRefCount(namePtr);
                return TCL_ERROR;
            }
        }
        tmpPtr = Tcl_ObjSetVar2(interp, tkoPtr->Obj_tko__option, namePtr, dictPtr, TCL_GLOBAL_ONLY);
        Tcl_DecrRefCount(dictPtr);
        Tcl_DecrRefCount(namePtr);
        if (tmpPtr == NULL) {
            return TCL_ERROR;
        }
        return TCL_OK;
    }
    return TCL_ERROR;
}

/*
* WidgetMethod_tko --
*    Implementation of the "my _tko" method.
*    Configuration of widget object options.
*
* Results:
*    A standard Tcl result.
*
* Side effects:
*/
static int WidgetMethod_tko(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    static const char *const myOptions[] = {
        "optionadd", "optiondel",
        "optionhide", "optionshow",NULL
    };
    enum options {
        MY_OPTIONADD, MY_OPTIONDEL,
        MY_OPTIONHIDE, MY_OPTIONSHOW
    };
    int index;
    Tcl_Obj *listPtr;
    int i;
    Tko_Widget *widget;
    int skip;
    Tcl_HashEntry *entryPtr;
    Tcl_HashSearch search;
    WidgetOption *optionPtr;
    Tcl_Obj *myCmd[6];
    Tcl_Object object;
    int argObjc;
    Tcl_Obj **argObjv;
    TkoThreadData *tkoPtr = (TkoThreadData *)Tcl_GetThreadData(&tkoKey, sizeof(TkoThreadData));
    (void)dummy;

    widget = (Tko_Widget *) Tko_WidgetClientData(context);
    if (widget == NULL || widget->myCmd == NULL) {
        return TCL_ERROR;
    }
    skip = Tcl_ObjectContextSkippedArgs(context);

    if (objc-skip <= 0) {
        Tcl_WrongNumArgs(interp, objc, objv, "option ?arg ...?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObjStruct(interp, objv[skip], myOptions,
        sizeof(char *), "option", 0, &index) != TCL_OK) {
        return TCL_ERROR;
    }

    switch ((enum options) index) {
    case MY_OPTIONADD:
        if (objc - skip != 3 && objc - skip != 4) {
            Tcl_WrongNumArgs(interp, skip + 1, objv,
                "-option definitionlist ?body?");
        }
        /* Check definition list */
        if (Tcl_ListObjGetElements(interp, objv[skip + 2], &argObjc, &argObjv) != TCL_OK) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("no definition list: %s {%s}",
                Tcl_GetString(objv[skip+1]), Tcl_GetString(objv[skip+2])));
            return TCL_ERROR;
        }
        /* Add body if given. */
        if (objc - skip == 4) {
            object = Tcl_ObjectContextObject(context);
            if (object == NULL) return TCL_ERROR;
            myCmd[0] = tkoPtr->Obj_oo_objdefine;
            myCmd[1] = Tcl_GetObjectName(interp, object);
            myCmd[2] = tkoPtr->Obj_method;
            myCmd[3] = objv[skip + 1];
            myCmd[4] = tkoPtr->Obj_empty;
            myCmd[5] = objv[skip + 3];
            if (Tcl_EvalObjv(interp, 6, myCmd, TCL_EVAL_GLOBAL) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        switch (argObjc) {
        case 2: /* synonym flags */
            return (WidgetOptionAdd(interp, widget, objv[skip+1], argObjv[0], NULL, NULL, argObjv[1], NULL, 0));
        case 4: /* dbname dbclass default flags */
            return (WidgetOptionAdd(interp, widget, objv[skip+1], argObjv[0], argObjv[1], argObjv[2], argObjv[3], NULL, 0));
        }
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong definition list: %s {%s}",
                Tcl_GetString(objv[skip+1]), Tcl_GetString(objv[skip+2])));
        return TCL_ERROR;
    case MY_OPTIONDEL: /* delete object options */
        for (i= skip+1; i<objc; i++) {
            entryPtr =
                Tcl_FindHashEntry(widget->optionsTable,
                    Tk_GetUid(Tcl_GetString(objv[i])));
            if (entryPtr == NULL) {
                Tcl_SetObjResult(interp,
                    Tcl_ObjPrintf("unknown option \"%s\"", Tcl_GetString(objv[i])));
                return TCL_ERROR;
            }
            /* delete with no additional check on synonym option */
            Tcl_UnsetVar2(interp, Tcl_GetString(widget->optionsArray),
                Tcl_GetString(objv[i]), TCL_GLOBAL_ONLY);
            WidgetOptionDelEntry(entryPtr);
        }
        return TCL_OK;
    case MY_OPTIONHIDE:
        /* Without args return all not hidden options */
        if ((objc - skip) == 1) {
            listPtr = Tcl_NewListObj(0,NULL);
            entryPtr = Tcl_FirstHashEntry(widget->optionsTable, &search);
            while (entryPtr != NULL) {
                optionPtr = (WidgetOption *)Tcl_GetHashValue(entryPtr);
                entryPtr = Tcl_NextHashEntry(&search);
                if ((optionPtr->flagbits&TKO_OPTION_HIDE)==0) {
                    Tcl_ListObjAppendElement(interp, listPtr, optionPtr->option);
                }
            }
            Tcl_SetObjResult(interp, listPtr);
            return TCL_OK;
        }
        /* Hide given options */
        skip++;
        while (skip < objc) {
            entryPtr = Tcl_FindHashEntry(widget->optionsTable,
                Tk_GetUid(Tcl_GetString(objv[skip])));
            if (entryPtr == NULL) {
                Tcl_SetObjResult(interp,
                    Tcl_ObjPrintf("unknown option \"%s\"", Tcl_GetString(objv[skip])));
                return TCL_ERROR;
            }
            optionPtr = (WidgetOption *)Tcl_GetHashValue(entryPtr);
            optionPtr->flagbits |= TKO_OPTION_HIDE;
            skip++;
        }
        return TCL_OK;
    case MY_OPTIONSHOW:
        /* Without args return all hidden options */
        if ((objc - skip) == 1) {
            listPtr = Tcl_NewObj();
            entryPtr = Tcl_FirstHashEntry(widget->optionsTable, &search);
            while (entryPtr != NULL) {
                optionPtr = (WidgetOption *)Tcl_GetHashValue(entryPtr);
                entryPtr = Tcl_NextHashEntry(&search);
                if (optionPtr->flagbits & TKO_OPTION_HIDE) {
                    Tcl_ListObjAppendElement(interp, listPtr, optionPtr->option);
                }
            }
            Tcl_SetObjResult(interp, listPtr);
            return TCL_OK;
        }
        /* Show given options */
        skip++;
        while (skip < objc) {
            entryPtr = Tcl_FindHashEntry(widget->optionsTable,
                Tk_GetUid(Tcl_GetString(objv[skip])));
            if (entryPtr == NULL) {
                Tcl_SetObjResult(interp,
                    Tcl_ObjPrintf("unknown option \"%s\"", Tcl_GetString(objv[skip])));
                return TCL_ERROR;
            }
            optionPtr = (WidgetOption *)Tcl_GetHashValue(entryPtr);
            optionPtr->flagbits &= ~TKO_OPTION_HIDE;
            skip++;
        }
        return TCL_OK;
    }
    return TCL_OK;
}


/*
* Tko_Init --
*    Initialize tko widgets.
*
* Results:
*    A standard Tcl result.
*
* Side effects:
*    Create available oo::class tko widgets.
*/
int
Tko_Init(
    Tcl_Interp * interp /* Tcl interpreter. */)
{
    /* Create common tko variables. */
    /* tko::_eventoption according library/ttk.tcl proc ttk::ThemeChanged */
    static const char initScript[] =
        "namespace eval ::tko {}\n"
        "array set ::tko::_option {}\n"
        "set ::tko::_unknown [list self method unknown {pathName args} {\n"
        " tailcall [[self] create ::$pathName {*}$args] configure .\n"
        "}]\n"
        "proc ::tko::_eventoption {} {\n"
        " set l .\n"
        " while {[llength $l]} {\n"
        "  set l1 [list]\n"
        "  foreach w $l {\n"
        "   event generate $w <<TkoEventOption>>\n"
        "   foreach c [winfo children $w] {\n"
        "    lappend l1 $c\n"
        "   }\n"
        "  }\n"
        "  set l $l1\n"
        " }\n"
        "}\n"
        "proc ::tko::_initwrap {class widget ro ml} {\n"
        " catch {destroy .__tko__}\n"
        " set myConf [[$widget .__tko__] configure]\n"
        " destroy .__tko__\n"
        " foreach myCmd $ml {\n"
        "  if {$myCmd in {cget configure}} continue\n"
        "  uplevel 1 [list method $myCmd args \"\\$tko(..) $myCmd {*}\\$args\"]\n"
        " }\n"
        " foreach myList $myConf {\n"
        "  lassign $myList o n c d\n"
        "  switch [llength $myList] {\n"
        "   2 {::tko optiondef $class $o [list $n {}]}\n"
        "   5 {if {$o in $ro} {set f r} else {set f {}}\n"
        "    ::tko optiondef $class $o [list $n $c $d $f ] \"\\$tko(..) configure $o \\$tko($o) ; set tko($o) \\[\\$tko(..) cget $o\\]\"\n"
        "   }\n"
        "  }\n"
        " }\n"
        "}";
    TkoThreadData *tkoPtr = (TkoThreadData *)Tcl_GetThreadData(&tkoKey, sizeof(TkoThreadData));

    /* Needed oo extension */
    if (Tcl_OOInitStubs(interp) == NULL) {
        return TCL_ERROR;
    }
    /*
    * Create tko namespace and data
    */
    if (Tcl_Eval(interp, initScript) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * Constants
     */
    tkoPtr->Uid_class = Tk_GetUid("-class");
    tkoPtr->Uid_empty = Tk_GetUid("");
    Tcl_IncrRefCount((tkoPtr->Obj_empty = Tcl_NewStringObj("", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_tko__option =
        Tcl_NewStringObj("::tko::_option", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_tko__eventoption =
        Tcl_NewStringObj("::tko::_eventoption", -1)));
    /* Internally visible */
    Tcl_IncrRefCount((tkoPtr->Obj_next = Tcl_NewStringObj("next", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_uplevel = Tcl_NewStringObj("::uplevel", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_oo_define =
        Tcl_NewStringObj("::oo::define", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_oo_objdefine =
        Tcl_NewStringObj("::oo::objdefine", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_method = Tcl_NewStringObj("method", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj__tko_configure =
        Tcl_NewStringObj("_tko_configure", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj__tko =
        Tcl_NewStringObj("_tko", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_cget =
        Tcl_NewStringObj("cget", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_configure =
        Tcl_NewStringObj("configure", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_tko = Tcl_NewStringObj("::tko", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_tko_widget =
        Tcl_NewStringObj("::tko::widget", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_lsort = Tcl_NewStringObj("::lsort", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_point = Tcl_NewStringObj(".", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_point2 = Tcl_NewStringObj("..", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj__screen = Tcl_NewStringObj("-screen", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_flags_r = Tcl_NewStringObj("r", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_flags_rh = Tcl_NewStringObj("rh", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_flags_h = Tcl_NewStringObj("h", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_rename = Tcl_NewStringObj("rename", -1)));
    Tcl_IncrRefCount((tkoPtr->Obj_tko__self = Tcl_NewStringObj("::tko::_self", -1)));
    /* commands */
    Tcl_CreateObjCommand(interp, "::tko", Tko_TkoObjCmd, NULL, NULL);

    if (Tko_FrameInit(interp) != TCL_OK) {
        return TCL_ERROR;
    }
/* TODO */
#ifdef USE_RBC
    if (Tko_GraphInit(interp) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tko_VectorInit(interp) != TCL_OK) {
        return TCL_ERROR;
    }
#endif
    return TCL_OK;
}

/*
 * Tko_WidgetClassDefine --
 *    Create a new tko widget class.
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *    Create new class with methods and option defines.
 */
int
Tko_WidgetClassDefine(
    Tcl_Interp * interp,
    Tcl_Obj * classname,
    const Tcl_MethodType * methods,
    const Tko_WidgetOptionDefine * options)
{
    Tcl_Class clazz;
    Tcl_Object object;
    Tcl_Obj *listPtr;
    Tcl_Obj *optionPtr;
    Tcl_Obj *tmpObj;
    Tcl_Obj *dictPtr;
    WidgetClientdata *clientdata;
    int i;
    TkoThreadData *tkoPtr = (TkoThreadData *)Tcl_GetThreadData(&tkoKey, sizeof(TkoThreadData));

    if (classname == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing class name"));
        return TCL_ERROR;
    }
    /*
     * Create widget class.
     */
    tmpObj = Tcl_ObjPrintf("::oo::class create %s {unexport destroy; variable tko; {*}$::tko::_unknown}", Tcl_GetString(classname));
    Tcl_IncrRefCount(tmpObj);
    if (Tcl_GlobalEval(interp, Tcl_GetString(tmpObj)) != TCL_OK) {
        Tcl_DecrRefCount(tmpObj);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(tmpObj);

    /* Get class object */
    if ((object = Tcl_GetObjectFromObj(interp, classname)) == NULL
        || (clazz = Tcl_GetObjectAsClass(object)) == NULL) {
        return TCL_ERROR;
    }

    /*
     * Add methods
     */
    if(methods) {
        /* constructor */
        if(methods[0].name == NULL && methods[0].callProc) {
            Tcl_ClassSetConstructor(interp, clazz,
                Tcl_NewMethod(interp, clazz, NULL, 1, &methods[0], NULL));
        }
        /* destructor */
        if(methods[1].name == NULL && methods[1].callProc) {
            Tcl_ClassSetDestructor(interp, clazz,
                Tcl_NewMethod(interp, clazz, NULL, 1, &methods[1], NULL));
        }
        /* our own methods */
        Tcl_NewMethod(interp, clazz, tkoPtr->Obj_cget, 1, &tkoWidgetMethods[3], NULL);
        Tcl_NewMethod(interp, clazz, tkoPtr->Obj_configure, 1, &tkoWidgetMethods[4], NULL);
        Tcl_NewMethod(interp, clazz, tkoPtr->Obj__tko_configure, 0, &tkoWidgetMethods[5], NULL);
        Tcl_NewMethod(interp, clazz, tkoPtr->Obj__tko, 0, &tkoWidgetMethods[6], NULL);
        /* public */
        for(i = 2; methods[i].name != NULL; i++) {
            tmpObj = Tcl_NewStringObj(methods[i].name, -1);
            Tcl_IncrRefCount(tmpObj);
            Tcl_NewMethod(interp, clazz, tmpObj, 1, &methods[i], NULL);
            Tcl_DecrRefCount(tmpObj);
        }
        i++;
        /* private */
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
        /* get dict variable */
        dictPtr = Tcl_ObjGetVar2(interp, tkoPtr->Obj_tko__option, classname,
            TCL_GLOBAL_ONLY);
        if (dictPtr == NULL) {
            dictPtr = Tcl_NewDictObj();
        }
        else {
            dictPtr = Tcl_DuplicateObj(dictPtr);
        }
        Tcl_IncrRefCount(dictPtr);
        /* Loop over all option definitions */
        for(i = 0;; i++) {
            /* test on end of options */
            if (options[i].option == NULL) {
                break;
            }
            /* test option name starting with "-" */
            if (options[i].option[0] != '-') {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong option name: %s",
                    options[i].option));
                Tcl_DecrRefCount(dictPtr);
                return TCL_ERROR;
            }
            /* we need at least an synonym name here */
            if(options[i].dbname == NULL) {
                Tcl_SetObjResult(interp,
                    Tcl_ObjPrintf("wrong option definition: %d", i));
                Tcl_DecrRefCount(dictPtr);
                return TCL_ERROR;
            }
            /* no dbclass means synonym option definition */
            if (options[i].dbclass == NULL || options[i].dbclass[0] == '\0') {
                /* test synonym option starting with "-" */
                if (options[i].dbname[0] != '-') {
                    Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong synonym name: %s",
                        options[i].dbname));
                    Tcl_DecrRefCount(dictPtr);
                    return TCL_ERROR;
                }
            }
            /* we build all options with {dbname dbclass defvalue flag} */
            optionPtr = Tcl_NewStringObj(options[i].option, -1);
            Tcl_IncrRefCount(optionPtr);
            listPtr = Tcl_NewListObj(0, NULL);
            Tcl_ListObjAppendElement(interp, listPtr,
                Tcl_NewStringObj(options[i].dbname, -1));
            /* only if not synonym option */
            if (options[i].dbclass != NULL) {
                Tcl_ListObjAppendElement(interp, listPtr,
                    Tcl_NewStringObj(options[i].dbclass, -1));
                if (options[i].defvalue == NULL) {
                    Tcl_ListObjAppendElement(interp, listPtr, tkoPtr->Obj_empty);
                }
                else {
                    Tcl_ListObjAppendElement(interp, listPtr,
                        Tcl_NewStringObj(options[i].defvalue, -1));
                }
            }
            /* always add flags */
            if (options[i].flags & TKO_OPTION_READONLY) {
                if (options[i].flags & TKO_OPTION_HIDE) {
                    Tcl_ListObjAppendElement(interp, listPtr, tkoPtr->Obj_flags_rh);
                }
                Tcl_ListObjAppendElement(interp, listPtr, tkoPtr->Obj_flags_r);
            }
            else if (options[i].flags & TKO_OPTION_HIDE) {
                Tcl_ListObjAppendElement(interp, listPtr, tkoPtr->Obj_flags_h);
            }
            else {
                Tcl_ListObjAppendElement(interp, listPtr, tkoPtr->Obj_empty);
            }
            if (Tcl_DictObjPut(interp, dictPtr, optionPtr, listPtr) != TCL_OK) {
                Tcl_DecrRefCount(optionPtr);
                Tcl_DecrRefCount(dictPtr);
                return TCL_ERROR;
            }
            /*
             * Now we create the necessary -option method if provided.
             * If given we create the -option method with the given method.
             * Or we use the internal implementation of a given type.
             * If none of the above are provided it is up to the caller
             * to create the necessary -option method.
             */
            if (options[i].method != NULL || options[i].type >= 0) {
                clientdata = (WidgetClientdata *)ckalloc(sizeof(WidgetClientdata));
                assert(clientdata);
                clientdata->method.version = TCL_OO_METHOD_VERSION_CURRENT;
                clientdata->method.name = options[i].option;
                if (options[i].method != NULL) {
                    clientdata->method.callProc = options[i].method;
                }
                else {
                    clientdata->method.callProc = WidgetMethod_;
                }
                clientdata->method.deleteProc = WidgetClientdataDelete;
                clientdata->method.cloneProc = WidgetClientdataClone;
                clientdata->option = optionPtr;/* we do not decrement here */
                clientdata->offset = options[i].offset;
                clientdata->type = options[i].type;
                clientdata->flags = options[i].flags;
                Tcl_NewMethod(interp, clazz, optionPtr, 0, &clientdata->method,
                    (ClientData) clientdata);
            }
            else {
                Tcl_DecrRefCount(optionPtr);
            }
        }
        if (Tcl_ObjSetVar2(interp, tkoPtr->Obj_tko__option, classname, dictPtr,
            TCL_GLOBAL_ONLY) == 0) {
            Tcl_DecrRefCount(dictPtr);
            return TCL_ERROR;
        }
        Tcl_DecrRefCount(dictPtr);
    }
    return TCL_OK;
}

/*
* WidgetDestructor --
*
* Results:
*    A standard Tcl result.
*
* Side effects:
*  Delete widget ressources.
*/
static int
WidgetDestructor(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tko_Widget *widget;
    (void)dummy;
    (void)interp;
    (void)objc;
    (void)objv;

    if ((widget = (Tko_Widget *)Tko_WidgetClientData(context)) != NULL) {
        Tcl_Preserve(widget);
        Tko_WidgetDestroy(context);
        Tcl_Release(widget);
    }
    return TCL_OK;
}

/*
* WidgetClassConstructor --
*    Create a new tko class object with common methods.
*
* Results:
*    A standard Tcl result.
*
* Side effects:
*    Create new object with methods and option defines.
 */
static int
WidgetClassConstructor(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Object object;
    Tko_Widget *widget;
    Tcl_Obj *myArglist;
    int skip;
    (void)dummy;

    /* Get current object. Should not fail? */
    if ((object = Tcl_ObjectContextObject(context)) == NULL) {
        return TCL_ERROR;
    }

    /* Create and initialize internal widget structure */
    widget = (Tko_Widget *)ckalloc(sizeof(Tko_Widget));
    assert(widget);
    memset(widget, 0, sizeof(Tko_Widget));

    skip = Tcl_ObjectContextSkippedArgs(context);
    if (objc - skip > 0) {
        myArglist = Tcl_NewListObj(objc - skip, &objv[skip]);
    }
    else {
        myArglist = Tcl_NewListObj(0,NULL);
    }
    Tcl_IncrRefCount(myArglist);
    if (Tko_WidgetCreate(widget, interp, object, TKO_CREATE_CLASS,
        myArglist) != TCL_OK) {
        Tcl_DecrRefCount(myArglist);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(myArglist);
    return TCL_OK;
}

/*
* WidgetWrapConstructor --
*    Create a new tko widget object with wrapping of the given widget command.
*
* Results:
*    A standard Tcl result.
*
* Side effects:
*    Create new object with methods and option defines.
*/
static int
WidgetWrapConstructor(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tcl_Object object;
    Tko_Widget *widget;
    Tcl_Obj *myArglist;
    int skip;
    const char *ch;
    int length;
    Tk_Window tkWin;
    Tk_Window tkWinTmp; /* tmp. created window to get Tk_Window from embedded window */
    Tcl_Obj *tmpPtr; /* tmp. string for evaluating code */
    (void)dummy;

                     /* Get current object. Should not fail? */
    if ((object = Tcl_ObjectContextObject(context)) == NULL) {
        return TCL_ERROR;
    }
    /* Check widget name on "::.*" */
    ch = NULL;
    if ((tmpPtr = Tcl_GetObjectName(interp, object)) == NULL
        || (ch = TclGetStringFromObj(tmpPtr, &length)) == NULL
        || length < 4 || ch[0] != ':' || ch[1] != ':' || ch[2] != '.') {
        if (ch == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("no pathName"));
        }
        else {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong pathName: %s", ch));
        }
        return TCL_ERROR;
    }

    /*
    * Get real widget Tk_Window.
    */
    tmpPtr = Tcl_NewStringObj(&ch[2], length - 2);
    Tcl_AppendToObj(tmpPtr, ".1", 2);
    Tcl_IncrRefCount(tmpPtr);
    tkWinTmp = Tk_CreateWindowFromPath(interp, Tk_MainWindow(interp), Tcl_GetString(tmpPtr), NULL);
    Tcl_DecrRefCount(tmpPtr);
    if (tkWinTmp == NULL) {
        return TCL_ERROR;
    }
    tkWin = Tk_NameToWindow(interp, &ch[2], tkWinTmp);
    Tk_DestroyWindow(tkWinTmp);
    if (tkWin == NULL) {
        return TCL_ERROR;
    }

    /* Create and initialize internal widget structure */
    widget = (Tko_Widget *)ckalloc(sizeof(Tko_Widget));
    assert(widget);
    memset(widget, 0, sizeof(Tko_Widget));
    widget->tkWin = tkWin;

    skip = Tcl_ObjectContextSkippedArgs(context);
    if (objc - skip > 0) {
        myArglist = Tcl_NewListObj(objc - skip, &objv[skip]);
    }
    else {
        myArglist = Tcl_NewListObj(0, NULL);
    }
    Tcl_IncrRefCount(myArglist);
    if (Tko_WidgetCreate(widget, interp, object, TKO_CREATE_WRAP,
        myArglist) != TCL_OK) {
        Tcl_DecrRefCount(myArglist);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(myArglist);
    return TCL_OK;
}

/*
 * Tko_WidgetCreate --
 *    Create new tko object.
 *    A check on the correct name of the object should be done in the calling function.
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *    Can create new widget.
 */
int
Tko_WidgetCreate(
    ClientData clientdata, /* pointer to Tko_Widget structure */
    Tcl_Interp * interp,
    Tcl_Object object,
    Tko_WidgetCreateMode createmode, /* */
    Tcl_Obj *arglist) /* -value option .. list, used options will be removed */
{
    Tko_Widget *widget;
    char *nsPtr;
    int argSize;
    Tcl_Obj *classObj;
    Tcl_Obj *optionList;
    Tcl_Obj *tmpObj;
    Tcl_Obj **optionObjv;
    int optionObjc;
    Tcl_Obj **argObjv;
    int argObjc;
    int index = 0; /* Index in option list */
    int ret;
    Tcl_Obj *value;
    Tcl_Obj *screen;
    char *ch;
    int length;
    Tcl_Obj *tmpPtr;
    int initmode=1;/* 1=own widget 2=wrapped widget */
    Tk_Window wrapWin = NULL;/* needed in error case */
    TkoThreadData *tkoPtr = (TkoThreadData *)Tcl_GetThreadData(&tkoKey, sizeof(TkoThreadData));

    /* This would be an internal programming error */
    if (clientdata == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("no widget data"));
        return TCL_ERROR;
    }
    /* Check name starting with "::" */
    if ((tmpPtr = Tcl_GetObjectName(interp, object)) == NULL
        || (ch = TclGetStringFromObj(tmpPtr, &length)) == NULL
        || length < 3 || ch[0] != ':' || ch[1] != ':') {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("no object"));
        return TCL_ERROR;
    }

    /* Add widget to metadata so it can be released  */
    Tcl_ObjectSetMetadata(object, &tkoWidgetMeta, clientdata);

    /*
    * Initialize internal widget strucure.
    */
    widget = (Tko_Widget *)clientdata;
    widget->interp = interp;
    widget->object = object;
    widget->optionsTable = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
    Tcl_InitHashTable(widget->optionsTable, TCL_ONE_WORD_KEYS);
    widget->widgetCmd = Tcl_GetObjectCommand(object);
    /* Create option array variable */
    nsPtr = Tcl_GetObjectNamespace(object)->fullName;
    widget->optionsArray = Tcl_ObjPrintf("%s::tko", nsPtr);
    Tcl_IncrRefCount(widget->optionsArray);
    /* Create my command */
    widget->myCmd = Tcl_ObjPrintf("%s::my", nsPtr);
    Tcl_IncrRefCount(widget->myCmd);

    if (createmode == TKO_CREATE_WRAP) {
        wrapWin = widget->tkWin;
        widget->tkWin = NULL;
    }

    /*
    * Get options from outermost class.
    */
    classObj = Widget_GetClassName(interp, object);
    if (classObj == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("no class name"));
        goto error;
    }
    optionList = Tcl_ObjGetVar2(interp, tkoPtr->Obj_tko__option, classObj, TCL_GLOBAL_ONLY);
    if (optionList == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("no option definitions"));
        goto error;
    }
    if (Tcl_ListObjGetElements(interp, optionList, &optionObjc, &optionObjv) != TCL_OK
        || optionObjc % 2 != 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong option definitions"));
        goto error;
    }

    /* Convert argument list in dictionary */
    if (Tcl_DictObjSize(interp, arglist, &argSize) != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("could not get arglist"));
        goto error;
    }

    /*
     * Do some initialization depending on the given createmode.
     */
    switch (createmode) {
    case TKO_CREATE_CLASS:
        widget->tkWin = NULL;
        break;
    case TKO_CREATE_TOPLEVEL:
        /* Check name starting with "::." */
        if (ch[2] != '.') {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong pathName: %s", ch));
            goto error;
        }
        if (optionObjc < 2) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing option definitions"));
            goto error;
        }
        /* The "-screen" option definition should be the first option in toplevels. */
        screen = NULL;
        /* -screen option should be first */
        if (strncmp("-screen", Tcl_GetString(optionObjv[0]), 8) != 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing -screen option"));
            goto error;
        }
        /* we only check argument number and assume readonly flag */
        if (Tcl_ListObjGetElements(interp, optionObjv[1], &argObjc, &argObjv) != TCL_OK
            || argObjc != 4) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong -screen option"));
            goto error;
        }
        /* Try to get value from command line or use default one. */
        Tcl_DictObjGet(interp, arglist, tkoPtr->Obj__screen, &screen);
        if (screen != NULL) {
            Tcl_DictObjRemove(interp, arglist, tkoPtr->Obj__screen);
            argSize--;
        }
        else {
            screen = argObjv[2];
        }
        Tcl_IncrRefCount(screen);
        widget->tkWin = Tk_CreateWindowFromPath(interp, Tk_MainWindow(interp), &ch[2],
            Tcl_GetString(screen));
        if (widget->tkWin == NULL) {
            goto error;
        }
        Tk_MakeWindowExist(widget->tkWin);
        if ((widget->display = Tk_Display(widget->tkWin))==NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("could not get display"));
            goto error;
        }
        /* When creating toplevels then check on "-screen" as first option. */
        ret = WidgetOptionAdd(interp, widget, optionObjv[0], argObjv[0],
            argObjv[1], argObjv[2], argObjv[3], screen, initmode);
        Tcl_DecrRefCount(screen);
        if (ret != TCL_OK) {
            goto error;
        }
        index = 2;
        break;
    case TKO_CREATE_WIDGET:
        /* Check name starting with "::." */
        if (ch[2] != '.') {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong pathName: %s", ch));
            goto error;
        }
        widget->tkWin = Tk_CreateWindowFromPath(interp, Tk_MainWindow(interp), &ch[2],
            NULL);
        if (widget->tkWin == NULL) {
            goto error;
        }
        Tk_MakeWindowExist(widget->tkWin);
        if ((widget->display = Tk_Display(widget->tkWin))==NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("could not get display"));
            goto error;
        }
        if (optionObjc < 1) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("empty option definitions"));
            goto error;
        }
        break;
    case TKO_CREATE_WRAP:
        if (wrapWin == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrap widget not found"));
            goto error;
        }
        /* Check name starting with "::." */
        if (ch[2] != '.') {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong pathName: %s", ch));
            goto error;
        }
        /* Set tko(..) to name of hidden widget */
        tmpObj = Tcl_ObjPrintf("::tko::%s", &ch[2]);
        Tcl_IncrRefCount(tmpObj);
        if (Tcl_ObjSetVar2(interp, widget->optionsArray, tkoPtr->Obj_point2,
            tmpObj, TCL_GLOBAL_ONLY) == NULL) {
            Tcl_DecrRefCount(tmpObj);
            goto error;
        }
        Tcl_DecrRefCount(tmpObj);
        if ((widget->display = Tk_Display(wrapWin))==NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("could not get display"));
            goto error;
        }
        widget->tkWin = wrapWin;
        wrapWin = NULL;
        initmode = 2;
        break;
    default:
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong internal create mode"));
        goto error;
    }
    /* Set tko(.) to name of widget or class */
    if (Tcl_ObjSetVar2(interp, widget->optionsArray, tkoPtr->Obj_point,
        Tcl_NewStringObj(&ch[2], length - 2), TCL_GLOBAL_ONLY) == NULL) {
        goto error;
    }

    /*
     * When creating widgets then "-class" option should be first option now.
     * It's value is needed to get option informations from option database.
     */
    if (createmode == TKO_CREATE_TOPLEVEL || createmode == TKO_CREATE_WIDGET) {
        ch = Tcl_GetStringFromObj(optionObjv[index], &length);
        if (strncmp(ch, "-class", length) != 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing -class option"));
            goto error;
        }
    }
    /*
     * Add options.
     */
    for(; index < optionObjc; index=index+2) {
        if (Tcl_ListObjGetElements(interp, optionObjv[index+1], &argObjc, &argObjv) !=TCL_OK
            || argObjc < 1 || argObjc > 4) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong option def: %s {%s}",
                    Tcl_GetString(optionObjv[index]),Tcl_GetString(optionObjv[index+1])));
            goto error;
        }
        Tcl_DictObjGet(interp, arglist, optionObjv[index], &value);
        if(value) {
            Tcl_IncrRefCount(value);
            Tcl_DictObjRemove(interp, arglist, optionObjv[index]);
            argSize--;
        }
        switch (argObjc) {
        case 2: /* synonym flags */
            ret = WidgetOptionAdd(interp, widget, optionObjv[index], argObjv[0],
                NULL, NULL, argObjv[1], value, initmode);
            break;
        case 4: /* dbname dbclass default flags */
            ret = WidgetOptionAdd(interp, widget, optionObjv[index], argObjv[0],
                argObjv[1], argObjv[2], argObjv[3], value, initmode);
            break;
        }
        if (value) {
            Tcl_DecrRefCount(value);
        }
        if (ret != TCL_OK) goto error;
    }
    if(argSize) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown options: %s",
                Tcl_GetString(arglist)));
        goto error;
    }

    Tcl_TraceVar2(interp, Tcl_GetString(widget->optionsArray), NULL,
        TCL_TRACE_WRITES | TCL_TRACE_RESULT_OBJECT, WidgetOptionTrace, widget);

    if (widget->tkWin) {
        Tk_CreateEventHandler(widget->tkWin, StructureNotifyMask | VirtualEventMask,
            WidgetEventProc, (ClientData)widget);
    }

    return TCL_OK;

error:
    if (wrapWin) {
        tmpObj = Tcl_ObjPrintf("rename ::tko::%s {}", &ch[2]);
        Tcl_IncrRefCount(tmpObj);
        Tcl_EvalObjEx(interp, tmpObj, TCL_GLOBAL_ONLY);
        Tcl_DecrRefCount(tmpObj);
    }
    Tcl_DeleteCommandFromToken(interp, widget->widgetCmd);
    return TCL_ERROR;
}

/*
 * Tko_WidgetDestroy --
 *    Delete widget window and command.
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *    Delete widget ressources and remove widget window.
 */
void
Tko_WidgetDestroy(
    Tcl_ObjectContext context)
{
    Tko_Widget *widget;

    if ((widget = (Tko_Widget *)Tko_WidgetClientData(context)) == NULL) {
        return;
    }
    Tcl_Preserve(widget);
    if (widget->tkWin) {
        WidgetDeleteTkwin(widget);
    }
    if (widget->myCmd) {
        Tcl_DecrRefCount(widget->myCmd);
        widget->myCmd = NULL;
    }
    Tcl_ObjectSetMetadata(widget->object, &tkoWidgetMeta, NULL);
    Tcl_Release(widget);
    return;
}

/*
* Tko_WidgetClientData --
*    Return pointer to widget client data.
*
* Results:
*    None.
*
* Side effects:
*    None.
*/
ClientData Tko_WidgetClientData(
    Tcl_ObjectContext context)
{
    Tcl_Object object;
    if ((object = Tcl_ObjectContextObject(context)) == NULL) {
        return NULL;
    }
    return Tcl_ObjectGetMetadata(object, &tkoWidgetMeta);
}

/*
 * WidgetMetaDestroy --
 *    Free ressources.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Delete or give back all used internal ressources
 */
static void
WidgetMetaDestroy(
    Tko_Widget * widget)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *entryPtr;

    entryPtr = Tcl_FirstHashEntry(widget->optionsTable, &search);
    while (entryPtr != NULL) {
        WidgetOptionDelEntry(entryPtr);
        entryPtr = Tcl_NextHashEntry(&search);
    }
    if (widget->optionsTable) {
        Tcl_DeleteHashTable(widget->optionsTable);
        ckfree(widget->optionsTable);
    }
    if (widget->optionsArray != NULL) {
        Tcl_DecrRefCount((widget->optionsArray));
        widget->optionsArray = NULL;
    }
    ckfree(widget);
}

/*
* WidgetDeleteTkwin --
*    Resets internal Tk_Window in widget structure.
*
* Results:
*    None.
*
* Side effects:
*    Delete event handler of widget.
*    When the widget is wrappen then delete wrap widget command.
*/
static void WidgetDeleteTkwin(
    Tko_Widget *widget)
{
    Tcl_Obj *tmpObj;
    TkoThreadData *tkoPtr = (TkoThreadData *)Tcl_GetThreadData(&tkoKey, sizeof(TkoThreadData));
    Tk_DeleteEventHandler(widget->tkWin, StructureNotifyMask | VirtualEventMask,
        WidgetEventProc, widget);
    tmpObj = Tcl_ObjGetVar2(widget->interp, widget->optionsArray, tkoPtr->Obj_point2, TCL_GLOBAL_ONLY);
    if (tmpObj) {
        tmpObj = Tcl_ObjPrintf("rename %s {}", Tcl_GetString(tmpObj));
        Tcl_IncrRefCount(tmpObj);
        Tcl_EvalObjEx(widget->interp, tmpObj,TCL_GLOBAL_ONLY);
        Tcl_DecrRefCount(tmpObj);
    }
    else {
        Tk_DestroyWindow(widget->tkWin);
    }
    widget->tkWin = NULL;
}

/*
* WidgetEventProc --
*    This function is invoked by the Tk dispatcher for various events on
*    canvases.
*
* Results:
*    None.
*
* Side effects:
*    When the window gets deleted, internal structures get cleaned up.
*/
static void
WidgetEventProc(
    ClientData clientData,     /* Information about window. */
    XEvent * eventPtr)
{              /* Information about event. */
    Tko_Widget *widget = (Tko_Widget *)clientData;

    switch (eventPtr->type) {
    case DestroyNotify:
        if (widget->tkWin) {
            WidgetDeleteTkwin(widget);
            Tcl_DeleteCommandFromToken(widget->interp, widget->widgetCmd);
        }
        if (widget->myCmd) {
            Tcl_DecrRefCount(widget->myCmd);
            widget->myCmd = NULL;
        }
        break;
    case VirtualEvent:
        if (widget->tkWin) {
            if (!strcmp("TkoEventOption", ((XVirtualEvent *)(eventPtr))->name)) {
                WidgetEventChanged(widget);
            }
        }
    }
}

/*
* WidgetEventChanged --
*    Reset all option with no TKO_OPTION_USER bit from option database.
*    canvases.
*
* Results:
*    None.
*
* Side effects:
*  Apply changed option database values.
*/
static void
WidgetEventChanged(
    Tko_Widget *widget)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *entryPtr;
    WidgetOption *optionPtr;
    Tk_Uid valueUid;
    Tk_Uid dbnameUid;
    Tk_Uid dbclassUid;
    int changed;
    Tcl_Obj *defvalue;
    Tcl_Obj *myObjv[2];
    TkoThreadData *tkoPtr = (TkoThreadData *)Tcl_GetThreadData(&tkoKey, sizeof(TkoThreadData));

    if (widget->myCmd == NULL) return;
    Tcl_Preserve(widget);
    entryPtr = Tcl_FirstHashEntry(widget->optionsTable, &search);
    changed = 0;
    while (entryPtr != NULL) {
        optionPtr = (WidgetOption *)Tcl_GetHashValue(entryPtr);
        entryPtr = Tcl_NextHashEntry(&search);
        if (optionPtr->dbclass == NULL) continue;/* synonym option */
        if (optionPtr->dbname == tkoPtr->Obj_empty && optionPtr->dbclass == tkoPtr->Obj_empty) continue;
        if (optionPtr->flagbits & TKO_OPTION_READONLY) continue;/* readonly option */
        if (optionPtr->flagbits & TKO_OPTION__USER) continue;/* user changed option */
          /*
          * Get value from option database or
          * check for a system-specific default value.
          */
        dbnameUid = Tk_GetUid(Tcl_GetString(optionPtr->dbname));
        dbclassUid = Tk_GetUid(Tcl_GetString(optionPtr->dbclass));
        if ((valueUid = Tk_GetOption(widget->tkWin, dbnameUid, dbclassUid)) != NULL) {
            defvalue = Tcl_NewStringObj(valueUid, -1);
        }
        else {
            defvalue = TkpGetSystemDefault(widget->tkWin, dbnameUid, dbclassUid);
            if (defvalue == NULL) continue;
        }
        Tcl_IncrRefCount(defvalue);
        /* No need to set same value again */
        if (strcmp(Tcl_GetString(defvalue), Tcl_GetString(optionPtr->value)) == 0) {
            Tcl_DecrRefCount(defvalue);
            continue;
        }
        /* Set new value */
        if (WidgetOptionSet(widget->interp, widget, optionPtr->option, defvalue) != TCL_OK) {
            Tcl_DecrRefCount(defvalue);
            optionPtr->flagbits &= ~TKO_OPTION__USER;/* reset option */
            continue; /* no additional error handling here */
        }
        Tcl_DecrRefCount(defvalue);
        changed++;
    }
    if (changed) {
        myObjv[0] = widget->myCmd;
        myObjv[1] = tkoPtr->Obj__tko_configure;
        if (Tcl_EvalObjv(widget->interp, 2, myObjv, TCL_EVAL_GLOBAL) != TCL_OK) {
            /* ignore errors */
        }
    }
    Tcl_Release(widget);
}

/*
 * WidgetMethod_cget --
 *    Tcl syntax: "widget cget -option".
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *    Return option value in interpreter result.
 */
static int
WidgetMethod_cget(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tko_Widget *widget;         /* widget. */
    int skip;
    (void)dummy;

    if ((widget = (Tko_Widget *)Tko_WidgetClientData(context)) == NULL
        || widget->myCmd == NULL) {
        return TCL_ERROR;
    }
    skip = Tcl_ObjectContextSkippedArgs(context);

    if(objc - skip != 1) {
        Tcl_WrongNumArgs(interp, skip, objv, "option");
        return TCL_ERROR;
    }
    return WidgetOptionGet(interp, widget, objv[skip]);
}

/*
 * WidgetMethod_configure --
 *    Tcl syntax:
 *        configure
 *        configure "-option"
 *        configure "-option value .."
 *        configure "add option dbname dbclass ?default?"
 *        configure "del option"
 *        configure "after"
 *    Changing of option values:
 *    1.    set tk(-option)
 *  2.    WidgetTraceOption()
 *    3.    "my -option $v .."
 *
 * Results:
 *    A standard Tcl result. Return result value in interpreter result.
 *
 * Side effects:
 *    Can add, delete or change options.
 */
static int
WidgetMethod_configure(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    Tko_Widget *widget;         /* widget. */
    int skip;
    Tcl_Obj *myObjv[2];
    Tcl_HashSearch search;
    Tcl_HashEntry *entryPtr;
    WidgetOption *optionPtr;
    Tcl_Obj *retPtr;
    Tcl_Obj *listPtr;
    const char *ch;
    int length;
    int i;
    TkoThreadData *tkoPtr = (TkoThreadData *)Tcl_GetThreadData(&tkoKey, sizeof(TkoThreadData));
    (void)dummy;

    if ((widget = (Tko_Widget *)Tko_WidgetClientData(context)) == NULL
        || widget->myCmd == NULL) {
        return TCL_ERROR;
    }
    skip = Tcl_ObjectContextSkippedArgs(context);

    /* configure */
    if(objc - skip == 0) {
        retPtr = Tcl_NewObj();
        entryPtr = Tcl_FirstHashEntry(widget->optionsTable, &search);
        while(entryPtr != NULL) {
            optionPtr = (WidgetOption *) Tcl_GetHashValue(entryPtr);
            entryPtr = Tcl_NextHashEntry(&search);
            /* hidden option, not visible in configure method */
            if (optionPtr->flagbits&TKO_OPTION_HIDE) continue;
            listPtr = Tcl_NewObj();
            Tcl_ListObjAppendElement(interp, listPtr, optionPtr->option);
            Tcl_ListObjAppendElement(interp, listPtr, optionPtr->dbname);
            if (optionPtr->dbclass != NULL) {
                Tcl_ListObjAppendElement(interp, listPtr, optionPtr->dbclass);
                Tcl_ListObjAppendElement(interp, listPtr, optionPtr->defvalue);
                Tcl_ListObjAppendElement(interp, listPtr, optionPtr->value);
            }
            Tcl_ListObjAppendElement(interp, retPtr, listPtr);
        }
        /* Return sorted list */
        myObjv[0] = tkoPtr->Obj_lsort;
        myObjv[1] = retPtr;
        return (Tcl_EvalObjv(interp, 2, myObjv, TCL_EVAL_GLOBAL));
    }
    /* configure "-option ?value? .." */
    if(objc - skip == 1) {  /* configure -option */
        ch = Tcl_GetStringFromObj(objv[skip],&length);
        /* configure . */
        if(ch[0] == '.' && length == 1) {
            // collect all not readonly options and configure
            Tcl_Preserve(widget);
            myObjv[0] = widget->myCmd;
            entryPtr = Tcl_FirstHashEntry(widget->optionsTable, &search);
            while (entryPtr != NULL) {
                optionPtr = (WidgetOption *)Tcl_GetHashValue(entryPtr);
                entryPtr = Tcl_NextHashEntry(&search);
                if (optionPtr->dbclass == NULL) {    /* synonym option */
                    if (optionPtr->value) {
                        Tcl_ObjSetVar2(interp, widget->optionsArray,
                            optionPtr->dbname, optionPtr->value, TCL_GLOBAL_ONLY);
                        Tcl_DecrRefCount(optionPtr->value);
                        optionPtr->value = NULL;
                    }
                }
                else {    /* normal option */
                    if ((optionPtr->flagbits & TKO_OPTION_READONLY) == 0) {
                        myObjv[1] = optionPtr->option;
                        if (Tcl_EvalObjv(interp, 2, myObjv,
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
            }
            myObjv[1] = tkoPtr->Obj__tko_configure;
            if (Tcl_EvalObjv(interp, 2, myObjv, TCL_EVAL_GLOBAL) != TCL_OK) {
                retPtr = Tcl_GetObjResult(interp);
                Tcl_IncrRefCount(retPtr);
                Tcl_Release(widget);
                Tcl_DeleteCommandFromToken(interp, widget->widgetCmd);
                Tcl_SetObjResult(interp, retPtr);
                Tcl_DecrRefCount(retPtr);
                return TCL_ERROR;
            }
            Tcl_Release(widget);
            Tcl_SetObjResult(interp, Tcl_ObjGetVar2(interp, widget->optionsArray, tkoPtr->Obj_point, TCL_GLOBAL_ONLY));
            return TCL_OK;
        }
        entryPtr =
            Tcl_FindHashEntry(widget->optionsTable,
            Tk_GetUid(Tcl_GetString(objv[skip])));
        if(entryPtr == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown option \"%s\"",
                Tcl_GetString(objv[skip])));
            return TCL_ERROR;
        }
        optionPtr = (WidgetOption *) Tcl_GetHashValue(entryPtr);
        /* hidden option, not visible in configure method */
        if (optionPtr->flagbits&TKO_OPTION_HIDE) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("hidden option \"%s\"",
                Tcl_GetString(objv[skip])));
            return TCL_ERROR;
        }
        if (optionPtr->dbclass == NULL) {
            entryPtr =
                Tcl_FindHashEntry(widget->optionsTable,
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
        if (optionPtr->defvalue) {
            Tcl_ListObjAppendElement(interp, listPtr, optionPtr->defvalue);
        }
        else {
            Tcl_ListObjAppendElement(interp, listPtr, tkoPtr->Obj_empty);
        }
        Tcl_ListObjAppendElement(interp, listPtr, optionPtr->value);
        Tcl_SetObjResult(interp, listPtr);
        return TCL_OK;
    }
    /* configure "-option ?value? .." */
    if((objc - skip) % 2 == 0) {
        Tcl_Preserve(widget);
        for (i = skip; i < objc; i = i + 2) {
            if (WidgetOptionSet(interp, widget, objv[i], objv[i + 1]) != TCL_OK) {
                Tcl_Release(widget);
                return TCL_ERROR;
            }
        }
        myObjv[0] = widget->myCmd;
        myObjv[1] = tkoPtr->Obj__tko_configure;
        if (Tcl_EvalObjv(interp, 2, myObjv, TCL_EVAL_GLOBAL) != TCL_OK) {
            Tcl_Release(widget);
            return TCL_ERROR;
        }
        Tcl_Release(widget);
        return TCL_OK;
    }
    Tcl_WrongNumArgs(interp, skip, objv, "?-option value ..?");
    return TCL_ERROR;
}

/*
 * WidgetOptionAdd --
 *    Add a new option to a created widget.
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *    Add and initialize the new option.
 */
static int
WidgetOptionAdd(
    Tcl_Interp * interp, /* used interpreter */
    Tko_Widget * widget, /* currrent widget or NULL if only checks should be done  */
    Tcl_Obj * option, /* name of option, always given*/
    Tcl_Obj * dbname, /* dbname or synonym, always given */
    Tcl_Obj * dbclass, /* dbclass or NULL if synonym option */
    Tcl_Obj * defvalue, /* default value of option */
    Tcl_Obj * flags, /* value or NULL if synonym option */
    Tcl_Obj * value, /* initialization value */
    int initmode) /* 0 when adding to existing object, 1 when constructor, 2 when wrapped widget */
{
    Tcl_HashEntry *entryPtr;
    WidgetOption *optionPtr;
    Tk_Uid valueUid;
    int isNew;
    Tk_Uid optionUid;
    Tk_Uid dbnameUid;
    Tk_Uid dbclassUid;
    int intFlags;
    int readonly;
    Tcl_Obj *myObjv[2];
    const char *ch;
    const char *opt;
    int traceadd = 0; /* if not 0 then readd trace on array variable */
    int searchdb = 0; /* search optiondb for values */
    TkoThreadData *tkoPtr = (TkoThreadData *)Tcl_GetThreadData(&tkoKey, sizeof(TkoThreadData));

    if((opt=Tcl_GetString(option))[0] != '-') {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong option: %s", opt));
        return TCL_ERROR;
    }
    /* synonym option check */
    if(dbclass == NULL) {
        if((ch=Tcl_GetString(dbname))[0] != '-' || ch[1]=='\0') {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("wrong synonym: %s %s", opt, ch));
            return TCL_ERROR;
        }
    }
    /* int flag */
    intFlags = 0;
    if (flags && WidgetFlagsObj(flags,&intFlags) != TCL_OK) {
        Tcl_SetObjResult(interp,
            Tcl_ObjPrintf("wrong flags: %s %s",opt,Tcl_GetString(flags)));
        return TCL_ERROR;
    }
    if (intFlags & TKO_OPTION_READONLY) {
        intFlags &= ~TKO_OPTION_READONLY;
        readonly = TKO_OPTION_READONLY;
    }
    else {
        readonly = 0;
    }
    /* return if no widget given, all class checks are done */
    if(widget == NULL) {
        return TCL_OK;
    }
    optionUid = Tk_GetUid(opt);
    entryPtr = Tcl_CreateHashEntry(widget->optionsTable, optionUid, &isNew);
    if(isNew == 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("option exists: %s", opt));
        return TCL_ERROR;
    }
    /* create option */
    optionPtr = (WidgetOption *)ckalloc(sizeof(WidgetOption));
    assert(optionPtr);
    memset(optionPtr, 0, sizeof(WidgetOption));
    optionPtr->option = option;
    Tcl_IncrRefCount(optionPtr->option);
    if (Tcl_GetString(dbname)[0] == '\0') {
        optionPtr->dbname = tkoPtr->Obj_empty;
        searchdb++;
    }
    else {
        optionPtr->dbname = dbname;
    }
    Tcl_IncrRefCount(optionPtr->dbname);
    Tcl_SetHashValue(entryPtr, (char *)optionPtr);
    if (flags) {
        optionPtr->flags = flags;
    }
    else {
        optionPtr->flags = tkoPtr->Obj_empty;
    }
    Tcl_IncrRefCount(optionPtr->flags);
    optionPtr->flagbits = intFlags;
    /* synonym options can have flags.
     * Need to check usage of init value! */
    if(dbclass == NULL) {
        optionPtr->dbclass = NULL;
        optionPtr->defvalue = NULL;
        if(value) {
            optionPtr->value = value;
            Tcl_IncrRefCount(optionPtr->value);
        }
        /* normal option */
    } else {
        if (Tcl_GetString(dbclass)[0] == '\0') {
            optionPtr->dbclass = tkoPtr->Obj_empty;
            dbclassUid = tkoPtr->Uid_empty;
            searchdb++;
        }
        else {
            dbclassUid = Tk_GetUid(Tcl_GetString(dbclass));
            optionPtr->dbclass = dbclass;
        }
        Tcl_IncrRefCount(optionPtr->dbclass);

        optionPtr->defvalue = defvalue;
        Tcl_IncrRefCount(optionPtr->defvalue);

        /*
         * If value is given use it.
         */
        if(value) {
            optionPtr->value = value;
            optionPtr->flagbits |= TKO_OPTION__USER;
        } else {
            if (searchdb < 2 && widget->tkWin != NULL) {
                /*
                 * Get value from option database
                 */
                dbnameUid = Tk_GetUid(Tcl_GetString(dbname));
                if (optionPtr->value == NULL) {
                    valueUid = Tk_GetOption(widget->tkWin, dbnameUid, dbclassUid);
                    if (valueUid != NULL) {
                        optionPtr->value = Tcl_NewStringObj(valueUid, -1);
                    }
                }
                /*
                 * Check for a system-specific default value.
                 * Do not for -class because Tcl_SetClass was not called.
                 * When -class is not first option (after -screen) we get a crash!
                 */
                if (optionPtr->value == NULL && optionUid != tkoPtr->Uid_class) {
                    optionPtr->value =
                        TkpGetSystemDefault(widget->tkWin, dbnameUid, dbclassUid);
                }
            }
            /*
             * Use default value.
             */
            if(optionPtr->value == NULL) {
                optionPtr->value = defvalue;
                optionPtr->flagbits |= TKO_OPTION__USER;
            }
        }
        /*
         * No given value defaults to empty string.
         */
        if(optionPtr->value == NULL) {
            optionPtr->value = tkoPtr->Obj_empty;
            /* No flag as this does not count as user supplied */
        }
        Tcl_IncrRefCount(optionPtr->value);
        /*
         * Outside initmode the trace on the array variable needs to be disabled.
         */
        if (initmode == 0) {
            Tcl_UntraceVar2(interp, Tcl_GetString(widget->optionsArray), NULL,
                TCL_TRACE_WRITES | TCL_TRACE_RESULT_OBJECT, WidgetOptionTrace, widget);
            traceadd = 1;
        }
        /*
         *Set option array variable
         */
        if (Tcl_ObjSetVar2(interp, widget->optionsArray, option,
            optionPtr->value, TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG) == NULL) {
            goto error;
        }
        /*
         * Do initialization with -option method.
         * We do it for readonly options only here.
         * And we do it for options added with "configure optionadd ..".
         */
        if (readonly || initmode == 0) {
            if (initmode != 2) {
                myObjv[0] = widget->myCmd;
                myObjv[1] = option;
                if (Tcl_EvalObjv(interp, 2, myObjv, TCL_EVAL_GLOBAL) != TCL_OK) {
                    goto error;
                }
                /*
                * We set the value again because the -option method may have changed it.
                */
                if (optionPtr->value) {
                    Tcl_DecrRefCount(optionPtr->value);
                }
                optionPtr->value = Tcl_ObjGetVar2(interp, widget->optionsArray, option, TCL_GLOBAL_ONLY);
                Tcl_IncrRefCount(optionPtr->value);
            }
            /* Now we are ready to set the readonly bit */
            if (readonly) {
                optionPtr->flagbits |= TKO_OPTION_READONLY;
            }
        }
    }
    if (traceadd) {
        Tcl_TraceVar2(interp, Tcl_GetString(widget->optionsArray), NULL,
            TCL_TRACE_WRITES | TCL_TRACE_RESULT_OBJECT, WidgetOptionTrace, widget);
    }
    return TCL_OK;
error:
    if (traceadd) {
        /* There should be no error and thus we don't need to save the result. */
        Tcl_TraceVar2(interp, Tcl_GetString(widget->optionsArray), NULL,
            TCL_TRACE_WRITES | TCL_TRACE_RESULT_OBJECT, WidgetOptionTrace, widget);
    }
    WidgetOptionDelEntry(entryPtr);
    return TCL_ERROR;
}

/*
 * WidgetOptionGet --
 *    Get option value.
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *    return current vlaue of widget option.
 */
static int
WidgetOptionGet(
    Tcl_Interp * interp,
    Tko_Widget * widget,
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
        Tcl_FindHashEntry(widget->optionsTable,
        Tk_GetUid(Tcl_GetString(option)));
    if(entryPtr == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown option \"%s\"",
                Tcl_GetString(option)));
        return TCL_ERROR;
    }
    optionPtr = (WidgetOption *)Tcl_GetHashValue(entryPtr);
    /* hidden option, not visible in cget method */
    if (optionPtr->flagbits&TKO_OPTION_HIDE) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("hidden option \"%s\"",
            Tcl_GetString(option)));
        return TCL_ERROR;
    }
    /* synonym option */
    if(optionPtr->dbclass == NULL) {
        entryPtr =
            Tcl_FindHashEntry(widget->optionsTable,
            Tk_GetUid(Tcl_GetString(optionPtr->dbname)));
        if(entryPtr == NULL) {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("unknown synonym option \"%s\"",
                    Tcl_GetString(option)));
            return TCL_ERROR;
        }
        optionPtr = (WidgetOption *)Tcl_GetHashValue(entryPtr);
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
 *    Set new widget option value.
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *    Set option value and call
 */
static int
WidgetOptionSet(
    Tcl_Interp * interp,
    Tko_Widget * widget,
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
        Tcl_FindHashEntry(widget->optionsTable,
        Tk_GetUid(Tcl_GetString(option)));
    if(entryPtr == NULL) {
        Tcl_SetObjResult(interp,
            Tcl_ObjPrintf("unknown option \"%s\"", Tcl_GetString(option)));
        return TCL_ERROR;
    }
    optionPtr = (WidgetOption *)Tcl_GetHashValue(entryPtr);
    /* hidden option, not visible in cget method */
    if (optionPtr->flagbits&TKO_OPTION_HIDE) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("hidden option \"%s\"",
            Tcl_GetString(option)));
        return TCL_ERROR;
    }
    /* synonym option */
    if(optionPtr->dbclass == NULL) {
        entryPtr =
            Tcl_FindHashEntry(widget->optionsTable,
            Tk_GetUid(Tcl_GetString(optionPtr->dbname)));
        if(entryPtr == NULL) {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("unknown synonym option \"%s\"",
                    Tcl_GetString(option)));
            return TCL_ERROR;
        }
        optionPtr = (WidgetOption *)Tcl_GetHashValue(entryPtr);
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
    optionPtr->flagbits |= TKO_OPTION__USER;
    return TCL_OK;
}

/*
* Tko_WidgetOptionGet --
*
* Results:
*    Return TclObj value of option or NULL if widget is destroyed.
*
* Side effects:
*/
Tcl_Obj *
Tko_WidgetOptionGet(
    Tko_Widget *widget,
    Tcl_Obj *option)
{
    if (widget->optionsArray == NULL || option ==NULL) return NULL;
    return Tcl_ObjGetVar2(widget->interp, widget->optionsArray, option,
        TCL_GLOBAL_ONLY);
}

/*
 * Tko_WidgetOptionSet --
 *    Set option value.
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 *    Create necessary C-values.
 */
Tcl_Obj *
Tko_WidgetOptionSet(
    Tko_Widget *widget,
    Tcl_Obj * option,
    Tcl_Obj * value)
{
    if (widget->optionsArray == NULL || option==NULL || value == NULL) return NULL;
    return Tcl_ObjSetVar2(widget->interp, widget->optionsArray, option, value,
        TCL_GLOBAL_ONLY);
}

/*
 * WidgetOptionTrace --
 * Write trace on option array variable
 *
 * Results:
 *    Return NULL if successfull and leave error message otherwise.
 *
 * Side effects:
 *    Check on existence of option and call "-option" method with new value.
 */
static char *
WidgetOptionTrace(
    ClientData clientData,
    Tcl_Interp * interp,
    const char *name1,
    const char *name2,
    int flags)
{
    Tko_Widget *widget = (Tko_Widget *) clientData;
    Tcl_HashEntry *entryPtr;
    Tcl_Obj *valuePtr;
    //    const char *result;
    WidgetOption *optionPtr;
    Tcl_Obj *myObjv[2];
    Tcl_Obj *myRet;
    (void)name1;
    (void)flags;

    /* get new value */
    entryPtr = Tcl_FindHashEntry(widget->optionsTable, Tk_GetUid(name2));
    if(entryPtr == NULL) {
        myRet = Tcl_ObjPrintf("option \"%s\" not found", name2);
        Tcl_IncrRefCount(myRet);
        return (char *)myRet;
    }
    optionPtr = (WidgetOption *) Tcl_GetHashValue(entryPtr);
    if(optionPtr->flagbits & TKO_OPTION_READONLY) {
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
    valuePtr = Tcl_ObjGetVar2(interp, widget->optionsArray, optionPtr->option, TCL_GLOBAL_ONLY);
    optionPtr->value = valuePtr;
    Tcl_IncrRefCount(optionPtr->value);
    return NULL;
}

/*
 * WidgetOptionDelEntry --
 *    Delete internal entry value.
 *
 * Results:
 *    None.
 *
 * Side effects:
 */
static void
WidgetOptionDelEntry(
    Tcl_HashEntry * entry)
{
    WidgetOption *optionPtr = (WidgetOption *)Tcl_GetHashValue(entry);

    if(optionPtr->option)
        Tcl_DecrRefCount(optionPtr->option);
    if(optionPtr->dbname)
        Tcl_DecrRefCount(optionPtr->dbname);
    if(optionPtr->dbclass)
        Tcl_DecrRefCount(optionPtr->dbclass);
    if(optionPtr->flags)
        Tcl_DecrRefCount(optionPtr->flags);
    if(optionPtr->defvalue)
        Tcl_DecrRefCount(optionPtr->defvalue);
    if(optionPtr->value)
        Tcl_DecrRefCount(optionPtr->value);
    ckfree(optionPtr);
    Tcl_DeleteHashEntry(entry);
}

/*
 * WidgetMethod_tko_configure --
 *    Virtual method called after configuring options.
 *    Should be implemented in derived classes.
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 */
static int
WidgetMethod_tko_configure(
    ClientData dummy,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{              /* virtual method */
    (void)dummy;
    (void)interp;
    (void)context;
    (void)objc;
    (void)objv;

    return TCL_OK;
}

/*
 * WidgetMetaDelete --
 *    Delete widget meta data when all preserve calls done.
 *
 * Results:
 *    None.
 *
 * Side effects:
 */
static void
WidgetMetaDelete(
    ClientData clientData)
{
    (void)clientData;
    /* Tcl_EventuallyFree(clientData, (Tcl_FreeProc *)WidgetMetaDestroy); */
}

/*
 * WidgetMethod_ --
 *    Standard option set method.
 *
 * Results:
 *    A standard Tcl result.
 *
 * Side effects:
 */
static int
WidgetMethod_(
    ClientData clientdata,
    Tcl_Interp * interp,
    Tcl_ObjectContext context,
    int objc,
    Tcl_Obj * const objv[])
{
    WidgetClientdata *define;
    Tcl_Object object;
    Tko_Widget *widget;
    Tcl_Obj *value;
    char *address = NULL;
    int intVal;
    double dblVal;
    Colormap colormap;
    int *intPtr;
    const char *str;
    int length;
    int pixels[4] = { 0, 0, 0, 0 };
    int myObjc;
    Tcl_Obj **myObjv;
    Visual * visual;
    XColor * color;
    Tk_3DBorder border;
    Tk_Anchor anchor;
    Tk_Cursor cursor;
    Tk_Window newWin;
    Tk_Font newFont;
    Tk_Justify justify;
    (void)objc;

    if ((define = (WidgetClientdata *)clientdata) == NULL
        || (object = Tcl_ObjectContextObject(context)) == NULL
        || (widget = (Tko_Widget *)Tcl_ObjectGetMetadata(object, &tkoWidgetMeta))
            == NULL
        || (value = Tcl_ObjGetVar2(interp, widget->optionsArray, define->option,
            TCL_GLOBAL_ONLY)) == NULL
        || widget->myCmd == NULL) {
        return TCL_ERROR;
    }
    if (define->offset > 0) {
        address = ((char *)widget) + define->offset;
    }

    switch (define->type) {
    case TKO_SET_CLASS:        /* (Tcl_Obj **)address */
        Tk_SetClass(widget->tkWin, Tcl_GetString(value));
        if (address) {
            if (*((Tcl_Obj **)address) != NULL)
                Tcl_DecrRefCount(*((Tcl_Obj **)address));
            *((Tcl_Obj **)address) = value;
            Tcl_IncrRefCount(value);
        }
        return TCL_OK;
    case TKO_SET_VISUAL:       /* (Tcl_Obj **)address */
        visual =
            Tk_GetVisual(interp, widget->tkWin, Tcl_GetString(value), &intVal,
                &colormap);
        if (visual == NULL)
            return TCL_ERROR;
        Tk_SetWindowVisual(widget->tkWin, visual, intVal, colormap);
        if (address) {
            if (*((Tcl_Obj **)address) != NULL)
                Tcl_DecrRefCount(*((Tcl_Obj **)address));
            *((Tcl_Obj **)address) = value;
            Tcl_IncrRefCount(value);
        }
        return TCL_OK;
    case TKO_SET_COLORMAP:     /* (Tcl_Obj **)address */
        str = Tcl_GetStringFromObj(value, &length);
        if (str && length) {
            colormap = Tk_GetColormap(interp, widget->tkWin, str);
            if (colormap == None)
                return TCL_ERROR;
            Tk_SetWindowColormap(widget->tkWin, colormap);
        }
        if (address) {
            if (*((Tcl_Obj **)address) != NULL)
                Tcl_DecrRefCount(*((Tcl_Obj **)address));
            *((Tcl_Obj **)address) = value;
            Tcl_IncrRefCount(value);
        }
        return TCL_OK;
    case TKO_SET_USE:      /* (Tcl_Obj **)address */
        str = Tcl_GetStringFromObj(value, &length);
        if (str && length) {
            if (TkpUseWindow(interp, widget->tkWin, str) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        else if (!(define->flags & TKO_OPTION_NULL)) {
            return TCL_ERROR;

        }
        if (address) {
            if (*((Tcl_Obj **)address) != NULL)
                Tcl_DecrRefCount(*((Tcl_Obj **)address));
            if (length) {
                *((Tcl_Obj **)address) = value;
                Tcl_IncrRefCount(value);
            }
            else {
                *((Tcl_Obj **)address) = NULL;
            }
        }
        return TCL_OK;
    case TKO_SET_CONTAINER:    /* (int *)address */
        if (Tcl_GetBooleanFromObj(interp, value, &intVal) != TCL_OK)
            return TCL_ERROR;
        if (intVal) {
            TkpMakeContainer(widget->tkWin);
            Tcl_ObjSetVar2(interp, widget->optionsArray, objv[1], Tcl_NewIntObj(1),
                TCL_GLOBAL_ONLY);
        }
        else {
            Tcl_ObjSetVar2(interp, widget->optionsArray, objv[1], Tcl_NewIntObj(0),
                TCL_GLOBAL_ONLY);
        }
        if (address) {
            *(int *)address = intVal;
        }
        return TCL_OK;
    case TKO_SET_TCLOBJ:       /* (Tcl_Obj **)address */
        if (address) {
            if (*((Tcl_Obj **)address) != NULL)
                Tcl_DecrRefCount(*((Tcl_Obj **)address));
            *((Tcl_Obj **)address) = value;
            Tcl_IncrRefCount(value);
        }
        return TCL_OK;
    case TKO_SET_XCOLOR:       /* (Xcolor **)address */
        color = Tk_AllocColorFromObj(interp, widget->tkWin, value);
        if (color == NULL)
            return TCL_ERROR;
        if (address) {
            if (*((XColor **)address) != NULL) {
                Tk_FreeColor(*((XColor **)address));
            }
            *((XColor **)address) = color;
        }
        else {
            Tk_FreeColor(color);
        }
        return TCL_OK;
    case TKO_SET_3DBORDER:     /* (Tk_3DBorder *)address */
        str = Tcl_GetStringFromObj(value, &length);
        if (str && length) {
            border = Tk_Alloc3DBorderFromObj(interp, widget->tkWin, value);
            if (border == NULL)
                return TCL_ERROR;
        }
        else if (define->flags & TKO_OPTION_NULL) {
            border = NULL;
        } else {
            return TCL_ERROR;
        }
        if (address) {
            if (*(Tk_3DBorder *)address != NULL) {
                Tk_Free3DBorder(*(Tk_3DBorder *)address);
            }
            *(Tk_3DBorder *)address = border;
        }
        else {
            Tk_Free3DBorder(border);
        }
        return TCL_OK;
    case TKO_SET_PIXEL:        /* (int *)address */
        if (Tk_GetPixelsFromObj(interp, widget->tkWin, value, &intVal) != TCL_OK) {
            return TCL_ERROR;
        }
        if (address) {
            *(int *)address = intVal;
        }
        Tcl_ObjSetVar2(interp, widget->optionsArray, objv[1],
            Tcl_NewIntObj(intVal), TCL_GLOBAL_ONLY);
        return TCL_OK;
    case TKO_SET_PIXELNONEGATIV:       /* (int *)address */
        if (Tk_GetPixelsFromObj(interp, widget->tkWin, value, &intVal) != TCL_OK) {
            return TCL_ERROR;
        }
        if (intVal >= SHRT_MAX) {
            Tcl_AppendResult(interp, "bad distance \"", Tcl_GetString(value),
                "\": ", "too big to represent", (char *)NULL);
            return TCL_ERROR;
        }
        if (intVal < 0) {
            Tcl_AppendResult(interp, "bad distance \"", Tcl_GetString(value),
                "\": ", "can't be negative", (char *)NULL);
            return TCL_ERROR;
        }
        if (address) {
            *(int *)address = intVal;
        }
        Tcl_ObjSetVar2(interp, widget->optionsArray, objv[1],
            Tcl_NewIntObj(intVal), TCL_GLOBAL_ONLY);
        return TCL_OK;
    case TKO_SET_PIXELPOSITIV: /* (int *)address */
        if (Tk_GetPixelsFromObj(interp, widget->tkWin, value, &intVal) != TCL_OK) {
            return TCL_ERROR;
        }
        if (intVal >= SHRT_MAX) {
            Tcl_AppendResult(interp, "bad distance \"", Tcl_GetString(value),
                "\": ", "too big to represent", (char *)NULL);
            return TCL_ERROR;
        }
        if (intVal <= 0) {
            Tcl_AppendResult(interp, "bad distance \"", Tcl_GetString(value),
                "\": ", "must be positive", (char *)NULL);
            return TCL_ERROR;
        }
        if (address) {
            *(int *)address = intVal;
        }
        Tcl_ObjSetVar2(interp, widget->optionsArray, objv[1],
            Tcl_NewIntObj(intVal), TCL_GLOBAL_ONLY);
        return TCL_OK;
    case TKO_SET_DOUBLE:       /* (double *)address */
        if (Tcl_GetDoubleFromObj(interp, value, &dblVal) != TCL_OK) {
            return TCL_ERROR;
        }
        if (address) {
            *(double *)address = dblVal;
        }
        Tcl_ObjSetVar2(interp, widget->optionsArray, objv[1],
            Tcl_NewDoubleObj(dblVal), TCL_GLOBAL_ONLY);
        return TCL_OK;
    case TKO_SET_BOOLEAN:      /* (int *)address */
        if (Tcl_GetBooleanFromObj(interp, value, &intVal) != TCL_OK) {
            return TCL_ERROR;
        }
        if (intVal) {
            Tcl_ObjSetVar2(interp, widget->optionsArray, objv[1], Tcl_NewIntObj(1),
                TCL_GLOBAL_ONLY);
        }
        else {
            Tcl_ObjSetVar2(interp, widget->optionsArray, objv[1], Tcl_NewIntObj(0),
                TCL_GLOBAL_ONLY);
        }
        if (address) {
            *(int *)address = intVal;
        }
        Tcl_ObjSetVar2(interp, widget->optionsArray, objv[1],
            Tcl_NewIntObj(intVal), TCL_GLOBAL_ONLY);
        return TCL_OK;
    case TKO_SET_CURSOR:       /* (Tk_Cursor *)address */
        cursor = NULL;
        if (Tcl_GetString(value)[0] != '\0') {
            cursor = Tk_AllocCursorFromObj(interp, widget->tkWin, value);
            if (cursor == NULL) {
                return TCL_ERROR;
            }
            Tk_DefineCursor(widget->tkWin, cursor);
        }
        if (address) {
            if (*(Tk_Cursor *)address != NULL) {
                Tk_FreeCursor(Tk_Display(widget->tkWin),
                    *(Tk_Cursor *)address);
            }
            *(Tk_Cursor *)address = cursor;
        }
        else {
            if (cursor != NULL) {
                Tk_FreeCursor(Tk_Display(widget->tkWin), cursor);/*TODO necessary? */
            }
        }
        return TCL_OK;
    case TKO_SET_INT:  /* (int *)address */
        if (Tcl_GetIntFromObj(interp, value, &intVal) != TCL_OK) {
            return TCL_ERROR;
        }
        if (address) {
            *(int *)address = intVal;
        }
        Tcl_ObjSetVar2(interp, widget->optionsArray, objv[1],
            Tcl_NewIntObj(intVal), TCL_GLOBAL_ONLY);
        return TCL_OK;
    case TKO_SET_RELIEF:       /* (int *)address */
        if (Tk_GetReliefFromObj(interp, value, &intVal) != TCL_OK) {
            return TCL_ERROR;
        }
        if (address) {
            *(int *)address = intVal;
        }
        return TCL_OK;
    case TKO_SET_ANCHOR:       /* (Tk_Anchor *)address */
        if (Tk_GetAnchorFromObj(interp, value, &anchor) != TCL_OK) {
            return TCL_ERROR;
        }
        if (address) {
            *(Tk_Anchor *)address = anchor;
        }
        return TCL_OK;
    case TKO_SET_WINDOW:       /* (Tk_Window *)address */
        if (value == NULL || Tcl_GetCharLength(value) == 0) {
            newWin = NULL;
        }
        else {
            if (TkGetWindowFromObj(interp, widget->tkWin, value,
                &newWin) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        if (address) {
            *(Tk_Window *)address = newWin;
        }
        return TCL_OK;
    case TKO_SET_FONT: /* (Tk_Font *)address */
        newFont = Tk_AllocFontFromObj(interp, widget->tkWin, value);
        if (newFont == NULL) {
            return TCL_ERROR;
        }
        if (address) {
            if (*(Tk_Font *)address != NULL) {
                Tk_FreeFont(*(Tk_Font *)address);
            }
            *(Tk_Font *)address = newFont;
        }
        else {
            Tk_FreeFont(newFont);
        }
        return TCL_OK;
    case TKO_SET_STRING:   /* (char **)address */
        if (address) {
            str = Tcl_GetStringFromObj(value, &length);
            if (*(char **)address != NULL) {
                ckfree(*(char **)address);
            }
            if (length == 0 && define->flags&TKO_OPTION_NULL) {
                *(char **)address = NULL;
            }
            else {
                *(char **)address=(char *)ckalloc(length + 1);
                assert(*(char **)address);
                memcpy(*(char **)address, str, length + 1);
            }
        }
        return TCL_OK;
    case TKO_SET_SCROLLREGION: /* (int *[4])address */
        if (Tcl_ListObjGetElements(interp, value, &myObjc, &myObjv) != TCL_OK) {
            return TCL_ERROR;
        }
        if (myObjc == 4) {
            if (Tk_GetPixelsFromObj(interp, widget->tkWin, myObjv[0],
                &pixels[0]) != TCL_OK
                || Tk_GetPixelsFromObj(interp, widget->tkWin, myObjv[1],
                    &pixels[1]) != TCL_OK
                || Tk_GetPixelsFromObj(interp, widget->tkWin, myObjv[2],
                    &pixels[2]) != TCL_OK
                || Tk_GetPixelsFromObj(interp, widget->tkWin, myObjv[3],
                    &pixels[3]) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        else if (myObjc != 0) {
            Tcl_SetObjResult(interp,
                Tcl_ObjPrintf("found %d instead of 4 values", myObjc));
            return TCL_ERROR;
        }
        if (address) {
            intPtr = (int *)address;
            intPtr[0] = pixels[0];
            intPtr[1] = pixels[1];
            intPtr[2] = pixels[2];
            intPtr[3] = pixels[3];
        }
        return TCL_OK;
    case TKO_SET_JUSTIFY:      /* (Tk_Justify *)address */
        if (Tk_GetJustify(interp, Tk_GetUid(Tcl_GetString(value)),
            &justify) != TCL_OK) {
            return TCL_ERROR;
        }
        if (address) {
            *(Tk_Justify *)address = justify;
        }
        return TCL_OK;
    }

    Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown type \"%d\"", define->type));
    return TCL_ERROR;
}

/*
* WidgetMethod_ --
*    Check given flagsPtr object and if flags is given set int value from string.
*
* Results:
*    A standard Tcl result.
*
* Side effects:
*/
static int WidgetFlagsObj(Tcl_Obj *flagsPtr, int *flags)
{
    char *ch;
    int retValue = 0;
    if (flagsPtr == NULL) return TCL_ERROR;
    ch = Tcl_GetString(flagsPtr);
    if (ch[0] != '\0') {
        if (ch[0] == 'r') {
            retValue |= TKO_OPTION_READONLY;
            if (ch[1] != '\0') {
                if (ch[1] == 'h') {
                    retValue |= TKO_OPTION_HIDE;
                }
                else {
                    return TCL_ERROR;
                }
            }
        }
        else if (ch[0] == 'h') {
            retValue |= TKO_OPTION_HIDE;
            if (ch[1] != '\0') {
                if (ch[1] == 'r') {
                    retValue |= TKO_OPTION_READONLY;
                }
                else {
                    return TCL_ERROR;
                }
            }
        }
        else {
            return TCL_ERROR;
        }
    }
    if (flags) {
        *flags |= retValue;
    }
    return TCL_OK;
}

/*
* WidgetFlagsHideGet --
*    Return 1 if option is hidden and 0 otherwise.
*
* Results:
*    Return 1 if option is hidden and 0 otherwise.
*
* Side effects:
*/
static int WidgetFlagsHideGet(Tcl_Obj *flags)
{
    const char *ch;

    ch = Tcl_GetString(flags);
    if (ch[0] == 'h' || (ch[0] == 'r' && ch[1] == 'h')) {
        return 1;
    }
    return 0;
}

/*
* WidgetFlagsHideSet --
*    Set hidden option state.
*
* Results:
*    Return object with new state.
*
* Side effects:
*/
static Tcl_Obj *WidgetFlagsHideSet(
    Tcl_Obj *flags) /* last flag value object */
{
    const char *ch;
    TkoThreadData *tkoPtr = (TkoThreadData *)Tcl_GetThreadData(&tkoKey, sizeof(TkoThreadData));

    ch = Tcl_GetString(flags);
    if (ch[0] != '\0' && (ch[0] == 'r' || ch[1] == 'r')) {
        return tkoPtr->Obj_flags_rh;
    }
    return tkoPtr->Obj_flags_h;
}

/*
* WidgetFlagsHideUnset --
*    Unset hidden option state.
*
* Results:
*    Return object with new state.
*
* Side effects:
*/
static Tcl_Obj *WidgetFlagsHideUnset(
    Tcl_Obj *flags) /* last flag value object */
{
    const char *ch;
    TkoThreadData *tkoPtr = (TkoThreadData *)Tcl_GetThreadData(&tkoKey, sizeof(TkoThreadData));

    ch = Tcl_GetString(flags);
    if (ch[0] != '\0') {
        if (ch[0] == 'h') {
            if (ch[1] == 'r') {
                return tkoPtr->Obj_flags_r;
            }
            else {
                return tkoPtr->Obj_empty;
            }
        }
        else {
            if (ch[1] == 'h') {
                return tkoPtr->Obj_flags_r;
            }
        }
    }
    return tkoPtr->Obj_empty;
}

/*
* WidgetClientdataDelete --
*    Delete widget internal method clientdata.
*
* Results:
*    None.
*
* Side effects:
*    Free memory.
*/
static void WidgetClientdataDelete(
    ClientData clientdata)
{
    WidgetClientdata *cd = (WidgetClientdata *)clientdata;
    Tcl_DecrRefCount(cd->option);
    ckfree(cd);
}

/*
* WidgetClientdataClone --
*    Copy widget internal method clientdata.
*
* Results:
*    Return copied clientdata in newPtr.
*
* Side effects:
*/
static int WidgetClientdataClone(
    Tcl_Interp *dummy,
    ClientData clientdata,
    ClientData *newPtr)
{
    WidgetClientdata *cd = (WidgetClientdata *)clientdata;
    (void)dummy;

    if (cd) {
        *newPtr = ckalloc(sizeof(WidgetClientdata));
        assert(*newPtr);
        memcpy(*newPtr, cd, sizeof(WidgetClientdata));
        Tcl_IncrRefCount(cd->option);
    }
    return TCL_OK;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
