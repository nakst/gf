// TODO UITextbox features - mouse input, undo, number dragging.
// TODO New elements - list view, menu bar.
// TODO Keyboard navigation in menus.
// TODO Easier to use fonts.

/////////////////////////////////////////
// Header includes.
/////////////////////////////////////////

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#ifdef UI_LINUX
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#endif

#ifdef UI_SSE2
#include <xmmintrin.h>
#endif

#ifdef UI_WINDOWS
#undef _UNICODE
#undef UNICODE
#include <windows.h>

#define UI_ASSERT(x) do { if (!(x)) { ui.assertionFailure = true; \
	MessageBox(0, "Assertion failure on line " _UI_TO_STRING_2(__LINE__), 0, 0); \
	ExitProcess(1); } } while (0)
#define UI_CALLOC(x) HeapAlloc(ui.heap, HEAP_ZERO_MEMORY, (x))
#define UI_FREE(x) HeapFree(ui.heap, 0, (x))
#define UI_MALLOC(x) HeapAlloc(ui.heap, 0, (x))
#define UI_REALLOC _UIHeapReAlloc
#define UI_CLOCK GetTickCount
#define UI_CLOCKS_PER_SECOND (1000)
#define UI_CLOCK_T DWORD
#define UI_MEMMOVE _UIMemmove
#endif

#ifdef UI_COCOA
#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#endif

#if defined(UI_LINUX) || defined(UI_COCOA)
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <math.h>

#define UI_ASSERT assert
#define UI_CALLOC(x) calloc(1, (x))
#define UI_FREE free
#define UI_MALLOC malloc
#define UI_REALLOC realloc
#define UI_CLOCK _UIClock
#define UI_CLOCKS_PER_SECOND 1000
#define UI_CLOCK_T clock_t
#define UI_MEMMOVE(d, s, n) do { size_t _n = n; if (_n) { memmove(d, s, _n); } } while (0)
#endif

#if defined(UI_ESSENCE)
#include <essence.h>

#define UI_ASSERT EsAssert
#define UI_CALLOC(x) EsHeapAllocate((x), true)
#define UI_FREE EsHeapFree
#define UI_MALLOC(x) EsHeapAllocate((x), false)
#define UI_REALLOC(x, y) EsHeapReallocate((x), (y), false)
#define UI_CLOCK EsTimeStampMs
#define UI_CLOCKS_PER_SECOND 1000
#define UI_CLOCK_T uint64_t
#define UI_MEMMOVE EsCRTmemmove

// Callback to allow the application to process messages.
void _UIMessageProcess(EsMessage *message);
#endif

#ifdef UI_DEBUG
#include <stdio.h>
#endif

#ifdef UI_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/ftbitmap.h>
#endif

/////////////////////////////////////////
// Definitions.
/////////////////////////////////////////

#define _UI_TO_STRING_1(x) #x
#define _UI_TO_STRING_2(x) _UI_TO_STRING_1(x)

#define UI_SIZE_BUTTON_MINIMUM_WIDTH (100)
#define UI_SIZE_BUTTON_PADDING (16)
#define UI_SIZE_BUTTON_HEIGHT (27)
#define UI_SIZE_BUTTON_CHECKED_AREA (4)

#define UI_SIZE_CHECKBOX_BOX (14)
#define UI_SIZE_CHECKBOX_GAP (8)

#define UI_SIZE_MENU_ITEM_HEIGHT (24)
#define UI_SIZE_MENU_ITEM_MINIMUM_WIDTH (160)
#define UI_SIZE_MENU_ITEM_MARGIN (9)

#define UI_SIZE_GAUGE_WIDTH (200)
#define UI_SIZE_GAUGE_HEIGHT (22)

#define UI_SIZE_SLIDER_WIDTH (200)
#define UI_SIZE_SLIDER_HEIGHT (25)
#define UI_SIZE_SLIDER_THUMB (15)
#define UI_SIZE_SLIDER_TRACK (5)

#define UI_SIZE_TEXTBOX_MARGIN (3)
#define UI_SIZE_TEXTBOX_WIDTH (200)
#define UI_SIZE_TEXTBOX_HEIGHT (27)

#define UI_SIZE_TAB_PANE_SPACE_TOP (2)
#define UI_SIZE_TAB_PANE_SPACE_LEFT (4)

#define UI_SIZE_SPLITTER (8)

#define UI_SIZE_SCROLL_BAR (16)
#define UI_SIZE_SCROLL_MINIMUM_THUMB (20)

#define UI_SIZE_CODE_MARGIN (ui.activeFont->glyphWidth * 5)
#define UI_SIZE_CODE_MARGIN_GAP (ui.activeFont->glyphWidth * 1)

#define UI_SIZE_TABLE_HEADER (26)
#define UI_SIZE_TABLE_COLUMN_GAP (20)
#define UI_SIZE_TABLE_ROW (20)

#define UI_SIZE_PANE_LARGE_BORDER (20)
#define UI_SIZE_PANE_LARGE_GAP (10)
#define UI_SIZE_PANE_MEDIUM_BORDER (5)
#define UI_SIZE_PANE_MEDIUM_GAP (5)
#define UI_SIZE_PANE_SMALL_BORDER (3)
#define UI_SIZE_PANE_SMALL_GAP (3)

#define UI_SIZE_MDI_CHILD_BORDER (6)
#define UI_SIZE_MDI_CHILD_TITLE (30)
#define UI_SIZE_MDI_CHILD_CORNER (12)
#define UI_SIZE_MDI_CHILD_MINIMUM_WIDTH (100)
#define UI_SIZE_MDI_CHILD_MINIMUM_HEIGHT (50)
#define UI_SIZE_MDI_CASCADE (30)

#define UI_MDI_CHILD_CALCULATE_LAYOUT(bounds, scale) \
	int titleSize = UI_SIZE_MDI_CHILD_TITLE * scale; \
	int borderSize = UI_SIZE_MDI_CHILD_BORDER * scale; \
	UIRectangle title = UIRectangleAdd(bounds, UI_RECT_4(borderSize, -borderSize, 0, 0)); \
	title.b = title.t + titleSize; \
	UIRectangle content = UIRectangleAdd(bounds, UI_RECT_4(borderSize, -borderSize, titleSize, -borderSize));

#define UI_UPDATE_HOVERED (1)
#define UI_UPDATE_PRESSED (2)
#define UI_UPDATE_FOCUSED (3)
#define UI_UPDATE_DISABLED (4)

typedef enum UIMessage {
	// General messages.
	UI_MSG_PAINT, // dp = pointer to UIPainter
	UI_MSG_PAINT_FOREGROUND, // after children have painted
	UI_MSG_LAYOUT,
	UI_MSG_DESTROY,
	UI_MSG_DEALLOCATE,
	UI_MSG_UPDATE, // di = UI_UPDATE_... constant
	UI_MSG_ANIMATE,
	UI_MSG_SCROLLED,
	UI_MSG_GET_WIDTH, // di = height (if known); return width
	UI_MSG_GET_HEIGHT, // di = width (if known); return height
	UI_MSG_GET_CHILD_STABILITY, // dp = child element; return stable axes, 1 (width) | 2 (height)

	// Input events.
	UI_MSG_INPUT_EVENTS_START, // not sent to disabled elements
	UI_MSG_LEFT_DOWN,
	UI_MSG_LEFT_UP,
	UI_MSG_MIDDLE_DOWN,
	UI_MSG_MIDDLE_UP,
	UI_MSG_RIGHT_DOWN,
	UI_MSG_RIGHT_UP,
	UI_MSG_KEY_TYPED, // dp = pointer to UIKeyTyped; return 1 if handled
	UI_MSG_KEY_RELEASED, // dp = pointer to UIKeyTyped; return 1 if handled
	UI_MSG_MOUSE_MOVE,
	UI_MSG_MOUSE_DRAG,
	UI_MSG_MOUSE_WHEEL, // di = delta; return 1 if handled
	UI_MSG_CLICKED,
	UI_MSG_GET_CURSOR, // return cursor code
	UI_MSG_PRESSED_DESCENDENT, // dp = pointer to child that is/contains pressed element
	UI_MSG_INPUT_EVENTS_END,

	// Specific elements.
	UI_MSG_VALUE_CHANGED, // sent to notify that the element's value has changed
	UI_MSG_TABLE_GET_ITEM, // dp = pointer to UITableGetItem; return string length
	UI_MSG_CODE_GET_MARGIN_COLOR, // di = line index (starts at 1); return color
	UI_MSG_CODE_DECORATE_LINE, // dp = pointer to UICodeDecorateLine
	UI_MSG_TAB_SELECTED, // sent to the tab that was selected (not the tab pane itself)

	// Windows.
	UI_MSG_WINDOW_DROP_FILES, // di = count, dp = char ** of paths
	UI_MSG_WINDOW_ACTIVATE,
	UI_MSG_WINDOW_CLOSE, // return 1 to prevent default (process exit for UIWindow; close for UIMDIChild)
	UI_MSG_WINDOW_UPDATE_START,
	UI_MSG_WINDOW_UPDATE_BEFORE_DESTROY,
	UI_MSG_WINDOW_UPDATE_BEFORE_LAYOUT,
	UI_MSG_WINDOW_UPDATE_BEFORE_PAINT,
	UI_MSG_WINDOW_UPDATE_END,

	// User-defined messages.
	UI_MSG_USER,
} UIMessage;

#ifdef UI_ESSENCE
#define UIRectangle EsRectangle
#else
typedef struct UIRectangle {
	int l, r, t, b;
} UIRectangle;
#endif

typedef struct UITheme {
	uint32_t panel1, panel2, selected, border;
	uint32_t text, textDisabled, textSelected;
	uint32_t buttonNormal, buttonHovered, buttonPressed, buttonDisabled;
	uint32_t textboxNormal, textboxFocused;
	uint32_t codeFocused, codeBackground, codeDefault, codeComment, codeString, codeNumber, codeOperator, codePreprocessor;
	uint32_t accent1, accent2;
} UITheme;

typedef struct UIPainter {
	UIRectangle clip;
	uint32_t *bits;
	int width, height;
#ifdef UI_DEBUG
	int fillCount;
#endif
} UIPainter;

typedef struct UIFont {
	int glyphWidth, glyphHeight;

#ifdef UI_FREETYPE
	bool isFreeType;
	FT_Face font;
#ifdef UI_UNICODE
	FT_Bitmap *glyphs;
	bool *glyphsRendered;
	int *glyphOffsetsX, *glyphOffsetsY;
#else
	FT_Bitmap glyphs[128];
	bool glyphsRendered[128];
	int glyphOffsetsX[128], glyphOffsetsY[128];
#endif
#endif
} UIFont;

typedef struct UIShortcut {
	intptr_t code;
	bool ctrl, shift, alt;
	void (*invoke)(void *cp);
	void *cp;
} UIShortcut;

typedef struct UIStringSelection {
	int carets[2];
	uint32_t colorText, colorBackground;
} UIStringSelection;

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

typedef struct UICodeDecorateLine {
	UIRectangle bounds;
	int index; // Starting at 1!
	int x, y; // Position where additional text can be drawn.
	UIPainter *painter;
} UICodeDecorateLine;

#define UI_RECT_1(x) ((UIRectangle) { (x), (x), (x), (x) })
#define UI_RECT_1I(x) ((UIRectangle) { (x), -(x), (x), -(x) })
#define UI_RECT_2(x, y) ((UIRectangle) { (x), (x), (y), (y) })
#define UI_RECT_2I(x, y) ((UIRectangle) { (x), -(x), (y), -(y) })
#define UI_RECT_2S(x, y) ((UIRectangle) { 0, (x), 0, (y) })
#define UI_RECT_4(x, y, z, w) ((UIRectangle) { (x), (y), (z), (w) })
#define UI_RECT_4PD(x, y, w, h) ((UIRectangle) { (x), ((x) + (w)), (y), ((y) + (h)) })
#define UI_RECT_WIDTH(_r) ((_r).r - (_r).l)
#define UI_RECT_HEIGHT(_r) ((_r).b - (_r).t)
#define UI_RECT_TOTAL_H(_r) ((_r).r + (_r).l)
#define UI_RECT_TOTAL_V(_r) ((_r).b + (_r).t)
#define UI_RECT_SIZE(_r) UI_RECT_WIDTH(_r), UI_RECT_HEIGHT(_r)
#define UI_RECT_TOP_LEFT(_r) (_r).l, (_r).t
#define UI_RECT_BOTTOM_LEFT(_r) (_r).l, (_r).b
#define UI_RECT_BOTTOM_RIGHT(_r) (_r).r, (_r).b
#define UI_RECT_ALL(_r) (_r).l, (_r).r, (_r).t, (_r).b
#define UI_RECT_VALID(_r) ((_r).l < (_r).r && (_r).t < (_r).b)

#define UI_COLOR_ALPHA_F(x) ((((x) >> 24) & 0xFF) / 255.0f)
#define UI_COLOR_RED_F(x) ((((x) >> 16) & 0xFF) / 255.0f)
#define UI_COLOR_GREEN_F(x) ((((x) >> 8) & 0xFF) / 255.0f)
#define UI_COLOR_BLUE_F(x) ((((x) >> 0) & 0xFF) / 255.0f)
#define UI_COLOR_ALPHA(x) ((((x) >> 24) & 0xFF))
#define UI_COLOR_RED(x) ((((x) >> 16) & 0xFF))
#define UI_COLOR_GREEN(x) ((((x) >> 8) & 0xFF))
#define UI_COLOR_BLUE(x) ((((x) >> 0) & 0xFF))
#define UI_COLOR_FROM_FLOAT(r, g, b) (((uint32_t) ((r) * 255.0f) << 16) | ((uint32_t) ((g) * 255.0f) << 8) | ((uint32_t) ((b) * 255.0f) << 0))
#define UI_COLOR_FROM_RGBA_F(r, g, b, a) (((uint32_t) ((r) * 255.0f) << 16) | ((uint32_t) ((g) * 255.0f) << 8) \
		| ((uint32_t) ((b) * 255.0f) << 0) | ((uint32_t) ((a) * 255.0f) << 24))

#define UI_SWAP(s, a, b) do { s t = (a); (a) = (b); (b) = t; } while (0)

#ifndef UI_DRAW_CONTROL_CUSTOM
#define UIDrawControl UIDrawControlDefault
#endif
#define UI_DRAW_CONTROL_PUSH_BUTTON          (1)
#define UI_DRAW_CONTROL_DROP_DOWN            (2)
#define UI_DRAW_CONTROL_MENU_ITEM            (3)
#define UI_DRAW_CONTROL_CHECKBOX             (4)
#define UI_DRAW_CONTROL_LABEL                (5)
#define UI_DRAW_CONTROL_SPLITTER             (6)
#define UI_DRAW_CONTROL_SCROLL_TRACK         (7)
#define UI_DRAW_CONTROL_SCROLL_UP            (8)
#define UI_DRAW_CONTROL_SCROLL_DOWN          (9)
#define UI_DRAW_CONTROL_SCROLL_THUMB        (10)
#define UI_DRAW_CONTROL_GAUGE               (11)
#define UI_DRAW_CONTROL_SLIDER              (12)
#define UI_DRAW_CONTROL_TEXTBOX             (13)
#define UI_DRAW_CONTROL_MODAL_POPUP         (14)
#define UI_DRAW_CONTROL_MENU                (15)
#define UI_DRAW_CONTROL_TABLE_ROW           (16)
#define UI_DRAW_CONTROL_TABLE_CELL          (17)
#define UI_DRAW_CONTROL_TABLE_BACKGROUND    (18)
#define UI_DRAW_CONTROL_TABLE_HEADER        (19)
#define UI_DRAW_CONTROL_MDI_CHILD           (20)
#define UI_DRAW_CONTROL_TAB                 (21)
#define UI_DRAW_CONTROL_TAB_BAND            (22)
#define UI_DRAW_CONTROL_TYPE_MASK           (0xFF)
#define UI_DRAW_CONTROL_STATE_SELECTED      (1 << 24)
#define UI_DRAW_CONTROL_STATE_VERTICAL      (1 << 25)
#define UI_DRAW_CONTROL_STATE_INDETERMINATE (1 << 26)
#define UI_DRAW_CONTROL_STATE_CHECKED       (1 << 27)
#define UI_DRAW_CONTROL_STATE_HOVERED       (1 << 28)
#define UI_DRAW_CONTROL_STATE_FOCUSED       (1 << 29)
#define UI_DRAW_CONTROL_STATE_PRESSED       (1 << 30)
#define UI_DRAW_CONTROL_STATE_DISABLED      (1 << 31)
#define UI_DRAW_CONTROL_STATE_FROM_ELEMENT(x) ((((x)->flags & UI_ELEMENT_DISABLED) ? UI_DRAW_CONTROL_STATE_DISABLED : 0) \
		| (((x)->window->hovered == (x)) ? UI_DRAW_CONTROL_STATE_HOVERED : 0) \
		| (((x)->window->focused == (x)) ? UI_DRAW_CONTROL_STATE_FOCUSED : 0) \
		| (((x)->window->pressed == (x)) ? UI_DRAW_CONTROL_STATE_PRESSED : 0))

#define UI_CURSOR_ARROW (0)
#define UI_CURSOR_TEXT (1)
#define UI_CURSOR_SPLIT_V (2)
#define UI_CURSOR_SPLIT_H (3)
#define UI_CURSOR_FLIPPED_ARROW (4)
#define UI_CURSOR_CROSS_HAIR (5)
#define UI_CURSOR_HAND (6)
#define UI_CURSOR_RESIZE_UP (7)
#define UI_CURSOR_RESIZE_LEFT (8)
#define UI_CURSOR_RESIZE_UP_RIGHT (9)
#define UI_CURSOR_RESIZE_UP_LEFT (10)
#define UI_CURSOR_RESIZE_DOWN (11)
#define UI_CURSOR_RESIZE_RIGHT (12)
#define UI_CURSOR_RESIZE_DOWN_RIGHT (13)
#define UI_CURSOR_RESIZE_DOWN_LEFT (14)
#define UI_CURSOR_COUNT (15)

#define UI_ALIGN_LEFT (1)
#define UI_ALIGN_RIGHT (2)
#define UI_ALIGN_CENTER (3)

extern const int UI_KEYCODE_A;
extern const int UI_KEYCODE_BACKSPACE;
extern const int UI_KEYCODE_DELETE;
extern const int UI_KEYCODE_DOWN;
extern const int UI_KEYCODE_END;
extern const int UI_KEYCODE_ENTER;
extern const int UI_KEYCODE_ESCAPE;
extern const int UI_KEYCODE_F1;
extern const int UI_KEYCODE_HOME;
extern const int UI_KEYCODE_LEFT;
extern const int UI_KEYCODE_RIGHT;
extern const int UI_KEYCODE_SPACE;
extern const int UI_KEYCODE_TAB;
extern const int UI_KEYCODE_UP;
extern const int UI_KEYCODE_INSERT;
extern const int UI_KEYCODE_0;
extern const int UI_KEYCODE_BACKTICK;
extern const int UI_KEYCODE_PAGE_UP;
extern const int UI_KEYCODE_PAGE_DOWN;

#define UI_KEYCODE_LETTER(x) (UI_KEYCODE_A + (x) - 'A')
#define UI_KEYCODE_DIGIT(x) (UI_KEYCODE_0 + (x) - '0')
#define UI_KEYCODE_FKEY(x) (UI_KEYCODE_F1 + (x) - 1)

#define UI_ELEMENT_FILL (UI_ELEMENT_V_FILL | UI_ELEMENT_H_FILL)

typedef struct UIElement {
#define UI_ELEMENT_V_FILL (1 << 16)
#define UI_ELEMENT_H_FILL (1 << 17)
#define UI_ELEMENT_WINDOW (1 << 18)
#define UI_ELEMENT_PARENT_PUSH (1 << 19)
#define UI_ELEMENT_TAB_STOP (1 << 20)
#define UI_ELEMENT_NON_CLIENT (1 << 21) // Don't destroy in UIElementDestroyDescendents, like scroll bars.
#define UI_ELEMENT_DISABLED (1 << 22) // Don't receive input events.
#define UI_ELEMENT_BORDER (1 << 23)

#define UI_ELEMENT_HIDE (1 << 27)
#define UI_ELEMENT_RELAYOUT (1 << 28)
#define UI_ELEMENT_RELAYOUT_DESCENDENT (1 << 29)
#define UI_ELEMENT_DESTROY (1 << 30)
#define UI_ELEMENT_DESTROY_DESCENDENT (1 << 31)

	uint32_t flags; // First 16 bits are element specific.
	uint32_t id;
	uint32_t childCount;
	uint32_t _unused0;

	struct UIElement *parent;
	struct UIElement **children;
	struct UIWindow *window;

	UIRectangle bounds, clip;

	void *cp; // Context pointer (for user).

	int (*messageClass)(struct UIElement *element, UIMessage message, int di /* data integer */, void *dp /* data pointer */);
	int (*messageUser)(struct UIElement *element, UIMessage message, int di, void *dp);

	const char *cClassName;
} UIElement;

#define UI_SHORTCUT(code, ctrl, shift, alt, invoke, cp) ((UIShortcut) { (code), (ctrl), (shift), (alt), (invoke), (cp) })

typedef struct UIWindow {
#define UI_WINDOW_MENU (1 << 0)
#define UI_WINDOW_INSPECTOR (1 << 1)
#define UI_WINDOW_CENTER_IN_OWNER (1 << 2)
#define UI_WINDOW_MAXIMIZE (1 << 3)

	UIElement e;

	UIElement *dialog;

	UIShortcut *shortcuts;
	size_t shortcutCount, shortcutAllocated;

	float scale;

	uint32_t *bits;
	int width, height;
	struct UIWindow *next;

	UIElement *hovered, *pressed, *focused, *dialogOldFocus;
	int pressedButton;

	int cursorX, cursorY;
	int cursorStyle;

	// Set when a textbox is modified.
	// Useful for tracking whether changes to the loaded document have been saved.
	bool textboxModifiedFlag;

	bool ctrl, shift, alt;

	UIRectangle updateRegion;

#ifdef UI_DEBUG
	float lastFullFillCount;
#endif

#ifdef UI_LINUX
	Window window;
	XImage *image;
	XIC xic;
	unsigned ctrlCode, shiftCode, altCode;
	Window dragSource;
#endif

#ifdef UI_WINDOWS
	HWND hwnd;
	bool trackingLeave;
#endif

#ifdef UI_ESSENCE
	EsWindow *window;
	EsElement *canvas;
	int cursor;
#endif

#ifdef UI_COCOA
	NSWindow *window;
	void *view;
#endif
} UIWindow;

typedef struct UIPanel {
#define UI_PANEL_HORIZONTAL (1 << 0)
#define UI_PANEL_COLOR_1 (1 << 2)
#define UI_PANEL_COLOR_2 (1 << 3)
#define UI_PANEL_SMALL_SPACING (1 << 5)
#define UI_PANEL_MEDIUM_SPACING (1 << 6)
#define UI_PANEL_LARGE_SPACING (1 << 7)
#define UI_PANEL_SCROLL (1 << 8)
#define UI_PANEL_EXPAND (1 << 9)
	UIElement e;
	struct UIScrollBar *scrollBar;
	UIRectangle border;
	int gap;
} UIPanel;

typedef struct UIButton {
#define UI_BUTTON_SMALL (1 << 0)
#define UI_BUTTON_MENU_ITEM (1 << 1)
#define UI_BUTTON_CAN_FOCUS (1 << 2)
#define UI_BUTTON_DROP_DOWN (1 << 3)
#define UI_BUTTON_CHECKED (1 << 15)
	UIElement e;
	char *label;
	ptrdiff_t labelBytes;
	void (*invoke)(void *cp);
} UIButton;

typedef struct UICheckbox {
#define UI_CHECKBOX_ALLOW_INDETERMINATE (1 << 0)
	UIElement e;
#define UI_CHECK_UNCHECKED (0)
#define UI_CHECK_CHECKED (1)
#define UI_CHECK_INDETERMINATE (2)
	uint8_t check;
	char *label;
	ptrdiff_t labelBytes;
	void (*invoke)(void *cp);
} UICheckbox;

typedef struct UILabel {
	UIElement e;
	char *label;
	ptrdiff_t labelBytes;
} UILabel;

typedef struct UISpacer {
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
	uint32_t active;
} UITabPane;

typedef struct UIScrollBar {
#define UI_SCROLL_BAR_HORIZONTAL (1 << 0)
	UIElement e;
	int64_t maximum, page;
	int64_t dragOffset;
	double position;
	UI_CLOCK_T lastAnimateTime;
	bool inDrag, horizontal;
} UIScrollBar;

#define _UI_LAYOUT_SCROLL_BAR_PAIR(element) \
		element->vScroll->page = vSpace - (element->hScroll->page < element->hScroll->maximum ? scrollBarSize : 0); \
		element->hScroll->page = hSpace - (element->vScroll->page < element->vScroll->maximum ? scrollBarSize : 0); \
		element->vScroll->page = vSpace - (element->hScroll->page < element->hScroll->maximum ? scrollBarSize : 0); \
		UIRectangle vScrollBarBounds = element->e.bounds, hScrollBarBounds = element->e.bounds; \
		hScrollBarBounds.r = vScrollBarBounds.l = vScrollBarBounds.r - (element->vScroll->page < element->vScroll->maximum ? scrollBarSize : 0); \
		vScrollBarBounds.b = hScrollBarBounds.t = hScrollBarBounds.b - (element->hScroll->page < element->hScroll->maximum ? scrollBarSize : 0); \
		UIElementMove(&element->vScroll->e, vScrollBarBounds, true); \
		UIElementMove(&element->hScroll->e, hScrollBarBounds, true);
#define _UI_KEY_INPUT_VSCROLL(element, rowHeight, pageHeight) \
		if (m->code == UI_KEYCODE_UP) element->vScroll->position -= (rowHeight); \
		else if (m->code == UI_KEYCODE_DOWN) element->vScroll->position += (rowHeight); \
		else if (m->code == UI_KEYCODE_PAGE_UP) element->vScroll->position += (pageHeight); \
		else if (m->code == UI_KEYCODE_PAGE_DOWN) element->vScroll->position -= (pageHeight); \
		else if (m->code == UI_KEYCODE_HOME) element->vScroll->position = 0; \
		else if (m->code == UI_KEYCODE_END) element->vScroll->position = element->vScroll->maximum; \
		UIElementRefresh(&element->e);

typedef struct UICodeLine {
	int offset, bytes;
} UICodeLine;

typedef struct UICode {
#define UI_CODE_NO_MARGIN (1 << 0)
#define UI_CODE_SELECTABLE (1 << 1)
	UIElement e;
	UIScrollBar *vScroll, *hScroll;
	UICodeLine *lines;
	UIFont *font;
	int lineCount, focused;
	bool moveScrollToFocusNextLayout;
	bool leftDownInMargin;
	char *content;
	size_t contentBytes;
	int tabSize;
	int columns;
	UI_CLOCK_T lastAnimateTime;
	struct { int line, offset; } selection[4 /* start, end, anchor, caret */];
	int verticalMotionColumn;
	bool useVerticalMotionColumn;
	bool moveScrollToCaretNextLayout;
} UICode;

typedef struct UIGauge {
	UIElement e;
	double position;
} UIGauge;

typedef struct UITable {
	UIElement e;
	UIScrollBar *vScroll, *hScroll;
	int itemCount;
	char *columns;
	int *columnWidths, columnCount, columnHighlight;
} UITable;

typedef struct UITextbox {
	UIElement e;
	char *string;
	ptrdiff_t bytes;
	int carets[2];
	int scroll;
	bool rejectNextKey;
} UITextbox;

#define UI_MENU_PLACE_ABOVE (1 << 0)
#define UI_MENU_NO_SCROLL (1 << 1)
#if defined(UI_COCOA)
typedef NSMenu UIMenu;
#elif defined(UI_ESSENCE)
typedef EsMenu UIMenu;
#else
typedef struct UIMenu {
	UIElement e;
	int pointX, pointY;
	UIScrollBar *vScroll;
	UIWindow *parentWindow;
} UIMenu;
#endif

typedef struct UISlider {
	UIElement e;
	double position;
	int steps;
} UISlider;

typedef struct UIMDIClient {
#define UI_MDI_CLIENT_TRANSPARENT (1 << 0)
	UIElement e;
	struct UIMDIChild *active;
	int cascade;
} UIMDIClient;

typedef struct UIMDIChild {
#define UI_MDI_CHILD_CLOSE_BUTTON (1 << 0)
	UIElement e;
	UIRectangle bounds;
	char *title;
	ptrdiff_t titleBytes;
	int dragHitTest;
	UIRectangle dragOffset;
} UIMDIChild;

typedef struct UIExpandPane {
	UIElement e;
	UIButton *button;
	UIPanel *panel;
	bool expanded;
} UIExpandPane;

typedef struct UIImageDisplay {
#define UI_IMAGE_DISPLAY_INTERACTIVE (1 << 0)
#define _UI_IMAGE_DISPLAY_ZOOM_FIT (1 << 1)

	UIElement e;
	uint32_t *bits;
	int width, height;
	float panX, panY, zoom;

	// Internals:
	int previousWidth, previousHeight;
	int previousPanPointX, previousPanPointY;
} UIImageDisplay;

typedef struct UIWrapPanel {
	UIElement e;
} UIWrapPanel;

typedef struct UISwitcher {
	UIElement e;
	UIElement *active;
} UISwitcher;

void UIInitialise();
int UIMessageLoop();

UIElement *UIElementCreate(size_t bytes, UIElement *parent, uint32_t flags,
	int (*messageClass)(UIElement *, UIMessage, int, void *), const char *cClassName);

UICheckbox *UICheckboxCreate(UIElement *parent, uint32_t flags, const char *label, ptrdiff_t labelBytes);
UIExpandPane *UIExpandPaneCreate(UIElement *parent, uint32_t flags, const char *label, ptrdiff_t labelBytes, uint32_t panelFlags);
UIMDIClient *UIMDIClientCreate(UIElement *parent, uint32_t flags);
UIMDIChild *UIMDIChildCreate(UIElement *parent, uint32_t flags, UIRectangle initialBounds, const char *title, ptrdiff_t titleBytes);
UIPanel *UIPanelCreate(UIElement *parent, uint32_t flags);
UIScrollBar *UIScrollBarCreate(UIElement *parent, uint32_t flags);
UISlider *UISliderCreate(UIElement *parent, uint32_t flags);
UISpacer *UISpacerCreate(UIElement *parent, uint32_t flags, int width, int height);
UISplitPane *UISplitPaneCreate(UIElement *parent, uint32_t flags, float weight);
UITabPane *UITabPaneCreate(UIElement *parent, uint32_t flags, const char *tabs /* separate with \t, terminate with \0 */);
UIWrapPanel *UIWrapPanelCreate(UIElement *parent, uint32_t flags);

UIGauge *UIGaugeCreate(UIElement *parent, uint32_t flags);
void UIGaugeSetPosition(UIGauge *gauge, float value);

UIButton *UIButtonCreate(UIElement *parent, uint32_t flags, const char *label, ptrdiff_t labelBytes);
void UIButtonSetLabel(UIButton *button, const char *string, ptrdiff_t stringBytes);
UILabel *UILabelCreate(UIElement *parent, uint32_t flags, const char *label, ptrdiff_t labelBytes);
void UILabelSetContent(UILabel *code, const char *content, ptrdiff_t byteCount);

UIImageDisplay *UIImageDisplayCreate(UIElement *parent, uint32_t flags, uint32_t *bits, size_t width, size_t height, size_t stride);
void UIImageDisplaySetContent(UIImageDisplay *display, uint32_t *bits, size_t width, size_t height, size_t stride);

UISwitcher *UISwitcherCreate(UIElement *parent, uint32_t flags);
void UISwitcherSwitchTo(UISwitcher *switcher, UIElement *child);

UIWindow *UIWindowCreate(UIWindow *owner, uint32_t flags, const char *cTitle, int width, int height);
void UIWindowRegisterShortcut(UIWindow *window, UIShortcut shortcut);
void UIWindowPostMessage(UIWindow *window, UIMessage message, void *dp); // Thread-safe.
void UIWindowPack(UIWindow *window, int width); // Change the size of the window to best match its contents.

typedef void (*UIDialogUserCallback)(UIElement *);
const char *UIDialogShow(UIWindow *window, uint32_t flags, const char *format, ...);

UIMenu *UIMenuCreate(UIElement *parent, uint32_t flags);
void UIMenuAddItem(UIMenu *menu, uint32_t flags, const char *label, ptrdiff_t labelBytes, void (*invoke)(void *cp), void *cp);
void UIMenuShow(UIMenu *menu);
bool UIMenusOpen();

UITextbox *UITextboxCreate(UIElement *parent, uint32_t flags);
void UITextboxReplace(UITextbox *textbox, const char *text, ptrdiff_t bytes, bool sendChangedMessage);
void UITextboxClear(UITextbox *textbox, bool sendChangedMessage);
void UITextboxMoveCaret(UITextbox *textbox, bool backward, bool word);
char *UITextboxToCString(UITextbox *textbox); // Free with UI_FREE.

UITable *UITableCreate(UIElement *parent, uint32_t flags, const char *columns /* separate with \t, terminate with \0 */);
int UITableHitTest(UITable *table, int x, int y); // Returns item index. Returns -1 if not on an item.
int UITableHeaderHitTest(UITable *table, int x, int y); // Returns column index or -1.
bool UITableEnsureVisible(UITable *table, int index); // Returns false if the item was already visible.
void UITableResizeColumns(UITable *table);

UICode *UICodeCreate(UIElement *parent, uint32_t flags);
void UICodeFocusLine(UICode *code, int index); // Line numbers are 1-indexed!!
int UICodeHitTest(UICode *code, int x, int y); // Returns line number; negates if in margin. Returns 0 if not on a line.
void UICodePositionToByte(UICode *code, int x, int y, int *line, int *byte);
void UICodeInsertContent(UICode *code, const char *content, ptrdiff_t byteCount, bool replace);
void UICodeMoveCaret(UICode *code, bool backward, bool word);

void UIDrawBlock(UIPainter *painter, UIRectangle rectangle, uint32_t color);
void UIDrawCircle(UIPainter *painter, int centerX, int centerY, int radius, uint32_t fillColor, uint32_t outlineColor, bool hollow);
void UIDrawControl(UIPainter *painter, UIRectangle bounds, uint32_t mode /* UI_DRAW_CONTROL_* */, const char *label, ptrdiff_t labelBytes, double position, float scale);
void UIDrawControlDefault(UIPainter *painter, UIRectangle bounds, uint32_t mode, const char *label, ptrdiff_t labelBytes, double position, float scale);
void UIDrawInvert(UIPainter *painter, UIRectangle rectangle);
bool UIDrawLine(UIPainter *painter, int x0, int y0, int x1, int y1, uint32_t color); // Returns false if the line was not visible.
void UIDrawTriangle(UIPainter *painter, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color);
void UIDrawTriangleOutline(UIPainter *painter, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color);
void UIDrawGlyph(UIPainter *painter, int x, int y, int c, uint32_t color);
void UIDrawRectangle(UIPainter *painter, UIRectangle r, uint32_t mainColor, uint32_t borderColor, UIRectangle borderSize);
void UIDrawBorder(UIPainter *painter, UIRectangle r, uint32_t borderColor, UIRectangle borderSize);
void UIDrawString(UIPainter *painter, UIRectangle r, const char *string, ptrdiff_t bytes, uint32_t color, int align, UIStringSelection *selection);
int  UIDrawStringHighlighted(UIPainter *painter, UIRectangle r, const char *string, ptrdiff_t bytes, int tabSize, UIStringSelection *selection); // Returns final x position.

int UIMeasureStringWidth(const char *string, ptrdiff_t bytes);
int UIMeasureStringHeight();

uint64_t UIAnimateClock(); // In ms.

