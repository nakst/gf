// TODO Disassembly window extensions.
// 	- Split source and disassembly view.
// 	- Setting/clearing/showing breakpoints.
// 	- Jump to line and run to line.
// 	- Shift+F10: run to next instruction (for skipping past loops).

// TODO Data window extensions.
// 	- Copy bitmap to clipboard, or save to file.

// TODO Watch window.
//	- Better toggle buttons.
// 	- Lock pointer address.

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

int fontSize = 13;
float uiScale = 1;

extern "C" {
#define UI_FONT_SIZE (fontSize)
#define UI_LINUX
#define UI_IMPLEMENTATION
#include "luigi.h"
}

#define MSG_RECEIVED_DATA ((UIMessage) (UI_MSG_USER + 1))
#define MSG_RECEIVED_CONTROL ((UIMessage) (UI_MSG_USER + 2))

struct INIState {
	char *buffer, *section, *key, *value;
	size_t bytes, sectionBytes, keyBytes, valueBytes;
};

FILE *commandLog;
char controlPipePath[PATH_MAX];
char emptyString;
bool programRunning;

// Current file and line:

char currentFile[PATH_MAX];
char currentFileFull[PATH_MAX];
int currentLine;
time_t currentFileReadTime;
bool showingDisassembly;
char previousLocation[256];

// User interface:

UIWindow *windowMain;

UICode *displayCode;
UICode *displayOutput;
UISpacer *trafficLight;

UIMDIClient *dataWindow;
UIPanel *dataTab;

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

// Breakpoints:

struct Breakpoint {
	char file[64];
	char fileFull[PATH_MAX];
	int line;
};

Array<Breakpoint> breakpoints;

// Stack:

struct StackEntry {
	char function[64];
	char location[sizeof(previousLocation)];
	uint64_t address;
	int id;
};

Array<StackEntry> stack;
int stackSelected;
bool stackChanged;

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

def gf_valueof(expression, format):
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
        if format[0] != ' ': result = result + value.format_string(max_elements=10,max_depth=3,format=format)[0:200]
        else: result = result + value.format_string(max_elements=10,max_depth=3)[0:200]
    except:
        result = result + '??'
    print(result)

def gf_addressof(expression):
    value = _gf_value(expression)
    if value == None: return
    print(value.address)

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

// Forward declarations:

extern "C" bool DisplaySetPosition(const char *file, int line, bool useGDBToGetFullPath);
void InterfaceShowMenu(void *self);

//////////////////////////////////////////////////////
// Utilities:
//////////////////////////////////////////////////////

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

char *LoadFile(const char *path, size_t *_bytes) {
	FILE *f = fopen(path, "rb");

	if (!f) {
		return nullptr;
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

int ModifiedRowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_PAINT) {
		UIDrawBorder((UIPainter *) dp, element->bounds, 0x00FF00, UI_RECT_1(2));
	}

	return 0;
}

int TrafficLightMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_PAINT) {
		UIDrawRectangle((UIPainter *) dp, element->bounds, programRunning ? 0xFF0000 : 0x00FF00, ui.theme.border, UI_RECT_1(1));
	}

	return 0;
}

//////////////////////////////////////////////////////
// Debugger interaction:
//////////////////////////////////////////////////////

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
char **gdbArgv;
int gdbArgc;
const char *gdbPath = "gdb";
bool firstUpdate = true;

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
			UIWindowPostMessage(windowMain, MSG_RECEIVED_DATA, copy);
		}

		receiveBufferPosition = 0;
	}

	return nullptr;
}

void DebuggerStartThread() {
	pthread_t debuggerThread;
	pthread_attr_t attributes;
	pthread_attr_init(&attributes);
	pthread_create(&debuggerThread, &attributes, DebuggerThread, nullptr);
	gdbThread = debuggerThread;
}

