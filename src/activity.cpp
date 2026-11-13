// SPDX-License-Identifier: AGPL-3.0-or-later
#include "mpv_rich_presence/activity.hpp"
#include "mpv_rich_presence/string_utils.hpp"

#include <algorithm>
#include <format>
#include <regex>
#include <sstream>

using nlohmann::json;

namespace mpvrp
{
    static bool public_url(const std::string& url)
    {
        static const std::regex valid(R"(^https://([a-zA-Z0-9-]+\.)+[a-zA-Z]{2,}(?:/[^\s?#]*)?$)");
        return url.size() <= 512 && std::regex_match(url,valid) && url.find(".localhost/") == std::string::npos && url.find(".local/") == std::string::npos;
    }

    static std::string render_template(const std::string& pattern, const json& fields)
    {
        std::istringstream input(pattern);
        std::string part, result;
        while (std::getline(input,part,'|'))
        {
            static const std::regex placeholder(R"(\{([a-zA-Z_][a-zA-Z_0-9]*)\})");
            if (std::any_of(std::sregex_iterator(part.begin(),part.end(),placeholder),std::sregex_iterator{},
                [&](const auto& token) { return !fields.contains(token[1].str()); })) continue;
            bool missing = false;
            for (auto& [key,value] : fields.items())
            {
                const auto token = "{" + key + "}";
                const auto& text = value.get_ref<const std::string&>();
                size_t offset = 0;
                while ((offset = part.find(token, offset)) != std::string::npos)
                {
                    if (text.empty()) { missing = true; break; }
                    part.replace(offset,token.size(),text);
                    offset += text.size();
                }
                if (missing) break;
            }
            const auto first = part.find_first_not_of(" \t\r\n");
            part = first == std::string::npos ? "" : part.substr(first,part.find_last_not_of(" \t\r\n")-first+1);
            if (!missing && !part.empty())
            {
                if (!result.empty()) result += " · ";
                result += part;
            }
        }
        return result;
    }

