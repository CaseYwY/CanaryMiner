// Pure Pearl lifecycle accounting and deterministic deadline classification.
//
// No sockets, CUDA types, threads, or wall clock live here. Production code
// supplies monotonic milliseconds and opaque generation tokens; host tests can
// therefore exercise every transition with a fake clock.

#pragma once

#include <cstdint>
#include <limits>

namespace om {

enum class PearlConnectionState {
    Disconnected,
    AwaitingAuthorization,
    AwaitingJob,
    Ready,
};

enum class PearlJobProgressState {
    NoJob,
    AwaitingFirstAttempt,
    Active,
};

enum class PearlCandidateState {
    Idle,
    Finalizing,
    AwaitingSubmit,
    AwaitingAcknowledgement,
    AwaitingOutcome,
};

enum class PearlDeadlineState {
    NotApplicable,
    Disabled,
    Fresh,
    Expired,
};

enum class PearlSubmitOutcome {
    None,
    SubmissionFailed,
    Acknowledged,
    Accepted,
    Rejected,
    Stale,
    Unknown,
    #ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    GuardRetired,
    #endif
};

enum class PearlOutcomeEvidence {
    None,
    LocalPreDeliveryFailure,
    FullySentNoReply,
    GatewayAcknowledgementOnly,
    CorrelatedRemoteOutcome,
    AcknowledgedOutcomeUnavailable,
    PossiblyDeliveredUnknown,
    #ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    ConfirmedGuardNonForwarding,
    #endif
};

struct PearlLivenessDeadlines {
    uint64_t authorizationMs = 0;
    uint64_t validJobMs = 0;
    uint64_t firstAttemptMs = 0;
    uint64_t attemptProgressMs = 0;
    uint64_t finalizerProgressMs = 0;
    uint64_t submitMs = 0;
    uint64_t acknowledgementMs = 0;
    uint64_t outcomeMs = 0;
    uint64_t outcomeFreshMs = 0;
};

struct PearlDeadline {
    PearlDeadlineState state = PearlDeadlineState::NotApplicable;
    uint64_t atMs = 0;
};

struct PearlLivenessSnapshot {
    PearlConnectionState connection = PearlConnectionState::Disconnected;
    PearlJobProgressState jobProgress = PearlJobProgressState::NoJob;
    PearlCandidateState candidate = PearlCandidateState::Idle;
    PearlSubmitOutcome lastOutcome = PearlSubmitOutcome::None;
    // Outcome and evidence are independent. In particular, Unknown may mean
    // a fully-sent request got no reply, a correlated remote Unknown arrived,
    // or acknowledgement arrived but its final verdict never did.
    PearlOutcomeEvidence lastOutcomeEvidence = PearlOutcomeEvidence::None;

    PearlDeadline connectionDeadline;
    PearlDeadline attemptDeadline;
    PearlDeadline candidateDeadline;
    PearlDeadline outcomeFreshness;

    uint64_t sessionGeneration = 0;
    uint64_t jobGeneration = 0;
    uint64_t candidateGeneration = 0;
    uint64_t submitGeneration = 0;
    uint64_t lastOutcomeSubmitGeneration = 0;

    uint64_t completedAttempts = 0;
    uint64_t completedAttemptsForJob = 0;
    uint64_t candidatesObserved = 0;
    uint64_t finalizerProgressEvents = 0;
    uint64_t candidatesFinalized = 0;
    uint64_t candidatesInvalid = 0;
    uint64_t submitAttempts = 0;
    uint64_t submissionFailures = 0;
    uint64_t possiblyDeliveredUnknowns = 0;
    // Fully-sent requests with no correlated reply. Possibly-delivered
    // unknowns are deliberately counted in the separate field above.
    uint64_t guardRetired = 0;
    uint64_t unacknowledgedUnknowns = 0;
    uint64_t acknowledgements = 0;
    uint64_t outcomes = 0;

    // A query before the last accepted event cannot make stale work look fresh.
    // Classifications use the last event time and expose the caller error here.
    bool queryTimeValid = true;

