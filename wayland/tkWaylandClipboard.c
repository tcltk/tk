/*
 * tkWaylandClipboard.c -- 
 *
 *      Native Wayland clipboard for the Tk/GLFW backend.
 *
 *      Tk owns the Wayland selection itself via wl_data_device /
 *      wl_data_source / wl_data_offer, using the same wl_display and
 *      wl_surface GLFW already owns.  
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution.
 */

#include "tkInt.h"
#include "tkSelect.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    struct wl_pointer *pointer;
    struct wl_data_source *source;  /* Uur current offer, or NULL.*/
    struct wl_data_offer *offer;    /* Current incoming offer, or NULL. */
    char **offerMimes;              /* Strdup'd mime list for offer. */
    int numOfferMimes;
    uint32_t lastEnterSerial;       /* Last keyboard/pointer enter serial. */
    int haveSerial;
    int Initialized;
    int pumpInstalled;              /* Tcl file handler installed. */
    char *text;                     /* Owned copy, what we published. */
    size_t textLen;
    char *pendingText;              /* Queued publish, no serial yet. */
    size_t pendingLen;
    int pendingClear;               /* Queued clear, no serial yet. */
} WaylandClipboard;

static WaylandClipboard clip = {0};

/* Forward declarations. */
static void WaylandPublish(const char *text, size_t len);
static void WaylandClear(void);
static void WaylandPump(ClientData clientData, int mask);

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
     * Accept the UTF-8 family; everything else gets an empty reply
     * (close without writing) so the paster does not hang.
     */
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
                break;
            }
            if (n == 0) {
                break;
            }
            off += (size_t)n;
        }
    }
    close(fd);
}

/*
 *----------------------------------------------------------------------
 *
 * SourceCancelled --
 *
 *      wl_data_source "cancelled" handler.  Fires when another client
 *      has taken the selection, or when we cleared it ourselves.  We
 *      drop the source pointer and destroy the object.  c->text is
 *      deliberately retained so a subsequent re-publish (or a get()
 *      that raced the cancel) still has data; it is freed by the next
 *      WaylandPublish/WaylandClear.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Destroys the wl_data_source and clears c->source.
 *
 *----------------------------------------------------------------------
 */

static void
SourceCancelled(void *data, struct wl_data_source *source)
{
    WaylandClipboard *c = (WaylandClipboard *)data;
    /* Someone else took the selection, or we cleared it. */
    if (c->source == source) {
        c->source = NULL;
    }
    wl_data_source_destroy(source);
    /*
     * Tell generic Tk the selection is gone. 
     * TkSelClearSelection is driven via SelectionClear in TkSelEventProc
     * on X11; on Wayland we have no XEvent, so free our copy here and
     * let the next publish replace it. Keep c->text until replaced so
     * a re-publish after a spurious cancel still works.
     */
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
 *      introduced.  We discard the previous offer's mime list, install
 *      the new offer, and attach the offer listener so its mime types
 *      are recorded as they arrive.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Frees old mime strings, replaces c->offer, registers listener.
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
    /* Previous offer is superseded. */
    for (i = 0; i < c->numOfferMimes; i++) {
        ckfree(c->offerMimes[i]);
    }
    ckfree((char *)c->offerMimes);
    c->offerMimes = NULL;
    c->numOfferMimes = 0;
    c->offer = offer;
    wl_data_offer_add_listener(offer, &offerListener, c);
}

/*
 *----------------------------------------------------------------------
 *
 * DeviceEnter --
 *
 *      wl_data_device "enter" handler (drag-and-drop).  Unused by the
 *      clipboard path; all arguments are ignored.
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
DeviceEnter(TCL_UNUSED(void *), /* data */
	    TCL_UNUSED(struct wl_data_device *), /* device */
	    TCL_UNUSED(uint32_t), /* serial */
	    TCL_UNUSED(struct wl_surface * ), /* surface */
	    TCL_UNUSED(wl_fixed_t), /* x */
	    TCL_UNUSED(wl_fixed_t), /* y */
	    TCL_UNUSED(struct wl_data_offer *)) /* offer */
{
    /* Drag-and-drop enter: not used by clipboard. */
}

