/////////////////////////////////////////////////////
// Source display:
/////////////////////////////////////////////////////

char autoPrintExpression[1024];
char autoPrintResult[1024];
int autoPrintExpressionLine;
int autoPrintResultLine;

int currentEndOfBlock;
int lastCursorX, lastCursorY;

Array<char *> inspectResults;
bool noInspectResults;
bool inInspectLineMode;
int inspectModeRestoreLine;
UIRectangle displayCurrentLineBounds;
const char *disassemblyCommand = "disas /s";

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
		if (strcmp(currentFile, file)) {
			reloadFile = true;
		}

		struct stat buf;

		if (!stat(file, &buf) && buf.st_mtime != currentFileReadTime) {
			reloadFile = true;
		}

		currentFileReadTime = buf.st_mtime;
	}

	bool changed = false;

	if (reloadFile) {
		currentLine = 0;
		StringFormat(currentFile, 4096, "%s", file);
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

	currentEndOfBlock = SourceFindEndOfBlock();
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
	EvaluateCommand(disassemblyCommand);

	if (!strstr(evaluateResult, "Dump of assembler code for function")) {
		char buffer[32];
		StringFormat(buffer, sizeof(buffer), "disas $pc,+1000");
		EvaluateCommand(buffer);
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
		currentEndOfBlock = -1;
		currentFile[0] = 0;
		currentFileReadTime = 0;
		DisplaySetPositionFromStack();
		displayCode->tabSize = 4;
	}

	UIElementRefresh(&displayCode->e);
}

void CommandSetDisassemblyMode(void *) {
	const char *newMode = UIDialogShow(windowMain, 0, "Select the disassembly mode:\n%b\n%b\n%b", "Disassembly only", "With source", "Source centric");

	if (0 == strcmp(newMode, "Disassembly only")) disassemblyCommand = "disas";
	if (0 == strcmp(newMode, "With source"))      disassemblyCommand = "disas /s";
	if (0 == strcmp(newMode, "Source centric"))   disassemblyCommand = "disas /m";

	if (showingDisassembly) {
		CommandToggleDisassembly(nullptr);
		CommandToggleDisassembly(nullptr);
	}
}

void DisplayCodeDrawInspectLineModeOverlay(UIPainter *painter) {
	const char *instructions = "(Press Esc to exit inspect line mode.)";
	int width = (strlen(instructions) + 8) * ui.activeFont->glyphWidth;

	for (int index = 0; index < inspectResults.Length() / 2; index++) {
		int w = (strlen(inspectResults[index * 2]) + strlen(inspectResults[index * 2 + 1]) + 8) * ui.activeFont->glyphWidth;
		if (w > width) width = w;
	}

	int xOffset = 0;

	{
		UICodeLine *line = &displayCode->lines[currentLine - 1];

		for (int i = 0; i < line->bytes; i++) {
			if (displayCode->content[line->offset + i] == '\t') {
				xOffset += 4 * ui.activeFont->glyphWidth;
			} else if (displayCode->content[line->offset + i] == ' ') {
				xOffset += 1 * ui.activeFont->glyphWidth;
			} else {
				break;
			}
		}
	}

	char buffer[256];
	int lineHeight = UIMeasureStringHeight();
	UIRectangle bounds = UIRectangleAdd(displayCurrentLineBounds, UI_RECT_4(xOffset, 0, lineHeight, 8 + lineHeight * (inspectResults.Length() / 2 + 1)));
	bounds.r = bounds.l + width;
	UIDrawBlock(painter, UIRectangleAdd(bounds, UI_RECT_1(3)), ui.theme.border);
	UIDrawRectangle(painter, bounds, ui.theme.codeBackground, ui.theme.border, UI_RECT_1(2));
	UIRectangle line = UIRectangleAdd(bounds, UI_RECT_4(4, -4, 4, 0));
	line.b = line.t + lineHeight;

	for (int index = 0; index < inspectResults.Length() / 2; index++) {
		if (noInspectResults) {
			StringFormat(buffer, sizeof(buffer), "%s", inspectResults[index * 2]);
		} else if (index < 9) {
			StringFormat(buffer, sizeof(buffer), "[%d] %s %s", index + 1, inspectResults[index * 2], inspectResults[index * 2 + 1]);
		} else {
			StringFormat(buffer, sizeof(buffer), "    %s %s", inspectResults[index * 2], inspectResults[index * 2 + 1]);
		}

		UIDrawString(painter, line, buffer, -1, noInspectResults ? ui.theme.codeOperator : ui.theme.codeString, UI_ALIGN_LEFT, NULL);
		line = UIRectangleAdd(line, UI_RECT_2(0, lineHeight));
	}

	UIDrawString(painter, line, instructions, -1, ui.theme.codeNumber, UI_ALIGN_RIGHT, NULL);
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
				DebuggerSend(buffer, true, false);
			} else if (element->window->alt || element->window->shift) {
				char buffer[1024];
				StringFormat(buffer, 1024, "tbreak %d", line);
				EvaluateCommand(buffer);
				StringFormat(buffer, 1024, "jump %d", line);
				DebuggerSend(buffer, true, false);
			}
		}
	} else if (message == UI_MSG_RIGHT_DOWN && !showingDisassembly) {
		int result = UICodeHitTest((UICode *) element, element->window->cursorX, element->window->cursorY);

		for (int i = 0; i < breakpoints.Length(); i++) {
			if (breakpoints[i].line == -result && 0 == strcmp(breakpoints[i].fileFull, currentFileFull)) {
				UIMenu *menu = UIMenuCreate(&element->window->e, UI_MENU_NO_SCROLL);
				UIMenuAddItem(menu, 0, "Delete", -1, CommandDeleteBreakpoint, (void *) (intptr_t) i);
				UIMenuAddItem(menu, 0, breakpoints[i].enabled ? "Disable" : "Enable", -1,
						breakpoints[i].enabled ? CommandDisableBreakpoint : CommandEnableBreakpoint, (void *) (intptr_t) i);
				UIMenuShow(menu);
			}
		}
	} else if (message == UI_MSG_CODE_GET_MARGIN_COLOR && !showingDisassembly) {
		for (int i = 0; i < breakpoints.Length(); i++) {
			if (breakpoints[i].line == di && 0 == strcmp(breakpoints[i].fileFull, currentFileFull)) {
				return breakpoints[i].enabled ? 0xFF0000 : 0x822454;
			}
		}
	} else if (message == UI_MSG_PAINT) {
		element->messageClass(element, message, di, dp);

		if (inInspectLineMode) {
			UIFont *previousFont = UIFontActivate(((UICode *) element)->font);
			DisplayCodeDrawInspectLineModeOverlay((UIPainter *) dp);
			UIFontActivate(previousFont);
		}

		return 1;
	} else if (message == UI_MSG_CODE_DECORATE_LINE) {
		UICodeDecorateLine *m = (UICodeDecorateLine *) dp;

		if (m->index == currentLine) {
			displayCurrentLineBounds = m->bounds;
		}

		if (m->index == autoPrintResultLine) {
			UIRectangle rectangle = UI_RECT_4(m->x + ui.activeFont->glyphWidth, m->bounds.r, m->y, m->y + UIMeasureStringHeight());
			UIDrawString(m->painter, rectangle, autoPrintResult, -1, ui.theme.codeComment, UI_ALIGN_LEFT, NULL);
		}

		if (UICodeHitTest((UICode *) element, element->window->cursorX, element->window->cursorY) == m->index
				&& element->window->hovered == element && (element->window->ctrl || element->window->alt || element->window->shift)
				&& !element->window->textboxModifiedFlag) {
			UIDrawBorder(m->painter, m->bounds, element->window->ctrl ? 0xFF6290E0 : 0xFFE09062, UI_RECT_1(2));
			UIDrawString(m->painter, m->bounds, element->window->ctrl ? "=> run until " : "=> skip to ", -1, ui.theme.text, UI_ALIGN_RIGHT, NULL);
		} else if (m->index == currentEndOfBlock) {
			UIDrawString(m->painter, m->bounds, "[Shift+F10]", -1, ui.theme.codeComment, UI_ALIGN_RIGHT, NULL);
		}
	} else if (message == UI_MSG_MOUSE_MOVE || message == UI_MSG_UPDATE) {
		if (element->window->cursorX != lastCursorX || element->window->cursorY != lastCursorY) {
			lastCursorX = element->window->cursorX;
			lastCursorY = element->window->cursorY;
			element->window->textboxModifiedFlag = false;
		}

		UIElementRefresh(element);
	}

	return 0;
}

UIElement *SourceWindowCreate(UIElement *parent) {
	displayCode = UICodeCreate(parent, 0);
	displayCode->font = fontCode;
	displayCode->e.messageUser = DisplayCodeMessage;
	return &displayCode->e;
}

