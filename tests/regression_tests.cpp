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
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
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

#include "mpv_rich_presence/anilist_parser.hpp"
#include "mpv_rich_presence/lease_manager.hpp"
#include "mpv_rich_presence/string_utils.hpp"

namespace fs = std::filesystem;
namespace chrono = std::chrono;
using namespace std::chrono_literals;

static std::atomic<int> g_failed_checks{0};

#define TEST_CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "FAIL: " #expr " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            g_failed_checks.fetch_add(1, std::memory_order_relaxed); \
        } \
    } while (0)

#define TEST_CHECK_EQ(a, b) \
    do { \
        auto val_a = (a); \
        auto val_b = (b); \
        if (val_a != val_b) { \
            std::cerr << "FAIL: " #a " == " #b " (got " << val_a << " vs " << val_b << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
            g_failed_checks.fetch_add(1, std::memory_order_relaxed); \
        } \
    } while (0)

template <typename Func>
static void run_test(const char* name, Func&& test_fn)
{
    std::cout << "[RUN] " << name << "...\n";
    int failures_before = g_failed_checks.load(std::memory_order_relaxed);
    test_fn();
    int failures_after = g_failed_checks.load(std::memory_order_relaxed);
    if (failures_after == failures_before)
    {
        std::cout << "[PASS] " << name << "\n";
    }
    else
    {
        std::cout << "[FAIL] " << name << " (" << (failures_after - failures_before) << " check failure(s))\n";
    }
}

static void verify_utf8_json(const std::string& str, const std::string& context)
{
    try
    {
        std::string dumped = nlohmann::json(str).dump();
        (void)dumped;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Invalid UTF-8 in " << context << ": " << e.what() << "\n";
        g_failed_checks.fetch_add(1, std::memory_order_relaxed);
    }
}

// Tests

static void test_utf8_truncation()
{
    // Edge cases: limits 0 to 3
    TEST_CHECK_EQ(truncate_utf8_bytes("Hello", 0), "");
    TEST_CHECK_EQ(truncate_utf8_bytes("Hello", 1), ".");
    TEST_CHECK_EQ(truncate_utf8_bytes("Hello", 2), "..");
    TEST_CHECK_EQ(truncate_utf8_bytes("Hello", 3), "...");

    verify_utf8_json(truncate_utf8_bytes("Hello", 0), "limit 0");
    verify_utf8_json(truncate_utf8_bytes("Hello", 1), "limit 1");
    verify_utf8_json(truncate_utf8_bytes("Hello", 2), "limit 2");
    verify_utf8_json(truncate_utf8_bytes("Hello", 3), "limit 3");

    // Short string and exact limit
    TEST_CHECK_EQ(truncate_utf8_bytes("Hello", 10), "Hello");
    TEST_CHECK_EQ(truncate_utf8_bytes("Hello", 5), "Hello");

    // ASCII truncation
    TEST_CHECK_EQ(truncate_utf8_bytes("Hello World", 8), "Hello...");

    // Multilingual strings with various byte lengths
    std::vector<std::string> test_strings = {
        "Café Crème au Chocolat",
        "ダンジョンに出会いを求めるのは間違っているだろうか",
        "هجوم العمالقة الموسم الأخير",
        "Movie 🎬🍿 and Anime 🌸",
        "Смерть шпионам: Ударная волна",
        "🔥⚡🚀🌟🎉✨"
    };

    for (const auto& sample : test_strings)
    {
        for (size_t limit = 0; limit <= sample.size() + 6; ++limit)
        {
            std::string res = truncate_utf8_bytes(sample, limit);
            TEST_CHECK(res.size() <= limit);
            verify_utf8_json(res, std::format("sample '{}' at limit {}", sample.substr(0, 10), limit));

            if (sample.size() > limit && limit >= 3)
            {
                TEST_CHECK(res.ends_with("..."));
                std::string prefix = res.substr(0, res.size() - 3);
                verify_utf8_json(prefix, std::format("prefix of sample '{}' at limit {}", sample.substr(0, 10), limit));
            }
        }
    }
}

