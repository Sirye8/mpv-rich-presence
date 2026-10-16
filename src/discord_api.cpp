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

#include "mpv_rich_presence/discord_api.hpp"

namespace dll = boost::dll;
using namespace mpvrp;

discord_api_importer::discord_api_importer(const dll::shared_library& sdk)
    : Discord_Activity_Drop_impl { dll::import_symbol<void(Discord_Activity* self)>(sdk, "Discord_Activity_Drop") }
    , Discord_Activity_Init_impl { dll::import_symbol<void(Discord_Activity* self)>(sdk, "Discord_Activity_Init") }
    , Discord_Activity_SetName_impl { dll::import_symbol<void(Discord_Activity* self, Discord_String value)>(sdk, "Discord_Activity_SetName") }
    , Discord_Activity_SetState_impl { dll::import_symbol<void(Discord_Activity* self, Discord_String* value)>(sdk, "Discord_Activity_SetState") }
    , Discord_Activity_SetTimestamps_impl { dll::import_symbol<void(Discord_Activity* self, Discord_ActivityTimestamps* value)>(sdk, "Discord_Activity_SetTimestamps") }
    , Discord_Activity_SetType_impl { dll::import_symbol<void(Discord_Activity* self, Discord_ActivityTypes value)>(sdk, "Discord_Activity_SetType") }
    , Discord_ActivityTimestamps_Drop_impl { dll::import_symbol<void(Discord_ActivityTimestamps* self)>(sdk, "Discord_ActivityTimestamps_Drop") }
    , Discord_ActivityTimestamps_Init_impl { dll::import_symbol<void(Discord_ActivityTimestamps* self)>(sdk, "Discord_ActivityTimestamps_Init") }
    , Discord_ActivityTimestamps_SetEnd_impl { dll::import_symbol<void(Discord_ActivityTimestamps* self, uint64_t value)>(sdk, "Discord_ActivityTimestamps_SetEnd") }
    , Discord_ActivityTimestamps_SetStart_impl { dll::import_symbol<void(Discord_ActivityTimestamps* self, uint64_t value)>(sdk, "Discord_ActivityTimestamps_SetStart") }
    , Discord_Client_AddLogCallback_impl { dll::import_symbol<void(Discord_Client* self, Discord_Client_LogCallback callback, Discord_FreeFn callback__userDataFree, void* callback__userData, Discord_LoggingSeverity minSeverity)>(sdk, "Discord_Client_AddLogCallback") }
    , Discord_Client_ClearRichPresence_impl { dll::import_symbol<void(Discord_Client* self)>(sdk, "Discord_Client_ClearRichPresence") }
    , Discord_Client_GetApplicationId_impl { dll::import_symbol<uint64_t(Discord_Client* self)>(sdk, "Discord_Client_GetApplicationId") }
    , Discord_Client_Drop_impl { dll::import_symbol<void(Discord_Client* self)>(sdk, "Discord_Client_Drop") }
    , Discord_Client_Init_impl { dll::import_symbol<void(Discord_Client* self)>(sdk, "Discord_Client_Init") }
    , Discord_Client_SetApplicationId_impl { dll::import_symbol<void(Discord_Client* self, uint64_t applicationId)>(sdk, "Discord_Client_SetApplicationId") }
    , Discord_Client_UpdateRichPresence_impl { dll::import_symbol<void(Discord_Client* self, Discord_Activity* activity, Discord_Client_UpdateRichPresenceCallback cb, Discord_FreeFn cb__userDataFree, void* cb__userData)>(sdk, "Discord_Client_UpdateRichPresence") }
    , Discord_ClientResult_ErrorCode_impl { dll::import_symbol<int32_t(Discord_ClientResult* self)>(sdk, "Discord_ClientResult_ErrorCode") }
    , Discord_ClientResult_Successful_impl { dll::import_symbol<bool(Discord_ClientResult* self)>(sdk, "Discord_ClientResult_Successful") }
    , Discord_RunCallbacks_impl { dll::import_symbol<void()>(sdk, "Discord_RunCallbacks") }
    , Discord_ActivityAssets_Init_impl { dll::import_symbol<void(Discord_ActivityAssets*)>(sdk, "Discord_ActivityAssets_Init") }
    , Discord_ActivityAssets_SetLargeImage_impl { dll::import_symbol<void(Discord_ActivityAssets*, Discord_String)>(sdk, "Discord_ActivityAssets_SetLargeImage") }
    , Discord_ActivityAssets_SetSmallImage_impl { dll::import_symbol<void(Discord_ActivityAssets*, Discord_String)>(sdk, "Discord_ActivityAssets_SetSmallImage") }
    , Discord_Activity_SetAssets_impl { dll::import_symbol<void(Discord_Activity*, Discord_ActivityAssets*)>(sdk, "Discord_Activity_SetAssets") }
    , Discord_Activity_SetDetails_impl { dll::import_symbol<void(Discord_Activity*, Discord_String*)>(sdk, "Discord_Activity_SetDetails") }
    , Discord_ActivityButton_Init_impl { dll::import_symbol<void(Discord_ActivityButton*)>(sdk, "Discord_ActivityButton_Init") }
    , Discord_ActivityButton_Drop_impl { dll::import_symbol<void(Discord_ActivityButton*)>(sdk, "Discord_ActivityButton_Drop") }
    , Discord_ActivityButton_SetLabel_impl { dll::import_symbol<void(Discord_ActivityButton*, Discord_String)>(sdk, "Discord_ActivityButton_SetLabel") }
    , Discord_ActivityButton_SetUrl_impl { dll::import_symbol<void(Discord_ActivityButton*, Discord_String)>(sdk, "Discord_ActivityButton_SetUrl") }
    , Discord_Activity_AddButton_impl { dll::import_symbol<void(Discord_Activity*, Discord_ActivityButton const*)>(sdk, "Discord_Activity_AddButton") }
    , Discord_ActivityAssets_SetLargeText_impl { dll::import_symbol<void(Discord_ActivityAssets*, Discord_String)>(sdk, "Discord_ActivityAssets_SetLargeText") }
    , Discord_ActivityAssets_SetSmallText_impl { dll::import_symbol<void(Discord_ActivityAssets*, Discord_String)>(sdk, "Discord_ActivityAssets_SetSmallText") }
{
}

