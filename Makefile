.PHONY: all clean

all: target/libunbreq_preload.so

# sanitize := -fsanitize=address,undefined

CC ?= cc
CXX ?= c++
CFLAGS ?= -Wall -Wextra -Wconversion -Wno-varargs -Og -g $(sanitize)
CFLAGS += -std=c99 -flto
CXXFLAGS ?= -Wall -Wextra -Wpedantic -Wconversion -Og -g $(sanitize)
CXXFLAGS += -std=c++2a -flto
LDFLAGS ?= $(sanitize)

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
