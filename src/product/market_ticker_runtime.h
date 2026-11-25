// Application wiring for the two installed, read-only ticker feeds.
#pragma once
#include "market_reference_sink.h"
#include <csignal>
#include <filesystem>

namespace om {
struct PublicTickerLaunch {
    bool available = false, requested = false, permitted = false, dashboard = false, suppressed = false;
};
std::filesystem::path installedPublicTickerHelper(const std::filesystem::path &module);
MarketReferenceFactory installedPublicTickerFactory(const std::filesystem::path &module, PublicTickerLaunch launch);
int commandPublicTickers(const std::filesystem::path &module, int argc, const char *const *argv,
                         const volatile sig_atomic_t *stop);
} // namespace om
