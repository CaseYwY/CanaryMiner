#include "market_reward.h"

#include "market_public_sample.h"
#include "market_ticker_runtime.h"

#include <cstdio>

namespace om {
namespace {

int sampleError(const char *message, int code = 3) {
    std::fprintf(stderr, "market-reward: %s\n", message);
    return code;
}

}  // namespace

int commandPublicMarketReward(const MarketRewardOptions &options, const std::filesystem::path &module,
                              const volatile sig_atomic_t *stop) {
    using namespace market_detail;
    if (marketSampleStopped(stop)) return sampleError("sample cancelled", 130);
    NativeProcessLifetime lifetime;
    if (!lifetime.begin()) return sampleError("helper ownership uncertain");
    try {
        const auto helper = installedPublicTickerHelper(module);
        if (helper.empty()) return sampleError("installed helper path unavailable");
        const auto fetch = marketProcessFetch(helper);
        PublicHttpsLimits limits;
        limits.bodyBytes = marketRewardBodyBytes;
        limits.dnsServer = options.dnsServer;
        limits.totalMs = marketRewardSampleMs;
        limits.parentDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(marketRewardSampleMs);
        const auto initial = marketSystemClock();
        if (!initial.second || initial.second > rewardUtcLimit) return sampleError("local UTC clock unavailable");
        auto previous = initial;
        const auto take = [&](const PublicJsonRequest &request, MarketRewardReply &reply) {
            if (!lifetime.permitted()) return sampleError("helper ownership uncertain");
            if (marketSampleStopped(stop)) return sampleError("sample cancelled", 130);
            const auto sent = marketSystemClock();
            if (!marketSampleClockContinuous(initial, previous, sent))
                return sampleError("local clock changed during the sample");
            auto response = awaitPublicMarketReply(fetch, request, limits, lifetime, stop);
            const auto received = marketSystemClock();
            if (response.status == PublicHttpsStatus::Unreaped || !lifetime.permitted())
                return sampleError("helper ownership uncertain");
            if (marketSampleStopped(stop)) return sampleError("sample cancelled", 130);
            if (response.status != PublicHttpsStatus::Ok) return sampleError(publicHttpsStatusName(response.status));
            if (!marketSampleClockContinuous(initial, sent, received) || response.observedUtc < sent.second ||
                response.observedUtc > received.second || response.httpStatus != 200)
                return sampleError("invalid response observation time or HTTP status");
            if (std::chrono::steady_clock::now() >= limits.parentDeadline)
                return sampleError(publicHttpsStatusName(PublicHttpsStatus::Timeout));
            previous = received;
            reply = {sent.second, response.observedUtc, received.second, std::move(response.body)};
            return 0;
        };

        MarketRewardReplies replies;
        auto &[opening, indexed, closing] = replies;
        if (const int error = take({PublicFeed::PearlMainnetTip}, opening)) return error;
        const auto tip = parsePearlBlockbookTip(opening.body, opening.observedUtc, PearlRewardCutoff::WholeObservationSecond);
        if (!lifetime.permitted()) return sampleError("helper ownership uncertain");
        if (marketSampleStopped(stop)) return sampleError("sample cancelled", 130);
        if (!tip.value) return reportPearlRewardFailure(tip);
        if (const int error = take({PublicFeed::PearlIndexedBlockHash, 0, 0, tip.value->tipHeight}, indexed)) return error;
        if (const int error = take({PublicFeed::PearlMainnetTip}, closing)) return error;
        return reportPublicMarketReward(options.archiveName, replies, lifetime, stop);
    } catch (...) {
        if (!lifetime.permitted()) return sampleError("helper ownership uncertain");
        if (marketSampleStopped(stop)) return sampleError("sample cancelled", 130);
        return sampleError("could not complete the public sample");
    }
}

}  // namespace om
