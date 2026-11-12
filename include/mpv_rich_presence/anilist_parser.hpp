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

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct anilist_media
{
    int id = 0;
    std::string title; // Preferred display title
    std::vector<std::string> matching_titles; // Romaji, English, Native, and Synonyms for alias matching
    std::string romaji, english, native;
    int episodes = 0;
    int release_year = 0;
    int duration_minutes = 0;
    std::string cover_url;
    std::string format;
    int sequel_id = -1;
    bool unambiguous_sequel = false;
};

extern const std::string ANILIST_PAGE_FIELDS;

std::optional<anilist_media> parse_anilist_media(const nlohmann::json& media);

bool parse_anilist_page_response(const nlohmann::json& j, std::vector<anilist_media>& results);
