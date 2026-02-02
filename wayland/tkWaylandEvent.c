/*
 * tkWaylandEvent.c --
 *
 *	This file implements an event source for the Wayland backend 
 *	of Tk.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkUnixInt.h"
#include <GLFW/glfw3.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

/*
 * Maximum number of pending events in our queue
 */
#define MAX_PENDING_EVENTS 1024

/*
 * Event queue structure for buffering GLFW events
 */
typedef struct TkEventQueue {
    XEvent events[MAX_PENDING_EVENTS];
    int head;
    int tail;
    int count;
    Tcl_Mutex mutex;
} TkEventQueue;

/*
 * Thread-local initialization data
 */
typedef struct {
    int initialized;
    int eventSourceCreated;
    int glfwInitialized;
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

/*
 * GLFW-specific per-display data
 */
typedef struct GLFWDisplayData {
    GLFWwindow *dummyWindow;       /* Dummy invisible window for event loop */
    int isWayland;                 /* 1 = native Wayland, 0 = X11 fallback */
    TkEventQueue eventQueue;       /* Event queue for this display */
   
    
    /* Clipboard cache */
    char *clipboardText;
    size_t clipboardLen;
    
    /* Error state */
    int lastError;
    char errorMsg[256];
    
    /* Timing */
    double lastPollTime;
    int pollCount;
} GLFWDisplayData;

/*
 * Forward declarations
 */
static void DisplayCheckProc(void *clientData, int flags);
static void DisplayExitHandler(void *clientData);
static void DisplayFileProc(void *clientData, int flags);
static void DisplaySetupProc(void *clientData, int flags);
static void ErrorCallback(int error, const char *description);
static int QueueEvent(TkEventQueue *queue, XEvent *event);
static int DequeueEvent(TkEventQueue *queue, XEvent *event);
static void InitEventQueue(TkEventQueue *queue);
static void CleanupEventQueue(TkEventQueue *queue);
static void ProcessPendingEvents(TkDisplay *dispPtr);

/* Global error tracking */
static int lastGLFWError = 0;
static char lastGLFWErrorMsg[512] = {0};


/* Error handling. */
static void
ErrorCallback(int error, const char *description)
{
    lastGLFWError = error;
    snprintf(lastGLFWErrorMsg, sizeof(lastGLFWErrorMsg), 
             "GLFW error %d: %s", error, description);
    fprintf(stderr, "%s\n", lastGLFWErrorMsg);
}

/* Event queue management. */

static void
InitEventQueue(TkEventQueue *queue)
{
    if (!queue) return;
    
    memset(queue, 0, sizeof(TkEventQueue));
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->mutex = NULL;
}

static void
CleanupEventQueue(TkEventQueue *queue)
{
    if (!queue) return;
    
    if (queue->mutex) {
        Tcl_MutexLock(&queue->mutex);
        queue->head = 0;
        queue->tail = 0;
        queue->count = 0;
        Tcl_MutexUnlock(&queue->mutex);
        Tcl_MutexFinalize(&queue->mutex);
    }
}

static int
QueueEvent(TkEventQueue *queue, XEvent *event)
{
    if (!queue || !event) return 0;
    
    Tcl_MutexLock(&queue->mutex);
    
    if (queue->count >= MAX_PENDING_EVENTS) {
        Tcl_MutexUnlock(&queue->mutex);
        fprintf(stderr, "Warning: Event queue full, dropping event\n");
        return 0;
    }
    
    memcpy(&queue->events[queue->tail], event, sizeof(XEvent));
    queue->tail = (queue->tail + 1) % MAX_PENDING_EVENTS;
    queue->count++;
    
    Tcl_MutexUnlock(&queue->mutex);
    return 1;
}

static int
DequeueEvent(TkEventQueue *queue, XEvent *event)
{
    if (!queue || !event) return 0;
    
    Tcl_MutexLock(&queue->mutex);
    
    if (queue->count == 0) {
        Tcl_MutexUnlock(&queue->mutex);
        return 0;
    }
    
    memcpy(event, &queue->events[queue->head], sizeof(XEvent));
    queue->head = (queue->head + 1) % MAX_PENDING_EVENTS;
    queue->count--;
    
    Tcl_MutexUnlock(&queue->mutex);
    return 1;
}


/* Initialization / creation. */

void
TkCreateXEventSource(void)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));

    if (tsdPtr->initialized) {
        return;  /* Already initialized */
    }

    /* Initialize GLFW if not already done */
    if (!tsdPtr->glfwInitialized) {
        glfwSetErrorCallback(ErrorCallback);
        
        if (!glfwInit()) {
            fprintf(stderr, "GLFW initialization failed: %s\n", lastGLFWErrorMsg);
            return;
        }
        
        tsdPtr->glfwInitialized = 1;

    /* Create Tcl event source. */
    if (!tsdPtr->eventSourceCreated) {
        Tcl_CreateEventSource(DisplaySetupProc, DisplayCheckProc, NULL);
        TkCreateExitHandler(DisplayExitHandler, NULL);
        tsdPtr->eventSourceCreated = 1;
    }
    
    tsdPtr->initialized = 1;
}

