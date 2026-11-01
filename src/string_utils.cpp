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

#include "mpv_rich_presence/string_utils.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

std::string to_lower(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return str;
}

std::string truncate_utf8_bytes(std::string_view text, std::size_t limit)
{
    if (text.size() <= limit)
        return std::string(text);
    constexpr std::string_view suffix = "...";
    if (limit < suffix.size())
        return std::string(limit, '.');
    std::size_t cut = limit - suffix.size();
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
    {
        --cut;
    }
    return std::string(text.substr(0, cut)) + std::string(suffix);
}

uint64_t compute_fnv1a64(const char* data, std::size_t len)
{
    uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < len; ++i)
    {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string normalize_path(const std::string& path_str)
{
    if (path_str.empty())
        return {};

    std::error_code ec;
    fs::path p = fs::weakly_canonical(path_str, ec);
    if (ec)
        p = fs::path(path_str).lexically_normal();

    std::string result = p.generic_string();

#if defined(_WIN32) || defined(__CYGWIN__)
    result = to_lower(result);
#endif

    if (!result.empty() && result.back() == '/')
        result.pop_back();

    return result;
}

std::vector<std::string> parse_folder_list(const std::string& raw)
{
    std::vector<std::string> result;
    std::string clean_raw = raw;
    auto g_start = clean_raw.find_first_not_of(" \t");
    auto g_end = clean_raw.find_last_not_of(" \t");
    if (g_start != std::string::npos && g_end != std::string::npos)
        clean_raw = clean_raw.substr(g_start, g_end - g_start + 1);
    if (clean_raw.size() >= 2 && ((clean_raw.front() == '"' && clean_raw.back() == '"') || (clean_raw.front() == '\'' && clean_raw.back() == '\'')))
        clean_raw = clean_raw.substr(1, clean_raw.size() - 2);

    std::istringstream ss(clean_raw);
    std::string item;
    while (std::getline(ss, item, ';'))
    {
        auto start = item.find_first_not_of(" \t\"'");
        auto end = item.find_last_not_of(" \t\"'");
        if (start != std::string::npos && end != std::string::npos)
        {
            std::string trimmed = item.substr(start, end - start + 1);
            if (!trimmed.empty())
                result.push_back(normalize_path(trimmed));
        }
    }
    return result;
}

std::string format_time_duration(double pos_s, double total_s)
{
    int pos = static_cast<int>(pos_s);
    if (pos < 0) pos = 0;
    int pos_h = pos / 3600;
    int pos_m = (pos % 3600) / 60;
    int pos_sec = pos % 60;

    if (total_s <= 0.0)
    {
        if (pos_h > 0)
            return std::format("{:02d}:{:02d}:{:02d} / Live", pos_h, pos_m, pos_sec);
        return std::format("{:02d}:{:02d} / Live", pos_m, pos_sec);
    }

    int total = static_cast<int>(total_s);
    if (total < 0) total = 0;
    int tot_h = total / 3600;
    int tot_m = (total % 3600) / 60;
    int tot_sec = total % 60;

    if (tot_h > 0 || pos_h > 0)
    {
        return std::format("{:02d}:{:02d}:{:02d} / {:02d}:{:02d}:{:02d}",
            pos_h, pos_m, pos_sec, tot_h, tot_m, tot_sec);
    }
    return std::format("{:02d}:{:02d} / {:02d}:{:02d}", pos_m, pos_sec, tot_m, tot_sec);
}

bool passes_folder_filter(const std::string& file_path, const std::string& mode, const std::vector<std::string>& folders)
{
    if (mode == "disabled")
        return true;

    if (mode == "whitelist")
    {
        if (folders.empty())
            return false;
    }
    else if (mode == "blacklist")
    {
        if (folders.empty())
            return true;
    }
    else
    {
        return true;
    }

    if (file_path.empty())
        return (mode == "blacklist");

    std::string normalized = normalize_path(file_path);

    bool in_list = false;
    for (const auto& folder : folders)
    {
        if (normalized.starts_with(folder + "/") || normalized == folder)
        {
            in_list = true;
            break;
        }
    }

    if (mode == "whitelist")
        return in_list;
    else
        return !in_list;
}
