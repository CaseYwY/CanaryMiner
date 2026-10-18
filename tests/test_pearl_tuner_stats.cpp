#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "algos/pearl-pow/tuner_stats.h"
#include "pearl_tile_timing_refusal_fixture.h"

namespace {

using namespace om::pearl::tuner;

int failures = 0;

void check(const char *name, bool condition) {
    if (condition) {
        std::printf("  ok    %s\n", name);
    } else {
        std::printf("  FAIL  %s\n", name);
        ++failures;
    }
}

bool near(double actual, double expected, double tolerance = 1e-12) {
    return std::abs(actual - expected) <= tolerance;
}

bool isPermutation(const std::vector<std::size_t> &order,
                   std::size_t count) {
    if (order.size() != count) return false;
    std::vector<std::size_t> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 0; i < count; ++i) {
        if (sorted[i] != i) return false;
    }
    return true;
}

void testExactSummary() {
    const auto summary = summarizeRates({101, 98, 99, 102, 100, 101, 99});
    check("seven valid rates are sorted exactly",
          summary.sortedSamples ==
              std::vector<double>({98, 99, 99, 100, 101, 101, 102}));
    check("odd-sample median is exact", summary.median == 100.0);
    check("odd-sample MAD is exact",
          summary.medianAbsoluteDeviation == 1.0);
    check("robust relative spread is 1.4826 * MAD / median",
          near(summary.relativeSpread, 0.014826));
    check("inclusive five-percent band counts every fixture sample",
          summary.inBandCount == 7);
    check("conservative lower bound is exact",
          near(summary.conservativeLowerBound, 97.0348));
    check("conservative upper bound is exact",
          near(summary.conservativeUpperBound, 102.9652));
    check("exact fixture is stable with no rejection",
          summary.stable && summary.rejection == RejectionReason::None);
}

void testRobustnessPolicy() {
    const auto lucky =
        summarizeRates({100, 100, 100, 100, 100, 100, 1000000000});
    const auto sustainablyFaster =
        summarizeRates({109, 110, 110, 110, 110, 110, 111});
    check("one impossibly fast outlier cannot lift the median",
          lucky.stable && sustainablyFaster.stable &&
              sustainablyFaster.median > lucky.median);

    const auto preempted =
        summarizeRates({1, 99, 100, 100, 100, 101, 102});
    check("one slow preemption outlier is tolerated",
          preempted.stable && preempted.inBandCount == 6 &&
              near(preempted.relativeSpread, 0.014826));

    const auto dispersed =
        summarizeRates({98, 98, 100, 100, 100, 102, 102});
    check("MAD spread over 2.5 percent rejects despite seven in band",
          !dispersed.stable && dispersed.inBandCount == 7 &&
              dispersed.rejection ==
                  RejectionReason::ExcessiveRelativeSpread);

    const auto sparseBand =
        summarizeRates({90, 100, 100, 100, 100, 110, 120});
    check("fewer than five in-band samples rejects despite zero MAD",
          !sparseBand.stable && sparseBand.inBandCount == 4 &&
              sparseBand.relativeSpread == 0.0 &&
              sparseBand.rejection ==
                  RejectionReason::InsufficientInBandSamples);

    const auto inclusiveBand =
        summarizeRates({95, 100, 100, 100, 100, 105, 1000});
    check("five-percent band endpoints are inclusive",
          inclusiveBand.stable && inclusiveBand.inBandCount == 6);

    const auto zeroMad = summarizeRates({42, 42, 42, 42, 42, 42, 42});
    check("identical measurements have zero-width conservative bounds",
          zeroMad.stable && zeroMad.relativeSpread == 0.0 &&
              zeroMad.conservativeLowerBound == 42.0 &&
              zeroMad.conservativeUpperBound == 42.0);

    const auto clamped =
        summarizeRates({1, 1, 1, 2, 3, 3, 3});
    check("conservative lower bound never goes below zero",
          clamped.conservativeLowerBound == 0.0);
}

