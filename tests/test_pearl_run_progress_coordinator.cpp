#include "../src/core/pearl_run_progress_coordinator.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace om;

namespace {

int failures = 0;

void check(const char *name, bool condition) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

struct ManualClock final : PearlSourceMonotonicClock {
    uint64_t value = 1;
    uint64_t nowMs() noexcept override { return value; }
    static uint64_t read(void *context) noexcept {
        return static_cast<ManualClock *>(context)->value;
    }
    uint64_t tick() { return ++value; }
};

PearlJobIdentityDigest digest(uint8_t seed) {
    PearlJobIdentityDigest out;
    for (std::size_t i = 0; i < out.bytes.size(); ++i)
        out.bytes[i] = static_cast<uint8_t>(seed + i);
    return out;
}

struct Scenario {
    ManualClock clock;
    PearlProgressBus bus;
    PearlSourceObservabilityMailbox mailbox;
    PearlRunProgressCoordinator coordinator;
    uint64_t session = 0;
    uint64_t authorization = 0;
    uint64_t job = 0;
    uint64_t submit = 0;
    uint64_t operation = 0;
    uint64_t nextSerializedOrdinal = 1;
    PearlJobIdentityDigest identity = digest(7);

    explicit Scenario(
        bool noAuth = false,
        const PearlProgressSnapshot &initial = PearlProgressSnapshot(),
        uint64_t jobGenerationLimit =
            std::numeric_limits<uint64_t>::max())
        : mailbox(clock),
          coordinator(&bus, &ManualClock::read, &clock, initial,
                      PearlLivenessDeadlines(), jobGenerationLimit) {
        check("attach real mailbox", coordinator.attachSource(&mailbox));
        session = mailbox.publishSessionStarted();
        coordinator.onSourcePublication();
        clock.tick();
        check("publish transport connected",
              mailbox.publishTransportConnected(session));
        coordinator.onSourcePublication();
        clock.tick();
        if (noAuth) {
            check("publish gateway readiness",
                  mailbox.publishSessionReadyNoAuth(session));
        } else {
            authorization = mailbox.publishAuthorizationSucceeded(session);
            check("publish authorization", authorization != 0);
        }
        coordinator.onSourcePublication();
        clock.tick();
        job = mailbox.publishJobAvailable(session, authorization, identity);
        check("publish job", job != 0);
        coordinator.onSourcePublication();
        const auto availableOnly = coordinator.snapshotForOwner();
        check("JobAvailable alone is not run-loop adoption",
              availableOnly.liveness.jobGeneration == 0 &&
                  availableOnly.liveness.connection ==
                      PearlConnectionState::AwaitingJob);
    }

    void prepareBoundJob() {
        check("start bound-job preparation",
              coordinator.onPrepareStarted());
        check("complete bound-job preparation",
              coordinator.onPrepareCompleted());
    }

    uint64_t observeCandidate() {
        const uint64_t candidate = coordinator.onCandidateObserved(
            session, authorization, job, identity,
            coordinator.snapshotForOwner().progress.searchGeneration,
            nextSerializedOrdinal);
        if (candidate != 0) ++nextSerializedOrdinal;
        return candidate;
    }

    void prepareSubmission(uint64_t expectedAttempts = 1) {
        check("bind mailbox issued job", mailbox.bindIssuedJob(session, job));
        check("bind coordinator issued job",
              coordinator.onIssuedJobBound(session, job, identity));
        prepareBoundJob();
        check("start run-loop search",
              coordinator.onSearchStarted(expectedAttempts));

        for (uint64_t i = 0; i < expectedAttempts; ++i) {
            clock.tick();
            coordinator.onPearlAlgorithmProgress(
                PearlAlgorithmProgressEvent::AttemptStarted);
            clock.tick();
            coordinator.onPearlAlgorithmProgress(
                PearlAlgorithmProgressEvent::AttemptCompleted);
            coordinator.onPearlAlgorithmProgress(
                PearlAlgorithmProgressEvent::HitsObserved);
            coordinator.onPearlAlgorithmProgress(
                PearlAlgorithmProgressEvent::HitsRetained);
            coordinator.onPearlAlgorithmProgress(
                PearlAlgorithmProgressEvent::FinalizerStarted);
            coordinator.onPearlAlgorithmProgress(
                PearlAlgorithmProgressEvent::CandidateSelected);
            coordinator.onPearlAlgorithmProgress(
                PearlAlgorithmProgressEvent::HostValid);
            coordinator.onPearlAlgorithmProgress(
                PearlAlgorithmProgressEvent::ProofSerialized);
        }
        check("complete run-loop search",
              coordinator.onSearchCompleted(true));
        const uint64_t candidate = observeCandidate();
        check("open sequential candidate", candidate != 0);
        check("finalize sequential candidate",
              coordinator.onCandidateFinalized(candidate, true));
        clock.tick();
        submit = coordinator.onSubmitAttempt();
        check("open local submit", submit != 0);
        operation = mailbox.publishSubmitDispatched(
            session, job, PearlSourceDelivery::NotSent);
        check("publish dispatch", operation != 0);
        coordinator.onSourcePublication();
    }

    void terminal(PearlSourceDelivery delivery,
                  PearlSourceTerminal terminal, bool acknowledged,
                  bool superseded = false,
                  PearlSourceTerminalEvidence evidence =
                      PearlSourceTerminalEvidence::None) {
        clock.tick();
        PearlSourceReason reason = PearlSourceReason::None;
        if (delivery == PearlSourceDelivery::NotSent ||
            delivery == PearlSourceDelivery::PossiblySent)
            reason = PearlSourceReason::Transport;
        else if (terminal == PearlSourceTerminal::Unknown && !acknowledged)
            reason = PearlSourceReason::Timeout;
        if (evidence == PearlSourceTerminalEvidence::None)
            evidence = pearlSourceCanonicalTerminalEvidence(
                delivery, terminal, acknowledged);
        check("publish terminal",
              mailbox.publishSubmitTerminal(
                  session, operation, delivery, terminal, acknowledged,
                  superseded, reason, evidence));
        coordinator.onSourcePublication();
    }
};

void primeOuterCandidate(Scenario &scenario) {
    check("candidate fixture mailbox bind",
          scenario.mailbox.bindIssuedJob(scenario.session, scenario.job));
    check("candidate fixture coordinator bind",
          scenario.coordinator.onIssuedJobBound(scenario.session,
                                                 scenario.job,
                                                 scenario.identity));
    scenario.prepareBoundJob();
    check("candidate fixture search start",
          scenario.coordinator.onSearchStarted(1));
    scenario.clock.tick();
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptStarted);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptCompleted);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsObserved);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsRetained);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::FinalizerStarted);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::CandidateSelected);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HostValid);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::ProofSerialized);
    check("candidate fixture search completion",
          scenario.coordinator.onSearchCompleted(true));
}

uint64_t prepareDirectSubmission(
    ManualClock &clock, PearlSourceObservabilityMailbox &mailbox,
    PearlRunProgressCoordinator &coordinator, uint64_t session,
    uint64_t authorization, uint64_t job,
    const PearlJobIdentityDigest &identity) {
    check("direct fixture source bind", mailbox.bindIssuedJob(session, job));
    check("direct fixture coordinator bind",
          coordinator.onIssuedJobBound(session, job, identity));
    check("direct fixture prepare start", coordinator.onPrepareStarted());
    check("direct fixture prepare complete", coordinator.onPrepareCompleted());
    check("direct fixture search start", coordinator.onSearchStarted(1));
    clock.tick();
    coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptStarted);
    coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptCompleted);
    coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsObserved);
    coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsRetained);
    coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::FinalizerStarted);
    coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::CandidateSelected);
    coordinator.onPearlAlgorithmProgress(PearlAlgorithmProgressEvent::HostValid);
    coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::ProofSerialized);
    check("direct fixture search complete", coordinator.onSearchCompleted(true));
    const uint64_t candidate = coordinator.onCandidateObserved(
        session, authorization, job, identity,
        coordinator.snapshotForOwner().progress.searchGeneration, 1);
    check("direct fixture candidate", candidate != 0);
    check("direct fixture candidate finalize",
          coordinator.onCandidateFinalized(candidate, true));
    const uint64_t submit = coordinator.onSubmitAttempt();
    check("direct fixture submit", submit != 0);
    const uint64_t operation = mailbox.publishSubmitDispatched(
        session, job, PearlSourceDelivery::NotSent);
    check("direct fixture dispatch", operation != 0);
    coordinator.onSourcePublication();
    return operation;
}

void testPoolLifecycleAndTerminalMappings() {
    struct Case {
        PearlSourceDelivery delivery;
        PearlSourceTerminal terminal;
        bool acknowledged;
        bool superseded;
        PearlSubmitOutcome outcome;
        PearlOutcomeEvidence evidence;
    };
    const Case cases[] = {
        {PearlSourceDelivery::NotSent, PearlSourceTerminal::LocalFailure,
         false, false, PearlSubmitOutcome::SubmissionFailed,
         PearlOutcomeEvidence::LocalPreDeliveryFailure},
        {PearlSourceDelivery::PossiblySent, PearlSourceTerminal::Unknown,
         false, false, PearlSubmitOutcome::Unknown,
         PearlOutcomeEvidence::PossiblyDeliveredUnknown},
        {PearlSourceDelivery::Sent, PearlSourceTerminal::Unknown, false,
         false, PearlSubmitOutcome::Unknown,
         PearlOutcomeEvidence::FullySentNoReply},
        {PearlSourceDelivery::Sent, PearlSourceTerminal::GatewayQueued, true,
         false, PearlSubmitOutcome::Acknowledged,
         PearlOutcomeEvidence::GatewayAcknowledgementOnly},
        {PearlSourceDelivery::Sent, PearlSourceTerminal::Accepted, true, false,
         PearlSubmitOutcome::Accepted,
         PearlOutcomeEvidence::CorrelatedRemoteOutcome},
        {PearlSourceDelivery::Sent, PearlSourceTerminal::Accepted, true, true,
         PearlSubmitOutcome::Accepted,
         PearlOutcomeEvidence::CorrelatedRemoteOutcome},
        {PearlSourceDelivery::Sent, PearlSourceTerminal::Rejected, true, false,
         PearlSubmitOutcome::Rejected,
         PearlOutcomeEvidence::CorrelatedRemoteOutcome},
        {PearlSourceDelivery::Sent, PearlSourceTerminal::Stale, true, false,
         PearlSubmitOutcome::Stale,
         PearlOutcomeEvidence::CorrelatedRemoteOutcome},
        {PearlSourceDelivery::Sent, PearlSourceTerminal::Unknown, true, false,
         PearlSubmitOutcome::Unknown,
         PearlOutcomeEvidence::AcknowledgedOutcomeUnavailable},
    };

    for (const Case &item : cases) {
        Scenario scenario(item.terminal == PearlSourceTerminal::GatewayQueued);
        scenario.prepareSubmission();
        const PearlSourceTerminalEvidence sourceEvidence =
            item.evidence ==
                    PearlOutcomeEvidence::AcknowledgedOutcomeUnavailable
                ? PearlSourceTerminalEvidence::AcknowledgedOutcomeUnavailable
                : pearlSourceCanonicalTerminalEvidence(
                      item.delivery, item.terminal, item.acknowledged);
        scenario.terminal(item.delivery, item.terminal, item.acknowledged,
                          item.superseded, sourceEvidence);
        const auto state = scenario.coordinator.snapshotForOwner();
        check("legal terminal does not quarantine", !state.sourceQuarantined);
        check("terminal maps exact outcome",
              state.liveness.lastOutcome == item.outcome);
        check("terminal maps exact evidence",
              state.liveness.lastOutcomeEvidence == item.evidence);
        check("liveness submission accounting holds",
              state.liveness.submissionAccountingHolds());
        check("liveness outcome evidence holds",
              state.liveness.outcomeEvidenceHolds());
        check("progress snapshot remains coherent",
              state.progress.invariantsHold());
        check("valid terminal raw evidence is retained as trusted",
              state.terminalEvidenceCount == 1 &&
                  state.terminalEvidence[0].trust ==
                      PearlRawTerminalTrust::Trusted &&
                  state.terminalEvidence[0].event.terminal == item.terminal &&
                  state.terminalEvidence[0].event.terminalEvidence ==
                      sourceEvidence);
        if (item.delivery == PearlSourceDelivery::PossiblySent)
            check("possibly sent is not fabricated as fully submitted",
                  state.progress.submitted == 0 &&
                      state.liveness.possiblyDeliveredUnknowns == 1);
        if (item.terminal == PearlSourceTerminal::GatewayQueued)
            check("gateway queue is acknowledgement-only",
                  state.progress.accepted == 0 &&
                      state.progress.acknowledged == 1 &&
                      state.progress.unknown == 1);
        if (item.terminal == PearlSourceTerminal::LocalFailure) {
            check("mirror issued release before source release",
                  scenario.coordinator.onIssuedJobReleased(scenario.session,
                                                            scenario.job));
            check("release source issued job",
                  scenario.mailbox.releaseIssuedJob(scenario.session,
                                                    scenario.job));
            scenario.coordinator.drainCheckpoint();
            scenario.clock.tick();
            check("retire terminal lifecycle job",
                  scenario.mailbox.publishJobRetired(
                      scenario.session, scenario.job,
                      PearlSourceReason::Shutdown));
            scenario.coordinator.onSourcePublication();
            scenario.clock.tick();
            check("disconnect terminal lifecycle session",
                  scenario.mailbox.publishDisconnected(
                      scenario.session, PearlSourceReason::Shutdown));
            scenario.coordinator.onSourcePublication();
            const auto afterDisconnect = scenario.coordinator.snapshotForOwner();
            check("valid pool lifecycle closes cleanly",
                  !afterDisconnect.sourceQuarantined &&
                      afterDisconnect.liveness.connection ==
                          PearlConnectionState::Disconnected);
        }
    }
}

