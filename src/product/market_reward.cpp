#include "market_reward.h"

#include "civil_time.h"
#include "exact_decimal.h"
#include "json_dom.h"
#include "local_review_input.h"

#include <chrono>
#include <cstdio>
#if defined(_WIN32)
#include <shellapi.h>
#endif

namespace om {
namespace {

constexpr uint64_t grainsPerPrl = 100'000'000;
constexpr uint64_t maxIndexAgeSeconds = 30 * 60;
constexpr uint64_t emissionBlocks = (4 * 365 * 24 * 60 * 60) / 194;

bool stringIs(const JsonValue *field, const char *expected) {
    return field && field->isString() && field->text == expected;
}

std::optional<uint64_t> utcTimestamp(std::string_view text) {
    if (text.size() < 20 || text.size() > 30 || text.back() != 'Z' || text[4] != '-' ||
        text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':') return {};
    if (text.size() != 20 &&
        (text.size() < 22 || text[19] != '.' ||
         text.substr(20, text.size() - 21).find_first_not_of("0123456789") != std::string_view::npos)) return {};
    const auto digits = [&](size_t pos, size_t width) {
        unsigned value = 0;
        for (const char digit : text.substr(pos, width)) {
            if (digit < '0' || digit > '9') return 10000u;
            value = value * 10 + static_cast<unsigned>(digit - '0');
        }
        return value;
    };
    const auto hour = digits(11, 2), minute = digits(14, 2), second = digits(17, 2);
    if (hour > 23 || minute > 59 || second > 59) return {};
    const auto serial = civilSerialDay({static_cast<int>(digits(0, 4)), digits(5, 2), digits(8, 2)});
    if (!serial || *serial < 0) return {};
    return static_cast<uint64_t>(*serial) * 86400 + hour * 3600 + minute * 60 + second;
}

std::optional<std::filesystem::path> nativeReviewFile(int argc, const char *const *argv, uint64_t cutoffUtc) {
    if (argc != 6) return {};
#if defined(_WIN32)
    (void)argv;
    int count = 0;
    struct Arguments {
        LPWSTR *values;
        ~Arguments() { if (values) LocalFree(values); }
    } args{CommandLineToArgvW(GetCommandLineW(), &count)};
    if (!args.values || count != argc) return {};
    const auto *a = args.values;
    if (std::wstring_view(a[1]) != L"market-reward" || std::wstring_view(a[2]) != L"--file" ||
        !a[3][0] || a[3][0] == L'-' || std::wstring_view(a[4]) != L"--as-of" ||
        std::wstring_view(a[5]) != std::to_wstring(cutoffUtc)) return {};
#else
    const auto *a = argv;
    if (std::string_view(a[1]) != "market-reward" || std::string_view(a[2]) != "--file" ||
        !a[3][0] || a[3][0] == '-' || std::string_view(a[4]) != "--as-of" ||
        std::string_view(a[5]) != std::to_string(cutoffUtc)) return {};
#endif
    return std::filesystem::path(a[3]);
}

int reviewError(const char *message, int code = 3) {
    std::fprintf(stderr, "market-reward: %s\n", message);
    return code;
}

}  // namespace

std::optional<uint64_t> pearlMainnetSubsidyGrains(uint32_t height) {
    if (height > INT32_MAX) return {};
    if (!height) return 0;
    const uint64_t first = height + emissionBlocks;
    ExactDecimal supply, emission, numerator, denominator, nearest, accounted;
    std::string rounded;
    // The v1.5.8 mainnet consensus uses floor(S * H / ((h + H) * (h + H - 1))).
    if (!ExactDecimal::parse("210000000000000000", &supply) ||
        !ExactDecimal::parse(std::to_string(emissionBlocks), &emission) ||
        !supply.multiply(emission, &numerator) ||
        !ExactDecimal::parse(std::to_string(first * (first - 1)), &denominator) ||
        !numerator.formatRatio(denominator, 0, &rounded) || !ExactDecimal::parse(rounded, &nearest) ||
        !nearest.multiply(denominator, &accounted)) return {};
    uint64_t grains = 0;
    const auto end = rounded.data() + rounded.size();
    const auto parsed = std::from_chars(rounded.data(), end, grains);
    if (parsed.ec != std::errc() || parsed.ptr != end) return {};
    // Nearest differs from floor by at most one. Compare the product in exact base 10.
    return grains - static_cast<uint64_t>(accounted.compare(numerator) > 0);
}

PearlRewardResult parsePearlBlockbookTip(const std::string &body, uint64_t cutoffUtc) {
    JsonValue root;
    if (!cutoffUtc || cutoffUtc > rewardUtcLimit || body.size() > marketRewardBodyBytes ||
        !boundedPublicJson(body) || !parseJsonStrict(body, &root, nullptr, {marketRewardBodyBytes, 256}) || !root.isObject()) return {};
    const auto *index = root.find("blockbook"), *node = root.find("backend");
    if (!index || !index->isObject() || !node || !node->isObject() ||
        !stringIs(index->find("coin"), "Pearl") || !stringIs(index->find("network"), "PRL") ||
        !stringIs(node->find("chain"), "mainnet")) return {};
    const auto *scale = index->find("decimals"), *height = index->find("bestHeight");
    const auto *blocks = node->find("blocks"), *headers = node->find("headers");
    const auto *hash = node->find("bestBlockHash"), *timestamp = index->find("lastBlockTime");
    const auto *inSync = index->find("inSync"), *initialSync = index->find("initialSync");
    uint64_t decimals = 0, bestHeight = 0, nodeBlocks = 0, nodeHeaders = 0;
    if (!scale || !scale->asUint64(&decimals) || decimals != 8 || !height || !height->asUint64(&bestHeight) ||
        bestHeight >= INT32_MAX || !blocks || !blocks->asUint64(&nodeBlocks) || !headers ||
        !headers->asUint64(&nodeHeaders) || !hash || !hash->isString() || hash->text.size() != 64 ||
        hash->text.find_first_not_of("0123456789abcdef") != std::string::npos ||
        hash->text.find_first_not_of('0') == std::string::npos || !timestamp || !timestamp->isString() ||
        !inSync || !inSync->isBool() || !initialSync || !initialSync->isBool()) return {};
    if (!inSync->boolean || initialSync->boolean || bestHeight != nodeBlocks || nodeBlocks != nodeHeaders)
        return {PearlRewardStatus::Unsynced, {}};
    const auto indexUpdateUtc = utcTimestamp(timestamp->text);
    if (!indexUpdateUtc || *indexUpdateUtc > cutoffUtc || cutoffUtc - *indexUpdateUtc > maxIndexAgeSeconds)
        return {PearlRewardStatus::Stale, {}};
    const auto subsidy = pearlMainnetSubsidyGrains(static_cast<uint32_t>(bestHeight + 1));
    if (!subsidy) return {};
    return {PearlRewardStatus::Ok, PearlSubsidyObservation{static_cast<uint32_t>(bestHeight), hash->text,
                                                         *indexUpdateUtc, *subsidy}};
}

int reportPearlSubsidy(const PearlRewardResult &result, uint64_t cutoffUtc, bool live) {
    if (!result.value) {
        if (result.status == PearlRewardStatus::Unsynced) return reviewError("Pearl indexer and backend are not synchronized");
        if (result.status == PearlRewardStatus::Stale) return reviewError("Pearl index time is invalid, future or over 1800 seconds old");
        return reviewError("invalid Pearl mainnet indexer response");
    }
    const auto &sample = *result.value;
    std::string fraction = std::to_string(sample.nextBlockGrains % grainsPerPrl);
    fraction.insert(0, 8 - fraction.size(), '0');
    const std::string report = live ? "Pearl mainnet subsidy [live Blockbook indexer, locally observed]\n"
                                    : "Pearl mainnet subsidy [saved Blockbook status, user-declared cutoff]\n";
    const auto text = report + "Indexed height: " + std::to_string(sample.tipHeight) +
        "\nBackend tip hash: " + sample.backendTipHash +
        "\nIndexer update UTC: " + std::to_string(sample.indexUpdateUtc) +
        "\n" + (live ? "Observation UTC: " : "Declared cutoff UTC: ") + std::to_string(cutoffUtc) +
        "\nNext block height: " + std::to_string(sample.tipHeight + 1) +
        "\nNext block subsidy: " + std::to_string(sample.nextBlockGrains / grainsPerPrl) + "." + fraction +
        " PRL (scheduled consensus subsidy, excluding fees)\n";
    return std::fwrite(text.data(), 1, text.size(), stdout) == text.size() && std::fflush(stdout) == 0 ? 0 : 1;
}

int commandMarketRewardFile(const MarketRewardOptions &options, int argc, const char *const *argv,
                            const volatile sig_atomic_t *stop) {
    const auto stopped = [&] { return stop && *stop; };
    if (stopped()) return reviewError("review cancelled", 130);
    try {
        const auto path = nativeReviewFile(argc, argv, options.cutoffUtc);
        if (!path) return reviewError("invalid arguments. See market-reward --help", 2);
        std::string body;
        const bool read = readLocalReviewInput(*path, marketRewardBodyBytes, body);
        if (stopped()) return reviewError("review cancelled", 130);
        if (!read || body.empty()) return reviewError("input file unavailable, empty, oversized or unsuitable");
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (now < 1 || static_cast<uint64_t>(now) > rewardUtcLimit ||
            options.cutoffUtc > static_cast<uint64_t>(now)) return reviewError("declared cutoff is in the future or local UTC is unavailable");
        const auto result = parsePearlBlockbookTip(body, options.cutoffUtc);
        if (stopped()) return reviewError("review cancelled", 130);
        return reportPearlSubsidy(result, options.cutoffUtc, false);
    } catch (...) {
        if (stopped()) return reviewError("review cancelled", 130);
        return reviewError("could not review the saved indexer response");
    }
}

}  // namespace om
