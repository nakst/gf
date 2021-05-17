# **gf** – A GDB Frontend

![Screenshot of the debugger's interface, showing the source view, breakpoints list, call stack and command prompt.](https://cdn.discordapp.com/attachments/462643277321994245/795277703943159818/image.png)

## Building

Download this project's source.

    git clone https://github.com/nakst/gf.git

And compile the application.

    ./build.sh

## Settings

On startup, settings are loaded from `~/.config/gf2_config.ini`, followed by `.project.gf`. This is an INI-style file.

### GDB configuration

You can pass additional arguments to GDB in the `[gdb]` section. For example,

    [gdb]
    argument=-nx
    argument=-ex record

### Custom keyboard shortcuts

Keyboard shortcuts are placed in the `[shortcuts]` section. For example,

    [shortcuts]
    Ctrl+I=print i
    Ctrl+Shift+F10=reverse-next
    Ctrl+Shift+F11=reverse-step

### User interface

You can change the font size and user interface scaling in the `[ui]` section. For example,

    [ui]
    scale=1.5
    font_size=20

## Tips

You can run the application with `./gf2`. Any additional command line arguments passed to `gf` will be forwarded to GDB.

You can view RGBA bitmaps with the command `bitmap <pointer> <width> <height> <stride>` and pressing Shift+Enter. Large bitmaps will take a while to load from GDB.