static void
DisplayExitHandler(void *dummy)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
            Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    (void)dummy;

    if (!tsdPtr->initialized) {
        return;
    }

    if (tsdPtr->eventSourceCreated) {
        Tcl_DeleteEventSource(DisplaySetupProc, DisplayCheckProc, NULL);
        tsdPtr->eventSourceCreated = 0;
    }
    
    if (tsdPtr->glfwInitialized) {
        glfwTerminate();
        tsdPtr->glfwInitialized = 0;
    }
    
    tsdPtr->initialized = 0;
}

/*  Open / close display. */

TkDisplay *
TkpOpenDisplay(const char *displayNameStr)
{
    TkDisplay *dispPtr = NULL;
    GLFWDisplayData *glfwData = NULL;
    
    /* GLFW ignores display names - uses default display. */
    (void)displayNameStr;

    /* Ensure event source is created. */
    TkCreateXEventSource();
    
    /* Allocate display structure. */
    dispPtr = (TkDisplay *)Tcl_Alloc(sizeof(TkDisplay));
    if (!dispPtr) {
        fprintf(stderr, "Failed to allocate TkDisplay\n");
        return NULL;
    }
    memset(dispPtr, 0, sizeof(TkDisplay));

    /* Allocate GLFW-specific data. */
    glfwData = (GLFWDisplayData *)Tcl_Alloc(sizeof(GLFWDisplayData));
    if (!glfwData) {
        fprintf(stderr, "Failed to allocate GLFWDisplayData\n");
        Tcl_Free((char *)dispPtr);
        return NULL;
    }
    memset(glfwData, 0, sizeof(GLFWDisplayData));
    dispPtr->glfw = glfwData;

    /* Initialize event queue. */
    InitEventQueue(&glfwData->eventQueue);

    /* Create invisible dummy window for event processing. */
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);

    glfwData->dummyWindow = glfwCreateWindow(1, 1, "Tk Wayland Dummy", NULL, NULL);
    if (!glfwData->dummyWindow) {
        fprintf(stderr, "Failed to create GLFW dummy window: %s\n", 
                lastGLFWErrorMsg);
        CleanupEventQueue(&glfwData->eventQueue);
        Tcl_Free((char *)glfwData);
        Tcl_Free((char *)dispPtr);
        return NULL;
    }

    /* Detect platform */
    glfwData->isWayland = (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND);
    

    /* 
     * Hook into Tcl event loop.
     * Note: GLFW doesn't expose file descriptors on all platforms. 
     * We use timer-based polling instead.
     */
    
    Tcl_CreateTimerHandler(10, (Tcl_TimerProc *)DisplayCheckProc, dispPtr);

    /* Initialize keymap. */
    TkpInitKeymapInfo(dispPtr);
    
    /* Initialize clipboard. */
    glfwData->clipboardText = NULL;
    glfwData->clipboardLen = 0;
    
    /* Initialize timing. */
    glfwData->lastPollTime = glfwGetTime();
    glfwData->pollCount = 0;

    return dispPtr;
}

