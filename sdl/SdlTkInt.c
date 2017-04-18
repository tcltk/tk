#include "tkInt.h"
#include "SdlTk.h"
#include "SdlTkInt.h"
#ifdef ANDROID
#include <android/log.h>
#endif

#undef TRACE_EVENTS

#ifdef TRACE_EVENTS
#ifdef ANDROID
#define EVLOG(...) __android_log_print(ANDROID_LOG_ERROR,"SDLEV",__VA_ARGS__)
#else
#define EVLOG(...) SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION,__VA_ARGS__)
#endif
#else
#define EVLOG(...)
#endif

SdlTkXInfo SdlTkX = { 0, 0, NULL };

#ifndef ANDROID
static int translate_zoom = 1;
#endif

#define TRANSLATE_RMB    1
#define TRANSLATE_PTZ    2
#define TRANSLATE_ZOOM   4
#define TRANSLATE_FINGER 8
#define TRANSLATE_FBTNS  16

/*
 *----------------------------------------------------------------------
 *
 * SendAppEvent --
 *
 *	Send virtual application events to all toplevel windows
 *	recursively.
 *
 * Results:
 *	First window to receive application event.
 *
 *----------------------------------------------------------------------
 */

static _Window *
SendAppEvent(XEvent *event, int *sentp, _Window *_w)
{
    _Window *result = NULL;

    while (_w != NULL) {
	if ((_w->tkwin != NULL) && (_w->tkwin->flags & TK_APP_TOP_LEVEL)) {
	    *sentp += 1;
	    if (*sentp == 1) {
		result = _w;
	    } else {
		event->xany.serial = _w->display->request;
		event->xany.display = _w->display;
		event->xany.window = (Window) _w;
		SdlTkQueueEvent(event);
	    }
	}
	if (_w->child != NULL) {
	    _Window *tmp = SendAppEvent(event, sentp, _w->child);

	    if ((result == NULL) && (tmp != NULL)) {
		result = tmp;
	    }
	}
	_w = _w->next;
    }
    if (result != NULL) {
	event->xany.serial = result->display->request;
	event->xany.display = result->display;
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkSendViewportUpdate --
 *
 *	Send virtual event <<ViewportUpdate>> to all toplevel windows.
 *
 * Results:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
SdlTkSendViewportUpdate(void)
{
    int nsent = 0;
    union {
	XEvent xe;
	XVirtualEvent ve;
    } event;

    memset(&event, 0, sizeof (event));
    event.xe.xany.type = VirtualEvent;
    event.xe.xany.send_event = False;
    event.xe.xany.window = (Window) SdlTkX.screen->root;
    event.xe.xbutton.root = (Window) SdlTkX.screen->root;
    event.xe.xany.display = SdlTkX.display;
    event.xe.xany.serial = SdlTkX.display->request;
    event.xe.xbutton.x = SdlTkX.viewport.x;
    event.xe.xbutton.y = SdlTkX.viewport.y;
    event.xe.xbutton.x_root = SdlTkX.viewport.w;
    event.xe.xbutton.y_root = SdlTkX.viewport.h;
    event.xe.xbutton.time = SdlTkX.time_count;
    event.xe.xbutton.state = (int) SDL_ceil(SdlTkX.scale * 10000);
    event.ve.name = (Tk_Uid) "ViewportUpdate";
    /* only TK_APP_TOP_LEVEL windows will get this */
    event.xe.xany.window = (Window)
	SendAppEvent(&event.xe, &nsent,
		     ((_Window *) SdlTkX.screen->root)->child);
    if (nsent > 0) {
	SdlTkQueueEvent(&event.xe);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SendPointerUpdate --
 *
 *	Send XUpdatePointerUpdate with mouse position and state.
 *	The XNextEvent routine calls Tk_UpdatePointer()
 *	with the event information.
 *
 * Results:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
SendPointerUpdate(Tk_Window tkwin, int x, int y, int state)
{
    union {
	XEvent xe;
	XUpdatePointerEvent pe;
    } event;

    memset(&event, 0, sizeof (event));
    event.pe.type = PointerUpdate;
    event.pe.serial = Tk_Display(tkwin)->request;
    event.pe.display = Tk_Display(tkwin);
    event.pe.window = Tk_WindowId(tkwin);
    event.pe.send_event = False;
    event.pe.x = x;
    event.pe.y = y;
    event.pe.state = state;
    event.pe.tkwin = tkwin;
    SdlTkQueueEvent(&event.xe);
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkAttachTkWindow --
 *
 *	Associates a TkWindow structure with the given X window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the tkwin field of the given _Window. Allocates a copy of
 *	the window name, if any.
 *
 *----------------------------------------------------------------------
 */

void
SdlTkAttachTkWindow(Window w, TkWindow *tkwin)
{
    _Window *_w = (_Window *) w;

    _w->tkwin = tkwin;
}

#ifdef ANDROID
/*
 *----------------------------------------------------------------------
 *
 * ConfigGLWindows --
 *
 *	Generate <Configure> event for flagged windows with GL context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A window event is added to the event queue.
 *
 *----------------------------------------------------------------------
 */

static void
ConfigGLWindows(Window w)
{
    _Window *_w = (_Window *) w;
    _w = _w->child;
    while (_w != NULL) {
	ConfigGLWindows((Window) _w);
	if (_w->gl_flags & 1) {
	    _w->gl_flags &= ~1;
	    if (_w->atts.map_state != IsUnmapped) {
		SdlTkGenerateConfigureNotify(NULL, (Window ) _w);
	    }
	}
	_w = _w->next;
    }
}
#endif

/*
 *----------------------------------------------------------------------
 *
 * SdlTkScreenChanged, SdlTkScreenRefresh --
 *
 *	Redraw entire display.
 *
 * Results:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
SdlTkScreenChanged(void)
{
    SdlTkX.draw_later |= SDLTKX_DRAW;
}

void
SdlTkScreenRefresh(void)
{
    _Window *child, *focus_window;
    Region tmpRgn;
#ifdef ANDROID
    SDL_GLContext currgl;
#endif

    if ((SdlTkX.draw_later & (SDLTKX_DRAW | SDLTKX_DRAWALL | SDLTKX_PRESENT))
	== SDLTKX_PRESENT) {
	SdlTkGfxPresent(SdlTkX.sdlrend, SdlTkX.sdltex);
	SdlTkX.draw_later &= ~SDLTKX_PRESENT;
    }

    if ((SdlTkX.draw_later & (SDLTKX_DRAW | SDLTKX_DRAWALL)) == 0) {
	return;
    }

#ifdef ANDROID
    /* Can happen when app is/gets paused. */
    if ((currgl = SDL_GL_GetCurrentContext()) == NULL) {
	EVLOG("SdlTkScreenRefresh: GL context is NULL");
	return;
    }
    /* Detect change of GL context. */
    if ((SdlTkX.gl_context != NULL) && (currgl != SdlTkX.gl_context)) {
	SDL_Texture *newtex;

	EVLOG("SdlTkScreenRefresh: GL context switching %p -> %p",
	      currgl, SdlTkX.gl_context);

	newtex = SDL_CreateTexture(SdlTkX.sdlrend,
				   SDL_PIXELFORMAT_RGB888,
				   SDL_TEXTUREACCESS_STREAMING,
				   SdlTkX.screen->width,
				   SdlTkX.screen->height);
	if (newtex != NULL) {
	    SDL_DestroyTexture(SdlTkX.sdltex);
	    SdlTkX.sdltex = newtex;
	    SdlTkX.gl_context = currgl;
	} else {
	    return;
	}
    } else if (SdlTkX.gl_context == NULL) {
	SdlTkX.gl_context = currgl;
    }
#endif

    tmpRgn = SdlTkRgnPoolGet();
    if (SdlTkX.screen_update_region == NULL) {
	SdlTkX.screen_update_region = SdlTkRgnPoolGet();
    }
    /* If areas of the root window were exposed, paint them now */
    if (SdlTkX.screen_dirty_region &&
	!XEmptyRegion(SdlTkX.screen_dirty_region)) {
	Uint32 pixel = SDL_MapRGB(SdlTkX.sdlsurf->format,
#ifdef ANDROID
				  0x00, 0x00, 0x00
#else
				  0x00, 0x4E, 0x98
#endif
				 );

	SdlTkGfxFillRegion(SdlTkX.screen->root, SdlTkX.screen_dirty_region,
			   pixel);
	XUnionRegion(SdlTkX.screen_dirty_region, SdlTkX.screen_update_region,
		     SdlTkX.screen_update_region);
	XSetEmptyRegion(SdlTkX.screen_dirty_region);
    }

    if (SdlTkX.draw_later & SDLTKX_DRAWALL) {
	XRectangle r;

	SdlTkX.draw_later &= ~SDLTKX_DRAWALL;
	r.x = r.y = 0;
	r.width = SdlTkX.screen->width;
	r.height = SdlTkX.screen->height;
	XUnionRectWithRegion(&r, SdlTkX.screen_update_region,
			     SdlTkX.screen_update_region);
    }

    focus_window = (_Window *) SdlTkX.focus_window_not_override;
    if (focus_window != NULL) {
	child = focus_window->parent;
	while ((child != NULL) && (child->dec == NULL)) {
	    child = child->parent;
	}
	if (child != NULL) {
	    focus_window = child->child;
 	}
    }

    /* Check each toplevel from highest to lowest */
    for (child = ((_Window *) SdlTkX.screen->root)->child;
	child != NULL;
	child = child->next) {

        if (child->atts.map_state == IsUnmapped) {
	    continue;
	}
	/* Keep track of which decframe was the "active" one. Redraw frames
	 * when changes occur. */
	if (child->dec != NULL) {
	    if ((child->child == (_Window *) focus_window) &&
		!SdlTkDecSetActive(child, -1) && SdlTkX.sdlfocus) {
		SdlTkDecSetDraw(child, 1);
		SdlTkDecSetActive(child, 1);
	    } else if ((child->child != (_Window *) focus_window) &&
		       SdlTkDecSetActive(child, -1)) {
		SdlTkDecSetDraw(child, 1);
		SdlTkDecSetActive(child, 0);
	    }
	    if (SdlTkDecSetDraw(child, -1)) {
		SdlTkDecDrawFrame(child);
		XUnionRegion(child->visRgn, child->dirtyRgn, child->dirtyRgn);
		SdlTkDecSetDraw(child, 0);
	    }
	}

	if (!XEmptyRegion(child->dirtyRgn)) {
	    XIntersectRegion(child->dirtyRgn, child->visRgnInParent, tmpRgn);

	    /* Add those areas to SdlTkX.screen_update_region. */
	    XOffsetRegion(tmpRgn, child->atts.x, child->atts.y);
	    XUnionRegion(tmpRgn, SdlTkX.screen_update_region,
			 SdlTkX.screen_update_region);

	    XSetEmptyRegion(child->dirtyRgn);
	}
    }

    SdlTkRgnPoolFree(tmpRgn);

#ifdef ANDROID
    if (!SdlTkX.in_background) {
	SdlTkGfxUpdateRegion(SdlTkX.sdlrend, SdlTkX.sdltex,
			     SdlTkX.sdlsurf, SdlTkX.screen_update_region);
    }
#else
    SdlTkGfxUpdateRegion(SdlTkX.sdlrend, SdlTkX.sdltex,
			 SdlTkX.sdlsurf, SdlTkX.screen_update_region);
#endif
    XSetEmptyRegion(SdlTkX.screen_update_region);
    SdlTkX.draw_later &= ~(SDLTKX_DRAW | SDLTKX_RENDCLR | SDLTKX_PRESENT);
    SdlTkX.frame_count++;
}

/*
 *----------------------------------------------------------------------
 *
 * AddToAccelRing --
 *
 *	Add accelerometer value to ring buffer given axis.
 *	Returns the delta to the last accelerometer value.
 *
 *----------------------------------------------------------------------
 */

#ifdef ANDROID

static int
AddToAccelRing(long time, short value, int axis)
{
    AccelRing *rp;
    int i, imax, dv = 0;
    long dt;

    if ((axis < 0) || (axis > 2)) {
	return dv;
    }
    rp = &SdlTkX.accel_ring[axis];
    imax = sizeof (rp->values) / sizeof (rp->values[0]);
    dt = (time - rp->time) / (1000 / SDLTK_FRAMERATE);
    if (dt >= imax) {
	for (i = 0; i < imax; i++) {
	    rp->values[i] = value;
	}
	rp->index = 0;
	rp->time = time;
	return dv;
    }
    if (dt <= 0) {
	dv = value - rp->values[rp->index];
	/* Don't update index and time fields. */
    } else {
	int prevval = rp->values[rp->index];

	if (dt > 1) {
	    /* Linear interpolate missing values. */
	    prevval <<= 8;
	    dv = ((value << 8) - prevval) / dt;
	    for (i = 0; i < dt - 1; i++) {
		prevval += dv;
		rp->index++;
		if (rp->index >= imax) {
		    rp->index = 0;
		}
		rp->values[rp->index] = prevval >> 8;
	    }
	    prevval >>= 8;
	}
	dv = value - prevval;
	rp->index++;
	if (rp->index >= imax) {
	    rp->index = 0;
	}
	rp->time = time;
    }
    rp->values[rp->index] = value;
    return dv;
}

#endif

/*
 *----------------------------------------------------------------------
 *
 * TranslatePointer, TranslateFinger --
 *
 *	Translate device (screen) coordinates to X11 coordinates
 *	and vice versa.
 *
 *----------------------------------------------------------------------
 */

static void
TranslatePointer(int rev, int *x, int *y)
{
    if (SdlTkX.draw_later & SDLTKX_SCALED) {
	if (rev) {
	    /* X to screen */
	    *x = (int) ((*x - SdlTkX.viewport.x) * SdlTkX.scale);
	    *y = (int) ((*y - SdlTkX.viewport.y) * SdlTkX.scale);
	    if (SdlTkX.outrect) {
		*x += SdlTkX.outrect->x;
		*y += SdlTkX.outrect->y;
	    }
	} else {
	    /* screen to X */
	    if (SdlTkX.outrect) {
		*x -= SdlTkX.outrect->x;
		*y -= SdlTkX.outrect->y;
	    }
	    *x = (int) (*x / SdlTkX.scale) + SdlTkX.viewport.x;
	    *y = (int) (*y / SdlTkX.scale) + SdlTkX.viewport.y;
	}
    } else if (SdlTkX.outrect) {
	if (rev) {
	    /* X to screen */
	    *x += SdlTkX.outrect->x;
	    *y += SdlTkX.outrect->y;
	} else {
	    /* screen to X */
	    *x -= SdlTkX.outrect->x;
	    *y -= SdlTkX.outrect->y;
	}
    }
}

int
SdlTkGetMouseState(int *x, int *y)
{
    int state;

    state = SDL_GetMouseState(x, y);
    TranslatePointer(0, x, y);
    return state;
}

static void
FingerToScreen(SDL_Event *in, int *x, int *y)
{
    int sw, sh;

    SDL_GetWindowSize(SdlTkX.sdlscreen, &sw, &sh);
    *x = (int) (in->tfinger.x * sw);
    *y = (int) (in->tfinger.y * sh);
    TranslatePointer(0, x, y);
}

#ifdef ANDROID

static void
TranslateFinger(SDL_Event *in, SDL_Event *out)
{
    *out = *in;
    if (SdlTkX.draw_later & SDLTKX_SCALED) {
	int x, y, sw, sh, w, h;

	SDL_GetWindowSize(SdlTkX.sdlscreen, &sw, &sh);
	x = (int) (out->tfinger.x * sw);
	y = (int) (out->tfinger.y * sh);
	TranslatePointer(0, &x, &y);
	if (SdlTkX.root_w) {
	    w = SdlTkX.root_w;
	    h = SdlTkX.root_h;
	} else {
	    w = SdlTkX.sdlsurf->w;
	    h = SdlTkX.sdlsurf->h;
	}
	out->tfinger.x = (float) x / w;
	out->tfinger.y = (float) y / h;
	out->tfinger.dx /= SdlTkX.scale;
	out->tfinger.dy /= SdlTkX.scale;
    }
}

#endif

#ifdef ANDROID

/*
 *----------------------------------------------------------------------
 *
 * Data and code to translate touch events to middle/right mouse
 * button and motion events.
 *
 *----------------------------------------------------------------------
 */

static struct TranslateInfo {
    int enabled;
    unsigned long now, when;
    void (*function)(ClientData);
    int state;
    int count;
    int fingerBits;
    int nFingers;
    int pinchDelta;
    double pinchDist;
    int pinchX, pinchY;
    SDL_Event sdl_event;
    SDL_Event mmb_event;
    SDL_TouchFingerEvent finger[10];
} TranslateInfo = {
    TRANSLATE_RMB | TRANSLATE_ZOOM | TRANSLATE_FINGER,
    0, 0, NULL, 0, 0, 0,
};

static void
TranslateStop(void)
{
    struct TranslateInfo *info = &TranslateInfo;

    info->function = NULL;
    info->count = 0;
    info->state = 0;
}

static void
TranslateTimer(ClientData clientData)
{
    struct TranslateInfo *info = (struct TranslateInfo *) clientData;
    SDL_Event e;

    EVLOG("                TIMER#0 FIRED");
    e = info->sdl_event;
    e.button.type = SDL_MOUSEBUTTONDOWN;
    e.button.button = SDL_BUTTON_RIGHT;
    e.button.state = SDL_PRESSED;
    SDL_PeepEvents(&e, 1, SDL_ADDEVENT, 0, 0);
}

static void
TranslateTimer1(ClientData clientData)
{
    struct TranslateInfo *info = (struct TranslateInfo *) clientData;

    EVLOG("                TIMER#1 FIRED");
    info->function = TranslateTimer;
    info->when = info->now + 900;
    info->state = 5;
    EVLOG("                TIMER ON ST=%d", info->state);
    SDL_PeepEvents(&info->sdl_event, 1, SDL_ADDEVENT, 0, 0);
}

static void
TranslateTimer2(ClientData clientData)
{
    struct TranslateInfo *info = (struct TranslateInfo *) clientData;

    EVLOG("                TIMER#2 FIRED");
    if (--info->count <= 0) {
	info->count = 0;
	info->state = 0;
    } else {
	int dx, dy;

	dx = (info->mmb_event.motion.xrel * 9) / 100;
	dx = (dx < 0) ? -dx : dx;
	dx = (dx < 1) ? 1 : dx;
	dy = (info->mmb_event.motion.yrel * 9) / 100;
	dy = (dy < 0) ? -dy : dy;
	dy = (dy < 1) ? 1 : dy;
	if (info->mmb_event.motion.xrel > 0) {
	    info->mmb_event.motion.xrel -= dx;
	    if (info->mmb_event.motion.xrel < 0) {
		info->mmb_event.motion.xrel = 1;
	    }
	} else if (info->mmb_event.motion.xrel < 0) {
	    info->mmb_event.motion.xrel += dx;
	    if (info->mmb_event.motion.xrel > 0) {
		info->mmb_event.motion.xrel = -1;
	    }
	}
	if (info->mmb_event.motion.yrel > 0) {
	    info->mmb_event.motion.yrel -= dy;
	    if (info->mmb_event.motion.yrel < 0) {
		info->mmb_event.motion.yrel = 1;
	    }
	} else if (info->mmb_event.motion.yrel < 0) {
	    info->mmb_event.motion.yrel += dy;
	    if (info->mmb_event.motion.yrel > 0) {
		info->mmb_event.motion.yrel = -1;
	    }
	}
	info->mmb_event.motion.x += info->mmb_event.motion.xrel;
	info->mmb_event.motion.y += info->mmb_event.motion.yrel;
	EVLOG("   MOUSEMOTION  X=%d Y=%d ID=%d S=%d dx=%d dy=%d",
	      info->mmb_event.motion.x, info->mmb_event.motion.y,
	      info->mmb_event.motion.which, info->mmb_event.motion.state,
	      info->mmb_event.motion.xrel, info->mmb_event.motion.yrel);
	if ((info->mmb_event.motion.xrel) || (info->mmb_event.motion.yrel)) {
	    SDL_PeepEvents(&info->mmb_event, 1, SDL_ADDEVENT, 0, 0);
	} else {
	    info->count = 0;
	    info->state = 0;
	}
    }
    if (info->state) {
	info->function = TranslateTimer2;
	info->when = info->now + 100;
	EVLOG("                TIMER ON ST=%d", info->state);
    } else {
	SDL_Event mmb;

	mmb.type = SDL_MOUSEBUTTONUP;
	mmb.button.which = info->mmb_event.motion.which;
	mmb.button.button = SDL_BUTTON_MIDDLE;
	mmb.button.state = SDL_RELEASED;
	mmb.button.x = info->mmb_event.motion.x;
	mmb.button.y = info->mmb_event.motion.y;
	SDL_PeepEvents(&mmb, 1, SDL_ADDEVENT, 0, 0);
   }
}

#endif

static int
FixKeyCode(int ch)
{
    if ((ch >= 'a') && (ch <= 'z')) {
	return SDL_SCANCODE_A + ch - 'a';
    } else if ((ch >= 'A') && (ch <= 'Z')) {
	return SDL_SCANCODE_A + ch - 'A';
    }
    switch (ch) {
    case '0':		return SDL_SCANCODE_0;
    case '1':		return SDL_SCANCODE_1;
    case '2':		return SDL_SCANCODE_2;
    case '3':		return SDL_SCANCODE_3;
    case '4':		return SDL_SCANCODE_4;
    case '5':		return SDL_SCANCODE_5;
    case '6':		return SDL_SCANCODE_6;
    case '7':		return SDL_SCANCODE_7;
    case '8':		return SDL_SCANCODE_8;
    case '9':		return SDL_SCANCODE_9;
    case ' ':		return SDL_SCANCODE_SPACE;
    case ',':		return SDL_SCANCODE_COMMA;
    case '.':		return SDL_SCANCODE_PERIOD;
    case '/':		return SDL_SCANCODE_SLASH;
    case '`':		return SDL_SCANCODE_GRAVE;
    case ';':		return SDL_SCANCODE_SEMICOLON;
    case '\'':		return SDL_SCANCODE_APOSTROPHE;
    case '\\':		return SDL_SCANCODE_BACKSLASH;
    case '\r':		return SDL_SCANCODE_RETURN;
    case '\010':	return SDL_SCANCODE_BACKSPACE;
    case '\t':		return SDL_SCANCODE_TAB;
    case '\033':	return SDL_SCANCODE_ESCAPE;
    case '-':		return SDL_SCANCODE_MINUS;
    case '=':		return SDL_SCANCODE_EQUALS;
    case '[':		return SDL_SCANCODE_LEFTBRACKET;
    case ']':		return SDL_SCANCODE_RIGHTBRACKET;
    }
    return 0;
}

static int
MkTransChars(XKeyEvent *ev)
{
    int ch = 0;

    if ((ev->keycode >= SDL_SCANCODE_A) && (ev->keycode <= SDL_SCANCODE_Z)) {
	ev->trans_chars[0] = ev->keycode - SDL_SCANCODE_A + 'a';
	ev->nbytes = 1;
	return 1;
    }
    switch (ev->keycode) {
    case SDL_SCANCODE_0:		ch = '0'; break;
    case SDL_SCANCODE_1:		ch = '1'; break;
    case SDL_SCANCODE_2:		ch = '2'; break;
    case SDL_SCANCODE_3:		ch = '3'; break;
    case SDL_SCANCODE_4:		ch = '4'; break;
    case SDL_SCANCODE_5:		ch = '5'; break;
    case SDL_SCANCODE_6:		ch = '6'; break;
    case SDL_SCANCODE_7:		ch = '7'; break;
    case SDL_SCANCODE_8:		ch = '8'; break;
    case SDL_SCANCODE_9:		ch = '9'; break;
    case SDL_SCANCODE_SPACE:		ch = ' '; break;
    case SDL_SCANCODE_COMMA:		ch = ','; break;
    case SDL_SCANCODE_PERIOD:		ch = '.'; break;
    case SDL_SCANCODE_SLASH:		ch = '/'; break;
    case SDL_SCANCODE_GRAVE:		ch = '`'; break;
    case SDL_SCANCODE_SEMICOLON:	ch = ';'; break;
    case SDL_SCANCODE_APOSTROPHE:	ch = '\''; break;
    case SDL_SCANCODE_BACKSLASH:	ch = '\\'; break;
    case SDL_SCANCODE_MINUS:		ch = '-'; break;
    case SDL_SCANCODE_EQUALS:		ch = '='; break;
    case SDL_SCANCODE_LEFTBRACKET:	ch = '['; break;
    case SDL_SCANCODE_RIGHTBRACKET:	ch = ']'; break;
    }
    if (ch) {
	ev->trans_chars[0] = ch;
	ev->nbytes = 1;
	return 1;
    }
    return 0;
}

static int
ProcessTextInput(XEvent *event, int no_rel, int sdl_mod,
		 const char *text, int len)
{
    int i, n, n2, ulen = Tcl_NumUtfChars(text, len);
    char buf[TCL_UTF_MAX];

    if (ulen <= 0) {
	SdlTkX.keyuc = 0;
	return 0;
    }
    if (sdl_mod & KMOD_RALT) {
	event->xkey.state &= ~Mod4Mask;
    }
    for (i = 0; i < ulen; i++) {
	Tcl_UniChar ch;

	n = Tcl_UtfToUniChar(text, &ch);
	n2 = 0;

	/* Deal with surrogate pairs */
#if TCL_UTF_MAX > 4
	if ((ch >= 0xd800) && (ch <= 0xdbff)) {
	    Tcl_UniChar ch2;

	    if (i + 1 < ulen) {
		n2 = Tcl_UtfToUniChar(text + n, &ch2);
		if ((ch2 >= 0xdc00) && (ch2 <= 0xdfff)) {
		    ch = ((ch & 0x3ff) << 10) | (ch2 & 0x3ff);
		    ch += 0x10000;
		    ++i;
		} else {
		    ch = 0xfffd;
		    n2 = 0;
		}
	    } else {
		SdlTkX.keyuc = ch;
		return -1;
	    }
	} else if ((ch >= 0xdc00) && (ch <= 0xdfff)) {
	    if (SdlTkX.keyuc) {
		ch = ((SdlTkX.keyuc & 0x3ff) << 10) | (ch & 0x3ff);
		ch += 0x10000;
	    } else {
		ch = 0xfffd;
	    }
	    SdlTkX.keyuc = 0;
	} else if ((ch == 0xfffe) || (ch == 0xffff)) {
	    ch = 0xfffd;
	    SdlTkX.keyuc = 0;
	} else {
	    SdlTkX.keyuc = 0;
	}
#else
	if ((ch >= 0xd800) && (ch <= 0xdbff)) {
	    Tcl_UniChar ch2;

	    if (i + 1 < ulen) {
		n2 = Tcl_UtfToUniChar(text + n, &ch2);
		if ((ch2 >= 0xdc00) && (ch2 <= 0xdfff)) {
		    ++i;
		} else {
		    n2 = 0;
		}
	    }
	    ch = 0xfffd;
	} else if ((ch >= 0xdc00) && (ch <= 0xdfff)) {
	    ch = 0xfffd;
	} else if ((ch == 0xfffe) || (ch == 0xffff)) {
	    ch = 0xfffd;
	}
	SdlTkX.keyuc = 0;
#endif
	event->xkey.nbytes = Tcl_UniCharToUtf(ch, buf);
	event->xkey.time = SdlTkX.time_count;
	if (event->xkey.nbytes > sizeof (event->xkey.trans_chars)) {
	    event->xkey.nbytes = sizeof (event->xkey.trans_chars);
	}
	memcpy(event->xkey.trans_chars, buf, event->xkey.nbytes);
	if (len == 1) {
	    event->xkey.keycode = FixKeyCode(event->xkey.trans_chars[0]);
	} else {
	    event->xkey.keycode = -1;
	}
	text += n + n2;

	/* Queue the KeyPress */
	EVLOG("   KEYPRESS:  CODE=0x%02X  UC=0x%X", event->xkey.keycode, ch);
	event->type = KeyPress;
	if (!no_rel || (i < ulen - 1)) {
	    SdlTkQueueEvent(event);
	    /* Queue the KeyRelease except for the last */
	    event->type = KeyRelease;
	    if (i < ulen - 1) {
		EVLOG(" KEYRELEASE:  CODE=0x%02X", event->xkey.keycode);
		SdlTkQueueEvent(event);
	    }
	}
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkTranslateEvent --
 *
 *	Turn an SDL_Event into an XEvent.
 *
 * Results:
 *	Return 1 if the SDL event was translated into an XEvent;
 *	otherwise return 0.
 *
 * Side effects:
 *	Pointer events other than MouseWheelEvent are handled by
 *	Tk_UpdatePointer(), which has various side effects.
 *	If the event type is SDL_QUIT, Tcl_Exit() is called.
 *
 *----------------------------------------------------------------------
 */

int
SdlTkTranslateEvent(SDL_Event *sdl_event, XEvent *event, unsigned long now_ms)
{
    int x, y, state = 0;
    SDL_Event fix_sdl_event;
    SDL_Event txt_sdl_event;
    const char *evname = NULL;
#ifdef ANDROID
    struct TranslateInfo *info = &TranslateInfo;
    int doFinger = 0;
    SDL_Event tmp_event;
#endif

#ifdef ANDROID

#ifdef TRACE_EVENTS
    if (sdl_event->type < SDL_USEREVENT) {
	if ((sdl_event->type != SDL_JOYAXISMOTION) ||
	    SdlTkX.accel_enabled ||
	    (sdl_event->jaxis.which != SdlTkX.accel_id)) {
	    EVLOG("                T=%ld dt=%ld ST=%d",
		  now_ms, now_ms - info->now, info->state);
	}
    }
#endif

    /* Handle timer */
    info->now = now_ms;
    if (info->function != NULL) {
	if ((long) info->when - (long) info->now <= 0) {
	    void (*function)(ClientData) = info->function;

	    info->function = NULL;
	    function((ClientData) info);
	}
    }

#ifdef TRACE_EVENTS
    switch (sdl_event->type) {
    case SDL_MOUSEBUTTONUP:
	EVLOG("EV=MOUSEUP      X=%d Y=%d ID=%d B=%d",
	      sdl_event->button.x, sdl_event->button.y,
	      sdl_event->button.which, sdl_event->button.button);
	break;
    case SDL_MOUSEBUTTONDOWN:
	EVLOG("EV=MOUSEDOWN    X=%d Y=%d ID=%d B=%d",
	      sdl_event->button.x, sdl_event->button.y,
	      sdl_event->button.which, sdl_event->button.button);
	break;
    case SDL_MOUSEMOTION:
	EVLOG("EV=MOUSEMOTION  X=%d Y=%d ID=%d S=%d dx=%d dy=%d",
	      sdl_event->motion.x, sdl_event->motion.y,
	      sdl_event->motion.which, sdl_event->motion.state,
	      sdl_event->motion.xrel, sdl_event->motion.yrel);
	break;
    case SDL_MOUSEWHEEL:
	EVLOG("EV=MOUSEWHEEL   X=%d Y=%d ID=%d",
	      sdl_event->wheel.x, sdl_event->wheel.y,
	      sdl_event->wheel.which);
	break;
    case SDL_FINGERDOWN:
	EVLOG("EV=FINGERDOWN   X=%g Y=%g ID=%lld dx=%g dy=%g",
	      sdl_event->tfinger.x, sdl_event->tfinger.y,
	      sdl_event->tfinger.fingerId,
	      sdl_event->tfinger.dx, sdl_event->tfinger.dy);
	break;
    case SDL_FINGERUP:
	EVLOG("EV=FINGERUP     X=%g Y=%g ID=%lld dx=%g dy=%g",
	      sdl_event->tfinger.x, sdl_event->tfinger.y,
	      sdl_event->tfinger.fingerId,
	      sdl_event->tfinger.dx, sdl_event->tfinger.dy);
	break;
    case SDL_FINGERMOTION:
	EVLOG("EV=FINGERMOTION X=%g Y=%g ID=%lld dx=%g dy=%g",
	      sdl_event->tfinger.x, sdl_event->tfinger.y,
	      sdl_event->tfinger.fingerId,
	      sdl_event->tfinger.dx, sdl_event->tfinger.dy);
	break;
    case SDL_MULTIGESTURE:
	EVLOG("EV=MULTIGESTURE X=%g Y=%g NF=%d d=%g t=%g",
	      sdl_event->mgesture.x, sdl_event->mgesture.y,
	      sdl_event->mgesture.numFingers,
	      sdl_event->mgesture.dTheta, sdl_event->mgesture.dDist);
	break;
    }
#endif

    if (!(info->enabled & (TRANSLATE_PTZ | TRANSLATE_ZOOM))) {
	goto skipPZ;
    }

    /*
     * Pinch-to-zoom two finger detection.
     */

    if ((sdl_event->type == SDL_FINGERDOWN) &&
	(sdl_event->tfinger.fingerId >= 0) &&
	(sdl_event->tfinger.fingerId < 10)) {
	info->nFingers++;
	info->fingerBits |= 1 << sdl_event->tfinger.fingerId;
	info->finger[sdl_event->tfinger.fingerId] = sdl_event->tfinger;
	doFinger = ((info->fingerBits & 3) == 3) ? 2 : 0;
    } else if ((sdl_event->type == SDL_FINGERUP) &&
	       (sdl_event->tfinger.fingerId >= 0) &&
	       (sdl_event->tfinger.fingerId < 10)) {
	int oldBits = info->fingerBits;

	info->nFingers--;
	info->fingerBits &= ~(1 << sdl_event->tfinger.fingerId);
	if (info->fingerBits == 0) {
	    info->nFingers = 0;
	}
	if (info->nFingers < 2) {
	    info->pinchDelta = 0;
	    info->pinchDist = 0;
	    info->pinchX = info->pinchY = 0;
	}
	info->finger[sdl_event->tfinger.fingerId] = sdl_event->tfinger;
	doFinger = sdl_event->tfinger.fingerId + 3;
	if ((oldBits & 3) != 3 || (doFinger > 4)) {
	    doFinger = 0;
	}
    } else if ((sdl_event->type == SDL_FINGERMOTION) &&
	       (sdl_event->tfinger.fingerId >= 0) &&
	       (sdl_event->tfinger.fingerId < 10)) {
	info->finger[sdl_event->tfinger.fingerId] = sdl_event->tfinger;
	doFinger = ((info->fingerBits & 3) == 3 &&
		    (sdl_event->tfinger.fingerId >= 0) &&
		    (sdl_event->tfinger.fingerId < 2)) ? 1 : 0;
    }
    if (doFinger) {
	float dx, dy, cx, cy;
	double dist, phi;
	Tk_Window tkwin;
	int xx, yy, pdx = 0, pdy = 0, ddist = 0, needFingers;

	needFingers = (info->enabled & TRANSLATE_PTZ) ? 3 : 2;
	TranslateStop();
	dx = info->finger[1].x - info->finger[0].x;
	dy = info->finger[1].y - info->finger[0].y;
	dist = sqrt(dx * dx + dy * dy);
	phi = atan2(dy, dx);
	cx = info->finger[0].x + dx / 2.0;
	cy = info->finger[0].y + dy / 2.0;
	EVLOG("EV=PINCHTOZOOM  X=%g Y=%g DIST=%g PHI=%g",
	      cx, cy, dist, phi);

	dx = SdlTkX.screen->width * dx;
	dy = SdlTkX.screen->height * dy;
	xx = sqrt(dx * dx + dy * dy);
	yy = 64 * 180 * phi / M_PI;

	x = SdlTkX.screen->width * cx;
	y = SdlTkX.screen->height * cy;

	if (info->pinchDelta) {
	    ddist = xx - info->pinchDist;
	    pdx = x - info->pinchX;
	    pdy = info->pinchY - y;
	    EVLOG("                dDIST=%d dX=%d dY=%d", ddist, pdx, pdy);
	}
	info->pinchDist = xx;
	info->pinchX = x;
	info->pinchY = y;

	if ((info->enabled & TRANSLATE_ZOOM) && info->pinchDelta &&
	    (info->nFingers >= needFingers)) {
	    float dir = 0;

	    if ((info->nFingers == needFingers) &&
		(ddist <= -SdlTkX.nearby_pixels / 4)) {
		dir = 0.99;
	    } else if ((info->nFingers == needFingers) &&
		       (ddist >= SdlTkX.nearby_pixels / 4)) {
		dir = 1 / 0.99;
	    }
	    if (dir) {
		SdlTkZoomInt(x, y, dir);
	    } else if ((info->nFingers > needFingers) &&
		       ((pdx <= -2) || (pdx >= 2) ||
		        (pdy <= -2) || (pdy >= 2))) {
		SdlTkPanInt(-pdx, pdy);
	    }
	    goto skipTranslation;
	}

	info->pinchDelta = 1;

	if (!(info->enabled & TRANSLATE_PTZ)) {
	    goto skipTranslation;
	}
	TranslatePointer(0, &x, &y);
	xx = (int) (xx / SdlTkX.scale);

	if (SdlTkX.mouse_window != NULL) {
	    tkwin = (Tk_Window) SdlTkX.mouse_window->tkwin;
	}
	if (SdlTkX.capture_window != NULL) {
	    if ((tkwin != NULL) &&
		(Tk_Display(tkwin) != SdlTkX.capture_window->display)) {
		tkwin = (Tk_Window) SdlTkX.capture_window;
	    }
	}
	if (SdlTkX.keyboard_window != NULL) {
	    if ((tkwin != NULL) &&
		(Tk_Display(tkwin) != SdlTkX.keyboard_window->display)) {
		tkwin = (Tk_Window) SdlTkX.keyboard_window->tkwin;
	    }
	}
	if (tkwin != NULL) {
	    memset(event, 0, sizeof (*event));
	    event->xany.type = VirtualEvent;
	    event->xany.serial = Tk_Display(tkwin)->request;
	    event->xany.send_event = False;
	    event->xany.window = Tk_WindowId(tkwin);
	    event->xbutton.root = (Window) SdlTkX.screen->root;
	    event->xany.display = Tk_Display(tkwin);
	    event->xbutton.x = xx;
	    event->xbutton.y = yy;
	    event->xbutton.x_root = x;
	    event->xbutton.y_root = y;
	    event->xbutton.time = now_ms;
	    event->xbutton.state = doFinger - 1;
	    ((XVirtualEvent *) event)->name = (Tk_Uid) "PinchToZoom";
	    SdlTkQueueEvent(event);
	    goto skipTranslation;
	}
    }

skipPZ:
    if (!(info->enabled & TRANSLATE_RMB)) {
	goto skipTranslation;
    }

    /*
     * Middle/right mouse button/motion emulation.
     */

    if (info->fingerBits > 1) {
	TranslateStop();
	goto skipTranslation;
    }

    if ((info->state & 8) &&
	!(((sdl_event->type == SDL_MOUSEMOTION) &&
	   (sdl_event->button.which == SDL_TOUCH_MOUSEID) &&
	   (sdl_event->button.button == SDL_BUTTON_MIDDLE)) ||
	  (sdl_event->type == SDL_FINGERUP) ||
	  ((sdl_event->type >= SDL_JOYAXISMOTION) &&
	   (sdl_event->type <= SDL_JOYDEVICEREMOVED)) ||
	  (sdl_event->type < SDL_KEYDOWN) ||
	  (sdl_event->type > SDL_FINGERMOTION))) {
	info->function = NULL;
	info->count = 0;
	info->state &= ~8;
    }

    if (sdl_event->type == SDL_MOUSEBUTTONDOWN) {
	if (!(info->state & 1)) {
	    info->function = NULL;
	    if ((info->state == 0) &&
		(sdl_event->button.which == SDL_TOUCH_MOUSEID) &&
		(sdl_event->button.button == SDL_BUTTON_LEFT)) {
		info->state = 1;
		info->sdl_event = *sdl_event;
		info->function = TranslateTimer1;
		info->when = info->now + 100;
		EVLOG("                TIMER ON ST=%d", info->state);
		return 0;
	    }
	} else if ((info->state == 0) &&
		   (sdl_event->button.which == SDL_TOUCH_MOUSEID) &&
		   (sdl_event->button.button == SDL_BUTTON_LEFT)) {
	    return 0;
	}
    } else if ((sdl_event->type == SDL_MOUSEMOTION) &&
	       (sdl_event->motion.which == SDL_TOUCH_MOUSEID)) {
	int nearby = 21 * SdlTkX.nearby_pixels / 30;

	if (nearby < 5) {
	    nearby = 5;
	}
	info->sdl_event.button.x = sdl_event->motion.x;
	info->sdl_event.button.y = sdl_event->motion.y;
	if ((info->state == 1) &&
	    ((sdl_event->motion.xrel > nearby) ||
	     (sdl_event->motion.xrel < -nearby) ||
	     (sdl_event->motion.yrel > nearby) ||
	     (sdl_event->motion.yrel < -nearby))) {
	    SDL_PeepEvents(sdl_event, 1, SDL_ADDEVENT, 0, 0);
	    tmp_event = info->sdl_event;
	    sdl_event = &tmp_event;
	    sdl_event->button.button = SDL_BUTTON_MIDDLE;
	    info->mmb_event = *sdl_event;
	    info->state = 2;
	    info->function = NULL;
            EVLOG("                TIMER OFF ST=%d", info->state);
	} else if (info->state & 2) {
	    tmp_event = *sdl_event;
	    sdl_event = &tmp_event;
	    sdl_event->motion.state = SDL_BUTTON(SDL_BUTTON_MIDDLE);
	    info->mmb_event = *sdl_event;
	} else if (info->state == 1) {
	    return 0;
	}
    } else if (sdl_event->type == SDL_MOUSEBUTTONUP) {
	info->sdl_event.button.x = sdl_event->button.x;
	info->sdl_event.button.y = sdl_event->button.y;
	if ((info->state & 2) &&
	    (sdl_event->button.which == SDL_TOUCH_MOUSEID) &&
	    (sdl_event->button.button == SDL_BUTTON_LEFT)) {
	    info->state = 8;
	    info->count = 7;
	    info->function = TranslateTimer2;
	    info->when = info->now + 100;
	    EVLOG("                TIMER ON ST=%d", info->state);
	    return 0;
	} else if ((info->state & 1) &&
		   (sdl_event->button.which == SDL_TOUCH_MOUSEID) &&
		   (sdl_event->button.button == SDL_BUTTON_LEFT)) {
	    if (info->state == 1) {
		SDL_PeepEvents(sdl_event, 1, SDL_ADDEVENT, 0, 0);
		sdl_event = &info->sdl_event;
	    } else {
		SDL_PeepEvents(sdl_event, 1, SDL_ADDEVENT, 0, 0);
		tmp_event = *sdl_event;
		sdl_event = &tmp_event;
		sdl_event->button.button = SDL_BUTTON_RIGHT;
	    }
	    info->function = NULL;
	    info->state = 0;
            EVLOG("                TIMER OFF ST=%d", info->state);
	}
    }

skipTranslation:
    ;
#endif

    switch (sdl_event->type) {

    /* Drop target support, maybe later */
    case SDL_DROPBEGIN:
    case SDL_DROPCOMPLETE:
	return 0;
    case SDL_DROPFILE:
    case SDL_DROPTEXT:
	if (sdl_event->drop.file != NULL) {
	    SDL_free(sdl_event->drop.file);
	}
	return 0;

    case SDL_TEXTINPUT:
    case SDL_TEXTEDITING:
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
	Window fwin;

#ifdef _WIN32
	switch (sdl_event->type) {
	case SDL_KEYDOWN:
	case SDL_KEYUP:
	    /* Should immediately queue following SDL_TEXTINPUT events */
	    SDL_PumpEvents();
	}
#endif
	state = SDL_GetMouseState(&x, &y);
	TranslatePointer(0, &x, &y);

	fwin = (SdlTkX.focus_window != None) ?
	    SdlTkX.focus_window : (Window) SdlTkX.screen->root;

	event->type = (sdl_event->type == SDL_KEYUP)
	    ? KeyRelease : KeyPress;
	event->xkey.serial = ((_Window *) fwin)->display->request;
	event->xkey.send_event = False;
	event->xkey.display = ((_Window *) fwin)->display;
	event->xkey.window = fwin;
	event->xkey.root = (Window) SdlTkX.screen->root;
	event->xkey.subwindow = None;
	event->xkey.time = now_ms;
	event->xkey.x = x;
	event->xkey.y = y;
	event->xkey.x_root = x;
	event->xkey.y_root = y;

	event->xkey.state = 0;
	if (state & SDL_BUTTON(1)) {
	    event->xkey.state |= Button1Mask;
	}
	if (state & SDL_BUTTON(2)) {
	    event->xkey.state |= Button2Mask;
	}
	if (state & SDL_BUTTON(3)) {
	    event->xkey.state |= Button3Mask;
	}

#ifdef TRACE_EVENTS
	if (sdl_event->type == SDL_TEXTINPUT) {
	    EVLOG("  TEXTINPUT:  '%s'", sdl_event->text.text);
	} else if (sdl_event->type == SDL_TEXTEDITING) {
	    EVLOG("TEXTEDITING:  '%s'", sdl_event->edit.text);
	} else if (sdl_event->type == SDL_KEYDOWN) {
	    EVLOG("    KEYDOWN:  CODE=0x%02X  MOD=0x%X",
		  sdl_event->key.keysym.scancode,
		  sdl_event->key.keysym.mod);
	} else if (sdl_event->type == SDL_KEYUP) {
	    EVLOG("      KEYUP:  CODE=0x%02X  MOD=0x%X",
		  sdl_event->key.keysym.scancode,
		  sdl_event->key.keysym.mod);
	}
#endif

	event->xkey.keycode = -1;

	if ((sdl_event->type != SDL_TEXTINPUT) &&
	    (sdl_event->type != SDL_TEXTEDITING)) {
	    int scancode = sdl_event->key.keysym.scancode;

	    if (sdl_event->key.keysym.mod & KMOD_LALT) {
		event->xkey.state |= Mod1Mask;
	    }
	    if (sdl_event->key.keysym.mod & KMOD_RALT) {
		event->xkey.state |= Mod4Mask;
	    }
	    if (sdl_event->key.keysym.mod & KMOD_CAPS) {
		event->xkey.state |= LockMask;
	    }
	    if (sdl_event->key.keysym.mod & KMOD_CTRL) {
		event->xkey.state |= ControlMask;
	    }
	    if (sdl_event->key.keysym.mod & KMOD_NUM) {
		event->xkey.state |= Mod2Mask;
	    }
	    if (sdl_event->key.keysym.mod & KMOD_SHIFT) {
		event->xkey.state |= ShiftMask;
	    }
	    /* SDL_SCANCODE_xxx */
	    event->xkey.keycode = scancode;
	}
#ifdef _WIN32
	/* fixup AltGr on Windows */
	if ((sdl_event->key.keysym.mod & (KMOD_CTRL | KMOD_RALT)) ==
	    (KMOD_LCTRL | KMOD_RALT)) {
		event->xkey.state &= ~ControlMask;
	}
#endif

	event->xkey.same_screen = True;

	event->xkey.nbytes = 0;

	if (sdl_event->type == SDL_TEXTINPUT) {
	    int len = strlen(sdl_event->text.text);

	    if (len == 0) {
		return 0;
	    }
	    if (ProcessTextInput(event, 0, 0, sdl_event->text.text, len) <= 0) {
		return 0;
	    }
	} else if (sdl_event->type == SDL_TEXTEDITING) {
	    /* TODO: what can we do here? */
	    return 0;
	} else if ((sdl_event->type == SDL_KEYDOWN) &&
		   (SDL_PeepEvents(&txt_sdl_event, 1, SDL_PEEKEVENT,
				   SDL_TEXTINPUT, SDL_TEXTINPUT) == 1) &&
		   (SDL_PeepEvents(&txt_sdl_event, 1, SDL_GETEVENT,
				   SDL_TEXTINPUT, SDL_TEXTINPUT) == 1)) {
	    int len = strlen(txt_sdl_event.text.text);

	    if (len <= 0) {
		goto doNormalKeyEvent;
	    }
	    len = ProcessTextInput(event, 1, sdl_event->key.keysym.mod,
				   txt_sdl_event.text.text, len);
	    if (len == 0) {
		goto doNormalKeyEvent;
	    } else if (len < 0) {
		return 0;
	    }
	} else if ((sdl_event->type == SDL_KEYDOWN) ||
		   (sdl_event->type == SDL_KEYUP)) {
doNormalKeyEvent:
	    /* Keypad mapping */
	    if ((event->xkey.keycode >= SDL_SCANCODE_KP_0) &&
		(event->xkey.keycode <= SDL_SCANCODE_KP_9) &&
		(sdl_event->key.keysym.mod & KMOD_NUM)) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '0' +
		    (event->xkey.keycode - SDL_SCANCODE_KP_0);
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_DIVIDE) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '/';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_MULTIPLY) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '*';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_MINUS) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '-';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_PLUS) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '+';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_ENTER) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '\r';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_PERIOD) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '.';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_COMMA) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = ',';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_EQUALS) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '=';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_LEFTPAREN) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '(';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_RIGHTPAREN) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = ')';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_LEFTBRACE) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '{';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_RIGHTBRACE) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '}';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_VERTICALBAR) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '|';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_TAB) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '\t';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_SPACE) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = ' ';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_EXCLAM) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '!';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_AT) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '@';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_HASH) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '#';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_COLON) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = ':';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_AMPERSAND) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '&';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_LESS) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '<';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_GREATER) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '>';
	    } else if (event->xkey.keycode == SDL_SCANCODE_KP_PERCENT) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = '%';
		/* normal mapping */
	    } else if (event->xkey.keycode == SDL_SCANCODE_SPACE) {
		event->xkey.nbytes = 1;
		event->xkey.trans_chars[0] = ' ';
	    } else {
		MkTransChars(&event->xkey);
	    }
	    if (event->xkey.nbytes > 0) {
		EVLOG(" %s:             TRANS=0x%X",
		      (event->type == KeyRelease) ? "KEYRELEASE" :
		      "  KEYPRESS", event->xkey.trans_chars[0]);
	    }
	}
	break;
    }

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEMOTION: {
	_Window *_w;
	int xstate = 0, othergrab = 0;
	Tk_Window tkwin;
	SDL_Keymod mod;

#ifdef ANDROID
	if ((info->enabled &
	     (TRANSLATE_RMB | TRANSLATE_PTZ | TRANSLATE_ZOOM)) &&
	    (info->fingerBits > 1)) {
	    return 0;
	}
#endif
	switch (sdl_event->type) {
	case SDL_MOUSEBUTTONUP:
	case SDL_MOUSEBUTTONDOWN:
	    x = sdl_event->button.x;
	    y = sdl_event->button.y;
	    state = SDL_BUTTON(sdl_event->button.button);
	    break;
	case SDL_MOUSEMOTION: {
	    int dx, dy;

	    fix_sdl_event = *sdl_event;
	    sdl_event = &fix_sdl_event;
	    x = sdl_event->motion.x;
	    y = sdl_event->motion.y;
	    dx = sdl_event->motion.xrel;
	    dy = sdl_event->motion.yrel;
	    if ((x <= 0) && (dx < 0)) {
		dx = 0;
	    } else if ((x >= SdlTkX.sdlsurf->w - 1) && (dx > 0)) {
		dx = 0;
	    }
	    if ((y <= 0) && (dy < 0)) {
		dy = 0;
	    } else if ((y >= SdlTkX.sdlsurf->h - 1) && (dy > 0)) {
		dy = 0;
	    }
	    sdl_event->motion.xrel = (int) (dx / SdlTkX.scale);
	    sdl_event->motion.yrel = (int) (dy / SdlTkX.scale);
	    state = sdl_event->motion.state;
#ifdef ANDROID
	    if ((sdl_event->button.which == SDL_TOUCH_MOUSEID) &&
		(sdl_event->button.button == SDL_BUTTON_LEFT)) {
		TranslateStop();
	    }
#else
	    if (state == 0) {
		int dwx = x, dwy = y;

		TranslatePointer(0, &dwx, &dwy);
		if (dwx < SdlTkX.viewport.x + 10) {
		    dwx = -8;
		} else if (dwx > SdlTkX.viewport.w + SdlTkX.viewport.x - 10) {
		    dwx = 8;
		} else {
		    dwx = 0;
		}
		if (dwy < SdlTkX.viewport.y + 10) {
		    dwy = -8;
		} else if (dwy > SdlTkX.viewport.h + SdlTkX.viewport.y - 10) {
		    dwy = 8;
		} else {
		    dwy = 0;
		}
		if (dwx || dwy) {
		    SdlTkPanInt(dwx, dwy);
		}
	    }
#endif
	    break;
	}
	}

	/* now in X coords */
	TranslatePointer(0, &x, &y);

	_w = SdlTkPointToWindow((_Window *) SdlTkX.screen->root,
				x, y, True, True);
	SdlTkX.mouse_window = _w;

	/*
	 * Click in background window to raise it,
	 * unless a Tk grab is on.
	 */
	if (!IS_ROOT(_w) && SdlTkGrabCheck(_w, &othergrab) &&
	    (sdl_event->type == SDL_MOUSEBUTTONDOWN) &&
	    ((sdl_event->button.which == SDL_TOUCH_MOUSEID) ||
	     (sdl_event->button.button == SDL_BUTTON_LEFT))) {
	    SdlTkBringToFrontIfNeeded(_w);
	    if (SdlTkX.keyboard_window == NULL) {
		SdlTkSetInputFocus(SdlTkX.display,
				   (Window) SdlTkWrapperForWindow(_w),
				   RevertToParent, CurrentTime);
		/* Frames need redrawing if the focus changed */
		SdlTkScreenChanged();
	    }
	}

	/*
	 * Possible event in decorative frame (button, drag, resize).
	 * If a menu is showing, give Tk the click so the
	 * menu will go away. I would still like to drag.
	 */
	if (SdlTkDecFrameEvent(_w, sdl_event, x, y)) {
	    /* Hide the event from Tk. */
#ifdef ANDROID
	    TranslateStop();
#endif
	    return 0;
	}
	if (othergrab) {
	    return 0;
	}

	/* NULL for root and decorative frames */
	tkwin = (Tk_Window) _w->tkwin;
	SdlTkX.cursor_change = 1;
	if ((tkwin == NULL) && (_w->dec != NULL)) {
	    SdlTkX.cursor_change = 0;
	    _w = _w->child;
	    tkwin = (Tk_Window) _w->tkwin;
	}

	if (SdlTkX.capture_window != NULL) {
	    if (_w->display != SdlTkX.capture_window->display) {
		tkwin = (Tk_Window) SdlTkX.capture_window;
		_w = (_Window *) Tk_WindowId(tkwin);
	    }
	}
	if (SdlTkX.keyboard_window != NULL) {
	    if (_w->display != SdlTkX.keyboard_window->display) {
		tkwin = (Tk_Window) SdlTkX.keyboard_window->tkwin;
		_w = SdlTkX.keyboard_window;
	    }
	}

	if (state & SDL_BUTTON(1)) {
	    xstate |= Button1Mask;
	}
	if (state & SDL_BUTTON(2)) {
	    xstate |= Button2Mask;
	}
	if (state & SDL_BUTTON(3)) {
	    xstate |= Button3Mask;
	}

	mod = SDL_GetModState();
	if (mod & KMOD_LALT) {
	    xstate |= Mod1Mask;
	}
	if (mod & KMOD_RALT) {
	    xstate |= Mod4Mask;
	}
	if (mod & KMOD_CAPS) {
	    xstate |= LockMask;
	}
	if (mod & KMOD_CTRL) {
	    xstate |= ControlMask;
	}
	if (mod & KMOD_NUM) {
	    xstate |= Mod2Mask;
	}
	if (mod & KMOD_SHIFT) {
	    xstate |= ShiftMask;
	}

	if (sdl_event->type == SDL_MOUSEBUTTONUP) {
	    if (state & SDL_BUTTON(1)) {
		xstate &= ~Button1Mask;
	    }
	    if (state & SDL_BUTTON(2)) {
		xstate &= ~Button2Mask;
	    }
	    if (state & SDL_BUTTON(3)) {
		xstate &= ~Button3Mask;
	    }
	}
	if (sdl_event->type == SDL_MOUSEBUTTONDOWN) {
	    int bstate = xstate;

	    if (state & SDL_BUTTON(1)) {
		bstate &= ~Button1Mask;
	    }
	    if (state & SDL_BUTTON(2)) {
		bstate &= ~Button2Mask;
	    }
	    if (state & SDL_BUTTON(3)) {
		bstate &= ~Button3Mask;
	    }
	}
	SdlTkX.mouse_x = x;
	SdlTkX.mouse_y = y;
	if ((tkwin != NULL) && (Tk_WindowId(tkwin) != None)) {
	    SendPointerUpdate(tkwin, x, y, xstate);
	}
	return 0;
    }

    case SDL_MOUSEWHEEL: {
	int xstate = 0;
	Tk_Window tkwin = NULL;
#ifdef ANDROID
	int translate_zoom = (info->enabled & TRANSLATE_ZOOM) ? 1 : 0;
#endif
	SDL_Keymod mod;

	if (translate_zoom) {
	    mod = SDL_GetModState();
	    if (mod & KMOD_LCTRL) {
		float dir = 0, factor = 0.96;

		if (SdlTkX.arg_nogl && (SdlTkX.root_w == 0)) {
		    /* integral scaling */
		    factor = 0.5;
		}
		if (sdl_event->wheel.y > 0) {
		    dir = factor;
		} else if (sdl_event->wheel.y < 0) {
		    dir = 1.0 / factor;
		}
		if (dir) {
		    SdlTkZoomInt(SdlTkX.mouse_x, SdlTkX.mouse_y, dir);
		}
		return 0;
	    }
	}
	if (SdlTkX.mouse_window != NULL) {
	    tkwin = (Tk_Window) SdlTkX.mouse_window->tkwin;
	}
	if (SdlTkX.capture_window != NULL) {
	    if ((tkwin == NULL) ||
		(Tk_Display(tkwin) != SdlTkX.capture_window->display)) {
		tkwin = (Tk_Window) SdlTkX.capture_window;
	    }
	}
	if (SdlTkX.keyboard_window != NULL) {
	    if ((tkwin == NULL) ||
		(Tk_Display(tkwin) != SdlTkX.keyboard_window->display)) {
		tkwin = (Tk_Window) SdlTkX.keyboard_window->tkwin;
	    }
	}
	if (sdl_event->wheel.y < 0) {
	    xstate |= Button5Mask;
	} else if (sdl_event->wheel.y > 0) {
	    xstate |= Button4Mask;
	}
	if ((tkwin != NULL) && (Tk_WindowId(tkwin) != None) && xstate) {
	    SendPointerUpdate(tkwin, SdlTkX.mouse_x, SdlTkX.mouse_y,
			      xstate);
	    SendPointerUpdate(tkwin, SdlTkX.mouse_x, SdlTkX.mouse_y,
			      0);
	}
	return 0;
    }

    case SDL_QUIT:
	if (SdlTkDecFrameEvent((_Window *) SdlTkX.screen->root,
			       sdl_event, 0, 0)) {
	    return 0;
	}
	SdlTkUnlock(NULL);
	Tcl_Exit(0);
	break;

    case SDL_APP_LOWMEMORY:
	evname = "LowMemory";
	goto doAppEvent;
    case SDL_APP_TERMINATING:
	evname = "Terminating";
	goto doAppEvent;
    case SDL_APP_WILLENTERBACKGROUND:
	evname = "WillEnterBackground";
	goto doAppEvent;
    case SDL_APP_DIDENTERBACKGROUND:
	SdlTkX.in_background = 1;
	evname = "DidEnterBackground";
	goto doAppEvent;
    case SDL_APP_WILLENTERFOREGROUND:
	evname = "WillEnterForeground";
	goto doAppEvent;
    case SDL_APP_DIDENTERFOREGROUND:
	evname = "DidEnterForeground";
