// Pure-host coordinator for Pearl source facts, liveness, and progress.
//
// One run-loop thread owns this object.  It is the only object permitted to
// mutate its liveness/progress snapshots and the only caller that publishes to
// the supplied PearlProgressBus.  Source integrity is deliberately kept
// separate from mining outcomes: bad source evidence quarantines translation,
// but never invents a share result or changes the mining phase to Failed.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>

#include "pearl_liveness.h"
#include "pearl_progress.h"
#include "pearl_source_observability.h"
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
#include "pearl_guard_retirement_v2.h"
#include "pearl_evidence_io.h"
#endif

namespace om {

enum class PearlSourceIntegrityFlag : uint64_t {
    None = 0,
    MailboxFailClosed = 1ULL << 0,
    PublicationRefused = 1ULL << 1,
    InvalidEvent = 1ULL << 2,
    ConsumerBudgetExceeded = 1ULL << 3,
    SnapshotAccountingInvalid = 1ULL << 4,
    SnapshotRollback = 1ULL << 5,
    WrongThread = 1ULL << 6,
    UnknownMailboxFault = 1ULL << 7,
    ReentrantDrain = 1ULL << 8,
    SourceException = 1ULL << 9,
    InvalidAlgorithmEvent = 1ULL << 10,
    TerminalEvidenceOverflow = 1ULL << 11,
    SearchProofBudgetExceeded = 1ULL << 12,
};

inline uint64_t pearlSourceIntegrityBit(PearlSourceIntegrityFlag flag) {
    return static_cast<uint64_t>(flag);
}

enum class PearlRawTerminalTrust : uint8_t {
    Untrusted = 0,
    Trusted = 1,
};

struct PearlRawTerminalEvidence {
    uint64_t observation = 0;
    PearlSourceEvent event;
    PearlRawTerminalTrust trust = PearlRawTerminalTrust::Untrusted;
};

constexpr std::size_t kPearlRawTerminalEvidenceCapacity = 256;

struct PearlRunProgressCoordinatorSnapshot {
    PearlProgressSnapshot progress;
    PearlLivenessSnapshot liveness;
    uint64_t sourceEventsDrained = 0;
    uint64_t sourceEventsInvalid = 0;
    uint64_t sourceRefusedEvents = 0;
    uint64_t sourceMailboxFaultBits = 0;
    uint64_t sourceIntegrityFlags = 0;
    uint64_t lastSourceEventToken = 0;
    uint64_t lastSourceEventMs = 0;
    uint64_t sourceQueueHighWater = 0;
    uint64_t sourceFaultEvents = 0;
    uint64_t terminalEvidenceObserved = 0;
    uint64_t terminalEvidenceDropped = 0;
    uint64_t terminalEvidenceConsumed = 0;
    std::size_t terminalEvidenceCount = 0;
    std::array<PearlRawTerminalEvidence,
               kPearlRawTerminalEvidenceCapacity>
        terminalEvidence{};
    bool sourceQuarantined = false;

    bool hasIntegrityFlag(PearlSourceIntegrityFlag flag) const {
        return (sourceIntegrityFlags & pearlSourceIntegrityBit(flag)) != 0;
    }
};

class PearlRunProgressCoordinator final : public PearlAlgorithmProgressSink {
    struct TranslationState;

   public:
    using Clock = uint64_t (*)(void *) noexcept;
    static constexpr std::size_t kDrainBatch = 32;
    static constexpr std::size_t kDrainPasses = 8;
    static constexpr std::size_t kProofBacklogCapacity = 256;

    PearlRunProgressCoordinator(
        PearlProgressBus *bus, Clock clock, void *clockContext,
        const PearlProgressSnapshot &initial = PearlProgressSnapshot(),
        const PearlLivenessDeadlines &deadlines = PearlLivenessDeadlines(),
        uint64_t jobGenerationLimitForTest =
            std::numeric_limits<uint64_t>::max())
        : bus_(bus),
          clock_(clock),
          clockContext_(clockContext),
          progress_(initial),
          deadlines_(deadlines),
          jobGenerationLimit_(jobGenerationLimitForTest),
          owner_(std::this_thread::get_id()) {
        publishProgress();
    }

    PearlRunProgressCoordinator(const PearlRunProgressCoordinator &) = delete;
    PearlRunProgressCoordinator &operator=(
        const PearlRunProgressCoordinator &) = delete;

#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
    bool enableDeferredSearchOwnership() noexcept {
        if (!ownerMutationAllowed() || source_ || sourceBaselined_ || deferredSearchOwnership_)
            return false;
        deferredSearchOwnership_=true;
        return true;
    }
    // Reconcile the explicitly deferred full-send return, keeping the original
    // in-flight slot until a correlated terminal fact arrives.
    bool onDeferredSubmitReturned(uint64_t generation,bool fullySent) noexcept {
        if (!deferredSearchOwnership_ || !ownerMutationAllowed() || !generation) return false;
        if (fullySent && inFlight_.present && inFlight_.localSubmit==generation &&
            pendingSubmitGeneration_==0) return true;
        return onSubmitReturned(generation,fullySent);
    }
#endif

    bool attachSource(PearlSourceObservability *source) noexcept {
        if (!onOwnerThread()) {
            noteWrongThread();
            return false;
        }
        if (absorbPendingIntegrity()) publishProgress();
        if (quarantined_) return false;
        if (source_ || sourceBaselined_) return false;
        source_ = source;
        return true;
    }

    void detachSource() noexcept {
        if (!onOwnerThread()) {
            noteWrongThread();
            return;
        }
        if (absorbPendingIntegrity()) publishProgress();
        source_ = nullptr;
    }

    /** Same-thread, noexcept hook invoked after source publication unlocks. */
    void onSourcePublication() noexcept { drainCheckpoint(); }

    /**
     * Drain at most 32 records in each of eight passes, then validate one
     * coherent mailbox snapshot.  Quarantined coordinators still drain, but
     * discard every later source event.
     */
    std::size_t drainCheckpoint() noexcept {
        if (!onOwnerThread()) {
            noteWrongThread();
            return 0;
        }

        bool changed = absorbPendingIntegrity();
        if (!source_) {
            if (changed) publishProgress();
            return 0;
        }

        if (draining_) {
            latch(PearlSourceIntegrityFlag::ReentrantDrain);
            return 0;
        }
        draining_ = true;
        const bool wasQuarantined = quarantined_;
        const TranslationState trusted = captureTranslationState();

        // This pre-drain observation is used only for queue high-water.  The
        // authoritative accounting/ownership validation snapshot remains the
        // one taken after the bounded drain.
        PearlSourceObservabilitySnapshot beforeDrain;
        try {
            beforeDrain = source_->snapshot();
        } catch (...) {
            latch(PearlSourceIntegrityFlag::SourceException);
            restoreTranslationState(trusted);
            draining_ = false;
            publishProgress();
            return 0;
        }
        if (beforeDrain.queued > sourceQueueHighWater_)
            sourceQueueHighWater_ = beforeDrain.queued;

        std::array<PearlSourceEvent, kDrainBatch> batch{};
        std::array<PearlSourceEvent, kDrainBatch * kDrainPasses>
            rawTerminals{};
        std::size_t rawTerminalCount = 0;
        std::size_t total = 0;
        bool exhausted = true;
        for (std::size_t pass = 0; pass < kDrainPasses; ++pass) {
            std::size_t count = 0;
            try {
                count = source_->drain(batch.data(), batch.size());
            } catch (...) {
                latch(PearlSourceIntegrityFlag::SourceException);
                changed = true;
                break;
            }
            if (count > batch.size()) {
                latch(PearlSourceIntegrityFlag::InvalidEvent, true);
                changed = true;
                break;
            }
            total += count;
            sourceEventsDrained_ = saturatingAdd(sourceEventsDrained_, count);
            if (count != 0) changed = true;
            sourceQueueHighWater_ =
                sourceQueueHighWater_ < count ? count : sourceQueueHighWater_;

            for (std::size_t i = 0; i < count; ++i) {
                if (batch[i].kind == PearlSourceEventKind::SubmitTerminal &&
                    rawTerminalCount < rawTerminals.size())
                    rawTerminals[rawTerminalCount++] = batch[i];
                if (quarantined_) continue;
                if (!consumeEvent(batch[i])) {
                    latch(PearlSourceIntegrityFlag::InvalidEvent, true);
                    changed = true;
                }
            }
            if (count < batch.size()) {
                exhausted = false;
                break;
            }
        }

        PearlSourceObservabilitySnapshot sourceState;
        try {
            sourceState = source_->snapshot();
        } catch (...) {
            latch(PearlSourceIntegrityFlag::SourceException);
            if (rawTerminalCount >
                kPearlRawTerminalEvidenceCapacity - terminalEvidenceCount_)
                latch(PearlSourceIntegrityFlag::TerminalEvidenceOverflow);
            appendRawTerminals(rawTerminals, rawTerminalCount, false);
            if (!wasQuarantined) restoreTranslationState(trusted);
            draining_ = false;
            publishProgress();
            return total;
        }
        if (sourceState.queued > sourceQueueHighWater_)
            sourceQueueHighWater_ = sourceState.queued;
        if (exhausted && sourceState.queued != 0) {
            latch(PearlSourceIntegrityFlag::ConsumerBudgetExceeded);
            changed = true;
        }
        changed = validateSourceSnapshot(sourceState, total) || changed;
        if (rawTerminalCount >
            kPearlRawTerminalEvidenceCapacity - terminalEvidenceCount_) {
            latch(PearlSourceIntegrityFlag::TerminalEvidenceOverflow);
            changed = true;
        }
        appendRawTerminals(rawTerminals, rawTerminalCount,
                           !wasQuarantined && !quarantined_);
        if (!wasQuarantined && quarantined_) {
            restoreTranslationState(trusted);
            changed = true;
        }
        draining_ = false;
        if (changed) publishProgress();
        return total;
    }

