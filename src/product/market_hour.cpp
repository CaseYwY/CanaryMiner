#include "market_hour.h"
#include "hash.h"
#include "json_dom.h"
#include "local_review_archive.h"
#include "market_hour_capture.h"
#include "market_hour_payload.h"
#include "public_json_process.h"

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
constexpr size_t maxReportBytes = 4 * 1024;
constexpr size_t maxArchiveBytes = maxInputBytes + maxReportBytes + 256;
constexpr size_t maxSampleResponseBytes = market_detail::marketHourBodyBytes + 128;
constexpr size_t maxPublicArchiveBytes = 3 * maxSampleResponseBytes + maxReportBytes + 512;
static_assert(maxPublicArchiveBytes <= maxBoundedLocalFileBytes);
constexpr std::string_view archiveHeader = "CANARY-MARKET-HOUR-REVIEW-1\n";
constexpr std::string_view publicArchiveHeader = "CANARY-MARKET-HOUR-REVIEW-2\n";
constexpr std::string_view fallbackArchiveHeader = "CANARY-MARKET-HOUR-REVIEW-3\n";
constexpr std::string_view sampleArchiveHeader = "CANARY-MARKET-HOUR-REVIEW-4\n";
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

const JsonValue *krakenHourRows(const JsonValue &payload) {
    const auto *error = payload.find("error"), *result = payload.find("result");
    uint64_t cursor = 0;
    if (!payload.isObject() || payload.members.size() != 2 || !error || !error->isArray() ||
        !error->items.empty() || !result || !result->isObject() || result->members.size() != 2 ||
        !integer(*result, "last", cursor))
        return nullptr;
    return result->find("USDTZUSD");
}

bool readKrakenMinute(const JsonValue &row, ReviewMinute &minute) {
    if (!row.isArray() || row.items.size() != 8) return false;
    const auto &v = row.items;
    ExactDecimal vwap;
    uint64_t count = 0;
    return v[0].asUint64(&minute.startUtc) && minute.startUtc && minute.startUtc % 60 == 0 &&
        readPrices(&v[1], &v[2], &v[3], &v[4], &v[6], minute) && decimal(&v[5], vwap, false) &&
        v[7].asUint64(&count);
}

}  // namespace

bool market_detail::readMarketHourPayload(const JsonValue &payload, HourFeed feed, uint64_t start, uint64_t end,
                                          uint64_t observedUtc, std::vector<ReviewMinute> &minutes) {
    if (minuteWindowBounds(start, end).expected != marketHourMinutes || observedUtc < end ||
        observedUtc > marketUtcLimit) return false;
    const bool kraken = feed == HourFeed::KrakenFx;
    unsigned maxRows = 500;
    auto readMinute = readMexcMinute;
    const auto *rows = &payload;
    if (feed == HourFeed::CoinbaseFx) {
        // Coinbase's payload is an object containing exactly one candles array.
        if (!rows->isObject() || rows->members.size() != 1) return false;
        rows = rows->find("candles");
        maxRows = 350;
        readMinute = readCoinbaseMinute;
    } else if (kraken) {
        rows = krakenHourRows(payload);
        maxRows = 720;
        readMinute = readKrakenMinute;
    }
    if (!rows || !rows->isArray() || rows->items.size() > maxRows || (kraken && rows->items.empty()))
        return false;
    std::vector<ReviewMinute> parsed;
    parsed.reserve(rows->items.size());
    for (const auto &row : rows->items) {
        ReviewMinute minute;
        if (!readMinute(row, minute) || minute.startUtc > observedUtc ||
            (kraken && !parsed.empty() && parsed.back().startUtc >= minute.startUtc))
            return false;
        parsed.push_back(std::move(minute));
    }
    if (kraken) {
        // Kraken defines its terminal row as current. Require its timestamp to match
        // the current UTC minute start at receipt before interpreting any row that way.
        if (parsed.back().startUtc != observedUtc - observedUtc % 60) return false;
        parsed.pop_back();
    } else {
        std::sort(parsed.begin(), parsed.end(), [](const auto &a, const auto &b) { return a.startUtc < b.startUtc; });
        if (std::adjacent_find(parsed.begin(), parsed.end(), [](const auto &a, const auto &b) {
                return a.startUtc == b.startUtc;
            }) != parsed.end()) return false;
    }
    parsed.erase(std::remove_if(parsed.begin(), parsed.end(), [&](const auto &minute) {
        return minute.startUtc < start || minute.startUtc >= end;
    }), parsed.end());
    minutes = std::move(parsed);
    return true;
}

