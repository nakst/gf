// Fasm Debugger with Gdb and wxWidgets.
// Compile with "g++ -o fdgw fdgw.cpp `wx-config --cxxflags --libs all`"

#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <wx/stc/stc.h>
#include <wx/splitter.h>
#include <wx/listctrl.h>
#include <wx/dataview.h>
#include <wx/thread.h>
#include <wx/notebook.h>
#include <spawn.h>
#define MT_IMPLEMENTATION
#define MT_BUFFERED
#define MT_ASSERT assert
#include "mt.h"
#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

#define RECEIVE_BUFFER_SIZE (4194304)
char receiveBuffer[RECEIVE_BUFFER_SIZE]; // TODO Check this isn't exceeded.
bool newReceive = true;
wxMutex evaluateMutex;
wxCondition evaluateEvent(evaluateMutex);
volatile bool modeEvaluate;
volatile int pipeToGDB;
volatile pid_t gdbPID;
volatile pthread_t gdbThread;

struct PreviousMemoryAddress {
	uint64_t address;
	bool viewingStack;
	char structName[128];
	int memoryWordSize;
};

PreviousMemoryAddress *previousMemoryAddresses;

#define MEMORY_VIEW_BYTES (256)
#define MEMORY_VIEW_PER_ROW (16)
uint8_t oldMemoryView[MEMORY_VIEW_BYTES];
uint64_t memoryAddress = 0x400000;
bool viewingStack;
int memoryWordSize = 8;

bool initialisedGDB;

struct StructField {
	int offset, nameLength, bytes;
	const char *name;
};

StructField *structFields;
char structName[128]; 

class MyApp : public wxApp {
	public:
		virtual bool OnInit();
		virtual int OnExit();
};

struct FASSymbol {
	uint64_t value;

#define FAS_SYMBOL_DEFINED 			(1 <<  0)
#define FAS_SYMBOL_ASSEMBLY_TIME 		(1 <<  1)
#define FAS_SYMBOL_CANNOT_FORWARD_REFERENCE 	(1 <<  2)
#define FAS_SYMBOL_USED 			(1 <<  3)
#define FAS_SYMBOL_DID_PREDICT_USAGE 		(1 <<  4)
#define FAS_SYMBOL_PREDICTED_USAGE		(1 <<  5)
#define FAS_SYMBOL_DID_PREDICT_DEFINED 		(1 <<  6)
#define FAS_SYMBOL_PREDICTED_DEFINED		(1 <<  7)
#define FAS_SYMBOL_OPTIMISED			(1 <<  8)
#define FAS_SYMBOL_NEGATIVE			(1 <<  9)
#define FAS_SYMBOL_SPECIAL_MARKER		(1 << 10)
	uint16_t flags;

	uint8_t size;

#define FAS_SYMBOL_ABSOLUTE 			(0)
#define FAS_SYMBOL_RELOCATABLE_SEGMENT 		(1)
#define FAS_SYMBOL_RELOCATABLE_32 		(2)
#define FAS_SYMBOL_RELOCATABLE_32_RELATIVE 	(3)
#define FAS_SYMBOL_RELOCATABLE_64 		(4)
#define FAS_SYMBOL_RELOCATABLE_32_RELATIVE_GOT 	(5)
#define FAS_SYMBOL_RELOCATABLE_32_PLT 		(6)
#define FAS_SYMBOL_RELOCATABLE_32_RELATIVE_PLT 	(7)
	uint8_t type;

	uint32_t esib;

	uint16_t lastDefinitionPass, lastUsedPass;

#define FAS_RELOCATION_RELATIVE_TO_SECTION	(0)
#define FAS_RELOCATION_RELATIVE_TO_SYMBOL	(1)
	uint32_t relocationData : 31, relocationType : 1;

#define FAS_NAME_IN_PREPROCESSED_SOURCE		(0)
#define FAS_NAME_IN_STRING_TABLE		(1)
	uint32_t nameData : 31, nameType : 1;

	uint32_t preprocessedLineOffset;
};

struct FASPreprocessedLineHeader {
	uint32_t generatorNameOffset;

#define FAS_LINE_SOURCE_NORMAL	(0)
#define FAS_LINE_SOURCE_MACRO	(1)
	uint32_t lineNumber : 31, lineSource : 1;

	union {
		uint32_t positionOfLineInSource;
		uint32_t macroInvokerLineOffset;
	};

	uint32_t macroLineOffset;
};

#define FAS_TOKEN_IDENTIFIER	(0x1A)
#define FAS_TOKEN_IDENTIFIER_2	(0x3B)
#define FAS_TOKEN_STRING	(0x22)

struct FASAssemblyRow {
	uint32_t outputOffset;
	uint32_t preprocessedLineOffset;
	uint32_t addressLow, addressMedium;
	uint32_t esib;
	uint32_t relocationData : 31, relocationType : 1;
	uint8_t  type;
	uint8_t  codeBits;

#define FAS_ASSEMBLY_IN_VIRTUAL_BLOCK 	(1 << 0)
#define FAS_ASSEMBLY_NOT_IN_OUTPUT	(1 << 1)
	uint8_t flags;

	uint8_t addressHigh;
};

struct FASSectionName {
	uint32_t stringOffset;
};

struct FASSymbolReference {
	uint32_t symbolOffset;
	uint32_t assemblyOffset;
};

struct FASHeader {
#define FAS_SIGNATURE (0x1A736166)
	uint32_t signature;
	uint8_t  majorVersion, minorVersion;
	uint16_t headerLength;
	uint32_t inputFileStringOffset, outputFileStringOffset;
	uint32_t stringTableOffset, stringTableLength;
	uint32_t symbolTableOffset, symbolTableLength;
	uint32_t preprocessedSourceOffset, preprocessedSourceLength;
	uint32_t assemblyOffset, assemblyLength;
	uint32_t sectionNamesOffset, sectionNamesLength;
	uint32_t symbolReferencesOffset, symbolReferencesLength;
};

struct PreprocessedLine {
	const char *name;
	size_t nameLength;
	uint32_t lineNumber;
	uint32_t macroLine;
	bool fromMacro;
};

class MyTextCtrl : public wxTextCtrl {
	public:
		MyTextCtrl(wxWindow *parent, wxWindowID id);

	private:
		void OnTabComplete(wxKeyEvent &event);
		wxDECLARE_EVENT_TABLE();
};

class CodeDisplay : public wxStyledTextCtrl {
	public:
		CodeDisplay(wxWindow *parent, wxWindowID id);

	private:
		void OnLeftUp(wxMouseEvent &event);
		void OnRunToLine(wxCommandEvent &event);
		void OnJumpToLine(wxCommandEvent &event);
		void OnKillFocus(wxFocusEvent &event);
		wxDECLARE_EVENT_TABLE();
};

struct RegisterValue {
	char format1[32];
	char format2[32];
	uint64_t asInteger;
};

struct RegisterPair {
	const char *key;
	RegisterValue value;
};

class MyFrame : public wxFrame {
	public:
		MyFrame(const wxString &title, const wxPoint &position, const wxSize &size);
		~MyFrame() { auiManager.UnInit(); }

		void SendToGDB(const char *string, bool echo = true);
		void LoadFile(const char *newFile, size_t newFileBytes);
		void SetLine(int line, bool asPosition);
		void SyncWithVim();
		void Update();
		void ToggleBreakpoint(int line);
		uint64_t GetSelectedAddress();
		void PushAddress();

		wxStyledTextCtrl *console = nullptr, *programOutput = nullptr, *memoryView = nullptr;
		struct CodeDisplay *display = nullptr;
		struct MyTextCtrl *consoleInput = nullptr;
		wxListCtrl *breakpointList = nullptr, *registerList = nullptr, *callStackList = nullptr, *localsList = nullptr;
		wxAuiManager auiManager;

		void FocusInput		(wxCommandEvent &event) { display->SetFocus(); consoleInput->SetFocus(); }
		void StepIn		(wxCommandEvent &event) { SendToGDB("stepi"); }
		void StepOver		(wxCommandEvent &event) { SendToGDB("nexti"); }
		void StepOut		(wxCommandEvent &event) { SendToGDB("finish"); }
		void Continue		(wxCommandEvent &event) { SendToGDB("c"); }
		void Connect		(wxCommandEvent &event) { SendToGDB("target remote :1234"); }
		void Run		(wxCommandEvent &event) { SendToGDB("r > .output.fifo"); }
		void RunPaused		(wxCommandEvent &event) { SendToGDB("starti > .output.fifo"); }
		void Kill		(wxCommandEvent &event) { SendToGDB("kill"); }
		void NextMemory		(wxCommandEvent &event) { memoryAddress += MEMORY_VIEW_BYTES / 2; viewingStack = false; Update(); }
		void PreviousMemory	(wxCommandEvent &event) { memoryAddress -= MEMORY_VIEW_BYTES / 2; viewingStack = false; Update(); }
		void ViewStack		(wxCommandEvent &event) { PushAddress(); viewingStack = true; Update(); }
		void Break		(wxCommandEvent &event) { kill(gdbPID, SIGINT); }
		void NextMemoryViewSize (wxCommandEvent &event) { if (structFields) arrfree(structFields); else { memoryWordSize <<= 1; if (memoryWordSize == 16) memoryWordSize = 1; } Update(); }
		void _SyncWithVim	(wxCommandEvent &event) { SyncWithVim(); }
		void AddressToMemory	(wxCommandEvent &event);
		void AddressToSource	(wxCommandEvent &event);
		void SelectMemory	(wxCommandEvent &event);
		void RemoveItem		(wxCommandEvent &event);
		void AddBreakpointH	(wxCommandEvent &event);
		void RestartGDB		(wxCommandEvent &event);
		void OnEnterInput	(wxCommandEvent &event);
		void OnExit		(wxCommandEvent &event);
		void OnReceiveData	(wxCommandEvent &event);
		void OnReceiveOutput	(wxCommandEvent &event);
		void FormatAsStruct	(wxCommandEvent &event);
		void PopAddress		(wxCommandEvent &event);
		void OnMarginClick	(wxStyledTextEvent &event);

