#define _GNU_SOURCE

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <spawn.h>

#include <linux/limits.h>

__attribute__((visibility("hidden"))) extern void record_path(const char* path);
__attribute__((visibility("hidden"))) extern void record_fd(int fd);
__attribute__((visibility("hidden"))) extern void record_openat_path(int fd, const char* file);
__attribute__((visibility("hidden"))) extern void record_path_search(const char* file);

static _Thread_local char* static_argv[4096] = {};

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

DECLARE_FUNCTION_POINTER(posix_spawn);
DECLARE_FUNCTION_POINTER(posix_spawnp);

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
	
	ASSIGN_FUNCTION_POINTER(posix_spawn);
	ASSIGN_FUNCTION_POINTER(posix_spawnp);
}

int open(const char* file, int oflag, ...)
{
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
	record_path(path);
	return execve_orig(path, argv, envp);
}

int fexecve(int fd, char* const argv[], char* const envp[])
{
	record_fd(fd);
	return fexecve_orig(fd, argv, envp);
}

int execv(const char* path, char* const argv[])
{
	record_path(path);
	return execv_orig(path, argv);
}

int execle(const char* path, const char* arg, ...)
{
	va_list args;
	va_start(args, arg);
	size_t argc = 1;
	while (va_arg(args, const char*) != NULL)
	{
		argc++;
	}
	char* const* envp = va_arg(args, char* const*);
	va_end(args);
	
	if (argc + 1 > sizeof(static_argv) / sizeof(static_argv[0]))
	{
		errno = E2BIG;
		return -1;
	}
	static_argv[0] = (char*)arg;
	va_start(args, arg);
	for (size_t i = 1; i < argc; i++)
	{
		static_argv[i] = va_arg(args, char*);
	}
	static_argv[argc] = NULL;
	va_end(args);
	
	record_path(path);
	return execve_orig(path, static_argv, envp);
}

int execl(const char* path, const char* arg, ...)
{
	va_list args;
	va_start(args, arg);
	size_t argc = 1;
	while (va_arg(args, const char*) != NULL)
	{
		argc++;
	}
	va_end(args);
	
	if (argc + 1 > sizeof(static_argv) / sizeof(static_argv[0]))
	{
		errno = E2BIG;
		return -1;
	}
	static_argv[0] = (char*)arg;
	va_start(args, arg);
	for (size_t i = 1; i < argc; i++)
	{
		static_argv[i] = va_arg(args, char*);
	}
	static_argv[argc] = NULL;
	va_end(args);
	
	record_path(path);
	return execv_orig(path, static_argv);
}

int execvp(const char* file, char* const argv[])
{
	record_path_search(file);
	return execvp_orig(file, argv);
}

int execlp(const char* file, const char* arg, ...)
{
	va_list args;
	va_start(args, arg);
	size_t argc = 1;
	while (va_arg(args, const char*) != NULL)
	{
		argc++;
	}
	va_end(args);
	
	if (argc + 1 > sizeof(static_argv) / sizeof(static_argv[0]))
	{
		errno = E2BIG;
		return -1;
	}
	static_argv[0] = (char*)arg;
	va_start(args, arg);
	for (size_t i = 1; i < argc; i++)
	{
		static_argv[i] = va_arg(args, char*);
	}
	static_argv[argc] = NULL;
	va_end(args);
	
	record_path_search(file);
	return execvp_orig(file, static_argv);
}

int execvpe(const char* file, char* const argv[], char* const envp[])
{
	record_path_search(file);
	return execvpe_orig(file, argv, envp);
}

int posix_spawn(pid_t* pid, const char* path,
	const posix_spawn_file_actions_t* file_actions,
	const posix_spawnattr_t* attrp,
	char* const argv[], char* const envp[])
{
	record_path(path);
	return posix_spawn_orig(pid, path, file_actions, attrp, argv, envp);
}

int posix_spawnp(pid_t* pid, const char* file,
	const posix_spawn_file_actions_t* file_actions,
	const posix_spawnattr_t* attrp,
	char* const argv[], char* const envp[])
{
	record_path_search(file);
	return posix_spawnp_orig(pid, file, file_actions, attrp, argv, envp);
}
