/*
 * tkWaylandKey.c --
 *
 * This file contains routines for dealing with international keyboard
 * input using GLFW.
 *
 * Copyright © 1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>
#include <xkbcommon/xkbcommon.h>
#include <string.h>

/*
 * Keyboard state management.
 */
typedef struct {
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    
    /* Modifier masks for Tk. */
    int mode_mod_mask;
    int meta_mod_mask;
    int alt_mod_mask;
    int lock_usage;
    
    /* Pending text input from character callback. */
    char *pending_text;
    int pending_text_len;
} KeyboardState;

static KeyboardState keyboardState = {NULL, NULL, NULL, 0, 0, 0, 0, NULL, 0};

/* Forward declarations. */
static void InitializeXKB(void);
static void CleanupXKB(void);
static KeySym XKBKeySymToX11KeySym(xkb_keysym_t xkb_sym);
static unsigned int GLFWModsToX11State(int glfw_mods);

/*
 *----------------------------------------------------------------------
 *
 * TkpInitKeymapInfo --
 *
 *      Initialize keyboard mapping using xkbcommon.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Initializes xkbcommon context and keymap.
 *
 *----------------------------------------------------------------------
 */

void
TkpInitKeymapInfo(TkDisplay *dispPtr)
{
    InitializeXKB();
    
    /* Initialize display keymap info. */
    dispPtr->bindInfoStale = 1;
    dispPtr->modeModMask = 0;
    dispPtr->metaModMask = Mod1Mask;
    dispPtr->altModMask = Mod1Mask;
    dispPtr->lockUsage = LU_CAPS;
}

/*
 *----------------------------------------------------------------------
 *
 * InitializeXKB --
 *
 *      Set up xkbcommon for keyboard handling.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates XKB context, keymap, and state.
 *
 *----------------------------------------------------------------------
 */

