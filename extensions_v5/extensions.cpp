#include <semaphore.h>

//////////////////////////////////////////////////////
// Utilities:
//////////////////////////////////////////////////////

__attribute__((optimize("-O3")))
void ThumbnailResize(uint32_t *bits, uint32_t originalWidth, uint32_t originalHeight, uint32_t targetWidth, uint32_t targetHeight) {
	float cx = (float) originalWidth / targetWidth;
	float cy = (float) originalHeight / targetHeight;

	for (uint32_t i = 0; i < originalHeight; i++) {
		uint32_t *output = bits + i * originalWidth;
		uint32_t *input = output;

		for (uint32_t j = 0; j < targetWidth; j++) {
			uint32_t sumAlpha = 0, sumRed = 0, sumGreen = 0, sumBlue = 0;
			uint32_t count = (uint32_t) ((j + 1) * cx) - (uint32_t) (j * cx);

			for (uint32_t k = 0; k < count; k++, input++) {
				uint32_t pixel = *input;
				sumAlpha += (pixel >> 24) & 0xFF;
				sumRed   += (pixel >> 16) & 0xFF;
				sumGreen += (pixel >>  8) & 0xFF;
				sumBlue  += (pixel >>  0) & 0xFF;
			}

			sumAlpha /= count;
			sumRed   /= count;
			sumGreen /= count;
			sumBlue  /= count;

			*output = (sumAlpha << 24) | (sumRed << 16) | (sumGreen << 8) | (sumBlue << 0);
			output++;
		}
	}

	for (uint32_t i = 0; i < targetWidth; i++) {
		uint32_t *output = bits + i;
		uint32_t *input = output;

		for (uint32_t j = 0; j < targetHeight; j++) {
			uint32_t sumAlpha = 0, sumRed = 0, sumGreen = 0, sumBlue = 0;
			uint32_t count = (uint32_t) ((j + 1) * cy) - (uint32_t) (j * cy);

			for (uint32_t k = 0; k < count; k++, input += originalWidth) {
				uint32_t pixel = *input;
				sumAlpha += (pixel >> 24) & 0xFF;
				sumRed   += (pixel >> 16) & 0xFF;
				sumGreen += (pixel >>  8) & 0xFF;
				sumBlue  += (pixel >>  0) & 0xFF;
			}

			sumAlpha /= count;
			sumRed   /= count;
			sumGreen /= count;
			sumBlue  /= count;

			*output = (sumAlpha << 24) | (sumRed << 16) | (sumGreen << 8) | (sumBlue << 0);
			output += originalWidth;
		}
	}

	for (uint32_t i = 0; i < targetHeight; i++) {
		for (uint32_t j = 0; j < targetWidth; j++) {
			bits[i * targetWidth + j] = bits[i * originalWidth + j];
		}
	}
}

/////////////////////////////////////////////////////
// Profiler:
//////////////////////////////////////////////////////

// TODO Inclusive/exclusive timing switch.
// TODO Horizontal scale modes (e.g. make all leaf calls equal width).
// TODO Coloring the flame graph based on other parameters?
// TODO Watching expressions during profiled step; highlight entries that modify it.

struct ProfProfilingEntry {
	void *thisFunction;
	uint64_t timeStamp; // High bit set if exiting the function.
};

struct ProfWindow {
	uint64_t ticksPerMs;
	UIFont *fontFlameGraph;
	bool inStepOverProfiled;
};

struct ProfFlameGraphEntry {
	void *thisFunction;
	const char *cName;
	double startTime, endTime;
	int depth;
	uint8_t colorIndex;
};

struct ProfFlameGraphEntryTime {
	// Keep this structure as small as possible!
	float start, end;
	int depth;
};

struct ProfSourceFileEntry {
	char cPath[256];
};

struct ProfFunctionEntry {
	uint32_t callCount;
	int lineNumber;
	int sourceFileIndex;
	double totalTime;
	char cName[64];
};

struct ProfFlameGraphReport {
	UIElement e;
	UIRectangle client;
	UIFont *font;
	UITable *table;
	UIButton *switchViewButton;
	UIScrollBar *vScroll;
	bool showingTable;

	Array<ProfFlameGraphEntry> entries;
	Array<ProfFlameGraphEntryTime> entryTimes;
	Array<ProfFunctionEntry> sortedFunctions;
	MapShort<void *, ProfFunctionEntry> functions;
	Array<ProfSourceFileEntry> sourceFiles;

	uint32_t *thumbnail;
	int thumbnailWidth, thumbnailHeight;

	double totalTime;
	double xStart, xEnd;

	ProfFlameGraphEntry *hover;
	ProfFlameGraphEntry *menuItem;

#define FLAME_GRAPH_DRAG_ZOOM_RANGE (1)
#define FLAME_GRAPH_DRAG_PAN (2)
#define FLAME_GRAPH_DRAG_X_PAN_AND_ZOOM (3)
#define FLAME_GRAPH_DRAG_X_SCROLL (4)
	bool dragStarted;
	int dragMode;
	double dragInitialValue, dragInitialValue2;
	int dragInitialPoint, dragInitialPoint2;
	int dragCurrentPoint;
	double dragScrollRate;
};

const uint32_t profMainColor = 0xFFBFC1C3;
const uint32_t profHoverColor = 0xFFBFC1FF;
const uint32_t profBorderLightColor = 0xFFFFFFFF;
const uint32_t profBorderDarkColor = 0xFF000000;
const uint32_t profBackgroundColor = 0xFF505153;
const uint32_t profTextColor = 0xFF000000;

const uint32_t profEntryColorPalette[] = {
	0xFFE5A0A0, 0xFFDBA0E5, 0xFFA0B5E5, 0xFFA0E5C6, 0xFFC9E5A0, 0xFFE5B1A0, 0xFFE5A0DE, 0xFFA0A4E5,
	0xFFA0E5D7, 0xFFB8E5A0, 0xFFE5C3A0, 0xFFE5A0CD, 0xFFAEA0E5, 0xFFA0E2E5, 0xFFA7E5A0, 0xFFE5D4A0,
	0xFFE5A0BC, 0xFFBFA0E5, 0xFFA0D0E5, 0xFFA0E5AA, 0xFFE5E5A0, 0xFFE5A0AA, 0xFFD0A0E5, 0xFFA0BFE5,
	0xFFA0E5BC, 0xFFD4E5A0, 0xFFE5A7A0, 0xFFE2A0E5, 0xFFA0AEE5, 0xFFA0E5CD, 0xFFC3E5A0, 0xFFE5B8A0,
	0xFFE5A0D7, 0xFFA4A0E5, 0xFFA0E5DE, 0xFFB1E5A0, 0xFFE5C9A0, 0xFFE5A0C6, 0xFFB5A0E5, 0xFFA0DBE5,
	0xFFA0E5A0, 0xFFE5DBA0, 0xFFE5A0B5, 0xFFC6A0E5, 0xFFA0C9E5, 0xFFA0E5B1, 0xFFDEE5A0, 0xFFE5A0A4,
	0xFFD7A0E5, 0xFFA0B8E5, 0xFFA0E5C3, 0xFFCDE5A0, 0xFFE5AEA0, 0xFFE5A0E2, 0xFFA0A7E5, 0xFFA0E5D4,
	0xFFBCE5A0, 0xFFE5BFA0, 0xFFE5A0D0, 0xFFAAA0E5, 0xFFA0E5E5, 0xFFAAE5A0, 0xFFE5D0A0, 0xFFE5A0BF,
	0xFFBCA0E5, 0xFFA0D4E5, 0xFFA0E5A7, 0xFFE5E2A0, 0xFFE5A0AE, 0xFFCDA0E5, 0xFFA0C3E5, 0xFFA0E5B8,
	0xFFD7E5A0, 0xFFE5A4A0, 0xFFDEA0E5, 0xFFA0B1E5, 0xFFA0E5C9, 0xFFC6E5A0, 0xFFE5B5A0, 0xFFE5A0DB,
};

const int profZoomBarHeight = 30;
const int profScaleHeight = 20;
const int profRowHeight = 30;

#define PROF_MAX_RENDER_THREAD_COUNT (8)
pthread_t profRenderThreads[PROF_MAX_RENDER_THREAD_COUNT];
sem_t profRenderStartSemaphores[PROF_MAX_RENDER_THREAD_COUNT];
sem_t profRenderEndSemaphore;
UIPainter *volatile profRenderPainter;
ProfFlameGraphReport *volatile profRenderReport;
int profRenderThreadCount;
volatile int profRenderThreadIndexAllocator;
volatile int profRenderActiveThreads;

int ProfFlameGraphEntryCompare(const void *_a, const void *_b) {
	ProfFlameGraphEntry *a = (ProfFlameGraphEntry *) _a;
	ProfFlameGraphEntry *b = (ProfFlameGraphEntry *) _b;
	return a->depth > b->depth ? 1 : a->depth < b->depth ? -1 : a->startTime > b->startTime ? 1 : a->startTime < b->startTime ? -1 : 0;
}

void ProfShowSource(void *_report) {
	ProfFlameGraphReport *report = (ProfFlameGraphReport *) _report;
	ProfFlameGraphEntry *entry = report->menuItem;
	ProfFunctionEntry *function = report->functions.At(entry->thisFunction, false);

	if (!function->cName[0]) {
		UIDialogShow(windowMain, 0, "Source information was not found for this function.\n%f%b", "OK");
		return;
	} else {
		DisplaySetPosition(report->sourceFiles[function->sourceFileIndex].cPath, function->lineNumber, false);
	}
}

void ProfAddBreakpoint(void *_entry) {
	ProfFlameGraphEntry *entry = (ProfFlameGraphEntry *) _entry;
	char buffer[80];
	StringFormat(buffer, sizeof(buffer), "b %s", entry->cName);
	CommandSendToGDB(buffer);
}

void ProfFillView(void *_report) {
	ProfFlameGraphReport *report = (ProfFlameGraphReport *) _report;
	ProfFlameGraphEntry *entry = report->menuItem;
	report->xStart = entry->startTime;
	report->xEnd = entry->endTime;
	UIElementRepaint(&report->e, 0);
}

void ProfDrawTransparentOverlay(UIPainter *painter, UIRectangle rectangle, uint32_t color) {
	rectangle = UIRectangleIntersection(painter->clip, rectangle);
	if (!UI_RECT_VALID(rectangle)) return;

	for (int line = rectangle.t; line < rectangle.b; line++) {
		uint32_t *bits = painter->bits + line * painter->width + rectangle.l;

		for (int x = 0; x < UI_RECT_WIDTH(rectangle); x++) {
			uint32_t original = bits[x];
			uint32_t m1 = 180;
			uint32_t m2 = 255 - m1;
			uint32_t r2 = m2 * (original & 0x00FF00FF);
			uint32_t g2 = m2 * (original & 0x0000FF00);
			uint32_t r1 = m1 * (color & 0x00FF00FF);
			uint32_t g1 = m1 * (color & 0x0000FF00);
			uint32_t result = 0xFF000000 | (0x0000FF00 & ((g1 + g2) >> 8)) | (0x00FF00FF & ((r1 + r2) >> 8));
			bits[x] = result;
		}
	}
}

#define PROFILER_ENTRY_RECTANGLE_EARLY() \
	int64_t rr = report->client.l + (int64_t) ((time->end - report->xStart) * zoomX + 0.999);
#define PROFILER_ENTRY_RECTANGLE_OTHER() \
	int64_t rl = report->client.l + (int64_t) ((time->start - report->xStart) * zoomX); \
	int64_t rt = report->client.t + time->depth * profRowHeight + profScaleHeight - report->vScroll->position; \
	int64_t rb = rt + profRowHeight;

