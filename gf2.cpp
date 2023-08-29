// TODO Disassembly window:
// 	- Setting/clearing/showing breakpoints.
// 	- Jump/run to line.
// 	- Shift+F10: run to next instruction (for skipping past loops).
// 	- Split source and disassembly view.

// TODO Inspect line mode:
// 	- Jump/run to selected line.
// 	- How to show overloaded variables correctly when moving lines?

// TODO More data visualization tools in the data window.

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
#include <time.h>

extern "C" {
#define UI_FONT_PATH
#define UI_LINUX
#define UI_IMPLEMENTATION
#include "luigi2.h"
}

#define MSG_RECEIVED_DATA ((UIMessage) (UI_MSG_USER + 1))
#define MSG_RECEIVED_CONTROL ((UIMessage) (UI_MSG_USER + 2))
#define MSG_RECEIVED_LOG ((UIMessage) (UI_MSG_USER + 3))

// Data structures:

template <class T>
struct Array {
	T *array;
	size_t length, allocated;

	void InsertMany(T *newItems, uintptr_t index, size_t newCount) {
		if (length + newCount > allocated) {
			allocated *= 2;
			if (length + newCount > allocated) allocated = length + newCount;
			array = (T *) realloc(array, allocated * sizeof(T));
		}

		length += newCount;
		memmove(array + index + newCount, array + index, (length - index - newCount) * sizeof(T));
		memcpy(array + index, newItems, newCount * sizeof(T));
	}

	void Delete(uintptr_t index, size_t count = 1) { 
		memmove(array + index, array + index + count, (length - index - count) * sizeof(T)); 
		length -= count;
	}

	void Insert(T item, uintptr_t index) { InsertMany(&item, index, 1); }
	void Add(T item) { Insert(item, length); }
	void Free() { free(array); array = nullptr; length = allocated = 0; }
	int Length() { return length; }
	T &First() { return array[0]; }
	T &Last() { return array[length - 1]; }
	T &operator[](uintptr_t index) { assert(index < length); return array[index]; }
	void Pop() { length--; }
	void DeleteSwap(uintptr_t index) { if (index != length - 1) array[index] = Last(); Pop(); }
};

template <class K, class V>
struct MapShort {
	struct { K key; V value; } *array;
	size_t used, capacity;

	uint64_t Hash(uint8_t *key, size_t keyBytes) {
		uint64_t hash = 0xCBF29CE484222325;
		for (uintptr_t i = 0; i < keyBytes; i++) hash = (hash ^ key[i]) * 0x100000001B3;
		return hash;
	}

	V *At(K key, bool createIfNeeded) {
		if (used + 1 > capacity / 2) {
			MapShort grow = {};
			grow.capacity = capacity ? (capacity + 1) * 2 - 1 : 15;
			*(void **) &grow.array = calloc(sizeof(array[0]), grow.capacity);
			for (uintptr_t i = 0; i < capacity; i++) if (array[i].key) grow.Put(array[i].key, array[i].value);
			free(array); *this = grow;
		}

		uintptr_t slot = Hash((uint8_t *) &key, sizeof(key)) % capacity;
		while (array[slot].key && array[slot].key != key) slot = (slot + 1) % capacity;

		if (!array[slot].key && createIfNeeded) {
			used++;
			array[slot].key = key;
		}

		return &array[slot].value;
	}

	bool Has(K key) {
		if (!capacity) return false;
		uintptr_t slot = Hash((uint8_t *) &key, sizeof(key)) % capacity;
		while (array[slot].key && array[slot].key != key) slot = (slot + 1) % capacity;
		return array[slot].key;
	}

	V Get(K key) { return *At(key, false); }
	void Put(K key, V value) { *At(key, true) = value; }
	void Free() { free(array); array = nullptr; used = capacity = 0; }
};

// General:

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
	bool queuedUpdate, alwaysUpdate;
};

struct InterfaceDataViewer {
	const char *addButtonLabel;
	void (*addButtonCallback)(void *_unused);
};

struct INIState {
	char *buffer, *section, *key, *value;
	size_t bytes, sectionBytes, keyBytes, valueBytes;
};

