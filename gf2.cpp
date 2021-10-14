// TODO Disassembly window:
// 	- Setting/clearing/showing breakpoints.
// 	- Jump/run to line.
// 	- Shift+F10: run to next instruction (for skipping past loops).
// 	- Split source and disassembly view.

// TODO Inspect line mode:
// 	- Jump/run to selected line.
// 	- How to show overloaded variables correctly when moving lines?

// TODO Other features:
// 	- Automatically restoring breakpoints and symbols files after restarting gdb.
// 	- Viewing data breakpoints in the breakpoint window.
// 	- Using the correct variable size in the watch logger.

// TODO Future extensions:
// 	- Memory window.
// 	- More data visualization tools in the data window.

#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>

char *layoutString = (char *) "v(75,h(80,Source,v(50,t(Breakpoints,Commands,Struct),t(Stack,Files,Thread))),h(65,Console,t(Watch,Registers,Data)))";
// char *layoutString = (char *) "h(70,v(80,Source,Console),v(33,t(Breakpoints,Commands,Struct),v(50,t(Stack,Files,Thread),t(Watch,Registers,Data))))";

int fontSize = 13;
float uiScale = 1;
bool maximize = false;

extern "C" {
#define UI_FONT_SIZE (fontSize)
#define UI_LINUX
#define UI_IMPLEMENTATION
#include "luigi.h"
}

#define MSG_RECEIVED_DATA ((UIMessage) (UI_MSG_USER + 1))
#define MSG_RECEIVED_CONTROL ((UIMessage) (UI_MSG_USER + 2))
#define MSG_RECEIVED_LOG ((UIMessage) (UI_MSG_USER + 3))

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

// General:

struct INIState {
	char *buffer, *section, *key, *value;
	size_t bytes, sectionBytes, keyBytes, valueBytes;
};

FILE *commandLog;
char emptyString;
bool programRunning;
const char *vimServerName = "GVIM";
const char *logPipePath;
const char *controlPipePath;
Array<INIState> presetCommands;

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
UITextbox *textboxInput;
UISpacer *trafficLight;

UIMDIClient *dataWindow;
UIPanel *dataTab;

// Breakpoints:

struct Breakpoint {
	char file[64];
	char fileFull[PATH_MAX];
	int line;
	int watchpoint;
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
            if isinstance(index, str) and index[0] == '[':
                basic_type = gdb.types.get_basic_type(value.type)
                if basic_type.code == gdb.TYPE_CODE_PTR:
                    basic_type = gdb.types.get_basic_type(basic_type.target())
                value = gf_hooks[str(basic_type)](value, index)
            else:
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
    try: gf_hooks[str(basic_type)](basic_type, None)
    except: _gf_fields_recurse(basic_type)

end
)";

// Forward declarations:

bool DisplaySetPosition(const char *file, int line, bool useGDBToGetFullPath);
void InterfaceShowMenu(void *self);
UIElement *InterfaceWindowSwitchToAndFocus(const char *name);
void WatchAddExpression2(char *string);
int WatchWindowMessage(UIElement *element, UIMessage message, int di, void *dp);

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

	if (!buffer) {
		fclose(f);
		return nullptr;
	}

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

