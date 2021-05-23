// Build with: g++ gf2.cpp -lX11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-truncation -o gf2 -g -pthread
// Add the following flags to use FreeType: -lfreetype -D UI_FREETYPE -I /usr/include/freetype2 -D UI_FONT_PATH=/usr/share/fonts/TTF/DejaVuSansMono.ttf

// TODO Disassembly window extensions.
// 	- Split source and disassembly view.
// 	- Setting/clearing/showing breakpoints.
// 	- Jump to line and run to line.
// 	- Shift+F10: run to next instruction (for skipping past loops).

// TODO Data window extensions.
// 	- View data windows in the whole of the main window.
// 	- Use error dialogs for bitmap errors.
// 	- Copy bitmap to clipboard, or save to file.

// TODO Future extensions.
// 	- Watch window.
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

int fontSize = 13;
float uiScale = 1;

extern "C" {
#define UI_FONT_SIZE (fontSize)
#define UI_LINUX
#define UI_IMPLEMENTATION
#include "luigi.h"
}

#define MSG_RECEIVED_DATA ((UIMessage) (UI_MSG_USER + 1))

FILE *commandLog;
char emptyString;

// Current file and line:

char currentFile[4096];
int currentLine;
time_t currentFileReadTime;

bool showingDisassembly;
char *disassemblyPreviousSourceFile;
int disassemblyPreviousSourceLine;

// User interface:

UIWindow *window;
UICode *displayCode;
UICode *displayOutput;
UITable *tableBreakpoints;
UITable *tableStack;
UITextbox *textboxInput;
UIButton *buttonMenu;
UISpacer *trafficLight;
UIMDIClient *dataWindow;
UITabPane *tabPaneWatchData;
UIPanel *registersWindow;

// Theme editor:

UIWindow *themeEditorWindow;
UIColorPicker *themeEditorColorPicker;
UITable *themeEditorTable;
int themeEditorSelectedColor;

// Bitmap viewer:

struct BitmapViewer {
	char pointer[256];
	char width[256];
	char height[256];
	char stride[256];
};

UIWindow *addBitmapDialog;
UITextbox *addBitmapPointer;
UITextbox *addBitmapWidth;
UITextbox *addBitmapHeight;
UITextbox *addBitmapStride;
char addBitmapPointerString[256];
char addBitmapWidthString[256];
char addBitmapHeightString[256];
char addBitmapStrideString[256];

// Dynamic arrays:

template <class T>
struct Array {
	T *array;
	size_t length;

	inline void Insert(T item, uintptr_t index) {
		length++;
		array = (T *) realloc(array, length * sizeof(T));
		memmove(array + index + 1, array + index, (length - index - 1) * sizeof(T));
		array[index] = item;
	}

	inline void Add(T item) { Insert(item, length); }
	inline void Free() { free(array); array = nullptr; length = 0; }
	inline int Length() { return length; }
	inline T &First() { return array[0]; }
	inline T &Last() { return array[length - 1]; }
	inline T &operator[](uintptr_t index) { return array[index]; }
	inline void Pop() { length--; }
};

// Call stack:

struct StackEntry {
	char function[64];
	char location[64];
	uint64_t address;
	int id;
};

Array<StackEntry> stack;
int stackSelected;
bool stackJustSelected;

// Breakpoints:

struct Breakpoint {
	char file[64];
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

// Registers:

struct RegisterData {
	char string[128];
};

Array<RegisterData> registerData;

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

	// char *const argv[] = { (char *) "gdb", NULL };
	posix_spawn_file_actions_t actions = {};
	posix_spawn_file_actions_adddup2(&actions, inputPipe[0],  0);
	posix_spawn_file_actions_adddup2(&actions, outputPipe[1], 1);
	posix_spawn_file_actions_adddup2(&actions, outputPipe[1], 2);

	posix_spawnattr_t attrs = {};
	posix_spawnattr_init(&attrs);
	posix_spawnattr_setflags(&attrs, POSIX_SPAWN_SETSID);

