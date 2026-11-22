// SPDX-License-Identifier: AGPL-3.0-or-later
#include "mpv_rich_presence/activity.hpp"
#include "mpv_rich_presence/string_utils.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <regex>
#include <sstream>

using nlohmann::json;

namespace mpvrp
{
    static bool public_url(const std::string& url)
    {
        static const std::regex valid(R"(^https://([a-zA-Z0-9-]+\.)+[a-zA-Z]{2,}(?:/[^\s?#]*)?(?:\?[^\s#]*)?$)");
        return url.size() <= 512 && std::regex_match(url,valid) && url.find(".localhost/") == std::string::npos && url.find(".local/") == std::string::npos;
    }

    static std::string url_encode(const std::string& text, bool plus_for_space = false)
    {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        for (const unsigned char c : text)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
            else if (c == ' ' && plus_for_space) out += '+';
            else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
        }
        return out;
    }

    struct button_link { std::string site, url; };

    static const std::string MPV_LOGO = "https://raw.githubusercontent.com/mpv-player/mpv/master/etc/mpv-icon-8bit-128x128.png";

    // Website of the matched entry, used for the provider badge icon.
    static std::string site_domain(const playback& m)
    {
        if (m.anime) return m.anilist_id > 0 ? "anilist.co" : "";
        const auto source = m.provider.value("provider", std::string{});
        const auto link = m.provider.value("link", std::string{});
        if (source.starts_with("tmdb-")) return "themoviedb.org";
        if (source == "tvmaze") return "tvmaze.com";
        if (source == "wikidata") return "wikidata.org";
        if (source == "musicbrainz" || source == "acoustid") return "musicbrainz.org";
        if (link.find("twitch.tv/") != std::string::npos) return "twitch.tv";
        if (link.find("youtube.com/") != std::string::npos || link.find("youtu.be/") != std::string::npos) return "youtube.com";
        return "";
    }

    // Resolves one button site to its display name and public URL, or an empty URL when this file has no such link.
    static button_link site_link(const std::string& site, const playback& m, const std::string& title, const std::string& query)
    {
        const auto& p = m.provider;
        const auto source = p.value("provider", std::string{});
        const auto link = p.value("link", std::string{});
        const auto kind = p.value("kind", std::string{});
        if (site == "provider")
        {
            if (m.anime) return site_link("anilist", m, title, query);
            if (source.starts_with("tmdb-")) return {"TMDB", link};
            if (source == "tvmaze") return {"TVmaze", link};
            if (source == "wikidata") return {"Wikidata", link};
            if (source == "musicbrainz" || source == "acoustid") return {"MusicBrainz", link};
            if (kind == "stream") return site_link("stream", m, title, query);
            return {source.empty() ? "provider" : source, link};
        }
        if (site == "anilist") return {"AniList", m.anime && m.anilist_id > 0 ? std::format("https://anilist.co/anime/{}", m.anilist_id) : ""};
        if (site == "myanimelist") return {"MyAnimeList", m.anime && m.mal_id > 0 ? std::format("https://myanimelist.net/anime/{}", m.mal_id) : ""};
        if (site == "tmdb") return {"TMDB", source.starts_with("tmdb-") ? link : ""};
        if (site == "tvmaze") return {"TVmaze", source == "tvmaze" ? link : ""};
        if (site == "wikidata") return {"Wikidata", source == "wikidata" ? link : ""};
        if (site == "musicbrainz") return {"MusicBrainz", source == "musicbrainz" || source == "acoustid" ? link : ""};
        if (site == "imdb")
        {
            static const std::regex imdb(R"(tt\d{5,10})");
            const auto id = p.value("imdb", std::string{});
            return {"IMDb", std::regex_match(id, imdb) ? "https://www.imdb.com/title/" + id + "/" : ""};
        }
        if (site == "lastfm")
        {
            const bool recording = kind == "recording";
            const auto artist = recording ? p.value("artist", m.artist) : m.artist;
            const auto track = recording ? p.value("name", m.track_title) : (m.track_title.empty() ? m.title : m.track_title);
            if (m.video && !recording) return {"Last.fm", ""};
            return {"Last.fm", artist.empty() || track.empty() ? "" :
                "https://www.last.fm/music/" + url_encode(artist, true) + "/_/" + url_encode(track, true)};
        }
        if (site == "stream")
        {
            const auto page = kind == "stream" ? link : std::string{};
            if (page.find("twitch.tv/") != std::string::npos) return {"Twitch", page};
            return {"YouTube", page};
        }
        if (site == "search-youtube")
            return {"YouTube", query.empty() ? "" : "https://www.youtube.com/results?search_query=" + url_encode(query, true)};
        if (site == "search-google")
            return {"Google", query.empty() ? "" : "https://www.google.com/search?q=" + url_encode(query, true)};
        return {site, ""};
    }

    // Default slots when nothing is saved. Matched music links to Last.fm and falls back to its recognition provider.
    static json default_buttons(const playback& m, const json& options)
    {
        if (options.contains("buttons"))
        {
            json slots = json::array();
            for (const auto& button : options.at("buttons"))
                if (button.is_object())
                    slots.push_back({{"site","custom"},{"label",button.value("label",std::string{})},{"url",button.value("url",std::string{})}});
            return slots;
        }
        if (!options.value("button", true)) return json::array();
        if (m.provider.value("kind", std::string{}) == "recording")
            return json::array({{{"site","lastfm"},{"fallback","provider"}}});
        return json::array({{{"site","provider"}}});
    }

    // Fills {field} tokens in one pass, so braces inside a title or artist stay literal.
    // A part between | separators is dropped when one of its fields is unknown or empty.
    static std::string render_template(const std::string& pattern, const json& fields)
    {
        std::istringstream input(pattern);
        std::string source, result;
        while (std::getline(input,source,'|'))
        {
            static const std::regex placeholder(R"(\{([a-zA-Z_][a-zA-Z_0-9]*)\})");
            std::string part;
            bool missing = false;
            auto last = source.cbegin();
            for (std::sregex_iterator it(source.begin(),source.end(),placeholder), end; it != end; ++it)
            {
                const auto key = (*it)[1].str();
                const auto& text = fields.contains(key) ? fields.at(key).get_ref<const std::string&>() : std::string{};
                if (text.empty()) { missing = true; break; }
                part.append(last, (*it)[0].first);
                part += text;
                last = (*it)[0].second;
            }
            if (missing) continue;
            part.append(last, source.cend());
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

    json layout_options(const playback& m, const json& settings)
    {
        json options = settings;
        auto layout = m.anime ? "anime_layout" : m.provider.value("kind",m.video ? std::string("video") : std::string("audio")) + "_layout";
        if (settings.contains(layout) && settings.at(layout).is_object())
        {
            const auto& specific = settings.at(layout);
            if (specific.contains("preset")) options = json::object();
            options.merge_patch(specific);
        }
        return options;
    }

    int pause_timeout_seconds(const playback& m, const json& settings, int fallback)
    {
        const auto options = layout_options(m, settings);
        if (options.value("while_paused", std::string("keep")) == "hide") return 1;
        const auto minutes = options.value("pause_timeout", -1);
        return minutes >= 0 ? minutes * 60 : fallback;
    }

    json format_activity(const playback& m, const json& settings)
    {
        const json options = layout_options(m, settings);
        const auto preset = options.value("preset", std::string("original"));
        const bool generic = preset == "private" || (m.paused && options.value("while_paused", std::string("keep")) == "generic");
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
            }
            if (m.buffering)
                state += state.empty() ? "Buffering... (" + duration + ")" : " (Buffering... " + duration + ")";
            else if (m.paused)
                state += state.empty() ? duration : " (" + duration + ")";
        }

        json fields = json::object();
        if (!generic)
        {
            fields={{"title",m.anime ? title : m.title},{"artist",m.artist},{"group",m.group},
                         {"episode",m.episode},{"original",m.raw_episode},{"album",m.album},{"state",state},
                         {"details",details},{"channel",m.channel},{"year",m.provider.value("year",std::string{})}};
            if (options.contains("headline")) name = render_template(options.at("headline"),fields);
            if (options.contains("details_template")) details = render_template(options.at("details_template"),fields);
            if (options.contains("state_template")) state = render_template(options.at("state_template"),fields);
            if (options.value("spoilers",false) && m.provider.contains("episode_title"))
                state = m.provider.at("episode_title");
            for (const auto* key : {"large_url","details_url","state_url"})
                if (options.contains(key) && public_url(options.at(key))) card[key] = options.at(key);
            const auto kind = m.provider.value("kind", std::string{});
            std::string query = m.anime ? title : name;
            if (!m.anime && (kind == "recording" || !m.video))
            {
                const auto artist = kind == "recording" ? m.provider.value("artist", m.artist) : m.artist;
                if (!artist.empty() && !to_lower(query).starts_with(to_lower(artist))) query = artist + " " + query;
            }
            const bool reset = options.contains("button_slots") && options.at("button_slots") == false;
            const auto slots = options.contains("button_slots") && options.at("button_slots").is_array()
                ? options.at("button_slots") : default_buttons(m, reset ? json::object() : options);
            json buttons = json::array();
            for (const auto& slot : slots)
            {
                if (!slot.is_object() || buttons.size() >= 2) continue;
                auto site = slot.value("site", std::string("off"));
                button_link chosen;
                if (site == "custom")
                {
                    json encoded = fields;
                    for (auto& [key, value] : encoded.items()) value = url_encode(value.get<std::string>());
                    chosen = {"link", render_template(slot.value("url", std::string{}), encoded)};
                    if (!public_url(chosen.url)) chosen.url.clear();
                }
                else if (site != "off") chosen = site_link(site, m, title, query);
                if (chosen.url.empty() && slot.contains("fallback"))
                {
                    site = slot.value("fallback", std::string("off"));
                    if (site != "off" && site != "custom") chosen = site_link(site, m, title, query);
                }
                if (chosen.url.empty() || !public_url(chosen.url)) continue;
                if (std::any_of(buttons.begin(), buttons.end(), [&](const json& b) { return b.at("url") == chosen.url; })) continue;
                const auto fallback_label = std::string(site.starts_with("search-") ? "Search on " : "View on ") + chosen.site;
                json label_fields = fields;
                label_fields["site"] = chosen.site;
                auto label = slot.contains("label") ? render_template(slot.value("label", std::string{}), label_fields) : fallback_label;
                if (label.empty()) label = site == "custom" ? std::string{} : fallback_label;
                if (label.empty()) continue;
                buttons.push_back({{"label", truncate_utf8_bytes(label, 32)}, {"url", chosen.url}});
            }
            if (!buttons.empty()) card["buttons"] = buttons;

            // Large image choice. Without one, the cover and its default hover text stay as set above.
            if (options.contains("large_image") || options.contains("large_text_template"))
            {
                const auto cover = m.anime ? m.cover : m.provider.value("cover", std::string{});
                const auto large = options.value("large_image", std::string(options.value("art", true) ? "cover" : "off"));
                std::string image;
                if (large == "cover") image = cover;
                else if (large == "still") image = m.provider.value("still", cover);
                else if (large == "mpv") image = MPV_LOGO;
                else if (large == "custom")
                {
                    json encoded = fields;
                    for (auto& [key, value] : encoded.items()) value = url_encode(value.get<std::string>());
                    image = render_template(options.value("large_image_url", std::string{}), encoded);
                }
                if (image.empty() || !public_url(image))
                {
                    card.erase("large_image");
                    card.erase("large_text");
                }
                else
                {
                    card["large_image"] = image;
                    card["large_text"] = options.contains("large_text_template")
                        ? render_template(options.at("large_text_template"), fields) : (m.anime ? title : name);
                    if (card["large_text"] == "") card.erase("large_text");
                }
            }
        }
        if (options.contains("compact")) card["compact"] = options.at("compact");
        card["name"] = name.empty() ? "mpv" : truncate_utf8_bytes(name, 128);
        if (!details.empty() && options.value("details", true)) card["details"] = details;
        if (!state.empty() && options.value("state", true)) card["state"] = state;
        // Small badge: playback status icon, the matched site's icon, or nothing. The older badge switch maps to status or off.
        auto small = options.contains("small_image") ? options.value("small_image", std::string("status"))
                                                       : std::string(options.value("badge", !generic) ? "status" : "off");
        if (generic && small == "provider") small = "status";
        if (small != "off")
        {
            const std::string icons = "https://raw.githubusercontent.com/google/material-design-icons/3.0.1/av/2x_web/";
            card["small_image"] = icons + (m.paused || m.buffering ? "ic_pause_white_48dp.png" : "ic_play_arrow_white_48dp.png");
            if (small == "provider")
            {
                const auto domain = site_domain(m);
                if (!domain.empty()) card["small_image"] = "https://www.google.com/s2/favicons?domain=" + domain + "&sz=64";
            }
            if (options.value("tooltip", true))
            {
                std::string status;
                if (m.buffering) status = "Buffering...";
                else if (m.paused) status = "Paused (" + duration + ")";
                else if (m.speed != 1) status = std::format("Playing ({:.2f}x)", m.speed);
                else status = "Playing";
                card["small_text"] = status;
                if (!generic && options.contains("small_text_template"))
                {
                    auto badge_fields = fields;
                    badge_fields["status"] = status;
                    card["small_text"] = render_template(options.at("small_text_template"), badge_fields);
                }
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
        // Discord rejects text fields shorter than two characters.
        for (const auto* key : {"details","state","large_text","small_text"})
            if (card.contains(key) && card.at(key).get_ref<const std::string&>().size() < 2) card.erase(key);
        if (card.at("name").get_ref<const std::string&>().size() < 2) card["name"] = "mpv";
        return card;
    }
}
