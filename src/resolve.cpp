#include <rpm/rpmds.h>
#include <rpm/rpmts.h>
#include <rpm/rpmio.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmlog.h>

#include <rpm/rpmspec.h>

#include <fcntl.h>

#include <fstream>
#include <iostream>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <map>
#include <vector>
#include <variant>
#include <span>

#include "shared.hpp"

struct File_desctiptor
{
	FD_t fd_;
	
	~File_desctiptor()
	{
		Fclose(fd_);
	}
	
	static File_desctiptor fopen(const char* filename, const char* mode)
	{
		auto result = Fopen(filename, mode);
		if (Ferror(result) != 0)
		{
			throw std::runtime_error(Fstrerror(result));
		}
		return File_desctiptor(result);
	}
	
	operator FD_t()
	{
		return fd_;
	}
};

struct RPM_transaction
{
	rpmts ts_;
	
	~RPM_transaction()
	{
		rpmtsFree(ts_);
	}
	
	static RPM_transaction create()
	{
		return RPM_transaction(rpmtsCreate());
	}
	
	operator rpmts()
	{
		return ts_;
	}
};

struct RPM_dependency_set
{
	rpmds ds_;
	
	~RPM_dependency_set()
	{
		rpmdsFree(ds_);
	}
	
	static RPM_dependency_set create(Header h, rpmTagVal tagN, int flags)
	{
		return RPM_dependency_set(rpmdsNew(h, tagN, flags));
	}
	
	operator rpmds()
	{
		return ds_;
	}
};

struct String_vec : std::variant<std::string, std::vector<std::string>>
{
	std::span<const std::string> span() const noexcept
	{
		if (auto* string = std::get_if<std::string>(this))
		{
			return std::span(string, 1);
		}
		else
		{
			return std::get<std::vector<std::string>>(*this);
		}
	}
	
	void push_back(std::string string)
	{
		if (auto* this_string = std::get_if<std::string>(this))
		{
			auto first = std::move(*this_string);
			auto self_vec = this->emplace<std::vector<std::string>>(2);
			self_vec[0] = std::move(first);
			self_vec[1] = std::move(string);
		}
		else
		{
			auto& self_vec = std::get<std::vector<std::string>>(*this);
			if (self_vec.empty())
			{
				this->emplace<std::string>(std::move(string));
			}
			else
			{
				self_vec.push_back(std::move(string));
			}
		}
	}
};

using Files_map = std::map<std::string_view, std::vector<std::string>>;
using Whatprovides_map = std::map<std::string, std::vector<Files_map::iterator>>;

static std::string_view static_current_build_require;
static auto static_whatprovides = Whatprovides_map();
static auto static_files = Files_map();
static auto static_files_it = Files_map::iterator();

static int callback_package_list([[maybe_unused]] QVA_t qva, rpmts ts, Header h)
{
	auto fi = rpmfiNew(ts, h, RPMTAG_BASENAMES, RPMFI_NOHEADER | RPMFI_FLAGS_QUERY | RPMFI_NOFILEDIGESTS);
	fi = rpmfiInit(fi, 0);
	while (rpmfiNext(fi) >= 0)
	{
		static_files_it->second.emplace_back(rpmfiFN(fi));
	}
	rpmfiFree(fi);
	return 0;
}

static int callback_what_provides([[maybe_unused]] QVA_t qva, rpmts ts, Header h)
{
	auto td = rpmtd_s();
	if (headerGet(h, RPMTAG_NVRA, &td, HEADERGET_EXT) == 0)
	{
		std::clog << "no nvra" << "\n";
		return 1;
	}
	for (char* nvra; nvra = const_cast<char*>(rpmtdNextString(&td)), nvra;)
	{
		auto [it, inserted] = static_files.try_emplace(std::string_view(nvra));
		static_whatprovides.try_emplace(std::string(static_current_build_require)).first->second.emplace_back(it);
		if (not inserted)
		{
			free(nvra);
			continue;
		}
		rpmQVKArguments_s args = {
			.qva_source = RPMQV_PACKAGE,
			.qva_sourceCount = 0,
			.qva_flags = QUERY_FOR_LIST,
			.qva_incattr = RPMFILE_NONE,
			.qva_excattr = RPMFILE_NONE,
			.qva_ofvattr = RPMVERIFY_NONE,
			// .qva_showPackage = nullptr,
			.qva_showPackage = &callback_package_list,
			.qva_specQuery = nullptr,
			.qva_queryFormat = nullptr,
			.qva_mode = 'q',
		};
		char* const argv[] = {nvra, nullptr};
		
		static_files_it = it;
		int result = rpmcliQuery(ts, &args, argv);
		static_files_it = {};
		if (result != 0)
		{
			return result;
		}
	}
	return 0;
}

