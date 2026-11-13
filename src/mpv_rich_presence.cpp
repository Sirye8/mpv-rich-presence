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

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <boost/dll.hpp>
#include <cmrc/cmrc.hpp>
#include <cpr/cpr.h>
#include <mpv/client.h>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include "mpv_rich_presence/publish_activity.hpp"
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <mutex>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mpv_rich_presence/anime_recognition.hpp"
#include "mpv_rich_presence/lease_manager.hpp"
#include "mpv_rich_presence/mpv_utils.hpp"
#include "mpv_rich_presence/rich_presence_state.hpp"
#include "mpv_rich_presence/string_utils.hpp"

namespace chrono = std::chrono;
namespace fs = std::filesystem;
namespace dll = boost::dll;
using namespace std::literals;
using namespace mpvrp;

static constexpr chrono::milliseconds SLEEP_DURATION = 250ms;
static const std::string DISCORD_SDK_DIR = "discord_social_sdk";

CMRC_DECLARE(mpvrp);

// Helper functions

static std::string generate_instance_id()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    return std::to_string(dist(gen));
}





static bool sharing_hidden(const rich_presence_state& state)
{
    return state.hidden_file || state.hidden_until_exit || chrono::steady_clock::now() < state.hide_until
        || state.privacy.value("mode", std::string("full")) == "hidden";
}

static bool passes_configured_folders(const rich_presence_state& state)
{
    const auto mode = state.folder_filter.find("mode");
    const auto& active_mode = mode == state.folder_filter.end() ? state.folder_filter_mode : mode->get_ref<const std::string&>();
    return passes_folder_filter(state.current_file_path, active_mode,
        state.folder_filter.contains("folders") ? state.panel_folders : state.folder_list);
}

static bool folder_exception_active(const rich_presence_state& state)
{
    return !state.scope_path.empty() && !state.scope_path.starts_with("stream-") &&
        (state.folder_session_exception || state.folder_exception == state.scope_path);
}

static bool local_permission(const rich_presence_state& state)
{
    return state.config_received && !state.store_failed && !state.private_session
        && !state.hard_block && !state.privacy.value("block_lookup", false)
        && (state.media_has_audio || state.media_has_video)
        && (folder_exception_active(state) || passes_configured_folders(state));
}

static bool is_window_eligible(const rich_presence_state& state)
{
    return local_permission(state) && state.is_enabled.value_or(false) && state.application_id && !sharing_hidden(state) && !state.pinned_elsewhere;
}

static bool lookup_allowed(const rich_presence_state& state)
{
    return (local_permission(state) && state.is_enabled.value_or(false) && state.application_id && !sharing_hidden(state))
        || (local_permission(state) && state.tracking.value("without_sharing", false)
        && state.tracking.value("mode", std::string("off")) != "off");
}

static void read_profiles(rich_presence_state& state)
{
    if (state.store)
    {
        state.store_version = state.store->version();
        state.privacy = state.store->effective("privacy", state.scope_path);
        state.hard_block = state.store->blocked(state.scope_path);
        state.appearance = state.store->effective("appearance", state.scope_path, &state.appearance_scope);
        state.correction = state.store->effective("match", state.scope_path, &state.match_scope);
        state.provider = state.store->effective("provider", state.scope_path);
        state.sources = state.store->effective("sources", state.scope_path, &state.source_scope);
        state.tracking = state.store->effective("tracking", state.scope_path);
        state.folder_filter = state.store->get("folder-filter", "global");
        state.panel_folders = state.folder_filter.value("folders", std::vector<std::string>{});
    }
    if (!state.appearance.contains("preset")) state.appearance["preset"] = state.config_preset;
    if (!state.sources.contains("source")) state.sources["source"] = state.config_source;
}

// AniList API caching

static std::mutex anilist_cache_mutex;
static chrono::steady_clock::time_point anilist_rate_limited_until;
template<typename T> struct cache_record
{
    T value;
    chrono::steady_clock::time_point expires;
};
static std::atomic<uint64_t> cache_generation{0};
static std::unordered_map<std::string, cache_record<std::vector<anilist_media>>> anilist_page_cache;
static std::unordered_map<int, cache_record<anilist_media>> anilist_id_cache;

static void clear_anilist_cache()
{
    std::lock_guard lock(anilist_cache_mutex);
    ++cache_generation;
    anilist_page_cache.clear();
    anilist_id_cache.clear();
}

static void record_anilist_rate_limit(const cpr::Response& response)
{
    if (response.status_code != 429) return;
    int seconds = 60;
    if (auto header = response.header.find("Retry-After"); header != response.header.end())
    {
        const auto& value = header->second;
        std::from_chars(value.data(), value.data() + value.size(), seconds);
    }
    std::lock_guard lock(anilist_cache_mutex);
    anilist_rate_limited_until = (std::max)(anilist_rate_limited_until,
        chrono::steady_clock::now() + chrono::seconds(std::clamp(seconds, 1, 300)));
}

static std::vector<anilist_media> search_anilist_page(const std::string& search, bool& confirmed)
{
    if (search.empty() || search.size() > 256 || search.find("://") != std::string::npos ||
        search.starts_with("/") || search.starts_with("\\") || (search.size() > 2 && search[1] == ':' &&
        (search[2] == '/' || search[2] == '\\')) ||
        (search.find('?') != std::string::npos && search.find('=') != std::string::npos))
    {
        return {};
    }
    auto generation = cache_generation.load();
    {
        std::lock_guard<std::mutex> lock(anilist_cache_mutex);
        auto it = anilist_page_cache.find(search);
        if (it != anilist_page_cache.end() && it->second.expires > chrono::steady_clock::now())
            return it->second.value;
        if (anilist_rate_limited_until > chrono::steady_clock::now())
        {
            confirmed = false;
            return {};
        }
    }

    nlohmann::json query = {
        { "query", std::format("query ($search: String) {{ Page(page: 1, perPage: 10) {{ media(search: $search, type: ANIME) {{ {} }} }} }}", ANILIST_PAGE_FIELDS) },
        { "variables", { { "search", search } } }
    };

    auto r = cpr::Post(cpr::Url { "https://graphql.anilist.co" },
        cpr::Header {
            { "Content-Type", "application/json" },
            { "Accept", "application/json" },
            { "User-Agent", "mpv-rich-presence/1.0" }
        },
        cpr::Body { query.dump() },
        cpr::Timeout { 3000 });

    record_anilist_rate_limit(r);
    std::vector<anilist_media> results;
    if (r.status_code == 200)
    {
        try
        {
            auto j = nlohmann::json::parse(r.text);
            if (parse_anilist_page_response(j, results))
            {
                std::lock_guard<std::mutex> lock(anilist_cache_mutex);
                if (generation != cache_generation.load())
                {
                    confirmed = false;
                    return {};
                }
                auto now = chrono::steady_clock::now();
                for (const auto& item : results)
                    anilist_id_cache[item.id] = {item, now + 24h};
                anilist_page_cache[search] = {results, now + (results.empty() ? 5min : 24h)};
                return results;
            }
        }
        catch (...)
        {
        }
    }

    confirmed = false;
    return results;
}

static std::optional<anilist_media> fetch_anilist_by_id(int id)
{
    auto generation = cache_generation.load();
    {
        std::lock_guard<std::mutex> lock(anilist_cache_mutex);
        auto it = anilist_id_cache.find(id);
        if (it != anilist_id_cache.end() && it->second.expires > chrono::steady_clock::now())
            return it->second.value;
        if (anilist_rate_limited_until > chrono::steady_clock::now())
            return {};
    }

    nlohmann::json query = {
        { "query", std::format("query ($id: Int) {{ Media(id: $id, type: ANIME) {{ {} }} }}", ANILIST_PAGE_FIELDS) },
        { "variables", { { "id", id } } }
    };

    auto r = cpr::Post(cpr::Url { "https://graphql.anilist.co" },
        cpr::Header {
            { "Content-Type", "application/json" },
            { "Accept", "application/json" },
            { "User-Agent", "mpv-rich-presence/1.0" }
        },
        cpr::Body { query.dump() },
        cpr::Timeout { 3000 });

    record_anilist_rate_limit(r);
    std::optional<anilist_media> result = std::nullopt;
    if (r.status_code == 200)
    {
        try
        {
            auto j = nlohmann::json::parse(r.text);
            if (!j.contains("errors") && j.contains("data") && !j["data"].is_null()
                && j["data"].contains("Media") && !j["data"]["Media"].is_null())
            {
                result = parse_anilist_media(j["data"]["Media"]);
                if (result)
                {
                    std::lock_guard<std::mutex> lock(anilist_cache_mutex);
                    if (generation != cache_generation.load()) return {};
                    anilist_id_cache[result->id] = {*result,chrono::steady_clock::now()+24h};
                }
            }
        }
        catch (...)
        {
        }
    }

    return result;
}