#ifdef ANDROID
	ConfigGLWindows(SdlTkX.screen->root);
#endif
    doAppEvent: {
	int nsent = 0;

#ifdef ANDROID
	TranslateStop();
#endif
	EVLOG("EV=APPEVENT     '%s'", evname);
	memset(event, 0, sizeof (*event));
	event->xany.type = VirtualEvent;
	event->xany.send_event = False;
	event->xany.window = (Window) SdlTkX.screen->root;
	event->xbutton.root = (Window) SdlTkX.screen->root;
	event->xany.display = SdlTkX.display;
	((XVirtualEvent *) event)->name = (Tk_Uid) evname;
	event->xany.serial = SdlTkX.display->request;
	/* only TK_APP_TOP_LEVEL windows will get this */
	event->xany.window = (Window)
	    SendAppEvent(event, &nsent,
			 ((_Window *) SdlTkX.screen->root)->child);
	return nsent > 0;
	}

    case SDL_USEREVENT: {
	int nsent = 0;

	if (sdl_event->user.data1 != NULL) {
	    evname = sdl_event->user.data1;
	}
	if (evname != NULL) {
	    EVLOG("EV=USEREVENT    '%s'", evname);
	    memset(event, 0, sizeof (*event));
	    event->xany.type = VirtualEvent;
	    event->xany.send_event = False;
	    event->xany.window = (Window) SdlTkX.screen->root;
	    event->xbutton.root = (Window) SdlTkX.screen->root;
	    event->xany.display = SdlTkX.display;
	    ((XVirtualEvent *) event)->name = (Tk_Uid) evname;
	    event->xany.serial = SdlTkX.display->request;
	    event->xbutton.x = event->xbutton.y = sdl_event->user.code;
	    event->xbutton.state = (long) sdl_event->user.data2;
	    /* only TK_APP_TOP_LEVEL windows will get this */
	    event->xany.window = (Window)
		SendAppEvent(event, &nsent,
			     ((_Window *) SdlTkX.screen->root)->child);
	}
	return nsent > 0;
    }

    case SDL_FINGERDOWN:
	evname = "FingerDown";
	goto doFingerEvent;
    case SDL_FINGERUP:
	evname = "FingerUp";
	goto doFingerEvent;
    case SDL_FINGERMOTION:
	evname = "FingerMotion";
    doFingerEvent: {
	_Window *_w;
	Tk_Window tkwin = NULL;
#if defined(_WIN32) || defined(_WIN64)
	static char fbits[20];
	static SDL_FingerID fids[20];
	int i, b = -1, fingerId = -1;

	/* This snippet cannot handle more than one touch screen! */
	for (i = 0; i < sizeof (fbits); i++) {
	    if (fbits[i]) {
		if (fids[i] == sdl_event->tfinger.fingerId) {
		    b = i;
		    break;
		}
	    } else if (fingerId < 0) {
		fingerId = i;
	    }
	}
	if (b >= 0) {
	    fingerId = b;
	    if (sdl_event->type == SDL_FINGERUP) {
		fbits[fingerId] = 0;
	    }
	} else if (fingerId >= 0) {
	    if ((sdl_event->type == SDL_FINGERDOWN) ||
		(sdl_event->type == SDL_FINGERMOTION)) {
		fbits[fingerId] = 1;
		fids[fingerId] = sdl_event->tfinger.fingerId;
	    } else {
		return 0;	/* ignore event */
	    }
	} else {
	    if ((sdl_event->type == SDL_FINGERDOWN) ||
		(sdl_event->type == SDL_FINGERMOTION)) {
		fingerId = 0;	/* reuse first */
		fbits[fingerId] = 1;
		fids[fingerId] = sdl_event->tfinger.fingerId;
	    } else {
		fbits[0] = 0;	/* make room and ignore event */
		return 0;	/* ignore event */
	    }
	}
#endif

	FingerToScreen(sdl_event, &x, &y);

	_w = SdlTkPointToWindow((_Window *) SdlTkX.screen->root,
				x, y, True, True);
	if (_w != NULL) {
	    tkwin = (Tk_Window) _w->tkwin;
	}
	if (tkwin == NULL) {
	    tkwin = (Tk_Window) SdlTkX.capture_window;
	    if (tkwin != NULL) {
		_w = (_Window *) Tk_WindowId(tkwin);
	    }
	}
	if (SdlTkX.capture_window != NULL) {
	    if ((_w == NULL) ||
		(_w->display != SdlTkX.capture_window->display)) {
		tkwin = (Tk_Window) SdlTkX.capture_window;
		_w = (_Window *) Tk_WindowId(tkwin);
	    }
	}
	if (SdlTkX.keyboard_window != NULL) {
	    if ((_w == NULL) ||
		(_w->display != SdlTkX.keyboard_window->display)) {
		tkwin = (Tk_Window) SdlTkX.keyboard_window->tkwin;
		_w = SdlTkX.keyboard_window;
	    }
	}
	if (tkwin != NULL) {
	    int pressure;

#ifdef ANDROID
	    if ((info->enabled & TRANSLATE_FBTNS) &&
		((sdl_event->type == SDL_FINGERDOWN) ||
		 (sdl_event->type == SDL_FINGERUP))) {
		int wx, wy;

		/* make ButtonPress/ButtonRelease for buttons 10-19 */
		SdlTkRootCoords(_w, &wx, &wy);
		memset(event, 0, sizeof (*event));
		event->xbutton.type = (sdl_event->type == SDL_FINGERUP) ?
		    ButtonRelease : ButtonPress;
		event->xbutton.serial = Tk_Display(tkwin)->request;
		event->xbutton.send_event = False;
		event->xbutton.display = Tk_Display(tkwin);
		event->xbutton.window = Tk_WindowId(tkwin);
		event->xbutton.root = (Window) SdlTkX.screen->root;
		event->xbutton.time = now_ms;
		event->xbutton.x = x - wx;
		event->xbutton.y = y - wy;
		event->xbutton.x_root = x;
		event->xbutton.y_root = y;
		event->xbutton.state = 0;
		event->xbutton.button = sdl_event->tfinger.fingerId + 10;
		event->xbutton.same_screen = True;
		SdlTkQueueEvent(event);
	    }

	    if (info->enabled & TRANSLATE_FINGER) {
		TranslateFinger(sdl_event, &tmp_event);
		sdl_event = &tmp_event;
	    }
#endif
	    memset(event, 0, sizeof (*event));
	    event->xany.type = VirtualEvent;
	    event->xany.serial = Tk_Display(tkwin)->request;
	    event->xany.send_event = False;
	    event->xany.window = Tk_WindowId(tkwin);
	    event->xbutton.root = (Window) SdlTkX.screen->root;
	    event->xany.display = Tk_Display(tkwin);
	    event->xbutton.x = sdl_event->tfinger.x * 10000;
	    if (event->xbutton.x >= 10000) {
		event->xbutton.x = 9999;
	    } else if (event->xbutton.x < 0) {
		event->xbutton.x = 0;
	    }
	    event->xbutton.y = sdl_event->tfinger.y * 10000;
	    if (event->xbutton.y >= 10000) {
		event->xbutton.y = 9999;
	    } else if (event->xbutton.y < 0) {
		event->xbutton.y = 0;
	    }
	    event->xbutton.x_root = sdl_event->tfinger.dx * 10000;
	    if (event->xbutton.x_root >= 10000) {
		event->xbutton.x_root = 9999;
	    } else if (event->xbutton.x_root < 0) {
		event->xbutton.x_root = 0;
	    }
	    event->xbutton.y_root = sdl_event->tfinger.dy * 10000;
	    if (event->xbutton.y_root >= 10000) {
		event->xbutton.y_root = 9999;
	    } else if (event->xbutton.y_root < 0) {
		event->xbutton.y_root = 0;
	    }
	    pressure = sdl_event->tfinger.pressure * 10000;
	    if (pressure >= 10000) {
		pressure = 9999;
	    } else if (pressure < 0) {
		pressure = 0;
	    }
	    event->xbutton.time = pressure;
#if defined(_WIN32) || defined(_WIN64)
	    event->xbutton.state = fingerId + 1;
#else
	    event->xbutton.state = sdl_event->tfinger.fingerId + 1;
#endif
	    ((XVirtualEvent *) event)->name = (Tk_Uid) evname;
	    return 1;
	}
	return 0;
    }

    case SDL_CLIPBOARDUPDATE:
	SdlTkSetSelectionOwner(SdlTkX.display, None, None, now_ms);
	return 0;

    case SDL_JOYDEVICEADDED:
    case SDL_JOYDEVICEREMOVED: {
	int nsent = 0;

	memset(event, 0, sizeof (*event));
	event->xany.type = VirtualEvent;
	event->xany.send_event = False;
	event->xany.window = (Window) SdlTkX.screen->root;
	event->xbutton.root = (Window) SdlTkX.screen->root;
	event->xany.display = SdlTkX.display;
	event->xany.serial = SdlTkX.display->request;
	event->xbutton.state = sdl_event->jdevice.which;
	event->xbutton.x_root = event->xbutton.y_root =
	    sdl_event->jdevice.which;
	if (sdl_event->type == SDL_JOYDEVICEADDED) {
	    SDL_Joystick *stick;
	    long which;
	    int new;
	    Tcl_HashEntry *hPtr;

	    stick = SDL_JoystickOpen(sdl_event->jdevice.which);
	    if (stick == NULL) {
		return 0;
	    }
	    event->xbutton.state = SDL_JoystickInstanceID(stick);
	    event->xbutton.x_root = event->xbutton.y_root =
		event->xbutton.state;
#ifdef ANDROID
	    if (strcmp(SDL_JoystickName(stick),
		       "Android Accelerometer") == 0) {
		SdlTkX.accel_id = event->xbutton.state;
	    }
#endif
	    which = event->xbutton.state;
	    hPtr = Tcl_CreateHashEntry(&SdlTkX.joystick_table,
				       (ClientData) which, &new);
	    if (!new) {
		SDL_JoystickClose(stick);
		return 0;
	    }
	    Tcl_SetHashValue(hPtr, (char *) stick);
	    ((XVirtualEvent *) event)->name = (Tk_Uid) "JoystickAdded";
	} else {
	    long which = sdl_event->jdevice.which;
	    Tcl_HashEntry *hPtr;

	    hPtr = Tcl_FindHashEntry(&SdlTkX.joystick_table,
				     (ClientData) which);
	    if (hPtr == NULL) {
		return 0;
	    }
	    SDL_JoystickClose((SDL_Joystick *) Tcl_GetHashValue(hPtr));
	    Tcl_DeleteHashEntry(hPtr);
	    ((XVirtualEvent *) event)->name = (Tk_Uid) "JoystickRemoved";
	}
	/* only TK_APP_TOP_LEVEL windows will get this */
	event->xany.window = (Window)
	    SendAppEvent(event, &nsent,
			 ((_Window *) SdlTkX.screen->root)->child);
	return nsent > 0;
    }

    case SDL_JOYAXISMOTION: {
	int nsent = 0;
#ifdef ANDROID
	int delta;

	if (sdl_event->jaxis.which == SdlTkX.accel_id) {
	    delta = AddToAccelRing(now_ms, sdl_event->jaxis.value,
				   sdl_event->jaxis.axis);
	    if (!SdlTkX.accel_enabled) {
		return 0;
	    }
	}
#endif
	memset(event, 0, sizeof (*event));
	event->xany.type = VirtualEvent;
	event->xany.send_event = False;
	event->xany.window = (Window) SdlTkX.screen->root;
	event->xbutton.root = (Window) SdlTkX.screen->root;
	event->xany.display = SdlTkX.display;
	event->xany.serial = SdlTkX.display->request;
	event->xbutton.time = now_ms;
	event->xbutton.x = event->xbutton.y = sdl_event->jaxis.value;
	event->xbutton.state = sdl_event->jaxis.axis + 1;
#ifdef ANDROID
	if (sdl_event->jaxis.which == SdlTkX.accel_id) {
	    ((XVirtualEvent *) event)->name = (Tk_Uid) "Accelerometer";
	    event->xbutton.x_root = event->xbutton.y_root = delta;
	} else
#endif
	{
	    event->xbutton.x_root = event->xbutton.y_root =
		sdl_event->jaxis.which;
	    ((XVirtualEvent *) event)->name = (Tk_Uid) "JoystickMotion";
	}
	/* only TK_APP_TOP_LEVEL windows will get this */
	event->xany.window = (Window)
	    SendAppEvent(event, &nsent,
			 ((_Window *) SdlTkX.screen->root)->child);
	return nsent > 0;
    }

    case SDL_JOYBALLMOTION: {
	int nsent = 0;

	memset(event, 0, sizeof (*event));
	event->xany.type = VirtualEvent;
	event->xany.send_event = False;
	event->xany.window = (Window) SdlTkX.screen->root;
	event->xany.display = SdlTkX.display;
	event->xany.serial = SdlTkX.display->request;
	event->xbutton.x = sdl_event->jball.xrel;
	event->xbutton.y = sdl_event->jball.yrel;
	event->xbutton.state = sdl_event->jball.ball + 1;
	event->xbutton.x_root = event->xbutton.y_root =
	    sdl_event->jball.which;
	((XVirtualEvent *) event)->name = (Tk_Uid) "TrackballMotion";
	/* only TK_APP_TOP_LEVEL windows will get this */
	event->xany.window = (Window)
	    SendAppEvent(event, &nsent,
			 ((_Window *) SdlTkX.screen->root)->child);
	return nsent > 0;
    }

    case SDL_JOYHATMOTION: {
	int nsent = 0;

	memset(event, 0, sizeof (*event));
	event->xany.type = VirtualEvent;
	event->xany.send_event = False;
	event->xany.window = (Window) SdlTkX.screen->root;
	event->xany.display = SdlTkX.display;
	event->xany.serial = SdlTkX.display->request;
	event->xbutton.x = event->xbutton.y = sdl_event->jhat.value;
	event->xbutton.state = sdl_event->jhat.hat + 1;
	event->xbutton.x_root = event->xbutton.y_root =
	    sdl_event->jhat.which;
	((XVirtualEvent *) event)->name = (Tk_Uid) "HatPosition";
	/* only TK_APP_TOP_LEVEL windows will get this */
	event->xany.window = (Window)
	    SendAppEvent(event, &nsent,
			 ((_Window *) SdlTkX.screen->root)->child);
	return nsent > 0;
    }

    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
	int nsent = 0;

	memset(event, 0, sizeof (*event));
	event->xany.type = VirtualEvent;
	event->xany.send_event = False;
	event->xany.window = (Window) SdlTkX.screen->root;
	event->xany.display = SdlTkX.display;
	event->xany.serial = SdlTkX.display->request;
	event->xbutton.state = sdl_event->jbutton.button + 1;
	event->xbutton.x_root = event->xbutton.y_root =
	    sdl_event->jbutton.which;
	((XVirtualEvent *) event)->name =  (Tk_Uid)
	    ((sdl_event->type == SDL_JOYBUTTONUP) ?
	     "JoystickButtonUp" : "JoystickButtonDown");
	/* only TK_APP_TOP_LEVEL windows will get this */
	event->xany.window = (Window)
	    SendAppEvent(event, &nsent,
			 ((_Window *) SdlTkX.screen->root)->child);
	return nsent > 0;
    }

    case SDL_WINDOWEVENT: {
	int width, height, oldw, oldh, ow, oh;
	SDL_PixelFormat *pfmt;
	SDL_Surface *newsurf = NULL;
	SDL_Texture *newtex = NULL;
	_Window *_w;
#ifndef ANDROID
	int tfmt = SDL_PIXELFORMAT_RGB888;
#endif

	switch (sdl_event->window.event) {

	case SDL_WINDOWEVENT_SIZE_CHANGED:
	    EVLOG("EV=WINDOWEVENT_SIZE_CHANGED");
	    width = sdl_event->window.data1;
	    height = sdl_event->window.data2;
	    if (SdlTkX.root_w) {
		SDL_GetWindowSize(SdlTkX.sdlscreen, &oldw, &oldh);
	    } else {
		oldw = SdlTkX.screen->width;
		oldh = SdlTkX.screen->height;
	    }
	    if ((width == oldw) && (height == oldh)) {
		return 0;
	    }

	    EVLOG("     width=%d height=%d", width, height);

	    if (SdlTkX.root_w) {
		float aspReal, aspRoot;

		aspReal = (float) width / height;
		aspRoot = (float) SdlTkX.root_w / SdlTkX.root_h;
		SdlTkX.scale_min = 1.0f;
		if (SDL_fabs(aspRoot - aspReal) < 0.0001) {
		    if (SdlTkX.root_w > width) {
			SdlTkX.scale_min = (float) width / SdlTkX.root_w;
		    }
		} else if (aspRoot > aspReal) {
		    if (SdlTkX.root_w > width) {
			SdlTkX.scale_min = (float) width / SdlTkX.root_w;
		    }
		} else {
		    if (SdlTkX.root_h > height) {
			SdlTkX.scale_min = (float) height / SdlTkX.root_h;
		    }
		}
		SdlTkPanZoom(1, SdlTkX.viewport.x, SdlTkX.viewport.y,
			     SdlTkX.viewport.w, SdlTkX.viewport.h);
		return 0;
	    }
	    pfmt = SdlTkX.sdlsurf->format;
	    newsurf = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height,
					   pfmt->BitsPerPixel, pfmt->Rmask,
					   pfmt->Gmask, pfmt->Bmask,
					   pfmt->Amask);
#ifndef ANDROID
	    if (pfmt->BitsPerPixel == 15) {
		tfmt = SDL_PIXELFORMAT_RGB555;
	    } else if (pfmt->BitsPerPixel == 16) {
		tfmt = SDL_PIXELFORMAT_RGB565;
	    } else if (pfmt->BitsPerPixel == 24) {
		if (pfmt->BytesPerPixel == 3) {
		    tfmt = SDL_PIXELFORMAT_RGB24;
		}
	    }
#endif
	    newtex =
		SDL_CreateTexture(SdlTkX.sdlrend,
#ifdef ANDROID
				  SDL_PIXELFORMAT_RGB888,
#else
				  tfmt,
#endif
				  SDL_TEXTUREACCESS_STREAMING,
				  width, height);
#ifdef ANDROID
	    if ((newsurf != NULL) && (newtex != NULL)) {
		SDL_GL_SwapWindow(SdlTkX.sdlscreen);
	    }
#endif
	    if ((newsurf != NULL) && (newtex != NULL)) {
		int xdpi, ydpi;
		SDL_Rect sr;
		Uint32 pixel;
		_Window *child;
		Display *dpy;

		SDL_BlitSurface(SdlTkX.sdlsurf, NULL, newsurf, NULL);
		SDL_FreeSurface(SdlTkX.sdlsurf);
		SdlTkX.sdlsurf = newsurf;
		SDL_DestroyTexture(SdlTkX.sdltex);
		SdlTkX.sdltex = newtex;
		SdlTkX.screen->width = width;
		SdlTkX.screen->height = height;
		xdpi = SdlTkX.arg_xdpi;
		ydpi = SdlTkX.arg_ydpi;
		if (xdpi == 0) {
		    xdpi = ydpi;
		}
		if (ydpi == 0) {
		    ydpi = xdpi;
		}
#if defined(ANDROID) && defined(SDL_HAS_GETWINDOWDPI)
		if (xdpi == 0) {
		    SDL_GetWindowDPI(SdlTkX.sdlscreen, &xdpi, &ydpi);
		}
#endif
		if (xdpi && ydpi) {
		    SdlTkX.screen->mwidth = (254 * width) / xdpi;
		    SdlTkX.screen->mwidth /= 10;
		    SdlTkX.screen->mheight = (254 * height) / ydpi;
		    SdlTkX.screen->mheight /= 10;
		} else {
#ifdef ANDROID
		    SdlTkX.screen->mwidth = (width * 254 + 360) / 1440;
		    SdlTkX.screen->mheight = (height * 254 + 360) / 1440;
#else
		    SdlTkX.screen->mwidth = (width * 254 + 360) / 720;
		    SdlTkX.screen->mheight = (height * 254 + 360) / 720;
#endif
		}
		dpy = SdlTkX.display->next_display;
		while (dpy != NULL) {
		    SdlTkGenerateConfigureNotify(dpy, dpy->screens[0].root);
		    dpy = dpy->next_display;
		}
		_w = (_Window *) SdlTkX.screen->root;
		_w->atts.width = _w->parentWidth = width;
		_w->atts.height = _w->parentHeight = height;
		pixel = SDL_MapRGB(SdlTkX.sdlsurf->format,
#ifdef ANDROID
				   0x00, 0x00, 0x00
#else
				   0x00, 0x4E, 0x78
#endif
				   );
		if (width > oldw) {
		    sr.x = oldw;
		    sr.w = width - oldw;
		    sr.y = 0;
		    sr.h = height;
		    SDL_FillRect(SdlTkX.sdlsurf, &sr, pixel);
		}
		if (height > oldh) {
		    sr.y = oldh;
		    sr.h = height - oldh;
		    sr.x = 0;
		    sr.w = width;
		    SDL_FillRect(SdlTkX.sdlsurf, &sr, pixel);
		}

		if ((width > oldw) || (height > oldh)) {
		    SdlTkVisRgnChanged(_w, VRC_CHANGED, 0, 0);
		}

		child = _w->child;
		while (child != NULL) {
		    if (child->fullscreen) {
			int xx, yy, ww, hh;
			_Window *_ww = child;

			xx = 0;
			yy = 0;
			ww = width;
			hh = height;
			if (child->dec != NULL) {
			    xx -= SdlTkX.dec_frame_width;
			    yy -= SdlTkX.dec_title_height;
			    _ww = child->child;
			}
			child->fullscreen = 0;
			_ww->fullscreen = 0;
			SdlTkMoveResizeWindow(SdlTkX.display, (Window) _ww,
					      xx, yy, ww, hh);
			_ww->fullscreen = 1;
			child->fullscreen = 1;
		    }
		    child = child->next;
		}
		if (SdlTkX.draw_later & SDLTKX_SCALED) {
		    int vw, vh, vx, vy;

		    vw = (int) (width / SdlTkX.scale);
		    vh = (int) (height / SdlTkX.scale);
		    vx = SdlTkX.viewport.x + (SdlTkX.viewport.w - vw) / 2;
		    vy = SdlTkX.viewport.y + (SdlTkX.viewport.h - vh) / 2;
		    if (width - (vw + vx) < 0) {
			vx = width - vw;
		    }
		    if (vx < 0) {
			vx = 0;
		    }
		    if (height - (vh + vy) < 0) {
			vy = height - vh;
		    }
		    if (vy < 0) {
			vy = 0;
		    }
		    if (vw > width) {
			vw = width;
		    }
		    if (vh > height) {
			vh = height;
		    }
		    SdlTkX.viewport.x = vx;
		    SdlTkX.viewport.y = vy;
		    SdlTkX.viewport.w = vw;
		    SdlTkX.viewport.h = vh;

		    ow = (int) SDL_ceil(vw * SdlTkX.scale);
		    oh = (int) SDL_ceil(vh * SdlTkX.scale);
		    if ((ow < width) || (oh < height)) {
			SdlTkX.outrect = &SdlTkX.outrect0;
			SdlTkX.outrect->x = (width - ow) / 2;
			SdlTkX.outrect->y = (height - oh) / 2;
			SdlTkX.outrect->w = ow;
			SdlTkX.outrect->h = oh;
		    } else {
			SdlTkX.outrect = NULL;
		    }
		} else {
		    SdlTkX.viewport.w = width;
		    SdlTkX.viewport.h = height;
		    SdlTkX.outrect = NULL;
		}
		SdlTkSendViewportUpdate();
		SDL_SetRenderTarget(SdlTkX.sdlrend, NULL);
		SDL_RenderSetViewport(SdlTkX.sdlrend, NULL);
	    } else {
		if (newsurf != NULL) {
		    SDL_FreeSurface(newsurf);
		}
		if (newtex != NULL) {
		    SDL_DestroyTexture(newtex);
		}
	    }
	    SdlTkX.draw_later |= SDLTKX_RENDCLR | SDLTKX_PRESENT;
	    goto fullRefresh;

	case SDL_WINDOWEVENT_FOCUS_GAINED:
	    EVLOG("EV=WINDOWEVENT_FOCUS_GAINED");
	    if (!SdlTkX.sdlfocus) {
		SdlTkX.sdlfocus = 1;
		if (SdlTkX.focus_window_old != None) {
		    SdlTkSetInputFocus(SdlTkX.display,
				       SdlTkX.focus_window_old,
				       RevertToParent, CurrentTime);
		    goto refresh;
		}
	    }
	    return 0;

	case SDL_WINDOWEVENT_FOCUS_LOST:
	    EVLOG("EV=WINDOWEVENT_FOCUS_LOST");
	    if (SdlTkX.sdlfocus) {
		SdlTkX.sdlfocus = 0;
		SdlTkX.focus_window_old = SdlTkX.focus_window;
		if (SdlTkX.focus_window != None) {
		    SdlTkSetInputFocus(SdlTkX.display, None,
				       RevertToNone, CurrentTime);
		    goto refresh;
		}
	    }
	    return 0;

	case SDL_WINDOWEVENT_HIDDEN:
	    EVLOG("EV=WINDOWEVENT_HIDDEN");
	    return 0;
	case SDL_WINDOWEVENT_MOVED:
	    EVLOG("EV=WINDOWEVENT_MOVED");
	    return 0;
	case SDL_WINDOWEVENT_RESIZED:
	    EVLOG("EV=WINDOWEVENT_RESIZED");
	    return 0;
	case SDL_WINDOWEVENT_MINIMIZED:
	    EVLOG("EV=WINDOWEVENT_MINIMIZED");
	    return 0;
	case SDL_WINDOWEVENT_MAXIMIZED:
	    EVLOG("EV=WINDOWEVENT_MAXIMIZED");
	    return 0;
	case SDL_WINDOWEVENT_ENTER:
	    EVLOG("EV=WINDOWEVENT_ENTER");
	    return 0;
	case SDL_WINDOWEVENT_LEAVE:
	    EVLOG("EV=WINDOWEVENT_LEAVE");
	    return 0;
	case SDL_WINDOWEVENT_CLOSE:
	    EVLOG("EV=WINDOWEVENT_CLOSE");
	    return 0;

	case SDL_WINDOWEVENT_RESTORED:
	    EVLOG("EV=WINDOWEVENT_RESTORED");
	    goto fullRefresh;
	case SDL_WINDOWEVENT_SHOWN:
	    EVLOG("EV=WINDOWEVENT_SHOWN");
	    goto fullRefresh;
	case SDL_WINDOWEVENT_EXPOSED:
	    EVLOG("EV=WINDOWEVENT_EXPOSED");
	fullRefresh:
	    SdlTkX.in_background = 0;
	    SdlTkX.draw_later |= SDLTKX_DRAW | SDLTKX_DRAWALL;
#ifdef ANDROID
	    SdlTkScreenRefresh();
#endif
	    return 0;

	refresh:
	    SdlTkScreenChanged();
	    return 0;
	}
	return 0;

	default:
	    return 0;
    }
    }

    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkCalculateVisibleRegion --
 *
 *	This procedure calculates the visible region for a window.
 *
 *	NOTE: The caller is responsible for freeing the old visRgn
 *	and visRgnInParent.
 *
 *	NOTE: The parent's visRgnInParent must be correct before
 *	this procedure may be called.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The visRgn and visRgnInParent members of the
 *	given window are changed.
 *
 *----------------------------------------------------------------------
 */