void testInvalidSamplesFailClosed() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const double denormal = std::numeric_limits<double>::denorm_min();
    const std::vector<std::pair<const char *, std::vector<double>>> invalid = {
        {"too few", {1, 1, 1, 1, 1, 1}},
        {"too many", {1, 1, 1, 1, 1, 1, 1, 1}},
        {"zero", {1, 1, 1, 0, 1, 1, 1}},
        {"negative", {1, 1, 1, -1, 1, 1, 1}},
        {"NaN", {1, 1, 1, nan, 1, 1, 1}},
        {"positive infinity", {1, 1, 1, infinity, 1, 1, 1}},
        {"negative infinity", {1, 1, 1, -infinity, 1, 1, 1}},
        {"subnormal", {1, 1, 1, denormal, 1, 1, 1}},
    };
    for (const auto &fixture : invalid) {
        const auto summary = summarizeRates(fixture.second);
        const std::string name =
            std::string(fixture.first) + " rate sample fails closed";
        check(name.c_str(), !summary.stable &&
                                summary.rejection != RejectionReason::None);
    }
    check("wrong sample counts have an explicit reason",
          summarizeRates({1, 1, 1, 1, 1, 1}).rejection ==
                  RejectionReason::WrongSampleCount &&
              summarizeRates({1, 1, 1, 1, 1, 1, 1, 1}).rejection ==
                  RejectionReason::WrongSampleCount);
    check("non-finite samples have an explicit reason",
          summarizeRates({1, 1, 1, nan, 1, 1, 1}).rejection ==
              RejectionReason::NonFiniteSample);
    check("non-positive samples have an explicit reason",
          summarizeRates({1, 1, 1, 0, 1, 1, 1}).rejection ==
              RejectionReason::NonPositiveSample);
    check("subnormal samples have an explicit reason",
          summarizeRates({1, 1, 1, denormal, 1, 1, 1}).rejection ==
              RejectionReason::NonNormalSample);

    const double maximum = std::numeric_limits<double>::max();
    const auto overflowingBounds = summarizeRates(
        {maximum * 0.99, maximum * 0.99, maximum, maximum,
         maximum, maximum, maximum});
    check("overflowing finite derived bounds fail closed",
          !overflowingBounds.stable &&
              overflowingBounds.rejection ==
                  RejectionReason::NonFiniteDerivedStatistic);
}

void testDurationConversion() {
    const auto ordinary = rateFromDuration(500, 2.0);
    check("duration converts milliseconds to candidates per second",
          ordinary.valid && ordinary.candidatesPerSecond == 250000.0 &&
              ordinary.rejection == RejectionReason::None);
    check("duration equal to the event floor is rejected",
          rateFromDuration(1, kEventResolutionFloorMs).rejection ==
              RejectionReason::DurationAtResolutionFloor);
    check("duration below the event floor is rejected",
          rateFromDuration(1, std::nextafter(kEventResolutionFloorMs, 0.0))
                  .rejection ==
              RejectionReason::DurationAtResolutionFloor);
    check("duration just above the event floor is accepted",
          rateFromDuration(
              1, std::nextafter(kEventResolutionFloorMs,
                                std::numeric_limits<double>::infinity()))
              .valid);
    check("zero, negative, NaN and infinite durations are rejected",
          !rateFromDuration(1, 0).valid &&
              !rateFromDuration(1, -1).valid &&
              !rateFromDuration(
                   1, std::numeric_limits<double>::quiet_NaN())
                   .valid &&
              !rateFromDuration(
                   1, std::numeric_limits<double>::infinity())
                   .valid);
    check("zero candidate work is rejected",
          rateFromDuration(0, 1).rejection ==
              RejectionReason::InvalidCandidateCount);
}