/*
 *----------------------------------------------------------------------
 *
 * DeviceLeave --
 *
 *      wl_data_device "leave" handler (drag-and-drop).  Unused by the
 *      clipboard path.
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
DeviceLeave(
	    TCL_UNUSED(void *), /*data */
	    TCL_UNUSED(struct wl_data_device * )) /* device */
{
    /* No-op .*/
}

/*
 *----------------------------------------------------------------------
 *
 * DeviceMotion --
 *
 *      wl_data_device "motion" handler (drag-and-drop).  Unused by the
 *      clipboard path.
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
DeviceMotion(TCL_UNUSED(void *), /* data */
	     TCL_UNUSED(struct wl_data_device *), /* device */
	     TCL_UNUSED(uint32_t), /* time */
	     TCL_UNUSED(wl_fixed_t), /* x */
	     TCL_UNUSED(wl_fixed_t)) /* y */
{
    /* No-op. */
}

/*
 *----------------------------------------------------------------------
 *
 * DeviceDrop --
 *
 *      wl_data_device "drop" handler (drag-and-drop).  Unused by the
 *      clipboard path.
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
DeviceDrop(TCL_UNUSED(void *),
	   TCL_UNUSED(struct wl_data_device * ))
{
    /* No-op. */
}

/*
 *----------------------------------------------------------------------
 *
 * DeviceSelection --
 *
 *      wl_data_device "selection" handler.  Announces the current
 *      selection owner's offer (or NULL when the clipboard is empty).
 *      On a NULL offer the stale mime list is freed so a later get()
 *      correctly reports "no selection".
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May free c->offerMimes; updates c->offer.
 *
 *----------------------------------------------------------------------
 */

static void
DeviceSelection(void *data, struct wl_data_device *device,
		struct wl_data_offer *offer)
{
    WaylandClipboard *c = (WaylandClipboard *)data;
    (void)device;
    /* offer == NULL means clipboard is now empty. */
    c->offer = offer;
    if (offer == NULL) {
        int i;
        for (i = 0; i < c->numOfferMimes; i++) {
            ckfree(c->offerMimes[i]);
        }
        ckfree((char *)c->offerMimes);
        c->offerMimes = NULL;
        c->numOfferMimes = 0;
    }
}

static const struct wl_data_device_listener deviceListener = {
    .data_offer = DeviceDataOffer,
    .enter = DeviceEnter,
    .leave = DeviceLeave,
    .motion = DeviceMotion,
    .drop = DeviceDrop,
    .selection = DeviceSelection,
};

/* 
 *  Keyboard / pointer: serial tracking.
 */

/*
 *----------------------------------------------------------------------
 *
 * KeyboardKeymap --
 *
 *      wl_keyboard "keymap" handler.  We do not interpret keymaps in
 *      this backend, so the received fd is simply closed.
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
 *      is the only serial GNOME will accept for set_selection().  If a
 *      publish or clear was queued waiting for focus, it is flushed now.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates lastEnterSerial/haveSerial; may publish or clear the
 *      clipboard and free the pending buffer.
 *
 *----------------------------------------------------------------------
 */

static void
KeyboardEnter(void *data,
	      TCL_UNUSED(struct wl_keyboard *), /* kbd */
	      uint32_t serial,
	      TCL_UNUSED(struct wl_surface *), /* surface */
	      TCL_UNUSED(struct wl_array * )) /* keys */
{
    WaylandClipboard *c = (WaylandClipboard *)data;
    c->lastEnterSerial = serial;
    c->haveSerial = 1;
    /* Flush any publish/clear that was waiting for focus. */
    if (c->pendingText != NULL) {
        char *t = c->pendingText;
        size_t len = c->pendingLen;
        c->pendingText = NULL;
        c->pendingLen = 0;
        WaylandPublish(t, len);
        ckfree(t);
    } else if (c->pendingClear) {
        c->pendingClear = 0;
        WaylandClear();
    }
}

