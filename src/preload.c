#define _GNU_SOURCE

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <errno.h>
#include <stdio.h>

#include <linux/limits.h>

// #define TRACE fprintf(stderr, "[DEBUG] %s\n", __func__)
#define TRACE ;

static const char* static_output_path = NULL;
static FILE* static_output = NULL;
static __thread char static_curdir[PATH_MAX] = {};
static __thread char static_link[32] = {};
static __thread char static_resolved[PATH_MAX] = {};
static __thread char* static_argv[4096] = {};

#define DECLARE_FUNCTION_POINTER(name) static __typeof__(name)* name##_orig = NULL
#define ASSIGN_FUNCTION_POINTER(name) name##_orig = (__typeof__(name##_orig))dlsym(RTLD_NEXT, #name)

DECLARE_FUNCTION_POINTER(open);
DECLARE_FUNCTION_POINTER(open64);
DECLARE_FUNCTION_POINTER(openat);
DECLARE_FUNCTION_POINTER(openat64);

DECLARE_FUNCTION_POINTER(execve);
DECLARE_FUNCTION_POINTER(fexecve);
DECLARE_FUNCTION_POINTER(execv);
DECLARE_FUNCTION_POINTER(execle);
DECLARE_FUNCTION_POINTER(execl);
DECLARE_FUNCTION_POINTER(execvp);
DECLARE_FUNCTION_POINTER(execlp);
DECLARE_FUNCTION_POINTER(execvpe);

__attribute__((constructor))
static void constructor(void)
{
	ASSIGN_FUNCTION_POINTER(open);
	ASSIGN_FUNCTION_POINTER(open64);
	ASSIGN_FUNCTION_POINTER(openat);
	ASSIGN_FUNCTION_POINTER(openat64);
	
	ASSIGN_FUNCTION_POINTER(execve);
	ASSIGN_FUNCTION_POINTER(fexecve);
	ASSIGN_FUNCTION_POINTER(execv);
	ASSIGN_FUNCTION_POINTER(execle);
	ASSIGN_FUNCTION_POINTER(execl);
	ASSIGN_FUNCTION_POINTER(execvp);
	ASSIGN_FUNCTION_POINTER(execlp);
	ASSIGN_FUNCTION_POINTER(execvpe);
	
	static_output_path = getenv("UNBREQ_OUTPUT_PATH");
	if (static_output_path == NULL)
	{
		fprintf(stderr, "[ERROR] unbreq plugin: UNBREQ_OUTPUT_PATH is not set\n");
		exit(127);
	}
	static_output = fopen(static_output_path, "a");
	if (static_output == NULL)
	{
		fprintf(stderr, "[ERROR] unbreq plugin: file %s could not be opened\n", static_output_path);
		exit(127);
	}
}

__attribute__((destructor))
static void destructor(void)
{
	if (static_output != NULL)
	{
		fclose(static_output);
	}
}

static void record_path(const char* path)
{
	TRACE;
	if (path == NULL)
	{
		return;
	}
	if (path[0] != '/')
	{
		if (getcwd(static_curdir, sizeof(static_curdir)) == NULL)
		{
			return;
		}
		fprintf(static_output, "%s/%s\n", static_curdir, path);
	}
	else
	{
		fprintf(static_output, "%s\n", path);
	}
}

static void record_fd(int fd)
{
	TRACE;
	snprintf(static_link, sizeof(static_link), "/proc/self/fd/%d", fd);
	ssize_t len = readlink(static_link, static_resolved, sizeof(static_resolved) - 1);
	if (len > 0)
	{
		static_resolved[len] = '\0';
		fprintf(static_output, "%s\n", static_resolved);
	}
}

static void record_openat_path(int fd, const char* file)
{
	TRACE;
	if (file == NULL)
	{
		return;
	}
	if (file[0] == '/')
	{
		fprintf(static_output, "%s\n", file);
	}
	else if (fd == AT_FDCWD)
	{
		if (getcwd(static_curdir, sizeof(static_curdir)) == NULL)
		{
			return;
		}
		fprintf(static_output, "%s/%s\n", static_curdir, file);
	}
	else
	{
		snprintf(static_link, sizeof(static_link), "/proc/self/fd/%d", fd);
		ssize_t len = readlink(static_link, static_resolved, sizeof(static_resolved) - 1);
		if (len > 0)
		{
			static_resolved[len] = '\0';
			fprintf(static_output, "%s/%s\n", static_resolved, file);
		}
	}
}

