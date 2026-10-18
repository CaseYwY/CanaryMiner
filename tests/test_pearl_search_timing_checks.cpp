#include "pearl_search_timing_checks.h"
#include "pearl_energy_window.h"
#include <cassert>

int main() {
    using namespace pearl_search_timing_test;
    constexpr uint64_t plan = UINT64_C(0x4355444100010028);
    om::PearlRetirementStats before;
    before.preparationPlanId = plan;
    before.preparationThreads = 256;
    before.snapshotCaptures = 4;
    before.serialRetirements = 4;
    before.slotRetirements[0] = 4;
    for (bool overlap : {false, true}) {
        auto after = before;
        (overlap ? after.overlapRetirements : after.serialRetirements) += 8;
        after.slotRetirements[0] += overlap ? 4 : 8;
        after.slotRetirements[1] += overlap ? 4 : 0;
        assert(validPathDelta(before, after, overlap, 8, plan));
        assert(!validPathDelta(before, after, !overlap, 8, plan));
        for (unsigned mutation = 0; mutation < 9; ++mutation) {
            auto bad = after;
            switch (mutation) {
                case 0: ++bad.serialRetirements; break;
                case 1: ++bad.overlapRetirements; break;
                case 2: ++bad.slotRetirements[0]; break;
                case 3: ++bad.slotRetirements[1]; break;
                case 4: ++bad.snapshotCaptures; break;
                case 5: bad.snapshotCaptureActive = true; break;
                case 6: --bad.preparationPlanId; break;
                case 7: bad.snapshotCaptures = 0; break;
                case 8: bad.preparationThreads = 64; break;
            }
            assert(!validPathDelta(before, bad, overlap, 8, plan));
        }
        assert(!validPathDelta(before, after, overlap, 0, plan));
        assert(!validPathDelta(before, after, overlap, 7, plan));
        assert(!validPathDelta(before, after, overlap, 8, 0));
        auto badBefore = before;
        badBefore.snapshotCaptureActive = true;
        assert(!validPathDelta(badBefore, after, overlap, 8, plan));
        badBefore = before; badBefore.preparationPlanId = 0;
        assert(!validPathDelta(badBefore, after, overlap, 8, plan));
        badBefore = before; badBefore.preparationThreads = 0;
        assert(!validPathDelta(badBefore, after, overlap, 8, plan));
        auto before64 = before, after64 = after;
        before64.preparationThreads = after64.preparationThreads = 64;
        assert(validPathDelta(before64, after64, overlap, 8, plan, 64) == overlap);
        assert(!validPathDelta(before64, after64, overlap, 8, plan, 256));
        assert(!validPathDelta(before, after64, overlap, 8, plan, 64));
        assert(!validPathDelta(before64, after, overlap, 8, plan, 64));
        assert(!validPathDelta(before, after, overlap, 8, plan, 128));
    }
    // Warmup/window totals need not be multiples of four for the explicitly
    // selected serial one-attempt path. Exercise actual counters, not just CLI.
    for (uint64_t attempts : {UINT64_C(1), UINT64_C(2), UINT64_C(3), UINT64_C(4),
                              UINT64_C(5), UINT64_C(41), UINT64_C(127)}) {
        auto after = before;
        after.serialRetirements += attempts;
        after.slotRetirements[0] += attempts;
        assert(validPathDelta(before, after, false, attempts, plan, 256, 1));
        assert(validPathDelta(before, after, false, attempts, plan) == (attempts % 4 == 0));
        assert(validPathDelta(before, after, false, attempts, plan, 256, 4) == (attempts % 4 == 0));
        assert(!validPathDelta(before, after, false, 0, plan, 256, 1));
        assert(!validPathDelta(before, after, false, attempts + 1, plan, 256, 1));
        assert(!validPathDelta(before, after, false, attempts, 0, 256, 1));
        for (unsigned badQuantum : {0U, 2U, 3U, 5U, ~0U})
            assert(!validPathDelta(before, after, false, attempts, plan, 256, badQuantum));
        for (unsigned mutation = 0; mutation < 9; ++mutation) {
            auto bad = after;
            switch (mutation) {
                case 0: ++bad.serialRetirements; break;
                case 1: ++bad.overlapRetirements; break;
                case 2: ++bad.slotRetirements[0]; break;
                case 3: ++bad.slotRetirements[1]; break;
                case 4: ++bad.snapshotCaptures; break;
                case 5: bad.snapshotCaptureActive = true; break;
                case 6: --bad.preparationPlanId; break;
                case 7: bad.serialRetirements = 0; break;
                case 8: bad.preparationThreads = 64; break;
            }
            assert(!validPathDelta(before, bad, false, attempts, plan, 256, 1));
        }
        auto badBefore = before;
        badBefore.snapshotCaptureActive = true;
        assert(!validPathDelta(badBefore, after, false, attempts, plan, 256, 1));
        badBefore = before; badBefore.preparationPlanId = 0;
        assert(!validPathDelta(badBefore, after, false, attempts, plan, 256, 1));
        badBefore = before; badBefore.preparationThreads = 0;
        assert(!validPathDelta(badBefore, after, false, attempts, plan, 256, 1));
        // Even a synthetically floor-balanced overlap must not gain quantum1
        // admission (in particular attempts1 with zero slot retirements).
        auto overlapped = before;
        overlapped.overlapRetirements += attempts;
        overlapped.slotRetirements[0] += attempts / 2;
        overlapped.slotRetirements[1] += attempts / 2;
        assert(!validPathDelta(before, overlapped, true, attempts, plan, 256, 1));
        assert(validPathDelta(before, overlapped, true, attempts, plan) == (attempts % 4 == 0));
    }
    assert(parseTimingOverlapMode(nullptr) == TimingOverlapMode::APrep256);
    assert(parseTimingOverlapMode("a-prep-v1") == TimingOverlapMode::APrep256);
    assert(parseTimingOverlapMode("a-prep64-v1") == TimingOverlapMode::APrep64);
    for (const char *bad : {"", "a-prep-v1 ", "a-prep64-v1\n", "a-prep64", "A-prep64-v1", "1"})
        assert(parseTimingOverlapMode(bad) == TimingOverlapMode::Invalid);
    assert(timingPreparationThreads(false, TimingOverlapMode::APrep64) == 256);
    assert(timingPreparationThreads(true, TimingOverlapMode::APrep64) == 64);
    assert(timingPreparationThreads(true, TimingOverlapMode::APrep256) == 256);
    assert(timingPreparationThreads(false, TimingOverlapMode::Invalid) == 0);
    assert(timingOverlapName(TimingOverlapMode::Invalid) == nullptr);
    assert(timingSchema(TimingOverlapMode::Invalid) == nullptr);
    assert(std::string_view(timingSchema(TimingOverlapMode::APrep64)) != timingSchema(TimingOverlapMode::APrep256));
    assert(!deltaEquals(UINT64_MAX, 0, 1));
    assert(deltaEquals(UINT64_MAX - 4, UINT64_MAX, 4));
    assert(pearl_energy_test::planAt(0, 0) == 0 && pearl_energy_test::planAt(0, 1) == 1 &&
           pearl_energy_test::planAt(0, 2) == 1 && pearl_energy_test::planAt(0, 3) == 0);
    assert(pearl_energy_test::planAt(1, 0) == 1 && pearl_energy_test::planAt(1, 1) == 0 &&
           pearl_energy_test::planAt(1, 2) == 0 && pearl_energy_test::planAt(1, 3) == 1);
}
