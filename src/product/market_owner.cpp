#include "market_owner.h"
#include "public_json_process.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace om {
namespace {
uint64_t later(uint64_t now, uint64_t delay) { return delay > UINT64_MAX - now ? UINT64_MAX : now + delay; }
unsigned index(PublicFeed feed) { return static_cast<unsigned>(feed); }
unsigned provider(unsigned feed) { return feed < 2 ? 0 : 1; }
bool candles(unsigned feed) { return feed == 1 || feed == 3; }
bool transient(const PublicJsonResponse &r) {
    return r.status == PublicHttpsStatus::Network || r.status == PublicHttpsStatus::Timeout ||
        (r.status == PublicHttpsStatus::HttpStatus && (r.httpStatus == 429 || (r.httpStatus >= 500 && r.httpStatus <= 599)));
}
bool sameTicket(const MarketFetchTicket &a, const MarketFetchTicket &b) {
    return a.generation == b.generation && a.serial == b.serial && a.request.feed == b.request.feed && a.request.fromUtc == b.request.fromUtc &&
        a.request.toUtc == b.request.toUtc && a.startedUtc == b.startedUtc && a.startedMonoMs == b.startedMonoMs;
}
std::pair<uint64_t,uint64_t> systemClock() {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto utc = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    return {ms < 0 ? 0 : static_cast<uint64_t>(ms), utc < 0 ? 0 : static_cast<uint64_t>(utc)};
}
}  // namespace