// Resolve outside the player lock, then publish the result for the current file.
static void process_anime_lookup(rich_presence_state& state, const std::string& filename,
    const std::string& folder, int load_id, const nlohmann::json& correction, bool& confirmed)
{
    const auto cancelled = [&] {
        return state.worker_stop.load(std::memory_order_acquire) || state.file_load_id != load_id;
    };
    auto match = recognize_anime(filename, correction, {search_anilist_page, fetch_anilist_by_id},
        cancelled, confirmed, folder);
    if (!match || cancelled()) return;

    std::lock_guard lock(state.anime_mutex);
    if (state.file_load_id != load_id) return;
    auto& media = match->media;
    state.episode_label = std::move(match->episode_label);
    state.episode_review = std::move(match->episode_review);
    state.watch_episode = match->watch_episode;
    state.is_anime = true;
    state.conflicting_anime_minutes = match->conflicting_anime_minutes;
    state.fuzzy_episode_match = match->fuzzy_episode;
    state.anime_duration_minutes = media.duration_minutes;
    state.anime_title = std::move(media.title);
    state.anime_english = std::move(media.english);
    state.anime_native = std::move(media.native);
    state.anime_romaji = std::move(media.romaji);
    state.anime_episode = match->episode > 0 ? std::to_string(match->episode) : "";
    state.anime_episode_raw = match->raw_episode > 0 ? std::to_string(match->raw_episode) : "";
    state.anime_episode_total = media.episodes;
    state.anime_release_group = std::move(match->release_group);
    state.anime_cover_url = std::move(media.cover_url);
    state.anime_anilist_id = media.id;
    state.anime_dirty = true;
}

// Background worker thread entry
static void anime_worker_loop(rich_presence_state& state)
{
    while (true)
    {
        int current_load_id = 0;
        nlohmann::json correction;
        std::string filename_str;
        std::string folder;

        {
            std::unique_lock<std::mutex> lock(state.worker_mutex);
            state.worker_cv.wait(lock, [&state]() {
                return state.worker_stop.load(std::memory_order_acquire) || state.has_pending_lookup;
            });

            if (state.worker_stop.load(std::memory_order_acquire))
                break;

            if (!state.has_pending_lookup)
                continue;

            current_load_id = state.pending_load_id;
            correction = state.pending_correction;
            filename_str = std::move(state.pending_filename);
            folder = std::move(state.pending_folder);
            state.has_pending_lookup = false;
        }

        bool confirmed = true;
        try
        {
            process_anime_lookup(state, filename_str, folder, current_load_id, correction, confirmed);
        }
        catch (...)
        {
            confirmed = false;
        }
        // A failed search is terminal for this attempt. Keep it distinct from
        // a confirmed miss so Automatic can still protect ambiguous anime.
        if (!confirmed) state.failed_anime_lookup = current_load_id;
        state.completed_anime_lookup = current_load_id;
    }
}

static bool use_anilist(const rich_presence_state& state)
{
    if (state.correction.value("not_anime", false)) return false;
    const auto source = state.sources.value("source", std::string("auto"));
    if (state.correction.contains("id") || source == "original") return true;
    return (source == "auto" || source == "anime") && !state.scope_path.starts_with("stream-");
}

// React to configuration or eligibility changes
static void on_eligibility_changed(rich_presence_state& state)
{
    state.force_update = true;
    bool eligible = lookup_allowed(state);

    if (!eligible)
    {
        // Invalidate any pending or in-flight background anime lookups
        {
            std::lock_guard<std::mutex> lock(state.worker_mutex);
            state.has_pending_lookup = false;
            state.pending_load_id = ++state.file_load_id;
        }
        {
            std::lock_guard<std::mutex> lock(state.anime_mutex);
            state.is_anime = false;
            state.anime_anilist_id = 0;
        }
        if (state.is_rpc_visible && state.discord_api && state.discord)
        {
            state.discord_api->Discord_Client_ClearRichPresence(&state.discord->get());
            state.is_rpc_visible = false;
        }
    }
    else
    {
        // If newly eligible and video media is loaded without resolved anime metadata, enqueue lookup
        if (state.media_has_video && state.provider.empty() && !state.current_file_path.empty() &&
            use_anilist(state))
        {
            bool needs_lookup = false;
            {
                std::lock_guard<std::mutex> lock(state.anime_mutex);
                if (!state.is_anime && state.anime_anilist_id == 0)
                    needs_lookup = true;
            }

            if (needs_lookup)
            {
                int current_load_id = ++state.file_load_id;
                std::string filename_str = path_utf8(fs::u8path(state.current_file_path).filename());
                {
                    std::lock_guard<std::mutex> lock(state.worker_mutex);
                    state.pending_load_id = current_load_id;
                    state.pending_filename = filename_str;
                    state.pending_folder = path_utf8(fs::u8path(state.current_file_path).parent_path().filename());
                    state.pending_correction = state.correction;
                    state.pending_correction["automatic"] = state.sources.value("source",std::string("auto")) == "auto";
                    state.has_pending_lookup = true;
                }
                state.worker_cv.notify_one();
            }
        }
    }
}

// Event handlers

static bool valid_source(const nlohmann::json& value)
{
    return value == "auto" || value == "local" || value == "anime" || value == "movie-tv" || value == "movie" || value == "tv"
        || value == "original" || value == "tmdb-movie" || value == "tmdb-tv" || value == "tvmaze"
        || value == "wikidata" || value == "musicbrainz" || value == "acoustid" || value == "stream";
}

static bool valid_settings(const std::string& category, const nlohmann::json& values)
{
    if (!values.is_object() || values.size() > 32) return false;
    for (const auto& [key, val] : values.items())
    {
        if (val.is_null()) continue;
        if (category == "appearance")
        {
            if (key.ends_with("_layout"))
            {
                if (val.contains(key) || !val.is_object() || !valid_settings(category,val)) return false;
            }
            else if (key == "buttons")
            {
                if (!val.is_array() || val.size()>2) return false;
                for (auto& b : val)
                    if (!b.is_object() || !b.contains("url") || !b.at("url").is_string() ||
                        !b.contains("label") || !b.at("label").is_string()) return false;
            }
            else if (key == "preset" || key == "language" || key == "numbering" || key == "timer" || key == "compact"
                || key == "headline" || key.ends_with("_template") || key.ends_with("_url"))
            { if (!val.is_string()) return false; }
            else if (!val.is_boolean()) return false;
        }
        if (category == "sources" && !(key == "source" && valid_source(val))) return false;
        if (category == "panel" && !(key == "position" && (val == "top-left" || val == "top-center" ||
            val == "top-right" || val == "bottom-left" || val == "bottom-center" || val == "bottom-right"))) return false;
        if (category == "match" && !((key == "id" && val.is_number_integer() && val > 0 && val <= INT32_MAX)
            || (key == "offset" && val.is_number_integer() && val >= -9999 && val <= 9999)
            || (key == "not_anime" && val.is_boolean()))) return false;
        if (category == "exclude" && !(key == "blocked" && val.is_boolean())) return false;
        if (category == "privacy" && !(key == "mode" && (val == "hidden" || val == "generic" || val == "full"))) return false;
        if (category == "tracking" && !((key == "mode" && (val == "off" || val == "manual" || val == "ask" || val == "auto"))
            || (key == "threshold" && val.is_number_integer() && val >= 1 && val <= 1440)
            || (key == "ask_timing" && (val == "threshold" || val == "end" || val == "leave"))
            || (key == "ask_position" && (val == "top-left" || val == "top-center" || val == "top-right"
                || val == "bottom-left" || val == "bottom-center" || val == "bottom-right"))
            || (key == "ask_leave_position" && (val == "center" || val == "top-left" || val == "top-center" || val == "top-right"
                || val == "bottom-left" || val == "bottom-center" || val == "bottom-right"))
            || (key == "ask_timeout" && val.is_number_integer() && (val == 0 || (val >= 5 && val <= 300)))
            || ((key == "without_sharing" || key == "resume" || key == "rewatch" || key == "keep_pending") && val.is_boolean()))) return false;
    }
    return true;
}

