// SPDX-License-Identifier: AGPL-3.0-or-later
#include "mpv_rich_presence/activity.hpp"
#include "mpv_rich_presence/publish_activity.hpp"
#include <fstream>
#include <iostream>
#include "mpv_rich_presence/string_utils.hpp"

using namespace mpvrp;
using nlohmann::json;

int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    auto expected = json::parse(std::ifstream(argv[2]));
    boost::dll::shared_library sdk{std::string(argv[1])};
    auto& take_calls = sdk.get<const char*()>("take_calls");
    rich_presence_state state;
    state.discord_api = std::make_shared<discord_api_importer>(sdk);
    state.discord = std::make_unique<discord_client>(state.discord_api);
    for (int kind = 0; kind < 8; ++kind)
    {
        for (int mode = 0; mode < 4; ++mode)
        {
            playback m;
            m.anime = kind < 4;
            m.video = kind < 6;
            m.anime_title = "Series";
            m.episode = kind < 2 ? "8" : "";
            m.total = kind == 0 ? 12 : 0;
            m.group = kind % 2 == 0 ? "Group" : "";
            m.cover = kind == 0 ? "https://example.org/cover.jpg" : "";
            m.anilist_id = kind == 0 ? 123 : 0;
            m.artist = kind == 5 || kind == 7 ? "Artist" : "";
            m.title = kind == 5 ? "Artist - Track" : kind == 7 ? "Track" : kind == 6 ? "" : "Film";
            m.buffering = mode == 3;
            m.paused = mode >= 2;
            m.position = 10;
            m.remaining = mode == 1 ? -1 : 50;
            m.speed = mode == 1 ? 1.5 : 1;
            m.playtime_left = m.remaining / m.speed;
            m.now = 1000000;
            m.pause_start = 812345;
            auto card = format_activity(m, json::object());
            publish_activity(state, card, nullptr);
            if (json::parse(take_calls()) != expected.at(kind * 4 + mode))
            {
                std::cerr << "SDK call mismatch in fixture " << kind << "/" << mode << '\n';
                return 1;
            }
            if (m.anime && !m.group.empty() && !m.episode.empty())
            {
                auto draft = format_activity(m, {{"group", false}});
                if (draft.at("state").get<std::string>().find("Group") != std::string::npos) return 1;
                draft["state"] = card["state"];
                if (draft != card) return 1;
            }
        }
    }
    playback music;
    music.video=false;
    music.title="Track";
    music.artist="Artist";
    auto repeated = format_activity(music, {{"headline"," {title} / {title} | {album} "}});
    if (repeated.at("name") != "Track / Track") return 1;
    auto layout = format_activity(music, {{"headline","Old override"},{"audio_layout",{{"preset","clean"}}}});
    if (layout.at("name") == "Old override") return 1;
    music.title="Artist - Track";
    music.track_title="Track";
    music.album="Album";
    const auto tagged_card = format_activity(music, json::object());
    if (tagged_card.at("name") != "Track" || tagged_card.at("details") != "Artist" ||
        tagged_card.at("state") != "Album" || tagged_card.contains("large_image") ||
        tagged_card.contains("buttons")) return 1;

    music.provider = {{"kind","recording"},{"name","Matched track"},{"artist","Matched artist"},{"album","Matched album"}};
    const auto music_card = format_activity(music, json::object());
    if (music_card.at("name") != "Matched track" || music_card.at("details") != "Matched artist" ||
        music_card.at("state") != "Matched album") return 1;
    playback movie;
    movie.video = true;
    movie.provider = {{"kind","movie"},{"provider","tmdb-movie"},{"name","Matched film"},{"year","2014"}};
    const auto movie_card = format_activity(movie, json::object());
    if (movie_card.at("name") != "Matched film" || movie_card.at("state") != "2014") return 1;

    // Templates fill each token once, keep braces from metadata, and respect Discord's two-character minimum.
    playback braces;
    braces.title = "{artist} Live";
    braces.artist = "Band";
    braces.track_title = "{artist} Live";
    auto literal = format_activity(braces, {{"headline","{title} by {artist}"},{"details_template","{unknown}|{artist}"},
        {"state_template","X"}});
    if (literal.at("name") != "{artist} Live by Band" || literal.at("details") != "Band" || literal.contains("state")) return 1;
    if (format_activity(braces, {{"headline","Y"}}).at("name") != "mpv") return 1;

    // Last.fm play count and loved state fill templates; an empty field drops its part.
    playback counted = braces;
    counted.title = counted.track_title = "Track";
    counted.plays = "12";
    if (format_activity(counted, {{"state_template","{plays} plays|{loved}"}}).at("state") != "12 plays") return 1;
    counted.loved = "Loved";
    if (format_activity(counted, {{"state_template","{plays} plays|{loved}"}}).at("state") != "12 plays · Loved") return 1;

    // Large image, badge and hover text choices.
    playback episode;
    episode.video = true;
    episode.title = "Loki";
    episode.provider = {{"kind","tv"},{"provider","tmdb-tv"},{"name","Loki"},{"numbering","S02E04"},
        {"cover","https://image.tmdb.org/poster.jpg"},{"still","https://image.tmdb.org/still.jpg"},{"link","https://www.themoviedb.org/tv/84958"}};
    auto plain = format_activity(episode, json::object());
    if (plain.at("large_image") != "https://image.tmdb.org/poster.jpg" || plain.at("large_text") != "Loki") return 1;
    auto still = format_activity(episode, {{"large_image","still"},{"large_text_template","{title} · {state}"},
        {"small_image","provider"},{"small_text_template","{status} · {year}|{status}"}});
    if (still.at("large_image") != "https://image.tmdb.org/still.jpg" || still.at("large_text") != "Loki · S02E04" ||
        still.at("small_image") != "https://www.google.com/s2/favicons?domain=themoviedb.org&sz=64" ||
        still.at("small_text") != "Playing") return 1;
    if (format_activity(episode, {{"large_image","mpv"}}).at("large_image").get<std::string>().find("mpv-icon") == std::string::npos) return 1;
    auto none = format_activity(episode, {{"large_image","off"},{"small_image","off"}});
    if (none.contains("large_image") || none.contains("large_text") || none.contains("small_image")) return 1;
    auto own = format_activity(episode, {{"large_image","custom"},{"large_image_url","https://img.example.org/{title}.png"}});
    if (own.at("large_image") != "https://img.example.org/Loki.png") return 1;
    if (format_activity(episode, {{"large_image","custom"},{"large_image_url","http://insecure.example.org/a.png"}}).contains("large_image")) return 1;
    if (format_activity(episode, {{"art",false}}).contains("large_image")) return 1;

    // Button slots: defaults, legacy settings, sites, fallbacks, custom URLs and limits.
    auto button = [](const json& card, size_t index, const char* field) {
        return card.contains("buttons") && card.at("buttons").size() > index
            ? card.at("buttons").at(index).value(field, std::string{}) : std::string{};
    };
    playback song;
    song.title = "Bring Me to Life";
    song.artist = "Evanescence";
    song.provider = {{"kind","recording"},{"provider","musicbrainz"},{"name","Bring Me to Life"},
        {"artist","Evanescence"},{"link","https://musicbrainz.org/recording/abc"}};
    auto song_card = format_activity(song, json::object());
    if (button(song_card,0,"label") != "View on Last.fm" ||
        button(song_card,0,"url") != "https://www.last.fm/music/Evanescence/_/Bring+Me+to+Life" ||
        song_card.at("buttons").size() != 1) return 1;
    song.provider["artist"] = "";
    song.artist = "";
    if (button(format_activity(song, json::object()),0,"label") != "View on MusicBrainz") return 1;
    song.provider["provider"] = "acoustid";
    if (button(format_activity(song, json::object()),0,"label") != "View on MusicBrainz") return 1;
    if (format_activity(song, {{"button",false}}).contains("buttons")) return 1;
    auto legacy = format_activity(song, {{"buttons",json::array({{{"label","Site"},{"url","https://example.org/a"}}})}});
    if (button(legacy,0,"label") != "Site" || legacy.at("buttons").size() != 1) return 1;
    if (format_activity(song, {{"buttons",json::array()}}).contains("buttons")) return 1;

    playback show;
    show.video = show.anime = true;
    show.anime_title = "A&B Show";
    show.anilist_id = 21;
    show.mal_id = 42;
    auto two = format_activity(show, {{"button_slots",json::array({{{"site","myanimelist"}},
        {{"site","anilist"},{"label","AniList · {title}"}}})}});
    if (button(two,0,"url") != "https://myanimelist.net/anime/42" || button(two,0,"label") != "View on MyAnimeList" ||
        button(two,1,"label") != "AniList · A&B Show") return 1;
    auto custom = format_activity(show, {{"button_slots",json::array({{{"site","custom"},{"label","Find {title}"},
        {"url","https://example.com/s?q={title}"}}})}});
    if (button(custom,0,"url") != "https://example.com/s?q=A%26B%20Show" || button(custom,0,"label") != "Find A&B Show") return 1;
    auto search = format_activity(show, {{"button_slots",json::array({{{"site","search-youtube"}}})}});
    if (button(search,0,"url") != "https://www.youtube.com/results?search_query=A%26B+Show" ||
        button(search,0,"label") != "Search on YouTube") return 1;
    auto duplicate = format_activity(show, {{"button_slots",json::array({{{"site","anilist"}},{{"site","provider"}}})}});
    if (duplicate.at("buttons").size() != 1) return 1;
    if (format_activity(show, {{"button_slots",json::array({{{"site","off"}}})}}).contains("buttons")) return 1;
    if (format_activity(show, {{"button_slots",json::array({{{"site","custom"},{"label","Bad"},{"url","http://example.com"}}})}}).contains("buttons")) return 1;

    playback film;
    film.video = true;
    film.provider = {{"kind","movie"},{"provider","tmdb-movie"},{"name","Interstellar"},{"link","https://www.themoviedb.org/movie/157336"}};
    auto fallback = format_activity(film, {{"button_slots",json::array({{{"site","imdb"},{"fallback","tmdb"}}})}});
    if (button(fallback,0,"label") != "View on TMDB") return 1;
    film.provider["imdb"] = "tt0816692";
    auto imdb = format_activity(film, {{"button_slots",json::array({{{"site","imdb"},{"fallback","tmdb"}}})}});
    if (button(imdb,0,"url") != "https://www.imdb.com/title/tt0816692/") return 1;
    auto layered = format_activity(film, {{"movie_layout",{{"button_slots",json::array({{{"site","wikidata"},{"fallback","off"}}})}}}});
    if (layered.contains("buttons")) return 1;

    // Pause behavior per media layout: timeout minutes, generic card or hide while paused.
    playback paused_song = song;
    paused_song.paused = true;
    if (pause_timeout_seconds(paused_song, json::object(), 1800) != 1800) return 1;
    if (pause_timeout_seconds(paused_song, {{"recording_layout",{{"pause_timeout",5}}}}, 1800) != 300) return 1;
    if (pause_timeout_seconds(paused_song, {{"pause_timeout",0}}, 1800) != 0) return 1;
    if (pause_timeout_seconds(paused_song, {{"recording_layout",{{"while_paused","hide"}}}}, 1800) != 1) return 1;
    if (pause_timeout_seconds(show, {{"recording_layout",{{"pause_timeout",5}}}}, 1800) != 1800) return 1;
    auto quiet = format_activity(paused_song, {{"while_paused","generic"}});
    if (quiet.at("name") != "mpv" || quiet.contains("buttons") || quiet.contains("details")) return 1;
    paused_song.paused = false;
    if (format_activity(paused_song, {{"while_paused","generic"}}).at("name") == "mpv") return 1;

    const auto directory = std::filesystem::temp_directory_path() /
        ("mpvrp-store-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto database = directory / std::filesystem::u8path("日本語.sqlite3");
    {
        profile_store store(database);
        store.put("appearance","global",{{"audio_layout",{{"headline","Old"},{"preset","original"}}}});
        store.save("appearance","global",{{"audio_layout",{{"preset","clean"}}}});
        if (store.get("appearance","global").at("audio_layout").contains("headline")) return 1;
        store.put("appearance","folder:/media",{{"audio_layout",{{"preset","detailed"}}}});
        store.put("appearance","file:/media/track.flac",{{"group",false}});
        auto effective = store.effective("appearance","/media/track.flac");
        if (effective.at("audio_layout").at("preset") != "detailed" || effective.at("group") != false) return 1;
        store.put("exclude","folder:/",{{"blocked",true}});
        if (!store.blocked("/media/track.flac")) return 1;
        store.clear("exclude","folder:/");
        store.put("exclude","folder:/media",{{"blocked",true}});
        if (store.blocked("/media-other/track.flac")) return 1;
        if (path_utf8(database).find("日本語") == std::string::npos) return 1;
        store.put("account","current",{{"persistent",false},{"id",7}});
        store.put("watch","7/18/2/0",{{"elapsed",73.0}});
        store.put("watch","8/18/2/0",{{"elapsed",12.0}});
        store.put("cache","v3:auto:wikidata/hash",{{"expires",1000}});
        const auto counts = store.record_counts();
        if (counts.value("watch",0) != 1 || counts.value("cache",0) != 1) return 1;
        const auto caches = store.records("cache");
        if (caches.size() != 1 || caches.at(0).value("label",std::string{}) != "v3:auto:wikidata") return 1;
        if (store.records("account").size() != 0 || store.records("watch").size() != 1) return 1;
        if (store.episode_watch(7,18,"2").value("elapsed",0.0) != 73.0) return 1;
        if (store.episode_watch(7,18,"3") != nlohmann::json::object()) return 1;
        store.put("cycle","7/18",{{"number",1}});
        if (!store.episode_watch(7,18,"2").empty()) return 1;
        store.put("watch","7/18/2/1",{{"elapsed",10.0},{"first_watched",123000.0},{"last_watched",133000.0}});
        if (store.episode_watch(7,18,"2").value("last_watched",0.0) != 133000.0) return 1;
        if (store.episode_watch(8,18,"2").value("elapsed",0.0) != 12.0) return 1;
        store.put("player","old-window",{{"session",2}});
        store.put("privacy","global",{{"mode","hidden"},{"block_lookup",true}});
        store.put("privacy","file:/media/track.flac",{{"mode","hidden"},{"block_lookup",true}});
    }
    const auto relocated = directory / "scripts" / "mpv-rich-presence" / "data.sqlite3";
    {
        profile_store store(relocated, database);
        if (!store.blocked("/media/track.flac")) return 1;
        if (!store.get("account","current").empty() || !store.get("player","old-window").empty()) return 1;
        if (!store.get("privacy","global").empty()) return 1;
        if (!store.get("privacy","file:/media/track.flac").value("block_lookup",false)) return 1;
        store.clear("exclude","folder:/media");
        store.put("privacy","global",{{"mode","hidden"}});
    }
    {
        profile_store store(relocated, database);
        if (store.blocked("/media/track.flac")) return 1;
        if (store.get("privacy","global").value("mode",std::string{}) != "hidden") return 1;
    }
    std::filesystem::remove(relocated);
    std::filesystem::remove(relocated.parent_path());
    std::filesystem::remove(relocated.parent_path().parent_path());
    std::filesystem::remove(database);
    std::filesystem::remove(directory);
}
