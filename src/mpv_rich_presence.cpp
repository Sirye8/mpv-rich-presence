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

#include <anitomy/anitomy.h>
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
#include <codecvt>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <locale>
#include <mutex>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mpv_rich_presence/anilist_parser.hpp"
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

static std::wstring s2ws(const std::string& str)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
    return myconv.from_bytes(str);
}

static std::string ws2s(const std::wstring& str)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> myconv;
    return myconv.to_bytes(str);
}

static std::string generate_instance_id()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    return std::to_string(dist(gen));
}

static std::string get_ordinal_string(int n)
{
    if (n == 1) return "1st";
    if (n == 2) return "2nd";
    if (n == 3) return "3rd";
    return std::to_string(n) + "th";
}

struct parsed_filename_info
{
    std::string title;
    int season = 0;
    int part = 0;
    int episode = 0;
    std::string release_group;
};

// Extract season/part and strip tags
static std::tuple<std::string, int, int> clean_anime_title(const std::string& raw_title)
{
    std::string name = raw_title;
    int season = 0;
    int part = 0;

    // Check for S01E13
    static const std::regex re_se(R"(\bS(\d{1,2})E(\d{1,3})\b)", std::regex::icase);
    std::smatch m_se;
    if (std::regex_search(name, m_se, re_se))
    {
        try { season = std::stoi(m_se[1].str()); } catch (...) {}
        name = name.substr(0, m_se.position()) + name.substr(m_se.position() + m_se.length());
    }

    // Check for Part / Cour
    static const std::regex re_part(R"(\b(?:Part|Cour)\s*(\d{1,2})\b)", std::regex::icase);
    std::smatch m_part;
    if (std::regex_search(name, m_part, re_part))
    {
        try { part = std::stoi(m_part[1].str()); } catch (...) {}
        name = name.substr(0, m_part.position()) + name.substr(m_part.position() + m_part.length());
    }

    // Check for Season / Ordinal
    if (season == 0)
    {
        static const std::regex re_ord(R"(\b(\d+(?:st|nd|rd|th)|first|second|third|fourth|fifth|sixth)\s+Season\b)", std::regex::icase);
        std::smatch m_ord;
        if (std::regex_search(name, m_ord, re_ord))
        {
            std::string ord_match = to_lower(m_ord[1].str());
            if (ord_match == "1st" || ord_match == "first") season = 1;
            else if (ord_match == "2nd" || ord_match == "second") season = 2;
            else if (ord_match == "3rd" || ord_match == "third") season = 3;
            else if (ord_match == "4th" || ord_match == "fourth") season = 4;
            else if (ord_match == "5th" || ord_match == "fifth") season = 5;
            else if (ord_match == "6th" || ord_match == "sixth") season = 6;
            else
            {
                try {
                    season = std::stoi(ord_match.substr(0, ord_match.size() - 2));
                } catch (...) {}
            }
            name = name.substr(0, m_ord.position()) + name.substr(m_ord.position() + m_ord.length());
        }
        else
        {
            static const std::regex re_season_word(R"(\bSeason\s*(\d{1,2})\b)", std::regex::icase);
            std::smatch m_sw;
            if (std::regex_search(name, m_sw, re_season_word))
            {
                try { season = std::stoi(m_sw[1].str()); } catch (...) {}
                name = name.substr(0, m_sw.position()) + name.substr(m_sw.position() + m_sw.length());
            }
            else
            {
                static const std::regex re_s_num(R"(\bS(\d{1,2})\b)", std::regex::icase);
                std::smatch m_sn;
                if (std::regex_search(name, m_sn, re_s_num))
                {
                    try { season = std::stoi(m_sn[1].str()); } catch (...) {}
                    name = name.substr(0, m_sn.position()) + name.substr(m_sn.position() + m_sn.length());
                }
            }
        }
    }

    // Strip trailing episode notation
    static const std::regex re_trailing_ep(R"(\s*(?:[-#]|\bep(?:isode)?\.?)\s*0*(\d{1,4})\s*$)", std::regex::icase);
    std::smatch m_te;
    if (std::regex_search(name, m_te, re_trailing_ep))
    {
        name = name.substr(0, m_te.position());
    }

    // Strip resolutions
    static const std::regex re_resolutions(
        R"(\b(?:\d{3,4}x\d{3,4}|4320p?|2160p?|1440p?|1080[pi]?|720[pi]?|576[pi]?|540p?|480[pi]?|360p?|240p?|8k|4k|2k|uhd|fhd|qhd)\b)",
        std::regex::icase
    );
    name = std::regex_replace(name, re_resolutions, " ");

    // Strip video/audio tags and codecs
    static const std::regex re_video_tags(
        R"(\b(?:bd-?rip|brrip|blu-?ray|dvd-?rip|dvd[59]?|r2dvd|hdtv-?rip|hdtv|tvrip|web-?dl|webrip|web|crunchyroll|netflix|hevc|avc|av1|x265|x264|h265|h264|xvid|divx|hi10p?|10-?bit|8-?bit|flac|aac|ac3|eac3|dts(?:-hd)?|truehd|opus|dual-?audio|multi-?audio)\b)",
        std::regex::icase
    );
    name = std::regex_replace(name, re_video_tags, " ");

    // Clean leftover brackets and extra spaces
    static const std::regex re_brackets(R"([\(\[\]\)])");
    name = std::regex_replace(name, re_brackets, " ");
    static const std::regex re_dashes(R"(\s*-\s*)");
    name = std::regex_replace(name, re_dashes, " ");
    static const std::regex re_spaces(R"(\s+)");
    name = std::regex_replace(name, re_spaces, " ");

    auto start = name.find_first_not_of(" \t");
    auto end = name.find_last_not_of(" \t");
    if (start != std::string::npos && end != std::string::npos)
        name = name.substr(start, end - start + 1);
    else
        name.clear();

    return { name, season, part };
}

