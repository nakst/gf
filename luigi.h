// TODO Use HeapReAlloc on Windows.
// TODO UIScrollBar - horizontal.
// TODO Scaling; include a larger font?
// TODO UITable - take column label into account when resizing columns.
// TODO Keyboard navigation:
// 	- menus
// 	- tabbing
// 	- dialogs
// TODO Textbox features:
// 	- mouse input
// 	- multi-line 
// 	- clipboard
// 	- undo/redo
// 	- IME support
// TODO Elements:
// 	- check box 
// 	- radio box
// 	- list view
// 	- dialogs
// 	- menu bar

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef UI_LINUX
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#define UI_ASSERT assert
#define UI_CALLOC(x) calloc(1, (x))
#define UI_FREE free
#define UI_MALLOC malloc
#define UI_REALLOC realloc
#define UI_CLOCK clock
#define UI_CLOCKS_PER_SECOND CLOCKS_PER_SEC
#define UI_CLOCK_T clock_t

#define UI_KEYCODE_SPACE XK_space
#define UI_KEYCODE_BACKSPACE XK_BackSpace
#define UI_KEYCODE_DELETE XK_Delete
#define UI_KEYCODE_LEFT XK_Left
#define UI_KEYCODE_RIGHT XK_Right
#define UI_KEYCODE_UP XK_Up
#define UI_KEYCODE_DOWN XK_Down
#define UI_KEYCODE_HOME XK_Home
#define UI_KEYCODE_END XK_End
#define UI_KEYCODE_A XK_a
#define UI_KEYCODE_F1 XK_F1
#define UI_KEYCODE_F2 XK_F2
#define UI_KEYCODE_F3 XK_F3
#define UI_KEYCODE_F4 XK_F4
#define UI_KEYCODE_F5 XK_F5
#define UI_KEYCODE_F6 XK_F6
#define UI_KEYCODE_F7 XK_F7
#define UI_KEYCODE_F8 XK_F8
#define UI_KEYCODE_F9 XK_F9
#define UI_KEYCODE_F10 XK_F10
#define UI_KEYCODE_F11 XK_F11
#define UI_KEYCODE_F12 XK_F12
#define UI_KEYCODE_ENTER XK_Return
#define UI_KEYCODE_TAB XK_Tab
#endif

#ifdef UI_WINDOWS
#include <windows.h>

#include <stdlib.h>

#define _UI_TO_STRING_1(x) #x
#define _UI_TO_STRING_2(x) _UI_TO_STRING_1(x)
#define UI_ASSERT(x) do { if (!(x)) { MessageBox(0, "Assertion failure on line " _UI_TO_STRING_2(__LINE__), 0, 0); ExitProcess(1); } } while (0)
#define UI_CALLOC(x) HeapAlloc(ui.heap, HEAP_ZERO_MEMORY, (x))
#define UI_FREE(x) HeapFree(ui.heap, 0, (x))
#define UI_MALLOC(x) HeapAlloc(ui.heap, 0, (x))
#define UI_REALLOC realloc
#define UI_CLOCK GetTickCount
#define UI_CLOCKS_PER_SECOND (1000)
#define UI_CLOCK_T DWORD

#define UI_KEYCODE_SPACE VK_SPACE
#define UI_KEYCODE_BACKSPACE VK_BACK
#define UI_KEYCODE_DELETE VK_DELETE
#define UI_KEYCODE_LEFT VK_LEFT
#define UI_KEYCODE_RIGHT VK_RIGHT
#define UI_KEYCODE_HOME VK_HOME
#define UI_KEYCODE_END VK_END
#define UI_KEYCODE_A 'A'
#endif

#ifdef UI_DEBUG
#include <stdio.h>
#endif

#define UI_KEYCODE_LETTER(x) (UI_KEYCODE_A + (x) - 'A')

#define UI_COLOR_PANEL_GRAY (0xF0F0F0)
#define UI_COLOR_PANEL_WHITE (0xFFFFFF)

#define UI_COLOR_TEXT (0x000000)

#define UI_COLOR_BORDER (0x404040)

#define UI_COLOR_BUTTON_NORMAL (0xE0E0E0)
#define UI_COLOR_BUTTON_HOVERED (0xF0F0F0)
#define UI_COLOR_BUTTON_PRESSED (0xA0A0A0)
#define UI_COLOR_BUTTON_FOCUSED (0xD3E4FF)

#define UI_COLOR_TEXTBOX_NORMAL (0xF8F8F8)
#define UI_COLOR_TEXTBOX_TEXT (0x000000)
#define UI_COLOR_TEXTBOX_FOCUSED (0xFFFFFF)
#define UI_COLOR_TEXTBOX_SELECTED (0x175EC9)
#define UI_COLOR_TEXTBOX_SELECTED_TEXT (0xFFFFFF)

#define UI_COLOR_SCROLL_GLYPH (0x606060)
#define UI_COLOR_SCROLL_THUMB_NORMAL (0xB0B0B0)
#define UI_COLOR_SCROLL_THUMB_HOVERED (0xD0D0D0)
#define UI_COLOR_SCROLL_THUMB_PRESSED (0x909090)

#define UI_COLOR_CODE_FOCUSED (0x505055)
#define UI_COLOR_CODE_BACKGROUND (0x28282D)
#define UI_COLOR_CODE_DEFAULT (0xFFFFFF)
#define UI_COLOR_CODE_COMMENT (0xB4B4B4)
#define UI_COLOR_CODE_STRING (0xF5DDD1)
#define UI_COLOR_CODE_NUMBER (0xD1F5DD)
#define UI_COLOR_CODE_OPERATOR (0xF5F3D1)
#define UI_COLOR_CODE_PREPROCESSOR (0xF5F3D1)

#define UI_COLOR_GAUGE_FILLED (0x2CE342)

#define UI_COLOR_TABLE_SELECTED (0x94BEFE)
#define UI_COLOR_TABLE_SELECTED_TEXT (0x000000)
#define UI_COLOR_TABLE_HOVERED (0xD3E4FF)
#define UI_COLOR_TABLE_HOVERED_TEXT (0x000000)

#define UI_SIZE_BUTTON_MINIMUM_WIDTH (100)
#define UI_SIZE_BUTTON_PADDING (16)
#define UI_SIZE_BUTTON_HEIGHT (27)

#define UI_SIZE_MENU_ITEM_HEIGHT (24)
#define UI_SIZE_MENU_ITEM_MINIMUM_WIDTH (160)
#define UI_SIZE_MENU_ITEM_MARGIN (9)

#define UI_SIZE_GAUGE_WIDTH (200)
#define UI_SIZE_GAUGE_HEIGHT (22)

#define UI_SIZE_SLIDER_WIDTH (200)
#define UI_SIZE_SLIDER_HEIGHT (25)
#define UI_SIZE_SLIDER_THUMB (15)
#define UI_SIZE_SLIDER_TRACK (3)

#define UI_SIZE_TEXTBOX_MARGIN (3)
#define UI_SIZE_TEXTBOX_WIDTH (200)
#define UI_SIZE_TEXTBOX_HEIGHT (25)

#define UI_SIZE_TAB_PANE_SPACE_TOP (2)
#define UI_SIZE_TAB_PANE_SPACE_LEFT (4)

#define UI_SIZE_SPLITTER (8)

#define UI_SIZE_SCROLL_BAR (20)
#define UI_SIZE_SCROLL_MINIMUM_THUMB (20)

#define UI_SIZE_GLYPH_WIDTH (9)
#define UI_SIZE_GLYPH_HEIGHT (16)

#define UI_SIZE_CODE_MARGIN (UI_SIZE_GLYPH_WIDTH * 5)
#define UI_SIZE_CODE_MARGIN_GAP (UI_SIZE_GLYPH_WIDTH * 1)

#define UI_SIZE_TABLE_HEADER (26)
#define UI_SIZE_TABLE_COLUMN_GAP (20)

typedef enum UIMessage {
	UI_MSG_PAINT, // dp = pointer to UIPainter
	UI_MSG_LAYOUT,
	UI_MSG_DESTROY,

	UI_MSG_UPDATE,
	UI_MSG_CLICKED,
	UI_MSG_ANIMATE,
	UI_MSG_SCROLLED,

	UI_MSG_GET_WIDTH, // di = height (if known); return width
	UI_MSG_GET_HEIGHT, // di = width (if known); return height
	UI_MSG_GET_CURSOR, // return cursor code

	UI_MSG_LEFT_DOWN,
	UI_MSG_LEFT_UP,
	UI_MSG_MIDDLE_DOWN,
	UI_MSG_MIDDLE_UP,
	UI_MSG_RIGHT_DOWN,
	UI_MSG_RIGHT_UP,
	UI_MSG_KEY_TYPED, // dp = pointer to UIKeyTyped; return 1 if handled

	UI_MSG_MOUSE_MOVE,
	UI_MSG_MOUSE_DRAG,
	UI_MSG_MOUSE_WHEEL, // di = delta; return 1 if handled

	UI_MSG_TABLE_GET_ITEM, // dp = pointer to UITableGetItem; return string length
	UI_MSG_CODE_GET_MARGIN_COLOR, // di = line index (starts at 1); return color

	UI_MSG_SLIDER_CHANGED,
	UI_MSG_TEXTBOX_CHANGED,

	UI_MSG_USER,
} UIMessage;

typedef struct UIKeyTyped {
	char *text;
	int textBytes;
	intptr_t code;
} UIKeyTyped;

typedef struct UITableGetItem {
	char *buffer;
	size_t bufferBytes;
	int index, column;
	bool isSelected;
} UITableGetItem;

typedef struct UIStringSelection {
	int carets[2];
	uint32_t colorText, colorBackground;
} UIStringSelection;

typedef struct UIRectangle {
	int l, r, t, b;
} UIRectangle;

#define UI_RECT_1(x) ((UIRectangle) { (x), (x), (x), (x) })
#define UI_RECT_1I(x) ((UIRectangle) { (x), -(x), (x), -(x) })
#define UI_RECT_2(x, y) ((UIRectangle) { (x), (x), (y), (y) })
#define UI_RECT_2I(x, y) ((UIRectangle) { (x), -(x), (y), -(y) })
#define UI_RECT_2S(x, y) ((UIRectangle) { 0, (x), 0, (y) })
#define UI_RECT_4(x, y, z, w) ((UIRectangle) { (x), (y), (z), (w) })
#define UI_RECT_WIDTH(_r) ((_r).r - (_r).l)
#define UI_RECT_HEIGHT(_r) ((_r).b - (_r).t)
#define UI_RECT_TOTAL_H(_r) ((_r).r + (_r).l)
#define UI_RECT_TOTAL_V(_r) ((_r).b + (_r).t)
#define UI_RECT_SIZE(_r) UI_RECT_WIDTH(_r), UI_RECT_HEIGHT(_r)
#define UI_RECT_TOP_LEFT(_r) (_r).l, (_r).t
#define UI_RECT_BOTTOM_LEFT(_r) (_r).l, (_r).b
#define UI_RECT_BOTTOM_RIGHT(_r) (_r).r, (_r).b
#define UI_RECT_ALL(_r) (_r).l, (_r).r, (_r).t, (_r).b
#define UI_RECT_VALID(_r) (UI_RECT_WIDTH(_r) > 0 && UI_RECT_HEIGHT(_r) > 0)

#define UI_SWAP(s, a, b) do { s t = (a); (a) = (b); (b) = t; } while (0)

#define UI_CURSOR_ARROW (0)
#define UI_CURSOR_TEXT (1)
#define UI_CURSOR_SPLIT_V (2)
#define UI_CURSOR_SPLIT_H (3)
#define UI_CURSOR_FLIPPED_ARROW (4)
#define UI_CURSOR_COUNT (5)

#define UI_ALIGN_LEFT (1)
#define UI_ALIGN_RIGHT (2)
#define UI_ALIGN_CENTER (3)

typedef struct UIPainter {
	UIRectangle clip;
	uint32_t *bits;
	int width, height;
} UIPainter;

typedef struct UIElement {
#define UI_ELEMENT_V_FILL ((uint64_t) 1 << 32)
#define UI_ELEMENT_H_FILL ((uint64_t) 1 << 33)

#define UI_ELEMENT_REPAINT ((uint64_t) 1 << 48)
#define UI_ELEMENT_HIDE ((uint64_t) 1 << 49)
#define UI_ELEMENT_DESTROY ((uint64_t) 1 << 50)
#define UI_ELEMENT_DESTROY_DESCENDENT ((uint64_t) 1 << 51)

	uint64_t flags; // First 32 bits are element specific.

	struct UIElement *parent;
	struct UIElement *next;
	struct UIElement *children;
	struct UIWindow *window;

	UIRectangle bounds, clip, repaint;
	
	void *cp; // Context pointer (for user).

	int (*messageClass)(struct UIElement *element, UIMessage message, int di, void *dp);
	int (*messageUser)(struct UIElement *element, UIMessage message, int di, void *dp);

#ifdef UI_DEBUG
	const char *cClassName;
	int id;
#endif
} UIElement;

typedef struct UIShortcut {
	intptr_t code;
	bool ctrl, shift, alt;
	void (*invoke)(void *cp);
	void *cp;
} UIShortcut;

typedef struct UIWindow {
#define UI_WINDOW_MENU (1 << 0)
#define UI_WINDOW_INSPECTOR (1 << 1)

	UIElement e;

	UIShortcut *shortcuts;
	size_t shortcutCount, shortcutAllocated;

	float scale;

	uint32_t *bits;
	int width, height;
	struct UIWindow *next;

	UIElement *hovered, *pressed, *focused;
	int pressedButton;

	int cursorX, cursorY;
	int cursorStyle;

	bool ctrl, shift, alt;

	UIRectangle updateRegion;

#ifdef UI_LINUX
	Window window;
	XImage *image;
	XIC xic;
	unsigned ctrlCode, shiftCode, altCode;
#endif

#ifdef UI_WINDOWS
	HWND hwnd;
	bool trackingLeave;
#endif
} UIWindow;

typedef struct UIPanel {
#define UI_PANEL_HORIZONTAL (1 << 0)
#define UI_PANEL_GRAY (1 << 2)
#define UI_PANEL_WHITE (1 << 3)
#define UI_PANEL_EXPAND (1 << 4)
	UIElement e;
	UIRectangle border;
	int gap;
} UIPanel;

typedef struct UIButton {
#define UI_BUTTON_SMALL (1 << 0)
#define UI_BUTTON_MENU_ITEM (1 << 1)
#define UI_BUTTON_CAN_FOCUS (1 << 2)
	UIElement e;
	char *label;
	ptrdiff_t labelBytes;
	void (*invoke)(void *cp);
} UIButton;

typedef struct UILabel {
	UIElement e;
	char *label;
	ptrdiff_t labelBytes;
} UILabel;

typedef struct UISpacer {
#define UI_SPACER_LINE (1 << 0)
	UIElement e;
	int width, height;
} UISpacer;

typedef struct UISplitPane {
#define UI_SPLIT_PANE_VERTICAL (1 << 0)
	UIElement e;
	float weight;
} UISplitPane;

typedef struct UITabPane {
	UIElement e;
	char *tabs;
	int active;
} UITabPane;

typedef struct UIScrollBar {
	UIElement e;
	int64_t maximum, page;
	int64_t dragOffset;
	double position;
	UI_CLOCK_T lastAnimateTime;
	bool inDrag;
} UIScrollBar;

typedef struct UICodeLine {
	int offset, bytes;
} UICodeLine;

typedef struct UICode {
#define UI_CODE_NO_MARGIN (1 << 0)

	UIElement e;
	UIScrollBar *vScroll;
	UICodeLine *lines;
	int lineCount, focused;
	bool moveScrollToFocusNextLayout;
	char *content;
	size_t contentBytes;
} UICode;

typedef struct UIGauge {
	UIElement e;
	float position;
} UIGauge;

typedef struct UITable {
	UIElement e;
	UIScrollBar *vScroll;
	int itemCount;
	char *columns;
	int *columnWidths, columnCount;
} UITable;

