#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <string_view>
#include <vector>

namespace om::pearl::tuner {

// Exact operator pins must never degrade into an automatic selection request.
inline bool parseExactShapePin(std::string_view text, std::uint32_t *m,
                               std::uint32_t *n) {
    if (!m || !n || text.empty()) return false;
    const auto separator = text.find('x');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 == text.size()) return false;
    std::uint32_t parsedM = 0, parsedN = 0;
    const char *begin = text.data(), *end = begin + text.size();
    const auto a = std::from_chars(begin, begin + separator, parsedM);
    const auto b = std::from_chars(begin + separator + 1, end, parsedN);
    if (a.ec != std::errc{} || a.ptr != begin + separator ||
        b.ec != std::errc{} || b.ptr != end || !parsedM || !parsedN) return false;
    *m = parsedM; *n = parsedN;
    return true;
}

constexpr std::size_t kSampleCount = 7;
constexpr double kMadScale = 1.4826;
constexpr double kMaximumRelativeSpread = 0.025;
constexpr double kInBandFraction = 0.05;
constexpr std::size_t kMinimumInBandSamples = 5;
constexpr double kEventResolutionFloorMs = 0.0005;
constexpr double kPairedTieRelativeTolerance = 0.001;
constexpr std::size_t kMaximumTileTuningAttempts = 2;
// Same bounded whole-epoch policy as tile timing; never rank retry epochs.
constexpr std::size_t kMaximumFullAttemptTuningEpochs =
    kMaximumTileTuningAttempts;

enum class RejectionReason {
    None,
    WrongSampleCount,
    NonFiniteSample,
    NonNormalSample,
    NonPositiveSample,
    ExcessiveRelativeSpread,
    InsufficientInBandSamples,
    InvalidCandidateCount,
    InvalidDuration,
    DurationAtResolutionFloor,
    NonFiniteRate,
    NonFiniteDerivedStatistic,
    AmbiguousPairScore,
    RoundSevenConfirmationFailed,
};

// Repeat the ENTIRE measurement epoch, never omit a row or retain favorable
// samples. Invalid data, failed CUDA calls and correctness failures cannot use
// this path. The first stable epoch wins; do not rank epochs by their rates.
constexpr bool retryableTimingRejection(RejectionReason reason) {
    return reason == RejectionReason::ExcessiveRelativeSpread;
}

constexpr bool retryFullAttemptEpoch(std::size_t completedEpoch,
                                    bool selectionValid,
                                    RejectionReason reason) {
    return completedEpoch < kMaximumFullAttemptTuningEpochs - 1 &&
        !selectionValid && retryableTimingRejection(reason);
}

constexpr const char *rejectionReasonName(RejectionReason reason) {
    switch (reason) {
        case RejectionReason::None: return "none";
        case RejectionReason::WrongSampleCount: return "wrong-sample-count";
        case RejectionReason::NonFiniteSample: return "non-finite-sample";
        case RejectionReason::NonNormalSample: return "non-normal-sample";
        case RejectionReason::NonPositiveSample: return "non-positive-sample";
        case RejectionReason::ExcessiveRelativeSpread:
            return "excessive-relative-spread";
        case RejectionReason::InsufficientInBandSamples:
            return "insufficient-in-band-samples";
        case RejectionReason::InvalidCandidateCount:
            return "invalid-candidate-count";
        case RejectionReason::InvalidDuration: return "invalid-duration";
        case RejectionReason::DurationAtResolutionFloor:
            return "duration-at-resolution-floor";
        case RejectionReason::NonFiniteRate: return "non-finite-rate";
        case RejectionReason::NonFiniteDerivedStatistic:
            return "non-finite-derived-statistic";
        case RejectionReason::AmbiguousPairScore:
            return "ambiguous-pair-score";
        case RejectionReason::RoundSevenConfirmationFailed:
            return "round-seven-confirmation-failed";
    }
    return "unknown";
}

struct RobustSummary {
    std::vector<double> sortedSamples;
    double median = 0.0;
    double medianAbsoluteDeviation = 0.0;
    double relativeSpread = 0.0;
    std::size_t inBandCount = 0;
    double conservativeLowerBound = 0.0;
    double conservativeUpperBound = 0.0;
    bool stable = false;
    RejectionReason rejection = RejectionReason::WrongSampleCount;
};

