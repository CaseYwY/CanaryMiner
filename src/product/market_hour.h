#pragma once

#include "local_export_name.h"
#include "public_https.h"

#include <charconv>
#include <csignal>
#include <filesystem>
#include <string_view>
#include <utility>

namespace om {

inline constexpr std::string_view marketReviewExtension = ".cmh";
inline constexpr int maxCombinedPublicHours = 16;

enum class MarketHourAction { Help, PublicSample, PublicSave, PublicCapture, Review, Save, Open, Combine };

struct MarketHourOptions {
    MarketHourAction action = MarketHourAction::Review;
    std::string archiveName;
    std::string dnsServer;
    int saveFlag = 0;
    int dnsFlag = 0;
    int krakenFallbackFlag = 0;
    uint64_t windowEndUtc = 0;
    uint64_t reviewCutoffUtc = 0;
    unsigned windowMinutes = 0;
    int archiveCount = 0;

    bool samplesPublic() const {
        return action == MarketHourAction::PublicSample || action == MarketHourAction::PublicSave ||
            action == MarketHourAction::PublicCapture;
    }
};

inline bool marketHourNumber(std::string_view arg, uint64_t &value) {
    if (arg.empty() || arg.front() < '1' || arg.front() > '9') return false;
    const auto *end = arg.data() + arg.size();
    const auto result = std::from_chars(arg.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

inline bool parseMarketHourCli(int argc,const char *const *argv,int begin,MarketHourOptions &out) {
    if (!argv || begin<1 || begin>=argc) return false;
    const std::string_view option(argv[begin]);
    const int count=argc-begin;
    MarketHourOptions parsed;
    if (count==1 && (option=="--help" || option=="-h")) {
        parsed.action=MarketHourAction::Help;
    } else if (option=="--allow-public-candles" && count<=6) {
        parsed.action=MarketHourAction::PublicSample;
        for (int i=begin+1;i<argc;++i) {
            const std::string_view flag(argv[i]);
            if (!parsed.krakenFallbackFlag && flag=="--kraken-fallback") {
                parsed.krakenFallbackFlag=i;
                continue;
            }
            if (i+1>=argc) return false;
            const char *value = argv[i+1];
            if (!parsed.saveFlag && (flag=="--save-review" || flag=="--save-sample") &&
                localExportName(value,marketReviewExtension)) {
                parsed.action=flag=="--save-sample" ? MarketHourAction::PublicCapture : MarketHourAction::PublicSave;
                parsed.archiveName=value;
                parsed.saveFlag=i;
            } else if (!parsed.dnsFlag && flag=="--dns-server" && validPublicDnsServer(value)) {
                parsed.dnsServer=value;
                parsed.dnsFlag=i;
            } else return false;
            ++i;
        }
    } else if (option=="--combine-public-hours") {
        uint64_t minutes = 0;
        if (count < 9 || std::string_view(argv[begin+1]) != "--end-utc" ||
            !marketHourNumber(argv[begin+2], parsed.windowEndUtc) ||
            std::string_view(argv[begin+3]) != "--minutes" ||
            !marketHourNumber(argv[begin+4], minutes) || minutes > 719 ||
            std::string_view(argv[begin+5]) != "--as-of-utc" ||
            !marketHourNumber(argv[begin+6], parsed.reviewCutoffUtc))
            return false;
        parsed.action = MarketHourAction::Combine;
        parsed.windowMinutes = static_cast<unsigned>(minutes);
        for (int i = begin+7; i < argc; i += 2) {
            if (i+1 >= argc || !argv[i+1][0]) return false;
            const std::string_view flag(argv[i]);
            if (flag == "--archive" && parsed.archiveCount < maxCombinedPublicHours) {
                ++parsed.archiveCount;
            } else if (flag == "--save-review" && i+2 == argc &&
                       localExportName(argv[i+1], marketReviewExtension)) {
                parsed.saveFlag = i;
                parsed.archiveName = argv[i+1];
            } else return false;
        }
        if (!parsed.archiveCount) return false;
    } else if (count==2 && argv[begin+1][0] && option=="--file") {
        parsed.action=MarketHourAction::Review;
    } else if (count==2 && argv[begin+1][0] && option=="--open-review") {
        parsed.action=MarketHourAction::Open;
    } else if (count==4 && argv[begin+1][0] && option=="--file" &&
               std::string_view(argv[begin+2])=="--save-review" &&
               localExportName(argv[begin+3],marketReviewExtension)) {
        parsed.action=MarketHourAction::Save;
        parsed.archiveName=argv[begin+3];
    } else return false;
    out=std::move(parsed);
    return true;
}

int commandMarketHour(const MarketHourOptions &options, int argc, const char *const *argv,
                      const volatile sig_atomic_t *stop);
bool nativePublicMarketHourInvocation(const MarketHourOptions &options, int argc, const char *const *argv);
int commandPublicMarketHour(const MarketHourOptions &options, const std::filesystem::path &module,
                            int argc, const char *const *argv,
                            const volatile sig_atomic_t *stop);

}  // namespace om
