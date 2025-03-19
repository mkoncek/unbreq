.PHONY: all clean install install-link uninstall

all: target/fanotify

# libdnf5 deadlocks with sanitizers
# sanitize := -fsanitize=address,undefined

CC ?= cc
CXX ?= c++
CFLAGS ?= -Wall -Wextra -Wconversion -Wno-varargs -Og -g
CFLAGS += -std=c99
CXXFLAGS ?= -Wall -Wextra -Wpedantic -Wconversion -Og -g $(sanitize)
CXXFLAGS += -std=c++2a
LDFLAGS ?= -fsanitize=address,undefined

buildroot ?= /usr/libexec
python3_sitelib ?= /usr/lib/python*/site-packages
libexecdir ?= /usr/libexec

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

target/rpmquery.o: CXXFLAGS += -fsanitize=address,undefined
target/rpmquery.o: src/rpmquery.cpp Makefile | target/
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

target/resolve_dnf: CPPFLAGS += $(shell pkg-config libdnf5 --cflags)
target/resolve_dnf: CPPFLAGS += $(shell pkg-config libdnf5-cli --cflags)
target/resolve_dnf: LDFLAGS += $(shell pkg-config libdnf5 --libs)
target/resolve_dnf: LDFLAGS += $(shell pkg-config libdnf5-cli --libs)
target/resolve_dnf: src/resolve_dnf.cpp target/rpmquery.o Makefile | target/
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< target/rpmquery.o $(LDFLAGS) $(LDLIBS) -o $@

install-link: target/fanotify
	ln -s -t $(python3_sitelib)/mockbuild/plugins/ $$(readlink -f src/unbreq.py)
	ln -s $$(readlink -f target/fanotify) $(libexecdir)/unbreq

install: target/fanotify
	install -m 755 -D -t $(buildroot)$(python3_sitelib)/mockbuild/plugins src/unbreq.py
	install -m 755 -D target/fanotify $(buildroot)$(libexecdir)/unbreq

uninstall:
	rm -fv $(python3_sitelib)/mockbuild/plugins/unbreq.py $(libexecdir)/unbreq

print:
	echo $(python3_sitelib)
