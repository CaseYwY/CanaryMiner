// The algorithm-agnostic run loop, shared by every backend binary.
//
// Both the CUDA build and the OpenCL build link this; the only difference
// between them is which Algorithm gets constructed and handed over.

#pragma once

#include <signal.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "algo.h"
#include "pearl_private_execution.h"
#include "pearl_authorization.h"
#include "pearl_work_range.h"
#include "pearl_public_execution.h"
#include "pearl_shape_experiment.h"
#include "http.h"
#include "platform.h"
#include "pearl_job_context.h"
#include "telemetry.h"

namespace om {

class PearlSourceObservability;

/** Read-only view of the run loop for the product layer (stats endpoint). Called on the run-loop thread, once per report
 *  interval, with the numbers the readout prints. Must not block. */
class RunObserver {
   public:
    virtual ~RunObserver() = default;
    virtual void onReport(const MinerStats &stats, const GpuTelemetry &gpu) = 0;
};
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
namespace guard_retirement_v2 { class Session; }
#endif

/** Product-layer hook called at the top of every run-loop iteration (never mid-search), after the previous iteration's
 *  proofs were reconciled. `searchCompleted`: the iteration since the previous call finished a search on a valid job.
 *  Return 0 to continue, or a run exit code (kRunFeeWindow / kRunFeeWindowDone) to end this runMiner call cleanly. */
class RunLoopHook {
   public:
    virtual ~RunLoopHook() = default;
    virtual int atLoopTop(bool searchCompleted) = 0;
};

inline uint64_t saturatingCounterAdd(uint64_t a, uint64_t b) {
    return b > UINT64_MAX - a ? UINT64_MAX : a + b;
}

inline bool saturatingCounterIncrement(uint64_t *value) {
    if (!value || *value == UINT64_MAX) return false;
    ++*value;
    return true;
}

/**
 * What a remote work source can prove about submitted shares.
 *
 * `acknowledged` is deliberately independent of `accepted`: it means the
 * remote endpoint correlated and answered the request, not that consensus or
 * a pool credited the proof.  `unknown` is terminal accounting (the source no
 * longer expects a better answer), while `pending` is still awaiting one.
 */
struct PoolOutcomeSnapshot {
    uint64_t submitted = 0;
    uint64_t acknowledged = 0;
    uint64_t accepted = 0;
    uint64_t rejected = 0;
    uint64_t stale = 0;
    uint64_t unknown = 0;
    uint64_t pending = 0;
    uint64_t guardRetired = 0; // confirmed non-forwarding, never pool credit
    uint64_t acceptedSuperseded = 0;  ///< diagnostic subset of accepted

    uint64_t classified() const {
        return saturatingCounterAdd(
            saturatingCounterAdd(saturatingCounterAdd(accepted, rejected),
                                 stale),
            saturatingCounterAdd(unknown, guardRetired));
    }

    bool conserved() const {
        uint64_t remaining = submitted;
        if (accepted > remaining) return false;
        remaining -= accepted;
        if (rejected > remaining) return false;
        remaining -= rejected;
        if (stale > remaining) return false;
        remaining -= stale;
        if (unknown > remaining) return false;
        remaining -= unknown;
        if (guardRetired > remaining || acknowledged > submitted-guardRetired) return false;
        remaining -= guardRetired;
        uint64_t unaccountedAcknowledgements = acknowledged;
        if (accepted > unaccountedAcknowledgements) return false;
        unaccountedAcknowledgements -= accepted;
        if (rejected > unaccountedAcknowledgements) return false;
        unaccountedAcknowledgements -= rejected;
        if (stale > unaccountedAcknowledgements) return false;
        return pending == remaining && acknowledged <= submitted &&
               acceptedSuperseded <= accepted;
    }
};

enum class OutcomeSnapshotStatus {
    Unsupported,
    Valid,
    Invalid,
};

/**
 * Where work comes from and where solutions go.
 *
 * This is the seam for pool support: a stratum client is another
 * implementation of this interface, and nothing else has to change.
 */
class JobSource {
   public:
    virtual ~JobSource() = default;
    virtual const char *describe() const = 0;
    virtual bool fetch(Job *job) = 0;
    virtual bool submit(const Job &job, const Solution &sol, std::string *err) = 0;