	private:

		wxDECLARE_EVENT_TABLE();
};

extern "C" char **environ;

enum {
	ID_SourceDisplay 	= 1,
	ID_ConsoleInput 	= 2,
	ID_ConsoleOutput 	= 3,
	ID_BreakpointList	= 4,
	ID_RegisterList		= 5,
	ID_FocusInput		= 10,
	ID_AddBreakpoint	= 13,
	ID_RestartGDB		= 15,
	ID_Kill			= 18,
	ID_CallStackList	= 19,
	ID_StepIn 		= 101,
	ID_StepOver 		= 102,
	ID_Continue 		= 103,
	ID_Break 		= 104,
	ID_StepOut		= 105,
	ID_Run			= 106,
	ID_RunPaused		= 107,
	ID_Connect 		= 108,
	ID_SyncWithVim 		= 109,
	ID_PreviousMemory	= 110,
	ID_NextMemory		= 111,
	ID_SelectMemory		= 112,
	ID_AddressToMemory	= 113,
	ID_AddressToSource	= 114,
	ID_ViewStack		= 115,
	ID_NextMemoryViewSize	= 116,
	ID_FormatAsStruct	= 118,
	ID_PopAddress		= 119,
};

struct Breakpoint {
	uint64_t address;
	int line;
	char file[PATH_MAX + 1];
};

struct CachedLine {
	uint64_t key;
	PreprocessedLine value;
};

CachedLine *cachedLinesResolved, *cachedLines;

Breakpoint *breakpoints;

RegisterPair *currentRegisters;
RegisterValue *currentRegistersAsArray;

MyFrame *frame;
MyApp *myApp;

char currentFile[PATH_MAX + 1];
int64_t currentFileModified;
int oldLine;
bool doNotUpdatePositionOnce;

char commandHistory[256][1024]; // TODO Check this isn't exceeded.
int commandHistoryIndex;
volatile bool focusInput;

char *_buffer;
FASHeader *header;

uint64_t onRunToLineAddress;
uint64_t *currentCallStack;

wxFont font;
wxFont font2;

char sendBuffer[4096];

wxDEFINE_EVENT(RECEIVE_GDB_DATA, wxCommandEvent);
wxDEFINE_EVENT(RECEIVE_OUTPUT_DATA, wxCommandEvent);
wxDEFINE_EVENT(RUN_TO_LINE, wxCommandEvent);
wxDEFINE_EVENT(JUMP_TO_LINE, wxCommandEvent);

wxBEGIN_EVENT_TABLE(MyFrame, wxFrame)
	EVT_STC_MARGINCLICK(	ID_SourceDisplay, 		MyFrame::OnMarginClick)
	EVT_TEXT_ENTER(		ID_ConsoleInput, 		MyFrame::OnEnterInput)
	EVT_COMMAND(		wxID_ANY, RECEIVE_GDB_DATA, 	MyFrame::OnReceiveData)
	EVT_COMMAND(		wxID_ANY, RECEIVE_OUTPUT_DATA, 	MyFrame::OnReceiveOutput)
	EVT_MENU(		ID_StepIn,			MyFrame::StepIn)
	EVT_MENU(		ID_StepOver,			MyFrame::StepOver)
	EVT_MENU(		ID_StepOut,			MyFrame::StepOut)
	EVT_MENU(		ID_Continue,			MyFrame::Continue)
	EVT_MENU(		ID_Connect,			MyFrame::Connect)
	EVT_MENU(		ID_Break,			MyFrame::Break)
	EVT_MENU(		ID_Run,				MyFrame::Run)
	EVT_MENU(		ID_RunPaused,			MyFrame::RunPaused)
	EVT_MENU(		ID_FocusInput,			MyFrame::FocusInput)
	EVT_MENU(		ID_AddBreakpoint,		MyFrame::AddBreakpointH)
	EVT_MENU(		ID_RestartGDB,			MyFrame::RestartGDB)
	EVT_MENU(		ID_Kill,			MyFrame::Kill)
	EVT_MENU(		ID_SyncWithVim, 		MyFrame::_SyncWithVim)
	EVT_MENU(		ID_NextMemory, 			MyFrame::NextMemory)
	EVT_MENU(		ID_SelectMemory,		MyFrame::SelectMemory)
	EVT_MENU(		ID_PreviousMemory, 		MyFrame::PreviousMemory)
	EVT_MENU(		ID_AddressToMemory,		MyFrame::AddressToMemory)
	EVT_MENU(		ID_AddressToSource,		MyFrame::AddressToSource)
	EVT_MENU(		ID_ViewStack,			MyFrame::ViewStack)
	EVT_MENU(		ID_NextMemoryViewSize,		MyFrame::NextMemoryViewSize)
	EVT_MENU(		ID_FormatAsStruct,		MyFrame::FormatAsStruct)
	EVT_MENU(		ID_PopAddress,			MyFrame::PopAddress)
	EVT_MENU(		wxID_DELETE,			MyFrame::RemoveItem)
	EVT_MENU(		wxID_EXIT, 			MyFrame::OnExit)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(MyTextCtrl, wxTextCtrl)
	EVT_CHAR(MyTextCtrl::OnTabComplete)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(CodeDisplay, wxStyledTextCtrl)
	EVT_LEFT_UP(CodeDisplay::OnLeftUp)
	EVT_COMMAND(wxID_ANY, RUN_TO_LINE, CodeDisplay::OnRunToLine)
	EVT_COMMAND(wxID_ANY, JUMP_TO_LINE, CodeDisplay::OnJumpToLine)
	EVT_KILL_FOCUS(CodeDisplay::OnKillFocus)
wxEND_EVENT_TABLE()

wxIMPLEMENT_APP(MyApp);

MyTextCtrl ::MyTextCtrl (wxWindow *parent, wxWindowID id) : wxTextCtrl      (parent, id) {}
CodeDisplay::CodeDisplay(wxWindow *parent, wxWindowID id) : wxStyledTextCtrl(parent, id) {}

void MyFrame::SendToGDB(const char *string, bool echo) {
	char newline = '\n';

	if (echo) {
		console->SetReadOnly(false);
		console->InsertText(console->GetTextLength(), wxString(string));
		console->InsertText(console->GetTextLength(), wxString("\n"));
		console->ScrollToEnd();
		console->SetReadOnly(true);
		display->SetCaretLineVisible(false);
	}

	// printf("sending: %s\n", string);

	write(pipeToGDB, string, strlen(string));
	write(pipeToGDB, &newline, 1);
}

void QueryGDB() {
	modeEvaluate = true;
	evaluateMutex.Lock();
	frame->SendToGDB(sendBuffer, false);
	evaluateEvent.Wait();
	evaluateMutex.Unlock();
	modeEvaluate = false;
}

volatile bool stopDebugger;

void *OutputThread(void *_frame) {
	int pipe = open(".output.fifo", O_RDONLY);
	char buffer[1024 + 1];

	while (true) {
		int n = read(pipe, buffer, sizeof(buffer) - 1);
		if (n < 0) { printf("FIFO ERROR %d\n", n); break; }
		buffer[n] = 0;
		if (n == 0) continue;
		for (int i = 0; i < n; i++) if (buffer[i] == 0) buffer[i] = '?';
		wxCommandEvent event(RECEIVE_OUTPUT_DATA); 
		event.SetClientData(strdup(buffer));
		if (!stopDebugger) wxPostEvent(frame, event); 
	}

	return nullptr;
}

void *DebuggerThread(void *_frame) {
	int outputPipe[2], inputPipe[2];
	pipe(outputPipe);
	pipe(inputPipe);

	char *const argv[] = { (char *) "gdb", nullptr };
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_adddup2(&actions, inputPipe[0],  0);
	posix_spawn_file_actions_adddup2(&actions, outputPipe[1], 1);
	posix_spawn_file_actions_adddup2(&actions, outputPipe[1], 2);
	posix_spawnp((pid_t *) &gdbPID, "gdb", &actions, nullptr, argv, environ);

	pipeToGDB = inputPipe[1];

	while (true) {
		char buffer[512 + 1];
		char *data = buffer;
		buffer[read(outputPipe[0], buffer, 512)] = 0;
		if (!buffer[0]) return nullptr;

		if (newReceive) strcpy(receiveBuffer, data);
		else strcat(receiveBuffer, data);
		newReceive = false;
		if (!strstr(data, "(gdb) ")) continue;
		data = receiveBuffer;
		newReceive = true;

		if (modeEvaluate) {
			evaluateMutex.Lock();
			evaluateEvent.Broadcast();
			evaluateMutex.Unlock();
		} else {
			wxCommandEvent event(RECEIVE_GDB_DATA); 
			if (!stopDebugger) wxPostEvent(frame, event); 
		}
	}
}