	posix_spawnp((pid_t *) &gdbPID, "gdb", &actions, &attrs, gdbArgv, environ);

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

void StartGDBThread() {
	pthread_t debuggerThread;
	pthread_attr_t attributes;
	pthread_attr_init(&attributes);
	pthread_create(&debuggerThread, &attributes, DebuggerThread, NULL);
	gdbThread = debuggerThread;
}

void SendToGDB(const char *string, bool echo) {
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

void EvaluateCommand(const char *command) {
	if (programRunning) {
		kill(gdbPID, SIGINT);
		usleep(1000 * 1000);
		programRunning = false;
	}

	evaluateMode = true;
	pthread_mutex_lock(&evaluateMutex);
	SendToGDB(command, false);
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

extern "C" const char *GetPosition(int *line) {
	*line = currentLine;
	return currentFile;
}

extern "C" bool SetPosition(const char *file, int line, bool useGDBToGetFullPath) {
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
		snprintf(buffer, sizeof(buffer), "%s/%s", getenv("HOME"), 1 + file);
		file = buffer;
	} else if (file && file[0] != '/' && useGDBToGetFullPath) {
		EvaluateCommand("info source");
		const char *f = strstr(evaluateResult, "Located in ");

		if (f) {
			f += 11;
			const char *end = strchr(f, '\n');

			if (end) {
				snprintf(buffer, sizeof(buffer), "%.*s", (int) (end - f), f);
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
		snprintf(currentFile, 4096, "%s", originalFile);

		printf("attempting to load '%s' (from '%s')\n", file, originalFile);

		size_t bytes;
		char *buffer2 = LoadFile(file, &bytes);

		if (!buffer2) {
			char buffer3[4096];
			snprintf(buffer3, 4096, "The file '%s' (from '%s') could not be loaded.", file, originalFile);
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
	SendToGDB((const char *) _string, true);
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

	SendToGDB(command, true);
}

void CommandDeleteBreakpoint(void *_index) {
	int index = (int) (intptr_t) _index;
	Breakpoint *breakpoint = &breakpoints[index];
	char buffer[1024];
	snprintf(buffer, 1024, "clear %s:%d", breakpoint->file, breakpoint->line);
	SendToGDB(buffer, true);
}

void CommandRestartGDB(void *) {
	kill(gdbPID, SIGKILL);
	pthread_cancel(gdbThread); // TODO Is there a nicer way to do this?
	receiveBufferPosition = 0;
	StartGDBThread();
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
			SetPosition(name, lineNumber, false);
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
		if (breakpoints[i].line == line && 0 == strcmp(breakpoints[i].file, currentFile)) {
			char buffer[1024];
			snprintf(buffer, 1024, "clear %s:%d", currentFile, line);
			SendToGDB(buffer, true);
			return;
		}
	}

	char buffer[1024];
	snprintf(buffer, 1024, "b %s:%d", currentFile, line);
	SendToGDB(buffer, true);
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
	snprintf(buffer, 4096, "%s/.config/gf2_theme.dat", getenv("HOME"));
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
	snprintf(buffer, 4096, "%s/.config/gf2_theme.dat", getenv("HOME"));
	FILE *f = fopen(buffer, "wb");

	if (f) {
		fwrite(&ui.theme, 1, sizeof(ui.theme), f);
		fclose(f);
	}
}

void CommandLoadDefaultColors(void *) {
	ui.theme = _uiThemeDark;
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
	UIButtonCreate(&panel->e, 0, "Load default colors", -1)->invoke = CommandLoadDefaultColors;
	themeEditorTable = UITableCreate(&splitPane->e, 0, "Item\tColor");
	themeEditorTable->itemCount = sizeof(themeItems) / sizeof(themeItems[0]);
	themeEditorTable->e.messageUser = ThemeEditorTableMessage;
	UITableResizeColumns(themeEditorTable);
}

void LoadDisassembly() {
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

void UpdateDisassemblyLine() {
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
				LoadDisassembly();
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
		LoadDisassembly();
		UpdateDisassemblyLine();
	} else {
		currentLine = -1;
		currentFile[0] = 0;
		currentFileReadTime = 0;
		SetPosition(disassemblyPreviousSourceFile, disassemblyPreviousSourceLine, true);
		displayCode->tabSize = 4;
	}

	UIElementRefresh(&displayCode->e);
}

void ShowMenu(void *) {
	UIMenu *menu = UIMenuCreate(&buttonMenu->e, UI_MENU_PLACE_ABOVE);
	UIMenuAddItem(menu, 0, "Run\tShift+F5", -1, CommandSendToGDB, (void *) "r");
	UIMenuAddItem(menu, 0, "Run paused\tCtrl+F5", -1, CommandSendToGDB, (void *) "start");
	UIMenuAddItem(menu, 0, "Kill\tF3", -1, CommandSendToGDB, (void *) "kill");
	UIMenuAddItem(menu, 0, "Restart GDB\tCtrl+R", -1, CommandRestartGDB, NULL);
	UIMenuAddItem(menu, 0, "Connect\tF4", -1, CommandSendToGDB, (void *) "target remote :1234");
	UIMenuAddItem(menu, 0, "Continue\tF5", -1, CommandSendToGDB, (void *) "c");
	UIMenuAddItem(menu, 0, "Step over\tF10", -1, CommandSendToGDBStep, (void *) "n");
	UIMenuAddItem(menu, 0, "Step in\tF11", -1, CommandSendToGDBStep, (void *) "s");
	UIMenuAddItem(menu, 0, "Step out\tShift+F11", -1, CommandSendToGDB, (void *) "finish");
	UIMenuAddItem(menu, 0, "Pause\tF8", -1, CommandPause, NULL);
	UIMenuAddItem(menu, 0, "Toggle breakpoint\tF9", -1, CommandToggleBreakpoint, NULL);
	UIMenuAddItem(menu, 0, "Sync with gvim\tF2", -1, CommandSyncWithGvim, NULL);
	UIMenuAddItem(menu, 0, "Toggle disassembly\tCtrl+D", -1, CommandToggleDisassembly, NULL);
	UIMenuAddItem(menu, 0, "Theme editor", -1, CommandThemeEditor, NULL);
	UIMenuShow(menu);
}

void RegisterShortcuts() {
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F5, .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "r" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F5, .ctrl = true, .invoke = CommandSendToGDB, .cp = (void *) "start" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F3, .invoke = CommandSendToGDB, .cp = (void *) "kill" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_LETTER('R'), .ctrl = true, .invoke = CommandRestartGDB, .cp = NULL });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F4, .invoke = CommandSendToGDB, .cp = (void *) "target remote :1234" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F5, .invoke = CommandSendToGDB, .cp = (void *) "c" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F10, .invoke = CommandSendToGDBStep, .cp = (void *) "n" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F11, .invoke = CommandSendToGDBStep, .cp = (void *) "s" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F11, .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "finish" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F8, .invoke = CommandPause, .cp = NULL });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F9, .invoke = CommandToggleBreakpoint, .cp = NULL });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F2, .invoke = CommandSyncWithGvim, .cp = NULL });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_LETTER('D'), .ctrl = true, .invoke = CommandToggleDisassembly, .cp = NULL });
}