/** Summarize exactly seven candidates/second measurements, failing closed. */
inline RobustSummary summarizeRates(const std::vector<double> &samples) {
    RobustSummary result;
    if (samples.size() != kSampleCount) return result;

    for (double sample : samples) {
        if (!std::isfinite(sample)) {
            result.rejection = RejectionReason::NonFiniteSample;
            return result;
        }
        if (sample <= 0.0) {
            result.rejection = RejectionReason::NonPositiveSample;
            return result;
        }
        if (!std::isnormal(sample)) {
            result.rejection = RejectionReason::NonNormalSample;
            return result;
        }
    }
    result.sortedSamples = samples;
    std::sort(result.sortedSamples.begin(), result.sortedSamples.end());
    result.median = result.sortedSamples[kSampleCount / 2];

    std::array<double, kSampleCount> deviations{};
    for (std::size_t i = 0; i < kSampleCount; ++i) {
        deviations[i] = std::abs(result.sortedSamples[i] - result.median);
    }
    std::sort(deviations.begin(), deviations.end());
    result.medianAbsoluteDeviation = deviations[kSampleCount / 2];
    const double scaledMad = kMadScale * result.medianAbsoluteDeviation;
    result.relativeSpread = scaledMad / result.median;
    result.conservativeLowerBound =
        std::max(0.0, result.median - 2.0 * scaledMad);
    result.conservativeUpperBound = result.median + 2.0 * scaledMad;

    const double lowerBand = result.median * (1.0 - kInBandFraction);
    const double upperBand = result.median * (1.0 + kInBandFraction);
    if (!std::isfinite(scaledMad) || !std::isfinite(result.relativeSpread) ||
        !std::isfinite(result.conservativeLowerBound) ||
        !std::isfinite(result.conservativeUpperBound) ||
        !std::isfinite(lowerBand) || !std::isfinite(upperBand)) {
        result.rejection = RejectionReason::NonFiniteDerivedStatistic;
        return result;
    }
    result.inBandCount = static_cast<std::size_t>(std::count_if(
        result.sortedSamples.begin(), result.sortedSamples.end(),
        [lowerBand, upperBand](double sample) {
            return sample >= lowerBand && sample <= upperBand;
        }));

    if (result.relativeSpread > kMaximumRelativeSpread) {
        result.rejection = RejectionReason::ExcessiveRelativeSpread;
        return result;
    }
    if (result.inBandCount < kMinimumInBandSamples) {
        result.rejection = RejectionReason::InsufficientInBandSamples;
        return result;
    }
    result.stable = true;
    result.rejection = RejectionReason::None;
    return result;
}

struct RateSample {
    double candidatesPerSecond = 0.0;
    bool valid = false;
    RejectionReason rejection = RejectionReason::InvalidDuration;
};

/** Convert a checked event interval into candidates/second. */
inline RateSample rateFromDuration(std::uint64_t candidates,
                                   double durationMs) {
    RateSample result;
    if (candidates == 0) {
        result.rejection = RejectionReason::InvalidCandidateCount;
        return result;
    }
    if (!std::isfinite(durationMs) || durationMs <= 0.0) {
        result.rejection = RejectionReason::InvalidDuration;
        return result;
    }
    if (durationMs <= kEventResolutionFloorMs) {
        result.rejection = RejectionReason::DurationAtResolutionFloor;
        return result;
    }
    result.candidatesPerSecond =
        static_cast<double>(candidates) * 1000.0 / durationMs;
    if (!std::isfinite(result.candidatesPerSecond) ||
        result.candidatesPerSecond <= 0.0) {
        result.candidatesPerSecond = 0.0;
        result.rejection = RejectionReason::NonFiniteRate;
        return result;
    }
    result.valid = true;
    result.rejection = RejectionReason::None;
    return result;
}

// Tile timing includes every launch in a fixed serial batch, never a prefix.
// Completion here counts successful callbacks; CUDA callers must additionally
// synchronize the terminal event before treating the interval as valid.
constexpr std::uint64_t kTileBatchCount = 8;

inline RateSample tileBatchRate(std::uint64_t tiles,
                               std::uint64_t completed, double durationMs) {
    if (completed != kTileBatchCount || tiles == 0 ||
        tiles > std::numeric_limits<std::uint64_t>::max() / kTileBatchCount)
        return {0.0, false, RejectionReason::InvalidCandidateCount};
    return rateFromDuration(tiles * kTileBatchCount, durationMs);
}

