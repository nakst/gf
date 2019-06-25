// Compile with "g++ -o gf gf.cpp `wx-config --cxxflags --libs all`"
//
// Bugs:
// 	- Watch expressions with inline structures/unions confuse the parser.
// 	- Sometimes it can't find the source file.
// 	- Relying on wxWidgets 3 is bad; most Linux distributions don't support it.
//
// Future extensions: 
// 	- Watch window.
// 	- Memory window.
// 	- Disassembly.
// 	- Hover to view value.
// 	- Thread selection.
// 	- Data breakpoint viewer.
//	- Track number of times each line in a function is executed.
//	- Track variable modifications including call stack history and watch expressions.

#include <wx/wx.h>
#include <wx/stc/stc.h>
#include <wx/splitter.h>
#include <wx/listctrl.h>
#include <wx/dataview.h>
#include <wx/thread.h>
#include <wx/notebook.h>
#include <spawn.h>

// Implementations are not required to declare this variable.
extern "C" char **environ;

class MyApp : public wxApp {
	public:
		virtual bool OnInit();
		virtual int OnExit();
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
		wxDECLARE_EVENT_TABLE();
};

struct DataNodeEntry {
	// TODO Make sure these arrays aren't exceeded.
	char name[256];
	char value[256];
	uint64_t address;
	bool isContainer, valueChanged, isPointer, justAdded, isVisible, isCached;
};

struct DataNode {
	int x, y;
	char name[64];
	char prefix[1024];
	DataNodeEntry *entries;
	size_t entryCount;
	uint64_t address;
	bool isPointer;
	int id;

#define DATA_NODE_GLOBALS (1)
#define DATA_NODE_STRUCT  (2)
#define DATA_NODE_WATCH   (3)
	int type;
#define DATA_NODE_BUTTON_ADD          (1 << 0)
#define DATA_NODE_BUTTON_LOCK_ADDRESS (1 << 1)
#define DATA_NODE_BUTTON_CLOSE	      (1 << 2)
	int visibleButtons;
};

