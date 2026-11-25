#include "market_ticker_runtime.h"
#include "market_reference_runtime.h"

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
    if (!launch.available || !launch.requested || !launch.permitted || !launch.dashboard || launch.suppressed) return {};
    const auto helper = installedPublicTickerHelper(module);
    if (helper.empty()) return {};
    // A caller may bind a path before startup. Only the owned background worker launches the helper.
    return [helper] {
        MarketPollingPolicy policy;
        policy.scheduleEconomicRequests = false;
        return makeMarketReferenceRuntime(marketProcessFetch(helper), policy);
    };
}

}  // namespace om
