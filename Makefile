all: target/libunbreq.so target/resolve

CC ?= cc
CXX ?= c++
CFLAGS ?= -Wall -Wextra -Wconversion -Wno-varargs -Og -g
CFLAGS += -std=c99
CXXFLAGS ?= -Wall -Wextra -Wpedantic -Wconversion -Og -g
CXXFLAGS += -std=c++20
LDLIBS += -lrpm -lrpmio -lrpmbuild

clean:
	@rm -rf -v target

target:
	@mkdir -p $@

target/libunbreq.so: src/preload.c Makefile src/shared.h | target
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -shared -fpic -o $@

target/resolve: src/resolve.cpp Makefile src/shared.h | target
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@