void StartGDBThread() {
	pthread_t thread;
	pthread_attr_t attributes;
	pthread_attr_init(&attributes);
	pthread_create(&thread, &attributes, DebuggerThread, frame);
	gdbThread = thread;
}

void StartOutputThread() {
	pthread_t thread;
	pthread_attr_t attributes;
	pthread_attr_init(&attributes);
	pthread_create(&thread, &attributes, OutputThread, frame);
}

char *LookupString(uint32_t offset) {
	assert(offset < header->stringTableLength);
	return _buffer + header->stringTableOffset + offset;
}

bool MyApp::OnInit() {
	stbds_sh_new_strdup(currentRegisters);

	if (argc != 2) {
		fprintf(stderr, "Usage: fgdw <symbol-file>\n");
		return false;
	}

	FILE *file = fopen(argv[1], "rb");

	if (!file) {
		fprintf(stderr, "Could not load symbol file '%s'.\n", argv[1]);
		return false;
	}

	fseek(file, 0, SEEK_END);
	size_t size = ftell(file);
	fseek(file, 0, SEEK_SET);
	_buffer = (char *) malloc(size);
	header = (FASHeader *) _buffer;
	fread(_buffer, 1, size, file);

	assert(header->signature == FAS_SIGNATURE);

	mkfifo(".output.fifo", 438 /* octal: 666 */);

	myApp = this;
	frame = new MyFrame("fdgw", wxPoint(50, 50), wxSize(1024, 768));
	frame->Show(true);

	StartGDBThread();
	StartOutputThread();

	return true;
}

int MyApp::OnExit() {
	stopDebugger = true;
	kill(gdbPID, SIGINT);
	write(pipeToGDB, "q\n", 2);
	// kill(gdbPID, SIGKILL);
	sprintf(sendBuffer, "kill -KILL %d", gdbPID);
	system(sendBuffer);
	system("rm .output.fifo");
	return wxApp::OnExit();
}

uint32_t SwapColorChannels(uint32_t color) {
	return ((color & 0xFF) << 16) | ((color & 0xFF0000) >> 16) | (color & 0xFF00);
}

