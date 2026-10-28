// Transactional, bounded local accepted history; optional system SQLite, no mining/network operations.
#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "accepted_history_view.h"
#include "sqlite_runtime.h"
#include "stats.h"

namespace om {

// General display parsing is permissive; a durable fact must not default a missing counter to zero.
inline bool acceptedHistoryCounter(const JsonValue &root, uint64_t *count) {
    const auto *schema = root.find("schema"), *devices = root.find("devices");
    if (!schema || !schema->isString() || schema->text != kStatsSchemaId || !devices || !devices->isArray() || devices->items.size() != 1)
        return false;
    const auto *outcomes = devices->items[0].find("outcomes");
    const auto *accepted = outcomes && outcomes->isObject() ? outcomes->find("accepted") : nullptr;
    return accepted && accepted->asUint64(count);
}

class HistoryLease {
   public:
    ~HistoryLease() {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
        if (fd_ >= 0) ::close(fd_);
#endif
    }
    bool acquire(const std::filesystem::path &path) {
#if defined(_WIN32)
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION info{};
        return GetFileType(handle_) == FILE_TYPE_DISK && GetFileInformationByHandle(handle_, &info) &&
               !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
#else
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        struct stat info {};
        return fd_ >= 0 && fstat(fd_, &info) == 0 && S_ISREG(info.st_mode) && flock(fd_, LOCK_EX | LOCK_NB) == 0;
#endif
    }
   private:
#if defined(_WIN32)
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

class AcceptedHistoryStore {
   public:
    static constexpr int64_t kRetention = 30 * 86400;
    AcceptedHistoryStore() = default;
    AcceptedHistoryStore(const AcceptedHistoryStore &) = delete;
    AcceptedHistoryStore &operator=(const AcceptedHistoryStore &) = delete;
    ~AcceptedHistoryStore() { if (db_) api_.closeV2(db_); }

