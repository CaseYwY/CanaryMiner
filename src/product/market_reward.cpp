#include "market_reward.h"

#include "civil_time.h"
#include "exact_decimal.h"
#include "hash.h"
#include "json_dom.h"
#include "local_review_archive.h"
#include "market_public_sample.h"
#include "native_process_admission.h"

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
constexpr std::string_view rewardArchiveHeader = "CMR1 PearlMainnetTip\n";
constexpr size_t rewardReportBytes = 1024;
constexpr size_t maxRewardArchiveBytes = marketRewardBodyBytes + rewardReportBytes + 256;

bool stringIs(const JsonValue *field, const char *expected) {
    return field && field->isString() && field->text == expected;
}

struct IndexTimestamp {
    uint64_t second;
    bool hasSubsecond;
};

std::optional<IndexTimestamp> utcTimestamp(std::string_view text) {
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
    const auto unixSecond = static_cast<uint64_t>(*serial) * 86400 + hour * 3600 + minute * 60 + second;
    const auto hasSubsecond = text.size() != 20 &&
        text.substr(20, text.size() - 21).find_first_not_of('0') != std::string_view::npos;
    return IndexTimestamp{unixSecond, hasSubsecond};
}

std::optional<std::filesystem::path> nativeReviewFile(int argc, const char *const *argv,
                                                       const MarketRewardOptions &options) {
    const bool opening = options.action == MarketRewardAction::OpenReview;
    if (argc != (opening ? 4 : 6)) return {};
#if defined(_WIN32)
    (void)argv;
    int count = 0;
    struct Arguments {
        LPWSTR *values;
        ~Arguments() { if (values) LocalFree(values); }
    } args{CommandLineToArgvW(GetCommandLineW(), &count)};
    if (!args.values || count != argc) return {};
    const auto *a = args.values;
    if (std::wstring_view(a[1]) != L"market-reward" ||
        std::wstring_view(a[2]) != (opening ? L"--open-review" : L"--file") ||
        !a[3][0] || a[3][0] == L'-' || (!opening &&
        (std::wstring_view(a[4]) != L"--as-of" || std::wstring_view(a[5]) != std::to_wstring(options.cutoffUtc))))
        return {};
#else
    const auto *a = argv;
    if (std::string_view(a[1]) != "market-reward" ||
        std::string_view(a[2]) != (opening ? "--open-review" : "--file") ||
        !a[3][0] || a[3][0] == '-' || (!opening &&
        (std::string_view(a[4]) != "--as-of" || std::string_view(a[5]) != std::to_string(options.cutoffUtc))))
        return {};
#endif
    return std::filesystem::path(a[3]);
}

int reviewError(const char *message, int code = 3) {
    std::fprintf(stderr, "market-reward: %s\n", message);
    return code;
}

std::string renderSubsidy(const PearlSubsidyObservation &sample, uint64_t cutoffUtc,
                          const char *title, const char *cutoffLabel) {
    std::string fraction = std::to_string(sample.nextBlockGrains % grainsPerPrl);
    fraction.insert(0, 8 - fraction.size(), '0');
    return std::string(title) + "\nIndexed height: " + std::to_string(sample.tipHeight) +
        "\nBackend tip hash: " + sample.backendTipHash +
        "\nIndexer update UTC: " + std::to_string(sample.indexUpdateUtc) +
        "\n" + cutoffLabel + std::to_string(cutoffUtc) +
        "\nNext block height: " + std::to_string(sample.tipHeight + 1) +
        "\nNext block subsidy: " + std::to_string(sample.nextBlockGrains / grainsPerPrl) + "." + fraction +
        " PRL (scheduled consensus subsidy, excluding fees)\n";
}

std::string renderCapturedSubsidy(const PearlSubsidyObservation &sample, uint64_t sentUtc,
                                  uint64_t observedUtc, uint64_t receivedUtc) {
    return renderSubsidy(sample, observedUtc,
                         "Pearl mainnet subsidy [historical Blockbook capture]", "Observation UTC: ") +
        "Request start UTC: " + std::to_string(sentUtc) + "\nCapture completed UTC: " +
        std::to_string(receivedUtc) + "\n";
}