static nlohmann::json current_ask_proposal(rich_presence_state& state)
{
    const auto mode = state.tracking.value("mode", std::string{});
    if (!state.store || (mode != "ask" && mode != "auto") || !lookup_allowed(state))
        return {};
    const auto player = state.store->get("player", state.instance_id);
    if (player.value("session", UINT64_MAX) != state.file_session)
        return {};
    const auto account = state.store->get("account", "current");
    const int viewer = account.value("id", 0);
    const int media = player.value("media", 0);
    const auto episode = player.value("episode", std::string{});
    if (viewer <= 0 || media <= 0 || account.value("expired", false))
        return {};
    {
        std::lock_guard lock(state.anime_mutex);
        if (!state.is_anime || state.anime_anilist_id != media || state.anime_episode != episode)
            return {};
        if (mode == "auto" && !state.episode_review.empty())
            return {{"key","episode-review"},{"media",media},{"title",state.anime_title},
                {"account",account.value("generation",std::string{})},{"review_only",state.episode_review}};
        if (!player.value("watch_episode", false) || !state.watch_episode || episode.empty()) return {};
    }
    const auto scope = std::format("{}/{}", viewer, media);
    const auto cycle = state.store->get("cycle", scope).value("number", 0);
    const auto proposal = state.store->get("pending", std::format("{}/{}/{}", scope, episode, cycle));
    if (proposal.empty() || proposal.value("offline", false) || !proposal.contains("status")
        || proposal.value("account", std::string{}) != account.value("generation", std::string{}))
        return {};
    if (mode == "auto")
    {
        const auto base = proposal.value("base",nlohmann::json::object());
        // Older builds only left proposals pending in Auto when they needed review,
        // or when a consecutive update failed. In either case let the user decide.
        if (!proposal.value("needs_review", !base.empty())) return {};
    }
    return proposal;
}

static std::string ask_prompt_id(const rich_presence_state& state, const nlohmann::json& proposal)
{
    if (proposal.empty()) return {};
    return std::format("{}/{}/{}/{}", state.file_session, proposal.at("account").get<std::string>(),
        proposal.at("key").get<std::string>(), proposal.value("revision", std::string{}));
}

static void publish_ask_offer(rich_presence_state& state, const nlohmann::json& proposal)
{
    auto id = ask_prompt_id(state, proposal);
    if (id == state.dismissed_ask) id.clear();
    const auto timing = state.tracking.value("mode", std::string{}) == "auto" ? "leave" :
        state.tracking.value("ask_timing",std::string("threshold"));
    auto offer = id.empty() ? nlohmann::json::object() :
        nlohmann::json{{"id",id},{"session",state.file_session},{"proposal",proposal},
            {"timing",timing},
            {"position",timing == "leave" ? state.tracking.value("ask_leave_position",std::string("center")) :
                state.tracking.value("ask_position",std::string("top-right"))},
            {"seconds",state.tracking.value("ask_timeout",30)}};
    const auto payload = offer.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (payload == state.ask_payload) return;
    const char* args[] = {"script-message-to", "mpv_rich_presence", "ask-review", payload.c_str(), nullptr};
    if (mpv_command_async(state.mpv, 0, args) < 0) return;
    state.ask_payload = payload;
    state.ask_offer = std::move(id);
}

