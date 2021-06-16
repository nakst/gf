// Build with: g++ gf2.cpp -lX11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-truncation -o gf2 -g -pthread
// Add the following flags to use FreeType: -lfreetype -D UI_FREETYPE -I /usr/include/freetype2 -D UI_FONT_PATH=/usr/share/fonts/TTF/DejaVuSansMono.ttf

// TODO Config file security concerns.
// 	We should probably ask the user if they trust the local config file the first time it's seen, and each time it's modified.
// 	Otherwise opening the application in the wrong directory could be dangerous (you can easily get GDB to run shell commands).

// TODO Run until current line reached again, maybe Ctrl+F10? I think "tbreak\nc" should work.

// TODO Rearrange default UI layout; the breakpoint window takes up too much space.

// TODO Disassembly window extensions.
// 	- Split source and disassembly view.
// 	- Setting/clearing/showing breakpoints.
// 	- Jump to line and run to line.
// 	- Shift+F10: run to next instruction (for skipping past loops).

// TODO Data window extensions.
// 	- Use error dialogs for bitmap errors.
// 	- Copy bitmap to clipboard, or save to file.

// TODO Watch window.
//	- Better toggle buttons.
// 	- Improve performance if possible?
// 	- Tab completion!
// 	- Lock pointer address.
// 	- Record a log of everytime the value changes.

// TODO Future extensions.
// 	- Memory window.
// 	- Hover to view value.
// 	- Thread selection.
// 	- Data breakpoint viewer.
// 	- Automatically restoring breakpoints and symbols files after restarting gdb.

#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <dirent.h>

char *layoutString = (char *) "v(75,h(80,Source,v(50,t(Breakpoints,Commands,Struct),t(Stack,Files))),h(65,Console,t(Watch,Registers,Data)))";
// char *layoutString = (char *) "h(75,v(75,Source,Console),v(50,t(Watch,Breakpoints,Commands,Struct),t(Stack,Files,Registers,Data))))";

int fontSize = 13;
float uiScale = 1;

extern "C" {
#define UI_FONT_SIZE (fontSize)
#define UI_LINUX
#define UI_IMPLEMENTATION
#include "luigi.h"
}

#define MSG_RECEIVED_DATA ((UIMessage) (UI_MSG_USER + 1))

struct INIState {
	char *buffer, *section, *key, *value;
	size_t bytes, sectionBytes, keyBytes, valueBytes;
};

FILE *commandLog;
char emptyString;

// Current file and line:

char currentFile[PATH_MAX];
char currentFileFull[PATH_MAX];
int currentLine;
time_t currentFileReadTime;

bool showingDisassembly;
char *disassemblyPreviousSourceFile;
int disassemblyPreviousSourceLine;

char previousLocation[256];

// User interface:

#define TAB_REGISTERS (0)
#define TAB_WATCH (1)
#define TAB_DATA (2)

UIButton *buttonFillWindow;
UIButton *buttonMenu;
UICode *displayCode;
UICode *displayOutput;
UICode *displayStruct;
UIMDIClient *dataWindow;
UIPanel *dataTab;
UIPanel *panelFiles;
UIPanel *panelPresetCommands;
UIPanel *registersWindow;
UIPanel *rootPanel;
UISpacer *trafficLight;
UITable *tableBreakpoints;
UITable *tableCommands;
UITable *tableStack;
UITextbox *textboxInput;
UITextbox *textboxStructName;
UIWindow *window;

// Theme editor:

UIWindow *themeEditorWindow;
UIColorPicker *themeEditorColorPicker;
UITable *themeEditorTable;
int themeEditorSelectedColor;

// Dynamic arrays:

template <class T>
struct Array {
	T *array;
	size_t length, allocated;

	inline void Insert(T item, uintptr_t index) {
		if (length == allocated) {
			allocated = length * 2 + 1;
			array = (T *) realloc(array, allocated * sizeof(T));
		}

		length++;
		memmove(array + index + 1, array + index, (length - index - 1) * sizeof(T));
		array[index] = item;
	}

	inline void Delete(uintptr_t index, size_t count = 1) { 
		memmove(array + index, array + index + count, (length - index - count) * sizeof(T)); 
		length -= count;
	}

	inline void Add(T item) { Insert(item, length); }
	inline void Free() { free(array); array = nullptr; length = allocated = 0; }
	inline int Length() { return length; }
	inline T &First() { return array[0]; }
	inline T &Last() { return array[length - 1]; }
	inline T &operator[](uintptr_t index) { return array[index]; }
	inline void Pop() { length--; }
	inline void DeleteSwap(uintptr_t index) { if (index != length - 1) array[index] = Last(); Pop(); }
};

// Bitmap viewer:

struct BitmapViewer {
	char pointer[256];
	char width[256];
	char height[256];
	char stride[256];
	UIButton *autoToggle;
	UIImageDisplay *display;
};

Array<UIElement *> autoUpdateBitmapViewers;
bool autoUpdateBitmapViewersQueued;

char *addBitmapPointerString;
char *addBitmapWidthString;
char *addBitmapHeightString;
char *addBitmapStrideString;

// Call stack:

struct StackEntry {
	char function[64];
	char location[sizeof(previousLocation)];
	uint64_t address;
	int id;
};

Array<StackEntry> stack;
int stackSelected;
bool stackChanged;

// Breakpoints:

struct Breakpoint {
	char file[64];
	char fileFull[PATH_MAX];
	int line;
};

Array<Breakpoint> breakpoints;

// Command history:

Array<char *> commandHistory;
int commandHistoryIndex;

// Auto-print expression:

char autoPrintExpression[1024];
char autoPrintResult[1024];
int autoPrintExpressionLine;
int autoPrintResultLine;

// Other debug windows:

struct RegisterData { char string[128]; };
Array<RegisterData> registerData;
Array<INIState> presetCommands;
char filesDirectory[PATH_MAX];

// GDB process:

#define RECEIVE_BUFFER_SIZE (16777216)
char receiveBuffer[RECEIVE_BUFFER_SIZE];
size_t receiveBufferPosition;
volatile int pipeToGDB;
volatile pid_t gdbPID;
volatile pthread_t gdbThread;
pthread_cond_t evaluateEvent;
pthread_mutex_t evaluateMutex;
char *evaluateResult;
bool evaluateMode;
bool programRunning;
char **gdbArgv;
int gdbArgc;
const char *gdbPath = "gdb";
bool firstUpdate = true;

// Watch window:

struct Watch {
	bool open, hasFields, loadedFields, isArray;
	uint8_t depth;
	uintptr_t arrayIndex;
	char *key, *value, *type;
	Array<Watch *> fields;
	Watch *parent;
	uint64_t updateIndex;
};

Array<Watch *> watchRows;
Array<Watch *> watchBaseExpressions;
UIElement *watchWindow;
UITextbox *watchTextbox;
int watchSelectedRow;
uint64_t watchUpdateIndex;

// Python code:

const char *pythonCode = R"(py

def _gf_value(expression):
    try:
        value = gdb.parse_and_eval(expression[0])
        for index in expression[1:]:
            value = value[index]
        return value
    except gdb.error:
        print('??')
        return None

def gf_typeof(expression):
    value = _gf_value(expression)
    if value == None: return
    print(value.type)

def gf_valueof(expression):
    value = _gf_value(expression)
    if value == None: return
    result = ''
    while True:
        basic_type = gdb.types.get_basic_type(value.type)
        if basic_type.code != gdb.TYPE_CODE_PTR: break
        try:
            result = result + '(' + str(value) + ') '
            value = value.dereference()
        except:
            break
    try:
        result = result + value.format_string(max_elements=10,max_depth=3)[0:200]
    except:
        result = result + '??'
    print(result)

def _gf_fields_recurse(type):
    if type.code == gdb.TYPE_CODE_STRUCT or type.code == gdb.TYPE_CODE_UNION:
        for field in gdb.types.deep_items(type):
            if field[1].is_base_class:
                _gf_fields_recurse(field[1].type)
            else:
                print(field[0])
    elif type.code == gdb.TYPE_CODE_ARRAY:
        print('(array)',type.range()[1])

def gf_fields(expression):
    value = _gf_value(expression)
    if value == None: return
    basic_type = gdb.types.get_basic_type(value.type)
    if basic_type.code == gdb.TYPE_CODE_PTR:
        basic_type = gdb.types.get_basic_type(basic_type.target())
    _gf_fields_recurse(basic_type)

end
)";

int StringFormat(char *buffer, size_t bufferSize, const char *format, ...) {
	va_list arguments;

	va_start(arguments, format);
	size_t length = vsnprintf(buffer, bufferSize, format, arguments);
	va_end(arguments);

	if (length > bufferSize) {
		// HACK This could truncate a UTF-8 codepoint.
		length = bufferSize;
	}

	return length;
}

void *DebuggerThread(void *) {
	int outputPipe[2], inputPipe[2];
	pipe(outputPipe);
	pipe(inputPipe);

	posix_spawn_file_actions_t actions = {};
	posix_spawn_file_actions_adddup2(&actions, inputPipe[0],  0);
	posix_spawn_file_actions_adddup2(&actions, outputPipe[1], 1);
	posix_spawn_file_actions_adddup2(&actions, outputPipe[1], 2);

	posix_spawnattr_t attrs = {};
	posix_spawnattr_init(&attrs);
	posix_spawnattr_setflags(&attrs, POSIX_SPAWN_SETSID);

	posix_spawnp((pid_t *) &gdbPID, gdbPath, &actions, &attrs, gdbArgv, environ);

	pipeToGDB = inputPipe[1];

	while (true) {
		char buffer[512 + 1];
		int count = read(outputPipe[0], buffer, 512);
		buffer[count] = 0;
		if (!count) break;
		receiveBufferPosition += StringFormat(receiveBuffer + receiveBufferPosition,
			RECEIVE_BUFFER_SIZE - receiveBufferPosition, "%s", buffer);
		if (!strstr(receiveBuffer, "(gdb) ")) continue;

		receiveBuffer[receiveBufferPosition] = 0;
		char *copy = strdup(receiveBuffer);

		// printf("got (%d) {%s}\n", evaluateMode, copy);

		// Notify the main thread we have data.

		if (evaluateMode) {
			free(evaluateResult);
			evaluateResult = copy;
			evaluateMode = false;
			pthread_mutex_lock(&evaluateMutex);
			pthread_cond_signal(&evaluateEvent);
			pthread_mutex_unlock(&evaluateMutex);
		} else {
			UIWindowPostMessage(window, MSG_RECEIVED_DATA, copy);
		}

		receiveBufferPosition = 0;
	}

	return NULL;
}

void DebuggerStartThread() {
	pthread_t debuggerThread;
	pthread_attr_t attributes;
	pthread_attr_init(&attributes);
	pthread_create(&debuggerThread, &attributes, DebuggerThread, NULL);
	gdbThread = debuggerThread;
}

void DebuggerSend(const char *string, bool echo) {
	if (programRunning) {
		kill(gdbPID, SIGINT);
	}

	programRunning = true;
	UIElementRepaint(&trafficLight->e, NULL);

	// printf("sending: %s\n", string);

	char newline = '\n';

	if (echo) {
		UICodeInsertContent(displayOutput, string, -1, false);
		UIElementRefresh(&displayOutput->e);
	}

	write(pipeToGDB, string, strlen(string));
	write(pipeToGDB, &newline, 1);
}