int RunSystemWithOutput(const char *command) {
	char buffer[4096];
	snprintf(buffer, 4096, "Running shell command \"%s\"...\n", command);
	UICodeInsertContent(displayOutput, buffer, -1, false);
	snprintf(buffer, 4096, "%s > .output.gf 2>&1", command);
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
	snprintf(buffer, 4096, "(exit code: %d; time: %ds)\n", result, (int) (time(NULL) - start));
	UICodeInsertContent(displayOutput, buffer, -1, false);
	UIElementRefresh(&displayOutput->e);
	return result;
}

void CustomCommand(void *_command) {
	const char *command = (const char *) _command;

	if (0 == memcmp(command, "shell ", 6)) {
		RunSystemWithOutput(command + 6);
	} else {
		SendToGDB(command, true);
	}
}

struct INIState {
	char *buffer, *section, *key, *value;
	size_t bytes, sectionBytes, keyBytes, valueBytes;
};

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
	snprintf(globalConfigPath, 4096, "%s/.config/gf2_config.ini", getenv("HOME"));

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
				shortcut.invoke = CustomCommand;
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
				}
			} else if (0 == strcmp(state.section, "gdb") && !earlyPass) {
				if (0 == strcmp(state.key, "argument")) {
					gdbArgc++;
					gdbArgv = (char **) realloc(gdbArgv, sizeof(char *) * (gdbArgc + 1));
					gdbArgv[gdbArgc - 1] = state.value;
					gdbArgv[gdbArgc] = nullptr;
				}
			}
		}
	}
}

