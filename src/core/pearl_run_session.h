// Worker-owned canonical live dispatch. No GPU or socket work in construction.
#pragma once

#include "pearl_pool.h"
#include "pearl_proof_binding.h"
#include "pearl_proof_ownership.h"
#include "pearl_run_progress_coordinator.h"
#include "pearl_local_retirement.h"
#include "pearl_proof_timing.h"

namespace om {

struct PearlRunFinalEvidence {
    PearlPoolSubmitCounters pool;
    PearlRunProgressCoordinatorSnapshot owner;
    PearlSourceObservabilitySnapshot source;
    PearlEvidenceDigest wire;
    bool complete=false;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    guard_retirement_v2::NaturalFinal guard;
#endif
};

class PearlRunSession final : public PearlProofBindingSink {
 public:
    using Clock = PearlRunProgressCoordinator::Clock;
    static constexpr uint64_t maximumAttempts =
        PearlRunProgressCoordinator::kProofBacklogCapacity;

    PearlRunSession(Algorithm &algorithm, PearlPoolSource &source,
                    PearlSourcePolicy policy, Clock clock, void *clockContext,
                    PearlLocalRetirementSink *localRetirements = nullptr)
        : algorithm_(algorithm), source_(source), policy_(policy),
          coordinator_(&bus_, clock, clockContext), localRetirements_(localRetirements) {}
    PearlRunSession(const PearlRunSession &) = delete;
    PearlRunSession &operator=(const PearlRunSession &) = delete;
    ~PearlRunSession() {
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
        // Retire a fully sent unresolved request before detaching its observer.
        if (deferredSearchOwnership_ && source_.hasDeferredSubmit()) {
            try { std::string error; source_.cancelDeferredSubmit(&error); }
            catch (...) { failed_=true; }
        }
#endif
        // Borrowed callbacks must never outlive this object's ledger/sink.
        source_.setPublicationCallback(nullptr, nullptr);
        try { algorithm_.setPearlProofBindingSink(nullptr); } catch (...) {}
        try { algorithm_.setPearlProgressSink(nullptr); } catch (...) {}
        coordinator_.detachSource();
    }

    bool attach() noexcept {
        if (attached_ || failed_ || policy_.network != PearlNetwork::Mainnet ||
            !pearlKnownTargetSemantics(policy_.targetSemantics) ||
            algorithm_.pearlRuntimeCapability() !=
                PearlRuntimeCapability::CanonicalCudaPoolV1)
            return fail();
        try {
            if (!coordinator_.attachSource(source_.pearlSourceObservability()) ||
                !algorithm_.setPearlProgressSink(&coordinator_) ||
                !algorithm_.setPearlProofBindingSink(this)) return fail();
            source_.setPublicationCallback(&publication, this);
            attached_ = true;
            return healthy();
        } catch (...) { return fail(); }
    }

#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
    bool enableDeferredSearchOwnership() noexcept {
        if (attached_ || failed_ || deferredSearchOwnership_) return false;
        try {
            if (!coordinator_.enableDeferredSearchOwnership() ||
                !source_.enableDeferredSearchOwnership()) return fail();
            deferredSearchOwnership_=true;
            return true;
        } catch (...) { return fail(); }
    }
    bool hasDeferredSubmission() const noexcept { return source_.hasDeferredSubmit(); }
    bool pollDeferredSubmission(bool cancel=false) noexcept {
        if (!deferredSearchOwnership_ || !healthy() || bindingEnabled_) return fail();
        try {
            std::string error;
            if (cancel) source_.cancelDeferredSubmit(&error);
            else source_.pollDeferredSubmit(&error);
            coordinator_.drainCheckpoint();
            PearlRawTerminalEvidence terminal;
            while(coordinator_.consumeTerminalEvidence(&terminal,1)) {
                if(terminal.trust!=PearlRawTerminalTrust::Trusted ||
                   terminal.event.delivery!=PearlSourceDelivery::Sent) return fail();
            }
            return reconcileDisconnectedSearch() && healthy();
        } catch (...) { return fail(); }
    }
    bool submitDeferred(const Job &job,const Solution &solution,std::string *error,
                        bool *delivered,bool *hostValid=nullptr) noexcept {
        if(!deferredSearchOwnership_ || source_.hasDeferredSubmit())return fail();
        return submitImpl(job,solution,error,delivered,hostValid,true);
    }
#endif