void testReplacementDuringInflight() {
    Scenario scenario;
    scenario.prepareSubmission(2);
    const uint64_t oldJob = scenario.job;
    const PearlJobIdentityDigest oldDigest = scenario.identity;

    scenario.clock.tick();
    check("retire active job while old submit is live",
          scenario.mailbox.publishJobRetired(
              scenario.session, oldJob, PearlSourceReason::Stale));
    scenario.coordinator.onSourcePublication();
    scenario.clock.tick();
    const PearlJobIdentityDigest replacement = digest(91);
    const uint64_t newJob = scenario.mailbox.publishJobAvailable(
        scenario.session, scenario.authorization, replacement);
    check("publish replacement job", newJob != 0 && newJob != oldJob);
    scenario.coordinator.onSourcePublication();

    scenario.terminal(PearlSourceDelivery::Sent,
                      PearlSourceTerminal::Accepted, true, true);
    auto state = scenario.coordinator.snapshotForOwner();
    check("replacement keeps old operation identity valid",
          !state.sourceQuarantined && state.progress.accepted == 1 &&
              state.progress.acceptedSuperseded == 1);
    check("replacement does not mutate old digest variable",
          oldDigest.bytes == digest(7).bytes);

    // A second solution already returned by the old search remains owned by
    // the old issued job even though source-active ownership moved to newJob.
    const uint64_t secondCandidate = scenario.observeCandidate();
    check("open second old-job solution", secondCandidate != 0);
    check("finalize second old-job solution",
          scenario.coordinator.onCandidateFinalized(secondCandidate, true));
    scenario.clock.tick();
    check("open second old-job submit",
          scenario.coordinator.onSubmitAttempt() != 0);
    const uint64_t secondOperation = scenario.mailbox.publishSubmitDispatched(
        scenario.session, oldJob, PearlSourceDelivery::NotSent);
    check("dispatch second old-job submit", secondOperation != 0);
    scenario.coordinator.onSourcePublication();
    scenario.operation = secondOperation;
    scenario.terminal(PearlSourceDelivery::Sent,
                      PearlSourceTerminal::Rejected, true, false);
    state = scenario.coordinator.snapshotForOwner();
    check("second old-job terminal remains coherent",
          !state.sourceQuarantined && state.progress.submitAttempts == 2 &&
              state.progress.accepted == 1 && state.progress.rejected == 1 &&
              state.progress.invariantsHold());

    // Mirror the no-event release before the source releases it.  No mailbox
    // publication hook is permitted between these two exact owner-thread
    // operations.
    check("release old issued binding after terminal",
          scenario.coordinator.onIssuedJobReleased(scenario.session, oldJob) &&
              scenario.mailbox.releaseIssuedJob(scenario.session, oldJob));
    scenario.coordinator.drainCheckpoint();
    state = scenario.coordinator.snapshotForOwner();
    check("released old issued binding matches source snapshot",
          !state.sourceQuarantined && state.liveness.jobGeneration == 1);

    // The next successful fetch adopts the already-available replacement.
    check("bind source replacement",
          scenario.mailbox.bindIssuedJob(scenario.session, newJob));
    check("adopt replacement only at issued-job boundary",
          scenario.coordinator.onIssuedJobBound(scenario.session, newJob,
                                                 replacement));
    scenario.coordinator.drainCheckpoint();
    state = scenario.coordinator.snapshotForOwner();
    check("replacement adoption advances exact local job",
          !state.sourceQuarantined && state.liveness.jobGeneration == 2);

    check("mirror replacement release before source release",
          scenario.coordinator.onIssuedJobReleased(scenario.session, newJob));
    check("release replacement source binding",
          scenario.mailbox.releaseIssuedJob(scenario.session, newJob));
    scenario.clock.tick();
    check("source can retire released adopted job",
          scenario.mailbox.publishJobRetired(
              scenario.session, newJob, PearlSourceReason::Transport));
    scenario.coordinator.onSourcePublication();
    state = scenario.coordinator.snapshotForOwner();
    check("source retirement alone does not retire run-loop adoption",
          !state.sourceQuarantined && state.liveness.jobGeneration == 2 &&
              state.liveness.connection == PearlConnectionState::Ready);
    check("failed next fetch retires adopted liveness",
          scenario.coordinator.onIssuedJobFetchFailed());
    state = scenario.coordinator.snapshotForOwner();
    check("failed fetch leaves liveness awaiting work",
          state.liveness.connection == PearlConnectionState::AwaitingJob &&
              state.liveness.jobProgress == PearlJobProgressState::NoJob &&
              state.liveness.jobGeneration == 2);
}

void testSearchAttemptConservation() {
    auto runCase = [](uint64_t expected, unsigned completed,
                      bool completionExpected, uint64_t completedSearches,
                      uint64_t failedSearches) {
        Scenario scenario;
        check("search fixture mailbox bind",
              scenario.mailbox.bindIssuedJob(scenario.session, scenario.job));
        check("search fixture coordinator bind",
              scenario.coordinator.onIssuedJobBound(scenario.session,
                                                     scenario.job,
                                                     scenario.identity));
        scenario.prepareBoundJob();
        const bool started = scenario.coordinator.onSearchStarted(expected);
        if (expected == 0) {
            check("zero planned attempts are refused", !started);
        } else {
            check("positive planned attempts start", started);
            for (unsigned i = 0; i < completed; ++i) {
                scenario.coordinator.onPearlAlgorithmProgress(
                    PearlAlgorithmProgressEvent::AttemptStarted);
                scenario.coordinator.onPearlAlgorithmProgress(
                    PearlAlgorithmProgressEvent::AttemptCompleted);
                scenario.coordinator.onPearlAlgorithmProgress(
                    PearlAlgorithmProgressEvent::HitsObserved, 0);
                scenario.coordinator.onPearlAlgorithmProgress(
                    PearlAlgorithmProgressEvent::HitsRetained, 0);
            }
            check("search completion classification",
                  scenario.coordinator.onSearchCompleted(true) ==
                      completionExpected);
        }
        const auto state = scenario.coordinator.snapshotForOwner();
        check("completed search count is exact",
              state.progress.searchesCompleted == completedSearches);
        check("failed search count is exact",
              state.progress.searchesFailed == failedSearches);
        if (expected == 0 || !completionExpected)
            check("attempt mismatch reason is explicit",
                  state.progress.dropsFor(
                      PearlProgressReason::AttemptCountMismatch) == 1);
        check("search accounting remains coherent",
              state.progress.invariantsHold());
    };

    runCase(0, 0, false, 0, 0);
    runCase(1, 1, true, 1, 0);
    runCase(2, 1, false, 0, 1);
    runCase(1, 2, false, 0, 1);

    {
        Scenario scenario;
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        scenario.prepareBoundJob();
        check("zero-hit search starts", scenario.coordinator.onSearchStarted(1));
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptStarted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptCompleted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsObserved, 0);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsRetained, 0);
        check("zero-hit search completes normally",
              scenario.coordinator.onSearchCompleted(true));
        const auto state = scenario.coordinator.snapshotForOwner();
        check("zero hit deltas are valid no-op boundaries",
              !state.sourceQuarantined && state.progress.hitsObserved == 0 &&
                  state.progress.hitsRetained == 0 &&
                  state.progress.searchesCompleted == 1);
    }

    {
        PearlProgressSnapshot initial;
        const uint64_t maximum = std::numeric_limits<uint64_t>::max();
        initial.attemptsStarted = maximum - 1;
        initial.attemptsCompleted = maximum - 1;
        Scenario scenario(false, initial);
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        scenario.prepareBoundJob();
        check("saturated planned attempt count starts",
              scenario.coordinator.onSearchStarted(1));
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptStarted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptCompleted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsObserved, 0);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsRetained, 0);
        check("saturated exact delta is accepted without wrap",
              scenario.coordinator.onSearchCompleted(true));
        check("saturated counters remain coherent",
              scenario.coordinator.snapshotForOwner().progress.invariantsHold());
    }

    {
        ManualClock clock;
        PearlProgressBus bus;
        PearlProgressSnapshot initial;
        initial.searchGeneration = std::numeric_limits<uint64_t>::max();
        PearlSourceObservabilityMailbox mailbox(clock);
        PearlRunProgressCoordinator coordinator(
            &bus, &ManualClock::read, &clock, initial);
        coordinator.attachSource(&mailbox);
        const uint64_t session = mailbox.publishSessionStarted();
        coordinator.onSourcePublication();
        clock.tick();
        mailbox.publishTransportConnected(session);
        coordinator.onSourcePublication();
        clock.tick();
        mailbox.publishSessionReadyNoAuth(session);
        coordinator.onSourcePublication();
        clock.tick();
        const uint64_t job =
            mailbox.publishJobAvailable(session, 0, digest(73));
        coordinator.onSourcePublication();
        mailbox.bindIssuedJob(session, job);
        coordinator.onIssuedJobBound(session, job, digest(73));
        check("exhaustion fixture prepare start", coordinator.onPrepareStarted());
        check("exhaustion fixture prepare complete",
              coordinator.onPrepareCompleted());
        check("search generation exhaustion refuses start",
              !coordinator.onSearchStarted(1));
        const auto state = coordinator.snapshotForOwner();
        check("search generation exhaustion is explicit",
              state.progress.phase == PearlProgressPhase::Failed &&
                  state.progress.dropsFor(PearlProgressReason::SearchFailed) ==
                      1);
    }

    {
        Scenario scenario;
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        scenario.prepareBoundJob();
        check("256-attempt proof budget is legal",
              scenario.coordinator.onSearchStarted(
                  PearlRunProgressCoordinator::kProofBacklogCapacity));
        check("legal proof-budget search is not quarantined",
              !scenario.coordinator.snapshotForOwner().sourceQuarantined);
        scenario.coordinator.onSearchCompleted(false);
    }

    {
        Scenario scenario;
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        scenario.prepareBoundJob();
        check("257-attempt proof budget is refused before work",
              !scenario.coordinator.onSearchStarted(
                  PearlRunProgressCoordinator::kProofBacklogCapacity + 1));
        const auto state = scenario.coordinator.snapshotForOwner();
        check("proof-budget refusal has dedicated integrity flag",
              state.sourceQuarantined &&
                  state.progress.searchesStarted == 0 &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::SearchProofBudgetExceeded));
    }

    {
        Scenario scenario;
        primeOuterCandidate(scenario);
        scenario.prepareBoundJob();
        check("search budget includes unconsumed serialized backlog",
              !scenario.coordinator.onSearchStarted(
                  PearlRunProgressCoordinator::kProofBacklogCapacity));
        check("remaining-capacity refusal uses dedicated flag",
              scenario.coordinator.snapshotForOwner().hasIntegrityFlag(
                  PearlSourceIntegrityFlag::SearchProofBudgetExceeded));
    }
}

