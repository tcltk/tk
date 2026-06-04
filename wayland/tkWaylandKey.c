/*
 * tkWaylandKey.c –
 *
 * This file contains functions for keyboard input handling on Wayland/GLFW,
 * including comprehensive IME (Input Method Editor) support for complex text
 * input (Chinese, Japanese, Korean, etc.).
 *
 * Copyright © 2026 Kevin Walzer
 * Copyright © 2026 Marc Culler
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkUnixInt.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <string.h>
#include <ctype.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <X11/keysymdef.h>

int TkWaylandIbus_Init(Tcl_Interp *interp);

/*
 * Keyboard implementation.
 *
 * These functions implement XKB keyboard support for key translation for Tk on
 * Wayland.
 */

/* XKB keyboard state for key translation. */
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
 * Synthetic keycode range for IME‑generated Unicode characters.
 * The high bit (0x80000000) is not used by real X11 keycodes, so we
 * can safely encode any Unicode scalar value in the low 21 bits.
 */
#define SYNTHETIC_KEYCODE_BASE 0xFF000000
#define SYNTHETIC_KEYCODE(ch)  (SYNTHETIC_KEYCODE_BASE | (ch))

/*
 * ----------------------------------------------------------------------------
 * TkpGetString --
 *
 *         Called in tkBind.c to generate a value for the %A field in a Tk
 *         <Key> event.  The stored text is consumed once: a second call for
 *         the same event returns an empty string.  This is intentional and
 *         matches the X11 pattern where XLookupString is called at most once
 *         per event.
 *
 * Results:
 *         Returns a string.
 *
 * Side effects:
 *         Clears the stored text after reading it.
 * ----------------------------------------------------------------------------
 */

const char*
TkpGetString(
    TkWindow *winPtr,
    TCL_UNUSED(XEvent*), /* eventPtr*/
    Tcl_DString *dsPtr)
{
    const char* result;
    TkWindow *toplevel = winPtr;

    while (!Tk_IsTopLevel(toplevel)) {
        toplevel = (TkWindow *) Tk_Parent(toplevel);
    }
    Tcl_DStringInit(dsPtr);
    result = Tcl_DStringAppend(dsPtr, TkWaylandGetStoredText(toplevel),
                               TCL_INDEX_NONE);
    TkWaylandClearStoredText(toplevel);
    return result;
}

/*
 * ----------------------------------------------------------------------------
 * TkpGetKeySym --
 *
 *         Called in tkBind.c to generate a value for the %K field in a Tk
 *         <Key> event.
 *
 * Results:
 *         Returns a keysym.
 *
 * Side effects:
 *         None.
 * ----------------------------------------------------------------------------
 */

KeySym
TkpGetKeySym(
    TCL_UNUSED(TkDisplay*), /*dispPtr */
    XEvent *eventPtr)           /* Description of X event. */
{
    return TkWaylandGetKeysymFromScancode(eventPtr->xkey.keycode);
}

/*
 * ----------------------------------------------------------------------------
 * TkpInitKeymapInfo --
 *
 *         This procedure is invoked to scan keymap information to recompute
 *         values that are important for binding, such as determining which
 *         modifier is associated with "mode switch".
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         Keymap-related information in dispPtr is updated.
 * ----------------------------------------------------------------------------
 */

void
TkpInitKeymapInfo(TkDisplay *dispPtr)
{
    /*
     * Set up default modifier masks for Wayland/GLFW.  These correspond to the
     * standard X11 modifier assignments.
     */
    dispPtr->modeModMask = Mod5Mask;   /* AltGr / Mode_switch */
    dispPtr->altModMask  = Mod1Mask;   /* Alt */
    dispPtr->metaModMask = Mod4Mask;   /* Super/Windows key */

    /*
     * Lock modifiers are platform-independent.
     */
    dispPtr->lockUsage = LU_CAPS;
}

/*
 * ----------------------------------------------------------------------------
 * TkWaylandGetKeysymFromScancode --
 *
 *         Convert a keyboard scancode (evdev code) into an X11 keysym using
 *         the current XKB state.  If the scancode is a synthetic IME keycode,
 *         the Unicode keysym is extracted directly.
 *
 * Results:
 *         Returns the keysym, or NoSymbol if no XKB state is available.
 *
 * Side effects:
 *         None.
 * ----------------------------------------------------------------------------
 */

KeySym
TkWaylandGetKeysymFromScancode(
    int scancode)
{
    /* Synthetic keycode generated by IME commit/preedit. */
    if (scancode >= SYNTHETIC_KEYCODE_BASE) {
        return (KeySym)(scancode - SYNTHETIC_KEYCODE_BASE);
    }

    if (xkbState.state) {
        /*
         * The scancode is a kernel ev_code, but xkbcommon expects an X11
         * keycode, which is the scancode offset by 8.
         */
        return (KeySym) xkb_state_key_get_one_sym(xkbState.state,
                                                  scancode + 8);
    } else {
        return NoSymbol;
    }
}

/*
 * -----------------------------------------------------------------------------
 * TkpSetKeycodeAndState --
 *
 *         Given a keysym and an XEvent, set the keycode and modifier state
 *         fields of the event.  This is used by the [event generate] command.
 *
 *         The function scans the live XKB keymap to find a keycode that
 *         generates the given keysym.  If the keymap is not yet loaded, it
 *         falls back to a hard-coded table of common non-printable keys.
 *
 *         Level-to-modifier mapping:
 *           level 0  → no modifier
 *           level 1  → ShiftMask
 *           level ≥ 2 → Mod5Mask (AltGr)
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         Modifies eventPtr->xkey.keycode and eventPtr->xkey.state.
 * ----------------------------------------------------------------------------
 */

