// Application wiring for installed read-only public market feeds.
#pragma once
#include "market_reference_sink.h"
#include <csignal>
#include <filesystem>
#include <string>

namespace om {
struct MarketTickerOptions;
struct PublicTickerLaunch {
    bool available = false, requested = false, permitted = false, dashboard = false, suppressed = false;
    bool hourPermitted = false;
    std::string dnsServer{};
};
std::filesystem::path installedPublicTickerHelper(const std::filesystem::path &module);
MarketReferenceFactory installedPublicTickerFactory(const std::filesystem::path &module, PublicTickerLaunch launch);
int commandPublicTickers(const MarketTickerOptions &options, const std::filesystem::path &module,
                         int argc, const char *const *argv, const volatile sig_atomic_t *stop);
} // namespace om