void testPrepareLifecycle() {
    {
        Scenario scenario;
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        check("search requires completed preparation",
              !scenario.coordinator.onSearchStarted(1));
        check("prepare completion cannot precede start",
              !scenario.coordinator.onPrepareCompleted());
        check("prepare failure cannot precede start",
              !scenario.coordinator.onPrepareFailed());
        check("prepare abort cannot precede start",
              !scenario.coordinator.onPrepareAborted());
        check("prepare starts once", scenario.coordinator.onPrepareStarted());
        check("duplicate prepare start is refused",
              !scenario.coordinator.onPrepareStarted());
        check("search cannot consume open preparation",
              !scenario.coordinator.onSearchStarted(1));
        check("prepare completes once",
              scenario.coordinator.onPrepareCompleted());
        check("duplicate prepare completion is refused",
              !scenario.coordinator.onPrepareCompleted());
        check("completed preparation pins issued job until search",
              !scenario.coordinator.onIssuedJobReleased(
                  scenario.session, scenario.job));
        check("search consumes exact completed preparation",
              scenario.coordinator.onSearchStarted(1));
    }

    {
        Scenario scenario;
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        scenario.coordinator.onPrepareStarted();
        check("prepare failure closes open preparation",
              scenario.coordinator.onPrepareFailed());
        check("failed preparation cannot complete or search",
              !scenario.coordinator.onPrepareCompleted() &&
                  !scenario.coordinator.onSearchStarted(1));
        const auto state = scenario.coordinator.snapshotForOwner();
        check("prepare failure reason and phase are explicit",
              state.progress.phase == PearlProgressPhase::Failed &&
                  state.progress.dropsFor(
                      PearlProgressReason::PrepareFailed) == 1);
    }

    {
        Scenario scenario;
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        scenario.coordinator.onPrepareStarted();
        check("prepare abort closes open preparation",
              scenario.coordinator.onPrepareAborted());
        check("aborted preparation cannot complete or search",
              !scenario.coordinator.onPrepareCompleted() &&
                  !scenario.coordinator.onSearchStarted(1));
        const auto state = scenario.coordinator.snapshotForOwner();
        check("prepare abort is explicit without failed mining phase",
              state.progress.phase == PearlProgressPhase::AwaitingJob &&
                  state.progress.dropsFor(
                      PearlProgressReason::PrepareFailed) == 1);
    }
}

void testPrepareCacheReuse() {
    {
        Scenario scenario;
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        scenario.prepareBoundJob();
        const uint64_t prepareGeneration =
            scenario.coordinator.snapshotForOwner()
                .progress.prepareGeneration;
        scenario.coordinator.onSearchStarted(1);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptStarted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptCompleted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsObserved, 0);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsRetained, 0);
        scenario.coordinator.onSearchCompleted(true);
        check("cached prepare fixture coordinator release",
              scenario.coordinator.onIssuedJobReleased(
                  scenario.session, scenario.job));
        check("cached prepare fixture source release",
              scenario.mailbox.releaseIssuedJob(scenario.session,
                                                 scenario.job));
        check("cached prepare fixture same-job source rebind",
              scenario.mailbox.bindIssuedJob(scenario.session,
                                              scenario.job));
        check("cached prepare fixture same-job coordinator rebind",
              scenario.coordinator.onIssuedJobBound(
                  scenario.session, scenario.job, scenario.identity));
        check("exact same-job prepared cache is reusable",
              scenario.coordinator.onPrepareReused(
                  scenario.session, scenario.authorization, scenario.job,
                  scenario.identity));
        check("cache reuse does not invent a preparation generation",
              scenario.coordinator.snapshotForOwner()
                      .progress.prepareGeneration == prepareGeneration);
        check("reused cache authorizes second-batch search",
              scenario.coordinator.onSearchStarted(1));
        check("second-batch search can close as failed",
              scenario.coordinator.onSearchCompleted(false));
    }

    {
        Scenario scenario;
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        scenario.prepareBoundJob();
        scenario.coordinator.onSearchStarted(1);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptStarted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptCompleted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsObserved, 0);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsRetained, 0);
        scenario.coordinator.onSearchCompleted(true);
        scenario.coordinator.onIssuedJobReleased(scenario.session,
                                                 scenario.job);
        scenario.mailbox.releaseIssuedJob(scenario.session, scenario.job);
        scenario.clock.tick();
        scenario.mailbox.publishJobRetired(
            scenario.session, scenario.job, PearlSourceReason::Stale);
        scenario.coordinator.onSourcePublication();
        const PearlJobIdentityDigest replacement = digest(137);
        scenario.clock.tick();
        const uint64_t replacementJob = scenario.mailbox.publishJobAvailable(
            scenario.session, scenario.authorization, replacement);
        scenario.coordinator.onSourcePublication();
        scenario.mailbox.bindIssuedJob(scenario.session, replacementJob);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, replacementJob, replacement);
        check("changed-job cache reuse is refused",
              !scenario.coordinator.onPrepareReused(
                  scenario.session, scenario.authorization, replacementJob,
                  replacement));
        check("changed job cannot search through stale prepare cache",
              !scenario.coordinator.onSearchStarted(1));
        check("changed job can begin a real preparation",
              scenario.coordinator.onPrepareStarted());
    }
}

void testMixedReturnedSolutionsUseSerializedOrdinals() {
    Scenario scenario;
    scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
    scenario.coordinator.onIssuedJobBound(
        scenario.session, scenario.job, scenario.identity);
    scenario.prepareBoundJob();
    scenario.coordinator.onSearchStarted(2);

    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptStarted);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptCompleted);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsObserved);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsRetained);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::FinalizerStarted);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::CandidateSelected);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HostInvalid);

    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptStarted);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptCompleted);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsObserved);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsRetained);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::FinalizerStarted);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::CandidateSelected);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HostValid);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::ProofSerialized);
    check("mixed returned-solution search completes",
          scenario.coordinator.onSearchCompleted(true));

    // The first returned Solution is the algorithm's empty HostInvalid
    // diagnostic. The run loop must skip the coordinator API for it. Only the
    // second, nonempty serialized output consumes serialized ordinal 1.
    const auto before = scenario.coordinator.snapshotForOwner();
    check("host-invalid empty output owns no serialized record",
          before.progress.hostInvalid == 1 &&
              before.progress.proofsSerialized == 1 &&
              before.progress.candidateGeneration == 0);
    const uint64_t candidate = scenario.observeCandidate();
    check("valid nonempty output consumes its exact serialized ordinal",
          candidate != 0 &&
              scenario.coordinator.onCandidateFinalized(candidate, true));
    const auto after = scenario.coordinator.snapshotForOwner();
    check("skipping empty output preserves exact candidate accounting",
          !after.sourceQuarantined &&
              after.progress.candidateGeneration == 1 &&
              after.liveness.candidatesObserved == 1);
}

void testOuterCandidateBoundaries() {
    {
        Scenario scenario;
        primeOuterCandidate(scenario);
        const uint64_t candidate = scenario.observeCandidate();
        check("outer candidate gets exact token", candidate != 0);
        check("stale candidate token is refused",
              !scenario.coordinator.onCandidateFinalized(candidate + 1, true));
        check("exact candidate finalizes",
              scenario.coordinator.onCandidateFinalized(candidate, true));
        check("candidate double close is refused",
              !scenario.coordinator.onCandidateFinalized(candidate, true));
    }

    {
        Scenario scenario;
        primeOuterCandidate(scenario);
        const uint64_t candidate = scenario.observeCandidate();
        const uint64_t hostChecksBefore =
            scenario.coordinator.snapshotForOwner().progress.hostChecked;
        check("outer ownership-invalid candidate closes",
              scenario.coordinator.onCandidateFinalized(
                  candidate, false,
                  PearlProgressReason::CandidateOwnershipLost));
        const auto state = scenario.coordinator.snapshotForOwner();
        check("invalid outer candidate cannot submit",
              scenario.coordinator.onSubmitAttempt() == 0 &&
                  state.liveness.candidatesInvalid == 1);
        check("outer close does not double-count algorithm host check",
              state.progress.hostChecked == hostChecksBefore);
        check("outer ownership loss has exact reason",
              state.progress.dropsFor(
                  PearlProgressReason::CandidateOwnershipLost) == 1);
    }

    {
        PearlProgressSnapshot initial;
        initial.candidateGeneration = std::numeric_limits<uint64_t>::max();
        Scenario scenario(false, initial);
        primeOuterCandidate(scenario);
        check("candidate generation exhaustion refuses observation",
              scenario.observeCandidate() == 0);
        const auto state = scenario.coordinator.snapshotForOwner();
        check("candidate exhaustion does not partially mutate liveness",
              state.liveness.candidate == PearlCandidateState::Idle &&
                  state.liveness.candidateGeneration == 0);
    }

    {
        Scenario scenario;
        primeOuterCandidate(scenario);
        const uint64_t search =
            scenario.coordinator.snapshotForOwner().progress.searchGeneration;
        check("wrong serialized ordinal is refused",
              scenario.coordinator.onCandidateObserved(
                  scenario.session, scenario.authorization, scenario.job,
                  scenario.identity, search, 2) == 0);
        const auto state = scenario.coordinator.snapshotForOwner();
        check("serialized ordinal mismatch fails closed",
              state.sourceQuarantined &&
                  state.progress.candidateGeneration == 0 &&
                  state.liveness.candidate == PearlCandidateState::Idle);
    }

    {
        Scenario scenario;
        primeOuterCandidate(scenario);
        const uint64_t oldSearch =
            scenario.coordinator.snapshotForOwner().progress.searchGeneration;
        check("backlog fixture coordinator release",
              scenario.coordinator.onIssuedJobReleased(scenario.session,
                                                        scenario.job));
        check("backlog fixture source release",
              scenario.mailbox.releaseIssuedJob(scenario.session,
                                                 scenario.job));
        scenario.clock.tick();
        scenario.mailbox.publishJobRetired(
            scenario.session, scenario.job, PearlSourceReason::Stale);
        scenario.coordinator.onSourcePublication();
        const PearlJobIdentityDigest replacement = digest(122);
        scenario.clock.tick();
        const uint64_t replacementJob = scenario.mailbox.publishJobAvailable(
            scenario.session, scenario.authorization, replacement);
        scenario.coordinator.onSourcePublication();
        check("backlog fixture source replacement bind",
              scenario.mailbox.bindIssuedJob(scenario.session,
                                              replacementJob));
        check("backlog fixture coordinator replacement bind",
              scenario.coordinator.onIssuedJobBound(
                  scenario.session, replacementJob, replacement));
        check("old proof cannot be observed as replacement result",
              scenario.coordinator.onCandidateObserved(
                  scenario.session, scenario.authorization, replacementJob,
                  replacement, oldSearch, 1) == 0);
        const auto state = scenario.coordinator.snapshotForOwner();
        check("cross-job proof mismatch fails closed without candidate",
              state.sourceQuarantined &&
                  state.liveness.candidate == PearlCandidateState::Idle &&
                  state.progress.candidateGeneration == 0 &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::InvalidAlgorithmEvent));
    }
}

