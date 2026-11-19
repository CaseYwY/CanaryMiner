// Application wiring for the two installed, read-only ticker feeds.
#pragma once
#include "market_reference_sink.h"
#include <filesystem>

namespace om {
struct PublicTickerLaunch {
    bool available = false, requested = false, permitted = false, dashboard = false, suppressed = false;
};
MarketReferenceFactory installedPublicTickerFactory(const std::filesystem::path &module, PublicTickerLaunch launch);
} // namespace om
