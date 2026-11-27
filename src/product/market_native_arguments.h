#pragma once

#include "market_sample_options.h"
#include "native_cli_arguments.h"

namespace om::market_detail {

inline bool nativeMarketArguments(int argc, const char *const *argv,
                                  std::initializer_list<std::pair<int, std::string_view>> expected) {
    return nativeCliArguments(argc, argv, expected);
}

inline bool nativeMarketArgumentPair(int argc, const char *const *argv, int firstIndex, std::string_view first,
                                     int secondIndex, std::string_view second) {
    return firstIndex != secondIndex && nativeMarketArguments(argc, argv, {{firstIndex, first}, {secondIndex, second}});
}

inline bool nativePublicTickerInvocation(const MarketTickerOptions &options, int argc, const char *const *argv) {
    const char *consent=options.action==MarketTickerAction::PublicTradeSample ? "--allow-public-trades" : "--allow-public-tickers";
    return isPublicTickerSample(options.action) &&
        argc == 3 + (options.dnsFlag ? 2 : 0) + (options.saveFlag ? 2 : 0) &&
        nativeMarketArgumentPair(argc, argv, 1, "market-tickers", 2, consent) &&
        (!options.dnsFlag || nativeMarketArgumentPair(argc, argv, options.dnsFlag, "--dns-server",
                                                      options.dnsFlag + 1, options.dnsServer)) &&
        (!options.saveFlag || nativeMarketArgumentPair(argc, argv, options.saveFlag,
                                                       options.saveIncomplete ? "--save-sample" : "--save-review",
                                                       options.saveFlag + 1, options.archiveName));
}

inline bool validDashboardMarketArguments(int argc, const char *const *argv, int latestIndex, bool trades, int hourIndex,
                                          int fallbackIndex, int dnsIndex, std::string_view dnsServer) {
    const char *permission=trades ? "--allow-public-trades" : "--allow-public-tickers";
    if (hourIndex) {
        if (!nativeMarketArgumentPair(argc, argv, latestIndex, permission, hourIndex, "--allow-public-candles"))
            return false;
    } else if (!nativeMarketArguments(argc, argv, {{latestIndex, permission}})) {
        return false;
    }
    if (fallbackIndex && !nativeMarketArguments(argc, argv, {{fallbackIndex, "--kraken-fallback"}})) return false;
    return !dnsIndex || (dnsIndex < argc - 1 &&
        nativeMarketArgumentPair(argc, argv, dnsIndex, "--market-dns-server", dnsIndex + 1, dnsServer));
}

} // namespace om::market_detail