const char *EvaluateExpression(const char *expression) {
	char buffer[1024];
	snprintf(buffer, sizeof(buffer), "p %s", expression);
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

void PrintErrorMessage(const char *message) {
	UICodeInsertContent(displayOutput, message, -1, false);
	UIElementRefresh(&displayOutput->e);
}

int BitmapViewerWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DESTROY) {
		free(element->cp);
	}
	
	return 0;
}

void AddBitmapInternal(const char *pointerString, const char *widthString, const char *heightString, const char *strideString, UIElement *owner = nullptr);

int BitmapViewerRefreshMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		BitmapViewer *bitmap = (BitmapViewer *) element->parent->cp;
		AddBitmapInternal(bitmap->pointer, bitmap->width, bitmap->height, bitmap->stride, element->parent);
	}

	return 0;
}

void AddBitmapInternal(const char *pointerString, const char *widthString, const char *heightString, const char *strideString, UIElement *owner) {
	const char *widthResult = EvaluateExpression(widthString);
	if (!widthResult) { PrintErrorMessage("Could not evaluate width.\n"); return; }
	int width = atoi(widthResult + 1);
	const char *heightResult = EvaluateExpression(heightString);
	if (!heightResult) { PrintErrorMessage("Could not evaluate height.\n"); return; }
	int height = atoi(heightResult + 1);
	int stride = width * 4;
	const char *pointerResult = EvaluateExpression(pointerString);
	if (!pointerResult) { PrintErrorMessage("Could not evaluate pointer.\n"); return; }
	char _pointerResult[1024];
	snprintf(_pointerResult, sizeof(_pointerResult), "%s", pointerResult);
	pointerResult = strstr(_pointerResult, " 0x");
	if (!pointerResult) { PrintErrorMessage("Pointer to image bits does not look like an address!\n"); return; }
	pointerResult++;

	if (strideString && *strideString) {
		const char *strideResult = EvaluateExpression(strideString);
		if (!strideResult) { PrintErrorMessage("Could not evaluate stride.\n"); return; }
		stride = atoi(strideResult + 1);
	}

	uint32_t *bits = (uint32_t *) malloc(stride * height * 4);

	char buffer[1024];
	snprintf(buffer, sizeof(buffer), "dump binary memory .bitmap.gf (%s) (%s+%d)", pointerResult, pointerResult, stride * height);
	EvaluateCommand(buffer);

	FILE *f = fopen(".bitmap.gf", "rb");

	if (f) {
		fread(bits, 1, stride * height * 4, f);
		fclose(f);
		unlink(".bitmap.gf");
	}

	if (!f || strstr(evaluateResult, "access")) {
		PrintErrorMessage("Could not read the image bits!\n");
	} else {
		BitmapViewer *bitmap = (BitmapViewer *) calloc(1, sizeof(BitmapViewer));
		strcpy(bitmap->pointer, pointerString);
		strcpy(bitmap->width, widthString);
		strcpy(bitmap->height, heightString);
		if (strideString) strcpy(bitmap->stride, strideString);

		if (!owner) {
			UIMDIChild *window = UIMDIChildCreate(&dataWindow->e, UI_MDI_CHILD_CLOSE_BUTTON, UI_RECT_1(0), "Bitmap", -1);
			window->e.messageUser = BitmapViewerWindowMessage;
			window->e.cp = bitmap;
			UIButtonCreate(&window->e, UI_BUTTON_SMALL | UI_ELEMENT_NON_CLIENT, "Refresh", -1)->e.messageUser = BitmapViewerRefreshMessage;
			owner = &window->e;
		} else {
			free(owner->cp);
			owner->cp = bitmap;
			UIElementDestroyDescendents(owner);
		}

		UIImageDisplayCreate(owner, 0, bits, width, height, stride);
		UIElementRefresh(owner);
		UIElementRefresh(&dataWindow->e);
	}

	free(bits);
}

