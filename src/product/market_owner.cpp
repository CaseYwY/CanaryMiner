#include "market_owner.h"
#include "market_hour_payload.h"
#include "market_public_sample.h"
#include "public_json_process.h"
#include "native_process_admission.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace om {
struct PublicHourObservation {
    uint64_t start = 0, end = 0, observedUtc = 0, receivedMonoMs = 0;
    std::vector<market_detail::ReviewMinute> minutes;
};

namespace {
using namespace market_detail;
constexpr auto pearlHourFeed = static_cast<unsigned>(PublicFeed::MexcPearlHour);
constexpr auto fxHourFeed = static_cast<unsigned>(PublicFeed::CoinbaseUsdtHour);
uint64_t later(uint64_t now, uint64_t delay) { return delay > UINT64_MAX - now ? UINT64_MAX : now + delay; }
bool tradeFeed(PublicFeed feed) { return feed==PublicFeed::GatePearlTrades || feed==PublicFeed::KrakenUsdtTrades; }
unsigned index(PublicFeed feed) {
    if (feed==PublicFeed::GatePearlTrades) return 0;
    if (feed==PublicFeed::KrakenUsdtTrades) return 2;
    return static_cast<unsigned>(feed);
}
uint64_t utcSeconds(const MarketPollingPolicy &policy,uint64_t utc) { return policy.publicTrades ? utc/1000 : utc; }
unsigned provider(unsigned feed) { return feed < pearlHourFeed ? feed / 2 : feed - 2; }
bool fixtureCandles(unsigned feed) { return feed == 1 || feed == 3; }
bool publicHourFeed(unsigned feed) { return feed == pearlHourFeed || feed == fxHourFeed; }
unsigned hourBit(unsigned feed) { return 1u << (feed - pearlHourFeed); }
uint64_t eligibleHourEnd(uint64_t utc, unsigned guard) {
    return utc > marketHourSeconds + 60 + guard ? (utc - guard) / 60 * 60 : 0;
}
bool transient(const PublicJsonResponse &r) {
    return r.status == PublicHttpsStatus::Network || r.status == PublicHttpsStatus::Timeout ||
        (r.status == PublicHttpsStatus::HttpStatus && (r.httpStatus == 429 || (r.httpStatus >= 500 && r.httpStatus <= 599)));
}
bool sameTicket(const MarketFetchTicket &a, const MarketFetchTicket &b) {
    return a.generation == b.generation && a.serial == b.serial && a.request.feed == b.request.feed && a.request.fromUtc == b.request.fromUtc &&
        a.request.toUtc == b.request.toUtc && a.request.blockHeight == b.request.blockHeight &&
        a.startedUtc == b.startedUtc && a.startedMonoMs == b.startedMonoMs;
}
}  // namespace

std::pair<uint64_t, uint64_t> marketSystemClock() {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto utc = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    return {ms < 0 ? 0 : static_cast<uint64_t>(ms), utc < 0 ? 0 : static_cast<uint64_t>(utc)};
}

