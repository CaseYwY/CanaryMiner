#include "market_hour.h"
#include "json_dom.h"
#include "local_review_input.h"
#include "market_minute_window.h"

#include <chrono>
#include <cstdio>
#include <string_view>
#if defined(_WIN32)
#include <shellapi.h>
#endif

namespace om {
namespace {

constexpr unsigned minutesPerHour = 60;
constexpr uint64_t secondsPerHour = 3600;
constexpr uint64_t maxUtc = 253402300799ULL;
constexpr size_t maxInputBytes = 64 * 1024;
constexpr char pearlSeriesId[] = "gate-pearl-mainnet-usdt-v1";
constexpr char fxSeriesId[] = "kraken-usdt-usd-v1";

struct ClosedMinute {
    uint64_t startUtc = 0;
    ExactDecimal close;
    bool closed = true;
};

struct LocalSeries {
    uint64_t observedUtc = 0;
    std::vector<ClosedMinute> minutes;
};

bool integer(const JsonValue &object, const char *key, uint64_t &value) {
    const auto *member = object.find(key);
    return member && member->asUint64(&value);
}

bool readSeries(const JsonValue &object, const char *expectedSeries, uint64_t start, uint64_t end,
                uint64_t now, LocalSeries &series) {
    const auto *identity = object.find("series");
    const auto *rows = object.find("minutes");
    if (!object.isObject() || object.members.size() != 3 || !identity || !identity->isString() ||
        identity->text != expectedSeries || !integer(object, "observed_utc", series.observedUtc) ||
        series.observedUtc < end || series.observedUtc > now || !rows || !rows->isArray() ||
        rows->items.empty() || rows->items.size() > minutesPerHour)
        return false;

    series.minutes.reserve(rows->items.size());
    uint64_t previous = 0;
    for (const auto &row : rows->items) {
        ClosedMinute minute;
        const auto *closed = row.find("closed");
        const auto *close = row.find("close");
        if (!row.isObject() || row.members.size() != 3 || !integer(row, "start_utc", minute.startUtc) ||
            minute.startUtc < start || minute.startUtc >= end || minute.startUtc % 60 ||
            minute.startUtc <= previous || !closed || !closed->isBool() || !closed->boolean ||
            !close || !close->isString() || !ExactDecimal::parse(close->text, &minute.close) ||
            minute.close.zero() || minute.close.negative() || minute.close.digits() > 38 ||
            minute.close.scale() > 18 || minute.close.text() != close->text)
            return false;
        previous = minute.startUtc;
        series.minutes.push_back(std::move(minute));
    }
    return true;
}

int refuse(const char *message, int code = 3) {
    std::fprintf(stderr, "market-hour: %s\n", message);
    return code;
}

void appendCoverage(std::string &report, const char *series, const LocalSeries &input,
                    unsigned covered, unsigned expected, uint64_t now) {
    report += std::string(series) + ": " + std::to_string(covered) + "/" + std::to_string(expected) +
              " closed minutes; declared observation UTC " + std::to_string(input.observedUtc) +
              " (age " + std::to_string(now - input.observedUtc) + " s)\n";
}

void appendMean(std::string &report, const char *units, const MarketExactMean &mean) {
    report += std::string("Mean ") + units + " (exact sum / minutes): " +
              mean.sum.text() + " / " + std::to_string(mean.count) + "\n";
}

int reviewHour(const std::filesystem::path &path) {
    std::string text;
    if (!readLocalReviewInput(path, maxInputBytes, text))
        return refuse("input file unavailable, oversized or unsuitable");

    const auto clock = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (clock < 1 || static_cast<uint64_t>(clock) > maxUtc)
        return refuse("local UTC clock unavailable");
    const auto now = static_cast<uint64_t>(clock);

    JsonValue root;
    uint64_t version = 0, end = 0;
    if (!parseJsonStrict(text, &root, nullptr) || !root.isObject())
        return refuse("invalid hour document");
    const auto *pearlJson = root.find("pearl");
    const auto *fxJson = root.find("fx");
    if (root.members.size() != (fxJson ? 4u : 3u) || !integer(root, "schema_version", version) ||
        version != 1 || !integer(root, "end_utc", end) || !pearlJson)
        return refuse("invalid hour document");
    if (end <= secondsPerHour || end % 60 || end > now)
        return refuse("end_utc must be a completed UTC minute boundary");

    const uint64_t start = end - secondsPerHour;
    LocalSeries pearl, fx;
    if (!readSeries(*pearlJson, pearlSeriesId, start, end, now, pearl))
        return refuse("invalid Pearl mainnet minute series");
    if (fxJson && !readSeries(*fxJson, fxSeriesId, start, end, now, fx))
        return refuse("invalid USDT/USD minute series");

    // A full set of validated FX starts aligns with a complete PRL series.
    const auto fxCovered = static_cast<unsigned>(fx.minutes.size());
    const auto *completeFx = fxCovered == minutesPerHour ? &fx.minutes : nullptr;
    const auto spec = market_detail::minuteWindowBounds(start, end);
    auto window = market_detail::accumulateMinuteWindow(spec, pearl.minutes, completeFx);
    // A USD product or sum can exceed the arithmetic budget while the PRL mean is representable.
    if (completeFx && window.status == MarketDataStatus::Arithmetic)
        window = market_detail::accumulateMinuteWindow<ClosedMinute>(spec, pearl.minutes, nullptr);
    if (window.status == MarketDataStatus::Arithmetic) return refuse("exact PRL arithmetic limit exceeded");
    if (!window.tokenUsdt) return refuse("Pearl mainnet requires all 60 closed minutes");

    std::string report = "Historical market review from user-declared data (offline; freshness unverified)\n";
    report += "UTC seconds [" + std::to_string(start) + ", " + std::to_string(end) +
              "); window ended " + std::to_string(now - end) + " s ago\n";
    appendCoverage(report, "Declared Gate Pearl mainnet PRL/USDT", pearl, window.pearlCovered, window.expected, now);
    appendMean(report, "USDT per PRL", *window.tokenUsdt);

    if (!fxJson) {
        report += "Mean USD per PRL unavailable (no FX series)\n";
    } else {
        appendCoverage(report, "Declared Kraken USDT/USD", fx, fxCovered, window.expected, now);
        if (window.tokenUsd)
            appendMean(report, "USD per PRL", *window.tokenUsd);
        else if (!completeFx)
            report += "Mean USD per PRL unavailable (incomplete aligned FX coverage)\n";
        else
            report += "Mean USD per PRL unavailable (exact arithmetic limit exceeded)\n";
    }

    if (std::fwrite(report.data(), 1, report.size(), stdout) != report.size() || std::fflush(stdout) != 0)
        return refuse("could not write report", 1);
    return 0;
}

}  // namespace

int commandMarketHour(bool help, int argc, const char *const *argv) {
    try {
        if (argc != (help ? 3 : 4))
            return refuse("invalid arguments; use market-hour --help", 2);
#if defined(_WIN32)
        (void)argv;
        // Use the native operands and require the command/options as spelled on the wide command line.
        struct WideArguments {
            LPWSTR *values;
            ~WideArguments() { if (values) LocalFree(values); }
        };
        int count = 0;
        const WideArguments args{CommandLineToArgvW(GetCommandLineW(), &count)};
        if (!args.values || count != argc || std::wstring_view(args.values[1]) != L"market-hour")
            return refuse("invalid arguments; use market-hour --help", 2);
        const std::wstring_view option(args.values[2]);
        if (help ? option != L"--help" && option != L"-h" : option != L"--file")
            return refuse("invalid arguments; use market-hour --help", 2);
#endif
        if (help) {
            std::printf("canary-miner market-hour --file FILE\n"
                        "Review an hour of user-declared closed UTC minute prices, offline.\n"
                        "JSON (up to 64 KiB): schema_version (1), end_utc, pearl, optional fx.\n"
                        "Each series has series, observed_utc and minutes. Use %s for pearl\n"
                        "and %s for fx. UTC times are Unix seconds.\n"
                        "Each minute has start_utc, closed (true), and close (a positive decimal string).\n"
                        "End is exclusive. Include 60 consecutive PRL minutes, oldest first.\n"
                        "Aligned USDT/USD minutes produce a second exact mean when coverage is complete.\n",
                        pearlSeriesId, fxSeriesId);
            return 0;
        }
#if defined(_WIN32)
        return reviewHour(args.values[3]);
#else
        return reviewHour(argv[3]);
#endif
    } catch (const std::exception &) {
        return refuse("could not review local input");
    }
}

}  // namespace om
