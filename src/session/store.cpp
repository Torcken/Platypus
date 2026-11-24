// Persistent session storage uses prepared statements so model text remains data.
// SQLite owns transactional durability; errors cross the module boundary through Result.
#include "store.hpp"
#include <sqlite3.h>
#include <stdexcept>
namespace platypus::session {
namespace {
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
Statement prepare(sqlite3 *database, const char *sql) {
    sqlite3_stmt *statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(database));
    return {statement, sqlite3_finalize};
}
void bind_text(sqlite3_stmt *statement, int index, const std::string &text) {
    if (sqlite3_bind_text(statement, index, text.data(), static_cast<int>(text.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK)
        throw std::runtime_error("Cannot bind SQLite text");
}
std::string text(sqlite3_stmt *statement, int column) {
    const auto *value = sqlite3_column_text(statement, column);
    return value ? std::string(reinterpret_cast<const char *>(value),
                               sqlite3_column_bytes(statement, column))
                 : "";
}
} // namespace
Store::Store(const std::filesystem::path &root) {
    auto directory = root / ".platypus";
    auto file = directory / "sessions.db";
    if (std::filesystem::is_symlink(directory) || std::filesystem::is_symlink(file))
        throw std::runtime_error("Session storage cannot be a symlink");
    std::filesystem::create_directories(directory);
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
    if (sqlite3_open_v2(file.c_str(), &database_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        auto error = std::string(sqlite3_errmsg(database_));
        sqlite3_close(database_);
        database_ = nullptr;
        throw std::runtime_error(error);
    }
    const char *schema =
        "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL;"
        "CREATE TABLE IF NOT EXISTS sessions(id INTEGER PRIMARY KEY, title TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS turns(id INTEGER PRIMARY KEY, session_id INTEGER NOT NULL "
        "REFERENCES sessions(id), role TEXT NOT NULL, content TEXT NOT NULL);"
        "CREATE TRIGGER IF NOT EXISTS immutable_turn_update BEFORE UPDATE ON turns BEGIN SELECT "
        "RAISE(ABORT, 'append-only log'); END;"
        "CREATE TRIGGER IF NOT EXISTS immutable_turn_delete BEFORE DELETE ON turns BEGIN SELECT "
        "RAISE(ABORT, 'append-only log'); END;";
    if (sqlite3_exec(database_, schema, nullptr, nullptr, nullptr) != SQLITE_OK) {
        auto error = std::string(sqlite3_errmsg(database_));
        sqlite3_close(database_);
        database_ = nullptr;
        throw std::runtime_error(error);
    }
}
Store::~Store() { sqlite3_close(database_); }
util::Result<std::string> Store::create(const std::string &title) {
    try {
        auto statement = prepare(database_, "INSERT INTO sessions(title) VALUES(?)");
        bind_text(statement.get(), 1, title);
        if (sqlite3_step(statement.get()) != SQLITE_DONE)
            return util::Result<std::string>::fail(sqlite3_errmsg(database_));
        return util::Result<std::string>::ok(std::to_string(sqlite3_last_insert_rowid(database_)));
    } catch (const std::exception &e) {
        return util::Result<std::string>::fail(e.what());
    }
}
util::Result<bool> Store::append(const std::string &id, const std::string &role,
                                 const std::string &content) {
    try {
        auto statement =
            prepare(database_, "INSERT INTO turns(session_id,role,content) VALUES(?,?,?)");
        bind_text(statement.get(), 1, id);
        bind_text(statement.get(), 2, role);
        bind_text(statement.get(), 3, content);
        if (sqlite3_step(statement.get()) != SQLITE_DONE)
            return util::Result<bool>::fail(sqlite3_errmsg(database_));
        return util::Result<bool>::ok(true);
    } catch (const std::exception &e) {
        return util::Result<bool>::fail(e.what());
    }
}
util::Result<std::pair<std::string, History>> Store::load(const std::string &id) {
    using Return = util::Result<std::pair<std::string, History>>;
    try {
        auto selected = id;
        if (selected.empty()) {
            auto statement = prepare(database_, "SELECT id FROM sessions ORDER BY id DESC LIMIT 1");
            if (sqlite3_step(statement.get()) != SQLITE_ROW)
                return Return::fail("No saved session");
            selected = text(statement.get(), 0);
        }
        auto exists = prepare(database_, "SELECT id FROM sessions WHERE id=?");
        bind_text(exists.get(), 1, selected);
        if (sqlite3_step(exists.get()) != SQLITE_ROW)
            return Return::fail("Unknown session: " + selected);
        auto statement =
            prepare(database_, "SELECT role,content FROM turns WHERE session_id=? ORDER BY id");
        bind_text(statement.get(), 1, selected);
        History history;
        int code;
        while ((code = sqlite3_step(statement.get())) == SQLITE_ROW)
            history.emplace_back(text(statement.get(), 0), text(statement.get(), 1));
        if (code != SQLITE_DONE)
            return Return::fail(sqlite3_errmsg(database_));
        return Return::ok({selected, history});
    } catch (const std::exception &e) {
        return Return::fail(e.what());
    }
}
} // namespace platypus::session