void SourceWindowUpdate(const char *data, UIElement *element) {
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

	if (changedSourceLine && stackSelected < stack.Length() && strcmp(stack[stackSelected].location, previousLocation)) {
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

	UIElementRefresh(element);
}

bool InspectIsTokenCharacter(char c) {
	return isalpha(c) || c == '_';
}

void InspectCurrentLine() {
	for (int i = 0; i < inspectResults.Length(); i++) free(inspectResults[i]);
	inspectResults.Free();

	UICodeLine *line = &displayCode->lines[currentLine - 1];
	const char *string = displayCode->content + line->offset;

	for (int i = 0; i < line->bytes; i++) {
		if ((i != line->bytes - 1 && InspectIsTokenCharacter(string[i]) && !InspectIsTokenCharacter(string[i + 1])) || string[i] == ']') {
			int b = 0, j = i;

			for (; j >= 0; j--) {
				if (j && string[j] == '>' && string[j - 1] == '-') {
					j--;
				} else if (string[j] == ']') {
					b++;
				} else if (string[j] == '[' && b) {
					b--;
				} else if (InspectIsTokenCharacter(string[j]) || b || string[j] == '.') {
				} else {
					j++;
					break;
				}
			}

			char buffer[256];
			if (i - j + 1 > 255 || j < 1) continue;
			StringFormat(buffer, sizeof(buffer), "%.*s", i - j + 1, string + j);

			if (0 == strcmp(buffer, "true") || 0 == strcmp(buffer, "false") || 0 == strcmp(buffer, "if") || 0 == strcmp(buffer, "for")
					|| 0 == strcmp(buffer, "else") || 0 == strcmp(buffer, "while") || 0 == strcmp(buffer, "int")
					|| 0 == strcmp(buffer, "char") || 0 == strcmp(buffer, "switch") || 0 == strcmp(buffer, "float")) {
				continue;
			}

			bool match = false;

			for (int k = 0; k < inspectResults.Length(); k += 2) {
				if (0 == strcmp(inspectResults[k], buffer)) {
					match = true;
				}
			}

			if (match) continue;

			const char *result = EvaluateExpression(buffer);
			if (!result) continue;
			if (0 == memcmp(result, "= {", 3) && !strchr(result + 3, '=')) continue;
			inspectResults.Add(strdup(buffer));
			inspectResults.Add(strdup(result));
		}
	}

	if (!inspectResults.Length()) {
		inspectResults.Add(strdup("No expressions to display."));
		inspectResults.Add(strdup(" "));
		noInspectResults = true;
	} else {
		noInspectResults = false;
	}
}

void InspectLineModeExit(UIElement *element) {
	UIElementDestroy(element);
	UIElementFocus(&textboxInput->e);
	inInspectLineMode = false;
	currentLine = inspectModeRestoreLine;
	UICodeFocusLine(displayCode, currentLine);
	UIElementRefresh(&displayCode->e);
}

int InspectLineModeMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_UPDATE && element->window->focused != element) {
		InspectLineModeExit(element);
	} else if (message == UI_MSG_KEY_TYPED) {
		UIKeyTyped *m = (UIKeyTyped *) dp;

		if ((m->textBytes == 1 && m->text[0] == '`') || m->code == UI_KEYCODE_ESCAPE) {
			InspectLineModeExit(element);
		} else if (m->code >= UI_KEYCODE_DIGIT('1') && m->code <= UI_KEYCODE_DIGIT('9')) {
			int index = (m->code - UI_KEYCODE_DIGIT('1')) * 2;

			if (index < inspectResults.Length()) {
				InspectLineModeExit(element);
				WatchAddExpression2(inspectResults[index]);
			}
		} else if ((m->code == UI_KEYCODE_UP && currentLine != 1) || (m->code == UI_KEYCODE_DOWN && currentLine != displayCode->lineCount)) {
			currentLine += m->code == UI_KEYCODE_UP ? -1 : 1;
			UICodeFocusLine(displayCode, currentLine);
			InspectCurrentLine();
			UIElementRefresh(&displayCode->e);
		}

		return 1;
	}

	return 0;
}

void CommandInspectLine(void *) {
	if (!currentLine || currentLine - 1 >= displayCode->lineCount) return;

	inspectModeRestoreLine = currentLine;
	inInspectLineMode = true;
	InspectCurrentLine();
	UIElementRefresh(&displayCode->e);

	// Create an element to receive key input messages.
	UIElement *element = UIElementCreate(sizeof(UIElement), &windowMain->e, 0, InspectLineModeMessage, 0);
	UIElementFocus(element);
}

//////////////////////////////////////////////////////
// Data viewers:
//////////////////////////////////////////////////////

struct AutoUpdateViewer {
	UIElement *element;
	void (*callback)(UIElement *);
};

Array<AutoUpdateViewer> autoUpdateViewers;
bool autoUpdateViewersQueued;

bool DataViewerRemoveFromAutoUpdateList(UIElement *element) {
	for (int i = 0; i < autoUpdateViewers.Length(); i++) {
		if (autoUpdateViewers[i].element == element) {
			autoUpdateViewers.DeleteSwap(i);
			return true;
		}
	}

	return false;
}

int DataViewerAutoUpdateButtonMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		element->flags ^= UI_BUTTON_CHECKED;

		if (element->flags & UI_BUTTON_CHECKED) {
			AutoUpdateViewer v = { .element = element->parent, .callback = (void (*)(UIElement *)) element->cp };
			autoUpdateViewers.Add(v);
		} else {
			bool found = DataViewerRemoveFromAutoUpdateList(element->parent);
			assert(found);
		}
	}

	return 0;
}

void DataViewersUpdateAll() {
	if (~dataTab->e.flags & UI_ELEMENT_HIDE) {
		for (int i = 0; i < autoUpdateViewers.Length(); i++) {
			autoUpdateViewers[i].callback(autoUpdateViewers[i].element);
		}
	} else if (autoUpdateViewers.Length()) {
		autoUpdateViewersQueued = true;
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

int BitmapViewerWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DESTROY) {
		DataViewerRemoveFromAutoUpdateList(element);
		free(element->cp);
	} else if (message == UI_MSG_GET_WIDTH) {
		int fit = ((BitmapViewer *) element->cp)->parsedWidth + 40;
		return fit > 300 ? fit : 300;
	} else if (message == UI_MSG_GET_HEIGHT) {
		int fit = ((BitmapViewer *) element->cp)->parsedHeight + 40;
		return fit > 100 ? fit : 100;
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

const char *BitmapViewerGetBits(const char *pointerString, const char *widthString, const char *heightString, const char *strideString,
		uint32_t **_bits, int *_width, int *_height, int *_stride) {
	const char *widthResult = EvaluateExpression(widthString);
	if (!widthResult) { return "Could not evaluate width."; }
	int width = atoi(widthResult + 1);
	const char *heightResult = EvaluateExpression(heightString);
	if (!heightResult) { return "Could not evaluate height."; }
	int height = atoi(heightResult + 1);
	int stride = width * 4;
	const char *pointerResult = EvaluateExpression(pointerString, "/x");
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

	uint32_t *bits = (uint32_t *) malloc(stride * height * 4); // TODO Is this multiply by 4 necessary?! And the one below.

	char bitmapPath[PATH_MAX];
	realpath(".bitmap.gf", bitmapPath);

	char buffer[PATH_MAX * 2];
	StringFormat(buffer, sizeof(buffer), "dump binary memory %s (%s) (%s+%d)", bitmapPath, pointerResult, pointerResult, stride * height);
	EvaluateCommand(buffer);

	FILE *f = fopen(bitmapPath, "rb");

	if (f) {
		fread(bits, 1, stride * height * 4, f); // TODO Is this multiply by 4 necessary?!
		fclose(f);
		unlink(bitmapPath);
	}

	if (!f || strstr(evaluateResult, "access")) {
		return "Could not read the image bits!";
	}

	*_bits = bits, *_width = width, *_height = height, *_stride = stride;
	return nullptr;
}

int BitmapViewerDisplayMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_RIGHT_UP) {
		UIMenu *menu = UIMenuCreate(&element->window->e, UI_MENU_NO_SCROLL);

		UIMenuAddItem(menu, 0, "Save to file...", -1, [] (void *cp) {
			static char *path = NULL;
			const char *result = UIDialogShow(windowMain, 0, "Save to file       \nPath:\n%t\n%f%B%C", &path, "Save", "Cancel");
			if (strcmp(result, "Save")) return;

			UIImageDisplay *display = (UIImageDisplay *) cp;
			FILE *f = fopen(path, "wb");
			fprintf(f, "P6\n%d %d\n255\n", display->width, display->height);

			for (int i = 0; i < display->width * display->height; i++) {
				uint8_t pixel[3] = { (uint8_t) (display->bits[i] >> 16), (uint8_t) (display->bits[i] >> 8), (uint8_t) display->bits[i] };
				fwrite(pixel, 1, 3, f);
			}

			fclose(f);
		}, element);

		UIMenuShow(menu);
	}

	return 0;
}

void BitmapViewerAutoUpdateCallback(UIElement *element) {
	BitmapViewer *bitmap = (BitmapViewer *) element->cp;
	BitmapViewerUpdate(bitmap->pointer, bitmap->width, bitmap->height, bitmap->stride, element);
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
		bitmap->autoToggle->e.cp = (void *) BitmapViewerAutoUpdateCallback;
		bitmap->autoToggle->e.messageUser = DataViewerAutoUpdateButtonMessage;
		UIButtonCreate(&window->e, UI_BUTTON_SMALL | UI_ELEMENT_NON_CLIENT, "Refresh", -1)->e.messageUser = BitmapViewerRefreshMessage;
		owner = &window->e;

		UIPanel *panel = UIPanelCreate(owner, UI_PANEL_EXPAND);
		bitmap->display = UIImageDisplayCreate(&panel->e, UI_IMAGE_DISPLAY_INTERACTIVE | UI_ELEMENT_V_FILL, bits, width, height, stride);
		bitmap->labelPanel = UIPanelCreate(&panel->e, UI_PANEL_COLOR_1 | UI_ELEMENT_V_FILL);
		bitmap->label = UILabelCreate(&bitmap->labelPanel->e, UI_ELEMENT_H_FILL, nullptr, 0);
		bitmap->display->e.messageUser = BitmapViewerDisplayMessage;
	}

	BitmapViewer *bitmap = (BitmapViewer *) owner->cp;
	bitmap->parsedWidth = width, bitmap->parsedHeight = height;
	UIImageDisplaySetContent(bitmap->display, bits, width, height, stride);
	if (error) UILabelSetContent(bitmap->label, error, -1);
	if (error) bitmap->labelPanel->e.flags &= ~UI_ELEMENT_HIDE, bitmap->display->e.flags |= UI_ELEMENT_HIDE;
	else bitmap->labelPanel->e.flags |= UI_ELEMENT_HIDE, bitmap->display->e.flags &= ~UI_ELEMENT_HIDE;
	UIElementRefresh(bitmap->labelPanel->e.parent);
	UIElementRefresh(owner);
	UIElementRefresh(&dataWindow->e);

	free(bits);
}