void DebuggerSend(const char *string, bool echo) {
	if (programRunning) {
		kill(gdbPID, SIGINT);
	}

	programRunning = true;
	if (trafficLight) UIElementRepaint(&trafficLight->e, nullptr);

	// printf("sending: %s\n", string);

	char newline = '\n';

	if (echo && displayOutput) {
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
	if (trafficLight) UIElementRepaint(&trafficLight->e, nullptr);
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

extern "C" void DebuggerClose() {
	kill(gdbPID, SIGKILL);
	pthread_cancel(gdbThread);
}

void *ControlPipeThread(void *) {
	while (true) {
		FILE *file = fopen(controlPipePath, "rb");
		char input[256];
		input[fread(input, 1, sizeof(input) - 1, file)] = 0;
		UIWindowPostMessage(windowMain, MSG_RECEIVED_CONTROL, strdup(input));
		fclose(file);
	}

	return nullptr;
}

void DebuggerGetStack() {
	EvaluateCommand("bt 50");
	stack.Free();

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

void DebuggerGetBreakpoints() {
	EvaluateCommand("info break");
	breakpoints.Free();

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

struct TabCompleter {
	bool _lastKeyWasTab;
	int consecutiveTabCount;
	int lastTabBytes;
};

void TabCompleterRun(TabCompleter *completer, UITextbox *textbox, bool lastKeyWasTab, bool addPrintPrefix) {
	char buffer[4096];
	StringFormat(buffer, sizeof(buffer), "complete %s%.*s", addPrintPrefix ? "p " : "",
			lastKeyWasTab ? completer->lastTabBytes : (int) textbox->bytes, textbox->string);
	for (int i = 0; buffer[i]; i++) if (buffer[i] == '\\') buffer[i] = ' ';
	EvaluateCommand(buffer);

	const char *start = evaluateResult;
	const char *end = strchr(evaluateResult, '\n');

	if (!lastKeyWasTab) {
		completer->consecutiveTabCount = 0;
		completer->lastTabBytes = textbox->bytes;
	}

	while (start && end && memcmp(start + (addPrintPrefix ? 2 : 0), textbox->string, completer->lastTabBytes)) {
		start = end + 1;
		end = strchr(start, '\n');
	}

	for (int i = 0; end && i < completer->consecutiveTabCount; i++) {
		start = end + 1;
		end = strchr(start, '\n');
	}

	if (!end) {
		completer->consecutiveTabCount = 0;
		start = evaluateResult;
		end = strchr(evaluateResult, '\n');
	}

	completer->_lastKeyWasTab = true;
	completer->consecutiveTabCount++;

	if (end) {
		if (addPrintPrefix) start += 2;
		UITextboxClear(textbox, false);
		UITextboxReplace(textbox, start, end - start, false);
		UIElementRefresh(&textbox->e);
	}
}

//////////////////////////////////////////////////////
// Commands:
//////////////////////////////////////////////////////

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
	FILE *file = popen("vim --servername GVIM --remote-expr \"execute(\\\"ls\\\")\" | grep %", "r");
	if (!file) return;
	char buffer[1024];
	buffer[fread(buffer, 1, 1023, file)] = 0;
	pclose(file);
	char *name = strchr(buffer, '"');
	if (!name) return;
	char *nameEnd = strchr(++name, '"');
	if (!nameEnd) return;
	*nameEnd = 0;
	char *line = strstr(nameEnd + 1, "line ");
	if (!line) return;
	int lineNumber = atoi(line + 5);
	DisplaySetPosition(name, lineNumber, false);
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

void CommandAskGDBForPWD(void *) {
	EvaluateCommand("info source");
	const char *needle = "Compilation directory is ";
	char *pwd = strstr(evaluateResult, needle);

	if (pwd) {
		pwd += strlen(needle);
		char *end = strchr(pwd, '\n');
		if (end) *end = 0;

		if (!chdir(pwd)) {
			if (!displayOutput) return;
			char buffer[4096];
			StringFormat(buffer, sizeof(buffer), "New working directory: %s", pwd);
			UICodeInsertContent(displayOutput, buffer, -1, false);
			UIElementRefresh(&displayOutput->e);
			return;
		}
	}

	UIDialogShow(windowMain, 0, "Couldn't get the working directory.\n%f%b", "OK");
}

void CommandCustom(void *_command) {
	const char *command = (const char *) _command;

	if (0 == memcmp(command, "shell ", 6)) {
		char buffer[4096];
		StringFormat(buffer, 4096, "Running shell command \"%s\"...\n", command);
		if (displayOutput) UICodeInsertContent(displayOutput, buffer, -1, false);
		StringFormat(buffer, 4096, "%s > .output.gf 2>&1", command);
		int start = time(nullptr);
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

		if (displayOutput) UICodeInsertContent(displayOutput, copy, j, false);
		free(output);
		free(copy);
		StringFormat(buffer, 4096, "(exit code: %d; time: %ds)\n", result, (int) (time(nullptr) - start));
		if (displayOutput) UICodeInsertContent(displayOutput, buffer, -1, false);
		if (displayOutput) UIElementRefresh(&displayOutput->e);
	} else {
		DebuggerSend(command, true);
	}
}

void CommandDonate(void *) {
	system("xdg-open https://www.patreon.com/nakst");
}

//////////////////////////////////////////////////////
// Themes:
//////////////////////////////////////////////////////

const char *themeItems[] = {
	"panel1", "panel2", "text", "textDisabled", "border",
	"buttonNormal", "buttonHovered", "buttonPressed", "buttonFocused", "buttonDisabled",
	"textboxNormal", "textboxText", "textboxFocused", "textboxSelected", "textboxSelectedText",
	"scrollGlyph", "scrollThumbNormal", "scrollThumbHovered", "scrollThumbPressed",
	"codeFocused", "codeBackground", "codeDefault", "codeComment", "codeString", "codeNumber", "codeOperator", "codePreprocessor",
	"gaugeFilled", "tableSelected", "tableSelectedText", "tableHovered", "tableHoveredText",
};

void ConvertOldTheme() {
	// TODO Remove this, eventually.

	char buffer[PATH_MAX];
	StringFormat(buffer, sizeof(buffer), "%s/.config/gf2_theme.dat", getenv("HOME"));
	FILE *f = fopen(buffer, "rb");
	if (!f) return;
	fread(&ui.theme, 1, sizeof(ui.theme), f);
	fclose(f);
	unlink(buffer);
	StringFormat(buffer, sizeof(buffer), "%s/.config/gf2_config.ini", getenv("HOME"));
	f = fopen(buffer, "a");
	if (!f) return;
	fprintf(f, "\n[theme]\n");

	for (uintptr_t i = 0; i < sizeof(themeItems) / sizeof(themeItems[0]); i++) {
		fprintf(f, "%s=%.6X\n", themeItems[i], ui.theme.colors[i] & 0xFFFFFF);
	}

	fclose(f);
}

//////////////////////////////////////////////////////
// Source display:
//////////////////////////////////////////////////////

char autoPrintExpression[1024];
char autoPrintResult[1024];
int autoPrintExpressionLine;
int autoPrintResultLine;

bool DisplaySetPosition(const char *file, int line, bool useGDBToGetFullPath) {
	if (showingDisassembly) {
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

void DisplaySetPositionFromStack() {
	if (stack.Length() > stackSelected) {
		char location[sizeof(previousLocation)];
		strcpy(previousLocation, stack[stackSelected].location);
		strcpy(location, stack[stackSelected].location);
		char *line = strchr(location, ':');
		if (line) *line = 0;
		DisplaySetPosition(location, line ? atoi(line + 1) : -1, true);
	}
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
		UICodeInsertContent(displayCode, "Disassembly could not be loaded.\nPress Ctrl+D to return to source view.", -1, true);
		displayCode->tabSize = 8;
		DisassemblyLoad();
		DisassemblyUpdateLine();
	} else {
		currentLine = -1;
		currentFile[0] = 0;
		currentFileReadTime = 0;
		DisplaySetPositionFromStack();
		displayCode->tabSize = 4;
	}

	UIElementRefresh(&displayCode->e);
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

UIElement *SourceWindowCreate(UIElement *parent) {
	displayCode = UICodeCreate(parent, 0);
	displayCode->e.messageUser = DisplayCodeMessage;
	return &displayCode->e;
}

void SourceWindowUpdate(const char *data, UIElement *) {
	bool changedSourceLine = false;

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

	if (!stackChanged && changedSourceLine) stackSelected = 0;
	stackChanged = false;

	if (changedSourceLine && strcmp(stack[stackSelected].location, previousLocation)) {
		DisplaySetPositionFromStack();
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
}

//////////////////////////////////////////////////////
// Bitmap viewer:
//////////////////////////////////////////////////////

struct BitmapViewer {
	char pointer[256];
	char width[256];
	char height[256];
	char stride[256];
	int parsedWidth, parsedHeight;
	UIButton *autoToggle;
	UIImageDisplay *display;
	UIPanel *labelPanel;
	UILabel *label;
};

Array<UIElement *> autoUpdateBitmapViewers;
bool autoUpdateBitmapViewersQueued;

int BitmapViewerWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DESTROY) {
		free(element->cp);
	} else if (message == UI_MSG_GET_WIDTH) {
		return ((BitmapViewer *) element->cp)->parsedWidth;
	} else if (message == UI_MSG_GET_HEIGHT) {
		return ((BitmapViewer *) element->cp)->parsedHeight;
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

const char *BitmapViewerGetBits(const char *pointerString, const char *widthString, const char *heightString, const char *strideString,
		uint32_t **_bits, int *_width, int *_height, int *_stride) {
	const char *widthResult = EvaluateExpression(widthString);
	if (!widthResult) { return "Could not evaluate width."; }
	int width = atoi(widthResult + 1);
	const char *heightResult = EvaluateExpression(heightString);
	if (!heightResult) { return "Could not evaluate height."; }
	int height = atoi(heightResult + 1);
	int stride = width * 4;
	const char *pointerResult = EvaluateExpression(pointerString);
	if (!pointerResult) { return "Could not evaluate pointer."; }
	char _pointerResult[1024];
	StringFormat(_pointerResult, sizeof(_pointerResult), "%s", pointerResult);
	pointerResult = strstr(_pointerResult, " 0x");
	if (!pointerResult) { return "Pointer to image bits does not look like an address!"; }
	pointerResult++;

	if (strideString && *strideString) {
		const char *strideResult = EvaluateExpression(strideString);
		if (!strideResult) { return "Could not evaluate stride."; }
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
		return "Could not read the image bits!";
	}

	*_bits = bits, *_width = width, *_height = height, *_stride = stride;
	return nullptr;
}

void BitmapViewerUpdate(const char *pointerString, const char *widthString, const char *heightString, const char *strideString, UIElement *owner) {
	uint32_t *bits = nullptr;
	int width = 0, height = 0, stride = 0;
	const char *error = BitmapViewerGetBits(pointerString, widthString, heightString, strideString,
			&bits, &width, &height, &stride);

	if (!owner) {
		BitmapViewer *bitmap = (BitmapViewer *) calloc(1, sizeof(BitmapViewer));
		if (pointerString) StringFormat(bitmap->pointer, sizeof(bitmap->pointer), "%s", pointerString);
		if (widthString) StringFormat(bitmap->width, sizeof(bitmap->width), "%s", widthString);
		if (heightString) StringFormat(bitmap->height, sizeof(bitmap->height), "%s", heightString);
		if (strideString) StringFormat(bitmap->stride, sizeof(bitmap->stride), "%s", strideString);

		UIMDIChild *window = UIMDIChildCreate(&dataWindow->e, UI_MDI_CHILD_CLOSE_BUTTON, UI_RECT_1(0), "Bitmap", -1);
		window->e.messageUser = BitmapViewerWindowMessage;
		window->e.cp = bitmap;
		bitmap->autoToggle = UIButtonCreate(&window->e, UI_BUTTON_SMALL | UI_ELEMENT_NON_CLIENT, "Auto", -1);
		bitmap->autoToggle->e.messageUser = BitmapViewerAutoMessage;
		UIButtonCreate(&window->e, UI_BUTTON_SMALL | UI_ELEMENT_NON_CLIENT, "Refresh", -1)->e.messageUser = BitmapViewerRefreshMessage;
		owner = &window->e;

		UIPanel *panel = UIPanelCreate(owner, UI_PANEL_EXPAND);
		bitmap->display = UIImageDisplayCreate(&panel->e, UI_IMAGE_DISPLAY_INTERACTIVE | UI_ELEMENT_V_FILL, bits, width, height, stride);
		bitmap->labelPanel = UIPanelCreate(&panel->e, UI_PANEL_GRAY | UI_ELEMENT_V_FILL);
		bitmap->label = UILabelCreate(&bitmap->labelPanel->e, UI_ELEMENT_H_FILL, nullptr, 0);
	}

	BitmapViewer *bitmap = (BitmapViewer *) owner->cp;
	bitmap->parsedWidth = width, bitmap->parsedHeight = height;
	UIImageDisplaySetContent(bitmap->display, bits, width, height, stride);
	if (error) UILabelSetContent(bitmap->label, error, -1);
	if (error) bitmap->labelPanel->e.flags &= ~UI_ELEMENT_HIDE, bitmap->display->e.flags |= UI_ELEMENT_HIDE;
	else bitmap->labelPanel->e.flags |= UI_ELEMENT_HIDE, bitmap->display->e.flags &= ~UI_ELEMENT_HIDE;
	UIElementRefresh(&bitmap->display->e);
	UIElementRefresh(&bitmap->label->e);
	UIElementRefresh(bitmap->labelPanel->e.parent);
	UIElementRefresh(owner);
	UIElementRefresh(&dataWindow->e);

	free(bits);
}

void BitmapAddDialog(void *) {
	static char *pointer = nullptr, *width = nullptr, *height = nullptr, *stride = nullptr;

	const char *result = UIDialogShow(windowMain, 0, 
			"Add bitmap\n\n%l\n\nPointer to bits: (32bpp, RR GG BB AA)\n%t\nWidth:\n%t\nHeight:\n%t\nStride: (optional)\n%t\n\n%l\n\n%f%b%b",
			&pointer, &width, &height, &stride, "Add", "Cancel");

	if (0 == strcmp(result, "Add")) {
		BitmapViewerUpdate(pointer, width, height, (stride && stride[0]) ? stride : nullptr);
	}
}

void BitmapViewerUpdateAll() {
	if (~dataTab->e.flags & UI_ELEMENT_HIDE) {
		for (int i = 0; i < autoUpdateBitmapViewers.Length(); i++) {
			BitmapViewer *bitmap = (BitmapViewer *) autoUpdateBitmapViewers[i]->cp;
			BitmapViewerUpdate(bitmap->pointer, bitmap->width, bitmap->height, bitmap->stride, autoUpdateBitmapViewers[i]);
		}
	} else if (autoUpdateBitmapViewers.Length()) {
		autoUpdateBitmapViewersQueued = true;
	}
}

//////////////////////////////////////////////////////
// Console:
//////////////////////////////////////////////////////

Array<char *> commandHistory;
int commandHistoryIndex;

int TextboxInputMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UITextbox *textbox = (UITextbox *) element;

	if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		static TabCompleter tabCompleter = {};
		bool lastKeyWasTab = tabCompleter._lastKeyWasTab;
		tabCompleter._lastKeyWasTab = false;

		if (m->code == UI_KEYCODE_ENTER && !element->window->shift) {
			char buffer[1024];
			StringFormat(buffer, 1024, "%.*s", (int) textbox->bytes, textbox->string);
			if (commandLog) fprintf(commandLog, "%s\n", buffer);
			DebuggerSend(buffer, true);

			char *string = (char *) malloc(textbox->bytes + 1);
			memcpy(string, textbox->string, textbox->bytes);
			string[textbox->bytes] = 0;
			commandHistory.Insert(string, 0);
			commandHistoryIndex = 0;

			if (commandHistory.Length() > 100) {
				free(commandHistory.Last());
				commandHistory.Pop();
			}

			UITextboxClear(textbox, false);
			UIElementRefresh(&textbox->e);

			return 1;
		} else if (m->code == UI_KEYCODE_TAB && textbox->bytes && !element->window->shift) {
			TabCompleterRun(&tabCompleter, textbox, lastKeyWasTab, false);
			return 1;
		} else if (m->code == UI_KEYCODE_UP) {
			if (commandHistoryIndex < commandHistory.Length()) {
				UITextboxClear(textbox, false);
				UITextboxReplace(textbox, commandHistory[commandHistoryIndex], -1, false);
				if (commandHistoryIndex < commandHistory.Length() - 1) commandHistoryIndex++;
				UIElementRefresh(&textbox->e);
			}
		} else if (m->code == UI_KEYCODE_DOWN) {
			UITextboxClear(textbox, false);

			if (commandHistoryIndex > 0) {
				--commandHistoryIndex;
				UITextboxReplace(textbox, commandHistory[commandHistoryIndex], -1, false);
			}

			UIElementRefresh(&textbox->e);
		}
	}

	return 0;
}

UIElement *ConsoleWindowCreate(UIElement *parent) {
	UIPanel *panel2 = UIPanelCreate(parent, UI_PANEL_EXPAND);
	displayOutput = UICodeCreate(&panel2->e, UI_CODE_NO_MARGIN | UI_ELEMENT_V_FILL);
	UIPanel *panel3 = UIPanelCreate(&panel2->e, UI_PANEL_HORIZONTAL | UI_PANEL_EXPAND | UI_PANEL_GRAY);
	panel3->border = UI_RECT_1(5);
	panel3->gap = 5;
	trafficLight = UISpacerCreate(&panel3->e, 0, 30, 30);
	trafficLight->e.messageUser = TrafficLightMessage;
	UIButton *buttonMenu = UIButtonCreate(&panel3->e, 0, "Menu", -1);
	buttonMenu->invoke = InterfaceShowMenu;
	buttonMenu->e.cp = buttonMenu;
	UITextbox *textboxInput = UITextboxCreate(&panel3->e, UI_ELEMENT_H_FILL);
	textboxInput->e.messageUser = TextboxInputMessage;
	UIElementFocus(&textboxInput->e);
	return &panel2->e;
}

//////////////////////////////////////////////////////
// Watch window:
//////////////////////////////////////////////////////

struct Watch {
	bool open, hasFields, loadedFields, isArray;
	uint8_t depth;
	char format;
	uintptr_t arrayIndex;
	char *key, *value, *type;
	Array<Watch *> fields;
	Watch *parent;
	uint64_t updateIndex;
};

struct WatchWindow {
	Array<Watch *> rows;
	Array<Watch *> baseExpressions;
	UIElement *element;
	UITextbox *textbox;
	int selectedRow;
	uint64_t updateIndex;
	bool waitingForFormatCharacter;
};

struct WatchLogEntry {
	char value[24];
	char where[96];
};

struct WatchLogger {
	int id;
	Array<WatchLogEntry> entries;
	UITable *table;
};

Array<WatchLogger *> watchLoggers;

int WatchTextboxMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UITextbox *textbox = (UITextbox *) element;

	if (message == UI_MSG_UPDATE) {
		if (element->window->focused != element) {
			UIElementDestroy(element);
			((WatchWindow *) element->cp)->textbox = nullptr;
		}
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		static TabCompleter tabCompleter = {};
		bool lastKeyWasTab = tabCompleter._lastKeyWasTab;
		tabCompleter._lastKeyWasTab = false;

		if (m->code == UI_KEYCODE_TAB && textbox->bytes && !element->window->shift) {
			TabCompleterRun(&tabCompleter, textbox, lastKeyWasTab, true);
			return 1;
		}
	}

	return 0;
}

void WatchDestroyTextbox(WatchWindow *w) {
	if (!w->textbox) return;
	UIElementDestroy(&w->textbox->e);
	w->textbox = nullptr;
	UIElementFocus(w->element);
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

void WatchDeleteExpression(WatchWindow *w) {
	WatchDestroyTextbox(w);
	if (w->selectedRow == w->rows.Length()) return;
	int end = w->selectedRow + 1;

	for (; end < w->rows.Length(); end++) {
		if (w->rows[w->selectedRow]->depth >= w->rows[end]->depth) {
			break;
		}
	}

	bool found = false;
	Watch *watch = w->rows[w->selectedRow];

	for (int i = 0; i < w->baseExpressions.Length(); i++) {
		if (watch == w->baseExpressions[i]) {
			found = true;
			w->baseExpressions.Delete(i);
			break;
		}
	}

	assert(found);
	w->rows.Delete(w->selectedRow, end - w->selectedRow);
	WatchFree(watch);
	free(watch);
}

void WatchEvaluate(const char *function, Watch *watch) {
	char buffer[4096];
	uintptr_t position = 0;

	position += StringFormat(buffer + position, sizeof(buffer) - position, "py %s([", function);

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
		} else {
			first = false;
		}

		if (stack[stackCount]->key) {
			position += StringFormat(buffer + position, sizeof(buffer) - position, "'%s'", stack[stackCount]->key);
		} else {
			position += StringFormat(buffer + position, sizeof(buffer) - position, "%lu", stack[stackCount]->arrayIndex);
		}
	}

	position += StringFormat(buffer + position, sizeof(buffer) - position, "]");

	if (0 == strcmp(function, "gf_valueof")) {
		position += StringFormat(buffer + position, sizeof(buffer) - position, ",'%c'", watch->format ?: ' ');
	}

	position += StringFormat(buffer + position, sizeof(buffer) - position, ")");

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

void WatchInsertFieldRows(WatchWindow *w, Watch *watch, int *position) {
	for (int i = 0; i < watch->fields.Length(); i++) {
		w->rows.Insert(watch->fields[i], *position);
		*position = *position + 1;

		if (watch->fields[i]->open) {
			WatchInsertFieldRows(w, watch->fields[i], position);
		}
	}
}

void WatchEnsureRowVisible(WatchWindow *w, int index) {
	if (w->selectedRow < 0) w->selectedRow = 0;
	else if (w->selectedRow > w->rows.Length()) w->selectedRow = w->rows.Length();
	UIScrollBar *scroll = ((UIPanel *) w->element->parent)->scrollBar;
	int rowHeight = (int) (UI_SIZE_TEXTBOX_HEIGHT * w->element->window->scale);
	int start = index * rowHeight, end = (index + 1) * rowHeight, height = UI_RECT_HEIGHT(w->element->parent->bounds);
	bool unchanged = false;
	if (end >= scroll->position + height) scroll->position = end - height;
	else if (start <= scroll->position) scroll->position = start;
	else unchanged = true;
	if (!unchanged) UIElementRefresh(w->element->parent);
}

void WatchAddExpression(WatchWindow *w, char *string = nullptr) {
	if (!string && w->textbox && !w->textbox->bytes) {
		WatchDestroyTextbox(w);
		return;
	}

	Watch *watch = (Watch *) calloc(1, sizeof(Watch));

	if (string) {
		watch->key = string;
	} else {
		watch->key = (char *) malloc(w->textbox->bytes + 1);
		watch->key[w->textbox->bytes] = 0;
		memcpy(watch->key, w->textbox->string, w->textbox->bytes);
	}

	WatchDeleteExpression(w); // Deletes textbox.
	w->rows.Insert(watch, w->selectedRow);
	w->baseExpressions.Add(watch);
	w->selectedRow++;

	WatchEvaluate("gf_typeof", watch);

	if (!strstr(evaluateResult, "??")) {
		watch->type = strdup(evaluateResult);
		char *end = strchr(watch->type, '\n');
		if (end) *end = 0;
		watch->hasFields = WatchHasFields(watch);
	}
}

int WatchLoggerWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DESTROY) {
		if (element->cp) {
			WatchLogger *logger = (WatchLogger *) element->cp;

			for (int i = 0; i < watchLoggers.Length(); i++) {
				if (watchLoggers[i] == logger) {
					watchLoggers.Delete(i);
					break;
				}
			}

			char buffer[256];
			StringFormat(buffer, sizeof(buffer), "delete %d", logger->id);
			EvaluateCommand(buffer);

			logger->entries.Free();
			free(logger);
		}
	} else if (message == UI_MSG_GET_WIDTH || message == UI_MSG_GET_HEIGHT) {
		return element->window->scale * 200;
	}

	return 0;
}

int WatchLoggerTableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		WatchLogEntry *entry = &((WatchLogger *) element->cp)->entries[m->index];

		if (m->column == 0) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->value);
		} else if (m->column == 1) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->where);
		}
	} else if (message == UI_MSG_LAYOUT) {
		UITable *table = (UITable *) element;
		UI_FREE(table->columnWidths);
		table->columnCount = 2;
		table->columnWidths = (int *) UI_MALLOC(table->columnCount * sizeof(int));
		int available = UI_RECT_WIDTH(table->e.bounds) - UI_SIZE_SCROLL_BAR * element->window->scale;
		table->columnWidths[0] = available / 3;
		table->columnWidths[1] = 2 * available / 3;
	}

	return 0;
}

