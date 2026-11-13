// SPDX-License-Identifier: AGPL-3.0-or-later
#include "mpv_rich_presence/anime_recognition.hpp"

#include <iostream>

using nlohmann::json;
using namespace mpvrp;

static json run(const json& test)
{
    auto requests = json::array();
    bool confirmed = true;
    const anime_catalog catalog{
        [&](const std::string& query, bool& ok) {
            requests.push_back(query);
            if (test.value("anime_offline", false))
            {
                ok = false;
                return std::vector<anilist_media>{};
            }
            const auto& pages = test.at("pages");
            const auto page = pages.find(query);
            const auto& rows = page == pages.end() ? test.at("default_page") : *page;
            std::vector<anilist_media> media;
            for (const auto& row : rows)
                if (auto entry = parse_anilist_media(row)) media.push_back(std::move(*entry));
            return media;
        },
        [&](int id) -> std::optional<anilist_media> {
            const auto& entries = test.at("by_id");
            if (auto it = entries.find(std::to_string(id)); it != entries.end())
                return parse_anilist_media(*it);
            return std::nullopt;
        }
    };
    std::optional<anime_match> anime;
    std::optional<folder_video_name> folder_name;
    const auto folder = test.value("folder", std::string{});
    const auto filename = test.at("filename").get<std::string>();
    const double duration = test.at("duration");
    if (test.at("video").get<bool>())
    {
        const auto tags = test.value("tags", json::object());
        folder_name = video_name_from_folder(filename, folder, duration, tags.value("title", std::string{}));
        anime = recognize_anime(filename,
            test.value("correction", json{{"automatic", true}}),
            catalog, [] { return false; }, confirmed, folder, folder_name);
    }

    const auto route = route_recognition("auto", true, true, false, false, false, !confirmed,
        bool(anime), true, anime && anime->fuzzy_episode, anime ? anime->media.duration_minutes : 0,
        anime ? anime->conflicting_anime_minutes : 0, duration);
    json result = {
        {"source", route.source}, {"runtime_conflict", route.runtime_conflict},
        {"hide_without_provider", route.hides_anime(true, false, false)},
        {"hide_on_provider_failure", route.hides_anime(true, false, true)},
        {"anime_id", anime ? anime->media.id : 0},
        {"anime_title", anime ? anime->media.title : ""},
        {"episode", anime ? anime->episode : 0},
        {"raw_episode", anime ? anime->raw_episode : 0},
        {"watch_episode", anime && anime->watch_episode && route.source == "original"},
        {"review", anime ? anime->episode_review : ""},
        {"confirmed", confirmed}, {"queries", std::move(requests)}
    };
    if (folder_name && route.source == "auto")
    {
        result["folder_title"] = folder_name->title;
        result["folder_year"] = folder_name->year;
        result["folder_has_episode"] = folder_name->episode.has_value();
        if (folder_name->season && folder_name->episode)
            result["folder_episode"] = {*folder_name->season, *folder_name->episode};
    }
    return result;
}

int main()
{
    for (std::string line; std::getline(std::cin, line);)
    {
        try
        {
            std::cout << run(json::parse(line)).dump() << '\n';
        }
        catch (const std::exception& error)
        {
            std::cout << json{{"error", error.what()}}.dump() << '\n';
        }
    }
}
