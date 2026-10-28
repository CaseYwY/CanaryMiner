#pragma once
#include "manual_market_preferences.h"
namespace om {
// Precomputes exact arithmetic once. The returned display-only closure reads no files/network and cannot enter policy.
ManualMarketProjector makeManualMarketProjector(const ManualMarketPreferences &preferences, uint64_t activationUtc, uint64_t activationMonoMs);
}  // namespace om