int SourceFindEndOfBlock() {
	if (!currentLine || currentLine - 1 >= displayCode->lineCount) return -1;

	int tabs = 0;

	for (int i = 0; i < displayCode->lines[currentLine - 1].bytes; i++) {
		if (isspace(displayCode->content[displayCode->lines[currentLine - 1].offset + i])) tabs++;
		else break;
	}

	for (int j = currentLine; j < displayCode->lineCount; j++) {
		int t = 0;

		for (int i = 0; i < displayCode->lines[j].bytes - 1; i++) {
			if (isspace(displayCode->content[displayCode->lines[j].offset + i])) t++;
			else break;
		}

		if (t < tabs && displayCode->content[displayCode->lines[j].offset + t] == '}') {
			return j + 1;
		}
	}

	return -1;
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

	const char *setPrompt = "set prompt (gdb) \n";
	write(pipeToGDB, setPrompt, strlen(setPrompt));

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

void DebuggerSend(const char *string, bool echo, bool synchronous) {
	if (synchronous) {
		if (programRunning) {
			kill(gdbPID, SIGINT);
			usleep(1000 * 1000);
			programRunning = false;
		}

		evaluateMode = true;
		pthread_mutex_lock(&evaluateMutex);
	}

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

	if (synchronous) {
		struct timespec timeout;
		clock_gettime(CLOCK_REALTIME, &timeout);
		timeout.tv_sec++;
		pthread_cond_timedwait(&evaluateEvent, &evaluateMutex, &timeout);
		pthread_mutex_unlock(&evaluateMutex);
		programRunning = false;
		if (trafficLight) UIElementRepaint(&trafficLight->e, nullptr);
		if (!evaluateResult) evaluateResult = strdup("\n(gdb) \n");
	}
}

void EvaluateCommand(const char *command, bool echo = false) {
	DebuggerSend(command, echo, true);
}

const char *EvaluateExpression(const char *expression, const char *format = nullptr) {
	char buffer[1024];
	StringFormat(buffer, sizeof(buffer), "p%s %s", format ?: "", expression);
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

void DebuggerClose() {
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
		} else {
			if (!strstr(position, "watchpoint")) goto doNext;
			const char *address = strstr(position, " y  ");
			if (!address) goto doNext;
			address += 2;
			while (*address == ' ') address++;
			if (isspace(*address)) goto doNext;
			const char *end = strchr(address, '\n');
			if (!end) goto doNext;
			breakpoint.watchpoint = atoi(position + 1);
			snprintf(breakpoint.file, sizeof(breakpoint.file), "%.*s", (int) (end - address), address);
			breakpoints.Add(breakpoint);
		}

		doNext:;
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

bool useHardwareBreakpoints;
#define BREAK_COMMAND (useHardwareBreakpoints ? "hbreak" : "b")
#define TBREAK_COMMAND (useHardwareBreakpoints ? "thbreak" : "tbreak")

bool CommandParseInternal(const char *command, bool synchronous) {
	if (0 == strcmp(command, "gf-step")) {
		DebuggerSend(showingDisassembly ? "stepi" : "s", true, synchronous);
		return true;
	} else if (0 == strcmp(command, "gf-next")) {
		DebuggerSend(showingDisassembly ? "nexti" : "n", true, synchronous);
		return true;
	} else if (0 == strcmp(command, "gf-step-out-of-block")) {
		int line = SourceFindEndOfBlock();

		if (line != -1) {
			char buffer[256];
			StringFormat(buffer, sizeof(buffer), "until %d", line);
			DebuggerSend(buffer, true, synchronous);
			return false;
		}
	} else if (0 == strcmp(command, "gf-restart-gdb")) {
		firstUpdate = true;
		kill(gdbPID, SIGKILL);
		pthread_cancel(gdbThread); // TODO Is there a nicer way to do this?
		receiveBufferPosition = 0;
		DebuggerStartThread();
	} else if (0 == strcmp(command, "gf-get-pwd")) {
		EvaluateCommand("info source");
		const char *needle = "Compilation directory is ";
		char *pwd = strstr(evaluateResult, needle);

		if (pwd) {
			pwd += strlen(needle);
			char *end = strchr(pwd, '\n');
			if (end) *end = 0;

			if (!chdir(pwd)) {
				if (!displayOutput) return false;
				char buffer[4096];
				StringFormat(buffer, sizeof(buffer), "New working directory: %s", pwd);
				UICodeInsertContent(displayOutput, buffer, -1, false);
				UIElementRefresh(&displayOutput->e);
				return false;
			}
		}

		UIDialogShow(windowMain, 0, "Couldn't get the working directory.\n%f%b", "OK");
	} else if (strlen(command) > 13 && 0 == memcmp(command, "gf-switch-to ", 13)) {
		InterfaceWindowSwitchToAndFocus(command + 13);
	} else if (strlen(command) > 11 && 0 == memcmp(command, "gf-command ", 11)) {
		for (int i = 0; i < presetCommands.Length(); i++) {
			if (strcmp(command + 11, presetCommands[i].key)) continue;
			char *copy = strdup(presetCommands[i].value);
			char *position = copy;

			while (true) {
				char *end = strchr(position, ';');
				if (end) *end = 0;
				char *async = strchr(position, '&');
				if (async && !async[1]) *async = 0; else async = nullptr;
				if (synchronous) async = nullptr; // Trim the '&' character, but run synchronously anyway.
				bool hasOutput = CommandParseInternal(position, !async) && !async;
				if (displayOutput && hasOutput) UICodeInsertContent(displayOutput, evaluateResult, -1, false);
				if (end) position = end + 1;
				else break;
			}

			if (displayOutput) UIElementRefresh(&displayOutput->e);
			free(copy);
			break;
		}
	} else {
		DebuggerSend(command, true, synchronous);
		return true;
	}

	return false;
}

void CommandSendToGDB(void *_string) {
	CommandParseInternal((const char *) _string, false);
}

void CommandDeleteBreakpoint(void *_index) {
	int index = (int) (intptr_t) _index;
	Breakpoint *breakpoint = &breakpoints[index];
	char buffer[1024];
	if (breakpoint->watchpoint) StringFormat(buffer, 1024, "delete %d", breakpoint->watchpoint);
	else StringFormat(buffer, 1024, "clear %s:%d", breakpoint->file, breakpoint->line);
	DebuggerSend(buffer, true, false);
}

void CommandPause(void *) {
	kill(gdbPID, SIGINT);
}

void CommandSyncWithGvim(void *) {
	char buffer[1024];
	StringFormat(buffer, sizeof(buffer), "vim --servername %s --remote-expr \"execute(\\\"ls\\\")\" | grep %%", vimServerName);
	FILE *file = popen(buffer, "r");
	if (!file) return;
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
	char buffer2[PATH_MAX];

	if (name[0] != '/' && name[0] != '~') {
		char buffer[1024];
		StringFormat(buffer, sizeof(buffer), "vim --servername %s --remote-expr \"execute(\\\"pwd\\\")\" | grep '/'", vimServerName);
		FILE *file = popen(buffer, "r");
		if (!file) return;
		buffer[fread(buffer, 1, 1023, file)] = 0;
		pclose(file);
		if (!strchr(buffer, '\n')) return;
		*strchr(buffer, '\n') = 0;
		StringFormat(buffer2, sizeof(buffer2), "%s/%s", buffer, name);
	} else {
		strcpy(buffer2, name);
	}

	DisplaySetPosition(buffer2, lineNumber, false);
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
			DebuggerSend(buffer, true, false);
			return;
		}
	}

	char buffer[1024];
	StringFormat(buffer, 1024, "%s %s:%d", BREAK_COMMAND, currentFile, line);
	DebuggerSend(buffer, true, false);
}

