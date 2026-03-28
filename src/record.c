#define _GNU_SOURCE

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <limits.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <spawn.h>

#include <linux/limits.h>

static const char* static_output_fd_env = NULL;
static int static_output_fd = 0;
static _Thread_local char static_buffer[PATH_MAX] = {};
static _Thread_local char static_link_buffer[32] = "/proc/self/fd/";

__attribute__((format(printf, 1, 2)))
static void log_warning(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fputs("[WARNING] unbreq plugin: ", stderr);
	vfprintf(stderr, fmt, args);
	fputs("\n", stderr);
	va_end(args);
}

__attribute__((format(printf, 1, 2), noreturn))
static void exit_with_error(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fputs("[ERROR] unbreq plugin: ", stderr);
	vfprintf(stderr, fmt, args);
	fputs("\n", stderr);
	va_end(args);
	exit(127);
}

__attribute__((constructor))
static void constructor(void)
{
	static_output_fd_env = getenv("UNBREQ_OUTPUT_FD");
	if (static_output_fd_env == NULL)
	{
		exit_with_error("UNBREQ_OUTPUT_FD is not set");
	}
	static_output_fd = atoi(static_output_fd_env);
	if (static_output_fd == 0)
	{
		exit_with_error("unable to read number from UNBREQ_OUTPUT_FD: %s", static_output_fd_env);
	}
}

static int buffer_check_size(int length)
{
	if (length < 0)
	{
		log_warning("invalid data length for buffer: %d", length);
		return 0;
	}
	if (length > (int)sizeof(static_buffer))
	{
		log_warning("data length for buffer is loo large: %d", length);
		return 0;
	}
	return 1;
}

static int buffer_store_fd(int fd)
{
	if (fd < 0 || fd > 9999)
	{
		exit_with_error("invalid file descriptor value: %d", fd);
	}
	const int length = sizeof("/proc/self/fd/") - 1;
	int digits = 1;
	for (int n = 10; n <= fd; n *= 10)
	{
		++digits;
	}
	for (int n = digits; n != 0; --n)
	{
		static_link_buffer[length - 1 + n] = '0' + (char)(fd % 10);
		fd /= 10;
	}
	static_link_buffer[length + digits] = '\0';
	return length + digits;
}

static int buffer_readlink(int fd)
{
	buffer_store_fd(fd);
	ssize_t length = readlink(static_link_buffer, static_buffer, sizeof(static_buffer));
	if (length == -1)
	{
		log_warning("readlink on %d returned error: %s", fd, strerror(errno));
	}
	return (int)length;
}

static int buffer_store_cwd()
{
	int result = 0;
	if (getcwd(static_buffer, sizeof(static_buffer)) == NULL)
	{
		log_warning("getcwd returned NULL: %s", strerror(errno));
		return -1;
	}
	result = (int)strlen(static_buffer);
	static_buffer[result] = '/';
	return result + 1;
}

static void buffer_record_output(int length)
{
	if (buffer_check_size(length + 1))
	{
		static_buffer[length] = '\n';
		++length;
		if (write(static_output_fd, static_buffer, (size_t)length) == -1)
		{
			log_warning("write failed on fd %d: %s", static_output_fd, strerror(errno));
		}
	}
}

void record_path(const char* path)
{
	if (path == NULL)
	{
		return;
	}
	int pos = 0;
	int path_length = (int)strlen(path);
	if (path[0] != '/')
	{
		if ((pos = buffer_store_cwd()) == -1)
		{
			return;
		}
	}
	memcpy(static_buffer + pos, path, (size_t)path_length);
	pos += path_length;
	buffer_record_output(pos);
}

void record_fd(int fd)
{
	int length = buffer_readlink(fd);
	if (length != -1)
	{
		buffer_record_output(length);
	}
}

void record_openat_path(int fd, const char* path)
{
	if (path == NULL)
	{
		return;
	}
	int path_length = (int)strlen(path);
	if (path[0] == '/')
	{
		memcpy(static_buffer, path, (size_t)path_length);
		buffer_record_output(path_length);
	}
	else if (fd == AT_FDCWD)
	{
		int pos = buffer_store_cwd();
		if (pos == -1)
		{
			return;
		}
		memcpy(static_buffer + pos, path, (size_t)path_length);
		buffer_record_output(pos + path_length);
	}
	else
	{
		int length = buffer_readlink(fd);
		if (length != -1)
		{
			static_buffer[length] = '/';
			++length;
			memcpy(static_buffer + length, path, (size_t)path_length);
			length += path_length;
			buffer_record_output((int)length);
		}
	}
}

void record_path_search(const char* path)
{
	if (path == NULL)
	{
		return;
	}
	if (strchr(path, '/') != NULL)
	{
		return record_path(path);
	}
	int path_length = (int)strlen(path);
	const char* path_env = getenv("PATH");
	if (path_env != NULL)
	{
		for (const char* entry_end = path_env; (entry_end = strchr(path_env, ':')) != NULL; path_env = entry_end + 1)
		{
			int length = (int)(entry_end - path_env);
			if (length > 0)
			{
				memcpy(static_buffer, path_env, (size_t)length);
				static_buffer[length] = '/';
				++length;
				memcpy(static_buffer + length, path, (size_t)path_length);
				length += path_length;
				static_buffer[length] = '\0';
				if (access(static_buffer, X_OK) == 0)
				{
					return buffer_record_output(length);
				}
			}
		}
	}
}
