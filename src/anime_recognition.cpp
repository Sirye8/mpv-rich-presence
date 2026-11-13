// SPDX-License-Identifier: AGPL-3.0-or-later
#include "mpv_rich_presence/anime_recognition.hpp"
#include "mpv_rich_presence/string_utils.hpp"

#include <anitomy/anitomy.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <codecvt>
#include <format>
#include <locale>
#include <regex>
#include <string_view>
#include <tuple>
#include <unordered_set>

namespace mpvrp
{
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

static const std::regex video_episode_marker(
    R"((?:^|[ ._-])(?:S[0-9]{1,2}[ ._-]*E[0-9]{1,3}|[0-9]{1,2}x[0-9]{2,3})(?:$|[^0-9]))",
    std::regex::icase);

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

std::optional<folder_video_name> video_name_from_folder(
    const std::string& filename, const std::string& folder, double duration,
    const std::string& embedded_title)
{
    const auto stem = filename.substr(0, filename.find_last_of('.'));
    static const std::regex numbered(R"(^(?:(?:ep(?:isode)?)[ ._-]*)?([0-9]{1,4})(?:v[0-9]+)?$)", std::regex::icase);
    static const std::regex season_numbered(R"(^s([0-9]{1,2})[ ._-]*e([0-9]{1,3})(?:v[0-9]+)?$)", std::regex::icase);
    static const std::regex unnamed(R"(^(?:movie|film|video|untitled|feature|main)(?:[ ._-]+[0-9]+)?$)", std::regex::icase);
    static const std::regex generic_folder(
        R"(^(?:season[ ._-]*[0-9]+|s[0-9]+|part[ ._-]*[0-9]+|extras?|bonus|downloads?|videos?|movies?|anime|tv|samples?|untitled|media|films?|episodes?)$)",
        std::regex::icase);
    static const std::regex year_token(R"(\s*[\[(]((?:18|19|20)[0-9]{2})[\])]\s*$)");
    if (!embedded_title.empty() && embedded_title != filename && embedded_title != stem &&
        !std::regex_match(embedded_title, unnamed) && !std::regex_match(embedded_title, numbered) &&
        !std::regex_match(embedded_title, season_numbered))
        return std::nullopt;
    if (folder.empty() || folder.size() > 160 || folder.find_first_of("/\\?&#:") != std::string::npos ||
        std::any_of(folder.begin(), folder.end(), [](unsigned char ch) { return ch < 32; }))
        return std::nullopt;

    std::smatch number;
    folder_video_name result;
    if (std::regex_match(stem, number, season_numbered))
    {
        result.season = std::stoi(number[1].str());
        result.episode = std::stoi(number[2].str());
    }
    else if (std::regex_match(stem, number, numbered))
    {
        const int episode = std::stoi(number[1].str());
        if (episode >= 1800 && episode <= 2099) return std::nullopt;
        result.episode = episode;
    }
    else if (duration < 35 * 60 || !std::regex_match(stem, unnamed))
        return std::nullopt;

    auto title = folder;
    std::smatch year;
    if (std::regex_search(title, year, year_token))
    {
        result.year = year[1].str();
        title.erase(year.position());
    }
    auto [cleaned, season, part] = clean_anime_title(title);
    if (!result.episode) cleaned = std::move(title);
    if (cleaned.size() < 3 || std::regex_match(cleaned, generic_folder) ||
        !std::any_of(cleaned.begin(), cleaned.end(), [](unsigned char ch) { return std::isalpha(ch) || ch >= 128; }))
        return std::nullopt;
    result.title = std::move(cleaned);
    if (result.episode && !result.season && season > 0) result.season = season;
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

static bool exact_series_title(const std::string& query, const anilist_media& media, int season)
{
    const auto expected = to_lower(query);
    static const std::array<std::string_view, 10> roman = {
        "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x"};
    return std::any_of(media.matching_titles.begin(), media.matching_titles.end(),
        [&](const auto& alias) {
            const auto title = to_lower(alias);
            if (season <= 1) return title == expected;
            const auto number = std::to_string(season);
            return title == expected + " " + number || title == expected + " s" + number ||
                title == expected + " season " + number ||
                title == expected + " " + get_ordinal_string(season) + " season" ||
                (season <= static_cast<int>(roman.size()) && title == expected + " " + std::string(roman[season - 1]));
        });
}

static bool is_anilist_tracking_series(const anilist_media& media)
{
    return media.format == "TV" || media.format == "ONA";
}

static std::optional<int> walk_to_season(anilist_media& media, int episode,
    const anime_catalog& catalog, const std::function<bool()>& cancelled, bool& unambiguous,
    bool try_absolute)
{
    // Some releases include the full cour name but retain series-wide numbers.
    // Use an exact prequel chain only when its totals put that number inside the
    // named entry. Otherwise keep the usual forward walk for continued numbering.
    if (try_absolute && media.episodes > 0 && episode > media.episodes)
    {
        int earlier = 0;
        int previous = media.prequel_id;
        std::unordered_set<int> seen{media.id};
        for (int step = 0; previous > 0 && step < 20 && seen.insert(previous).second; ++step)
        {
            if (cancelled()) return std::nullopt;
            auto prequel = catalog.by_id(previous);
            if (!prequel || prequel->episodes <= 0) break;
            earlier += prequel->episodes;
            previous = prequel->prequel_id;
        }
        if (previous == -1 && episode > earlier && episode - earlier <= media.episodes)
            return episode - earlier;
    }
    std::unordered_set<int> visited{media.id};
    for (int step = 0; step < 20 && episode > 0 && media.episodes > 0 && episode > media.episodes; ++step)
    {
        if (cancelled()) return std::nullopt;
        if (media.sequel_id <= 0 || !visited.insert(media.sequel_id).second)
        {
            unambiguous = false;
            break;
        }
        auto next = catalog.by_id(media.sequel_id);
        if (!next)
        {
            unambiguous = false;
            break;
        }
        unambiguous = unambiguous && media.unambiguous_sequel &&
            is_anilist_tracking_series(media) && is_anilist_tracking_series(*next);
        episode -= media.episodes;
        media = std::move(*next);
    }
    if (media.episodes > 0 && episode > media.episodes) unambiguous = false;
    return episode;
}

// Anime lookup logic processed in worker thread
std::optional<anime_match> recognize_anime(const std::string& filename_str,
    const nlohmann::json& correction, const anime_catalog& catalog,
    const std::function<bool()>& cancelled, bool& confirmed, const std::string& folder,
    const std::optional<folder_video_name>& folder_name)
{
    if (correction.value("not_anime", false) || filename_str.empty())
        return std::nullopt;

    static const std::regex clip_name(
        R"(^(?:(?:img|vid|dsc|dji|pxl|mov|mvi|gopr|g[hp]0[0-9])[ _.-]*[0-9]|(?:screen[ _.-]*(?:recording|capture)|untitled|test[ _.-]*clip)(?:[ _.-]|$)))",
        std::regex::icase);
    if (correction.value("automatic", false) && !correction.contains("id") && !correction.contains("offset") &&
        std::regex_search(filename_str, clip_name)) return std::nullopt;
    if (cancelled())
        return std::nullopt;

    anitomy::Anitomy anitomy;
    anitomy.Parse(s2ws(filename_str));
    auto& elements = anitomy.elements();

    // Check for anime indicators
    bool has_anime_indicator = elements.count(anitomy::kElementAnimeTitle)
        || elements.count(anitomy::kElementEpisodeNumber)
        || elements.count(anitomy::kElementAnimeSeason)
        || elements.count(anitomy::kElementReleaseGroup);

    if (!has_anime_indicator && !correction.contains("id") && !folder_name)
        return std::nullopt;

    parsed_filename_info pinfo = parse_anime_filename(filename_str, elements);
    if (pinfo.title.empty() && !correction.contains("id") && !folder_name)
        return std::nullopt;

    std::string a_title = pinfo.title;
    int parsed_season = pinfo.season;
    int parsed_part = pinfo.part;
    int parsed_ep = pinfo.episode;
    std::string a_group = pinfo.release_group;

    std::vector<std::string> search_queries;
    const bool automatic = correction.value("automatic", false);
    const bool automatic_match = automatic && !correction.contains("id") && !correction.contains("offset");
    const bool title_from_folder = automatic_match && folder_name.has_value();
    if (title_from_folder)
    {
        a_title = folder_name->title;
        parsed_season = folder_name->season.value_or(0);
        parsed_ep = folder_name->episode.value_or(0);
        parsed_part = 0;
    }
    static const std::regex supplementary_video(
        R"((?:^|[ ._(\[\-])(?:nc[ ._-]*(?:op|ed)|(?:op|ed)[ ._-]*[0-9]+|creditless|trailer|sample)(?=$|[ ._()\[\]-]|[0-9])|(?:^|\s-\s)(?:opening|ending)(?:[ ._-]*[0-9]+|(?=\s*[\[(])))",
        std::regex::icase);
    if (automatic_match && !std::regex_search(filename_str, video_episode_marker) &&
        std::regex_search(filename_str, supplementary_video))
        return std::nullopt;
    static const std::regex dual_episode_number(
        R"(\bS[0-9]{1,2}E([0-9]{1,3})[ ._-]+\(0*([0-9]{1,3})\)(?=[ ._\[]|$))",
        std::regex::icase);
    std::smatch dual_number;
    const bool has_absolute_number = automatic_match && parsed_season > 1 &&
        std::regex_search(filename_str, dual_number, dual_episode_number) &&
        std::stoi(dual_number[2].str()) > std::stoi(dual_number[1].str());
    if (has_absolute_number)
    {
        // A release can give a season-local number and a different series-wide
        // number. Anchor the latter to the base title, then resolve its sequel.
        parsed_ep = std::stoi(dual_number[2].str());
        parsed_season = 0;
    }
    static const std::regex named_episode(
        R"(\b(?:episode|ep)[ ._-]+([0-9]{1,4})(?![0-9]|[.-][0-9]))", std::regex::icase);
    std::smatch named_number;
    const bool has_named_episode = automatic_match && !title_from_folder && !has_absolute_number &&
        std::regex_search(filename_str, named_number, named_episode);
    if (has_named_episode)
        parsed_ep = std::stoi(named_number[1].str());
    // In a dashed scene filename the episode subtitle can survive Anitomy
    // and make a direct catalog title look fuzzy.
    static const std::regex dashed_subtitle(
        R"(\s+-\s*([0-9]{1,4})(?:v[0-9]+)?\s*-\s+\S)", std::regex::icase);
    std::smatch subtitle;
    if (automatic_match && std::regex_search(filename_str, subtitle, dashed_subtitle))
    {
        auto prefix = filename_str.substr(0, subtitle.position());
        if (prefix.starts_with('['))
            if (const auto end = prefix.find(']'); end != std::string::npos)
                prefix.erase(0, end + 1);
        auto [title, season, part] = clean_anime_title(prefix);
        if (!title.empty() && parsed_season == 0 && season == 0 && part == 0)
        {
            a_title = std::move(title);
            parsed_ep = std::stoi(subtitle[1].str());
        }
    }
    static const std::regex film_marker(
        R"((?:^|[ ._(-])(?:the[ ._-]+movie|(?:movie|film)[ ._-]+\d{1,2})(?=$|[^A-Za-z0-9]))",
        std::regex::icase);
    const bool explicit_film = automatic_match && (std::regex_search(filename_str, film_marker) ||
        (title_from_folder && !folder_name->episode));
    auto film_year = title_from_folder ? folder_name->year : ws2s(elements.get(anitomy::kElementAnimeYear));
    if (explicit_film && !title_from_folder)
    {
        // Release names sometimes put the franchise, year and subtitle in
        // separate fields. Try the full title before a broad franchise search.
        auto query = filename_str.substr(0, filename_str.find_last_of('.'));
        static const std::regex release_group(R"(^\[[^\]]+\]\s*)");
        static const std::regex release_tags(
            R"(\s*[\[(][^\])]*(?:[0-9]{3,4}p|bluray|bdrip|dual-audio|multi-subs|[a-f0-9]{8})[^\])]*[\])].*$)",
            std::regex::icase);
        static const std::regex year_token(R"([\[(]((?:19|20)[0-9]{2})[\])])");
        std::smatch year_match;
        if (std::regex_search(query, year_match, year_token))
            film_year = year_match[1].str();
        query = std::regex_replace(query, release_group, "");
        query = std::regex_replace(query, release_tags, "");
        query = std::regex_replace(query, year_token, " ");
        for (auto& ch : query) if (ch == '.' || ch == '_') ch = ' ';
        query = std::regex_replace(query, std::regex(R"(\s+)"), " ");
        if (const auto last = query.find_last_not_of(" -"); last != std::string::npos)
        {
            query.erase(last + 1);
            if (query != a_title) search_queries.push_back(query);
        }
    }
    static const std::regex named_special(
        R"((?:\(|\[)(?:OVA|OAD|Special)(?:\)|\])\s*-\s*([^\[\(]+))", std::regex::icase);
    std::smatch special;
    if (automatic_match && std::regex_search(filename_str, special, named_special))
    {
        auto name = special[1].str();
        name.erase(name.find_last_not_of(" \t") + 1);
        if (!name.empty())
        {
            const auto query = a_title + " " + name;
            auto results = catalog.search(query, confirmed);
            const auto selected = std::find_if(results.begin(), results.end(), [&](const auto& media) {
                return (media.format == "OVA" || media.format == "SPECIAL" || media.format == "ONA") &&
                    std::any_of(media.matching_titles.begin(), media.matching_titles.end(),
                        [&](const auto& alias) { return to_lower(alias).ends_with(to_lower(name)); });
            });
            if (selected != results.end())
            {
                anime_match match;
                match.media = *selected;
                match.release_group = std::move(a_group);
                match.episode_review = "Recap or special";
                match.raw_episode = parsed_ep;
                match.episode = selected->episodes == 1 ? 1 : 0;
                return match;
            }
        }
    }
    std::string ord_str = get_ordinal_string(parsed_season);

    // A matching series prefix plus a named folder arc can disambiguate a
    // distributor's season number. Compare the folder to the returned aliases
    // locally. Provider queries still contain only the filename title.
    std::string arc_query;
    std::string arc_search_title;
    if (automatic_match && !title_from_folder && parsed_season > 0 && !has_absolute_number && !folder.empty())
    {
        // Cut the filename at its episode marker so a scene episode subtitle
        // cannot become part of the series title used for the arc check.
        std::smatch marker;
        arc_search_title = a_title;
        if (std::regex_search(filename_str, marker, video_episode_marker))
        {
            auto before_marker = filename_str.substr(0, marker.position());
            if (before_marker.starts_with('['))
                if (const auto end = before_marker.find(']'); end != std::string::npos)
                    before_marker.erase(0, end + 1);
            std::replace(before_marker.begin(), before_marker.end(), '.', ' ');
            auto cleaned = clean_anime_title(before_marker);
            if (!std::get<0>(cleaned).empty()) arc_search_title = std::move(std::get<0>(cleaned));
        }
        const auto folder_lower = to_lower(folder);
        const auto title_lower = to_lower(arc_search_title);
        const auto first_space = title_lower.find(' ');
        const auto shared_prefix = title_lower.substr(0, first_space);
        auto prefix = title_lower + " ";
        if (!folder_lower.starts_with(prefix) && shared_prefix.size() >= 5)
        {
            prefix = shared_prefix + " ";
            if (folder_lower.starts_with(prefix)) arc_search_title = shared_prefix;
        }
        if (folder_lower.starts_with(prefix))
        {
            static const std::regex arc_suffix(
                R"(^[\w -]{3,80}$)");
            static const std::regex generic_suffix(
                R"(^(?:s[0-9]+|season\b|part\b|cour\b|extras?\b|complete\b|[0-9]).*)",
                std::regex::icase);
            const auto suffix = folder.substr(prefix.size());
            if (std::regex_match(suffix, arc_suffix) && !std::regex_match(suffix, generic_suffix))
            {
                arc_query = folder;
                search_queries.insert(search_queries.begin(), arc_search_title);
            }
        }
    }

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
        candidate = catalog.by_id(correction.at("id"));
        if (!candidate) return std::nullopt;
        search_queries.clear();
    }

    const auto valid_film = [&](const anilist_media& media) {
        return !explicit_film || (media.format == "MOVIE" &&
            (film_year.size() != 4 || media.release_year <= 0 || film_year == std::to_string(media.release_year)));
    };
    // For automatic numbered series, prefer a direct title match within the
    // same search results before using the original fuzzy/episode ranking.
    // This keeps absolute numbering anchored at the base series.
    const bool prefer_exact_series = automatic_match && !explicit_film && parsed_ep > 0;
    bool ambiguous_entry = false;
    const auto release_year = ws2s(elements.get(anitomy::kElementAnimeYear));
    const auto choose_exact = [&](const auto& page, const auto& matches) -> std::optional<anilist_media> {
        std::vector<const anilist_media*> exact;
        std::unordered_set<int> ids;
        for (const auto& media : page)
            if (matches(media) && ids.insert(media.id).second) exact.push_back(&media);
        if (exact.size()>1 && release_year.size()==4)
        {
            auto dated = exact;
            std::erase_if(dated,[&](const auto* media) {
                return std::to_string(media->release_year)!=release_year;
            });
            if (!dated.empty()) exact = std::move(dated);
        }
        if (exact.size()>1 && parsed_season<=1 && parsed_part==0 && parsed_ep>0)
        {
            auto roots = exact;
            std::erase_if(roots,[&](const auto* media) {
                return !std::any_of(exact.begin(),exact.end(),[&](const auto* other) {
                    return media->sequel_id==other->id && other->sequel_id!=media->id;
                }) || std::any_of(exact.begin(),exact.end(),[&](const auto* other) {
                    return other->sequel_id==media->id;
                });
            });
            // A direct link can establish a two-entry sequence. An unrelated
            // candidate still leaves the filename ambiguous.
            if (exact.size()==2 && roots.size()==1) exact = std::move(roots);
        }
        ambiguous_entry = exact.size()>1;
        return exact.empty() ? std::nullopt : std::optional<anilist_media>(*exact.front());
    };
    std::unordered_set<std::string> searched;
    for (const auto& q_str : search_queries)
    {
        if (cancelled())
            return std::nullopt;

        if (!searched.insert(q_str).second) continue;
        auto page = catalog.search(q_str, confirmed);
        if (page.empty())
            continue;

        if (!arc_query.empty() && q_str == arc_search_title)
        {
            const auto exact_arc = choose_exact(page, [&](const auto& media) {
                return is_anilist_tracking_series(media) && exact_series_title(arc_query, media, 0);
            });
            if (exact_arc)
            {
                candidate = *exact_arc;
                break;
            }
            continue;
        }

        if (prefer_exact_series)
        {
            const auto exact = choose_exact(page, [&](const auto& media) {
                return is_anilist_tracking_series(media) &&
                    (parsed_part == 0 ? exact_series_title(a_title, media, parsed_season)
                                      : exact_series_title(q_str, media, 0));
            });
            if (exact)
            {
                candidate = *exact;
                break;
            }
            if (parsed_season <= 1 && parsed_part == 0)
            {
                const auto prefix = to_lower(a_title) + ": ";
                const auto base = std::find_if(page.begin(), page.end(), [&](const auto& media) {
                    return is_anilist_tracking_series(media) &&
                        (to_lower(media.romaji).starts_with(prefix) || to_lower(media.english).starts_with(prefix)) &&
                        std::any_of(page.begin(), page.end(), [&](const auto& other) {
                            return other.id == media.sequel_id && is_anilist_tracking_series(other) &&
                                is_media_title_match(a_title, other);
                        }) &&
                        std::none_of(page.begin(), page.end(), [&](const auto& other) {
                            return other.sequel_id == media.id && is_anilist_tracking_series(other) &&
                                is_media_title_match(a_title, other);
                        });
                });
                if (base != page.end())
                {
                    candidate = *base;
                    break;
                }
            }
        }

        std::optional<anilist_media> best = std::nullopt;
        for (const auto& m : page)
        {
            if (!valid_film(m) || (title_from_folder
                ? !exact_series_title(a_title, m, parsed_season)
                : !is_media_title_match(explicit_film ? q_str : a_title, m)))
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

            if (title_from_folder && best && best->id != m.id)
                return std::nullopt;
            if (!title_from_folder && (explicit_film || (parsed_ep > 0 && m.episodes > 0 && parsed_ep <= m.episodes)))
            {
                best = m;
                break;
            }

            if (!best)
                best = m;
        }

        if (best)
        {
            if (!automatic_match && parsed_ep>0 && !correction.contains("id") &&
                exact_series_title(a_title,*best,parsed_season))
                choose_exact(page,[&](const auto& media) {
                    return is_anilist_tracking_series(media) && exact_series_title(a_title,media,parsed_season);
                });
            candidate = best;
            break;
        }
    }

    if (!candidate && (confirmed || !searched.contains(a_title)))
    {
        if (cancelled())
            return std::nullopt;

        auto base_page = catalog.search(a_title, confirmed);
        for (const auto& m : base_page)
        {
            if (valid_film(m) && (title_from_folder
                ? exact_series_title(a_title, m, parsed_season) : is_media_title_match(a_title, m)))
            {
                candidate = m;
                break;
            }
        }
    }

    if (!candidate)
        return std::nullopt;

    if (ambiguous_entry)
    {
        anime_match match;
        match.episode_review = "Multiple AniList entries match this title";
        return match;
    }

    if (explicit_film && candidate->format == "MOVIE")
        parsed_ep = 0;
    anilist_media current_resolved_media = *candidate;
    if (correction.contains("id") && correction.contains("episode"))
    {
        const auto episode = correction.at("episode").get<int>();
        anime_match match;
        match.media = *candidate;
        match.raw_episode = parsed_ep;
        match.episode = episode;
        match.release_group = std::move(a_group);
        match.watch_episode = episode>0 && (candidate->episodes<=0 || episode<=candidate->episodes)
            && is_anilist_tracking_series(*candidate);
        if (!match.watch_episode) match.episode_review = "Corrected episode is outside the trackable range";
        return match;
    }
    parsed_ep += correction.value("offset", 0);
    bool unambiguous_numbering = true;
    const bool matched_folder_arc = !arc_query.empty() && exact_series_title(arc_query, *candidate, 0);
    const bool named_entry = exact_series_title(a_title, *candidate, 0) ||
        (parsed_part > 0 && exact_series_title(std::format("{} Part {}", a_title, parsed_part), *candidate, 0));
    const auto resolved_episode = walk_to_season(current_resolved_media, parsed_ep,
        catalog, cancelled, unambiguous_numbering,
        automatic_match && (matched_folder_arc || (parsed_season == 0 && named_entry)));
    if (!resolved_episode) return std::nullopt;
    const int current_resolved_ep = *resolved_episode;

    if (cancelled())
        return std::nullopt;

    const auto lower_title = to_lower(a_title);
    const auto exact_title = [&](const anilist_media& media) {
        if (title_from_folder)
            return exact_series_title(a_title, media, parsed_season);
        return to_lower(media.title) == lower_title ||
            std::any_of(media.matching_titles.begin(), media.matching_titles.end(),
                [&](const auto& alias) { return to_lower(alias) == lower_title; });
    };
    const bool fuzzy_episode = !matched_folder_arc && !correction.contains("id") && !correction.contains("offset") &&
        !exact_title(*candidate) && !exact_title(current_resolved_media) &&
        std::regex_search(filename_str, video_episode_marker);
    int conflicting_anime_minutes = 0;
    if (!correction.contains("id") && pinfo.episode == 0 && pinfo.season == 0 && pinfo.part == 0 &&
        current_resolved_media.format != "MOVIE" && current_resolved_media.duration_minutes > 0 &&
        current_resolved_media.duration_minutes <= 30 && current_resolved_media.release_year > 0)
    {
        const auto year = ws2s(elements.get(anitomy::kElementAnimeYear));
        if (year.size() == 4 && year != std::to_string(current_resolved_media.release_year) &&
            std::none_of(current_resolved_media.matching_titles.begin(), current_resolved_media.matching_titles.end(),
                [&](const auto& alias) { return to_lower(alias) == lower_title; }))
            conflicting_anime_minutes = current_resolved_media.duration_minutes;
    }

    anime_match match;
    static const std::regex whole_episode("[0-9]+");
    const bool integer_episode = has_absolute_number || has_named_episode ||
        (title_from_folder && folder_name->episode) ||
        (elements.count(anitomy::kElementEpisodeNumber) == 1 &&
            std::regex_match(ws2s(elements.get(anitomy::kElementEpisodeNumber)), whole_episode));
    match.episode_label.clear();
    if (!integer_episode && elements.count(anitomy::kElementEpisodeNumber)>0)
    {
        for (const auto& value : elements.get_all(anitomy::kElementEpisodeNumber))
        {
            if (!match.episode_label.empty()) match.episode_label += "-";
            match.episode_label += ws2s(value);
        }
    }
    if (integer_episode && parsed_ep==0) match.episode_label="0";
    match.episode_review.clear();
    const auto parsed_number = ws2s(elements.get(anitomy::kElementEpisodeNumber));
    static const std::regex decimal_episode(R"(^[0-9]+\.[0-9]+$)");
    static const std::regex extra_episode(
        R"((?:^|[^A-Za-z0-9])(?:recap|special|ova|oad)[ ._-]*[0-9]*(?:[^A-Za-z0-9]|$))", std::regex::icase);
    const auto title_pos = to_lower(filename_str).find(lower_title);
    const auto suffix = title_pos == std::string::npos ? filename_str : filename_str.substr(title_pos + a_title.size());
    if (std::regex_match(parsed_number, decimal_episode)) match.episode_review = "Fractional episode " + parsed_number;
    else if (std::regex_search(suffix, extra_episode)) match.episode_review = "Recap or special";
    else if (current_resolved_media.format == "SPECIAL" || current_resolved_media.format == "OVA" ||
        current_resolved_media.format == "TV_SHORT") match.episode_review = "Special or short-form entry";
    else if (!has_absolute_number && !has_named_episode && elements.count(anitomy::kElementEpisodeNumber) > 1) match.episode_review = "Multiple episode numbers";
    else if (integer_episode && parsed_ep == 0) match.episode_review = "Episode zero";
    else if (integer_episode && !unambiguous_numbering) match.episode_review = "Unclear season numbering";
    const bool eligible_episode = integer_episode && parsed_ep > 0 && current_resolved_ep > 0
        && (current_resolved_media.episodes <= 0 || current_resolved_ep <= current_resolved_media.episodes)
        && is_anilist_tracking_series(current_resolved_media);
    match.watch_episode = eligible_episode && unambiguous_numbering && match.episode_review.empty();
    match.conflicting_anime_minutes = conflicting_anime_minutes;
    match.fuzzy_episode = fuzzy_episode;
    match.episode = current_resolved_ep;
    match.raw_episode = parsed_ep;
    match.release_group = std::move(a_group);
    match.media = std::move(current_resolved_media);
    return match;
}

bool recognition_route::hides_anime(bool resolved, bool provider_match, bool unavailable) const
{
    return (source == "verify-movie" || source == "verify-tv") &&
        (!resolved || provider_match || source == "verify-movie" || runtime_conflict || unavailable);
}

recognition_route route_recognition(std::string source, bool allowed, bool has_store,
    bool pinned_anime, bool has_provider, bool anime_pending, bool anime_unavailable,
    bool has_anime, bool anime_complete, bool fuzzy_episode, int anime_minutes,
    int conflicting_anime_minutes, double duration)
{
    if (has_store && source == "auto" && allowed && has_anime && !pinned_anime && anime_complete)
    {
        if (conflicting_anime_minutes > 0 && duration >= (std::max)(60, 3 * conflicting_anime_minutes) * 60)
            return {"verify-movie"};
        if (fuzzy_episode)
            return {"verify-tv", anime_minutes > 0 && duration >= (std::max)(30, 2 * anime_minutes) * 60};
    }
    if (source == "local" || source == "anime" || has_provider || !allowed || has_anime ||
        pinned_anime || anime_pending || anime_unavailable)
        return {"original"};
    return {std::move(source)};
}
}