    /**
     * Share counts as confirmed by the far end, for sources that answer.
     * Returning false means "this source cannot tell you" - which is the
     * honest answer for solo, where the node's HTTP reply is synchronous.
     */
    /**
     * A one-off human-readable note about the last submission, or the last
     * unusable job, popped so it is logged exactly once.
     *
     * These are on the INTERFACE rather than on each stratum class, and that
     * is the point. The run loop used to reach them through a chain of
     * dynamic_casts naming every source type it knew about, so adding a source
     * and forgetting the chain left its counters reading zero for ever while
     * the pool happily credited shares. That has now happened twice - once for
     * BC3, once for KawPow - and a cast chain is a hand-maintained list of
     * types, which is the same failure as a hand-maintained list of files.
     *
     * The defaults are empty, so a source that has nothing to say says nothing
     * and needs no code at all.
     */
    /**
     * The reason the pool REJECTED the last share, or empty. Rejection-only:
     * the run loop prints whatever comes back as a rejection, so anything put
     * here is a rejection by definition.
     *
     * It used to be called takeSubmitVerdict(), and "verdict" reads as though
     * it carries either outcome. The KawPow client duly put "share accepted"
     * in it, and the miner reported every accepted share as REJECTED - on a
     * live pool, with the counters simultaneously reading 3 accepted and 0
     * rejected. Acceptance is already reported by poolCounters(); this channel
     * carries bad news only.
     */
    virtual std::string takeRejectReason() { return std::string(); }
    virtual std::string takeJobWarning() { return std::string(); }

    /** Optional fixed-schema Pearl source event capability. */
    virtual PearlSourceObservability *pearlSourceObservability() {
        return nullptr;
    }

    virtual bool poolCounters(uint64_t *accepted, uint64_t *rejected,
                              uint64_t *pending, std::string *lastError) const {
        (void)accepted; (void)rejected; (void)pending; (void)lastError;
        return false;
    }

    /**
     * Rich submission accounting.  The default adapts the legacy three
     * counters, so existing JobSource implementations and consumers remain
     * source-compatible.  A source with stale/unknown/acknowledgement
     * semantics overrides this method and preserves those distinctions.
     */
    virtual OutcomeSnapshotStatus outcomeSnapshot(
        PoolOutcomeSnapshot *out, std::string *lastError) const {
        if (!out || !lastError) return OutcomeSnapshotStatus::Invalid;
        uint64_t accepted = 0, rejected = 0, pending = 0;
        if (!poolCounters(&accepted, &rejected, &pending, lastError))
            return OutcomeSnapshotStatus::Unsupported;
        PoolOutcomeSnapshot adapted;
        const bool firstOverflow = accepted > UINT64_MAX - rejected;
        if (firstOverflow) return OutcomeSnapshotStatus::Invalid;
        const uint64_t terminal = accepted + rejected;
        if (terminal > UINT64_MAX - pending)
            return OutcomeSnapshotStatus::Invalid;
        adapted.submitted = terminal + pending;
        adapted.acknowledged = terminal;
        adapted.accepted = accepted;
        adapted.rejected = rejected;
        adapted.pending = pending;
        if (!adapted.conserved()) return OutcomeSnapshotStatus::Invalid;
        *out = adapted;
        return OutcomeSnapshotStatus::Valid;
    }
};

/**
 * Interpret a successful submit return for sources with no outcome channel.
 *
 * This narrow fallback exists for synchronous non-pool sources such as the
 * Ergo node HTTP path.  A source that exposes any remote outcome snapshot,
 * even one with an invariant violation, must never reach this acceptance
 * fallback.
 */
inline bool successfulSubmitIsTerminalAcceptance(
    const JobSource *source, PoolOutcomeSnapshot *out,
    std::string *lastError) {
    return source && out && lastError &&
           source->outcomeSnapshot(out, lastError) ==
               OutcomeSnapshotStatus::Unsupported;
}

inline void recordSynchronousAcceptance(MinerStats *stats) {
    if (!stats) return;
    saturatingCounterIncrement(&stats->submitted);
    saturatingCounterIncrement(&stats->acknowledged);
    saturatingCounterIncrement(&stats->accepted);
}

struct RunOptions;
/** True when the pool login is ACCOUNT.WORKER (F2Pool account via the v2 dialect): the wallet field is not used, so it is not
 *  required to be a prl1 address (H-031). Defined after RunOptions. */
inline bool pearlLoginByAccount(const RunOptions &opt);

struct RunOptions {
    RunObserver *observer = nullptr;  // optional; never owned
    RunLoopHook *loopHook = nullptr;  // optional; never owned. Pool runs only; used by the fee scheduler.
    uint64_t batch = 1ULL << 22;
    // Default-off completion qualification. Captured from the exact environment
    // opt-in at CLI admission; never inferred from a reply or periodic report.
    unsigned pearlAcceptedQuota = 0;
    bool pearlSoloAuthorization = false;
    PearlWorkRange pearlWorkRange;
    PearlShapeExperiment pearlShapeExperiment = PearlShapeExperiment::Disabled;
    PearlPublicExecution pearlPublicExecution = PearlPublicExecution::Disabled;
    PearlPrivateExecution pearlPrivateExecution = PearlPrivateExecution::Disabled;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    // Borrowed trusted supervisor channel; no CLI/environment inference.
    guard_retirement_v2::Session *pearlGuardRetirementSession = nullptr;
#endif
    unsigned pearlSustainedSeconds = 0;
    bool pearlSustainedPublic = false;
    bool pearlProduction = false; // Explicit owned continuous mode; never selected by defaults.
    bool bench = false;
    bool autotune = false; // Product runs use the qualified fixed selection unless explicitly opted in.
    bool productFixedSelection = false;
    bool plain = false;
    uint64_t benchEpoch = 1851444;
    FormalBenchmarkRequest formalBenchmark;
    int reportSeconds = 5;
    std::string backendLabel;

