#!/bin/sh

function attempt_install {
    echo "Do you want to run: $1"
    read -p "[y/n]: " yn
    case $yn in
        [Yy]*)
            eval $1
            return 0
            ;;
    esac
    echo "Aborted"
    return 1
}
function attempt_install_gdb {
    # Automatically find command to run as root, if not, use su as a fallback.
    superuser_cmd="su -c"
    which sudo > /dev/null 2>&1 && superuser_cmd="sudo"
    which doas > /dev/null 2>&1 && superuser_cmd="doas"

    # Detect what package manager is installed and use an appropriate command
    which yay     > /dev/null 2>&1 && attempt_install "yay -S gdb"                                      && return 0
    which pamac   > /dev/null 2>&1 && attempt_install "pamac install gdb"                               && return 0
    which pacman  > /dev/null 2>&1 && attempt_install "${superuser_cmd} pacman -S gdb"                  && return 0
    which apt     > /dev/null 2>&1 && attempt_install "${superuser_cmd} apt install gdb"                && return 0
    which apt-get > /dev/null 2>&1 && attempt_install "${superuser_cmd} apt-get install gdb"            && return 0
    which zypper  > /dev/null 2>&1 && attempt_install "${superuser_cmd} zypper in gdb"                  && return 0
    which dnf     > /dev/null 2>&1 && attempt_install "${superuser_cmd} dnf install gdb"                && return 0
    which apk     > /dev/null 2>&1 && attempt_install "${superuser_cmd} apk add gdb"                    && return 0
    which emerge  > /dev/null 2>&1 && attempt_install "${superuser_cmd} emerge --ask sys-devel/gdb"     && return 0
    which nix-env > /dev/null 2>&1 && attempt_install "nix-env -i $(nix-env -qaP | grep -o 'gdb[^:]*')" && return 0

    echo
    echo "Couldn't automatically install GDB."
    echo "You can try to install it yourself using '${superuser_cmd} <package manager command>'."
}

echo "Checking if GDB is installed."
gdb_warning_1="\
\033[0;31mWarning\033[0m: GDB not detected. \
You must install GDB to use gf. \
Attempting to automatically install GDB. \
"
gdb --version > /dev/null 2>&1 || (printf "${gdb_warning_1}\n" && attempt_install_gdb)
gdb --version > /dev/null 2>&1 || exit 1

echo "Checking if GDB is non-standard."
gdb_warning_2="\
\033[0;31mWarning\033[0m: \
Your copy of GDB appears to be non-standard or has been heavily reconfigured with .gdbinit.\n\
If you are using GDB plugins like 'GDB Dashboard' you must remove them,\n\
as otherwise gf will be unable to communicate with GDB.
"
echo q | gdb | grep "(gdb)" > /dev/null 2>&1 || printf "${gdb_warning_2}\n"

echo "Checking if FreeType is available."
if [ -d /usr/include/freetype2 ]; then extra_flags="$extra_flags -lfreetype -D UI_FREETYPE -I /usr/include/freetype2"; 
else printf "\033[0;31mWarning\033[0m: FreeType could not be found. The fallback font will be used.\n"; fi

echo "Checking if SSE2 is available."
uname -m | grep x86_64 > /dev/null && extra_flags="$extra_flags -DUI_SSE2"

echo "Building gf2."
g++ gf2.cpp -o gf2 -g -O2 -lX11 -pthread $extra_flags -Wall -Wextra -Wno-unused-parameter -Wno-unused-result -Wno-missing-field-initializers -Wno-format-truncation || exit 1
