# **gf** – A GDB Frontend

![Build status](https://img.shields.io/github/workflow/status/nakst/gf/CI)

![Screenshot of the debugger's interface, showing the source view, breakpoints list, call stack, bitmap viewer, and command prompt.](https://raw.githubusercontent.com/nakst/cdn/main/gf2.png)
![Another screenshot, showing the watch window and different color scheme.](https://raw.githubusercontent.com/nakst/cdn/main/gf1.png)
![Another screenshot, showing the disassembly and register windows.](https://raw.githubusercontent.com/nakst/cdn/main/gf3.png)

## Building

Download this project's source.

```bash
git clone https://github.com/nakst/gf.git
```

And compile the application.

```bash
./build.sh
```

Please read the rest of this file to learn about using and configuring `gf`. If you're new to GDB, see [this article](https://handmade.network/forums/articles/t/2883-gdb).

## Extension pack

![A screenshot showing the embedded profiler, which is displaying a multi-colored flame graph.](https://raw.githubusercontent.com/nakst/cdn/main/unknown2.png)
![A screenshot showing the memory window and extended watch expression view.](https://raw.githubusercontent.com/nakst/cdn/main/%20memory%20window%20and%20extended%20view%20window.png)

All tiers on my Patreon, https://www.patreon.com/nakst, get access to the extension pack. 

This currently includes:
- Embedded tracing profiler
- Memory window
- Extended watch expression view (for strings, matrices and base conversion)
- Full source code for the pack

Make sure you use the latest version of the extension pack with the latest commit of `gf`, otherwise you'll likely run into compile or runtime errors!

## Tips

- You can run the application with `./gf2`. Any additional command line arguments passed to `gf` will be forwarded to GDB.
- Press Ctrl+Shift+P to synchronize your working directory with GDB after you start your target executable. This is necessary if you open `gf` in a different directory to the one you compile in.
- To view RGBA bitmaps, select the `Data` tab and then select `Add bitmap...`.
- Ctrl+Click a line in the source view to run "until" that line. Shift+Click a line in the source view to skip to it without executing the code in between.
- Press Shift+F10 to step out of a block, and press Shift+F11 to step out a function.
- Press Tab while entering a watch expression to auto-complete it.
- Press `/` with a watch expression highlighted to change the format specifier. For example, `/x` switches to hexadecimal view.
- Press backtick to enter line inspect mode. This mode evaluates all expressions on the current line.

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

You can change the font and user interface scaling in the `[ui]` section. For example,

```ini
[ui]
scale=1.5
font_path=/usr/share/fonts/TTF/DejaVuSansMono.ttf
font_size_interface=17
font_size_code=20
```

To change the font, FreeType must have been available when you compiled gf. You can enable subpixel font rendering by recompiling with `extra_flags=-DUI_FREETYPE_SUBPIXEL ./build.sh`.
    
You can also configure the interface layout, with the `layout` parameter. Use `h(position,left,right)` to create a horizontal split, `v(position,left,right)` to create a vertical split, and `t(...)` to create a tab pane. This value should not contain any whitespace. Please note this value is not validated, so make sure it is formatted correctly!

```ini
layout=h(75,v(75,Source,Console),v(50,t(Watch,Breakpoints,Commands,Struct,Exe),t(Stack,Files,Registers,Data,Thread))))
```

**NB: Horizontal and vertical splits must have exactly two children.** Instead, you can nest them to create more complex layouts.

You can maximize the window at startup with `maximize=1`.

You can request for the expressions in the watch window to be saved and restored by setting `restore_watch_window=1`.

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

First, you must set the location of the log pipe. In the `[pipe]` section of the configuration file, set the `log` key to the absolute path where you want the log pipe to be. Next, you must add the "Log" window somewhere in your layout string (see the "User interface" section above). Once configured, you can then send messages to the pipe and they will appear in the log window. 

Here is an example of how to send messages to the pipe:

```c
#define LOG(...) do { fprintf(logFile, __VA_ARGS__); fflush(logFile); } while (0)
#define LOG_OPEN(path) logFile = fopen(path, "w")
FILE *logFile;

...

LOG_OPEN("...");
LOG("Hello, world!\n");
```

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

### gf-inspect-line

`gf-inspect-line` toggles inspect line mode. By default, this is bound to the backtick key.

## Watch window hooks

You can customize the behaviour of the watch window when displaying specific types using Python. When the watch window wants to display the fields of a value, it will look a hook function at `gf_hooks[type_of_value]`. The hook function should take two arguments, `item` and `field`. If the hook function exists, it will be called in one of two ways:

1. When the watch window needs a list of the fields in the value, it calls the hook with `item` set to an opaque handle and `field` set to `None`. You should print out a list of all the names of the fields in the value, one on each line. You can print out all the standard fields by calling `_gf_fields_recurse(item)`. **When adding custom fields, their names must be enclosed by `[]`.**
2. When the watch window needs to get the value of a specific custom field in the value, it calls the hook with `item` set to a `gdb.Value` for the value, and `field` to the name of the custom field that was added. **The hook is not called for standard fields.** You should return a `gdb.Value` that gives the value of the field.

For example, the following hook add a width and height custom field for a rectangle type.

```py
def RectangleHook(item, field):
    if field:
        if field == '[width]':  
            # item['...'] looks up a field in the struct, returned as a gdb.Value
            # int(...) converts the gdb.Value to an int so we can do arithmetic on it
            # gdb.Value(...) converts the result back to a gdb.Value
            return gdb.Value(int(item['right']) - item['left'])
        if field == '[height]': 
            # do something similar for the height
            return gdb.Value(int(item['bottom']) - item['top'])
    else:
        print('[width]')         # add the width custom field
        print('[height]')        # add the height custom field
        _gf_fields_recurse(item) # add the fields actually in the struct

gf_hooks = { 'Rectangle': RectangleHook } # create the hook dictionary
```

If you want to create a custom dynamic array type, instead of printing field names, print `(d_arr)` followed by the number of array items. The fields will then be automatically populated in the form of `[%d]`, where `%d` is the index. For example, given the following structure:

```cpp
struct MyArray {
	int length;
	float *items;
};
```

This is the hook definition:

```py
def MyArrayHook(item, field):
	if field: return item['items'][int(field[1:-1])]
	else: print('(d_arr)', int(item['length']))
```

Templates are removed from the name of the type. For example, `Array<int>`, `Array<char *>` and `Array<float>` would all use the `Array` hook.

## Contributors

```
nakst
Philippe Mongeau phmongeau 
Jimmy "Keeba" Lefevre JimmyLefevre 
John Blat johnblat64 
IWouldRatherUsePasteBin
Gavin Beatty gavinbeatty
Michael Stopa StomyPX
Anders Kaare sqaxomonophonen
Arseniy Khvorov khvorov45
```
