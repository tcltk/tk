/*
 * rbcSwitch.c --
 *
 *      This module implements command/argument switch parsing
 *      procedures for the rbc toolkit.
 *
 * Copyright (c) 2001 BLT was created by George Howlett.
 * Copyright (c) 2009 RBC was created by Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * Copyright (c) 2018 Rene Zaumseil

 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "rbcInt.h"

static RbcSwitchSpec *FindSwitchSpec(
    Tcl_Interp * interp,
    RbcSwitchSpec * specs,
    const char *name,
    int needFlags,
    int hateFlags);
static int      DoSwitch(
    Tcl_Interp * interp,
    RbcSwitchSpec * specPtr,
    const char *string,
    ClientData record);

/*
 *--------------------------------------------------------------
 *
 * FindSwitchSpec --
 *
 *      Search through a table of configuration specs, looking for
 *      one that matches a given argvName.
 *
 * Results:
 *      The return value is a pointer to the matching entry, or NULL
 *      if nothing matched.  In that case an error message is left
 *      in the interp's result.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------
 */
static RbcSwitchSpec *
FindSwitchSpec(
    Tcl_Interp * interp,        /* Used for reporting errors. */
    RbcSwitchSpec * specs,      /* Pointer to table of configuration
                                 * specifications for a widget. */
    const char *name,           /* Name (suitable for use in a "switch"
                                 * command) identifying particular option. */
    int needFlags,              /* Flags that must be present in matching
                                 * entry. */
    int hateFlags)
{                               /* Flags that must NOT be present in
                                 * matching entry. */
    register RbcSwitchSpec *specPtr;
    register char   c;          /* First character of current argument. */
    RbcSwitchSpec  *matchPtr;   /* Matching spec, or NULL. */
    size_t          length;

    c = name[1];
    length = strlen(name);
    matchPtr = NULL;

    for (specPtr = specs; specPtr->type != RBC_SWITCH_END; specPtr++) {
        if (specPtr->switchName == NULL) {
            continue;
        }
        if ((specPtr->switchName[1] != c)
            || (strncmp(specPtr->switchName, name, length) != 0)) {
            continue;
        }
        if (((specPtr->flags & needFlags) != needFlags)
            || (specPtr->flags & hateFlags)) {
            continue;
        }
        if (specPtr->switchName[length] == 0) {
            return specPtr;     /* Stop on a perfect match. */
        }
        if (matchPtr != NULL) {
            Tcl_AppendResult(interp, "ambiguous option \"", name, "\"",
                (char *) NULL);
            return (RbcSwitchSpec *) NULL;
        }
        matchPtr = specPtr;
    }

    if (matchPtr == NULL) {
        Tcl_AppendResult(interp, "unknown option \"", name, "\"",
            (char *) NULL);
        return (RbcSwitchSpec *) NULL;
    }
    return matchPtr;
}

/*
 *--------------------------------------------------------------
 *
 * DoSwitch --
 *
 *      This procedure applies a single configuration option
 *      to a widget record.
 *
 * Results:
 *      A standard Tcl return value.
 *
 * Side effects:
 *      WidgRec is modified as indicated by specPtr and value.
 *      The old value is recycled, if that is appropriate for
 *      the value type.
 *
 *--------------------------------------------------------------
 */
