// Thread-safe publication of Pearl progress for an independent observer.
//
// This is a pure host bridge: no CUDA, sockets, source objects, clocks, or
// watchdog policy. One worker owns and mutates a PearlProgressSnapshot, then
// publishes it here. Observers read only the atomic mirror.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "pearl_liveness.h"

namespace om {

enum class PearlProgressPhase : uint64_t {
    Idle = 0,
    Connecting = 1,
    AwaitingAuthorization = 2,
    AwaitingJob = 3,
    Preparing = 4,
    Searching = 5,
    Finalizing = 6,
    AwaitingSubmit = 7,
    Submitting = 8,
    AwaitingAcknowledgement = 9,
    AwaitingOutcome = 10,
    Stopping = 11,
    Stopped = 12,
    Failed = 13,
};

// Stable diagnostic codes. These are observations, not automatic restart
// commands; enforcement remains outside this bridge.
enum class PearlProgressReason : uint64_t {
    None = 0,
    SessionSuperseded = 1,
    InvalidJob = 2,
    PrepareFailed = 3,
    SearchFailed = 4,
    AttemptCountMismatch = 5,
    HitBufferSaturated = 6,
    CandidateOwnershipLost = 7,
    FinalizerFailed = 8,
    HostVerificationFailed = 9,
    ProofSerializationFailed = 10,
    SubmissionFailed = 11,
    OutcomeInvariantViolation = 12,
    FinalizerGenerationExhausted = 13,
    Count = 14,
};

/** Exact worker-side boundaries exported by a Pearl algorithm implementation. */
enum class PearlAlgorithmProgressEvent : uint64_t {
    AttemptStarted,
    AttemptCompleted,
    HitsObserved,
    HitsRetained,
    HitBufferSaturated,
    FinalizerStarted,
    CandidateSelected,
    FinalizerProgress,
    HostValid,
    HostInvalid,
    ProofSerialized,
    CandidateDropped,
};

/**
 * Minimal algorithm hook.  Calls are synchronous on the mining worker; the
 * implementation must publish through a thread-safe bridge rather than let an
 * observer inspect algorithm/CUDA state.  `amount` is a saturating delta for
 * counter events; lifecycle boundary events always represent one boundary.
 * Implementations must not throw across the mining backend callback.
 */
class PearlAlgorithmProgressSink {
   public:
    virtual ~PearlAlgorithmProgressSink() = default;
    virtual void onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent event, uint64_t amount = 1,
        PearlProgressReason reason = PearlProgressReason::None) noexcept = 0;
};

/**
 * Fail-closed ownership for one algorithm finalizer invocation.
 *
 * Construct this immediately after publishing FinalizerStarted. Every exit
 * must either close the finalizer with an explicit terminal event or leave the
 * scope armed; an armed destructor publishes exactly one FinalizerFailed drop.
 * This keeps early device-readback and allocation failures from leaving the
 * observer permanently in Finalizing.
 */
class PearlFinalizerProgressScope {
   public:
    explicit PearlFinalizerProgressScope(
        PearlAlgorithmProgressSink *sink) noexcept
        : sink_(sink) {}

    ~PearlFinalizerProgressScope() noexcept { drop(); }

    PearlFinalizerProgressScope(const PearlFinalizerProgressScope &) = delete;
    PearlFinalizerProgressScope &operator=(
        const PearlFinalizerProgressScope &) = delete;

    void close() noexcept { active_ = false; }

    void drop(PearlProgressReason reason =
                  PearlProgressReason::FinalizerFailed) noexcept {
        if (!active_) return;
        active_ = false;
        if (sink_)
            sink_->onPearlAlgorithmProgress(
                PearlAlgorithmProgressEvent::CandidateDropped, 1, reason);
    }

    bool activeForTest() const noexcept { return active_; }

   private:
    PearlAlgorithmProgressSink *sink_ = nullptr;
    bool active_ = true;
};

constexpr std::size_t kPearlProgressReasonCount =
    static_cast<std::size_t>(PearlProgressReason::Count);

inline uint64_t pearlProgressSaturatingAdd(uint64_t value,
                                           uint64_t amount = 1) {
    const uint64_t max = std::numeric_limits<uint64_t>::max();
    return amount > max - value ? max : value + amount;
}

