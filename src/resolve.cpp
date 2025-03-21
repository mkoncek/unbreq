#include <iostream>
#include <memory>
#include <string_view>
#include <ranges>

#include <experimental/scope>

#include <sys/mman.h>
#include <sys/stat.h>

#include <libdnf5/base/base.hpp>
#include <libdnf5/base/goal.hpp>
#include <libdnf5/logger/null_logger.hpp>
#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/utils/patterns.hpp>
#include <libdnf5-cli/output/adapters/transaction.hpp>
#include <libdnf5-cli/output/transaction_table.hpp>

#include "rpmquery.hpp"

auto query_whatprovides(libdnf5::Base& base, const std::vector<std::string>& values)
-> libdnf5::rpm::PackageQuery
{
	auto result_query = libdnf5::rpm::PackageQuery(base, libdnf5::sack::ExcludeFlags::IGNORE_VERSIONLOCK, true);
	auto provides_query = libdnf5::rpm::PackageQuery(base, libdnf5::sack::ExcludeFlags::IGNORE_VERSIONLOCK, false);
	provides_query.filter_provides(values, libdnf5::sack::QueryCmp::GLOB);
	
	auto file_patterns = std::vector<std::string>();
	for (const auto& capability : values)
	{
		if (libdnf5::utils::is_file_pattern(capability))
		{
			file_patterns.push_back(capability);
		}
	}
	result_query = provides_query;
	if (not file_patterns.empty())
	{
		result_query.filter_file(file_patterns, libdnf5::sack::QueryCmp::GLOB);
		result_query |= provides_query;
	} else
	{
		result_query = provides_query;
	}
	
	return result_query;
}

auto query_remove(libdnf5::Base& base, const std::vector<std::string>& values)
-> libdnf5::base::Transaction
{
	auto goal = libdnf5::Goal(base);
	auto settings = libdnf5::GoalJobSettings();
	settings.set_with_nevra(true);
	settings.set_with_provides(false);
	settings.set_with_filenames(false);
	settings.set_with_binaries(false);
	for (const auto& value : values)
	{
		goal.add_remove(value, settings);
	}
    goal.set_allow_erasing(true);
	return goal.resolve();
}

//! Sorted vector of file names as well as the owner of the mmapping
struct Accessed_files : protected std::vector<std::string_view>
{
	static void drop(std::span<const char> value)
	{
		if (munmap(const_cast<char*>(value.data()), value.size()))
		{
			std::clog << std::format("munmap failed: {}", std::strerror(errno)) << "\n";
		}
	}
	
	std::experimental::unique_resource<std::span<const char>, decltype(&drop)> memory_;
	
	Accessed_files(int fd)
	{
		struct stat st {};
		if (fstat(fd, &st))
		{
			throw std::runtime_error(std::format("fstat failed: {}", std::strerror(errno)));
		}
		
		auto size = st.st_size;
		auto memory = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
		if (memory == MAP_FAILED)
		{
			throw std::runtime_error(std::format("mmap failed: {}", std::strerror(errno)));
		}
		
		auto span = std::span(static_cast<const char*>(memory), size);
		
		{
			std::size_t begin = 0;
			for (std::size_t i = 0; i != span.size(); ++i)
			{
				if (span[i] == '\n')
				{
					this->emplace_back(&span[begin], &span[i]);
					begin = i + 1;
				}
			}
		}
		
		if (not std::ranges::is_sorted(static_cast<const std::vector<std::string_view>&>(*this)))
		{
			throw std::runtime_error(std::format("accessed files are not sorted"));
		}
		
		memory_ = std::experimental::unique_resource(span, &drop);
	}
	
	bool contains(std::string_view path) const
	{
		return std::ranges::binary_search(static_cast<const std::vector<std::string_view>&>(*this), path);
	}
};

