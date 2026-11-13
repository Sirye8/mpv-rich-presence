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

#include <mpv/client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "mpv_rich_presence/store.hpp"
#include "mpv_rich_presence/activity.hpp"
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
        uint64_t application_id = 0;
        uint64_t session_start = 0, file_session = 0;
        std::unique_ptr<profile_store> store;
        bool store_failed = false, hidden_until_exit = false;
        std::string appearance_scope = "configuration", match_scope = "configuration", source_scope = "configuration";
        int store_version = -1;
        std::string cache_epoch, episode_label, scope_path, hidden_reason, last_sdk = "Waiting";
        bool hard_block = false;
        bool pinned_elsewhere = false, pinned_here = false;
        bool private_session = false, hidden_file = false;
        std::chrono::steady_clock::time_point hide_until{};
        std::string config_preset = "original";
        std::string config_source = "auto";
        nlohmann::json appearance = nlohmann::json::object(), privacy = nlohmann::json::object();
        nlohmann::json sources = nlohmann::json::object();
        nlohmann::json provider = nlohmann::json::object();
        nlohmann::json correction = nlohmann::json::object(), tracking = nlohmann::json::object();
        nlohmann::json preview = nlohmann::json::object();
        std::string snapshot_request, panel_page, panel_records, last_notice;
        std::string ask_offer, ask_payload, dismissed_ask;
        bool replace_draft = false;
        std::string anime_english, anime_romaji, anime_native;
        bool config_received = false;

        bool media_has_audio = false;
        bool media_has_video = false;
        std::string album, channel;
        std::string media_artist = "";
        std::string media_title = "";
        std::string track_title;

        std::mutex anime_mutex;
        std::atomic<int> file_load_id = 0;
        std::atomic<int> completed_anime_lookup = -1;

        bool watch_episode = false;
        int conflicting_anime_minutes = 0;
        bool fuzzy_episode_match = false;
        bool is_anime = false;
        std::string anime_title = "";
        std::string anime_episode = "";
        std::string anime_episode_raw = "";
        int anime_episode_total = 0;
        std::string anime_release_group = "";
        std::string anime_cover_url = "";
        int anime_anilist_id = 0;

        int last_playback_paused = -1;
        int last_published_paused = -1;
        int last_caching_state = 0;
        bool was_active_instance = false;
        std::atomic<bool> anime_dirty = false;
        double last_reported_pos = -1.0;
        std::chrono::steady_clock::time_point last_activity_update;

        int pause_timeout_seconds = 1800;
        std::chrono::steady_clock::time_point pause_start_time;
        uint64_t pause_start_epoch_ms = 0;
        bool rpc_hidden_by_timeout = false;

        std::string folder_filter_mode = "disabled";
        std::vector<std::string> folder_list;
        std::vector<std::string> panel_folders;
        nlohmann::json folder_filter = nlohmann::json::object();
        std::string folder_exception;
        bool folder_session_exception = false;

        std::string current_file_path;

        std::string instance_id = "";

        // Presence state tracking
        bool is_rpc_visible = false;
        bool force_update = false;
        bool needs_retry = false;
        std::chrono::steady_clock::time_point last_error_time;
        int retry_backoff_ms = 0;
        std::chrono::steady_clock::time_point last_heartbeat;
        double playback_speed = 1.0;

        // Background anime lookup worker
        std::mutex worker_mutex;
        std::condition_variable worker_cv;
        bool has_pending_lookup = false;
        nlohmann::json pending_correction;
        int pending_load_id = 0;
        std::string pending_filename = "";
        std::string pending_file_path = "";
        std::atomic<bool> worker_stop{false};
        std::thread worker_thread;

        ~rich_presence_state()
        {
            {
                std::lock_guard<std::mutex> lock(worker_mutex);
                worker_stop.store(true, std::memory_order_release);
            }
            worker_cv.notify_all();
            if (worker_thread.joinable())
                worker_thread.join();
            discord.reset();
            discord_api.reset();
        }
    };
}