/*
 *----------------------------------------------------------------------
 *
 * KeyboardLeave --
 *
 *      wl_keyboard "leave" handler.  The last enter serial is
 *      deliberately retained: GNOME in practice still accepts the most
 *      recent enter serial for set_selection() after leave.
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
KeyboardLeave(TCL_UNUSED(void *), /* data */
	      TCL_UNUSED(struct wl_keyboard *), /* kbd */
	      TCL_UNUSED(uint32_t), /* serial */
	      TCL_UNUSED(struct wl_surface *)) /* surface */
{
    /*
     * Keep lastEnterSerial: GNOME accepts the most recent enter serial
     * even after leave for set_selection in practice; do not clear.
     */
}

/*
 *----------------------------------------------------------------------
 *
 * KeyboardKey --
 *
 *      wl_keyboard "key" handler.  Unused by the clipboard path.
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
KeyboardKey(TCL_UNUSED(void *), /* data */
	    TCL_UNUSED(struct wl_keyboard *), /*kbd */
	    TCL_UNUSED(uint32_t),  /* serial */
	    TCL_UNUSED(uint32_t),  /* time */
	    TCL_UNUSED(uint32_t), /* key */
	    TCL_UNUSED(uint32_t)) /* state */
{
    /* No-op. */
}

/*
 *----------------------------------------------------------------------
 *
 * KeyboardModifiers --
 *
 *      wl_keyboard "modifiers" handler.  Unused by the clipboard path.
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
KeyboardModifiers(TCL_UNUSED(void *), /* data */
		  TCL_UNUSED(struct wl_keyboard *), /*kbd */
		  TCL_UNUSED(uint32_t),  /* serial */
		  TCL_UNUSED(uint32_t),  /* depressed */
		  TCL_UNUSED(uint32_t), /* latched */
		  TCL_UNUSED(uint32_t), /* locked */
		  TCL_UNUSED(uint32_t)) /* group */
		  
{
    /* No-op. */
}

/*
 *----------------------------------------------------------------------
 *
 * KeyboardRepeatInfo --
 *
 *      wl_keyboard "repeat_info" handler.  Unused by the clipboard path.
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
KeyboardRepeatInfo(TCL_UNUSED(void *), /* data */
		   TCL_UNUSED(struct wl_keyboard *), /*kbd */
		   TCL_UNUSED(int32_t),  /* rate */
		   TCL_UNUSED(int32_t))/* delay */
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
 *----------------------------------------------------------------------
 *
 * PointerEnter --
 *
 *      wl_pointer "enter" handler.  Captures a pointer enter serial
 *      as a fallback for set_selection() when no keyboard enter serial
 *      has been seen yet.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May set lastEnterSerial/haveSerial.
 *
 *----------------------------------------------------------------------
 */

static void
PointerEnter(void *data,
	     TCL_UNUSED(struct wl_pointer *), /* pointer */
	     uint32_t serial,
	     TCL_UNUSED(struct wl_surface *), /* surface */
	     TCL_UNUSED(wl_fixed_t), /* sx */
	     TCL_UNUSED(wl_fixed_t)) /* sy */
{
    WaylandClipboard *c = (WaylandClipboard *)data;
    if (!c->haveSerial) {
        c->lastEnterSerial = serial;
        c->haveSerial = 1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PointerLeave --
 *
 *      wl_pointer "leave" handler.  Unused by the clipboard path.
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
PointerLeave(TCL_UNUSED(void *), /* data */
	     TCL_UNUSED(struct wl_pointer *), /* pointer */
	     TCL_UNUSED(uint32_t), /* serial */
	     TCL_UNUSED(struct wl_surface *)) /* surface */
{
    /* No-op. */
}

/*
 *----------------------------------------------------------------------
 *
 * PointerMotion --
 *
 *      wl_pointer "motion" handler.  Unused by the clipboard path.
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
PointerMotion(TCL_UNUSED(void *), /* data */
	      TCL_UNUSED(struct wl_pointer *), /* pointer */
	      TCL_UNUSED(uint32_t), /* time */
	      TCL_UNUSED(wl_fixed_t), /* sx */
	      TCL_UNUSED(wl_fixed_t)) /* sy */
{
    /* No-op. */
}

/*
 *----------------------------------------------------------------------
 *
 * PointerButton --
 *
 *      wl_pointer "button" handler.  Unused by the clipboard path.
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
PointerButton(TCL_UNUSED(void *), /* data */
	      TCL_UNUSED(struct wl_pointer *), /* pointer */
	      TCL_UNUSED(uint32_t), /* serial */
	      TCL_UNUSED(uint32_t), /* time */
	      TCL_UNUSED(uint32_t), /* button */
	      TCL_UNUSED(uint32_t)) /* state */
{
    /* No-op. */
}