void
SdlTkCalculateVisibleRegion(_Window *_w)
{
    XRectangle rect;
    _Window *ancestor, *child, *parent = _w->parent;

    /* Caller must free old ones */
    _w->visRgn = SdlTkRgnPoolGet();
    _w->visRgnInParent = SdlTkRgnPoolGet();

    /* An unmapped window has an empty visible region */
    if (_w->atts.map_state == IsUnmapped) {
	return;
    }

    /*
     * If any ancestor is unmapped, this window has an empty
     * visible region. In X11, a window may be mapped even if an
     * ancestor isn't.
     */
    if (parent != NULL) {
	ancestor = parent;
	while (!IS_ROOT(ancestor)) {
	    if (ancestor->atts.map_state == IsUnmapped) {
		return;
	    }
	    ancestor = ancestor->parent;
	}
    }

    /* Start with a rectangle as large as ourself in our parent */
    rect.x = _w->atts.x;
    rect.y = _w->atts.y;
    rect.width = _w->parentWidth;
    rect.height = _w->parentHeight;
    XUnionRectWithRegion(&rect, _w->visRgnInParent, _w->visRgnInParent);

    if (parent != NULL) {
	/* Intersect with parent's visRgnInParent */
	XIntersectRegion(_w->visRgnInParent, parent->visRgnInParent,
			 _w->visRgnInParent);

	if (XEmptyRegion(_w->visRgnInParent)) {
	    /* Entirely outside of parent's visRgn, we're done. */
	    return;
	}

	/*
	 * Subtract one rectangle for each mapped sibling higher in the
	 * stacking order.
	 */
	if (parent->child != _w) {
	    Region rgn2 = SdlTkRgnPoolGet();

	    child = parent->child;
	    while (child != _w) {
		if ((child->atts.map_state != IsUnmapped) &&
		    (XRectInRegion(_w->visRgnInParent, child->atts.x,
				   child->atts.y, child->parentWidth,
				   child->parentHeight) != RectangleOut)) {
		    rect.x = child->atts.x;
		    rect.y = child->atts.y;
		    rect.width = child->parentWidth;
		    rect.height = child->parentHeight;
		    XUnionRectWithRegion(&rect, rgn2, rgn2);
		}
		child = child->next;
	    }
	    XSubtractRegion(_w->visRgnInParent, rgn2, _w->visRgnInParent);
	    SdlTkRgnPoolFree(rgn2);

	    /* A window's siblings may completely obscure it */
	    if (XEmptyRegion(_w->visRgnInParent)) {
	        if (!PARENT_IS_ROOT(_w)) {
		    return;
		}
	    }
	}
    }

    /*
     * Calculation of visRgnInParent is complete. Copy visRgnInParent
     * into visRgn.
     */
    XUnionRegion(_w->visRgnInParent, _w->visRgn, _w->visRgn);

    /*
     * Subtract one rectangle for each mapped child from visRgn (a window
     * can't draw over its children)
     */
    if (_w->child != NULL) {
	Region rgn2 = SdlTkRgnPoolGet();

	for (child = _w->child; child != NULL; child = child->next) {
	    if (child->atts.map_state != IsUnmapped) {
		rect.x = _w->atts.x + child->atts.x;
		rect.y = _w->atts.y + child->atts.y;
		rect.width = child->parentWidth;
		rect.height = child->parentHeight;
		XUnionRectWithRegion(&rect, rgn2, rgn2);
	    }
	}
	XSubtractRegion(_w->visRgn, rgn2, _w->visRgn);
	SdlTkRgnPoolFree(rgn2);
    }

    /* Make relative to this window's top-left corner */
    XOffsetRegion(_w->visRgn, -_w->atts.x, -_w->atts.y);
    XOffsetRegion(_w->visRgnInParent, -_w->atts.x, -_w->atts.y);
}