MyFrame::MyFrame(const wxString &title, const wxPoint &position, const wxSize &size) : wxFrame(nullptr, wxID_ANY, title, position, size) {
	auiManager.SetManagedWindow(this);

	FILE *file = fopen("config.mtsrc", "rb");

	int sourceCodeAndConsoleTextSize = 14;
	int dataViewTextSize = 11;

	wxColor background = wxColor(40, 40, 45), backgroundLight = wxColor(80, 80, 85);
	wxColor textColors[32] = { wxColor(255, 255, 255), wxColor(180, 180, 180), wxColor(180, 180, 180), wxColor(180, 180, 180), 
				   wxColor(209, 245, 221), wxColor(255, 255, 255), wxColor(245, 221, 209), wxColor(245, 221, 209),
				   wxColor(245, 221, 209), wxColor(245, 243, 209), wxColor(245, 243, 209), wxColor(255, 255, 255),
				   wxColor(245, 221, 209), wxColor(255, 255, 255), wxColor(245, 221, 209), wxColor(180, 180, 180),
				   wxColor(255, 255, 255), wxColor(180, 180, 180), wxColor(180, 180, 180), wxColor(255, 255, 255), };

	const char *keyContinue = "F5", *keyBreak = "Ctrl+Shift+C", *keyStepOver = "F10", *keyStepIn = "F11", *keyStepOut = "Shift+F11", *keyToggleBreakpoint = "F9";

	if (file) {
		fseek(file, 0, SEEK_END);
		size_t bytes = ftell(file);
		fseek(file, 0, SEEK_SET);
		char *buffer = (char *) malloc(bytes + 1);
		buffer[bytes] = 0;
		fread(buffer, 1, bytes, file);
		fclose(file);

		MTStream stream = MTBufferedCreateWriteStream();

		if (!MTParse(&stream, buffer, nullptr)) {
			printf("Could not parse 'config.mtsrc': L%d - %s.\n", stream.line, MTErrorMessage(&stream));
		} else {
			MTBufferedStartReading(&stream);

			MTReadFormat(&stream, "{");

			while (true) {
				MTEntry entry = MTRead(&stream);
				if (entry.type == MT_CLOSE) break;

				if (0 == strcmp(entry.key, "sourceCodeAndConsoleTextSize") && entry.type == MT_INTEGER) {
					sourceCodeAndConsoleTextSize = entry.integer;
				} else if (0 == strcmp(entry.key, "dataViewTextSize") && entry.type == MT_INTEGER) {
					dataViewTextSize = entry.integer;
				} else if (0 == strcmp(entry.key, "color1") && entry.type == MT_INTEGER) {
					textColors[0] = textColors[5] = textColors[11] = textColors[13] = textColors[16] = textColors[19] = wxColor(SwapColorChannels(entry.integer));
				} else if (0 == strcmp(entry.key, "color2") && entry.type == MT_INTEGER) {
					textColors[1] = textColors[2] = textColors[3] = textColors[15] = textColors[17] = textColors[18] = wxColor(SwapColorChannels(entry.integer));
				} else if (0 == strcmp(entry.key, "color3") && entry.type == MT_INTEGER) {
					textColors[4] = wxColor(SwapColorChannels(entry.integer));
				} else if (0 == strcmp(entry.key, "color4") && entry.type == MT_INTEGER) {
					textColors[6] = textColors[7] = textColors[8] = textColors[12] = textColors[14] = wxColor(SwapColorChannels(entry.integer));
				} else if (0 == strcmp(entry.key, "color5") && entry.type == MT_INTEGER) {
					textColors[9] = textColors[10] = wxColor(SwapColorChannels(entry.integer));
				} else if (0 == strcmp(entry.key, "background1") && entry.type == MT_INTEGER) {
					background = wxColor(SwapColorChannels(entry.integer));
				} else if (0 == strcmp(entry.key, "background2") && entry.type == MT_INTEGER) {
					backgroundLight = wxColor(SwapColorChannels(entry.integer));
				} else if (0 == strcmp(entry.key, "keyContinue") && entry.type == MT_DATA) {
					keyContinue = (const char *) entry.data;
				} else if (0 == strcmp(entry.key, "keyBreak") && entry.type == MT_DATA) {
					keyBreak = (const char *) entry.data;
				} else if (0 == strcmp(entry.key, "keyStepOver") && entry.type == MT_DATA) {
					keyStepOver = (const char *) entry.data;
				} else if (0 == strcmp(entry.key, "keyStepIn") && entry.type == MT_DATA) {
					keyStepIn = (const char *) entry.data;
				} else if (0 == strcmp(entry.key, "keyStepOut") && entry.type == MT_DATA) {
					keyStepOut = (const char *) entry.data;
				} else if (0 == strcmp(entry.key, "keyToggleBreakpoint") && entry.type == MT_DATA) {
					keyToggleBreakpoint = (const char *) entry.data;
				} else {
					printf("Unrecognised configuration value '%s'.\n", entry.key);
				}
			}

			if (stream.error) {
				printf("The config.mtsrc file was invalid; some settings may not have been loaded.\n");
			}

			MTBufferedDestroyStream(&stream);
		}
	}

	font = wxFont(sourceCodeAndConsoleTextSize, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	font2 = wxFont(dataViewTextSize, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);

	wxTextAttr textAttributes = wxTextAttr(*wxWHITE, *wxBLACK, font);

	display = new CodeDisplay(this, ID_SourceDisplay);
	display->SetTabWidth(32);
	display->SetReadOnly(true);
	display->SetCaretWidth(0);
	display->SetMarginWidth(0, 40);
	display->SetMarginWidth(1, 32);
	display->SetMarginType(1, wxSTC_MARGIN_TEXT);
	display->SetMarginSensitive(1, true);
	display->SetYCaretPolicy(wxSTC_CARET_STRICT | wxSTC_CARET_EVEN, 0);
	display->SetCaretLineBackground(backgroundLight);
	// display->SetCaretLineVisibleAlways(true);
	display->SetLexer(wxSTC_LEX_ASM);
	display->SetFocus();

	for (int i = 0; i < wxSTC_STYLE_MAX; i++) {
		if (i != wxSTC_STYLE_LINENUMBER && i != wxSTC_STYLE_DEFAULT) display->StyleSetFont(i, font);
		display->StyleSetBackground(i, background);
		if (i < 20) display->StyleSetForeground(i, textColors[i]);
	}

	display->StyleSetBackground(wxSTC_STYLE_MAX - 1, *wxRED);
	display->StyleSetBackground(wxSTC_STYLE_MAX - 2, *wxYELLOW);
	display->StyleSetBackground(wxSTC_STYLE_MAX - 3, *wxCYAN);
	display->StyleSetBackground(wxSTC_STYLE_LINENUMBER, backgroundLight);
	display->StyleSetForeground(wxSTC_STYLE_LINENUMBER, *wxWHITE);

	breakpointList = new wxListCtrl(this, ID_BreakpointList, wxPoint(0, 0), wxSize(400, 200), wxLC_REPORT);
	breakpointList->AppendColumn(wxT("File"));
	breakpointList->AppendColumn(wxT("Line"));
	breakpointList->AppendColumn(wxT("Address"));

	localsList = new wxListCtrl(this, ID_CallStackList, wxPoint(0, 0), wxSize(250, 0), wxLC_REPORT);
	localsList->AppendColumn(wxT("Name"));
	localsList->AppendColumn(wxT("Decimal"));
	localsList->AppendColumn(wxT("Hex"));
	localsList->AppendColumn(wxT("Stack address"));

	callStackList = new wxListCtrl(this, ID_CallStackList, wxPoint(0, 0), wxSize(250, 0), wxLC_REPORT);
	callStackList->AppendColumn(wxT("Index"));
	callStackList->AppendColumn(wxT("Location"));
	callStackList->AppendColumn(wxT("Address"));
	callStackList->AppendColumn(wxT("Function"));

	registerList = new wxListCtrl(this, ID_RegisterList, wxPoint(0, 0), wxSize(400, 400), wxLC_REPORT);
	registerList->AppendColumn(wxT("Name"));
	registerList->AppendColumn(wxT("Hex"));
	registerList->AppendColumn(wxT("Value"));
	registerList->AppendColumn(wxT("Location"));

	wxPanel *panel2 = new wxPanel(this, wxID_ANY, wxPoint(0, 0), wxSize(200, 350));
	wxBoxSizer *sizer2 = new wxBoxSizer(wxVERTICAL);
	panel2->SetSizer(sizer2);
	console = new wxStyledTextCtrl(panel2, ID_ConsoleOutput, wxPoint(0, 0), wxSize(200, 450));
	console->StyleSetFont(0, font);
	console->SetTabWidth(8);
	console->SetReadOnly(true);
	console->SetMarginWidth(1, 0);
	console->SetCaretWidth(0);
	console->StyleSetBackground(0, background);
	console->StyleSetForeground(0, textColors[0]);
	console->StyleSetBackground(wxSTC_STYLE_DEFAULT, background);
	consoleInput = new MyTextCtrl(panel2, ID_ConsoleInput);
	consoleInput->SetWindowStyle(wxHSCROLL | wxTE_PROCESS_ENTER | wxTE_PROCESS_TAB);
	consoleInput->SetFocus();
	consoleInput->SetDefaultStyle(textAttributes);
	consoleInput->SetFont(font);
	sizer2->Add(console, 1, wxEXPAND);
	sizer2->Add(consoleInput, 0, wxEXPAND);

	programOutput = new wxStyledTextCtrl(this, wxID_ANY, wxPoint(0, 0), wxSize(200, 250));
	programOutput->StyleSetFont(0, font);
	programOutput->SetTabWidth(8);
	programOutput->SetReadOnly(true);
	programOutput->SetMarginWidth(1, 0);
	programOutput->SetCaretWidth(0);
	programOutput->StyleSetBackground(0, background);
	programOutput->StyleSetForeground(0, textColors[0]);
	programOutput->StyleSetBackground(wxSTC_STYLE_DEFAULT, background);

	memoryView = new wxStyledTextCtrl(this, wxID_ANY, wxPoint(0, 0), wxSize(200, 250));
	memoryView->StyleSetFont(0, font);
	memoryView->SetTabWidth(8);
	memoryView->SetReadOnly(true);
	memoryView->SetMarginWidth(1, 0);
	memoryView->SetCaretWidth(0);
	memoryView->StyleSetBackground(0, background);
	memoryView->StyleSetForeground(0, textColors[0]);
	memoryView->StyleSetBackground(wxSTC_STYLE_DEFAULT, background);

	wxMenu *menuProcess = new wxMenu;
	menuProcess->Append(ID_Run, 		"&Run\tShift+F5");
	menuProcess->Append(ID_RunPaused,	"Run &paused\tCtrl+F5");
	menuProcess->AppendSeparator();
	menuProcess->Append(ID_Kill,		"&Kill\tF3");

	wxMenu *menuEdit = new wxMenu;
	menuEdit->Append(wxID_CUT);
	menuEdit->Append(wxID_COPY);
	menuEdit->Append(wxID_PASTE);
	menuEdit->Append(wxID_DELETE, 		"&Delete\tDel");
	menuEdit->Append(wxID_SELECTALL);

	wxMenu *menuDebug = new wxMenu;
	menuDebug->Append(ID_RestartGDB, 	"&Restart GDB\tCtrl+R");
	menuDebug->AppendSeparator();
	menuDebug->Append(ID_Connect, 		"Connec&t\tF4");
	menuDebug->Append(ID_Continue, 		wxString::Format("&Continue\t%s", keyContinue));
	menuDebug->Append(ID_StepOver, 		wxString::Format("Step &over\t%s", keyStepOver));
	menuDebug->Append(ID_StepIn, 		wxString::Format("Step &in\t%s", keyStepIn));
	menuDebug->Append(ID_StepOut, 		wxString::Format("Step &out\t%s", keyStepOut));
	menuDebug->Append(ID_Break, 		wxString::Format("&Break\t%s", keyBreak));
	menuDebug->AppendSeparator();
	menuDebug->Append(ID_AddBreakpoint,	wxString::Format("&Add breakpoint\t%s", keyToggleBreakpoint));

	wxMenu *menuView = new wxMenu;
	menuView->Append(ID_FocusInput, 	"Focus &input\tCtrl+I");
	menuView->Append(ID_SyncWithVim, 	"&Sync source with gvim\tF2");
	menuView->AppendSeparator();
	menuView->Append(ID_PreviousMemory, 	"Move memory view &backwards\tF6");
	menuView->Append(ID_NextMemory, 	"Move memory view &forwards\tF7");
	menuView->Append(ID_NextMemoryViewSize, "&Change memory view word size\tF12");
	menuView->Append(ID_FormatAsStruct, 	"Format &as structure\tShift+F12");
	menuView->AppendSeparator();
	menuView->Append(ID_SelectMemory, 	"Select &address\tCtrl+F7");
	menuView->Append(ID_ViewStack, 		"View stac&k\tCtrl+S");
	menuView->Append(ID_PopAddress, 	"&Previous viewed address\tCtrl+F6");
	menuView->AppendSeparator();
	menuView->Append(ID_AddressToMemory,	"Goto address in &memory view\tCtrl+B");
	menuView->Append(ID_AddressToSource,	"&Goto address in source view\tCtrl+G");

	wxMenuBar *menuBar = new wxMenuBar;
	menuBar->Append(menuProcess, "&Process");
	menuBar->Append(menuEdit, "&Edit");
	menuBar->Append(menuDebug, "&Debug");
	menuBar->Append(menuView, "&View");
	SetMenuBar(menuBar);

	auiManager.AddPane(display, wxCENTER, wxT("Source"));
	auiManager.AddPane(breakpointList, wxRIGHT, wxT("Breakpoints"));
	auiManager.AddPane(registerList, wxLEFT, wxT("Registers"));
	auiManager.AddPane(memoryView, wxBOTTOM, wxT("Memory"));
	auiManager.AddPane(panel2, wxBOTTOM, wxT("GDB"));
	auiManager.GetPane(panel2).MinSize(wxSize(10, 350));
	auiManager.AddPane(programOutput, wxRIGHT, wxT("Output"));
	auiManager.AddPane(callStackList, wxRIGHT, wxT("Stack trace"));
	auiManager.AddPane(localsList, wxLEFT, wxT("Locals"));

	auiManager.Update();

	Centre();
}

void MyFrame::OnExit(wxCommandEvent &event) {
	stopDebugger = true;
	frame = nullptr;
	Close(true);
}

void MyFrame::SyncWithVim() {
	if (!system("vim --servername GVIM --remote-expr \"execute(\\\"ls\\\")\" | grep % > .temp.txt")) {
		char buffer[4096];
		FILE *file = fopen(".temp.txt", "r");

		if (file) {
			buffer[fread(buffer, 1, sizeof(buffer) - 1, file)] = 0;
			fclose(file);

			{
				char *name = strchr(buffer, '"');
				if (!name) goto done;
				char *nameEnd = strchr(++name, '"');
				if (!nameEnd) goto done;
				LoadFile(name, nameEnd - name);
				char *line = strstr(buffer, "line ");
				if (!line) goto done;
				int lineNumber = atoi(line + 5);
				SetLine(lineNumber, false);
			}

			done:;
			unlink(".temp.txt");
		}
	}
}

PreprocessedLine LookupLine(uint32_t offset) {
	FASPreprocessedLineHeader *line = (FASPreprocessedLineHeader *) (_buffer + header->preprocessedSourceOffset + offset);

	const char *name;
	size_t nameLength;
	uint32_t macroLine;

	if (line->generatorNameOffset) {
		if (line->lineSource == FAS_LINE_SOURCE_MACRO) {
			name = _buffer + header->preprocessedSourceOffset + line->generatorNameOffset + 1;
			nameLength = (uint8_t) _buffer[header->preprocessedSourceOffset + line->generatorNameOffset];
		} else {
			name = _buffer + header->preprocessedSourceOffset + line->generatorNameOffset;
			nameLength = strlen(name);
		}
	} else {
		name = LookupString(header->inputFileStringOffset);
		nameLength = strlen(name);
	}

	if (line->lineSource == FAS_LINE_SOURCE_MACRO) {
		macroLine = line->macroLineOffset;
	} else {
		macroLine = 0;
	}

	return { name, nameLength, line->lineNumber, macroLine, line->lineSource == FAS_LINE_SOURCE_MACRO };
}

uint64_t AddressFromLine(const char *fileName, int _line) {
	uint32_t offset = 0;
	size_t fileNameLength = strlen(fileName);

	while (offset < header->assemblyLength - sizeof(uint32_t)) {
		FASAssemblyRow *row = (FASAssemblyRow *) (_buffer + header->assemblyOffset + offset);
		int next = row->preprocessedLineOffset;
		PreprocessedLine line;

		do {
			line = LookupLine(next);

			if (line.lineNumber == _line && line.nameLength == fileNameLength && 0 == memcmp(line.name, fileName, fileNameLength)) {
				return ((uint64_t) row->addressLow | ((uint64_t) row->addressMedium << 32));
			}

			next = line.macroLine;
		} while (line.fromMacro);

		offset += sizeof(FASAssemblyRow);
	}

	return 0;
}

struct Symbol {
	const char *name;
	size_t nameLength;
	uint64_t address;
};

uint64_t AddressFromSymbol(const char *string) {
	uint32_t offset = 0;

	while (offset < header->symbolTableLength) {
		FASSymbol *symbol = (FASSymbol *) (_buffer + header->symbolTableOffset + offset);

		const char *name;
		size_t nameLength;

		if (symbol->nameType == FAS_NAME_IN_PREPROCESSED_SOURCE) {
			name = _buffer + header->preprocessedSourceOffset + symbol->nameData + 1;
			nameLength = (uint8_t) _buffer[header->preprocessedSourceOffset + symbol->nameData];
		} else /* if (symbol->nameType == FAS_NAME_IN_STRING_TABLE) */ {
			name = LookupString(symbol->nameData);
			nameLength = strlen(name);
		}

		if (0 == memcmp(string, name, nameLength) && string[nameLength] == 0) {
			return symbol->value;
		}

		offset += sizeof(FASSymbol);
	}

	return 0;
}

Symbol SymbolFromAddress(uint64_t address) {
	// printf("\n~~~ Symbols ~~~\n");

	uint32_t offset = 0;
	Symbol _symbol = {};

	while (offset < header->symbolTableLength) {
		FASSymbol *symbol = (FASSymbol *) (_buffer + header->symbolTableOffset + offset);

		const char *name;
		size_t nameLength;

		if (symbol->nameType == FAS_NAME_IN_PREPROCESSED_SOURCE) {
			name = _buffer + header->preprocessedSourceOffset + symbol->nameData + 1;
			nameLength = (uint8_t) _buffer[header->preprocessedSourceOffset + symbol->nameData];
		} else /* if (symbol->nameType == FAS_NAME_IN_STRING_TABLE) */ {
			name = LookupString(symbol->nameData);
			nameLength = strlen(name);
		}

		if (symbol->value <= address && symbol->value >= _symbol.address) {
			_symbol.name = name, _symbol.nameLength = nameLength, _symbol.address = symbol->value;
		}

		// printf("'%.*s': 0x%llX\n", nameLength, name, symbol->value);
		offset += sizeof(FASSymbol);
	}

	return _symbol;
}

bool LineFromAddress(uint64_t address, PreprocessedLine *line, bool resolveMacro, bool early) {
	ptrdiff_t index = resolveMacro ? hmgeti(cachedLinesResolved, address) : hmgeti(cachedLines, address);

	if (index != -1) {
		*line = (resolveMacro ? cachedLinesResolved : cachedLines)[index].value;
		return true;
	}

	uint32_t offset = 0;
	bool found = false;

	while (offset < header->assemblyLength - sizeof(uint32_t)) {
		FASAssemblyRow *row = (FASAssemblyRow *) (_buffer + header->assemblyOffset + offset);

		if (((uint64_t) row->addressLow | ((uint64_t) row->addressMedium << 32)) == address) {
			*line = LookupLine(row->preprocessedLineOffset);

			while (resolveMacro && line->fromMacro) {
				*line = LookupLine(line->macroLine);
			}

			found = true;

			if (early) {
				break;
			}
		}

		offset += sizeof(FASAssemblyRow);
	}

	if (found) {
		if (resolveMacro) {
			hmput(cachedLinesResolved, address, *line);
		} else {
			hmput(cachedLines, address, *line);
		}
	}

	return found;
}

void MyFrame::SetLine(int line, bool asPosition) {
	display->GotoLine(line - 1);
	display->SetCaretLineVisible(true);
	if (asPosition) {
		if (oldLine) display->MarginSetStyle(oldLine - 1, wxSTC_STYLE_LINENUMBER);
		display->MarginSetStyle(line - 1, wxSTC_STYLE_MAX - 3);
		oldLine = line;
	}
}

void MyFrame::LoadFile(const char *_newFile, size_t newFileBytes) {
	char newPath2[PATH_MAX + 1];
	char newPath[PATH_MAX + 1];
	snprintf(newPath2, sizeof(newPath2), "%.*s", newFileBytes, _newFile);
	realpath(newPath2, newPath);

	struct stat s = {};
	if (stat(newPath, &s)) return;
	if (!strcmp(newPath2, currentFile) && currentFileModified == s.st_mtime) return;
	currentFileModified = s.st_mtime;

	FILE *input = fopen(newPath, "rb");
	if (!input) { return; }
	fseek(input, 0, SEEK_END);
	uint64_t fileLength = ftell(input);
	fseek(input, 0, SEEK_SET);
	char *data = (char *) malloc(fileLength);
	fileLength = fread(data, 1, fileLength, input);
	if (!fileLength) { free(data); return; }
	fclose(input);

	display->SetReadOnly(false);
	display->ClearAll();
	display->InsertText(0, wxString(data, fileLength));
	display->SetReadOnly(true);
	display->SetFocus();
	consoleInput->SetFocus();

	strcpy(currentFile, newPath2);
}

void MyFrame::OnReceiveOutput(wxCommandEvent &event) {
	char *data = (char *) event.GetClientData();
	programOutput->SetReadOnly(false);
	programOutput->InsertText(programOutput->GetTextLength(), wxString(data));
	programOutput->ScrollToEnd();
	programOutput->SetReadOnly(true);
	free(data);
}

void MyFrame::Update() {
	if (!initialisedGDB) {
		char *executableFile = LookupString(header->outputFileStringOffset);
		sprintf(sendBuffer, "file \"%s\"", executableFile);
		QueryGDB();
		initialisedGDB = true;
	}

	uint64_t basePointer = 0, stackPointer = 0, instructionPointer = 0;

	{
		sprintf(sendBuffer, "info registers");
		QueryGDB();
		// printf("\n\n\n%s\n\n\n", receiveBuffer);
		
		if (!strstr(receiveBuffer, "The program has no registers now.")) {
			{
				char *position = receiveBuffer;
				registerList->DeleteAllItems();
				int index = 0;
				arrfree(currentRegistersAsArray);

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

					char name[64];
					snprintf(name, sizeof(name), "%.*s", nameEnd - nameStart, nameStart);
					char format1[64];
					snprintf(format1, sizeof(format1), "%.*s", format1End - format1Start, format1Start);
					char format2[64];
					snprintf(format2, sizeof(format2), "%.*s", format2End - format2Start, format2Start);

					char location[64];
					location[0] = 0;
					PreprocessedLine line;
					uint64_t asInteger = strtoul(format1, nullptr, 0);

					if (asInteger && LineFromAddress(asInteger, &line, true, false)) {
						snprintf(location, sizeof(location), "%.*s:%d\n", line.nameLength, line.name, line.lineNumber);
					}

					RegisterValue value = shget(currentRegisters, name);
					bool modified = false;

					if (strcmp(format1, value.format1) || strcmp(format2, value.format2)) {
						strcpy(value.format1, format1);
						strcpy(value.format2, format2);
						value.asInteger = asInteger;
						shput(currentRegisters, name, value);
						modified = true;
					}

					arrput(currentRegistersAsArray, value);

					if (0 == strcmp(name, "rip") || 0 == strcmp(name, "eip") || 0 == strcmp(name, "ip")) {
						modified = false; // It's a bit distracting for the instruction pointer to change on each step.
					}

					wxListItem info;
					info.SetMask(wxLIST_MASK_TEXT);
					info.SetId(index);
					info.SetBackgroundColour(wxColour(255, modified ? 180 : 255, 255));
					info.SetTextColour(wxColor(0, 0, 0));
					info.SetText(name);
					registerList->InsertItem(info);
					registerList->SetItem(index, 1, format1);
					registerList->SetItem(index, 2, format2);
					registerList->SetItem(index, 3, location);
					registerList->SetColumnWidth(0, wxLIST_AUTOSIZE);
					registerList->SetColumnWidth(1, wxLIST_AUTOSIZE);
					registerList->SetColumnWidth(2, wxLIST_AUTOSIZE);
					registerList->SetColumnWidth(3, wxLIST_AUTOSIZE);
					index++;
				}
			}

			{
				char *addressString = strstr(receiveBuffer, "\nrip");
				if (!addressString) addressString = strstr(receiveBuffer, "\neip");
				if (!addressString) addressString = strstr(receiveBuffer, "\nip");

				if (addressString && !doNotUpdatePositionOnce) {
					uint64_t address = strtoul(addressString + 5, &addressString, 0);
					PreprocessedLine line;

					if (LineFromAddress(address, &line, true, false)) {
						// printf("%.*s:%d\n", line.nameLength, line.name, line.lineNumber);
						LoadFile(line.name, line.nameLength);
						SetLine(line.lineNumber, true);
					}
				}

				doNotUpdatePositionOnce = false;

				char *stackString = strstr(receiveBuffer, "\nrsp");
				if (!stackString) addressString = strstr(receiveBuffer, "\nesp");
				if (!stackString) addressString = strstr(receiveBuffer, "\nsp");

				if (stackString && viewingStack) {
					memoryAddress = strtoul(stackString + 5, &stackString, 0);
				}
			}
		}
	}

	{
		RegisterValue value;
		value = shget(currentRegisters, "rbp");
		if (value.asInteger) basePointer = value.asInteger;
		value = shget(currentRegisters, "ebp");
		if (value.asInteger) basePointer = value.asInteger;
		value = shget(currentRegisters, "bp");
		if (value.asInteger) basePointer = value.asInteger;
	}

	{
		RegisterValue value;
		value = shget(currentRegisters, "rsp");
		if (value.asInteger) stackPointer = value.asInteger;
		value = shget(currentRegisters, "esp");
		if (value.asInteger) stackPointer = value.asInteger;
		value = shget(currentRegisters, "sp");
		if (value.asInteger) stackPointer = value.asInteger;
	}

	{
		RegisterValue value;
		value = shget(currentRegisters, "rip");
		if (value.asInteger) instructionPointer = value.asInteger;
		value = shget(currentRegisters, "eip");
		if (value.asInteger) instructionPointer = value.asInteger;
		value = shget(currentRegisters, "ip");
		if (value.asInteger) instructionPointer = value.asInteger;
	}

	{
		sprintf(sendBuffer, "x/%dxb 0x%llx", MEMORY_VIEW_BYTES, memoryAddress);
		QueryGDB();

		if (!strstr(receiveBuffer, "Cannot access memory")) {
			uint8_t view[MEMORY_VIEW_BYTES];
			char *position = receiveBuffer;
			int index = 0;

			while (true) {
				position = strchr(position, ':');
				if (!position) break;
				position++;

				while (true) {
					while (*position == ' ' || *position == '\t') position++;
					if (*position == '\n') break;
					view[index++] = strtoul(position, &position, 0);
				}
			}

			char display[65536];
			index = 0;

			if (structFields) {
				for (uintptr_t i = 0; i < arrlen(structFields); i++) {
					StructField *field = structFields + i;

					if (field->offset + field->bytes > MEMORY_VIEW_BYTES) {
						index += snprintf(display + index, sizeof(display) - index, "(truncated)\n");
						break;
					}

					index += snprintf(display + index, sizeof(display) - index, "%.8llX %.*s: ", memoryAddress + field->offset, field->nameLength, field->name);

					bool isMod = false;

					for (int k = 0; k < field->bytes; k++) {
						if (oldMemoryView[field->offset + k] != view[field->offset + k]) {
							isMod = true;
						}
					}

					char mod = isMod ? '*' : ' ';

					if (field->bytes == 1) {
						index += snprintf(display + index, sizeof(display) - index, "%c%.2X %d%c", mod, view[field->offset], view[field->offset], mod);
					} else if (field->bytes == 2) {
						index += snprintf(display + index, sizeof(display) - index, "%c%.4X %d%c", mod, *(uint16_t *) (view + field->offset), 
								*(uint16_t *) (view + field->offset), mod);
					} else if (field->bytes == 4) {
						index += snprintf(display + index, sizeof(display) - index, "%c%.8X %d%c", mod, *(uint32_t *) (view + field->offset), 
								*(uint32_t *) (view + field->offset), mod);
					} else if (field->bytes == 8) {
						index += snprintf(display + index, sizeof(display) - index, "%c%.16llX %lld%c", mod, *(uint64_t *) (view + field->offset), 
								*(uint64_t *) (view + field->offset), mod);
					}

					index += snprintf(display + index, sizeof(display) - index, "\n");
				}
			} else {
				for (uintptr_t i = 0; i < MEMORY_VIEW_BYTES / MEMORY_VIEW_PER_ROW; i++) {
					index += snprintf(display + index, sizeof(display) - index, "%.16llX  ", memoryAddress + i * MEMORY_VIEW_PER_ROW);

					for (uintptr_t j = 0; j < MEMORY_VIEW_PER_ROW; j += memoryWordSize) {
						int offset = i * MEMORY_VIEW_PER_ROW + j;
						bool isMod = false;

						for (int k = 0; k < memoryWordSize; k++) {
							if (oldMemoryView[offset + k] != view[offset + k]) {
								isMod = true;
							}
						}

						char mod = isMod ? '*' : ' ';
						if (basePointer == offset + memoryAddress) mod = '/';

						if (memoryWordSize == 1) {
							index += snprintf(display + index, sizeof(display) - index, "%c%.2X%c", mod, view[offset], mod);
						} else if (memoryWordSize == 2) {
							index += snprintf(display + index, sizeof(display) - index, "%c%.4X%c", mod, *(uint16_t *) (view + offset), mod);
						} else if (memoryWordSize == 4) {
							index += snprintf(display + index, sizeof(display) - index, "%c%.8X%c", mod, *(uint32_t *) (view + offset), mod);
						} else if (memoryWordSize == 8) {
							index += snprintf(display + index, sizeof(display) - index, "%c%.16llX%c", mod, *(uint64_t *) (view + offset), mod);
						}
					}

					index += snprintf(display + index, sizeof(display) - index, "  ");

					for (uintptr_t j = 0; j < MEMORY_VIEW_PER_ROW; j++) {
						int offset = i * MEMORY_VIEW_PER_ROW + j;
						char c = view[offset];
						index += snprintf(display + index, sizeof(display) - index, "%c", c >= 0x20 && c < 0x7F ? c : ' ');
					}

					index += snprintf(display + index, sizeof(display) - index, "\n");
				}
			}

			display[index - 1] = 0;
			memcpy(oldMemoryView, view, sizeof(view));

			memoryView->SetReadOnly(false);
			memoryView->ClearAll();
			memoryView->InsertText(memoryView->GetTextLength(), wxString(display));
			memoryView->ScrollToEnd();
			memoryView->SetReadOnly(true);
		} else {
			memoryView->SetReadOnly(false);
			memoryView->ClearAll();
			memoryView->InsertText(memoryView->GetTextLength(), wxString::Format("Cannot access memory at address 0x%llX.", memoryAddress));
			memoryView->ScrollToEnd();
			memoryView->SetReadOnly(true);
		}
	}

	{
		for (uintptr_t i = 0; i < arrlenu(breakpoints); i++) {
			Breakpoint *breakpoint = breakpoints + i;

			if (0 == strcmp(breakpoint->file, currentFile)) {
				display->MarginSetStyle(breakpoint->line - 1, wxSTC_STYLE_MAX - 1);
			} else {
				display->MarginSetStyle(breakpoint->line - 1, wxSTC_STYLE_LINENUMBER);
			}
		}
	}

	{
		callStackList->DeleteAllItems();

		// printf("---\n");

		int index = 0;
		uint64_t ip = instructionPointer, bp = basePointer;
		arrfree(currentCallStack);

		while (ip && bp) {
			PreprocessedLine line;
			callStackList->InsertItem(index, wxString::Format(wxT("%i"), (int) index));
			// printf("#%d  ", index);
			if (LineFromAddress(ip, &line, true, false)) {
				// printf("%.*s:%d  ", line.nameLength, line.name, line.lineNumber);
				callStackList->SetItem(index, 1, wxString::Format(wxT("%.*s:%i"), (int) line.nameLength, line.name, (int) line.lineNumber));
			} else {
				// printf("??  ");
			}
			Symbol symbol = SymbolFromAddress(ip);
			if (symbol.address) {
				// printf("%.*s  ", symbol.nameLength, symbol.name);
				callStackList->SetItem(index, 3, wxString::Format(wxT("%.*s"), (int) symbol.nameLength, symbol.name));
			}
			// printf("0x%llX\n", ip);
			callStackList->SetItem(index, 2, wxString::Format(wxT("0x%llX"), (uint64_t) ip));
			index++;
			arrput(currentCallStack, ip);

			sprintf(sendBuffer, "x/2gx 0x%llX", bp);
			QueryGDB();

			char *position = receiveBuffer;
			position = strstr(position, ":");
			if (!position) break;
			bp = strtoul(position + 1, &position, 0);
			ip = strtoul(position + 1, &position, 0);
		}

		callStackList->SetColumnWidth(0, wxLIST_AUTOSIZE);
		callStackList->SetColumnWidth(1, wxLIST_AUTOSIZE);
		callStackList->SetColumnWidth(2, wxLIST_AUTOSIZE);
		callStackList->SetColumnWidth(3, wxLIST_AUTOSIZE);
	}
	
	if (onRunToLineAddress) {
		sprintf(sendBuffer, "clear *0x%llX", onRunToLineAddress);
		onRunToLineAddress = 0;
		QueryGDB();
	}

	{
		struct LocalVariable {
			uint64_t offset /* negative from rbp */, size;
			const char *name;
			size_t nameLength;
		};

		uint32_t offset = 0;
		char match[256];
		LocalVariable *variables = nullptr;
		const char *firstDot;
		Symbol symbol;
		uint64_t rbp;

		localsList->DeleteAllItems();

		uint64_t currentAddress = AddressFromLine(currentFile, oldLine);
		if (!currentAddress) goto gotLocals;
		rbp = shget(currentRegisters, "rbp").asInteger;
		if (!rbp) goto gotLocals;
		symbol = SymbolFromAddress(currentAddress);
		if (!symbol.address) goto gotLocals;
		firstDot = strchr(symbol.name, '.');
		if (firstDot) symbol.nameLength = firstDot - symbol.name;
		snprintf(match, sizeof(match), "%.*s._Local_", symbol.nameLength, symbol.name);

		while (offset < header->symbolTableLength) {
			FASSymbol *symbol = (FASSymbol *) (_buffer + header->symbolTableOffset + offset);

			const char *name;
			size_t nameLength;

			if (symbol->nameType == FAS_NAME_IN_PREPROCESSED_SOURCE) {
				name = _buffer + header->preprocessedSourceOffset + symbol->nameData + 1;
				nameLength = (uint8_t) _buffer[header->preprocessedSourceOffset + symbol->nameData];
			} else /* if (symbol->nameType == FAS_NAME_IN_STRING_TABLE) */ {
				name = LookupString(symbol->nameData);
				nameLength = strlen(name);
			}

			if (0 == memcmp(match, name, strlen(match))) {
				LocalVariable variable = {};
				variable.offset = variable.size = symbol->value;
				variable.name = name + strlen(match);
				variable.nameLength = nameLength - strlen(match);
				arrput(variables, variable);
			}

			offset += sizeof(FASSymbol);
		}

		if (!arrlenu(variables)) goto gotLocals;

		for (uintptr_t i = 0; i < arrlenu(variables) - 1; i++) {
			variables[i].size = variables[i].offset - variables[i + 1].offset;
		}

		for (uintptr_t i = 0; i < arrlenu(variables); i++) {
			char c = 'b';
			bool w = false;
			if (variables[i].size == 1) c = 'b';
			else if (variables[i].size == 2) c = 'h';
			else if (variables[i].size == 4) c = 'w';
			else if (variables[i].size == 8) c = 'g';
			else w = true;
			sprintf(sendBuffer, "x/%d%cx 0x%llX", w ? variables[i].size : 1, c, rbp - variables[i].offset);
			QueryGDB();
			if (strstr(receiveBuffer, "Cannot")) continue;
			char *result = strchr(receiveBuffer, ':');
			if (!result) continue;
			char *end = strstr(result, "(gdb)");
			if (!end) continue;
			*end = 0;
			result++;
			localsList->InsertItem(i, wxString(variables[i].name, variables[i].nameLength));
			localsList->SetItem(i, 2, result);
			localsList->SetItem(i, 3, wxString::Format(wxT("0x%llX"), rbp - variables[i].offset));
			if (w) continue;
			sprintf(sendBuffer, "x/%d%cd 0x%llX", w ? variables[i].size : 1, c, rbp - variables[i].offset);
			QueryGDB();
			if (strstr(receiveBuffer, "Cannot")) continue;
			result = strchr(receiveBuffer, ':');
			if (!result) continue;
			end = strstr(result, "(gdb)");
			if (!end) continue;
			*end = 0;
			result++;
			localsList->SetItem(i, 1, result);
		}

		localsList->SetColumnWidth(0, 100);
		localsList->SetColumnWidth(1, 100);
		localsList->SetColumnWidth(2, wxLIST_AUTOSIZE);
		gotLocals:;
		arrfree(variables);
	}
}