#define OM_PEARL_PROGRESS_U64_FIELDS(X)                                      \
    X(sessionGeneration) X(authorizationGeneration) X(jobGeneration)         \
    X(prepareGeneration) X(searchGeneration) X(finalizerGeneration)          \
    X(candidateGeneration)                                                    \
    X(submitGeneration) X(sessionStartedMs) X(authorizedMs) X(jobAdoptedMs)  \
    X(jobRetiredMs) X(prepareStartedMs) X(prepareCompletedMs)                 \
    X(searchStartedMs) X(searchCompletedMs) X(lastAttemptStartedMs)           \
    X(lastAttemptCompletedMs) X(lastFinalizerStartedMs)                       \
    X(lastFinalizerProgressMs) X(lastProofSerializedMs)                       \
    X(lastSubmitAttemptMs) X(lastAcknowledgedMs) X(lastOutcomeMs)             \
    X(publishedAtMs) X(searchesStarted) X(searchesCompleted)                  \
    X(searchesFailed) X(attemptsStarted) X(attemptsCompleted)                 \
    X(hitsObserved) X(hitsRetained) X(finalizersStarted)                      \
    X(hitSaturationEvents) X(hitsDroppedBySaturation)                         \
    X(candidatesSelected) X(hostChecked) X(hostValid) X(hostInvalid)          \
    X(proofsSerialized) X(submitAttempts) X(submissionFailures)               \
    X(submitted) X(acknowledged) X(accepted) X(rejected) X(stale) X(unknown) \
    X(pending) X(acceptedSuperseded) X(guardRetired)

struct PearlProgressSnapshot {
    PearlProgressPhase phase = PearlProgressPhase::Idle;
    PearlProgressReason lastReason = PearlProgressReason::None;
    PearlSubmitOutcome lastOutcome = PearlSubmitOutcome::None;

#define OM_DECLARE_PROGRESS_FIELD(name) uint64_t name = 0;
    OM_PEARL_PROGRESS_U64_FIELDS(OM_DECLARE_PROGRESS_FIELD)
#undef OM_DECLARE_PROGRESS_FIELD

    std::array<uint64_t, kPearlProgressReasonCount> localDrops{};

    void add(uint64_t *counter, uint64_t amount = 1) {
        if (counter) *counter = pearlProgressSaturatingAdd(*counter, amount);
    }

    void noteDrop(PearlProgressReason reason, uint64_t amount = 1) {
        const std::size_t index = static_cast<std::size_t>(reason);
        if (reason == PearlProgressReason::None ||
            index >= kPearlProgressReasonCount)
            return;
        localDrops[index] =
            pearlProgressSaturatingAdd(localDrops[index], amount);
        lastReason = reason;
    }

    uint64_t dropsFor(PearlProgressReason reason) const {
        const std::size_t index = static_cast<std::size_t>(reason);
        return index < kPearlProgressReasonCount ? localDrops[index] : 0;
    }

    bool invariantsHold() const {
        if (static_cast<uint64_t>(phase) >
                static_cast<uint64_t>(PearlProgressPhase::Failed) ||
            static_cast<uint64_t>(lastReason) >=
                static_cast<uint64_t>(PearlProgressReason::Count) ||
            static_cast<uint64_t>(lastOutcome) >
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
                static_cast<uint64_t>(PearlSubmitOutcome::GuardRetired))
#else
                static_cast<uint64_t>(PearlSubmitOutcome::Unknown))
#endif
            return false;
        if (searchesCompleted > searchesStarted) return false;
        if (searchesFailed > searchesStarted - searchesCompleted) return false;
        if (attemptsCompleted > attemptsStarted) return false;
        if (hitsRetained > hitsObserved) return false;
        if (hitsDroppedBySaturation > hitsObserved - hitsRetained) return false;
        if (hitSaturationEvents > attemptsStarted) return false;
        if (finalizersStarted > attemptsCompleted) return false;
        if (finalizersStarted > hitsRetained) return false;
        if (finalizersStarted != 0 && finalizerGeneration == 0) return false;
        if (candidatesSelected > finalizersStarted) return false;
        if (candidatesSelected > hitsRetained) return false;
        if (hostChecked > candidatesSelected) return false;
        if (hostValid > hostChecked || hostInvalid != hostChecked - hostValid)
            return false;
        if (proofsSerialized > hostValid) return false;
        if (submitAttempts > proofsSerialized) return false;
        if (submissionFailures > submitAttempts) return false;
        if (submitted > submitAttempts - submissionFailures) return false;

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
        if (pending != remaining) return false;
        if (acknowledged > submitted || acceptedSuperseded > accepted)
            return false;

