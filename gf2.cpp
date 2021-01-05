// Build with: g++ gf2.cpp -lX11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-truncation -o gf2 -g -pthread

// Future extensions: 
// 	- Saving commands to shortcuts (with alt- key).
// 	- Watch window.
// 	- Memory window.
// 	- Disassembly window.
// 	- Hover to view value.
// 	- Automatically show results from previous line.
// 	- Thread selection.
// 	- Data breakpoint viewer.
// 	- More reliable way to get the exact source file and line?
// 	- Automatically restoring breakpoints and symbols files after restarting gdb.

#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <ctype.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#define UI_LINUX
#define UI_IMPLEMENTATION
#include "luigi.h"

#define MSG_RECEIVED_DATA ((UIMessage) (UI_MSG_USER + 1))

// Current file and line:

char currentFile[4096];
int currentLine;

// User interface:

UIWindow *window;
UICode *displayCode;
UICode *displayOutput;
UITable *tableBreakpoints;
UITable *tableStack;
UITextbox *textboxInput;
UIButton *buttonMenu;
UISpacer *trafficLight;

// Find dialog:

UIWindow *findWindow;
UITextbox *findTextbox;
UILabel *findLabel;

// Theme editor:

UIWindow *themeEditorWindow;
UIColorPicker *themeEditorColorPicker;
UITable *themeEditorTable;
int themeEditorSelectedColor;

// Call stack:

struct StackEntry {
	char function[64];
	char location[64];
	uint64_t address;
	int id;
};

StackEntry *stack;
int stackSelected;
bool stackJustSelected;

// Breakpoints:

struct Breakpoint {
	char file[64];
	int line;
};

Breakpoint *breakpoints;

// Command history:

char **commandHistory;
int commandHistoryIndex;

// GDB process:

#define RECEIVE_BUFFER_SIZE (4194304)
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

void *DebuggerThread(void *) {
	int outputPipe[2], inputPipe[2];
	pipe(outputPipe);
	pipe(inputPipe);

	char *const argv[] = { (char *) "gdb", NULL };
	posix_spawn_file_actions_t actions = {};
	posix_spawn_file_actions_adddup2(&actions, inputPipe[0],  0);
	posix_spawn_file_actions_adddup2(&actions, outputPipe[1], 1);
	posix_spawn_file_actions_adddup2(&actions, outputPipe[1], 2);
	posix_spawnp((pid_t *) &gdbPID, "gdb", &actions, NULL, argv, environ);

	pipeToGDB = inputPipe[1];

	while (true) {
		char buffer[512 + 1];
		int count = read(outputPipe[0], buffer, 512);
		buffer[count] = 0;
		if (!count) break;
		receiveBufferPosition += snprintf(receiveBuffer + receiveBufferPosition, 
			RECEIVE_BUFFER_SIZE - receiveBufferPosition, "%s", buffer);
		if (!strstr(buffer, "(gdb) ")) continue;

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
	pthread_cond_wait(&evaluateEvent, &evaluateMutex);
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

void SetPosition(const char *file, int line) {
	char buffer[4096];

	if (file && file[0] == '~') {
		snprintf(buffer, sizeof(buffer), "%s/%s", getenv("HOME"), 1 + file);
		file = buffer;
	}

	if (file && strcmp(currentFile, file)) {
		currentLine = 0;
		snprintf(currentFile, 4096, "%s", file);

		printf("attempting to load '%s'\n", file);

		size_t bytes;
		char *buffer2 = LoadFile(file, &bytes);

		if (!buffer2) {
			char buffer3[4096];
			snprintf(buffer3, 4096, "The file '%s' could not be loaded.", file);
			UICodeInsertContent(displayCode, buffer3, -1, true);
		} else {
			UICodeInsertContent(displayCode, buffer2, bytes, true);
			free(buffer2);
		}
	}

	if (line != -1 && currentLine != line) {
		currentLine = line;
		UICodeFocusLine(displayCode, line); 
	}

	UIElementRefresh(&displayCode->e);
}

void CommandSendToGDB(void *_string) {
	SendToGDB((const char *) _string, true);
}

void CommandDeleteBreakpoint(void *_index) {
	int index = (int) (intptr_t) _index;
	Breakpoint *breakpoint = breakpoints + index;
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
			SetPosition(name, lineNumber);
		}

		done:;
		unlink(".temp.gf");
	}
}