typedef struct UITextbox {
	UIElement e;
	char *string;
	ptrdiff_t bytes;
	int carets[2];
	int scroll;
} UITextbox;

typedef struct UIMenu {
#define UI_MENU_PLACE_ABOVE (1 << 0)
	UIElement e;
	int pointX, pointY;
} UIMenu;

typedef struct UISlider {
	UIElement e;
	float position;
} UISlider;

void UIInitialise();
int UIMessageLoop();

UIButton *UIButtonCreate(UIElement *parent, uint64_t flags, const char *label, ptrdiff_t labelBytes);
UIGauge *UIGaugeCreate(UIElement *parent, uint64_t flags);
UIPanel *UIPanelCreate(UIElement *parent, uint64_t flags);
UIScrollBar *UIScrollBarCreate(UIElement *parent, uint64_t flags);
UISlider *UISliderCreate(UIElement *parent, uint64_t flags);
UISpacer *UISpacerCreate(UIElement *parent, uint64_t flags, int width, int height);
UISplitPane *UISplitPaneCreate(UIElement *parent, uint64_t flags, float weight);
UITabPane *UITabPaneCreate(UIElement *parent, uint64_t flags, const char *tabs /* separate with \t, terminate with \0 */);
UITextbox *UITextboxCreate(UIElement *parent, uint64_t flags);

UILabel *UILabelCreate(UIElement *parent, uint64_t flags, const char *label, ptrdiff_t labelBytes);
void UILabelSetContent(UILabel *code, const char *content, ptrdiff_t byteCount);

UIWindow *UIWindowCreate(UIWindow *owner, uint64_t flags, const char *cTitle);
void UIWindowRegisterShortcut(UIWindow *window, UIShortcut shortcut);
void UIWindowPostMessage(UIWindow *window, UIMessage message, void *dp); // Thread-safe.

UIMenu *UIMenuCreate(UIElement *parent, uint64_t flags);
void UIMenuAddItem(UIMenu *menu, uint64_t flags, const char *label, ptrdiff_t labelBytes, void (*invoke)(void *cp), void *cp);
void UIMenuShow(UIMenu *menu);

UITextbox *UITextboxCreate(UIElement *parent, uint64_t flags);
void UITextboxReplace(UITextbox *textbox, const char *text, ptrdiff_t bytes, bool sendChangedMessage);
void UITextboxClear(UITextbox *textbox, bool sendChangedMessage);
void UITextboxMoveCaret(UITextbox *textbox, bool backward, bool word);

UITable *UITableCreate(UIElement *parent, uint64_t flags, const char *columns /* separate with \t, terminate with \0 */);
int UITableHitTest(UITable *table, int x, int y); // Returns item index. Returns -1 if not on an item.
bool UITableEnsureVisible(UITable *table, int index); // Returns false if the item was already visible.
void UITableResizeColumns(UITable *table);

UICode *UICodeCreate(UIElement *parent, uint64_t flags);
void UICodeFocusLine(UICode *code, int index); // Line numbers are 1-indexed!!
int UICodeHitTest(UICode *code, int x, int y); // Returns line number; negates if in margin. Returns 0 if not on a line.
void UICodeInsertContent(UICode *code, const char *content, ptrdiff_t byteCount, bool replace);

void UIDrawBlock(UIPainter *painter, UIRectangle rectangle, uint32_t color);
void UIDrawInvert(UIPainter *painter, UIRectangle rectangle);
void UIDrawGlyph(UIPainter *painter, int x, int y, int c, uint32_t color);
void UIDrawRectangle(UIPainter *painter, UIRectangle r, uint32_t mainColor, uint32_t borderColor, UIRectangle borderSize);
void UIDrawString(UIPainter *painter, UIRectangle r, const char *string, ptrdiff_t bytes, uint32_t color, int align, UIStringSelection *selection);

int UIMeasureStringWidth(const char *string, ptrdiff_t bytes);
int UIMeasureStringHeight();

void UIElementDestroy(UIElement *element);
UIElement *UIElementFindByPoint(UIElement *element, int x, int y);
void UIElementFocus(UIElement *element);
UIRectangle UIElementScreenBounds(UIElement *element); // Returns bounds of element in same coordinate system as used by UIWindowCreate.
void UIElementRefresh(UIElement *element);
void UIElementRepaint(UIElement *element, UIRectangle *region);
void UIElementMove(UIElement *element, UIRectangle bounds, bool alwaysLayout);
int UIElementMessage(UIElement *element, UIMessage message, int di, void *dp);

UIRectangle UIRectangleIntersection(UIRectangle a, UIRectangle b);
UIRectangle UIRectangleBounding(UIRectangle a, UIRectangle b);
UIRectangle UIRectangleAdd(UIRectangle a, UIRectangle b);
UIRectangle UIRectangleTranslate(UIRectangle a, UIRectangle b);
bool UIRectangleEquals(UIRectangle a, UIRectangle b);
bool UIRectangleContains(UIRectangle a, int x, int y);

#ifdef UI_IMPLEMENTATION

struct {
	UIWindow *windows;
	UIElement *animating;

#ifdef UI_DEBUG
	UIWindow *inspector;
	UITable *inspectorTable;
	UIWindow *inspectorTarget;
#endif

#ifdef UI_LINUX
	Display *display;
	Visual *visual;
	XIM xim;
	Atom windowClosedID;
	Cursor cursors[UI_CURSOR_COUNT];
#endif

#ifdef UI_WINDOWS
	HCURSOR cursors[UI_CURSOR_COUNT];
	HANDLE heap;
#endif
} ui;

// Taken from https://commons.wikimedia.org/wiki/File:Codepage-437.png
// Public domain.

const uint64_t _uiFont[] = {
	0x0000000000000000UL, 0x0000000000000000UL, 0xBD8181A5817E0000UL, 0x000000007E818199UL, 0xC3FFFFDBFF7E0000UL, 0x000000007EFFFFE7UL, 0x7F7F7F3600000000UL, 0x00000000081C3E7FUL, 
	0x7F3E1C0800000000UL, 0x0000000000081C3EUL, 0xE7E73C3C18000000UL, 0x000000003C1818E7UL, 0xFFFF7E3C18000000UL, 0x000000003C18187EUL, 0x3C18000000000000UL, 0x000000000000183CUL, 
	0xC3E7FFFFFFFFFFFFUL, 0xFFFFFFFFFFFFE7C3UL, 0x42663C0000000000UL, 0x00000000003C6642UL, 0xBD99C3FFFFFFFFFFUL, 0xFFFFFFFFFFC399BDUL, 0x331E4C5870780000UL, 0x000000001E333333UL, 
	0x3C666666663C0000UL, 0x0000000018187E18UL, 0x0C0C0CFCCCFC0000UL, 0x00000000070F0E0CUL, 0xC6C6C6FEC6FE0000UL, 0x0000000367E7E6C6UL, 0xE73CDB1818000000UL, 0x000000001818DB3CUL, 
	0x1F7F1F0F07030100UL, 0x000000000103070FUL, 0x7C7F7C7870604000UL, 0x0000000040607078UL, 0x1818187E3C180000UL, 0x0000000000183C7EUL, 0x6666666666660000UL, 0x0000000066660066UL, 
	0xD8DEDBDBDBFE0000UL, 0x00000000D8D8D8D8UL, 0x6363361C06633E00UL, 0x0000003E63301C36UL, 0x0000000000000000UL, 0x000000007F7F7F7FUL, 0x1818187E3C180000UL, 0x000000007E183C7EUL, 
	0x1818187E3C180000UL, 0x0000000018181818UL, 0x1818181818180000UL, 0x00000000183C7E18UL, 0x7F30180000000000UL, 0x0000000000001830UL, 0x7F060C0000000000UL, 0x0000000000000C06UL, 
	0x0303000000000000UL, 0x0000000000007F03UL, 0xFF66240000000000UL, 0x0000000000002466UL, 0x3E1C1C0800000000UL, 0x00000000007F7F3EUL, 0x3E3E7F7F00000000UL, 0x0000000000081C1CUL, 
	0x0000000000000000UL, 0x0000000000000000UL, 0x18183C3C3C180000UL, 0x0000000018180018UL, 0x0000002466666600UL, 0x0000000000000000UL, 0x36367F3636000000UL, 0x0000000036367F36UL, 
	0x603E0343633E1818UL, 0x000018183E636160UL, 0x1830634300000000UL, 0x000000006163060CUL, 0x3B6E1C36361C0000UL, 0x000000006E333333UL, 0x000000060C0C0C00UL, 0x0000000000000000UL, 
	0x0C0C0C0C18300000UL, 0x0000000030180C0CUL, 0x30303030180C0000UL, 0x000000000C183030UL, 0xFF3C660000000000UL, 0x000000000000663CUL, 0x7E18180000000000UL, 0x0000000000001818UL, 
	0x0000000000000000UL, 0x0000000C18181800UL, 0x7F00000000000000UL, 0x0000000000000000UL, 0x0000000000000000UL, 0x0000000018180000UL, 0x1830604000000000UL, 0x000000000103060CUL, 
	0xDBDBC3C3663C0000UL, 0x000000003C66C3C3UL, 0x1818181E1C180000UL, 0x000000007E181818UL, 0x0C183060633E0000UL, 0x000000007F630306UL, 0x603C6060633E0000UL, 0x000000003E636060UL, 
	0x7F33363C38300000UL, 0x0000000078303030UL, 0x603F0303037F0000UL, 0x000000003E636060UL, 0x633F0303061C0000UL, 0x000000003E636363UL, 0x18306060637F0000UL, 0x000000000C0C0C0CUL, 
	0x633E6363633E0000UL, 0x000000003E636363UL, 0x607E6363633E0000UL, 0x000000001E306060UL, 0x0000181800000000UL, 0x0000000000181800UL, 0x0000181800000000UL, 0x000000000C181800UL, 
	0x060C183060000000UL, 0x000000006030180CUL, 0x00007E0000000000UL, 0x000000000000007EUL, 0x6030180C06000000UL, 0x00000000060C1830UL, 0x18183063633E0000UL, 0x0000000018180018UL, 
	0x7B7B63633E000000UL, 0x000000003E033B7BUL, 0x7F6363361C080000UL, 0x0000000063636363UL, 0x663E6666663F0000UL, 0x000000003F666666UL, 0x03030343663C0000UL, 0x000000003C664303UL, 
	0x66666666361F0000UL, 0x000000001F366666UL, 0x161E1646667F0000UL, 0x000000007F664606UL, 0x161E1646667F0000UL, 0x000000000F060606UL, 0x7B030343663C0000UL, 0x000000005C666363UL, 
	0x637F636363630000UL, 0x0000000063636363UL, 0x18181818183C0000UL, 0x000000003C181818UL, 0x3030303030780000UL, 0x000000001E333333UL, 0x1E1E366666670000UL, 0x0000000067666636UL, 
	0x06060606060F0000UL, 0x000000007F664606UL, 0xC3DBFFFFE7C30000UL, 0x00000000C3C3C3C3UL, 0x737B7F6F67630000UL, 0x0000000063636363UL, 0x63636363633E0000UL, 0x000000003E636363UL, 
	0x063E6666663F0000UL, 0x000000000F060606UL, 0x63636363633E0000UL, 0x000070303E7B6B63UL, 0x363E6666663F0000UL, 0x0000000067666666UL, 0x301C0663633E0000UL, 0x000000003E636360UL, 
	0x18181899DBFF0000UL, 0x000000003C181818UL, 0x6363636363630000UL, 0x000000003E636363UL, 0xC3C3C3C3C3C30000UL, 0x00000000183C66C3UL, 0xDBC3C3C3C3C30000UL, 0x000000006666FFDBUL, 
	0x18183C66C3C30000UL, 0x00000000C3C3663CUL, 0x183C66C3C3C30000UL, 0x000000003C181818UL, 0x0C183061C3FF0000UL, 0x00000000FFC38306UL, 0x0C0C0C0C0C3C0000UL, 0x000000003C0C0C0CUL, 
	0x1C0E070301000000UL, 0x0000000040607038UL, 0x30303030303C0000UL, 0x000000003C303030UL, 0x0000000063361C08UL, 0x0000000000000000UL, 0x0000000000000000UL, 0x0000FF0000000000UL, 
	0x0000000000180C0CUL, 0x0000000000000000UL, 0x3E301E0000000000UL, 0x000000006E333333UL, 0x66361E0606070000UL, 0x000000003E666666UL, 0x03633E0000000000UL, 0x000000003E630303UL, 
	0x33363C3030380000UL, 0x000000006E333333UL, 0x7F633E0000000000UL, 0x000000003E630303UL, 0x060F0626361C0000UL, 0x000000000F060606UL, 0x33336E0000000000UL, 0x001E33303E333333UL, 
	0x666E360606070000UL, 0x0000000067666666UL, 0x18181C0018180000UL, 0x000000003C181818UL, 0x6060700060600000UL, 0x003C666660606060UL, 0x1E36660606070000UL, 0x000000006766361EUL, 
	0x18181818181C0000UL, 0x000000003C181818UL, 0xDBFF670000000000UL, 0x00000000DBDBDBDBUL, 0x66663B0000000000UL, 0x0000000066666666UL, 0x63633E0000000000UL, 0x000000003E636363UL, 
	0x66663B0000000000UL, 0x000F06063E666666UL, 0x33336E0000000000UL, 0x007830303E333333UL, 0x666E3B0000000000UL, 0x000000000F060606UL, 0x06633E0000000000UL, 0x000000003E63301CUL, 
	0x0C0C3F0C0C080000UL, 0x00000000386C0C0CUL, 0x3333330000000000UL, 0x000000006E333333UL, 0xC3C3C30000000000UL, 0x00000000183C66C3UL, 0xC3C3C30000000000UL, 0x0000000066FFDBDBUL, 
	0x3C66C30000000000UL, 0x00000000C3663C18UL, 0x6363630000000000UL, 0x001F30607E636363UL, 0x18337F0000000000UL, 0x000000007F63060CUL, 0x180E181818700000UL, 0x0000000070181818UL, 
	0x1800181818180000UL, 0x0000000018181818UL, 0x18701818180E0000UL, 0x000000000E181818UL, 0x000000003B6E0000UL, 0x0000000000000000UL, 0x63361C0800000000UL, 0x00000000007F6363UL, 
};

void _UIWindowEndPaint(UIWindow *window, UIPainter *painter);
void _UIWindowSetCursor(UIWindow *window, int cursor);
void _UIWindowGetScreenPosition(UIWindow *window, int *x, int *y);
void _UIInspectorRefresh();

UIRectangle UIRectangleIntersection(UIRectangle a, UIRectangle b) {
	if (a.l < b.l) a.l = b.l;
	if (a.t < b.t) a.t = b.t;
	if (a.r > b.r) a.r = b.r;
	if (a.b > b.b) a.b = b.b;
	return a;
}

UIRectangle UIRectangleBounding(UIRectangle a, UIRectangle b) {
	if (a.l > b.l) a.l = b.l;
	if (a.t > b.t) a.t = b.t;
	if (a.r < b.r) a.r = b.r;
	if (a.b < b.b) a.b = b.b;
	return a;
}

UIRectangle UIRectangleAdd(UIRectangle a, UIRectangle b) {
	a.l += b.l;
	a.t += b.t;
	a.r += b.r;
	a.b += b.b;
	return a;
}

UIRectangle UIRectangleTranslate(UIRectangle a, UIRectangle b) {
	a.l += b.l;
	a.t += b.t;
	a.r += b.l;
	a.b += b.t;
	return a;
}

bool UIRectangleEquals(UIRectangle a, UIRectangle b) {
	return a.l == b.l && a.r == b.r && a.t == b.t && a.b == b.b;
}

bool UIRectangleContains(UIRectangle a, int x, int y) {
	return a.l <= x && a.r > x && a.t <= y && a.b > y;
}

void UIElementRefresh(UIElement *element) {
	UIElementMessage(element, UI_MSG_LAYOUT, 0, 0);
	UIElementRepaint(element, NULL);
}