void testOrder() {
    constexpr std::size_t candidates = 9;
    const auto first = makeSevenRoundOrder(candidates, "full-key-A", 409632768);
    const auto repeat =
        makeSevenRoundOrder(candidates, "full-key-A", 409632768);
    const auto changedKey =
        makeSevenRoundOrder(candidates, "full-key-B", 409632768);
    const auto changedShape =
        makeSevenRoundOrder(candidates, "full-key-A", 409632769);

    bool everyRoundComplete = true;
    for (const auto &round : first) {
        everyRoundComplete &= isPermutation(round, candidates);
    }
    check("every candidate appears exactly once in every round",
          everyRoundComplete);
    check("the same full key and shape reproduce all seven rounds",
          first == repeat);
    check("a changed tuning key changes the schedule", first != changedKey);
    check("a changed shape changes the schedule", first != changedShape);

    bool pairsAreReversed = true;
    bool positionsBalance = true;
    for (std::size_t pair = 0; pair < 3; ++pair) {
        const auto &forward = first[pair * 2];
        const auto &reverse = first[pair * 2 + 1];
        for (std::size_t position = 0; position < candidates; ++position) {
            pairsAreReversed &=
                forward[position] == reverse[candidates - 1 - position];
        }
        for (std::size_t candidate = 0; candidate < candidates; ++candidate) {
            const auto f = static_cast<std::size_t>(std::find(
                               forward.begin(), forward.end(), candidate) -
                           forward.begin());
            const auto r = static_cast<std::size_t>(std::find(
                               reverse.begin(), reverse.end(), candidate) -
                           reverse.begin());
            positionsBalance &= f + r == candidates - 1;
        }
    }
    check("round pairs use a permutation and its exact reverse",
          pairsAreReversed);
    check("each permutation/reverse pair balances every candidate position",
          positionsBalance);
    check("fresh pair seeds do not preserve one accidental order",
          first[0] != first[2] && first[2] != first[4] &&
              first[4] != first[6]);

    const auto empty = makeSevenRoundOrder(0, "key", 1);
    const auto singleton = makeSevenRoundOrder(1, "key", 1);
    check("empty candidate sets produce seven empty complete rounds",
          std::all_of(empty.begin(), empty.end(),
                      [](const auto &round) { return round.empty(); }));
    check("singleton candidate sets produce seven singleton rounds",
          std::all_of(singleton.begin(), singleton.end(), [](const auto &round) {
              return round == std::vector<std::size_t>{0};
          }));

    check("seed derivation is stable and domain-separated",
          deriveOrderSeed("ab", 12, 3) == deriveOrderSeed("ab", 12, 3) &&
              deriveOrderSeed("ab", 12, 3) !=
                  deriveOrderSeed("a", 0x6200000000000000ULL, 12));

    const std::vector<std::size_t> expectedFirst = {6, 5, 7, 2, 8, 4, 3, 0, 1};
    check("SplitMix64/Fisher-Yates sequence matches the pinned fixture",
          first[0] == expectedFirst);
}

void testShapeRoundSchedule() {
    // The CUDA shape stage has twelve source candidates and uses this domain.
    // Its old 4-forward/3-reverse blocks let forward traversal own a median
    // majority under monotonic drift. Seven rounds provide three exact
    // reverse pairs for the drift-normalized scorer and one confirmation.
    constexpr std::size_t shapes = 12;
    const auto rounds = makeSevenRoundOrder(
        shapes, "synthetic-device-uuid", UINT64_C(0x66756c6c6174746d));
    bool eachRoundHasEveryShapeOnce = true;
    for (const auto &round : rounds)
        eachRoundHasEveryShapeOnce &= isPermutation(round, shapes);
    check("shape timing is seven one-sample-per-shape rounds",
          eachRoundHasEveryShapeOnce);

    bool pairedPositionsCancel = true;
    for (std::size_t pair = 0; pair < 3; ++pair) {
        for (std::size_t shape = 0; shape < shapes; ++shape) {
            const auto &forward = rounds[pair * 2];
            const auto &reverse = rounds[pair * 2 + 1];
            const std::size_t forwardPosition = static_cast<std::size_t>(
                std::find(forward.begin(), forward.end(), shape) -
                forward.begin());
            const std::size_t reversePosition = static_cast<std::size_t>(
                std::find(reverse.begin(), reverse.end(), shape) -
                reverse.begin());
            pairedPositionsCancel &=
                forwardPosition + reversePosition == shapes - 1;
        }
    }
    check("shape round pairs cancel traversal-position drift",
          pairedPositionsCancel);
}

std::vector<std::vector<double>> driftedSamples(
        const SevenRoundOrder &rounds, const std::vector<double> &baseRates,
        double logSlope) {
    std::vector<std::vector<double>> samples(
        baseRates.size(), std::vector<double>(kSampleCount));
    std::size_t globalIndex = 0;
    for (std::size_t round = 0; round < rounds.size(); ++round) {
        for (std::size_t candidate : rounds[round]) {
            samples[candidate][round] =
                baseRates[candidate] *
                std::exp(logSlope * static_cast<double>(globalIndex));
            ++globalIndex;
        }
    }
    return samples;
}