void discord_api_importer::Discord_Activity_Drop(Discord_Activity* self) const
{
    Discord_Activity_Drop_impl(self);
}

void discord_api_importer::Discord_Activity_Init(Discord_Activity* self) const
{
    Discord_Activity_Init_impl(self);
}

void discord_api_importer::Discord_Activity_SetName(Discord_Activity* self, Discord_String value) const
{
    Discord_Activity_SetName_impl(self, value);
}

void discord_api_importer::Discord_Activity_SetState(Discord_Activity* self, Discord_String* value) const
{
    Discord_Activity_SetState_impl(self, value);
}

void discord_api_importer::Discord_Activity_SetTimestamps(Discord_Activity* self, Discord_ActivityTimestamps* value) const
{
    Discord_Activity_SetTimestamps_impl(self, value);
}

void discord_api_importer::Discord_Activity_SetType(Discord_Activity* self, Discord_ActivityTypes value) const
{
    Discord_Activity_SetType_impl(self, value);
}

void discord_api_importer::Discord_ActivityTimestamps_Drop(Discord_ActivityTimestamps* self) const
{
    Discord_ActivityTimestamps_Drop_impl(self);
}

void discord_api_importer::Discord_ActivityTimestamps_Init(Discord_ActivityTimestamps* self) const
{
    Discord_ActivityTimestamps_Init_impl(self);
}

void discord_api_importer::Discord_ActivityTimestamps_SetEnd(Discord_ActivityTimestamps* self, uint64_t value) const
{
    Discord_ActivityTimestamps_SetEnd_impl(self, value);
}

void discord_api_importer::Discord_ActivityTimestamps_SetStart(Discord_ActivityTimestamps* self, uint64_t value) const
{
    Discord_ActivityTimestamps_SetStart_impl(self, value);
}