template <typename LaunchTile>
inline bool completeTileBatch(LaunchTile &&launch, std::uint64_t *completed) {
    if (!completed) return false;
    *completed = 0;
    for (std::uint64_t slot = 0; slot < kTileBatchCount; ++slot) {
        if (!launch()) return false;
        ++*completed;
    }
    return true;
}

// A timing observation is a fixed complete serial batch, not a best prefix.
// Build timing-row indices without discarding correctness observations.
inline bool timingRowIndices(const std::vector<bool> &selectable, bool pinnedOnly,
                             std::vector<std::size_t> *out) {
    if (!out || selectable.empty()) return false;
    std::vector<std::size_t> rows;
    for (std::size_t i=0;i<selectable.size();++i)
        if (!pinnedOnly || selectable[i]) rows.push_back(i);
    if (pinnedOnly && rows.size()!=1) return false;
    *out=std::move(rows);
    return true;
}

constexpr std::uint64_t kFullAttemptBatchCount = 32;
static_assert(kMaximumFullAttemptTuningEpochs <= 2 && kSampleCount <= 8 &&
              kFullAttemptBatchCount < 64, "timing nonce field widths");

inline RateSample fullAttemptBatchRate(std::uint64_t tiles,
                                      std::uint64_t completed,
                                      double durationMs) {
    if (completed != kFullAttemptBatchCount || tiles == 0 ||
        tiles > std::numeric_limits<std::uint64_t>::max() / kFullAttemptBatchCount)
        return {0.0, false, RejectionReason::InvalidCandidateCount};
    return rateFromDuration(tiles * kFullAttemptBatchCount, durationMs);
}

// Synthetic-only domain, 17-bit M/N (including N=65536), one epoch bit,
// three round bits and six slot bits. Slot 0 warms up; slots 1..32 measure.
// The fields are disjoint; changing epochs cannot repeat a timing nonce.
inline bool fullAttemptNonce(std::uint32_t m, std::uint32_t n,
                             std::size_t epoch, std::size_t round,
                             std::uint64_t slot, std::uint64_t *out) {
    if (!out || m == 0 || n == 0 || m >= (1U << 18) || n >= (1U << 18) ||
        epoch >= kMaximumFullAttemptTuningEpochs || round >= kSampleCount ||
        slot > kFullAttemptBatchCount) return false;
    // Separate synthetic domain for 18-bit dimensions. Keep legacy nonces
    // unchanged; disjoint fields use 18+18+1+3+6 bits plus two spare bits.
    if (m >= (1U << 17) || n >= (1U << 17)) {
        *out = UINT64_C(0x51ee000000000000) | (std::uint64_t{m} << 30) |
               (std::uint64_t{n} << 12) | (std::uint64_t{epoch} << 11) |
               (std::uint64_t{round} << 8) | (slot << 2);
        return true;
    }
    *out = UINT64_C(0x51ed000000000000) | (std::uint64_t{m} << 31) |
           (std::uint64_t{n} << 14) | (std::uint64_t{epoch} << 13) |
           (std::uint64_t{round} << 10) | (slot << 4);
    return true;
}

template <typename CompleteAttempt>
inline bool completeFullAttemptBatch(std::uint32_t m, std::uint32_t n,
                                     std::size_t epoch, std::size_t round,
                                     CompleteAttempt &&complete,
                                     std::uint64_t *completed) {
    if (!completed) return false;
    *completed = 0;
    for (std::uint64_t slot = 1; slot <= kFullAttemptBatchCount; ++slot) {
        std::uint64_t nonce = 0;
        if (!fullAttemptNonce(m, n, epoch, round, slot, &nonce) || !complete(nonce))
            return false;
        ++*completed;
    }
    return true;
}

struct PairedRateSummary {
    std::array<double, 3> pairScores{};
    double medianPairScore = 0.0;
    double relativePairSpread = 0.0;
    double confirmationRate = 0.0;
    bool valid = false;
    RejectionReason rejection = RejectionReason::WrongSampleCount;
};

/**
 * Score a seven-round candidate without giving traversal drift a majority.
 * Rounds 0/1, 2/3 and 4/5 are permutation/reverse pairs. The geometric mean
 * of each pair cancels linear drift in log-rate because every candidate's two
 * global sample positions sum to the same value. Catalog-wide normalization
 * and stability checks are applied by selectPairedMedianWinner() before the
 * median ranks a candidate. Round seven is retained only as an independent
 * confirmation observation by selectPairedRateWinner().
 */
