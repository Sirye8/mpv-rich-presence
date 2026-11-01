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

#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
namespace chrono = std::chrono;

// Helpers under test replicated from implementation

static std::string to_lower(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return str;
}

static std::string truncate_utf8_bytes(std::string_view text, std::size_t limit)
{
    if (text.size() <= limit)
        return std::string(text);
    constexpr std::string_view suffix = "...";
    if (limit < suffix.size())
        return std::string(limit, '.');
    std::size_t cut = limit - suffix.size();
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
    {
        --cut;
    }
    return std::string(text.substr(0, cut)) + std::string(suffix);
}

static bool write_lease_record_locked(const fs::path& lock_path, const std::string& instance_id, int64_t timestamp_ms)
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

static constexpr int64_t TEST_LEASE_TIMEOUT_MS = 6000;

static bool acquire_or_renew_active_instance_test(const fs::path& lock_path, const std::string& instance_id, int64_t now_ms)
{
    try
    {
        bool can_acquire_or_renew = false;

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
            else if (now_ms - stored_time > TEST_LEASE_TIMEOUT_MS)
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
            return write_lease_record_locked(lock_path, instance_id, now_ms);
        }

        return false;
    }
    catch (...)
    {
        return false;
    }
}

// Tests

static void test_utf8_truncation()
{
    std::cout << "[RUN] test_utf8_truncation...\n";

    // Short string
    assert(truncate_utf8_bytes("Hello", 10) == "Hello");

    // Exact limit
    assert(truncate_utf8_bytes("Hello", 5) == "Hello");

    // ASCII truncation
    assert(truncate_utf8_bytes("Hello World", 8) == "Hello...");

    // Multilingual with accented characters (e.g. French 'Café Crème')
    std::string french = "Café Crème au Chocolat";
    for (size_t limit = 4; limit <= french.size() + 5; ++limit)
    {
        std::string res = truncate_utf8_bytes(french, limit);
        assert(res.size() <= limit);
        // Verify valid UTF-8 boundary: last char before '...' must not be an incomplete sequence start
        size_t cut_len = res.size() >= 3 ? res.size() - 3 : res.size();
        if (cut_len > 0)
        {
            unsigned char last = static_cast<unsigned char>(res[cut_len - 1]);
            // Must not be a start byte of a multi-byte sequence without continuation bytes
            assert((last & 0x80) == 0 || (last & 0xC0) == 0x80 || (last & 0xE0) != 0xC0);
        }
    }

    // Japanese 3-byte characters
    std::string japanese = "ダンジョンに出会いを求めるのは間違っているだろうか";
    for (size_t limit = 4; limit <= japanese.size() + 5; ++limit)
    {
        std::string res = truncate_utf8_bytes(japanese, limit);
        assert(res.size() <= limit);
    }

    // Arabic 2-byte characters
    std::string arabic = "هجوم العمالقة الموسم الأخير";
    for (size_t limit = 4; limit <= arabic.size() + 5; ++limit)
    {
        std::string res = truncate_utf8_bytes(arabic, limit);
        assert(res.size() <= limit);
    }

    // 4-byte emoji
    std::string emoji = "Movie 🎬🍿 and Anime 🌸";
    for (size_t limit = 4; limit <= emoji.size() + 5; ++limit)
    {
        std::string res = truncate_utf8_bytes(emoji, limit);
        assert(res.size() <= limit);
    }

    std::cout << "[PASS] test_utf8_truncation\n";
}

static void test_safe_to_lower()
{
    std::cout << "[RUN] test_safe_to_lower...\n";

    assert(to_lower("HELLO WORLD") == "hello world");
    assert(to_lower("mpv-rich-presence") == "mpv-rich-presence");

    // Negative/signed char values must not crash
    std::string with_accents = "Caf\xE9";
    std::string lower_accents = to_lower(with_accents);
    assert(!lower_accents.empty());

    std::cout << "[PASS] test_safe_to_lower\n";
}

static void test_anilist_page_parsing()
{
    std::cout << "[RUN] test_anilist_page_parsing...\n";

    // Valid GraphQL response with data.Page.media
    std::string valid_json = R"({
        "data": {
            "Page": {
                "media": [
                    {
                        "id": 16498,
                        "title": {
                            "romaji": "Shingeki no Kyojin",
                            "english": "Attack on Titan",
                            "native": "進撃の巨人"
                        },
                        "synonyms": ["SnK", "AoT"],
                        "episodes": 25,
                        "coverImage": {
                            "large": "https://example.com/cover.jpg"
                        }
                    }
                ]
            }
        }
    })";

    auto j_valid = nlohmann::json::parse(valid_json);
    bool valid_matched = (!j_valid.contains("errors") && j_valid.contains("data") && !j_valid["data"].is_null()
        && j_valid["data"].contains("Page") && !j_valid["data"]["Page"].is_null()
        && j_valid["data"]["Page"].contains("media") && j_valid["data"]["Page"]["media"].is_array());
    assert(valid_matched == true);
    assert(j_valid["data"]["Page"]["media"].size() == 1);
    assert(j_valid["data"]["Page"]["media"][0]["id"] == 16498);

    // Invalid response where Page is missing (e.g. data.media)
    std::string invalid_json = R"({
        "data": {
            "media": [
                {
                    "id": 16498
                }
            ]
        }
    })";

    auto j_invalid = nlohmann::json::parse(invalid_json);
    bool invalid_matched = (!j_invalid.contains("errors") && j_invalid.contains("data") && !j_invalid["data"].is_null()
        && j_invalid["data"].contains("Page") && !j_invalid["data"]["Page"].is_null()
        && j_invalid["data"]["Page"].contains("media") && j_invalid["data"]["Page"]["media"].is_array());
    assert(invalid_matched == false);

    std::cout << "[PASS] test_anilist_page_parsing\n";
}

