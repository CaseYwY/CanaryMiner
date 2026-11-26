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
        const auto initial = marketSystemClock();
        if (!initial.second || initial.second > rewardUtcLimit) return sampleError("local UTC clock unavailable");
        const auto response = awaitPublicMarketReply(fetch, {PublicFeed::PearlMainnetTip}, limits, lifetime, stop);
        if (response.status == PublicHttpsStatus::Unreaped || !lifetime.permitted())
            return sampleError("helper ownership uncertain");
        if (marketSampleStopped(stop)) return sampleError("sample cancelled", 130);
        if (response.status != PublicHttpsStatus::Ok) return sampleError(publicHttpsStatusName(response.status));
        const auto received = marketSystemClock();
        if (!marketSampleClockContinuous(initial, initial, received) || response.observedUtc < initial.second ||
            response.observedUtc > received.second || response.httpStatus != 200)
            return sampleError("invalid response observation time or HTTP status");
        const auto result = parsePearlBlockbookTip(response.body, response.observedUtc, PearlRewardCutoff::WholeObservationSecond);
        if (!lifetime.permitted()) return sampleError("helper ownership uncertain");
        if (marketSampleStopped(stop)) return sampleError("sample cancelled", 130);
        if (options.saveFlag)
            return savePublicMarketRewardReview(options.archiveName, response, initial.second, received.second,
                                                result, lifetime, stop);
        return reportPearlSubsidy(result, response.observedUtc, true);
    } catch (...) {
        if (!lifetime.permitted()) return sampleError("helper ownership uncertain");
        if (marketSampleStopped(stop)) return sampleError("sample cancelled", 130);
        return sampleError("could not complete the public sample");
    }
}

}  // namespace om
