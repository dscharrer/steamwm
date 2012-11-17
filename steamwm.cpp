
#if 0

#// Various window management fixes for the Linux Steam client.
#//
#// You can set the following environment variables to 0 to disable individual features:
#//
#// STEAMWM_FORCE_BORDERS    Force borders on non-menu windows.
#//
#// STEAMWM_PREVENT_MOVE     Let the WM position non-menu/tooltip windows.
#//
#// STEAMWM_FIX_NET_WM_NAME  Set _NET_WM_NAME to the WM_NAME value to get better window
#//                          titles (and add " - Steam" suffix if needed).
#//
#// STEAMWM_GROUP_WINDOWS    Group all steam windows.
#//                          This helps WMs with their focus stealing preventions,
#//                          and also prevents all Steam windows from being dimmed
#//                          (by KWin) if any Steam window has focus (is a KWin setting).
#//                          NOTE: Window is still dimmed when showing menus/tooltips :(
#//
#// STEAMWM_SET_WINDOW_TYPE  Tell the WM which Steam windows are dialogs.
#//                          This lets the window manager place them more intelligently.
#//                          For example, the WM might center dialogs.
#//                          NOTE: We simply treat every window with a title other than
#//                                "Steam" or "Friends" as a dialog window.
#//                                The startup window is also marked as a dialog.
#//
#// STEAMWM_SET_FIXED_SIZE   Set fixed size hints for windows with a fixed layout.
#//
#// STEAMWM_MANAGE_ERRORS    Steam sets error dialogs as unmanaged windows - fix that.
#//
#//
#// Requires: g++ with support for x86 targets, Xlib + headers
#//
#//
#// Use:
#// $ chmod +x steamwm.cpp
#// and then
#//
#//
#// $ DEBUGGER="$(pwd)/steamwm.cpp" steam
#//
#// *or*
#//
#// $ ./steamwm.cpp steam                    // Prints ld.so errors on 64-bit systems
#//
#// *or*
#//
#// $ ./steamwm.cpp                          // Compile
#// $ LD_PRELOAD="$(pwd)/steamwm.so" steam   // Prints ld.so errors on 64-bit systems
#//
#//
#// DISCLAIMER: Use at your own risk! This is in no way endorsed by VALVE.
#//
#// This program is free software. It comes without any warranty, to
#// the extent permitted by applicable law. You can redistribute it
#// and/or modify it under the terms of the Do What The Fuck You Want
#// To Public License, Version 2, as published by Sam Hocevar. See
#// http://sam.zoy.org/wtfpl/COPYING for more details. */ 
#//


[ -z $STEAMWM_FORCE_BORDERS   ] && export STEAMWM_FORCE_BORDERS=1
[ -z $STEAMWM_PREVENT_MOVE    ] && export STEAMWM_PREVENT_MOVE=1
[ -z $STEAMWM_FIX_NET_WM_NAME ] && export STEAMWM_FIX_NET_WM_NAME=1
[ -z $STEAMWM_GROUP_WINDOWS   ] && export STEAMWM_GROUP_WINDOWS=1
[ -z $STEAMWM_SET_WINDOW_TYPE ] && export STEAMWM_SET_WINDOW_TYPE=1
[ -z $STEAMWM_SET_FIXED_SIZE  ] && export STEAMWM_SET_FIXED_SIZE=1
[ -z $STEAMWM_MANAGE_ERRORS   ] && export STEAMWM_MANAGE_ERRORS=1


self="$(readlink -f "$(which "$0")")"
out="$(dirname "$self")/$(basename "$self" .cpp).so"


#// Compile the LD_PRELOAD library
if [ "$self" -nt "$out" ] ; then
	echo "Compiling $(basename "$out")..."
	g++ -shared -fPIC -m32 "$self" -o "$out" \
	    -lX11 -static-libgcc -static-libstdc++ \
	    -O3 -Wall -Wextra -x c++ \
		|| exit 1
fi


#// Run the executable
export LD_PRELOAD="$out:$LD_PRELOAD"
[ -z "$1" ] || exec "$@"


exit

#endif // 0


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>


