/*
 * tkWaylandKey.c --
 *
 * This file contains functions for keyboard input handling on Wayland/GLFW,
 * including comprehensive IME (Input Method Editor) support for
 * complex text input (Chinese, Japanese, Korean, etc.).
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

/* GLFW Wayland native access for getting wl_surface. */
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <wayland-client.h>
#include <string.h>
#include <ctype.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include "text-input-unstable-v3-client-protocol.h"

/* Forward declaration of IME state structure */
typedef struct TkIMEState TkIMEState;

/*
 *---------------------------------------------------------------------------
 *
 * Keyboard State Management
 *
 *---------------------------------------------------------------------------
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
    uint32_t modifiers_depressed;
    uint32_t modifiers_latched;
    uint32_t modifiers_locked;
    uint32_t group;
} TkXKBState;

static TkXKBState xkbState = {NULL, NULL, NULL, NULL, NULL, 0, 0, 0, 0};

/*
 *---------------------------------------------------------------------------
 *
 * IME (Input Method Editor) Support
 *
 *---------------------------------------------------------------------------
 */

/*
 * Global Wayland IME connection state - separate from GLFW's connection.
 */
typedef struct {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct zwp_text_input_manager_v3 *text_input_manager;
    
    int fd;                         /* File descriptor for event loop */
    Tcl_Channel channel;             /* Tcl channel for event handling */
    int display_initialized;         /* Flag for connection state */
    
    /* Hash table for window -> IME state mapping */
    TkIMEState **ime_hash;
    int hash_size;
    int hash_count;
} WaylandIMEConnection;

static WaylandIMEConnection wlIME = {
    NULL, NULL, NULL, NULL, NULL, -1, NULL, 0, NULL, 0, 0
};

/*
 * IME state per Tk window.
 */