    json format_activity(const playback& m, const json& settings)
    {
        json options = settings;
        auto layout = m.anime ? "anime_layout" : m.provider.value("kind",m.video ? std::string("video") : std::string("audio")) + "_layout";
        if (settings.contains(layout) && settings.at(layout).is_object())
        {
            const auto& specific = settings.at(layout);
            if (specific.contains("preset")) options = json::object();
            options.merge_patch(specific);
        }
        const auto preset = options.value("preset", std::string("original"));
        const bool generic = preset == "private";
        const bool clean = preset == "clean";
        const auto language = options.value("language", std::string("original"));
        std::string title = m.anime_title;
        if (language == "english" && !m.english.empty()) title = m.english;
        if (language == "native" && !m.native.empty()) title = m.native;
        if (language == "romaji" && !m.romaji.empty()) title = m.romaji;

        std::string song = m.title;
        if (song.empty()) song = m.artist;
        else if (!m.artist.empty() && !to_lower(song).starts_with(to_lower(m.artist)))
            song = m.artist + " - " + song;

        const auto duration = format_time_duration(m.position, m.remaining > 0 ? m.position + m.remaining : -1);
        std::string name = "mpv", details, state;
        json card = {{"type", m.video ? "Watching" : "Listening"}};
        if (!generic)
        {
            if (m.anime)
            {
                name = details = title;
                auto ep = m.episode;
                const auto numbering = options.value("numbering", std::string("relative"));
                if (numbering == "original" && !m.raw_episode.empty()) ep = m.raw_episode;
                if (!m.episode_label.empty() && (preset != "original" || numbering != "relative")) ep = m.episode_label;
                if (!ep.empty() && options.value("episode", true))
                {
                    state = "Episode " + ep;
                    if (m.total > 0 && (numbering != "original" || ep == m.episode))
                        state += "/" + std::to_string(m.total);
                    if (numbering == "both" && !m.raw_episode.empty() && m.raw_episode != ep)
                        state += " (" + m.raw_episode + ")";
                }
                if (options.value("group", !clean) && !m.group.empty())
                    state += (state.empty() ? "by " : " by ") + m.group;
                if (options.value("art", true) && !m.cover.empty())
                {
                    card["large_image"] = m.cover;
                    card["large_text"] = truncate_utf8_bytes(title, 128);
                }
                if (options.value("button", true) && m.anilist_id > 0)
                    card["buttons"] = json::array({{{"label", "View on AniList"}, {"url", std::format("https://anilist.co/anime/{}", m.anilist_id)}}});
            }
            else
            {
                details = song;
                if ((!m.video || preset == "detailed" || clean) && !song.empty()) name = song;
                if (!m.video && !m.track_title.empty())
                {
                    name = m.track_title;
                    details = m.artist;
                    state = m.album;
                }
                else if (preset == "detailed" && !m.video && !m.title.empty())
                {
                    name = m.title;
                    details = m.artist;
                }
            }
            if (!m.provider.empty())
            {
                name = details = m.provider.value("name", song);
                state = m.provider.value("numbering", std::string{});
                if (m.provider.value("kind",std::string{}) == "stream")
                {
                    details = m.provider.value("stream_title", m.title);
                    if (m.provider.value("live",false)) state = "Live";
                }
                const auto kind = m.provider.value("kind", std::string{});
                if (kind == "recording")
                {
                    details = m.provider.value("artist", m.artist);
                    state = m.provider.value("album", m.album);
                }
                if (state.empty() && (kind == "movie" || preset == "detailed"))
                {
                    state = m.provider.value("year",std::string{});
                    auto genre = preset == "detailed" ? m.provider.value("genre",std::string{}) : std::string{};
                    if (!genre.empty()) state += (state.empty() ? "" : " · ") + genre;
                }
                if (options.value("art", true) && m.provider.contains("cover") && m.provider.at("cover") != "")
                {
                    card["large_image"] = m.provider.at("cover");
                    card["large_text"] = truncate_utf8_bytes(name,128);
                }
                if (options.value("button",true) && m.provider.contains("link"))
                {
                    auto source = m.provider.value("provider", std::string("provider"));
                    if (source.starts_with("tmdb-")) source = "TMDB";
                    else if (source == "tvmaze") source = "TVmaze";
                    else if (source == "wikidata") source = "Wikidata";
                    else if (source == "musicbrainz") source = "MusicBrainz";
                    card["buttons"] = json::array({{{"label", "View on " + source},
                                                    {"url",m.provider.at("link")}}});
                }
            }
            if (m.buffering)
                state += state.empty() ? "Buffering... (" + duration + ")" : " (Buffering... " + duration + ")";
            else if (m.paused)
                state += state.empty() ? duration : " (" + duration + ")";
        }

        if (!generic)
        {
            json fields={{"title",m.anime ? title : m.title},{"artist",m.artist},{"group",m.group},
                         {"episode",m.episode},{"original",m.raw_episode},{"album",m.album},{"state",state},
                         {"details",details},{"channel",m.channel},{"year",m.provider.value("year",std::string{})}};
            if (options.contains("headline")) name = render_template(options.at("headline"),fields);
            if (options.contains("details_template")) details = render_template(options.at("details_template"),fields);
            if (options.contains("state_template")) state = render_template(options.at("state_template"),fields);
            if (options.value("spoilers",false) && m.provider.contains("episode_title"))
                state = m.provider.at("episode_title");
            for (const auto* key : {"large_url","details_url","state_url"})
                if (options.contains(key) && public_url(options.at(key))) card[key] = options.at(key);
            if (options.contains("buttons"))
            {
                card.erase("buttons");
                for (const auto& button : options.at("buttons"))
                {
                    if (!button.is_object()) continue;
                    auto label = button.value("label",std::string{});
                    auto link = button.value("url",std::string{});
                    if (!label.empty() && public_url(link) && card["buttons"].size()<2)
                        card["buttons"].push_back({{"label",truncate_utf8_bytes(label,32)},{"url",link}});
                }
            }
        }
        if (options.contains("compact")) card["compact"] = options.at("compact");
        if (card.contains("buttons"))
        {
            static const std::regex youtube(R"(^https://(?:www\.)?youtube\.com/watch\?v=[A-Za-z0-9_-]{11}$)");
            const auto kind = m.provider.value("kind",std::string{});
            auto& buttons = card["buttons"];
            for (auto it=buttons.begin(); it!=buttons.end();)
            {
                const auto& button = *it;
                const auto url = button.value("url",std::string{});
                const bool invalid = !public_url(url) &&
                    !(kind=="stream" && !options.contains("buttons") && std::regex_match(url,youtube));
                if (invalid) it = buttons.erase(it);
                else ++it;
            }
            if (buttons.empty()) card.erase("buttons");
        }
        card["name"] = name.empty() ? "mpv" : truncate_utf8_bytes(name, 128);
        if (!details.empty() && options.value("details", true)) card["details"] = details;
        if (!state.empty() && options.value("state", true)) card["state"] = state;
        if (options.value("badge", !generic))
        {
            const std::string icons = "https://raw.githubusercontent.com/google/material-design-icons/3.0.1/av/2x_web/";
            card["small_image"] = icons + (m.paused || m.buffering ? "ic_pause_white_48dp.png" : "ic_play_arrow_white_48dp.png");
            if (options.value("tooltip", true))
            {
                if (m.buffering) card["small_text"] = "Buffering...";
                else if (m.paused) card["small_text"] = "Paused (" + duration + ")";
                else if (m.speed != 1) card["small_text"] = std::format("Playing ({:.2f}x)", m.speed);
                else card["small_text"] = "Playing";
            }
        }
        const auto timer = options.value("timer", std::string(generic ? "off" : "progress"));
        if (timer != "off" && !m.buffering)
        {
            if (m.paused)
            {
                if (m.pause_start) card["start"] = m.pause_start;
            }
            else if (timer == "session") card["start"] = m.session_start;
            else
            {
                if (timer != "remaining") card["start"] = m.now - static_cast<uint64_t>(m.position * 1000 / m.speed);
                if (m.remaining > 0 && m.playtime_left > 0)
                    card["end"] = m.now + static_cast<uint64_t>(m.playtime_left * 1000);
            }
        }
        for (const auto* key : {"details","state","large_text","small_text"})
            if (card.contains(key)) card[key] = truncate_utf8_bytes(card.at(key).get_ref<const std::string&>(),128);
        return card;
    }
}