void
TkpSetKeycodeAndState(
    TCL_UNUSED(Tk_Window),
    KeySym keysym,
    XEvent *eventPtr)
{

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
                             * Map the shift level to modifier bits:
                             *   level 0 → no modifier
                             *   level 1 → ShiftMask
                             *   level ≥ 2 → Mod5Mask (AltGr)
                             */
                            eventPtr->xkey.keycode = kc;
                            if (level == 1) {
                                eventPtr->xkey.state |= ShiftMask;
                            } else if (level >= 2) {
                                eventPtr->xkey.state |= Mod5Mask;
                            }
                            return;
                        }
                    }
                }
            }
        }
    }

    /*
     * Fallback for common keys when the XKB keymap is not yet loaded.  These
     * are X11 keycodes (evdev + 8) for a standard US-QWERTY layout.
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
        /* Last resort: encode the low byte of the keysym. */
        eventPtr->xkey.keycode = keysym & 0xFFu;
        break;
    }
}

/*
 * ----------------------------------------------------------------------------
 * XStringToKeysym --
 *
 *         Convert a keysym name string to a KeySym value.
 *
 *         NOTE: This stub is only correct when REDO_KEYSYM_LOOKUP is defined
 *         in the build, which causes tkBind.c to use its own internal
 *         implementation.  If REDO_KEYSYM_LOOKUP is ever removed, this
 *         function must be replaced with a real xkbcommon-based lookup using
 *         xkb_keysym_from_name().
 *
 * Results:
 *         Always returns NoSymbol.
 *
 * Side effects:
 *         None.
 * ----------------------------------------------------------------------------
 */

KeySym
XStringToKeysym(_Xconst char *string)
{
   return NoSymbol;
}

/*
 * ----------------------------------------------------------------------------
 * XKeysymToString --
 *
 *         Convert a KeySym value to its string name.
 *
 *         NOTE: This stub is only correct when REDO_KEYSYM_LOOKUP is defined
 *         in the build.  See comment in XStringToKeysym above.
 *
 * Results:
 *         Always returns NULL.
 *
 * Side effects:
 *         None.
 * ----------------------------------------------------------------------------
 */

char *
XKeysymToString(KeySym keysym)
{
    return NULL;
}

/*
 * ----------------------------------------------------------------------------
 * InitializeXKB --
 *
 *         Initialize the XKB context, keymap, compose table, and state.
 *
 * Results:
 *         Returns 1 on success, 0 on failure.
 *
 * Side effects:
 *         Allocates global XKB resources.
 * ----------------------------------------------------------------------------
 */

static int
InitializeXKB(void)
{
    const char *locale;

    /* Create an XKB context. */
    xkbState.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkbState.context) {
        return 0;
    }

    /* Obtain the locale for compose sequences. */
    locale = getenv("LC_ALL");
    if (!locale) locale = getenv("LC_CTYPE");
    if (!locale) locale = getenv("LANG");
    if (!locale) locale = "C";

    /* Create a compose table. */
    xkbState.compose_table = xkb_compose_table_new_from_locale(
            xkbState.context, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);

    if (xkbState.compose_table) {
        xkbState.compose_state = xkb_compose_state_new(
                xkbState.compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
    }

    /* Compile a keymap (default US layout as a starting point). */
    struct xkb_rule_names names = {
        .rules = "evdev",
        .model = "pc105",
        .layout = "us",
        .variant = "",
        .options = ""
    };

    xkbState.keymap = xkb_keymap_new_from_names(xkbState.context,
                          &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!xkbState.keymap) {
        return 0;
    }

    /* Create the XKB state object. */
    xkbState.state = xkb_state_new(xkbState.keymap);
    if (!xkbState.state) {
        return 0;
    }

    return 1;
}

/*
 * ----------------------------------------------------------------------------
 * CleanupXKB --
 *
 *         Free all XKB resources.
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         The global XKB state is reset to NULL.
 * ----------------------------------------------------------------------------
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
 * ----------------------------------------------------------------------------
 * TkWaylandKeyInit --
 *
 *         Initialize the Wayland keyboard subsystem, including XKB and IME
 *         (IBus) integration.
 *
 *         IBus initialization is treated as optional: if the IBus daemon is
 *         not running (common for non-CJK users), the keyboard still functions
 *         correctly via XKB alone.
 *
 * Results:
 *         Returns 1 on success, 0 on failure (XKB failure only).
 *
 * Side effects:
 *         Creates the XKB context and attempts to start the IBus D-Bus
 *         connection.
 * ----------------------------------------------------------------------------
 */

int
TkWaylandKeyInit(void)
{
    if (!InitializeXKB()) {
        fprintf(stderr, "TkWaylandKeyInit: failed to initialize xkb.\n");
        return 0;
    }
    TkMainInfo *info = TkGetMainInfoList();
    /* IBus failure is non-fatal; keyboard works without it. */
    TkWaylandIbus_Init(info->interp);
    return 1;
}

/*
 * ----------------------------------------------------------------------------
 * TkWaylandKeyCleanup --
 *
 *         Clean up all Wayland keyboard resources, including XKB.
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         Frees the XKB state, keymap, and context.
 * ----------------------------------------------------------------------------
 */

void
TkWaylandKeyCleanup() {
    CleanupXKB();
}

