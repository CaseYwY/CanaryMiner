#include "market_hour.h"
#include "json_dom.h"
#include "local_review_input.h"
#include "market_hour_payload.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string_view>
#if defined(_WIN32)
#include <shellapi.h>
#endif

namespace om {
namespace {

using market_detail::HourFeed;
using market_detail::ReviewMinute;
using market_detail::marketHourMinutes;
using market_detail::marketHourSeconds;
using market_detail::marketUtcLimit;
constexpr size_t maxInputBytes = 64 * 1024;
constexpr char pearlSeriesId[] = "gate-pearl-mainnet-usdt-v1";
constexpr char fxSeriesId[] = "kraken-usdt-usd-v1";
constexpr char mexcSeriesId[] = "mexc-pearl-mainnet-usdt-v1";
constexpr char coinbaseSeriesId[] = "coinbase-usdt-usd-v1";

struct LocalSeries {
    uint64_t observedUtc = 0;
    std::vector<ReviewMinute> minutes;
};

bool integer(const JsonValue &object, const char *key, uint64_t &value) {
    const auto *member = object.find(key);
    return member && member->asUint64(&value);
}

bool declaredSeries(const JsonValue &object, const char *identity, uint64_t end, uint64_t now, LocalSeries &series) {
    const auto *id = object.find("series");
    return object.isObject() && object.members.size() == 3 && id && id->isString() &&
        id->text == identity && integer(object, "observed_utc", series.observedUtc) &&
        series.observedUtc >= end && series.observedUtc <= now;
}

bool decimal(const JsonValue *value, ExactDecimal &result, bool positive) {
    return value && value->isString() && ExactDecimal::parse(value->text, &result) &&
        !result.negative() && (!positive || !result.zero()) && result.digits() <= 38 && result.scale() <= 18;
}

bool readSeries(const JsonValue &object, const char *expectedSeries, uint64_t start, uint64_t end,
                uint64_t now, LocalSeries &series) {
    const auto *rows = object.find("minutes");
    if (!declaredSeries(object, expectedSeries, end, now, series) || !rows || !rows->isArray() ||
        rows->items.empty() || rows->items.size() > marketHourMinutes)
        return false;

    series.minutes.reserve(rows->items.size());
    uint64_t previous = 0;
    for (const auto &row : rows->items) {
        ReviewMinute minute;
        const auto *closed = row.find("closed");
        const auto *close = row.find("close");
        if (!row.isObject() || row.members.size() != 3 || !integer(row, "start_utc", minute.startUtc) ||
            minute.startUtc < start || minute.startUtc >= end || minute.startUtc % 60 ||
            minute.startUtc <= previous || !closed || !closed->isBool() || !closed->boolean ||
            !decimal(close, minute.close, true) || minute.close.text() != close->text)
            return false;
        previous = minute.startUtc;
        series.minutes.push_back(std::move(minute));
    }
    return true;
}

bool readPrices(const JsonValue *open, const JsonValue *high, const JsonValue *low,
                const JsonValue *close, const JsonValue *volume, ReviewMinute &minute) {
    ExactDecimal o, h, l, v;
    return decimal(open, o, true) && decimal(high, h, true) && decimal(low, l, true) &&
        decimal(close, minute.close, true) && decimal(volume, v, false) &&
        l.compare(o) <= 0 && o.compare(h) <= 0 && l.compare(minute.close) <= 0 && minute.close.compare(h) <= 0;
}

bool readMexcMinute(const JsonValue &row, ReviewMinute &minute) {
    if (!row.isArray() || row.items.size() != 8) return false;
    const auto &v = row.items;
    uint64_t openMs = 0, closeMs = 0;
    ExactDecimal quoteVolume;
    // Match the published millisecond example's exact 60,000-ms interval.
    if (!v[0].asUint64(&openMs) || !openMs || openMs % 60000 || openMs / 1000 > marketUtcLimit ||
        !v[6].asUint64(&closeMs) || closeMs != openMs + 60000 || !decimal(&v[7], quoteVolume, false))
        return false;
    minute.startUtc = openMs / 1000;
    return readPrices(&v[1], &v[2], &v[3], &v[4], &v[5], minute);
}

bool readCoinbaseMinute(const JsonValue &row, ReviewMinute &minute) {
    const auto *start = row.find("start");
    if (!row.isObject() || row.members.size() != 6 || !start || !start->isString()) return false;
    JsonValue seconds;
    seconds.type = JsonValue::Type::Number;
    seconds.text = start->text;
    return seconds.asUint64(&minute.startUtc) && minute.startUtc && minute.startUtc % 60 == 0 &&
        readPrices(row.find("open"), row.find("high"), row.find("low"), row.find("close"), row.find("volume"), minute);
}

}  // namespace

bool market_detail::readMarketHourPayload(const JsonValue &payload, HourFeed feed, uint64_t start, uint64_t end,
                                          uint64_t observedUtc, std::vector<ReviewMinute> &minutes) {
    if (minuteWindowBounds(start, end).expected != marketHourMinutes || observedUtc < end ||
        observedUtc > marketUtcLimit) return false;
    const bool pearl = feed == HourFeed::MexcPearl;
    const auto *rows = &payload;
    if (!pearl) {
        // Coinbase's payload is an object containing exactly one candles array.
        if (!rows || !rows->isObject() || rows->members.size() != 1) return false;
        rows = rows->find("candles");
    }
    if (!rows || !rows->isArray() || rows->items.size() > (pearl ? 500u : 350u)) return false;
    std::vector<ReviewMinute> parsed;
    parsed.reserve(rows->items.size());
    const auto readMinute = pearl ? readMexcMinute : readCoinbaseMinute;
    for (const auto &row : rows->items) {
        ReviewMinute minute;
        if (!readMinute(row, minute) || minute.startUtc > observedUtc) return false;
        parsed.push_back(std::move(minute));
    }
    std::sort(parsed.begin(), parsed.end(), [](const auto &a, const auto &b) { return a.startUtc < b.startUtc; });
    if (std::adjacent_find(parsed.begin(), parsed.end(), [](const auto &a, const auto &b) {
            return a.startUtc == b.startUtc;
        }) != parsed.end()) return false;
    parsed.erase(std::remove_if(parsed.begin(), parsed.end(), [&](const auto &minute) {
        return minute.startUtc < start || minute.startUtc >= end;
    }), parsed.end());
    minutes = std::move(parsed);
    return true;
}

namespace {

bool readCandleSeries(const JsonValue &object, bool pearl, uint64_t start, uint64_t end,
                      uint64_t now, LocalSeries &series) {
    const auto *payload = object.find("candles");
    return declaredSeries(object, pearl ? mexcSeriesId : coinbaseSeriesId, end, now, series) && payload &&
        market_detail::readMarketHourPayload(*payload, pearl ? HourFeed::MexcPearl : HourFeed::CoinbaseFx,
                                            start, end, series.observedUtc, series.minutes);
}

int refuse(const char *message, int code = 3) {
    std::fprintf(stderr, "market-hour: %s\n", message);
    return code;
}

void appendCoverage(std::string &report, const char *series, const LocalSeries &input,
                    unsigned covered, unsigned expected, uint64_t now, bool vendor) {
    report += std::string(series) + ": " + std::to_string(covered) + "/" + std::to_string(expected) +
              (vendor ? " elapsed minute buckets" : " closed minutes") +
              "; declared observation UTC " + std::to_string(input.observedUtc) +
              " (age " + std::to_string(now - input.observedUtc) + " s)\n";
}

void appendMean(std::string &report, const char *units, const MarketExactMean &mean) {
    report += std::string("Mean ") + units + " (exact sum / minutes): " +
              market_detail::exactHourMean(mean) + "\n";
}

int reviewHour(const std::filesystem::path &path) {
    std::string text;
    if (!readLocalReviewInput(path, maxInputBytes, text))
        return refuse("input file unavailable, oversized or unsuitable");

    const auto clock = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (clock < 1 || static_cast<uint64_t>(clock) > marketUtcLimit)
        return refuse("local UTC clock unavailable");
    const auto now = static_cast<uint64_t>(clock);

    JsonValue root;
    uint64_t version = 0, end = 0;
    if (!parseJsonStrict(text, &root, nullptr) || !root.isObject())
        return refuse("invalid hour document");
    const auto *pearlJson = root.find("pearl");
    const auto *fxJson = root.find("fx");
    if (root.members.size() != (fxJson ? 4u : 3u) || !integer(root, "schema_version", version) ||
        (version != 1 && version != 2) || !integer(root, "end_utc", end) || !pearlJson)
        return refuse("invalid hour document");
    if (end <= marketHourSeconds || end % 60 || end > now)
        return refuse("end_utc must be a completed UTC minute boundary");

    const uint64_t start = end - marketHourSeconds;
    LocalSeries pearl, fx;
    const bool vendor = version == 2;
    const auto read = [&](const JsonValue &json, bool token, LocalSeries &series) {
        return vendor ? readCandleSeries(json, token, start, end, now, series) :
            readSeries(json, token ? pearlSeriesId : fxSeriesId, start, end, now, series);
    };
    if (!read(*pearlJson, true, pearl)) return refuse("invalid Pearl mainnet minute series");
    if (fxJson && !read(*fxJson, false, fx)) return refuse("invalid USDT/USD minute series");

    const auto fxCovered = static_cast<unsigned>(fx.minutes.size());
    const auto window = market_detail::calculateMarketHour(start, end, pearl.minutes, fx.minutes);
    if (window.status == MarketDataStatus::Arithmetic) return refuse("exact PRL arithmetic limit exceeded");
    if (!window.tokenUsdt)
        return refuse(vendor ? "Pearl mainnet requires all 60 elapsed minute buckets" :
                               "Pearl mainnet requires all 60 closed minutes");

    std::string report = "Historical market review from user-declared data (offline; freshness unverified)\n";
    if (vendor) report += "Candle finality is unverified; coverage is based on elapsed minute buckets.\n";
    report += "UTC seconds [" + std::to_string(start) + ", " + std::to_string(end) +
              "); window ended " + std::to_string(now - end) + " s ago\n";
    appendCoverage(report, vendor ? "Declared MEXC Pearl mainnet PRL/USDT" : "Declared Gate Pearl mainnet PRL/USDT",
                   pearl, window.pearlCovered, window.expected, now, vendor);
    appendMean(report, "USDT per PRL", *window.tokenUsdt);

    if (!fxJson) {
        report += "Mean USD per PRL unavailable (no FX series)\n";
    } else {
        appendCoverage(report, vendor ? "Declared Coinbase USDT/USD" : "Declared Kraken USDT/USD",
                       fx, fxCovered, window.expected, now, vendor);
        if (window.tokenUsd)
            appendMean(report, "USD per PRL", *window.tokenUsd);
        else if (fxCovered != marketHourMinutes)
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
                        "canary-miner market-hour --allow-public-candles\n"
                        "Request one hour from the installed HTTPS helper for MEXC Pearl/USDT and Coinbase USDT/USD.\n"
                        "Review an hour of user-declared UTC minute prices, offline.\n"
                        "JSON (up to 64 KiB): schema_version, end_utc, pearl, optional fx.\n"
                        "Version 1 accepts normalized minutes. Version 2 accepts declared MEXC and Coinbase candle payloads.\n"
                        "Version 1 series have series, observed_utc and minutes. Use %s for pearl\n"
                        "and %s for fx. UTC times are Unix seconds.\n"
                        "Each minute has start_utc, closed (true), and close (a positive decimal string).\n"
                        "End is exclusive. In version 1, include 60 consecutive PRL minutes, oldest first.\n"
                        "Aligned USDT/USD minutes produce a second exact mean when coverage is complete.\n"
                        "Version 2 uses candles (raw JSON response) in place of minutes. Use %s for pearl\n"
                        "and %s for fx. Supply the actual observation UTC in seconds.\n"
                        "Version 2 reports elapsed minute buckets, with candle finality unverified.\n",
                        pearlSeriesId, fxSeriesId, mexcSeriesId, coinbaseSeriesId);
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