    bool healthy() noexcept {
        return !evidenceFinalizing_ && integrityHealthy();
    }
    PearlRunProgressCoordinatorSnapshot snapshot() noexcept {
        return coordinator_.snapshotForOwner();
    }
    bool retiredAfterDisconnect() const noexcept { return disconnectedSearch_; }
    bool localRetirementFailed() const noexcept { return localRetirementFailed_; }
    bool armGuardNaturalStop(const char *runId,unsigned target,unsigned seconds,unsigned minimum) noexcept {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        auto *control=source_.guardRetirementSession();
        return !control || coordinator_.armGuardNaturalStop(*control,runId,target,seconds,minimum);
#else
        (void)runId; (void)target; (void)seconds; (void)minimum; return true;
#endif
    }
    bool armGuardProduction(const char *runId) noexcept {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        auto *control=source_.guardRetirementSession();
        return control && coordinator_.armGuardProduction(*control,runId);
#else
        (void)runId; return false;
#endif
    }
    bool startGuardDuration(int64_t startNs) noexcept {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        return coordinator_.startGuardDuration(startNs);
#else
        (void)startNs; return true;
#endif
    }
    bool finishEvidenceRun(uint64_t localRecords,PearlRunFinalEvidence *out,int64_t stopNs=0) noexcept {
        try {
        if (!out || overlapsSelf(out,sizeof(*out))) return fail();
        *out={};
        if (!healthy() || searchOpen_ || bindingEnabled_ || issuedBound_ ||
            ledger_.searchOpen() || ledger_.activeCount()) return fail();
        // finishSearch released the owner's issued slot, while the source
        // retains its binding until fetch/close. Diagnostic close aligns that
        // non-event binding before its first publication; only then drain and
        // perform the unchanged strict source/owner snapshot comparison.
        if (!coordinator_.evidenceQuiescent()) return fail();
        evidenceFinalizing_=true;
        struct EndFinalizing { bool &active; ~EndFinalizing() { active=false; } } guard{evidenceFinalizing_};
        PearlRunFinalEvidence final;
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        if (auto *control=source_.guardRetirementSession()) {
            const auto &p=source_.submitCounters();
            const guard_retirement_v2::Totals totals{p.submitted,p.accepted,p.rejected,p.stale,p.unknown,p.guardRetired};
            if (!p.conserved() || p.pending() || !coordinator_.finishGuardNaturalStop(totals,stopNs)) return fail();
            final.guard=control->naturalFinal();
        }
#else
        (void)stopNs;
#endif
        if (!source_.finishDiagnosticRun(&final.wire)) return fail();
        coordinator_.drainCheckpoint();
        final.owner=coordinator_.snapshotForOwner();
        final.source=source_.pearlSourceObservability()->snapshot();
        final.pool=source_.submitCounters();
        const auto &s=final.source; const auto &o=final.owner;
        if (!integrityHealthy() || !coordinator_.evidenceQuiescent() ||
            !s.accountingHolds() || s.queued || s.failClosed || s.faultBits || s.refusedEvents ||
            s.transportConnected || s.activeSessionToken || s.issuedJobBound || s.submitInFlight ||
            o.sourceIntegrityFlags || o.sourceEventsInvalid || o.sourceRefusedEvents ||
            o.sourceMailboxFaultBits || o.sourceQuarantined || o.terminalEvidenceDropped ||
            o.terminalEvidenceCount || o.terminalEvidenceObserved!=o.terminalEvidenceConsumed ||
            !o.progress.invariantsHold() || !o.liveness.submissionAccountingHolds() ||
            !o.liveness.queryTimeValid || o.liveness.connection!=PearlConnectionState::Disconnected ||
            !final.pool.conserved() || final.pool.pending() || o.progress.pending ||
            localRecords!=o.progress.submissionFailures || localRecords!=o.liveness.submissionFailures ||
            final.pool.submitted!=o.progress.submitted || final.pool.acknowledged!=o.progress.acknowledged ||
            final.pool.accepted!=o.progress.accepted || final.pool.rejected!=o.progress.rejected ||
            final.pool.stale!=o.progress.stale || final.pool.unknown!=o.progress.unknown ||
            final.pool.acceptedSuperseded!=o.progress.acceptedSuperseded) return fail();
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        if (final.pool.guardRetired!=o.progress.guardRetired || final.pool.guardRetired!=o.liveness.guardRetired ||
            (final.pool.guardRetired && !final.guard.complete)) return fail();
#endif
        final.complete=true; evidenceFinished_=true; *out=final; return true;
        } catch (...) { return fail(); }
    }