bool UIElementAnimate(UIElement *element, bool stop);
void UIElementDestroy(UIElement *element);
void UIElementDestroyDescendents(UIElement *element);
UIElement *UIElementFindByPoint(UIElement *element, int x, int y);
void UIElementFocus(UIElement *element);
UIRectangle UIElementScreenBounds(UIElement *element); // Returns bounds of element in same coordinate system as used by UIWindowCreate.
void UIElementRefresh(UIElement *element);
void UIElementRelayout(UIElement *element);
void UIElementRepaint(UIElement *element, UIRectangle *region);
void UIElementMeasurementsChanged(UIElement *element, int which);
void UIElementMove(UIElement *element, UIRectangle bounds, bool alwaysLayout);
int UIElementMessage(UIElement *element, UIMessage message, int di, void *dp);
UIElement *UIElementChangeParent(UIElement *element, UIElement *newParent, UIElement *insertBefore); // Set insertBefore to null to insert at the end. Returns the element it was before in its previous parent, or NULL.

UIElement *UIParentPush(UIElement *element);
UIElement *UIParentPop();

UIRectangle UIRectangleIntersection(UIRectangle a, UIRectangle b);
UIRectangle UIRectangleBounding(UIRectangle a, UIRectangle b);
UIRectangle UIRectangleAdd(UIRectangle a, UIRectangle b);
UIRectangle UIRectangleTranslate(UIRectangle a, UIRectangle b);
UIRectangle UIRectangleCenter(UIRectangle parent, UIRectangle child);
UIRectangle UIRectangleFit(UIRectangle parent, UIRectangle child, bool allowScalingUp);
bool UIRectangleEquals(UIRectangle a, UIRectangle b);
bool UIRectangleContains(UIRectangle a, int x, int y);

bool UIColorToHSV(uint32_t rgb, float *hue, float *saturation, float *value);
void UIColorToRGB(float hue, float saturation, float value, uint32_t *rgb);

char *UIStringCopy(const char *in, ptrdiff_t inBytes);

UIFont *UIFontCreate(const char *cPath, uint32_t size);
UIFont *UIFontActivate(UIFont *font); // Returns the previously active font.

#ifdef UI_DEBUG
void UIInspectorLog(const char *cFormat, ...);
#endif

ptrdiff_t _UIStringLength(const char *cString) {
	if (!cString) return 0;
	ptrdiff_t length;
	for (length = 0; cString[length]; length++);
	return length;
}

#ifdef UI_UNICODE

#ifndef UI_FREETYPE
#error "Unicode support requires Freetype"
#endif

#define _UNICODE_MAX_CODEPOINT 0x10FFFF

int Utf8GetCodePoint(const char *cString, ptrdiff_t bytesLength, ptrdiff_t *bytesConsumed) {
	UI_ASSERT(bytesLength > 0 && "Attempted to get UTF-8 code point from an empty string");

	if (bytesConsumed == NULL) {
		ptrdiff_t bytesConsumed;
		return Utf8GetCodePoint(cString, bytesLength, &bytesConsumed);
	}

	ptrdiff_t numExtraBytes;
	uint8_t first = cString[0];

	*bytesConsumed = 1;
	if ((first & 0xF0) == 0xF0) {
		numExtraBytes = 3;
	} else if ((first & 0xE0) == 0xE0) {
		numExtraBytes = 2;
	} else if ((first & 0xC0) == 0xC0) {
		numExtraBytes = 1;
	} else if (first & 0x7F) {
		return first & 0x80 ? -1 : first;
	} else {
		return -1;
	}

	if (bytesLength < numExtraBytes + 1) {
		return -1;
	}

	int codePoint = ((int)first & (0x3F >> numExtraBytes)) << (6 * numExtraBytes);
	for (ptrdiff_t idx = 1; idx < numExtraBytes + 1; idx++) {
		char byte = cString[idx];
		if ((byte & 0xC0) != 0x80) {
			return -1;
		}

		codePoint |= (byte & 0x3F) << (6 * (numExtraBytes - idx));
		(*bytesConsumed)++;
	}

	return codePoint > _UNICODE_MAX_CODEPOINT ? -1 : codePoint;
}

char * Utf8GetPreviousChar(char *string, char *offset) {
	if (string == offset) {
		return string;
	}

	char *prev = offset - 1;
	while (prev > string) {
		if ((*prev & 0xC0) == 0x80) prev--;
		else break;
	}

	return prev;
}

ptrdiff_t Utf8GetCharBytes(const char *cString, ptrdiff_t bytes) {
	if (!cString) {
		return 0;
	}
	if (bytes == -1) {
		bytes = _UIStringLength(cString);
	}

	ptrdiff_t bytesConsumed;
	Utf8GetCodePoint(cString, bytes, &bytesConsumed);
	return bytesConsumed;
}

ptrdiff_t Utf8StringLength(const char *cString, ptrdiff_t bytes) {
	if (!cString) {
		return 0;
	}
	if (bytes == -1) {
		bytes = _UIStringLength(cString);
	}

	ptrdiff_t length = 0;
	ptrdiff_t byteIndex = 0;
	while (byteIndex < bytes) {
		ptrdiff_t bytesConsumed;
		Utf8GetCodePoint(cString+ byteIndex, bytes - byteIndex, &bytesConsumed);
		byteIndex += bytesConsumed;
		length++;

		UI_ASSERT(byteIndex <= bytes && "Overran the end of the string while counting the number of UTF-8 code points");
	}

	return length;
}

#endif // UI_UNICODE


#ifdef UI_IMPLEMENTATION

/////////////////////////////////////////
// Global variables.
/////////////////////////////////////////

struct {
	UIWindow *windows;
	UITheme theme;

	UIElement **animating;
	uint32_t animatingCount;

	UIElement *parentStack[16];
	int parentStackCount;

	bool quit;
	const char *dialogResult;
	UIElement *dialogOldFocus;
	bool dialogCanExit;

	UIFont *activeFont;

#ifdef UI_DEBUG
	UIWindow *inspector;
	UITable *inspectorTable;
	UIWindow *inspectorTarget;
	UICode *inspectorLog;
#endif

#ifdef UI_LINUX
	Display *display;
	Visual *visual;
	XIM xim;
	Atom windowClosedID, primaryID, uriListID, plainTextID;
	Atom dndEnterID, dndPositionID, dndStatusID, dndActionCopyID, dndDropID, dndSelectionID, dndFinishedID, dndAwareID;
	Atom clipboardID, xSelectionDataID, textID, targetID, incrID;
	Cursor cursors[UI_CURSOR_COUNT];
	char *pasteText;
	XEvent copyEvent;
#endif

#ifdef UI_WINDOWS
	HCURSOR cursors[UI_CURSOR_COUNT];
	HANDLE heap;
	bool assertionFailure;
#endif

#ifdef UI_ESSENCE
	EsInstance *instance;
#endif

#if defined(UI_ESSENCE) || defined(UI_COCOA)
	void *menuData[256]; // HACK This limits the number of menu items to 128.
	uintptr_t menuIndex;
#endif

#ifdef UI_COCOA
	int menuX, menuY;
	UIWindow *menuWindow;
#endif

#ifdef UI_FREETYPE
	FT_Library ft;
#endif
} ui;

/////////////////////////////////////////
// Themes.
/////////////////////////////////////////

UITheme uiThemeClassic = {
	.panel1 = 0xFFF0F0F0,
	.panel2 = 0xFFFFFFFF,
	.selected = 0xFF94BEFE,
	.border = 0xFF404040,

	.text = 0xFF000000,
	.textDisabled = 0xFF404040,
	.textSelected = 0xFF000000,

	.buttonNormal = 0xFFE0E0E0,
	.buttonHovered = 0xFFF0F0F0,
	.buttonPressed = 0xFFA0A0A0,
	.buttonDisabled = 0xFFF0F0F0,

	.textboxNormal = 0xFFF8F8F8,
	.textboxFocused = 0xFFFFFFFF,

	.codeFocused = 0xFFE0E0E0,
	.codeBackground = 0xFFFFFFFF,
	.codeDefault = 0xFF000000,
	.codeComment = 0xFFA11F20,
	.codeString = 0xFF037E01,
	.codeNumber = 0xFF213EF1,
	.codeOperator = 0xFF7F0480,
	.codePreprocessor = 0xFF545D70,

	.accent1 = 0xFF0000,
	.accent2 = 0x00FF00,
};

UITheme uiThemeDark = {
	.panel1 = 0xFF252B31,
	.panel2 = 0xFF14181E,
	.selected = 0xFF94BEFE,
	.border = 0xFF000000,

	.text = 0xFFFFFFFF,
	.textDisabled = 0xFF787D81,
	.textSelected = 0xFF000000,

	.buttonNormal = 0xFF383D41,
	.buttonHovered = 0xFF4B5874,
	.buttonPressed = 0xFF0D0D0F,
	.buttonDisabled = 0xFF1B1F23,

	.textboxNormal = 0xFF31353C,
	.textboxFocused = 0xFF4D4D59,

	.codeFocused = 0xFF505055,
	.codeBackground = 0xFF212126,
	.codeDefault = 0xFFFFFFFF,
	.codeComment = 0xFFB4B4B4,
	.codeString = 0xFFF5DDD1,
	.codeNumber = 0xFFC3F5D3,
	.codeOperator = 0xFFF5D499,
	.codePreprocessor = 0xFFF5F3D1,

	.accent1 = 0xF01231,
	.accent2 = 0x45F94E,
};

/////////////////////////////////////////
// Forward declarations.
/////////////////////////////////////////

void _UIWindowEndPaint(UIWindow *window, UIPainter *painter);
void _UIWindowSetCursor(UIWindow *window, int cursor);
void _UIWindowGetScreenPosition(UIWindow *window, int *x, int *y);
void _UIWindowSetPressed(UIWindow *window, UIElement *element, int button);
void _UIClipboardWriteText(UIWindow *window, char *text);
char *_UIClipboardReadTextStart(UIWindow *window, size_t *bytes);
void _UIClipboardReadTextEnd(UIWindow *window, char *text);
bool _UIMessageLoopSingle(int *result);
void _UIInspectorRefresh();
void _UIUpdate();

#if defined(UI_LINUX) || defined(UI_COCOA)
UI_CLOCK_T _UIClock() {
	struct timespec spec;
	clock_gettime(CLOCK_REALTIME, &spec);
	return spec.tv_sec * 1000 + spec.tv_nsec / 1000000;
}
#endif

#ifdef UI_WINDOWS
void *_UIHeapReAlloc(void *pointer, size_t size);
void *_UIMemmove(void *dest, const void *src, size_t n);
#endif

/////////////////////////////////////////
// Helper functions.
/////////////////////////////////////////

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

UIRectangle UIRectangleCenter(UIRectangle parent, UIRectangle child) {
	int childWidth = UI_RECT_WIDTH(child), childHeight = UI_RECT_HEIGHT(child);
	int parentWidth = UI_RECT_WIDTH(parent), parentHeight = UI_RECT_HEIGHT(parent);
	child.l = parentWidth / 2 - childWidth / 2 + parent.l, child.r = child.l + childWidth;
	child.t = parentHeight / 2 - childHeight / 2 + parent.t, child.b = child.t + childHeight;
	return child;
}

UIRectangle UIRectangleFit(UIRectangle parent, UIRectangle child, bool allowScalingUp) {
	int childWidth = UI_RECT_WIDTH(child), childHeight = UI_RECT_HEIGHT(child);
	int parentWidth = UI_RECT_WIDTH(parent), parentHeight = UI_RECT_HEIGHT(parent);

	if (childWidth < parentWidth && childHeight < parentHeight && !allowScalingUp) {
		return UIRectangleCenter(parent, child);
	}

	float childAspectRatio = (float) childWidth / childHeight;
	int childMaximumWidth = parentHeight * childAspectRatio;
	int childMaximumHeight = parentWidth / childAspectRatio;

	if (childMaximumWidth > parentWidth) {
		return UIRectangleCenter(parent, UI_RECT_2S(parentWidth, childMaximumHeight));
	} else {
		return UIRectangleCenter(parent, UI_RECT_2S(childMaximumWidth, parentHeight));
	}
}

bool UIRectangleEquals(UIRectangle a, UIRectangle b) {
	return a.l == b.l && a.r == b.r && a.t == b.t && a.b == b.b;
}

bool UIRectangleContains(UIRectangle a, int x, int y) {
	return a.l <= x && a.r > x && a.t <= y && a.b > y;
}

typedef union _UIConvertFloatInteger {
	float f;
	uint32_t i;
} _UIConvertFloatInteger;

float _UIFloorFloat(float x) {
	_UIConvertFloatInteger convert = {x};
	uint32_t sign = convert.i & 0x80000000;
	int exponent = (int) ((convert.i >> 23) & 0xFF) - 0x7F;

	if (exponent >= 23) {
		// There aren't any bits representing a fractional part.
	} else if (exponent >= 0) {
		// Positive exponent.
		uint32_t mask = 0x7FFFFF >> exponent;
		if (!(mask & convert.i)) return x; // Already an integer.
		if (sign) convert.i += mask;
		convert.i &= ~mask; // Mask out the fractional bits.
	} else if (exponent < 0) {
		// Negative exponent.
		return sign ? -1.0 : 0.0;
	}

	return convert.f;
}

float _UILinearMap(float value, float inFrom, float inTo, float outFrom, float outTo) {
	float inRange = inTo - inFrom, outRange = outTo - outFrom;
	float normalisedValue = (value - inFrom) / inRange;
	return normalisedValue * outRange + outFrom;
}

bool UIColorToHSV(uint32_t rgb, float *hue, float *saturation, float *value) {
	float r = UI_COLOR_RED_F(rgb);
	float g = UI_COLOR_GREEN_F(rgb);
	float b = UI_COLOR_BLUE_F(rgb);

	float maximum = (r > g && r > b) ? r : (g > b ? g : b),
	      minimum = (r < g && r < b) ? r : (g < b ? g : b),
	      difference = maximum - minimum;
	*value = maximum;

	if (!difference) {
		*saturation = 0;
		return false;
	} else {
		if (r == maximum) *hue = (g - b) / difference + 0;
		if (g == maximum) *hue = (b - r) / difference + 2;
		if (b == maximum) *hue = (r - g) / difference + 4;
		if (*hue < 0) *hue += 6;
		*saturation = difference / maximum;
		return true;
	}
}

void UIColorToRGB(float h, float s, float v, uint32_t *rgb) {
	float r, g, b;

	if (!s) {
		r = g = b = v;
	} else {
		int h0 = ((int) h) % 6;
		float f = h - _UIFloorFloat(h);
		float x = v * (1 - s), y = v * (1 - s * f), z = v * (1 - s * (1 - f));

		switch (h0) {
			case 0:  r = v, g = z, b = x; break;
			case 1:  r = y, g = v, b = x; break;
			case 2:  r = x, g = v, b = z; break;
			case 3:  r = x, g = y, b = v; break;
			case 4:  r = z, g = x, b = v; break;
			default: r = v, g = x, b = y; break;
		}
	}

	*rgb = UI_COLOR_FROM_FLOAT(r, g, b);
}

char *UIStringCopy(const char *in, ptrdiff_t inBytes) {
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

/////////////////////////////////////////
// Animations.
/////////////////////////////////////////

bool UIElementAnimate(UIElement *element, bool stop) {
	if (stop) {
		for (uint32_t i = 0; i < ui.animatingCount; i++) {
			if (ui.animating[i] == element) {
				ui.animating[i] = ui.animating[ui.animatingCount - 1];
				ui.animatingCount--;
				return true;
			}
		}

		return false;
	} else {
		for (uint32_t i = 0; i < ui.animatingCount; i++) {
			if (ui.animating[i] == element) {
				return true;
			}
		}

		ui.animating = (UIElement **) UI_REALLOC(ui.animating, sizeof(UIElement *) * (ui.animatingCount + 1));
		ui.animating[ui.animatingCount] = element;
		ui.animatingCount++;
		UI_ASSERT(~element->flags & UI_ELEMENT_DESTROY);
		return true;
	}
}

uint64_t UIAnimateClock() {
	return (uint64_t) UI_CLOCK() * 1000 / UI_CLOCKS_PER_SECOND;
}

void _UIProcessAnimations() {
	bool update = ui.animatingCount;

	for (uint32_t i = 0; i < ui.animatingCount; i++) {
		UIElementMessage(ui.animating[i], UI_MSG_ANIMATE, 0, 0);
	}

	if (update) {
		_UIUpdate();
	}
}

/////////////////////////////////////////
// Rendering.
/////////////////////////////////////////

void UIDrawBlock(UIPainter *painter, UIRectangle rectangle, uint32_t color) {
	rectangle = UIRectangleIntersection(painter->clip, rectangle);

	if (!UI_RECT_VALID(rectangle)) {
		return;
	}

#ifdef UI_SSE2
	__m128i color4 = _mm_set_epi32(color, color, color, color);
#endif

	for (int line = rectangle.t; line < rectangle.b; line++) {
		uint32_t *bits = painter->bits + line * painter->width + rectangle.l;
		int count = UI_RECT_WIDTH(rectangle);

#ifdef UI_SSE2
		while (count >= 4) {
			_mm_storeu_si128((__m128i *) bits, color4);
			bits += 4;
			count -= 4;
		}
#endif

		while (count--) {
			*bits++ = color;
		}
	}

#ifdef UI_DEBUG
	painter->fillCount += UI_RECT_WIDTH(rectangle) * UI_RECT_HEIGHT(rectangle);
#endif
}

bool UIDrawLine(UIPainter *painter, int x0, int y0, int x1, int y1, uint32_t color) {
	// Apply the clip.

	UIRectangle c = painter->clip;
	if (!UI_RECT_VALID(c)) return false;
	int dx = x1 - x0, dy = y1 - y0;
	const int p[4] = { -dx, dx, -dy, dy };
	const int q[4] = { x0 - c.l, c.r - 1 - x0, y0 - c.t, c.b - 1 - y0 };
	float t0 = 0.0f, t1 = 1.0f; // How far along the line the points end up.

	for (int i = 0; i < 4; i++) {
		if (!p[i] && q[i] < 0) return false;
		float r = (float) q[i] / p[i];
		if (p[i] < 0 && r > t1) return false;
		if (p[i] > 0 && r < t0) return false;
		if (p[i] < 0 && r > t0) t0 = r;
		if (p[i] > 0 && r < t1) t1 = r;
	}

	x1 = x0 + t1 * dx, y1 = y0 + t1 * dy;
	x0 += t0 * dx, y0 += t0 * dy;

	// Calculate the delta X and delta Y.

	if (y1 < y0) {
		int t;
		t = x0, x0 = x1, x1 = t;
		t = y0, y0 = y1, y1 = t;
	}

	dx = x1 - x0, dy = y1 - y0;
	int dxs = dx < 0 ? -1 : 1;
	if (dx < 0) dx = -dx;

	// Draw the line using Bresenham's line algorithm.

	uint32_t *bits = painter->bits + y0 * painter->width + x0;

	if (dy * dy < dx * dx) {
		int m = 2 * dy - dx;

		for (int i = 0; i < dx; i++, bits += dxs) {
			*bits = color;
			if (m > 0) bits += painter->width, m -= 2 * dx;
			m += 2 * dy;
		}
	} else {
		int m = 2 * dx - dy;

		for (int i = 0; i < dy; i++, bits += painter->width) {
			*bits = color;
			if (m > 0) bits += dxs, m -= 2 * dy;
			m += 2 * dx;
		}
	}

	return true;
}

void UIDrawCircle(UIPainter *painter, int cx, int cy, int radius, uint32_t fillColor, uint32_t outlineColor, bool hollow) {
	// TODO There's a hole missing at the bottom of the circle!
	// TODO This looks bad at small radii (< 20).

	float x = 0, y = -radius;
	float dx = radius, dy = 0;
	float step = 0.2f / radius;
	int px = 0, py = cy + y;

	while (x >= 0) {
		x  += dx * step;
		y  += dy * step;
		dx += -x * step;
		dy += -y * step;

		int ix = x, iy = cy + y;

		while (py <= iy) {
			if (py >= painter->clip.t && py < painter->clip.b) {
				for (int s = 0; s <= ix || s <= px; s++) {
					bool inOutline = ((s <= ix) != (s <= px)) || ((ix == px) && (s == ix));
					if (hollow && !inOutline) continue;
					bool clip0 = cx + s >= painter->clip.l && cx + s < painter->clip.r;
					bool clip1 = cx - s >= painter->clip.l && cx - s < painter->clip.r;
					if (clip0) painter->bits[painter->width * py + cx + s] = inOutline ? outlineColor : fillColor;
					if (clip1) painter->bits[painter->width * py + cx - s] = inOutline ? outlineColor : fillColor;
				}
			}

			px = ix, py++;
		}
	}
}

void UIDrawTriangle(UIPainter *painter, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
	// Step 1: Sort the points by their y-coordinate.
	if (y1 < y0) { int xt = x0; x0 = x1, x1 = xt; int yt = y0; y0 = y1, y1 = yt; }
	if (y2 < y1) { int xt = x1; x1 = x2, x2 = xt; int yt = y1; y1 = y2, y2 = yt; }
	if (y1 < y0) { int xt = x0; x0 = x1, x1 = xt; int yt = y0; y0 = y1, y1 = yt; }
	if (y2 == y0) return;

	// Step 2: Clip the triangle.
	if (x0 < painter->clip.l && x1 < painter->clip.l && x2 < painter->clip.l) return;
	if (x0 >= painter->clip.r && x1 >= painter->clip.r && x2 >= painter->clip.r) return;
	if (y2 < painter->clip.t || y0 >= painter->clip.b) return;
	bool needsXClip = x0 < painter->clip.l + 1 || x0 >= painter->clip.r - 1
		|| x1 < painter->clip.l + 1 || x1 >= painter->clip.r - 1
		|| x2 < painter->clip.l + 1 || x2 >= painter->clip.r - 1;
	bool needsYClip = y0 < painter->clip.t + 1 || y2 >= painter->clip.b - 1;
#define _UI_DRAW_TRIANGLE_APPLY_CLIP(xo, yo) \
	if (needsYClip && (yi + yo < painter->clip.t || yi + yo >= painter->clip.b)) continue; \
	if (needsXClip && xf + xo < painter->clip.l) xf = painter->clip.l - xo; \
	if (needsXClip && xt + xo > painter->clip.r) xt = painter->clip.r - xo;

	// Step 3: Split into 2 triangles with bases aligned with the x-axis.
	float xm0 = (x2 - x0) * (y1 - y0) / (y2 - y0), xm1 = x1 - x0;
	if (xm1 < xm0) { float xmt = xm0; xm0 = xm1, xm1 = xmt; }
	float xe0 = xm0 + x0 - x2, xe1 = xm1 + x0 - x2;
	int ym = y1 - y0, ye = y2 - y1;
	float ymr = 1.0f / ym, yer = 1.0f / ye;

	// Step 4: Draw the top part.
	for (float y = 0; y < ym; y++) {
		int xf = xm0 * y * ymr, xt = xm1 * y * ymr, yi = (int) y;
		_UI_DRAW_TRIANGLE_APPLY_CLIP(x0, y0);
		uint32_t *b = &painter->bits[(yi + y0) * painter->width + x0];
		for (int x = xf; x < xt; x++) b[x] = color;
	}

	// Step 5: Draw the bottom part.
	for (float y = 0; y < ye; y++) {
		int xf = xe0 * (ye - y) * yer, xt = xe1 * (ye - y) * yer, yi = (int) y;
		_UI_DRAW_TRIANGLE_APPLY_CLIP(x2, y1);
		uint32_t *b = &painter->bits[(yi + y1) * painter->width + x2];
		for (int x = xf; x < xt; x++) b[x] = color;
	}
}

void UIDrawTriangleOutline(UIPainter *painter, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
	UIDrawLine(painter, x0, y0, x1, y1, color);
	UIDrawLine(painter, x1, y1, x2, y2, color);
	UIDrawLine(painter, x2, y2, x0, y0, color);
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

int UIMeasureStringWidth(const char *string, ptrdiff_t bytes) {
#ifdef UI_UNICODE
	return Utf8StringLength(string, bytes) * ui.activeFont->glyphWidth;
#else
	if (bytes == -1) {
		bytes = _UIStringLength(string);
	}

	return bytes * ui.activeFont->glyphWidth;
#endif
}

int UIMeasureStringHeight() {
	return ui.activeFont->glyphHeight;
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

	while (j < bytes) {
		ptrdiff_t bytesConsumed = 1;
#ifdef UI_UNICODE
		int c = Utf8GetCodePoint(string, bytes - j, &bytesConsumed);
		UI_ASSERT(bytesConsumed > 0);
		string += bytesConsumed;
#else
		char c = *string++;
#endif
		uint32_t colorText = color;

		if (i >= selectFrom && i < selectTo) {
			int w = ui.activeFont->glyphWidth;
			if (c == '\t') {
				int ii = i;
				while (++ii & 3) w += ui.activeFont->glyphWidth;
			}
			UIDrawBlock(painter, UI_RECT_4(x, x + w, y, y + height), selection->colorBackground);
			colorText = selection->colorText;
		}

		if (c != '\t') {
			UIDrawGlyph(painter, x, y, c, colorText);
		}

		if (selection && selection->carets[0] == i) {
			UIDrawInvert(painter, UI_RECT_4(x, x + 1, y, y + height));
		}

		x += ui.activeFont->glyphWidth, i++;

		if (c == '\t') {
			while (i & 3) x += ui.activeFont->glyphWidth, i++;
		}

		j += bytesConsumed;
	}

	if (selection && selection->carets[0] == i) {
		UIDrawInvert(painter, UI_RECT_4(x, x + 1, y, y + height));
	}

	painter->clip = oldClip;
}

void UIDrawBorder(UIPainter *painter, UIRectangle r, uint32_t borderColor, UIRectangle borderSize) {
	UIDrawBlock(painter, UI_RECT_4(r.l, r.r, r.t, r.t + borderSize.t), borderColor);
	UIDrawBlock(painter, UI_RECT_4(r.l, r.l + borderSize.l, r.t + borderSize.t, r.b - borderSize.b), borderColor);
	UIDrawBlock(painter, UI_RECT_4(r.r - borderSize.r, r.r, r.t + borderSize.t, r.b - borderSize.b), borderColor);
	UIDrawBlock(painter, UI_RECT_4(r.l, r.r, r.b - borderSize.b, r.b), borderColor);
}

void UIDrawRectangle(UIPainter *painter, UIRectangle r, uint32_t mainColor, uint32_t borderColor, UIRectangle borderSize) {
	UIDrawBorder(painter, r, borderColor, borderSize);
	UIDrawBlock(painter, UI_RECT_4(r.l + borderSize.l, r.r - borderSize.r, r.t + borderSize.t, r.b - borderSize.b), mainColor);
}

void UIDrawControlDefault(UIPainter *painter, UIRectangle bounds, uint32_t mode, const char *label, ptrdiff_t labelBytes, double position, float scale) {
	bool checked       = mode & UI_DRAW_CONTROL_STATE_CHECKED;
	bool disabled      = mode & UI_DRAW_CONTROL_STATE_DISABLED;
	bool focused       = mode & UI_DRAW_CONTROL_STATE_FOCUSED;
	bool hovered       = mode & UI_DRAW_CONTROL_STATE_HOVERED;
	bool indeterminate = mode & UI_DRAW_CONTROL_STATE_INDETERMINATE;
	bool pressed       = mode & UI_DRAW_CONTROL_STATE_PRESSED;
	bool selected      = mode & UI_DRAW_CONTROL_STATE_SELECTED;
	uint32_t which     = mode & UI_DRAW_CONTROL_TYPE_MASK;

	uint32_t buttonColor = disabled ? ui.theme.buttonDisabled
		: (pressed && hovered) ? ui.theme.buttonPressed
		: (pressed || hovered) ? ui.theme.buttonHovered
		: focused ? ui.theme.selected : ui.theme.buttonNormal;
	uint32_t buttonTextColor = disabled ? ui.theme.textDisabled
		: buttonColor == ui.theme.selected ? ui.theme.textSelected : ui.theme.text;

	if (which == UI_DRAW_CONTROL_CHECKBOX) {
		uint32_t color = buttonColor, textColor = buttonTextColor;
		int midY = (bounds.t + bounds.b) / 2;
		UIRectangle boxBounds = UI_RECT_4(bounds.l, bounds.l + UI_SIZE_CHECKBOX_BOX,
				midY - UI_SIZE_CHECKBOX_BOX / 2, midY + UI_SIZE_CHECKBOX_BOX / 2);
		UIDrawRectangle(painter, boxBounds, color, ui.theme.border, UI_RECT_1(1));
		UIDrawString(painter, UIRectangleAdd(boxBounds, UI_RECT_4(1, 0, 0, 0)),
				checked ? "*" : indeterminate ? "-" : " ", -1,
				textColor, UI_ALIGN_CENTER, NULL);
		UIDrawString(painter, UIRectangleAdd(bounds, UI_RECT_4(UI_SIZE_CHECKBOX_BOX + UI_SIZE_CHECKBOX_GAP, 0, 0, 0)),
				label, labelBytes, disabled ? ui.theme.textDisabled : ui.theme.text, UI_ALIGN_LEFT, NULL);
	} else if (which == UI_DRAW_CONTROL_MENU_ITEM || which == UI_DRAW_CONTROL_DROP_DOWN || which == UI_DRAW_CONTROL_PUSH_BUTTON) {
		uint32_t color = buttonColor, textColor = buttonTextColor;
		int borderSize = which == UI_DRAW_CONTROL_MENU_ITEM ? 0 : scale;
		UIDrawRectangle(painter, bounds, color, ui.theme.border, UI_RECT_1(borderSize));

		if (checked && !focused) {
			UIDrawBlock(painter, UIRectangleAdd(bounds, UI_RECT_1I((int) (UI_SIZE_BUTTON_CHECKED_AREA * scale))), ui.theme.buttonPressed);
		}

		UIRectangle innerBounds = UIRectangleAdd(bounds, UI_RECT_2I((int) (UI_SIZE_MENU_ITEM_MARGIN * scale), 0));

		if (which == UI_DRAW_CONTROL_MENU_ITEM) {
			if (labelBytes == -1) {
				labelBytes = _UIStringLength(label);
			}

			int tab = 0;
			for (; tab < labelBytes && label[tab] != '\t'; tab++);

			UIDrawString(painter, innerBounds, label, tab, textColor, UI_ALIGN_LEFT, NULL);

			if (labelBytes > tab) {
				UIDrawString(painter, innerBounds, label + tab + 1, labelBytes - tab - 1, textColor, UI_ALIGN_RIGHT, NULL);
			}
		} else if (which == UI_DRAW_CONTROL_DROP_DOWN) {
			UIDrawString(painter, innerBounds, label, labelBytes, textColor, UI_ALIGN_LEFT, NULL);
			UIDrawString(painter, innerBounds, "\x19", 1, textColor, UI_ALIGN_RIGHT, NULL);
		} else {
			UIDrawString(painter, bounds, label, labelBytes, textColor, UI_ALIGN_CENTER, NULL);
		}
	} else if (which == UI_DRAW_CONTROL_LABEL) {
		UIDrawString(painter, bounds, label, labelBytes, ui.theme.text, UI_ALIGN_LEFT, NULL);
	} else if (which == UI_DRAW_CONTROL_SPLITTER) {
		UIRectangle borders = (mode & UI_DRAW_CONTROL_STATE_VERTICAL) ? UI_RECT_2(0, 1) : UI_RECT_2(1, 0);
		UIDrawRectangle(painter, bounds, ui.theme.buttonNormal, ui.theme.border, borders);
	} else if (which == UI_DRAW_CONTROL_SCROLL_TRACK) {
		if (disabled) UIDrawBlock(painter, bounds, ui.theme.panel1);
	} else if (which == UI_DRAW_CONTROL_SCROLL_DOWN || which == UI_DRAW_CONTROL_SCROLL_UP) {
		bool isDown = which == UI_DRAW_CONTROL_SCROLL_DOWN;
		uint32_t color = pressed ? ui.theme.buttonPressed : hovered ? ui.theme.buttonHovered : ui.theme.panel2;
		UIDrawRectangle(painter, bounds, color, ui.theme.border, UI_RECT_1(0));

		if (mode & UI_DRAW_CONTROL_STATE_VERTICAL) {
			UIDrawGlyph(painter, (bounds.l + bounds.r - ui.activeFont->glyphWidth) / 2 + 1,
					isDown ? (bounds.b - ui.activeFont->glyphHeight - 2 * scale)
					: (bounds.t + 2 * scale),
					isDown ? 25 : 24, ui.theme.text);
		} else {
			UIDrawGlyph(painter, isDown ? (bounds.r - ui.activeFont->glyphWidth - 2 * scale)
					: (bounds.l + 2 * scale),
					(bounds.t + bounds.b - ui.activeFont->glyphHeight) / 2,
					isDown ? 26 : 27, ui.theme.text);
		}
	} else if (which == UI_DRAW_CONTROL_SCROLL_THUMB) {
		uint32_t color = pressed ? ui.theme.buttonPressed : hovered ? ui.theme.buttonHovered : ui.theme.buttonNormal;
		UIDrawRectangle(painter, bounds, color, ui.theme.border, UI_RECT_1(2));
	} else if (which == UI_DRAW_CONTROL_GAUGE) {
		UIDrawRectangle(painter, bounds, ui.theme.buttonNormal, ui.theme.border, UI_RECT_1(1));
		UIRectangle filled = UIRectangleAdd(bounds, UI_RECT_1I(1));
		filled.r = filled.l + UI_RECT_WIDTH(filled) * position;
		UIDrawBlock(painter, filled, ui.theme.selected);
	} else if (which == UI_DRAW_CONTROL_SLIDER) {
		int centerY = (bounds.t + bounds.b) / 2;
		int trackSize = UI_SIZE_SLIDER_TRACK * scale;
		int thumbSize = UI_SIZE_SLIDER_THUMB * scale;
		int thumbPosition = (UI_RECT_WIDTH(bounds) - thumbSize) * position;
		UIRectangle track = UI_RECT_4(bounds.l, bounds.r, centerY - (trackSize + 1) / 2, centerY + trackSize / 2);
		UIDrawRectangle(painter, track, disabled ? ui.theme.buttonDisabled : ui.theme.buttonNormal, ui.theme.border, UI_RECT_1(1));
		uint32_t color = disabled ? ui.theme.buttonDisabled : pressed ? ui.theme.buttonPressed : hovered ? ui.theme.buttonHovered : ui.theme.buttonNormal;
		UIRectangle thumb = UI_RECT_4(bounds.l + thumbPosition, bounds.l + thumbPosition + thumbSize, centerY - (thumbSize + 1) / 2, centerY + thumbSize / 2);
		UIDrawRectangle(painter, thumb, color, ui.theme.border, UI_RECT_1(1));
	} else if (which == UI_DRAW_CONTROL_TEXTBOX) {
		UIDrawRectangle(painter, bounds,
				disabled ? ui.theme.buttonDisabled : focused ? ui.theme.textboxFocused : ui.theme.textboxNormal,
				ui.theme.border, UI_RECT_1(1));
	} else if (which == UI_DRAW_CONTROL_MODAL_POPUP) {
		UIRectangle bounds2 = UIRectangleAdd(bounds, UI_RECT_1I(-1));
		UIDrawBorder(painter, bounds2, ui.theme.border, UI_RECT_1(1));
		UIDrawBorder(painter, UIRectangleAdd(bounds2, UI_RECT_1(1)), ui.theme.border, UI_RECT_1(1));
	} else if (which == UI_DRAW_CONTROL_MENU) {
		UIDrawBlock(painter, bounds, ui.theme.border);
	} else if (which == UI_DRAW_CONTROL_TABLE_ROW) {
		if (selected) UIDrawBlock(painter, bounds, ui.theme.selected);
		else if (hovered) UIDrawBlock(painter, bounds, ui.theme.buttonHovered);
	} else if (which == UI_DRAW_CONTROL_TABLE_CELL) {
		uint32_t textColor = selected ? ui.theme.textSelected : ui.theme.text;
		UIDrawString(painter, bounds, label, labelBytes, textColor, UI_ALIGN_LEFT, NULL);
	} else if (which == UI_DRAW_CONTROL_TABLE_BACKGROUND) {
		UIDrawBlock(painter, bounds, ui.theme.panel2);
		UIDrawRectangle(painter, UI_RECT_4(bounds.l, bounds.r, bounds.t, bounds.t + (int) (UI_SIZE_TABLE_HEADER * scale)),
				ui.theme.panel1, ui.theme.border, UI_RECT_4(0, 0, 0, 1));
	} else if (which == UI_DRAW_CONTROL_TABLE_HEADER) {
		UIDrawString(painter, bounds, label, labelBytes, ui.theme.text, UI_ALIGN_LEFT, NULL);
		if (selected) UIDrawInvert(painter, bounds);
	} else if (which == UI_DRAW_CONTROL_MDI_CHILD) {
		UI_MDI_CHILD_CALCULATE_LAYOUT(bounds, scale);
		UIRectangle borders = UI_RECT_4(borderSize, borderSize, titleSize, borderSize);
		UIDrawBorder(painter, bounds, ui.theme.buttonNormal, borders);
		UIDrawBorder(painter, bounds, ui.theme.border, UI_RECT_1((int) scale));
		UIDrawBorder(painter, UIRectangleAdd(content, UI_RECT_1I(-1)), ui.theme.border, UI_RECT_1((int) scale));
		UIDrawString(painter, title, label, labelBytes, ui.theme.text, UI_ALIGN_LEFT, NULL);
	} else if (which == UI_DRAW_CONTROL_TAB) {
		uint32_t color = selected ? ui.theme.buttonPressed : ui.theme.buttonNormal;
		UIRectangle t = bounds;
		if (selected) t.b++, t.t--;
		else t.t++;
		UIDrawRectangle(painter, t, color, ui.theme.border, UI_RECT_1(1));
		UIDrawString(painter, bounds, label, labelBytes, ui.theme.text, UI_ALIGN_CENTER, NULL);
	} else if (which == UI_DRAW_CONTROL_TAB_BAND) {
		UIDrawRectangle(painter, bounds, ui.theme.panel1, ui.theme.border, UI_RECT_4(0, 0, 0, 1));
	}
}

/////////////////////////////////////////
// Element hierarchy.
/////////////////////////////////////////

void _UIElementDestroyDescendents(UIElement *element, bool topLevel) {
	for (uint32_t i = 0; i < element->childCount; i++) {
		UIElement *child = element->children[i];

		if (!topLevel || (~child->flags & UI_ELEMENT_NON_CLIENT)) {
			UIElementDestroy(child);
		}
	}

#ifdef UI_DEBUG
	_UIInspectorRefresh();
#endif
}

void UIElementDestroyDescendents(UIElement *element) {
	_UIElementDestroyDescendents(element, true);
}

void UIElementDestroy(UIElement *element) {
	if (element->flags & UI_ELEMENT_DESTROY) {
		return;
	}

	UIElementMessage(element, UI_MSG_DESTROY, 0, 0);
	element->flags |= UI_ELEMENT_DESTROY | UI_ELEMENT_HIDE;

	UIElement *ancestor = element->parent;

	while (ancestor) {
		if (ancestor->flags & UI_ELEMENT_DESTROY_DESCENDENT) break;
		ancestor->flags |= UI_ELEMENT_DESTROY_DESCENDENT;
		ancestor = ancestor->parent;
	}

	_UIElementDestroyDescendents(element, false);

	if (element->parent) {
		UIElementRelayout(element->parent);
		UIElementRepaint(element->parent, &element->bounds);
		UIElementMeasurementsChanged(element->parent, 3);
	}
}

int UIElementMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message != UI_MSG_DEALLOCATE && (element->flags & UI_ELEMENT_DESTROY)) {
		return 0;
	}

	if (message >= UI_MSG_INPUT_EVENTS_START && message <= UI_MSG_INPUT_EVENTS_END && (element->flags & UI_ELEMENT_DISABLED)) {
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

UIElement *UIElementChangeParent(UIElement *element, UIElement *newParent, UIElement *insertBefore) {
	bool found = false;
	UIElement *oldBefore = NULL;

	for (uint32_t i = 0; i < element->parent->childCount; i++) {
		if (element->parent->children[i] == element) {
			UI_MEMMOVE(&element->parent->children[i], &element->parent->children[i + 1], sizeof(UIElement *) * (element->parent->childCount - i - 1));
			element->parent->childCount--;
			oldBefore = i == element->parent->childCount ? NULL : element->parent->children[i];
			found = true;
			break;
		}
	}

	UI_ASSERT(found && (~element->flags & UI_ELEMENT_DESTROY));

	for (uint32_t i = 0; i <= newParent->childCount; i++) {
		if (i == newParent->childCount || newParent->children[i] == insertBefore) {
			newParent->children = (UIElement **) UI_REALLOC(newParent->children, sizeof(UIElement *) * (newParent->childCount + 1));
			UI_MEMMOVE(&newParent->children[i + 1], &newParent->children[i], sizeof(UIElement *) * (newParent->childCount - i));
			newParent->childCount++;
			newParent->children[i] = element;
			found = true;
			break;
		}
	}

	UIElement *oldParent = element->parent;
	element->parent = newParent;
	element->window = newParent->window;

	UIElementMeasurementsChanged(oldParent, 3);
	UIElementMeasurementsChanged(newParent, 3);

	return oldBefore;
}

UIElement *UIElementCreate(size_t bytes, UIElement *parent, uint32_t flags, int (*message)(UIElement *, UIMessage, int, void *), const char *cClassName) {
	UI_ASSERT(bytes >= sizeof(UIElement));
	UIElement *element = (UIElement *) UI_CALLOC(bytes);
	element->flags = flags;
	element->messageClass = message;

	if (!parent && (~flags & UI_ELEMENT_WINDOW)) {
		UI_ASSERT(ui.parentStackCount);
		parent = ui.parentStack[ui.parentStackCount - 1];
	}

	if (parent) {
		UI_ASSERT(~parent->flags & UI_ELEMENT_DESTROY);
		element->window = parent->window;
		element->parent = parent;
		parent->children = (UIElement **) UI_REALLOC(parent->children, sizeof(UIElement *) * (parent->childCount + 1));
		parent->children[parent->childCount] = element;
		parent->childCount++;
		UIElementRelayout(parent);
		UIElementMeasurementsChanged(parent, 3);
	}

	element->cClassName = cClassName;
	static uint32_t id = 0;
	element->id = ++id;

#ifdef UI_DEBUG
	_UIInspectorRefresh();
#endif

	if (flags & UI_ELEMENT_PARENT_PUSH) {
		UIParentPush(element);
	}

	return element;
}

UIElement *UIParentPush(UIElement *element) {
	UI_ASSERT(ui.parentStackCount != sizeof(ui.parentStack) / sizeof(ui.parentStack[0]));
	ui.parentStack[ui.parentStackCount++] = element;
	return element;
}

UIElement *UIParentPop() {
	UI_ASSERT(ui.parentStackCount);
	ui.parentStackCount--;
	return ui.parentStack[ui.parentStackCount];
}

/////////////////////////////////////////
// Panels.
/////////////////////////////////////////

int _UIPanelCalculatePerFill(UIPanel *panel, int *_count, int hSpace, int vSpace, float scale) {
	bool horizontal = panel->e.flags & UI_PANEL_HORIZONTAL;
	int available = horizontal ? hSpace : vSpace;
	int count = 0, fill = 0, perFill = 0;

	for (uint32_t i = 0; i < panel->e.childCount; i++) {
		UIElement *child = panel->e.children[i];

		if (child->flags & (UI_ELEMENT_HIDE | UI_ELEMENT_NON_CLIENT)) {
			continue;
		}

		count++;

		if (horizontal) {
			if (child->flags & UI_ELEMENT_H_FILL) {
				fill++;
			} else if (available > 0) {
				available -= UIElementMessage(child, UI_MSG_GET_WIDTH, vSpace, 0);
			}
		} else {
			if (child->flags & UI_ELEMENT_V_FILL) {
				fill++;
			} else if (available > 0) {
				available -= UIElementMessage(child, UI_MSG_GET_HEIGHT, hSpace, 0);
			}
		}
	}

	if (count) {
		available -= (count - 1) * (int) (panel->gap * scale);
	}

	if (available > 0 && fill) {
		perFill = available / fill;
	}

	if (_count) {
		*_count = count;
	}

	return perFill;
}

int _UIPanelMeasure(UIPanel *panel, int di) {
	bool horizontal = panel->e.flags & UI_PANEL_HORIZONTAL;
	int perFill = _UIPanelCalculatePerFill(panel, NULL, horizontal ? di : 0, horizontal ? 0 : di, panel->e.window->scale);
	int size = 0;

	for (uint32_t i = 0; i < panel->e.childCount; i++) {
		UIElement *child = panel->e.children[i];
		if (child->flags & (UI_ELEMENT_HIDE | UI_ELEMENT_NON_CLIENT)) continue;
		int childSize = UIElementMessage(child, horizontal ? UI_MSG_GET_HEIGHT : UI_MSG_GET_WIDTH,
				(child->flags & (horizontal ? UI_ELEMENT_H_FILL : UI_ELEMENT_V_FILL)) ? perFill : 0, 0);
		if (childSize > size) size = childSize;
	}

	int border = horizontal ? (panel->border.t + panel->border.b) : (panel->border.l + panel->border.r);
	return size + border * panel->e.window->scale;
}

int _UIPanelLayout(UIPanel *panel, UIRectangle bounds, bool measure) {
	bool horizontal = panel->e.flags & UI_PANEL_HORIZONTAL;
	float scale = panel->e.window->scale;
	int position = (horizontal ? panel->border.l : panel->border.t) * scale;
	if (panel->scrollBar && !measure) position -= panel->scrollBar->position;
	int hSpace = UI_RECT_WIDTH(bounds) - UI_RECT_TOTAL_H(panel->border) * scale;
	int vSpace = UI_RECT_HEIGHT(bounds) - UI_RECT_TOTAL_V(panel->border) * scale;
	int count = 0;
	int perFill = _UIPanelCalculatePerFill(panel, &count, hSpace, vSpace, scale);
	int scaledBorder2 = (horizontal ? panel->border.t : panel->border.l) * panel->e.window->scale;
	bool expand = panel->e.flags & UI_PANEL_EXPAND;

	for (uint32_t i = 0; i < panel->e.childCount; i++) {
		UIElement *child = panel->e.children[i];

		if (child->flags & (UI_ELEMENT_HIDE | UI_ELEMENT_NON_CLIENT)) {
			continue;
		}

		if (horizontal) {
			int height = ((child->flags & UI_ELEMENT_V_FILL) || expand) ? vSpace
				: UIElementMessage(child, UI_MSG_GET_HEIGHT, (child->flags & UI_ELEMENT_H_FILL) ? perFill : 0, 0);
			int width = (child->flags & UI_ELEMENT_H_FILL) ? perFill : UIElementMessage(child, UI_MSG_GET_WIDTH, height, 0);
			UIRectangle relative = UI_RECT_4(position, position + width,
					scaledBorder2 + (vSpace - height) / 2,
					scaledBorder2 + (vSpace + height) / 2);
			if (!measure) UIElementMove(child, UIRectangleTranslate(relative, bounds), false);
			position += width + panel->gap * scale;
		} else {
			int width = ((child->flags & UI_ELEMENT_H_FILL) || expand) ? hSpace
				: UIElementMessage(child, UI_MSG_GET_WIDTH, (child->flags & UI_ELEMENT_V_FILL) ? perFill : 0, 0);
			int height = (child->flags & UI_ELEMENT_V_FILL) ? perFill : UIElementMessage(child, UI_MSG_GET_HEIGHT, width, 0);
			UIRectangle relative = UI_RECT_4(scaledBorder2 + (hSpace - width) / 2,
					scaledBorder2 + (hSpace + width) / 2, position, position + height);
			if (!measure) UIElementMove(child, UIRectangleTranslate(relative, bounds), false);
			position += height + panel->gap * scale;
		}
	}

	return position - (count ? panel->gap : 0) * scale + (horizontal ? panel->border.r : panel->border.b) * scale;
}

int _UIPanelMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIPanel *panel = (UIPanel *) element;
	bool horizontal = element->flags & UI_PANEL_HORIZONTAL;

	if (message == UI_MSG_LAYOUT) {
		int scrollBarWidth = panel->scrollBar ? (UI_SIZE_SCROLL_BAR * element->window->scale) : 0;
		UIRectangle bounds = element->bounds;
		bounds.r -= scrollBarWidth;

		if (panel->scrollBar) {
			UIRectangle scrollBarBounds = element->bounds;
			scrollBarBounds.l = scrollBarBounds.r - scrollBarWidth;
			panel->scrollBar->maximum = _UIPanelLayout(panel, bounds, true);
			panel->scrollBar->page = UI_RECT_HEIGHT(element->bounds);
			UIElementMove(&panel->scrollBar->e, scrollBarBounds, true);
		}

		_UIPanelLayout(panel, bounds, false);
	} else if (message == UI_MSG_GET_WIDTH) {
		if (horizontal) {
			return _UIPanelLayout(panel, UI_RECT_4(0, 0, 0, di), true);
		} else {
			return _UIPanelMeasure(panel, di);
		}
	} else if (message == UI_MSG_GET_HEIGHT) {
		if (horizontal) {
			return _UIPanelMeasure(panel, di);
		} else {
			int width = di && panel->scrollBar ? (di - UI_SIZE_SCROLL_BAR * element->window->scale) : di;
			return _UIPanelLayout(panel, UI_RECT_4(0, width, 0, 0), true);
		}
	} else if (message == UI_MSG_PAINT) {
		if (element->flags & UI_PANEL_COLOR_1) {
			UIDrawBlock((UIPainter *) dp, element->bounds, ui.theme.panel1);
		} else if (element->flags & UI_PANEL_COLOR_2) {
			UIDrawBlock((UIPainter *) dp, element->bounds, ui.theme.panel2);
		}
	} else if (message == UI_MSG_MOUSE_WHEEL && panel->scrollBar) {
		return UIElementMessage(&panel->scrollBar->e, message, di, dp);
	} else if (message == UI_MSG_SCROLLED) {
		UIElementRefresh(element);
	} else if (message == UI_MSG_GET_CHILD_STABILITY) {
		UIElement *child = (UIElement *) dp;
		return ((element->flags & UI_PANEL_EXPAND) ? (horizontal ? 2 : 1) : 0)
			| ((child->flags & UI_ELEMENT_H_FILL) ? 1 : 0) | ((child->flags & UI_ELEMENT_V_FILL) ? 2 : 0);
	}

	return 0;
}

UIPanel *UIPanelCreate(UIElement *parent, uint32_t flags) {
	UIPanel *panel = (UIPanel *) UIElementCreate(sizeof(UIPanel), parent, flags, _UIPanelMessage, "Panel");

	if (flags & UI_PANEL_LARGE_SPACING) {
		panel->border = UI_RECT_1(UI_SIZE_PANE_LARGE_BORDER);
		panel->gap = UI_SIZE_PANE_LARGE_GAP;
	} else if (flags & UI_PANEL_MEDIUM_SPACING) {
		panel->border = UI_RECT_1(UI_SIZE_PANE_MEDIUM_BORDER);
		panel->gap = UI_SIZE_PANE_MEDIUM_GAP;
	} else if (flags & UI_PANEL_SMALL_SPACING) {
		panel->border = UI_RECT_1(UI_SIZE_PANE_SMALL_BORDER);
		panel->gap = UI_SIZE_PANE_SMALL_GAP;
	}

	if (flags & UI_PANEL_SCROLL) {
		panel->scrollBar = UIScrollBarCreate(&panel->e, UI_ELEMENT_NON_CLIENT);
	}

	return panel;
}

void _UIWrapPanelLayoutRow(UIWrapPanel *panel, uint32_t rowStart, uint32_t rowEnd, int rowY, int rowHeight) {
	int rowPosition = 0;

	for (uint32_t i = rowStart; i < rowEnd; i++) {
		UIElement *child = panel->e.children[i];
		if (child->flags & UI_ELEMENT_HIDE) continue;
		int height = UIElementMessage(child, UI_MSG_GET_HEIGHT, 0, 0);
		int width = UIElementMessage(child, UI_MSG_GET_WIDTH, 0, 0);
		UIRectangle relative = UI_RECT_4(rowPosition, rowPosition + width, rowY + rowHeight / 2 - height / 2, rowY + rowHeight / 2 + height / 2);
		UIElementMove(child, UIRectangleTranslate(relative, panel->e.bounds), false);
		rowPosition += width;
	}
}

int _UIWrapPanelMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIWrapPanel *panel = (UIWrapPanel *) element;

	if (message == UI_MSG_LAYOUT || message == UI_MSG_GET_HEIGHT) {
		int totalHeight = 0;
		int rowPosition = 0;
		int rowHeight = 0;
		int rowLimit = message == UI_MSG_LAYOUT ? UI_RECT_WIDTH(element->bounds) : di;

		uint32_t rowStart = 0;

		for (uint32_t i = 0; i < panel->e.childCount; i++) {
			UIElement *child = panel->e.children[i];
			if (child->flags & UI_ELEMENT_HIDE) continue;

			int height = UIElementMessage(child, UI_MSG_GET_HEIGHT, 0, 0);
			int width = UIElementMessage(child, UI_MSG_GET_WIDTH, 0, 0);

			if (rowLimit && rowPosition + width > rowLimit) {
				_UIWrapPanelLayoutRow(panel, rowStart, i, totalHeight, rowHeight);
				totalHeight += rowHeight;
				rowPosition = rowHeight = 0;
				rowStart = i;
			}

			if (height > rowHeight) {
				rowHeight = height;
			}

			rowPosition += width;
		}

		if (message == UI_MSG_GET_HEIGHT) {
			return totalHeight + rowHeight;
		} else {
			_UIWrapPanelLayoutRow(panel, rowStart, panel->e.childCount, totalHeight, rowHeight);
		}
	}

	return 0;
}

