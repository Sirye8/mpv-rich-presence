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

#include <cstdint>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

class ipc_mutex_guard
{
public:
    explicit ipc_mutex_guard(const std::string& mutex_name = "Local\\mpv_rich_presence_lease_mutex");
    ~ipc_mutex_guard();

    ipc_mutex_guard(const ipc_mutex_guard&) = delete;
    ipc_mutex_guard& operator=(const ipc_mutex_guard&) = delete;

    bool is_locked() const { return m_locked; }

private:
#if defined(_WIN32)
    HANDLE m_handle = NULL;
#else
    int m_fd = -1;
    bool m_thread_locked = false;
#endif
    bool m_locked = false;
};

static constexpr int64_t DEFAULT_LEASE_TIMEOUT_MS = 6000;

fs::path get_default_lock_directory();
fs::path get_default_lock_file_path();

bool write_lease_record_locked(const fs::path& lock_path, const std::string& instance_id, int64_t timestamp_ms);

bool claim_active_instance(const std::string& instance_id, const fs::path& lock_path = get_default_lock_file_path());

bool acquire_or_renew_active_instance(const std::string& instance_id,
    const fs::path& lock_path = get_default_lock_file_path(),
    int64_t lease_timeout_ms = DEFAULT_LEASE_TIMEOUT_MS);

bool is_active_instance(const std::string& instance_id,
    const fs::path& lock_path = get_default_lock_file_path(),
    int64_t lease_timeout_ms = DEFAULT_LEASE_TIMEOUT_MS);

void release_active_instance(const std::string& instance_id,
    const fs::path& lock_path = get_default_lock_file_path());