    // false = fatal integrity failure; true + !fetched = ordinary no-job retry.
    bool fetch(Job *job, bool *fetched) noexcept {
        if ((job && overlapsSelf(job, sizeof(*job))) ||
            (fetched && (overlapsSelf(fetched, sizeof(*fetched)) ||
                         (job && overlapsJob(fetched, sizeof(*fetched), *job)))))
            return fail();
        if (fetched) *fetched = false;
        if (!job || !fetched || !healthy() || issuedBound_ || searchOpen_)
            return fail();
        try {
            *job = Job{};
            const bool got =
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
                deferredSearchOwnership_ ? source_.fetchDeferred(job) :
#endif
                source_.fetch(job);
            if (!got) {
                coordinator_.drainCheckpoint();
                if (!healthy()) return fail();
                const bool disconnected = coordinator_.snapshotForOwner().liveness.connection ==
                    PearlConnectionState::Disconnected;
                if (!disconnected && adopted_ && !coordinator_.onIssuedJobFetchFailed())
                    return fail();
                adopted_ = false;
                hasPreparedCache_ = false;
                return true;
            }
            PearlPoolCanonicalIdentity canonical;
            const auto state = source_.pearlSourceObservability()->snapshot();
            if (!state.issuedJobBound || state.failClosed || state.queued ||
                !pearlPoolCanonicalIdentityForJob(*job, policy_, &canonical) ||
                canonical.sourceDigest.bytes != state.issuedJobIdentity.bytes)
                return fail();
            issued_ = {state.issuedSessionToken, state.issuedAuthorizationToken,
                       state.issuedJobToken, state.issuedJobIdentity};
            if (!pearlProofJobIdentityValid(issued_) ||
                !coordinator_.onIssuedJobBound(issued_.session, issued_.job,
                                               issued_.digest)) return fail();
            coordinator_.drainCheckpoint();
            adopted_ = issuedBound_ = true;
            prepared_ = false;
            *fetched = true;
            return healthy();
        } catch (...) { return fail(); }
    }

    bool prepare(const Job &job) noexcept {
        if (!healthy() || !issuedBound_ || prepared_ || searchOpen_ ||
            !matches(job)) return fail();
        // Rebinding the same issued source identity does not invalidate CUDA's
        // durable prepared B matrix. A transport/session replacement still
        // requires a fresh preparation boundary even if its header matches.
        if (hasPreparedCache_ && pearlProofJobIdentityEqual(issued_, preparedIdentity_)) {
            if (!coordinator_.onPrepareReused(issued_.session, issued_.authorization,
                                             issued_.job, issued_.digest)) return fail();
            prepared_ = true;
            return healthy();
        }
        if (!coordinator_.onPrepareStarted()) return fail();
        hasPreparedCache_ = false;
        try {
            if (!algorithm_.prepare(job)) {
                coordinator_.onPrepareFailed();
                return fail();
            }
            if (!coordinator_.onPrepareCompleted()) return fail();
            preparedIdentity_ = issued_;
            hasPreparedCache_ = true;
            prepared_ = true;
            return healthy();
        } catch (...) {
            coordinator_.onPrepareFailed();
            return fail();
        }
    }