void BitmapAddDialog(void *) {
	static char *pointer = nullptr, *width = nullptr, *height = nullptr, *stride = nullptr;

	const char *result = UIDialogShow(windowMain, 0,
			"Add bitmap\n\n%l\n\nPointer to bits: (32bpp, RR GG BB AA)\n%t\nWidth:\n%t\nHeight:\n%t\nStride: (optional)\n%t\n\n%l\n\n%f%B%C",
			&pointer, &width, &height, &stride, "Add", "Cancel");

	if (0 == strcmp(result, "Add")) {
		BitmapViewerUpdate(pointer, width, height, (stride && stride[0]) ? stride : nullptr);
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

		if (m->textBytes && !element->window->ctrl && !element->window->alt && m->text[0] == '`' && !textbox->bytes) {
			textbox->rejectNextKey = true;
		} else if (m->code == UI_KEYCODE_ENTER && !element->window->shift) {
			if (!textbox->bytes) {
				if (commandHistory.Length()) {
					CommandSendToGDB(commandHistory[0]);
				}

				return 1;
			}

			char buffer[1024];
			StringFormat(buffer, 1024, "%.*s", (int) textbox->bytes, textbox->string);
			if (commandLog) fprintf(commandLog, "%s\n", buffer);
			CommandSendToGDB(buffer);

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
			if (element->window->shift) {
				if (currentLine > 1) {
					DisplaySetPosition(NULL, currentLine - 1, false);
				}
			} else {
				if (commandHistoryIndex < commandHistory.Length()) {
					UITextboxClear(textbox, false);
					UITextboxReplace(textbox, commandHistory[commandHistoryIndex], -1, false);
					if (commandHistoryIndex < commandHistory.Length() - 1) commandHistoryIndex++;
					UIElementRefresh(&textbox->e);
				}
			}
		} else if (m->code == UI_KEYCODE_DOWN) {
			if (element->window->shift) {
				if (currentLine < displayCode->lineCount) {
					DisplaySetPosition(NULL, currentLine + 1, false);
				}
			} else {
				UITextboxClear(textbox, false);

				if (commandHistoryIndex > 0) {
					--commandHistoryIndex;
					UITextboxReplace(textbox, commandHistory[commandHistoryIndex], -1, false);
				}

				UIElementRefresh(&textbox->e);
			}
		}
	}

	return 0;
}

UIElement *ConsoleWindowCreate(UIElement *parent) {
	UIPanel *panel2 = UIPanelCreate(parent, UI_PANEL_EXPAND);
	displayOutput = UICodeCreate(&panel2->e, UI_CODE_NO_MARGIN | UI_ELEMENT_V_FILL);
	UIPanel *panel3 = UIPanelCreate(&panel2->e, UI_PANEL_HORIZONTAL | UI_PANEL_EXPAND | UI_PANEL_COLOR_1);
	panel3->border = UI_RECT_1(5);
	panel3->gap = 5;
	trafficLight = UISpacerCreate(&panel3->e, 0, 30, 30);
	trafficLight->e.messageUser = TrafficLightMessage;
	UIButton *buttonMenu = UIButtonCreate(&panel3->e, 0, "Menu", -1);
	buttonMenu->invoke = InterfaceShowMenu;
	buttonMenu->e.cp = buttonMenu;
	textboxInput = UITextboxCreate(&panel3->e, UI_ELEMENT_H_FILL);
	textboxInput->e.messageUser = TextboxInputMessage;
	UIElementFocus(&textboxInput->e);
	return &panel2->e;
}

//////////////////////////////////////////////////////
// Watch window:
//////////////////////////////////////////////////////

struct Watch {
	bool open, hasFields, loadedFields, isArray, isDynamicArray;
	uint8_t depth;
	char format;
	uintptr_t arrayIndex;
	char *key, *value, *type;
	Array<Watch *> fields;
	Watch *parent;
	uint64_t updateIndex;
};

enum WatchWindowMode {
	WATCH_NORMAL,
	WATCH_LOCALS,
};

struct WatchWindow {
	Array<Watch *> rows;
	Array<Watch *> baseExpressions;
	Array<Watch *> dynamicArrays;
	UIElement *element;
	UITextbox *textbox;
	char *lastLocalList;
	int selectedRow;
	int extraRows;
	WatchWindowMode mode;
	uint64_t updateIndex;
	bool waitingForFormatCharacter;
};

struct WatchLogEvaluated {
	char result[64];
};

struct WatchLogEntry {
	char value[64];
	char where[128];
	Array<WatchLogEvaluated> evaluated;
	Array<StackEntry> trace;
};

struct WatchLogger {
	int id, selectedEntry;
	char columns[256];
	char *expressionsToEvaluate;
	Array<WatchLogEntry> entries;
	UITable *table, *trace;
};

Array<WatchLogger *> watchLoggers;

int WatchLastRow(WatchWindow *w) {
	return w->rows.Length() - 1 + w->extraRows;
}

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

void WatchFree(WatchWindow *w, Watch *watch, bool fieldsOnly = false) {
	for (int i = 0; i < watch->fields.Length(); i++) {
		WatchFree(w, watch->fields[i]);
		if (!watch->isArray) free(watch->fields[i]);
	}

	if (watch->isDynamicArray) {
		for (int i = 0; i < w->dynamicArrays.Length(); i++) {
			if (w->dynamicArrays[i] == watch) {
				w->dynamicArrays.DeleteSwap(i);
				break;
			}
		}
	}

	if (watch->isArray && watch->fields.Length()) {
		free(watch->fields[0]);
	}

	watch->loadedFields = false;
	watch->fields.Free();

	if (!fieldsOnly) {
		free(watch->key);
		free(watch->value);
		free(watch->type);
	}
}

void WatchDeleteExpression(WatchWindow *w, bool fieldsOnly = false) {
	WatchDestroyTextbox(w);
	if (w->selectedRow == w->rows.Length()) return;
	int end = w->selectedRow + 1;

	for (; end < w->rows.Length(); end++) {
		if (w->rows[w->selectedRow]->depth >= w->rows[end]->depth) {
			break;
		}
	}

	Watch *watch = w->rows[w->selectedRow];

	if (!fieldsOnly) {
		bool found = false;

		for (int i = 0; i < w->baseExpressions.Length(); i++) {
			if (watch == w->baseExpressions[i]) {
				found = true;
				w->baseExpressions.Delete(i);
				break;
			}
		}

		assert(found);
	}

	if (fieldsOnly) w->selectedRow++;
	w->rows.Delete(w->selectedRow, end - w->selectedRow);
	WatchFree(w, watch, fieldsOnly);
	if (!fieldsOnly) free(watch);
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
		} else if (stack[stackCount]->parent && stack[stackCount]->parent->isDynamicArray) {
			position += StringFormat(buffer + position, sizeof(buffer) - position, "'[%lu]'", stack[stackCount]->arrayIndex);
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

	if (strstr(evaluateResult, "(array)") || strstr(evaluateResult, "(d_arr)")) {
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

void WatchAddFields(WatchWindow *w, Watch *watch) {
	if (watch->loadedFields) {
		return;
	}

	watch->loadedFields = true;

	WatchEvaluate("gf_fields", watch);

	if (strstr(evaluateResult, "(array)") || strstr(evaluateResult, "(d_arr)")) {
		int count = atoi(evaluateResult + 7);

#define WATCH_ARRAY_MAX_FIELDS (10000000)
		if (count > WATCH_ARRAY_MAX_FIELDS) count = WATCH_ARRAY_MAX_FIELDS;
		if (count < 0) count = 0;

		Watch *fields = (Watch *) calloc(count, sizeof(Watch));
		watch->isArray = true;
		bool hasSubFields = false;

		if (strstr(evaluateResult, "(d_arr)")) {
			watch->isDynamicArray = true;
			w->dynamicArrays.Add(watch);
		}

		for (int i = 0; i < count; i++) {
			fields[i].format = watch->format;
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

void WatchInsertFieldRows2(WatchWindow *w, Watch *watch, Array<Watch *> *array) {
	for (int i = 0; i < watch->fields.Length(); i++) {
		array->Add(watch->fields[i]);
		if (watch->fields[i]->open) WatchInsertFieldRows2(w, watch->fields[i], array);
	}
}

void WatchInsertFieldRows(WatchWindow *w, Watch *watch, int position, bool ensureLastVisible) {
	Array<Watch *> array = {};
	WatchInsertFieldRows2(w, watch, &array);
	w->rows.InsertMany(array.array, position, array.Length());
	if (ensureLastVisible) WatchEnsureRowVisible(w, position + array.Length() - 1);
	array.Free();
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

void WatchAddExpression2(char *string) {
	UIElement *element = InterfaceWindowSwitchToAndFocus("Watch");
	WatchWindow *w = (WatchWindow *) element->cp;
	w->selectedRow = w->rows.Length();
	WatchAddExpression(w, strdup(string));
	if (w->selectedRow) w->selectedRow--;
	WatchEnsureRowVisible(w, w->selectedRow);
	UIElementRefresh(w->element->parent);
	UIElementRefresh(w->element);
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

			for (int i = 0; i < logger->entries.Length(); i++) {
				logger->entries[i].trace.Free();
				logger->entries[i].evaluated.Free();
			}

			logger->entries.Free();
			free(logger->expressionsToEvaluate);
			free(logger);
		}
	} else if (message == UI_MSG_GET_WIDTH || message == UI_MSG_GET_HEIGHT) {
		return element->window->scale * 400;
	}

	return 0;
}

void WatchLoggerTraceSelectFrame(UIElement *element, int index, WatchLogger *logger) {
	if (index == -1) {
		return;
	}

	StackEntry *entry = &logger->entries[logger->selectedEntry].trace[index];
	char location[sizeof(entry->location)];
	strcpy(location, entry->location);
	char *colon = strchr(location, ':');

	if (colon) {
		*colon = 0;
		DisplaySetPosition(location, atoi(colon + 1), false);
		UIElementRefresh(element);
	}
}

int WatchLoggerTableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	WatchLogger *logger = (WatchLogger *) element->cp;

	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		WatchLogEntry *entry = &logger->entries[m->index];
		m->isSelected = m->index == logger->selectedEntry;

		if (m->column == 0) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->value);
		} else if (m->column == 1) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->where);
		} else {
			if (m->column - 2 < entry->evaluated.Length()) {
				return StringFormat(m->buffer, m->bufferBytes, "%s", entry->evaluated[m->column - 2].result);
			} else {
				return 0;
			}
		}
	} else if (message == UI_MSG_LEFT_DOWN || message == UI_MSG_MOUSE_DRAG) {
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY);

		if (index != -1 && logger->selectedEntry != index) {
			logger->selectedEntry = index;
			logger->trace->itemCount = logger->entries[index].trace.Length();
			WatchLoggerTraceSelectFrame(&logger->trace->e, 0, logger);
			UITableResizeColumns(logger->trace);
			UIElementRefresh(&logger->trace->e);
			UIElementRefresh(element);
		}
	}

	return 0;
}