    /** Mirror the source's non-event issued-job binding on the owner thread. */
    bool onIssuedJobBound(uint64_t sessionToken, uint64_t jobToken,
                          const PearlJobIdentityDigest &identity) noexcept {
        if (!ownerMutationAllowed() || issued_.present || (inFlight_.present && !deferredSearchOwnership_) ||
            searchInFlight_ || pendingSubmitGeneration_ != 0 ||
            !active_.present || sessionToken != sourceSessionToken_ ||
            jobToken != active_.token || !digestEqual(identity, active_.digest))
            return false;
        PearlSourceObservabilitySnapshot sourceState;
        try {
            if (!source_) return false;
            sourceState = source_->snapshot();
        } catch (...) {
            latch(PearlSourceIntegrityFlag::SourceException);
            publishProgress();
            return false;
        }
        if (sourceState.queued != 0 || !sourceState.issuedJobBound ||
            sourceState.issuedSessionToken != sessionToken ||
            sourceState.issuedAuthorizationToken != sourceAuthorizationToken_ ||
            sourceState.issuedJobToken != jobToken ||
            !digestEqual(sourceState.issuedJobIdentity, identity) ||
            sourceState.activeJobToken != jobToken ||
            !digestEqual(sourceState.activeJobIdentity, identity)) {
            latch(PearlSourceIntegrityFlag::InvalidEvent);
            publishProgress();
            return false;
        }
        const uint64_t t = now();
        bool adoptedNow = false;
        if (!adopted_.present || adopted_.token != active_.token ||
            !digestEqual(adopted_.digest, active_.digest)) {
            const PearlLivenessSnapshot before =
                liveness_.snapshot(t, deadlines_);
            if (!before.queryTimeValid ||
                before.jobGeneration >= jobGenerationLimit_ ||
                (adopted_.present &&
                 (before.connection != PearlConnectionState::Ready ||
                  !searchCandidateAllowed(before.candidate))) ||
                (!adopted_.present &&
                 before.connection != PearlConnectionState::AwaitingJob))
                return false;
            if (adopted_.present &&
                !liveness_.onJobRetired(localSessionGeneration_, t))
                return false;
            const uint64_t localJob =
                liveness_.onValidJob(localSessionGeneration_, t);
            if (!localJob) return false;
            adopted_ = active_;
            prepareState_ = PrepareState::Idle;
            preparedJobGeneration_ = 0;
            progress_.jobGeneration = localJob;
            progress_.jobAdoptedMs = t;
            adoptedNow = true;
        }
        issued_ = active_;
        issued_.session = sessionToken;
        issued_.authorization = sourceAuthorizationToken_;
        prepareState_ = PrepareState::Idle;
        preparedJobGeneration_ = 0;
        if (adoptedNow) publishProgressAt(t);
        return true;
    }

    bool onIssuedJobReleased(uint64_t sessionToken,
                             uint64_t jobToken) noexcept {
        if (!ownerMutationAllowed() || !issued_.present || (inFlight_.present && !deferredSearchOwnership_) ||
            searchInFlight_ || pendingSubmitGeneration_ != 0 ||
            prepareState_ == PrepareState::Started ||
            prepareState_ == PrepareState::Completed ||
            sessionToken != issued_.session || jobToken != issued_.token)
            return false;
        const PearlLivenessSnapshot current =
            liveness_.snapshot(now(), deadlines_);
        if (!current.queryTimeValid ||
            !searchCandidateAllowed(current.candidate))
            return false;
        issued_ = JobSlot();
        return true;
    }

    /** Retire the run-loop-adopted job after a fetch failure. */
    bool onIssuedJobFetchFailed() noexcept {
        if (!ownerMutationAllowed() || issued_.present || (inFlight_.present && !deferredSearchOwnership_) ||
            searchInFlight_ || pendingSubmitGeneration_ != 0 ||
            !adopted_.present)
            return false;
        const uint64_t t = now();
        const PearlLivenessSnapshot before =
            liveness_.snapshot(t, deadlines_);
        if (!before.queryTimeValid ||
            !searchCandidateAllowed(before.candidate))
            return false;
        if (!liveness_.onJobRetired(localSessionGeneration_, t)) return false;
        adopted_ = JobSlot();
        prepareState_ = PrepareState::Idle;
        preparedJobGeneration_ = 0;
        progress_.jobRetiredMs = t;
        progress_.phase = PearlProgressPhase::AwaitingJob;
        publishProgressAt(t);
        return true;
    }

    bool onPrepareStarted() noexcept {
        if (!ownerMutationAllowed() || !issued_.present || !adopted_.present ||
            issued_.token != adopted_.token ||
            !digestEqual(issued_.digest, adopted_.digest) ||
            prepareState_ != PrepareState::Idle || searchInFlight_ ||
            (inFlight_.present && !deferredSearchOwnership_) || pendingSubmitGeneration_ != 0 ||
            progress_.prepareGeneration ==
                std::numeric_limits<uint64_t>::max())
            return false;
        const uint64_t t = now();
        const PearlLivenessSnapshot live = liveness_.snapshot(t, deadlines_);
        if (!live.queryTimeValid ||
            !searchCandidateAllowed(live.candidate) ||
            live.jobGeneration == 0)
            return false;
        prepareState_ = PrepareState::Started;
        preparedJobGeneration_ = live.jobGeneration;
        lastPrepared_ = JobSlot();
        lastPreparedJobGeneration_ = 0;
        ++progress_.prepareGeneration;
        progress_.prepareStartedMs = t;
        progress_.phase = PearlProgressPhase::Preparing;
        publishProgressAt(t);
        return true;
    }

    bool onPrepareCompleted() noexcept {
        if (!ownerMutationAllowed() || prepareState_ != PrepareState::Started ||
            progress_.phase != PearlProgressPhase::Preparing)
            return false;
        const uint64_t t = now();
        const PearlLivenessSnapshot live = liveness_.snapshot(t, deadlines_);
        if (!live.queryTimeValid ||
            live.jobGeneration != preparedJobGeneration_)
            return false;
        prepareState_ = PrepareState::Completed;
        lastPrepared_ = issued_;
        lastPreparedJobGeneration_ = live.jobGeneration;
        progress_.prepareCompletedMs = t;
        publishProgressAt(t);
        return true;
    }

    /** Reuse a durable prepared cache only for the exact rebound identity. */
    bool onPrepareReused(uint64_t sessionToken, uint64_t authorizationToken,
                         uint64_t jobToken,
                         const PearlJobIdentityDigest &identity) noexcept {
        if (!ownerMutationAllowed() || prepareState_ != PrepareState::Idle ||
            !issued_.present || !adopted_.present || !lastPrepared_.present ||
            issued_.session != sessionToken ||
            issued_.authorization != authorizationToken ||
            issued_.token != jobToken ||
            !digestEqual(issued_.digest, identity) ||
            adopted_.session != sessionToken ||
            adopted_.authorization != authorizationToken ||
            adopted_.token != jobToken ||
            !digestEqual(adopted_.digest, identity) ||
            lastPrepared_.session != sessionToken ||
            lastPrepared_.authorization != authorizationToken ||
            lastPrepared_.token != jobToken ||
            !digestEqual(lastPrepared_.digest, identity))
            return false;
        const uint64_t t = now();
        const PearlLivenessSnapshot live = liveness_.snapshot(t, deadlines_);
        if (!live.queryTimeValid ||
            live.jobGeneration != lastPreparedJobGeneration_)
            return false;
        prepareState_ = PrepareState::Completed;
        preparedJobGeneration_ = live.jobGeneration;
        publishProgress();
        return true;
    }

    bool onPrepareFailed(
        PearlProgressReason reason = PearlProgressReason::PrepareFailed) noexcept {
        return closePrepare(false, reason);
    }

    bool onPrepareAborted(
        PearlProgressReason reason = PearlProgressReason::PrepareFailed) noexcept {
        return closePrepare(true, reason);
    }

    bool onSearchStarted(uint64_t expectedAttempts) noexcept {
        if (!ownerMutationAllowed() || !issued_.present || !adopted_.present ||
            issued_.token != adopted_.token ||
            !digestEqual(issued_.digest, adopted_.digest) || (inFlight_.present && !deferredSearchOwnership_) ||
            pendingSubmitGeneration_ != 0 || searchInFlight_ ||
            attemptState_ != AttemptState::Idle ||
            finalizerState_ != FinalizerState::Idle ||
            prepareState_ != PrepareState::Completed)
            return false;
        if (expectedAttempts >
            kProofBacklogCapacity - proofBacklogCount_) {
            latch(PearlSourceIntegrityFlag::SearchProofBudgetExceeded);
            publishProgress();
            return false;
        }
        const uint64_t t = now();
        const PearlLivenessSnapshot before =
            liveness_.snapshot(t, deadlines_);
        if (!before.queryTimeValid ||
            !searchCandidateAllowed(before.candidate) ||
            before.jobGeneration != preparedJobGeneration_)
            return false;
        if (expectedAttempts == 0) {
            progress_.noteDrop(PearlProgressReason::AttemptCountMismatch);
            publishProgressAt(t);
            return false;
        }
        if (progress_.searchGeneration ==
            std::numeric_limits<uint64_t>::max()) {
            progress_.noteDrop(PearlProgressReason::SearchFailed);
            progress_.phase = PearlProgressPhase::Failed;
            publishProgressAt(t);
            return false;
        }
        ++progress_.searchGeneration;
        lastSearchJob_ = issued_;
        progress_.searchStartedMs = t;
        progress_.add(&progress_.searchesStarted);
        searchAttemptBaseline_ = progress_.attemptsCompleted;
        expectedSearchAttempts_ = expectedAttempts;
        serializedOrdinalForSearch_ = 0;
        resetAttemptAccounting();
        searchInFlight_ = true;
        prepareState_ = PrepareState::Idle;
        preparedJobGeneration_ = 0;
        progress_.phase = PearlProgressPhase::Searching;
        publishProgressAt(t);
        return true;
    }

    bool onSearchCompleted(bool success = true) noexcept {
        if (!ownerMutationAllowed() || !searchInFlight_ ||
            progress_.searchesStarted == 0 ||
            progress_.searchesCompleted > progress_.searchesStarted ||
            progress_.searchesFailed >=
                progress_.searchesStarted - progress_.searchesCompleted)
            return false;
        if (finalizerState_ != FinalizerState::Idle) {
            latch(PearlSourceIntegrityFlag::InvalidAlgorithmEvent);
            publishProgress();
            return false;
        }
        const uint64_t t = now();
        searchInFlight_ = false;
        if (success &&
            (progress_.attemptsCompleted < searchAttemptBaseline_ ||
             progress_.attemptsCompleted - searchAttemptBaseline_ !=
                 expectedSearchAttempts_)) {
            progress_.add(&progress_.searchesFailed);
            progress_.noteDrop(PearlProgressReason::AttemptCountMismatch);
            progress_.searchCompletedMs = t;
            attemptState_ = AttemptState::Idle;
            publishProgressAt(t);
            return false;
        }
        if (success && !currentAttemptClosed()) {
            latch(PearlSourceIntegrityFlag::InvalidAlgorithmEvent);
            publishProgress();
            return false;
        }
        attemptState_ = AttemptState::Idle;
        if (success)
            progress_.add(&progress_.searchesCompleted);
        else {
            progress_.add(&progress_.searchesFailed);
            progress_.lastReason = PearlProgressReason::SearchFailed;
        }
        progress_.searchCompletedMs = t;
        publishProgressAt(t);
        return true;
    }

