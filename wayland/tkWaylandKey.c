/*
 * tkWaylandKey.c --
 *
 *	This file contains functions for keyboard input handling on Wayland,
 *	including comprehensive IME (Input Method Editor) support for
 *	complex text input (Chinese, Japanese, Korean, etc.).
 *
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkUnixInt.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <wayland-client.h>
#include <string.h>
#include <ctype.h>
#include <sys/mman.h>
#include <unistd.h>
#include "text-input-unstable-v3-client-protocol.h"



/*
 *----------------------------------------------------------------------
 *
 * Keyboard State Management
 *
 *----------------------------------------------------------------------
 */

/*
 * XKB keyboard state - for key translation.
 */
typedef struct {
    struct xkb_context *context;
    struct xkb_keymap *keymap;
    struct xkb_state *state;
    struct xkb_compose_table *compose_table;
    struct xkb_compose_state *compose_state;
} TkXKBState;

static TkXKBState xkbState = {NULL, NULL, NULL, NULL, NULL};

/*
 *----------------------------------------------------------------------
 *
 * IME (Input Method Editor) Support
 *
 *----------------------------------------------------------------------
 */

/*
 * IME state per Tk window.
 */
typedef struct TkIMEState {
    struct zwp_text_input_v3 *text_input;
    
    int enabled;
    int preedit_active;
    char *preedit_string;
    int preedit_cursor;
    char *commit_string;
    
    Tk_Window tkwin;
    
    int cursor_x;
    int cursor_y;
    int cursor_width;
    int cursor_height;
    
    struct TkIMEState *next;  /* For hash table collision chain */
} TkIMEState;

/*
 * Global Wayland connection for IME - separate from GLFW.
 */
typedef struct {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;
    struct zwp_text_input_manager_v3 *text_input_manager;
    
    int fd;
    Tcl_Channel channel;
    
    /* Hash table for IME state lookup by Tk_Window */
    TkIMEState **ime_hash;
    int hash_size;
} WaylandIMEConnection;

static WaylandIMEConnection wlIME = {NULL, NULL, NULL, NULL, -1, NULL, NULL, 0};
static TkIMEState *currentIME = NULL;

/* Hash table functions */
static unsigned int HashWindow(Tk_Window tkwin);
static TkIMEState *FindIMEState(Tk_Window tkwin);
static void StoreIMEState(Tk_Window tkwin, TkIMEState *ime);
static void RemoveIMEState(Tk_Window tkwin);

/*
 *----------------------------------------------------------------------
 *
 * Forward Declarations
 *
 *----------------------------------------------------------------------
 */

/* XKB functions. */
static int InitializeXKB(void);
static void CleanupXKB(void);
static void UpdateXKBModifiers(unsigned int mods_depressed,
    unsigned int mods_latched, unsigned int mods_locked,
    unsigned int group);
static KeySym XKBKeycodeToKeysym(unsigned int keycode);
static int XKBKeysymToString(KeySym keysym, char *buffer, int buflen);

/* IME functions. */
static int InitializeIME(void);
static void CleanupIME(void);
static void WaylandIMEEventHandler(ClientData clientData, int mask);
static TkIMEState *CreateIMEState(Tk_Window tkwin);
static void DestroyIMEState(TkIMEState *ime);
static void SendIMECommitEvent(TkIMEState *ime);

/* Wayland callbacks. */
static void RegistryHandleGlobal(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version);
static void RegistryHandleGlobalRemove(void *data,
    struct wl_registry *registry, uint32_t name);
static void SeatHandleCapabilities(void *data, struct wl_seat *seat,
    uint32_t capabilities);
static void SeatHandleName(void *data, struct wl_seat *seat,
    const char *name);
static void IMETextInputEnter(void *data,
    struct zwp_text_input_v3 *text_input, struct wl_surface *surface);
static void IMETextInputLeave(void *data,
    struct zwp_text_input_v3 *text_input, struct wl_surface *surface);