void EvaluateCommand(const char *command, bool echo = false) {
	if (programRunning) {
		kill(gdbPID, SIGINT);
		usleep(1000 * 1000);
		programRunning = false;
	}

	evaluateMode = true;
	pthread_mutex_lock(&evaluateMutex);
	DebuggerSend(command, echo);
	struct timespec timeout;
	clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec++;
	pthread_cond_timedwait(&evaluateEvent, &evaluateMutex, &timeout);
	pthread_mutex_unlock(&evaluateMutex);
	programRunning = false;
	UIElementRepaint(&trafficLight->e, NULL);
}

char *LoadFile(const char *path, size_t *_bytes) {
	FILE *f = fopen(path, "rb");

	if (!f) {
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	size_t bytes = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buffer = (char *) malloc(bytes + 1);
	buffer[bytes] = 0;
	fread(buffer, 1, bytes, f);
	fclose(f);
	if (_bytes) *_bytes = bytes;

	return buffer;
}

extern "C" bool DisplaySetPosition(const char *file, int line, bool useGDBToGetFullPath) {
	if (showingDisassembly) {
		if (file) {
			free(disassemblyPreviousSourceFile);
			disassemblyPreviousSourceFile = strdup(file);
		}

		if (line != -1) {
			disassemblyPreviousSourceLine = line;
		}

		return false;
	}

	char buffer[4096];
	const char *originalFile = file;

	if (file && file[0] == '~') {
		StringFormat(buffer, sizeof(buffer), "%s/%s", getenv("HOME"), 1 + file);
		file = buffer;
	} else if (file && file[0] != '/' && useGDBToGetFullPath) {
		EvaluateCommand("info source");
		const char *f = strstr(evaluateResult, "Located in ");

		if (f) {
			f += 11;
			const char *end = strchr(f, '\n');

			if (end) {
				StringFormat(buffer, sizeof(buffer), "%.*s", (int) (end - f), f);
				file = buffer;
			}
		}
	}

	bool reloadFile = false;

	if (file) {
		if (strcmp(currentFile, originalFile)) {
			reloadFile = true;
		}

		struct stat buf;

		if (!stat(file, &buf) && buf.st_mtim.tv_sec != currentFileReadTime) {
			reloadFile = true;
		}

		currentFileReadTime = buf.st_mtim.tv_sec;
	}

	bool changed = false;

	if (reloadFile) {
		currentLine = 0;
		StringFormat(currentFile, 4096, "%s", originalFile);
		realpath(currentFile, currentFileFull);

		printf("attempting to load '%s' (from '%s')\n", file, originalFile);

		size_t bytes;
		char *buffer2 = LoadFile(file, &bytes);

		if (!buffer2) {
			char buffer3[4096];
			StringFormat(buffer3, 4096, "The file '%s' (from '%s') could not be loaded.", file, originalFile);
			UICodeInsertContent(displayCode, buffer3, -1, true);
		} else {
			UICodeInsertContent(displayCode, buffer2, bytes, true);
			free(buffer2);
		}

		changed = true;
		autoPrintResult[0] = 0;
	}

	if (line != -1 && currentLine != line) {
		currentLine = line;
		UICodeFocusLine(displayCode, line);
		changed = true;
	}

	UIElementRefresh(&displayCode->e);

	return changed;
}

void CommandSendToGDB(void *_string) {
	DebuggerSend((const char *) _string, true);
}

void CommandSendToGDBStep(void *_string) {
	const char *command = (const char *) _string;

	if (showingDisassembly) {
		if (0 == strcmp(command, "s")) {
			command = "stepi";
		} else if (0 == strcmp(command, "n")) {
			command = "nexti";
		}
	}

	DebuggerSend(command, true);
}

void CommandStepOutOfBlock(void *) {
	if (currentLine - 1 >= displayCode->lineCount) return;

	int tabs = 0;

	for (int i = 0; i < displayCode->lines[currentLine - 1].bytes; i++) {
		if (displayCode->content[displayCode->lines[currentLine - 1].offset + i] == '\t') tabs++;
		else break;
	}

	for (int j = currentLine; j < displayCode->lineCount; j++) {
		int t = 0;

		for (int i = 0; i < displayCode->lines[j].bytes - 1; i++) {
			if (displayCode->content[displayCode->lines[j].offset + i] == '\t') t++;
			else break;
		}

		if (t < tabs && displayCode->content[displayCode->lines[j].offset + t] == '}') {
			char buffer[256];
			StringFormat(buffer, sizeof(buffer), "until %d", j + 1);
			DebuggerSend(buffer, true);
			return;
		}
	}
}

void CommandDeleteBreakpoint(void *_index) {
	int index = (int) (intptr_t) _index;
	Breakpoint *breakpoint = &breakpoints[index];
	char buffer[1024];
	StringFormat(buffer, 1024, "clear %s:%d", breakpoint->file, breakpoint->line);
	DebuggerSend(buffer, true);
}

void CommandRestartGDB(void *) {
	firstUpdate = true;
	kill(gdbPID, SIGKILL);
	pthread_cancel(gdbThread); // TODO Is there a nicer way to do this?
	receiveBufferPosition = 0;
	DebuggerStartThread();
}

void CommandPause(void *) {
	kill(gdbPID, SIGINT);
}

void CommandSyncWithGvim(void *) {
	if (system("vim --servername GVIM --remote-expr \"execute(\\\"ls\\\")\" | grep % > .temp.gf")) {
		return;
	}

	char buffer[1024];
	FILE *file = fopen(".temp.gf", "r");

	if (file) {
		buffer[fread(buffer, 1, 1023, file)] = 0;
		fclose(file);

		{
			char *name = strchr(buffer, '"');
			if (!name) goto done;
			char *nameEnd = strchr(++name, '"');
			if (!nameEnd) goto done;
			*nameEnd = 0;
			char *line = strstr(nameEnd + 1, "line ");
			if (!line) goto done;
			int lineNumber = atoi(line + 5);
			DisplaySetPosition(name, lineNumber, false);
		}

		done:;
		unlink(".temp.gf");
	}
}

void CommandToggleBreakpoint(void *_line) {
	int line = (int) (intptr_t) _line;

	if (showingDisassembly) {
		// TODO.
		return;
	}

	if (!line) {
		line = currentLine;
	}

	for (int i = 0; i < breakpoints.Length(); i++) {
		if (breakpoints[i].line == line && 0 == strcmp(breakpoints[i].fileFull, currentFileFull)) {
			char buffer[1024];
			StringFormat(buffer, 1024, "clear %s:%d", currentFile, line);
			DebuggerSend(buffer, true);
			return;
		}
	}

	char buffer[1024];
	StringFormat(buffer, 1024, "b %s:%d", currentFile, line);
	DebuggerSend(buffer, true);
}

int ThemeEditorWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_WINDOW_CLOSE) {
		UIElementDestroy(element);
		themeEditorWindow = NULL;
		return 1;
	}

	return 0;
}

const char *themeItems[] = {
	"panel1", "panel2", "text", "textDisabled", "border",
	"buttonNormal", "buttonHovered", "buttonPressed", "buttonFocused", "buttonDisabled",
	"textboxNormal", "textboxText", "textboxFocused", "textboxSelected", "textboxSelectedText",
	"scrollGlyph", "scrollThumbNormal", "scrollThumbHovered", "scrollThumbPressed",
	"codeFocused", "codeBackground", "codeDefault", "codeComment", "codeString", "codeNumber", "codeOperator", "codePreprocessor",
	"gaugeFilled", "tableSelected", "tableSelectedText", "tableHovered", "tableHoveredText",
};

int ThemeEditorTableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		m->isSelected = themeEditorSelectedColor == m->index;

		if (m->column == 0) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", themeItems[m->index]);
		} else {
			return StringFormat(m->buffer, m->bufferBytes, "#%.6x", ui.theme.colors[m->index]);
		}
	} else if (message == UI_MSG_CLICKED) {
		themeEditorSelectedColor = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY);
		UIColorToHSV(ui.theme.colors[themeEditorSelectedColor],
			&themeEditorColorPicker->hue, &themeEditorColorPicker->saturation, &themeEditorColorPicker->value);
		UIElementRepaint(&themeEditorColorPicker->e, NULL);
	}

	return 0;
}

int ThemeEditorColorPickerMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_VALUE_CHANGED && themeEditorSelectedColor >= 0) {
		UIColorToRGB(themeEditorColorPicker->hue, themeEditorColorPicker->saturation, themeEditorColorPicker->value,
			&ui.theme.colors[themeEditorSelectedColor]);
		UIElementRepaint(&window->e, NULL);
		UIElementRepaint(&element->window->e, NULL);
	}

	return 0;
}

void CommandLoadTheme(void *) {
	char buffer[4096];
	StringFormat(buffer, 4096, "%s/.config/gf2_theme.dat", getenv("HOME"));
	FILE *f = fopen(buffer, "rb");

	if (f) {
		fread(&ui.theme, 1, sizeof(ui.theme), f);
		fclose(f);

		if (window) {
			UIElementRepaint(&window->e, NULL);
		}

		if (themeEditorWindow) {
			UIElementRepaint(&themeEditorWindow->e, NULL);
		}
	}
}

void CommandSaveTheme(void *) {
	char buffer[4096];
	StringFormat(buffer, 4096, "%s/.config/gf2_theme.dat", getenv("HOME"));
	FILE *f = fopen(buffer, "wb");

	if (f) {
		fwrite(&ui.theme, 1, sizeof(ui.theme), f);
		fclose(f);
	}
}

void ColorPresetDark(void *) {
	ui.theme = _uiThemeDark;
	UIElementRepaint(&window->e, NULL);
	UIElementRepaint(&themeEditorWindow->e, NULL);
}

void ColorPresetClassic(void *) {
	const uint8_t data[] = {
		0xF0, 0xF0, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x40, 0x40, 0x40, 0xFF,
		0x40, 0x40, 0x40, 0xFF, 0xE0, 0xE0, 0xE0, 0xFF, 0xF0, 0xF0, 0xF0, 0xFF, 0xA0, 0xA0, 0xA0, 0xFF,
		0xFF, 0xE4, 0xD3, 0xFF, 0xF0, 0xF0, 0xF0, 0xFF, 0xF8, 0xF8, 0xF8, 0xFF, 0x00, 0x00, 0x00, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x5E, 0x17, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x60, 0x60, 0x60, 0xFF,
		0xB0, 0xB0, 0xB0, 0xFF, 0xD0, 0xD0, 0xD0, 0xFF, 0x90, 0x90, 0x90, 0xFF, 0xCC, 0xC0, 0xC0, 0x00,
		0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0x73, 0x81, 0x00, 0x14, 0x47, 0xCF, 0x00,
		0x12, 0x9C, 0x00, 0x00, 0xDF, 0x14, 0x13, 0x00, 0x5C, 0x6B, 0x97, 0x00, 0x42, 0xE3, 0x2C, 0xFF,
		0xFE, 0xBE, 0x94, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xE4, 0xD3, 0xFF, 0x00, 0x00, 0x00, 0xFF,
	};

	memcpy(&ui.theme, data, sizeof(UITheme));
	UIElementRepaint(&window->e, NULL);
	UIElementRepaint(&themeEditorWindow->e, NULL);
}

