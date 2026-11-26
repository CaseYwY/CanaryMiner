#pragma once

#include <algorithm>
#include <initializer_list>
#include <string_view>
#include <utility>
#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#endif

namespace om::market_detail {

inline bool nativeMarketArguments(int argc, const char *const *argv,
                                  std::initializer_list<std::pair<int, std::string_view>> expected) {
#if defined(_WIN32)
    (void)argv;
    // CRT argv best-fit conversion cannot supply consent on behalf of a different native spelling.
    struct WideArguments {
        LPWSTR *values;
        ~WideArguments() { if (values) LocalFree(values); }
    };
    int count = 0;
    const WideArguments args{CommandLineToArgvW(GetCommandLineW(), &count)};
    if (!args.values || count != argc) return false;
#else
    if (!argv) return false;
#endif
    for (const auto &[index, ascii] : expected) {
        if (index <= 0 || index >= argc) return false;
#if defined(_WIN32)
        const std::wstring_view actual(args.values[index]);
        if (!std::equal(ascii.begin(), ascii.end(), actual.begin(), actual.end())) return false;
#else
        if (std::string_view(argv[index]) != ascii) return false;
#endif
    }
    return true;
}

inline bool nativeMarketArgumentPair(int argc, const char *const *argv, int firstIndex, std::string_view first,
                                     int secondIndex, std::string_view second) {
    return firstIndex != secondIndex && nativeMarketArguments(argc, argv, {{firstIndex, first}, {secondIndex, second}});
}

inline bool validDashboardMarketArguments(int argc, const char *const *argv, int tickerIndex, int hourIndex,
                                          int dnsIndex, std::string_view dnsServer) {
    if (hourIndex) {
        if (!nativeMarketArgumentPair(argc, argv, tickerIndex, "--allow-public-tickers", hourIndex, "--allow-public-candles"))
            return false;
    } else if (!nativeMarketArguments(argc, argv, {{tickerIndex, "--allow-public-tickers"}})) {
        return false;
    }
    return !dnsIndex || (dnsIndex < argc - 1 &&
        nativeMarketArgumentPair(argc, argv, dnsIndex, "--market-dns-server", dnsIndex + 1, dnsServer));
}

} // namespace om::market_detail
