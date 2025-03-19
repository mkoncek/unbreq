#pragma once

#include <experimental/scope>
#include <unistd.h>
#include <iostream>
#include <iostream>
#include <cstring>
#include <cerrno>

inline void checked_close(int fd)
{
	if (close(fd))
	{
		std::clog << "[WARN] failed to close file descriptor " << fd << ": " << std::strerror(errno) << "\n";
	}
}