int WatchLoggerTraceMessage(UIElement *element, UIMessage message, int di, void *dp) {
	WatchLogger *logger = (WatchLogger *) element->cp;

	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		StackEntry *entry = &logger->entries[logger->selectedEntry].trace[m->index];

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
		WatchLoggerTraceSelectFrame(element, index, logger);
	}

	return 0;
}

bool WatchGetAddress(Watch *watch) {
	WatchEvaluate("gf_addressof", watch);

	if (strstr(evaluateResult, "??")) {
		UIDialogShow(windowMain, 0, "Couldn't get the address of the variable.\n%f%B", "OK");
		return false;
	}

	char *end = strstr(evaluateResult, " ");

	if (!end) {
		UIDialogShow(windowMain, 0, "Couldn't get the address of the variable.\n%f%B", "OK");
		return false;
	}

	*end = 0;

	char *end2 = strchr(evaluateResult, '\n');
	if (end2) *end2 = 0;

	return true;
}

void WatchLoggerResizeColumns(void *_logger) {
	WatchLogger *logger = (WatchLogger *) _logger;
	UITableResizeColumns(logger->table);
	UIElementRefresh(&logger->table->e);
}

void WatchChangeLoggerCreate(WatchWindow *w) {
	if (w->selectedRow == w->rows.Length()) {
		return;
	}

	if (!dataTab) {
		UIDialogShow(windowMain, 0, "The data window is not open.\nThe watch log cannot be created.\n%f%B", "OK");
		return;
	}

	if (!WatchGetAddress(w->rows[w->selectedRow])) {
		return;
	}

	char *expressionsToEvaluate = nullptr;
	const char *result = UIDialogShow(windowMain, 0, "-- Watch logger settings --\nExpressions to evaluate (separate with semicolons):\n%t\n\n%l\n\n%f%B%C",
			&expressionsToEvaluate, "Start", "Cancel");

	if (0 == strcmp(result, "Cancel")) {
		free(expressionsToEvaluate);
		return;
	}

	char buffer[256];
	StringFormat(buffer, sizeof(buffer), "Log %s", evaluateResult);
	UIMDIChild *child = UIMDIChildCreate(&dataWindow->e, UI_MDI_CHILD_CLOSE_BUTTON, UI_RECT_1(0), buffer, -1);
	StringFormat(buffer, sizeof(buffer), "watch * %s", evaluateResult);
	EvaluateCommand(buffer);
	char *number = strstr(evaluateResult, "point ");

	if (!number) {
		UIDialogShow(windowMain, 0, "Couldn't set the watchpoint.\n%f%B", "OK");
		return;
	}

	WatchLogger *logger = (WatchLogger *) calloc(1, sizeof(WatchLogger));

	UIButton *button = UIButtonCreate(&child->e, UI_BUTTON_SMALL | UI_ELEMENT_NON_CLIENT, "Resize columns", -1);
	button->e.cp = logger;
	button->invoke = WatchLoggerResizeColumns;

	uintptr_t position = 0;
	position += StringFormat(logger->columns + position, sizeof(logger->columns) - position, "New value\tWhere");

	if (expressionsToEvaluate) {
		uintptr_t start = 0;

		for (uintptr_t i = 0; true; i++) {
			if (expressionsToEvaluate[i] == ';' || !expressionsToEvaluate[i]) {
				position += StringFormat(logger->columns + position, sizeof(logger->columns) - position, "\t%.*s",
						i - start, expressionsToEvaluate + start);
				start = i + 1;
			}

			if (!expressionsToEvaluate[i]) {
				break;
			}
		}
	}

	UISplitPane *panel = UISplitPaneCreate(&child->e, 0, 0.5f);
	UITable *table = UITableCreate(&panel->e, UI_ELEMENT_H_FILL | UI_ELEMENT_V_FILL, logger->columns);
	UITable *trace = UITableCreate(&panel->e, UI_ELEMENT_H_FILL | UI_ELEMENT_V_FILL, "Index\tFunction\tLocation\tAddress");

	logger->id = atoi(number + 6);
	logger->table = table;
	logger->trace = trace;
	logger->selectedEntry = -1;
	logger->expressionsToEvaluate = expressionsToEvaluate;
	child->e.cp = logger;
	table->e.cp = logger;
	trace->e.cp = logger;
	child->e.messageUser = WatchLoggerWindowMessage;
	table->e.messageUser = WatchLoggerTableMessage;
	trace->e.messageUser = WatchLoggerTraceMessage;
	watchLoggers.Add(logger);
	UIElementRefresh(&dataWindow->e);
	WatchLoggerResizeColumns(logger);

	UIDialogShow(windowMain, 0, "The log has been setup in the data window.\n%f%B", "OK");
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
	WatchLogger *logger = nullptr;

	for (int i = 0; i < watchLoggers.Length(); i++) {
		if (watchLoggers[i]->id == id) {
			logger = watchLoggers[i];
			break;
		}
	}

	if (!logger) return false;

	*afterValue = 0;
	*afterWhere = 0;
	WatchLogEntry entry = {};

	char *expressionsToEvaluate = logger->expressionsToEvaluate;

	if (expressionsToEvaluate) {
		uintptr_t start = 0;

		for (uintptr_t i = 0; true; i++) {
			if (expressionsToEvaluate[i] == ';' || !expressionsToEvaluate[i]) {
				WatchLogEvaluated evaluated;
				char buffer[256];
				StringFormat(buffer, sizeof(buffer), "%.*s", i - start, expressionsToEvaluate + start);
				EvaluateExpression(buffer);
				start = i + 1;
				size_t length = strlen(evaluateResult);
				if (length >= sizeof(evaluated.result)) length = sizeof(evaluated.result) - 1;
				char *start = strstr(evaluateResult, " = ");
				memcpy(evaluated.result, start ? start + 3 : evaluateResult, length);
				evaluated.result[length] = 0;
				entry.evaluated.Add(evaluated);
			}

			if (!expressionsToEvaluate[i]) {
				break;
			}
		}
	}

	if (strlen(value) >= sizeof(entry.value)) value[sizeof(entry.value) - 1] = 0;
	if (strlen(where) >= sizeof(entry.where)) where[sizeof(entry.where) - 1] = 0;
	strcpy(entry.value, value);
	strcpy(entry.where, where);
	Array<StackEntry> previousStack = stack;
	stack = {};
	DebuggerGetStack();
	entry.trace = stack;
	stack = previousStack;
	logger->entries.Add(entry);
	logger->table->itemCount++;
	UIElementRefresh(&logger->table->e);
	DebuggerSend("c", false, false);
	return true;
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

WatchWindow *WatchGetFocused() {
	return windowMain->focused->messageClass == WatchWindowMessage ? (WatchWindow *) windowMain->focused->cp : NULL;
}

void CommandWatchAddEntryForAddress(void *cp) {
	WatchWindow *w = (WatchWindow *) cp ?: WatchGetFocused();
	if (!w) return;
	if (w->mode == WATCH_NORMAL && w->selectedRow == w->rows.Length()) return;
	Watch *watch = w->rows[w->selectedRow];
	if (!WatchGetAddress(watch)) return;

	if (w->mode != WATCH_NORMAL) {
		InterfaceWindowSwitchToAndFocus("Watch");
		w = WatchGetFocused();
		assert(w != NULL);
	}

	char address[64];
	StringFormat(address, sizeof(address), "%s", evaluateResult);
	WatchEvaluate("gf_typeof", watch);
	if (strstr(evaluateResult, "??")) return;
	char *end = strchr(evaluateResult, '\n');
	if (end) *end = 0;
	size_t size = strlen(address) + strlen(evaluateResult) + 16;
	char *buffer = (char *) malloc(size);
	StringFormat(buffer, size, "(%s*)%s", evaluateResult, address);
	WatchAddExpression(w, buffer);
	WatchEnsureRowVisible(w, w->selectedRow);
	UIElementRefresh(w->element->parent);
	UIElementRefresh(w->element);
}

void CommandWatchViewSourceAtAddress(void *cp) {
	WatchWindow *w = (WatchWindow *) cp ?: WatchGetFocused();
	if (!w) return;
	if (w->mode == WATCH_NORMAL && w->selectedRow == w->rows.Length()) return;
	char *position = w->rows[w->selectedRow]->value;
	while (*position && !isdigit(*position)) position++;
	if (!(*position)) return;
	uint64_t value = strtoul(position, &position, 0);
	char buffer[256];
	StringFormat(buffer, sizeof(buffer), "info line * 0x%lx", value);
	EvaluateCommand(buffer);
	position = evaluateResult;

	if (strstr(evaluateResult, "No line number")) {
		char *end = strchr(evaluateResult, '\n');
		if (end) *end = 0;
		UIDialogShow(windowMain, 0, "%s\n%f%B", evaluateResult, "OK");
		return;
	}

	while (*position && !isdigit(*position)) position++;
	if (!(*position)) return;
	int line = strtol(position, &position, 0);
	while (*position && *position != '"') position++;
	if (!(*position)) return;
	char *file = position + 1;
	char *end = strchr(file, '"');
	if (!end) return;
	*end = 0;
	DisplaySetPosition(file, line, false);
}

void CommandWatchSaveAsRecurse(FILE *file, Watch *watch, int indent, int indexInParentArray) {
	fprintf(file, "%.*s", indent, "\t\t\t\t\t\t\t\t\t\t\t\t\t\t");

	if (indexInParentArray == -1) {
		fprintf(file, "%s = ", watch->key);
	} else {
		fprintf(file, "[%d] = ", indexInParentArray);
	}

	if (watch->open) {
		fprintf(file, "\n");

		for (int i = 0; i < watch->fields.Length(); i++) {
			CommandWatchSaveAsRecurse(file, watch->fields[i], indent + 1, watch->isArray ? i : -1);
		}
	} else {
		WatchEvaluate("gf_valueof", watch);
		char *value = strdup(evaluateResult);
		char *end = strchr(value, '\n');
		if (end) *end = 0;
		fprintf(file, "%s\n", value);
		free(value);
	}
}

void CommandWatchSaveAs(void *cp) {
	WatchWindow *w = (WatchWindow *) cp ?: WatchGetFocused();
	if (!w) return;
	if (w->selectedRow == w->rows.Length()) return;

	char *filePath = nullptr;
	const char *result = UIDialogShow(windowMain, 0, "Path:            \n%t\n%f%B%C", &filePath, "Save", "Cancel");

	if (0 == strcmp(result, "Cancel")) {
		free(filePath);
		return;
	}

	FILE *f = fopen(filePath, "wb");
	free(filePath);

	if (!f) {
		UIDialogShow(windowMain, 0, "Could not open the file for writing!\n%f%B", "OK");
		return;
	}

	Watch *watch = w->rows[w->selectedRow];
	CommandWatchSaveAsRecurse(f, watch, 0, -1);
	fclose(f);
}

void CommandWatchCopyValueToClipboard(void *cp) {
	WatchWindow *w = (WatchWindow *) cp ?: WatchGetFocused();
	if (!w) return;
	if (w->mode == WATCH_NORMAL && w->selectedRow == w->rows.Length()) return;

	Watch *watch = w->rows[w->selectedRow];

	WatchEvaluate("gf_valueof", watch);
	char *value = strdup(evaluateResult);
	char *end = strchr(value, '\n');
	if (end) *end = 0;

	_UIClipboardWriteText(w->element->window, value);
}

int WatchWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	WatchWindow *w = (WatchWindow *) element->cp;
	int rowHeight = (int) (UI_SIZE_TEXTBOX_HEIGHT * element->window->scale);
	int result = 0;

	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;

		for (int i = (painter->clip.t - element->bounds.t) / rowHeight; i <= WatchLastRow(w); i++) {
			UIRectangle row = element->bounds;
			row.t += i * rowHeight, row.b = row.t + rowHeight;

			UIRectangle intersection = UIRectangleIntersection(row, painter->clip);
			if (!UI_RECT_VALID(intersection)) break;

			bool focused = i == w->selectedRow && element->window->focused == element;

			if (focused) UIDrawBlock(painter, row, ui.theme.selected);
			UIDrawBorder(painter, row, ui.theme.border, UI_RECT_4(0, 1, 0, 1));

			row.l += UI_SIZE_TEXTBOX_MARGIN;
			row.r -= UI_SIZE_TEXTBOX_MARGIN;

			if (i != w->rows.Length()) {
				Watch *watch = w->rows[i];
				char buffer[256];

				if ((!watch->value || watch->updateIndex != w->updateIndex) && !watch->open) {
					if (!programRunning) {
						free(watch->value);
						watch->updateIndex = w->updateIndex;
						WatchEvaluate("gf_valueof", watch);
						watch->value = strdup(evaluateResult);
						char *end = strchr(watch->value, '\n');
						if (end) *end = 0;
					} else {
						free(watch->value);
						watch->value = strdup("..");
					}
				}

				char keyIndex[64];

				if (!watch->key) {
					StringFormat(keyIndex, sizeof(keyIndex), "[%lu]", watch->arrayIndex);
				}

				if (focused && w->waitingForFormatCharacter) {
					StringFormat(buffer, sizeof(buffer), "Enter format character: (e.g. 'x' for hex)");
				} else {
					StringFormat(buffer, sizeof(buffer), "%.*s%s%s%s%s",
							watch->depth * 3, "                                           ",
							watch->open ? "v " : watch->hasFields ? "> " : "",
							watch->key ?: keyIndex,
							watch->open ? "" : " = ",
							watch->open ? "" : watch->value);
				}

				if (focused) {
					UIDrawString(painter, row, buffer, -1, ui.theme.textSelected, UI_ALIGN_LEFT, nullptr);
				} else {
					UIDrawStringHighlighted(painter, row, buffer, -1, 1);
				}
			}
		}
	} else if (message == UI_MSG_GET_HEIGHT) {
		return (WatchLastRow(w) + 1) * rowHeight;
	} else if (message == UI_MSG_LEFT_DOWN) {
		w->selectedRow = (element->window->cursorY - element->bounds.t) / rowHeight;

		if (w->selectedRow >= 0 && w->selectedRow < w->rows.Length()) {
			Watch *watch = w->rows[w->selectedRow];
			int x = (element->window->cursorX - element->bounds.l) / ui.activeFont->glyphWidth;

			if (x >= watch->depth * 3 - 1 && x <= watch->depth * 3 + 1 && watch->hasFields) {
				UIKeyTyped m = { 0 };
				m.code = watch->open ? UI_KEYCODE_LEFT : UI_KEYCODE_RIGHT;
				WatchWindowMessage(element, UI_MSG_KEY_TYPED, 0, &m);
			}
		}

		UIElementFocus(element);
		UIElementRepaint(element, nullptr);
	} else if (message == UI_MSG_RIGHT_DOWN) {
		int index = (element->window->cursorY - element->bounds.t) / rowHeight;

		if (index >= 0 && index < w->rows.Length()) {
			WatchWindowMessage(element, UI_MSG_LEFT_DOWN, di, dp);
			UIMenu *menu = UIMenuCreate(&element->window->e, UI_MENU_NO_SCROLL);

			if (w->mode == WATCH_NORMAL && !w->rows[index]->parent) {
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

			UIMenuAddItem(menu, 0, "Copy value to clipboard\tCtrl+C", -1, CommandWatchCopyValueToClipboard, w);

			UIMenuAddItem(menu, 0, "Log writes to address...", -1, [] (void *cp) {
				WatchChangeLoggerCreate((WatchWindow *) cp);
			}, w);

			UIMenuAddItem(menu, 0, "Break on writes to address", -1, [] (void *cp) {
				WatchWindow *w = (WatchWindow *) cp;
				if (w->selectedRow == w->rows.Length()) return;
				if (!WatchGetAddress(w->rows[w->selectedRow])) return;
				char buffer[256];
				StringFormat(buffer, sizeof(buffer), "watch * %s", evaluateResult);
				DebuggerSend(buffer, true, false);
			}, w);

			if (firstWatchWindow) {
				UIMenuAddItem(menu, 0, "Add entry for address\tCtrl+E", -1, CommandWatchAddEntryForAddress, w);
			}

			UIMenuAddItem(menu, 0, "View source at address\tCtrl+G", -1, CommandWatchViewSourceAtAddress, w);
			UIMenuAddItem(menu, 0, "Save as...", -1, CommandWatchSaveAs, w);

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

			if (w->rows[w->selectedRow]->isArray) {
				for (int i = 0; i < w->rows[w->selectedRow]->fields.Length(); i++) {
					w->rows[w->selectedRow]->fields[i]->format = w->rows[w->selectedRow]->format;
					w->rows[w->selectedRow]->fields[i]->updateIndex--;
				}
			}

			w->waitingForFormatCharacter = false;
		} else if (w->mode == WATCH_NORMAL && w->selectedRow != w->rows.Length() && !w->textbox
				&& (m->code == UI_KEYCODE_ENTER || m->code == UI_KEYCODE_BACKSPACE || (m->code == UI_KEYCODE_LEFT && !w->rows[w->selectedRow]->open))
				&& !w->rows[w->selectedRow]->parent) {
			WatchCreateTextboxForRow(w, true);
		} else if (m->code == UI_KEYCODE_DELETE && !w->textbox
				&& w->selectedRow != w->rows.Length() && !w->rows[w->selectedRow]->parent) {
			WatchDeleteExpression(w);
		} else if (m->textBytes && m->text[0] == '/' && w->selectedRow != w->rows.Length()) {
			w->waitingForFormatCharacter = true;
		} else if (m->textBytes && m->text[0] == '`') {
			result = 0;
		} else if (w->mode == WATCH_NORMAL && m->textBytes && m->code != UI_KEYCODE_TAB && !w->textbox && !element->window->ctrl && !element->window->alt
				&& (w->selectedRow == w->rows.Length() || !w->rows[w->selectedRow]->parent)) {
			WatchCreateTextboxForRow(w, false);
			UIElementMessage(&w->textbox->e, message, di, dp);
		} else if (w->mode == WATCH_NORMAL && m->textBytes && m->code == UI_KEYCODE_LETTER('V') && !w->textbox && element->window->ctrl
				&& !element->window->alt && !element->window->shift && (w->selectedRow == w->rows.Length() || !w->rows[w->selectedRow]->parent)) {
			WatchCreateTextboxForRow(w, false);
			UIElementMessage(&w->textbox->e, message, di, dp);
		} else if (m->code == UI_KEYCODE_ENTER && w->textbox) {
			WatchAddExpression(w);
		} else if (m->code == UI_KEYCODE_ESCAPE) {
			WatchDestroyTextbox(w);
		} else if (m->code == UI_KEYCODE_UP) {
			if (element->window->shift) {
				if (currentLine > 1) {
					DisplaySetPosition(NULL, currentLine - 1, false);
				}
			} else {
				WatchDestroyTextbox(w);
				w->selectedRow--;
			}
		} else if (m->code == UI_KEYCODE_DOWN) {
			if (element->window->shift) {
				if (currentLine < displayCode->lineCount) {
					DisplaySetPosition(NULL, currentLine + 1, false);
				}
			} else {
				WatchDestroyTextbox(w);
				w->selectedRow++;
			}
		} else if (m->code == UI_KEYCODE_HOME) {
			w->selectedRow = 0;
		} else if (m->code == UI_KEYCODE_END) {
			w->selectedRow = WatchLastRow(w);
		} else if (m->code == UI_KEYCODE_RIGHT && !w->textbox
				&& w->selectedRow != w->rows.Length() && w->rows[w->selectedRow]->hasFields
				&& !w->rows[w->selectedRow]->open) {
			Watch *watch = w->rows[w->selectedRow];
			watch->open = true;
			WatchAddFields(w, watch);
			WatchInsertFieldRows(w, watch, w->selectedRow + 1, true);
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
		} else if (m->code == UI_KEYCODE_LETTER('C') && !w->textbox
				&& !element->window->shift && !element->window->alt && element->window->ctrl) {
			CommandWatchCopyValueToClipboard(w);
		} else {
			result = 0;
		}

		WatchEnsureRowVisible(w, w->selectedRow);
		UIElementRefresh(element->parent);
		UIElementRefresh(element);
	}

	if (w->selectedRow < 0) {
		w->selectedRow = 0;
	} else if (w->selectedRow > WatchLastRow(w)) {
		w->selectedRow = WatchLastRow(w);
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
	UIPanel *panel = UIPanelCreate(parent, UI_PANEL_SCROLL | UI_PANEL_COLOR_1);
	panel->e.messageUser = WatchPanelMessage;
	panel->e.cp = w;
	w->element = UIElementCreate(sizeof(UIElement), &panel->e, UI_ELEMENT_H_FILL | UI_ELEMENT_TAB_STOP, WatchWindowMessage, "Watch");
	w->element->cp = w;
	w->mode = WATCH_NORMAL;
	w->extraRows = 1;
	if (!firstWatchWindow) firstWatchWindow = w;
	return &panel->e;
}

UIElement *LocalsWindowCreate(UIElement *parent) {
	WatchWindow *w = (WatchWindow *) calloc(1, sizeof(WatchWindow));
	UIPanel *panel = UIPanelCreate(parent, UI_PANEL_SCROLL | UI_PANEL_COLOR_1);
	panel->e.messageUser = WatchPanelMessage;
	panel->e.cp = w;
	w->element = UIElementCreate(sizeof(UIElement), &panel->e, UI_ELEMENT_H_FILL | UI_ELEMENT_TAB_STOP, WatchWindowMessage, "Locals");
	w->element->cp = w;
	w->mode = WATCH_LOCALS;
	return &panel->e;
}

void WatchWindowUpdate(const char *, UIElement *element) {
	WatchWindow *w = (WatchWindow *) element->cp;

	if (w->mode == WATCH_LOCALS) {
		EvaluateCommand("py gf_locals()");

		bool newFrame = (!w->lastLocalList || 0 != strcmp(w->lastLocalList, evaluateResult));

		if (newFrame) {
			if (w->lastLocalList) free(w->lastLocalList);
			w->lastLocalList = strdup(evaluateResult);

			char *buffer = strdup(evaluateResult);
			char *s = buffer;
			char *end;
			Array<char *> expressions = {};

			while ((end = strchr(s, '\n')) != NULL) {
				*end = '\0';
				if (strstr(s, "(gdb)")) break;
				expressions.Add(s);
				s = end + 1;
			}

			if (expressions.Length() > 0) {
				for (int watchIndex = 0; watchIndex < w->baseExpressions.Length(); watchIndex++) {
					Watch *watch = w->baseExpressions[watchIndex];
					bool matched = false;

					for (int expressionIndex = 0; expressionIndex < expressions.Length(); expressionIndex++) {
						char *expression = expressions[expressionIndex];
						if (0 == strcmp(watch->key, expression)) {
							expressions.Delete(expressionIndex);
							matched = true;
							break;
						}
					}

					if (!matched) {
						bool found = false;
						for (int rowIndex = 0; rowIndex < w->rows.Length(); rowIndex++) {
							if (w->rows[rowIndex] == watch) {
								w->selectedRow = rowIndex;
								WatchDeleteExpression(w);
								watchIndex--;
								found = true;
								break;
							}
						}
						assert(found);
					}
				}

				// Add the remaining (new) variables.
				for (int expressionIndex = 0; expressionIndex < expressions.Length(); expressionIndex++) {
					char *expression = strdup(expressions[expressionIndex]);
					w->selectedRow = w->rows.Length();
					WatchAddExpression(w, expression);
				}

				w->selectedRow = w->rows.Length();
			}

			free(buffer);
			expressions.Free();
		}
	}

	for (int i = 0; i < w->baseExpressions.Length(); i++) {
		Watch *watch = w->baseExpressions[i];
		WatchEvaluate("gf_typeof", watch);
		char *result = strdup(evaluateResult);
		char *end = strchr(result, '\n');
		if (end) *end = 0;
		const char *oldType = watch->type ?: "??";

		if (strcmp(result, oldType) && strcmp(result, "??")) {
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

	for (int i = 0; i < w->dynamicArrays.Length(); i++) {
		Watch *watch = w->dynamicArrays[i];
		WatchEvaluate("gf_fields", watch);
		if (!strstr(evaluateResult, "(d_arr)")) continue;
		int count = atoi(evaluateResult + 7);
		if (count > WATCH_ARRAY_MAX_FIELDS) count = WATCH_ARRAY_MAX_FIELDS;
		if (count < 0) count = 0;
		int oldCount = watch->fields.Length();

		if (oldCount != count) {
			int index = -1;

			for (int i = 0; i < w->rows.Length(); i++) {
				if (w->rows[i] == watch) {
					index = i;
					break;
				}
			}

			assert(index != -1);
			w->selectedRow = index;
			WatchDeleteExpression(w, true);
			watch->open = true;
			WatchAddFields(w, watch);
			WatchInsertFieldRows(w, watch, index + 1, false);
		}
	}

	w->updateIndex++;
	UIElementRefresh(element->parent);
	UIElementRefresh(element);
}

void WatchWindowFocus(UIElement *element) {
	WatchWindow *w = (WatchWindow *) element->cp;
	UIElementFocus(w->element);
}

void CommandAddWatch(void *) {
	UIElement *element = InterfaceWindowSwitchToAndFocus("Watch");
	if (!element) return;
	WatchWindow *w = (WatchWindow *) element->cp;
	if (w->textbox) return;
	w->selectedRow = w->rows.Length();
	WatchCreateTextboxForRow(w, false);
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
			DebuggerSend(buffer, false, false);
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

		m->isSelected = false;
		for (int i = 0; i < selectedBreakpoints.Length(); i++) {
			if (m->index == selectedBreakpoints[i]) {
				m->isSelected = true;
				break;
			}
		}

		Breakpoint *entry = &breakpoints[m->index];

		if (m->column == 0) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->file);
		} else if (m->column == 1) {
			if (entry->watchpoint) return StringFormat(m->buffer, m->bufferBytes, "watch %d", entry->watchpoint);
			else return StringFormat(m->buffer, m->bufferBytes, "%d", entry->line);
		} else if (m->column == 2) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->enabled ? "yes" : "no");
		} else if (m->column == 3) {
			if (entry->hit > 0) {
				return StringFormat(m->buffer, m->bufferBytes, "%d", entry->hit);
			}
		}
	} else if (message == UI_MSG_RIGHT_DOWN) {
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY);

		if (selectedBreakpoints.Length() <= 1) {
			selectedBreakpoints.Free();
			selectedBreakpoints.Add(index);
		} else {
			bool breakpointIsSelected = false;
			for (int i = 0; i < selectedBreakpoints.Length(); i++) {
				if (selectedBreakpoints[i] == index) {
					breakpointIsSelected = true;
					break;
				}
			}

			if (!breakpointIsSelected) {
				selectedBreakpoints.Free();
				selectedBreakpoints.Add(index);
			}
		}

		if (index != -1) {
			UIMenu *menu = UIMenuCreate(&element->window->e, UI_MENU_NO_SCROLL);

			if (selectedBreakpoints.Length() > 1) {
				bool atLeastOneBreakpointDisabled = false;

				for (int i = 0; i < selectedBreakpoints.Length(); i++) {
					if (!breakpoints[selectedBreakpoints[i]].enabled) {
						atLeastOneBreakpointDisabled = true;
						break;
					}
				}

				UIMenuAddItem(menu, 0, "Delete", -1, CommandDeleteSelectedBreakpoints, NULL);
				UIMenuAddItem(menu, 0, atLeastOneBreakpointDisabled ? "Enable" : "Disable", -1,
						atLeastOneBreakpointDisabled ? CommandEnableSelectedBreakpoints : CommandDisableSelectedBreakpoints, NULL);
			} else {
				UIMenuAddItem(menu, 0, "Delete", -1, CommandDeleteBreakpoint, (void *) (intptr_t) index);
				UIMenuAddItem(menu, 0, breakpoints[index].enabled ? "Disable" : "Enable", -1,
						breakpoints[index].enabled ? CommandDisableBreakpoint : CommandEnableBreakpoint, (void *) (intptr_t) index);
			}

			UIMenuShow(menu);
		}
	} else if (message == UI_MSG_LEFT_DOWN) {
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY);

		if (index != -1) {
			if (element->window->ctrl || element->window->shift) {
				bool breakpointIsSelected = false;
				int i;
				for (i = 0; i < selectedBreakpoints.Length(); i++) {
					if (selectedBreakpoints[i] == index) {
						breakpointIsSelected = true;
						break;
					}
				}
				breakpointIsSelected ? selectedBreakpoints.Delete(i) : selectedBreakpoints.Add(index);

				if (element->window->shift) {
					if (selectedBreakpoints.Length() > 1) {
						int max = selectedBreakpoints[0];
						int min = selectedBreakpoints[0];
						for (int i = 1; i < selectedBreakpoints.Length(); i++) {
							max = max > selectedBreakpoints[i] ? max : selectedBreakpoints[i];
							min = min < selectedBreakpoints[i] ? min : selectedBreakpoints[i];
						}

						selectedBreakpoints.Free();
						for (int i = min; i <= max; i++) selectedBreakpoints.Add(i);
					}
				}
			} else {
				selectedBreakpoints.Free();
				selectedBreakpoints.Add(index);

				if (!breakpoints[index].watchpoint)
					DisplaySetPosition(breakpoints[index].file, breakpoints[index].line, false);
			}
		} else selectedBreakpoints.Free();
	}

	return 0;
}

