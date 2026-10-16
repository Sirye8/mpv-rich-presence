// This file is part of mpv-rich-presence.
// Copyright (c) 2026 Alden Wu.
// Copyright (c) 2026 Youssef M. (Sirye8).
//
// mpv-rich-presence is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.

#pragma once

#include <mpv/client.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "mpv_rich_presence/discord_api.hpp"
#include "mpv_rich_presence/discord_utils.hpp"

namespace mpvrp
{
    struct rich_presence_state
    {
        mpv_handle* mpv = nullptr;

        std::unique_ptr<discord_client> discord = nullptr;
        std::shared_ptr<discord_api_importer> discord_api = nullptr;

        std::optional<bool> is_enabled = std::nullopt;

        bool media_has_audio = false;
        bool media_has_video = false;
        std::string media_artist = "";
        std::string media_title = "";

        std::mutex anime_mutex;
        std::atomic<int> file_load_id = 0;

        bool is_anime = false;
        std::string anime_title = "";
        std::string anime_episode = "";
        std::string anime_episode_raw = "";
        int anime_episode_total = 0;
        std::string anime_release_group = "";
        std::string anime_cover_url = "";
        int anime_anilist_id = 0;

        int last_paused = -1;
        bool was_paused = false;
        std::atomic<bool> anime_dirty = false;
        double last_reported_pos = -1.0;
        std::chrono::steady_clock::time_point last_activity_update;

        int pause_timeout_seconds = 1800;
        std::chrono::steady_clock::time_point pause_start_time;
        uint64_t pause_start_epoch_ms = 0;
        bool rpc_hidden_by_timeout = false;

        std::string folder_filter_mode = "disabled";
        std::vector<std::string> folder_list;

        std::string current_file_path;

        std::string instance_id = "";
    };
}
