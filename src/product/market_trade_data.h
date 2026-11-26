#pragma once

#include "market_data.h"
#include <utility>

namespace om {

using MarketTradeTime = std::pair<uint64_t, uint32_t>;  // UTC milliseconds and nanoseconds within that millisecond

struct MarketTradeObservation {
    MarketTradeTime time;
    ExactDecimal price;
    uint64_t receivedUtcMs=0, receivedMonoMs=0;
};

struct MarketTradeAges { uint64_t eventMs, receiptMs; };

struct MarketTradeReference {
    bool pearlFresh=false, fxFresh=false;
    std::optional<ExactDecimal> usd;
    const char *unavailable="requires two valid trades and receipts within 120 s";
};

MarketDataResult<MarketTradeObservation> parsePublicTrade(const PublicJsonResponse &response,PublicFeed feed,
    uint64_t sentUtcMs,uint64_t receivedUtcMs,uint64_t receivedMonoMs,
    std::optional<MarketTradeTime> *conflictingEvent=nullptr);
MarketTradeReference publicTradeReference(const MarketTradeObservation *pearl,const MarketTradeObservation *fx,
    uint64_t nowUtcMs,uint64_t nowMonoMs);
std::string marketTradeUtc(const MarketTradeTime &time);
std::optional<MarketTradeAges> marketTradeAges(const MarketTradeObservation &trade,uint64_t nowUtcMs,uint64_t nowMonoMs);
const char *marketTradeStatusName(MarketDataStatus status,PublicHttpsStatus transport);

} // namespace om