void
TkpCloseDisplay(TkDisplay *dispPtr)
{
    GLFWDisplayData *glfwData;
    
    if (!dispPtr) {
        return;
    }

    glfwData = (GLFWDisplayData *)dispPtr->glfw;

    /* Clean up Tk internal. */
    TkSendCleanup(dispPtr);
    TkWmCleanup(dispPtr);
    TkClipCleanup(dispPtr);
    
    /* Clean up keymap info. */
    TkpCleanupKeymapInfo(dispPtr);

    if (glfwData) {
        /* Destroy GLFW window. */
        if (glfwData->dummyWindow) {
            glfwDestroyWindow(glfwData->dummyWindow);
            glfwData->dummyWindow = NULL;
        }
        
        /* Clean up event queue. */
        CleanupEventQueue(&glfwData->eventQueue);
        

        /* Free clipboard cache. */
        if (glfwData->clipboardText) {
            Tcl_Free(glfwData->clipboardText);
            glfwData->clipboardText = NULL;
        }
        
        Tcl_Free((char *)glfwData);
        dispPtr->glfw = NULL;
    }

    Tcl_Free((char *)dispPtr);
}

/* Event loop integration.  */

static void
DisplaySetupProc(void *dummy, int flags)
{
    Tcl_Time blockTime = {0, 10000};  /* 10ms max block */
    
    (void)dummy;
    
    if (!(flags & TCL_WINDOW_EVENTS)) {
        return;
    }

    /* Set maximum block time for event loo. */
    Tcl_SetMaxBlockTime(&blockTime);
}