void MyFrame::OnReceiveData(wxCommandEvent &event) {
	const char *data = receiveBuffer;
	// printf("receive: %s\n", data);
	console->SetReadOnly(false);
	console->InsertText(console->GetTextLength(), wxString(data));
	console->ScrollToEnd();
	console->SetReadOnly(true);
	Update();
}

void CodeDisplay::OnRunToLine(wxCommandEvent &event) {
	int line = LineFromPosition(GetCurrentPos()) + 1;
	if (line == oldLine) return;
	
	uint64_t address = AddressFromLine(currentFile, line);

	if (!address) {
		wxMessageBox(wxT("Could not find a corresponding address for the line."), wxT("Error"), wxICON_INFORMATION);
		return;
	}

	onRunToLineAddress = address;
	sprintf(sendBuffer, "break *0x%llX\nc", address);
	MarginSetStyle(line - 1, wxSTC_STYLE_MAX - 2);
	frame->SendToGDB(sendBuffer);
}

void CodeDisplay::OnKillFocus(wxFocusEvent &event) {
	// HACK
	// To avoid relying on the newest versions of wxWidgets,
	// we don't tell the wxStyledTextCtrl about losing focus,
	// so we don't need SetCaretLineVisibleAlways().
}

void CodeDisplay::OnJumpToLine(wxCommandEvent &event) {
	int line = LineFromPosition(GetCurrentPos()) + 1;
	if (line == oldLine) return;

	uint64_t address = AddressFromLine(currentFile, line);

	if (!address) {
		wxMessageBox(wxT("Could not find a corresponding address for the line."), wxT("Error"), wxICON_INFORMATION);
		return;
	}

	sprintf(sendBuffer, "set $rip=0x%llX", address);
	frame->SendToGDB(sendBuffer);
}

