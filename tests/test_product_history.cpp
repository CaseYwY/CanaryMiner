// Transactional local history: synthetic counters/timestamps, no GPU or external network.
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

#include "../src/product/supervisor.h"
#include "product_test_util.h"

using namespace om;
static constexpr int64_t kNow = 1700000000;
static std::string bytes(const std::filesystem::path &p) {
    std::ifstream input(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

static void counters(const std::filesystem::path &path) {
    std::string error;
    {
        AcceptedHistoryStore store;
        CHECK(store.open(path, kNow, &error));
        AcceptedHistoryView value;
        CHECK(store.snapshot(kNow, &value, &error) && value.ready && value.all == 0);
        CHECK(store.observe(0, 1, 2, kNow + 1, &error));
        CHECK(store.observe(0, 1, 2, kNow + 2, &error));
        CHECK(store.observe(0, 1, 5, kNow + 3, &error));
        CHECK(store.observe(1, 1, 3, kNow + 4, &error));
        CHECK(store.observe(0, 2, 4, kNow + 5, &error));
        CHECK(store.snapshot(kNow + 5, &value, &error) && value.all == 12 && value.day == 12 && !value.gaps);
        CHECK(store.observe(0, 1, 999, kNow + 6, &error));
        CHECK(store.observe(0, 2, 3, kNow + 7, &error));
        CHECK(store.observe(0, 2, 6, kNow + 8, &error));
        CHECK(store.snapshot(kNow + 8, &value, &error) && value.all == 14 && value.gaps && !value.clockAdjusted);
        AcceptedHistoryStore other;
        CHECK(!other.open(path, kNow + 8, &error));
        CHECK(store.finish(kNow + 10, &error));
        CHECK(!store.observe(0, 3, 4, kNow + 11, &error));
    }
    {
        AcceptedHistoryStore store;
        CHECK(store.open(path, kNow + 11, &error));
        CHECK(store.observe(0, 1, 7, kNow + AcceptedHistoryStore::kRetention + 20, &error));
        CHECK(store.maintain(kNow + AcceptedHistoryStore::kRetention + 20, &error));
        AcceptedHistoryView value;
        CHECK(store.snapshot(kNow + AcceptedHistoryStore::kRetention + 20, &value, &error));
        CHECK(value.all == 21 && value.day == 7 && value.week == 7 && value.month == 7);
        CHECK(store.finish(kNow + AcceptedHistoryStore::kRetention + 21, &error));
    }
}

static void windows(const std::filesystem::path &path) {
    AcceptedHistoryStore store;
    std::string error;
    CHECK(store.open(path, kNow, &error));
    // kNow is :20, so +39/:59, +40/:00 and +41/:01 cross a minute-summary boundary.
    CHECK(store.observe(0, 1, 2, kNow + 39, &error));
    CHECK(store.observe(0, 1, 5, kNow + 40, &error));
    CHECK(store.observe(0, 1, 9, kNow + 41, &error));
    AcceptedHistoryView value;
    CHECK(store.snapshot(kNow + 86400, &value, &error) && value.day == 9);
    CHECK(store.snapshot(kNow + 39 + 86400, &value, &error) && value.day == 7);
    CHECK(store.snapshot(kNow + 40 + 86400, &value, &error) && value.day == 4);
    CHECK(store.snapshot(kNow + 41 + 86400, &value, &error) && value.day == 0 && value.week == 9);
    CHECK(store.markGap(&error, 0, kNow + 45));
    CHECK(store.snapshot(kNow + 50, &value, &error) && value.gaps && value.gapFromUtc == kNow + 45 && value.gapToUtc == kNow + 50);
    CHECK(store.observe(0, 1, 9, kNow + 55, &error));  // unchanged counter closes a known outage without adding income/counts
    CHECK(store.snapshot(kNow + 56, &value, &error) && value.all == 9 && value.gapToUtc == kNow + 55);
    CHECK(store.snapshot(kNow + 40 + 7 * 86400, &value, &error) && value.week == 4 && value.month == 9);
    CHECK(store.maintain(kNow + 40 + AcceptedHistoryStore::kRetention, &error));
    CHECK(store.snapshot(kNow + 40 + AcceptedHistoryStore::kRetention, &value, &error) && value.month == 4 && value.all == 9);
    CHECK(store.maintain(kNow + 41 + AcceptedHistoryStore::kRetention, &error));
    CHECK(store.snapshot(kNow + 41 + AcceptedHistoryStore::kRetention, &value, &error) && value.month == 0 && value.all == 9);
    CHECK(store.snapshot(kNow + 40 + AcceptedHistoryStore::kRetention, &value, &error) && value.clockAdjusted);
    CHECK(store.finish(kNow + 42 + AcceptedHistoryStore::kRetention, &error));
}

static void limitsAndClock(const std::filesystem::path &path) {
    AcceptedHistoryStore store;
    std::string error;
    CHECK(store.open(path, kNow, &error));
    CHECK(store.observe(0, 1, 5, kNow + 20, &error));
    CHECK(store.observe(0, 1, 7, kNow + 10, &error));
    AcceptedHistoryView value;
    CHECK(store.snapshot(kNow + 10, &value, &error) && value.clockAdjusted && value.all == 7);
    CHECK(!store.observe(0, 0, 1, kNow + 21, &error));
    CHECK(!store.observe(0, 1, std::numeric_limits<uint64_t>::max(), kNow + 21, &error));
    CHECK(!store.snapshot(0, &value, &error));
    const auto maximum = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    CHECK(store.observe(0, 1, maximum, kNow + 22, &error));
    CHECK(!store.observe(0, 2, 1, kNow + 23, &error));
    CHECK(store.snapshot(kNow + 23, &value, &error) && value.all == maximum);
    CHECK(store.finish(kNow + 23, &error));
}

static void idleClock(const std::filesystem::path &path) {
    AcceptedHistoryStore store;
    std::string error;
    AcceptedHistoryView value;
    CHECK(store.open(path, kNow, &error));
    CHECK(store.heartbeat(kNow + 20, &error));
    CHECK(store.observe(0, 1, 0, kNow + 19, &error));  // queued before a later persistence heartbeat
    CHECK(store.snapshot(kNow + 20, &value, &error) && !value.clockAdjusted);
    CHECK(store.snapshot(kNow + 10, &value, &error) && value.clockAdjusted && value.all == 0);
    CHECK(store.heartbeat(kNow + 10, &error));
    CHECK(store.snapshot(kNow + 30, &value, &error) && value.clockAdjusted);
    CHECK(store.finish(kNow + 30, &error));
}

static void foreignFile(const std::filesystem::path &path) {
    const std::string original = "synthetic non-database file; preserve me";
    { std::ofstream output(path, std::ios::binary); output << original; }
    {
        AcceptedHistoryStore store;
        std::string error;
        CHECK(!store.open(path, kNow, &error));
        CHECK(!error.empty());
    }
    CHECK(bytes(path) == original);
}

static void unsupportedAndCorrupt(const std::filesystem::path &path, bool corrupt) {
    {
        AcceptedHistoryStore store;
        std::string error;
        CHECK(store.open(path, kNow, &error));
        CHECK(store.observe(0, 1, 5, kNow + 1, &error));
        CHECK(store.finish(kNow + 2, &error));
    }
    if (corrupt) std::filesystem::resize_file(path, 4096);  // retains header identity but truncates the tables
    else {
        SqliteRuntime api;
        SqliteDb *db = nullptr;
        CHECK(api.load() && api.openV2(path.u8string().c_str(), &db, 2, nullptr) == 0);
        CHECK(api.execute(db, "PRAGMA user_version=99", nullptr, nullptr, nullptr) == 0);
        CHECK(api.closeV2(db) == 0);
    }
    const auto original = bytes(path);
    {
        AcceptedHistoryStore store;
        std::string error;
        CHECK(!store.open(path, kNow + 3, &error));
        CHECK(!error.empty());
    }
    CHECK(bytes(path) == original);
}

static void competingWriter(const std::filesystem::path &path) {
    AcceptedHistoryStore store;
    std::string error;
    CHECK(store.open(path, kNow, &error));
    SqliteRuntime api;
    SqliteDb *db = nullptr;
    CHECK(api.load() && api.openV2(path.u8string().c_str(), &db, 2, nullptr) == 0);
    CHECK(api.execute(db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) == 0);
    CHECK(!store.observe(0, 1, 5, kNow + 1, &error));
    CHECK(api.execute(db, "ROLLBACK", nullptr, nullptr, nullptr) == 0);
    CHECK(store.observe(0, 1, 5, kNow + 2, &error));
    AcceptedHistoryView value;
    CHECK(store.snapshot(kNow + 2, &value, &error) && value.all == 5);
    CHECK(api.closeV2(db) == 0);
    CHECK(store.finish(kNow + 3, &error));
}

static void windowOracle(const std::filesystem::path &path) {
    AcceptedHistoryStore store;
    std::string error;
    CHECK(store.open(path, kNow, &error));
    std::vector<int64_t> instants;
    for (int i = 0; i < 120; ++i) {
        const int64_t instant = kNow + i * 28349 + i % 7;
        instants.push_back(instant);
        CHECK(store.observe(0, 1, instants.size(), instant, &error));
    }
    for (int offset : {0, 1, 39, 40, 41, 59, 60, 61, 86399, 86400, 604800, 2592000}) {
        const auto utc = instants.back() + offset;
        uint64_t expected[3]{};
        const int64_t durations[] = {86400, 604800, 2592000};
        for (const auto instant : instants) for (int i = 0; i < 3; ++i) if (utc - durations[i] < instant && instant <= utc) ++expected[i];
        AcceptedHistoryView value;
        CHECK(store.snapshot(utc, &value, &error));
        CHECK(value.day == expected[0] && value.week == expected[1] && value.month == expected[2] && value.all == instants.size());
    }
    CHECK(store.finish(instants.back() + 1, &error));
}

static void strictCounter() {
    JsonValue root;
    std::string error;
    uint64_t count = 42;
    StatsSnapshot source;
    source.devices.push_back(StatsDevice{});
    source.devices[0].outcomes.accepted = 5;
    source.fee.outcomes.accepted = 999;
    CHECK(parseJsonStrict(statsToJson(source), &root, &error));
    CHECK(acceptedHistoryCounter(root, &count) && count == 5);
    for (const auto &value : {"null", "-1", "1.5", "1e2", "\"5\"", "18446744073709551616"}) {
        CHECK(parseJsonStrict(std::string("{\"schema\":\"canaryminer-stats-v1\",\"devices\":[{\"outcomes\":{\"accepted\":") + value + "}}]}", &root, &error));
        CHECK(!acceptedHistoryCounter(root, &count));
    }
    CHECK(parseJsonStrict("{\"schema\":\"canaryminer-stats-v1\",\"devices\":[{\"outcomes\":{}}]}", &root, &error));
    CHECK(!acceptedHistoryCounter(root, &count));
}

static bool child(const char *mode, const std::filesystem::path &path) {
    ChildProcess process;
    std::string error;
    if (!process.start(selfExecutablePath(), {mode, path.u8string()}, [](const std::string &) {}, &error)) return false;
    int code = -1;
    const bool finished = process.waitFor(15000, &code);
    process.joinReader();
    return finished && code == 0;
}

static void recovery(const std::filesystem::path &path) {
    CHECK(child("--commit-and-crash", path));
    {
        AcceptedHistoryStore store;
        std::string error;
        CHECK(store.open(path, kNow + 3, &error));
        AcceptedHistoryView value;
        CHECK(store.snapshot(kNow + 3, &value, &error) && value.all == 5 && value.gaps);
        CHECK(store.finish(kNow + 4, &error));
    }
    CHECK(child("--uncommitted-crash", path));
    {
        AcceptedHistoryStore store;
        std::string error;
        CHECK(store.open(path, kNow + 5, &error));
        AcceptedHistoryView value;
        CHECK(store.snapshot(kNow + 5, &value, &error) && value.all == 5);
        CHECK(store.finish(kNow + 6, &error));
    }
}

static void asynchronous(const std::filesystem::path &path) {
    AcceptedHistory history(path);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!history.snapshot().ready && history.snapshot().error.empty() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK(history.snapshot().ready);
    const auto utc = static_cast<int64_t>(time(nullptr));
    history.observe(0, 1, 5, utc);
    history.observe(0, 1, 5, utc);
    history.stop();
    CHECK(history.snapshot().ready && history.snapshot().all == 5);
    AcceptedHistoryStore store;
    std::string error;
    CHECK(store.open(path, utc + 1, &error));
    AcceptedHistoryView value;
    CHECK(store.snapshot(utc + 1, &value, &error) && value.all == 5 && value.gaps && value.gapFromUtc == static_cast<uint64_t>(utc));
    CHECK(store.finish(utc + 2, &error));
}

static void rendering() {
    DashModel model;
    model.history.enabled = model.history.ready = true;
    model.history.all = 8;
    model.history.day = 2;
    model.history.week = 4;
    model.history.month = 8;
    model.history.startedUtc = kNow;
    model.history.asOfUtc = kNow + 60;
    model.donation.accepted = 999;
    DashView view;
    view.columns = 120;
    auto frame = renderDashboard(model, view);
    CHECK(frame.find("24h 2 | 7d 4 | 30d 8 | all local 8") != std::string::npos);
    CHECK(frame.find("partial span") != std::string::npos);
    CHECK(frame.find("donor shares excluded") != std::string::npos);
    CHECK(renderPlainSummary(model, kNow).find("all local 8") != std::string::npos);
    model.history.clockAdjusted = true;
    frame = renderDashboard(model, view);
    CHECK(frame.find("24h N/A | 7d N/A | 30d N/A | all local 8") != std::string::npos);
    model.history.ready = false;
    model.history.error = "synthetic history failure";
    frame = renderDashboard(model, view);
    CHECK(frame.find("N/A: synthetic history failure") != std::string::npos);
    CHECK(frame.find("all local 0") == std::string::npos);
    CHECK(renderPlainSummary(model, kNow).find("history N/A") != std::string::npos);
}

int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--commit-and-crash") {
        AcceptedHistoryStore store;
        std::string error;
        if (!store.open(std::filesystem::u8path(argv[2]), kNow, &error) || !store.observe(0, 1, 5, kNow + 1, &error)) return 77;
        std::_Exit(0);
    }
    if (argc == 3 && std::string(argv[1]) == "--uncommitted-crash") {
        SqliteRuntime api;
        SqliteDb *db = nullptr;
        if (!api.load() || api.openV2(argv[2], &db, 2, nullptr) != 0 ||
            api.execute(db, "BEGIN IMMEDIATE; UPDATE meta SET total=total+123;", nullptr, nullptr, nullptr) != 0) return 78;
        std::_Exit(0);
    }
    const auto dir = std::filesystem::path(makeScratchDir("accepted-history"));
    counters(dir / "counters.sqlite3");
    windows(dir / "windows.sqlite3");
    limitsAndClock(dir / "limits.sqlite3");
    idleClock(dir / "idle-clock.sqlite3");
    foreignFile(dir / "foreign.sqlite3");
    unsupportedAndCorrupt(dir / "newer.sqlite3", false);
    unsupportedAndCorrupt(dir / "corrupt.sqlite3", true);
    competingWriter(dir / "locked.sqlite3");
    windowOracle(dir / "oracle.sqlite3");
    strictCounter();
    counters(dir / std::filesystem::u8path(u8"合成-history.sqlite3"));
    recovery(dir / "recovery.sqlite3");
    asynchronous(dir / "async.sqlite3");
    rendering();
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);  // only this test's newly created, isolated scratch directory
    return finishTests("product_history");
}