void CommandToggleBreakpoint(void *_line) {
	int line = (int) (intptr_t) _line;

	if (!line) {
		line = currentLine;
	}

	for (int i = 0; i < arrlen(breakpoints); i++) {
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

int FindWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_WINDOW_CLOSE) {
		UIElementDestroy(element);
		findWindow = NULL;
		return 1;
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		if (m->code == UI_KEYCODE_ESCAPE) {
			UIElementDestroy(element);
			findWindow = NULL;
			return 1;
		} else if (m->code == UI_KEYCODE_ENTER) {
			displayCode->content = (char *) UI_REALLOC(displayCode->content, displayCode->contentBytes + 1);
			displayCode->content[displayCode->contentBytes] = 0;
			findTextbox->string = (char *) UI_REALLOC(findTextbox->string, findTextbox->bytes + 1);
			findTextbox->string[findTextbox->bytes] = 0;
			const char *result = strstr(displayCode->content, findTextbox->string);

			if (result) {
				int line = 1;

				for (int i = 0; i < result - displayCode->content; i++) {
					if (displayCode->content[i] == '\n') {
						line++;
					}
				}

				UICodeFocusLine(displayCode, line); 
				UIElementRefresh(&displayCode->e);

				UIElementDestroy(element);
				findWindow = NULL;
			} else {
				UILabelSetContent(findLabel, "Search for: (not found)", -1);
				UIElementRefresh(&findLabel->e);
			}

			return 1;

		}
	}

	return 0;
}

void CommandFind(void *) {
	if (findWindow) return;
	findWindow = UIWindowCreate(0, 0, "Find", 300, 70);
	findWindow->e.messageUser = FindWindowMessage;
	UIPanel *panel = UIPanelCreate(&findWindow->e, UI_PANEL_GRAY | UI_PANEL_EXPAND);
	panel->border = UI_RECT_1(5);
	panel->gap = 5;
	findLabel = UILabelCreate(&panel->e, 0, "Search for:", -1);
	findTextbox = UITextboxCreate(&panel->e, 0); 
	UIElementFocus(&findTextbox->e);
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
			return snprintf(m->buffer, m->bufferBytes, "%s", themeItems[m->index]);
		} else {
			return snprintf(m->buffer, m->bufferBytes, "#%.6x", ui.theme.colors[m->index]);
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

void ShowMenu(void *) {
	UIMenu *menu = UIMenuCreate(&buttonMenu->e, UI_MENU_PLACE_ABOVE);
	UIMenuAddItem(menu, 0, "Run\tShift+F5", -1, CommandSendToGDB, (void *) "r");
	UIMenuAddItem(menu, 0, "Run paused\tCtrl+F5", -1, CommandSendToGDB, (void *) "start");
	UIMenuAddItem(menu, 0, "Kill\tF3", -1, CommandSendToGDB, (void *) "kill");
	UIMenuAddItem(menu, 0, "Restart GDB\tCtrl+R", -1, CommandRestartGDB, NULL);
	UIMenuAddItem(menu, 0, "Connect\tF4", -1, CommandSendToGDB, (void *) "target remote :1234");
	UIMenuAddItem(menu, 0, "Continue\tF5", -1, CommandSendToGDB, (void *) "c");
	UIMenuAddItem(menu, 0, "Step over\tF10", -1, CommandSendToGDB, (void *) "n");
	UIMenuAddItem(menu, 0, "Step in\tF11", -1, CommandSendToGDB, (void *) "s");
	UIMenuAddItem(menu, 0, "Step out\tShift+F11", -1, CommandSendToGDB, (void *) "finish");
	UIMenuAddItem(menu, 0, "Pause\tF8", -1, CommandPause, NULL);
	UIMenuAddItem(menu, 0, "Toggle breakpoint\tF9", -1, CommandToggleBreakpoint, NULL);
	UIMenuAddItem(menu, 0, "Sync with gvim\tF2", -1, CommandSyncWithGvim, NULL);
	UIMenuAddItem(menu, 0, "Find\tCtrl+F", -1, CommandFind, NULL);
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
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F10, .invoke = CommandSendToGDB, .cp = (void *) "n" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F11, .invoke = CommandSendToGDB, .cp = (void *) "s" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F11, .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "finish" });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F8, .invoke = CommandPause, .cp = NULL });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F9, .invoke = CommandToggleBreakpoint, .cp = NULL });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_F2, .invoke = CommandSyncWithGvim, .cp = NULL });
	UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_LETTER('F'), .ctrl = true, .invoke = CommandFind, .cp = NULL });
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

void CustomCommandAndRun(void *_command) {
	const char *command = (const char *) _command;

	if (0 == memcmp(command, "shell ", 6)) {
		if (RunSystemWithOutput(command + 6)) {
			return;
		}
	} else {
		SendToGDB(command, true);
	}

	CommandSendToGDB((void *) "r");
}