    /** Retire the exact unconsumed search suffix after a trusted disconnect.
     * This owner boundary is deliberately outside drain/translation rollback.
     * Invalid requests preserve backlog and outcomes; callers must stop. */
    bool retireDisconnectedSearch(
        uint64_t session, uint64_t authorization, uint64_t job,
        const PearlJobIdentityDigest &identity, uint64_t searchGeneration,
        uint64_t expectedRemaining) noexcept {
        if (!ownerMutationAllowed() || draining_ || !sourceBaselined_ ||
            !haveLastConsumedEvent_ ||
            lastConsumedEvent_.kind != PearlSourceEventKind::Disconnected ||
            lastConsumedEvent_.sessionToken != session ||
            lastConsumedEvent_.authorizationToken != authorization ||
            sourceSessionToken_ != 0 || transportConnected_ || sessionReady_ ||
            retained_.present || active_.present || adopted_.present ||
            issued_.present || inFlight_.present || searchInFlight_ ||
            pendingSubmitGeneration_ != 0 || prepareState_ != PrepareState::Idle ||
            attemptState_ != AttemptState::Idle || finalizerState_ != FinalizerState::Idle ||
            searchGeneration == 0 || searchGeneration != progress_.searchGeneration ||
            searchGeneration <= lastRetiredDisconnectedSearchGeneration_ ||
            !lastSearchJob_.present || lastSearchJob_.session != session ||
            lastSearchJob_.authorization != authorization || lastSearchJob_.token != job ||
            !digestEqual(lastSearchJob_.digest, identity) ||
            expectedRemaining != proofBacklogCount_)
            return false;
        for (std::size_t i = 0; i < proofBacklogCount_; ++i) {
            const ProofRecord &proof = proofBacklog_[
                (proofBacklogHead_ + i) % kProofBacklogCapacity];
            if (proof.job.session != session || proof.job.authorization != authorization ||
                proof.job.token != job || !digestEqual(proof.job.digest, identity) ||
                proof.searchGeneration != searchGeneration)
                return false;
        }
        proofBacklog_ = {};
        proofBacklogHead_ = proofBacklogCount_ = 0;
        lastRetiredDisconnectedSearchGeneration_ = searchGeneration;
        if (expectedRemaining != 0)
            progress_.noteDrop(PearlProgressReason::SessionSuperseded, expectedRemaining);
        publishProgress();
        return true;
    }

    /** Open one exact serialized result before outer ownership verification. */
    uint64_t onCandidateObserved(
        uint64_t sessionToken, uint64_t authorizationToken, uint64_t jobToken,
        const PearlJobIdentityDigest &identity,
        uint64_t searchGeneration, uint64_t serializedOrdinal) noexcept {
        if (!ownerMutationAllowed() || searchInFlight_ || !issued_.present ||
            !adopted_.present ||
            issued_.token != adopted_.token ||
            !digestEqual(issued_.digest, adopted_.digest) ||
            proofBacklogCount_ == 0 ||
            progress_.candidateGeneration ==
                std::numeric_limits<uint64_t>::max())
            return 0;
        const ProofRecord &proof = proofBacklog_[proofBacklogHead_];
        if (proof.job.session != sessionToken ||
            proof.job.authorization != authorizationToken ||
            proof.job.token != jobToken ||
            !digestEqual(proof.job.digest, identity) ||
            proof.searchGeneration != searchGeneration ||
            proof.serializedOrdinal != serializedOrdinal ||
            issued_.session != sessionToken ||
            issued_.authorization != authorizationToken ||
            issued_.token != jobToken ||
            !digestEqual(issued_.digest, identity)) {
            latch(PearlSourceIntegrityFlag::InvalidAlgorithmEvent);
            publishProgress();
            return 0;
        }
        const uint64_t t = now();
        const PearlLivenessSnapshot before = liveness_.snapshot(t, deadlines_);
        const uint64_t candidate =
            liveness_.onCandidate(before.jobGeneration, t);
        if (candidate == 0) return 0;
        proofBacklogHead_ =
            (proofBacklogHead_ + 1) % kProofBacklogCapacity;
        --proofBacklogCount_;
        ++progress_.candidateGeneration;
        progress_.phase = PearlProgressPhase::Finalizing;
        publishProgressAt(t);
        return candidate;
    }

    /** Close the exact outer verification boundary once, before submission. */
    bool onCandidateFinalized(
        uint64_t candidateGeneration, bool submittable,
        PearlProgressReason reason =
            PearlProgressReason::CandidateOwnershipLost) noexcept {
        if (!ownerMutationAllowed()) return false;
        const uint64_t t = now();
        if (!liveness_.onCandidateFinalized(candidateGeneration, submittable,
                                            t))
            return false;
        if (submittable) {
            progress_.phase = PearlProgressPhase::AwaitingSubmit;
        } else {
            progress_.noteDrop(reason == PearlProgressReason::None
                                   ? PearlProgressReason::CandidateOwnershipLost
                                   : reason);
            progress_.phase = PearlProgressPhase::Searching;
        }
        publishProgressAt(t);
        return true;
    }

    /** Must run immediately before the potentially blocking source submit. */
    uint64_t onSubmitAttempt() noexcept {
        if (!ownerMutationAllowed() || pendingSubmitGeneration_ != 0 ||
            !issued_.present)
            return 0;
        const uint64_t t = now();
        const PearlLivenessSnapshot before = liveness_.snapshot(t, deadlines_);
        const uint64_t submit =
            liveness_.onSubmitAttempt(before.candidateGeneration, t);
        if (submit == 0) return 0;
        pendingSubmitGeneration_ = submit;
        progress_.submitGeneration = submit;
        progress_.add(&progress_.submitAttempts);
        progress_.lastSubmitAttemptMs = t;
        progress_.phase = PearlProgressPhase::Submitting;
        publishProgressAt(t);
        return submit;
    }

    /** Reconcile the legacy synchronous submit return after its final drain. */
    bool onSubmitReturned(uint64_t submitGeneration,
                          bool returnedSuccess) noexcept {
        if (!ownerMutationAllowed() || submitGeneration == 0) return false;
        if (submitGeneration == lastCompletedLocalSubmit_ &&
            pendingSubmitGeneration_ == 0 && !inFlight_.present)
            return true;
        if (inFlight_.present &&
            inFlight_.localSubmit == submitGeneration) {
            latch(PearlSourceIntegrityFlag::InvalidEvent);
            publishProgress();
            return false;
        }
        if (pendingSubmitGeneration_ != submitGeneration) return false;
        if (returnedSuccess) {
            latch(PearlSourceIntegrityFlag::InvalidEvent);
            publishProgress();
            return false;
        }
        const uint64_t t = now();
        if (!liveness_.onSubmissionFailed(submitGeneration, t)) return false;
        pendingSubmitGeneration_ = 0;
        lastCompletedLocalSubmit_ = submitGeneration;
        progress_.add(&progress_.submissionFailures);
        progress_.noteDrop(PearlProgressReason::SubmissionFailed);
        progress_.lastOutcome = PearlSubmitOutcome::SubmissionFailed;
        progress_.lastOutcomeMs = t;
        progress_.phase = adopted_.present ? PearlProgressPhase::Searching
                                           : PearlProgressPhase::AwaitingJob;
        publishProgressAt(t);
        return true;
    }

    void onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent event, uint64_t amount = 1,
        PearlProgressReason reason =
            PearlProgressReason::None) noexcept override {
        if (!onOwnerThread()) {
            noteWrongThread();
            return;
        }
        if (absorbPendingIntegrity()) {
            publishProgress();
            return;
        }
        if (quarantined_ || algorithmGenerationExhausted_) return;
        if (!validAlgorithmEvent(event, amount)) {
            latch(PearlSourceIntegrityFlag::InvalidAlgorithmEvent);
            publishProgress();
            return;
        }
        const uint64_t t = now();
        const PearlLivenessSnapshot live = liveness_.snapshot(t, deadlines_);
        if (!live.queryTimeValid) {
            latch(PearlSourceIntegrityFlag::InvalidAlgorithmEvent);
            publishProgress();
            return;
        }
        switch (event) {
            case PearlAlgorithmProgressEvent::AttemptStarted:
                resetAttemptAccounting();
                attemptState_ = AttemptState::Started;
                progress_.add(&progress_.attemptsStarted, amount);
                progress_.lastAttemptStartedMs = t;
                progress_.phase = PearlProgressPhase::Searching;
                break;
            case PearlAlgorithmProgressEvent::AttemptCompleted: {
                PearlLivenessTracker nextLiveness = liveness_;
                if (!nextLiveness.onAttemptCompleted(live.jobGeneration, t)) {
                    latch(PearlSourceIntegrityFlag::InvalidAlgorithmEvent);
                    publishProgress();
                    return;
                }
                liveness_ = nextLiveness;
                attemptState_ = AttemptState::Completed;
                progress_.add(&progress_.attemptsCompleted, amount);
                progress_.lastAttemptCompletedMs = t;
                break;
            }
            case PearlAlgorithmProgressEvent::HitsObserved:
                attemptState_ = AttemptState::Observed;
                currentAttemptObserved_ = amount;
                progress_.add(&progress_.hitsObserved, amount);
                break;
            case PearlAlgorithmProgressEvent::HitsRetained:
                attemptState_ = AttemptState::Retained;
                currentAttemptRetained_ = amount;
                progress_.add(&progress_.hitsRetained, amount);
                break;
            case PearlAlgorithmProgressEvent::HitBufferSaturated:
                currentAttemptSaturationReported_ = true;
                currentAttemptDropped_ = amount;
                progress_.add(&progress_.hitSaturationEvents);
                progress_.add(&progress_.hitsDroppedBySaturation, amount);
                progress_.lastReason = PearlProgressReason::HitBufferSaturated;
                break;
            case PearlAlgorithmProgressEvent::FinalizerStarted:
                if (progress_.finalizerGeneration ==
                    std::numeric_limits<uint64_t>::max()) {
                    progress_.noteDrop(
                        PearlProgressReason::FinalizerGenerationExhausted);
                    progress_.phase = PearlProgressPhase::Failed;
                    algorithmGenerationExhausted_ = true;
                    break;
                }
                currentAttemptFinalizerStarted_ = true;
                finalizerState_ = FinalizerState::Started;
                ++progress_.finalizerGeneration;
                progress_.add(&progress_.finalizersStarted);
                progress_.lastFinalizerStartedMs = t;
                progress_.lastFinalizerProgressMs = t;
                progress_.phase = PearlProgressPhase::Finalizing;
                break;
            case PearlAlgorithmProgressEvent::CandidateSelected:
                finalizerState_ = FinalizerState::CandidateSelected;
                progress_.add(&progress_.candidatesSelected, amount);
                progress_.lastFinalizerProgressMs = t;
                break;
            case PearlAlgorithmProgressEvent::FinalizerProgress:
                progress_.lastFinalizerProgressMs = t;
                break;
            case PearlAlgorithmProgressEvent::HostValid:
                finalizerState_ = FinalizerState::HostValid;
                progress_.add(&progress_.hostChecked, amount);
                progress_.add(&progress_.hostValid, amount);
                progress_.lastFinalizerProgressMs = t;
                break;
            case PearlAlgorithmProgressEvent::HostInvalid:
                currentAttemptFinalizerCompleted_ = true;
                finalizerState_ = FinalizerState::Idle;
                progress_.add(&progress_.hostChecked, amount);
                progress_.add(&progress_.hostInvalid, amount);
                progress_.noteDrop(PearlProgressReason::HostVerificationFailed,
                                   amount);
                progress_.phase = PearlProgressPhase::Searching;
                break;
            case PearlAlgorithmProgressEvent::ProofSerialized:
                appendProofBacklog();
                currentAttemptFinalizerCompleted_ = true;
                finalizerState_ = FinalizerState::Idle;
                progress_.add(&progress_.proofsSerialized, amount);
                progress_.lastProofSerializedMs = t;
                progress_.phase = PearlProgressPhase::Searching;
                break;
            case PearlAlgorithmProgressEvent::CandidateDropped:
                currentAttemptFinalizerCompleted_ = true;
                finalizerState_ = FinalizerState::Idle;
                progress_.noteDrop(reason == PearlProgressReason::None
                                       ? PearlProgressReason::FinalizerFailed
                                       : reason,
                                   amount);
                progress_.phase = PearlProgressPhase::Searching;
                break;
        }
        publishProgressAt(t);
    }