inline PairedRateSummary summarizePairedRates(
        const std::vector<double> &samples) {
    PairedRateSummary result;
    if (samples.size() != kSampleCount) return result;
    for (double sample : samples) {
        if (!std::isfinite(sample)) {
            result.rejection = RejectionReason::NonFiniteSample;
            return result;
        }
        if (sample <= 0.0) {
            result.rejection = RejectionReason::NonPositiveSample;
            return result;
        }
        if (!std::isnormal(sample)) {
            result.rejection = RejectionReason::NonNormalSample;
            return result;
        }
    }
    for (std::size_t pair = 0; pair < result.pairScores.size(); ++pair) {
        // Half the logs before adding, so two finite near-maximum rates cannot
        // overflow the intermediate sum.
        const double meanLog = 0.5 * std::log(samples[pair * 2]) +
                               0.5 * std::log(samples[pair * 2 + 1]);
        result.pairScores[pair] = std::exp(meanLog);
        if (!std::isfinite(result.pairScores[pair]) ||
            !std::isnormal(result.pairScores[pair])) {
            result.rejection = RejectionReason::NonFiniteDerivedStatistic;
            return result;
        }
    }
    std::array<double, 3> sorted = result.pairScores;
    std::sort(sorted.begin(), sorted.end());
    result.medianPairScore = sorted[1];
    result.confirmationRate = samples[6];
    result.valid = true;
    result.rejection = RejectionReason::None;
    return result;
}

struct FullAttemptDisposition {
    const char *reason = "not-evaluated";
    double observedLow = 0.0;
    double observedHigh = 0.0;
    bool stable = false;
    bool confirmationPassed = false;
};

struct PairedRateSelection {
    std::vector<PairedRateSummary> summaries;
    // Populated only by the explicitly separate full-attempt selector.
    std::vector<FullAttemptDisposition> dispositions;
    std::size_t winner = 0;
    bool valid = false;
    RejectionReason rejection = RejectionReason::WrongSampleCount;
};

/**
 * Select by the three paired scores, before the independent seventh-round
 * confirmation. Each pair is rescaled by one catalog-wide geometric-mean
 * reference. This removes a common log-linear change between pair epochs,
 * while retaining the within-pair forward/reverse cancellation. A candidate
 * must then have three tightly clustered adjusted scores; a median cannot
 * hide two matching high outliers such as [200, 200, 1].
 */
inline PairedRateSelection selectPairedMedianWinner(
        const std::vector<std::vector<double>> &candidateSamples,
        const std::vector<bool> &selectable = {}) {
    PairedRateSelection result;
    result.summaries.reserve(candidateSamples.size());
    for (const auto &samples : candidateSamples)
        result.summaries.push_back(summarizePairedRates(samples));

    if (result.summaries.empty()) return result;
    if (!selectable.empty() && selectable.size() != result.summaries.size()) {
        result.rejection = RejectionReason::InvalidCandidateCount;
        return result;
    }
    // A dropout changes every later candidate's global sample position. Never
    // compare the remaining records as though the planned schedule completed.
    for (const PairedRateSummary &summary : result.summaries) {
        if (!summary.valid) {
            result.rejection = summary.rejection;
            return result;
        }
    }

    std::array<double, 3> references = {1.0, 1.0, 1.0};
    if (result.summaries.size() > 1) {
        for (std::size_t pair = 0; pair < references.size(); ++pair) {
            double meanLog = 0.0;
            for (const PairedRateSummary &summary : result.summaries)
                meanLog += std::log(summary.pairScores[pair]) /
                           static_cast<double>(result.summaries.size());
            references[pair] = std::exp(meanLog);
            if (!std::isfinite(references[pair]) ||
                !std::isnormal(references[pair])) {
                result.rejection = RejectionReason::NonFiniteDerivedStatistic;
                return result;
            }
        }
    }

    for (PairedRateSummary &summary : result.summaries) {
        for (std::size_t pair = 0; pair < summary.pairScores.size(); ++pair) {
            const double adjustedLog = std::log(summary.pairScores[pair]) +
                                       std::log(references[0]) -
                                       std::log(references[pair]);
            summary.pairScores[pair] = std::exp(adjustedLog);
            if (!std::isfinite(summary.pairScores[pair]) ||
                !std::isnormal(summary.pairScores[pair])) {
                summary.valid = false;
                summary.rejection =
                    RejectionReason::NonFiniteDerivedStatistic;
                result.rejection = summary.rejection;
                return result;
            }
        }
        std::array<double, 3> sorted = summary.pairScores;
        std::sort(sorted.begin(), sorted.end());
        summary.medianPairScore = sorted[1];
        summary.relativePairSpread =
            (sorted[2] - sorted[0]) / summary.medianPairScore;
        if (!std::isfinite(summary.relativePairSpread)) {
            summary.valid = false;
            summary.rejection = RejectionReason::NonFiniteDerivedStatistic;
            result.rejection = summary.rejection;
            return result;
        }
        if (summary.relativePairSpread > kMaximumRelativeSpread) {
            summary.valid = false;
            summary.rejection = RejectionReason::ExcessiveRelativeSpread;
            result.rejection = summary.rejection;
            return result;
        }
    }

    bool found = false;
    double best = 0.0;
    for (std::size_t i = 0; i < result.summaries.size(); ++i) {
        const PairedRateSummary &summary = result.summaries[i];
        if (!summary.valid || (!selectable.empty() && !selectable[i])) continue;
        if (!found || summary.medianPairScore > best) {
            found = true;
            best = summary.medianPairScore;
            result.winner = i;
        }
    }
    if (!found) return result;
    for (std::size_t i = 0; i < result.summaries.size(); ++i) {
        if (i == result.winner || !result.summaries[i].valid ||
            (!selectable.empty() && !selectable[i]))
            continue;
        const double other = result.summaries[i].medianPairScore;
        if (!(best > other) ||
            (best - other) / best <= kPairedTieRelativeTolerance) {
            result.rejection = RejectionReason::AmbiguousPairScore;
            return result;
        }
    }

    result.valid = true;
    result.rejection = RejectionReason::None;
    return result;
}