void ColorPresetIce(void *) {
	const uint8_t data[] = {
		0xFF, 0xF4, 0xF1, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x7D, 0x78, 0xFF,
		0x00, 0x00, 0x00, 0x00, 0xFF, 0xED, 0xEA, 0x00, 0xFF, 0xF8, 0xF0, 0x00, 0xFB, 0xC5, 0xB6, 0x00,
		0xF7, 0xCD, 0xB4, 0x00, 0x23, 0x1F, 0x1B, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xCB, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x51, 0x47, 0x38, 0x00, 0x65, 0x65, 0x65, 0x00, 0x27, 0x27, 0x27, 0x00, 0xFF, 0xD6, 0x9C, 0x00,
		0xFF, 0xF2, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x81, 0x6F, 0x7B, 0x00, 0x32, 0x7D, 0x0F, 0x00,
		0xF5, 0x58, 0x00, 0x00, 0xE7, 0x0E, 0x72, 0x00, 0x92, 0x00, 0x90, 0x00, 0x42, 0xE3, 0x2C, 0xFF,
		0xFE, 0xD0, 0xB5, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xEC, 0xEC, 0x00, 0x00, 0x00, 0x00, 0xFF,
	};

	memcpy(&ui.theme, data, sizeof(UITheme));
	UIElementRepaint(&window->e, NULL);
	UIElementRepaint(&themeEditorWindow->e, NULL);
}

void ColorPresetPink(void *) {
	const uint8_t data[] = {
		0xF4, 0xF1, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7D, 0x78, 0x81, 0x00,
		0x00, 0x00, 0x00, 0x00, 0xED, 0xEA, 0xFF, 0x00, 0xF8, 0xF0, 0xFF, 0x00, 0xC5, 0xB6, 0xFB, 0x00,
		0xCD, 0xB4, 0xF7, 0x00, 0x1F, 0x1B, 0x23, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xFF, 0xFF, 0xFF, 0x00, 0xCB, 0xA0, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xD5, 0xBA, 0xEF, 0x00, 0xDF, 0xD5, 0xE0, 0x00, 0x9A, 0x89, 0x99, 0x00, 0xFF, 0xBA, 0xFC, 0x00,
		0xF2, 0xE8, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6F, 0x7B, 0x81, 0x00, 0x97, 0x46, 0x70, 0x00,
		0x40, 0x11, 0xC2, 0x00, 0x16, 0x67, 0xC7, 0x00, 0x92, 0x70, 0x7A, 0x00, 0xE3, 0x2B, 0x41, 0x00,
		0xD0, 0xB5, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xEC, 0xEC, 0x00, 0x00, 0x00, 0x00, 0xFF,
	};

	memcpy(&ui.theme, data, sizeof(UITheme));
	UIElementRepaint(&window->e, NULL);
	UIElementRepaint(&themeEditorWindow->e, NULL);
}

void CommandThemeEditor(void *) {
	if (themeEditorWindow) return;
	themeEditorSelectedColor = -1;
	themeEditorWindow = UIWindowCreate(0, 0, "Theme Editor", 0, 0);
	themeEditorWindow->scale = uiScale;
	themeEditorWindow->e.messageUser = ThemeEditorWindowMessage;
	UISplitPane *splitPane = UISplitPaneCreate(&themeEditorWindow->e, 0, 0.5f);
	UIPanel *panel = UIPanelCreate(&splitPane->e, UI_PANEL_GRAY);
	panel->border = UI_RECT_1(5);
	panel->gap = 5;
	themeEditorColorPicker = UIColorPickerCreate(&panel->e, 0);
	themeEditorColorPicker->e.messageUser = ThemeEditorColorPickerMessage;
	UISpacerCreate(&panel->e, 0, 10, 10);
	UIButtonCreate(&panel->e, 0, "Save theme", -1)->invoke = CommandSaveTheme;
	UIButtonCreate(&panel->e, 0, "Load theme", -1)->invoke = CommandLoadTheme;
	UISpacerCreate(&panel->e, UI_ELEMENT_H_FILL | UI_SPACER_LINE, 0, 1);
	UILabelCreate(&panel->e, UI_ELEMENT_H_FILL, "Presets:", -1);
	UIButtonCreate(&panel->e, 0, "Dark", -1)->invoke = ColorPresetDark;
	UIButtonCreate(&panel->e, 0, "Classic", -1)->invoke = ColorPresetClassic;
	UIButtonCreate(&panel->e, 0, "Ice", -1)->invoke = ColorPresetIce;
	UIButtonCreate(&panel->e, 0, "Lotus", -1)->invoke = ColorPresetPink;
	themeEditorTable = UITableCreate(&splitPane->e, 0, "Item\tColor");
	themeEditorTable->itemCount = sizeof(themeItems) / sizeof(themeItems[0]);
	themeEditorTable->e.messageUser = ThemeEditorTableMessage;
	UITableResizeColumns(themeEditorTable);
}

void DisassemblyLoad() {
	EvaluateCommand("disas");

	if (!strstr(evaluateResult, "Dump of assembler code for function")) {
		EvaluateCommand("disas $pc,+1000");
	}

	char *end = strstr(evaluateResult, "End of assembler dump.");

	if (!end) {
		printf("Disassembly failed. GDB output:\n%s\n", evaluateResult);
		return;
	}

	char *start = strstr(evaluateResult, ":\n");

	if (!start) {
		printf("Disassembly failed. GDB output:\n%s\n", evaluateResult);
		return;
	}

	start += 2;

	if (start >= end) {
		printf("Disassembly failed. GDB output:\n%s\n", evaluateResult);
		return;
	}

	char *pointer = strstr(start, "=> ");

	if (pointer) {
		pointer[0] = ' ';
		pointer[1] = ' ';
	}

	UICodeInsertContent(displayCode, start, end - start, true);
}

void DisassemblyUpdateLine() {
	EvaluateCommand("p $pc");
	char *address = strstr(evaluateResult, "0x");

	if (address) {
		char *addressEnd;
		uint64_t a = strtoul(address, &addressEnd, 0);

		for (int i = 0; i < 2; i++) {
			// Look for the line in the disassembly.

			bool found = false;

			for (int i = 0; i < displayCode->lineCount; i++) {
				uint64_t b = strtoul(displayCode->content + displayCode->lines[i].offset + 3, &addressEnd, 0);

				if (a == b) {
					UICodeFocusLine(displayCode, i + 1);
					autoPrintExpressionLine = i;
					found = true;
					break;
				}
			}

			if (!found) {
				// Reload the disassembly.
				DisassemblyLoad();
			} else {
				break;
			}
		}

		UIElementRefresh(&displayCode->e);
	}
}

void CommandToggleDisassembly(void *) {
	showingDisassembly = !showingDisassembly;
	autoPrintResultLine = 0;
	autoPrintExpression[0] = 0;
	displayCode->e.flags ^= UI_CODE_NO_MARGIN;

	if (showingDisassembly) {
		free(disassemblyPreviousSourceFile);
		disassemblyPreviousSourceFile = strdup(currentFile);
		disassemblyPreviousSourceLine = currentLine;

		UICodeInsertContent(displayCode, "Disassembly could not be loaded.\nPress Ctrl+D to return to source view.", -1, true);
		displayCode->tabSize = 8;
		DisassemblyLoad();
		DisassemblyUpdateLine();
	} else {
		currentLine = -1;
		currentFile[0] = 0;
		currentFileReadTime = 0;
		DisplaySetPosition(disassemblyPreviousSourceFile, disassemblyPreviousSourceLine, true);
		displayCode->tabSize = 4;
	}

	UIElementRefresh(&displayCode->e);
}

void CommandToggleFillDataTab(void *) {
	// HACK.

	static UIElement *oldParent;
	
	if (window->e.children == &dataTab->e) {
		window->e.children = &rootPanel->e;
		dataTab->e.parent = oldParent;
		buttonFillWindow->e.flags &= ~UI_BUTTON_CHECKED;
		UIElementRefresh(&window->e);
		UIElementRefresh(&rootPanel->e);
		UIElementRefresh(oldParent);
	} else {
		dataTab->e.flags &= ~UI_ELEMENT_HIDE;
		UIElementMessage(&dataTab->e, UI_MSG_TAB_SELECTED, 0, 0);
		window->e.children = &dataTab->e;
		oldParent = dataTab->e.parent;
		dataTab->e.parent = &window->e;
		buttonFillWindow->e.flags |= UI_BUTTON_CHECKED;
		UIElementRefresh(&window->e);
		UIElementRefresh(&dataTab->e);
	}
}

void InterfaceShowMenu(void *) {
	UIMenu *menu = UIMenuCreate(&buttonMenu->e, UI_MENU_PLACE_ABOVE);
	UIMenuAddItem(menu, 0, "Run\tShift+F5", -1, CommandSendToGDB, (void *) "r");
	UIMenuAddItem(menu, 0, "Run paused\tCtrl+F5", -1, CommandSendToGDB, (void *) "start");
	UIMenuAddItem(menu, 0, "Kill\tF3", -1, CommandSendToGDB, (void *) "kill");
	UIMenuAddItem(menu, 0, "Restart GDB\tCtrl+R", -1, CommandRestartGDB, NULL);
	UIMenuAddItem(menu, 0, "Connect\tF4", -1, CommandSendToGDB, (void *) "target remote :1234");
	UIMenuAddItem(menu, 0, "Continue\tF5", -1, CommandSendToGDB, (void *) "c");
	UIMenuAddItem(menu, 0, "Step over\tF10", -1, CommandSendToGDBStep, (void *) "n");
	UIMenuAddItem(menu, 0, "Step out of block\tShift+F10", -1, CommandStepOutOfBlock, NULL);
	UIMenuAddItem(menu, 0, "Step in\tF11", -1, CommandSendToGDBStep, (void *) "s");
	UIMenuAddItem(menu, 0, "Step out\tShift+F11", -1, CommandSendToGDB, (void *) "finish");
	UIMenuAddItem(menu, 0, "Pause\tF8", -1, CommandPause, NULL);
	UIMenuAddItem(menu, 0, "Toggle breakpoint\tF9", -1, CommandToggleBreakpoint, NULL);
	UIMenuAddItem(menu, 0, "Sync with gvim\tF2", -1, CommandSyncWithGvim, NULL);
	UIMenuAddItem(menu, 0, "Toggle disassembly\tCtrl+D", -1, CommandToggleDisassembly, NULL);
	UIMenuAddItem(menu, 0, "Theme editor", -1, CommandThemeEditor, NULL);
	UIMenuShow(menu);
}

