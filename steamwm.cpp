#if 0

#// Various window management fixes for the Linux Steam client.
#//
#// You can set the following environment variables to 0 to disable individual features:
#//
#// STEAMWM_FORCE_BORDERS    Force borders on non-menu windows.
#//
#// STEAMWM_PREVENT_MOVE     Let the WM position non-menu/tooltip windows.
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
#// STEAMWM_MANAGE_ERRORS    Steam sets error dialogs as unmanaged windows - fix that.
#//
#//
#// Obsolete fixes (now disabled by default):
#//
#// STEAMWM_FIX_NET_WM_NAME  Set _NET_WM_NAME to the WM_NAME value to get better window
#//                          titles (and add " - Steam" suffix if needed).
#//                          Steam now doesn't set WM_ICON_NAME, _NET_WM_NAME or
#//                          _NET_WM_ICON_NAME anymore - while it would be better to set
#//                          them, their absence is unlikely to cause problems.
#//
#// STEAMWM_SET_FIXED_SIZE   Set fixed size hints for windows with a fixed layout.
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
#// http://sam.zoy.org/wtfpl/COPYING for more details.
#//


[ -z $STEAMWM_FORCE_BORDERS   ] && export STEAMWM_FORCE_BORDERS=1
[ -z $STEAMWM_PREVENT_MOVE    ] && export STEAMWM_PREVENT_MOVE=1
[ -z $STEAMWM_FIX_NET_WM_NAME ] && export STEAMWM_FIX_NET_WM_NAME=0
[ -z $STEAMWM_GROUP_WINDOWS   ] && export STEAMWM_GROUP_WINDOWS=1
[ -z $STEAMWM_SET_WINDOW_TYPE ] && export STEAMWM_SET_WINDOW_TYPE=1
[ -z $STEAMWM_SET_FIXED_SIZE  ] && export STEAMWM_SET_FIXED_SIZE=0
[ -z $STEAMWM_MANAGE_ERRORS   ] && export STEAMWM_MANAGE_ERRORS=1


self="$(readlink -f "$(which "$0")")"
name="$(basename "$self" .cpp)"
out="$(dirname "$self")/$name"
soname="$name.so"


#// On amd64 platforms, compile a dummy 64-bit steamwm.so,
#// so that native 64-bit tools invoked by Steam and its
#// launch script won't spam (harmless) ld.so errors.
if [ -f '/lib64/ld-linux-x86-64.so.2' ] ; then
	dout="$out/64"
	mkdir -p "$dout"
	if ! [ -f "$dout/$soname" ] ; then
		echo -e "\n" | gcc -shared -fPIC -m64 -x c - -o "$dout/$soname" &> /dev/null
		strip "$dout/$soname" &> /dev/null
		#// ignore all errors - this may at worst cause warnings later
	fi
	export LD_LIBRARY_PATH="$dout:$LD_LIBRARY_PATH"
fi


#// Compile the LD_PRELOAD library
mkdir -p "$out"
if [ "$self" -nt "$out/$soname" ] ; then
	echo "Compiling $soname..."
	g++ -shared -fPIC -m32 -x c++ "$self" -o "$out/$soname" \
	    -lX11 -static-libgcc -static-libstdc++ \
	    -O3 -Wall -Wextra \
		|| exit 1
fi


#// Run the executable
export LD_PRELOAD="$soname:$LD_PRELOAD"
export LD_LIBRARY_PATH="$out:$LD_LIBRARY_PATH"
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
// List of window titles for windows that should be dialogs even if unmanaged
static const char * dialog_windows[] = {
	"Untitled",
	"Steam - Go Online",
};
static const char * fixed_size_windows[] = {
	"Settings",
	"About Steam",
	"Backup and Restore Programs",
	"Steam - Error",
	"Steam - Self Updater",
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
#define BASE_NAME(SymbolName) base_ ## SymbolName
#define TYPE_NAME(SymbolName) SymbolName ## _t
#define INTERCEPT(ReturnType, SymbolName, ...) \
	typedef ReturnType (*TYPE_NAME(SymbolName))(__VA_ARGS__); \
	static void * const BASE_NAME(SymbolName) = dlsym(RTLD_NEXT, STR(SymbolName)); \
	ReturnType SymbolName(__VA_ARGS__)
#define BASE(SymbolName) ((TYPE_NAME(SymbolName))BASE_NAME(SymbolName))

static bool is_unmanaged_window(Display * dpy, Window w);
static void set_is_unmanaged_window(Display * dpy, Window w, bool is_unmanaged);
static bool is_fixed_size_window_name(const char * name);
static bool is_main_window_name(const char * name);
static bool is_dialog_window_name(const char * name);
static void set_window_desired_size(Display * dpy, Window w, int width, int height,
                                    bool set_fixed);
static void set_window_property(Display * dpy, Window w, Atom property, Atom type,
                                long value);
static void set_window_group_hint(Display * dpy, Window w, XID window_group);
static void set_window_is_dialog(Display * dpy, Window w, bool is_dialog);
static void set_window_modal(Display * dpy, Window w);


/* fix window titles and types, and add window borders & title bars */

static Window first_window = None, second_window = None;

static void name_changed(Display * dpy, Window w, const unsigned char * data, int n);

INTERCEPT(void, XSetWMName,
	Display *       dpy,
	Window          w,
	XTextProperty * prop
) {
	
	if(prop->format == 8) {
		// The libX11 pulled in with STEAM_RUNTIME=1 has XSetWMName (or XSetTextProperty?)
		// liked/optimized to use internal functions and not the global XChangeProperty,
		// so our override below won't work -> also intercept XSetWMName().
		name_changed(dpy, w, prop->value, prop->nitems);
	}
	
	return BASE(XSetWMName)(dpy, w, prop);
}

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
		name_changed(dpy, w, data, n);
	}
	
	if(manage_errors && property == XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False)) {
		if(!is_unmanaged_window(dpy, w) && type == XA_ATOM && format == 32 && n > 0) {
			Atom menu = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_MENU", False);
			Atom type = reinterpret_cast<const long * /* sic */>(data)[0];
			if(type == menu) {
				// Ignore the window type Steam sets on error dialogs.
				return 1;
			}
		}
	}
	
	if(force_borders && property == XInternAtom(dpy, "_MOTIF_WM_HINTS", False)) {
		// Don't suppress window borders!
		return 1;
	}
	
	return BASE(XChangeProperty)(dpy, w, property, type, format, mode, data, n);
}

