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

#include "mpv_rich_presence/mpv_utils.hpp"
#include "mpv_rich_presence/rich_presence_state.hpp"

namespace chrono = std::chrono;
namespace fs = std::filesystem;
namespace dll = boost::dll;
using namespace std::literals;
using namespace mpvrp;

static constexpr chrono::milliseconds SLEEP_DURATION = 250ms;
static const std::string DISCORD_SDK_DIR = "discord_social_sdk";
static constexpr int64_t LEASE_TIMEOUT_MS = 6000;

// Material Design icons for play and pause
static const std::string ICON_PLAYING = "https://raw.githubusercontent.com/google/material-design-icons/3.0.1/av/2x_web/ic_play_arrow_white_48dp.png";
static const std::string ICON_PAUSED = "https://raw.githubusercontent.com/google/material-design-icons/3.0.1/av/2x_web/ic_pause_white_48dp.png";

CMRC_DECLARE(mpvrp);

// Interprocess synchronization guard for multi-instance lease coordination
#if defined(_WIN32)
class ipc_mutex_guard
{
public:
    ipc_mutex_guard()
    {
        m_handle = CreateMutexW(NULL, FALSE, L"Local\\mpv_rich_presence_lease_mutex");
        if (m_handle)
        {
            DWORD res = WaitForSingleObject(m_handle, 3000);
            m_locked = (res == WAIT_OBJECT_0 || res == WAIT_ABANDONED);
        }
    }

    ~ipc_mutex_guard()
    {
        if (m_locked && m_handle)
            ReleaseMutex(m_handle);
        if (m_handle)
            CloseHandle(m_handle);
    }

    bool is_locked() const { return m_locked; }

private:
    HANDLE m_handle = NULL;
    bool m_locked = false;
};
#else
class ipc_mutex_guard
{
public:
    ipc_mutex_guard()
    {
        auto lock_dir = fs::temp_directory_path() / "mpv-rich-presence";
        std::error_code ec;
        fs::create_directories(lock_dir, ec);
        auto lock_path = lock_dir / ".lease.lock";
        m_fd = open(lock_path.c_str(), O_CREAT | O_RDWR, 0666);
        if (m_fd >= 0)
        {
            struct flock fl {};
            fl.l_type = F_WRLCK;
            fl.l_whence = SEEK_SET;
            if (fcntl(m_fd, F_SETLKW, &fl) == 0)
                m_locked = true;
        }
    }

    ~ipc_mutex_guard()
    {
        if (m_locked && m_fd >= 0)
        {
            struct flock fl {};
            fl.l_type = F_UNLCK;
            fl.l_whence = SEEK_SET;
            fcntl(m_fd, F_SETLK, &fl);
        }
        if (m_fd >= 0)
            close(m_fd);
    }

    bool is_locked() const { return m_locked; }

private:
    int m_fd = -1;
    bool m_locked = false;
};
#endif

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

static std::string to_lower(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return str;
}

static std::string truncate_utf8_bytes(std::string_view text, std::size_t limit)
{
    if (text.size() <= limit)
        return std::string(text);
    constexpr std::string_view suffix = "...";
    if (limit < suffix.size())
        return std::string(limit, '.');
    std::size_t cut = limit - suffix.size();
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
    {
        --cut;
    }
    return std::string(text.substr(0, cut)) + std::string(suffix);
}