void InterfaceRegisterShortcuts() {
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F5, .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "r" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F5, .ctrl = true, .invoke = CommandSendToGDB, .cp = (void *) "start" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F3, .invoke = CommandSendToGDB, .cp = (void *) "kill" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_LETTER('R'), .ctrl = true, .invoke = CommandRestartGDB, .cp = NULL });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F4, .invoke = CommandSendToGDB, .cp = (void *) "target remote :1234" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F5, .invoke = CommandSendToGDB, .cp = (void *) "c" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F10, .invoke = CommandSendToGDBStep, .cp = (void *) "n" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F10, .shift = true, .invoke = CommandStepOutOfBlock });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F11, .invoke = CommandSendToGDBStep, .cp = (void *) "s" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F11, .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "finish" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F8, .invoke = CommandPause, .cp = NULL });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F9, .invoke = CommandToggleBreakpoint, .cp = NULL });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F2, .invoke = CommandSyncWithGvim, .cp = NULL });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_LETTER('D'), .ctrl = true, .invoke = CommandToggleDisassembly, .cp = NULL });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_LETTER('B'), .ctrl = true, .invoke = CommandToggleFillDataTab, .cp = NULL });
}

void CommandCustom(void *_command) {
	const char *command = (const char *) _command;

	if (0 == memcmp(command, "shell ", 6)) {
		char buffer[4096];
		StringFormat(buffer, 4096, "Running shell command \"%s\"...\n", command);
		UICodeInsertContent(displayOutput, buffer, -1, false);
		StringFormat(buffer, 4096, "%s > .output.gf 2>&1", command);
		int start = time(NULL);
		int result = system(buffer);
		size_t bytes;
		char *output = LoadFile(".output.gf", &bytes);
		unlink(".output.gf");
		char *copy = (char *) malloc(bytes + 1);
		uintptr_t j = 0;

		for (uintptr_t i = 0; i <= bytes;) {
			if ((uint8_t) output[i] == 0xE2 && (uint8_t) output[i + 1] == 0x80
					&& ((uint8_t) output[i + 2] == 0x98 || (uint8_t) output[i + 2] == 0x99)) {
				copy[j++] = '\'';
				i += 3;
			} else {
				copy[j++] = output[i++];
			}
		}

		UICodeInsertContent(displayOutput, copy, j, false);
		free(output);
		free(copy);
		StringFormat(buffer, 4096, "(exit code: %d; time: %ds)\n", result, (int) (time(NULL) - start));
		UICodeInsertContent(displayOutput, buffer, -1, false);
		UIElementRefresh(&displayOutput->e);
	} else {
		DebuggerSend(command, true);
	}
}

bool INIParse(INIState *s) {
#define INI_READ(destination, counter, c1, c2) \
	s->destination = s->buffer, s->counter = 0; \
	while (s->bytes && *s->buffer != c1 && *s->buffer != c2) s->counter++, s->buffer++, s->bytes--; \
	if (s->bytes && *s->buffer == c1) s->buffer++, s->bytes--;

	while (s->bytes) {
		char c = *s->buffer;

		if (c == ' ' || c == '\n' || c == '\r') { 
			s->buffer++, s->bytes--; 
			continue;
		} else if (c == ';') {
			s->valueBytes = 0;
			INI_READ(key, keyBytes, '\n', 0);
		} else if (c == '[') {
			s->keyBytes = s->valueBytes = 0;
			s->buffer++, s->bytes--;
			INI_READ(section, sectionBytes, ']', 0);
		} else {
			INI_READ(key, keyBytes, '=', '\n');
			INI_READ(value, valueBytes, '\n', 0);
		}

		if (s->sectionBytes) s->section[s->sectionBytes] = 0; else s->section = &emptyString;
		if (s->keyBytes) s->key[s->keyBytes] = 0; else s->key = &emptyString;
		if (s->valueBytes) s->value[s->valueBytes] = 0; else s->value = &emptyString;

		return true;
	}

	return false;
}

void LoadSettings(bool earlyPass) {
	char globalConfigPath[4096];
	StringFormat(globalConfigPath, 4096, "%s/.config/gf2_config.ini", getenv("HOME"));

	for (int i = 0; i < 2; i++) {
		INIState state = { .buffer = LoadFile(i ? ".project.gf" : globalConfigPath, &state.bytes) };

		while (INIParse(&state)) {
			if (0 == strcmp(state.section, "shortcuts") && *state.key && !earlyPass) {
				UIShortcut shortcut = {};

				for (int i = 0; state.key[i]; i++) {
					state.key[i] = tolower(state.key[i]);
				}

				shortcut.ctrl = strstr(state.key, "ctrl+");
				shortcut.shift = strstr(state.key, "shift+");
				shortcut.alt = strstr(state.key, "alt+");
				shortcut.invoke = CommandCustom;
				shortcut.cp = state.value;

				const char *codeStart = state.key;

				for (int i = 0; state.key[i]; i++) {
					if (state.key[i] == '+') {
						codeStart = state.key + i + 1;
					}
				}

				for (int i = 0; i < 26; i++) {
					if (codeStart[0] == 'a' + i && codeStart[1] == 0) {
						shortcut.code = UI_KEYCODE_A + i;
					}
				}

				for (int i = 0; i < 10; i++) {
					if (codeStart[0] == '0' + i && codeStart[1] == 0) {
						shortcut.code = UI_KEYCODE_0 + i;
					}
				}

				if (0 == strcmp(codeStart, "F1"))  shortcut.code = UI_KEYCODE_F1;
				if (0 == strcmp(codeStart, "F2"))  shortcut.code = UI_KEYCODE_F2;
				if (0 == strcmp(codeStart, "F3"))  shortcut.code = UI_KEYCODE_F3;
				if (0 == strcmp(codeStart, "F4"))  shortcut.code = UI_KEYCODE_F4;
				if (0 == strcmp(codeStart, "F5"))  shortcut.code = UI_KEYCODE_F5;
				if (0 == strcmp(codeStart, "F6"))  shortcut.code = UI_KEYCODE_F6;
				if (0 == strcmp(codeStart, "F7"))  shortcut.code = UI_KEYCODE_F7;
				if (0 == strcmp(codeStart, "F8"))  shortcut.code = UI_KEYCODE_F8;
				if (0 == strcmp(codeStart, "F9"))  shortcut.code = UI_KEYCODE_F9;
				if (0 == strcmp(codeStart, "F10")) shortcut.code = UI_KEYCODE_F10;
				if (0 == strcmp(codeStart, "F11")) shortcut.code = UI_KEYCODE_F11;
				if (0 == strcmp(codeStart, "F12")) shortcut.code = UI_KEYCODE_F12;

				if (!shortcut.code) {
					fprintf(stderr, "Warning: Could not register shortcut for '%s'.\n", state.key);
				}

				UIWindowRegisterShortcut(window, shortcut);
			} else if (0 == strcmp(state.section, "ui") && earlyPass) {
				if (0 == strcmp(state.key, "font_size")) {
					fontSize = atoi(state.value);
				} else if (0 == strcmp(state.key, "scale")) {
					uiScale = atof(state.value);
				} else if (0 == strcmp(state.key, "layout")) {
					layoutString = state.value;
				}
			} else if (0 == strcmp(state.section, "gdb") && !earlyPass) {
				if (0 == strcmp(state.key, "argument")) {
					gdbArgc++;
					gdbArgv = (char **) realloc(gdbArgv, sizeof(char *) * (gdbArgc + 1));
					gdbArgv[gdbArgc - 1] = state.value;
					gdbArgv[gdbArgc] = nullptr;
				} else if (0 == strcmp(state.key, "path")) {
					gdbPath = state.value;
				}
			} else if (0 == strcmp(state.section, "commands") && earlyPass && state.keyBytes && state.valueBytes) {
				presetCommands.Add(state);
			}
		}
	}
}

const char *EvaluateExpression(const char *expression) {
	char buffer[1024];
	StringFormat(buffer, sizeof(buffer), "p %s", expression);
	EvaluateCommand(buffer);
	char *result = strchr(evaluateResult, '=');

	if (result) {
		char *end = strchr(result, '\n');

		if (end) {
			*end = 0;
			return result;
		}
	}

	return nullptr;
}

void InterfaceShowError(const char *message) {
	UICodeInsertContent(displayOutput, message, -1, false);
	UIElementRefresh(&displayOutput->e);
}

int BitmapViewerWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DESTROY) {
		free(element->cp);
	}
	
	return 0;
}

void BitmapViewerUpdate(const char *pointerString, const char *widthString, const char *heightString, const char *strideString, UIElement *owner = nullptr);

int BitmapViewerRefreshMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		BitmapViewer *bitmap = (BitmapViewer *) element->parent->cp;
		BitmapViewerUpdate(bitmap->pointer, bitmap->width, bitmap->height, bitmap->stride, element->parent);
	}

	return 0;
}

int BitmapViewerAutoMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		element->flags ^= UI_BUTTON_CHECKED;

		if (element->flags & UI_BUTTON_CHECKED) {
			autoUpdateBitmapViewers.Add(element->parent);
		} else {
			bool found = false;

			for (int i = 0; i < autoUpdateBitmapViewers.Length(); i++) {
				if (autoUpdateBitmapViewers[i] == element->parent) {
					autoUpdateBitmapViewers.DeleteSwap(i);
					found = true;
					break;
				}
			}

			assert(found);
		}
	}

	return 0;
}

void BitmapViewerUpdate(const char *pointerString, const char *widthString, const char *heightString, const char *strideString, UIElement *owner) {
	const char *widthResult = EvaluateExpression(widthString);
	if (!widthResult) { InterfaceShowError("Could not evaluate width.\n"); return; }
	int width = atoi(widthResult + 1);
	const char *heightResult = EvaluateExpression(heightString);
	if (!heightResult) { InterfaceShowError("Could not evaluate height.\n"); return; }
	int height = atoi(heightResult + 1);
	int stride = width * 4;
	const char *pointerResult = EvaluateExpression(pointerString);
	if (!pointerResult) { InterfaceShowError("Could not evaluate pointer.\n"); return; }
	char _pointerResult[1024];
	StringFormat(_pointerResult, sizeof(_pointerResult), "%s", pointerResult);
	pointerResult = strstr(_pointerResult, " 0x");
	if (!pointerResult) { InterfaceShowError("Pointer to image bits does not look like an address!\n"); return; }
	pointerResult++;

	if (strideString && *strideString) {
		const char *strideResult = EvaluateExpression(strideString);
		if (!strideResult) { InterfaceShowError("Could not evaluate stride.\n"); return; }
		stride = atoi(strideResult + 1);
	}

	uint32_t *bits = (uint32_t *) malloc(stride * height * 4);

	char buffer[1024];
	StringFormat(buffer, sizeof(buffer), "dump binary memory .bitmap.gf (%s) (%s+%d)", pointerResult, pointerResult, stride * height);
	EvaluateCommand(buffer);

	FILE *f = fopen(".bitmap.gf", "rb");

	if (f) {
		fread(bits, 1, stride * height * 4, f);
		fclose(f);
		unlink(".bitmap.gf");
	}

	if (!f || strstr(evaluateResult, "access")) {
		InterfaceShowError("Could not read the image bits!\n");
	} else {
		if (!owner) {
			BitmapViewer *bitmap = (BitmapViewer *) calloc(1, sizeof(BitmapViewer));
			strcpy(bitmap->pointer, pointerString);
			strcpy(bitmap->width, widthString);
			strcpy(bitmap->height, heightString);
			if (strideString) strcpy(bitmap->stride, strideString);

			UIMDIChild *window = UIMDIChildCreate(&dataWindow->e, UI_MDI_CHILD_CLOSE_BUTTON, UI_RECT_1(0), "Bitmap", -1);
			window->e.messageUser = BitmapViewerWindowMessage;
			window->e.cp = bitmap;
			bitmap->autoToggle = UIButtonCreate(&window->e, UI_BUTTON_SMALL | UI_ELEMENT_NON_CLIENT, "Auto", -1);
			bitmap->autoToggle->e.messageUser = BitmapViewerAutoMessage;
			UIButtonCreate(&window->e, UI_BUTTON_SMALL | UI_ELEMENT_NON_CLIENT, "Refresh", -1)->e.messageUser = BitmapViewerRefreshMessage;
			owner = &window->e;

			bitmap->display = UIImageDisplayCreate(owner, UI_IMAGE_DISPLAY_INTERACTIVE | UI_ELEMENT_H_FILL | UI_ELEMENT_V_FILL, bits, width, height, stride);
		}

		BitmapViewer *bitmap = (BitmapViewer *) owner->cp;
		UIImageDisplaySetContent(bitmap->display, bits, width, height, stride);
		UIElementRefresh(&bitmap->display->e);
		UIElementRefresh(owner);
		UIElementRefresh(&dataWindow->e);
	}

	free(bits);
}

