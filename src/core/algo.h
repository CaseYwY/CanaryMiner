// The interface every algorithm implements.
//
// Design notes, since the shape here is what decides how painful the second
// algorithm is to add:
//
//  * Algorithms own their own memory. Autolykos v2 wants a 7 GB dataset
//    rebuilt when the height changes; Ethash-family algorithms want a DAG per
//    epoch; a memoryless algorithm wants nothing at all. Rather than model
//    "dataset" in the core, the core just calls prepare() when the job's epoch
//    key changes and lets the algorithm decide what that means.
//
//  * Jobs carry a 32-byte message and a 256-bit target because that covers
//    every PoW worth supporting. Anything algorithm-specific (Ergo's `pk`,
//    a pool's job id) rides along in `extra` and is opaque to the core.
//
//  * search() is synchronous and batched. The core owns the nonce counter, the
//    stop signal and the reporting, so an algorithm is only ever responsible
//    for "test these nonces, tell me which ones win".

#pragma once

#include <stdint.h>
#include <signal.h>

#include <string>
#include <vector>

#include "benchmark_identity.h"
#include "pearl_native_selection.h"
#include "pearl_private_execution_types.h"

namespace om {
// Defined by the linked CUDA backend; entry-point flags are not capability.
bool pearlCudaAttemptOverlapCompiled() noexcept;
bool pearlCudaARootFusionCompiled() noexcept;
bool pearlCudaIntegratedPipelineCompiled() noexcept;

/// Defined in run.h, which includes this header - hence the declaration
/// rather than the include. Inside `om`, not at global scope: a global one is
/// a DIFFERENT type, and `using namespace om` then makes every mention
/// ambiguous.
struct RunOptions;
class PearlAlgorithmProgressSink;
class PearlProofBindingSink;

// Factory implementation capability, never inferred from a display label.
enum class PearlRuntimeCapability { Unsupported, CanonicalCudaPoolV1 };

/** A unit of work handed out by a node or pool. */
struct Job {
    uint8_t msg[32] = {};      ///< header digest to hash against
    uint64_t target[4] = {};   ///< 256-bit target, limb[0] least significant
    uint64_t epoch = 0;        ///< height/epoch; a change triggers prepare()
    std::string extra;         ///< algorithm-specific payload (e.g. Ergo pk)
    std::string jobId;         ///< pool's job id; travels with the job so a
                               ///< solution is submitted under the id it was
                               ///< computed against, not whatever arrived since
    bool valid = false;
    /// H-025 D6: the pool is a v2 session. The resolved bound (CPU view and GPU
    /// search) uses the compact-floored target; `target` stays raw for identity.
    bool compactFloorTarget = false;
};

/** A nonce whose hit fell below the job's target. */
struct Solution {
    uint64_t nonce = 0;
    uint64_t hit[4] = {};

    /**
     * Algorithm-specific payload for the submission, opaque to the core - the
     * mirror of Job::extra on the way back out.
     *
     * A nonce is enough to describe a win for every hash-and-compare PoW, and
     * is not enough for all of them. Pearl's win is a tile of a matrix product
     * the miner chose, and what gets submitted is a Merkle opening of sixteen
     * rows and sixteen columns - tens of kilobytes that no fixed field can
     * hold. Empty for algorithms whose nonce says everything.
     */
    std::string extra;

    /**
     * Pearl-only opaque ownership token for one serialized proof. Zero means
     * unbound and is the permanent value for non-Pearl algorithms. Allocation
     * and lifecycle are intentionally deferred to the Pearl coordinator.
     */
    uint64_t pearlProofToken = 0;
};

class Algorithm {
   public:
    virtual ~Algorithm() = default;

    /** Short lowercase identifier, e.g. "autolykos2". */
    virtual const char *name() const = 0;

    virtual PearlRuntimeCapability pearlRuntimeCapability() const noexcept {
        return PearlRuntimeCapability::Unsupported;
    }