/*
 * Input Method Editor implementation.
 *
 * This implementation is necessary because GLFW does not provide any built-in
 * support for IME despite requests dating back many years.  We use sd-bus to
 * connect to the IBus IME daemon.  IBus was chosen because it is widely used,
 * and the other primary IME library, Fcitx, provides a compatibility interface
 * for IBus.  IBus provides the UI (candidate windows, preëdit) and creates a
 * context for each Tk toplevel, then forwards committed text to the focused
 * text widget via a Tcl command.
 *
 * We use the direct D-Bus connection rather than the Wayland text-input
 * protocols because those would require managing a separate Wayland surface
 * with GLFW, which is complex and fragile.  GLFW does not expose any way to
 * hook directly into its Wayland surface.
 *
 * IBus D-Bus signal signatures (real wire format):
 *
 *   CommitText      (v)   – IBus.Text variant containing the committed string
 *   UpdatePreedit   (vub) – IBus.Text variant, uint32 cursor, bool visible
 *   HidePreeditText ()
 *   ShowPreeditText ()
 *
 * IBus.Text is a D-Bus struct: (sa{sv}sv) — (name, attachments, text,
 * attributes).  We unpack only the plain text string for simplicity.
 */


 /* IBus D-Bus constants. */

#define IBUS_SERVICE          "org.freedesktop.IBus"
#define IBUS_PATH             "/org/freedesktop/IBus"
#define IBUS_INTERFACE        "org.freedesktop.IBus"
#define IBUS_IC_INTERFACE     "org.freedesktop.IBus.InputContext"

/* IBus signal names. */
#define IBUS_SIGNAL_COMMIT_TEXT      "CommitText"
#define IBUS_SIGNAL_UPDATE_PREEDIT   "UpdatePreedit"
#define IBUS_SIGNAL_HIDE_PREEDIT     "HidePreeditText"
#define IBUS_SIGNAL_SHOW_PREEDIT     "ShowPreeditText"

/* Per-window IBus context structure. */

typedef struct IbusContext {
    Tk_Window tkwin;            /* Tk toplevel this context belongs to. */
    char *obj_path;             /* D-Bus object path of the IBus input context. */
    sd_bus_slot *signal_slot;   /* Slot for CommitText signal subscription. */
    sd_bus_slot *preedit_slot;  /* Slot for UpdatePreedit signal subscription. */
    Tcl_Interp *interp;         /* Tcl interpreter (for callbacks). */
    int enabled;                /* Whether IME is active for this window. */
    struct IbusContext *next;   /* Linked list pointer. */
} IbusContext;

/* Global IBus state. */
static sd_bus *ibus_bus = NULL;
static IbusContext *all_contexts = NULL;

/*
 * ----------------------------------------------------------------------------
 * FindContext --
 *
 *         Find the IBus context associated with a given Tk window.
 *
 * Results:
 *         Returns a pointer to the IbusContext structure, or NULL if no
 *         context exists for the window.
 *
 * Side effects:
 *         None.
 * ----------------------------------------------------------------------------
 */

static IbusContext *FindContext(Tk_Window tkwin)
{
    IbusContext *ctx = all_contexts;
    while (ctx) {
        if (ctx->tkwin == tkwin) return ctx;
        ctx = ctx->next;
    }
    return NULL;
}

/*
 * ----------------------------------------------------------------------------
 * IbusReadTextFromVariant --
 *
 *         Read the plain text string from an IBus.Text variant embedded in a
 *         D-Bus message.  IBus.Text has the wire type "v" containing a struct
 *         (sa{sv}sv): (name, attachments, text, attributes).  We enter the
 *         variant and the struct, skip the first two fields, and read the
 *         plain text string.
 *
 *         On success, *text_out points into the message buffer (no copy
 *         needed; lifetime is the message).  On failure, *text_out is set to
 *         NULL.
 *
 * Results:
 *         Returns the sd_bus return code of the last read operation.
 *
 * Side effects:
 *         Advances the message cursor.
 * ----------------------------------------------------------------------------
 */

static int
IbusReadTextFromVariant(
			sd_bus_message *m,
			const char **text_out)
{
    int r;
    *text_out = NULL;

    /* Enter the outer "v" variant (which contains the IBus.Text struct). */
    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "sa{sv}sv");
    if (r < 0) return r;

    /* Enter the struct itself. */
    r = sd_bus_message_enter_container(m, SD_BUS_TYPE_STRUCT, "sa{sv}sv");
    if (r < 0) return r;

    /* Skip field 0: string name (e.g., "IBusText"). */
    r = sd_bus_message_skip(m, "s");
    if (r < 0) return r;

    /* Skip field 1: a{sv} attachments dict. */
    r = sd_bus_message_skip(m, "a{sv}");
    if (r < 0) return r;

    /* Read field 2: string text. */
    r = sd_bus_message_read(m, "s", text_out);
    if (r < 0) return r;

    /* We don't need the attribute list; exit the containers. */
    sd_bus_message_exit_container(m); /* struct */
    sd_bus_message_exit_container(m); /* variant */

    return r;
}

/*
 * ----------------------------------------------------------------------------
 * TkWaylandSendUnicodeString --
 *
 *         Generate synthetic KeyPress events for each character in a UTF-8
 *         string and queue them to the Tk event loop.  This is used to
 *         insert committed text from the IME or to display preëdit text.
 *
 *         Each synthetic event uses a keycode in the range
 *         SYNTHETIC_KEYCODE_BASE + Unicode scalar value.  The modifier state
 *         is taken from the current XKB state.
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         Queues KeyPress events.
 * ----------------------------------------------------------------------------
 */