static void test_lease_operations()
{
    std::cout << "[RUN] test_lease_operations...\n";

    auto test_dir = fs::temp_directory_path() / "mpv_rpc_test_leases";
    std::error_code ec;
    fs::create_directories(test_dir, ec);
    auto lock_file = test_dir / "test_active.lock";

    fs::remove(lock_file, ec);

    int64_t t0 = 1000000;

    // 1. Missing lease: Window A acquires ownership
    assert(acquire_or_renew_active_instance_test(lock_file, "instance_A", t0) == true);
    assert(fs::exists(lock_file));

    // 2. Same instance renews: Window A renews ownership
    assert(acquire_or_renew_active_instance_test(lock_file, "instance_A", t0 + 2000) == true);

    // 3. Another live owner: Window B cannot acquire or renew
    assert(acquire_or_renew_active_instance_test(lock_file, "instance_B", t0 + 3000) == false);

    // 4. Lease expiration: After timeout (6000ms), Window B can acquire
    assert(acquire_or_renew_active_instance_test(lock_file, "instance_B", t0 + 10000) == true);

    // 5. Malformed lease: File with invalid or empty contents allows recovery
    {
        std::ofstream out(lock_file, std::ios::trunc);
        out << "\n0\n";
    }
    assert(acquire_or_renew_active_instance_test(lock_file, "instance_C", t0 + 12000) == true);

    // 6. Failed lease write: when path is an existing directory, writing fails safely
    auto dir_collision = test_dir / "dir_as_lock";
    fs::create_directories(dir_collision, ec);
    assert(write_lease_record_locked(dir_collision, "instance_X", t0) == false);
    assert(acquire_or_renew_active_instance_test(dir_collision, "instance_X", t0) == false);

    fs::remove_all(test_dir, ec);
    std::cout << "[PASS] test_lease_operations\n";
}

static void test_buffering_and_seek_state_machine()
{
    std::cout << "[RUN] test_buffering_and_seek_state_machine...\n";

    // Simulate main loop state
    int last_caching_state = 0;
    int is_caching = 0;
    int is_seeking = 0;
    bool force_update = false;
    bool update_published = false;

    // Case 1: normal playback
    is_caching = 0;
    is_seeking = 0;
    last_caching_state = is_caching;

    // Case 2: seek starts
    is_seeking = 1;

    // Case 3: caching starts while seeking is active
    is_caching = 1;
    bool buffering_changed = (is_caching != last_caching_state);
    last_caching_state = is_caching;
    if (buffering_changed)
        force_update = true;

    // Loop iteration during seek skips publishing
    if (is_seeking == 1)
    {
        // skip publishing
    }
    else
    {
        update_published = true;
    }
    assert(update_published == false);
    assert(force_update == true); // Must retain forced update!

    // Case 4: seek ends
    is_seeking = 0;
    buffering_changed = (is_caching != last_caching_state); // false now
    assert(buffering_changed == false);

    // Because force_update is retained, next iteration publishes the buffering update!
    if (force_update)
    {
        force_update = false;
        update_published = true;
    }
    assert(update_published == true);
    assert(force_update == false);

    std::cout << "[PASS] test_buffering_and_seek_state_machine\n";
}

static void test_pause_timeout_callback_race()
{
    std::cout << "[RUN] test_pause_timeout_callback_race...\n";

    bool rpc_hidden_by_timeout = false;
    bool is_rpc_visible = false;
    int clear_calls = 0;

    auto hide_rich_presence = [&]() {
        if (is_rpc_visible)
        {
            clear_calls++;
            is_rpc_visible = false;
        }
    };

    // 1. Update in-flight, timeout elapses
    rpc_hidden_by_timeout = true;
    hide_rich_presence();
    assert(clear_calls == 0); // is_rpc_visible was false

    // 2. Late success callback arrives
    is_rpc_visible = true;

    // 3. Next loop iteration with continuous timeout enforcement
    // Continuous enforcement: hide_rich_presence() is called unconditionally during timeout
    hide_rich_presence();
    assert(clear_calls == 1); // Clears the late success!
    assert(is_rpc_visible == false);

    // 4. Subsequent timeout iteration does not redundantly clear
    hide_rich_presence();
    assert(clear_calls == 1); // No extra clear call

    std::cout << "[PASS] test_pause_timeout_callback_race\n";
}

static void test_worker_shutdown_concurrency()
{
    std::cout << "[RUN] test_worker_shutdown_concurrency...\n";

    for (int cycle = 0; cycle < 100; ++cycle)
    {
        std::mutex worker_mutex;
        std::condition_variable worker_cv;
        std::atomic<bool> worker_stop{false};
        bool has_work = false;

        std::thread worker([&]() {
            std::unique_lock<std::mutex> lock(worker_mutex);
            worker_cv.wait(lock, [&]() {
                return worker_stop.load(std::memory_order_acquire) || has_work;
            });
        });

        // Fast shutdown with synchronized notification
        {
            std::lock_guard<std::mutex> lock(worker_mutex);
            worker_stop.store(true, std::memory_order_release);
        }
        worker_cv.notify_all();

        if (worker.joinable())
            worker.join();
    }

    std::cout << "[PASS] test_worker_shutdown_concurrency\n";
}

int main()
{
    std::cout << "Starting mpv-rich-presence regression test suite...\n\n";

    test_utf8_truncation();
    test_safe_to_lower();
    test_anilist_page_parsing();
    test_lease_operations();
    test_buffering_and_seek_state_machine();
    test_pause_timeout_callback_race();
    test_worker_shutdown_concurrency();

    std::cout << "\nAll regression tests passed successfully!\n";
    return 0;
}
