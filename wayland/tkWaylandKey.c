/*
 * tkWaylandKey.c –
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
#include <GLFW/glfw3native.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <X11/keysymdef.h>
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
 * **************************************************
 * Keyboard State Management
 * **************************************************
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
 * Keyboard repeat information from compositor.
 */
static int keyboard_repeat_rate = 0;   /* characters per second */
static int keyboard_repeat_delay = 0;  /* milliseconds before repeating */

/*
 * **************************************************
 * IME (Input Method Editor) Support
 * **************************************************
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

    int fd;                          /* File descriptor for event loop */
    Tcl_Channel channel;             /* Tcl channel for event handling */
    int display_initialized;         /* Flag for connection state */

    /* Hash table for window -> IME state mapping */
    TkIMEState **ime_hash;
    int hash_size;
    int hash_count;

    char *seat_name;                 /* Name of the seat (for debugging) */
} WaylandIMEConnection;

static WaylandIMEConnection wlIME = {
    NULL, NULL, NULL, NULL, NULL, -1, NULL, 0, NULL, 0, 0, NULL
};

/*
 * IME state per Tk window.
 */
typedef struct TkIMEState {
    struct zwp_text_input_v3 *text_input;
    struct wl_surface *surface;   /* Wayland surface from GLFW */
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
static Tk_Window focusedTkWin = NULL;  /* Currently focused Tk window */

/*
 * Sentinel keycode used to mark IME commit events so that TkpGetKeySym
 * and TkpGetString can distinguish them from normal key events without
 * hijacking any bits of event.xkey.state.
 *
 * 0xFFFE is safely outside the range of any real X11/evdev keycode.
 */
#define TK_WAYLAND_IME_KEYCODE  0xFFFEu

/*
 * Unicode codepoint for the IME commit event currently being processed.
 * Set by SendIMECommitEvent before queuing the event; consumed (cleared)
 * by TkpGetString after it has been read.
 */
static int imeCommitCodepoint = 0;

/* Hash table functions */
static unsigned int HashWindow(Tk_Window tkwin);
static TkIMEState *FindIMEState(Tk_Window tkwin);
static TkIMEState *FindIMEStateBySurface(struct wl_surface *surface);
static void StoreIMEState(Tk_Window tkwin, TkIMEState *ime);
static void RemoveIMEState(Tk_Window tkwin);
static void ResizeHashTableIfNeeded(void);

/* Helper for synthetic events */
static void SendKeySymEvent(Tk_Window tkwin, KeySym keysym, int pressed);

/*
 * **************************************************
 * Forward Declarations
 * **************************************************
 */

/* XKB functions. */
static int InitializeXKB(void);
static void CleanupXKB(void);
static void UpdateXKBModifiers(unsigned int mods_depressed,
        unsigned int mods_latched, unsigned int mods_locked,
        unsigned int group);
static unsigned int XKBGetModifierState(void);

/* IME functions. */
static int InitializeIME(void);
static void CleanupIME(void);
static void WaylandIMEEventHandler(void *clientData, int mask);
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
    .global        = RegistryHandleGlobal,
    .global_remove = RegistryHandleGlobalRemove,
};

static const struct wl_seat_listener seat_listener = {
    .capabilities = SeatHandleCapabilities,
    .name         = SeatHandleName,
};

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap      = KeyboardHandleKeymap,
    .enter       = KeyboardHandleEnter,
    .leave       = KeyboardHandleLeave,
    .key         = KeyboardHandleKey,
    .modifiers   = KeyboardHandleModifiers,
    .repeat_info = KeyboardHandleRepeatInfo,
};

static const struct zwp_text_input_v3_listener text_input_listener = {
    .enter                   = IMETextInputEnter,
    .leave                   = IMETextInputLeave,
    .preedit_string          = IMETextInputPreeditString,
    .commit_string           = IMETextInputCommitString,
    .delete_surrounding_text = IMETextInputDeleteSurroundingText,
    .done                    = IMETextInputDone,
};

static Time TkGetCurrentTimeMillis(void);

/*
 * **************************************************
 * TkGlfwGetFocusedChild --
 *
 *      Return the TkWindow that currently holds Tk's keyboard focus
 *      within the given toplevel window.  If no child widget has been
 *      focused yet, the toplevel itself is returned.
 *
 *      This is used by TkWaylandProcessKey, SendKeySymEvent, and
 *      SendIMECommitEvent to ensure that keyboard events are delivered
 *      to the widget that actually holds focus (e.g. an Entry or Text
 *      widget) rather than the containing toplevel.  Without this
 *      routing, bindings and key handlers on child widgets never fire.
 *
 * Results:
 *      Pointer to the focused TkWindow; never NULL if topPtr is non-NULL.
 *
 * Side effects:
 *      None.
 *
 * **************************************************
 */

MODULE_SCOPE TkWindow *
TkGlfwGetFocusedChild(TkWindow *topPtr)
{
    if (!topPtr) {
        return NULL;
    }

    /* Only valid for toplevels. */
    if (!Tk_IsTopLevel((Tk_Window)topPtr)) {
        return NULL;
    }

    /*
     * Ask Tk which window currently has focus on this display.
     * This uses Tk's internal focus tracking (dispPtr->focusPtr).
     */
    TkWindow *focusWin = TkGetFocusWin(topPtr);

    if (focusWin) {
        /*
         * Make sure the focused window belongs to this same
         * main window (important if multiple Tk apps share display).
         */
        if (focusWin->mainPtr == topPtr->mainPtr) {
            return focusWin;
        }
    }

    /* Fallback: return the toplevel itself. */
    return topPtr;
}

/*
 * **************************************************
 * TkpKeycodeToKeysym --
 *
 *      Convert a keycode to a keysym during early initialization.
 *      This provides a fallback when XKB state isn't ready yet.
 *
 *      Keycodes here are X11 keycodes (evdev + 8), which is the value
 *      stored in event.xkey.keycode throughout this file.  The table
 *      covers the keys most commonly needed during Tk's startup
 *      sequence (tk.tcl) to avoid "bad event type or keysym" errors.
 *
 * Results:
 *      Returns the KeySym for the given X11 keycode, or NoSymbol.
 *
 * Side effects:
 *      None.
 *
 * **************************************************
 */

