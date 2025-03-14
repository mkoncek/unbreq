#include <fcntl.h>
#include <unistd.h>
#include <sys/fanotify.h>
#include <sys/stat.h>
#include <sys/epoll.h>

#include <algorithm>
#include <charconv>
#include <iostream>
#include <array>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <climits>
#include <set>
#include <string>
#include <format>
#include <experimental/scope>

static void checked_close(int fd)
{
	if (close(fd))
	{
		std::clog << "[WARN] failed to close file descriptor " << fd << ": " << std::strerror(errno) << "\n";
	}
}

struct Unbreq
{
	std::string mock_root_;
	std::experimental::unique_resource<int, void(*)(int)> fanotify_fd_;
	std::experimental::unique_resource<int, void(*)(int)> epoll_fd_;
	std::experimental::unique_resource<int, void(*)(int)> mount_fd_;
	alignas(fanotify_event_metadata) std::array<unsigned char, sizeof(fanotify_event_metadata) * 32> data_;
	int out_fd_;
	
	Unbreq(std::string mock_root, int out_fd)
		:
		mock_root_(std::move(mock_root)),
		data_(),
		out_fd_(out_fd)
	{
		auto fanotify_fd = std::experimental::make_unique_resource_checked(fanotify_init(
			FAN_CLASS_NOTIF | FAN_CLOEXEC | FAN_REPORT_DFID_NAME | FAN_NONBLOCK,
			O_RDONLY | O_LARGEFILE
		), -1, &checked_close);
		if (fanotify_fd.get() == -1)
		{
			throw std::runtime_error(std::format("fanotify_init failed: {}", std::strerror(errno)));
		}
		
		if (fanotify_mark(fanotify_fd.get(),
			FAN_MARK_ADD | FAN_MARK_FILESYSTEM,
			FAN_ACCESS | FAN_MODIFY | FAN_CLOSE_WRITE | FAN_CLOSE_NOWRITE | FAN_OPEN | FAN_OPEN_EXEC,
			AT_FDCWD,
			mock_root_.c_str()) == -1)
		{
			throw std::runtime_error(std::format("fanotify_mark failed: {}", std::strerror(errno)));
		}
		
		auto epoll_fd = std::experimental::make_unique_resource_checked(epoll_create1(0), -1, &checked_close);
		if (epoll_fd.get() == -1)
		{
			throw std::runtime_error(std::format("failed to create epoll file descriptor: {}", std::strerror(errno)));
		}
		
		{
			auto event = epoll_event();
			event.events = EPOLLIN;
			
			event.data.fd = 0;
			if (epoll_ctl(epoll_fd.get(), EPOLL_CTL_ADD, 0, &event))
			{
				throw std::runtime_error(std::format("failed to add standard input file descriptor to epoll: {}", std::strerror(errno)));
			}
			
			event.data.fd = fanotify_fd.get();
			if (epoll_ctl(epoll_fd.get(), EPOLL_CTL_ADD, fanotify_fd.get(), &event))
			{
				throw std::runtime_error(std::format("failed to add fanotify file descriptor {} to epoll: {}", fanotify_fd.get(), std::strerror(errno)));
			}
		}
		
		if (fcntl(0, F_SETFL, O_NONBLOCK))
		{
			throw std::runtime_error(std::format("failed to set non-blocking mode for the standard input stream: {}", std::strerror(errno)));
		}
		
		auto mount_fd = std::experimental::make_unique_resource_checked(open(mock_root_.c_str(), O_DIRECTORY | O_RDONLY), -1, &checked_close);
		if (mount_fd.get() == -1)
		{
			throw std::runtime_error(std::format("open of {} failed: {}", mock_root_, std::strerror(errno)));
		}
		
		fanotify_fd_ = std::move(fanotify_fd);
		epoll_fd_ = std::move(epoll_fd);
		mount_fd_ = std::move(mount_fd);
	}
	