void CommandCustom(void *_command) {
	const char *command = (const char *) _command;

	if (0 == memcmp(command, "shell ", 6)) {
		// TODO Move this into CommandParseInternal?

		char buffer[4096];
		StringFormat(buffer, 4096, "Running shell command \"%s\"...\n", command);
		if (displayOutput) UICodeInsertContent(displayOutput, buffer, -1, false);
		StringFormat(buffer, 4096, "%s > .output.gf 2>&1", command);
		int start = time(nullptr);
		int result = system(buffer);
		size_t bytes = 0;
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
		CommandParseInternal(command, false);
	}
}

void CommandDonate(void *) {
	system("xdg-open https://www.patreon.com/nakst");
}

//////////////////////////////////////////////////////
// Themes:
//////////////////////////////////////////////////////

const char *themeItems[] = {
	"panel1", "panel2", "selected", "border", "text", "textDisabled", "textSelected",
	"buttonNormal", "buttonHovered", "buttonPressed", "buttonDisabled", "textboxNormal", "textboxFocused",
	"codeFocused", "codeBackground", "codeDefault", "codeComment", "codeString", "codeNumber", "codeOperator", "codePreprocessor",
};

//////////////////////////////////////////////////////
// Debug windows:
//////////////////////////////////////////////////////

#include "windows.cpp"

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
	void (*focus)(UIElement *element);
	UIElement *element;
	bool queuedUpdate;
};