static void
TkWaylandSendUnicodeString(
    Tk_Window tkwin,            /* Toplevel window receiving the text. */
    const char *utf8_str)       /* UTF-8 string to insert. */
{
    if (!utf8_str || !*utf8_str) return;
    if (!tkwin) return;

    Display *display = Tk_Display(tkwin);
    Window window = Tk_WindowId(tkwin);
    Tcl_Time now;
	Tcl_GetTime(&now);
	Time time = (Time)(now.sec * 1000 + now.usec / 1000);

    /* Obtain current modifier state from XKB. */
    unsigned int state = 0;
    if (xkbState.state) {
        xkb_mod_mask_t mods = xkb_state_serialize_mods(xkbState.state,
                              XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
        /* Map XKB modifier bits to X11 modifier masks. */
        if (mods & (1 << xkb_keymap_mod_get_index(xkbState.keymap, "Shift")))
            state |= ShiftMask;
        if (mods & (1 << xkb_keymap_mod_get_index(xkbState.keymap, "Control")))
            state |= ControlMask;
        if (mods & (1 << xkb_keymap_mod_get_index(xkbState.keymap, "Alt")))
            state |= Mod1Mask;
        if (mods & (1 << xkb_keymap_mod_get_index(xkbState.keymap, "Mod5")))
            state |= Mod5Mask;  /* AltGr */
        /* Add other modifiers as needed. */
    }

    XEvent xEvent;
    memset(&xEvent, 0, sizeof(xEvent));
    xEvent.xany.display = display;
    xEvent.xany.window = window;
    xEvent.xkey.root = XRootWindow(display, 0);
    xEvent.xkey.time = time;
    xEvent.xkey.state = state;
    xEvent.xkey.same_screen = True;

    /* Convert UTF-8 to Unicode and generate events. */
    const char *p = utf8_str;
    while (*p) {
        Tcl_UniChar ch;
        p += Tcl_UtfToUniChar(p, &ch);
        xEvent.xkey.keycode = SYNTHETIC_KEYCODE(ch);
        xEvent.xany.type = KeyPress;
        Tk_QueueWindowEvent(&xEvent, TCL_QUEUE_TAIL);
    }
}

/*
 * ----------------------------------------------------------------------------
 * OnCommitText --
 *
 *         D-Bus signal handler for the IBus "CommitText" signal.  Called when
 *         the IME has finished composing a text string and it should be
 *         inserted into the focused widget.
 *
 *         Wire signature: CommitText(v)  where v contains an IBus.Text struct.
 *
 *         The committed text is inserted by generating synthetic KeyPress
 *         events.  Any ongoing preëdit is first cleared via the
 *         <<TkClearIMEMarkedText>> virtual event.
 *
 * Results:
 *         Returns 0 on success, or a negative error code on failure.
 *
 * Side effects:
 *         Queues key events and virtual events for the target toplevel.
 * ----------------------------------------------------------------------------
 */

static int OnCommitText(
			sd_bus_message *m,
			void *userdata,
			sd_bus_error *ret_error)
{
    IbusContext *ctx = (IbusContext *)userdata;
    const char *text = NULL;
    int r;

    r = IbusReadTextFromVariant(m, &text);
    if (r < 0 || !text || !*text) return 0;

    /* Clear any pending preëdit before committing. */
    Tk_SendVirtualEvent(ctx->tkwin, "TkClearIMEMarkedText", NULL);

    /* Insert the committed text via synthetic key events. */
    TkWaylandSendUnicodeString(ctx->tkwin, text);

    return 0;
}

/*
 * ----------------------------------------------------------------------------
 * OnUpdatePreedit --
 *
 *         D-Bus signal handler for the IBus "UpdatePreedit" signal.  Called
 *         when the IME preëdit string (the ongoing composition) changes.
 *
 *         Wire signature: UpdatePreedit(v, u, b)
 *           v – IBus.Text variant (the preëdit string)
 *           u – uint32 cursor position within the preëdit
 *           b – bool visible
 *
 *         The preëdit is displayed using the standard Tk virtual events:
 *           <<TkStartIMEMarkedText>>, <<TkEndIMEMarkedText>>, and
 *           <<TkClearIMEMarkedText>>.  The preëdit text itself is inserted
 *         as synthetic key events (just like committed text) but between the
 *         start and end markers, causing the Tk bindings to underline or
 *         select the marked region.
 *
 * Results:
 *         Returns 0 on success, or a negative error code on failure.
 *
 * Side effects:
 *         Sends virtual events and synthetic key events.
 * ----------------------------------------------------------------------------
 */

static int OnUpdatePreedit(
			   sd_bus_message *m,
			   void *userdata,
			   sd_bus_error *ret_error)
{
    IbusContext *ctx = (IbusContext *)userdata;
    const char *text = NULL;
    uint32_t cursor_pos = 0;
    int visible = 0;
    int r;

    /* Field 0: IBus.Text variant. */
    r = IbusReadTextFromVariant(m, &text);
    if (r < 0) return 0;

    /* Field 1: uint32 cursor position. */
    r = sd_bus_message_read(m, "u", &cursor_pos);
    if (r < 0) return 0;

    /* Field 2: bool visible. */
    r = sd_bus_message_read(m, "b", &visible);
    if (r < 0) return 0;

    if (text && *text && visible) {
        /* Clear any previous preëdit text. */
        Tk_SendVirtualEvent(ctx->tkwin, "TkClearIMEMarkedText", NULL);

        /* Start the marked region. */
        Tk_SendVirtualEvent(ctx->tkwin, "TkStartIMEMarkedText", NULL);

        /* Insert the preëdit text via synthetic key events. */
        TkWaylandSendUnicodeString(ctx->tkwin, text);

        /* End the marked region – Tk bindings will now underline/select it. */
        Tk_SendVirtualEvent(ctx->tkwin, "TkEndIMEMarkedText", NULL);

        /*
         * Note: The cursor position is not used in this simple implementation;
         * a full IME would also move the insertion cursor accordingly.
         */
    } else {
        /* Hide preëdit: clear the marked region. */
        Tk_SendVirtualEvent(ctx->tkwin, "TkClearIMEMarkedText", NULL);
    }
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 * FreeIbusContext --
 *
 *         Free all resources owned by an IbusContext.  Does not unlink it
 *         from the global list.
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         Unregisters signal slots and frees memory.
 * ----------------------------------------------------------------------------
 */

static void
FreeIbusContext(IbusContext *ctx)
{
    if (!ctx) return;
    if (ctx->preedit_slot) {
        sd_bus_slot_unref(ctx->preedit_slot);
        ctx->preedit_slot = NULL;
    }
    if (ctx->signal_slot) {
        sd_bus_slot_unref(ctx->signal_slot);
        ctx->signal_slot = NULL;
    }
    if (ctx->obj_path) {
        free(ctx->obj_path);
        ctx->obj_path = NULL;
    }
    Tcl_Free((char *)ctx);
}

/*
 * ----------------------------------------------------------------------------
 * CreateIbusContext --
 *
 *         Create an IBus input context for a given Tk toplevel window.
 *
 * Results:
 *         Returns TCL_OK on success, or TCL_ERROR on failure.  An error
 *         message is left in the interpreter.
 *
 * Side effects:
 *         Connects to the IBus daemon (if not already connected), creates a
 *         new input context, subscribes to its signals, and adds the context
 *         to the global list.
 * ----------------------------------------------------------------------------
 */

static int CreateIbusContext(
			     Tcl_Interp *interp,
			     Tk_Window tkwin)
{
    if (!ibus_bus) {
        Tcl_SetResult(interp, "IBus not connected", TCL_STATIC);
        return TCL_ERROR;
    }
    if (FindContext(tkwin)) return TCL_OK; /* Already exists. */

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *msg = NULL;
    const char *obj_path = NULL;
    int r;

    /* Call IBus.CreateInputContext(string client_name) -> object path. */
    r = sd_bus_call_method(ibus_bus,
                           IBUS_SERVICE,
                           IBUS_PATH,
                           IBUS_INTERFACE,
                           "CreateInputContext",
                           &error,
                           &msg,
                           "s", "tk_wayland_ime");
    if (r < 0) goto fail;

    r = sd_bus_message_read(msg, "o", &obj_path);
    if (r < 0) goto fail;

    /* Allocate and fill the context structure. */
    IbusContext *ctx = (IbusContext *)Tcl_Alloc(sizeof(IbusContext));
    memset(ctx, 0, sizeof(IbusContext));
    ctx->tkwin = tkwin;
    ctx->obj_path = strdup(obj_path);
    ctx->interp = interp;
    ctx->enabled = 0;

    if (!ctx->obj_path) {
        Tcl_Free((char *)ctx);
        Tcl_SetResult(interp, "out of memory", TCL_STATIC);
        sd_bus_message_unref(msg);
        sd_bus_error_free(&error);
        return TCL_ERROR;
    }

    /* Subscribe to CommitText signals from this input context. */
    r = sd_bus_match_signal(ibus_bus,
                            &ctx->signal_slot,
                            IBUS_SERVICE,
                            ctx->obj_path,
                            IBUS_IC_INTERFACE,
                            IBUS_SIGNAL_COMMIT_TEXT,
                            OnCommitText,
                            ctx);
    if (r < 0) goto fail_ctx;

    /* Subscribe to UpdatePreedit signals (non-fatal if unavailable). */
    r = sd_bus_match_signal(ibus_bus,
                            &ctx->preedit_slot,
                            IBUS_SERVICE,
                            ctx->obj_path,
                            IBUS_IC_INTERFACE,
                            IBUS_SIGNAL_UPDATE_PREEDIT,
                            OnUpdatePreedit,
                            ctx);
    if (r < 0) {
        /* Non-fatal: preëdit display is optional. */
        ctx->preedit_slot = NULL;
    }

    /* 5. Add to the global list. */
    ctx->next = all_contexts;
    all_contexts = ctx;

    sd_bus_message_unref(msg);
    sd_bus_error_free(&error);
    return TCL_OK;

fail_ctx:
    FreeIbusContext(ctx);
    /* fall through */
fail:
    if (msg) sd_bus_message_unref(msg);
    Tcl_SetResult(interp, (char *)error.message, TCL_STATIC);
    sd_bus_error_free(&error);
    return TCL_ERROR;
}

/*
 * ----------------------------------------------------------------------------
 * IbusFocusIn --
 *
 *         Inform IBus that a toplevel window (or a widget inside it) has
 *         received keyboard focus.
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         Sends a FocusIn D-Bus call to the IBus input context.
 * ----------------------------------------------------------------------------
 */

static void IbusFocusIn(IbusContext *ctx)
{
    if (!ctx || !ibus_bus || !ctx->obj_path) return;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_call_method(ibus_bus,
                       IBUS_SERVICE,
                       ctx->obj_path,
                       IBUS_IC_INTERFACE,
                       "FocusIn",
                       &error,
                       NULL,
                       "");
    sd_bus_error_free(&error);
}

/*
 * ----------------------------------------------------------------------------
 * IbusFocusOut --
 *
 *         Inform IBus that a toplevel window has lost keyboard focus.
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         Sends a FocusOut D-Bus call to the IBus input context.
 * ----------------------------------------------------------------------------
 */

static void IbusFocusOut(IbusContext *ctx)
{
    if (!ctx || !ibus_bus || !ctx->obj_path) return;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_call_method(ibus_bus,
                       IBUS_SERVICE,
                       ctx->obj_path,
                       IBUS_IC_INTERFACE,
                       "FocusOut",
                       &error,
                       NULL,
                       "");
    sd_bus_error_free(&error);
}

/*
 * ----------------------------------------------------------------------------
 * IbusEnable --
 *
 *         Enable the IME for a given IBus context.
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         Sends an Enable D-Bus call and sets the context's enabled flag.
 * ----------------------------------------------------------------------------
 */

static void IbusEnable(IbusContext *ctx)
{
    if (!ctx || !ibus_bus || !ctx->obj_path) return;
    if (ctx->enabled) return;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_call_method(ibus_bus,
                       IBUS_SERVICE,
                       ctx->obj_path,
                       IBUS_IC_INTERFACE,
                       "Enable",
                       &error,
                       NULL,
                       "");
    sd_bus_error_free(&error);
    ctx->enabled = 1;
}

/*
 * ----------------------------------------------------------------------------
 * IbusDisable --
 *
 *         Disable the IME for a given IBus context.
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         Sends a Disable D-Bus call and clears the context's enabled flag.
 * ----------------------------------------------------------------------------
 */

static void IbusDisable(IbusContext *ctx)
{
    if (!ctx || !ibus_bus || !ctx->obj_path) return;
    if (!ctx->enabled) return;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_call_method(ibus_bus,
                       IBUS_SERVICE,
                       ctx->obj_path,
                       IBUS_IC_INTERFACE,
                       "Disable",
                       &error,
                       NULL,
                       "");
    sd_bus_error_free(&error);
    ctx->enabled = 0;
}

/*
 * ----------------------------------------------------------------------------
 * IbusProcessKeyEvent --
 *
 *         Send a key event to the IBus daemon for processing.  If the IME
 *         handles the event (e.g., starts composition), the application should
 *         not process the key further.
 *
 * Results:
 *         Returns 1 if IBus handled the key event, 0 otherwise.
 *
 * Side effects:
 *         May change the internal state of the IBus input context.
 * ----------------------------------------------------------------------------
 */

static int IbusProcessKeyEvent(
			       IbusContext *ctx,
			       uint32_t keyval,
                               uint32_t keycode,
			       uint32_t state)
{
    if (!ctx || !ibus_bus || !ctx->obj_path) return 0;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int handled = 0;
    int r;

    r = sd_bus_call_method(ibus_bus,
                           IBUS_SERVICE,
                           ctx->obj_path,
                           IBUS_IC_INTERFACE,
                           "ProcessKeyEvent",
                           &error,
                           &reply,
                           "uuu",
                           keyval, keycode, state);
    if (r < 0) {
        sd_bus_error_free(&error);
        return 0;
    }
    sd_bus_message_read(reply, "b", &handled);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return handled;
}

/*
 * ----------------------------------------------------
 * Tk_SetCaretPos --
 *
 *         Called by text widgets to report the screen position of the
 *         insertion cursor.  We store it in the standard TkCaret field on
 *         the display (so Tk internals stay consistent) and then forward
 *         the screen-absolute rectangle to IBus via SetCursorLocation so
 *         the IME candidate window appears next to the caret.
 *
 *         The x/y arguments are widget-relative; IBus expects screen-absolute
 *         coordinates, so we use Tk_GetRootCoords to convert.
 *
 *         height is the line height at the caret; we pass zero width because
 *         the caret is a point, not a selection rectangle.
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         Updates dispPtr->caret.  Sends a SetCursorLocation D-Bus call to
 *         the IBus input context for the enclosing toplevel, if one exists.
 * ----------------------------------------------------
 */

void
Tk_SetCaretPos(
    Tk_Window tkwin,
    int x,
    int y,
    int height)
{
    TkWindow *winPtr = (TkWindow *) tkwin;
    TkDisplay *dispPtr = winPtr->dispPtr;

    /* Skip if nothing changed. */
    if ((dispPtr->caret.winPtr == winPtr)
            && (dispPtr->caret.x == x)
            && (dispPtr->caret.y == y)
            && (dispPtr->caret.height == height)) {
        return;
    }

    dispPtr->caret.winPtr = winPtr;
    dispPtr->caret.x      = x;
    dispPtr->caret.y      = y;
    dispPtr->caret.height = height;

    if (!ibus_bus) return;

    /* Walk up to the toplevel to find the IBus context. */
    TkWindow *topPtr = winPtr;
    while (!Tk_IsTopLevel(topPtr)) {
        topPtr = (TkWindow *) Tk_Parent(topPtr);
    }
    IbusContext *ctx = FindContext((Tk_Window) topPtr);
    if (!ctx || !ctx->obj_path) return;

    /*
     * Convert widget-relative (x, y) to screen-absolute coordinates.
     * Tk_GetRootCoords returns the screen position of the widget's origin,
     * so we add the local offsets to get the caret's screen position.
     */
    int rootX, rootY;
    Tk_GetRootCoords(tkwin, &rootX, &rootY);
    int screenX = rootX + x;
    int screenY = rootY + y;

    /*
     * Tell IBus where the caret is.  The candidate window will anchor
     * itself just below (screenX, screenY + height).  Width is 0 because
     * the insertion caret is a line, not a rectangle.
     *
     * Signature: SetCursorLocation(iiii) — x, y, w, h  (all int32).
     */
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_call_method(ibus_bus,
                       IBUS_SERVICE,
                       ctx->obj_path,
                       IBUS_IC_INTERFACE,
                       "SetCursorLocation",
                       &error,
                       NULL,
                       "iiii",
                       (int32_t) screenX,
                       (int32_t) screenY,
                       (int32_t) 0,
                       (int32_t) height);
    sd_bus_error_free(&error);
}


/*
 * ----------------------------------------------------------------------------
 * TkWaylandIbusCreateContext --
 *
 *         Public wrapper around CreateIbusContext.  Called from GLFW callbacks
 *         (tkWaylandNotify.c) when a new toplevel GLFWwindow is created, so
 *         that an IBus input context is ready before the window receives focus.
 *
 * Results:
 *         Returns TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *         Creates a new IBus input context for tkwin.
 * ----------------------------------------------------------------------------
 */

int
TkWaylandIbusCreateContext(
    Tcl_Interp *interp,
    Tk_Window   tkwin)
{
    return CreateIbusContext(interp, tkwin);
}

/*
 * ----------------------------------------------------------------------------
 * TkWaylandIbusFocusIn --
 *
 *         Public wrapper called from TkGlfwWindowFocusCallback when a window
 *         gains focus.  Looks up the IBus context for the toplevel and calls
 *         IbusFocusIn + IbusEnable so callers need not see IbusContext*.
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         Sends FocusIn and Enable D-Bus calls to the IBus input context.
 * ----------------------------------------------------------------------------
 */

void
TkWaylandIbusFocusIn(
    Tk_Window tkwin)
{
    IbusContext *ctx = FindContext(GetToplevelOfWidget(tkwin));
    if (!ctx) return;
    IbusFocusIn(ctx);
    IbusEnable(ctx);
}

/*
 * ----------------------------------------------------------------------------
 * TkWaylandIbusFocusOut --
 *
 *         Public wrapper called from TkGlfwWindowFocusCallback when a window
 *         loses focus.  Looks up the IBus context and calls IbusFocusOut +
 *         IbusDisable.
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         Sends FocusOut and Disable D-Bus calls to the IBus input context.
 * ----------------------------------------------------------------------------
 */

void
TkWaylandIbusFocusOut(
    Tk_Window tkwin)
{
    IbusContext *ctx = FindContext(GetToplevelOfWidget(tkwin));
    if (!ctx) return;
    IbusFocusOut(ctx);
    IbusDisable(ctx);
}

/*
 * ----------------------------------------------------------------------------
 * TkWaylandIbusProcessKey --
 *
 *         Public wrapper called from TkGlfwKeyCallback.  Looks up the IBus
 *         context for the focused toplevel and forwards the key event to the
 *         IBus daemon.
 *
 * Results:
 *         Returns 1 if IBus consumed the key event (caller should suppress the
 *         normal Tk KeyPress event), 0 otherwise.
 *
 * Side effects:
 *         May start or advance IME composition.
 * ----------------------------------------------------------------------------
 */

int
TkWaylandIbusProcessKey(
    Tk_Window tkwin,
    uint32_t  keyval,
    uint32_t  keycode,
    uint32_t  state)
{
    IbusContext *ctx = FindContext(GetToplevelOfWidget(tkwin));
    if (!ctx) return 0;
    return IbusProcessKeyEvent(ctx, keyval, keycode, state);
}

/*
 * ----------------------------------------------------------------------------
 * CmdCreateContext --
 *
 *         Creates an IBus input context for the given toplevel window.
 *
 * Results:
 *         Returns TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *         Creates a new IBus context and stores it in the global list.
 * ----------------------------------------------------------------------------
 */

int CmdCreateContext(
			    ClientData clientData,
			    Tcl_Interp *interp,
                            int objc,
			    Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "window");
        return TCL_ERROR;
    }
    const char *winName = Tcl_GetString(objv[1]);
    Tk_Window tkwin = Tk_NameToWindow(interp, winName, Tk_MainWindow(interp));
    if (!tkwin) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Invalid window name", -1));
        return TCL_ERROR;
    }
    if (!Tk_IsTopLevel(tkwin)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Not a toplevel window", -1));
        return TCL_ERROR;
    }
    return CreateIbusContext(interp, tkwin);
}