UIElement *BreakpointsWindowCreate(UIElement *parent) {
	UITable *table = UITableCreate(parent, 0, "File\tLine\tEnabled\tHit");
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
	if (message == UI_MSG_TAB_SELECTED && autoUpdateViewersQueued) {
		// If we've switched to the data tab, we may need to update the bitmap viewers.

		for (int i = 0; i < autoUpdateViewers.Length(); i++) {
			autoUpdateViewers[i].callback(autoUpdateViewers[i].element);
		}

		autoUpdateViewersQueued = false;
	}

	return 0;
}

void CommandToggleFillDataTab(void *) {
	if (!dataTab) return;
	static UIElement *oldParent, *oldBefore;
	buttonFillWindow->e.flags ^= UI_BUTTON_CHECKED;

	if (switcherMain->active == &dataTab->e) {
		UISwitcherSwitchTo(switcherMain, switcherMain->e.children[0]);
		UIElementChangeParent(&dataTab->e, oldParent, oldBefore);
	} else {
		UIElementMessage(&dataTab->e, UI_MSG_TAB_SELECTED, 0, 0);
		oldParent = dataTab->e.parent;
		oldBefore = UIElementChangeParent(&dataTab->e, &switcherMain->e, NULL);
		UISwitcherSwitchTo(switcherMain, &dataTab->e);
	}
}