int writeRewardReport(const std::string &text) {
    return std::fwrite(text.data(), 1, text.size(), stdout) == text.size() && std::fflush(stdout) == 0 ? 0 : 1;
}

const char *replayCapturedSubsidy(const std::string &bytes, uint64_t now, std::string &report) {
    using local_review_archive::readArchiveNumber;
    using local_review_archive::readArchivePart;
    size_t offset = rewardArchiveHeader.size();
    int64_t sent = 0, observed = 0, received = 0;
    std::string body, savedReport;
    if (std::string_view(bytes).substr(0, offset) != rewardArchiveHeader ||
        !readArchiveNumber(bytes, offset, rewardUtcLimit, sent) ||
        !readArchiveNumber(bytes, offset, rewardUtcLimit, observed) ||
        !readArchiveNumber(bytes, offset, rewardUtcLimit, received) ||
        !readArchivePart(bytes, offset, marketRewardBodyBytes, body) ||
        !readArchivePart(bytes, offset, rewardReportBytes, savedReport) ||
        bytes.size() - offset != 65 || bytes.back() != '\n' ||
        bytes.compare(offset, 64, sha256Hex(bytes.substr(0, offset))) != 0)
        return "reward review archive unavailable or invalid";
    if (sent > observed || observed > received || static_cast<uint64_t>(received) > now)
        return "reward review observation times are invalid or in the future";
    const auto result = parsePearlBlockbookTip(body, static_cast<uint64_t>(observed),
                                              PearlRewardCutoff::WholeObservationSecond);
    if (!result.value) return "saved Pearl mainnet indexer response is invalid or stale at capture";
    report = renderCapturedSubsidy(*result.value, sent, observed, received);
    return report == savedReport ? nullptr : "saved reward report differs from the recalculated report";
}

}  // namespace

bool nativeMarketRewardInvocation(const MarketRewardOptions &options, int argc, const char *const *argv) {
    using market_detail::nativeMarketArgumentPair;
    if (options.action == MarketRewardAction::Help)
        return argc == 3 && (nativeMarketArgumentPair(argc, argv, 1, "market-reward", 2, "--help") ||
                             nativeMarketArgumentPair(argc, argv, 1, "market-reward", 2, "-h"));
    return argc == 3 + (options.dnsFlag ? 2 : 0) + (options.saveFlag ? 2 : 0) &&
        nativeMarketArgumentPair(argc, argv, 1, "market-reward", 2, "--allow-public-reward") &&
        (!options.dnsFlag || nativeMarketArgumentPair(argc, argv, options.dnsFlag, "--dns-server",
                                                      options.dnsFlag + 1, options.dnsServer)) &&
        (!options.saveFlag || nativeMarketArgumentPair(argc, argv, options.saveFlag, "--save-review",
                                                       options.saveFlag + 1, options.archiveName));
}

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

PearlRewardResult parsePearlBlockbookTip(const std::string &body, uint64_t cutoffUtc, PearlRewardCutoff precision) {
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
    if (!indexUpdateUtc || indexUpdateUtc->second > cutoffUtc ||
        (precision == PearlRewardCutoff::ExactSecond && indexUpdateUtc->second == cutoffUtc && indexUpdateUtc->hasSubsecond) ||
        cutoffUtc - indexUpdateUtc->second > maxIndexAgeSeconds)
        return {PearlRewardStatus::Stale, {}};
    const auto subsidy = pearlMainnetSubsidyGrains(static_cast<uint32_t>(bestHeight + 1));
    if (!subsidy) return {};
    return {PearlRewardStatus::Ok, PearlSubsidyObservation{static_cast<uint32_t>(bestHeight), hash->text,
                                                         indexUpdateUtc->second, *subsidy}};
}

