#include <cstdint>
#include <cstdio>
#include <limits>

#include "../src/core/pearl_liveness.h"

namespace {

int failures = 0;

void check(const char *name, bool condition) {
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", name);
    if (!condition) ++failures;
}

om::PearlLivenessDeadlines limits() {
    om::PearlLivenessDeadlines out;
    out.authorizationMs = 10;
    out.validJobMs = 20;
    out.firstAttemptMs = 30;
    out.attemptProgressMs = 25;
    out.finalizerProgressMs = 15;
    out.submitMs = 12;
    out.acknowledgementMs = 8;
    out.outcomeMs = 10;
    out.outcomeFreshMs = 20;
    return out;
}

bool sameDeadline(const om::PearlDeadline &a, const om::PearlDeadline &b) {
    return a.state == b.state && a.atMs == b.atMs;
}

bool sameSnapshot(const om::PearlLivenessSnapshot &a,
                  const om::PearlLivenessSnapshot &b) {
    return a.connection == b.connection && a.jobProgress == b.jobProgress &&
           a.candidate == b.candidate && a.lastOutcome == b.lastOutcome &&
           a.lastOutcomeEvidence == b.lastOutcomeEvidence &&
           sameDeadline(a.connectionDeadline, b.connectionDeadline) &&
           sameDeadline(a.attemptDeadline, b.attemptDeadline) &&
           sameDeadline(a.candidateDeadline, b.candidateDeadline) &&
           sameDeadline(a.outcomeFreshness, b.outcomeFreshness) &&
           a.sessionGeneration == b.sessionGeneration &&
           a.jobGeneration == b.jobGeneration &&
           a.candidateGeneration == b.candidateGeneration &&
           a.submitGeneration == b.submitGeneration &&
           a.lastOutcomeSubmitGeneration == b.lastOutcomeSubmitGeneration &&
           a.completedAttempts == b.completedAttempts &&
           a.completedAttemptsForJob == b.completedAttemptsForJob &&
           a.candidatesObserved == b.candidatesObserved &&
           a.finalizerProgressEvents == b.finalizerProgressEvents &&
           a.candidatesFinalized == b.candidatesFinalized &&
           a.candidatesInvalid == b.candidatesInvalid &&
           a.submitAttempts == b.submitAttempts &&
           a.submissionFailures == b.submissionFailures &&
           a.possiblyDeliveredUnknowns == b.possiblyDeliveredUnknowns &&
           a.unacknowledgedUnknowns == b.unacknowledgedUnknowns &&
           a.acknowledgements == b.acknowledgements &&
           a.outcomes == b.outcomes &&
           a.queryTimeValid == b.queryTimeValid;
}

bool reachAwaitingAcknowledgement(om::PearlLivenessTracker *tracker,
                                  uint64_t baseMs, uint64_t *session,
                                  uint64_t *submit) {
    if (!tracker || !session || !submit) return false;
    *session = tracker->beginSession(baseMs);
    if (*session == 0 ||
        !tracker->onAuthorization(*session, baseMs + 1))
        return false;
    const uint64_t job = tracker->onValidJob(*session, baseMs + 2);
    if (job == 0 || !tracker->onAttemptCompleted(job, baseMs + 3))
        return false;
    const uint64_t candidate = tracker->onCandidate(job, baseMs + 4);
    if (candidate == 0 ||
        !tracker->onCandidateFinalized(candidate, true, baseMs + 5))
        return false;
    *submit = tracker->onSubmitAttempt(candidate, baseMs + 6);
    return *submit != 0;
}

}  // namespace