// Parse anime filename into title, season, part, episode, group
static parsed_filename_info parse_anime_filename(const std::string& raw_filename, const anitomy::Elements& anitomy_elements)
{
    parsed_filename_info info;

    // Release group
    if (anitomy_elements.count(anitomy::kElementReleaseGroup))
        info.release_group = ws2s(anitomy_elements.get(anitomy::kElementReleaseGroup));
    if (info.release_group.empty())
    {
        std::smatch m_grp;
        static const std::regex re_grp(R"(^\[([^\]]+)\])");
        if (std::regex_search(raw_filename, m_grp, re_grp))
            info.release_group = m_grp[1].str();
    }

    // Episode
    if (anitomy_elements.count(anitomy::kElementEpisodeNumber))
    {
        try { info.episode = std::stoi(ws2s(anitomy_elements.get(anitomy::kElementEpisodeNumber))); }
        catch (...) {}
    }
    if (info.episode == 0)
    {
        static const std::regex re_se_ep(R"(\bS\d{1,2}E(\d{1,3})\b)", std::regex::icase);
        std::smatch m_se;
        if (std::regex_search(raw_filename, m_se, re_se_ep))
        {
            try { info.episode = std::stoi(m_se[1].str()); } catch (...) {}
        }
        else
        {
            static const std::regex re_ep(R"([-]\s*(\d{1,4})(?:\s*v\d+)?\b)");
            std::smatch m_ep;
            if (std::regex_search(raw_filename, m_ep, re_ep))
            {
                try { info.episode = std::stoi(m_ep[1].str()); } catch (...) {}
            }
        }
    }

    // Season from anitomy
    if (anitomy_elements.count(anitomy::kElementAnimeSeason))
    {
        try { info.season = std::stoi(ws2s(anitomy_elements.get(anitomy::kElementAnimeSeason))); }
        catch (...) {}
    }

    // If Anitomy parsed a title, clean it up
    if (anitomy_elements.count(anitomy::kElementAnimeTitle))
    {
        std::string a_title = ws2s(anitomy_elements.get(anitomy::kElementAnimeTitle));

        // Strip trailing episode from Anitomy title if present
        static const std::regex re_trailing_dash_ep(R"(\s*(?:[-#]|\bep(?:isode)?\.?)\s*0*(\d{1,4})\s*$)", std::regex::icase);
        std::smatch m_te;
        if (std::regex_search(a_title, m_te, re_trailing_dash_ep))
        {
            if (info.episode == 0)
            {
                try { info.episode = std::stoi(m_te[1].str()); } catch (...) {}
            }
            a_title = a_title.substr(0, m_te.position());
        }

        auto [clean_title, s, p] = clean_anime_title(a_title);
        if (!clean_title.empty())
        {
            info.title = clean_title;
            if (info.season == 0) info.season = s;
            if (info.part == 0) info.part = p;
        }
    }

    // Fallback: parse title directly from filename
    if (info.title.empty())
    {
        std::string name = raw_filename;
        auto last_slash = name.find_last_of("/\\");
        if (last_slash != std::string::npos) name = name.substr(last_slash + 1);
        auto last_dot = name.find_last_of('.');
        if (last_dot != std::string::npos) name = name.substr(0, last_dot);

        // Strip leading release group
        static const std::regex re_grp(R"(^\[([^\]]+)\]\s*)");
        std::smatch m_g;
        if (std::regex_search(name, m_g, re_grp))
            name = name.substr(m_g.length());

        // Strip trailing bracketed tags
        static const std::regex re_any_bracket(R"((\s*[\(\[][^\)\]]*[\)\]]\s*)$)");
        while (true)
        {
            std::smatch m_tr;
            if (std::regex_search(name, m_tr, re_any_bracket))
                name = name.substr(0, m_tr.position());
            else
                break;
        }

        // Cut at SxxExx or episode delimiter
        static const std::regex re_cut(R"(\bS\d{1,2}E\d{1,3}\b|[-]\s*\d{1,4}\b)", std::regex::icase);
        std::smatch m_c;
        if (std::regex_search(name, m_c, re_cut))
            name = name.substr(0, m_c.position());

        auto [clean_title, s, p] = clean_anime_title(name);
        info.title = clean_title;
        if (info.season == 0) info.season = s;
        if (info.part == 0) info.part = p;
    }

    return info;
}