    /** Conservation constraints for the one-at-a-time submit pipeline. */
    bool submissionAccountingHolds() const {
        if (submissionFailures > submitAttempts ||
            possiblyDeliveredUnknowns > submitAttempts ||
            unacknowledgedUnknowns > submitAttempts ||
            acknowledgements > submitAttempts || outcomes > submitAttempts)
            return false;

        // Failure, possibly-delivered unknown, fully-sent no-reply unknown,
        // and acknowledgement are mutually exclusive first terminal/remote
        // observations for a submit.
        uint64_t unclassified = submitAttempts;
        if (submissionFailures > unclassified) return false;
        unclassified -= submissionFailures;
        if (possiblyDeliveredUnknowns > unclassified) return false;
        unclassified -= possiblyDeliveredUnknowns;
        if (unacknowledgedUnknowns > unclassified) return false;
        unclassified -= unacknowledgedUnknowns;
        if (guardRetired > unclassified) return false;
        unclassified -= guardRetired;
        if (acknowledgements > unclassified) return false;

        // Every outcome is local failure, one of the two unacknowledged
        // unknown classes, or a terminal result following acknowledgement. A
        // nonterminal ack may legitimately leave outcomes below
        // acknowledgements.
        uint64_t remainingOutcomes = outcomes;
        if (submissionFailures > remainingOutcomes) return false;
        remainingOutcomes -= submissionFailures;
        if (possiblyDeliveredUnknowns > remainingOutcomes) return false;
        remainingOutcomes -= possiblyDeliveredUnknowns;
        if (unacknowledgedUnknowns > remainingOutcomes) return false;
        remainingOutcomes -= unacknowledgedUnknowns;
        if (guardRetired > remainingOutcomes) return false;
        remainingOutcomes -= guardRetired;
        return remainingOutcomes <= acknowledgements;
    }

    /** Last terminal result, its evidence, and its owning token must agree. */
    bool outcomeEvidenceHolds() const {
        if (lastOutcome == PearlSubmitOutcome::None)
            return lastOutcomeEvidence == PearlOutcomeEvidence::None &&
                   lastOutcomeSubmitGeneration == 0;
        if (lastOutcomeEvidence == PearlOutcomeEvidence::None ||
            lastOutcomeSubmitGeneration == 0 ||
            lastOutcomeSubmitGeneration > submitGeneration)
            return false;

        switch (lastOutcome) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
            case PearlSubmitOutcome::GuardRetired:
                return lastOutcomeEvidence == PearlOutcomeEvidence::ConfirmedGuardNonForwarding;
#endif
            case PearlSubmitOutcome::SubmissionFailed:
                return lastOutcomeEvidence ==
                       PearlOutcomeEvidence::LocalPreDeliveryFailure;
            case PearlSubmitOutcome::Acknowledged:
                return lastOutcomeEvidence ==
                       PearlOutcomeEvidence::GatewayAcknowledgementOnly;
            case PearlSubmitOutcome::Accepted:
            case PearlSubmitOutcome::Rejected:
            case PearlSubmitOutcome::Stale:
                return lastOutcomeEvidence ==
                       PearlOutcomeEvidence::CorrelatedRemoteOutcome;
            case PearlSubmitOutcome::Unknown:
                return lastOutcomeEvidence ==
                           PearlOutcomeEvidence::PossiblyDeliveredUnknown ||
                       lastOutcomeEvidence ==
                           PearlOutcomeEvidence::FullySentNoReply ||
                       lastOutcomeEvidence ==
                           PearlOutcomeEvidence::CorrelatedRemoteOutcome ||
                       lastOutcomeEvidence ==
                           PearlOutcomeEvidence::AcknowledgedOutcomeUnavailable;
            case PearlSubmitOutcome::None:
                break;
        }
        return false;
    }
};

inline void pearlLivenessSaturatingIncrement(uint64_t *counter) {
    if (counter && *counter != std::numeric_limits<uint64_t>::max())
        ++*counter;
}