void testDriftNormalizedShapeSelection() {
    const PairedRateSummary exact =
        summarizePairedRates({100, 121, 110, 110, 121, 100, 109});
    check("paired score is the median of three geometric means",
          exact.valid && near(exact.pairScores[0], 110.0) &&
              near(exact.pairScores[1], 110.0) &&
              near(exact.pairScores[2], 110.0) &&
              near(exact.medianPairScore, 110.0) &&
              exact.confirmationRate == 109.0);
    check("paired scoring rejects partial and non-finite records",
          summarizePairedRates({1, 1, 1, 1, 1, 1}).rejection ==
                  RejectionReason::WrongSampleCount &&
              summarizePairedRates(
                  {1, 1, 1, 1, 1,
                   std::numeric_limits<double>::infinity(), 1})
                      .rejection == RejectionReason::NonFiniteSample);

    const auto dropout = selectPairedMedianWinner({
        {150, 150, 150, 150, 150, 150, 150},
        {200, 200, 200, 200, 200, 200}});
    check("one candidate dropout aborts the entire paired ranking",
          !dropout.valid &&
              dropout.rejection == RejectionReason::WrongSampleCount);

    const auto twoHighOutliers = selectPairedMedianWinner({
        {150, 150, 150, 150, 150, 150, 150},
        {200, 200, 200, 200, 1, 1, 200}});
    check("two matching high pair scores cannot beat a stable candidate",
          !twoHighOutliers.valid &&
              twoHighOutliers.rejection ==
                  RejectionReason::ExcessiveRelativeSpread);
    const auto twoLowOutliers = selectPairedMedianWinner({
        {150, 150, 150, 150, 150, 150, 150},
        {200, 200, 1, 1, 1, 1, 200}});
    check("two matching low pair scores also fail the stability gate",
          !twoLowOutliers.valid &&
              twoLowOutliers.rejection ==
                  RejectionReason::ExcessiveRelativeSpread);
    const auto singletonOutliers = selectPairedMedianWinner({
        {200, 200, 200, 200, 1, 1, 200}});
    check("a pinned singleton cannot normalize away its own dispersion",
          !singletonOutliers.valid &&
              singletonOutliers.rejection ==
                  RejectionReason::ExcessiveRelativeSpread);

    const auto filtered = selectPairedRateWinner(
        {{100, 100, 100, 100, 100, 100, 100},
         {200, 200, 200, 200, 200, 200, 200}},
        {true, false});
    check("selection filter cannot remove a candidate from drift reference",
          filtered.valid && filtered.winner == 0);

    const auto confirmed = selectPairedRateWinner({
        {100, 100, 100, 100, 100, 100, 100},
        {110, 110, 110, 110, 110, 110, 110}});
    check("round seven confirms but does not create the paired winner",
          confirmed.valid && confirmed.winner == 1);
    const auto contradicted = selectPairedRateWinner({
        {100, 100, 100, 100, 100, 100, 120},
        {110, 110, 110, 110, 110, 110, 109}});
    check("round seven contradiction fails closed instead of reranking",
          !contradicted.valid &&
              contradicted.rejection ==
                  RejectionReason::RoundSevenConfirmationFailed);
    const auto tied = selectPairedRateWinner({
        {100, 100, 100, 100, 100, 100, 101},
        {100, 100, 100, 100, 100, 100, 99}});
    check("equal paired medians are rejected",
          !tied.valid &&
              tied.rejection == RejectionReason::AmbiguousPairScore);
    const auto nearTied = selectPairedRateWinner({
        {100, 100, 100, 100, 100, 100, 100},
        {100.05, 100.05, 100.05, 100.05, 100.05, 100.05, 100.05}});
    check("sub-tolerance paired separation is rejected as a tie",
          !nearTied.valid &&
              nearTied.rejection == RejectionReason::AmbiguousPairScore);

    const std::vector<double> baseRates = {100.0, 102.0, 107.0, 113.0};
    const std::vector<double> slopes = {
        -0.05, -0.02, -0.005, 0.0, 0.005, 0.02, 0.05};
    bool pairStageAlwaysSelectsWinner = true;
    bool confirmationAcceptsWinnerOrFailsClosed = true;
    for (int seed = 0; seed < 32; ++seed) {
        const std::string key = "drift-seed-" + std::to_string(seed);
        const auto rounds = makeSevenRoundOrder(
            baseRates.size(), key, UINT64_C(0x66756c6c6174746d));
        for (double slope : slopes) {
            const auto samples = driftedSamples(rounds, baseRates, slope);
            const auto pairSelection = selectPairedMedianWinner(samples);
            pairStageAlwaysSelectsWinner &=
                pairSelection.valid && pairSelection.winner == 3;
            const auto confirmedSelection = selectPairedRateWinner(samples);
            confirmationAcceptsWinnerOrFailsClosed &=
                (confirmedSelection.valid &&
                 confirmedSelection.winner == 3) ||
                (!confirmedSelection.valid &&
                 confirmedSelection.rejection ==
                     RejectionReason::RoundSevenConfirmationFailed);
        }
    }
    check("paired-median stage identifies the invariant winner for every drift",
          pairStageAlwaysSelectsWinner);
    check("round seven only accepts that winner or rejects confirmation",
          confirmationAcceptsWinnerOrFailsClosed);
}

