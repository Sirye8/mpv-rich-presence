// SPDX-License-Identifier: AGPL-3.0-or-later
#include "mpv_rich_presence/publish_activity.hpp"

namespace mpvrp
{
    void publish_activity(rich_presence_state& state, const nlohmann::json& card,
        Discord_Client_UpdateRichPresenceCallback callback, void* context, Discord_FreeFn free_context)
    {
        const auto& api = *state.discord_api;
        discord_activity activity(state.discord_api);
        discord_activity_assets assets(state.discord_api);
        auto str = [&](const char* key) { return sdk_string(card.at(key).get_ref<const std::string&>()); };
        api.Discord_Activity_SetType(&activity.get(), card.at("type") == "Watching" ? Discord_ActivityTypes_Watching : Discord_ActivityTypes_Listening);
        api.Discord_Activity_SetName(&activity.get(), str("name"));

        auto set_text = [&](const char* key, auto& setter, auto& object) {
            if (card.contains(key))
            {
                auto value = str(key);
                setter(&object.get(), &value);
            }
        };
        set_text("details", api.Discord_Activity_SetDetails, activity);
        set_text("state", api.Discord_Activity_SetState, activity);
        if (api.Discord_Activity_SetStatusDisplayType && card.contains("compact"))
        {
            auto value = card.at("compact") == "state" ? Discord_StatusDisplayTypes_State :
                card.at("compact") == "details" ? Discord_StatusDisplayTypes_Details : Discord_StatusDisplayTypes_Name;
            api.Discord_Activity_SetStatusDisplayType(&activity.get(), &value);
        }
        if (api.Discord_Activity_SetDetailsUrl) set_text("details_url", *api.Discord_Activity_SetDetailsUrl, activity);
        if (api.Discord_Activity_SetStateUrl) set_text("state_url", *api.Discord_Activity_SetStateUrl, activity);
        if (api.Discord_ActivityAssets_SetLargeUrl) set_text("large_url", *api.Discord_ActivityAssets_SetLargeUrl, assets);
        set_text("large_image", api.Discord_ActivityAssets_SetLargeImage, assets);
        set_text("large_text", api.Discord_ActivityAssets_SetLargeText, assets);
        if (card.contains("buttons"))
        {
            for (const auto& item : card.at("buttons"))
            {
                discord_activity_button button(state.discord_api);
                api.Discord_ActivityButton_SetLabel(&button.get(), sdk_string(item.at("label").get_ref<const std::string&>()));
                api.Discord_ActivityButton_SetUrl(&button.get(), sdk_string(item.at("url").get_ref<const std::string&>()));
                api.Discord_Activity_AddButton(&activity.get(), &button.get());
            }
        }
        set_text("small_image", api.Discord_ActivityAssets_SetSmallImage, assets);
        set_text("small_text", api.Discord_ActivityAssets_SetSmallText, assets);
        api.Discord_Activity_SetAssets(&activity.get(), &assets.get());
        if (card.contains("start") || card.contains("end"))
        {
            discord_activity_timestamps timestamps(state.discord_api);
            if (card.contains("start")) api.Discord_ActivityTimestamps_SetStart(&timestamps.get(), card.at("start"));
            if (card.contains("end")) api.Discord_ActivityTimestamps_SetEnd(&timestamps.get(), card.at("end"));
            api.Discord_Activity_SetTimestamps(&activity.get(), &timestamps.get());
        }
        api.Discord_Client_UpdateRichPresence(&state.discord->get(), &activity.get(), callback, free_context,
            context ? context : &state);
    }
}
