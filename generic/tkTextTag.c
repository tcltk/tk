/* 
 * tkTextTag.c --
 *
 *	This module implements the "tag" subcommand of the widget command
 *	for text widgets, plus most of the other high-level functions
 *	related to tags.
 *
 * Copyright (c) 1992-1994 The Regents of the University of California.
 * Copyright (c) 1994-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkTextTag.c,v 1.12 2003/11/07 15:36:26 vincentdarley Exp $
 */

#include "default.h"
#include "tkPort.h"
#include "tkInt.h"
#include "tkText.h"

/*
 * The 'TkWrapMode' enum in tkText.h is used to define a type for the
 * -wrap option of tags in a Text widget.  These values are used as
 * indices into the string table below.  Tags are allowed an empty wrap
 * value, but the widget as a whole is not.
 */

static char *wrapStrings[] = {
    "char", "none", "word", "", (char *) NULL
};

static Tk_OptionSpec tagOptionSpecs[] = {
    {TK_OPTION_BORDER, "-background", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, border), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BITMAP, "-bgstipple", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, bgStipple), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_PIXELS, "-borderwidth", (char *) NULL, (char *) NULL,
	"0", Tk_Offset(TkTextTag, borderWidthPtr), Tk_Offset(TkTextTag, borderWidth),
	TK_OPTION_DONT_SET_DEFAULT|TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-elide", (char *) NULL, (char *) NULL,
	"0", -1, Tk_Offset(TkTextTag, elideString),
	TK_OPTION_DONT_SET_DEFAULT|TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BITMAP, "-fgstipple", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, fgStipple), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_FONT, "-font", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, tkfont), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_COLOR, "-foreground", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, fgColor), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-justify", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, justifyString), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-lmargin1", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, lMargin1String), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-lmargin2", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, lMargin2String), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-offset", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, offsetString), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-overstrike", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, overstrikeString),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-relief", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, reliefString), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-rmargin", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, rMarginString), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-spacing1", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, spacing1String), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-spacing2", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, spacing2String), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-spacing3", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, spacing3String), TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-tabs", (char *) NULL, (char *) NULL,
	(char *) NULL, Tk_Offset(TkTextTag, tabStringPtr), -1, TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-underline", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, underlineString),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING_TABLE, "-wrap", (char *) NULL, (char *) NULL,
	(char *) NULL, -1, Tk_Offset(TkTextTag, wrapMode),
	TK_OPTION_NULL_OK, (ClientData) wrapStrings, 0},
    {TK_OPTION_END}
};

/*
 * Forward declarations for procedures defined later in this file:
 */

static void		ChangeTagPriority _ANSI_ARGS_((TkText *textPtr,
			    TkTextTag *tagPtr, int prio));
static TkTextTag *	FindTag _ANSI_ARGS_((Tcl_Interp *interp,
			    TkText *textPtr, Tcl_Obj *tagName));
static void		SortTags _ANSI_ARGS_((int numTags,
			    TkTextTag **tagArrayPtr));
static int		TagSortProc _ANSI_ARGS_((CONST VOID *first,
			    CONST VOID *second));