bool validMarketPollingPolicy(const MarketPollingPolicy &p) {
    return p.quoteRefreshMs >= 1000 && p.quoteRefreshMs <= 86400000 && p.candleRefreshMs >= 1000 && p.candleRefreshMs <= 86400000 &&
        p.retryBaseMs >= 1000 && p.retryBaseMs <= p.retryMaxMs && p.retryMaxMs <= 3600000 && p.failedCycleCooldownMs >= p.retryMaxMs &&
        p.failedCycleCooldownMs <= 86400000 && p.maxAttempts >= 1 && p.maxAttempts <= 5 && p.economicsMinutes >= 1 && p.economicsMinutes <= 719 &&
        p.settlementGuardSeconds <= 60 && p.economicIntervalMs >= 1000 && p.economicIntervalMs <= 86400000 &&
        p.freshness.maxObservationAgeMs >= 1000 && p.freshness.maxObservationAgeMs <= 86400000 && p.freshness.maxCrossFeedSkewMs <= 3600000 &&
        p.https.connectMs > 0 && p.https.connectMs <= p.https.totalMs && p.https.totalMs >= 50 && p.https.totalMs <= 60000 &&
        p.https.bodyBytes > 0 && p.https.bodyBytes <= 1024 * 1024 && p.https.headerBytes >= 64 && p.https.headerBytes <= 32768;
}
MarketFetch marketProcessFetch(std::filesystem::path helper) {
    auto client = std::make_shared<PublicJsonProcessClient>(std::move(helper));
    return [client](const PublicJsonRequest &request, const std::atomic<bool> &cancel, PublicHttpsLimits limits) {
        return client->get(request, cancel, limits);
    };
}
bool MarketPollingPlan::start(uint64_t generation, uint64_t ms, uint64_t utc, const MarketPollingPolicy &policy, bool fixture) {
    if (active_ || !generation || generation <= generation_ || !utc || utc > 253402300799ULL || !validMarketPollingPolicy(policy)) return false;
    generation_ = generation; policy_ = policy; fixtureCandles_ = fixture; baseMs_ = lastMs_ = ms; baseUtc_ = lastUtc_ = utc;
    economicDue_ = ms; providerDue_ = {ms, ms}; health_ = {}; current_.reset(); clockFault_ = false; active_ = true;
    for (auto &h : health_) h.nextDueMs = ms;
    return true;
}
void MarketPollingPlan::stop() { active_ = false; current_.reset(); for (auto &h : health_) h.inFlight = false; }
bool MarketPollingPlan::clock(uint64_t ms, uint64_t utc) {
    if (!active_) return false;
    const uint64_t elapsed = ms >= baseMs_ ? (ms - baseMs_) / 1000 : 0;
    const uint64_t wall = utc >= baseUtc_ ? utc - baseUtc_ : 0;
    const auto drift = wall > elapsed ? wall - elapsed : elapsed - wall;
    if (!utc || utc > 253402300799ULL || ms < lastMs_ || ms < baseMs_ || utc < lastUtc_ || drift > 5) {
        stop(); clockFault_ = true; return false;
    }
    lastMs_ = ms; lastUtc_ = utc; return true;
}
std::optional<MarketFetchTicket> MarketPollingPlan::next(uint64_t ms, uint64_t utc) {
    if (!clock(ms, utc) || current_) return std::nullopt;
    unsigned choice = static_cast<unsigned>(health_.size());
    PublicJsonRequest chosen;
    for (unsigned i = 0; i < health_.size(); ++i) {
        auto &h = health_[i];
        if (h.latched || (candles(i) && !fixtureCandles_) || ms < h.nextDueMs || ms < providerDue_[provider(i)]) continue;
        PublicJsonRequest request{static_cast<PublicFeed>(i)};
        if (candles(i)) {
            const auto minutes = (std::max)(60u, policy_.economicsMinutes);
            if (utc <= policy_.settlementGuardSeconds) continue;
            const auto end = (utc - policy_.settlementGuardSeconds) / 60 * 60;
            if (end <= minutes * 60 + 60) continue;
            request.fromUtc = static_cast<int64_t>(end - minutes * 60); request.toUtc = static_cast<int64_t>(end);
        }
        if (choice == health_.size() || h.nextDueMs < health_[choice].nextDueMs) { choice = i; chosen = request; }
    }
    if (choice == health_.size()) return std::nullopt;
    auto &h = health_[choice];
    if (h.attempts >= policy_.maxAttempts) h.attempts = 0;  // a new, separately cooled-down periodic cycle
    if (serial_ == UINT64_MAX) { stop(); return std::nullopt; }
    MarketFetchTicket ticket{chosen, generation_, ++serial_, utc, ms};
    h.inFlight = true;
    providerDue_[provider(choice)] = later(ms, 1000);  // shared endpoint/IP budget; never evade with concurrency
    current_ = ticket; return ticket;
}
bool MarketPollingPlan::complete(const MarketFetchTicket &ticket, const PublicJsonResponse &r, MarketDataStatus data, uint64_t ms, uint64_t utc) {
    if (!clock(ms, utc) || !current_ || !sameTicket(*current_, ticket)) return false;
    auto &h = health_[index(ticket.request.feed)];
    current_.reset(); h.inFlight = false; h.transport = r.status; h.data = data;
    const auto refresh = candles(index(ticket.request.feed)) ? policy_.candleRefreshMs : policy_.quoteRefreshMs;
    if (r.status == PublicHttpsStatus::Ok && data == MarketDataStatus::Ok) {
        h.attempts = 0; h.nextDueMs = later(ms, refresh); return true;
    }
    ++h.attempts;
    if ((!transient(r) && !(r.status == PublicHttpsStatus::Ok && data == MarketDataStatus::RateLimited)) ||
        (r.httpStatus == 429 && r.retryAfterSeconds >= 3600)) {
        // A transport-capped Retry-After could mean a much longer refusal. Do not automatically retry early.
        h.latched = true; h.nextDueMs = UINT64_MAX; return true;
    }
    uint64_t delay = policy_.retryBaseMs;
    for (unsigned i = 1; i < h.attempts; ++i) delay = (std::min)(later(delay, delay), policy_.retryMaxMs);
    delay = (std::max)(delay, static_cast<uint64_t>(r.retryAfterSeconds) * 1000);
    if (h.attempts >= policy_.maxAttempts) delay = (std::max)(delay, (std::max)(refresh, policy_.failedCycleCooldownMs));
    h.nextDueMs = later(ms, delay); return true;
}
bool MarketPollingPlan::takeEconomicRequest(uint64_t ms, uint64_t utc) {
    if (!clock(ms, utc) || ms < economicDue_) return false;
    economicDue_ = later(ms, policy_.economicIntervalMs); return true;  // coalesce missed intervals; no catch-up loop
}

