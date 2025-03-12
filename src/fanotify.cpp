#include <algorithm>
#include <charconv>
#include <iostream>
#include <array>
#include <cstring>
#include <cerrno>
#include <set>
#include <string>
#include <thread>
#include <format>
#include <experimental/scope>

#include <fcntl.h>
#include <unistd.h>
#include <climits>
#include <sys/fanotify.h>
#include <sys/stat.h>

static void checked_close(int fd)
{
	if (close(fd))
	{
		std::clog << "[WARN] failed to close file descriptor " << fd << ": " << std::strerror(errno) << "\n";
	}
}

void run_fanotify(std::stop_token stop_token, std::string& error, std::set<std::string>& names, int fd, const std::string& mock_root) noexcept
try
{
	alignas(fanotify_event_metadata) auto buf = std::array<unsigned char, sizeof(fanotify_event_metadata) * 32>();
	
	std::clog << "[INFO] fanotify running..." << "\n";
	
	while (not stop_token.stop_requested())
	{
		auto len = read(fd, std::data(buf), std::size(buf));
		if (len == -1)
		{
			throw std::format("read of {} failed: {}", fd, std::strerror(errno));
		}
		
		if (len <= 0)
		{
			continue;
		}
		
		auto mount_fd = std::experimental::make_unique_resource_checked(open(mock_root.c_str(), O_DIRECTORY | O_RDONLY), -1, &checked_close);
		if (mount_fd.get() == -1)
		{
			throw std::format("open {} failed: {}", "\".\"", std::strerror(errno));
		}
		
		auto procfd_path = std::array<char, PATH_MAX>();
		const auto procfd_end = std::ranges::copy(std::string_view("/proc/self/fd/"), std::data(procfd_path)).out;
		
		for (fanotify_event_metadata* const metadata = std::bit_cast<fanotify_event_metadata*>(std::data(buf));
			not stop_token.stop_requested() && FAN_EVENT_OK(metadata, len);
			std::copy(reinterpret_cast<unsigned char*>(FAN_EVENT_NEXT(metadata, len)), std::begin(buf) + len, std::begin(buf)))
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
			
			auto event_fd = open_by_handle_at(mount_fd.get(), file_handle, O_RDONLY);
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
			
			
			if (path.starts_with(mock_root))
			{
				std::clog << "[DEBUG] file access: " << path << "\n";
				auto name = std::string();
				name.reserve(path.size() - mock_root.size() + 1 + file_name.size() + 1);
				name.append(path.substr(mock_root.size())).append("/").append(file_name);
				names.insert(name);
			}
		}
	}
}
catch (std::string& ex)
{
	error = std::move(ex);
}

struct Fanotify : std::experimental::unique_resource<int, void(*)(int)>
{
	Fanotify(unsigned int flags, unsigned int event_flags)
		:
		std::experimental::unique_resource<int, void(*)(int)>::unique_resource(
			std::experimental::make_unique_resource_checked(fanotify_init(flags, event_flags), -1, &checked_close)
		)
	{
		if (get() == -1)
		{
			throw std::runtime_error(std::format("fanotify_init failed: {}", std::strerror(errno)));
		}
	}
};

int main([[maybe_unused]] int argc, [[maybe_unused]] const char** argv)
{
	if (argc != 2)
	{
		std::clog << "mock root path argument required" << "\n";
		return 1;
	}
	
	auto mock_root = std::string(argv[1]);
	
	auto fanotify = Fanotify(
		FAN_CLASS_NOTIF | FAN_CLOEXEC | FAN_REPORT_DFID_NAME /* | FAN_NONBLOCK */,
		O_RDONLY | O_LARGEFILE
	);
	
	if (fanotify_mark(
		fanotify.get(),
		FAN_MARK_ADD | FAN_MARK_FILESYSTEM,
		FAN_ACCESS | FAN_MODIFY | FAN_CLOSE_WRITE | FAN_CLOSE_NOWRITE | FAN_OPEN | FAN_OPEN_EXEC,
		AT_FDCWD,
		mock_root.c_str()) == -1)
	{
		std::clog << "fanotify_mark: " << std::strerror(errno) << "\n";
		return 1;
	}
	
	auto names = std::set<std::string>();
	
	{
		auto error = std::string();
		auto thread = std::jthread(run_fanotify, std::ref(error), std::ref(names), fanotify.get(), std::cref(mock_root));
		std::cin.get();
		if (not error.empty())
		{
			std::clog << "an error occured: " << error << "\n";
		}
	}
	
	for (const auto& name : names)
	{
		std::cout << name << "\n";
	}
}