const InterfaceCommand interfaceCommands[] = {
	{ .label = "Run\tShift+F5", { .code = UI_KEYCODE_FKEY(5), .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "r" } },
	{ .label = "Run paused\tCtrl+F5", { .code = UI_KEYCODE_FKEY(5), .ctrl = true, .invoke = CommandSendToGDB, .cp = (void *) "start" } },
	{ .label = "Kill\tF3", { .code = UI_KEYCODE_FKEY(3), .invoke = CommandSendToGDB, .cp = (void *) "kill" } },
	{ .label = "Restart GDB\tCtrl+R", { .code = UI_KEYCODE_LETTER('R'), .ctrl = true, .invoke = CommandSendToGDB, .cp = (void *) "gf-restart-gdb" } },
	{ .label = "Connect\tF4", { .code = UI_KEYCODE_FKEY(4), .invoke = CommandSendToGDB, .cp = (void *) "target remote :1234" } },
	{ .label = "Continue\tF5", { .code = UI_KEYCODE_FKEY(5), .invoke = CommandSendToGDB, .cp = (void *) "c" } },
	{ .label = "Step over\tF10", { .code = UI_KEYCODE_FKEY(10), .invoke = CommandSendToGDB, .cp = (void *) "gf-next" } },
	{ .label = "Step out of block\tShift+F10", { .code = UI_KEYCODE_FKEY(10), .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "gf-step-out-of-block" } },
	{ .label = "Step in\tF11", { .code = UI_KEYCODE_FKEY(11), .invoke = CommandSendToGDB, .cp = (void *) "gf-step" } },
	{ .label = "Step out\tShift+F11", { .code = UI_KEYCODE_FKEY(11), .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "finish" } },
	{ .label = "Pause\tF8", { .code = UI_KEYCODE_FKEY(8), .invoke = CommandPause } },
	{ .label = "Toggle breakpoint\tF9", { .code = UI_KEYCODE_FKEY(9), .invoke = CommandToggleBreakpoint } },
	{ .label = "Sync with gvim\tF2", { .code = UI_KEYCODE_FKEY(2), .invoke = CommandSyncWithGvim } },
	{ .label = "Ask GDB for PWD\tCtrl+Shift+P", { .code = UI_KEYCODE_LETTER('P'), .ctrl = true, .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "gf-get-pwd" } },
	{ .label = "Toggle disassembly\tCtrl+D", { .code = UI_KEYCODE_LETTER('D'), .ctrl = true, .invoke = CommandToggleDisassembly } },
	{ .label = "Add watch", { .invoke = CommandAddWatch } },
	{ .label = "Inspect line", { .code = XK_grave, .invoke = CommandInspectLine } },
	{ .label = nullptr, { .code = UI_KEYCODE_LETTER('E'), .ctrl = true, .invoke = CommandWatchAddEntryForAddress } },
	{ .label = nullptr, { .code = UI_KEYCODE_LETTER('G'), .ctrl = true, .invoke = CommandWatchViewSourceAtAddress } },
	{ .label = nullptr, { .code = UI_KEYCODE_LETTER('B'), .ctrl = true, .invoke = CommandToggleFillDataTab } },
	{ .label = "Donate", { .invoke = CommandDonate } },
};

