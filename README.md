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
    
You can also configure the interface layout, with the `layout` parameter. Use `h(position,left,right)` to create a horizontal split, `v(position,left,right)` to create a vertical split, and `t(...)` to create a tab pane. This value should not contain any whitespace. Please note this value is not validated, so make sure it is formatted correctly!

    layout=h(75,v(75,Source,Console),v(50,t(Watch,Breakpoints,Commands,Struct),t(Stack,Files,Registers,Data))))

### Themes

You can change the theme in the `theme` section. See https://github.com/nakst/gf/wiki/Themes for a list of examples.

### Preset commands

You can create a list of quickly accessible commands, available in the "Commands" tab in the UI. Separate individual commands using a semicolon. For example,

    [commands]
    Compile=shell gcc -o bin/app src/main.c
    Run normal=file bin/app;run
    Run tests=file bin/app;run test_cases.txt
    Set breakpoints=b main;b LoadFile;b AssertionFailure

## Tips

- You can run the application with `./gf2`. Any additional command line arguments passed to `gf` will be forwarded to GDB.
- To view RGBA bitmaps, select the `Data` tab and then select `Add bitmap...`.
- Ctrl+Click a line in the source view to run "until" that line. Alt+Click a line in the source view to skip to it without executing the code in between.
- Press Shift+F10 to step out of a block, and press Shift+F11 to step out a function.
- Press Tab while entering a watch expression to auto-complete it.

## Control pipe

You can change the loaded file and line by sending commands to the control pipe, located at `$HOME/.config/gf2_control.dat`. For example,

    # Load the specified file (must be a full path).
    echo f /home/a/test.c > $HOME/.config/gf2_control.dat

    # Go to line 123.
    echo l 123 > $HOME/.config/gf2_control.dat
    
    # Send a GDB command.
    echo c file myapp > $HOME/.config/gf2_control.dat

This can be used for text editor integration.