// List of window titles for windows that should not be marked as dialogs
static const char * main_windows[] = {
	"Steam",
	"Friends",
};
static const char * fixed_size_windows[] = {
	"Settings",
	"About Steam",
	"Backup and Restore Programs",
};
static const char * fixed_size_suffixes[] = {
	" - Properties",
	" - Category",
};


static bool force_borders = false;
static bool prevent_move = false;
static bool fix_net_wm_name = false;
static bool group_windows = false;
static bool set_window_type = false;
static bool set_fixed_size = false;
static bool manage_errors = false;

extern "C" {
extern char * program_invocation_short_name; // provided by glibc
}

static bool get_setting(const char * name) {
	char * setting = getenv(name);
	return (setting && setting[0] != '\0' && setting[0] != '0');
}

void steamwm_init(void) __attribute__((constructor));
void steamwm_init(void) {
	
	// Only attach to steam!
	if(strcmp(program_invocation_short_name, "steam") != 0) {
		return;
	}
	
	force_borders   = get_setting("STEAMWM_FORCE_BORDERS");
	prevent_move    = get_setting("STEAMWM_PREVENT_MOVE");
	fix_net_wm_name = get_setting("STEAMWM_FIX_NET_WM_NAME");
	group_windows   = get_setting("STEAMWM_GROUP_WINDOWS");
	set_window_type = get_setting("STEAMWM_SET_WINDOW_TYPE");
	set_fixed_size  = get_setting("STEAMWM_SET_FIXED_SIZE");
	manage_errors   = get_setting("STEAMWM_MANAGE_ERRORS");
	
	
	fprintf(stderr, "\n[steamwm] attached to steam:\n force_borders     %d\n"
	                " prevent_move      %d\n fix_net_wm_name   %d\n"
	                " group_windows     %d\n set_window_type   %d\n"
	                " set_fixed_size    %d\n manage_errors     %d\n\n",
	                int(force_borders), int(prevent_move), int(fix_net_wm_name),
	                int(group_windows), int(set_window_type), int(set_fixed_size),
	                int(manage_errors));
	
}


/* helper functions */

#define STR_(x) # x
#define STR(x)  STR_(x)
#define BASE_NAME(name) base_ ## name
#define TYPE_NAME(name) name ## _t
#define INTERCEPT(return, name, ...) \
	typedef return (*TYPE_NAME(name))(__VA_ARGS__); \
	static void * const BASE_NAME(name) = dlsym(RTLD_NEXT, STR(name)); \
	return name(__VA_ARGS__)
#define BASE(name) ((TYPE_NAME(name))BASE_NAME(name))

static bool is_unmanaged_window(Display * dpy, Window w);
static void set_is_unmanaged_window(Display * dpy, Window w, bool is_unmanaged);
static bool is_fixed_size_window_name(const char * name);
static bool is_main_window_name(const char * name);
static void set_window_desired_size(Display * dpy, Window w, int width, int height,
                                    bool set_fixed);
static void set_window_property(Display * dpy, Window w, Atom property, Atom type,
                                long value);
static void set_window_group_hint(Display * dpy, Window w, XID window_group);
static void set_window_is_dialog(Display * dpy, Window w, bool is_dialog);
static void set_window_modal(Display * dpy, Window w);


/* fix window titles and types, and add window borders & title bars */

static Window first_window = None, second_window = None;