    bool search(const Job &job, uint64_t nonce, uint64_t work,
                std::vector<Solution> *solutions) noexcept {
        if (!solutions || !healthy() || !prepared_ || searchOpen_ || !matches(job))
            return fail();
        solutions->clear();
        uint64_t units = 0;
        try { units = algorithm_.workUnitsPerNonce(); }
        catch (...) { return fail(); }
        if (!units || !work || work % units || work / units > maximumAttempts ||
            !coordinator_.onSearchStarted(work / units)) return fail();
        searchGeneration_ = coordinator_.snapshotForOwner().progress.searchGeneration;
        if (!ledger_.beginSearch(issued_, searchGeneration_, work / units)) return fail();
        searchOpen_ = true;
        disconnectedSearch_ = false;
        dispatchOrdinal_ = 0;
        boundCount_ = 0;
        const uint64_t serializedBefore = coordinator_.snapshotForOwner().progress.proofsSerialized;
        bindingEnabled_ = true;
        bool searched = false;
        try { searched = algorithm_.search(job, nonce, work, solutions); }
        catch (...) { searched = false; }
        bindingEnabled_ = false;
        if (!coordinator_.onSearchCompleted(searched) || !searched || !healthy() ||
            solutions->size() != boundCount_ || ledger_.activeCount() != boundCount_) {
            solutions->clear();
            return fail();
        }
        const uint64_t serializedAfter = coordinator_.snapshotForOwner().progress.proofsSerialized;
        if (serializedAfter < serializedBefore || serializedAfter - serializedBefore != boundCount_) {
            solutions->clear();
            return fail();
        }
        for (std::size_t i = 0; i < solutions->size(); ++i) {
            PearlSerializedProofMetadata returned;
            if ((*solutions)[i].pearlProofToken != boundProofs_[i].token ||
                !pearlSerializedProofMetadataFromSolution((*solutions)[i], &returned) ||
                !pearlSerializedProofMetadataEqual(returned, boundProofs_[i].proof)) {
                solutions->clear();
                return fail();
            }
        }
        return true;
    }

    bool bindSerializedProof(const Job &job, Solution *solution) noexcept override {
        if (!solution || solution->pearlProofToken || !bindingEnabled_ ||
            !searchOpen_ || !healthy() || !matches(job)) return fail();
        PearlSerializedProofMetadata metadata;
        if (boundCount_ >= boundProofs_.size() ||
            !pearlSerializedProofMetadataFromSolution(*solution, &metadata) ||
            !ledger_.bind(issued_, searchGeneration_, metadata,
                          &solution->pearlProofToken)) return fail();
        boundProofs_[boundCount_] = {solution->pearlProofToken, metadata};
        ++boundCount_;
        return true;
    }

    // This is the sole live path from returned proof to source submission.
    // Delivery is not acceptance; correlated remote outcomes own that fact.
    bool submit(const Job &job, const Solution &solution, std::string *error,
                bool *delivered, bool *hostValid = nullptr) noexcept {
        return submitImpl(job,solution,error,delivered,hostValid,false);
    }