wxFont font(14, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
wxFont font2(11, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
// wxPen connectionPen(*wxGREEN, 3);
wxPen connectionPenUnlocked(*wxYELLOW, 3);

// The current window and application.
struct MyFrame *frame;
struct MyApp *myApp;

// GDB process.
volatile int pipeToGDB;
volatile pid_t gdbPID;
volatile pthread_t gdbThread;

class MyFrame : public wxFrame {
	public:
		MyFrame(const wxString &title, const wxPoint &position, const wxSize &size);
		void SendToGDB(const char *string, bool echo = true);

		wxStyledTextCtrl *console = nullptr;
		struct CodeDisplay *display = nullptr;
		struct MyTextCtrl *consoleInput = nullptr;
		wxListCtrl *breakpointList = nullptr, *callStackList = nullptr, *functionList = nullptr;
		wxDataViewColumn *watchExpressionColumn = nullptr;
		struct DataView *dataView = nullptr;
		const void *rightClickData = nullptr;

	private:
		void OnExit(wxCommandEvent &event);
		void OnMarginClick(wxStyledTextEvent &event);
		void OnEnterInput(wxCommandEvent &event);
		void OnReceiveData(wxCommandEvent &event);
		void OnSelectFrame(wxListEvent &event);
		void OnFunctionListRightClick(wxListEvent &event);
		void OnSelectFunction(wxListEvent &event);

		void FocusInput		(wxCommandEvent &event) { consoleInput->SetFocus(); }

		void StepIn		(wxCommandEvent &event) { SendToGDB("s"); }
		void StepOver		(wxCommandEvent &event) { SendToGDB("n"); }
		void StepOut		(wxCommandEvent &event) { SendToGDB("finish"); }
		void Continue		(wxCommandEvent &event) { SendToGDB("c"); }
		void Connect		(wxCommandEvent &event) { SendToGDB("target remote :1234"); }
		void Run		(wxCommandEvent &event) { SendToGDB("r"); }
		void RunPaused		(wxCommandEvent &event) { SendToGDB("start"); }
		void Break		(wxCommandEvent &event) { kill(gdbPID, SIGINT); }

		void RemoveItem				(wxCommandEvent &event);
		void AddBreakpointH			(wxCommandEvent &event);
		void RestartGDB				(wxCommandEvent &event);
		void BreakAtFunction			(wxCommandEvent &event);

		bool LoadFile(char *newFile);
		void SetLine(int line);
		void AddBreakpoint(int index);
		void RemoveBreakpoint(int index);

		wxDECLARE_EVENT_TABLE();
};

class DataView : public wxControl {
	public:
		DataView() {
			nodeCount = 1;
			nodeDrag = -1;
			nodes = (DataNode *) calloc(nodeCount, sizeof(DataNode));

			nodes[0].x = nodes[0].y = 20;
			nodes[0].type = DATA_NODE_WATCH;
			nodes[0].visibleButtons = DATA_NODE_BUTTON_ADD;
			strcpy(nodes[0].name, "Watch");
		}

		DataNode *nodes;
		size_t nodeCount;
		ptrdiff_t nodeDrag;
		bool panning;
		int dragOffsetX, dragOffsetY;
		void UpdateNodes(bool newlyVisibleOnly = false);

	protected:
		virtual wxSize DoGetBestSize() const {
			return wxSize(200, 300);
		}

#define NODE_WIDTH (400)

		DataNode *AddNode() {
			nodes = (DataNode *) realloc(nodes, ++nodeCount * sizeof(DataNode));
			DataNode *newNode = nodes + nodeCount - 1;
			memset(newNode, 0, sizeof(DataNode));
			static int nextNodeID = 1;
			newNode->id = nextNodeID++;
			return newNode;
		}

		void DrawNode(wxDC &dc, DataNode *node) {
			int x = node->x, y = node->y, 
			    width = NODE_WIDTH, height = 32 + 16 * node->entryCount; // NOTE Duplicated in UpdateNodes().
			int parentWidth, parentHeight;
			GetClientSize(&parentWidth, &parentHeight);
			bool visible = true;

			if (x + width < 0 || y + height < 0 || x > parentWidth || y > parentHeight) {
				visible = false;
			}

			if (visible) {
				dc.SetBrush(*wxMEDIUM_GREY_BRUSH);
				dc.SetPen(wxNullPen);
				dc.DrawRectangle(x + 2, y + 2, width, height);
				dc.SetBrush(*wxWHITE_BRUSH);
				dc.SetPen(*wxBLACK_PEN);
				dc.DrawRectangle(x, y, width, height);
				dc.SetBrush(*wxLIGHT_GREY_BRUSH);
				dc.DrawRectangle(x, y, width, 20);

				dc.SetTextForeground(*wxBLACK);
				dc.SetFont(*wxNORMAL_FONT);
				char buffer[64];
				sprintf(buffer, "0x%lx", node->address);
				dc.DrawText(node->name, x + 8, y + 2);
				if (node->type == DATA_NODE_STRUCT) dc.DrawText(buffer, x + width / 2, y + 2);

				dc.SetFont(font2);
				dc.SetPen(*wxBLACK_PEN);

				for (int i = 0; i < 3; i++) {
					if (node->visibleButtons & (1 << i)) {
						int bx = x + width - 52 + 17 * i;
						const wxBrush *brushes[] = { wxCYAN_BRUSH, wxYELLOW_BRUSH, wxRED_BRUSH };
						dc.SetBrush(*(brushes[i]));
						dc.DrawRectangle(bx, y + 2, 16, 16);
						const char *buttons[] = { "+", "&", "X" };
						dc.DrawText(buttons[i], bx + 4, y + 1);
					}
				}

				dc.SetBrush(*wxCYAN_BRUSH);
			}

			for (uintptr_t i = 0; i < node->entryCount; i++) {
				DataNodeEntry *entry = node->entries + i;

				int yp = y + 24 + i * 16;

				if (visible && yp + 20 > 0 && yp < parentHeight) {
					dc.SetPen(wxNullPen);
					if (entry->valueChanged) dc.DrawRectangle(x + 1, yp + 1, width - 2, 16);
					dc.DrawText(entry->name, x + 8, yp);
					dc.DrawText(entry->value, x + width / 2, yp);
				}

				if (entry->address && entry->isContainer) {
					for (uintptr_t i = 0; i < nodeCount; i++) {
						DataNode *connectedNode = nodes + i;

						// TODO Since the addresses of invisible nodes/entries aren't updated, should we always draw these..?

						if (entry->address == connectedNode->address && connectedNode != node) {
							dc.SetPen(connectionPenUnlocked);
							dc.DrawLine(x + width, yp + 8, connectedNode->x, connectedNode->y);
						}
					}
				}
			}
		}

		void ParseFields(DataNode *node, DataNode *newNode, DataNodeEntry *entry);

		void OnClick(wxMouseEvent &event) {
			// TODO Allow the data view to take keyboard focus?
			// event.Skip();

			int mx = event.GetX(), my = event.GetY();

			for (uintptr_t nodeIndex = nodeCount; nodeIndex; ) {
				nodeIndex--;

				// TODO Skip non-visible nodes.

				DataNode *node = nodes + nodeIndex;

				for (int i = 0; i < 3; i++) {
					if (node->visibleButtons & (1 << i)) {
						int left = node->x + NODE_WIDTH - 52 + 17 * i, right = left + 16,
						    top = node->y + 2, bottom = top + 16;

						if (mx > left && mx < right && my > top && my < bottom) {
							if ((1 << i) == DATA_NODE_BUTTON_ADD) {
								wxTextEntryDialog dialog(frame, wxT("Enter the watch expression:"), wxT("Add Watch Expression"));
								int result = dialog.ShowModal();
								if (result == wxID_CANCEL) goto done;
								node->entries = (DataNodeEntry *) realloc(node->entries, ++node->entryCount * sizeof(DataNodeEntry));
								DataNodeEntry *newEntry = node->entries + node->entryCount - 1;
								memset(newEntry, 0, sizeof(DataNodeEntry));
								snprintf(newEntry->name, 256, "(%s)", (const char *) dialog.GetValue().mb_str());
								UpdateNodes();
							} else if ((1 << i) == DATA_NODE_BUTTON_LOCK_ADDRESS) {
							} else if ((1 << i) == DATA_NODE_BUTTON_CLOSE) {
								free(nodes[nodeIndex].entries);
								memmove(nodes + nodeIndex, nodes + nodeIndex + 1, (nodeCount - nodeIndex - 1) * sizeof(DataNode));
								nodeCount--;
							}

							goto done;
						}
					}
				}

				if (my >= node->y && my <= node->y + 20 && mx > node->x && mx < node->x + NODE_WIDTH) {
					DataNode _node = *node;
					memmove(nodes + nodeIndex, nodes + nodeIndex + 1, (nodeCount - nodeIndex - 1) * sizeof(DataNode));
					nodes[nodeCount - 1] = _node;
					nodeDrag = nodeCount - 1, node = nodes + nodeDrag;
					dragOffsetX = mx - node->x, dragOffsetY = my - node->y;
					goto done;
				}

				for (uintptr_t i = 0; i < node->entryCount; i++) {
					DataNodeEntry *entry = node->entries + i;

					if (entry->isContainer) {
						int left = node->x, right = node->x + NODE_WIDTH,
						    top = node->y + 24 + i * 16, bottom = node->y + 24 + (i + 1) * 16;

						if (mx > left && mx < right && my > top && my < bottom) {
							DataNode *newNode = AddNode();
							node = nodes + nodeIndex;
							newNode->x = node->x + NODE_WIDTH + 32, newNode->y = node->y;
							newNode->type = DATA_NODE_STRUCT;
							newNode->isPointer = entry->isPointer;
							newNode->visibleButtons = DATA_NODE_BUTTON_LOCK_ADDRESS | DATA_NODE_BUTTON_CLOSE;
							snprintf(newNode->prefix, 1024, "%s %s", node->prefix, entry->name);
							snprintf(newNode->name, 256, "%s", entry->name);
							ParseFields(node, newNode, entry);
							goto done;
						}
					}
				}
			}

			done:;
			Refresh();
		}

		void LeftReleased(wxMouseEvent &event) {
			nodeDrag = -1;
		}

		void PanStart(wxMouseEvent &event) {
			dragOffsetX = event.GetX();
			dragOffsetY = event.GetY();
			panning = true;
		}

		void PanEnd(wxMouseEvent &event) {
			panning = false;
		}

		void MouseMoved(wxMouseEvent &event) {
			int mx = event.GetX(), my = event.GetY();

			if (nodeDrag != -1) {
				DataNode *node = nodes + nodeDrag;
				node->x = mx - dragOffsetX;
				node->y = my - dragOffsetY;
				UpdateNodes(true);
				Refresh();
			} else if (panning) {
				for (uintptr_t i = 0; i < nodeCount; i++) {
					nodes[i].x += mx - dragOffsetX;
					nodes[i].y += my - dragOffsetY;
				}

				dragOffsetX = mx;
				dragOffsetY = my;
				UpdateNodes(true);
				Refresh();
			}
		}

		void OnPaint(wxPaintEvent &) {
			int width, height;
			GetClientSize(&width, &height);

			wxPaintDC dc(this);
			dc.SetBrush(*wxGREY_BRUSH);
			dc.SetPen(wxNullPen);
			dc.DrawRectangle(0, 0, width, height);

			for (uintptr_t i = 0; i < nodeCount; i++) {
				DrawNode(dc, nodes + i);
			}
		}

	private:
		wxDECLARE_EVENT_TABLE();
};

wxBEGIN_EVENT_TABLE(DataView, wxControl)
	EVT_PAINT(DataView::OnPaint)
	EVT_LEFT_DOWN(DataView::OnClick)
	EVT_LEFT_UP(DataView::LeftReleased)
	EVT_MIDDLE_DOWN(DataView::PanStart)
	EVT_MIDDLE_UP(DataView::PanEnd)
	EVT_MOTION(DataView::MouseMoved)
wxEND_EVENT_TABLE()

struct Breakpoint {
	char filename[128]; // TODO Check this isn't exceeded.
	int line;
	bool seen;
};

// Buffer for sending commands to GDB.
char sendBuffer[4096];

// The current file.
char *currentFile = nullptr;
int64_t currentFileModified;

// Breakpoints and run-to-line.
Breakpoint breakpoints[128]; // TODO Check this isn't exceeded.
int breakpointCount, resetBreakpoints;
int oldLine, untilLine, jumpLine;

// How to parse the input received from GDB.
#define MODE_NORMAL       		(0)
#define MODE_TAB_COMPLETE 		(1)
#define MODE_GET_BREAKPOINTS		(2)
#define MODE_JUMP_TO_LINE		(3)
#define MODE_GET_CALL_STACK		(4)
#define MODE_PRINT_EXPRESSION		(5)
#define MODE_BREAK_ON_WRITE		(6)
#define MODE_LIST_FUNCTIONS		(7)
#define MODE_FIND_FUNCTION		(8)
volatile int mode;

// Buffer up input from gdb until we get the "(gdb)" prompt.
#define RECEIVE_BUFFER_SIZE (4194304)
char receiveBuffer[RECEIVE_BUFFER_SIZE]; // TODO Check this isn't exceeded.
bool newReceive = true;
wxMutex printExpressionMutex;
wxCondition printExpressionEvent(printExpressionMutex);

// Command history.
char commandHistory[256][1024]; // TODO Check this isn't exceeded.
int commandHistoryIndex;
volatile bool focusInput;

// Symbol files.
char symbolFiles[256][1024]; // TODO Check this isn't exceeded.
size_t symbolFileCount;

// Startup commands.
const char *startCommands[] = { "set print array-indexes", };
volatile size_t started = 0;
volatile size_t reloadSymbolFiles;

void QueryGDB();

void QueryGDB() {
	mode = MODE_PRINT_EXPRESSION;
	printExpressionMutex.Lock();
	frame->SendToGDB(sendBuffer, false);
	printExpressionEvent.Wait();
	printExpressionMutex.Unlock();
	mode = MODE_NORMAL;
}

enum {
	ID_SourceDisplay 	= 1,
	ID_ConsoleInput 	= 2,
	ID_ConsoleOutput 	= 3,
	ID_BreakpointList	= 4,
	ID_WatchList		= 5,
	ID_CallStackList	= 6,
	ID_ViewBreakpoints	= 7,
	ID_ViewCallStack	= 8,
	ID_ViewWatch		= 9,
	ID_FocusInput		= 10,
	ID_AddWatchExpression	= 11,
	ID_AddFields		= 12,
	ID_AddBreakpoint	= 13,
	ID_BreakOnWrite		= 14,
	ID_RestartGDB		= 15,
	ID_FunctionList		= 16,
	ID_BreakAtFunction	= 17,

	ID_StepIn 		= 101,
	ID_StepOver 		= 102,
	ID_Continue 		= 103,
	ID_Break 		= 104,
	ID_StepOut		= 105,
	ID_Run			= 106,
	ID_RunPaused		= 107,
	ID_Connect 		= 108,
};

wxDEFINE_EVENT(RECEIVE_GDB_DATA, wxCommandEvent);
wxDEFINE_EVENT(RUN_TO_LINE, wxCommandEvent);
wxDEFINE_EVENT(JUMP_TO_LINE, wxCommandEvent);

wxBEGIN_EVENT_TABLE(MyFrame, wxFrame)
	EVT_MENU(wxID_EXIT, MyFrame::OnExit)
	EVT_STC_MARGINCLICK(ID_SourceDisplay, MyFrame::OnMarginClick)
	EVT_TEXT_ENTER(ID_ConsoleInput, MyFrame::OnEnterInput)
	EVT_LIST_ITEM_SELECTED(ID_CallStackList, MyFrame::OnSelectFrame)
	EVT_COMMAND(wxID_ANY, RECEIVE_GDB_DATA, MyFrame::OnReceiveData)
	EVT_LIST_ITEM_RIGHT_CLICK(ID_FunctionList, MyFrame::OnFunctionListRightClick)
	EVT_LIST_ITEM_SELECTED(ID_FunctionList, MyFrame::OnSelectFunction)

	EVT_MENU(		ID_StepIn,		MyFrame::StepIn)
	EVT_MENU(		ID_StepOver,		MyFrame::StepOver)
	EVT_MENU(		ID_StepOut,		MyFrame::StepOut)
	EVT_MENU(		ID_Continue,		MyFrame::Continue)
	EVT_MENU(		ID_Connect,		MyFrame::Connect)
	EVT_MENU(		ID_Break,		MyFrame::Break)
	EVT_MENU(		ID_Run,			MyFrame::Run)
	EVT_MENU(		ID_RunPaused,		MyFrame::RunPaused)
	EVT_MENU(		ID_FocusInput,		MyFrame::FocusInput)
	EVT_MENU(		ID_AddBreakpoint,	MyFrame::AddBreakpointH)
	EVT_MENU(		ID_RestartGDB,		MyFrame::RestartGDB)
	EVT_MENU(		ID_BreakAtFunction,	MyFrame::BreakAtFunction)
	EVT_MENU(		wxID_DELETE,		MyFrame::RemoveItem)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(MyTextCtrl, wxTextCtrl)
	EVT_CHAR(MyTextCtrl::OnTabComplete)
wxEND_EVENT_TABLE()

wxBEGIN_EVENT_TABLE(CodeDisplay, wxStyledTextCtrl)
	EVT_LEFT_UP(CodeDisplay::OnLeftUp)
	EVT_COMMAND(wxID_ANY, RUN_TO_LINE, CodeDisplay::OnRunToLine)
	EVT_COMMAND(wxID_ANY, JUMP_TO_LINE, CodeDisplay::OnJumpToLine)
wxEND_EVENT_TABLE()

wxIMPLEMENT_APP(MyApp);

void *DebuggerThread(void *_frame) {
	MyFrame *frame = (MyFrame *) _frame;
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

		if (mode == MODE_PRINT_EXPRESSION) {
			printExpressionMutex.Lock();
			printExpressionEvent.Broadcast();
			printExpressionMutex.Unlock();
			// TODO Can we safely assume we won't get anymore input until the next SendToGDB?
		} else {
			wxCommandEvent event(RECEIVE_GDB_DATA); 
			wxPostEvent(frame, event); 
		}
	}
}

void StartGDBThread() {
	pthread_t debuggerThread;
	pthread_attr_t attributes;
	pthread_attr_init(&attributes);
	pthread_create(&debuggerThread, &attributes, DebuggerThread, frame);
	gdbThread = debuggerThread;
}

bool MyApp::OnInit() {
	myApp = this;
	frame = new MyFrame("gdb frontend", wxPoint(50, 50), wxSize(1024, 768));
	StartGDBThread();
	frame->Show(true);
	return true;
}

int MyApp::OnExit() {
	const char *exit = "q\n";
	write(pipeToGDB, exit, strlen(exit));
	return wxApp::OnExit();
}

MyTextCtrl ::MyTextCtrl (wxWindow *parent, wxWindowID id) : wxTextCtrl      (parent, id) {}
CodeDisplay::CodeDisplay(wxWindow *parent, wxWindowID id) : wxStyledTextCtrl(parent, id) {}

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

MyFrame::MyFrame(const wxString &title, const wxPoint &position, const wxSize &size) : wxFrame(nullptr, wxID_ANY, title, position, size) {
	currentFile = strdup("");

	wxPanel *panel = new wxPanel(this, wxID_ANY);
	wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
	panel->SetSizer(sizer);
	wxTextAttr textAttributes = wxTextAttr(*wxWHITE, *wxBLACK, font);

	wxSplitterWindow *splitter1 = new wxSplitterWindow(panel);
	wxSplitterWindow *splitter2 = new wxSplitterWindow(splitter1);
	wxSplitterWindow *splitter3 = new wxSplitterWindow(splitter2);
	wxSplitterWindow *splitter4 = new wxSplitterWindow(splitter1, wxID_ANY, wxPoint(0, 0), wxSize(0, 250));

	wxPanel *panel2 = new wxPanel(splitter4, wxID_ANY, wxPoint(0, 0), wxSize(0, 250));
	wxBoxSizer *sizer2 = new wxBoxSizer(wxVERTICAL);
	panel2->SetSizer(sizer2);

	display = new CodeDisplay(splitter2, ID_SourceDisplay);

	wxColor background = wxColor(40, 40, 45), backgroundLight = wxColor(80, 80, 85);
	wxColor textColors[32] = { wxColor(255, 255, 255), wxColor(180, 180, 180), wxColor(180, 180, 180), wxColor(180, 180, 180), 
				   wxColor(209, 245, 221), wxColor(255, 255, 255), wxColor(245, 221, 209), wxColor(245, 221, 209),
				   wxColor(245, 221, 209), wxColor(245, 243, 209), wxColor(245, 243, 209), wxColor(255, 255, 255),
				   wxColor(245, 221, 209), wxColor(255, 255, 255), wxColor(245, 221, 209), wxColor(180, 180, 180),
				   wxColor(255, 255, 255), wxColor(180, 180, 180), wxColor(180, 180, 180), wxColor(255, 255, 255), };

	display->SetTabWidth(8);
	display->SetReadOnly(true);
	display->SetCaretWidth(0);
	display->SetMarginWidth(0, 40);
	display->SetMarginWidth(1, 32);
	display->SetMarginType(1, wxSTC_MARGIN_TEXT);
	display->SetMarginSensitive(1, true);
	display->SetYCaretPolicy(wxSTC_CARET_STRICT | wxSTC_CARET_EVEN, 0);
	display->SetCaretLineBackground(backgroundLight);
	display->SetCaretLineVisibleAlways(true);
	display->SetLexer(wxSTC_LEX_CPP);
	display->SetProperty("lexer.cpp.track.preprocessor", "0");

	for (int i = 0; i < wxSTC_STYLE_MAX; i++) {
		if (i != wxSTC_STYLE_LINENUMBER && i != wxSTC_STYLE_DEFAULT) display->StyleSetFont(i, font);
		display->StyleSetBackground(i, background);
		if (i < 20) display->StyleSetForeground(i, textColors[i]);
	}

	display->StyleSetBackground(wxSTC_STYLE_MAX - 1, *wxRED);
	display->StyleSetBackground(wxSTC_STYLE_MAX - 2, *wxYELLOW);
	display->StyleSetBackground(wxSTC_STYLE_LINENUMBER, backgroundLight);
	display->StyleSetForeground(wxSTC_STYLE_LINENUMBER, *wxWHITE);

	wxNotebook *notebook1 = new wxNotebook(splitter3, wxID_ANY);

	breakpointList = new wxListCtrl(notebook1, ID_BreakpointList, wxPoint(0, 0), wxSize(400, 0), wxLC_REPORT);
	breakpointList->AppendColumn(wxT("File"));
	breakpointList->AppendColumn(wxT("Line"));

	functionList = new wxListCtrl(notebook1, ID_FunctionList, wxPoint(0, 0), wxSize(400, 0), wxLC_REPORT);
	functionList->AppendColumn(wxT("Name"));

	callStackList = new wxListCtrl(splitter3, ID_CallStackList, wxPoint(0, 0), wxSize(250, 0), wxLC_REPORT);
	callStackList->AppendColumn(wxT("Index"));
	callStackList->AppendColumn(wxT("Function"));
	callStackList->AppendColumn(wxT("Location"));
	callStackList->AppendColumn(wxT("Address"));

	console = new wxStyledTextCtrl(panel2, ID_ConsoleOutput, wxPoint(0, 0), wxSize(0, 250));
	console->StyleSetFont(0, font);
	console->SetTabWidth(8);
	console->SetReadOnly(true);
	console->SetMarginWidth(1, 0);
	console->SetCaretWidth(0);
	console->StyleSetBackground(0, background);
	console->StyleSetForeground(0, *wxWHITE);
	console->StyleSetBackground(wxSTC_STYLE_DEFAULT, background);

	consoleInput = new MyTextCtrl(panel2, ID_ConsoleInput);
	consoleInput->SetWindowStyle(wxHSCROLL | wxTE_PROCESS_ENTER | wxTE_PROCESS_TAB);
	consoleInput->SetFocus();
	consoleInput->SetDefaultStyle(textAttributes);
	consoleInput->SetFont(font);

	dataView = new DataView();
	dataView->Create(splitter4, wxID_ANY);

	sizer->Add(splitter1, 1, wxEXPAND);
	sizer2->Add(console, 1, wxEXPAND);
	sizer2->Add(consoleInput, 0, wxEXPAND);

	splitter1->SetMinimumPaneSize(100);
	splitter1->SplitHorizontally(splitter2, splitter4);
	splitter1->SetSashPosition(10000);
	splitter1->SetSashGravity(1);

	splitter2->SetMinimumPaneSize(300);
	splitter2->SplitVertically(display, splitter3);
	splitter2->SetSashPosition(10000);
	splitter2->SetSashGravity(1);

	splitter3->SetMinimumPaneSize(100);
	splitter3->SplitHorizontally(notebook1, callStackList);

	splitter4->SetMinimumPaneSize(100);
	splitter4->SplitVertically(panel2, dataView);
	splitter4->SetSashPosition(0.5);
	splitter4->SetSashGravity(0.5);

	notebook1->AddPage(breakpointList, wxT("Breakpoints"));
	notebook1->AddPage(functionList,   wxT("Functions"));

	wxMenu *menuProcess = new wxMenu;
	menuProcess->Append(ID_Run, 		"&Run\tShift+F5");
	menuProcess->Append(ID_RunPaused,	"Run &paused\tCtrl+F5");

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
	menuDebug->Append(ID_Continue, 		"&Continue\tF5");
	menuDebug->Append(ID_StepOver, 		"Step &over\tF10");
	menuDebug->Append(ID_StepIn, 		"Step &in\tF11");
	menuDebug->Append(ID_StepOut, 		"Step &out\tShift+F11");
	menuDebug->Append(ID_Break, 		"&Break\tCtrl+Shift+C");
	menuDebug->AppendSeparator();
	menuDebug->Append(ID_AddWatchExpression,"Add &watch expression\tF8");
	menuDebug->Append(ID_AddBreakpoint,	"&Add breakpoint\tF9");

	wxMenu *menuView = new wxMenu;
	menuView->Append(ID_ViewBreakpoints, 	"&Breakpoints\tCtrl+Alt+B");
	menuView->Append(ID_ViewCallStack, 	"&Call stack\tCtrl+Alt+C");
	menuView->Append(ID_ViewWatch, 		"&Watch expressions\tCtrl+Alt+W");
	menuView->Append(ID_FocusInput, 	"Focus &input\tCtrl+I");

	wxMenuBar *menuBar = new wxMenuBar;
	menuBar->Append(menuProcess, "&Process");
	menuBar->Append(menuEdit, "&Edit");
	menuBar->Append(menuDebug, "&Debug");
	menuBar->Append(menuView, "&View");
	SetMenuBar(menuBar);

	Centre();
}

void MyFrame::OnExit(wxCommandEvent &event) {
	Close(true);
}

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

	write(pipeToGDB, string, strlen(string));
	write(pipeToGDB, &newline, 1);
}