/*
 * ----------------------------------------------------------------------------
 * CmdFocusIn --
 *
 *         Informs IBus that a toplevel window  (or a widget inside it) 
 *		   has gained focus, and enables the IME.
 *
 *
 * Results:
 *         Returns TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *         Sends FocusIn and Enable D-Bus calls to the IBus context.
 * ----------------------------------------------------------------------------
 */

static int CmdFocusIn(
		      ClientData clientData,
		      Tcl_Interp *interp,
                      int objc,
		      Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "window");
        return TCL_ERROR;
    }
    const char *winName = Tcl_GetString(objv[1]);
    Tk_Window tkwin = Tk_NameToWindow(interp, winName, Tk_MainWindow(interp));
    if (!tkwin) return TCL_ERROR;
    IbusContext *ctx = FindContext(GetToplevelOfWidget(tkwin));
    if (!ctx) {
        Tcl_SetResult(interp, "No IBus context for this toplevel", TCL_STATIC);
        return TCL_ERROR;
    }
    IbusFocusIn(ctx);
    IbusEnable(ctx);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------------
 * CmdFocusOut --
 *
 *         Informs IBus that a toplevel window has lost focus and disables the IME.
 *
 *
 * Results:
 *         Returns TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *         Sends FocusOut and Disable D-Bus calls to the IBus context.
 * ----------------------------------------------------------------------------
 */