void UIElementRepaint(UIElement *element, UIRectangle *region) {
	if (!region) {
		region = &element->bounds;
	}

	UIRectangle r = UIRectangleIntersection(*region, element->clip);

	if (!UI_RECT_VALID(r)) {
		return;
	}

	bool changed = false;

	if (element->flags & UI_ELEMENT_REPAINT) {
		UIRectangle old = element->repaint;
		element->repaint = UIRectangleBounding(element->repaint, r);
		changed = !UIRectangleEquals(element->repaint, old);
	} else {
		element->flags |= UI_ELEMENT_REPAINT;
		element->repaint = r;
		changed = true;
	}

	if (changed && element->parent) {
		UIElementRepaint(element->parent, &r);
	}
}

void UIElementDestroy(UIElement *element) {
	if (element->flags & UI_ELEMENT_DESTROY) {
		return;
	}

	element->flags |= UI_ELEMENT_DESTROY | UI_ELEMENT_HIDE;

	UIElement *ancestor = element->parent;

	while (ancestor) {
		ancestor->flags |= UI_ELEMENT_DESTROY_DESCENDENT;
		ancestor = ancestor->parent;
	}

	UIElement *child = element->children;

	while (child) {
		UIElementDestroy(child);
		child = child->next;
	}

#ifdef UI_DEBUG
	_UIInspectorRefresh();
#endif
}

void UIDrawBlock(UIPainter *painter, UIRectangle rectangle, uint32_t color) {
	rectangle = UIRectangleIntersection(painter->clip, rectangle);

	if (!UI_RECT_VALID(rectangle)) {
		return;
	}

	for (int line = rectangle.t; line < rectangle.b; line++) {
		uint32_t *bits = painter->bits + line * painter->width + rectangle.l;
		int count = UI_RECT_WIDTH(rectangle);

		while (count--) {
			*bits++ = color;
		}
	}
}

void UIDrawInvert(UIPainter *painter, UIRectangle rectangle) {
	rectangle = UIRectangleIntersection(painter->clip, rectangle);

	if (!UI_RECT_VALID(rectangle)) {
		return;
	}

	for (int line = rectangle.t; line < rectangle.b; line++) {
		uint32_t *bits = painter->bits + line * painter->width + rectangle.l;
		int count = UI_RECT_WIDTH(rectangle);

		while (count--) {
			uint32_t in = *bits;
			*bits = in ^ 0xFFFFFF;
			bits++;
		}
	}
}

void UIDrawGlyph(UIPainter *painter, int x, int y, int c, uint32_t color) {
	if (c < 0 || c > 127) c = '?';

	UIRectangle rectangle = UIRectangleIntersection(painter->clip, UI_RECT_4(x, x + 8, y, y + 16));

	const uint8_t *data = (const uint8_t *) _uiFont + c * 16;

	for (int i = rectangle.t; i < rectangle.b; i++) {
		uint32_t *bits = painter->bits + i * painter->width + rectangle.l;
		uint8_t byte = data[i - y];

		for (int j = rectangle.l; j < rectangle.r; j++) {
			if (byte & (1 << (j - x))) {
				*bits = color;
			}

			bits++;
		}
	}
}

ptrdiff_t _UIStringLength(const char *cString) {
	if (!cString) return 0;
	ptrdiff_t length;
	for (length = 0; cString[length]; length++);
	return length;
}

char *_UIStringCopy(const char *in, ptrdiff_t inBytes) {
	if (inBytes == -1) {
		inBytes = _UIStringLength(in);
	}

	char *buffer = (char *) UI_MALLOC(inBytes + 1);
	
	for (intptr_t i = 0; i < inBytes; i++) {
		buffer[i] = in[i];
	}
	
	buffer[inBytes] = 0;
	return buffer;
}

int UIMeasureStringWidth(const char *string, ptrdiff_t bytes) {
	if (bytes == -1) {
		bytes = _UIStringLength(string);
	}
	
	return bytes * UI_SIZE_GLYPH_WIDTH;
}

int UIMeasureStringHeight() {
	return UI_SIZE_GLYPH_HEIGHT;
}

void UIDrawString(UIPainter *painter, UIRectangle r, const char *string, ptrdiff_t bytes, uint32_t color, int align, UIStringSelection *selection) {
	UIRectangle oldClip = painter->clip;
	painter->clip = UIRectangleIntersection(r, oldClip);

	if (!UI_RECT_VALID(painter->clip)) {
		painter->clip = oldClip;
		return;
	}

	if (bytes == -1) {
		bytes = _UIStringLength(string);
	}

	int width = UIMeasureStringWidth(string, bytes);
	int height = UIMeasureStringHeight();
	int x = align == UI_ALIGN_CENTER ? ((r.l + r.r - width) / 2) : align == UI_ALIGN_RIGHT ? (r.r - width) : r.l;
	int y = (r.t + r.b - height) / 2;
	int i = 0, j = 0;

	int selectFrom = -1, selectTo = -1;

	if (selection) {
		selectFrom = selection->carets[0];
		selectTo = selection->carets[1];
		
		if (selectFrom > selectTo) {
			UI_SWAP(int, selectFrom, selectTo);
		}
	}


	for (; j < bytes; j++) {
		char c = *string++;
		uint32_t colorText = color;

		if (j >= selectFrom && j < selectTo) {
			UIDrawBlock(painter, UI_RECT_4(x, x + UI_SIZE_GLYPH_WIDTH, y, y + height), selection->colorBackground);
			colorText = selection->colorText;
		}

		if (c != '\t') {
			UIDrawGlyph(painter, x, y, c, colorText);
		}

		if (selection && selection->carets[0] == j) {
			UIDrawInvert(painter, UI_RECT_4(x, x + 1, y, y + height));
		}

		x += UI_SIZE_GLYPH_WIDTH, i++;

		if (c == '\t') {
			while (i & 3) x += UI_SIZE_GLYPH_WIDTH, i++;
		}
	}

	if (selection && selection->carets[0] == j) {
		UIDrawInvert(painter, UI_RECT_4(x, x + 1, y, y + height));
	}

	painter->clip = oldClip;
}

void UIDrawRectangle(UIPainter *painter, UIRectangle r, uint32_t mainColor, uint32_t borderColor, UIRectangle borderSize) {
	UIDrawBlock(painter, UI_RECT_4(r.l, r.r, r.t, r.t + borderSize.t), borderColor);
	UIDrawBlock(painter, UI_RECT_4(r.l, r.l + borderSize.l, r.t + borderSize.t, r.b - borderSize.b), borderColor);
	UIDrawBlock(painter, UI_RECT_4(r.l + borderSize.l, r.r - borderSize.r, r.t + borderSize.t, r.b - borderSize.b), mainColor);
	UIDrawBlock(painter, UI_RECT_4(r.r - borderSize.r, r.r, r.t + borderSize.t, r.b - borderSize.b), borderColor);
	UIDrawBlock(painter, UI_RECT_4(r.l, r.r, r.b - borderSize.b, r.b), borderColor);
}

void UIElementMove(UIElement *element, UIRectangle bounds, bool alwaysLayout) {
	element->clip = UIRectangleIntersection(element->parent->clip, bounds);

	if (!UIRectangleEquals(element->bounds, bounds) || alwaysLayout) {
		element->bounds = bounds;
		UIElementMessage(element, UI_MSG_LAYOUT, 0, 0);
	}
}

int UIElementMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message != UI_MSG_DESTROY && (element->flags & UI_ELEMENT_DESTROY)) {
		return 0;
	}

	if (element->messageUser) {
		int result = element->messageUser(element, message, di, dp);

		if (result) {
			return result;
		}
	}

	if (element->messageClass) {
		return element->messageClass(element, message, di, dp);
	} else {
		return 0;
	}
}

UIElement *_UIElementSetup(size_t bytes, UIElement *parent, uint64_t flags, int (*message)(UIElement *, UIMessage, int, void *), const char *cClassName) {
	UI_ASSERT(bytes >= sizeof(UIElement));
	UIElement *element = (UIElement *) UI_CALLOC(bytes);
	element->flags = flags;
	element->parent = parent;
	element->messageClass = message;

	if (parent) {
		element->window = parent->window;
		element->parent = parent;

		if (parent->children) {
			UIElement *sibling = parent->children;

			while (sibling->next) {
				sibling = sibling->next;
			}

			sibling->next = element;
		} else {
			parent->children = element;
		}

		UI_ASSERT(~parent->flags & UI_ELEMENT_DESTROY);
	}

#ifdef UI_DEBUG
	element->cClassName = cClassName;
	static int id = 0;
	element->id = ++id;
	_UIInspectorRefresh();
#endif

	return element;
}

int _UIPanelMeasure(UIPanel *panel) {
	bool horizontal = panel->e.flags & UI_PANEL_HORIZONTAL;
	int size = 0;
	UIElement *child = panel->e.children;

	while (child) {
		if (~child->flags & UI_ELEMENT_HIDE) {
			if (horizontal) {
				int height = UIElementMessage(child, UI_MSG_GET_HEIGHT, 0, 0);

				if (height > size) {
					size = height;
				}
			} else {
				int width = UIElementMessage(child, UI_MSG_GET_WIDTH, 0, 0);

				if (width > size) {
					size = width;
				}
			}
		}

		child = child->next;
	}

	int border = 0;

	if (horizontal) {
		border = panel->border.t + panel->border.b;
	} else {
		border = panel->border.l + panel->border.r;
	}

	return size + border * panel->e.window->scale;
}

int _UIPanelLayout(UIPanel *panel, UIRectangle bounds, bool measure) {
	bool horizontal = panel->e.flags & UI_PANEL_HORIZONTAL;
	float scale = panel->e.window->scale;
	int position = (horizontal ? panel->border.l : panel->border.t) * scale;
	int hSpace = UI_RECT_WIDTH(bounds) - UI_RECT_TOTAL_H(panel->border) * scale;
	int vSpace = UI_RECT_HEIGHT(bounds) - UI_RECT_TOTAL_V(panel->border) * scale;

	int available = horizontal ? hSpace : vSpace;
	int fill = 0, count = 0, perFill = 0;

	UIElement *child = panel->e.children;

	while (child) {
		if (~child->flags & UI_ELEMENT_HIDE) {
			count++;

			if (horizontal) {
				if (child->flags & UI_ELEMENT_H_FILL) {
					fill++;
				} else {
					available -= UIElementMessage(child, UI_MSG_GET_WIDTH, vSpace, 0);
				}
			} else {
				if (child->flags & UI_ELEMENT_V_FILL) {
					fill++;
				} else {
					available -= UIElementMessage(child, UI_MSG_GET_HEIGHT, hSpace, 0);
				}
			}
		}

		child = child->next;
	}

	if (count) {
		available -= (count - 1) * (int) (panel->gap * scale);
	}

	if (available > 0 && fill) {
		perFill = available / fill;
	}

	child = panel->e.children;

	bool expand = panel->e.flags & UI_PANEL_EXPAND;

	while (child) {
		if (~child->flags & UI_ELEMENT_HIDE) {
			if (horizontal) {
				int height = ((child->flags & UI_ELEMENT_V_FILL) || expand) ? vSpace : UIElementMessage(child, UI_MSG_GET_HEIGHT, 0, 0);
				int width = (child->flags & UI_ELEMENT_H_FILL) ? perFill : UIElementMessage(child, UI_MSG_GET_WIDTH, height, 0);
				UIRectangle relative = UI_RECT_4(position, position + width, 
						panel->border.t + (vSpace - height) / 2, 
						panel->border.t + (vSpace + height) / 2);
				if (!measure) UIElementMove(child, UIRectangleTranslate(relative, bounds), false);
				position += width + panel->gap * scale;
			} else {
				int width = ((child->flags & UI_ELEMENT_H_FILL) || expand) ? hSpace : UIElementMessage(child, UI_MSG_GET_WIDTH, 0, 0);
				int height = (child->flags & UI_ELEMENT_V_FILL) ? perFill : UIElementMessage(child, UI_MSG_GET_HEIGHT, width, 0);
				UIRectangle relative = UI_RECT_4(panel->border.l + (hSpace - width) / 2, 
						panel->border.l + (hSpace + width) / 2, position, position + height);
				if (!measure) UIElementMove(child, UIRectangleTranslate(relative, bounds), false);
				position += height + panel->gap * scale;
			}
		}

		child = child->next;
	}

	return position - panel->gap * scale + (horizontal ? panel->border.r : panel->border.b) * scale;
}

int _UIPanelMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIPanel *panel = (UIPanel *) element;
	bool horizontal = panel->e.flags & UI_PANEL_HORIZONTAL;

	if (message == UI_MSG_LAYOUT) {
		_UIPanelLayout(panel, element->bounds, false);
	} else if (message == UI_MSG_GET_WIDTH && horizontal) {
		if (horizontal) {
			return _UIPanelLayout(panel, UI_RECT_4(0, 0, 0, di), true);
		} else {
			return _UIPanelMeasure(panel);
		}
	} else if (message == UI_MSG_GET_HEIGHT) {
		if (horizontal) {
			return _UIPanelMeasure(panel);
		} else {
			return _UIPanelLayout(panel, UI_RECT_4(0, di, 0, 0), true);
		}
	} else if (message == UI_MSG_PAINT) {
		if (element->flags & UI_PANEL_GRAY) {
			UIDrawBlock((UIPainter *) dp, element->bounds, UI_COLOR_PANEL_GRAY);
		} else if (element->flags & UI_PANEL_WHITE) {
			UIDrawBlock((UIPainter *) dp, element->bounds, UI_COLOR_PANEL_WHITE);
		}
	}

	return 0;
}

UIPanel *UIPanelCreate(UIElement *parent, uint64_t flags) {
	return (UIPanel *) _UIElementSetup(sizeof(UIPanel), parent, flags, _UIPanelMessage, "Panel");
}

int _UIButtonMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIButton *button = (UIButton *) element;
	bool isMenuItem = element->flags & UI_BUTTON_MENU_ITEM;
	
	if (message == UI_MSG_GET_HEIGHT) {
		if (isMenuItem) {
			return UI_SIZE_MENU_ITEM_HEIGHT * element->window->scale;
		} else {
			return UI_SIZE_BUTTON_HEIGHT * element->window->scale;
		}
	} else if (message == UI_MSG_GET_WIDTH) {
		int labelSize = UIMeasureStringWidth(button->label, button->labelBytes);
		int paddedSize = labelSize + UI_SIZE_BUTTON_PADDING * element->window->scale;
		int minimumSize = ((element->flags & UI_BUTTON_SMALL) ? 0 
				: isMenuItem ? UI_SIZE_MENU_ITEM_MINIMUM_WIDTH 
				: UI_SIZE_BUTTON_MINIMUM_WIDTH) 
			* element->window->scale;
		return paddedSize > minimumSize ? paddedSize : minimumSize;
	} else if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		bool focused = element == element->window->focused;
		bool pressed = element == element->window->pressed;
		bool hovered = element == element->window->hovered;
		uint32_t color = (pressed && hovered) ? UI_COLOR_BUTTON_PRESSED 
			: (pressed || hovered) ? UI_COLOR_BUTTON_HOVERED 
			: focused ? UI_COLOR_BUTTON_FOCUSED : UI_COLOR_BUTTON_NORMAL;
		UIDrawRectangle(painter, element->bounds, color, UI_COLOR_BORDER, UI_RECT_1(isMenuItem ? 0 : 1));

		if (isMenuItem) {
			UIRectangle bounds = UIRectangleAdd(element->bounds, UI_RECT_2I((int) (UI_SIZE_MENU_ITEM_MARGIN * element->window->scale), 0));

			if (button->labelBytes == -1) {
				button->labelBytes = _UIStringLength(button->label);
			}

			int tab = 0;
			for (; tab < button->labelBytes && button->label[tab] != '\t'; tab++);

			UIDrawString(painter, bounds, button->label, tab, UI_COLOR_TEXT, UI_ALIGN_LEFT, NULL);

			if (button->labelBytes > tab) {
				UIDrawString(painter, bounds, button->label + tab + 1, button->labelBytes - tab - 1, UI_COLOR_TEXT, UI_ALIGN_RIGHT, NULL);
			}
		} else {
			UIDrawString(painter, element->bounds, button->label, button->labelBytes, UI_COLOR_TEXT, UI_ALIGN_CENTER, NULL);
		}
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_DESTROY) {
		UI_FREE(button->label);
	} else if (message == UI_MSG_LEFT_DOWN) {
		if (element->flags & UI_BUTTON_CAN_FOCUS) {
			UIElementFocus(element);
		}
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;
		
		if (m->code == UI_KEYCODE_SPACE) {
			UIElementMessage(element, UI_MSG_CLICKED, 0, 0);
		}
	} else if (message == UI_MSG_CLICKED) {
		if (button->invoke) {
			button->invoke(element->cp);
		}
	}

	return 0;
}