void discord_api_importer::Discord_Client_AddLogCallback(Discord_Client* self, Discord_Client_LogCallback callback, Discord_FreeFn callback__userDataFree, void* callback__userData, Discord_LoggingSeverity minSeverity) const
{
    Discord_Client_AddLogCallback_impl(self, callback, callback__userDataFree, callback__userData, minSeverity);
}

void discord_api_importer::Discord_Client_ClearRichPresence(Discord_Client* self) const
{
    Discord_Client_ClearRichPresence_impl(self);
}

auto discord_api_importer::Discord_Client_GetApplicationId(Discord_Client* self) const -> uint64_t
{
    return Discord_Client_GetApplicationId_impl(self);
}

void discord_api_importer::Discord_Client_Drop(Discord_Client* self) const
{
    Discord_Client_Drop_impl(self);
}

void discord_api_importer::Discord_Client_Init(Discord_Client* self) const
{
    Discord_Client_Init_impl(self);
}

void discord_api_importer::Discord_Client_SetApplicationId(Discord_Client* self, uint64_t applicationId) const
{
    Discord_Client_SetApplicationId_impl(self, applicationId);
}

void discord_api_importer::Discord_Client_UpdateRichPresence(Discord_Client* self, Discord_Activity* activity, Discord_Client_UpdateRichPresenceCallback cb, Discord_FreeFn cb__userDataFree, void* cb__userData) const
{
    Discord_Client_UpdateRichPresence_impl(self, activity, cb, cb__userDataFree, cb__userData);
}

auto discord_api_importer::Discord_ClientResult_ErrorCode(Discord_ClientResult* self) const -> int32_t
{
    return Discord_ClientResult_ErrorCode_impl(self);
}

auto discord_api_importer::Discord_ClientResult_Successful(Discord_ClientResult* self) const -> bool
{
    return Discord_ClientResult_Successful_impl(self);
}

void discord_api_importer::Discord_RunCallbacks() const
{
    Discord_RunCallbacks_impl();
}

void discord_api_importer::Discord_ActivityAssets_Init(Discord_ActivityAssets* self) const
{
    Discord_ActivityAssets_Init_impl(self);
}

void discord_api_importer::Discord_ActivityAssets_SetLargeImage(Discord_ActivityAssets* self, Discord_String value) const
{
    Discord_ActivityAssets_SetLargeImage_impl(self, value);
}

void discord_api_importer::Discord_Activity_SetAssets(Discord_Activity* self, Discord_ActivityAssets* value) const
{
    Discord_Activity_SetAssets_impl(self, value);
}

void discord_api_importer::Discord_ActivityAssets_SetSmallImage(Discord_ActivityAssets* self, Discord_String value) const
{
    Discord_ActivityAssets_SetSmallImage_impl(self, value);
}

void discord_api_importer::Discord_Activity_SetDetails(Discord_Activity* self, Discord_String* value) const
{
    Discord_Activity_SetDetails_impl(self, value);
}

void discord_api_importer::Discord_ActivityButton_Init(Discord_ActivityButton* self) const
{
    Discord_ActivityButton_Init_impl(self);
}

void discord_api_importer::Discord_ActivityButton_Drop(Discord_ActivityButton* self) const
{
    Discord_ActivityButton_Drop_impl(self);
}

void discord_api_importer::Discord_ActivityButton_SetLabel(Discord_ActivityButton* self, Discord_String value) const
{
    Discord_ActivityButton_SetLabel_impl(self, value);
}

void discord_api_importer::Discord_ActivityButton_SetUrl(Discord_ActivityButton* self, Discord_String value) const
{
    Discord_ActivityButton_SetUrl_impl(self, value);
}

void discord_api_importer::Discord_Activity_AddButton(Discord_Activity* self, Discord_ActivityButton const* button) const
{
    Discord_Activity_AddButton_impl(self, button);
}

void discord_api_importer::Discord_ActivityAssets_SetLargeText(Discord_ActivityAssets* self, Discord_String value) const
{
    Discord_ActivityAssets_SetLargeText_impl(self, value);
}

void discord_api_importer::Discord_ActivityAssets_SetSmallText(Discord_ActivityAssets* self, Discord_String value) const
{
    Discord_ActivityAssets_SetSmallText_impl(self, value);
}
