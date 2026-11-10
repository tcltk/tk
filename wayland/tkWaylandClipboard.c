/*
 * tkWaylandClipboard.c -- 
 *
 *      Native Wayland clipboard for the Tk/GLFW backend.
 *
 *      Tk owns the Wayland selection itself via wl_data_device /
 *      wl_data_source / wl_data_offer, using the same wl_display and
 *      wl_surface GLFW already owns. Supports both external integration
 *      with Wayland system clipboard and Tk's internal clipboard. 

 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution.
 */

#include "tkInt.h"
#include "tkSelect.h"
#include "tkWaylandInt.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/*
 * Debugging.
 #define DEBUG_CHANNEL stdout
 #define DEBUG_LABEL "clipboard"
*/

/*
 *  Wayland state.                                                      
 */

typedef struct WaylandClipboard {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_data_device_manager *manager;
    struct wl_seat *seat;
    struct wl_data_device *device;
    struct wl_keyboard *keyboard;
    struct wl_data_source *source;  /* Our current source, or NULL. */
    struct wl_data_offer *offer;    /* Current selection offer, or NULL. */
    struct wl_data_offer *incoming; /* Introduced, not yet the selection. */
    char **offerMimes;              /* Strdup'd mime list for offer. */
    int numOfferMimes;
    uint32_t lastSerial;            /* Most recent keyboard enter serial. */
    int haveSerial;
    int claimIssued;                /* set_selection() sent for c->source. */
    int sourceConfirmed;            /* Compositor echoed our source back. */
    int Initialized;                /* First-touch setup performed. */
    int ready;                      /* manager + seat + device all bound. */
    struct wl_event_queue *queue;   /* Private queue for all our objects. */
    int sourceInstalled;            /* Tcl event source registered. */
    int kickLoop;                   /* Default queue left non-empty. */
    char *text;                     /* Owned copy, what we published. */
    size_t textLen;
} WaylandClipboard;

static WaylandClipboard clip = {0};

/* Forward declarations. */
static void WaylandPublish(const char *text, size_t len);
static void WaylandClear(void);
static void WaylandDispatch(void);
static void ClipSourceSetup(ClientData clientData, int flags);
static void ClipSourceCheck(ClientData clientData, int flags);
static void ClipExitHandler(ClientData clientData);
static void WaylandTryClaimSource(void);
static void QueueClipboardLost(void);

/*
 *----------------------------------------------------------------------
 *
 * QueueClipboardLost --
 *
 *      Tell Tk's generic code that some other Wayland client now owns
 *      the clipboard.  Tk's generic [clipboard append] registered Tk
 *      as owner of CLIPBOARD via Tk_OwnSelection (XSetSelectionOwner
 *      is a stub here), so until Tk is told otherwise, [clipboard get]
 *      and Ctrl+V are answered from Tk's own buffers and never reach
 *      TkSelGetSelection -- external copies would be invisible.  On
 *      X11 the server sends SelectionClear; here we synthesize one and
 *      queue it so it runs from Tk's event loop, never from inside a
 *      Wayland dispatch callback.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May queue a SelectionClear event for the CLIPBOARD selection.
 *
 *----------------------------------------------------------------------
 */

static void
QueueClipboardLost(void)
{
    TkDisplay *dispPtr = TkGetDisplayList();
    XEvent event;

    if (dispPtr == NULL || dispPtr->clipWindow == NULL ||
	!dispPtr->clipboardActive) {
	return;
    }
    DEBUG_LOG("QueueClipboardLost: queueing SelectionClear");
    memset(&event, 0, sizeof(event));
    event.xselectionclear.type = SelectionClear;
    event.xselectionclear.display = Tk_Display(dispPtr->clipWindow);
    event.xselectionclear.window = Tk_WindowId(dispPtr->clipWindow);
    event.xselectionclear.selection = dispPtr->clipboardAtom;
    event.xselectionclear.time = dispPtr->lastEventTime;
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *  wl_data_source listener -- we are the clipboard owner.             
 */

/*
 *----------------------------------------------------------------------
 *
 * SourceSend --
 *
 *      wl_data_source "send" handler.  The compositor, on behalf of a
 *      pasting application, has requested that we write the selection
 *      contents for the given mime type into fd.  This routine writes
 *      the in-memory text (for any UTF-8-family mime type) and closes
 *      fd.  Unknown mime types get an empty reply (immediate close) so
 *      the pasting application does not hang.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Writes to fd and closes it.  Does not block.
 *
 *----------------------------------------------------------------------
 */

static void
SourceSend(void *data,
	   TCL_UNUSED(struct wl_data_source *),
	   const char *mimeType,
	   int32_t fd)
{
    WaylandClipboard *c = (WaylandClipboard *)data;
    /*
     * Compositor (on behalf of the pasting app) wants our data.
     * Write it, close the fd, return. Never block: the fd is
     * non-blocking by contract and our text is in memory.
     */
    DEBUG_LOG("SourceSend: mime=%s fd=%d textLen=%zu",
	      mimeType, fd, c->textLen);
    if (c->text != NULL && c->textLen > 0 &&
        (strcmp(mimeType, "text/plain") == 0 ||
         strcmp(mimeType, "text/plain;charset=utf-8") == 0 ||
         strcmp(mimeType, "UTF8_STRING") == 0 ||
         strcmp(mimeType, "STRING") == 0 ||
         strcmp(mimeType, "COMPOUND_TEXT") == 0)) {
        size_t off = 0;
        while (off < c->textLen) {
            ssize_t n = write(fd, c->text + off, c->textLen - off);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                DEBUG_LOG("SourceSend: write failed: %s", strerror(errno));
                break;
            }
            if (n == 0) {
                break;
            }
            off += (size_t)n;
        }
        DEBUG_LOG("SourceSend: wrote %zu of %zu bytes", off, c->textLen);
    } else {
        DEBUG_LOG("SourceSend: empty reply for mime=%s", mimeType);
    }
    close(fd);
}

/*
 *----------------------------------------------------------------------
 *
 * SourceCancelled --
 *
 *      wl_data_source "cancelled" handler.  Fires when another client
 *      has taken the selection.  We drop the source pointer, destroy
 *      the object, free our text, and tell Tk it has lost ownership of
 *      CLIPBOARD so later pastes read the new owner's data.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Destroys the wl_data_source, clears c->source, frees c->text,
 *      queues a SelectionClear for Tk.
 *
 *----------------------------------------------------------------------
 */

