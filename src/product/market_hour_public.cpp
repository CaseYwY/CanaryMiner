#include "market_hour.h"
#include "market_hour_payload.h"
#include "market_public_sample.h"
#include "market_ticker_runtime.h"

#include <cstdio>

namespace om {
namespace {

using namespace market_detail;
using ClockReading = std::pair<uint64_t, uint64_t>;

bool clockContinuous(const ClockReading &initial, const ClockReading &previous, const ClockReading &current) {
    if (current.first < previous.first || current.second < previous.second ||
        current.first < initial.first || current.second < initial.second || current.second > marketUtcLimit)
        return false;
    const auto steadySeconds = (current.first - initial.first) / 1000;
    const auto utcSeconds = current.second - initial.second;
    return (std::max)(steadySeconds, utcSeconds) - (std::min)(steadySeconds, utcSeconds) <= 5;
}

struct HourSample {
    const char *problem = nullptr;
    uint64_t observedUtc = 0;
    std::vector<ReviewMinute> minutes;
};

HourSample decodeHourReply(const PublicJsonResponse &reply, HourFeed feed, uint64_t start, uint64_t end,
                          uint64_t sentUtc, uint64_t receivedUtc) {
    HourSample sample;
    if (reply.status != PublicHttpsStatus::Ok) {
        sample.problem = reply.httpStatus == 429 ? "provider rate limited the request" : publicHttpsStatusName(reply.status);
    } else if (reply.httpStatus != 200) {
        sample.problem = "unexpected HTTP status";
    } else {
        const auto status = decodePublicHourPayload(reply, feed, start, end, sentUtc, receivedUtc, sample.minutes);
        if (status == MarketDataStatus::Timestamp)
            sample.problem = "invalid response observation time";
        else if (status != MarketDataStatus::Ok)
            sample.problem = "invalid minute response schema or timestamp";
        else
            sample.observedUtc = reply.observedUtc;
    }
    return sample;
}

void appendPublicCoverage(std::string &report, const char *label, const HourSample &sample, uint64_t nowUtc) {
    report += label;
    report += ": ";
    if (sample.problem) {
        report += sample.problem;
    } else {
        report += std::to_string(sample.minutes.size()) + "/60 elapsed minute buckets; observed UTC " +
            std::to_string(sample.observedUtc) + " (receipt age " +
            std::to_string(nowUtc - sample.observedUtc) + " s)";
    }
    report += '\n';
}

int hourError(const char *message, int code = 3) {
    std::fprintf(stderr, "market-hour: %s\n", message);
    return code;
}

}  // namespace

int commandPublicMarketHour(const std::filesystem::path &module, int argc, const char *const *argv,
                            const volatile sig_atomic_t *stop) {
    if (!nativeMarketConsent(argc, argv, "market-hour", "--allow-public-candles"))
        return hourError("exact --allow-public-candles required", 2);
    if (marketSampleStopped(stop)) return hourError("sample cancelled", 130);
    const auto ownershipUncertain = [] { return hourError("helper ownership uncertain"); };
    NativeProcessLifetime lifetime;
    if (!lifetime.begin()) return ownershipUncertain();
    try {
        const auto helper = installedPublicTickerHelper(module);
        if (helper.empty()) return hourError("installed helper path unavailable");
        const auto initial = marketSystemClock();
        const MarketPollingPolicy policy;
        if (initial.second > marketUtcLimit || initial.second <= marketHourSeconds + 60 + policy.settlementGuardSeconds)
            return hourError("local UTC clock unavailable");
        const auto end = (initial.second - policy.settlementGuardSeconds) / 60 * 60;
        const auto start = end - marketHourSeconds;
        const auto fetch = marketProcessFetch(helper);
        auto limits = policy.https;
        limits.bodyBytes = marketHourBodyBytes;
        auto previous = initial;
        HourSample pearl, fx;
        for (const auto feed : {HourFeed::MexcPearl, HourFeed::CoinbaseFx}) {
            if (!lifetime.permitted()) return ownershipUncertain();
            if (marketSampleStopped(stop)) return hourError("sample cancelled", 130);
            const auto sent = marketSystemClock();
            if (!clockContinuous(initial, previous, sent))
                return hourError("local clock changed during sampling");
            const bool token = feed == HourFeed::MexcPearl;
            const PublicJsonRequest request{token ? PublicFeed::MexcPearlHour : PublicFeed::CoinbaseUsdtHour,
                static_cast<int64_t>(start), static_cast<int64_t>(end)};
            const auto reply = awaitPublicMarketReply(fetch, request, limits, lifetime, stop);
            if (reply.status == PublicHttpsStatus::Unreaped || !lifetime.permitted()) return ownershipUncertain();
            if (marketSampleStopped(stop)) return hourError("sample cancelled", 130);
            const auto received = marketSystemClock();
            if (!clockContinuous(initial, sent, received))
                return hourError("local clock changed during sampling");
            previous = received;
            (token ? pearl : fx) = decodeHourReply(reply, feed, start, end, sent.second, received.second);
        }

        const auto window = calculateMarketHour(start, end, pearl.minutes, fx.minutes);
        std::string report = "One public candle sample; candle finality is unverified.\n";
        report += "UTC seconds [" + std::to_string(start) + ", " + std::to_string(end) + "); window ended " +
            std::to_string(previous.second - end) + " s ago\n";
        appendPublicCoverage(report, "MEXC Pearl mainnet PRL/USDT", pearl, previous.second);
        appendPublicCoverage(report, "Coinbase USDT/USD", fx, previous.second);
        report += "Mean USDT per PRL (exact sum / minutes): ";
        report += window.tokenUsdt ? exactHourMean(*window.tokenUsdt) :
            window.status == MarketDataStatus::Arithmetic ? "N/A (exact arithmetic limit exceeded)" :
            "N/A (requires all 60 PRL minutes)";
        report += "\nMean USD per PRL (exact sum / minutes): ";
        report += window.tokenUsd ? exactHourMean(*window.tokenUsd) :
            window.tokenUsdt && fx.minutes.size() == marketHourMinutes ? "N/A (exact arithmetic limit exceeded)" :
            "N/A (requires complete aligned PRL and FX coverage)";
        report += '\n';
        if (!lifetime.permitted()) return ownershipUncertain();
        if (marketSampleStopped(stop)) return hourError("sample cancelled", 130);
        if (std::fwrite(report.data(), 1, report.size(), stdout) != report.size() || std::fflush(stdout) != 0) return 1;
        return window.tokenUsd ? 0 : 3;
    } catch (...) {
        if (!lifetime.permitted()) return ownershipUncertain();
        if (marketSampleStopped(stop)) return hourError("sample cancelled", 130);
        return hourError("could not complete the public sample");
    }
}

}  // namespace om
