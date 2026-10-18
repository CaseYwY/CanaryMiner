#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "../src/algos/pearl-pow/search_state.h"
#include "../src/algos/pearl-pow/cuda_preparation_plan.h"
#include "../src/core/run.h"

namespace {

int failures = 0;

void check(const char *name, bool condition) {
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", name);
    if (!condition) ++failures;
}

struct ProofRecord {
    uint64_t nonce = 0;
    uint32_t digestTag = 0;
    bool verified = false;
};

}  // namespace

int main() {
    using namespace om;
    using namespace om::pearl;

    std::puts("Pearl continuous-work schedule");
    // The E2E supersession phase uses the CLI minimum to require a fetch
    // between proofs, independently of the selected supported CUDA shape.
    for (const auto &shape : kCudaPreparationShapes) {
        const uint64_t tiles = uint64_t(shape.m / 16) * (shape.n / 16);
        const WorkBatch batch = planWorkBatch(256, UINT64_MAX, tiles);
        check("supersession fixture requests exactly one full CUDA grid",
              tiles >= 256 && batch.nonces == 1 && batch.workUnits == tiles);
    }
    {
        AttemptSchedule schedule(0x1000, 4);
        std::vector<uint64_t> got;
        uint64_t nonce = 0;
        while (schedule.next(&nonce)) got.push_back(nonce);
        check("one search call advances beyond its first grid",
              got == std::vector<uint64_t>({0x1000, 0x1001, 0x1002, 0x1003}));
        check("the schedule reports every attempt completed",
              schedule.complete() && schedule.completed() == 4);
        check("a completed schedule cannot replay its final nonce",
              !schedule.next(&nonce));

        AttemptSchedule edge(std::numeric_limits<uint64_t>::max(), 2);
        check("the last nonce is still valid", edge.next(&nonce) &&
                  nonce == std::numeric_limits<uint64_t>::max());
        check("a schedule refuses nonce wrap", !edge.next(&nonce));
    }

    // Model several unchanged-job run-loop iterations.  This is the exact
    // plan/schedule pairing used by run.cpp and PearlPow::search(): candidate
    // units determine batch size, while nonce attempts advance the cursor.
    {
        constexpr uint64_t tiles = 1024;
        constexpr uint64_t request = 3 * tiles + 17;
        uint64_t cursor = 0x9000;
        uint64_t work = 0;
        std::vector<uint64_t> attempts;
        for (int batchNo = 0; batchNo < 5; ++batchNo) {
            const WorkBatch batch =
                planWorkBatch(request, remainingNonceCount(~0ull, cursor), tiles);
            AttemptSchedule schedule(cursor, batch.nonces);
            uint64_t nonce = 0;
            while (schedule.next(&nonce)) attempts.push_back(nonce);
            check("each unchanged-job batch completes all planned grids",
                  schedule.complete() && schedule.completed() == batch.nonces);
            cursor += batch.nonces;
            work += batch.workUnits;
        }
        bool contiguous = attempts.size() == 15;
        for (size_t i = 0; i < attempts.size() && contiguous; ++i)
            contiguous = attempts[i] == 0x9000 + i;
        check("five calls continue through fifteen distinct attempts", contiguous);
        check("continued work accounting includes every grid", work == 15 * tiles);
    }

    std::puts("Pearl saturated-hit ownership");
    {
        const HitWindow exact = hitWindow(kMaxHits);
        const HitWindow overflow = hitWindow(kMaxHits + 19);
        check("an exactly full buffer is not reported as truncated",
              exact.retained == kMaxHits && !exact.saturated);
        check("overflow preserves the true observed count and caps reads",
              overflow.observed == kMaxHits + 19 &&
                  overflow.retained == kMaxHits && overflow.saturated);

        std::vector<uint32_t> backing(kMaxHits + 8, 0);
        for (uint32_t i = 0; i < kMaxHits; ++i) backing[i] = 500 + i;
        // A stale value beyond the retained prefix would incorrectly win if a
        // selector scanned allocated memory rather than this attempt's count.
        backing[kMaxHits] = 1;
        backing[17] = 7;
        uint32_t slot = 999;
        check("selection succeeds from a saturated retained prefix",
              selectLowestRetainedHit(backing.data(), kMaxHits, overflow, &slot));
        check("stale tail data cannot own the selected proof",
              slot == 17 && backing[slot] == 7);
        check("a short device read is rejected",
              !selectLowestRetainedHit(backing.data(), kMaxHits - 1, overflow,
                                       &slot));
        check("zero hits cannot fabricate a selected proof",
              !selectLowestRetainedHit(backing.data(), backing.size(),
                                       hitWindow(0), &slot));
    }

    std::puts("Pearl proof ledger lifetime");
    {
        ProofOwnershipLedger<ProofRecord> ledger;
        ledger.beginSearch();
        const uint64_t firstGeneration = ledger.generation();
        ledger.record({11, 111, true});
        ledger.record({22, 222, false});
        ledger.record({11, 333, true});
        const ProofRecord *latest = ledger.newest(
            11, [](const ProofRecord &r) { return r.nonce; });
        check("a repeated nonce is owned by its newest proof record",
              latest && latest->digestTag == 333 && latest->verified);
        const ProofRecord *invalid = ledger.newest(
            22, [](const ProofRecord &r) { return r.nonce; });
        check("an invalid proof remains explicitly invalid",
              invalid && !invalid->verified);

        ledger.beginSearch();
        check("starting a new search invalidates all old proof ownership",
              ledger.size() == 0 && ledger.generation() == firstGeneration + 1 &&
                  ledger.newest(11, [](const ProofRecord &r) { return r.nonce; }) ==
                      nullptr);
        ledger.record({11, 444, false});
        latest = ledger.newest(11, [](const ProofRecord &r) { return r.nonce; });
        check("a reused nonce cannot fall back to a prior valid proof",
              latest && latest->digestTag == 444 && !latest->verified);
    }

    if (failures) {
        std::printf("FAIL: %d Pearl search-state checks failed\n", failures);
        return 1;
    }
    std::puts("PASS: Pearl work continues and proof ownership is bounded");
    return 0;
}