static void
SourceCancelled(void *data, struct wl_data_source *source)
{
    WaylandClipboard *c = (WaylandClipboard *)data;
    int wasOurs = (c->source == source);

    DEBUG_LOG("SourceCancelled: source=%p (ours=%d)",
	      (void *)source, wasOurs);
    if (wasOurs) {
	c->source = NULL;
	c->sourceConfirmed = 0;
	c->claimIssued = 0;
	ckfree(c->text);
	c->text = NULL;
	c->textLen = 0;
    }
    wl_data_source_destroy(source);
    if (wasOurs) {
	QueueClipboardLost();
    }
}

static const struct wl_data_source_listener sourceListener = {
    .target = NULL,
    .send = SourceSend,
    .cancelled = SourceCancelled,
    .dnd_drop_performed = NULL,
    .dnd_finished = NULL,
    .action = NULL,
};

/* 
 *  wl_data_offer listener -- someone else owns the clipboard.          
 */

/*
 *----------------------------------------------------------------------
 *
 * OfferOffer --
 *
 *      wl_data_offer "offer" handler.  Records each mime type that the
 *      current owner is offering, appending to the strdup'd mime list
 *      used later by WaylandReadOffer to pick a target.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Grows c->offerMimes and increments c->numOfferMimes.
 *
 *----------------------------------------------------------------------
 */

static void
OfferOffer(void *data,
	   TCL_UNUSED(struct wl_data_offer *),
	   const char *mimeType)
{
    WaylandClipboard *c = (WaylandClipboard *)data;
    DEBUG_LOG("OfferOffer: mime=%s (now %d mimes)",
	      mimeType, c->numOfferMimes + 1);
    c->offerMimes = (char **)ckrealloc((char *)c->offerMimes,
				       sizeof(char *) * (size_t)(c->numOfferMimes + 1));
    {
	char *dup = (char *)ckalloc(strlen(mimeType) + 1);
	strcpy(dup, mimeType);
	c->offerMimes[c->numOfferMimes++] = dup;
    }
}

static const struct wl_data_offer_listener offerListener = {
    .offer = OfferOffer,
    .source_actions = NULL,
    .action = NULL,
};

/*
 * wl_data_device listener.                                           
 */

/*
 *----------------------------------------------------------------------
 *
 * DeviceDataOffer --
 *
 *      wl_data_device "data_offer" handler.  A new offer has been
 *      introduced.  It is not necessarily the selection (it could be
 *      drag-and-drop), so it is parked in c->incoming until a
 *      "selection" event adopts it.  An earlier offer that was never
 *      adopted is destroyed.  The mime list is reset and the offer
 *      listener attached so its types are recorded as they arrive.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Frees old mime strings, replaces c->incoming, registers listener.
 *
 *----------------------------------------------------------------------
 */

static void
DeviceDataOffer(void *data,
		TCL_UNUSED(struct wl_data_device *),
		struct wl_data_offer *offer)
{
    WaylandClipboard *c = (WaylandClipboard *)data;
    int i;
    DEBUG_LOG("DeviceDataOffer: offer=%p supersedes %d old mimes",
	      (void *)offer, c->numOfferMimes);
    if (c->incoming != NULL && c->incoming != c->offer) {
	wl_data_offer_destroy(c->incoming);
    }
    for (i = 0; i < c->numOfferMimes; i++) {
	ckfree(c->offerMimes[i]);
    }
    ckfree((char *)c->offerMimes);
    c->offerMimes = NULL;
    c->numOfferMimes = 0;
    c->incoming = offer;
    wl_data_offer_add_listener(offer, &offerListener, c);
}

/*
 *----------------------------------------------------------------------
 *
 * DeviceSelection --
 *
 *      wl_data_device "selection" handler.  Announces the current
 *      selection owner's offer (or NULL when the clipboard is empty).
 *
 *      If our source has had set_selection() issued but is not yet
 *      confirmed, a non-NULL offer is taken to be our own echo.  A NULL
 *      offer is ignored in that state: it is normally the echo of an
 *      earlier clear (Tk's copy is "clear" then "append", and both
 *      requests are sent before either reply is read), not a rejection.
 *
 *      If our source was never claimed (no serial yet) and another
 *      client's offer arrives, our provisional source is dropped so it
 *      cannot later overwrite the new owner.
 *
 *      Whenever a foreign owner appears, Tk is told it lost CLIPBOARD
 *      (QueueClipboardLost) so that paste reads from the offer.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Adopts/destroys offers, may destroy a stale provisional source,
 *      updates c->offer and c->sourceConfirmed, may queue a
 *      SelectionClear for Tk.
 *
 *----------------------------------------------------------------------
 */

static void
DeviceSelection(void *data, struct wl_data_device *device,
		struct wl_data_offer *offer)
{
    WaylandClipboard *c = (WaylandClipboard *)data;
    int foreign = 0;
    (void)device;
    DEBUG_LOG("DeviceSelection: offer=%s sourceConfirmed=%d claimIssued=%d",
	      offer ? "present" : "NULL (clipboard empty)",
	      c->sourceConfirmed, c->claimIssued);

    if (c->source != NULL && !c->sourceConfirmed) {
	if (offer == NULL) {
	    DEBUG_LOG("DeviceSelection: NULL while unconfirmed, ignoring");
	} else if (c->claimIssued) {
	    DEBUG_LOG("DeviceSelection: source confirmed");
	    c->sourceConfirmed = 1;
	} else {
	    struct wl_data_source *old = c->source;
	    DEBUG_LOG("DeviceSelection: foreign owner beat provisional "
		      "source, dropping it");
	    c->source = NULL;
	    wl_data_source_destroy(old);
	    ckfree(c->text);
	    c->text = NULL;
	    c->textLen = 0;
	    foreign = 1;
	}
    } else if (c->source == NULL && offer != NULL) {
	foreign = 1;
    }