/** Select by stable paired median; round seven may confirm or reject, never rank. */
inline PairedRateSelection selectPairedRateWinner(
        const std::vector<std::vector<double>> &candidateSamples,
        const std::vector<bool> &selectable = {}) {
    PairedRateSelection result =
        selectPairedMedianWinner(candidateSamples, selectable);
    if (!result.valid) return result;
    const double confirmation = result.summaries[result.winner].confirmationRate;
    for (std::size_t i = 0; i < result.summaries.size(); ++i) {
        if (i == result.winner || !result.summaries[i].valid ||
            (!selectable.empty() && !selectable[i]))
            continue;
        // A tie is not confirmation. A reversed ordering under the fresh
        // seventh permutation indicates drift/noise large enough to make the
        // winner ambiguous, so fail closed rather than selecting the runner.
        const double other = result.summaries[i].confirmationRate;
        if (!(confirmation > other) ||
            (confirmation - other) / confirmation <=
                kPairedTieRelativeTolerance) {
            result.valid = false;
            result.rejection =
                RejectionReason::RoundSevenConfirmationFailed;
            return result;
        }
    }
    return result;
}

/** Explicit user pin: validate all observations, assess only the pinned rate.
 * This is not a comparative winner claim. Cross-family transcript correctness
 * admission occurs before timing in the caller and remains mandatory.
 */
inline PairedRateSelection selectExplicitPinnedRate(
        const std::vector<std::vector<double>> &samples,
        const std::vector<bool> &pinned) {
    PairedRateSelection out;
    if (samples.empty() || pinned.size()!=samples.size()) {
        out.rejection=RejectionReason::InvalidCandidateCount;
        return out;
    }
    std::size_t selected=0, count=0;
    for (std::size_t i=0;i<samples.size();++i) {
        out.summaries.push_back(summarizePairedRates(samples[i]));
        if (!out.summaries.back().valid) {
            out.rejection=out.summaries.back().rejection;
            return out;
        }
        if (pinned[i]) {selected=i;++count;}
    }
    if (count!=1) {
        out.rejection=RejectionReason::InvalidCandidateCount;
        return out;
    }
    const auto one=selectPairedRateWinner({samples[selected]});
    out.summaries[selected]=one.summaries[0];
    out.winner=selected;out.valid=one.valid;out.rejection=one.rejection;
    if (out.valid && std::abs(one.summaries[0].confirmationRate -
            one.summaries[0].medianPairScore) / one.summaries[0].medianPairScore >
            kMaximumRelativeSpread) {
        out.valid=false;out.rejection=RejectionReason::RoundSevenConfirmationFailed;
    }
    return out;
}