UIButton *UIButtonCreate(UIElement *parent, uint64_t flags, const char *label, ptrdiff_t labelBytes) {
	UIButton *button = (UIButton *) _UIElementSetup(sizeof(UIButton), parent, flags, _UIButtonMessage, "Button");
	button->label = _UIStringCopy(label, (button->labelBytes = labelBytes));
	return button;
}

int _UILabelMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UILabel *label = (UILabel *) element;
	
	if (message == UI_MSG_GET_HEIGHT) {
		return UIMeasureStringHeight();
	} else if (message == UI_MSG_GET_WIDTH) {
		return UIMeasureStringWidth(label->label, label->labelBytes);
	} else if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		UIDrawString(painter, element->bounds, label->label, label->labelBytes, UI_COLOR_TEXT, UI_ALIGN_LEFT, NULL);
	} else if (message == UI_MSG_DESTROY) {
		UI_FREE(label->label);
	}

	return 0;
}

void UILabelSetContent(UILabel *label, const char *string, ptrdiff_t stringBytes) {
	UI_FREE(label->label);
	label->label = _UIStringCopy(string, (label->labelBytes = stringBytes));
}

UILabel *UILabelCreate(UIElement *parent, uint64_t flags, const char *string, ptrdiff_t stringBytes) {
	UILabel *label = (UILabel *) _UIElementSetup(sizeof(UILabel), parent, flags, _UILabelMessage, "Label");
	label->label = _UIStringCopy(string, (label->labelBytes = stringBytes));
	return label;
}

int _UISplitterMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UISplitPane *splitPane = (UISplitPane *) element->parent;
	bool vertical = splitPane->e.flags & UI_SPLIT_PANE_VERTICAL;

	if (message == UI_MSG_PAINT) {
		UIRectangle borders = vertical ? UI_RECT_2(0, 1) : UI_RECT_2(1, 0);
		UIDrawRectangle((UIPainter *) dp, element->bounds, UI_COLOR_BUTTON_NORMAL, UI_COLOR_BORDER, borders);
	} else if (message == UI_MSG_GET_CURSOR) {
		return vertical ? UI_CURSOR_SPLIT_V : UI_CURSOR_SPLIT_H;
	} else if (message == UI_MSG_MOUSE_DRAG) {
		int cursor = vertical ? element->window->cursorY : element->window->cursorX;
		int splitterSize = UI_SIZE_SPLITTER * element->window->scale;
		int space = (vertical ? UI_RECT_HEIGHT(splitPane->e.bounds) : UI_RECT_WIDTH(splitPane->e.bounds)) - splitterSize;
		splitPane->weight = (float) (cursor - splitterSize / 2) / space;
		if (splitPane->weight < 0.05f) splitPane->weight = 0.05f;
		if (splitPane->weight > 0.95f) splitPane->weight = 0.95f;
		UIElementRefresh(&splitPane->e);
	}

	return 0;
}

int _UISplitPaneMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UISplitPane *splitPane = (UISplitPane *) element;
	bool vertical = splitPane->e.flags & UI_SPLIT_PANE_VERTICAL;

	if (message == UI_MSG_LAYOUT) {
		UIElement *splitter = element->children;
		UI_ASSERT(splitter);
		UIElement *left = splitter->next;
		UI_ASSERT(left);
		UIElement *right = left->next;
		UI_ASSERT(right);
		UI_ASSERT(!right->next);

		int splitterSize = UI_SIZE_SPLITTER * element->window->scale;
		int space = (vertical ? UI_RECT_HEIGHT(element->bounds) : UI_RECT_WIDTH(element->bounds)) - splitterSize;
		int leftSize = space * splitPane->weight;
		int rightSize = space - leftSize;

		if (vertical) {
			UIElementMove(left, UI_RECT_4(element->bounds.l, element->bounds.r, element->bounds.t, element->bounds.t + leftSize), false);
			UIElementMove(splitter, UI_RECT_4(element->bounds.l, element->bounds.r, element->bounds.t + leftSize, element->bounds.t + leftSize + splitterSize), false);
			UIElementMove(right, UI_RECT_4(element->bounds.l, element->bounds.r, element->bounds.b - rightSize, element->bounds.b), false);
		} else {
			UIElementMove(left, UI_RECT_4(element->bounds.l, element->bounds.l + leftSize, element->bounds.t, element->bounds.b), false);
			UIElementMove(splitter, UI_RECT_4(element->bounds.l + leftSize, element->bounds.l + leftSize + splitterSize, element->bounds.t, element->bounds.b), false);
			UIElementMove(right, UI_RECT_4(element->bounds.r - rightSize, element->bounds.r, element->bounds.t, element->bounds.b), false);
		}
	}

	return 0;
}

UISplitPane *UISplitPaneCreate(UIElement *parent, uint64_t flags, float weight) {
	UISplitPane *splitPane = (UISplitPane *) _UIElementSetup(sizeof(UISplitPane), parent, flags, _UISplitPaneMessage, "Split Pane");
	splitPane->weight = weight;
	_UIElementSetup(sizeof(UIElement), &splitPane->e, 0, _UISplitterMessage, "Splitter");
	return splitPane;
}

int _UITabPaneMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UITabPane *tabPane = (UITabPane *) element;
	
	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		UIRectangle top = element->bounds;
		top.b = top.t + UI_SIZE_BUTTON_HEIGHT;
		UIDrawRectangle(painter, top, UI_COLOR_PANEL_GRAY, UI_COLOR_BORDER, UI_RECT_4(0, 0, 0, 1));

		UIRectangle tab = top;
		tab.l += UI_SIZE_TAB_PANE_SPACE_LEFT * element->window->scale;
		tab.t += UI_SIZE_TAB_PANE_SPACE_TOP * element->window->scale;

		int position = 0;
		int index = 0;

		while (true) {
			int end = position;
			for (; tabPane->tabs[end] != '\t' && tabPane->tabs[end]; end++);

			int width = UIMeasureStringWidth(tabPane->tabs, end - position);
			tab.r = tab.l + width + UI_SIZE_BUTTON_PADDING;

			uint32_t color = tabPane->active == index ? UI_COLOR_BUTTON_PRESSED : UI_COLOR_BUTTON_NORMAL;

			UIRectangle t = tab;

			if (tabPane->active == index) {
				t.b++;
				t.t--;
			} else {
				t.t++;
			}

			UIDrawRectangle(painter, t, color, UI_COLOR_BORDER, UI_RECT_1(1));
			UIDrawString(painter, tab, tabPane->tabs + position, end - position, UI_COLOR_TEXT, UI_ALIGN_CENTER, NULL);
			tab.l = tab.r - 1;

			if (tabPane->tabs[end] == '\t') {
				position = end + 1;
				index++;
			} else {
				break;
			}
		}
	} else if (message == UI_MSG_LEFT_DOWN) {
		UIRectangle tab = element->bounds;
		tab.b = tab.t + UI_SIZE_BUTTON_HEIGHT;
		tab.l += UI_SIZE_TAB_PANE_SPACE_LEFT * element->window->scale;
		tab.t += UI_SIZE_TAB_PANE_SPACE_TOP * element->window->scale;

		int position = 0;
		int index = 0;

		while (true) {
			int end = position;
			for (; tabPane->tabs[end] != '\t' && tabPane->tabs[end]; end++);

			int width = UIMeasureStringWidth(tabPane->tabs, end - position);
			tab.r = tab.l + width + UI_SIZE_BUTTON_PADDING;

			if (UIRectangleContains(tab, element->window->cursorX, element->window->cursorY)) {
				tabPane->active = index;
				UIElementMessage(element, UI_MSG_LAYOUT, 0, 0);
				UIElementRepaint(element, NULL);
				break;
			}

			tab.l = tab.r - 1;

			if (tabPane->tabs[end] == '\t') {
				position = end + 1;
				index++;
			} else {
				break;
			}
		}
	} else if (message == UI_MSG_LAYOUT) {
		UIElement *child = element->children;
		int index = 0;

		UIRectangle content = element->bounds;
		content.t += UI_SIZE_BUTTON_HEIGHT;

		while (child) {
			if (tabPane->active == index) {
				child->flags &= ~UI_ELEMENT_HIDE;
				UIElementMove(child, content, false);
			} else {
				child->flags |= UI_ELEMENT_HIDE;
			}

			child = child->next;
			index++;
		}
	} else if (message == UI_MSG_DESTROY) {
		UI_FREE(tabPane->tabs);
	}

	return 0;
}

UITabPane *UITabPaneCreate(UIElement *parent, uint64_t flags, const char *tabs) {
	UITabPane *tabPane = (UITabPane *) _UIElementSetup(sizeof(UITabPane), parent, flags, _UITabPaneMessage, "Tab Pane");
	tabPane->tabs = _UIStringCopy(tabs, -1);
	return tabPane;
}

int _UISpacerMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UISpacer *spacer = (UISpacer *) element;
	
	if (message == UI_MSG_GET_HEIGHT) {
		return spacer->height * element->window->scale;
	} else if (message == UI_MSG_GET_WIDTH) {
		return spacer->width * element->window->scale;
	} else if (message == UI_MSG_PAINT && (element->flags & UI_SPACER_LINE)) {
		UIDrawBlock((UIPainter *) dp, element->bounds, UI_COLOR_BORDER);
	}

	return 0;
}

UISpacer *UISpacerCreate(UIElement *parent, uint64_t flags, int width, int height) {
	UISpacer *spacer = (UISpacer *) _UIElementSetup(sizeof(UISpacer), parent, flags, _UISpacerMessage, "Spacer");
	spacer->width = width;
	spacer->height = height;
	return spacer;
}

int _UIScrollBarMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIScrollBar *scrollBar = (UIScrollBar *) element;

	if (message == UI_MSG_GET_WIDTH || message == UI_MSG_GET_HEIGHT) {
		return UI_SIZE_SCROLL_BAR * element->window->scale;
	} else if (message == UI_MSG_LAYOUT) {
		UIElement *up = element->children;
		UIElement *thumb = up->next;
		UIElement *down = thumb->next;

		if (scrollBar->page >= scrollBar->maximum || scrollBar->maximum <= 0 || scrollBar->page <= 0) {
			up->flags |= UI_ELEMENT_HIDE;
			thumb->flags |= UI_ELEMENT_HIDE;
			down->flags |= UI_ELEMENT_HIDE;

			scrollBar->position = 0;
		} else {
			up->flags &= ~UI_ELEMENT_HIDE;
			thumb->flags &= ~UI_ELEMENT_HIDE;
			down->flags &= ~UI_ELEMENT_HIDE;

			int size = UI_RECT_HEIGHT(element->bounds);
			int thumbSize = size * scrollBar->page / scrollBar->maximum;

			if (thumbSize < UI_SIZE_SCROLL_MINIMUM_THUMB) {
				thumbSize = UI_SIZE_SCROLL_MINIMUM_THUMB;
			}

			if (scrollBar->position < 0) {
				scrollBar->position = 0;
			} else if (scrollBar->position > scrollBar->maximum - scrollBar->page) {
				scrollBar->position = scrollBar->maximum - scrollBar->page;
			}

			int thumbPosition = scrollBar->position / (scrollBar->maximum - scrollBar->page) * (size - thumbSize);

			if (scrollBar->position == scrollBar->maximum - scrollBar->page) {
				thumbPosition = size - thumbSize;
			}

			UIRectangle r = element->bounds;
			r.b = r.t + thumbPosition;
			UIElementMove(up, r, false);
			r.t = r.b, r.b = r.t + thumbSize;
			UIElementMove(thumb, r, false);
			r.t = r.b, r.b = element->bounds.b;
			UIElementMove(down, r, false);
		}
	} else if (message == UI_MSG_PAINT && (scrollBar->page >= scrollBar->maximum || scrollBar->maximum <= 0 || scrollBar->page <= 0)) {
		UIDrawBlock((UIPainter *) dp, element->bounds, UI_COLOR_PANEL_WHITE);
	} else if (message == UI_MSG_MOUSE_WHEEL) {
		scrollBar->position += di;
		UIElementRefresh(element);
		UIElementMessage(element->parent, UI_MSG_SCROLLED, 0, 0);
		return 1;
	}

	return 0;
}

int _UIScrollUpDownMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIScrollBar *scrollBar = (UIScrollBar *) element->parent;
	bool isDown = element->cp;

	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		uint32_t color = element == element->window->pressed ? UI_COLOR_BUTTON_PRESSED 
			: element == element->window->hovered ? UI_COLOR_BUTTON_HOVERED : UI_COLOR_PANEL_WHITE;
		UIDrawRectangle(painter, element->bounds, color, UI_COLOR_BORDER, UI_RECT_1(0));
		UIDrawGlyph(painter, element->bounds.l + 6, 
			isDown ? element->bounds.b - 18 : element->bounds.t + 2, 
			isDown ? 25 : 24, UI_COLOR_SCROLL_GLYPH);
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_LEFT_DOWN && !ui.animating) {
		ui.animating = element;
		scrollBar->lastAnimateTime = UI_CLOCK();
	} else if (message == UI_MSG_LEFT_UP) {
		ui.animating = NULL;
	} else if (message == UI_MSG_ANIMATE) {
		UI_CLOCK_T previous = scrollBar->lastAnimateTime;
		UI_CLOCK_T current = UI_CLOCK();
		UI_CLOCK_T delta = current - previous;
		double deltaSeconds = (double) delta / UI_CLOCKS_PER_SECOND;
		if (deltaSeconds > 0.1) deltaSeconds = 0.1;
		double deltaPixels = deltaSeconds * scrollBar->page * 3;
		scrollBar->lastAnimateTime = current;
		if (isDown) scrollBar->position += deltaPixels;
		else scrollBar->position -= deltaPixels;
		UIElementRefresh(&scrollBar->e);
		UIElementMessage(scrollBar->e.parent, UI_MSG_SCROLLED, 0, 0);
	}

	return 0;
}

int _UIScrollThumbMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIScrollBar *scrollBar = (UIScrollBar *) element->parent;

	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		uint32_t color = element == element->window->pressed ? UI_COLOR_SCROLL_THUMB_PRESSED 
			: element == element->window->hovered ? UI_COLOR_SCROLL_THUMB_HOVERED : UI_COLOR_SCROLL_THUMB_NORMAL;
		UIDrawRectangle(painter, element->bounds, color, UI_COLOR_BORDER, UI_RECT_1(0));
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_MOUSE_DRAG && element->window->pressedButton == 1) {
		if (!scrollBar->inDrag) {
			scrollBar->inDrag = true;
			scrollBar->dragOffset = element->bounds.t - scrollBar->e.bounds.t - element->window->cursorY;
		}

		int thumbPosition = element->window->cursorY + scrollBar->dragOffset;
		scrollBar->position = (double) thumbPosition 
			/ (UI_RECT_HEIGHT(scrollBar->e.bounds) - UI_RECT_HEIGHT(element->bounds)) 
			* (scrollBar->maximum - scrollBar->page);
		UIElementRefresh(&scrollBar->e);
		UIElementMessage(scrollBar->e.parent, UI_MSG_SCROLLED, 0, 0);
	} else if (message == UI_MSG_LEFT_UP) {
		scrollBar->inDrag = false;
	}

	return 0;
}