INTERCEPT(int, XChangeProperty,
	Display *             dpy,
	Window                w,
	Atom                  property,
	Atom                  type,
	int                   format,
	int                   mode,
	const unsigned char * data,
	int                   n
) {
	
	if(property == XA_WM_NAME && format == 8) {
		
		char * value = (char *)data;
		
		if(fix_net_wm_name) {
			// Use the XA_WM_NAME as both XA_WM_NAME and _NET_WM_NAME.
			// Steam sets _NET_WM_NAME to just "Steam" for all windows.
			const unsigned char * name = data;
			unsigned char * buffer = NULL;
			int nn = n;
			if(n > 0 && strstr((char *)data, "Steam") == 0) {
				// Make sure "Steam" is in all window titles.
				char suffix[] = " - Steam";
				nn = n + sizeof(suffix) - 1;
				name = buffer = (unsigned char *)malloc(nn + 1);
				memcpy(buffer, data, n);
				memcpy(buffer + n, suffix, sizeof(suffix));
			}
			Atom net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
			Atom utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
			BASE(XChangeProperty)(dpy, w, net_wm_name, utf8_string, format, mode, name, nn);
			if(buffer) {
				free(buffer);
			}
		}
		
		if(manage_errors && is_unmanaged_window(dpy, w) && strcmp(value, "Steam") != 0) {
			// Error dialogs should be managed by the window manager.
			set_is_unmanaged_window(dpy, w, false);
			set_window_modal(dpy, w);
			set_window_desired_size(dpy, w, -1, -1, true);
		}
		
		if(set_window_type && !is_unmanaged_window(dpy, w)
		   && w != first_window && w != second_window) {
			// Set the window type for non-menu windows.
			// This should probably be done *before* mapping the windows,
			// but the we don't have a title yet.
			// Try to guess the window type from the title.
			set_window_is_dialog(dpy, w, !is_main_window_name(value));
		}
		
		if(set_fixed_size && is_fixed_size_window_name(value)) {
			// Set fixed size hints for windows with static layouts.
			set_window_desired_size(dpy, w, -1, -1, true);
		}
		
	}
	
	if(manage_errors && property == XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False)) {
		if(!is_unmanaged_window(dpy, w)) {
			// Ignore the window type Steam sets on error dialogs.
			return 1;
		}
	}
	
	if(force_borders && property == XInternAtom(dpy, "_MOTIF_WM_HINTS", False)) {
		// Don't suppress window borders!
		return 1;
	}
	
	return BASE(XChangeProperty)(dpy, w, property, type, format, mode, data, n);
}


/* ignore window move requests for non-menu windows */

INTERCEPT(int, XResizeWindow,
	Display *    dpy,
	Window       w,
	unsigned int width,
	unsigned int height
) {
	
	if(set_fixed_size) {
		// Set fixed size hints for windows with static layouts.
		set_window_desired_size(dpy, w, width, height, false);
	}
	
	return BASE(XResizeWindow)(dpy, w, width, height);
}

INTERCEPT(int, XMoveResizeWindow,
	Display *    dpy,
	Window       w,
	int          x,
	int          y,
	unsigned int width,
	unsigned int height
) {
	
	if(set_fixed_size) {
		// Set fixed size hints for windows with static layouts.
		set_window_desired_size(dpy, w, width, height, false);
	}
	
	if(prevent_move && !is_unmanaged_window(dpy, w)) {
		// Ignore the position request for non-menu windows.
		return BASE(XResizeWindow)(dpy, w, width, height);
	}
	
	return BASE(XMoveResizeWindow)(dpy, w, x, y, width, height);
}

INTERCEPT(int, XMoveWindow,
	Display *    dpy,
	Window       w,
	int          x,
	int          y
) {
	
	if(prevent_move && !is_unmanaged_window(dpy, w)) {
		// Ignore the position request for non-menu windows.
		return 1;
	}
	
	return BASE(XMoveWindow)(dpy, w, x, y);
}


/* group windows and force the first and second window to be dialogs */

INTERCEPT(int, XMapWindow,
	Display * dpy,
	Window    w
) {
	
	if(first_window == None) {
		first_window = w;
	}
	
	if(group_windows) {
		// Group all steam windows.
		Atom leader = XInternAtom(dpy, "WM_CLIENT_LEADER", False);
		set_window_property(dpy, w, leader, XA_WINDOW, first_window);
		set_window_group_hint(dpy, w, first_window);
	}
	
	if(set_window_type && (w == first_window || second_window == None)) {
		// Force the first and second windows to be marked as dialogs.
		set_window_is_dialog(dpy, w, true);
		if(w != first_window) {
			// Give the second window a proper size *now* so that the WM can center it.
			second_window = w;
			XResizeWindow(dpy, w, 384, 107);
		}
	}
	
	return BASE(XMapWindow)(dpy, w);
}


/* helper function implementations */

static bool is_unmanaged_window(Display * dpy, Window w) {
	XWindowAttributes xwa;
	if(!XGetWindowAttributes(dpy, w, &xwa)) {
		return false;
	}
	return xwa.override_redirect;
}

static void set_is_unmanaged_window(Display * dpy, Window w, bool is_unmanaged) {
	XSetWindowAttributes xswa;
	xswa.override_redirect = is_unmanaged;
	XChangeWindowAttributes(dpy, w, CWOverrideRedirect, &xswa);
}