int main()
{
	if (auto error = rpmReadConfigFiles(nullptr, nullptr); error != 0)
	{
		std::clog << "rpm error rpmReadConfigFiles: " << error << "\n";
	}
	
	auto accessed_files = std::set<std::string>();
	
	{
		auto ifs = std::ifstream(static_accessed_files);
		while (ifs)
		{
			auto opened_file = std::string();
			std::getline(ifs, opened_file);
			accessed_files.insert(opened_file);
		}
	}
	
	// auto it = std::filesystem::directory_iterator("/builddir/build/SRPMS");
	auto srpm = *std::filesystem::directory_iterator("/var/lib/mock/fedora-rawhide-x86_64/root/builddir/build/SRPMS");
	
	{
		auto ts = RPM_transaction::create();
		auto fd = File_desctiptor::fopen(srpm.path().c_str(), "r");
		rpmtsSetVSFlags(ts, RPMVSF_NOHDRCHK | RPMVSF_NOSHA1HEADER | RPMVSF_NODSAHEADER | RPMVSF_NORSAHEADER | RPMVSF_NOMD5 | RPMVSF_NODSA | RPMVSF_NORSA);
		Header h = nullptr;
		int rc = rpmReadPackageFile(ts, fd, nullptr, &h);
		if (rc == RPMRC_NOTFOUND)
		{
			throw std::runtime_error("not an RPM file");
		}
		if (rc != RPMRC_OK && rc != RPMRC_NOTTRUSTED && rc != RPMRC_NOKEY)
		{
			throw std::runtime_error("failed to parse RPM header");
		}
		
		{
			auto ds = RPM_dependency_set::create(h, RPMTAG_REQUIRENAME, 0);
			while (rpmdsNext(ds) >= 0)
			{
				auto ts = RPM_transaction::create();
				if (auto error = rpmtsSetRootDir(ts, "/var/lib/mock/fedora-rawhide-x86_64/root"); error != 0)
				{
					std::clog << "rpm error rpmtsSetRootDir: " << error << "\n";
				}
				
				rpmQVKArguments_s args = {
					.qva_source = RPMQV_WHATPROVIDES,
					.qva_sourceCount = 1,
					.qva_flags = QUERY_FOR_DEFAULT,
					.qva_incattr = RPMFILE_NONE,
					.qva_excattr = RPMFILE_NONE,
					.qva_ofvattr = RPMVERIFY_NONE,
					.qva_showPackage = &callback_what_provides,
					.qva_specQuery = nullptr,
					.qva_queryFormat = nullptr,
					.qva_mode = 'q',
				};
				char* const argv[] = {const_cast<char*>(rpmdsN(ds)), nullptr};
				
				if (static_current_build_require = *argv,
					static_current_build_require.starts_with("rpmlib(") && static_current_build_require.ends_with(")"))
				{
					continue;
				}
				
				if (auto error = rpmcliQuery(ts, &args, argv); error != 0)
				{
					std::clog << "rpm error rpmcliQuery: " << error << "\n";
				}
				free(args.qva_queryFormat);
			}
		}
		
		headerFree(h);
	}
	
	for (const auto& provides : static_whatprovides)
	{
		bool is_needed = false;
		
		for (const auto& it : provides.second)
		{
			// HACK we should inspect the transitive dependencies???
			if (it->second.empty())
			{
				is_needed = true;
				break;;
			}
			
			for (const auto& filename : it->second)
			{
				if (accessed_files.contains(filename))
				{
					is_needed = true;
					break;
				}
			}
			
			if (is_needed)
			{
				break;
			}
		}
		
		if (not is_needed)
		{
			rpmlog(RPMLOG_WARNING, "BuildRequires %s is not needed\n", provides.first.c_str());
		}
	}
	
	for (auto& entry : static_files)
	{
		std::cout << entry.first << "\n";
		for (auto& second : entry.second)
		{
			std::cout << "\t" << second << "\n";
		}
	}
	
	std::cout << "#######" << "\n";
	
	for (auto& entry : static_whatprovides)
	{
		std::cout << entry.first << "\n";
		for (auto second : entry.second)
		{
			std::cout << "\t" << second->first << "\n";
		}
	}
	
	for (auto& entry : static_files)
	{
		free(const_cast<char*>(entry.first.data()));
	}
}