static void record_path_search(const char* file)
{
	TRACE;
	if (file == NULL)
	{
		return;
	}
	if (strchr(file, '/') != NULL)
	{
		record_path(file);
		return;
	}
	const char* path_env = getenv("PATH");
	if (path_env == NULL)
	{
		return;
	}
	char* path_copy = strdup(path_env);
	if (path_copy == NULL)
	{
		return;
	}
	char* saveptr = NULL;
	for (char* dir = strtok_r(path_copy, ":", &saveptr); dir != NULL; dir = strtok_r(NULL, ":", &saveptr))
	{
		snprintf(static_resolved, sizeof(static_resolved), "%s/%s", dir, file);
		if (access(static_resolved, X_OK) == 0)
		{
			fprintf(static_output, "%s\n", static_resolved);
			free(path_copy);
			return;
		}
	}
	free(path_copy);
}

int open(const char* file, int oflag, ...)
{
	TRACE;
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, oflag);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	record_path(file);
	return open_orig(file, oflag, mode);
}

int open64(const char* file, int oflag, ...)
{
	TRACE;
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, oflag);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	record_path(file);
	return open64_orig(file, oflag, mode);
}

int openat(int fd, const char* file, int oflag, ...)
{
	TRACE;
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, oflag);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	record_openat_path(fd, file);
	return openat_orig(fd, file, oflag, mode);
}

int openat64(int fd, const char* file, int oflag, ...)
{
	TRACE;
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, oflag);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	record_openat_path(fd, file);
	return openat64_orig(fd, file, oflag, mode);
}

int execve(const char* path, char* const argv[], char* const envp[])
{
	TRACE;
	record_path(path);
	fflush(static_output);
	return execve_orig(path, argv, envp);
}

int fexecve(int fd, char* const argv[], char* const envp[])
{
	TRACE;
	record_fd(fd);
	fflush(static_output);
	return fexecve_orig(fd, argv, envp);
}

int execv(const char* path, char* const argv[])
{
	TRACE;
	record_path(path);
	fflush(static_output);
	return execv_orig(path, argv);
}

int execle(const char* path, const char* arg, ...)
{
	TRACE;
	va_list ap;
	va_start(ap, arg);
	size_t argc = 1;
	while (va_arg(ap, const char*) != NULL)
	{
		argc++;
	}
	char* const* envp = va_arg(ap, char* const*);
	va_end(ap);
	
	if (argc + 1 > sizeof(static_argv) / sizeof(static_argv[0]))
	{
		errno = E2BIG;
		return -1;
	}
	static_argv[0] = (char*)arg;
	va_start(ap, arg);
	for (size_t i = 1; i < argc; i++)
	{
		static_argv[i] = va_arg(ap, char*);
	}
	static_argv[argc] = NULL;
	va_end(ap);
	
	record_path(path);
	fflush(static_output);
	return execve_orig(path, static_argv, envp);
}

int execl(const char* path, const char* arg, ...)
{
	TRACE;
	va_list ap;
	va_start(ap, arg);
	size_t argc = 1;
	while (va_arg(ap, const char*) != NULL)
	{
		argc++;
	}
	va_end(ap);
	
	if (argc + 1 > sizeof(static_argv) / sizeof(static_argv[0]))
	{
		errno = E2BIG;
		return -1;
	}
	static_argv[0] = (char*)arg;
	va_start(ap, arg);
	for (size_t i = 1; i < argc; i++)
	{
		static_argv[i] = va_arg(ap, char*);
	}
	static_argv[argc] = NULL;
	va_end(ap);
	
	record_path(path);
	fflush(static_output);
	return execv_orig(path, static_argv);
}

int execvp(const char* file, char* const argv[])
{
	TRACE;
	record_path_search(file);
	fflush(static_output);
	return execvp_orig(file, argv);
}

int execlp(const char* file, const char* arg, ...)
{
	TRACE;
	va_list ap;
	va_start(ap, arg);
	size_t argc = 1;
	while (va_arg(ap, const char*) != NULL)
	{
		argc++;
	}
	va_end(ap);
	
	if (argc + 1 > sizeof(static_argv) / sizeof(static_argv[0]))
	{
		errno = E2BIG;
		return -1;
	}
	static_argv[0] = (char*)arg;
	va_start(ap, arg);
	for (size_t i = 1; i < argc; i++)
	{
		static_argv[i] = va_arg(ap, char*);
	}
	static_argv[argc] = NULL;
	va_end(ap);
	
	record_path_search(file);
	fflush(static_output);
	return execvp_orig(file, static_argv);
}

int execvpe(const char* file, char* const argv[], char* const envp[])
{
	TRACE;
	record_path_search(file);
	fflush(static_output);
	return execvpe_orig(file, argv, envp);
}
