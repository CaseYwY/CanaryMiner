#pragma once

#include "local_export_name.h"
#include "public_https.h"

#include <csignal>
#include <filesystem>
#include <string_view>
#include <utility>

namespace om {

inline constexpr std::string_view marketReviewExtension = ".cmh";

enum class MarketHourAction { Help, PublicSample, PublicSave, Review, Save, Open };

struct MarketHourOptions {
    MarketHourAction action = MarketHourAction::Review;
    std::string archiveName;
    std::string dnsServer;
    int saveFlag = 0;
    int dnsFlag = 0;

    bool samplesPublic() const {
        return action == MarketHourAction::PublicSample || action == MarketHourAction::PublicSave;
    }
};

inline bool parseMarketHourCli(int argc,const char *const *argv,int begin,MarketHourOptions &out) {
    if (!argv || begin<1 || begin>=argc) return false;
    const std::string_view option(argv[begin]);
    const int count=argc-begin;
    MarketHourOptions parsed;
    if (count==1 && (option=="--help" || option=="-h")) {
        parsed.action=MarketHourAction::Help;
    } else if (option=="--allow-public-candles" && (count==1 || count==3 || count==5)) {
        parsed.action=MarketHourAction::PublicSample;
        for (int i=begin+1;i<argc;i+=2) {
            const std::string_view flag(argv[i]);
            const char *value = argv[i + 1];
            if (!parsed.saveFlag && flag=="--save-review" && localExportName(value,marketReviewExtension)) {
                parsed.action=MarketHourAction::PublicSave;
                parsed.archiveName=value;
                parsed.saveFlag=i;
            } else if (!parsed.dnsFlag && flag=="--dns-server" && validPublicDnsServer(value)) {
                parsed.dnsServer=value;
                parsed.dnsFlag=i;
            } else return false;
        }
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
int commandPublicMarketHour(const MarketHourOptions &options, const std::filesystem::path &module,
                            int argc, const char *const *argv,
                            const volatile sig_atomic_t *stop);

}  // namespace om