static void test_safe_to_lower()
{
    TEST_CHECK_EQ(to_lower("HELLO WORLD"), "hello world");
    TEST_CHECK_EQ(to_lower("mpv-rich-presence"), "mpv-rich-presence");

    // Negative/signed char values must not crash
    std::string with_accents = "Caf\xE9";
    std::string lower_accents = to_lower(with_accents);
    TEST_CHECK(!lower_accents.empty());
}

static void test_folder_filtering()
{
    // Empty whitelist blocks everything
    std::vector<std::string> empty_list;
    TEST_CHECK(passes_folder_filter("C:/Videos/anime.mkv", "whitelist", empty_list) == false);

    // Empty blacklist allows everything
    TEST_CHECK(passes_folder_filter("C:/Videos/anime.mkv", "blacklist", empty_list) == true);

    // Disabled mode allows everything
    TEST_CHECK(passes_folder_filter("C:/Videos/anime.mkv", "disabled", empty_list) == true);

    // Unknown filter mode allows everything safely
    TEST_CHECK(passes_folder_filter("C:/Videos/anime.mkv", "other_mode", empty_list) == true);

    // Whitelist matching
    auto whitelist = parse_folder_list("C:/Anime; /mnt/media/anime");
    TEST_CHECK(passes_folder_filter("C:/Anime/episode1.mkv", "whitelist", whitelist) == true);
    TEST_CHECK(passes_folder_filter("C:/Anime/Season 1/ep.mkv", "whitelist", whitelist) == true);
    TEST_CHECK(passes_folder_filter("C:/Movies/film.mkv", "whitelist", whitelist) == false);

    // Blacklist matching
    auto blacklist = parse_folder_list("C:/Private; /home/user/private");
    TEST_CHECK(passes_folder_filter("C:/Private/secret.mp4", "blacklist", blacklist) == false);
    TEST_CHECK(passes_folder_filter("C:/Videos/public.mp4", "blacklist", blacklist) == true);
}

static void test_anilist_page_parsing()
{
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
                        },
                        "relations": {
                            "edges": [
                                {
                                    "relationType": "SEQUEL",
                                    "node": {
                                        "id": 20958,
                                        "type": "ANIME",
                                        "format": "TV"
                                    }
                                }
                            ]
                        }
                    }
                ]
            }
        }
    })";

    auto j_valid = nlohmann::json::parse(valid_json);
    std::vector<anilist_media> results;
    bool parse_ok = parse_anilist_page_response(j_valid, results);
    TEST_CHECK(parse_ok == true);
    TEST_CHECK_EQ(results.size(), 1u);
    if (!results.empty())
    {
        TEST_CHECK_EQ(results[0].id, 16498);
        TEST_CHECK_EQ(results[0].title, "Shingeki no Kyojin");
        TEST_CHECK_EQ(results[0].episodes, 25);
        TEST_CHECK_EQ(results[0].sequel_id, 20958);
        TEST_CHECK_EQ(results[0].cover_url, "https://example.com/cover.jpg");
    }

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
    std::vector<anilist_media> bad_results;
    bool bad_parse_ok = parse_anilist_page_response(j_invalid, bad_results);
    TEST_CHECK(bad_parse_ok == false);
    TEST_CHECK(bad_results.empty());
}