        uint64_t ackRemaining = acknowledged;
        if (accepted > ackRemaining) return false;
        ackRemaining -= accepted;
        if (rejected > ackRemaining) return false;
        ackRemaining -= rejected;
        if (stale > ackRemaining) return false;

        switch (phase) {
            case PearlProgressPhase::Connecting:
            case PearlProgressPhase::AwaitingAuthorization:
                return sessionGeneration != 0;
            case PearlProgressPhase::AwaitingJob:
                return sessionGeneration != 0 &&
                       authorizationGeneration != 0;
            case PearlProgressPhase::Preparing:
                return sessionGeneration != 0 && jobGeneration != 0 &&
                       prepareGeneration != 0;
            case PearlProgressPhase::Searching:
                return sessionGeneration != 0 && jobGeneration != 0 &&
                       searchGeneration != 0;
            case PearlProgressPhase::Finalizing:
                return sessionGeneration != 0 && jobGeneration != 0 &&
                       searchGeneration != 0 && finalizerGeneration != 0;
            case PearlProgressPhase::AwaitingSubmit:
                return sessionGeneration != 0 && jobGeneration != 0 &&
                       searchGeneration != 0 && candidateGeneration != 0;
            case PearlProgressPhase::Submitting:
            case PearlProgressPhase::AwaitingAcknowledgement:
            case PearlProgressPhase::AwaitingOutcome:
                return sessionGeneration != 0 && jobGeneration != 0 &&
                       searchGeneration != 0 && candidateGeneration != 0 &&
                       submitGeneration != 0;
            default:
                return true;
        }
    }
};

/**
 * Single-writer, multi-reader coherent atomic mirror.
 *
 * The writer flips `sequence_` odd before payload stores and even afterward.
 * All participating atomic operations are sequentially consistent (therefore
 * acquire/release or stronger). A reader accepts only two matching even
 * sequence reads. Payload members are atomic too, so a failed read attempt is
 * still data-race-free.
 */
class PearlProgressBus {
   public:
    PearlProgressBus() {
        for (auto &counter : localDrops_)
            counter.store(0, std::memory_order_relaxed);
    }
    PearlProgressBus(const PearlProgressBus &) = delete;
    PearlProgressBus &operator=(const PearlProgressBus &) = delete;

    void publish(const PearlProgressSnapshot &snapshot) {
        publishImpl(snapshot, nullptr, nullptr);
    }

    // Deterministic host-test seam. The hook runs after the sequence becomes
    // odd and before the uint64 counter/timestamp payload is stored.
    // Production callers use publish(); the hook must not throw.
    void publishWithMidpointHookForTest(const PearlProgressSnapshot &snapshot,
                                        void (*hook)(void *),
                                        void *context) {
        publishImpl(snapshot, hook, context);
    }

   private:
    void publishImpl(const PearlProgressSnapshot &snapshot,
                     void (*hook)(void *), void *context) {
        // Single writer is part of the API contract. Stable values are always
        // even; uint64 wrap is safe for one publish (even -> odd -> even).
        sequence_.fetch_add(1, std::memory_order_seq_cst);
        phase_.store(static_cast<uint64_t>(snapshot.phase),
                     std::memory_order_seq_cst);
        lastReason_.store(static_cast<uint64_t>(snapshot.lastReason),
                          std::memory_order_seq_cst);
        lastOutcome_.store(static_cast<uint64_t>(snapshot.lastOutcome),
                           std::memory_order_seq_cst);
        if (hook) hook(context);
#define OM_STORE_PROGRESS_FIELD(name)                                       \
        name##_.store(snapshot.name, std::memory_order_seq_cst);
        OM_PEARL_PROGRESS_U64_FIELDS(OM_STORE_PROGRESS_FIELD)
#undef OM_STORE_PROGRESS_FIELD
        for (std::size_t i = 0; i < kPearlProgressReasonCount; ++i)
            localDrops_[i].store(snapshot.localDrops[i],
                                 std::memory_order_seq_cst);
        sequence_.fetch_add(1, std::memory_order_seq_cst);
    }