UIWrapPanel *UIWrapPanelCreate(UIElement *parent, uint32_t flags) {
	return (UIWrapPanel *) UIElementCreate(sizeof(UIWrapPanel), parent, flags, _UIWrapPanelMessage, "Wrap Panel");
}

int _UISwitcherMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UISwitcher *switcher = (UISwitcher *) element;

	if (!switcher->active) {
	} else if (message == UI_MSG_GET_WIDTH || message == UI_MSG_GET_HEIGHT) {
		return UIElementMessage(switcher->active, message, di, dp);
	} else if (message == UI_MSG_LAYOUT) {
		UIElementMove(switcher->active, element->bounds, false);
	}

	return 0;
}

void UISwitcherSwitchTo(UISwitcher *switcher, UIElement *child) {
	for (uint32_t i = 0; i < switcher->e.childCount; i++) {
		switcher->e.children[i]->flags |= UI_ELEMENT_HIDE;
	}

	UI_ASSERT(child->parent == &switcher->e);
	child->flags &= ~UI_ELEMENT_HIDE;
	switcher->active = child;
	UIElementMeasurementsChanged(&switcher->e, 3);
	UIElementRefresh(&switcher->e);
}

UISwitcher *UISwitcherCreate(UIElement *parent, uint32_t flags) {
	return (UISwitcher *) UIElementCreate(sizeof(UISwitcher), parent, flags, _UISwitcherMessage, "Switcher");
}

/////////////////////////////////////////
// Checkboxes and buttons.
/////////////////////////////////////////

int _UIButtonMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIButton *button = (UIButton *) element;
	bool isMenuItem = element->flags & UI_BUTTON_MENU_ITEM;
	bool isDropDown = element->flags & UI_BUTTON_DROP_DOWN;

	if (message == UI_MSG_GET_HEIGHT) {
		if (isMenuItem) {
			return UI_SIZE_MENU_ITEM_HEIGHT * element->window->scale;
		} else {
			return UI_SIZE_BUTTON_HEIGHT * element->window->scale;
		}
	} else if (message == UI_MSG_GET_WIDTH) {
		int labelSize = UIMeasureStringWidth(button->label, button->labelBytes);
		int paddedSize = labelSize + UI_SIZE_BUTTON_PADDING * element->window->scale;
		if (isDropDown) paddedSize += ui.activeFont->glyphWidth * 2;
		int minimumSize = ((element->flags & UI_BUTTON_SMALL) ? 0
				: isMenuItem ? UI_SIZE_MENU_ITEM_MINIMUM_WIDTH
				: UI_SIZE_BUTTON_MINIMUM_WIDTH)
			* element->window->scale;
		return paddedSize > minimumSize ? paddedSize : minimumSize;
	} else if (message == UI_MSG_PAINT) {
		UIDrawControl((UIPainter *) dp, element->bounds,
				(isMenuItem ? UI_DRAW_CONTROL_MENU_ITEM : isDropDown ? UI_DRAW_CONTROL_DROP_DOWN : UI_DRAW_CONTROL_PUSH_BUTTON)
				| ((element->flags & UI_BUTTON_CHECKED) ? UI_DRAW_CONTROL_STATE_CHECKED : 0) | UI_DRAW_CONTROL_STATE_FROM_ELEMENT(element),
				button->label, button->labelBytes, 0, element->window->scale);
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_DEALLOCATE) {
		UI_FREE(button->label);
	} else if (message == UI_MSG_LEFT_DOWN) {
		if (element->flags & UI_BUTTON_CAN_FOCUS) {
			UIElementFocus(element);
		}
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		if ((m->textBytes == 1 && m->text[0] == ' ') || m->code == UI_KEYCODE_ENTER) {
			UIElementMessage(element, UI_MSG_CLICKED, 0, 0);
			UIElementRepaint(element, NULL);
			return 1;
		}
	} else if (message == UI_MSG_CLICKED) {
		if (button->invoke) {
			button->invoke(element->cp);
		}
	}

	return 0;
}

void UIButtonSetLabel(UIButton *button, const char *string, ptrdiff_t stringBytes) {
	UI_FREE(button->label);
	button->label = UIStringCopy(string, (button->labelBytes = stringBytes));
	UIElementMeasurementsChanged(&button->e, 1);
	UIElementRepaint(&button->e, NULL);
}

UIButton *UIButtonCreate(UIElement *parent, uint32_t flags, const char *label, ptrdiff_t labelBytes) {
	UIButton *button = (UIButton *) UIElementCreate(sizeof(UIButton), parent, flags | UI_ELEMENT_TAB_STOP, _UIButtonMessage, "Button");
	button->label = UIStringCopy(label, (button->labelBytes = labelBytes));
	return button;
}

int _UICheckboxMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UICheckbox *box = (UICheckbox *) element;

	if (message == UI_MSG_GET_HEIGHT) {
		return UI_SIZE_BUTTON_HEIGHT * element->window->scale;
	} else if (message == UI_MSG_GET_WIDTH) {
		int labelSize = UIMeasureStringWidth(box->label, box->labelBytes);
		return (labelSize + UI_SIZE_CHECKBOX_BOX + UI_SIZE_CHECKBOX_GAP) * element->window->scale;
	} else if (message == UI_MSG_PAINT) {
		UIDrawControl((UIPainter *) dp, element->bounds,
				UI_DRAW_CONTROL_CHECKBOX | (box->check == UI_CHECK_INDETERMINATE ? UI_DRAW_CONTROL_STATE_INDETERMINATE
					: box->check == UI_CHECK_CHECKED ? UI_DRAW_CONTROL_STATE_CHECKED : 0)
				| UI_DRAW_CONTROL_STATE_FROM_ELEMENT(element),
				box->label, box->labelBytes, 0, element->window->scale);
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_DEALLOCATE) {
		UI_FREE(box->label);
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		if (m->textBytes == 1 && m->text[0] == ' ') {
			UIElementMessage(element, UI_MSG_CLICKED, 0, 0);
			UIElementRepaint(element, NULL);
		}
	} else if (message == UI_MSG_CLICKED) {
		box->check = (box->check + 1) % ((element->flags & UI_CHECKBOX_ALLOW_INDETERMINATE) ? 3 : 2);
		UIElementRepaint(element, NULL);
		if (box->invoke) box->invoke(element->cp);
	}

	return 0;
}

UICheckbox *UICheckboxCreate(UIElement *parent, uint32_t flags, const char *label, ptrdiff_t labelBytes) {
	UICheckbox *box = (UICheckbox *) UIElementCreate(sizeof(UICheckbox), parent, flags | UI_ELEMENT_TAB_STOP, _UICheckboxMessage, "Checkbox");
	box->label = UIStringCopy(label, (box->labelBytes = labelBytes));
	return box;
}

/////////////////////////////////////////
// Labels.
/////////////////////////////////////////

int _UILabelMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UILabel *label = (UILabel *) element;

	if (message == UI_MSG_GET_HEIGHT) {
		return UIMeasureStringHeight();
	} else if (message == UI_MSG_GET_WIDTH) {
		return UIMeasureStringWidth(label->label, label->labelBytes);
	} else if (message == UI_MSG_PAINT) {
		UIDrawControl((UIPainter *) dp, element->bounds, UI_DRAW_CONTROL_LABEL | UI_DRAW_CONTROL_STATE_FROM_ELEMENT(element),
				label->label, label->labelBytes, 0, element->window->scale);
	} else if (message == UI_MSG_DEALLOCATE) {
		UI_FREE(label->label);
	}

	return 0;
}

void UILabelSetContent(UILabel *label, const char *string, ptrdiff_t stringBytes) {
	UI_FREE(label->label);
	label->label = UIStringCopy(string, (label->labelBytes = stringBytes));
	UIElementMeasurementsChanged(&label->e, 1);
	UIElementRepaint(&label->e, NULL);
}

UILabel *UILabelCreate(UIElement *parent, uint32_t flags, const char *string, ptrdiff_t stringBytes) {
	UILabel *label = (UILabel *) UIElementCreate(sizeof(UILabel), parent, flags, _UILabelMessage, "Label");
	label->label = UIStringCopy(string, (label->labelBytes = stringBytes));
	return label;
}

/////////////////////////////////////////
// Split panes.
/////////////////////////////////////////

int _UISplitPaneMessage(UIElement *element, UIMessage message, int di, void *dp);

int _UISplitterMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UISplitPane *splitPane = (UISplitPane *) element->parent;
	bool vertical = splitPane->e.flags & UI_SPLIT_PANE_VERTICAL;

	if (message == UI_MSG_PAINT) {
		UIDrawControl((UIPainter *) dp, element->bounds, UI_DRAW_CONTROL_SPLITTER | (vertical ? UI_DRAW_CONTROL_STATE_VERTICAL : 0)
				| UI_DRAW_CONTROL_STATE_FROM_ELEMENT(element), NULL, 0, 0, element->window->scale);
	} else if (message == UI_MSG_GET_CURSOR) {
		return vertical ? UI_CURSOR_SPLIT_V : UI_CURSOR_SPLIT_H;
	} else if (message == UI_MSG_MOUSE_DRAG) {
		int cursor = vertical ? element->window->cursorY : element->window->cursorX;
		int splitterSize = UI_SIZE_SPLITTER * element->window->scale;
		int space = (vertical ? UI_RECT_HEIGHT(splitPane->e.bounds) : UI_RECT_WIDTH(splitPane->e.bounds)) - splitterSize;
		float oldWeight = splitPane->weight;
		splitPane->weight = (float) (cursor - splitterSize / 2 - (vertical ? splitPane->e.bounds.t : splitPane->e.bounds.l)) / space;
		if (splitPane->weight < 0.05f) splitPane->weight = 0.05f;
		if (splitPane->weight > 0.95f) splitPane->weight = 0.95f;

		if (splitPane->e.children[2]->messageClass == _UISplitPaneMessage
				&& (splitPane->e.children[2]->flags & UI_SPLIT_PANE_VERTICAL) == (splitPane->e.flags & UI_SPLIT_PANE_VERTICAL)) {
			UISplitPane *subSplitPane = (UISplitPane *) splitPane->e.children[2];
			subSplitPane->weight = (splitPane->weight - oldWeight - subSplitPane->weight + oldWeight * subSplitPane->weight) / (-1 + splitPane->weight);
			if (subSplitPane->weight < 0.05f) subSplitPane->weight = 0.05f;
			if (subSplitPane->weight > 0.95f) subSplitPane->weight = 0.95f;
		}

		UIElementRefresh(&splitPane->e);
	}

	return 0;
}

int _UISplitPaneMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UISplitPane *splitPane = (UISplitPane *) element;
	bool vertical = splitPane->e.flags & UI_SPLIT_PANE_VERTICAL;

	if (message == UI_MSG_LAYOUT) {
		UIElement *splitter = element->children[0];
		UIElement *left = element->children[1];
		UIElement *right = element->children[2];

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

UISplitPane *UISplitPaneCreate(UIElement *parent, uint32_t flags, float weight) {
	UISplitPane *splitPane = (UISplitPane *) UIElementCreate(sizeof(UISplitPane), parent, flags, _UISplitPaneMessage, "Split Pane");
	splitPane->weight = weight;
	UIElementCreate(sizeof(UIElement), &splitPane->e, 0, _UISplitterMessage, "Splitter");
	return splitPane;
}

/////////////////////////////////////////
// Tab panes.
/////////////////////////////////////////

int _UITabPaneMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UITabPane *tabPane = (UITabPane *) element;

	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		UIRectangle top = element->bounds;
		top.b = top.t + UI_SIZE_BUTTON_HEIGHT * element->window->scale;
		UIDrawControl(painter, top, UI_DRAW_CONTROL_TAB_BAND, NULL, 0, 0, element->window->scale);

		UIRectangle tab = top;
		tab.l += UI_SIZE_TAB_PANE_SPACE_LEFT * element->window->scale;
		tab.t += UI_SIZE_TAB_PANE_SPACE_TOP * element->window->scale;

		int position = 0;
		uint32_t index = 0;

		while (true) {
			int end = position;
			for (; tabPane->tabs[end] != '\t' && tabPane->tabs[end]; end++);

			int width = UIMeasureStringWidth(tabPane->tabs, end - position);
			tab.r = tab.l + width + UI_SIZE_BUTTON_PADDING;

			UIDrawControl(painter, tab, UI_DRAW_CONTROL_TAB | (tabPane->active == index ? UI_DRAW_CONTROL_STATE_SELECTED : 0),
					tabPane->tabs + position, end - position, 0, element->window->scale);
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
		tab.b = tab.t + UI_SIZE_BUTTON_HEIGHT * element->window->scale;
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
				UIElementRelayout(element);
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
		UIRectangle content = element->bounds;
		content.t += UI_SIZE_BUTTON_HEIGHT * element->window->scale;

		for (uint32_t index = 0; index < element->childCount; index++) {
			UIElement *child = element->children[index];

			if (tabPane->active == index) {
				child->flags &= ~UI_ELEMENT_HIDE;
				UIElementMove(child, content, false);
				UIElementMessage(child, UI_MSG_TAB_SELECTED, 0, 0);
			} else {
				child->flags |= UI_ELEMENT_HIDE;
			}
		}
	} else if (message == UI_MSG_GET_HEIGHT) {
		int baseHeight = UI_SIZE_BUTTON_HEIGHT * element->window->scale;

		for (uint32_t index = 0; index < element->childCount; index++) {
			UIElement *child = element->children[index];

			if (tabPane->active == index) {
				return baseHeight + UIElementMessage(child, UI_MSG_GET_HEIGHT, di, dp);
			}
		}
	} else if (message == UI_MSG_DEALLOCATE) {
		UI_FREE(tabPane->tabs);
	}

	return 0;
}

UITabPane *UITabPaneCreate(UIElement *parent, uint32_t flags, const char *tabs) {
	UITabPane *tabPane = (UITabPane *) UIElementCreate(sizeof(UITabPane), parent, flags, _UITabPaneMessage, "Tab Pane");
	tabPane->tabs = UIStringCopy(tabs, -1);
	return tabPane;
}

/////////////////////////////////////////
// Spacers.
/////////////////////////////////////////

int _UISpacerMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UISpacer *spacer = (UISpacer *) element;

	if (message == UI_MSG_GET_HEIGHT) {
		return spacer->height * element->window->scale;
	} else if (message == UI_MSG_GET_WIDTH) {
		return spacer->width * element->window->scale;
	}

	return 0;
}

UISpacer *UISpacerCreate(UIElement *parent, uint32_t flags, int width, int height) {
	UISpacer *spacer = (UISpacer *) UIElementCreate(sizeof(UISpacer), parent, flags, _UISpacerMessage, "Spacer");
	spacer->width = width;
	spacer->height = height;
	return spacer;
}

/////////////////////////////////////////
// Scroll bars.
/////////////////////////////////////////

int _UIScrollBarMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIScrollBar *scrollBar = (UIScrollBar *) element;

	if (message == UI_MSG_GET_WIDTH || message == UI_MSG_GET_HEIGHT) {
		return UI_SIZE_SCROLL_BAR * element->window->scale;
	} else if (message == UI_MSG_LAYOUT) {
		UIElement *up = element->children[0];
		UIElement *thumb = element->children[1];
		UIElement *down = element->children[2];

		if (scrollBar->page >= scrollBar->maximum || scrollBar->maximum <= 0 || scrollBar->page <= 0) {
			up->flags |= UI_ELEMENT_HIDE;
			thumb->flags |= UI_ELEMENT_HIDE;
			down->flags |= UI_ELEMENT_HIDE;

			scrollBar->position = 0;
		} else {
			up->flags &= ~UI_ELEMENT_HIDE;
			thumb->flags &= ~UI_ELEMENT_HIDE;
			down->flags &= ~UI_ELEMENT_HIDE;

			int size = scrollBar->horizontal ? UI_RECT_WIDTH(element->bounds) : UI_RECT_HEIGHT(element->bounds);
			int thumbSize = size * scrollBar->page / scrollBar->maximum;

			if (thumbSize < UI_SIZE_SCROLL_MINIMUM_THUMB * element->window->scale) {
				thumbSize = UI_SIZE_SCROLL_MINIMUM_THUMB * element->window->scale;
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

			if (scrollBar->horizontal) {
				UIRectangle r = element->bounds;
				r.r = r.l + thumbPosition;
				UIElementMove(up, r, false);
				r.l = r.r, r.r = r.l + thumbSize;
				UIElementMove(thumb, r, false);
				r.l = r.r, r.r = element->bounds.r;
				UIElementMove(down, r, false);
			} else {
				UIRectangle r = element->bounds;
				r.b = r.t + thumbPosition;
				UIElementMove(up, r, false);
				r.t = r.b, r.b = r.t + thumbSize;
				UIElementMove(thumb, r, false);
				r.t = r.b, r.b = element->bounds.b;
				UIElementMove(down, r, false);
			}
		}
	} else if (message == UI_MSG_PAINT) {
		UIDrawControl((UIPainter *) dp, element->bounds, UI_DRAW_CONTROL_SCROLL_TRACK
				| ((scrollBar->page >= scrollBar->maximum || scrollBar->maximum <= 0 || scrollBar->page <= 0) ? UI_DRAW_CONTROL_STATE_DISABLED : 0),
				NULL, 0, 0, element->window->scale);
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
		UIDrawControl((UIPainter *) dp, element->bounds, (isDown ? UI_DRAW_CONTROL_SCROLL_DOWN : UI_DRAW_CONTROL_SCROLL_UP)
				| (scrollBar->horizontal ? 0 : UI_DRAW_CONTROL_STATE_VERTICAL) | UI_DRAW_CONTROL_STATE_FROM_ELEMENT(element),
				NULL, 0, 0, element->window->scale);
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_LEFT_DOWN) {
		UIElementAnimate(element, false);
		scrollBar->lastAnimateTime = UI_CLOCK();
	} else if (message == UI_MSG_LEFT_UP) {
		UIElementAnimate(element, true);
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
		UIDrawControl((UIPainter *) dp, element->bounds, UI_DRAW_CONTROL_SCROLL_THUMB
				| (scrollBar->horizontal ? 0 : UI_DRAW_CONTROL_STATE_VERTICAL)
				| UI_DRAW_CONTROL_STATE_FROM_ELEMENT(element), NULL, 0, 0, element->window->scale);
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_MOUSE_DRAG && element->window->pressedButton == 1) {
		if (!scrollBar->inDrag) {
			scrollBar->inDrag = true;

			if (scrollBar->horizontal) {
				scrollBar->dragOffset = element->bounds.l - scrollBar->e.bounds.l - element->window->cursorX;
			} else {
				scrollBar->dragOffset = element->bounds.t - scrollBar->e.bounds.t - element->window->cursorY;
			}
		}

		int thumbPosition = (scrollBar->horizontal ? element->window->cursorX : element->window->cursorY) + scrollBar->dragOffset;
		int size = scrollBar->horizontal ? (UI_RECT_WIDTH(scrollBar->e.bounds) - UI_RECT_WIDTH(element->bounds))
				: (UI_RECT_HEIGHT(scrollBar->e.bounds) - UI_RECT_HEIGHT(element->bounds));
		scrollBar->position = (double) thumbPosition / size * (scrollBar->maximum - scrollBar->page);
		UIElementRefresh(&scrollBar->e);
		UIElementMessage(scrollBar->e.parent, UI_MSG_SCROLLED, 0, 0);
	} else if (message == UI_MSG_LEFT_UP) {
		scrollBar->inDrag = false;
	}

	return 0;
}

