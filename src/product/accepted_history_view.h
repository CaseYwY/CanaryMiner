// Local observed user-share history only. Never a reward, payout or wallet record.
#pragma once

#include <cstdint>
#include <string>

namespace om {

struct AcceptedHistoryView {
    bool enabled = false, ready = false, gaps = false, clockAdjusted = false;
    uint64_t day = 0, week = 0, month = 0, all = 0;
    uint64_t startedUtc = 0, asOfUtc = 0;
    uint64_t gapFromUtc = 0, gapToUtc = 0;
    std::string error;
};

}  // namespace om