void LoadProjectSettings() {
	char *settings = LoadFile(".project.gf", NULL);
	int index = 0;

	while (settings && *settings) {
		char *start = settings;
		char *end = strchr(settings, '\n');

		if (end) { *end = 0, settings = end + 1; }
		else settings = NULL;

		printf("shortcut for \"%s\" on ctrl+%d\n", start, index);

		UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_DIGIT('0' + index), .ctrl = true, .invoke = CustomCommand, .cp = start });
		UIWindowRegisterShortcut(window, { .code = UI_KEYCODE_DIGIT('0' + index), .ctrl = true, .shift = true, .invoke = CustomCommandAndRun, .cp = start });
		index++;
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

		if (m->code == UI_KEYCODE_ENTER) {
			char buffer[1024];
			snprintf(buffer, 1024, "%.*s", (int) textboxInput->bytes, textboxInput->string);
			SendToGDB(buffer, true);

			char *string = (char *) malloc(textboxInput->bytes + 1);
			memcpy(string, textboxInput->string, textboxInput->bytes);
			string[textboxInput->bytes] = 0;
			arrins(commandHistory, 0, string);
			commandHistoryIndex = 0;

			if (arrlen(commandHistory) > 100) {
				free(arrlast(commandHistory));
				arrpop(commandHistory);
			}

			UITextboxClear(textboxInput, false);
			UIElementRefresh(&textboxInput->e);

			return 1;
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
			if (commandHistoryIndex < arrlen(commandHistory)) {
				UITextboxClear(textboxInput, false);
				UITextboxReplace(textboxInput, commandHistory[commandHistoryIndex], -1, false);
				if (commandHistoryIndex < arrlen(commandHistory) - 1) commandHistoryIndex++;
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

void Update(const char *data) {
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

	SetPosition(fileChanged ? newFile : NULL, lineChanged ? newLine : -1);

	// Get the list of breakpoints.

	EvaluateCommand("info break");
	arrfree(breakpoints);

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
				arrput(breakpoints, breakpoint);
			}

			position = next;
		}
	}

	tableBreakpoints->itemCount = arrlen(breakpoints);
	UITableResizeColumns(tableBreakpoints);
	UIElementRefresh(&tableBreakpoints->e);

	// Get the stack.

	EvaluateCommand("bt 50");
	arrfree(stack);
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

			arrput(stack, entry);

			position = next + 1;
		}
	}

	tableStack->itemCount = arrlen(stack);
	UITableResizeColumns(tableStack);
	UIElementRefresh(&tableStack->e);
}

int TableStackMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		m->isSelected = m->index == stackSelected;
		StackEntry *entry = stack + m->index;

		if (m->column == 0) {
			return snprintf(m->buffer, m->bufferBytes, "%d", entry->id);
		} else if (m->column == 1) {
			return snprintf(m->buffer, m->bufferBytes, "%s", entry->function);
		} else if (m->column == 2) {
			return snprintf(m->buffer, m->bufferBytes, "%s", entry->location);
		} else if (m->column == 3) {
			return snprintf(m->buffer, m->bufferBytes, "0x%lX", entry->address);
		}
	} else if (message == UI_MSG_CLICKED) {
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY); 

		if (index != -1) {
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
		Breakpoint *entry = breakpoints + m->index;

		if (m->column == 0) {
			return snprintf(m->buffer, m->bufferBytes, "%s", entry->file);
		} else if (m->column == 1) {
			return snprintf(m->buffer, m->bufferBytes, "%d", entry->line);
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
	if (message == UI_MSG_CLICKED) {
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
	} else if (message == UI_MSG_CODE_GET_MARGIN_COLOR) {
		for (int i = 0; i < arrlen(breakpoints); i++) {
			if (breakpoints[i].line == di && 0 == strcmp(breakpoints[i].file, currentFile)) {
				return 0xFF0000;
			}
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

int main(int, char **) {
	UIInitialise();
	CommandLoadTheme(NULL);

	window = UIWindowCreate(0, 0, "gf2", 0, 0);
	window->e.messageUser = WindowMessage;
	UIPanel *panel1 = UIPanelCreate(&window->e, UI_PANEL_EXPAND);
	UISplitPane *split1 = UISplitPaneCreate(&panel1->e, UI_SPLIT_PANE_VERTICAL | UI_ELEMENT_V_FILL, 0.75f);
	UISplitPane *split2 = UISplitPaneCreate(&split1->e, /* horizontal */ 0, 0.80f);
	UIPanel *panel2 = UIPanelCreate(&split1->e, UI_PANEL_EXPAND);
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

	pthread_cond_init(&evaluateEvent, NULL);
	pthread_mutex_init(&evaluateMutex, NULL);
	StartGDBThread();

	CommandSyncWithGvim(NULL);
	RegisterShortcuts();
	LoadProjectSettings();
	UIMessageLoop();
	kill(gdbPID, SIGKILL);
	pthread_cancel(gdbThread);

	return 0;
}