void AddDataWindow() {
	tabPaneWatchData->active = 2;
	UIElementRefresh(&tabPaneWatchData->e);

	char buffer[1024];
	snprintf(buffer, 1024, "%.*s", (int) textboxInput->bytes, textboxInput->string);

	char *tokens[16];
	size_t tokenCount = 0;

	for (int i = 0; buffer[i]; i++) {
		if (buffer[i] == ' ') {
			buffer[i] = 0;
		}

		if ((!i || (i && buffer[i - 1] == 0)) && buffer[i] != 0 && tokenCount != 16) {
			tokens[tokenCount++] = buffer + i;
		}
	}

	if (tokenCount < 1) {
		return;
	}

	if (0 == strcmp(tokens[0], "bitmap")) {
		if (tokenCount != 5 && tokenCount != 4) {
			PrintErrorMessage("Usage: bitmap [pointer] [width] [height] [stride]\n");
			return;
		} else {
			AddBitmapInternal(tokens[1], tokens[2], tokens[3], tokenCount == 5 ? tokens[4] : nullptr);
		}
	} else {
		PrintErrorMessage("Unknown command.\n");
		return;
	}

	UITextboxClear(textboxInput, false);
	UIElementRefresh(&textboxInput->e);
}

int AddBitmapDialogMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_WINDOW_CLOSE) {
		UIElementDestroy(element);
		addBitmapDialog = NULL;
		return 1;
	}

	return 0;
}

void AddBitmap(void *) {
	snprintf(addBitmapPointerString, sizeof(addBitmapPointerString), "%.*s", (int) addBitmapPointer->bytes, addBitmapPointer->string);
	snprintf(addBitmapWidthString, sizeof(addBitmapWidthString), "%.*s", (int) addBitmapWidth->bytes, addBitmapWidth->string);
	snprintf(addBitmapHeightString, sizeof(addBitmapHeightString), "%.*s", (int) addBitmapHeight->bytes, addBitmapHeight->string);
	snprintf(addBitmapStrideString, sizeof(addBitmapStrideString), "%.*s", (int) addBitmapStride->bytes, addBitmapStride->string);
	AddBitmapInternal(addBitmapPointerString, addBitmapWidthString, addBitmapHeightString, addBitmapStrideString[0] ? addBitmapStrideString : nullptr);
	UIElementDestroy(&addBitmapDialog->e);
	addBitmapDialog = nullptr;
}

