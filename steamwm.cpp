
#if 0

# Add window frames and title bars to steam and fix resulting issues:
# // Fix steam's mouse recognition being offset by the frame dimensions
# // Change the window titles to use the full window name
# // Group all steam windows to help with focus stealing prevention
#
# Just adding the window frames kan be done with KWin rules, but the rest needs steam-
# specific hacks.
#
# Requires: g++ with support for x86 targets, Xlib headers
#
# Use:
# $ chmod +x steamwm.cpp
# $ DEBUGGER="$(pwd)/steamwm.cpp" steam
#
# Or if you prefer:
# $ chmod +x steamwm.cpp
# $ ./steamwm.cpp                          // Compile
# $ LD_PRELOAD="$(pwd)/steamwm.so" steam   // don't mind the ld.so errors on 64-bit systems
#
# DISCLAIMER: Use at your own risk! This is in no way endorsed by VALVE.
#

self="$(readlink -f "$(which "$0")")"
out="$(dirname "$self")/$(basename "$self" .cpp).so"

if [ "$self" -nt "$out" ] ; then
	echo "Compiling $(basename "$out")..."
	g++ -shared -fPIC -m32 "$self" -o "$out" \
	    -lX11 -static-libgcc -static-libstdc++ \
	    -O3 -Wall -Wextra -x c++ \
		|| exit 1
fi

export OLD_LD_PRELOAD="$LD_PRELOAD"
export LD_PRELOAD="$out:$LD_PRELOAD"

[ -z "$1" ] || exec "$@"

exit

#endif

/* This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */ 


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>


static bool active = false;

extern "C" {
extern char * program_invocation_short_name; // provided by glibc
}

void steamwm_init(void) __attribute__((constructor));
void steamwm_init(void) {
	
	if(strcmp(program_invocation_short_name, "steam") != 0) {
		// only attach to steam
		return;
	}
	
	if(char * old_preload = getenv("OLD_LD_PRELOAD")) {
		// avoid loading this library for child processes
		setenv("LD_PRELOAD", old_preload, 1);
	}
	
	active = true;
}


#define STR_(x) # x
#define STR(x)  STR_(x)
#define BASE_NAME(name) base_ ## name
#define TYPE_NAME(name) name ## _t
#define REIMPLEMENT(return, name, ...) \
	typedef return (*TYPE_NAME(name))(__VA_ARGS__); \
	static void * const BASE_NAME(name) = dlsym(RTLD_NEXT, STR(name)); \
	return name(__VA_ARGS__)
#define BASE(name) ((TYPE_NAME(name))BASE_NAME(name))


REIMPLEMENT(int, XChangeProperty,
	Display *             display,
	Window                w,
	Atom                  property,
	Atom                  type,
	int                   format,
	int                   mode,
	const unsigned char * data,
	int                   n
) {
	if(active) {
		if(property == XA_WM_NAME) {
			// Use the XA_WM_NAME as both XA_WM_NAME and _NET_WM_NAME
			// Steam sets _NET_WM_NAME to just "Steam" for all windows
			Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
			Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
			BASE(XChangeProperty)(display, w, net_wm_name, utf8_string, format, mode, data, n);
		} else if(property == XInternAtom(display, "_MOTIF_WM_HINTS", False)) {
			/*
			* Don't suppress window borders!
			*
			* However, steam expects the window to be positioned at the exact position
			* specified in XMoveResizeWindow() requests and will spam new requests until
			* the window is that that positition (and ignore any other positions).
			* At least under KDE the client window will be slightly offset from the request
			* if it has borders - but the steam behavior is broken anyway.
			*
			* This has the effect that steam will continuously reset the window pos until the
			* windo is first moved from inside steam.
			* Also, the mouse cursor is offset by the window fram dimensions.
			*
			* To fix it we remember the requested coordinates and return them for the next
			* (and only the next) XTranslateCoordinates() request that has the moved window as
			* the source and a root window as the target.
			* This gets steam to calm donw and let the WM do it's thing.
			*/
			return 1;
		}
	}
	return BASE(XChangeProperty)(display, w, property, type, format, mode, data, n);
}