/*
 *----------------------------------------------------------------------
 *
 * PointerAxis --
 *
 *      wl_pointer "axis" handler.  Unused by the clipboard path.
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
PointerAxis(TCL_UNUSED(void *), /* data */
	    TCL_UNUSED(struct wl_pointer *), /* pointer */
	    TCL_UNUSED(uint32_t), /* time */
	    TCL_UNUSED(uint32_t), /* axis */
	    TCL_UNUSED( wl_fixed_t)) /* value */
{
    /* No-op. */
}

static const struct wl_pointer_listener pointerListener = {
    .enter = PointerEnter,
    .leave = PointerLeave,
    .motion = PointerMotion,
    .button = PointerButton,
    .axis = PointerAxis,
    .frame = NULL,
    .axis_source = NULL,
    .axis_stop = NULL,
    .axis_discrete = NULL,
    .axis_value120 = NULL,
    .axis_relative_direction = NULL,
};

/* 
 *  Seat + registry. 
 */

/*
 *----------------------------------------------------------------------
 *
 * SeatCapabilities --
 *
 *      wl_seat "capabilities" handler.  Binds our own wl_keyboard and
 *      wl_pointer (for serial tracking) when the seat supports them,
 *      and creates the wl_data_device once both the manager and the
 *      seat are known.  Idempotent: existing objects are not recreated.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May create keyboard/pointer/device objects and add listeners.
 *
 *----------------------------------------------------------------------
 */

