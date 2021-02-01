// TODO General editing:
// !	Visual mode: v, V, ctrl+v
// !	Searching: /, *, n, N, %, t, f
// !	Clipboard: y, ", p, P, ;
// 	Join "J" command: modifying whitespace.
// 	Tabs character are incorrectly sized when horizontally scrolled.
//
// TODO Extended commands:
// !	Edit
// !	Tab-completion.
//
// TODO Features:
// !	Multiple tabs.
// !	Autocomplete.
// 	Import vim colorscheme.
// 	Syntax highlighting identifiers.
// 	Browsing directories.
// 	Function list.
// 	Ctags lookup.
// 	gf integration.
// 	Evaluate expression.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define BRAND "never"

#define FONT_PATH "/usr/share/fonts/adobe-source-code-pro/SourceCodePro-Semibold.otf"
#define FONT_SIZE (13)

void DrawGlyph(int x0, int y0, int c, uint32_t color);
void DrawBlock(int x0, int y0, int xc, uint32_t color);
void DrawCaret(int x0, int y0, uint32_t color);
void DrawSync();
char *FileLoad(const char *path, int *length);
bool FileExists(const char *path);
bool FileSave(const char *path, char *buffer, int length);
void FileFree(char *buffer);

///////////////////////////////////////////////////

#ifdef __linux__

#include <sys/time.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/ftbitmap.h>

#define KEYCODE_A XK_a
#define KEYCODE_BACKSPACE XK_BackSpace
#define KEYCODE_DELETE XK_Delete
#define KEYCODE_DOWN XK_Down
#define KEYCODE_END XK_End
#define KEYCODE_ENTER XK_Return
#define KEYCODE_ESCAPE XK_Escape
#define KEYCODE_F1 XK_F1
#define KEYCODE_F10 XK_F10
#define KEYCODE_F11 XK_F11
#define KEYCODE_F12 XK_F12
#define KEYCODE_F2 XK_F2
#define KEYCODE_F3 XK_F3
#define KEYCODE_F4 XK_F4
#define KEYCODE_F5 XK_F5
#define KEYCODE_F6 XK_F6
#define KEYCODE_F7 XK_F7
#define KEYCODE_F8 XK_F8
#define KEYCODE_F9 XK_F9
#define KEYCODE_HOME XK_Home
#define KEYCODE_LEFT XK_Left
#define KEYCODE_RIGHT XK_Right
#define KEYCODE_SPACE XK_space
#define KEYCODE_TAB XK_Tab
#define KEYCODE_UP XK_Up
#define KEYCODE_0 XK_0

#endif

#define KEYCODE_CTRL (1 << 30)
#define KEYCODE_SHIFT (1 << 29)
#define KEYCODE_ALT (1 << 28)

#define KEYCODE_LETTER(x) (KEYCODE_A + (x) - 'A')
#define KEYCODE_DIGIT(x) (KEYCODE_0 + (x) - '0')

///////////////////////////////////////////////////

#define COLOR_BACKGROUND (0x303036)
#define COLOR_TEXT (0xFFFFFF)
#define COLOR_LINE_NUMBERS (0x141415)
#define COLOR_LINE_NUMBERS_TEXT (0x606067)
#define COLOR_COMMENT (0xFFB4B4B4)
#define COLOR_STRING (0xFFF5DDD1)
#define COLOR_NUMBER (0xFFD1F5DD)
#define COLOR_OPERATOR (0xFFF5F3D1)
#define COLOR_PREPROCESSOR (0xFFF5F3D1)

///////////////////////////////////////////////////

#include "stb_ds.h"

#define ALLOC(x, y) ((y) ? calloc(1, (x)) : malloc((x)))
#define COPY(x, y, z) (memcpy((x), (y), (z)))
#define STRLEN(x) (strlen((x)))
#define EXIT() (exit(0))

///////////////////////////////////////////////////

typedef struct Input {
	char text[11];
	uint8_t textBytes;
	int code;
} Input;

typedef struct Line {
	char *text;
	bool redraw;
} Line;

typedef struct Caret {
	int line, offset;
} Caret;

typedef struct Buffer {
	Line *lines;
	Caret marks[26];
	char *path;
	uint64_t modified;
} Buffer;

typedef struct View {
	Buffer *buffer;
	int scrollX, scrollY;
	Caret caret;
	int columns, rows;
} View;

#define STEP_MARKER      (0)
#define STEP_INSERT      (1)
#define STEP_DELETE      (2)
#define STEP_INSERT_LINE (3)
#define STEP_DELETE_LINE (4)
#define STEP_JOIN        (5)

typedef struct Step {
	int type;
	Caret caretBefore, caretAfter;
	int d1;
	char *text;
} Step;

#define MODE_NORMAL (0)
#define MODE_INSERT (1)

struct {
	View *view;
	int columns, rows;
	int mode;
	Input *command;
	bool redrawAll;
	int moveColumn, oldMoveColumn;
	Input *macros[27];
	int recording;
	Step *undo;
	int undoIndex, undoBase;
	char message[64];
} state;

///////////////////////////////////////////////////