UIScrollBar *UIScrollBarCreate(UIElement *parent, uint64_t flags) {
	UIScrollBar *scrollBar = (UIScrollBar *) _UIElementSetup(sizeof(UIScrollBar), parent, flags, _UIScrollBarMessage, "Scroll Bar");
	_UIElementSetup(sizeof(UIElement), &scrollBar->e, flags, _UIScrollUpDownMessage, "Scroll Up")->cp = (void *) (uintptr_t) 0;
	_UIElementSetup(sizeof(UIElement), &scrollBar->e, flags, _UIScrollThumbMessage, "Scroll Thumb");
	_UIElementSetup(sizeof(UIElement), &scrollBar->e, flags, _UIScrollUpDownMessage, "Scroll Down")->cp = (void *) (uintptr_t) 1;
	return scrollBar;
}

bool _UICharIsAlpha(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool _UICharIsDigit(char c) {
	return c >= '0' && c <= '9';
}

bool _UICharIsAlphaOrDigitOrUnderscore(char c) {
	return _UICharIsAlpha(c) || _UICharIsDigit(c) || c == '_';
}

int UICodeHitTest(UICode *code, int x, int y) {
	x -= code->e.bounds.l;

	if (x < 0 || x >= UI_RECT_WIDTH(code->e.bounds) - UI_SIZE_SCROLL_BAR) {
		return 0;
	}

	y -= code->e.bounds.t - code->vScroll->position;

	int lineHeight = UIMeasureStringHeight();

	if (y < 0 || y >= lineHeight * code->lineCount) {
		return 0;
	}

	int line = y / lineHeight + 1;

	if (x < UI_SIZE_CODE_MARGIN && (~code->e.flags & UI_CODE_NO_MARGIN)) {
		return -line;
	} else {
		return line;
	}
}

int _UICodeMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UICode *code = (UICode *) element;
	
	if (message == UI_MSG_LAYOUT) {
		if (code->moveScrollToFocusNextLayout) {
			code->vScroll->position = (code->focused + 0.5) * UIMeasureStringHeight() - UI_RECT_HEIGHT(code->e.bounds) / 2;
		}

		UIRectangle scrollBarBounds = element->bounds;
		scrollBarBounds.l = scrollBarBounds.r - UI_SIZE_SCROLL_BAR;
		code->vScroll->maximum = code->lineCount * UIMeasureStringHeight();
		code->vScroll->page = UI_RECT_HEIGHT(element->bounds);
		UIElementMove(&code->vScroll->e, scrollBarBounds, true);
	} else if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		UIRectangle lineBounds = element->bounds;
		lineBounds.r -= UI_SIZE_SCROLL_BAR;

		if (~code->e.flags & UI_CODE_NO_MARGIN) {
			lineBounds.l += UI_SIZE_CODE_MARGIN + UI_SIZE_CODE_MARGIN_GAP;
		}

		int lineHeight = UIMeasureStringHeight();
		lineBounds.t -= (int64_t) code->vScroll->position % lineHeight;

		UIDrawBlock(painter, element->bounds, UI_COLOR_CODE_BACKGROUND);

		uint32_t colors[] = {
			UI_COLOR_CODE_DEFAULT,
			UI_COLOR_CODE_COMMENT,
			UI_COLOR_CODE_STRING,
			UI_COLOR_CODE_NUMBER,
			UI_COLOR_CODE_OPERATOR,
			UI_COLOR_CODE_PREPROCESSOR,
		};

		for (int i = code->vScroll->position / lineHeight; i < code->lineCount; i++) {
			if (lineBounds.t > element->clip.b) {
				break;
			}

			lineBounds.b = lineBounds.t + lineHeight;

			if (~code->e.flags & UI_CODE_NO_MARGIN) {
				char string[16];
				int p = 16;
				int lineNumber = i + 1;

				while (lineNumber) {
					string[--p] = (lineNumber % 10) + '0';
					lineNumber /= 10;
				}

				UIRectangle marginBounds = lineBounds;
				marginBounds.r = marginBounds.l - UI_SIZE_CODE_MARGIN_GAP;
				marginBounds.l -= UI_SIZE_CODE_MARGIN + UI_SIZE_CODE_MARGIN_GAP;

				uint32_t marginColor = UIElementMessage(element, UI_MSG_CODE_GET_MARGIN_COLOR, i + 1, 0);

				if (marginColor) {
					UIDrawBlock(painter, marginBounds, marginColor);
				}

				UIDrawString(painter, marginBounds, string + p, 16 - p, UI_COLOR_CODE_DEFAULT, UI_ALIGN_RIGHT, NULL);
			}

			if (code->focused == i) {
				UIDrawBlock(painter, lineBounds, UI_COLOR_CODE_FOCUSED);
			}

			const char *string = code->content + code->lines[i].offset;
			size_t bytes = code->lines[i].bytes;
			int x = lineBounds.l;
			int y = (lineBounds.t + lineBounds.b - UIMeasureStringHeight()) / 2;
			int ti = 0;
			int lexState = 0;
			bool inComment = false, inIdentifier = false, inChar = false, startedString = false;
			uint32_t last = 0;

			while (bytes--) {
				char c = *string++;

				last <<= 8;
				last |= c;

				if (lexState == 4) {
					lexState = 0;
				} else if (lexState == 1) {
					if ((last & 0xFF0000) == ('*' << 16) && (last & 0xFF00) == ('/' << 8) && inComment) {
						lexState = 0, inComment = false;
					}
				} else if (lexState == 3) {
					if (!_UICharIsAlpha(c) && !_UICharIsDigit(c)) {
						lexState = 0;
					}
				} else if (lexState == 2) {
					if (!startedString) {
						if (!inChar && ((last >> 8) & 0xFF) == '"' && ((last >> 16) & 0xFF) != '\\') {
							lexState = 0;
						} else if (inChar && ((last >> 8) & 0xFF) == '\'' && ((last >> 16) & 0xFF) != '\\') {
							lexState = 0;
						}
					}

					startedString = false;
				}

				if (lexState == 0) {
					if (c == '#') {
						lexState = 5;
					} else if (c == '/' && *string == '/') {
						lexState = 1;
					} else if (c == '/' && *string == '*') {
						lexState = 1, inComment = true;
					} else if (c == '"') {
						lexState = 2;
						inChar = false;
						startedString = true;
					} else if (c == '\'') {
						lexState = 2;
						inChar = true;
						startedString = true;
					} else if (_UICharIsDigit(c) && !inIdentifier) {
						lexState = 3;
					} else if (!_UICharIsAlpha(c) && !_UICharIsDigit(c)) {
						lexState = 4;
						inIdentifier = false;
					} else {
						inIdentifier = true;
					}
				}

				if (c == '\t') {
					x += UI_SIZE_GLYPH_WIDTH, ti++;
					while (ti & 3) x += UI_SIZE_GLYPH_WIDTH, ti++;
				} else {
					UIDrawGlyph(painter, x, y, c, colors[lexState]);
					x += UI_SIZE_GLYPH_WIDTH, ti++;
				}
			}

			lineBounds.t += lineHeight;
		}
	} else if (message == UI_MSG_SCROLLED) {
		code->moveScrollToFocusNextLayout = false;
		UIElementRefresh(element);
	} else if (message == UI_MSG_MOUSE_WHEEL) {
		return UIElementMessage(&code->vScroll->e, message, di, dp);
	} else if (message == UI_MSG_GET_CURSOR) {
		if (UICodeHitTest(code, element->window->cursorX, element->window->cursorY) < 0) {
			return UI_CURSOR_FLIPPED_ARROW;
		}
	} else if (message == UI_MSG_DESTROY) {
		UI_FREE(code->content);
		UI_FREE(code->lines);
	}

	return 0;
}

void UICodeFocusLine(UICode *code, int index) {
	code->focused = index - 1;
	code->moveScrollToFocusNextLayout = true;
}

void UICodeInsertContent(UICode *code, const char *content, ptrdiff_t byteCount, bool replace) {
	if (byteCount == -1) {
		byteCount = _UIStringLength(content);
	}

	if (byteCount > 1000000000) {
		byteCount = 1000000000;
	}

	if (replace) {
		UI_FREE(code->content);
		UI_FREE(code->lines);
		code->content = NULL;
		code->lines = NULL;
		code->contentBytes = 0;
		code->lineCount = 0;
	}

	code->content = (char *) UI_REALLOC(code->content, code->contentBytes + byteCount);

	if (!byteCount) {
		return;
	}

	int lineCount = content[byteCount - 1] != '\n';

	for (int i = 0; i < byteCount; i++) {
		code->content[i + code->contentBytes] = content[i];

		if (content[i] == '\n') {
			lineCount++;
		}
	}

	code->lines = (UICodeLine *) UI_REALLOC(code->lines, sizeof(UICodeLine) * (code->lineCount + lineCount));
	int offset = 0, lineIndex = 0;

	for (intptr_t i = 0; i <= byteCount && lineIndex < lineCount; i++) {
		if (content[i] == '\n' || i == byteCount) {
			UICodeLine line = { 0 };
			line.offset = offset + code->contentBytes;
			line.bytes = i - offset;
			code->lines[code->lineCount + lineIndex] = line;
			lineIndex++;
			offset = i + 1;
		}
	}

	code->lineCount += lineCount;
	code->contentBytes += byteCount;

	if (!replace) {
		code->vScroll->position = code->lineCount * UIMeasureStringHeight();
	}
}

UICode *UICodeCreate(UIElement *parent, uint64_t flags) {
	UICode *code = (UICode *) _UIElementSetup(sizeof(UICode), parent, flags, _UICodeMessage, "Code");
	code->vScroll = UIScrollBarCreate(&code->e, 0);
	code->focused = -1;
	return code;
}

int _UIGaugeMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIGauge *gauge = (UIGauge *) element;

	if (message == UI_MSG_GET_HEIGHT) {
		return UI_SIZE_GAUGE_HEIGHT * element->window->scale;
	} else if (message == UI_MSG_GET_WIDTH) {
		return UI_SIZE_GAUGE_WIDTH * element->window->scale;
	} else if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		UIDrawRectangle(painter, element->bounds, UI_COLOR_BUTTON_NORMAL, UI_COLOR_BORDER, UI_RECT_1(1));
		UIRectangle filled = UIRectangleAdd(element->bounds, UI_RECT_1I(1));
		filled.r = filled.l + UI_RECT_WIDTH(filled) * gauge->position;
		UIDrawBlock(painter, filled, UI_COLOR_GAUGE_FILLED);
	}

	return 0;
}

UIGauge *UIGaugeCreate(UIElement *parent, uint64_t flags) {
	return (UIGauge *) _UIElementSetup(sizeof(UIGauge), parent, flags, _UIGaugeMessage, "Gauge");
}

int _UISliderMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UISlider *slider = (UISlider *) element;

	if (message == UI_MSG_GET_HEIGHT) {
		return UI_SIZE_SLIDER_HEIGHT * element->window->scale;
	} else if (message == UI_MSG_GET_WIDTH) {
		return UI_SIZE_SLIDER_WIDTH * element->window->scale;
	} else if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		UIRectangle bounds = element->bounds;
		int centerY = (bounds.t + bounds.b) / 2;
		int trackSize = UI_SIZE_SLIDER_TRACK * element->window->scale;
		int thumbSize = UI_SIZE_SLIDER_THUMB * element->window->scale;
		int thumbPosition = (UI_RECT_WIDTH(bounds) - thumbSize) * slider->position;
		UIRectangle track = UI_RECT_4(bounds.l, bounds.r, centerY - (trackSize + 1) / 2, centerY + trackSize / 2);
		UIDrawRectangle(painter, track, UI_COLOR_BUTTON_NORMAL, UI_COLOR_BORDER, UI_RECT_1(1));
		bool pressed = element == element->window->pressed;
		bool hovered = element == element->window->hovered;
		uint32_t color = pressed ? UI_COLOR_BUTTON_PRESSED : hovered ? UI_COLOR_BUTTON_HOVERED : UI_COLOR_BUTTON_NORMAL;
		UIRectangle thumb = UI_RECT_4(bounds.l + thumbPosition, bounds.l + thumbPosition + thumbSize, centerY - (thumbSize + 1) / 2, centerY + thumbSize / 2);
		UIDrawRectangle(painter, thumb, color, UI_COLOR_BORDER, UI_RECT_1(1));
	} else if (message == UI_MSG_LEFT_DOWN || (message == UI_MSG_MOUSE_DRAG && element->window->pressedButton == 1)) {
		UIRectangle bounds = element->bounds;
		int thumbSize = UI_SIZE_SLIDER_THUMB * element->window->scale;
		slider->position = (float) (element->window->cursorX - thumbSize / 2 - bounds.l) / (UI_RECT_WIDTH(bounds) - thumbSize);
		if (slider->position < 0) slider->position = 0;
		if (slider->position > 1) slider->position = 1;
		UIElementMessage(element, UI_MSG_SLIDER_CHANGED, 0, 0);
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, NULL);
	}

	return 0;
}

UISlider *UISliderCreate(UIElement *parent, uint64_t flags) {
	return (UISlider *) _UIElementSetup(sizeof(UISlider), parent, flags, _UISliderMessage, "Slider");
}

int UITableHitTest(UITable *table, int x, int y) {
	x -= table->e.bounds.l;

	if (x < 0 || x >= UI_RECT_WIDTH(table->e.bounds) - UI_SIZE_SCROLL_BAR) {
		return -1;
	}

	y -= (table->e.bounds.t + UI_SIZE_TABLE_HEADER) - table->vScroll->position;

	int rowHeight = UIMeasureStringHeight();

	if (y < 0 || y >= rowHeight * table->itemCount) {
		return -1;
	}

	return y / rowHeight;
}

bool UITableEnsureVisible(UITable *table, int index) {
	int rowHeight = UIMeasureStringHeight();
	int y = index * rowHeight;
	y -= table->vScroll->position;
	int height = UI_RECT_HEIGHT(table->e.bounds) - UI_SIZE_TABLE_HEADER - rowHeight;

	if (y < 0) {
		table->vScroll->position += y;
		UIElementRefresh(&table->e);
		return true;
	} else if (y > height) {
		table->vScroll->position -= height - y;
		UIElementRefresh(&table->e);
		return true;
	} else {
		return false;
	}
}

void UITableResizeColumns(UITable *table) {
	int position = 0;
	int count = 0;

	while (true) {
		int end = position;
		for (; table->columns[end] != '\t' && table->columns[end]; end++);
		count++;
		if (table->columns[end] == '\t') position = end + 1;
		else break;
	}

	UI_FREE(table->columnWidths);
	table->columnWidths = (int *) UI_MALLOC(count * sizeof(int));
	table->columnCount = count;

	position = 0;

	char buffer[256];
	UITableGetItem m = { 0 };
	m.buffer = buffer;
	m.bufferBytes = sizeof(buffer);

	while (true) {
		int end = position;
		for (; table->columns[end] != '\t' && table->columns[end]; end++);

		int longest = 0;

		for (int i = 0; i < table->itemCount; i++) {
			m.index = i;
			int bytes = UIElementMessage(&table->e, UI_MSG_TABLE_GET_ITEM, 0, &m);
			int width = UIMeasureStringWidth(buffer, bytes);

			if (width > longest) {
				longest = width;
			}
		}

		table->columnWidths[m.column] = longest;
		m.column++;
		if (table->columns[end] == '\t') position = end + 1;
		else break;
	}
}