int main(int argc, const char** argv)
{
	if (argc < 2)
	{
		throw std::runtime_error(std::format("missing argument: accessed files file descriptor"));
	}
	
	int accessed_files_fd = std::atoi(argv[1]);
	if (accessed_files_fd == 0)
	{
		throw std::runtime_error(std::format("invalid argument #1, expected a nonzero numeric file descriptor, received: {}", argv[1]));
	}
	auto accessed_files = Accessed_files(accessed_files_fd);
	
	if (argc < 3)
	{
		throw std::runtime_error(std::format("missing argument: SRPM directory path"));
	}
	auto srpms = std::vector<std::string>();
	
	{
		auto srpm_dir = std::filesystem::directory_iterator(argv[2]);
		for (const auto& file : srpm_dir)
		{
			if (not file.path().native().ends_with(".src.rpm"))
			{
				std::clog << std::format("suspicious file found in the SRPM directory: {}", file.path().native()) << "\n";
			}
			srpms.emplace_back(file.path().native());
		}
	}
	
	if (srpms.empty())
	{
		throw std::runtime_error(std::format("no file found in the SRPM directory: {}", argv[2]));
	}
	
	auto installroot = std::optional<const char*>();
	if (argc >= 4)
	{
		installroot.emplace(argv[3]);
	}
	
	rpmquery::init_cli();
	
	auto base = libdnf5::Base();
	base.load_config();
	
	// --installroot
	if (installroot.has_value())
	{
		base.get_config().get_installroot_option().set(*installroot);
	}
	base.setup();
	
	// --installed
	base.get_repo_sack()->load_repos(libdnf5::repo::Repo::Type::SYSTEM);
	
	auto br_providers = std::map<std::string, std::vector<std::string>, std::less<>>();
	auto rev_br_providers = std::map<std::string, std::vector<std::string>>();
	
	for (const auto& srpm : srpms)
	{
		for (auto& br : rpmquery::query_buildrequires(srpm.c_str(), installroot))
		{
			auto br_list = std::vector<std::string> {br};
			auto& ref_br_providers = br_providers[br];
			for (const auto& provider : query_whatprovides(base, br_list))
			{
				ref_br_providers.emplace_back(provider.get_nevra());
				rev_br_providers[provider.get_nevra()].emplace_back(br);
			}
		}
	}
	
	{
		auto sorted_br_providers = std::vector<std::pair<std::string_view, std::size_t>>();
		sorted_br_providers.reserve(br_providers.size());
		for (const auto& kv : br_providers)
		{
			sorted_br_providers.emplace_back(kv.first, kv.second.size());
			std::ranges::push_heap(sorted_br_providers, {}, &std::pair<std::string_view, std::size_t>::second);
		}
		std::ranges::sort_heap(sorted_br_providers, {}, &std::pair<std::string_view, std::size_t>::second);
		
		for (std::string_view br : std::views::keys(sorted_br_providers))
		{
			auto& providers = br_providers.find(br)->second;
			if (providers.size() == 1)
			{
				for (const auto& rev_br : rev_br_providers.find(providers.front())->second)
				{
					if (rev_br != br)
					{
						auto& br_provides_rev = br_providers.find(rev_br)->second;
						std::erase(br_provides_rev, rev_br);
					}
				}
			}
		}
	}
	
	base.get_config().get_assumeno_option().set(true);
	
	auto brs_can_be_removed = std::vector<std::string>();
	auto rpms_can_be_removed = std::vector<std::string>();
	for (const auto& [br, providers] : br_providers)
	{
		auto rpms_size = rpms_can_be_removed.size();
		for (const auto& provider : providers)
		{
			rpms_can_be_removed.emplace_back(provider);
		}
		bool can_be_removed = true;
		auto tr_removed_packages = query_remove(base, rpms_can_be_removed);
		for (auto&& tr_package : tr_removed_packages.get_transaction_packages())
		{
			auto&& package = tr_package.get_package();
			for (auto&& path : package.get_files())
			{
				if (accessed_files.contains(path))
				{
					can_be_removed = false;
					break;
				}
			}
			if (not can_be_removed)
			{
				break;
			}
		}
		if (not can_be_removed)
		{
			rpms_can_be_removed.resize(rpms_size);
		}
		else
		{
			brs_can_be_removed.emplace_back(br);
		}
	}
	
	for (const auto& br : brs_can_be_removed)
	{
		std::cout << br << "\n";
	}
}
