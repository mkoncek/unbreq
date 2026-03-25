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

#include <unistd.h>
#include <linux/limits.h>

static const char* const static_output_path = "/var/mock/unbreq";
static FILE* static_output = NULL;
static __thread char static_curdir[PATH_MAX] = {};
static __thread char static_link[32] = {};
static __thread char static_resolved[PATH_MAX] = {};
static __thread char* static_argv[4096] = {};

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
	
	static_output = fopen(static_output_path, "a");
	if (static_output == NULL)
	{
		fprintf(stderr, "[ERROR] File %s could not be opened", static_output_path);
	}
}

__attribute__((destructor))
static void destructor(void)
{
	fclose(static_output);
}

static void record_path(const char* path)
{
	fprintf(stderr, "[DEBUG] %s\n", __func__);
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

static void record_fd(int fd)
{
	fprintf(stderr, "[DEBUG] %s\n", __func__);
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
	fprintf(stderr, "[DEBUG] %s\n", __func__);
	if (file[0] == '/')
	{
		fprintf(static_output, "%s\n", file);
	}
	else if (fd == AT_FDCWD)
	{
		getcwd(static_curdir, sizeof(static_curdir));
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
	fprintf(stderr, "[DEBUG] %s\n", __func__);
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
	fprintf(stderr, "[DEBUG] %s\n", __func__);
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, 1);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	record_path(file);
	fprintf(stderr, "[DEBUG] ???\n");
	int result = copen(file, oflag, mode);
	fprintf(stderr, "[DEBUG] %d\n", result);
	return result;
}

int open64(const char* file, int oflag, ...)
{
	fprintf(stderr, "[DEBUG] %s\n", __func__);
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

int openat(int fd, const char* file, int oflag, ...)
{
	fprintf(stderr, "[DEBUG] %s\n", __func__);
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, oflag);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	record_openat_path(fd, file);
	return copenat(fd, file, oflag, mode);
}

int openat64(int fd, const char* file, int oflag, ...)
{
	fprintf(stderr, "[DEBUG] %s\n", __func__);
	mode_t mode = 0;
	if (oflag & (O_CREAT | __O_TMPFILE))
	{
		va_list args;
		va_start(args, oflag);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	record_openat_path(fd, file);
	return copenat64(fd, file, oflag, mode);
}

int execve(const char* path, char* const argv[], char* const envp[])
{
	fprintf(stderr, "[DEBUG] %s\n", __func__);
	record_path(path);
	return cexecve(path, argv, envp);
}

int fexecve(int fd, char* const argv[], char* const envp[])
{
	fprintf(stderr, "[DEBUG] %s\n", __func__);
	record_fd(fd);
	return cfexecve(fd, argv, envp);
}

int execv(const char* path, char* const argv[])
{
	fprintf(stderr, "[DEBUG] %s\n", __func__);
	record_path(path);
	return cexecv(path, argv);
}

int execle(const char* path, const char* arg, ...)
{
	fprintf(stderr, "[DEBUG] %s\n", __func__);
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
	return cexecve(path, static_argv, envp);
}

int execl(const char* path, const char* arg, ...)
{
	fprintf(stderr, "[DEBUG] %s\n", __func__);
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
	return cexecv(path, static_argv);
}

int execvp(const char* file, char* const argv[])
{
	fprintf(stderr, "[DEBUG] %s\n", __func__);
	record_path_search(file);
	return cexecvp(file, argv);
}

int execlp(const char* file, const char* arg, ...)
{
	fprintf(stderr, "[DEBUG] %s\n", __func__);
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
	return cexecvp(file, static_argv);
}

int execvpe(const char* file, char* const argv[], char* const envp[])
{
	fprintf(stderr, "[DEBUG] %s\n", __func__);
	record_path_search(file);
	return cexecvpe(file, argv, envp);
}