int _UITableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UITable *table = (UITable *) element;

	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		UIRectangle bounds = element->bounds;
		bounds.r -= UI_SIZE_SCROLL_BAR;
		UIDrawBlock(painter, bounds, UI_COLOR_PANEL_WHITE);
		char buffer[256];
		UIRectangle row = bounds;
		int rowHeight = UIMeasureStringHeight();
		UITableGetItem m = { 0 };
		m.buffer = buffer;
		m.bufferBytes = sizeof(buffer);
		row.t += UI_SIZE_TABLE_HEADER;
		row.t -= (int64_t) table->vScroll->position % rowHeight;
		int hovered = UITableHitTest(table, element->window->cursorX, element->window->cursorY);

		for (int i = table->vScroll->position / rowHeight; i < table->itemCount; i++) {
			if (row.t > element->clip.b) {
				break;
			}
			
			row.b = row.t + rowHeight;
			m.index = i;
			m.isSelected = false;
			m.column = 0;
			int bytes = UIElementMessage(element, UI_MSG_TABLE_GET_ITEM, 0, &m);
			uint32_t textColor = UI_COLOR_TEXT;

			if (m.isSelected) {
				UIDrawBlock(painter, row, UI_COLOR_TABLE_SELECTED);
				textColor = UI_COLOR_TABLE_SELECTED_TEXT;
			} else if (hovered == i) {
				UIDrawBlock(painter, row, UI_COLOR_TABLE_HOVERED);
				textColor = UI_COLOR_TABLE_HOVERED_TEXT;
			}

			UIRectangle cell = row;
			cell.l += UI_SIZE_TABLE_COLUMN_GAP;

			for (int j = 0; j < table->columnCount; j++) {
				if (j) {
					m.column = j;
					bytes = UIElementMessage(element, UI_MSG_TABLE_GET_ITEM, 0, &m);
				}

				cell.r = cell.l + table->columnWidths[j];
				UIDrawString(painter, cell, buffer, bytes, textColor, UI_ALIGN_LEFT, NULL);
				cell.l += table->columnWidths[j] + UI_SIZE_TABLE_COLUMN_GAP;
			}

			row.t += rowHeight;
		}

		UIRectangle header = bounds;
		header.b = header.t + UI_SIZE_TABLE_HEADER;
		UIDrawRectangle(painter, header, UI_COLOR_PANEL_GRAY, UI_COLOR_BORDER, UI_RECT_4(0, 0, 0, 1));
		header.l += UI_SIZE_TABLE_COLUMN_GAP;

		int position = 0;
		int index = 0;

		if (table->columnCount) {
			while (true) {
				int end = position;
				for (; table->columns[end] != '\t' && table->columns[end]; end++);

				header.r = header.l + table->columnWidths[index];
				UIDrawString(painter, header, table->columns + position, end - position, UI_COLOR_TEXT, UI_ALIGN_LEFT, NULL);
				header.l += table->columnWidths[index] + UI_SIZE_TABLE_COLUMN_GAP;

				if (table->columns[end] == '\t') {
					position = end + 1;
					index++;
				} else {
					break;
				}
			}
		}
	} else if (message == UI_MSG_LAYOUT) {
		UIRectangle scrollBarBounds = element->bounds;
		scrollBarBounds.l = scrollBarBounds.r - UI_SIZE_SCROLL_BAR;
		table->vScroll->maximum = table->itemCount * UIMeasureStringHeight();
		table->vScroll->page = UI_RECT_HEIGHT(element->bounds) - UI_SIZE_TABLE_HEADER;
		UIElementMove(&table->vScroll->e, scrollBarBounds, true);
	} else if (message == UI_MSG_MOUSE_MOVE) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_SCROLLED) {
		UIElementRefresh(element);
	} else if (message == UI_MSG_MOUSE_WHEEL) {
		return UIElementMessage(&table->vScroll->e, message, di, dp);
	} else if (message == UI_MSG_DESTROY) {
		UI_FREE(table->columns);
		UI_FREE(table->columnWidths);
	}

	return 0;
}

UITable *UITableCreate(UIElement *parent, uint64_t flags, const char *columns) {
	UITable *table = (UITable *) _UIElementSetup(sizeof(UITable), parent, flags, _UITableMessage, "Table");
	table->vScroll = UIScrollBarCreate(&table->e, 0);
	table->columns = _UIStringCopy(columns, -1);
	return table;
}

void UITextboxReplace(UITextbox *textbox, const char *text, ptrdiff_t bytes, bool sendChangedMessage) {
	if (bytes == -1) {
		bytes = _UIStringLength(text);
	}

	int deleteFrom = textbox->carets[0], deleteTo = textbox->carets[1];

	if (deleteFrom > deleteTo) {
		UI_SWAP(int, deleteFrom, deleteTo);
	}

	for (int i = deleteTo; i < textbox->bytes; i++) {
		textbox->string[i - deleteTo + deleteFrom] = textbox->string[i];
	}

	textbox->bytes -= deleteTo - deleteFrom;
	textbox->carets[0] = textbox->carets[1] = deleteFrom;

	textbox->string = (char *) UI_REALLOC(textbox->string, textbox->bytes + bytes);

	for (int i = textbox->carets[0] + bytes; i < textbox->bytes + bytes; i++) {
		textbox->string[i] = textbox->string[i - bytes];
	}

	for (int i = textbox->carets[0]; i < textbox->carets[0] + bytes; i++) {
		textbox->string[i] = text[i - textbox->carets[0]];
	}

	textbox->bytes += bytes;
	textbox->carets[0] += bytes;
	textbox->carets[1] = textbox->carets[0];

	UIElementMessage(&textbox->e, UI_MSG_TEXTBOX_CHANGED, 0, 0);
}

void UITextboxClear(UITextbox *textbox, bool sendChangedMessage) {
	textbox->carets[1] = 0;
	textbox->carets[0] = textbox->bytes;
	UITextboxReplace(textbox, "", 0, sendChangedMessage);
}

void UITextboxMoveCaret(UITextbox *textbox, bool backward, bool word) {
	while (true) {
		if (textbox->carets[0] > 0 && backward) {
			textbox->carets[0]--;
		} else if (textbox->carets[0] < textbox->bytes && !backward) {
			textbox->carets[0]++;
		} else {
			return;
		}

		if (!word) {
			return;
		} else if (textbox->carets[0] != textbox->bytes && textbox->carets[0] != 0) {
			char c1 = textbox->string[textbox->carets[0] - 1];
			char c2 = textbox->string[textbox->carets[0]];

			if (_UICharIsAlphaOrDigitOrUnderscore(c1) != _UICharIsAlphaOrDigitOrUnderscore(c2)) {
				return;
			}
		}
	}
}

int _UITextboxMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UITextbox *textbox = (UITextbox *) element;

	if (message == UI_MSG_GET_HEIGHT) {
		return UI_SIZE_TEXTBOX_HEIGHT * element->window->scale;
	} else if (message == UI_MSG_GET_WIDTH) {
		return UI_SIZE_TEXTBOX_WIDTH * element->window->scale;
	} else if (message == UI_MSG_PAINT) {
		int scaledMargin = UI_SIZE_TEXTBOX_MARGIN * element->window->scale;
		int totalWidth = UIMeasureStringWidth(textbox->string, textbox->bytes) + scaledMargin * 2;
		UIRectangle textBounds = UIRectangleAdd(element->bounds, UI_RECT_1I(scaledMargin));

		if (textbox->scroll > totalWidth - UI_RECT_WIDTH(textBounds)) {
			textbox->scroll = totalWidth - UI_RECT_WIDTH(textBounds);
		}

		if (textbox->scroll < 0) {
			textbox->scroll = 0;
		}

		int caretX = UIMeasureStringWidth(textbox->string, textbox->carets[0]) - textbox->scroll;

		if (caretX < 0) {
			textbox->scroll = caretX + textbox->scroll;
		} else if (caretX > UI_RECT_WIDTH(textBounds)) {
			textbox->scroll = caretX - UI_RECT_WIDTH(textBounds) + textbox->scroll + 1;
		}

		UIPainter *painter = (UIPainter *) dp;
		bool focused = element->window->focused == element;
		UIDrawRectangle(painter, element->bounds, 
			focused ? UI_COLOR_TEXTBOX_FOCUSED : UI_COLOR_TEXTBOX_NORMAL, 
			UI_COLOR_BORDER, UI_RECT_1(1));
		UIStringSelection selection = { 0 };
		selection.carets[0] = textbox->carets[0];
		selection.carets[1] = textbox->carets[1];
		selection.colorBackground = UI_COLOR_TEXTBOX_SELECTED;
		selection.colorText = UI_COLOR_TEXTBOX_SELECTED_TEXT;
		textBounds.l -= textbox->scroll;
		UIDrawString(painter, textBounds, textbox->string, textbox->bytes, UI_COLOR_TEXTBOX_TEXT, UI_ALIGN_LEFT, focused ? &selection : NULL);
	} else if (message == UI_MSG_GET_CURSOR) {
		return UI_CURSOR_TEXT;
	} else if (message == UI_MSG_LEFT_DOWN) {
		UIElementFocus(element);
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_DESTROY) {
		UI_FREE(textbox->string);
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		if (m->code == UI_KEYCODE_BACKSPACE || m->code == UI_KEYCODE_DELETE) {
			if (textbox->carets[0] == textbox->carets[1]) {
				UITextboxMoveCaret(textbox, m->code == UI_KEYCODE_BACKSPACE, element->window->ctrl);
			}

			UITextboxReplace(textbox, NULL, 0, true);
		} else if (m->code == UI_KEYCODE_LEFT || m->code == UI_KEYCODE_RIGHT) {
			UITextboxMoveCaret(textbox, m->code == UI_KEYCODE_LEFT, element->window->ctrl);

			if (!element->window->shift) {
				textbox->carets[1] = textbox->carets[0];
			}
		} else if (m->code == UI_KEYCODE_HOME || m->code == UI_KEYCODE_END) {
			if (m->code == UI_KEYCODE_HOME) {
				textbox->carets[0] = 0;
			} else {
				textbox->carets[0] = textbox->bytes;
			}

			if (!element->window->shift) {
				textbox->carets[1] = textbox->carets[0];
			}
		} else if (m->code == UI_KEYCODE_LETTER('A') && element->window->ctrl) {
			textbox->carets[1] = 0;
			textbox->carets[0] = textbox->bytes;
		} else if (m->textBytes && !element->window->ctrl) {
			UITextboxReplace(textbox, m->text, m->textBytes, true);
		}

		UIElementRepaint(element, NULL);
	}

	return 0;
}

UITextbox *UITextboxCreate(UIElement *parent, uint64_t flags) {
	return (UITextbox *) _UIElementSetup(sizeof(UITextbox), parent, flags, _UITextboxMessage, "Textbox");
}

bool _UICloseMenus() {
	UIWindow *window = ui.windows;
	bool anyClosed = false;

	while (window) {
		if (window->e.flags & UI_WINDOW_MENU) {
			UIElementDestroy(&window->e);
			anyClosed = true;
		}

		window = window->next;
	}

	return anyClosed;
}

int _UIMenuItemMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		_UICloseMenus();
	}

	return 0;
}

int _UIMenuMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_GET_WIDTH) {
		UIElement *child = element->children;
		int width = 0;

		while (child) {
			int w = UIElementMessage(child, UI_MSG_GET_WIDTH, 0, 0);
			if (w > width) width = w;
			child = child->next;
		}

		return width;
	} else if (message == UI_MSG_GET_HEIGHT) {
		UIElement *child = element->children;
		int height = 0;

		while (child) {
			height += UIElementMessage(child, UI_MSG_GET_HEIGHT, 0, 0);
			child = child->next;
		}

		return height;
	} else if (message == UI_MSG_LAYOUT) {
		UIElement *child = element->children;
		int position = element->bounds.t;

		while (child) {
			int height = UIElementMessage(child, UI_MSG_GET_HEIGHT, 0, 0);
			UIElementMove(child, UI_RECT_4(element->bounds.l, element->bounds.r, position, position + height), false);
			position += height;
			child = child->next;
		}
	}

	return 0;
}

void UIMenuAddItem(UIMenu *menu, uint64_t flags, const char *label, ptrdiff_t labelBytes, void (*invoke)(void *cp), void *cp) {
	UIButton *button = UIButtonCreate(&menu->e, flags | UI_BUTTON_MENU_ITEM, label, labelBytes);
	button->invoke = invoke;
	button->e.messageUser = _UIMenuItemMessage;
	button->e.cp = cp;
}

void _UIMenuPrepare(UIMenu *menu, int *width, int *height) {
	*width = UIElementMessage(&menu->e, UI_MSG_GET_WIDTH, 0, 0);
	*height = UIElementMessage(&menu->e, UI_MSG_GET_HEIGHT, 0, 0);

	if (menu->e.flags & UI_MENU_PLACE_ABOVE) {
		menu->pointY -= *height;
	}
}

UIMenu *UIMenuCreate(UIElement *parent, uint64_t flags) {
	UIWindow *window = UIWindowCreate(parent->window, UI_WINDOW_MENU, 0);
	
	UIMenu *menu = (UIMenu *) _UIElementSetup(sizeof(UIMenu), &window->e, flags, _UIMenuMessage, "Menu");

	if (parent->parent) {
		UIRectangle screenBounds = UIElementScreenBounds(parent);
		menu->pointX = screenBounds.l;
		menu->pointY = (flags & UI_MENU_PLACE_ABOVE) ? screenBounds.t : screenBounds.b;
	} else {
		int x = 0, y = 0;
		_UIWindowGetScreenPosition(parent->window, &x, &y);

		menu->pointX = parent->window->cursorX + x;
		menu->pointY = parent->window->cursorY + y;
	}

	return menu;
}

UIRectangle UIElementScreenBounds(UIElement *element) {
	int x = 0, y = 0;
	_UIWindowGetScreenPosition(element->window, &x, &y);
	return UIRectangleAdd(element->bounds, UI_RECT_2(x, y));
}

void UIWindowRegisterShortcut(UIWindow *window, UIShortcut shortcut) {
	if (window->shortcutCount + 1 > window->shortcutAllocated) {
		window->shortcutAllocated = (window->shortcutCount + 1) * 2;
		window->shortcuts = (UIShortcut *) UI_REALLOC(window->shortcuts, window->shortcutAllocated * sizeof(UIShortcut));
	}

	window->shortcuts[window->shortcutCount++] = shortcut;
}

void _UIElementPaint(UIElement *element, UIPainter *painter, bool forRepaint) {
	if (element->flags & UI_ELEMENT_HIDE) {
		return;
	}

	// Clip painting to the element's clip.

	painter->clip = UIRectangleIntersection(element->clip, painter->clip);

	if (!UI_RECT_VALID(painter->clip)) {
		return;
	}

	if (forRepaint) {
		// Add to the repaint region the intersection of the parent's repaint region with our clip.

		if (element->parent) {
			UIRectangle parentRepaint = UIRectangleIntersection(element->parent->repaint, element->clip);

			if (UI_RECT_VALID(parentRepaint)) {
				if (element->flags & UI_ELEMENT_REPAINT) {
					element->repaint = UIRectangleBounding(element->repaint, parentRepaint);
				} else {
					element->repaint = parentRepaint;
					element->flags |= UI_ELEMENT_REPAINT;
				}
			} 
		}

		// If we don't need to repaint, don't.
		
		if (~element->flags & UI_ELEMENT_REPAINT) {
			return;
		}

		// Clip painting to our repaint region.

		painter->clip = UIRectangleIntersection(element->repaint, painter->clip);

		if (!UI_RECT_VALID(painter->clip)) {
			return;
		}
	}

	// Paint the element.

	UIElementMessage(element, UI_MSG_PAINT, 0, painter);

	// Paint its children.

	UIElement *child = element->children;
	UIRectangle previousClip = painter->clip;

	while (child) {
		painter->clip = previousClip;
		_UIElementPaint(child, painter, forRepaint);
		child = child->next;
	}

	// Clear the repaint flag.

	if (forRepaint) {
		element->flags &= ~UI_ELEMENT_REPAINT;
	}
}

void UIElementFocus(UIElement *element) {
	UIElement *previous = element->window->focused;
	if (previous == element) return;
	element->window->focused = element;
	if (previous) UIElementMessage(previous, UI_MSG_UPDATE, 0, 0);
	if (element) UIElementMessage(element, UI_MSG_UPDATE, 0, 0);
}

void _UIWindowSetPressed(UIWindow *window, UIElement *element, int button) {
	UIElement *previous = window->pressed;
	window->pressed = element;
	window->pressedButton = button;
	if (previous) UIElementMessage(previous, UI_MSG_UPDATE, 0, 0);
	if (element) UIElementMessage(element, UI_MSG_UPDATE, 0, 0);
}

