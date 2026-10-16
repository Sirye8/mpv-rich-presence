// This file is part of mpv-rich-presence.
// Copyright (c) 2026 Alden Wu.
// Copyright (c) 2026 Youssef M. (Sirye8).
//
// mpv-rich-presence is free software: you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.

#include <anitomy/anitomy.h>
#include <boost/dll.hpp>
#include <cmrc/cmrc.hpp>
#include <cpr/cpr.h>
#include <mpv/client.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <codecvt>
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
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mpv_rich_presence/mpv_utils.hpp"
#include "mpv_rich_presence/rich_presence_state.hpp"

namespace chrono = std::chrono;
namespace fs = std::filesystem;
namespace dll = boost::dll;
using namespace std::literals;
using namespace mpvrp;

static constexpr chrono::milliseconds SLEEP_DURATION = 250ms;
static const std::string DISCORD_SDK_DIR = "discord_social_sdk";

// Material Design icons for play/pause
static const std::string ICON_PLAYING = "https://raw.githubusercontent.com/google/material-design-icons/3.0.1/av/2x_web/ic_play_arrow_white_48dp.png";
static const std::string ICON_PAUSED = "https://raw.githubusercontent.com/google/material-design-icons/3.0.1/av/2x_web/ic_pause_white_48dp.png";

CMRC_DECLARE(mpvrp);

// Helpers

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

// Windows path matching
static std::string to_lower(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return str;
}

// Normalize path separators and lowercase
static std::string normalize_path(const std::string& path)
{
    std::string result = to_lower(path);
    std::replace(result.begin(), result.end(), '\\', '/');
    if (!result.empty() && result.back() == '/')
        result.pop_back();
    return result;
}

// Parse folder list
static std::vector<std::string> parse_folder_list(const std::string& raw)
{
    std::vector<std::string> result;
    std::string clean_raw = raw;
    auto g_start = clean_raw.find_first_not_of(" \t");
    auto g_end = clean_raw.find_last_not_of(" \t");
    if (g_start != std::string::npos && g_end != std::string::npos)
        clean_raw = clean_raw.substr(g_start, g_end - g_start + 1);
    if (clean_raw.size() >= 2 && ((clean_raw.front() == '"' && clean_raw.back() == '"') || (clean_raw.front() == '\'' && clean_raw.back() == '\'')))
        clean_raw = clean_raw.substr(1, clean_raw.size() - 2);

    std::istringstream ss(clean_raw);
    std::string item;
    while (std::getline(ss, item, ';'))
    {
        auto start = item.find_first_not_of(" \t\"'");
        auto end = item.find_last_not_of(" \t\"'");
        if (start != std::string::npos && end != std::string::npos)
        {
            std::string trimmed = item.substr(start, end - start + 1);
            if (!trimmed.empty())
                result.push_back(normalize_path(trimmed));
        }
    }
    return result;
}

