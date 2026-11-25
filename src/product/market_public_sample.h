#pragma once

#include "market_owner.h"
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

inline bool nativeMarketConsent(int argc, const char *const *argv, std::string_view command, std::string_view option) {
    return argc == 3 && nativeMarketArgumentPair(argc, argv, 1, command, 2, option);
}

inline bool marketSampleStopped(const volatile sig_atomic_t *stop) { return stop && *stop; }

inline PublicJsonResponse awaitPublicMarketReply(const MarketFetch &fetch, const PublicJsonRequest &request,
                                                 PublicHttpsLimits limits, const volatile sig_atomic_t *stop) {
    std::atomic<bool> cancel{marketSampleStopped(stop)};
    if (cancel.load()) {
        PublicJsonResponse result;
        result.status = PublicHttpsStatus::Cancelled;
        return result;
    }
    auto job = std::async(std::launch::async, [&] { return fetch(request, cancel, limits); });
    while (job.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready)
        if (marketSampleStopped(stop)) cancel.store(true);
    return job.get();
}

}  // namespace om::market_detail