void CodeDisplay::OnLeftUp(wxMouseEvent &event) {
	if (event.ControlDown()) {
		wxCommandEvent e2(RUN_TO_LINE); 
		wxPostEvent(this, e2); 
	} else if (event.AltDown()) {
		wxCommandEvent e2(JUMP_TO_LINE); 
		wxPostEvent(this, e2); 
	} 
	
	event.Skip();
}

void MyTextCtrl::OnTabComplete(wxKeyEvent &event) {
	if (event.GetKeyCode() == WXK_TAB) {
		wxString string = GetLineText(0);
		const char *data = string.mb_str();
		sprintf(sendBuffer, "complete %s\n", data);
		QueryGDB();
		const char *end = strchr(receiveBuffer, '\n');

		if (end) {
			frame->consoleInput->Clear();
			frame->consoleInput->AppendText(wxString(receiveBuffer, end - receiveBuffer));
		}
	} else if (event.GetKeyCode() == WXK_UP) {
		if (commandHistoryIndex != 256 - 1) {
			Clear();
			AppendText(commandHistory[++commandHistoryIndex]);
		}
	} else if (event.GetKeyCode() == WXK_DOWN) {
		Clear();

		if (commandHistoryIndex > 1) {
			AppendText(commandHistory[--commandHistoryIndex]);
		}
	} else {
		event.Skip();
	}
}

