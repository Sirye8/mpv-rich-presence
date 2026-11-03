// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "mpv_rich_presence/rich_presence_state.hpp"
#include <nlohmann/json.hpp>

namespace mpvrp
{
    void publish_activity(rich_presence_state& state, const nlohmann::json& card, Discord_Client_UpdateRichPresenceCallback callback);
}