 private:
    bool submitImpl(const Job &job,const Solution &solution,std::string *error,
                    bool *delivered,bool *hostValid,bool deferred) noexcept {
        // Validate output storage before clearing it. In particular, using
        // job.valid as an output must not mutate the authority being checked.
        const auto badOutput = [&](const void *p, std::size_t size) {
            return p && (overlapsSelf(p, size) || overlapsJob(p, size, job) ||
                pearlProofMemoryRangesOverlap(p, size, &solution, sizeof(solution)) ||
                pearlProofMemoryRangesOverlap(p, size, solution.extra.c_str(),
                                              solution.extra.size() + 1));
        };
        if (badOutput(error, error ? sizeof(*error) : 0) ||
            badOutput(delivered, sizeof(bool)) || badOutput(hostValid, sizeof(bool)) ||
            (delivered && hostValid && delivered == hostValid) ||
            (error && (pearlProofMemoryRangesOverlap(delivered, sizeof(bool), error, sizeof(*error)) ||
                       pearlProofMemoryRangesOverlap(hostValid, sizeof(bool), error, sizeof(*error)) ||
                       pearlProofMemoryRangesOverlap(delivered, sizeof(bool), error->c_str(), error->size() + 1) ||
                       pearlProofMemoryRangesOverlap(hostValid, sizeof(bool), error->c_str(), error->size() + 1))))
            return fail();
        if (delivered) *delivered = false;
        if (hostValid) *hostValid = false;
        if (!error || !delivered || error == &job.extra || error == &job.jobId ||
            error == &solution.extra || !healthy() || !searchOpen_ ||
            bindingEnabled_ || disconnectedSearch_ || !matches(job)) return fail();
        PearlSerializedProofMetadata metadata;
        PearlProofBinding binding;
        if (dispatchOrdinal_ >= boundCount_ ||
            solution.pearlProofToken != boundProofs_[dispatchOrdinal_].token ||
            !pearlSerializedProofMetadataFromSolution(solution, &metadata) ||
            !ledger_.consume(solution.pearlProofToken, issued_, searchGeneration_,
                              metadata, &binding)) return fail();
        const uint64_t candidate = coordinator_.onCandidateObserved(
            issued_.session, issued_.authorization, issued_.job, issued_.digest,
            searchGeneration_, ++dispatchOrdinal_);
        if (!candidate) return fail();
        try {
            bool verified = false;
            {
                pearl::proof_timing::HostVerifyScope timing(
                    std::getenv("CANARY_PEARL_PROOF_TIMING"), solution.nonce, solution.pearlProofToken);
                verified = algorithm_.verify(job, solution);
                timing.boundary();
                timing.finish();
            }
            if (!coordinator_.onCandidateFinalized(candidate, verified)) return fail();
            if (hostValid) *hostValid = verified;
            if (!verified) { *error = "candidate failed host verification"; return healthy(); }
            const uint64_t submitGeneration = coordinator_.onSubmitAttempt();
            if (!submitGeneration) return fail();
            const uint64_t previousOperation = localRetirements_
                ? source_.pearlSourceObservability()->snapshot().lastOperationToken : 0;
            if (localRetirements_ && previousOperation == UINT64_MAX) return failLocalRetirement();
            *delivered =
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
                deferred ? source_.beginDeferredSubmit(job,solution,error) :
#endif
                source_.submit(job, solution, error);
            coordinator_.drainCheckpoint();
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
            if(deferred) {
                if(!coordinator_.onDeferredSubmitReturned(submitGeneration,*delivered))return fail();
            } else
#else
            (void)deferred;
#endif
            if (!coordinator_.onSubmitReturned(submitGeneration, *delivered)) return fail();
            // Bounded retained diagnostic history is explicitly consumed by
            // this owner; cumulative outcomes remain in the progress snapshot.
            PearlRawTerminalEvidence terminal;
            std::size_t localCount = 0;
            while (coordinator_.consumeTerminalEvidence(&terminal, 1)) {
                if (terminal.trust != PearlRawTerminalTrust::Trusted) return fail();
                if (localRetirements_ && terminal.event.terminal == PearlSourceTerminal::LocalFailure) {
                    PearlLocalRetirement record{terminal, binding, candidate, submitGeneration,
                                                dispatchOrdinal_, previousOperation + 1};
                    if (*delivered || ++localCount != 1 || !pearlLocalRetirementValid(record))
                        return failLocalRetirement();
                    retainingLocal_ = true;
                    const bool retained = localRetirements_->retain(record);
                    retainingLocal_ = false;
                    if (!retained || failed_) return failLocalRetirement();
                }
            }
            // Never invent a typed local reason for a bare false return. Partial
            // delivery is a separate Unknown terminal, not local retirement.
            // Its existing counters/terminal remain authoritative.
            return reconcileDisconnectedSearch() && healthy();
        } catch (...) { return fail(); }
    }

