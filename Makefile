MAKEFLAGS += -r
.PHONY: compile clean install install-link uninstall

define removesrcprefix
$(patsubst src/%,%,$(1))
endef

compile: target/bin/fanotify target/bin/resolve

# libdnf5 deadlocks with sanitizers
# sanitize := -fsanitize=address,undefined

CXX ?= c++
CXXFLAGS ?= -Wall -Wextra -Wpedantic -Wconversion -Og -g $(sanitize)
CXXFLAGS += -std=c++2a
# LDFLAGS ?= -fsanitize=address,undefined

buildroot ?= target/buildroot
python3_sitelib ?= /usr/lib/python*/site-packages
libexecdir ?= /usr/libexec

clean:
	@rm -rf -v target

%/:
	@mkdir -p $@

target/object_files/%.o: Makefile | target/dependencies/ target/object_files/
	$(CXX) -o $@ $(word 2,$^) -c $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -MF target/dependencies/$(basename $(call removesrcprefix,$(word 2,$^))).mk -MT $@

target/bin/%: Makefile | target/bin/
	$(CXX) -o $@ $(wordlist 2,$(words $^),$^) $(LDFLAGS) $(LDLIBS)

# target/object_files/rpmquery.o: CXXFLAGS += -fsanitize=address,undefined
target/object_files/rpmquery.o: CXXFLAGS += $(shell pkg-config --cflags rpm)
target/object_files/rpmquery.o: src/rpmquery.cpp

target/object_files/resolve.o: CXXFLAGS += $(shell pkg-config --cflags libdnf5 libdnf5-cli)
target/object_files/resolve.o: src/resolve.cpp

target/object_files/fanotify.o: src/fanotify.cpp

target/bin/fanotify: target/object_files/fanotify.o
target/bin/resolve: LDFLAGS += $(shell pkg-config --libs libdnf5 libdnf5-cli rpm)
target/bin/resolve: target/object_files/resolve.o target/object_files/rpmquery.o

install-link: target/bin/fanotify
	ln -s -t $(python3_sitelib)/mockbuild/plugins/ $$(readlink -f src/unbreq.py)
	mkdir -p $(libexecdir)/unbreq
	ln -s -t $(libexecdir)/unbreq $$(readlink -f target/bin/fanotify target/bin/resolve)
	ls -l $(python3_sitelib)/mockbuild/plugins/unbreq.py $(libexecdir)/unbreq/*

install: target/bin/fanotify
	install -m 755 -D -t $(buildroot)$(python3_sitelib)/mockbuild/plugins src/unbreq.py
	install -m 755 -D -t $(buildroot)$(libexecdir)/unbreq target/bin/fanotify target/bin/resolve

uninstall:
	rm -rfv $(python3_sitelib)/mockbuild/plugins/unbreq.py $(libexecdir)/unbreq

-include target/dependencies/*.mk