// Format MM:SS / MM:SS or HH:MM:SS / HH:MM:SS
static std::string format_time_duration(double pos_s, double total_s)
{
    int pos = static_cast<int>(pos_s);
    int total = static_cast<int>(total_s);
    if (pos < 0) pos = 0;
    if (total < 0) total = 0;

    int pos_h = pos / 3600;
    int pos_m = (pos % 3600) / 60;
    int pos_sec = pos % 60;

    int tot_h = total / 3600;
    int tot_m = (total % 3600) / 60;
    int tot_sec = total % 60;

    if (tot_h > 0 || pos_h > 0)
    {
        return std::format("{:02d}:{:02d}:{:02d} / {:02d}:{:02d}:{:02d}",
            pos_h, pos_m, pos_sec, tot_h, tot_m, tot_sec);
    }
    return std::format("{:02d}:{:02d} / {:02d}:{:02d}", pos_m, pos_sec, tot_m, tot_sec);
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
    static const std::regex re_trailing_ep(R"(\s*(?:[-–—]|#|\bep(?:isode)?\.?)\s*0*(\d{1,4})\s*$)", std::regex::icase);
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
    static const std::regex re_dashes(R"(\s*[-–—]\s*)");
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
            static const std::regex re_ep(R"([-–—]\s*(\d{1,4})(?:\s*v\d+)?\b)");
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
        static const std::regex re_trailing_dash_ep(R"(\s*(?:[-–—]|#|\bep(?:isode)?\.?)\s*0*(\d{1,4})\s*$)", std::regex::icase);
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
        static const std::regex re_cut(R"(\bS\d{1,2}E\d{1,3}\b|[-–—]\s*\d{1,4}\b)", std::regex::icase);
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

static bool passes_folder_filter(const std::string& file_path, const std::string& mode, const std::vector<std::string>& folders)
{
    if (mode == "disabled" || folders.empty())
        return true;

    std::string normalized = normalize_path(file_path);

    bool in_list = false;
    for (const auto& folder : folders)
    {
        if (normalized.starts_with(folder + "/") || normalized == folder)
        {
            in_list = true;
            break;
        }
    }

    if (mode == "whitelist")
        return in_list;
    else if (mode == "blacklist")
        return !in_list;

    return true;
}

// Multi-instance coordination

static fs::path get_lock_file_path()
{
    return fs::temp_directory_path() / "mpv-rpc-active.lock";
}

static void claim_active_instance(const std::string& instance_id)
{
    try
    {
        auto path = get_lock_file_path();
        auto now = chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch())
                       .count();
        std::ofstream f(path, std::ios::trunc);
        f << instance_id << "\n"
          << now << "\n";
    }
    catch (...)
    {
    }
}

static bool is_active_instance(const std::string& instance_id)
{
    try
    {
        auto path = get_lock_file_path();
        if (!fs::exists(path))
            return true;

        std::ifstream f(path);
        std::string stored_id;
        std::getline(f, stored_id);
        return stored_id == instance_id;
    }
    catch (...)
    {
        return true;
    }
}

// AniList API

struct anilist_media
{
    int id = 0;
    std::string title;
    int episodes = 0;
    std::string cover_url;
    std::string format;
    int sequel_id = -1;
};

static const std::string ANILIST_PAGE_FIELDS = R"(
    id
    title { romaji english }
    episodes
    format
    coverImage { large }
    relations {
        edges {
            relationType(version: 2)
            node { id type format title { romaji } }
        }
    }
)";

static std::optional<anilist_media> parse_anilist_media(const nlohmann::json& media)
{
    if (media.is_null())
        return std::nullopt;

    anilist_media result;
    result.id = media.value("id", 0);
    if (media.contains("title") && !media["title"].is_null())
    {
        if (media["title"].contains("romaji") && !media["title"]["romaji"].is_null())
            result.title = media["title"]["romaji"].get<std::string>();
        if (result.title.empty() && media["title"].contains("english") && !media["title"]["english"].is_null())
            result.title = media["title"]["english"].get<std::string>();
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

// AniList cache

static std::mutex anilist_cache_mutex;
static std::unordered_map<std::string, std::vector<anilist_media>> anilist_page_cache;
static std::unordered_map<int, anilist_media> anilist_id_cache;

static std::vector<anilist_media> search_anilist_page(const std::string& search)
{
    {
        std::lock_guard<std::mutex> lock(anilist_cache_mutex);
        auto it = anilist_page_cache.find(search);
        if (it != anilist_page_cache.end())
            return it->second;
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
            if (j.contains("data") && !j["data"].is_null() && j["data"].contains("Page")
                && j["data"]["Page"].contains("media") && j["data"]["Page"]["media"].is_array())
            {
                for (auto& item : j["data"]["Page"]["media"])
                {
                    auto parsed = parse_anilist_media(item);
                    if (parsed)
                    {
                        results.push_back(*parsed);
                        std::lock_guard<std::mutex> lock(anilist_cache_mutex);
                        anilist_id_cache[parsed->id] = *parsed;
                    }
                }
            }
        }
        catch (...)
        {
        }
    }

    {
        std::lock_guard<std::mutex> lock(anilist_cache_mutex);
        anilist_page_cache[search] = results;
    }
    return results;
}

static std::optional<anilist_media> fetch_anilist_by_id(int id)
{
    {
        std::lock_guard<std::mutex> lock(anilist_cache_mutex);
        auto it = anilist_id_cache.find(id);
        if (it != anilist_id_cache.end())
            return it->second;
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
            if (j.contains("data") && !j["data"].is_null() && j["data"].contains("Media"))
                result = parse_anilist_media(j["data"]["Media"]);
        }
        catch (...)
        {
        }
    }

    if (result)
    {
        std::lock_guard<std::mutex> lock(anilist_cache_mutex);
        anilist_id_cache[result->id] = *result;
    }
    return result;
}