UIElement *DataWindowCreate(UIElement *parent) {
	dataTab = UIPanelCreate(parent, UI_PANEL_EXPAND);
	UIPanel *panel5 = UIPanelCreate(&dataTab->e, UI_PANEL_COLOR_1 | UI_PANEL_HORIZONTAL | UI_PANEL_SMALL_SPACING);
	buttonFillWindow = UIButtonCreate(&panel5->e, UI_BUTTON_SMALL, "Fill window", -1);
	buttonFillWindow->invoke = CommandToggleFillDataTab;

	for (int i = 0; i < interfaceDataViewers.Length(); i++) {
		UIButtonCreate(&panel5->e, UI_BUTTON_SMALL, interfaceDataViewers[i].addButtonLabel, -1)->invoke
			= interfaceDataViewers[i].addButtonCallback;
	}

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
	UIPanel *panel = UIPanelCreate(parent, UI_PANEL_COLOR_1 | UI_PANEL_EXPAND);
	window->textbox = UITextboxCreate(&panel->e, 0);
	window->textbox->e.messageUser = TextboxStructNameMessage;
	window->textbox->e.cp = window;
	window->display = UICodeCreate(&panel->e, UI_ELEMENT_V_FILL | UI_CODE_NO_MARGIN);
	UICodeInsertContent(window->display, "Type the name of a struct to view its layout.", -1, false);
	return &panel->e;
}