void MyTextCtrl::OnTabComplete(wxKeyEvent &event) {
	if (event.GetKeyCode() == WXK_TAB) {
		wxString string = GetLineText(0);
		const char *data = string.mb_str();
		const char *complete = "complete ";
		char newline = '\n';
		mode = MODE_TAB_COMPLETE;
		write(pipeToGDB, complete, strlen(complete));
		write(pipeToGDB, data, strlen(data));
		write(pipeToGDB, &newline, 1);
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

#if 0
			for (int i = 0; i < 256; i++) {
				if (commandHistory[i][0]) {
					printf("command %d: %s\n", i, commandHistory[i]);
				} else {
					break;
				}
			}
#endif
		}
	}

	commandHistoryIndex = 0;

	focusInput = true;
	write(pipeToGDB, &newline, 1);
	consoleInput->Clear();
}

bool MyFrame::LoadFile(char *newFile) {
	// printf("> load file: %s\n", newFile);
	struct stat s = {};
	if (stat(newFile, &s)) return false;
	if (!strcmp(newFile, currentFile) && currentFileModified == s.st_mtime) return false;
	currentFileModified = s.st_mtime;
	FILE *input = fopen(newFile, "rb");
	if (!input) return false;
	// printf("\t...\n");
	fseek(input, 0, SEEK_END);
	uint64_t fileLength = ftell(input);
	fseek(input, 0, SEEK_SET);
	char *data = (char *) malloc(fileLength);
	fileLength = fread(data, 1, fileLength, input);
	if (!fileLength) { free(data); return false; }
	fclose(input);
	display->SetReadOnly(false);
	display->ClearAll();
	display->InsertText(0, wxString(data, fileLength));
	display->SetReadOnly(true);
	free(currentFile);
	currentFile = newFile;

	for (int i = 0; i < breakpointCount; i++) {
		Breakpoint *breakpoint = breakpoints + i;

		if (0 == strcmp(currentFile, breakpoint->filename)) {
			display->MarginSetStyle(breakpoint->line - 1, wxSTC_STYLE_MAX - 1);
		}
	}

	return true;
}