void BitmapAddDialog(void *) {
	const char *result = UIDialogShow(window, 0, 
			"Add bitmap\n\n%l\n\nPointer to bits: (32bpp, RR GG BB AA)\n%t\nWidth:\n%t\nHeight:\n%t\nStride: (optional)\n%t\n\n%l\n\n%f%b%b",
			&addBitmapPointerString, &addBitmapWidthString, &addBitmapHeightString, &addBitmapStrideString, "Add", "Cancel");

	if (0 == strcmp(result, "Add")) {
		BitmapViewerUpdate(addBitmapPointerString, addBitmapWidthString, addBitmapHeightString, 
				(addBitmapStrideString && addBitmapStrideString[0]) ? addBitmapStrideString : nullptr);
	}
}

int TextboxInputMessage(UIElement *, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		static bool _lastKeyWasTab = false;
		static int consecutiveTabCount = 0;
		static int lastTabBytes = 0;
		bool lastKeyWasTab = _lastKeyWasTab;
		_lastKeyWasTab = false;

		if (m->code == UI_KEYCODE_ENTER && !window->shift) {
			char buffer[1024];
			StringFormat(buffer, 1024, "%.*s", (int) textboxInput->bytes, textboxInput->string);
			if (commandLog) fprintf(commandLog, "%s\n", buffer);
			DebuggerSend(buffer, true);

			char *string = (char *) malloc(textboxInput->bytes + 1);
			memcpy(string, textboxInput->string, textboxInput->bytes);
			string[textboxInput->bytes] = 0;
			commandHistory.Insert(string, 0);
			commandHistoryIndex = 0;

			if (commandHistory.Length() > 100) {
				free(commandHistory.Last());
				commandHistory.Pop();
			}

			UITextboxClear(textboxInput, false);
			UIElementRefresh(&textboxInput->e);

			return 1;
		} else if (m->code == UI_KEYCODE_TAB && textboxInput->bytes && !window->shift) {
			char buffer[4096];
			StringFormat(buffer, sizeof(buffer), "complete %.*s", lastKeyWasTab ? lastTabBytes : (int) textboxInput->bytes, textboxInput->string);
			for (int i = 0; buffer[i]; i++) if (buffer[i] == '\\') buffer[i] = ' ';
			EvaluateCommand(buffer);

			const char *start = evaluateResult;
			const char *end = strchr(evaluateResult, '\n');

			if (!lastKeyWasTab) {
				consecutiveTabCount = 0;
				lastTabBytes = textboxInput->bytes;
			}

			while (start && end && memcmp(start, textboxInput->string, lastTabBytes)) {
				start = end + 1;
				end = strchr(start, '\n');
			}

			for (int i = 0; end && i < consecutiveTabCount; i++) {
				start = end + 1;
				end = strchr(start, '\n');
			}

			if (!end) {
				consecutiveTabCount = 0;
				start = evaluateResult;
				end = strchr(evaluateResult, '\n');
			}

			_lastKeyWasTab = true;
			consecutiveTabCount++;

			if (end) {
				UITextboxClear(textboxInput, false);
				UITextboxReplace(textboxInput, start, end - start, false);
				UIElementRefresh(&textboxInput->e);
			}

			return 1;
		} else if (m->code == UI_KEYCODE_UP) {
			if (commandHistoryIndex < commandHistory.Length()) {
				UITextboxClear(textboxInput, false);
				UITextboxReplace(textboxInput, commandHistory[commandHistoryIndex], -1, false);
				if (commandHistoryIndex < commandHistory.Length() - 1) commandHistoryIndex++;
				UIElementRefresh(&textboxInput->e);
			}
		} else if (m->code == UI_KEYCODE_DOWN) {
			UITextboxClear(textboxInput, false);

			if (commandHistoryIndex > 0) {
				--commandHistoryIndex;
				UITextboxReplace(textboxInput, commandHistory[commandHistoryIndex], -1, false);
			}

			UIElementRefresh(&textboxInput->e);
		}
	}

	return 0;
}

int ModifiedRowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_PAINT) {
		UIDrawBorder((UIPainter *) dp, element->bounds, 0x00FF00, UI_RECT_1(2));
	}

	return 0;
}

int WatchTextboxMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_UPDATE) {
		if (element->window->focused != element) {
			UIElementDestroy(element);
			watchTextbox = nullptr;
		}
	}

	return 0;
}

void WatchDestroyTextbox() {
	if (!watchTextbox) return;
	UIElementDestroy(&watchTextbox->e);
	watchTextbox = nullptr;
	UIElementFocus(watchWindow);
}

void WatchFree(Watch *watch) {
	for (int i = 0; i < watch->fields.Length(); i++) {
		WatchFree(watch->fields[i]);
		if (!watch->isArray) free(watch->fields[i]);
	}

	if (watch->isArray) free(watch->fields[0]);
	free(watch->key);
	free(watch->value);
	free(watch->type);
	watch->fields.Free();
}

void WatchDeleteExpression() {
	WatchDestroyTextbox();
	if (watchSelectedRow == watchRows.Length()) return;
	int end = watchSelectedRow + 1;

	for (; end < watchRows.Length(); end++) {
		if (watchRows[watchSelectedRow]->depth >= watchRows[end]->depth) {
			break;
		}
	}

	bool found = false;
	Watch *w = watchRows[watchSelectedRow];

	for (int i = 0; i < watchBaseExpressions.Length(); i++) {
		if (w == watchBaseExpressions[i]) {
			found = true;
			watchBaseExpressions.Delete(i);
			break;
		}
	}

	assert(found);
	watchRows.Delete(watchSelectedRow, end - watchSelectedRow);
	WatchFree(w);
	free(w);
}

void WatchEvaluate(const char *function, Watch *watch) {
	char buffer[4096];
	uintptr_t position = 0;

	position += StringFormat(buffer + position, sizeof(buffer) - position, "py %s([", function);
	if (position > sizeof(buffer)) position = sizeof(buffer);

	Watch *stack[32];
	int stackCount = 0;
	stack[0] = watch;

	while (stack[stackCount]) {
		stack[stackCount + 1] = stack[stackCount]->parent;
		stackCount++;
		if (stackCount == 32) break;
	}

	bool first = true;

	while (stackCount) {
		stackCount--;

		if (!first) {
			position += StringFormat(buffer + position, sizeof(buffer) - position, ",");
			if (position > sizeof(buffer)) position = sizeof(buffer);
		} else {
			first = false;
		}

		if (stack[stackCount]->key) {
			position += StringFormat(buffer + position, sizeof(buffer) - position, "'%s'", stack[stackCount]->key);
		} else {
			position += StringFormat(buffer + position, sizeof(buffer) - position, "%lu", stack[stackCount]->arrayIndex);
		}

		if (position > sizeof(buffer)) position = sizeof(buffer);
	}

	position += StringFormat(buffer + position, sizeof(buffer) - position, "])");
	if (position > sizeof(buffer)) position = sizeof(buffer);

	EvaluateCommand(buffer);
}

bool WatchHasFields(Watch *watch) {
	WatchEvaluate("gf_fields", watch);

	if (strstr(evaluateResult, "(array)")) {
		return true;
	} else {
		char *position = evaluateResult;
		char *end = strchr(position, '\n');
		if (!end) return false;
		*end = 0;
		if (strstr(position, "(gdb)")) return false;
		return true;
	}
}

void WatchAddFields(Watch *watch) {
	if (watch->loadedFields) {
		return;
	}

	watch->loadedFields = true;

	WatchEvaluate("gf_fields", watch);

	if (strstr(evaluateResult, "(array)")) {
		int count = atoi(evaluateResult + 7) + 1;

		if (count > 10000000) {
			count = 10000000;
		}

		Watch *fields = (Watch *) calloc(count, sizeof(Watch));
		watch->isArray = true;
		bool hasSubFields = false;

		for (int i = 0; i < count; i++) {
			fields[i].parent = watch;
			fields[i].arrayIndex = i;
			watch->fields.Add(&fields[i]);
			if (!i) hasSubFields = WatchHasFields(&fields[i]);
			fields[i].hasFields = hasSubFields;
			fields[i].depth = watch->depth + 1;
		}
	} else {
		char *start = strdup(evaluateResult);
		char *position = start;

		while (true) {
			char *end = strchr(position, '\n');
			if (!end) break;
			*end = 0;
			if (strstr(position, "(gdb)")) break;
			Watch *field = (Watch *) calloc(1, sizeof(Watch));
			field->depth = watch->depth + 1;
			field->parent = watch;
			field->key = (char *) malloc(end - position + 1);
			strcpy(field->key, position);
			watch->fields.Add(field);
			field->hasFields = WatchHasFields(field);
			position = end + 1;
		}

		free(start);
	}
}

void WatchInsertFieldRows(Watch *watch, int *position) {
	for (int i = 0; i < watch->fields.Length(); i++) {
		watchRows.Insert(watch->fields[i], *position);
		*position = *position + 1;

		if (watch->fields[i]->open) {
			WatchInsertFieldRows(watch->fields[i], position);
		}
	}
}

void WatchEnsureRowVisible(int index) {
	if (watchSelectedRow < 0) watchSelectedRow = 0;
	else if (watchSelectedRow > watchRows.Length()) watchSelectedRow = watchRows.Length();
	UIScrollBar *scroll = ((UIPanel *) watchWindow->parent)->scrollBar;
	int rowHeight = (int) (UI_SIZE_TEXTBOX_HEIGHT * window->scale);
	int start = index * rowHeight, end = (index + 1) * rowHeight, height = UI_RECT_HEIGHT(watchWindow->parent->bounds);
	bool unchanged = false;
	if (end >= scroll->position + height) scroll->position = end - height;
	else if (start <= scroll->position) scroll->position = start;
	else unchanged = true;
	if (!unchanged) UIElementRefresh(watchWindow->parent);
}

