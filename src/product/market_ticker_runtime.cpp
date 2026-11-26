#include "market_ticker_runtime.h"
#include "market_reference_runtime.h"
#include <utility>

namespace om {

std::filesystem::path installedPublicTickerHelper(const std::filesystem::path &module) {
    if (!module.is_absolute() || !module.has_filename()) return {};
#if defined(_WIN32)
    return module.parent_path() / "canary_public_json_worker.exe";
#else
    return module.parent_path() / "canary_public_json_worker";
#endif
}

MarketReferenceFactory installedPublicTickerFactory(const std::filesystem::path &module, PublicTickerLaunch launch) {
    if (!launch.available || !launch.requested || !launch.permitted || !launch.dashboard || launch.suppressed ||
        (!launch.dnsServer.empty() && !validPublicDnsServer(launch.dnsServer))) return {};
    const auto helper = installedPublicTickerHelper(module);
    if (helper.empty()) return {};
    // A caller may bind a path before startup. Only the owned background worker launches the helper.
    return [helper, enableHour = launch.hourPermitted, enableTrades = launch.tradesPermitted, dns = std::move(launch.dnsServer)] {
        MarketPollingPolicy policy;
        policy.scheduleEconomicRequests = false;
        policy.publicHour = enableHour;
        policy.publicTrades = enableTrades;
        policy.https.dnsServer = dns;
        return makeMarketReferenceRuntime(marketProcessFetch(helper), policy);
    };
}

}  // namespace om