/*
 *----------------------------------------------------------------------
 *
 * BlitMovedWindow --
 *
 *	Helper routine for SdlTkVisRgnChanged(). Copies the visible
 *	pixels of a window from its old location to its new location.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Pixels will be blitted within the drawing surface of the
 *	window's parent.
 *
 *----------------------------------------------------------------------
 */

static void
BlitMovedWindow(_Window *_w, int x, int y)
{
    XGCValues fakeGC;
    TkpClipMask clip;
    int xOff, yOff, width, height;
    Region parentVisRgn;

    /*
     * Hack: SdlTkGfxCopyArea will clip to our parent's visRgn. Need to
     * clip to our parent's visRgnInParent instead.
     */
    parentVisRgn = _w->parent->visRgn;
    _w->parent->visRgn = _w->parent->visRgnInParent;

    /*
     * What actually needs to be done is copy pixels inside the
     * old visRgnInParent to the new location. The copy operation
     * must be constrained by the parent window, since the
     * child may be larger.
     */
    width = _w->parentWidth;
    height = _w->parentHeight;

    /*
     * This window's visRgnInParent is used as the clip region. Put
     * it in the parent's coordinates.
     */
    XOffsetRegion(_w->visRgnInParent, _w->atts.x, _w->atts.y);

    clip.type = TKP_CLIP_REGION;
    clip.value.region = (TkRegion) _w->visRgnInParent;
    fakeGC.clip_mask = (Pixmap) &clip;
    fakeGC.graphics_exposures = False;
    fakeGC.clip_x_origin = 0;
    fakeGC.clip_y_origin = 0;

    SdlTkGfxCopyArea((Drawable) _w->parent, (Drawable) _w->parent, &fakeGC,
	x, y, width, height, _w->atts.x, _w->atts.y);

    XOffsetRegion(_w->visRgnInParent, -_w->atts.x, -_w->atts.y);

    /* Undo hack */
    _w->parent->visRgn = parentVisRgn;

    /*
     * Add its visible area to the screen's dirtyRgn.
     */
    SdlTkRootCoords(_w, &xOff, &yOff);
    XOffsetRegion(_w->visRgnInParent, xOff, yOff);
    if (SdlTkX.screen_update_region == NULL) {
	SdlTkX.screen_update_region = SdlTkRgnPoolGet();
    }
    XUnionRegion(_w->visRgnInParent, SdlTkX.screen_update_region,
		 SdlTkX.screen_update_region);
    XOffsetRegion(_w->visRgnInParent, -xOff, -yOff);
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkVisRgnChanged --
 *
 *	This procedure gets called whenever a window is mapped,
 *	unmapped, raised, lowered, moved, or resized. It recalculates
 *	the visible regions for all windows affected by the change.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The visRgn, visRgnInParent and dirtyRgn
 *	members of various windows may be updated. If the window
 *	was moved, then pixels may be copied within its parent's
 *	drawing surface. <Expose> events may be added to the event
 *	queue.
 *
 *----------------------------------------------------------------------
 */

static void
SdlTkVisRgnChangedInt(_Window *_w, int flags, int x, int y)
{
    Region visRgn = NULL;
    Region visRgnInParent = NULL;
    int clrRgn = 0;

    /*
     * If this is NOT the window that was mapped/unmapped/moved/
     * resized/restacked and it is unmapped, don't bother recalculating
     * visible regions for this window.
     */
    if ((flags & VRC_CHANGED) || (_w->atts.map_state != IsUnmapped)) {

	/*
	 * A window obscures part of its parent.
	 * Update parent's visible region before blitting this window.
	 */
        if ((flags & VRC_DO_PARENT) && (_w->parent != NULL)) {
	    SdlTkVisRgnChangedInt(_w->parent, VRC_SELF_ONLY, 0, 0);
	}

	/* Save the old regions temporarily so changes can be examined */
	visRgn = _w->visRgn;
	visRgnInParent = _w->visRgnInParent;

	SdlTkCalculateVisibleRegion(_w);

	/*
  	 * If this window moved within its parent, copy all the visible
  	 * pixels to the new location.
  	 */
	if ((flags & VRC_MOVE) && !XEmptyRegion(_w->visRgnInParent)) {
	    Region newRgnInParent, blitRgn = SdlTkRgnPoolGet();

	    /* For the pixel copy, intersect old and new regions of parent */
	    newRgnInParent = _w->visRgnInParent;
	    XIntersectRegion(visRgnInParent, newRgnInParent, blitRgn);
	    _w->visRgnInParent = blitRgn;
	    BlitMovedWindow(_w, x, y);
	    _w->visRgnInParent = newRgnInParent;
	    SdlTkRgnPoolFree(blitRgn);
	}

	if (_w->atts.map_state != IsUnmapped) {
	    /* Take away what was visible before */
	    XSubtractRegion(_w->visRgn, visRgn, visRgn);

	    /*
	     * Generate <Expose> events for newly-exposed areas.
	     * Only generate events for real Tk windows, not a
	     * decframe or wrapper
	     */
	    if (_w->tkwin != NULL) {
		if (!XEmptyRegion(visRgn) || (_w->gl_tex != NULL)) {
		    flags |= VRC_EXPOSE;
		    clrRgn = 1;
		}
	    } else if (_w->dec != NULL) {
		if (!XEmptyRegion(visRgn)) {
		    SdlTkDecSetDraw(_w, 1);
		    clrRgn = 1;
		}
		flags |= VRC_EXPOSE;
	    } else if (IS_ROOT(_w)) {
		/*
		 * Can't erase right away, because it will erase the pixels of
		 * any toplevels which moved (I want to blit those pixels).
		 */
		if (SdlTkX.screen_dirty_region == NULL) {
		    SdlTkX.screen_dirty_region = SdlTkRgnPoolGet();
		}
		XUnionRegion(SdlTkX.screen_dirty_region, visRgn,
			     SdlTkX.screen_dirty_region);
	    }
	}
    }

    if ((_w->atts.map_state != IsUnmapped) && (flags & VRC_EXPOSE)) {
	if ((flags & (VRC_MOVE | VRC_CHANGED)) && XEmptyRegion(visRgn)) {
	    SdlTkGfxExposeRegion((Window) _w, _w->visRgn);
	} else {
	    SdlTkGfxExposeRegion((Window) _w, visRgn);
	}
    }

    if (!(flags & VRC_SELF_ONLY)) {
	/*
	 * If this is NOT the window that was mapped/unmapped/moved/
	 * resized/restacked and it is unmapped, don't bother
	 * recalculating visible regions for any descendants.
	 */
	if ((flags & (VRC_CHANGED | VRC_EXPOSE)) ||
	    (_w->atts.map_state != IsUnmapped)) {
	    /*
	     * If this window's visRgnInParent did not change (such as when
	     * moving a toplevel), don't recalculate the visible
	     * regions of any descendants.
	     */
	    if ((flags & VRC_EXPOSE) ||
		!XEqualRegion(_w->visRgnInParent, visRgnInParent)) {
		/*
		 * We only need to do our first child, because it will do
		 * its next sibling etc.
		 */
	        if (_w->child != NULL) {
		    SdlTkVisRgnChangedInt(_w->child, flags & VRC_EXPOSE,
					  0, 0);
		}
	    }
	}

	if (clrRgn && (visRgn != NULL)) {
	    /* Clear what we <Expose>'d before */
	    SdlTkGfxClearRegion((Window) _w, visRgn);
	}

	/* A window may obscure siblings lower in the stacking order */
	if (!(flags & VRC_DO_SIBLINGS)) {
	    while (_w->next != NULL) {
		SdlTkVisRgnChangedInt(_w->next, VRC_DO_SIBLINGS |
				      (flags & VRC_EXPOSE), 0, 0);
		_w = _w->next;
	    }
	}
    }

    if (visRgn != NULL) {
	SdlTkRgnPoolFree(visRgn);
	visRgn = NULL;
    }

    if (visRgnInParent != NULL) {
	SdlTkRgnPoolFree(visRgnInParent);
	visRgnInParent = NULL;
    }
}

void
SdlTkVisRgnChanged(_Window *_w, int flags, int x, int y)
{
    SdlTkVisRgnChangedInt(_w, flags, x, y);

    /* If areas of the root window were exposed, paint them now */
    if (SdlTkX.screen_dirty_region &&
	!XEmptyRegion(SdlTkX.screen_dirty_region)) {
	Uint32 pixel = SDL_MapRGB(SdlTkX.sdlsurf->format,
#ifdef ANDROID
				  0x00, 0x00, 0x00
#else
				  0x00, 0x4E, 0x78
#endif
				  );

	SdlTkGfxFillRegion(SdlTkX.screen->root, SdlTkX.screen_dirty_region,
			   pixel);
	if (SdlTkX.screen_update_region == NULL) {
	    SdlTkX.screen_update_region = SdlTkRgnPoolGet();
	}
	XUnionRegion(SdlTkX.screen_dirty_region,
		     SdlTkX.screen_update_region,
		     SdlTkX.screen_update_region);
	XSetEmptyRegion(SdlTkX.screen_dirty_region);
    }
}

Region
SdlTkGetVisibleRegion(_Window *_w)
{
    if (_w->visRgn == NULL) {
	_w->visRgn = SdlTkRgnPoolGet();
    }
    return _w->visRgn;
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkGenerateConfigureNotify --
 *
 *	Generate a <Configure> event for a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A window event is added to the event queue.
 *
 *----------------------------------------------------------------------
 */

void
SdlTkGenerateConfigureNotify(Display *display, Window w)
{
    _Window *_w = (_Window *) w;
    _Window *parent = NULL;
    XEvent event;

    memset(&event, 0, sizeof (event));
    event.type = ConfigureNotify;
    event.xconfigure.serial = _w->display->request;
    event.xconfigure.send_event = False;
    event.xconfigure.display = (display == NULL) ? _w->display : display;
    event.xconfigure.event = w;
    event.xconfigure.window = w;
    event.xconfigure.above = None;
    if (w == _w->display->screens[0].root) {
	/* special case: send mwidth/mheight as x/y */
	event.xconfigure.border_width = 0;
	event.xconfigure.override_redirect = 0;
	event.xconfigure.x = SdlTkX.screen->mwidth;
	event.xconfigure.y = SdlTkX.screen->mheight;
	event.xconfigure.width = SdlTkX.screen->width;
	event.xconfigure.height = SdlTkX.screen->height;
    } else {
	event.xconfigure.border_width = _w->atts.border_width;
	event.xconfigure.override_redirect = _w->atts.override_redirect;
	event.xconfigure.x = _w->atts.x;
	event.xconfigure.y = _w->atts.y;
	event.xconfigure.width = _w->atts.width;
	event.xconfigure.height = _w->atts.height;
	if (!IS_ROOT(_w->parent)) {
	    parent = _w->parent;
	    if (!(parent->atts.your_event_mask & SubstructureNotifyMask)) {
		parent = NULL;
	    }
	}
    }
    SdlTkQueueEvent(&event);
    if (parent != NULL) {
	event.xconfigure.event = (Window) parent;
	event.xconfigure.serial = parent->display->request;
	event.xconfigure.display = parent->display;
	SdlTkQueueEvent(&event);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkGenerateExpose --
 *
 *	Generate an <Expose> event for part of a window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A window event is added to the event queue.
 *
 *----------------------------------------------------------------------
 */

void
SdlTkGenerateExpose(Window w, int x, int y,
		    int width, int height, int count)
{
    _Window *_w = (_Window *) w;
    XEvent event;

    memset(&event, 0, sizeof (event));
    event.type = Expose;
    event.xexpose.serial = _w->display->request;
    event.xexpose.send_event = False;
    event.xexpose.display = _w->display;
    event.xexpose.window = w;
    event.xexpose.x = x;
    event.xexpose.y = y;
    event.xexpose.width = width;
    event.xexpose.height = height;
    event.xexpose.count = count;
    SdlTkQueueEvent(&event);
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkRootCoords --
 *
 *	Determines the screen coordinates of the top-left corner
 *	of a window, whether it is mapped or not.
 *
 * Results:
 *	*x and *y contain the offsets from the top-left of the
 *	root window to the top-left corner of the given window.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

void
SdlTkRootCoords(_Window *_w, int *x, int *y)
{
    int xOff, yOff;

    xOff = _w->atts.x;
    yOff = _w->atts.y;
    while (_w->parent != NULL) {
	_w = _w->parent;
	xOff += _w->atts.x;
	yOff += _w->atts.y;
    }
    if (x) {
	*x = xOff;
    }
    if (y) {
	*y = yOff;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkToplevelForWindow --
 *
 *	Return the coordinates for a window in its toplevel.
 *
 * Results:
 *	Returns the toplevel window containing the given window.
 *	*x and *y (which may be NULL) contain the offsets from
 *	the top-left corner of the toplevel to the top-left
 *	corner of the given window.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

_Window *
SdlTkToplevelForWindow(_Window *_w, int *x, int *y)
{
    int xOff, yOff;

    if (_w == NULL) {
	return NULL;
    }

    if (IS_ROOT(_w)) {
	return NULL;
    }

    /* Won't usually ask for this (no drawing takes place in wrapper) */
    if (PARENT_IS_ROOT(_w)) {
        if (x) {
	    *x = 0;
	}
	if (y) {
	    *y = 0;
	}
	return _w;
    }

    xOff = _w->atts.x;
    yOff = _w->atts.y;
    while ((_w->parent != NULL) && !PARENT_IS_ROOT(_w->parent)) {
	_w = _w->parent;
	xOff += _w->atts.x;
	yOff += _w->atts.y;
    }
    if (x) {
	*x = xOff;
    }
    if (y) {
	*y = yOff;
    }
    return _w ? _w->parent : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkWrapperForWindow --
 *
 *	Get the Tk wrapper for a window.
 *
 * Results:
 *	Returns the Tk wrapper window that is an ancestor of the given
 *	window, or, if the given window is a decframe, returns its child
 *	(which must be a wrapper).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

_Window *
SdlTkWrapperForWindow(_Window *_w)
{
    if (IS_ROOT(_w)) {
	return NULL;
    }
    while (!PARENT_IS_ROOT(_w)) {
	_w = _w->parent;
    }
    if (_w->dec != NULL) {
	_w = _w->child;
    }
    return _w;
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkTopVisibleWrapper --
 *
 *	Determine the top mapped Tk wrapper.
 *
 * Results:
 *	Returns the mapped wrapper highest in the stack, or NULL if
 *	there are no mapped wrapper windows.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

_Window *
SdlTkTopVisibleWrapper(void)
{
    _Window *child = ((_Window *) SdlTkX.screen->root)->child;

    while (child != NULL) {
	if ((child->atts.map_state != IsUnmapped) &&
	    !child->atts.override_redirect) {
	    if (child->dec != NULL) {
		child = child->child; /* the wrapper */
	    }
	    break;
	}
	child = child->next;
    }
    return child;
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkGetDrawableSurface --
 *
 *	Determines which SDL_Surface should be used to draw into an
 *	X Drawable.
 *
 * Results:
 *	If the given Drawable is a pixmap, the result is the pixmap's
 *	own surface.
 *
 *	The result is the screen's surface.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

SDL_Surface *
SdlTkGetDrawableSurface(Drawable d, int *x, int *y, int *format)
{
    _Pixmap *_p = (_Pixmap *) d;
    _Window *_w = (_Window *) d;

    if (_p->type == DT_PIXMAP) {
        if (x) {
	    *x = 0;
	}
	if (y) {
	    *y = 0;
	}
	if (format != NULL) {
	    *format = _p->format;
	}
	return _p->sdl;
    }
    if (IS_ROOT(_w)) {
        if (x) {
	    *x = 0;
	}
	if (y) {
	    *y = 0;
	}
    } else {
	SdlTkRootCoords(_w, x, y);
    }
    if (format != NULL) {
	*format = ((_Window *) (SdlTkX.screen->root))->format;
    }
    return SdlTkX.sdlsurf;
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkPointToWindow --
 *
 *	Determines which window contains the given x,y location.
 *
 * Results:
 *	Returns the deepest descendant containing the given
 *	location; returns the given window if no descendant
 *	contains the location.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

_Window *
SdlTkPointToWindow(_Window *_w, int x, int y, Bool mapped, Bool depth)
{
    _Window *child;

    for (child = _w->child; child != NULL; child = child->next) {
	if ((x >= child->atts.x) &&
	    (x < child->atts.x + child->parentWidth) &&
	    (y >= child->atts.y) &&
	    (y < child->atts.y + child->parentHeight)) {
	    if (!mapped || (child->atts.map_state != IsUnmapped)) {
		x -= child->atts.x;
		y -= child->atts.y;
		if (!depth) {
		    return child;
		}
		return SdlTkPointToWindow(child, x, y, mapped, depth);
	    }
	}
    }
    return _w;
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkRemoveFromParent --
 *
 *	Remove a window from its parent's list of children.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window is removed from the parent's list of children.
 *
 *----------------------------------------------------------------------
 */

void
SdlTkRemoveFromParent(_Window *_w)
{
    _Window *child = _w->parent->child, *prev = NULL;

    /* Find previous */
    while (child != NULL) {
        if (child == _w) {
	    break;
	}
	prev = child;
	child = child->next;
    }
    if (child == NULL) {
	Tcl_Panic("SdlTkRemoveFromParent: can't find %p\n", _w);
    }

    if (prev == NULL) {
	_w->parent->child = _w->next;
    } else {
	prev->next = _w->next;
    }
    _w->parent = NULL;
    _w->next = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkAddToParent --
 *
 *	Add a window to another's list of children.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The window is inserted into the parent's list of children.
 *
 *----------------------------------------------------------------------
 */

void
SdlTkAddToParent(_Window *_w, _Window *parent, _Window *sibling)
{
    _Window *child = parent->child, *prev = NULL;

    _w->parent = parent;

    /* Make only child */
    if (child == NULL) {
	parent->child = _w;
	return;
    }

    /* Make last child */
    if (sibling == NULL) {
        while (child->next != NULL) {
	    child = child->next;
	}
	child->next = _w;
	return;
    }

    /* Make first child */
    if (child == sibling) {
	_w->next = sibling;
	parent->child = _w;
	return;
    }

    /* Find previous to sibling */
    while (child != NULL) {
        if (child == sibling) {
	    break;
	}
	prev = child;
	child = child->next;
    }
    if (child == NULL) {
	Tcl_Panic("SdlTkAddToParent: can't find sibling");
    }

    prev->next = _w;
    _w->next = sibling;
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkRestackWindow --
 *
 *	Put a window above or below a sibling. This is the main
 *	window-restacking function.
 *
 * Results:
 *
 * Side effects:
 *	Stacking order may change, visible regions may change, <Expose>
 *	events may be added to the event queue.
 *
 *----------------------------------------------------------------------
 */

void
SdlTkRestackWindow(_Window *_w, _Window *sibling, int stack_mode)
{
    _Window *parent = _w->parent;
    int oldPos = 0, newPos = 0;
    _Window *child, *oldNext = _w->next;;

    if ((parent->child == _w) && (_w->next == NULL)) {
	return;
    }

    for (child = parent->child; child != _w; child = child->next) {
	oldPos++;
    }

    SdlTkRemoveFromParent(_w);

    if (sibling == NULL) {
	switch (stack_mode) {
	case Above:
	    sibling = parent->child;
	    break;
	case Below:
	    sibling = parent->child;
	    while (sibling->next != NULL) {
		sibling = sibling->next;
	    }
	    if (sibling == NULL) {
		return;
	    }
	    break;
	}
    } else {
	switch (stack_mode) {
	case Below:
	    sibling = sibling->next;
	    break;
	}
    }
    SdlTkAddToParent(_w, parent, sibling);

    /*
     * When restacking a child window, the parent's visible region is
     * never affected.
     */
    for (child = parent->child; child != _w; child = child->next) {
	newPos++;
    }

    if (oldPos > newPos) {
	/* Raised */
	SdlTkVisRgnChanged(_w, VRC_CHANGED, 0, 0);
    } else if (oldPos < newPos) {
	/* Lowered */
	SdlTkVisRgnChanged(oldNext, VRC_CHANGED, 0, 0);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkRestackTransients --
 *
 *	Restack any transient toplevels of the given toplevel so they
 *	are kept in front of it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stacking order may change, visible regions may change, <Expose>
 *	events may be added to the event queue.
 *
 *----------------------------------------------------------------------
 */

void
SdlTkRestackTransients(_Window *_w)
{
    _Window *sibling;

    if (_w == NULL) {
	return;
    }

    _w = SdlTkToplevelForWindow(_w, NULL, NULL);

again:
    sibling = _w ? _w->next : NULL;
    while (sibling != NULL) {
	if (SdlTkWrapperForWindow(sibling)->master ==
	    SdlTkWrapperForWindow(_w)) {
	    SdlTkRestackWindow(sibling, _w, Above);
	    SdlTkRestackTransients(sibling);
	    goto again;
	}
	sibling = sibling->next;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkBringToFrontIfNeeded --
 *
 *	Raises the toplevel for the given window above any toplevels
 *	higher in the stacking order which are not transients (or
 *	transients of transients) of the toplevel.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stacking order may change, visible regions may change, <Expose>
 *	events may be added to the event queue.
 *
 *----------------------------------------------------------------------
 */

void
SdlTkBringToFrontIfNeeded(_Window *_w)
{
    _Window *sibling, *master;

    _w = SdlTkToplevelForWindow(_w, NULL, NULL);
    if (_w == NULL) {
	return;
    }

    sibling = _w->parent->child;
    while ((sibling != _w) && SdlTkIsTransientOf(sibling, _w))
	sibling = sibling->next;

    if (sibling != _w) {
	SdlTkRestackWindow(_w, sibling, Above);
	SdlTkRestackTransients(_w);
    }

    master = SdlTkWrapperForWindow(_w);
    if (master != NULL) {
	master = master->master;
    }
    if (master != NULL) {
	SdlTkBringToFrontIfNeeded(master);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkIsTransientOf --
 *
 *	Determine if a toplevel is a transient (or transient of a
 *	transient) of another toplevel.
 *
 * Results:
 *	Returns TRUE if the window is a transient, FALSE otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
SdlTkIsTransientOf(_Window *_w, _Window *other)
{
    _Window *master = SdlTkWrapperForWindow(_w)->master;

    other = SdlTkWrapperForWindow(other);
    while (master != NULL) {
        if (master == other) {
	    return 1;
	}
	master = master->master;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * SdlTkGrabCheck --
 *
 *	Determine if a pointer events should be allowed in a window.
 *
 * Results:
 *	Returns TRUE if the Tk toplevel for the window has a grab on
 *	the pointer, or if the Tk toplevel is not excluded from any
 *	local or global grab that is in progress.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
SdlTkGrabCheck(_Window *_w, int *othergrab)
{
    TkMainInfo *mainInfo;
    int state = 0;
    _w = SdlTkWrapperForWindow(_w);

    *othergrab = 0;
    /* Get the actual Tk toplevel inside the wrapper */
    if (_w->child != NULL) {
        if (_w->child->next != NULL) {
	    _w = _w->child->next; /* skip menubar */
	} else {
	    _w = _w->child;
	}
    }

    if (SdlTkX.keyboard_window != NULL) {
	/* global grab */
	if (SdlTkX.keyboard_window == _w) {
	    return 1;
	}
	return 0;
    }

    if (SdlTkX.capture_window != NULL) {
	return SdlTkX.capture_window == _w->tkwin;
    }

    if (_w->tkwin != NULL) {
        state = TkGrabState(_w->tkwin);
        if (state == TK_GRAB_EXCLUDED) {
	    return 0;
	}
	if (state != TK_GRAB_NONE) {
	    return 1;
	}
	/* Check console vs. main window */
	mainInfo = TkGetMainInfoList();
	while (mainInfo != NULL) {
	    if (mainInfo != _w->tkwin->mainPtr) {
		if (mainInfo->winPtr != NULL) {
		    TkDisplay *dispPtr = mainInfo->winPtr->dispPtr;

		    if (dispPtr->grabWinPtr != NULL) {
			*othergrab = 1;
			return 0;
		    }
		}
	    }
	    mainInfo = mainInfo->nextPtr;
	}
	return 1;
    }

    return 0;
}

void
SdlTkDirtyAll(Window w)
{
    _Window *_w = (_Window *) w;

    SdlTkDirtyArea(w, 0, 0, _w->parentWidth, _w->parentHeight);
}

void
SdlTkDirtyArea(Window w, int x, int y, int width, int height)
{
    _Window *_w = (_Window *) w;
    int xOff, yOff;
    _Window *top = SdlTkToplevelForWindow(_w, &xOff, &yOff);
    XRectangle rect;
    Region rgn;

    if (top == NULL) {
	return;
    }

    rgn = SdlTkRgnPoolGet();
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    XUnionRectWithRegion(&rect, rgn, rgn);
    XIntersectRegion(_w->visRgn, rgn, rgn);
    XOffsetRegion(rgn, xOff, yOff);

    XUnionRegion(rgn, top->dirtyRgn, top->dirtyRgn);
    SdlTkRgnPoolFree(rgn);
}

void
SdlTkDirtyRegion(Window w, Region rgn)
{
    _Window *_w = (_Window *) w;
    int xOff, yOff;
    _Window *top = SdlTkToplevelForWindow(_w, &xOff, &yOff);
    Region r;

    if (top == NULL) {
	return;
    }

    r = SdlTkRgnPoolGet();
    XIntersectRegion(_w->visRgn, rgn, r);
    XOffsetRegion(r, xOff, yOff);
    XUnionRegion(top->dirtyRgn, r, top->dirtyRgn);
    SdlTkRgnPoolFree(r);
}

#ifndef ANDROID
void
SdlTkSetCaretPosUnlocked(int x, int y, int height)
{
    SDL_Rect r;

    SdlTkX.caret_x = x;
    SdlTkX.caret_y = y;
    SdlTkX.caret_height = height;
    TranslatePointer(1, &x, &y);
    if (x < 0) {
	x = 0;
    }
    if (y < 0) {
	y = 0;
    }
    r.x = x;
    r.y = y + height;
    r.w = 32;
    r.h = 4;
    if ((r.x != SdlTkX.caret_rect.x) ||
	(r.y != SdlTkX.caret_rect.y) ||
	(r.w != SdlTkX.caret_rect.w) ||
	(r.h != SdlTkX.caret_rect.h)) {
#ifdef SDL_TEXTINPUT_WITH_HINTS
	SDL_SetTextInputRect(&r, 0);
#else
	SDL_SetTextInputRect(&r);
#endif
	SdlTkX.caret_rect = r;
    }
}

void
SdlTkSetCaretPos(int x, int y, int height)
{
    SdlTkLock(NULL);
    SdlTkSetCaretPosUnlocked(x, y, height);
    SdlTkUnlock(NULL);
}

void
SdlTkResetCaretPos(int locked)
{
    if (!locked) {
	SdlTkLock(NULL);
    }
    SdlTkSetCaretPosUnlocked(SdlTkX.caret_x, SdlTkX.caret_y,
			     SdlTkX.caret_height);
    if (!locked) {
	SdlTkUnlock(NULL);
    }
}
#endif

static int
AccelbufferObjCmd(ClientData clientData, Tcl_Interp *interp,
		  int objc, Tcl_Obj *const objv[])
{
    int axis;
    Tcl_Obj *list;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "axis");
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[1], &axis) != TCL_OK) {
	return TCL_ERROR;
    }
    --axis;
    if ((axis < 0) || (axis > 2)) {
	Tcl_SetResult(interp, "illegal axis", TCL_STATIC);
	return TCL_ERROR;
    }
    list = Tcl_NewListObj(0, NULL);
#ifdef ANDROID
    SdlTkLock(NULL);
    if (SdlTkX.accel_id != -1) {
	AccelRing *rp = &SdlTkX.accel_ring[axis];
	int i, k, imax;

	imax = sizeof (rp->values) / sizeof (rp->values[0]);
	k = rp->index;
	for (i = 0; i < imax; i++) {
	    if (++k >= imax) {
		k = 0;
	    }
	    Tcl_ListObjAppendElement(NULL, list,
				     Tcl_NewIntObj(rp->values[k]));
	}
    }
    SdlTkUnlock(NULL);
#endif
    Tcl_SetObjResult(interp, list);
    return TCL_OK;
}

static int
AccelerometerObjCmd(ClientData clientData, Tcl_Interp *interp,
		    int objc, Tcl_Obj *const objv[])
{
    int flag = 0;

    if (objc > 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "?onoff?");
	return TCL_ERROR;
    }
    if (objc == 2) {
	if (Tcl_GetBooleanFromObj(interp, objv[1], &flag) != TCL_OK) {
	    return TCL_ERROR;
	}
#ifdef ANDROID
	SdlTkLock(NULL);
	if (SdlTkX.accel_id != -1) {
	    SdlTkX.accel_enabled = flag;
	}
	SdlTkUnlock(NULL);
#endif
    } else {
#ifdef ANDROID
	SdlTkLock(NULL);
	flag = SdlTkX.accel_enabled;
	SdlTkUnlock(NULL);
#endif
	Tcl_SetBooleanObj(Tcl_GetObjResult(interp), flag);
    }
    return TCL_OK;
}

static int
AddfontObjCmd(ClientData clientData, Tcl_Interp *interp,
	      int objc, Tcl_Obj *const objv[])
{
    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "filename");
	return TCL_ERROR;
    }
    return SdlTkFontAdd(interp, Tcl_GetString(objv[1]));
}

static int
AndroidObjCmd(ClientData clientData, Tcl_Interp *interp,
	      int objc, Tcl_Obj *const objv[])
{
    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, "");
	return TCL_ERROR;
    }
#ifdef ANDROID
    Tcl_SetBooleanObj(Tcl_GetObjResult(interp), 1);
#else
    Tcl_SetBooleanObj(Tcl_GetObjResult(interp), 0);
#endif
    return TCL_OK;
}

static int
DeiconifyObjCmd(ClientData clientData, Tcl_Interp *interp,
	       int objc, Tcl_Obj *const objv[])
{
    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, "");
	return TCL_ERROR;
    }
    SdlTkSetWindowFlags(SDL_WINDOW_SHOWN, 0, 0, 0, 0);
    return TCL_OK;
}

static int
ExposeObjCmd(ClientData clientData, Tcl_Interp *interp,
	     int objc, Tcl_Obj *const objv[])
{
    Tk_Window tkwin = (Tk_Window) clientData;
    int x, y;
    _Window *_w;
    Region rgn;
    XRectangle rect;
    int ret = TCL_OK;

    if (objc > 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "?window?");
	return TCL_ERROR;
    }
    SdlTkLock(NULL);
    if (objc == 2) {
	Tk_Window tkwin2;

	if (TkGetWindowFromObj(interp, tkwin, objv[1], &tkwin2) != TCL_OK) {
	    ret = TCL_ERROR;
	    goto done;
	}
	_w = (_Window *) ((TkWindow *) tkwin2)->window;
    } else {
	(void) SDL_GetMouseState(&x, &y);
	TranslatePointer(0, &x, &y);
	_w = SdlTkPointToWindow((_Window *) SdlTkX.screen->root,
				x, y, True, True);
    }
    rgn = SdlTkRgnPoolGet();
    rect.x = rect.y = 0;
    rect.width = _w->parentWidth;
    rect.height = _w->parentHeight;
    XUnionRectWithRegion(&rect, rgn, rgn);
    XIntersectRegion(_w->visRgn, rgn, rgn);
    if (IS_ROOT(_w)) {
	XUnionRegion(rgn, SdlTkX.screen_dirty_region,
		     SdlTkX.screen_dirty_region);
	SdlTkScreenChanged();
    } else if (_w->tkwin != NULL) {
	SdlTkGfxExposeRegion((Window) _w, rgn);
    }
    SdlTkRgnPoolFree(rgn);
done:
    SdlTkUnlock(NULL);
    return ret;
}

static int
FontsObjCmd(ClientData clientData, Tcl_Interp *interp,
	       int objc, Tcl_Obj *const objv[])
{
    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, "");
	return TCL_ERROR;
    }
    return SdlTkFontList(interp);
}

static int
FullscreenObjCmd(ClientData clientData, Tcl_Interp *interp,
	       int objc, Tcl_Obj *const objv[])
{
    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, "");
	return TCL_ERROR;
    }
    SdlTkSetWindowFlags(SDL_WINDOW_FULLSCREEN, 0, 0, 0, 0);
    return TCL_OK;
}

static int
HasglObjCmd(ClientData clientData, Tcl_Interp *interp,
	    int objc, Tcl_Obj *const objv[])
{
    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, "");
	return TCL_ERROR;
    }
    Tcl_SetBooleanObj(Tcl_GetObjResult(interp), !SdlTkX.arg_nogl);
    return TCL_OK;
}

static int
IconifyObjCmd(ClientData clientData, Tcl_Interp *interp,
	       int objc, Tcl_Obj *const objv[])
{
    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, "");
	return TCL_ERROR;
    }
    SdlTkSetWindowFlags(SDL_WINDOW_MINIMIZED, 0, 0, 0, 0);
    return TCL_OK;
}

static int
JoystickObjCmd(ClientData clientData, Tcl_Interp *interp,
	       int objc, Tcl_Obj *const objv[])
{
    static const char *joptStrings[] = {
	"ids", "guid", "name", "numaxes", "numballs",
	"numbuttons", "numhats",
	NULL
    };
    enum jopts {
	JOY_IDS, JOY_GUID, JOY_NAME, JOY_NAXES, JOY_NBALLS,
	JOY_NBUTTONS, JOY_NHATS
    };
    int index, joy_id;
    long joy_idl;
    Tcl_HashEntry *hPtr;
    SDL_Joystick *stick;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "suboption ?joyid?");
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], joptStrings,
			    "suboption", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }
    if (index == JOY_IDS) {
	Tcl_Obj *result;
	Tcl_HashSearch search;

	if (objc != 2) {
	    Tcl_WrongNumArgs(interp, 2, objv, "");
	    return TCL_ERROR;
	}
	result = Tcl_NewListObj(0, NULL);
	SdlTkLock(NULL);
	hPtr = Tcl_FirstHashEntry(&SdlTkX.joystick_table, &search);
	while (hPtr != NULL) {
	    joy_idl = (long) Tcl_GetHashKey(&SdlTkX.joystick_table, hPtr);
	    Tcl_ListObjAppendElement(interp, result,
				     Tcl_NewIntObj((int) joy_idl));
	    hPtr = Tcl_NextHashEntry(&search);
	}
	SdlTkUnlock(NULL);
	Tcl_SetObjResult(interp, result);
	return TCL_OK;
    }
    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "joyid");
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[2], &joy_id) != TCL_OK) {
	return TCL_ERROR;
    }
    SdlTkLock(NULL);
    joy_idl = joy_id;
    hPtr = Tcl_FindHashEntry(&SdlTkX.joystick_table, (ClientData) joy_idl);
    if (hPtr == NULL) {
	SdlTkUnlock(NULL);
	Tcl_SetResult(interp, "unknown joystick identifier", TCL_STATIC);
	return TCL_ERROR;
    }
    stick = (SDL_Joystick *) Tcl_GetHashValue(hPtr);
    switch (index) {
    case JOY_NAME:
	Tcl_SetResult(interp, (char *) SDL_JoystickName(stick), TCL_VOLATILE);
	break;
    case JOY_GUID: {
	SDL_JoystickGUID guid;
	char buffer[128];

	guid = SDL_JoystickGetGUID(stick);
	sprintf(buffer, "%02x%02x%02x%02x-%02x%02x-%02x%02x"
		"-%02x%02x-%02x%02x%02x%02x%02x%02x",
		guid.data[0], guid.data[1], guid.data[2],
		guid.data[3], guid.data[4], guid.data[5],
		guid.data[6], guid.data[7], guid.data[8],
		guid.data[9], guid.data[10], guid.data[11],
		guid.data[12], guid.data[13], guid.data[14],
		guid.data[15]);
	Tcl_SetResult(interp, buffer, TCL_VOLATILE);
	break;
    }
    case JOY_NAXES:
	Tcl_SetIntObj(Tcl_GetObjResult(interp), SDL_JoystickNumAxes(stick));
	break;
    case JOY_NBALLS:
	Tcl_SetIntObj(Tcl_GetObjResult(interp), SDL_JoystickNumBalls(stick));
	break;
    case JOY_NHATS:
	Tcl_SetIntObj(Tcl_GetObjResult(interp), SDL_JoystickNumHats(stick));
	break;
    case JOY_NBUTTONS:
	Tcl_SetIntObj(Tcl_GetObjResult(interp), SDL_JoystickNumButtons(stick));
	break;
    }
    SdlTkUnlock(NULL);
    return TCL_OK;
}