namespace {

bool readCandleSeries(const JsonValue &object, const char *identity, HourFeed feed,
                      uint64_t start, uint64_t end, uint64_t now, LocalSeries &series) {
    const auto *payload = object.find("candles");
    return declaredSeries(object, identity, end, now, series) && payload &&
        market_detail::readMarketHourPayload(*payload, feed, start, end, series.observedUtc, series.minutes);
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

void appendPublicCoverage(std::string &report, const char *label, const market_detail::HourSample &sample,
                          uint64_t cutoff) {
    report += label;
    report += ": ";
    if (sample.problem) {
        report += sample.problem;
    } else {
        report += std::to_string(sample.minutes.size()) + "/60 elapsed minute buckets; observed UTC " +
            std::to_string(sample.observedUtc) + " (receipt age " +
            std::to_string(cutoff - sample.observedUtc) + " s)";
    }
    report += '\n';
}

const char *makeHourReport(const std::string &text, uint64_t now, std::string &report) {
    JsonValue root;
    uint64_t version = 0, end = 0;
    if (!parseJsonStrict(text, &root, nullptr) || !root.isObject())
        return "invalid hour document";
    const auto *pearlJson = root.find("pearl");
    const auto *fxJson = root.find("fx");
    const auto *krakenJson = root.find("kraken_fallback");
    if (!integer(root, "schema_version", version) || version < 1 || version > 3 ||
        !integer(root, "end_utc", end) || !pearlJson)
        return "invalid hour document";
    if (version == 3 ? (!fxJson || !krakenJson || root.members.size() != 5)
                     : (krakenJson || root.members.size() != (fxJson ? 4u : 3u)))
        return "invalid hour document";
    if (end <= marketHourSeconds || end % 60 || end > now)
        return "end_utc must be a completed UTC minute boundary";

    const uint64_t start = end - marketHourSeconds;
    LocalSeries pearl, fx, kraken;
    const bool vendor = version >= 2;
    const auto read = [&](const JsonValue &json, bool token, LocalSeries &series) {
        return vendor ? readCandleSeries(json, token ? mexcSeriesId : coinbaseSeriesId,
                                         token ? HourFeed::MexcPearl : HourFeed::CoinbaseFx, start, end, now, series)
                      : readSeries(json, token ? pearlSeriesId : fxSeriesId, start, end, now, series);
    };
    if (!read(*pearlJson, true, pearl)) return "invalid Pearl mainnet minute series";
    if (fxJson && !read(*fxJson, false, fx)) return "invalid USDT/USD minute series";
    if (krakenJson) {
        if (fx.minutes.size() == marketHourMinutes) return "Kraken fallback requires incomplete Coinbase coverage";
        if (!readCandleSeries(*krakenJson, fxSeriesId, HourFeed::KrakenFx, start, end, now, kraken))
            return "invalid Kraken fallback minute series";
    }

    const auto &fxForMean = krakenJson ? kraken : fx;
    const auto window = market_detail::calculateMarketHour(start, end, pearl.minutes, fxForMean.minutes);
    if (window.status == MarketDataStatus::Arithmetic) return "exact PRL arithmetic limit exceeded";
    if (!window.tokenUsdt)
        return vendor ? "Pearl mainnet requires all 60 elapsed minute buckets" :
                        "Pearl mainnet requires all 60 closed minutes";

    report = "Historical market review from user-declared data (offline; freshness unverified)\n";
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
                       fx, static_cast<unsigned>(fx.minutes.size()), window.expected, now, vendor);
        if (krakenJson) {
            appendCoverage(report, "Declared Kraken USDT/USD fallback", kraken,
                           static_cast<unsigned>(kraken.minutes.size()), window.expected, now, true);
            report += "Kraken minute starts inferred from declared observation.\n";
            if (window.tokenUsd) report += "The USD mean uses the declared Kraken series.\n";
        }
        if (window.tokenUsd)
            appendMean(report, "USD per PRL", *window.tokenUsd);
        else if (fxForMean.minutes.size() != marketHourMinutes)
            report += "Mean USD per PRL unavailable (incomplete aligned FX coverage)\n";
        else
            report += "Mean USD per PRL unavailable (exact arithmetic limit exceeded)\n";
    }