static bool sharing_hidden(const rich_presence_state& state)
{
    return state.hidden_file || state.hidden_until_exit || chrono::steady_clock::now() < state.hide_until
        || state.privacy.value("mode", std::string("full")) == "hidden";
}

static bool local_permission(const rich_presence_state& state)
{
    return state.config_received && !state.store_failed && !state.private_session
        && !state.hard_block && !state.privacy.value("block_lookup", false)
        && (state.media_has_audio || state.media_has_video)
        && passes_folder_filter(state.current_file_path, state.folder_filter_mode, state.folder_list);
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
    }
    if (!state.appearance.contains("preset")) state.appearance["preset"] = state.config_preset;
    if (!state.sources.contains("source")) state.sources["source"] = state.config_source;
}

// AniList API caching

static std::mutex anilist_cache_mutex;
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

static std::vector<anilist_media> search_anilist_page(const std::string& search, bool& confirmed)
{
    if (search.empty() || search.size() > 256 || search.find("://") != std::string::npos ||
        search.starts_with("/") || search.starts_with("\\") || (search.size() > 2 && search[1] == ':' &&
        (search[2] == '/' || search[2] == '\\')) ||
        (search.find('?') != std::string::npos && search.find('=') != std::string::npos))
    {
        confirmed = false;
        return {};
    }
    auto generation = cache_generation.load();
    {
        std::lock_guard<std::mutex> lock(anilist_cache_mutex);
        auto it = anilist_page_cache.find(search);
        if (it != anilist_page_cache.end() && it->second.expires > chrono::steady_clock::now())
            return it->second.value;
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

// Single title similarity check
static bool is_single_title_match(const std::string& query, const std::string& candidate_title)
{
    std::string q = to_lower(query);
    std::string t = to_lower(candidate_title);
    if (q.empty() || t.empty())
        return false;
    if (t.find(q) != std::string::npos || q.find(t) != std::string::npos)
        return true;

    auto tokenize = [](const std::string& s) {
        std::unordered_set<std::string> tokens;
        std::string cur;
        for (char c : s)
        {
            if (std::isalnum(static_cast<unsigned char>(c)))
                cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            else if (!cur.empty())
            {
                tokens.insert(cur);
                cur.clear();
            }
        }
        if (!cur.empty())
            tokens.insert(cur);
        return tokens;
    };

    auto q_tokens = tokenize(q);
    auto t_tokens = tokenize(t);
    if (q_tokens.empty() || t_tokens.empty())
        return false;

    int common = 0;
    for (const auto& w : q_tokens)
    {
        if (t_tokens.contains(w))
            common++;
    }

    double sim = static_cast<double>(common) / (std::max)(q_tokens.size(), t_tokens.size());
    return sim >= 0.4;
}

// Matches query against any title variant or synonym of an AniList media entry
static bool is_media_title_match(const std::string& query, const anilist_media& media)
{
    if (is_single_title_match(query, media.title))
        return true;

    for (const auto& alias : media.matching_titles)
    {
        if (is_single_title_match(query, alias))
            return true;
    }

    return false;
}

static bool is_anilist_tracking_series(const anilist_media& media)
{
    return media.format == "TV" || media.format == "ONA";
}

// Anime lookup logic processed in worker thread
static void process_anime_lookup(rich_presence_state& state, const std::string& filename_str, const std::string& file_path_str,
                                 int current_load_id, const nlohmann::json& correction, bool& confirmed)
{
    if (correction.value("not_anime", false) || filename_str.empty())
        return;

    if (state.worker_stop.load(std::memory_order_acquire) || state.file_load_id != current_load_id)
        return;

    anitomy::Anitomy anitomy;
    anitomy.Parse(s2ws(filename_str));
    auto& elements = anitomy.elements();

    // Check for anime indicators
    bool has_anime_indicator = elements.count(anitomy::kElementAnimeTitle)
        || elements.count(anitomy::kElementEpisodeNumber)
        || elements.count(anitomy::kElementAnimeSeason)
        || elements.count(anitomy::kElementReleaseGroup);

    if (!has_anime_indicator && !correction.contains("id"))
        return;

    parsed_filename_info pinfo = parse_anime_filename(filename_str, elements);
    if (pinfo.title.empty() && !correction.contains("id"))
        return;

    std::string a_title = pinfo.title;
    int parsed_season = pinfo.season;
    int parsed_part = pinfo.part;
    int parsed_ep = pinfo.episode;
    std::string a_group = pinfo.release_group;

    std::vector<std::string> search_queries;
    std::string ord_str = get_ordinal_string(parsed_season);

    if (parsed_season > 1)
    {
        if (parsed_part > 0)
        {
            search_queries.push_back(std::format("{} {} Season Part {}", a_title, ord_str, parsed_part));
            search_queries.push_back(std::format("{} Season {} Part {}", a_title, parsed_season, parsed_part));
        }
        search_queries.push_back(std::format("{} {} Season", a_title, ord_str));
        search_queries.push_back(std::format("{} Season {}", a_title, parsed_season));
        search_queries.push_back(std::format("{} Part {}", a_title, parsed_season));
    }
    else if (parsed_season == 1)
    {
        if (parsed_part > 0)
            search_queries.push_back(std::format("{} Part {}", a_title, parsed_part));
        search_queries.push_back(a_title);
    }
    else
    {
        if (parsed_part > 0)
            search_queries.push_back(std::format("{} Part {}", a_title, parsed_part));
        search_queries.push_back(a_title);
    }

    std::optional<anilist_media> candidate = std::nullopt;

    if (correction.contains("id"))
    {
        candidate = fetch_anilist_by_id(correction.at("id"));
        if (!candidate) return;
        search_queries.clear();
    }

    for (const auto& q_str : search_queries)
    {
        if (state.worker_stop.load(std::memory_order_acquire) || state.file_load_id != current_load_id)
            return;

        auto page = search_anilist_page(q_str, confirmed);
        if (page.empty())
            continue;

        std::optional<anilist_media> best = std::nullopt;
        for (const auto& m : page)
        {
            if (!is_media_title_match(a_title, m))
                continue;

            std::string title_lower = to_lower(m.title);

            if (parsed_season > 1 && page.size() > 1)
            {
                bool has_season = (title_lower.find(std::to_string(parsed_season)) != std::string::npos)
                                  || (title_lower.find(to_lower(ord_str)) != std::string::npos);
                if (!has_season)
                {
                    for (const auto& alt : m.matching_titles)
                    {
                        std::string alt_lower = to_lower(alt);
                        if ((alt_lower.find(std::to_string(parsed_season)) != std::string::npos)
                            || (alt_lower.find(to_lower(ord_str)) != std::string::npos))
                        {
                            has_season = true;
                            break;
                        }
                    }
                }
                if (!has_season)
                    continue;
            }

            if (parsed_part > 0)
            {
                bool has_part = (title_lower.find("part " + std::to_string(parsed_part)) != std::string::npos)
                                || (title_lower.find("cour " + std::to_string(parsed_part)) != std::string::npos);
                if (!has_part)
                {
                    for (const auto& alt : m.matching_titles)
                    {
                        std::string alt_lower = to_lower(alt);
                        if ((alt_lower.find("part " + std::to_string(parsed_part)) != std::string::npos)
                            || (alt_lower.find("cour " + std::to_string(parsed_part)) != std::string::npos))
                        {
                            has_part = true;
                            break;
                        }
                    }
                }
                if (!has_part)
                    continue;
            }

            if (parsed_ep > 0 && m.episodes > 0 && parsed_ep <= m.episodes)
            {
                best = m;
                break;
            }

            if (!best)
                best = m;
        }

        if (best)
        {
            candidate = best;
            break;
        }
    }

    if (!candidate)
    {
        if (state.worker_stop.load(std::memory_order_acquire) || state.file_load_id != current_load_id)
            return;

        auto base_page = search_anilist_page(a_title, confirmed);
        for (const auto& m : base_page)
        {
            if (is_media_title_match(a_title, m))
            {
                candidate = m;
                break;
            }
        }
    }

    if (!candidate)
        return;

    anilist_media current_resolved_media = *candidate;
    parsed_ep += correction.value("offset", 0);
    int current_resolved_ep = parsed_ep;
    bool unambiguous_numbering = true;

    // Walk sequels if episode number exceeds current season total
    if (parsed_ep > 0 && candidate->episodes > 0 && parsed_ep > candidate->episodes)
    {
        int curr_id = candidate->id;
        int remaining_ep = parsed_ep;
        std::unordered_set<int> visited;

        for (int step = 0; step < 20 && curr_id > 0; step++)
        {
            if (state.worker_stop.load(std::memory_order_acquire) || state.file_load_id != current_load_id)
                return;

            if (visited.contains(curr_id))
                break;
            visited.insert(curr_id);

            auto m_opt = (curr_id == candidate->id) ? candidate : fetch_anilist_by_id(curr_id);
            if (!m_opt)
                break;

            auto m = *m_opt;
            if (m.episodes > 0 && remaining_ep <= m.episodes)
            {
                current_resolved_media = m;
                current_resolved_ep = remaining_ep;
                break;
            }
            else if (m.episodes <= 0)
            {
                // Ongoing or unannounced total episodes
                current_resolved_media = m;
                current_resolved_ep = remaining_ep;
                break;
            }
            else
            {
                // remaining_ep > m.episodes: only advance after successfully fetching sequel
                if (m.sequel_id > 0)
                {
                    auto next_opt = fetch_anilist_by_id(m.sequel_id);
                    if (next_opt)
                    {
                        unambiguous_numbering = unambiguous_numbering && m.unambiguous_sequel
                            && is_anilist_tracking_series(m) && is_anilist_tracking_series(*next_opt);
                        remaining_ep -= m.episodes;
                        current_resolved_media = *next_opt;
                        current_resolved_ep = remaining_ep;
                        curr_id = m.sequel_id;
                        continue;
                    }
                    else
                    {
                        // Fetch failed: preserve last consistent media and episode
                        break;
                    }
                }
                else
                {
                    // No sequel exists: preserve last consistent media and episode
                    break;
                }
            }
        }
    }

    if (state.worker_stop.load(std::memory_order_acquire) || state.file_load_id != current_load_id)
        return;

    int conflicting_anime_minutes = 0;
    if (!correction.contains("id") && pinfo.episode == 0 && pinfo.season == 0 && pinfo.part == 0 &&
        current_resolved_media.format != "MOVIE" && current_resolved_media.duration_minutes > 0 &&
        current_resolved_media.duration_minutes <= 30 && current_resolved_media.release_year > 0)
    {
        const auto year = ws2s(elements.get(anitomy::kElementAnimeYear));
        const auto lower_title = to_lower(a_title);
        if (year.size() == 4 && year != std::to_string(current_resolved_media.release_year) &&
            std::none_of(current_resolved_media.matching_titles.begin(), current_resolved_media.matching_titles.end(),
                [&](const auto& alias) { return to_lower(alias) == lower_title; }))
            conflicting_anime_minutes = current_resolved_media.duration_minutes;
    }

    // Commit results under lock
    std::lock_guard<std::mutex> lock(state.anime_mutex);
    if (state.file_load_id == current_load_id)
    {
        const bool integer_episode = elements.count(anitomy::kElementEpisodeNumber) == 1 &&
            std::regex_match(ws2s(elements.get(anitomy::kElementEpisodeNumber)), std::regex("[0-9]+"));
        state.episode_label.clear();
        if (!integer_episode && elements.count(anitomy::kElementEpisodeNumber)>0)
        {
            for (const auto& value : elements.get_all(anitomy::kElementEpisodeNumber))
            {
                if (!state.episode_label.empty()) state.episode_label += "-";
                state.episode_label += ws2s(value);
            }
        }
        if (integer_episode && parsed_ep==0) state.episode_label="0";
        const bool eligible_episode = integer_episode && parsed_ep > 0 && current_resolved_ep > 0
            && (current_resolved_media.episodes <= 0 || current_resolved_ep <= current_resolved_media.episodes)
            && is_anilist_tracking_series(current_resolved_media);
        state.watch_episode = eligible_episode && unambiguous_numbering;
        state.is_anime = true;
        state.conflicting_anime_minutes = conflicting_anime_minutes;
        state.anime_title = current_resolved_media.title;
        state.anime_english = current_resolved_media.english;
        state.anime_native = current_resolved_media.native;
        state.anime_romaji = current_resolved_media.romaji;
        state.anime_episode = (current_resolved_ep > 0) ? std::to_string(current_resolved_ep) : "";
        state.anime_episode_raw = (parsed_ep > 0) ? std::to_string(parsed_ep) : "";
        state.anime_episode_total = current_resolved_media.episodes;
        state.anime_release_group = a_group;
        state.anime_cover_url = current_resolved_media.cover_url;
        state.anime_anilist_id = current_resolved_media.id;
        state.anime_dirty = true;
    }
}

// Background worker thread entry
static void anime_worker_loop(rich_presence_state& state)
{
    while (true)
    {
        int current_load_id = 0;
        nlohmann::json correction;
        std::string filename_str;
        std::string file_path_str;

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
            file_path_str = std::move(state.pending_file_path);
            state.has_pending_lookup = false;
        }

        bool confirmed = true;
        try
        {
            process_anime_lookup(state, filename_str, file_path_str, current_load_id, correction, confirmed);
        }
        catch (...)
        {
            confirmed = false;
        }
        // A failed search does not establish that the file is non-anime.
        if (confirmed) state.completed_anime_lookup = current_load_id;
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
                    state.pending_file_path = state.current_file_path;
                    state.pending_correction = state.correction;
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
    return value == "auto" || value == "anime" || value == "movie-tv" || value == "movie" || value == "tv"
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
            || (key == "ask_timeout" && val.is_number_integer() && val >= 5 && val <= 300)
            || ((key == "without_sharing" || key == "resume" || key == "rewatch" || key == "keep_pending") && val.is_boolean()))) return false;
    }
    return true;
}

