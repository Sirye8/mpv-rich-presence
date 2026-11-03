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
        std::string album, channel, title, artist, anime_title, english, native, romaji;
        std::string episode, raw_episode, group, cover;
        int total = 0, anilist_id = 0;
        double position = 0, remaining = 0, playtime_left = 0, speed = 1;
        uint64_t now = 0, pause_start = 0, session_start = 0;
    };

    nlohmann::json format_activity(const playback& media, const nlohmann::json& overrides);
}