    return nullptr;
}

struct HourReviewOriginal {
    int64_t cutoff = 0;
    std::string input, report;
};

std::string encodeHourReview(const HourReviewOriginal &original) {
    std::string body(archiveHeader);
    body += std::to_string(original.cutoff) + '\n';
    for (const auto *part : {&original.input, &original.report}) {
        body += std::to_string(part->size()) + '\n';
        body += *part;
    }
    return body + sha256Hex(body) + '\n';
}

bool decodeHourReview(const std::string &bytes, HourReviewOriginal &original) {
    using local_review_archive::readArchiveNumber;
    using local_review_archive::readArchivePart;
    if (std::string_view(bytes).substr(0, archiveHeader.size()) != archiveHeader) return false;
    size_t offset = archiveHeader.size();
    return readArchiveNumber(bytes, offset, marketUtcLimit, original.cutoff) &&
           readArchivePart(bytes, offset, maxInputBytes, original.input) &&
           readArchivePart(bytes, offset, maxReportBytes, original.report) &&
           bytes.size() - offset == 65 && bytes.back() == '\n' &&
           bytes.compare(offset, 64, sha256Hex(bytes.substr(0, offset))) == 0;
}

bool readPublicHourReceipt(const std::string &bytes, size_t &offset, market_detail::PublicHourReceipt &receipt) {
    using local_review_archive::readArchiveNumber;
    int64_t sent = 0, observed = 0, received = 0;
    if (!readArchiveNumber(bytes, offset, marketUtcLimit, sent) ||
        !readArchiveNumber(bytes, offset, marketUtcLimit, observed) ||
        !readArchiveNumber(bytes, offset, marketUtcLimit, received) ||
        !local_review_archive::readArchivePart(bytes, offset, market_detail::marketHourBodyBytes, receipt.response.body))
        return false;
    receipt.sentUtc = static_cast<uint64_t>(sent);
    receipt.receivedUtc = static_cast<uint64_t>(received);
    receipt.response.status = PublicHttpsStatus::Ok;
    receipt.response.httpStatus = 200;
    receipt.response.observedUtc = static_cast<uint64_t>(observed);
    return true;
}

bool readSampleReceipt(const std::string &bytes, size_t &offset, market_detail::PublicHourReceipt &receipt) {
    using local_review_archive::readArchiveNumber;
    int64_t sent = 0, received = 0;
    std::string wire, canonical;
    if (!readArchiveNumber(bytes, offset, marketUtcLimit, sent) ||
        !readArchiveNumber(bytes, offset, marketUtcLimit, received) || received < sent ||
        !local_review_archive::readArchivePart(bytes, offset, maxSampleResponseBytes, wire) ||
        !decodePublicJsonResponse(wire, market_detail::marketHourBodyBytes, &receipt.response) ||
        receipt.response.status == PublicHttpsStatus::Cancelled ||
        !encodePublicJsonResponse(receipt.response, &canonical) || canonical != wire)
        return false;
    receipt.sentUtc = static_cast<uint64_t>(sent);
    receipt.receivedUtc = static_cast<uint64_t>(received);
    return true;
}

