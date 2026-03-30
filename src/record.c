#define _GNU_SOURCE

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <spawn.h>

#include <linux/limits.h>

static const char* static_output_fd_env = NULL;
static int static_output_fd = 0;

static _Thread_local int static_buffer_end = 0;
static _Thread_local char static_buffer[PATH_MAX] = {};
static _Thread_local char static_link_buffer[32] = "/proc/self/fd/";

typedef struct
{
	const char* data;
	int length;
} buffer_chunk;

static const buffer_chunk chunk_zero = {.data = "\0", .length = 1};
static const buffer_chunk chunk_slash = {.data = "/", .length = 1};
static const buffer_chunk chunk_newline = {.data = "\n", .length = 1};

static buffer_chunk buffer_chunk_from(const char* data, int length)
{
	return (buffer_chunk) {
		.data = data, .length = length,
	};
}

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

//! Push multiple data chunks ino the static buffer. If the buffer is too small,
//! drop all the content and clear the buffer.
//! @param n Number of chunks.
//! @param chunks Array of chunks.
//! @return True if the push operation succeeded, otherwise false.
static _Bool buffer_push(int n, const buffer_chunk* chunks)
{
	int total_length = 0;
	for (int i = 0; i != n; ++i)
	{
		total_length += (int)chunks[i].length;
	}
	if (static_buffer_end + total_length > (int)sizeof(static_buffer))
	{
		log_warning("data length for buffer is too large: %d", static_buffer_end + total_length);
		static_buffer_end = 0;
		return 0;
	}
	for (int i = 0; i != n; ++i)
	{
		memcpy(static_buffer + (size_t)static_buffer_end, chunks[i].data, (size_t)chunks[i].length);
		static_buffer_end += (int)chunks[i].length;
	}
	return 1;
}

#define BUFFER_PUSH(...) buffer_push(sizeof((buffer_chunk[]){__VA_ARGS__}) / sizeof(buffer_chunk), (buffer_chunk[]){__VA_ARGS__})

//! Write the @p fd to the static link buffer for later use with `readlink`.
//! Includes the null terminator.
//! @param fd The file descriptor number to write.
//! @return The total length of the string in the link buffer.
static int link_buffer_store_fd(int fd)
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

//! Resolve @p fd to a file path and store it in the static buffer.
//! @param fd File descriptor to resolve.
//! @return True on success, false otherwise.
static _Bool buffer_readlink(int fd)
{
	link_buffer_store_fd(fd);
	ssize_t length = readlink(static_link_buffer, static_buffer, sizeof(static_buffer));
	if (length == -1)
	{
		log_warning("readlink on %d returned error: %s", fd, strerror(errno));
		return 0;
	}
	else if (length >= (ssize_t)sizeof(static_buffer))
	{
		log_warning("readlink on %d: file name too long", fd);
		return 0;
	}
	static_buffer_end = (int)length;
	return 1;
}

//! Get the current working directory and store it in the static buffer.
//! @return True on success, false otherwise.
static _Bool buffer_store_cwd(void)
{
	if (getcwd(static_buffer, sizeof(static_buffer)) == NULL)
	{
		log_warning("getcwd returned NULL: %s", strerror(errno));
		return 0;
	}
	static_buffer_end = (int)strlen(static_buffer);
	return 1;
}

//! Write the content of the static buffer to the output file descriptor and
//! clear the buffer.
static void buffer_record_output(void)
{
	if (write(static_output_fd, static_buffer, (size_t)static_buffer_end) == -1)
	{
		log_warning("write failed on fd %d: %s", static_output_fd, strerror(errno));
	}
	static_buffer_end = 0;
}

void record_path(const char* path)
{
	if (path == NULL)
	{
		return;
	}
	int path_length = (int)strlen(path);
	if (path[0] != '/')
	{
		if (buffer_store_cwd())
		{
			BUFFER_PUSH(
				chunk_slash,
				buffer_chunk_from(path, path_length),
				chunk_newline,
			);
			buffer_record_output();
		}
	}
}

void record_fd(int fd)
{
	if (buffer_readlink(fd))
	{
		BUFFER_PUSH(chunk_newline);
		buffer_record_output();
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
		BUFFER_PUSH(
			buffer_chunk_from(path, path_length),
			chunk_newline,
		);
		buffer_record_output();
	}
	else if (fd == AT_FDCWD)
	{
		if (buffer_store_cwd())
		{
			BUFFER_PUSH(
				chunk_slash,
				buffer_chunk_from(path, path_length),
				chunk_newline,
			);
			buffer_record_output();
		}
	}
	else if (buffer_readlink(fd))
	{
		BUFFER_PUSH(
			chunk_slash,
			buffer_chunk_from(path, path_length),
			chunk_newline,
		);
		buffer_record_output();
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
		for (const char* entry_end = path_env; *entry_end != '\0'; path_env = entry_end + 1)
		{
			entry_end = path_env;
			while (*entry_end != '\0' && *entry_end != ':')
			{
				++entry_end;
			}
			
			int length = (int)(entry_end - path_env);
			if (length > 0)
			{
				// PATH may contain relative paths.
				if (path_env[0] != '/')
				{
					if (!(buffer_store_cwd() && BUFFER_PUSH(
						chunk_slash, buffer_chunk_from(path_env, length),
						chunk_slash, buffer_chunk_from(path, path_length), chunk_zero)))
					{
						break;
					}
				}
				else if (!BUFFER_PUSH(buffer_chunk_from(path_env, length),
					chunk_slash, buffer_chunk_from(path, path_length), chunk_zero))
				{
					break;
				}
				
				// Null-terminator is not path of the path name.
				--static_buffer_end;
				if (access(static_buffer, X_OK) == 0)
				{
					// Replace null-terminator with a new line.
					static_buffer[static_buffer_end] = '\n';
					return buffer_record_output();
				}
			}
		}
	}
}