void WatchChangeLoggerCreate(WatchWindow *w) {
	// TODO Using the correct variable size.
	// TODO Make the MDI child a reasonable width/height by default.

	if (w->selectedRow == w->rows.Length()) {
		return;
	}

	if (!dataTab) {
		UIDialogShow(windowMain, 0, "The data window is not open.\nThe watch log cannot be created.\n%f%b", "OK");
		return;
	}

	WatchEvaluate("gf_addressof", w->rows[w->selectedRow]);

	if (strstr(evaluateResult, "??")) {
		UIDialogShow(windowMain, 0, "Couldn't get the address of the variable.\n%f%b", "OK");
		return;
	}

	char *end = strstr(evaluateResult, " ");

	if (!end) {
		UIDialogShow(windowMain, 0, "Couldn't get the address of the variable.\n%f%b", "OK");
		return;
	}

	*end = 0;
	char buffer[256];
	StringFormat(buffer, sizeof(buffer), "Log %s", evaluateResult);
	UIMDIChild *child = UIMDIChildCreate(&dataWindow->e, UI_MDI_CHILD_CLOSE_BUTTON, UI_RECT_1(0), buffer, -1);
	UITable *table = UITableCreate(&child->e, 0, "New value\tWhere");
	StringFormat(buffer, sizeof(buffer), "watch * %s", evaluateResult);
	EvaluateCommand(buffer);
	char *number = strstr(evaluateResult, "point ");

	if (!number) {
		UIDialogShow(windowMain, 0, "Couldn't set the watchpoint.\n%f%b", "OK");
		return;
	}

	number += 6;

	WatchLogger *logger = (WatchLogger *) calloc(1, sizeof(WatchLogger));
	logger->id = atoi(number);
	logger->table = table;
	child->e.cp = logger;
	table->e.cp = logger;
	child->e.messageUser = WatchLoggerWindowMessage;
	table->e.messageUser = WatchLoggerTableMessage;
	watchLoggers.Add(logger);
	UIElementRefresh(&dataWindow->e);

	UIDialogShow(windowMain, 0, "The log has been setup in the data window.\n%f%b", "OK");
	return;
}

