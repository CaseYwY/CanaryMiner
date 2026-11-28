#include "product/market_owner.h"
#include "product/market_hour_payload.h"
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
    for (const unsigned delay : {30u, maxPublicRetryAfterSeconds}) {
        MarketPollingPlan throttle;
        CHECK(throttle.start(1, 0, E + 10, fast, true));
        ticket = throttle.next(0, E + 10);
        CHECK(ticket && ticket->request.feed == PublicFeed::GatePearlTicker);
        auto retry = failure;
        retry.httpStatus = 503;
        retry.retryAfterSeconds = delay;
        CHECK(ticket && throttle.complete(*ticket, retry, MarketDataStatus::Transport, 0, E + 10));
        ticket = throttle.next(0, E + 10);
        CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtTicker);
        CHECK(ticket && throttle.complete(*ticket, okay(), MarketDataStatus::Ok, 0, E + 10));
        ticket = throttle.next(1000, E + 11);
        CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtCandles);
        CHECK(ticket && throttle.complete(*ticket, okay(), MarketDataStatus::Ok, 1000, E + 11));
        const auto expectKraken = [&](uint64_t ms) {
            auto next = throttle.next(ms, E + 10 + ms / 1000);
            CHECK(next && next->request.feed == PublicFeed::KrakenUsdtTicker);
            CHECK(next && throttle.complete(*next, okay(), MarketDataStatus::Ok, ms, E + 10 + ms / 1000));
        };
        expectKraken(29000); // only the Gate sibling stays delayed
        if (delay == maxPublicRetryAfterSeconds) {
            CHECK(throttle.health()[0].latched);
            expectKraken(3600000); // capped instructions outlast the normal retry window
        } else {
            ticket = throttle.next(30000, E + 40);
            CHECK(ticket && ticket->request.feed == PublicFeed::GatePearlCandles);
        }
    }
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

    auto hours = policy;
    hours.publicHour = true;
    hours.scheduleEconomicRequests = false;
    MarketPollingPlan hourPlan;
    CHECK(!hourPlan.start(1, 0, E + 64, hours, true));
    CHECK(hourPlan.start(1, 0, E + 64, hours, false));
    for (auto feed : {PublicFeed::GatePearlTicker, PublicFeed::KrakenUsdtTicker}) {
        ticket = hourPlan.next(0, E + 64);
        CHECK(ticket && ticket->request.feed == feed);
        CHECK(ticket && hourPlan.complete(*ticket, okay(), MarketDataStatus::Ok, 0, E + 64));
    }
    ticket = hourPlan.next(0, E + 64);
    CHECK(ticket && ticket->request.feed == PublicFeed::MexcPearlHour &&
        ticket->request.fromUtc == E - 3600 && ticket->request.toUtc == E);
    CHECK(ticket && hourPlan.complete(*ticket, okay(), MarketDataStatus::Ok, 1000, E + 65));
    ticket = hourPlan.next(2000, E + 66);
    CHECK(ticket && ticket->request.feed == PublicFeed::CoinbaseUsdtHour &&
        ticket->request.fromUtc == E - 3600 && ticket->request.toUtc == E);
    failure.status = PublicHttpsStatus::HttpStatus; failure.httpStatus = 429; failure.retryAfterSeconds = 100;
    CHECK(ticket && hourPlan.complete(*ticket, failure, MarketDataStatus::Transport, 2000, E + 66));
    CHECK(!hourPlan.next(59999, E + 123));
    for (auto feed : {PublicFeed::GatePearlTicker, PublicFeed::KrakenUsdtTicker}) {
        ticket = hourPlan.next(60000, E + 124);
        CHECK(ticket && ticket->request.feed == feed);
        CHECK(ticket && hourPlan.complete(*ticket, okay(), MarketDataStatus::Ok, 60000, E + 124));
    }
    ticket = hourPlan.next(61000, E + 125);
    CHECK(ticket && ticket->request.feed == PublicFeed::MexcPearlHour && ticket->request.toUtc == E + 120);
    CHECK(ticket && hourPlan.complete(*ticket, okay(), MarketDataStatus::Ok, 61000, E + 125));
    CHECK(!hourPlan.next(101999, E + 165));
    ticket = hourPlan.next(102000, E + 166);
    CHECK(ticket && ticket->request.feed == PublicFeed::CoinbaseUsdtHour && ticket->request.toUtc == E + 120);
    CHECK(ticket && hourPlan.complete(*ticket, okay(), MarketDataStatus::Ok, 102000, E + 166));
    CHECK(!hourPlan.takeEconomicRequest(102000, E + 166));
    hourPlan.stop();

    auto fallbackPolicy = hours;
    fallbackPolicy.publicHourKrakenFallback = true;
    auto invalidFallback = fallbackPolicy;
    invalidFallback.publicHour = false;
    CHECK(!validMarketPollingPolicy(invalidFallback));
    invalidFallback = fallbackPolicy;
    invalidFallback.scheduleEconomicRequests = true;
    CHECK(!validMarketPollingPolicy(invalidFallback));
    MarketPollingPlan fallbackPlan;
    CHECK(fallbackPlan.start(1, 0, E + 10, fallbackPolicy, false));
    for (auto feed : {PublicFeed::GatePearlTicker, PublicFeed::KrakenUsdtTicker,
                      PublicFeed::MexcPearlHour, PublicFeed::CoinbaseUsdtHour}) {
        ticket = fallbackPlan.next(0, E + 10);
        CHECK(ticket && ticket->request.feed == feed);
        CHECK(ticket && fallbackPlan.complete(*ticket, okay(), MarketDataStatus::Ok, 0, E + 10));
    }
    fallbackPlan.setPublicHourFallback(E);
    CHECK(!fallbackPlan.next(999, E + 10)); // same Kraken endpoint budget as quotes
    ticket = fallbackPlan.next(1000, E + 11);
    CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtCandles &&
          ticket->request.fromUtc == E - 3600 && ticket->request.toUtc == E);
    failure.retryAfterSeconds = 100;
    CHECK(ticket && fallbackPlan.complete(*ticket, failure, MarketDataStatus::Transport, 1000, E + 11));
    CHECK(!fallbackPlan.next(59000, E + 69));
    for (auto feed : {PublicFeed::GatePearlTicker, PublicFeed::MexcPearlHour, PublicFeed::CoinbaseUsdtHour}) {
        ticket = fallbackPlan.next(60000, E + 70);
        CHECK(ticket && ticket->request.feed == feed);
        CHECK(ticket && fallbackPlan.complete(*ticket, okay(), MarketDataStatus::Ok, 60000, E + 70));
    }
    CHECK(!fallbackPlan.next(100999, E + 110));
    ticket = fallbackPlan.next(101000, E + 111);
    CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtTicker);
    CHECK(ticket && fallbackPlan.complete(*ticket, okay(), MarketDataStatus::Ok, 101000, E + 111));
    CHECK(!fallbackPlan.next(102000, E + 112));  // a rate-limited hour gets no second attempt
    fallbackPlan.stop();

    for (const auto &[http, retryAfter] : {std::pair{429, 30u}, std::pair{429, 3600u},
                                             std::pair{200, 0u}, std::pair{503, 30u}}) {
        MarketPollingPlan limitedQuote;
        CHECK(limitedQuote.start(1, 0, E + 10, fallbackPolicy, false));
        for (auto feed : {PublicFeed::GatePearlTicker, PublicFeed::KrakenUsdtTicker,
                          PublicFeed::MexcPearlHour, PublicFeed::CoinbaseUsdtHour}) {
            ticket = limitedQuote.next(0, E + 10);
            CHECK(ticket && ticket->request.feed == feed);
            auto rateLimit = failure;
            rateLimit.httpStatus = http;
            rateLimit.retryAfterSeconds = retryAfter;
            if (http == 200) rateLimit.status = PublicHttpsStatus::Ok;
            const bool quoteLimited = feed == PublicFeed::KrakenUsdtTicker;
            const auto data = quoteLimited ? (http == 200 ? MarketDataStatus::RateLimited : MarketDataStatus::Transport) :
                MarketDataStatus::Ok;
            CHECK(ticket && limitedQuote.complete(*ticket, quoteLimited ? rateLimit : okay(), data, 0, E + 10));
        }
        limitedQuote.setPublicHourFallback(E);
        CHECK(!limitedQuote.next(1000, E + 11));  // a quote's refusal also holds the Kraken fallback lane
        if (retryAfter < 3600) {
            const auto delay = retryAfter ? retryAfter : 5;
            CHECK(!limitedQuote.next(delay * 1000 - 1, E + 9 + delay));
            ticket = limitedQuote.next(delay * 1000, E + 10 + delay);
            CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtCandles);
        } else {
            limitedQuote.setPublicHourFallback(E + 3600);
            for (auto feed : {PublicFeed::GatePearlTicker, PublicFeed::MexcPearlHour, PublicFeed::CoinbaseUsdtHour}) {
                ticket = limitedQuote.next(3600000, E + 3610);
                CHECK(ticket && ticket->request.feed == feed);
                CHECK(ticket && limitedQuote.complete(*ticket, okay(), MarketDataStatus::Ok, 3600000, E + 3610));
            }
            CHECK(!limitedQuote.next(3600000, E + 3610));  // capped Retry-After latches the provider budget
        }
    }

    const auto serviceThrottle = [](std::string_view deadline) {
        auto reply = okay();
        reply.observedUtc = E + 11;
        reply.body = R"({"error":["EService: Throttled: )" + std::string(deadline) + R"("]})";
        return reply;
    };
    const auto throttle = serviceThrottle(std::to_string(E + 40));
    MarketParseContext quoteContext{{PublicFeed::KrakenUsdtTicker}, E + 11, 1000, 1, MarketOrigin::ProviderResponse,
        MarketMinuteConvention::Unqualified, E + 11};
    const auto throttledQuote = parseMarketQuote(throttle, quoteContext);
    CHECK(throttledQuote.status == MarketDataStatus::ProviderThrottled && !throttledQuote.value);
    const auto throttledTrade = parsePublicTrade(throttle, PublicFeed::KrakenUsdtTrades,
        (E + 11) * 1000, (E + 11) * 1000, 1000);
    CHECK(throttledTrade.status == MarketDataStatus::ProviderThrottled && !throttledTrade.value);
    CHECK(market_detail::krakenThrottleUtc(throttle) == E + 40);
    CHECK(!market_detail::krakenThrottleUtc(serviceThrottle("invalid")));

    MarketPollingPlan quoteThrottle;
    CHECK(quoteThrottle.start(1, 0, E + 10, fallbackPolicy, false));
    for (auto feed : {PublicFeed::GatePearlTicker, PublicFeed::KrakenUsdtTicker,
                      PublicFeed::MexcPearlHour, PublicFeed::CoinbaseUsdtHour}) {
        ticket = quoteThrottle.next(1000, E + 11);
        CHECK(ticket && ticket->request.feed == feed);
        const bool limited = feed == PublicFeed::KrakenUsdtTicker;
        CHECK(ticket && quoteThrottle.complete(*ticket, limited ? throttle : okay(),
            limited ? throttledQuote.status : MarketDataStatus::Ok, 1000, E + 11,
            limited ? market_detail::krakenThrottleUtc(throttle) : 0));
    }
    quoteThrottle.setPublicHourFallback(E);
    CHECK(!quoteThrottle.next(30999, E + 40));
    ticket = quoteThrottle.next(31000, E + 41);
    CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtCandles);

    for (const auto &deadline : {std::string("invalid"), std::to_string(E + 3610)}) {
        MarketPollingPlan planWithHold;
        CHECK(planWithHold.start(1, 0, E + 10, fallbackPolicy, false));
        ticket = planWithHold.next(0, E + 10);
        CHECK(ticket && ticket->request.feed == PublicFeed::GatePearlTicker);
        CHECK(ticket && planWithHold.complete(*ticket, okay(), MarketDataStatus::Ok, 0, E + 10));
        ticket = planWithHold.next(0, E + 10);
        CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtTicker);
        const auto reply = serviceThrottle(deadline);
        CHECK(ticket && planWithHold.complete(*ticket, reply, MarketDataStatus::ProviderThrottled, 0, E + 10,
              market_detail::krakenThrottleUtc(reply)));
        planWithHold.setPublicHourFallback(E + 3600);
        for (auto feed : {PublicFeed::MexcPearlHour, PublicFeed::CoinbaseUsdtHour, PublicFeed::GatePearlTicker}) {
            ticket = planWithHold.next(3600000, E + 3610);
            CHECK(ticket && ticket->request.feed == feed);
            CHECK(ticket && planWithHold.complete(*ticket, okay(), MarketDataStatus::Ok, 3600000, E + 3610));
        }
        CHECK(!planWithHold.next(3600000, E + 3610));
    }

    auto fastFallback = fallbackPolicy;
    fastFallback.quoteRefreshMs = 1000;
    MarketPollingPlan limitedOhlc;
    CHECK(limitedOhlc.start(1, 0, E + 10, fastFallback, false));
    for (auto feed : {PublicFeed::GatePearlTicker, PublicFeed::KrakenUsdtTicker,
                      PublicFeed::MexcPearlHour, PublicFeed::CoinbaseUsdtHour}) {
        ticket = limitedOhlc.next(0, E + 10);
        CHECK(ticket && ticket->request.feed == feed);
        CHECK(ticket && limitedOhlc.complete(*ticket, okay(), MarketDataStatus::Ok, 0, E + 10));
    }
    limitedOhlc.setPublicHourFallback(E);
    ticket = limitedOhlc.next(1000, E + 11);
    CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtCandles);
    auto limitedReply = okay();
    limitedReply.observedUtc = E + 11;
    limitedReply.body = R"({"error":["EAPI:Rate limit exceeded"]})";
    std::vector<market_detail::ReviewMinute> minutes;
    const auto limitedStatus = market_detail::decodePublicHourPayload(limitedReply, market_detail::HourFeed::KrakenFx,
        E - 3600, E, E + 11, E + 11, minutes);
    CHECK(limitedStatus == MarketDataStatus::RateLimited && minutes.empty());
    CHECK(ticket && limitedOhlc.complete(*ticket, limitedReply, limitedStatus, 1000, E + 11));
    ticket = limitedOhlc.next(1000, E + 11);
    CHECK(ticket && ticket->request.feed == PublicFeed::GatePearlTicker);
    CHECK(ticket && limitedOhlc.complete(*ticket, okay(), MarketDataStatus::Ok, 1000, E + 11));
    ticket = limitedOhlc.next(2000, E + 12);
    CHECK(ticket && ticket->request.feed == PublicFeed::GatePearlTicker);
    CHECK(ticket && limitedOhlc.complete(*ticket, okay(), MarketDataStatus::Ok, 2000, E + 12));
    ticket = limitedOhlc.next(6000, E + 16);
    CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtTicker);

    MarketPollingPlan ohlcThrottle;
    CHECK(ohlcThrottle.start(1, 0, E + 10, fastFallback, false));
    for (auto feed : {PublicFeed::GatePearlTicker, PublicFeed::KrakenUsdtTicker,
                      PublicFeed::MexcPearlHour, PublicFeed::CoinbaseUsdtHour}) {
        ticket = ohlcThrottle.next(0, E + 10);
        CHECK(ticket && ticket->request.feed == feed);
        CHECK(ticket && ohlcThrottle.complete(*ticket, okay(), MarketDataStatus::Ok, 0, E + 10));
    }
    ohlcThrottle.setPublicHourFallback(E);
    ticket = ohlcThrottle.next(1000, E + 11);
    CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtCandles);
    const auto throttleStatus = market_detail::decodePublicHourPayload(throttle, market_detail::HourFeed::KrakenFx,
        E - 3600, E, E + 11, E + 11, minutes);
    CHECK(throttleStatus == MarketDataStatus::ProviderThrottled && minutes.empty());
    CHECK(ticket && ohlcThrottle.complete(*ticket, throttle, throttleStatus, 1000, E + 11,
        market_detail::krakenThrottleUtc(throttle)));
    ticket = ohlcThrottle.next(30999, E + 40);
    CHECK(ticket && ticket->request.feed == PublicFeed::GatePearlTicker);
    CHECK(ticket && ohlcThrottle.complete(*ticket, okay(), MarketDataStatus::Ok, 30999, E + 40));
    ticket = ohlcThrottle.next(31000, E + 41);
    CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtTicker);

    MarketPollingPlan recurringThrottle;
    CHECK(recurringThrottle.start(1, 0, E + 10, fastFallback, false));
    const auto elapsedDeadline = serviceThrottle(std::to_string(E + 10));
    for (const uint64_t tick : {0u, 5000u, 15000u}) {
        const auto utc = E + 10 + tick / 1000;
        ticket = recurringThrottle.next(tick, utc);
        CHECK(ticket && ticket->request.feed == PublicFeed::GatePearlTicker);
        CHECK(ticket && recurringThrottle.complete(*ticket, okay(), MarketDataStatus::Ok, tick, utc));
        ticket = recurringThrottle.next(tick, utc);
        CHECK(ticket && ticket->request.feed == PublicFeed::KrakenUsdtTicker);
        CHECK(ticket && recurringThrottle.complete(*ticket, elapsedDeadline, MarketDataStatus::ProviderThrottled, tick, utc,
            market_detail::krakenThrottleUtc(elapsedDeadline)));
        if (!tick) {
            for (auto feed : {PublicFeed::MexcPearlHour, PublicFeed::CoinbaseUsdtHour}) {
                ticket = recurringThrottle.next(tick, utc);
                CHECK(ticket && ticket->request.feed == feed);
                CHECK(ticket && recurringThrottle.complete(*ticket, okay(), MarketDataStatus::Ok, tick, utc));
            }
        }
    }
    const auto &heldTicker = recurringThrottle.health()[static_cast<unsigned>(PublicFeed::KrakenUsdtTicker)];
    CHECK(!heldTicker.latched && heldTicker.attempts == 3 && heldTicker.nextDueMs == 315000);

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
    const auto afterNetworkFailure = owner.snapshot();
    CHECK(afterNetworkFailure->pearlQuote &&
        afterNetworkFailure->pearlQuote->last.text() == "2.5" &&
        afterNetworkFailure->pearlQuote->observation.observedUtc == E + 10);
    ms = 120001;
    CHECK(waitUntil([&] { return owner.snapshot()->health[0].latched; }));
    const auto later = owner.snapshot();
    CHECK(!later->pearlQuote);  // conflicting identity withdraws the formerly accepted quote
    CHECK(first->revision < later->revision && first->pearlQuote->last.text() == "2.5");
    text = joined(marketSnapshotLines(*later, policy, E + 130, 120001));
    CHECK(text.find("stale/invalid cached reference 2.5") == std::string::npos);
    CHECK(text.find("Latest PRL/USD reference: N/A") != std::string::npos);
    CHECK(later->economicRequests == 1);  // refresh clock has not advanced the independent hourly request clock
    ms = 0;
    CHECK(waitUntil([&] { return owner.snapshot()->clockFault; }));
    owner.stop(); CHECK(!owner.snapshot()->running);
    CHECK(!owner.start(1)); CHECK(owner.start(2)); owner.stop(); CHECK(owner.snapshot()->generation == 2);

    ms = 0;
    std::atomic<unsigned> krakenRequests{0};
    MarketDataOwner throttledOwner([&](const PublicJsonRequest &request, const std::atomic<bool> &, PublicHttpsLimits) {
        auto response = okay();
        response.observedUtc = clock().second;
        if (request.feed == PublicFeed::GatePearlTicker)
            response.body = "[{\"currency_pair\":\"PEARL_USDT\",\"last\":\"2.50\"}]";
        else if (++krakenRequests == 1)
            response.body = "{\"error\":[\"EService: Throttled: " + std::to_string(E + 20) + "\"]}";
        else
            response.body = "{\"error\":[],\"result\":{\"USDTZUSD\":{\"c\":[\"0.98\",\"1\"]}}}";
        return response;
    }, policy, MarketOrigin::SyntheticFixture, false, clock);
    CHECK(throttledOwner.start(1));
    CHECK(waitUntil([&] { return throttledOwner.snapshot()->health[2].data == MarketDataStatus::ProviderThrottled; }));
    const auto krakenWait = throttledOwner.snapshot()->health[2];
    CHECK(krakenRequests == 1 && !krakenWait.latched && krakenWait.nextDueMs == 11000);
    ms = 11000;
    CHECK(waitUntil([&] { return throttledOwner.snapshot()->fxQuote.has_value(); }));
    CHECK(krakenRequests == 2);
    throttledOwner.stop();

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
    MarketPollingPlan lifecycle;
    CHECK(lifecycle.start(1,0,E+10,policy,false));
    ticket=lifecycle.next(0,E+10);CHECK(ticket.has_value());
    failure={};failure.status=PublicHttpsStatus::Unreaped;
    auto wrongLoss=*ticket;++wrongLoss.serial;
    CHECK(!lifecycle.complete(wrongLoss,failure,MarketDataStatus::Transport,0,E+10)&&lifecycle.active()&&!lifecycle.helperFault());
    auto wrongHeight = *ticket;
    wrongHeight.request.blockHeight = 0;
    CHECK(!lifecycle.complete(wrongHeight,failure,MarketDataStatus::Transport,0,E+10)&&lifecycle.active()&&!lifecycle.helperFault());
    auto wrongRate = *ticket;
    wrongRate.request.octopus = OctopusRatePage{"AGILE-24-02-29", 'C', 1};
    CHECK(!lifecycle.complete(wrongRate,failure,MarketDataStatus::Transport,0,E+10)&&lifecycle.active()&&!lifecycle.helperFault());
    CHECK(lifecycle.complete(*ticket,failure,MarketDataStatus::Transport,0,E+9));  // a concurrent clock problem cannot hide ownership loss
    CHECK(lifecycle.helperFault()&&!lifecycle.active()&&!lifecycle.next(0,E+10)&&!lifecycle.takeEconomicRequest(0,E+10));
    CHECK(lifecycle.health()[0].transport==PublicHttpsStatus::Unreaped);
    lifecycle.stop();CHECK(!lifecycle.start(2,0,E+10,policy,false));

    ms=0;std::atomic<unsigned> lifecycleCalls{0};
    MarketDataOwner lostHelper([&](const PublicJsonRequest &request,const std::atomic<bool> &,PublicHttpsLimits){
        if(++lifecycleCalls>=3){PublicJsonResponse r;r.status=PublicHttpsStatus::Unreaped;r.httpStatus=200;r.observedUtc=clock().second;
            r.body="[{\"currency_pair\":\"PEARL_USDT\",\"last\":\"999\"}]";return r;}
        auto r=okay();r.observedUtc=clock().second;
        r.body=request.feed==PublicFeed::GatePearlTicker?"[{\"currency_pair\":\"PEARL_USDT\",\"last\":\"2.5\"}]":
            "{\"error\":[],\"result\":{\"USDTZUSD\":{\"c\":[\"0.98\",\"1\"]}}}";return r;
    },policy,MarketOrigin::SyntheticFixture,false,clock);
    CHECK(lostHelper.start(1));CHECK(waitUntil([&]{return lostHelper.snapshot()->pearlQuote&&lostHelper.snapshot()->fxQuote;}));
    const auto beforeLoss=lostHelper.snapshot();ms=60000;
    CHECK(waitUntil([&]{return !lostHelper.snapshot()->running;}));
    lostHelper.stop();const auto lostView=lostHelper.snapshot();
    CHECK(lifecycleCalls==3&&lostView->helperFault&&lostView->pearlQuote&&lostView->fxQuote);
    CHECK(lostView->pearlQuote->last.text()=="2.5");
    text=joined(marketSnapshotLines(*lostView,policy,E+70,60000));
    CHECK(text.find("Latest PRL/USD reference: N/A")!=text.npos);
    CHECK(text.find("helper ownership fault")!=text.npos);
    CHECK(beforeLoss->running&&beforeLoss->pearlQuote->last.text()=="2.5");
    CHECK(!lostHelper.start(2)&&lifecycleCalls==3&&lostHelper.snapshot()==lostView);

    for (const auto rejection : {PublicHttpsStatus::AssetIdentity, PublicHttpsStatus::AssetObservation,
                                 PublicHttpsStatus::InvalidObservationTime, PublicHttpsStatus::Ok}) {
        ms = 0;
        std::atomic<unsigned> gateAttempts{0};
        MarketDataOwner changedAsset([&](const PublicJsonRequest &request, const std::atomic<bool> &, PublicHttpsLimits) {
            const bool gate = request.feed == PublicFeed::GatePearlTicker;
            const bool changed = gate && ++gateAttempts > 1;
            if (changed && rejection != PublicHttpsStatus::Ok) {
                PublicJsonResponse invalid;
                invalid.status = rejection;
                return invalid;
            }
            auto response = okay();
            response.observedUtc = clock().second;
            if (gate)
                response.body = changed ? R"([{"currency_pair":"PRL_USDT","last":"2.50"}])" :
                    R"([{"currency_pair":"PEARL_USDT","last":"2.50"}])";
            else response.body = R"({"error":[],"result":{"USDTZUSD":{"c":["0.98","1"]}}})";
            return response;
        }, policy, MarketOrigin::SyntheticFixture, false, clock);
        CHECK(changedAsset.start(1));
        CHECK(waitUntil([&] { return changedAsset.snapshot()->pearlQuote && changedAsset.snapshot()->fxQuote; }));
        const auto beforeMismatch = changedAsset.snapshot();
        ms = 60000;
        CHECK(waitUntil([&] { return changedAsset.snapshot()->health[0].latched; }));
        const auto afterMismatch = changedAsset.snapshot();
        CHECK(afterMismatch->running && !afterMismatch->pearlQuote && afterMismatch->fxQuote);
        CHECK(rejection != PublicHttpsStatus::Ok ? afterMismatch->health[0].transport == rejection :
            afterMismatch->health[0].data == MarketDataStatus::Identity);
        CHECK(beforeMismatch->pearlQuote.has_value());
        changedAsset.stop();
    }

    std::atomic<bool> lossEntered{false};
    MarketDataOwner lateLoss([&](const PublicJsonRequest &,const std::atomic<bool> &cancelled,PublicHttpsLimits){
        lossEntered=true;while(!cancelled.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        PublicJsonResponse r;r.status=PublicHttpsStatus::Unreaped;return r;
    },policy,MarketOrigin::SyntheticFixture,false,clock);
    CHECK(lateLoss.start(1));CHECK(waitUntil([&]{return lossEntered.load();}));lateLoss.requestStop();lateLoss.stop();
    CHECK(lateLoss.snapshot()->helperFault&&!lateLoss.snapshot()->running&&!lateLoss.snapshot()->pearlQuote);
    CHECK(!lateLoss.start(2));
    return finishTests("market_owner");
}