class PearlLivenessTracker {
   public:
    /** Start a new transport session and return its nonzero ownership token. */
    uint64_t beginSession(uint64_t nowMs) {
        if (connection_ != PearlConnectionState::Disconnected ||
            !canAdvance(sessionGeneration_) || !acceptTime(nowMs) ||
            !advance(&sessionGeneration_))
            return 0;
        connection_ = PearlConnectionState::AwaitingAuthorization;
        jobProgress_ = PearlJobProgressState::NoJob;
        candidate_ = PearlCandidateState::Idle;
        awaitingJobAtMs_ = jobAtMs_ = lastAttemptAtMs_ = 0;
        finalizerProgressAtMs_ = finalizedAtMs_ = 0;
        submittedAtMs_ = acknowledgedAtMs_ = outcomeAtMs_ = 0;
        sessionAtMs_ = nowMs;
        lastOutcomeSubmitGeneration_ = 0;
        completedAttemptsForJob_ = 0;
        lastOutcome_ = PearlSubmitOutcome::None;
        lastOutcomeEvidence_ = PearlOutcomeEvidence::None;
        return sessionGeneration_;
    }

    bool onAuthorization(uint64_t sessionGeneration, uint64_t nowMs) {
        if (sessionGeneration != sessionGeneration_ ||
            connection_ != PearlConnectionState::AwaitingAuthorization ||
            !acceptTime(nowMs))
            return false;
        connection_ = PearlConnectionState::AwaitingJob;
        awaitingJobAtMs_ = nowMs;
        return true;
    }

    /** Accept a valid job and return its nonzero generation token. */
    uint64_t onValidJob(uint64_t sessionGeneration, uint64_t nowMs) {
        if (sessionGeneration != sessionGeneration_ ||
            (connection_ != PearlConnectionState::AwaitingJob &&
             connection_ != PearlConnectionState::Ready) ||
            !canAdvance(jobGeneration_) || !acceptTime(nowMs))
            return 0;
        advance(&jobGeneration_);
        connection_ = PearlConnectionState::Ready;
        jobProgress_ = PearlJobProgressState::AwaitingFirstAttempt;
        jobAtMs_ = nowMs;
        lastAttemptAtMs_ = 0;
        completedAttemptsForJob_ = 0;

        // A candidate not yet sent belongs to the replaced job and loses
        // ownership. A submitted candidate remains independently identifiable
        // by submit generation: a pool may acknowledge it after pushing a new
        // job, so preserve AwaitingAcknowledgement/AwaitingOutcome.
        if (candidate_ == PearlCandidateState::Finalizing ||
            candidate_ == PearlCandidateState::AwaitingSubmit)
            candidate_ = PearlCandidateState::Idle;
        return jobGeneration_;
    }

    /** Retire unusable work while keeping the authorized transport session. */
    bool onJobRetired(uint64_t sessionGeneration, uint64_t nowMs) {
        if (sessionGeneration != sessionGeneration_ ||
            connection_ != PearlConnectionState::Ready ||
            !acceptTime(nowMs))
            return false;
        connection_ = PearlConnectionState::AwaitingJob;
        jobProgress_ = PearlJobProgressState::NoJob;
        awaitingJobAtMs_ = nowMs;
        lastAttemptAtMs_ = 0;
        completedAttemptsForJob_ = 0;
        if (candidate_ == PearlCandidateState::Finalizing ||
            candidate_ == PearlCandidateState::AwaitingSubmit)
            candidate_ = PearlCandidateState::Idle;
        return true;
    }

    bool onAttemptCompleted(uint64_t jobGeneration, uint64_t nowMs) {
        if (jobGeneration != jobGeneration_ ||
            connection_ != PearlConnectionState::Ready ||
            jobProgress_ == PearlJobProgressState::NoJob ||
            !acceptTime(nowMs))
            return false;
        jobProgress_ = PearlJobProgressState::Active;
        lastAttemptAtMs_ = nowMs;
        pearlLivenessSaturatingIncrement(&completedAttempts_);
        pearlLivenessSaturatingIncrement(&completedAttemptsForJob_);
        return true;
    }

