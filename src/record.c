#define _GNU_SOURCE

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <spawn.h>

#include <sys/file.h>
#include <linux/limits.h>

static const char* static_output_fd_env = NULL;
static int static_output_fd = 0;
static _Thread_local char static_curdir[PATH_MAX] = {};
static _Thread_local char static_link[32] = {};
static _Thread_local char static_resolved[PATH_MAX] = {};

__attribute__((constructor))
static void constructor(void)
{
	static_output_fd_env = getenv("UNBREQ_OUTPUT_FD");
	if (static_output_fd_env == NULL)
	{
		fprintf(stderr, "[ERROR] unbreq plugin: UNBREQ_OUTPUT_FD is not set\n");
		exit(127);
	}
	static_output_fd = atoi(static_output_fd_env);
	if (static_output_fd == 0)
	{
		fprintf(stderr, "[ERROR] unbreq plugin: unable to read number from UNBREQ_OUTPUT_FD: %s\n", static_output_fd_env);
		exit(127);
	}
}

#define RECORD_OUTPUT(...) do {\
	flock(static_output_fd, LOCK_EX);\
	dprintf(static_output_fd, __VA_ARGS__);\
	flock(static_output_fd, LOCK_UN);\
} while (0)

void record_path(const char* path)
{
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
		RECORD_OUTPUT("%s/%s\n", static_curdir, path);
	}
	else
	{
		RECORD_OUTPUT("%s\n", path);
	}
}

void record_fd(int fd)
{
	snprintf(static_link, sizeof(static_link), "/proc/self/fd/%d", fd);
	ssize_t len = readlink(static_link, static_resolved, sizeof(static_resolved));
	if (len > 0 && len <= INT_MAX)
	{
		RECORD_OUTPUT("%.*s\n", (int)len, static_resolved);
	}
}

void record_openat_path(int fd, const char* file)
{
	if (file == NULL)
	{
		return;
	}
	if (file[0] == '/')
	{
		RECORD_OUTPUT("%s\n", file);
	}
	else if (fd == AT_FDCWD)
	{
		if (getcwd(static_curdir, sizeof(static_curdir)) == NULL)
		{
			return;
		}
		RECORD_OUTPUT("%s/%s\n", static_curdir, file);
	}
	else
	{
		snprintf(static_link, sizeof(static_link), "/proc/self/fd/%d", fd);
		ssize_t len = readlink(static_link, static_resolved, sizeof(static_resolved));
		if (len > 0 && len <= INT_MAX)
		{
			RECORD_OUTPUT("%.*s/%s\n", (int)len, static_resolved, file);
		}
	}
}

void record_path_search(const char* file)
{
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
			RECORD_OUTPUT("%s\n", static_resolved);
			free(path_copy);
			return;
		}
	}
	free(path_copy);
}