static void name_changed(Display * dpy, Window w, const unsigned char * data, int n) {
	
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
		BASE(XChangeProperty)(dpy, w, net_wm_name, utf8_string, 8, PropModeReplace, name, nn);
		if(buffer) {
			free(buffer);
		}
	}
	
	if(manage_errors && is_unmanaged_window(dpy, w) && is_dialog_window_name(value)) {
		// Error dialogs should be managed by the window manager.
		set_is_unmanaged_window(dpy, w, false);
		set_window_modal(dpy, w);
		set_window_desired_size(dpy, w, -1, -1, true);
	}
	
	if(set_window_type && !is_unmanaged_window(dpy, w)
	   && w != first_window && w != second_window) {
		// Set the window type for non-menu windows.
		// This should probably be done *before* mapping the windows,
		// but then we don't have a title yet.
		// Try to guess the window type from the title.
		set_window_is_dialog(dpy, w, !is_main_window_name(value));
	}
	
	if(set_fixed_size && is_fixed_size_window_name(value)) {
		// Set fixed size hints for windows with static layouts.
		set_window_desired_size(dpy, w, -1, -1, true);
	}
	
}


/* ignore window move requests for non-menu windows */

INTERCEPT(int, XResizeWindow,
	Display *    dpy,
	Window       w,
	unsigned int width,
	unsigned int height
) {
	
	if(set_fixed_size || manage_errors) {
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
	
	if(set_fixed_size || manage_errors) {
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

INTERCEPT(void, XSetWMNormalHints,
	Display *    dpy,
	Window       w,
	XSizeHints * hints
) {
	XSizeHints old_hints;
	long supplied;
	// Prevent steam from overriding the max size hints
	if((set_fixed_size || manage_errors)
	   && XGetWMNormalHints(dpy, w, &old_hints, &supplied)) {
		if(!(hints->flags & PSize) && (old_hints.flags & PSize)) {
			hints->flags |= PSize;
			hints->width  = old_hints.width;
			hints->height = old_hints.height;
		}
		if((old_hints.flags & PMinSize) && (old_hints.flags & PMaxSize)
		   && old_hints.min_width == old_hints.max_width
		   && old_hints.min_height == old_hints.max_height) {
			hints->flags |= PMinSize | PMaxSize;
			hints->min_width  = hints->max_width  = old_hints.max_width;
			hints->min_height = hints->max_height = old_hints.max_height;
		}
	}
	BASE(XSetWMNormalHints)(dpy, w, hints);
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

template <size_t N>
static bool is_in_array(const char * (&array)[N], const char * needle) {
	
	for(unsigned i = 0; i < N; i++) {
		if(strcmp(needle, array[i]) == 0) {
			return true;
		}
	}
	
	return false;
}

static bool is_main_window_name(const char * name) {
	return is_in_array(main_windows, name);
}

static bool is_dialog_window_name(const char * name) {
	return is_in_array(dialog_windows, name);
}

static bool is_fixed_size_window_name(const char * name) {
	
	if(is_in_array(fixed_size_windows, name)) {
		return true;
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
		// PBaseSize (base_width and base_height) sounds more appropriate
		// (and is not obsolete), but some window managers won't let the window
		// get smaller than the base size.
		xsh.flags |= PSize;
		xsh.width = width, xsh.height = height;
	} else if(xsh.flags & PSize) {
		// Retrieve the desired size.
		width = xsh.width, height = xsh.height;
	} else {
		Window root;
		int x, y;
		unsigned int cwidth, cheight, border_width, depth;
		if(!XGetGeometry(dpy, w, &root, &x, &y, &cwidth, &cheight, &border_width, &depth)) {
			return;
		}
		width = cwidth, height = cheight;
	}
	if(set_fixed || ((xsh.flags & PMinSize) && (xsh.flags & PMaxSize)
	   && xsh.min_width == xsh.max_width && xsh.min_height == xsh.max_height)) {
		xsh.flags |= PMinSize | PMaxSize;
		xsh.min_width  = xsh.max_width  = width;
		xsh.min_height = xsh.max_height = height;
	}
	BASE(XSetWMNormalHints)(dpy, w, &xsh);
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
