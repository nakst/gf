#!/bin/sh

# Check GDB is installed and uses the expected prompt.
gdb --version > /dev/null 2>&1 || printf "\033[0;31mWarning\033[0m: GDB not detected. You must install GDB to use gf.\n"
echo q | gdb | grep "(gdb)" > /dev/null 2>&1 || printf "\033[0;31mWarning\033[0m: Your copy of GDB appears to be non-standard or has been heavily reconfigured with .gdbinit.\nIf you are using GDB plugins like 'GDB Dashboard' you must remove them,\nas otherwise gf will be unable to communicate with GDB.\n"

# Check if FreeType is available.
if [ -d /usr/include/freetype2 ]; then extra_flags="$extra_flags -lfreetype -D UI_FREETYPE -I /usr/include/freetype2"; 
elif [ -d /usr/local/include/freetype2 ]; then extra_flags="$extra_flags -lfreetype -D UI_FREETYPE -I /usr/local/include/freetype2"; 
else printf "\033[0;31mWarning\033[0m: FreeType could not be found. The fallback font will be used.\n"; fi

# Check if SSE2 is available.
uname -m | grep x86_64 > /dev/null && extra_flags="$extra_flags -DUI_SSE2"

# Build the executable.
if [ "$(uname)" == "FreeBSD" ]; then
c++ gf2.cpp -o gf2 -g -O2 -L/usr/local/lib -I/usr/local/include -lX11 -pthread -DUI_NO_COLOR_PICKER $extra_flags -Wall -Wextra -Wno-unused-parameter -Wno-unused-result -Wno-missing-field-initializers -Wno-format-truncation || exit 1
else
g++ gf2.cpp -o gf2 -g -O2 -lX11 -pthread -DUI_NO_COLOR_PICKER $extra_flags -Wall -Wextra -Wno-unused-parameter -Wno-unused-result -Wno-missing-field-initializers -Wno-format-truncation || exit 1
fi
