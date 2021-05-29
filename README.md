# **gf** – A GDB Frontend

![Screenshot of the debugger's interface, showing the source view, breakpoints list, call stack, bitmap viewer, and command prompt.](https://cdn.discordapp.com/attachments/462643277321994245/848264598673948672/sc2.png)
![Another screenshot, showing the watch window and different color scheme.](https://cdn.discordapp.com/attachments/462643277321994245/848264595621675068/sc1.png)

## Building

Download this project's source.

    git clone https://github.com/nakst/gf.git

And compile the application.

    ./build.sh

## Support

To support development, you can donate to my Patreon: https://www.patreon.com/nakst.

## Settings

On startup, settings are loaded from `~/.config/gf2_config.ini`, followed by `.project.gf`. This is an INI-style file.

### GDB configuration

You can pass additional arguments to GDB in the `[gdb]` section. For example,

    [gdb]
    argument=-nx
    argument=-ex record

You can also change the location of the GDB executable. For example,

    [gdb]
    path=/home/a/opt/gdb

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

- You can run the application with `./gf2`. Any additional command line arguments passed to `gf` will be forwarded to GDB.
- To view RGBA bitmaps, select the `Data` tab and then select `Add bitmap...`.
- Ctrl+Click a line in the source view to run "until" that line. Alt+Click a line in the source view to skip to it without executing the code in between.