UIScrollBar *UIScrollBarCreate(UIElement *parent, uint32_t flags) {
	UIScrollBar *scrollBar = (UIScrollBar *) UIElementCreate(sizeof(UIScrollBar), parent, flags, _UIScrollBarMessage, "Scroll Bar");
	bool horizontal = scrollBar->horizontal = flags & UI_SCROLL_BAR_HORIZONTAL;
	UIElementCreate(sizeof(UIElement), &scrollBar->e, flags, _UIScrollUpDownMessage, !horizontal ? "Scroll Up" : "Scroll Left")->cp = (void *) (uintptr_t) 0;
	UIElementCreate(sizeof(UIElement), &scrollBar->e, flags, _UIScrollThumbMessage, "Scroll Thumb");
	UIElementCreate(sizeof(UIElement), &scrollBar->e, flags, _UIScrollUpDownMessage, !horizontal ? "Scroll Down" : "Scroll Right")->cp = (void *) (uintptr_t) 1;
	return scrollBar;
}

/////////////////////////////////////////
// Code views.
/////////////////////////////////////////

bool _UICharIsDigit(int c) {
	return c >= '0' && c <= '9';
}

bool _UICharIsAlpha(int c) {
	return (
		('A' <= c && c <= 'Z') ||
		('a' <= c && c <= 'z') ||
		c > 127
	);
}

bool _UICharIsAlphaOrDigitOrUnderscore(int c) {
	return _UICharIsAlpha(c) || _UICharIsDigit(c) || c == '_';
}

#ifdef UI_UNICODE
#define _UI_ADVANCE_BYTE(byte, code, bytes) \
	byte += Utf8GetCharBytes(&code->content[byte + code->lines[line].offset], bytes - byte)
#else
#define _UI_ADVANCE_BYTE(byte, code, bytes) byte++
#endif
int _UICodeColumnToByte(UICode *code, int line, int column) {
	int byte = 0, ti = 0;
	int bytes = code->lines[line].bytes;

	while (byte < bytes) {
		ti++;
		if (code->content[byte + code->lines[line].offset] == '\t') while (ti % code->tabSize) ti++;
		if (column < ti) break;

		_UI_ADVANCE_BYTE(byte, code, bytes);
	}

	return byte;
}

#ifdef UI_UNICODE
#define _UI_ADVANCE_COLUMN(columnIndex, code, byte) \
	columnIndex += Utf8GetCharBytes(&code->content[columnIndex + code->lines[line].offset], code->lines[line].bytes - columnIndex)

#else
#define _UI_ADVANCE_COLUMN(columnIndex, code, byte) \
	columnIndex++
#endif
int _UICodeByteToColumn(UICode *code, int line, int byte) {
	int ti = 0, i = 0;

	while (i < byte) {
		ti++;
		_UI_ADVANCE_COLUMN(i, code, byte);
	}

	return ti;
}

void UICodePositionToByte(UICode *code, int x, int y, int *line, int *byte) {
	UIFont *previousFont = UIFontActivate(code->font);
	int lineHeight = UIMeasureStringHeight();
	*line = (y - code->e.bounds.t + code->vScroll->position) / lineHeight;
	if (*line < 0) *line = 0;
	else if (*line >= code->lineCount) *line = code->lineCount - 1;
	int column = (x - code->e.bounds.l + code->hScroll->position + ui.activeFont->glyphWidth / 2) / ui.activeFont->glyphWidth;
	if (~code->e.flags & UI_CODE_NO_MARGIN) column -= (UI_SIZE_CODE_MARGIN + UI_SIZE_CODE_MARGIN_GAP) / ui.activeFont->glyphWidth;
	UIFontActivate(previousFont);
	*byte = _UICodeColumnToByte(code, *line, column);
}

int UICodeHitTest(UICode *code, int x, int y) {
	x -= code->e.bounds.l;

	if (x < 0 || x >= code->vScroll->e.bounds.l) {
		return 0;
	}

	y -= code->e.bounds.t - code->vScroll->position;

	UIFont *previousFont = UIFontActivate(code->font);
	int lineHeight = UIMeasureStringHeight();
	bool inMargin = x < UI_SIZE_CODE_MARGIN + UI_SIZE_CODE_MARGIN_GAP / 2 && (~code->e.flags & UI_CODE_NO_MARGIN);
	UIFontActivate(previousFont);

	if (y < 0 || y >= lineHeight * code->lineCount) {
		return 0;
	}

	int line = y / lineHeight + 1;
	return inMargin ? -line : line;
}

int UIDrawStringHighlighted(UIPainter *painter, UIRectangle lineBounds, const char *string, ptrdiff_t bytes, int tabSize, UIStringSelection *selection) {
	if (bytes == -1) bytes = _UIStringLength(string);
	if (bytes > 10000) bytes = 10000;

	typedef enum _UICodeTokenType {
		UI_CODE_TOKEN_TYPE_DEFAULT,
		UI_CODE_TOKEN_TYPE_COMMENT,
		UI_CODE_TOKEN_TYPE_STRING,
		UI_CODE_TOKEN_TYPE_NUMBER,
		UI_CODE_TOKEN_TYPE_OPERATOR,
		UI_CODE_TOKEN_TYPE_PREPROCESSOR,
	} _UICodeTokenType;

	uint32_t colors[] = {
		ui.theme.codeDefault,
		ui.theme.codeComment,
		ui.theme.codeString,
		ui.theme.codeNumber,
		ui.theme.codeOperator,
		ui.theme.codePreprocessor,
	};

	int lineHeight = UIMeasureStringHeight();
	int x = lineBounds.l;
	int y = (lineBounds.t + lineBounds.b - lineHeight) / 2;
	int ti = 0;
	_UICodeTokenType tokenType = UI_CODE_TOKEN_TYPE_DEFAULT;
	bool inComment = false, inIdentifier = false, inChar = false, startedString = false, startedPreprocessor = false;
	uint32_t last = 0;
	int j = 0;

	while (bytes) {
#ifdef UI_UNICODE
		ptrdiff_t bytesConsumed;
		int c = Utf8GetCodePoint(string, bytes, &bytesConsumed);
		UI_ASSERT(bytesConsumed > 0);
		string += bytesConsumed;
		bytes -= bytesConsumed;
#else
		char c = *string++;
		bytes--;
#endif

		last <<= 8;
		last |= (char) c;

		if (tokenType == UI_CODE_TOKEN_TYPE_PREPROCESSOR) {
			if (bytes && c == '/' && (*string == '/' || *string == '*')) {
				tokenType = UI_CODE_TOKEN_TYPE_DEFAULT;
			}
		} else if (tokenType == UI_CODE_TOKEN_TYPE_OPERATOR) {
			tokenType = UI_CODE_TOKEN_TYPE_DEFAULT;
		} else if (tokenType == UI_CODE_TOKEN_TYPE_COMMENT) {
			if ((last & 0xFF0000) == ('*' << 16) && (last & 0xFF00) == ('/' << 8) && inComment) {
				tokenType = startedPreprocessor ? UI_CODE_TOKEN_TYPE_PREPROCESSOR : UI_CODE_TOKEN_TYPE_DEFAULT;
				inComment = false;
			}
		} else if (tokenType == UI_CODE_TOKEN_TYPE_NUMBER) {
			if (!_UICharIsAlpha(c) && !_UICharIsDigit(c)) {
				tokenType = UI_CODE_TOKEN_TYPE_DEFAULT;
			}
		} else if (tokenType == UI_CODE_TOKEN_TYPE_STRING) {
			if (!startedString) {
				if (!inChar && ((last >> 8) & 0xFF) == '"' && ((last >> 16) & 0xFF) != '\\') {
					tokenType = UI_CODE_TOKEN_TYPE_DEFAULT;
				} else if (inChar && ((last >> 8) & 0xFF) == '\'' && ((last >> 16) & 0xFF) != '\\') {
					tokenType = UI_CODE_TOKEN_TYPE_DEFAULT;
				}
			}

			startedString = false;
		}

		if (tokenType == UI_CODE_TOKEN_TYPE_DEFAULT) {
			if (c == '#') {
				tokenType = UI_CODE_TOKEN_TYPE_PREPROCESSOR;
				startedPreprocessor = true;
			} else if (bytes && c == '/' && *string == '/') {
				tokenType = UI_CODE_TOKEN_TYPE_COMMENT;
			} else if (bytes && c == '/' && *string == '*') {
				tokenType = UI_CODE_TOKEN_TYPE_COMMENT, inComment = true;
			} else if (c == '"') {
				tokenType = UI_CODE_TOKEN_TYPE_STRING;
				inChar = false;
				startedString = true;
			} else if (c == '\'') {
				tokenType = UI_CODE_TOKEN_TYPE_STRING;
				inChar = true;
				startedString = true;
			} else if (_UICharIsDigit(c) && !inIdentifier) {
				tokenType = UI_CODE_TOKEN_TYPE_NUMBER;
			} else if (!_UICharIsAlpha(c) && !_UICharIsDigit(c)) {
				tokenType = UI_CODE_TOKEN_TYPE_OPERATOR;
				inIdentifier = false;
			} else {
				inIdentifier = true;
			}
		}

		int oldX = x;

		if (c == '\t') {
			x += ui.activeFont->glyphWidth, ti++;
			while (ti % tabSize) x += ui.activeFont->glyphWidth, ti++;
		} else {
			UIDrawGlyph(painter, x, y, c, colors[tokenType]);
			x += ui.activeFont->glyphWidth, ti++;
		}

		if (selection && j >= selection->carets[0] && j < selection->carets[1]) {
			UIDrawBlock(painter, UI_RECT_4(oldX, x, y, y + lineHeight), selection->colorBackground);
			if (c != '\t') UIDrawGlyph(painter, oldX, y, c, selection->colorText);
		}

		if (selection && selection->carets[0] == j) {
			UIDrawInvert(painter, UI_RECT_4(oldX, oldX + 1, y, y + lineHeight));
		}

		j++;
	}

	if (selection && selection->carets[0] == j) {
		UIDrawInvert(painter, UI_RECT_4(x, x + 1, y, y + lineHeight));
	}

	return x;
}

void _UICodeUpdateSelection(UICode *code) {
	bool swap = code->selection[3].line < code->selection[2].line
		|| (code->selection[3].line == code->selection[2].line && code->selection[3].offset < code->selection[2].offset);
	code->selection[1 - swap] = code->selection[3];
	code->selection[0 + swap] = code->selection[2];
	code->moveScrollToCaretNextLayout = true;
	UIElementRefresh(&code->e);
}

void _UICodeSetVerticalMotionColumn(UICode *code, bool restore) {
	if (restore) {
		code->selection[3].offset = _UICodeColumnToByte(code, code->selection[3].line, code->verticalMotionColumn);
	} else if (!code->useVerticalMotionColumn) {
		code->useVerticalMotionColumn = true;
		code->verticalMotionColumn = _UICodeByteToColumn(code, code->selection[3].line, code->selection[3].offset);
	}
}

void _UICodeCopyText(void *cp) {
	UICode *code = (UICode *) cp;

	int from = code->lines[code->selection[0].line].offset + code->selection[0].offset;
	int to = code->lines[code->selection[1].line].offset + code->selection[1].offset;

	if (from != to) {
		char *pasteText = (char *) UI_CALLOC(to - from + 2);
		for (int i = from; i < to; i++) pasteText[i - from] = code->content[i];
		_UIClipboardWriteText(code->e.window, pasteText);
	}
}

int _UICodeMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UICode *code = (UICode *) element;

	if (message == UI_MSG_LAYOUT) {
		UIFont *previousFont = UIFontActivate(code->font);
		int scrollBarSize = UI_SIZE_SCROLL_BAR * code->e.window->scale;
		code->vScroll->maximum = code->lineCount * UIMeasureStringHeight();
		code->hScroll->maximum = code->columns * code->font->glyphWidth; // TODO This doesn't take into account tab sizes!
		int vSpace = code->vScroll->page = UI_RECT_HEIGHT(element->bounds);
		int hSpace = code->hScroll->page = UI_RECT_WIDTH(element->bounds);

		if (code->moveScrollToCaretNextLayout) {
			int top = code->selection[3].line * UIMeasureStringHeight();
			int bottom = top + UIMeasureStringHeight();
			int context = UIMeasureStringHeight() * 2;
			if (bottom > code->vScroll->position + vSpace - context) code->vScroll->position = bottom - vSpace + context;
			if (top < code->vScroll->position + context) code->vScroll->position = top - context;
			code->moveScrollToCaretNextLayout = code->moveScrollToFocusNextLayout = false;
			// TODO Horizontal scrolling.
		} else if (code->moveScrollToFocusNextLayout) {
			code->vScroll->position = (code->focused + 0.5) * UIMeasureStringHeight() - UI_RECT_HEIGHT(code->e.bounds) / 2;
		}

		if (!(code->e.flags & UI_CODE_NO_MARGIN)) hSpace -= UI_SIZE_CODE_MARGIN + UI_SIZE_CODE_MARGIN_GAP;
		_UI_LAYOUT_SCROLL_BAR_PAIR(code);

		UIFontActivate(previousFont);
	} else if (message == UI_MSG_PAINT) {
		UIFont *previousFont = UIFontActivate(code->font);

		UIPainter *painter = (UIPainter *) dp;
		UIRectangle lineBounds = element->bounds;

		lineBounds.r = code->vScroll->e.bounds.l;

		if (~code->e.flags & UI_CODE_NO_MARGIN) {
			lineBounds.l += UI_SIZE_CODE_MARGIN + UI_SIZE_CODE_MARGIN_GAP;
		}

		int lineHeight = UIMeasureStringHeight();
		lineBounds.t -= (int64_t) code->vScroll->position % lineHeight;

		UIDrawBlock(painter, element->bounds, ui.theme.codeBackground);

#ifdef __cplusplus
		UIStringSelection selection = {};
#else
		UIStringSelection selection = { 0 };
#endif
		selection.colorBackground = ui.theme.selected;
		selection.colorText = ui.theme.textSelected;

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

				UIDrawString(painter, marginBounds, string + p, 16 - p,
						marginColor ? ui.theme.codeDefault : ui.theme.codeComment, UI_ALIGN_RIGHT, NULL);
			}

			if (code->focused == i) {
				UIDrawBlock(painter, lineBounds, ui.theme.codeFocused);
			}

			UIRectangle oldClip = painter->clip;
			painter->clip = UIRectangleIntersection(oldClip, lineBounds);
			if (code->hScroll) lineBounds.l -= (int64_t) code->hScroll->position;
			selection.carets[0] = i == code->selection[0].line ? _UICodeByteToColumn(code, i, code->selection[0].offset) : 0;
			selection.carets[1] = i == code->selection[1].line ? _UICodeByteToColumn(code, i, code->selection[1].offset) : code->lines[i].bytes;
			int x = UIDrawStringHighlighted(painter, lineBounds, code->content + code->lines[i].offset, code->lines[i].bytes, code->tabSize,
					element->window->focused == element && i >= code->selection[0].line && i <= code->selection[1].line ? &selection : NULL);
			int y = (lineBounds.t + lineBounds.b - UIMeasureStringHeight()) / 2;

			if (element->window->focused == element && i >= code->selection[0].line && i < code->selection[1].line) {
				UIDrawBlock(painter, UI_RECT_4PD(x, y, code->font->glyphWidth, code->font->glyphHeight), selection.colorBackground);
			}

			if (code->hScroll) lineBounds.l += (int64_t) code->hScroll->position;
			painter->clip = oldClip;

			UICodeDecorateLine m;
			m.x = x, m.y = y, m.bounds = lineBounds, m.index = i + 1, m.painter = painter;
			UIElementMessage(element, UI_MSG_CODE_DECORATE_LINE, 0, &m);

			lineBounds.t += lineHeight;
		}

		UIFontActivate(previousFont);
	} else if (message == UI_MSG_SCROLLED) {
		code->moveScrollToFocusNextLayout = false;
		UIElementRefresh(element);
	} else if (message == UI_MSG_MOUSE_WHEEL) {
		return UIElementMessage(&code->vScroll->e, message, di, dp);
	} else if (message == UI_MSG_GET_CURSOR) {
		if (UICodeHitTest(code, element->window->cursorX, element->window->cursorY) < 0) {
			return UI_CURSOR_FLIPPED_ARROW;
		}

		if (element->flags & UI_CODE_SELECTABLE) {
			return UI_CURSOR_TEXT;
		}
	} else if (message == UI_MSG_LEFT_UP) {
		UIElementAnimate(element, true);
	} else if (message == UI_MSG_LEFT_DOWN && code->lineCount) {
		int hitTest = UICodeHitTest(code, element->window->cursorX, element->window->cursorY);
		code->leftDownInMargin = hitTest < 0;

		if (hitTest > 0 && (element->flags & UI_CODE_SELECTABLE)) {
			UICodePositionToByte(code, element->window->cursorX, element->window->cursorY, &code->selection[2].line, &code->selection[2].offset);
			_UICodeMessage(element, UI_MSG_MOUSE_DRAG, di, dp);
			UIElementFocus(element);
			UIElementAnimate(element, false);
			code->lastAnimateTime = UI_CLOCK();
		}
	} else if (message == UI_MSG_ANIMATE) {
		if (element->window->pressed == element && element->window->pressedButton == 1 && code->lineCount && !code->leftDownInMargin) {
			UI_CLOCK_T previous = code->lastAnimateTime;
			UI_CLOCK_T current = UI_CLOCK();
			UI_CLOCK_T deltaTicks = current - previous;
			double deltaSeconds = (double) deltaTicks / UI_CLOCKS_PER_SECOND;
			if (deltaSeconds > 0.1) deltaSeconds = 0.1;
			int delta = deltaSeconds * 800;
			if (!delta) { return 0; }
			code->lastAnimateTime = current;

			UIFont *previousFont = UIFontActivate(code->font);

			if (element->window->cursorX < element->bounds.l + ((element->flags & UI_CODE_NO_MARGIN)
						? UI_SIZE_CODE_MARGIN_GAP : (UI_SIZE_CODE_MARGIN + UI_SIZE_CODE_MARGIN_GAP * 2))) {
				code->hScroll->position -= delta;
			} else if (element->window->cursorX >= code->vScroll->e.bounds.l - UI_SIZE_CODE_MARGIN_GAP) {
				code->hScroll->position += delta;
			}

			if (element->window->cursorY < element->bounds.t + UI_SIZE_CODE_MARGIN_GAP) {
				code->vScroll->position -= delta;
			} else if (element->window->cursorY >= code->hScroll->e.bounds.t - UI_SIZE_CODE_MARGIN_GAP) {
				code->vScroll->position += delta;
			}

			code->moveScrollToFocusNextLayout = false;
			UIFontActivate(previousFont);
			_UICodeMessage(element, UI_MSG_MOUSE_DRAG, di, dp);
			UIElementRefresh(element);
		}
	} else if (message == UI_MSG_MOUSE_DRAG && element->window->pressedButton == 1 && code->lineCount && !code->leftDownInMargin) {
		// TODO Double-click and triple-click dragging for word and line granularity respectively.
		UICodePositionToByte(code, element->window->cursorX, element->window->cursorY, &code->selection[3].line, &code->selection[3].offset);
		_UICodeUpdateSelection(code);
		code->moveScrollToFocusNextLayout = code->moveScrollToCaretNextLayout = false;
		code->useVerticalMotionColumn = false;
	} else if (message == UI_MSG_KEY_TYPED && code->lineCount) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		if ((m->code == UI_KEYCODE_LETTER('C') || m->code == UI_KEYCODE_LETTER('X') || m->code == UI_KEYCODE_INSERT)
				&& element->window->ctrl && !element->window->alt && !element->window->shift) {
			_UICodeCopyText(code);
		} else if ((m->code == UI_KEYCODE_UP || m->code == UI_KEYCODE_DOWN || m->code == UI_KEYCODE_PAGE_UP || m->code == UI_KEYCODE_PAGE_DOWN)
				&& !element->window->ctrl && !element->window->alt) {
			UIFont *previousFont = UIFontActivate(code->font);
			int lineHeight = UIMeasureStringHeight();

			if (element->window->shift) {
				if (m->code == UI_KEYCODE_UP) {
					if (code->selection[3].line - 1 >= 0) {
						_UICodeSetVerticalMotionColumn(code, false);
						code->selection[3].line--;
						_UICodeSetVerticalMotionColumn(code, true);
					}
				} else if (m->code == UI_KEYCODE_DOWN) {
					if (code->selection[3].line + 1 < code->lineCount) {
						_UICodeSetVerticalMotionColumn(code, false);
						code->selection[3].line++;
						_UICodeSetVerticalMotionColumn(code, true);
					}
				} else if (m->code == UI_KEYCODE_PAGE_UP || m->code == UI_KEYCODE_PAGE_DOWN) {
					_UICodeSetVerticalMotionColumn(code, false);
					int pageHeight = (element->bounds.t - code->hScroll->e.bounds.t) / lineHeight * 4 / 5;
					code->selection[3].line += m->code == UI_KEYCODE_PAGE_UP ? pageHeight : -pageHeight;
					if (code->selection[3].line < 0) code->selection[3].line = 0;
					if (code->selection[3].line >= code->lineCount) code->selection[3].line = code->lineCount - 1;
					_UICodeSetVerticalMotionColumn(code, true);
				}

				_UICodeUpdateSelection(code);
			} else {
				code->moveScrollToFocusNextLayout = false;
				_UI_KEY_INPUT_VSCROLL(code, lineHeight, (element->bounds.t - code->hScroll->e.bounds.t) * 4 / 5 /* leave a few lines for context */);
			}

			UIFontActivate(previousFont);
		} else if ((m->code == UI_KEYCODE_HOME || m->code == UI_KEYCODE_END) && !element->window->alt) {
			if (element->window->shift) {
				if (m->code == UI_KEYCODE_HOME) {
					if (element->window->ctrl) code->selection[3].line = 0;
					code->selection[3].offset = 0;
					code->useVerticalMotionColumn = false;
				} else {
					if (element->window->ctrl) code->selection[3].line = code->lineCount - 1;
					code->selection[3].offset = code->lines[code->selection[3].line].bytes;
					code->useVerticalMotionColumn = false;
				}

				_UICodeUpdateSelection(code);
			} else {
				code->vScroll->position = m->code == UI_KEYCODE_HOME ? 0 : code->vScroll->maximum;
				code->moveScrollToFocusNextLayout = false;
				UIElementRefresh(&code->e);
			}
		} else if ((m->code == UI_KEYCODE_LEFT || m->code == UI_KEYCODE_RIGHT) && !element->window->alt) {
			if (element->window->shift) {
				UICodeMoveCaret(code, m->code == UI_KEYCODE_LEFT, element->window->ctrl);
			} else if (!element->window->ctrl) {
				code->hScroll->position += m->code == UI_KEYCODE_LEFT ? -ui.activeFont->glyphWidth : ui.activeFont->glyphWidth;
				UIElementRefresh(&code->e);
			} else {
				return 0;
			}
		} else {
			return 0;
		}

		return 1;
	} else if (message == UI_MSG_RIGHT_DOWN) {
		int hitTest = UICodeHitTest(code, element->window->cursorX, element->window->cursorY);

		if (hitTest > 0 && (element->flags & UI_CODE_SELECTABLE)) {
			UIElementFocus(element);
			UIMenu *menu = UIMenuCreate(&element->window->e, UI_MENU_NO_SCROLL);
			UIMenuAddItem(menu, (code->selection[0].line == code->selection[1].line
						&& code->selection[0].offset == code->selection[1].offset) ? UI_ELEMENT_DISABLED : 0, "Copy", -1, _UICodeCopyText, code);
			UIMenuShow(menu);
		}
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_DEALLOCATE) {
		UI_FREE(code->content);
		UI_FREE(code->lines);
	}

	return 0;
}

#ifdef UI_UNICODE
#define _UI_CODE_MOVE_CARET_BACKWARD(code) do { \
	int offset = code->lines[code->selection[3].line].offset + code->selection[3].offset; \
	char *prev = Utf8GetPreviousChar(code->content, code->content + offset); \
	code->selection[3].offset = prev - code->content - code->lines[code->selection[3].line].offset; \
} while (0)

#define _UI_CODE_MOVE_CARET_FORWARD(code) do { \
	int offset = code->lines[code->selection[3].line].offset + code->selection[3].offset; \
	code->selection[3].offset += Utf8GetCharBytes(code->content + offset, code->contentBytes - offset); \
} while (0)

#define _UI_CODE_MOVE_CARET_BY_WORD(code) { \
	int offset = code->lines[code->selection[3].line].offset + code->selection[3].offset; \
	char *prev = Utf8GetPreviousChar(code->content, code->content + offset); \
	int c1 = Utf8GetCodePoint(prev, code->contentBytes - (prev - code->content), NULL); \
	int c2 = Utf8GetCodePoint(code->content + offset, code->contentBytes - offset, NULL); \
	if (_UICharIsAlphaOrDigitOrUnderscore(c1) != _UICharIsAlphaOrDigitOrUnderscore(c2)) break; \
}

#else
#define _UI_CODE_MOVE_CARET_BACKWARD(code) code->selection[3].offset--
#define _UI_CODE_MOVE_CARET_FORWARD(code) code->selection[3].offset++

#define _UI_CODE_MOVE_CARET_BY_WORD(code) { \
	char c1 = code->content[code->lines[code->selection[3].line].offset + code->selection[3].offset - 1]; \
	char c2 = code->content[code->lines[code->selection[3].line].offset + code->selection[3].offset]; \
	if (_UICharIsAlphaOrDigitOrUnderscore(c1) != _UICharIsAlphaOrDigitOrUnderscore(c2)) break; \
}

#endif

void UICodeMoveCaret(UICode *code, bool backward, bool word) {
	while (true) {
		if (backward) {
			if (code->selection[3].offset - 1 < 0) {
				if (code->selection[3].line > 0) {
					code->selection[3].line--;
					code->selection[3].offset = code->lines[code->selection[3].line].bytes;
				} else break;
			} else _UI_CODE_MOVE_CARET_BACKWARD(code);
		} else {
			if (code->selection[3].offset + 1 > code->lines[code->selection[3].line].bytes) {
				if (code->selection[3].line + 1 < code->lineCount) {
					code->selection[3].line++;
					code->selection[3].offset = 0;
				} else break;
			} else _UI_CODE_MOVE_CARET_FORWARD(code);
		}

		if (!word) break;

		if (code->selection[3].offset != 0 && code->selection[3].offset != code->lines[code->selection[3].line].bytes) _UI_CODE_MOVE_CARET_BY_WORD(code);
	}

	code->useVerticalMotionColumn = false;
	_UICodeUpdateSelection(code);
}

void UICodeFocusLine(UICode *code, int index) {
	code->focused = index - 1;
	code->moveScrollToFocusNextLayout = true;
	UIElementRefresh(&code->e);
}

void UICodeInsertContent(UICode *code, const char *content, ptrdiff_t byteCount, bool replace) {
	code->useVerticalMotionColumn = false;

	UIFont *previousFont = UIFontActivate(code->font);

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
		code->columns = 0;
		code->selection[0].line = code->selection[1].line = 0;
		code->selection[0].offset = code->selection[1].offset = 0;
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
			if (line.bytes > code->columns) code->columns = line.bytes;
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

	UIFontActivate(previousFont);
	UIElementRepaint(&code->e, NULL);
}

UICode *UICodeCreate(UIElement *parent, uint32_t flags) {
	UICode *code = (UICode *) UIElementCreate(sizeof(UICode), parent, flags, _UICodeMessage, "Code");
	code->font = ui.activeFont;
	code->vScroll = UIScrollBarCreate(&code->e, 0);
	code->hScroll = UIScrollBarCreate(&code->e, UI_SCROLL_BAR_HORIZONTAL);
	code->focused = -1;
	code->tabSize = 4;
	return code;
}

/////////////////////////////////////////
// Gauges.
/////////////////////////////////////////

int _UIGaugeMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIGauge *gauge = (UIGauge *) element;

	if (message == UI_MSG_GET_HEIGHT) {
		return UI_SIZE_GAUGE_HEIGHT * element->window->scale;
	} else if (message == UI_MSG_GET_WIDTH) {
		return UI_SIZE_GAUGE_WIDTH * element->window->scale;
	} else if (message == UI_MSG_PAINT) {
		UIDrawControl((UIPainter *) dp, element->bounds, UI_DRAW_CONTROL_GAUGE | UI_DRAW_CONTROL_STATE_FROM_ELEMENT(element),
				NULL, 0, gauge->position, element->window->scale);
	}

	return 0;
}

void UIGaugeSetPosition(UIGauge *gauge, float position) {
	if (position == gauge->position) return;
	gauge->position = position;
	UIElementRepaint(&gauge->e, NULL);
}

UIGauge *UIGaugeCreate(UIElement *parent, uint32_t flags) {
	return (UIGauge *) UIElementCreate(sizeof(UIGauge), parent, flags, _UIGaugeMessage, "Gauge");
}

/////////////////////////////////////////
// Sliders.
/////////////////////////////////////////

int _UISliderMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UISlider *slider = (UISlider *) element;

	if (message == UI_MSG_GET_HEIGHT) {
		return UI_SIZE_SLIDER_HEIGHT * element->window->scale;
	} else if (message == UI_MSG_GET_WIDTH) {
		return UI_SIZE_SLIDER_WIDTH * element->window->scale;
	} else if (message == UI_MSG_PAINT) {
		UIDrawControl((UIPainter *) dp, element->bounds, UI_DRAW_CONTROL_SLIDER | UI_DRAW_CONTROL_STATE_FROM_ELEMENT(element),
				NULL, 0, slider->position, element->window->scale);
	} else if (message == UI_MSG_LEFT_DOWN || (message == UI_MSG_MOUSE_DRAG && element->window->pressedButton == 1)) {
		UIRectangle bounds = element->bounds;
		int thumbSize = UI_SIZE_SLIDER_THUMB * element->window->scale;
		slider->position = (double) (element->window->cursorX - thumbSize / 2 - bounds.l) / (UI_RECT_WIDTH(bounds) - thumbSize);
		if (slider->steps > 1) slider->position = (int) (slider->position * (slider->steps - 1) + 0.5f) / (double) (slider->steps - 1);
		if (slider->position < 0) slider->position = 0;
		if (slider->position > 1) slider->position = 1;
		UIElementMessage(element, UI_MSG_VALUE_CHANGED, 0, 0);
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, NULL);
	}

	return 0;
}

UISlider *UISliderCreate(UIElement *parent, uint32_t flags) {
	return (UISlider *) UIElementCreate(sizeof(UISlider), parent, flags, _UISliderMessage, "Slider");
}

/////////////////////////////////////////
// Tables.
/////////////////////////////////////////

int UITableHitTest(UITable *table, int x, int y) {
	x -= table->e.bounds.l;

	if (x < 0 || x >= table->vScroll->e.bounds.l) {
		return -1;
	}

	y -= (table->e.bounds.t + UI_SIZE_TABLE_HEADER * table->e.window->scale) - table->vScroll->position;

	int rowHeight = UI_SIZE_TABLE_ROW * table->e.window->scale;

	if (y < 0 || y >= rowHeight * table->itemCount) {
		return -1;
	}

	return y / rowHeight;
}

int UITableHeaderHitTest(UITable *table, int x, int y) {
	if (!table->columnCount) return -1;
	UIRectangle header = table->e.bounds;
	header.b = header.t + UI_SIZE_TABLE_HEADER * table->e.window->scale;
	header.l += UI_SIZE_TABLE_COLUMN_GAP * table->e.window->scale;
	int position = 0, index = 0;

	while (true) {
		int end = position;
		for (; table->columns[end] != '\t' && table->columns[end]; end++);
		header.r = header.l + table->columnWidths[index];
		if (UIRectangleContains(header, x, y)) return index;
		header.l += table->columnWidths[index] + UI_SIZE_TABLE_COLUMN_GAP * table->e.window->scale;
		if (table->columns[end] != '\t') break;
		position = end + 1, index++;
	}

	return -1;
}

bool UITableEnsureVisible(UITable *table, int index) {
	int rowHeight = UI_SIZE_TABLE_ROW * table->e.window->scale;
	int y = index * rowHeight;
	y -= table->vScroll->position;
	int height = UI_RECT_HEIGHT(table->e.bounds) - UI_SIZE_TABLE_HEADER * table->e.window->scale - rowHeight;

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

		int longest = UIMeasureStringWidth(table->columns + position, end - position);

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

	UIElementRepaint(&table->e, NULL);
}