void testAtomicJobReplacementExhaustion() {
    Scenario scenario(false, PearlProgressSnapshot(), 1);
    check("limited fixture source bind",
          scenario.mailbox.bindIssuedJob(scenario.session, scenario.job));
    check("limited fixture initial adoption",
          scenario.coordinator.onIssuedJobBound(scenario.session,
                                                 scenario.job,
                                                 scenario.identity));
    check("limited fixture mirror release",
          scenario.coordinator.onIssuedJobReleased(scenario.session,
                                                    scenario.job));
    check("limited fixture source release",
          scenario.mailbox.releaseIssuedJob(scenario.session, scenario.job));
    scenario.clock.tick();
    check("limited fixture retire old source job",
          scenario.mailbox.publishJobRetired(
              scenario.session, scenario.job, PearlSourceReason::Stale));
    scenario.coordinator.onSourcePublication();
    scenario.clock.tick();
    const uint64_t replacement = scenario.mailbox.publishJobAvailable(
        scenario.session, scenario.authorization, digest(111));
    scenario.coordinator.onSourcePublication();
    check("limited fixture bind replacement source",
          scenario.mailbox.bindIssuedJob(scenario.session, replacement));
    check("job generation limit refuses replacement atomically",
          !scenario.coordinator.onIssuedJobBound(scenario.session, replacement,
                                                  digest(111)));
    const auto state = scenario.coordinator.snapshotForOwner();
    check("replacement exhaustion preserves old adopted liveness",
          state.liveness.connection == PearlConnectionState::Ready &&
              state.liveness.jobGeneration == 1 &&
              state.liveness.jobProgress ==
                  PearlJobProgressState::AwaitingFirstAttempt);
}

void testOutOfOrderRunLoopBoundaries() {
    Scenario scenario;
    check("ordering fixture source bind",
          scenario.mailbox.bindIssuedJob(scenario.session, scenario.job));
    check("ordering fixture coordinator bind",
          scenario.coordinator.onIssuedJobBound(scenario.session,
                                                 scenario.job,
                                                 scenario.identity));
    scenario.prepareBoundJob();
    check("ordering fixture search start",
          scenario.coordinator.onSearchStarted(1));
    check("candidate cannot open before search completion",
          scenario.observeCandidate() == 0);
    check("issued job cannot release during search",
          !scenario.coordinator.onIssuedJobReleased(scenario.session,
                                                    scenario.job));
    check("fetch failure cannot retire during search",
          !scenario.coordinator.onIssuedJobFetchFailed());
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptStarted);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::AttemptCompleted);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsObserved);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HitsRetained);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::FinalizerStarted);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::CandidateSelected);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::HostValid);
    scenario.coordinator.onPearlAlgorithmProgress(
        PearlAlgorithmProgressEvent::ProofSerialized);
    check("ordering fixture search completion",
          scenario.coordinator.onSearchCompleted(true));
    const uint64_t candidate = scenario.observeCandidate();
    check("ordering fixture candidate observed", candidate != 0);
    check("issued job cannot release with candidate open",
          !scenario.coordinator.onIssuedJobReleased(scenario.session,
                                                    scenario.job));
    check("ordering fixture candidate finalized",
          scenario.coordinator.onCandidateFinalized(candidate, true));
    check("issued job cannot release while candidate awaits submit",
          !scenario.coordinator.onIssuedJobReleased(scenario.session,
                                                    scenario.job));
    check("ordering fixture submit opens",
          scenario.coordinator.onSubmitAttempt() != 0);
    check("issued job cannot release with pending dispatch",
          !scenario.coordinator.onIssuedJobReleased(scenario.session,
                                                    scenario.job));
}

void testSubmitReturnReconciliation() {
    {
        Scenario scenario;
        primeOuterCandidate(scenario);
        const uint64_t candidate = scenario.observeCandidate();
        scenario.coordinator.onCandidateFinalized(candidate, true);
        const uint64_t submit = scenario.coordinator.onSubmitAttempt();
        check("false return without dispatch closes local failure",
              scenario.coordinator.onSubmitReturned(submit, false));
        const auto state = scenario.coordinator.snapshotForOwner();
        check("local return failure maps once",
              state.liveness.lastOutcome ==
                      PearlSubmitOutcome::SubmissionFailed &&
                  state.progress.submissionFailures == 1 &&
                  state.progress.dropsFor(
                      PearlProgressReason::SubmissionFailed) == 1);
        check("completed local return is idempotent",
              scenario.coordinator.onSubmitReturned(submit, false) &&
                  scenario.coordinator.snapshotForOwner()
                          .progress.submissionFailures == 1);
    }

    {
        Scenario scenario;
        primeOuterCandidate(scenario);
        const uint64_t candidate = scenario.observeCandidate();
        scenario.coordinator.onCandidateFinalized(candidate, true);
        const uint64_t submit = scenario.coordinator.onSubmitAttempt();
        check("success without dispatch quarantines",
              !scenario.coordinator.onSubmitReturned(submit, true));
        const auto state = scenario.coordinator.snapshotForOwner();
        check("success without evidence fabricates no outcome",
              state.sourceQuarantined && state.liveness.outcomes == 0 &&
                  state.progress.accepted == 0);
    }

    {
        Scenario scenario;
        primeOuterCandidate(scenario);
        const uint64_t candidate = scenario.observeCandidate();
        scenario.coordinator.onCandidateFinalized(candidate, true);
        const uint64_t submit = scenario.coordinator.onSubmitAttempt();
        const uint64_t operation = scenario.mailbox.publishSubmitDispatched(
            scenario.session, scenario.job, PearlSourceDelivery::NotSent);
        check("missing-terminal fixture dispatch", operation != 0);
        scenario.coordinator.onSourcePublication();
        check("synchronous return with in-flight evidence quarantines",
              !scenario.coordinator.onSubmitReturned(submit, false));
        check("missing terminal fabricates no failure",
              scenario.coordinator.snapshotForOwner()
                          .progress.submissionFailures == 0);
    }

    {
        Scenario scenario;
        scenario.prepareSubmission();
        scenario.terminal(PearlSourceDelivery::Sent,
                          PearlSourceTerminal::Accepted, true);
        const auto before = scenario.coordinator.snapshotForOwner();
        check("terminal evidence dominates legacy return",
              scenario.coordinator.onSubmitReturned(scenario.submit, false));
        const auto after = scenario.coordinator.snapshotForOwner();
        check("legacy return cannot double-map terminal",
              before.progress.accepted == after.progress.accepted &&
                  before.liveness.outcomes == after.liveness.outcomes);
    }
}

void testExactBindDigestAndDisconnectReconciliation() {
    {
        Scenario scenario;
        check("source binds digest fixture",
              scenario.mailbox.bindIssuedJob(scenario.session, scenario.job));
        PearlJobIdentityDigest wrong = scenario.identity;
        wrong.bytes[0] ^= 0xff;
        check("wrong returned-job digest is refused",
              !scenario.coordinator.onIssuedJobBound(
                  scenario.session, scenario.job, wrong));
        check("wrong digest cannot adopt liveness",
              scenario.coordinator.snapshotForOwner().liveness.jobGeneration ==
                  0);
        check("exact returned-job digest binds",
              scenario.coordinator.onIssuedJobBound(
                  scenario.session, scenario.job, scenario.identity));
    }

    {
        Scenario scenario;
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        // Deliberately omit the local non-event release mirror. The ordered
        // retire+disconnect facts and post-drain snapshot reconcile it.
        scenario.mailbox.releaseIssuedJob(scenario.session, scenario.job);
        scenario.mailbox.publishJobRetired(
            scenario.session, scenario.job, PearlSourceReason::Shutdown);
        scenario.mailbox.publishDisconnected(
            scenario.session, PearlSourceReason::Shutdown);
        scenario.coordinator.onSourcePublication();
        const auto state = scenario.coordinator.snapshotForOwner();
        check("ordered disconnect reconciles released local ownership",
              !state.sourceQuarantined &&
                  state.liveness.connection ==
                      PearlConnectionState::Disconnected);
    }
}

class ScriptSource final : public PearlSourceObservability {
   public:
    std::deque<PearlSourceEvent> events;
    PearlSourceObservabilitySnapshot state;
    bool endless = false;
    bool rollbackOnSnapshot = false;

    std::size_t drain(PearlSourceEvent *out, std::size_t maximum) override {
        std::size_t count = 0;
        if (endless) {
            for (; count < maximum; ++count) {
                PearlSourceEvent e;
                e.eventToken = state.drainedEvents + count + 1;
                out[count] = e;
            }
        } else {
            while (count < maximum && !events.empty()) {
                out[count++] = events.front();
                events.pop_front();
            }
        }
        state.drainedEvents += count;
        state.queued = endless ? 1 : events.size();
        state.publishedEvents = state.drainedEvents + state.queued;
        return count;
    }

    PearlSourceObservabilitySnapshot snapshot() const override {
        PearlSourceObservabilitySnapshot out = state;
        if (rollbackOnSnapshot && out.drainedEvents != 0)
            --out.drainedEvents;
        return out;
    }
};

class MutatingSource final : public PearlSourceObservability {
   public:
    enum class Mutation {
        Digest,
        TerminalTuple,
        TerminalReason,
        MissingUnknownReason,
        TerminalEvidenceCrossClass,
        UnknownTerminalEvidence
    };

    MutatingSource(PearlSourceObservability &inner, Mutation mutation)
        : inner_(inner), mutation_(mutation) {}

    std::size_t drain(PearlSourceEvent *out, std::size_t maximum) override {
        const std::size_t count = inner_.drain(out, maximum);
        for (std::size_t i = 0; i < count; ++i) {
            if (out[i].kind != PearlSourceEventKind::SubmitTerminal) continue;
            if (mutation_ == Mutation::Digest)
                out[i].jobIdentity.bytes[0] ^= 0xff;
            else if (mutation_ == Mutation::TerminalTuple)
                out[i].delivery = PearlSourceDelivery::NotSent;
            else if (mutation_ == Mutation::TerminalReason)
                out[i].reason = PearlSourceReason::Transport;
            else if (mutation_ == Mutation::TerminalEvidenceCrossClass)
                out[i].terminalEvidence =
                    PearlSourceTerminalEvidence::
                        AcknowledgedOutcomeUnavailable;
            else if (mutation_ == Mutation::UnknownTerminalEvidence)
                out[i].terminalEvidence =
                    static_cast<PearlSourceTerminalEvidence>(255);
            else
                out[i].reason = PearlSourceReason::None;
        }
        return count;
    }

    PearlSourceObservabilitySnapshot snapshot() const override {
        return inner_.snapshot();
    }

   private:
    PearlSourceObservability &inner_;
    Mutation mutation_;
};

class SnapshotTamperSource final : public PearlSourceObservability {
   public:
    enum class Mutation { LastEventField, MissingLastEvent, IssuedStillBound };

    SnapshotTamperSource(PearlSourceObservability &inner, Mutation mutation)
        : inner_(inner), mutation_(mutation) {}

    std::size_t drain(PearlSourceEvent *out, std::size_t maximum) override {
        return inner_.drain(out, maximum);
    }

    PearlSourceObservabilitySnapshot snapshot() const override {
        PearlSourceObservabilitySnapshot out = inner_.snapshot();
        if (!enabled) return out;
        if (mutation_ == Mutation::LastEventField)
            out.lastPublishedEvent.reserved1 ^= 1;
        else if (mutation_ == Mutation::MissingLastEvent)
            out.hasLastPublishedEvent = false;
        else
            out.issuedJobBound = true;
        return out;
    }

    bool enabled = true;

   private:
    PearlSourceObservability &inner_;
    Mutation mutation_;
};

class AcceptedThenBadSource final : public PearlSourceObservability {
   public:
    explicit AcceptedThenBadSource(PearlSourceObservability &inner)
        : inner_(inner) {}

    std::size_t drain(PearlSourceEvent *out, std::size_t maximum) override {
        std::size_t count = inner_.drain(out, maximum);
        if (!injected_ && count < maximum) {
            for (std::size_t i = 0; i < count; ++i) {
                if (out[i].kind != PearlSourceEventKind::SubmitTerminal)
                    continue;
                bad_ = out[i];
                ++bad_.eventToken;
                bad_.kind = PearlSourceEventKind::SourceFault;
                bad_.operationToken = 0;
                bad_.delivery = PearlSourceDelivery::None;
                bad_.terminal = PearlSourceTerminal::None;
                bad_.reason = PearlSourceReason::Transport;
                bad_.acknowledged = 0;
                bad_.superseded = 0;
                bad_.reserved1 = 1;
                out[count++] = bad_;
                injected_ = true;
                break;
            }
        }
        return count;
    }

