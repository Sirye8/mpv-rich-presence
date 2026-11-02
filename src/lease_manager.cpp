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

#include "mpv_rich_presence/lease_manager.hpp"

#include <chrono>
#include <format>
#include <fstream>
#include <mutex>
#include <system_error>

namespace chrono = std::chrono;

#if defined(_WIN32)
ipc_mutex_guard::ipc_mutex_guard(const std::string& mutex_name)
{
    std::wstring wname(mutex_name.begin(), mutex_name.end());
    m_handle = CreateMutexW(NULL, FALSE, wname.c_str());
    if (m_handle)
    {
        DWORD res = WaitForSingleObject(m_handle, 3000);
        m_locked = (res == WAIT_OBJECT_0 || res == WAIT_ABANDONED);
    }
}

ipc_mutex_guard::~ipc_mutex_guard()
{
    if (m_locked && m_handle)
        ReleaseMutex(m_handle);
    if (m_handle)
        CloseHandle(m_handle);
}
#else
std::mutex g_process_lease_mutex;

ipc_mutex_guard::ipc_mutex_guard(const std::string& mutex_name)
    : m_thread_lock(g_process_lease_mutex)
{
    std::error_code ec;
    auto temp_dir = fs::temp_directory_path(ec);
    if (ec)
        temp_dir = fs::path("/tmp");
    auto lock_dir = temp_dir / "mpv-rich-presence";
    fs::create_directories(lock_dir, ec);
    auto lock_path = lock_dir / ".lease.lock";
    m_fd = open(lock_path.c_str(), O_CREAT | O_RDWR, 0666);
    if (m_fd >= 0)
    {
        struct flock fl {};
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        if (fcntl(m_fd, F_SETLKW, &fl) == 0)
            m_locked = true;
    }

    if (!m_locked)
    {
        if (m_fd >= 0)
        {
            close(m_fd);
            m_fd = -1;
        }
        if (m_thread_lock.owns_lock())
            m_thread_lock.unlock();
    }
}

ipc_mutex_guard::~ipc_mutex_guard()
{
    if (m_locked && m_fd >= 0)
    {
        struct flock fl {};
        fl.l_type = F_UNLCK;
        fl.l_whence = SEEK_SET;
        fcntl(m_fd, F_SETLK, &fl);
    }
    if (m_fd >= 0)
    {
        close(m_fd);
        m_fd = -1;
    }
}
#endif

fs::path get_default_lock_directory()
{
    std::error_code ec;
    auto dir = fs::temp_directory_path(ec);
    if (ec)
    {
#if defined(_WIN32)
        dir = fs::path("C:/Temp");
#else
        dir = fs::path("/tmp");
#endif
    }
    dir /= "mpv-rich-presence";
    fs::create_directories(dir, ec);
    return dir;
}

fs::path get_default_lock_file_path()
{
    return get_default_lock_directory() / "mpv-rpc-active.lock";
}

bool write_lease_record_locked(const fs::path& lock_path, const std::string& instance_id, int64_t timestamp_ms)
{
    auto tmp_path = lock_path.parent_path() / std::format("mpv-rpc-active.{}.tmp", instance_id);
    std::error_code ec;

    try
    {
        {
            std::ofstream out;
            out.exceptions(std::ios::failbit | std::ios::badbit);
            out.open(tmp_path, std::ios::binary | std::ios::trunc);
            out << instance_id << '\n' << timestamp_ms << '\n';
            out.flush();
            out.close();
        }

        fs::rename(tmp_path, lock_path, ec);
        if (ec)
        {
            // Direct write fallback within the held interprocess lock
            std::ofstream out;
            out.exceptions(std::ios::failbit | std::ios::badbit);
            out.open(lock_path, std::ios::binary | std::ios::trunc);
            out << instance_id << '\n' << timestamp_ms << '\n';
            out.flush();
            out.close();
            fs::remove(tmp_path, ec);
        }
        return true;
    }
    catch (...)
    {
        fs::remove(tmp_path, ec);
        return false;
    }
}

bool claim_active_instance(const std::string& instance_id, const fs::path& lock_path)
{
    try
    {
        ipc_mutex_guard guard;
        if (!guard.is_locked())
            return false;

        auto now = chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count();

        return write_lease_record_locked(lock_path, instance_id, now);
    }
    catch (...)
    {
        return false;
    }
}

bool acquire_or_renew_active_instance(const std::string& instance_id,
    const fs::path& lock_path,
    int64_t lease_timeout_ms)
{
    try
    {
        ipc_mutex_guard guard;
        if (!guard.is_locked())
            return false;

        bool can_acquire_or_renew = false;

        auto now = chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count();

        if (!fs::exists(lock_path))
        {
            can_acquire_or_renew = true;
        }
        else
        {
            std::ifstream f(lock_path);
            std::string stored_id;
            int64_t stored_time = 0;
            if (std::getline(f, stored_id))
            {
                std::string time_str;
                if (std::getline(f, time_str))
                {
                    try { stored_time = std::stoll(time_str); } catch (...) { stored_time = 0; }
                }
            }
            f.close();

            while (!stored_id.empty() && (stored_id.back() == '\r' || stored_id.back() == '\n' || stored_id.back() == ' '))
                stored_id.pop_back();

            if (stored_id == instance_id)
            {
                can_acquire_or_renew = true;
            }
            else if (stored_id.empty() || stored_time <= 0)
            {
                can_acquire_or_renew = true;
            }
            else if (now - stored_time > lease_timeout_ms)
            {
                can_acquire_or_renew = true;
            }
            else
            {
                return false;
            }
        }

        if (can_acquire_or_renew)
        {
            return write_lease_record_locked(lock_path, instance_id, now);
        }

        return false;
    }
    catch (...)
    {
        return false;
    }
}

bool is_active_instance(const std::string& instance_id,
    const fs::path& lock_path,
    int64_t lease_timeout_ms)
{
    try
    {
        ipc_mutex_guard guard;
        if (!guard.is_locked())
            return false;

        if (!fs::exists(lock_path))
            return true;

        std::ifstream f(lock_path);
        std::string stored_id;
        int64_t stored_time = 0;
        if (std::getline(f, stored_id))
        {
            std::string time_str;
            if (std::getline(f, time_str))
            {
                try { stored_time = std::stoll(time_str); } catch (...) { stored_time = 0; }
            }
        }

        while (!stored_id.empty() && (stored_id.back() == '\r' || stored_id.back() == '\n' || stored_id.back() == ' '))
            stored_id.pop_back();

        if (stored_id == instance_id)
            return true;

        // Malformed, empty, or non-positive timestamp records are treated as immediately recoverable
        if (stored_id.empty() || stored_time <= 0)
            return true;

        auto now = chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()).count();

        if (now - stored_time > lease_timeout_ms)
        {
            return true;
        }

        return false;
    }
    catch (...)
    {
        return false;
    }
}

void release_active_instance(const std::string& instance_id, const fs::path& lock_path)
{
    try
    {
        ipc_mutex_guard guard;
        if (!guard.is_locked())
            return;

        if (!fs::exists(lock_path))
            return;

        std::ifstream f(lock_path);
        std::string stored_id;
        if (std::getline(f, stored_id))
        {
            while (!stored_id.empty() && (stored_id.back() == '\r' || stored_id.back() == '\n' || stored_id.back() == ' '))
                stored_id.pop_back();

            if (stored_id == instance_id)
            {
                f.close();
                std::error_code ec;
                fs::remove(lock_path, ec);
            }
        }
    }
    catch (...)
    {
    }
}