int _UITableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UITable *table = (UITable *) element;

	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		UIRectangle bounds = element->bounds;
		bounds.r = table->vScroll->e.bounds.l;
		UIDrawControl(painter, element->bounds, UI_DRAW_CONTROL_TABLE_BACKGROUND | UI_DRAW_CONTROL_STATE_FROM_ELEMENT(element), NULL, 0, 0, element->window->scale);
		char buffer[256];
		UIRectangle row = bounds;
		int rowHeight = UI_SIZE_TABLE_ROW * element->window->scale;
		UITableGetItem m = { 0 };
		m.buffer = buffer;
		m.bufferBytes = sizeof(buffer);
		row.t += UI_SIZE_TABLE_HEADER * table->e.window->scale;
		row.t -= (int64_t) table->vScroll->position % rowHeight;
		int hovered = UITableHitTest(table, element->window->cursorX, element->window->cursorY);
		UIRectangle oldClip = painter->clip;
		painter->clip = UIRectangleIntersection(oldClip, UI_RECT_4(bounds.l, bounds.r,
					bounds.t + (int) (UI_SIZE_TABLE_HEADER * element->window->scale), bounds.b));

		for (int i = table->vScroll->position / rowHeight; i < table->itemCount; i++) {
			if (row.t > painter->clip.b) {
				break;
			}

			row.b = row.t + rowHeight;
			m.index = i;
			m.isSelected = false;
			m.column = 0;
			int bytes = UIElementMessage(element, UI_MSG_TABLE_GET_ITEM, 0, &m);

			uint32_t rowFlags = (m.isSelected ? UI_DRAW_CONTROL_STATE_SELECTED : 0) | (hovered == i ? UI_DRAW_CONTROL_STATE_HOVERED : 0);
			UIDrawControl(painter, row, UI_DRAW_CONTROL_TABLE_ROW | rowFlags, NULL, 0, 0, element->window->scale);

			UIRectangle cell = row;
			cell.l += UI_SIZE_TABLE_COLUMN_GAP * table->e.window->scale - (int64_t) table->hScroll->position;

			for (int j = 0; j < table->columnCount; j++) {
				if (j) {
					m.column = j;
					bytes = UIElementMessage(element, UI_MSG_TABLE_GET_ITEM, 0, &m);
				}

				cell.r = cell.l + table->columnWidths[j];
				if ((size_t) bytes > m.bufferBytes && bytes > 0) bytes = m.bufferBytes;
				UIDrawControl(painter, cell, UI_DRAW_CONTROL_TABLE_CELL | rowFlags, buffer, bytes, 0, element->window->scale);
				cell.l += table->columnWidths[j] + UI_SIZE_TABLE_COLUMN_GAP * table->e.window->scale;
			}

			row.t += rowHeight;
		}

		bounds = element->bounds;
		painter->clip = UIRectangleIntersection(oldClip, bounds);
		if (table->hScroll) bounds.l -= (int64_t) table->hScroll->position;

		UIRectangle header = bounds;
		header.b = header.t + UI_SIZE_TABLE_HEADER * table->e.window->scale;
		header.l += UI_SIZE_TABLE_COLUMN_GAP * table->e.window->scale;

		int position = 0;
		int index = 0;

		if (table->columnCount) {
			while (true) {
				int end = position;
				for (; table->columns[end] != '\t' && table->columns[end]; end++);

				header.r = header.l + table->columnWidths[index];
				UIDrawControl(painter, header, UI_DRAW_CONTROL_TABLE_HEADER | (index == table->columnHighlight ? UI_DRAW_CONTROL_STATE_SELECTED : 0),
						table->columns + position, end - position, 0, element->window->scale);
				header.l += table->columnWidths[index] + UI_SIZE_TABLE_COLUMN_GAP * table->e.window->scale;

				if (table->columns[end] == '\t') {
					position = end + 1;
					index++;
				} else {
					break;
				}
			}
		}
	} else if (message == UI_MSG_LAYOUT) {
		int scrollBarSize = UI_SIZE_SCROLL_BAR * table->e.window->scale;
		int columnGap = UI_SIZE_TABLE_COLUMN_GAP * table->e.window->scale;

		table->vScroll->maximum = table->itemCount * UI_SIZE_TABLE_ROW * element->window->scale;
		table->hScroll->maximum = columnGap;
		for (int i = 0; i < table->columnCount; i++) { table->hScroll->maximum += table->columnWidths[i] + columnGap; }

		int vSpace = table->vScroll->page = UI_RECT_HEIGHT(element->bounds) - UI_SIZE_TABLE_HEADER * element->window->scale;
		int hSpace = table->hScroll->page = UI_RECT_WIDTH(element->bounds);
		_UI_LAYOUT_SCROLL_BAR_PAIR(table);
	} else if (message == UI_MSG_MOUSE_MOVE || message == UI_MSG_UPDATE) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_SCROLLED) {
		UIElementRefresh(element);
	} else if (message == UI_MSG_MOUSE_WHEEL) {
		return UIElementMessage(&table->vScroll->e, message, di, dp);
	} else if (message == UI_MSG_LEFT_DOWN) {
		UIElementFocus(element);
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		if ((m->code == UI_KEYCODE_UP || m->code == UI_KEYCODE_DOWN || m->code == UI_KEYCODE_PAGE_UP || m->code == UI_KEYCODE_PAGE_DOWN
				|| m->code == UI_KEYCODE_HOME || m->code == UI_KEYCODE_END)
				&& !element->window->ctrl && !element->window->alt && !element->window->shift) {
			_UI_KEY_INPUT_VSCROLL(table, UI_SIZE_TABLE_ROW * element->window->scale,
					(element->bounds.t - table->hScroll->e.bounds.t + UI_SIZE_TABLE_HEADER) * 4 / 5);
			return 1;
		} else if ((m->code == UI_KEYCODE_LEFT || m->code == UI_KEYCODE_RIGHT)
				&& !element->window->ctrl && !element->window->alt && !element->window->shift) {
			table->hScroll->position += m->code == UI_KEYCODE_LEFT ? -ui.activeFont->glyphWidth : ui.activeFont->glyphWidth;
			UIElementRefresh(&table->e);
			return 1;
		}
	} else if (message == UI_MSG_DEALLOCATE) {
		UI_FREE(table->columns);
		UI_FREE(table->columnWidths);
	}

	return 0;
}

UITable *UITableCreate(UIElement *parent, uint32_t flags, const char *columns) {
	UITable *table = (UITable *) UIElementCreate(sizeof(UITable), parent, flags, _UITableMessage, "Table");
	table->vScroll = UIScrollBarCreate(&table->e, 0);
	table->hScroll = UIScrollBarCreate(&table->e, UI_SCROLL_BAR_HORIZONTAL);
	table->columns = UIStringCopy(columns, -1);
	table->columnHighlight = -1;
	return table;
}

/////////////////////////////////////////
// Textboxes.
/////////////////////////////////////////

int _UITextboxByteToColumn(const char *string, int byte, ptrdiff_t bytes) {
	int ti = 0, i = 0;

	while (i < byte) {
		ti++;
		if (string[i] == '\t') while (ti & 3) ti++;
#ifdef UI_UNICODE
		i += Utf8GetCharBytes(string + i, bytes - i);
#else
		i++;
#endif
	}

	return ti;
}

char *UITextboxToCString(UITextbox *textbox) {
	char *buffer = (char *) UI_MALLOC(textbox->bytes + 1);

	for (intptr_t i = 0; i < textbox->bytes; i++) {
		buffer[i] = textbox->string[i];
	}

	buffer[textbox->bytes] = 0;
	return buffer;
}

void UITextboxReplace(UITextbox *textbox, const char *text, ptrdiff_t bytes, bool sendChangedMessage) {
	if (bytes == -1) bytes = _UIStringLength(text);
	int deleteFrom = textbox->carets[0], deleteTo = textbox->carets[1];
	if (deleteFrom > deleteTo) UI_SWAP(int, deleteFrom, deleteTo);

	UI_MEMMOVE(&textbox->string[deleteFrom], &textbox->string[deleteTo], textbox->bytes - deleteTo);
	textbox->bytes -= deleteTo - deleteFrom;
	textbox->string = (char *) UI_REALLOC(textbox->string, textbox->bytes + bytes);
	UI_MEMMOVE(&textbox->string[deleteFrom + bytes], &textbox->string[deleteFrom], textbox->bytes - deleteFrom);
	UI_MEMMOVE(&textbox->string[deleteFrom], &text[0], bytes);
	textbox->bytes += bytes;
	textbox->carets[0] = deleteFrom + bytes;
	textbox->carets[1] = textbox->carets[0];

	if (sendChangedMessage) UIElementMessage(&textbox->e, UI_MSG_VALUE_CHANGED, 0, 0);
	textbox->e.window->textboxModifiedFlag = true;
	UIElementRepaint(&textbox->e, NULL);
}

void UITextboxClear(UITextbox *textbox, bool sendChangedMessage) {
	textbox->carets[1] = 0;
	textbox->carets[0] = textbox->bytes;
	UITextboxReplace(textbox, "", 0, sendChangedMessage);
}

#ifdef UI_UNICODE

#define _UI_TEXTBOX_MOVE_CARET_BACKWARD(textbox) do { \
	char *prev = Utf8GetPreviousChar(textbox->string, textbox->string + textbox->carets[0]); \
	textbox->carets[0] = prev - textbox->string; \
} while (0)

#define _UI_TEXTBOX_MOVE_CARET_FORWARD(textbox) do { \
	textbox->carets[0] += Utf8GetCharBytes(textbox->string + textbox->carets[0], textbox->bytes - textbox->carets[0]); \
} while (0)

#define _UI_TEXTBOX_MOVE_CARET_WORD(textbox) do { \
	char *prev = Utf8GetPreviousChar(textbox->string, textbox->string + textbox->carets[0]); \
	int c1 = Utf8GetCodePoint(prev, textbox->bytes - (prev - textbox->string), NULL); \
	int c2 = Utf8GetCodePoint(textbox->string + textbox->carets[0], textbox->bytes - textbox->carets[0], NULL); \
	if (_UICharIsAlphaOrDigitOrUnderscore(c1) != _UICharIsAlphaOrDigitOrUnderscore(c2)) { \
		return; \
	} \
} while (0)

#else

#define _UI_TEXTBOX_MOVE_CARET_BACKWARD(textbox) textbox->carets[0]--;
#define _UI_TEXTBOX_MOVE_CARET_FORWARD(textbox) textbox->carets[0]++;

#define _UI_TEXTBOX_MOVE_CARET_WORD(textbox) do { \
	char c1 = textbox->string[textbox->carets[0] - 1]; \
	char c2 = textbox->string[textbox->carets[0]]; \
	if (_UICharIsAlphaOrDigitOrUnderscore(c1) != _UICharIsAlphaOrDigitOrUnderscore(c2)) { \
		return; \
	} \
} while (0)

#endif

void UITextboxMoveCaret(UITextbox *textbox, bool backward, bool word) {
	while (true) {
		if (textbox->carets[0] > 0 && backward) {
			_UI_TEXTBOX_MOVE_CARET_BACKWARD(textbox);
		} else if (textbox->carets[0] < textbox->bytes && !backward) {
			_UI_TEXTBOX_MOVE_CARET_FORWARD(textbox);
		} else {
			return;
		}

		if (!word) {
			return;
		} else if (textbox->carets[0] != textbox->bytes && textbox->carets[0] != 0) {
			_UI_TEXTBOX_MOVE_CARET_WORD(textbox);
		}
	}

	UIElementRepaint(&textbox->e, NULL);
}

void _UITextboxCopyText(void *cp) {
	UITextbox *textbox = (UITextbox *) cp;

	int   to = textbox->carets[0] > textbox->carets[1] ? textbox->carets[0] : textbox->carets[1];
	int from = textbox->carets[0] < textbox->carets[1] ? textbox->carets[0] : textbox->carets[1];

	if (from != to) {
		char *pasteText = (char *) UI_CALLOC(to - from + 1);
		for (int i = from; i < to; i++) pasteText[i - from] = textbox->string[i];
		_UIClipboardWriteText(textbox->e.window, pasteText);
	}
}

void _UITextboxPasteText(void *cp) {
	UITextbox *textbox = (UITextbox *) cp;
	size_t bytes;
	char *text = _UIClipboardReadTextStart(textbox->e.window, &bytes);

	if (text) {
		for (size_t i = 0; i < bytes; i++) {
			if (text[i] == '\n') text[i] = ' ';
		}

		UITextboxReplace(textbox, text, bytes, true);
	}

	_UIClipboardReadTextEnd(textbox->e.window, text);
}

int _UITextboxMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UITextbox *textbox = (UITextbox *) element;

	if (message == UI_MSG_GET_HEIGHT) {
		return UI_SIZE_TEXTBOX_HEIGHT * element->window->scale;
	} else if (message == UI_MSG_GET_WIDTH) {
		return UI_SIZE_TEXTBOX_WIDTH * element->window->scale;
	} else if (message == UI_MSG_PAINT) {
		UIDrawControl((UIPainter *) dp, element->bounds, UI_DRAW_CONTROL_TEXTBOX | UI_DRAW_CONTROL_STATE_FROM_ELEMENT(element),
				NULL, 0, 0, element->window->scale);

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

#ifdef __cplusplus
		UIStringSelection selection = {};
#else
		UIStringSelection selection = { 0 };
#endif
		selection.carets[0] = _UITextboxByteToColumn(textbox->string, textbox->carets[0], textbox->bytes);
		selection.carets[1] = _UITextboxByteToColumn(textbox->string, textbox->carets[1], textbox->bytes);
		selection.colorBackground = ui.theme.selected;
		selection.colorText = ui.theme.textSelected;
		textBounds.l -= textbox->scroll;

		UIDrawString((UIPainter *) dp, textBounds, textbox->string, textbox->bytes,
			(element->flags & UI_ELEMENT_DISABLED) ? ui.theme.textDisabled : ui.theme.text, UI_ALIGN_LEFT,
			element->window->focused == element ? &selection : NULL);
	} else if (message == UI_MSG_GET_CURSOR) {
		return UI_CURSOR_TEXT;
	} else if (message == UI_MSG_LEFT_DOWN) {
		int column = (element->window->cursorX - element->bounds.l + textbox->scroll - UI_SIZE_TEXTBOX_MARGIN * element->window->scale
				+ ui.activeFont->glyphWidth / 2) / ui.activeFont->glyphWidth;
		textbox->carets[0] = textbox->carets[1] = column >= textbox->bytes ? textbox->bytes : column <= 0 ? 0 : column;
		UIElementFocus(element);
	} else if (message == UI_MSG_UPDATE) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_DEALLOCATE) {
		UI_FREE(textbox->string);
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;
		bool handled = true;

		if (textbox->rejectNextKey) {
			textbox->rejectNextKey = false;
			handled = false;
		} else if (m->code == UI_KEYCODE_BACKSPACE || m->code == UI_KEYCODE_DELETE) {
			if (textbox->carets[0] == textbox->carets[1]) {
				UITextboxMoveCaret(textbox, m->code == UI_KEYCODE_BACKSPACE, element->window->ctrl);
			}

			UITextboxReplace(textbox, NULL, 0, true);
		} else if (m->code == UI_KEYCODE_LEFT || m->code == UI_KEYCODE_RIGHT) {
			if (textbox->carets[0] == textbox->carets[1] || element->window->shift) {
				UITextboxMoveCaret(textbox, m->code == UI_KEYCODE_LEFT, element->window->ctrl);
				if (!element->window->shift) textbox->carets[1] = textbox->carets[0];
			} else {
				textbox->carets[1 - element->window->shift] = textbox->carets[element->window->shift];
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
		} else if (m->textBytes && !element->window->alt && !element->window->ctrl && m->text[0] >= 0x20) {
			UITextboxReplace(textbox, m->text, m->textBytes, true);
		} else if ((m->code == UI_KEYCODE_LETTER('C') || m->code == UI_KEYCODE_LETTER('X') || m->code == UI_KEYCODE_INSERT)
				&& element->window->ctrl && !element->window->alt && !element->window->shift) {
			_UITextboxCopyText(textbox);

			if (m->code == UI_KEYCODE_LETTER('X')) {
				UITextboxReplace(textbox, NULL, 0, true);
			}
		} else if ((m->code == UI_KEYCODE_LETTER('V') && element->window->ctrl && !element->window->alt && !element->window->shift)
				|| (m->code == UI_KEYCODE_INSERT && !element->window->ctrl && !element->window->alt && element->window->shift)) {
			_UITextboxPasteText(textbox);
		} else {
			handled = false;
		}

		if (handled) {
			UIElementRepaint(element, NULL);
			return 1;
		}
	} else if (message == UI_MSG_RIGHT_DOWN) {
		int c0 = textbox->carets[0], c1 = textbox->carets[1];
		_UITextboxMessage(element, UI_MSG_LEFT_DOWN, di, dp);

		if (c0 < c1 ? (textbox->carets[0] >= c0 && textbox->carets[0] < c1) : (textbox->carets[0] >= c1 && textbox->carets[0] < c0)) {
			textbox->carets[0] = c0, textbox->carets[1] = c1; // Only move caret if clicking outside the existing selection.
		}

		UIMenu *menu = UIMenuCreate(&element->window->e, UI_MENU_NO_SCROLL);
		UIMenuAddItem(menu, textbox->carets[0] == textbox->carets[1] ? UI_ELEMENT_DISABLED : 0, "Copy", -1, _UITextboxCopyText, textbox);
		size_t pasteBytes;
		char *paste = _UIClipboardReadTextStart(textbox->e.window, &pasteBytes);
		UIMenuAddItem(menu, !paste || !pasteBytes ? UI_ELEMENT_DISABLED : 0, "Paste", -1, _UITextboxPasteText, textbox);
		_UIClipboardReadTextEnd(textbox->e.window, paste);
		UIMenuShow(menu);
	}

	return 0;
}

UITextbox *UITextboxCreate(UIElement *parent, uint32_t flags) {
	return (UITextbox *) UIElementCreate(sizeof(UITextbox), parent, flags | UI_ELEMENT_TAB_STOP, _UITextboxMessage, "Textbox");
}

/////////////////////////////////////////
// MDI clients.
/////////////////////////////////////////

int _UIMDIChildHitTest(UIMDIChild *mdiChild, int x, int y) {
	UIElement *element = &mdiChild->e;
	UI_MDI_CHILD_CALCULATE_LAYOUT(element->bounds, element->window->scale);
	int cornerSize = UI_SIZE_MDI_CHILD_CORNER * element->window->scale;
	if (!UIRectangleContains(element->bounds, x, y) || UIRectangleContains(content, x, y)) return -1;
	else if (x < element->bounds.l + cornerSize && y < element->bounds.t + cornerSize) return 0b1010;
	else if (x > element->bounds.r - cornerSize && y < element->bounds.t + cornerSize) return 0b0110;
	else if (x < element->bounds.l + cornerSize && y > element->bounds.b - cornerSize) return 0b1001;
	else if (x > element->bounds.r - cornerSize && y > element->bounds.b - cornerSize) return 0b0101;
	else if (x < element->bounds.l + borderSize) return 0b1000;
	else if (x > element->bounds.r - borderSize) return 0b0100;
	else if (y < element->bounds.t + borderSize) return 0b0010;
	else if (y > element->bounds.b - borderSize) return 0b0001;
	else if (UIRectangleContains(title, x, y)) return 0b1111;
	else return -1;
}

void _UIMDIChildCloseButton(void *_child) {
	UIElement *child = (UIElement *) _child;

	if (!UIElementMessage(child, UI_MSG_WINDOW_CLOSE, 0, 0)) {
		UIElementDestroy(child);
		UIElementRefresh(child->parent);
	}
}

int _UIMDIChildMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIMDIChild *mdiChild = (UIMDIChild *) element;

	if (message == UI_MSG_PAINT) {
		UIDrawControl((UIPainter *) dp, element->bounds, UI_DRAW_CONTROL_MDI_CHILD, mdiChild->title, mdiChild->titleBytes, 0, element->window->scale);
	} else if (message == UI_MSG_GET_WIDTH) {
		UIElement *child = element->childCount ? element->children[element->childCount - 1] : NULL;
		int width = 2 * UI_SIZE_MDI_CHILD_BORDER;
		width += (child ? UIElementMessage(child, message, di ? (di - UI_SIZE_MDI_CHILD_TITLE + UI_SIZE_MDI_CHILD_BORDER) : 0, dp) : 0);
		if (width < UI_SIZE_MDI_CHILD_MINIMUM_WIDTH) width = UI_SIZE_MDI_CHILD_MINIMUM_WIDTH;
		return width;
	} else if (message == UI_MSG_GET_HEIGHT) {
		UIElement *child = element->childCount ? element->children[element->childCount - 1] : NULL;
		int height = UI_SIZE_MDI_CHILD_TITLE + UI_SIZE_MDI_CHILD_BORDER;
		height += (child ? UIElementMessage(child, message, di ? (di - 2 * UI_SIZE_MDI_CHILD_BORDER) : 0, dp) : 0);
		if (height < UI_SIZE_MDI_CHILD_MINIMUM_HEIGHT) height = UI_SIZE_MDI_CHILD_MINIMUM_HEIGHT;
		return height;
	} else if (message == UI_MSG_LAYOUT) {
		UI_MDI_CHILD_CALCULATE_LAYOUT(element->bounds, element->window->scale);

		int position = title.r;

		for (uint32_t i = 0; i < element->childCount - 1; i++) {
			UIElement *child = element->children[i];
			int width = UIElementMessage(child, UI_MSG_GET_WIDTH, 0, 0);
			UIElementMove(child, UI_RECT_4(position - width, position, title.t, title.b), false);
			position -= width;
		}

		UIElement *child = element->childCount ? element->children[element->childCount - 1] : NULL;

		if (child) {
			UIElementMove(child, content, false);
		}
	} else if (message == UI_MSG_GET_CURSOR) {
		int hitTest = _UIMDIChildHitTest(mdiChild, element->window->cursorX, element->window->cursorY);
		if (hitTest == 0b1000) return UI_CURSOR_RESIZE_LEFT;
		if (hitTest == 0b0010) return UI_CURSOR_RESIZE_UP;
		if (hitTest == 0b0110) return UI_CURSOR_RESIZE_UP_RIGHT;
		if (hitTest == 0b1010) return UI_CURSOR_RESIZE_UP_LEFT;
		if (hitTest == 0b0100) return UI_CURSOR_RESIZE_RIGHT;
		if (hitTest == 0b0001) return UI_CURSOR_RESIZE_DOWN;
		if (hitTest == 0b1001) return UI_CURSOR_RESIZE_DOWN_LEFT;
		if (hitTest == 0b0101) return UI_CURSOR_RESIZE_DOWN_RIGHT;
		return UI_CURSOR_ARROW;
	} else if (message == UI_MSG_LEFT_DOWN) {
		mdiChild->dragHitTest = _UIMDIChildHitTest(mdiChild, element->window->cursorX, element->window->cursorY);
		mdiChild->dragOffset = UIRectangleAdd(element->bounds, UI_RECT_2(-element->window->cursorX, -element->window->cursorY));
	} else if (message == UI_MSG_LEFT_UP) {
		if (mdiChild->bounds.l < 0) mdiChild->bounds.r -= mdiChild->bounds.l, mdiChild->bounds.l = 0;
		if (mdiChild->bounds.t < 0) mdiChild->bounds.b -= mdiChild->bounds.t, mdiChild->bounds.t = 0;
		UIElementRefresh(element->parent);
	} else if (message == UI_MSG_MOUSE_DRAG) {
		if (mdiChild->dragHitTest > 0) {
#define _UI_MDI_CHILD_MOVE_EDGE(bit, edge, cursor, size, opposite, negate, minimum, offset) \
	if (mdiChild->dragHitTest & bit) mdiChild->bounds.edge = mdiChild->dragOffset.edge + element->window->cursor - element->parent->bounds.offset; \
	if ((mdiChild->dragHitTest & bit) && size(mdiChild->bounds) < minimum) mdiChild->bounds.edge = mdiChild->bounds.opposite negate minimum;
			_UI_MDI_CHILD_MOVE_EDGE(0b1000, l, cursorX, UI_RECT_WIDTH, r, -, UI_SIZE_MDI_CHILD_MINIMUM_WIDTH, l);
			_UI_MDI_CHILD_MOVE_EDGE(0b0100, r, cursorX, UI_RECT_WIDTH, l, +, UI_SIZE_MDI_CHILD_MINIMUM_WIDTH, l);
			_UI_MDI_CHILD_MOVE_EDGE(0b0010, t, cursorY, UI_RECT_HEIGHT, b, -, UI_SIZE_MDI_CHILD_MINIMUM_HEIGHT, t);
			_UI_MDI_CHILD_MOVE_EDGE(0b0001, b, cursorY, UI_RECT_HEIGHT, t, +, UI_SIZE_MDI_CHILD_MINIMUM_HEIGHT, t);
			UIElementRefresh(element->parent);
		}
	} else if (message == UI_MSG_DESTROY) {
		UIMDIClient *client = (UIMDIClient *) element->parent;

		if (client->active == mdiChild) {
			client->active = (UIMDIChild *) (client->e.childCount == 1 ? NULL : client->e.children[client->e.childCount - 2]);
		}
	} else if (message == UI_MSG_DEALLOCATE) {
		UI_FREE(mdiChild->title);
	}

	return 0;
}

int _UIMDIClientMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIMDIClient *client = (UIMDIClient *) element;

	if (message == UI_MSG_PAINT) {
		if (~element->flags & UI_MDI_CLIENT_TRANSPARENT) {
			UIDrawBlock((UIPainter *) dp, element->bounds, ui.theme.panel2);
		}
	} else if (message == UI_MSG_LAYOUT) {
		for (uint32_t i = 0; i < element->childCount; i++) {
			UIMDIChild *mdiChild = (UIMDIChild *) element->children[i];
			UI_ASSERT(mdiChild->e.messageClass == _UIMDIChildMessage);

			if (UIRectangleEquals(mdiChild->bounds, UI_RECT_1(0))) {
				int width = UIElementMessage(&mdiChild->e, UI_MSG_GET_WIDTH, 0, 0);
				int height = UIElementMessage(&mdiChild->e, UI_MSG_GET_HEIGHT, width, 0);
				if (client->cascade + width > element->bounds.r || client->cascade + height > element->bounds.b) client->cascade = 0;
				mdiChild->bounds = UI_RECT_4(client->cascade, client->cascade + width, client->cascade, client->cascade + height);
				client->cascade += UI_SIZE_MDI_CASCADE * element->window->scale;
			}

			UIRectangle bounds = UIRectangleAdd(mdiChild->bounds, UI_RECT_2(element->bounds.l, element->bounds.t));
			UIElementMove(&mdiChild->e, bounds, false);
		}
	} else if (message == UI_MSG_PRESSED_DESCENDENT) {
		UIMDIChild *child = (UIMDIChild *) dp;

		if (child && child != client->active) {
			for (uint32_t i = 0; i < element->childCount; i++) {
				if (element->children[i] == &child->e) {
					UI_MEMMOVE(&element->children[i], &element->children[i + 1], sizeof(UIElement *) * (element->childCount - i - 1));
					element->children[element->childCount - 1] = &child->e;
					break;
				}
			}

			client->active = child;
			UIElementRefresh(element);
		}
	}

	return 0;
}

UIMDIChild *UIMDIChildCreate(UIElement *parent, uint32_t flags, UIRectangle initialBounds, const char *title, ptrdiff_t titleBytes) {
	UI_ASSERT(parent->messageClass == _UIMDIClientMessage);

	UIMDIChild *mdiChild = (UIMDIChild *) UIElementCreate(sizeof(UIMDIChild), parent, flags, _UIMDIChildMessage, "MDIChild");
	UIMDIClient *mdiClient = (UIMDIClient *) parent;

	mdiChild->bounds = initialBounds;
	mdiChild->title = UIStringCopy(title, (mdiChild->titleBytes = titleBytes));
	mdiClient->active = mdiChild;

	if (flags & UI_MDI_CHILD_CLOSE_BUTTON) {
		UIButton *closeButton = UIButtonCreate(&mdiChild->e, UI_BUTTON_SMALL | UI_ELEMENT_NON_CLIENT, "X", 1);
		closeButton->invoke = _UIMDIChildCloseButton;
		closeButton->e.cp = mdiChild;
	}

	return mdiChild;
}

UIMDIClient *UIMDIClientCreate(UIElement *parent, uint32_t flags) {
	return (UIMDIClient *) UIElementCreate(sizeof(UIMDIClient), parent, flags, _UIMDIClientMessage, "MDIClient");
}

/////////////////////////////////////////
// Image displays.
/////////////////////////////////////////

void _UIImageDisplayUpdateViewport(UIImageDisplay *display) {
	UIRectangle bounds = display->e.bounds;
	bounds.r -= bounds.l, bounds.b -= bounds.t;

	float minimumZoomX = 1, minimumZoomY = 1;
	if (display->width  > bounds.r) minimumZoomX = (float) bounds.r / display->width;
	if (display->height > bounds.b) minimumZoomY = (float) bounds.b / display->height;
	float minimumZoom = minimumZoomX < minimumZoomY ? minimumZoomX : minimumZoomY;

	if (display->zoom < minimumZoom || (display->e.flags & _UI_IMAGE_DISPLAY_ZOOM_FIT)) {
		display->zoom = minimumZoom;
		display->e.flags |= _UI_IMAGE_DISPLAY_ZOOM_FIT;
	}

	if (display->panX < 0) display->panX = 0;
	if (display->panY < 0) display->panY = 0;
	if (display->panX > display->width  - bounds.r / display->zoom) display->panX = display->width  - bounds.r / display->zoom;
	if (display->panY > display->height - bounds.b / display->zoom) display->panY = display->height - bounds.b / display->zoom;

	if (bounds.r && display->width  * display->zoom <= bounds.r) display->panX = display->width  / 2 - bounds.r / display->zoom / 2;
	if (bounds.b && display->height * display->zoom <= bounds.b) display->panY = display->height / 2 - bounds.b / display->zoom / 2;
}

int _UIImageDisplayMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIImageDisplay *display = (UIImageDisplay *) element;

	if (message == UI_MSG_GET_HEIGHT) {
		return display->height;
	} else if (message == UI_MSG_GET_WIDTH) {
		return display->width;
	} else if (message == UI_MSG_DEALLOCATE) {
		UI_FREE(display->bits);
	} else if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;

		int w = UI_RECT_WIDTH(element->bounds), h = UI_RECT_HEIGHT(element->bounds);
		int x = _UILinearMap(0, display->panX, display->panX + w / display->zoom, 0, w) + element->bounds.l;
		int y = _UILinearMap(0, display->panY, display->panY + h / display->zoom, 0, h) + element->bounds.t;

		UIRectangle image = UI_RECT_4(x, x + (int) (display->width * display->zoom), y, (int) (y + display->height * display->zoom));
		UIRectangle bounds = UIRectangleIntersection(painter->clip, UIRectangleIntersection(display->e.bounds, image));
		if (!UI_RECT_VALID(bounds)) return 0;

		if (display->zoom == 1) {
			uint32_t *lineStart = (uint32_t *) painter->bits + bounds.t * painter->width + bounds.l;
			uint32_t *sourceLineStart = display->bits + (bounds.l - image.l) + display->width * (bounds.t - image.t);

			for (int i = 0; i < bounds.b - bounds.t; i++, lineStart += painter->width, sourceLineStart += display->width) {
				uint32_t *destination = lineStart;
				uint32_t *source = sourceLineStart;
				int j = bounds.r - bounds.l;

				do {
					*destination = *source;
					destination++;
					source++;
				} while (--j);
			}
		} else {
			float zr = 1.0f / display->zoom;
			uint32_t *destination = (uint32_t *) painter->bits;

			for (int i = bounds.t; i < bounds.b; i++) {
				int ty = (i - image.t) * zr;

				for (int j = bounds.l; j < bounds.r; j++) {
					int tx = (j - image.l) * zr;
					destination[i * painter->width + j] = display->bits[ty * display->width + tx];
				}
			}
		}
	} else if (message == UI_MSG_MOUSE_WHEEL && (element->flags & UI_IMAGE_DISPLAY_INTERACTIVE)) {
		display->e.flags &= ~_UI_IMAGE_DISPLAY_ZOOM_FIT;
		int divisions = -di / 72;
		float factor = 1;
		float perDivision = element->window->ctrl ? 2.0f : element->window->alt ? 1.01f : 1.2f;
		while (divisions > 0) factor *= perDivision, divisions--;
		while (divisions < 0) factor /= perDivision, divisions++;
		if (display->zoom * factor > 64) factor = 64 / display->zoom;
		int mx = element->window->cursorX - element->bounds.l;
		int my = element->window->cursorY - element->bounds.t;
		display->zoom *= factor;
		display->panX -= mx / display->zoom * (1 - factor);
		display->panY -= my / display->zoom * (1 - factor);
		_UIImageDisplayUpdateViewport(display);
		UIElementRepaint(&display->e, NULL);
	} else if (message == UI_MSG_LAYOUT && (element->flags & UI_IMAGE_DISPLAY_INTERACTIVE)) {
		UIRectangle bounds = display->e.bounds;
		bounds.r -= bounds.l, bounds.b -= bounds.t;
		display->panX -= (bounds.r - display->previousWidth ) / 2 / display->zoom;
		display->panY -= (bounds.b - display->previousHeight) / 2 / display->zoom;
		display->previousWidth = bounds.r, display->previousHeight = bounds.b;
		_UIImageDisplayUpdateViewport(display);
	} else if (message == UI_MSG_GET_CURSOR && (element->flags & UI_IMAGE_DISPLAY_INTERACTIVE)
			&& (UI_RECT_WIDTH(element->bounds) < display->width * display->zoom
				|| UI_RECT_HEIGHT(element->bounds) < display->height * display->zoom)) {
		return UI_CURSOR_HAND;
	} else if (message == UI_MSG_MOUSE_DRAG) {
		display->panX -= (element->window->cursorX - display->previousPanPointX) / display->zoom;
		display->panY -= (element->window->cursorY - display->previousPanPointY) / display->zoom;
		_UIImageDisplayUpdateViewport(display);
		display->previousPanPointX = element->window->cursorX;
		display->previousPanPointY = element->window->cursorY;
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_LEFT_DOWN) {
		display->e.flags &= ~_UI_IMAGE_DISPLAY_ZOOM_FIT;
		display->previousPanPointX = element->window->cursorX;
		display->previousPanPointY = element->window->cursorY;
	}

	return 0;
}

void UIImageDisplaySetContent(UIImageDisplay *display, uint32_t *bits, size_t width, size_t height, size_t stride) {
	UI_FREE(display->bits);

	display->bits = (uint32_t *) UI_MALLOC(width * height * 4);
	display->width = width;
	display->height = height;

	uint32_t *destination = display->bits;
	uint32_t *source = bits;

	for (uintptr_t row = 0; row < height; row++, source += stride / 4) {
		for (uintptr_t i = 0; i < width; i++) {
			*destination++ = source[i];
		}
	}

	UIElementMeasurementsChanged(&display->e, 3);
	UIElementRepaint(&display->e, NULL);
}

UIImageDisplay *UIImageDisplayCreate(UIElement *parent, uint32_t flags, uint32_t *bits, size_t width, size_t height, size_t stride) {
	UIImageDisplay *display = (UIImageDisplay *) UIElementCreate(sizeof(UIImageDisplay), parent, flags, _UIImageDisplayMessage, "ImageDisplay");
	display->zoom = 1.0f;
	UIImageDisplaySetContent(display, bits, width, height, stride);
	return display;
}

/////////////////////////////////////////
// Modal dialogs.
/////////////////////////////////////////

int _UIDialogWrapperMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_LAYOUT) {
		int width = UIElementMessage(element->children[0], UI_MSG_GET_WIDTH, 0, 0);
		int height = UIElementMessage(element->children[0], UI_MSG_GET_HEIGHT, width, 0);
		int cx = (element->bounds.l + element->bounds.r) / 2;
		int cy = (element->bounds.t + element->bounds.b) / 2;
		UIRectangle bounds = UI_RECT_4(cx - (width + 1) / 2, cx + width / 2, cy - (height + 1) / 2, cy + height / 2);
		UIElementMove(element->children[0], bounds, false);
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_PAINT) {
		UIDrawControl((UIPainter *) dp, element->children[0]->bounds, UI_DRAW_CONTROL_MODAL_POPUP, NULL, 0, 0, element->window->scale);
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *typed = (UIKeyTyped *) dp;

		if (element->window->ctrl) return 0;
		if (element->window->shift) return 0;

		if (!ui.dialogCanExit) {
		} else if (!element->window->alt && typed->code == UI_KEYCODE_ESCAPE) {
			ui.dialogResult = "__C";
			return 1;
		} else if (!element->window->alt && typed->code == UI_KEYCODE_ENTER) {
			ui.dialogResult = "__D";
			return 1;
		}

		char c0 = 0, c1 = 0;

		if (typed->textBytes == 1 && typed->text[0] >= 'a' && typed->text[0] <= 'z') {
			c0 = typed->text[0], c1 = typed->text[0] - 'a' + 'A';
		} else {
			return 0;
		}

		UIElement *rowContainer = element->children[0];
		UIElement *target = NULL;
		bool duplicate = false;

		for (uint32_t i = 0; i < rowContainer->childCount; i++) {
			for (uint32_t j = 0; j < rowContainer->children[i]->childCount; j++) {
				UIElement *item = rowContainer->children[i]->children[j];

				if (item->messageClass == _UIButtonMessage) {
					UIButton *button = (UIButton *) item;

					if (button->label && button->labelBytes && (button->label[0] == c0 || button->label[0] == c1)) {
						if (!target) {
							target = &button->e;
						} else {
							duplicate = true;
						}
					}
				}
			}
		}

		if (target) {
			if (duplicate) {
				UIElementFocus(target);
			} else {
				UIElementMessage(target, UI_MSG_CLICKED, 0, 0);
			}

			return 1;
		}
	}

	return 0;
}

void _UIDialogButtonInvoke(void *cp) {
	ui.dialogResult = (const char *) cp;
}

int _UIDialogDefaultButtonMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_PAINT && element->window->focused->messageClass != _UIButtonMessage) {
		element->flags |= UI_BUTTON_CHECKED;
		element->messageClass(element, message, di, dp);
		element->flags &= ~UI_BUTTON_CHECKED;
		return 1;
	}

	return 0;
}