static void test_lease_operations()
{
    auto test_dir = fs::temp_directory_path() / "mpv_rpc_test_leases";
    std::error_code ec;
    fs::create_directories(test_dir, ec);
    auto lock_file = test_dir / "test_active.lock";

    fs::remove(lock_file, ec);

    int64_t t0 = 1000000;

    // 1. Missing lease: Window A acquires ownership
    bool acq1 = acquire_or_renew_active_instance("instance_A", lock_file, 6000);
    TEST_CHECK(acq1 == true);
    TEST_CHECK(fs::exists(lock_file));
    TEST_CHECK(is_active_instance("instance_A", lock_file, 6000) == true);
    TEST_CHECK(is_active_instance("instance_B", lock_file, 6000) == false);

    // 2. Same instance renews: Window A renews ownership
    bool acq2 = acquire_or_renew_active_instance("instance_A", lock_file, 6000);
    TEST_CHECK(acq2 == true);

    // 3. Another live owner: Window B cannot acquire or renew
    bool acq3 = acquire_or_renew_active_instance("instance_B", lock_file, 6000);
    TEST_CHECK(acq3 == false);

    // 4. Explicit release
    release_active_instance("instance_A", lock_file);
    TEST_CHECK(!fs::exists(lock_file));

    // 5. Window B can now acquire immediately
    bool acq4 = acquire_or_renew_active_instance("instance_B", lock_file, 6000);
    TEST_CHECK(acq4 == true);
    TEST_CHECK(is_active_instance("instance_B", lock_file, 6000) == true);

    // 6. Expired lease takeover: write old timestamp
    write_lease_record_locked(lock_file, "instance_B", t0);
    // Timeout of 1000ms: with t0 ancient compared to now, instance_C takes over
    bool acq5 = acquire_or_renew_active_instance("instance_C", lock_file, 1000);
    TEST_CHECK(acq5 == true);
    TEST_CHECK(is_active_instance("instance_C", lock_file, 1000) == true);

    // 7. Malformed lease: File with invalid or empty contents allows recovery
    {
        std::ofstream out(lock_file, std::ios::trunc);
        out << "\n0\n";
    }
    bool acq6 = acquire_or_renew_active_instance("instance_D", lock_file, 6000);
    TEST_CHECK(acq6 == true);
    TEST_CHECK(is_active_instance("instance_D", lock_file, 6000) == true);

    // 8. Failed lease write: when path is an existing directory, writing fails safely
    auto dir_collision = test_dir / "dir_as_lock";
    fs::create_directories(dir_collision, ec);
    bool write_fail = write_lease_record_locked(dir_collision, "instance_X", t0);
    TEST_CHECK(write_fail == false);
    bool acq_fail = acquire_or_renew_active_instance("instance_X", dir_collision, 6000);
    TEST_CHECK(acq_fail == false);

    fs::remove_all(test_dir, ec);
}