/* try to guess the main window and set WM_TRANSIENT_FOR hints */

REIMPLEMENT(int, XMapWindow,
	Display * display,
	Window    w
) {
	if(active) {
		// Group all steam windows
		static Window main_window = w;
		Atom leader = XInternAtom(display, "WM_CLIENT_LEADER", False);
		unsigned char data[sizeof(long)];
		long value = main_window;
		memcpy(&data, &value, sizeof(long));
		BASE(XChangeProperty)(display, w, leader, XA_ATOM, 32, PropModeReplace, data, 1);
		XWMHints base_hints;
		XWMHints * h = XGetWMHints(display, w);
		if(!h) {
			h = &base_hints;
			h->flags = 0;
		}
		h->flags |= WindowGroupHint;
		h->window_group = main_window;
		XSetWMHints(display, w, h);
		if(h != &base_hints) {
			XFree(h);
		}
	}
	return BASE(XMapWindow)(display, w);
}


/* remember requested window positions */

typedef struct requested_pos {
	Display * display;
	Window w;
	int x;
	int y;
	struct requested_pos * next;
} requested_pos;

static pthread_rwlock_t requested_pos_lock = PTHREAD_RWLOCK_INITIALIZER;
static requested_pos * head = NULL;

static requested_pos * get_requested_pos(Display * display, Window w) {
	requested_pos * p = head;
	while(p) {
		if(p->display == display && p->w == w) {
			return p;
		}
		p = p->next;
	}
	return NULL;
}

static void set_requested_pos(Display * display, Window w, int x, int y) {
	pthread_rwlock_wrlock(&requested_pos_lock);
	requested_pos * pos = get_requested_pos(display, w);
	if(!pos) {
		pos = (requested_pos *)malloc(sizeof(requested_pos));
		pos->display = display;
		pos->w = w;
		pos->next = head;
		head = pos;
	}
	pos->x = x;
	pos->y = y;
	pthread_rwlock_unlock(&requested_pos_lock);
}

static void clean_requested_pos_(Display * display, Window w) {
	pthread_rwlock_wrlock(&requested_pos_lock);
	requested_pos * p = head, * prev = NULL;
	while(p) {
		if(p->display == display && p->w == w) {
			if(prev) {
				prev->next = p->next;
			} else {
				head = p->next;
			}
			free(p);
			break;
		}
		prev = p, p = p->next;
	}
	pthread_rwlock_unlock(&requested_pos_lock);
}

static void clean_requested_pos(Display * display, Window w) {
	pthread_rwlock_rdlock(&requested_pos_lock);
	bool abort = !get_requested_pos(display, w);
	pthread_rwlock_unlock(&requested_pos_lock);
	if(abort) {
		return;
	}
	clean_requested_pos_(display, w);
}

REIMPLEMENT(int, XMoveResizeWindow,
	Display *    display,
	Window       w,
	int          x,
	int          y,
	unsigned int width,
	unsigned int height
) {
	if(active) {
		set_requested_pos(display, w, x, y);
	}
	return BASE(XMoveResizeWindow)(display, w, x, y, width, height);
}


/* use the saved position to adjust queries */

static bool is_root_window(Display * display, Window w) {
	for(int scr = 0; scr < ScreenCount(display); scr++) {
		if(RootWindow(display, scr) == w) {
			return true;
		}
	}
	return false;
}