static int
LogObjCmd(ClientData clientData, Tcl_Interp *interp,
	  int objc, Tcl_Obj *const objv[])
{
    int prio = SDL_LOG_PRIORITY_VERBOSE;
    const char *prioStr;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "prio message");
	return TCL_ERROR;
    }
    prioStr = Tcl_GetString(objv[1]);
    if (strcmp(prioStr, "verbose") == 0) {
	prio = SDL_LOG_PRIORITY_VERBOSE;
    } else if (strcmp(prioStr, "debug") == 0) {
	prio = SDL_LOG_PRIORITY_DEBUG;
    } else if (strcmp(prioStr, "info") == 0) {
	prio = SDL_LOG_PRIORITY_INFO;
    } else if (strcmp(prioStr, "warn") == 0) {
	prio = SDL_LOG_PRIORITY_WARN;
    } else if (strcmp(prioStr, "error") == 0) {
	prio = SDL_LOG_PRIORITY_ERROR;
    } else if (strcmp(prioStr, "fatal") == 0) {
	prio = SDL_LOG_PRIORITY_CRITICAL;
    }
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, prio, "%s",
		   Tcl_GetString(objv[2]));
    return TCL_OK;
}

static int
MaximizeObjCmd(ClientData clientData, Tcl_Interp *interp,
	       int objc, Tcl_Obj *const objv[])
{
    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, "");
	return TCL_ERROR;
    }
    SdlTkSetWindowFlags(SDL_WINDOW_MAXIMIZED, 0, 0, 0, 0);
    return TCL_OK;
}