InterfaceWindow interfaceWindows[] = {
	{ "Stack", StackWindowCreate, StackWindowUpdate, },
	{ "Source", SourceWindowCreate, SourceWindowUpdate, },
	{ "Breakpoints", BreakpointsWindowCreate, BreakpointsWindowUpdate, },
	{ "Registers", RegistersWindowCreate, RegistersWindowUpdate, },
	{ "Watch", WatchWindowCreate, WatchWindowUpdate, WatchWindowFocus, },
	{ "Commands", CommandsWindowCreate, nullptr, },
	{ "Data", DataWindowCreate, nullptr, },
	{ "Struct", StructWindowCreate, nullptr, },
	{ "Files", FilesWindowCreate, nullptr, },
	{ "Console", ConsoleWindowCreate, nullptr, },
	{ "Log", LogWindowCreate, nullptr, },
	{ "Thread", ThreadWindowCreate, ThreadWindowUpdate, },
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

				for (int i = 1; i <= 12; i++) {
					if (codeStart[0] == 'f' && isdigit(codeStart[1]) && atoi(codeStart + 1) == i) {
						shortcut.code = UI_KEYCODE_FKEY(i);
					}
				}

				if (!shortcut.code) {
					fprintf(stderr, "Warning: Could not register shortcut for '%s'.\n", state.key);
				} else {
					UIWindowRegisterShortcut(windowMain, shortcut);
				}
			} else if (0 == strcmp(state.section, "ui") && earlyPass) {
				if (0 == strcmp(state.key, "font_size")) {
					fontSize = atoi(state.value);
				} else if (0 == strcmp(state.key, "scale")) {
					uiScale = atof(state.value);
				} else if (0 == strcmp(state.key, "layout")) {
					layoutString = state.value;
				} else if (0 == strcmp(state.key, "maximize")) {
					maximize = atoi(state.value);
				}
			} else if (0 == strcmp(state.section, "gdb") && !earlyPass) {
				if (0 == strcmp(state.key, "argument")) {
					gdbArgc++;
					gdbArgv = (char **) realloc(gdbArgv, sizeof(char *) * (gdbArgc + 1));
					gdbArgv[gdbArgc - 1] = state.value;
					gdbArgv[gdbArgc] = nullptr;
				} else if (0 == strcmp(state.key, "path")) {
					gdbPath = state.value;
				} else if (0 == strcmp(state.key, "breakpoint_type")) {
					if (0 == strcmp(state.value, "software")) {
						useHardwareBreakpoints = false;
					} else if (0 == strcmp(state.value, "hardware")) {
						useHardwareBreakpoints = true;
					} else {
						useHardwareBreakpoints = false;
						fprintf(stderr, "Warning: Invalid breakpoint value '%s'; using software breakpoints.\n", state.value);
					}
				}
			} else if (0 == strcmp(state.section, "commands") && earlyPass && state.keyBytes && state.valueBytes) {
				presetCommands.Add(state);
			} else if (0 == strcmp(state.section, "trusted_folders") && earlyPass && state.keyBytes) {
				char path[PATH_MAX];
				getcwd(path, sizeof(path));
				if (0 == strcmp(path, state.key)) currentFolderIsTrusted = true;
			} else if (0 == strcmp(state.section, "theme") && !earlyPass && state.keyBytes && state.valueBytes) {
				for (uintptr_t i = 0; i < sizeof(themeItems) / sizeof(themeItems[0]); i++) {
					if (strcmp(state.key, themeItems[i])) continue;
					((uint32_t *) &ui.theme)[i] = strtoul(state.value, nullptr, 16);
				}
			} else if (0 == strcmp(state.section, "vim") && earlyPass && 0 == strcmp(state.key, "server_name")) {
				vimServerName = state.value;
			} else if (0 == strcmp(state.section, "pipe") && earlyPass && 0 == strcmp(state.key, "log")) {
				logPipePath = state.value;
				mkfifo(logPipePath, 6 + 6 * 8 + 6 * 64);
			} else if (0 == strcmp(state.section, "pipe") && earlyPass && 0 == strcmp(state.key, "control")) {
				controlPipePath = state.value;
				mkfifo(controlPipePath, 6 + 6 * 8 + 6 * 64);
				pthread_t thread;
				pthread_create(&thread, nullptr, ControlPipeThread, nullptr);
			}
		}
	}
}

void InterfaceShowMenu(void *self) {
	UIMenu *menu = UIMenuCreate((UIElement *) self, UI_MENU_PLACE_ABOVE | UI_MENU_NO_SCROLL);

	for (uintptr_t i = 0; i < sizeof(interfaceCommands) / sizeof(interfaceCommands[0]); i++) {
		if (!interfaceCommands[i].label) continue;
		UIMenuAddItem(menu, 0, interfaceCommands[i].label, -1, interfaceCommands[i].shortcut.invoke, interfaceCommands[i].shortcut.cp);
	}

	UIMenuShow(menu);
}

