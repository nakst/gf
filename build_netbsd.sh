#!/bin/sh

# Check GDB is installed and uses the expected prompt.
gdb --version > /dev/null 2>&1 || printf "\033[0;31mWarning\033[0m: GDB not detected. You must install GDB to use gf.\n"
echo q | gdb | grep "(gdb)" > /dev/null 2>&1 || printf "\033[0;31mWarning\033[0m: Your copy of GDB appears to be non-standard or has been heavily reconfigured with .gdbinit.\nIf you are using GDB plugins like 'GDB Dashboard' you must remove them,\nas otherwise gf will be unable to communicate with GDB.\n"

clang++ --version > /dev/null 2>&1 || printf "\033[0;31mWarning\033[0m: Please install clang or alternatively a newer version of GCC to build gf.\n"

# Check if SSE2 is available.
[ "$(uname -m)" = amd64 ] && extra_flags="$extra_flags -DUI_SSE2"

# Build the executable.
clang++ -Wl,-R/usr/X11R7/lib gf2.cpp -o gf2 -g -O2 \
	-I/usr/local/include -L/usr/local/lib \
	-I/usr/X11R7/include -L/usr/X11R7/lib -lX11 \
	-I/usr/X11R7/include/freetype2 -lfreetype -lpthread \
	-DUI_FREETYPE \
	$extra_flags \
	-Wall -Wextra -Wno-unused-parameter -Wno-unused-result -Wno-missing-field-initializers || exit 1