static int
MaxrootObjCmd(ClientData clientData, Tcl_Interp *interp,
	      int objc, Tcl_Obj *const objv[])
{
    SDL_RendererInfo ri;
    char buffer[128];

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, "");
	return TCL_ERROR;
    }
    SdlTkLock(NULL);
    SDL_GetRendererInfo(SdlTkX.sdlrend, &ri);
    SdlTkUnlock(NULL);
    sprintf(buffer, "%d %d", ri.max_texture_width, ri.max_texture_height);
    Tcl_SetResult(interp, buffer, TCL_VOLATILE);
    return TCL_OK;
}

static int
OpacityObjCmd(ClientData clientData, Tcl_Interp *interp,
	      int objc, Tcl_Obj *const objv[])
{
    if (objc > 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "?value?");
	return TCL_ERROR;
    }
    if (objc > 1) {
	double d;

	if (Tcl_GetDoubleFromObj(interp, objv[1], &d) != TCL_OK) {
	    return TCL_ERROR;
	}
	SdlTkSetWindowOpacity(d);
    } else {
	float f = 1.0;

	SdlTkLock(NULL);
	SDL_GetWindowOpacity(SdlTkX.sdlscreen, &f);
	SdlTkUnlock(NULL);
	Tcl_SetObjResult(interp, Tcl_NewDoubleObj(f));
    }
    return TCL_OK;
}