static void IMETextInputPreeditString(void *data,
    struct zwp_text_input_v3 *text_input, const char *text,
    int32_t cursor_begin, int32_t cursor_end);
static void IMETextInputCommitString(void *data,
    struct zwp_text_input_v3 *text_input, const char *text);
static void IMETextInputDeleteSurroundingText(void *data,
    struct zwp_text_input_v3 *text_input,
    uint32_t before_length, uint32_t after_length);
static void IMETextInputDone(void *data,
    struct zwp_text_input_v3 *text_input, uint32_t serial);

/*
 * Wayland protocol listeners.
 */
static const struct wl_registry_listener registry_listener = {
    .global = RegistryHandleGlobal,
    .global_remove = RegistryHandleGlobalRemove,
};

static const struct wl_seat_listener seat_listener = {
    .capabilities = SeatHandleCapabilities,
    .name = SeatHandleName,
};

static const struct zwp_text_input_v3_listener text_input_listener = {
    .enter = IMETextInputEnter,
    .leave = IMETextInputLeave,
    .preedit_string = IMETextInputPreeditString,
    .commit_string = IMETextInputCommitString,
    .delete_surrounding_text = IMETextInputDeleteSurroundingText,
    .done = IMETextInputDone,
};

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandKeyInit --
 *
 *	Initialize keyboard handling for Wayland, including XKB and IME.
 *
 * Results:
 *	Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *	Initializes XKB context and IME connection.
 *
 *----------------------------------------------------------------------
 */

