/*
 * Copyright 2004, Joe English.
 *
 * Usage:
 *	TtkBlinkCursor(corePtr), usually called in a widget's Init hook,
 *	arranges to periodically toggle the corePtr->flags CURSOR_ON bit
 *	on and off (and schedule a redisplay) whenever the widget has focus.
 *
 *	Note: Widgets may have additional logic to decide whether
 *	to display the cursor or not (e.g., readonly or disabled states);
 *	TtkBlinkCursor() does not account for this.
 *
 */

#include "tkInt.h"
#include "ttkThemeInt.h"
#include "ttkWidget.h"

#define DEF_CURSOR_ON_TIME	600		/* milliseconds */
#define DEF_CURSOR_OFF_TIME	300		/* milliseconds */

/* Interp-specific data for tracking cursors:
 */
typedef struct
{
    WidgetCore		*owner;	/* Widget that currently has cursor */
    Tcl_TimerToken	timer;		/* Blink timer */
    int		onTime;		/* #milliseconds to blink cursor on */
    int		offTime;	/* #milliseconds to blink cursor off */
} CursorManager;

/* CursorManagerDeleteProc --
 *	InterpDeleteProc for cursor manager.
 */
static void CursorManagerDeleteProc(
    void *clientData,
    TCL_UNUSED(Tcl_Interp *))
{
    CursorManager *cm = (CursorManager*)clientData;

    if (cm->timer) {
	Tcl_DeleteTimerHandler(cm->timer);
    }
    ckfree(clientData);
}

/* GetCursorManager --
 *	Look up and create if necessary the interp's cursor manager.
 */
static CursorManager *GetCursorManager(Tcl_Interp *interp)
{
    static const char *cm_key = "ttk::CursorManager";
    CursorManager *cm = (CursorManager *)Tcl_GetAssocData(interp, cm_key,0);
    Tk_Window window;
    Tk_Uid value;
    int intValue;

    if (!cm) {
	cm = (CursorManager *)ckalloc(sizeof(*cm));
	cm->timer = 0;
	cm->owner = 0;
	cm->onTime = DEF_CURSOR_ON_TIME;
	cm->offTime = DEF_CURSOR_OFF_TIME;

	/* Override on and off default times with values obtained from
	 * the option database (if such values are specified).
	 */

	window = Tk_MainWindow(interp);
	if (window) {
	    value = Tk_GetOption(window, "insertOnTime", "OnTime");
	    if (value) {
		if (Tcl_GetInt(interp, value, &intValue) == TCL_OK) {
		    cm->onTime = intValue;
		}
	    }
	    value = Tk_GetOption(window, "insertOffTime", "OffTime");
	    if (value) {
		if (Tcl_GetInt(interp, value, &intValue) == TCL_OK) {
		    cm->offTime = intValue;
		}
	    }
	}

	Tcl_SetAssocData(interp, cm_key, CursorManagerDeleteProc, cm);
    }
    return cm;
}

/* CursorBlinkProc --
 *	Timer handler to blink the insert cursor on and off.
 */
static void
CursorBlinkProc(void *clientData)
{
    CursorManager *cm = (CursorManager *)clientData;
    int blinkTime;

    if (cm->owner->flags & CURSOR_ON) {
	cm->owner->flags &= ~CURSOR_ON;
	blinkTime = cm->offTime;
    } else {
	cm->owner->flags |= CURSOR_ON;
	blinkTime = cm->onTime;
    }
    cm->timer = Tcl_CreateTimerHandler(blinkTime, CursorBlinkProc, clientData);
    TtkRedisplayWidget(cm->owner);
}

/* LoseCursor --
 *	Turn cursor off, disable blink timer.
 */
static void LoseCursor(CursorManager *cm, WidgetCore *corePtr)
{
    if (corePtr->flags & CURSOR_ON) {
	corePtr->flags &= ~CURSOR_ON;
	TtkRedisplayWidget(corePtr);
    }
    if (cm->owner == corePtr) {
	cm->owner = NULL;
    }
    if (cm->timer) {
	Tcl_DeleteTimerHandler(cm->timer);
	cm->timer = 0;
    }
}

/* ClaimCursor --
 *	Claim ownership of the insert cursor and blink on.
 */
static void ClaimCursor(CursorManager *cm, WidgetCore *corePtr)
{
    if (cm->owner == corePtr)
	return;
    if (cm->owner)
	LoseCursor(cm, cm->owner);

    corePtr->flags |= CURSOR_ON;
    TtkRedisplayWidget(corePtr);

    cm->owner = corePtr;
    cm->timer = Tcl_CreateTimerHandler(cm->onTime, CursorBlinkProc, cm);
}

/*
 * CursorEventProc --
 *	Event handler for FocusIn and FocusOut events;
 *	claim/lose ownership of the insert cursor when the widget
 *	acquires/loses keyboard focus.
 */

#define CursorEventMask (FocusChangeMask|StructureNotifyMask)
#define RealFocusEvent(d) \
    (d == NotifyInferior || d == NotifyAncestor || d == NotifyNonlinear)

static void
CursorEventProc(void *clientData, XEvent *eventPtr)
{
    WidgetCore *corePtr = (WidgetCore *)clientData;
    CursorManager *cm = GetCursorManager(corePtr->interp);

    switch (eventPtr->type) {
	case DestroyNotify:
	    if (cm->owner == corePtr)
		LoseCursor(cm, corePtr);
	    Tk_DeleteEventHandler(
		corePtr->tkwin, CursorEventMask, CursorEventProc, clientData);
	    break;
	case FocusIn:
	    if (RealFocusEvent(eventPtr->xfocus.detail))
		ClaimCursor(cm, corePtr);
	    break;
	case FocusOut:
	    if (RealFocusEvent(eventPtr->xfocus.detail))
		LoseCursor(cm, corePtr);
	    break;
    }
}

void TtkSetBlinkCursorOnTime(Tcl_Interp* interp, int onTime)
{
    CursorManager* cm = GetCursorManager(interp);

    if (onTime >= 0)
	cm->onTime = onTime;
}

void TtkSetBlinkCursorOffTime(Tcl_Interp* interp, int offTime)
{
    CursorManager* cm = GetCursorManager(interp);

    if (offTime >= 0)
	cm->offTime = offTime;
}

/*
 * TtkSetBlinkCursorTimes --
 *	Set cursor blink on and off times from the "." style defaults
 *	-insertontime and -insertofftime - For instance to set cursor
 *	blinking off:
 *	    ttk::style configure . -insertofftime 0
 */
void TtkSetBlinkCursorTimes(Tcl_Interp* interp)
{
    Ttk_Theme theme;
    Ttk_Style style = NULL;
    Tcl_Obj* result;
    int timeInterval;

    theme = Ttk_GetCurrentTheme(interp);
    style = Ttk_GetStyle(theme, ".");
    result = Ttk_StyleDefault(style, "-insertontime");
    if (result) {
	Tcl_GetIntFromObj(interp, result, &timeInterval);
	TtkSetBlinkCursorOnTime(interp, timeInterval);
    }
    result = Ttk_StyleDefault(style, "-insertofftime");
    if (result) {
	Tcl_GetIntFromObj(interp, result, &timeInterval);
	TtkSetBlinkCursorOffTime(interp, timeInterval);
    }
}
/*
 * TtkBlinkCursor (main routine) --
 *	Arrange to blink the cursor on and off whenever the
 *	widget has focus.
 */
void TtkBlinkCursor(WidgetCore *corePtr)
{
    Tk_CreateEventHandler(
	corePtr->tkwin, CursorEventMask, CursorEventProc, corePtr);
}

/*EOF*/
