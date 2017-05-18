#include <X11/Xlib.h>
#include "tkInt.h"
#include "default.h"
#include "SdlTkInt.h"

/* Window button identifiers */

enum {
    DEC_BUTTON_NONE,
    DEC_BUTTON_CLOSE,
#ifdef MIN_MAX_BOXES
    DEC_BUTTON_MAXIMIZE,
    DEC_BUTTON_MINIMIZE,
    DEC_NUM_BUTTONS = DEC_BUTTON_MINIMIZE
#else
    DEC_NUM_BUTTONS = DEC_BUTTON_CLOSE
#endif
};

/* Hit test direction identifiers */

enum {
    DEC_HIT_NONE,
    DEC_HIT_TITLE,
    DEC_HIT_NW,
    DEC_HIT_N,
    DEC_HIT_NE,
    DEC_HIT_W,
    DEC_HIT_E,
    DEC_HIT_SW,
    DEC_HIT_S,
    DEC_HIT_SE,
    DEC_HIT_BUTTON
};

/* Hit test type identifiers */

enum {
    DEC_TRACK_NONE,
    DEC_TRACK_BUTTON,
    DEC_TRACK_DRAG,
    DEC_TRACK_RESIZE
};

/*
 * Info about decorative frame window which has titlebar and
 * close button (and min/max buttons)
 */

struct _DecFrame {
    int draw;		 /* true if frame needs redrawing */
    int button;		 /* DEC_BUTTON_XXX mouse is over */
    int pressed;	 /* true if mouse is down in a button */
    int active;		 /* true if frame should be drawn active */
};

/*
 * Local static globals
 */

struct {
    XFontStruct *titlebar_font;
    GC bgGC, lightGC, darkGC;
    _Window *track_toplevel;
    int track_action;
    int track_button;
    int track_edge;
    int track_x0;
    int track_y0;
    _Window *motion_toplevel;
    int motion_button;
#ifndef ANDROID
    int last_hit;
#endif
} _DFInfo = {
    NULL,
    None, None, None,
    NULL,
    DEC_TRACK_NONE,
    DEC_BUTTON_NONE,
    DEC_HIT_NONE,
    0, 0,
    NULL,
    DEC_BUTTON_NONE,
#ifndef ANDROID
    DEC_HIT_NONE,
#endif
};

static void
GetButtonBounds(_Window *_w, int button, int *x, int *y, int *w, int *h)
{
#ifdef ANDROID
    int fw = SdlTkX.dec_frame_width - 1;
    int buttonSize = SdlTkX.dec_title_height - fw;

    *x = _w->atts.width - (fw + buttonSize) * button;
    *y = SdlTkX.dec_frame_width / 2;
#else
    int buttonSize = SdlTkX.dec_title_height - 6;

    *x = _w->atts.width - (5 + buttonSize) * button;
    *y = 3;
#endif
    *w = *h = buttonSize;
}

static int
HitTestFrame(_Window *_w, int x, int y, int *button)
{
    int i;
    int n = 0, s = 0, w = 0, e = 0;
    int buttonX, buttonY, buttonW, buttonH;

    *button = DEC_BUTTON_NONE;

    for (i = 1; i <= DEC_NUM_BUTTONS; i++) {
	GetButtonBounds(_w, i, &buttonX, &buttonY, &buttonW, &buttonH);
	if ((x >= buttonX) && (x < buttonX + buttonW) &&
	    (y >= buttonY) && (y < buttonY + buttonH)) {
	    *button = i;
	    return DEC_HIT_BUTTON;
	}
    }

    if (y < SdlTkX.dec_frame_width - 2) {
	n = 1;
    }
    if (y >= _w->atts.height - (SdlTkX.dec_frame_width - 2)) {
	s = 1;
    }
    if (x < SdlTkX.dec_frame_width - 2) {
	w = 1;
    }
    if (x >= _w->atts.width - (SdlTkX.dec_frame_width - 2)) {
	e = 1;
    }

    if (n && w) {
        return DEC_HIT_NW;
    }
    if (n && e) {
        return DEC_HIT_NE;
    }
    if (s && w) {
        return DEC_HIT_SW;
    }
    if (s && e) {
        return DEC_HIT_SE;
    }
    if (n) {
        return DEC_HIT_N;
    }
    if (s) {
        return DEC_HIT_S;
    }
    if (w) {
        return DEC_HIT_W;
    }
    if (e) {
        return DEC_HIT_E;
    }

    if (y < SdlTkX.dec_title_height) {
	return DEC_HIT_TITLE;
    }

    return DEC_HIT_NONE;
}

