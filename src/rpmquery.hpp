#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace rpmquery
{
void init_cli();

auto query_buildrequires(const char* srpm_path, std::optional<const char*> root = {}) -> std::vector<std::string>;

//! Requires that rpmReadConfigFiles has already been called
auto query_files(std::string_view rpm, std::optional<const char*> root = {}) -> std::vector<std::string>;
} // rpmquery