bool decodePublicHourReview(const std::string &bytes, market_detail::PublicHourOriginal &original,
                            std::string &savedReport) {
    const auto header = std::string_view(bytes).substr(0, publicArchiveHeader.size());
    const bool sample = header == sampleArchiveHeader;
    size_t offset = header.size();
    int64_t initial = 0, end = 0;
    if (!local_review_archive::readArchiveNumber(bytes, offset, marketUtcLimit, initial) ||
        !local_review_archive::readArchiveNumber(bytes, offset, marketUtcLimit, end))
        return false;
    bool fallback = header == fallbackArchiveHeader;
    if (sample) {
        int64_t permission = 0, receipts = 0;
        if (!local_review_archive::readArchiveNumber(bytes, offset, 2, permission) ||
            !local_review_archive::readArchiveNumber(bytes, offset, 3, receipts) || receipts < 2)
            return false;
        original.captureFallbackAllowed = permission == 2;
        fallback = receipts == 3;
    }
    const auto readReceipt = sample ? readSampleReceipt : readPublicHourReceipt;
    if (!readReceipt(bytes, offset, original.pearl) || !readReceipt(bytes, offset, original.coinbase) ||
        (fallback && !readReceipt(bytes, offset, original.kraken.emplace())) ||
        !local_review_archive::readArchivePart(bytes, offset, maxReportBytes, savedReport) ||
        bytes.size() - offset != 65 || bytes.back() != '\n' ||
        bytes.compare(offset, 64, sha256Hex(bytes.substr(0, offset))) != 0)
        return false;
    original.initialUtc = static_cast<uint64_t>(initial);
    original.endUtc = static_cast<uint64_t>(end);
    return original.endUtc && original.endUtc == market_detail::publicHourEnd(original.initialUtc) &&
           original.initialUtc <= original.pearl.sentUtc && original.pearl.receivedUtc <= original.coinbase.sentUtc &&
           (!original.kraken || original.coinbase.receivedUtc <= original.kraken->sentUtc);
}

const char *replayPublicHour(const std::string &bytes, uint64_t clock, std::string &report) {
    using namespace market_detail;
    PublicHourOriginal original;
    std::string savedReport;
    if (!decodePublicHourReview(bytes, original, savedReport))
        return "review archive unavailable or invalid";
    const auto cutoff = original.kraken ? original.kraken->receivedUtc : original.coinbase.receivedUtc;
    if (cutoff > clock) return "saved review cutoff is in the future";
    const auto start = original.endUtc - marketHourSeconds;
    const bool captured = original.captureFallbackAllowed.has_value();
    const auto pearl = decodePublicHourReply(original.pearl, HourFeed::MexcPearl, start, original.endUtc);
    const auto coinbase = decodePublicHourReply(original.coinbase, HourFeed::CoinbaseFx, start, original.endUtc);
    if (!captured && (pearl.problem || coinbase.problem))
        return "saved public hour response unavailable or invalid";
    const bool eligibleFallback = canFallbackHourFx(pearl, coinbase);
    if (captured && original.kraken.has_value() != (*original.captureFallbackAllowed && eligibleFallback))
        return "saved Kraken fallback does not match the captured request permission and coverage";
    HourSample kraken;
    if (original.kraken) {
        kraken = decodePublicHourReply(*original.kraken, HourFeed::KrakenFx, start, original.endUtc);
        if (!captured && (!eligibleFallback || kraken.problem))
            return "saved Kraken fallback unavailable or invalid";
    }
    const bool complete = renderPublicHour(original.endUtc, cutoff, pearl, coinbase,
                                           original.kraken ? &kraken : nullptr, report, captured);
    if (!complete && !captured)
        return "saved public hour requires complete aligned minute coverage and exact means";
    report += publicHourCaptureTime(cutoff);
    if (savedReport != report) return "saved review report differs from the recalculated report";
    return nullptr;
}