static void
EnterButton(_Window *_w, int button)
{
    if ((_DFInfo.track_action == DEC_TRACK_BUTTON) &&
	((_DFInfo.track_toplevel != _w) || (_DFInfo.track_button != button))) {
	return;
    }
    _w->dec->button = button;
    _w->dec->pressed = _DFInfo.track_action == DEC_TRACK_BUTTON;

    _w->dec->draw = 1;
    SdlTkScreenChanged();
}

static void
LeaveButton(_Window *_w, int button)
{
    if ((_DFInfo.track_action == DEC_TRACK_BUTTON) &&
	((_DFInfo.track_toplevel != _w) || (_DFInfo.track_button != button))) {
	return;
    }
    _w->dec->button = DEC_BUTTON_NONE;
    _w->dec->pressed = False;

    _w->dec->draw = 1;
    SdlTkScreenChanged();
}

static void
Send_WM_DELETE_WINDOW(TkWindow *tkwin)
{
    XEvent event;

    memset(&event, 0, sizeof (event));
    event.type = ClientMessage;
    event.xclient.serial = tkwin->display->request;
    event.xclient.send_event = False;
    event.xclient.display = tkwin->display;
    event.xclient.window = tkwin->window;
    event.xclient.message_type = SdlTkX.wm_prot_atom;
    event.xclient.format = 32;
    event.xclient.data.l[0] = SdlTkX.wm_dele_atom;
    SdlTkQueueEvent(&event);
}