void *ProfFlameGraphRenderThread(void *_unused) {
	(void) _unused;
	int threadIndex = __sync_fetch_and_add(&profRenderThreadIndexAllocator, 1);

	while (true) {
		sem_wait(&profRenderStartSemaphores[threadIndex]);

		ProfFlameGraphReport *report = profRenderReport;
		UIElement *element = &report->e;

		double zoomX = (double) UI_RECT_WIDTH(report->client) / (report->xEnd - report->xStart);
		UIPainter _painter = *profRenderPainter; // Some of the draw functions modify the painter's clip, so make a copy.
		UIPainter *painter = &_painter;

		int64_t pr = 0, pd = 0;
		float xStartF = (float) report->xStart;
		float xEndF = (float) report->xEnd;

		int startIndex = report->entries.Length() / profRenderThreadIndexAllocator * threadIndex;
		int endIndex = report->entries.Length() / profRenderThreadIndexAllocator * (threadIndex + 1);

		if (profRenderThreadCount == threadIndex + 1) {
			endIndex = report->entries.Length();
		}

		// printf("render on thread %d from %d to %d\n", threadIndex, startIndex, endIndex);

		for (int i = startIndex; i < endIndex; i++) {
			ProfFlameGraphEntryTime *time = &report->entryTimes[i];

			if (time->end < xStartF || time->start > xEndF) {
				continue;
			}

			PROFILER_ENTRY_RECTANGLE_EARLY();

			if (pr == rr && pd == time->depth) {
				continue;
			}

			ProfFlameGraphEntry *entry = &report->entries[i];
			PROFILER_ENTRY_RECTANGLE_OTHER();

			if (rl <= element->clip.r && rr >= element->clip.l && rt <= element->clip.b && rb >= element->clip.t) {
				// Carefully convert 64-bit integers to 32-bit integers for UIRectangle,
				// since the rectangle may be really large when zoomed in.
				UIRectangle r;
				r.l = rl < report->client.l ? report->client.l : rl;
				r.r = rr > report->client.r ? report->client.r : rr;
				r.t = rt < report->client.t ? report->client.t : rt;
				r.b = rb > report->client.b ? report->client.b : rb;

				UIDrawBlock(painter, UI_RECT_4(r.r - 1, r.r, r.t, r.b - 1), profBorderDarkColor);
				UIDrawBlock(painter, UI_RECT_4(r.l, r.r, r.b - 1, r.b), profBorderDarkColor);
				UIDrawBlock(painter, UI_RECT_4(r.l, r.r - 1, r.t, r.t + 1), profBorderLightColor);
				UIDrawBlock(painter, UI_RECT_4(r.l, r.l + 1, r.t + 1, r.b - 1), profBorderLightColor);

				bool hovered = report->hover && report->hover->thisFunction == entry->thisFunction && !report->dragMode;
				uint32_t color = hovered ? profHoverColor : profEntryColorPalette[entry->colorIndex];
				/// uint32_t color = hovered ? profHoverColor : profMainColor;
				UIDrawBlock(painter, UI_RECT_4(r.l + 1, r.r - 1, r.t + 1, r.b - 1), color);

				if (UI_RECT_WIDTH(r) > 40) {
					char string[128];
					StringFormat(string, sizeof(string), "%s %fms", entry->cName, entry->endTime - entry->startTime);
					UIDrawString(painter, UI_RECT_4(r.l + 2, r.r, r.t, r.b), string, -1, profTextColor, UI_ALIGN_LEFT, NULL);
				}
			}

			pr = rr, pd = entry->depth;

			float nextDrawTime = 0.99f / zoomX + time->end;

			for (; i < report->entries.Length(); i++) {
				if (report->entryTimes[i].end >= nextDrawTime || report->entryTimes[i].depth != time->depth) {
					i--;
					break;
				}
			}
		}

		__sync_fetch_and_sub(&profRenderActiveThreads, 1);
		sem_post(&profRenderEndSemaphore);
	}
}

int ProfFlameGraphMessage(UIElement *element, UIMessage message, int di, void *dp) {
	ProfFlameGraphReport *report = (ProfFlameGraphReport *) element;

	if (message == UI_MSG_PAINT) {
		UIFont *previousFont = UIFontActivate(report->font);

		if (report->xStart < 0) report->xStart = 0;
		if (report->xEnd > report->totalTime) report->xEnd = report->totalTime;
		if (report->xEnd < report->xStart + 1e-7) report->xEnd = report->xStart + 1e-7;

		double zoomX = (double) UI_RECT_WIDTH(report->client) / (report->xEnd - report->xStart);

		if (!profRenderThreadCount) {
			profRenderThreadCount = sysconf(_SC_NPROCESSORS_CONF);
			if (profRenderThreadCount < 1) profRenderThreadCount = 1;
			if (profRenderThreadCount > PROF_MAX_RENDER_THREAD_COUNT) profRenderThreadCount = PROF_MAX_RENDER_THREAD_COUNT;
			printf("Using %d render threads.\n", profRenderThreadCount);

			sem_init(&profRenderEndSemaphore, 0, 0);

			for (int i = 0; i < profRenderThreadCount; i++) {
				sem_init(&profRenderStartSemaphores[i], 0, 0);
				pthread_create(&profRenderThreads[i], nullptr, ProfFlameGraphRenderThread, report);
			}
		}

		UIPainter *painter = (UIPainter *) dp;
		UIDrawBlock(painter, report->client, profBackgroundColor);

		profRenderReport = report;
		profRenderPainter = painter;
		profRenderActiveThreads = profRenderThreadCount;
		__sync_synchronize();
		for (int i = 0; i < profRenderThreadCount; i++) sem_post(&profRenderStartSemaphores[i]);
		for (int i = 0; i < profRenderThreadCount; i++) sem_wait(&profRenderEndSemaphore);
		assert(!__sync_fetch_and_sub(&profRenderActiveThreads, 1));

		{
			UIRectangle r = UI_RECT_4(report->client.l, report->client.r, report->client.t, report->client.t + profScaleHeight);
			UIDrawRectangle(painter, r, profMainColor, profBorderDarkColor, UI_RECT_4(0, 0, 0, 1));

			double increment = 1000.0;
			while (increment > 1e-6 && increment * zoomX > 600.0) increment *= 0.1;

			double start = (painter->clip.l - report->client.l) / zoomX + report->xStart;
			start -= fmod(start, increment) + increment;

			for (double i = start; i < report->totalTime; i += increment) {
				UIRectangle r;
				r.t = report->client.t;
				r.b = r.t + profScaleHeight;
				r.l = report->client.l + (int) ((i - report->xStart) * zoomX);
				r.r = r.l + (int) (increment * zoomX);
				if (r.l > painter->clip.r) break;
				char string[128];
				StringFormat(string, sizeof(string), "%.4fms", i);
				UIDrawBlock(painter, UI_RECT_4(r.l, r.l + 1, r.t, r.b), profBorderLightColor);
				UIDrawString(painter, r, string, -1, profTextColor, UI_ALIGN_LEFT, NULL);
			}
		}

		if (report->dragMode == FLAME_GRAPH_DRAG_ZOOM_RANGE) {
			UIRectangle r = report->client;
			r.l = report->dragInitialPoint, r.r = report->dragCurrentPoint;
			if (r.l > r.r) r.r = report->dragInitialPoint, r.l = report->dragCurrentPoint;
			UIDrawInvert(painter, r);
		}

		if (report->thumbnail) {
			UIRectangle zoomBar = UI_RECT_4(report->client.l, report->client.r, report->client.b - profZoomBarHeight, report->client.b);
			UIRectangle zoomBarThumb = zoomBar;
			zoomBarThumb.l = zoomBar.l + UI_RECT_WIDTH(zoomBar) * (report->xStart / report->totalTime);
			zoomBarThumb.r = zoomBar.l + UI_RECT_WIDTH(zoomBar) * (report->xEnd   / report->totalTime);
			UIRectangle drawBounds = UIRectangleIntersection(zoomBar, painter->clip);

			for (int i = drawBounds.t; i < drawBounds.b; i++) {
				for (int j = drawBounds.l; j < drawBounds.r; j++) {
					int si = (i - zoomBar.t) * report->thumbnailHeight / UI_RECT_HEIGHT(zoomBar);
					int sj = (j - zoomBar.l) * report->thumbnailWidth / UI_RECT_WIDTH(zoomBar);

					if (si >= 0 && si < report->thumbnailHeight && sj >= 0 && sj < report->thumbnailWidth) {
						painter->bits[i * painter->width + j] = report->thumbnail[si * report->thumbnailWidth + sj];
					}
				}
			}

			UIDrawBorder(painter, zoomBar, profBorderDarkColor, UI_RECT_1(2));
			UIDrawBorder(painter, zoomBarThumb, profBorderLightColor, UI_RECT_1(4));
		}

		if (report->hover && !report->dragMode) {
			ProfFunctionEntry function = report->functions.Get(report->hover->thisFunction);

			char line1[256], line2[256], line3[256];
			StringFormat(line1, sizeof(line1), "[%s] %s:%d", report->hover->cName, 
					function.sourceFileIndex != -1 ? report->sourceFiles[function.sourceFileIndex].cPath : "??",
					function.lineNumber);
			StringFormat(line2, sizeof(line2), "This call: %fms %.1f%%",
					report->hover->endTime - report->hover->startTime, (report->hover->endTime - report->hover->startTime) / report->totalTime * 100.0);
			StringFormat(line3, sizeof(line3), "Total: %fms in %d calls (%fms avg) %.1f%%", 
					function.totalTime, function.callCount, function.totalTime / function.callCount, function.totalTime / report->totalTime * 100.0);

			int width = 0;
			int line1Width = UIMeasureStringWidth(line1, -1); if (width < line1Width) width = line1Width;
			int line2Width = UIMeasureStringWidth(line2, -1); if (width < line2Width) width = line2Width;
			int line3Width = UIMeasureStringWidth(line3, -1); if (width < line3Width) width = line3Width;
			int lineHeight = UIMeasureStringHeight();
			int height = 3 * lineHeight;
			int x = element->window->cursorX;
			if (x + width > element->clip.r) x = element->clip.r - width;
			int y = element->window->cursorY + 25;
			if (y + height > element->clip.b) y = element->window->cursorY - height - 10;
			UIRectangle rectangle = UI_RECT_4(x, x + width, y, y + height);

			ProfDrawTransparentOverlay(painter, UIRectangleAdd(rectangle, UI_RECT_1I(-5)), 0xFF000000);
			UIDrawString(painter, UI_RECT_4(x, x + width, y + lineHeight * 0, y + lineHeight * 1), line1, -1, 0xFFFFFFFF, UI_ALIGN_LEFT, 0);
			UIDrawString(painter, UI_RECT_4(x, x + width, y + lineHeight * 1, y + lineHeight * 2), line2, -1, 0xFFFFFFFF, UI_ALIGN_LEFT, 0);
			UIDrawString(painter, UI_RECT_4(x, x + width, y + lineHeight * 2, y + lineHeight * 3), line3, -1, 0xFFFFFFFF, UI_ALIGN_LEFT, 0);
		}

		UIFontActivate(previousFont);
	} else if (message == UI_MSG_MOUSE_MOVE) {
		double zoomX = (double) UI_RECT_WIDTH(report->client) / (report->xEnd - report->xStart);
		ProfFlameGraphEntry *hover = nullptr;
		int depth = (element->window->cursorY - report->client.t + report->vScroll->position - profScaleHeight) / profRowHeight;
		float xStartF = (float) report->xStart;
		float xEndF = (float) report->xEnd;

		for (int i = 0; i < report->entries.Length(); i++) {
			ProfFlameGraphEntryTime *time = &report->entryTimes[i];

			if (time->depth != depth || time->end < xStartF || time->start > xEndF) {
				continue;
			}

			PROFILER_ENTRY_RECTANGLE_EARLY();
			PROFILER_ENTRY_RECTANGLE_OTHER();

			(void) rt;
			(void) rb;

			if (element->window->cursorX >= rl && element->window->cursorX < rr) {
				hover = &report->entries[i];
				break;
			}
		}

		if (hover != report->hover || hover /* to repaint the tooltip */) {
			report->hover = hover;
			UIElementRepaint(element, NULL);
		}
	} else if (message == UI_MSG_UPDATE) {
		if (report->hover && element->window->hovered != element) {
			report->hover = NULL;
			UIElementRepaint(element, NULL);
		}
	} else if (message == UI_MSG_LEFT_DOWN) {
		if (element->window->cursorY < report->client.b - profZoomBarHeight) {
			report->dragMode = FLAME_GRAPH_DRAG_PAN;
			report->dragInitialValue = report->xStart;
			report->dragInitialPoint = element->window->cursorX;
			report->dragInitialValue2 = report->vScroll->position;
			report->dragInitialPoint2 = element->window->cursorY;
			_UIWindowSetCursor(element->window, UI_CURSOR_HAND);
		} else {
			report->dragMode = FLAME_GRAPH_DRAG_X_SCROLL;
			report->dragInitialValue = report->xStart;
			report->dragInitialPoint = element->window->cursorX;
			report->dragScrollRate = 1.0;

			if (element->window->cursorX < report->client.l + UI_RECT_WIDTH(report->client) * (report->xStart / report->totalTime)
					|| element->window->cursorY >= report->client.l + UI_RECT_WIDTH(report->client) * (report->xEnd   / report->totalTime)) {
				report->dragScrollRate = 0.2;
			}
		}
	} else if (message == UI_MSG_MIDDLE_DOWN) {
		if (element->window->cursorY < report->client.b - profZoomBarHeight) {
			report->dragMode = FLAME_GRAPH_DRAG_X_PAN_AND_ZOOM;
			report->dragInitialValue = report->xStart;
			report->dragInitialPoint = element->window->cursorX;
			report->dragInitialPoint2 = element->window->cursorY;
			_UIWindowSetCursor(element->window, UI_CURSOR_CROSS_HAIR);
		}
	} else if (message == UI_MSG_RIGHT_DOWN) {
		if (element->window->cursorY < report->client.b - profZoomBarHeight) {
			report->dragMode = FLAME_GRAPH_DRAG_ZOOM_RANGE;
			report->dragInitialPoint = element->window->cursorX;
		}
	} else if (message == UI_MSG_LEFT_UP || message == UI_MSG_RIGHT_UP || message == UI_MSG_MIDDLE_UP) {
		if (report->dragMode == FLAME_GRAPH_DRAG_ZOOM_RANGE && report->dragStarted) {
			UIRectangle r = report->client;
			r.l = report->dragInitialPoint, r.r = report->dragCurrentPoint;
			if (r.l > r.r) r.r = report->dragInitialPoint, r.l = report->dragCurrentPoint;
			double zoomX = (double) UI_RECT_WIDTH(report->client) / (report->xEnd - report->xStart);
			report->xEnd = (r.r - report->client.l) / zoomX + report->xStart;
			report->xStart = (r.l - report->client.l) / zoomX + report->xStart;
		} else if (!report->dragStarted && message == UI_MSG_RIGHT_UP && report->hover) {
			report->menuItem = report->hover;
			UIMenu *menu = UIMenuCreate(&element->window->e, UI_MENU_NO_SCROLL);
			UIMenuAddItem(menu, 0, "Show source", -1, ProfShowSource, report);
			UIMenuAddItem(menu, 0, "Add breakpoint", -1, ProfAddBreakpoint, report->hover);
			UIMenuAddItem(menu, 0, "Fill view", -1, ProfFillView, report);
			UIMenuShow(menu);
		} else if (!report->dragStarted && message == UI_MSG_MIDDLE_UP && report->hover) {
			report->menuItem = report->hover;
			ProfFillView(report);
		}

		report->dragMode = 0;
		report->dragStarted = false;
		UIElementRepaint(element, NULL);
		_UIWindowSetCursor(element->window, UI_CURSOR_ARROW);
	} else if (message == UI_MSG_MOUSE_DRAG) {
		report->dragStarted = true;

		if (report->dragMode == FLAME_GRAPH_DRAG_PAN) {
			double delta = report->xEnd - report->xStart;
			report->xStart = report->dragInitialValue - (double) (element->window->cursorX - report->dragInitialPoint) 
				* report->totalTime / UI_RECT_WIDTH(report->client) * delta / report->totalTime;
			report->xEnd = report->xStart + delta;
			if (report->xStart < 0) { report->xEnd -= report->xStart; report->xStart = 0; }
			if (report->xEnd > report->totalTime) { report->xStart += report->totalTime - report->xEnd; report->xEnd = report->totalTime; }
			report->vScroll->position = report->dragInitialValue2 - (double) (element->window->cursorY - report->dragInitialPoint2);
			UIElementRefresh(&report->vScroll->e);
		} else if (report->dragMode == FLAME_GRAPH_DRAG_X_SCROLL) {
			double delta = report->xEnd - report->xStart;
			report->xStart = report->dragInitialValue + (double) (element->window->cursorX - report->dragInitialPoint) 
				* report->totalTime / UI_RECT_WIDTH(report->client) * report->dragScrollRate;
			report->xEnd = report->xStart + delta;
			if (report->xStart < 0) { report->xEnd -= report->xStart; report->xStart = 0; }
			if (report->xEnd > report->totalTime) { report->xStart += report->totalTime - report->xEnd; report->xEnd = report->totalTime; }
		} else if (report->dragMode == FLAME_GRAPH_DRAG_X_PAN_AND_ZOOM) {
			double delta = report->xEnd - report->xStart;
			report->xStart += (double) (element->window->cursorX - report->dragInitialPoint) 
				* report->totalTime / UI_RECT_WIDTH(report->client) * delta / report->totalTime * 3.0;
			report->xEnd = report->xStart + delta;
			double factor = powf(1.02, element->window->cursorY - report->dragInitialPoint2);
			double mouse = (double) (element->window->cursorX - report->client.l) / UI_RECT_WIDTH(report->client);
#if 0
			mouse = 0.5;
			XWarpPointer(ui.display, None, windowMain->window, 0, 0, 0, 0, report->dragInitialPoint, report->dragInitialPoint2);
#else
			report->dragInitialPoint = element->window->cursorX;
			report->dragInitialPoint2 = element->window->cursorY;
#endif
			double newZoom = (report->xEnd - report->xStart) / report->totalTime * factor;
			report->xStart += mouse * (report->xEnd - report->xStart) * (1 - factor);
			report->xEnd = newZoom * report->totalTime + report->xStart;
		} else if (report->dragMode == FLAME_GRAPH_DRAG_ZOOM_RANGE) {
			report->dragCurrentPoint = element->window->cursorX;
		}

		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_MOUSE_WHEEL) {
		int divisions = di / 72;
		double factor = 1;
		double perDivision = 1.2f;
		while (divisions > 0) factor *= perDivision, divisions--;
		while (divisions < 0) factor /= perDivision, divisions++;
		double mouse = (double) (element->window->cursorX - report->client.l) / UI_RECT_WIDTH(report->client);
		double newZoom = (report->xEnd - report->xStart) / report->totalTime * factor;
		report->xStart += mouse * (report->xEnd - report->xStart) * (1 - factor);
		report->xEnd = newZoom * report->totalTime + report->xStart;
		UIElementRepaint(element, NULL);
		return 1;
	} else if (message == UI_MSG_GET_CURSOR) {
		return report->dragMode == FLAME_GRAPH_DRAG_PAN ? UI_CURSOR_HAND 
			: report->dragMode == FLAME_GRAPH_DRAG_X_PAN_AND_ZOOM ? UI_CURSOR_CROSS_HAIR
			: UI_CURSOR_ARROW;
	} else if (message == UI_MSG_LAYOUT) {
		UIRectangle scrollBarBounds = element->bounds;
		scrollBarBounds.l = scrollBarBounds.r - UI_SIZE_SCROLL_BAR * element->window->scale;
		report->vScroll->page = UI_RECT_HEIGHT(element->bounds) - profZoomBarHeight;
		UIElementMove(&report->vScroll->e, scrollBarBounds, true);
		report->client = element->bounds;
		report->client.r = scrollBarBounds.l;
	} else if (message == UI_MSG_SCROLLED) {
		UIElementRefresh(element);
	} else if (message == UI_MSG_DESTROY) {
		report->entries.Free();
		report->functions.Free();
		report->sourceFiles.Free();
		report->entryTimes.Free();
		free(report->thumbnail);
	}

	return 0;
}

int ProfReportWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	ProfFlameGraphReport *report = (ProfFlameGraphReport *) element->cp;
	
	if (message == UI_MSG_LAYOUT) {
		if (report->showingTable) { report->e.flags |=  UI_ELEMENT_HIDE; report->table->e.flags &= ~UI_ELEMENT_HIDE; }
		else                      { report->e.flags &= ~UI_ELEMENT_HIDE; report->table->e.flags |=  UI_ELEMENT_HIDE; }
		element->messageClass(element, message, di, dp);
		UIElementMove(&report->table->e, report->e.bounds, false);
		return 1;
	}

	return 0;
}

void ProfSwitchView(void *_report) {
	ProfFlameGraphReport *report = (ProfFlameGraphReport *) _report;
	report->showingTable = !report->showingTable;
	UI_FREE(report->switchViewButton->label);
	report->switchViewButton->label = UIStringCopy(report->showingTable ? "Graph view" : "Table view", (report->switchViewButton->labelBytes = -1));
	UIElementRefresh(report->e.parent);
}

#define PROF_FUNCTION_COMPARE(a, b) \
       int a(const void *c, const void *d) { \
	       const ProfFunctionEntry *left = (const ProfFunctionEntry *) c; \
	       const ProfFunctionEntry *right = (const ProfFunctionEntry *) d; \
	       return b; \
       }
#define PROF_COMPARE_NUMBERS(a, b) \
	(a) > (b) ? -1 : (a) < (b) ? 1 : 0

PROF_FUNCTION_COMPARE(ProfFunctionCompareName, strcmp(left->cName, right->cName));
PROF_FUNCTION_COMPARE(ProfFunctionCompareTotalTime, PROF_COMPARE_NUMBERS(left->totalTime, right->totalTime));
PROF_FUNCTION_COMPARE(ProfFunctionCompareCallCount, PROF_COMPARE_NUMBERS(left->callCount, right->callCount));
PROF_FUNCTION_COMPARE(ProfFunctionCompareAverage, PROF_COMPARE_NUMBERS(left->totalTime / left->callCount, right->totalTime / right->callCount));
PROF_FUNCTION_COMPARE(ProfFunctionComparePercentage, PROF_COMPARE_NUMBERS(left->totalTime, right->totalTime));

int ProfTableMessage(UIElement *element, UIMessage message, int di, void *dp) {
	ProfFlameGraphReport *report = (ProfFlameGraphReport *) element->cp;
	UITable *table = report->table;

	if (message == UI_MSG_TABLE_GET_ITEM) {
		UITableGetItem *m = (UITableGetItem *) dp;
		ProfFunctionEntry *entry = &report->sortedFunctions[m->index];

		if (m->column == 0) {
			return StringFormat(m->buffer, m->bufferBytes, "%s", entry->cName);
		} else if (m->column == 1) {
			return StringFormat(m->buffer, m->bufferBytes, "%f", entry->totalTime);
		} else if (m->column == 2) {
			return StringFormat(m->buffer, m->bufferBytes, "%d", entry->callCount);
		} else if (m->column == 3) {
			return StringFormat(m->buffer, m->bufferBytes, "%f", entry->totalTime / entry->callCount);
		} else if (m->column == 4) {
			return StringFormat(m->buffer, m->bufferBytes, "%f", entry->totalTime / report->totalTime * 100);
		}
	} else if (message == UI_MSG_LEFT_DOWN) {
		int index = UITableHeaderHitTest(table, element->window->cursorX, element->window->cursorY);

		if (index != -1) {
			if (index == 0) {
				qsort(report->sortedFunctions.array, report->sortedFunctions.Length(), sizeof(ProfFunctionEntry), ProfFunctionCompareName);
			} else if (index == 1) {
				qsort(report->sortedFunctions.array, report->sortedFunctions.Length(), sizeof(ProfFunctionEntry), ProfFunctionCompareTotalTime);
			} else if (index == 2) {
				qsort(report->sortedFunctions.array, report->sortedFunctions.Length(), sizeof(ProfFunctionEntry), ProfFunctionCompareCallCount);
			} else if (index == 3) {
				qsort(report->sortedFunctions.array, report->sortedFunctions.Length(), sizeof(ProfFunctionEntry), ProfFunctionCompareAverage);
			} else if (index == 4) {
				qsort(report->sortedFunctions.array, report->sortedFunctions.Length(), sizeof(ProfFunctionEntry), ProfFunctionComparePercentage);
			}

			UIElementRefresh(element);
			table->columnHighlight = index;
		}
	} else if (message == UI_MSG_GET_CURSOR) {
		return UITableHeaderHitTest(table, element->window->cursorX, element->window->cursorY) == -1 ? UI_CURSOR_ARROW : UI_CURSOR_HAND;
	}

	return 0;
}