static KeySym
TkpKeycodeToKeysym(unsigned int keycode)
{
    switch (keycode) {
        /* Control keys */
        case 9:   return XK_Escape;
        case 22:  return XK_BackSpace;
        case 23:  return XK_Tab;
        case 36:  return XK_Return;
        case 65:  return XK_space;

        /* Arrow keys - critical for tk.tcl initialization */
        case 111: return XK_Up;
        case 113: return XK_Left;
        case 114: return XK_Right;
        case 116: return XK_Down;

        /* Navigation keys */
        case 110: return XK_Home;
        case 112: return XK_Page_Up;
        case 115: return XK_End;
        case 117: return XK_Page_Down;
        case 118: return XK_Insert;
        case 119: return XK_Delete;

        /* Function keys */
        case 67:  return XK_F1;
        case 68:  return XK_F2;
        case 69:  return XK_F3;
        case 70:  return XK_F4;
        case 71:  return XK_F5;
        case 72:  return XK_F6;
        case 73:  return XK_F7;
        case 74:  return XK_F8;
        case 75:  return XK_F9;
        case 76:  return XK_F10;
        case 95:  return XK_F11;
        case 96:  return XK_F12;

        default:
            /* Letters a-z (QWERTY layout) */
            if (keycode >= 38 && keycode <= 63) {
                return XK_a + (keycode - 38);
            }
            return NoSymbol;
    }
}

/*
 * **************************************************
 * XStringToKeysym --
 *
 *      Convert a keysym name string to a KeySym value.
 *      This is a Wayland/XKB implementation of the X11 function.
 *
 * Results:
 *      Returns the KeySym corresponding to the name, or NoSymbol
 *      if the name is not recognized.
 *
 * Side effects:
 *      None.
 *
 * **************************************************
 */

KeySym
XStringToKeysym(_Xconst char *string)
{
    xkb_keysym_t keysym;

    if (!string || !*string) {
        return NoSymbol;
    }

    /*
     * Use xkbcommon to convert string to keysym.
     * This handles standard X11 keysym names like "Right", "Left", etc.
     */
    keysym = xkb_keysym_from_name(string, XKB_KEYSYM_NO_FLAGS);

    /*
     * If case-sensitive lookup fails, try case-insensitive.
     * This matches X11 XStringToKeysym behavior.
     */
    if (keysym == XKB_KEY_NoSymbol) {
        keysym = xkb_keysym_from_name(string, XKB_KEYSYM_CASE_INSENSITIVE);
    }

    return (KeySym)keysym;
}

/*
 * **************************************************
 * XKeysymToString --
 *
 *      Convert a KeySym value to its string name.
 *      This is a Wayland/XKB implementation of the X11 function.
 *
 * Results:
 *      Returns a pointer to a static string containing the keysym name,
 *      or NULL if the keysym is not recognized.
 *
 * Side effects:
 *      The returned string is stored in a static buffer.
 *
 * **************************************************
 */

char *
XKeysymToString(KeySym keysym)
{
    static char buffer[64];

    /*
     * Use xkbcommon to convert keysym to string.
     */
    int result = xkb_keysym_get_name((xkb_keysym_t)keysym, buffer, sizeof(buffer));
    
    if (result > 0) {
        return buffer;
    }
    
    return NULL;
}

/*
 *---------------------------------------------------------------------------
 *
 * XKB Keyboard Handling
 * **************************************************
 */

/*
 * **************************************************
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
 * **************************************************
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
    if (!locale) locale = getenv("LC_CTYPE");
    if (!locale) locale = getenv("LANG");
    if (!locale) locale = "C";

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
 * **************************************************
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
 * **************************************************
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
 * **************************************************
 * TkWaylandSetKeymap --
 *
 *      Set XKB keymap from file descriptor (called by Wayland seat via
 *      KeyboardHandleKeymap when the compositor sends the keymap).
 *
 *      Keycode convention used throughout this file:
 *
 *        raw evdev code  — what wl_keyboard.key delivers from the compositor
 *        X11 keycode     — evdev + 8  (stored in event.xkey.keycode)
 *        XKB keycode     — evdev + 8  (xkbcommon's native format)
 *
 *      Because X11 keycodes and XKB keycodes are identical (both = evdev+8),
 *      event.xkey.keycode can be passed directly to xkbcommon without any
 *      offset adjustment.  TkpGetKeySym and TkpGetString therefore call
 *      xkb_state_key_get_one_sym / xkb_state_key_get_utf8 with
 *      keyPtr->keycode directly — no subtraction of 8.
 *
 * Results:
 *      Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *      Updates XKB keymap and state.
 *
 * **************************************************
 */

int
TkWaylandSetKeymap(int fd, uint32_t size)
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
            xkbState.context, map_str,
            XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);

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
 * **************************************************
 * UpdateXKBModifiers --
 *
 *      Update XKB modifier state from values delivered by the compositor
 *      via the wl_keyboard.modifiers event.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates XKB state with new modifiers.
 *
 * **************************************************
 */

static void
UpdateXKBModifiers(unsigned int mods_depressed, unsigned int mods_latched,
        unsigned int mods_locked, unsigned int group)
{
    if (!xkbState.state) {
        return;
    }

    xkbState.modifiers_depressed = mods_depressed;
    xkbState.modifiers_latched   = mods_latched;
    xkbState.modifiers_locked    = mods_locked;
    xkbState.group               = group;

    xkb_state_update_mask(xkbState.state,
            mods_depressed, mods_latched, mods_locked,
            0, 0, group);
}

