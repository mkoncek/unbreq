#include <fcntl.h>
#include <unistd.h>
#include <sys/fanotify.h>
#include <sys/stat.h>
#include <sys/epoll.h>

#include <algorithm>
#include <span>
#include <regex>
#include <vector>
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
#include <experimental/array>

static void checked_close(int fd)
{
	if (close(fd))
	{
		std::clog << "[WARN] failed to close file descriptor " << fd << ": " << std::strerror(errno) << "\n";
	}
}

struct Arguments
{
	std::string root_path_;
	int output_fd_ = 1;
	std::vector<std::regex> exclude_accessed_files_;
	
	static Arguments parse(int argc, const char** argv)
	{
		auto result = Arguments();
		
		if (argc < 2)
		{
			throw std::invalid_argument("missing argument #1: root path");
		}
		
		result.root_path_ = argv[1];
		
		if (argc >= 3)
		{
			result.output_fd_ = std::atoi(argv[2]);
			
			if (result.output_fd_ == 0)
			{
				throw std::invalid_argument(std::format("invalid argument #2: expected a nonzero integer, got: {}", argv[2]));
			}
		}
		
		constexpr auto key_args = std::experimental::make_array<std::string_view>(
			"-e"
		);
		auto last_arg = std::optional<std::string_view>();
		for (int i = 3; i != argc; ++i)
		{
			auto arg = std::string_view(argv[i]);
			if (std::ranges::find(key_args, arg) != std::end(key_args))
			{
				last_arg.emplace(arg);
				continue;
			}
			if (last_arg == "-e")
			{
				last_arg.reset();
				result.exclude_accessed_files_.emplace_back(arg.data(), arg.size(), std::regex_constants::extended);
			}
		}
		
		if (last_arg.has_value())
		{
			throw std::invalid_argument(std::format("missing value for argument #{}: {}", argc, *last_arg));
		}
		
		return result;
	}
};

struct Unbreq
{
	std::experimental::unique_resource<int, void(*)(int)> fanotify_fd_;
	std::experimental::unique_resource<int, void(*)(int)> epoll_fd_;
	std::experimental::unique_resource<int, void(*)(int)> mount_fd_;
	alignas(fanotify_event_metadata) std::array<unsigned char, sizeof(fanotify_event_metadata) * 32> data_;
	
	Unbreq(const Arguments& args)
		:
		data_()
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
			args.root_path_.c_str()) == -1)
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
		
		auto mount_fd = std::experimental::make_unique_resource_checked(open(args.root_path_.c_str(), O_DIRECTORY | O_RDONLY), -1, &checked_close);
		if (mount_fd.get() == -1)
		{
			throw std::runtime_error(std::format("open of {} failed: {}", args.root_path_, std::strerror(errno)));
		}
		
		fanotify_fd_ = std::move(fanotify_fd);
		epoll_fd_ = std::move(epoll_fd);
		mount_fd_ = std::move(mount_fd);
	}
	
	void run(const Arguments& args)
	{
		auto events = std::array<epoll_event, 8>();
		bool keep_running = true;
		auto names = std::set<std::string>();
		
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
						
						if (path.starts_with(args.root_path_))
						{
							auto name = std::string();
							name.reserve(path.size() - args.root_path_.size() + 1 + file_name.size() + 1 + 1);
							name.append(path.substr(args.root_path_.size())).append("/").append(file_name).append("\n");
							names.insert(name);
						}
					}
				}
			}
		}
		
		for (const auto& name : names)
		{
			bool skip = false;
			for (const auto& regex : args.exclude_accessed_files_)
			{
				if (std::regex_search(name, regex))
				{
					skip = true;
				}
			}
			if (skip)
			{
				continue;
			}
			
			if (write(args.output_fd_, name.c_str(), name.size()) == -1)
			{
				throw std::runtime_error(std::format("when writing to file descrptor {}: {}", args.output_fd_, std::strerror(errno)));
			}
		}
	}
};

int main(int argc, const char** argv)
{
	auto args = Arguments::parse(argc, argv);
	auto unbreq = Unbreq(args);
	
	unbreq.run(args);
	
	fsync(args.output_fd_);
	lseek(args.output_fd_, 0, SEEK_SET);
}