int _UIDialogTextboxMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UITextbox *textbox = (UITextbox *) element;

	if (message == UI_MSG_VALUE_CHANGED) {
		char **buffer = (char **) element->cp;
		*buffer = (char *) UI_REALLOC(*buffer, textbox->bytes + 1);
		(*buffer)[textbox->bytes] = 0;

		for (ptrdiff_t i = 0; i < textbox->bytes; i++) {
			(*buffer)[i] = textbox->string[i];
		}
	} else if (message == UI_MSG_UPDATE && di == UI_UPDATE_FOCUSED && element->window->focused == element) {
		textbox->carets[1] = 0;
		textbox->carets[0] = textbox->bytes;
		UIElementRepaint(element, NULL);
	}

	return 0;
}

const char *UIDialogShow(UIWindow *window, uint32_t flags, const char *format, ...) {
	// Create the dialog wrapper and panel.

	UI_ASSERT(!window->dialog);
	window->dialog = UIElementCreate(sizeof(UIElement), &window->e, 0, _UIDialogWrapperMessage, "DialogWrapper");
	UIPanel *panel = UIPanelCreate(window->dialog, UI_PANEL_MEDIUM_SPACING | UI_PANEL_COLOR_1);
	panel->border = UI_RECT_1(UI_SIZE_PANE_MEDIUM_BORDER * 2);
	window->e.children[0]->flags |= UI_ELEMENT_DISABLED;

	// Create the dialog contents.

	va_list arguments;
	va_start(arguments, format);
	UIPanel *row = NULL;
	UIElement *focus = NULL;
	UIButton *defaultButton = NULL;
	UIButton *cancelButton = NULL;
	uint32_t buttonCount = 0;

	for (int i = 0; format[i]; i++) {
		if (i == 0 || format[i - 1] == '\n') {
			row = UIPanelCreate(&panel->e, UI_PANEL_HORIZONTAL | UI_ELEMENT_H_FILL);
			row->gap = UI_SIZE_PANE_SMALL_GAP;
		}

		if (format[i] == ' ' || format[i] == '\n') {
		} else if (format[i] == '%') {
			i++;

			if (format[i] == 'b' /* button */ || format[i] == 'B' /* default button */ || format[i] == 'C' /* cancel button */) {
				const char *label = va_arg(arguments, const char *);
				UIButton *button = UIButtonCreate(&row->e, 0, label, -1);
				if (!focus) focus = &button->e;
				if (format[i] == 'B') defaultButton = button;
				if (format[i] == 'C') cancelButton = button;
				buttonCount++;
				button->invoke = _UIDialogButtonInvoke;
				if (format[i] == 'B') button->e.messageUser = _UIDialogDefaultButtonMessage;
				button->e.cp = (void *) label;
			} else if (format[i] == 's' /* label from string */) {
				const char *label = va_arg(arguments, const char *);
				UILabelCreate(&row->e, 0, label, -1);
			} else if (format[i] == 't' /* textbox */) {
				char **buffer = va_arg(arguments, char **);
				UITextbox *textbox = UITextboxCreate(&row->e, UI_ELEMENT_H_FILL);
				if (!focus) focus = &textbox->e;
				if (*buffer) UITextboxReplace(textbox, *buffer, _UIStringLength(*buffer), false);
				textbox->e.cp = buffer;
				textbox->e.messageUser = _UIDialogTextboxMessage;
			} else if (format[i] == 'f' /* horizontal fill */) {
				UISpacerCreate(&row->e, UI_ELEMENT_H_FILL, 0, 0);
			} else if (format[i] == 'l' /* horizontal line */) {
				UISpacerCreate(&row->e, UI_ELEMENT_BORDER | UI_ELEMENT_H_FILL, 0, 1);
			} else if (format[i] == 'u' /* user */) {
				UIDialogUserCallback callback = va_arg(arguments, UIDialogUserCallback);
				callback(&row->e);
			}
		} else {
			int j = i;
			while (format[j] && format[j] != '%' && format[j] != '\n') j++;
			UILabelCreate(&row->e, 0, format + i, j - i);
			i = j - 1;
		}
	}

	va_end(arguments);

	window->dialogOldFocus = window->focused;
	UIElementFocus(focus ? focus : window->dialog);

	// Run the modal message loop.

	int result;
	ui.dialogResult = NULL;
	ui.dialogCanExit = buttonCount != 0;
	for (int i = 1; i <= 3; i++) _UIWindowSetPressed(window, NULL, i);
	UIElementRefresh(&window->e);
	_UIUpdate();
	while (!ui.dialogResult && _UIMessageLoopSingle(&result));
	ui.quit = !ui.dialogResult;

	// Check for cancel/default action.

	if (buttonCount == 1 && defaultButton && !cancelButton) {
		cancelButton = defaultButton;
	}

	if (!ui.dialogResult) {
	} else if (ui.dialogResult[0] == '_' && ui.dialogResult[1] == '_' && ui.dialogResult[2] == 'C' && ui.dialogResult[3] == 0 && cancelButton) {
		ui.dialogResult = (const char *) cancelButton->e.cp;
	} else if (ui.dialogResult[0] == '_' && ui.dialogResult[1] == '_' && ui.dialogResult[2] == 'D' && ui.dialogResult[3] == 0 && defaultButton) {
		ui.dialogResult = (const char *) defaultButton->e.cp;
	}

	// Destroy the dialog.

	window->e.children[0]->flags &= ~UI_ELEMENT_DISABLED;
	UIElementDestroy(window->dialog);
	window->dialog = NULL;
	UIElementRefresh(&window->e);
	if (window->dialogOldFocus) UIElementFocus(window->dialogOldFocus);
	return ui.dialogResult ? ui.dialogResult : "";
}

/////////////////////////////////////////
// Menus (common).
/////////////////////////////////////////

bool _UIMenusClose() {
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

#if !defined(UI_ESSENCE) && !defined(UI_COCOA)
int _UIMenuItemMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		_UIMenusClose();
	}

	return 0;
}

int _UIMenuMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIMenu *menu = (UIMenu *) element;

	if (message == UI_MSG_GET_WIDTH) {
		int width = 0;

		for (uint32_t i = 0; i < element->childCount; i++) {
			UIElement *child = element->children[i];

			if (~child->flags & UI_ELEMENT_NON_CLIENT) {
				int w = UIElementMessage(child, UI_MSG_GET_WIDTH, 0, 0);
				if (w > width) width = w;
			}
		}

		return width + 4 + UI_SIZE_SCROLL_BAR;
	} else if (message == UI_MSG_GET_HEIGHT) {
		int height = 0;

		for (uint32_t i = 0; i < element->childCount; i++) {
			UIElement *child = element->children[i];

			if (~child->flags & UI_ELEMENT_NON_CLIENT) {
				height += UIElementMessage(child, UI_MSG_GET_HEIGHT, 0, 0);
			}
		}

		return height + 4;
	} else if (message == UI_MSG_PAINT) {
		UIDrawControl((UIPainter *) dp, element->bounds, UI_DRAW_CONTROL_MENU, NULL, 0, 0, element->window->scale);
	} else if (message == UI_MSG_LAYOUT) {
		int position = element->bounds.t + 2 - menu->vScroll->position;
		int totalHeight = 0;
		int scrollBarSize = (menu->e.flags & UI_MENU_NO_SCROLL) ? 0 : UI_SIZE_SCROLL_BAR;

		for (uint32_t i = 0; i < element->childCount; i++) {
			UIElement *child = element->children[i];

			if (~child->flags & UI_ELEMENT_NON_CLIENT) {
				int height = UIElementMessage(child, UI_MSG_GET_HEIGHT, 0, 0);
				UIElementMove(child, UI_RECT_4(element->bounds.l + 2, element->bounds.r - scrollBarSize - 2,
							position, position + height), false);
				position += height;
				totalHeight += height;
			}
		}

		UIRectangle scrollBarBounds = element->bounds;
		scrollBarBounds.l = scrollBarBounds.r - scrollBarSize * element->window->scale;
		menu->vScroll->maximum = totalHeight;
		menu->vScroll->page = UI_RECT_HEIGHT(element->bounds);
		UIElementMove(&menu->vScroll->e, scrollBarBounds, true);
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		if (m->code == UI_KEYCODE_ESCAPE) {
			_UIMenusClose();
			return 1;
		}
	} else if (message == UI_MSG_MOUSE_WHEEL) {
		return UIElementMessage(&menu->vScroll->e, message, di, dp);
	} else if (message == UI_MSG_SCROLLED) {
		UIElementRefresh(element);
	}

	return 0;
}

void UIMenuAddItem(UIMenu *menu, uint32_t flags, const char *label, ptrdiff_t labelBytes, void (*invoke)(void *cp), void *cp) {
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

UIMenu *UIMenuCreate(UIElement *parent, uint32_t flags) {
	UIWindow *window = UIWindowCreate(parent->window, UI_WINDOW_MENU, 0, 0, 0);
	UIMenu *menu = (UIMenu *) UIElementCreate(sizeof(UIMenu), &window->e, flags, _UIMenuMessage, "Menu");
	menu->vScroll = UIScrollBarCreate(&menu->e, UI_ELEMENT_NON_CLIENT);
	menu->parentWindow = parent->window;

	if (parent->parent) {
		UIRectangle screenBounds = UIElementScreenBounds(parent);
		menu->pointX = screenBounds.l;
		menu->pointY = (flags & UI_MENU_PLACE_ABOVE) ? (screenBounds.t + 1) : (screenBounds.b - 1);
	} else {
		int x = 0, y = 0;
		_UIWindowGetScreenPosition(parent->window, &x, &y);

		menu->pointX = parent->window->cursorX + x;
		menu->pointY = parent->window->cursorY + y;
	}

	return menu;
}
#endif

/////////////////////////////////////////
// Miscellaneous core functions.
/////////////////////////////////////////

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

void UIElementSetDisabled(UIElement *element, bool disabled) {
	if (element->window->focused == element && disabled) {
		UIElementFocus(&element->window->e);
	}

	if ((element->flags & UI_ELEMENT_DISABLED) && disabled) return;
	if ((~element->flags & UI_ELEMENT_DISABLED) && !disabled) return;

	if (disabled) element->flags |= UI_ELEMENT_DISABLED;
	else element->flags &= ~UI_ELEMENT_DISABLED;

	UIElementMessage(element, UI_MSG_UPDATE, UI_UPDATE_DISABLED, 0);
}

void UIElementFocus(UIElement *element) {
	UIElement *previous = element->window->focused;
	if (previous == element) return;
	element->window->focused = element;
	if (previous) UIElementMessage(previous, UI_MSG_UPDATE, UI_UPDATE_FOCUSED, 0);
	if (element) UIElementMessage(element, UI_MSG_UPDATE, UI_UPDATE_FOCUSED, 0);

#ifdef UI_DEBUG
	_UIInspectorRefresh();
#endif
}

/////////////////////////////////////////
// Update cycles.
/////////////////////////////////////////

void UIElementRefresh(UIElement *element) {
	UIElementRelayout(element);
	UIElementRepaint(element, NULL);
}

void UIElementRelayout(UIElement *element) {
	if (element->flags & UI_ELEMENT_RELAYOUT) {
		return;
	}

	element->flags |= UI_ELEMENT_RELAYOUT;
	UIElement *ancestor = element->parent;

	while (ancestor) {
		ancestor->flags |= UI_ELEMENT_RELAYOUT_DESCENDENT;
		ancestor = ancestor->parent;
	}
}

void UIElementMeasurementsChanged(UIElement *element, int which) {
	if (!element->parent) {
		return; // This is the window element.
	}

	while (true) {
		if (element->parent->flags & UI_ELEMENT_DESTROY) return;
		which &= ~UIElementMessage(element->parent, UI_MSG_GET_CHILD_STABILITY, which, element);
		if (!which) break;
		element->flags |= UI_ELEMENT_RELAYOUT;
		element = element->parent;
	}

	UIElementRelayout(element);
}

void UIElementRepaint(UIElement *element, UIRectangle *region) {
	if (!region) {
		region = &element->bounds;
	}

	UIRectangle r = UIRectangleIntersection(*region, element->clip);

	if (!UI_RECT_VALID(r)) {
		return;
	}

	if (UI_RECT_VALID(element->window->updateRegion)) {
		element->window->updateRegion = UIRectangleBounding(element->window->updateRegion, r);
	} else {
		element->window->updateRegion = r;
	}
}

void UIElementMove(UIElement *element, UIRectangle bounds, bool layout) {
	UIRectangle clip = element->parent? UIRectangleIntersection(element->parent->clip, bounds) : bounds;
	bool moved = !UIRectangleEquals(element->bounds, bounds) || !UIRectangleEquals(element->clip, clip);

	if (moved) {
		layout = true;

		UIElementRepaint(&element->window->e, &element->clip);
		UIElementRepaint(&element->window->e, &clip);

		element->bounds = bounds;
		element->clip = clip;
	}

	if (element->flags & UI_ELEMENT_RELAYOUT) {
		layout = true;
	}

	if (layout) {
		UIElementMessage(element, UI_MSG_LAYOUT, 0, 0);
	} else if (element->flags & UI_ELEMENT_RELAYOUT_DESCENDENT) {
		for (uint32_t i = 0; i < element->childCount; i++) {
			UIElementMove(element->children[i], element->children[i]->bounds, false);
		}
	}

	element->flags &= ~(UI_ELEMENT_RELAYOUT_DESCENDENT | UI_ELEMENT_RELAYOUT);
}

void _UIElementPaint(UIElement *element, UIPainter *painter) {
	if (element->flags & UI_ELEMENT_HIDE) {
		return;
	}

	// Clip painting to the element's clip.

	painter->clip = UIRectangleIntersection(element->clip, painter->clip);

	if (!UI_RECT_VALID(painter->clip)) {
		return;
	}

	// Paint the element.

	UIElementMessage(element, UI_MSG_PAINT, 0, painter);

	// Paint its children.

	UIRectangle previousClip = painter->clip;

	for (uintptr_t i = 0; i < element->childCount; i++) {
		painter->clip = previousClip;
		_UIElementPaint(element->children[i], painter);
	}

	// Draw the foreground and border.

	painter->clip = previousClip;
	UIElementMessage(element, UI_MSG_PAINT_FOREGROUND, 0, painter);

	if (element->flags & UI_ELEMENT_BORDER) {
		UIDrawBorder(painter, element->bounds, ui.theme.border, UI_RECT_1((int) element->window->scale));
	}
}

bool _UIDestroy(UIElement *element) {
	if (element->flags & UI_ELEMENT_DESTROY_DESCENDENT) {
		element->flags &= ~UI_ELEMENT_DESTROY_DESCENDENT;

		for (uintptr_t i = 0; i < element->childCount; i++) {
			if (_UIDestroy(element->children[i])) {
				UI_MEMMOVE(&element->children[i], &element->children[i + 1], sizeof(UIElement *) * (element->childCount - i - 1));
				element->childCount--, i--;
			}
		}
	}

	if (element->flags & UI_ELEMENT_DESTROY) {
		UIElementMessage(element, UI_MSG_DEALLOCATE, 0, 0);

		if (element->window->pressed == element) {
			_UIWindowSetPressed(element->window, NULL, 0);
		}

		if (element->window->hovered == element) {
			element->window->hovered = &element->window->e;
		}

		if (element->window->focused == element) {
			element->window->focused = NULL;
		}

		if (element->window->dialogOldFocus == element) {
			element->window->dialogOldFocus = NULL;
		}

		UIElementAnimate(element, true);
		UI_FREE(element->children);
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

		UIElementMessage(&window->e, UI_MSG_WINDOW_UPDATE_START, 0, 0);
		UIElementMessage(&window->e, UI_MSG_WINDOW_UPDATE_BEFORE_DESTROY, 0, 0);

		if (_UIDestroy(&window->e)) {
			*link = next;
		} else {
			link = &window->next;

			UIElementMessage(&window->e, UI_MSG_WINDOW_UPDATE_BEFORE_LAYOUT, 0, 0);
			UIElementMove(&window->e, window->e.bounds, false);
			UIElementMessage(&window->e, UI_MSG_WINDOW_UPDATE_BEFORE_PAINT, 0, 0);

			if (UI_RECT_VALID(window->updateRegion)) {
#ifdef __cplusplus
				UIPainter painter = {};
#else
				UIPainter painter = { 0 };
#endif
				painter.bits = window->bits;
				painter.width = window->width;
				painter.height = window->height;
				painter.clip = UIRectangleIntersection(UI_RECT_2S(window->width, window->height), window->updateRegion);
				_UIElementPaint(&window->e, &painter);
				_UIWindowEndPaint(window, &painter);
				window->updateRegion = UI_RECT_1(0);

#ifdef UI_DEBUG
				window->lastFullFillCount = (float) painter.fillCount / (UI_RECT_WIDTH(window->updateRegion) * UI_RECT_HEIGHT(window->updateRegion));
#endif
			}

			UIElementMessage(&window->e, UI_MSG_WINDOW_UPDATE_END, 0, 0);
		}

		window = next;
	}
}

/////////////////////////////////////////
// Input event handling.
/////////////////////////////////////////

void _UIWindowSetPressed(UIWindow *window, UIElement *element, int button) {
	UIElement *previous = window->pressed;
	window->pressed = element;
	window->pressedButton = button;
	if (previous) UIElementMessage(previous, UI_MSG_UPDATE, UI_UPDATE_PRESSED, 0);
	if (element) UIElementMessage(element, UI_MSG_UPDATE, UI_UPDATE_PRESSED, 0);

	UIElement *ancestor = element;
	UIElement *child = NULL;

	while (ancestor) {
		UIElementMessage(ancestor, UI_MSG_PRESSED_DESCENDENT, 0, child);
		child = ancestor;
		ancestor = ancestor->parent;
	}
}

UIElement *UIElementFindByPoint(UIElement *element, int x, int y) {
	for (uint32_t i = element->childCount; i > 0; i--) {
		UIElement *child = element->children[i - 1];

		if ((~child->flags & UI_ELEMENT_HIDE) && UIRectangleContains(child->clip, x, y)) {
			return UIElementFindByPoint(child, x, y);
		}
	}

	return element;
}

bool UIMenusOpen() {
	UIWindow *window = ui.windows;

	while (window) {
		if (window->e.flags & UI_WINDOW_MENU) {
			return true;
		}

		window = window->next;
	}

	return false;
}

UIElement *_UIElementNextOrPreviousSibling(UIElement *element, bool previous) {
	if (!element->parent) {
		return NULL;
	}

	for (uint32_t i = 0; i < element->parent->childCount; i++) {
		if (element->parent->children[i] == element) {
			if (previous) {
				return i > 0 ? element->parent->children[i - 1] : NULL;
			} else {
				return i < element->parent->childCount - 1 ? element->parent->children[i + 1] : NULL;
			}
		}
	}

	UI_ASSERT(false);
	return NULL;
}

bool _UIWindowInputEvent(UIWindow *window, UIMessage message, int di, void *dp) {
	bool handled = true;

	if (window->pressed) {
		if (message == UI_MSG_MOUSE_MOVE) {
			UIElementMessage(window->pressed, UI_MSG_MOUSE_DRAG, di, dp);
		} else if (message == UI_MSG_LEFT_UP && window->pressedButton == 1) {
			if (window->hovered == window->pressed) {
				UIElementMessage(window->pressed, UI_MSG_CLICKED, di, dp);
				if (ui.quit || ui.dialogResult) goto end;
			}

			if (window->pressed) {
				UIElementMessage(window->pressed, UI_MSG_LEFT_UP, di, dp);
				if (ui.quit || ui.dialogResult) goto end;
				_UIWindowSetPressed(window, NULL, 1);
			}
		} else if (message == UI_MSG_MIDDLE_UP && window->pressedButton == 2) {
			UIElementMessage(window->pressed, UI_MSG_MIDDLE_UP, di, dp);
			if (ui.quit || ui.dialogResult) goto end;
			_UIWindowSetPressed(window, NULL, 2);
		} else if (message == UI_MSG_RIGHT_UP && window->pressedButton == 3) {
			UIElementMessage(window->pressed, UI_MSG_RIGHT_UP, di, dp);
			if (ui.quit || ui.dialogResult) goto end;
			_UIWindowSetPressed(window, NULL, 3);
		}
	}

	if (window->pressed) {
		bool inside = UIRectangleContains(window->pressed->clip, window->cursorX, window->cursorY);

		if (inside && window->hovered == &window->e) {
			window->hovered = window->pressed;
			UIElementMessage(window->pressed, UI_MSG_UPDATE, UI_UPDATE_HOVERED, 0);
		} else if (!inside && window->hovered == window->pressed) {
			window->hovered = &window->e;
			UIElementMessage(window->pressed, UI_MSG_UPDATE, UI_UPDATE_HOVERED, 0);
		}

		if (ui.quit || ui.dialogResult) goto end;
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
			if ((window->e.flags & UI_WINDOW_MENU) || !_UIMenusClose()) {
				_UIWindowSetPressed(window, hovered, 1);
				UIElementMessage(hovered, UI_MSG_LEFT_DOWN, di, dp);
			}
		} else if (message == UI_MSG_MIDDLE_DOWN) {
			if ((window->e.flags & UI_WINDOW_MENU) || !_UIMenusClose()) {
				_UIWindowSetPressed(window, hovered, 2);
				UIElementMessage(hovered, UI_MSG_MIDDLE_DOWN, di, dp);
			}
		} else if (message == UI_MSG_RIGHT_DOWN) {
			if ((window->e.flags & UI_WINDOW_MENU) || !_UIMenusClose()) {
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
		} else if (message == UI_MSG_KEY_TYPED || message == UI_MSG_KEY_RELEASED) {
			handled = false;

			if (window->focused) {
				UIElement *element = window->focused;

				while (element) {
					if (UIElementMessage(element, message, di, dp)) {
						handled = true;
						break;
					}

					element = element->parent;
				}
			} else {
				if (UIElementMessage(&window->e, message, di, dp)) {
					handled = true;
				}
			}

			if (!handled && !UIMenusOpen() && message == UI_MSG_KEY_TYPED) {
				UIKeyTyped *m = (UIKeyTyped *) dp;

				if (m->code == UI_KEYCODE_TAB && !window->ctrl && !window->alt) {
					UIElement *start = window->focused ? window->focused : &window->e;
					UIElement *element = start;

					do {
						if (element->childCount && !(element->flags & (UI_ELEMENT_HIDE | UI_ELEMENT_DISABLED))) {
							element = window->shift ? element->children[element->childCount - 1] : element->children[0];
							continue;
						}

						while (element) {
							UIElement *sibling = _UIElementNextOrPreviousSibling(element, window->shift);
							if (sibling) { element = sibling; break; }
							element = element->parent;
						}

						if (!element) {
							element = &window->e;
						}
					} while (element != start && ((~element->flags & UI_ELEMENT_TAB_STOP)
						|| (element->flags & (UI_ELEMENT_HIDE | UI_ELEMENT_DISABLED))));

					if (~element->flags & UI_ELEMENT_WINDOW) {
						UIElementFocus(element);
					}

					handled = true;
				} else if (!window->dialog) {
					for (intptr_t i = window->shortcutCount - 1; i >= 0; i--) {
						UIShortcut *shortcut = window->shortcuts + i;

						if (shortcut->code == m->code && shortcut->ctrl == window->ctrl
								&& shortcut->shift == window->shift && shortcut->alt == window->alt) {
							shortcut->invoke(shortcut->cp);
							handled = true;
							break;
						}
					}
				} else if (window->dialog) {
					UIElementMessage(window->dialog, message, di, dp);
				}
			}
		}

		if (ui.quit || ui.dialogResult) goto end;

		if (hovered != window->hovered) {
			UIElement *previous = window->hovered;
			window->hovered = hovered;
			UIElementMessage(previous, UI_MSG_UPDATE, UI_UPDATE_HOVERED, 0);
			UIElementMessage(window->hovered, UI_MSG_UPDATE, UI_UPDATE_HOVERED, 0);
		}
	}

	end: _UIUpdate();
	return handled;
}

/////////////////////////////////////////
// Font handling.
/////////////////////////////////////////

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

void UIDrawGlyph(UIPainter *painter, int x0, int y0, int c, uint32_t color) {
#ifdef UI_FREETYPE
	UIFont *font = ui.activeFont;

	if (font->isFreeType) {
#ifdef UI_UNICODE
		if (c < 0) c = '?';
#else
		if (c < 0 || c > 127) c = '?';
#endif
		if (c == '\r') c = ' ';

		if (!font->glyphsRendered[c]) {
			FT_Load_Char(font->font, c == 24 ? 0x2191 : c == 25 ? 0x2193 : c == 26 ? 0x2192 : c == 27 ? 0x2190 : c, FT_LOAD_DEFAULT);
#ifdef UI_FREETYPE_SUBPIXEL
			FT_Render_Glyph(font->font->glyph, FT_RENDER_MODE_LCD);
#else
			FT_Render_Glyph(font->font->glyph, FT_RENDER_MODE_NORMAL);
#endif
			FT_Bitmap_Copy(ui.ft, &font->font->glyph->bitmap, &font->glyphs[c]);
			font->glyphOffsetsX[c] = font->font->glyph->bitmap_left;
			font->glyphOffsetsY[c] = font->font->size->metrics.ascender / 64 - font->font->glyph->bitmap_top;
			font->glyphsRendered[c] = true;
		}

		FT_Bitmap *bitmap = &font->glyphs[c];
		x0 += font->glyphOffsetsX[c], y0 += font->glyphOffsetsY[c];

		for (int y = 0; y < (int) bitmap->rows; y++) {
			if (y0 + y < painter->clip.t) continue;
			if (y0 + y >= painter->clip.b) break;

			int width = bitmap->pixel_mode == FT_PIXEL_MODE_LCD ? bitmap->width / 3 : bitmap->width;

			for (int x = 0; x < width; x++) {
				if (x0 + x < painter->clip.l) continue;
				if (x0 + x >= painter->clip.r) break;

				uint32_t *destination = painter->bits + (x0 + x) + (y0 + y) * painter->width;
				uint32_t original = *destination, ra, ga, ba;

				if (bitmap->pixel_mode == FT_PIXEL_MODE_LCD) {
					ra = ((uint8_t *) bitmap->buffer)[x * 3 + y * bitmap->pitch + 0];
					ga = ((uint8_t *) bitmap->buffer)[x * 3 + y * bitmap->pitch + 1];
					ba = ((uint8_t *) bitmap->buffer)[x * 3 + y * bitmap->pitch + 2];
					ra += (ga - ra) / 2, ba += (ga - ba) / 2; // TODO Gamma correct blending!
				} else if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
					ra = (((uint8_t *) bitmap->buffer)[(x >> 3) + y * bitmap->pitch] & (0x80 >> (x & 7))) ? 0xFF : 0;
					ga = ra, ba = ra;
				} else if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY) {
					ra = ((uint8_t *) bitmap->buffer)[x + y * bitmap->pitch];
					ga = ra, ba = ra;
				} else {
					ra = ga = ba = 0;
				}

				uint32_t r2 = (255 - ra) * ((original & 0x000000FF) >> 0);
				uint32_t g2 = (255 - ga) * ((original & 0x0000FF00) >> 8);
				uint32_t b2 = (255 - ba) * ((original & 0x00FF0000) >> 16);
				uint32_t r1 = ra * ((color & 0x000000FF) >> 0);
				uint32_t g1 = ga * ((color & 0x0000FF00) >> 8);
				uint32_t b1 = ba * ((color & 0x00FF0000) >> 16);

				uint32_t result = 0xFF000000 | (0x00FF0000 & ((b1 + b2) << 8))
					| (0x0000FF00 & ((g1 + g2) << 0))
					| (0x000000FF & ((r1 + r2) >> 8));
				*destination = result;
			}
		}

		return;
	}
#endif

	if (c < 0 || c > 127) c = '?';

	UIRectangle rectangle = UIRectangleIntersection(painter->clip, UI_RECT_4(x0, x0 + 8, y0, y0 + 16));

	const uint8_t *data = (const uint8_t *) _uiFont + c * 16;

	for (int i = rectangle.t; i < rectangle.b; i++) {
		uint32_t *bits = painter->bits + i * painter->width + rectangle.l;
		uint8_t byte = data[i - y0];

		for (int j = rectangle.l; j < rectangle.r; j++) {
			if (byte & (1 << (j - x0))) {
				*bits = color;
			}

			bits++;
		}
	}
}

UIFont *UIFontCreate(const char *cPath, uint32_t size) {
	UIFont *font = (UIFont *) UI_CALLOC(sizeof(UIFont));

#ifdef UI_FREETYPE
#ifdef UI_UNICODE
	font->glyphs = (FT_Bitmap *) UI_CALLOC(sizeof(FT_Bitmap) * (_UNICODE_MAX_CODEPOINT + 1));
	font->glyphsRendered = (bool *) UI_CALLOC(sizeof(bool) * (_UNICODE_MAX_CODEPOINT + 1));
	font->glyphOffsetsX = (int *) UI_CALLOC(sizeof(int) * (_UNICODE_MAX_CODEPOINT + 1));
	font->glyphOffsetsY = (int *) UI_CALLOC(sizeof(int) * (_UNICODE_MAX_CODEPOINT + 1));
#endif
	if (cPath) {
		int ret = FT_New_Face(ui.ft, cPath, 0, &font->font);
		if (ret == 0) {
			FT_Select_Charmap(font->font, FT_ENCODING_UNICODE);
			if (FT_HAS_FIXED_SIZES(font->font) && font->font->num_fixed_sizes) {
				// Look for the smallest strike that's at least `size`.
				int j = 0;

				for (int i = 0; i < font->font->num_fixed_sizes; i++) {
					if ((uint32_t) font->font->available_sizes[i].height >= size
							&& font->font->available_sizes[i].y_ppem < font->font->available_sizes[j].y_ppem) {
						j = i;
					}
				}

				FT_Set_Pixel_Sizes(font->font, font->font->available_sizes[j].x_ppem / 64, font->font->available_sizes[j].y_ppem / 64);
			} else {
				FT_Set_Char_Size(font->font, 0, size * 64, 100, 100);
			}

			FT_Load_Char(font->font, 'a', FT_LOAD_DEFAULT);
			font->glyphWidth = font->font->glyph->advance.x / 64;
			font->glyphHeight = (font->font->size->metrics.ascender - font->font->size->metrics.descender) / 64;
			font->isFreeType = true;
			return font;
		} else
			printf("Cannot load font %s : %d\n", cPath, ret);
	}
#endif

	font->glyphWidth = 9;
	font->glyphHeight = 16;
	return font;
}

UIFont *UIFontActivate(UIFont *font) {
	UIFont *previous = ui.activeFont;
	ui.activeFont = font;
	return previous;
}

/////////////////////////////////////////
// Debugging.
/////////////////////////////////////////

#ifdef UI_DEBUG

void UIInspectorLog(const char *cFormat, ...) {
	va_list arguments;
	va_start(arguments, cFormat);
	char buffer[4096];
	vsnprintf(buffer, sizeof(buffer), cFormat, arguments);
	UICodeInsertContent(ui.inspectorLog, buffer, -1, false);
	va_end(arguments);
	UIElementRefresh(&ui.inspectorLog->e);
}

UIElement *_UIInspectorFindNthElement(UIElement *element, int *index, int *depth) {
	if (*index == 0) {
		return element;
	}

	*index = *index - 1;

	for (uint32_t i = 0; i < element->childCount; i++) {
		UIElement *child = element->children[i];

		if (!(child->flags & (UI_ELEMENT_DESTROY | UI_ELEMENT_HIDE))) {
			UIElement *result = _UIInspectorFindNthElement(child, index, depth);

			if (result) {
				if (depth) {
					*depth = *depth + 1;
				}

				return result;
			}
		}
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
			return snprintf(m->buffer, m->bufferBytes, "%d%c", element->id, element->window->focused == element ? '*' : ' ');
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

		_UIElementPaint(&window->e, &painter);
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
	ui.inspector = UIWindowCreate(0, UI_WINDOW_INSPECTOR, "Inspector", 0, 0);
	UISplitPane *splitPane = UISplitPaneCreate(&ui.inspector->e, 0, 0.5f);
	ui.inspectorTable = UITableCreate(&splitPane->e, 0, "Class\tBounds\tID");
	ui.inspectorTable->e.messageUser = _UIInspectorTableMessage;
	ui.inspectorLog = UICodeCreate(&splitPane->e, 0);
}

int _UIInspectorCountElements(UIElement *element) {
	int count = 1;

	for (uint32_t i = 0; i < element->childCount; i++) {
		UIElement *child = element->children[i];

		if (!(child->flags & (UI_ELEMENT_DESTROY | UI_ELEMENT_HIDE))) {
			count += _UIInspectorCountElements(child);
		}
	}

	return count;
}

void _UIInspectorRefresh() {
	if (!ui.inspectorTarget || !ui.inspector || !ui.inspectorTable) return;
	ui.inspectorTable->itemCount = _UIInspectorCountElements(&ui.inspectorTarget->e);
	UITableResizeColumns(ui.inspectorTable);
	UIElementRefresh(&ui.inspectorTable->e);
}

void _UIInspectorSetFocusedWindow(UIWindow *window) {
	if (!ui.inspector || !ui.inspectorTable) return;

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

/////////////////////////////////////////
// Automation for tests.
/////////////////////////////////////////

#ifdef UI_AUTOMATION_TESTS

int UIAutomationRunTests();

void UIAutomationProcessMessage() {
	int result;
	_UIMessageLoopSingle(&result);
}

void UIAutomationKeyboardTypeSingle(intptr_t code, bool ctrl, bool shift, bool alt) {
	UIWindow *window = ui.windows; // TODO Get the focused window.
	UIKeyTyped m = { 0 };
	m.code = code;
	window->ctrl = ctrl;
	window->alt = alt;
	window->shift = shift;
	_UIWindowInputEvent(window, UI_MSG_KEY_TYPED, 0, &m);
	window->ctrl = false;
	window->alt = false;
	window->shift = false;
}

void UIAutomationKeyboardType(const char *string) {
	UIWindow *window = ui.windows; // TODO Get the focused window.

	UIKeyTyped m = { 0 };
	char c[2];
	m.text = c;
	m.textBytes = 1;
	c[1] = 0;

	for (int i = 0; string[i]; i++) {
		window->ctrl = false;
		window->alt = false;
		window->shift = (c[0] >= 'A' && c[0] <= 'Z');
		c[0] = string[i];
		m.code = (c[0] >= 'A' && c[0] <= 'Z') ? UI_KEYCODE_LETTER(c[0])
			: c[0] == '\n' ? UI_KEYCODE_ENTER
			: c[0] == '\t' ? UI_KEYCODE_TAB
			: c[0] == ' ' ? UI_KEYCODE_SPACE
			: (c[0] >= '0' && c[0] <= '9') ? UI_KEYCODE_DIGIT(c[0]) : 0;
		_UIWindowInputEvent(window, UI_MSG_KEY_TYPED, 0, &m);
	}

	window->ctrl = false;
	window->alt = false;
	window->shift = false;
}

bool UIAutomationCheckCodeLineMatches(UICode *code, int lineIndex, const char *input) {
	if (lineIndex < 1 || lineIndex > code->lineCount) return false;
	int bytes = 0;
	for (int i = 0; input[i]; i++) bytes++;
	if (bytes != code->lines[lineIndex - 1].bytes) return false;
	for (int i = 0; input[i]; i++) if (code->content[code->lines[lineIndex - 1].offset + i] != input[i]) return false;
	return true;
}

bool UIAutomationCheckTableItemMatches(UITable *table, int row, int column, const char *input) {
	int bytes = 0;
	for (int i = 0; input[i]; i++) bytes++;
	if (row < 0 || row >= table->itemCount) return false;
	if (column < 0 || column >= table->columnCount) return false;
	char *buffer = (char *) UI_MALLOC(bytes + 1);
	UITableGetItem m = { 0 };
	m.buffer = buffer;
	m.bufferBytes = bytes + 1;
	m.column = column;
	m.index = row;
	int length = UIElementMessage(&table->e, UI_MSG_TABLE_GET_ITEM, 0, &m);
	if (length != bytes) return false;
	for (int i = 0; input[i]; i++) if (buffer[i] != input[i]) return false;
	return true;
}

#endif

/////////////////////////////////////////
// Common platform layer functionality.
/////////////////////////////////////////

void _UIWindowDestroyCommon(UIWindow *window) {
	UI_FREE(window->bits);
	UI_FREE(window->shortcuts);
}

void _UIInitialiseCommon() {
	ui.theme = uiThemeClassic;

#ifdef UI_FREETYPE
	FT_Init_FreeType(&ui.ft);
	UIFontActivate(UIFontCreate(_UI_TO_STRING_2(UI_FONT_PATH), 11));
#else
	UIFontActivate(UIFontCreate(0, 0));
#endif
}

void _UIWindowAdd(UIWindow *window) {
	window->scale = 1.0f;
	window->e.window = window;
	window->hovered = &window->e;
	window->next = ui.windows;
	ui.windows = window;
}

int _UIWindowMessageCommon(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_LAYOUT && element->childCount) {
		UIElementMove(element->children[0], element->bounds, false);
		if (element->window->dialog) UIElementMove(element->window->dialog, element->bounds, false);
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_GET_CHILD_STABILITY) {
		return 3; // Both width and height of the child element are ignored.
	}

	return 0;
}

int UIMessageLoop() {
	_UIInspectorCreate();
	_UIUpdate();
#ifdef UI_AUTOMATION_TESTS
	return UIAutomationRunTests();
#else
	int result = 0;
	while (!ui.quit && _UIMessageLoopSingle(&result)) ui.dialogResult = NULL;
	return result;
#endif
}

/////////////////////////////////////////
// Platform layers.
/////////////////////////////////////////

#ifdef UI_LINUX

const int UI_KEYCODE_A = XK_a;
const int UI_KEYCODE_BACKSPACE = XK_BackSpace;
const int UI_KEYCODE_DELETE = XK_Delete;
const int UI_KEYCODE_DOWN = XK_Down;
const int UI_KEYCODE_END = XK_End;
const int UI_KEYCODE_ENTER = XK_Return;
const int UI_KEYCODE_ESCAPE = XK_Escape;
const int UI_KEYCODE_F1 = XK_F1;
const int UI_KEYCODE_HOME = XK_Home;
const int UI_KEYCODE_LEFT = XK_Left;
const int UI_KEYCODE_RIGHT = XK_Right;
const int UI_KEYCODE_SPACE = XK_space;
const int UI_KEYCODE_TAB = XK_Tab;
const int UI_KEYCODE_UP = XK_Up;
const int UI_KEYCODE_INSERT = XK_Insert;
const int UI_KEYCODE_0 = XK_0;
const int UI_KEYCODE_BACKTICK = XK_grave;
const int UI_KEYCODE_PAGE_DOWN = XK_Page_Down;
const int UI_KEYCODE_PAGE_UP = XK_Page_Up;

int _UIWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DEALLOCATE) {
		UIWindow *window = (UIWindow *) element;
		_UIWindowDestroyCommon(window);
		window->image->data = NULL;
		XDestroyImage(window->image);
		XDestroyIC(window->xic);
		XDestroyWindow(ui.display, ((UIWindow *) element)->window);
	}

	return _UIWindowMessageCommon(element, message, di, dp);
}