//////////////////////////////////////////////////////
// Files window:
//////////////////////////////////////////////////////

struct FilesWindow {
	char directory[PATH_MAX];
	UIPanel *panel;
	UILabel *path;
};

bool FilesPanelPopulate(FilesWindow *window);

mode_t FilesGetMode(FilesWindow *window, UIButton *button, size_t *oldLength) {
	const char *name = button->label;
	*oldLength = strlen(window->directory);
	strcat(window->directory, "/");
	strcat(window->directory, name);
	struct stat s;
	stat(window->directory, &s);
	return s.st_mode;
}

int FilesButtonMessage(UIElement *element, UIMessage message, int di, void *dp) {
	UIButton *button = (UIButton *) element;

	if (message == UI_MSG_CLICKED) {
		FilesWindow *window = (FilesWindow *) element->cp;
		size_t oldLength;
		mode_t mode = FilesGetMode(window, button, &oldLength);

		if (S_ISDIR(mode)) {
			if (FilesPanelPopulate(window)) {
				char copy[PATH_MAX];
				realpath(window->directory, copy);
				strcpy(window->directory, copy);
				return 0;
			}
		} else if (S_ISREG(mode)) {
			DisplaySetPosition(window->directory, 1, false);
		}

		window->directory[oldLength] = 0;
	} else if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		int i = (element == element->window->pressed) + (element == element->window->hovered);
		if (i) UIDrawBlock(painter, element->bounds, i == 2 ? ui.theme.buttonPressed : ui.theme.buttonHovered);
		UIDrawString(painter, UIRectangleAdd(element->bounds, UI_RECT_4(UI_SIZE_BUTTON_PADDING, 0, 0, 0)), button->label, button->labelBytes,
				button->e.flags & UI_BUTTON_CHECKED ? ui.theme.codeNumber : ui.theme.codeDefault, UI_ALIGN_LEFT, NULL);
		return 1;
	}

	return 0;
}

bool FilesPanelPopulate(FilesWindow *window) {
	size_t oldLength;
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
			button->e.flags &= ~UI_ELEMENT_TAB_STOP;
			button->e.cp = window;
			button->e.messageUser = FilesButtonMessage;

			if (S_ISDIR(FilesGetMode(window, button, &oldLength))) {
				button->e.flags |= UI_BUTTON_CHECKED;
			}

			window->directory[oldLength] = 0;
		}

		free(names[i]);
	}

	names.Free();
	UIElementRefresh(&window->panel->e);

	{
		char path[PATH_MAX];
		realpath(window->directory, path);
		UILabelSetContent(window->path, path, -1);
	}

	return true;
}

void FilesNavigateToCWD(void *cp) {
	FilesWindow *window = (FilesWindow *) cp;
	getcwd(window->directory, sizeof(window->directory));
	FilesPanelPopulate(window);
}

void FilesNavigateToActiveFile(void *cp) {
	FilesWindow *window = (FilesWindow *) cp;
	StringFormat(window->directory, sizeof(window->directory), "%s", currentFileFull);
	int p = strlen(window->directory);
	while (p--) if (window->directory[p] == '/') { window->directory[p] = 0; break; }
	FilesPanelPopulate(window);
}

UIElement *FilesWindowCreate(UIElement *parent) {
	FilesWindow *window = (FilesWindow *) calloc(1, sizeof(FilesWindow));
	UIPanel *container = UIPanelCreate(parent, UI_PANEL_EXPAND);
	window->panel = UIPanelCreate(&container->e, UI_PANEL_COLOR_1 | UI_PANEL_EXPAND | UI_PANEL_SCROLL | UI_ELEMENT_V_FILL);
	window->panel->gap = -1, window->panel->border = UI_RECT_1(1);
	window->panel->e.cp = window;
	UIPanel *row = UIPanelCreate(&container->e, UI_PANEL_COLOR_2 | UI_PANEL_HORIZONTAL | UI_PANEL_SMALL_SPACING);
	UIButton *button;
	button = UIButtonCreate(&row->e, UI_BUTTON_SMALL, "-> cwd", -1);
	button->e.cp = window, button->invoke = FilesNavigateToCWD;
	button = UIButtonCreate(&row->e, UI_BUTTON_SMALL, "-> active file", -1);
	button->e.cp = window, button->invoke = FilesNavigateToActiveFile;
	window->path = UILabelCreate(&row->e, UI_ELEMENT_H_FILL, "", 0);
	FilesNavigateToCWD(window);
	return &container->e;
}