void MyFrame::ToggleBreakpoint(int line) {
	const char *fileName = currentFile;
	uint64_t address = AddressFromLine(fileName, line);
	if (!address) {
		wxMessageBox(wxT("Could not find a corresponding address for the line."), wxT("Error"), wxICON_INFORMATION);
		return;
	}

	for (uintptr_t i = 0; i < arrlenu(breakpoints); i++) {
		if (breakpoints[i].address == address) {
			display->MarginSetStyle(breakpoints[i].line - 1, wxSTC_STYLE_LINENUMBER);
			arrdel(breakpoints, i);
			doNotUpdatePositionOnce = true;
			sprintf(sendBuffer, "clear *0x%llx", address);
			SendToGDB(sendBuffer, false);
			breakpointList->DeleteItem(i);
			return;
		}
	}

	Breakpoint breakpoint = { .address = address, .line = line };
	strcpy(breakpoint.file, fileName);
	arrput(breakpoints, breakpoint);
	doNotUpdatePositionOnce = true;
	sprintf(sendBuffer, "b *0x%llx", address);
	SendToGDB(sendBuffer, false);
	uintptr_t index = arrlenu(breakpoints) - 1;
	breakpointList->InsertItem(index, fileName);
	breakpointList->SetItem(index, 1, wxString::Format(wxT("%i"), line));
	breakpointList->SetItem(index, 2, wxString::Format(wxT("0x%llX"), address));
	breakpointList->SetColumnWidth(0, wxLIST_AUTOSIZE);
}

void MyFrame::OnMarginClick(wxStyledTextEvent &event) {
	ToggleBreakpoint(display->LineFromPosition(event.GetPosition()) + 1);
}

void MyFrame::AddBreakpointH(wxCommandEvent &event) {
	display->SetCaretLineVisible(true);
	ToggleBreakpoint(display->LineFromPosition(display->GetSelectionStart()) + 1);
}

