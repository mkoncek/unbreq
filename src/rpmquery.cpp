#include "rpmquery.hpp"

#include <format>
#include <experimental/scope>
#include <cstdio>

#include <rpm/rpmds.h>
#include <rpm/rpmts.h>
#include <rpm/rpmio.h>
#include <rpm/rpmfi.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmtag.h>
#include <rpm/rpmcli.h>

static auto create_ts() -> std::experimental::unique_resource<rpmts, rpmts(*)(rpmts)>
{
	auto ts = std::experimental::make_unique_resource_checked(rpmtsCreate(), nullptr, rpmtsFree);
	if (not ts.get())
	{
		throw std::runtime_error("failed to create RPM transaction set");
	}
	return ts;
}

static auto Fclose_checked(FD_t value) -> int
{
	auto result = Fclose(value);
	if (result != 0)
	{
		std::fprintf(stderr, "fclose failed with %d", result);
	}
	return result;
}

static auto create_fd(const char* file, const char* mode) -> std::experimental::unique_resource<FD_t, int(*)(FD_t)>
{
	auto result = Fopen(file, mode);
	if (Ferror(result) != 0)
	{
		throw std::runtime_error(std::format("failed to create file descriptor: {}", Fstrerror(result)));
	}
	if (not result)
	{
		throw std::runtime_error("failed to create file descriptor");
	}
	return std::experimental::unique_resource(result, Fclose_checked);
}

static auto create_header(rpmts ts, FD_t fd) -> std::experimental::unique_resource<Header, Header(*)(Header)>
{
	Header result = nullptr;
	int rc = rpmReadPackageFile(ts, fd, nullptr, &result);
	
	switch (rc)
	{
	case RPMRC_OK: break;
	case RPMRC_NOTFOUND: throw std::runtime_error("file not found");
	case RPMRC_FAIL: throw std::runtime_error("failed to parse RPM header");
	case RPMRC_NOTTRUSTED: throw std::runtime_error("untrusted key");
	case RPMRC_NOKEY: throw std::runtime_error("public key is unavailable");
	default: throw std::runtime_error(std::format("failed to parse RPM header: unrecognized error code: {}", rc));
	}
	
	if (not result)
	{
		throw std::runtime_error("failed to create RPM header");
	}
	
	return std::experimental::unique_resource(result, headerFree);
}

static auto create_fi(rpmts ts, Header header, rpmTagVal tagN, rpmfiFlags flags) -> std::experimental::unique_resource<rpmfi, rpmfi(*)(rpmfi)>
{
	auto result = rpmfiNew(ts, header, tagN, flags);
	if (not result)
	{
		throw std::runtime_error("failed to create RPM file info");
	}
	result = rpmfiInit(result, 0);
	return std::experimental::unique_resource(result, rpmfiFree);
}

static auto create_ds(Header header, rpmTagVal tagN, int flags) -> std::experimental::unique_resource<rpmds, rpmds(*)(rpmds)>
{
	auto result = std::experimental::make_unique_resource_checked(rpmdsNew(header, tagN, flags), nullptr, rpmdsFree);
	if (not result.get())
	{
		throw std::runtime_error("failed to create RPM dependency set");
	}
	return result;
}

auto rpmquery::query_buildrequires(const char* srpm_path) -> std::vector<std::string>
try
{
	auto result = std::vector<std::string>();
	
	auto ts = create_ts();
	auto fd = create_fd(srpm_path, "r");
	rpmtsSetVSFlags(ts.get(),
		RPMVSF_NOHDRCHK | RPMVSF_NOSHA1HEADER | RPMVSF_NODSAHEADER | RPMVSF_NORSAHEADER | RPMVSF_NOMD5 | RPMVSF_NODSA | RPMVSF_NORSA
	);
	auto header = create_header(ts.get(), fd.get());
	
	auto ds = create_ds(header.get(), RPMTAG_REQUIRENAME, 0);
	
	while (rpmdsNext(ds.get()) >= 0)
	{
		auto br_name = std::string_view(rpmdsN(ds.get()));
		if (br_name.starts_with("rpmlib(") and br_name.ends_with(")"))
		{
			continue;
		}
		const char* br = rpmdsDNEVR(ds.get());
		if (not br)
		{
			throw std::runtime_error("rpmdsDNEVR returned null");
		}
		result.emplace_back(br + 2);
	}
	
	return result;
}
catch (std::exception& ex)
{
	throw std::runtime_error(std::format("when querying BuildRequires of {}: {}", srpm_path, ex.what()));
}

void rpmquery::init_cli()
{
	if (auto rc = rpmReadConfigFiles(nullptr, nullptr); rc != 0)
	{
		throw std::runtime_error("rpmReadConfigFiles failed");
	}
}

auto rpmquery::query_files(std::string_view rpm) -> std::vector<std::string>
try
{
	auto ts = create_ts();
	auto it = std::experimental::make_unique_resource_checked(rpmtsInitIterator(ts.get(), RPMDBI_LABEL, rpm.data(), rpm.size()), nullptr, rpmdbFreeIterator);
	if (not it.get())
	{
		throw std::runtime_error("rpmtsInitIterator failed, package may not be installed");
	}
	auto result = std::vector<std::string>();
	while (auto header = rpmdbNextIterator(it.get()))
	{
		auto fi = create_fi(ts.get(), header, RPMTAG_BASENAMES, RPMFI_NOHEADER | RPMFI_FLAGS_QUERY | RPMFI_NOFILEDIGESTS);
		while (rpmfiNext(fi.get()) >= 0)
		{
			result.emplace_back(rpmfiFN(fi.get()));
		}
	}
	
	return result;
}
catch (std::exception& ex)
{
	throw std::runtime_error(std::format("when querying files of {}: {}", rpm, ex.what()));
}