void MyFrame::SetLine(int line) {
	// printf("> goto line: %d\n", line);
	display->GotoLine(line - 1);
	display->SetCaretLineVisible(true);

	oldLine = line;
}

void MyFrame::BreakAtFunction(wxCommandEvent &event) {
	sprintf(sendBuffer, "b %s", (const char *) rightClickData);
	SendToGDB(sendBuffer);
}

void MyFrame::AddBreakpoint(int index) {
	Breakpoint *breakpoint = breakpoints + index;

	if (0 == strcmp(currentFile, breakpoint->filename)) {
		display->MarginSetStyle(breakpoint->line - 1, wxSTC_STYLE_MAX - 1);
	}

	breakpointList->InsertItem(index, breakpoint->filename);
	breakpointList->SetItem(index, 1, wxString::Format(wxT("%i"), breakpoint->line));
	breakpointList->SetColumnWidth(0, wxLIST_AUTOSIZE);
}

void MyFrame::RemoveBreakpoint(int index) {
	Breakpoint *breakpoint = breakpoints + index;

	if (0 == strcmp(currentFile, breakpoint->filename)) {
		display->MarginSetStyle(breakpoint->line - 1, wxSTC_STYLE_LINENUMBER);
	}

	breakpointList->DeleteItem(index);

	memmove(breakpoints + index, breakpoints + index + 1, (breakpointCount - index - 1) * sizeof(Breakpoint));
	breakpointCount--;
}

