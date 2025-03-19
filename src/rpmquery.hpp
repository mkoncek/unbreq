#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rpmquery
{
void init_cli();

auto query_buildrequires(const char* srpm_path) -> std::vector<std::string>;

//! Requires that rpmReadConfigFiles has already been called
auto query_files(std::string_view rpm) -> std::vector<std::string>;
} // rpmquery
