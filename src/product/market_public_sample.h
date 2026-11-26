#pragma once

#include "market_owner.h"
#include "native_process_admission.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <future>
#include <string_view>
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

namespace om::market_detail {

inline bool nativeMarketArgumentPair(int argc, const char *const *argv, int firstIndex, std::string_view first,
                                     int secondIndex, std::string_view second) {
    if (firstIndex <= 0 || firstIndex >= argc || secondIndex <= 0 || secondIndex >= argc || firstIndex == secondIndex)
        return false;
#if defined(_WIN32)
    (void)argv;
    // CRT argv best-fit conversion cannot supply consent on behalf of a different native spelling.
    struct WideArguments {
        LPWSTR *values;
        ~WideArguments() { if (values) LocalFree(values); }
    };
    int count = 0;
    const WideArguments args{CommandLineToArgvW(GetCommandLineW(), &count)};
    const auto exact = [](std::string_view ascii, std::wstring_view native) {
        return std::equal(ascii.begin(), ascii.end(), native.begin(), native.end());
    };
    return args.values && count == argc && exact(first, args.values[firstIndex]) && exact(second, args.values[secondIndex]);
#else
    return std::string_view(argv[firstIndex]) == first && std::string_view(argv[secondIndex]) == second;
#endif
}

using MarketSampleClock = std::pair<uint64_t, uint64_t>;

inline bool marketSampleClockContinuous(const MarketSampleClock &initial, const MarketSampleClock &previous,
                                        const MarketSampleClock &current) {
    if (current.first < previous.first || current.second < previous.second ||
        current.first < initial.first || current.second < initial.second || current.second > 253402300799ULL)
        return false;
    const auto steadySeconds = (current.first - initial.first) / 1000;
    const auto utcSeconds = current.second - initial.second;
    return (std::max)(steadySeconds, utcSeconds) - (std::min)(steadySeconds, utcSeconds) <= 5;
}

inline bool marketSampleStopped(const volatile sig_atomic_t *stop) { return stop && *stop; }

inline PublicJsonResponse marketSampleFailure(PublicHttpsStatus status) {
    PublicJsonResponse response;
    response.status = status;
    return response;
}

inline PublicJsonResponse awaitPublicMarketReply(const MarketFetch &fetch, const PublicJsonRequest &request,
                                                 PublicHttpsLimits limits, const NativeProcessLifetime &lifetime,
                                                 const volatile sig_atomic_t *stop) {
    if (!lifetime.permitted()) return marketSampleFailure(PublicHttpsStatus::Unreaped);
    std::atomic<bool> cancel{marketSampleStopped(stop)};
    if (cancel.load()) return marketSampleFailure(PublicHttpsStatus::Cancelled);

    // A new std::async thread does not inherit the command's original native scope.
    auto job = std::async(std::launch::async, [&] {
        const NativeProcessScope scope(lifetime.epoch());
        if (!lifetime.permitted()) return marketSampleFailure(PublicHttpsStatus::Unreaped);
        return fetch(request, cancel, limits);
    });
    while (job.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready) {
        if (marketSampleStopped(stop) || !lifetime.permitted()) cancel.store(true);
    }
    auto reply = job.get();
    if (!lifetime.permitted()) return marketSampleFailure(PublicHttpsStatus::Unreaped);
    return reply;
}

}  // namespace om::market_detail