static nlohmann::json current_ask_proposal(rich_presence_state& state)
{
    if (!state.store || state.tracking.value("mode", std::string{}) != "ask" || !lookup_allowed(state))
        return {};
    const auto player = state.store->get("player", state.instance_id);
    if (player.value("session", UINT64_MAX) != state.file_session || !player.value("watch_episode", false))
        return {};
    const auto account = state.store->get("account", "current");
    const int viewer = account.value("id", 0);
    const int media = player.value("media", 0);
    const auto episode = player.value("episode", std::string{});
    if (viewer <= 0 || media <= 0 || episode.empty() || account.value("expired", false))
        return {};
    {
        std::lock_guard lock(state.anime_mutex);
        if (!state.is_anime || !state.watch_episode || state.anime_anilist_id != media || state.anime_episode != episode)
            return {};
    }
    const auto scope = std::format("{}/{}", viewer, media);
    const auto cycle = state.store->get("cycle", scope).value("number", 0);
    const auto proposal = state.store->get("pending", std::format("{}/{}/{}", scope, episode, cycle));
    if (proposal.empty() || proposal.value("offline", false) || !proposal.contains("status")
        || proposal.value("account", std::string{}) != account.value("generation", std::string{}))
        return {};
    return proposal;
}

static std::string ask_prompt_id(const rich_presence_state& state, const nlohmann::json& proposal)
{
    if (proposal.empty()) return {};
    return std::format("{}/{}/{}", state.file_session, proposal.at("account").get<std::string>(),
        proposal.at("key").get<std::string>());
}