   public:
    /** Bounded read: false means the writer stayed active through all tries. */
    bool tryRead(PearlProgressSnapshot *out,
                 unsigned maxAttempts = 128) const {
        return tryReadImpl(out, maxAttempts, nullptr, nullptr);
    }

    // Deterministic host-test seam for the other retry boundary: the hook runs
    // after the first even sequence read but before its payload reads.
    bool tryReadWithAfterFirstSequenceHookForTest(
        PearlProgressSnapshot *out, unsigned maxAttempts,
        void (*hook)(void *), void *context) const {
        return tryReadImpl(out, maxAttempts, hook, context);
    }

   private:
    bool tryReadImpl(PearlProgressSnapshot *out, unsigned maxAttempts,
                     void (*hook)(void *), void *context) const {
        if (!out || maxAttempts == 0) return false;
        for (unsigned attempt = 0; attempt < maxAttempts; ++attempt) {
            const uint64_t before = sequence_.load(std::memory_order_seq_cst);
            if (before & 1) continue;
            if (attempt == 0 && hook) hook(context);

            PearlProgressSnapshot candidate;
            candidate.phase = static_cast<PearlProgressPhase>(
                phase_.load(std::memory_order_seq_cst));
            candidate.lastReason = static_cast<PearlProgressReason>(
                lastReason_.load(std::memory_order_seq_cst));
            candidate.lastOutcome = static_cast<PearlSubmitOutcome>(
                lastOutcome_.load(std::memory_order_seq_cst));
#define OM_LOAD_PROGRESS_FIELD(name)                                        \
            candidate.name = name##_.load(std::memory_order_seq_cst);
            OM_PEARL_PROGRESS_U64_FIELDS(OM_LOAD_PROGRESS_FIELD)
#undef OM_LOAD_PROGRESS_FIELD
            for (std::size_t i = 0; i < kPearlProgressReasonCount; ++i)
                candidate.localDrops[i] =
                    localDrops_[i].load(std::memory_order_seq_cst);
            const uint64_t after = sequence_.load(std::memory_order_seq_cst);
            if (before == after && !(after & 1)) {
                *out = candidate;
                return true;
            }
        }
        return false;
    }

   public:
    uint64_t sequence() const {
        return sequence_.load(std::memory_order_seq_cst);
    }

   private:
    std::atomic<uint64_t> sequence_{0};
    std::atomic<uint64_t> phase_{
        static_cast<uint64_t>(PearlProgressPhase::Idle)};
    std::atomic<uint64_t> lastReason_{
        static_cast<uint64_t>(PearlProgressReason::None)};
    std::atomic<uint64_t> lastOutcome_{
        static_cast<uint64_t>(PearlSubmitOutcome::None)};
#define OM_DECLARE_ATOMIC_PROGRESS_FIELD(name) std::atomic<uint64_t> name##_{0};
    OM_PEARL_PROGRESS_U64_FIELDS(OM_DECLARE_ATOMIC_PROGRESS_FIELD)
#undef OM_DECLARE_ATOMIC_PROGRESS_FIELD
    std::array<std::atomic<uint64_t>, kPearlProgressReasonCount> localDrops_{};
};

/**
 * Single-worker algorithm event recorder.  It owns algorithm attempt, hit,
 * finalizer, host-check, and proof counters, but no returned-Solution or
 * submission ownership.  In particular, candidateGeneration is supplied and
 * advanced only by a future sequential run-loop adapter. This recorder owns no
 * thread and applies no liveness policy.
 */
class PearlAlgorithmProgressWriter final : public PearlAlgorithmProgressSink {
   public:
    using Clock = uint64_t (*)(void *) noexcept;

