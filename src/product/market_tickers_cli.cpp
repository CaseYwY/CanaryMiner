#include "market_ticker_runtime.h"
#include "market_public_sample.h"
#include "market_ticker_review.h"

#include <cstdio>

namespace om {
namespace {

using market_detail::awaitPublicMarketReply;
using market_detail::marketSampleStopped;

struct TickerSample {
    MarketSnapshot snapshot;
    uint64_t mono = 0, utc = 0;
};

TickerSample sampleTickers(const std::filesystem::path &helper, const MarketPollingPolicy &policy,
                           const NativeProcessLifetime &lifetime, const volatile sig_atomic_t *stop,
                           PublicTickerCapture *capture) {
    TickerSample result;
    const auto start = marketSystemClock();
    if (capture) capture->startedUtc = start.second;
    MarketPollingPlan plan;
    constexpr uint64_t generation = 1;  // the whole sample shares one locally owned request sequence
    if (!plan.start(generation, start.first, start.second, policy, false)) {
        result.snapshot.clockFault = true;
        return result;
    }
    const auto fetch = marketProcessFetch(helper);
    for (const auto feed : {PublicFeed::GatePearlTicker, PublicFeed::KrakenUsdtTicker}) {
        // Only the control thread can admit each new request. The helper worker completes one.
        if (!lifetime.permitted()) {
            result.snapshot.helperFault = true;
            return result;
        }
        if (marketSampleStopped(stop)) return result;
        const auto sent = marketSystemClock();
        const auto ticket = plan.next(sent.first, sent.second);
        if (!ticket || ticket->request.feed != feed) {
            result.snapshot.clockFault = true;
            return result;
        }
        auto reply = awaitPublicMarketReply(fetch, ticket->request, policy.https, lifetime, stop);
        // Parent-side cleanup uncertainty outranks a simultaneous stop and invalidates both quotes.
        if (reply.status == PublicHttpsStatus::Unreaped || !lifetime.permitted()) {
            result.snapshot.helperFault = true;
            return result;
        }
        if (marketSampleStopped(stop)) return result;
        const auto received = marketSystemClock();
        const MarketParseContext context{ticket->request, ticket->startedUtc, received.first, generation,
            MarketOrigin::ProviderResponse, MarketMinuteConvention::Unqualified, received.second};
        auto quote = parseMarketQuote(reply, context);
        const auto finished = marketSystemClock();
        if (capture && (!market_detail::marketSampleClockContinuous(start, sent, received) ||
                        !market_detail::marketSampleClockContinuous(start, received, finished))) {
            result.snapshot.clockFault = true;
            return result;
        }
        if (!plan.complete(*ticket, reply, quote.status, finished.first, finished.second)) {
            result.snapshot.clockFault = true;
            return result;
        }
        if (capture) {
            auto &receipt = feed == PublicFeed::GatePearlTicker ? capture->gate : capture->kraken;
            receipt = {ticket->startedUtc, received.second, received.first - start.first, std::move(reply)};
            capture->cutoffUtc = finished.second;
            capture->elapsedMs = finished.first - start.first;
        }
        if (feed == PublicFeed::GatePearlTicker) result.snapshot.pearlQuote = std::move(quote.value);
        else result.snapshot.fxQuote = std::move(quote.value);
        result.mono = finished.first;
        result.utc = finished.second;
    }
    result.snapshot.health = plan.health();
    result.snapshot.running = true; // the finished sample is eligible for observation freshness checks
    return result;
}

int ownershipUncertain() {
    std::fputs("market-tickers: helper ownership uncertain\n", stderr);
    return 3;
}

int interrupted() {
    std::fputs("market-tickers: sample cancelled\n", stderr);
    return 130;
}

int tradeClockFault() {
    std::fputs("market-tickers: local clock unavailable or changed during sampling\n", stderr);
    return 3;
}

int samplePublicTrades(const std::filesystem::path &helper,const MarketTickerOptions &options,
                       const NativeProcessLifetime &lifetime,const volatile sig_atomic_t *stop) {
    using market_detail::marketTradeClock;
    using market_detail::marketTradeClockContinuous;
    PublicTickerCapture capture;
    capture.trades=true;
    PublicHttpsLimits limits;
    limits.bodyBytes=maxPublicTradeBodyBytes;
    limits.dnsServer=options.dnsServer;
    const auto start=marketTradeClock();
    if (!marketTradeClockContinuous(start,start,start)) return tradeClockFault();
    capture.startedUtc=start.second;
    const auto fetch=marketProcessFetch(helper);
    auto previous=start;
    for (auto feed:{PublicFeed::GatePearlTrades,PublicFeed::KrakenUsdtTrades}) {
        if (!lifetime.permitted()) return ownershipUncertain();
        if (marketSampleStopped(stop)) return interrupted();
        const auto sent=marketTradeClock();
        if (!marketTradeClockContinuous(start,previous,sent)) return tradeClockFault();
        auto reply=awaitPublicMarketReply(fetch,{feed},limits,lifetime,stop);
        if (reply.status==PublicHttpsStatus::Unreaped || !lifetime.permitted()) return ownershipUncertain();
        if (marketSampleStopped(stop)) return interrupted();
        const auto received=marketTradeClock();
        if (!marketTradeClockContinuous(start,sent,received)) return tradeClockFault();
        auto &receipt=feed==PublicFeed::GatePearlTrades ? capture.gate : capture.kraken;
        receipt={sent.second,received.second,received.first-start.first,std::move(reply)};
        previous=received;
    }
    const auto cutoff=marketTradeClock();
    if (!marketTradeClockContinuous(start,previous,cutoff)) return tradeClockFault();
    capture.cutoffUtc=cutoff.second;
    capture.elapsedMs=cutoff.first-start.first;
    if (options.saveFlag) return savePublicTickerReview(options.archiveName,capture,lifetime,stop,options.saveIncomplete);

    std::string report;
    const auto status=renderPublicTradeSample(capture,report);
    if (!lifetime.permitted()) return ownershipUncertain();
    if (marketSampleStopped(stop)) return interrupted();
    if (status==PublicQuoteReport::InvalidClock) return tradeClockFault();
    if (std::fwrite(report.data(),1,report.size(),stdout)!=report.size() || std::fflush(stdout)!=0) return 1;
    return status==PublicQuoteReport::Complete ? 0 : 3;
}

}  // namespace

int commandPublicTickers(const MarketTickerOptions &options, const std::filesystem::path &module,
                         int argc, const char *const *argv, const volatile sig_atomic_t *stop) {
    if (!market_detail::nativePublicTickerInvocation(options, argc, argv)) {
        std::fputs(options.action==MarketTickerAction::PublicTradeSample ?
            "market-tickers: exact --allow-public-trades required\n" :
            "market-tickers: exact --allow-public-tickers required\n", stderr);
        return 2;
    }
    if (marketSampleStopped(stop)) return interrupted();
    NativeProcessLifetime lifetime;
    if (!lifetime.begin()) return ownershipUncertain();
    try {
        const auto helper = installedPublicTickerHelper(module);
        if (helper.empty()) {
            std::fputs("market-tickers: installed helper path unavailable\n", stderr);
            return 3;
        }
        if (options.action==MarketTickerAction::PublicTradeSample) return samplePublicTrades(helper,options,lifetime,stop);
        MarketPollingPolicy policy;
        policy.scheduleEconomicRequests = false;
        policy.maxAttempts = 1;
        policy.https.dnsServer = options.dnsServer;
        PublicTickerCapture capture;
        const auto sample = sampleTickers(helper, policy, lifetime, stop, options.saveFlag ? &capture : nullptr);
        if (sample.snapshot.helperFault || !lifetime.permitted()) return ownershipUncertain();
        if (marketSampleStopped(stop)) return interrupted();
        if (sample.snapshot.clockFault) {
            std::fputs("market-tickers: local clock unavailable or changed during sampling\n", stderr);
            return 3;
        }
        if (options.saveFlag)
            return savePublicTickerReview(options.archiveName, capture, lifetime, stop, options.saveIncomplete);
        const auto &p = sample.snapshot.pearlQuote, &f = sample.snapshot.fxQuote;
        const auto latest = latestMarketReference(p ? &*p : nullptr, f ? &*f : nullptr, sample.utc, sample.mono,
                                                   policy.freshness);
        std::string report = "One public ticker sample (local observation times; no trade-event timestamps)\n";
        for (const auto &line : marketTickerLines(sample.snapshot, policy.freshness, sample.utc, sample.mono))
            report += line + '\n';
        if (!lifetime.permitted()) return ownershipUncertain();
        if (marketSampleStopped(stop)) return interrupted();
        if (std::fwrite(report.data(), 1, report.size(), stdout) != report.size() || std::fflush(stdout) != 0) return 1;
        return latest.tokenUsd ? 0 : 3;
    } catch (...) {
        if (!lifetime.permitted()) return ownershipUncertain();
        if (marketSampleStopped(stop)) return interrupted();
        std::fputs("market-tickers: could not complete the public sample\n", stderr);
        return 3;
    }
}

}  // namespace om