static int CmdFocusOut(
		       ClientData clientData,
		       Tcl_Interp *interp,
                       int objc,
		       Tcl_Obj *const objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "window");
        return TCL_ERROR;
    }
    const char *winName = Tcl_GetString(objv[1]);
    Tk_Window tkwin = Tk_NameToWindow(interp, winName, Tk_MainWindow(interp));
    if (!tkwin) return TCL_ERROR;
    IbusContext *ctx = FindContext(GetToplevelOfWidget(tkwin));
    if (!ctx) return TCL_OK;
    IbusFocusOut(ctx);
    IbusDisable(ctx);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------------
 * CmdProcessKey --
 *
 *         Sends a key event to IBus and returns whether the IME handled it.
 *
 * Results:
 *         Returns 1 if IBus handled the key event, 0 otherwise.
 *
 * Side effects:
 *         May start or update IME composition.
 * ----------------------------------------------------------------------------
 */

static int CmdProcessKey(ClientData clientData,
			 Tcl_Interp *interp,
                         int objc,
			 Tcl_Obj *const objv[])
{
    if (objc != 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "window keyval keycode state");
        return TCL_ERROR;
    }
    const char *winName = Tcl_GetString(objv[1]);
    Tk_Window tkwin = Tk_NameToWindow(interp, winName, Tk_MainWindow(interp));
    if (!tkwin) return TCL_ERROR;

    uint32_t keyval, keycode, state;
    if (Tcl_GetIntFromObj(interp, objv[2], (int*)&keyval) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetIntFromObj(interp, objv[3], (int*)&keycode) != TCL_OK) return TCL_ERROR;
    if (Tcl_GetIntFromObj(interp, objv[4], (int*)&state) != TCL_OK) return TCL_ERROR;

    IbusContext *ctx = FindContext(GetToplevelOfWidget(tkwin));
    if (!ctx) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
        return TCL_OK;
    }
    int handled = IbusProcessKeyEvent(ctx, keyval, keycode, state);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(handled));
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------------
 * IbusBusHandler --
 *
 *         Tcl file handler callback for the D-Bus socket.  Called when the
 *         D-Bus file descriptor becomes readable.
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         Processes pending D-Bus messages.
 * ----------------------------------------------------------------------------
 */

