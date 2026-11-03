// SPDX-License-Identifier: AGPL-3.0-or-later
#include "mpv_rich_presence/activity.hpp"
#include "mpv_rich_presence/publish_activity.hpp"
#include <fstream>
#include <iostream>

using namespace mpvrp;
using nlohmann::json;

int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    auto expected = json::parse(std::ifstream(argv[2]));
    boost::dll::shared_library sdk(argv[1]);
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
}