bool WatchLoggerUpdate(char *data) {
	char *stringWatchpoint = strstr(data, "watchpoint ");
	if (!stringWatchpoint) return false;
	char *stringAddressStart = strstr(data, ": * ");
	if (!stringAddressStart) return false;
	int id = atoi(stringWatchpoint + 11);
	char *value = strstr(data, "\nNew value = ");
	if (!value) return false;
	value += 13;
	char *afterValue = strchr(value, '\n');
	if (!afterValue) return false;
	char *where = strstr(afterValue, " at ");
	if (!where) return false;
	where += 4;
	char *afterWhere = strchr(where, '\n');
	if (!afterWhere) return false;

	for (int i = 0; i < watchLoggers.Length(); i++) {
		if (watchLoggers[i]->id == id) {
			*afterValue = 0;
			*afterWhere = 0;
			WatchLogEntry entry = {};
			if (strlen(value) >= sizeof(entry.value)) value[sizeof(entry.value) - 1] = 0;
			if (strlen(where) >= sizeof(entry.where)) where[sizeof(entry.where) - 1] = 0;
			strcpy(entry.value, value);
			strcpy(entry.where, where);
			watchLoggers[i]->entries.Add(entry);
			watchLoggers[i]->table->itemCount++;
			UIElementRefresh(&watchLoggers[i]->table->e);
			DebuggerSend("c", false);
			return true;
		}
	}

	return false;
}