    PearlSourceObservabilitySnapshot snapshot() const override {
        PearlSourceObservabilitySnapshot out = inner_.snapshot();
        if (!injected_) return out;
        ++out.publishedEvents;
        ++out.drainedEvents;
        out.lastEventToken = bad_.eventToken;
        out.lastTimestampMs = bad_.timestampMs;
        out.hasLastPublishedEvent = true;
        out.lastPublishedEvent = bad_;
        return out;
    }

   private:
    PearlSourceObservability &inner_;
    bool injected_ = false;
    PearlSourceEvent bad_;
};

void testDisconnectPostSnapshotConfirmation() {
    ManualClock clock;
    PearlProgressBus bus;
    PearlSourceObservabilityMailbox mailbox(clock);
    SnapshotTamperSource source(
        mailbox, SnapshotTamperSource::Mutation::IssuedStillBound);
    source.enabled = false;
    PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read, &clock);
    check("attach disconnect snapshot wrapper",
          coordinator.attachSource(&source));

    const PearlJobIdentityDigest identity = digest(73);
    const uint64_t session = mailbox.publishSessionStarted();
    coordinator.onSourcePublication();
    clock.tick();
    check("disconnect fixture transport",
          mailbox.publishTransportConnected(session));
    coordinator.onSourcePublication();
    clock.tick();
    const uint64_t authorization =
        mailbox.publishAuthorizationSucceeded(session);
    coordinator.onSourcePublication();
    clock.tick();
    const uint64_t job =
        mailbox.publishJobAvailable(session, authorization, identity);
    coordinator.onSourcePublication();
    check("disconnect fixture mailbox bind",
          mailbox.bindIssuedJob(session, job));
    check("disconnect fixture coordinator bind",
          coordinator.onIssuedJobBound(session, job, identity));

    // Exercise the exact internal-close recovery path: the source has already
    // released its non-event binding, but the run loop missed its local mirror.
    check("disconnect fixture source release",
          mailbox.releaseIssuedJob(session, job));
    check("disconnect fixture retire",
          mailbox.publishJobRetired(session, job,
                                    PearlSourceReason::Shutdown));
    check("disconnect fixture close",
          mailbox.publishDisconnected(session, PearlSourceReason::Shutdown));
    source.enabled = true;
    coordinator.onSourcePublication();

    const auto state = coordinator.snapshotForOwner();
    check("disconnect requires coherent released post-snapshot",
          state.sourceQuarantined &&
              state.hasIntegrityFlag(
                  PearlSourceIntegrityFlag::SnapshotAccountingInvalid));
    check("failed disconnect confirmation restores adopted liveness",
          state.liveness.connection == PearlConnectionState::Ready &&
              state.liveness.jobGeneration == 1 &&
              state.progress.phase == PearlProgressPhase::AwaitingJob);
}

void testRawTerminalEvidenceSurvivesRollback() {
    {
        ManualClock clock;
        PearlProgressBus bus;
        PearlSourceObservabilityMailbox mailbox(clock);
        AcceptedThenBadSource source(mailbox);
        PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read,
                                                &clock);
        coordinator.attachSource(&source);
        const PearlJobIdentityDigest identity = digest(81);
        const uint64_t session = mailbox.publishSessionStarted();
        coordinator.onSourcePublication();
        clock.tick();
        mailbox.publishTransportConnected(session);
        coordinator.onSourcePublication();
        clock.tick();
        const uint64_t authorization =
            mailbox.publishAuthorizationSucceeded(session);
        coordinator.onSourcePublication();
        clock.tick();
        const uint64_t job =
            mailbox.publishJobAvailable(session, authorization, identity);
        coordinator.onSourcePublication();
        const uint64_t operation = prepareDirectSubmission(
            clock, mailbox, coordinator, session, authorization, job,
            identity);
        clock.tick();
        check("accepted-before-bad fixture terminal",
              mailbox.publishSubmitTerminal(
                  session, operation, PearlSourceDelivery::Sent,
                  PearlSourceTerminal::Accepted, true, false,
                  PearlSourceReason::None));
        coordinator.onSourcePublication();
        const auto state = coordinator.snapshotForOwner();
        check("accepted before bad event is retained but untrusted",
              state.sourceQuarantined && state.progress.accepted == 0 &&
                  state.liveness.outcomes == 0 &&
                  state.terminalEvidenceCount == 1 &&
                  state.terminalEvidence[0].trust ==
                      PearlRawTerminalTrust::Untrusted &&
                  state.terminalEvidence[0].event.terminal ==
                      PearlSourceTerminal::Accepted);
        PearlRawTerminalEvidence retained;
        check("consumption preserves raw untrusted terminal after quarantine",
              coordinator.consumeTerminalEvidence(&retained, 1) == 1 &&
                  retained.observation == 1 &&
                  retained.trust == PearlRawTerminalTrust::Untrusted &&
                  retained.event.operationToken == operation &&
                  retained.event.terminal == PearlSourceTerminal::Accepted &&
                  coordinator.snapshotForOwner().terminalEvidenceObserved == 1 &&
                  coordinator.snapshotForOwner().terminalEvidenceConsumed == 1 &&
                  coordinator.snapshotForOwner().sourceQuarantined);
    }

    {
        ManualClock clock;
        PearlProgressBus bus;
        PearlSourceObservabilityMailbox mailbox(clock);
        SnapshotTamperSource source(
            mailbox, SnapshotTamperSource::Mutation::LastEventField);
        source.enabled = false;
        PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read,
                                                &clock);
        coordinator.attachSource(&source);
        const PearlJobIdentityDigest identity = digest(82);
        const uint64_t session = mailbox.publishSessionStarted();
        coordinator.onSourcePublication();
        clock.tick();
        mailbox.publishTransportConnected(session);
        coordinator.onSourcePublication();
        clock.tick();
        const uint64_t authorization =
            mailbox.publishAuthorizationSucceeded(session);
        coordinator.onSourcePublication();
        clock.tick();
        const uint64_t job =
            mailbox.publishJobAvailable(session, authorization, identity);
        coordinator.onSourcePublication();
        const uint64_t operation = prepareDirectSubmission(
            clock, mailbox, coordinator, session, authorization, job,
            identity);
        clock.tick();
        check("post-snapshot-failure fixture terminal",
              mailbox.publishSubmitTerminal(
                  session, operation, PearlSourceDelivery::Sent,
                  PearlSourceTerminal::Accepted, true, false,
                  PearlSourceReason::None));
        source.enabled = true;
        coordinator.onSourcePublication();
        const auto state = coordinator.snapshotForOwner();
        check("accepted before bad snapshot is retained but untrusted",
              state.sourceQuarantined && state.progress.accepted == 0 &&
                  state.liveness.outcomes == 0 &&
                  state.terminalEvidenceCount == 1 &&
                  state.terminalEvidence[0].trust ==
                      PearlRawTerminalTrust::Untrusted &&
                  state.terminalEvidence[0].event.terminal ==
                      PearlSourceTerminal::Accepted);
    }

    {
        ManualClock clock;
        PearlProgressBus bus;
        ScriptSource source;
        for (uint64_t token = 1;
             token <= kPearlRawTerminalEvidenceCapacity; ++token) {
            PearlSourceEvent event;
            event.eventToken = token;
            event.timestampMs = token;
            event.kind = PearlSourceEventKind::SubmitTerminal;
            event.delivery = PearlSourceDelivery::Sent;
            event.terminal = PearlSourceTerminal::Accepted;
            event.acknowledged = 1;
            source.events.push_back(event);
            source.state.lastPublishedEvent = event;
        }
        source.state.lastEventToken = kPearlRawTerminalEvidenceCapacity;
        source.state.lastTimestampMs = kPearlRawTerminalEvidenceCapacity;
        source.state.hasLastPublishedEvent = true;
        PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read,
                                                &clock);
        coordinator.attachSource(&source);
        coordinator.drainCheckpoint();
        auto state = coordinator.snapshotForOwner();
        check("raw terminal ledger fills without overwrite",
              state.terminalEvidenceCount ==
                      kPearlRawTerminalEvidenceCapacity &&
                  state.terminalEvidence[0].observation == 1 &&
                  state.terminalEvidence[0].event.eventToken == 1);

        PearlSourceEvent overflow;
        overflow.eventToken = kPearlRawTerminalEvidenceCapacity + 1;
        overflow.timestampMs = overflow.eventToken;
        overflow.kind = PearlSourceEventKind::SubmitTerminal;
        overflow.delivery = PearlSourceDelivery::Sent;
        overflow.terminal = PearlSourceTerminal::Accepted;
        overflow.acknowledged = 1;
        source.events.push_back(overflow);
        source.state.lastEventToken = overflow.eventToken;
        source.state.lastTimestampMs = overflow.timestampMs;
        source.state.lastPublishedEvent = overflow;
        coordinator.drainCheckpoint();
        state = coordinator.snapshotForOwner();
        check("raw terminal overflow preserves append-only prefix",
              state.terminalEvidenceCount ==
                      kPearlRawTerminalEvidenceCapacity &&
                  state.terminalEvidenceObserved ==
                      kPearlRawTerminalEvidenceCapacity + 1 &&
                  state.terminalEvidenceDropped == 1 &&
                  state.terminalEvidence[0].event.eventToken == 1 &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::TerminalEvidenceOverflow));
    }
}

void testDisconnectedSearchRetirement() {
    for (uint64_t remaining : {uint64_t{0}, uint64_t{2}}) {
        Scenario scenario;
        scenario.prepareSubmission(remaining + 1);
        const uint64_t generation = scenario.coordinator.snapshotForOwner().progress.searchGeneration;
        check("cannot retire search before correlated terminal and disconnect",
              !scenario.coordinator.retireDisconnectedSearch(
                  scenario.session, scenario.authorization, scenario.job,
                  scenario.identity, generation, remaining));
        scenario.terminal(PearlSourceDelivery::Sent,
                          PearlSourceTerminal::Unknown, false);
        scenario.clock.tick();
        check("disconnect fixture retires active source job",
              scenario.mailbox.publishJobRetired(scenario.session, scenario.job,
                                                  PearlSourceReason::Transport));
        scenario.coordinator.onSourcePublication();
        check("disconnect fixture source releases issued work",
              scenario.mailbox.releaseIssuedJob(scenario.session, scenario.job));
        scenario.clock.tick();
        check("disconnect fixture publishes disconnect",
              scenario.mailbox.publishDisconnected(scenario.session, PearlSourceReason::Transport));
        scenario.coordinator.onSourcePublication();
        const auto before = scenario.coordinator.snapshotForOwner();
        check("disconnect fixture remains trusted with exact unknown outcome",
              !before.sourceQuarantined && before.progress.unknown == 1 &&
              before.progress.accepted == 0 && before.progress.rejected == 0);
        bool mismatches = true;
        for (unsigned field = 0; field < 6; ++field) {
            auto identity = scenario.identity;
            if (field == 3) identity.bytes[0] ^= 1;
            mismatches = mismatches && !scenario.coordinator.retireDisconnectedSearch(
                scenario.session + (field == 0), scenario.authorization + (field == 1),
                scenario.job + (field == 2), identity, generation + (field == 4),
                remaining + (field == 5));
        }
        check("retirement requires exact tuple, search generation, and remaining count",
              mismatches && scenario.coordinator.snapshotForOwner().progress.dropsFor(
                  PearlProgressReason::SessionSuperseded) == 0);
        check("exact disconnected suffix retires once including zero remaining",
              scenario.coordinator.retireDisconnectedSearch(
                  scenario.session, scenario.authorization, scenario.job,
                  scenario.identity, generation, remaining) &&
              !scenario.coordinator.retireDisconnectedSearch(
                  scenario.session, scenario.authorization, scenario.job,
                  scenario.identity, generation, remaining));
        const auto after = scenario.coordinator.snapshotForOwner();
        check("disconnected retirement preserves legitimate terminal and adds only exact local drops",
              !after.sourceQuarantined &&
              after.progress.dropsFor(PearlProgressReason::SessionSuperseded) == remaining &&
              after.progress.unknown == before.progress.unknown &&
              after.progress.accepted == before.progress.accepted &&
              after.progress.rejected == before.progress.rejected &&
              after.progress.submitAttempts == before.progress.submitAttempts &&
              after.terminalEvidenceCount == before.terminalEvidenceCount &&
              after.terminalEvidence[0].event.operationToken == scenario.operation);
        scenario.clock.tick();
        scenario.session = scenario.mailbox.publishSessionStarted();
        scenario.coordinator.onSourcePublication();
        scenario.mailbox.publishTransportConnected(scenario.session);
        scenario.coordinator.onSourcePublication();
        scenario.authorization = scenario.mailbox.publishAuthorizationSucceeded(scenario.session);
        scenario.coordinator.onSourcePublication();
        scenario.job = scenario.mailbox.publishJobAvailable(
            scenario.session, scenario.authorization, scenario.identity);
        scenario.coordinator.onSourcePublication();
        check("reconnected source binds after retirement",
              scenario.mailbox.bindIssuedJob(scenario.session, scenario.job) &&
              scenario.coordinator.onIssuedJobBound(scenario.session, scenario.job,
                                                     scenario.identity));
        scenario.prepareBoundJob();
        check("retirement releases the full proof backlog budget for reconnect",
              scenario.coordinator.onSearchStarted(PearlRunProgressCoordinator::kProofBacklogCapacity) &&
              scenario.coordinator.onSearchCompleted(false) &&
              !scenario.coordinator.snapshotForOwner().sourceQuarantined);
    }
}