static int
PaintvisrgnObjCmd(ClientData clientData, Tcl_Interp *interp,
		  int objc, Tcl_Obj *const objv[])
{
    Tk_Window tkwin = (Tk_Window) clientData;
    int x, y;
    _Window *_w;
    Region r;
    int ret = TCL_OK;

    if (objc > 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "?window?");
	return TCL_ERROR;
    }
    SdlTkLock(NULL);
    if (objc == 2) {
	Tk_Window tkwin2;

	if (TkGetWindowFromObj(interp, tkwin, objv[1], &tkwin2) != TCL_OK) {
	    ret = TCL_ERROR;
	    goto done;
	}
	_w = (_Window *) ((TkWindow *) tkwin2)->window;
    } else {
	(void) SDL_GetMouseState(&x, &y);
	TranslatePointer(0, &x, &y);
	_w = SdlTkPointToWindow((_Window *) SdlTkX.screen->root,
				x, y, True, True);
    }
    r = SdlTkGetVisibleRegion(_w);
    SdlTkGfxFillRegion((Drawable) _w, r, 0x0000FF88);
    SDL_UpdateWindowSurface(SdlTkX.sdlscreen);
done:
    SdlTkUnlock(NULL);
    return ret;
}

static int
PowerinfoObjCmd(ClientData clientData, Tcl_Interp *interp,
		int objc, Tcl_Obj *const objv[])
{
    SDL_PowerState pst;
    int secs, pct;
    char buf[32];

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, "");
	return TCL_ERROR;
    }
    SdlTkLock(NULL);
    pst = SDL_GetPowerInfo(&secs, &pct);
    SdlTkUnlock(NULL);
    Tcl_AppendElement(interp, "state");
    switch (pst) {
    case SDL_POWERSTATE_ON_BATTERY:
	Tcl_AppendElement(interp, "onbattery");
	break;
    case SDL_POWERSTATE_NO_BATTERY:
	Tcl_AppendElement(interp, "nobattery");
	break;
    case SDL_POWERSTATE_CHARGING:
	Tcl_AppendElement(interp, "charging");
	break;
    case SDL_POWERSTATE_CHARGED:
	Tcl_AppendElement(interp, "charged");
	break;
    default:
	Tcl_AppendElement(interp, "unknown");
	break;
    }
    Tcl_AppendElement(interp, "seconds");
    sprintf(buf, "%d", secs);
    Tcl_AppendElement(interp, buf);
    Tcl_AppendElement(interp, "percent");
    sprintf(buf, "%d", pct);
    Tcl_AppendElement(interp, buf);
    return TCL_OK;
}

