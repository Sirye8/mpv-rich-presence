// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace mpvrp
{
    struct playback
    {
        nlohmann::json provider = nlohmann::json::object();
        bool video = false, anime = false, paused = false, buffering = false;
        std::string album, channel, title, track_title, artist, anime_title, english, native, romaji;
        std::string episode_label, episode, raw_episode, group, cover;
        std::string plays, loved; // Last.fm play count and "Loved" for card templates
        int total = 0, anilist_id = 0, mal_id = 0;
        double position = 0, remaining = 0, playtime_left = 0, speed = 1;
        uint64_t now = 0, pause_start = 0, session_start = 0;
    };

    nlohmann::json format_activity(const playback& media, const nlohmann::json& overrides);
    // Appearance settings with the media layout's overrides applied.
    nlohmann::json layout_options(const playback& media, const nlohmann::json& overrides);
    // Seconds a paused card stays visible for this media layout. 0 keeps it, and fallback is the conf value.
    int pause_timeout_seconds(const playback& media, const nlohmann::json& overrides, int fallback);
}