    bool open(const std::filesystem::path &path, int64_t utc, std::string *error) {
        if (db_ || !validUtc(utc)) return fail(error, "invalid history open request");
        if (!api_.load()) return fail(error, "system SQLite unavailable/too old; history not initialized");
        std::error_code ec;
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return fail(error, "cannot create private history directory");
        auto lock = path;
        lock += ".lock";
        if (!lease_.acquire(lock)) return fail(error, "history already in use or lock is not a regular file");
        const auto status = std::filesystem::symlink_status(path, ec);
        const bool absent = ec == std::errc::no_such_file_or_directory || (!ec && status.type() == std::filesystem::file_type::not_found);
        if (!absent && (ec || !std::filesystem::is_regular_file(status))) return fail(error, "history must be a regular local file");
        if (absent) {
#if defined(_WIN32)
            HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return fail(error, "cannot create history file");
            CloseHandle(file);
#else
            const int file = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (file < 0) return fail(error, "cannot create history file");
            ::close(file);
#endif
        }
        // READWRITE only: CREATE is deliberately excluded after the exclusive local creation.
        const auto absolute = std::filesystem::absolute(path, ec);
        if (ec || api_.openV2(absolute.u8string().c_str(), &db_, 2 | 0x10000 | 0x1000000, nullptr) != 0) return fail(error, "cannot open history database");
        api_.busyTimeout(db_, 0);  // a competing DB writer never stalls the session loop
        int64_t app = 0, version = 0;
        if (!scalar("PRAGMA application_id", {}, &app) || !scalar("PRAGMA user_version", {}, &version)) return databaseFailure(error);
        if (!absent && (app != kApplication || version != 1)) return fail(error, "unrecognized/newer history database; left unchanged");
        if (!exec("PRAGMA trusted_schema=OFF; PRAGMA foreign_keys=ON; PRAGMA temp_store=MEMORY; PRAGMA cache_size=-2048;")) return databaseFailure(error);
        if (!absent && !textIs("PRAGMA quick_check(1)", "ok")) return fail(error, "history integrity check failed; file preserved");
        if (!textIs("PRAGMA journal_mode=WAL", "wal") || !exec("PRAGMA synchronous=FULL; PRAGMA wal_autocheckpoint=256; PRAGMA journal_size_limit=1048576;"))
            return databaseFailure(error);
        int64_t pageSize = 0;
        if (!scalar("PRAGMA page_size", {}, &pageSize) || pageSize < 512 || pageSize > 65536 ||
            !exec(("PRAGMA max_page_count=" + std::to_string(536870912 / pageSize)).c_str())) return databaseFailure(error);
        path_ = absolute;
        if (absent) {
            if (!exec("BEGIN IMMEDIATE; CREATE TABLE meta(id INTEGER PRIMARY KEY CHECK(id=1), started INTEGER NOT NULL, last INTEGER NOT NULL, seen INTEGER NOT NULL, total INTEGER NOT NULL CHECK(total>=0), gaps INTEGER NOT NULL, clock INTEGER NOT NULL);"
                      "CREATE TABLE sessions(id INTEGER PRIMARY KEY AUTOINCREMENT, started INTEGER NOT NULL, last_seen INTEGER NOT NULL, ended INTEGER);"
                      "CREATE TABLE sources(session INTEGER NOT NULL, slot INTEGER NOT NULL, generation INTEGER NOT NULL, counter INTEGER NOT NULL CHECK(counter>=0), PRIMARY KEY(session,slot));"
                      "CREATE TABLE buckets(utc INTEGER PRIMARY KEY, accepted INTEGER NOT NULL CHECK(accepted>0));"
                      "CREATE TABLE minutes(utc INTEGER PRIMARY KEY, accepted INTEGER NOT NULL CHECK(accepted>0));"
                      "CREATE TABLE gaps(session INTEGER NOT NULL, slot INTEGER NOT NULL, since INTEGER NOT NULL, until INTEGER);"
                      "CREATE INDEX gaps_since ON gaps(since); CREATE INDEX gaps_source ON gaps(session,slot,until);")) return databaseFailure(error);
            if (!run("INSERT INTO meta VALUES(1,?1,?1,?1,0,0,0)", {utc}) ||
                !exec(("PRAGMA application_id=" + std::to_string(kApplication) + "; PRAGMA user_version=1; COMMIT;").c_str())) return databaseFailure(error);
        }
        if (!exec("BEGIN IMMEDIATE") || !exec("UPDATE meta SET gaps=1 WHERE EXISTS(SELECT 1 FROM sessions WHERE ended IS NULL)") ||
            !run("UPDATE gaps SET until=MAX(since,?1) WHERE until IS NULL", {utc}) ||
            !run("INSERT INTO gaps SELECT id,-1,last_seen,MAX(last_seen,?1) FROM sessions WHERE ended IS NULL", {utc}) ||
            !run("INSERT INTO gaps SELECT id,-2,ended,?1 FROM sessions WHERE id=(SELECT MAX(id) FROM sessions) AND ended IS NOT NULL AND ended<?1", {utc}) ||
            !exec("UPDATE meta SET gaps=1 WHERE EXISTS(SELECT 1 FROM gaps)") ||
            !run("UPDATE sessions SET ended=MAX(last_seen,?1) WHERE ended IS NULL", {utc}) ||
            !run("UPDATE meta SET clock=CASE WHEN seen>?1 THEN 1 ELSE clock END,seen=MAX(seen,?1)", {utc}) ||
            !run("INSERT INTO sessions(started,last_seen) VALUES(?1,?1)", {utc}) ||
            !scalar("SELECT last_insert_rowid()", {}, &session_) || !exec("COMMIT")) return databaseFailure(error);
        return maintain(utc, error);
    }