static void finish_ask_prompt(rich_presence_state& state)
{
    if (!state.ask_prompt.empty())
    {
        const char* args[] = {"script-message-to", "mpv_rich_presence", "ask-review-close", state.ask_prompt.c_str(), nullptr};
        mpv_command(state.mpv, args);
        state.ask_prompt.clear();
    }
    if (state.ask_hook)
    {
        mpv_hook_continue(state.mpv, state.ask_hook);
        state.ask_hook = 0;
    }
}

static bool show_ask_prompt(rich_presence_state& state, const nlohmann::json& proposal)
{
    const auto id = ask_prompt_id(state, proposal);
    if (id.empty() || id == state.seen_ask_prompt)
        return !id.empty() && id == state.ask_prompt;
    const int seconds = state.tracking.value("ask_timeout", 30);
    const auto payload = nlohmann::json{{"id",id},{"session",state.file_session},{"proposal",proposal},
        {"position",state.tracking.value("ask_position",std::string("top-right"))},{"seconds",seconds}}
        .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    const char* args[] = {"script-message-to", "mpv_rich_presence", "ask-review", payload.c_str(), nullptr};
    state.seen_ask_prompt = id;
    if (mpv_command(state.mpv, args) < 0) return false;
    state.ask_prompt = id;
    state.ask_deadline = chrono::steady_clock::now() + chrono::seconds(seconds);
    return true;
}