static int
RestoreObjCmd(ClientData clientData, Tcl_Interp *interp,
	       int objc, Tcl_Obj *const objv[])
{
    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, "");
	return TCL_ERROR;
    }
    SdlTkSetWindowFlags(SDL_WINDOW_SHOWN | SDL_WINDOW_HIDDEN, 0, 0, 0, 0);
    return TCL_OK;
}

static int
RootObjCmd(ClientData clientData, Tcl_Interp *interp,
	   int objc, Tcl_Obj *const objv[])
{
    int w, h;
    char buffer[128];

    if ((objc != 1) && (objc != 3)) {
	Tcl_WrongNumArgs(interp, 1, objv, "?width height?");
	return TCL_ERROR;
    }
    if (objc > 1) {
	SDL_RendererInfo ri;

	if ((Tcl_GetIntFromObj(interp, objv[1], &w) != TCL_OK) ||
	    (Tcl_GetIntFromObj(interp, objv[2], &h) != TCL_OK)) {
	    return TCL_ERROR;
	}
	SdlTkLock(NULL);
	SDL_GetRendererInfo(SdlTkX.sdlrend, &ri);
	SdlTkUnlock(NULL);
	if ((w == 0) && (h == 0)) {
	    /* accepted, native size */
	} else if ((w < 200) || (h < 200) ||
		   (w > ri.max_texture_width) || (h > ri.max_texture_height)) {
	    Tcl_SetResult(interp, "unsupported width or height", TCL_STATIC);
	    return TCL_ERROR;
	}
    }
    if (objc > 1) {
	SdlTkSetRootSize(w, h);
    } else {
	SdlTkLock(NULL);
	sprintf(buffer, "%d %d", SdlTkX.root_w, SdlTkX.root_h);
	SdlTkUnlock(NULL);
    }
    if (objc <= 1) {
	Tcl_SetResult(interp, buffer, TCL_VOLATILE);
    }
    return TCL_OK;
}

static int
ScreensaverObjCmd(ClientData clientData, Tcl_Interp *interp,
		  int objc, Tcl_Obj *const objv[])
{
    int flag;

    if (objc > 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "?flag?");
	return TCL_ERROR;
    }
    if (objc > 1) {
	if (Tcl_GetBooleanFromObj(interp, objv[1], &flag) != TCL_OK) {
	    return TCL_ERROR;
	}
	SdlTkLock(NULL);
	if (flag) {
	    SDL_EnableScreenSaver();
	} else {
	    SDL_DisableScreenSaver();
	}
	SdlTkUnlock(NULL);
    }
    SdlTkLock(NULL);
    flag = SDL_IsScreenSaverEnabled();
    SdlTkUnlock(NULL);
    Tcl_SetBooleanObj(Tcl_GetObjResult(interp), flag);
    return TCL_OK;
}

static int
StatObjCmd(ClientData clientData, Tcl_Interp *interp,
	   int objc, Tcl_Obj *const objv[])
{
    Tk_Window tkwin = (Tk_Window) clientData;
    Display *display;
    Tcl_DString ds;
    char buffer[128];
    int *rgnCounts;

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, NULL);
	return TCL_ERROR;
    }
    Tcl_DStringInit(&ds);
    SdlTkLock(NULL);
    rgnCounts = SdlTkRgnPoolStat();
    sprintf(buffer, "frame_count %ld", SdlTkX.frame_count);
    Tcl_DStringAppend(&ds, buffer, -1);
    sprintf(buffer, " time_count %ld", SdlTkX.time_count);
    Tcl_DStringAppend(&ds, buffer, -1);
    sprintf(buffer, " window_free %d", SdlTkX.nwfree);
    Tcl_DStringAppend(&ds, buffer, -1);
    sprintf(buffer, " window_total %d", SdlTkX.nwtotal);
    Tcl_DStringAppend(&ds, buffer, -1);
    sprintf(buffer, " region_free %d", rgnCounts[0]);
    Tcl_DStringAppend(&ds, buffer, -1);
    sprintf(buffer, " region_total %d", rgnCounts[1]);
    Tcl_DStringAppend(&ds, buffer, -1);
    SdlTkUnlock(NULL);
    display = Tk_Display(tkwin);
    Tcl_MutexLock((Tcl_Mutex *) &display->qlock);
    sprintf(buffer, " event_length %d", display->qlen);
    Tcl_DStringAppend(&ds, buffer, -1);
    sprintf(buffer, " event_length_max %d", display->qlenmax);
    Tcl_DStringAppend(&ds, buffer, -1);
    sprintf(buffer, " event_total %d", display->nqtotal);
    Tcl_DStringAppend(&ds, buffer, -1);
    Tcl_MutexUnlock((Tcl_Mutex *) &display->qlock);
    Tcl_DStringResult(interp, &ds);
    return TCL_OK;
}

static int
TextinputObjCmd(ClientData clientData, Tcl_Interp *interp,
		int objc, Tcl_Obj *const objv[])
{
    static int last_hints = 0;
    int flag = 0, hints = 0, hints_changed = 0, ret = TCL_OK;

    if (objc > 5) {
tiWrongArgs:
	Tcl_WrongNumArgs(interp, 1, objv, "?onoff ?x y ?hints???");
	return TCL_ERROR;
    }
    if ((objc == 2) || (objc == 4) || (objc == 5)) {
	if (Tcl_GetBooleanFromObj(interp, objv[1], &flag) != TCL_OK) {
	    return TCL_ERROR;
	}
	SdlTkLock(NULL);
	if (!SDL_HasScreenKeyboardSupport()) {
	    goto tiDone;
	}
	if (flag) {
	    if ((objc == 4) || (objc == 5)) {
		int x, y;
		SDL_Rect r;

		if ((Tcl_GetIntFromObj(interp, objv[2], &x) != TCL_OK) ||
		    (Tcl_GetIntFromObj(interp, objv[3], &y) != TCL_OK)) {
		    ret = TCL_ERROR;
		    goto tiDone;
		}
		TranslatePointer(1, &x, &y);
		x -= 64;
		if (x < 0) {
		    x = 0;
		}
		y -= 64;
		if (y < 0) {
		    y = 0;
		}
		r.x = x;
		r.y = y;
		r.w = 256;
		r.h = 128;
		if (objc > 4) {
		    if (Tcl_GetIntFromObj(interp, objv[4], &hints) != TCL_OK) {
			ret = TCL_ERROR;
			goto tiDone;
		    }
		}
		if (hints != last_hints) {
		    hints_changed = 1;
		    last_hints = hints;
		}
#ifdef SDL_TEXTINPUT_WITH_HINTS
		SDL_SetTextInputRect(&r, hints);
#else
		SDL_SetTextInputRect(&r);
#endif
	    }
	    if (hints_changed &&
		SDL_IsScreenKeyboardShown(SdlTkX.sdlscreen)) {
		SDL_StopTextInput();
	    }
	    SDL_StartTextInput();
	} else {
	    SDL_StopTextInput();
	}
tiDone:
	SdlTkUnlock(NULL);
	return ret;
    } else if (objc == 3) {
	goto tiWrongArgs;
    }
    SdlTkLock(NULL);
    if (SDL_HasScreenKeyboardSupport()) {
	flag = SDL_IsScreenKeyboardShown(SdlTkX.sdlscreen);
    }
    SdlTkUnlock(NULL);
    Tcl_SetBooleanObj(Tcl_GetObjResult(interp), flag);
    return ret;
}

static int
TouchtranslateObjCmd(ClientData clientData, Tcl_Interp *interp,
		     int objc, Tcl_Obj *const objv[])
{
#ifdef ANDROID
    int flag = TranslateInfo.enabled;
#else
    int flag = translate_zoom ? TRANSLATE_ZOOM : 0;
#endif

    if (objc > 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "?mask?");
	return TCL_ERROR;
    }
    if (objc > 1) {
	if (Tcl_GetIntFromObj(interp, objv[1], &flag) != TCL_OK) {
	    return TCL_ERROR;
	}
#ifdef ANDROID
	flag &= TRANSLATE_RMB | TRANSLATE_PTZ |
	    TRANSLATE_ZOOM | TRANSLATE_FINGER | TRANSLATE_FBTNS;
	SdlTkLock(NULL);
	if (flag != TranslateInfo.enabled) {
	    TranslateInfo.enabled = flag;
	    if (!(flag & TRANSLATE_RMB)) {
		TranslateInfo.function = NULL;
	    }
	    TranslateInfo.state = 0;
	    TranslateInfo.count = 0;
	}
	SdlTkUnlock(NULL);
#else
	translate_zoom = (flag & TRANSLATE_ZOOM) ? 1 : 0;
	flag = translate_zoom ? TRANSLATE_ZOOM : 0;
#endif
    }
    Tcl_SetIntObj(Tcl_GetObjResult(interp), flag);
    return TCL_OK;
}

static int
ViewportObjCmd(ClientData clientData, Tcl_Interp *interp,
	       int objc, Tcl_Obj *const objv[])
{
    int x, y, w, h;
    char buffer[128];

    if ((objc != 1) && (objc != 3) && (objc != 5)) {
	Tcl_WrongNumArgs(interp, 1, objv, "?xoffset yoffset? width height??");
	return TCL_ERROR;
    }
    if (objc > 1) {
	if ((Tcl_GetIntFromObj(interp, objv[1], &x) != TCL_OK) ||
	    (Tcl_GetIntFromObj(interp, objv[2], &y) != TCL_OK)) {
	    return TCL_ERROR;
	}
    }
    if (objc > 3) {
	int sw, sh;

	if ((Tcl_GetIntFromObj(interp, objv[3], &w) != TCL_OK) ||
	    (Tcl_GetIntFromObj(interp, objv[4], &h) != TCL_OK)) {
	    return TCL_ERROR;
	}
	SdlTkLock(NULL);
	SDL_GetWindowSize(SdlTkX.sdlscreen, &sw, &sh);
	SdlTkUnlock(NULL);
	if ((w < 0) || (h < 0) || (w > sw) || (h > sh)) {
	    Tcl_SetResult(interp, "illegal width or height", TCL_STATIC);
	    return TCL_ERROR;
	}
    }
    if (objc > 1) {
	SdlTkPanZoom(0, x, y, w, h);
    } else {
	SdlTkLock(NULL);
	sprintf(buffer, "%d %d %d %d",
		SdlTkX.viewport.x, SdlTkX.viewport.y,
		SdlTkX.viewport.w, SdlTkX.viewport.h);
	SdlTkUnlock(NULL);
    }
    if (objc <= 1) {
	Tcl_SetResult(interp, buffer, TCL_VOLATILE);
    }
    return TCL_OK;
}

static int
VsyncObjCmd(ClientData clientData, Tcl_Interp *interp,
	    int objc, Tcl_Obj *const objv[])
{
    int frame_count;

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, "");
	return TCL_ERROR;
    }
    SdlTkLock(NULL);
    frame_count = SdlTkX.frame_count;
    SdlTkWaitVSync();
    if (SdlTkX.frame_count == frame_count) {
	SdlTkWaitVSync();
    }
    frame_count -= SdlTkX.frame_count;
    SdlTkUnlock(NULL);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(frame_count));
    return TCL_OK;
}

static int
WithdrawObjCmd(ClientData clientData, Tcl_Interp *interp,
	       int objc, Tcl_Obj *const objv[])
{
    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, "");
	return TCL_ERROR;
    }
    SdlTkSetWindowFlags(SDL_WINDOW_HIDDEN, 0, 0, 0, 0);
    return TCL_OK;
}

/*
 * Table of sdltk subcommand names and implementations.
 */

static const TkEnsemble sdltkCmdMap[] = {
    { "accelbuffer", AccelbufferObjCmd, NULL },
    { "accelerometer", AccelerometerObjCmd, NULL },
    { "addfont", AddfontObjCmd, NULL },
    { "android", AndroidObjCmd, NULL },
    { "deiconify", DeiconifyObjCmd, NULL },
    { "expose", ExposeObjCmd, NULL },
    { "fonts", FontsObjCmd, NULL },
    { "fullscreen", FullscreenObjCmd, NULL },
    { "hasgl", HasglObjCmd, NULL },
    { "iconify", IconifyObjCmd, NULL },
    { "joystick", JoystickObjCmd, NULL },
    { "log", LogObjCmd, NULL },
    { "maxroot", MaxrootObjCmd, NULL },
    { "opacity", OpacityObjCmd, NULL },
    { "maximize", MaximizeObjCmd, NULL },
    { "paintvisrgn", PaintvisrgnObjCmd, NULL },
    { "powerinfo", PowerinfoObjCmd, NULL },
    { "restore", RestoreObjCmd, NULL },
    { "root", RootObjCmd, NULL },
    { "screensaver", ScreensaverObjCmd, NULL },
    { "stat", StatObjCmd, NULL },
    { "textinput", TextinputObjCmd, NULL },
    { "touchtranslate", TouchtranslateObjCmd, NULL },
    { "viewport", ViewportObjCmd, NULL },
    { "vsync", VsyncObjCmd, NULL },
    { "withdraw", WithdrawObjCmd, NULL },
    { NULL, NULL, NULL }
};

int
TkInitSdltkCmd(Tcl_Interp *interp, ClientData clientData)
{
    TkMakeEnsemble(interp, "::", "sdltk", clientData, sdltkCmdMap);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetMS --
 *
 *	Return a relative time in milliseconds.  It doesn't matter
 *	when the epoch was.
 *
 * Results:
 *	Number of milliseconds.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

unsigned long
TkpGetMS(void)
{
    /* Used for XEvent time stamps. */
    return SdlTkX.time_count;
}

/* NOTE: If this changes, update XAllocColor */
unsigned long
TkpGetPixel(XColor *color)
{
    Uint8 r = (color->red / 65535.0) * 255.0;
    Uint8 g = (color->green / 65535.0) * 255.0;
    Uint8 b = (color->blue / 65535.0) * 255.0;

    /* All SDL_gfx xxxColor() routines expect RGBA format */
    return SDL_MapRGB(SdlTkX.sdlsurf->format, r, g, b);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetCaptureEx --
 *
 *	This function captures the mouse so that all future events
 *	will be reported to this window, even if the mouse is outside
 *	the window.  If the specified window is NULL, then the mouse
 *	is released.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the capture flag and captures the mouse.
 *
 *----------------------------------------------------------------------
 */

int
TkpSetCaptureEx(Display *display, TkWindow *winPtr)
{
    _Window *w0 = NULL, *w1 = NULL;
    int ret = GrabSuccess;

    SdlTkLock(display);
    display->request++;
    if (winPtr != NULL) {
	w1 = (_Window *) winPtr->window;
    }
    if (SdlTkX.capture_window != NULL) {
	w0 = (_Window *) SdlTkX.capture_window->window;
    }
    if ((w0 != NULL) && (w1 != NULL)) {
	if (w0->display == w1->display) {
	    SdlTkX.capture_window = winPtr;
	} else {
	    ret = GrabFrozen;
	}
    } else if (w0 == NULL) {
	SdlTkX.capture_window = winPtr;
    } else if (w1 == NULL) {
	if (display == w0->display) {
	    SdlTkX.capture_window = winPtr;
	} else {
	    ret = GrabFrozen;
	}
    }
    SdlTkUnlock(display);
    return ret;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSetCursor --
 *
 *	Set the global cursor.  If the cursor is None, then use the
 *	default Tk cursor.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Changes the mouse cursor.
 *
 *----------------------------------------------------------------------
 */

void
SdlTkSetCursor(TkpCursor cursor)
{
#ifndef ANDROID
    _Cursor *_c = (_Cursor *) cursor;
    long shape = SDL_SYSTEM_CURSOR_ARROW;
    SDL_Cursor *sc = NULL;
    Tcl_HashEntry *hPtr;

    if (_c != NULL) {
	shape = _c->shape;
    }
    hPtr = Tcl_FindHashEntry(&SdlTkX.sdlcursors, (ClientData) shape);
    if (hPtr == NULL) {
	sc = SDL_CreateSystemCursor(shape);
	if (sc != NULL) {
	    int isNew;

	    hPtr = Tcl_CreateHashEntry(&SdlTkX.sdlcursors,
				       (ClientData) shape, &isNew);
	    Tcl_SetHashValue(hPtr, (ClientData) sc);
	}
    } else {
	sc = (SDL_Cursor *) Tcl_GetHashValue(hPtr);
    }
    if (sc != NULL) {
	SDL_SetCursor(sc);
    }
#endif
}

void
TkpSetCursor(TkpCursor cursor)
{
#ifndef ANDROID
    SdlTkLock(NULL);
    if (SdlTkX.cursor_change) {
	SdlTkSetCursor(cursor);
    }
    SdlTkUnlock(NULL);
#endif
}

void
SdlTkClearPointer(_Window *_w)
{
    if ((_w != NULL) && (_w->tkwin != NULL)) {
	if (SdlTkX.capture_window == _w->tkwin) {
	    SdlTkX.capture_window = NULL;
	}
	if (SdlTkX.mouse_window == _w) {
	    SdlTkX.mouse_window = NULL;
	}
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