	void run(std::set<std::string>& names)
	{
		auto events = std::array<epoll_event, 8>();
		bool keep_running = true;
		
		std::clog << "[INFO] fanotify running..." << "\n";
		
		while (keep_running)
		{
			auto ready_events = epoll_wait(epoll_fd_.get(), events.data(), events.size(), -1);
			
			for (int i = 0; i != ready_events; ++i)
			{
				auto fd = events[i].data.fd;
				
				if (fd == 0)
				{
					keep_running = false;
					break;
				}
				else
				{
					auto len = read(fd, std::data(data_), std::span(data_).size_bytes());
					if (len == -1)
					{
						throw std::runtime_error(std::format("read of {} failed: {}", fd, std::strerror(errno)));
					}
					
					if (len <= 0)
					{
						return;
					}
					
					auto procfd_path = std::array<char, PATH_MAX>();
					const auto procfd_end = std::ranges::copy(std::string_view("/proc/self/fd/"), std::data(procfd_path)).out;
					
					for (fanotify_event_metadata* const metadata = std::bit_cast<fanotify_event_metadata*>(std::data(data_));
						FAN_EVENT_OK(metadata, len);
						std::copy(reinterpret_cast<unsigned char*>(FAN_EVENT_NEXT(metadata, len)), std::begin(data_) + len, std::begin(data_)))
					{
						if (metadata->vers != FANOTIFY_METADATA_VERSION) [[unlikely]]
						{
							throw std::format("mismatch of fanotify metadata version, expected: {}, found: {}", FANOTIFY_METADATA_VERSION, metadata->vers);
						}
						
						auto fid = std::bit_cast<fanotify_event_info_fid*>(metadata + 1);
						auto file_name = std::string_view();
						auto file_handle = std::bit_cast<struct file_handle*>(&*fid->handle);
						
						if (fid->hdr.info_type == FAN_EVENT_INFO_TYPE_FID)
						{
						}
						else if (fid->hdr.info_type == FAN_EVENT_INFO_TYPE_DFID)
						{
						}
						else if (fid->hdr.info_type == FAN_EVENT_INFO_TYPE_DFID_NAME)
						{
							file_name = std::bit_cast<const char*>(&*file_handle->f_handle + file_handle->handle_bytes);
						}
						else
						{
							throw std::format("received unexpected event info type");
						}
						
						auto event_fd = open_by_handle_at(mount_fd_.get(), file_handle, O_RDONLY);
						if (event_fd == -1)
						{
							// File handle is no longer valid, we do not care
							if (errno == ESTALE)
							{
								continue;
							}
							else
							{
								throw std::format("open_by_handle_at failed: {}", std::strerror(errno));
							}
						}
						auto event_fd_owner = std::experimental::unique_resource(event_fd, &checked_close);
						
						*std::to_chars(procfd_end, std::end(procfd_path), event_fd).ptr = '\0';
						auto path_buf = std::array<char, PATH_MAX>();
						auto path_len = readlink(std::data(procfd_path), std::data(path_buf), std::size(path_buf) - 1);
						if (path_len == -1)
						{
							throw std::format("readlink of {} failed: {}", std::string_view(std::data(procfd_path), procfd_end), std::strerror(errno));
						}
						
						auto path = std::string_view(std::data(path_buf), path_len);
						
						if (path.starts_with(mock_root_))
						{
							auto name = std::string();
							name.reserve(path.size() - mock_root_.size() + 1 + file_name.size() + 1 + 1);
							name.append(path.substr(mock_root_.size())).append("/").append(file_name).append("\n");
							auto [it, inserted] = names.insert(name);
							if (inserted)
							{
								if (write(out_fd_, it->c_str(), it->size()) == -1)
								{
									std::clog << std::format("[ERROR] when writing to file descrptor {}: {}", out_fd_, std::strerror(errno)) << "\n";
								}
							}
						}
					}
				}
			}
		}
	}
};

int main([[maybe_unused]] int argc, [[maybe_unused]] const char** argv)
{
	if (argc < 2)
	{
		std::clog << "[ERROR] mock root path argument required" << "\n";
		return 1;
	}
	
	int out_fd = 1;
	
	if (argc == 3)
	{
		out_fd = std::atoi(argv[2]);
	}
	
	auto unbreq = Unbreq(argv[1], out_fd);
	auto names = std::set<std::string>();
	
	unbreq.run(names);
	fsync(out_fd);
}