/*
 * **************************************************
 * XKBGetModifierState --
 *
 *      Return the current XKB effective modifier state as an X11
 *      modifier mask suitable for placing in event.xkey.state.
 *
 * Results:
 *      Returns X11 modifier mask.
 *
 * Side effects:
 *      None.
 *
 * **************************************************
 */

static unsigned int
XKBGetModifierState(void)
{
    unsigned int mask = 0;

    if (!xkbState.state || !xkbState.keymap) {
        return 0;
    }

#define MOD_ACTIVE(xkb_name, x11_mask)                                   \
    if (xkb_state_mod_index_is_active(xkbState.state,                    \
            xkb_keymap_mod_get_index(xkbState.keymap, (xkb_name)),       \
            XKB_STATE_MODS_EFFECTIVE) > 0) {                             \
        mask |= (x11_mask);                                              \
    }

    MOD_ACTIVE(XKB_MOD_NAME_SHIFT, ShiftMask)
    MOD_ACTIVE(XKB_MOD_NAME_CAPS,  LockMask)
    MOD_ACTIVE(XKB_MOD_NAME_CTRL,  ControlMask)
    MOD_ACTIVE(XKB_MOD_NAME_ALT,   Mod1Mask)
    MOD_ACTIVE(XKB_MOD_NAME_LOGO,  Mod4Mask)

#undef MOD_ACTIVE

    return mask;
}

/*
 * **************************************************
 * TkWaylandProcessKey --
 *
 *      Process a key press or release event originating from the
 *      Wayland wl_keyboard listener (KeyboardHandleKey).
 *
 *      This is the PRIMARY key event path.  The GLFW key callback
 *      (TkGlfwKeyCallback in tkWaylandEvent.c) must have its
 *      glfwSetKeyCallback and glfwSetCharCallback registrations
 *      removed from TkGlfwSetupCallbacks to prevent duplicate events:
 *
 *          Remove:  glfwSetKeyCallback(glfwWindow, TkGlfwKeyCallback);
 *          Remove:  glfwSetCharCallback(glfwWindow, TkGlfwCharCallback);
 *
 *      Keycode encoding:
 *        The Wayland compositor delivers raw evdev keycodes.
 *        We store (evdev + 8) in event.xkey.keycode, making it an
 *        X11/XKB keycode.  TkpGetKeySym and TkpGetString then pass
 *        this value directly to xkbcommon without any further offset.
 *
 *      Focus routing:
 *        Key events are delivered to the focused child widget rather
 *        than the toplevel so that Entry, Text, and other widgets
 *        receive keyboard input and their bindings fire correctly.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Queues a KeyPress or KeyRelease XEvent on Tk's event queue.
 *
 * **************************************************
 */

