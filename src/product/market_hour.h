#pragma once

#include "local_export_name.h"

#include <csignal>
#include <filesystem>

namespace om {

inline constexpr std::string_view marketReviewExtension = ".cmh";

enum class MarketHourAction { Help, PublicSample, Review, Save, Open };

struct MarketHourOptions {
    MarketHourAction action = MarketHourAction::Review;
    std::string archiveName;
};

inline bool parseMarketHourCli(int argc, const char *const *argv, int begin, MarketHourOptions &out) {
    if (!argv || begin < 1 || begin >= argc) return false;
    const std::string_view option(argv[begin]);
    const int count = argc - begin;
    if (count == 1 && (option == "--help" || option == "-h")) {
        out.action = MarketHourAction::Help;
    } else if (count == 1 && option == "--allow-public-candles") {
        out.action = MarketHourAction::PublicSample;
    } else if (count == 2 && argv[begin + 1][0] && option == "--file") {
        out.action = MarketHourAction::Review;
    } else if (count == 2 && argv[begin + 1][0] && option == "--open-review") {
        out.action = MarketHourAction::Open;
    } else if (count == 4 && argv[begin + 1][0] && option == "--file" &&
               std::string_view(argv[begin + 2]) == "--save-review" &&
               localExportName(argv[begin + 3], marketReviewExtension)) {
        out.action = MarketHourAction::Save;
        out.archiveName = argv[begin + 3];
    } else return false;
    return true;
}

int commandMarketHour(const MarketHourOptions &options, int argc, const char *const *argv,
                      const volatile sig_atomic_t *stop);
int commandPublicMarketHour(const std::filesystem::path &module, int argc, const char *const *argv,
                            const volatile sig_atomic_t *stop);

}  // namespace om