void ProfLoadProfileData(void *_window) {
	ProfWindow *data = (ProfWindow *) _window;

	const char *ticksPerMsString = EvaluateExpression("gfProfilingTicksPerMs");
	ticksPerMsString = ticksPerMsString ? strstr(ticksPerMsString, "= ") : nullptr;
	data->ticksPerMs = ticksPerMsString ? atoi(ticksPerMsString + 2) : 0;

	if (!ticksPerMsString || !data->ticksPerMs) {
		UIDialogShow(windowMain, 0, "Profile data could not be loaded (1).\nConsult the guide.\n%f%b", "OK");
		return;
	}
	
	int rawEntryCount = atoi(strstr(EvaluateExpression("gfProfilingBufferPosition"), "= ") + 2);
	printf("Reading %d profiling entries...\n", rawEntryCount);

	if (rawEntryCount == 0) {
		return;
	}

	if (rawEntryCount > 10000000) {
		// Show a loading message.
		UIWindow *window = windowMain;
		UIPainter painter = {};
		painter.bits = window->bits;
		painter.width = window->width;
		painter.height = window->height;
		painter.clip = UI_RECT_2S(window->width, window->height);
		char string[256];
		StringFormat(string, sizeof(string), "Loading data... (estimated time: %d seconds)", rawEntryCount / 5000000 + 1);
		UIDrawBlock(&painter, painter.clip, ui.theme.panel1);
		UIDrawString(&painter, painter.clip, string, -1, ui.theme.text, UI_ALIGN_CENTER, 0);
		window->updateRegion = UI_RECT_2S(window->width, window->height);
		_UIWindowEndPaint(window, nullptr);
		window->updateRegion = painter.clip;
	}

	ProfProfilingEntry *rawEntries = (ProfProfilingEntry *) calloc(sizeof(ProfProfilingEntry), rawEntryCount);

	char path[PATH_MAX];
	realpath(".profile.gf", path);
	char buffer[PATH_MAX * 2];
	StringFormat(buffer, sizeof(buffer), "dump binary memory %s (gfProfilingBuffer) (gfProfilingBuffer+gfProfilingBufferPosition)", path);
	EvaluateCommand(buffer);
	FILE *f = fopen(path, "rb");

	if (!f) {
		UIDialogShow(windowMain, 0, "Profile data could not be loaded (2).\nConsult the guide.\n%f%b", "OK");
		free(rawEntries);
		return;
	}

	fread(rawEntries, 1, sizeof(ProfProfilingEntry) * rawEntryCount, f);
	fclose(f);
	unlink(path);

	printf("Got raw profile data.\n");

	MapShort<void *, ProfFunctionEntry> functions = {};
	Array<ProfSourceFileEntry> sourceFiles = {};

	int stackErrorCount = 0;
	int stackDepth = 0;

	for (int i = 0; i < rawEntryCount; i++) {
		if (rawEntries[i].timeStamp >> 63) {
			if (stackDepth) stackDepth--;
			else stackErrorCount++;
		} else {
			stackDepth++;
		}

		if (functions.Has(rawEntries[i].thisFunction)) continue;
		ProfFunctionEntry *function = functions.At(rawEntries[i].thisFunction, true);

		function->sourceFileIndex = -1;

		StringFormat(buffer, sizeof(buffer), "(void *) %ld", rawEntries[i].thisFunction);
		const char *cName = EvaluateExpression(buffer);
		if (!cName) continue;

		if (strchr(cName, '<')) cName = strchr(cName, '<') + 1;
		int length = strlen(cName);
		if (length > (int) sizeof(function->cName) - 1) length = sizeof(function->cName) - 1;
		memcpy(function->cName, cName, length);
		function->cName[length] = 0;

		int inTemplate = 0;

		for (int j = 0; j < length; j++) {
			if (function->cName[j] == '(' && !inTemplate) {
				function->cName[j] = 0;
				break;
			} else if (function->cName[j] == '<') {
				inTemplate++;
			} else if (function->cName[j] == '>') {
				if (inTemplate) {
					inTemplate--;
				} else {
					function->cName[j] = 0;
					break;
				}
			}
		}

		StringFormat(buffer, sizeof(buffer), "py print(gdb.lookup_global_symbol('%s').symtab.filename)", function->cName);
		EvaluateCommand(buffer);

		if (!strstr(evaluateResult, "Traceback (most recent call last):")) {
			char *end = strchr(evaluateResult, '\n');
			if (end) *end = 0;
			ProfSourceFileEntry sourceFile = {};
			char *cSourceFile = evaluateResult;
			length = strlen(cSourceFile);
			if (length > (int) sizeof(sourceFile.cPath) - 1) length = sizeof(sourceFile.cPath) - 1;
			memcpy(sourceFile.cPath, cSourceFile, length);
			sourceFile.cPath[length] = 0;
			StringFormat(buffer, sizeof(buffer), "py print(gdb.lookup_global_symbol('%s').line)", function->cName);
			EvaluateCommand(buffer);
			function->lineNumber = atoi(evaluateResult);

			for (int i = 0; i < sourceFiles.Length(); i++) {
				if (0 == strcmp(sourceFiles[i].cPath, sourceFile.cPath)) {
					function->sourceFileIndex = i;
					break;
				}
			}

			if (function->sourceFileIndex == -1) {
				function->sourceFileIndex = sourceFiles.Length();
				sourceFiles.Add(sourceFile);
			}
		}
	}

	UIMDIChild *window = UIMDIChildCreate(&dataWindow->e, UI_MDI_CHILD_CLOSE_BUTTON, UI_RECT_2S(800, 600), "Flame graph", -1);
	UIButton *switchViewButton = UIButtonCreate(&window->e, UI_BUTTON_SMALL | UI_ELEMENT_NON_CLIENT, "Table view", -1);
	UITable *table = UITableCreate(&window->e, 0, "Name\tTime spent (ms)\tCall count\tAverage per call (ms)");
	ProfFlameGraphReport *report = (ProfFlameGraphReport *) UIElementCreate(sizeof(ProfFlameGraphReport), 
			&window->e, 0, ProfFlameGraphMessage, "flame graph");

	report->vScroll = UIScrollBarCreate(&report->e, 0);
	report->font = data->fontFlameGraph;

	window->e.cp = report;
	window->e.messageUser = ProfReportWindowMessage;
	switchViewButton->e.cp = report;
	switchViewButton->invoke = ProfSwitchView;
	table->e.cp = report;
	table->e.messageUser = ProfTableMessage;
	report->switchViewButton = switchViewButton;
	report->table = table;

	report->functions = functions;
	functions = {};
	report->sourceFiles = sourceFiles;
	sourceFiles = {};

	Array<ProfFlameGraphEntry> stack = {};

	for (int i = 0; i < stackErrorCount; i++) {
		ProfFlameGraphEntry entry = {};
		entry.cName = "[unknown]";
		entry.startTime = 0;
		entry.depth = stack.Length();
		stack.Add(entry);
	}

	for (int i = 0; i < rawEntryCount; i++) {
		if (rawEntries[i].timeStamp >> 63) {
			if (!stack.Length()) {
				continue;
			}

			ProfFlameGraphEntry entry = stack.Last();
			entry.endTime = (double) ((rawEntries[i].timeStamp & 0x7FFFFFFFFFFFFFFFUL) 
					- (rawEntries[0].timeStamp & 0x7FFFFFFFFFFFFFFFUL)) / data->ticksPerMs;

			if (0 == strcmp(entry.cName, "[unknown]")) {
				ProfFunctionEntry *function = report->functions.At(rawEntries[i].thisFunction, false);
				if (function) entry.cName = function->cName;
			}

			entry.thisFunction = rawEntries[i].thisFunction;
			stack.Pop();
			report->entries.Add(entry);
		} else {
			ProfFlameGraphEntry entry = {};
			ProfFunctionEntry *function = report->functions.At(rawEntries[i].thisFunction, false);

			if (function) {
				entry.cName = function->cName;
				entry.colorIndex = function->sourceFileIndex % (sizeof(profEntryColorPalette) / sizeof(profEntryColorPalette[0]));
			}

			entry.startTime = (double) (rawEntries[i].timeStamp 
					- (rawEntries[0].timeStamp & 0x7FFFFFFFFFFFFFFFUL)) / data->ticksPerMs;
			entry.thisFunction = rawEntries[i].thisFunction;
			entry.depth = stack.Length();
			stack.Add(entry);
		}
	}

	for (int i = 0; i < report->entries.Length(); i++) {
		if (report->entries[i].endTime > report->totalTime) {
			report->totalTime = report->entries[i].endTime;
		}
	}

	while (stack.Length()) {
		ProfFlameGraphEntry entry = stack.Last();
		entry.endTime = report->totalTime;
		stack.Pop();
		report->entries.Add(entry);
	}

	if (!report->totalTime) {
		report->totalTime = 1;
	}

	stack.Free();
	report->xEnd = report->totalTime;
	qsort(report->entries.array, report->entries.Length(), sizeof(ProfFlameGraphEntry), ProfFlameGraphEntryCompare);

	int maxDepth = 0;

	for (int i = 0; i < report->entries.Length(); i++) {
		ProfFlameGraphEntryTime time;
		time.start = report->entries[i].startTime;
		time.end = report->entries[i].endTime;
		time.depth = report->entries[i].depth;
		report->entryTimes.Add(time);

		if (report->entries[i].depth > maxDepth) {
			maxDepth = report->entries[i].depth;
		}

		ProfFunctionEntry *function = report->functions.At(report->entries[i].thisFunction, true);
		function->callCount++;
		function->totalTime += report->entries[i].endTime - report->entries[i].startTime;
	}

	printf("Found %ld functions over %d source files.\n", report->functions.used, report->sourceFiles.Length());

	report->vScroll->maximum = (maxDepth + 2) * 30;

	for (uintptr_t i = 0; i < report->functions.capacity; i++) {
		if (report->functions.array[i].key) {
			report->sortedFunctions.Add(report->functions.array[i].value);
		}
	}

	{
		// Create an image of the graph for the zoom bar.

		UIPainter painter = {};
		painter.width = 1200;
		painter.height = maxDepth * 30 + 30;
		painter.clip = UI_RECT_4(0, painter.width, 0, painter.height);
		painter.bits = (uint32_t *) malloc(painter.width * painter.height * 4);
		report->client = report->e.bounds = report->e.clip = painter.clip;
		ProfFlameGraphMessage(&report->e, UI_MSG_PAINT, 0, &painter);
		int newHeight = 30;
		ThumbnailResize(painter.bits, painter.width, painter.height, painter.width, newHeight);
		painter.height = newHeight;
		painter.bits = (uint32_t *) realloc(painter.bits, painter.width * painter.height * 4);
		report->thumbnail = painter.bits;
		report->thumbnailWidth = painter.width;
		report->thumbnailHeight = painter.height;
	}

	table->itemCount = report->sortedFunctions.Length();
	qsort(report->sortedFunctions.array, report->sortedFunctions.Length(), sizeof(ProfFunctionEntry), ProfFunctionCompareTotalTime);
	table->columnHighlight = 1;
	UITableResizeColumns(table);

	free(rawEntries);
}

void ProfStepOverProfiled(void *_window) {
	ProfWindow *window = (ProfWindow *) _window;
	EvaluateCommand("call GfProfilingStart()");
	CommandSendToGDB((void *) "gf-next");
	window->inStepOverProfiled = true;
}

void ProfWindowUpdate(const char *data, UIElement *element) {
	ProfWindow *window = (ProfWindow *) element->cp;

	if (window->inStepOverProfiled) {
		EvaluateCommand("call GfProfilingStop()");
		ProfLoadProfileData(window);
		InterfaceWindowSwitchToAndFocus("Data");
		UIElementRefresh(&dataWindow->e);
		window->inStepOverProfiled = false;
	}
}

UIElement *ProfWindowCreate(UIElement *parent) {
	const int fontSizeFlameGraph = 8;
	ProfWindow *window = (ProfWindow *) calloc(1, sizeof(ProfWindow));
	window->fontFlameGraph = UIFontCreate(_UI_TO_STRING_2(UI_FONT_PATH), fontSizeFlameGraph);
	UIPanel *panel = UIPanelCreate(parent, UI_PANEL_COLOR_1 | UI_PANEL_EXPAND);
	panel->e.cp = window;
	UIButton *button = UIButtonCreate(&panel->e, UI_ELEMENT_V_FILL, "Step over profiled", -1);
	button->e.cp = window;
	button->invoke = ProfStepOverProfiled;

#ifdef UI_FREETYPE
	// Since we will do multithreaded painting with fontFlameGraph, we need to make sure all its glyphs are ready to go.
	for (uintptr_t i = 0; i < sizeof(window->fontFlameGraph->glyphsRendered); i++) {
		UIPainter fakePainter = {};
		UIFont *previousFont = UIFontActivate(window->fontFlameGraph);
		UIDrawGlyph(&fakePainter, 0, 0, i, 0xFF000000);
		UIFontActivate(previousFont);
	}
#endif

	return &panel->e;
}

/////////////////////////////////////////////////////
// Memory window:
/////////////////////////////////////////////////////

// TODO Click a pointer to go to that address.
// TODO Click a function pointer to go the source location.
// TODO Better string visualization.
// TODO Moving between watch window and memory window.
// TODO Set data breakpoints.
// TODO Highlight modified bytes.

struct MemoryWindow {
	UIElement e;
	UIButton *gotoButton;
	Array<int16_t> loadedBytes;
	uint64_t offset;
};

int MemoryWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	MemoryWindow *window = (MemoryWindow *) element;

	if (message == UI_MSG_PAINT) {
		UIPainter *painter = (UIPainter *) dp;
		UIDrawBlock(painter, element->bounds, ui.theme.panel1);

		char buffer[64];
		uint64_t address = window->offset;
		int rowHeight = UIMeasureStringHeight();
		UIRectangle row = UIRectangleAdd(element->bounds, UI_RECT_1I(10));
		int rowCount = (painter->clip.b - row.t) / rowHeight;
		row.b = row.t + rowHeight;

		{
			StringFormat(buffer, sizeof(buffer), "Inspecting memory @%p", (void *) window->offset);
			UIDrawString(painter, row, buffer, -1, ui.theme.codeString, UI_ALIGN_LEFT, 0);
			row.t += rowHeight;
			row.b += rowHeight;
			const char *header = "         0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F   0123456789ABCDEF";
			UIDrawString(painter, row, header, -1, ui.theme.codeComment, UI_ALIGN_LEFT, 0);
			row.t += rowHeight;
			row.b += rowHeight;
		}

		if (rowCount > 0 && rowCount * 16 > window->loadedBytes.Length()) {
			window->loadedBytes.Free();

			for (int i = 0; i < rowCount * 16 / 8; i++) {
				StringFormat(buffer, sizeof(buffer), "x/8xb 0x%lx", window->offset + i * 8);
				EvaluateCommand(buffer);

				bool error = true;

				if (!strstr(evaluateResult, "Cannot access memory")) {
					char *position = strchr(evaluateResult, ':');

					if (position) {
						position++;

						for (int i = 0; i < 8; i++) {
							window->loadedBytes.Add(strtol(position, &position, 0));
						}

						error = false;
					}
				}

				if (error) {
					for (int i = 0; i < 8; i++) {
						window->loadedBytes.Add(-1);
					}
				}
			}
		}

		while (row.t < painter->clip.b) {
			int position = 0;

			StringFormat(buffer, sizeof(buffer), "%.8X ", (uint32_t) (address & 0xFFFFFFFF));
			UIDrawString(painter, row, buffer, -1, ui.theme.codeComment, UI_ALIGN_LEFT, 0);
			UIRectangle r = UIRectangleAdd(row, UI_RECT_4(UIMeasureStringWidth(buffer, -1), 0, 0, 0));
			int glyphWidth = UIMeasureStringWidth("a", 1);

			for (int i = 0; i < 16; i++) {
				if (address + i >= window->offset + window->loadedBytes.Length() || window->loadedBytes[address + i - window->offset] < 0) {
					UIDrawGlyph(painter, r.l + position, r.t, '?', ui.theme.codeOperator); position += glyphWidth;
					UIDrawGlyph(painter, r.l + position, r.t, '?', ui.theme.codeOperator); position += glyphWidth;
				} else {
					const char *hexChars = "0123456789ABCDEF";
					uint8_t byte = window->loadedBytes[address + i - window->offset];
					UIDrawGlyph(painter, r.l + position, r.t, hexChars[(byte & 0xF0) >> 4], ui.theme.codeNumber); position += glyphWidth;
					UIDrawGlyph(painter, r.l + position, r.t, hexChars[(byte & 0x0F) >> 0], ui.theme.codeNumber); position += glyphWidth;

					if (byte >= 0x20 && byte < 0x7F) {
						UIDrawGlyph(painter, r.l + (49 + i) * glyphWidth, r.t, byte, ui.theme.codeString); 
					}
				}

				position += glyphWidth;
			}

			row.t += rowHeight;
			row.b += rowHeight;
			address += 0x10;
		}
	} else if (message == UI_MSG_LAYOUT) {
		UIRectangle bounds = UIRectangleAdd(element->bounds, UI_RECT_1I(10));
		UIElementMove(&window->gotoButton->e, UI_RECT_4(bounds.r - UIElementMessage(&window->gotoButton->e, UI_MSG_GET_WIDTH, 0, 0), bounds.r, 
					bounds.t, bounds.t + UIElementMessage(&window->gotoButton->e, UI_MSG_GET_HEIGHT, 0, 0)), false);
	} else if (message == UI_MSG_MOUSE_WHEEL) {
		window->offset += di / 72 * 0x10;
		window->loadedBytes.Free();
		UIElementRepaint(&window->e, nullptr);
	}

	return 0;
}

void MemoryWindowUpdate(const char *data, UIElement *element) {
	MemoryWindow *window = (MemoryWindow *) element;
	window->loadedBytes.Free();
	UIElementRepaint(element, NULL);
}

void MemoryWindowGotoButtonInvoke(void *cp) {
	MemoryWindow *window = (MemoryWindow *) cp;
	char *expression = nullptr;

	if (0 == strcmp("Goto", UIDialogShow(windowMain, 0, "Enter address expression:\n%t\n%f%b%b", &expression, "Goto", "Cancel"))) {
		char buffer[4096];
		StringFormat(buffer, sizeof(buffer), "py gf_valueof(['%s'],' ')", expression);
		EvaluateCommand(buffer);
		const char *result = evaluateResult;

		if (result && ((*result == '(' && isdigit(result[1])) || isdigit(*result))) {
			if (*result == '(') result++;
			uint64_t address = strtol(result, nullptr, 0);

			if (address) {
				window->loadedBytes.Free();
				window->offset = address & ~0xF;
				UIElementRepaint(&window->e, nullptr);
			} else {
				UIDialogShow(windowMain, 0, "Cannot access memory at address 0.\n%f%b", "OK");
			}
		} else {
			UIDialogShow(windowMain, 0, "Expression did not evaluate to an address.\n%f%b", "OK");
		}
	}

	free(expression);
}

UIElement *MemoryWindowCreate(UIElement *parent) {
	MemoryWindow *window = (MemoryWindow *) UIElementCreate(sizeof(MemoryWindow), parent, 0, MemoryWindowMessage, "memory window");
	window->gotoButton = UIButtonCreate(&window->e, UI_BUTTON_SMALL, "&", -1);
	window->gotoButton->invoke = MemoryWindowGotoButtonInvoke;
	window->gotoButton->e.cp = window;
	return &window->e;
}

/////////////////////////////////////////////////////
// View window:
/////////////////////////////////////////////////////

struct ViewWindowColorSwatch {
	UIElement e;
	uint32_t color;
};

struct ViewWindowMatrixGrid {
	UIElement e;
	UIScrollBar *hScroll, *vScroll;
#define VIEW_WINDOW_MATRIX_GRID_TYPE_CHAR (0)
#define VIEW_WINDOW_MATRIX_GRID_TYPE_FLOAT (1)
#define VIEW_WINDOW_MATRIX_GRID_TYPE_DOUBLE (2)
	int type;
	int w, h;
	char *data;
};

struct ViewWindowString {
	UIElement e;
	UIScrollBar *vScroll;
	char *data;
	int length;
};

int ViewWindowColorSwatchMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_GET_HEIGHT) {
		return UIMeasureStringHeight();
	} else if (message == UI_MSG_PAINT) {
		uint32_t color = ((ViewWindowColorSwatch *) element)->color;
		UIPainter *painter = (UIPainter *) dp;
		const char *message = "Col: ";
		UIDrawString(painter, element->bounds, message, -1, ui.theme.text, UI_ALIGN_LEFT, nullptr);
		UIRectangle swatch = UI_RECT_4(element->bounds.l + UIMeasureStringWidth(message, -1), 0, element->bounds.t + 2, element->bounds.b - 2);
		swatch.r = swatch.l + 50;
		UIDrawRectangle(painter, swatch, color, 0xFF000000, UI_RECT_1(1));
	}

	return 0;
}

double ViewWindowMatrixCalculateDeterminant(double *matrix, int n) {
	if (n == 1) {
		return matrix[0];
	}

	double s = 0;

	for (int i = 0; i < n; i++) {
		double *sub = (double *) malloc(sizeof(double) * (n - 1) * (n - 1));

		for (int j = 0; j < n - 1; j++) {
			for (int k = 0; k < n - 1; k++) {
				sub[j * (n - 1) + k] = matrix[(j + 1) * n + (k < i ? k : k + 1)];
			}
		}

		double x = ViewWindowMatrixCalculateDeterminant(sub, n - 1);
		x *= matrix[0 * n + i];
		if (i & 1) s -= x;
		else s += x;

		free(sub);
	}

	return s;
}

int ViewWindowMatrixGridMessage(UIElement *element, UIMessage message, int di, void *dp) {
	ViewWindowMatrixGrid *grid = (ViewWindowMatrixGrid *) element;

	if (message == UI_MSG_PAINT) {
		// TODO Optimise for really large arrays.
		// TODO Calculate eigenvectors/values.
		
		int glyphWidth = UIMeasureStringWidth("A", 1);
		int glyphHeight = UIMeasureStringHeight();
		UIPainter *painter = (UIPainter *) dp;

		for (int i = 0; i < grid->h; i++) {
			for (int j = 0; j < grid->w; j++) {
				if (grid->type == VIEW_WINDOW_MATRIX_GRID_TYPE_CHAR) {
					char c = grid->data[i * grid->w + j];
					if (!c) continue;
					UIDrawGlyph(painter, element->bounds.l + j * glyphWidth - grid->hScroll->position, 
							element->bounds.t + i * glyphHeight - grid->vScroll->position, c, ui.theme.text);
				} else  if (grid->type == VIEW_WINDOW_MATRIX_GRID_TYPE_FLOAT || grid->type == VIEW_WINDOW_MATRIX_GRID_TYPE_DOUBLE) {
					double f = grid->type == VIEW_WINDOW_MATRIX_GRID_TYPE_DOUBLE ? ((double *) grid->data)[i * grid->w + j] 
						: (double) ((float *) grid->data)[i * grid->w + j];
					char buffer[64];
					StringFormat(buffer, sizeof(buffer), "%f", f);
					UIRectangle rectangle = UI_RECT_4(j * glyphWidth * 14, (j + 1) * glyphWidth * 14, i * glyphHeight, (i + 1) * glyphHeight);
					UIRectangle offset = UI_RECT_2(element->bounds.l - (int) grid->hScroll->position, element->bounds.t - (int) grid->vScroll->position);
					UIDrawString(painter, UIRectangleAdd(rectangle, offset), buffer, -1, ui.theme.text, UI_ALIGN_RIGHT, nullptr);
				}
			}
		}

		int scrollBarSize = UI_SIZE_SCROLL_BAR * element->window->scale;
		UIDrawBlock(painter, UI_RECT_4(element->bounds.r - scrollBarSize, element->bounds.r, element->bounds.b - scrollBarSize, element->bounds.b), ui.theme.panel1);
	} else if (message == UI_MSG_LAYOUT) {
		UIRectangle scrollBarBounds = element->bounds;
		scrollBarBounds.l = scrollBarBounds.r - UI_SIZE_SCROLL_BAR * element->window->scale;
		scrollBarBounds.b -= UI_SIZE_SCROLL_BAR * element->window->scale;
		grid->vScroll->page = UI_RECT_HEIGHT(scrollBarBounds);
		UIElementMove(&grid->vScroll->e, scrollBarBounds, true);
		scrollBarBounds = element->bounds;
		scrollBarBounds.t = scrollBarBounds.b - UI_SIZE_SCROLL_BAR * element->window->scale;
		scrollBarBounds.r -= UI_SIZE_SCROLL_BAR * element->window->scale;
		grid->hScroll->page = UI_RECT_WIDTH(scrollBarBounds);
		UIElementMove(&grid->hScroll->e, scrollBarBounds, true);
	} else if (message == UI_MSG_SCROLLED) {
		UIElementRepaint(element, nullptr);
	} else if (message == UI_MSG_DESTROY) {
		free(grid->data);
	}

	return 0;
}