/* Handle mouse event in a decframe */
int
SdlTkDecFrameEvent(_Window *_w, SDL_Event *sdl_event, int x, int y)
{
    int hit, button, dummy;

    switch (sdl_event->type) {
    case SDL_MOUSEBUTTONDOWN:
	if ((sdl_event->button.which != SDL_TOUCH_MOUSEID) &&
	    (sdl_event->button.button != SDL_BUTTON_LEFT)) {
	    break;
	}
	if (_w->dec == NULL) {
	    break;
	}
	if (!SdlTkGrabCheck(_w, &dummy)) {
	    break;
	}
	hit = HitTestFrame(_w, x - _w->atts.x, y - _w->atts.y, &button);
	switch (hit) {
	case DEC_HIT_BUTTON:
	    _DFInfo.track_toplevel = _w;
	    _DFInfo.track_action = DEC_TRACK_BUTTON;
	    _DFInfo.track_button = button;
	    if (_DFInfo.motion_toplevel == NULL) {
		_DFInfo.motion_toplevel = _w;
		_DFInfo.motion_button = button;
	    }
	    EnterButton(_w, button); /* redraw pressed */
	    break;
	case DEC_HIT_TITLE:
	    _DFInfo.track_toplevel = _w;
	    _DFInfo.track_action = DEC_TRACK_DRAG;
	    _DFInfo.track_x0 = x;
	    _DFInfo.track_y0 = y;
	    break;
	default:
	    _DFInfo.track_toplevel = _w;
	    _DFInfo.track_action = DEC_TRACK_RESIZE;
	    _DFInfo.track_edge = hit;
	    _DFInfo.track_x0 = x;
	    _DFInfo.track_y0 = y;
	    break;
	}
	return 1;

    case SDL_MOUSEBUTTONUP:
	if ((sdl_event->button.which != SDL_TOUCH_MOUSEID) &&
	    (sdl_event->button.button != SDL_BUTTON_LEFT)) {
	    break;
	}
	if (_DFInfo.track_action != DEC_TRACK_NONE) {
	    switch (_DFInfo.track_action) {
	    case DEC_TRACK_BUTTON:
		if ((_DFInfo.motion_toplevel == _w) &&
		    (_DFInfo.track_toplevel == _w) &&
		    (_DFInfo.track_button == _DFInfo.motion_button)) {
		    if (_DFInfo.track_button == DEC_BUTTON_CLOSE) {
			TkWindow *tkwin = _w->child->tkwin;

			if (tkwin != NULL &&
			    !(tkwin->flags & TK_ALREADY_DEAD)) {
			    Send_WM_DELETE_WINDOW(tkwin);
			}
		    }
		    /* redraw not pressed */
		    _w->dec->pressed = 0;
		    _w->dec->draw = 1;
		    SdlTkScreenChanged();
		} else if (_DFInfo.motion_button != DEC_BUTTON_NONE) {
		    _DFInfo.motion_toplevel->dec->draw = 1;
		    _DFInfo.motion_toplevel->dec->button =
			_DFInfo.motion_button;
		    SdlTkScreenChanged();
		}
		break;
	    }
	    _DFInfo.track_toplevel = NULL;
	    _DFInfo.track_action = DEC_TRACK_NONE;
	    return 1;
	}
	break;

    case SDL_MOUSEMOTION: {
	int dx, dy;
	_Window *child;

	dx = sdl_event->motion.xrel;
	dy = sdl_event->motion.yrel;
	if (_DFInfo.track_action == DEC_TRACK_DRAG) {
	    /*
	     * Note: dragging the wrapper to the new position
	     * of the decframe
	     */
	    child = _DFInfo.track_toplevel->child;
drag:
	    SdlTkMoveWindow(SdlTkX.display, (Window) child,
			    _DFInfo.track_toplevel->atts.x + dx,
			    _DFInfo.track_toplevel->atts.y + dy);
	    return 1;
	}
	if (_DFInfo.track_action == DEC_TRACK_RESIZE) {
	    int dw, dh;

	    child = _DFInfo.track_toplevel->child;
	    if (child->size.flags & PResizeInc) {
		dw = child->size.width_inc;
		if (dw > 0) {
		    dx = x - _DFInfo.track_x0;
		    dx = dx / dw;
		    dx *= dw;
		}
		dh = child->size.height_inc;
		if (dh > 0) {
		    dy = y - _DFInfo.track_y0;
		    dy = dy / dh;
		    dy *= dh;
		}
	    }
	    if ((child->size.flags & PMinSize) &&
		(child->size.flags & PMaxSize) &&
		(child->size.min_width == child->size.max_width) &&
		(child->size.min_height == child->size.max_height)) {
		goto drag;
	    }
	    dw = dh = 0;
	    switch (_DFInfo.track_edge) {
	    case DEC_HIT_NW:
	    case DEC_HIT_SW:
	    case DEC_HIT_W:
		dw = child->atts.width - dx;
		break;
	    case DEC_HIT_NE:
	    case DEC_HIT_SE:
	    case DEC_HIT_E:
		dw = child->atts.width + dx;
		break;
	    }
	    if (child->size.flags & PMinSize) {
		if (dw < child->size.min_width) {
		    dx = 0;
		}
	    }
	    if (child->size.flags & PMaxSize) {
		if (dw > child->size.max_width) {
		    dx = 0;
		}
	    }
	    switch (_DFInfo.track_edge) {
	    case DEC_HIT_NW:
	    case DEC_HIT_NE:
	    case DEC_HIT_N:
		dh = child->atts.height - dy;
		break;
	    case DEC_HIT_SW:
	    case DEC_HIT_SE:
	    case DEC_HIT_S:
		dh = child->atts.height + dy;
		break;
	    }
	    if (child->size.flags & PMinSize) {
		if (dh < child->size.min_height) {
		    dy = 0;
		}
	    }
	    if (child->size.flags & PMaxSize) {
		if (dh > child->size.max_height) {
		    dy = 0;
		}
	    }
	    if ((dx == 0) && (dy == 0)) {
		return 1;
	    }
	    switch (_DFInfo.track_edge) {
	    case DEC_HIT_NW:
		SdlTkMoveResizeWindow(SdlTkX.display, (Window) child,
				      _DFInfo.track_toplevel->atts.x + dx,
				      _DFInfo.track_toplevel->atts.y + dy,
				      child->atts.width - dx,
				      child->atts.height - dy);
		break;
	    case DEC_HIT_NE:
		SdlTkMoveResizeWindow(SdlTkX.display, (Window) child,
				      _DFInfo.track_toplevel->atts.x,
				      _DFInfo.track_toplevel->atts.y + dy,
				      child->atts.width + dx,
				      child->atts.height - dy);
		break;
	    case DEC_HIT_SW:
		SdlTkMoveResizeWindow(SdlTkX.display, (Window) child,
				      _DFInfo.track_toplevel->atts.x + dx,
				      _DFInfo.track_toplevel->atts.y,
				      child->atts.width - dx,
				      child->atts.height + dy);
		break;
	    case DEC_HIT_SE:
		SdlTkResizeWindow(SdlTkX.display, (Window) child,
				  child->atts.width + dx,
				  child->atts.height + dy);
		break;
	    case DEC_HIT_N:
		SdlTkMoveResizeWindow(SdlTkX.display, (Window) child,
				      _DFInfo.track_toplevel->atts.x,
				      _DFInfo.track_toplevel->atts.y + dy,
				      child->atts.width,
				      child->atts.height - dy);
		break;
	    case DEC_HIT_S:
		SdlTkResizeWindow(SdlTkX.display, (Window) child,
				  child->atts.width,
				  child->atts.height + dy);
		break;
	    case DEC_HIT_W:
		SdlTkMoveResizeWindow(SdlTkX.display, (Window) child,
				      _DFInfo.track_toplevel->atts.x + dx,
				      _DFInfo.track_toplevel->atts.y,
				      child->atts.width - dx,
				      child->atts.height);
		break;
	    case DEC_HIT_E:
		SdlTkResizeWindow(SdlTkX.display, (Window) child,
				  child->atts.width + dx,
				  child->atts.height);
		break;
	    }
	    _DFInfo.track_x0 = x;
	    _DFInfo.track_y0 = y;
	    return 1;
	}

	/* If the pointer is in the root, leave any button */
	if (_w->dec == NULL) {
	    if (_DFInfo.motion_button) {
		LeaveButton(_DFInfo.motion_toplevel,
			    _DFInfo.motion_button);
	    }
	    _DFInfo.motion_toplevel = NULL;
	    _DFInfo.motion_button = DEC_BUTTON_NONE;
	    hit = DEC_HIT_NONE;
	} else {
	    hit = HitTestFrame(_w, x - _w->atts.x, y - _w->atts.y, &button);
	    if ((button != _DFInfo.motion_button) ||
		(_w != _DFInfo.motion_toplevel)) {
		if (_DFInfo.motion_button) {
		    LeaveButton(_DFInfo.motion_toplevel,
				_DFInfo.motion_button);
		}
		if (SdlTkGrabCheck(_w, &dummy)) {
		    _DFInfo.motion_toplevel = _w;
		    _DFInfo.motion_button = button;
		} else {
		    _DFInfo.motion_toplevel = NULL;
		    _DFInfo.motion_button = DEC_BUTTON_NONE;
		    hit = DEC_HIT_NONE;
		}
		if (_DFInfo.motion_button) {
		    EnterButton(_DFInfo.motion_toplevel,
				_DFInfo.motion_button);
		}
	    }
	}
#ifndef ANDROID
	if (hit != _DFInfo.last_hit) {
	    _Cursor _c;

	    _DFInfo.last_hit = hit;
	    switch (hit) {
	    case DEC_HIT_TITLE:
		_c.shape = SDL_SYSTEM_CURSOR_HAND;
		break;
	    case DEC_HIT_NW:
	    case DEC_HIT_SE:
		_c.shape = SDL_SYSTEM_CURSOR_SIZENWSE;
		break;
	    case DEC_HIT_NE:
	    case DEC_HIT_SW:
		_c.shape = SDL_SYSTEM_CURSOR_SIZENESW;
		break;
	    case DEC_HIT_N:
	    case DEC_HIT_S:
		_c.shape = SDL_SYSTEM_CURSOR_SIZENS;
		break;
	    case DEC_HIT_W:
	    case DEC_HIT_E:
		_c.shape = SDL_SYSTEM_CURSOR_SIZEWE;
		break;
	    default:
		_c.shape = SDL_SYSTEM_CURSOR_ARROW;
		break;
	    }
	    SdlTkSetCursor((TkpCursor) &_c);
	}
#endif
	/* Hide the event from Tk if mousebutton 1 is pressed */
	if (_DFInfo.track_action != DEC_TRACK_NONE) {
	    return 1;
	}
	return 0;
    }
    case SDL_QUIT: {
	_Window *_w = (_Window *) SdlTkX.screen->root;
	int evsent = 0;

	if (_w->child != NULL) {
	    _w = _w->child;
	}
	while (_w != NULL) {
	    TkWindow *tkwin = _w->tkwin;
	    _Window *_ww = _w->child;

	    if (tkwin == NULL) {
		if (_w->dec != NULL) {
		    tkwin = _w->tkwin;
		}
	    }
	    if (tkwin == NULL) {
		if ((_ww != NULL) && (_ww->child != NULL)) {
		    tkwin = _ww->child->tkwin;
		}
	    }
	    if ((tkwin != NULL) && !(tkwin->flags & TK_ALREADY_DEAD)) {
		Send_WM_DELETE_WINDOW(tkwin);
		evsent++;
	    }
	    _w = _w->next;
	}
	if (evsent > 0) {
	    return 1;
	}
	break;
    }
    }
    return 0;
}