static void IbusBusHandler(
			   ClientData clientData,
			   int mask)
{
    sd_bus *bus = (sd_bus *)clientData;
    if (mask & TCL_READABLE) {
        sd_bus_process(bus, NULL);
    }
}

/*
 * ----------------------------------------------------------------------------
 * IbusEventSetup --
 *
 *         Tcl event source setup function.  Registers a file handler for the
 *         D-Bus socket so the event loop wakes up when IBus sends a signal.
 *
 *         The file descriptor is fetched fresh each call to guard against
 *         reconnection (e.g., IBus daemon restart).
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         Installs IbusBusHandler as a file handler on the D-Bus file
 *         descriptor.
 * ----------------------------------------------------------------------------
 */

static void IbusEventSetup(
			   ClientData clientData,
			   int flags)
{
    if (!(flags & TCL_WINDOW_EVENTS)) return;
    if (!ibus_bus) return;

    int fd = sd_bus_get_fd(ibus_bus);
    if (fd >= 0) {
        Tcl_CreateFileHandler(fd, TCL_READABLE, IbusBusHandler, ibus_bus);
    }
}

/*
 * ----------------------------------------------------------------------------
 * IbusEventCheck --
 *
 *         Tcl event source check function.  If D-Bus events are pending,
 *         sets the maximum block time to zero so the event loop wakes
 *         immediately and the file handler can drain them.
 *
 * Results:
 *         None.
 *
 * Side effects:
 *         May set the maximum block time to zero.
 * ----------------------------------------------------------------------------
 */

