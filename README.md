
# Various window management fixes for the Linux Steam client

DISCLAIMER: Use at your own risk! This is in no way endorsed by VALVE.


## steamwm.cpp

The window management fixes, including forcing window borders for Steam. Fixes can be individually enabled or disabled - for details see the comments in the source file.

This file compiles to a library that can be `LD_PRELOAD`ed into the steam process. For your convenience it is also it's own build and wrapper script.

Requires: `g++` with support for x86 targets, `Xlib` + headers

Use:

    $ chmod +x steamwm.cpp
    $ ./steamwm.cpp steam


# noframe.patch

This is a Steam skin that complements `steamwm.cpp`: It is exactly the same as the default skin, but with the window borders and controls removed.

To install it use:

    $ chmod +x noframe.patch
    $ ./noframe.patch

and then select the **`noframe`** skin in the Steam settings.