int ViewWindowStringLayout(ViewWindowString *display, UIPainter *painter, int offset) {
	UIRectangle clientBounds = display->e.bounds;
	clientBounds.r -= UI_SIZE_SCROLL_BAR * display->e.window->scale;
	int x = clientBounds.l, y = clientBounds.t - offset;
	int glyphWidth = UIMeasureStringWidth("a", 1), glyphHeight = UIMeasureStringHeight();

	for (int i = 0; i < display->length; i++) {
		if (x + glyphWidth > clientBounds.r) {
			x = clientBounds.l + glyphWidth;
			y += glyphHeight;
			if (painter) UIDrawGlyph(painter, clientBounds.l, y, '>', ui.theme.codeComment);
		}

		if (display->data[i] < 0x20 || display->data[i] >= 0x7F) {
			if (display->data[i] == '\n') {
				if (painter) UIDrawGlyph(painter, x, y, '\\', ui.theme.codeComment);
				x += glyphWidth;
				if (painter) UIDrawGlyph(painter, x, y, 'n', ui.theme.codeComment);
				x = clientBounds.l;
				y += glyphHeight;
			} else if (display->data[i] == '\t') {
				if (painter) UIDrawGlyph(painter, x, y, '\\', ui.theme.codeNumber);
				x += glyphWidth;
				if (painter) UIDrawGlyph(painter, x, y, 't', ui.theme.codeNumber);
				x += glyphWidth;
			} else {
				const char *hexChars = "0123456789ABCDEF";
				if (painter) UIDrawGlyph(painter, x, y, '<', ui.theme.codeNumber);
				x += glyphWidth;
				if (painter) UIDrawGlyph(painter, x, y, hexChars[(display->data[i] & 0xF0) >> 4], ui.theme.codeNumber);
				x += glyphWidth;
				if (painter) UIDrawGlyph(painter, x, y, hexChars[(display->data[i] & 0x0F) >> 0], ui.theme.codeNumber);
				x += glyphWidth;
				if (painter) UIDrawGlyph(painter, x, y, '>', ui.theme.codeNumber);
				x += glyphWidth;
			}
		} else {
			if (painter) UIDrawGlyph(painter, x, y, display->data[i], ui.theme.codeDefault);
			x += glyphWidth;
		}
	}

	return y - clientBounds.t + glyphHeight;
}

int ViewWindowStringMessage(UIElement *element, UIMessage message, int di, void *dp) {
	ViewWindowString *display = (ViewWindowString *) element;

	if (message == UI_MSG_DESTROY) {
		free(display->data);
	} else if (message == UI_MSG_LAYOUT) {
		UIRectangle scrollBarBounds = element->bounds;
		scrollBarBounds.l = scrollBarBounds.r - UI_SIZE_SCROLL_BAR * element->window->scale;
		UIRectangle clientBounds = element->bounds;
		clientBounds.r -= UI_SIZE_SCROLL_BAR * element->window->scale;
		display->vScroll->maximum = ViewWindowStringLayout(display, nullptr, 0);
		display->vScroll->page = UI_RECT_HEIGHT(element->bounds);
		UIElementMove(&display->vScroll->e, scrollBarBounds, true);
	} else if (message == UI_MSG_PAINT) {
		UIDrawBlock((UIPainter *) dp, element->bounds, ui.theme.codeBackground);
		ViewWindowStringLayout(display, (UIPainter *) dp, display->vScroll->position);
	} else if (message == UI_MSG_MOUSE_WHEEL) {
		return UIElementMessage(&display->vScroll->e, message, di, dp);
	} else if (message == UI_MSG_SCROLLED) {
		UIElementRepaint(element, nullptr);
	}

	return 0;
}

void ViewWindowView(void *cp) {
	// Get the selected watch expression.
	UIElement *watchElement = InterfaceWindowSwitchToAndFocus("Watch");
	if (!watchElement) return;
	WatchWindow *w = (WatchWindow *) watchElement->cp;
	if (w->textbox) return;
	if (w->selectedRow < 0 || w->selectedRow > w->rows.Length() || !w->rows.Length()) return;
	Watch *watch = w->rows[w->selectedRow == w->rows.Length() ? w->selectedRow - 1 : w->selectedRow];
	if (!watch) return;
	if (!cp) cp = InterfaceWindowSwitchToAndFocus("View");
	if (!cp) return;

	// Destroy the previous panel contents.
	UIElement *panel = (UIElement *) cp;
	UIElementDestroyDescendents(panel);
	UIButton *button = UIButtonCreate(panel, 0, "View (Ctrl+Shift+V)", -1);
	button->e.cp = panel;
	button->invoke = ViewWindowView;

	// Get information about the watch expression.
	char *end, type[256], buffer[256];
	char oldFormat = watch->format;
	watch->format = 0;
	WatchEvaluate("gf_typeof", watch);
	end = strchr(evaluateResult, '\n');
	if (end) *end = 0;
	StringFormat(type, sizeof(type), "%s", evaluateResult);
	StringFormat(buffer, sizeof(buffer), "Type: %s", type);
	UILabelCreate(panel, 0, buffer, -1);
	WatchEvaluate("gf_valueof", watch);
	end = strchr(evaluateResult, '\n');
	if (end) *end = 0;
	watch->format = oldFormat;
	// printf("valueof: {%s}\n", evaluateResult);

	// Create the specific display for the given type.
	if (0 == strcmp(type, "uint8_t") || 0 == strcmp(type, "uint16_t") || 0 == strcmp(type, "uint32_t") || 0 == strcmp(type, "uint64_t")
			|| 0 == strcmp(type, "int8_t") || 0 == strcmp(type, "int16_t") || 0 == strcmp(type, "int32_t") || 0 == strcmp(type, "int64_t")
			|| 0 == strcmp(type, "unsigned char") || 0 == strcmp(type, "unsigned short") || 0 == strcmp(type, "unsigned int") 
			|| 0 == strcmp(type, "unsigned") || 0 == strcmp(type, "unsigned long") || 0 == strcmp(type, "unsigned long long")
			|| 0 == strcmp(type, "char") || 0 == strcmp(type, "short") || 0 == strcmp(type, "int") 
			|| 0 == strcmp(type, "long") || 0 == strcmp(type, "long long")) {
		uint64_t value;

		if (evaluateResult[0] == '-') {
			value = -strtoul(evaluateResult + 1, nullptr, 10);
		} else {
			value = strtoul(evaluateResult, nullptr, 10);
		}

		StringFormat(buffer, sizeof(buffer), " 8b: %d %u 0x%x '%c'", (int8_t) value, (uint8_t) value, (uint8_t) value, (char) value);
		UILabelCreate(panel, 0, buffer, -1);
		StringFormat(buffer, sizeof(buffer), "16b: %d %u 0x%x", (int16_t) value, (uint16_t) value, (uint16_t) value);
		UILabelCreate(panel, 0, buffer, -1);
		StringFormat(buffer, sizeof(buffer), "32b: %d %u 0x%x", (int32_t) value, (uint32_t) value, (uint32_t) value);
		UILabelCreate(panel, 0, buffer, -1);
		StringFormat(buffer, sizeof(buffer), "64b: %ld %lu 0x%lx", (int64_t) value, (uint64_t) value, (uint64_t) value);
		UILabelCreate(panel, 0, buffer, -1);

		int p = StringFormat(buffer, sizeof(buffer), "Bin: ");

		for (int64_t i = 63; i >= 32; i--) {
			buffer[p++] = (value & ((uint64_t) 1 << i)) ? '1' : '0';
			if ((i & 7) == 0) buffer[p++] = ' ';
		}
		
		UILabelCreate(panel, 0, buffer, p);

		p = StringFormat(buffer, sizeof(buffer), "     ");

		for (int64_t i = 31; i >= 0; i--) {
			buffer[p++] = (value & ((uint64_t) 1 << i)) ? '1' : '0';
			if ((i & 7) == 0) buffer[p++] = ' ';
		}
		
		UILabelCreate(panel, 0, buffer, p);

		if (value <= 0xFFFFFFFF) {
			((ViewWindowColorSwatch *) UIElementCreate(sizeof(ViewWindowColorSwatch), panel, 0, 
				ViewWindowColorSwatchMessage, "Color swatch"))->color = (uint32_t) value;
		}
	} else if ((0 == memcmp(type, "char [", 6) && !strstr(type, "][")) 
			|| 0 == strcmp(type, "const char *") 
			|| 0 == strcmp(type, "char *")) {
		printf("string '%s'\n", evaluateResult);
		char address[64];

		if (evaluateResult[0] != '(') {
			WatchEvaluate("gf_addressof", watch);
			printf("addressof '%s'\n", evaluateResult);
			char *end = strchr(evaluateResult, ' ');
			if (end) *end = 0;
			end = strchr(evaluateResult, '\n');
			if (end) *end = 0;
			StringFormat(address, sizeof(address), "%s", evaluateResult);
		} else {
			char *end = strchr(evaluateResult + 1, ' ');
			if (!end) goto unrecognised;
			*end = 0;
			StringFormat(address, sizeof(address), "%s", evaluateResult + 1);
		}

		char tempPath[PATH_MAX];
		realpath(".temp.gf", tempPath);
		char buffer[PATH_MAX * 2];
		StringFormat(buffer, sizeof(buffer), "(size_t)strlen((const char *)(%s))", address);
		EvaluateExpression(buffer);
		printf("'%s' -> '%s'\n", buffer, evaluateResult);
		const char *lengthString = evaluateResult ? strstr(evaluateResult, "= ") : nullptr;
		size_t length = lengthString ? atoi(lengthString + 2) : 0;
		// TODO Preventing errors when calling strlen from crashing the target?

		if (!length) {
			goto unrecognised;
		}

		char *data = (char *) malloc(length + 1);

		if (!data) {
			goto unrecognised;
		}

		StringFormat(buffer, sizeof(buffer), "dump binary memory %s (%s) (%s+%d)", tempPath, address, address, length);
		EvaluateCommand(buffer);
		printf("'%s' -> '%s'\n", buffer, evaluateResult);
		FILE *f = fopen(tempPath, "rb");

		if (f) {
			fread(data, 1, length, f);
			fclose(f);
			unlink(tempPath);
			data[length] = 0;
			// printf("got '%s'\n", data);
			ViewWindowString *display = (ViewWindowString *) UIElementCreate(sizeof(ViewWindowString), panel, 
					UI_ELEMENT_H_FILL | UI_ELEMENT_V_FILL, ViewWindowStringMessage, "String display");
			display->vScroll = UIScrollBarCreate(&display->e, 0);
			display->data = data;
			display->length = length;
			StringFormat(buffer, sizeof(buffer), "%d+1 bytes", length);
			UILabelCreate(panel, UI_ELEMENT_H_FILL, buffer, -1);
		} else {
			free(data);
			goto unrecognised;
		}
	} else if (0 == memcmp(type, "char [", 6) 
			|| 0 == memcmp(type, "float [", 7) 
			|| 0 == memcmp(type, "double [", 8)) {
		int typeID = type[0] == 'c' ? VIEW_WINDOW_MATRIX_GRID_TYPE_CHAR 
			: type[0] == 'f' ? VIEW_WINDOW_MATRIX_GRID_TYPE_FLOAT : VIEW_WINDOW_MATRIX_GRID_TYPE_DOUBLE;
		int itemSize = type[0] == 'c' ? sizeof(char) : type[0] == 'f' ? sizeof(float) : sizeof(double);
		int itemCharacters = type[0] == 'c' ? 1 : 14;

		char *p = strchr(type, '[') + 1;
		int w = strtol(p, &p, 0);
		if (memcmp(p, "][", 2)) goto unrecognised;
		p += 2;
		int h = strtol(p, &p, 0);
		if (strcmp(p, "]")) goto unrecognised;
		if (w <= 1 || h <= 1) goto unrecognised;
		if (!WatchGetAddress(watch)) goto unrecognised;

		ViewWindowMatrixGrid *grid = (ViewWindowMatrixGrid *) UIElementCreate(sizeof(ViewWindowMatrixGrid), panel, 
				UI_ELEMENT_H_FILL | UI_ELEMENT_V_FILL, ViewWindowMatrixGridMessage, "Matrix grid");
		grid->hScroll = UIScrollBarCreate(&grid->e, UI_SCROLL_BAR_HORIZONTAL);
		grid->vScroll = UIScrollBarCreate(&grid->e, 0);
		grid->hScroll->maximum = w * UIMeasureStringWidth("A", 1) * itemCharacters;
		grid->vScroll->maximum = h * UIMeasureStringHeight();
		grid->w = w, grid->h = h;
		grid->data = (char *) malloc(w * h * itemSize);
		grid->type = typeID;

		char tempPath[PATH_MAX];
		realpath(".temp.gf", tempPath);
		char buffer[PATH_MAX * 2];
		StringFormat(buffer, sizeof(buffer), "dump binary memory %s (%s) (%s+%d)", tempPath, evaluateResult, evaluateResult, w * h * itemSize);
		EvaluateCommand(buffer);
		FILE *f = fopen(tempPath, "rb");

		if (f) {
			fread(grid->data, 1, w * h * itemSize, f);
			fclose(f);
			unlink(tempPath);
		}

		if ((typeID == VIEW_WINDOW_MATRIX_GRID_TYPE_FLOAT || typeID == VIEW_WINDOW_MATRIX_GRID_TYPE_DOUBLE) && w == h && w <= 4 && w >= 2) {
			double matrix[16];

			for (int i = 0; i < w; i++) {
				for (int j = 0; j < w; j++) {
					if (typeID == VIEW_WINDOW_MATRIX_GRID_TYPE_FLOAT) {
						matrix[i * w + j] = ((float *) grid->data)[i * w + j];
					} else {
						matrix[i * w + j] = ((double *) grid->data)[i * w + j];
					}
				}
			}

			double determinant = ViewWindowMatrixCalculateDeterminant(matrix, w);
			StringFormat(buffer, sizeof(buffer), "Determinant: %f", determinant);
			UILabelCreate(panel, 0, buffer, -1);
		}
	} else {
		unrecognised:;
		// TODO Custom view.
		// TODO Table view for array of structures.
		UILabelCreate(panel, 0, "No view available for type.", -1);
	}

	// Relayout the panel.
	UIElementRefresh(panel);
}