static uint64_t compute_fnv1a64(const char* data, size_t len)
{
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i)
    {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// OS-aware path normalization
static std::string normalize_path(const std::string& path_str)
{
    if (path_str.empty())
        return {};

    std::error_code ec;
    fs::path p = fs::weakly_canonical(path_str, ec);
    if (ec)
        p = fs::path(path_str).lexically_normal();

    std::string result = p.generic_string();

#if defined(_WIN32) || defined(__CYGWIN__)
    result = to_lower(result);
#endif

    if (!result.empty() && result.back() == '/')
        result.pop_back();

    return result;
}

// Parse folder list from semicolon-separated string
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

// Format time string
static std::string format_time_duration(double pos_s, double total_s)
{
    int pos = static_cast<int>(pos_s);
    if (pos < 0) pos = 0;
    int pos_h = pos / 3600;
    int pos_m = (pos % 3600) / 60;
    int pos_sec = pos % 60;

    if (total_s <= 0.0)
    {
        if (pos_h > 0)
            return std::format("{:02d}:{:02d}:{:02d} / Live", pos_h, pos_m, pos_sec);
        return std::format("{:02d}:{:02d} / Live", pos_m, pos_sec);
    }

    int total = static_cast<int>(total_s);
    if (total < 0) total = 0;
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

// Check folder whitelist/blacklist
static bool passes_folder_filter(const std::string& file_path, const std::string& mode, const std::vector<std::string>& folders)
{
    if (mode == "disabled")
        return true;

    if (mode == "whitelist")
    {
        if (folders.empty())
            return false;
    }
    else if (mode == "blacklist")
    {
        if (folders.empty())
            return true;
    }
    else
    {
        return true;
    }

    if (file_path.empty())
        return (mode == "blacklist");

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
    else
        return !in_list;
}

static bool is_window_eligible(const rich_presence_state& state)
{
    if (!state.config_received)
        return false;

    if (!state.is_enabled.value_or(false))
        return false;

    if (!state.media_has_audio && !state.media_has_video)
        return false;

    return passes_folder_filter(state.current_file_path, state.folder_filter_mode, state.folder_list);
}

// Multi-instance coordination

static fs::path get_lock_file_path()
{
    auto dir = fs::temp_directory_path() / "mpv-rich-presence";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir / "mpv-rpc-active.lock";
}

// Internal checked writer for lease records: caller MUST already hold the interprocess mutex
static bool write_lease_record_locked(const fs::path& lock_path, const std::string& instance_id, int64_t timestamp_ms)
{
    auto tmp_path = lock_path.parent_path() / std::format("mpv-rpc-active.{}.tmp", instance_id);
    std::error_code ec;

    try
    {
        {
            std::ofstream out;
            out.exceptions(std::ios::failbit | std::ios::badbit);
            out.open(tmp_path, std::ios::binary | std::ios::trunc);
            out << instance_id << '\n' << timestamp_ms << '\n';
            out.flush();
            out.close();
        }

        fs::rename(tmp_path, lock_path, ec);
        if (ec)
        {
            // Direct write fallback within the held interprocess lock
            std::ofstream out;
            out.exceptions(std::ios::failbit | std::ios::badbit);
            out.open(lock_path, std::ios::binary | std::ios::trunc);
            out << instance_id << '\n' << timestamp_ms << '\n';
            out.flush();
            out.close();
            fs::remove(tmp_path, ec);
        }
        return true;
    }
    catch (...)
    {
        fs::remove(tmp_path, ec);
        return false;
    }
}

// Explicit takeover: claims active ownership
static bool claim_active_instance(const std::string& instance_id)
{
    try
    {
        ipc_mutex_guard guard;
        if (!guard.is_locked())
            return false;

        auto path = get_lock_file_path();
        auto now = chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count();

        return write_lease_record_locked(path, instance_id, now);
    }
    catch (...)
    {
        return false;
    }
}

// Atomic lease renewal and takeover: renews if already owner, acquires if missing, invalid, or expired
static bool acquire_or_renew_active_instance(const std::string& instance_id)
{
    try
    {
        ipc_mutex_guard guard;
        if (!guard.is_locked())
            return false;

        auto path = get_lock_file_path();
        bool can_acquire_or_renew = false;

        auto now = chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count();

        if (!fs::exists(path))
        {
            can_acquire_or_renew = true;
        }
        else
        {
            std::ifstream f(path);
            std::string stored_id;
            int64_t stored_time = 0;
            if (std::getline(f, stored_id))
            {
                std::string time_str;
                if (std::getline(f, time_str))
                {
                    try { stored_time = std::stoll(time_str); } catch (...) { stored_time = 0; }
                }
            }
            f.close();

            while (!stored_id.empty() && (stored_id.back() == '\r' || stored_id.back() == '\n' || stored_id.back() == ' '))
                stored_id.pop_back();

            if (stored_id == instance_id)
            {
                can_acquire_or_renew = true;
            }
            else if (stored_id.empty() || stored_time <= 0)
            {
                can_acquire_or_renew = true;
            }
            else if (now - stored_time > LEASE_TIMEOUT_MS)
            {
                can_acquire_or_renew = true;
            }
            else
            {
                return false;
            }
        }

        if (can_acquire_or_renew)
        {
            return write_lease_record_locked(path, instance_id, now);
        }

        return false;
    }
    catch (...)
    {
        return false;
    }
}

static bool is_active_instance(const std::string& instance_id)
{
    try
    {
        ipc_mutex_guard guard;
        if (!guard.is_locked())
            return false;

        auto path = get_lock_file_path();
        if (!fs::exists(path))
            return true;

        std::ifstream f(path);
        std::string stored_id;
        int64_t stored_time = 0;
        if (std::getline(f, stored_id))
        {
            std::string time_str;
            if (std::getline(f, time_str))
            {
                try { stored_time = std::stoll(time_str); } catch (...) { stored_time = 0; }
            }
        }

        while (!stored_id.empty() && (stored_id.back() == '\r' || stored_id.back() == '\n' || stored_id.back() == ' '))
            stored_id.pop_back();

        if (stored_id == instance_id)
            return true;

        // Malformed, empty, or non-positive timestamp records are treated as immediately recoverable
        if (stored_id.empty() || stored_time <= 0)
            return true;

        auto now = chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count();

        if (now - stored_time > LEASE_TIMEOUT_MS)
        {
            return true;
        }

        return false;
    }
    catch (...)
    {
        return false;
    }
}

static void release_active_instance(const std::string& instance_id)
{
    try
    {
        ipc_mutex_guard guard;
        if (!guard.is_locked())
            return;

        auto path = get_lock_file_path();
        if (!fs::exists(path))
            return;

        std::ifstream f(path);
        std::string stored_id;
        if (std::getline(f, stored_id))
        {
            while (!stored_id.empty() && (stored_id.back() == '\r' || stored_id.back() == '\n' || stored_id.back() == ' '))
                stored_id.pop_back();

            if (stored_id == instance_id)
            {
                f.close();
                std::error_code ec;
                fs::remove(path, ec);
            }
        }
    }
    catch (...)
    {
    }
}

// AniList API structures and caching

struct anilist_media
{
    int id = 0;
    std::string title; // Preferred display title
    std::vector<std::string> matching_titles; // Romaji, English, Native, and Synonyms for alias matching
    int episodes = 0;
    std::string cover_url;
    std::string format;
    int sequel_id = -1;
};

static const std::string ANILIST_PAGE_FIELDS = R"(
    id
    title { romaji english native }
    synonyms
    episodes
    format
    coverImage { large }
    relations {
        edges {
            relationType(version: 2)
            node {
                id
                type
                format
                title { romaji english native }
                synonyms
            }
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
        {
            std::string romaji = media["title"]["romaji"].get<std::string>();
            if (!romaji.empty())
            {
                result.title = romaji;
                result.matching_titles.push_back(romaji);
            }
        }
        if (media["title"].contains("english") && !media["title"]["english"].is_null())
        {
            std::string english = media["title"]["english"].get<std::string>();
            if (!english.empty())
            {
                if (result.title.empty())
                    result.title = english;
                result.matching_titles.push_back(english);
            }
        }
        if (media["title"].contains("native") && !media["title"]["native"].is_null())
        {
            std::string native = media["title"]["native"].get<std::string>();
            if (!native.empty())
                result.matching_titles.push_back(native);
        }
    }

    if (media.contains("synonyms") && media["synonyms"].is_array())
    {
        for (const auto& syn : media["synonyms"])
        {
            if (syn.is_string())
            {
                std::string s = syn.get<std::string>();
                if (!s.empty())
                    result.matching_titles.push_back(s);
            }
        }
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
            if (!j.contains("errors") && j.contains("data") && !j["data"].is_null()
                && j["data"].contains("Page") && !j["data"]["Page"].is_null()
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

                std::lock_guard<std::mutex> lock(anilist_cache_mutex);
                anilist_page_cache[search] = results;
            }
        }
        catch (...)
        {
        }
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
            if (!j.contains("errors") && j.contains("data") && !j["data"].is_null()
                && j["data"].contains("Media") && !j["data"]["Media"].is_null())
            {
                result = parse_anilist_media(j["data"]["Media"]);
                if (result)
                {
                    std::lock_guard<std::mutex> lock(anilist_cache_mutex);
                    anilist_id_cache[result->id] = *result;
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

// Anime lookup logic processed in worker thread
static void process_anime_lookup(rich_presence_state& state, const std::string& filename_str, const std::string& file_path_str, int current_load_id)
{
    if (filename_str.empty())
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

    if (!has_anime_indicator)
        return;

    parsed_filename_info pinfo = parse_anime_filename(filename_str, elements);
    if (pinfo.title.empty())
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

    for (const auto& q_str : search_queries)
    {
        if (state.worker_stop.load(std::memory_order_acquire) || state.file_load_id != current_load_id)
            return;

        auto page = search_anilist_page(q_str);
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

        auto base_page = search_anilist_page(a_title);
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
    int current_resolved_ep = parsed_ep;

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

    // Commit results under lock
    std::lock_guard<std::mutex> lock(state.anime_mutex);
    if (state.file_load_id == current_load_id)
    {
        state.is_anime = true;
        state.anime_title = current_resolved_media.title;
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
            filename_str = std::move(state.pending_filename);
            file_path_str = std::move(state.pending_file_path);
            state.has_pending_lookup = false;
        }

        try
        {
            process_anime_lookup(state, filename_str, file_path_str, current_load_id);
        }
        catch (...)
        {
        }
    }
}

// React to configuration or eligibility changes
static void on_eligibility_changed(rich_presence_state& state)
{
    state.force_update = true;
    bool eligible = is_window_eligible(state);

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
        if (state.media_has_video && !state.current_file_path.empty())
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
                std::string filename_str = fs::path(state.current_file_path).filename().string();
                {
                    std::lock_guard<std::mutex> lock(state.worker_mutex);
                    state.pending_load_id = current_load_id;
                    state.pending_filename = filename_str;
                    state.pending_file_path = state.current_file_path;
                    state.has_pending_lookup = true;
                }
                state.worker_cv.notify_one();
            }
        }
    }
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
            try
            {
                application_id = std::stoll(msg->args[1]);
                mpv_print(state.mpv, std::format("Received application_id: {}", application_id));
                state.force_update = true;
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

static void handle_file_loaded(rich_presence_state& state)
{
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
            mpv_get_property(state.mpv, std::format("track-list/{}/image", i).c_str(), MPV_FORMAT_FLAG, &is_image);
            if (is_image == 0)
                state.media_has_video = true;
        }
    }

    std::string media_artist = get_mpv_osd_string(state.mpv, "metadata/by-key/Artist");
    if (media_artist.empty())
        media_artist = get_mpv_osd_string(state.mpv, "metadata/by-key/artist");
    if (media_artist.empty())
        media_artist = get_mpv_osd_string(state.mpv, "metadata/by-key/album_artist");
    std::string media_title = get_mpv_osd_string(state.mpv, "media-title");

    state.media_artist = std::move(media_artist);
    state.media_title = std::move(media_title);

    // Get full file path for folder filtering
    state.current_file_path = get_mpv_string(state.mpv, "path");

    std::string filename_str = get_mpv_string(state.mpv, "filename");
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
    if (state.media_has_video && is_window_eligible(state))
    {
        std::lock_guard<std::mutex> lock(state.worker_mutex);
        state.pending_load_id = current_load_id;
        state.pending_filename = filename_str;
        state.pending_file_path = state.current_file_path;
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
    {
        state.is_rpc_visible = true;
        state.retry_backoff_ms = 0;
        state.needs_retry = false;
        return;
    }

    int32_t code = state.discord_api->Discord_ClientResult_ErrorCode(result);
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
    mpv_print(state.mpv, "Pinging rich_presence_conf for config data...");
    auto ping_args = std::array<const char*, 4> { "script-message-to", "rich_presence_conf", "ping", nullptr };
    mpv_command(state.mpv, ping_args.data());

    std::atomic<bool> is_ready = false;
    int64_t application_id = 0;

    mpv_print(state.mpv, "Initializing Discord Social SDK...");
    auto discord_init = std::jthread { [&state, &is_ready]() {
        try
        {
            auto embedded_fs = cmrc::mpvrp::get_filesystem();
            auto filename = (*embedded_fs.iterate_directory(DISCORD_SDK_DIR).begin()).filename();
            auto embedded_sdk = embedded_fs.open(std::format("{}/{}", DISCORD_SDK_DIR, filename));

            uint64_t sdk_hash = compute_fnv1a64(embedded_sdk.begin(), embedded_sdk.size());
            auto target_dir = fs::temp_directory_path() / "mpv-rich-presence" / std::format("sdk_{:016x}", sdk_hash);
            std::error_code ec;
            fs::create_directories(target_dir, ec);

            auto temp_sdk_path = target_dir / filename;
            bool needs_write = true;
            if (fs::exists(temp_sdk_path, ec))
            {
                auto existing_size = fs::file_size(temp_sdk_path, ec);
                if (!ec && existing_size == embedded_sdk.size())
                {
                    needs_write = false;
                }
            }

            if (needs_write)
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
                        if (!fs::exists(temp_sdk_path, ec) || fs::file_size(temp_sdk_path, ec) != embedded_sdk.size())
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
            state.discord_api->Discord_Client_AddLogCallback(&state.discord->get(), on_discord_log, nullptr, &state, Discord_LoggingSeverity::Warning);

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

            if (event->event_id == MPV_EVENT_CLIENT_MESSAGE)
            {
                handle_client_message(state, application_id, (mpv_event_client_message*)event->data);
            }
            else if (event->event_id == MPV_EVENT_FILE_LOADED)
            {
                handle_file_loaded(state);
            }
            else if (event->event_id == MPV_EVENT_END_FILE)
            {
                state.media_has_audio = false;
                state.media_has_video = false;
                state.media_title.clear();
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

        if (!is_ready)
            continue;

        state.discord_api->Discord_RunCallbacks();

        if (state.discord_api->Discord_Client_GetApplicationId(&state.discord->get()) != application_id)
            state.discord_api->Discord_Client_SetApplicationId(&state.discord->get(), application_id);

        if (application_id == 0)
            continue;

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

        std::string dur_str = format_time_duration(media_time_pos_s, (media_time_left_s > 0.0) ? (media_time_pos_s + media_time_left_s) : -1.0);

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

                    if (is_caching == 1)
                        state_str += std::format(" (Buffering... {})", dur_str);
                    else if (is_media_paused == 1)
                        state_str += std::format(" ({})", dur_str);
                }
                else if (!state.anime_release_group.empty())
                {
                    state_str = std::format("by {}", state.anime_release_group);
                    if (is_caching == 1)
                        state_str += std::format(" (Buffering... {})", dur_str);
                    else if (is_media_paused == 1)
                        state_str += std::format(" ({})", dur_str);
                }
                else if (is_caching == 1)
                {
                    state_str = std::format("Buffering... ({})", dur_str);
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
            activity_name = truncate_utf8_bytes(activity_name, 128);
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

            // AniList link button
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
            // Fallback for non-anime files
            std::string song_info;
            if (!state.media_artist.empty() && !state.media_title.empty())
            {
                std::string title_lower = to_lower(state.media_title);
                std::string artist_lower = to_lower(state.media_artist);
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

            activity_name = truncate_utf8_bytes(activity_name, 128);

            state.discord_api->Discord_Activity_SetName(&activity.get(), { activity_name.data(), activity_name.size() });

            std::string fallback_details = !song_info.empty() ? song_info : state.media_title;
            if (!fallback_details.empty())
            {
                Discord_String d_details = { fallback_details.data(), fallback_details.size() };
                state.discord_api->Discord_Activity_SetDetails(&activity.get(), &d_details);
            }

            if (is_caching == 1)
            {
                std::string b_state = std::format("Buffering... ({})", dur_str);
                Discord_String d_state = { b_state.data(), b_state.size() };
                state.discord_api->Discord_Activity_SetState(&activity.get(), &d_state);
            }
            else if (is_media_paused == 1)
            {
                Discord_String d_state = { dur_str.data(), dur_str.size() };
                state.discord_api->Discord_Activity_SetState(&activity.get(), &d_state);
            }
        }

        // Play/pause and buffering indicator
        const std::string& icon_url = (is_media_paused == 1 || is_caching == 1) ? ICON_PAUSED : ICON_PLAYING;
        state.discord_api->Discord_ActivityAssets_SetSmallImage(&assets.get(), { icon_url.data(), icon_url.size() });

        std::string small_tooltip;
        if (is_caching == 1)
            small_tooltip = "Buffering...";
        else if (is_media_paused == 1)
            small_tooltip = std::format("Paused ({})", dur_str);
        else if (current_speed != 1.0)
            small_tooltip = std::format("Playing ({:.2f}x)", current_speed);
        else
            small_tooltip = "Playing";

        state.discord_api->Discord_ActivityAssets_SetSmallText(&assets.get(), { small_tooltip.data(), small_tooltip.size() });
        state.discord_api->Discord_Activity_SetAssets(&activity.get(), &assets.get());

        // Timestamps
        if (is_caching == 1)
        {
            // Omit timestamps while buffering to avoid reusing an earlier manual pause timer
        }
        else if (is_media_paused == 1)
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
            uint64_t start = now_ms - static_cast<uint64_t>(media_time_pos_s * 1000 / current_speed);

            state.discord_api->Discord_ActivityTimestamps_SetStart(&timestamps.get(), start);

            if (media_time_left_s > 0.0 && playtime_left_s > 0.0)
            {
                uint64_t end = now_ms + static_cast<uint64_t>(playtime_left_s * 1000);
                state.discord_api->Discord_ActivityTimestamps_SetEnd(&timestamps.get(), end);
            }

            state.discord_api->Discord_Activity_SetTimestamps(&activity.get(), &timestamps.get());
        }

        state.discord_api->Discord_Client_UpdateRichPresence(&state.discord->get(), &activity.get(), on_discord_update_rich_presence, nullptr, &state);
    }

    // Shut down gracefully
    mpv_print(state.mpv, "Shutting down...");

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