static void
DisplayCheckProc(void *clientData, int flags)
{
    TkDisplay *dispPtr = (TkDisplay *)clientData;
    GLFWDisplayData *glfwData;
    
    (void)flags;
    
    if (!(flags & TCL_WINDOW_EVENTS)) {
        return;
    }
    
    if (!dispPtr) {
        return;
    }
    
    glfwData = (GLFWDisplayData *)dispPtr->glfw;
    if (!glfwData || !glfwData->dummyWindow) {
        return;
    }

    /* Poll GLFW events non-blocking. */
    glfwPollEvents();
    
    /* Update timing statistics. */
    double currentTime = glfwGetTime();
    double elapsed = currentTime - glfwData->lastPollTime;
    glfwData->lastPollTime = currentTime;
    glfwData->pollCount++;
    
    /* Check for window close events. */
    if (glfwWindowShouldClose(glfwData->dummyWindow)) {
        XEvent event;
        memset(&event, 0, sizeof(XEvent));
        event.type = ClientMessage;
        event.xclient.message_type = XInternAtom(Tk_Display(Tk_MainWindow(dispPtr)), 
                                                "WM_DELETE_WINDOW", False);
        event.xclient.format = 32;
        QueueEvent(&glfwData->eventQueue, &event);
    }
    
    /* Check for window focus changes. */
    int focused = glfwGetWindowAttrib(glfwData->dummyWindow, GLFW_FOCUSED);
    static int lastFocused = 0;
    
    if (focused != lastFocused) {
        XEvent event;
        memset(&event, 0, sizeof(XEvent));
        event.type = (focused) ? FocusIn : FocusOut;
        event.xfocus.mode = NotifyNormal;
        event.xfocus.detail = NotifyDetailNone;
        QueueEvent(&glfwData->eventQueue, &event);
        lastFocused = focused;
    }
    
    /* Check for window size changes. */
    int width, height;
    glfwGetWindowSize(glfwData->dummyWindow, &width, &height);
    static int lastWidth = 0, lastHeight = 0;
    
    if (width != lastWidth || height != lastHeight) {
        XEvent event;
        memset(&event, 0, sizeof(XEvent));
        event.type = ConfigureNotify;
        event.xconfigure.width = width;
        event.xconfigure.height = height;
        event.xconfigure.x = 0;
        event.xconfigure.y = 0;
        QueueEvent(&glfwData->eventQueue, &event);
        
        lastWidth = width;
        lastHeight = height;
    }
    
    /* Check for mouse button events. */
    for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; button++) {
        static int lastButtonStates[GLFW_MOUSE_BUTTON_LAST + 1] = {0};
        int state = glfwGetMouseButton(glfwData->dummyWindow, button);
        
        if (state != lastButtonStates[button]) {
            XEvent event;
            memset(&event, 0, sizeof(XEvent));
            event.type = (state == GLFW_PRESS) ? ButtonPress : ButtonRelease;
            event.xbutton.button = button + 1; /* GLFW buttons are 0-based, X11 are 1-based. */
            event.xbutton.state = 0;
            
            double x, y;
            glfwGetCursorPos(glfwData->dummyWindow, &x, &y);
            event.xbutton.x = (int)x;
            event.xbutton.y = (int)y;
            
            QueueEvent(&glfwData->eventQueue, &event);
            lastButtonStates[button] = state;
        }
    }
    
    /* Check for mouse motion. */
    static double lastX = 0, lastY = 0;
    double x, y;
    glfwGetCursorPos(glfwData->dummyWindow, &x, &y);
    
    if (x != lastX || y != lastY) {
        XEvent event;
        memset(&event, 0, sizeof(XEvent));
        event.type = MotionNotify;
        event.xmotion.x = (int)x;
        event.xmotion.y = (int)y;
        event.xmotion.state = 0;
        
        /* Check which buttons are pressed. */
        for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; button++) {
            if (glfwGetMouseButton(glfwData->dummyWindow, button) == GLFW_PRESS) {
                event.xmotion.state |= (1 << button);
            }
        }
        
        QueueEvent(&glfwData->eventQueue, &event);
        lastX = x;
        lastY = y;
    }
    
    /* Check for key events. */
    static int lastKeyStates[GLFW_KEY_LAST + 1] = {0};
    
    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; key++) {
        int state = glfwGetKey(glfwData->dummyWindow, key);
        
        if (state != lastKeyStates[key]) {
            XEvent event;
            memset(&event, 0, sizeof(XEvent));
            event.type = (state == GLFW_PRESS) ? KeyPress : KeyRelease;
            event.xkey.keycode = key;
            event.xkey.state = 0;
            
            /* Set modifier state. */
            if (glfwGetKey(glfwData->dummyWindow, GLFW_KEY_LEFT_SHIFT) || 
                glfwGetKey(glfwData->dummyWindow, GLFW_KEY_RIGHT_SHIFT)) {
                event.xkey.state |= ShiftMask;
            }
            if (glfwGetKey(glfwData->dummyWindow, GLFW_KEY_LEFT_CONTROL) || 
                glfwGetKey(glfwData->dummyWindow, GLFW_KEY_RIGHT_CONTROL)) {
                event.xkey.state |= ControlMask;
            }
            if (glfwGetKey(glfwData->dummyWindow, GLFW_KEY_LEFT_ALT) || 
                glfwGetKey(glfwData->dummyWindow, GLFW_KEY_RIGHT_ALT)) {
                event.xkey.state |= Mod1Mask;
            }
            
            QueueEvent(&glfwData->eventQueue, &event);
            lastKeyStates[key] = state;
        }
    }
    
    /* Check for clipboard updates. */
    const char *clipboardText = glfwGetClipboardString(glfwData->dummyWindow);
    static char *lastClipboardText = NULL;
    
    if ((clipboardText == NULL && lastClipboardText != NULL) ||
        (clipboardText != NULL && lastClipboardText == NULL) ||
        (clipboardText != NULL && lastClipboardText != NULL && 
         strcmp(clipboardText, lastClipboardText) != 0)) {
        
        XEvent event;
        memset(&event, 0, sizeof(XEvent));
        event.type = SelectionClear; 
        QueueEvent(&glfwData->eventQueue, &event);
        
        if (lastClipboardText) {
            Tcl_Free(lastClipboardText);
        }
        if (clipboardText) {
            lastClipboardText = Tcl_Alloc(strlen(clipboardText) + 1);
            strcpy(lastClipboardText, clipboardText);
        } else {
            lastClipboardText = NULL;
        }
    }

    /* Process any pending events from our queue. */
    ProcessPendingEvents(dispPtr);
    
    /* Check for GLFW errors. */
    if (lastGLFWError != 0) {
        fprintf(stderr, "GLFW error in DisplayCheckProc: %s\n", lastGLFWErrorMsg);
        lastGLFWError = 0;
    }
    
    /* Re-schedule ourselves for continuous polling. */
    Tcl_CreateTimerHandler(10, (Tcl_TimerProc *)DisplayCheckProc, dispPtr);
}