    // Memory clock offset in MHz of transfer rate. CUDA and Vulkan both force
    // performance state P2, which runs memory below its rated speed, and this
    // is what puts it back. 0 leaves the card alone.
    int memOffsetMhz = 0;

    // Opt-in conservative memory overclock chosen by GPU generation. On AMD it
    // raises the memory clock a validated-safe amount over stock via the amdgpu
    // overdrive sysfs table (needs root and amdgpu.ppfeaturemask enabled); on
    // NVIDIA it points at --mclk-offset instead. Off by default - an auto OC on
    // an unknown card is how you crash someone's rig.
    bool memOc = false;

    // Benchmark only: pretend a new block arrives this often, so the cost of
    // an epoch change is included in the average. 0 means the height is fixed.
    int benchEpochSeconds = 0;

    // Pool mode. When poolHost is set, a StratumSource is used instead of the
    // node source.
    std::string poolHost;
    int poolPort = 0;
    std::string wallet;
    std::string worker = "canary";
    bool poolProofCompression = false;  // H-019: offer the gzip v2 dialect at login (product pool.compression auto)
    std::string poolAccount;            // H-019: v2 login account; empty uses the payout address
    std::string password = "x";

    // Test-only, credential-redacted Pearl pool protocol evidence; never logs authorize.
    std::string pearlTranscript;
    // Explicit policy, copied immutably by the canonical source/algorithm.
    // Zero-valued default is deliberately inadmissible for live Pearl work.
    PearlSourcePolicy pearlPolicy;
    bool remoteOptionsSpecified = false;

};

#if defined(CANARY_PEARL_TEST_RUN_QUOTA)
#if CANARY_PEARL_TEST_RUN_QUOTA != 1
#error "CANARY_PEARL_TEST_RUN_QUOTA must be exactly 1"
#endif
class PearlPoolSource;
// CPU fixture only; absent from ordinary production builds.
PearlPoolSource *pearlTestQuotaSource(const RunOptions &, bool nativeEvidence);
#endif

inline bool parsePearlNetworkOption(const std::string &text,
                                   PearlNetwork *out) noexcept {
    if (!out) return false;
    PearlNetwork value{};
    if (!pearlParseNetwork(text, 0, text.size(), &value) ||
        value == PearlNetwork::SyntheticBenchmark) return false;
    *out = value;
    return true;
}

inline bool parsePearlTargetSemanticsOption(
    const std::string &text, PearlTargetSemantics *out) noexcept {
    return out && pearlParseTargetSemantics(text, 0, text.size(), out);
}

/** Pure admission check: nullptr means allowed; no setup is performed here. */
inline const char *pearlRuntimeAdmissionProblem(
    const RunOptions &opt, const char *algorithmName,
    PearlRuntimeCapability capability, bool allHitRequested = false,
    bool hardTargetRequested = false) noexcept {
    if (!algorithmName) return "algorithm is missing";
    const bool pearl = std::strcmp(algorithmName, "pearl-pow") == 0;
    const bool policyPresent = static_cast<unsigned>(opt.pearlPolicy.network) != 0 ||
        static_cast<unsigned>(opt.pearlPolicy.targetSemantics) != 0;
    bool authorizationResearch = opt.pearlAcceptedQuota != 0 || opt.pearlSustainedSeconds != 0 || opt.pearlProduction ||
        pearlPublicEnabled(opt.pearlPublicExecution) || pearlPrivateEnabled(opt.pearlPrivateExecution);
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    authorizationResearch = authorizationResearch || opt.pearlGuardRetirementSession != nullptr;
#endif
    if (!pearlSoloAuthorizationAllowed(opt.pearlSoloAuthorization, algorithmName,
            opt.bench, !opt.poolHost.empty(), authorizationResearch))
        return "SOLO authorization requires a normal Pearl pool run without research guards";
    if (!pearl) {
        if (policyPresent || !opt.pearlTranscript.empty())
            return "Pearl options require --algo pearl-pow";
        return nullptr;
    }
    if (capability != PearlRuntimeCapability::CanonicalCudaPoolV1)
        return "canonical Pearl requires the CUDA backend";
    if (allHitRequested)
        return "canonical Pearl rejects CANARY_PEARL_BENCH_ALL_HIT";
    if (hardTargetRequested && !opt.bench)
        return "canonical Pearl live mining rejects CANARY_PEARL_HARD_TARGET";
    if (opt.bench) {
        if (opt.remoteOptionsSpecified || !opt.poolHost.empty() || opt.poolPort ||
            !opt.wallet.empty() || opt.worker != "canary" || opt.password != "x" ||
            !opt.pearlTranscript.empty() || policyPresent)
            return "Pearl synthetic benchmark rejects live source/policy options";
        if (opt.benchEpoch != kPearlBenchmarkEpoch || opt.benchEpochSeconds != 0)
            return "Pearl synthetic benchmark requires its fixed fixture height";
        return nullptr;
    }
    if (opt.poolHost.empty() || opt.poolHost[0] == '-' || opt.poolPort <= 0 ||
        opt.poolPort > 65535)
        return "canonical Pearl requires an explicit --pool HOST:PORT";
    if ((!pearlPublicNetwork(opt.pearlPolicy.network) &&
         opt.pearlPolicy.network != PearlNetwork::Regtest) ||
        !pearlKnownTargetSemantics(opt.pearlPolicy.targetSemantics))
        return "Pearl pool requires explicit --pearl-network and --pearl-target-semantics";
    if (opt.pearlPolicy.network != PearlNetwork::Mainnet)
        return "canonical Pearl live runtime supports mainnet only; non-mainnet networks are unsupported in this milestone";
    if (opt.wallet.empty() && !pearlLoginByAccount(opt)) return "Pearl pool requires --wallet";
    return nullptr;
}

/** Build/validate a complete Pearl fixture without repairing invalid output. */
inline bool initializePearlBenchmarkJob(Algorithm *algo, Job *out) noexcept {
    if (!out) return false;
    *out = Job{};
    if (!algo || !algo->name() || std::strcmp(algo->name(), "pearl-pow") ||
        algo->pearlRuntimeCapability() != PearlRuntimeCapability::CanonicalCudaPoolV1)
        return false;
    try {
        Job next;
        algo->fillBenchJob(&next);
        PearlJobContext context;
        if (!validatePearlJobContext(next, nullptr, &context, nullptr) ||
            context.network != PearlNetwork::SyntheticBenchmark ||
            next.epoch != kPearlBenchmarkEpoch || !pearlTargetIsZero(next.target))
            return false;
        *out = std::move(next);
        return true;
    } catch (...) { return false; }
}

/**
 * What the last prepare() was for, and whether a new job needs another one.
 *
 * Kept here rather than inside the run loop so the decision is testable
 * without a GPU. It is the whole of the fix for the Pearl pool rejections:
 * gating on the epoch alone left the miner mining the first job's matrices
 * for the life of the connection.
 */
struct PreparedJob {
    bool valid = false;
    uint64_t epoch = ~0ULL;
    uint8_t msg[32] = {};
    uint64_t target[4] = {};
    std::string extra;
    // Transport ownership is part of the prepare decision even though it is
    // deliberately not part of a backend's computational device-cache key.
    // A solution prepared under one pool job id must never be relabelled with
    // a later id whose computational inputs happen to be identical.
    std::string jobId;

