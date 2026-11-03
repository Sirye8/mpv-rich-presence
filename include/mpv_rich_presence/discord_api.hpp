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

#pragma once

#include <boost/dll/shared_library.hpp>
#define DISCORD_API
#include <cdiscord.h>
#undef DISCORD_API

#include <string_view>

namespace mpvrp
{
    inline Discord_String sdk_string(std::string_view value)
    {
        return { reinterpret_cast<uint8_t*>(const_cast<char*>(value.data())), value.size() };
    }

    class discord_api_importer
    {
        boost::dll::shared_library sdk;

    public:
        explicit discord_api_importer(boost::dll::shared_library library)
            : sdk(std::move(library)) {}

        decltype(::Discord_ActivityAssets_Drop)& Discord_ActivityAssets_Drop = sdk.get<decltype(::Discord_ActivityAssets_Drop)>("Discord_ActivityAssets_Drop");
        decltype(::Discord_ActivityAssets_Init)& Discord_ActivityAssets_Init = sdk.get<decltype(::Discord_ActivityAssets_Init)>("Discord_ActivityAssets_Init");
        decltype(::Discord_ActivityAssets_SetLargeImage)& Discord_ActivityAssets_SetLargeImage = sdk.get<decltype(::Discord_ActivityAssets_SetLargeImage)>("Discord_ActivityAssets_SetLargeImage");
        decltype(::Discord_ActivityAssets_SetLargeText)& Discord_ActivityAssets_SetLargeText = sdk.get<decltype(::Discord_ActivityAssets_SetLargeText)>("Discord_ActivityAssets_SetLargeText");
        decltype(::Discord_ActivityAssets_SetSmallImage)& Discord_ActivityAssets_SetSmallImage = sdk.get<decltype(::Discord_ActivityAssets_SetSmallImage)>("Discord_ActivityAssets_SetSmallImage");
        decltype(::Discord_ActivityAssets_SetSmallText)& Discord_ActivityAssets_SetSmallText = sdk.get<decltype(::Discord_ActivityAssets_SetSmallText)>("Discord_ActivityAssets_SetSmallText");
        decltype(::Discord_ActivityButton_Drop)& Discord_ActivityButton_Drop = sdk.get<decltype(::Discord_ActivityButton_Drop)>("Discord_ActivityButton_Drop");
        decltype(::Discord_ActivityButton_Init)& Discord_ActivityButton_Init = sdk.get<decltype(::Discord_ActivityButton_Init)>("Discord_ActivityButton_Init");
        decltype(::Discord_ActivityButton_SetLabel)& Discord_ActivityButton_SetLabel = sdk.get<decltype(::Discord_ActivityButton_SetLabel)>("Discord_ActivityButton_SetLabel");
        decltype(::Discord_ActivityButton_SetUrl)& Discord_ActivityButton_SetUrl = sdk.get<decltype(::Discord_ActivityButton_SetUrl)>("Discord_ActivityButton_SetUrl");
        decltype(::Discord_ActivityTimestamps_Drop)& Discord_ActivityTimestamps_Drop = sdk.get<decltype(::Discord_ActivityTimestamps_Drop)>("Discord_ActivityTimestamps_Drop");
        decltype(::Discord_ActivityTimestamps_Init)& Discord_ActivityTimestamps_Init = sdk.get<decltype(::Discord_ActivityTimestamps_Init)>("Discord_ActivityTimestamps_Init");
        decltype(::Discord_ActivityTimestamps_SetEnd)& Discord_ActivityTimestamps_SetEnd = sdk.get<decltype(::Discord_ActivityTimestamps_SetEnd)>("Discord_ActivityTimestamps_SetEnd");
        decltype(::Discord_ActivityTimestamps_SetStart)& Discord_ActivityTimestamps_SetStart = sdk.get<decltype(::Discord_ActivityTimestamps_SetStart)>("Discord_ActivityTimestamps_SetStart");
        decltype(::Discord_Activity_AddButton)& Discord_Activity_AddButton = sdk.get<decltype(::Discord_Activity_AddButton)>("Discord_Activity_AddButton");
        decltype(::Discord_Activity_Drop)& Discord_Activity_Drop = sdk.get<decltype(::Discord_Activity_Drop)>("Discord_Activity_Drop");
        decltype(::Discord_Activity_Init)& Discord_Activity_Init = sdk.get<decltype(::Discord_Activity_Init)>("Discord_Activity_Init");
        decltype(::Discord_Activity_SetAssets)& Discord_Activity_SetAssets = sdk.get<decltype(::Discord_Activity_SetAssets)>("Discord_Activity_SetAssets");
        decltype(::Discord_Activity_SetDetails)& Discord_Activity_SetDetails = sdk.get<decltype(::Discord_Activity_SetDetails)>("Discord_Activity_SetDetails");
        decltype(::Discord_Activity_SetName)& Discord_Activity_SetName = sdk.get<decltype(::Discord_Activity_SetName)>("Discord_Activity_SetName");
        decltype(::Discord_Activity_SetState)& Discord_Activity_SetState = sdk.get<decltype(::Discord_Activity_SetState)>("Discord_Activity_SetState");
        decltype(::Discord_Activity_SetTimestamps)& Discord_Activity_SetTimestamps = sdk.get<decltype(::Discord_Activity_SetTimestamps)>("Discord_Activity_SetTimestamps");
        decltype(::Discord_Activity_SetType)& Discord_Activity_SetType = sdk.get<decltype(::Discord_Activity_SetType)>("Discord_Activity_SetType");
        decltype(::Discord_ClientResult_Drop)& Discord_ClientResult_Drop = sdk.get<decltype(::Discord_ClientResult_Drop)>("Discord_ClientResult_Drop");
        decltype(::Discord_ClientResult_ErrorCode)& Discord_ClientResult_ErrorCode = sdk.get<decltype(::Discord_ClientResult_ErrorCode)>("Discord_ClientResult_ErrorCode");
        decltype(::Discord_ClientResult_Successful)& Discord_ClientResult_Successful = sdk.get<decltype(::Discord_ClientResult_Successful)>("Discord_ClientResult_Successful");
        decltype(::Discord_Client_AddLogCallback)& Discord_Client_AddLogCallback = sdk.get<decltype(::Discord_Client_AddLogCallback)>("Discord_Client_AddLogCallback");
        decltype(::Discord_Client_ClearRichPresence)& Discord_Client_ClearRichPresence = sdk.get<decltype(::Discord_Client_ClearRichPresence)>("Discord_Client_ClearRichPresence");
        decltype(::Discord_Client_Drop)& Discord_Client_Drop = sdk.get<decltype(::Discord_Client_Drop)>("Discord_Client_Drop");
        decltype(::Discord_Client_GetApplicationId)& Discord_Client_GetApplicationId = sdk.get<decltype(::Discord_Client_GetApplicationId)>("Discord_Client_GetApplicationId");
        decltype(::Discord_Client_Init)& Discord_Client_Init = sdk.get<decltype(::Discord_Client_Init)>("Discord_Client_Init");
        decltype(::Discord_Client_SetApplicationId)& Discord_Client_SetApplicationId = sdk.get<decltype(::Discord_Client_SetApplicationId)>("Discord_Client_SetApplicationId");
        decltype(::Discord_Client_UpdateRichPresence)& Discord_Client_UpdateRichPresence = sdk.get<decltype(::Discord_Client_UpdateRichPresence)>("Discord_Client_UpdateRichPresence");
        decltype(::Discord_Free)& Discord_Free = sdk.get<decltype(::Discord_Free)>("Discord_Free");
        decltype(::Discord_RunCallbacks)& Discord_RunCallbacks = sdk.get<decltype(::Discord_RunCallbacks)>("Discord_RunCallbacks");
    };
}
