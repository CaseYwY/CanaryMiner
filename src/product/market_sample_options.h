#pragma once

#include "local_export_name.h"
#include "public_https.h"
#include <utility>

namespace om {

enum class MarketTickerAction { Help, PublicSample, LocalReview, OpenReview };
inline constexpr std::string_view tickerReviewExtension = ".cmt";
struct MarketTickerOptions {
    MarketTickerAction action = MarketTickerAction::Help;
    std::string dnsServer, archiveName;
    int dnsFlag = 0, saveFlag = 0;
};

inline bool parseMarketTickerCli(int argc, const char *const *argv, int begin, MarketTickerOptions &out) {
    if (!argv || begin < 1 || begin >= argc) return false;
    MarketTickerOptions options;
    const std::string_view flag(argv[begin]);
    const int count = argc - begin;
    if (count == 1 && (flag == "-h" || flag == "--help")) {
        options.action = MarketTickerAction::Help;
    } else if (count == 2 && (flag == "--file" || flag == "--open-review") && argv[begin + 1][0]) {
        options.action = flag == "--file" ? MarketTickerAction::LocalReview : MarketTickerAction::OpenReview;
    } else if (flag == "--allow-public-tickers" && (count == 1 || count == 3 || count == 5)) {
        options.action = MarketTickerAction::PublicSample;
        for (int i = begin + 1; i < argc; i += 2) {
            const std::string_view option(argv[i]);
            if (!options.dnsFlag && option == "--dns-server" && validPublicDnsServer(argv[i + 1])) {
                options.dnsFlag = i;
                options.dnsServer = argv[i + 1];
            } else if (!options.saveFlag && option == "--save-review" &&
                       localExportName(argv[i + 1], tickerReviewExtension)) {
                options.saveFlag = i;
                options.archiveName = argv[i + 1];
            } else return false;
        }
    } else return false;
    out = std::move(options);
    return true;
}

}  // namespace om
