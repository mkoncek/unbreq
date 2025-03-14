.PHONY: all clean install install-link uninstall

all: target/fanotify

CC ?= cc
CXX ?= c++
CFLAGS ?= -Wall -Wextra -Wconversion -Wno-varargs -Og -g -fsanitize=address,undefined
CFLAGS += -std=c99
CXXFLAGS ?= -Wall -Wextra -Wpedantic -Wconversion -Og -g -fsanitize=address,undefined
CXXFLAGS += -std=c++2a
LDFLAGS ?= -fsanitize=address,undefined
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

install-link: target/fanotify
	ln -s -t /usr/lib/python*/site-packages/mockbuild/plugins/ $$(readlink -f src/unbreq.py)
	ln -s $$(readlink -f target/fanotify) /usr/libexec/unbreq

install: target/fanotify
	install -m 755 -t /usr/lib/python*/site-packages/mockbuild/plugins src/unbreq.py
	install -m 755 target/fanotify /usr/libexec/unbreq

uninstall:
	rm -fv /usr/lib/python*/site-packages/mockbuild/plugins/unbreq.py /usr/libexec/unbreq
# sudo setcap 'cap_sys_admin=+ep cap_dac_read_search=+ep' ./target/fanotify

# /usr/lib/python3.13/site-packages/mockbuild/plugins/package_state.py