UIElement *InterfaceWindowSwitchToAndFocus(const char *name) {
	for (uintptr_t i = 0; i < sizeof(interfaceWindows) / sizeof(interfaceWindows[0]); i++) {
		InterfaceWindow *window = &interfaceWindows[i];
		if (!window->element) continue;
		if (strcmp(window->name, name)) continue;

		if ((window->element->flags & UI_ELEMENT_HIDE)
				&& window->element->parent->messageClass == _UITabPaneMessage) {
			UITabPane *tabPane = (UITabPane *) window->element->parent;
			tabPane->active = 0;
			UIElement *child = tabPane->e.children;
			while (child != window->element) child = child->next, tabPane->active++;
			UIElementRefresh(&tabPane->e);
		}

		if (window->focus) {
			window->focus(window->element);
		} else if (window->element->flags & UI_ELEMENT_TAB_STOP) {
			UIElementFocus(window->element);
		}

		return window->element;
	}

	UIDialogShow(windowMain, 0, "Couldn't find the window '%s'.\n%f%b", name, "OK");
	return nullptr;
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
			CommandParseInternal(input + 2, false);
		}

		free(input);
	} else if (message == MSG_RECEIVED_LOG) {
		LogReceived(dp);
	} else if (message == UI_MSG_WINDOW_ACTIVATE) {
		DisplaySetPosition(currentFileFull, currentLine, false);
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
	uintptr_t exactChildCount = 0, currentChildCount = 0;

	if (layoutString[0] == 'h' && layoutString[1] == '(') {
		layoutString += 2;
		container = &UISplitPaneCreate(parent, UI_ELEMENT_V_FILL | UI_ELEMENT_H_FILL, strtol(layoutString, &layoutString, 10) * 0.01f)->e;
		layoutString++;
		exactChildCount = 2;
	} else if (layoutString[0] == 'v' && layoutString[1] == '(') {
		layoutString += 2;
		container = &UISplitPaneCreate(parent, UI_SPLIT_PANE_VERTICAL | UI_ELEMENT_V_FILL | UI_ELEMENT_H_FILL, strtol(layoutString, &layoutString, 10) * 0.01f)->e;
		layoutString++;
		exactChildCount = 2;
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

		if (!found) {
			fprintf(stderr, "Error: Invalid layout string! The specified window was not found.\n");
			exit(1);
		}
	}

	while (container) {
		if (layoutString[0] == ',') {
			layoutString++;
		} else if (layoutString[0] == ')') {
			layoutString++;
			break;
		} else {
			InterfaceLayoutCreate(container);
			currentChildCount++;
		}
	}

	if (currentChildCount != exactChildCount && exactChildCount) {
		fprintf(stderr, "Error: Invalid layout string! Split panes, h(...) and v(...), must have exactly 2 children.\n");
		exit(1);
	}
}

int main(int argc, char **argv) {
	struct sigaction sigintHandler = {};
	sigintHandler.sa_handler = [] (int) { DebuggerClose(); exit(0); };
	sigaction(SIGINT, &sigintHandler, nullptr);

	gdbArgv = (char **) malloc(sizeof(char *) * (argc + 1));
	gdbArgv[0] = (char *) "gdb";
	memcpy(gdbArgv + 1, argv + 1, sizeof(argv) * argc);
	gdbArgc = argc;

	LoadSettings(true);
	UIInitialise();

	windowMain = UIWindowCreate(0, maximize ? UI_WINDOW_MAXIMIZE : 0, "gf2", 0, 0);
	windowMain->scale = uiScale;
	windowMain->e.messageUser = WindowMessage;

	for (uintptr_t i = 0; i < sizeof(interfaceCommands) / sizeof(interfaceCommands[0]); i++) {
		if (!interfaceCommands[i].shortcut.code) continue;
		UIWindowRegisterShortcut(windowMain, interfaceCommands[i].shortcut);
	}

	InterfaceLayoutCreate(&UIPanelCreate(&windowMain->e, UI_PANEL_EXPAND)->e);
	LoadSettings(false);
	pthread_cond_init(&evaluateEvent, nullptr);
	pthread_mutex_init(&evaluateMutex, nullptr);
	DebuggerStartThread();
	CommandSyncWithGvim(nullptr);
	UIMessageLoop();
	DebuggerClose();

	return 0;
}