REIMPLEMENT(Bool, XTranslateCoordinates,
	Display * display,
	Window    src_w,
	Window    dest_w,
	int       src_x,
	int       src_y,
	int *     dest_x_return,
	int *     dest_y_return,
	Window *  child_return
) {
	Bool ret = BASE(XTranslateCoordinates)(display, src_w, dest_w, src_x, src_y,
	                                       dest_x_return, dest_y_return, child_return);
	if(active && is_root_window(display, dest_w)) {
		bool clean = false;
		pthread_rwlock_rdlock(&requested_pos_lock);
		if(requested_pos * pos = get_requested_pos(display, src_w)) {
			// return the position we remembered earlier
			*dest_x_return = src_x + pos->x;
			*dest_y_return = src_y + pos->y;
			clean = true;
		}
		pthread_rwlock_unlock(&requested_pos_lock);
		if(clean) {
			// use the actual window position for further queries
			clean_requested_pos_(display, src_w);
		}
	}
	return ret;
}


/* cleanup requested positions that are no longer needed */

REIMPLEMENT(int, XDestroyWindow,
	Display * display,
	Window    w
) {
	int ret = BASE(XDestroyWindow)(display, w);
	if(active) {
		clean_requested_pos(display, w);
	}
	return ret;
}

static void handle_event(XEvent * event) {
	if(active && event && event->type == DestroyNotify) {
		clean_requested_pos(event->xany.display, event->xany.window);
	}
}

typedef Bool (*XEvent_predicate_t)(Display *, XEvent *, XPointer);

REIMPLEMENT(Bool, XCheckIfEvent,
	Display *          display,
	XEvent *           event_return,
	XEvent_predicate_t predicate,
	XPointer           arg
) {
	Bool ret = BASE(XCheckIfEvent)(display, event_return, predicate, arg);
	if(ret == True) { handle_event(event_return); }
	return ret;
}

REIMPLEMENT(Bool, XCheckMaskEvent,
	Display * display,
	long      event_mask,
	XEvent *  event_return
) {
	Bool ret = BASE(XCheckMaskEvent)(display, event_mask, event_return);
	if(ret == True) { handle_event(event_return); };
	return ret;
}

REIMPLEMENT(Bool, XCheckTypedEvent,
	Display * display,
	int       event_type,
	XEvent *  event_return
) {
	Bool ret = BASE(XCheckTypedEvent)(display, event_type, event_return);
	if(ret == True) { handle_event(event_return); };
	return ret;
}

REIMPLEMENT(Bool, XCheckTypedWindowEvent,
	Display * display,
	Window    w,
	int       event_type,
	XEvent *  event_return
) {
	Bool ret = BASE(XCheckTypedWindowEvent)(display, w, event_type, event_return);
	if(ret == True) { handle_event(event_return); };
	return ret;
}

REIMPLEMENT(Bool, XCheckWindowEvent,
	Display * display,
	Window    w,
	long      event_mask,
	XEvent *  event_return
) {
	Bool ret = BASE(XCheckWindowEvent)(display, w, event_mask, event_return);
	if(ret == True) { handle_event(event_return); };
	return ret;
}

REIMPLEMENT(int, XIfEvent,
	Display *          display,
	XEvent *           event_return,
	XEvent_predicate_t predicate,
	XPointer           arg
) {
	int ret = BASE(XIfEvent)(display, event_return, predicate, arg);
	handle_event(event_return);
	return ret;
}

REIMPLEMENT(int, XMaskEvent,
	Display * display,
	long      event_mask,
	XEvent *  event_return
) {
	int ret = BASE(XMaskEvent)(display, event_mask, event_return);
	handle_event(event_return);
	return ret;
}

REIMPLEMENT(int, XWindowEvent,
	Display * display,
	Window    w,
	long      event_mask,
	XEvent *  event_return
) {
	int ret = BASE(XWindowEvent)(display, w, event_mask, event_return);
	handle_event(event_return);
	return ret;
}

REIMPLEMENT(int, XNextEvent,
	Display * display,
	XEvent *  event_return
) {
	int ret = BASE(XNextEvent)(display, event_return);
	handle_event(event_return);
	return ret;
}