int wxCALLBACK SortFunctions(wxIntPtr item1, wxIntPtr item2, wxIntPtr sortData) {
	return strcmp((char *) item1, (char *) item2);
}

void MyFrame::OnReceiveData(wxCommandEvent &event) {
	const char *data = receiveBuffer;
	// printf("-------------------\n%s\n-------------------\n", data);

	if (mode == MODE_TAB_COMPLETE) {
		const char *end = strchr(data, '\n');

		if (end) {
			consoleInput->Clear();
			consoleInput->AppendText(wxString(data, end - data));
		}

		mode = MODE_NORMAL;
		return;
	} else if (mode == MODE_JUMP_TO_LINE) {
		sprintf(sendBuffer, "jump %d", jumpLine);
		mode = MODE_NORMAL;
		SendToGDB(sendBuffer);
		return;
	} else if (mode == MODE_BREAK_ON_WRITE) {
		mode = MODE_NORMAL;
		char *result = strchr(receiveBuffer, '=');

		if (result) {
			result += 2;
			char *end = strstr(result, "(gdb)");
			if (end) {
				sprintf(sendBuffer, "watch *(%.*s)", (int) (end - result - 1), result);
				SendToGDB(sendBuffer);
			}
		}

		return;
	} else if (mode == MODE_GET_BREAKPOINTS) {
		for (int i = 0; i < breakpointCount; i++) breakpoints[i].seen = false;

		const char *position = data;

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

			// printf("|||\n%.*s\n|||\n", (int) (next - position), position);

			const char *file = strstr(position, " at ");
			if (file) file += 4;

			Breakpoint breakpoint = {};
			bool recognised = true;

			if (file && file < next) {
				const char *end = strchr(file, ':');

				if (end && isdigit(end[1])) {
					memcpy(breakpoint.filename, file, end - file);
					breakpoint.filename[end - file] = 0;
					breakpoint.line = atoi(end + 1);
				} else recognised = false;
			} else recognised = false;

			if (recognised) {
				for (int i = 0; i < breakpointCount; i++) {
					if (0 == memcmp(breakpoints + i, &breakpoint, sizeof(Breakpoint))) {
						breakpoints[i].seen = breakpoint.seen = true;
					}
				}

				if (!breakpoint.seen) {
					breakpoint.seen = true;
					breakpoints[breakpointCount] = breakpoint;
					AddBreakpoint(breakpointCount);
					breakpointCount++;
				}
			}

			position = next;
		}

		for (int i = 0; i < breakpointCount; i++) {
			if (!breakpoints[i].seen) {
				RemoveBreakpoint(i);
				i--;
			}
		}

#if 0
		for (int i = 0; i < breakpointCount; i++) {
			printf("breakpoint %d - %s:%d\n", i, breakpoints[i].filename, breakpoints[i].line);
		}
#endif

		mode = MODE_GET_CALL_STACK;
		SendToGDB("bt 50", false);
		return;
	} else if (mode == MODE_GET_CALL_STACK) {
		callStackList->DeleteAllItems();
		const char *position = data;

		while (*position == '#') {
			const char *next = position;

			while (true) {
				next = strchr(next + 1, '\n');
				if (!next || next[1] == '#') break;
			}

			if (!next) next = position + strlen(position);

			long int id = strtoul(position + 1, (char **) &position, 0);
			callStackList->InsertItem(id, wxString::Format(wxT("%i"), (int) id));

			while (*position == ' ' && position < next) position++;
			bool hasAddress = *position == '0';

			if (hasAddress) {
				long int address = strtoul(position, (char **) &position, 0);
				callStackList->SetItem(id, 3, wxString::Format(wxT("%p"), (void *) address));
				position += 4;
			}

			while (*position == ' ' && position < next) position++;
			const char *functionName = position;
			position = strchr(position, ' ');
			if (!position || position >= next) break;
			callStackList->SetItem(id, 1, wxString(functionName, position - functionName));

			const char *file = strstr(position, " at ");

			if (file && file < next) {
				file += 4;
				const char *end = file;
				while (*end != '\n' && end < next) end++;
				callStackList->SetItem(id, 2, wxString(file, end - file));
			}

			position = next + 1;
		}

		callStackList->SetColumnWidth(0, wxLIST_AUTOSIZE);
		callStackList->SetColumnWidth(1, wxLIST_AUTOSIZE);
		callStackList->SetColumnWidth(2, wxLIST_AUTOSIZE);
		callStackList->SetColumnWidth(3, wxLIST_AUTOSIZE);
		mode = MODE_NORMAL;

		return;
	} else if (mode == MODE_LIST_FUNCTIONS) {
		mode = MODE_NORMAL;
		char *position = (char *) data;
		uintptr_t index = 0;
		functionList->DeleteAllItems();

		while (*position) {
			char c = *position;
			position++;

			if (c == '(') {
				const char *start = position;

				while (start != data && *start != ' ' && *start != '*' && *start != '\n') {
					start--;
				}

				if (*start == ' ' || *start == '*') {
					position[-1] = 0;
					start++;
					functionList->InsertItem(index, start);
					functionList->SetItemPtrData(index, (wxUIntPtr) start);
					index++;
				}

				while (*position && *position != '\n') position++;
			} else if (!c) {
				return;
			}
		}

		functionList->SetColumnWidth(0, wxLIST_AUTOSIZE);
		functionList->SortItems(SortFunctions, 0);

		return;
	} else if (mode == MODE_FIND_FUNCTION) {
		if (strstr(data, " starts at address ")) {
			const char *file = strchr(data, '"') + 1;
			const char *end = strchr(file, '"');

			char *newFile = (char *) malloc(end - file + 1);
			memcpy(newFile, file, end - file);
			newFile[end - file] = 0;

			if (!LoadFile(newFile)) {
				free(newFile);
			} 
			
			const char *line = data + 5;
			int number = 0;

			while (*line != ' ') {
				number = number * 10 + *line - '0';
				line++;
			}

			SetLine(number);
		}

		mode = MODE_NORMAL;
		return;
	}

	// Process ended?

	if (strstr(data, "exited with code") || strstr(data, "Remote connection closed") || strstr(data, "Program terminated")) {
		SetLine(0);
	}

	// Parse the name of the file.

	{
		const char *file = data;

		while (true) {
			file = strstr(file, " at ");
			if (!file) break;

			file += 4;
			const char *end = strchr(file, ':');

			if (end && isdigit(end[1])) {
				char *newFile = (char *) malloc(end - file + 1);
				memcpy(newFile, file, end - file);
				newFile[end - file] = 0;
				if (!LoadFile(newFile)) free(newFile);
			}
		}
	}

	// Parse the current line.

	bool lineChanged = false;

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
					SetLine(number);
				}

				tryNext:;
				line += i + 1;
			} else {
				line++;
			}
		}
	}

	// Add the text to the console.

	console->SetReadOnly(false);
	console->InsertText(console->GetTextLength(), wxString(data));
	console->ScrollToEnd();
	console->SetReadOnly(true);

	// Clear the until line.
	
	if (untilLine) {
		display->MarginSetStyle(untilLine - 1, wxSTC_STYLE_LINENUMBER);
	}

	// Update the data view.

	if (lineChanged) {
		dataView->UpdateNodes();
	}

	// Send the startup commands.

	if (started < sizeof(startCommands) / sizeof(startCommands[0])) {
		SendToGDB(startCommands[started++]);
		return;
	}

	// Restore state after reloading gdb.

	if (symbolFileCount > reloadSymbolFiles) {
		sprintf(sendBuffer, "file %s", symbolFiles[reloadSymbolFiles++]);
		SendToGDB(sendBuffer);
		return;
	}

	if (resetBreakpoints) {
		resetBreakpoints--;
		sprintf(sendBuffer, "b %s:%d", breakpoints[resetBreakpoints].filename, breakpoints[resetBreakpoints].line);
		SendToGDB(sendBuffer);
		return;
	}

	// Send the command line arguments.

	static int argumentsSent = 1;

	if (argumentsSent < myApp->argc) {
		SendToGDB(myApp->argv[argumentsSent++]);
		return;
	}

	// Parse the name of a loaded symbol file.

	{
		const char *query = "Reading symbols from ";
		const char *name = strstr(data, query);
		const char *end = strstr(data, "...done.\n");

		if (name && end) {
			name += strlen(query);
			memcpy(symbolFiles[symbolFileCount], name, end - name);
			symbolFiles[symbolFileCount][end - name] = 0;
			symbolFileCount++;
			reloadSymbolFiles++;

			mode = MODE_LIST_FUNCTIONS;
			SendToGDB("info fun", false);
			return;

#if 0
			printf("Loaded files:\n");

			for (uintptr_t i = 0; i < symbolFileCount; i++) {
				printf("%lu: %s\n", i, symbolFiles[i]);
			}
#endif
		}
	}

	// Get the first file from Vim.

	static bool gotFirstFile = false;

	if (!gotFirstFile) {
		gotFirstFile = true;

		if (!system("vim --servername GVIM --remote-expr \"execute(\\\"ls\\\")\" | grep % > current_file_open_in_vim.txt")) {
			char buffer[1024];
			FILE *file = fopen("current_file_open_in_vim.txt", "r");

			if (file) {
				buffer[fread(buffer, 1, 1023, file)] = 0;
				fclose(file);

				{
					char *name = strchr(buffer, '"');
					if (!name) goto done;
					char *nameEnd = strchr(++name, '"');
					if (!nameEnd) goto done;
					*nameEnd = 0;
					if (!LoadFile(strdup(name))) goto done;
					char *line = strstr(nameEnd + 1, "line ");
					if (!line) goto done;
					int lineNumber = atoi(line + 5);
					SetLine(lineNumber);
				}

				done:;
				     unlink("current_file_open_in_vim.txt");
			}
		}
	}

	// Update the list of breakpoints.

	mode = MODE_GET_BREAKPOINTS;
	SendToGDB("info break", false);
}

