#include "market_ticker_runtime.h"
#include "market_reference_runtime.h"

namespace om {
MarketReferenceFactory installedPublicTickerFactory(const std::filesystem::path &module, PublicTickerLaunch launch) {
    if (!launch.available || !launch.requested || !launch.permitted || !launch.dashboard || launch.suppressed ||
        !module.is_absolute() || !module.has_filename()) return {};
#ifdef _WIN32
    const auto helper = module.parent_path() / "canary_public_json_worker.exe";
#else
    const auto helper = module.parent_path() / "canary_public_json_worker";
#endif
    // A caller may bind a path before startup. Only the owned background worker launches the helper.
    return [helper] {
        MarketPollingPolicy policy;
        policy.scheduleEconomicRequests = false;
        return makeMarketReferenceRuntime(marketProcessFetch(helper), policy);
    };
}
} // namespace om