static void
SeatCapabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    WaylandClipboard *c = (WaylandClipboard *)data;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && c->keyboard == NULL) {
        c->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(c->keyboard, &keyboardListener, c);
    }
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && c->pointer == NULL) {
        c->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(c->pointer, &pointerListener, c);
    }
    if (c->device == NULL && c->manager != NULL) {
        c->device = wl_data_device_manager_get_data_device(c->manager, seat);
        wl_data_device_add_listener(c->device, &deviceListener, c);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SeatName --
 *
 *      wl_seat "name" handler.  Unused by the clipboard path.
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
SeatName(void *data, struct wl_seat *seat, const char *name)
{
    (void)data; (void)seat; (void)name;
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
 *      wl_registry "global" handler.  Binds the interfaces the
 *      clipboard needs: wl_data_device_manager (v3) and wl_seat (v7,
 *      capped to the advertised version).  A seat listener is attached
 *      so SeatCapabilities can create the data device.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Binds globals; may replace c->manager / c->seat.
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
        c->manager = wl_registry_bind(registry, name,
				      &wl_data_device_manager_interface, v);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        uint32_t v = version < 7 ? version : 7;
        c->seat = wl_registry_bind(registry, name,
				   &wl_seat_interface, v);
        wl_seat_add_listener(c->seat, &seatListener, c);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * RegistryGlobalRemove --
 *
 *      wl_registry "global_remove" handler.  Unused; we do not track
 *      global lifetimes past binding.
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
RegistryGlobalRemove(TCL_UNUSED(void *), /* data */
		     TCL_UNUSED(struct wl_registry *), /* registry */
		     TCL_UNUSED(uint32_t)) /* name */
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
 *      Lazily initialize the Wayland clipboard state: grab the
 *      wl_display GLFW already owns (never wl_display_connect), bind
 *      our own registry objects via one roundtrip, create the data
 *      device, and install a Tcl file handler on the display fd so
 *      events are pumped by Tk's event loop.  This roundtrip is the
 *      only blocking call in the clipboard path, and it happens once,
 *      on first use.
 *
 * Results:
 *      Returns 1 if a display is available (possibly already
 *      Initialized), 0 if GLFW has no Wayland display.
 *
 * Side effects:
 *      Creates registry, binds globals, creates data device, registers
 *      Tcl file handler.  Sets clip.Initialized.
 *
 *----------------------------------------------------------------------
 */

static int
WaylandEnsureInitialized(void)
{
    if (clip.Initialized) {
	return clip.display != NULL;
    }
    clip.Initialized = 1;

    clip.display = glfwGetWaylandDisplay();
    if (clip.display == NULL) {
	return 0;
    }
    clip.registry = wl_display_get_registry(clip.display);
    wl_registry_add_listener(clip.registry, &registryListener, &clip);

    /*
     * One roundtrip here is safe: it happens once, on first clipboard
     * use, not on every append/clear. It lets the registry deliver
     * manager + seat so device can be created. Do NOT roundtrip per
     * operation. After this, events flow via the pump below.
     */
    wl_display_roundtrip(clip.display);

    if (clip.manager != NULL && clip.seat != NULL && clip.device == NULL) {
	clip.device = wl_data_device_manager_get_data_device(
							     clip.manager, clip.seat);
	wl_data_device_add_listener(clip.device, &deviceListener, &clip);
    }

    /* Pump Wayland fd through Tcl's event loop. */
    if (!clip.pumpInstalled) {
	int fd = wl_display_get_fd(clip.display);
	if (fd >= 0) {
	    Tcl_CreateFileHandler(fd, TCL_READABLE, WaylandPump, NULL);
	    clip.pumpInstalled = 1;
	}
    }
    return clip.display != NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * WaylandPump --
 *
 *      Drain pending Wayland events from the display fd.  Called by
 *      Tcl's file handler (or directly by TkWaylandClipboardPump) when
 *      the display fd is readable.  Uses prepare_read/read_events so it
 *      never blocks, then dispatches pending callbacks and flushes.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Delivers Wayland events (send, cancelled, selection, enter...)
 *      to our listeners and flushes outgoing requests.
 *
 *----------------------------------------------------------------------
 */

static void
WaylandPump(TCL_UNUSED(ClientData), /* clientData */
	    TCL_UNUSED(int)) /* mask */
{
    if (clip.display == NULL) {
	return;
    }
    /* Drain pending events; never blocks. */
    if (wl_display_prepare_read(clip.display) == 0) {
	wl_display_read_events(clip.display);
    }
    wl_display_dispatch_pending(clip.display);
    wl_display_flush(clip.display);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandClipboardPump --
 *
 *      Public pump entry point.  Call this from the Tk/GLFW main loop
 *      if Tcl file handlers are not running (e.g. a pure glfwPollEvents
 *      loop).  Safe to call often; never blocks.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Same as WaylandPump.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandClipboardPump(void)
{
    WaylandPump(NULL, 0);
}

/* 
 *  Publish / clear.
 */


/*
 *----------------------------------------------------------------------
 *
 * WaylandPublish --
 *
 *      Publish text as the Wayland selection.  If no enter serial has
 *      been seen yet, the text is queued (pendingText) and published on
 *      the next KeyboardEnter.  Otherwise a fresh wl_data_source is
 *      created, the UTF-8 mime family offered, and set_selection() is
 *      issued.  Any previous source is destroyed first.  Never blocks.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Replaces clip.text, creates/destroys wl_data_source objects,
 *      issues set_selection and flushes the display.
 *
 *----------------------------------------------------------------------
 */

static void
WaylandPublish(const char *text, size_t len)
{
    if (!WaylandEnsureInitialized()) {
	return;
    }
    if (clip.manager == NULL || clip.device == NULL) {
	return;
    }
    if (!clip.haveSerial) {
	/* No focus yet: queue until KeyboardEnter. */
	ckfree(clip.pendingText);
	clip.pendingText = (char *)ckalloc(len + 1);
	memcpy(clip.pendingText, text, len);
	clip.pendingText[len] = '\0';
	clip.pendingLen = len;
	clip.pendingClear = 0;
	return;
    }

    /* Replace any previous source; its cancelled() will fire. */
    if (clip.source != NULL) {
	struct wl_data_source *old = clip.source;
	clip.source = NULL;
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

    wl_data_device_set_selection(clip.device, clip.source,
				 clip.lastEnterSerial);
    wl_display_flush(clip.display);
}

/*
 *----------------------------------------------------------------------
 *
 * WaylandClear --
 *
 *      Clear the Wayland selection.  If no enter serial is available
 *      the clear is queued (pendingClear) until the next KeyboardEnter.
 *      Otherwise set_selection(NULL) is issued, which causes the
 *      compositor to fire cancelled() on our source (cleaning it up);
 *      our in-memory text is freed here.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Issues set_selection, frees clip.text, flushes the display.
 *
 *----------------------------------------------------------------------
 */

static void
WaylandClear(void)
{
    if (!WaylandEnsureInitialized()) {
	return;
    }
    if (clip.device == NULL) {
	return;
    }
    if (!clip.haveSerial) {
	clip.pendingClear = 1;
	ckfree(clip.pendingText);
	clip.pendingText = NULL;
	clip.pendingLen = 0;
	return;
    }
    wl_data_device_set_selection(clip.device, NULL, clip.lastEnterSerial);
    wl_display_flush(clip.display);
    /* cancelled() cleans up clip.source. */
    ckfree(clip.text);
    clip.text = NULL;
    clip.textLen = 0;
}

/*
 *  Read incoming offer (clipboard get).
 */

/*
 *----------------------------------------------------------------------
 *
 * WaylandReadOffer --
 *
 *      Read the current incoming offer (owned by another application)
 *      into a newly allocated, NUL-terminated buffer.  Picks the best
 *      available UTF-8 mime type, requests it via wl_data_offer_receive
 *      through a non-blocking pipe, then pumps and polls the pipe in
 *      short slices so a misbehaving owner cannot freeze Tk.  Returns
 *      NULL if no offer exists or no suitable mime type is offered.
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
	return NULL;
    }
    if (clip.offer == NULL) {
	return NULL;
    }
    /* Prefer UTF-8 mime. */
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
	return NULL;
    }
    if (pipe(pipefd) != 0) {
	return NULL;
    }
    /* Set CLOEXEC | NONBLOCK on both ends via fcntl(). */
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
    wl_data_offer_receive(clip.offer, mime, pipefd[1]);
    close(pipefd[1]);
    wl_display_flush(clip.display);

    /*
     * Pump + poll: owner writes, then closes. 500ms cap so a
     * misbehaving owner cannot freeze Tk.
     */
    {
	int slices = 0;
	for (;;) {
	    struct pollfd pfd;
	    pfd.fd = pipefd[0];
	    pfd.events = POLLIN;
	    pfd.revents = 0;
	    /* Let Wayland deliver send() completion while we wait. */
	    wl_display_dispatch_pending(clip.display);
	    if (poll(&pfd, 1, 50) == 0) {
		/* Timeout slice: keep looping up to ~500ms total. */
		/* slices is local: reset on every call. */
		if (++slices >= 10) {
		    break;
		}
		continue;
	    }
	    if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
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
		    } else if (n == 0) {
			goto done;
		    } else {
			if (errno == EAGAIN || errno == EINTR) {
			    break;
			}
			goto done;
		    }
		}
		if (pfd.revents & (POLLHUP | POLLERR)) {
		    /* Drain once more, then finish. */
		    continue;
		}
	    } else {
		break;
	    }
	}
    done:
	close(pipefd[0]);
	if (buf != NULL) {
	    buf[len] = '\0';
	}
	*lenPtr = len;
	return buf;
    }
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
 *      handlers.  Tk stores clipboard append data as selection handlers
 *      on TkDisplay->clipTargetPtr; this routine walks them, finds the first
 *      UTF-8/STRING target, and pulls its data in chunks via the
 *      buffer chain of the first usable target.  Used by
 *      TkSelUpdateClipboard to obtain the text to hand to
 *      WaylandPublish.
 *
 * Results:
 *      Returns TCL_OK and stores a ckalloc'd, NUL-terminated buffer in
 *      *outText with its length in *outLen if any usable target yields
 *      data; returns TCL_ERROR otherwise (with *outText == NULL).
 *
 * Side effects:
 *      Allocates via ckalloc; calls the target's proc callback.
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
	return TCL_ERROR;
    }
    /*
     * Tk stores clipboard appends as a linked list of targets
     * (TkDisplay->clipTargetPtr, via TkClipboardTarget.nextPtr),
     * each holding a chain of buffers (firstBufferPtr via
     * TkClipboardBuffer.nextPtr). There is no proc/clientData:
     * the data is already in the buffers. Concatenate every
     * buffer of the first UTF-8/STRING target.
     */
    for (targetPtr = winPtr->dispPtr->clipTargetPtr; targetPtr != NULL;
	 targetPtr = targetPtr->nextPtr) {
	const char *name = Tk_GetAtomName((Tk_Window)winPtr, targetPtr->type);
	if (name == NULL) {
	    continue;
	}
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
	break; /* first usable target wins */
    }

    if (buf == NULL) {
	return TCL_ERROR;
    }
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
 *      Retrieve the CLIPBOARD selection. On Wayland this means reading
 *      from the current wl_data_offer (owned by another app) if any,
 *      or from our own published text if we are the owner.
 *
 * Results:
 *      TCL_OK with the selection text as the interp result, or
 *      TCL_ERROR if no selection exists.
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
		  TCL_UNUSED(Tk_GetSelProc *),
		  TCL_UNUSED(void *))
{
    const char *selName = Tk_GetAtomName(tkwin, selection);
    const char *targetName = Tk_GetAtomName(tkwin, target);
    char *text = NULL;
    size_t len = 0;

    (void)tkwin;

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
	Tcl_SetObjResult(interp, Tcl_NewStringObj(
						  "UTF8_STRING STRING text/plain text/plain;charset=utf-8", -1));
	return TCL_OK;
    }

    /* If we own it, return our copy directly -- no round-trip. */
    if (clip.source != NULL && clip.text != NULL) {
	Tcl_SetObjResult(interp, Tcl_NewStringObj(clip.text, (int)clip.textLen));
	return TCL_OK;
    }

    text = WaylandReadOffer(&len);
    if (text == NULL) {
	goto error;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(text, (int)len));
    ckfree(text);
    return TCL_OK;

 error:
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
					   "%s selection doesn't exist or form \"%s\" not defined",
					   selName ? selName : "?",
					   targetName ? targetName : "?"));
    Tcl_SetErrorCode(interp, "TK", "SELECTION", "EXISTS", (char *)NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetSelectionOwner --
 *
 *      Stub kept for Tk's generic code. Real ownership is via
 *      wl_data_device_set_selection() in WaylandPublish/WaylandClear.
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
XSetSelectionOwner(
		   TCL_UNUSED(Display *),
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
 *      consistent when a SelectionClear reaches us.
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
TkSelEventProc(
	       Tk_Window tkwin,
	       XEvent *eventPtr)
{
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
TkSelPropProc(
	      TCL_UNUSED(XEvent *))
{
    /* No-op */
}

/*
 *----------------------------------------------------------------------
 *
 * TkSelUpdateClipboard --
 *
 *      Called by generic Tk after [clipboard append] or
 *      [clipboard clear]. Publish our assembled clipboard text as a
 *      wl_data_source, or clear the Wayland selection if there are no
 *      clipboard targets left.
 *
 *      This is the ONLY place that talks to wl_data_device for writes.
 *      It never blocks.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May publish or clear the Wayland selection; may allocate/free
 *      clipboard text via CollectClipboardText.
 *
 *----------------------------------------------------------------------
 */

void
TkSelUpdateClipboard(
		     TkWindow *winPtr,
		     clipboardOption option)
{
    char *text = NULL;
    size_t len = 0;

    (void)option;

    if (winPtr == NULL) {
	return;
    }
    /* clipboard clear -> no targets remain. */
    if (winPtr->dispPtr == NULL ||
	winPtr->dispPtr->clipTargetPtr == NULL) {
	WaylandClear();
	return;
    }
    if (CollectClipboardText(winPtr, NULL, &text, &len) != TCL_OK) {
	return;
    }
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