void MyFrame::RestartGDB(wxCommandEvent &event) {
	kill(gdbPID, SIGKILL);
	pthread_cancel(gdbThread); // TODO Is there a nicer way to do this?
	breakpointList->DeleteAllItems();
	initialisedGDB = false;
	StartGDBThread();
}

void MyFrame::OnEnterInput(wxCommandEvent &event) {
	wxString string = consoleInput->GetLineText(0);
	const char *data = string.mb_str();
	char newline = '\n';
	console->SetReadOnly(false);
	console->InsertText(console->GetTextLength(), wxString(data));
	console->InsertText(console->GetTextLength(), wxString("\n"));
	console->ScrollToEnd();
	console->SetReadOnly(true);

	if (!strlen(data)) {
		write(pipeToGDB, commandHistory[0], strlen(commandHistory[0]));
	} else {
		write(pipeToGDB, data, strlen(data));

		if (strcmp(commandHistory[0], data)) {
			strcpy(commandHistory[0], data);
			memmove(commandHistory[1], commandHistory[0], sizeof(commandHistory) - sizeof(commandHistory[0]));
		}
	}

	commandHistoryIndex = 0;

	focusInput = true;
	write(pipeToGDB, &newline, 1);
	consoleInput->Clear();
}

void MyFrame::RemoveItem(wxCommandEvent &event) {
	if (breakpointList->HasFocus()) {
		long index = breakpointList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

		if (index >= 0 && index < arrlen(breakpoints)) {
			display->MarginSetStyle(breakpoints[index].line - 1, wxSTC_STYLE_LINENUMBER);
			arrdel(breakpoints, index);
			sprintf(sendBuffer, "clear *0x%llx", breakpoints[index].address);
			SendToGDB(sendBuffer, false);
			breakpointList->DeleteItem(index);
		}
	}
}

void MyFrame::SelectMemory(wxCommandEvent &event) {
	wxTextEntryDialog dialog(frame, wxT("Enter the new address:"), wxT("Memory View"));
	int result = dialog.ShowModal();
	if (result == wxID_CANCEL) return;

	PushAddress();
	wxString string = dialog.GetValue();
	memoryAddress = strtoul(string.mb_str(), nullptr, 0);
	viewingStack = false;
	Update();
}

uint64_t MyFrame::GetSelectedAddress() {
	if (breakpointList->HasFocus()) {
		long index = breakpointList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

		if (index >= 0 && index < arrlen(breakpoints)) {
			return breakpoints[index].address;
		}
	} else if (registerList->HasFocus()) {
		long index = registerList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

		if (index >= 0 && index < arrlen(currentRegistersAsArray)) {
			return currentRegistersAsArray[index].asInteger;
		}
	} else if (callStackList->HasFocus()) {
		long index = callStackList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

		if (index >= 0 && index < arrlen(currentCallStack)) {
			return currentCallStack[index];
		}
	} else if (localsList->HasFocus()) {
		long index = localsList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

		if (index >= 0) {
			wxString string = localsList->GetItemText(index, 3);
			return strtoul(string, nullptr, 0);
		}
	} else if (memoryView->HasFocus()) {
		int wordStart = memoryView->GetSelectionStart();
		int wordEnd = memoryView->GetSelectionEnd();
		wxCharBuffer address = memoryView->GetTextRangeRaw(wordStart, wordEnd);
		return strtoul(address.data(), nullptr, 16);
	} else if (console->HasFocus()) {
		int wordStart = console->GetSelectionStart();
		int wordEnd = console->GetSelectionEnd();
		if (wordStart == wordEnd) return -1;
		wxCharBuffer address = console->GetTextRangeRaw(wordStart, wordEnd);

		if (isalpha(address.data()[0])) {
			char symbol[256];
			snprintf(symbol, sizeof(symbol), "%.*s", address.length(), address.data());
			return AddressFromSymbol(symbol);
		}

		return strtoul(address.data(), nullptr, 0);
	} else if (programOutput->HasFocus()) {
		int wordStart = programOutput->GetSelectionStart();
		int wordEnd = programOutput->GetSelectionEnd();
		if (wordStart == wordEnd) return -1;
		wxCharBuffer address = programOutput->GetTextRangeRaw(wordStart, wordEnd);

		if (isalpha(address.data()[0])) {
			char symbol[256];
			snprintf(symbol, sizeof(symbol), "%.*s", address.length(), address.data());
			return AddressFromSymbol(symbol);
		}

		return strtoul(address.data(), nullptr, 0);
	} else if (display->HasFocus()) {
		int wordStart = display->GetSelectionStart();
		int wordEnd = display->GetSelectionEnd();
		if (wordStart == wordEnd) return -1;
		wxCharBuffer address = display->GetTextRangeRaw(wordStart, wordEnd);

		if (isalpha(address.data()[0])) {
			char symbol[256];
			snprintf(symbol, sizeof(symbol), "%.*s", address.length(), address.data());
			return AddressFromSymbol(symbol);
		}

		return strtoul(address.data(), nullptr, 0);
	}

	return -1;
}

void MyFrame::AddressToMemory(wxCommandEvent &event) {
	uint64_t address = GetSelectedAddress();

	if (address != (uint64_t) -1) {
		PushAddress();
		memoryAddress = address;
		viewingStack = false;
		arrfree(structFields);
		Update();
	} else {
		wxMessageBox(wxT("No address selected."), wxT("Error"), wxICON_INFORMATION);
	}
}

void MyFrame::AddressToSource(wxCommandEvent &event) {
	uint64_t address = GetSelectedAddress();
	PreprocessedLine line;

	if (LineFromAddress(address, &line, true, true)) {
		LoadFile(line.name, line.nameLength);
		SetLine(line.lineNumber, false);
	} else {
		wxMessageBox(wxT("Could not find a corresponding line for the address."), wxT("Error"), wxICON_INFORMATION);
	}
}

void LoadStructFields() {
	arrfree(structFields);

	uint32_t offset = 0;
	size_t structLength = 0;

	while (offset < header->symbolTableLength) {
		FASSymbol *symbol = (FASSymbol *) (_buffer + header->symbolTableOffset + offset);

		const char *name;
		size_t nameLength;

		if (symbol->nameType == FAS_NAME_IN_PREPROCESSED_SOURCE) {
			name = _buffer + header->preprocessedSourceOffset + symbol->nameData + 1;
			nameLength = (uint8_t) _buffer[header->preprocessedSourceOffset + symbol->nameData];
		} else /* if (symbol->nameType == FAS_NAME_IN_STRING_TABLE) */ {
			name = LookupString(symbol->nameData);
			nameLength = strlen(name);
		}

		if (nameLength > strlen(structName) && 0 == memcmp(structName, name, strlen(structName))) {
			for (int i = 0; i < arrlen(structFields); i++) {
				if (structFields[i].offset == symbol->value) {
					goto nextSymbol;
				}
			}

			// printf("'%.*s': 0x%llX\n", nameLength, name, symbol->value);
			StructField field = {};
			field.offset = symbol->value;
			field.name = name + strlen(structName);
			field.nameLength = nameLength - strlen(structName);
			arrput(structFields, field);
		} else if (0 == memcmp(name, "sizeof.", 7) && 0 == memcmp(name + 7, structName, strlen(structName)) && nameLength == strlen(structName) + 7) {
			structLength = symbol->value;
			// printf("sizeof: %d\n", structLength);
		}

		nextSymbol:;
			   offset += sizeof(FASSymbol);
	}

	qsort(structFields, arrlen(structFields), sizeof(StructField), [] (const void *a, const void *b) {
			StructField *left = (StructField *) a, *right = (StructField *) b;
			return left->offset - right->offset;
			});

	for (uintptr_t i = 0; i < arrlenu(structFields); i++) {
		if (i == arrlenu(structFields) - 1) {
			structFields[i].bytes = structLength - structFields[i].offset;
		} else {
			structFields[i].bytes = structFields[i + 1].offset - structFields[i].offset;
		}
	}
}

void MyFrame::FormatAsStruct(wxCommandEvent &event) {
	wxTextEntryDialog dialog(frame, wxT("Enter the struct name:"), wxT("Memory View"));
	int result = dialog.ShowModal();
	if (result == wxID_CANCEL) return;
	wxString string = dialog.GetValue();
	const char *_structName = string.mb_str();
	strncpy(structName, _structName, sizeof(structName) - 1);
	LoadStructFields();
	Update();
}

void MyFrame::PushAddress() {
	PreviousMemoryAddress previous = {};
	previous.address = memoryAddress;
	previous.viewingStack = viewingStack;
	if (structFields) strcpy(previous.structName, structName);
	previous.memoryWordSize = memoryWordSize;
	arrput(previousMemoryAddresses, previous);
	if (arrlen(previousMemoryAddresses) > 100) arrdel(previousMemoryAddresses, 0);
}

void MyFrame::PopAddress(wxCommandEvent &event) {
	if (!arrlen(previousMemoryAddresses)) return;
	arrfree(structFields);
	PreviousMemoryAddress previous = arrpop(previousMemoryAddresses);
	memoryAddress = previous.address;
	memoryWordSize = previous.memoryWordSize;
	viewingStack = previous.viewingStack;
	strcpy(structName, previous.structName);
	if (structName[0]) LoadStructFields();
	Update();
}
