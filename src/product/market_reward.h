#pragma once

#include "local_export_name.h"
#include "public_https.h"

#include <array>
#include <charconv>
#include <csignal>
#include <filesystem>
#include <optional>
#include <utility>

namespace om {

inline constexpr uint64_t rewardUtcLimit = 253402300799ULL;
inline constexpr unsigned marketRewardSampleMs = 15000;
inline constexpr size_t marketRewardBodyBytes = 16 * 1024;
inline constexpr std::string_view rewardReviewExtension = ".cmr";
inline constexpr char marketRewardHelp[] =
    "canary-miner market-reward --allow-public-reward [--dns-server IP|cloudflare-doh] [--save-review NAME.cmr]\n"
    "canary-miner market-reward --file FILE --as-of UTC_SECONDS\n"
    "canary-miner market-reward --open-review FILE\n"
    "Calculate the next Pearl mainnet block subsidy from a live Blockbook tip check or saved status.\n"
    "Public sampling requires a public-HTTPS-enabled build. A block subsidy excludes transaction fees.";

enum class MarketRewardAction { Help, PublicSample, LocalReview, OpenReview };
struct MarketRewardOptions {
    MarketRewardAction action = MarketRewardAction::Help;
    std::string dnsServer, archiveName;
    uint64_t cutoffUtc = 0;
    int dnsFlag = 0;
    int saveFlag = 0;
};

inline bool parseMarketRewardCli(int argc, const char *const *argv, int begin, MarketRewardOptions &out) {
    if (!argv || begin < 1 || begin >= argc) return false;
    MarketRewardOptions options;
    const std::string_view flag(argv[begin]);
    const int count = argc - begin;
    if (count == 1 && (flag == "-h" || flag == "--help")) {
        options.action = MarketRewardAction::Help;
    } else if (count == 4 && flag == "--file" && argv[begin + 1][0] &&
               std::string_view(argv[begin + 2]) == "--as-of") {
        const std::string_view time(argv[begin + 3]);
        const auto end = time.data() + time.size();
        const auto parsed = std::from_chars(time.data(), end, options.cutoffUtc);
        if (parsed.ec != std::errc() || parsed.ptr != end || !options.cutoffUtc ||
            options.cutoffUtc > rewardUtcLimit || time.front() == '0') return false;
        options.action = MarketRewardAction::LocalReview;
    } else if (count == 2 && flag == "--open-review" && argv[begin + 1][0]) {
        options.action = MarketRewardAction::OpenReview;
    } else if (flag == "--allow-public-reward" && (count == 1 || count == 3 || count == 5)) {
        options.action = MarketRewardAction::PublicSample;
        for (int i = begin + 1; i < argc; i += 2) {
            const std::string_view option(argv[i]);
            if (!options.dnsFlag && option == "--dns-server" && validPublicDnsServer(argv[i + 1])) {
                options.dnsFlag = i;
                options.dnsServer = argv[i + 1];
            } else if (!options.saveFlag && option == "--save-review" &&
                       localExportName(argv[i + 1], rewardReviewExtension)) {
                options.saveFlag = i;
                options.archiveName = argv[i + 1];
            } else return false;
        }
    } else return false;
    out = std::move(options);
    return true;
}

enum class PearlRewardStatus { Ok, Invalid, Unsynced, Stale };
struct PearlSubsidyObservation {
    uint32_t tipHeight = 0;
    std::string backendTipHash;
    uint64_t indexUpdateUtc = 0;
    uint64_t nextBlockGrains = 0;
};
struct PearlRewardResult {
    PearlRewardStatus status = PearlRewardStatus::Invalid;
    std::optional<PearlSubsidyObservation> value;
};

std::optional<uint64_t> pearlMainnetSubsidyGrains(uint32_t height);
enum class PearlRewardCutoff { ExactSecond, WholeObservationSecond };
PearlRewardResult parsePearlBlockbookTip(const std::string &body, uint64_t cutoffUtc, PearlRewardCutoff precision);
struct MarketRewardReply {
    uint64_t sentUtc = 0, observedUtc = 0, receivedUtc = 0;
    std::string body;
};
using MarketRewardReplies = std::array<MarketRewardReply, 3>;

int reportPearlRewardFailure(const PearlRewardResult &result);
class NativeProcessLifetime;
int reportPublicMarketReward(const std::string &archiveName, const MarketRewardReplies &replies,
                             const NativeProcessLifetime &lifetime, const volatile sig_atomic_t *stop);
int commandMarketRewardFile(const MarketRewardOptions &options, int argc, const char *const *argv,
                            const volatile sig_atomic_t *stop);
bool nativeMarketRewardInvocation(const MarketRewardOptions &options, int argc, const char *const *argv);
int commandPublicMarketReward(const MarketRewardOptions &options, const std::filesystem::path &module,
                              const volatile sig_atomic_t *stop);

}  // namespace om