int main() {
    using namespace om;
    const PearlLivenessDeadlines configured = limits();
    PearlLivenessTracker tracker;

    PearlLivenessSnapshot snap = tracker.snapshot(0, configured);
    check("initial state is disconnected",
          snap.connection == PearlConnectionState::Disconnected &&
              snap.jobProgress == PearlJobProgressState::NoJob &&
              snap.connectionDeadline.state ==
                  PearlDeadlineState::NotApplicable);

    const uint64_t session1 = tracker.beginSession(100);
    check("a session receives a nonzero generation", session1 != 0);
    snap = tracker.snapshot(109, configured);
    check("authorization deadline is fresh before its boundary",
          snap.connection == PearlConnectionState::AwaitingAuthorization &&
              snap.connectionDeadline.state == PearlDeadlineState::Fresh &&
              snap.connectionDeadline.atMs == 110);
    snap = tracker.snapshot(110, configured);
    check("authorization expires exactly at its boundary",
          snap.connectionDeadline.state == PearlDeadlineState::Expired);
    check("authorization rejects a stale session token",
          !tracker.onAuthorization(session1 + 1, 104));
    check("authorization accepts the current session",
          tracker.onAuthorization(session1, 104));
    snap = tracker.snapshot(123, configured);
    check("valid-job deadline is distinct from authorization",
          snap.connection == PearlConnectionState::AwaitingJob &&
              snap.connectionDeadline.state == PearlDeadlineState::Fresh &&
              snap.connectionDeadline.atMs == 124);

    const uint64_t job1 = tracker.onValidJob(session1, 110);
    check("a valid job receives a nonzero generation", job1 != 0);
    snap = tracker.snapshot(139, configured);
    check("first-attempt deadline starts at valid job adoption",
          snap.connection == PearlConnectionState::Ready &&
              snap.jobProgress == PearlJobProgressState::AwaitingFirstAttempt &&
              snap.attemptDeadline.state == PearlDeadlineState::Fresh &&
              snap.attemptDeadline.atMs == 140);
    snap = tracker.snapshot(140, configured);
    check("missing first attempt expires at the exact boundary",
          snap.attemptDeadline.state == PearlDeadlineState::Expired);
    check("an attempt cannot advance the wrong job generation",
          !tracker.onAttemptCompleted(job1 + 1, 115));
    check("the first completed attempt activates job progress",
          tracker.onAttemptCompleted(job1, 120));
    snap = tracker.snapshot(145, configured);
    check("continued-attempt freshness has its own deadline",
          snap.jobProgress == PearlJobProgressState::Active &&
              snap.attemptDeadline.state == PearlDeadlineState::Expired &&
              snap.completedAttempts == 1 &&
              snap.completedAttemptsForJob == 1);
    check("absence of a stochastic candidate is not a deadline",
          snap.candidate == PearlCandidateState::Idle &&
              snap.candidateDeadline.state ==
                  PearlDeadlineState::NotApplicable &&
              snap.candidatesObserved == 0);

    const uint64_t candidate1 = tracker.onCandidate(job1, 151);
    check("a GPU candidate receives a finalizer generation", candidate1 != 0);
    snap = tracker.snapshot(166, configured);
    check("an idle finalizer expires independently",
          snap.candidate == PearlCandidateState::Finalizing &&
              snap.candidateDeadline.state == PearlDeadlineState::Expired &&
              snap.candidateDeadline.atMs == 166);
    check("a stale candidate cannot refresh finalizer progress",
          !tracker.onFinalizerProgress(candidate1 + 1, 155));
    check("real finalizer progress refreshes its deadline",
          tracker.onFinalizerProgress(candidate1, 160));
    snap = tracker.snapshot(174, configured);
    check("refreshed finalizer remains fresh",
          snap.candidateDeadline.state == PearlDeadlineState::Fresh &&
              snap.candidateDeadline.atMs == 175 &&
              snap.finalizerProgressEvents == 1);
    check("a submittable finalized candidate awaits send",
          tracker.onCandidateFinalized(candidate1, true, 165));
    snap = tracker.snapshot(177, configured);
    check("submit deadline is classified separately",
          snap.candidate == PearlCandidateState::AwaitingSubmit &&
              snap.candidateDeadline.state == PearlDeadlineState::Expired);

    const uint64_t submit1 = tracker.onSubmitAttempt(candidate1, 170);
    check("a submit attempt receives an acknowledgement generation",
          submit1 != 0);
    const uint64_t job2 = tracker.onValidJob(session1, 172);
    snap = tracker.snapshot(177, configured);
    check("replacement job preserves an already-sent share",
          job2 != 0 && job2 != job1 &&
              snap.jobProgress == PearlJobProgressState::AwaitingFirstAttempt &&
              snap.candidate ==
                  PearlCandidateState::AwaitingAcknowledgement &&
              snap.candidateDeadline.state == PearlDeadlineState::Fresh);
    check("old submitted share can be acknowledged after replacement job",
          tracker.onAcknowledgement(submit1, 175));
    snap = tracker.snapshot(185, configured);
    check("acknowledgement and final outcome have distinct deadlines",
          snap.candidate == PearlCandidateState::AwaitingOutcome &&
              snap.candidateDeadline.state == PearlDeadlineState::Expired &&
              snap.acknowledgements == 1);
    check("the replacement job can make progress while outcome is pending",
          tracker.onAttemptCompleted(job2, 176));
    check("a stale submit token cannot provide an outcome",
          !tracker.onOutcome(submit1 + 1, PearlSubmitOutcome::Accepted, 179));
    check("local and acknowledgement-only results cannot masquerade as pool outcomes",
          !tracker.onOutcome(submit1, PearlSubmitOutcome::SubmissionFailed, 179) &&
              !tracker.onOutcome(submit1, PearlSubmitOutcome::Acknowledged, 179));
    check("the exact submit token records its outcome",
          tracker.onOutcome(submit1, PearlSubmitOutcome::Accepted, 180));
    snap = tracker.snapshot(199, configured);
    check("a completed outcome is fresh for the configured interval",
          snap.candidate == PearlCandidateState::Idle &&
              snap.lastOutcome == PearlSubmitOutcome::Accepted &&
              snap.lastOutcomeEvidence ==
                  PearlOutcomeEvidence::CorrelatedRemoteOutcome &&
              snap.outcomeFreshness.state == PearlDeadlineState::Fresh &&
              snap.outcomeFreshness.atMs == 200 && snap.outcomes == 1 &&
              snap.submissionAccountingHolds());
    snap = tracker.snapshot(200, configured);
    check("outcome freshness expires exactly at its boundary",
          snap.outcomeFreshness.state == PearlDeadlineState::Expired);

    const uint64_t candidate2 = tracker.onCandidate(job2, 201);
    check("a new candidate can be rejected by its finalizer",
          candidate2 != 0 &&
              tracker.onCandidateFinalized(candidate2, false, 202));
    snap = tracker.snapshot(202, configured);
    check("invalid finalization returns idle and remains observable",
          snap.candidate == PearlCandidateState::Idle &&
              snap.candidatesFinalized == 2 && snap.candidatesInvalid == 1);

    const uint64_t failedCandidate = tracker.onCandidate(job2, 202);
    check("a candidate can reach a local submission attempt",
          failedCandidate != 0 &&
              tracker.onCandidateFinalized(failedCandidate, true, 202));
    const uint64_t failedSubmit =
        tracker.onSubmitAttempt(failedCandidate, 202);
    check("a stale token cannot close a failed submission",
          failedSubmit != 0 &&
              !tracker.onSubmissionFailed(failedSubmit + 1, 202));
    check("local submission failure closes the pipeline",
          tracker.onSubmissionFailed(failedSubmit, 202));
    snap = tracker.snapshot(202, configured);
    check("local submission failure is not a pool rejection",
          snap.candidate == PearlCandidateState::Idle &&
              snap.lastOutcome == PearlSubmitOutcome::SubmissionFailed &&
              snap.lastOutcomeEvidence ==
                  PearlOutcomeEvidence::LocalPreDeliveryFailure &&
              snap.submissionFailures == 1 && snap.acknowledgements == 1 &&
              snap.possiblyDeliveredUnknowns == 0 &&
              snap.unacknowledgedUnknowns == 0 && snap.outcomes == 2 &&
              snap.submissionAccountingHolds());

    const uint64_t unknownCandidate = tracker.onCandidate(job2, 202);
    check("a fully-sent no-reply candidate reaches acknowledgement wait",
          unknownCandidate != 0 &&
              tracker.onCandidateFinalized(unknownCandidate, true, 202));
    const uint64_t unknownSubmit =
        tracker.onSubmitAttempt(unknownCandidate, 202);
    check("stale token cannot manufacture an unacknowledged unknown",
          unknownSubmit != 0 &&
              !tracker.onDeliveryUnknown(unknownSubmit + 1, 202));
    check("fully-sent no-reply request closes as delivery unknown",
          tracker.onDeliveryUnknown(unknownSubmit, 202));
    snap = tracker.snapshot(202, configured);
    check("delivery unknown is terminal but not acknowledged or locally failed",
          snap.candidate == PearlCandidateState::Idle &&
              snap.lastOutcome == PearlSubmitOutcome::Unknown &&
              snap.lastOutcomeEvidence ==
                  PearlOutcomeEvidence::FullySentNoReply &&
              snap.lastOutcomeSubmitGeneration == unknownSubmit &&
              snap.submissionFailures == 1 &&
              snap.possiblyDeliveredUnknowns == 0 &&
              snap.unacknowledgedUnknowns == 1 &&
              snap.acknowledgements == 1 && snap.outcomes == 3 &&
              snap.candidateDeadline.state ==
                  PearlDeadlineState::NotApplicable &&
              snap.outcomeFreshness.state == PearlDeadlineState::Fresh &&
              snap.submissionAccountingHolds());
    check("closed delivery-unknown token cannot later acknowledge or fail",
          !tracker.onAcknowledgement(unknownSubmit, 202) &&
              !tracker.onSubmissionFailed(unknownSubmit, 202) &&
              !tracker.onOutcome(unknownSubmit, PearlSubmitOutcome::Unknown,
                                 202));

    check("job replacement resets first-attempt accounting",
          tracker.onValidJob(session1, 203) != 0);
    const uint64_t job3 = tracker.snapshot(203, configured).jobGeneration;
    check("third job accepts its first completed attempt",
          tracker.onAttemptCompleted(job3, 204));
    const uint64_t candidate3 = tracker.onCandidate(job3, 205);
    const uint64_t job4 = tracker.onValidJob(session1, 206);
    snap = tracker.snapshot(206, configured);
    check("unsent candidate is retired by a replacement job",
          candidate3 != 0 && job4 != 0 &&
              snap.candidate == PearlCandidateState::Idle &&
              !tracker.onCandidateFinalized(candidate3, true, 207));
    check("retiring invalid work preserves authorization and awaits a job",
          tracker.onJobRetired(session1, 207));
    snap = tracker.snapshot(207, configured);
    check("retired work has no active attempt deadline",
          snap.connection == PearlConnectionState::AwaitingJob &&
              snap.jobProgress == PearlJobProgressState::NoJob &&
              snap.attemptDeadline.state ==
                  PearlDeadlineState::NotApplicable &&
              !tracker.onAttemptCompleted(job4, 208));

    check("disconnect rejects a stale session token",
          !tracker.onDisconnect(session1 + 1, 208));
    check("disconnect retires current job and candidate state",
          tracker.onDisconnect(session1, 208));
    snap = tracker.snapshot(208, configured);
    check("disconnected snapshot has no active deadlines",
          snap.connection == PearlConnectionState::Disconnected &&
              snap.jobProgress == PearlJobProgressState::NoJob &&
              snap.connectionDeadline.state ==
                  PearlDeadlineState::NotApplicable &&
              snap.attemptDeadline.state ==
                  PearlDeadlineState::NotApplicable);

    const uint64_t session2 = tracker.beginSession(300);
    check("generations do not repeat across transport sessions",
          session2 > session1);
    check("clock rollback cannot create a fresh event",
          !tracker.onAuthorization(session2, 299));
    snap = tracker.snapshot(299, configured);
    check("a backwards query is reported and clamped to the event clock",
          !snap.queryTimeValid &&
              snap.connectionDeadline.state == PearlDeadlineState::Fresh &&
              snap.connectionDeadline.atMs == 310);
    PearlLivenessDeadlines disabled;
    snap = tracker.snapshot(300, disabled);
    check("zero durations explicitly disable applicable deadlines",
          snap.connectionDeadline.state == PearlDeadlineState::Disabled);

    check("second session authorizes after the rejected clock rollback",
          tracker.onAuthorization(session2, 301));
    const uint64_t gatewayJob = tracker.onValidJob(session2, 302);
    check("acknowledgement-only path receives active work",
          gatewayJob != 0 && tracker.onAttemptCompleted(gatewayJob, 303));
    const uint64_t gatewayCandidate = tracker.onCandidate(gatewayJob, 304);
    check("acknowledgement-only path finalizes a candidate",
          gatewayCandidate != 0 &&
              tracker.onCandidateFinalized(gatewayCandidate, true, 305));
    const uint64_t gatewaySubmit =
        tracker.onSubmitAttempt(gatewayCandidate, 306);
    check("terminal acknowledgement closes the candidate pipeline",
          gatewaySubmit != 0 &&
              tracker.onAcknowledgement(gatewaySubmit, 307, true));
    snap = tracker.snapshot(307, configured);
    check("terminal acknowledgement is fresh without inventing acceptance",
          snap.candidate == PearlCandidateState::Idle &&
              snap.lastOutcome == PearlSubmitOutcome::Acknowledged &&
              snap.lastOutcomeEvidence ==
                  PearlOutcomeEvidence::GatewayAcknowledgementOnly &&
              snap.lastOutcomeSubmitGeneration == gatewaySubmit &&
              snap.outcomeFreshness.state == PearlDeadlineState::Fresh &&
              snap.acknowledgements == 2 && snap.submissionFailures == 1 &&
              snap.possiblyDeliveredUnknowns == 0 &&
              snap.unacknowledgedUnknowns == 1 && snap.outcomes == 4 &&
              snap.submissionAccountingHolds());

    PearlLivenessSnapshot impossible = snap;
    impossible.acknowledgements = impossible.submitAttempts + 1;
    check("acknowledgements cannot exceed submit attempts",
          !impossible.submissionAccountingHolds());
    impossible = snap;
    impossible.outcomes = impossible.submissionFailures;
    check("terminal local classes must be represented in outcomes",
          !impossible.submissionAccountingHolds());
    impossible = snap;
    impossible.possiblyDeliveredUnknowns = impossible.submitAttempts;
    check("possibly-delivered terminals are exclusive first observations",
          !impossible.submissionAccountingHolds());
    impossible = snap;
    impossible.unacknowledgedUnknowns = impossible.submitAttempts;
    check("first-observation classes are mutually exclusive",
          !impossible.submissionAccountingHolds());

    PearlLivenessTracker acknowledgedUnknown;
    const uint64_t auSession = acknowledgedUnknown.beginSession(400);
    check("acknowledged-unknown fixture authorizes",
          auSession != 0 &&
              acknowledgedUnknown.onAuthorization(auSession, 401));
    const uint64_t auJob = acknowledgedUnknown.onValidJob(auSession, 402);
    check("acknowledged-unknown fixture gets active work",
          auJob != 0 && acknowledgedUnknown.onAttemptCompleted(auJob, 403));
    const uint64_t auCandidate = acknowledgedUnknown.onCandidate(auJob, 404);
    check("acknowledged-unknown fixture finalizes",
          auCandidate != 0 &&
              acknowledgedUnknown.onCandidateFinalized(auCandidate, true,
                                                        405));
    const uint64_t auSubmit =
        acknowledgedUnknown.onSubmitAttempt(auCandidate, 406);
    check("correlated unknown remains distinct from lost reply",
          auSubmit != 0 &&
              acknowledgedUnknown.onAcknowledgement(auSubmit, 407) &&
              acknowledgedUnknown.onOutcome(
                  auSubmit, PearlSubmitOutcome::Unknown, 408));
    snap = acknowledgedUnknown.snapshot(408, configured);
    check("acknowledged unknown sets acknowledgement evidence only once",
          snap.lastOutcome == PearlSubmitOutcome::Unknown &&
              snap.lastOutcomeEvidence ==
                  PearlOutcomeEvidence::CorrelatedRemoteOutcome &&
              snap.acknowledgements == 1 &&
              snap.unacknowledgedUnknowns == 0 &&
              snap.submissionFailures == 0 && snap.outcomes == 1 &&
              snap.submissionAccountingHolds());

    PearlLivenessTracker readySession;
    const uint64_t readySessionToken = readySession.beginSession(500);
    check("ready-session fixture reaches a valid job",
          readySessionToken != 0 &&
              readySession.onAuthorization(readySessionToken, 501) &&
              readySession.onValidJob(readySessionToken, 502) != 0);
    const PearlLivenessSnapshot readyBefore =
        readySession.snapshot(502, configured);
    check("beginSession refuses Ready without any mutation",
          readySession.beginSession(550) == 0 &&
              sameSnapshot(readyBefore,
                           readySession.snapshot(502, configured)));

    PearlLivenessTracker localFailure;
    uint64_t localSession = 0;
    uint64_t localSubmit = 0;
    check("local-failure fixture reaches acknowledgement wait",
          reachAwaitingAcknowledgement(&localFailure, 600, &localSession,
                                       &localSubmit));
    const PearlLivenessSnapshot awaitingAckBefore =
        localFailure.snapshot(606, configured);
    check("beginSession refuses AwaitingAcknowledgement without mutation",
          localFailure.beginSession(650) == 0 &&
              sameSnapshot(awaitingAckBefore,
                           localFailure.snapshot(606, configured)));
    check("disconnect refuses AwaitingAcknowledgement without mutation",
          !localFailure.onDisconnect(localSession, 651) &&
              sameSnapshot(awaitingAckBefore,
                           localFailure.snapshot(606, configured)));
    check("explicit local failure permits disconnect",
          localFailure.onSubmissionFailed(localSubmit, 607) &&
              !localFailure.onSubmissionFailed(localSubmit, 607) &&
              !localFailure.onPossiblyDeliveredUnknown(localSubmit, 607) &&
              !localFailure.onDeliveryUnknown(localSubmit, 607) &&
              !localFailure.onAcknowledgement(localSubmit, 607) &&
              !localFailure.onOutcome(localSubmit,
                                      PearlSubmitOutcome::Rejected, 607) &&
              localFailure.onDisconnect(localSession, 608));

    PearlLivenessTracker deliveryUnknown;
    uint64_t deliverySession = 0;
    uint64_t deliverySubmit = 0;
    check("delivery-unknown fixture reaches acknowledgement wait",
          reachAwaitingAcknowledgement(&deliveryUnknown, 700,
                                       &deliverySession, &deliverySubmit));
    const PearlLivenessSnapshot deliveryBefore =
        deliveryUnknown.snapshot(706, configured);
    check("backwards delivery-unknown event is refused without mutation",
          !deliveryUnknown.onDeliveryUnknown(deliverySubmit, 705) &&
              sameSnapshot(deliveryBefore,
                           deliveryUnknown.snapshot(706, configured)));
    check("fully-sent delivery unknown permits disconnect",
          deliveryUnknown.onDeliveryUnknown(deliverySubmit, 707) &&
              !deliveryUnknown.onSubmissionFailed(deliverySubmit, 707) &&
              !deliveryUnknown.onPossiblyDeliveredUnknown(deliverySubmit,
                                                          707) &&
              !deliveryUnknown.onDeliveryUnknown(deliverySubmit, 707) &&
              !deliveryUnknown.onAcknowledgement(deliverySubmit, 707) &&
              !deliveryUnknown.onOutcome(deliverySubmit,
                                         PearlSubmitOutcome::Unknown, 707) &&
              deliveryUnknown.onDisconnect(deliverySession, 708));

    PearlLivenessTracker possiblyDelivered;
    uint64_t possiblyDeliveredSession = 0;
    uint64_t possiblyDeliveredSubmit = 0;
    check("possibly-delivered fixture reaches acknowledgement wait",
          reachAwaitingAcknowledgement(&possiblyDelivered, 720,
                                       &possiblyDeliveredSession,
                                       &possiblyDeliveredSubmit));
    const PearlLivenessSnapshot possiblyDeliveredBefore =
        possiblyDelivered.snapshot(726, configured);
    check("possibly-delivered unknown rejects stale, future, and backwards events",
          !possiblyDelivered.onPossiblyDeliveredUnknown(
              possiblyDeliveredSubmit - 1, 727) &&
              !possiblyDelivered.onPossiblyDeliveredUnknown(
                  possiblyDeliveredSubmit + 1, 727) &&
              !possiblyDelivered.onPossiblyDeliveredUnknown(
                  possiblyDeliveredSubmit, 725) &&
              sameSnapshot(possiblyDeliveredBefore,
                           possiblyDelivered.snapshot(726, configured)));
    check("possibly-delivered unknown closes the exact submit token",
          possiblyDelivered.onPossiblyDeliveredUnknown(
              possiblyDeliveredSubmit, 727));
    snap = possiblyDelivered.snapshot(746, configured);
    check("possibly-delivered evidence is fresh and conserves one terminal",
          snap.candidate == PearlCandidateState::Idle &&
              snap.lastOutcome == PearlSubmitOutcome::Unknown &&
              snap.lastOutcomeEvidence ==
                  PearlOutcomeEvidence::PossiblyDeliveredUnknown &&
              snap.lastOutcomeSubmitGeneration == possiblyDeliveredSubmit &&
              snap.submitAttempts == 1 && snap.submissionFailures == 0 &&
              snap.possiblyDeliveredUnknowns == 1 &&
              snap.unacknowledgedUnknowns == 0 &&
              snap.acknowledgements == 0 && snap.outcomes == 1 &&
              snap.outcomeFreshness.state == PearlDeadlineState::Fresh &&
              snap.outcomeFreshness.atMs == 747 &&
              snap.submissionAccountingHolds() && snap.outcomeEvidenceHolds());
    snap = possiblyDelivered.snapshot(747, configured);
    check("possibly-delivered outcome freshness expires at its exact boundary",
          snap.outcomeFreshness.state == PearlDeadlineState::Expired);
    const PearlLivenessSnapshot possiblyDeliveredTerminal = snap;
    check("possibly-delivered terminal evidence rejects every second terminal",
          !possiblyDelivered.onSubmissionFailed(possiblyDeliveredSubmit, 747) &&
              !possiblyDelivered.onPossiblyDeliveredUnknown(
                  possiblyDeliveredSubmit, 747) &&
              !possiblyDelivered.onDeliveryUnknown(possiblyDeliveredSubmit,
                                                    747) &&
              !possiblyDelivered.onAcknowledgement(possiblyDeliveredSubmit,
                                                    747) &&
              !possiblyDelivered.onAcknowledgement(possiblyDeliveredSubmit,
                                                    747, true) &&
              !possiblyDelivered.onOutcome(possiblyDeliveredSubmit,
                                           PearlSubmitOutcome::Rejected, 747) &&
              !possiblyDelivered.onOutcomeUnavailable(
                  possiblyDeliveredSubmit, 747) &&
              sameSnapshot(possiblyDeliveredTerminal,
                           possiblyDelivered.snapshot(747, configured)));
    check("disconnect preserves possibly-delivered terminal evidence",
          possiblyDelivered.onDisconnect(possiblyDeliveredSession, 748));
    snap = possiblyDelivered.snapshot(748, configured);
    check("disconnect does not rewrite possibly-delivered evidence",
          snap.lastOutcome == PearlSubmitOutcome::Unknown &&
              snap.lastOutcomeEvidence ==
                  PearlOutcomeEvidence::PossiblyDeliveredUnknown &&
              snap.possiblyDeliveredUnknowns == 1 && snap.outcomes == 1 &&
              snap.submissionAccountingHolds() && snap.outcomeEvidenceHolds());
    const uint64_t nextPossiblyDeliveredSession =
        possiblyDelivered.beginSession(760);
    snap = possiblyDelivered.snapshot(760, configured);
    check("new session clears transient evidence and preserves partial-delivery totals",
          nextPossiblyDeliveredSession > possiblyDeliveredSession &&
              snap.lastOutcome == PearlSubmitOutcome::None &&
              snap.lastOutcomeEvidence == PearlOutcomeEvidence::None &&
              snap.lastOutcomeSubmitGeneration == 0 &&
              snap.possiblyDeliveredUnknowns == 1 && snap.submitAttempts == 1 &&
              snap.outcomes == 1 &&
              snap.outcomeFreshness.state ==
                  PearlDeadlineState::NotApplicable &&
              snap.submissionAccountingHolds() && snap.outcomeEvidenceHolds());
    check("new-session partial fixture reaches a newer submit token",
          possiblyDelivered.onAuthorization(nextPossiblyDeliveredSession,
                                            761));
    const uint64_t nextPossiblyDeliveredJob =
        possiblyDelivered.onValidJob(nextPossiblyDeliveredSession, 762);
    check("new-session partial fixture activates work",
          nextPossiblyDeliveredJob != 0 &&
              possiblyDelivered.onAttemptCompleted(nextPossiblyDeliveredJob,
                                                    763));
    const uint64_t nextPossiblyDeliveredCandidate =
        possiblyDelivered.onCandidate(nextPossiblyDeliveredJob, 764);
    check("new-session partial fixture reaches submission",
          nextPossiblyDeliveredCandidate != 0 &&
              possiblyDelivered.onCandidateFinalized(
                  nextPossiblyDeliveredCandidate, true, 765));
    const uint64_t nextPossiblyDeliveredSubmit =
        possiblyDelivered.onSubmitAttempt(nextPossiblyDeliveredCandidate,
                                          766);
    const PearlLivenessSnapshot nextPossiblyDeliveredBefore =
        possiblyDelivered.snapshot(766, configured);
    check("prior-session submit token cannot close current submission",
          nextPossiblyDeliveredSubmit > possiblyDeliveredSubmit &&
              !possiblyDelivered.onPossiblyDeliveredUnknown(
                  possiblyDeliveredSubmit, 767) &&
              sameSnapshot(nextPossiblyDeliveredBefore,
                           possiblyDelivered.snapshot(766, configured)));
    check("new-session exact token remains legal after stale-token refusal",
          possiblyDelivered.onPossiblyDeliveredUnknown(
              nextPossiblyDeliveredSubmit, 767));
    snap = possiblyDelivered.snapshot(767, configured);
    check("partial-delivery counters conserve across sessions",
          snap.submitAttempts == 2 && snap.possiblyDeliveredUnknowns == 2 &&
              snap.unacknowledgedUnknowns == 0 &&
              snap.submissionFailures == 0 && snap.acknowledgements == 0 &&
              snap.outcomes == 2 && snap.submissionAccountingHolds() &&
              snap.outcomeEvidenceHolds());

    PearlLivenessTracker awaitingOutcome;
    uint64_t outcomeSession = 0;
    uint64_t outcomeSubmit = 0;
    check("outcome fixture reaches acknowledgement wait",
          reachAwaitingAcknowledgement(&awaitingOutcome, 800,
                                       &outcomeSession, &outcomeSubmit) &&
              awaitingOutcome.onAcknowledgement(outcomeSubmit, 807));
    const PearlLivenessSnapshot awaitingOutcomeBefore =
        awaitingOutcome.snapshot(807, configured);
    check("acknowledged pending submit has valid conservation",
          awaitingOutcomeBefore.candidate ==
                  PearlCandidateState::AwaitingOutcome &&
              awaitingOutcomeBefore.submitAttempts == 1 &&
              awaitingOutcomeBefore.acknowledgements == 1 &&
              awaitingOutcomeBefore.outcomes == 0 &&
              awaitingOutcomeBefore.submissionAccountingHolds());
    check("beginSession refuses AwaitingOutcome without mutation",
          awaitingOutcome.beginSession(850) == 0 &&
              sameSnapshot(awaitingOutcomeBefore,
                           awaitingOutcome.snapshot(807, configured)));
    check("disconnect preserves an acknowledged pending outcome",
          !awaitingOutcome.onDisconnect(outcomeSession, 851) &&
              sameSnapshot(awaitingOutcomeBefore,
                           awaitingOutcome.snapshot(807, configured)));
    check("outcome-unavailable rejects stale token and backwards time",
          !awaitingOutcome.onOutcomeUnavailable(outcomeSubmit + 1, 808) &&
              !awaitingOutcome.onOutcomeUnavailable(outcomeSubmit, 806) &&
              sameSnapshot(awaitingOutcomeBefore,
                           awaitingOutcome.snapshot(807, configured)));
    snap = awaitingOutcome.snapshot(817, configured);
    check("missing final verdict reaches its distinct deadline",
          snap.candidate == PearlCandidateState::AwaitingOutcome &&
              snap.candidateDeadline.state == PearlDeadlineState::Expired &&
              snap.candidateDeadline.atMs == 817);
    check("explicit unavailable final verdict permits disconnect",
          awaitingOutcome.onOutcomeUnavailable(outcomeSubmit, 817));
    snap = awaitingOutcome.snapshot(817, configured);
    check("unavailable verdict is acknowledged terminal unknown",
          snap.candidate == PearlCandidateState::Idle &&
              snap.lastOutcome == PearlSubmitOutcome::Unknown &&
              snap.lastOutcomeEvidence ==
                  PearlOutcomeEvidence::AcknowledgedOutcomeUnavailable &&
              snap.acknowledgements == 1 && snap.outcomes == 1 &&
              snap.submissionFailures == 0 &&
              snap.unacknowledgedUnknowns == 0 &&
              snap.outcomeFreshness.state == PearlDeadlineState::Fresh &&
              snap.submissionAccountingHolds());
    check("unavailable verdict rejects every second terminal event",
          !awaitingOutcome.onSubmissionFailed(outcomeSubmit, 817) &&
              !awaitingOutcome.onPossiblyDeliveredUnknown(outcomeSubmit,
                                                          817) &&
              !awaitingOutcome.onDeliveryUnknown(outcomeSubmit, 817) &&
              !awaitingOutcome.onAcknowledgement(outcomeSubmit, 817) &&
              !awaitingOutcome.onOutcome(outcomeSubmit,
                                         PearlSubmitOutcome::Accepted, 817) &&
              !awaitingOutcome.onOutcomeUnavailable(outcomeSubmit, 817) &&
              awaitingOutcome.onDisconnect(outcomeSession, 818));
    const PearlLivenessSnapshot completedSession =
        awaitingOutcome.snapshot(818, configured);
    const uint64_t nextOutcomeSession = awaitingOutcome.beginSession(900);
    const PearlLivenessSnapshot resetSession =
        awaitingOutcome.snapshot(900, configured);
    check("new session resets transient state and preserves cumulative totals",
          nextOutcomeSession > outcomeSession &&
              resetSession.connection ==
                  PearlConnectionState::AwaitingAuthorization &&
              resetSession.jobProgress == PearlJobProgressState::NoJob &&
              resetSession.candidate == PearlCandidateState::Idle &&
              resetSession.lastOutcome == PearlSubmitOutcome::None &&
              resetSession.lastOutcomeEvidence == PearlOutcomeEvidence::None &&
              resetSession.lastOutcomeSubmitGeneration == 0 &&
              resetSession.outcomeFreshness.state ==
                  PearlDeadlineState::NotApplicable &&
              resetSession.completedAttemptsForJob == 0 &&
              resetSession.jobGeneration == completedSession.jobGeneration &&
              resetSession.candidateGeneration ==
                  completedSession.candidateGeneration &&
              resetSession.submitGeneration ==
                  completedSession.submitGeneration &&
              resetSession.completedAttempts ==
                  completedSession.completedAttempts &&
              resetSession.submitAttempts == completedSession.submitAttempts &&
              resetSession.acknowledgements ==
                  completedSession.acknowledgements &&
              resetSession.outcomes == completedSession.outcomes &&
              resetSession.submissionAccountingHolds());

    PearlLivenessTracker terminalAcknowledgement;
    uint64_t terminalSession = 0;
    uint64_t terminalSubmit = 0;
    check("terminal-acknowledgement fixture reaches submit",
          reachAwaitingAcknowledgement(&terminalAcknowledgement, 1000,
                                       &terminalSession, &terminalSubmit));
    check("terminal acknowledgement rejects every second terminal event",
          terminalAcknowledgement.onAcknowledgement(terminalSubmit, 1007,
                                                    true) &&
              !terminalAcknowledgement.onSubmissionFailed(terminalSubmit,
                                                          1007) &&
              !terminalAcknowledgement.onPossiblyDeliveredUnknown(
                  terminalSubmit, 1007) &&
              !terminalAcknowledgement.onDeliveryUnknown(terminalSubmit,
                                                         1007) &&
              !terminalAcknowledgement.onAcknowledgement(terminalSubmit,
                                                         1007, true) &&
              !terminalAcknowledgement.onOutcome(
                  terminalSubmit, PearlSubmitOutcome::Accepted, 1007));

    const PearlSubmitOutcome remoteTerminalOutcomes[] = {
        PearlSubmitOutcome::Accepted, PearlSubmitOutcome::Rejected,
        PearlSubmitOutcome::Stale, PearlSubmitOutcome::Unknown};
    bool remoteTerminalMatrixHolds = true;
    for (uint64_t i = 0; i < 4; ++i) {
        PearlLivenessTracker remoteTerminal;
        uint64_t remoteSession = 0;
        uint64_t remoteSubmit = 0;
        const uint64_t base = 1100 + i * 20;
        remoteTerminalMatrixHolds =
            remoteTerminalMatrixHolds &&
            reachAwaitingAcknowledgement(&remoteTerminal, base,
                                         &remoteSession, &remoteSubmit) &&
            remoteTerminal.onAcknowledgement(remoteSubmit, base + 7) &&
            remoteTerminal.onOutcome(remoteSubmit, remoteTerminalOutcomes[i],
                                     base + 8);
        const PearlLivenessSnapshot terminalBeforeSecond =
            remoteTerminal.snapshot(base + 8, configured);
        remoteTerminalMatrixHolds =
            remoteTerminalMatrixHolds &&
            terminalBeforeSecond.candidate == PearlCandidateState::Idle &&
            terminalBeforeSecond.lastOutcome == remoteTerminalOutcomes[i] &&
            terminalBeforeSecond.lastOutcomeEvidence ==
                PearlOutcomeEvidence::CorrelatedRemoteOutcome &&
            terminalBeforeSecond.lastOutcomeSubmitGeneration == remoteSubmit &&
            terminalBeforeSecond.submitAttempts == 1 &&
            terminalBeforeSecond.acknowledgements == 1 &&
            terminalBeforeSecond.outcomes == 1 &&
            terminalBeforeSecond.submissionAccountingHolds() &&
            terminalBeforeSecond.outcomeEvidenceHolds() &&
            !remoteTerminal.onSubmissionFailed(remoteSubmit, base + 8) &&
            !remoteTerminal.onPossiblyDeliveredUnknown(remoteSubmit,
                                                       base + 8) &&
            !remoteTerminal.onDeliveryUnknown(remoteSubmit, base + 8) &&
            !remoteTerminal.onAcknowledgement(remoteSubmit, base + 8, true) &&
            !remoteTerminal.onOutcome(remoteSubmit,
                                      remoteTerminalOutcomes[i], base + 8) &&
            !remoteTerminal.onOutcomeUnavailable(remoteSubmit, base + 8) &&
            sameSnapshot(terminalBeforeSecond,
                         remoteTerminal.snapshot(base + 8, configured));
    }
    check("every remote terminal outcome rejects every second terminal event",
          remoteTerminalMatrixHolds);

    const uint64_t maxCount = std::numeric_limits<uint64_t>::max();
    uint64_t saturatedIncrement = maxCount;
    pearlLivenessSaturatingIncrement(&saturatedIncrement);
    check("liveness counter increment saturates instead of wrapping",
          saturatedIncrement == maxCount);

    PearlLivenessSnapshot invalidEvidence;
    invalidEvidence.lastOutcome = PearlSubmitOutcome::Accepted;
    invalidEvidence.lastOutcomeEvidence =
        PearlOutcomeEvidence::FullySentNoReply;
    invalidEvidence.lastOutcomeSubmitGeneration = 1;
    invalidEvidence.submitGeneration = 1;
    check("accepted outcome cannot claim no-reply evidence",
          !invalidEvidence.outcomeEvidenceHolds());
    invalidEvidence.lastOutcomeEvidence =
        PearlOutcomeEvidence::PossiblyDeliveredUnknown;
    check("accepted outcome cannot claim possibly-delivered evidence",
          !invalidEvidence.outcomeEvidenceHolds());
    invalidEvidence.lastOutcomeEvidence =
        PearlOutcomeEvidence::CorrelatedRemoteOutcome;
    check("accepted outcome requires correlated remote evidence",
          invalidEvidence.outcomeEvidenceHolds());
    invalidEvidence.lastOutcome = PearlSubmitOutcome::None;
    check("no outcome cannot retain terminal evidence or token",
          !invalidEvidence.outcomeEvidenceHolds());

    const PearlSubmitOutcome evidenceOutcomes[] = {
        PearlSubmitOutcome::None,
        PearlSubmitOutcome::SubmissionFailed,
        PearlSubmitOutcome::Acknowledged,
        PearlSubmitOutcome::Accepted,
        PearlSubmitOutcome::Rejected,
        PearlSubmitOutcome::Stale,
        PearlSubmitOutcome::Unknown};
    const PearlOutcomeEvidence evidenceKinds[] = {
        PearlOutcomeEvidence::None,
        PearlOutcomeEvidence::LocalPreDeliveryFailure,
        PearlOutcomeEvidence::PossiblyDeliveredUnknown,
        PearlOutcomeEvidence::FullySentNoReply,
        PearlOutcomeEvidence::GatewayAcknowledgementOnly,
        PearlOutcomeEvidence::CorrelatedRemoteOutcome,
        PearlOutcomeEvidence::AcknowledgedOutcomeUnavailable};
    bool evidenceMatrixHolds = true;
    for (const PearlSubmitOutcome outcome : evidenceOutcomes) {
        for (const PearlOutcomeEvidence evidence : evidenceKinds) {
            const bool legal =
                (outcome == PearlSubmitOutcome::None &&
                 evidence == PearlOutcomeEvidence::None) ||
                (outcome == PearlSubmitOutcome::SubmissionFailed &&
                 evidence == PearlOutcomeEvidence::LocalPreDeliveryFailure) ||
                (outcome == PearlSubmitOutcome::Acknowledged &&
                 evidence ==
                     PearlOutcomeEvidence::GatewayAcknowledgementOnly) ||
                ((outcome == PearlSubmitOutcome::Accepted ||
                  outcome == PearlSubmitOutcome::Rejected ||
                  outcome == PearlSubmitOutcome::Stale) &&
                 evidence == PearlOutcomeEvidence::CorrelatedRemoteOutcome) ||
                (outcome == PearlSubmitOutcome::Unknown &&
                 (evidence ==
                      PearlOutcomeEvidence::PossiblyDeliveredUnknown ||
                  evidence == PearlOutcomeEvidence::FullySentNoReply ||
                  evidence == PearlOutcomeEvidence::CorrelatedRemoteOutcome ||
                  evidence ==
                      PearlOutcomeEvidence::AcknowledgedOutcomeUnavailable));
            PearlLivenessSnapshot evidenceSnapshot;
            evidenceSnapshot.lastOutcome = outcome;
            evidenceSnapshot.lastOutcomeEvidence = evidence;
            evidenceSnapshot.lastOutcomeSubmitGeneration =
                outcome == PearlSubmitOutcome::None ? 0 : 1;
            evidenceSnapshot.submitGeneration = 1;
            evidenceMatrixHolds =
                evidenceMatrixHolds &&
                evidenceSnapshot.outcomeEvidenceHolds() == legal;
        }
    }
    check("every outcome/evidence tuple is classified",
          evidenceMatrixHolds);
    PearlLivenessSnapshot futureEvidenceOwner;
    futureEvidenceOwner.lastOutcome = PearlSubmitOutcome::Accepted;
    futureEvidenceOwner.lastOutcomeEvidence =
        PearlOutcomeEvidence::CorrelatedRemoteOutcome;
    futureEvidenceOwner.lastOutcomeSubmitGeneration = 2;
    futureEvidenceOwner.submitGeneration = 1;
    check("terminal evidence cannot claim a future submit owner",
          !futureEvidenceOwner.outcomeEvidenceHolds());
    PearlLivenessSnapshot ownerlessPartialEvidence;
    ownerlessPartialEvidence.lastOutcome = PearlSubmitOutcome::Unknown;
    ownerlessPartialEvidence.lastOutcomeEvidence =
        PearlOutcomeEvidence::PossiblyDeliveredUnknown;
    ownerlessPartialEvidence.submitGeneration = 1;
    check("possibly-delivered evidence requires a nonzero submit owner",
          !ownerlessPartialEvidence.outcomeEvidenceHolds());

    PearlLivenessSnapshot saturatedAccounting;
    saturatedAccounting.submitAttempts = maxCount;
    saturatedAccounting.submissionFailures = maxCount / 4;
    saturatedAccounting.possiblyDeliveredUnknowns = maxCount / 4;
    saturatedAccounting.unacknowledgedUnknowns = maxCount / 4;
    saturatedAccounting.acknowledgements =
        maxCount - saturatedAccounting.submissionFailures -
        saturatedAccounting.possiblyDeliveredUnknowns -
        saturatedAccounting.unacknowledgedUnknowns;
    saturatedAccounting.outcomes = maxCount;
    check("submission conservation remains valid at saturated counters",
          saturatedAccounting.submissionAccountingHolds());

    PearlLivenessSnapshot saturatedPending;
    saturatedPending.submitAttempts = maxCount;
    saturatedPending.acknowledgements = maxCount;
    saturatedPending.outcomes = maxCount - 1;
    check("max-scale acknowledged pending accounting remains valid",
          saturatedPending.submissionAccountingHolds());

    PearlLivenessSnapshot saturatedFirstObservationConflict;
    saturatedFirstObservationConflict.submitAttempts = maxCount;
    saturatedFirstObservationConflict.submissionFailures = maxCount;
    saturatedFirstObservationConflict.acknowledgements = 1;
    saturatedFirstObservationConflict.outcomes = maxCount;
    check("max-scale mutually exclusive first observations remain invalid",
          !saturatedFirstObservationConflict.submissionAccountingHolds());

    PearlLivenessSnapshot saturatedUnbackedOutcome;
    saturatedUnbackedOutcome.submitAttempts = maxCount;
    saturatedUnbackedOutcome.acknowledgements = maxCount - 1;
    saturatedUnbackedOutcome.outcomes = maxCount;
    check("max-scale unbacked terminal outcome remains invalid",
          !saturatedUnbackedOutcome.submissionAccountingHolds());

    PearlLivenessSnapshot saturatedPartialConflict;
    saturatedPartialConflict.submitAttempts = maxCount;
    saturatedPartialConflict.possiblyDeliveredUnknowns = maxCount;
    saturatedPartialConflict.unacknowledgedUnknowns = 1;
    saturatedPartialConflict.outcomes = maxCount;
    check("max-scale partial and full unknown classes cannot overlap",
          !saturatedPartialConflict.submissionAccountingHolds());

    PearlLivenessSnapshot partialOutcomeDeficit;
    partialOutcomeDeficit.submitAttempts = 1;
    partialOutcomeDeficit.possiblyDeliveredUnknowns = 1;
    check("possibly-delivered terminal must be represented in outcomes",
          !partialOutcomeDeficit.submissionAccountingHolds());

    PearlLivenessTracker saturatingPartialFreshness;
    uint64_t saturatingPartialSession = 0;
    uint64_t saturatingPartialSubmit = 0;
    const uint64_t partialBase = maxCount - 12;
    check("near-max partial-delivery fixture reaches acknowledgement wait",
          reachAwaitingAcknowledgement(&saturatingPartialFreshness,
                                       partialBase,
                                       &saturatingPartialSession,
                                       &saturatingPartialSubmit) &&
              saturatingPartialFreshness.onPossiblyDeliveredUnknown(
                  saturatingPartialSubmit, maxCount - 5));
    snap = saturatingPartialFreshness.snapshot(maxCount - 1, configured);
    check("possibly-delivered freshness saturates without wrapping",
          snap.lastOutcomeEvidence ==
                  PearlOutcomeEvidence::PossiblyDeliveredUnknown &&
              snap.possiblyDeliveredUnknowns == 1 && snap.outcomes == 1 &&
              snap.outcomeFreshness.atMs == maxCount &&
              snap.outcomeFreshness.state == PearlDeadlineState::Fresh &&
              snap.submissionAccountingHolds() && snap.outcomeEvidenceHolds());
    snap = saturatingPartialFreshness.snapshot(maxCount, configured);
    check("saturated possibly-delivered freshness expires at UINT64_MAX",
          snap.outcomeFreshness.state == PearlDeadlineState::Expired);

    PearlLivenessTracker saturating;
    const uint64_t nearMax = std::numeric_limits<uint64_t>::max() - 5;
    const uint64_t maxSession = saturating.beginSession(nearMax);
    snap = saturating.snapshot(std::numeric_limits<uint64_t>::max() - 1,
                               configured);
    check("deadline addition saturates instead of wrapping",
          maxSession != 0 && snap.connectionDeadline.atMs ==
                                 std::numeric_limits<uint64_t>::max() &&
              snap.connectionDeadline.state == PearlDeadlineState::Fresh);
    snap = saturating.snapshot(std::numeric_limits<uint64_t>::max(), configured);
    check("a saturated deadline still expires at UINT64_MAX",
          snap.connectionDeadline.state == PearlDeadlineState::Expired);

    if (failures) {
        std::printf("FAIL: %d Pearl liveness-state checks failed\n", failures);
        return 1;
    }
    std::puts("PASS: Pearl lifecycle states and deadlines are deterministic");
    return 0;
}