void MyFrame::OnMarginClick(wxStyledTextEvent &event) {
	int line = display->LineFromPosition(event.GetPosition()) + 1;

	for (int i = 0; i < breakpointCount; i++) {
		if (line == breakpoints[i].line && 0 == strcmp(breakpoints[i].filename, currentFile)) {
			sprintf(sendBuffer, "clear %s:%d", currentFile, line);
			SendToGDB(sendBuffer, false);
			return;
		}
	}

	sprintf(sendBuffer, "break %s:%d", currentFile, line);
	SendToGDB(sendBuffer, false);
	oldLine = display->LineFromPosition(display->GetCurrentPos()) + 1;
}

void CodeDisplay::OnRunToLine(wxCommandEvent &event) {
	int line = LineFromPosition(GetCurrentPos()) + 1;
	if (line == oldLine) return;
	sprintf(sendBuffer, "until %d", line);
	MarginSetStyle(line - 1, wxSTC_STYLE_MAX - 2);
	untilLine = line;
	frame->SendToGDB(sendBuffer);
}

void CodeDisplay::OnJumpToLine(wxCommandEvent &event) {
	int line = LineFromPosition(GetCurrentPos()) + 1;
	if (line == oldLine) return;
	mode = MODE_JUMP_TO_LINE;
	sprintf(sendBuffer, "tbreak %d", line);
	jumpLine = line;
	frame->SendToGDB(sendBuffer);
}

