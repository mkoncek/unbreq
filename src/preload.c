#include <stdarg.h>
#include <stddef.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdio.h>

#include "shared.h"

static FILE* output = NULL;
static char curdir[1024] = {};
static int (*copen)(const char* file, int oflag, ...) = NULL;
static int (*copen64)(const char* file, int oflag, ...) = NULL;
static int (*copenat)(int fd, const char* file, int oflag, ...) = NULL;
static int (*copenat64)(int fd, const char* file, int oflag, ...) = NULL;

__attribute__((constructor))
static void constructor(void)
{
	copen = dlsym(RTLD_NEXT, "open");
	copen64 = dlsym(RTLD_NEXT, "open64");
	copenat = dlsym(RTLD_NEXT, "openat");
	copenat64 = dlsym(RTLD_NEXT, "openat64");
	output = fopen(static_accessed_files, "a");
}

__attribute__((destructor))
static void destructor(void)
{
	fclose(output);
}

int open(const char *file, int oflag, ...)
{
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, 1);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	// getcwd(curdir, sizeof(curdir));
	// fprintf(output, "%s, %d, %d\n", curdir, file, oflag, mode);
	if (file[0] != '/')
	{
		getcwd(curdir, sizeof(curdir));
		fprintf(output, "%s/%s\n", curdir, file);
	}
	else
	{
		fprintf(output, "%s\n", file);
	}
	return copen(file, oflag, mode);
}

int open64(const char *file, int oflag, ...)
{
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, 1);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	// getcwd(curdir, sizeof(curdir));
	// fprintf(output, "%s/%s, %d, %d\n", curdir, file, oflag, mode);
	if (file[0] != '/')
	{
		getcwd(curdir, sizeof(curdir));
		fprintf(output, "%s/%s\n", curdir, file);
	}
	else
	{
		fprintf(output, "%s\n", file);
	}
	return copen64(file, oflag, mode);
}

int openat(int fd, const char *file, int oflag, ...)
{
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, 1);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	// printf("### openat: %d, %s, %d, %d\n", fd, file, oflag, mode);
	return copenat(fd, file, oflag, mode);
}

int openat64(int fd, const char *file, int oflag, ...)
{
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, 1);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	// printf("### openat64: %d, %s, %d, %d\n", fd, file, oflag, mode);
	return copenat64(fd, file, oflag, mode);
}
