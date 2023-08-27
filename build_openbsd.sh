#!/bin/sh

# Check GDB is installed and uses the expected prompt.
egdb --version > /dev/null 2>&1 || printf "\033[0;31mWarning\033[0m: GDB not detected. You must install GDB to use gf.\n"
echo q | egdb | grep "(gdb)" > /dev/null 2>&1 || printf "\033[0;31mWarning\033[0m: Your copy of GDB appears to be non-standard or has been heavily reconfigured with .gdbinit.\nIf you are using GDB plugins like 'GDB Dashboard' you must remove them,\nas otherwise gf will be unable to communicate with GDB.\n"

# Check if SSE2 is available.
[ "$(uname -m)" = amd64 ] && extra_flags="$extra_flags -DUI_SSE2"

# Build the executable.
c++ gf2.cpp -o gf2 -g -O2 \
	-I/usr/local/include -L/usr/local/lib \
	-I/usr/X11R6/include -L/usr/X11R6/lib -lX11 \
	-I/usr/X11R6/include/freetype2 -lfreetype \
	-DUI_FREETYPE \
	$extra_flags \
	-Wall -Wextra -Wno-unused-parameter -Wno-unused-result -Wno-missing-field-initializers || exit 1