static int
DoSwitch(
    Tcl_Interp * interp,        /* Interpreter for error reporting. */
    RbcSwitchSpec * specPtr,    /* Specifier to apply. */
    const char *string,         /* Value to use to fill in widgRec. */
    ClientData record)
{                               /* Record whose fields are to be
                                 * modified.  Values must be properly
                                 * initialized. */
    char           *ptr;
    int             isNull;
    int             count;

    isNull = ((*string == '\0') && (specPtr->flags & RBC_SWITCH_NULL_OK));
    do {
        ptr = (char *) record + specPtr->offset;
        switch (specPtr->type) {
        case RBC_SWITCH_BOOLEAN:
            if (Tcl_GetBoolean(interp, string, (int *) ptr) != TCL_OK) {
                return TCL_ERROR;
            }
            break;

        case RBC_SWITCH_INT:
            if (Tcl_GetInt(interp, string, (int *) ptr) != TCL_OK) {
                return TCL_ERROR;
            }
            break;

        case RBC_SWITCH_INT_NONNEGATIVE:
            if (Tcl_GetInt(interp, string, &count) != TCL_OK) {
                return TCL_ERROR;
            }
            if (count < 0) {
                Tcl_AppendResult(interp, "bad value \"", string, "\": ",
                    "can't be negative", (char *) NULL);
                return TCL_ERROR;
            }
            *((int *) ptr) = count;
            break;

        case RBC_SWITCH_INT_POSITIVE:
            if (Tcl_GetInt(interp, string, &count) != TCL_OK) {
                return TCL_ERROR;
            }
            if (count <= 0) {
                Tcl_AppendResult(interp, "bad value \"", string, "\": ",
                    "must be positive", (char *) NULL);
                return TCL_ERROR;
            }
            *((int *) ptr) = count;
            break;

        case RBC_SWITCH_DOUBLE:
            if (Tcl_GetDouble(interp, string, (double *) ptr) != TCL_OK) {
                return TCL_ERROR;
            }
            break;

        case RBC_SWITCH_STRING:{
                char           *old, *new, **strPtr;

                strPtr = (char **) ptr;
                if (isNull) {
                    new = NULL;
                } else {
                    new = RbcStrdup(string);
                }
                old = *strPtr;
                if (old != NULL) {
                    ckfree((char *) old);
                }
                *strPtr = new;
            }
            break;

        case RBC_SWITCH_LIST:
            if (Tcl_SplitList(interp, string, &count, (const char ***) ptr)
                != TCL_OK) {
                return TCL_ERROR;
            }
            break;

        case RBC_SWITCH_CUSTOM:
            if ((*specPtr->customPtr->parseProc)
                (specPtr->customPtr->clientData, interp, (char *)specPtr->switchName,
                    (char *)string, record, specPtr->offset) != TCL_OK) {
                return TCL_ERROR;
            }
            break;

        default:
            Tcl_AppendPrintfToObj(Tcl_GetObjResult(interp),
                "bad switch table: unknown type \"%d\"", specPtr->type);
            return TCL_ERROR;
        }
        specPtr++;
    } while ((specPtr->switchName == NULL) &&
        (specPtr->type != RBC_SWITCH_END));
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * RbcProcessSwitches --
 *
 *      Process command-line options and database options to
 *      fill in fields of a widget record with resources and
 *      other parameters.
 *
 * Results:
 *      Returns the number of arguments comsumed by parsing the
 *      command line.  If an error occurred, -1 will be returned
 *      and an error messages can be found as the interpreter
 *      result.
 *
 * Side effects:
 *      The fields of widgRec get filled in with information
 *      from argc/argv and the option database.  Old information
 *      in widgRec's fields gets recycled.
 *
 *--------------------------------------------------------------
 */
int
RbcProcessSwitches(
    Tcl_Interp * interp,        /* Interpreter for error reporting. */
    RbcSwitchSpec * specs,      /* Describes legal options. */
    int argc,                   /* Number of elements in argv. */
    const char **argv,          /* Command-line options. */
    char *record,               /* Record whose fields are to be
                                 * modified.  Values must be properly
                                 * initialized. */
    int flags)
{                               /* Used to specify additional flags
                                 * that must be present in switch specs
                                 * for them to be considered.  Also,
                                 * may have RBC_SWITCH_ARGV_ONLY set. */
    register int    count;
    const char     *arg;
    register RbcSwitchSpec *specPtr;
    int             needFlags;  /* Specs must contain this set of flags
                                 * or else they are not considered. */
    int             hateFlags;  /* If a spec contains any bits here, it's
                                 * not considered. */

    needFlags = flags & ~(RBC_SWITCH_USER_BIT - 1);
    hateFlags = 0;

    /*
     * Pass 1:  Clear the change flags on all the specs so that we
     *          can check it later.
     */
    for (specPtr = specs; specPtr->type != RBC_SWITCH_END; specPtr++) {
        specPtr->flags &= ~RBC_SWITCH_SPECIFIED;
    }
    /*
     * Pass 2:  Process the arguments that match entries in the specs.
     *          It's an error if the argument doesn't match anything.
     */
    for (count = 0; count < argc; count++) {
        arg = argv[count];
        if (flags & RBC_SWITCH_OBJV_PARTIAL) {
            if ((arg[0] != '-') || ((arg[1] == '-') && (argv[2] == '\0'))) {
                /*
                 * If the argument doesn't start with a '-' (not a switch)
                 * or is '--', stop processing and return the number of
                 * arguments comsumed.
                 */
                return count;
            }
        }
        specPtr = FindSwitchSpec(interp, specs, arg, needFlags, hateFlags);
        if (specPtr == NULL) {
            return -1;
        }
        if (specPtr->type == RBC_SWITCH_FLAG) {
            char           *ptr;

            ptr = record + specPtr->offset;
            *((int *) ptr) |= specPtr->value;
        } else if (specPtr->type == RBC_SWITCH_VALUE) {
            char           *ptr;

            ptr = record + specPtr->offset;
            *((int *) ptr) = specPtr->value;
        } else {
            if ((count + 1) == argc) {
                Tcl_AppendResult(interp, "value for \"", arg, "\" missing",
                    (char *) NULL);
                return -1;
            }
            count++;
            if (DoSwitch(interp, specPtr, argv[count], record) != TCL_OK) {
                char            msg[100];

                sprintf(msg, "\n    (processing \"%.40s\" option)",
                    specPtr->switchName);
                Tcl_AddErrorInfo(interp, msg);
                return -1;
            }
        }
        specPtr->flags |= RBC_SWITCH_SPECIFIED;
    }
    return count;
}

/*
 *--------------------------------------------------------------
 *
 * RbcProcessObjSwitches --
 *
 *      Process command-line options and database options to
 *      fill in fields of a widget record with resources and
 *      other parameters.
 *
 * Results:
 *      Returns the number of arguments comsumed by parsing the
 *      command line.  If an error occurred, -1 will be returned
 *      and an error messages can be found as the interpreter
 *      result.
 *
 * Side effects:
 *      The fields of widgRec get filled in with information
 *      from argc/argv and the option database.  Old information
 *      in widgRec's fields gets recycled.
 *
 *--------------------------------------------------------------
 */
int
RbcProcessObjSwitches(
    Tcl_Interp * interp,        /* Interpreter for error reporting. */
    RbcSwitchSpec * specs,      /* Describes legal options. */
    int objc,                   /* Number of elements in argv. */
    Tcl_Obj * const *objv,      /* Command-line options. */
    char *record,               /* Record whose fields are to be
                                 * modified.  Values must be properly
                                 * initialized. */
    int flags)
{                               /* Used to specify additional flags
                                 * that must be present in switch specs
                                 * for them to be considered.  Also,
                                 * may have RBC_SWITCH_ARGV_ONLY set. */
    register RbcSwitchSpec *specPtr;
    register int    count;
    int             needFlags;  /* Specs must contain this set of flags
                                 * or else they are not considered. */
    int             hateFlags;  /* If a spec contains any bits here, it's
                                 * not considered. */

    needFlags = flags & ~(RBC_SWITCH_USER_BIT - 1);
    hateFlags = 0;

    /*
     * Pass 1:  Clear the change flags on all the specs so that we
     *          can check it later.
     */
    for (specPtr = specs; specPtr->type != RBC_SWITCH_END; specPtr++) {
        specPtr->flags &= ~RBC_SWITCH_SPECIFIED;
    }
    /*
     * Pass 2:  Process the arguments that match entries in the specs.
     *          It's an error if the argument doesn't match anything.
     */
    for (count = 0; count < objc; count++) {
        char           *arg;

        arg = Tcl_GetString(objv[count]);
        if (flags & RBC_SWITCH_OBJV_PARTIAL) {
            if ((arg[0] != '-') || ((arg[1] == '-') && (arg[2] == '\0'))) {
                /*
                 * If the argument doesn't start with a '-' (not a switch)
                 * or is '--', stop processing and return the number of
                 * arguments comsumed.
                 */
                return count;
            }
        }
        specPtr = FindSwitchSpec(interp, specs, arg, needFlags, hateFlags);
        if (specPtr == NULL) {
            return -1;
        }
        if (specPtr->type == RBC_SWITCH_FLAG) {
            char           *ptr;

            ptr = record + specPtr->offset;
            *((int *) ptr) |= specPtr->value;
        } else if (specPtr->type == RBC_SWITCH_VALUE) {
            char           *ptr;

            ptr = record + specPtr->offset;
            *((int *) ptr) = specPtr->value;
        } else {
            count++;
            if (count == objc) {
                Tcl_AppendResult(interp, "value for \"", arg, "\" missing",
                    (char *) NULL);
                return -1;
            }
            arg = Tcl_GetString(objv[count]);
            if (DoSwitch(interp, specPtr, arg, record) != TCL_OK) {
                char            msg[100];

                sprintf(msg, "\n    (processing \"%.40s\" option)",
                    specPtr->switchName);
                Tcl_AddErrorInfo(interp, msg);
                return -1;
            }
        }
        specPtr->flags |= RBC_SWITCH_SPECIFIED;
    }
    return count;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcFreeSwitches --
 *
 *      Free up all resources associated with switch options.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
void
RbcFreeSwitches(
    RbcSwitchSpec * specs,      /* Describes legal options. */
    char *record,               /* Record whose fields contain current
                                 * values for options. */
    int needFlags)
{                               /* Used to specify additional flags
                                 * that must be present in config specs
                                 * for them to be considered. */
    register RbcSwitchSpec *specPtr;

    for (specPtr = specs; specPtr->type != RBC_SWITCH_END; specPtr++) {
        if ((specPtr->flags & needFlags) == needFlags) {
            char           *ptr;

            ptr = record + specPtr->offset;
            switch (specPtr->type) {
            case RBC_SWITCH_STRING:
            case RBC_SWITCH_LIST:
                if (*((char **) ptr) != NULL) {
                    ckfree((char *) *((char **) ptr));
                    *((char **) ptr) = NULL;
                }
                break;

            case RBC_SWITCH_CUSTOM:
                if ((*(char **) ptr != NULL) &&
                    (specPtr->customPtr->freeProc != NULL)) {
                    (*specPtr->customPtr->freeProc) (*(char **) ptr);
                    *((char **) ptr) = NULL;
                }
                break;

            default:
                break;
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RbcSwitchChanged --
 *
 *      Given the configuration specifications and one or more option
 *      patterns (terminated by a NULL), indicate if any of the matching
 *      configuration options has been reset.
 *
 * Results:
 *      Returns 1 if one of the options has changed, 0 otherwise.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int
RbcSwitchChanged(
    RbcSwitchSpec * specs,
    ...)
{
    va_list         argList;
    register RbcSwitchSpec *specPtr;
    register char  *switchName;

    va_start(argList, specs);
    while ((switchName = va_arg(argList, char *)) != NULL) {
        for (specPtr = specs; specPtr->type != RBC_SWITCH_END; specPtr++) {
            if ((Tcl_StringMatch(specPtr->switchName, switchName)) &&
                (specPtr->flags & RBC_SWITCH_SPECIFIED)) {
                va_end(argList);
                return 1;
            }
        }
    }
    va_end(argList);
    return 0;
}

/* vim: set ts=4 sw=4 sts=4 ff=unix et : */