struct MarketDataOwner::Impl {
    MarketFetch fetch;
    MarketPollingPolicy policy;
    MarketOrigin origin;
    bool fixture;
    MarketClock clock;
    std::atomic<bool> cancel{true};
    std::thread thread;
    std::mutex mutex, waitMutex, commitMutex;
    std::condition_variable wait;
    MarketPollingPlan plan;
    MarketSnapshot current;
    std::shared_ptr<const MarketSnapshot> published = std::make_shared<const MarketSnapshot>();
    void publish() {
        current.health = plan.health(); current.clockFault = plan.clockFault(); current.running = plan.active() && !cancel.load();
        ++current.revision;
        auto value = std::make_shared<const MarketSnapshot>(current);
        std::lock_guard<std::mutex> lock(mutex); published = std::move(value);
    }
    void run() noexcept {
        try {
            while (!cancel.load()) {
                auto now = clock();
                const bool economicRequest = plan.takeEconomicRequest(now.first, now.second);
                if (economicRequest) ++current.economicRequests;
                auto ticket = plan.next(now.first, now.second);
                if (!plan.active()) break;
                if (ticket) {
                    publish();
                    if (cancel.load()) break;
                    PublicJsonResponse response;
                    try { response = fetch(ticket->request, cancel, policy.https); } catch (...) { response.status = PublicHttpsStatus::Unavailable; }
                    now = clock();
                    if (cancel.load()) break;
                    const MarketParseContext context{ticket->request, ticket->startedUtc, now.first, ticket->generation, origin,
                        fixture ? MarketMinuteConvention::FixtureMinuteStart : MarketMinuteConvention::Unqualified, now.second};
                    MarketDataResult<MarketQuote> quote;
                    MarketDataResult<MarketCandles> minutes;
                    if (candles(index(ticket->request.feed))) minutes = parseMarketCandles(response, context);
                    else quote = parseMarketQuote(response, context);
                    const auto status = candles(index(ticket->request.feed)) ? minutes.status : quote.status;
                    now = clock();
                    {
                        // Serialize cancellation with cache commit, not with network/parse work. No late fact commits after stop.
                        std::lock_guard<std::mutex> commit(commitMutex);
                        if (cancel.load()) break;
                        if (plan.complete(*ticket, response, status, now.first, now.second)) {
                            if (quote.value) {
                                if (ticket->request.feed == PublicFeed::GatePearlTicker) current.pearlQuote = std::move(quote.value);
                                else current.fxQuote = std::move(quote.value);
                            }
                            if (minutes.value) {
                                if (ticket->request.feed == PublicFeed::GatePearlCandles) current.pearlCandles = std::move(minutes.value);
                                else current.fxCandles = std::move(minutes.value);
                            }
                        }
                        publish();
                    }
                } else if (economicRequest) publish();
                std::unique_lock<std::mutex> lock(waitMutex);
                wait.wait_for(lock, std::chrono::milliseconds(20), [&] { return cancel.load(); });
            }
        } catch (...) { /* no provider exception escapes into mining; final snapshot is stopped/unavailable */ }
        plan.stop();
        try { publish(); } catch (...) {}
    }
};
MarketDataOwner::MarketDataOwner(MarketFetch fetch, MarketPollingPolicy policy, MarketOrigin origin, bool fixture, MarketClock clock) : impl_(new Impl()) {
    impl_->fetch = std::move(fetch); impl_->policy = policy; impl_->origin = origin; impl_->fixture = fixture;
    impl_->clock = clock ? std::move(clock) : MarketClock(systemClock);
}
MarketDataOwner::~MarketDataOwner() { stop(); }
bool MarketDataOwner::start(uint64_t generation) {
    if (impl_->thread.joinable() || !impl_->fetch || (impl_->fixture && impl_->origin != MarketOrigin::SyntheticFixture) ||
        (impl_->origin != MarketOrigin::SyntheticFixture && impl_->origin != MarketOrigin::ProviderResponse)) return false;
    try {
        const auto now = impl_->clock();
        if (!impl_->plan.start(generation, now.first, now.second, impl_->policy, impl_->fixture)) return false;
        impl_->current = {}; impl_->current.generation = generation; impl_->current.origin = impl_->origin;
        impl_->cancel = false; impl_->publish();
        impl_->thread = std::thread([this] { impl_->run(); }); return true;
    } catch (...) { impl_->cancel = true; impl_->plan.stop(); try { impl_->publish(); } catch (...) {} return false; }
}
void MarketDataOwner::stop() {
    { std::lock_guard<std::mutex> commit(impl_->commitMutex); impl_->cancel = true; }
    impl_->wait.notify_all();
    if (impl_->thread.joinable()) impl_->thread.join();
}
std::shared_ptr<const MarketSnapshot> MarketDataOwner::snapshot() const {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->published;
}

