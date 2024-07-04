#include <stdarg.h>
#include <stddef.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdio.h>

#include "shared.h"

static FILE* static_output = NULL;
static char static_curdir[1024] = {};

static int (*copen)(const char* file, int oflag, ...) = NULL;
static int (*copen64)(const char* file, int oflag, ...) = NULL;
static int (*copenat)(int fd, const char* file, int oflag, ...) = NULL;
static int (*copenat64)(int fd, const char* file, int oflag, ...) = NULL;

static int (*cexecve)(const char* path, char* const argv[], char* const envp[]) = NULL;
static int (*cfexecve)(int fd, char* const argv[], char* const envp[]) = NULL;
static int (*cexecv)(const char* path, char* const argv[]) = NULL;
static int (*cexecle)(const char* path, const char* arg, ...) = NULL;
static int (*cexecl)(const char* path, const char* arg, ...) = NULL;
static int (*cexecvp)(const char* file, char* const argv[]) = NULL;
static int (*cexeclp)(const char* file, const char* arg, ...) = NULL;
static int (*cexecvpe)(const char* file, char* const argv[], char* const envp[]) = NULL;

__attribute__((constructor))
static void constructor(void)
{
	copen = dlsym(RTLD_NEXT, "open");
	copen64 = dlsym(RTLD_NEXT, "open64");
	copenat = dlsym(RTLD_NEXT, "openat");
	copenat64 = dlsym(RTLD_NEXT, "openat64");
	
	cexecve = dlsym(RTLD_NEXT, "execve");
	cfexecve = dlsym(RTLD_NEXT, "fexecve");
	cexecv = dlsym(RTLD_NEXT, "execv");
	cexecle = dlsym(RTLD_NEXT, "execle");
	cexecl = dlsym(RTLD_NEXT, "execl");
	cexecvp = dlsym(RTLD_NEXT, "execvp");
	cexeclp = dlsym(RTLD_NEXT, "execlp");
	cexecvpe = dlsym(RTLD_NEXT, "execvpe");
	
	static_output = fopen(static_accessed_files, "a");
}

__attribute__((destructor))
static void destructor(void)
{
	fclose(static_output);
}

static void record_path(const char* path)
{
	if (path[0] != '/')
	{
		getcwd(static_curdir, sizeof(static_curdir));
		fprintf(static_output, "%s/%s\n", static_curdir, path);
	}
	else
	{
		fprintf(static_output, "%s\n", path);
	}
}

int open(const char* file, int oflag, ...)
{
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, 1);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	record_path(file);
	return copen(file, oflag, mode);
}

int open64(const char* file, int oflag, ...)
{
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, 1);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	record_path(file);
	return copen64(file, oflag, mode);
}

/*
int openat(int fd, const char* file, int oflag, ...)
{
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, 1);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	// TODO
	return copenat(fd, file, oflag, mode);
}

int openat64(int fd, const char* file, int oflag, ...)
{
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, 1);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	// TODO
	return copenat64(fd, file, oflag, mode);
}
*/

int execve(const char* path, char* const argv[], char* const envp[])
{
	record_path(path);
	return cexecve(path, argv, envp);
}

/*
int fexecve(int fd, char* const argv[], char* const envp[])
{
	// TODO
	return cfexecve(fd, argv, envp);
}
*/

int execv(const char* path, char* const argv[])
{
	record_path(path);
	return cexecv(path, argv);
}

/*
int execle(const char* path, const char* arg, ...)
{
	fprintf(static_output, "%s\n", path);
	return cexecle(path, arg, ...);
}

int execl(const char* path, const char* arg, ...)
{
	fprintf(static_output, "%s\n", path);
	return cexecl(path, arg, ...);
}

int execvp(const char* file, char* const argv[])
{
	// TODO
	return cexecvp(file, argv);
}

int execlp(const char* file, const char* arg, ...)
{
	// TODO
	return cexeclp(file, arg, ...);
}

int execvpe(const char* file, char* const argv[], char* const envp[])
{
	// TODO
	return cexecvpe(file, argv, envp);
}
*/