    if (c->offer != NULL && c->offer != offer) {
	wl_data_offer_destroy(c->offer);
    }
    c->offer = offer;
    c->incoming = NULL;
    if (offer == NULL) {
	int i;
	for (i = 0; i < c->numOfferMimes; i++) {
	    ckfree(c->offerMimes[i]);
	}
	ckfree((char *)c->offerMimes);
	c->offerMimes = NULL;
	c->numOfferMimes = 0;
    }
    if (foreign) {
	QueueClipboardLost();
    }
}

static const struct wl_data_device_listener deviceListener = {
    .data_offer = DeviceDataOffer,
    .enter = NULL,
    .leave = NULL,
    .motion = NULL,
    .drop = NULL,
    .selection = DeviceSelection,
};

/* 
 *  Keyboard: serial tracking only.  We do not need wl_pointer.
 */

/*
 *----------------------------------------------------------------------
 *
 * KeyboardKeymap --
 *
 *      wl_keyboard "keymap" handler.  The received fd is closed.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Closes fd.
 *
 *----------------------------------------------------------------------
 */

static void
KeyboardKeymap(TCL_UNUSED(void *), /* data */
	       TCL_UNUSED(struct wl_keyboard *), /* kdb */
	       TCL_UNUSED(uint32_t),  /* fmt */
	       int32_t fd,
	       TCL_UNUSED(uint32_t)) /* size */
{
    close(fd);
}

/*
 *----------------------------------------------------------------------
 *
 * KeyboardEnter --
 *
 *      wl_keyboard "enter" handler.  Records the enter serial, which
 *      is the serial set_selection() will accept.  If our source is
 *      still provisional, attempt to claim it now.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates lastSerial/haveSerial; may issue set_selection.
 *
 *----------------------------------------------------------------------
 */

static void
KeyboardEnter(void *data,
	      TCL_UNUSED(struct wl_keyboard *),
	      uint32_t serial,
	      TCL_UNUSED(struct wl_surface *),
	      TCL_UNUSED(struct wl_array *))
{
    WaylandClipboard *c = (WaylandClipboard *)data;
    DEBUG_LOG("KeyboardEnter: serial=%u source=%p",
	      serial, (void *)c->source);
    c->lastSerial = serial;
    c->haveSerial = 1;
    WaylandTryClaimSource();
}

/*
 *----------------------------------------------------------------------
 *
 * KeyboardLeave --
 *
 *      wl_keyboard "leave" handler.  We retain the last serial; GNOME
 *      in practice still accepts it for set_selection after leave.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
KeyboardLeave(TCL_UNUSED(void *), 
	      TCL_UNUSED(struct wl_keyboard *),
	      TCL_UNUSED(uint32_t), 
	      TCL_UNUSED(struct wl_surface *))
{
    DEBUG_LOG("KeyboardLeave: retaining serial=%u", clip.lastSerial);
}

static void
KeyboardKey(void *data,
	    TCL_UNUSED(struct wl_keyboard *),
	    uint32_t serial,
	    TCL_UNUSED(uint32_t),
	    TCL_UNUSED(uint32_t),
	    TCL_UNUSED(uint32_t))
{
    /*
     * Keep the freshest input serial: Ctrl+C reaches us as a key event
     * immediately before [clipboard append], and compositors are most
     * willing to honor set_selection() with a recent serial.  The
     * keyboard enter serial can be too old by then.
     */
    WaylandClipboard *c = (WaylandClipboard *)data;
    c->lastSerial = serial;
    c->haveSerial = 1;
}

static void
KeyboardModifiers(void *data,
		  TCL_UNUSED(struct wl_keyboard *),
		  uint32_t serial,
		  TCL_UNUSED(uint32_t),
		  TCL_UNUSED(uint32_t),
		  TCL_UNUSED(uint32_t),
		  TCL_UNUSED(uint32_t))
{
    WaylandClipboard *c = (WaylandClipboard *)data;
    c->lastSerial = serial;
    c->haveSerial = 1;
}

/*
 *----------------------------------------------------------------------
 *
 * KeyboardRepeatInfo  --
 *
 *      Info on keyboard repeating. 
 *
 * Results:
 *      None. No-op with GLFW. 
 *
 * Side effects:
 *      None.
 * 
 *----------------------------------------------------------------------
 */

static void
KeyboardRepeatInfo(TCL_UNUSED(void *), 
		   TCL_UNUSED(struct wl_keyboard *),
		   TCL_UNUSED(int32_t), 
		   TCL_UNUSED(int32_t))
{
    /* No-op. */
}

static const struct wl_keyboard_listener keyboardListener = {
    .keymap = KeyboardKeymap,
    .enter = KeyboardEnter,
    .leave = KeyboardLeave,
    .key = KeyboardKey,
    .modifiers = KeyboardModifiers,
    .repeat_info = KeyboardRepeatInfo,
};

/* 
 *  Seat + registry. 
 */

/*
 *----------------------------------------------------------------------
 *
 * SeatCapabilities --
 *
 *      wl_seat "capabilities" handler.  Binds wl_keyboard (for serial
 *      tracking) and creates the wl_data_device once both the manager
 *      and the seat are known.  Idempotent.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May create keyboard/device objects and add listeners; sets
 *      clip.ready when the device is installed.
 *
 *----------------------------------------------------------------------
 */