bool _UIDestroy(UIElement *element) {
	if (element->flags & UI_ELEMENT_DESTROY_DESCENDENT) {
		element->flags &= ~UI_ELEMENT_DESTROY_DESCENDENT;

		UIElement *child = element->children;
		UIElement **link = &element->children;

		while (child) {
			UIElement *next = child->next;

			if (_UIDestroy(child)) {
				*link = next;
			} else {
				link = &child->next;
			}

			child = next;
		}
	}

	if (element->flags & UI_ELEMENT_DESTROY) {
		UIElementMessage(element, UI_MSG_DESTROY, 0, 0);

		if (element->window->pressed == element) {
			_UIWindowSetPressed(element->window, NULL, 0);
		}

		if (element->window->hovered == element) {
			element->window->hovered = &element->window->e;
		}

		if (element->window->focused == element) {
			element->window->focused = NULL;
		}

		UI_FREE(element);
		return true;
	} else {
		return false;
	}
}

void _UIUpdate() {
	UIWindow *window = ui.windows;
	UIWindow **link = &ui.windows;

	while (window) {
		UIWindow *next = window->next;

		if (_UIDestroy(&window->e)) {
			*link = next;
		} else {
			link = &window->next;

			if (window->e.flags & UI_ELEMENT_REPAINT) {
				UIPainter painter = { 0 };
				window->updateRegion = window->e.repaint;
				painter.bits = window->bits;
				painter.width = window->width;
				painter.height = window->height;
				painter.clip = UI_RECT_2S(window->width, window->height);
				_UIElementPaint(&window->e, &painter, true);
				_UIWindowEndPaint(window, &painter);
			}
		}

		window = window->next;
	}
}

UIElement *UIElementFindByPoint(UIElement *element, int x, int y) {
	UIElement *child = element->children;

	while (child) {
		if ((~child->flags & UI_ELEMENT_HIDE) && UIRectangleContains(child->clip, x, y)) {
			return UIElementFindByPoint(child, x, y);
		}

		child = child->next;
	}

	return element;
}

void _UIProcessAnimations() {
	if (ui.animating) {
		UIElementMessage(ui.animating, UI_MSG_ANIMATE, 0, 0);
		_UIUpdate();
	}
}

bool _UIMenusOpen() {
	UIWindow *window = ui.windows;

	while (window) {
		if (window->e.flags & UI_WINDOW_MENU) {
			return true;
		}

		window = window->next;
	}

	return false;
}

void _UIWindowDestroyCommon(UIWindow *window) {
	UI_FREE(window->bits);
	UI_FREE(window->shortcuts);
}

void _UIWindowInputEvent(UIWindow *window, UIMessage message, int di, void *dp) {
	if (window->pressed) {
		if (message == UI_MSG_MOUSE_MOVE) {
			UIElementMessage(window->pressed, UI_MSG_MOUSE_DRAG, di, dp);
		} else if (message == UI_MSG_LEFT_UP && window->pressedButton == 1) {
			if (window->hovered == window->pressed) {
				UIElementMessage(window->pressed, UI_MSG_CLICKED, di, dp);
			}

			UIElementMessage(window->pressed, UI_MSG_LEFT_UP, di, dp);
			_UIWindowSetPressed(window, NULL, 1);
		} else if (message == UI_MSG_MIDDLE_UP && window->pressedButton == 2) {
			UIElementMessage(window->pressed, UI_MSG_MIDDLE_UP, di, dp);
			_UIWindowSetPressed(window, NULL, 2);
		} else if (message == UI_MSG_RIGHT_UP && window->pressedButton == 3) {
			UIElementMessage(window->pressed, UI_MSG_RIGHT_UP, di, dp);
			_UIWindowSetPressed(window, NULL, 3);
		}
	}

	if (window->pressed) {
		bool inside = UIRectangleContains(window->pressed->clip, window->cursorX, window->cursorY);

		if (inside && window->hovered == &window->e) {
			window->hovered = window->pressed;
			UIElementMessage(window->pressed, UI_MSG_UPDATE, 0, 0);
		} else if (!inside && window->hovered == window->pressed) {
			window->hovered = &window->e;
			UIElementMessage(window->pressed, UI_MSG_UPDATE, 0, 0);
		}
	}

	if (!window->pressed) {
		UIElement *hovered = UIElementFindByPoint(&window->e, window->cursorX, window->cursorY);

		if (message == UI_MSG_MOUSE_MOVE) {
			UIElementMessage(hovered, UI_MSG_MOUSE_MOVE, di, dp);

			int cursor = UIElementMessage(window->hovered, UI_MSG_GET_CURSOR, di, dp);

			if (cursor != window->cursorStyle) {
				window->cursorStyle = cursor;
				_UIWindowSetCursor(window, cursor);
			}
		} else if (message == UI_MSG_LEFT_DOWN) {
			if ((window->e.flags & UI_WINDOW_MENU) || !_UICloseMenus()) {
				_UIWindowSetPressed(window, hovered, 1);
				UIElementMessage(hovered, UI_MSG_LEFT_DOWN, di, dp);
			}
		} else if (message == UI_MSG_MIDDLE_DOWN) {
			if ((window->e.flags & UI_WINDOW_MENU) || !_UICloseMenus()) {
				_UIWindowSetPressed(window, hovered, 2);
				UIElementMessage(hovered, UI_MSG_MIDDLE_DOWN, di, dp);
			}
		} else if (message == UI_MSG_RIGHT_DOWN) {
			if ((window->e.flags & UI_WINDOW_MENU) || !_UICloseMenus()) {
				_UIWindowSetPressed(window, hovered, 3);
				UIElementMessage(hovered, UI_MSG_RIGHT_DOWN, di, dp);
			}
		} else if (message == UI_MSG_MOUSE_WHEEL) {
			UIElement *element = hovered;

			while (element) {
				if (UIElementMessage(element, UI_MSG_MOUSE_WHEEL, di, dp)) {
					break;
				}

				element = element->parent;
			}
		} else if (message == UI_MSG_KEY_TYPED) {
			bool handled = false;

			if (window->focused) {
				UIElement *element = window->focused;

				while (element) {
					if (UIElementMessage(element, UI_MSG_KEY_TYPED, di, dp)) {
						handled = true;
						break;
					}

					element = element->parent;
				}
			}

			if (!handled && !_UIMenusOpen()) {
				UIKeyTyped *m = (UIKeyTyped *) dp;

				for (uintptr_t i = 0; i < window->shortcutCount; i++) {
					UIShortcut *shortcut = window->shortcuts + i;

					if (shortcut->code == m->code && shortcut->ctrl == window->ctrl 
							&& shortcut->shift == window->shift && shortcut->alt == window->alt) {
						shortcut->invoke(shortcut->cp);
						break;
					}
				}
			}
		}

		if (hovered != window->hovered) {
			UIElement *previous = window->hovered;
			window->hovered = hovered;
			UIElementMessage(previous, UI_MSG_UPDATE, 0, 0);
			UIElementMessage(window->hovered, UI_MSG_UPDATE, 0, 0);
		}
	}

	_UIUpdate();
}

#ifdef UI_DEBUG

UIElement *_UIInspectorFindNthElement(UIElement *element, int *index, int *depth) {
	if (*index == 0) {
		return element;
	}

	*index = *index - 1;
	
	UIElement *child = element->children;

	while (child) {
		if (!(child->flags & (UI_ELEMENT_DESTROY | UI_ELEMENT_HIDE))) {
			UIElement *result = _UIInspectorFindNthElement(child, index, depth);

			if (result) {
				if (depth) {
					*depth = *depth + 1;
				}

				return result;
			}
		}

		child = child->next;
	}

	return NULL;
}

int _UIInspectorTableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (!ui.inspectorTarget) {
		return 0;
	}

	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		int index = m->index;
		int depth = 0;
		UIElement *element = _UIInspectorFindNthElement(&ui.inspectorTarget->e, &index, &depth);
		if (!element) return 0;

		if (m->column == 0) {
			return snprintf(m->buffer, m->bufferBytes, "%.*s%s", depth * 2, "                ", element->cClassName);
		} else if (m->column == 1) {
			return snprintf(m->buffer, m->bufferBytes, "%d:%d, %d:%d", UI_RECT_ALL(element->bounds));
		} else if (m->column == 2) {
			return snprintf(m->buffer, m->bufferBytes, "%d", element->id);
		}
	} else if (message == UI_MSG_MOUSE_MOVE) {
		int index = UITableHitTest(ui.inspectorTable, element->window->cursorX, element->window->cursorY);
		UIElement *element = NULL;
		if (index >= 0) element = _UIInspectorFindNthElement(&ui.inspectorTarget->e, &index, NULL);
		UIWindow *window = ui.inspectorTarget;
		UIPainter painter = { 0 };
		window->updateRegion = window->e.bounds;
		painter.bits = window->bits;
		painter.width = window->width;
		painter.height = window->height;
		painter.clip = UI_RECT_2S(window->width, window->height);

		for (int i = 0; i < window->width * window->height; i++) {
			window->bits[i] = 0xFF00FF;
		}

		_UIElementPaint(&window->e, &painter, false);
		painter.clip = UI_RECT_2S(window->width, window->height);

		if (element) {
			UIDrawInvert(&painter, element->bounds);
			UIDrawInvert(&painter, UIRectangleAdd(element->bounds, UI_RECT_1I(4)));
		}

		_UIWindowEndPaint(window, &painter);
	}

	return 0;
}

void _UIInspectorCreate() {
	ui.inspector = UIWindowCreate(0, UI_WINDOW_INSPECTOR, "Inspector");
	ui.inspectorTable = UITableCreate(&ui.inspector->e, 0, "Class\tBounds\tID");
	ui.inspectorTable->e.messageUser = _UIInspectorTableMessage;
}

int _UIInspectorCountElements(UIElement *element) {
	UIElement *child = element->children;
	int count = 1;

	while (child) {
		if (!(child->flags & (UI_ELEMENT_DESTROY | UI_ELEMENT_HIDE))) {
			count += _UIInspectorCountElements(child);
		}

		child = child->next;
	}

	return count;
}

void _UIInspectorRefresh() {
	if (!ui.inspectorTarget) return;
	ui.inspectorTable->itemCount = _UIInspectorCountElements(&ui.inspectorTarget->e);
	UITableResizeColumns(ui.inspectorTable);
	UIElementRefresh(&ui.inspectorTable->e);
}

void _UIInspectorSetFocusedWindow(UIWindow *window) {
	if (window->e.flags & UI_WINDOW_INSPECTOR) {
		return;
	}

	if (ui.inspectorTarget != window) {
		ui.inspectorTarget = window;
		_UIInspectorRefresh();
	}
}

#else

void _UIInspectorCreate() {}
void _UIInspectorSetFocusedWindow(UIWindow *window) {}
void _UIInspectorRefresh() {}

#endif

#ifdef UI_LINUX

int _UIWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_LAYOUT && element->children) {
		UIElementMove(element->children, element->bounds, false);
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_DESTROY) {
		UIWindow *window = (UIWindow *) element;
		_UIWindowDestroyCommon(window);
		window->image->data = NULL;
		XDestroyImage(window->image);
		XDestroyIC(window->xic);
		XDestroyWindow(ui.display, ((UIWindow *) element)->window);
	}

	return 0;
}

UIWindow *UIWindowCreate(UIWindow *owner, uint64_t flags, const char *cTitle) {
	_UICloseMenus();

	UIWindow *window = (UIWindow *) _UIElementSetup(sizeof(UIWindow), NULL, flags, _UIWindowMessage, "Window");
	window->scale = 1.0f;
	window->e.window = window;
	window->hovered = &window->e;
	window->next = ui.windows;
	ui.windows = window;

	int width = (flags & UI_WINDOW_MENU) ? 1 : 800;
	int height = (flags & UI_WINDOW_MENU) ? 1 : 600;

	window->window = XCreateWindow(ui.display, DefaultRootWindow(ui.display), 0, 0, width, height, 0, 0, 
		InputOutput, CopyFromParent, 0, 0);
	XStoreName(ui.display, window->window, cTitle);
	XSelectInput(ui.display, window->window, SubstructureNotifyMask | ExposureMask | PointerMotionMask 
		| ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask
		| EnterWindowMask | LeaveWindowMask | ButtonMotionMask | KeymapStateMask | FocusChangeMask);

	if (~flags & UI_WINDOW_MENU) {
		XMapRaised(ui.display, window->window);
	}

	XSetWMProtocols(ui.display, window->window, &ui.windowClosedID, 1);
	window->image = XCreateImage(ui.display, ui.visual, 24, ZPixmap, 0, NULL, 10, 10, 32, 0);

	window->xic = XCreateIC(ui.xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, window->window, XNFocusWindow, window->window, NULL);

	return window;
}

void UIInitialise() {
	XInitThreads();

	ui.display = XOpenDisplay(NULL);
	ui.visual = XDefaultVisual(ui.display, 0);
	ui.windowClosedID = XInternAtom(ui.display, "WM_DELETE_WINDOW", 0);

	ui.cursors[UI_CURSOR_ARROW] = XCreateFontCursor(ui.display, XC_left_ptr);
	ui.cursors[UI_CURSOR_TEXT] = XCreateFontCursor(ui.display, XC_xterm);
	ui.cursors[UI_CURSOR_SPLIT_V] = XCreateFontCursor(ui.display, XC_sb_v_double_arrow);
	ui.cursors[UI_CURSOR_SPLIT_H] = XCreateFontCursor(ui.display, XC_sb_h_double_arrow);
	ui.cursors[UI_CURSOR_FLIPPED_ARROW] = XCreateFontCursor(ui.display, XC_right_ptr);

	XSetLocaleModifiers("");

	ui.xim = XOpenIM(ui.display, 0, 0, 0);

	if(!ui.xim){
		XSetLocaleModifiers("@im=none");
		ui.xim = XOpenIM(ui.display, 0, 0, 0);
	}
}

UIWindow *_UIFindWindow(Window window) {
	UIWindow *w = ui.windows;

	while (w) {
		if (w->window == window) {
			return w;
		}

		w = w->next;
	}

	return NULL;
}

void _UIWindowSetCursor(UIWindow *window, int cursor) {
	XDefineCursor(ui.display, window->window, ui.cursors[cursor]);
}

void _UIWindowEndPaint(UIWindow *window, UIPainter *painter) {
	(void) painter;

	XPutImage(ui.display, window->window, DefaultGC(ui.display, 0), window->image, 
		UI_RECT_TOP_LEFT(window->updateRegion), UI_RECT_TOP_LEFT(window->updateRegion),
		UI_RECT_SIZE(window->updateRegion));
}

void _UIWindowGetScreenPosition(UIWindow *window, int *_x, int *_y) {
	int x, y;
	Window child;
	XWindowAttributes attributes;
	XTranslateCoordinates(ui.display, window->window, DefaultRootWindow(ui.display), 0, 0, &x, &y, &child);
	XGetWindowAttributes(ui.display, window->window, &attributes);
	*_x = x - attributes.x; 
	*_y = y - attributes.y;
}

void UIMenuShow(UIMenu *menu) {
	int width, height;
	_UIMenuPrepare(menu, &width, &height);

	struct Hints {
		int flags;
		int functions;
		int decorations;
		int inputMode;
		int status;
	};

	struct Hints hints = { 2 };
	Atom property = XInternAtom(ui.display, "_MOTIF_WM_HINTS", true);
	XChangeProperty(ui.display, menu->e.window->window, property, property, 32, PropModeReplace, (uint8_t *) &hints, 5);

	XMapWindow(ui.display, menu->e.window->window);
	XMoveResizeWindow(ui.display, menu->e.window->window, menu->pointX, menu->pointY, width, height);
}

