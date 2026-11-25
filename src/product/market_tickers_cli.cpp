#include "market_ticker_runtime.h"
#include "market_owner.h"

#include <chrono>
#include <cstdio>
#include <future>
#include <string_view>
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

namespace om {
namespace {

bool nativeTickerConsent(int argc, const char *const *argv) {
    if (argc != 3) return false;
#if defined(_WIN32)
    (void)argv;
    // The CRT narrow argv can best-fit visually different spellings to the ASCII network opt-in.
    struct WideArguments {
        LPWSTR *values;
        ~WideArguments() { if (values) LocalFree(values); }
    };
    int count = 0;
    const WideArguments args{CommandLineToArgvW(GetCommandLineW(), &count)};
    return args.values && count == argc && std::wstring_view(args.values[1]) == L"market-tickers" &&
           std::wstring_view(args.values[2]) == L"--allow-public-tickers";
#else
    return std::string_view(argv[1]) == "market-tickers" && std::string_view(argv[2]) == "--allow-public-tickers";
#endif
}

bool stopped(const volatile sig_atomic_t *stop) { return stop && *stop; }

struct TickerSample {
    MarketSnapshot snapshot;
    uint64_t mono = 0, utc = 0;
};

PublicJsonResponse awaitTickerReply(const MarketFetch &fetch, const PublicJsonRequest &request,
                                   PublicHttpsLimits limits, const volatile sig_atomic_t *stop) {
    std::atomic<bool> cancel{stopped(stop)};
    if (cancel.load()) {
        PublicJsonResponse result;
        result.status = PublicHttpsStatus::Cancelled;
        return result;
    }
    auto job = std::async(std::launch::async, [&] { return fetch(request, cancel, limits); });
    while (job.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready)
        if (stopped(stop)) cancel.store(true);
    return job.get();
}

TickerSample sampleTickers(const std::filesystem::path &helper, const MarketPollingPolicy &policy,
                           const volatile sig_atomic_t *stop) {
    TickerSample result;
    const auto start = marketSystemClock();
    MarketPollingPlan plan;
    constexpr uint64_t generation = 1;  // the whole sample shares one locally owned request sequence
    if (!plan.start(generation, start.first, start.second, policy, false)) {
        result.snapshot.clockFault = true;
        return result;
    }
    const auto fetch = marketProcessFetch(helper);
    for (const auto feed : {PublicFeed::GatePearlTicker, PublicFeed::KrakenUsdtTicker}) {
        // Only the control thread can admit each new request. The helper worker completes one.
        if (stopped(stop)) return result;
        const auto sent = marketSystemClock();
        const auto ticket = plan.next(sent.first, sent.second);
        if (!ticket || ticket->request.feed != feed) {
            result.snapshot.clockFault = true;
            return result;
        }
        const auto reply = awaitTickerReply(fetch, ticket->request, policy.https, stop);
        // Parent-side cleanup uncertainty outranks a simultaneous stop and invalidates both quotes.
        if (reply.status == PublicHttpsStatus::Unreaped) {
            result.snapshot.helperFault = true;
            return result;
        }
        if (stopped(stop)) return result;
        const auto received = marketSystemClock();
        const MarketParseContext context{ticket->request, ticket->startedUtc, received.first, generation,
            MarketOrigin::ProviderResponse, MarketMinuteConvention::Unqualified, received.second};
        auto quote = parseMarketQuote(reply, context);
        const auto finished = marketSystemClock();
        if (!plan.complete(*ticket, reply, quote.status, finished.first, finished.second)) {
            result.snapshot.clockFault = true;
            return result;
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

int interrupted() {
    std::fputs("market-tickers: sample cancelled\n", stderr);
    return 130;
}

}  // namespace

int commandPublicTickers(const std::filesystem::path &module, int argc, const char *const *argv,
                         const volatile sig_atomic_t *stop) {
    if (!nativeTickerConsent(argc, argv)) {
        std::fputs("market-tickers: exact --allow-public-tickers required\n", stderr);
        return 2;
    }
    if (stopped(stop)) return interrupted();
    try {
        const auto helper = installedPublicTickerHelper(module);
        if (helper.empty()) {
            std::fputs("market-tickers: installed helper path unavailable\n", stderr);
            return 3;
        }
        MarketPollingPolicy policy;
        policy.scheduleEconomicRequests = false;
        policy.maxAttempts = 1;
        const auto sample = sampleTickers(helper, policy, stop);
        if (sample.snapshot.helperFault) {
            std::fputs("market-tickers: helper ownership uncertain\n", stderr);
            return 3;
        }
        if (stopped(stop)) return interrupted();
        if (sample.snapshot.clockFault) {
            std::fputs("market-tickers: local clock unavailable or changed during sampling\n", stderr);
            return 3;
        }
        const auto &p = sample.snapshot.pearlQuote, &f = sample.snapshot.fxQuote;
        const auto latest = latestMarketReference(p ? &*p : nullptr, f ? &*f : nullptr, sample.utc, sample.mono,
                                                   policy.freshness);
        std::string report = "One public ticker sample (local observation times; no trade-event timestamps)\n";
        for (const auto &line : marketTickerLines(sample.snapshot, policy.freshness, sample.utc, sample.mono))
            report += line + '\n';
        if (stopped(stop)) return interrupted();
        if (std::fwrite(report.data(), 1, report.size(), stdout) != report.size() || std::fflush(stdout) != 0) return 1;
        return latest.tokenUsd ? 0 : 3;
    } catch (...) {
        if (stopped(stop)) return interrupted();
        std::fputs("market-tickers: could not complete the public sample\n", stderr);
        return 3;
    }
}

}  // namespace om
