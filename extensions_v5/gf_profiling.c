// ------------- Configuration -------------
#define GF_PROFILING_BUFFER_BYTES (64 * 1024 * 1024)
#define GF_PROFILING_CLOCK CLOCK_MONOTONIC
// #define GF_PROFILING_CLOCK CLOCK_THREAD_CPUTIME_ID
// -----------------------------------------

/*
------------------------------------------------------------------------------
This file is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2021 nakst
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------
*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>

#ifdef __cplusplus
#define GF_PROFILING_EXTERN extern "C"
#else
#define GF_PROFILING_EXTERN
#endif

typedef struct GfProfilingEntry {
	void *thisFunction;
	uint64_t timeStamp;
} GfProfilingEntry;

static __thread bool gfProfilingEnabledOnThisThread;
static bool gfProfilingEnabled;
static size_t gfProfilingBufferSize;
GfProfilingEntry *gfProfilingBuffer;
uintptr_t gfProfilingBufferPosition;
uint64_t gfProfilingTicksPerMs;

#define GF_PROFILING_FUNCTION(_exiting) \
	(void) callSite; \
	\
	if (gfProfilingBufferPosition < gfProfilingBufferSize && gfProfilingEnabledOnThisThread) { \
		GfProfilingEntry *entry = (GfProfilingEntry *) &gfProfilingBuffer[gfProfilingBufferPosition++]; \
		entry->thisFunction = thisFunction; \
		struct timespec time; \
		clock_gettime(GF_PROFILING_CLOCK, &time); \
		entry->timeStamp = ((uint64_t) time.tv_sec * 1000000000 + time.tv_nsec) | ((uint64_t) _exiting << 63); \
	}

GF_PROFILING_EXTERN __attribute__((no_instrument_function))
void __cyg_profile_func_enter(void *thisFunction, void *callSite) {
	GF_PROFILING_FUNCTION(0);
}

GF_PROFILING_EXTERN __attribute__((no_instrument_function))
void __cyg_profile_func_exit(void *thisFunction, void *callSite) {
	GF_PROFILING_FUNCTION(1);
}

GF_PROFILING_EXTERN __attribute__((no_instrument_function))
void GfProfilingStart() {
	assert(!gfProfilingEnabled);
	assert(!gfProfilingEnabledOnThisThread);
	assert(gfProfilingBufferSize);
	gfProfilingEnabled = true;
	gfProfilingEnabledOnThisThread = true;
	gfProfilingBufferPosition = 0;
}

GF_PROFILING_EXTERN __attribute__((no_instrument_function))
void GfProfilingStop() {
	assert(gfProfilingEnabled);
	assert(gfProfilingEnabledOnThisThread);
	gfProfilingEnabled = false;
	gfProfilingEnabledOnThisThread = false;
}

__attribute__((constructor)) 
__attribute__((no_instrument_function))
void GfProfilingInitialise() {
	gfProfilingBufferSize = GF_PROFILING_BUFFER_BYTES / sizeof(GfProfilingEntry);
	gfProfilingBuffer = (GfProfilingEntry *) malloc(GF_PROFILING_BUFFER_BYTES);
	gfProfilingTicksPerMs = 1000000;
	assert(gfProfilingBufferSize && gfProfilingBuffer);
}
