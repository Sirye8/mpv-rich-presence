// SPDX-License-Identifier: AGPL-3.0-or-later
#include "mpv_rich_presence/store.hpp"
#include "mpv_rich_presence/string_utils.hpp"
#include <memory>
#include <stdexcept>

using nlohmann::json;
namespace fs = std::filesystem;

namespace
{
    void check(sqlite3* db, int code)
    {
        if (code != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
    }

    using statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
    statement prepare(sqlite3* db, const char* sql, const std::initializer_list<std::string>& args = {})
    {
        sqlite3_stmt* raw = nullptr;
        check(db, sqlite3_prepare_v2(db, sql, -1, &raw, nullptr));
        statement stmt(raw, sqlite3_finalize);
        int i = 0;
        for (const auto& arg : args)
            check(db, sqlite3_bind_text(raw, ++i, arg.c_str(), -1, SQLITE_TRANSIENT));
        return stmt;
    }

    std::string column(sqlite3_stmt* stmt, int index)
    {
        return reinterpret_cast<const char*>(sqlite3_column_text(stmt, index));
    }

    void execute(sqlite3* db, const char* sql, const std::initializer_list<std::string>& args)
    {
        auto stmt = prepare(db, sql, args);
        if (sqlite3_step(stmt.get()) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
    }
}

namespace mpvrp
{
    profile_store::profile_store(const fs::path& path)
    {
        fs::create_directories(path.parent_path());
        const auto utf8 = path.u8string();
        try
        {
            int opened = sqlite3_open_v2(reinterpret_cast<const char*>(utf8.c_str()), &db,
                                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
            check(db, opened);
            sqlite3_busy_timeout(db, 1500);
            auto version = prepare(db, "PRAGMA user_version");
            if (sqlite3_step(version.get()) != SQLITE_ROW || sqlite3_column_int(version.get(), 0) > 1)
                throw std::runtime_error("Unsupported saved data version");
            version.reset();
            check(db, sqlite3_exec(db, "BEGIN IMMEDIATE;"
                "CREATE TABLE IF NOT EXISTS records(category TEXT, scope TEXT, value TEXT NOT NULL, PRIMARY KEY(category,scope));"
                "PRAGMA user_version=1; COMMIT;", nullptr, nullptr, nullptr));
        }
        catch (...) { sqlite3_close(db); throw; }
    }

    profile_store::~profile_store() { sqlite3_close(db); }

    int profile_store::version() const
    {
        auto stmt = prepare(db, "PRAGMA data_version");
        if (sqlite3_step(stmt.get()) != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db));
        return sqlite3_column_int(stmt.get(), 0);
    }

    json profile_store::records() const
    {
        auto result = json::array();
        auto stmt = prepare(db, "SELECT category,scope FROM records WHERE category IN ('appearance','privacy','match','tracking','provider') ORDER BY category,scope");
        int code;
        while ((code = sqlite3_step(stmt.get())) == SQLITE_ROW)
            result.push_back({{"category",column(stmt.get(),0)},{"scope",column(stmt.get(),1)}});
        if (code != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
        return result;
    }

    json profile_store::effective(const std::string& category, const std::string& path) const
    {
        auto result = json::object();
        auto stmt = prepare(db, "SELECT scope,value FROM records WHERE category=? ORDER BY "
                                "CASE WHEN scope='global' THEN 0 WHEN scope LIKE 'folder:%' THEN 1 ELSE 2 END,length(scope)", {category});
        int code;
        while ((code = sqlite3_step(stmt.get())) == SQLITE_ROW)
        {
            auto scope = column(stmt.get(),0);
            if (scope == "global" || (!path.empty() && (scope == "file:" + path ||
                (scope.starts_with("folder:") && path.starts_with(scope.substr(7) + "/")))))
                result.merge_patch(json::parse(column(stmt.get(),1)));
        }
        if (code != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
        return result;
    }

    json profile_store::get(const std::string& category, const std::string& scope) const
    {
        auto stmt = prepare(db, "SELECT value FROM records WHERE category=? AND scope=?", {category,scope});
        const int code = sqlite3_step(stmt.get());
        if (code == SQLITE_ROW) return json::parse(column(stmt.get(), 0));
        if (code != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
        return json::object();
    }

    void profile_store::put(const std::string& category, const std::string& scope, const json& value)
    {
        execute(db, "INSERT INTO records VALUES(?,?,?) ON CONFLICT(category,scope) DO UPDATE SET value=excluded.value",
                {category,scope,value.dump()});
    }

    void profile_store::save(const std::string& category, const std::string& scope, const json& patch)
    {
        execute(db, "INSERT INTO records VALUES(?,?,?) ON CONFLICT(category,scope) DO UPDATE "
                    "SET value=json_patch(value,excluded.value)", {category,scope,patch.dump()});
    }

    void profile_store::clear(const std::string& category, const std::string& scope)
    {
        execute(db, "DELETE FROM records WHERE category=? AND scope=?", {category,scope});
    }
}