void MyFrame::OnSelectFrame(wxListEvent &event) {
	sprintf(sendBuffer, "frame %d", (int) event.GetIndex());
	SendToGDB(sendBuffer);
}

void MyFrame::AddBreakpointH(wxCommandEvent &event) {
	wxTextEntryDialog dialog(frame, wxT("Enter the function or line to break on:"), wxT("Add Breakpoint"));
	int result = dialog.ShowModal();
	if (result == wxID_CANCEL) return;
	sprintf(sendBuffer, "b %s", (const char *) dialog.GetValue().mb_str());
	SendToGDB(sendBuffer);
}

void MyFrame::RemoveItem(wxCommandEvent &event) {
	if (breakpointList->HasFocus()) {
		long index = breakpointList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

		if (index >= 0 && index < breakpointCount) {
			Breakpoint *breakpoint = breakpoints + index;
			sprintf(sendBuffer, "clear %s:%d", breakpoint->filename, breakpoint->line);
			SendToGDB(sendBuffer, false);
		}
	}
}

void MyFrame::RestartGDB(wxCommandEvent &event) {
	kill(gdbPID, SIGKILL);
	pthread_cancel(gdbThread); // TODO Is there a nicer way to do this?
	newReceive = true, mode = MODE_NORMAL;
	started = reloadSymbolFiles = 0;
	resetBreakpoints = breakpointCount, breakpointCount = 0;
	breakpointList->DeleteAllItems();
	StartGDBThread();
}