static void
SeatCapabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    WaylandClipboard *c = (WaylandClipboard *)data;
    DEBUG_LOG("SeatCapabilities: caps=0x%x kbd=%p dev=%p",
	      caps, (void *)c->keyboard, (void *)c->device);
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && c->keyboard == NULL) {
        c->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(c->keyboard, &keyboardListener, c);
    }
    if (c->device == NULL && c->manager != NULL) {
        c->device = wl_data_device_manager_get_data_device(c->manager, seat);
        wl_data_device_add_listener(c->device, &deviceListener, c);
        DEBUG_LOG("SeatCapabilities: created data device %p",
		  (void *)c->device);
    }
    if (c->device != NULL) {
        if (!c->ready) {
            DEBUG_LOG("SeatCapabilities: clipboard ready");
        }
        c->ready = 1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SeatName --
 *
 *      wl_seat identifier. 
 *
 * Results:
 *      None. No-op with GLFW. 
 *
 * Side effects:
 *      None.
 * 
 *----------------------------------------------------------------------
 */

static void
SeatName(TCL_UNUSED(void *), 
	 TCL_UNUSED(struct wl_seat *),
	 TCL_UNUSED(const char *))
{
    /* No-op. */
}

static const struct wl_seat_listener seatListener = {
    .capabilities = SeatCapabilities,
    .name = SeatName,
};

/*
 *----------------------------------------------------------------------
 *
 * RegistryGlobal --
 *
 *      wl_registry "global" handler.  Binds wl_data_device_manager
 *      (v3) and wl_seat (v7, capped).  A seat listener is attached so
 *      SeatCapabilities can create the data device.  After the seat
 *      is bound, capabilities arrive on the next event dispatch and
 *      SeatCapabilities installs the device.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Binds globals.
 *
 *----------------------------------------------------------------------
 */

static void
RegistryGlobal(void *data, struct wl_registry *registry, uint32_t name,
	       const char *interface, uint32_t version)
{
    WaylandClipboard *c = (WaylandClipboard *)data;
    if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        uint32_t v = version < 3 ? version : 3;
        DEBUG_LOG("RegistryGlobal: binding %s v%u (advertised %u)",
		  interface, v, version);
        c->manager = wl_registry_bind(registry, name,
				      &wl_data_device_manager_interface, v);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        uint32_t v = version < 7 ? version : 7;
        DEBUG_LOG("RegistryGlobal: binding %s v%u (advertised %u)",
		  interface, v, version);
        c->seat = wl_registry_bind(registry, name,
				   &wl_seat_interface, v);
        wl_seat_add_listener(c->seat, &seatListener, c);
    }

    /*
     * If both globals have now been bound and the seat already
     * delivered its capabilities, SeatCapabilities created the device
     * inline.  If capabilities have not arrived yet, they will on the
     * next pump and SeatCapabilities will set ready then.
     */
    if (c->manager != NULL && c->seat != NULL && c->device == NULL) {
        c->device = wl_data_device_manager_get_data_device(c->manager, c->seat);
        wl_data_device_add_listener(c->device, &deviceListener, c);
        DEBUG_LOG("RegistryGlobal: created data device %p", (void *)c->device);
        c->ready = 1;
        DEBUG_LOG("RegistryGlobal: clipboard ready");
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RegistryGlobalRemove --
 *
 *      wl_registry "global" handler.  
 *
 * Results:
 *      None. No-op with GLFW.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
 
static void
RegistryGlobalRemove(TCL_UNUSED(void *), 
		     TCL_UNUSED(struct wl_registry *),
		     TCL_UNUSED(uint32_t))
{
    /* No-op. */
}

static const struct wl_registry_listener registryListener = {
    .global = RegistryGlobal,
    .global_remove = RegistryGlobalRemove,
};

/* 
 *  Init / pump.
 */

/*
 *----------------------------------------------------------------------
 *
 * WaylandEnsureInitialized --
 *
 *      Non-blocking lazy initialization.  On first touch, grab the
 *      wl_display GLFW already owns and create a private wl_event_queue
 *      for every clipboard object (registry, seat, keyboard, data
 *      device, data source, data offers).  Objects inherit the queue of
 *      the proxy that creates them, so everything below the registry
 *      lands in this queue and GLFW's own objects stay in the default
 *      queue.  The queue is serviced by a Tcl event source; no Tcl file
 *      handler is installed on the display fd, because Tcl keeps one
 *      handler per fd and ours would replace the notifier's.  Do NOT
 *      roundtrip here: that would block the main thread.
 *
 *      Returns 1 only when the clipboard is fully ready (display,
 *      manager, seat, and data device all present).  Callers that get
 *      0 simply drop their request; the next clipboard operation
 *      retries after the globals have been delivered.
 *
 * Results:
 *      Returns 1 if the clipboard is ready, 0 otherwise.
 *
 * Side effects:
 *      On first call: creates the event queue and registry, adds the
 *      registry listener, registers the Tcl event source and a thread
 *      exit handler.  Subsequent calls are cheap.
 *
 *----------------------------------------------------------------------
 */

static int
WaylandEnsureInitialized(void)
{
    if (!clip.Initialized) {
	struct wl_display *wrapper;

	clip.Initialized = 1;

	clip.display = glfwGetWaylandDisplay();
	DEBUG_LOG("WaylandEnsureInitialized: glfwGetWaylandDisplay()=%p",
		  (void *)clip.display);
	if (clip.display == NULL) {
	    return 0;
	}

	clip.queue = wl_display_create_queue(clip.display);
	if (clip.queue == NULL) {
	    clip.display = NULL;
	    return 0;
	}

	/*
	 * Create the registry through a proxy wrapper that already
	 * points at our queue; this is the race-free way to get the
	 * registry (and everything bound from it) into a private queue.
	 */
	wrapper = (struct wl_display *)wl_proxy_create_wrapper(clip.display);
	if (wrapper == NULL) {
	    wl_event_queue_destroy(clip.queue);
	    clip.queue = NULL;
	    clip.display = NULL;
	    return 0;
	}
	wl_proxy_set_queue((struct wl_proxy *)wrapper, clip.queue);
	clip.registry = wl_display_get_registry(wrapper);
	wl_proxy_wrapper_destroy(wrapper);
	wl_registry_add_listener(clip.registry, &registryListener, &clip);

	Tcl_CreateEventSource(ClipSourceSetup, ClipSourceCheck, NULL);
	clip.sourceInstalled = 1;
	Tcl_CreateThreadExitHandler(ClipExitHandler, NULL);

	/*
	 * Flush the registry request now so the globals start arriving.
	 * No read/wait here.
	 */
	wl_display_flush(clip.display);
    }

    if (!clip.ready) {
	DEBUG_LOG("WaylandEnsureInitialized: not ready yet "
		  "(manager=%p seat=%p device=%p)",
		  (void *)clip.manager, (void *)clip.seat,
		  (void *)clip.device);
	return 0;
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * WaylandDispatch --
 *
 *      Run the callbacks of events already queued for the clipboard's
 *      private queue, then flush outgoing requests.  Never reads the
 *      socket and never touches the default queue, so GLFW callbacks
 *      cannot run from here (e.g. inside [clipboard get]).  Events reach
 *      the clipboard queue whenever anyone reads the display fd, which
 *      GLFW/the notifier already does.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Delivers Wayland events to our listeners.
 *
 *----------------------------------------------------------------------
 */

static void
WaylandDispatch(void)
{
    if (clip.display == NULL || clip.queue == NULL) {
	return;
    }
    wl_display_dispatch_queue_pending(clip.display, clip.queue);
    wl_display_flush(clip.display);
}

/*
 *----------------------------------------------------------------------
 *
 * ClipSourceSetup, ClipSourceCheck --
 *
 *      Tcl event source for the clipboard queue.  Setup keeps the event
 *      loop from sleeping while events are queued (or when
 *      WaylandReadOffer left GLFW events in the default queue); check
 *      dispatches the clipboard queue from Tk's event loop, never from
 *      inside glfwPollEvents().
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May set the maximum block time to zero; check runs callbacks.
 *
 *----------------------------------------------------------------------
 */

static void
ClipSourceSetup(TCL_UNUSED(ClientData),
		int flags)
{
    static const Tcl_Time zero = {0, 0};
    int pending = 0;

    if (!(flags & TCL_FILE_EVENTS) || clip.display == NULL ||
	clip.queue == NULL) {
	return;
    }
    if (clip.kickLoop) {
	clip.kickLoop = 0;
	pending = 1;
    }
    if (wl_display_prepare_read_queue(clip.display, clip.queue) != 0) {
	pending = 1;            /* Events are waiting in our queue. */
    } else {
	wl_display_cancel_read(clip.display);
    }
    if (pending) {
	Tcl_SetMaxBlockTime(&zero);
    }
}

static void
ClipSourceCheck(TCL_UNUSED(ClientData),
		int flags)
{
    if (flags & TCL_FILE_EVENTS) {
	WaylandDispatch();
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandClipboardPump --
 *
 *      Public entry point; dispatches already-queued clipboard events.
 *      Safe to call often; never blocks, never reads the socket, never
 *      runs GLFW callbacks.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Same as WaylandDispatch.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandClipboardPump(void)
{
    WaylandDispatch();
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandClipboardCleanup --
 *
 *      Destroy every Wayland object the clipboard created, then its
 *      event queue, and unregister the event source.  Idempotent.
 *
 *      MUST run before glfwTerminate() closes the wl_display.  The
 *      backend's shutdown path should call this directly, immediately
 *      before glfwTerminate(); the thread exit handler registered at
 *      initialization is only a safety net, and its ordering relative
 *      to glfwTerminate() is not guaranteed.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Destroys proxies, flushes the display, frees clipboard state,
 *      resets it so a later initialization starts clean.  If we own the
 *      selection, destroying our source drops it.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandClipboardCleanup(void)
{
    int i;

    if (!clip.Initialized) {
	return;
    }
    if (clip.sourceInstalled) {
	Tcl_DeleteEventSource(ClipSourceSetup, ClipSourceCheck, NULL);
	clip.sourceInstalled = 0;
    }
    Tcl_DeleteThreadExitHandler(ClipExitHandler, NULL);

    if (clip.display != NULL) {
	if (clip.source != NULL) {
	    wl_data_source_destroy(clip.source);
	}
	if (clip.offer != NULL) {
	    wl_data_offer_destroy(clip.offer);
	}
	if (clip.incoming != NULL && clip.incoming != clip.offer) {
	    wl_data_offer_destroy(clip.incoming);
	}
	if (clip.keyboard != NULL) {
	    if (wl_proxy_get_version((struct wl_proxy *)clip.keyboard) >= 3) {
		wl_keyboard_release(clip.keyboard);
	    } else {
		wl_keyboard_destroy(clip.keyboard);
	    }
	}
	if (clip.device != NULL) {
	    if (wl_proxy_get_version((struct wl_proxy *)clip.device) >= 2) {
		wl_data_device_release(clip.device);
	    } else {
		wl_data_device_destroy(clip.device);
	    }
	}
	if (clip.manager != NULL) {
	    wl_data_device_manager_destroy(clip.manager);
	}
	if (clip.seat != NULL) {
	    if (wl_proxy_get_version((struct wl_proxy *)clip.seat) >= 5) {
		wl_seat_release(clip.seat);
	    } else {
		wl_seat_destroy(clip.seat);
	    }
	}
	if (clip.registry != NULL) {
	    wl_registry_destroy(clip.registry);
	}
	wl_display_flush(clip.display);
	if (clip.queue != NULL) {
	    /* All proxies attached to the queue are gone by now. */
	    wl_event_queue_destroy(clip.queue);
	}
    }

    for (i = 0; i < clip.numOfferMimes; i++) {
	ckfree(clip.offerMimes[i]);
    }
    ckfree((char *)clip.offerMimes);
    ckfree(clip.text);
    memset(&clip, 0, sizeof(clip));
}

static void
ClipExitHandler(TCL_UNUSED(ClientData))
{
    TkWaylandClipboardCleanup();
}

/* 
 *  Publish / clear.
 */

/*
 *----------------------------------------------------------------------
 *
 * WaylandTryClaimSource --
 *
 *      If a wl_data_source is installed but not yet confirmed, and a
 *      serial is available, issue set_selection() with the current
 *      serial.  This is the only place set_selection() is called for
 *      a publish.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May issue set_selection and flush the display.
 *
 *----------------------------------------------------------------------
 */

static void
WaylandTryClaimSource(void)
{
    if (clip.source == NULL || clip.sourceConfirmed) {
	return;
    }
    if (!clip.ready) {
	DEBUG_LOG("WaylandTryClaimSource: clipboard not ready, deferring");
	return;
    }
    if (!clip.haveSerial) {
	DEBUG_LOG("WaylandTryClaimSource: no serial yet, deferring");
	return;
    }
    DEBUG_LOG("WaylandTryClaimSource: set_selection source=%p serial=%u",
	      (void *)clip.source, clip.lastSerial);
    wl_data_device_set_selection(clip.device, clip.source, clip.lastSerial);
    clip.claimIssued = 1;
    wl_display_flush(clip.display);
}

/*
 *----------------------------------------------------------------------
 *
 * WaylandPublish --
 *
 *      Publish text as the Wayland selection.  Creates a
 *      wl_data_source immediately, offers the UTF-8 mime family, and
 *      attempts to claim it with the current serial.  If no serial is
 *      available yet, the source stays provisional and is claimed on
 *      the next KeyboardEnter.  If the clipboard is not yet ready
 *      (asynchronous init), the request is dropped; the next
 *      [clipboard append] retries once the pump has delivered globals.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Destroys any previous source, allocates clip.text, creates a
 *      new wl_data_source, may issue set_selection.
 *
 *----------------------------------------------------------------------
 */

static void
WaylandPublish(const char *text, size_t len)
{
    DEBUG_LOG("WaylandPublish: len=%zu haveSerial=%d ready=%d",
	      len, clip.haveSerial, clip.ready);
    if (!WaylandEnsureInitialized()) {
	DEBUG_LOG("WaylandPublish: clipboard not ready, dropping");
	return;
    }

    /* Replace any previous source; its cancelled() will fire. */
    if (clip.source != NULL) {
	struct wl_data_source *old = clip.source;
	DEBUG_LOG("WaylandPublish: destroying previous source %p",
		  (void *)old);
	clip.source = NULL;
	clip.sourceConfirmed = 0;
	clip.claimIssued = 0;
	wl_data_source_destroy(old);
    }
    ckfree(clip.text);
    clip.text = (char *)ckalloc(len + 1);
    memcpy(clip.text, text, len);
    clip.text[len] = '\0';
    clip.textLen = len;

    clip.source = wl_data_device_manager_create_data_source(clip.manager);
    wl_data_source_add_listener(clip.source, &sourceListener, &clip);
    wl_data_source_offer(clip.source, "text/plain;charset=utf-8");
    wl_data_source_offer(clip.source, "text/plain");
    wl_data_source_offer(clip.source, "UTF8_STRING");
    wl_data_source_offer(clip.source, "STRING");
    wl_data_source_offer(clip.source, "COMPOUND_TEXT");

    DEBUG_LOG("WaylandPublish: new source=%p", (void *)clip.source);
    WaylandTryClaimSource();
}

/*
 *----------------------------------------------------------------------
 *
 * WaylandClear --
 *
 *      Clear the Wayland selection, but only if we own it.  If we have
 *      a source installed, destroy it (and drop our text) and issue
 *      set_selection(NULL) with the current serial if one is available.
 *      If we do NOT own the selection (another client does, e.g. after
 *      an external copy), nothing is sent: Tk's own buffers are cleared
 *      by generic code, but we must never wipe someone else's clipboard.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Destroys wl_data_source, frees clip.text, may issue
 *      set_selection(NULL).
 *
 *----------------------------------------------------------------------
 */

static void
WaylandClear(void)
{
    int owned;

    DEBUG_LOG("WaylandClear: haveSerial=%d source=%p ready=%d",
	      clip.haveSerial, (void *)clip.source, clip.ready);
    if (!WaylandEnsureInitialized()) {
	DEBUG_LOG("WaylandClear: clipboard not ready, dropping");
	return;
    }

    owned = (clip.source != NULL);
    if (clip.source != NULL) {
	struct wl_data_source *old = clip.source;
	clip.source = NULL;
	clip.sourceConfirmed = 0;
	clip.claimIssued = 0;
	wl_data_source_destroy(old);
    }
    ckfree(clip.text);
    clip.text = NULL;
    clip.textLen = 0;

    if (owned && clip.haveSerial) {
	DEBUG_LOG("WaylandClear: set_selection(NULL) serial=%u",
		  clip.lastSerial);
	wl_data_device_set_selection(clip.device, NULL, clip.lastSerial);
	wl_display_flush(clip.display);
    } else {
	DEBUG_LOG("WaylandClear: nothing to clear on the compositor");
    }
}

/*
 *  Read incoming offer (clipboard get).
 */

static long
MonotonicMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + (long)(ts.tv_nsec / 1000000L);
}

/*
 *----------------------------------------------------------------------
 *
 * WaylandReadOffer --
 *
 *      Read the current incoming offer (owned by another application,
 *      or echoed back to us by the compositor if we are the owner) into
 *      a newly allocated, NUL-terminated buffer.  Picks the best
 *      available UTF-8 mime type, requests it via
 *      wl_data_offer_receive through a non-blocking pipe, then pumps
 *      and polls the pipe in short slices so a misbehaving owner cannot
 *      freeze Tk.  Returns NULL if no offer exists or no suitable mime
 *      type is offered.
 *
 * Results:
 *      Returns a ckalloc'd buffer and stores its length in *lenPtr,
 *      or returns NULL with *lenPtr == 0.
 *
 * Side effects:
 *      Creates a pipe, issues wl_data_offer_receive, pumps Wayland
 *      events and polls the pipe (bounded), closes the pipe.
 *
 *----------------------------------------------------------------------
 */

static char *
WaylandReadOffer(size_t *lenPtr)
{
    int pipefd[2];
    const char *mime = NULL;
    int i;
    char *buf = NULL;
    size_t cap = 0, len = 0;

    *lenPtr = 0;
    if (!WaylandEnsureInitialized()) {
	DEBUG_LOG("WaylandReadOffer: clipboard not ready");
	return NULL;
    }

    /* Make sure a selection/offer already received is the one we read. */
    WaylandDispatch();
    if (clip.offer == NULL) {
	DEBUG_LOG("WaylandReadOffer: no offer");
	return NULL;
    }
    DEBUG_LOG("WaylandReadOffer: %d mime(s) available", clip.numOfferMimes);
    for (i = 0; i < clip.numOfferMimes; i++) {
	if (strcmp(clip.offerMimes[i], "text/plain;charset=utf-8") == 0) {
	    mime = clip.offerMimes[i];
	    break;
	}
    }
    if (mime == NULL) {
	for (i = 0; i < clip.numOfferMimes; i++) {
	    if (strcmp(clip.offerMimes[i], "UTF8_STRING") == 0 ||
		strcmp(clip.offerMimes[i], "text/plain") == 0 ||
		strcmp(clip.offerMimes[i], "STRING") == 0) {
		mime = clip.offerMimes[i];
		break;
	    }
	}
    }
    if (mime == NULL) {
	DEBUG_LOG("WaylandReadOffer: no suitable UTF-8 mime");
	return NULL;
    }
    if (pipe(pipefd) != 0) {
	DEBUG_LOG("WaylandReadOffer: pipe() failed: %s", strerror(errno));
	return NULL;
    }
    {
	int i;
	for (i = 0; i < 2; i++) {
	    int flags = fcntl(pipefd[i], F_GETFD);
	    if (flags >= 0) {
		fcntl(pipefd[i], F_SETFD, flags | FD_CLOEXEC);
	    }
	    flags = fcntl(pipefd[i], F_GETFL);
	    if (flags >= 0) {
		fcntl(pipefd[i], F_SETFL, flags | O_NONBLOCK);
	    }
	}
    }
    DEBUG_LOG("WaylandReadOffer: requesting mime=%s via fd=%d",
	      mime, pipefd[1]);
    wl_data_offer_receive(clip.offer, mime, pipefd[1]);
    close(pipefd[1]);
    wl_display_flush(clip.display);

    {
	/*
	 * Wait for data, bounded.  The display fd is polled too and
	 * events are read with the proper prepare_read protocol: when
	 * the current owner is our own source, the compositor delivers
	 * the "send" request over the display connection, and nothing
	 * would ever arrive on the pipe if we only polled the pipe.
	 * The deadline is pushed back whenever data arrives.
	 */
	int displayFd = wl_display_get_fd(clip.display);
	long deadline = MonotonicMs() + 500;

	for (;;) {
	    struct pollfd pfds[2];
	    int rc;

	    while (wl_display_prepare_read_queue(clip.display,
						 clip.queue) != 0) {
		wl_display_dispatch_queue_pending(clip.display, clip.queue);
	    }
	    wl_display_flush(clip.display);

	    pfds[0].fd = pipefd[0];
	    pfds[0].events = POLLIN;
	    pfds[0].revents = 0;
	    pfds[1].fd = displayFd;
	    pfds[1].events = POLLIN;
	    pfds[1].revents = 0;
	    rc = poll(pfds, 2, 50);
	    if (rc > 0 && (pfds[1].revents & POLLIN)) {
		wl_display_read_events(clip.display);
	    } else {
		wl_display_cancel_read(clip.display);
	    }
	    wl_display_dispatch_queue_pending(clip.display, clip.queue);

	    if (rc < 0 && errno != EINTR) {
		break;
	    }
	    if (rc > 0 &&
		(pfds[0].revents & (POLLIN | POLLHUP | POLLERR))) {
		char tmp[4096];
		ssize_t n;
		for (;;) {
		    n = read(pipefd[0], tmp, sizeof(tmp));
		    if (n > 0) {
			if (len + (size_t)n + 1 > cap) {
			    cap = (len + (size_t)n + 1) * 2 + 256;
			    buf = (char *)ckrealloc(buf, cap);
			}
			memcpy(buf + len, tmp, (size_t)n);
			len += (size_t)n;
			deadline = MonotonicMs() + 500;
		    } else if (n == 0) {
			goto done;
		    } else {
			if (errno == EAGAIN || errno == EINTR) {
			    break;
			}
			goto done;
		    }
		}
	    }
	    if (MonotonicMs() >= deadline) {
		DEBUG_LOG("WaylandReadOffer: timed out");
		break;
	    }
	}
    }
done:
    close(pipefd[0]);

    /*
     * We may have read GLFW's events off the socket while waiting for
     * the clipboard data.  They now sit, undispatched, in the default
     * queue, and the socket will not look readable again.  Make the next
     * pass through Tcl's event loop run without blocking so whoever
     * services GLFW gets to them.  Nothing is dispatched from here.
     */
    if (wl_display_prepare_read(clip.display) != 0) {
	clip.kickLoop = 1;
    } else {
	wl_display_cancel_read(clip.display);
    }
    DEBUG_LOG("WaylandReadOffer: read %zu bytes", len);
    if (buf != NULL) {
	buf[len] = '\0';
    }
    *lenPtr = len;
    return buf;
}

/*
 *  Helpers: pull Tk's own clipboard content for publish.
 */

/*
 *----------------------------------------------------------------------
 *
 * CollectClipboardText --
 *
 *      Assemble the current clipboard text from Tk's own selection
 *      handlers.  Tk stores clipboard append data as a linked list of
 *      targets on TkDisplay->clipTargetPtr, each holding a chain of
 *      buffers.  Concatenate every buffer of the first UTF-8/STRING
 *      target.
 *
 * Results:
 *      Returns TCL_OK and stores a ckalloc'd, NUL-terminated buffer in
 *      *outText with its length in *outLen; returns TCL_ERROR
 *      otherwise.
 *
 * Side effects:
 *      Allocates via ckalloc.
 *
 *----------------------------------------------------------------------
 */

static int
CollectClipboardText(TkWindow *winPtr,
		     TCL_UNUSED(Tcl_Interp *),
		     char **outText,
		     size_t *outLen)
{
    TkClipboardTarget *targetPtr;
    TkClipboardBuffer *cbPtr;
    char *buf = NULL;
    size_t cap = 0, len = 0;

    *outText = NULL;
    *outLen = 0;
    if (winPtr == NULL || winPtr->dispPtr == NULL) {
	DEBUG_LOG("CollectClipboardText: no window/display");
	return TCL_ERROR;
    }
    DEBUG_LOG("CollectClipboardText: scanning targets for %s",
	      Tk_PathName(winPtr));
    for (targetPtr = winPtr->dispPtr->clipTargetPtr; targetPtr != NULL;
	 targetPtr = targetPtr->nextPtr) {
	const char *name = Tk_GetAtomName((Tk_Window)winPtr, targetPtr->type);
	if (name == NULL) {
	    continue;
	}
	DEBUG_LOG("CollectClipboardText: considering target %s", name);
	if (strcmp(name, "UTF8_STRING") != 0 &&
	    strcmp(name, "STRING") != 0 &&
	    strcmp(name, "text/plain") != 0 &&
	    strcmp(name, "text/plain;charset=utf-8") != 0) {
	    continue;
	}
	for (cbPtr = targetPtr->firstBufferPtr; cbPtr != NULL;
	     cbPtr = cbPtr->nextPtr) {
	    if (cbPtr->buffer == NULL || cbPtr->length <= 0) {
		continue;
	    }
	    if (len + (size_t)cbPtr->length + 1 > cap) {
		cap = (len + (size_t)cbPtr->length + 1) * 2 + 256;
		buf = (char *)ckrealloc(buf, cap);
	    }
	    memcpy(buf + len, cbPtr->buffer, (size_t)cbPtr->length);
	    len += (size_t)cbPtr->length;
	}
	break;
    }

    if (buf == NULL) {
	DEBUG_LOG("CollectClipboardText: no usable target produced data");
	return TCL_ERROR;
    }
    DEBUG_LOG("CollectClipboardText: assembled %zu bytes", len);
    buf[len] = '\0';
    *outText = buf;
    *outLen = len;
    return TCL_OK;
}

/*
 *  Tk-required entry points.
 */

/*
 *----------------------------------------------------------------------
 *
 * TkSelGetSelection --
 *
 *      Retrieve the CLIPBOARD selection from whichever Wayland client
 *      owns it.  Reads from the current wl_data_offer, which the
 *      compositor routes either to another client's source or back to
 *      our own.  We deliberately do NOT return clip.text directly, even
 *      when clip.source is installed: set_selection() may have been
 *      silently rejected, in which case our own copy is stale.
 *
 *      Generic Tk (e.g. [clipboard get], [selection get]) collects the
 *      result through the Tk_GetSelProc callback, so the text is
 *      delivered via proc(); setting only the interpreter result would
 *      be overwritten by the caller's empty accumulator.
 *
 * Results:
 *      The result of proc (TCL_OK normally), or TCL_ERROR with an error
 *      message if no selection exists.
 *
 * Side effects:
 *      May pump the Wayland connection briefly (bounded poll).
 *
 *----------------------------------------------------------------------
 */

int
TkSelGetSelection(
		  Tcl_Interp *interp,
		  Tk_Window tkwin,
		  Atom selection,
		  Atom target,
		  Tk_GetSelProc *proc,
		  void *clientData)
{
    const char *selName = Tk_GetAtomName(tkwin, selection);
    const char *targetName = Tk_GetAtomName(tkwin, target);
    char *text = NULL;
    size_t len = 0;
    int result;

    DEBUG_LOG("TkSelGetSelection: selection=%s target=%s",
	      selName ? selName : "?", targetName ? targetName : "?");

    if (selName == NULL || strcmp(selName, "CLIPBOARD") != 0) {
	goto error;
    }
    if (targetName != NULL &&
	strcmp(targetName, "UTF8_STRING") != 0 &&
	strcmp(targetName, "STRING") != 0 &&
	strcmp(targetName, "text/plain") != 0 &&
	strcmp(targetName, "text/plain;charset=utf-8") != 0 &&
	strcmp(targetName, "TARGETS") != 0) {
	goto error;
    }
    if (targetName != NULL && strcmp(targetName, "TARGETS") == 0) {
	static const char targets[] =
	    "UTF8_STRING STRING text/plain text/plain;charset=utf-8";
	if (proc != NULL) {
	    return proc(clientData, interp, targets);
	}
	Tcl_SetObjResult(interp, Tcl_NewStringObj(targets, -1));
	return TCL_OK;
    }

    text = WaylandReadOffer(&len);
    if (text == NULL) {
	goto error;
    }
    if (proc != NULL) {
	result = proc(clientData, interp, text);
    } else {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(text, (Tcl_Size)len));
	result = TCL_OK;
    }
    ckfree(text);
    return result;

 error:
    DEBUG_LOG("TkSelGetSelection: no selection or unsupported target");
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		     "%s selection doesn't exist or form \"%s\" not defined",
		     selName ? selName : "?", targetName ? targetName : "?"));
    Tcl_SetErrorCode(interp, "TK", "SELECTION", "EXISTS", (char *)NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetSelectionOwner --
 *
 *      Stub kept for Tk's generic code.  Real ownership is via
 *      wl_data_device_set_selection() in WaylandPublish.
 *
 * Results:
 *      Always Success.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
XSetSelectionOwner(TCL_UNUSED(Display *), 
		   TCL_UNUSED(Atom),
		   TCL_UNUSED(Window), 
		   TCL_UNUSED(Time))
{
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelEventProc --
 *
 *      Handle SelectionClear events (ownership lost).  Forwards the
 *      event to TkSelClearSelection so Tk's generic bookkeeping stays
 *      consistent.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      For SelectionClear, notifies Tk via TkSelClearSelection.
 *
 *----------------------------------------------------------------------
 */

void
TkSelEventProc(Tk_Window tkwin, XEvent *eventPtr)
{
    DEBUG_LOG("TkSelEventProc: type=%d", eventPtr->type);
    if (eventPtr->type == SelectionClear) {
	TkSelClearSelection(tkwin, eventPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelPropProc --
 *
 *      Stub -- not needed with GLFW/Wayland backend.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
TkSelPropProc(TCL_UNUSED(XEvent *))
{
    /* No-op */
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelUpdateClipboard --
 *
 *      Called by generic Tk after [clipboard append] or
 *      [clipboard clear].  Publish our assembled clipboard text as a
 *      wl_data_source, or clear the Wayland selection if there are no
 *      clipboard targets left.
 *
 *      This is the ONLY place that talks to wl_data_device for writes.
 *      It never blocks.  If asynchronous initialization has not yet
 *      completed, the publish is dropped; the next [clipboard append]
 *      retries once the pump has delivered the registry globals.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May publish or clear the Wayland selection.
 *
 *----------------------------------------------------------------------
 */

void
TkSelUpdateClipboard(TkWindow *winPtr, clipboardOption option)
{
    char *text = NULL;
    size_t len = 0;

    (void)option;

    DEBUG_LOG("TkSelUpdateClipboard: win=%s targets=%s",
	      winPtr ? Tk_PathName(winPtr) : "(null)",
	      (winPtr && winPtr->dispPtr && winPtr->dispPtr->clipTargetPtr)
	      ? "yes" : "none");

    if (winPtr == NULL) {
	return;
    }
    if (winPtr->dispPtr == NULL ||
	winPtr->dispPtr->clipTargetPtr == NULL) {
	WaylandClear();
	return;
    }
    if (CollectClipboardText(winPtr, NULL, &text, &len) != TCL_OK) {
	return;
    }
    DEBUG_LOG("TkSelUpdateClipboard: publishing %zu bytes", len);
    WaylandPublish(text, len);
    ckfree(text);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