int reviewHour(const MarketHourOptions &options, const std::filesystem::path &path,
               const volatile sig_atomic_t *stop) {
    using local_review_archive::ReviewSave;
    const bool saving = options.action == MarketHourAction::Save;
    const bool reopening = options.action == MarketHourAction::Open;
    const auto stopped = [stop] { return stop && *stop; };
    const auto cancelled = [] { return refuse("local market review stopped", 130); };

    if (stopped()) return cancelled();
    HourReviewOriginal original;
    std::string bytes;
    bool publicArchive = false;
    if (reopening) {
        if (!readLocalReviewInput(path, maxPublicArchiveBytes, bytes))
            return refuse("review archive unavailable or invalid");
        publicArchive = std::string_view(bytes).substr(0, publicArchiveHeader.size()) == publicArchiveHeader ||
                        std::string_view(bytes).substr(0, fallbackArchiveHeader.size()) == fallbackArchiveHeader ||
                        std::string_view(bytes).substr(0, sampleArchiveHeader.size()) == sampleArchiveHeader;
        if (!publicArchive && (bytes.size() > maxArchiveBytes || !decodeHourReview(bytes, original)))
            return refuse("review archive unavailable or invalid");
    } else if (!readLocalReviewInput(path, maxInputBytes, original.input)) {
        return refuse("input file unavailable, oversized or unsuitable");
    }

    if (stopped()) return cancelled();
    const auto clock = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (clock < 1 || static_cast<uint64_t>(clock) > marketUtcLimit)
        return refuse("local UTC clock unavailable");
    if (original.cutoff > clock) return refuse("saved review cutoff is in the future");
    if (!reopening) original.cutoff = clock;

    std::string report;
    if (publicArchive) {
        if (const auto *problem = replayPublicHour(bytes, static_cast<uint64_t>(clock), report))
            return stopped() ? cancelled() : refuse(problem);
    } else {
        if (const char *problem = makeHourReport(original.input, static_cast<uint64_t>(original.cutoff), report))
            return refuse(problem);
        if (saving || reopening)
            report += "Review UTC " + std::to_string(original.cutoff) + " (ages as of this review)\n";
        if (reopening && original.report != report)
            return refuse("saved review report differs from the recalculated report");
    }
    if (stopped()) return cancelled();
    if (saving) {
        if (report.size() > maxReportBytes) return refuse("review report exceeds archive bound");
        original.report = report;
        const auto result = local_review_archive::save(options.archiveName, marketReviewExtension,
                                                       encodeHourReview(original), maxArchiveBytes, stop);
        if (result == ReviewSave::Stopped) return cancelled();
        if (result == ReviewSave::Uncertain)
            return refuse("archive publication or cleanup is uncertain; inspect the review and its temporary file");
        if (result != ReviewSave::Written)
            return refuse("could not create the new review archive; any existing archive was preserved");
    }
    if (std::fwrite(report.data(), 1, report.size(), stdout) != report.size() || std::fflush(stdout) != 0)
        return refuse("could not write report", 1);
    return 0;
}

}  // namespace

market_detail::HourSample market_detail::decodePublicHourReply(const PublicHourReceipt &receipt, HourFeed feed,
                                                               uint64_t start, uint64_t end) {
    HourSample sample;
    const auto &reply = receipt.response;
    sample.httpStatus = reply.httpStatus;
    sample.retryAfterSeconds = reply.retryAfterSeconds;
    if (reply.status != PublicHttpsStatus::Ok) {
        sample.problem = reply.httpStatus == 429 ? "provider rate limited the request" : publicHttpsStatusName(reply.status);
    } else if (reply.httpStatus != 200) {
        sample.problem = "unexpected HTTP status";
    } else {
        const auto status = decodePublicHourPayload(reply, feed, start, end, receipt.sentUtc, receipt.receivedUtc,
                                                   sample.minutes);
        if (status == MarketDataStatus::Timestamp)
            sample.problem = "invalid response observation time";
        else if (status == MarketDataStatus::RateLimited)
            sample.problem = "provider rate limited the request";
        else if (status == MarketDataStatus::ProviderThrottled)
            sample.problem = "provider throttled the request";
        else if (status == MarketDataStatus::ProviderError)
            sample.problem = "provider error envelope";
        else if (status != MarketDataStatus::Ok)
            sample.problem = "invalid minute response schema or timestamp";
        else
            sample.observedUtc = reply.observedUtc;
    }
    return sample;
}

