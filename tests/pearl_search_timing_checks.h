#pragma once
#include "pearl_transcript_snapshot.h"
#include <string_view>

namespace pearl_search_timing_test {
enum class TimingOverlapMode { APrep256, APrep64, Invalid };
inline TimingOverlapMode parseTimingOverlapMode(const char *text) noexcept {
    if (!text || std::string_view(text) == "a-prep-v1") return TimingOverlapMode::APrep256;
    return std::string_view(text) == "a-prep64-v1" ? TimingOverlapMode::APrep64 : TimingOverlapMode::Invalid;
}
inline const char *timingOverlapName(TimingOverlapMode mode) noexcept {
    return mode == TimingOverlapMode::APrep256 ? "a-prep-v1" :
           mode == TimingOverlapMode::APrep64 ? "a-prep64-v1" : nullptr;
}
inline unsigned timingPreparationThreads(bool overlap, TimingOverlapMode mode) noexcept {
    if (mode != TimingOverlapMode::APrep256 && mode != TimingOverlapMode::APrep64) return 0;
    return overlap && mode == TimingOverlapMode::APrep64 ? 64U : 256U;
}
inline const char *timingSchema(TimingOverlapMode mode) noexcept {
    return mode == TimingOverlapMode::APrep256 ? "pearl-whole-search-overlap-v1" :
           mode == TimingOverlapMode::APrep64 ? "pearl-whole-search-aprep64-v1" : nullptr;
}
inline bool deltaEquals(uint64_t before, uint64_t after, uint64_t expected) noexcept {
    return after >= before && after - before == expected;
}
inline bool validPathDelta(const om::PearlRetirementStats &before,
                          const om::PearlRetirementStats &after,
                          bool overlap, uint64_t attempts, uint64_t plan,
                          unsigned preparationThreads = 256,
                          unsigned batchQuantum = 4) noexcept {
    // Single-attempt batches are explicit and serial-only; overlap retains its
    // four-attempt slot balance and every existing caller retains that default.
    return (batchQuantum == 4 || (!overlap && batchQuantum == 1)) &&
        attempts > 0 && attempts % batchQuantum == 0 && plan != 0 &&
        (preparationThreads == 256 || (overlap && preparationThreads == 64)) &&
        before.preparationThreads == preparationThreads && after.preparationThreads == preparationThreads &&
        !before.snapshotCaptureActive && !after.snapshotCaptureActive &&
        before.preparationPlanId == plan && after.preparationPlanId == plan &&
        deltaEquals(before.serialRetirements, after.serialRetirements, overlap ? 0 : attempts) &&
        deltaEquals(before.overlapRetirements, after.overlapRetirements, overlap ? attempts : 0) &&
        deltaEquals(before.slotRetirements[0], after.slotRetirements[0], overlap ? attempts / 2 : attempts) &&
        deltaEquals(before.slotRetirements[1], after.slotRetirements[1], overlap ? attempts / 2 : 0) &&
        deltaEquals(before.snapshotCaptures, after.snapshotCaptures, 0);
}
} // namespace pearl_search_timing_test
