#include "market_hour.h"
#include "market_hour_capture.h"
#include "market_public_sample.h"
#include "market_ticker_runtime.h"

#include <cstdio>

namespace om {
namespace {

using namespace market_detail;

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

int hourError(const char *message, int code = 3) {
    std::fprintf(stderr, "market-hour: %s\n", message);
    return code;
}

}  // namespace

int commandPublicMarketHour(const MarketHourOptions &options, const std::filesystem::path &module,
                            int argc, const char *const *argv,
                            const volatile sig_atomic_t *stop) {
    const bool saving = options.action == MarketHourAction::PublicSave;
    const bool allowFallback = options.krakenFallbackFlag != 0;
    const auto operandCount=3+(saving ? 2 : 0)+(options.dnsFlag ? 2 : 0)+(allowFallback ? 1 : 0);
    if (argc!=operandCount ||
        !nativeMarketArgumentPair(argc,argv,1,"market-hour",2,"--allow-public-candles") ||
        (saving && !nativeMarketArgumentPair(argc,argv,options.saveFlag,"--save-review",
                                              options.saveFlag+1,options.archiveName)) ||
        (options.dnsFlag && !nativeMarketArgumentPair(argc,argv,options.dnsFlag,"--dns-server",
                                                      options.dnsFlag+1,options.dnsServer)) ||
        (allowFallback && !nativeMarketArguments(argc, argv, {{options.krakenFallbackFlag, "--kraken-fallback"}})))
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
        const auto end = publicHourEnd(initial.second);
        if (!end) return hourError("local UTC clock unavailable");
        const auto start = end - marketHourSeconds;
        const auto fetch = marketProcessFetch(helper);
        auto limits = policy.https;
        limits.bodyBytes = marketHourBodyBytes;
        limits.dnsServer=options.dnsServer;
        auto previous = initial;
        HourSample pearl, coinbase;
        PublicHourOriginal original{initial.second, end, {}, {}, {}};
        const auto sample = [&](PublicFeed source, HourFeed decoder, HourSample &result, PublicHourReceipt &receipt) {
            if (!lifetime.permitted()) return ownershipUncertain();
            if (marketSampleStopped(stop)) return hourError("sample cancelled", 130);
            const auto sent = marketSystemClock();
            if (!marketSampleClockContinuous(initial, previous, sent))
                return hourError("local clock changed during sampling");
            const PublicJsonRequest request{source, static_cast<int64_t>(start), static_cast<int64_t>(end)};
            auto reply = awaitPublicMarketReply(fetch, request, limits, lifetime, stop);
            if (reply.status == PublicHttpsStatus::Unreaped || !lifetime.permitted()) return ownershipUncertain();
            if (marketSampleStopped(stop)) return hourError("sample cancelled", 130);
            const auto received = marketSystemClock();
            if (!marketSampleClockContinuous(initial, sent, received))
                return hourError("local clock changed during sampling");
            previous = received;
            result = decodeHourReply(reply, decoder, start, end, sent.second, received.second);
            if (saving)
                receipt = PublicHourReceipt{sent.second, received.second, std::move(reply)};
            return 0;
        };
        if (const int result = sample(PublicFeed::MexcPearlHour, HourFeed::MexcPearl, pearl, original.pearl))
            return result;
        if (const int result = sample(PublicFeed::CoinbaseUsdtHour, HourFeed::CoinbaseFx, coinbase, original.coinbase))
            return result;
        std::optional<HourSample> kraken;
        if (allowFallback && canFallbackHourFx(pearl, coinbase)) {
            if (const int result = sample(PublicFeed::KrakenUsdtCandles, HourFeed::KrakenFx,
                                          kraken.emplace(), original.kraken.emplace()))
                return result;
        }

        std::string report;
        const auto *fallback = kraken ? &*kraken : nullptr;
        const bool complete = renderPublicHour(end, previous.second, pearl, coinbase, fallback, report);
        if (!lifetime.permitted()) return ownershipUncertain();
        if (marketSampleStopped(stop)) return hourError("sample cancelled", 130);
        if (saving) {
            using local_review_archive::ReviewSave;
            if (!complete) {
                const auto details=publicHourCoverage(end,previous.second,pearl,coinbase,fallback);
                if (!lifetime.permitted()) return ownershipUncertain();
                if (marketSampleStopped(stop)) return hourError("sample cancelled",130);
                hourError("could not save public review: complete aligned minute coverage and exact means required");
                std::fputs(details.c_str(),stderr);
                return 3;
            }
            report += publicHourCaptureTime(previous.second);
            const auto state = savePublicHourReview(options.archiveName, original, report, [&] {
                return marketSampleStopped(stop) || !lifetime.permitted();
            });
            if (state == ReviewSave::Stopped)
                return lifetime.permitted() ? hourError("sample cancelled", 130) : ownershipUncertain();
            if (state == ReviewSave::Uncertain)
                return hourError("archive publication or cleanup is uncertain; inspect the review and its temporary file");
            if (state != ReviewSave::Written)
                return hourError("could not create the new review archive; any existing archive was preserved");
        }
        if (std::fwrite(report.data(), 1, report.size(), stdout) != report.size() || std::fflush(stdout) != 0) return 1;
        return complete ? 0 : 3;
    } catch (...) {
        if (!lifetime.permitted()) return ownershipUncertain();
        if (marketSampleStopped(stop)) return hourError("sample cancelled", 130);
        return hourError("could not complete the public sample");
    }
}

}  // namespace om