    /** Record a GPU candidate and return its nonzero finalizer token. */
    uint64_t onCandidate(uint64_t jobGeneration, uint64_t nowMs) {
        if (jobGeneration != jobGeneration_ ||
            jobProgress_ != PearlJobProgressState::Active ||
            candidate_ != PearlCandidateState::Idle ||
            !canAdvance(candidateGeneration_) || !acceptTime(nowMs))
            return 0;
        advance(&candidateGeneration_);
        candidate_ = PearlCandidateState::Finalizing;
        finalizerProgressAtMs_ = nowMs;
        pearlLivenessSaturatingIncrement(&candidatesObserved_);
        return candidateGeneration_;
    }

    bool onFinalizerProgress(uint64_t candidateGeneration, uint64_t nowMs) {
        if (candidateGeneration != candidateGeneration_ ||
            candidate_ != PearlCandidateState::Finalizing ||
            !acceptTime(nowMs))
            return false;
        finalizerProgressAtMs_ = nowMs;
        pearlLivenessSaturatingIncrement(&finalizerProgressEvents_);
        return true;
    }

    /** Finish proof construction. Non-submittable candidates return to idle. */
    bool onCandidateFinalized(uint64_t candidateGeneration, bool submittable,
                              uint64_t nowMs) {
        if (candidateGeneration != candidateGeneration_ ||
            candidate_ != PearlCandidateState::Finalizing ||
            !acceptTime(nowMs))
            return false;
        pearlLivenessSaturatingIncrement(&candidatesFinalized_);
        finalizedAtMs_ = nowMs;
        if (submittable) {
            candidate_ = PearlCandidateState::AwaitingSubmit;
        } else {
            pearlLivenessSaturatingIncrement(&candidatesInvalid_);
            candidate_ = PearlCandidateState::Idle;
        }
        return true;
    }

    /** Record a send attempt and return its nonzero acknowledgement token. */
    uint64_t onSubmitAttempt(uint64_t candidateGeneration, uint64_t nowMs) {
        if (candidateGeneration != candidateGeneration_ ||
            candidate_ != PearlCandidateState::AwaitingSubmit ||
            !canAdvance(submitGeneration_) || !acceptTime(nowMs))
            return 0;
        advance(&submitGeneration_);
        candidate_ = PearlCandidateState::AwaitingAcknowledgement;
        submittedAtMs_ = nowMs;
        pearlLivenessSaturatingIncrement(&submitAttempts_);
        return submitGeneration_;
    }

    /**
     * Close a send attempt that failed before any remote acknowledgement.
     * This is a local transport/dispatch result, never a pool rejection.
     */
    bool onSubmissionFailed(uint64_t submitGeneration, uint64_t nowMs) {
        if (submitGeneration != submitGeneration_ ||
            candidate_ != PearlCandidateState::AwaitingAcknowledgement ||
            !acceptTime(nowMs))
            return false;
        candidate_ = PearlCandidateState::Idle;
        lastOutcome_ = PearlSubmitOutcome::SubmissionFailed;
        lastOutcomeEvidence_ = PearlOutcomeEvidence::LocalPreDeliveryFailure;
        lastOutcomeSubmitGeneration_ = submitGeneration;
        outcomeAtMs_ = nowMs;
        pearlLivenessSaturatingIncrement(&submissionFailures_);
        pearlLivenessSaturatingIncrement(&outcomes_);
        return true;
    }

