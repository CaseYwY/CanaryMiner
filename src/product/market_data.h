// Typed public-data adapters and exact reference calculations. No network, wallets, credit ledger or policy actions.
#pragma once
#include "exact_decimal.h"
#include "public_https.h"
#include <optional>
#include <vector>

namespace om {
enum class MarketSeries { GatePearlMainnetUsdtV1, KrakenUsdtUsdV1 };
enum class MarketOrigin { ProviderResponse, SyntheticFixture };
// The documented candle time anchors are not yet production-qualified. Only offline fixtures can opt in.
enum class MarketMinuteConvention { Unqualified, FixtureMinuteStart };
enum class MarketDataStatus { Ok, Transport, Identity, Schema, Decimal, Timestamp, UnsupportedConvention, Coverage, Arithmetic, ProviderError, RateLimited };
const char *marketDataStatusName(MarketDataStatus status);
const char *marketSeriesName(MarketSeries series);

struct MarketParseContext {
    PublicJsonRequest request;
    uint64_t requestStartedUtc = 0, receivedMonoMs = 0, generation = 0;
    MarketOrigin origin = MarketOrigin::ProviderResponse;
    MarketMinuteConvention convention = MarketMinuteConvention::Unqualified;
    uint64_t receivedUtc = 0;
};
struct MarketObservation {
    MarketSeries series = MarketSeries::GatePearlMainnetUsdtV1;
    MarketOrigin origin = MarketOrigin::ProviderResponse;
    uint64_t requestStartedUtc = 0, observedUtc = 0, receivedMonoMs = 0, generation = 0;
};
struct MarketQuote {
    MarketObservation observation;
    ExactDecimal last;
    std::optional<ExactDecimal> bid, ask;
    std::optional<uint64_t> sourceEventUtc;  // ticker adapters never manufacture this from receive/gateway times
};
struct MarketMinute {
    uint64_t startUtc = 0;
    ExactDecimal open, high, low, close, baseVolume;
    bool closed = false;
};
struct MarketCandles {
    MarketObservation observation;
    MarketMinuteConvention convention = MarketMinuteConvention::Unqualified;
    std::vector<MarketMinute> minutes;  // sorted, unique; Kraken's original terminal row is not present
    std::optional<uint64_t> committedCursor;
};
template<class T> struct MarketDataResult {
    MarketDataStatus status = MarketDataStatus::Schema;
    std::optional<T> value;
};
MarketDataResult<MarketQuote> parseMarketQuote(const PublicJsonResponse &response, const MarketParseContext &context);
MarketDataResult<MarketCandles> parseMarketCandles(const PublicJsonResponse &response, const MarketParseContext &context);

struct MarketExactMean {
    ExactDecimal sum;
    uint32_t count = 0;  // rational sum/count, not an implicitly rounded stored decimal
};
struct MarketWindow {
    MarketDataStatus status = MarketDataStatus::Coverage;
    uint64_t startUtc = 0, endUtc = 0;
    unsigned expected = 0, pearlCovered = 0, fxCovered = 0;
    std::optional<MarketExactMean> tokenUsdt, tokenUsd;
};
// [start,end), complete minute bins only. Historical USD = mean(P_i*F_i), never mean(P)*mean(F).
MarketWindow marketWindow(const MarketCandles &pearl, const MarketCandles *fx, uint64_t start, uint64_t end);

enum class MarketFreshness { Available, Missing, Stale, InvalidClock, WrongIdentity, EventTimeUnavailable, Skew };
struct MarketFreshnessPolicy {
    uint64_t maxObservationAgeMs = 120000, maxCrossFeedSkewMs = 30000;
    bool requireEventTime = false;
};
struct MarketLatest {
    MarketFreshness tokenStatus = MarketFreshness::Missing, usdStatus = MarketFreshness::Missing;
    std::optional<ExactDecimal> tokenUsdt, usdtUsd, tokenUsd;
    std::optional<uint64_t> tokenObservationAgeMs, fxObservationAgeMs;
};
MarketLatest latestMarketReference(const MarketQuote *pearl, const MarketQuote *fx, uint64_t nowUtc, uint64_t nowMonoMs,
                                  MarketFreshnessPolicy policy = {});
}  // namespace om