static void
DisplayFileProc(void *clientData, int flags)
{
    TkDisplay *dispPtr = (TkDisplay *)clientData;
    GLFWDisplayData *glfwData;
    
    (void)flags;
    
    if (!dispPtr) {
        return;
    }
    
    glfwData = (GLFWDisplayData *)dispPtr->glfw;
    if (!glfwData) {
        return;
    }

    /* Update poll timing statistics. */
    double currentTime = glfwGetTime();
    double elapsed = currentTime - glfwData->lastPollTime;
    glfwData->lastPollTime = currentTime;
    glfwData->pollCount++;

    /* Poll GLFW for events. */
    glfwPollEvents();
    
    /* Process queued events. */
    ProcessPendingEvents(dispPtr);
    
    /* Check for errors. */
    if (lastGLFWError != 0) {
        fprintf(stderr, "GLFW error during event processing: %s\n", 
                lastGLFWErrorMsg);
        lastGLFWError = 0;
    }
}

static void
ProcessPendingEvents(TkDisplay *dispPtr)
{
    GLFWDisplayData *glfwData;
    XEvent event;
    int processed = 0;
    
    if (!dispPtr) {
        return;
    }
    
    glfwData = (GLFWDisplayData *)dispPtr->glfw;
    if (!glfwData) {
        return;
    }

    /* Process all queued events. */
    while (DequeueEvent(&glfwData->eventQueue, &event)) {
        /* Queue event to Tk. */
        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
        processed++;
        
        /* Prevent infinite loops - process max 100 events per call. */
        if (processed >= 100) {
            break;
        }
    }
}

/* Clipboard management. */

void
TkClipCleanup(TkDisplay *dispPtr)
{
    GLFWDisplayData *glfwData;
    
    if (!dispPtr) {
        return;
    }
    
    glfwData = (GLFWDisplayData *)dispPtr->glfw;
    if (!glfwData) {
        return;
    }

    /* Free cached clipboard text. */
    if (glfwData->clipboardText) {
        Tcl_Free(glfwData->clipboardText);
        glfwData->clipboardText = NULL;
        glfwData->clipboardLen = 0;
    }
}

int
TkpClipboardSetText(TkDisplay *dispPtr, const char *text)
{
    GLFWDisplayData *glfwData;
    
    if (!dispPtr || !text) {
        return 0;
    }
    
    glfwData = (GLFWDisplayData *)dispPtr->glfw;
    if (!glfwData || !glfwData->dummyWindow) {
        return 0;
    }

    /* Set clipboard via GLFW. */
    glfwSetClipboardString(glfwData->dummyWindow, text);
    
    /* Update cache. */
    if (glfwData->clipboardText) {
        Tcl_Free(glfwData->clipboardText);
    }
    
    glfwData->clipboardLen = strlen(text);
    glfwData->clipboardText = (char *)Tcl_Alloc(glfwData->clipboardLen + 1);
    strcpy(glfwData->clipboardText, text);
    
    return 1;
}