std::string market_detail::publicHourCoverage(uint64_t end, uint64_t cutoff, const HourSample &pearl,
                                              const HourSample &coinbase, const HourSample *kraken) {
    std::string details="UTC seconds ["+std::to_string(end-marketHourSeconds)+", "+std::to_string(end)+
        "); window ended "+std::to_string(cutoff-end)+" s ago\n";
    appendPublicCoverage(details,"MEXC Pearl mainnet PRL/USDT",pearl,cutoff);
    appendPublicCoverage(details, "Coinbase USDT/USD", coinbase, cutoff);
    if (kraken)
        appendPublicCoverage(details, "Kraken USDT/USD fallback", *kraken, cutoff);
    return details;
}

bool market_detail::renderPublicHour(uint64_t end, uint64_t cutoff, const HourSample &pearl,
                                    const HourSample &coinbase, const HourSample *kraken, std::string &report,
                                    bool capture) {
    const auto &fx = kraken ? *kraken : coinbase;
    const auto window = calculateMarketHour(end-marketHourSeconds,end,pearl.minutes,fx.minutes);
    report = "One public candle sample; candle finality is unverified.\n";
    report += publicHourCoverage(end, cutoff, pearl, coinbase, kraken);
    if (kraken && window.tokenUsd)
        report += "FX source for USD mean: Kraken USDT/USD (candidate, minute starts inferred at receipt)\n";
    report += "Mean USDT per PRL (exact sum / minutes): ";
    report += window.tokenUsdt ? exactHourMean(*window.tokenUsdt) :
        window.status == MarketDataStatus::Arithmetic ? "N/A (exact arithmetic limit exceeded)" :
        "N/A (requires all 60 PRL minutes)";
    report += "\nMean USD per PRL (exact sum / minutes): ";
    report += window.tokenUsd ? exactHourMean(*window.tokenUsd) :
        window.tokenUsdt && fx.minutes.size() == marketHourMinutes ? "N/A (exact arithmetic limit exceeded)" :
        "N/A (requires complete aligned PRL and FX coverage)";
    report += '\n';
    if (capture) {
        report += window.tokenUsd ? "Sample result: complete\n" : "Sample result: incomplete\n";
        const auto detail = [&](const char *name, const HourSample &sample) {
            if (sample.problem) {
                if (sample.httpStatus || sample.retryAfterSeconds) {
                    report += name;
                    if (sample.httpStatus) report += " HTTP status " + std::to_string(sample.httpStatus);
                    if (sample.retryAfterSeconds)
                        report += " (Retry-After " + std::to_string(sample.retryAfterSeconds) + " s)";
                    report += '\n';
                }
                return;
            }
            if (sample.minutes.size() == marketHourMinutes) return;
            report += std::string(name) + " missing elapsed-minute ranges (UTC seconds)";
            uint64_t expected = end - marketHourSeconds;
            const auto gapBefore = [&](uint64_t next) {
                if (expected < next)
                    report += " [" + std::to_string(expected) + ", " + std::to_string(next) + ")";
            };
            for (const auto &minute : sample.minutes) {
                gapBefore(minute.startUtc);
                expected = minute.startUtc + 60;
            }
            gapBefore(end);
            report += '\n';
        };
        detail("MEXC Pearl mainnet PRL/USDT", pearl);
        detail("Coinbase USDT/USD", coinbase);
        if (kraken) detail("Kraken USDT/USD fallback", *kraken);
    }
    return window.tokenUsd.has_value();
}