static void handle_client_message(rich_presence_state& state, mpv_event_client_message* msg)
{
    if (msg->num_args == 0)
        return;

    auto is_enabled_prev = state.is_enabled;

    if (msg->args[0] == "panel"s && msg->num_args == 2)
    {
        try
        {
            auto req = nlohmann::json::parse(msg->args[1]);
            auto action = req.at("action").get<std::string>();
            if (action == "snapshot")
            {
                state.snapshot_request = req.value("nonce", std::string("menu"));
                state.panel_page = req.value("page",std::string("Menu"));
                state.panel_records = req.value("records", std::string{});
                state.replace_draft = req.value("replace",false);
                state.preview = req.value("draft", nlohmann::json::object());
                return;
            }
            if (action == "setup")
            {
                state.config_preset = req.value("preset", std::string("original"));
                const auto source = req.value("source", std::string("auto"));
                state.config_source = valid_source(source) ? source : "auto";
                if (state.config_source != source) mpv_print(state.mpv, "Invalid recognition_source. Using Automatic.");
                if (!state.store)
                {
                    state.store_failed = true;
                    auto path = fs::u8path(req.at("database").get<std::string>());
                    auto legacy = fs::u8path(req.value("legacy_database", std::string{}));
                    state.store = std::make_unique<profile_store>(path, legacy);
                    state.store_failed = false;
                }
                read_profiles(state);
                return;
            }
            if (req.value("session", state.file_session) != state.file_session) return;
            if (action == "ask_dismiss")
            {
                if (req.value("id", std::string{}) == state.ask_offer)
                    state.dismissed_ask = state.ask_offer;
                return;
            }
            if (action == "ask_refresh")
            {
                if (!state.ask_offer.empty())
                {
                    state.ask_payload.clear();
                    publish_ask_offer(state, current_ask_proposal(state));
                }
                return;
            }
            if (action == "service" && state.store)
            {
                auto job = req.at("job");
                auto op = job.at("op").get<std::string>();
                if (req.contains("ask_prompt"))
                {
                    const auto proposal = current_ask_proposal(state);
                    if ((op != "accept" && op != "skip") || state.ask_offer.empty() ||
                        req.at("ask_prompt") != state.ask_offer ||
                        ask_prompt_id(state, proposal) != state.ask_offer ||
                        proposal.contains("review_only") ||
                        job.value("proposal", std::string{}) != proposal.value("key", std::string{})) return;
                    job["revision"] = proposal.value("revision", std::string{});
                }
                if ((op == "search" || op == "select" || op == "write" || op == "accept" || op == "read")
                    && !lookup_allowed(state)) return;
                job["window"] = state.instance_id;
                job["session"] = state.file_session;
                {
                    std::lock_guard lock(state.anime_mutex);
                    job["media"] = state.anime_anilist_id;
                }
                state.store->put("job", generate_instance_id(), job);
                const char* args[] = {"script-message-to","mpv_rich_presence","start-service",state.instance_id.c_str(),nullptr};
                mpv_command(state.mpv,args);
                return;
            }
            bool old_lookup = lookup_allowed(state);
            auto old_sources = state.sources;
            auto old_correction = state.correction;
            if (action == "private")
            {
                state.private_session = req.at("enabled").get<bool>();
                if (state.private_session && state.store)
                {
                    state.store->clear("player",state.instance_id);
                    state.store->clear("response",state.instance_id);
                    state.store->put("cancel",state.instance_id,{{"session",state.file_session}});
                }
            }
            else if (action == "hide_file") state.hidden_file = !state.hidden_file;
            else if (action == "folder_exception")
            {
                const auto mode = req.value("mode", std::string{});
                if (mode != "off" && (state.scope_path.empty() || state.scope_path.starts_with("stream-"))) return;
                if (mode != "off" && mode != "file" && mode != "window") return;
                state.folder_exception = mode == "file" ? state.scope_path : "";
                state.folder_session_exception = mode == "window";
            }
            else if (action == "folder_filter" && state.store)
            {
                const auto mode = req.value("mode", std::string{});
                if (mode == "config") state.store->clear("folder-filter", "global");
                else if (mode == "disabled" || mode == "whitelist" || mode == "blacklist")
                    state.store->save("folder-filter", "global", {{"mode",mode}});
                else if (req.contains("folders"))
                {
                    const auto& list = req.at("folders");
                    if (!list.is_array() || list.size() > 256) return;
                    std::vector<std::string> folders;
                    for (const auto& item : list)
                    {
                        auto path = item.get<std::string>();
                        if (path.size() == 2 && path.back() == ':') path += '/';
                        if (path.size() > 4096 || path.find('\0') != std::string::npos ||
                            !fs::u8path(path).is_absolute()) return;
                        const auto normalized = normalize_path(path);
                        const auto folder = normalized.empty() ? "/" : normalized;
                        if (std::find(folders.begin(), folders.end(), folder) == folders.end()) folders.push_back(folder);
                    }
                    state.store->save("folder-filter", "global", {{"folders",folders}});
                }
                else return;
                state.folder_exception.clear();
                state.folder_session_exception = false;
                read_profiles(state);
            }
            else if (action == "hide_hour") state.hide_until = chrono::steady_clock::now() + chrono::hours(1);
            else if (action == "hide_until_exit") state.hidden_until_exit = req.at("enabled").get<bool>();
            else if (action == "hide_minutes")
            {
                auto minutes = req.at("minutes").get<int>();
                if (minutes < 0 || minutes > 1440) return;
                state.hide_until = chrono::steady_clock::now() + chrono::minutes(minutes);
            }
            else if ((action == "pin" || action == "unpin") && state.store)
            {
                if (action == "pin" && local_permission(state))
                {
                    auto now = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
                    state.store->put("pin","current",{{"owner",state.instance_id},{"expires",now+6000}});
                    state.pinned_elsewhere = false;
                    if (is_window_eligible(state)) claim_active_instance(state.instance_id);
                }
                else if (action == "unpin") state.store->clear("pin","current");
            }
            else if (action == "claim")
            {
                if (is_window_eligible(state)) claim_active_instance(state.instance_id);
            }
            else if (action == "refresh_match")
            {
                if (!lookup_allowed(state)) return;
                clear_anilist_cache();
                if (state.store)
                {
                    state.store->clear("auto",state.instance_id);
                    state.store->put("refresh",state.instance_id,{{"id",generate_instance_id()}});
                }
            }
            else if ((action == "apply" || action == "clear") && state.store)
            {
                const auto category = req.at("category").get<std::string>();
                if (category == "panel")
                {
                    if (action == "clear") state.store->clear("panel","global");
                    else if (const auto& values = req.at("values"); valid_settings(category,values))
                        state.store->save("panel","global",values);
                    return;
                }
                if (category != "appearance" && category != "match" && category != "privacy" && category != "exclude" && category != "sources" && category != "tracking") return;
                auto scope = req.value("scope", std::string("global"));
                if (scope == "file" && !state.scope_path.empty()) scope = "file:" + state.scope_path;
                else if (scope == "folder" && !state.scope_path.starts_with("stream-"))
                {
                    auto folder = path_utf8(fs::u8path(state.scope_path).parent_path());
                    if (folder.empty()) return;
                    scope = "folder:" + folder;
                }
                else if (scope != "global") return;
                if (action == "clear")
                {
                    if (category == "privacy") state.hidden_file = true;
                    state.store->clear(category, scope);
                }
                else
                {
                    const auto& values = req.at("values");
                    if (!valid_settings(category, values)) return;
                    auto patch = values;
                    if (category == "privacy" && values.value("mode",std::string("hidden")) != "hidden") patch["block_lookup"] = nullptr;
                    if (category == "appearance" && req.value("replace",false)) state.store->put(category,scope,patch);
                    else state.store->save(category, scope, patch);
                }
                read_profiles(state);
            }
            else return;

            if (old_sources != state.sources || old_correction != state.correction || action == "refresh_match")
            {
                ++state.file_load_id;
                std::lock_guard lock(state.anime_mutex);
                state.is_anime = false;
                state.anime_anilist_id = 0;
            }
            if (old_lookup != lookup_allowed(state) || old_sources != state.sources || old_correction != state.correction || action == "refresh_match")
                on_eligibility_changed(state);
            state.force_update = true;
        }
        catch (const std::exception&) { mpv_print(state.mpv, "Could not apply panel request"); }
        return;
    }

    if (msg->args[0] == "toggle"s)
    {
        state.is_enabled = !state.is_enabled.has_value() || !*state.is_enabled;
        if (state.config_received)
            on_eligibility_changed(state);
    }
    else if (msg->args[0] == "on"s)
    {
        state.is_enabled = true;
        if (state.config_received)
            on_eligibility_changed(state);
    }
    else if (msg->args[0] == "off"s)
    {
        state.is_enabled = false;
        if (state.config_received)
            on_eligibility_changed(state);
    }
    else if (msg->args[0] == "application_id"s)
    {
        if (msg->num_args < 2)
        {
            mpv_print(state.mpv, "Not enough args for application_id");
        }
        else
        {
            std::string_view value(msg->args[1]);
            uint64_t id = 0;
            auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), id);
            if (error != std::errc{} || end != value.data() + value.size()) id = 0;
            if (state.application_id != id)
            {
                state.application_id = id;
                if (state.config_received) on_eligibility_changed(state);
            }
        }
    }
    else if (msg->args[0] == "folder_filter_mode"s)
    {
        if (msg->num_args >= 2)
        {
            std::string mode = msg->args[1];
            if (mode != "whitelist" && mode != "blacklist" && mode != "disabled")
            {
                mpv_print(state.mpv, std::format("Warning: invalid folder filter mode '{}', ignoring and preserving '{}'", mode, state.folder_filter_mode));
            }
            else
            {
                state.folder_filter_mode = mode;
                mpv_print(state.mpv, std::format("Folder filter mode: {}", state.folder_filter_mode));
                if (state.config_received)
                    on_eligibility_changed(state);
            }
        }
    }
    else if (msg->args[0] == "folder_list"s)
    {
        if (msg->num_args >= 2)
        {
            state.folder_list = parse_folder_list(msg->args[1]);
            mpv_print(state.mpv, std::format("Folder list: {} entries", state.folder_list.size()));
            if (state.config_received)
                on_eligibility_changed(state);
        }
    }
    else if (msg->args[0] == "pause_timeout"s)
    {
        if (msg->num_args >= 2)
        {
            try
            {
                state.pause_timeout_seconds = std::stoi(msg->args[1]);
                mpv_print(state.mpv, std::format("Pause timeout: {}s", state.pause_timeout_seconds));
            }
            catch (...)
            {
            }
        }
    }
    else if (msg->args[0] == "config_complete"s)
    {
        state.config_received = true;
        mpv_print(state.mpv, "Configuration complete received from bridge");
        on_eligibility_changed(state);
    }

    if (is_enabled_prev.has_value() && state.is_enabled.has_value() && state.is_enabled != is_enabled_prev)
        mpv_show(state.mpv, std::format("Rich presence {}", *state.is_enabled ? "enabled" : "disabled"));
}

static std::string get_audio_artist(mpv_handle* mpv)
{
    for (const auto* key : {"metadata/by-key/artist", "metadata/by-key/Artist",
        "metadata/by-key/album_artist", "metadata/by-key/albumartist"})
    {
        auto artist = get_mpv_osd_string(mpv,key);
        if (!artist.empty()) return artist;
    }
    return {};
}

static void handle_file_loaded(rich_presence_state& state)
{
    ++state.file_session;
    state.folder_exception.clear();
    state.hidden_file = false;
    state.session_start = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    state.media_has_audio = false;
    state.media_has_video = false;

    int64_t media_track_count = 0;
    mpv_get_property(state.mpv, "track-list/count", MPV_FORMAT_INT64, &media_track_count);
    for (int64_t i = 0; i < media_track_count; i++)
    {
        std::string media_track_type = get_mpv_string(state.mpv, std::format("track-list/{}/type", i).c_str());
        if (media_track_type == "audio")
            state.media_has_audio = true;

        if (media_track_type == "video")
        {
            int is_image = 0;
            int is_album_art = 0;
            mpv_get_property(state.mpv, std::format("track-list/{}/image", i).c_str(), MPV_FORMAT_FLAG, &is_image);
            mpv_get_property(state.mpv, std::format("track-list/{}/albumart", i).c_str(), MPV_FORMAT_FLAG, &is_album_art);
            if (is_image == 0 && is_album_art == 0)
                state.media_has_video = true;
        }
    }

    std::string media_artist = get_audio_artist(state.mpv);
    std::string media_title = get_mpv_osd_string(state.mpv, "media-title");

    state.album = get_mpv_osd_string(state.mpv, "metadata/by-key/album");
    state.channel = get_mpv_osd_string(state.mpv, "metadata/by-key/uploader");
    state.media_artist = std::move(media_artist);
    state.media_title = std::move(media_title);
    state.track_title = state.media_has_video ? "" : get_mpv_string(state.mpv,"metadata/by-key/title");

    // Get full file path for folder filtering
    state.current_file_path = get_mpv_string(state.mpv, "path");

    const bool remote = state.current_file_path.find("://") != std::string::npos;
    state.scope_path = remote ? std::format("stream-{:016x}", compute_fnv1a64(state.current_file_path.data(),
        state.current_file_path.size())) : normalize_path(state.current_file_path);
    read_profiles(state);
    std::string filename_str = get_mpv_string(state.mpv, "filename");
    if (filename_str.empty() && !state.current_file_path.empty())
    {
        filename_str = path_utf8(fs::u8path(state.current_file_path).filename());
    }

    // Bump file ID and reset anime state
    int current_load_id = ++state.file_load_id;
    {
        std::lock_guard<std::mutex> lock(state.anime_mutex);
        state.is_anime = false;
        state.anime_anilist_id = 0;
    }

    // Reset pause state and activity tracking for new file
    state.last_reported_pos = -1.0;
    state.last_playback_paused = -1;
    state.last_published_paused = -1;
    state.pause_start_epoch_ms = 0;
    state.pause_start_time = chrono::steady_clock::time_point{};
    state.rpc_hidden_by_timeout = false;
    state.force_update = true;

    // Claim active instance if eligible
    if (is_window_eligible(state))
    {
        if (claim_active_instance(state.instance_id))
        {
            state.last_heartbeat = chrono::steady_clock::now();
            state.was_active_instance = true;
        }
        else
        {
            state.was_active_instance = false;
        }
    }

    // Only dispatch background anime lookup if eligible video file
    if (state.media_has_video && lookup_allowed(state) && state.provider.empty() && use_anilist(state))
    {
        std::lock_guard<std::mutex> lock(state.worker_mutex);
        state.pending_load_id = current_load_id;
        state.pending_filename = filename_str;
        state.pending_folder = path_utf8(fs::u8path(state.current_file_path).parent_path().filename());
        state.pending_correction = state.correction;
        state.pending_correction["automatic"] = state.sources.value("source",std::string("auto")) == "auto";
        state.has_pending_lookup = true;
        state.worker_cv.notify_one();
    }
}

