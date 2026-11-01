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

#include "mpv_rich_presence/anilist_parser.hpp"

const std::string ANILIST_PAGE_FIELDS = R"(
    id
    title { romaji english native }
    synonyms
    episodes
    format
    coverImage { large }
    relations {
        edges {
            relationType(version: 2)
            node {
                id
                type
                format
                title { romaji english native }
                synonyms
            }
        }
    }
)";

std::optional<anilist_media> parse_anilist_media(const nlohmann::json& media)
{
    if (media.is_null())
        return std::nullopt;

    anilist_media result;
    result.id = media.value("id", 0);

    if (media.contains("title") && !media["title"].is_null())
    {
        if (media["title"].contains("romaji") && !media["title"]["romaji"].is_null())
        {
            std::string romaji = media["title"]["romaji"].get<std::string>();
            if (!romaji.empty())
            {
                result.title = romaji;
                result.matching_titles.push_back(romaji);
            }
        }
        if (media["title"].contains("english") && !media["title"]["english"].is_null())
        {
            std::string english = media["title"]["english"].get<std::string>();
            if (!english.empty())
            {
                if (result.title.empty())
                    result.title = english;
                result.matching_titles.push_back(english);
            }
        }
        if (media["title"].contains("native") && !media["title"]["native"].is_null())
        {
            std::string native = media["title"]["native"].get<std::string>();
            if (!native.empty())
                result.matching_titles.push_back(native);
        }
    }

    if (media.contains("synonyms") && media["synonyms"].is_array())
    {
        for (const auto& syn : media["synonyms"])
        {
            if (syn.is_string())
            {
                std::string s = syn.get<std::string>();
                if (!s.empty())
                    result.matching_titles.push_back(s);
            }
        }
    }

    if (media.contains("episodes") && !media["episodes"].is_null())
    {
        try { result.episodes = media["episodes"].get<int>(); }
        catch (...) { result.episodes = 0; }
    }
    else
    {
        result.episodes = 0;
    }

    if (media.contains("format") && !media["format"].is_null())
        result.format = media["format"].get<std::string>();

    if (media.contains("coverImage") && !media["coverImage"].is_null())
    {
        if (media["coverImage"].contains("large") && !media["coverImage"]["large"].is_null())
            result.cover_url = media["coverImage"]["large"].get<std::string>();
    }

    // Find anime sequels (prefer TV formats)
    if (media.contains("relations") && !media["relations"].is_null()
        && media["relations"].contains("edges") && media["relations"]["edges"].is_array())
    {
        int any_sequel = -1;
        int tv_sequel = -1;
        for (auto& edge : media["relations"]["edges"])
        {
            if (edge.is_null() || !edge.contains("relationType") || edge["relationType"].is_null()
                || edge["relationType"].get<std::string>() != "SEQUEL"
                || !edge.contains("node") || edge["node"].is_null()
                || !edge["node"].contains("type") || edge["node"]["type"].is_null()
                || edge["node"]["type"].get<std::string>() != "ANIME")
                continue;

            int node_id = edge["node"].value("id", -1);
            std::string node_format = (edge["node"].contains("format") && !edge["node"]["format"].is_null())
                ? edge["node"]["format"].get<std::string>() : "";

            if (any_sequel <= 0)
                any_sequel = node_id;

            if (tv_sequel <= 0 && (node_format == "TV" || node_format == "TV_SHORT"))
                tv_sequel = node_id;
        }
        result.sequel_id = (tv_sequel > 0) ? tv_sequel : any_sequel;
    }

    return result;
}

bool parse_anilist_page_response(const nlohmann::json& j, std::vector<anilist_media>& results)
{
    results.clear();
    if (!j.contains("errors") && j.contains("data") && !j["data"].is_null()
        && j["data"].contains("Page") && !j["data"]["Page"].is_null()
        && j["data"]["Page"].contains("media") && j["data"]["Page"]["media"].is_array())
    {
        for (const auto& item : j["data"]["Page"]["media"])
        {
            auto parsed = parse_anilist_media(item);
            if (parsed)
                results.push_back(*parsed);
        }
        return true;
    }
    return false;
}
