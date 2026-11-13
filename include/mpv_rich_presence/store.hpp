// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

namespace mpvrp
{
    class profile_store
    {
        sqlite3* db = nullptr;
    public:
        explicit profile_store(const std::filesystem::path& path,
                               const std::filesystem::path& legacy = {});
        ~profile_store();
        profile_store(const profile_store&) = delete;
        profile_store& operator=(const profile_store&) = delete;
        void renew_pin(const std::string& owner, uint64_t expiry);
        void release_pin(const std::string& owner);
        void close_window(const std::string& owner);
        bool blocked(const std::string& path) const;
        int version() const;
        nlohmann::json get(const std::string& category, const std::string& scope) const;
        nlohmann::json episode_watch(int viewer, int media, const std::string& episode) const;
        void put(const std::string& category, const std::string& scope, const nlohmann::json& value);
        nlohmann::json record_counts() const;
        nlohmann::json records(const std::string& category) const;
        nlohmann::json pending() const;
        nlohmann::json effective(const std::string& category, const std::string& path, std::string* winning_scope = nullptr) const;
        void save(const std::string& category, const std::string& scope, const nlohmann::json& patch);
        void clear(const std::string& category, const std::string& scope);
    };
}