    /** Transfer the oldest raw terminals to the owner without erasing their
     * cumulative accounting. Quarantine does not hide untrusted evidence.
     * Output must not overlap this coordinator; no allocation or callback. */
    std::size_t consumeTerminalEvidence(PearlRawTerminalEvidence *out,
                                        std::size_t maximum) noexcept {
        if (!onOwnerThread()) {
            noteWrongThread();
            return 0;
        }
        if (draining_) {
            latch(PearlSourceIntegrityFlag::ReentrantDrain);
            return 0;
        }
        if (!out || maximum == 0 || terminalEvidenceCount_ == 0) return 0;
        const std::size_t count = maximum < terminalEvidenceCount_
                                     ? maximum : terminalEvidenceCount_;
        const auto begin = reinterpret_cast<std::uintptr_t>(out);
        const auto self = reinterpret_cast<std::uintptr_t>(this);
        const std::size_t bytes = count * sizeof(*out);
        if (begin > std::numeric_limits<std::uintptr_t>::max() - bytes ||
            self > std::numeric_limits<std::uintptr_t>::max() - sizeof(*this) ||
            (begin < self + sizeof(*this) && self < begin + bytes)) {
            latch(PearlSourceIntegrityFlag::InvalidEvent);
            publishProgress();
            return 0;
        }
        for (std::size_t i = 0; i < count; ++i) out[i] = terminalEvidence_[i];
        const std::size_t remaining = terminalEvidenceCount_ - count;
        for (std::size_t i = 0; i < remaining; ++i)
            terminalEvidence_[i] = terminalEvidence_[i + count];
        for (std::size_t i = remaining; i < terminalEvidenceCount_; ++i)
            terminalEvidence_[i] = PearlRawTerminalEvidence{};
        terminalEvidenceCount_ = remaining;
        terminalEvidenceConsumed_ = saturatingAdd(terminalEvidenceConsumed_, count);
        return count;
    }

    PearlRunProgressCoordinatorSnapshot snapshotForOwner() noexcept {
        if (!onOwnerThread()) {
            noteWrongThread();
            return PearlRunProgressCoordinatorSnapshot();
        }
        if (absorbPendingIntegrity()) publishProgress();
        PearlRunProgressCoordinatorSnapshot out;
        out.progress = progress_;
        out.liveness = liveness_.snapshot(now(), deadlines_);
        out.sourceEventsDrained = sourceEventsDrained_;
        out.sourceEventsInvalid = sourceEventsInvalid_;
        out.sourceRefusedEvents = sourceRefusedEvents_;
        out.sourceMailboxFaultBits = sourceMailboxFaultBits_;
        out.sourceIntegrityFlags = sourceIntegrityFlags_;
        out.lastSourceEventToken = lastSourceEventToken_;
        out.lastSourceEventMs = lastSourceEventMs_;
        out.sourceQueueHighWater = sourceQueueHighWater_;
        out.sourceFaultEvents = sourceFaultEvents_;
        out.terminalEvidenceObserved = terminalEvidenceObserved_;
        out.terminalEvidenceDropped = terminalEvidenceDropped_;
        out.terminalEvidenceConsumed = terminalEvidenceConsumed_;
        out.terminalEvidenceCount = terminalEvidenceCount_;
        out.terminalEvidence = terminalEvidence_;
        out.sourceQuarantined = quarantined_;
        return out;
    }
    bool evidenceQuiescent() const noexcept {
        if (!onOwnerThread() || quarantined_ || pendingIntegrity_.load(std::memory_order_relaxed) || draining_) return false;
        return !issued_.present && !inFlight_.present && !pendingSubmitGeneration_ &&
            !searchInFlight_ && !proofBacklogCount_ &&
            prepareState_==PrepareState::Idle && attemptState_==AttemptState::Idle &&
            finalizerState_==FinalizerState::Idle && terminalEvidenceCount_==0;
    }

#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    // Bound before work; subsequent completion cannot supply a new target or identity.
    bool armGuardNaturalStop(guard_retirement_v2::Session &session,const char *runId,
            unsigned target,unsigned durationSeconds,unsigned minimumAccepted) noexcept {
        if (!onOwnerThread() || naturalSession_ || !pearlEvidenceRunIdValid(runId) ||
            !evidenceQuiescent() || progress_.submitAttempts || session.totals().fullProxy ||
            !session.ready(now()) || session.production() || (target==0)==(durationSeconds==0) || target>10 ||
            (durationSeconds && (!minimumAccepted || durationSeconds>86400))) return false;
        retirement_experiment::Id run{};
        auto digit=[](char c) { return c<='9' ? c-'0' : c-'a'+10; };
        for (unsigned i=0;i<16;++i) run[i]=uint8_t(digit(runId[i*2])*16+digit(runId[i*2+1]));
        if (run!=session.run()) return false;
        naturalSession_=&session; naturalRun_=run; naturalSessionId_=session.session();
        naturalTarget_=target; naturalSeconds_=durationSeconds; naturalMinimum_=minimumAccepted;
        return true;
    }
    // Explicit operator-owned lifecycle. No quota, no duration, no fabricated qualification.
    bool armGuardProduction(guard_retirement_v2::Session &session,const char *runId) noexcept {
        if (!onOwnerThread() || naturalSession_ || !pearlEvidenceRunIdValid(runId) ||
            !evidenceQuiescent() || progress_.submitAttempts || session.totals().fullProxy ||
            !session.production() || session.deadline()!=0 || !session.ready(now())) return false;
        retirement_experiment::Id run{};
        auto digit=[](char c) { return c<='9' ? c-'0' : c-'a'+10; };
        for (unsigned i=0;i<16;++i) run[i]=uint8_t(digit(runId[i*2])*16+digit(runId[i*2+1]));
        if (run!=session.run()) return false;
        naturalSession_=&session; naturalRun_=run; naturalSessionId_=session.session();
        return true;
    }
    bool startGuardDuration(int64_t startNs) noexcept {
        if (!naturalSession_) return true;
        if (!onOwnerThread() || !naturalSeconds_ || naturalStarted_ || startNs<=0 ||
            static_cast<uint64_t>(startNs/1000000)>now()) return false;
        naturalStartNs_=startNs;
        naturalStartMs_=static_cast<uint64_t>(startNs/1000000);
        naturalStarted_=true; return true;
    }
    bool guardCapacityHealthy() const noexcept {
        if (!naturalSession_ || naturalSession_->totals().fullProxy<naturalSession_->cap()) return true;
        if (naturalTarget_) return naturalSession_->totals().accepted==naturalTarget_;
        return naturalStarted_ && naturalSession_->capacityReachedMs()>=naturalStartMs_ &&
            naturalSession_->capacityReachedMs()-naturalStartMs_>=uint64_t(naturalSeconds_)*1000+
                (naturalStartNs_%1000000 ? 1 : 0);
    }
    bool finishGuardNaturalStop(const guard_retirement_v2::Totals &pool,int64_t stopNs) noexcept {
        if (!naturalSession_ || !evidenceQuiescent() || !guardCapacityHealthy()) return false;
        // Source retains the released issued binding until diagnostic close;
        // a redundant source checkpoint here would falsely quarantine it.
        const auto state=snapshotForOwner(); const auto &g=state.progress; const auto &l=state.liveness;
        const uint64_t current=now();
        const uint64_t stop=stopNs>0 ? static_cast<uint64_t>(stopNs/1000000) : current;
        if (!evidenceQuiescent() || state.sourceIntegrityFlags || state.sourceEventsInvalid ||
            state.sourceRefusedEvents || state.sourceMailboxFaultBits || state.terminalEvidenceDropped ||
            state.terminalEvidenceObserved!=state.terminalEvidenceConsumed ||
            !g.invariantsHold() || !l.submissionAccountingHolds() || !l.outcomeEvidenceHolds() ||
            !l.queryTimeValid || l.candidate!=PearlCandidateState::Idle || g.pending ||
            g.submissionFailures!=l.submissionFailures || l.possiblyDeliveredUnknowns || l.unacknowledgedUnknowns ||
            g.hostInvalid || g.searchesFailed || g.rejected || g.unknown ||
            (g.stale && !naturalSession_->personalProduction()) ||
            pool.fullProxy!=g.submitted || pool.accepted!=g.accepted || pool.stale!=g.stale ||
            pool.retired!=g.guardRetired || l.guardRetired!=g.guardRetired || pool.rejected || pool.unknown ||
            (pool.stale && !naturalSession_->personalProduction()) ||
            g.acknowledged!=g.accepted+g.stale || l.acknowledgements!=g.accepted+g.stale ||
            l.outcomes!=g.submitAttempts || l.submitAttempts!=g.submitAttempts ||
            g.submitted>g.submitAttempts || g.submitAttempts-g.submitted!=g.submissionFailures || stop>current ||
            (naturalTarget_ && (stopNs || g.accepted!=naturalTarget_)) ||
            (naturalSeconds_ && (!naturalStarted_ || stopNs<=0 || g.accepted<naturalMinimum_ ||
                stopNs<naturalStartNs_ || uint64_t(stopNs-naturalStartNs_)<uint64_t(naturalSeconds_)*1000000000))) return false;
        return naturalSession_->finishNatural(naturalRun_,naturalSessionId_,pool,stop,current);
    }
#endif

    /** Atomic notification mirror; not a progress/integrity coherence claim. */
    uint64_t publishedSourceIntegrityFlags() const noexcept {
        return publishedIntegrityFlags_.load(std::memory_order_acquire);
    }

   private:
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    guard_retirement_v2::Session *naturalSession_=nullptr;
    retirement_experiment::Id naturalRun_{},naturalSessionId_{};
    unsigned naturalTarget_=0,naturalSeconds_=0,naturalMinimum_=0;
    int64_t naturalStartNs_=0;
    uint64_t naturalStartMs_=0; bool naturalStarted_=false;
#endif
    enum class PrepareState : uint8_t {
        Idle,
        Started,
        Completed,
        Failed,
        Aborted,
    };

    enum class AttemptState : uint8_t {
        Idle,
        Started,
        Completed,
        Observed,
        Retained,
    };

    enum class FinalizerState : uint8_t {
        Idle,
        Started,
        CandidateSelected,
        HostValid,
    };

