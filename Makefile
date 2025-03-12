all: target/libunbreq.so target/resolve

CC ?= cc
CXX ?= c++
CFLAGS ?= -Wall -Wextra -Wconversion -Wno-varargs -Og -g
CFLAGS += -std=c99
CXXFLAGS ?= -Wall -Wextra -Wpedantic -Wconversion -Og -g
CXXFLAGS += -std=c++2a
LDLIBS += -lrpm -lrpmio -lrpmbuild

clean:
	@rm -rf -v target

%/:
	@mkdir -p $@

target/libunbreq.so: src/preload.c Makefile src/shared.hpp | target/
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -shared -fpic -o $@

target/resolve: src/resolve.cpp Makefile src/shared.hpp | target/
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

target/fuse: src/fuse.cpp Makefile | target/
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I/usr/include/fuse3 $(LDFLAGS) $(LDLIBS) -lfuse3 -o $@ $<

target/fanotify: src/fanotify.cpp Makefile | target/
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

# sudo setcap 'cap_sys_admin=+ep cap_dac_read_search=+ep' ./target/fanotify

# /usr/lib/python3.13/site-packages/mockbuild/plugins/package_state.py