// Discord callbacks

void on_discord_log(Discord_String msg, Discord_LoggingSeverity severity, void* payload)
{
    auto& state = *(rich_presence_state*)payload;

    auto severity_str = ""s;
    switch (severity)
    {
    case Discord_LoggingSeverity_Verbose:
        severity_str = "Verbose";
        break;
    case Discord_LoggingSeverity_Info:
        severity_str = "Info";
        break;
    case Discord_LoggingSeverity_Warning:
        severity_str = "Warning";
        break;
    case Discord_LoggingSeverity_Error:
        severity_str = "Error";
        break;
    case Discord_LoggingSeverity_None:
        severity_str = "None";
        break;
    default:
        severity_str = "unknown";
        break;
    }

    auto msg_view = std::string { reinterpret_cast<const char*>(msg.ptr), msg.size };
    state.discord_api->Discord_Free(msg.ptr);
    mpv_print(state.mpv, std::format("[{}] {}", severity_str, msg_view));
}

void on_discord_update_rich_presence(Discord_ClientResult* result, void* payload)
{
    auto& state = *(rich_presence_state*)payload;

    const auto success = state.discord_api->Discord_ClientResult_Successful(result);
    const auto code = success ? 0 : state.discord_api->Discord_ClientResult_ErrorCode(result);
    state.discord_api->Discord_ClientResult_Drop(result);
    if (success)
    {
        state.last_sdk = "Published";
        state.is_rpc_visible = true;
        state.retry_backoff_ms = 0;
        state.needs_retry = false;
        return;
    }

    state.last_sdk = std::format("Error {}", code);
    mpv_print(state.mpv, std::format("Rich presence error: {}", code));
    state.last_error_time = chrono::steady_clock::now();
    state.retry_backoff_ms = (state.retry_backoff_ms == 0) ? 1000 : (std::min)(state.retry_backoff_ms * 2, 16000);
    state.needs_retry = true;
}

// Main plugin entry