    /** True when `j` is the job this prepare() was for, field by field. */
    bool matches(const Job &j) const {
        return valid && j.valid && epoch == j.epoch &&
               memcmp(msg, j.msg, sizeof(msg)) == 0 &&
               memcmp(target, j.target, sizeof(target)) == 0 &&
               extra == j.extra && jobId == j.jobId;
    }

    bool take(const Job &j) {
        if (!j.valid) return false;

        // Copy every potentially-throwing field before publishing any part of
        // the new snapshot. If allocation fails, the previous ownership tuple
        // remains intact rather than becoming a mixed old/new job.
        std::string nextExtra(j.extra);
        std::string nextJobId(j.jobId);

        epoch = j.epoch;
        memcpy(msg, j.msg, sizeof(msg));
        memcpy(target, j.target, sizeof(target));
        extra.swap(nextExtra);
        jobId.swap(nextJobId);
        valid = true;
        return true;
    }
};

/** Exact source-job identity used by the live run loop's adoption gate. */
inline bool sameRunLoopJobIdentity(const Job &a, const Job &b) {
    return a.valid && b.valid && a.epoch == b.epoch &&
           memcmp(a.msg, b.msg, sizeof(a.msg)) == 0 &&
           memcmp(a.target, b.target, sizeof(a.target)) == 0 &&
           a.extra == b.extra && a.jobId == b.jobId;
}

/**
 * Does this job need prepare() run for it?
 *
 * Autolykos keeps an epoch-only gate: re-preparing rebuilds a 7.27 GB dataset,
 * and its epoch genuinely tracks the work. Pearl re-prepares on any material
 * change, because a pool changes the header without ever moving the epoch -
 * `PearlPoolSource` sets `epoch = 0` on every job it builds. PearlPow::prepare()
 * caches on header, target and certificate version, so a job that has not
 * really changed costs nothing.
 */
inline bool shouldPrepare(bool isPearl, const PreparedJob &prepared,
                          const Job &job) {
    const bool epochChanged = !prepared.valid || job.epoch != prepared.epoch;
    return isPearl ? !prepared.matches(job) : epochChanged;
}

/**
 * Turn a requested work-unit budget into whole nonce attempts.
 *
 * Most algorithms have one work unit per nonce. Pearl produces an entire tile
 * grid from one nonce, so a request that is not a multiple of that grid cannot
 * be executed literally. Use as many complete attempts as fit, except that a
 * positive sub-grid request still performs one attempt. Returning both values
 * keeps the hashrate counter and nonce cursor tied to what search() really did.
 */
struct WorkBatch {
    uint64_t workUnits = 0;
    uint64_t nonces = 0;
};

/** Pure value-level guard for programmatic formal benchmark callers. */
inline bool canonicalFormalBenchmarkRunOptions(const RunOptions &opt,
                                               const char *algorithmName) {
    return validFormalBenchmarkRequest(opt.formalBenchmark) && algorithmName &&
           std::strcmp(algorithmName, "pearl-pow") == 0 && opt.bench &&
           opt.plain && opt.backendLabel == "CUDA" &&
           opt.batch == kPearlFormalBenchmarkBatch &&
           opt.reportSeconds ==
               static_cast<int>(kPearlFormalBenchmarkReportSeconds) &&
           opt.benchEpoch == kPearlBenchmarkEpoch &&
           opt.benchEpochSeconds == 0 && opt.memOffsetMhz == 0 &&
           !opt.memOc;
}

/**
 * Count the nonce values from `counter` through `mask`, inclusively.
 *
 * The full 64-bit range beginning at zero contains 2^64 values, which cannot
 * be represented by uint64_t. Saturating to UINT64_MAX is exact for planning:
 * a uint64_t work request can never require more than UINT64_MAX attempts.
 */
inline uint64_t remainingNonceCount(uint64_t mask, uint64_t counter) {
    if (counter > mask) return 0;
    const uint64_t distance = mask - counter;
    return distance == UINT64_MAX ? UINT64_MAX : distance + 1;
}

inline WorkBatch planWorkBatch(uint64_t requestedUnits,
                               uint64_t remainingNonces,
                               uint64_t unitsPerNonce) {
    if (!requestedUnits || !remainingNonces || !unitsPerNonce) return {};
    uint64_t nonces = requestedUnits / unitsPerNonce;
    if (!nonces) nonces = 1;
    if (nonces > remainingNonces) nonces = remainingNonces;
    return {nonces * unitsPerNonce, nonces};
}

/** Drives an already-constructed algorithm until *stop becomes non-zero. */
/** runMiner returns this when a pool session stopped on a non-transport fault with the source still
 * integrity-clean (no quarantine, no disconnect). The product layer may restart the GPU under its
 * guard-restart budget (H-019 D3, live v28u). Every other failure keeps returning 1. */
constexpr int kRunGuardStop = 8;
/** The loop hook asked for a fee window: the user run ended at a loop top with its shares settled (fee wiring, H-022). */
constexpr int kRunFeeWindow = 9;
/** The fee run's hook ended its window at a loop top. */
constexpr int kRunFeeWindowDone = 10;
int runMiner(Algorithm *algo, const RunOptions &opt, const char *gpuName,
             double gpuMemGB, const char *archLabel, volatile sig_atomic_t *stop,
             const char *gpuUuid = nullptr, unsigned telemetryDeviceIndex = 0);

inline bool pearlLoginByAccount(const RunOptions &opt) { return !opt.poolAccount.empty() && opt.poolProofCompression; }

}  // namespace om