bool CharIsAlpha(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool CharIsDigit(char c) {
	return c >= '0' && c <= '9';
}

bool CharIsSpace(char c) {
	return c == '\t' || c == ' ';
}

bool CharIsAlphaOrDigitOrUnderscore(char c) {
	return CharIsAlpha(c) || CharIsDigit(c) || c == '_';
}

///////////////////////////////////////////////////

int ColumnToIndex(Line *line, int column) {
	int p = 0;

	for (int i = 0; i < arrlen(line->text); i++) {
		if (column <= p) {
			return i;
		} else if (line->text[i] == '\t') {
			p = (p + 8) & ~7;
		} else {
			p++;
		}
	}

	return arrlen(line->text);
}

int IndexToColumn(Line *line, int index) {
	int p = 0;

	for (int i = 0; i < index; i++) {
		if (line->text[i] == '\t') {
			p = (p + 8) & ~7;
		} else {
			p++;
		}
	}

	return p;
}

void DrawLine(int lineIndex) {
	Line *line = state.view->buffer->lines + lineIndex;
	Caret *caret = &state.view->caret;
	int row = lineIndex - state.view->scrollY;
	DrawBlock(0, row, state.columns, COLOR_BACKGROUND);

	int index = ColumnToIndex(line, state.view->scrollX);

	int lexState = 0;
	bool inComment = false, inIdentifier = false, inChar = false, startedString = false;
	uint32_t last = 0;

	for (int j = 0; j < state.view->columns; j++, index++) {
		if (index > arrlen(line->text)) {
			break;
		}

		char c = index >= arrlen(line->text) ? 0 : line->text[index];
		char c1 = index >= arrlen(line->text) - 1 ? 0 : line->text[index + 1];
		last <<= 8;
		last |= c;

		if (lexState == 4) {
			lexState = 0;
		} else if (lexState == 1) {
			if ((last & 0xFF0000) == ('*' << 16) && (last & 0xFF00) == ('/' << 8) && inComment) {
				lexState = 0, inComment = false;
			}
		} else if (lexState == 3) {
			if (!CharIsAlphaOrDigitOrUnderscore(c)) {
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
			} else if (c == '/' && c1 == '/') {
				lexState = 1;
			} else if (c == '/' && c1 == '*') {
				lexState = 1, inComment = true;
			} else if (c == '"') {
				lexState = 2;
				inChar = false;
				startedString = true;
			} else if (c == '\'') {
				lexState = 2;
				inChar = true;
				startedString = true;
			} else if (CharIsDigit(c) && !inIdentifier) {
				lexState = 3;
			} else if (!CharIsAlphaOrDigitOrUnderscore(c)) {
				lexState = 4;
				inIdentifier = false;
			} else {
				inIdentifier = true;
			}
		}

		uint32_t colors[] = { COLOR_TEXT, COLOR_COMMENT, COLOR_STRING, COLOR_NUMBER, COLOR_OPERATOR, COLOR_PREPROCESSOR };
		uint32_t color = colors[lexState];

		if (caret->line == lineIndex && caret->offset == index && (!arrlen(state.command) || state.command[0].text[0] != '\\')) {
			if (state.mode == MODE_INSERT) {
				DrawCaret(j + 5, row, color);
			} else {
				DrawBlock(j + 5, row, 1, color);
				color = COLOR_BACKGROUND;
			}
		}

		if (index < arrlen(line->text)) {
			if (line->text[index] == '\t') {
				j = ((j + state.view->scrollX + 8) & ~7) - 1 - state.view->scrollX;
			} else {
				DrawGlyph(j + 5, row, line->text[index], color);
			}
		}
	}

	DrawBlock(0, row, 5, COLOR_LINE_NUMBERS);

	{
		char buffer[5];
		int length = snprintf(buffer, 5, "%4d", lineIndex + 1);

		for (int i = 0; i < length; i++) {
			DrawGlyph(i, row, buffer[i], COLOR_LINE_NUMBERS_TEXT);
		}
	}
}

void DrawStatusBar() {
	char buffer[64];
	int length = snprintf(buffer, 64, "%d,%d", state.view->caret.line + 1, state.view->caret.offset);
	DrawBlock(0, state.rows - 1, state.columns, COLOR_BACKGROUND);

	for (int i = 0; i < length; i++) {
		DrawGlyph(i + state.columns - length - 1, state.rows - 1, buffer[i], COLOR_TEXT);
	}

	if (state.message[0]) {
		for (int i = 0; state.message[i]; i++) {
			DrawGlyph(i, state.rows - 1, state.message[i], COLOR_NUMBER);
		}
	} else {
		for (int i = 0; i < arrlen(state.command); i++) {
			DrawGlyph(i, state.rows - 1, state.command[i].text[0], COLOR_TEXT);
		}

		if (arrlen(state.command)) {
			DrawCaret(arrlen(state.command), state.rows - 1, COLOR_TEXT);
		}
	}
}

void Draw() {
	Buffer *buffer = state.view->buffer;

	for (int i = 0; i < state.view->rows; i++) {
		int line = i + state.view->scrollY;

		if (line < 0 || line >= arrlen(buffer->lines)) {
			if (state.redrawAll) {
				DrawBlock(0, i, state.columns, COLOR_BACKGROUND);
			}
		} else {
			if (state.redrawAll || buffer->lines[line].redraw) {
				DrawLine(line);
				buffer->lines[line].redraw = false;
			}
		}
	}

	DrawSync();
	DrawStatusBar();
	state.redrawAll = false;
}

///////////////////////////////////////////////////

#define MOVE_FORWARD (false)
#define MOVE_BACKWARD (true)

#define MOVE_SINGLE (0)
#define MOVE_WORD (1)
#define MOVE_WORD_WEAK (2)
#define MOVE_LINE (3)
#define MOVE_VERTICAL (4)
#define MOVE_LARGE_VERTICAL (5)
#define MOVE_ALL (6)
#define MOVE_WRAP (1 << 16)

bool MoveCaret(Caret *caret, bool backward, int type) {
	bool first = true, wrap = type & MOVE_WRAP;
	type &= 0xFFFF;
	Line *line = state.view->buffer->lines + caret->line;

	if (type == MOVE_LARGE_VERTICAL) {
		bool moved = false;

		for (int i = 0; i < 20; i++) {
			moved = MoveCaret(caret, backward, MOVE_VERTICAL) || moved;
		}

		return moved;
	} else if (type == MOVE_VERTICAL) {
		if ((backward && caret->line == 0) || (!backward && caret->line == arrlen(state.view->buffer->lines) - 1)) {
			return false;
		}

		int column = state.oldMoveColumn != -1 ? state.oldMoveColumn : IndexToColumn(line, caret->offset);
		caret->line += backward ? -1 : 1;
		line = state.view->buffer->lines + caret->line;
		caret->offset = column == -1 ? arrlen(line->text) : ColumnToIndex(line, column);
		state.moveColumn = column;
		return true;
	} else if (type == MOVE_LINE) {
		if (backward && caret->offset > 0) {
			caret->offset = 0;
		} else if (!backward && caret->offset != arrlen(line->text)) {
			caret->offset = arrlen(line->text);
		} else {
			return false;
		}

		return true;
	} else if (type == MOVE_ALL) {
		int lastLine = arrlen(state.view->buffer->lines) - 1;
		int lastLineLength = arrlen(arrlast(state.view->buffer->lines).text);

		if (backward && (caret->line || caret->offset)) {
			caret->line = 0;
			caret->offset = 0;
		} else if (!backward && (caret->line != lastLine || caret->offset != lastLineLength)) {
			caret->line = lastLine;
			caret->offset = lastLineLength;
		} else {
			return false;
		}

		return true;
	}

	while (true) {
		if (backward) {
			if (caret->offset > 0) {
				caret->offset--;
			} else if (wrap && caret->line > 0 && first) {
				line--;
				caret->line--;
				caret->offset = arrlen(line->text);
				return true;
			} else {
				return first;
			}
		} else if (!backward) {
			if (caret->offset < arrlen(line->text)) {
				caret->offset++;
			} else if (wrap && caret->line != arrlen(state.view->buffer->lines) - 1 && first) {
				line++;
				caret->line++;
				caret->offset = 0;
				return true;
			} else {
				return first;
			}
		} else {
			return first;
		}

		first = false;

		if (type == MOVE_SINGLE) {
			return true;
		} else if (caret->offset != arrlen(line->text) && caret->offset != 0) {
			char c1 = line->text[caret->offset - 1];
			char c2 = line->text[caret->offset];
			bool b1 = CharIsAlphaOrDigitOrUnderscore(c1);
			bool b2 = CharIsAlphaOrDigitOrUnderscore(c2);
			bool n = type == MOVE_WORD_WEAK ? !backward : backward;

			if ((!b1 && b2 && !n) || (b1 && !b2 && n)) {
				return true;
			}

			if (!CharIsSpace(c2) && !b2 && c1 != c2) {
				return true;
			}
		}
	}
}

///////////////////////////////////////////////////

Buffer *BufferLoadFromFile(const char *path) {
	Buffer *buffer = ALLOC(sizeof(Buffer), true);

	int bytes;
	char *text = FileLoad(path, &bytes);

	if (!text) {
		const char *errorMessage = "Error: could not load the file.";
		Line line = { 0 };
		arrinsn(line.text, 0, STRLEN(errorMessage));
		COPY(line.text, errorMessage, STRLEN(errorMessage));
		arrput(state.view->buffer->lines, line);
		arrput(buffer->lines, line);
	} else {
		int lineStart = 0;

		for (int i = 0; i <= bytes; i++) {
			if (i == bytes || text[i] == '\n') {
				Line line = { 0 };

				if (i != lineStart) {
					arrinsn(line.text, 0, i - lineStart);
					COPY(line.text, text + lineStart, i - lineStart);
				}

				lineStart = i + 1;
				arrput(buffer->lines, line);
			}
		}
	}

	FileFree(text);

	return buffer;
}

bool BufferWrite(const char *path, bool canOverwrite) {
	Buffer *buffer = state.view->buffer;

	if (!canOverwrite && path && *path && FileExists(path)) {
		const char *message = "Error: the file already exists.";
		COPY(state.message, message, STRLEN(message));
		return false;
	} else if (!buffer->path && (!path || !(*path))) {
		const char *message = "Error: specify a file to write to.";
		COPY(state.message, message, STRLEN(message));
		return false;
	}

	char *b = NULL;
	int length = 0;

	for (int i = 0; i < arrlen(buffer->lines); i++) {
		length += 1 + arrlen(buffer->lines[i].text);
	}

	arrsetlen(b, (size_t) length);
	length = 0;

	for (int i = 0; i < arrlen(buffer->lines); i++) {
		int l = arrlen(buffer->lines[i].text);
		COPY(b + length, buffer->lines[i].text, l);
		b[length + l] = '\n';
		length += l + 1;
	}

	bool success = FileSave((path && *path) ? path : buffer->path, b, length);
	arrfree(b);

	if (!success) {
		const char *message = "Error: the file could not be accessed.";
		COPY(state.message, message, STRLEN(message));
		return false;
	} else if (path && *path) {
		arrfree(buffer->path);
		arrsetlen(buffer->path, STRLEN(path) + 1);
		COPY(buffer->path, path, STRLEN(path) + 1);
	}

	buffer->modified = 0;
	return true;
}

///////////////////////////////////////////////////

void _Insert(const char *text, int textBytes) {
	if (!textBytes) return;
	Buffer *buffer = state.view->buffer;
	Caret *caret = &state.view->caret;
	Line *line = buffer->lines + caret->line;
	line->redraw = true;
	Step step = { 0 };
	step.type = STEP_DELETE;
	step.caretAfter = *caret;
	arrinsn(line->text, caret->offset, textBytes);
	COPY(line->text + caret->offset, text, textBytes);
	caret->offset += textBytes;
	buffer->modified++;
	step.caretBefore = *caret;
	step.d1 = textBytes;

	if (arrlen(state.undo) > 2 && arrlast(state.undo).type == STEP_MARKER) {
		Step *merge = state.undo + arrlen(state.undo) - 2;
	
		if (merge->type == STEP_DELETE && merge->caretBefore.line == step.caretAfter.line 
				&& merge->caretBefore.offset == step.caretAfter.offset) {
			merge->d1 += step.d1;
			merge->caretBefore = step.caretBefore;
			return;
		}
	}

	arrput(state.undo, step);
}

void _InsertLine() {
	Buffer *buffer = state.view->buffer;
	Caret *caret = &state.view->caret;
	Step step = { 0 };
	step.type = STEP_JOIN;
	step.caretBefore = *caret;
	step.caretAfter = *caret;
	caret->line++;
	Line _line = { 0 };
	arrins(buffer->lines, caret->line, _line);
	Line *oldLine = buffer->lines + caret->line - 1;
	Line *newLine = buffer->lines + caret->line;
	arrsetlen(newLine->text, arrlenu(oldLine->text) - caret->offset);
	COPY(newLine->text, oldLine->text + caret->offset, arrlen(oldLine->text) - caret->offset);
	arrsetlen(oldLine->text, (size_t) caret->offset);
	caret->offset = 0;
	state.redrawAll = true;

	for (int i = 0; i < 26; i++) {
		if (buffer->marks[i].line == caret->line - 1) {
			buffer->marks[i].offset = 0;
		} else if (buffer->marks[i].line >= caret->line) {
			buffer->marks[i].line++;
		} 
	}

	buffer->modified++;
	arrput(state.undo, step);
}

void _Delete(int line, int from, int to) {
	if (from == to) return;
	Buffer *buffer = state.view->buffer;
	if (line >= arrlen(buffer->lines)) return;
	Step step = { 0 };
	step.type = STEP_INSERT;
	step.caretAfter = state.view->caret;
	arrsetlen(step.text, (size_t) (to - from));
	COPY(step.text, buffer->lines[line].text + from, to - from);
	arrdeln(buffer->lines[line].text, from, to - from);
	buffer->lines[line].redraw = true;

	for (int i = 0; i < 26; i++) {
		if (buffer->marks[i].line == line && buffer->marks[i].offset >= from && buffer->marks[i].offset <= to) {
			buffer->marks[i].offset = from;
		} 
	}

	buffer->modified++;
	step.caretBefore.line = line;
	step.caretBefore.offset = from;

	if (arrlen(state.undo) > 2 && arrlast(state.undo).type == STEP_MARKER) {
		Step *merge = state.undo + arrlen(state.undo) - 2;
	
		if (merge->type == STEP_INSERT && merge->caretBefore.line == step.caretAfter.line 
				&& merge->caretBefore.offset == step.caretAfter.offset) {
			int end = arrlen(step.text);
			arrinsn(step.text, end, arrlen(merge->text));
			COPY(step.text + end, merge->text, arrlen(merge->text));
			arrfree(merge->text);
			merge->text = step.text;
			merge->caretBefore = step.caretBefore;
			return;
		}
	}

	arrput(state.undo, step);
}

void Join(int line) {
	Buffer *buffer = state.view->buffer;
	if (line >= arrlen(buffer->lines) - 1) return;
	Step step = { 0 };
	step.type = STEP_INSERT_LINE;
	step.caretAfter = state.view->caret;
	step.caretBefore.line = line;
	Line *previous = buffer->lines + line;
	Line *next = previous + 1;
	int oldLength = arrlen(previous->text);
	step.caretBefore.offset = oldLength;
	arrinsn(previous->text, oldLength, arrlen(next->text));
	COPY(previous->text + oldLength, next->text, arrlen(next->text));
	arrdel(buffer->lines, line + 1);
	state.redrawAll = true;

	for (int i = 0; i < 26; i++) {
		if (buffer->marks[i].line == line + 1) {
			buffer->marks[i].line--;
			buffer->marks[i].offset = 0;
		} else if (buffer->marks[i].line > line + 1) {
			buffer->marks[i].line--;
		}
	}

	buffer->modified++;
	arrput(state.undo, step);
}

void ApplyStep(Step step) {
	state.view->caret = step.caretBefore;

	if (step.type == STEP_JOIN) {
		Join(step.caretBefore.line);
	} else if (step.type == STEP_DELETE) {
		_Delete(step.caretAfter.line, step.caretAfter.offset, step.caretAfter.offset + step.d1);
	} else if (step.type == STEP_INSERT) {
		_Insert(step.text, arrlen(step.text));
	} else if (step.type == STEP_INSERT_LINE) {
		_InsertLine();
	}

	state.view->caret = step.caretAfter;
}

void Delete(Caret *anchor) {
	Buffer *buffer = state.view->buffer;
	Caret *caret = &state.view->caret;

	Caret from, to;

	if (anchor->line == caret->line && anchor->offset == caret->offset) {
		return;
	} else if (anchor->line > caret->line || (anchor->line == caret->line && anchor->offset > caret->offset)) {
		from = *caret, to = *anchor;
	} else {
		from = *anchor, to = *caret;
	}

	if (from.line == to.line) {
		_Delete(from.line, from.offset, to.offset);
	} else {
		_Delete(from.line, from.offset, arrlen(buffer->lines[from.line].text));

		for (int i = from.line + 1; i < to.line; i++) {
			_Delete(from.line + 1, 0, arrlen(buffer->lines[from.line + 1].text));
			Join(from.line);
		}

		_Delete(from.line + 1, 0, to.offset);
		Join(from.line);
	}

	*caret = from;
}

void Insert(const char *text, int textBytes) {
	if (!textBytes) return;
	int start = 0;

	for (int i = 0; i < textBytes; i++) {
		if (text[i] == '\r') {
			_Insert(text + start, i - start);
			start = i + 1;
			_InsertLine();
		}
	}

	_Insert(text + start, textBytes - start);
}

///////////////////////////////////////////////////

void IndentLine(int line) {
	int p = line - 1;
	int t = 0;

	Line *l = state.view->buffer->lines + line;

	for (int i = 0; i < arrlen(l->text); i++) {
		if (l->text[i] == '#') {
			t = -1;
		} else if (l->text[i] != '\t') {
			break;
		}
	}

	while (p >= 0 && t != -1) {
		Line *l = state.view->buffer->lines + p;

		if (!arrlen(l->text) || l->text[0] == '#') {
			p--;
			continue;
		} else {
			for (int i = 0; i < arrlen(l->text); i++) {
				if (l->text[i] == '\t') {
					t++;
				} else {
					break;
				}
			}

			if (p == line - 1 && arrlast(l->text) == '{') {
				t++;
			}

			break;
		}
	}

	Caret anchor = { line, 0 };
	state.view->caret.line = line;
	state.view->caret.offset = 0;

	for (int i = 0; i < arrlen(l->text); i++) {
		if (l->text[i] == '\t') {
			state.view->caret.offset++;
		} else {
			break;
		}
	}

	Delete(&anchor);
	const char *tabs = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
	if (t > (int) STRLEN(tabs)) t = STRLEN(tabs);
	if (t == -1) t = 0;
	Insert(tabs, t);
}

///////////////////////////////////////////////////

void ProcessInput(Input in);

bool RunCommand(Input *command) {
	Caret *caret = &state.view->caret;

	if (!command) return false;
	int c = command[0].code;

	if (c == KEYCODE_LETTER('I')) {
		state.mode = MODE_INSERT;
	} else if (c == KEYCODE_LETTER('A')) {
		MoveCaret(caret, MOVE_FORWARD, MOVE_SINGLE);
		state.mode = MODE_INSERT;
	} else if (c == (KEYCODE_LETTER('A') | KEYCODE_SHIFT)) {
		MoveCaret(caret, MOVE_FORWARD, MOVE_LINE);
		state.mode = MODE_INSERT;
	} else if (c == KEYCODE_LETTER('O')) {
		MoveCaret(caret, MOVE_FORWARD, MOVE_LINE);
		Insert("\r", 1);
		IndentLine(caret->line);
		state.mode = MODE_INSERT;
	} else if (c == (KEYCODE_LETTER('O') | KEYCODE_SHIFT)) {
		MoveCaret(caret, MOVE_BACKWARD, MOVE_VERTICAL);
		MoveCaret(caret, MOVE_FORWARD, MOVE_LINE);
		Insert("\r", 1);
		IndentLine(caret->line);
		state.mode = MODE_INSERT;
	} else if (c == KEYCODE_LETTER('H') || c == KEYCODE_LEFT) {
		MoveCaret(caret, MOVE_BACKWARD, MOVE_SINGLE);
	} else if (c == KEYCODE_LETTER('L') || c == KEYCODE_RIGHT) {
		MoveCaret(caret, MOVE_FORWARD, MOVE_SINGLE);
	} else if (c == KEYCODE_LETTER('W')) {
		MoveCaret(caret, MOVE_FORWARD, MOVE_WORD | MOVE_WRAP);
	} else if (c == KEYCODE_LETTER('E')) {
		MoveCaret(caret, MOVE_FORWARD, MOVE_SINGLE);
		MoveCaret(caret, MOVE_FORWARD, MOVE_WORD_WEAK | MOVE_WRAP);
		MoveCaret(caret, MOVE_BACKWARD, MOVE_SINGLE);
	} else if (c == KEYCODE_LETTER('B')) {
		MoveCaret(caret, MOVE_BACKWARD, MOVE_WORD_WEAK | MOVE_WRAP);
	} else if (c == (KEYCODE_LEFT | KEYCODE_CTRL)) {
		MoveCaret(caret, MOVE_BACKWARD, MOVE_WORD_WEAK | MOVE_WRAP);
	} else if (c == (KEYCODE_RIGHT | KEYCODE_CTRL)) {
		MoveCaret(caret, MOVE_FORWARD, MOVE_WORD_WEAK | MOVE_WRAP);
	} else if (c == KEYCODE_LETTER('J') || c == KEYCODE_DOWN) {
		MoveCaret(caret, MOVE_FORWARD, MOVE_VERTICAL);
	} else if (c == KEYCODE_LETTER('K') || c == KEYCODE_UP) {
		MoveCaret(caret, MOVE_BACKWARD, MOVE_VERTICAL);
	} else if (c == (KEYCODE_LETTER('D') | KEYCODE_CTRL)) {
		MoveCaret(caret, MOVE_FORWARD, MOVE_LARGE_VERTICAL);
	} else if (c == (KEYCODE_LETTER('U') | KEYCODE_CTRL)) {
		MoveCaret(caret, MOVE_BACKWARD, MOVE_LARGE_VERTICAL);
	} else if (c == (KEYCODE_LETTER('G') | KEYCODE_SHIFT)) {
		MoveCaret(caret, MOVE_FORWARD, MOVE_ALL);
	} else if (c == KEYCODE_HOME) {
		MoveCaret(caret, MOVE_BACKWARD, MOVE_LINE);
	} else if (c == KEYCODE_END) {
		MoveCaret(caret, MOVE_FORWARD, MOVE_LINE);
	} else if (c == (KEYCODE_HOME | KEYCODE_CTRL)) {
		MoveCaret(caret, MOVE_BACKWARD, MOVE_ALL);
	} else if (c == (KEYCODE_END | KEYCODE_CTRL)) {
		MoveCaret(caret, MOVE_FORWARD, MOVE_ALL);
	} else if (c == (KEYCODE_LETTER('J') | KEYCODE_SHIFT)) {
		MoveCaret(caret, MOVE_FORWARD, MOVE_LINE);
		Join(caret->line);
	} else if (command[0].text[0] == '=' && command[0].textBytes == 1) {
		Caret anchor = *caret;

		if (command[1].code) {
			if (command[1].text[0] == '=' && command[1].textBytes == 1) {
			} else if (RunCommand(command + 1)) {
				return true;
			}

			int from = anchor.line, to = caret->line;
			if (from > to) { int temp = from; from = to; to = temp; }

			for (int i = from; i <= to; i++) {
				IndentLine(i);
			}

			state.redrawAll = true;
		} else {
			return true;
		}
	} else if (c == KEYCODE_LETTER('D')) {
		Caret anchor = *caret;

		if (command[1].code) {
			if (command[1].code == KEYCODE_LETTER('D')) {
				MoveCaret(caret, MOVE_BACKWARD, MOVE_LINE);
				MoveCaret(&anchor, MOVE_BACKWARD, MOVE_LINE);
				MoveCaret(&anchor, MOVE_FORWARD, MOVE_VERTICAL);
			} else if (RunCommand(command + 1)) {
				return true;
			}

			if (command[1].code == KEYCODE_LETTER('E')) {
				MoveCaret(caret, MOVE_FORWARD, MOVE_SINGLE);
			}

			Delete(&anchor);
		} else {
			return true;
		}
	} else if (c == KEYCODE_LETTER('C')) {
		Caret anchor = *caret;

		if (command[1].code) {
			if (command[1].code == KEYCODE_LETTER('W')) {
				command[1].code = KEYCODE_LETTER('E');
			}

			if (RunCommand(command + 1)) {
				return true;
			}

			if (command[1].code == KEYCODE_LETTER('E')) {
				MoveCaret(caret, MOVE_FORWARD, MOVE_SINGLE);
			}

			Delete(&anchor);
			state.mode = MODE_INSERT;
		} else {
			return true;
		}
	} else if (c == KEYCODE_LETTER('R')) {
		if (command[1].code) {
			Caret anchor = *caret;
			MoveCaret(caret, MOVE_FORWARD, MOVE_SINGLE);
			Delete(&anchor);
			Insert(command[1].text, command[1].textBytes);
			MoveCaret(caret, MOVE_BACKWARD, MOVE_SINGLE);
		} else {
			return true;
		}
	} else if (c == KEYCODE_LETTER('G')) {
		if (command[1].code == KEYCODE_LETTER('G')) {
			MoveCaret(caret, MOVE_BACKWARD, MOVE_ALL);
		} else if (!command[1].code) {
			return true;
		}
	} else if (c == KEYCODE_DIGIT('0')) {
		MoveCaret(caret, MOVE_BACKWARD, MOVE_LINE);
	} else if (c == (KEYCODE_DIGIT('4') | KEYCODE_SHIFT)) {
		MoveCaret(caret, MOVE_FORWARD, MOVE_LINE);
	} else if (c >= KEYCODE_DIGIT('1') && c <= KEYCODE_DIGIT('9')) {
		int repeat = c - KEYCODE_DIGIT('0');
		int position = 1;

		while (true) {
			c = command[position].code;

			if (c >= KEYCODE_DIGIT('0') && c <= KEYCODE_DIGIT('9')) {
				repeat = (repeat * 10) + c - KEYCODE_DIGIT('0');
			} else if (c == 0) {
				return true;
			} else {
				break;
			}

			position++;
		}

		if (command[position].code == (KEYCODE_LETTER('G') | KEYCODE_SHIFT)) {
			caret->offset = 0;
			caret->line = repeat - 1;

			if (caret->line < 0) { 
				caret->line = 0;
			} else if (caret->line >= arrlen(state.view->buffer->lines)) {
				caret->line = arrlen(state.view->buffer->lines) - 1;
			}
		} else {
			while (repeat--) {
				if (RunCommand(command + position)) {
					return true;
				}
			}
		}
	} else if (c == (KEYCODE_DIGIT('2') | KEYCODE_SHIFT)) {
		if (command[1].code >= KEYCODE_LETTER('A') && command[1].code <= KEYCODE_LETTER('Z')) {
			Input *macro = state.macros[command[1].code - KEYCODE_LETTER('A') + 1];

			for (int i = 0; i < arrlen(macro); i++) {
				Input empty = arrpop(state.command);
				arrfree(state.command);
				ProcessInput(macro[i]);
				arrput(state.command, empty);
			}
		} else if (!command[1].code) {
			return true;
		}
	} else if (c == KEYCODE_LETTER('Q')) {
		if (command[1].code >= KEYCODE_LETTER('A') && command[1].code <= KEYCODE_LETTER('Z')) {
			state.recording = command[1].code - KEYCODE_LETTER('A') + 1;
			arrfree(state.macros[state.recording]);
		} else if (!command[1].code) {
			return true;
		}
	} else if (c == KEYCODE_LETTER('M')) {
		if (command[1].code >= KEYCODE_LETTER('A') && command[1].code <= KEYCODE_LETTER('Z')) {
			state.view->buffer->marks[command[1].code - KEYCODE_LETTER('A')] = *caret;
		} else if (!command[1].code) {
			return true;
		}
	} else if (command[0].textBytes == 1 && command[0].text[0] == '\'') {
		if (command[1].code >= KEYCODE_LETTER('A') && command[1].code <= KEYCODE_LETTER('Z')) {
			*caret = state.view->buffer->marks[command[1].code - KEYCODE_LETTER('A')];

			if (caret->line >= arrlen(state.view->buffer->lines)) {
				caret->line = arrlen(state.view->buffer->lines) - 1;
			}

			if (caret->offset > arrlen(state.view->buffer->lines[caret->line].text)) {
				caret->offset = arrlen(state.view->buffer->lines[caret->line].text);
			}
		} else if (!command[1].code) {
			return true;
		}
	} else if (command[0].textBytes == 1 && command[0].text[0] == '\\') {
		if (command[arrlen(command) - 2].text[0] == '\r') {
			char string[4096];
			int split = -1;

			for (int i = 1, j = 0; i < arrlen(command) - 2; i++) {
				if (command[i].textBytes) {
					if (command[i].text[0] == ' ' && split == -1) {
						split = j;
					} else {
						string[j++] = command[i].text[0];
						string[j] = 0;
					}
				}
			}

			if (split == -1) {
				split = STRLEN(string);
			}

			int argument = STRLEN(string) - split;
			const char *unexpectedArgument = "Error: unexpected argument.";

			if (string[0] == 'q' && split == 1) {
				if (state.view->buffer->modified) {
					const char *message = "Error: buffer has unsaved changes.";
					COPY(state.message, message, STRLEN(message) + 1);
				} else if (argument) {
					COPY(state.message, unexpectedArgument, STRLEN(unexpectedArgument) + 1);
				} else {
					EXIT();
				}
			} else if (string[0] == 'q' && string[1] == '!' && split == 2) {
				if (argument) {
					COPY(state.message, unexpectedArgument, STRLEN(unexpectedArgument) + 1);
				} else {
					EXIT();
				}
			} else if (string[0] == 'w' && split == 1) {
				BufferWrite(string + split, false);
			} else if (string[0] == 'w' && string[1] == '!' && split == 2) {
				BufferWrite(string + split, true);
			} else if (string[0] == 'w' && string[1] == 'q' && split == 2) {
				if (BufferWrite(string + split, false)) {
					EXIT();
				}
			} else {
				const char *message = "Error: unknown extended command.";
				COPY(state.message, message, STRLEN(message) + 1);
			}
		} else {
			return true;
		}
	}

	return false;
}

void ProcessInput(Input in) {
	Caret *caret = &state.view->caret;

	if (state.mode == MODE_NORMAL) {
		if (in.code == KEYCODE_ESCAPE) {
			arrfree(state.command);
		} else if (in.code == KEYCODE_BACKSPACE && arrlen(state.command)) {
			(void) arrpop(state.command);
		} else if (!arrlen(state.command) && in.textBytes == 1 && in.text[0] == '.') {
			RunCommand(state.macros[0]);
		} else {
			arrput(state.command, in);
			Input empty = { 0 };
			arrput(state.command, empty);

			uint64_t oldModified = state.view->buffer->modified;

			if (!RunCommand(state.command)) {
				if (oldModified < state.view->buffer->modified) {
					arrfree(state.macros[0]);
					state.macros[0] = state.command;
					state.command = NULL;
				} else {
					arrfree(state.command);
				}

				state.oldMoveColumn = state.moveColumn;
				state.moveColumn = -1;
			} else {
				(void) arrpop(state.command);
			}
		}
	} else if (state.mode == MODE_INSERT) {
		if (in.code == KEYCODE_ESCAPE) {
			state.mode = MODE_NORMAL;
		} else if (in.code == KEYCODE_BACKSPACE) {
			Caret anchor = *caret;
			MoveCaret(&anchor, MOVE_BACKWARD, MOVE_SINGLE | MOVE_WRAP);
			Delete(&anchor);
		} else if (in.code == (KEYCODE_BACKSPACE | KEYCODE_CTRL)) {
			Caret anchor = *caret;
			MoveCaret(&anchor, MOVE_BACKWARD, MOVE_WORD_WEAK | MOVE_WRAP);
			Delete(&anchor);
		} else if (in.code == KEYCODE_DELETE) {
			Caret anchor = *caret;
			MoveCaret(&anchor, MOVE_FORWARD, MOVE_SINGLE | MOVE_WRAP);
			Delete(&anchor);
		} else if (in.code == (KEYCODE_DELETE | KEYCODE_CTRL)) {
			Caret anchor = *caret;
			MoveCaret(&anchor, MOVE_FORWARD, MOVE_WORD_WEAK | MOVE_WRAP);
			Delete(&anchor);
		} else if (in.code == (KEYCODE_LEFT | KEYCODE_CTRL)) {
			MoveCaret(caret, MOVE_BACKWARD, MOVE_WORD_WEAK | MOVE_WRAP);
		} else if (in.code == KEYCODE_LEFT) {
			MoveCaret(caret, MOVE_BACKWARD, MOVE_SINGLE | MOVE_WRAP);
		} else if (in.code == (KEYCODE_RIGHT | KEYCODE_CTRL)) {
			MoveCaret(caret, MOVE_FORWARD, MOVE_WORD_WEAK | MOVE_WRAP);
		} else if (in.code == KEYCODE_RIGHT) {
			MoveCaret(caret, MOVE_FORWARD, MOVE_SINGLE | MOVE_WRAP);
		} else if (in.code == KEYCODE_UP) {
			MoveCaret(caret, MOVE_BACKWARD, MOVE_VERTICAL);
		} else if (in.code == KEYCODE_DOWN) {
			MoveCaret(caret, MOVE_FORWARD, MOVE_VERTICAL);
		} else if (in.textBytes) {
			Insert(in.text, in.textBytes);

			if (in.text[0] == '\r' && in.textBytes == 1) {
				IndentLine(caret->line);
			} else if (in.text[0] == '#' && in.textBytes == 1) {
				Line *l = state.view->buffer->lines + caret->line;

				for (int i = 0; i < arrlen(l->text); i++) {
					if (l->text[i] == '#') {
						IndentLine(caret->line);
						MoveCaret(caret, MOVE_FORWARD, MOVE_SINGLE);
						break;
					} else if (l->text[i] != '\t') {
						break;
					}
				}

			}
		}
	}
}

///////////////////////////////////////////////////

void EventInitialise() {
	state.view = ALLOC(sizeof(View), true);
	state.view->buffer = BufferLoadFromFile("never.c");

	Step stepMarker = { 0 };
	arrput(state.undo, stepMarker);
}

void EventInput(Input in) {
	state.message[0] = 0;
	
	int caretOldLine = state.view->caret.line;

	if (in.code == KEYCODE_LETTER('Q') && state.recording && state.mode == MODE_NORMAL && !arrlen(state.command)) {
		state.recording = 0;
		in.code = in.textBytes = 0;
	} else if (state.recording) {
		arrput(state.macros[state.recording], in);
	}

	bool fixUndoIndex = false;

	if (in.code == KEYCODE_LETTER('U') && state.mode == MODE_NORMAL && !arrlen(state.command) && state.undoIndex) {
		if (!state.undoBase) {
			state.undoBase = state.undoIndex;
		}

		while (true) {
			if (state.undo[--state.undoIndex].type == STEP_MARKER) break;
			ApplyStep(state.undo[state.undoIndex]);
		}
	} else if (in.code == (KEYCODE_LETTER('R') | KEYCODE_CTRL) && state.mode == MODE_NORMAL && !arrlen(state.command) && state.undoIndex < state.undoBase) {
		while (state.undo[++state.undoIndex].type != STEP_MARKER);

		int p = arrlen(state.undo) - 1;

		while (true) {
			if (state.undo[--p].type == STEP_MARKER) break;
			ApplyStep(state.undo[p]);
		}

		for (int i = p; i < arrlen(state.undo); i++) {
			arrfree(state.undo[i].text);
		}

		arrsetlen(state.undo, (size_t) p);
	} else {
		ProcessInput(in);
		fixUndoIndex = true;
	}

	if (arrlast(state.undo).type != STEP_MARKER) {
		Step step = { 0 };
		step.type = STEP_MARKER;
		arrput(state.undo, step);

		if (fixUndoIndex) {
			state.undoIndex = arrlen(state.undo) - 1;
			state.undoBase = 0;
		}
	}

#if 0
	printf("\n----------\n");
	for (int i = 0; i < arrlen(state.undo); i++) {
		Step step = state.undo[i];
		const char *types[] = { "marker", "insert", "delete", "insert_line", "delete_line", "join" };
		printf("%s, %d/%d, %d/%d, %d, '%.*s'", 
				types[step.type], step.caretBefore.line, step.caretBefore.offset, 
				step.caretAfter.line, step.caretAfter.offset, step.d1, 
				(int) arrlen(step.text), step.text);
		if (i == state.undoIndex) printf("  <--");
		if (i == state.undoBase) printf("  (B)");
		printf("\n");
	}
#endif

	Buffer *buffer = state.view->buffer;
	Caret *caret = &state.view->caret;

	int caretColumn = IndexToColumn(buffer->lines + caret->line, caret->offset);
	int caretPosition = caretColumn - state.view->scrollX;

	if (caretPosition < 0) {
		state.view->scrollX = caretColumn;
		state.redrawAll = true;
	} else if (caretPosition >= state.view->columns) {
		state.view->scrollX = caretColumn + 1 - state.view->columns;
		state.redrawAll = true;
	}

	caretPosition = caret->line - state.view->scrollY;

	if (caretPosition < 5) {
		state.view->scrollY = caret->line - 5;
		state.redrawAll = true;
	} else if (caretPosition >= state.view->rows - 5) {
		state.view->scrollY = caret->line + 6 - state.view->rows;
		state.redrawAll = true;
	}

	if (state.view->scrollY + state.view->rows > arrlen(buffer->lines)) {
		state.view->scrollY = arrlen(buffer->lines) - state.view->rows;
		state.redrawAll = true;
	}

	if (state.view->scrollY < 0) {
		state.view->scrollY = 0;
		state.redrawAll = true;
	}

	if (!state.redrawAll) {
		if (caretOldLine < arrlen(buffer->lines)) {
			buffer->lines[caretOldLine].redraw = true;
		}

		if (caret->line < arrlen(buffer->lines)) {
			buffer->lines[caret->line].redraw = true;
		}
	}

	Draw();
}

void EventResize(int columns, int rows) {
	if (!columns || !rows) return;
	state.columns = columns;
	state.rows = rows;
	state.view->columns = columns - 5 /* line numbers */;
	state.view->rows = rows - 1 /* status bar */;
	state.redrawAll = true;
	Draw();
}

void EventClose() {
	Input *in = NULL;
	Input i1 = { "\\", 1, KEYCODE_BACKSPACE };
	Input i2 = { "q", 1, KEYCODE_LETTER('Q') };
	Input i3 = { "\r", 1, KEYCODE_ENTER };
	Input i4 = { "", 0, 0 };
	arrput(in, i1);
	arrput(in, i2);
	arrput(in, i3);
	arrput(in, i4);
	RunCommand(in);
	arrfree(in);
	state.redrawAll = true;
	Draw();
}

///////////////////////////////////////////////////

#ifdef __linux__

struct {
	Window window;
	XImage *image;
	XIC xic;
	unsigned ctrlCode, shiftCode, altCode;
	Display *display;
	Visual *visual;
	XIM xim;
	Atom windowClosedID;
	FT_Face font;
	FT_Library ft;
	FT_Bitmap glyphs[128];
	bool glyphsRendered[128];
	int glyphOffsetsX[128], glyphOffsetsY[128];
	uint32_t *bits;
	int glyphWidth, glyphHeight;
	int windowWidth, windowHeight;
	int updateLeft, updateRight, updateTop, updateBottom;
	bool ctrl, alt, shift;
} platform;

void _DrawBlock(int x0, int y0, int xc, uint32_t color) {
	if (x0 < platform.updateLeft) platform.updateLeft = x0;
	if (x0 + xc > platform.updateRight) platform.updateRight = x0 + xc;
	if (y0 < platform.updateTop) platform.updateTop = y0;
	if (y0 + platform.glyphHeight > platform.updateBottom) platform.updateBottom = y0 + platform.glyphHeight;

	for (int line = y0; line < y0 + platform.glyphHeight; line++) {
		uint32_t *bits = platform.bits + line * platform.windowWidth + x0;
		int count = xc;

		while (count--) {
			*bits++ = color;
		}
	}
}

void DrawBlock(int x0, int y0, int xc, uint32_t color) {
	if (x0 < 0 || y0 < 0 || x0 + xc > platform.windowWidth / platform.glyphWidth 
			|| y0 >= platform.windowHeight / platform.glyphHeight) {
		return;
	}

	_DrawBlock(x0 * platform.glyphWidth, y0 * platform.glyphHeight, xc * platform.glyphWidth, color);
}

void DrawCaret(int x0, int y0, uint32_t color) {
	if (x0 < 0 || y0 < 0 || x0 + 3 > platform.windowWidth / platform.glyphWidth 
			|| y0 >= platform.windowHeight / platform.glyphHeight) {
		return;
	}

	_DrawBlock(x0 * platform.glyphWidth, y0 * platform.glyphHeight, 3, color);
}

void DrawGlyph(int x0, int y0, int c, uint32_t color) {
	if (x0 < 0 || y0 < 0 || x0 >= platform.windowWidth / platform.glyphWidth || y0 >= platform.windowHeight / platform.glyphHeight) {
		return;
	}

	x0 *= platform.glyphWidth;
	y0 *= platform.glyphHeight;

	if (x0 < platform.updateLeft) platform.updateLeft = x0;
	if (x0 + platform.glyphWidth > platform.updateRight) platform.updateRight = x0 + platform.glyphWidth;
	if (y0 < platform.updateTop) platform.updateTop = y0;
	if (y0 + platform.glyphHeight > platform.updateBottom) platform.updateBottom = y0 + platform.glyphHeight;

	if (c < 0 || c > 127) c = '?';

	if (!platform.glyphsRendered[c]) {
		FT_Load_Char(platform.font, c == 24 ? 0x2191 : c == 25 ? 0x2193 : c, FT_LOAD_DEFAULT);
		FT_Render_Glyph(platform.font->glyph, FT_RENDER_MODE_LCD);
		FT_Bitmap_Copy(platform.ft, &platform.font->glyph->bitmap, &platform.glyphs[c]);
		platform.glyphOffsetsX[c] = platform.font->glyph->bitmap_left;
		platform.glyphOffsetsY[c] = platform.font->size->metrics.ascender / 64 - platform.font->glyph->bitmap_top;
		platform.glyphsRendered[c] = true;
	}

	FT_Bitmap *bitmap = &platform.glyphs[c];
	x0 += platform.glyphOffsetsX[c], y0 += platform.glyphOffsetsY[c];

	for (int y = 0; y < (int) bitmap->rows; y++) {
		for (int x = 0; x < (int) bitmap->width / 3; x++) {
			uint32_t *destination = platform.bits + (x0 + x) + (y0 + y) * platform.windowWidth;
			uint32_t original = *destination;

			uint32_t ra = ((uint8_t *) bitmap->buffer)[x * 3 + y * bitmap->pitch + 0];
			uint32_t ga = ((uint8_t *) bitmap->buffer)[x * 3 + y * bitmap->pitch + 1];
			uint32_t ba = ((uint8_t *) bitmap->buffer)[x * 3 + y * bitmap->pitch + 2];
			ra += (ga - ra) / 2, ba += (ga - ba) / 2;
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
}

char *FileLoad(const char *path, int *length) {
	FILE *file = fopen(path, "rb");

	if (file) {
		fseek(file, 0, SEEK_END);
		size_t fileSize = ftell(file);
		fseek(file, 0, SEEK_SET);
		char *buffer = (char *) malloc(fileSize + 1);
		buffer[fileSize] = 0;
		fread(buffer, 1, fileSize, file);
		fclose(file);
		if (length) *length = fileSize;
		return buffer;
	} else {
		return NULL;
	}
}

void FileFree(char *buffer) {
	free(buffer);
}

bool FileExists(const char *path) {
	FILE *file = fopen(path, "rb");

	if (!file) {
		return false;
	}

	fclose(file);
	return true;
}

bool FileSave(const char *path, char *buffer, int length) {
	FILE *file = fopen(path, "wb");
	if (!file) return false;
	fwrite(buffer, 1, length, file);
	fclose(file);
	return true;
}

void DrawSync() {
	if (platform.updateLeft < platform.updateRight && platform.updateTop < platform.updateBottom) {
		XPutImage(platform.display, platform.window, DefaultGC(platform.display, 0), platform.image, 
				platform.updateLeft, platform.updateTop, platform.updateLeft, platform.updateTop,
				platform.updateRight - platform.updateLeft, platform.updateBottom - platform.updateTop);
	}

	platform.updateLeft = platform.windowWidth;
	platform.updateRight = 0;
	platform.updateTop = platform.windowHeight;
	platform.updateBottom = 0;
}

int main(int argc, char **argv) {
	(void) argc;
	(void) argv;

	FT_Init_FreeType(&platform.ft);
	FT_New_Face(platform.ft, FONT_PATH, 0, &platform.font); 
	FT_Set_Char_Size(platform.font, 0, FONT_SIZE * 64, 100, 100);
	FT_Load_Char(platform.font, 'a', FT_LOAD_DEFAULT);
	platform.glyphWidth = platform.font->glyph->advance.x / 64 - 1;
	platform.glyphHeight = (platform.font->size->metrics.ascender - platform.font->size->metrics.descender) / 64 - 1;

	XInitThreads();

	platform.display = XOpenDisplay(NULL);
	platform.visual = XDefaultVisual(platform.display, 0);
	platform.windowClosedID = XInternAtom(platform.display, "WM_DELETE_WINDOW", 0);

	XSetLocaleModifiers("");

	platform.xim = XOpenIM(platform.display, 0, 0, 0);

	if (!platform.xim) {
		XSetLocaleModifiers("@im=none");
		platform.xim = XOpenIM(platform.display, 0, 0, 0);
	}

	platform.window = XCreateWindow(platform.display, DefaultRootWindow(platform.display), 0, 0, 
			800, 600, 0, 0, InputOutput, CopyFromParent, 0, 0);
	XStoreName(platform.display, platform.window, BRAND);
	XSelectInput(platform.display, platform.window, SubstructureNotifyMask | ExposureMask | PointerMotionMask 
			| ButtonPressMask | ButtonReleaseMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask
			| EnterWindowMask | LeaveWindowMask | ButtonMotionMask | KeymapStateMask | FocusChangeMask);
	XMapRaised(platform.display, platform.window);
	XSetWMProtocols(platform.display, platform.window, &platform.windowClosedID, 1);
	platform.image = XCreateImage(platform.display, platform.visual, 24, ZPixmap, 0, NULL, 10, 10, 32, 0);
	platform.xic = XCreateIC(platform.xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, platform.window, XNFocusWindow, platform.window, NULL);

	EventInitialise();

	while (true) {
		XEvent event;
		XNextEvent(platform.display, &event);

#if 0
		struct timeval startTime;
		gettimeofday(&startTime, NULL);
#endif

		if (event.type == ClientMessage && (Atom) event.xclient.data.l[0] == platform.windowClosedID) {
			EventClose();
		} else if (event.type == Expose) {
			XPutImage(platform.display, platform.window, DefaultGC(platform.display, 0), platform.image, 0, 0, 0, 0, platform.windowWidth, platform.windowHeight);
		} else if (event.type == ConfigureNotify) {
			if (platform.windowWidth != event.xconfigure.width || platform.windowHeight != event.xconfigure.height) {
				platform.windowWidth = event.xconfigure.width;
				platform.windowHeight = event.xconfigure.height;
				platform.bits = (uint32_t *) realloc(platform.bits, platform.windowWidth * platform.windowHeight * 4);
				platform.image->width = platform.windowWidth;
				platform.image->height = platform.windowHeight;
				platform.image->bytes_per_line = platform.windowWidth * 4;
				platform.image->data = (char *) platform.bits;
				for (int i = 0; i < platform.windowWidth * platform.windowHeight; i++) platform.bits[i] = COLOR_BACKGROUND;
				EventResize(platform.windowWidth / platform.glyphWidth, platform.windowHeight / platform.glyphHeight);
			}
		} else if (event.type == KeyPress) {
			Input in = { 0 };
			KeySym symbol = NoSymbol;
			Status status;

			in.textBytes = Xutf8LookupString(platform.xic, &event.xkey, in.text, sizeof(in.text) - 1, &symbol, &status); 
			in.code = XLookupKeysym(&event.xkey, 0);

			if (symbol == XK_Control_L || symbol == XK_Control_R) {
				platform.ctrl = true;
				platform.ctrlCode = event.xkey.keycode;
			} else if (symbol == XK_Shift_L || symbol == XK_Shift_R) {
				platform.shift = true;
				platform.shiftCode = event.xkey.keycode;
			} else if (symbol == XK_Alt_L || symbol == XK_Alt_R) {
				platform.alt = true;
				platform.altCode = event.xkey.keycode;
			} else {
				if (platform.ctrl)  in.code |= KEYCODE_CTRL;
				if (platform.shift) in.code |= KEYCODE_SHIFT;
				if (platform.alt)   in.code |= KEYCODE_ALT;

				EventInput(in);
			}
		} else if (event.type == KeyRelease) {
			if (event.xkey.keycode == platform.ctrlCode) {
				platform.ctrl = false;
			} else if (event.xkey.keycode == platform.shiftCode) {
				platform.shift = false;
			} else if (event.xkey.keycode == platform.altCode) {
				platform.alt = false;
			}
		} else if (event.type == ButtonPress || event.type == ButtonRelease) {
			if (event.xbutton.button == 4) {
				state.redrawAll = true;
				state.view->scrollY -= 2;
				Draw();
			} else if (event.xbutton.button == 5) {
				state.redrawAll = true;
				state.view->scrollY += 2;
				Draw();
			}
		} else if (event.type == FocusIn) {
			platform.ctrl = platform.shift = platform.alt = false;
		}

		DrawSync();

#if 0
		struct timeval endTime;
		gettimeofday(&endTime, NULL);
		double delta = (endTime.tv_sec + endTime.tv_usec / 1000000.0) - (startTime.tv_sec + startTime.tv_usec / 1000000.0);
		printf("%fms\n", delta * 1000);
#endif
	}
}

#endif