void ViewWindowUpdate(const char *data, UIElement *element) {
}

UIElement *ViewWindowCreate(UIElement *parent) {
	UIPanel *panel = UIPanelCreate(parent, UI_PANEL_EXPAND | UI_PANEL_COLOR_1);
	UIButton *button = UIButtonCreate(&panel->e, 0, "View (Ctrl+Shift+V)", -1);
	button->e.cp = panel;
	button->invoke = ViewWindowView;
	UILabelCreate(&panel->e, 0, "Select a watch expression, then click View.", -1);
	return &panel->e;
}

//////////////////////////////////////////////////////
// Waveform display:
//////////////////////////////////////////////////////

struct WaveformDisplay {
	UIElement e;
	float *samples;
	size_t sampleCount, channels;
	int samplesOnScreen, minimumZoom;
	UIScrollBar *scrollBar;
	UIButton *zoomOut, *zoomIn, *normalize;
	int dragLastX, dragLastModification;
	float peak;
};

void WaveformDisplayDrawVerticalLineWithTranslucency(UIPainter *painter, UIRectangle rectangle, uint32_t color, uint32_t alpha) {
	rectangle = UIRectangleIntersection(painter->clip, rectangle);
	if (!UI_RECT_VALID(rectangle)) return;
	uint32_t *bits = painter->bits + rectangle.t * painter->width + rectangle.l;

	for (int y = 0; y < rectangle.b - rectangle.t; y++) {
		uint32_t *destination = &bits[y * painter->width];
		uint32_t m1 = alpha, m2 = 255 - m1;
		uint32_t original = *destination;
		uint32_t r2 = m2 * (original & 0x00FF00FF);
		uint32_t g2 = m2 * (original & 0x0000FF00);
		uint32_t r1 = m1 * (color & 0x00FF00FF);
		uint32_t g1 = m1 * (color & 0x0000FF00);
		uint32_t result = 0xFF000000 | (0x0000FF00 & ((g1 + g2) >> 8)) | (0x00FF00FF & ((r1 + r2) >> 8));
		*destination = result;
	}
}

int WaveformDisplayMessage(UIElement *element, UIMessage message, int di, void *dp) {
	WaveformDisplay *display = (WaveformDisplay *) element;

	if (display->sampleCount == 0 && message != UI_MSG_DESTROY) {
		return 0;
	}

	if (message == UI_MSG_DESTROY) {
		free(display->samples);
		display->samples = nullptr;
	} else if (message == UI_MSG_LAYOUT) {
		if (display->samplesOnScreen > (int) display->sampleCount) {
			display->samplesOnScreen = display->sampleCount;
		}

		int scrollBarHeight = UI_SIZE_SCROLL_BAR * element->window->scale;
		UIRectangle scrollBarBounds = element->bounds;
		scrollBarBounds.t = scrollBarBounds.b - scrollBarHeight;
		display->scrollBar->maximum = display->sampleCount;
		display->scrollBar->page = display->samplesOnScreen;
		UIElementMove(&display->scrollBar->e, scrollBarBounds, true);

		UIElementMove(&display->zoomOut->e, UI_RECT_4(element->bounds.l + (int) (15 * element->window->scale), 
					element->bounds.l + (int) (45 * element->window->scale), 
					element->bounds.t + (int) (15 * element->window->scale), 
					element->bounds.t + (int) (45 * element->window->scale)), true);
		UIElementMove(&display->zoomIn->e, UI_RECT_4(element->bounds.l + (int) (45 * element->window->scale), 
					element->bounds.l + (int) (75 * element->window->scale), 
					element->bounds.t + (int) (15 * element->window->scale), 
					element->bounds.t + (int) (45 * element->window->scale)), true);
		UIElementMove(&display->normalize->e, UI_RECT_4(element->bounds.l + (int) (75 * element->window->scale), 
					element->bounds.l + (int) (135 * element->window->scale), 
					element->bounds.t + (int) (15 * element->window->scale), 
					element->bounds.t + (int) (45 * element->window->scale)), true);
	} else if (message == UI_MSG_MOUSE_DRAG && element->window->pressedButton == 1) {
		display->scrollBar->position += display->dragLastModification;
		display->dragLastModification = (element->window->cursorX - display->dragLastX) 
						* display->samplesOnScreen / UI_RECT_WIDTH(element->bounds);
		display->scrollBar->position -= display->dragLastModification;
		UIElementRefresh(&display->e);
	} else if (message == UI_MSG_MOUSE_DRAG && element->window->pressedButton == 2) {
		UIElementRepaint(&display->e, NULL);
	} else if (message == UI_MSG_MOUSE_MOVE) {
		UIElementRepaint(&display->e, NULL);
	} else if (message == UI_MSG_MIDDLE_UP) {
		int l = element->window->cursorX - element->bounds.l, r = display->dragLastX - element->bounds.l;
		if (r < l) { int t = l; l = r; r = t; }
		float lf = (float) l / UI_RECT_WIDTH(element->bounds) * display->samplesOnScreen + display->scrollBar->position;
		float rf = (float) r / UI_RECT_WIDTH(element->bounds) * display->samplesOnScreen + display->scrollBar->position;

		if (rf - lf >= display->minimumZoom) {
			display->scrollBar->position = lf;
			display->samplesOnScreen = rf - lf;
		}

		UIElementRefresh(&display->e);
	} else if (message == UI_MSG_LEFT_DOWN || message == UI_MSG_MIDDLE_DOWN) {
		display->dragLastX = element->window->cursorX;
		display->dragLastModification = 0;
	} else if (message == UI_MSG_MOUSE_WHEEL) {
		int divisions = di / 72;
		double factor = 1;
		double perDivision = 1.2f;
		while (divisions > 0) factor *= perDivision, divisions--;
		while (divisions < 0) factor /= perDivision, divisions++;
		double mouse = (double) (element->window->cursorX - element->bounds.l) / UI_RECT_WIDTH(element->bounds);
		double newZoom = (double) display->samplesOnScreen / display->sampleCount * factor;

		if (newZoom * display->sampleCount >= display->minimumZoom) {
			display->scrollBar->position += mouse * display->samplesOnScreen * (1 - factor);
			display->samplesOnScreen = newZoom * display->sampleCount;
		}

		UIElementRefresh(&display->e);
	} else if (message == UI_MSG_SCROLLED) {
		UIElementRepaint(element, NULL);
	} else if (message == UI_MSG_PAINT) {
		UIRectangle client = element->bounds;
		client.b -= UI_RECT_HEIGHT(display->scrollBar->e.bounds);

		UIPainter *painter = (UIPainter *) dp;
		UIRectangle oldClip = painter->clip;
		painter->clip = UIRectangleIntersection(client, painter->clip);
		int ym = (client.t + client.b) / 2;
		int h2 = (client.b - client.t) / 2;
		int yp = ym;
		UIDrawBlock(painter, painter->clip, ui.theme.panel1);
		UIDrawBlock(painter, UI_RECT_4(client.l, client.r, ym, ym + 1), 0x707070);

		float yScale = (display->normalize->e.flags & UI_BUTTON_CHECKED) && display->peak > 0.00001f ? 1.0f / display->peak : 1.0f;

		int sampleOffset = (int) display->scrollBar->position;
		float *samples = &display->samples[display->channels * sampleOffset];
		int sampleCount = display->samplesOnScreen;
		UI_ASSERT(sampleOffset + sampleCount <= (int) display->sampleCount);

		if (sampleCount > UI_RECT_WIDTH(client)) {
			uint32_t alpha = 255 - 80 * (display->channels - 1);

			for (size_t channel = 0; channel < display->channels; channel++) {
				for (int32_t x = painter->clip.l; x < painter->clip.r; x++) {
					int32_t x0 = x - client.l;
					float xf0 = (float) x0 / (client.r - client.l);
					float xf1 = (float) (x0 + 1) / (client.r - client.l);
					float yf = 0.0f, yt = 0.0f;
					int i0 = xf0 * sampleCount;
					int i1 = xf1 * sampleCount;
					int is = 1 + (i1 - i0) / 1000; // Skip samples if we're zoomed really far out.

					for (int k = i0; k < i1; k += is) {
						float t = samples[channel + display->channels * (int) k];
						if (t < 0 && yt < -t) yt = -t;
						else if (t > 0 && yf <  t) yf = t;
					}

					UIRectangle r = UI_RECT_4(x, x + 1, ym - (int) (yt * h2 * yScale), ym + (int) (yf * h2 * yScale));
					WaveformDisplayDrawVerticalLineWithTranslucency(painter, r, ui.theme.text, alpha);
				}
			}
		} else {
			for (size_t channel = 0; channel < display->channels; channel++) {
				yp = ym + h2 * yScale * samples[channel + 0];

				for (int32_t i = 0; i < sampleCount; i++) {
					int32_t x0 = (int) ((float) i / sampleCount * UI_RECT_WIDTH(client)) + client.l;
					int32_t x1 = (int) ((float) (i + 1) / sampleCount * UI_RECT_WIDTH(client)) + client.l;
					int32_t y = ym + h2 * yScale * samples[channel + display->channels * (int) i];
					UIDrawLine(painter, x0, yp, x1, y, ui.theme.text);
					yp = y;
				}
			}

			if (sampleCount < UI_RECT_WIDTH(client) / 4) {
				for (size_t channel = 0; channel < display->channels; channel++) {
					for (int32_t i = 0; i < sampleCount; i++) {
						int32_t x1 = (int) ((float) (i + 1) / sampleCount * UI_RECT_WIDTH(client)) + client.l;
						int32_t y = ym + h2 * yScale * samples[channel + display->channels * (int) i];
						UIDrawBlock(painter, UI_RECT_4(x1 - 2, x1 + 2, y - 2, y + 2), 
								channel % 2 ? 0xFFFF00FF : 0xFF00FFFF);
					}
				}
			}

			int mouseXSample = (float) (element->window->cursorX - client.l) / UI_RECT_WIDTH(element->bounds) 
				* display->samplesOnScreen - 0.5f;

			if (mouseXSample >= 0 && mouseXSample < sampleCount
					&& UIRectangleContains(element->clip, element->window->cursorX, element->window->cursorY)
					&& !UIRectangleContains(display->scrollBar->e.clip, element->window->cursorX, element->window->cursorY)) {
				int stringOffset = 20 * element->window->scale;
				UIRectangle stringRectangle = UI_RECT_4(client.l + stringOffset, client.r - stringOffset, 
						client.t + stringOffset, client.t + stringOffset + UIMeasureStringHeight());
				char buffer[100];
				snprintf(buffer, sizeof(buffer), "%d: ", (int) (mouseXSample + display->scrollBar->position));

				for (size_t channel = 0; channel < display->channels; channel++) {
					char buffer2[30];
					float sample = samples[channel + display->channels * mouseXSample];
					snprintf(buffer2, sizeof(buffer2), "%s%s%.5f", channel ? ", " : "", sample >= 0.0f ? "+" : "", sample);
					if (strlen(buffer) + strlen(buffer2) > sizeof(buffer) - 1) break;
					strcat(buffer, buffer2); 
				}

				UIDrawString(painter, stringRectangle, buffer, -1, ui.theme.text, UI_ALIGN_RIGHT, NULL);

				int32_t x1 = (int) ((float) (mouseXSample + 1) / sampleCount * UI_RECT_WIDTH(client)) + client.l;
				WaveformDisplayDrawVerticalLineWithTranslucency(painter, UI_RECT_4(x1, x1 + 1, client.t, client.b), 0xFFFFFF, 100);
			}
		}

		if (element->window->pressedButton == 2 && element->window->pressed) {
			int l = element->window->cursorX, r = display->dragLastX;
			UIDrawInvert(painter, UI_RECT_4(l > r ? r : l, l > r ? l : r, element->bounds.t, element->bounds.r));
		}

		painter->clip = oldClip;
	}

	return 0;
}