UIWindow *UIWindowCreate(UIWindow *owner, uint32_t flags, const char *cTitle, int _width, int _height) {
	_UIMenusClose();

	UIWindow *window = (UIWindow *) UIElementCreate(sizeof(UIWindow), NULL, flags | UI_ELEMENT_WINDOW, _UIWindowMessage, "Window");
	_UIWindowAdd(window);
	if (owner) window->scale = owner->scale;

	int width = (flags & UI_WINDOW_MENU) ? 1 : _width ? _width : 800;
	int height = (flags & UI_WINDOW_MENU) ? 1 : _height ? _height : 600;

	XSetWindowAttributes attributes = {};
	attributes.override_redirect = flags & UI_WINDOW_MENU;

	window->window = XCreateWindow(ui.display, DefaultRootWindow(ui.display), 0, 0, width, height, 0, 0,
		InputOutput, CopyFromParent, CWOverrideRedirect, &attributes);
	if (cTitle) XStoreName(ui.display, window->window, cTitle);
	XSelectInput(ui.display, window->window, SubstructureNotifyMask | ExposureMask | PointerMotionMask
		| ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask
		| EnterWindowMask | LeaveWindowMask | ButtonMotionMask | KeymapStateMask | FocusChangeMask | PropertyChangeMask);

	if (flags & UI_WINDOW_MAXIMIZE) {
		Atom atoms[2] = { XInternAtom(ui.display, "_NET_WM_STATE_MAXIMIZED_HORZ", 0), XInternAtom(ui.display, "_NET_WM_STATE_MAXIMIZED_VERT", 0) };
		XChangeProperty(ui.display, window->window, XInternAtom(ui.display, "_NET_WM_STATE", 0), XA_ATOM, 32, PropModeReplace, (unsigned char *) atoms, 2);
	}

	if (~flags & UI_WINDOW_MENU) {
		XMapRaised(ui.display, window->window);
	}

	if (flags & UI_WINDOW_CENTER_IN_OWNER) {
		int x = 0, y = 0;
		_UIWindowGetScreenPosition(owner, &x, &y);
		XMoveResizeWindow(ui.display, window->window, x + owner->width / 2 - width / 2, y + owner->height / 2 - height / 2, width, height);
	}

	XSetWMProtocols(ui.display, window->window, &ui.windowClosedID, 1);
	window->image = XCreateImage(ui.display, ui.visual, 24, ZPixmap, 0, NULL, 10, 10, 32, 0);

	window->xic = XCreateIC(ui.xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, window->window, XNFocusWindow, window->window, NULL);

	int dndVersion = 4;
	XChangeProperty(ui.display, window->window, ui.dndAwareID, XA_ATOM, 32 /* bits */, PropModeReplace, (uint8_t *) &dndVersion, 1);

	return window;
}

Display *_UIX11GetDisplay() {
	return ui.display;
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

void _UIClipboardWriteText(UIWindow *window, char *text) {
	UI_FREE(ui.pasteText);
	ui.pasteText = text;
	XSetSelectionOwner(ui.display, ui.clipboardID, window->window, 0);
}

char *_UIClipboardReadTextStart(UIWindow *window, size_t *bytes) {
	Window clipboardOwner = XGetSelectionOwner(ui.display, ui.clipboardID);

	if (clipboardOwner == None) {
		return NULL;
	}

	if (_UIFindWindow(clipboardOwner)) {
		*bytes = strlen(ui.pasteText);
		char *copy = (char *) UI_MALLOC(*bytes);
		memcpy(copy, ui.pasteText, *bytes);
		return copy;
	}

	XConvertSelection(ui.display, ui.clipboardID, XA_STRING, ui.xSelectionDataID, window->window, CurrentTime);
	XSync(ui.display, 0);
	XNextEvent(ui.display, &ui.copyEvent);

	// Hack to get around the fact that PropertyNotify arrives before SelectionNotify.
	// We need PropertyNotify for incremental transfers.
	while (ui.copyEvent.type == PropertyNotify) {
		XNextEvent(ui.display, &ui.copyEvent);
	}

	if (ui.copyEvent.type == SelectionNotify && ui.copyEvent.xselection.selection == ui.clipboardID && ui.copyEvent.xselection.property) {
		Atom target;
		// This `itemAmount` is actually `bytes_after_return`
		unsigned long size, itemAmount;
		char *data;
		int format;
		XGetWindowProperty(ui.copyEvent.xselection.display, ui.copyEvent.xselection.requestor, ui.copyEvent.xselection.property, 0L, ~0L, 0,
				AnyPropertyType, &target, &format, &size, &itemAmount, (unsigned char **) &data);

		// We have to allocate for incremental transfers but we don't have to allocate for non-incremental transfers.
		// I'm allocating for both here to make _UIClipboardReadTextEnd work the same for both
		if (target != ui.incrID) {
			*bytes = size;
			char *copy = (char *) UI_MALLOC(*bytes);
			memcpy(copy, data, *bytes);
			XFree(data);
			XDeleteProperty(ui.copyEvent.xselection.display, ui.copyEvent.xselection.requestor, ui.copyEvent.xselection.property);
			return copy;
		}

		XFree(data);
		XDeleteProperty(ui.display, ui.copyEvent.xselection.requestor, ui.copyEvent.xselection.property);
		XSync(ui.display, 0);

		*bytes = 0;
		char *fullData = NULL;

		while (true) {
			// TODO Timeout.
			XNextEvent(ui.display, &ui.copyEvent);

			if (ui.copyEvent.type == PropertyNotify) {
				// The other case - PropertyDelete would be caused by us and can be ignored
				if (ui.copyEvent.xproperty.state == PropertyNewValue) {
					unsigned long chunkSize;

					// Note that this call deletes the property.
					XGetWindowProperty(ui.display, ui.copyEvent.xproperty.window, ui.copyEvent.xproperty.atom, 0L, ~0L,
						True, AnyPropertyType, &target, &format, &chunkSize, &itemAmount, (unsigned char **) &data);

					if (chunkSize == 0) {
						return fullData;
					} else {
						ptrdiff_t currentOffset = *bytes;
						*bytes += chunkSize;
						fullData = (char *) UI_REALLOC(fullData, *bytes);
						memcpy(fullData + currentOffset, data, chunkSize);
					}

					XFree(data);
				}
			}
		}
	} else {
		// TODO What should happen in this case? Is the next event always going to be the selection event?
		return NULL;
	}
}

void _UIClipboardReadTextEnd(UIWindow *window, char *text) {
	if (text) {
		UI_FREE(text);
	}
}

void UIInitialise() {
	_UIInitialiseCommon();

	XInitThreads();

	ui.display = XOpenDisplay(NULL);
	ui.visual = XDefaultVisual(ui.display, 0);

	ui.windowClosedID = XInternAtom(ui.display, "WM_DELETE_WINDOW", 0);
	ui.primaryID = XInternAtom(ui.display, "PRIMARY", 0);
	ui.dndEnterID = XInternAtom(ui.display, "XdndEnter", 0);
	ui.dndPositionID = XInternAtom(ui.display, "XdndPosition", 0);
	ui.dndStatusID = XInternAtom(ui.display, "XdndStatus", 0);
	ui.dndActionCopyID = XInternAtom(ui.display, "XdndActionCopy", 0);
	ui.dndDropID = XInternAtom(ui.display, "XdndDrop", 0);
	ui.dndSelectionID = XInternAtom(ui.display, "XdndSelection", 0);
	ui.dndFinishedID = XInternAtom(ui.display, "XdndFinished", 0);
	ui.dndAwareID = XInternAtom(ui.display, "XdndAware", 0);
	ui.uriListID = XInternAtom(ui.display, "text/uri-list", 0);
	ui.plainTextID = XInternAtom(ui.display, "text/plain", 0);
	ui.clipboardID = XInternAtom(ui.display, "CLIPBOARD", 0);
	ui.xSelectionDataID = XInternAtom(ui.display, "XSEL_DATA", 0);
	ui.textID = XInternAtom(ui.display, "TEXT", 0);
	ui.targetID = XInternAtom(ui.display, "TARGETS", 0);
	ui.incrID = XInternAtom(ui.display, "INCR", 0);

	ui.cursors[UI_CURSOR_ARROW] = XCreateFontCursor(ui.display, XC_left_ptr);
	ui.cursors[UI_CURSOR_TEXT] = XCreateFontCursor(ui.display, XC_xterm);
	ui.cursors[UI_CURSOR_SPLIT_V] = XCreateFontCursor(ui.display, XC_sb_v_double_arrow);
	ui.cursors[UI_CURSOR_SPLIT_H] = XCreateFontCursor(ui.display, XC_sb_h_double_arrow);
	ui.cursors[UI_CURSOR_FLIPPED_ARROW] = XCreateFontCursor(ui.display, XC_right_ptr);
	ui.cursors[UI_CURSOR_CROSS_HAIR] = XCreateFontCursor(ui.display, XC_crosshair);
	ui.cursors[UI_CURSOR_HAND] = XCreateFontCursor(ui.display, XC_hand1);
	ui.cursors[UI_CURSOR_RESIZE_UP] = XCreateFontCursor(ui.display, XC_top_side);
	ui.cursors[UI_CURSOR_RESIZE_LEFT] = XCreateFontCursor(ui.display, XC_left_side);
	ui.cursors[UI_CURSOR_RESIZE_UP_RIGHT] = XCreateFontCursor(ui.display, XC_top_right_corner);
	ui.cursors[UI_CURSOR_RESIZE_UP_LEFT] = XCreateFontCursor(ui.display, XC_top_left_corner);
	ui.cursors[UI_CURSOR_RESIZE_DOWN] = XCreateFontCursor(ui.display, XC_bottom_side);
	ui.cursors[UI_CURSOR_RESIZE_RIGHT] = XCreateFontCursor(ui.display, XC_right_side);
	ui.cursors[UI_CURSOR_RESIZE_DOWN_LEFT] = XCreateFontCursor(ui.display, XC_bottom_left_corner);
	ui.cursors[UI_CURSOR_RESIZE_DOWN_RIGHT] = XCreateFontCursor(ui.display, XC_bottom_right_corner);

	XSetLocaleModifiers("");

	ui.xim = XOpenIM(ui.display, 0, 0, 0);

	if(!ui.xim){
		XSetLocaleModifiers("@im=none");
		ui.xim = XOpenIM(ui.display, 0, 0, 0);
	}
}

void _UIWindowSetCursor(UIWindow *window, int cursor) {
	XDefineCursor(ui.display, window->window, ui.cursors[cursor]);
}

void _UIX11ResetCursor(UIWindow *window) {
	XDefineCursor(ui.display, window->window, ui.cursors[UI_CURSOR_ARROW]);
}

void _UIWindowEndPaint(UIWindow *window, UIPainter *painter) {
	(void) painter;

	XPutImage(ui.display, window->window, DefaultGC(ui.display, 0), window->image,
		UI_RECT_TOP_LEFT(window->updateRegion), UI_RECT_TOP_LEFT(window->updateRegion),
		UI_RECT_SIZE(window->updateRegion));
}

void _UIWindowGetScreenPosition(UIWindow *window, int *_x, int *_y) {
	Window child;
	XTranslateCoordinates(ui.display, window->window, DefaultRootWindow(ui.display), 0, 0, _x, _y, &child);
}

void UIMenuShow(UIMenu *menu) {
	Window child;

	// Find the screen that contains the point the menu was created at.
	Screen *menuScreen = NULL;
	int screenX, screenY;

	for (int i = 0; i < ScreenCount(ui.display); i++) {
		Screen *screen = ScreenOfDisplay(ui.display, i);
		int x, y;
		XTranslateCoordinates(ui.display, screen->root, DefaultRootWindow(ui.display), 0, 0, &x, &y, &child);

		if (menu->pointX >= x && menu->pointX < x + screen->width && menu->pointY >= y && menu->pointY < y + screen->height) {
			menuScreen = screen;
			screenX = x, screenY = y;
			break;
		}
	}
		
	int width, height;
	_UIMenuPrepare(menu, &width, &height);

	{
		// Clamp the menu to the bounds of the window.
		// This step shouldn't be necessary with the screen clamping below, but there are some buggy X11 drivers that report screen sizes incorrectly.
		int wx, wy;
		UIWindow *parentWindow = menu->parentWindow;
		XTranslateCoordinates(ui.display, parentWindow->window, DefaultRootWindow(ui.display), 0, 0, &wx, &wy, &child);
		if (menu->pointX + width > wx + parentWindow->width) menu->pointX = wx + parentWindow->width - width;
		if (menu->pointY + height > wy + parentWindow->height) menu->pointY = wy + parentWindow->height - height;
		if (menu->pointX < wx) menu->pointX = wx;
		if (menu->pointY < wy) menu->pointY = wy;
	}

	if (menuScreen) {
		// Clamp to the bounds of the screen.
		if (menu->pointX + width > screenX + menuScreen->width) menu->pointX = screenX + menuScreen->width - width;
		if (menu->pointY + height > screenY + menuScreen->height) menu->pointY = screenY + menuScreen->height - height;
		if (menu->pointX < screenX) menu->pointX = screenX;
		if (menu->pointY < screenY) menu->pointY = screenY;
		if (menu->pointX + width > screenX + menuScreen->width) width = screenX + menuScreen->width - menu->pointX;
		if (menu->pointY + height > screenY + menuScreen->height) height = screenY + menuScreen->height - menu->pointY;
	}

	Atom properties[] = {
		XInternAtom(ui.display, "_NET_WM_WINDOW_TYPE", true),
		XInternAtom(ui.display, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", true),
		XInternAtom(ui.display, "_MOTIF_WM_HINTS", true),
	};

	XChangeProperty(ui.display, menu->e.window->window, properties[0], XA_ATOM, 32, PropModeReplace, (uint8_t *) properties, 2);
	XSetTransientForHint(ui.display, menu->e.window->window, DefaultRootWindow(ui.display));

	struct Hints {
		int flags;
		int functions;
		int decorations;
		int inputMode;
		int status;
	};

	struct Hints hints = { 0 };
	hints.flags = 2;
	XChangeProperty(ui.display, menu->e.window->window, properties[2], properties[2], 32, PropModeReplace, (uint8_t *) &hints, 5);

	XMapWindow(ui.display, menu->e.window->window);
	XMoveResizeWindow(ui.display, menu->e.window->window, menu->pointX, menu->pointY, width, height);
}

void UIWindowPack(UIWindow *window, int _width) {
	int width = _width ? _width : UIElementMessage(window->e.children[0], UI_MSG_GET_WIDTH, 0, 0);
	int height = UIElementMessage(window->e.children[0], UI_MSG_GET_HEIGHT, width, 0);
	XResizeWindow(ui.display, window->window, width, height);
}

bool _UIProcessEvent(XEvent *event) {
	if (event->type == ClientMessage && (Atom) event->xclient.data.l[0] == ui.windowClosedID) {
		UIWindow *window = _UIFindWindow(event->xclient.window);
		if (!window) return false;
		bool exit = !UIElementMessage(&window->e, UI_MSG_WINDOW_CLOSE, 0, 0);
		if (exit) return true;
		_UIUpdate();
		return false;
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
			UIElementRelayout(&window->e);
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
			uintptr_t p = ((uintptr_t) (event->xkey.x_root & 0xFFFF) << 0) | ((uintptr_t) (event->xkey.y_root & 0xFFFF) << 16);
#if INTPTR_MAX == INT64_MAX
			p |= (uintptr_t) (event->xkey.time & 0xFFFFFFFF) << 32;
#endif
			UIElementMessage(&window->e, (UIMessage) event->xkey.state, 0, (void *) p);
			_UIUpdate();
		} else {
			char text[32];
			KeySym symbol = NoSymbol;
			Status status;
			// printf("%ld, %s\n", symbol, text);
			UIKeyTyped m = { 0 };
			m.textBytes = Xutf8LookupString(window->xic, &event->xkey, text, sizeof(text) - 1, &symbol, &status);
			m.text = text;
			m.code = XLookupKeysym(&event->xkey, 0);

			if (symbol == XK_Control_L || symbol == XK_Control_R) {
				window->ctrl = true;
				window->ctrlCode = event->xkey.keycode;
				_UIWindowInputEvent(window, UI_MSG_MOUSE_MOVE, 0, 0);
			} else if (symbol == XK_Shift_L || symbol == XK_Shift_R) {
				window->shift = true;
				window->shiftCode = event->xkey.keycode;
				_UIWindowInputEvent(window, UI_MSG_MOUSE_MOVE, 0, 0);
			} else if (symbol == XK_Alt_L || symbol == XK_Alt_R) {
				window->alt = true;
				window->altCode = event->xkey.keycode;
				_UIWindowInputEvent(window, UI_MSG_MOUSE_MOVE, 0, 0);
			} else if (symbol == XK_KP_Left) {
				m.code = UI_KEYCODE_LEFT;
			} else if (symbol == XK_KP_Right) {
				m.code = UI_KEYCODE_RIGHT;
			} else if (symbol == XK_KP_Up) {
				m.code = UI_KEYCODE_UP;
			} else if (symbol == XK_KP_Down) {
				m.code = UI_KEYCODE_DOWN;
			} else if (symbol == XK_KP_Home) {
				m.code = UI_KEYCODE_HOME;
			} else if (symbol == XK_KP_End) {
				m.code = UI_KEYCODE_END;
			} else if (symbol == XK_KP_Enter) {
				m.code = UI_KEYCODE_ENTER;
			} else if (symbol == XK_KP_Delete) {
				m.code = UI_KEYCODE_DELETE;
			} else if (symbol == XK_KP_Page_Up) {
				m.code = UI_KEYCODE_UP;
			} else if (symbol == XK_KP_Page_Down) {
				m.code = UI_KEYCODE_DOWN;
			}

			_UIWindowInputEvent(window, UI_MSG_KEY_TYPED, 0, &m);
		}
	} else if (event->type == KeyRelease) {
		UIWindow *window = _UIFindWindow(event->xkey.window);
		if (!window) return false;

		if (event->xkey.keycode == window->ctrlCode) {
			window->ctrl = false;
			_UIWindowInputEvent(window, UI_MSG_MOUSE_MOVE, 0, 0);
		} else if (event->xkey.keycode == window->shiftCode) {
			window->shift = false;
			_UIWindowInputEvent(window, UI_MSG_MOUSE_MOVE, 0, 0);
		} else if (event->xkey.keycode == window->altCode) {
			window->alt = false;
			_UIWindowInputEvent(window, UI_MSG_MOUSE_MOVE, 0, 0);
		} else {
			char text[32];
			KeySym symbol = NoSymbol;
			Status status;
			UIKeyTyped m = { 0 };
			m.textBytes = Xutf8LookupString(window->xic, &event->xkey, text, sizeof(text) - 1, &symbol, &status);
			m.text = text;
			m.code = XLookupKeysym(&event->xkey, 0);
			_UIWindowInputEvent(window, UI_MSG_KEY_RELEASED, 0, &m);
		}
	} else if (event->type == FocusIn) {
		UIWindow *window = _UIFindWindow(event->xfocus.window);
		if (!window) return false;
		window->ctrl = window->shift = window->alt = false;
		UIElementMessage(&window->e, UI_MSG_WINDOW_ACTIVATE, 0, 0);
	} else if (event->type == FocusOut || event->type == ResizeRequest) {
		_UIMenusClose();
		_UIUpdate();
	} else if (event->type == ClientMessage && event->xclient.message_type == ui.dndEnterID) {
		UIWindow *window = _UIFindWindow(event->xclient.window);
		if (!window) return false;
		window->dragSource = (Window) event->xclient.data.l[0];
	} else if (event->type == ClientMessage && event->xclient.message_type == ui.dndPositionID) {
		UIWindow *window = _UIFindWindow(event->xclient.window);
		if (!window) return false;
		XClientMessageEvent m = { 0 };
		m.type = ClientMessage;
		m.display = event->xclient.display;
		m.window = (Window) event->xclient.data.l[0];
		m.message_type = ui.dndStatusID;
		m.format = 32;
		m.data.l[0] = window->window;
		m.data.l[1] = true;
		m.data.l[4] = ui.dndActionCopyID;
		XSendEvent(ui.display, m.window, False, NoEventMask, (XEvent *) &m);
		XFlush(ui.display);
	} else if (event->type == ClientMessage && event->xclient.message_type == ui.dndDropID) {
		UIWindow *window = _UIFindWindow(event->xclient.window);
		if (!window) return false;

		// TODO Dropping text.

		if (!XConvertSelection(ui.display, ui.dndSelectionID, ui.uriListID, ui.primaryID, window->window, event->xclient.data.l[2])) {
			XClientMessageEvent m = { 0 };
			m.type = ClientMessage;
			m.display = ui.display;
			m.window = window->dragSource;
			m.message_type = ui.dndFinishedID;
			m.format = 32;
			m.data.l[0] = window->window;
			m.data.l[1] = 0;
			m.data.l[2] = ui.dndActionCopyID;
			XSendEvent(ui.display, m.window, False, NoEventMask, (XEvent *) &m);
			XFlush(ui.display);
		}
	} else if (event->type == SelectionNotify) {
		UIWindow *window = _UIFindWindow(event->xselection.requestor);
		if (!window) return false;
		if (!window->dragSource) return false;

		Atom type = None;
		int format = 0;
		unsigned long count = 0, bytesLeft = 0;
		uint8_t *data = NULL;
		XGetWindowProperty(ui.display, window->window, ui.primaryID, 0, 65536, False, AnyPropertyType, &type, &format, &count, &bytesLeft, &data);

		if (format == 8 /* bits per character */) {
			if (event->xselection.target == ui.uriListID) {
				char *copy = (char *) UI_MALLOC(count);
				int fileCount = 0;

				for (int i = 0; i < (int) count; i++) {
					copy[i] = data[i];

					if (i && data[i - 1] == '\r' && data[i] == '\n') {
						fileCount++;
					}
				}

				char **files = (char **) UI_MALLOC(sizeof(char *) * fileCount);
				fileCount = 0;

				for (int i = 0; i < (int) count; i++) {
					char *s = copy + i;
					while (!(i && data[i - 1] == '\r' && data[i] == '\n' && i < (int) count)) i++;
					copy[i - 1] = 0;

					for (int j = 0; s[j]; j++) {
						if (s[j] == '%' && s[j + 1] && s[j + 2]) {
							char n[3];
							n[0] = s[j + 1], n[1] = s[j + 2], n[2] = 0;
							s[j] = strtol(n, NULL, 16);
							if (!s[j]) break;
							UI_MEMMOVE(s + j + 1, s + j + 3, strlen(s) - j - 2);
						}
					}

					if (s[0] == 'f' && s[1] == 'i' && s[2] == 'l' && s[3] == 'e' && s[4] == ':' && s[5] == '/' && s[6] == '/') {
						files[fileCount++] = s + 7;
					}
				}

				UIElementMessage(&window->e, UI_MSG_WINDOW_DROP_FILES, fileCount, files);

				UI_FREE(files);
				UI_FREE(copy);
			} else if (event->xselection.target == ui.plainTextID) {
				// TODO.
			}
		}

		XFree(data);

		XClientMessageEvent m = { 0 };
		m.type = ClientMessage;
		m.display = ui.display;
		m.window = window->dragSource;
		m.message_type = ui.dndFinishedID;
		m.format = 32;
		m.data.l[0] = window->window;
		m.data.l[1] = true;
		m.data.l[2] = ui.dndActionCopyID;
		XSendEvent(ui.display, m.window, False, NoEventMask, (XEvent *) &m);
		XFlush(ui.display);

		window->dragSource = 0; // Drag complete.
		_UIUpdate();
	} else if (event->type == SelectionRequest) {
		UIWindow *window = _UIFindWindow(event->xclient.window);
		if (!window) return false;

		if ((XGetSelectionOwner(ui.display, ui.clipboardID) == window->window)
				&& (event->xselectionrequest.selection == ui.clipboardID)) {
			XSelectionRequestEvent requestEvent = event->xselectionrequest;
			Atom utf8ID = XInternAtom(ui.display, "UTF8_STRING", 1);
			if (utf8ID == None) utf8ID = XA_STRING;

			Atom type = requestEvent.target;
			type = (type == ui.textID) ? XA_STRING : type;
			int changePropertyResult = 0;

			if(requestEvent.target == XA_STRING || requestEvent.target == ui.textID || requestEvent.target == utf8ID) {
				changePropertyResult = XChangeProperty(requestEvent.display, requestEvent.requestor, requestEvent.property,
						type, 8, PropModeReplace, (const unsigned char *) ui.pasteText, strlen(ui.pasteText));
			} else if (requestEvent.target == ui.targetID) {
				changePropertyResult = XChangeProperty(requestEvent.display, requestEvent.requestor, requestEvent.property,
						XA_ATOM, 32, PropModeReplace, (unsigned char *) &utf8ID, 1);
			}

			if(changePropertyResult == 0 || changePropertyResult == 1) {
				XSelectionEvent sendEvent = {
					.type = SelectionNotify,
					.serial = requestEvent.serial,
					.send_event = requestEvent.send_event,
					.display = requestEvent.display,
					.requestor = requestEvent.requestor,
					.selection = requestEvent.selection,
					.target = requestEvent.target,
					.property = requestEvent.property,
					.time = requestEvent.time
				};

				XSendEvent(ui.display, requestEvent.requestor, 0, 0, (XEvent *) &sendEvent);
			}
		}
	}

	return false;
}

bool _UIMessageLoopSingle(int *result) {
	XEvent events[64];

	if (ui.animatingCount) {
		if (XPending(ui.display)) {
			XNextEvent(ui.display, events + 0);
		} else {
			_UIProcessAnimations();
			return true;
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
			return false;
		}
	}

	return true;
}

void UIWindowPostMessage(UIWindow *window, UIMessage message, void *_dp) {
	// HACK! Xlib doesn't seem to have a nice way to do this,
	// so send a specially crafted key press event instead.
	// TODO Maybe ClientMessage is what this should use?
	uintptr_t dp = (uintptr_t) _dp;
	XKeyEvent event = { 0 };
	event.display = ui.display;
	event.window = window->window;
	event.root = DefaultRootWindow(ui.display);
	event.subwindow = None;
#if INTPTR_MAX == INT64_MAX
	event.time = dp >> 32;
#endif
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

const int UI_KEYCODE_A = 'A';
const int UI_KEYCODE_0 = '0';
const int UI_KEYCODE_BACKSPACE = VK_BACK;
const int UI_KEYCODE_DELETE = VK_DELETE;
const int UI_KEYCODE_DOWN = VK_DOWN;
const int UI_KEYCODE_END = VK_END;
const int UI_KEYCODE_ENTER = VK_RETURN;
const int UI_KEYCODE_ESCAPE = VK_ESCAPE;
const int UI_KEYCODE_F1 = VK_F1;
const int UI_KEYCODE_HOME = VK_HOME;
const int UI_KEYCODE_LEFT = VK_LEFT;
const int UI_KEYCODE_RIGHT = VK_RIGHT;
const int UI_KEYCODE_SPACE = VK_SPACE;
const int UI_KEYCODE_TAB = VK_TAB;
const int UI_KEYCODE_UP = VK_UP;
const int UI_KEYCODE_INSERT = VK_INSERT;
const int UI_KEYCODE_PAGE_UP = VK_PRIOR;
const int UI_KEYCODE_PAGE_DOWN = VK_NEXT;

int _UIWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DEALLOCATE) {
		UIWindow *window = (UIWindow *) element;
		_UIWindowDestroyCommon(window);
		SetWindowLongPtr(window->hwnd, GWLP_USERDATA, 0);
		DestroyWindow(window->hwnd);
	}

	return _UIWindowMessageCommon(element, message, di, dp);
}

LRESULT CALLBACK _UIWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	UIWindow *window = (UIWindow *) GetWindowLongPtr(hwnd, GWLP_USERDATA);

	if (!window || ui.assertionFailure) {
		return DefWindowProc(hwnd, message, wParam, lParam);
	}

	if (message == WM_CLOSE) {
		if (UIElementMessage(&window->e, UI_MSG_WINDOW_CLOSE, 0, 0)) {
			_UIUpdate();
			return 0;
		} else {
			PostQuitMessage(0);
		}
	} else if (message == WM_SIZE) {
		RECT client;
		GetClientRect(hwnd, &client);
		window->width = client.right;
		window->height = client.bottom;
		window->bits = (uint32_t *) UI_REALLOC(window->bits, window->width * window->height * 4);
		window->e.bounds = UI_RECT_2S(window->width, window->height);
		window->e.clip = UI_RECT_2S(window->width, window->height);
		UIElementRelayout(&window->e);
		_UIUpdate();
	} else if (message == WM_MOUSEMOVE) {
		if (!window->trackingLeave) {
			window->trackingLeave = true;
			TRACKMOUSEEVENT leave = { 0 };
			leave.cbSize = sizeof(TRACKMOUSEEVENT);
			leave.dwFlags = TME_LEAVE;
			leave.hwndTrack = hwnd;
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
		BITMAPINFOHEADER info = { 0 };
		info.biSize = sizeof(info);
		info.biWidth = window->width, info.biHeight = -window->height;
		info.biPlanes = 1, info.biBitCount = 32;
		StretchDIBits(dc, 0, 0, UI_RECT_SIZE(window->e.bounds), 0, 0, UI_RECT_SIZE(window->e.bounds),
			window->bits, (BITMAPINFO *) &info, DIB_RGB_COLORS, SRCCOPY);
		EndPaint(hwnd, &paint);
	} else if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT) {
		SetCursor(ui.cursors[window->cursorStyle]);
		return 1;
	} else if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
		_UIMenusClose();

		if (message == WM_SETFOCUS) {
			_UIInspectorSetFocusedWindow(window);
			UIElementMessage(&window->e, UI_MSG_WINDOW_ACTIVATE, 0, 0);
		}
	} else if (message == WM_MOUSEACTIVATE && (window->e.flags & UI_WINDOW_MENU)) {
		return MA_NOACTIVATE;
	} else if (message == WM_DROPFILES) {
		HDROP drop = (HDROP) wParam;
		int count = DragQueryFile(drop, 0xFFFFFFFF, NULL, 0);
		char **files = (char **) UI_MALLOC(sizeof(char *) * count);

		for (int i = 0; i < count; i++) {
			int length = DragQueryFile(drop, i, NULL, 0);
			files[i] = (char *) UI_MALLOC(length + 1);
			files[i][length] = 0;
			DragQueryFile(drop, i, files[i], length + 1);
		}

		UIElementMessage(&window->e, UI_MSG_WINDOW_DROP_FILES, count, files);
		for (int i = 0; i < count; i++) UI_FREE(files[i]);
		UI_FREE(files);
		DragFinish(drop);
		_UIUpdate();
	} else if (message == WM_APP + 1) {
		UIElementMessage(&window->e, (UIMessage) wParam, 0, (void *) lParam);
		_UIUpdate();
	} else {
		if (message == WM_NCLBUTTONDOWN || message == WM_NCMBUTTONDOWN || message == WM_NCRBUTTONDOWN) {
			if (~window->e.flags & UI_WINDOW_MENU) {
				_UIMenusClose();
				_UIUpdate();
			}
		}

		return DefWindowProc(hwnd, message, wParam, lParam);
	}

	return 0;
}

void UIInitialise() {
	ui.heap = GetProcessHeap();

	_UIInitialiseCommon();

	ui.cursors[UI_CURSOR_ARROW] = LoadCursor(NULL, IDC_ARROW);
	ui.cursors[UI_CURSOR_TEXT] = LoadCursor(NULL, IDC_IBEAM);
	ui.cursors[UI_CURSOR_SPLIT_V] = LoadCursor(NULL, IDC_SIZENS);
	ui.cursors[UI_CURSOR_SPLIT_H] = LoadCursor(NULL, IDC_SIZEWE);
	ui.cursors[UI_CURSOR_FLIPPED_ARROW] = LoadCursor(NULL, IDC_ARROW);
	ui.cursors[UI_CURSOR_CROSS_HAIR] = LoadCursor(NULL, IDC_CROSS);
	ui.cursors[UI_CURSOR_HAND] = LoadCursor(NULL, IDC_HAND);
	ui.cursors[UI_CURSOR_RESIZE_UP] = LoadCursor(NULL, IDC_SIZENS);
	ui.cursors[UI_CURSOR_RESIZE_LEFT] = LoadCursor(NULL, IDC_SIZEWE);
	ui.cursors[UI_CURSOR_RESIZE_UP_RIGHT] = LoadCursor(NULL, IDC_SIZENESW);
	ui.cursors[UI_CURSOR_RESIZE_UP_LEFT] = LoadCursor(NULL, IDC_SIZENWSE);
	ui.cursors[UI_CURSOR_RESIZE_DOWN] = LoadCursor(NULL, IDC_SIZENS);
	ui.cursors[UI_CURSOR_RESIZE_RIGHT] = LoadCursor(NULL, IDC_SIZEWE);
	ui.cursors[UI_CURSOR_RESIZE_DOWN_LEFT] = LoadCursor(NULL, IDC_SIZENESW);
	ui.cursors[UI_CURSOR_RESIZE_DOWN_RIGHT] = LoadCursor(NULL, IDC_SIZENWSE);

	WNDCLASS windowClass = { 0 };
	windowClass.lpfnWndProc = _UIWindowProcedure;
	windowClass.lpszClassName = "normal";
	RegisterClass(&windowClass);
	windowClass.style |= CS_DROPSHADOW;
	windowClass.lpszClassName = "shadow";
	RegisterClass(&windowClass);
}