static bool at_end_of_file(mpv_handle* mpv)
{
    int eof = 0;
    return mpv_get_property(mpv, "eof-reached", MPV_FORMAT_FLAG, &eof) >= 0 && eof;
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
                if (req.value("id", std::string{}) == state.ask_prompt) finish_ask_prompt(state);
                return;
            }
            if (action == "service" && state.store)
            {
                auto job = req.at("job");
                auto op = job.at("op").get<std::string>();
                if (req.contains("ask_prompt"))
                {
                    if ((op != "accept" && op != "skip") || state.ask_prompt.empty() ||
                        req.at("ask_prompt") != state.ask_prompt) return;
                    state.ask_deadline = chrono::steady_clock::now() + chrono::seconds(25);
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
        state.pending_file_path = state.current_file_path;
        state.pending_correction = state.correction;
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
    struct ask_cleanup
    {
        rich_presence_state& state;
        ~ask_cleanup() { finish_ask_prompt(state); }
    } cleanup_review{state};
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
    mpv_observe_property(state.mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
    mpv_hook_add(state.mpv, 0, "on_unload", 30);

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
            if (event->event_id == MPV_EVENT_HOOK)
            {
                const auto* hook = static_cast<mpv_event_hook*>(event->data);
                bool held = false;
                try
                {
                    const auto timing = state.tracking.value("ask_timing", std::string("threshold"));
                    if (timing == "leave" || (timing == "end" && at_end_of_file(state.mpv)))
                        held = show_ask_prompt(state, current_ask_proposal(state));
                }
                catch (const std::exception& e)
                {
                    mpv_print(state.mpv, std::format("Could not open AniList review: {}", e.what()));
                }
                if (held) state.ask_hook = hook->id;
                else mpv_hook_continue(state.mpv, hook->id);
            }
            else if (event->event_id == MPV_EVENT_CLIENT_MESSAGE)
            {
                handle_client_message(state, (mpv_event_client_message*)event->data);
            }
            else if (event->event_id == MPV_EVENT_FILE_LOADED)
            {
                handle_file_loaded(state);
            }
            else if (event->event_id == MPV_EVENT_END_FILE)
            {
                finish_ask_prompt(state);
                ++state.file_load_id;
                ++state.file_session;
                state.media_has_audio = false;
                state.media_has_video = false;
                state.media_title.clear();
                state.track_title.clear();
                state.media_artist.clear();
                state.current_file_path.clear();
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
        bool watch_episode = false;
        {
            std::lock_guard lock(state.anime_mutex);
            completed_anime_lookup = state.completed_anime_lookup;
            media.anime = state.is_anime;
            if (media.anime)
            {
                watch_episode = state.watch_episode;
                conflicting_anime_minutes = state.conflicting_anime_minutes;
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
            if (epoch != state.cache_epoch)
            {
                state.cache_epoch = epoch;
                ++state.file_load_id;
                clear_anilist_cache();
            }
            auto old = state.correction;
            auto old_appearance = state.appearance;
            auto old_privacy = state.privacy;
            bool old_block = state.hard_block;
            auto old_provider = state.provider;
            auto old_sources = state.sources;
            bool was_allowed = lookup_allowed(state);
            read_profiles(state);
            if (old_sources != state.sources || old != state.correction)
            {
                ++state.file_load_id;
                std::lock_guard lock(state.anime_mutex);
                state.is_anime = false;
                state.anime_anilist_id = 0;
            }
            if (old_sources != state.sources || old != state.correction || was_allowed != lookup_allowed(state)) on_eligibility_changed(state);
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
        const bool anime_unconfirmed = state.media_has_video && use_anilist(state) &&
            state.pending_load_id == load_id && completed_anime_lookup != load_id;
        const bool pinned_anime = state.correction.contains("id") && !state.correction.value("not_anime", false);
        const double duration_estimate = media.position + media.remaining;
        const bool verify_movie = state.store && source == "auto" && allowed && media.anime &&
            !pinned_anime && completed_anime_lookup == load_id && conflicting_anime_minutes > 0 &&
            duration_estimate >= (std::max)(60, 3 * conflicting_anime_minutes) * 60;
        if (verify_movie) source = "verify-movie";
        else if (source == "anime" || !state.provider.empty() || !allowed || media.anime || pinned_anime || anime_unconfirmed)
            source = "original";
        const bool extra = source != "original";
        if (verify_movie) watch_episode = false;
        if (!media.video && !state.scope_path.starts_with("stream-") &&
            (source == "auto" || source == "musicbrainz" || source == "acoustid"))
            media.track_title = state.track_title;
        const int tracked_media = verify_movie ? 0 : media.anilist_id;
        const auto tracked_episode = verify_movie ? "" : media.episode;
        const bool advancing = watch && !state.ask_hook && !media.paused && !media.buffering && !is_seeking;
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
                {"match_title",verify_movie ? "" : media.anime_title}};
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
        if (extra && state.store)
        {
            const auto candidate = state.store->get("auto",state.instance_id);
            if (candidate != automatic) { state.force_update = true; automatic = candidate; }
            const bool resolved = candidate.value("session",uint64_t{}) == state.file_session &&
                candidate.value("source",std::string{}) == source && candidate.value("title",std::string{}) == media.title &&
                candidate.value("track_title",std::string{}) == media.track_title &&
                candidate.value("artist",std::string{}) == media.artist && candidate.value("album",std::string{}) == media.album;
            if (resolved) media.provider = candidate.value("hit",nlohmann::json::object());
            if (verify_movie && (!resolved || !media.provider.empty()))
            {
                media.anime = false;
                media.anilist_id = 0;
                media.episode.clear();
                media.anime_title.clear();
            }
        }
        if (!state.provider.empty() && !media.anime)
        {
            media.provider = state.provider;
        }
        if (now_steady >= next_ask_check && (watch_episode || !state.ask_prompt.empty()))
        {
            next_ask_check = now_steady + chrono::milliseconds(250);
            const auto proposal = current_ask_proposal(state);
            if (!state.ask_prompt.empty() && (ask_prompt_id(state, proposal) != state.ask_prompt ||
                now_steady >= state.ask_deadline))
                finish_ask_prompt(state);
            const auto timing = state.tracking.value("ask_timing", std::string("threshold"));
            if (!proposal.empty() && (timing == "threshold" || at_end_of_file(state.mpv)))
                show_ask_prompt(state, proposal);
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
                {"service",state.store ? state.store->get("response",state.instance_id) : nlohmann::json::object()},
                {"account",std::move(account)},{"watch_episode",watch_episode},
                {"episode_watch",std::move(episode_watch)},
                {"panel_position",state.store ? state.store->get("panel","global").value("position",std::string{}) : ""},
                {"pending",state.store && (state.panel_page=="AniList" || state.panel_page=="Pending updates") ?
                    state.store->pending() : nlohmann::json::array()},
                {"sources",state.sources},{"provider",media.anime ? "anilist" :
                    media.provider.value("provider",std::string(media.track_title.empty() ? "local" : "local tags"))},
                {"hard_block",state.hard_block},{"records", state.store && state.panel_page=="Saved data" ? state.store->records() : nlohmann::json::array()},
                {"reason", !state.config_received ? "Waiting for configuration" : state.store_failed ? "Saved data unavailable" :
                    state.private_session ? "Private session" : !state.is_enabled.value_or(false) ? "Sharing off" :
                    !state.application_id ? "Set a Discord application ID" : !local_permission(state) ? "No eligible media" :
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
    finish_ask_prompt(state);
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
