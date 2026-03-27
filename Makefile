.PHONY: all clean install install-link uninstall

all: target/libunbreq_preload.so

# sanitize := -fsanitize=address,undefined

CC ?= cc
CXX ?= c++
CFLAGS ?= -Wall -Wextra -Wconversion -Wno-varargs -Og -g $(sanitize)
CFLAGS += -std=c99 -flto
CXXFLAGS ?= -Wall -Wextra -Wpedantic -Wconversion -Og -g $(sanitize)
CXXFLAGS += -std=c++2a -flto
LDFLAGS ?= $(sanitize)

buildroot ?= /usr/libexec
python3_sitelib ?= /usr/lib/python*/site-packages
libexecdir ?= /usr/libexec

clean:
	@rm -rf -v target

%/:
	@mkdir -p $@

target/preload.c.o: src/preload.c Makefile | target/
	$(CC) $(CPPFLAGS) $(CFLAGS) -fpic -c -o $@ $<
target/record.c.o: src/record.c Makefile | target/
	$(CC) $(CPPFLAGS) $(CFLAGS) -fpic -c -o $@ $<

target/libunbreq_preload.so: target/preload.c.o target/record.c.o Makefile | target/
target/libunbreq_preload.so: target/preload.c.o target/record.c.o
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -fpic -o $@ target/preload.c.o target/record.c.o

# TODO
install-link: target/libunbreq_preload.so
	ln -s -t $(python3_sitelib)/mockbuild/plugins/ $$(readlink -f src/unbreq.py)
	ln -s $$(readlink -f target/fanotify) $(libexecdir)/unbreq

install: target/libunbreq_preload.so
	install -m 755 -D -t $(buildroot)$(python3_sitelib)/mockbuild/plugins src/unbreq.py
	install -m 755 -D target/fanotify $(buildroot)$(libexecdir)/unbreq

uninstall:
	rm -fv $(python3_sitelib)/mockbuild/plugins/unbreq.py $(libexecdir)/unbreq
