#include "product/market_owner.h"
#include "product_test_util.h"
#include <chrono>
#include <thread>

using namespace om;
constexpr uint64_t E = 1800000000;
PublicJsonResponse okay() { PublicJsonResponse r; r.status = PublicHttpsStatus::Ok; r.httpStatus = 200; r.observedUtc = E + 10; return r; }
bool waitUntil(const std::function<bool()> &ready) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!ready()) { if (std::chrono::steady_clock::now() >= until) return false; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    return true;
}
std::string joined(const std::vector<std::string> &lines) { std::string text; for (const auto &line : lines) text += line + '\n'; return text; }
int main() {
    MarketPollingPolicy policy;
    CHECK(validMarketPollingPolicy(policy));
    auto invalid = policy; invalid.economicsMinutes = 720; CHECK(!validMarketPollingPolicy(invalid));
    invalid = policy; invalid.quoteRefreshMs = 999; CHECK(!validMarketPollingPolicy(invalid));
    invalid = policy; invalid.maxAttempts = 6; CHECK(!validMarketPollingPolicy(invalid));
    invalid = policy; invalid.freshness.maxObservationAgeMs = 0; CHECK(!validMarketPollingPolicy(invalid));
    MarketPollingPlan plan;
    CHECK(!plan.start(0, 0, E + 10, policy, false));
    CHECK(plan.start(1, 0, E + 10, policy, false));
    CHECK(!plan.start(2, 0, E + 10, policy, false));
    CHECK(plan.takeEconomicRequest(0, E + 10)); CHECK(!plan.takeEconomicRequest(0, E + 10));
    auto ticket = plan.next(0, E + 10);
    CHECK(ticket && ticket->request.feed == PublicFeed::GatePearlTicker);
    CHECK(!plan.next(0, E + 10));
    auto tampered = *ticket; ++tampered.generation;
    CHECK(!plan.complete(tampered, okay(), MarketDataStatus::Ok, 0, E + 10));
    CHECK(plan.complete(*ticket, okay(), MarketDataStatus::Ok, 0, E + 10));
    CHECK(!plan.complete(*ticket, okay(), MarketDataStatus::Ok, 0, E + 10));
    ticket = plan.next(0, E + 10); CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtTicker);
    CHECK(plan.complete(*ticket, okay(), MarketDataStatus::Ok, 0, E + 10));
    CHECK(!plan.next(1000, E + 11));
    CHECK(!plan.takeEconomicRequest(3599999, E + 3609)); CHECK(plan.takeEconomicRequest(3600000, E + 3610));
    plan.stop(); CHECK(!plan.next(3600000, E + 3610));
    CHECK(!plan.start(1, 0, E + 10, policy, false)); CHECK(plan.start(2, 0, E + 10, policy, true));
    ticket = plan.next(0, E + 10); CHECK(ticket && ticket->request.feed == PublicFeed::GatePearlTicker);
    plan.complete(*ticket, okay(), MarketDataStatus::Ok, 0, E + 10);
    ticket = plan.next(0, E + 10); CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtTicker);
    plan.complete(*ticket, okay(), MarketDataStatus::Ok, 0, E + 10);
    CHECK(!plan.next(999, E + 10));
    ticket = plan.next(1000, E + 11); CHECK(ticket && ticket->request.feed == PublicFeed::GatePearlCandles);
    CHECK(ticket->request.fromUtc == E - 480 * 60 && ticket->request.toUtc == E);
    std::string url; CHECK(publicFeedUrl(ticket->request, &url));
    CHECK(url.find("interval=1m") != std::string::npos && url.find("limit=") == std::string::npos && url.find("page=") == std::string::npos);
    plan.complete(*ticket, okay(), MarketDataStatus::Ok, 1000, E + 11);
    ticket = plan.next(1000, E + 11); CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtCandles);
    plan.complete(*ticket, okay(), MarketDataStatus::Ok, 1000, E + 11);
    plan.stop();
    auto fast = policy; fast.quoteRefreshMs = 1000;
    CHECK(plan.start(3, 0, E + 10, fast, true));
    for (int i = 0; i < 2; ++i) { ticket = plan.next(0, E + 10); CHECK(ticket.has_value()); plan.complete(*ticket, okay(), MarketDataStatus::Ok, 0, E + 10); }
    ticket = plan.next(1000, E + 11); CHECK(ticket && ticket->request.feed == PublicFeed::GatePearlCandles);  // overdue candles not starved by fast ticker
    plan.stop();
    CHECK(plan.start(4, 0, E + 10, policy, false));
    auto failure = okay(); failure.status = PublicHttpsStatus::Network; failure.httpStatus = 0;
    ticket = plan.next(0, E + 10); plan.complete(*ticket, failure, MarketDataStatus::Transport, 0, E + 10);
    CHECK(plan.health()[0].attempts == 1 && plan.health()[0].nextDueMs == 5000);
    ticket = plan.next(0, E + 10); plan.complete(*ticket, okay(), MarketDataStatus::Ok, 0, E + 10);
    CHECK(!plan.next(4999, E + 14));
    ticket = plan.next(5000, E + 15); CHECK(ticket && ticket->request.feed == PublicFeed::GatePearlTicker);
    plan.complete(*ticket, failure, MarketDataStatus::Transport, 5000, E + 15);
    CHECK(plan.health()[0].attempts == 2 && plan.health()[0].nextDueMs == 15000);
    ticket = plan.next(15000, E + 25); CHECK(ticket && ticket->request.feed == PublicFeed::GatePearlTicker);
    plan.complete(*ticket, failure, MarketDataStatus::Transport, 15000, E + 25);
    CHECK(plan.health()[0].attempts == 3 && plan.health()[0].nextDueMs == 315000);
    CHECK(!plan.health()[0].latched);
    plan.stop(); CHECK(plan.start(5, 0, E + 10, policy, false));
    failure.status = PublicHttpsStatus::HttpStatus; failure.httpStatus = 429; failure.retryAfterSeconds = 30;
    ticket = plan.next(0, E + 10); plan.complete(*ticket, failure, MarketDataStatus::Transport, 0, E + 10);
    CHECK(plan.health()[0].nextDueMs == 30000);
    plan.stop(); CHECK(plan.start(6, 0, E + 10, policy, false));
    failure.retryAfterSeconds = 3600;
    ticket = plan.next(0, E + 10); plan.complete(*ticket, failure, MarketDataStatus::Transport, 0, E + 10);
    CHECK(plan.health()[0].latched);
    for (const auto &error : {PublicHttpsStatus::TlsVerification, PublicHttpsStatus::TlsProtocol, PublicHttpsStatus::DestinationBlocked, PublicHttpsStatus::InvalidJson}) {
        plan.stop(); static uint64_t generation = 6; CHECK(plan.start(++generation, 0, E + 10, policy, false));
        failure.status = error; failure.httpStatus = 0; failure.retryAfterSeconds = 0;
        ticket = plan.next(0, E + 10); plan.complete(*ticket, failure, MarketDataStatus::Transport, 0, E + 10);
        CHECK(plan.health()[0].latched && plan.health()[0].nextDueMs == UINT64_MAX);
    }
    plan.stop(); CHECK(plan.start(100, 0, E + 10, policy, false));
    ticket = plan.next(0, E + 10); plan.stop(); CHECK(!plan.complete(*ticket, okay(), MarketDataStatus::Ok, 0, E + 10));
    CHECK(plan.start(101, 0, E + 10, policy, false)); CHECK(!plan.next(0, E + 9)); CHECK(plan.clockFault() && !plan.active());
    CHECK(plan.start(102, 1000, E + 10, policy, false)); CHECK(!plan.next(999, E + 10)); CHECK(plan.clockFault());
    CHECK(plan.start(103, 0, E + 10, policy, false)); CHECK(!plan.next(0, E + 16)); CHECK(plan.clockFault());
    CHECK(plan.start(104, 0, E + 10, policy, false));
    ticket = plan.next(1000, E + 11); CHECK(ticket.has_value());
    CHECK(!plan.next(1001, E + 10)); CHECK(plan.clockFault());  // even a small rollback after start is latched
    CHECK(plan.start(105, 0, E + 10, policy, false));
    ticket = plan.next(0, E + 10); plan.complete(*ticket, okay(), MarketDataStatus::RateLimited, 0, E + 10);
    CHECK(!plan.health()[0].latched && plan.health()[0].nextDueMs == 5000);
    plan.stop();

    std::atomic<uint64_t> ms{0};
    std::atomic<unsigned> pearlCalls{0}, fxCalls{0};
    auto clock = [&] { const auto t = ms.load(); return std::make_pair(t, E + 10 + t / 1000); };
    auto fetch = [&](const PublicJsonRequest &request, const std::atomic<bool> &, PublicHttpsLimits) {
        auto r = okay(); r.observedUtc = clock().second;
        if (request.feed == PublicFeed::GatePearlTicker) {
            const auto call = ++pearlCalls;
            r.body = "[{\"currency_pair\":\"PEARL_USDT\",\"last\":\"2.50\"}]";
            if (call == 2) { r.status = PublicHttpsStatus::Network; r.httpStatus = 0; r.body.clear(); }
            if (call == 3) r.body = "[{\"currency_pair\":\"WRONG_USDT\",\"last\":\"3\"}]";
        } else { ++fxCalls; r.body = "{\"error\":[],\"result\":{\"USDTZUSD\":{\"c\":[\"0.98\",\"1\"]}}}"; }
        return r;
    };
    MarketDataOwner owner(fetch, policy, MarketOrigin::SyntheticFixture, false, clock);
    CHECK(pearlCalls == 0 && fxCalls == 0 && !owner.snapshot()->running);
    CHECK(owner.start(1)); CHECK(!owner.start(2));
    CHECK(waitUntil([&] { auto s = owner.snapshot(); return s->pearlQuote && s->fxQuote; }));
    auto first = owner.snapshot();
    CHECK(first->economicRequests == 1 && first->generation == 1);
    auto text = joined(marketSnapshotLines(*first, policy, E + 10, 0));
    CHECK(text.find("SYNTHETIC FIXTURE - NOT LIVE") != std::string::npos);
    CHECK(text.find("Latest PRL/USD reference: 2.45") != std::string::npos);
    CHECK(text.find("trade-event time NOT SUPPLIED") != std::string::npos);
    CHECK(text.find("N/A (time convention unqualified/unavailable)") != std::string::npos);
    ms = 60000;
    CHECK(waitUntil([&] { return owner.snapshot()->health[0].transport == PublicHttpsStatus::Network; }));
    CHECK(owner.snapshot()->pearlQuote->last.text() == "2.5");
    ms = 120001;
    CHECK(waitUntil([&] { return owner.snapshot()->health[0].latched; }));
    const auto later = owner.snapshot();
    CHECK(later->pearlQuote->observation.observedUtc == E + 10);  // failures never freshen the old quote
    CHECK(first->revision < later->revision && first->pearlQuote->last.text() == "2.5");
    text = joined(marketSnapshotLines(*later, policy, E + 130, 120001));
    CHECK(text.find("stale/invalid cached reference 2.5") != std::string::npos);
    CHECK(text.find("Latest PRL/USD reference: N/A") != std::string::npos);
    CHECK(later->economicRequests == 1);  // refresh clock has not advanced the independent hourly request clock
    ms = 0;
    CHECK(waitUntil([&] { return owner.snapshot()->clockFault; }));
    owner.stop(); CHECK(!owner.snapshot()->running);
    CHECK(!owner.start(1)); CHECK(owner.start(2)); owner.stop(); CHECK(owner.snapshot()->generation == 2);

    std::atomic<bool> entered{false};
    std::atomic<unsigned> stoppedCalls{0};
    MarketDataOwner cancellable([&](const PublicJsonRequest &, const std::atomic<bool> &cancel, PublicHttpsLimits) {
        ++stoppedCalls; entered = true;
        while (!cancel.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return okay();  // even an apparent successful late response cannot overwrite the stopped generation
    }, policy, MarketOrigin::SyntheticFixture, false, clock);
    CHECK(cancellable.start(1)); CHECK(waitUntil([&] { return entered.load(); }));
    const auto start = std::chrono::steady_clock::now(); cancellable.stop();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
    CHECK(!cancellable.snapshot()->pearlQuote && !cancellable.snapshot()->running && stoppedCalls == 1);
    MarketDataOwner unqualified(fetch, policy, MarketOrigin::ProviderResponse, true, clock);
    CHECK(!unqualified.start(1));
    MarketDataOwner badPolicy(fetch, invalid, MarketOrigin::SyntheticFixture, false, clock);
    CHECK(!badPolicy.start(1));
    CHECK(!marketDashboardView(MarketSnapshot{}, policy, E + 10, 0).enabled);
    std::atomic<unsigned> periodicFetches{0};
    ms = 0;
    auto independent = policy; independent.economicIntervalMs = 1000;
    MarketDataOwner economicClock([&](const PublicJsonRequest &, const std::atomic<bool> &, PublicHttpsLimits) {
        ++periodicFetches; auto r = okay(); r.status = PublicHttpsStatus::TlsVerification; return r;
    }, independent, MarketOrigin::SyntheticFixture, false, clock);
    CHECK(economicClock.start(1)); CHECK(waitUntil([&] { return periodicFetches == 2; }));
    ms = 1000;
    CHECK(waitUntil([&] { return economicClock.snapshot()->economicRequests == 2; }));
    CHECK(periodicFetches == 2);  // economic request timer is published without starting an extra feed request
    economicClock.stop();
    ms = 0;
    MarketDataOwner allFeeds([&](const PublicJsonRequest &request, const std::atomic<bool> &, PublicHttpsLimits) {
        auto r = okay(); r.observedUtc = clock().second;
        if (request.feed == PublicFeed::GatePearlTicker) r.body = "[{\"currency_pair\":\"PEARL_USDT\",\"last\":\"2.5\"}]";
        else if (request.feed == PublicFeed::KrakenUsdtTicker) r.body = "{\"error\":[],\"result\":{\"USDTZUSD\":{\"c\":[\"0.98\",\"1\"]}}}";
        else {
            const bool gate = request.feed == PublicFeed::GatePearlCandles;
            r.body = gate ? "[" : "{\"error\":[],\"result\":{\"USDTZUSD\":[";
            for (unsigned i = 0; i < 480; ++i) {
                if (i) r.body += ',';
                const auto t = std::to_string(E - 480 * 60 + i * 60);
                r.body += gate ? "[\"" + t + "\",\"0\",\"2\",\"3\",\"1\",\"2\",\"0\",\"true\"]" :
                    "[" + t + ",\"1\",\"1\",\"1\",\"1\",\"1\",\"0\",0]";
            }
            r.body += gate ? "]" : ",[1800000000,\"1\",\"1\",\"1\",\"1\",\"1\",\"0\",0]],\"last\":1800000000}}";
        }
        return r;
    }, policy, MarketOrigin::SyntheticFixture, true, clock);
    CHECK(allFeeds.start(1));
    CHECK(waitUntil([&] { return allFeeds.snapshot()->pearlQuote && allFeeds.snapshot()->fxQuote; }));
    ms = 1000;
    CHECK(waitUntil([&] { return allFeeds.snapshot()->pearlCandles && allFeeds.snapshot()->fxCandles; }));
    const auto complete = allFeeds.snapshot();
    CHECK(complete->pearlCandles->minutes.size() == 480 && complete->fxCandles->minutes.size() == 480);
    text = joined(marketSnapshotLines(*complete, policy, E + 11, 1000));
    CHECK(text.find("Pearl 60/60 | FX 60/60") != std::string::npos && text.find("2.000000 USD/PRL") != std::string::npos);
    CHECK(marketWindow(*complete->pearlCandles, &*complete->fxCandles, E - 480 * 60, E).tokenUsd.has_value());
    allFeeds.stop();
    return finishTests("market_owner");
}