bool _UIMessageLoopSingle(int *result) {
	MSG message = { 0 };

	if (ui.animating) {
		if (PeekMessage(&message, NULL, 0, 0, PM_REMOVE)) {
			if (message.message == WM_QUIT) {
				*result = message.wParam;
				return false;
			}

			TranslateMessage(&message);
			DispatchMessage(&message);
		} else {
			_UIProcessAnimations();
		}
	} else {
		if (!GetMessage(&message, NULL, 0, 0)) {
			*result = message.wParam;
			return false;
		}

		TranslateMessage(&message);
		DispatchMessage(&message);
	}

	return true;
}

void UIMenuShow(UIMenu *menu) {
	int width, height;
	_UIMenuPrepare(menu, &width, &height);
	MoveWindow(menu->e.window->hwnd, menu->pointX, menu->pointY, width, height, FALSE);
	ShowWindow(menu->e.window->hwnd, SW_SHOWNOACTIVATE);
}

UIWindow *UIWindowCreate(UIWindow *owner, uint32_t flags, const char *cTitle, int width, int height) {
	_UIMenusClose();

	UIWindow *window = (UIWindow *) UIElementCreate(sizeof(UIWindow), NULL, flags | UI_ELEMENT_WINDOW, _UIWindowMessage, "Window");
	_UIWindowAdd(window);
	if (owner) window->scale = owner->scale;

	if (flags & UI_WINDOW_MENU) {
		UI_ASSERT(owner);

		window->hwnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_NOACTIVATE, "shadow", 0, WS_POPUP,
			0, 0, 0, 0, owner->hwnd, NULL, NULL, NULL);
	} else {
		window->hwnd = CreateWindowEx(WS_EX_ACCEPTFILES, "normal", cTitle, WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, width ? width : CW_USEDEFAULT, height ? height : CW_USEDEFAULT,
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
	BITMAPINFOHEADER info = { 0 };
	info.biSize = sizeof(info);
	info.biWidth = window->width, info.biHeight = window->height;
	info.biPlanes = 1, info.biBitCount = 32;
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

void UIWindowPostMessage(UIWindow *window, UIMessage message, void *_dp) {
	PostMessage(window->hwnd, WM_APP + 1, (WPARAM) message, (LPARAM) _dp);
}

void *_UIHeapReAlloc(void *pointer, size_t size) {
	if (pointer) {
		if (size) {
			return HeapReAlloc(ui.heap, 0, pointer, size);
		} else {
			UI_FREE(pointer);
			return NULL;
		}
	} else {
		if (size) {
			return UI_MALLOC(size);
		} else {
			return NULL;
		}
	}
}

void _UIClipboardWriteText(UIWindow *window, char *text) {
	if (OpenClipboard(window->hwnd)) {
		EmptyClipboard();
		HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, _UIStringLength(text) + 1);
		char *copy = (char *) GlobalLock(memory);
		for (uintptr_t i = 0; text[i]; i++) copy[i] = text[i];
		GlobalUnlock(copy);
		SetClipboardData(CF_TEXT, memory);
		CloseClipboard();
	}
}

char *_UIClipboardReadTextStart(UIWindow *window, size_t *bytes) {
	if (!OpenClipboard(window->hwnd)) {
		return NULL;
	}

	HANDLE memory = GetClipboardData(CF_TEXT);

	if (!memory) {
		CloseClipboard();
		return NULL;
	}

	char *buffer = (char *) GlobalLock(memory);

	if (!buffer) {
		CloseClipboard();
		return NULL;
	}

	size_t byteCount = GlobalSize(memory);

	if (byteCount < 1) {
		GlobalUnlock(memory);
		CloseClipboard();
		return NULL;
	}

	char *copy = (char *) UI_MALLOC(byteCount + 1);
	for (uintptr_t i = 0; i < byteCount; i++) copy[i] = buffer[i];
	copy[byteCount] = 0; // Just in case.

	GlobalUnlock(memory);
	CloseClipboard();

	if (bytes) *bytes = _UIStringLength(copy);
	return copy;
}

void _UIClipboardReadTextEnd(UIWindow *window, char *text) {
	UI_FREE(text);
}

void *_UIMemmove(void *dest, const void *src, size_t n) {
	if ((uintptr_t) dest < (uintptr_t) src) {
		uint8_t *dest8 = (uint8_t *) dest;
		const uint8_t *src8 = (const uint8_t *) src;
		for (uintptr_t i = 0; i < n; i++) {
			dest8[i] = src8[i];
		}
		return dest;
	} else {
		uint8_t *dest8 = (uint8_t *) dest;
		const uint8_t *src8 = (const uint8_t *) src;
		for (uintptr_t i = n; i; i--) {
			dest8[i - 1] = src8[i - 1];
		}
		return dest;
	}
}

#endif

#ifdef UI_ESSENCE

const int UI_KEYCODE_A = ES_SCANCODE_A;
const int UI_KEYCODE_0 = ES_SCANCODE_0;
const int UI_KEYCODE_BACKSPACE = ES_SCANCODE_BACKSPACE;
const int UI_KEYCODE_DELETE = ES_SCANCODE_DELETE;
const int UI_KEYCODE_DOWN = ES_SCANCODE_DOWN_ARROW;
const int UI_KEYCODE_END = ES_SCANCODE_END;
const int UI_KEYCODE_ENTER = ES_SCANCODE_ENTER;
const int UI_KEYCODE_ESCAPE = ES_SCANCODE_ESCAPE;
const int UI_KEYCODE_F1 = ES_SCANCODE_F1;
const int UI_KEYCODE_HOME = ES_SCANCODE_HOME;
const int UI_KEYCODE_LEFT = ES_SCANCODE_LEFT_ARROW;
const int UI_KEYCODE_RIGHT = ES_SCANCODE_RIGHT_ARROW;
const int UI_KEYCODE_SPACE = ES_SCANCODE_SPACE;
const int UI_KEYCODE_TAB = ES_SCANCODE_TAB;
const int UI_KEYCODE_UP = ES_SCANCODE_UP_ARROW;
const int UI_KEYCODE_INSERT = ES_SCANCODE_INSERT;
const int UI_KEYCODE_PAGE_UP = ES_SCANCODE_PAGE_UP;
const int UI_KEYCODE_PAGE_DOWN = ES_SCANCODE_PAGE_DOWN;

int _UIWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DEALLOCATE) {
		// TODO Non-main windows.
		element->window = NULL;
		EsInstanceCloseReference(ui.instance);
	}

	return _UIWindowMessageCommon(element, message, di, dp);
}

void UIInitialise() {
	_UIInitialiseCommon();

	while (true) {
		EsMessage *message = EsMessageReceive();

		if (message->type == ES_MSG_INSTANCE_CREATE) {
			ui.instance = EsInstanceCreate(message, NULL, 0);
			break;
		}
	}
}

bool _UIMessageLoopSingle(int *result) {
	if (ui.animating) {
		// TODO.
	} else {
		_UIMessageProcess(EsMessageReceive());
	}

	return true;
}

UIMenu *UIMenuCreate(UIElement *parent, uint32_t flags) {
	ui.menuIndex = 0;
	return EsMenuCreate(parent->window->window, ES_MENU_AT_CURSOR);
}

void _UIMenuItemCallback(EsMenu *menu, EsGeneric context) {
	((void (*)(void *)) ui.menuData[context.u * 2 + 0])(ui.menuData[context.u * 2 + 1]);
}

void UIMenuAddItem(UIMenu *menu, uint32_t flags, const char *label, ptrdiff_t labelBytes, void (*invoke)(void *cp), void *cp) {
	EsAssert(ui.menuIndex < 128);
	ui.menuData[ui.menuIndex * 2 + 0] = (void *) invoke;
	ui.menuData[ui.menuIndex * 2 + 1] = cp;
	EsMenuAddItem(menu, (flags & UI_BUTTON_CHECKED) ? ES_MENU_ITEM_CHECKED : ES_FLAGS_DEFAULT,
			label, labelBytes, _UIMenuItemCallback, ui.menuIndex);
	ui.menuIndex++;
}

void UIMenuShow(UIMenu *menu) {
	EsMenuShow(menu);
}

int _UIWindowCanvasMessage(EsElement *element, EsMessage *message) {
	UIWindow *window = (UIWindow *) element->window->userData.p;

	if (!window) {
		return 0;
	} else if (message->type == ES_MSG_PAINT) {
		EsRectangle bounds = ES_RECT_4PD(message->painter->offsetX, message->painter->offsetY, window->width, window->height);
		EsDrawBitmap(message->painter, bounds, window->bits, window->width * 4, 0xFFFF);
	} else if (message->type == ES_MSG_LAYOUT) {
		EsElementGetSize(element, &window->width, &window->height);
		window->bits = (uint32_t *) UI_REALLOC(window->bits, window->width * window->height * 4);
		window->e.bounds = UI_RECT_2S(window->width, window->height);
		window->e.clip = UI_RECT_2S(window->width, window->height);
		UIElementRelayout(&window->e);
		_UIUpdate();
	} else if (message->type == ES_MSG_SCROLL_WHEEL) {
		_UIWindowInputEvent(window, UI_MSG_MOUSE_WHEEL, -message->scrollWheel.dy, 0);
	} else if (message->type == ES_MSG_MOUSE_MOVED || message->type == ES_MSG_HOVERED_END
			|| message->type == ES_MSG_MOUSE_LEFT_DRAG || message->type == ES_MSG_MOUSE_RIGHT_DRAG || message->type == ES_MSG_MOUSE_MIDDLE_DRAG) {
		EsPoint point = EsMouseGetPosition(element);
		window->cursorX = point.x, window->cursorY = point.y;
		_UIWindowInputEvent(window, UI_MSG_MOUSE_MOVE, 0, 0);
	} else if (message->type == ES_MSG_KEY_UP) {
		window->ctrl = EsKeyboardIsCtrlHeld();
		window->shift = EsKeyboardIsShiftHeld();
		window->alt = EsKeyboardIsAltHeld();
	} else if (message->type == ES_MSG_KEY_DOWN) {
		window->ctrl = EsKeyboardIsCtrlHeld();
		window->shift = EsKeyboardIsShiftHeld();
		window->alt = EsKeyboardIsAltHeld();
		UIKeyTyped m = { 0 };
		char c[64];
		m.text = c;
		m.textBytes = EsMessageGetInputText(message, c);
		m.code = message->keyboard.scancode;
		return _UIWindowInputEvent(window, UI_MSG_KEY_TYPED, 0, &m) ? ES_HANDLED : 0;
	} else if (message->type == ES_MSG_MOUSE_LEFT_CLICK) {
		_UIInspectorSetFocusedWindow(window);
	} else if (message->type == ES_MSG_USER_START) {
		UIElementMessage(&window->e, (UIMessage) message->user.context1.u, 0, (void *) message->user.context2.p);
		_UIUpdate();
	} else if (message->type == ES_MSG_GET_CURSOR) {
		message->cursorStyle = ES_CURSOR_NORMAL;
		if (window->cursor == UI_CURSOR_TEXT)              message->cursorStyle = ES_CURSOR_TEXT;
		if (window->cursor == UI_CURSOR_SPLIT_V)           message->cursorStyle = ES_CURSOR_SPLIT_VERTICAL;
		if (window->cursor == UI_CURSOR_SPLIT_H)           message->cursorStyle = ES_CURSOR_SPLIT_HORIZONTAL;
		if (window->cursor == UI_CURSOR_FLIPPED_ARROW)     message->cursorStyle = ES_CURSOR_SELECT_LINES;
		if (window->cursor == UI_CURSOR_CROSS_HAIR)        message->cursorStyle = ES_CURSOR_CROSS_HAIR_PICK;
		if (window->cursor == UI_CURSOR_HAND)              message->cursorStyle = ES_CURSOR_HAND_HOVER;
		if (window->cursor == UI_CURSOR_RESIZE_UP)         message->cursorStyle = ES_CURSOR_RESIZE_VERTICAL;
		if (window->cursor == UI_CURSOR_RESIZE_LEFT)       message->cursorStyle = ES_CURSOR_RESIZE_HORIZONTAL;
		if (window->cursor == UI_CURSOR_RESIZE_UP_RIGHT)   message->cursorStyle = ES_CURSOR_RESIZE_DIAGONAL_1;
		if (window->cursor == UI_CURSOR_RESIZE_UP_LEFT)    message->cursorStyle = ES_CURSOR_RESIZE_DIAGONAL_2;
		if (window->cursor == UI_CURSOR_RESIZE_DOWN)       message->cursorStyle = ES_CURSOR_RESIZE_VERTICAL;
		if (window->cursor == UI_CURSOR_RESIZE_RIGHT)      message->cursorStyle = ES_CURSOR_RESIZE_HORIZONTAL;
		if (window->cursor == UI_CURSOR_RESIZE_DOWN_RIGHT) message->cursorStyle = ES_CURSOR_RESIZE_DIAGONAL_1;
		if (window->cursor == UI_CURSOR_RESIZE_DOWN_LEFT)  message->cursorStyle = ES_CURSOR_RESIZE_DIAGONAL_2;
	}

	else if (message->type == ES_MSG_MOUSE_LEFT_DOWN)   _UIWindowInputEvent(window, UI_MSG_LEFT_DOWN, 0, 0);
	else if (message->type == ES_MSG_MOUSE_LEFT_UP)     _UIWindowInputEvent(window, UI_MSG_LEFT_UP, 0, 0);
	else if (message->type == ES_MSG_MOUSE_MIDDLE_DOWN) _UIWindowInputEvent(window, UI_MSG_MIDDLE_DOWN, 0, 0);
	else if (message->type == ES_MSG_MOUSE_MIDDLE_UP)   _UIWindowInputEvent(window, UI_MSG_MIDDLE_UP, 0, 0);
	else if (message->type == ES_MSG_MOUSE_RIGHT_DOWN)  _UIWindowInputEvent(window, UI_MSG_RIGHT_DOWN, 0, 0);
	else if (message->type == ES_MSG_MOUSE_RIGHT_UP)    _UIWindowInputEvent(window, UI_MSG_RIGHT_UP, 0, 0);

	else return 0;

	return ES_HANDLED;
}

UIWindow *UIWindowCreate(UIWindow *owner, uint32_t flags, const char *cTitle, int width, int height) {
	_UIMenusClose();

	UIWindow *window = (UIWindow *) UIElementCreate(sizeof(UIWindow), NULL, flags | UI_ELEMENT_WINDOW, _UIWindowMessage, "Window");
	_UIWindowAdd(window);
	if (owner) window->scale = owner->scale;

	if (flags & UI_WINDOW_MENU) {
		// TODO.
	} else {
		// TODO Non-main windows.
		window->window = ui.instance->window;
		window->window->userData = window;
		window->canvas = EsCustomElementCreate(window->window, ES_CELL_FILL | ES_ELEMENT_FOCUSABLE);
		window->canvas->messageUser = _UIWindowCanvasMessage;
		EsWindowSetTitle(window->window, cTitle, -1);
		EsElementFocus(window->canvas);
	}

	return window;
}

void _UIWindowEndPaint(UIWindow *window, UIPainter *painter) {
	EsElementRepaint(window->canvas, &window->updateRegion);
}

void _UIWindowSetCursor(UIWindow *window, int cursor) {
	window->cursor = cursor;
}

void _UIWindowGetScreenPosition(UIWindow *window, int *_x, int *_y) {
	EsRectangle r = EsElementGetScreenBounds(window->window);
	*_x = r.l, *_y = r.t;
}

void UIWindowPostMessage(UIWindow *window, UIMessage message, void *_dp) {
	EsMessage m = {};
	m.type = ES_MSG_USER_START;
	m.user.context1.u = message;
	m.user.context2.p = _dp;
	EsMessagePost(window->canvas, &m);
}

void _UIClipboardWriteText(UIWindow *window, char *text) {
	EsClipboardAddText(ES_CLIPBOARD_PRIMARY, text, -1);
	UI_FREE(text);
}

char *_UIClipboardReadTextStart(UIWindow *window, size_t *bytes) {
	return EsClipboardReadText(ES_CLIPBOARD_PRIMARY, bytes, NULL);
}

void _UIClipboardReadTextEnd(UIWindow *window, char *text) {
	EsHeapFree(text);
}

#endif

#ifdef UI_COCOA

// TODO Standard keyboard shortcuts (Command+Q, Command+W).

const int UI_KEYCODE_A = -100; // TODO Keyboard layout support.
const int UI_KEYCODE_F1 = -70;
const int UI_KEYCODE_0 = -50;
const int UI_KEYCODE_INSERT = -30;

const int UI_KEYCODE_BACKSPACE = kVK_Delete;
const int UI_KEYCODE_DELETE = kVK_ForwardDelete;
const int UI_KEYCODE_DOWN = kVK_DownArrow;
const int UI_KEYCODE_END = kVK_End;
const int UI_KEYCODE_ENTER = kVK_Return;
const int UI_KEYCODE_ESCAPE = kVK_Escape;
const int UI_KEYCODE_HOME = kVK_Home;
const int UI_KEYCODE_LEFT = kVK_LeftArrow;
const int UI_KEYCODE_RIGHT = kVK_RightArrow;
const int UI_KEYCODE_SPACE = kVK_Space;
const int UI_KEYCODE_TAB = kVK_Tab;
const int UI_KEYCODE_UP = kVK_UpArrow;
const int UI_KEYCODE_BACKTICK = kVK_ANSI_Grave; // TODO Keyboard layout support.
const int UI_KEYCODE_PAGE_UP = kVK_PageUp;
const int UI_KEYCODE_PAGE_DOWN = kVK_PageDown;

int (*_cocoaAppMain)(int, char **);
int _cocoaArgc;
char **_cocoaArgv;

struct _UIPostedMessage {
	UIMessage message;
	void *dp;
};

char *_UIUTF8StringFromNSString(NSString *string) {
	NSUInteger size = [string lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
	char *buffer = (char *) UI_MALLOC(size + 1);
	buffer[size] = 0;
	[string getBytes:buffer maxLength:size usedLength:NULL encoding:NSUTF8StringEncoding options:0 range:NSMakeRange(0, [string length]) remainingRange:NULL];
	return buffer;
}

int _UICocoaRemapKey(int code) {
	if (code == kVK_ANSI_A) { return UI_KEYCODE_LETTER('A'); }
	if (code == kVK_ANSI_B) { return UI_KEYCODE_LETTER('B'); }
	if (code == kVK_ANSI_C) { return UI_KEYCODE_LETTER('C'); }
	if (code == kVK_ANSI_D) { return UI_KEYCODE_LETTER('D'); }
	if (code == kVK_ANSI_E) { return UI_KEYCODE_LETTER('E'); }
	if (code == kVK_ANSI_F) { return UI_KEYCODE_LETTER('F'); }
	if (code == kVK_ANSI_G) { return UI_KEYCODE_LETTER('G'); }
	if (code == kVK_ANSI_H) { return UI_KEYCODE_LETTER('H'); }
	if (code == kVK_ANSI_I) { return UI_KEYCODE_LETTER('I'); }
	if (code == kVK_ANSI_J) { return UI_KEYCODE_LETTER('J'); }
	if (code == kVK_ANSI_K) { return UI_KEYCODE_LETTER('K'); }
	if (code == kVK_ANSI_L) { return UI_KEYCODE_LETTER('L'); }
	if (code == kVK_ANSI_M) { return UI_KEYCODE_LETTER('M'); }
	if (code == kVK_ANSI_N) { return UI_KEYCODE_LETTER('N'); }
	if (code == kVK_ANSI_O) { return UI_KEYCODE_LETTER('O'); }
	if (code == kVK_ANSI_P) { return UI_KEYCODE_LETTER('P'); }
	if (code == kVK_ANSI_Q) { return UI_KEYCODE_LETTER('Q'); }
	if (code == kVK_ANSI_R) { return UI_KEYCODE_LETTER('R'); }
	if (code == kVK_ANSI_S) { return UI_KEYCODE_LETTER('S'); }
	if (code == kVK_ANSI_T) { return UI_KEYCODE_LETTER('T'); }
	if (code == kVK_ANSI_U) { return UI_KEYCODE_LETTER('U'); }
	if (code == kVK_ANSI_V) { return UI_KEYCODE_LETTER('V'); }
	if (code == kVK_ANSI_W) { return UI_KEYCODE_LETTER('W'); }
	if (code == kVK_ANSI_X) { return UI_KEYCODE_LETTER('X'); }
	if (code == kVK_ANSI_Y) { return UI_KEYCODE_LETTER('Y'); }
	if (code == kVK_ANSI_Z) { return UI_KEYCODE_LETTER('Z'); }

	if (code == kVK_ANSI_0) { return UI_KEYCODE_DIGIT('0'); }
	if (code == kVK_ANSI_1) { return UI_KEYCODE_DIGIT('1'); }
	if (code == kVK_ANSI_2) { return UI_KEYCODE_DIGIT('2'); }
	if (code == kVK_ANSI_3) { return UI_KEYCODE_DIGIT('3'); }
	if (code == kVK_ANSI_4) { return UI_KEYCODE_DIGIT('4'); }
	if (code == kVK_ANSI_5) { return UI_KEYCODE_DIGIT('5'); }
	if (code == kVK_ANSI_6) { return UI_KEYCODE_DIGIT('6'); }
	if (code == kVK_ANSI_7) { return UI_KEYCODE_DIGIT('7'); }
	if (code == kVK_ANSI_8) { return UI_KEYCODE_DIGIT('8'); }
	if (code == kVK_ANSI_9) { return UI_KEYCODE_DIGIT('9'); }

	if (code == kVK_F1)  { return UI_KEYCODE_FKEY( 1); }
	if (code == kVK_F2)  { return UI_KEYCODE_FKEY( 2); }
	if (code == kVK_F3)  { return UI_KEYCODE_FKEY( 3); }
	if (code == kVK_F4)  { return UI_KEYCODE_FKEY( 4); }
	if (code == kVK_F5)  { return UI_KEYCODE_FKEY( 5); }
	if (code == kVK_F6)  { return UI_KEYCODE_FKEY( 6); }
	if (code == kVK_F7)  { return UI_KEYCODE_FKEY( 7); }
	if (code == kVK_F8)  { return UI_KEYCODE_FKEY( 8); }
	if (code == kVK_F9)  { return UI_KEYCODE_FKEY( 9); }
	if (code == kVK_F10) { return UI_KEYCODE_FKEY(10); }
	if (code == kVK_F11) { return UI_KEYCODE_FKEY(11); }
	if (code == kVK_F12) { return UI_KEYCODE_FKEY(12); }

	return code;
}

@interface UICocoaApplicationDelegate : NSObject<NSApplicationDelegate>
@end

@interface UICocoaWindowDelegate : NSObject<NSWindowDelegate>
@property (nonatomic) UIWindow *uiWindow;
@end

@interface UICocoaMainView : NSView
- (void)handlePostedMessage:(id)message;
- (void)eventCommon:(NSEvent *)event;
@property (nonatomic) UIWindow *uiWindow;
@end

@implementation UICocoaApplicationDelegate
- (void)applicationWillFinishLaunching:(NSNotification *)notification {
	int code = _cocoaAppMain(_cocoaArgc, _cocoaArgv);
	if (code) exit(code);
}
@end

@implementation UICocoaWindowDelegate
- (void)windowDidBecomeKey:(NSNotification *)notification {
	UIElementMessage(&_uiWindow->e, UI_MSG_WINDOW_ACTIVATE, 0, 0);
	_UIUpdate();
}

- (void)windowDidResize:(NSNotification *)notification {
	_uiWindow->width = ((UICocoaMainView *) _uiWindow->view).frame.size.width;
	_uiWindow->height = ((UICocoaMainView *) _uiWindow->view).frame.size.height;
	_uiWindow->bits = (uint32_t *) UI_REALLOC(_uiWindow->bits, _uiWindow->width * _uiWindow->height * 4);
	_uiWindow->e.bounds = UI_RECT_2S(_uiWindow->width, _uiWindow->height);
	_uiWindow->e.clip = UI_RECT_2S(_uiWindow->width, _uiWindow->height);
	UIElementRelayout(&_uiWindow->e);
	_UIUpdate();
}
@end

@implementation UICocoaMainView
- (void)handlePostedMessage:(id)_message {
	_UIPostedMessage *message = (_UIPostedMessage *) _message;
	_UIWindowInputEvent(_uiWindow, message->message, 0, message->dp);
	UI_FREE(message);
}

- (BOOL)acceptsFirstResponder {
	return YES;
}

- (void)onMenuItemSelected:(NSMenuItem *)menuItem {
	((void (*)(void *)) ui.menuData[menuItem.tag * 2 + 0])(ui.menuData[menuItem.tag * 2 + 1]);
}

- (void)drawRect:(NSRect)dirtyRect {
	const unsigned char *data = (const unsigned char *) _uiWindow->bits;
	NSDrawBitmap(NSMakeRect(0, 0, _uiWindow->width, _uiWindow->height), _uiWindow->width, _uiWindow->height,
			8 /* bits per channel */, 4 /* channels per pixel */,
			32 /* bits per pixel */, 4 * _uiWindow->width /* bytes per row */, NO /* planar */, YES /* has alpha */,
			NSDeviceRGBColorSpace /* color space */, &data /* data */);
}

- (void)eventCommon:(NSEvent *)event {
	NSPoint cursor = [self convertPoint:[event locationInWindow] fromView:nil];
	_uiWindow->cursorX = cursor.x, _uiWindow->cursorY = _uiWindow->height - cursor.y - 1;
	_uiWindow->ctrl = event.modifierFlags & NSEventModifierFlagCommand;
	_uiWindow->shift = event.modifierFlags & NSEventModifierFlagShift;
	_uiWindow->alt = event.modifierFlags & NSEventModifierFlagOption;
}

- (void)keyDown:(NSEvent *)event {
	[self eventCommon:event];
	char *text = _UIUTF8StringFromNSString(event.characters);
	UIKeyTyped m = { .code = _UICocoaRemapKey(event.keyCode), .text = text, .textBytes = (int) strlen(text) };
	_UIWindowInputEvent(_uiWindow, UI_MSG_KEY_TYPED, 0, &m);
	UI_FREE(text);
}

- (void)keyUp:(NSEvent *)event {
	[self eventCommon:event];
	UIKeyTyped m = { .code = _UICocoaRemapKey(event.keyCode) };
	_UIWindowInputEvent(_uiWindow, UI_MSG_KEY_RELEASED, 0, &m);
}

- (void)mouseMoved:(NSEvent *)event {
	[self eventCommon:event];
	_UIWindowInputEvent(_uiWindow, UI_MSG_MOUSE_MOVE, 0, 0);
}

- (void)mouseExited:(NSEvent *)event { [self mouseMoved:event]; }
- (void)flagsChanged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)mouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)rightMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)otherMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }

- (void)mouseDown:(NSEvent *)event {
	[self eventCommon:event];
	_UIWindowInputEvent(_uiWindow, UI_MSG_LEFT_DOWN, 0, 0);
}

- (void)mouseUp:(NSEvent *)event {
	[self eventCommon:event];
	_UIWindowInputEvent(_uiWindow, UI_MSG_LEFT_UP, 0, 0);
}

- (void)rightMouseDown:(NSEvent *)event {
	[self eventCommon:event];
	_UIWindowInputEvent(_uiWindow, UI_MSG_RIGHT_DOWN, 0, 0);
}

- (void)rightMouseUp:(NSEvent *)event {
	[self eventCommon:event];
	_UIWindowInputEvent(_uiWindow, UI_MSG_RIGHT_UP, 0, 0);
}

- (void)otherMouseDown:(NSEvent *)event {
	[self eventCommon:event];
	_UIWindowInputEvent(_uiWindow, UI_MSG_MIDDLE_DOWN, 0, 0);
}

- (void)otherMouseUp:(NSEvent *)event {
	[self eventCommon:event];
	_UIWindowInputEvent(_uiWindow, UI_MSG_MIDDLE_UP, 0, 0);
}

- (void)scrollWheel:(NSEvent *)event {
	[self eventCommon:event];
	_UIWindowInputEvent(_uiWindow, UI_MSG_MOUSE_WHEEL, -3 * event.deltaY, 0);
	_UIWindowInputEvent(_uiWindow, UI_MSG_MOUSE_MOVE, 0, 0);
}

// TODO Animations.
// TODO Drag and drop.
// TODO Reporting window close.

@end

int _UIWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DEALLOCATE) {
		UIWindow *window = (UIWindow *) element;
		_UIWindowDestroyCommon(window);
		[window->window close];
	}

	return _UIWindowMessageCommon(element, message, di, dp);
}

UIWindow *UIWindowCreate(UIWindow *owner, uint32_t flags, const char *cTitle, int _width, int _height) {
	_UIMenusClose();
	UIWindow *window = (UIWindow *) UIElementCreate(sizeof(UIWindow), NULL, flags | UI_ELEMENT_WINDOW, _UIWindowMessage, "Window");
	_UIWindowAdd(window);
	if (owner) window->scale = owner->scale;

	NSRect frame = NSMakeRect(0, 0, _width ?: 800, _height ?: 600);
	NSWindowStyleMask styleMask = NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskTitled;
	NSWindow *nsWindow = [[NSWindow alloc] initWithContentRect:frame styleMask:styleMask backing:NSBackingStoreBuffered defer:NO];
	[nsWindow center];
	[nsWindow setTitle:@(cTitle ?: "untitled")];
	UICocoaWindowDelegate *delegate = [UICocoaWindowDelegate alloc];
	[delegate setUiWindow:window];
	nsWindow.delegate = delegate;
	UICocoaMainView *view = [UICocoaMainView alloc];
	window->window = nsWindow;
	window->view = view;
	window->width = frame.size.width;
	window->height = frame.size.height;
	window->bits = (uint32_t *) UI_REALLOC(window->bits, window->width * window->height * 4);
	window->e.bounds = UI_RECT_2S(window->width, window->height);
	window->e.clip = UI_RECT_2S(window->width, window->height);
	[view setUiWindow:window];
	[view initWithFrame:frame];
	nsWindow.contentView = view;
	[view addTrackingArea:[[NSTrackingArea alloc] initWithRect:frame
		options:NSTrackingMouseMoved|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect owner:view userInfo:nil]];
	[nsWindow setInitialFirstResponder:view];
	[nsWindow makeKeyAndOrderFront:delegate];

	// TODO UI_WINDOW_MAXIMIZE.

	return window;
}

void _UIClipboardWriteText(UIWindow *window, char *text) {
	// TODO Clipboard support.
}

char *_UIClipboardReadTextStart(UIWindow *window, size_t *bytes) {
	// TODO Clipboard support.
	return NULL;
}

void _UIClipboardReadTextEnd(UIWindow *window, char *text) {
	UI_FREE(text);
}

void UIInitialise() {
	_UIInitialiseCommon();
}

void _UIWindowSetCursor(UIWindow *window, int cursor) {
	if      (cursor == UI_CURSOR_TEXT)          [[NSCursor IBeamCursor] set];
	else if (cursor == UI_CURSOR_SPLIT_V)       [[NSCursor resizeUpDownCursor] set];
	else if (cursor == UI_CURSOR_SPLIT_H)       [[NSCursor resizeLeftRightCursor] set];
	else if (cursor == UI_CURSOR_FLIPPED_ARROW) [[NSCursor pointingHandCursor] set];
	else if (cursor == UI_CURSOR_CROSS_HAIR)    [[NSCursor crosshairCursor] set];
	else if (cursor == UI_CURSOR_HAND)          [[NSCursor pointingHandCursor] set];
	else                                        [[NSCursor arrowCursor] set];
}

void _UIWindowEndPaint(UIWindow *window, UIPainter *painter) {
	for (int y = painter->clip.t; y < painter->clip.b; y++) {
		for (int x = painter->clip.l; x < painter->clip.r; x++) {
			uint32_t *p = &painter->bits[y * painter->width + x];
			*p = 0xFF000000 | (*p & 0xFF00) | ((*p & 0xFF0000) >> 16) | ((*p & 0xFF) << 16);
		}
	}

	[(UICocoaMainView *)window->view setNeedsDisplayInRect:((UICocoaMainView *)window->view).frame];
}

void _UIWindowGetScreenPosition(UIWindow *window, int *x, int *y) {
	NSPoint point = [window->window convertPointToScreen:NSMakePoint(0, 0)];
	*x = point.x, *y = point.y;
}

UIMenu *UIMenuCreate(UIElement *parent, uint32_t flags) {
	// TODO Fix the vertical position.

	if (parent->parent) {
		UIRectangle screenBounds = UIElementScreenBounds(parent);
		ui.menuX = screenBounds.l;
		ui.menuY = screenBounds.b;
	} else {
		_UIWindowGetScreenPosition(parent->window, &ui.menuX, &ui.menuY);
		ui.menuX += parent->window->cursorX;
		ui.menuY += parent->window->cursorY;
	}

	ui.menuIndex = 0;
	ui.menuWindow = parent->window;

	NSMenu *menu = [[NSMenu alloc] init];
	[menu setAutoenablesItems:NO];
	return menu;
}

void UIMenuAddItem(UIMenu *menu, uint32_t flags, const char *label, ptrdiff_t labelBytes, void (*invoke)(void *cp), void *cp) {
	if (ui.menuIndex == 128) return;
	ui.menuData[ui.menuIndex * 2 + 0] = (void *) invoke;
	ui.menuData[ui.menuIndex * 2 + 1] = cp;
	NSString *title = [[NSString alloc] initWithBytes:label length:(labelBytes == -1 ? strlen(label) : labelBytes) encoding:NSUTF8StringEncoding];
	NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:@selector(onMenuItemSelected:) keyEquivalent:@""];
	item.tag = ui.menuIndex++;
	if (flags & UI_BUTTON_CHECKED) [item setState:NSControlStateValueOn];
	[item setEnabled:((flags & UI_ELEMENT_DISABLED) ? NO : YES)];
	[item setTarget:(UICocoaMainView *)ui.menuWindow->view];
	[menu addItem:item];
	[title release];
	[item release];
}

void UIMenuShow(UIMenu *menu) {
	[menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(ui.menuX, ui.menuY) inView:nil];
	[menu release];
}

void UIWindowPack(UIWindow *window, int _width) {
	int width = _width ? _width : UIElementMessage(window->e.children[0], UI_MSG_GET_WIDTH, 0, 0);
	int height = UIElementMessage(window->e.children[0], UI_MSG_GET_HEIGHT, width, 0);
	[window->window setContentSize:NSMakeSize(width, height)];
}

bool _UIMessageLoopSingle(int *result) {
	// TODO Modal dialog support.
	return false;
}

void UIWindowPostMessage(UIWindow *window, UIMessage _message, void *dp) {
	_UIPostedMessage *message = (_UIPostedMessage *) UI_MALLOC(sizeof(_UIPostedMessage));
	message->message = _message;
	message->dp = dp;
	[(UICocoaMainView*)window->view performSelectorOnMainThread:@selector(handlePostedMessage:) withObject:(id)message waitUntilDone:NO];
}

int UICocoaMain(int argc, char **argv, int (*appMain)(int, char **)) {
	_cocoaArgc = argc, _cocoaArgv = argv, _cocoaAppMain = appMain;
	NSApplication *application = [NSApplication sharedApplication];
	application.delegate = [[UICocoaApplicationDelegate alloc] init];
	return NSApplicationMain(argc, (const char **) argv);
}

#endif

#endif