std::vector<std::string> marketSnapshotLines(const MarketSnapshot &s, const MarketPollingPolicy &policy, uint64_t utc, uint64_t ms) {
    std::vector<std::string> lines;
    lines.push_back(std::string("Market references: ") + (s.clockFault ? "clock fault" : s.running ? "active" : "stopped") +
        (s.origin == MarketOrigin::SyntheticFixture ? " | SYNTHETIC FIXTURE - NOT LIVE" : " | public provider observations"));
    auto latest = latestMarketReference(s.pearlQuote ? &*s.pearlQuote : nullptr, s.fxQuote ? &*s.fxQuote : nullptr, utc, ms, policy.freshness);
    if (!s.running || s.clockFault) { latest.tokenUsdt.reset(); latest.tokenUsd.reset(); latest.usdtUsd.reset(); }
    auto quoteLine = [&](const char *label, const std::optional<MarketQuote> &quote, const std::optional<ExactDecimal> &fresh) {
        lines.push_back(std::string(label) + ": " + (fresh ? fresh->text() : "N/A") + (quote && !fresh ? " | stale/invalid cached reference " + quote->last.text() : "") +
            (quote ? " | observed UTC " + std::to_string(quote->observation.observedUtc) + " | observation age " +
                (ms >= quote->observation.receivedMonoMs ? std::to_string((ms - quote->observation.receivedMonoMs) / 1000) + "s" : "N/A") : " | no observation"));
    };
    quoteLine("Latest PRL/USDT [Gate international PEARL_USDT; Pearl mainnet]", s.pearlQuote, latest.tokenUsdt);
    quoteLine("Latest USDT/USD [Kraken USDTZUSD]", s.fxQuote, latest.usdtUsd);
    lines.push_back(std::string("Source health: Gate ") + publicHttpsStatusName(s.health[0].transport) + "/" + marketDataStatusName(s.health[0].data) +
        " | Kraken " + publicHttpsStatusName(s.health[2].transport) + "/" + marketDataStatusName(s.health[2].data));
    lines.push_back("Latest PRL/USD reference: " + (latest.tokenUsd ? latest.tokenUsd->text() : "N/A (missing/stale/skewed conversion)") +
        " | trade-event time NOT SUPPLIED; observation age is not trade age");
    const unsigned expected = 60;
    MarketWindow window;
    if (s.running && !s.clockFault && s.pearlCandles && utc >= s.pearlCandles->observation.observedUtc && ms >= s.pearlCandles->observation.receivedMonoMs &&
        ms - s.pearlCandles->observation.receivedMonoMs <= policy.freshness.maxObservationAgeMs &&
        utc - s.pearlCandles->observation.observedUtc <= policy.freshness.maxObservationAgeMs / 1000 && utc > policy.settlementGuardSeconds + 3600) {
        const auto end = (utc - policy.settlementGuardSeconds) / 60 * 60;
        const MarketCandles *fx = nullptr;
        if (s.fxCandles && ms >= s.fxCandles->observation.receivedMonoMs && ms - s.fxCandles->observation.receivedMonoMs <= policy.freshness.maxObservationAgeMs &&
            utc >= s.fxCandles->observation.observedUtc && utc - s.fxCandles->observation.observedUtc <= policy.freshness.maxObservationAgeMs / 1000) fx = &*s.fxCandles;
        window = marketWindow(*s.pearlCandles, fx, end - 3600, end);
    }
    auto average = [](const std::optional<MarketExactMean> &value) {
        std::string text;
        if (!value || !value->sum.formatRatio(value->count, 6, &text)) return std::string("N/A");
        return text == "0.000000" && !value->sum.zero() ? std::string("<0.000001") : text;
    };
    lines.push_back("1h equal-time mean of 1m closes (TWAP approximation): " + average(window.tokenUsdt) + " USDT/PRL | " + average(window.tokenUsd) + " USD/PRL");
    lines.push_back("1h coverage: Pearl " + std::to_string(window.pearlCovered) + "/" + std::to_string(expected) + " | FX " + std::to_string(window.fxCovered) +
        "/" + std::to_string(expected) + " | end UTC " + (window.endUtc ? std::to_string(window.endUtc) + " | lag " + std::to_string(utc - window.endUtc) + "s" : "N/A (time convention unqualified/unavailable)"));
    lines.push_back("Reference prices are not executable proceeds, credited income or current holdings.");
    return lines;
}
PublicMarketView marketDashboardView(const MarketSnapshot &snapshot, const MarketPollingPolicy &policy, uint64_t utc, uint64_t ms) {
    PublicMarketView view;
    if (!snapshot.generation) return view;  // an unstarted/default owner must not enable a completed-looking feature
    view.enabled = true; view.lines = marketSnapshotLines(snapshot, policy, utc, ms); return view;
}
}  // namespace om