void WatchAddExpression(char *string = nullptr) {
	if (!string && watchTextbox && !watchTextbox->bytes) {
		WatchDestroyTextbox();
		return;
	}

	Watch *watch = (Watch *) calloc(1, sizeof(Watch));

	if (string) {
		watch->key = string;
	} else {
		watch->key = (char *) malloc(watchTextbox->bytes + 1);
		watch->key[watchTextbox->bytes] = 0;
		memcpy(watch->key, watchTextbox->string, watchTextbox->bytes);
	}

	WatchDeleteExpression(); // Deletes textbox.
	watchRows.Insert(watch, watchSelectedRow);
	watchBaseExpressions.Add(watch);
	watchSelectedRow++;

	WatchEvaluate("gf_typeof", watch);

	if (!strstr(evaluateResult, "??")) {
		watch->type = strdup(evaluateResult);
		char *end = strchr(watch->type, '\n');
		if (end) *end = 0;
		watch->hasFields = WatchHasFields(watch);
	}
}

int WatchWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	int rowHeight = (int) (UI_SIZE_TEXTBOX_HEIGHT * element->window->scale);
	int result = 0;

	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;

		for (int i = (painter->clip.t - element->bounds.t) / rowHeight; i <= watchRows.Length(); i++) {
			UIRectangle row = element->bounds;
			row.t += i * rowHeight, row.b = row.t + rowHeight;

			UIRectangle intersection = UIRectangleIntersection(row, painter->clip);
			if (!UI_RECT_VALID(intersection)) break;

			bool focused = i == watchSelectedRow && element->window->focused == element;

			if (focused) UIDrawBlock(painter, row, ui.theme.tableSelected);
			UIDrawBorder(painter, row, ui.theme.border, UI_RECT_4(1, 1, 0, 1));

			row.l += UI_SIZE_TEXTBOX_MARGIN;
			row.r -= UI_SIZE_TEXTBOX_MARGIN;

			if (i != watchRows.Length()) {
				Watch *watch = watchRows[i];
				char buffer[256];

				if ((!watch->value || watch->updateIndex != watchUpdateIndex) && !watch->open) {
					free(watch->value);
					watch->updateIndex = watchUpdateIndex;
					WatchEvaluate("gf_valueof", watch);
					watch->value = strdup(evaluateResult);
					char *end = strchr(watch->value, '\n');
					if (end) *end = 0;
				}

				char keyIndex[64];

				if (!watch->key) {
					StringFormat(keyIndex, sizeof(keyIndex), "[%lu]", watch->arrayIndex);
				}

				StringFormat(buffer, sizeof(buffer), "%.*s%s%s%s%s", 
						watch->depth * 2, "                                ",
						watch->open ? "v " : watch->hasFields ? "> " : "", 
						watch->key ?: keyIndex, 
						watch->open ? "" : " = ", 
						watch->open ? "" : watch->value);

				if (focused) {
					UIDrawString(painter, row, buffer, -1, ui.theme.tableSelectedText, UI_ALIGN_LEFT, nullptr);
				} else {
					UIDrawStringHighlighted(painter, row, buffer, -1, 1);
				}
			}
		}
	} else if (message == UI_MSG_GET_HEIGHT) {
		return (watchRows.Length() + 1) * rowHeight;
	} else if (message == UI_MSG_LEFT_DOWN) {
		watchSelectedRow = (element->window->cursorY - element->bounds.t) / rowHeight;
		UIElementFocus(element);
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;
		result = 1;

		if ((m->code == UI_KEYCODE_ENTER || m->code == UI_KEYCODE_BACKSPACE) 
				&& watchSelectedRow != watchRows.Length() && !watchTextbox
				&& !watchRows[watchSelectedRow]->parent) {
			UIRectangle row = element->bounds;
			row.t += watchSelectedRow * rowHeight, row.b = row.t + rowHeight;
			watchTextbox = UITextboxCreate(element, 0);
			watchTextbox->e.messageUser = WatchTextboxMessage;
			UIElementMove(&watchTextbox->e, row, true);
			UIElementFocus(&watchTextbox->e);
			UITextboxReplace(watchTextbox, watchRows[watchSelectedRow]->key, -1, false);
		} else if (m->code == UI_KEYCODE_DELETE && !watchTextbox
				&& watchSelectedRow != watchRows.Length() && !watchRows[watchSelectedRow]->parent) {
			WatchDeleteExpression();
		} else if (m->textBytes && m->code != UI_KEYCODE_TAB && !watchTextbox && !window->ctrl && !window->alt
				&& (watchSelectedRow == watchRows.Length() || !watchRows[watchSelectedRow]->parent)) {
			UIRectangle row = element->bounds;
			row.t += watchSelectedRow * rowHeight, row.b = row.t + rowHeight;
			watchTextbox = UITextboxCreate(element, 0);
			watchTextbox->e.messageUser = WatchTextboxMessage;
			UIElementMove(&watchTextbox->e, row, true);
			UIElementFocus(&watchTextbox->e);
			UIElementMessage(&watchTextbox->e, message, di, dp);
		} else if (m->code == UI_KEYCODE_ENTER && watchTextbox) {
			WatchAddExpression();
		} else if (m->code == UI_KEYCODE_ESCAPE) {
			WatchDestroyTextbox();
		} else if (m->code == UI_KEYCODE_UP) {
			WatchDestroyTextbox();
			watchSelectedRow--;
		} else if (m->code == UI_KEYCODE_DOWN) {
			WatchDestroyTextbox();
			watchSelectedRow++;
		} else if (m->code == UI_KEYCODE_HOME) {
			watchSelectedRow = 0;
		} else if (m->code == UI_KEYCODE_END) {
			watchSelectedRow = watchRows.Length();
		} else if (m->code == UI_KEYCODE_RIGHT && !watchTextbox
				&& watchSelectedRow != watchRows.Length() && watchRows[watchSelectedRow]->hasFields
				&& !watchRows[watchSelectedRow]->open) {
			Watch *watch = watchRows[watchSelectedRow];
			watch->open = true;
			WatchAddFields(watch);
			int position = watchSelectedRow + 1;
			WatchInsertFieldRows(watch, &position);
			WatchEnsureRowVisible(position - 1);
		} else if (m->code == UI_KEYCODE_LEFT && !watchTextbox
				&& watchSelectedRow != watchRows.Length() && watchRows[watchSelectedRow]->hasFields
				&& watchRows[watchSelectedRow]->open) {
			int end = watchSelectedRow + 1;

			for (; end < watchRows.Length(); end++) {
				if (watchRows[watchSelectedRow]->depth >= watchRows[end]->depth) {
					break;
				}
			}

			watchRows.Delete(watchSelectedRow + 1, end - watchSelectedRow - 1);
			watchRows[watchSelectedRow]->open = false;
		} else if (m->code == UI_KEYCODE_LEFT && !watchTextbox 
				&& watchSelectedRow != watchRows.Length() && !watchRows[watchSelectedRow]->open) {
			for (int i = 0; i < watchRows.Length(); i++) {
				if (watchRows[watchSelectedRow]->parent == watchRows[i]) {
					watchSelectedRow = i;
					break;
				}
			}
		} else {
			result = 0;
		}

		WatchEnsureRowVisible(watchSelectedRow);
		UIElementRefresh(element->parent);
		UIElementRefresh(element);
	}

	if (watchSelectedRow < 0) {
		watchSelectedRow = 0;
	} else if (watchSelectedRow > watchRows.Length()) {
		watchSelectedRow = watchRows.Length();
	}

	return result;
}

int WatchPanelMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_LEFT_DOWN) {
		UIElementFocus(watchWindow);
		UIElementRepaint(watchWindow, NULL);
	}

	return 0;
}

