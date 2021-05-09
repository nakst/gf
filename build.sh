if [ $# -eq 0 ]; then
	font_name=`fc-match mono | awk '{ print($1) }'`
	font_path=`fc-list | grep $font_name | awk 'BEGIN { FS = ":" } ; { print($1) }'`
	echo "Automatically detected monospaced font: $font_path"
	echo "If you want to change this, pass the path of the desired font to $0."
	echo "(Only monospaced fonts are supported.)"
else
	font_path=$1
fi

g++ gf2.cpp -o gf2 -g -lX11 -pthread \
	-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-truncation \
	-lfreetype -D UI_FREETYPE -I /usr/include/freetype2 -D UI_FONT_PATH=$font_path
