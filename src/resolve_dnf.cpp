#include <iostream>
#include <filesystem>

#include <libdnf5/base/base.hpp>
#include <libdnf5/base/goal.hpp>
#include <libdnf5/logger/null_logger.hpp>
#include <libdnf5/rpm/package_query.hpp>
#include <libdnf5/utils/patterns.hpp>

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

auto query_remove_assumeno(libdnf5::Base& base, const std::vector<std::string>& values)
-> void
{
	auto goal = libdnf5::Goal(base);
	auto settings = libdnf5::GoalJobSettings();
	settings.set_with_nevra(true);
	settings.set_with_provides(false);
	settings.set_with_filenames(false);
	settings.set_with_binaries(false);
	for (const auto& spec : values)
	{
		goal.add_remove(spec, settings);
	}
    goal.set_allow_erasing(true);
}

int main()
{
	rpmquery::init_cli();
	
	auto base = libdnf5::Base();
	base.load_config();
	base.setup();
	
	// --installed
	base.get_repo_sack()->load_repos(libdnf5::repo::Repo::Type::SYSTEM);
	
	/*
	for (const auto& p : query_whatprovides(base, {"maven-local"}))
	{
		std::cout << p.get_nevra() << "\n";
	}
	
	for (const auto& br : rpmquery::query_buildrequires("/var/lib/mock/fedora-rawhide-x86_64/result/plexus-cipher-2.0-25.fc43.src.rpm"))
	{
		std::cout << br << "\n";
	}
	
	for (const auto& f : rpmquery::query_files("maven-3.9.6-7.fc41.noarch"))
	{
		std::cout << f << "\n";
	}
	
	for (const auto& f : rpmquery::query_files("ant"))
	{
		std::cout << f << "\n";
	}
	
	base.get_config().get_assumeno_option().set(true);
	*/
}