void WatchCreateTextboxForRow(WatchWindow *w, bool addExistingText) {
	int rowHeight = (int) (UI_SIZE_TEXTBOX_HEIGHT * w->element->window->scale);
	UIRectangle row = w->element->bounds;
	row.t += w->selectedRow * rowHeight, row.b = row.t + rowHeight;
	w->textbox = UITextboxCreate(w->element, 0);
	w->textbox->e.messageUser = WatchTextboxMessage;
	w->textbox->e.cp = w;
	UIElementMove(&w->textbox->e, row, true);
	UIElementFocus(&w->textbox->e);

	if (addExistingText) {
		UITextboxReplace(w->textbox, w->rows[w->selectedRow]->key, -1, false);
	}
}

int WatchWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	WatchWindow *w = (WatchWindow *) element->cp;
	int rowHeight = (int) (UI_SIZE_TEXTBOX_HEIGHT * element->window->scale);
	int result = 0;

	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;

		for (int i = (painter->clip.t - element->bounds.t) / rowHeight; i <= w->rows.Length(); i++) {
			UIRectangle row = element->bounds;
			row.t += i * rowHeight, row.b = row.t + rowHeight;

			UIRectangle intersection = UIRectangleIntersection(row, painter->clip);
			if (!UI_RECT_VALID(intersection)) break;

			bool focused = i == w->selectedRow && element->window->focused == element;

			if (focused) UIDrawBlock(painter, row, ui.theme.tableSelected);
			UIDrawBorder(painter, row, ui.theme.border, UI_RECT_4(1, 1, 0, 1));

			row.l += UI_SIZE_TEXTBOX_MARGIN;
			row.r -= UI_SIZE_TEXTBOX_MARGIN;

			if (i != w->rows.Length()) {
				Watch *watch = w->rows[i];
				char buffer[256];

				if ((!watch->value || watch->updateIndex != w->updateIndex) && !watch->open) {
					free(watch->value);
					watch->updateIndex = w->updateIndex;
					WatchEvaluate("gf_valueof", watch);
					watch->value = strdup(evaluateResult);
					char *end = strchr(watch->value, '\n');
					if (end) *end = 0;
				}

				char keyIndex[64];

				if (!watch->key) {
					StringFormat(keyIndex, sizeof(keyIndex), "[%lu]", watch->arrayIndex);
				}

				if (focused && w->waitingForFormatCharacter) {
					StringFormat(buffer, sizeof(buffer), "Enter format character: (e.g. 'x' for hex)");
				} else {
					StringFormat(buffer, sizeof(buffer), "%.*s%s%s%s%s", 
							watch->depth * 2, "                                ",
							watch->open ? "v " : watch->hasFields ? "> " : "", 
							watch->key ?: keyIndex, 
							watch->open ? "" : " = ", 
							watch->open ? "" : watch->value);
				}

				if (focused) {
					UIDrawString(painter, row, buffer, -1, ui.theme.tableSelectedText, UI_ALIGN_LEFT, nullptr);
				} else {
					UIDrawStringHighlighted(painter, row, buffer, -1, 1);
				}
			}
		}
	} else if (message == UI_MSG_GET_HEIGHT) {
		return (w->rows.Length() + 1) * rowHeight;
	} else if (message == UI_MSG_LEFT_DOWN) {
		w->selectedRow = (element->window->cursorY - element->bounds.t) / rowHeight;
		UIElementFocus(element);
		UIElementRepaint(element, nullptr);
	} else if (message == UI_MSG_RIGHT_DOWN) {
		int index = (element->window->cursorY - element->bounds.t) / rowHeight;

		if (index >= 0 && index < w->rows.Length()) {
			WatchWindowMessage(element, UI_MSG_LEFT_DOWN, di, dp);
			UIMenu *menu = UIMenuCreate(&element->window->e, 0);

			if (!w->rows[index]->parent) {
				UIMenuAddItem(menu, 0, "Edit expression", -1, [] (void *cp) { 
					WatchCreateTextboxForRow((WatchWindow *) cp, true); 
				}, w);

				UIMenuAddItem(menu, 0, "Delete", -1, [] (void *cp) { 
					WatchWindow *w = (WatchWindow *) cp;
					WatchDeleteExpression(w); 
					UIElementRefresh(w->element->parent);
					UIElementRefresh(w->element);
				}, w);
			}

			UIMenuAddItem(menu, 0, "Log changes", -1, [] (void *cp) { 
				WatchChangeLoggerCreate((WatchWindow *) cp); 
			}, w);

			UIMenuShow(menu);
		}
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, nullptr);
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;
		result = 1;

		if (w->waitingForFormatCharacter) {
			w->rows[w->selectedRow]->format = (m->textBytes && isalpha(m->text[0])) ? m->text[0] : 0;
			w->rows[w->selectedRow]->updateIndex--;
			w->waitingForFormatCharacter = false;
		} else if ((m->code == UI_KEYCODE_ENTER || m->code == UI_KEYCODE_BACKSPACE) 
				&& w->selectedRow != w->rows.Length() && !w->textbox
				&& !w->rows[w->selectedRow]->parent) {
			WatchCreateTextboxForRow(w, true);
		} else if (m->code == UI_KEYCODE_DELETE && !w->textbox
				&& w->selectedRow != w->rows.Length() && !w->rows[w->selectedRow]->parent) {
			WatchDeleteExpression(w);
		} else if (m->textBytes && m->text[0] == '/' && w->selectedRow != w->rows.Length()) {
			w->waitingForFormatCharacter = true;
		} else if (m->textBytes && m->code != UI_KEYCODE_TAB && !w->textbox && !element->window->ctrl && !element->window->alt
				&& (w->selectedRow == w->rows.Length() || !w->rows[w->selectedRow]->parent)) {
			WatchCreateTextboxForRow(w, false);
			UIElementMessage(&w->textbox->e, message, di, dp);
		} else if (m->code == UI_KEYCODE_ENTER && w->textbox) {
			WatchAddExpression(w);
		} else if (m->code == UI_KEYCODE_ESCAPE) {
			WatchDestroyTextbox(w);
		} else if (m->code == UI_KEYCODE_UP) {
			WatchDestroyTextbox(w);
			w->selectedRow--;
		} else if (m->code == UI_KEYCODE_DOWN) {
			WatchDestroyTextbox(w);
			w->selectedRow++;
		} else if (m->code == UI_KEYCODE_HOME) {
			w->selectedRow = 0;
		} else if (m->code == UI_KEYCODE_END) {
			w->selectedRow = w->rows.Length();
		} else if (m->code == UI_KEYCODE_RIGHT && !w->textbox
				&& w->selectedRow != w->rows.Length() && w->rows[w->selectedRow]->hasFields
				&& !w->rows[w->selectedRow]->open) {
			Watch *watch = w->rows[w->selectedRow];
			watch->open = true;
			WatchAddFields(watch);
			int position = w->selectedRow + 1;
			WatchInsertFieldRows(w, watch, &position);
			WatchEnsureRowVisible(w, position - 1);
		} else if (m->code == UI_KEYCODE_LEFT && !w->textbox
				&& w->selectedRow != w->rows.Length() && w->rows[w->selectedRow]->hasFields
				&& w->rows[w->selectedRow]->open) {
			int end = w->selectedRow + 1;

			for (; end < w->rows.Length(); end++) {
				if (w->rows[w->selectedRow]->depth >= w->rows[end]->depth) {
					break;
				}
			}

			w->rows.Delete(w->selectedRow + 1, end - w->selectedRow - 1);
			w->rows[w->selectedRow]->open = false;
		} else if (m->code == UI_KEYCODE_LEFT && !w->textbox 
				&& w->selectedRow != w->rows.Length() && !w->rows[w->selectedRow]->open) {
			for (int i = 0; i < w->rows.Length(); i++) {
				if (w->rows[w->selectedRow]->parent == w->rows[i]) {
					w->selectedRow = i;
					break;
				}
			}
		} else {
			result = 0;
		}

		WatchEnsureRowVisible(w, w->selectedRow);
		UIElementRefresh(element->parent);
		UIElementRefresh(element);
	}

	if (w->selectedRow < 0) {
		w->selectedRow = 0;
	} else if (w->selectedRow > w->rows.Length()) {
		w->selectedRow = w->rows.Length();
	}

	return result;
}

int WatchPanelMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_LEFT_DOWN) {
		UIElement *window = ((WatchWindow *) element->cp)->element;
		UIElementFocus(window);
		UIElementRepaint(window, nullptr);
	}

	return 0;
}

UIElement *WatchWindowCreate(UIElement *parent) {
	WatchWindow *w = (WatchWindow *) calloc(1, sizeof(WatchWindow));
	UIPanel *panel = UIPanelCreate(parent, UI_PANEL_SCROLL | UI_PANEL_GRAY);
	panel->e.messageUser = WatchPanelMessage;
	panel->e.cp = w;
	w->element = UIElementCreate(sizeof(UIElement), &panel->e, UI_ELEMENT_H_FILL | UI_ELEMENT_TAB_STOP, WatchWindowMessage, "Watch");
	w->element->cp = w;
	return &panel->e;
}

void WatchWindowUpdate(const char *, UIElement *element) {
	WatchWindow *w = (WatchWindow *) element->cp;

	for (int i = 0; i < w->baseExpressions.Length(); i++) {
		Watch *watch = w->baseExpressions[i];
		WatchEvaluate("gf_typeof", watch);
		char *result = strdup(evaluateResult);
		char *end = strchr(result, '\n');
		if (end) *end = 0;
		const char *oldType = watch->type ?: "??";

		if (strcmp(result, oldType)) {
			free(watch->type);
			watch->type = result;

			for (int j = 0; j < w->rows.Length(); j++) {
				if (w->rows[j] == watch) {
					w->selectedRow = j;
					WatchAddExpression(w, strdup(watch->key));
					w->selectedRow = w->rows.Length(), i--;
					break;
				}
			}
		} else {
			free(result);
		}
	}

	w->updateIndex++;
	UIElementRefresh(element->parent);
	UIElementRefresh(element);
}

//////////////////////////////////////////////////////
// Stack window:
//////////////////////////////////////////////////////

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
			UIElementRepaint(element, nullptr);
		}
	}

	return 0;
}

UIElement *StackWindowCreate(UIElement *parent) {
	UITable *table = UITableCreate(parent, 0, "Index\tFunction\tLocation\tAddress");
	table->e.messageUser = TableStackMessage;
	return &table->e;
}

void StackWindowUpdate(const char *, UIElement *_table) {
	UITable *table = (UITable *) _table;
	table->itemCount = stack.Length();
	UITableResizeColumns(table);
	UIElementRefresh(&table->e);
}

//////////////////////////////////////////////////////
// Breakpoints window:
//////////////////////////////////////////////////////

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
			UIMenu *menu = UIMenuCreate(&element->window->e, 0);
			UIMenuAddItem(menu, 0, "Delete", -1, CommandDeleteBreakpoint, (void *) (intptr_t) index);
			UIMenuShow(menu);
		}
	} else if (message == UI_MSG_LEFT_DOWN) {
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY);
		if (index != -1) DisplaySetPosition(breakpoints[index].file, breakpoints[index].line, false);
	}

	return 0;
}

UIElement *BreakpointsWindowCreate(UIElement *parent) {
	UITable *table = UITableCreate(parent, 0, "File\tLine");
	table->e.messageUser = TableBreakpointsMessage;
	return &table->e;
}

void BreakpointsWindowUpdate(const char *, UIElement *_table) {
	UITable *table = (UITable *) _table;
	table->itemCount = breakpoints.Length();
	UITableResizeColumns(table);
	UIElementRefresh(&table->e);
}

//////////////////////////////////////////////////////
// Data window:
//////////////////////////////////////////////////////

UIButton *buttonFillWindow;

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

void CommandToggleFillDataTab(void *) {
	// HACK.

	if (!dataTab) return;
	static UIElement *oldParent;
	UIWindow *window = dataTab->e.window;
	
	if (window->e.children == &dataTab->e) {
		UIElementChangeParent(&dataTab->e, oldParent, false);
		buttonFillWindow->e.flags &= ~UI_BUTTON_CHECKED;
		UIElementRefresh(&window->e);
		UIElementRefresh(window->e.children);
		UIElementRefresh(oldParent);
	} else {
		dataTab->e.flags &= ~UI_ELEMENT_HIDE;
		UIElementMessage(&dataTab->e, UI_MSG_TAB_SELECTED, 0, 0);
		oldParent = dataTab->e.parent;
		window->e.children->clip = UI_RECT_1(0);
		UIElementChangeParent(&dataTab->e, &window->e, true);
		buttonFillWindow->e.flags |= UI_BUTTON_CHECKED;
		UIElementRefresh(&window->e);
		UIElementRefresh(&dataTab->e);
	}
}

UIElement *DataWindowCreate(UIElement *parent) {
	dataTab = UIPanelCreate(parent, UI_PANEL_EXPAND);
	UIPanel *panel5 = UIPanelCreate(&dataTab->e, UI_PANEL_GRAY | UI_PANEL_HORIZONTAL | UI_PANEL_SMALL_SPACING);
	buttonFillWindow = UIButtonCreate(&panel5->e, UI_BUTTON_SMALL, "Fill window", -1);
	buttonFillWindow->invoke = CommandToggleFillDataTab;
	UIButtonCreate(&panel5->e, UI_BUTTON_SMALL, "Add bitmap...", -1)->invoke = BitmapAddDialog;
	dataWindow = UIMDIClientCreate(&dataTab->e, UI_ELEMENT_V_FILL);
	dataTab->e.messageUser = DataTabMessage;
	return &dataTab->e;
}

//////////////////////////////////////////////////////
// Struct window:
//////////////////////////////////////////////////////

struct StructWindow {
	UICode *display;
	UITextbox *textbox;
};

int TextboxStructNameMessage(UIElement *element, UIMessage message, int di, void *dp) {
	StructWindow *window = (StructWindow *) element->cp;

	if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		if (m->code == UI_KEYCODE_ENTER) {
			char buffer[4096];
			StringFormat(buffer, sizeof(buffer), "ptype /o %.*s", (int) window->textbox->bytes, window->textbox->string);
			EvaluateCommand(buffer);
			char *end = strstr(evaluateResult, "\n(gdb)");
			if (end) *end = 0;
			UICodeInsertContent(window->display, evaluateResult, -1, true);
			UITextboxClear(window->textbox, false);
			UIElementRefresh(&window->display->e);
			UIElementRefresh(element);
			return 1;
		}
	}

	return 0;
}

UIElement *StructWindowCreate(UIElement *parent) {
	StructWindow *window = (StructWindow *) calloc(1, sizeof(StructWindow));
	UIPanel *panel = UIPanelCreate(parent, UI_PANEL_GRAY | UI_PANEL_EXPAND);
	window->textbox = UITextboxCreate(&panel->e, 0);
	window->textbox->e.messageUser = TextboxStructNameMessage;
	window->textbox->e.cp = window;
	window->display = UICodeCreate(&panel->e, UI_ELEMENT_V_FILL | UI_CODE_NO_MARGIN);
	UICodeInsertContent(window->display, "Type the name of a struct\nto view its layout.", -1, false);
	return &panel->e;
}

//////////////////////////////////////////////////////
// Files window:
//////////////////////////////////////////////////////

struct FilesWindow {
	char directory[PATH_MAX];
	UIPanel *panel;
};

bool FilesPanelPopulate(FilesWindow *window);

int FilesButtonMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		FilesWindow *window = (FilesWindow *) element->cp;
		const char *name = ((UIButton *) element)->label;
		size_t oldLength = strlen(window->directory);
		strcat(window->directory, "/");
		strcat(window->directory, name);
		struct stat s;
		stat(window->directory, &s);

		if (S_ISDIR(s.st_mode)) {
			if (FilesPanelPopulate(window)) {
				char copy[PATH_MAX];
				realpath(window->directory, copy);
				strcpy(window->directory, copy);
				return 0;
			}
		} else if (S_ISREG(s.st_mode)) {
			DisplaySetPosition(window->directory, 1, false);
		}

		window->directory[oldLength] = 0;
	}

	return 0;
}

bool FilesPanelPopulate(FilesWindow *window) {
	DIR *directory = opendir(window->directory);
	struct dirent *entry;
	if (!directory) return false;
	Array<char *> names = {};
	while ((entry = readdir(directory))) names.Add(strdup(entry->d_name));
	closedir(directory);
	UIElementDestroyDescendents(&window->panel->e);

	qsort(names.array, names.Length(), sizeof(char *), [] (const void *a, const void *b) {
		return strcmp(*(const char **) a, *(const char **) b);
	});

	for (int i = 0; i < names.Length(); i++) {
		if (names[i][0] != '.' || names[i][1] != 0) {
			UIButton *button = UIButtonCreate(&window->panel->e, 0, names[i], -1);
			button->e.cp = window;
			button->e.messageUser = FilesButtonMessage;
		}

		free(names[i]);
	}

	names.Free();
	UIElementRefresh(&window->panel->e);
	return true;
}

UIElement *FilesWindowCreate(UIElement *parent) {
	FilesWindow *window = (FilesWindow *) calloc(1, sizeof(FilesWindow));
	window->panel = UIPanelCreate(parent, UI_PANEL_GRAY | UI_PANEL_EXPAND | UI_PANEL_SCROLL);
	window->panel->e.cp = window;
	getcwd(window->directory, sizeof(window->directory));
	FilesPanelPopulate(window);
	return &window->panel->e;
}

//////////////////////////////////////////////////////
// Registers window:
//////////////////////////////////////////////////////

struct RegisterData { char string[128]; };
Array<RegisterData> registerData;

UIElement *RegistersWindowCreate(UIElement *parent) {
	return &UIPanelCreate(parent, UI_PANEL_SMALL_SPACING | UI_PANEL_GRAY | UI_PANEL_SCROLL)->e;
}

void RegistersWindowUpdate(const char *, UIElement *panel) {
	EvaluateCommand("info registers");

	if (strstr(evaluateResult, "The program has no registers now.")
			|| strstr(evaluateResult, "The current thread has terminated")) {
		return;
	}

	UIElementDestroyDescendents(panel);
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

		UIPanel *row = UIPanelCreate(panel, UI_PANEL_HORIZONTAL | UI_ELEMENT_H_FILL);
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

	UIElementRefresh(panel);
	registerData.Free();
	registerData = newRegisterData;
}

//////////////////////////////////////////////////////
// Commands window:
//////////////////////////////////////////////////////

Array<INIState> presetCommands;

void CommandPreset(void *_index) {
	char *copy = strdup(presetCommands[(intptr_t) _index].value);
	char *position = copy;

	while (true) {
		char *end = strchr(position, ';');
		if (end) *end = 0;
		EvaluateCommand(position, true);
		if (displayOutput) UICodeInsertContent(displayOutput, evaluateResult, -1, false);
		if (end) position = end + 1;
		else break;
	}

	if (displayOutput) UIElementRefresh(&displayOutput->e);
	free(copy);
}

UIElement *CommandsWindowCreate(UIElement *parent) {
	UIPanel *panel = UIPanelCreate(parent, UI_PANEL_GRAY | UI_PANEL_SMALL_SPACING | UI_PANEL_EXPAND | UI_PANEL_SCROLL);
	if (!presetCommands.Length()) UILabelCreate(&panel->e, 0, "No preset commands found in config file!", -1);

	for (int i = 0; i < presetCommands.Length(); i++) {
		UIButton *button = UIButtonCreate(&panel->e, 0, presetCommands[i].key, -1);
		button->e.cp = (void *) (intptr_t) i;
		button->invoke = CommandPreset;
	}

	return &panel->e;
}

//////////////////////////////////////////////////////
// Interface and main:
//////////////////////////////////////////////////////

struct InterfaceCommand {
	const char *label;
	UIShortcut shortcut;
};

struct InterfaceWindow {
	const char *name;
	UIElement *(*create)(UIElement *parent);
	void (*update)(const char *data, UIElement *element);
	UIElement *element;
	bool queuedUpdate;
};

const InterfaceCommand interfaceCommands[] = {
	{ .label = "Run\tShift+F5", { .code = UI_KEYCODE_F5, .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "r" } },
	{ .label = "Run paused\tCtrl+F5", { .code = UI_KEYCODE_F5, .ctrl = true, .invoke = CommandSendToGDB, .cp = (void *) "start" } },
	{ .label = "Kill\tF3", { .code = UI_KEYCODE_F3, .invoke = CommandSendToGDB, .cp = (void *) "kill" } },
	{ .label = "Restart GDB\tCtrl+R", { .code = UI_KEYCODE_LETTER('R'), .ctrl = true, .invoke = CommandRestartGDB } },
	{ .label = "Connect\tF4", { .code = UI_KEYCODE_F4, .invoke = CommandSendToGDB, .cp = (void *) "target remote :1234" } },
	{ .label = "Continue\tF5", { .code = UI_KEYCODE_F5, .invoke = CommandSendToGDB, .cp = (void *) "c" } },
	{ .label = "Step over\tF10", { .code = UI_KEYCODE_F10, .invoke = CommandSendToGDBStep, .cp = (void *) "n" } },
	{ .label = "Step out of block\tShift+F10", { .code = UI_KEYCODE_F10, .shift = true, .invoke = CommandStepOutOfBlock } },
	{ .label = "Step in\tF11", { .code = UI_KEYCODE_F11, .invoke = CommandSendToGDBStep, .cp = (void *) "s" } },
	{ .label = "Step out\tShift+F11", { .code = UI_KEYCODE_F11, .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "finish" } },
	{ .label = "Pause\tF8", { .code = UI_KEYCODE_F8, .invoke = CommandPause } },
	{ .label = "Toggle breakpoint\tF9", { .code = UI_KEYCODE_F9, .invoke = CommandToggleBreakpoint } },
	{ .label = "Sync with gvim\tF2", { .code = UI_KEYCODE_F2, .invoke = CommandSyncWithGvim } },
	{ .label = "Ask GDB for PWD\tCtrl+Shift+P", { .code = UI_KEYCODE_LETTER('P'), .ctrl = true, .shift = true, .invoke = CommandAskGDBForPWD } },
	{ .label = "Toggle disassembly\tCtrl+D", { .code = UI_KEYCODE_LETTER('D'), .ctrl = true, .invoke = CommandToggleDisassembly } },
	{ .label = nullptr, { .code = UI_KEYCODE_LETTER('B'), .ctrl = true, .invoke = CommandToggleFillDataTab } },
	{ .label = "Donate", { .invoke = CommandDonate } },
};

InterfaceWindow interfaceWindows[] = {
	{ "Stack", StackWindowCreate, StackWindowUpdate, },
	{ "Source", SourceWindowCreate, SourceWindowUpdate, },
	{ "Breakpoints", BreakpointsWindowCreate, BreakpointsWindowUpdate, },
	{ "Registers", RegistersWindowCreate, RegistersWindowUpdate, },
	{ "Watch", WatchWindowCreate, WatchWindowUpdate, },
	{ "Commands", CommandsWindowCreate, nullptr, },
	{ "Data", DataWindowCreate, nullptr, },
	{ "Struct", StructWindowCreate, nullptr, },
	{ "Files", FilesWindowCreate, nullptr, },
	{ "Console", ConsoleWindowCreate, nullptr, },
};