void
TkWaylandProcessKey(Tk_Window tkwin, unsigned int keycode, int pressed,
        unsigned int time)
{
    XEvent event;
    TkIMEState *ime;
    TkWindow *winPtr;
    TkWindow *focusWin;

    if (!tkwin) {
        return;
    }

    /* Check if IME is active - if so, let it handle the key. */
    ime = FindIMEState(tkwin);
    if (ime && ime->enabled && ime->preedit_active) {
        /* IME is handling composition, don't send raw key events. */
        return;
    }

    winPtr   = (TkWindow *)tkwin;
    focusWin = TkGlfwGetFocusedChild(winPtr);

    /* Build X event. */
    memset(&event, 0, sizeof(XEvent));
    event.xkey.type       = pressed ? KeyPress : KeyRelease;
    event.xkey.serial     = 0;
    event.xkey.send_event = False;
    event.xkey.display    = Tk_Display(tkwin);

    /*
     * Route to the focused child widget so that Entry/Text widgets
     * receive keyboard events rather than just the toplevel.
     */
    event.xkey.window      = Tk_WindowId((Tk_Window)focusWin);
    event.xkey.root        = RootWindow(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
    event.xkey.subwindow   = None;
    event.xkey.time        = time;
    event.xkey.x           = 0;
    event.xkey.y           = 0;
    event.xkey.x_root      = 0;
    event.xkey.y_root      = 0;
    event.xkey.state       = XKBGetModifierState();

    /*
     * Store the X11/XKB keycode (evdev + 8).  xkbcommon expects XKB
     * keycodes which equal evdev + 8, so TkpGetKeySym and TkpGetString
     * can pass event.xkey.keycode directly without any offset adjustment.
     */
    event.xkey.keycode     = keycode + 8;
    event.xkey.same_screen = True;

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 * **************************************************
 * IME (Input Method Editor) Implementation
 * **************************************************
 */

/*
 * **************************************************
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
 * **************************************************
 */

static unsigned int
HashWindow(Tk_Window tkwin)
{
    uintptr_t ptr = (uintptr_t)tkwin;
    return (unsigned int)((ptr >> 4) ^ ptr);
}

/*
 * **************************************************
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
 * **************************************************
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
    wlIME.ime_hash  = new_hash;
    wlIME.hash_size = new_size;
}

/*
 * **************************************************
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
 * **************************************************
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
    for (ime = wlIME.ime_hash[hash]; ime; ime = ime->next) {
        if (ime->tkwin == tkwin) {
            return ime;
        }
    }

    return NULL;
}

/*
 * **************************************************
 * FindIMEStateBySurface --
 *
 *      Find IME state for a Wayland surface by scanning hash table.
 *
 * Results:
 *      Returns IME state or NULL.
 *
 * Side effects:
 *      None.
 *
 * **************************************************
 */

static TkIMEState *
FindIMEStateBySurface(struct wl_surface *surface)
{
    int i;
    TkIMEState *ime;

    if (!wlIME.ime_hash || !surface) {
        return NULL;
    }

    for (i = 0; i < wlIME.hash_size; i++) {
        for (ime = wlIME.ime_hash[i]; ime; ime = ime->next) {
            if (ime->surface == surface) {
                return ime;
            }
        }
    }

    return NULL;
}

/*
 * **************************************************
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
 * **************************************************
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
 * **************************************************
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
 * **************************************************
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
    for (ime = wlIME.ime_hash[hash]; ime; ime = ime->next) {
        if (ime->tkwin == tkwin) {
            if (prev) prev->next = ime->next;
            else      wlIME.ime_hash[hash] = ime->next;
            wlIME.hash_count--;
            return;
        }
        prev = ime;
    }
}

/*
 * **************************************************
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
 * **************************************************
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
    wlIME.hash_size  = 64;
    wlIME.hash_count = 0;
    wlIME.ime_hash   = (TkIMEState **)ckalloc(sizeof(TkIMEState *) * wlIME.hash_size);
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
    wlIME.channel = Tcl_MakeFileChannel(
            (void *)(intptr_t)wlIME.fd, TCL_READABLE);
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
 * **************************************************
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
 * **************************************************
 */

static void
CleanupIME(void)
{
    int i;

    wlIME.display_initialized = 0;

    if (wlIME.channel) {
        Tcl_DeleteChannelHandler(wlIME.channel, WaylandIMEEventHandler, NULL);
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

    if (wlIME.seat_name) {
        ckfree(wlIME.seat_name);
        wlIME.seat_name = NULL;
    }

    if (wlIME.display) {
        wl_display_disconnect(wlIME.display);
        wlIME.display = NULL;
    }

    wlIME.fd         = -1;
    wlIME.hash_size  = 0;
    wlIME.hash_count = 0;
}

/*
 * **************************************************
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
 * **************************************************
 */

static int
WaylandIMEDispatchEvents(void)
{
    int ret;

    if (!wlIME.display || !wlIME.display_initialized) {
        return 0;
    }

    /* Prepare to read events. */
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
 * **************************************************
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
 * **************************************************
 */

static void
WaylandIMEEventHandler(void *clientData, int mask)
{
    (void)clientData;
    (void)mask;
    WaylandIMEDispatchEvents();
}

/*
 * **************************************************
 * Wayland Registry Callbacks --
 * **************************************************
 */

static void
RegistryHandleGlobal(TCL_UNUSED(void *), 
	struct wl_registry *registry,
	uint32_t name, 
	const char *interface, 
	uint32_t version)
{
    if (strcmp(interface, "wl_seat") == 0) {
        wlIME.seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        wl_seat_add_listener(wlIME.seat, &seat_listener, NULL);
    } else if (strcmp(interface, zwp_text_input_manager_v3_interface.name) == 0) {
        /* Use version 1 for maximum compatibility. */
        wlIME.text_input_manager = wl_registry_bind(registry, name,
                &zwp_text_input_manager_v3_interface,
                (version < 1) ? version : 1);
    }
}

static void
RegistryHandleGlobalRemove(TCL_UNUSED(void *),
	TCL_UNUSED(struct wl_registry *), 
	TCL_UNUSED(uint32_t))
{
    /*
     * If the removed global is the seat or text input manager,
     * we should clean up related resources. For simplicity, we
     * ignore this for now; if the seat disappears, the compositor
     * is likely shutting down.
     */
}

static void
SeatHandleCapabilities(TCL_UNUSED(void *), 
		struct wl_seat *seat,
		uint32_t capabilities)
{
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !wlIME.keyboard) {
        wlIME.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(wlIME.keyboard, &keyboard_listener, NULL);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && wlIME.keyboard) {
        wl_keyboard_destroy(wlIME.keyboard);
        wlIME.keyboard = NULL;
    }
}

static void
SeatHandleName(TCL_UNUSED(void *), 
		TCL_UNUSED(struct wl_seat *),
		const char *name)
{
    /* Store seat name for debugging purposes. */
    if (wlIME.seat_name) {
        ckfree(wlIME.seat_name);
    }
    wlIME.seat_name = (char *)ckalloc(strlen(name) + 1);
    strcpy(wlIME.seat_name, name);
}

/*
 * **************************************************
 * Keyboard Callbacks --
 * **************************************************
 */

static void
KeyboardHandleKeymap(TCL_UNUSED(void *), 
		TCL_UNUSED(struct wl_keyboard *),
        uint32_t format, 
        int32_t fd, 
        uint32_t size)
{
    if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        TkWaylandSetKeymap(fd, size);
    } else {
        close(fd);
    }
}

static void
KeyboardHandleEnter(TCL_UNUSED(void *), 
		TCL_UNUSED(struct wl_keyboard *),
        TCL_UNUSED(uint32_t), 
        struct wl_surface *surface,
        TCL_UNUSED(struct wl_array *))
{
    /* Find which Tk window this surface belongs to. */
    TkIMEState *ime = FindIMEStateBySurface(surface);
    focusedTkWin = ime ? ime->tkwin : NULL;
}

static void
KeyboardHandleLeave(TCL_UNUSED(void *),
		TCL_UNUSED(struct wl_keyboard *),
        TCL_UNUSED(uint32_t), 
        TCL_UNUSED(struct wl_surface *))
{
    focusedTkWin = NULL;
}

/*
 * **************************************************
 * KeyboardHandleKey --
 *
 *      Called by the separate IME Wayland connection's wl_keyboard for
 *      every key event.  Delegates to TkWaylandProcessKey which handles
 *      keycode encoding and focus routing.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Queues a KeyPress/KeyRelease event via TkWaylandProcessKey.
 *
 * **************************************************
 */
 
static void
KeyboardHandleKey(TCL_UNUSED(void *), 
		TCL_UNUSED(struct wl_keyboard *),
        TCL_UNUSED(uint32_t), 
        uint32_t time, 
        uint32_t key, 
        uint32_t state)
{
    if (focusedTkWin) {
        /* key is a raw evdev code; TkWaylandProcessKey adds 8. */
        TkWaylandProcessKey(focusedTkWin, key,
                (state == WL_KEYBOARD_KEY_STATE_PRESSED), time);
    }
}

/* Helper functions. */
static void
KeyboardHandleModifiers(TCL_UNUSED(void *), 
		TCL_UNUSED(struct wl_keyboard *),
        TCL_UNUSED(uint32_t), 
		uint32_t mods_depressed, 
		uint32_t mods_latched,
        uint32_t mods_locked, 
        uint32_t group)
{
    UpdateXKBModifiers(mods_depressed, mods_latched, mods_locked, group);
}

static void
KeyboardHandleRepeatInfo(TCL_UNUSED(void *), 
		TCL_UNUSED(struct wl_keyboard *),
        int32_t rate, 
        int32_t delay)
{
    /* Store repeat information for potential use. */
    keyboard_repeat_rate  = rate;
    keyboard_repeat_delay = delay;

    /*
     * Note: Tk handles autorepeat internally via timer events,
     * but we could use this info to adjust the repeat behavior.
     * For now, we just store it.
     */
}

/*
 * **************************************************
 * IME State Management --
 * **************************************************
 */

/*
 * **************************************************
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
 * **************************************************
 */

static TkIMEState *
CreateIMEState(Tk_Window tkwin)
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

    /* Get Wayland surface from GLFW using native access. */
    surface = glfwGetWaylandWindow(glfwWindow);
    if (!surface) {
        return NULL;
    }

    ime = (TkIMEState *)ckalloc(sizeof(TkIMEState));
    memset(ime, 0, sizeof(TkIMEState));

    ime->tkwin   = tkwin;
    ime->surface = surface;  /* Store GLFW's Wayland surface. */

    /* Create text-input object using the seat from our separate connection. */
    ime->text_input = zwp_text_input_manager_v3_get_text_input(
            wlIME.text_input_manager, wlIME.seat);

    if (!ime->text_input) {
        ckfree((char *)ime);
        return NULL;
    }

    /* Get keyboard for this seat. */
    ime->keyboard = wlIME.keyboard;

    zwp_text_input_v3_add_listener(ime->text_input, &text_input_listener, ime);
    return ime;
}

/*
 * **************************************************
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
 * **************************************************
 */

static void
DestroyIMEState(TkIMEState *ime)
{
    if (!ime) {
        return;
    }

    if (ime == currentIME) {
        currentIME = NULL;
    }

    if (ime->text_input)    zwp_text_input_v3_destroy(ime->text_input);
    if (ime->preedit_string) ckfree(ime->preedit_string);
    if (ime->commit_string)  ckfree(ime->commit_string);

    /* Note: We don't destroy ime->surface or ime->keyboard
     * - they are owned elsewhere. */

    ckfree((char *)ime);
}

/*
 * **************************************************
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
 * **************************************************
 */

int
TkWaylandIMEEnable(Tk_Window tkwin)
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
        currentIME   = ime;
    }

    return ime->enabled;
}