// Event handlers

static void handle_client_message(rich_presence_state& state, int64_t& application_id, mpv_event_client_message* msg)
{
    if (msg->num_args == 0)
        return;

    auto is_enabled_prev = state.is_enabled;

    if (msg->args[0] == "toggle"s)
    {
        state.is_enabled = !state.is_enabled.has_value() || !*state.is_enabled;
    }
    else if (msg->args[0] == "on"s)
    {
        state.is_enabled = true;
    }
    else if (msg->args[0] == "off"s)
    {
        state.is_enabled = false;
    }
    else if (msg->args[0] == "application_id"s)
    {
        if (msg->num_args < 2)
        {
            mpv_print(state.mpv, "Not enough args for application_id");
        }
        else
        {
            try
            {
                application_id = std::stoll(msg->args[1]);
                mpv_print(state.mpv, std::format("Received application_id: {}", application_id));
            }
            catch (const std::exception& e)
            {
                mpv_print(state.mpv, e.what() + ": "s + msg->args[1]);
            }
        }
    }
    else if (msg->args[0] == "folder_filter_mode"s)
    {
        if (msg->num_args >= 2)
        {
            state.folder_filter_mode = msg->args[1];
            mpv_print(state.mpv, std::format("Folder filter mode: {}", state.folder_filter_mode));
        }
    }
    else if (msg->args[0] == "folder_list"s)
    {
        if (msg->num_args >= 2)
        {
            state.folder_list = parse_folder_list(msg->args[1]);
            mpv_print(state.mpv, std::format("Folder list: {} entries", state.folder_list.size()));
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

    if (is_enabled_prev.has_value() && state.is_enabled.has_value() && state.is_enabled != is_enabled_prev)
        mpv_show(state.mpv, std::format("Rich presence {}", *state.is_enabled ? "enabled" : "disabled"));
}

static void handle_file_loaded(rich_presence_state& state)
{
    state.media_has_audio = false;
    state.media_has_video = false;

    int64_t media_track_count = 0;
    mpv_get_property(state.mpv, "track-list/count", MPV_FORMAT_INT64, &media_track_count);
    for (int64_t i = 0; i < media_track_count; i++)
    {
        const char* media_track_type = "";
        mpv_get_property(state.mpv, std::format("track-list/{}/type", i).c_str(), MPV_FORMAT_STRING, &media_track_type);
        if (media_track_type == "audio"s)
            state.media_has_audio = true;

        if (media_track_type == "video"s)
        {
            int is_image = 0;
            mpv_get_property(state.mpv, std::format("track-list/{}/image", i).c_str(), MPV_FORMAT_FLAG, &is_image);
            if (is_image == 0)
                state.media_has_video = true;
        }
    }

    const char* media_artist = "";
    const char* media_title = "";
    mpv_get_property(state.mpv, "metadata/by-key/Artist", MPV_FORMAT_OSD_STRING, &media_artist);
    if (!media_artist || strlen(media_artist) == 0)
        mpv_get_property(state.mpv, "metadata/by-key/artist", MPV_FORMAT_OSD_STRING, &media_artist);
    if (!media_artist || strlen(media_artist) == 0)
        mpv_get_property(state.mpv, "metadata/by-key/album_artist", MPV_FORMAT_OSD_STRING, &media_artist);
    mpv_get_property(state.mpv, "media-title", MPV_FORMAT_OSD_STRING, &media_title);
    state.media_artist = media_artist == nullptr ? "" : media_artist;
    state.media_title = media_title == nullptr ? "" : media_title;

    // Get full file path for folder filtering
    const char* file_path = "";
    mpv_get_property(state.mpv, "path", MPV_FORMAT_STRING, &file_path);
    state.current_file_path = file_path ? file_path : "";

    const char* filename = "";
    mpv_get_property(state.mpv, "filename", MPV_FORMAT_STRING, &filename);
    std::string filename_str = filename ? filename : "";
    if (filename_str.empty() && !state.current_file_path.empty())
    {
        filename_str = fs::path(state.current_file_path).filename().string();
    }

    // Bump file ID and reset anime state
    int current_load_id = ++state.file_load_id;
    {
        std::lock_guard<std::mutex> lock(state.anime_mutex);
        state.is_anime = false;
        state.anime_anilist_id = 0;
    }

    // Reset pause state and activity tracking for new file
    state.was_paused = false;
    state.last_paused = -1;
    state.last_reported_pos = -1.0;
    state.pause_start_epoch_ms = 0;
    state.rpc_hidden_by_timeout = false;

    // Claim this instance as active on file load
    claim_active_instance(state.instance_id);

    std::thread([&state, filename_str, current_load_id]() {
        anitomy::Anitomy anitomy;
        anitomy.Parse(s2ws(filename_str));
        auto& elements = anitomy.elements();

        parsed_filename_info pinfo = parse_anime_filename(filename_str, elements);
        if (pinfo.title.empty())
            return;

        std::string a_title = pinfo.title;
        int parsed_season = pinfo.season;
        int parsed_part = pinfo.part;
        int parsed_ep = pinfo.episode;
        std::string a_group = pinfo.release_group;

        // Search AniList
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

        for (const auto& q_str : search_queries)
        {
            auto page = search_anilist_page(q_str);
            if (page.empty())
                continue;

            // Find best media among the candidates
            std::optional<anilist_media> best = std::nullopt;
            for (const auto& m : page)
            {
                std::string romaji_lower = to_lower(m.title);

                // If season > 1, check that the title references the season
                if (parsed_season > 1 && page.size() > 1)
                {
                    bool has_season = (romaji_lower.find(std::to_string(parsed_season)) != std::string::npos)
                                      || (romaji_lower.find(to_lower(ord_str)) != std::string::npos);
                    if (!has_season)
                        continue;
                }

                // If part > 0, check that title references the part
                if (parsed_part > 0)
                {
                    if (romaji_lower.find("part " + std::to_string(parsed_part)) == std::string::npos
                        && romaji_lower.find("cour " + std::to_string(parsed_part)) == std::string::npos)
                        continue;
                }

                // If episode fits within total episodes
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

        // Fallback to base title if not found
        if (!candidate)
        {
            auto base_page = search_anilist_page(a_title);
            if (!base_page.empty())
                candidate = base_page[0];
        }

        if (!candidate)
            return;

        anilist_media resolved_media = *candidate;
        int resolved_ep = parsed_ep;

        // Walk sequels if episode number exceeds current season
        if (parsed_ep > 0 && candidate->episodes > 0 && parsed_ep > candidate->episodes)
        {
            int curr_id = candidate->id;
            int remaining_ep = parsed_ep;
            std::unordered_set<int> visited;

            for (int step = 0; step < 20 && curr_id > 0; step++)
            {
                if (visited.contains(curr_id))
                    break;
                visited.insert(curr_id);

                auto m_opt = fetch_anilist_by_id(curr_id);
                if (!m_opt)
                    break;

                auto m = *m_opt;
                if (m.episodes > 0 && remaining_ep <= m.episodes)
                {
                    resolved_media = m;
                    resolved_ep = remaining_ep;
                    break;
                }
                else if (m.episodes > 0)
                {
                    remaining_ep -= m.episodes;
                }
                else
                {
                    // Ongoing or unannounced total episodes
                    resolved_media = m;
                    resolved_ep = remaining_ep;
                    break;
                }

                if (m.sequel_id <= 0)
                {
                    resolved_media = m;
                    resolved_ep = remaining_ep;
                    break;
                }
                curr_id = m.sequel_id;
            }
        }

        // Update state
        std::lock_guard<std::mutex> lock(state.anime_mutex);
        if (state.file_load_id == current_load_id)
        {
            state.is_anime = true;
            state.anime_title = resolved_media.title;
            state.anime_episode = (resolved_ep > 0) ? std::to_string(resolved_ep) : "";
            state.anime_episode_raw = (parsed_ep > 0) ? std::to_string(parsed_ep) : "";
            state.anime_episode_total = resolved_media.episodes;
            state.anime_release_group = a_group;
            state.anime_cover_url = resolved_media.cover_url;
            state.anime_anilist_id = resolved_media.id;
            state.anime_dirty = true;
        }
    }).detach();
}

// Discord callbacks

void on_discord_log(Discord_String msg, Discord_LoggingSeverity severity, void* payload)
{
    auto& state = *(rich_presence_state*)payload;

    auto severity_str = ""s;
    switch (severity)
    {
    case Discord_LoggingSeverity::Verbose:
        severity_str = "Verbose";
        break;
    case Discord_LoggingSeverity::Info:
        severity_str = "Info";
        break;
    case Discord_LoggingSeverity::Warning:
        severity_str = "Warning";
        break;
    case Discord_LoggingSeverity::Error:
        severity_str = "Error";
        break;
    case Discord_LoggingSeverity::None:
        severity_str = "None";
        break;
    default:
        severity_str = "unknown";
        break;
    }

    auto msg_view = std::string_view { msg.data, msg.size };
    mpv_print(state.mpv, std::format("[{}] {}", severity_str, msg_view));
}

void on_discord_update_rich_presence(Discord_ClientResult* result, void* payload)
{
    auto& state = *(rich_presence_state*)payload;

    if (state.discord_api->Discord_ClientResult_Successful(result))
        return;

    int32_t code = state.discord_api->Discord_ClientResult_ErrorCode(result);
    mpv_print(state.mpv, std::format("Rich presence error: {}", code));
    // Retry on failure
    state.last_activity_update = chrono::steady_clock::time_point{};
    state.last_paused = -1;
}

// Main plugin entry

auto mpv_open_cplugin_impl(mpv_handle* ctx) -> int
{
    auto state = rich_presence_state {};
    state.mpv = ctx;
    state.instance_id = generate_instance_id();

    // Initialization

    mpv_print(state.mpv, "Pinging rich_presence_conf for config data...");
    auto ping_args = std::array<const char*, 4> { "script-message-to", "rich_presence_conf", "ping", nullptr };
    mpv_command(state.mpv, ping_args.data());

    // these are to deal with synchronization issues, not really state
    std::atomic<bool> is_ready = false;
    int64_t application_id = 0;

    mpv_print(state.mpv, "Initializing Discord Social SDK...");
    auto discord_init = std::jthread { [&state, &is_ready]() {
        auto embedded_fs = cmrc::mpvrp::get_filesystem();
        auto filename = (*embedded_fs.iterate_directory(DISCORD_SDK_DIR).begin()).filename();
        auto embedded_sdk = embedded_fs.open(std::format("{}/{}", DISCORD_SDK_DIR, filename));
        auto temp_sdk_path = fs::temp_directory_path() / filename;
        {
            auto temp_sdk = std::ofstream { temp_sdk_path, std::ios_base::binary };
            temp_sdk.write(embedded_sdk.begin(), embedded_sdk.size());
        }
        auto sdk = dll::shared_library { fs::absolute(temp_sdk_path).string() };
        state.discord_api = std::make_shared<discord_api_importer>(sdk);

        state.discord = std::make_unique<discord_client>(state.discord_api);
        state.discord_api->Discord_Client_AddLogCallback(&state.discord->get(), on_discord_log, nullptr, &state, Discord_LoggingSeverity::Warning);

        is_ready = true;
    } };

    // Wake up immediately on pause/seek changes
    mpv_observe_property(state.mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(state.mpv, 0, "seeking", MPV_FORMAT_FLAG);

    // Main event loop

    while (true)
    {
        // Event handling (+ sleep)

        auto* event = mpv_wait_event(state.mpv, SLEEP_DURATION.count() * 1e-3);
        bool should_shutdown = false;
        while (event && event->event_id != MPV_EVENT_NONE)
        {
            if (event->event_id == MPV_EVENT_SHUTDOWN)
            {
                should_shutdown = true;
                break;
            }

            if (event->event_id == MPV_EVENT_CLIENT_MESSAGE)
                handle_client_message(state, application_id, (mpv_event_client_message*)event->data);
            else if (event->event_id == MPV_EVENT_FILE_LOADED)
                handle_file_loaded(state);

            event = mpv_wait_event(state.mpv, 0);
        }

        if (should_shutdown)
            break;

        // Rich Presence updating

        if (!is_ready)
            continue;

        state.discord_api->Discord_RunCallbacks();

        if (state.discord_api->Discord_Client_GetApplicationId(&state.discord->get()) != application_id)
            state.discord_api->Discord_Client_SetApplicationId(&state.discord->get(), application_id);

        if (application_id == 0)
            continue;

        if (!state.is_enabled.has_value()
            || !*state.is_enabled
            || (!state.media_has_audio && !state.media_has_video))
        {
            state.discord_api->Discord_Client_ClearRichPresence(&state.discord->get());
            continue;
        }

        // Multi-instance: skip update if another instance is active
        if (!is_active_instance(state.instance_id))
        {
            state.discord_api->Discord_Client_ClearRichPresence(&state.discord->get());
            continue;
        }

        // Folder filtering: clear RPC if current file doesn't pass the filter
        if (!passes_folder_filter(state.current_file_path, state.folder_filter_mode, state.folder_list))
        {
            state.discord_api->Discord_Client_ClearRichPresence(&state.discord->get());
            continue;
        }

        double media_time_pos_s = 0.0;
        double media_time_left_s = 0.0;
        int is_media_paused = 0;
        int is_seeking = 0;
        mpv_get_property(state.mpv, "seeking", MPV_FORMAT_FLAG, &is_seeking);
        mpv_get_property(state.mpv, "time-pos/full", MPV_FORMAT_DOUBLE, &media_time_pos_s);
        mpv_get_property(state.mpv, "time-remaining/full", MPV_FORMAT_DOUBLE, &media_time_left_s);
        mpv_get_property(state.mpv, "pause", MPV_FORMAT_FLAG, &is_media_paused);

        // Skip tick during seek or when time is transiently unavailable
        if (is_seeking == 1 || (is_media_paused == 0 && media_time_left_s <= 0.0))
            continue;

        // Claim active instance on play/pause transitions
        if (is_media_paused == 0 && state.was_paused)
        {
            claim_active_instance(state.instance_id);
            state.rpc_hidden_by_timeout = false;
        }

        // Pause timeout handling
        if (is_media_paused == 1)
        {
            if (state.last_paused != 1)
            {
                // Just paused: record start time
                state.pause_start_time = chrono::steady_clock::now();
                state.pause_start_epoch_ms = chrono::duration_cast<chrono::milliseconds>(
                    chrono::system_clock::now().time_since_epoch()).count();
            }

            if (state.pause_timeout_seconds > 0)
            {
                auto pause_elapsed = chrono::steady_clock::now() - state.pause_start_time;
                if (pause_elapsed >= chrono::seconds(state.pause_timeout_seconds))
                {
                    if (!state.rpc_hidden_by_timeout)
                    {
                        state.rpc_hidden_by_timeout = true;
                        state.discord_api->Discord_Client_ClearRichPresence(&state.discord->get());
                    }
                    state.was_paused = true;
                    continue;
                }
            }
        }
        else
        {
            // Playing: reset timeout state
            state.rpc_hidden_by_timeout = false;
        }

        // Rate-limit updates to prevent saturating Discord RPC
        bool pause_changed = (is_media_paused != state.last_paused);
        bool anime_updated = state.anime_dirty.exchange(false);
        auto now_steady = chrono::steady_clock::now();

        bool seek_detected = false;
        if (state.last_reported_pos >= 0.0)
        {
            if (is_media_paused == 0)
            {
                auto elapsed_s = chrono::duration_cast<chrono::milliseconds>(now_steady - state.last_activity_update).count() * 1e-3;
                double expected_pos = state.last_reported_pos + elapsed_s;
                if (std::abs(media_time_pos_s - expected_pos) > 0.5 && (now_steady - state.last_activity_update >= 400ms))
                    seek_detected = true;
            }
            else
            {
                if (std::abs(media_time_pos_s - state.last_reported_pos) > 0.5 && (now_steady - state.last_activity_update >= 400ms))
                    seek_detected = true;
            }
        }

        // Don't send periodic updates when paused so Discord's timer doesn't jump
        bool periodic_update = (is_media_paused == 0 && (now_steady - state.last_activity_update >= chrono::seconds(15)));
        bool first_update = (state.last_reported_pos < 0.0);

        if (!pause_changed && !anime_updated && !seek_detected && !periodic_update && !first_update)
            continue;

        state.last_paused = is_media_paused;
        state.was_paused = (is_media_paused == 1);
        state.last_reported_pos = media_time_pos_s;
        state.last_activity_update = now_steady;

        auto activity = discord_activity { state.discord_api };
        auto assets = discord_activity_assets { state.discord_api };

        std::string activity_name;
        std::string details_str;
        std::string state_str;
        std::string cover_url;
        std::string anilist_url;
        int local_anilist_id = 0;

        auto activity_type = state.media_has_video ? Discord_ActivityTypes::Watching : Discord_ActivityTypes::Listening;
        state.discord_api->Discord_Activity_SetType(&activity.get(), activity_type);

        std::string dur_str = format_time_duration(media_time_pos_s, media_time_pos_s + media_time_left_s);

        // Read anime state
        bool local_is_anime = false;
        {
            std::lock_guard<std::mutex> lock(state.anime_mutex);
            local_is_anime = state.is_anime;
            if (local_is_anime)
            {
                activity_name = state.anime_title;
                details_str = state.anime_title;

                if (!state.anime_episode.empty())
                {
                    if (state.anime_episode_total > 0)
                        state_str = std::format("Episode {}/{}", state.anime_episode, state.anime_episode_total);
                    else
                        state_str = std::format("Episode {}", state.anime_episode);

                    if (!state.anime_release_group.empty())
                        state_str += std::format(" by {}", state.anime_release_group);

                    if (is_media_paused == 1)
                        state_str += std::format(" ({})", dur_str);
                }
                else if (!state.anime_release_group.empty())
                {
                    state_str = std::format("by {}", state.anime_release_group);
                    if (is_media_paused == 1)
                        state_str += std::format(" ({})", dur_str);
                }
                else if (is_media_paused == 1)
                {
                    state_str = dur_str;
                }

                cover_url = state.anime_cover_url;
                local_anilist_id = state.anime_anilist_id;
            }
        }

        if (local_is_anime)
        {
            state.discord_api->Discord_Activity_SetName(&activity.get(), { activity_name.data(), activity_name.size() });

            if (!details_str.empty())
            {
                Discord_String d_details = { details_str.data(), details_str.size() };
                state.discord_api->Discord_Activity_SetDetails(&activity.get(), &d_details);
            }

            if (!state_str.empty())
            {
                Discord_String d_state = { state_str.data(), state_str.size() };
                state.discord_api->Discord_Activity_SetState(&activity.get(), &d_state);
            }

            if (!cover_url.empty())
            {
                state.discord_api->Discord_ActivityAssets_SetLargeImage(&assets.get(), { cover_url.data(), cover_url.size() });
                state.discord_api->Discord_ActivityAssets_SetLargeText(&assets.get(), { activity_name.data(), activity_name.size() });
            }

            // "View on AniList" button
            if (local_anilist_id > 0)
            {
                anilist_url = std::format("https://anilist.co/anime/{}", local_anilist_id);
                auto button = discord_activity_button { state.discord_api };
                std::string button_label = "View on AniList";
                state.discord_api->Discord_ActivityButton_SetLabel(&button.get(), { button_label.data(), button_label.size() });
                state.discord_api->Discord_ActivityButton_SetUrl(&button.get(), { anilist_url.data(), anilist_url.size() });
                state.discord_api->Discord_Activity_AddButton(&activity.get(), &button.get());
            }
        }
        else
        {
            // Non-anime fallback
            std::string song_info;
            if (!state.media_artist.empty() && !state.media_title.empty())
            {
                std::string title_lower = state.media_title;
                std::string artist_lower = state.media_artist;
                std::transform(title_lower.begin(), title_lower.end(), title_lower.begin(), ::tolower);
                std::transform(artist_lower.begin(), artist_lower.end(), artist_lower.begin(), ::tolower);
                if (title_lower.rfind(artist_lower, 0) == 0)
                    song_info = state.media_title;
                else
                    song_info = std::format("{} - {}", state.media_artist, state.media_title);
            }
            else if (!state.media_artist.empty())
            {
                song_info = state.media_artist;
            }
            else
            {
                song_info = state.media_title;
            }

            if (!state.media_has_video && !song_info.empty())
                activity_name = song_info;
            else
                activity_name = "mpv";

            if (activity_name.size() > 128)
                activity_name = activity_name.substr(0, 125) + "...";

            state.discord_api->Discord_Activity_SetName(&activity.get(), { activity_name.data(), activity_name.size() });

            std::string fallback_details = !song_info.empty() ? song_info : state.media_title;
            if (!fallback_details.empty())
            {
                Discord_String d_details = { fallback_details.data(), fallback_details.size() };
                state.discord_api->Discord_Activity_SetDetails(&activity.get(), &d_details);
            }

            if (is_media_paused == 1)
            {
                Discord_String d_state = { dur_str.data(), dur_str.size() };
                state.discord_api->Discord_Activity_SetState(&activity.get(), &d_state);
            }
        }

        // Play/pause icon via small image
        const std::string& icon_url = (is_media_paused == 1) ? ICON_PAUSED : ICON_PLAYING;
        state.discord_api->Discord_ActivityAssets_SetSmallImage(&assets.get(), { icon_url.data(), icon_url.size() });

        std::string small_tooltip = (is_media_paused == 1) ? std::format("Paused ({})", dur_str) : "Playing";
        state.discord_api->Discord_ActivityAssets_SetSmallText(&assets.get(), { small_tooltip.data(), small_tooltip.size() });

        state.discord_api->Discord_Activity_SetAssets(&activity.get(), &assets.get());

        // Timestamps
        if (is_media_paused == 1)
        {
            if (state.pause_start_epoch_ms > 0)
            {
                auto timestamps = discord_activity_timestamps { state.discord_api };
                state.discord_api->Discord_ActivityTimestamps_SetStart(&timestamps.get(), state.pause_start_epoch_ms);
                state.discord_api->Discord_Activity_SetTimestamps(&activity.get(), &timestamps.get());
            }
        }
        else
        {
            auto timestamps = discord_activity_timestamps { state.discord_api };
            uint64_t now_ms = chrono::duration_cast<chrono::milliseconds>(
                chrono::system_clock::now().time_since_epoch()).count();
            uint64_t start = now_ms - static_cast<uint64_t>(media_time_pos_s * 1000);
            uint64_t end = now_ms + static_cast<uint64_t>(media_time_left_s * 1000);

            state.discord_api->Discord_ActivityTimestamps_SetStart(&timestamps.get(), start);
            state.discord_api->Discord_ActivityTimestamps_SetEnd(&timestamps.get(), end);
            state.discord_api->Discord_Activity_SetTimestamps(&activity.get(), &timestamps.get());
        }

        state.discord_api->Discord_Client_UpdateRichPresence(&state.discord->get(), &activity.get(), on_discord_update_rich_presence, nullptr, &state);
    }

    // Shut down

    mpv_print(state.mpv, "Shutting down...");
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
