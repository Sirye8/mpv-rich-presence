// SPDX-License-Identifier: AGPL-3.0-or-later
// Test implementation of the SDK entry points used by the publisher.
#define DISCORD_API
#include <cdiscord.h>
#include <nlohmann/json.hpp>
#include <deque>
#include <string>

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif

static nlohmann::json calls = nlohmann::json::array();

// Like the real SDK, publish results arrive later from Discord_RunCallbacks, not during the call.
struct queued_update
{
    Discord_Client_UpdateRichPresenceCallback callback;
    Discord_FreeFn free;
    void* data;
};
static std::deque<queued_update> queued;
static bool next_success = true;
static int32_t next_code = 0;
static bool delivering_success = true;
static int32_t delivering_code = 0;

EXPORT void set_next_result(bool success, int32_t code)
{
    next_success = success;
    next_code = code;
}

EXPORT int pending_callbacks()
{
    return static_cast<int>(queued.size());
}

EXPORT const char* take_calls()
{
    static std::string result;
    result = calls.dump();
    calls.clear();
    return result.c_str();
}

extern "C" void Discord_ActivityAssets_Drop(Discord_ActivityAssets* self)
{
}

extern "C" void Discord_ActivityAssets_Init(Discord_ActivityAssets* self)
{
}

extern "C" void Discord_ActivityAssets_SetLargeImage(Discord_ActivityAssets* self, Discord_String* value)
{
    calls.push_back({"Discord_ActivityAssets_SetLargeImage", std::string(reinterpret_cast<const char*>(value->ptr), value->size)});
}

extern "C" void Discord_ActivityAssets_SetLargeText(Discord_ActivityAssets* self, Discord_String* value)
{
    calls.push_back({"Discord_ActivityAssets_SetLargeText", std::string(reinterpret_cast<const char*>(value->ptr), value->size)});
}

extern "C" void Discord_ActivityAssets_SetSmallImage(Discord_ActivityAssets* self, Discord_String* value)
{
    calls.push_back({"Discord_ActivityAssets_SetSmallImage", std::string(reinterpret_cast<const char*>(value->ptr), value->size)});
}

extern "C" void Discord_ActivityAssets_SetSmallText(Discord_ActivityAssets* self, Discord_String* value)
{
    calls.push_back({"Discord_ActivityAssets_SetSmallText", std::string(reinterpret_cast<const char*>(value->ptr), value->size)});
}

extern "C" void Discord_ActivityButton_Drop(Discord_ActivityButton* self)
{
}

extern "C" void Discord_ActivityButton_Init(Discord_ActivityButton* self)
{
}

extern "C" void Discord_ActivityButton_SetLabel(Discord_ActivityButton* self, Discord_String value)
{
    calls.push_back({"Discord_ActivityButton_SetLabel", std::string(reinterpret_cast<const char*>(value.ptr), value.size)});
}

extern "C" void Discord_ActivityButton_SetUrl(Discord_ActivityButton* self, Discord_String value)
{
    calls.push_back({"Discord_ActivityButton_SetUrl", std::string(reinterpret_cast<const char*>(value.ptr), value.size)});
}

extern "C" void Discord_ActivityTimestamps_Drop(Discord_ActivityTimestamps* self)
{
}

extern "C" void Discord_ActivityTimestamps_Init(Discord_ActivityTimestamps* self)
{
}

extern "C" void Discord_ActivityTimestamps_SetEnd(Discord_ActivityTimestamps* self, uint64_t value)
{
    calls.push_back({"Discord_ActivityTimestamps_SetEnd", value});
}

extern "C" void Discord_ActivityTimestamps_SetStart(Discord_ActivityTimestamps* self, uint64_t value)
{
    calls.push_back({"Discord_ActivityTimestamps_SetStart", value});
}

extern "C" void Discord_Activity_AddButton(Discord_Activity* self, Discord_ActivityButton const* button)
{
    calls.push_back({"Discord_Activity_AddButton", true});
}

extern "C" void Discord_Activity_Drop(Discord_Activity* self)
{
}

extern "C" void Discord_Activity_Init(Discord_Activity* self)
{
}

extern "C" void Discord_Activity_SetAssets(Discord_Activity* self, Discord_ActivityAssets* value)
{
    calls.push_back({"Discord_Activity_SetAssets", true});
}

extern "C" void Discord_Activity_SetDetails(Discord_Activity* self, Discord_String* value)
{
    calls.push_back({"Discord_Activity_SetDetails", std::string(reinterpret_cast<const char*>(value->ptr), value->size)});
}

extern "C" void Discord_Activity_SetName(Discord_Activity* self, Discord_String value)
{
    calls.push_back({"Discord_Activity_SetName", std::string(reinterpret_cast<const char*>(value.ptr), value.size)});
}

extern "C" void Discord_Activity_SetState(Discord_Activity* self, Discord_String* value)
{
    calls.push_back({"Discord_Activity_SetState", std::string(reinterpret_cast<const char*>(value->ptr), value->size)});
}

extern "C" void Discord_Activity_SetTimestamps(Discord_Activity* self, Discord_ActivityTimestamps* value)
{
    calls.push_back({"Discord_Activity_SetTimestamps", true});
}

extern "C" void Discord_Activity_SetType(Discord_Activity* self, Discord_ActivityTypes value)
{
    calls.push_back({"Discord_Activity_SetType", value == Discord_ActivityTypes_Watching ? "Watching" : "Listening"});
}

extern "C" void Discord_ClientResult_Drop(Discord_ClientResult* self)
{
}

extern "C" int32_t Discord_ClientResult_ErrorCode(Discord_ClientResult* self)
{
    return delivering_code;
}

extern "C" bool Discord_ClientResult_Successful(Discord_ClientResult* self)
{
    return delivering_success;
}

extern "C" void Discord_Client_AddLogCallback(Discord_Client* self, Discord_Client_LogCallback callback, Discord_FreeFn callback__userDataFree, void* callback__userData, Discord_LoggingSeverity minSeverity)
{
}

extern "C" void Discord_Client_ClearRichPresence(Discord_Client* self)
{
}

extern "C" void Discord_Client_Drop(Discord_Client* self)
{
}

extern "C" uint64_t Discord_Client_GetApplicationId(Discord_Client* self)
{
    return 0;
}

extern "C" void Discord_Client_Init(Discord_Client* self)
{
}

extern "C" void Discord_Client_SetApplicationId(Discord_Client* self, uint64_t applicationId)
{
}

extern "C" void Discord_Client_UpdateRichPresence(Discord_Client* self, Discord_Activity* activity, Discord_Client_UpdateRichPresenceCallback cb, Discord_FreeFn cb__userDataFree, void* cb__userData)
{
    if (cb) queued.push_back({cb, cb__userDataFree, cb__userData});
    else if (cb__userDataFree) cb__userDataFree(cb__userData);
}

extern "C" void Discord_Free(void* ptr)
{
}

extern "C" void Discord_RunCallbacks()
{
    auto ready = std::move(queued);
    queued.clear();
    for (auto& update : ready)
    {
        delivering_success = next_success;
        delivering_code = next_code;
        static Discord_ClientResult result{};
        update.callback(&result, update.data);
        if (update.free) update.free(update.data);
    }
}
