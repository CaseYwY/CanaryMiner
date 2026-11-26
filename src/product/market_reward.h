#pragma once

#include "market_sample_options.h"

#include <charconv>
#include <csignal>
#include <filesystem>
#include <optional>
#include <utility>

namespace om {

inline constexpr uint64_t rewardUtcLimit = 253402300799ULL;
inline constexpr size_t marketRewardBodyBytes = 16 * 1024;
inline constexpr char marketRewardHelp[] =
    "canary-miner market-reward --allow-public-reward [--dns-server IP]\n"
    "canary-miner market-reward --file FILE --as-of UTC_SECONDS\n"
    "Calculate the next Pearl mainnet block subsidy from live or saved Blockbook indexer status.\n"
    "Public sampling requires a public-HTTPS-enabled build. A block subsidy excludes transaction fees.";

enum class MarketRewardAction { Help, PublicSample, LocalReview };
struct MarketRewardOptions {
    MarketRewardAction action = MarketRewardAction::Help;
    std::string dnsServer;
    uint64_t cutoffUtc = 0;
};

inline bool parseMarketRewardCli(int argc, const char *const *argv, int begin, MarketRewardOptions &out) {
    if (!argv || begin < 1 || begin >= argc) return false;
    MarketRewardOptions options;
    const std::string_view flag(argv[begin]);
    const int count = argc - begin;
    if (count == 1 && (flag == "-h" || flag == "--help")) {
        options.action = MarketRewardAction::Help;
    } else if (count == 4 && flag == "--file" && argv[begin + 1][0] && argv[begin + 1][0] != '-' &&
               std::string_view(argv[begin + 2]) == "--as-of") {
        const std::string_view time(argv[begin + 3]);
        const auto end = time.data() + time.size();
        const auto parsed = std::from_chars(time.data(), end, options.cutoffUtc);
        if (parsed.ec != std::errc() || parsed.ptr != end || !options.cutoffUtc ||
            options.cutoffUtc > rewardUtcLimit || time.front() == '0') return false;
        options.action = MarketRewardAction::LocalReview;
    } else if (parsePublicMarketSampleCli(argc, argv, begin, "--allow-public-reward", options.dnsServer)) {
        options.action = MarketRewardAction::PublicSample;
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
PearlRewardResult parsePearlBlockbookTip(const std::string &body, uint64_t cutoffUtc);
int reportPearlSubsidy(const PearlRewardResult &result, uint64_t cutoffUtc, bool live);
int commandMarketRewardFile(const MarketRewardOptions &options, int argc, const char *const *argv,
                            const volatile sig_atomic_t *stop);
int commandPublicMarketReward(const MarketRewardOptions &options, const std::filesystem::path &module,
                              int argc, const char *const *argv, const volatile sig_atomic_t *stop);

}  // namespace om