void LoadSettings(bool earlyPass) {
	char globalConfigPath[4096];
	StringFormat(globalConfigPath, 4096, "%s/.config/gf2_config.ini", getenv("HOME"));
	bool currentFolderIsTrusted = false;
	static bool cwdConfigNotTrusted = false;

	for (int i = 0; i < 2; i++) {
		INIState state = { .buffer = LoadFile(i ? ".project.gf" : globalConfigPath, &state.bytes) };

		if (earlyPass && i && !currentFolderIsTrusted && state.buffer) {
			fprintf(stderr, "Would you like to load the config file .project.gf from your current directory?\n");
			fprintf(stderr, "You have not loaded this config file before.\n");
			fprintf(stderr, "(Y) - Yes, and add it to the list of trusted files\n");
			fprintf(stderr, "(N) - No\n");
			char c = 'n';
			fread(&c, 1, 1, stdin);

			if (c != 'y') {
				cwdConfigNotTrusted = true;
				break;
			} else {
				char *config = LoadFile(globalConfigPath, nullptr);
				size_t length = config ? strlen(config) : 0;
				size_t insert = 0;
				const char *sectionString = "\n[trusted_folders]\n";
				bool addSectionString = true;

				if (config) {
					char *section = strstr(config, sectionString);

					if (section) {
						insert = section - config + strlen(sectionString);
						addSectionString = false;
					} else {
						insert = length;
					}
				}

				FILE *f = fopen(globalConfigPath, "wb");
				
				if (!f) {
					fprintf(stderr, "Error: Could not modify the global config file!\n");
				} else {
					if (insert) fwrite(config, 1, insert, f);
					if (addSectionString) fwrite(sectionString, 1, strlen(sectionString), f);
					char path[PATH_MAX];
					getcwd(path, sizeof(path));
					fwrite(path, 1, strlen(path), f);
					char newline = '\n';
					fwrite(&newline, 1, 1, f);
					if (length - insert) fwrite(config + insert, 1, length - insert, f);
					fclose(f);
				}
			}
		} else if (!earlyPass && cwdConfigNotTrusted && i) {
			break;
		}

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

				UIWindowRegisterShortcut(windowMain, shortcut);
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
			} else if (0 == strcmp(state.section, "trusted_folders") && earlyPass && state.keyBytes) {
				char path[PATH_MAX];
				getcwd(path, sizeof(path));
				if (0 == strcmp(path, state.key)) currentFolderIsTrusted = true;
			} else if (0 == strcmp(state.section, "theme") && earlyPass && state.keyBytes && state.valueBytes) {
				for (uintptr_t i = 0; i < sizeof(themeItems) / sizeof(themeItems[0]); i++) {
					if (strcmp(state.key, themeItems[i])) continue;
					ui.theme.colors[i] = strtoul(state.value, nullptr, 16);
				}
			}
		}
	}
}

void InterfaceShowMenu(void *self) {
	UIMenu *menu = UIMenuCreate((UIElement *) self, UI_MENU_PLACE_ABOVE);

	for (uintptr_t i = 0; i < sizeof(interfaceCommands) / sizeof(interfaceCommands[0]); i++) {
		if (!interfaceCommands[i].label) continue;
		UIMenuAddItem(menu, 0, interfaceCommands[i].label, -1, interfaceCommands[i].shortcut.invoke, interfaceCommands[i].shortcut.cp);
	}

	UIMenuShow(menu);
}

bool ElementHidden(UIElement *element) {
	while (element) {
		if (element->flags & UI_ELEMENT_HIDE) {
			return true;
		} else {
			element = element->parent;
		}
	}

	return false;
}

int WindowMessage(UIElement *, UIMessage message, int di, void *dp) {
	if (message == MSG_RECEIVED_DATA) {
		programRunning = false;
		char *input = (char *) dp;

		if (firstUpdate) EvaluateCommand(pythonCode);
		firstUpdate = false;

		if (WatchLoggerUpdate(input)) goto skip;
		if (showingDisassembly) DisassemblyUpdateLine();

		DebuggerGetStack();
		DebuggerGetBreakpoints();

		for (uintptr_t i = 0; i < sizeof(interfaceWindows) / sizeof(interfaceWindows[0]); i++) {
			InterfaceWindow *window = &interfaceWindows[i];
			if (!window->update || !window->element) continue;
			if (ElementHidden(window->element)) window->queuedUpdate = true;
			else window->update(input, window->element);
		}

		BitmapViewerUpdateAll();

		if (displayOutput) {
			UICodeInsertContent(displayOutput, input, -1, false);
			UIElementRefresh(&displayOutput->e);
		}

		if (trafficLight) UIElementRepaint(&trafficLight->e, nullptr);

		skip:;
		free(input);
	} else if (message == MSG_RECEIVED_CONTROL) {
		char *input = (char *) dp;
		char *end = strchr(input, '\n');
		if (end) *end = 0;

		if (input[0] == 'f' && input[1] == ' ') {
			DisplaySetPosition(input + 2, 1, false);
		} else if (input[0] == 'l' && input[1] == ' ') {
			DisplaySetPosition(nullptr, atoi(input + 2), false);
		} else if (input[0] == 'c' && input[1] == ' ') {
			DebuggerSend(input + 2, true);
		}

		free(input);
	}

	return 0;
}

int InterfaceTabPaneMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_LAYOUT) {
		element->messageClass(element, message, di, dp);

		for (uintptr_t i = 0; i < sizeof(interfaceWindows) / sizeof(interfaceWindows[0]); i++) {
			InterfaceWindow *window = &interfaceWindows[i];

			if (window->element && (~window->element->flags & UI_ELEMENT_HIDE) && window->queuedUpdate) {
				window->queuedUpdate = false;
				window->update("", window->element);
			}
		}

		return 1;
	}

	return 0;
}

void InterfaceLayoutCreate(UIElement *parent) {
	UIElement *container = nullptr;

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
		container->messageUser = InterfaceTabPaneMessage;
	} else {
		bool found = false;

		for (uintptr_t i = 0; i < sizeof(interfaceWindows) / sizeof(interfaceWindows[0]); i++) {
			InterfaceWindow *w = interfaceWindows + i;

			if (strlen(layoutString) > strlen(w->name) && 0 == memcmp(layoutString, w->name, strlen(w->name)) && 
					(layoutString[strlen(w->name)] == ',' || layoutString[strlen(w->name)] == ')')) { 
				layoutString += strlen(w->name);
				w->element = w->create(parent);
				found = true;
				break;
			}
		}

		assert(found);
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

	windowMain = _window;
	windowMain->scale = uiScale;
	windowMain->e.messageUser = WindowMessage;

	InterfaceLayoutCreate(&UIPanelCreate(&windowMain->e, UI_PANEL_EXPAND)->e);

	for (uintptr_t i = 0; i < sizeof(interfaceCommands) / sizeof(interfaceCommands[0]); i++) {
		if (!interfaceCommands[i].shortcut.code) continue;
		UIWindowRegisterShortcut(windowMain, interfaceCommands[i].shortcut);
	}

	LoadSettings(false);

	pthread_cond_init(&evaluateEvent, nullptr);
	pthread_mutex_init(&evaluateMutex, nullptr);
	DebuggerStartThread();
}

void SignalINT(int sig) {
	DebuggerClose();
	exit(0);
}

#ifndef EMBED_GF
int main(int argc, char **argv) {
	struct sigaction sigintHandler = {};
	sigintHandler.sa_handler = SignalINT;
	sigaction(SIGINT, &sigintHandler, nullptr);

	StringFormat(controlPipePath, sizeof(controlPipePath), "%s/.config/gf2_control.dat", getenv("HOME"));
	mkfifo(controlPipePath, 6 + 6 * 8 + 6 * 64);
	pthread_t thread;
	pthread_create(&thread, nullptr, ControlPipeThread, nullptr);

	// Setup GDB arguments.
	gdbArgv = (char **) malloc(sizeof(char *) * (argc + 1));
	gdbArgv[0] = (char *) "gdb";
	memcpy(gdbArgv + 1, argv + 1, sizeof(argv) * argc);
	gdbArgc = argc;

	ConvertOldTheme();
	LoadSettings(true);
	UIInitialise();
	InterfaceCreate(UIWindowCreate(0, 0, "gf2", 0, 0));
	CommandSyncWithGvim(nullptr);
	UIMessageLoop();
	DebuggerClose();

	return 0;
}
#endif