bool validMarketPollingPolicy(const MarketPollingPolicy &p) {
    return !(p.publicTrades && p.scheduleEconomicRequests) && p.quoteRefreshMs >= 1000 && p.quoteRefreshMs <= 86400000 && p.candleRefreshMs >= 1000 && p.candleRefreshMs <= 86400000 &&
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
    if (active_ || helperFault_ || !generation || generation <= generation_ ||
        !utcSeconds(policy,utc) || utc>(policy.publicTrades ? maxMarketUtcMs : marketUtcLimit) ||
        (fixture && (policy.publicHour || policy.publicTrades)) || !validMarketPollingPolicy(policy)) return false;
    generation_ = generation; policy_ = policy; fixtureCandles_ = fixture; baseMs_ = lastMs_ = ms; baseUtc_ = lastUtc_ = utc;
    economicDue_ = ms; providerDue_.fill(ms); health_ = {}; current_.reset(); clockFault_ = false; active_ = true;
    pendingHourFeeds_ = 0; pendingHourEnd_ = 0;
    for (auto &h : health_) h.nextDueMs = ms;
    return true;
}
void MarketPollingPlan::stop() { active_ = false; current_.reset(); for (auto &h : health_) h.inFlight = false; }
void MarketPollingPlan::helperUnreaped() {
    if (current_) {
        auto &h = health_[index(current_->request.feed)];
        h.transport = PublicHttpsStatus::Unreaped; h.data = MarketDataStatus::Transport;
    }
    helperFault_ = true;
    stop();
    for (auto &h : health_) { h.latched = true; h.nextDueMs = UINT64_MAX; }
}
bool MarketPollingPlan::observeClock(uint64_t ms, uint64_t utc) {
    if (!active_) return false;
    const uint64_t elapsed = ms >= baseMs_ ? (ms - baseMs_) / (policy_.publicTrades ? 1 : 1000) : 0;
    const uint64_t wall = utc >= baseUtc_ ? utc - baseUtc_ : 0;
    const auto drift = wall > elapsed ? wall - elapsed : elapsed - wall;
    if (!utc || utc>(policy_.publicTrades ? maxMarketUtcMs : marketUtcLimit) || ms<lastMs_ ||
        ms<baseMs_ || utc<lastUtc_ || drift>(policy_.publicTrades ? 5000u : 5u)) {
        stop(); clockFault_ = true; return false;
    }
    lastMs_ = ms; lastUtc_ = utc; return true;
}
std::optional<MarketFetchTicket> MarketPollingPlan::next(uint64_t ms, uint64_t utc) {
    if (!observeClock(ms, utc) || current_) return std::nullopt;
    auto due = [&](unsigned i) {
        const auto &h = health_[i];
        return !h.latched && ms >= h.nextDueMs && ms >= providerDue_[provider(i)];
    };
    const auto seconds=utcSeconds(policy_,utc);
    unsigned hourFeeds = pendingHourFeeds_;
    auto hourEnd = pendingHourEnd_;
    if (policy_.publicHour && !hourFeeds) {
        hourEnd = eligibleHourEnd(seconds, policy_.settlementGuardSeconds);
        if (hourEnd) {
            for (const auto i : {pearlHourFeed, fxHourFeed})
                if (due(i)) hourFeeds |= hourBit(i);
            // Healthy lanes share a refresh window. A backed-off or latched lane cannot hold the other up.
            const auto healthy = [&](unsigned i) { return !health_[i].latched && !health_[i].attempts; };
            if (healthy(pearlHourFeed) && healthy(fxHourFeed) && hourFeeds != (hourBit(pearlHourFeed) | hourBit(fxHourFeed)))
                hourFeeds = 0;
        }
    }
    unsigned choice = static_cast<unsigned>(health_.size());
    PublicJsonRequest chosen;
    for (unsigned i = 0; i < health_.size(); ++i) {
        auto &h = health_[i];
        if (!due(i) || (fixtureCandles(i) && !fixtureCandles_)) continue;
        PublicJsonRequest request{static_cast<PublicFeed>(i)};
        if (policy_.publicTrades && (i==0 || i==2))
            request.feed=i==0 ? PublicFeed::GatePearlTrades : PublicFeed::KrakenUsdtTrades;
        if (publicHourFeed(i)) {
            if (!(hourFeeds & hourBit(i))) continue;
            request.fromUtc = static_cast<int64_t>(hourEnd - marketHourSeconds);
            request.toUtc = static_cast<int64_t>(hourEnd);
        } else if (fixtureCandles(i)) {
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
    MarketFetchTicket ticket{chosen, generation_, ++serial_, seconds, ms};
    if (publicHourFeed(choice)) {
        pendingHourFeeds_ = hourFeeds & ~hourBit(choice);
        pendingHourEnd_ = hourEnd;
    }
    h.inFlight = true;
    providerDue_[provider(choice)] = later(ms, 1000);  // shared endpoint/IP budget; never evade with concurrency
    current_ = ticket; return ticket;
}
bool MarketPollingPlan::complete(const MarketFetchTicket &ticket, const PublicJsonResponse &r, MarketDataStatus data, uint64_t ms, uint64_t utc) {
    if (r.status == PublicHttpsStatus::Unreaped && current_ && sameTicket(*current_, ticket)) { helperUnreaped(); return true; }
    if (!observeClock(ms, utc) || !current_ || !sameTicket(*current_, ticket)) return false;
    auto &h = health_[index(ticket.request.feed)];
    current_.reset(); h.inFlight = false; h.transport = r.status; h.data = data;
    const auto feed = index(ticket.request.feed);
    const auto refresh = fixtureCandles(feed) || publicHourFeed(feed) ? policy_.candleRefreshMs : policy_.quoteRefreshMs;
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
    if (!observeClock(ms, utc) || !policy_.scheduleEconomicRequests || ms < economicDue_) return false;
    economicDue_ = later(ms, policy_.economicIntervalMs); return true;  // coalesce missed intervals; no catch-up loop
}

struct MarketDataOwner::Impl {
    NativeProcessLifetime nativeLife;std::atomic<bool> nativeFaulted{false};
    MarketFetch fetch;
    MarketPollingPolicy policy;
    MarketOrigin origin;
    bool fixture;
    MarketClock clock;
    std::atomic<bool> cancel{true};
    std::thread thread;
    std::mutex mutex, waitMutex, commitMutex,lifecycle;
    std::condition_variable wait;
    MarketPollingPlan plan;
    MarketSnapshot current;
    std::shared_ptr<const MarketSnapshot> published = std::make_shared<const MarketSnapshot>();
    const std::shared_ptr<const MarketSnapshot> emptyTerminal=published;
    const std::shared_ptr<const MarketSnapshot> emptyNativeFault=[] {MarketSnapshot value;value.helperFault=true;return std::make_shared<const MarketSnapshot>(std::move(value));}();
    std::shared_ptr<const MarketSnapshot> maskedSource,masked;
    bool nativeCurrent()noexcept{ // commitMutex held; snapshot revocation never mutates the background-only plan/cache
        if(!nativeLife.bound()||nativeLife.current())return true;
        nativeFaulted=true;cancel=true;wait.notify_all();return false;
    }
    void publish() {
        current.health = plan.health(); current.clockFault = plan.clockFault(); current.helperFault = plan.helperFault()||nativeFaulted.load(); current.running = plan.active() && !cancel.load()&&nativeLife.current();
        ++current.revision;
        auto value = std::make_shared<const MarketSnapshot>(current);
        std::lock_guard<std::mutex> lock(mutex); published = std::move(value);
    }
    void run() noexcept {
        NativeProcessScope original(nativeLife.epoch());
        try {
            while (!cancel.load()) {
                {std::lock_guard<std::mutex> commit(commitMutex);if(!nativeCurrent()||cancel.load())break;}
                auto now = clock();
                {std::lock_guard<std::mutex> commit(commitMutex);if(!nativeCurrent()||cancel.load())break;}
                const bool economicRequest = plan.takeEconomicRequest(now.first, now.second);
                if (economicRequest) ++current.economicRequests;
                auto ticket = plan.next(now.first, now.second);
                if (!plan.active()) break;
                if (ticket) {
                    publish();
                    {std::lock_guard<std::mutex> commit(commitMutex);if(!nativeCurrent()||cancel.load())break;}
                    const auto feed = index(ticket->request.feed);
                    auto limits = policy.https;
                    if (publicHourFeed(feed)) limits.bodyBytes = (std::min)(limits.bodyBytes, marketHourBodyBytes);
                    const bool trades=tradeFeed(ticket->request.feed);
                    if (trades) limits.bodyBytes=(std::min)(limits.bodyBytes,maxPublicTradeBodyBytes);
                    const auto sentUtc=now.second;
                    PublicJsonResponse response;
                    try { response = fetch(ticket->request, cancel, limits); } catch (...) { response.status = PublicHttpsStatus::Unavailable; }
                    if (response.status == PublicHttpsStatus::Unreaped) {
                        // Cleanup uncertainty survives a simultaneous stop; it publishes no quote facts.
                        std::lock_guard<std::mutex> commit(commitMutex);
                        nativeCurrent();
                        plan.helperUnreaped(); publish(); break;
                    }
                    {std::lock_guard<std::mutex> commit(commitMutex);if(!nativeCurrent()||cancel.load())break;}
                    now = clock();
                    if (cancel.load()) break;
                    if (policy.publicTrades && !plan.observeClock(now.first,now.second)) break;
                    const MarketParseContext context{ticket->request, ticket->startedUtc, now.first, ticket->generation, origin,
                        fixture ? MarketMinuteConvention::FixtureMinuteStart : MarketMinuteConvention::Unqualified,
                        utcSeconds(policy,now.second)};
                    MarketDataResult<MarketQuote> quote;
                    MarketDataResult<MarketCandles> minutes;
                    std::shared_ptr<PublicHourObservation> hour;
                    MarketDataResult<MarketTradeObservation> trade;
                    bool conflictingTrade=false;
                    MarketDataStatus status;
                    if (publicHourFeed(feed)) {
                        hour = std::make_shared<PublicHourObservation>();
                        hour->start = static_cast<uint64_t>(ticket->request.fromUtc);
                        hour->end = static_cast<uint64_t>(ticket->request.toUtc);
                        hour->observedUtc = response.observedUtc;
                        hour->receivedMonoMs = now.first;
                        status = decodePublicHourPayload(response, feed == pearlHourFeed ? HourFeed::MexcPearl : HourFeed::CoinbaseFx,
                            hour->start, hour->end, ticket->startedUtc, context.receivedUtc, hour->minutes);
                        if (status != MarketDataStatus::Ok) hour.reset();
                    } else if (fixtureCandles(feed)) {
                        minutes = parseMarketCandles(response, context);
                        status = minutes.status;
                    } else if (trades) {
                        std::optional<MarketTradeTime> conflictingEvent;
                        trade=parsePublicTrade(response,ticket->request.feed,sentUtc,now.second,now.first,&conflictingEvent);
                        const auto &previous=feed==0 ? current.pearlTrade : current.fxTrade;
                        conflictingTrade=previous && conflictingEvent && *conflictingEvent>=previous->time;
                        if (trade.value && previous) {
                            conflictingTrade=trade.value->time==previous->time &&
                                trade.value->price.compare(previous->price)!=0;
                            if (conflictingTrade || trade.value->time<previous->time) {
                                trade.value.reset();
                                trade.status=conflictingTrade ? MarketDataStatus::Coverage : MarketDataStatus::Timestamp;
                            }
                        }
                        status=trade.status;
                    } else {
                        quote = parseMarketQuote(response, context);
                        status = quote.status;
                    }
                    now = clock();
                    {
                        // Serialize cancellation with cache commit, not with network/parse work. No late fact commits after stop.
                        std::lock_guard<std::mutex> commit(commitMutex);
                        if (!nativeCurrent()||cancel.load()) break;
                        if (plan.complete(*ticket, response, status, now.first, now.second)) {
                            if (quote.value) {
                                if (ticket->request.feed == PublicFeed::GatePearlTicker) current.pearlQuote = std::move(quote.value);
                                else current.fxQuote = std::move(quote.value);
                            }
                            if (minutes.value) {
                                if (ticket->request.feed == PublicFeed::GatePearlCandles) current.pearlCandles = std::move(minutes.value);
                                else current.fxCandles = std::move(minutes.value);
                            }
                            if (hour) (feed == pearlHourFeed ? current.pearlHour : current.fxHour) = std::move(hour);
                            if (trade.value || conflictingTrade)
                                (feed==0 ? current.pearlTrade : current.fxTrade)=std::move(trade.value);
                        }
                        publish();
                    }
                } else if (economicRequest) publish();
                std::unique_lock<std::mutex> lock(waitMutex);
                wait.wait_for(lock, std::chrono::milliseconds(20), [&] { return cancel.load(); });
            }
        } catch (...) { /* no provider exception escapes into mining; final snapshot is stopped/unavailable */ }
        if(nativeFaulted.load())plan.helperUnreaped();else plan.stop();
        try { publish(); } catch (...) {}
    }
};
MarketDataOwner::MarketDataOwner(MarketFetch fetch, MarketPollingPolicy policy, MarketOrigin origin, bool fixture,
                                 MarketClock clock) : impl_(new Impl()) {
    impl_->fetch = std::move(fetch); impl_->policy = policy; impl_->origin = origin; impl_->fixture = fixture;
    impl_->clock=clock ? std::move(clock) : MarketClock(policy.publicTrades ? marketTradeClock : marketSystemClock);
}
MarketDataOwner::~MarketDataOwner() { stop(); }
bool MarketDataOwner::start(uint64_t generation) {
    std::lock_guard<std::mutex> life(impl_->lifecycle);
    if (impl_->thread.joinable() || !impl_->fetch || (impl_->fixture && impl_->origin != MarketOrigin::SyntheticFixture) ||
        ((impl_->policy.publicHour || impl_->policy.publicTrades) && impl_->origin != MarketOrigin::ProviderResponse) ||
        (impl_->origin != MarketOrigin::SyntheticFixture && impl_->origin != MarketOrigin::ProviderResponse)) return false;
    try {
        {std::lock_guard<std::mutex> commit(impl_->commitMutex);if(!impl_->nativeLife.begin()){impl_->nativeCurrent();return false;}}
        const auto now = impl_->clock();
        std::lock_guard<std::mutex> commit(impl_->commitMutex);
        if(!impl_->nativeCurrent()||!impl_->nativeLife.permitted())return false;
        if (!impl_->plan.start(generation, now.first, now.second, impl_->policy, impl_->fixture)) return false;
        impl_->current = {}; impl_->current.generation = generation; impl_->current.origin = impl_->origin;
        impl_->cancel = false; impl_->publish();
        impl_->thread = std::thread([this] { impl_->run(); }); return true;
    } catch (...) { impl_->cancel = true; impl_->plan.stop(); try { impl_->publish(); } catch (...) {} return false; }
}
void MarketDataOwner::requestStop() {
    { std::lock_guard<std::mutex> commit(impl_->commitMutex); impl_->cancel = true; }
    impl_->wait.notify_all();
}
void MarketDataOwner::stop() {
    std::lock_guard<std::mutex> life(impl_->lifecycle);
    requestStop();
    if (impl_->thread.joinable()) impl_->thread.join();
}
std::shared_ptr<const MarketSnapshot> MarketDataOwner::snapshot() const {
    auto &s=*impl_;bool foreignScope=false;
    {std::lock_guard<std::mutex> commit(s.commitMutex);s.nativeCurrent();foreignScope=s.nativeLife.current()&&!s.nativeLife.permitted();}
    std::lock_guard<std::mutex> lock(s.mutex);
    if(foreignScope)return s.emptyTerminal;
    if(!s.nativeFaulted.load())return s.published;
    if(s.published->helperFault&&!s.published->running)return s.published;
    if(s.maskedSource!=s.published){try{auto value=*s.published;value.running=false;value.helperFault=true;
            for(auto &health:value.health){health.inFlight=false;health.latched=true;health.transport=PublicHttpsStatus::Unreaped;health.data=MarketDataStatus::Transport;health.nextDueMs=UINT64_MAX;}
            auto masked=std::make_shared<const MarketSnapshot>(std::move(value));s.masked=std::move(masked);s.maskedSource=s.published;
        }catch(...){return s.emptyNativeFault;}}
    return s.masked?s.masked:s.emptyNativeFault;
}

std::vector<std::string> marketTickerLines(const MarketSnapshot &s, const MarketFreshnessPolicy &policy,
                                          uint64_t utc, uint64_t ms) {
    std::vector<std::string> lines;
    auto latest = latestMarketReference(s.pearlQuote ? &*s.pearlQuote : nullptr, s.fxQuote ? &*s.fxQuote : nullptr, utc, ms, policy);
    if (!s.running || s.clockFault) { latest.tokenUsdt.reset(); latest.tokenUsd.reset(); latest.usdtUsd.reset(); }
    auto quoteLine = [&](const char *label, const std::optional<MarketQuote> &quote, const std::optional<ExactDecimal> &fresh) {
        lines.push_back(std::string(label) + ": " + (fresh ? fresh->text() : "N/A") + (quote && !fresh ? " | stale/invalid cached reference " + quote->last.text() : "") +
            (quote ? " | observed UTC " + std::to_string(quote->observation.observedUtc) + " | observation age " +
                (utc >= quote->observation.observedUtc && ms >= quote->observation.receivedMonoMs
                     ? std::to_string(utc - quote->observation.observedUtc) + "s" : "N/A") : " | no observation"));
    };
    quoteLine("Latest PRL/USDT [Gate international PEARL_USDT; Pearl mainnet]", s.pearlQuote, latest.tokenUsdt);
    quoteLine("Latest USDT/USD [Kraken USDTZUSD]", s.fxQuote, latest.usdtUsd);
    lines.push_back(std::string("Source health: Gate ") + publicHttpsStatusName(s.health[0].transport) + "/" + marketDataStatusName(s.health[0].data) +
        " | Kraken " + publicHttpsStatusName(s.health[2].transport) + "/" + marketDataStatusName(s.health[2].data));
    lines.push_back("Latest PRL/USD reference: " + (latest.tokenUsd ? latest.tokenUsd->text() : "N/A (missing/stale/skewed conversion)") +
        " | trade-event time NOT SUPPLIED; observation age is not trade age");
    return lines;
}

namespace {
std::vector<std::string> dashboardTradeLines(const MarketSnapshot &s,uint64_t utc,uint64_t ms) {
    // Dashboard receives whole UTC seconds. The upper end avoids overstating freshness by a partial second.
    const auto cutoff=utc && utc<=marketUtcLimit ? utc*1000+999 : 0;
    const bool current=s.running && !s.clockFault && !s.helperFault;
    const auto *pearl=current && s.pearlTrade ? &*s.pearlTrade : nullptr;
    const auto *fx=current && s.fxTrade ? &*s.fxTrade : nullptr;
    const auto reference=publicTradeReference(pearl,fx,cutoff,ms,TradeEventAge::WallAndMonotonic);
    std::vector<std::string> lines;
    const auto add=[&](const char *label,const MarketTradeObservation *trade,const MarketFeedHealth &health,bool fresh) {
        const std::string source(label);
        lines.push_back(source+": "+(fresh ? trade->price.text() : "N/A"));
        if (trade) {
            const auto ages=marketTradeAges(*trade,cutoff,ms,TradeEventAge::WallAndMonotonic);
            lines.push_back("  Event UTC "+marketTradeUtc(trade->time)+
                (ages ? " | age "+std::to_string(ages->eventMs)+" ms" : " | age unavailable"));
            lines.push_back("  Receipt UTC ms "+std::to_string(trade->receivedUtcMs)+
                (ages ? " | age "+std::to_string(ages->receiptMs)+" ms" : " | age unavailable"));
        }
        lines.push_back("  Source: "+std::string(marketTradeStatusName(health.data,health.transport))+
                        (trade && !fresh ? " | stale/invalid observation" : ""));
    };
    add("Gate PRL/USDT trade",pearl,s.health[0],reference.pearlFresh);
    add("Kraken USDT/USD trade",fx,s.health[2],reference.fxFresh);
    lines.push_back("PRL/USD trade reference: "+(reference.usd ? reference.usd->text() :
                    "N/A ("+std::string(reference.unavailable)+")"));
    return lines;
}

std::string displayedMean(const std::optional<MarketExactMean> &value) {
    std::string text;
    if (!value || !value->sum.formatRatio(value->count, 6, &text)) return "N/A";
    return text == "0.000000" && !value->sum.zero() ? "<0.000001" : text;
}

void appendPublicHour(std::vector<std::string> &lines, const MarketSnapshot &s, const MarketPollingPolicy &policy,
                      uint64_t utc, uint64_t ms) {
    const auto targetEnd = eligibleHourEnd(utc, policy.settlementGuardSeconds);
    const auto maxAge = policy.freshness.maxObservationAgeMs;
    const auto fresh = [&](const std::shared_ptr<const PublicHourObservation> &reading) {
        return s.running && !s.clockFault && !s.helperFault && s.origin == MarketOrigin::ProviderResponse &&
            reading && reading->end <= targetEnd && targetEnd - reading->end <= 60 &&
            utc >= reading->observedUtc && utc - reading->observedUtc <= maxAge / 1000 &&
            ms >= reading->receivedMonoMs && ms - reading->receivedMonoMs <= maxAge;
    };
    const auto *pearl = fresh(s.pearlHour) ? s.pearlHour.get() : nullptr;
    const auto *fx = fresh(s.fxHour) ? s.fxHour.get() : nullptr;
    const bool aligned = pearl && fx && pearl->start == fx->start && pearl->end == fx->end;
    MarketWindow window;
    if (pearl) {
        const std::vector<ReviewMinute> missing;
        window = calculateMarketHour(pearl->start, pearl->end, pearl->minutes, aligned ? fx->minutes : missing);
    }
    lines.push_back("1h mean of minute closes: " + displayedMean(window.tokenUsdt) + " USDT/PRL | " +
        displayedMean(window.tokenUsd) + " USD/PRL");
    lines.push_back("1h candles: TWAP approximation | candidate data, finality unverified");
    const auto describe = [&](unsigned feed, const char *label, const PublicHourObservation *reading) {
        std::string line = label;
        if (reading) {
            line += std::to_string(reading->minutes.size()) + "/60 | received " +
                std::to_string(utc - reading->observedUtc) + "s ago";
            if (reading->end != targetEnd) line += " | previous minute window";
        } else {
            line += "N/A (no fresh window)";
        }
        const auto &health = s.health[feed];
        lines.push_back(line + " | " + publicHttpsStatusName(health.transport) + "/" + marketDataStatusName(health.data));
    };
    describe(pearlHourFeed, "MEXC PEARLUSDT: ", pearl);
    describe(fxHourFeed, "Coinbase USDT-USD: ", fx);
    const auto interval = [](const PublicHourObservation &reading) {
        return "[" + std::to_string(reading.start) + ", " + std::to_string(reading.end) + ")";
    };
    if (pearl) lines.push_back("1h UTC seconds " + interval(*pearl));
    if (fx && !aligned) lines.push_back("FX UTC seconds " + interval(*fx));
    if (pearl && fx && !aligned) lines.push_back("1h USD unavailable: source windows differ");
}
}  // namespace

std::vector<std::string> marketSnapshotLines(const MarketSnapshot &s, const MarketPollingPolicy &policy, uint64_t utc, uint64_t ms) {
    auto lines=policy.publicTrades ? dashboardTradeLines(s,utc,ms) : marketTickerLines(s,policy.freshness,utc,ms);
    lines.insert(lines.begin(), std::string("Market references: ") + (s.helperFault ? "helper ownership fault" : s.clockFault ? "clock fault" : s.running ? "active" : "stopped") +
        (s.origin == MarketOrigin::SyntheticFixture ? " | SYNTHETIC FIXTURE - NOT LIVE" : " | public provider observations"));
    if (policy.publicHour) {
        appendPublicHour(lines, s, policy, utc, ms);
        lines.push_back("Reference prices are not executable proceeds, credited income or current holdings.");
        return lines;
    }
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
    lines.push_back("1h equal-time mean of 1m closes (TWAP approximation): " + displayedMean(window.tokenUsdt) + " USDT/PRL | " + displayedMean(window.tokenUsd) + " USD/PRL");
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