void testTerminalEvidenceConsumption() {
    Scenario scenario;
    bool exact = true;
    constexpr uint64_t count = kPearlRawTerminalEvidenceCapacity + 17;
    for (uint64_t i = 1; i <= count; ++i) {
        scenario.nextSerializedOrdinal = 1;
        scenario.prepareSubmission();
        scenario.terminal(PearlSourceDelivery::Sent,
                          PearlSourceTerminal::Accepted, true);
        exact = exact && scenario.coordinator.onSubmitReturned(scenario.submit, true);
        PearlRawTerminalEvidence evidence[2];
        evidence[1].observation = UINT64_MAX;
        exact = exact && scenario.coordinator.consumeTerminalEvidence(evidence, 2) == 1 &&
            evidence[0].observation == i &&
            evidence[0].trust == PearlRawTerminalTrust::Trusted &&
            evidence[0].event.operationToken == scenario.operation &&
            evidence[0].event.jobIdentity.bytes == scenario.identity.bytes &&
            evidence[1].observation == UINT64_MAX;
        exact = exact && scenario.coordinator.onIssuedJobReleased(scenario.session, scenario.job);
        exact = exact && scenario.mailbox.releaseIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onSourcePublication();
    }
    const auto state = scenario.coordinator.snapshotForOwner();
    check("incremental terminal consumption exceeds capacity without loss or quarantine",
          exact && !state.sourceQuarantined && state.progress.accepted == count &&
          state.terminalEvidenceObserved == count && state.terminalEvidenceConsumed == count &&
          state.terminalEvidenceDropped == 0 && state.terminalEvidenceCount == 0);

    // Undrained history remains bounded and visibly overflows; consuming later
    // cannot clear quarantine, reconstruct dropped evidence, or rewrite trust.
    Scenario overflow;
    for (uint64_t i = 0; i <= kPearlRawTerminalEvidenceCapacity; ++i) {
        overflow.nextSerializedOrdinal = 1;
        overflow.prepareSubmission();
        overflow.terminal(PearlSourceDelivery::Sent,
                          PearlSourceTerminal::Accepted, true);
        if (i < kPearlRawTerminalEvidenceCapacity) {
            overflow.coordinator.onIssuedJobReleased(overflow.session, overflow.job);
            overflow.mailbox.releaseIssuedJob(overflow.session, overflow.job);
            overflow.coordinator.onSourcePublication();
        }
    }
    const auto before = overflow.coordinator.snapshotForOwner();
    PearlRawTerminalEvidence prefix[3];
    const bool noops = overflow.coordinator.consumeTerminalEvidence(nullptr, 3) == 0 &&
        overflow.coordinator.consumeTerminalEvidence(prefix, 0) == 0;
    const auto copied = overflow.coordinator.consumeTerminalEvidence(prefix, 3);
    const auto after = overflow.coordinator.snapshotForOwner();
    check("overflow remains explicit and partial consume preserves exact oldest-first prefix",
          noops && before.sourceQuarantined && before.terminalEvidenceDropped == 1 &&
          copied == 3 && prefix[0].observation == 1 && prefix[1].observation == 2 &&
          prefix[2].observation == 3 && prefix[2].trust == PearlRawTerminalTrust::Trusted &&
          after.sourceQuarantined && after.terminalEvidenceObserved == before.terminalEvidenceObserved &&
          after.terminalEvidenceDropped == 1 && after.terminalEvidenceConsumed == 3 &&
          after.terminalEvidenceCount == kPearlRawTerminalEvidenceCapacity - 3 &&
          after.terminalEvidence[0].observation == 4);
    bool otherThreadRefused = false;
    std::thread other([&] {
        otherThreadRefused = overflow.coordinator.consumeTerminalEvidence(prefix, 1) == 0;
    });
    other.join();
    const auto threaded = overflow.coordinator.snapshotForOwner();
    check("wrong-thread consumption cannot remove evidence",
          otherThreadRefused && threaded.terminalEvidenceConsumed == 3 &&
          threaded.terminalEvidenceCount == after.terminalEvidenceCount &&
          threaded.hasIntegrityFlag(PearlSourceIntegrityFlag::WrongThread));
    auto *alias = reinterpret_cast<PearlRawTerminalEvidence *>(&overflow.coordinator);
    check("overlapping consumption is refused without removing evidence",
          overflow.coordinator.consumeTerminalEvidence(alias, 1) == 0 &&
          overflow.coordinator.snapshotForOwner().terminalEvidenceConsumed == 3);
}

void testExhaustiveTerminalValidatorParity() {
    bool parity = true;
    std::size_t legal = 0;
    for (uint8_t deliveryValue = 0; deliveryValue <= 4; ++deliveryValue) {
        for (uint8_t terminalValue = 0; terminalValue <= 7;
             ++terminalValue) {
            for (uint8_t acknowledgedValue = 0; acknowledgedValue <= 1;
                 ++acknowledgedValue) {
                for (uint8_t supersededValue = 0; supersededValue <= 1;
                     ++supersededValue) {
                    for (uint8_t reasonValue = 0; reasonValue <= 9;
                         ++reasonValue) {
                        const auto delivery =
                            static_cast<PearlSourceDelivery>(deliveryValue);
                        const auto terminal =
                            static_cast<PearlSourceTerminal>(terminalValue);
                        const bool acknowledged = acknowledgedValue != 0;
                        const bool superseded = supersededValue != 0;
                        const auto reason =
                            static_cast<PearlSourceReason>(reasonValue);
                        const bool expected =
                            pearlSourceTerminalEvidenceValid(
                                delivery, terminal, acknowledged, superseded,
                                reason);
                        if (expected) ++legal;

                        Scenario scenario;
                        scenario.prepareSubmission();
                        const bool published =
                            scenario.mailbox.publishSubmitTerminal(
                                scenario.session, scenario.operation, delivery,
                                terminal, acknowledged, superseded, reason);
                        scenario.coordinator.onSourcePublication();
                        const auto state =
                            scenario.coordinator.snapshotForOwner();
                        parity = parity && published == expected &&
                                 state.sourceQuarantined == !expected;
                        if (expected) {
                            parity =
                                parity && state.terminalEvidenceCount == 1 &&
                                state.terminalEvidence[0].trust ==
                                    PearlRawTerminalTrust::Trusted;
                        } else {
                            parity = parity &&
                                     state.terminalEvidenceCount == 0;
                        }
                    }
                }
            }
        }
    }
    check("reason-aware terminal Cartesian matrix has producer/consumer parity",
          parity);
    check("reason-aware terminal Cartesian legal cardinality is exact",
          legal == 25);
}

class ThrowingSource final : public PearlSourceObservability {
   public:
    enum class Point { Drain, Snapshot };
    explicit ThrowingSource(Point point) : point_(point) {}

    std::size_t drain(PearlSourceEvent *, std::size_t) override {
        if (point_ == Point::Drain) throw std::runtime_error("drain");
        return 0;
    }

    PearlSourceObservabilitySnapshot snapshot() const override {
        if (point_ == Point::Snapshot) throw std::runtime_error("snapshot");
        return PearlSourceObservabilitySnapshot();
    }

   private:
    Point point_;
};

class ReentrantSource final : public PearlSourceObservability {
   public:
    PearlRunProgressCoordinator *coordinator = nullptr;

    std::size_t drain(PearlSourceEvent *out, std::size_t maximum) override {
        if (drained_ == 32 && coordinator) coordinator->onSourcePublication();
        const std::size_t remaining = 33 - drained_;
        const std::size_t count = remaining < maximum ? remaining : maximum;
        for (std::size_t i = 0; i < count; ++i) {
            const uint64_t token = drained_ + i + 1;
            PearlSourceEvent event;
            event.eventToken = token;
            event.timestampMs = token;
            event.sessionToken = 1;
            if (token == 1) {
                event.kind = PearlSourceEventKind::SessionStarted;
            } else {
                event.kind = PearlSourceEventKind::SourceFault;
                event.reason = PearlSourceReason::Transport;
            }
            out[i] = event;
        }
        drained_ += count;
        return count;
    }

    PearlSourceObservabilitySnapshot snapshot() const override {
        PearlSourceObservabilitySnapshot out;
        out.queued = 33 - drained_;
        out.publishedEvents = 33;
        out.drainedEvents = drained_;
        out.lastEventToken = 33;
        out.lastSessionToken = 1;
        out.lastTimestampMs = 33;
        out.activeSessionToken = 1;
        out.sourceFaultObserved = true;
        out.hasLastPublishedEvent = true;
        out.lastPublishedEvent.eventToken = 33;
        out.lastPublishedEvent.timestampMs = 33;
        out.lastPublishedEvent.sessionToken = 1;
        out.lastPublishedEvent.kind = PearlSourceEventKind::SourceFault;
        out.lastPublishedEvent.reason = PearlSourceReason::Transport;
        return out;
    }

   private:
    std::size_t drained_ = 0;
};

PearlSourceEvent sessionEvent(uint64_t eventToken = 1,
                              uint64_t sessionToken = 1,
                              uint64_t timestamp = 10) {
    PearlSourceEvent e;
    e.eventToken = eventToken;
    e.timestampMs = timestamp;
    e.sessionToken = sessionToken;
    e.kind = PearlSourceEventKind::SessionStarted;
    return e;
}

PearlSourceEvent connectedEvent(uint64_t eventToken,
                                uint64_t sessionToken,
                                uint64_t timestamp) {
    PearlSourceEvent e;
    e.eventToken = eventToken;
    e.timestampMs = timestamp;
    e.sessionToken = sessionToken;
    e.kind = PearlSourceEventKind::TransportConnected;
    return e;
}

void runInvalidScript(const char *name,
                      const std::vector<PearlSourceEvent> &events) {
    ManualClock clock;
    clock.value = 100;
    PearlProgressBus bus;
    ScriptSource source;
    for (const auto &event : events) source.events.push_back(event);
    if (!events.empty()) {
        source.state.lastEventToken = events.back().eventToken;
        source.state.lastTimestampMs = events.back().timestampMs;
        source.state.lastSessionToken = events.front().sessionToken;
        source.state.hasLastPublishedEvent = true;
        source.state.lastPublishedEvent = events.back();
    }
    PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read, &clock);
    check("attach scripted source", coordinator.attachSource(&source));
    coordinator.drainCheckpoint();
    const auto state = coordinator.snapshotForOwner();
    check(name, state.sourceQuarantined &&
                    state.hasIntegrityFlag(
                        PearlSourceIntegrityFlag::InvalidEvent));
}