void InterfaceUpdate(const char *data) {
	if (firstUpdate) {
		firstUpdate = false;
		EvaluateCommand(pythonCode);
	}

	// Get the current address.

	if (showingDisassembly) {
		DisassemblyUpdateLine();
	}

	// Get the stack.

	EvaluateCommand("bt 50");
	stack.Free();

	{
		const char *position = evaluateResult;

		while (*position == '#') {
			const char *next = position;

			while (true) {
				next = strchr(next + 1, '\n');
				if (!next || next[1] == '#') break;
			}

			if (!next) next = position + strlen(position);

			StackEntry entry = {};

			entry.id = strtoul(position + 1, (char **) &position, 0);

			while (*position == ' ' && position < next) position++;
			bool hasAddress = *position == '0';

			if (hasAddress) {
				entry.address = strtoul(position, (char **) &position, 0);
				position += 4;
			}

			while (*position == ' ' && position < next) position++;
			const char *functionName = position;
			position = strchr(position, ' ');
			if (!position || position >= next) break;
			StringFormat(entry.function, sizeof(entry.function), "%.*s", (int) (position - functionName), functionName);

			const char *file = strstr(position, " at ");

			if (file && file < next) {
				file += 4;
				const char *end = file;
				while (*end != '\n' && end < next) end++;
				StringFormat(entry.location, sizeof(entry.location), "%.*s", (int) (end - file), file);
			}

			stack.Add(entry);

			if (!(*next)) break;
			position = next + 1;
		}
	}

	tableStack->itemCount = stack.Length();
	UITableResizeColumns(tableStack);
	UIElementRefresh(&tableStack->e);

	// Set the file and line in the source display.

	bool changedSourceLine = false;

	{
		const char *line = data;

		while (*line) {
			if (line[0] == '\n' || line == data) {
				int i = line == data ? 0 : 1, number = 0;

				while (line[i]) {
					if (line[i] == '\t') {
						break;
					} else if (isdigit(line[i])) {
						number = number * 10 + line[i] - '0';
						i++;
					} else {
						goto tryNext;
					}
				}

				if (!line[i]) break;
				if (number) changedSourceLine = true;
				tryNext:;
				line += i + 1;
			} else {
				line++;
			}
		}
	}

	if (!stackChanged && changedSourceLine) stackSelected = 0;
	stackChanged = false;
	
	if (changedSourceLine && stack.Length() > stackSelected && strcmp(stack[stackSelected].location, previousLocation)) {
		char location[sizeof(previousLocation)];
		strcpy(previousLocation, stack[stackSelected].location);
		strcpy(location, stack[stackSelected].location);
		char *line = strchr(location, ':');
		if (line) *line = 0;
		DisplaySetPosition(location, line ? atoi(line + 1) : -1, true);
	}

	if (changedSourceLine && currentLine < displayCode->lineCount && currentLine > 0) {
		// If there is an auto-print expression from the previous line, evaluate it.

		if (autoPrintExpression[0]) {
			char buffer[1024];
			StringFormat(buffer, sizeof(buffer), "p %s", autoPrintExpression);
			EvaluateCommand(buffer);
			const char *result = strchr(evaluateResult, '=');

			if (result) {
				autoPrintResultLine = autoPrintExpressionLine;
				StringFormat(autoPrintResult, sizeof(autoPrintResult), "%s", result);
				char *end = strchr(autoPrintResult, '\n');
				if (end) *end = 0;
			} else {
				autoPrintResult[0] = 0;
			}

			autoPrintExpression[0] = 0;
		}

		// Parse the new source line.

		UICodeLine *line = displayCode->lines + currentLine - 1;
		const char *text = displayCode->content + line->offset;
		size_t bytes = line->bytes;
		uintptr_t position = 0;

		while (position < bytes) {
			if (text[position] != '\t') break;
			else position++;
		}

		uintptr_t expressionStart = position;

		{
			// Try to parse a type name.

			uintptr_t position2 = position;

			while (position2 < bytes) {
				char c = text[position2];
				if (!_UICharIsAlphaOrDigitOrUnderscore(c)) break;
				else position2++;
			}

			if (position2 == bytes) goto noTypeName;
			if (text[position2] != ' ') goto noTypeName;
			position2++;

			while (position2 < bytes) {
				if (text[position2] != '*') break;
				else position2++;
			}

			if (position2 == bytes) goto noTypeName;
			if (!_UICharIsAlphaOrDigitOrUnderscore(text[position2])) goto noTypeName;

			position = expressionStart = position2;
			noTypeName:;
		}

		while (position < bytes) {
			char c = text[position];
			if (!_UICharIsAlphaOrDigitOrUnderscore(c) && c != '[' && c != ']' && c != ' ' && c != '.' && c != '-' && c != '>') break;
			else position++;
		}

		uintptr_t expressionEnd = position;

		while (position < bytes) {
			if (text[position] != ' ') break;
			else position++;
		}

		if (position != bytes && text[position] == '=') {
			StringFormat(autoPrintExpression, sizeof(autoPrintExpression), "%.*s",
				(int) (expressionEnd - expressionStart), text + expressionStart);
		}

		autoPrintExpressionLine = currentLine;
	}

	// Get the list of breakpoints.

	EvaluateCommand("info break");
	breakpoints.Free();

	{
		const char *position = evaluateResult;

		while (true) {
			while (true) {
				position = strchr(position, '\n');
				if (!position || isdigit(position[1])) break;
				position++;
			}

			if (!position) break;

			const char *next = position;

			while (true) {
				next = strchr(next + 1, '\n');
				if (!next || isdigit(next[1])) break;
			}

			if (!next) next = position + strlen(position);

			const char *file = strstr(position, " at ");
			if (file) file += 4;

			Breakpoint breakpoint = {};
			bool recognised = true;

			if (file && file < next) {
				const char *end = strchr(file, ':');

				if (end && isdigit(end[1])) {
					if (file[0] == '.' && file[1] == '/') file += 2;
					StringFormat(breakpoint.file, sizeof(breakpoint.file), "%.*s", (int) (end - file), file);
					breakpoint.line = atoi(end + 1);
				} else recognised = false;
			} else recognised = false;

			if (recognised) {
				realpath(breakpoint.file, breakpoint.fileFull);
				breakpoints.Add(breakpoint);
			}

			position = next;
		}
	}

	tableBreakpoints->itemCount = breakpoints.Length();
	UITableResizeColumns(tableBreakpoints);
	UIElementRefresh(&tableBreakpoints->e);

	// Get registers.

	EvaluateCommand("info registers");

	if (!strstr(evaluateResult, "The program has no registers now.")
			&& !strstr(evaluateResult, "The current thread has terminated")) {
		UIElementDestroyDescendents(&registersWindow->e);
		char *position = evaluateResult;
		Array<RegisterData> newRegisterData = {};
		bool anyChanges = false;

		while (*position != '(') {
			char *nameStart = position;
			while (isspace(*nameStart)) nameStart++;
			char *nameEnd = position = strchr(nameStart, ' ');
			if (!nameEnd) break;
			char *format1Start = position;
			while (isspace(*format1Start)) format1Start++;
			char *format1End = position = strchr(format1Start, ' ');
			if (!format1End) break;
			char *format2Start = position;
			while (isspace(*format2Start)) format2Start++;
			char *format2End = position = strchr(format2Start, '\n');
			if (!format2End) break;

			char *stringStart = nameStart;
			char *stringEnd = format2End;

			RegisterData data;
			StringFormat(data.string, sizeof(data.string), "%.*s",
					(int) (stringEnd - stringStart), stringStart);
			bool modified = false;

			if (registerData.Length() > newRegisterData.Length()) {
				RegisterData *old = &registerData[newRegisterData.Length()];

				if (strcmp(old->string, data.string)) {
					modified = true;
				}
			}

			newRegisterData.Add(data);

			UIPanel *row = UIPanelCreate(&registersWindow->e, UI_PANEL_HORIZONTAL | UI_ELEMENT_H_FILL);
			if (modified) row->e.messageUser = ModifiedRowMessage;
			UILabelCreate(&row->e, 0, stringStart, stringEnd - stringStart);

			bool isPC = false;
			if (nameEnd == nameStart + 3 && 0 == memcmp(nameStart, "rip", 3)) isPC = true;
			if (nameEnd == nameStart + 3 && 0 == memcmp(nameStart, "eip", 3)) isPC = true;
			if (nameEnd == nameStart + 2 && 0 == memcmp(nameStart,  "ip", 2)) isPC = true;

			if (modified && showingDisassembly && !isPC) {
				if (!anyChanges) {
					autoPrintResult[0] = 0;
					autoPrintResultLine = autoPrintExpressionLine;
					anyChanges = true;
				} else {
					int position = strlen(autoPrintResult);
					StringFormat(autoPrintResult + position, sizeof(autoPrintResult) - position, ", ");
				}

				int position = strlen(autoPrintResult);
				StringFormat(autoPrintResult + position, sizeof(autoPrintResult) - position, "%.*s=%.*s",
						(int) (nameEnd - nameStart), nameStart,
						(int) (format1End - format1Start), format1Start);
			}
		}

		UIElementRefresh(&registersWindow->e);
		registerData.Free();
		registerData = newRegisterData;
	}

	// Update bitmap viewers.

	if (~dataTab->e.flags & UI_ELEMENT_HIDE) {
		for (int i = 0; i < autoUpdateBitmapViewers.Length(); i++) {
			BitmapViewer *bitmap = (BitmapViewer *) autoUpdateBitmapViewers[i]->cp;
			BitmapViewerUpdate(bitmap->pointer, bitmap->width, bitmap->height, bitmap->stride, autoUpdateBitmapViewers[i]);
		}
	} else if (autoUpdateBitmapViewers.Length()) {
		autoUpdateBitmapViewersQueued = true;
	}

	// Update watch display.

	for (int i = 0; i < watchBaseExpressions.Length(); i++) {
		Watch *watch = watchBaseExpressions[i];
		WatchEvaluate("gf_typeof", watch);
		char *result = strdup(evaluateResult);
		char *end = strchr(result, '\n');
		if (end) *end = 0;
		const char *oldType = watch->type ?: "??";

		if (strcmp(result, oldType)) {
			free(watch->type);
			watch->type = result;

			for (int j = 0; j < watchRows.Length(); j++) {
				if (watchRows[j] == watch) {
					watchSelectedRow = j;
					WatchAddExpression(strdup(watch->key));
					watchSelectedRow = watchRows.Length(), i--;
					break;
				}
			}
		} else {
			free(result);
		}
	}

	watchUpdateIndex++;
	UIElementRefresh(watchWindow->parent);
	UIElementRefresh(watchWindow);
}

int TableStackMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		m->isSelected = m->index == stackSelected;
		StackEntry *entry = &stack[m->index];

		if (m->column == 0) {
			return StringFormat(m->buffer, m->bufferBytes, "%d", entry->id);
		} else if (m->column == 1) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->function);
		} else if (m->column == 2) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->location);
		} else if (m->column == 3) {
			return StringFormat(m->buffer, m->bufferBytes, "0x%lX", entry->address);
		}
	} else if (message == UI_MSG_LEFT_DOWN || message == UI_MSG_MOUSE_DRAG) {
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY);

		if (index != -1 && stackSelected != index) {
			char buffer[64];
			StringFormat(buffer, 64, "frame %d", index);
			DebuggerSend(buffer, false);
			stackSelected = index;
			stackChanged = true;
			UIElementRepaint(element, NULL);
		}
	}

	return 0;
}

int TableBreakpointsMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		Breakpoint *entry = &breakpoints[m->index];

		if (m->column == 0) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->file);
		} else if (m->column == 1) {
			return StringFormat(m->buffer, m->bufferBytes, "%d", entry->line);
		}
	} else if (message == UI_MSG_RIGHT_DOWN) {
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY);

		if (index != -1) {
			UIMenu *menu = UIMenuCreate(&window->e, 0);
			UIMenuAddItem(menu, 0, "Delete", -1, CommandDeleteBreakpoint, (void *) (intptr_t) index);
			UIMenuShow(menu);
		}
	} else if (message == UI_MSG_LEFT_DOWN) {
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY);
		if (index != -1) DisplaySetPosition(breakpoints[index].file, breakpoints[index].line, false);
	}

	return 0;
}

int DisplayCodeMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED && !showingDisassembly) {
		int result = UICodeHitTest((UICode *) element, element->window->cursorX, element->window->cursorY);

		if (result < 0) {
			int line = -result;
			CommandToggleBreakpoint((void *) (intptr_t) line);
		} else if (result > 0) {
			int line = result;

			if (element->window->ctrl) {
				char buffer[1024];
				StringFormat(buffer, 1024, "until %d", line);
				DebuggerSend(buffer, true);
			} else if (element->window->alt) {
				char buffer[1024];
				StringFormat(buffer, 1024, "tbreak %d", line);
				EvaluateCommand(buffer);
				StringFormat(buffer, 1024, "jump %d", line);
				DebuggerSend(buffer, true);
			}
		}
	} else if (message == UI_MSG_CODE_GET_MARGIN_COLOR && !showingDisassembly) {
		for (int i = 0; i < breakpoints.Length(); i++) {
			if (breakpoints[i].line == di && 0 == strcmp(breakpoints[i].fileFull, currentFileFull)) {
				return 0xFF0000;
			}
		}
	} else if (message == UI_MSG_CODE_GET_LINE_HINT) {
		UITableGetItem *m = (UITableGetItem *) dp;

		if (m->index == autoPrintResultLine) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", autoPrintResult);
		}
	}

	return 0;
}

int DataTabMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TAB_SELECTED && autoUpdateBitmapViewersQueued) {
		// If we've switched to the data tab, we may need to update the bitmap viewers.

		for (int i = 0; i < autoUpdateBitmapViewers.Length(); i++) {
			BitmapViewer *bitmap = (BitmapViewer *) autoUpdateBitmapViewers[i]->cp;
			BitmapViewerUpdate(bitmap->pointer, bitmap->width, bitmap->height, bitmap->stride, autoUpdateBitmapViewers[i]);
		}

		autoUpdateBitmapViewersQueued = false;
	}

	return 0;
}

int TrafficLightMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_PAINT) {
		UIDrawRectangle((UIPainter *) dp, element->bounds, programRunning ? 0xFF0000 : 0x00FF00, ui.theme.border, UI_RECT_1(1));
	}

	return 0;
}

void CommandDonate(void *) {
	system("xdg-open https://www.patreon.com/nakst");
}

int WindowMessage(UIElement *, UIMessage message, int di, void *dp) {
	if (message == MSG_RECEIVED_DATA) {
		programRunning = false;
		char *input = (char *) dp;
		InterfaceUpdate(input);
		// printf("%s\n", input);
		UICodeInsertContent(displayOutput, input, -1, false);
		UIElementRefresh(&displayOutput->e);
		UIElementRepaint(&trafficLight->e, NULL);
		free(input);
	}

	return 0;
}

