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
                              int argc, const char *const *argv, const volatile sig_atomic_t *stop) {
    using namespace market_detail;
    if (argc != (options.dnsServer.empty() ? 3 : 5) ||
        !nativeMarketArgumentPair(argc, argv, 1, "market-reward", 2, "--allow-public-reward") ||
        (!options.dnsServer.empty() && !nativeMarketArgumentPair(argc, argv, 3, "--dns-server", 4, options.dnsServer)))
        return sampleError("exact --allow-public-reward required", 2);
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
        const auto result = parsePearlBlockbookTip(response.body, response.observedUtc);
        if (!lifetime.permitted()) return sampleError("helper ownership uncertain");
        if (marketSampleStopped(stop)) return sampleError("sample cancelled", 130);
        return reportPearlSubsidy(result, response.observedUtc, true);
    } catch (...) {
        if (!lifetime.permitted()) return sampleError("helper ownership uncertain");
        if (marketSampleStopped(stop)) return sampleError("sample cancelled", 130);
        return sampleError("could not complete the public sample");
    }
}

}  // namespace om