void testInvalidSequencesAndIdentity() {
    runInvalidScript("gap quarantines", {sessionEvent(2, 1, 10)});
    runInvalidScript("duplicate quarantines",
                     {sessionEvent(), connectedEvent(1, 1, 11)});
    runInvalidScript("reorder quarantines",
                     {sessionEvent(), connectedEvent(3, 1, 11),
                      connectedEvent(2, 1, 12)});
    runInvalidScript("wrong session identity quarantines",
                     {sessionEvent(), connectedEvent(2, 2, 11)});
    runInvalidScript("timestamp rollback quarantines",
                     {sessionEvent(1, 1, 11), connectedEvent(2, 1, 10)});

    PearlSourceEvent malformed = sessionEvent();
    malformed.reserved1 = 1;
    runInvalidScript("reserved field quarantines", {malformed});
    malformed = sessionEvent();
    malformed.jobIdentity = digest(3);
    runInvalidScript("forbidden digest quarantines", {malformed});
}

void testTamperedTerminalEvidence() {
    for (MutatingSource::Mutation mutation :
          {MutatingSource::Mutation::Digest,
           MutatingSource::Mutation::TerminalTuple,
           MutatingSource::Mutation::TerminalReason,
           MutatingSource::Mutation::MissingUnknownReason,
           MutatingSource::Mutation::TerminalEvidenceCrossClass,
           MutatingSource::Mutation::UnknownTerminalEvidence}) {
        ManualClock clock;
        PearlProgressBus bus;
        PearlSourceObservabilityMailbox mailbox(clock);
        MutatingSource source(mailbox, mutation);
        PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read,
                                                &clock);
        coordinator.attachSource(&source);
        const PearlJobIdentityDigest identity = digest(61);
        const uint64_t session = mailbox.publishSessionStarted();
        coordinator.onSourcePublication();
        clock.tick();
        mailbox.publishTransportConnected(session);
        coordinator.onSourcePublication();
        clock.tick();
        const uint64_t authorization =
            mailbox.publishAuthorizationSucceeded(session);
        coordinator.onSourcePublication();
        clock.tick();
        const uint64_t job =
            mailbox.publishJobAvailable(session, authorization, identity);
        coordinator.onSourcePublication();
        check("tamper fixture source bind", mailbox.bindIssuedJob(session, job));
        check("tamper fixture coordinator bind",
              coordinator.onIssuedJobBound(session, job, identity));
        check("tamper fixture prepare start", coordinator.onPrepareStarted());
        check("tamper fixture prepare complete", coordinator.onPrepareCompleted());
        check("tamper fixture search start", coordinator.onSearchStarted(1));
        clock.tick();
        coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptStarted);
        coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptCompleted);
        coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsObserved);
        coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsRetained);
        coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::FinalizerStarted);
        coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::CandidateSelected);
        coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HostValid);
        coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::ProofSerialized);
        coordinator.onSearchCompleted(true);
        const uint64_t candidate = coordinator.onCandidateObserved(
            session, authorization, job, identity,
            coordinator.snapshotForOwner().progress.searchGeneration, 1);
        coordinator.onCandidateFinalized(candidate, true);
        coordinator.onSubmitAttempt();
        const uint64_t operation = mailbox.publishSubmitDispatched(
            session, job, PearlSourceDelivery::NotSent);
        coordinator.onSourcePublication();
        clock.tick();
        const bool missingUnknownReason =
            mutation == MutatingSource::Mutation::MissingUnknownReason;
        mailbox.publishSubmitTerminal(
            session, operation, PearlSourceDelivery::Sent,
            missingUnknownReason ? PearlSourceTerminal::Unknown
                                 : PearlSourceTerminal::Accepted,
            !missingUnknownReason, false,
            missingUnknownReason ? PearlSourceReason::Timeout
                                 : PearlSourceReason::None);
        coordinator.onSourcePublication();
        const auto state = coordinator.snapshotForOwner();
        const char *name = mutation == MutatingSource::Mutation::Digest
                               ? "wrong terminal digest quarantines"
                           : mutation == MutatingSource::Mutation::TerminalTuple
                               ? "illegal terminal tuple quarantines"
                           : mutation == MutatingSource::Mutation::TerminalReason
                               ? "unexpected terminal reason quarantines"
                           : mutation == MutatingSource::Mutation::MissingUnknownReason
                               ? "missing unknown reason quarantines"
                           : mutation == MutatingSource::Mutation::TerminalEvidenceCrossClass
                               ? "cross-class terminal evidence quarantines"
                               : "unknown terminal evidence quarantines";
        check(name,
              state.sourceQuarantined &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::InvalidEvent));
        check("tampered terminal fabricates no accepted outcome",
              state.progress.accepted == 0 && state.liveness.outcomes == 0);
    }


    for (SnapshotTamperSource::Mutation mutation :
         {SnapshotTamperSource::Mutation::LastEventField,
          SnapshotTamperSource::Mutation::MissingLastEvent}) {
        ManualClock clock;
        PearlProgressBus bus;
        PearlSourceObservabilityMailbox mailbox(clock);
        SnapshotTamperSource source(mailbox, mutation);
        PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read,
                                                &clock);
        coordinator.attachSource(&source);
        mailbox.publishSessionStarted();
        coordinator.onSourcePublication();
        const auto state = coordinator.snapshotForOwner();
        check(mutation == SnapshotTamperSource::Mutation::LastEventField
                  ? "snapshot last-event field tamper quarantines"
                  : "snapshot missing last-event marker quarantines",
              state.sourceQuarantined &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::SnapshotAccountingInvalid));
        check("untrusted snapshot rolls translation back",
              state.liveness.sessionGeneration == 0 &&
                  state.liveness.connection ==
                      PearlConnectionState::Disconnected &&
                  state.progress.phase == PearlProgressPhase::Idle);
    }
}

void testMaliciousTerminalBypassesProducerValidation() {
    ManualClock clock;
    PearlProgressBus bus;
    PearlSourceObservabilityMailbox mailbox(clock);
    MutatingSource source(mailbox, MutatingSource::Mutation::TerminalTuple);
    PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read, &clock);
    coordinator.attachSource(&source);
    const PearlJobIdentityDigest identity = digest(177);
    const uint64_t session = mailbox.publishSessionStarted();
    coordinator.onSourcePublication();
    clock.tick();
    mailbox.publishTransportConnected(session);
    coordinator.onSourcePublication();
    clock.tick();
    const uint64_t authorization =
        mailbox.publishAuthorizationSucceeded(session);
    coordinator.onSourcePublication();
    clock.tick();
    const uint64_t job =
        mailbox.publishJobAvailable(session, authorization, identity);
    coordinator.onSourcePublication();
    const uint64_t operation = prepareDirectSubmission(
        clock, mailbox, coordinator, session, authorization, job, identity);
    check("malicious tuple fixture starts without producer refusal",
          mailbox.snapshot().refusedEvents == 0);
    clock.tick();
    check("producer accepts the original legal terminal",
          mailbox.publishSubmitTerminal(
              session, operation, PearlSourceDelivery::Sent,
              PearlSourceTerminal::Accepted, true, false,
              PearlSourceReason::None));
    check("producer did not refuse before consumer-side mutation",
          mailbox.snapshot().refusedEvents == 0);
    coordinator.onSourcePublication();
    const auto state = coordinator.snapshotForOwner();
    const PearlSourceEvent &raw = state.terminalEvidence[0].event;
    check("consumer rejects malicious tuple independently of producer",
          state.sourceQuarantined && state.sourceRefusedEvents == 0 &&
              state.hasIntegrityFlag(PearlSourceIntegrityFlag::InvalidEvent) &&
              state.progress.accepted == 0 &&
              state.terminalEvidenceCount == 1 &&
              state.terminalEvidence[0].trust ==
                  PearlRawTerminalTrust::Untrusted &&
              !pearlSourceTerminalEvidenceValid(
                  raw.delivery, raw.terminal, raw.acknowledged != 0,
                  raw.superseded != 0, raw.reason));
}

void testDrainReentryExceptionsAndAlgorithmValidation() {
    for (ThrowingSource::Point point :
         {ThrowingSource::Point::Drain, ThrowingSource::Point::Snapshot}) {
        ManualClock clock;
        PearlProgressBus bus;
        ThrowingSource source(point);
        PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read,
                                                &clock);
        coordinator.attachSource(&source);
        check("throwing source cannot escape noexcept drain",
              coordinator.drainCheckpoint() == 0);
        const auto state = coordinator.snapshotForOwner();
        check("source exception is explicit",
              state.sourceQuarantined &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::SourceException));
    }

    {
        ManualClock clock;
        PearlProgressBus bus;
        ReentrantSource source;
        PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read,
                                                &clock);
        source.coordinator = &coordinator;
        coordinator.attachSource(&source);
        PearlProgressSnapshot before;
        bus.tryRead(&before);
        check("reentrant drain returns without recursion",
              coordinator.drainCheckpoint() == 33);
        const auto state = coordinator.snapshotForOwner();
        PearlProgressSnapshot after;
        bus.tryRead(&after);
        check("reentrant drain is explicit",
              state.sourceQuarantined &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::ReentrantDrain));
        check("reentry cannot publish staged source translation",
              before.sessionGeneration == 0 && after.sessionGeneration == 0 &&
                  state.liveness.sessionGeneration == 0 &&
                  after.phase == PearlProgressPhase::Idle);
    }

    {
        Scenario scenario;
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptCompleted);
        const auto state = scenario.coordinator.snapshotForOwner();
        check("out-of-order algorithm callback quarantines",
              state.sourceQuarantined &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::InvalidAlgorithmEvent));
        check("invalid algorithm callback publishes no counter",
              state.progress.attemptsCompleted == 0);
    }

    {
        Scenario scenario;
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        scenario.prepareBoundJob();
        scenario.coordinator.onSearchStarted(1);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptStarted, 0);
        const auto state = scenario.coordinator.snapshotForOwner();
        check("zero algorithm amount quarantines before mutation",
              state.sourceQuarantined && state.progress.attemptsStarted == 0 &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::InvalidAlgorithmEvent));
    }

    {
        Scenario scenario;
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        scenario.prepareBoundJob();
        scenario.coordinator.onSearchStarted(1);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsObserved, 0);
        const auto state = scenario.coordinator.snapshotForOwner();
        check("zero-hit callback still requires completed attempt",
              state.sourceQuarantined && state.progress.hitsObserved == 0 &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::InvalidAlgorithmEvent));
    }

    auto primeFinalizer = [](Scenario &scenario) {
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        scenario.prepareBoundJob();
        scenario.coordinator.onSearchStarted(1);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptStarted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptCompleted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsObserved);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsRetained);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::FinalizerStarted);
    };

    {
        Scenario scenario;
        primeFinalizer(scenario);
        check("open finalizer cannot be hidden by search completion",
              !scenario.coordinator.onSearchCompleted(true));
        check("unterminated finalizer is explicit",
              scenario.coordinator.snapshotForOwner().hasIntegrityFlag(
                  PearlSourceIntegrityFlag::InvalidAlgorithmEvent));
    }

    {
        Scenario scenario;
        primeFinalizer(scenario);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::CandidateSelected);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HostInvalid);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::FinalizerProgress);
        const auto state = scenario.coordinator.snapshotForOwner();
        check("host-invalid closes the finalizer",
              state.sourceQuarantined && state.progress.hostInvalid == 1 &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::InvalidAlgorithmEvent));
    }

    {
        Scenario scenario;
        primeFinalizer(scenario);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::CandidateSelected);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HostValid);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::CandidateSelected);
        const auto state = scenario.coordinator.snapshotForOwner();
        check("candidate selection is single-use per finalizer",
              state.sourceQuarantined &&
                  state.progress.candidatesSelected == 1 &&
                  state.progress.hostValid == 1);
    }

    {
        Scenario scenario;
        primeFinalizer(scenario);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::ProofSerialized);
        const auto state = scenario.coordinator.snapshotForOwner();
        check("proof requires a host-valid open finalizer",
              state.sourceQuarantined && state.progress.proofsSerialized == 0);
    }

    {
        Scenario scenario;
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        scenario.prepareBoundJob();
        scenario.coordinator.onSearchStarted(2);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptStarted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptCompleted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsObserved);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsRetained);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::FinalizerStarted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::CandidateSelected);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HostValid);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::ProofSerialized);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptStarted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptCompleted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsObserved, 0);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsRetained, 0);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::FinalizerStarted);
        const auto state = scenario.coordinator.snapshotForOwner();
        check("prior-attempt hits cannot authorize zero-hit finalizer",
              state.sourceQuarantined &&
                  state.progress.finalizersStarted == 1);
    }

    {
        Scenario scenario;
        scenario.mailbox.bindIssuedJob(scenario.session, scenario.job);
        scenario.coordinator.onIssuedJobBound(
            scenario.session, scenario.job, scenario.identity);
        scenario.prepareBoundJob();
        scenario.coordinator.onSearchStarted(1);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptStarted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::AttemptCompleted);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsObserved, 2);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::HitsRetained, 1);
        scenario.coordinator.onPearlAlgorithmProgress(
            PearlAlgorithmProgressEvent::FinalizerStarted);
        const auto state = scenario.coordinator.snapshotForOwner();
        check("unreported per-attempt saturation cannot leak eligibility",
              state.sourceQuarantined &&
                  state.progress.finalizersStarted == 0 &&
                  state.progress.hitSaturationEvents == 0);
    }
}

