#pragma once

#include "local_review_archive.h"
#include "market_hour_payload.h"

#include <functional>

namespace om::market_detail {

struct HourSample {
    const char *problem = nullptr;
    uint64_t observedUtc = 0;
    std::vector<ReviewMinute> minutes;
};

struct PublicHourReceipt {
    uint64_t sentUtc = 0, receivedUtc = 0;
    PublicJsonResponse response;
};

struct PublicHourOriginal {
    uint64_t initialUtc = 0, endUtc = 0;
    PublicHourReceipt pearl, fx;
};

inline uint64_t publicHourEnd(uint64_t initialUtc) {
    constexpr uint64_t settlementGuardSeconds = 5;
    return initialUtc > marketHourSeconds + 60 + settlementGuardSeconds && initialUtc <= marketUtcLimit
        ? (initialUtc - settlementGuardSeconds) / 60 * 60 : 0;
}

std::string publicHourCoverage(uint64_t end,uint64_t cutoff,const HourSample &pearl,const HourSample &fx);

bool renderPublicHour(uint64_t end, uint64_t cutoff, const HourSample &pearl, const HourSample &fx,
                      std::string &report);

inline std::string publicHourCaptureTime(uint64_t cutoff) {
    return "Capture UTC " + std::to_string(cutoff) + " (ages at capture)\n";
}

local_review_archive::ReviewSave savePublicHourReview(const std::string &name, const PublicHourOriginal &original,
                                                      const std::string &report,
                                                      const std::function<bool()> &stopped);

}  // namespace om::market_detail