FILE *commandLog;
char emptyString;
bool programRunning = true;
const char *vimServerName = "GVIM";
const char *logPipePath;
const char *controlPipePath;
Array<INIState> presetCommands;
char globalConfigPath[PATH_MAX];
char localConfigDirectory[PATH_MAX];
char localConfigPath[PATH_MAX];
const char *executablePath;
const char *executableArguments;
bool executableAskDirectory = true;
Array<InterfaceWindow> interfaceWindows;
Array<InterfaceCommand> interfaceCommands;
Array<InterfaceDataViewer> interfaceDataViewers;
char *layoutString = (char *) "v(75,h(80,Source,v(50,t(Exe,Breakpoints,Commands,Struct),t(Stack,Files,Thread))),h(65,Console,t(Watch,Locals,Registers,Data)))";
const char *fontPath;
int fontSizeCode = 13;
int fontSizeInterface = 11;
float uiScale = 1;
bool restoreWatchWindow;
struct WatchWindow *firstWatchWindow;
bool maximize;
bool confirmCommandConnect = true, confirmCommandKill = true;
int backtraceCountLimit = 50;

// Current file and line:

char currentFile[PATH_MAX];
char currentFileFull[PATH_MAX];
int currentLine;
time_t currentFileReadTime;
bool showingDisassembly;
char previousLocation[256];

// User interface:

UIWindow *windowMain;
UISwitcher *switcherMain;

UICode *displayCode;
UICode *displayOutput;
UITextbox *textboxInput;
UISpacer *trafficLight;

UIMDIClient *dataWindow;
UIPanel *dataTab;

UIFont *fontCode;

// Breakpoints:

struct Breakpoint {
	char file[PATH_MAX];
	char fileFull[PATH_MAX];
	int line;
	int watchpoint;
	int hit;
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

def _gf_hook_string(basic_type):
    hook_string = str(basic_type)
    template_start = hook_string.find('<')
    if template_start != -1: hook_string = hook_string[0:template_start]
    return hook_string

def _gf_basic_type(value):
    basic_type = gdb.types.get_basic_type(value.type)
    if basic_type.code == gdb.TYPE_CODE_PTR:
        basic_type = gdb.types.get_basic_type(basic_type.target())
    return basic_type

def _gf_value(expression):
    try:
        value = gdb.parse_and_eval(expression[0])
        for index in expression[1:]:
            if isinstance(index, str) and index[0] == '[':
                value = gf_hooks[_gf_hook_string(_gf_basic_type(value))](value, index)
            else: value = value[index]
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
        except: break
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

def __gf_fields_recurse(type):
    if type.code == gdb.TYPE_CODE_STRUCT or type.code == gdb.TYPE_CODE_UNION:
        for field in gdb.types.deep_items(type):
            if field[1].is_base_class: __gf_fields_recurse(field[1].type)
            else: print(field[0])
    elif type.code == gdb.TYPE_CODE_ARRAY:
        print('(array) %d' % (type.range()[1]+1))

def _gf_fields_recurse(value):
    basic_type = _gf_basic_type(value)
    __gf_fields_recurse(basic_type)

def gf_fields(expression):
    value = _gf_value(expression)
    if value == None: return
    basic_type = _gf_basic_type(value)
    hook_string = _gf_hook_string(basic_type)
    try: gf_hooks[hook_string](value, None)
    except: __gf_fields_recurse(basic_type)

def gf_locals():
    try:
        frame = gdb.selected_frame()
        block = frame.block()
    except:
        return
    names = set()
    while block and not (block.is_global or block.is_static):
        for symbol in block:
            if (symbol.is_argument or symbol.is_variable or symbol.is_constant):
                names.add(symbol.name)
        block = block.superblock
    for name in names:
        print(name)

end
)";

// Forward declarations:

bool DisplaySetPosition(const char *file, int line, bool useGDBToGetFullPath);
void InterfaceShowMenu(void *self);
UIElement *InterfaceWindowSwitchToAndFocus(const char *name);
void WatchAddExpression2(char *string);
int WatchWindowMessage(UIElement *element, UIMessage message, int di, void *dp);
void CommandInspectLine(void *);

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

bool SourceFindOuterFunctionCall(char **start, char **end) {
	if (!currentLine || currentLine - 1 >= displayCode->lineCount) return false;
	uintptr_t offset = displayCode->lines[currentLine - 1].offset;
	bool found = false;

	// Look forwards for the end of the call ");".

	while (offset < displayCode->contentBytes - 1) {
		if (displayCode->content[offset] == ')' && displayCode->content[offset + 1] == ';') {
			found = true;
			break;
		} else if (displayCode->content[offset] == ';' || displayCode->content[offset] == '{') {
			break;
		}

		offset++;
	}

	if (!found) return false;

	// Look backwards for the matching bracket.

	int level = 0;

	while (offset > 0) {
		if (displayCode->content[offset] == ')') {
			level++;
		} else if (displayCode->content[offset] == '(') {
			level--;
			if (level == 0) break;
		}

		offset--;
	}

	if (level) return false;

	*start = *end = displayCode->content + offset;
	found = false;
	offset--;

	// Look backwards for the start of the function name.
	// TODO Support function pointers.
	
	while (offset > 0) {
		char c = displayCode->content[offset];

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ' ' || (c >= '0' && c <= '9')) {
			// Part of the function name.
			offset--;
		} else {
			*start = displayCode->content + offset + 1;
			found = true;
			break;
		}
	}

	return found;
}