static bool is_main_window_name(const char * name) {
	
	for(unsigned i = 0; i < sizeof(main_windows)/sizeof(*main_windows); i++) {
		if(strcmp(name, main_windows[i]) == 0) {
			return true;
		}
	}
	
	return false;
}

static bool is_fixed_size_window_name(const char * name) {
	
	for(unsigned i = 0; i < sizeof(fixed_size_windows)/sizeof(*fixed_size_windows); i++) {
		if(strcmp(name, fixed_size_windows[i]) == 0) {
			return true;
		}
	}
	
	int len = strlen(name);
	for(unsigned i = 0; i < sizeof(fixed_size_suffixes)/sizeof(*fixed_size_suffixes); i++) {
		int plen = strlen(fixed_size_suffixes[i]);
		if(len > plen && strcmp(name + len - plen, fixed_size_suffixes[i]) == 0) {
			return true;
		}
	}
	
	return false;
}

static void set_window_desired_size(Display * dpy, Window w, int width, int height,
                                    bool set_fixed) {
	XSizeHints xsh;
	long supplied;
	if(!XGetWMNormalHints(dpy, w, &xsh, &supplied)) {
		xsh.flags = 0;
	}
	if(width > 0 && height > 0) {
		// Store the desired size.
		xsh.flags |= PBaseSize;
		xsh.base_width = width, xsh.base_height = height;
	} else if(xsh.flags & PBaseSize) {
		// Retrieve the desired size.
		width = xsh.base_width, height = xsh.base_height;
	} else {
		Window root;
		int x, y;
		unsigned int cwidth, cheight, border_width, depth;
		if(!XGetGeometry(dpy, w, &root, &x, &y, &cwidth, &cheight, &border_width, &depth)) {
			return;
		}
		width = cwidth, height = cheight;
	}
	if(set_fixed || (xsh.flags & (PMinSize | PMaxSize))) {
		xsh.flags |= PMinSize | PMaxSize;
		xsh.min_width = xsh.max_width = width;
		xsh.min_height = xsh.max_height = height;
	}
	XSetWMNormalHints(dpy, w, &xsh);
}

static void set_window_property(Display * dpy, Window w, Atom property, Atom type,
                                long value) {
	unsigned char data[sizeof(long)];
	memcpy(&data, &value, sizeof(long));
	BASE(XChangeProperty)(dpy, w, property, type, 32, PropModeReplace, data, 1);
}

static void set_window_group_hint(Display * dpy, Window w, XID window_group) {
	XWMHints base_hints;
	XWMHints * h = XGetWMHints(dpy, w);
	if(!h) {
		h = &base_hints;
		h->flags = 0;
	}
	h->flags |= WindowGroupHint;
	h->window_group = window_group;
	XSetWMHints(dpy, w, h);
	if(h != &base_hints) {
		XFree(h);
	}
}

static void set_window_is_dialog(Display * dpy, Window w, bool is_dialog) {
	Atom window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	if(is_dialog) {
		Atom dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
		set_window_property(dpy, w, window_type, XA_ATOM, dialog);
	} else {
		Atom normal = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
		set_window_property(dpy, w, window_type, XA_ATOM, normal);
	}
}

static void set_window_modal(Display * dpy, Window w) {
	XWindowAttributes xwa;
	if(!XGetWindowAttributes(dpy, w, &xwa)) {
		return;
	}
	Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
	Atom state_modal = XInternAtom(dpy, "_NET_WM_STATE_MODAL", False);
	if(xwa.map_state == IsUnmapped) {
		set_window_property(dpy, w, state, XA_ATOM, state_modal);
	} else {
		XEvent event;
		event.type = ClientMessage;
		event.xclient.message_type = state;
		event.xclient.window = w;
		event.xclient.format = 32;
		event.xclient.data.l[0] = 1; // add
		event.xclient.data.l[1] = state_modal;
		event.xclient.data.l[2] = 0;
		event.xclient.data.l[3] = 1;
		event.xclient.data.l[4] = 0;
		XSendEvent(dpy, DefaultRootWindow(dpy), False,
		           (SubstructureNotifyMask | SubstructureRedirectMask), &event);
	}
}