typedef struct TkIMEState {
    struct zwp_text_input_v3 *text_input;
    struct wl_surface *surface;  /* Wayland surface from GLFW */
    struct wl_keyboard *keyboard; /* Keyboard object for this seat */
    
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

static TkIMEState *currentIME = NULL;

/* Hash table functions */
static unsigned int HashWindow(Tk_Window tkwin);
static TkIMEState *FindIMEState(Tk_Window tkwin);
static void StoreIMEState(Tk_Window tkwin, TkIMEState *ime);
static void RemoveIMEState(Tk_Window tkwin);
static void ResizeHashTableIfNeeded(void);

/*
 *---------------------------------------------------------------------------
 *
 * Forward Declarations
 *
 *---------------------------------------------------------------------------
 */

/* XKB functions. */
static int InitializeXKB(void);
static void CleanupXKB(void);
static void UpdateXKBModifiers(unsigned int mods_depressed,
        unsigned int mods_latched, unsigned int mods_locked,
        unsigned int group);
static int XKBKeycodeToX11Keycode(unsigned int keycode);
static unsigned int XKBGetModifierState(void);

/* IME functions. */
static int InitializeIME(void);
static void CleanupIME(void);
static void WaylandIMEEventHandler(ClientData clientData, int mask);
static int WaylandIMEDispatchEvents(void);
static TkIMEState *CreateIMEState(Tk_Window tkwin);
static void DestroyIMEState(TkIMEState *ime);
static void SendIMECommitEvent(TkIMEState *ime);
static void SendIMEPreeditEvent(TkIMEState *ime);

/* Wayland callbacks. */
static void RegistryHandleGlobal(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version);
static void RegistryHandleGlobalRemove(void *data,
        struct wl_registry *registry, uint32_t name);
static void SeatHandleCapabilities(void *data, struct wl_seat *seat,
        uint32_t capabilities);
static void SeatHandleName(void *data, struct wl_seat *seat,
        const char *name);

/* Keyboard callbacks. */
static void KeyboardHandleKeymap(void *data, struct wl_keyboard *keyboard,
        uint32_t format, int32_t fd, uint32_t size);
static void KeyboardHandleEnter(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, struct wl_surface *surface, struct wl_array *keys);
static void KeyboardHandleLeave(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, struct wl_surface *surface);
static void KeyboardHandleKey(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
static void KeyboardHandleModifiers(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
        uint32_t mods_locked, uint32_t group);
static void KeyboardHandleRepeatInfo(void *data, struct wl_keyboard *keyboard,
        int32_t rate, int32_t delay);

/* IME callbacks. */
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

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = KeyboardHandleKeymap,
    .enter = KeyboardHandleEnter,
    .leave = KeyboardHandleLeave,
    .key = KeyboardHandleKey,
    .modifiers = KeyboardHandleModifiers,
    .repeat_info = KeyboardHandleRepeatInfo,
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
 *---------------------------------------------------------------------------
 *
 * TkWaylandKeyInit --
 *
 *      Initialize keyboard handling for Wayland, including XKB and IME.
 *
 * Results:
 *      Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *      Initializes XKB context and IME connection.
 *
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
 *
 * TkWaylandKeyCleanup --
 *
 *      Clean up keyboard and IME resources.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Frees XKB and IME resources.
 *
 *---------------------------------------------------------------------------
 */

void
TkWaylandKeyCleanup(void)
{
    CleanupIME();
    CleanupXKB();
}

/*
 *---------------------------------------------------------------------------
 *
 * XKB Keyboard Handling
 *
 *---------------------------------------------------------------------------
 */

/*
 *---------------------------------------------------------------------------
 *
 * InitializeXKB --
 *
 *      Initialize XKB context for key translation.
 *
 * Results:
 *      Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *      Creates XKB context and compose table.
 *
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
 *
 * CleanupXKB --
 *
 *      Clean up XKB resources.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Frees XKB context and compose state.
 *
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
 *
 * TkWaylandSetKeymap --
 *
 *      Set XKB keymap from file descriptor (called by Wayland seat).
 *
 * Results:
 *      Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *      Updates XKB keymap and state.
 *
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
 *
 * UpdateXKBModifiers --
 *
 *      Update XKB modifier state.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates XKB state with new modifiers.
 *
 *---------------------------------------------------------------------------
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
    
    xkbState.modifiers_depressed = mods_depressed;
    xkbState.modifiers_latched = mods_latched;
    xkbState.modifiers_locked = mods_locked;
    xkbState.group = group;
    
    xkb_state_update_mask(xkbState.state,
            mods_depressed, mods_latched, mods_locked,
            0, 0, group);
}


/*
 *---------------------------------------------------------------------------
 *
 * XKBKeycodeToX11Keycode --
 *
 *      Convert Wayland keycode to X11 keycode.
 *
 * Results:
 *      Returns X11 keycode.
 *
 * Side effects:
 *      None.
 *
 *---------------------------------------------------------------------------
 */

static int
XKBKeycodeToX11Keycode(
    unsigned int keycode)
{
    /* Wayland uses evdev keycodes (8-255)
     * X11 typically uses the same range + 8 offset */
    return keycode + 8;
}

/*
 *---------------------------------------------------------------------------
 *
 * XKBGetModifierState --
 *
 *      Get current XKB modifier state as X11 modifier mask.
 *
 * Results:
 *      Returns modifier mask.
 *
 * Side effects:
 *      None.
 *
 *---------------------------------------------------------------------------
 */

static unsigned int
XKBGetModifierState(void)
{
    unsigned int mask = 0;
    
    if (!xkbState.state) {
        return 0;
    }
    
    /* Convert XKB modifier indices to X11 masks. */
    if (xkb_state_mod_index_is_active(xkbState.state,
            xkb_keymap_mod_get_index(xkbState.keymap, XKB_MOD_NAME_SHIFT),
            XKB_STATE_MODS_EFFECTIVE) > 0) {
        mask |= ShiftMask;
    }
    
    if (xkb_state_mod_index_is_active(xkbState.state,
            xkb_keymap_mod_get_index(xkbState.keymap, XKB_MOD_NAME_CAPS),
            XKB_STATE_MODS_EFFECTIVE) > 0) {
        mask |= LockMask;
    }
    
    if (xkb_state_mod_index_is_active(xkbState.state,
            xkb_keymap_mod_get_index(xkbState.keymap, XKB_MOD_NAME_CTRL),
            XKB_STATE_MODS_EFFECTIVE) > 0) {
        mask |= ControlMask;
    }
    
    if (xkb_state_mod_index_is_active(xkbState.state,
            xkb_keymap_mod_get_index(xkbState.keymap, XKB_MOD_NAME_ALT),
            XKB_STATE_MODS_EFFECTIVE) > 0) {
        mask |= Mod1Mask;
    }
    
    if (xkb_state_mod_index_is_active(xkbState.state,
            xkb_keymap_mod_get_index(xkbState.keymap, XKB_MOD_NAME_LOGO),
            XKB_STATE_MODS_EFFECTIVE) > 0) {
        mask |= Mod4Mask;
    }
    
    return mask;
}

/*
 *---------------------------------------------------------------------------
 *
 * XKBKeysymToString --
 *
 *      Convert KeySym to UTF-8 string.
 *
 * Results:
 *      Returns number of bytes written to buffer.
 *
 * Side effects:
 *      Writes UTF-8 string to buffer.
 *
 *---------------------------------------------------------------------------
 */

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandProcessKey --
 *
 *      Process a key press or release event.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates Tk KeyPress/KeyRelease events.
 *
 *---------------------------------------------------------------------------
 */

void
TkWaylandProcessKey(
    Tk_Window tkwin,
    unsigned int keycode,
    int pressed,
    unsigned int time)
{
    XEvent event;
    TkIMEState *ime;
    
    if (!tkwin) {
        return;
    }
    
    /* Check if IME is active - if so, let it handle the key. */
    ime = FindIMEState(tkwin);
    if (ime && ime->enabled && ime->preedit_active) {
        /* IME is handling composition, don't send raw key events. */
        return;
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
    event.xkey.state = XKBGetModifierState();
    event.xkey.keycode = XKBKeycodeToX11Keycode(keycode);
    event.xkey.same_screen = True;
    
    /* For KeyPress, we can't directly set string data in XKeyEvent */
    
    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 *---------------------------------------------------------------------------
 *
 * IME (Input Method Editor) Implementation
 *
 *---------------------------------------------------------------------------
 */

/*
 *---------------------------------------------------------------------------
 *
 * HashWindow --
 *
 *      Simple hash function for Tk_Window pointers.
 *
 * Results:
 *      Returns hash value.
 *
 * Side effects:
 *      None.
 *
 *---------------------------------------------------------------------------
 */

static unsigned int
HashWindow(Tk_Window tkwin)
{
    uintptr_t ptr = (uintptr_t)tkwin;
    return (unsigned int)((ptr >> 4) ^ (ptr));
}

/*
 *---------------------------------------------------------------------------
 *
 * ResizeHashTableIfNeeded --
 *
 *      Resize hash table if load factor is too high.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Reallocates and rehashes the hash table.
 *
 *---------------------------------------------------------------------------
 */

static void
ResizeHashTableIfNeeded(void)
{
    TkIMEState **new_hash;
    int new_size, i;
    unsigned int hash;
    TkIMEState *ime, *next;
    
    if (!wlIME.ime_hash || wlIME.hash_count < wlIME.hash_size * 2) {
        return;
    }
    
    new_size = wlIME.hash_size * 2;
    new_hash = (TkIMEState **)ckalloc(sizeof(TkIMEState *) * new_size);
    memset(new_hash, 0, sizeof(TkIMEState *) * new_size);
    
    /* Rehash all entries. */
    for (i = 0; i < wlIME.hash_size; i++) {
        ime = wlIME.ime_hash[i];
        while (ime) {
            next = ime->next;
            hash = HashWindow(ime->tkwin) % new_size;
            ime->next = new_hash[hash];
            new_hash[hash] = ime;
            ime = next;
        }
    }
    
    ckfree((char *)wlIME.ime_hash);
    wlIME.ime_hash = new_hash;
    wlIME.hash_size = new_size;
}

/*
 *---------------------------------------------------------------------------
 *
 * FindIMEState --
 *
 *      Find IME state for a window in hash table.
 *
 * Results:
 *      Returns IME state or NULL.
 *
 * Side effects:
 *      None.
 *
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
 *
 * StoreIMEState --
 *
 *      Store IME state in hash table.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Adds IME state to hash table.
 *
 *---------------------------------------------------------------------------
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
    wlIME.hash_count++;
    
    ResizeHashTableIfNeeded();
}

/*
 *---------------------------------------------------------------------------
 *
 * RemoveIMEState --
 *
 *      Remove IME state from hash table.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Removes IME state from hash table.
 *
 *---------------------------------------------------------------------------
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
            wlIME.hash_count--;
            return;
        }
        prev = ime;
        ime = ime->next;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * InitializeIME --
 *
 *      Initialize IME support via separate Wayland connection.
 *
 * Results:
 *      Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *      Opens Wayland connection, registers with Tcl event loop.
 *
 *---------------------------------------------------------------------------
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
    wlIME.hash_count = 0;
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
    
    wlIME.display_initialized = 1;
    
    return 1;
}

/*
 *---------------------------------------------------------------------------
 *
 * CleanupIME --
 *
 *      Clean up IME resources.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Closes Wayland connection.
 *
 *---------------------------------------------------------------------------
 */

static void
CleanupIME(void)
{
    int i;
    
    wlIME.display_initialized = 0;
    
    if (wlIME.channel) {
        Tcl_DeleteChannelHandler(wlIME.channel,
                WaylandIMEEventHandler, NULL);
        Tcl_Close(NULL, wlIME.channel);
        wlIME.channel = NULL;
    }
    
    if (wlIME.keyboard) {
        wl_keyboard_destroy(wlIME.keyboard);
        wlIME.keyboard = NULL;
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
        /* Free any remaining IME states. */
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
    wlIME.hash_count = 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * WaylandIMEDispatchEvents --
 *
 *      Properly read and dispatch Wayland events.
 *
 * Results:
 *      1 if events were processed, 0 otherwise.
 *
 * Side effects:
 *      Processes pending Wayland events.
 *
 *---------------------------------------------------------------------------
 */

static int
WaylandIMEDispatchEvents(void)
{
    int ret;
    
    if (!wlIME.display || !wlIME.display_initialized) {
        return 0;
    }
    
    /* Prepare to read events */
    while (wl_display_prepare_read(wlIME.display) != 0) {
        ret = wl_display_dispatch_pending(wlIME.display);
        if (ret < 0) {
            return 0;
        }
    }
    
    /* Flush any pending requests. */
    ret = wl_display_flush(wlIME.display);
    if (ret < 0 && errno != EAGAIN) {
        wl_display_cancel_read(wlIME.display);
        return 0;
    }
    
    /* Read events from the fd. */
    ret = wl_display_read_events(wlIME.display);
    if (ret < 0) {
        return 0;
    }
    
    /* Dispatch all pending events. */
    ret = wl_display_dispatch_pending(wlIME.display);
    
    return (ret >= 0) ? 1 : 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * WaylandIMEEventHandler --
 *
 *      Process Wayland IME events.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Dispatches Wayland events.
 *
 *---------------------------------------------------------------------------
 */

static void
WaylandIMEEventHandler(
    ClientData clientData,
    int mask)
{
    (void)clientData;
    (void)mask;
    WaylandIMEDispatchEvents();
}

/*
 *---------------------------------------------------------------------------
 *
 * Wayland Registry Callbacks --
 *
 *---------------------------------------------------------------------------
 */

static void
RegistryHandleGlobal(
    void *data,
    struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version)
{
    (void)data;
    
    if (strcmp(interface, "wl_seat") == 0) {
        wlIME.seat = wl_registry_bind(registry, name,
                &wl_seat_interface, 1);
        wl_seat_add_listener(wlIME.seat, &seat_listener, NULL);
    } else if (strcmp(interface, zwp_text_input_manager_v3_interface.name) == 0) {
        /* Use version 1 for maximum compatibility */
        wlIME.text_input_manager = wl_registry_bind(registry, name,
                &zwp_text_input_manager_v3_interface,
                (version < 1) ? version : 1);
    }
}

static void
RegistryHandleGlobalRemove(
    void *data,
    struct wl_registry *registry,
    uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
    /* Handle removal if needed. */
}

static void
SeatHandleCapabilities(
    void *data,
    struct wl_seat *seat,
    uint32_t capabilities)
{
    (void)data;
    (void)seat;
    
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !wlIME.keyboard) {
        wlIME.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(wlIME.keyboard, &keyboard_listener, NULL);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && wlIME.keyboard) {
        wl_keyboard_destroy(wlIME.keyboard);
        wlIME.keyboard = NULL;
    }
}

static void
SeatHandleName(
    void *data,
    struct wl_seat *seat,
    const char *name)
{
    (void)data;
    (void)seat;
    (void)name;
    /* Seat name received. */
}

/*
 *---------------------------------------------------------------------------
 *
 * Keyboard Callbacks --
 *
 *---------------------------------------------------------------------------
 */

static void
KeyboardHandleKeymap(
    void *data,
    struct wl_keyboard *keyboard,
    uint32_t format,
    int32_t fd,
    uint32_t size)
{
    (void)data;
    (void)keyboard;
    
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        TkWaylandSetKeymap(fd, size);
    } else {
        close(fd);
    }
}

static void
KeyboardHandleEnter(
    void *data,
    struct wl_keyboard *keyboard,
    uint32_t serial,
    struct wl_surface *surface,
    struct wl_array *keys)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    (void)keys;
    /* Keyboard focus entered. */
}

static void
KeyboardHandleLeave(
    void *data,
    struct wl_keyboard *keyboard,
    uint32_t serial,
    struct wl_surface *surface)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    /* Keyboard focus left. */
}

static void
KeyboardHandleKey(
    void *data,
    struct wl_keyboard *keyboard,
    uint32_t serial,
    uint32_t time,
    uint32_t key,
    uint32_t state)
{
    TkIMEState *ime = (TkIMEState *)data;
    (void)keyboard;
    (void)serial;
    
    if (ime && ime->tkwin) {
        TkWaylandProcessKey(ime->tkwin, key,
                (state == WL_KEYBOARD_KEY_STATE_PRESSED), time);
    }
}

static void
KeyboardHandleModifiers(
    void *data,
    struct wl_keyboard *keyboard,
    uint32_t serial,
    uint32_t mods_depressed,
    uint32_t mods_latched,
    uint32_t mods_locked,
    uint32_t group)
{
    (void)data;
    (void)keyboard;
    (void)serial;
    UpdateXKBModifiers(mods_depressed, mods_latched, mods_locked, group);
}

static void
KeyboardHandleRepeatInfo(
    void *data,
    struct wl_keyboard *keyboard,
    int32_t rate,
    int32_t delay)
{
    (void)data;
    (void)keyboard;
    (void)rate;
    (void)delay;
    /* Key repeat info - could be used for auto-repeat. */
}

/*
 *---------------------------------------------------------------------------
 *
 * IME State Management --
 *
 *---------------------------------------------------------------------------
 */

/*
 *---------------------------------------------------------------------------
 *
 * CreateIMEState --
 *
 *      Create IME state for a window and get its Wayland surface from GLFW.
 *
 * Results:
 *      Returns new IME state or NULL.
 *
 * Side effects:
 *      Allocates IME state, creates text-input object, links to GLFW surface.
 *
 *---------------------------------------------------------------------------
 */

static TkIMEState *
CreateIMEState(
    Tk_Window tkwin)
{
    TkIMEState *ime;
    GLFWwindow *glfwWindow;
    struct wl_surface *surface;
    
    if (!wlIME.text_input_manager || !wlIME.seat) {
        return NULL;
    }
    
    /* Get GLFW window to access Wayland surface. */
    glfwWindow = TkGlfwGetGLFWWindow(tkwin);
    if (!glfwWindow) {
        return NULL;
    }
    
    /* Get Wayland surface from GLFW using native access */
    surface = glfwGetWaylandWindow(glfwWindow);
    if (!surface) {
        return NULL;
    }
    
    ime = (TkIMEState *)ckalloc(sizeof(TkIMEState));
    memset(ime, 0, sizeof(TkIMEState));
    
    ime->tkwin = tkwin;
    ime->surface = surface;  /* Store GLFW's Wayland surface. */
    
    /* Create text-input object using the seat from our separate connection. */
    ime->text_input = zwp_text_input_manager_v3_get_text_input(
            wlIME.text_input_manager, wlIME.seat);
    
    if (!ime->text_input) {
        ckfree((char *)ime);
        return NULL;
    }
    
    /* Get keyboard for this seat. */
    if (wlIME.keyboard) {
        ime->keyboard = wlIME.keyboard;
    }
    
    zwp_text_input_v3_add_listener(ime->text_input,
            &text_input_listener, ime);
    
    return ime;
}

/*
 *---------------------------------------------------------------------------
 *
 * DestroyIMEState --
 *
 *      Destroy IME state.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Frees resources.
 *
 *---------------------------------------------------------------------------
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
    
    /* Note: We don't destroy ime->surface or ime->keyboard - they're owned elsewhere. */
    
    ckfree((char *)ime);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandIMEEnable --
 *
 *      Enable IME for a window.
 *
 * Results:
 *      Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *      Enables text input.
 *
 *---------------------------------------------------------------------------
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
    if (ime->text_input && ime->surface) {
        zwp_text_input_v3_enable(ime->text_input);
        zwp_text_input_v3_set_cursor_rectangle(ime->text_input,
                ime->cursor_x, ime->cursor_y,
                ime->cursor_width, ime->cursor_height);
        zwp_text_input_v3_commit(ime->text_input);
        
        ime->enabled = 1;
        currentIME = ime;
    }
    
    return ime->enabled;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandIMEDisable --
 *
 *      Disable IME for a window.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Disables text input.
 *
 *---------------------------------------------------------------------------
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
    
    if (ime->text_input) {
        zwp_text_input_v3_disable(ime->text_input);
        zwp_text_input_v3_commit(ime->text_input);
    }
    
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
 *---------------------------------------------------------------------------
 *
 * TkWaylandIMESetCursorRect --
 *
 *      Set cursor rectangle for IME positioning.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates cursor position for compositor.
 *
 *---------------------------------------------------------------------------
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
    
    if (ime->enabled && ime->text_input && ime->surface) {
        zwp_text_input_v3_set_cursor_rectangle(ime->text_input,
                x, y, width, height);
        zwp_text_input_v3_commit(ime->text_input);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandIMEGetPreedit --
 *
 *      Get current preedit string.
 *
 * Results:
 *      Returns preedit string or NULL.
 *
 * Side effects:
 *      None.
 *
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
 *
 * TkWaylandIMESetSurroundingText --
 *
 *      Provide surrounding text context to IME.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Sends surrounding text to compositor.
 *
 *---------------------------------------------------------------------------
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
    
    if (!ime->enabled || !ime->text_input || !ime->surface) {
        return;
    }
    
    zwp_text_input_v3_set_surrounding_text(ime->text_input,
            text ? text : "", cursor_index, anchor_index);
    zwp_text_input_v3_commit(ime->text_input);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandIMESetContentType --
 *
 *      Set content type hints for IME.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Notifies compositor of content type.
 *
 *---------------------------------------------------------------------------
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
    
    if (!ime->enabled || !ime->text_input || !ime->surface) {
        return;
    }
    
    zwp_text_input_v3_set_content_type(ime->text_input, hint, purpose);
    zwp_text_input_v3_commit(ime->text_input);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandIMERemove --
 *
 *      Remove IME state for a window (called when window is destroyed).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Removes IME state from hash table and destroys it.
 *
 *---------------------------------------------------------------------------
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
 *---------------------------------------------------------------------------
 *
 * SendIMEPreeditEvent --
 *
 *      Send preedit change notification.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates custom event for preedit update.
 *
 *---------------------------------------------------------------------------
 */

static void
SendIMEPreeditEvent(
    TkIMEState *ime)
{
    if (!ime || !ime->tkwin) {
        return;
    }
    
    /* Trigger widget redraw to show preedit */
	Tcl_DoOneEvent(TCL_ALL_EVENTS);

    
}

/*
 *---------------------------------------------------------------------------
 *
 * SendIMECommitEvent --
 *
 *      Send committed text as KeyPress events.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates KeyPress events with proper UTF-8 text.
 *
 *---------------------------------------------------------------------------
 */

static void
SendIMECommitEvent(
    TkIMEState *ime)
{
    XEvent event;
    const char *p;
    int ch;
    char utf8_buf[6];  /* Max UTF-8 char length */
    int utf8_len;
    
    if (!ime || !ime->commit_string || !ime->tkwin) {
        return;
    }
    
    /* Generate KeyPress for each UTF-8 character. */
    p = ime->commit_string;
    while (*p) {
        utf8_len = Tcl_UtfToUniChar(p, &ch);
        memcpy(utf8_buf, p, utf8_len);
        utf8_buf[utf8_len] = '\0';
        
        memset(&event, 0, sizeof(XEvent));
        event.xkey.type = KeyPress;
        event.xkey.display = Tk_Display(ime->tkwin);
        event.xkey.window = Tk_WindowId(ime->tkwin);
        event.xkey.keycode = 0;  /* Virtual keycode for IME input */
        event.xkey.state = XKBGetModifierState();
        
        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
        
        /* Optionally send KeyRelease if needed */
        event.xkey.type = KeyRelease;
        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
        
        p += utf8_len;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * Wayland Text-Input Callbacks --
 *
 *---------------------------------------------------------------------------
 */

static void
IMETextInputEnter(
    void *data,
    struct zwp_text_input_v3 *text_input,
    struct wl_surface *surface)
{
    TkIMEState *ime = (TkIMEState *)data;
    (void)text_input;
    
    if (ime && ime->surface == surface) {
        ime->enabled = 1;
        currentIME = ime;
    }
}

static void
IMETextInputLeave(
    void *data,
    struct zwp_text_input_v3 *text_input,
    struct wl_surface *surface)
{
    TkIMEState *ime = (TkIMEState *)data;
    (void)text_input;
    
    if (ime && ime->surface == surface) {
        ime->enabled = 0;
        if (currentIME == ime) {
            currentIME = NULL;
        }
    }
}

static void
IMETextInputPreeditString(
    void *data,
    struct zwp_text_input_v3 *text_input,
    const char *text,
    int32_t cursor_begin,
    int32_t cursor_end)
{
    TkIMEState *ime = (TkIMEState *)data;
    (void)text_input;
    (void)cursor_end;
    
    if (!ime) {
        return;
    }
    
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
    
    /* Notify widgets of preedit change. */
    SendIMEPreeditEvent(ime);
}

static void
IMETextInputCommitString(
    void *data,
    struct zwp_text_input_v3 *text_input,
    const char *text)
{
    TkIMEState *ime = (TkIMEState *)data;
    (void)text_input;
    
    if (!ime) {
        return;
    }
    
    if (ime->commit_string) {
        ckfree(ime->commit_string);
        ime->commit_string = NULL;
    }
    
    if (text && *text) {
        ime->commit_string = (char *)ckalloc(strlen(text) + 1);
        strcpy(ime->commit_string, text);
    }
    
    /* Clear preedit */
    if (ime->preedit_string) {
        ckfree(ime->preedit_string);
        ime->preedit_string = NULL;
    }
    ime->preedit_active = 0;
}

static void
IMETextInputDeleteSurroundingText(
    void *data,
    struct zwp_text_input_v3 *text_input,
    uint32_t before_length,
    uint32_t after_length)
{
    (void)data;
    (void)text_input;
    (void)before_length;
    (void)after_length;
    /* No-op */
}

static void
IMETextInputDone(
    void *data,
    struct zwp_text_input_v3 *text_input,
    uint32_t serial)
{
    TkIMEState *ime = (TkIMEState *)data;
    (void)text_input;
    (void)serial;
    
    if (!ime) {
        return;
    }
    
    /* Process commit */
    if (ime->commit_string) {
        SendIMECommitEvent(ime);
        ckfree(ime->commit_string);
        ime->commit_string = NULL;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * Tk_SetCaretPos --
 *
 *      Standard Tk API for setting caret position (for IME).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates IME cursor rectangle.
 *
 *---------------------------------------------------------------------------
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