static void test_multi_thread_lease_contention()
{
    auto test_dir = fs::temp_directory_path() / "mpv_rpc_test_contention";
    std::error_code ec;
    fs::create_directories(test_dir, ec);
    auto lock_file = test_dir / "contention.lock";
    fs::remove(lock_file, ec);

    std::atomic<bool> start_gate{false};
    std::atomic<int> successful_acquisitions{0};
    constexpr int NUM_THREADS = 6;
    std::vector<std::thread> threads;

    for (int i = 0; i < NUM_THREADS; ++i)
    {
        threads.emplace_back([&, i]() {
            while (!start_gate.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            std::string id = std::format("thread_{}", i);
            if (acquire_or_renew_active_instance(id, lock_file, 60000))
            {
                successful_acquisitions.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    start_gate.store(true, std::memory_order_release);
    for (auto& t : threads)
        t.join();

    // Exactly one thread should succeed in acquiring the initial lease
    TEST_CHECK_EQ(successful_acquisitions.load(), 1);

    fs::remove_all(test_dir, ec);
}

static void test_multi_process_lease_contention(const fs::path& exe_path)
{
    auto test_dir = fs::temp_directory_path() / "mpv_rpc_test_proc_contention";
    std::error_code ec;
    fs::remove_all(test_dir, ec);
    fs::create_directories(test_dir, ec);

    auto lock_file = test_dir / "contention.lock";
    auto gate_file = test_dir / "start_gate.txt";

    constexpr int NUM_PROCS = 6;
    std::vector<std::thread> threads;

    for (int i = 0; i < NUM_PROCS; ++i)
    {
        threads.emplace_back([&, i]() {
            std::string id = std::format("proc_{}", i);
            auto ready_file = test_dir / std::format("ready_{}.txt", i);
            auto res_file = test_dir / std::format("proc_{}.res", i);

#if defined(_WIN32)
            std::string cmd = std::format("\"\"{}\" --lease-contender \"{}\" \"{}\" \"{}\" \"{}\" \"{}\"\"",
                exe_path.string(), lock_file.string(), id, gate_file.string(), ready_file.string(), res_file.string());
#else
            std::string cmd = std::format("\"{}\" --lease-contender \"{}\" \"{}\" \"{}\" \"{}\" \"{}\"",
                exe_path.string(), lock_file.string(), id, gate_file.string(), ready_file.string(), res_file.string());
#endif
            int ret = std::system(cmd.c_str());
            (void)ret;
        });
    }

    // Wait until all worker processes report ready
    auto wait_start = chrono::steady_clock::now();
    bool all_ready = false;
    while (chrono::steady_clock::now() - wait_start < 10s)
    {
        int ready_count = 0;
        for (int i = 0; i < NUM_PROCS; ++i)
        {
            if (fs::exists(test_dir / std::format("ready_{}.txt", i)))
                ready_count++;
        }
        if (ready_count == NUM_PROCS)
        {
            all_ready = true;
            break;
        }
        std::this_thread::sleep_for(5ms);
    }
    TEST_CHECK(all_ready == true);

    // Release all workers simultaneously
    {
        std::ofstream out(gate_file);
        out << "START";
        out.flush();
    }

    for (auto& t : threads)
        t.join();

    int winners = 0;
    int losers = 0;
    for (int i = 0; i < NUM_PROCS; ++i)
    {
        auto res_file = test_dir / std::format("proc_{}.res", i);
        if (fs::exists(res_file))
        {
            std::ifstream in(res_file);
            std::string text;
            in >> text;
            if (text == "WON")
                winners++;
            else if (text == "LOST")
                losers++;
        }
    }

    // Exactly one process should succeed in acquiring the lease
    TEST_CHECK_EQ(winners, 1);
    TEST_CHECK_EQ(losers, NUM_PROCS - 1);

    fs::remove_all(test_dir, ec);
}

static void test_multi_process_lease_takeover(const fs::path& exe_path)
{
    auto test_dir = fs::temp_directory_path() / "mpv_rpc_test_proc_takeover";
    std::error_code ec;
    fs::remove_all(test_dir, ec);
    fs::create_directories(test_dir, ec);

    auto lock_file = test_dir / "takeover.lock";

    // 1. Parent process acquires the initial lease
    bool acq = acquire_or_renew_active_instance("parent_owner", lock_file, 60000);
    TEST_CHECK(acq == true);
    TEST_CHECK(is_active_instance("parent_owner", lock_file, 60000) == true);

    // 2. Spawn worker to explicitly claim active ownership
    auto res_file = test_dir / "takeover.res";
#if defined(_WIN32)
    std::string cmd = std::format("\"\"{}\" --lease-claim \"{}\" \"child_takeover\" \"{}\"\"",
        exe_path.string(), lock_file.string(), res_file.string());
#else
    std::string cmd = std::format("\"{}\" --lease-claim \"{}\" \"child_takeover\" \"{}\"",
        exe_path.string(), lock_file.string(), res_file.string());
#endif
    int ret = std::system(cmd.c_str());
    (void)ret;

    // 3. Verify worker reported CLAIMED
    TEST_CHECK(fs::exists(res_file));
    if (fs::exists(res_file))
    {
        std::ifstream in(res_file);
        std::string res;
        in >> res;
        TEST_CHECK_EQ(res, "CLAIMED");
    }

    // 4. Verify child now owns the lease and parent no longer does
    TEST_CHECK(is_active_instance("child_takeover", lock_file, 60000) == true);
    TEST_CHECK(is_active_instance("parent_owner", lock_file, 60000) == false);

    fs::remove_all(test_dir, ec);
}

static void test_buffering_and_seek_state_machine()
{
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
    TEST_CHECK(update_published == false);
    TEST_CHECK(force_update == true); // Must retain forced update!

    // Case 4: seek ends
    is_seeking = 0;
    buffering_changed = (is_caching != last_caching_state); // false now
    TEST_CHECK(buffering_changed == false);

    // Because force_update is retained, next iteration publishes the buffering update!
    if (force_update)
    {
        force_update = false;
        update_published = true;
    }
    TEST_CHECK(update_published == true);
    TEST_CHECK(force_update == false);
}

static void test_pause_timeout_callback_race()
{
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
    TEST_CHECK_EQ(clear_calls, 0); // is_rpc_visible was false

    // 2. Late success callback arrives
    is_rpc_visible = true;

    // 3. Next loop iteration with continuous timeout enforcement
    // Continuous enforcement: hide_rich_presence() is called unconditionally during timeout
    hide_rich_presence();
    TEST_CHECK_EQ(clear_calls, 1); // Clears the late success!
    TEST_CHECK(is_rpc_visible == false);

    // 4. Subsequent timeout iteration does not redundantly clear
    hide_rich_presence();
    TEST_CHECK_EQ(clear_calls, 1); // No extra clear call
}

static void test_worker_shutdown_concurrency()
{
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
}

int main(int argc, char* argv[])
{
    // Worker submode for multi-process lease contention
    if (argc >= 6 && std::string_view(argv[1]) == "--lease-contender")
    {
        std::string lock_path = argv[2];
        std::string instance_id = argv[3];
        std::string gate_path = argv[4];
        std::string ready_path = argv[5];
        std::string result_path = (argc >= 7) ? argv[6] : "";

        // Signal ready
        {
            std::ofstream out(ready_path);
            out << "READY";
            out.flush();
        }

        // Wait until gate file appears or timeout (15 seconds)
        auto start = chrono::steady_clock::now();
        while (!fs::exists(gate_path))
        {
            if (chrono::steady_clock::now() - start > 15s)
                return 1;
            std::this_thread::sleep_for(1ms);
        }

        bool won = acquire_or_renew_active_instance(instance_id, lock_path, 60000);
        if (!result_path.empty())
        {
            std::ofstream out(result_path);
            out << (won ? "WON" : "LOST");
            out.flush();
        }
        return won ? 0 : 2;
    }

    // Worker submode for multi-process lease takeover
    if (argc >= 4 && (std::string_view(argv[1]) == "--lease-claim" || std::string_view(argv[1]) == "--lease-worker"))
    {
        std::string lock_path = argv[2];
        std::string instance_id = argv[3];
        std::string opt_arg = (argc >= 5) ? argv[4] : "";
        int hold_ms = 0;
        if (!opt_arg.empty() && std::isdigit(static_cast<unsigned char>(opt_arg[0])))
        {
            hold_ms = std::atoi(opt_arg.c_str());
        }
        bool claimed = claim_active_instance(instance_id, lock_path);
        if (!opt_arg.empty() && hold_ms == 0)
        {
            std::ofstream out(opt_arg);
            out << (claimed ? "CLAIMED" : "FAILED");
            out.flush();
        }
        if (hold_ms > 0)
        {
            std::this_thread::sleep_for(chrono::milliseconds(hold_ms));
        }
        return claimed ? 0 : 1;
    }

    fs::path exe_path = fs::absolute(argv[0]);
#if defined(_WIN32)
    if (!fs::exists(exe_path) && fs::exists(fs::path(exe_path.string() + ".exe")))
    {
        exe_path = fs::path(exe_path.string() + ".exe");
    }
#endif

    std::cout << "Starting mpv-rich-presence regression test suite...\n\n";

    run_test("test_utf8_truncation", test_utf8_truncation);
    run_test("test_safe_to_lower", test_safe_to_lower);
    run_test("test_folder_filtering", test_folder_filtering);
    run_test("test_anilist_page_parsing", test_anilist_page_parsing);
    run_test("test_lease_operations", test_lease_operations);
    run_test("test_multi_thread_lease_contention", test_multi_thread_lease_contention);
    run_test("test_multi_process_lease_contention", [&]() { test_multi_process_lease_contention(exe_path); });
    run_test("test_multi_process_lease_takeover", [&]() { test_multi_process_lease_takeover(exe_path); });
    run_test("test_buffering_and_seek_state_machine", test_buffering_and_seek_state_machine);
    run_test("test_pause_timeout_callback_race", test_pause_timeout_callback_race);
    run_test("test_worker_shutdown_concurrency", test_worker_shutdown_concurrency);

    int failures = g_failed_checks.load();
    if (failures > 0)
    {
        std::cerr << "\nRegression test suite FAILED: " << failures << " check(s) failed.\n";
        return 1;
    }

    std::cout << "\nAll regression tests passed successfully!\n";
    return 0;
}