    bool observe(unsigned slot, uint64_t generation, uint64_t counter, int64_t utc, std::string *error) {
        if (!db_ || closed_ || !generation || generation > limit() || counter > limit() || !validUtc(utc)) return fail(error, "invalid history observation");
        if (!exec("BEGIN IMMEDIATE")) return databaseFailure(error);
        std::vector<int64_t> prior;
        if (!row("SELECT generation,counter FROM sources WHERE session=?1 AND slot=?2", {session_, slot}, 2, &prior)) return databaseFailure(error);
        if (!prior.empty() && (prior.size() != 2 || prior[0] <= 0 || prior[1] < 0)) return databaseFailure(error);
        if (!prior.empty() && (generation < static_cast<uint64_t>(prior[0]) ||
            (generation == static_cast<uint64_t>(prior[0]) && counter < static_cast<uint64_t>(prior[1])))) {
            if (!exec("UPDATE meta SET gaps=1") || !run("INSERT INTO gaps VALUES(?1,?2,?3,?3)", {session_, slot, utc}) || !exec("COMMIT"))
                return databaseFailure(error);
            return true;  // never lower a high-water counter or count a stale generation again
        }
        const uint64_t previous = !prior.empty() && generation == static_cast<uint64_t>(prior[0]) ? static_cast<uint64_t>(prior[1]) : 0;
        if (!prior.empty() && generation == static_cast<uint64_t>(prior[0]) && counter == previous) {
            int64_t gaps = 0, last = 0;
            if (!scalar("SELECT COUNT(*) FROM gaps WHERE session=?1 AND slot=?2 AND until IS NULL", {session_, slot}, &gaps) ||
                !scalar("SELECT last FROM meta WHERE id=1", {}, &last)) return databaseFailure(error);
            if (gaps || utc < last) {
                if (!run("UPDATE gaps SET until=MAX(since,?1) WHERE session=?2 AND slot=?3 AND until IS NULL", {utc, session_, slot}) ||
                    !run("UPDATE meta SET clock=CASE WHEN last>?1 THEN 1 ELSE clock END,seen=MAX(seen,?1)", {utc}) ||
                    !exec("COMMIT")) return databaseFailure(error);
            } else if (!exec("ROLLBACK")) return databaseFailure(error);
            return true;  // repeated telemetry is not a disk write or another accepted event
        }
        const auto delta = counter - previous;
        int64_t last = 0, total = 0;
        if (!scalar("SELECT last FROM meta WHERE id=1", {}, &last) || !scalar("SELECT total FROM meta WHERE id=1", {}, &total) ||
            !validUtc(last) || total < 0) return databaseFailure(error);
        if (delta > limit() - static_cast<uint64_t>(total)) { exec("ROLLBACK"); return fail(error, "history counter capacity exceeded; no wrapped values stored"); }
        if (!run("UPDATE meta SET clock=CASE WHEN last>?1 THEN 1 ELSE clock END,seen=MAX(seen,?1),last=MAX(last,?1),total=total+?2 WHERE id=1", {utc, static_cast<int64_t>(delta)}) ||
            !run("INSERT INTO sources VALUES(?1,?2,?3,?4) ON CONFLICT(session,slot) DO UPDATE SET generation=excluded.generation,counter=excluded.counter",
                 {session_, slot, static_cast<int64_t>(generation), static_cast<int64_t>(counter)})) return databaseFailure(error);
        if (delta && !run("INSERT INTO buckets VALUES(?1,?2) ON CONFLICT(utc) DO UPDATE SET accepted=accepted+excluded.accepted",
                          {std::max(last, utc), static_cast<int64_t>(delta)})) return databaseFailure(error);
        if (delta && !run("INSERT INTO minutes VALUES(?1,?2) ON CONFLICT(utc) DO UPDATE SET accepted=accepted+excluded.accepted",
                          {std::max(last, utc) / 60 * 60, static_cast<int64_t>(delta)})) return databaseFailure(error);
        if (!run("UPDATE gaps SET until=MAX(since,?1) WHERE session=?2 AND slot=?3 AND until IS NULL", {utc, session_, slot}) || !exec("COMMIT"))
            return databaseFailure(error);
        return true;
    }

    bool markGap(std::string *error, unsigned slot = 0, int64_t since = 0) {
        if (!db_ || closed_) return fail(error, "history session is not open");
        if (!since && !scalar("SELECT last FROM meta WHERE id=1", {}, &since)) return databaseFailure(error);
        if (!validUtc(since)) return fail(error, "invalid history gap time");
        if (!exec("BEGIN IMMEDIATE") || !exec("UPDATE meta SET gaps=1 WHERE id=1") ||
            !run("INSERT INTO gaps SELECT ?1,?2,?3,NULL WHERE NOT EXISTS(SELECT 1 FROM gaps WHERE session=?1 AND slot=?2 AND until IS NULL)",
                 {session_, slot, since}) || !exec("COMMIT")) return databaseFailure(error);
        return true;
    }