static void
InitializeXKB(void)
{
    if (keyboardState.xkb_context) {
        return;  /* Already initialized. */
    }

    /* Create XKB context. */
    keyboardState.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!keyboardState.xkb_context) {
        fprintf(stderr, "Failed to create XKB context\n");
        return;
    }

    /* Create keymap from names (uses system defaults). */
    struct xkb_rule_names names = {
        .rules = NULL,
        .model = NULL,
        .layout = NULL,
        .variant = NULL,
        .options = NULL
    };
    
    keyboardState.xkb_keymap = xkb_keymap_new_from_names(
        keyboardState.xkb_context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    
    if (!keyboardState.xkb_keymap) {
        fprintf(stderr, "Failed to create XKB keymap\n");
        xkb_context_unref(keyboardState.xkb_context);
        keyboardState.xkb_context = NULL;
        return;
    }

    /* Create XKB state. */
    keyboardState.xkb_state = xkb_state_new(keyboardState.xkb_keymap);
    if (!keyboardState.xkb_state) {
        fprintf(stderr, "Failed to create XKB state\n");
        xkb_keymap_unref(keyboardState.xkb_keymap);
        xkb_context_unref(keyboardState.xkb_context);
        keyboardState.xkb_keymap = NULL;
        keyboardState.xkb_context = NULL;
        return;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CleanupXKB --
 *
 *      Clean up xkbcommon resources.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Frees XKB state, keymap, and context.
 *
 *----------------------------------------------------------------------
 */

static void
CleanupXKB(void)
{
    if (keyboardState.pending_text) {
        ckfree(keyboardState.pending_text);
        keyboardState.pending_text = NULL;
    }
    
    if (keyboardState.xkb_state) {
        xkb_state_unref(keyboardState.xkb_state);
        keyboardState.xkb_state = NULL;
    }
    
    if (keyboardState.xkb_keymap) {
        xkb_keymap_unref(keyboardState.xkb_keymap);
        keyboardState.xkb_keymap = NULL;
    }
    
    if (keyboardState.xkb_context) {
        xkb_context_unref(keyboardState.xkb_context);
        keyboardState.xkb_context = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpCleanupKeymapInfo --
 *
 *      Clean up keyboard mapping info.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Cleans up XKB resources.
 *
 *----------------------------------------------------------------------
 */

void
TkpCleanupKeymapInfo(TkDisplay *dispPtr)
{
    CleanupXKB();
    
    dispPtr->modKeyCodes = NULL;
    dispPtr->numModKeyCodes = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetKeySym --
 *
 *      Get the KeySym for a key event using xkbcommon.
 *
 * Results:
 *      Returns the KeySym.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

KeySym
TkpGetKeySym(
    TkDisplay *dispPtr,
    XEvent *eventPtr)
{
    TkKeyEvent *kePtr = (TkKeyEvent *)eventPtr;
    KeyCode keycode;
    
    /* Check if already cached. */
    if (kePtr->keysym != NoSymbol) {
        return kePtr->keysym;
    }
    
    if (eventPtr->type != KeyPress && eventPtr->type != KeyRelease) {
        return NoSymbol;
    }
    
    keycode = eventPtr->xkey.keycode;
    
    if (!keyboardState.xkb_state) {
        return NoSymbol;
    }
    
    /* Use XKB to get keysym from scancode. */
    xkb_keycode_t xkb_keycode = keycode + 8;  /* XKB uses keycode + 8 */
    xkb_keysym_t xkb_sym = xkb_state_key_get_one_sym(
        keyboardState.xkb_state, xkb_keycode);
    
    /* Convert XKB keysym to X11 keysym. */
    KeySym keysym = XKBKeySymToX11KeySym(xkb_sym);
    
    /* Cache it. */
    kePtr->keysym = keysym;
    
    return keysym;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetString --
 *
 *      Get the UTF-8 string for a key event.
 *
 * Results:
 *      Returns the string.
 *
 * Side effects:
 *      Stores string in Tcl_DString.
 *
 *----------------------------------------------------------------------
 */

const char *
TkpGetString(
    TkWindow *winPtr,
    XEvent *eventPtr,
    Tcl_DString *dsPtr)
{
    TkKeyEvent *kePtr = (TkKeyEvent *)eventPtr;
    
    /* Check cache. */
    if (kePtr->charValuePtr) {
        Tcl_DStringSetLength(dsPtr, kePtr->charValueLen);
        memcpy(Tcl_DStringValue(dsPtr), kePtr->charValuePtr,
               kePtr->charValueLen + 1);
        return Tcl_DStringValue(dsPtr);
    }
    
    /* Only for KeyPress. */
    if (eventPtr->type != KeyPress) {
        Tcl_DStringSetLength(dsPtr, 0);
        return Tcl_DStringValue(dsPtr);
    }
    
    /* Check for pending text from character callback. */
    if (keyboardState.pending_text && keyboardState.pending_text_len > 0) {
        Tcl_DStringAppend(dsPtr, keyboardState.pending_text, 
                         keyboardState.pending_text_len);
        
        /* Cache it. */
        kePtr->charValuePtr = (char *)ckalloc(keyboardState.pending_text_len + 1);
        kePtr->charValueLen = keyboardState.pending_text_len;
        memcpy(kePtr->charValuePtr, keyboardState.pending_text,
               keyboardState.pending_text_len);
        kePtr->charValuePtr[keyboardState.pending_text_len] = '\0';
        
        /* Clear pending. */
        ckfree(keyboardState.pending_text);
        keyboardState.pending_text = NULL;
        keyboardState.pending_text_len = 0;
        
        return Tcl_DStringValue(dsPtr);
    }
    
    /* Use XKB to get UTF-8 string. */
    if (keyboardState.xkb_state) {
        xkb_keycode_t xkb_keycode = eventPtr->xkey.keycode + 8;
        char buffer[32];
        
        int len = xkb_state_key_get_utf8(keyboardState.xkb_state,
                                          xkb_keycode, buffer, sizeof(buffer));
        
        if (len > 0) {
            Tcl_DStringAppend(dsPtr, buffer, len);
            
            /* Cache it. */
            kePtr->charValuePtr = (char *)ckalloc(len + 1);
            kePtr->charValueLen = len;
            memcpy(kePtr->charValuePtr, buffer, len);
            kePtr->charValuePtr[len] = '\0';
            
            return Tcl_DStringValue(dsPtr);
        }
    }
    
    /* No string. */
    Tcl_DStringSetLength(dsPtr, 0);
    return Tcl_DStringValue(dsPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandUpdateKeyboardModifiers --
 *
 *      Update XKB state when modifiers change.
 *      Should be called from key callbacks.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates XKB modifier state.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandUpdateKeyboardModifiers(int glfw_mods)
{
    if (!keyboardState.xkb_state) {
        return;
    }
    
    /* Update XKB state based on GLFW modifiers. */
    xkb_mod_mask_t depressed = 0;
    
    if (glfw_mods & GLFW_MOD_SHIFT) {
        xkb_mod_index_t idx = xkb_keymap_mod_get_index(
            keyboardState.xkb_keymap, XKB_MOD_NAME_SHIFT);
        if (idx != XKB_MOD_INVALID) {
            depressed |= (1 << idx);
        }
    }
    
    if (glfw_mods & GLFW_MOD_CONTROL) {
        xkb_mod_index_t idx = xkb_keymap_mod_get_index(
            keyboardState.xkb_keymap, XKB_MOD_NAME_CTRL);
        if (idx != XKB_MOD_INVALID) {
            depressed |= (1 << idx);
        }
    }
    
    if (glfw_mods & GLFW_MOD_ALT) {
        xkb_mod_index_t idx = xkb_keymap_mod_get_index(
            keyboardState.xkb_keymap, XKB_MOD_NAME_ALT);
        if (idx != XKB_MOD_INVALID) {
            depressed |= (1 << idx);
        }
    }
    
    if (glfw_mods & GLFW_MOD_SUPER) {
        xkb_mod_index_t idx = xkb_keymap_mod_get_index(
            keyboardState.xkb_keymap, XKB_MOD_NAME_LOGO);
        if (idx != XKB_MOD_INVALID) {
            depressed |= (1 << idx);
        }
    }
    
    xkb_state_update_mask(keyboardState.xkb_state, depressed, 0, 0, 0, 0, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandStoreCharacterInput --
 *
 *      Store character from GLFW character callback.
 *      Called by TkGlfwCharCallback in tkGlfwCallbacks.c
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Stores pending text for next TkpGetString() call.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandStoreCharacterInput(unsigned int codepoint)
{
    char utf8[8];
    int len;
    
    /* Convert Unicode codepoint to UTF-8. */
    if (codepoint < 0x80) {
        utf8[0] = (char)codepoint;
        len = 1;
    } else if (codepoint < 0x800) {
        utf8[0] = 0xC0 | (codepoint >> 6);
        utf8[1] = 0x80 | (codepoint & 0x3F);
        len = 2;
    } else if (codepoint < 0x10000) {
        utf8[0] = 0xE0 | (codepoint >> 12);
        utf8[1] = 0x80 | ((codepoint >> 6) & 0x3F);
        utf8[2] = 0x80 | (codepoint & 0x3F);
        len = 3;
    } else {
        utf8[0] = 0xF0 | (codepoint >> 18);
        utf8[1] = 0x80 | ((codepoint >> 12) & 0x3F);
        utf8[2] = 0x80 | ((codepoint >> 6) & 0x3F);
        utf8[3] = 0x80 | (codepoint & 0x3F);
        len = 4;
    }
    utf8[len] = '\0';
    
    /* Store for next TkpGetString() call. */
    if (keyboardState.pending_text) {
        ckfree(keyboardState.pending_text);
    }
    
    keyboardState.pending_text = (char *)ckalloc(len + 1);
    memcpy(keyboardState.pending_text, utf8, len + 1);
    keyboardState.pending_text_len = len;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SetCaretPos --
 *
 *      Set text input caret position for IME.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates caret position in display structure.
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
    TkWindow *winPtr = (TkWindow *)tkwin;
    TkDisplay *dispPtr = winPtr->dispPtr;
    
    if ((dispPtr->caret.winPtr == winPtr) &&
        (dispPtr->caret.x == x) &&
        (dispPtr->caret.y == y) &&
        (dispPtr->caret.height == height)) {
        return;
    }
    
    dispPtr->caret.winPtr = winPtr;
    dispPtr->caret.x = x;
    dispPtr->caret.y = y;
    dispPtr->caret.height = height;
}

/*
 *----------------------------------------------------------------------
 *
 * Helper Functions
 *
 *----------------------------------------------------------------------
 */

static KeySym
XKBKeySymToX11KeySym(xkb_keysym_t xkb_sym)
{
    /* XKB keysyms are compatible with X11 keysyms. */
    return (KeySym)xkb_sym;
}

static unsigned int
GLFWModsToX11State(int glfw_mods)
{
    unsigned int state = 0;
    
    if (glfw_mods & GLFW_MOD_SHIFT)     state |= ShiftMask;
    if (glfw_mods & GLFW_MOD_CONTROL)   state |= ControlMask;
    if (glfw_mods & GLFW_MOD_ALT)       state |= Mod1Mask;
    if (glfw_mods & GLFW_MOD_SUPER)     state |= Mod4Mask;
    if (glfw_mods & GLFW_MOD_CAPS_LOCK) state |= LockMask;
    if (glfw_mods & GLFW_MOD_NUM_LOCK)  state |= Mod2Mask;
    
    return state;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */

