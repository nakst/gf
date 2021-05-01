# **gf** - A GDB Frontend

![Screenshot of the debugger's interface, showing the source view, breakpoints list, call stack and command prompt.](https://cdn.discordapp.com/attachments/462643277321994245/795277703943159818/image.png)

## Building

Download this project's source.

    git clone https://github.com/nakst/gf.git

And compile the application.

    g++ -o gf2 gf2.cpp -lX11 -pthread

If you want to specify a font, add the following to the compile command:

    -lfreetype -I /usr/include/freetype2 -D UI_FREETYPE -D UI_FONT_PATH=<path to font file> -D UI_FONT_SIZE=13

You can run the application with `./gf2`. Any additional command line arguments passed to `gf` will be forwarded to GDB.

On startup, the file `.project.gf` will be loaded (if it exists), and each line will be assigned as a command to the keyboard shortcuts Ctrl+n sequentially for `n=0,1,...`.

You can view RGBA bitmaps with the command `bitmap <pointer> <width> <height> <stride>` and pressing Shift+Enter. Large bitmaps will take a while to load from GDB.