    /**
     * Close a fully-sent request for which no correlated reply arrived.
     * Delivery and credit are both unknown. This is terminal, but it is
     * neither a remote acknowledgement nor a local pre-delivery failure.
     */
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    // Confirmed native-to-guard delivery with no pool forwarding or ACK.
    bool onGuardRetired(uint64_t submitGeneration, uint64_t nowMs) {
        if (submitGeneration != submitGeneration_ ||
            candidate_ != PearlCandidateState::AwaitingAcknowledgement || !acceptTime(nowMs)) return false;
        candidate_ = PearlCandidateState::Idle;
        lastOutcome_ = PearlSubmitOutcome::GuardRetired;
        lastOutcomeEvidence_ = PearlOutcomeEvidence::ConfirmedGuardNonForwarding;
        lastOutcomeSubmitGeneration_ = submitGeneration;
        outcomeAtMs_ = nowMs;
        pearlLivenessSaturatingIncrement(&guardRetired_);
        pearlLivenessSaturatingIncrement(&outcomes_);
        return true;
    }

#endif
    bool onDeliveryUnknown(uint64_t submitGeneration, uint64_t nowMs) {
        if (submitGeneration != submitGeneration_ ||
            candidate_ != PearlCandidateState::AwaitingAcknowledgement ||
            !acceptTime(nowMs))
            return false;
        candidate_ = PearlCandidateState::Idle;
        lastOutcome_ = PearlSubmitOutcome::Unknown;
        lastOutcomeEvidence_ = PearlOutcomeEvidence::FullySentNoReply;
        lastOutcomeSubmitGeneration_ = submitGeneration;
        outcomeAtMs_ = nowMs;
        pearlLivenessSaturatingIncrement(&unacknowledgedUnknowns_);
        pearlLivenessSaturatingIncrement(&outcomes_);
        return true;
    }

    /**
     * Close a send attempt after bytes may have reached the peer, while full
     * request delivery is unproven and no acknowledgement arrived. This is
     * terminal Unknown evidence, not a local pre-delivery failure and not a
     * fully-sent-no-reply classification.
     */
    bool onPossiblyDeliveredUnknown(uint64_t submitGeneration,
                                    uint64_t nowMs) {
        if (submitGeneration != submitGeneration_ ||
            candidate_ != PearlCandidateState::AwaitingAcknowledgement ||
            !acceptTime(nowMs))
            return false;
        candidate_ = PearlCandidateState::Idle;
        lastOutcome_ = PearlSubmitOutcome::Unknown;
        lastOutcomeEvidence_ =
            PearlOutcomeEvidence::PossiblyDeliveredUnknown;
        lastOutcomeSubmitGeneration_ = submitGeneration;
        outcomeAtMs_ = nowMs;
        pearlLivenessSaturatingIncrement(&possiblyDeliveredUnknowns_);
        pearlLivenessSaturatingIncrement(&outcomes_);
        return true;
    }

    /**
     * Record source acknowledgement. For an acknowledgement-only source such
     * as the Pearl gateway, terminal=true closes the pipeline without inventing
     * an accepted/rejected verdict. Pools use terminal=false and later provide
     * onOutcome().
     */
    bool onAcknowledgement(uint64_t submitGeneration, uint64_t nowMs,
                           bool terminal = false) {
        if (submitGeneration != submitGeneration_ ||
            candidate_ != PearlCandidateState::AwaitingAcknowledgement ||
            !acceptTime(nowMs))
            return false;
        acknowledgedAtMs_ = nowMs;
        pearlLivenessSaturatingIncrement(&acknowledgements_);
        if (terminal) {
            candidate_ = PearlCandidateState::Idle;
            lastOutcome_ = PearlSubmitOutcome::Acknowledged;
            lastOutcomeEvidence_ =
                PearlOutcomeEvidence::GatewayAcknowledgementOnly;
            lastOutcomeSubmitGeneration_ = submitGeneration;
            outcomeAtMs_ = nowMs;
            pearlLivenessSaturatingIncrement(&outcomes_);
        } else {
            candidate_ = PearlCandidateState::AwaitingOutcome;
        }
        return true;
    }

    bool onOutcome(uint64_t submitGeneration, PearlSubmitOutcome outcome,
                   uint64_t nowMs) {
        if (submitGeneration != submitGeneration_ ||
            candidate_ != PearlCandidateState::AwaitingOutcome ||
            !isRemoteOutcome(outcome) || !acceptTime(nowMs))
            return false;
        candidate_ = PearlCandidateState::Idle;
        lastOutcome_ = outcome;
        lastOutcomeEvidence_ = PearlOutcomeEvidence::CorrelatedRemoteOutcome;
        lastOutcomeSubmitGeneration_ = submitGeneration;
        outcomeAtMs_ = nowMs;
        pearlLivenessSaturatingIncrement(&outcomes_);
        return true;
    }

