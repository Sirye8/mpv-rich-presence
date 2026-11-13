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
    if (test.at("video").get<bool>())
        anime = recognize_anime(test.at("filename").get<std::string>(),
            test.value("correction", json{{"automatic", true}}),
            catalog, [] { return false; }, confirmed, test.value("folder", std::string{}));

    const auto route = route_recognition("auto", true, true, false, false, false, !confirmed,
        bool(anime), true, anime && anime->fuzzy_episode, anime ? anime->media.duration_minutes : 0,
        anime ? anime->conflicting_anime_minutes : 0, test.at("duration").get<double>());
    return {
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