void testRefusalFailClosedAndBudget() {
    for (std::size_t depth : {std::size_t(31), std::size_t(32),
                              std::size_t(33), std::size_t(255),
                              std::size_t(256)}) {
        ManualClock clock;
        PearlProgressBus bus;
        PearlSourceObservabilityMailbox mailbox(clock);
        PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read,
                                                &clock);
        coordinator.attachSource(&mailbox);
        const uint64_t session = mailbox.publishSessionStarted();
        for (std::size_t i = 1; i < depth; ++i)
            check("fill legal depth fixture",
                  mailbox.publishSourceFault(session,
                                             PearlSourceReason::Transport));
        check("bounded depth drains completely",
              coordinator.drainCheckpoint() == depth &&
                  mailbox.snapshot().queued == 0);
        const auto state = coordinator.snapshotForOwner();
        check("pre-drain queue high-water is exact",
              state.sourceQueueHighWater == depth);
        check("legal bounded depth does not quarantine",
              !state.sourceQuarantined);
    }

    {
        ManualClock clock;
        PearlProgressBus bus;
        PearlSourceObservabilityMailbox mailbox(clock);
        PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read,
                                                &clock);
        coordinator.attachSource(&mailbox);
        check("invalid publisher call is refused",
              !mailbox.publishTransportConnected(99));
        coordinator.drainCheckpoint();
        const auto state = coordinator.snapshotForOwner();
        check("refusal is explicit and quarantined",
              state.sourceQuarantined && state.sourceRefusedEvents == 1 &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::PublicationRefused));
        check("refusal fabricates no mining progress",
              state.progress.submitAttempts == 0 && state.progress.accepted == 0 &&
                  state.liveness.outcomes == 0);
    }

    {
        ManualClock clock;
        PearlProgressBus bus;
        PearlSourceObservabilityMailbox mailbox(clock);
        PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read,
                                                &clock);
        coordinator.attachSource(&mailbox);
        const uint64_t session = mailbox.publishSessionStarted();
        check("overflow setup session", session != 0);
        for (std::size_t i = 1;
             i < PearlSourceObservabilityMailbox::kCapacity; ++i)
            check("fill mailbox with legal source faults",
                  mailbox.publishSourceFault(session,
                                             PearlSourceReason::Transport));
        check("overflow publication fails closed",
              !mailbox.publishSourceFault(session,
                                          PearlSourceReason::Transport));
        coordinator.drainCheckpoint();
        const auto state = coordinator.snapshotForOwner();
        check("overflow is explicit",
              state.sourceQuarantined &&
                  (state.sourceMailboxFaultBits &
                   pearlSourceMailboxFaultBit(
                       PearlSourceMailboxFault::Overflow)) != 0 &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::MailboxFailClosed));
        check("overflow fabricates no share outcome",
              state.progress.accepted == 0 && state.progress.rejected == 0 &&
                  state.progress.unknown == 0 && state.liveness.outcomes == 0);
    }

    {
        ManualClock clock;
        PearlProgressBus bus;
        ScriptSource source;
        source.endless = true;
        PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read,
                                                &clock);
        coordinator.attachSource(&source);
        PearlProgressSnapshot before;
        check("read progress before budget test", bus.tryRead(&before));
        check("bounded drain consumes exactly 32x8",
              coordinator.drainCheckpoint() ==
                  PearlRunProgressCoordinator::kDrainBatch *
                      PearlRunProgressCoordinator::kDrainPasses);
        PearlProgressSnapshot after;
        check("read progress after budget test", bus.tryRead(&after));
        const auto state = coordinator.snapshotForOwner();
        check("remaining queue latches budget",
              state.sourceQuarantined &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::ConsumerBudgetExceeded));
        check("budget fabricates no mining counters",
              before.submitAttempts == after.submitAttempts &&
                  before.accepted == after.accepted &&
                  before.unknown == after.unknown);
        check("quarantine still drains and discards",
              coordinator.drainCheckpoint() ==
                  PearlRunProgressCoordinator::kDrainBatch *
                      PearlRunProgressCoordinator::kDrainPasses);
    }

    {
        ManualClock clock;
        PearlProgressBus bus;
        ScriptSource source;
        source.events.push_back(sessionEvent());
        source.state.lastEventToken = 1;
        source.state.lastTimestampMs = 10;
        source.state.lastSessionToken = 1;
        source.state.hasLastPublishedEvent = true;
        source.state.lastPublishedEvent = source.events.front();
        source.rollbackOnSnapshot = true;
        PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read,
                                                &clock);
        coordinator.attachSource(&source);
        coordinator.drainCheckpoint();
        const auto state = coordinator.snapshotForOwner();
        check("snapshot rollback quarantines",
              state.sourceQuarantined &&
                  state.hasIntegrityFlag(
                      PearlSourceIntegrityFlag::SnapshotRollback));
    }
}

void testQuarantineDiscardsLaterFacts() {
    ManualClock clock;
    PearlProgressBus bus;
    ScriptSource source;
    source.events.push_back(sessionEvent(2, 1, 10));
    source.state.lastEventToken = 2;
    source.state.lastTimestampMs = 10;
    source.state.lastSessionToken = 1;
    source.state.hasLastPublishedEvent = true;
    source.state.lastPublishedEvent = source.events.front();
    PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read, &clock);
    coordinator.attachSource(&source);
    coordinator.drainCheckpoint();
    const auto before = coordinator.snapshotForOwner();

    source.events.push_back(sessionEvent(3, 1, 20));
    source.state.lastEventToken = 3;
    source.state.lastTimestampMs = 20;
    source.state.publishedEvents += 1;
    coordinator.drainCheckpoint();
    const auto after = coordinator.snapshotForOwner();
    check("post-quarantine event is drained",
          after.sourceEventsDrained == before.sourceEventsDrained + 1);
    check("post-quarantine event cannot refresh liveness",
          after.liveness.sessionGeneration == before.liveness.sessionGeneration &&
              after.liveness.connection == before.liveness.connection);
    check("post-quarantine explicit state remains latched",
          after.sourceQuarantined && after.sourceIntegrityFlags != 0);
}

void testNullSourceAndWrongThread() {
    ManualClock clock;
    PearlProgressBus bus;
    PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read, &clock);
    const uint64_t beforeSequence = bus.sequence();
    check("null source attachment is supported", coordinator.attachSource(nullptr));
    check("null source checkpoint drains nothing",
          coordinator.drainCheckpoint() == 0 &&
              bus.sequence() == beforeSequence);

    std::thread wrong([&]() { coordinator.onSourcePublication(); });
    wrong.join();
    check("wrong-thread hook itself does not publish",
          bus.sequence() == beforeSequence &&
              coordinator.publishedSourceIntegrityFlags() == 0);
    // Any next owner API must absorb and publish the pending notification;
    // this deliberately does not call an explicit drain.
    const auto state = coordinator.snapshotForOwner();
    check("owner publishes explicit wrong-thread quarantine",
              state.sourceQuarantined &&
              state.hasIntegrityFlag(PearlSourceIntegrityFlag::WrongThread) &&
              (coordinator.publishedSourceIntegrityFlags() &
               pearlSourceIntegrityBit(
                   PearlSourceIntegrityFlag::WrongThread)) != 0 &&
              bus.sequence() == beforeSequence + 2);
}

void testSoleWriterCoherentSnapshots() {
    PearlProgressBus bus;
    std::atomic<bool> running{true};
    std::atomic<bool> coherent{true};
    std::thread writer([&]() {
        ManualClock clock;
        PearlSourceObservabilityMailbox mailbox(clock);
        PearlRunProgressCoordinator coordinator(&bus, &ManualClock::read,
                                                &clock);
        coordinator.attachSource(&mailbox);
        const uint64_t session = mailbox.publishSessionStarted();
        coordinator.onSourcePublication();
        clock.tick();
        mailbox.publishTransportConnected(session);
        coordinator.onSourcePublication();
        clock.tick();
        mailbox.publishSessionReadyNoAuth(session);
        coordinator.onSourcePublication();
        clock.tick();
        mailbox.publishJobAvailable(session, 0, digest(40));
        coordinator.onSourcePublication();
        const uint64_t job = mailbox.snapshot().activeJobToken;
        mailbox.bindIssuedJob(session, job);
        coordinator.onIssuedJobBound(session, job, digest(40));
        coordinator.onPrepareStarted();
        coordinator.onPrepareCompleted();
        coordinator.onSearchStarted(
            PearlRunProgressCoordinator::kProofBacklogCapacity);
        for (std::size_t i = 0;
             i < PearlRunProgressCoordinator::kProofBacklogCapacity; ++i) {
            clock.tick();
            coordinator.onPearlAlgorithmProgress(
                PearlAlgorithmProgressEvent::AttemptStarted);
            coordinator.onPearlAlgorithmProgress(
                PearlAlgorithmProgressEvent::AttemptCompleted);
            coordinator.onPearlAlgorithmProgress(
                PearlAlgorithmProgressEvent::HitsObserved, 0);
            coordinator.onPearlAlgorithmProgress(
                PearlAlgorithmProgressEvent::HitsRetained, 0);
        }
        coordinator.onSearchCompleted(true);
        running.store(false, std::memory_order_release);
    });
    while (running.load(std::memory_order_acquire)) {
        PearlProgressSnapshot sample;
        if (bus.tryRead(&sample) && !sample.invariantsHold())
            coherent.store(false, std::memory_order_relaxed);
    }
    writer.join();
    PearlProgressSnapshot final;
    check("final coherent bus read", bus.tryRead(&final));
    check("single coordinator writer publishes coherent snapshots",
          coherent.load(std::memory_order_relaxed) &&
              final.attemptsStarted ==
                  PearlRunProgressCoordinator::kProofBacklogCapacity &&
              final.attemptsCompleted ==
                  PearlRunProgressCoordinator::kProofBacklogCapacity &&
              final.invariantsHold());
}

}  // namespace

int main() {
    testPoolLifecycleAndTerminalMappings();
    testReplacementDuringInflight();
    testSearchAttemptConservation();
    testPrepareLifecycle();
    testPrepareCacheReuse();
    testMixedReturnedSolutionsUseSerializedOrdinals();
    testOuterCandidateBoundaries();
    testAtomicJobReplacementExhaustion();
    testOutOfOrderRunLoopBoundaries();
    testSubmitReturnReconciliation();
    testExactBindDigestAndDisconnectReconciliation();
    testDisconnectPostSnapshotConfirmation();
    testRawTerminalEvidenceSurvivesRollback();
    testTerminalEvidenceConsumption();
    testDisconnectedSearchRetirement();
    testExhaustiveTerminalValidatorParity();
    testInvalidSequencesAndIdentity();
    testTamperedTerminalEvidence();
    testMaliciousTerminalBypassesProducerValidation();
    testDrainReentryExceptionsAndAlgorithmValidation();
    testRefusalFailClosedAndBudget();
    testQuarantineDiscardsLaterFacts();
    testNullSourceAndWrongThread();
    testSoleWriterCoherentSnapshots();
    if (failures != 0) {
        std::fprintf(stderr, "%d coordinator test(s) failed\n", failures);
        return 1;
    }
    std::puts("pearl run progress coordinator tests passed");
    return 0;
}