void CommandPreset(void *_index) {
	char *copy = strdup(presetCommands[(intptr_t) _index].value);
	char *position = copy;

	while (true) {
		char *end = strchr(position, ';');
		if (end) *end = 0;
		EvaluateCommand(position, true);
		UICodeInsertContent(displayOutput, evaluateResult, -1, false);
		if (end) position = end + 1;
		else break;
	}

	UIElementRefresh(&displayOutput->e);
	free(copy);
}

int TextboxStructNameMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		if (m->code == UI_KEYCODE_ENTER) {
			char buffer[4096];
			StringFormat(buffer, sizeof(buffer), "ptype /o %.*s", (int) textboxStructName->bytes, textboxStructName->string);
			EvaluateCommand(buffer);
			char *end = strstr(evaluateResult, "\n(gdb)");
			if (end) *end = 0;
			UICodeInsertContent(displayStruct, evaluateResult, -1, true);
			UITextboxClear(textboxStructName, false);
			UIElementRefresh(&displayStruct->e);
			UIElementRefresh(element);
			return 1;
		}
	}

	return 0;
}

bool FilesPanelPopulate();

void FilesOpen(void *_name) {
	const char *name = (const char *) _name;
	size_t oldLength = strlen(filesDirectory);
	strcat(filesDirectory, "/");
	strcat(filesDirectory, name);
	struct stat s;
	stat(filesDirectory, &s);

	if (S_ISDIR(s.st_mode)) {
		if (FilesPanelPopulate()) {
			char copy[PATH_MAX];
			realpath(filesDirectory, copy);
			strcpy(filesDirectory, copy);
			return;
		}
	} else if (S_ISREG(s.st_mode)) {
		DisplaySetPosition(filesDirectory, 1, false);
	}

	filesDirectory[oldLength] = 0;
}

bool FilesPanelPopulate() {
	if (!filesDirectory[0]) getcwd(filesDirectory, sizeof(filesDirectory));
	DIR *directory = opendir(filesDirectory);
	struct dirent *entry;
	if (!directory) return false;
	Array<char *> names = {};
	while ((entry = readdir(directory))) names.Add(strdup(entry->d_name));
	closedir(directory);
	UIElementDestroyDescendents(&panelFiles->e);

	qsort(names.array, names.Length(), sizeof(char *), [] (const void *a, const void *b) {
		return strcmp(*(const char **) a, *(const char **) b);
	});

	for (int i = 0; i < names.Length(); i++) {
		if (names[i][0] == '.' && names[i][1] == 0) continue;
		UIButton *button = UIButtonCreate(&panelFiles->e, 0, names[i], -1);
		button->e.cp = button->label;
		button->invoke = FilesOpen;
		free(names[i]);
	}

	names.Free();
	UIElementRefresh(&panelFiles->e);
	return true;
}

void InterfaceLayoutCreate(UIElement *parent) {
	UIElement *container = nullptr;

#define CHECK_LAYOUT_TOKEN(x) } else if (strlen(layoutString) > strlen(x) && 0 == memcmp(layoutString, x, strlen(x)) && \
		(layoutString[strlen(x)] == ',' || layoutString[strlen(x)] == ')')) { layoutString += strlen(x);

	if (layoutString[0] == 'h' && layoutString[1] == '(') {
		layoutString += 2;
		container = &UISplitPaneCreate(parent, UI_ELEMENT_V_FILL | UI_ELEMENT_H_FILL, strtol(layoutString, &layoutString, 10) * 0.01f)->e;
		layoutString++;
	} else if (layoutString[0] == 'v' && layoutString[1] == '(') {
		layoutString += 2;
		container = &UISplitPaneCreate(parent, UI_SPLIT_PANE_VERTICAL | UI_ELEMENT_V_FILL | UI_ELEMENT_H_FILL, strtol(layoutString, &layoutString, 10) * 0.01f)->e;
		layoutString++;
	} else if (layoutString[0] == 't' && layoutString[1] == '(') {
		layoutString += 2;
		char *copy = strdup(layoutString);
		for (uintptr_t i = 0; copy[i]; i++) if (copy[i] == ',') copy[i] = '\t'; else if (copy[i] == ')') copy[i] = 0;
		container = &UITabPaneCreate(parent, UI_ELEMENT_V_FILL | UI_ELEMENT_H_FILL, copy)->e;
	CHECK_LAYOUT_TOKEN("Source")
		displayCode = UICodeCreate(parent, 0);
		displayCode->e.messageUser = DisplayCodeMessage;
	CHECK_LAYOUT_TOKEN("Watch")
		UIPanel *watchPanel = UIPanelCreate(parent, UI_PANEL_SCROLL | UI_PANEL_GRAY);
		watchPanel->e.messageUser = WatchPanelMessage;
		watchWindow = UIElementCreate(sizeof(UIElement), &watchPanel->e, UI_ELEMENT_H_FILL | UI_ELEMENT_TAB_STOP, WatchWindowMessage, "Watch");
	CHECK_LAYOUT_TOKEN("Console")
		UIPanel *panel2 = UIPanelCreate(parent, UI_PANEL_EXPAND);
		displayOutput = UICodeCreate(&panel2->e, UI_CODE_NO_MARGIN | UI_ELEMENT_V_FILL);
		UIPanel *panel3 = UIPanelCreate(&panel2->e, UI_PANEL_HORIZONTAL | UI_PANEL_EXPAND | UI_PANEL_GRAY);
		panel3->border = UI_RECT_1(5);
		panel3->gap = 5;
		trafficLight = UISpacerCreate(&panel3->e, 0, 30, 30);
		trafficLight->e.messageUser = TrafficLightMessage;
		buttonMenu = UIButtonCreate(&panel3->e, 0, "Menu", -1);
		buttonMenu->invoke = InterfaceShowMenu;
		UIButtonCreate(&panel3->e, 0, "Donate", -1)->invoke = CommandDonate;
		textboxInput = UITextboxCreate(&panel3->e, UI_ELEMENT_H_FILL);
		textboxInput->e.messageUser = TextboxInputMessage;
		UIElementFocus(&textboxInput->e);
	CHECK_LAYOUT_TOKEN("Breakpoints")
		tableBreakpoints = UITableCreate(parent, 0, "File\tLine");
		tableBreakpoints->e.messageUser = TableBreakpointsMessage;
	CHECK_LAYOUT_TOKEN("Stack")
		tableStack = UITableCreate(parent, 0, "Index\tFunction\tLocation\tAddress");
		tableStack->e.messageUser = TableStackMessage;
	CHECK_LAYOUT_TOKEN("Registers")
		registersWindow = UIPanelCreate(parent, UI_PANEL_SMALL_SPACING | UI_PANEL_GRAY | UI_PANEL_SCROLL);
	CHECK_LAYOUT_TOKEN("Data")
		dataTab = UIPanelCreate(parent, UI_PANEL_EXPAND);
		UIPanel *panel5 = UIPanelCreate(&dataTab->e, UI_PANEL_GRAY | UI_PANEL_HORIZONTAL | UI_PANEL_SMALL_SPACING);
		buttonFillWindow = UIButtonCreate(&panel5->e, UI_BUTTON_SMALL, "Fill window", -1);
		buttonFillWindow->invoke = CommandToggleFillDataTab;
		UIButtonCreate(&panel5->e, UI_BUTTON_SMALL, "Add bitmap...", -1)->invoke = BitmapAddDialog;
		dataWindow = UIMDIClientCreate(&dataTab->e, UI_ELEMENT_V_FILL);
		dataTab->e.messageUser = DataTabMessage;
	CHECK_LAYOUT_TOKEN("Commands")
		panelPresetCommands = UIPanelCreate(parent, UI_PANEL_GRAY | UI_PANEL_SMALL_SPACING | UI_PANEL_EXPAND | UI_PANEL_SCROLL);
		if (!presetCommands.Length()) UILabelCreate(&panelPresetCommands->e, 0, "No preset commands found in config file!", -1);

		for (int i = 0; i < presetCommands.Length(); i++) {
			UIButton *button = UIButtonCreate(&panelPresetCommands->e, 0, presetCommands[i].key, -1);
			button->e.cp = (void *) (intptr_t) i;
			button->invoke = CommandPreset;
		}
	CHECK_LAYOUT_TOKEN("Struct")
		UIPanel *panel6 = UIPanelCreate(parent, UI_PANEL_GRAY | UI_PANEL_EXPAND);
		textboxStructName = UITextboxCreate(&panel6->e, 0);
		textboxStructName->e.messageUser = TextboxStructNameMessage;
		displayStruct = UICodeCreate(&panel6->e, UI_ELEMENT_V_FILL | UI_CODE_NO_MARGIN);
		UICodeInsertContent(displayStruct, "Type the name of a struct\nto view its layout.", -1, false);
	CHECK_LAYOUT_TOKEN("Files")
		panelFiles = UIPanelCreate(parent, UI_PANEL_GRAY | UI_PANEL_EXPAND | UI_PANEL_SCROLL);
		FilesPanelPopulate();
	} else {
		assert(false);
	}

	while (container) {
		if (layoutString[0] == ',') {
			layoutString++;
		} else if (layoutString[0] == ')') {
			layoutString++;
			return;
		} else {
			InterfaceLayoutCreate(container);
		}
	}
}

extern "C" void InterfaceCreate(UIWindow *_window) {
#ifdef LOG_COMMANDS
	{
		char path[4096];
		StringFormat(path, sizeof(path), "%s/gf_log.txt", getenv("HOME"));
		commandLog = fopen(path, "ab");
	}
#endif

	window = _window;
	window->scale = uiScale;
	window->e.messageUser = WindowMessage;

	rootPanel = UIPanelCreate(&window->e, UI_PANEL_EXPAND);
	InterfaceLayoutCreate(&rootPanel->e);

	InterfaceRegisterShortcuts();
	LoadSettings(false);
	CommandLoadTheme(NULL);

	pthread_cond_init(&evaluateEvent, NULL);
	pthread_mutex_init(&evaluateMutex, NULL);
	DebuggerStartThread();
}

extern "C" void DebuggerClose() {
	kill(gdbPID, SIGKILL);
	pthread_cancel(gdbThread);
}

void SignalINT(int sig) {
	DebuggerClose();
	exit(0);
}

#ifndef EMBED_GF
int main(int argc, char **argv) {
	struct sigaction sigintHandler = {};
	sigintHandler.sa_handler = SignalINT;
	sigaction(SIGINT, &sigintHandler, NULL);

	// Setup GDB arguments.
	gdbArgv = (char **) malloc(sizeof(char *) * (argc + 1));
	gdbArgv[0] = (char *) "gdb";
	memcpy(gdbArgv + 1, argv + 1, sizeof(argv) * argc);
	gdbArgc = argc;

	LoadSettings(true);
	UIInitialise();
	InterfaceCreate(UIWindowCreate(0, 0, "gf2", 0, 0));
	CommandSyncWithGvim(NULL);
	UIMessageLoop();
	DebuggerClose();

	return 0;
}
#endif