    // Virtual capability avoids linking CUDA symbols from CPU/OpenCL run callers.
    virtual bool pearlIntegratedPipelineCompiled() const noexcept { return false; }

    // Worker-thread-only, invalid until successful prepare and after release
    // or selection invalidation. Actual compiled selection, never a tuning
    // request. Does NOT bind a Job, authorize a proof, or admit a public run.
    virtual PearlNativeSelection pearlNativeSelection() const noexcept {
        return {};
    }
    virtual PearlPrivateExecutionStats pearlPrivateExecutionStats() const noexcept { return {}; }

    // Pure admission before configure/telemetry/source setup. Backends without
    // an experiment reject every present selector, including an empty one.
    virtual bool pearlExperimentConfigurationValid(
        const RunOptions &, const char *selection) const noexcept {
        return selection == nullptr;
    }

    /** Optional signal flag for bounded, cancellation-aware startup preparation. */
    virtual void setPreparationStopFlag(volatile sig_atomic_t *stop) noexcept { (void)stop; }

    /** Device memory this algorithm will allocate for `job`, for reporting. */
    virtual size_t memoryBytes(const Job &job) const = 0;

    /**
     * Called once per epoch change (and once at startup). Build whatever
     * precomputed state the algorithm needs. Returns false on failure.
     */
    virtual bool prepare(const Job &job) = 0;

    // Speculative preparation only; never authorizes an additional attempt.
    // The run loop owns nonce scheduling. Defaults preserve every other backend.
    virtual void pearlNextAttemptHint(bool valid, uint64_t nonce) noexcept {
        (void)valid; (void)nonce;
    }
    virtual bool discardPearlLookahead() noexcept { return true; }

    /**
     * Test `count` work units starting at `nonceBase`. For ordinary PoWs one
     * unit is one nonce. Algorithms that test several candidates per nonce
     * expose that granularity through workUnitsPerNonce(). Appends any winners
     * to `out`. Returns false only on an unrecoverable device error.
     */
    virtual bool search(const Job &job, uint64_t nonceBase, uint64_t count,
                        std::vector<Solution> *out) = 0;

    /** Number of counted work units produced by one nonce/launch attempt. */
    virtual uint64_t workUnitsPerNonce() const { return 1; }

    /**
     * Monotonic count of fully synchronized search attempts, when exposed.
     * Used by device liveness gates to distinguish real completed work from a
     * backend that returns success without launching another grid.
     */
    virtual uint64_t completedSearchAttempts() const { return 0; }

    /**
     * Optional Pearl progress capability.  The sink is worker-thread-only;
     * it publishes coherent atomics for any independent observer.  Binding
     * happens before search and unbinding after the final search, never while
     * a callback is active. The pointer is borrowed: a future caller must use
     * a scoped binding whose destructor first clears it with nullptr, before
     * destroying the sink, on every normal and exceptional exit. Callbacks
     * are synchronous and noexcept. Non-Pearl algorithms deliberately do
     * nothing.
     *
     * Returning true is the capability test, so the run loop does not need a
     * name-based list of algorithms that happen to expose progress.
     */
    virtual bool setPearlProgressSink(PearlAlgorithmProgressSink *sink) {
        (void)sink;
        return false;
    }

    /**
     * Optional worker-only Pearl proof binding capability. Live canonical
     * Pearl runs must attach a run-owned sink before search and clear it before
     * its destruction. Standalone offline device fixtures may leave it null;
     * their resulting zero-token proofs are never submit-authorized.
     */
    virtual bool setPearlProofBindingSink(PearlProofBindingSink *sink) {
        (void)sink;
        return false;
    }

    /**
     * Host-side re-verification of a solution before it is submitted.
     * A GPU that is overclocked into instability produces wrong hits; this is
     * what stops those being sent upstream as invalid shares.
     */
    virtual bool verify(const Job &job, const Solution &sol) const = 0;