void MyFrame::OnFunctionListRightClick(wxListEvent &event) {
	const wxListItem &item = event.GetItem();
	wxString string = item.GetText();
	const char *text = string.mb_str();
	rightClickData = text;
	wxMenu menu("");
	menu.Append(ID_BreakAtFunction, "Add &breakpoint");
	PopupMenu(&menu);
}

void MyFrame::OnSelectFunction(wxListEvent &event) {
	const wxListItem &item = event.GetItem();
	wxString string = item.GetText();
	const char *text = string.mb_str();
	mode = MODE_FIND_FUNCTION;
	sprintf(sendBuffer, "info line %s", text);
	SendToGDB(sendBuffer, false);
}

void DataView::UpdateNodes(bool newlyVisibleOnly) {
	// TODO This makes stepping sloooow!

	int parentWidth, parentHeight;
	GetClientSize(&parentWidth, &parentHeight);

	for (uintptr_t i = 0; i < nodeCount; i++) {
		DataNode *node = nodes + i;
		int x = node->x, y = node->y, 
		    width = NODE_WIDTH, height = 32 + 16 * node->entryCount;

		if (x + width < 0 || y + height < 0 || x > parentWidth || y > parentHeight) {
			for (uintptr_t i = 0; i < node->entryCount; i++) {
				DataNodeEntry *entry = node->entries + i;
				entry->isVisible = false;
			}

			continue;
		}

		if (node->type == DATA_NODE_GLOBALS) {
			if (!node->entryCount) {
				sprintf(sendBuffer, "info variables");
				QueryGDB();
				size_t length = strstr(receiveBuffer, "Non-debugging symbols:") - receiveBuffer;

				char *position = (char *) receiveBuffer;
				size_t globalCount = 0;
				for (uintptr_t i = 0; i < length; i++) if (receiveBuffer[i] == ';') globalCount++;
				// printf("globalCount = %d\n", (int) globalCount);

				node->entries = (DataNodeEntry *) calloc(globalCount, sizeof(DataNodeEntry));

				while (*position) {
					char c = *position;
					position++;

					if (c == ';') {
						const char *start = position - 1;

						while (start != position && *start != ' ' && *start != '*' && *start != '\n') {
							start--;
						}

						start++;
						strncpy(node->entries[node->entryCount].name, start, position - start - 1);
						// printf("global: %s\n", node->entries[node->entryCount].name);
						while (*position && *position != '\n') position++;
						node->entryCount++;
						if (node->entryCount == globalCount) break;
					} else if (position == receiveBuffer + length) {
						break;
					}
				}
			}
		} else if (node->type == DATA_NODE_STRUCT) {
			sprintf(sendBuffer, "p %c(%s)", "& "[node->isPointer], node->prefix);
			QueryGDB();
			node->address = 0;
			const char *address = strstr(receiveBuffer, ") 0x");
			if (address) node->address = strtoul(address + 4, nullptr, 16);
		}

		for (uintptr_t i = 0; i < node->entryCount; i++) {
			DataNodeEntry *entry = node->entries + i;
			int yp = y + 24 + i * 16;
			bool wasVisible = entry->isVisible;
			entry->isVisible = yp + 20 > 0 && yp < parentHeight;
			if (!newlyVisibleOnly) entry->isCached = false;
			if ((wasVisible && newlyVisibleOnly) || !entry->isVisible || entry->isCached) continue;
			entry->isCached = true;

			char newValue[64];
			newValue[0] = newValue[1] = '?';
			newValue[2] = 0;

			for (int i = 0; i < 2; i++) {
				sprintf(sendBuffer, "p %c(%s %s)", " *"[i], node->prefix, entry->name);
				QueryGDB();
				char *result = strchr(receiveBuffer, '=');

				if (result) {
					// printf("%s -> %s\n", sendBuffer, result);
					result += 2;

					if (strstr(result, ") 0x0\n")) {
						sprintf(newValue, "null");
						entry->isPointer = true;
						break;
					}

					char *end = strstr(result, "(gdb)");

					if (end) {
						if (!i) entry->isPointer = false;
						snprintf(newValue, 24 /* There isn't much room... */, "%s%.*s", i ? "&" : "", (int) (end - result - 1), result);
						if (newValue[0] != '(') break;
						entry->isPointer = true;
					}
				}
			}

			entry->valueChanged = !entry->justAdded && strcmp(entry->value, newValue);
			entry->justAdded = false;
			strcpy(entry->value, newValue);
			entry->isContainer = entry->value[0] == '{' || (entry->value[0] == '&' && entry->value[1] == '{'); 

			sprintf(sendBuffer, "p %c(%s %s)", "& "[entry->isPointer], node->prefix, entry->name);
			QueryGDB();
			entry->address = 0;
			const char *address = strstr(receiveBuffer, ") 0x");
			if (address) entry->address = strtoul(address + 4, nullptr, 16);
		}
	}

	Refresh();
}

void DataView::ParseFields(DataNode *node, DataNode *newNode, DataNodeEntry *entry) {
	sprintf(sendBuffer, "p %c(%s %s)", entry->isPointer ? '*' : ' ', node->prefix, entry->name);
	QueryGDB();

	// printf("got: %s\n", receiveBuffer);

	int layer = 0;
	bool inQuotes = false;
	const char *position = receiveBuffer;
	bool parseField = false;

	while (*position) {
		char c = *position;

		if (parseField) {
			newNode->entries = (DataNodeEntry *) realloc(newNode->entries, ++newNode->entryCount * sizeof(DataNodeEntry));
			DataNodeEntry *newEntry = newNode->entries + newNode->entryCount - 1;
			memset(newEntry, 0, sizeof(DataNodeEntry));
			newEntry->justAdded = true;

			while (isblank(*position) || *position == '\n') position++;
			strcat(newEntry->name, *position == '[' ? "" : ".");
			strncat(newEntry->name, position, strchr(position, '=') - position);
			parseField = false;
		}

		if (!inQuotes && c == '{') { layer++; if (layer == 1) parseField = true; }
		else if (!inQuotes && c == '}') layer--;
		else if (position[-1] != '\\' && c == '"') inQuotes = !inQuotes;
		else if (!inQuotes && c == ',' && layer == 1) parseField = true;
		position++;
	}

#if 0
	for (uintptr_t i = 0; i < newNode->entryCount; i++) {
		printf("%d: '%s'\n", (int) i, newNode->entries[i].name);
	}
#endif

	UpdateNodes();
}