/**
 * Full-attempt-only empirical admission. The highest paired median must be
 * stable; a noisy loser cannot veto it only when EVERY first-six observation
 * is separated by the unchanged margin. These extrema are observed envelopes,
 * NOT statistical confidence bounds. No row is removed from normalization,
 * and the seventh round challenges the same winner against every row.
 * Tile selection and its nonselectable controls retain the legacy selector.
 */
inline PairedRateSelection selectFullAttemptRateWinner(
        const std::vector<std::vector<double>> &candidateSamples) {
    PairedRateSelection result;
    for (const auto &samples : candidateSamples)
        result.summaries.push_back(summarizePairedRates(samples));
    result.dispositions.resize(candidateSamples.size());
    if (candidateSamples.empty()) return result;
    // Validate every row, including the final challenger/held-out value.
    for (std::size_t i = 0; i < result.summaries.size(); ++i) {
        if (!result.summaries[i].valid) {
            result.dispositions[i].reason = "invalid-data";
            result.rejection = result.summaries[i].rejection;
            return result;
        }
    }
    std::array<double, 3> references = {1.0, 1.0, 1.0};
    if (result.summaries.size() > 1) {
        for (std::size_t pair = 0; pair < references.size(); ++pair) {
            double meanLog = 0.0;
            for (const auto &summary : result.summaries)
                meanLog += std::log(summary.pairScores[pair]) /
                           static_cast<double>(result.summaries.size());
            references[pair] = std::exp(meanLog);
            if (!std::isnormal(references[pair])) {
                result.rejection = RejectionReason::NonFiniteDerivedStatistic;
                return result;
            }
        }
    }
    for (std::size_t i = 0; i < result.summaries.size(); ++i) {
        auto &summary = result.summaries[i];
        auto &disposition = result.dispositions[i];
        for (std::size_t pair = 0; pair < summary.pairScores.size(); ++pair) {
            summary.pairScores[pair] = std::exp(
                std::log(summary.pairScores[pair]) + std::log(references[0]) -
                std::log(references[pair]));
            if (!std::isnormal(summary.pairScores[pair])) {
                summary.valid = false;
                summary.rejection = RejectionReason::NonFiniteDerivedStatistic;
                result.rejection = summary.rejection;
                disposition.reason = "invalid-derived-data";
                return result;
            }
        }
        auto sorted = summary.pairScores;
        std::sort(sorted.begin(), sorted.end());
        summary.medianPairScore = sorted[1];
        summary.relativePairSpread = (sorted[2] - sorted[0]) / sorted[1];
        if (!std::isfinite(summary.relativePairSpread)) {
            summary.valid = false;
            summary.rejection = RejectionReason::NonFiniteDerivedStatistic;
            result.rejection = summary.rejection;
            disposition.reason = "invalid-derived-data";
            return result;
        }
        disposition.stable = summary.relativePairSpread <= kMaximumRelativeSpread;
        const auto bounds = std::minmax_element(candidateSamples[i].begin(),
                                               candidateSamples[i].begin() + 6);
        disposition.observedLow = *bounds.first;
        disposition.observedHigh = *bounds.second;
        if (summary.medianPairScore > result.summaries[result.winner].medianPairScore)
            result.winner = i;
    }
    const auto winner = result.winner;
    if (!result.dispositions[winner].stable) {
        result.dispositions[winner].reason = "unstable-winner";
        result.rejection = RejectionReason::ExcessiveRelativeSpread;
        return result; // Never fall back to a lower stable median.
    }
    result.dispositions[winner].reason = "stable-paired-winner";
    result.rejection = RejectionReason::None;
    const auto reject = [&](RejectionReason reason) {
        if (result.rejection == RejectionReason::None) result.rejection = reason;
    };
    const double best = result.summaries[winner].medianPairScore;
    const double low = result.dispositions[winner].observedLow;
    for (std::size_t i = 0; i < result.summaries.size(); ++i) {
        if (i == winner) continue;
        auto &disposition = result.dispositions[i];
        const double other = result.summaries[i].medianPairScore;
        if (!(best > other) || (best - other) / best <= kPairedTieRelativeTolerance) {
            disposition.reason = "paired-tie";
            reject(RejectionReason::AmbiguousPairScore);
        } else if (disposition.stable) {
            disposition.reason = "stable-lower-paired-score";
        } else if (low > disposition.observedHigh &&
                   (low - disposition.observedHigh) / low > kPairedTieRelativeTolerance) {
            disposition.reason = "unstable-observed-dominated";
        } else {
            disposition.reason = "unresolved-unstable-challenger";
            reject(RejectionReason::ExcessiveRelativeSpread);
        }
    }
    // Record all held-out challenges, but never use them to rescue/rerank.
    const double confirmation = result.summaries[winner].confirmationRate;
    for (std::size_t i = 0; i < result.summaries.size(); ++i) {
        if (i == winner) continue;
        const double other = result.summaries[i].confirmationRate;
        result.dispositions[i].confirmationPassed = confirmation > other &&
            (confirmation - other) / confirmation > kPairedTieRelativeTolerance;
        if (!result.dispositions[i].confirmationPassed)
            reject(RejectionReason::RoundSevenConfirmationFailed);
    }
    result.valid = result.rejection == RejectionReason::None;
    return result;
}

