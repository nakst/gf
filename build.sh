RED='\033[0;31m'
WHITE='\033[0m'

if gdb --version > /dev/null
then
	do_nothing=""
else
	echo "GDB not detected. Please install GDB first!"
	exit
fi

if echo q | gdb | grep "(gdb)" > /dev/null
then
	do_nothing=""
else
	echo -e "${RED}"
	echo "Your copy of GDB appears to be non-standard or has been heavily reconfigured with .gdbinit."
	echo "If you are using GDB plugins like 'GDB Dashboard' you must remove them,"
	echo "as otherwise gf will be unable to communicate with GDB."
	echo -e "${WHITE}"
fi

if command -v fc-match &> /dev/null
then
	if [ $# -eq 0 ]; then
		font_name=`fc-match mono | awk '{ print($1) }'`
		font_path=`fc-list | grep $font_name | awk 'BEGIN { FS = ":" } ; { print($1) }' | head -n1`
		echo "Automatically detected monospaced font: $font_path"
		echo "If you want to change this, pass the path of the desired font to $0."
		echo "(Only monospaced fonts are supported.)"
	else
		font_path=$1
	fi

	if [ -z "$font_path" ];
	then
		font_flags=
		echo "No monospaced fonts were found."
		echo "If you have a specific font you want to use, pass its path to $0."
		echo "Falling back to builtin font..."
	else
		font_flags="-lfreetype -D UI_FREETYPE -I /usr/include/freetype2 -D UI_FONT_PATH=$font_path"
	fi
else
	font_flags=
	echo "It looks like you don't have fontconfig installed."
	echo "Falling back to builtin font..."
fi

if [ -f prof_window.cpp ];
then
	extension_flags=" -DPROF_EXTENSION "
else
	extension_flags=""
fi

warning_flags="-Wall -Wextra -Wno-unused-parameter -Wno-unused-result -Wno-missing-field-initializers -Wno-format-truncation"

g++ gf2.cpp -o gf2 -g -O2 -lX11 -pthread -DUI_SSE2 $warning_flags $font_flags $extra_flags $extension_flags