    bool markClock(std::string *error) { return db_ && !closed_ && exec("UPDATE meta SET clock=1 WHERE id=1") ? true : databaseFailure(error); }
    bool heartbeat(int64_t utc, std::string *error) {
        if (!db_ || closed_ || !validUtc(utc)) return fail(error, "invalid history heartbeat");
        if (!exec("BEGIN IMMEDIATE") || !run("UPDATE sessions SET last_seen=MAX(last_seen,?1) WHERE id=?2", {utc, session_}) ||
            !run("UPDATE meta SET clock=CASE WHEN seen>?1 THEN 1 ELSE clock END,seen=MAX(seen,?1)", {utc}) || !exec("COMMIT"))
            return databaseFailure(error);
        return true;
    }

    bool maintain(int64_t utc, std::string *error) {
        if (!db_ || closed_ || !validUtc(utc)) return fail(error, "invalid history maintenance");
        if (!exec("BEGIN IMMEDIATE") ||
            !run("DELETE FROM buckets WHERE utc<=?1", {utc - kRetention}) ||
            !run("DELETE FROM minutes WHERE utc<?1", {(utc - kRetention) / 60 * 60}) ||
            !run("DELETE FROM gaps WHERE until IS NOT NULL AND until<=?1", {utc - kRetention}) ||
            !run("DELETE FROM sources WHERE session IN (SELECT id FROM sessions WHERE ended IS NOT NULL AND ended<=?1)", {utc - kRetention}) ||
            !run("DELETE FROM sessions WHERE ended IS NOT NULL AND ended<=?1", {utc - kRetention}) ||
            !run("UPDATE meta SET clock=CASE WHEN seen>?1 THEN 1 ELSE clock END,seen=MAX(seen,?1) WHERE id=1", {utc}) || !exec("COMMIT"))
            return databaseFailure(error);
        return true;
    }

    bool snapshot(int64_t utc, AcceptedHistoryView *view, std::string *error) {
        if (!db_ || !validUtc(utc)) return fail(error, "invalid history snapshot time");
        std::error_code ec;
        auto wal = path_;
        wal += "-wal";
        const auto size = std::filesystem::file_size(wal, ec);
        if (!ec && size > 16 * 1024 * 1024) return fail(error, "history WAL capacity exceeded; release external readers before restarting");
        AcceptedHistoryView result;
        result.enabled = true;
        std::vector<int64_t> meta;
        if (!row("SELECT started,seen,total,gaps,clock FROM meta WHERE id=1", {}, 5, &meta) || meta.size() != 5 ||
            !validUtc(meta[0]) || !validUtc(meta[1]) || meta[2] < 0 || meta[3] < 0 || meta[3] > 1 || meta[4] < 0 || meta[4] > 1)
            return databaseFailure(error);
        int64_t day = 0, week = 0, month = 0;
        if (!window(utc, 86400, &day) || !window(utc, 7 * 86400, &week) || !window(utc, kRetention, &month) ||
            day < 0 || day > week || week > month || month > meta[2]) return databaseFailure(error);
        result.ready = true;
        result.startedUtc = static_cast<uint64_t>(meta[0]);
        result.asOfUtc = static_cast<uint64_t>(utc);
        result.all = static_cast<uint64_t>(meta[2]);
        result.day = static_cast<uint64_t>(day);
        result.week = static_cast<uint64_t>(week);
        result.month = static_cast<uint64_t>(month);
        result.gaps = meta[3] != 0;
        result.clockAdjusted = meta[4] != 0 || utc < meta[1];
        std::vector<int64_t> gap;
        if (!row("SELECT since,COALESCE(until,?1) FROM gaps ORDER BY since DESC LIMIT 1", {utc}, 2, &gap)) return databaseFailure(error);
        if (gap.size() == 2) {
            if (!validUtc(gap[0]) || !validUtc(gap[1])) return databaseFailure(error);
            result.gapFromUtc = static_cast<uint64_t>(gap[0]);
            result.gapToUtc = static_cast<uint64_t>(std::max(gap[0], gap[1]));
        }
        *view = result;
        return true;
    }