void testFullAttemptEpochPolicy() {
    check("full-attempt retries share the existing two-epoch tile cap",
          kMaximumFullAttemptTuningEpochs == 2 &&
          kMaximumFullAttemptTuningEpochs == kMaximumTileTuningAttempts);
    check("only first rejected spread epoch can request a second full epoch",
          retryFullAttemptEpoch(0, false, RejectionReason::ExcessiveRelativeSpread) &&
          !retryFullAttemptEpoch(1, false, RejectionReason::ExcessiveRelativeSpread) &&
          !retryFullAttemptEpoch(std::numeric_limits<std::size_t>::max(), false,
                                RejectionReason::ExcessiveRelativeSpread));
    check("accepted epoch cannot be retried even with inconsistent reason",
          !retryFullAttemptEpoch(0, true, RejectionReason::ExcessiveRelativeSpread));
    for (RejectionReason reason : {RejectionReason::None,
             RejectionReason::WrongSampleCount, RejectionReason::NonFiniteSample,
             RejectionReason::NonNormalSample, RejectionReason::NonPositiveSample,
             RejectionReason::AmbiguousPairScore,
             RejectionReason::RoundSevenConfirmationFailed,
             RejectionReason::InsufficientInBandSamples,
             RejectionReason::InvalidCandidateCount, RejectionReason::InvalidDuration,
             RejectionReason::DurationAtResolutionFloor,
             RejectionReason::NonFiniteRate, RejectionReason::NonFiniteDerivedStatistic})
        check("full-attempt invalid/partial/confirmation failures cannot retry",
              !retryFullAttemptEpoch(0, false, reason));

    using Matrix = std::vector<std::vector<double>>;
    const Matrix unstable = {{100,100,70,70,130,130,100}};
    const Matrix stable = {{90,90,90,90,90,90,90}};
    const Matrix faster = {{120,120,120,120,120,120,120}};
    const auto unstableBefore = unstable;
    std::vector<Matrix> retained;
    PairedRateSelection selected;
    for (const auto &epoch : {unstable, stable, faster}) {
        retained.push_back(epoch);
        selected = selectPairedRateWinner(retained.back());
        if (selected.valid || !retryFullAttemptEpoch(
                retained.size()-1, selected.valid, selected.rejection)) break;
    }
    check("whole failed epoch retained, first stable epoch accepted without ranking later epoch",
          retained.size()==2 && retained[0]==unstableBefore && retained[1]==stable &&
          selected.valid && near(selected.summaries[0].medianPairScore,90));
    const auto failed = selectPairedRateWinner(unstable);
    check("two unstable complete epochs still fail at the unchanged spread gate",
          !failed.valid && failed.rejection==RejectionReason::ExcessiveRelativeSpread &&
          !retryFullAttemptEpoch(1, failed.valid, failed.rejection));
    check("spread/band/confirmation sample thresholds unchanged",
          kMaximumRelativeSpread==0.025 && kMinimumInBandSamples==5 &&
          kInBandFraction==0.05 && kSampleCount==7);
}