void
SdlTkDecDrawFrame(_Window *_w)
{
    Drawable d = (Drawable) _w;
    const char *title = NULL;
    GC bgGC, lightGC, darkGC;
    int w, h;
    unsigned long savePixel;
    Uint32 titlePixel;
    int buttonX, buttonY, buttonW, buttonH;

    if (_w->dec == NULL) {
        Tcl_Panic("SdlTkRedrawFrame: not a decorative frame");
    }
    if ((_w->child == NULL) || (_w->child->tkwin == NULL)) {
	return;
    }

    if (_DFInfo.bgGC == None) {
	XGCValues values;

	values.graphics_exposures = False;
	values.foreground = SDL_MapRGB(SdlTkX.sdlsurf->format,
				       0xd9, 0xd9, 0xd9);
	values.background = SDL_MapRGB(SdlTkX.sdlsurf->format,
				       0x00, 0x00, 0x00);
	_DFInfo.bgGC =
	    XCreateGC(SdlTkX.display, SdlTkX.screen->root,
		      GCGraphicsExposures|GCForeground|GCBackground,
		      &values);
    }
    if (_DFInfo.lightGC == None) {
	XGCValues values;

	values.graphics_exposures = False;
	values.foreground = SDL_MapRGB(SdlTkX.sdlsurf->format,
				       0xff, 0xff, 0xff);
	values.background = SDL_MapRGB(SdlTkX.sdlsurf->format,
				       0x00, 0x00, 0x00);
	_DFInfo.lightGC =
	    XCreateGC(SdlTkX.display, SdlTkX.screen->root,
		      GCGraphicsExposures|GCForeground|GCBackground,
		      &values);
    }
    if (_DFInfo.darkGC == None) {
	XGCValues values;

	values.graphics_exposures = False;
	values.foreground = SDL_MapRGB(SdlTkX.sdlsurf->format,
				       0x82, 0x82, 0x82);
	values.background = SDL_MapRGB(SdlTkX.sdlsurf->format,
				       0x00, 0x00, 0x00);
	_DFInfo.darkGC =
	    XCreateGC(SdlTkX.display, SdlTkX.screen->root,
		      GCGraphicsExposures|GCForeground|GCBackground,
		      &values);
    }

    bgGC = _DFInfo.bgGC;
    lightGC = _DFInfo.lightGC;
    darkGC = _DFInfo.darkGC;

    w = _w->atts.width;
    h = _w->atts.height;

    /* top */
    SdlTkGfxFillRect(d, bgGC,
		     0, 0,
		     w, SdlTkX.dec_title_height);
    /* bottom */
    SdlTkGfxFillRect(d, bgGC,
		     0, h - SdlTkX.dec_frame_width,
		     w, SdlTkX.dec_frame_width);
    /* left */
    SdlTkGfxFillRect(d, bgGC,
		     0, 0,
		     SdlTkX.dec_frame_width, h);
    /* right */
    SdlTkGfxFillRect(d, bgGC,
		     w - SdlTkX.dec_frame_width, 0,
		     SdlTkX.dec_frame_width, h);

    if (_w->dec->active) {
	unsigned long fg = darkGC->foreground;

	darkGC->foreground = SDL_MapRGB(SdlTkX.sdlsurf->format,
					0x92, 0x92, 0x92);
	titlePixel = darkGC->foreground;
	/* top */
	SdlTkGfxFillRect(d, darkGC,
			 0, 0,
			 w, SdlTkX.dec_title_height - 1);
	/* bottom */
        SdlTkGfxFillRect(d, darkGC,
			 0, h - SdlTkX.dec_frame_width,
			 w, SdlTkX.dec_frame_width - 1);
	/* left */
        SdlTkGfxFillRect(d, darkGC,
			 0, 0,
			 SdlTkX.dec_frame_width - 1, h);
	/* right */
        SdlTkGfxFillRect(d, darkGC,
			 w - SdlTkX.dec_frame_width, 0,
			 SdlTkX.dec_frame_width + 2, h - 2);
	darkGC->foreground = fg;
    } else {
	titlePixel = bgGC->foreground;
    }

    /* XChangeProperty sets the title for the wrapper */
    title = _w->child->title;
    if (title != NULL) {
	XGCValues fakeGC;
	int lineHeight, x, n, bb[4];
	char *p;
	Tcl_Encoding encoding;
	Tcl_DString ds;

	if (_DFInfo.titlebar_font == NULL) {
	    char fontname[128];

	    sprintf(fontname, "-*-dejavu sans-normal-r-*-*-%d-*-*-*-*-*-*-*",
		    SdlTkX.dec_font_size);
	    _DFInfo.titlebar_font = XLoadQueryFont(SdlTkX.display, fontname);
	}
	if (_DFInfo.titlebar_font != NULL) {
	    lineHeight =
		_DFInfo.titlebar_font->ascent +_DFInfo.titlebar_font->descent;
	    fakeGC.font = _DFInfo.titlebar_font->fid;
	    fakeGC.foreground = SdlTkX.screen->black_pixel;
	    fakeGC.clip_mask = None;
	    fakeGC.stipple = None;
	    fakeGC.fill_style = FillSolid;
	    if (_w->dec->active) {
		fakeGC.foreground = lightGC->foreground;
	    }
	    encoding = Tcl_GetEncoding(NULL, "ucs-4");
	    Tcl_UtfToExternalDString(encoding, title, strlen(title), &ds);

	    n = Tcl_DStringLength(&ds) / sizeof (unsigned int);
	    p = Tcl_DStringValue(&ds);
	    GetButtonBounds(_w, DEC_BUTTON_CLOSE, bb, bb + 1, bb + 2, bb + 3);

	    x = SdlTkX.dec_frame_width * 2;
	    while (n-- > 0) {
		SdlTkGfxDrawString(d, &fakeGC, x,
		    1 + (SdlTkX.dec_title_height - lineHeight) / 2 +
		    _DFInfo.titlebar_font->ascent, p, sizeof (unsigned int),
		    0.0, &x, NULL);
		if (x > bb[0] - 2 * SdlTkX.dec_frame_width) {
		    break;
		}
		p += sizeof (unsigned int);
	    }

	    if (encoding) {
		Tcl_FreeEncoding(encoding);
	    }
	    Tcl_DStringFree(&ds);
	}
    }

    /* Close box */
    {
#ifdef ANDROID
	int lw2 = SdlTkX.dec_line_width + 2;
#endif
        XPoint points[2];

	GetButtonBounds(_w, DEC_BUTTON_CLOSE, &buttonX, &buttonY,
			&buttonW, &buttonH);

	/* Button background (erase 1 pixel outside) */
	savePixel = bgGC->foreground;
	bgGC->foreground = titlePixel;
	SdlTkGfxFillRect(d, bgGC, buttonX - 1, buttonY - 1, buttonW + 2,
			 buttonH + 2);
	bgGC->foreground = savePixel;

	if (_w->dec->button == DEC_BUTTON_CLOSE) {
	    savePixel = bgGC->foreground;
	    bgGC->foreground = SDL_MapRGB(SdlTkX.sdlsurf->format,
					  128 + 64 - _w->dec->pressed * 64,
					  0, 0);
	    SdlTkGfxFillRect(d, bgGC, buttonX, buttonY, buttonW, buttonH);
	    bgGC->foreground = savePixel;
	}

	SdlTkGfxDrawRect(d, lightGC, buttonX, buttonY, buttonW - 1,
			 buttonH - 1);

#ifdef ANDROID
	lightGC->line_width = SdlTkX.dec_line_width;

	points[0].x = buttonX + lw2;
	points[0].y = buttonY + lw2;
	points[1].x = buttonX + buttonW - lw2;
	points[1].y = buttonY + buttonH - lw2;
	SdlTkGfxDrawLines(d, lightGC, points, 2, CoordModeOrigin);

	points[0].x = buttonX + buttonW - lw2;
	points[0].y = buttonY + lw2;
	points[1].x = buttonX + lw2;
	points[1].y = buttonY + buttonH - lw2;
	SdlTkGfxDrawLines(d, lightGC, points, 2, CoordModeOrigin);
#else
	lightGC->line_width = 2;

	points[0].x = buttonX + 3;
	points[0].y = buttonY + 3;
	points[1].x = buttonX + buttonW - 3;
	points[1].y = buttonY + buttonH - 3;
	SdlTkGfxDrawLines(d, lightGC, points, 2, CoordModeOrigin);

	points[0].x = buttonX + buttonW - 3;
	points[0].y = buttonY + 3;
	points[1].x = buttonX + 3;
	points[1].y = buttonY + buttonH - 3;
	SdlTkGfxDrawLines(d, lightGC, points, 2, CoordModeOrigin);
#endif
	lightGC->line_width = 1;
    }

#ifdef MIN_MAX_BOXES
    /* Maximize box */
    {
        GetButtonBounds(_w, DEC_BUTTON_MAXIMIZE, &buttonX, &buttonY,
			&buttonW, &buttonH);

	/* Button background (erase 1 pixel outside) */
	savePixel = bgGC->foreground;
	bgGC->foreground = titlePixel;
	SdlTkGfxFillRect(d, bgGC, buttonX - 1, buttonY - 1,
			 buttonW + 2, buttonH + 2);
	bgGC->foreground = savePixel;

	if (_w->dec->button == DEC_BUTTON_MAXIMIZE) {
	    savePixel = bgGC->foreground;
	    bgGC->foreground = SDL_MapRGB(SdlTkX.sdlscreen->format, 0,
					  128 + 64 - _w->dec->pressed * 64, 0);
	    SdlTkGfxFillRect(d, bgGC, buttonX, buttonY, buttonW, buttonH);
	    bgGC->foreground = savePixel;
	}

	/* Outline */
	SdlTkGfxDrawRect(d, lightGC, buttonX, buttonY,
			 buttonW - 1, buttonH - 1);

	/* Symbol */
	SdlTkGfxDrawRect(d, lightGC, buttonX + 4, buttonY + 4,
			 buttonW - 9, buttonH - 9);
	SdlTkGfxFillRect(d, lightGC, buttonX + 4, buttonY + 5, buttonW - 8, 2);
    }

    /* Minimize box */
    {
        GetButtonBounds(_w, DEC_BUTTON_MINIMIZE, &buttonX, &buttonY,
			&buttonW, &buttonH);

	/* Button background (erase 1 pixel outside) */
	savePixel = bgGC->foreground;
	bgGC->foreground = titlePixel;
	SdlTkGfxFillRect(d, bgGC, buttonX - 1, buttonY - 1,
			 buttonW + 2, buttonH + 2);
	bgGC->foreground = savePixel;

	if (_w->dec->button == DEC_BUTTON_MINIMIZE) {
	    savePixel = bgGC->foreground;
	    bgGC->foreground = SDL_MapRGB(SdlTkX.sdlscreen->format, 0, 0,
					  255 - _w->dec->pressed * 64);
	    SdlTkGfxFillRect(d, bgGC, buttonX, buttonY, buttonW, buttonH);
	    bgGC->foreground = savePixel;
	}

	/* Outline */
	SdlTkGfxDrawRect(d, lightGC, buttonX, buttonY,
			 buttonW - 1, buttonH - 1);

	/* Symbol */
	SdlTkGfxFillRect(d, lightGC, buttonX + 4,
			 buttonY + buttonH - 7, buttonW - 8, 3);
    }
#endif

    /* outer highlight */
    SdlTkGfxFillRect(d, darkGC, w - 1, 0, 1, h);
    SdlTkGfxFillRect(d, darkGC, 0, h - 1, w, 1);
    SdlTkGfxFillRect(d, lightGC, 0, 0, 1, h);
    SdlTkGfxFillRect(d, lightGC, 0, 0, w, 1);

    /* inner highlight */
    SdlTkGfxFillRect(d, darkGC,
		     SdlTkX.dec_frame_width - 1,
		     SdlTkX.dec_title_height - 1,
		     SdlTkX.dec_frame_width - 1,
		     h - SdlTkX.dec_title_height - SdlTkX.dec_frame_width + 2);
    SdlTkGfxFillRect(d, darkGC,
		     SdlTkX.dec_frame_width - 1,
		     SdlTkX.dec_title_height - 1,
		     w - SdlTkX.dec_frame_width * 2 + 2,
		     1);
    SdlTkGfxFillRect(d, lightGC,
		     w - SdlTkX.dec_frame_width,
		     SdlTkX.dec_title_height - 1,
		     1,
		     h - SdlTkX.dec_title_height - SdlTkX.dec_frame_width + 2);
    SdlTkGfxFillRect(d, lightGC,
		     SdlTkX.dec_frame_width - 1,
		     h - SdlTkX.dec_frame_width,
		     w - SdlTkX.dec_frame_width * 2 + 2,
		     1);
}

int
SdlTkDecSetActive(_Window *_w, int active)
{
    if (active != -1) {
	_w->dec->active = active;
    }
    return _w->dec->active;
}

int
SdlTkDecSetDraw(_Window *_w, int draw)
{
    if (draw != -1) {
	_w->dec->draw = draw;
    }
    return _w->dec->draw;
}

void
SdlTkDecCreate(_Window *_w)
{
    _w->dec = (struct _DecFrame *) ckalloc(sizeof (struct _DecFrame));
    _w->dec->draw = 0;
    _w->dec->button = DEC_BUTTON_NONE;
    _w->dec->pressed = False;
    _w->dec->active = 0;
}

void
SdlTkDecDestroy(_Window *_w)
{
    ckfree((char *) _w->dec);
    _w->dec = NULL;

    if (_DFInfo.track_toplevel == _w) {
	_DFInfo.track_toplevel = NULL;
	_DFInfo.track_action = DEC_TRACK_NONE;
    }
    if (_DFInfo.motion_toplevel == _w) {
	_DFInfo.motion_toplevel = NULL;
	_DFInfo.motion_button = DEC_BUTTON_NONE;
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * tab-width: 8
 * End:
 */