auto mpv_open_cplugin_impl(mpv_handle* ctx) -> int
{
    auto state = rich_presence_state {};
    state.mpv = ctx;
    state.instance_id = generate_instance_id();

    // Start background anime lookup worker
    state.worker_thread = std::thread(anime_worker_loop, std::ref(state));

    // Initialization
    mpv_print(state.mpv, "Pinging mpv_rich_presence for config data...");
    auto ping_args = std::array<const char*, 4> { "script-message-to", "mpv_rich_presence", "ping", nullptr };
    mpv_command(state.mpv, ping_args.data());

    std::atomic<bool> is_ready = false;


    mpv_print(state.mpv, "Initializing Discord Social SDK...");
    auto discord_init = std::jthread { [&state, &is_ready]() {
        try
        {
            auto embedded_fs = cmrc::mpvrp::get_filesystem();
            auto filename = (*embedded_fs.iterate_directory(DISCORD_SDK_DIR).begin()).filename();
            auto embedded_sdk = embedded_fs.open(std::format("{}/{}", DISCORD_SDK_DIR, filename));

            uint64_t sdk_hash = compute_fnv1a64(embedded_sdk.begin(), embedded_sdk.size());
            auto private_dir = get_default_lock_directory();
            if (private_dir.empty()) throw std::runtime_error("No private runtime directory available");
            auto target_dir = private_dir / std::format("sdk_{:016x}", sdk_hash);
            std::error_code ec;
            fs::create_directories(target_dir, ec);

            auto temp_sdk_path = target_dir / filename;
            auto matches_embedded = [&]() {
                if (fs::file_size(temp_sdk_path, ec) != embedded_sdk.size() || ec) return false;
                std::ifstream input(temp_sdk_path, std::ios::binary);
                std::array<char, 16384> buffer;
                size_t offset = 0;
                while (input && offset < embedded_sdk.size())
                {
                    auto count = (std::min)(buffer.size(), embedded_sdk.size()-offset);
                    if (!input.read(buffer.data(), count) ||
                        !std::equal(buffer.begin(),buffer.begin()+count,embedded_sdk.begin()+offset)) return false;
                    offset += count;
                }
                return offset == embedded_sdk.size();
            };

            if (!matches_embedded())
            {
                auto tmp_file = target_dir / std::format("{}.tmp.{}", filename, state.instance_id);
                try
                {
                    std::ofstream temp_sdk;
                    temp_sdk.exceptions(std::ios::failbit | std::ios::badbit);
                    temp_sdk.open(tmp_file, std::ios_base::binary | std::ios_base::trunc);
                    temp_sdk.write(embedded_sdk.begin(), embedded_sdk.size());
                    temp_sdk.flush();
                    temp_sdk.close();

                    fs::rename(tmp_file, temp_sdk_path, ec);
                    if (ec)
                    {
                        if (!matches_embedded())
                        {
                            mpv_print(state.mpv, std::format("Failed to write Discord SDK: {}", ec.message()));
                            fs::remove(tmp_file, ec);
                            return;
                        }
                        fs::remove(tmp_file, ec);
                    }
                }
                catch (const std::exception& e)
                {
                    fs::remove(tmp_file, ec);
                    mpv_print(state.mpv, std::format("Failed to write Discord SDK: {}", e.what()));
                    return;
                }
            }

            auto sdk = dll::shared_library { fs::absolute(temp_sdk_path).string() };
            state.discord_api = std::make_shared<discord_api_importer>(sdk);

            state.discord = std::make_unique<discord_client>(state.discord_api);
            state.discord_api->Discord_Client_AddLogCallback(&state.discord->get(), on_discord_log, nullptr, &state, Discord_LoggingSeverity_Warning);

            is_ready = true;
        }
        catch (const std::exception& e)
        {
            mpv_print(state.mpv, std::format("Discord SDK initialization failed: {}", e.what()));
        }
        catch (...)
        {
            mpv_print(state.mpv, "Discord SDK initialization failed with unknown error");
        }
    } };

    // Observe properties
    mpv_observe_property(state.mpv, 1, "metadata", MPV_FORMAT_NONE);
    mpv_observe_property(state.mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(state.mpv, 0, "seeking", MPV_FORMAT_FLAG);
    mpv_observe_property(state.mpv, 0, "speed", MPV_FORMAT_DOUBLE);
    mpv_observe_property(state.mpv, 0, "paused-for-cache", MPV_FORMAT_FLAG);
    mpv_observe_property(state.mpv, 0, "playtime-remaining", MPV_FORMAT_DOUBLE);

    auto hide_rich_presence = [&]() {
        if (state.is_rpc_visible && is_ready)
        {
            state.discord_api->Discord_Client_ClearRichPresence(&state.discord->get());
            state.is_rpc_visible = false;
        }
    };

    auto next_sample = chrono::steady_clock::time_point{};
    auto next_ask_check = chrono::steady_clock::time_point{};
    bool service_started = false;
    bool observed = false;
    std::string observed_source, observed_title, observed_track_title, observed_artist, observed_album;
    nlohmann::json automatic = nlohmann::json::object();
    uint64_t observed_session = UINT64_MAX;
    bool last_allowed = false;
    bool last_sample_active = false, last_watch_episode = false;
    std::string last_sample_episode;
    int last_sample_id = -1;
    double last_sample_speed = 1;
    uint64_t sampled_seek = 0;
    uint64_t last_seek = 0;
    // Main event loop

    while (true)
    {
        auto* event = mpv_wait_event(state.mpv, SLEEP_DURATION.count() * 1e-3);
        bool should_shutdown = false;
        while (event && event->event_id != MPV_EVENT_NONE)
        {
            if (event->event_id == MPV_EVENT_SHUTDOWN)
            {
                should_shutdown = true;
                break;
            }

            if (event->event_id == MPV_EVENT_PROPERTY_CHANGE)
            {
                const auto* change = static_cast<mpv_event_property*>(event->data);
                if (change && change->name)
                {
                    std::string_view name(change->name);
                    if (name=="speed" || name=="pause" || name=="paused-for-cache" || name=="seeking") ++last_seek;
                }
            }
            if (event->event_id == MPV_EVENT_SEEK || event->event_id == MPV_EVENT_PLAYBACK_RESTART) ++last_seek;
            if (event->event_id == MPV_EVENT_PROPERTY_CHANGE && event->reply_userdata == 1 && !state.current_file_path.empty())
            {
                auto title = get_mpv_osd_string(state.mpv, "media-title");
                auto artist = get_audio_artist(state.mpv);
                auto album = get_mpv_osd_string(state.mpv,"metadata/by-key/album");
                auto track_title = state.media_has_video ? "" : get_mpv_string(state.mpv,"metadata/by-key/title");
                if (title != state.media_title || track_title != state.track_title ||
                    artist != state.media_artist || album != state.album)
                {
                    state.media_title = title;
                    state.track_title = std::move(track_title);
                    state.media_artist = artist;
                    state.album = album;
                    state.force_update = true;
                }
            }
            if (event->event_id == MPV_EVENT_CLIENT_MESSAGE)
            {
                handle_client_message(state, (mpv_event_client_message*)event->data);
            }
            else if (event->event_id == MPV_EVENT_FILE_LOADED)
            {
                handle_file_loaded(state);
            }
            else if (event->event_id == MPV_EVENT_END_FILE)
            {
                publish_ask_offer(state, {});
                ++state.file_load_id;
                ++state.file_session;
                state.folder_exception.clear();
                state.media_has_audio = false;
                state.media_has_video = false;
                state.media_title.clear();
                state.track_title.clear();
                state.media_artist.clear();
                state.current_file_path.clear();
                state.scope_path.clear();
                {
                    std::lock_guard<std::mutex> lock(state.anime_mutex);
                    state.is_anime = false;
                    state.anime_anilist_id = 0;
                }
                hide_rich_presence();
            }

            event = mpv_wait_event(state.mpv, 0);
        }

        if (should_shutdown)
            break;

        auto now_steady = chrono::steady_clock::now();

        // Playback state inspection across all instances
        double media_time_pos_s = 0.0;
        double media_time_left_s = 0.0;
        int is_media_paused = 0;
        int is_seeking = 0;
        int is_caching = 0;
        double current_speed = 1.0;
        double playtime_left_s = 0.0;

        mpv_get_property(state.mpv, "seeking", MPV_FORMAT_FLAG, &is_seeking);
        mpv_get_property(state.mpv, "time-pos/full", MPV_FORMAT_DOUBLE, &media_time_pos_s);
        mpv_get_property(state.mpv, "time-remaining/full", MPV_FORMAT_DOUBLE, &media_time_left_s);
        mpv_get_property(state.mpv, "pause", MPV_FORMAT_FLAG, &is_media_paused);
        mpv_get_property(state.mpv, "paused-for-cache", MPV_FORMAT_FLAG, &is_caching);

        if (mpv_get_property(state.mpv, "speed", MPV_FORMAT_DOUBLE, &current_speed) < 0 || current_speed <= 0.01)
            current_speed = 1.0;
        state.playback_speed = current_speed;

        if (mpv_get_property(state.mpv, "playtime-remaining", MPV_FORMAT_DOUBLE, &playtime_left_s) < 0 || playtime_left_s <= 0.0)
        {
            if (media_time_left_s > 0.0)
                playtime_left_s = media_time_left_s / current_speed;
            else
                playtime_left_s = 0.0;
        }

        playback media;
        media.video = state.media_has_video;
        media.paused = is_media_paused;
        media.buffering = is_caching;
        media.artist = state.media_artist;
        media.album = state.album;
        media.channel = state.channel;
        media.title = state.media_title;
        media.position = media_time_pos_s;
        media.remaining = media_time_left_s;
        media.speed = current_speed;
        media.playtime_left = playtime_left_s;
        media.now = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
        media.session_start = state.session_start;
        int completed_anime_lookup;
        int conflicting_anime_minutes = 0;
        bool fuzzy_episode_match = false;
        int anime_duration_minutes = 0;
        bool watch_episode = false;
        std::string episode_review;
        {
            std::lock_guard lock(state.anime_mutex);
            completed_anime_lookup = state.completed_anime_lookup;
            media.anime = state.is_anime;
            if (media.anime)
            {
                watch_episode = state.watch_episode;
                episode_review = state.episode_review;
                conflicting_anime_minutes = state.conflicting_anime_minutes;
                fuzzy_episode_match = state.fuzzy_episode_match;
                anime_duration_minutes = state.anime_duration_minutes;
                media.anime_title = state.anime_title;
                media.english = state.anime_english;
                media.romaji = state.anime_romaji;
                media.native = state.anime_native;
                media.episode = state.anime_episode;
                media.episode_label = state.episode_label;
                media.raw_episode = state.anime_episode_raw;
                media.total = state.anime_episode_total;
                media.group = state.anime_release_group;
                media.cover = state.anime_cover_url;
                media.anilist_id = state.anime_anilist_id;
            }
        }

        // Track playback transitions independently of ownership or publishing
        bool transitioned_to_playing = (state.last_playback_paused == 1 && is_media_paused == 0);
        bool transitioned_to_paused = (state.last_playback_paused != 1 && is_media_paused == 1);
        bool buffering_changed = (is_caching != state.last_caching_state);

        state.last_playback_paused = is_media_paused;
        state.last_caching_state = is_caching;
        if (buffering_changed)
            state.force_update = true;

        if (transitioned_to_paused || (is_media_paused == 1 && state.pause_start_epoch_ms == 0))
        {
            state.pause_start_time = now_steady;
            state.pause_start_epoch_ms = chrono::duration_cast<chrono::milliseconds>(
                chrono::system_clock::now().time_since_epoch()).count();
        }

        media.pause_start = state.pause_start_epoch_ms;

        if (state.store)
        {
            const auto pin = state.store->get("pin","current");
            const auto owner = pin.value("owner",std::string{});
            const auto expires = pin.value("expires",uint64_t{});
            state.pinned_elsewhere = !owner.empty() && owner != state.instance_id && expires > media.now;
            state.pinned_here = owner == state.instance_id && expires > media.now;
            if (state.pinned_here && expires < media.now + 4000)
                state.store->renew_pin(state.instance_id,media.now+6000);
        }

        // Handover on resume: unpausing an eligible instance claims ownership immediately
        bool eligible = is_window_eligible(state);
        if (eligible && transitioned_to_playing)
        {
            if (claim_active_instance(state.instance_id))
            {
                state.last_heartbeat = now_steady;
                state.was_active_instance = true;
                state.force_update = true;
                state.rpc_hidden_by_timeout = false;
            }
            else
            {
                state.was_active_instance = false;
            }
        }

        // Track ownership transitions
        bool currently_active = is_active_instance(state.instance_id);
        if (currently_active && !state.was_active_instance)
        {
            // Regained active ownership: force an immediate update and lease renewal
            state.force_update = true;
            state.last_heartbeat = chrono::steady_clock::time_point{};
        }
        state.was_active_instance = currently_active;

        if (state.store && state.store_version != state.store->version())
        {
            auto response = state.store->get("response",state.instance_id);
            auto message = response.value("notice",std::string{});
            if (!message.empty() && message != state.last_notice)
            {
                state.last_notice = message;
                const auto page = response.value("notice_page",std::string{});
                const auto context = response.find("context");
                if ((context == response.end() || context->is_null() || *context == state.file_session)
                    && (!page.empty() || local_permission(state)))
                {
                    const auto notice = response.value("message",std::string{});
                    const char* args[] = {"script-message-to","mpv_rich_presence","notice",notice.c_str(),page.c_str(),nullptr};
                    mpv_command(state.mpv,args);
                }
            }
            auto epoch = state.store->get("generation","cache").value("id",std::string{});
            const bool cache_cleared = epoch != state.cache_epoch;
            if (cache_cleared)
            {
                state.cache_epoch = epoch;
                clear_anilist_cache();
            }
            auto old = state.correction;
            auto old_appearance = state.appearance;
            auto old_privacy = state.privacy;
            bool old_block = state.hard_block;
            auto old_provider = state.provider;
            auto old_sources = state.sources;
            auto old_filter = state.folder_filter;
            bool was_allowed = lookup_allowed(state);
            read_profiles(state);
            if (old_filter != state.folder_filter)
            {
                state.folder_exception.clear();
                state.folder_session_exception = false;
            }
            if (old_sources != state.sources || old != state.correction)
            {
                ++state.file_load_id;
                std::lock_guard lock(state.anime_mutex);
                state.is_anime = false;
                state.anime_anilist_id = 0;
            }
            if (old_sources != state.sources || old != state.correction ||
                was_allowed != lookup_allowed(state)) on_eligibility_changed(state);
            if (old_block != state.hard_block || old_appearance != state.appearance || old_privacy != state.privacy || old != state.correction || old_provider != state.provider || old_sources != state.sources)
            {
                state.force_update = true;
                continue;
            }
        }
        const bool watch = lookup_allowed(state) && state.tracking.value("mode", std::string("off")) != "off";
        bool allowed = lookup_allowed(state);
        auto source = state.sources.value("source",std::string("auto"));
        const auto load_id = state.file_load_id.load();
        const bool anime_requested = state.media_has_video && use_anilist(state) &&
            state.pending_load_id == load_id;
        const bool anime_pending = anime_requested && completed_anime_lookup != load_id;
        const bool anime_unavailable = anime_requested && !anime_pending && state.failed_anime_lookup == load_id;
        const bool pinned_anime = state.correction.contains("id") && !state.correction.value("not_anime", false);
        const double duration_estimate = media.position + media.remaining;
        const auto route = route_recognition(source, allowed, bool(state.store), pinned_anime,
            !state.provider.empty(), anime_pending, anime_unavailable, media.anime,
            completed_anime_lookup == load_id, fuzzy_episode_match, anime_duration_minutes,
            conflicting_anime_minutes, duration_estimate);
        source = route.source;
        const bool extra = source != "original";
        if (!media.video && !state.scope_path.starts_with("stream-") &&
            (source == "auto" || source == "musicbrainz" || source == "acoustid"))
            media.track_title = state.track_title;
        const auto candidate = extra && state.store ? state.store->get("auto",state.instance_id) : nlohmann::json::object();
        const bool resolved = candidate.value("session",uint64_t{}) == state.file_session &&
            candidate.value("source",std::string{}) == source && candidate.value("title",std::string{}) == media.title &&
            candidate.value("track_title",std::string{}) == media.track_title &&
            candidate.value("artist",std::string{}) == media.artist && candidate.value("album",std::string{}) == media.album;
        if (resolved) media.provider = candidate.value("hit",nlohmann::json::object());
        if (route.hides_anime(resolved, !media.provider.empty(), candidate.value("unavailable", false)))
        {
            media.anime = false;
            media.anilist_id = 0;
            media.episode.clear();
            media.anime_title.clear();
            watch_episode = false;
            episode_review.clear();
        }
        if (candidate != automatic) { state.force_update = true; automatic = candidate; }
        const int tracked_media = media.anilist_id;
        const auto tracked_episode = media.episode;
        const bool advancing = watch && !media.paused && !media.buffering && !is_seeking;
        if (state.store && ((watch && now_steady >= next_sample) || observed_session != state.file_session
            || observed_source != source || (extra && (observed_title != media.title || observed_track_title != media.track_title ||
                observed_artist != media.artist || observed_album != media.album))
            || last_allowed != allowed || observed != watch || last_sample_id != tracked_media
            || last_sample_episode != tracked_episode || last_watch_episode != watch_episode
            || (watch && (advancing != last_sample_active || sampled_seek != last_seek || last_sample_speed != media.speed))))
        {
            next_sample = now_steady + chrono::seconds(1);
            auto seconds = chrono::duration<double>(now_steady.time_since_epoch()).count();
            nlohmann::json sample = {{"session",state.file_session},{"media",tracked_media},
                {"episode",tracked_episode},
                {"watch_episode",watch_episode},{"total",media.total},{"pos",media.position},{"seek",last_seek},
                {"scope_path",state.scope_path},{"path",allowed ? state.scope_path : ""},{"speed",media.speed},{"active",advancing},
                {"allowed",allowed},{"watch",watch},{"tracking",state.tracking},{"time",seconds},{"wall",media.now},
                {"match_title",media.anime_title}};
            sample["source"] = source;
            if (extra)
            {
                sample["title"] = media.title;
                sample["artist"] = media.artist;
                sample["album"] = media.album;
                if (!media.video)
                {
                    sample["track_title"] = media.track_title;
                    auto album_artist = get_mpv_string(state.mpv,"metadata/by-key/album_artist");
                    if (album_artist.empty()) album_artist = get_mpv_string(state.mpv,"metadata/by-key/albumartist");
                    sample["album_artist"] = std::move(album_artist);
                    sample["release_date"] = get_mpv_string(state.mpv,"metadata/by-key/date");
                }
                sample["video"] = media.video;
                sample["remote"] = state.scope_path.starts_with("stream-");
                if (!sample["remote"].get<bool>()) sample["filename"] = get_mpv_string(state.mpv, "filename");
                double duration_s = 0;
                mpv_get_property(state.mpv,"duration",MPV_FORMAT_DOUBLE,&duration_s);
                sample["duration"] = duration_s;
                if (source == "verify-tv") sample["anime_runtime_conflict"] = route.runtime_conflict;
                sample["recording"] = get_mpv_string(state.mpv,"metadata/by-key/MUSICBRAINZ_TRACKID");
                sample["release"] = get_mpv_string(state.mpv,"metadata/by-key/MUSICBRAINZ_ALBUMID");
                sample["tmdb"] = get_mpv_string(state.mpv,"metadata/by-key/TMDB");
                if (source == "stream" || (source == "auto" && state.scope_path.starts_with("stream-")))
                {
                    static const std::regex page(R"(^https://(?:www\.)?(?:youtube\.com/watch\?v=[A-Za-z0-9_-]{11}|youtu\.be/[A-Za-z0-9_-]{11}|twitch\.tv/[A-Za-z0-9_]+)$)");
                    if (std::regex_match(state.current_file_path,page)) sample["page"] = state.current_file_path;
                    sample["channel"] = media.channel;
                    sample["live"] = duration_s <= 0;
                }
            }
            state.store->put("player",state.instance_id,sample);
            observed_source = source;
            observed_title = media.title;
            observed_track_title = media.track_title;
            observed_artist = media.artist;
            observed_album = media.album;
            sampled_seek = last_seek;
            last_sample_speed = media.speed;
            last_sample_id = tracked_media;
            last_sample_active = advancing;
            last_sample_episode = tracked_episode;
            last_watch_episode = watch_episode;
            observed_session = state.file_session;
            last_allowed = allowed;
            observed = watch;
            if ((extra || watch) && !service_started)
            {
                const char* args[] = {"script-message-to","mpv_rich_presence","start-service",state.instance_id.c_str(),nullptr};
                mpv_command(state.mpv,args);
                service_started = true;
            }
        }
        if (!state.provider.empty() && !media.anime)
        {
            media.provider = state.provider;
        }
        if (now_steady >= next_ask_check && (watch_episode || !episode_review.empty() || !state.ask_offer.empty()))
        {
            next_ask_check = now_steady + chrono::milliseconds(250);
            publish_ask_offer(state, current_ask_proposal(state));
        }
        auto options = state.appearance;
        if (state.privacy.value("mode", std::string("full")) == "generic") options = {{"preset", "private"}};
        if (!state.snapshot_request.empty())
        {
            auto draft = state.replace_draft && state.privacy.value("mode",std::string("full")) != "generic" ? nlohmann::json::object() : options;
            if (valid_settings("appearance",state.preview) && state.privacy.value("mode", std::string("full")) != "generic")
            {
                for (const auto& [key,value] : state.preview.items())
                    if (key.ends_with("_layout") && value.is_object() && value.contains("preset")) draft.erase(key);
                draft.merge_patch(state.preview);
            }
            auto account = state.store ? state.store->get("account","current") : nlohmann::json::object();
            auto episode_watch = nlohmann::json::object();
            const int viewer = account.value("id",0);
            if (state.store && state.panel_page=="AniList" && allowed && viewer>0 && watch_episode)
            {
                const auto record = state.store->episode_watch(viewer,media.anilist_id,media.episode);
                if (state.tracking.value("resume",true) ||
                    (record.value("owner",std::string{})==state.instance_id &&
                    record.value("session",uint64_t{})==state.file_session))
                {
                    episode_watch = {{"elapsed",record.value("elapsed",0.0)},
                        {"first_watched",record.value("first_watched",0.0)}, {"last_watched",record.value("last_watched",0.0)}};
                }
            }
            auto snapshot = nlohmann::json{
                {"session", state.file_session}, {"instance", state.instance_id}, {"allowed", lookup_allowed(state)},
                {"configured", state.config_received}, {"owner", eligible && currently_active},{"pinned",state.pinned_here},{"pinned_elsewhere",state.pinned_elsewhere}, {"sdk", state.last_sdk},
                {"match", media.anilist_id}, {"episode", media.episode}, {"total", media.total}, {"title", media.anime_title},
                {"appearance", state.appearance}, {"privacy", state.privacy}, {"tracking", state.tracking},
                {"private", state.private_session}, {"hidden_file", state.hidden_file}, {"correction", state.correction},
                {"hidden_until_exit",state.hidden_until_exit},{"track_allowed",watch},{"appearance_scope",state.appearance_scope},
                {"match_scope",state.match_scope},{"source_scope",state.source_scope},
                {"has_local_folder", !state.scope_path.empty() && !state.scope_path.starts_with("stream-")},
                {"folder_filter", {
                    {"mode",state.folder_filter.value("mode",state.folder_filter_mode)},
                    {"folders",state.folder_filter.contains("folders") ? state.panel_folders : state.folder_list},
                    {"inherited",state.folder_filter.empty()},
                    {"passes",passes_configured_folders(state)},
                    {"current",state.scope_path.empty() || state.scope_path.starts_with("stream-") ? "" :
                        path_utf8(fs::u8path(state.scope_path).parent_path())},
                    {"exception",state.folder_session_exception ? "window" : folder_exception_active(state) ? "file" : "off"}
                }},
                {"service",state.store ? state.store->get("response",state.instance_id) : nlohmann::json::object()},
                {"account",std::move(account)},{"watch_episode",watch_episode},
                {"episode_review",episode_review},
                {"recognition_status", !allowed ? "Blocked" : anime_pending || (extra && !resolved) ? "Searching" :
                    media.anime || !media.provider.empty() || !state.provider.empty() ? "Matched" :
                    anime_unavailable || (resolved && candidate.value("unavailable", false)) ? "Provider unavailable" :
                    "Local title or tags"},
                {"episode_watch",std::move(episode_watch)},
                {"panel_position",state.store ? state.store->get("panel","global").value("position",std::string{}) : ""},
                {"pending",state.store && (state.panel_page=="AniList" || state.panel_page=="Pending updates") ?
                    state.store->pending() : nlohmann::json::array()},
                {"sources",state.sources},{"provider",media.anime ? "anilist" :
                    media.provider.value("provider",std::string(media.track_title.empty() ? "local" : "local tags"))},
                {"hard_block",state.hard_block},
                {"record_counts",state.store && (state.panel_page.starts_with("Saved ") || !state.panel_records.empty()) ?
                    state.store->record_counts() : nlohmann::json::object()},
                {"records",state.store && !state.panel_records.empty() ? state.store->records(state.panel_records) : nlohmann::json::array()},
                {"reason", !state.config_received ? "Waiting for configuration" : state.store_failed ? "Saved data unavailable" :
                    state.private_session ? "Private session" : !state.is_enabled.value_or(false) ? "Sharing off" :
                    !state.application_id ? "Set a Discord application ID" :
                    !state.scope_path.empty() && !folder_exception_active(state) && !passes_configured_folders(state) ? "Blocked by folder filter" :
                    !local_permission(state) ? "No eligible media" :
                    sharing_hidden(state) ? "Hidden" : state.pinned_elsewhere ? "Another window is pinned" : !currently_active ? "Another window owns Discord" :
                    state.rpc_hidden_by_timeout ? "Pause timeout" : !is_ready ? "Waiting for Discord SDK" : "Sharing"},
                {"card", format_activity(media, draft)}, {"nonce", state.snapshot_request}
            }.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            const char* args[] = {"script-message-to", "mpv_rich_presence", "snapshot", snapshot.c_str(), nullptr};
            mpv_command(state.mpv, args);
            state.snapshot_request.clear();
        }

        if (!is_ready)
            continue;

        state.discord_api->Discord_RunCallbacks();

        if (state.discord_api->Discord_Client_GetApplicationId(&state.discord->get()) != state.application_id)
            state.discord_api->Discord_Client_SetApplicationId(&state.discord->get(), state.application_id);


        if (!eligible || !currently_active)
        {
            hide_rich_presence();
            continue;
        }

        // Heartbeat renewal or lease acquisition
        if (now_steady - state.last_heartbeat >= chrono::seconds(2))
        {
            if (acquire_or_renew_active_instance(state.instance_id))
            {
                state.last_heartbeat = now_steady;
            }
            else
            {
                state.was_active_instance = false;
                hide_rich_presence();
                continue;
            }
        }

        // Skip update during seek
        if (is_seeking == 1)
            continue;

        // Pause timeout handling
        if (is_media_paused == 1)
        {
            if (state.pause_timeout_seconds > 0)
            {
                auto pause_elapsed = now_steady - state.pause_start_time;
                if (pause_elapsed >= chrono::seconds(state.pause_timeout_seconds))
                {
                    state.rpc_hidden_by_timeout = true;
                    hide_rich_presence();
                    continue;
                }
            }
        }
        else
        {
            state.rpc_hidden_by_timeout = false;
        }

        // Scheduled retry check on previous error
        bool retry_ready = false;
        if (state.needs_retry)
        {
            if (now_steady - state.last_error_time >= chrono::milliseconds(state.retry_backoff_ms))
                retry_ready = true;
            else
                continue;
        }

        // Update detection
        bool pause_changed = (is_media_paused != state.last_published_paused);
        bool anime_updated = state.anime_dirty.exchange(false);
        bool buffering_update = buffering_changed;

        bool seek_detected = false;
        if (state.last_reported_pos >= 0.0)
        {
            if (is_media_paused == 0 && is_caching == 0)
            {
                auto elapsed_s = chrono::duration_cast<chrono::milliseconds>(now_steady - state.last_activity_update).count() * 1e-3;
                double expected_pos = state.last_reported_pos + (elapsed_s * current_speed);
                if (std::abs(media_time_pos_s - expected_pos) > (0.5 * current_speed + 0.5) && (now_steady - state.last_activity_update >= 400ms))
                    seek_detected = true;
            }
            else
            {
                if (std::abs(media_time_pos_s - state.last_reported_pos) > 0.5 && (now_steady - state.last_activity_update >= 400ms))
                    seek_detected = true;
            }
        }

        bool periodic_update = (is_media_paused == 0 && is_caching == 0 && (now_steady - state.last_activity_update >= chrono::seconds(15)));
        bool first_update = (state.last_reported_pos < 0.0);
        bool forced = state.force_update;

        if (!forced && !retry_ready && !pause_changed && !anime_updated && !buffering_update && !seek_detected && !periodic_update && !first_update)
            continue;

        state.force_update = false;
        state.needs_retry = false;
        state.last_published_paused = is_media_paused;
        state.last_reported_pos = media_time_pos_s;
        state.last_activity_update = now_steady;

        publish_activity(state, format_activity(media, options), on_discord_update_rich_presence);
    }

    // Shut down gracefully
    mpv_print(state.mpv, "Shutting down...");

    if (state.store)
    {
        state.store->close_window(state.instance_id);
    }
    release_active_instance(state.instance_id);

    if (is_ready)
        state.discord_api->Discord_Client_ClearRichPresence(&state.discord->get());

    return EXIT_SUCCESS;
}

extern "C" MPV_EXPORT auto mpv_open_cplugin(mpv_handle* ctx) -> int
{
    try
    {
        return mpv_open_cplugin_impl(ctx);
    }
    catch (const std::exception& e)
    {
        mpv_print(ctx, std::format("Encountered exception: {}", e.what()));
    }
    return EXIT_FAILURE;
}
