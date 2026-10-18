#pragma once
#ifndef CANARY_PEARL_TEST_TRANSCRIPT_SNAPSHOT
#error "This seam is only available in the dedicated transcript test build"
#endif

#include <array>
#include <vector>
#include "../src/core/algo.h"
#include "../src/core/pearl_speculative_attempt.h"
#include "../src/algos/pearl-pow/integrated_pipeline.h"
#include "../src/algos/pearl-pow/job_publication.h"

namespace om {
// Lifetime-cumulative test evidence, not a work/proof ledger. Query only while
// the Algorithm is idle; concurrent access to one backend is unsupported.
struct PearlRetirementStats {
    uint64_t serialRetirements = 0, overlapRetirements = 0;
    uint64_t slotRetirements[2] = {};
    uint64_t preparationPlanId = 0; // last actually retired attempt, initially 0
    unsigned preparationThreads = 0; // actual last A preparation CTA cap
    uint64_t aRootFusionRetirements = 0, aRootSeparateRetirements = 0;
    bool aRootFusion = false; // last actual terminal path, not configured mode
    uint64_t snapshotCaptures = 0; // successful batch/single snapshots, not shares
    bool snapshotCaptureActive = false;
};
// Owned serial reference from an idle, ordinary-prepared APrepV1 backend.
// No caller tuple or tuning override is accepted. Call before its first search,
// with the overlap environment absent for reference construction. No concurrent
// backend/environment access. Caller owns returned pointer; null is failure.
// Test-only: the returned selection is a replay, never a serial tuner winner.
Algorithm *pearlTestCreateSerialReplay(Algorithm *ordinaryOverlap,const Job &job) noexcept;

// Returns false (and clears output) for the wrong backend or counter overflow.
// Does not arm capture, synchronize CUDA, log, or read device memory.
bool pearlTestRetirementStats(Algorithm *algorithm, PearlRetirementStats *out) noexcept;
// CPU-owned counters only. Query after an explicit discard/drain for final
// endpoint accounting; the getter itself never waits or promotes an attempt.
bool pearlTestIntegratedPipelineStats(Algorithm *algorithm,
                    PearlSpeculativeAttemptCounters *out) noexcept;
bool pearlTestIntegratedPipelineTimings(Algorithm *algorithm,
                    pearl::IntegratedPipelineHostTimings *out) noexcept;


struct PearlTranscriptSnapshot {
    bool valid = false;
    bool overlapRetirement = false;
    unsigned aSlot = 0;
    unsigned preparationThreads = 256;
    bool aRootFusion = false;
    uint64_t nonce = 0, completedBefore = 0, completedAfter = 0;
    pearl::PreparedJobHost prepared;
    std::array<uint8_t, 64> roots{};
    std::array<uint8_t, 32> commitA{}, commitB{};
    std::vector<int8_t> rawA, rawBt;
    std::vector<uint32_t> transcripts;
};
// Performs one ordinary search and synchronously captures its still-owned
// buffers. Not present in production; never opens a network connection.
bool pearlTestSearchAndCapture(Algorithm *algorithm, const Job &job,
                              uint64_t nonce,
                              PearlTranscriptSnapshot *out) noexcept;
// One real multi-attempt search. Captures each owned slot after compute/scan
// completion and before any reuse; permits zero-hit as well as all-hit jobs.
// With valid nonnull output pointers and no reentry, failure discards all
// captures/proofs and invalidates backend proof ownership. Null/reentrant
// calls are rejected without mutating the ongoing operation.
bool pearlTestSearchBatchAndCapture(Algorithm *algorithm, const Job &job,
                    uint64_t nonce, uint64_t attempts,
                    std::vector<PearlTranscriptSnapshot> *captures,
                    std::vector<Solution> *solutions) noexcept;
}
