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
        if (store.episode_watch(7,18,"2").value("elapsed",0.0) != 73.0) return 1;
        if (store.episode_watch(7,18,"3") != nlohmann::json::object()) return 1;
        store.put("cycle","7/18",{{"number",1}});
        if (!store.episode_watch(7,18,"2").empty()) return 1;
        store.put("watch","7/18/2/1",{{"elapsed",10.0},{"first_watched",123000.0},{"last_watched",133000.0}});
        if (store.episode_watch(7,18,"2").value("last_watched",0.0) != 133000.0) return 1;
        if (store.episode_watch(8,18,"2").value("elapsed",0.0) != 12.0) return 1;
        store.put("player","old-window",{{"session",2}});
    }
    const auto relocated = directory / "scripts" / "mpv-rich-presence" / "data.sqlite3";
    {
        profile_store store(relocated, database);
        if (!store.blocked("/media/track.flac")) return 1;
        if (!store.get("account","current").empty() || !store.get("player","old-window").empty()) return 1;
        store.clear("exclude","folder:/media");
    }
    {
        profile_store store(relocated, database);
        if (store.blocked("/media/track.flac")) return 1;
    }
    std::filesystem::remove(relocated);
    std::filesystem::remove(relocated.parent_path());
    std::filesystem::remove(relocated.parent_path().parent_path());
    std::filesystem::remove(database);
    std::filesystem::remove(directory);
}