    /**
     * Opt in to precomputing the *next* epoch's state in the background, so a
     * new job does not stall mining while it is built.
     *
     * Mode: -1 decide automatically (build ahead only when the memory is
     * free), 0 never, 1 always. Default is a no-op, so a backend that has not
     * implemented it is unaffected.
     */
    /** Multiply-accumulates one counted work unit costs, or 0 when the unit
     * IS the hash and no conversion applies.
     *
     * Autolykos counts hashes and returns 0. Pearl counts candidates, and a
     * candidate is a 16x16 tile of a matrix product whose every element is a
     * length-k dot product, so it costs tileSize * k MACs. Reporting the rate
     * in TH/s means multiplying by exactly this and nothing else. */
    virtual double macsPerUnit() const { return 0; }

    // Exact host accounting capability after prepare. Zero means unsupported; never recover an integer from a display double.
    virtual uint64_t exactMacsPerUnit() const noexcept { return 0; }

    virtual void setPrefetch(int mode) { (void)mode; }

    /** True when the last prepare() was served from a prefetched buffer. */
    virtual bool servedFromPrefetch() const { return false; }

    /**
     * True when prepare(job) will be instant because the state is already
     * built. Only used to decide whether to warn the user about a wait.
     */
    virtual bool prefetchReadyFor(const Job &job) const { (void)job; return false; }

    /** One line on what prefetch is doing, or empty if it is not running. */
    virtual std::string prefetchNote() const { return std::string(); }

    /**
     * The launcher scripts that mine this algorithm, e.g. "mine_rvn_*", or
     * empty if it has none. Benchmark mode points the user at these.
     *
     * This lives here, and not as a name lookup in the run loop, because it
     * was a name lookup in the run loop and it went wrong twice: an if-chain
     * over algorithm names told BC3 users to go and mine Ergo, was fixed by
     * adding a branch, and then told KawPow users the same thing.
     *
     * The default is DELIBERATELY empty rather than a guess. An algorithm
     * that forgets to answer gets no script name, which reads as incomplete;
     * defaulting to some other coin's script reads as correct and sends the
     * user to mine the wrong chain.
     */
    /**
     * Hand the algorithm the run's options, once, before mining starts.
     *
     * Default does nothing, deliberately: an algorithm that needs no tuning
     * gets correct behaviour by existing rather than by someone remembering to
     * add it to a list. The alternative - a dispatch in the caller naming each
     * algorithm that has options - is the shape that silently stopped the
     * share counters twice on this project.
     *
     * RunOptions is forward-declared because run.h includes this header; an
     * implementation that needs the fields includes run.h itself.
     */
    virtual void configure(const RunOptions &) {}

    virtual std::string launcherGlob() const { return std::string(); }

    /**
     * Fill in whatever a synthetic benchmark job needs beyond the generic
     * fields the run loop sets (message, epoch, target).
     *
     * Default does nothing, which is right for every algorithm whose job fits
     * `Job`'s fixed fields. Pearl is the exception: its 76-byte header stub and
     * certificate version ride in `Job::extra`, so a bench job without them is
     * refused by `prepare()` and `--bench` fails.
     *
     * This lives on the interface rather than as a branch in the run loop for
     * the same reason `launcherGlob()` does: a new algorithm that needs bench
     * setup gets it by existing, not by someone remembering to edit run.cpp.
     */
    virtual void fillBenchJob(Job *job) const { (void)job; }

    /** Invalid unless a formal benchmark selection passed every final gate. */
    virtual BenchmarkIdentity benchmarkIdentity() const { return {}; }

    /** Release device memory. */
    virtual void release() = 0;
};

/** Returns the algorithm registered under `name`, or nullptr. */
Algorithm *createAlgorithm(const std::string &name);

/** Names of every compiled-in algorithm. */
std::vector<std::string> availableAlgorithms();

}  // namespace om