/*
 *--------------------------------------------------------------
 *
 * TkTextTagCmd --
 *
 *	This procedure is invoked to process the "tag" options of
 *	the widget command for text widgets. See the user documentation
 *	for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

int
TkTextTagCmd(textPtr, interp, objc, objv)
    register TkText *textPtr;	/* Information about text widget. */
    Tcl_Interp *interp;		/* Current interpreter. */
    int objc;			/* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. Someone else has already
				 * parsed this command enough to know that
				 * objv[1] is "tag". */
{
    int optionIndex;
    
    static CONST char *tagOptionStrings[] = {
	"add", "bind", "cget", "configure", "delete", "lower",
	"names", "nextrange", "prevrange", "raise", "ranges", 
	"remove", (char *) NULL 
    };
    enum tagOptions {
	TAG_ADD, TAG_BIND, TAG_CGET, TAG_CONFIGURE, TAG_DELETE,
	TAG_LOWER, TAG_NAMES, TAG_NEXTRANGE, TAG_PREVRANGE,
	TAG_RAISE, TAG_RANGES, TAG_REMOVE
    };

    int i;
    register TkTextTag *tagPtr;
    TkTextIndex first, last, index1, index2;

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "option ?arg arg ...?");
	return TCL_ERROR;
    }
    
    if (Tcl_GetIndexFromObj(interp, objv[2], tagOptionStrings, 
			    "tag option", 0, &optionIndex) != TCL_OK) {
	return TCL_ERROR;
    }

    switch ((enum tagOptions)optionIndex) {
	case TAG_ADD: 
	case TAG_REMOVE: {
	    int addTag;
	    if (((enum tagOptions)optionIndex) == TAG_ADD) {
	        addTag = 1;
	    } else {
		addTag = 0;
	    }
	    if (objc < 5) {
		Tcl_WrongNumArgs(interp, 3, objv, 
				 "tagName index1 ?index2 index1 index2 ...?");
		return TCL_ERROR;
	    }
	    tagPtr = TkTextCreateTag(textPtr, Tcl_GetString(objv[3]));
	    for (i = 4; i < objc; i += 2) {
		if (TkTextGetObjIndex(interp, textPtr, objv[i], 
				      &index1) != TCL_OK) {
		    return TCL_ERROR;
		}
		if (objc > (i+1)) {
		    if (TkTextGetObjIndex(interp, textPtr, objv[i+1], &index2)
			    != TCL_OK) {
			return TCL_ERROR;
		    }
		    if (TkTextIndexCmp(&index1, &index2) >= 0) {
			return TCL_OK;
		    }
		} else {
		    index2 = index1;
		    TkTextIndexForwChars(NULL,&index2, 1, &index2, COUNT_INDICES);
		}

		if (tagPtr->affectsDisplay) {
		    TkTextRedrawTag(textPtr, &index1, &index2, tagPtr, !addTag);
		} else {
		    /*
		     * Still need to trigger enter/leave events on tags that
		     * have changed.
		     */

		    TkTextEventuallyRepick(textPtr);
		}
		if (TkBTreeTag(&index1, &index2, tagPtr, addTag)) {
		    /*
		     * If the tag is "sel", and we actually adjusted
		     * something then grab the selection if we're
		     * supposed to export it and don't already have it.
		     * Also, invalidate partially-completed selection
		     * retrievals.
		     */

		    if (tagPtr == textPtr->selTagPtr) {
			XEvent event;
			/*
			 * Send an event that the selection changed.
			 * This is equivalent to
			 * "event generate $textWidget <<Selection>>"
			 */

			memset((VOID *) &event, 0, sizeof(event));
			event.xany.type = VirtualEvent;
			event.xany.serial = NextRequest(Tk_Display(textPtr->tkwin));
			event.xany.send_event = False;
			event.xany.window = Tk_WindowId(textPtr->tkwin);
			event.xany.display = Tk_Display(textPtr->tkwin);
			((XVirtualEvent *) &event)->name = Tk_GetUid("Selection");
			Tk_HandleEvent(&event);

			if (addTag && textPtr->exportSelection
				&& !(textPtr->flags & GOT_SELECTION)) {
			    Tk_OwnSelection(textPtr->tkwin, XA_PRIMARY,
				    TkTextLostSelection, (ClientData) textPtr);
			    textPtr->flags |= GOT_SELECTION;
			}
			textPtr->abortSelections = 1;
		    }
		}
	    }
	    break;
	}
	case TAG_BIND: {
	    if ((objc < 4) || (objc > 6)) {
		Tcl_WrongNumArgs(interp, 3, objv, "tagName ?sequence? ?command?");
		return TCL_ERROR;
	    }
	    tagPtr = TkTextCreateTag(textPtr, Tcl_GetString(objv[3]));

	    /*
	     * Make a binding table if the widget doesn't already have
	     * one.
	     */

	    if (textPtr->bindingTable == NULL) {
		textPtr->bindingTable = Tk_CreateBindingTable(interp);
	    }

	    if (objc == 6) {
		int append = 0;
		unsigned long mask;
		char *fifth = Tcl_GetString(objv[5]);

		if (fifth[0] == 0) {
		    return Tk_DeleteBinding(interp, textPtr->bindingTable,
			    (ClientData) tagPtr, Tcl_GetString(objv[4]));
		}
		if (fifth[0] == '+') {
		    fifth++;
		    append = 1;
		}
		mask = Tk_CreateBinding(interp, textPtr->bindingTable,
			(ClientData) tagPtr, Tcl_GetString(objv[4]), 
					fifth, append);
		if (mask == 0) {
		    return TCL_ERROR;
		}
		if (mask & (unsigned) ~(ButtonMotionMask|Button1MotionMask
			|Button2MotionMask|Button3MotionMask|Button4MotionMask
			|Button5MotionMask|ButtonPressMask|ButtonReleaseMask
			|EnterWindowMask|LeaveWindowMask|KeyPressMask
			|KeyReleaseMask|PointerMotionMask|VirtualEventMask)) {
		    Tk_DeleteBinding(interp, textPtr->bindingTable,
			    (ClientData) tagPtr, Tcl_GetString(objv[4]));
		    Tcl_ResetResult(interp);
		    Tcl_AppendResult(interp, "requested illegal events; ",
			    "only key, button, motion, enter, leave, and virtual ",
			    "events may be used", (char *) NULL);
		    return TCL_ERROR;
		}
	    } else if (objc == 5) {
		CONST char *command;
	
		command = Tk_GetBinding(interp, textPtr->bindingTable,
			(ClientData) tagPtr, Tcl_GetString(objv[4]));
		if (command == NULL) {
		    CONST char *string = Tcl_GetStringResult(interp); 

		    /*
		     * Ignore missing binding errors.  This is a special hack
		     * that relies on the error message returned by FindSequence
		     * in tkBind.c.
		     */

		    if (string[0] != '\0') {
			return TCL_ERROR;
		    } else {
			Tcl_ResetResult(interp);
		    }
		} else {
		    Tcl_SetResult(interp, (char *) command, TCL_STATIC);
		}
	    } else {
		Tk_GetAllBindings(interp, textPtr->bindingTable,
			(ClientData) tagPtr);
	    }
	    break;
	}
	case TAG_CGET: {
	    if (objc != 5) {
		Tcl_WrongNumArgs(interp, 1, objv, "tag cget tagName option");
		return TCL_ERROR;
	    } else {
		Tcl_Obj *objPtr;
		
		tagPtr = FindTag(interp, textPtr, objv[3]);
		if (tagPtr == NULL) {
		    return TCL_ERROR;
		}
		objPtr = Tk_GetOptionValue(interp, (char *) tagPtr,
					   tagPtr->optionTable, objv[4], textPtr->tkwin);
		if (objPtr == NULL) {
		    return TCL_ERROR;
		} else {
		    Tcl_SetObjResult(interp, objPtr);
		    return TCL_OK;
		}
	    }
	    break;
	}
	case TAG_CONFIGURE: {
	    if (objc < 4) {
		Tcl_WrongNumArgs(interp, 3, objv, "tagName ?option? ?value? ?option value ...?");
		return TCL_ERROR;
	    }
	    tagPtr = TkTextCreateTag(textPtr, Tcl_GetString(objv[3]));
	    if (objc <= 5) {
		Tcl_Obj* objPtr = Tk_GetOptionInfo(interp, (char *) tagPtr,
						   tagPtr->optionTable,
			(objc == 5) ? objv[4] : (Tcl_Obj *) NULL,
					  textPtr->tkwin);
		if (objPtr == NULL) {
		    return TCL_ERROR;
		} else {
		    Tcl_SetObjResult(interp, objPtr);
		    return TCL_OK;
		}
	    } else {
		int result = TCL_OK;

		if (Tk_SetOptions(interp, (char*)tagPtr, tagPtr->optionTable,
			objc-4, objv+4, textPtr->tkwin, NULL, NULL) != TCL_OK) {
		    return TCL_ERROR;
		}
		/*
		 * Some of the configuration options, like -underline
		 * and -justify, require additional translation (this is
		 * needed because we need to distinguish a particular value
		 * of an option from "unspecified").
		 */

		if (tagPtr->borderWidth < 0) {
		    tagPtr->borderWidth = 0;
		}
		if (tagPtr->reliefString != NULL) {
		    if (Tk_GetRelief(interp, tagPtr->reliefString,
			    &tagPtr->relief) != TCL_OK) {
			return TCL_ERROR;
		    }
		}
		if (tagPtr->justifyString != NULL) {
		    if (Tk_GetJustify(interp, tagPtr->justifyString,
			    &tagPtr->justify) != TCL_OK) {
			return TCL_ERROR;
		    }
		}
		if (tagPtr->lMargin1String != NULL) {
		    if (Tk_GetPixels(interp, textPtr->tkwin,
			    tagPtr->lMargin1String, &tagPtr->lMargin1) != TCL_OK) {
			return TCL_ERROR;
		    }
		}
		if (tagPtr->lMargin2String != NULL) {
		    if (Tk_GetPixels(interp, textPtr->tkwin,
			    tagPtr->lMargin2String, &tagPtr->lMargin2) != TCL_OK) {
			return TCL_ERROR;
		    }
		}
		if (tagPtr->offsetString != NULL) {
		    if (Tk_GetPixels(interp, textPtr->tkwin, tagPtr->offsetString,
			    &tagPtr->offset) != TCL_OK) {
			return TCL_ERROR;
		    }
		}
		if (tagPtr->overstrikeString != NULL) {
		    if (Tcl_GetBoolean(interp, tagPtr->overstrikeString,
			    &tagPtr->overstrike) != TCL_OK) {
			return TCL_ERROR;
		    }
		}
		if (tagPtr->rMarginString != NULL) {
		    if (Tk_GetPixels(interp, textPtr->tkwin,
			    tagPtr->rMarginString, &tagPtr->rMargin) != TCL_OK) {
			return TCL_ERROR;
		    }
		}
		if (tagPtr->spacing1String != NULL) {
		    if (Tk_GetPixels(interp, textPtr->tkwin,
			    tagPtr->spacing1String, &tagPtr->spacing1) != TCL_OK) {
			return TCL_ERROR;
		    }
		    if (tagPtr->spacing1 < 0) {
			tagPtr->spacing1 = 0;
		    }
		}
		if (tagPtr->spacing2String != NULL) {
		    if (Tk_GetPixels(interp, textPtr->tkwin,
			    tagPtr->spacing2String, &tagPtr->spacing2) != TCL_OK) {
			return TCL_ERROR;
		    }
		    if (tagPtr->spacing2 < 0) {
			tagPtr->spacing2 = 0;
		    }
		}
		if (tagPtr->spacing3String != NULL) {
		    if (Tk_GetPixels(interp, textPtr->tkwin,
			    tagPtr->spacing3String, &tagPtr->spacing3) != TCL_OK) {
			return TCL_ERROR;
		    }
		    if (tagPtr->spacing3 < 0) {
			tagPtr->spacing3 = 0;
		    }
		}
		if (tagPtr->tabArrayPtr != NULL) {
		    ckfree((char *) tagPtr->tabArrayPtr);
		    tagPtr->tabArrayPtr = NULL;
		}
		if (tagPtr->tabStringPtr != NULL) {
		    tagPtr->tabArrayPtr = TkTextGetTabs(interp, textPtr->tkwin,
			    tagPtr->tabStringPtr);
		    if (tagPtr->tabArrayPtr == NULL) {
			return TCL_ERROR;
		    }
		}
		if (tagPtr->underlineString != NULL) {
		    if (Tcl_GetBoolean(interp, tagPtr->underlineString,
			    &tagPtr->underline) != TCL_OK) {
			return TCL_ERROR;
		    }
		}
		if (tagPtr->elideString != NULL) {
		    if (Tcl_GetBoolean(interp, tagPtr->elideString,
			    &tagPtr->elide) != TCL_OK) {
			return TCL_ERROR;
		    }
		}

		/*
		 * If the "sel" tag was changed, be sure to mirror information
		 * from the tag back into the text widget record.   NOTE: we
		 * don't have to free up information in the widget record
		 * before overwriting it, because it was mirrored in the tag
		 * and hence freed when the tag field was overwritten.
		 */

		if (tagPtr == textPtr->selTagPtr) {
		    textPtr->selBorder = tagPtr->border;
		    textPtr->selBorderWidth = tagPtr->borderWidth;
		    textPtr->selBorderWidthPtr = tagPtr->borderWidthPtr;
		    textPtr->selFgColorPtr = tagPtr->fgColor;
		}
		tagPtr->affectsDisplay = 0;
		tagPtr->affectsDisplayGeometry = 0;
		if ((tagPtr->elideString != NULL)
			|| (tagPtr->tkfont != None)
			|| (tagPtr->justifyString != NULL)
			|| (tagPtr->lMargin1String != NULL)
			|| (tagPtr->lMargin2String != NULL)
			|| (tagPtr->offsetString != NULL)
			|| (tagPtr->rMarginString != NULL)
			|| (tagPtr->spacing1String != NULL)
			|| (tagPtr->spacing2String != NULL)
			|| (tagPtr->spacing3String != NULL)
			|| (tagPtr->tabStringPtr != NULL)
			|| (tagPtr->wrapMode != TEXT_WRAPMODE_NULL)) {
		    tagPtr->affectsDisplay = 1;
		    tagPtr->affectsDisplayGeometry = 1;
		}
		if ((tagPtr->border != NULL)
			|| (tagPtr->reliefString != NULL)
			|| (tagPtr->bgStipple != None)
			|| (tagPtr->fgColor != NULL)
			|| (tagPtr->fgStipple != None)
			|| (tagPtr->overstrikeString != NULL)
			|| (tagPtr->underlineString != NULL)) {
		    tagPtr->affectsDisplay = 1;
		}
		/* 
		 * This line is totally unnecessary if this is a new
		 * tag, since it can't possibly have been applied to
		 * anything yet.  We might wish to test for that
		 * case specially
		 */
		TkTextRedrawTag(textPtr, (TkTextIndex *) NULL,
			(TkTextIndex *) NULL, tagPtr, 1);
		return result;
	    }
	    break;
	}
	case TAG_DELETE: {
	    Tcl_HashEntry *hPtr;

	    if (objc < 4) {
		Tcl_WrongNumArgs(interp, 3, objv, "tagName ?tagName ...?");
		return TCL_ERROR;
	    }
	    for (i = 3; i < objc; i++) {
		hPtr = Tcl_FindHashEntry(&textPtr->tagTable, Tcl_GetString(objv[i]));
		if (hPtr == NULL) {
		    continue;
		}
		tagPtr = (TkTextTag *) Tcl_GetHashValue(hPtr);
		if (tagPtr == textPtr->selTagPtr) {
		    continue;
		}
		if (tagPtr->affectsDisplay) {
		    TkTextRedrawTag(textPtr, (TkTextIndex *) NULL,
			    (TkTextIndex *) NULL, tagPtr, 1);
		}
		TkTextMakeByteIndex(textPtr->tree, 0, 0, &first);
		TkTextMakeByteIndex(textPtr->tree, TkBTreeNumLines(textPtr->tree),
			0, &last),
		TkBTreeTag(&first, &last, tagPtr, 0);

		if (tagPtr == textPtr->selTagPtr) {
		    XEvent event;
		    /*
		     * Send an event that the selection changed.
		     * This is equivalent to
		     * "event generate $textWidget <<Selection>>"
		     */

		    memset((VOID *) &event, 0, sizeof(event));
		    event.xany.type = VirtualEvent;
		    event.xany.serial = NextRequest(Tk_Display(textPtr->tkwin));
		    event.xany.send_event = False;
		    event.xany.window = Tk_WindowId(textPtr->tkwin);
		    event.xany.display = Tk_Display(textPtr->tkwin);
		    ((XVirtualEvent *) &event)->name = Tk_GetUid("Selection");
		    Tk_HandleEvent(&event);
		}

		Tcl_DeleteHashEntry(hPtr);
		if (textPtr->bindingTable != NULL) {
		    Tk_DeleteAllBindings(textPtr->bindingTable,
			    (ClientData) tagPtr);
		}
	    
		/*
		 * Update the tag priorities to reflect the deletion of this tag.
		 */

		ChangeTagPriority(textPtr, tagPtr, textPtr->numTags-1);
		textPtr->numTags -= 1;
		TkTextFreeTag(textPtr, tagPtr);
	    }
	    break;
	}
	case TAG_LOWER: {
	    TkTextTag *tagPtr2;
	    int prio;

	    if ((objc != 4) && (objc != 5)) {
		Tcl_WrongNumArgs(interp, 3, objv, "tagName ?belowThis?");
		return TCL_ERROR;
	    }
	    tagPtr = FindTag(interp, textPtr, objv[3]);
	    if (tagPtr == NULL) {
		return TCL_ERROR;
	    }
	    if (objc == 5) {
		tagPtr2 = FindTag(interp, textPtr, objv[4]);
		if (tagPtr2 == NULL) {
		    return TCL_ERROR;
		}
		if (tagPtr->priority < tagPtr2->priority) {
		    prio = tagPtr2->priority - 1;
		} else {
		    prio = tagPtr2->priority;
		}
	    } else {
		prio = 0;
	    }
	    ChangeTagPriority(textPtr, tagPtr, prio);
	    TkTextRedrawTag(textPtr, (TkTextIndex *) NULL, (TkTextIndex *) NULL,
		    tagPtr, 1);
	    break;
	}
	case TAG_NAMES: {
	    TkTextTag **arrayPtr;
	    int arraySize;
	    Tcl_Obj *listObj;
	    
	    if ((objc != 3) && (objc != 4)) {
		Tcl_WrongNumArgs(interp, 3, objv, "?index?");
		return TCL_ERROR;
	    }
	    if (objc == 3) {
		Tcl_HashSearch search;
		Tcl_HashEntry *hPtr;

		arrayPtr = (TkTextTag **) ckalloc((unsigned)
			(textPtr->numTags * sizeof(TkTextTag *)));
		for (i = 0, hPtr = Tcl_FirstHashEntry(&textPtr->tagTable, &search);
			hPtr != NULL; i++, hPtr = Tcl_NextHashEntry(&search)) {
		    arrayPtr[i] = (TkTextTag *) Tcl_GetHashValue(hPtr);
		}
		arraySize = textPtr->numTags;
	    } else {
		if (TkTextGetObjIndex(interp, textPtr, objv[3], &index1)
			!= TCL_OK) {
		    return TCL_ERROR;
		}
		arrayPtr = TkBTreeGetTags(&index1, &arraySize);
		if (arrayPtr == NULL) {
		    return TCL_OK;
		}
	    }
	    SortTags(arraySize, arrayPtr);
	    listObj = Tcl_NewListObj(0, NULL);
	    for (i = 0; i < arraySize; i++) {
		tagPtr = arrayPtr[i];
		Tcl_ListObjAppendElement(interp, listObj, 
					 Tcl_NewStringObj(tagPtr->name,-1));
	    }
	    Tcl_SetObjResult(interp, listObj);
	    ckfree((char *) arrayPtr);
	    break;
	}
	case TAG_NEXTRANGE: {
	    TkTextSearch tSearch;
	    char position[TK_POS_CHARS];

	    if ((objc != 5) && (objc != 6)) {
		Tcl_WrongNumArgs(interp, 3, objv, "tagName index1 ?index2?");
		return TCL_ERROR;
	    }
	    tagPtr = FindTag((Tcl_Interp *) NULL, textPtr, objv[3]);
	    if (tagPtr == NULL) {
		return TCL_OK;
	    }
	    if (TkTextGetObjIndex(interp, textPtr, objv[4], &index1) != TCL_OK) {
		return TCL_ERROR;
	    }
	    TkTextMakeByteIndex(textPtr->tree, TkBTreeNumLines(textPtr->tree),
		    0, &last);
	    if (objc == 5) {
		index2 = last;
	    } else if (TkTextGetObjIndex(interp, textPtr, objv[5], &index2)
		    != TCL_OK) {
		return TCL_ERROR;
	    }

	    /*
	     * The search below is a bit tricky.  Rather than use the B-tree
	     * facilities to stop the search at index2, let it search up
	     * until the end of the file but check for a position past index2
	     * ourselves.  The reason for doing it this way is that we only
	     * care whether the *start* of the range is before index2;  once
	     * we find the start, we don't want TkBTreeNextTag to abort the
	     * search because the end of the range is after index2.
	     */

	    TkBTreeStartSearch(&index1, &last, tagPtr, &tSearch);
	    if (TkBTreeCharTagged(&index1, tagPtr)) {
		TkTextSegment *segPtr;
		int offset;

		/*
		 * The first character is tagged.  See if there is an
		 * on-toggle just before the character.  If not, then
		 * skip to the end of this tagged range.
		 */

		for (segPtr = index1.linePtr->segPtr, offset = index1.byteIndex; 
			offset >= 0;
			offset -= segPtr->size, segPtr = segPtr->nextPtr) {
		    if ((offset == 0) && (segPtr->typePtr == &tkTextToggleOnType)
			    && (segPtr->body.toggle.tagPtr == tagPtr)) {
			goto gotStart;
		    }
		}
		if (!TkBTreeNextTag(&tSearch)) {
		     return TCL_OK;
		}
	    }

	    /*
	     * Find the start of the tagged range.
	     */

	    if (!TkBTreeNextTag(&tSearch)) {
		return TCL_OK;
	    }
	    gotStart:
	    if (TkTextIndexCmp(&tSearch.curIndex, &index2) >= 0) {
		return TCL_OK;
	    }
	    TkTextPrintIndex(&tSearch.curIndex, position);
	    Tcl_AppendElement(interp, position);
	    TkBTreeNextTag(&tSearch);
	    TkTextPrintIndex(&tSearch.curIndex, position);
	    Tcl_AppendElement(interp, position);
	    break;
	}
	case TAG_PREVRANGE: {
	    TkTextSearch tSearch;
	    char position1[TK_POS_CHARS];
	    char position2[TK_POS_CHARS];

	    if ((objc != 5) && (objc != 6)) {
		Tcl_WrongNumArgs(interp, 3, objv, "tagName index1 ?index2?");
		return TCL_ERROR;
	    }
	    tagPtr = FindTag((Tcl_Interp *) NULL, textPtr, objv[3]);
	    if (tagPtr == NULL) {
		return TCL_OK;
	    }
	    if (TkTextGetObjIndex(interp, textPtr, objv[4], &index1) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (objc == 5) {
		TkTextMakeByteIndex(textPtr->tree, 0, 0, &index2);
	    } else if (TkTextGetObjIndex(interp, textPtr, objv[5], &index2)
		    != TCL_OK) {
		return TCL_ERROR;
	    }

	    /*
	     * The search below is a bit weird.  The previous toggle can be
	     * either an on or off toggle. If it is an on toggle, then we
	     * need to turn around and search forward for the end toggle.
	     * Otherwise we keep searching backwards.
	     */

	    TkBTreeStartSearchBack(&index1, &index2, tagPtr, &tSearch);

	    if (!TkBTreePrevTag(&tSearch)) {
		return TCL_OK;
	    }
	    if (tSearch.segPtr->typePtr == &tkTextToggleOnType) {
		TkTextPrintIndex(&tSearch.curIndex, position1);
		TkTextMakeByteIndex(textPtr->tree, TkBTreeNumLines(textPtr->tree),
			0, &last);
		TkBTreeStartSearch(&tSearch.curIndex, &last, tagPtr, &tSearch);
		TkBTreeNextTag(&tSearch);
		TkTextPrintIndex(&tSearch.curIndex, position2);
	    } else {
		TkTextPrintIndex(&tSearch.curIndex, position2);
		TkBTreePrevTag(&tSearch);
		if (TkTextIndexCmp(&tSearch.curIndex, &index2) < 0) {
		    return TCL_OK;
		}
		TkTextPrintIndex(&tSearch.curIndex, position1);
	    }
	    Tcl_AppendElement(interp, position1);
	    Tcl_AppendElement(interp, position2);
	    break;
	}
	case TAG_RAISE: {
	    TkTextTag *tagPtr2;
	    int prio;

	    if ((objc != 4) && (objc != 5)) {
		Tcl_WrongNumArgs(interp, 3, objv, "tagName ?aboveThis?");
		return TCL_ERROR;
	    }
	    tagPtr = FindTag(interp, textPtr, objv[3]);
	    if (tagPtr == NULL) {
		return TCL_ERROR;
	    }
	    if (objc == 5) {
		tagPtr2 = FindTag(interp, textPtr, objv[4]);
		if (tagPtr2 == NULL) {
		    return TCL_ERROR;
		}
		if (tagPtr->priority <= tagPtr2->priority) {
		    prio = tagPtr2->priority;
		} else {
		    prio = tagPtr2->priority + 1;
		}
	    } else {
		prio = textPtr->numTags-1;
	    }
	    ChangeTagPriority(textPtr, tagPtr, prio);
	    TkTextRedrawTag(textPtr, (TkTextIndex *) NULL, (TkTextIndex *) NULL,
		    tagPtr, 1);
	    break;
	}
	case TAG_RANGES: {
	    TkTextSearch tSearch;
	    Tcl_Obj *listObj = Tcl_NewListObj(0, NULL);

	    if (objc != 4) {
		Tcl_WrongNumArgs(interp, 3, objv, "tagName");
		return TCL_ERROR;
	    }
	    tagPtr = FindTag((Tcl_Interp *) NULL, textPtr, objv[3]);
	    if (tagPtr == NULL) {
		return TCL_OK;
	    }
	    TkTextMakeByteIndex(textPtr->tree, 0, 0, &first);
	    TkTextMakeByteIndex(textPtr->tree, TkBTreeNumLines(textPtr->tree),
		    0, &last);
	    TkBTreeStartSearch(&first, &last, tagPtr, &tSearch);
	    if (TkBTreeCharTagged(&first, tagPtr)) {
		Tcl_ListObjAppendElement(interp, listObj, 
		    TkTextNewIndexObj(textPtr, &first));
	    }
	    while (TkBTreeNextTag(&tSearch)) {
		Tcl_ListObjAppendElement(interp, listObj,
		    TkTextNewIndexObj(textPtr, &tSearch.curIndex));
	    }
	    Tcl_SetObjResult(interp, listObj);
	    break;
	}
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextCreateTag --
 *
 *	Find the record describing a tag within a given text widget,
 *	creating a new record if one doesn't already exist.
 *
 * Results:
 *	The return value is a pointer to the TkTextTag record for tagName.
 *
 * Side effects:
 *	A new tag record is created if there isn't one already defined
 *	for tagName.
 *
 *----------------------------------------------------------------------
 */

TkTextTag *
TkTextCreateTag(textPtr, tagName)
    TkText *textPtr;		/* Widget in which tag is being used. */
    CONST char *tagName;	/* Name of desired tag. */
{
    register TkTextTag *tagPtr;
    Tcl_HashEntry *hPtr;
    int new;

    hPtr = Tcl_CreateHashEntry(&textPtr->tagTable, tagName, &new);
    if (!new) {
	return (TkTextTag *) Tcl_GetHashValue(hPtr);
    }

    /*
     * No existing entry.  Create a new one, initialize it, and add a
     * pointer to it to the hash table entry.
     */

    tagPtr = (TkTextTag *) ckalloc(sizeof(TkTextTag));
    tagPtr->name = Tcl_GetHashKey(&textPtr->tagTable, hPtr);
    tagPtr->toggleCount = 0;
    tagPtr->tagRootPtr = NULL;
    tagPtr->priority = textPtr->numTags;
    tagPtr->border = NULL;
    tagPtr->borderWidth = 0;
    tagPtr->borderWidthPtr = NULL;
    tagPtr->reliefString = NULL;
    tagPtr->relief = TK_RELIEF_FLAT;
    tagPtr->bgStipple = None;
    tagPtr->fgColor = NULL;
    tagPtr->tkfont = NULL;
    tagPtr->fgStipple = None;
    tagPtr->justifyString = NULL;
    tagPtr->justify = TK_JUSTIFY_LEFT;
    tagPtr->lMargin1String = NULL;
    tagPtr->lMargin1 = 0;
    tagPtr->lMargin2String = NULL;
    tagPtr->lMargin2 = 0;
    tagPtr->offsetString = NULL;
    tagPtr->offset = 0;
    tagPtr->overstrikeString = NULL;
    tagPtr->overstrike = 0;
    tagPtr->rMarginString = NULL;
    tagPtr->rMargin = 0;
    tagPtr->spacing1String = NULL;
    tagPtr->spacing1 = 0;
    tagPtr->spacing2String = NULL;
    tagPtr->spacing2 = 0;
    tagPtr->spacing3String = NULL;
    tagPtr->spacing3 = 0;
    tagPtr->tabStringPtr = NULL;
    tagPtr->tabArrayPtr = NULL;
    tagPtr->underlineString = NULL;
    tagPtr->underline = 0;
    tagPtr->elideString = NULL;
    tagPtr->elide = 0;
    tagPtr->wrapMode = TEXT_WRAPMODE_NULL;
    tagPtr->affectsDisplay = 0;
    tagPtr->affectsDisplayGeometry = 0;
    textPtr->numTags++;
    Tcl_SetHashValue(hPtr, tagPtr);
    tagPtr->optionTable = Tk_CreateOptionTable(textPtr->interp, tagOptionSpecs);
    return tagPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * FindTag --
 *
 *	See if tag is defined for a given widget.
 *
 * Results:
 *	If tagName is defined in textPtr, a pointer to its TkTextTag
 *	structure is returned.  Otherwise NULL is returned and an
 *	error message is recorded in the interp's result unless interp
 *	is NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkTextTag *
FindTag(interp, textPtr, tagName)
    Tcl_Interp *interp;		/* Interpreter to use for error message;
				 * if NULL, then don't record an error
				 * message. */
    TkText *textPtr;		/* Widget in which tag is being used. */
    Tcl_Obj *tagName;	        /* Name of desired tag. */
{
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&textPtr->tagTable, Tcl_GetString(tagName));
    if (hPtr != NULL) {
	return (TkTextTag *) Tcl_GetHashValue(hPtr);
    }
    if (interp != NULL) {
	Tcl_AppendResult(interp, "tag \"", Tcl_GetString(tagName),
		"\" isn't defined in text widget", (char *) NULL);
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextFreeTag --
 *
 *	This procedure is called when a tag is deleted to free up the
 *	memory and other resources associated with the tag.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory and other resources are freed.
 *
 *----------------------------------------------------------------------
 */

void
TkTextFreeTag(textPtr, tagPtr)
    TkText *textPtr;			/* Info about overall widget. */
    register TkTextTag *tagPtr;		/* Tag being deleted. */
{
    /* Let Tk do most of the hard work for us */
    Tk_FreeConfigOptions((char *) tagPtr, tagPtr->optionTable,
			 textPtr->tkwin);
    /* This associated information is managed by us */
    if (tagPtr->tabArrayPtr != NULL) {
	ckfree((char *) tagPtr->tabArrayPtr);
    }
    ckfree((char *) tagPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * SortTags --
 *
 *	This procedure sorts an array of tag pointers in increasing
 *	order of priority, optimizing for the common case where the
 *	array is small.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
SortTags(numTags, tagArrayPtr)
    int numTags;		/* Number of tag pointers at *tagArrayPtr. */
    TkTextTag **tagArrayPtr;	/* Pointer to array of pointers. */
{
    int i, j, prio;
    register TkTextTag **tagPtrPtr;
    TkTextTag **maxPtrPtr, *tmp;

    if (numTags < 2) {
	return;
    }
    if (numTags < 20) {
	for (i = numTags-1; i > 0; i--, tagArrayPtr++) {
	    maxPtrPtr = tagPtrPtr = tagArrayPtr;
	    prio = tagPtrPtr[0]->priority;
	    for (j = i, tagPtrPtr++; j > 0; j--, tagPtrPtr++) {
		if (tagPtrPtr[0]->priority < prio) {
		    prio = tagPtrPtr[0]->priority;
		    maxPtrPtr = tagPtrPtr;
		}
	    }
	    tmp = *maxPtrPtr;
	    *maxPtrPtr = *tagArrayPtr;
	    *tagArrayPtr = tmp;
	}
    } else {
	qsort((VOID *) tagArrayPtr, (unsigned) numTags, sizeof (TkTextTag *),
		    TagSortProc);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TagSortProc --
 *
 *	This procedure is called by qsort when sorting an array of
 *	tags in priority order.
 *
 * Results:
 *	The return value is -1 if the first argument should be before
 *	the second element (i.e. it has lower priority), 0 if it's
 *	equivalent (this should never happen!), and 1 if it should be
 *	after the second element.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
TagSortProc(first, second)
    CONST VOID *first, *second;		/* Elements to be compared. */
{
    TkTextTag *tagPtr1, *tagPtr2;

    tagPtr1 = * (TkTextTag **) first;
    tagPtr2 = * (TkTextTag **) second;
    return tagPtr1->priority - tagPtr2->priority;
}

/*
 *----------------------------------------------------------------------
 *
 * ChangeTagPriority --
 *
 *	This procedure changes the priority of a tag by modifying
 *	its priority and the priorities of other tags that are affected
 *	by the change.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Priorities may be changed for some or all of the tags in
 *	textPtr.  The tags will be arranged so that there is exactly
 *	one tag at each priority level between 0 and textPtr->numTags-1,
 *	with tagPtr at priority "prio".
 *
 *----------------------------------------------------------------------
 */

static void
ChangeTagPriority(textPtr, tagPtr, prio)
    TkText *textPtr;			/* Information about text widget. */
    TkTextTag *tagPtr;			/* Tag whose priority is to be
					 * changed. */
    int prio;				/* New priority for tag. */
{
    int low, high, delta;
    register TkTextTag *tagPtr2;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;

    if (prio < 0) {
	prio = 0;
    }
    if (prio >= textPtr->numTags) {
	prio = textPtr->numTags-1;
    }
    if (prio == tagPtr->priority) {
	return;
    } else if (prio < tagPtr->priority) {
	low = prio;
	high = tagPtr->priority-1;
	delta = 1;
    } else {
	low = tagPtr->priority+1;
	high = prio;
	delta = -1;
    }
    for (hPtr = Tcl_FirstHashEntry(&textPtr->tagTable, &search);
	    hPtr != NULL; hPtr = Tcl_NextHashEntry(&search)) {
	tagPtr2 = (TkTextTag *) Tcl_GetHashValue(hPtr);
	if ((tagPtr2->priority >= low) && (tagPtr2->priority <= high)) {
	    tagPtr2->priority += delta;
	}
    }
    tagPtr->priority = prio;
}

/*
 *--------------------------------------------------------------
 *
 * TkTextBindProc --
 *
 *	This procedure is invoked by the Tk dispatcher to handle
 *	events associated with bindings on items.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on the command invoked as part of the binding
 *	(if there was any).
 *
 *--------------------------------------------------------------
 */

void
TkTextBindProc(clientData, eventPtr)
    ClientData clientData;		/* Pointer to canvas structure. */
    XEvent *eventPtr;			/* Pointer to X event that just
					 * happened. */
{
    TkText *textPtr = (TkText *) clientData;
    int repick  = 0;

# define AnyButtonMask (Button1Mask|Button2Mask|Button3Mask\
	|Button4Mask|Button5Mask)

    textPtr->refCount++;

    /*
     * This code simulates grabs for mouse buttons by keeping track
     * of whether a button is pressed and refusing to pick a new current
     * character while a button is pressed.
     */

    if (eventPtr->type == ButtonPress) {
	textPtr->flags |= BUTTON_DOWN;
    } else if (eventPtr->type == ButtonRelease) {
	int mask;

	switch (eventPtr->xbutton.button) {
	    case Button1:
		mask = Button1Mask;
		break;
	    case Button2:
		mask = Button2Mask;
		break;
	    case Button3:
		mask = Button3Mask;
		break;
	    case Button4:
		mask = Button4Mask;
		break;
	    case Button5:
		mask = Button5Mask;
		break;
	    default:
		mask = 0;
		break;
	}
	if ((eventPtr->xbutton.state & AnyButtonMask) == (unsigned) mask) {
	    textPtr->flags &= ~BUTTON_DOWN;
	    repick = 1;
	}
    } else if ((eventPtr->type == EnterNotify)
	    || (eventPtr->type == LeaveNotify)) {
	if (eventPtr->xcrossing.state & AnyButtonMask)  {
	    textPtr->flags |= BUTTON_DOWN;
	} else {
	    textPtr->flags &= ~BUTTON_DOWN;
	}
	TkTextPickCurrent(textPtr, eventPtr);
	goto done;
    } else if (eventPtr->type == MotionNotify) {
	if (eventPtr->xmotion.state & AnyButtonMask)  {
	    textPtr->flags |= BUTTON_DOWN;
	} else {
	    textPtr->flags &= ~BUTTON_DOWN;
	}
	TkTextPickCurrent(textPtr, eventPtr);
    }
    if ((textPtr->numCurTags > 0) && (textPtr->bindingTable != NULL)
	    && (textPtr->tkwin != NULL) && !(textPtr->flags & DESTROYED)) {
	Tk_BindEvent(textPtr->bindingTable, eventPtr, textPtr->tkwin,
		textPtr->numCurTags, (ClientData *) textPtr->curTagArrayPtr);
    }
    if (repick) {
	unsigned int oldState;

	oldState = eventPtr->xbutton.state;
	eventPtr->xbutton.state &= ~(Button1Mask|Button2Mask
		|Button3Mask|Button4Mask|Button5Mask);
	if (!(textPtr->flags & DESTROYED)) {
	    TkTextPickCurrent(textPtr, eventPtr);
	}
	eventPtr->xbutton.state = oldState;
    }

    done:
    if (--textPtr->refCount == 0) {
	ckfree((char *) textPtr);
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkTextPickCurrent --
 *
 *	Find the character containing the coordinates in an event
 *	and place the "current" mark on that character.  If the
 *	"current" mark has moved then generate a fake leave event
 *	on the old current character and a fake enter event on the new
 *	current character.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The current mark for textPtr may change.  If it does,
 *	then the commands associated with character entry and leave
 *	could do just about anything.  For example, the text widget
 *	might be deleted.  It is up to the caller to protect itself
 *	by incrementing the refCount of the text widget.
 *
 *--------------------------------------------------------------
 */

void
TkTextPickCurrent(textPtr, eventPtr)
    register TkText *textPtr;		/* Text widget in which to select
					 * current character. */
    XEvent *eventPtr;			/* Event describing location of
					 * mouse cursor.  Must be EnterWindow,
					 * LeaveWindow, ButtonRelease, or
					 * MotionNotify. */
{
    TkTextIndex index;
    TkTextTag **oldArrayPtr, **newArrayPtr;
    TkTextTag **copyArrayPtr = NULL;	/* Initialization needed to prevent
					 * compiler warning. */

    int numOldTags, numNewTags, i, j, size;
    XEvent event;

    /*
     * If a button is down, then don't do anything at all;  we'll be
     * called again when all buttons are up, and we can repick then.
     * This implements a form of mouse grabbing.
     */

    if (textPtr->flags & BUTTON_DOWN) {
	if (((eventPtr->type == EnterNotify) || (eventPtr->type == LeaveNotify))
		&& ((eventPtr->xcrossing.mode == NotifyGrab)
		|| (eventPtr->xcrossing.mode == NotifyUngrab))) {
	    /*
	     * Special case:  the window is being entered or left because
	     * of a grab or ungrab.  In this case, repick after all.
	     * Furthermore, clear BUTTON_DOWN to release the simulated
	     * grab.
	     */

	    textPtr->flags &= ~BUTTON_DOWN;
	} else {
	    return;
	}
    }

    /*
     * Save information about this event in the widget in case we have
     * to synthesize more enter and leave events later (e.g. because a
     * character was deleted, causing a new character to be underneath
     * the mouse cursor).  Also translate MotionNotify events into
     * EnterNotify events, since that's what gets reported to event
     * handlers when the current character changes.
     */

    if (eventPtr != &textPtr->pickEvent) {
	if ((eventPtr->type == MotionNotify)
		|| (eventPtr->type == ButtonRelease)) {
	    textPtr->pickEvent.xcrossing.type = EnterNotify;
	    textPtr->pickEvent.xcrossing.serial = eventPtr->xmotion.serial;
	    textPtr->pickEvent.xcrossing.send_event
		    = eventPtr->xmotion.send_event;
	    textPtr->pickEvent.xcrossing.display = eventPtr->xmotion.display;
	    textPtr->pickEvent.xcrossing.window = eventPtr->xmotion.window;
	    textPtr->pickEvent.xcrossing.root = eventPtr->xmotion.root;
	    textPtr->pickEvent.xcrossing.subwindow = None;
	    textPtr->pickEvent.xcrossing.time = eventPtr->xmotion.time;
	    textPtr->pickEvent.xcrossing.x = eventPtr->xmotion.x;
	    textPtr->pickEvent.xcrossing.y = eventPtr->xmotion.y;
	    textPtr->pickEvent.xcrossing.x_root = eventPtr->xmotion.x_root;
	    textPtr->pickEvent.xcrossing.y_root = eventPtr->xmotion.y_root;
	    textPtr->pickEvent.xcrossing.mode = NotifyNormal;
	    textPtr->pickEvent.xcrossing.detail = NotifyNonlinear;
	    textPtr->pickEvent.xcrossing.same_screen
		    = eventPtr->xmotion.same_screen;
	    textPtr->pickEvent.xcrossing.focus = False;
	    textPtr->pickEvent.xcrossing.state = eventPtr->xmotion.state;
	} else  {
	    textPtr->pickEvent = *eventPtr;
	}
    }

    /*
     * Find the new current character, then find and sort all of the
     * tags associated with it.
     */

    if (textPtr->pickEvent.type != LeaveNotify) {
	TkTextPixelIndex(textPtr, textPtr->pickEvent.xcrossing.x,
		textPtr->pickEvent.xcrossing.y, &index);
	newArrayPtr = TkBTreeGetTags(&index, &numNewTags);
	SortTags(numNewTags, newArrayPtr);
    } else {
	newArrayPtr = NULL;
	numNewTags = 0;
    }

    /*
     * Resort the tags associated with the previous marked character
     * (the priorities might have changed), then make a copy of the
     * new tags, and compare the old tags to the copy, nullifying
     * any tags that are present in both groups (i.e. the tags that
     * haven't changed).
     */

    SortTags(textPtr->numCurTags, textPtr->curTagArrayPtr);
    if (numNewTags > 0) {
	size = numNewTags * sizeof(TkTextTag *);
	copyArrayPtr = (TkTextTag **) ckalloc((unsigned) size);
	memcpy((VOID *) copyArrayPtr, (VOID *) newArrayPtr, (size_t) size);
	for (i = 0; i < textPtr->numCurTags; i++) {
	    for (j = 0; j < numNewTags; j++) {
		if (textPtr->curTagArrayPtr[i] == copyArrayPtr[j]) {
		    textPtr->curTagArrayPtr[i] = NULL;
		    copyArrayPtr[j] = NULL;
		    break;
		}
	    }
	}
    }

    /*
     * Invoke the binding system with a LeaveNotify event for all of
     * the tags that have gone away.  We have to be careful here,
     * because it's possible that the binding could do something
     * (like calling tkwait) that eventually modifies
     * textPtr->curTagArrayPtr.  To avoid problems in situations like
     * this, update curTagArrayPtr to its new value before invoking
     * any bindings, and don't use it any more here.
     */

    numOldTags = textPtr->numCurTags;
    textPtr->numCurTags = numNewTags;
    oldArrayPtr = textPtr->curTagArrayPtr;
    textPtr->curTagArrayPtr = newArrayPtr;
    if (numOldTags != 0) {
	if ((textPtr->bindingTable != NULL) && (textPtr->tkwin != NULL)
	  && !(textPtr->flags & DESTROYED)) {
	    event = textPtr->pickEvent;
	    event.type = LeaveNotify;

	    /*
	     * Always use a detail of NotifyAncestor.  Besides being
	     * consistent, this avoids problems where the binding code
	     * will discard NotifyInferior events.
	     */

	    event.xcrossing.detail = NotifyAncestor;
	    Tk_BindEvent(textPtr->bindingTable, &event, textPtr->tkwin,
		    numOldTags, (ClientData *) oldArrayPtr);
	}
	ckfree((char *) oldArrayPtr);
    }

    /*
     * Reset the "current" mark (be careful to recompute its location,
     * since it might have changed during an event binding).  Then
     * invoke the binding system with an EnterNotify event for all of
     * the tags that have just appeared.
     */

    TkTextPixelIndex(textPtr, textPtr->pickEvent.xcrossing.x,
	    textPtr->pickEvent.xcrossing.y, &index);
    TkTextSetMark(textPtr, "current", &index);
    if (numNewTags != 0) {
	if ((textPtr->bindingTable != NULL) && (textPtr->tkwin != NULL)
	  && !(textPtr->flags & DESTROYED)) {
	    event = textPtr->pickEvent;
	    event.type = EnterNotify;
	    event.xcrossing.detail = NotifyAncestor;
	    Tk_BindEvent(textPtr->bindingTable, &event, textPtr->tkwin,
		    numNewTags, (ClientData *) copyArrayPtr);
	}
	ckfree((char *) copyArrayPtr);
    }
}
