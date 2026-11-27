#pragma once

#include "local_review_archive.h"
#include "market_hour_payload.h"

#include <functional>
#include <optional>

namespace om::market_detail {

struct HourSample {
    const char *problem = nullptr;
    uint64_t observedUtc = 0;
    std::vector<ReviewMinute> minutes;
    long httpStatus = 0;
    unsigned retryAfterSeconds = 0;
};

struct PublicHourReceipt {
    uint64_t sentUtc = 0, receivedUtc = 0;
    PublicJsonResponse response;
};

struct PublicHourOriginal {
    uint64_t initialUtc = 0, endUtc = 0;
    PublicHourReceipt pearl, coinbase;
    std::optional<PublicHourReceipt> kraken;
    std::optional<bool> captureFallbackAllowed; // present only in a sample archive
};

inline bool canFallbackHourFx(const HourSample &pearl, const HourSample &coinbase) {
    return !pearl.problem && !coinbase.problem && hourFallbackCoverage(pearl.minutes.size(), coinbase.minutes.size());
}

inline uint64_t publicHourEnd(uint64_t initialUtc) {
    constexpr uint64_t settlementGuardSeconds = 5;
    return initialUtc > marketHourSeconds + 60 + settlementGuardSeconds && initialUtc <= marketUtcLimit
        ? (initialUtc - settlementGuardSeconds) / 60 * 60 : 0;
}

std::string publicHourCoverage(uint64_t end, uint64_t cutoff, const HourSample &pearl, const HourSample &coinbase,
                               const HourSample *kraken);

HourSample decodePublicHourReply(const PublicHourReceipt &receipt, HourFeed feed, uint64_t start, uint64_t end);

bool renderPublicHour(uint64_t end, uint64_t cutoff, const HourSample &pearl, const HourSample &coinbase,
                      const HourSample *kraken, std::string &report, bool capture = false);

inline std::string publicHourCaptureTime(uint64_t cutoff) {
    return "Capture UTC " + std::to_string(cutoff) + " (ages at capture)\n";
}

local_review_archive::ReviewSave savePublicHourReview(const std::string &name, const PublicHourOriginal &original,
                                                      const std::string &report,
                                                      const std::function<bool()> &stopped);

}  // namespace om::market_detail
