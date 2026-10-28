// Independent public-data scheduling/ownership. Never selects a pool, starts a miner or changes credit facts.
#pragma once
#include "market_data.h"
#include "market_view.h"
#include <array>
#include <functional>
#include <filesystem>
#include <memory>

namespace om {
struct MarketPollingPolicy {
    uint64_t quoteRefreshMs = 60000, candleRefreshMs = 60000;
    uint64_t retryBaseMs = 5000, retryMaxMs = 300000, failedCycleCooldownMs = 300000;
    unsigned maxAttempts = 3, economicsMinutes = 480, settlementGuardSeconds = 5;
    uint64_t economicIntervalMs = 3600000;
    MarketFreshnessPolicy freshness;
    PublicHttpsLimits https;
};
bool validMarketPollingPolicy(const MarketPollingPolicy &policy);
struct MarketFetchTicket {
    PublicJsonRequest request;
    uint64_t generation = 0, serial = 0, startedUtc = 0, startedMonoMs = 0;
};
struct MarketFeedHealth {
    bool inFlight = false, latched = false;
    unsigned attempts = 0;
    uint64_t nextDueMs = 0;
    PublicHttpsStatus transport = PublicHttpsStatus::Unavailable;
    MarketDataStatus data = MarketDataStatus::Transport;
};
// Pure state machine: caller owns clocks and execution; no sleeps or network in this class.
class MarketPollingPlan {
   public:
    bool start(uint64_t generation, uint64_t nowMs, uint64_t nowUtc, const MarketPollingPolicy &policy, bool fixtureCandles);
    void stop();
    std::optional<MarketFetchTicket> next(uint64_t nowMs, uint64_t nowUtc);
    bool complete(const MarketFetchTicket &ticket, const PublicJsonResponse &response, MarketDataStatus data, uint64_t nowMs, uint64_t nowUtc);
    bool takeEconomicRequest(uint64_t nowMs, uint64_t nowUtc);
    bool active() const { return active_; }
    bool clockFault() const { return clockFault_; }
    const std::array<MarketFeedHealth, 4> &health() const { return health_; }
   private:
    bool clock(uint64_t nowMs, uint64_t nowUtc);
    MarketPollingPolicy policy_;
    bool active_ = false, fixtureCandles_ = false, clockFault_ = false;
    uint64_t generation_ = 0, serial_ = 0, baseMs_ = 0, baseUtc_ = 0, lastMs_ = 0, lastUtc_ = 0, economicDue_ = 0;
    std::array<uint64_t, 2> providerDue_{};
    std::optional<MarketFetchTicket> current_;
    std::array<MarketFeedHealth, 4> health_{};
};
struct MarketSnapshot {
    uint64_t generation = 0, revision = 0, economicRequests = 0;
    bool running = false, clockFault = false;
    MarketOrigin origin = MarketOrigin::ProviderResponse;
    std::array<MarketFeedHealth, 4> health;
    std::optional<MarketQuote> pearlQuote, fxQuote;
    std::optional<MarketCandles> pearlCandles, fxCandles;
};
using MarketFetch = std::function<PublicJsonResponse(const PublicJsonRequest &, const std::atomic<bool> &, PublicHttpsLimits)>;
using MarketClock = std::function<std::pair<uint64_t, uint64_t>()>;  // monotonic milliseconds, UTC seconds
// Production fetch seam uses the P-06a owned process, not helper-side PublicJsonClient in the miner.
MarketFetch marketProcessFetch(std::filesystem::path installedHelper);
class MarketDataOwner {
   public:
    MarketDataOwner(MarketFetch fetch, MarketPollingPolicy policy = {}, MarketOrigin origin = MarketOrigin::ProviderResponse,
                    bool fixtureCandles = false, MarketClock clock = {});
    ~MarketDataOwner();
    MarketDataOwner(const MarketDataOwner &) = delete;
    MarketDataOwner &operator=(const MarketDataOwner &) = delete;
    bool start(uint64_t generation);  // explicit public-data start, never mining authorization
    void stop();
    std::shared_ptr<const MarketSnapshot> snapshot() const;
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Native read-only projection. Kept separate from income/net rows; synthetic origins are conspicuously labelled.
std::vector<std::string> marketSnapshotLines(const MarketSnapshot &snapshot, const MarketPollingPolicy &policy, uint64_t nowUtc, uint64_t nowMonoMs);
PublicMarketView marketDashboardView(const MarketSnapshot &snapshot, const MarketPollingPolicy &policy, uint64_t nowUtc, uint64_t nowMonoMs);
}  // namespace om