    struct JobSlot {
        bool present = false;
        uint64_t session = 0;
        uint64_t authorization = 0;
        uint64_t token = 0;
        PearlJobIdentityDigest digest;
    };

    struct InFlightSlot : JobSlot {
        uint64_t operation = 0;
        uint64_t localSubmit = 0;
    };

    struct ProofRecord {
        JobSlot job;
        uint64_t searchGeneration = 0;
        uint64_t serializedOrdinal = 0;
    };

    struct TranslationState {
        PearlProgressSnapshot progress;
        PearlLivenessTracker liveness;
        bool haveSourceTimestamp = false;
        bool transportConnected = false;
        bool sessionReady = false;
        bool readyWithoutAuthorization = false;
        bool sourceFaultObserved = false;
        bool searchInFlight = false;
        PrepareState prepareState = PrepareState::Idle;
        AttemptState attemptState = AttemptState::Idle;
        FinalizerState finalizerState = FinalizerState::Idle;
        uint64_t sourceSessionToken = 0;
        uint64_t sourceAuthorizationToken = 0;
        uint64_t localSessionGeneration = 0;
        uint64_t pendingSubmitGeneration = 0;
        uint64_t lastCompletedLocalSubmit = 0;
        uint64_t searchAttemptBaseline = 0;
        uint64_t expectedSearchAttempts = 0;
        uint64_t preparedJobGeneration = 0;
        uint64_t lastSourceEventToken = 0;
        uint64_t lastSourceEventMs = 0;
        uint64_t lastSourceSessionToken = 0;
        uint64_t lastSourceAuthorizationToken = 0;
        uint64_t lastSourceJobToken = 0;
        uint64_t lastSourceOperationToken = 0;
        bool haveLastConsumedEvent = false;
        PearlSourceEvent lastConsumedEvent;
        JobSlot retained;
        JobSlot active;
        JobSlot issued;
        JobSlot adopted;
        InFlightSlot inFlight;
        uint64_t sourceFaultEvents = 0;
    };

    TranslationState captureTranslationState() const {
        TranslationState out;
        out.progress = progress_;
        out.liveness = liveness_;
        out.haveSourceTimestamp = haveSourceTimestamp_;
        out.transportConnected = transportConnected_;
        out.sessionReady = sessionReady_;
        out.readyWithoutAuthorization = readyWithoutAuthorization_;
        out.sourceFaultObserved = sourceFaultObserved_;
        out.searchInFlight = searchInFlight_;
        out.prepareState = prepareState_;
        out.attemptState = attemptState_;
        out.finalizerState = finalizerState_;
        out.sourceSessionToken = sourceSessionToken_;
        out.sourceAuthorizationToken = sourceAuthorizationToken_;
        out.localSessionGeneration = localSessionGeneration_;
        out.pendingSubmitGeneration = pendingSubmitGeneration_;
        out.lastCompletedLocalSubmit = lastCompletedLocalSubmit_;
        out.searchAttemptBaseline = searchAttemptBaseline_;
        out.expectedSearchAttempts = expectedSearchAttempts_;
        out.preparedJobGeneration = preparedJobGeneration_;
        out.lastSourceEventToken = lastSourceEventToken_;
        out.lastSourceEventMs = lastSourceEventMs_;
        out.lastSourceSessionToken = lastSourceSessionToken_;
        out.lastSourceAuthorizationToken = lastSourceAuthorizationToken_;
        out.lastSourceJobToken = lastSourceJobToken_;
        out.lastSourceOperationToken = lastSourceOperationToken_;
        out.haveLastConsumedEvent = haveLastConsumedEvent_;
        out.lastConsumedEvent = lastConsumedEvent_;
        out.retained = retained_;
        out.active = active_;
        out.issued = issued_;
        out.adopted = adopted_;
        out.inFlight = inFlight_;
        out.sourceFaultEvents = sourceFaultEvents_;
        return out;
    }

    void restoreTranslationState(const TranslationState &state) {
        progress_ = state.progress;
        liveness_ = state.liveness;
        haveSourceTimestamp_ = state.haveSourceTimestamp;
        transportConnected_ = state.transportConnected;
        sessionReady_ = state.sessionReady;
        readyWithoutAuthorization_ = state.readyWithoutAuthorization;
        sourceFaultObserved_ = state.sourceFaultObserved;
        searchInFlight_ = state.searchInFlight;
        prepareState_ = state.prepareState;
        attemptState_ = state.attemptState;
        finalizerState_ = state.finalizerState;
        sourceSessionToken_ = state.sourceSessionToken;
        sourceAuthorizationToken_ = state.sourceAuthorizationToken;
        localSessionGeneration_ = state.localSessionGeneration;
        pendingSubmitGeneration_ = state.pendingSubmitGeneration;
        lastCompletedLocalSubmit_ = state.lastCompletedLocalSubmit;
        searchAttemptBaseline_ = state.searchAttemptBaseline;
        expectedSearchAttempts_ = state.expectedSearchAttempts;
        preparedJobGeneration_ = state.preparedJobGeneration;
        lastSourceEventToken_ = state.lastSourceEventToken;
        lastSourceEventMs_ = state.lastSourceEventMs;
        lastSourceSessionToken_ = state.lastSourceSessionToken;
        lastSourceAuthorizationToken_ = state.lastSourceAuthorizationToken;
        lastSourceJobToken_ = state.lastSourceJobToken;
        lastSourceOperationToken_ = state.lastSourceOperationToken;
        haveLastConsumedEvent_ = state.haveLastConsumedEvent;
        lastConsumedEvent_ = state.lastConsumedEvent;
        retained_ = state.retained;
        active_ = state.active;
        issued_ = state.issued;
        adopted_ = state.adopted;
        inFlight_ = state.inFlight;
        sourceFaultEvents_ = state.sourceFaultEvents;
    }

    bool closePrepare(bool aborted, PearlProgressReason reason) noexcept {
        if (!ownerMutationAllowed() ||
            prepareState_ != PrepareState::Started)
            return false;
        const uint64_t t = now();
        const PearlLivenessSnapshot live = liveness_.snapshot(t, deadlines_);
        if (!live.queryTimeValid ||
            live.jobGeneration != preparedJobGeneration_)
            return false;
        progress_.noteDrop(reason == PearlProgressReason::None
                               ? PearlProgressReason::PrepareFailed
                               : reason);
        prepareState_ =
            aborted ? PrepareState::Aborted : PrepareState::Failed;
        progress_.phase = aborted ? PearlProgressPhase::AwaitingJob
                                  : PearlProgressPhase::Failed;
        publishProgressAt(t);
        return true;
    }

    void resetAttemptAccounting() noexcept {
        attemptState_ = AttemptState::Idle;
        currentAttemptObserved_ = 0;
        currentAttemptRetained_ = 0;
        currentAttemptDropped_ = 0;
        currentAttemptSaturationReported_ = false;
        currentAttemptFinalizerStarted_ = false;
        currentAttemptFinalizerCompleted_ = false;
    }

    bool currentAttemptHitAccountingClosed() const noexcept {
        if (currentAttemptObserved_ == currentAttemptRetained_)
            return !currentAttemptSaturationReported_ &&
                   currentAttemptDropped_ == 0;
        return currentAttemptObserved_ > currentAttemptRetained_ &&
               currentAttemptSaturationReported_ &&
               currentAttemptDropped_ ==
                   currentAttemptObserved_ - currentAttemptRetained_;
    }

    bool currentAttemptClosed() const noexcept {
        return attemptState_ == AttemptState::Retained &&
               currentAttemptHitAccountingClosed() &&
               (currentAttemptRetained_ == 0 ||
                (currentAttemptFinalizerStarted_ &&
                 currentAttemptFinalizerCompleted_));
    }

    void appendProofBacklog() noexcept {
        const std::size_t tail =
            (proofBacklogHead_ + proofBacklogCount_) %
            kProofBacklogCapacity;
        proofBacklog_[tail].job = issued_;
        proofBacklog_[tail].searchGeneration = progress_.searchGeneration;
        proofBacklog_[tail].serializedOrdinal =
            ++serializedOrdinalForSearch_;
        ++proofBacklogCount_;
    }

    static uint64_t saturatingAdd(uint64_t value, std::size_t amount) {
        const uint64_t converted = static_cast<uint64_t>(amount);
        return converted > std::numeric_limits<uint64_t>::max() - value
                   ? std::numeric_limits<uint64_t>::max()
                   : value + converted;
    }

    void appendRawTerminals(
        const std::array<PearlSourceEvent,
                         kDrainBatch * kDrainPasses> &events,
        std::size_t count, bool trusted) noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            terminalEvidenceObserved_ =
                saturatingAdd(terminalEvidenceObserved_, 1);
            if (terminalEvidenceCount_ >=
                kPearlRawTerminalEvidenceCapacity) {
                terminalEvidenceDropped_ =
                    saturatingAdd(terminalEvidenceDropped_, 1);
                continue;
            }
            PearlRawTerminalEvidence &record =
                terminalEvidence_[terminalEvidenceCount_++];
            record.observation = terminalEvidenceObserved_;
            record.event = events[i];
            record.trust = trusted ? PearlRawTerminalTrust::Trusted
                                   : PearlRawTerminalTrust::Untrusted;
        }
    }

    static bool digestZero(const PearlJobIdentityDigest &digest) {
        for (uint8_t byte : digest.bytes)
            if (byte != 0) return false;
        return true;
    }

    static bool digestEqual(const PearlJobIdentityDigest &a,
                            const PearlJobIdentityDigest &b) {
        return a.bytes == b.bytes;
    }

    static bool nextToken(uint64_t previous, uint64_t candidate) {
        return previous != std::numeric_limits<uint64_t>::max() &&
               candidate == previous + 1;
    }

    static bool knownKind(PearlSourceEventKind value) {
        return value > PearlSourceEventKind::None &&
               value <= PearlSourceEventKind::SourceFault;
    }

    static bool knownDelivery(PearlSourceDelivery value) {
        return value <= PearlSourceDelivery::Sent;
    }

    static bool knownTerminal(PearlSourceTerminal value) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        return value <= PearlSourceTerminal::GuardRetired;
#else
        return value <= PearlSourceTerminal::GatewayQueued;
#endif
    }

    static bool knownReason(PearlSourceReason value) {
        return value <= PearlSourceReason::Shutdown;
    }

    static bool knownTerminalEvidence(PearlSourceTerminalEvidence value) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        return value <= PearlSourceTerminalEvidence::ConfirmedGuardNonForwarding;
#else
        return value <= PearlSourceTerminalEvidence::CorrelatedRemoteOutcome;