    PearlAlgorithmProgressWriter(PearlProgressBus *bus,
                                  const PearlProgressSnapshot &initial,
                                  Clock clock, void *clockContext)
        : bus_(bus), clock_(clock), clockContext_(clockContext), state_(initial) {
        publishAt(now());
    }

    void onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent event, uint64_t amount = 1,
        PearlProgressReason reason =
            PearlProgressReason::None) noexcept override {
        if (generationExhausted_) return;
        const uint64_t t = now();
        switch (event) {
            case PearlAlgorithmProgressEvent::AttemptStarted:
                state_.add(&state_.attemptsStarted, amount);
                state_.lastAttemptStartedMs = t;
                state_.phase = PearlProgressPhase::Searching;
                break;
            case PearlAlgorithmProgressEvent::AttemptCompleted:
                state_.add(&state_.attemptsCompleted, amount);
                state_.lastAttemptCompletedMs = t;
                break;
            case PearlAlgorithmProgressEvent::HitsObserved:
                state_.add(&state_.hitsObserved, amount);
                break;
            case PearlAlgorithmProgressEvent::HitsRetained:
                state_.add(&state_.hitsRetained, amount);
                break;
            case PearlAlgorithmProgressEvent::HitBufferSaturated:
                state_.add(&state_.hitSaturationEvents);
                state_.add(&state_.hitsDroppedBySaturation, amount);
                state_.lastReason = PearlProgressReason::HitBufferSaturated;
                break;
            case PearlAlgorithmProgressEvent::FinalizerStarted:
                if (state_.finalizerGeneration ==
                    std::numeric_limits<uint64_t>::max()) {
                    state_.noteDrop(
                        PearlProgressReason::FinalizerGenerationExhausted);
                    state_.phase = PearlProgressPhase::Failed;
                    generationExhausted_ = true;
                    break;
                }
                ++state_.finalizerGeneration;
                state_.add(&state_.finalizersStarted);
                state_.lastFinalizerStartedMs = t;
                state_.lastFinalizerProgressMs = t;
                state_.phase = PearlProgressPhase::Finalizing;
                break;
            case PearlAlgorithmProgressEvent::CandidateSelected:
                state_.add(&state_.candidatesSelected, amount);
                state_.lastFinalizerProgressMs = t;
                break;
            case PearlAlgorithmProgressEvent::FinalizerProgress:
                state_.lastFinalizerProgressMs = t;
                break;
            case PearlAlgorithmProgressEvent::HostValid:
                state_.add(&state_.hostChecked, amount);
                state_.add(&state_.hostValid, amount);
                state_.lastFinalizerProgressMs = t;
                break;
            case PearlAlgorithmProgressEvent::HostInvalid:
                state_.add(&state_.hostChecked, amount);
                state_.add(&state_.hostInvalid, amount);
                state_.noteDrop(PearlProgressReason::HostVerificationFailed,
                                amount);
                state_.phase = PearlProgressPhase::Searching;
                break;
            case PearlAlgorithmProgressEvent::ProofSerialized:
                state_.add(&state_.proofsSerialized, amount);
                state_.lastProofSerializedMs = t;
                state_.phase = PearlProgressPhase::Searching;
                break;
            case PearlAlgorithmProgressEvent::CandidateDropped:
                state_.noteDrop(reason == PearlProgressReason::None
                                    ? PearlProgressReason::FinalizerFailed
                                    : reason,
                                amount);
                state_.phase = PearlProgressPhase::Searching;
                break;
        }
        publishAt(t);
    }

    PearlProgressSnapshot snapshotForTest() const { return state_; }

   private:
    uint64_t now() const noexcept {
        return clock_ ? clock_(clockContext_) : 0;
    }

    void publishAt(uint64_t t) noexcept {
        state_.publishedAtMs = t;
        if (bus_) bus_->publish(state_);
    }

    PearlProgressBus *bus_ = nullptr;
    Clock clock_ = nullptr;
    void *clockContext_ = nullptr;
    PearlProgressSnapshot state_;
    bool generationExhausted_ = false;
};

#undef OM_PEARL_PROGRESS_U64_FIELDS

}  // namespace om
