// This file is part of mpv-rich-presence.
// Copyright (c) 2026 Alden Wu.
// Copyright (c) 2026 Youssef M. (Sirye8).
//
// mpv-rich-presence is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// mpv-rich-presence is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License
// for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with mpv-rich-presence. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstddef>
#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

std::string to_lower(std::string str);

std::string truncate_utf8_bytes(std::string_view text, std::size_t limit);

uint64_t compute_fnv1a64(const char* data, std::size_t len);

std::string normalize_path(const std::string& path_str);

std::vector<std::string> parse_folder_list(const std::string& raw);

std::string format_time_duration(double pos_s, double total_s);

bool passes_folder_filter(const std::string& file_path, const std::string& mode, const std::vector<std::string>& folders);
std::string path_utf8(const std::filesystem::path& path);