    /**
     * Close an acknowledged submission when no final verdict will arrive.
     * This records terminal Unknown without fabricating a remote verdict and
     * without counting a second acknowledgement.
     */
    bool onOutcomeUnavailable(uint64_t submitGeneration, uint64_t nowMs) {
        if (submitGeneration != submitGeneration_ ||
            candidate_ != PearlCandidateState::AwaitingOutcome ||
            !acceptTime(nowMs))
            return false;
        candidate_ = PearlCandidateState::Idle;
        lastOutcome_ = PearlSubmitOutcome::Unknown;
        lastOutcomeEvidence_ =
            PearlOutcomeEvidence::AcknowledgedOutcomeUnavailable;
        lastOutcomeSubmitGeneration_ = submitGeneration;
        outcomeAtMs_ = nowMs;
        pearlLivenessSaturatingIncrement(&outcomes_);
        return true;
    }

    bool onDisconnect(uint64_t sessionGeneration, uint64_t nowMs) {
        if (sessionGeneration != sessionGeneration_ ||
            connection_ == PearlConnectionState::Disconnected ||
            candidate_ == PearlCandidateState::AwaitingAcknowledgement ||
            candidate_ == PearlCandidateState::AwaitingOutcome ||
            !acceptTime(nowMs))
            return false;
        connection_ = PearlConnectionState::Disconnected;
        jobProgress_ = PearlJobProgressState::NoJob;
        candidate_ = PearlCandidateState::Idle;
        lastAttemptAtMs_ = 0;
        completedAttemptsForJob_ = 0;
        return true;
    }

    PearlLivenessSnapshot snapshot(
        uint64_t nowMs, const PearlLivenessDeadlines &limits) const {
        PearlLivenessSnapshot out;
        out.queryTimeValid = !haveTime_ || nowMs >= lastEventAtMs_;
        if (!out.queryTimeValid) nowMs = lastEventAtMs_;

        out.connection = connection_;
        out.jobProgress = jobProgress_;
        out.candidate = candidate_;
        out.lastOutcome = lastOutcome_;
        out.lastOutcomeEvidence = lastOutcomeEvidence_;
        out.sessionGeneration = sessionGeneration_;
        out.jobGeneration = jobGeneration_;
        out.candidateGeneration = candidateGeneration_;
        out.submitGeneration = submitGeneration_;
        out.lastOutcomeSubmitGeneration = lastOutcomeSubmitGeneration_;
        out.completedAttempts = completedAttempts_;
        out.completedAttemptsForJob = completedAttemptsForJob_;
        out.candidatesObserved = candidatesObserved_;
        out.finalizerProgressEvents = finalizerProgressEvents_;
        out.candidatesFinalized = candidatesFinalized_;
        out.candidatesInvalid = candidatesInvalid_;
        out.submitAttempts = submitAttempts_;
        out.submissionFailures = submissionFailures_;
        out.possiblyDeliveredUnknowns = possiblyDeliveredUnknowns_;
        out.guardRetired = guardRetired_;
        out.unacknowledgedUnknowns = unacknowledgedUnknowns_;
        out.acknowledgements = acknowledgements_;
        out.outcomes = outcomes_;

        if (connection_ == PearlConnectionState::AwaitingAuthorization)
            out.connectionDeadline = deadline(sessionAtMs_,
                                              limits.authorizationMs, nowMs);
        else if (connection_ == PearlConnectionState::AwaitingJob)
            out.connectionDeadline = deadline(awaitingJobAtMs_,
                                              limits.validJobMs, nowMs);

        if (jobProgress_ == PearlJobProgressState::AwaitingFirstAttempt)
            out.attemptDeadline = deadline(jobAtMs_, limits.firstAttemptMs,
                                           nowMs);
        else if (jobProgress_ == PearlJobProgressState::Active)
            out.attemptDeadline = deadline(lastAttemptAtMs_,
                                           limits.attemptProgressMs, nowMs);

        switch (candidate_) {
            case PearlCandidateState::Finalizing:
                out.candidateDeadline = deadline(finalizerProgressAtMs_,
                                                 limits.finalizerProgressMs,
                                                 nowMs);
                break;
            case PearlCandidateState::AwaitingSubmit:
                out.candidateDeadline = deadline(finalizedAtMs_,
                                                 limits.submitMs, nowMs);
                break;
            case PearlCandidateState::AwaitingAcknowledgement:
                out.candidateDeadline = deadline(submittedAtMs_,
                                                 limits.acknowledgementMs,
                                                 nowMs);
                break;
            case PearlCandidateState::AwaitingOutcome:
                out.candidateDeadline = deadline(acknowledgedAtMs_,
                                                 limits.outcomeMs, nowMs);
                break;
            default:
                break;
        }

        if (lastOutcome_ != PearlSubmitOutcome::None)
            out.outcomeFreshness = deadline(outcomeAtMs_,
                                            limits.outcomeFreshMs, nowMs);
        return out;
    }