const char *
TkpClipboardGetText(TkDisplay *dispPtr)
{
    GLFWDisplayData *glfwData;
    const char *text;
    
    if (!dispPtr) {
        return NULL;
    }
    
    glfwData = (GLFWDisplayData *)dispPtr->glfw;
    if (!glfwData || !glfwData->dummyWindow) {
        return NULL;
    }

    /* Get clipboard from GLFW. */
    text = glfwGetClipboardString(glfwData->dummyWindow);
    if (!text) {
        return NULL;
    }
    
    /* Update cache. */
    if (glfwData->clipboardText) {
        Tcl_Free(glfwData->clipboardText);
    }
    
    glfwData->clipboardLen = strlen(text);
    glfwData->clipboardText = (char *)Tcl_Alloc(glfwData->clipboardLen + 1);
    strcpy(glfwData->clipboardText, text);
    
    return glfwData->clipboardText;
}

/* Event processing utilities. */

int
TkUnixDoOneXEvent(Tcl_Time *timePtr)
{
    double timeout_sec = 0.0;
    int result;
    
    /* Calculate timeout */
    if (timePtr) {
        Tcl_Time now;
        Tcl_GetTime(&now);
        
        timeout_sec = (timePtr->sec - now.sec) + 
                     (timePtr->usec - now.usec) / 1000000.0;
        
        if (timeout_sec < 0.0) {
            timeout_sec = 0.0;
        }
    }

    /* Wait for or poll events. */
    if (timeout_sec <= 0.0) {
        glfwPollEvents();
    } else {
        /* Cap timeout at reasonable value (1 second). */
        if (timeout_sec > 1.0) {
            timeout_sec = 1.0;
        }
        glfwWaitEventsTimeout(timeout_sec);
    }

    /* Service Tcl events. */
    result = Tcl_ServiceAll();
    
    return result;
}

void
TkpSync(Display *display)
{
    (void)display;
    
    /* 
     * GLFW doesn't have a direct sync equivalent.
     * Poll events to ensure all pending events are processed.
     */
    glfwPollEvents();
}

/* Pointer/cursor management. */

void
TkpWarpPointer(TkDisplay *dispPtr)
{
    (void)dispPtr;
    
    /*
     * Wayland does not allow global pointer warping for security reasons.
     * Local warping within a window is possible via glfwSetCursorPos,
     * but requires the window handle and coordinates.
     * 
     * This would need to be implemented at the window level, not display level.
     */
}

int
TkpGetCursorPosition(TkDisplay *dispPtr, int *xPtr, int *yPtr)
{
    GLFWDisplayData *glfwData;
    double x, y;
    
    if (!dispPtr || !xPtr || !yPtr) {
        return 0;
    }
    
    glfwData = (GLFWDisplayData *)dispPtr->glfw;
    if (!glfwData || !glfwData->dummyWindow) {
        return 0;
    }

    /* Get cursor position relative to dummy window. */
    glfwGetCursorPos(glfwData->dummyWindow, &x, &y);
    
    *xPtr = (int)x;
    *yPtr = (int)y;
    
    return 1;
}



/* Platform detection.  */

int
TkpIsWayland(TkDisplay *dispPtr)
{
    GLFWDisplayData *glfwData;
    
    if (!dispPtr) {
        return 0;
    }
    
    glfwData = (GLFWDisplayData *)dispPtr->glfw;
    if (!glfwData) {
        return 0;
    }
    
    return glfwData->isWayland;
}



/* Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