//////////////////////////////////////////////////////
// Registers window:
//////////////////////////////////////////////////////

struct RegisterData { char string[128]; };
Array<RegisterData> registerData;

UIElement *RegistersWindowCreate(UIElement *parent) {
	return &UIPanelCreate(parent, UI_PANEL_SMALL_SPACING | UI_PANEL_COLOR_1 | UI_PANEL_SCROLL)->e;
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

UIElement *CommandsWindowCreate(UIElement *parent) {
	UIPanel *panel = UIPanelCreate(parent, UI_PANEL_COLOR_1 | UI_PANEL_SMALL_SPACING | UI_PANEL_EXPAND | UI_PANEL_SCROLL);
	if (!presetCommands.Length()) UILabelCreate(&panel->e, 0, "No preset commands found in config file!", -1);

	for (int i = 0; i < presetCommands.Length(); i++) {
		char buffer[256];
		StringFormat(buffer, sizeof(buffer), "gf-command %s", presetCommands[i].key);
		UIButton *button = UIButtonCreate(&panel->e, 0, presetCommands[i].key, -1);
		button->e.cp = strdup(buffer);
		button->invoke = CommandSendToGDB;
	}

	return &panel->e;
}

//////////////////////////////////////////////////////
// Log window:
//////////////////////////////////////////////////////

void *LogWindowThread(void *context) {
	if (!logPipePath) {
		fprintf(stderr, "Warning: The log pipe path has not been set in the configuration file!\n");
		return nullptr;
	}

	int file = open(logPipePath, O_RDONLY | O_NONBLOCK);

	if (file == -1) {
		fprintf(stderr, "Warning: Could not open the log pipe!\n");
		return nullptr;
	}

	struct pollfd p = { .fd = file, .events = POLLIN };

	while (true) {
		poll(&p, 1, 10000);

		if (p.revents & POLLHUP) {
			struct timespec t = { .tv_nsec = 10000000 };
			nanosleep(&t, 0);
		}

		while (true) {
			char input[16384];
			int length = read(file, input, sizeof(input) - 1);
			if (length <= 0) break;
			input[length] = 0;
			void *buffer = malloc(strlen(input) + sizeof(context) + 1);
			memcpy(buffer, &context, sizeof(context));
			strcpy((char *) buffer + sizeof(context), input);
			UIWindowPostMessage(windowMain, msgReceivedLog, buffer);
		}
	}
}

void LogReceived(char *buffer) {
	UICodeInsertContent(*(UICode **) buffer, buffer + sizeof(void *), -1, false);
	UIElementRefresh(*(UIElement **) buffer);
}

UIElement *LogWindowCreate(UIElement *parent) {
	UICode *code = UICodeCreate(parent, 0);
	pthread_t thread;
	pthread_create(&thread, nullptr, LogWindowThread, code);
	return &code->e;
}

//////////////////////////////////////////////////////
// Thread window:
//////////////////////////////////////////////////////

struct Thread {
	char frame[127];
	bool active;
	int id;
};

struct ThreadWindow {
	Array<Thread> threads;
};

int ThreadTableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	ThreadWindow *window = (ThreadWindow *) element->cp;

	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		m->isSelected = window->threads[m->index].active;

		if (m->column == 0) {
			return StringFormat(m->buffer, m->bufferBytes, "%d", window->threads[m->index].id);
		} else if (m->column == 1) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", window->threads[m->index].frame);
		}
	} else if (message == UI_MSG_LEFT_DOWN) {
		int index = UITableHitTest((UITable *) element, element->window->cursorX, element->window->cursorY);

		if (index != -1) {
			char buffer[1024];
			StringFormat(buffer, 1024, "thread %d", window->threads[index].id);
			DebuggerSend(buffer, true, false);
		}
	}

	return 0;
}

UIElement *ThreadWindowCreate(UIElement *parent) {
	UITable *table = UITableCreate(parent, 0, "ID\tFrame");
	table->e.cp = (ThreadWindow *) calloc(1, sizeof(ThreadWindow));
	table->e.messageUser = ThreadTableMessage;
	return &table->e;
}

void ThreadWindowUpdate(const char *, UIElement *_table) {
	ThreadWindow *window = (ThreadWindow *) _table->cp;
	window->threads.length = 0;

	EvaluateCommand("info threads");
	char *position = evaluateResult;

	for (int i = 0; position[i]; i++) {
		if (position[i] == '\n' && position[i + 1] == ' ' && position[i + 2] == ' ' && position[i + 3] == ' ') {
			memmove(position + i, position + i + 3, strlen(position) - 3 - i + 1);
		}
	}

	while (true) {
		position = strchr(position, '\n');
		if (!position) break;
		Thread thread = {};
		if (position[1] == '*') thread.active = true;
		thread.id = atoi(position + 2);
		position = strchr(position + 1, '"');
		if (!position) break;
		position = strchr(position + 1, '"');
		if (!position) break;
		position++;
		char *end = strchr(position, '\n');
		if (end - position >= (ptrdiff_t) sizeof(thread.frame))
			end = position + sizeof(thread.frame) - 1;
		memcpy(thread.frame, position, end - position);
		thread.frame[end - position] = 0;
		window->threads.Add(thread);
	}

	UITable *table = (UITable *) _table;
	table->itemCount = window->threads.Length();
	UITableResizeColumns(table);
	UIElementRefresh(&table->e);
}

//////////////////////////////////////////////////////
// Executable window:
//////////////////////////////////////////////////////

struct ExecutableWindow {
	UITextbox *path, *arguments;
	UICheckbox *askDirectory;
};

void ExecutableWindowStartOrRun(ExecutableWindow *window, bool pause) {
	char buffer[4096];
	StringFormat(buffer, sizeof(buffer), "file \"%.*s\"", window->path->bytes, window->path->string);
	EvaluateCommand(buffer);

	if (strstr(evaluateResult, "No such file or directory.")) {
		UIDialogShow(windowMain, 0, "The executable path is invalid.\n%f%B", "OK");
		return;
	}

	StringFormat(buffer, sizeof(buffer), "start %.*s", window->arguments->bytes, window->arguments->string);
	EvaluateCommand(buffer);

	if (window->askDirectory->check == UI_CHECK_CHECKED) {
		CommandParseInternal("gf-get-pwd", true);
	}

	if (!pause) {
		CommandParseInternal("run", false);
	} else {
		DebuggerGetStack();
		DisplaySetPositionFromStack();
	}
}

void ExecutableWindowRunButton(void *_window) {
	ExecutableWindowStartOrRun((ExecutableWindow *) _window, false);
}

void ExecutableWindowStartButton(void *_window) {
	ExecutableWindowStartOrRun((ExecutableWindow *) _window, true);
}

void ExecutableWindowSaveButton(void *_window) {
	ExecutableWindow *window = (ExecutableWindow *) _window;
	FILE *f = fopen(localConfigPath, "rb");

	if (f) {
		const char *result = UIDialogShow(windowMain, 0, ".project.gf already exists in the current directory.\n%f%B%C", "Overwrite", "Cancel");
		if (strcmp(result, "Overwrite")) return;
		fclose(f);
	}

	f = fopen(localConfigPath, "wb");
	fprintf(f, "[executable]\npath=%.*s\narguments=%.*s\nask_directory=%c\n",
			(int) window->path->bytes, window->path->string,
			(int) window->arguments->bytes, window->arguments->string,
			window->askDirectory->check == UI_CHECK_CHECKED ? '1' : '0');
	fclose(f);
	SettingsAddTrustedFolder();
	UIDialogShow(windowMain, 0, "Saved executable settings!\n%f%B", "OK");
}

UIElement *ExecutableWindowCreate(UIElement *parent) {
	ExecutableWindow *window = (ExecutableWindow *) calloc(1, sizeof(ExecutableWindow));
	UIPanel *panel = UIPanelCreate(parent, UI_PANEL_COLOR_1 | UI_PANEL_EXPAND);
	UILabelCreate(&panel->e, 0, "Path to executable:", -1);
	window->path = UITextboxCreate(&panel->e, 0);
	UITextboxReplace(window->path, executablePath, -1, false);
	UILabelCreate(&panel->e, 0, "Command line arguments:", -1);
	window->arguments = UITextboxCreate(&panel->e, 0);
	UITextboxReplace(window->arguments, executableArguments, -1, false);
	window->askDirectory = UICheckboxCreate(&panel->e, 0, "Ask GDB for working directory", -1);
	window->askDirectory->check = executableAskDirectory ? UI_CHECK_CHECKED : UI_CHECK_UNCHECKED;
	UIPanel *row = UIPanelCreate(&panel->e, UI_PANEL_HORIZONTAL);
	UIButton *button;
	button = UIButtonCreate(&row->e, 0, "Run", -1);
	button->e.cp = window;
	button->invoke = ExecutableWindowRunButton;
	button = UIButtonCreate(&row->e, 0, "Start", -1);
	button->e.cp = window;
	button->invoke = ExecutableWindowStartButton;
	UISpacerCreate(&row->e, 0, 10, 0);
	button = UIButtonCreate(&row->e, 0, "Save to .project.gf", -1);
	button->e.cp = window;
	button->invoke = ExecutableWindowSaveButton;
	return &panel->e;
}