 public:
    bool finishSearch() noexcept {
        if (healthy() && searchOpen_ && disconnectedSearch_ && !bindingEnabled_) {
            if (ledger_.searchOpen() || ledger_.activeCount()) return fail();
            searchOpen_ = prepared_ = false;
            return healthy();
        }
        if (!healthy() || !searchOpen_ || bindingEnabled_ ||
            dispatchOrdinal_ != boundCount_ ||
            !ledger_.reconcileSearch(issued_, searchGeneration_) ||
            !coordinator_.onIssuedJobReleased(issued_.session, issued_.job))
            return fail();
        searchOpen_ = issuedBound_ = prepared_ = false;
#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
        if(deferredSearchOwnership_ && !source_.releaseDeferredSearchBinding())return fail();
#endif
        return healthy();
    }

 private:
    bool reconcileDisconnectedSearch() noexcept {
        if(searchOpen_ && !disconnectedSearch_ &&
            coordinator_.snapshotForOwner().liveness.connection==PearlConnectionState::Disconnected) {
            const uint64_t remaining=boundCount_-dispatchOrdinal_;
            if(!coordinator_.retireDisconnectedSearch(issued_.session,issued_.authorization,
                issued_.job,issued_.digest,searchGeneration_,remaining) ||
                ledger_.invalidateSearch(issued_,searchGeneration_)!=remaining)return fail();
            disconnectedSearch_=true;
            issuedBound_=adopted_=hasPreparedCache_=false;
        }
        return true;
    }
    bool integrityHealthy() noexcept {
        return attached_ && !failed_ && !evidenceFinished_ && !retainingLocal_ &&
               source_.diagnosticCompletionHealthy() && !ledger_.quarantined() &&
               !coordinator_.snapshotForOwner().sourceQuarantined;
    }
    bool overlapsSelf(const void *p, std::size_t size) const noexcept {
        return pearlProofMemoryRangesOverlap(p, size, this, sizeof(*this));
    }
    static bool overlapsJob(const void *p, std::size_t size, const Job &job) noexcept {
        return pearlProofMemoryRangesOverlap(p, size, &job, sizeof(job)) ||
            pearlProofMemoryRangesOverlap(p, size, job.extra.c_str(), job.extra.size() + 1) ||
            pearlProofMemoryRangesOverlap(p, size, job.jobId.c_str(), job.jobId.size() + 1);
    }
    static void publication(void *context) noexcept {
        static_cast<PearlRunSession *>(context)->coordinator_.onSourcePublication();
    }
    bool matches(const Job &job) const noexcept {
        PearlPoolCanonicalIdentity identity;
        return pearlPoolCanonicalIdentityForJob(job, policy_, &identity) &&
               identity.sourceDigest.bytes == issued_.digest.bytes;
    }
    bool fail() noexcept {
        failed_ = true;
        bindingEnabled_ = false;
        if (ledger_.searchOpen() && !ledger_.quarantined())
            ledger_.invalidateSearch(issued_, searchGeneration_);
        return false; // fail-and-stop; never reuse unresolved coordinator state
    }
    bool failLocalRetirement() noexcept {
        localRetirementFailed_ = true;
        retainingLocal_ = false;
        return fail();
    }
    Algorithm &algorithm_;
    PearlPoolSource &source_;
    const PearlSourcePolicy policy_;
    PearlProgressBus bus_;
    PearlRunProgressCoordinator coordinator_;
    PearlProofOwnershipLedger ledger_;
    PearlProofJobIdentity issued_;
    PearlProofJobIdentity preparedIdentity_;
    struct BoundProof { uint64_t token = 0; PearlSerializedProofMetadata proof; };
    std::array<BoundProof, maximumAttempts> boundProofs_{};
    uint64_t searchGeneration_ = 0, dispatchOrdinal_ = 0, boundCount_ = 0;
    bool attached_ = false, failed_ = false, adopted_ = false;
    bool deferredSearchOwnership_ = false;
    bool issuedBound_ = false, prepared_ = false, searchOpen_ = false;
    bool bindingEnabled_ = false;
    bool hasPreparedCache_ = false;
    bool disconnectedSearch_ = false;
    // Borrowed synchronous sink must outlive this session; never reenter it.
    PearlLocalRetirementSink *localRetirements_ = nullptr;
    bool retainingLocal_ = false, localRetirementFailed_ = false;
    bool evidenceFinished_=false;
    bool evidenceFinalizing_=false;
};
} // namespace om
