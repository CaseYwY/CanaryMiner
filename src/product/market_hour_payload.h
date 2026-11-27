// Structurally validated minute payloads for one hour review. Elapsed buckets do not establish trade finality.
#pragma once

#include "json_dom.h"
#include "market_minute_window.h"

namespace om::market_detail {

inline constexpr unsigned marketHourMinutes = 60;
inline constexpr uint64_t marketHourSeconds = 3600;
inline constexpr uint64_t marketUtcLimit = 253402300799ULL;
inline constexpr size_t marketHourBodyBytes = 64 * 1024;

enum class HourFeed { MexcPearl, CoinbaseFx, KrakenFx };

struct ReviewMinute {
    uint64_t startUtc = 0;
    ExactDecimal close;
    bool closed = true;  // eligible for window arithmetic, not a verified exchange finality flag
};

bool readMarketHourPayload(const JsonValue &payload, HourFeed feed, uint64_t start, uint64_t end,
                           uint64_t observedUtc, std::vector<ReviewMinute> &minutes);

inline MarketDataStatus decodePublicHourPayload(const PublicJsonResponse &reply, HourFeed feed,
                                                uint64_t start, uint64_t end, uint64_t sentUtc, uint64_t receivedUtc,
                                                std::vector<ReviewMinute> &minutes) {
    if (reply.status != PublicHttpsStatus::Ok || reply.httpStatus != 200) return MarketDataStatus::Transport;
    if (!sentUtc || reply.observedUtc < sentUtc || reply.observedUtc > receivedUtc) return MarketDataStatus::Timestamp;
    JsonValue payload;
    if (reply.body.size() > marketHourBodyBytes || !parseJsonStrict(reply.body, &payload, nullptr))
        return MarketDataStatus::Schema;
    if (feed == HourFeed::KrakenFx) {
        const auto status = krakenEnvelopeStatus(payload);
        if (status != MarketDataStatus::Ok) return status;
    }
    if (!readMarketHourPayload(payload, feed, start, end, reply.observedUtc, minutes))
        return MarketDataStatus::Schema;
    return MarketDataStatus::Ok;
}

inline MarketWindow calculateMarketHour(uint64_t start, uint64_t end, const std::vector<ReviewMinute> &pearl,
                                        const std::vector<ReviewMinute> &fx) {
    const auto spec = minuteWindowBounds(start, end);
    if (spec.expected != marketHourMinutes) return spec;
    const auto *completeFx = fx.size() == marketHourMinutes ? &fx : nullptr;
    auto window = accumulateMinuteWindow(spec, pearl, completeFx);
    if (completeFx && window.status == MarketDataStatus::Arithmetic)
        window = accumulateMinuteWindow<ReviewMinute>(spec, pearl, nullptr);
    return window;
}

inline bool hourFallbackCoverage(size_t pearl, size_t coinbase) {
    return pearl == marketHourMinutes && coinbase < marketHourMinutes;
}

inline std::string exactHourMean(const MarketExactMean &mean) {
    return mean.sum.text() + " / " + std::to_string(mean.count);
}

}  // namespace om::market_detail
