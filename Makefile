#
# MIT License
# 
# Copyright (c) 2026 mebenn (Benny Lyons) and others
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

#
# Usage:
#        make         -> builds the standard gf2
#        make clean   -> clean-up
#        make check   -> for developers, add what you'd like here
#                        currently display flags for your OS
# Use only GNU make, make without any targets just builds a staandard
#
OSTYPE := $(shell uname -s)

#
# Linux
#
ifeq ($(OSTYPE),Linux)
	OSFLAG += -D LINUX
	CPUARCH := $(shell uname -m)
	ifeq ($(CPUARCH),x86_64)
		LDFLAGS = -DUI_SSE2
	endif
	ifeq ("$(wildcard "/usr/include/freetype2")", "")
		LDFLAGS += -lfreetype -D UI_FREETYPE -I/usr/include/freetype2
	else
		$(warning FreeType fonts coulld not be found, defecting to defaults)
	endif
endif

#
# OpenIndiana/Illumos
#
ifeq ($(OSTYPE),SunOS)
	OSFLAG += -D SunOS
	CPUARCH := $(shell isainfo -k)
	ifeq ($(CPUARCH),amd64)
		LDFLAGS += -DUI_SSE2
	endif
	ifeq ("$(wildcard "/usr/include/freetype2")", "")
		LDFLAGS += -lfreetype -D UI_FREETYPE -I/usr/include/freetype2
	else
		$(warning FreeType fonts coulld not be found, defecting to defaults)
	endif
endif

# TODO:
# FreeBSD
#
ifeq ($(OSTYPE),FreeBSD)
	OSFLAG += FreeBSD
endif

# TODO:
# OpenBSD
#
ifeq ($(OSTYPE),OpenBSD)
		OSFLAG += OpenBSD
endif

#TODO:
# NetBSD
#
ifeq ($(OSTYPE),NetBSD)
	OSFLAG += NetBSD
endif

# TODO:
# MacOS
#
ifeq ($(OSTYPE),Darwin)
	OSFLAG += -D OSX
endif


APP=gf2


CXX=g++
CXXFLAGS=-g -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-result -Wno-missing-field-initializers -Wno-format-truncation 
LDLIBS = -lX11 -pthread 


$(APP): $(APP).o
	$(CXX) $^ -o $@ $(LDLIBS) $(LDFLAGS)
$(APP).o: $(APP).cpp
	$(CXX)   -c $< -o $@ $(LDFLAGS) $(CXXFLAGS) 


.PHONY: clean 
clean:
	rm -f ./*.o $(APP)
.PHONY: check
check:
	rm -f ./*.o $(APP)
	$(warning $(LDFLAGS) $(OSFLAG) )