int reportPearlSubsidy(const PearlRewardResult &result, uint64_t cutoffUtc, bool live) {
    if (!result.value) {
        if (result.status == PearlRewardStatus::Unsynced) return reviewError("Pearl indexer and backend are not synchronized");
        if (result.status == PearlRewardStatus::Stale) return reviewError("Pearl index time is invalid, future or over 1800 seconds old");
        return reviewError("invalid Pearl mainnet indexer response");
    }
    return writeRewardReport(renderSubsidy(*result.value, cutoffUtc,
        live ? "Pearl mainnet subsidy [live Blockbook indexer, locally observed]" :
               "Pearl mainnet subsidy [saved Blockbook status, user-declared cutoff]",
        live ? "Observation UTC: " : "Declared cutoff UTC: "));
}

int savePublicMarketRewardReview(const std::string &name, const PublicJsonResponse &response,
                                 uint64_t sentUtc, uint64_t receivedUtc, const PearlRewardResult &result,
                                 const NativeProcessLifetime &lifetime, const volatile sig_atomic_t *stop) {
    using local_review_archive::ReviewSave;
    if (!result.value) return reportPearlSubsidy(result, response.observedUtc, true);
    const auto report = renderCapturedSubsidy(*result.value, sentUtc, response.observedUtc, receivedUtc);
    if (report.size() > rewardReportBytes) return reviewError("reward review report exceeds the archive bound");
    std::string bytes(rewardArchiveHeader);
    bytes += std::to_string(sentUtc) + '\n' + std::to_string(response.observedUtc) + '\n' +
        std::to_string(receivedUtc) + '\n' + std::to_string(response.body.size()) + '\n' + response.body +
        std::to_string(report.size()) + '\n' + report;
    const auto state = local_review_archive::saveWhileActive(name, rewardReviewExtension,
        bytes + sha256Hex(bytes) + '\n', maxRewardArchiveBytes, [&] { return (stop && *stop) || !lifetime.permitted(); });
    if (state == ReviewSave::Stopped)
        return lifetime.permitted() ? reviewError("sample cancelled", 130) : reviewError("helper ownership uncertain");
    if (state == ReviewSave::Uncertain)
        return reviewError("archive publication or cleanup is uncertain; inspect the review and its temporary file");
    if (state != ReviewSave::Written)
        return reviewError("could not create the new reward review archive; any existing archive was preserved");
    return writeRewardReport(report);
}

int commandMarketRewardFile(const MarketRewardOptions &options, int argc, const char *const *argv,
                            const volatile sig_atomic_t *stop) {
    const auto stopped = [&] { return stop && *stop; };
    if (stopped()) return reviewError("review cancelled", 130);
    try {
        const auto path = nativeReviewFile(argc, argv, options);
        if (!path) return reviewError("invalid arguments. See market-reward --help", 2);
        std::string body;
        const bool opening = options.action == MarketRewardAction::OpenReview;
        const bool read = readLocalReviewInput(*path, opening ? maxRewardArchiveBytes : marketRewardBodyBytes, body);
        if (stopped()) return reviewError("review cancelled", 130);
        if (!read || body.empty()) return reviewError("input file unavailable, empty, oversized or unsuitable");
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (now < 1 || static_cast<uint64_t>(now) > rewardUtcLimit)
            return reviewError("local UTC is unavailable");
        if (opening) {
            std::string report;
            const auto *problem = replayCapturedSubsidy(body, static_cast<uint64_t>(now), report);
            if (stopped()) return reviewError("review cancelled", 130);
            return problem ? reviewError(problem) : writeRewardReport(report);
        }
        if (options.cutoffUtc > static_cast<uint64_t>(now)) return reviewError("declared cutoff is in the future");
        const auto result = parsePearlBlockbookTip(body, options.cutoffUtc, PearlRewardCutoff::ExactSecond);
        if (stopped()) return reviewError("review cancelled", 130);
        return reportPearlSubsidy(result, options.cutoffUtc, false);
    } catch (...) {
        if (stopped()) return reviewError("review cancelled", 130);
        return reviewError("could not review the saved indexer response");
    }
}

}  // namespace om