int
TkWaylandKeyInit(void)
{
    if (!InitializeXKB()) {
        return 0;
    }
    
    /* Initialize IME (may fail if not on Wayland). */
    InitializeIME();
    
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandKeyCleanup --
 *
 *	Clean up keyboard and IME resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees XKB and IME resources.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandKeyCleanup(void)
{
    CleanupIME();
    CleanupXKB();
}

/*
 *----------------------------------------------------------------------
 *
 * XKB Keyboard Handling
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * InitializeXKB --
 *
 *	Initialize XKB context for key translation.
 *
 * Results:
 *	Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *	Creates XKB context and compose table.
 *
 *----------------------------------------------------------------------
 */

static int
InitializeXKB(void)
{
    const char *locale;
    
    /* Create XKB context. */
    xkbState.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkbState.context) {
        return 0;
    }
    
    /* Get locale for compose sequences. */
    locale = getenv("LC_ALL");
    if (!locale) {
        locale = getenv("LC_CTYPE");
    }
    if (!locale) {
        locale = getenv("LANG");
    }
    if (!locale) {
        locale = "C";
    }
    
    /* Create compose table. */
    xkbState.compose_table = xkb_compose_table_new_from_locale(
        xkbState.context, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
    
    if (xkbState.compose_table) {
        xkbState.compose_state = xkb_compose_state_new(
            xkbState.compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
    }
    
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * CleanupXKB --
 *
 *	Clean up XKB resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees XKB context and compose state.
 *
 *----------------------------------------------------------------------
 */

static void
CleanupXKB(void)
{
    if (xkbState.compose_state) {
        xkb_compose_state_unref(xkbState.compose_state);
        xkbState.compose_state = NULL;
    }
    
    if (xkbState.compose_table) {
        xkb_compose_table_unref(xkbState.compose_table);
        xkbState.compose_table = NULL;
    }
    
    if (xkbState.state) {
        xkb_state_unref(xkbState.state);
        xkbState.state = NULL;
    }
    
    if (xkbState.keymap) {
        xkb_keymap_unref(xkbState.keymap);
        xkbState.keymap = NULL;
    }
    
    if (xkbState.context) {
        xkb_context_unref(xkbState.context);
        xkbState.context = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSetKeymap --
 *
 *	Set XKB keymap from file descriptor (called by Wayland seat).
 *
 * Results:
 *	Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *	Updates XKB keymap and state.
 *
 *----------------------------------------------------------------------
 */

int
TkWaylandSetKeymap(
    int fd,
    uint32_t size)
{
    char *map_str;
    
    if (!xkbState.context) {
        close(fd);
        return 0;
    }
    
    /* Map keymap file. */
    map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map_str == MAP_FAILED) {
        close(fd);
        return 0;
    }
    
    /* Create new keymap. */
    if (xkbState.keymap) {
        xkb_keymap_unref(xkbState.keymap);
    }
    
    xkbState.keymap = xkb_keymap_new_from_string(
        xkbState.context, map_str, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);
    
    munmap(map_str, size);
    close(fd);
    
    if (!xkbState.keymap) {
        return 0;
    }
    
    /* Create new state. */
    if (xkbState.state) {
        xkb_state_unref(xkbState.state);
    }
    
    xkbState.state = xkb_state_new(xkbState.keymap);
    if (!xkbState.state) {
        return 0;
    }
    
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateXKBModifiers --
 *
 *	Update XKB modifier state.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates XKB state with new modifiers.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateXKBModifiers(
    unsigned int mods_depressed,
    unsigned int mods_latched,
    unsigned int mods_locked,
    unsigned int group)
{
    if (!xkbState.state) {
        return;
    }
    
    xkb_state_update_mask(xkbState.state,
        mods_depressed, mods_latched, mods_locked,
        0, 0, group);
}

/*
 *----------------------------------------------------------------------
 *
 * XKBKeycodeToKeysym --
 *
 *	Convert XKB keycode to X11 KeySym.
 *
 * Results:
 *	Returns KeySym.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static KeySym
XKBKeycodeToKeysym(
    unsigned int keycode)
{
    xkb_keysym_t keysym;
    
    if (!xkbState.state) {
        return NoSymbol;
    }
    
    /* XKB keycode is evdev keycode + 8. */
    keysym = xkb_state_key_get_one_sym(xkbState.state, keycode + 8);
    
    /* Handle compose sequences. */
    if (xkbState.compose_state) {
        if (xkb_compose_state_feed(xkbState.compose_state, keysym) ==
            XKB_COMPOSE_FEED_ACCEPTED) {
            
            switch (xkb_compose_state_get_status(xkbState.compose_state)) {
            case XKB_COMPOSE_COMPOSED:
                keysym = xkb_compose_state_get_one_sym(
                    xkbState.compose_state);
                xkb_compose_state_reset(xkbState.compose_state);
                break;
            case XKB_COMPOSE_COMPOSING:
                return NoSymbol;  /* Still composing. */
            case XKB_COMPOSE_CANCELLED:
                xkb_compose_state_reset(xkbState.compose_state);
                break;
            case XKB_COMPOSE_NOTHING:
                break;
            }
        }
    }
    
    return (KeySym)keysym;
}

/*
 *----------------------------------------------------------------------
 *
 * XKBKeysymToString --
 *
 *	Convert KeySym to UTF-8 string.
 *
 * Results:
 *	Returns number of bytes written to buffer.
 *
 * Side effects:
 *	Writes UTF-8 string to buffer.
 *
 *----------------------------------------------------------------------
 */

static int
XKBKeysymToString(
    KeySym keysym,
    char *buffer,
    int buflen)
{
    if (!xkbState.state) {
        return 0;
    }
    
    return xkb_keysym_to_utf8(keysym, buffer, buflen);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandProcessKey --
 *
 *	Process a key press or release event.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Generates Tk KeyPress/KeyRelease events.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandProcessKey(
    Tk_Window tkwin,
    unsigned int keycode,
    int pressed,
    unsigned int time)
{
    XEvent event;
    KeySym keysym;
    char buffer[32];
    int len;
    
    if (!tkwin) {
        return;
    }
    
    /* Convert keycode to keysym. */
    keysym = XKBKeycodeToKeysym(keycode);
    if (keysym == NoSymbol && !pressed) {
        /* Key release for composing key - send it. */
        keysym = XK_VoidSymbol;
    }
    
    /* Get UTF-8 string for key. */
    len = 0;
    if (pressed && keysym != NoSymbol) {
        len = XKBKeysymToString(keysym, buffer, sizeof(buffer) - 1);
        if (len > 0) {
            buffer[len] = '\0';
        }
    }
    
    /* Build X event. */
    memset(&event, 0, sizeof(XEvent));
    event.xkey.type = pressed ? KeyPress : KeyRelease;
    event.xkey.serial = 0;
    event.xkey.send_event = False;
    event.xkey.display = Tk_Display(tkwin);
    event.xkey.window = Tk_WindowId(tkwin);
    event.xkey.root = RootWindow(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
    event.xkey.subwindow = None;
    event.xkey.time = time;
    event.xkey.x = 0;
    event.xkey.y = 0;
    event.xkey.x_root = 0;
    event.xkey.y_root = 0;
    event.xkey.state = 0;  /* Modifiers - could extract from XKB state. */
    event.xkey.keycode = keycode;
    event.xkey.same_screen = True;
    
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *----------------------------------------------------------------------
 *
 * IME (Input Method Editor) Implementation
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * HashWindow --
 *
 *	Simple hash function for Tk_Window pointers.
 *
 * Results:
 *	Returns hash value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static unsigned int
HashWindow(Tk_Window tkwin)
{
    uintptr_t ptr = (uintptr_t)tkwin;
    return (unsigned int)((ptr >> 4) ^ (ptr));
}

/*
 *----------------------------------------------------------------------
 *
 * FindIMEState --
 *
 *	Find IME state for a window in hash table.
 *
 * Results:
 *	Returns IME state or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static TkIMEState *
FindIMEState(Tk_Window tkwin)
{
    unsigned int hash;
    TkIMEState *ime;
    
    if (!wlIME.ime_hash || !tkwin) {
        return NULL;
    }
    
    hash = HashWindow(tkwin) % wlIME.hash_size;
    ime = wlIME.ime_hash[hash];
    
    while (ime) {
        if (ime->tkwin == tkwin) {
            return ime;
        }
        ime = ime->next;
    }
    
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * StoreIMEState --
 *
 *	Store IME state in hash table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds IME state to hash table.
 *
 *----------------------------------------------------------------------
 */

static void
StoreIMEState(Tk_Window tkwin, TkIMEState *ime)
{
    unsigned int hash;
    
    if (!wlIME.ime_hash || !tkwin || !ime) {
        return;
    }
    
    hash = HashWindow(tkwin) % wlIME.hash_size;
    ime->next = wlIME.ime_hash[hash];
    wlIME.ime_hash[hash] = ime;
}

/*
 *----------------------------------------------------------------------
 *
 * RemoveIMEState --
 *
 *	Remove IME state from hash table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes IME state from hash table.
 *
 *----------------------------------------------------------------------
 */

static void
RemoveIMEState(Tk_Window tkwin)
{
    unsigned int hash;
    TkIMEState *ime, *prev = NULL;
    
    if (!wlIME.ime_hash || !tkwin) {
        return;
    }
    
    hash = HashWindow(tkwin) % wlIME.hash_size;
    ime = wlIME.ime_hash[hash];
    
    while (ime) {
        if (ime->tkwin == tkwin) {
            if (prev) {
                prev->next = ime->next;
            } else {
                wlIME.ime_hash[hash] = ime->next;
            }
            return;
        }
        prev = ime;
        ime = ime->next;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * InitializeIME --
 *
 *	Initialize IME support via separate Wayland connection.
 *
 * Results:
 *	Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *	Opens Wayland connection, registers with Tcl event loop.
 *
 *----------------------------------------------------------------------
 */

static int
InitializeIME(void)
{
    /* Connect to Wayland display. */
    wlIME.display = wl_display_connect(NULL);
    if (!wlIME.display) {
        return 0;  /* Not on Wayland. */
    }
    
    /* Get registry. */
    wlIME.registry = wl_display_get_registry(wlIME.display);
    if (!wlIME.registry) {
        wl_display_disconnect(wlIME.display);
        wlIME.display = NULL;
        return 0;
    }
    
    /* Initialize hash table with reasonable size. */
    wlIME.hash_size = 64;
    wlIME.ime_hash = (TkIMEState **)ckalloc(sizeof(TkIMEState *) * wlIME.hash_size);
    memset(wlIME.ime_hash, 0, sizeof(TkIMEState *) * wlIME.hash_size);
    
    /* Listen for globals. */
    wl_registry_add_listener(wlIME.registry, &registry_listener, NULL);
    
    /* Roundtrip to get all globals. */
    wl_display_roundtrip(wlIME.display);
    
    if (!wlIME.seat || !wlIME.text_input_manager) {
        /* Required interfaces not available. */
        CleanupIME();
        return 0;
    }
    
    /* Get file descriptor for event loop. */
    wlIME.fd = wl_display_get_fd(wlIME.display);
    if (wlIME.fd < 0) {
        CleanupIME();
        return 0;
    }
    
    /* Create Tcl channel. */
    wlIME.channel = Tcl_MakeFileChannel((ClientData)(intptr_t)wlIME.fd,
        TCL_READABLE);
    if (!wlIME.channel) {
        CleanupIME();
        return 0;
    }
    
    Tcl_CreateChannelHandler(wlIME.channel, TCL_READABLE,
        WaylandIMEEventHandler, NULL);
    
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * CleanupIME --
 *
 *	Clean up IME resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Closes Wayland connection.
 *
 *----------------------------------------------------------------------
 */

static void
CleanupIME(void)
{
    int i;
    
    if (wlIME.channel) {
        Tcl_DeleteChannelHandler(wlIME.channel,
            WaylandIMEEventHandler, NULL);
        Tcl_Close(NULL, wlIME.channel);
        wlIME.channel = NULL;
    }
    
    if (wlIME.text_input_manager) {
        zwp_text_input_manager_v3_destroy(wlIME.text_input_manager);
        wlIME.text_input_manager = NULL;
    }
    
    if (wlIME.seat) {
        wl_seat_destroy(wlIME.seat);
        wlIME.seat = NULL;
    }
    
    if (wlIME.registry) {
        wl_registry_destroy(wlIME.registry);
        wlIME.registry = NULL;
    }
    
    if (wlIME.ime_hash) {
        /* Free any remaining IME states */
        for (i = 0; i < wlIME.hash_size; i++) {
            TkIMEState *ime = wlIME.ime_hash[i];
            while (ime) {
                TkIMEState *next = ime->next;
                DestroyIMEState(ime);
                ime = next;
            }
        }
        ckfree((char *)wlIME.ime_hash);
        wlIME.ime_hash = NULL;
    }
    
    if (wlIME.display) {
        wl_display_disconnect(wlIME.display);
        wlIME.display = NULL;
    }
    
    wlIME.fd = -1;
    wlIME.hash_size = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * WaylandIMEEventHandler --
 *
 *	Process Wayland IME events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Dispatches Wayland events.
 *
 *----------------------------------------------------------------------
 */

static void
WaylandIMEEventHandler(
    TCL_UNUSED(ClientData), /* clientData */
    TCL_UNUSED(int)) /* mask */
{
    
    if (!wlIME.display) {
        return;
    }
    
    wl_display_dispatch_pending(wlIME.display);
    wl_display_flush(wlIME.display);
}

/*
 *----------------------------------------------------------------------
 *
 * Wayland Registry Callbacks --
 *
 *----------------------------------------------------------------------
 */

static void
RegistryHandleGlobal(
    TCL_UNUSED(void *), /* data */
    struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version)
{
    
    if (strcmp(interface, "wl_seat") == 0) {
        wlIME.seat = wl_registry_bind(registry, name,
            &wl_seat_interface, 1);
        wl_seat_add_listener(wlIME.seat, &seat_listener, NULL);
    } else if (strcmp(interface, zwp_text_input_manager_v3_interface.name) == 0) {
        wlIME.text_input_manager = wl_registry_bind(registry, name,
            &zwp_text_input_manager_v3_interface,
            version < 1 ? version : 1);
    }
}

static void
RegistryHandleGlobalRemove(
    TCL_UNUSED(void*), /* data */
    TCL_UNUSED(struct wl_registry *), /* registry */
    TCL_UNUSED(uint32_t)) /* name */
{
    /* Handle removal if needed. */
}

static void
SeatHandleCapabilities(
    TCL_UNUSED(void *), /* data */
    TCL_UNUSED(struct wl_seat *), /*seat */
    TCL_UNUSED(uint32_t)) /* capabilities */
{
    /* Seat capabilities changed. */
}

static void
SeatHandleName(
    TCL_UNUSED(void *), /* data */
    TCL_UNUSED(struct wl_seat *), /*seat */
    TCL_UNUSED(const char *)) /* name */
{
    /* Seat name received. */
}

/*
 *----------------------------------------------------------------------
 *
 * IME State Management --
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * CreateIMEState --
 *
 *	Create IME state for a window.
 *
 * Results:
 *	Returns new IME state or NULL.
 *
 * Side effects:
 *	Allocates IME state, creates text-input object.
 *
 *----------------------------------------------------------------------
 */

static TkIMEState *
CreateIMEState(
    Tk_Window tkwin)
{
    TkIMEState *ime;
    
    if (!wlIME.text_input_manager || !wlIME.seat) {
        return NULL;
    }
    
    ime = (TkIMEState *)ckalloc(sizeof(TkIMEState));
    memset(ime, 0, sizeof(TkIMEState));
    
    ime->tkwin = tkwin;
    
    /* Create text-input object. */
    ime->text_input = zwp_text_input_manager_v3_create_text_input(
        wlIME.text_input_manager);
    
    if (!ime->text_input) {
        ckfree((char *)ime);
        return NULL;
    }
    
    zwp_text_input_v3_add_listener(ime->text_input,
        &text_input_listener, ime);
    
    return ime;
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyIMEState --
 *
 *	Destroy IME state.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees resources.
 *
 *----------------------------------------------------------------------
 */

static void
DestroyIMEState(
    TkIMEState *ime)
{
    if (!ime) {
        return;
    }
    
    if (ime == currentIME) {
        currentIME = NULL;
    }
    
    if (ime->text_input) {
        zwp_text_input_v3_destroy(ime->text_input);
    }
    
    if (ime->preedit_string) {
        ckfree(ime->preedit_string);
    }
    
    if (ime->commit_string) {
        ckfree(ime->commit_string);
    }
    
    ckfree((char *)ime);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandIMEEnable --
 *
 *	Enable IME for a window.
 *
 * Results:
 *	Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *	Enables text input.
 *
 *----------------------------------------------------------------------
 */

int
TkWaylandIMEEnable(
    Tk_Window tkwin)
{
    TkIMEState *ime;
    
    if (!tkwin) {
        return 0;
    }
    
    /* Get or create IME state. */
    ime = FindIMEState(tkwin);
    if (!ime) {
        ime = CreateIMEState(tkwin);
        if (!ime) {
            return 0;
        }
        StoreIMEState(tkwin, ime);
    }
    
    if (ime->enabled) {
        return 1;
    }
    
    /* Enable text input. */
    zwp_text_input_v3_enable(ime->text_input);
    zwp_text_input_v3_commit(ime->text_input);
    
    ime->enabled = 1;
    currentIME = ime;
    
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandIMEDisable --
 *
 *	Disable IME for a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Disables text input.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandIMEDisable(
    Tk_Window tkwin)
{
    TkIMEState *ime;
    
    if (!tkwin) {
        return;
    }
    
    ime = FindIMEState(tkwin);
    if (!ime) {
        return;
    }
    
    if (!ime->enabled) {
        return;
    }
    
    zwp_text_input_v3_disable(ime->text_input);
    zwp_text_input_v3_commit(ime->text_input);
    
    if (ime->preedit_string) {
        ckfree(ime->preedit_string);
        ime->preedit_string = NULL;
    }
    
    ime->enabled = 0;
    ime->preedit_active = 0;
    
    if (currentIME == ime) {
        currentIME = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandIMESetCursorRect --
 *
 *	Set cursor rectangle for IME positioning.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates cursor position for compositor.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandIMESetCursorRect(
    Tk_Window tkwin,
    int x,
    int y,
    int width,
    int height)
{
    TkIMEState *ime;
    
    if (!tkwin) {
        return;
    }
    
    ime = FindIMEState(tkwin);
    if (!ime) {
        return;
    }
    
    ime->cursor_x = x;
    ime->cursor_y = y;
    ime->cursor_width = width;
    ime->cursor_height = height;
    
    if (ime->enabled && ime->text_input) {
        zwp_text_input_v3_set_cursor_rectangle(ime->text_input,
            x, y, width, height);
        zwp_text_input_v3_commit(ime->text_input);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandIMEGetPreedit --
 *
 *	Get current preedit string.
 *
 * Results:
 *	Returns preedit string or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
TkWaylandIMEGetPreedit(
    Tk_Window tkwin,
    int *cursorPos)
{
    TkIMEState *ime;
    
    if (!tkwin) {
        return NULL;
    }
    
    ime = FindIMEState(tkwin);
    if (!ime) {
        return NULL;
    }
    
    if (!ime->preedit_active || !ime->preedit_string) {
        return NULL;
    }
    
    if (cursorPos) {
        *cursorPos = ime->preedit_cursor;
    }
    
    return ime->preedit_string;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandIMESetSurroundingText --
 *
 *	Provide surrounding text context to IME.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sends surrounding text to compositor.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandIMESetSurroundingText(
    Tk_Window tkwin,
    const char *text,
    int cursor_index,
    int anchor_index)
{
    TkIMEState *ime;
    
    if (!tkwin) {
        return;
    }
    
    ime = FindIMEState(tkwin);
    if (!ime) {
        return;
    }
    
    if (!ime->enabled || !ime->text_input) {
        return;
    }
    
    zwp_text_input_v3_set_surrounding_text(ime->text_input,
        text ? text : "", cursor_index, anchor_index);
    zwp_text_input_v3_commit(ime->text_input);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandIMESetContentType --
 *
 *	Set content type hints for IME.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Notifies compositor of content type.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandIMESetContentType(
    Tk_Window tkwin,
    uint32_t hint,
    uint32_t purpose)
{
    TkIMEState *ime;
    
    if (!tkwin) {
        return;
    }
    
    ime = FindIMEState(tkwin);
    if (!ime) {
        return;
    }
    
    if (!ime->enabled || !ime->text_input) {
        return;
    }
    
    zwp_text_input_v3_set_content_type(ime->text_input, hint, purpose);
    zwp_text_input_v3_commit(ime->text_input);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandIMERemove --
 *
 *	Remove IME state for a window (called when window is destroyed).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes IME state from hash table and destroys it.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandIMERemove(
    Tk_Window tkwin)
{
    TkIMEState *ime;
    
    if (!tkwin) {
        return;
    }
    
    ime = FindIMEState(tkwin);
    if (ime) {
        RemoveIMEState(tkwin);
        DestroyIMEState(ime);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Wayland Text-Input Callbacks --
 *
 *----------------------------------------------------------------------
 */

static void
IMETextInputEnter(
    void *data,
    TCL_UNUSED(struct zwp_text_input_v3 *), /* text_input */
    TCL_UNUSED(struct wl_surface *)) /* surface */
{
    TkIMEState *ime = (TkIMEState *)data;
    
    ime->enabled = 1;
    currentIME = ime;
}

static void
IMETextInputLeave(
    void *data,
    TCL_UNUSED(struct zwp_text_input_v3 *), /* text_input */
    TCL_UNUSED(struct wl_surface *)) /* surface */
{
    TkIMEState *ime = (TkIMEState *)data;
    (void)text_input;
    (void)surface;
    
    ime->enabled = 0;
    if (currentIME == ime) {
        currentIME = NULL;
    }
}

static void
IMETextInputPreeditString(
    void *data,
    TCL_UNUSED(struct zwp_text_input_v3 *), /* text_input */
    const char *text,
    int32_t cursor_begin,
    TCL_UNUSED(int32_t)) /* cursor_end */
{
    TkIMEState *ime = (TkIMEState *)data;
    
    if (ime->preedit_string) {
        ckfree(ime->preedit_string);
        ime->preedit_string = NULL;
    }
    
    if (text && *text) {
        ime->preedit_string = (char *)ckalloc(strlen(text) + 1);
        strcpy(ime->preedit_string, text);
        ime->preedit_cursor = cursor_begin;
        ime->preedit_active = 1;
    } else {
        ime->preedit_active = 0;
        ime->preedit_cursor = 0;
    }
    
    /* Trigger widget redraw to show preedit. */
    if (ime->tkwin) {
        Tk_RedrawWindow(ime->tkwin, NULL, 0);
    }
}

static void
IMETextInputCommitString(
    void *data,
    TCL_UNUSED(struct zwp_text_input_v3 *), /* text_input */
    const char *text)
{
    TkIMEState *ime = (TkIMEState *)data;
    
    if (ime->commit_string) {
        ckfree(ime->commit_string);
        ime->commit_string = NULL;
    }
    
    if (text && *text) {
        ime->commit_string = (char *)ckalloc(strlen(text) + 1);
        strcpy(ime->commit_string, text);
    }
    
    /* Clear preedit. */
    if (ime->preedit_string) {
        ckfree(ime->preedit_string);
        ime->preedit_string = NULL;
    }
    ime->preedit_active = 0;
}

static void
IMETextInputDeleteSurroundingText(
    TCL_UNUSED(void *), /* data */
    TCL_UNUSED(struct zwp_text_input_v3 *), /* text_input */
    TCL_UNUSED(uint32_t),  /* before_length */
    TCL_UNUSED(uint32_t)) /* after_length */
{
    /* Handle deletion if needed. */
}

static void
IMETextInputDone(
    void *data,
    TCL_UNUSED(struct zwp_text_input_v3 *), /* text_input */
    TCL_UNUSED(uint32_t)) /* serial */
{
    TkIMEState *ime = (TkIMEState *)data;
    (void)text_input;
    (void)serial;
    
    /* Process commit. */
    if (ime->commit_string) {
        SendIMECommitEvent(ime);
        ckfree(ime->commit_string);
        ime->commit_string = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SendIMECommitEvent --
 *
 *	Send committed text as KeyPress events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Generates KeyPress events for each character.
 *
 *----------------------------------------------------------------------
 */

static void
SendIMECommitEvent(
    TkIMEState *ime)
{
    XEvent event;
    const char *p;
    int ch;
    
    if (!ime || !ime->commit_string || !ime->tkwin) {
        return;
    }
    
    /* Generate KeyPress for each UTF-8 character. */
    p = ime->commit_string;
    while (*p) {
        p += Tcl_UtfToUniChar(p, &ch);
        
        memset(&event, 0, sizeof(XEvent));
        event.xkey.type = KeyPress;
        event.xkey.display = Tk_Display(ime->tkwin);
        event.xkey.window = Tk_WindowId(ime->tkwin);
        event.xkey.keycode = ch;
        event.xkey.state = 0;
        
        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
        
        event.xkey.type = KeyRelease;
        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetCaretPos --
 *
 *	Standard Tk API for setting caret position (for IME).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates IME cursor rectangle.
 *
 *----------------------------------------------------------------------
 */

void
Tk_SetCaretPos(
    Tk_Window tkwin,
    int x,
    int y,
    int height)
{
    TkWaylandIMESetCursorRect(tkwin, x, y, 1, height);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
