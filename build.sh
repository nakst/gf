if command -v fc-match &> /dev/null
then
	if [ $# -eq 0 ]; then
		font_name=`fc-match mono | awk '{ print($1) }'`
		font_path=`fc-list | grep $font_name | awk 'BEGIN { FS = ":" } ; { print($1) }'`
		echo "Automatically detected monospaced font: $font_path"
		echo "If you want to change this, pass the path of the desired font to $0."
		echo "(Only monospaced fonts are supported.)"
	else
		font_path=$1
	fi

	font_flags="-lfreetype -D UI_FREETYPE -I /usr/include/freetype2 -D UI_FONT_PATH=$font_path"
else
	font_flags=
	echo "It looks like you don't have fontconfig installed."
	echo "Falling back to builtin font..."
fi

warning_flags="-Wall -Wextra -Wno-unused-parameter -Wno-unused-result -Wno-missing-field-initializers -Wno-format-truncation"

g++ gf2.cpp -o gf2 -g -O2 -lX11 -pthread $warning_flags $font_flags