/** Project-owned generator so sample order is independent of std::shuffle. */
class SplitMix64 {
  public:
    explicit constexpr SplitMix64(std::uint64_t seed) : state_(seed) {}

    constexpr std::uint64_t next() {
        std::uint64_t z = (state_ += UINT64_C(0x9e3779b97f4a7c15));
        z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
        return z ^ (z >> 31);
    }

    // Rejection sampling avoids modulo bias without relying on a library
    // distribution whose mapping is permitted to differ across platforms.
    std::uint64_t below(std::uint64_t bound) {
        if (bound <= 1) return 0;
        const std::uint64_t threshold = (std::uint64_t{0} - bound) % bound;
        for (;;) {
            const std::uint64_t value = next();
            if (value >= threshold) return value % bound;
        }
    }

  private:
    std::uint64_t state_;
};

namespace detail {

constexpr std::uint64_t kFnvOffset = UINT64_C(14695981039346656037);
constexpr std::uint64_t kFnvPrime = UINT64_C(1099511628211);

inline void hashByte(std::uint64_t &hash, std::uint8_t byte) {
    hash ^= byte;
    hash *= kFnvPrime;
}

inline void hashUint64(std::uint64_t &hash, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        hashByte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

}  // namespace detail

/** Derive an order seed from the exact tuning-key bytes, shape and round pair. */
inline std::uint64_t deriveOrderSeed(std::string_view tuningKey,
                                     std::uint64_t shape,
                                     std::uint64_t roundPair) {
    std::uint64_t hash = detail::kFnvOffset;
    // Length-prefix the variable field, then hash integers bytewise to make
    // the result independent of host endianness and concatenation choices.
    detail::hashUint64(hash, static_cast<std::uint64_t>(tuningKey.size()));
    for (unsigned char byte : tuningKey) detail::hashByte(hash, byte);
    detail::hashUint64(hash, shape);
    detail::hashUint64(hash, roundPair);
    SplitMix64 avalanche(hash);
    return avalanche.next();
}

inline std::vector<std::size_t> samplingPermutation(std::size_t candidateCount,
                                                    std::uint64_t seed) {
    std::vector<std::size_t> order(candidateCount);
    std::iota(order.begin(), order.end(), std::size_t{0});
    SplitMix64 random(seed);
    for (std::size_t remaining = order.size(); remaining > 1; --remaining) {
        const std::size_t other = static_cast<std::size_t>(
            random.below(static_cast<std::uint64_t>(remaining)));
        std::swap(order[remaining - 1], order[other]);
    }
    return order;
}

using SevenRoundOrder =
    std::array<std::vector<std::size_t>, kSampleCount>;

/** Build three permutation/reverse pairs and one final fresh permutation. */
inline SevenRoundOrder makeSevenRoundOrder(std::size_t candidateCount,
                                           std::string_view tuningKey,
                                           std::uint64_t shape) {
    SevenRoundOrder rounds;
    for (std::size_t pair = 0; pair < 3; ++pair) {
        rounds[pair * 2] = samplingPermutation(
            candidateCount, deriveOrderSeed(tuningKey, shape, pair));
        rounds[pair * 2 + 1] = rounds[pair * 2];
        std::reverse(rounds[pair * 2 + 1].begin(),
                     rounds[pair * 2 + 1].end());
    }
    rounds[6] = samplingPermutation(
        candidateCount, deriveOrderSeed(tuningKey, shape, 3));
    return rounds;
}

}  // namespace om::pearl::tuner