bool _UIProcessEvent(XEvent *event) {
	// printf("x11 event: %d\n", event->type);

	if (event->type == ClientMessage && (Atom) event->xclient.data.l[0] == ui.windowClosedID) {
		return true;
	} else if (event->type == Expose) {
		UIWindow *window = _UIFindWindow(event->xexpose.window);
		if (!window) return false;
		XPutImage(ui.display, window->window, DefaultGC(ui.display, 0), window->image, 0, 0, 0, 0, window->width, window->height);
	} else if (event->type == ConfigureNotify) {
		UIWindow *window = _UIFindWindow(event->xconfigure.window);
		if (!window) return false;

		if (window->width != event->xconfigure.width || window->height != event->xconfigure.height) {
			window->width = event->xconfigure.width;
			window->height = event->xconfigure.height;
			window->bits = (uint32_t *) UI_REALLOC(window->bits, window->width * window->height * 4);
			window->image->width = window->width;
			window->image->height = window->height;
			window->image->bytes_per_line = window->width * 4;
			window->image->data = (char *) window->bits;
			window->e.bounds = UI_RECT_2S(window->width, window->height);
			window->e.clip = UI_RECT_2S(window->width, window->height);
#ifdef UI_DEBUG
			for (int i = 0; i < window->width * window->height; i++) window->bits[i] = 0xFF00FF;
#endif
			UIElementMessage(&window->e, UI_MSG_LAYOUT, 0, 0);
			_UIUpdate();
		}
	} else if (event->type == MotionNotify) {
		UIWindow *window = _UIFindWindow(event->xmotion.window);
		if (!window) return false;
		window->cursorX = event->xmotion.x;
		window->cursorY = event->xmotion.y;
		_UIWindowInputEvent(window, UI_MSG_MOUSE_MOVE, 0, 0);
	} else if (event->type == LeaveNotify) {
		UIWindow *window = _UIFindWindow(event->xcrossing.window);
		if (!window) return false;

		if (!window->pressed) {
			window->cursorX = -1;
			window->cursorY = -1;
		}

		_UIWindowInputEvent(window, UI_MSG_MOUSE_MOVE, 0, 0);
	} else if (event->type == ButtonPress || event->type == ButtonRelease) {
		UIWindow *window = _UIFindWindow(event->xbutton.window);
		if (!window) return false;
		window->cursorX = event->xbutton.x;
		window->cursorY = event->xbutton.y;

		if (event->xbutton.button >= 1 && event->xbutton.button <= 3) {
			_UIWindowInputEvent(window, (UIMessage) ((event->type == ButtonPress ? UI_MSG_LEFT_DOWN : UI_MSG_LEFT_UP) 
				+ event->xbutton.button * 2 - 2), 0, 0);
		} else if (event->xbutton.button == 4) {
			_UIWindowInputEvent(window, UI_MSG_MOUSE_WHEEL, -72, 0);
		} else if (event->xbutton.button == 5) {
			_UIWindowInputEvent(window, UI_MSG_MOUSE_WHEEL, 72, 0);
		}

		_UIInspectorSetFocusedWindow(window);
	} else if (event->type == KeyPress) {

		UIWindow *window = _UIFindWindow(event->xkey.window);
		if (!window) return false;

		if (event->xkey.x == 0x7123 && event->xkey.y == 0x7456) {
			// HACK! See UIWindowPostMessage.
			UIElementMessage(&window->e, (UIMessage) event->xkey.state, 0, 
				(void *) (((uintptr_t) (event->xkey.time & 0xFFFFFFFF) << 32) 
					| ((uintptr_t) (event->xkey.x_root & 0xFFFF) << 0) 
					| ((uintptr_t) (event->xkey.y_root & 0xFFFF) << 16)));
			_UIUpdate();
		} else {
			char text[32];
			KeySym symbol = NoSymbol;
			Status status;
			// printf("%ld, %s\n", symbol, text);
			UIKeyTyped m = { 0 };
			m.textBytes = Xutf8LookupString(window->xic, &event->xkey, text, sizeof(text) - 1, &symbol, &status); 
			m.text = text;
			m.code = symbol;

			if (symbol == XK_Control_L || symbol == XK_Control_R) {
				window->ctrl = true;
				window->ctrlCode = event->xkey.keycode;
			} else if (symbol == XK_Shift_L || symbol == XK_Shift_R) {
				window->shift = true;
				window->shiftCode = event->xkey.keycode;
			} else if (symbol == XK_Alt_L || symbol == XK_Alt_R) {
				window->alt = true;
				window->altCode = event->xkey.keycode;
			}

			_UIWindowInputEvent(window, UI_MSG_KEY_TYPED, 0, &m);
		}
	} else if (event->type == KeyRelease) {
		UIWindow *window = _UIFindWindow(event->xkey.window);
		if (!window) return false;

		if (event->xkey.keycode == window->ctrlCode) {
			window->ctrl = false;
		} else if (event->xkey.keycode == window->shiftCode) {
			window->shift = false;
		} else if (event->xkey.keycode == window->altCode) {
			window->alt = false;
		}

	}

	return false;
}

int UIMessageLoop() {
	_UIInspectorCreate();

	while (true) {
		XEvent events[64];

		if (ui.animating) {
			if (XPending(ui.display)) {
				XNextEvent(ui.display, events + 0);
			} else {
				_UIProcessAnimations();
				continue;
			}
		} else {
			XNextEvent(ui.display, events + 0);
		}

		int p = 1;

		int configureIndex = -1, motionIndex = -1, exposeIndex = -1;

		while (p < 64 && XPending(ui.display)) {
			XNextEvent(ui.display, events + p);

#define _UI_MERGE_EVENTS(a, b) \
	if (events[p].type == a) { \
		if (b != -1) events[b].type = 0; \
		b = p; \
	}

			_UI_MERGE_EVENTS(ConfigureNotify, configureIndex);
			_UI_MERGE_EVENTS(MotionNotify, motionIndex);
			_UI_MERGE_EVENTS(Expose, exposeIndex);

			p++;
		}

		for (int i = 0; i < p; i++) {
			if (!events[i].type) {
				continue;
			}

			if (_UIProcessEvent(events + i)) {
				return 0;
			}
		}
	}
}

void UIWindowPostMessage(UIWindow *window, UIMessage message, void *_dp) {
	// HACK! Xlib doesn't seem to have a nice way to do this,
	// so send a specially crafted key press event instead.
	uintptr_t dp = (uintptr_t) _dp;
	XKeyEvent event = { 0 };
	event.display = ui.display;
	event.window = window->window;
	event.root = DefaultRootWindow(ui.display);
	event.subwindow = None;
	event.time = dp >> 32;
	event.x = 0x7123;
	event.y = 0x7456;
	event.x_root = (dp >> 0) & 0xFFFF;
	event.y_root = (dp >> 16) & 0xFFFF;
	event.same_screen = True;
	event.keycode = 1;
	event.state = message;
	event.type = KeyPress;
	XSendEvent(ui.display, window->window, True, KeyPressMask, (XEvent *) &event);
	XFlush(ui.display);
}

#endif

#ifdef UI_WINDOWS

int _UIWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_LAYOUT && element->children) {
		UIElementMove(element->children, element->bounds, false);
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_DESTROY) {
		UIWindow *window = (UIWindow *) element;
		_UIWindowDestroyCommon(window);
		SetWindowLongPtr(window->hwnd, GWLP_USERDATA, 0);
		DestroyWindow(window->hwnd);
	}

	return 0;
}

LRESULT CALLBACK _UIWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	UIWindow *window = (UIWindow *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

	if (!window) {
		return DefWindowProc(hwnd, message, wParam, lParam);
	}

	if (message == WM_CLOSE) {
		ExitProcess(0);
	} else if (message == WM_SIZE) {
		RECT client;
		GetClientRect(hwnd, &client);
		window->width = client.right;
		window->height = client.bottom;
		window->bits = (uint32_t *) UI_REALLOC(window->bits, window->width * window->height * 4);
		window->e.bounds = UI_RECT_2S(window->width, window->height);
		window->e.clip = UI_RECT_2S(window->width, window->height);
		UIElementMessage(&window->e, UI_MSG_LAYOUT, 0, 0);
		_UIUpdate();
	} else if (message == WM_MOUSEMOVE) {
		if (!window->trackingLeave) {
			window->trackingLeave = true;
			TRACKMOUSEEVENT leave = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd };
			TrackMouseEvent(&leave);
		}

		POINT cursor;
		GetCursorPos(&cursor);
		ScreenToClient(hwnd, &cursor);
		window->cursorX = cursor.x;
		window->cursorY = cursor.y;
		_UIWindowInputEvent(window, UI_MSG_MOUSE_MOVE, 0, 0);
	} else if (message == WM_MOUSELEAVE) {
		window->trackingLeave = false;

		if (!window->pressed) {
			window->cursorX = -1;
			window->cursorY = -1;
		}

		_UIWindowInputEvent(window, UI_MSG_MOUSE_MOVE, 0, 0);
	} else if (message == WM_LBUTTONDOWN) {
		SetCapture(hwnd);
		_UIWindowInputEvent(window, UI_MSG_LEFT_DOWN, 0, 0);
	} else if (message == WM_LBUTTONUP) {
		if (window->pressedButton == 1) ReleaseCapture();
		_UIWindowInputEvent(window, UI_MSG_LEFT_UP, 0, 0);
	} else if (message == WM_MBUTTONDOWN) {
		SetCapture(hwnd);
		_UIWindowInputEvent(window, UI_MSG_MIDDLE_DOWN, 0, 0);
	} else if (message == WM_MBUTTONUP) {
		if (window->pressedButton == 2) ReleaseCapture();
		_UIWindowInputEvent(window, UI_MSG_MIDDLE_UP, 0, 0);
	} else if (message == WM_RBUTTONDOWN) {
		SetCapture(hwnd);
		_UIWindowInputEvent(window, UI_MSG_RIGHT_DOWN, 0, 0);
	} else if (message == WM_RBUTTONUP) {
		if (window->pressedButton == 3) ReleaseCapture();
		_UIWindowInputEvent(window, UI_MSG_RIGHT_UP, 0, 0);
	} else if (message == WM_MOUSEWHEEL) {
		int delta = (int) wParam >> 16;
		_UIWindowInputEvent(window, UI_MSG_MOUSE_WHEEL, -delta, 0);
	} else if (message == WM_KEYDOWN) {
		window->ctrl = GetKeyState(VK_CONTROL) & 0x8000;
		window->shift = GetKeyState(VK_SHIFT) & 0x8000;
		window->alt = GetKeyState(VK_MENU) & 0x8000;

		UIKeyTyped m = { 0 };
		m.code = wParam;
		_UIWindowInputEvent(window, UI_MSG_KEY_TYPED, 0, &m);
	} else if (message == WM_CHAR) {
		UIKeyTyped m = { 0 };
		char c = wParam;
		m.text = &c;
		m.textBytes = 1;
		_UIWindowInputEvent(window, UI_MSG_KEY_TYPED, 0, &m);
	} else if (message == WM_PAINT) {
		PAINTSTRUCT paint;
		HDC dc = BeginPaint(hwnd, &paint);
		BITMAPINFOHEADER info = { sizeof(info), window->width, -window->height, 1, 32 };
		StretchDIBits(dc, 0, 0, UI_RECT_SIZE(window->e.bounds), 0, 0, UI_RECT_SIZE(window->e.bounds),
			window->bits, (BITMAPINFO *) &info, DIB_RGB_COLORS, SRCCOPY);
		EndPaint(hwnd, &paint);
	} else if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT) {
		SetCursor(ui.cursors[window->cursorStyle]);
		return 1;
	} else if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
		_UICloseMenus();

		if (message == WM_SETFOCUS) {
			_UIInspectorSetFocusedWindow(window);
		}
	} else if (message == WM_MOUSEACTIVATE && (window->e.flags & UI_WINDOW_MENU)) {
		return MA_NOACTIVATE;
	} else {
		return DefWindowProc(hwnd, message, wParam, lParam);
	}

	return 0;
}

void UIInitialise() {
	ui.cursors[UI_CURSOR_ARROW] = LoadCursor(NULL, IDC_ARROW);
	ui.cursors[UI_CURSOR_TEXT] = LoadCursor(NULL, IDC_IBEAM);
	ui.cursors[UI_CURSOR_SPLIT_V] = LoadCursor(NULL, IDC_SIZENS);
	ui.cursors[UI_CURSOR_SPLIT_H] = LoadCursor(NULL, IDC_SIZEWE);
	ui.cursors[UI_CURSOR_FLIPPED_ARROW] = LoadCursor(NULL, IDC_ARROW);

	ui.heap = GetProcessHeap();

	WNDCLASS windowClass = { 0 };
	windowClass.lpfnWndProc = _UIWindowProcedure;
	windowClass.lpszClassName = "normal";
	RegisterClass(&windowClass);
	windowClass.style |= CS_DROPSHADOW;
	windowClass.lpszClassName = "shadow";
	RegisterClass(&windowClass);
}

int UIMessageLoop() {
	_UIInspectorCreate();

	MSG message = { 0 };

	_UIUpdate();

	while (true) {
		if (ui.animating) {
			if (PeekMessage(&message, NULL, 0, 0, PM_REMOVE)) {
				TranslateMessage(&message);
				DispatchMessage(&message);
			} else {
				_UIProcessAnimations();
			}
		} else {
			GetMessage(&message, NULL, 0, 0);
			TranslateMessage(&message);
			DispatchMessage(&message);
		}
	}

	return message.wParam;
}

void UIMenuShow(UIMenu *menu) {
	int width, height;
	_UIMenuPrepare(menu, &width, &height);
	RECT r = { 0, 0, width, height };
	AdjustWindowRect(&r, WS_BORDER, FALSE);
	MoveWindow(menu->e.window->hwnd, menu->pointX, menu->pointY, r.right - r.left, r.bottom - r.top, FALSE);
	ShowWindow(menu->e.window->hwnd, SW_SHOWNOACTIVATE);
}

UIWindow *UIWindowCreate(UIWindow *owner, uint64_t flags, const char *cTitle) {
	_UICloseMenus();

	UIWindow *window = (UIWindow *) _UIElementSetup(sizeof(UIWindow), NULL, flags, _UIWindowMessage, "Window");
	window->scale = 1.0f;
	window->e.window = window;
	window->hovered = &window->e;
	window->next = ui.windows;
	ui.windows = window;

	if (flags & UI_WINDOW_MENU) {
		UI_ASSERT(owner);

		window->hwnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_NOACTIVATE, "shadow", 0, WS_BORDER | WS_POPUP, 
			0, 0, 0, 0, owner->hwnd, NULL, NULL, NULL);
	} else {
		window->hwnd = CreateWindow("normal", cTitle, WS_OVERLAPPEDWINDOW, 
			CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,  CW_USEDEFAULT,
			owner ? owner->hwnd : NULL, NULL, NULL, NULL);
	}

	SetWindowLongPtr(window->hwnd, GWLP_USERDATA, (LONG_PTR) window);

	if (~flags & UI_WINDOW_MENU) {
		ShowWindow(window->hwnd, SW_SHOW);
		PostMessage(window->hwnd, WM_SIZE, 0, 0);
	}

	return window;
}

void _UIWindowEndPaint(UIWindow *window, UIPainter *painter) {
	HDC dc = GetDC(window->hwnd);
	BITMAPINFOHEADER info = { sizeof(info), window->width, window->height, 1, 32 };
	StretchDIBits(dc, 
		UI_RECT_TOP_LEFT(window->updateRegion), UI_RECT_SIZE(window->updateRegion), 
		window->updateRegion.l, window->updateRegion.b + 1, 
		UI_RECT_WIDTH(window->updateRegion), -UI_RECT_HEIGHT(window->updateRegion),
		window->bits, (BITMAPINFO *) &info, DIB_RGB_COLORS, SRCCOPY);
	ReleaseDC(window->hwnd, dc);
}

void _UIWindowSetCursor(UIWindow *window, int cursor) {
	SetCursor(ui.cursors[cursor]);
}

void _UIWindowGetScreenPosition(UIWindow *window, int *_x, int *_y) {
	POINT p;
	p.x = 0;
	p.y = 0;
	ClientToScreen(window->hwnd, &p);
	*_x = p.x;
	*_y = p.y;
}

#endif

#endif
