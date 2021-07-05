# **gf** – A GDB Frontend

![Screenshot of the debugger's interface, showing the source view, breakpoints list, call stack, bitmap viewer, and command prompt.](https://cdn.discordapp.com/attachments/462643277321994245/848264598673948672/sc2.png)
![Another screenshot, showing the watch window and different color scheme.](https://cdn.discordapp.com/attachments/462643277321994245/848264595621675068/sc1.png)

## Building

Download this project's source.

```bash
git clone https://github.com/nakst/gf.git
```

And compile the application.

```bash
./build.sh
```

## Support

To support development, you can donate to my Patreon: https://www.patreon.com/nakst.

## Settings

On startup, settings are loaded from `~/.config/gf2_config.ini`, followed by `.project.gf`. This is an INI-style file.

### GDB configuration

You can pass additional arguments to GDB in the `[gdb]` section. For example,

```ini
[gdb]
argument=-nx
argument=-ex record
```

You can also change the location of the GDB executable. For example,

```ini
[gdb]
path=/home/a/opt/gdb
```

### Custom keyboard shortcuts

Keyboard shortcuts are placed in the `[shortcuts]` section. For example,

```ini
[shortcuts]
Ctrl+I=print i
Ctrl+Shift+F10=reverse-next
Ctrl+Shift+F11=reverse-step
```

You can use any standard GDB command, or any of the commands listed in "Special commands" below.

### User interface

You can change the font size and user interface scaling in the `[ui]` section. For example,

```ini
[ui]
scale=1.5
font_size=20
```
    
You can also configure the interface layout, with the `layout` parameter. Use `h(position,left,right)` to create a horizontal split, `v(position,left,right)` to create a vertical split, and `t(...)` to create a tab pane. This value should not contain any whitespace. Please note this value is not validated, so make sure it is formatted correctly!

```ini
layout=h(75,v(75,Source,Console),v(50,t(Watch,Breakpoints,Commands,Struct),t(Stack,Files,Registers,Data))))
```

**NB: Horizontal and vertical splits must have exactly two children.** Instead, you can nest them to create more complex layouts.

### Themes

You can change the theme in the `theme` section. See https://github.com/nakst/gf/wiki/Themes for a list of examples.

### Preset commands

You can create a list of quickly accessible commands, available in the "Commands" tab in the UI. Separate individual commands using a semicolon. Each command in the list is run one after another; to run the final command asynchronously, put a `&` at the end. For example,

```ini
[commands]
Compile=shell gcc -o bin/app src/main.c
Run normal=file bin/app;run&
Run tests=file bin/app;run test_cases.txt&
Set breakpoints=b main;b LoadFile;b AssertionFailure
```

You can use any standard GDB command, or any of the commands listed in "Special commands" below.

### Vim integration

You can change the server name with the `server_name` key in the `vim` section. For example,

```ini
[vim]
server_name=MyVimServer
```

## Tips

- You can run the application with `./gf2`. Any additional command line arguments passed to `gf` will be forwarded to GDB.
- To view RGBA bitmaps, select the `Data` tab and then select `Add bitmap...`.
- Ctrl+Click a line in the source view to run "until" that line. Alt+Click a line in the source view to skip to it without executing the code in between.
- Press Shift+F10 to step out of a block, and press Shift+F11 to step out a function.
- Press Tab while entering a watch expression to auto-complete it.
- Press `/` with a watch expression highlighted to change the format specifier. For example, `/x` switches to hexadecimal view.

## Control pipe

You can change the loaded file and line by sending commands to the control pipe. 

First, you must set the location of the control pipe. In the `[pipe]` section of the configuration file, set the `control` key to the absolute path where you want the control pipe to be.

Then, you can send commands to the pipe. For example,

```bash
# Load the specified file (must be a full path).
echo f /home/a/test.c > /home/a/control_pipe.dat

# Go to line 123.
echo l 123 > /home/a/control_pipe.dat

# Send a GDB command.
echo c file myapp > /home/a/control_pipe.dat
```

This can be used for text editor integration.

## Log window

You can show messages send to a pipe using the log window.

First, you must set the location of the log pipe. In the `[pipe]` section of the configuration file, set the `log` key to the absolute path where you want the log pipe to be. Next, you must add the "Log" window somewhere in your layout string (see the "User interface" section above).

Once configured, you can then send messages to the pipe and they will appear in the log window.

## Special commands

### gf-step

`gf-step` either steps a single line (`step`) or single instruction (`stepi`), depending whether disassembly view is active.

### gf-next

`gf-next` either steps over a single line (`next`) or single instruction (`nexti`), depending whether disassembly view is active.

### gf-step-out-of-block

`gf-step-out-of-block` steps out of the current block. That is, it steps to the next line after the first unmatched `}`, starting from the current line. 

### gf-restart-gdb

`gf-restart-gdb` restarts the GDB process immediately. Any state such as loaded symbol files or breakpoints will be lost.

### gf-get-pwd

`gf-get-pwd` asks GDB for the working directory in which the current executable file was compiled. This ensures the source view tries to load files from the correct directory.

### gf-switch-to

`gf-switch-to <window-name>` switches to a specific window. The window names are the same as given in the layout string, as seen in the "User interface" section.

### gf-command

`gf-command <name>` runs the command(s) corresponding to `name` in the `[commands]` section of your configuration file.