//////////////////////////////////////////////////////
// Debugger interaction:
//////////////////////////////////////////////////////

volatile int pipeToGDB;
volatile pid_t gdbPID;
volatile pthread_t gdbThread;
pthread_cond_t evaluateEvent;
pthread_mutex_t evaluateMutex;
char *evaluateResult;
bool evaluateMode;
char **gdbArgv;
int gdbArgc;

#if defined(__OpenBSD__)
const char *gdbPath = "egdb";
#else
const char *gdbPath = "gdb";
#endif

bool firstUpdate = true;
void *sendAllGDBOutputToLogWindowContext;

void *DebuggerThread(void *) {
	int outputPipe[2], inputPipe[2];
	pipe(outputPipe);
	pipe(inputPipe);

#if defined(__FreeBSD__) || defined(__OpenBSD__)
	gdbPID = fork();
	
	if(gdbPID == 0) {
		setsid();
		dup2(inputPipe[0],  0);
		dup2(outputPipe[1], 1);
		dup2(outputPipe[1], 2);
		execvp(gdbPath, gdbArgv);
		fprintf(stderr, "Error: Couldn't execute gdb.\n");
		exit(EXIT_FAILURE);
	} else if (gdbPID < 0) {
		fprintf(stderr, "Error: Couldn't fork.\n");
		exit(EXIT_FAILURE);
	}
#else
	posix_spawn_file_actions_t actions = {};
	posix_spawn_file_actions_adddup2(&actions, inputPipe[0],  0);
	posix_spawn_file_actions_adddup2(&actions, outputPipe[1], 1);
	posix_spawn_file_actions_adddup2(&actions, outputPipe[1], 2);

	posix_spawnattr_t attrs = {};
	posix_spawnattr_init(&attrs);
	posix_spawnattr_setflags(&attrs, POSIX_SPAWN_SETSID);

	posix_spawnp((pid_t *) &gdbPID, gdbPath, &actions, &attrs, gdbArgv, environ);
#endif

	pipeToGDB = inputPipe[1];

	const char *setPrompt = "set prompt (gdb) \n";
	write(pipeToGDB, setPrompt, strlen(setPrompt));

	char *catBuffer = NULL;
	size_t catBufferUsed = 0;
	size_t catBufferAllocated = 0;

	while (true) {
		char buffer[512 + 1];
		int count = read(outputPipe[0], buffer, 512);
		buffer[count] = 0;
		if (!count) break;

		if (sendAllGDBOutputToLogWindowContext && !evaluateMode) {
			void *message = malloc(count + sizeof(sendAllGDBOutputToLogWindowContext) + 1);
			memcpy(message, &sendAllGDBOutputToLogWindowContext, sizeof(sendAllGDBOutputToLogWindowContext));
			strcpy((char *) message + sizeof(sendAllGDBOutputToLogWindowContext), buffer);
			UIWindowPostMessage(windowMain, MSG_RECEIVED_LOG, message);
		}

		size_t neededSpace = catBufferUsed + count + 1;

		if (neededSpace > catBufferAllocated) {
			catBufferAllocated *= 2;
			if (catBufferAllocated < neededSpace) catBufferAllocated = neededSpace;
			catBuffer = (char *) realloc(catBuffer, catBufferAllocated);
		}

		strcpy(catBuffer + catBufferUsed, buffer);
		catBufferUsed += count;
		if (!strstr(catBuffer, "(gdb) ")) continue;

		// printf("got (%d) {%s}\n", evaluateMode, copy);

		// Notify the main thread we have data.

		if (evaluateMode) {
			free(evaluateResult);
			evaluateResult = catBuffer;
			evaluateMode = false;
			pthread_mutex_lock(&evaluateMutex);
			pthread_cond_signal(&evaluateEvent);
			pthread_mutex_unlock(&evaluateMutex);
		} else {
			UIWindowPostMessage(windowMain, MSG_RECEIVED_DATA, catBuffer);
		}

		catBuffer = NULL;
		catBufferUsed = 0;
		catBufferAllocated = 0;
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
	char buffer[16];
	StringFormat(buffer, sizeof(buffer), "bt %d", backtraceCountLimit);
	EvaluateCommand(buffer);
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

		const char *hitCountNeedle = "breakpoint already hit";
		const char *hitCount = strstr(position, hitCountNeedle);
		if (hitCount) hitCount += strlen(hitCountNeedle);

		if (hitCount && hitCount < next) {
			breakpoint.hit = atoi(hitCount);
		}

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

bool CommandParseInternal(const char *command, bool synchronous) {
	if (0 == strcmp(command, "gf-step")) {
		if (!programRunning) DebuggerSend(showingDisassembly ? "stepi" : "s", true, synchronous);
		return true;
	} else if (0 == strcmp(command, "gf-next")) {
		if (!programRunning) DebuggerSend(showingDisassembly ? "nexti" : "n", true, synchronous);
		return true;
	} else if (0 == strcmp(command, "gf-step-out-of-block")) {
		int line = SourceFindEndOfBlock();

		if (line != -1) {
			char buffer[256];
			StringFormat(buffer, sizeof(buffer), "until %d", line);
			DebuggerSend(buffer, true, synchronous);
			return false;
		}
	} else if (0 == strcmp(command, "gf-step-into-outer")) {
		char *start, *end;
		bool found = SourceFindOuterFunctionCall(&start, &end);

		if (found) {
			char buffer[256];
			StringFormat(buffer, sizeof(buffer), "advance %.*s", (int) (end - start), start);
			DebuggerSend(buffer, true, synchronous);
			return true;
		} else {
			return CommandParseInternal("gf-step", synchronous);
		}
	} else if (0 == strcmp(command, "gf-restart-gdb")) {
		firstUpdate = true;
		kill(gdbPID, SIGKILL);
		pthread_cancel(gdbThread); // TODO Is there a nicer way to do this?
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

		UIDialogShow(windowMain, 0, "Couldn't get the working directory.\n%f%B", "OK");
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
	} else if (0 == strcmp(command, "gf-inspect-line")) {
		CommandInspectLine(nullptr);
	} else if (0 == strcmp(command, "target remote :1234") && confirmCommandConnect
			&& 0 == strcmp("Cancel", UIDialogShow(windowMain, 0, "Connect to remote target?\n%f%B%C", "Connect", "Cancel"))) {
	} else if (0 == strcmp(command, "kill") && confirmCommandKill
			&& 0 == strcmp("Cancel", UIDialogShow(windowMain, 0, "Kill debugging target?\n%f%B%C", "Kill", "Cancel"))) {
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
	char *line = nameEnd + 1;
	while (!isdigit(*line) && *line) line++;
	if (!line) return;
	int lineNumber = atoi(line);
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
	StringFormat(buffer, 1024, "b %s:%d", currentFile, line);
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
// Settings:
//////////////////////////////////////////////////////

const char *themeItems[] = {
	"panel1", "panel2", "selected", "border", "text", "textDisabled", "textSelected",
	"buttonNormal", "buttonHovered", "buttonPressed", "buttonDisabled", "textboxNormal", "textboxFocused",
	"codeFocused", "codeBackground", "codeDefault", "codeComment", "codeString", "codeNumber", "codeOperator", "codePreprocessor",
};

void SettingsAddTrustedFolder() {
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
		fwrite(localConfigDirectory, 1, strlen(localConfigDirectory), f);
		char newline = '\n';
		fwrite(&newline, 1, 1, f);
		if (length - insert) fwrite(config + insert, 1, length - insert, f);
		fclose(f);
	}
}

void SettingsLoad(bool earlyPass) {
	bool currentFolderIsTrusted = false;
	static bool cwdConfigNotTrusted = false;

	for (int i = 0; i < 2; i++) {
		INIState state = { .buffer = LoadFile(i ? localConfigPath : globalConfigPath, &state.bytes) };

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
				SettingsAddTrustedFolder();
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
				if (0 == strcmp(state.key, "font_path")) {
					fontPath = state.value;
				} else if (0 == strcmp(state.key, "font_size")) {
					fontSizeInterface = fontSizeCode = atoi(state.value);
				} else if (0 == strcmp(state.key, "font_size_code")) {
					fontSizeCode = atoi(state.value);
				} else if (0 == strcmp(state.key, "font_size_interface")) {
					fontSizeInterface = atoi(state.value);
				} else if (0 == strcmp(state.key, "scale")) {
					uiScale = atof(state.value);
				} else if (0 == strcmp(state.key, "layout")) {
					layoutString = state.value;
				} else if (0 == strcmp(state.key, "maximize")) {
					maximize = atoi(state.value);
				} else if (0 == strcmp(state.key, "restore_watch_window")) {
					restoreWatchWindow = atoi(state.value);
				}
			} else if (0 == strcmp(state.section, "gdb") && !earlyPass) {
				if (0 == strcmp(state.key, "argument")) {
					gdbArgc++;
					gdbArgv = (char **) realloc(gdbArgv, sizeof(char *) * (gdbArgc + 1));
					gdbArgv[gdbArgc - 1] = state.value;
					gdbArgv[gdbArgc] = nullptr;
				} else if (0 == strcmp(state.key, "path")) {
					gdbPath = state.value;
				} else if (0 == strcmp(state.key, "log_all_output") && atoi(state.value)) {
					for (int i = 0; i < interfaceWindows.Length(); i++) {
						InterfaceWindow *window = &interfaceWindows[i];

						if (0 == strcmp(window->name, "Log")) {
							sendAllGDBOutputToLogWindowContext = window->element;
						}
					}

					if (!sendAllGDBOutputToLogWindowContext) {
						fprintf(stderr, "Warning: gdb.log_all_output was enabled, "
								"but your layout does not have a 'Log' window.\n");
					}
				} else if (0 == strcmp(state.key, "confirm_command_kill")) {
					confirmCommandKill = atoi(state.value);
				} else if (0 == strcmp(state.key, "confirm_command_connect")) {
					confirmCommandConnect = atoi(state.value);
				} else if (0 == strcmp(state.key, "backtrace_count_limit")) {
					backtraceCountLimit = atoi(state.value);
				}
			} else if (0 == strcmp(state.section, "commands") && earlyPass && state.keyBytes && state.valueBytes) {
				presetCommands.Add(state);
			} else if (0 == strcmp(state.section, "trusted_folders") && earlyPass && state.keyBytes) {
				if (0 == strcmp(localConfigDirectory, state.key)) currentFolderIsTrusted = true;
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
			} else if (0 == strcmp(state.section, "executable") && earlyPass) {
				if (0 == strcmp(state.key, "path")) {
					executablePath = state.value;
				} else if (0 == strcmp(state.key, "arguments")) {
					executableArguments = state.value;
				} else if (0 == strcmp(state.key, "ask_directory")) {
					executableAskDirectory = atoi(state.value);
				}
			}
		}
	}
}

//////////////////////////////////////////////////////
// Debug windows:
//////////////////////////////////////////////////////

#include "windows.cpp"

#if __has_include("extensions.cpp")
#include "extensions.cpp"
#endif

#if __has_include("plugins.cpp")
#include "plugins.cpp"
#endif

//////////////////////////////////////////////////////
// Interface and main:
//////////////////////////////////////////////////////

__attribute__((constructor)) 
void InterfaceAddBuiltinWindowsAndCommands() {
	interfaceWindows.Add({ "Stack", StackWindowCreate, StackWindowUpdate });
	interfaceWindows.Add({ "Source", SourceWindowCreate, SourceWindowUpdate });
	interfaceWindows.Add({ "Breakpoints", BreakpointsWindowCreate, BreakpointsWindowUpdate });
	interfaceWindows.Add({ "Registers", RegistersWindowCreate, RegistersWindowUpdate });
	interfaceWindows.Add({ "Watch", WatchWindowCreate, WatchWindowUpdate, WatchWindowFocus });
	interfaceWindows.Add({ "Locals", LocalsWindowCreate, WatchWindowUpdate, WatchWindowFocus });
	interfaceWindows.Add({ "Commands", CommandsWindowCreate, nullptr });
	interfaceWindows.Add({ "Data", DataWindowCreate, nullptr });
	interfaceWindows.Add({ "Struct", StructWindowCreate, nullptr });
	interfaceWindows.Add({ "Files", FilesWindowCreate, nullptr });
	interfaceWindows.Add({ "Console", ConsoleWindowCreate, nullptr });
	interfaceWindows.Add({ "Log", LogWindowCreate, nullptr });
	interfaceWindows.Add({ "Thread", ThreadWindowCreate, ThreadWindowUpdate });
	interfaceWindows.Add({ "Exe", ExecutableWindowCreate, nullptr });

	interfaceDataViewers.Add({ "Add bitmap...", BitmapAddDialog });

	interfaceCommands.Add({ .label = "Run\tShift+F5", 
			{ .code = UI_KEYCODE_FKEY(5), .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "r" } });
	interfaceCommands.Add({ .label = "Run paused\tCtrl+F5", 
			{ .code = UI_KEYCODE_FKEY(5), .ctrl = true, .invoke = CommandSendToGDB, .cp = (void *) "start" } });
	interfaceCommands.Add({ .label = "Kill\tF3", 
			{ .code = UI_KEYCODE_FKEY(3), .invoke = CommandSendToGDB, .cp = (void *) "kill" } });
	interfaceCommands.Add({ .label = "Restart GDB\tCtrl+R", 
			{ .code = UI_KEYCODE_LETTER('R'), .ctrl = true, .invoke = CommandSendToGDB, .cp = (void *) "gf-restart-gdb" } });
	interfaceCommands.Add({ .label = "Connect\tF4", 
			{ .code = UI_KEYCODE_FKEY(4), .invoke = CommandSendToGDB, .cp = (void *) "target remote :1234" } });
	interfaceCommands.Add({ .label = "Continue\tF5", 
			{ .code = UI_KEYCODE_FKEY(5), .invoke = CommandSendToGDB, .cp = (void *) "c" } });
	interfaceCommands.Add({ .label = "Step over\tF10", 
			{ .code = UI_KEYCODE_FKEY(10), .invoke = CommandSendToGDB, .cp = (void *) "gf-next" } });
	interfaceCommands.Add({ .label = "Step out of block\tShift+F10", 
			{ .code = UI_KEYCODE_FKEY(10), .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "gf-step-out-of-block" } });
	interfaceCommands.Add({ .label = "Step in\tF11", 
			{ .code = UI_KEYCODE_FKEY(11), .invoke = CommandSendToGDB, .cp = (void *) "gf-step" } });
	interfaceCommands.Add({ .label = "Step into outer\tShift+F8", 
			{ .code = UI_KEYCODE_FKEY(8), .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "gf-step-into-outer" } });
	interfaceCommands.Add({ .label = "Step out\tShift+F11", 
			{ .code = UI_KEYCODE_FKEY(11), .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "finish" } });
	interfaceCommands.Add({ .label = "Pause\tF8", 
			{ .code = UI_KEYCODE_FKEY(8), .invoke = CommandPause } });
	interfaceCommands.Add({ .label = "Toggle breakpoint\tF9", 
			{ .code = UI_KEYCODE_FKEY(9), .invoke = CommandToggleBreakpoint } });
	interfaceCommands.Add({ .label = "Sync with gvim\tF2", 
			{ .code = UI_KEYCODE_FKEY(2), .invoke = CommandSyncWithGvim } });
	interfaceCommands.Add({ .label = "Ask GDB for PWD\tCtrl+Shift+P", 
			{ .code = UI_KEYCODE_LETTER('P'), .ctrl = true, .shift = true, .invoke = CommandSendToGDB, .cp = (void *) "gf-get-pwd" } });
	interfaceCommands.Add({ .label = "Toggle disassembly\tCtrl+D", 
			{ .code = UI_KEYCODE_LETTER('D'), .ctrl = true, .invoke = CommandToggleDisassembly } });
	interfaceCommands.Add({ .label = "Set disassembly mode\tCtrl+M", 
			{ .code = UI_KEYCODE_LETTER('M'), .ctrl = true, .invoke = CommandSetDisassemblyMode } });
	interfaceCommands.Add({ .label = "Add watch", 
			{ .invoke = CommandAddWatch } });
	interfaceCommands.Add({ .label = "Inspect line", 
			{ .code = XK_grave, .invoke = CommandInspectLine } });
	interfaceCommands.Add({ .label = nullptr,
			{ .code = UI_KEYCODE_LETTER('E'), .ctrl = true, .invoke = CommandWatchAddEntryForAddress } });
	interfaceCommands.Add({ .label = nullptr, 
			{ .code = UI_KEYCODE_LETTER('G'), .ctrl = true, .invoke = CommandWatchViewSourceAtAddress } });
	interfaceCommands.Add({ .label = nullptr, 
			{ .code = UI_KEYCODE_LETTER('B'), .ctrl = true, .invoke = CommandToggleFillDataTab } });
	interfaceCommands.Add({ .label = "Donate", 
			{ .invoke = CommandDonate } });
}

void InterfaceShowMenu(void *self) {
	UIMenu *menu = UIMenuCreate((UIElement *) self, UI_MENU_PLACE_ABOVE | UI_MENU_NO_SCROLL);

	for (int i = 0; i < interfaceCommands.Length(); i++) {
		if (!interfaceCommands[i].label) continue;
		UIMenuAddItem(menu, 0, interfaceCommands[i].label, -1, interfaceCommands[i].shortcut.invoke, interfaceCommands[i].shortcut.cp);
	}

	UIMenuShow(menu);
}

UIElement *InterfaceWindowSwitchToAndFocus(const char *name) {
	for (int i = 0; i < interfaceWindows.Length(); i++) {
		InterfaceWindow *window = &interfaceWindows[i];
		if (!window->element) continue;
		if (strcmp(window->name, name)) continue;

		if ((window->element->flags & UI_ELEMENT_HIDE)
				&& window->element->parent->messageClass == _UITabPaneMessage) {
			UITabPane *tabPane = (UITabPane *) window->element->parent;

			for (uint32_t i = 0; i < tabPane->e.childCount; i++) {
				if (tabPane->e.children[i] == window->element) {
					tabPane->active = i;
					break;
				}
			}

			UIElementRefresh(&tabPane->e);
		}

		if (window->focus) {
			window->focus(window->element);
		} else if (window->element->flags & UI_ELEMENT_TAB_STOP) {
			UIElementFocus(window->element);
		}

		return window->element;
	}

	UIDialogShow(windowMain, 0, "Couldn't find the window '%s'.\n%f%B", name, "OK");
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

		if (firstUpdate) {
			EvaluateCommand(pythonCode);

			char path[PATH_MAX];
			StringFormat(path, sizeof(path), "%s/.config/gf2_watch.txt", getenv("HOME"));
			char *data = LoadFile(path, NULL);

			while (data && restoreWatchWindow) {
				char *end = strchr(data, '\n');
				if (!end) break;
				*end = 0;
				WatchAddExpression2(data);
				data = end + 1;
			}

			firstUpdate = false;
		}

		if (WatchLoggerUpdate(input)) goto skip;
		if (showingDisassembly) DisassemblyUpdateLine();

		DebuggerGetStack();
		DebuggerGetBreakpoints();

		for (int i = 0; i < interfaceWindows.Length(); i++) {
			InterfaceWindow *window = &interfaceWindows[i];
			if (!window->update || !window->element) continue;
			if (!window->alwaysUpdate && ElementHidden(window->element)) window->queuedUpdate = true;
			else window->update(input, window->element);
		}

		DataViewersUpdateAll();

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

		for (int i = 0; i < interfaceWindows.Length(); i++) {
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

const char *InterfaceLayoutNextToken(const char *expected = nullptr) {
	static char buffer[32];
	char *out = buffer;

	while (isspace(*layoutString)) {
		layoutString++;
	}

	char first = *layoutString;

	if (first == 0) {
		*out = 0;
	} else if (first == ',' || first == '(' || first == ')') {
		out[0] = first;
		out[1] = 0;
		layoutString++;
	} else if (isalnum(first)) {
		for (uintptr_t i = 0; i < sizeof(buffer) - 1; i++) {
			if (isalnum(*layoutString)) {
				*out++ = *layoutString++;
			} else {
				break;
			}
		}

		*out = 0;
	} else {
		fprintf(stderr, "Error: Invalid character in layout string '%c'.\n", first);
		exit(1);
	}

	if (expected) {
		if (*expected == '#') {
			bool valid = true;

			for (int i = 0; buffer[i]; i++) {
				if (!isdigit(buffer[i])) valid = false;
			}

			if (!valid) {
				fprintf(stderr, "Error: Expected a number in layout string; got '%s'.\n", buffer);
				exit(1);
			}
		} else if (strcmp(expected, buffer)) {
			fprintf(stderr, "Error: Expected '%s' in layout string; got '%s'.\n", expected, buffer);
			exit(1);
		}
	}

	return buffer;
}

void InterfaceLayoutCreate(UIElement *parent) {
	const char *token = InterfaceLayoutNextToken();

	if (0 == strcmp("h", token) || 0 == strcmp("v", token)) {
		uint32_t flags = UI_ELEMENT_V_FILL | UI_ELEMENT_H_FILL;
		if (*token == 'v') flags |= UI_SPLIT_PANE_VERTICAL;
		InterfaceLayoutNextToken("(");
		UIElement *container = &UISplitPaneCreate(parent, flags, atoi(InterfaceLayoutNextToken("#")) * 0.01f)->e;
		InterfaceLayoutNextToken(",");
		InterfaceLayoutCreate(container);
		InterfaceLayoutNextToken(",");
		InterfaceLayoutCreate(container);
		InterfaceLayoutNextToken(")");
	} else if (0 == strcmp("t", token)) {
		InterfaceLayoutNextToken("(");
		char *copy = strdup(layoutString);
		for (uintptr_t i = 0; copy[i]; i++) if (copy[i] == ',') copy[i] = '\t'; else if (copy[i] == ')') copy[i] = 0;
		UIElement *container = &UITabPaneCreate(parent, UI_ELEMENT_V_FILL | UI_ELEMENT_H_FILL, copy)->e;
		container->messageUser = InterfaceTabPaneMessage;
		free(copy);
		InterfaceLayoutCreate(container);

		while (true) {
			token = InterfaceLayoutNextToken();

			if (0 == strcmp(token, ",")) {
				InterfaceLayoutCreate(container);
			} else if (0 == strcmp(token, ")")) {
				break;
			} else {
				fprintf(stderr, "Error: Invalid layout string! Expected ',' or ')' in tab container list; got '%s'.\n", token);
				exit(1);
			}
		}
	} else {
		bool found = false;

		for (int i = 0; i < interfaceWindows.Length(); i++) {
			InterfaceWindow *w = &interfaceWindows[i];

			if (0 == strcmp(token, w->name)) { 
				w->element = w->create(parent);
				found = true;
				break;
			}
		}

		if (!found) {
			fprintf(stderr, "Error: Invalid layout string! The window '%s' was not found.\n", token);
			exit(1);
		}
	}
}

int main(int argc, char **argv) {
	if (argc == 2 && (0 == strcmp(argv[1], "-?") || 0 == strcmp(argv[1], "-h") || 0 == strcmp(argv[1], "--help"))) {
		fprintf(stderr, "Usage: %s [GDB ARGS]\n\n"
			        "GDB ARGS: Pass any gdb arguments here, they will be forwarded to gdb.\n\nFor more information, view the README.md (https://github.com/nakst/gf/blob/master/README.md).\n", argv[0]);
		return 0;
	}

	struct sigaction sigintHandler = {};
	sigintHandler.sa_handler = [] (int) { DebuggerClose(); exit(0); };
	sigaction(SIGINT, &sigintHandler, nullptr);

	gdbArgv = (char **) malloc(sizeof(char *) * (argc + 1));
	gdbArgv[0] = (char *) "gdb";
	memcpy(gdbArgv + 1, argv + 1, sizeof(argv) * argc);
	gdbArgc = argc;

	getcwd(localConfigDirectory, sizeof(localConfigDirectory));
	StringFormat(globalConfigPath, sizeof(globalConfigPath), "%s/.config/gf2_config.ini", getenv("HOME"));
	StringFormat(localConfigPath, sizeof(localConfigPath), "%s/.project.gf", localConfigDirectory);

	SettingsLoad(true);
	UIInitialise();
	ui.theme = uiThemeDark;

#ifdef UI_FREETYPE
	if (!fontPath) {
		// Ask fontconfig for a monospaced font. If this fails, the fallback font will be used.
		FILE *f = popen("fc-list | grep `fc-match mono | awk '{ print($1) }'` "
				"| awk 'BEGIN { FS = \":\" } ; { print($1) }'", "r");

		if (f) {
			char *buffer = (char *) malloc(PATH_MAX + 1);
			buffer[fread(buffer, 1, PATH_MAX, f)] = 0;
			pclose(f);
			char *newline = strchr(buffer, '\n');
			if (newline) *newline = 0;
			fontPath = buffer;
		}
	}
#endif

	fontCode = UIFontCreate(fontPath, fontSizeCode);
	UIFontActivate(UIFontCreate(fontPath, fontSizeInterface));

	windowMain = UIWindowCreate(0, maximize ? UI_WINDOW_MAXIMIZE : 0, "gf2", 0, 0);
	windowMain->scale = uiScale;
	windowMain->e.messageUser = WindowMessage;

	for (int i = 0; i < interfaceCommands.Length(); i++) {
		if (!interfaceCommands[i].shortcut.code) continue;
		UIWindowRegisterShortcut(windowMain, interfaceCommands[i].shortcut);
	}

	switcherMain = UISwitcherCreate(&windowMain->e, 0);
	InterfaceLayoutCreate(&UIPanelCreate(&switcherMain->e, UI_PANEL_EXPAND)->e);
	UISwitcherSwitchTo(switcherMain, switcherMain->e.children[0]);

	if (*InterfaceLayoutNextToken()) {
		fprintf(stderr, "Warning: Layout string has additional text after the end of the top-level entry.\n");
	}

	SettingsLoad(false);
	pthread_cond_init(&evaluateEvent, nullptr);
	pthread_mutex_init(&evaluateMutex, nullptr);
	DebuggerStartThread();
	CommandSyncWithGvim(nullptr);
	UIMessageLoop();
	DebuggerClose();

	if (restoreWatchWindow && firstWatchWindow) {
		StringFormat(globalConfigPath, sizeof(globalConfigPath), "%s/.config/gf2_watch.txt", getenv("HOME"));
		FILE *f = fopen(globalConfigPath, "wb");

		if (f) {
			for (int i = 0; i < firstWatchWindow->baseExpressions.Length(); i++) {
				fprintf(f, "%s\n", firstWatchWindow->baseExpressions[i]->key);
			}
		} else {
			fprintf(stderr, "Warning: Could not save the contents of the watch window; '%s' was not accessible.\n", globalConfigPath);
		}
	}

	return 0;
}
