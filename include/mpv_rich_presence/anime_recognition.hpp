// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <functional>
#include "mpv_rich_presence/anilist_parser.hpp"

namespace mpvrp
{
    struct folder_video_name
    {
        std::string title;
        std::string year;
        std::optional<int> season;
        std::optional<int> episode;
    };

    std::optional<folder_video_name> video_name_from_folder(
        const std::string& filename, const std::string& folder, double duration,
        const std::string& embedded_title = {});

    struct anime_match
    {
        anilist_media media;
        std::string release_group, episode_label, episode_review;
        int episode = 0;
        int raw_episode = 0;
        int conflicting_anime_minutes = 0;
        bool fuzzy_episode = false;
        bool watch_episode = false;
    };

    struct anime_catalog
    {
        std::function<std::vector<anilist_media>(const std::string&, bool&)> search;
        std::function<std::optional<anilist_media>(int)> by_id;
    };

    std::optional<anime_match> recognize_anime(const std::string& filename,
        const nlohmann::json& correction, const anime_catalog& catalog,
        const std::function<bool()>& cancelled, bool& confirmed, const std::string& folder = {},
        const std::optional<folder_video_name>& folder_name = {});

    struct recognition_route
    {
        std::string source;
        bool runtime_conflict = false;
        bool hides_anime(bool resolved, bool provider_match, bool unavailable) const;
    };

    recognition_route route_recognition(std::string source, bool allowed, bool has_store,
        bool pinned_anime, bool has_provider, bool anime_pending, bool anime_unavailable,
        bool has_anime, bool anime_complete, bool fuzzy_episode, int anime_minutes,
        int conflicting_anime_minutes, double duration);
}