   private:
    static bool isRemoteOutcome(PearlSubmitOutcome outcome) {
        return outcome == PearlSubmitOutcome::Accepted ||
               outcome == PearlSubmitOutcome::Rejected ||
               outcome == PearlSubmitOutcome::Stale ||
               outcome == PearlSubmitOutcome::Unknown;
    }

    static bool canAdvance(uint64_t generation) {
        return generation != std::numeric_limits<uint64_t>::max();
    }

    static bool advance(uint64_t *generation) {
        if (!generation || !canAdvance(*generation)) return false;
        ++*generation;
        return true;
    }

    bool acceptTime(uint64_t nowMs) {
        if (haveTime_ && nowMs < lastEventAtMs_) return false;
        haveTime_ = true;
        lastEventAtMs_ = nowMs;
        return true;
    }

    static uint64_t saturatingAdd(uint64_t base, uint64_t duration) {
        const uint64_t max = std::numeric_limits<uint64_t>::max();
        return duration > max - base ? max : base + duration;
    }

    static PearlDeadline deadline(uint64_t sinceMs, uint64_t durationMs,
                                  uint64_t nowMs) {
        PearlDeadline out;
        if (!durationMs) {
            out.state = PearlDeadlineState::Disabled;
            return out;
        }
        out.atMs = saturatingAdd(sinceMs, durationMs);
        out.state = nowMs >= out.atMs ? PearlDeadlineState::Expired
                                      : PearlDeadlineState::Fresh;
        return out;
    }

    PearlConnectionState connection_ = PearlConnectionState::Disconnected;
    PearlJobProgressState jobProgress_ = PearlJobProgressState::NoJob;
    PearlCandidateState candidate_ = PearlCandidateState::Idle;
    PearlSubmitOutcome lastOutcome_ = PearlSubmitOutcome::None;
    PearlOutcomeEvidence lastOutcomeEvidence_ = PearlOutcomeEvidence::None;

    bool haveTime_ = false;
    uint64_t lastEventAtMs_ = 0;
    uint64_t sessionAtMs_ = 0, awaitingJobAtMs_ = 0, jobAtMs_ = 0;
    uint64_t lastAttemptAtMs_ = 0;
    uint64_t finalizerProgressAtMs_ = 0, finalizedAtMs_ = 0;
    uint64_t submittedAtMs_ = 0, acknowledgedAtMs_ = 0, outcomeAtMs_ = 0;

    uint64_t sessionGeneration_ = 0, jobGeneration_ = 0;
    uint64_t candidateGeneration_ = 0, submitGeneration_ = 0;
    uint64_t lastOutcomeSubmitGeneration_ = 0;

    uint64_t completedAttempts_ = 0, completedAttemptsForJob_ = 0;
    uint64_t candidatesObserved_ = 0, finalizerProgressEvents_ = 0;
    uint64_t candidatesFinalized_ = 0, candidatesInvalid_ = 0;
    uint64_t submitAttempts_ = 0, submissionFailures_ = 0;
    uint64_t possiblyDeliveredUnknowns_ = 0;
    uint64_t guardRetired_ = 0;
    uint64_t unacknowledgedUnknowns_ = 0;
    uint64_t acknowledgements_ = 0, outcomes_ = 0;
};

}  // namespace om