local_review_archive::ReviewSave market_detail::savePublicHourReview(const std::string &name,
                                                                    const PublicHourOriginal &original,
                                                                    const std::string &report,
                                                                    const std::function<bool()> &stopped) {
    using local_review_archive::ReviewSave;
    if (report.empty() || report.size() > maxReportBytes) return ReviewSave::Unavailable;
    const bool capture = original.captureFallbackAllowed.has_value();
    std::string body(capture ? sampleArchiveHeader : original.kraken ? fallbackArchiveHeader : publicArchiveHeader);
    body += std::to_string(original.initialUtc) + '\n' + std::to_string(original.endUtc) + '\n';
    if (capture) {
        body += *original.captureFallbackAllowed ? "2\n" : "1\n";
        body += original.kraken ? "3\n" : "2\n";
    }
    const auto appendReceipt = [&](const PublicHourReceipt &receipt) {
        if (receipt.response.body.size() > marketHourBodyBytes) return false;
        body += std::to_string(receipt.sentUtc) + '\n';
        if (capture) {
            std::string wire;
            if (receipt.response.status == PublicHttpsStatus::Cancelled ||
                !encodePublicJsonResponse(receipt.response, &wire) || wire.size() > maxSampleResponseBytes) return false;
            body += std::to_string(receipt.receivedUtc) + '\n' + std::to_string(wire.size()) + '\n' + wire;
        } else {
            if (receipt.response.body.empty()) return false;
            body += std::to_string(receipt.response.observedUtc) + '\n' + std::to_string(receipt.receivedUtc) + '\n' +
                std::to_string(receipt.response.body.size()) + '\n' + receipt.response.body;
        }
        return true;
    };
    if (!appendReceipt(original.pearl) || !appendReceipt(original.coinbase) ||
        (original.kraken && !appendReceipt(*original.kraken))) return ReviewSave::Unavailable;
    body += std::to_string(report.size()) + '\n' + report;
    return local_review_archive::saveWhileActive(name, marketReviewExtension, body + sha256Hex(body) + '\n',
                                                maxPublicArchiveBytes, stopped);
}

int commandMarketHour(const MarketHourOptions &options, int argc, const char *const *argv,
                      const volatile sig_atomic_t *stop) {
    try {
        const bool help = options.action == MarketHourAction::Help;
        const bool saving = options.action == MarketHourAction::Save;
        if (argc != (help ? 3 : saving ? 6 : 4))
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
        const auto option = std::wstring_view(args.values[2]);
        const auto fileFlag = options.action == MarketHourAction::Open ? L"--open-review" : L"--file";
        if (help ? option != L"--help" && option != L"-h" : option != fileFlag)
            return refuse("invalid arguments; use market-hour --help", 2);
        if (saving && (std::wstring_view(args.values[4]) != L"--save-review" ||
                       std::wstring_view(args.values[5]) !=
                           std::wstring(options.archiveName.begin(), options.archiveName.end())))
            return refuse("invalid arguments; use market-hour --help", 2);
#endif
        if (help) {
            std::printf("canary-miner market-hour --file FILE [--save-review NAME.cmh]\n"
                        "canary-miner market-hour --open-review FILE\n"
                        "canary-miner market-hour --allow-public-candles [--kraken-fallback] [--dns-server IP|cloudflare-doh]\n"
                        "    [--save-review NAME.cmh | --save-sample NAME.cmh]\n"
                        "Request one hour from the installed HTTPS helper for MEXC Pearl/USDT and Coinbase USDT/USD.\n"
                        "With --kraken-fallback, request Kraken USDT/USD if the valid Coinbase hour has missing minutes and MEXC is complete.\n"
                        "Review an hour of user-declared UTC minute prices, offline.\n"
                        "Save a new archive in your trusted local folder. Open recalculates the original review.\n"
                        "JSON (up to 64 KiB): schema_version, end_utc, pearl, optional fx.\n"
                        "Local schema 3 requires fx (incomplete Coinbase) and kraken_fallback.\n"
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
        return reviewHour(options, args.values[3], stop);
#else
        return reviewHour(options, argv[3], stop);
#endif
    } catch (const std::exception &) {
        return refuse("could not review local input");
    }
}

}  // namespace om