    bool finish(int64_t utc, std::string *error) {
        if (closed_) return true;
        if (!validUtc(utc) || !exec("BEGIN IMMEDIATE") ||
            !run("UPDATE gaps SET until=MAX(since,?1) WHERE session=?2 AND until IS NULL", {utc, session_}) ||
            !run("UPDATE meta SET clock=CASE WHEN seen>?1 THEN 1 ELSE clock END,seen=MAX(seen,?1)", {utc}) ||
            !run("UPDATE sessions SET ended=MAX(started,?1),last_seen=MAX(last_seen,?1) WHERE id=?2", {utc, session_}) || !exec("COMMIT"))
            return databaseFailure(error);
        closed_ = true;
        return true;
    }

   private:
    static constexpr int64_t kApplication = 0x43415259;
    SqliteRuntime api_;  // unloaded last, after connection/statement cleanup
    HistoryLease lease_;
    SqliteDb *db_ = nullptr;
    std::filesystem::path path_;
    int64_t session_ = 0;
    bool closed_ = false;
    static uint64_t limit() { return static_cast<uint64_t>(std::numeric_limits<int64_t>::max()); }
    static bool validUtc(int64_t value) { return value > 0 && value <= 253402300799LL; }
    static bool fail(std::string *error, const char *text) { if (error) *error = text; return false; }
    bool databaseFailure(std::string *error) { if (db_) exec("ROLLBACK"); return fail(error, "history database operation failed; records preserved"); }
    bool exec(const char *sql) { return db_ && api_.execute(db_, sql, nullptr, nullptr, nullptr) == 0; }
    bool textIs(const char *sql, const char *expected) {
        SqliteStatement *statement = nullptr;
        if (api_.prepare(db_, sql, -1, &statement, nullptr) != 0) return false;
        const bool stepped = api_.step(statement) == 100;
        const auto *text = stepped ? api_.columnText(statement, 0) : nullptr;
        const bool ok = text && std::string(reinterpret_cast<const char *>(text)) == expected;
        api_.finalize(statement);
        return ok;
    }
    bool row(const char *sql, std::initializer_list<int64_t> values, int columns, std::vector<int64_t> *output) {
        SqliteStatement *statement = nullptr;
        if (!db_ || api_.prepare(db_, sql, -1, &statement, nullptr) != 0) return false;
        struct Final { SqliteRuntime *api; SqliteStatement *value; ~Final() { api->finalize(value); } } final{&api_, statement};
        int index = 1;
        for (auto value : values) if (api_.bindInteger(statement, index++, value) != 0) return false;
        const int code = api_.step(statement);
        output->clear();
        if (code == 101) return true;
        if (code != 100 || columns == 0) return false;
        for (int col = 0; col < columns; ++col) {
            if (api_.columnType(statement, col) != 1) return false;
            output->push_back(api_.columnInteger(statement, col));
        }
        return true;
    }
    bool scalar(const char *sql, std::initializer_list<int64_t> values, int64_t *output) {
        std::vector<int64_t> result;
        if (!row(sql, values, 1, &result) || result.size() != 1) return false;
        *output = result[0];
        return true;
    }
    bool run(const char *sql, std::initializer_list<int64_t> values) { std::vector<int64_t> ignored; return row(sql, values, 0, &ignored); }
    bool window(int64_t utc, int64_t duration, int64_t *result) {
        const int64_t cutoff = utc - duration, firstFull = cutoff / 60 * 60 + 60, lastFull = utc / 60 * 60;
        int64_t middle = 0, lower = 0, upper = 0;
        if (!scalar("SELECT COALESCE(SUM(accepted),0) FROM minutes WHERE utc>=?1 AND utc<?2", {firstFull, lastFull}, &middle) ||
            !scalar("SELECT COALESCE(SUM(accepted),0) FROM buckets WHERE utc>?1 AND utc<?2", {cutoff, firstFull}, &lower) ||
            !scalar("SELECT COALESCE(SUM(accepted),0) FROM buckets WHERE utc>=?1 AND utc<=?2", {lastFull, utc}, &upper)) return false;
        if (middle < 0 || lower < 0 || upper < 0 || middle > std::numeric_limits<int64_t>::max() - lower ||
            middle + lower > std::numeric_limits<int64_t>::max() - upper) return false;
        *result = middle + lower + upper;
        return true;
    }
};

class AcceptedHistory {
   public:
    explicit AcceptedHistory(std::filesystem::path path) : path_(std::move(path)) {
        latest_.enabled = true;
        try { thread_ = std::thread([this] { loop(); }); }
        catch (...) { latest_.error = "history background worker unavailable"; }
    }
    AcceptedHistory(const AcceptedHistory &) = delete;
    AcceptedHistory &operator=(const AcceptedHistory &) = delete;
    ~AcceptedHistory() { stop(); }
    void observe(unsigned slot, uint64_t generation, uint64_t counter, int64_t utc) { enqueue({slot, generation, counter, utc, false}); }
    void markGap(unsigned slot = 0, int64_t sinceUtc = 0) { enqueue({slot, 0, 0, nowUtc(), true, sinceUtc}); }
    AcceptedHistoryView snapshot() const { std::lock_guard<std::mutex> lock(mutex_); return latest_; }
    void stop() {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
        changed_.notify_one();
        if (thread_.joinable()) thread_.join();
    }
   private:
    struct Observation { unsigned slot; uint64_t generation, counter; int64_t utc; bool gap; int64_t sinceUtc = 0; };
    std::filesystem::path path_;
    const int64_t openedUtc_ = nowUtc();
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Observation> queue_;
    AcceptedHistoryView latest_;
    bool stopping_ = false, overflow_ = false;
    std::thread thread_;
    static int64_t nowUtc() { return static_cast<int64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())); }
    void enqueue(Observation value) {
        { std::lock_guard<std::mutex> lock(mutex_);
          if (stopping_) return;
          if (queue_.size() >= 4096) overflow_ = true;
          else queue_.push_back(value); }
        changed_.notify_one();
    }
    void failed(const std::string &error) {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_.ready = false;
        latest_.error = error;
    }
    void loop() {
        try {
            AcceptedHistoryStore store;
            std::string error;
            if (!store.open(path_, openedUtc_, &error)) { failed(error); return; }
            int64_t maintained = nowUtc();
            int64_t heartbeat = maintained;
            auto steady = std::chrono::steady_clock::now();
            auto wall = maintained;
            for (;;) {
                std::deque<Observation> batch;
                bool stopping = false, overflow = false;
                { std::unique_lock<std::mutex> lock(mutex_);
                  changed_.wait_for(lock, std::chrono::seconds(1), [this] { return stopping_ || !queue_.empty() || overflow_; });
                  batch.swap(queue_); stopping = stopping_; overflow = overflow_; }
                if (overflow) { store.markGap(&error); failed("history queue full; coverage incomplete"); return; }
                for (const auto &value : batch) {
                    if (!(value.gap ? store.markGap(&error, value.slot, value.sinceUtc) : store.observe(value.slot, value.generation, value.counter, value.utc, &error))) {
                        failed(error); return;
                    }
                }
                const int64_t utc = nowUtc();
                const auto nextSteady = std::chrono::steady_clock::now();
                const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(nextSteady - steady).count();
                if (std::abs((utc - wall) - elapsed) > 5 && !store.markClock(&error)) { failed(error); return; }
                wall = utc;
                steady = nextSteady;
                if (utc < heartbeat || utc - heartbeat >= 15) {
                    if (!store.heartbeat(utc, &error)) { failed(error); return; }
                    heartbeat = utc;
                }
                if (utc < maintained || utc - maintained >= 600) {
                    if (!store.maintain(utc, &error)) { failed(error); return; }
                    maintained = utc;
                }
                AcceptedHistoryView view;
                if (!store.snapshot(utc, &view, &error)) { failed(error); return; }
                { std::lock_guard<std::mutex> lock(mutex_); latest_ = view; }
                if (stopping) {
                    if (!store.finish(utc, &error)) failed(error);
                    return;
                }
            }
        } catch (...) { failed("history initialization failed; private paths/details omitted"); }
    }
};

}  // namespace om