void testTileBatchAccounting() {
    check("tile batch size fixed at eight", kTileBatchCount == 8);
    std::uint64_t completed = 99;
    unsigned calls = 0;
    check("complete tile observation launches exactly eight times",
          completeTileBatch([&]() { ++calls; return true; }, &completed) &&
          calls == 8 && completed == 8);
    const auto full = tileBatchRate(1048576, completed, 64.0);
    const auto doubled = tileBatchRate(1048576, completed, 128.0);
    check("complete batch accounts every tile and the whole duration",
          full.valid && full.candidatesPerSecond == 131072000.0 &&
          doubled.valid && doubled.candidatesPerSecond * 2 == full.candidatesPerSecond);
    for (unsigned failure = 0; failure < 8; ++failure) {
        calls = 0;
        completed = 99;
        const bool ok = completeTileBatch([&]() { return calls++ != failure; }, &completed);
        check("failed launch stops suffix and cannot publish partial work",
              !ok && calls == failure + 1 && completed == failure &&
              !tileBatchRate(1048576, completed, 64).valid);
    }
    calls = 0;
    check("missing count does not launch", !completeTileBatch(
        [&]() { ++calls; return true; }, nullptr) && calls == 0);
    for (std::uint64_t count : {UINT64_C(0), UINT64_C(7), UINT64_C(9)}) {
        const auto rate = tileBatchRate(1048576, count, 64);
        check("wrong batch count rejected", !rate.valid &&
              rate.rejection == RejectionReason::InvalidCandidateCount);
    }
    const auto maxTiles = std::numeric_limits<std::uint64_t>::max() / kTileBatchCount;
    check("largest multiplication without overflow allowed",
          tileBatchRate(maxTiles, 8, 1000).valid);
    for (auto tiles : {UINT64_C(0), maxTiles + 1,
                       std::numeric_limits<std::uint64_t>::max()}) {
        const auto rate = tileBatchRate(tiles, 8, 1000);
        check("zero or overflowing tile work rejected", !rate.valid &&
              rate.rejection == RejectionReason::InvalidCandidateCount);
    }
    for (double ms : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(), kEventResolutionFloorMs})
        check("invalid or resolution-floor batch interval rejected",
              !tileBatchRate(1048576, 8, ms).valid);
}

void testRetainedTileTimingRefusal() {
    const double expectedSpread[] = {0.027275409026462458, 0.025215623960500717};
    for (std::size_t epoch = 0; epoch < 2; ++epoch) {
        std::vector<std::vector<double>> rows;
        for (const auto &row : kGpu0ShapeRefusalRates[epoch])
            rows.emplace_back(std::begin(row), std::end(row));
        const auto result = selectPairedRateWinner(rows);
        check("retained complete 38-row GPU epoch still refuses at unchanged spread gate",
              rows.size() == 38 && !result.valid &&
              result.rejection == RejectionReason::ExcessiveRelativeSpread);
        check("retained first rejecting row and paired statistic reproduced",
              result.summaries.size() == 38 && !result.summaries[8].valid &&
              result.summaries[8].rejection == RejectionReason::ExcessiveRelativeSpread &&
              near(result.summaries[8].relativePairSpread, expectedSpread[epoch], 1e-12));
        for (std::size_t i = 0; i < 8; ++i)
            check("every row before the retained rejection is stable",
                  result.summaries[i].valid &&
                  result.summaries[i].relativePairSpread <= kMaximumRelativeSpread);
    }
}

}  // namespace

int main() {
    std::puts("Pearl robust autotuner host policy");
    check("tile retries have a fixed two-epoch cap",
          kMaximumTileTuningAttempts == 2);
    check("excessive timing spread can request one fresh epoch",
          retryableTimingRejection(RejectionReason::ExcessiveRelativeSpread));
    for (RejectionReason reason : {RejectionReason::None,
             RejectionReason::WrongSampleCount, RejectionReason::NonFiniteSample,
             RejectionReason::NonNormalSample, RejectionReason::NonPositiveSample,
             RejectionReason::AmbiguousPairScore,
             RejectionReason::RoundSevenConfirmationFailed,
             RejectionReason::InsufficientInBandSamples,
             RejectionReason::InvalidCandidateCount, RejectionReason::InvalidDuration,
             RejectionReason::DurationAtResolutionFloor,
             RejectionReason::NonFiniteRate,
             RejectionReason::NonFiniteDerivedStatistic})
        check("invalid/partial measurements never request a statistical retry",
              !retryableTimingRejection(reason));
    testExactSummary();
    testRobustnessPolicy();
    testInvalidSamplesFailClosed();
    testDurationConversion();
    testTileBatchAccounting();
    testRetainedTileTimingRefusal();
    testOrder();
    testShapeRoundSchedule();
    testDriftNormalizedShapeSelection();
    testFullAttemptEpochPolicy();

    if (failures) {
        std::printf("FAIL: %d tuner policy checks failed\n", failures);
        return 1;
    }
    std::puts("PASS: robust summaries and paired drift scoring are deterministic");
    return 0;
}