void AddBitmapDialog(void *) {
	if (addBitmapDialog) return;
	addBitmapDialog = UIWindowCreate(0, 0, "Add Bitmap", 0, 0);
	addBitmapDialog->scale = uiScale;
	addBitmapDialog->e.messageUser = AddBitmapDialogMessage;

	UIPanelCreate(&addBitmapDialog->e, UI_PANEL_GRAY | UI_ELEMENT_PARENT_PUSH | UI_PANEL_MEDIUM_SPACING);
	UILabelCreate(0, UI_ELEMENT_H_FILL, "Pointer to bits: (32bpp, RR GG BB AA)", -1);
	addBitmapPointer = UITextboxCreate(0, 0);
	UITextboxReplace(addBitmapPointer, addBitmapPointerString, -1, false);
	UILabelCreate(0, UI_ELEMENT_H_FILL, "Width:", -1);
	addBitmapWidth = UITextboxCreate(0, 0);
	UITextboxReplace(addBitmapWidth, addBitmapWidthString, -1, false);
	UILabelCreate(0, UI_ELEMENT_H_FILL, "Height:", -1);
	addBitmapHeight = UITextboxCreate(0, 0);
	UITextboxReplace(addBitmapHeight, addBitmapHeightString, -1, false);
	UILabelCreate(0, UI_ELEMENT_H_FILL, "Stride: (optional)", -1);
	addBitmapStride = UITextboxCreate(0, 0);
	UITextboxReplace(addBitmapStride, addBitmapStrideString, -1, false);
	UIButtonCreate(0, 0, "Add", -1)->invoke = AddBitmap;
	UIParentPop();

	UIElementFocus(&addBitmapPointer->e);
	UIWindowPack(addBitmapDialog, 0);
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
			snprintf(buffer, 1024, "%.*s", (int) textboxInput->bytes, textboxInput->string);
			if (commandLog) fprintf(commandLog, "%s\n", buffer);
			SendToGDB(buffer, true);

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
		} else if (m->code == UI_KEYCODE_ENTER && window->shift) {
			AddDataWindow();
		} else if (m->code == UI_KEYCODE_TAB) {
			char buffer[4096];
			snprintf(buffer, sizeof(buffer), "complete %.*s", lastKeyWasTab ? lastTabBytes : (int) textboxInput->bytes, textboxInput->string);
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

void Update(const char *data) {
	// Parse the current line.

	bool lineChanged = false;
	int newLine = 0;

	{
		const char *line = data;

		while (*line) {
			if (line[0] == '\n' || line == data) {
				int i = line == data ? 0 : 1, number = 0;

				while (true) {
					if (line[i] == '\t') {
						break;
					} else if (isdigit(line[i])) {
						number = number * 10 + line[i] - '0';
						i++;
					} else {
						goto tryNext;
					}
				}

				if (number) {
					lineChanged = true;
					newLine = number;
				}

				tryNext:;
				line += i + 1;
			} else {
				line++;
			}
		}
	}

	// Parse the name of the file.

	bool fileChanged = false;
	char newFile[4096];

	{
		const char *file = data;

		while (true) {
			file = strstr(file, " at ");
			if (!file) break;

			file += 4;
			const char *end = strchr(file, ':');

			if (end && isdigit(end[1])) {
				snprintf(newFile, sizeof(newFile), "%.*s", (int) (end - file), file);
				fileChanged = true;
			}
		}
	}

	// Get the current address.

	if (showingDisassembly) {
		UpdateDisassemblyLine();
	}

	// Set the file and line in the source display.

	bool changedSourceLine = SetPosition(fileChanged ? newFile : NULL, lineChanged ? newLine : -1, true);

	if (changedSourceLine && currentLine < displayCode->lineCount) {
		// If there is an auto-print expression from the previous line, evaluate it.

		if (autoPrintExpression[0]) {
			char buffer[1024];
			snprintf(buffer, sizeof(buffer), "p %s", autoPrintExpression);
			EvaluateCommand(buffer);
			const char *result = strchr(evaluateResult, '=');

			if (result) {
				autoPrintResultLine = autoPrintExpressionLine;
				snprintf(autoPrintResult, sizeof(autoPrintResult), "%s", result);
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
			snprintf(autoPrintExpression, sizeof(autoPrintExpression), "%.*s",
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
					snprintf(breakpoint.file, sizeof(breakpoint.file), "%.*s", (int) (end - file), file);
					breakpoint.line = atoi(end + 1);
				} else recognised = false;
			} else recognised = false;

			if (recognised) {
				breakpoints.Add(breakpoint);
			}

			position = next;
		}
	}

	tableBreakpoints->itemCount = breakpoints.Length();
	UITableResizeColumns(tableBreakpoints);
	UIElementRefresh(&tableBreakpoints->e);

	// Get the stack.

	EvaluateCommand("bt 50");
	stack.Free();
	if (!stackJustSelected) stackSelected = 0;
	stackJustSelected = false;

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
			snprintf(entry.function, sizeof(entry.function), "%.*s", (int) (position - functionName), functionName);

			const char *file = strstr(position, " at ");

			if (file && file < next) {
				file += 4;
				const char *end = file;
				while (*end != '\n' && end < next) end++;
				snprintf(entry.location, sizeof(entry.location), "%.*s", (int) (end - file), file);
			}

			stack.Add(entry);

			if (!(*next)) break;
			position = next + 1;
		}
	}

	tableStack->itemCount = stack.Length();
	UITableResizeColumns(tableStack);
	UIElementRefresh(&tableStack->e);

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
			snprintf(data.string, sizeof(data.string), "%.*s",
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
					snprintf(autoPrintResult + position, sizeof(autoPrintResult) - position, ", ");
				}

				int position = strlen(autoPrintResult);
				snprintf(autoPrintResult + position, sizeof(autoPrintResult) - position, "%.*s=%.*s",
						(int) (nameEnd - nameStart), nameStart,
						(int) (format1End - format1Start), format1Start);
			}
		}

		UIElementRefresh(&registersWindow->e);
		registerData.Free();
		registerData = newRegisterData;
	}
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
			snprintf(buffer, 64, "frame %d", index);
			SendToGDB(buffer, false);
			stackSelected = index;
			stackJustSelected = true;
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
				snprintf(buffer, 1024, "until %d", line);
				SendToGDB(buffer, true);
			} else if (element->window->alt) {
				char buffer[1024];
				snprintf(buffer, 1024, "tbreak %d", line);
				EvaluateCommand(buffer);
				snprintf(buffer, 1024, "jump %d", line);
				SendToGDB(buffer, true);
			}
		}
	} else if (message == UI_MSG_CODE_GET_MARGIN_COLOR && !showingDisassembly) {
		for (int i = 0; i < breakpoints.Length(); i++) {
			if (breakpoints[i].line == di && 0 == strcmp(breakpoints[i].file, currentFile)) {
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

int TrafficLightMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_PAINT) {
		UIDrawRectangle((UIPainter *) dp, element->bounds, programRunning ? 0xFF0000 : 0x00FF00, ui.theme.border, UI_RECT_1(1));
	}

	return 0;
}

int WindowMessage(UIElement *, UIMessage message, int di, void *dp) {
	if (message == MSG_RECEIVED_DATA) {
		programRunning = false;
		char *input = (char *) dp;
		Update(input);
		// printf("%s\n", input);
		UICodeInsertContent(displayOutput, input, -1, false);
		UIElementRefresh(&displayOutput->e);
		UIElementRepaint(&trafficLight->e, NULL);
		free(input);
	}

	return 0;
}

extern "C" void CreateInterface(UIWindow *_window) {
#ifdef LOG_COMMANDS
	{
		char path[4096];
		snprintf(path, sizeof(path), "%s/gf_log.txt", getenv("HOME"));
		commandLog = fopen(path, "ab");
	}
#endif

	window = _window;
	window->scale = uiScale;
	window->e.messageUser = WindowMessage;

	UIPanel *panel1 = UIPanelCreate(&window->e, UI_PANEL_EXPAND);
	UISplitPane *split1 = UISplitPaneCreate(&panel1->e, UI_SPLIT_PANE_VERTICAL | UI_ELEMENT_V_FILL, 0.75f);
	UISplitPane *split2 = UISplitPaneCreate(&split1->e, /* horizontal */ 0, 0.80f);
	UISplitPane *split4 = UISplitPaneCreate(&split1->e, /* horizontal */ 0, 0.65f);
	UIPanel *panel2 = UIPanelCreate(&split4->e, UI_PANEL_EXPAND);
	tabPaneWatchData = UITabPaneCreate(&split4->e, 0, "Registers\tWatch\tData");
	registersWindow = UIPanelCreate(&tabPaneWatchData->e, UI_PANEL_SMALL_SPACING | UI_PANEL_GRAY | UI_PANEL_SCROLL);
	UIPanel *watchWindow = UIPanelCreate(&tabPaneWatchData->e, UI_PANEL_SMALL_SPACING | UI_PANEL_GRAY);
	UILabelCreate(&watchWindow->e, UI_ELEMENT_H_FILL, "(Work in progress.)", -1);
	UIPanel *panel4 = UIPanelCreate(&tabPaneWatchData->e, UI_PANEL_EXPAND);
	UIPanel *panel5 = UIPanelCreate(&panel4->e, UI_PANEL_GRAY | UI_PANEL_HORIZONTAL);
	UIButtonCreate(&panel5->e, 0, "Add bitmap...", -1)->invoke = AddBitmapDialog;
	dataWindow = UIMDIClientCreate(&panel4->e, UI_ELEMENT_V_FILL);
	displayOutput = UICodeCreate(&panel2->e, UI_CODE_NO_MARGIN | UI_ELEMENT_V_FILL);
	UIPanel *panel3 = UIPanelCreate(&panel2->e, UI_PANEL_HORIZONTAL | UI_PANEL_EXPAND | UI_PANEL_GRAY);
	panel3->border = UI_RECT_1(5);
	panel3->gap = 5;
	trafficLight = UISpacerCreate(&panel3->e, 0, 30, 30);
	trafficLight->e.messageUser = TrafficLightMessage;
	buttonMenu = UIButtonCreate(&panel3->e, 0, "Menu", -1);
	buttonMenu->invoke = ShowMenu;
	textboxInput = UITextboxCreate(&panel3->e, UI_ELEMENT_H_FILL);
	textboxInput->e.messageUser = TextboxInputMessage;
	UIElementFocus(&textboxInput->e);
	displayCode = UICodeCreate(&split2->e, 0);
	displayCode->e.messageUser = DisplayCodeMessage;
	UISplitPane *split3 = UISplitPaneCreate(&split2->e, UI_SPLIT_PANE_VERTICAL, 0.50f);
	tableBreakpoints = UITableCreate(&split3->e, 0, "File\tLine");
	tableBreakpoints->e.messageUser = TableBreakpointsMessage;
	tableStack = UITableCreate(&split3->e, 0, "Index\tFunction\tLocation\tAddress");
	tableStack->e.messageUser = TableStackMessage;

	RegisterShortcuts();
	LoadSettings(false);
	CommandLoadTheme(NULL);

	pthread_cond_init(&evaluateEvent, NULL);
	pthread_mutex_init(&evaluateMutex, NULL);
	StartGDBThread();
}

extern "C" void CloseDebugger() {
	kill(gdbPID, SIGKILL);
	pthread_cancel(gdbThread);
}

#ifndef EMBED_GF
int main(int argc, char **argv) {
	// Setup GDB arguments.
	gdbArgv = (char **) malloc(sizeof(char *) * (argc + 1));
	gdbArgv[0] = (char *) "gdb";
	memcpy(gdbArgv + 1, argv + 1, sizeof(argv) * argc);
	gdbArgc = argc;

	LoadSettings(true);
	UIInitialise();
	CreateInterface(UIWindowCreate(0, 0, "gf2", 0, 0));
	CommandSyncWithGvim(NULL);
	UIMessageLoop();
	CloseDebugger();

	return 0;
}
#endif