#endif
    }

    static bool noTerminalFields(const PearlSourceEvent &event) {
        return event.delivery == PearlSourceDelivery::None &&
               event.terminal == PearlSourceTerminal::None &&
               event.terminalEvidence ==
                   PearlSourceTerminalEvidence::None &&
               event.acknowledged == 0 && event.superseded == 0;
    }

    static bool validTerminalTuple(const PearlSourceEvent &event) {
        return pearlSourceTerminalFactValid(
            event.delivery, event.terminal, event.acknowledged != 0,
            event.superseded != 0, event.reason,
            event.terminalEvidence);
    }

    bool validAlgorithmEvent(PearlAlgorithmProgressEvent event,
                             uint64_t amount) const {
        if (event > PearlAlgorithmProgressEvent::CandidateDropped)
            return false;
        if (amount == 0 &&
            event != PearlAlgorithmProgressEvent::HitsObserved &&
            event != PearlAlgorithmProgressEvent::HitsRetained)
            return false;
        switch (event) {
            case PearlAlgorithmProgressEvent::AttemptStarted:
                return amount == 1 && searchInFlight_ &&
                       finalizerState_ == FinalizerState::Idle &&
                       (attemptState_ == AttemptState::Idle ||
                        (attemptState_ == AttemptState::Retained &&
                         currentAttemptClosed()));
            case PearlAlgorithmProgressEvent::AttemptCompleted:
                return amount == 1 && searchInFlight_ &&
                       attemptState_ == AttemptState::Started &&
                       progress_.attemptsCompleted < progress_.attemptsStarted;
            case PearlAlgorithmProgressEvent::HitsObserved:
                return searchInFlight_ &&
                       attemptState_ == AttemptState::Completed;
            case PearlAlgorithmProgressEvent::HitsRetained:
                return searchInFlight_ &&
                       attemptState_ == AttemptState::Observed &&
                       amount <= currentAttemptObserved_;
            case PearlAlgorithmProgressEvent::HitBufferSaturated:
                return searchInFlight_ &&
                       attemptState_ == AttemptState::Retained &&
                       !currentAttemptSaturationReported_ &&
                       currentAttemptObserved_ > currentAttemptRetained_ &&
                       amount == currentAttemptObserved_ -
                                     currentAttemptRetained_;
            case PearlAlgorithmProgressEvent::FinalizerStarted:
                return amount == 1 && searchInFlight_ &&
                       attemptState_ == AttemptState::Retained &&
                       currentAttemptHitAccountingClosed() &&
                       currentAttemptRetained_ != 0 &&
                       !currentAttemptFinalizerStarted_ &&
                       finalizerState_ == FinalizerState::Idle &&
                       progress_.finalizersStarted <
                           progress_.attemptsCompleted &&
                       progress_.finalizersStarted < progress_.hitsRetained;
            case PearlAlgorithmProgressEvent::CandidateSelected:
                return amount == 1 && searchInFlight_ &&
                       finalizerState_ == FinalizerState::Started &&
                       progress_.candidatesSelected <
                           progress_.finalizersStarted &&
                       progress_.candidatesSelected < progress_.hitsRetained;
            case PearlAlgorithmProgressEvent::FinalizerProgress:
                return amount == 1 && searchInFlight_ &&
                       finalizerState_ != FinalizerState::Idle;
            case PearlAlgorithmProgressEvent::HostValid:
            case PearlAlgorithmProgressEvent::HostInvalid:
                return amount == 1 && searchInFlight_ &&
                       finalizerState_ == FinalizerState::CandidateSelected &&
                       progress_.hostChecked < progress_.candidatesSelected;
            case PearlAlgorithmProgressEvent::ProofSerialized:
                return amount == 1 && searchInFlight_ &&
                       finalizerState_ == FinalizerState::HostValid &&
                       proofBacklogCount_ < kProofBacklogCapacity &&
                       progress_.proofsSerialized < progress_.hostValid;
            case PearlAlgorithmProgressEvent::CandidateDropped:
                return amount == 1 && searchInFlight_ &&
                       finalizerState_ != FinalizerState::Idle;
        }
        return false;
    }

    bool consumeEvent(const PearlSourceEvent &event) noexcept {
        if (!knownKind(event.kind) || !knownDelivery(event.delivery) ||
            !knownTerminal(event.terminal) || !knownReason(event.reason) ||
            !knownTerminalEvidence(event.terminalEvidence) ||
            event.acknowledged > 1 || event.superseded > 1 ||
            event.reserved1 != 0 ||
            !nextToken(lastSourceEventToken_, event.eventToken) ||
            (haveSourceTimestamp_ && event.timestampMs < lastSourceEventMs_))
            return false;

        bool accepted = false;
        switch (event.kind) {
            case PearlSourceEventKind::SessionStarted:
                accepted = consumeSessionStarted(event);
                break;
            case PearlSourceEventKind::TransportConnected:
                accepted = consumeTransportConnected(event);
                break;
            case PearlSourceEventKind::AuthorizationSucceeded:
                accepted = consumeAuthorization(event);
                break;
            case PearlSourceEventKind::SessionReadyNoAuth:
                accepted = consumeReadyNoAuth(event);
                break;
            case PearlSourceEventKind::JobRetainedPreAuth:
                accepted = consumeRetained(event);
                break;
            case PearlSourceEventKind::JobAvailable:
                accepted = consumeJobAvailable(event);
                break;
            case PearlSourceEventKind::JobRetired:
                accepted = consumeJobRetired(event);
                break;
            case PearlSourceEventKind::SubmitDispatched:
                accepted = consumeSubmitDispatched(event);
                break;
            case PearlSourceEventKind::SubmitTerminal:
                accepted = consumeSubmitTerminal(event);
                break;
            case PearlSourceEventKind::Disconnected:
                accepted = consumeDisconnected(event);
                break;
            case PearlSourceEventKind::SourceFault:
                accepted = consumeSourceFault(event);
                break;
            case PearlSourceEventKind::None:
                break;
        }
        if (!accepted) return false;
        lastSourceEventToken_ = event.eventToken;
        lastSourceEventMs_ = event.timestampMs;
        haveSourceTimestamp_ = true;
        lastConsumedEvent_ = event;
        haveLastConsumedEvent_ = true;
        return true;
    }

    bool consumeSessionStarted(const PearlSourceEvent &e) noexcept {
        if (sourceSessionToken_ != 0 || !nextToken(lastSourceSessionToken_, e.sessionToken) ||
            e.authorizationToken || e.jobToken || e.operationToken ||
            !digestZero(e.jobIdentity) || !noTerminalFields(e) ||
            e.reason != PearlSourceReason::None)
            return false;
        const uint64_t local = liveness_.beginSession(e.timestampMs);
        if (!local) return false;
        lastSourceSessionToken_ = e.sessionToken;
        sourceSessionToken_ = e.sessionToken;
        localSessionGeneration_ = local;
        transportConnected_ = false;
        sessionReady_ = false;
        readyWithoutAuthorization_ = false;
        sourceAuthorizationToken_ = 0;
        retained_ = JobSlot();
        active_ = JobSlot();
        issued_ = JobSlot();
        adopted_ = JobSlot();
        inFlight_ = InFlightSlot();
        prepareState_ = PrepareState::Idle;
        preparedJobGeneration_ = 0;
        sourceFaultObserved_ = false;
        progress_.sessionGeneration = local;
        progress_.sessionStartedMs = e.timestampMs;
        progress_.phase = PearlProgressPhase::Connecting;
        return true;
    }

    bool consumeTransportConnected(const PearlSourceEvent &e) noexcept {
        if (e.sessionToken != sourceSessionToken_ || transportConnected_ ||
            e.authorizationToken || e.jobToken || e.operationToken ||
            !digestZero(e.jobIdentity) || !noTerminalFields(e) ||
            e.reason != PearlSourceReason::None)
            return false;
        transportConnected_ = true;
        progress_.phase = PearlProgressPhase::AwaitingAuthorization;
        return true;
    }

    bool consumeAuthorization(const PearlSourceEvent &e) noexcept {
        if (e.sessionToken != sourceSessionToken_ || !transportConnected_ ||
            sessionReady_ ||
            !nextToken(lastSourceAuthorizationToken_, e.authorizationToken) ||
            e.jobToken || e.operationToken || !digestZero(e.jobIdentity) ||
            !noTerminalFields(e) || e.reason != PearlSourceReason::None ||
            progress_.authorizationGeneration ==
                std::numeric_limits<uint64_t>::max() ||
            !liveness_.onAuthorization(localSessionGeneration_, e.timestampMs))
            return false;
        lastSourceAuthorizationToken_ = e.authorizationToken;
        sourceAuthorizationToken_ = e.authorizationToken;
        sessionReady_ = true;
        readyWithoutAuthorization_ = false;
        ++progress_.authorizationGeneration;
        progress_.authorizedMs = e.timestampMs;
        progress_.phase = PearlProgressPhase::AwaitingJob;
        return true;
    }

    bool consumeReadyNoAuth(const PearlSourceEvent &e) noexcept {
        if (e.sessionToken != sourceSessionToken_ || !transportConnected_ ||
            sessionReady_ || e.authorizationToken || e.jobToken ||
            e.operationToken || !digestZero(e.jobIdentity) ||
            !noTerminalFields(e) || e.reason != PearlSourceReason::None ||
            progress_.authorizationGeneration ==
                std::numeric_limits<uint64_t>::max() ||
            !liveness_.onAuthorization(localSessionGeneration_, e.timestampMs))
            return false;
        sessionReady_ = true;
        readyWithoutAuthorization_ = true;
        ++progress_.authorizationGeneration;
        progress_.authorizedMs = e.timestampMs;
        progress_.phase = PearlProgressPhase::AwaitingJob;
        return true;
    }

    bool consumeRetained(const PearlSourceEvent &e) noexcept {
        if (e.sessionToken != sourceSessionToken_ || !transportConnected_ ||
            sessionReady_ || retained_.present || active_.present ||
            e.authorizationToken || !nextToken(lastSourceJobToken_, e.jobToken) ||
            e.operationToken || digestZero(e.jobIdentity) ||
            !noTerminalFields(e) || e.reason != PearlSourceReason::None)
            return false;
        lastSourceJobToken_ = e.jobToken;
        retained_ = makeJob(e);
        return true;
    }

    bool consumeJobAvailable(const PearlSourceEvent &e) noexcept {
        if (e.sessionToken != sourceSessionToken_ || !sessionReady_ ||
            active_.present || e.operationToken || digestZero(e.jobIdentity) ||
            !noTerminalFields(e) || e.reason != PearlSourceReason::None ||
            !validAuthorization(e.authorizationToken))
            return false;
        if (retained_.present) {
            if (e.jobToken != retained_.token ||
                !digestEqual(e.jobIdentity, retained_.digest))
                return false;
        } else if (!nextToken(lastSourceJobToken_, e.jobToken)) {
            return false;
        } else {
            lastSourceJobToken_ = e.jobToken;
        }
        const JobSlot newActive = makeJob(e);
        // Availability is source ownership, not proof that this exact Job was
        // returned to the run loop.  Liveness adoption occurs only at the
        // mirrored issued-job boundary in onIssuedJobBound().
        active_ = newActive;
        retained_ = JobSlot();
        return true;
    }

    bool consumeJobRetired(const PearlSourceEvent &e) noexcept {
        if (e.sessionToken != sourceSessionToken_ || e.operationToken ||
            e.delivery != PearlSourceDelivery::None ||
            e.terminal != PearlSourceTerminal::None ||
            e.terminalEvidence != PearlSourceTerminalEvidence::None ||
            e.acknowledged ||
            e.superseded || e.reason == PearlSourceReason::None)
            return false;
        if (retained_.present && e.authorizationToken == 0 &&
            e.jobToken == retained_.token &&
            digestEqual(e.jobIdentity, retained_.digest)) {
            retained_ = JobSlot();
            return true;
        }
        if (!active_.present || e.authorizationToken != sourceAuthorizationToken_ ||
            e.jobToken != active_.token ||
            !digestEqual(e.jobIdentity, active_.digest))
            return false;
        // Retirement is likewise only a source fact.  The exact Job already
        // returned by fetch remains adopted until the run loop reports a
        // later successful binding or an explicit fetch failure.
        active_ = JobSlot();
        return true;
    }

    bool consumeSubmitDispatched(const PearlSourceEvent &e) noexcept {
        if (e.sessionToken != sourceSessionToken_ || !sessionReady_ ||
            !issued_.present || inFlight_.present ||
            pendingSubmitGeneration_ == 0 ||
            e.authorizationToken != issued_.authorization ||
            e.jobToken != issued_.token ||
            !digestEqual(e.jobIdentity, issued_.digest) ||
            !nextToken(lastSourceOperationToken_, e.operationToken) ||
            e.delivery != PearlSourceDelivery::NotSent ||
            e.terminal != PearlSourceTerminal::None ||
            e.terminalEvidence != PearlSourceTerminalEvidence::None ||
            e.reason != PearlSourceReason::None ||
            e.acknowledged || e.superseded)
            return false;
        lastSourceOperationToken_ = e.operationToken;
        inFlight_.present = true;
        inFlight_.session = e.sessionToken;
        inFlight_.authorization = e.authorizationToken;
        inFlight_.token = e.jobToken;
        inFlight_.digest = e.jobIdentity;
        inFlight_.operation = e.operationToken;
        inFlight_.localSubmit = pendingSubmitGeneration_;
        pendingSubmitGeneration_ = 0;
        progress_.phase = PearlProgressPhase::AwaitingAcknowledgement;
        return true;
    }

    bool consumeSubmitTerminal(const PearlSourceEvent &e) noexcept {
        if (!inFlight_.present || e.sessionToken != inFlight_.session ||
            e.authorizationToken != inFlight_.authorization ||
            e.jobToken != inFlight_.token ||
            e.operationToken != inFlight_.operation ||
            !digestEqual(e.jobIdentity, inFlight_.digest) ||
            !validTerminalTuple(e))
            return false;

        const uint64_t submit = inFlight_.localSubmit;
        bool mapped = false;
        switch (e.terminalEvidence) {
            case PearlSourceTerminalEvidence::LocalPreDeliveryFailure:
                mapped =
                    liveness_.onSubmissionFailed(submit, e.timestampMs);
                if (mapped) {
                    progress_.add(&progress_.submissionFailures);
                    progress_.noteDrop(
                        PearlProgressReason::SubmissionFailed);
                }
                break;
            case PearlSourceTerminalEvidence::PossiblyDeliveredUnknown:
                mapped = liveness_.onPossiblyDeliveredUnknown(
                    submit, e.timestampMs);
                // PearlProgressSnapshot predates this evidence class. Do not
                // put it in `submitted`; exact accounting remains in liveness.
                break;
            case PearlSourceTerminalEvidence::FullySentNoReply:
                mapped = liveness_.onDeliveryUnknown(submit, e.timestampMs);
                if (mapped) noteSentOutcome(e);
                break;
            case PearlSourceTerminalEvidence::GatewayAcknowledgementOnly:
                mapped =
                    liveness_.onAcknowledgement(submit, e.timestampMs, true);
                if (mapped) noteSentOutcome(e);
                break;
            case PearlSourceTerminalEvidence::AcknowledgedOutcomeUnavailable:
                mapped =
                    liveness_.onAcknowledgement(submit, e.timestampMs, false);
                if (mapped)
                    mapped = liveness_.onOutcomeUnavailable(
                        submit, e.timestampMs);
                if (mapped) noteSentOutcome(e);
                break;
            case PearlSourceTerminalEvidence::CorrelatedRemoteOutcome:
                mapped =
                    liveness_.onAcknowledgement(submit, e.timestampMs, false);
                if (mapped)
                    mapped = liveness_.onOutcome(
                        submit, terminalOutcome(e.terminal), e.timestampMs);
                if (mapped) noteSentOutcome(e);
                break;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
            case PearlSourceTerminalEvidence::ConfirmedGuardNonForwarding:
                mapped = liveness_.onGuardRetired(submit, e.timestampMs);
                if (mapped) noteSentOutcome(e);
                break;
#endif
            case PearlSourceTerminalEvidence::None:
                return false;
        }
        if (!mapped) return false;
        progress_.lastOutcome =
            liveness_.snapshot(e.timestampMs, deadlines_).lastOutcome;
        progress_.lastOutcomeMs = e.timestampMs;
        if (e.acknowledged) progress_.lastAcknowledgedMs = e.timestampMs;
        progress_.phase = adopted_.present ? PearlProgressPhase::Searching
                                           : PearlProgressPhase::AwaitingJob;
        lastCompletedLocalSubmit_ = submit;
        inFlight_ = InFlightSlot();
        return true;
    }

    bool consumeDisconnected(const PearlSourceEvent &e) noexcept {
        if (e.sessionToken != sourceSessionToken_ ||
            e.authorizationToken != sourceAuthorizationToken_ || e.jobToken ||
            e.operationToken || !digestZero(e.jobIdentity) ||
            !noTerminalFields(e) || e.reason == PearlSourceReason::None ||
            retained_.present || active_.present || inFlight_.present ||
            pendingSubmitGeneration_ != 0 || searchInFlight_ ||
            prepareState_ == PrepareState::Started ||
            prepareState_ == PrepareState::Completed)
            return false;
        const PearlLivenessSnapshot before =
            liveness_.snapshot(e.timestampMs, deadlines_);
        if (!before.queryTimeValid ||
            before.candidate != PearlCandidateState::Idle ||
            !liveness_.onDisconnect(localSessionGeneration_, e.timestampMs))
            return false;
        sourceSessionToken_ = 0;
        sourceAuthorizationToken_ = 0;
        transportConnected_ = false;
        sessionReady_ = false;
        readyWithoutAuthorization_ = false;
        sourceFaultObserved_ = false;
        issued_ = JobSlot();
        adopted_ = JobSlot();
        prepareState_ = PrepareState::Idle;
        preparedJobGeneration_ = 0;
        progress_.phase = PearlProgressPhase::Idle;
        return true;
    }

    bool consumeSourceFault(const PearlSourceEvent &e) noexcept {
        if (e.sessionToken != sourceSessionToken_ ||
            e.authorizationToken != sourceAuthorizationToken_ ||
            e.operationToken || e.reason == PearlSourceReason::None ||
            !noTerminalFields(e))
            return false;
        if (active_.present) {
            if (e.jobToken != active_.token ||
                !digestEqual(e.jobIdentity, active_.digest))
                return false;
        } else if (e.jobToken || !digestZero(e.jobIdentity)) {
            return false;
        }
        sourceFaultObserved_ = true;
        sourceFaultEvents_ = saturatingAdd(sourceFaultEvents_, 1);
        return true;
    }

    static PearlSubmitOutcome terminalOutcome(PearlSourceTerminal terminal) {
        switch (terminal) {
            case PearlSourceTerminal::Accepted:
                return PearlSubmitOutcome::Accepted;
            case PearlSourceTerminal::Rejected:
                return PearlSubmitOutcome::Rejected;
            case PearlSourceTerminal::Stale:
                return PearlSubmitOutcome::Stale;
            default:
                return PearlSubmitOutcome::Unknown;
        }
    }

    void noteSentOutcome(const PearlSourceEvent &e) {
        progress_.add(&progress_.submitted);
        if (e.acknowledged) progress_.add(&progress_.acknowledged);
        switch (e.terminal) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
            case PearlSourceTerminal::GuardRetired:
                progress_.add(&progress_.guardRetired);
                break;
#endif
            case PearlSourceTerminal::Accepted:
                progress_.add(&progress_.accepted);
                if (e.superseded)
                    progress_.add(&progress_.acceptedSuperseded);
                break;
            case PearlSourceTerminal::Rejected:
                progress_.add(&progress_.rejected);
                break;
            case PearlSourceTerminal::Stale:
                progress_.add(&progress_.stale);
                break;
            case PearlSourceTerminal::Unknown:
            case PearlSourceTerminal::GatewayQueued:
                progress_.add(&progress_.unknown);
                break;
            default:
                break;
        }
    }

    JobSlot makeJob(const PearlSourceEvent &e) const {
        JobSlot slot;
        slot.present = true;
        slot.session = e.sessionToken;
        slot.authorization = e.authorizationToken;
        slot.token = e.jobToken;
        slot.digest = e.jobIdentity;
        return slot;
    }

    bool validAuthorization(uint64_t token) const {
        return readyWithoutAuthorization_ ? token == 0
                                          : token != 0 &&
                                                token == sourceAuthorizationToken_;
    }

    bool validateSourceSnapshot(const PearlSourceObservabilitySnapshot &s,
                                std::size_t drainedNow) noexcept {
        bool changed = false;
        const uint8_t knownFaults =
            pearlSourceMailboxFaultBit(PearlSourceMailboxFault::Overflow) |
            pearlSourceMailboxFaultBit(PearlSourceMailboxFault::TokenExhaustion) |
            pearlSourceMailboxFaultBit(PearlSourceMailboxFault::TimestampRollback);

        if (!s.accountingHolds() || s.failClosed != (s.faultBits != 0)) {
            latch(PearlSourceIntegrityFlag::SnapshotAccountingInvalid);
            changed = true;
        }
        if ((s.faultBits & static_cast<uint8_t>(~knownFaults)) != 0) {
            latch(PearlSourceIntegrityFlag::UnknownMailboxFault);
            changed = true;
        }
        if (s.failClosed) {
            latch(PearlSourceIntegrityFlag::MailboxFailClosed);
            changed = true;
        }

        if (sourceBaselined_) {
            if (s.publishedEvents < lastMailboxPublished_ ||
                s.drainedEvents < lastMailboxDrained_ ||
                s.refusedEvents < lastMailboxRefused_ ||
                s.lastEventToken < lastMailboxEventToken_ ||
                s.lastSessionToken < lastMailboxSessionToken_ ||
                s.lastAuthorizationToken < lastMailboxAuthorizationToken_ ||
                s.lastJobToken < lastMailboxJobToken_ ||
                s.lastOperationToken < lastMailboxOperationToken_ ||
                s.lastTimestampMs < lastMailboxTimestampMs_ ||
                s.drainedEvents - lastMailboxDrained_ != drainedNow) {
                latch(PearlSourceIntegrityFlag::SnapshotRollback);
                changed = true;
            }
            if (s.refusedEvents > lastMailboxRefused_) {
                latch(PearlSourceIntegrityFlag::PublicationRefused);
                changed = true;
            }
        } else {
            if (s.drainedEvents != drainedNow) {
                latch(PearlSourceIntegrityFlag::SnapshotRollback);
                changed = true;
            }
            if (s.refusedEvents != 0) {
                latch(PearlSourceIntegrityFlag::PublicationRefused);
                changed = true;
            }
            sourceBaselined_ = true;
        }

        sourceRefusedEvents_ = s.refusedEvents;
        sourceMailboxFaultBits_ = s.faultBits;

        if (s.queued == 0) {
            if (s.lastEventToken != lastSourceEventToken_ ||
                (s.publishedEvents != 0 &&
                 s.lastTimestampMs != lastSourceEventMs_) ||
                s.lastSessionToken != lastSourceSessionToken_ ||
                s.lastAuthorizationToken != lastSourceAuthorizationToken_ ||
                s.lastJobToken != lastSourceJobToken_ ||
                s.lastOperationToken != lastSourceOperationToken_ ||
                s.hasLastPublishedEvent != haveLastConsumedEvent_ ||
                (haveLastConsumedEvent_ &&
                 !eventsEqual(s.lastPublishedEvent, lastConsumedEvent_)) ||
                !snapshotOwnershipMatches(s)) {
                latch(PearlSourceIntegrityFlag::SnapshotAccountingInvalid);
                changed = true;
            }
        } else if (s.lastEventToken < lastSourceEventToken_ ||
                   s.lastTimestampMs < lastSourceEventMs_) {
            latch(PearlSourceIntegrityFlag::SnapshotAccountingInvalid);
            changed = true;
        }

        lastMailboxPublished_ = s.publishedEvents;
        lastMailboxDrained_ = s.drainedEvents;
        lastMailboxRefused_ = s.refusedEvents;
        lastMailboxEventToken_ = s.lastEventToken;
        lastMailboxSessionToken_ = s.lastSessionToken;
        lastMailboxAuthorizationToken_ = s.lastAuthorizationToken;
        lastMailboxJobToken_ = s.lastJobToken;
        lastMailboxOperationToken_ = s.lastOperationToken;
        lastMailboxTimestampMs_ = s.lastTimestampMs;
        return changed;
    }

    bool snapshotOwnershipMatches(
        const PearlSourceObservabilitySnapshot &s) const {
        if (s.activeSessionToken != sourceSessionToken_ ||
            s.transportConnected != transportConnected_ ||
            s.activeAuthorizationToken != sourceAuthorizationToken_ ||
            s.sessionReady != sessionReady_ ||
            s.sessionReadyWithoutAuthorization != readyWithoutAuthorization_ ||
            s.sourceFaultObserved != sourceFaultObserved_)
            return false;
        if (!snapshotJobMatches(s.retainedPreAuthJobToken,
                                s.retainedPreAuthJobIdentity, retained_) ||
            !snapshotJobMatches(s.activeJobToken, s.activeJobIdentity, active_))
            return false;
        if (s.issuedJobBound != issued_.present ||
            (issued_.present &&
             (s.issuedSessionToken != issued_.session ||
              s.issuedAuthorizationToken != issued_.authorization ||
              s.issuedJobToken != issued_.token ||
              !digestEqual(s.issuedJobIdentity, issued_.digest))))
            return false;
        if (s.submitInFlight != inFlight_.present ||
            (inFlight_.present &&
             (s.inFlightOperationToken != inFlight_.operation ||
              s.inFlightAuthorizationToken != inFlight_.authorization ||
              s.inFlightJobToken != inFlight_.token ||
              !digestEqual(s.inFlightJobIdentity, inFlight_.digest))))
            return false;
        return true;
    }

    static bool snapshotJobMatches(uint64_t token,
                                   const PearlJobIdentityDigest &digest,
                                   const JobSlot &slot) {
        return slot.present ? token == slot.token && digestEqual(digest, slot.digest)
                            : token == 0 && digestZero(digest);
    }

    static bool eventsEqual(const PearlSourceEvent &a,
                            const PearlSourceEvent &b) {
        return a.eventToken == b.eventToken &&
               a.timestampMs == b.timestampMs &&
               a.sessionToken == b.sessionToken &&
               a.authorizationToken == b.authorizationToken &&
               a.jobToken == b.jobToken &&
               a.operationToken == b.operationToken &&
               digestEqual(a.jobIdentity, b.jobIdentity) && a.kind == b.kind &&
               a.delivery == b.delivery && a.terminal == b.terminal &&
               a.reason == b.reason &&
               a.terminalEvidence == b.terminalEvidence &&
               a.acknowledged == b.acknowledged &&
               a.superseded == b.superseded &&
               a.reserved1 == b.reserved1;
    }

    void latch(PearlSourceIntegrityFlag flag,
               bool invalidEvent = false) noexcept {
        sourceIntegrityFlags_ |= pearlSourceIntegrityBit(flag);
        if (invalidEvent)
            sourceEventsInvalid_ = saturatingAdd(sourceEventsInvalid_, 1);
        quarantined_ = true;
    }

    void noteWrongThread() noexcept {
        pendingIntegrity_.fetch_or(
            pearlSourceIntegrityBit(PearlSourceIntegrityFlag::WrongThread),
            std::memory_order_relaxed);
    }

    bool absorbPendingIntegrity() noexcept {
        const uint64_t pending =
            pendingIntegrity_.exchange(0, std::memory_order_relaxed);
        if (!pending) return false;
        sourceIntegrityFlags_ |= pending;
        quarantined_ = true;
        return true;
    }

    bool ownerMutationAllowed() noexcept {
        if (!onOwnerThread()) {
            noteWrongThread();
            return false;
        }
        if (absorbPendingIntegrity()) publishProgress();
        return !quarantined_;
    }

    bool onOwnerThread() const noexcept {
        return std::this_thread::get_id() == owner_;
    }

    uint64_t now() const noexcept {
        return clock_ ? clock_(clockContext_) : 0;
    }

    void publishProgressAt(uint64_t timestampMs) noexcept {
        progress_.publishedAtMs = timestampMs;
        publishProgress();
    }

    void publishProgress() noexcept {
        publishedIntegrityFlags_.store(sourceIntegrityFlags_,
                                       std::memory_order_release);
        if (bus_) bus_->publish(progress_);
    }

    PearlProgressBus *bus_ = nullptr;
    Clock clock_ = nullptr;
    void *clockContext_ = nullptr;
    PearlProgressSnapshot progress_;
    PearlLivenessDeadlines deadlines_;
    uint64_t jobGenerationLimit_ = std::numeric_limits<uint64_t>::max();
    PearlLivenessTracker liveness_;
    PearlSourceObservability *source_ = nullptr;
    const std::thread::id owner_;
    std::atomic<uint64_t> pendingIntegrity_{0};
    std::atomic<uint64_t> publishedIntegrityFlags_{0};

    bool quarantined_ = false;
    bool algorithmGenerationExhausted_ = false;
    bool haveSourceTimestamp_ = false;
    bool sourceBaselined_ = false;
    bool transportConnected_ = false;
    bool sessionReady_ = false;
    bool readyWithoutAuthorization_ = false;
    bool sourceFaultObserved_ = false;
    bool deferredSearchOwnership_ = false;
    bool searchCandidateAllowed(PearlCandidateState state) const noexcept {
        return state==PearlCandidateState::Idle ||
            (deferredSearchOwnership_ && inFlight_.present &&
             state==PearlCandidateState::AwaitingAcknowledgement);
    }
    bool searchInFlight_ = false;
    PrepareState prepareState_ = PrepareState::Idle;
    AttemptState attemptState_ = AttemptState::Idle;
    FinalizerState finalizerState_ = FinalizerState::Idle;
    bool draining_ = false;
    uint64_t sourceSessionToken_ = 0;
    uint64_t sourceAuthorizationToken_ = 0;
    uint64_t localSessionGeneration_ = 0;
    uint64_t pendingSubmitGeneration_ = 0;
    uint64_t lastCompletedLocalSubmit_ = 0;
    uint64_t searchAttemptBaseline_ = 0;
    uint64_t expectedSearchAttempts_ = 0;
    uint64_t serializedOrdinalForSearch_ = 0;
    uint64_t preparedJobGeneration_ = 0;
    uint64_t currentAttemptObserved_ = 0;
    uint64_t currentAttemptRetained_ = 0;
    uint64_t currentAttemptDropped_ = 0;
    bool currentAttemptSaturationReported_ = false;
    bool currentAttemptFinalizerStarted_ = false;
    bool currentAttemptFinalizerCompleted_ = false;
    uint64_t lastSourceEventToken_ = 0;
    uint64_t lastSourceEventMs_ = 0;
    uint64_t lastSourceSessionToken_ = 0;
    uint64_t lastSourceAuthorizationToken_ = 0;
    uint64_t lastSourceJobToken_ = 0;
    uint64_t lastSourceOperationToken_ = 0;
    bool haveLastConsumedEvent_ = false;
    PearlSourceEvent lastConsumedEvent_;
    JobSlot retained_;
    JobSlot active_;
    JobSlot issued_;
    JobSlot adopted_;
    JobSlot lastPrepared_;
    uint64_t lastPreparedJobGeneration_ = 0;
    InFlightSlot inFlight_;
    std::array<ProofRecord, kProofBacklogCapacity> proofBacklog_{};
    JobSlot lastSearchJob_;
    uint64_t lastRetiredDisconnectedSearchGeneration_ = 0;
    std::size_t proofBacklogHead_ = 0;
    std::size_t proofBacklogCount_ = 0;

    uint64_t sourceEventsDrained_ = 0;
    uint64_t sourceEventsInvalid_ = 0;
    uint64_t sourceRefusedEvents_ = 0;
    uint64_t sourceMailboxFaultBits_ = 0;
    uint64_t sourceIntegrityFlags_ = 0;
    uint64_t sourceQueueHighWater_ = 0;
    uint64_t sourceFaultEvents_ = 0;
    uint64_t terminalEvidenceObserved_ = 0;
    uint64_t terminalEvidenceDropped_ = 0;
    uint64_t terminalEvidenceConsumed_ = 0;
    std::size_t terminalEvidenceCount_ = 0;
    std::array<PearlRawTerminalEvidence,
               kPearlRawTerminalEvidenceCapacity>
        terminalEvidence_{};
    uint64_t lastMailboxPublished_ = 0;
    uint64_t lastMailboxDrained_ = 0;
    uint64_t lastMailboxRefused_ = 0;
    uint64_t lastMailboxEventToken_ = 0;
    uint64_t lastMailboxSessionToken_ = 0;
    uint64_t lastMailboxAuthorizationToken_ = 0;
    uint64_t lastMailboxJobToken_ = 0;
    uint64_t lastMailboxOperationToken_ = 0;
    uint64_t lastMailboxTimestampMs_ = 0;
};

}  // namespace om