static void IbusEventCheck(
			   ClientData clientData,
			   int flags)
{
    if (!(flags & TCL_WINDOW_EVENTS)) return;
    if (ibus_bus && sd_bus_get_events(ibus_bus) > 0) {
        Tcl_Time zeroTime = {0, 0};
        Tcl_SetMaxBlockTime(&zeroTime);
    }
}

/*
 * ----------------------------------------------------------------------------
 * TkWaylandIbus_Init --
 *
 *         Initialize the IBus D-Bus connection, create the Tcl commands, and
 *         integrate the D-Bus event source into the Tcl event loop.
 *
 *         Availability of the IBus daemon is tested by calling GetEngine on
 *         the IBus service.  If the call fails (daemon not running), this
 *         function returns TCL_ERROR, which the caller treats as non-fatal.
 *
 *         The commands created are:
 *           ::_ime::create_context window
 *           ::_ime::focus_in window
 *           ::_ime::focus_out window
 *           ::_ime::process_key window keyval keycode state
 *
 *         No separate commit/preedit commands are needed; the signal handlers
 *         send the standard Tk virtual events <<TkStartIMEMarkedText>>,
 *         <<TkEndIMEMarkedText>>, and <<TkClearIMEMarkedText>>, and generate
 *         synthetic key events for the text.
 *
 * Results:
 *         Returns TCL_OK on success, TCL_ERROR if IBus is unavailable.
 *
 * Side effects:
 *         Connects to the session bus, registers Tcl commands, and sets up
 *         an event source.
 * ----------------------------------------------------------------------------
 */

int TkWaylandIbus_Init(Tcl_Interp *interp)
{
    /* Connect to the session bus. */
    int r = sd_bus_default_user(&ibus_bus);
    if (r < 0) {
        /* Fall back to the system bus (unlikely to work for IBus). */
        r = sd_bus_default_system(&ibus_bus);
        if (r < 0) {
            ibus_bus = NULL;
            return TCL_ERROR;
        }
    }

    /*
     * Check that the IBus daemon is available by calling GetEngine.
     * GetEngine is a method call (not a property), so use sd_bus_call_method.
     * We do not use the reply; we only check whether the call succeeds.
     */
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    r = sd_bus_call_method(ibus_bus,
                           IBUS_SERVICE,
                           IBUS_PATH,
                           IBUS_INTERFACE,
                           "GetEngine",
                           &error,
                           &reply,
                           "");
    if (reply) sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    if (r < 0) {
        sd_bus_unref(ibus_bus);
        ibus_bus = NULL;
        return TCL_ERROR;
    }

    /* Integrate sd-bus into the Tcl event loop. */
    Tcl_CreateEventSource(IbusEventSetup, IbusEventCheck, NULL);

    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