/*
 * **************************************************
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
 * **************************************************
 */

void
TkWaylandIMEDisable(Tk_Window tkwin)
{
    TkIMEState *ime;

    if (!tkwin) {
        return;
    }

    ime = FindIMEState(tkwin);
    if (!ime || !ime->enabled) {
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

    ime->enabled        = 0;
    ime->preedit_active = 0;

    if (currentIME == ime) {
        currentIME = NULL;
    }
}

/*
 * **************************************************
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
 * **************************************************
 */

void
TkWaylandIMESetCursorRect(Tk_Window tkwin, int x, int y, int width, int height)
{
    TkIMEState *ime;

    if (!tkwin) {
        return;
    }

    ime = FindIMEState(tkwin);
    if (!ime) {
        return;
    }

    ime->cursor_x      = x;
    ime->cursor_y      = y;
    ime->cursor_width  = width;
    ime->cursor_height = height;

    if (ime->enabled && ime->text_input && ime->surface) {
        zwp_text_input_v3_set_cursor_rectangle(ime->text_input,
                x, y, width, height);
        zwp_text_input_v3_commit(ime->text_input);
    }
}

/*
 * **************************************************
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
 * **************************************************
 */

const char *
TkWaylandIMEGetPreedit(Tk_Window tkwin, int *cursorPos)
{
    TkIMEState *ime;

    if (!tkwin) {
        return NULL;
    }

    ime = FindIMEState(tkwin);
    if (!ime || !ime->preedit_active || !ime->preedit_string) {
        return NULL;
    }

    if (cursorPos) {
        *cursorPos = ime->preedit_cursor;
    }

    return ime->preedit_string;
}

/*
 * **************************************************
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
 * **************************************************
 */

void
TkWaylandIMESetSurroundingText(Tk_Window tkwin, const char *text,
        int cursor_index, int anchor_index)
{
    TkIMEState *ime;

    if (!tkwin) {
        return;
    }

    ime = FindIMEState(tkwin);
    if (!ime || !ime->enabled || !ime->text_input || !ime->surface) {
        return;
    }

    zwp_text_input_v3_set_surrounding_text(ime->text_input,
            text ? text : "", cursor_index, anchor_index);
    zwp_text_input_v3_commit(ime->text_input);
}

/*
 * **************************************************
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
 * **************************************************
 */

void
TkWaylandIMESetContentType(Tk_Window tkwin, uint32_t hint, uint32_t purpose)
{
    TkIMEState *ime;

    if (!tkwin) {
        return;
    }

    ime = FindIMEState(tkwin);
    if (!ime || !ime->enabled || !ime->text_input || !ime->surface) {
        return;
    }

    zwp_text_input_v3_set_content_type(ime->text_input, hint, purpose);
    zwp_text_input_v3_commit(ime->text_input);
}

/*
 * **************************************************
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
 * **************************************************
 */

void
TkWaylandIMERemove(Tk_Window tkwin)
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
 * **************************************************
 * SendIMEPreeditEvent --
 *
 *      Send preedit change notification to trigger widget redraw.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Generates custom event for preedit update.
 *
 * **************************************************
 */

static void
SendIMEPreeditEvent(TkIMEState *ime)
{
    if (!ime || !ime->tkwin) {
        return;
    }

    /* Trigger widget redraw to show preedit. */
    Tcl_DoOneEvent(TCL_ALL_EVENTS);
}

/*
 * **************************************************
 * SendKeySymEvent --
 *
 *      Helper to generate a synthetic KeyPress/KeyRelease event for a
 *      keysym.  Routes the event to the focused child widget rather than
 *      the toplevel so that bindings on Entry/Text widgets fire correctly.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Queues X KeyPress/KeyRelease events.
 *
 * **************************************************
 */

static void
SendKeySymEvent(Tk_Window tkwin, KeySym keysym, int pressed)
{
    XEvent event;
    TkWindow *winPtr;
    TkWindow *focusWin;

    if (!tkwin) {
        return;
    }

    winPtr   = (TkWindow *)tkwin;
    focusWin = TkGlfwGetFocusedChild(winPtr);

    memset(&event, 0, sizeof(XEvent));
    event.xkey.type        = pressed ? KeyPress : KeyRelease;
    event.xkey.display     = Tk_Display(tkwin);
    event.xkey.window      = Tk_WindowId((Tk_Window)focusWin);
    event.xkey.root        = RootWindow(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
    event.xkey.time        = TkGetCurrentTimeMillis();
    event.xkey.state       = XKBGetModifierState();
    event.xkey.same_screen = True;

    /* Set keycode and state using TkpSetKeycodeAndState. */
    TkpSetKeycodeAndState(tkwin, keysym, &event);

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/*
 * **************************************************
 * SendIMECommitEvent --
 *
 *      Inject committed IME text as KeyPress/KeyRelease event pairs,
 *      one pair per Unicode codepoint in the commit string.
 *
 *      Design note — why we use a sentinel keycode rather than encoding
 *      the codepoint in event.xkey.keycode directly:
 *
 *        The original approach packed a Unicode codepoint into
 *        event.xkey.keycode and a UTF-8 byte-length into bits 16-23 of
 *        event.xkey.state.  This was fragile for two reasons:
 *          (a) Any modifier bits above bit 15 would silently collide.
 *          (b) TkpGetKeySym passes event.xkey.keycode to xkbcommon
 *              expecting an X11/XKB keycode, not a Unicode codepoint,
 *              so the translation produced garbage keysyms.
 *
 *        New approach: a dedicated module-level variable
 *        (imeCommitCodepoint) carries the codepoint.  The event's
 *        keycode is set to TK_WAYLAND_IME_KEYCODE (0xFFFE), a value
 *        outside the range of all real X11/evdev keycodes.  Both
 *        TkpGetKeySym and TkpGetString detect this sentinel and read
 *        from imeCommitCodepoint instead of consulting xkbcommon.
 *        TkpGetString clears imeCommitCodepoint after consuming it.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Sets imeCommitCodepoint and queues KeyPress/KeyRelease events.
 *
 * **************************************************
 */

static void
SendIMECommitEvent(TkIMEState *ime)
{
    XEvent event;
    const char *p;
    Tcl_UniChar ch;
    int utf8_len;
    TkWindow *winPtr;
    TkWindow *focusWin;

    if (!ime || !ime->commit_string || !ime->tkwin) {
        return;
    }

    winPtr   = (TkWindow *)ime->tkwin;
    focusWin = TkGlfwGetFocusedChild(winPtr);

    /* Generate one KeyPress/KeyRelease pair per Unicode codepoint. */
    p = ime->commit_string;
    while (*p) {
        utf8_len = Tcl_UtfToUniChar(p, &ch);

        /* Store codepoint for TkpGetKeySym / TkpGetString to consume. */
        imeCommitCodepoint = (int)ch;

        memset(&event, 0, sizeof(XEvent));
        event.xkey.type        = KeyPress;
        event.xkey.display     = Tk_Display(ime->tkwin);
        event.xkey.window      = Tk_WindowId((Tk_Window)focusWin);
        event.xkey.root        = RootWindow(Tk_Display(ime->tkwin),
                                             Tk_ScreenNumber(ime->tkwin));
        event.xkey.time        = TkGetCurrentTimeMillis();
        event.xkey.state       = XKBGetModifierState();
        event.xkey.keycode     = TK_WAYLAND_IME_KEYCODE;
        event.xkey.same_screen = True;

        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

        event.xkey.type = KeyRelease;
        Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);

        p += utf8_len;
    }
}

/*
 * **************************************************
 * Wayland Text-Input Callbacks --
 * **************************************************
 */

static void
IMETextInputEnter(void *data, struct zwp_text_input_v3 *text_input,
        struct wl_surface *surface)
{
    TkIMEState *ime = (TkIMEState *)data;
    (void)text_input;

    if (ime && ime->surface == surface) {
        ime->enabled = 1;
        currentIME   = ime;
    }
}

static void
IMETextInputLeave(void *data, struct zwp_text_input_v3 *text_input,
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
IMETextInputPreeditString(void *data, struct zwp_text_input_v3 *text_input,
        const char *text, int32_t cursor_begin, int32_t cursor_end)
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
IMETextInputCommitString(void *data, struct zwp_text_input_v3 *text_input,
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

    /* Clear preedit on commit. */
    if (ime->preedit_string) {
        ckfree(ime->preedit_string);
        ime->preedit_string = NULL;
    }
    ime->preedit_active = 0;
}

static void
IMETextInputDeleteSurroundingText(void *data,
        struct zwp_text_input_v3 *text_input,
        uint32_t before_length, uint32_t after_length)
{
    TkIMEState *ime = (TkIMEState *)data;
    uint32_t i;
    (void)text_input;

    if (!ime || !ime->tkwin) {
        return;
    }

    /*
     * Simulate deletion by sending Backspace (for before_length)
     * and Delete (for after_length) key events.
     */

    /* Delete characters before cursor (Backspace). */
    for (i = 0; i < before_length; i++) {
        SendKeySymEvent(ime->tkwin, XK_BackSpace, 1);  /* Press */
        SendKeySymEvent(ime->tkwin, XK_BackSpace, 0);  /* Release */
    }

    /* Delete characters after cursor (Delete). */
    for (i = 0; i < after_length; i++) {
        SendKeySymEvent(ime->tkwin, XK_Delete, 1);
        SendKeySymEvent(ime->tkwin, XK_Delete, 0);
    }
}

static void
IMETextInputDone(void *data, struct zwp_text_input_v3 *text_input,
        uint32_t serial)
{
    TkIMEState *ime = (TkIMEState *)data;
    (void)text_input;
    (void)serial;

    if (!ime) {
        return;
    }

    /* Process commit string accumulated by IMETextInputCommitString. */
    if (ime->commit_string) {
        SendIMECommitEvent(ime);
        ckfree(ime->commit_string);
        ime->commit_string = NULL;
    }
}

/*
 * **************************************************
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
 * **************************************************
 */

void
Tk_SetCaretPos(Tk_Window tkwin, int x, int y, int height)
{
    TkWaylandIMESetCursorRect(tkwin, x, y, 1, height);
}

/*
 * **************************************************
 * TkpGetKeySym --
 *
 *      Given a KeyPress or KeyRelease event, map the keycode to a KeySym.
 *
 *      Keycode convention:
 *        Normal keys:   event.xkey.keycode = evdev + 8  (= XKB keycode).
 *                       Passed directly to xkb_state_key_get_one_sym
 *                       without any offset adjustment.
 *        IME commits:   event.xkey.keycode = TK_WAYLAND_IME_KEYCODE.
 *                       The codepoint is read from imeCommitCodepoint.
 *
 *      The original code subtracted 8 from keycode before calling
 *      xkbcommon.  That was wrong: xkbcommon expects XKB keycodes
 *      (= evdev + 8), not raw evdev codes.  The subtraction has been
 *      removed.
 *
 * Results:
 *      Returns the KeySym corresponding to the event, or NoSymbol.
 *
 * Side effects:
 *      None.
 *
 * **************************************************
 */

KeySym
TkpGetKeySym(TCL_UNUSED(TkDisplay *), XEvent *eventPtr)
{
    XKeyEvent *keyPtr;
    xkb_keysym_t keysym;

    if (eventPtr->type != KeyPress && eventPtr->type != KeyRelease) {
        return NoSymbol;
    }

    keyPtr = &eventPtr->xkey;

    /* IME commit events carry the codepoint in imeCommitCodepoint. */
    if (keyPtr->keycode == TK_WAYLAND_IME_KEYCODE) {
        return (imeCommitCodepoint > 0)
            ? (KeySym)imeCommitCodepoint : NoSymbol;
    }

    /*
     * XKB not yet loaded — use static fallback table.
     * This ensures keysyms work during Tk initialization (tk.tcl)
     * and fixes the "bad event type or keysym" error.
     */
    if (!xkbState.state) {
        return TkpKeycodeToKeysym(keyPtr->keycode);
    }

    /*
     * Pass keycode directly to xkbcommon.  event.xkey.keycode is an
     * X11/XKB keycode (evdev + 8), which is exactly what
     * xkb_state_key_get_one_sym expects.  No subtraction needed.
     */
    keysym = xkb_state_key_get_one_sym(xkbState.state, keyPtr->keycode);

    /* Handle compose sequences if available. */
    if (xkbState.compose_state && keysym != XKB_KEY_NoSymbol) {
        if (xkb_compose_state_feed(xkbState.compose_state, keysym)
                == XKB_COMPOSE_FEED_ACCEPTED) {
            enum xkb_compose_status status =
                xkb_compose_state_get_status(xkbState.compose_state);
            if (status == XKB_COMPOSE_COMPOSED) {
                keysym = xkb_compose_state_get_one_sym(xkbState.compose_state);
                xkb_compose_state_reset(xkbState.compose_state);
            } else if (status == XKB_COMPOSE_CANCELLED) {
                xkb_compose_state_reset(xkbState.compose_state);
            }
        }
    }

    return (KeySym)keysym;
}

/*
 * **************************************************
 * TkpGetString --
 *
 *      Retrieve the UTF-8 string generated by a KeyPress event.
 *
 *      IME commits:  read codepoint from imeCommitCodepoint, convert to
 *                    UTF-8, then clear imeCommitCodepoint.
 *      Normal keys:  call xkb_state_key_get_utf8 with the XKB keycode
 *                    (event.xkey.keycode, passed without any offset).
 *
 *      The original code subtracted 8 from keycode before calling
 *      xkbcommon.  That was wrong and has been removed.  The original
 *      IME path packed a UTF-8 length into bits 16-23 of event.xkey.state
 *      and a codepoint into event.xkey.keycode; that fragile encoding
 *      has been replaced by the imeCommitCodepoint sentinel mechanism.
 *
 * Results:
 *      Returns a pointer to the UTF-8 string stored in dsPtr.
 *
 * Side effects:
 *      Clears imeCommitCodepoint after consuming it.
 *
 * **************************************************
 */

const char *
TkpGetString(TCL_UNUSED(TkWindow *), XEvent *eventPtr, Tcl_DString *dsPtr)
{
    XKeyEvent *keyPtr;
    char buf[64];
    int len;

    Tcl_DStringInit(dsPtr);

    if (eventPtr->type != KeyPress) {
        return "";
    }

    keyPtr = &eventPtr->xkey;

    /* IME commit path — codepoint was stored by SendIMECommitEvent. */
    if (keyPtr->keycode == TK_WAYLAND_IME_KEYCODE) {
        if (imeCommitCodepoint > 0) {
            len = Tcl_UniCharToUtf((Tcl_UniChar)imeCommitCodepoint, buf);
            if (len > 0) {
                buf[len] = '\0';
                Tcl_DStringAppend(dsPtr, buf, len);
            }
            imeCommitCodepoint = 0;   /* Consume the codepoint. */
        }
        return Tcl_DStringValue(dsPtr);
    }

    /* Normal key path via XKB. */
    if (!xkbState.state) {
        return "";
    }

    /*
     * Pass keycode directly — it is already an XKB keycode (evdev + 8).
     * No subtraction of 8 needed or correct here.
     */
    len = xkb_state_key_get_utf8(xkbState.state, keyPtr->keycode,
                                  buf, sizeof(buf));
    if (len > 0) {
        Tcl_DStringAppend(dsPtr, buf, len);
    }

    return Tcl_DStringValue(dsPtr);
}

/*
 * **************************************************
 * TkpSetKeycodeAndState --
 *
 *      Given a keysym and modifier state, find an X11 keycode that will
 *      generate that keysym and write it into the event structure.
 *      Used by [event generate] and the IME surrogate-key helpers.
 *
 *      The original implementation used a hard-coded QWERTY offset table
 *      that mapped letters alphabetically (keysym-'a'+38) rather than by
 *      physical key position.  This produced wrong keycodes for almost
 *      every letter except 'a'.  The new implementation scans the live
 *      XKB keymap to find the correct keycode for any keysym on the
 *      user's current layout, falling back to a hand-coded table of
 *      common non-printable keys only when the keymap is not yet loaded.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates eventPtr->xkey.keycode and eventPtr->xkey.state.
 *
 * **************************************************
 */

void
TkpSetKeycodeAndState(TCL_UNUSED(Tk_Window), 
	KeySym keysym, 
	XEvent *eventPtr)
{
    /* Scan the live XKB keymap for the correct keycode. */
    if (xkbState.keymap) {
        xkb_keycode_t min_kc = xkb_keymap_min_keycode(xkbState.keymap);
        xkb_keycode_t max_kc = xkb_keymap_max_keycode(xkbState.keymap);
        xkb_keycode_t kc;

        for (kc = min_kc; kc <= max_kc; kc++) {
            xkb_layout_index_t num_layouts =
                xkb_keymap_num_layouts_for_key(xkbState.keymap, kc);
            xkb_layout_index_t layout;

            for (layout = 0; layout < num_layouts; layout++) {
                xkb_level_index_t num_levels =
                    xkb_keymap_num_levels_for_key(xkbState.keymap, kc, layout);
                xkb_level_index_t level;

                for (level = 0; level < num_levels; level++) {
                    const xkb_keysym_t *syms;
                    int n, i;

                    n = xkb_keymap_key_get_syms_by_level(
                            xkbState.keymap, kc, layout, level, &syms);

                    for (i = 0; i < n; i++) {
                        if (syms[i] == (xkb_keysym_t)keysym) {
                            /*
                             * kc is an XKB/X11 keycode (evdev + 8).
                             * Set ShiftMask if the keysym is at level 1.
                             */
                            eventPtr->xkey.keycode = kc;
                            if (level == 1) {
                                eventPtr->xkey.state |= ShiftMask;
                            }
                            return;
                        }
                    }
                }
            }
        }
    }

    /*
     * Fallback for common keys when the XKB keymap is not yet loaded.
     * These are X11 keycodes (evdev + 8) for standard US-QWERTY layout.
     */
    switch (keysym) {
    case XK_Return:    eventPtr->xkey.keycode = 36;  break;
    case XK_Escape:    eventPtr->xkey.keycode = 9;   break;
    case XK_BackSpace: eventPtr->xkey.keycode = 22;  break;
    case XK_Tab:       eventPtr->xkey.keycode = 23;  break;
    case XK_space:     eventPtr->xkey.keycode = 65;  break;
    case XK_Left:      eventPtr->xkey.keycode = 113; break;
    case XK_Right:     eventPtr->xkey.keycode = 114; break;
    case XK_Up:        eventPtr->xkey.keycode = 111; break;
    case XK_Down:      eventPtr->xkey.keycode = 116; break;
    case XK_Home:      eventPtr->xkey.keycode = 110; break;
    case XK_End:       eventPtr->xkey.keycode = 115; break;
    case XK_Delete:    eventPtr->xkey.keycode = 119; break;
    case XK_Insert:    eventPtr->xkey.keycode = 118; break;
    default:
        /* Last-resort: encode the low byte of the keysym. */
        eventPtr->xkey.keycode = keysym & 0xFFu;
        break;
    }
}

/*
 * **************************************************
 * TkpInitKeymapInfo --
 *
 *      This procedure is invoked to scan keymap information to
 *      recompute stuff that's important for binding, such as
 *      determining what modifier is associated with "mode switch".
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Keymap-related information in dispPtr is updated.
 *
 * **************************************************
 */

void
TkpInitKeymapInfo(TkDisplay *dispPtr)
{
    /*
     * Set up default modifier masks for Wayland/GLFW.
     * These correspond to standard X11 modifier assignments.
     */
    dispPtr->modeModMask = Mod5Mask;   /* AltGr / Mode_switch */
    dispPtr->altModMask  = Mod1Mask;   /* Alt */
    dispPtr->metaModMask = Mod4Mask;   /* Super/Windows key */

    /*
     * Lock modifiers are platform-independent.
     */
    dispPtr->lockUsage = LU_CAPS;

    /*
     * Note: We don't use XKB directly here. The XKB handling
     * is done in tkWaylandKey.c keyboard event processing.
     * This function just sets up the modifier mask mappings
     * that Tk's binding system expects.
     */
}

/*
 * **************************************************
 * TkGetCurrentTimeMillis (file-local helper)
 *
 *      Return the current time in milliseconds, suitable for use in
 *      event.xkey.time and similar timestamp fields.
 *
 * Results:
 *      Current time as a Tk Time value.
 *
 * Side effects:
 *      None.
 *
 * **************************************************
 */

static Time
TkGetCurrentTimeMillis(void)
{
    Tcl_Time now;
    Tcl_GetTime(&now);
    return (Time)(now.sec * 1000 + now.usec / 1000);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandKeyInit --
 *
 *       Iniitalizes keyboard and IME resources.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Launches XKB and IME resources.
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
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