int WaveformDisplayZoomButtonMessage(UIElement *element, UIMessage message, int di, void *dp) {
	WaveformDisplay *display = (WaveformDisplay *) element->parent;

	if (message == UI_MSG_CLICKED) {
		if (element == &display->zoomOut->e) {
			display->scrollBar->position -= display->samplesOnScreen / 2;
			display->samplesOnScreen *= 2;
		} else if (element == &display->zoomIn->e && display->samplesOnScreen >= display->minimumZoom) {
			display->samplesOnScreen /= 2;
			display->scrollBar->position += display->samplesOnScreen / 2;
		}

		UIElementRefresh(&display->e);
	}

	return 0;
}

int WaveformDisplayNormalizeButtonMessage(UIElement *element, UIMessage message, int di, void *dp) {
	WaveformDisplay *display = (WaveformDisplay *) element->parent;

	if (message == UI_MSG_CLICKED) {
		element->flags ^= UI_BUTTON_CHECKED;
		UIElementRefresh(&display->e);
	}

	return 0;
}

WaveformDisplay *WaveformDisplayCreate(UIElement *parent, uint32_t flags) {
	WaveformDisplay *display = (WaveformDisplay *) UIElementCreate(sizeof(WaveformDisplay), 
			parent, flags, WaveformDisplayMessage, "WaveformDisplay");
	display->scrollBar = UIScrollBarCreate(&display->e, UI_ELEMENT_NON_CLIENT | UI_SCROLL_BAR_HORIZONTAL);
	display->zoomOut = UIButtonCreate(&display->e, UI_BUTTON_SMALL, "-", -1);
	display->zoomIn = UIButtonCreate(&display->e, UI_BUTTON_SMALL, "+", -1);
	display->normalize = UIButtonCreate(&display->e, UI_BUTTON_SMALL, "Norm", -1);
	display->zoomOut->e.messageUser = display->zoomIn->e.messageUser = WaveformDisplayZoomButtonMessage;
	display->normalize->e.messageUser = WaveformDisplayNormalizeButtonMessage;
	return display;
}

void WaveformDisplaySetContent(WaveformDisplay *display, float *samples, size_t sampleCount, size_t channels) {
	UI_ASSERT(channels >= 1);
	free(display->samples);
	display->samples = (float *) malloc(sizeof(float) * sampleCount * channels);
	memcpy(display->samples, samples, sizeof(float) * sampleCount * channels);
	display->sampleCount = sampleCount;
	display->channels = channels;
	display->samplesOnScreen = sampleCount;
	display->minimumZoom = 40 > sampleCount ? sampleCount : 40;

	float peak = 0.0f;

	for (uintptr_t i = 0; i < sampleCount * channels; i++) {
		if (samples[i] > peak) {
			peak = samples[i];
		} else if (-samples[i] > peak) {
			peak = -samples[i];
		}
	}

	display->peak = peak;
}

//////////////////////////////////////////////////////
// Waveform viewer:
//////////////////////////////////////////////////////

struct WaveformViewer {
	char pointer[256];
	char sampleCount[256];
	char channels[256];
	int parsedSampleCount, parsedChannels;
	UIButton *autoToggle;
	UIPanel *labelPanel;
	UILabel *label;
	WaveformDisplay *display;
};

void WaveformViewerUpdate(const char *pointerString, const char *sampleCountString, const char *channelsString, UIElement *owner);

const char *WaveformViewerGetSamples(const char *pointerString, const char *sampleCountString, const char *channelsString,
		float **_samples, int *_sampleCount, int *_channels) {
	const char *sampleCountResult = EvaluateExpression(sampleCountString);
	if (!sampleCountResult) { return "Could not evaluate sample count."; }
	int sampleCount = atoi(sampleCountResult + 1);
	const char *channelsResult = EvaluateExpression(channelsString);
	if (!channelsResult) { return "Could not evaluate channels."; }
	int channels = atoi(channelsResult + 1);
	if (channels < 1 || channels > 8) { return "Channels must be between 1 and 8."; }
	const char *pointerResult = EvaluateExpression(pointerString, "/x");
	if (!pointerResult) { return "Could not evaluate pointer."; }
	char _pointerResult[1024];
	StringFormat(_pointerResult, sizeof(_pointerResult), "%s", pointerResult);
	pointerResult = strstr(_pointerResult, " 0x");
	if (!pointerResult) { return "Pointer to sample data does not look like an address!"; }
	pointerResult++;

	size_t byteCount = sampleCount * channels * 4;
	float *samples = (float *) malloc(byteCount);

	char transferPath[PATH_MAX];
	realpath(".transfer.gf", transferPath);

	char buffer[PATH_MAX * 2];
	StringFormat(buffer, sizeof(buffer), "dump binary memory %s (%s) (%s+%d)", transferPath, pointerResult, pointerResult, byteCount);
	EvaluateCommand(buffer);

	FILE *f = fopen(transferPath, "rb");

	if (f) {
		fread(samples, 1, byteCount, f);
		fclose(f);
		unlink(transferPath);
	}

	if (!f || strstr(evaluateResult, "access")) {
		return "Could not read the waveform samples!";
	}

	*_samples = samples, *_sampleCount = sampleCount, *_channels = channels;
	return nullptr;
}

int WaveformViewerWindowMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_DESTROY) {
		DataViewerRemoveFromAutoUpdateList(element);
		free(element->cp);
	} else if (message == UI_MSG_GET_WIDTH) {
		return 300;
	} else if (message == UI_MSG_GET_HEIGHT) {
		return 300;
	}
	
	return 0;
}

void WaveformViewerAutoUpdateCallback(UIElement *element) {
	WaveformViewer *viewer = (WaveformViewer *) element->cp;
	WaveformViewerUpdate(viewer->pointer, viewer->sampleCount, viewer->channels, element);
}

int WaveformViewerRefreshMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_CLICKED) {
		WaveformViewerAutoUpdateCallback(element->parent);
	}

	return 0;
}

void WaveformViewerSaveToFile(void *cp) {
	static char *path = NULL;
	const char *result = UIDialogShow(windowMain, 0, "Save to file       \nPath:\n%t\n%f%b%b%b", &path, "Save", "Save and open", "Cancel");
	if (0 == strcmp(result, "Cancel")) return;
	WaveformDisplay *display = (WaveformDisplay *) cp;
	FILE *f = fopen(path, "wb");
	if (!f) { UIDialogShow(windowMain, 0, "Unable to open file for writing.\n%f%b", "OK"); return; }
	int32_t i;
	int16_t s;
	fwrite("RIFF", 1, 4, f);
	i = 36 + display->channels * 4 * display->sampleCount; fwrite(&i, 1, 4, f); // Total file size, minus 8.
	fwrite("WAVE", 1, 4, f);
	fwrite("fmt ", 1, 4, f);
	i = 16; fwrite(&i, sizeof(i), 1, f); // Format chunk size.
	s = 3; fwrite(&s, sizeof(s), 1, f); // Format tag (float).
	s = display->channels; fwrite(&s, sizeof(s), 1, f); // Channels.
	i = 48000; fwrite(&i, sizeof(i), 1, f); // Sample rate (guessed).
	i = 48000 * display->channels * sizeof(float); fwrite(&i, sizeof(i), 1, f); // Average bytes per second.
	s = display->channels * sizeof(float); fwrite(&s, sizeof(s), 1, f); // Block align.
	s = sizeof(float) * 8; fwrite(&s, sizeof(s), 1, f); // Bits per sample.
	fwrite("data", 1, 4, f);
	i = display->channels * sizeof(float) * display->sampleCount; fwrite(&i, sizeof(i), 1, f); // Bytes of sample data.
	fwrite(display->samples, display->channels * 4, display->sampleCount, f); // Sample data.
	fclose(f);

	if (0 == strcmp(result, "Save and open")) {
		char buffer[4000];
		snprintf(buffer, sizeof(buffer), "xdg-open \"%s\"", path);
		system(buffer);
	}
}

int WaveformViewerDisplayMessage(UIElement *element, UIMessage message, int di, void *dp) {
	if (message == UI_MSG_RIGHT_UP) {
		UIMenu *menu = UIMenuCreate(&element->window->e, UI_MENU_NO_SCROLL);
		UIMenuAddItem(menu, 0, "Save to .wav...", -1, [] (void *cp) { WaveformViewerSaveToFile(cp); }, element);
		UIMenuShow(menu);
	}

	return 0;
}

void WaveformViewerUpdate(const char *pointerString, const char *sampleCountString, const char *channelsString, UIElement *owner) {
	float *samples = nullptr;
	int sampleCount = 0, channels = 0;
	const char *error = WaveformViewerGetSamples(pointerString, sampleCountString, channelsString, 
			&samples, &sampleCount, &channels);

	if (!owner) {
		WaveformViewer *viewer = (WaveformViewer *) calloc(1, sizeof(WaveformViewer));
		if (pointerString) StringFormat(viewer->pointer, sizeof(viewer->pointer), "%s", pointerString);
		if (sampleCountString) StringFormat(viewer->sampleCount, sizeof(viewer->sampleCount), "%s", sampleCountString);
		if (channelsString) StringFormat(viewer->channels, sizeof(viewer->channels), "%s", channelsString);

		UIMDIChild *window = UIMDIChildCreate(&dataWindow->e, UI_MDI_CHILD_CLOSE_BUTTON, UI_RECT_1(0), "Waveform", -1);
		window->e.messageUser = WaveformViewerWindowMessage;
		window->e.cp = viewer;
		viewer->autoToggle = UIButtonCreate(&window->e, UI_BUTTON_SMALL | UI_ELEMENT_NON_CLIENT, "Auto", -1);
		viewer->autoToggle->e.cp = (void *) WaveformViewerAutoUpdateCallback;
		viewer->autoToggle->e.messageUser = DataViewerAutoUpdateButtonMessage;
		UIButtonCreate(&window->e, UI_BUTTON_SMALL | UI_ELEMENT_NON_CLIENT, "Refresh", -1)->e.messageUser = WaveformViewerRefreshMessage;
		owner = &window->e;

		UIPanel *panel = UIPanelCreate(owner, UI_PANEL_EXPAND);
		viewer->labelPanel = UIPanelCreate(&panel->e, UI_PANEL_COLOR_1 | UI_ELEMENT_V_FILL);
		viewer->label = UILabelCreate(&viewer->labelPanel->e, UI_ELEMENT_H_FILL, nullptr, 0);
		viewer->display = WaveformDisplayCreate(&panel->e, UI_ELEMENT_V_FILL);
		viewer->display->e.messageUser = WaveformViewerDisplayMessage;
	}

	WaveformViewer *viewer = (WaveformViewer *) owner->cp;
	viewer->parsedSampleCount = sampleCount, viewer->parsedChannels = channels;

	if (error) {
		UILabelSetContent(viewer->label, error, -1);
		viewer->labelPanel->e.flags &= ~UI_ELEMENT_HIDE;
		viewer->display->e.flags |= UI_ELEMENT_HIDE;
	} else {
		viewer->labelPanel->e.flags |= UI_ELEMENT_HIDE;
		viewer->display->e.flags &= ~UI_ELEMENT_HIDE;
		WaveformDisplaySetContent(viewer->display, samples, sampleCount, channels);
	}

	UIElementRefresh(&viewer->display->e);
	UIElementRefresh(&viewer->label->e);
	UIElementRefresh(viewer->labelPanel->e.parent);
	UIElementRefresh(owner);
	UIElementRefresh(&dataWindow->e);

	free(samples);
}

void WaveformAddDialog(void *) {
	static char *pointer = nullptr, *sampleCount = nullptr, *channels = nullptr;

	const char *result = UIDialogShow(windowMain, 0, 
			"Add waveform\n\n%l\n\nPointer to samples: (float *)\n%t\nSample count (per channel):\n%t\n"
			"Channels (interleaved):\n%t\n\n%l\n\n%f%b%b",
			&pointer, &sampleCount, &channels, "Add", "Cancel");

	if (0 == strcmp(result, "Add")) {
		WaveformViewerUpdate(pointer, sampleCount, channels, nullptr);
	}
}

//////////////////////////////////////////////////////
// Registration:
//////////////////////////////////////////////////////

__attribute__((constructor)) 
void ExtensionsRegister() {
	interfaceWindows.Add({ "Prof", ProfWindowCreate, ProfWindowUpdate, .alwaysUpdate = true });
	interfaceWindows.Add({ "Memory", MemoryWindowCreate, MemoryWindowUpdate });
	interfaceWindows.Add({ "View", ViewWindowCreate, ViewWindowUpdate });
	interfaceDataViewers.Add({ "Add waveform...", WaveformAddDialog });
	interfaceCommands.Add({ .label = nullptr, 
			{ .code = UI_KEYCODE_LETTER('V'), .ctrl = true, .shift = true, .invoke = ViewWindowView } });
}
