#include "../src/core/pearl_observer.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <new>
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

struct OwnedObserverBus {
    PearlObserverBus bus;
    PearlObserverBus::Writer writer;

    OwnedObserverBus() : writer(bus.acquireWriter()) {}
    explicit OwnedObserverBus(uint64_t sequence)
        : bus(sequence), writer(bus.acquireWriter()) {}

    bool publish(const PearlObserverSnapshot &snapshot) {
        return writer.publish(snapshot);
    }
    bool publishWithMidpointHookForTest(const PearlObserverSnapshot &snapshot,
                                        void (*hook)(void *) noexcept,
                                        void *context) {
        return writer.publishWithMidpointHookForTest(snapshot, hook, context);
    }
    bool tryRead(PearlObserverSnapshot *out,
                 unsigned attempts = 128) const {
        return bus.tryRead(out, attempts);
    }
    bool tryReadWithAfterFirstSequenceHookForTest(
        PearlObserverSnapshot *out, unsigned attempts,
        void (*hook)(void *), void *context) const {
        return bus.tryReadWithAfterFirstSequenceHookForTest(
            out, attempts, hook, context);
    }
    uint64_t sequence() const { return bus.sequence(); }
};

PearlJobIdentityDigest digest(uint64_t tag) {
    PearlJobIdentityDigest out;
    for (std::size_t i = 0; i < out.bytes.size(); ++i)
        out.bytes[i] =
            static_cast<uint8_t>((tag * 17 + i * 13 + 1) & 0xff);
    return out;
}

void stampSourceEvent(PearlObserverSnapshot *out,
                      PearlSourceEventKind kind,
                      uint64_t sessionToken) {
    out->lastSourceEvent = PearlSourceEvent();
    out->lastSourceEvent.eventToken = out->lastSourceEventToken;
    out->lastSourceEvent.timestampMs = out->lastSourceEventMs;
    out->lastSourceEvent.sessionToken = sessionToken;
    out->lastSourceEvent.kind = kind;
}

void stampTerminalEvent(PearlObserverSnapshot *out,
                        const PearlObserverSnapshot &dispatched,
                        PearlSourceTerminal terminal) {
    stampSourceEvent(out, PearlSourceEventKind::SubmitTerminal,
                     dispatched.inFlightSessionToken);
    out->lastSourceEvent.authorizationToken =
        dispatched.inFlightAuthorizationToken;
    out->lastSourceEvent.jobToken = dispatched.inFlightJobToken;
    out->lastSourceEvent.operationToken =
        dispatched.inFlightOperationToken;
    out->lastSourceEvent.jobIdentity = dispatched.inFlightJobIdentity;
    out->lastSourceEvent.delivery = PearlSourceDelivery::Sent;
    out->lastSourceEvent.terminal = terminal;
    out->lastSourceEvent.terminalEvidence =
        PearlSourceTerminalEvidence::CorrelatedRemoteOutcome;
    out->lastSourceEvent.acknowledged = 1;
}

void stampSourceFaultEvent(PearlObserverSnapshot *out,
                           PearlSourceReason reason) {
    stampSourceEvent(out, PearlSourceEventKind::SourceFault,
                     out->sourceSessionToken);
    out->lastSourceEvent.authorizationToken =
        out->sourceAuthorizationToken;
    out->lastSourceEvent.jobToken = out->activeJobToken;
    out->lastSourceEvent.jobIdentity = out->activeJobIdentity;
    out->lastSourceEvent.reason = reason;
}

PearlObserverSnapshot tagged(uint64_t tag) {
    PearlObserverSnapshot out;
    out.progress.phase = PearlProgressPhase::Searching;
    out.progress.sessionGeneration = tag;
    out.progress.authorizationGeneration = tag;
    out.progress.jobGeneration = tag;
    out.progress.prepareGeneration = tag;
    out.progress.searchGeneration = tag;
    out.progress.searchesStarted = tag;
    out.progress.searchesCompleted = tag - 1;
    out.progress.attemptsStarted = tag;
    out.progress.attemptsCompleted = tag;
    out.progress.publishedAtMs = tag * 10;

    out.liveness.connection = PearlConnectionState::Ready;
    out.liveness.jobProgress = PearlJobProgressState::Active;
    out.liveness.sessionGeneration = tag;
    out.liveness.jobGeneration = tag;
    out.liveness.completedAttempts = tag;
    out.liveness.completedAttemptsForJob = 1;
    out.liveness.attemptDeadline.state = PearlDeadlineState::Disabled;

    out.sourceState = PearlObserverSourceState::Trusted;
    out.sourceConnected = true;
    out.sourceReady = true;
    out.physicalTransportConnected = true;
    out.sourceEventsDrained = tag;
    out.sourceQueueHighWater = tag;
    out.lastSourceEventToken = tag;
    out.lastSourceEventMs = tag * 10;
    out.sourceSessionToken = 100000 + tag;
    out.sourceAuthorizationToken = 200000 + tag;
    out.activeJobToken = 300000 + tag;
    out.issuedSessionToken = out.sourceSessionToken;
    out.issuedAuthorizationToken = out.sourceAuthorizationToken;
    out.issuedJobToken = out.activeJobToken;
    out.issuedBindingGeneration = 1;
    out.issuedBindingCause = PearlIssuedBindingCause::DifferentJobAdoption;
    out.activeJobIdentity = digest(tag);
    out.issuedJobIdentity = out.activeJobIdentity;
    out.publishedAtMs = tag * 10;
    stampSourceEvent(&out, PearlSourceEventKind::JobAvailable,
                     out.sourceSessionToken);
    out.lastSourceEvent.authorizationToken =
        out.sourceAuthorizationToken;
    out.lastSourceEvent.jobToken = out.activeJobToken;
    out.lastSourceEvent.jobIdentity = out.activeJobIdentity;
    return out;
}

void addSubmitFunnel(PearlObserverSnapshot *out) {
    out->progress.hitsObserved = 1;
    out->progress.hitsRetained = 1;
    out->progress.finalizersStarted = 1;
    out->progress.finalizerGeneration = 1;
    out->progress.candidatesSelected = 1;
    out->progress.hostChecked = 1;
    out->progress.hostValid = 1;
    out->progress.proofsSerialized = 1;
    out->progress.candidateGeneration = 1;
    out->progress.submitGeneration = 1;
    out->progress.submitAttempts = 1;
    out->liveness.candidateGeneration = 1;
    out->liveness.submitGeneration = 1;
    out->liveness.candidatesObserved = 1;
    out->liveness.candidatesFinalized = 1;
    out->liveness.submitAttempts = 1;
}

PearlObserverSnapshot accepted(uint64_t tag) {
    PearlObserverSnapshot out = tagged(tag);
    addSubmitFunnel(&out);
    out.progress.submitted = 1;
    out.progress.acknowledged = 1;
    out.progress.accepted = 1;
    out.progress.lastOutcome = PearlSubmitOutcome::Accepted;
    out.liveness.acknowledgements = 1;
    out.liveness.outcomes = 1;
    out.liveness.lastOutcome = PearlSubmitOutcome::Accepted;
    out.liveness.lastOutcomeEvidence =
        PearlOutcomeEvidence::CorrelatedRemoteOutcome;
    out.liveness.lastOutcomeSubmitGeneration = 1;
    out.liveness.outcomeFreshness.state = PearlDeadlineState::Disabled;
    return out;
}

PearlObserverSnapshot saturatedTerminal() {
    const uint64_t max = std::numeric_limits<uint64_t>::max();
    PearlObserverSnapshot out = tagged(1);
    out.progress.finalizerGeneration = max;
    out.progress.candidateGeneration = max;
    out.progress.submitGeneration = max;
    out.progress.attemptsStarted = max;
    out.progress.attemptsCompleted = max;
    out.progress.hitsObserved = max;
    out.progress.hitsRetained = max;
    out.progress.finalizersStarted = max;
    out.progress.candidatesSelected = max;
    out.progress.hostChecked = max;
    out.progress.hostValid = max;
    out.progress.proofsSerialized = max;
    out.progress.submitAttempts = max;
    out.progress.submitted = max;
    out.progress.acknowledged = max;
    // Preserve exact terminal conservation while leaving one final Rejected
    // increment available to reach UINT64_MAX without wrapping.
    out.progress.rejected = max - 1;
    out.progress.pending = 1;
    out.progress.phase = PearlProgressPhase::AwaitingOutcome;
    out.progress.lastOutcome = PearlSubmitOutcome::None;
    out.liveness.candidateGeneration = max;
    out.liveness.submitGeneration = max;
    out.liveness.lastOutcomeSubmitGeneration = 0;
    out.liveness.completedAttempts = max;
    out.liveness.candidatesObserved = max;
    out.liveness.candidatesFinalized = max;
    out.liveness.submitAttempts = max;
    out.liveness.acknowledgements = max;
    out.liveness.outcomes = max - 1;
    out.liveness.lastOutcome = PearlSubmitOutcome::None;
    out.liveness.lastOutcomeEvidence = PearlOutcomeEvidence::None;
    out.liveness.candidate = PearlCandidateState::AwaitingOutcome;
    out.liveness.candidateDeadline.state = PearlDeadlineState::Disabled;
    out.unresolvedSubmit = true;
    out.sourceDispatchObserved = true;
    out.unresolvedSubmitGeneration = max;
    out.inFlightOperationToken = 700001;
    out.inFlightSessionToken = out.sourceSessionToken;
    out.inFlightAuthorizationToken = out.sourceAuthorizationToken;
    out.inFlightJobToken = out.issuedJobToken;
    out.inFlightJobIdentity = out.issuedJobIdentity;
    stampSourceEvent(&out, PearlSourceEventKind::SubmitDispatched,
                     out.inFlightSessionToken);
    out.lastSourceEvent.authorizationToken =
        out.inFlightAuthorizationToken;
    out.lastSourceEvent.jobToken = out.inFlightJobToken;
    out.lastSourceEvent.operationToken = out.inFlightOperationToken;
    out.lastSourceEvent.jobIdentity = out.inFlightJobIdentity;
    out.lastSourceEvent.delivery = PearlSourceDelivery::NotSent;
    return out;
}

PearlObserverSnapshot possiblyDelivered(uint64_t tag) {
    PearlObserverSnapshot out = tagged(tag);
    addSubmitFunnel(&out);
    out.progress.lastOutcome = PearlSubmitOutcome::Unknown;
    out.liveness.possiblyDeliveredUnknowns = 1;
    out.liveness.outcomes = 1;
    out.liveness.lastOutcome = PearlSubmitOutcome::Unknown;
    out.liveness.lastOutcomeEvidence =
        PearlOutcomeEvidence::PossiblyDeliveredUnknown;
    out.liveness.lastOutcomeSubmitGeneration = 1;
    out.liveness.outcomeFreshness.state = PearlDeadlineState::Disabled;
    return out;
}

PearlObserverSnapshot openDispatchedSubmit(uint64_t tag) {
    PearlObserverSnapshot out = tagged(tag);
    addSubmitFunnel(&out);
    out.progress.phase = PearlProgressPhase::AwaitingAcknowledgement;
    out.liveness.candidate =
        PearlCandidateState::AwaitingAcknowledgement;
    out.liveness.candidateDeadline.state = PearlDeadlineState::Disabled;
    out.unresolvedSubmit = true;
    out.unresolvedSubmitGeneration = 1;
    out.sourceDispatchObserved = true;
    out.inFlightOperationToken = 700001;
    out.inFlightSessionToken = out.sourceSessionToken;
    out.inFlightAuthorizationToken = out.sourceAuthorizationToken;
    out.inFlightJobToken = out.issuedJobToken;
    out.inFlightJobIdentity = out.issuedJobIdentity;
    stampSourceEvent(&out, PearlSourceEventKind::SubmitDispatched,
                     out.inFlightSessionToken);
    out.lastSourceEvent.authorizationToken =
        out.inFlightAuthorizationToken;
    out.lastSourceEvent.jobToken = out.inFlightJobToken;
    out.lastSourceEvent.operationToken = out.inFlightOperationToken;
    out.lastSourceEvent.jobIdentity = out.inFlightJobIdentity;
    out.lastSourceEvent.delivery = PearlSourceDelivery::NotSent;
    return out;
}

PearlObserverSnapshot terminalFromDispatch(
    const PearlObserverSnapshot &dispatched,
    PearlSubmitOutcome outcome, PearlOutcomeEvidence evidence,
    PearlSourceDelivery delivery, PearlSourceTerminal terminal,
    bool acknowledged, PearlSourceReason reason) {
    PearlObserverSnapshot out = dispatched;
    out.progress.phase = PearlProgressPhase::Searching;
    out.progress.lastOutcome = outcome;
    out.progress.pending = 0;
    out.liveness.candidate = PearlCandidateState::Idle;
    out.liveness.lastOutcome = outcome;
    out.liveness.lastOutcomeEvidence = evidence;
    out.liveness.lastOutcomeSubmitGeneration =
        dispatched.unresolvedSubmitGeneration;
    out.liveness.candidateDeadline = PearlDeadline();
    out.liveness.outcomeFreshness.state = PearlDeadlineState::Disabled;
    ++out.liveness.outcomes;
    switch (evidence) {
        case PearlOutcomeEvidence::LocalPreDeliveryFailure:
            ++out.progress.submissionFailures;
            ++out.liveness.submissionFailures;
            break;
        case PearlOutcomeEvidence::PossiblyDeliveredUnknown:
            ++out.liveness.possiblyDeliveredUnknowns;
            break;
        case PearlOutcomeEvidence::FullySentNoReply:
            ++out.progress.submitted;
            ++out.progress.unknown;
            ++out.liveness.unacknowledgedUnknowns;
            break;
        case PearlOutcomeEvidence::GatewayAcknowledgementOnly:
        case PearlOutcomeEvidence::AcknowledgedOutcomeUnavailable:
            ++out.progress.submitted;
            ++out.progress.acknowledged;
            ++out.progress.unknown;
            ++out.liveness.acknowledgements;
            break;
        case PearlOutcomeEvidence::CorrelatedRemoteOutcome:
            ++out.progress.submitted;
            ++out.progress.acknowledged;
            ++out.liveness.acknowledgements;
            if (outcome == PearlSubmitOutcome::Accepted)
                ++out.progress.accepted;
            else if (outcome == PearlSubmitOutcome::Rejected)
                ++out.progress.rejected;
            else if (outcome == PearlSubmitOutcome::Stale)
                ++out.progress.stale;
            else if (outcome == PearlSubmitOutcome::Unknown)
                ++out.progress.unknown;
            break;
        case PearlOutcomeEvidence::None:
            break;
    }
    out.unresolvedSubmit = false;
    out.sourceDispatchObserved = false;
    out.unresolvedSubmitGeneration = 0;
    out.inFlightOperationToken = 0;
    out.inFlightSessionToken = 0;
    out.inFlightAuthorizationToken = 0;
    out.inFlightJobToken = 0;
    out.inFlightJobIdentity = PearlJobIdentityDigest();
    ++out.sourceEventsDrained;
    ++out.lastSourceEventToken;
    ++out.lastSourceEventMs;
    ++out.publishedAtMs;
    stampTerminalEvent(&out, dispatched, terminal);
    out.lastSourceEvent.delivery = delivery;
    out.lastSourceEvent.acknowledged = acknowledged ? 1 : 0;
    out.lastSourceEvent.reason = reason;
    switch (evidence) {
        case PearlOutcomeEvidence::LocalPreDeliveryFailure:
            out.lastSourceEvent.terminalEvidence =
                PearlSourceTerminalEvidence::LocalPreDeliveryFailure;
            break;
        case PearlOutcomeEvidence::PossiblyDeliveredUnknown:
            out.lastSourceEvent.terminalEvidence =
                PearlSourceTerminalEvidence::PossiblyDeliveredUnknown;
            break;
        case PearlOutcomeEvidence::FullySentNoReply:
            out.lastSourceEvent.terminalEvidence =
                PearlSourceTerminalEvidence::FullySentNoReply;
            break;
        case PearlOutcomeEvidence::GatewayAcknowledgementOnly:
            out.lastSourceEvent.terminalEvidence =
                PearlSourceTerminalEvidence::GatewayAcknowledgementOnly;
            break;
        case PearlOutcomeEvidence::AcknowledgedOutcomeUnavailable:
            out.lastSourceEvent.terminalEvidence =
                PearlSourceTerminalEvidence::AcknowledgedOutcomeUnavailable;
            break;
        case PearlOutcomeEvidence::CorrelatedRemoteOutcome:
            out.lastSourceEvent.terminalEvidence =
                PearlSourceTerminalEvidence::CorrelatedRemoteOutcome;
            break;
        case PearlOutcomeEvidence::None:
            out.lastSourceEvent.terminalEvidence =
                PearlSourceTerminalEvidence::None;
            break;
    }
    return out;
}

PearlObserverSnapshot retireActiveJob(PearlObserverSnapshot out) {
    const uint64_t retiredSession = out.sourceSessionToken;
    const uint64_t retiredAuthorization = out.sourceAuthorizationToken;
    const uint64_t retiredJob = out.activeJobToken;
    const PearlJobIdentityDigest retiredIdentity = out.activeJobIdentity;
    out.activeJobToken = 0;
    out.activeJobIdentity = PearlJobIdentityDigest();
    ++out.sourceEventsDrained;
    ++out.lastSourceEventToken;
    ++out.lastSourceEventMs;
    ++out.publishedAtMs;
    stampSourceEvent(&out, PearlSourceEventKind::JobRetired,
                     retiredSession);
    out.lastSourceEvent.authorizationToken = retiredAuthorization;
    out.lastSourceEvent.jobToken = retiredJob;
    out.lastSourceEvent.jobIdentity = retiredIdentity;
    out.lastSourceEvent.reason = PearlSourceReason::Shutdown;
    return out;
}

PearlObserverSnapshot releaseIssuedJob(PearlObserverSnapshot out) {
    out.lastReleasedIssuedJobIdentity = out.issuedJobIdentity;
    out.issuedSessionToken = 0;
    out.issuedAuthorizationToken = 0;
    out.issuedJobToken = 0;
    out.issuedJobIdentity = PearlJobIdentityDigest();
    ++out.issuedBindingGeneration;
    out.issuedBindingCause = PearlIssuedBindingCause::Released;
    out.progress.phase = PearlProgressPhase::AwaitingJob;
    out.liveness.connection = PearlConnectionState::AwaitingJob;
    out.liveness.jobProgress = PearlJobProgressState::NoJob;
    out.liveness.completedAttemptsForJob = 0;
    out.liveness.attemptDeadline = PearlDeadline();
    ++out.publishedAtMs;
    out.liveness.connectionDeadline.state = PearlDeadlineState::Fresh;
    out.liveness.connectionDeadline.atMs = out.publishedAtMs + 1000;
    return out;
}

PearlObserverSnapshot disconnectSource(PearlObserverSnapshot out,
                                       PearlSourceReason reason) {
    const uint64_t disconnectedSession = out.sourceSessionToken;
    const uint64_t disconnectedAuthorization =
        out.sourceAuthorizationToken;
    out.progress.phase = PearlProgressPhase::Stopped;
    out.liveness.connection = PearlConnectionState::Disconnected;
    out.liveness.jobProgress = PearlJobProgressState::NoJob;
    out.liveness.completedAttemptsForJob = 0;
    out.liveness.connectionDeadline = PearlDeadline();
    out.liveness.attemptDeadline = PearlDeadline();
    out.sourceConnected = false;
    out.sourceReady = false;
    out.sourceFaultObserved = false;
    out.physicalTransportConnected = false;
    out.sourceResourcesReleased = true;
    out.sourceSessionToken = 0;
    out.sourceAuthorizationToken = 0;
    ++out.sourceEventsDrained;
    ++out.lastSourceEventToken;
    ++out.lastSourceEventMs;
    ++out.publishedAtMs;
    stampSourceEvent(&out, PearlSourceEventKind::Disconnected,
                     disconnectedSession);
    out.lastSourceEvent.authorizationToken =
        disconnectedAuthorization;
    out.lastSourceEvent.reason = reason;
    ++out.physicalTeardownGeneration;
    out.physicalTeardownCause =
        PearlPhysicalTeardownCause::SourceDisconnected;
    return out;
}

PearlObserverSnapshot withoutSourceCapability(PearlObserverSnapshot out) {
    out.sourceState = PearlObserverSourceState::Unsupported;
    out.sourceConnected = false;
    out.sourceReady = false;
    out.sourceFaultObserved = false;
    out.physicalTransportConnected = false;
    out.sourceResourcesReleased = false;
    out.sourceIntegrityFlags = 0;
    out.sourceMailboxFaultBits = 0;
    out.sourceEventsDrained = 0;
    out.sourceEventsInvalid = 0;
    out.sourceRefusedEvents = 0;
    out.sourceFaultEvents = 0;
    out.sourceQueueHighWater = 0;
    out.lastSourceEventToken = 0;
    out.lastSourceEventMs = 0;
    out.lastSourceEvent = PearlSourceEvent();
    out.sourceSessionToken = 0;
    out.sourceAuthorizationToken = 0;
    out.activeJobToken = 0;
    out.issuedJobToken = 0;
    out.issuedSessionToken = 0;
    out.issuedAuthorizationToken = 0;
    out.issuedBindingGeneration = 0;
    out.issuedBindingCause = PearlIssuedBindingCause::None;
    out.inFlightOperationToken = 0;
    out.inFlightSessionToken = 0;
    out.inFlightAuthorizationToken = 0;
    out.inFlightJobToken = 0;
    out.activeJobIdentity = PearlJobIdentityDigest();
    out.issuedJobIdentity = PearlJobIdentityDigest();
    out.lastReleasedIssuedJobIdentity = PearlJobIdentityDigest();
    out.inFlightJobIdentity = PearlJobIdentityDigest();
    out.sourceDispatchObserved = false;
    out.physicalTeardownGeneration = 0;
    out.physicalTeardownCause = PearlPhysicalTeardownCause::None;
    return out;
}

bool coherentTag(const PearlObserverSnapshot &sample) {
    const uint64_t tag = sample.progress.sessionGeneration;
    return tag != 0 && sample.invariantsHold() &&
           sample.progress.authorizationGeneration == tag &&
           sample.progress.jobGeneration == tag &&
           sample.progress.prepareGeneration == tag &&
           sample.progress.searchGeneration == tag &&
           sample.progress.searchesStarted == tag &&
           sample.progress.searchesCompleted == tag - 1 &&
           sample.progress.attemptsStarted == tag &&
           sample.progress.attemptsCompleted == tag &&
           sample.progress.publishedAtMs == tag * 10 &&
           sample.liveness.sessionGeneration == tag &&
           sample.liveness.jobGeneration == tag &&
           sample.liveness.completedAttempts == tag &&
           sample.sourceEventsDrained == tag &&
           sample.sourceQueueHighWater == tag &&
           sample.lastSourceEventToken == tag &&
           sample.lastSourceEventMs == tag * 10 &&
           sample.sourceSessionToken == 100000 + tag &&
           sample.sourceAuthorizationToken == 200000 + tag &&
           sample.physicalTransportConnected &&
           sample.activeJobToken == 300000 + tag &&
           sample.issuedJobToken == sample.activeJobToken &&
           pearlObserverDigestEqual(sample.activeJobIdentity, digest(tag)) &&
           pearlObserverDigestEqual(sample.issuedJobIdentity, digest(tag)) &&
           sample.publishedAtMs == tag * 10;
}

bool coherentBenchmarkTag(const PearlObserverSnapshot &sample) {
    const uint64_t tag = sample.progress.sessionGeneration;
    return tag != 0 && sample.invariantsHold() &&
           sample.sourceState == PearlObserverSourceState::Unsupported &&
           sample.progress.authorizationGeneration == tag &&
           sample.progress.jobGeneration == tag &&
           sample.progress.prepareGeneration == tag &&
           sample.progress.searchGeneration == tag &&
           sample.progress.searchesStarted == tag &&
           sample.progress.searchesCompleted == tag - 1 &&
           sample.progress.attemptsStarted == tag &&
           sample.progress.attemptsCompleted == tag &&
           sample.liveness.sessionGeneration == tag &&
           sample.liveness.jobGeneration == tag &&
           sample.liveness.completedAttempts == tag &&
           sample.sourceSessionToken == 0 && sample.activeJobToken == 0 &&
           sample.issuedJobToken == 0 &&
           pearlObserverDigestZero(sample.activeJobIdentity) &&
           pearlObserverDigestZero(sample.issuedJobIdentity) &&
           sample.publishedAtMs == tag * 10;
}

struct MidpointPause {
    std::atomic<bool> entered{false};
    std::atomic<bool> readerFinished{false};
    std::atomic<bool> timedOut{false};
};

void pauseMidpoint(void *context) noexcept {
    auto *pause = static_cast<MidpointPause *>(context);
    pause->entered.store(true, std::memory_order_release);
    for (uint64_t spin = 0; spin < 2000000; ++spin) {
        if (pause->readerFinished.load(std::memory_order_acquire)) return;
        std::this_thread::yield();
    }
    pause->timedOut.store(true, std::memory_order_release);
}

struct SequenceRace {
    PearlObserverSnapshot replacement;
    std::atomic<bool> start{false};
    std::atomic<bool> done{false};
    std::atomic<bool> timedOut{false};
};

void replaceAfterFirstSequence(void *context) {
    auto *race = static_cast<SequenceRace *>(context);
    race->start.store(true, std::memory_order_release);
    for (uint64_t spin = 0; spin < 2000000; ++spin) {
        if (race->done.load(std::memory_order_acquire)) return;
        std::this_thread::yield();
    }
    race->timedOut.store(true, std::memory_order_release);
}

struct ReentryAttempt {
    PearlObserverBus::Writer *writer = nullptr;
    PearlObserverSnapshot nested;
    bool accepted = true;
};

void attemptReentry(void *context) noexcept {
    auto *attempt = static_cast<ReentryAttempt *>(context);
    attempt->accepted = attempt->writer->publish(attempt->nested);
}

struct CrossBusReentryAttempt {
    PearlObserverBus::Writer *first = nullptr;
    PearlObserverBus::Writer *second = nullptr;
    PearlObserverSnapshot firstNested;
    PearlObserverSnapshot secondOuter;
    bool firstNestedAccepted = true;
    bool secondOuterAccepted = false;
};

void attemptFirstBusAgain(void *context) noexcept {
    auto *attempt = static_cast<CrossBusReentryAttempt *>(context);
    attempt->firstNestedAccepted =
        attempt->first->publish(attempt->firstNested);
}

void routeReentryThroughSecondBus(void *context) noexcept {
    auto *attempt = static_cast<CrossBusReentryAttempt *>(context);
    attempt->secondOuterAccepted =
        attempt->second->publishWithMidpointHookForTest(
            attempt->secondOuter, attemptFirstBusAgain, attempt);
}

}  // namespace

int main() {
    OwnedObserverBus emptyBus;
    PearlObserverSnapshot sample;
    check("default unsupported envelope is coherent and claim-free",
          emptyBus.tryRead(&sample) && sample.invariantsHold() &&
              sample.schemaVersion == kPearlObserverSchemaVersion &&
              sample.sourceState == PearlObserverSourceState::Unsupported &&
              sample.sourceSessionToken == 0 &&
              pearlObserverDigestZero(sample.activeJobIdentity));

    auto bareEvent = [](PearlSourceEventKind kind) {
        PearlSourceEvent event;
        event.eventToken = 1;
        event.timestampMs = 1;
        event.sessionToken = 1;
        event.kind = kind;
        return event;
    };
    PearlSourceEvent malformedSession =
        bareEvent(PearlSourceEventKind::SessionStarted);
    malformedSession.authorizationToken = 1;
    check("SessionStarted rejects unused authorization",
          !pearlObserverSourceEventShapeHolds(malformedSession));
    PearlSourceEvent malformedTransport =
        bareEvent(PearlSourceEventKind::TransportConnected);
    malformedTransport.jobToken = 1;
    check("TransportConnected rejects unused job token",
          !pearlObserverSourceEventShapeHolds(malformedTransport));
    PearlSourceEvent malformedAuthorization =
        bareEvent(PearlSourceEventKind::AuthorizationSucceeded);
    malformedAuthorization.authorizationToken = 1;
    malformedAuthorization.operationToken = 1;
    check("AuthorizationSucceeded rejects unused operation",
          !pearlObserverSourceEventShapeHolds(malformedAuthorization));
    PearlSourceEvent malformedNoAuth =
        bareEvent(PearlSourceEventKind::SessionReadyNoAuth);
    malformedNoAuth.authorizationToken = 1;
    check("SessionReadyNoAuth rejects an authorization token",
          !pearlObserverSourceEventShapeHolds(malformedNoAuth));
    PearlSourceEvent malformedRetained =
        bareEvent(PearlSourceEventKind::JobRetainedPreAuth);
    malformedRetained.authorizationToken = 1;
    malformedRetained.jobToken = 1;
    malformedRetained.jobIdentity = digest(1);
    check("JobRetainedPreAuth rejects unused authorization",
          !pearlObserverSourceEventShapeHolds(malformedRetained));
    PearlSourceEvent malformedAvailable =
        bareEvent(PearlSourceEventKind::JobAvailable);
    malformedAvailable.jobToken = 1;
    malformedAvailable.jobIdentity = digest(1);
    malformedAvailable.reason = PearlSourceReason::Protocol;
    check("JobAvailable rejects unused reason",
          !pearlObserverSourceEventShapeHolds(malformedAvailable));
    PearlSourceEvent malformedRetired =
        bareEvent(PearlSourceEventKind::JobRetired);
    malformedRetired.jobToken = 1;
    malformedRetired.jobIdentity = digest(1);
    check("JobRetired requires a reason",
          !pearlObserverSourceEventShapeHolds(malformedRetired));
    PearlSourceEvent malformedDisconnected =
        bareEvent(PearlSourceEventKind::Disconnected);
    malformedDisconnected.reason = PearlSourceReason::Shutdown;
    malformedDisconnected.jobToken = 1;
    malformedDisconnected.jobIdentity = digest(1);
    check("Disconnected rejects unused job identity",
          !pearlObserverSourceEventShapeHolds(malformedDisconnected));
    PearlSourceEvent malformedFault =
        bareEvent(PearlSourceEventKind::SourceFault);
    malformedFault.reason = PearlSourceReason::Protocol;
    malformedFault.operationToken = 1;
    check("SourceFault rejects unused operation",
          !pearlObserverSourceEventShapeHolds(malformedFault));
    PearlSourceEvent malformedNone;
    malformedNone.terminalEvidence =
        PearlSourceTerminalEvidence::CorrelatedRemoteOutcome;
    check("None rejects every terminal evidence fact",
          !pearlObserverSourceEventShapeHolds(malformedNone));
    PearlSourceEvent validDispatch =
        bareEvent(PearlSourceEventKind::SubmitDispatched);
    validDispatch.authorizationToken = 2;
    validDispatch.jobToken = 3;
    validDispatch.operationToken = 4;
    validDispatch.jobIdentity = digest(1);
    validDispatch.delivery = PearlSourceDelivery::NotSent;
    check("canonical SubmitDispatched shape is valid",
          pearlObserverSourceEventShapeHolds(validDispatch));
    PearlSourceEvent malformedDispatch = validDispatch;
    malformedDispatch.delivery = PearlSourceDelivery::Sent;
    check("SubmitDispatched rejects non-NotSent delivery",
          !pearlObserverSourceEventShapeHolds(malformedDispatch));
    malformedDispatch = validDispatch;
    malformedDispatch.terminal = PearlSourceTerminal::Unknown;
    check("SubmitDispatched rejects unused terminal",
          !pearlObserverSourceEventShapeHolds(malformedDispatch));
    malformedDispatch = validDispatch;
    malformedDispatch.reason = PearlSourceReason::Protocol;
    check("SubmitDispatched rejects unused reason",
          !pearlObserverSourceEventShapeHolds(malformedDispatch));
    malformedDispatch = validDispatch;
    malformedDispatch.terminalEvidence =
        PearlSourceTerminalEvidence::FullySentNoReply;
    check("SubmitDispatched rejects unused terminal evidence",
          !pearlObserverSourceEventShapeHolds(malformedDispatch));
    malformedDispatch = validDispatch;
    malformedDispatch.acknowledged = 1;
    check("SubmitDispatched rejects unused acknowledgement",
          !pearlObserverSourceEventShapeHolds(malformedDispatch));
    malformedDispatch = validDispatch;
    malformedDispatch.superseded = 1;
    check("SubmitDispatched rejects unused superseded flag",
          !pearlObserverSourceEventShapeHolds(malformedDispatch));
    malformedDispatch = validDispatch;
    malformedDispatch.sessionToken = 0;
    check("SubmitDispatched requires a session token",
          !pearlObserverSourceEventShapeHolds(malformedDispatch));
    malformedDispatch = validDispatch;
    malformedDispatch.jobToken = 0;
    check("SubmitDispatched requires a job token",
          !pearlObserverSourceEventShapeHolds(malformedDispatch));
    malformedDispatch = validDispatch;
    malformedDispatch.operationToken = 0;
    check("SubmitDispatched requires an operation token",
          !pearlObserverSourceEventShapeHolds(malformedDispatch));
    malformedDispatch = validDispatch;
    malformedDispatch.jobIdentity = PearlJobIdentityDigest();
    check("SubmitDispatched requires a job digest",
          !pearlObserverSourceEventShapeHolds(malformedDispatch));

    PearlObserverSnapshot unsupportedBad;
    unsupportedBad.sourceSessionToken = 1;
    check("unsupported source cannot publish ownership",
          !emptyBus.publish(unsupportedBad));
    check("invalid unsupported candidate latches without ownership",
          emptyBus.tryRead(&sample) &&
              sample.hasObserverIntegrityFlag(
                  PearlObserverIntegrityFlag::InvalidCandidate) &&
              sample.sourceState == PearlObserverSourceState::Quarantined &&
              sample.sourceSessionToken == 0);

    OwnedObserverBus benchmarkBus;
    PearlObserverSnapshot benchmark = withoutSourceCapability(tagged(2));
    check("active benchmark can remain explicitly source-Unsupported",
          benchmarkBus.publish(benchmark) && benchmarkBus.tryRead(&sample) &&
              sample.progress.phase == PearlProgressPhase::Searching &&
              sample.sourceState == PearlObserverSourceState::Unsupported);

    OwnedObserverBus gatewayBus;
    PearlObserverSnapshot gateway = tagged(3);
    addSubmitFunnel(&gateway);
    gateway.progress.phase = PearlProgressPhase::Submitting;
    gateway.liveness.candidate =
        PearlCandidateState::AwaitingAcknowledgement;
    gateway.liveness.candidateDeadline.state = PearlDeadlineState::Disabled;
    gateway.unresolvedSubmit = true;
    gateway.unresolvedSubmitGeneration = 1;
    gateway = withoutSourceCapability(gateway);
    check("capability-null gateway preserves unresolved local submit",
          gatewayBus.publish(gateway) && gatewayBus.tryRead(&sample) &&
              sample.unresolvedSubmit && !sample.sourceDispatchObserved &&
              sample.sourceState == PearlObserverSourceState::Unsupported);

    OwnedObserverBus disconnectBus;
    PearlObserverSnapshot connected = tagged(4);
    check("clean-disconnect fixture publishes",
          disconnectBus.publish(connected));
    const PearlObserverSnapshot retiredConnected =
        retireActiveJob(connected);
    check("active job retires before clean disconnect",
          disconnectBus.publish(retiredConnected));
    const PearlObserverSnapshot releasedConnected =
        releaseIssuedJob(retiredConnected);
    check("issued job releases before clean disconnect",
          disconnectBus.publish(releasedConnected));
    PearlObserverSnapshot disconnected =
        disconnectSource(releasedConnected, PearlSourceReason::Shutdown);
    check("clean disconnect retains trusted capability and bounded history",
          disconnectBus.publish(disconnected) &&
              disconnectBus.tryRead(&sample) &&
              sample.sourceState == PearlObserverSourceState::Trusted &&
              !sample.sourceConnected && sample.sourceSessionToken == 0 &&
              sample.sourceEventsDrained == disconnected.sourceEventsDrained &&
              sample.lastSourceEventToken ==
                  disconnected.lastSourceEventToken &&
              sample.progress.phase == PearlProgressPhase::Stopped);
    OwnedObserverBus duplicateDisconnectBus;
    check("duplicate-disconnect base publishes",
          duplicateDisconnectBus.publish(disconnected));
    PearlObserverSnapshot duplicateDisconnect = disconnected;
    ++duplicateDisconnect.sourceEventsDrained;
    ++duplicateDisconnect.lastSourceEventToken;
    ++duplicateDisconnect.lastSourceEventMs;
    ++duplicateDisconnect.publishedAtMs;
    stampSourceEvent(&duplicateDisconnect,
                     PearlSourceEventKind::Disconnected,
                     connected.sourceSessionToken);
    duplicateDisconnect.lastSourceEvent.reason =
        PearlSourceReason::Shutdown;
    check("Disconnected cannot repeat after logical session retirement",
          duplicateDisconnect.invariantsHold() &&
              !duplicateDisconnectBus.publish(duplicateDisconnect));
    OwnedObserverBus wrongDisconnectBus;
    check("wrong-disconnect base publishes",
          wrongDisconnectBus.publish(releasedConnected));
    PearlObserverSnapshot wrongDisconnect = disconnected;
    stampSourceEvent(&wrongDisconnect,
                     PearlSourceEventKind::JobRetainedPreAuth,
                     connected.sourceSessionToken);
    wrongDisconnect.lastSourceEvent.jobToken = 400001;
    wrongDisconnect.lastSourceEvent.jobIdentity = digest(40);
    check("connection/readiness/physical teardown require Disconnect evidence",
          wrongDisconnect.invariantsHold() &&
              !wrongDisconnectBus.publish(wrongDisconnect));
    OwnedObserverBus wrongSessionDisconnectBus;
    check("wrong-session disconnect base publishes",
          wrongSessionDisconnectBus.publish(releasedConnected));
    PearlObserverSnapshot wrongSessionDisconnect = disconnected;
    stampSourceEvent(&wrongSessionDisconnect,
                     PearlSourceEventKind::Disconnected,
                     connected.sourceSessionToken + 1);
    wrongSessionDisconnect.lastSourceEvent.reason =
        PearlSourceReason::Shutdown;
    check("Disconnect evidence must name the exact prior session",
          wrongSessionDisconnect.invariantsHold() &&
              !wrongSessionDisconnectBus.publish(wrongSessionDisconnect));
    OwnedObserverBus wrongAuthorizationDisconnectBus;
    check("wrong-authorization disconnect base publishes",
          wrongAuthorizationDisconnectBus.publish(releasedConnected));
    PearlObserverSnapshot wrongAuthorizationDisconnect = disconnected;
    ++wrongAuthorizationDisconnect.lastSourceEvent.authorizationToken;
    check("Disconnect evidence must name the exact prior authorization",
          wrongAuthorizationDisconnect.invariantsHold() &&
              !wrongAuthorizationDisconnectBus.publish(
                  wrongAuthorizationDisconnect));
    OwnedObserverBus pseudoLiveDisconnectBus;
    check("pseudo-live disconnect base publishes",
          pseudoLiveDisconnectBus.publish(releasedConnected));
    PearlObserverSnapshot pseudoLiveDisconnect = disconnected;
    pseudoLiveDisconnect.progress.phase = PearlProgressPhase::Connecting;
    pseudoLiveDisconnect.liveness.connection =
        PearlConnectionState::AwaitingAuthorization;
    pseudoLiveDisconnect.sourceSessionToken =
        releasedConnected.sourceSessionToken;
    pseudoLiveDisconnect.sourceResourcesReleased = false;
    pseudoLiveDisconnect.liveness.connectionDeadline =
        releasedConnected.liveness.connectionDeadline;
    check("Disconnected cannot leave a pseudo-live logical session",
          pseudoLiveDisconnect.invariantsHold() &&
              !pseudoLiveDisconnectBus.publish(pseudoLiveDisconnect));

    PearlObserverSnapshot awaitingAuthorization = tagged(2);
    awaitingAuthorization.progress.phase = PearlProgressPhase::Connecting;
    awaitingAuthorization.liveness.connection =
        PearlConnectionState::AwaitingAuthorization;
    awaitingAuthorization.liveness.jobProgress =
        PearlJobProgressState::NoJob;
    awaitingAuthorization.liveness.completedAttemptsForJob = 0;
    awaitingAuthorization.liveness.connectionDeadline.state =
        PearlDeadlineState::Disabled;
    awaitingAuthorization.liveness.attemptDeadline = PearlDeadline();
    awaitingAuthorization.sourceReady = false;
    awaitingAuthorization.sourceAuthorizationToken = 0;
    awaitingAuthorization.activeJobToken = 0;
    awaitingAuthorization.issuedSessionToken = 0;
    awaitingAuthorization.issuedAuthorizationToken = 0;
    awaitingAuthorization.issuedJobToken = 0;
    awaitingAuthorization.issuedBindingGeneration = 0;
    awaitingAuthorization.issuedBindingCause = PearlIssuedBindingCause::None;
    awaitingAuthorization.activeJobIdentity = PearlJobIdentityDigest();
    awaitingAuthorization.issuedJobIdentity = PearlJobIdentityDigest();
    stampSourceEvent(&awaitingAuthorization,
                     PearlSourceEventKind::TransportConnected,
                     awaitingAuthorization.sourceSessionToken);
    PearlObserverSnapshot authorizationReady = awaitingAuthorization;
    authorizationReady.progress.phase = PearlProgressPhase::AwaitingJob;
    ++authorizationReady.progress.authorizationGeneration;
    authorizationReady.liveness.connection = PearlConnectionState::AwaitingJob;
    authorizationReady.sourceReady = true;
    authorizationReady.sourceAuthorizationToken = 9002;
    ++authorizationReady.sourceEventsDrained;
    ++authorizationReady.lastSourceEventToken;
    ++authorizationReady.lastSourceEventMs;
    ++authorizationReady.publishedAtMs;
    stampSourceEvent(&authorizationReady,
                     PearlSourceEventKind::AuthorizationSucceeded,
                     authorizationReady.sourceSessionToken);
    authorizationReady.lastSourceEvent.authorizationToken =
        authorizationReady.sourceAuthorizationToken;
    OwnedObserverBus exactReadyBus;
    check("authorization-ready base publishes",
          awaitingAuthorization.invariantsHold() &&
              exactReadyBus.publish(awaitingAuthorization));
    check("readiness accepts the exact session/authorization event",
          authorizationReady.invariantsHold() &&
              exactReadyBus.publish(authorizationReady));
    OwnedObserverBus coalescedAuthorizationBus;
    check("coalesced-authorization base publishes",
          coalescedAuthorizationBus.publish(awaitingAuthorization));
    PearlObserverSnapshot coalescedAuthorization = authorizationReady;
    ++coalescedAuthorization.sourceEventsDrained;
    ++coalescedAuthorization.lastSourceEventToken;
    coalescedAuthorization.lastSourceEvent.eventToken =
        coalescedAuthorization.lastSourceEventToken;
    check("non-restart source events publish one consumed event at a time",
          coalescedAuthorization.invariantsHold() &&
              !coalescedAuthorizationBus.publish(
                  coalescedAuthorization));
    OwnedObserverBus missingReadyGenerationBus;
    check("missing-ready-generation base publishes",
          missingReadyGenerationBus.publish(awaitingAuthorization));
    PearlObserverSnapshot missingReadyGeneration = authorizationReady;
    missingReadyGeneration.progress.authorizationGeneration =
        awaitingAuthorization.progress.authorizationGeneration;
    check("readiness requires its exact authorization generation advance",
          missingReadyGeneration.invariantsHold() &&
              !missingReadyGenerationBus.publish(missingReadyGeneration));
    OwnedObserverBus noEffectAuthorizationBus;
    check("no-effect authorization base publishes",
          noEffectAuthorizationBus.publish(awaitingAuthorization));
    PearlObserverSnapshot noEffectAuthorization = awaitingAuthorization;
    ++noEffectAuthorization.sourceEventsDrained;
    ++noEffectAuthorization.lastSourceEventToken;
    ++noEffectAuthorization.lastSourceEventMs;
    ++noEffectAuthorization.publishedAtMs;
    stampSourceEvent(&noEffectAuthorization,
                     PearlSourceEventKind::AuthorizationSucceeded,
                     noEffectAuthorization.sourceSessionToken);
    noEffectAuthorization.lastSourceEvent.authorizationToken = 9003;
    check("AuthorizationSucceeded cannot be a no-effect cursor event",
          noEffectAuthorization.invariantsHold() &&
              !noEffectAuthorizationBus.publish(noEffectAuthorization));
    OwnedObserverBus wrongReadySessionBus;
    check("wrong-ready-session base publishes",
          wrongReadySessionBus.publish(awaitingAuthorization));
    PearlObserverSnapshot wrongReadySession = authorizationReady;
    ++wrongReadySession.lastSourceEvent.sessionToken;
    check("readiness event must name the exact active session",
          wrongReadySession.invariantsHold() &&
              !wrongReadySessionBus.publish(wrongReadySession));
    OwnedObserverBus wrongReadyAuthorizationBus;
    check("wrong-ready-authorization base publishes",
          wrongReadyAuthorizationBus.publish(awaitingAuthorization));
    PearlObserverSnapshot wrongReadyAuthorization = authorizationReady;
    ++wrongReadyAuthorization.lastSourceEvent.authorizationToken;
    check("readiness event must name the exact authorization",
          wrongReadyAuthorization.invariantsHold() &&
              !wrongReadyAuthorizationBus.publish(
                  wrongReadyAuthorization));
    OwnedObserverBus repeatedAuthorizationBus;
    PearlObserverSnapshot readyAuthorizationBase = authorizationReady;
    check("repeated-authorization base publishes",
          repeatedAuthorizationBus.publish(readyAuthorizationBase));
    PearlObserverSnapshot repeatedAuthorization = readyAuthorizationBase;
    ++repeatedAuthorization.progress.authorizationGeneration;
    ++repeatedAuthorization.sourceAuthorizationToken;
    ++repeatedAuthorization.sourceEventsDrained;
    ++repeatedAuthorization.lastSourceEventToken;
    ++repeatedAuthorization.lastSourceEventMs;
    ++repeatedAuthorization.publishedAtMs;
    stampSourceEvent(&repeatedAuthorization,
                     PearlSourceEventKind::AuthorizationSucceeded,
                     repeatedAuthorization.sourceSessionToken);
    repeatedAuthorization.lastSourceEvent.authorizationToken =
        repeatedAuthorization.sourceAuthorizationToken;
    check("already-ready session rejects replacement authorization",
          repeatedAuthorization.invariantsHold() &&
              !repeatedAuthorizationBus.publish(repeatedAuthorization));
    OwnedObserverBus duplicateAuthorizationBus;
    check("duplicate-authorization base publishes",
          duplicateAuthorizationBus.publish(readyAuthorizationBase));
    PearlObserverSnapshot duplicateAuthorization = readyAuthorizationBase;
    ++duplicateAuthorization.sourceEventsDrained;
    ++duplicateAuthorization.lastSourceEventToken;
    ++duplicateAuthorization.lastSourceEventMs;
    ++duplicateAuthorization.publishedAtMs;
    stampSourceEvent(&duplicateAuthorization,
                     PearlSourceEventKind::AuthorizationSucceeded,
                     duplicateAuthorization.sourceSessionToken);
    duplicateAuthorization.lastSourceEvent.authorizationToken =
        duplicateAuthorization.sourceAuthorizationToken;
    check("already-ready session rejects duplicate AuthorizationSucceeded",
          duplicateAuthorization.invariantsHold() &&
              !duplicateAuthorizationBus.publish(duplicateAuthorization));

    OwnedObserverBus noOpSessionBus;
    PearlObserverSnapshot noOpSessionBase = tagged(23);
    check("no-op SessionStarted base publishes",
          noOpSessionBus.publish(noOpSessionBase));
    PearlObserverSnapshot noOpSession = noOpSessionBase;
    ++noOpSession.sourceEventsDrained;
    ++noOpSession.lastSourceEventToken;
    ++noOpSession.lastSourceEventMs;
    ++noOpSession.publishedAtMs;
    stampSourceEvent(&noOpSession, PearlSourceEventKind::SessionStarted,
                     noOpSession.sourceSessionToken);
    check("SessionStarted cannot retain the same live session",
          noOpSession.invariantsHold() &&
              !noOpSessionBus.publish(noOpSession));

    OwnedObserverBus noOpTransportBus;
    check("no-op TransportConnected base publishes",
          noOpTransportBus.publish(noOpSessionBase));
    PearlObserverSnapshot noOpTransport = noOpSessionBase;
    ++noOpTransport.sourceEventsDrained;
    ++noOpTransport.lastSourceEventToken;
    ++noOpTransport.lastSourceEventMs;
    ++noOpTransport.publishedAtMs;
    stampSourceEvent(&noOpTransport,
                     PearlSourceEventKind::TransportConnected,
                     noOpTransport.sourceSessionToken);
    check("TransportConnected cannot repeat an established transport",
          noOpTransport.invariantsHold() &&
              !noOpTransportBus.publish(noOpTransport));

    OwnedObserverBus noEffectReadyNoAuthBus;
    check("no-effect ready-no-auth base publishes",
          noEffectReadyNoAuthBus.publish(awaitingAuthorization));
    PearlObserverSnapshot noEffectReadyNoAuth = awaitingAuthorization;
    ++noEffectReadyNoAuth.sourceEventsDrained;
    ++noEffectReadyNoAuth.lastSourceEventToken;
    ++noEffectReadyNoAuth.lastSourceEventMs;
    ++noEffectReadyNoAuth.publishedAtMs;
    stampSourceEvent(&noEffectReadyNoAuth,
                     PearlSourceEventKind::SessionReadyNoAuth,
                     noEffectReadyNoAuth.sourceSessionToken);
    check("SessionReadyNoAuth cannot be a no-effect cursor event",
          noEffectReadyNoAuth.invariantsHold() &&
              !noEffectReadyNoAuthBus.publish(noEffectReadyNoAuth));

    OwnedObserverBus retainedPreAuthBus;
    check("retained-pre-auth base publishes",
          retainedPreAuthBus.publish(awaitingAuthorization));
    PearlObserverSnapshot retainedPreAuth = awaitingAuthorization;
    ++retainedPreAuth.sourceEventsDrained;
    ++retainedPreAuth.lastSourceEventToken;
    ++retainedPreAuth.lastSourceEventMs;
    ++retainedPreAuth.publishedAtMs;
    stampSourceEvent(&retainedPreAuth,
                     PearlSourceEventKind::JobRetainedPreAuth,
                     retainedPreAuth.sourceSessionToken);
    retainedPreAuth.lastSourceEvent.jobToken = 93001;
    retainedPreAuth.lastSourceEvent.jobIdentity = digest(93);
    check("JobRetainedPreAuth is visible only in exact pre-auth context",
          retainedPreAuth.invariantsHold() &&
              retainedPreAuthBus.publish(retainedPreAuth));
    PearlObserverSnapshot retiredPreAuth = retainedPreAuth;
    ++retiredPreAuth.sourceEventsDrained;
    ++retiredPreAuth.lastSourceEventToken;
    ++retiredPreAuth.lastSourceEventMs;
    ++retiredPreAuth.publishedAtMs;
    stampSourceEvent(&retiredPreAuth, PearlSourceEventKind::JobRetired,
                     retiredPreAuth.sourceSessionToken);
    retiredPreAuth.lastSourceEvent.jobToken =
        retainedPreAuth.lastSourceEvent.jobToken;
    retiredPreAuth.lastSourceEvent.jobIdentity =
        retainedPreAuth.lastSourceEvent.jobIdentity;
    retiredPreAuth.lastSourceEvent.reason = PearlSourceReason::Stale;
    check("retained-pre-auth retirement remains explicitly event-only",
          retiredPreAuth.invariantsHold() &&
              retainedPreAuthBus.publish(retiredPreAuth));

    OwnedObserverBus retainedInReadyBus;
    check("ready retained-event base publishes",
          retainedInReadyBus.publish(noOpSessionBase));
    PearlObserverSnapshot retainedInReady = noOpSessionBase;
    ++retainedInReady.sourceEventsDrained;
    ++retainedInReady.lastSourceEventToken;
    ++retainedInReady.lastSourceEventMs;
    ++retainedInReady.publishedAtMs;
    stampSourceEvent(&retainedInReady,
                     PearlSourceEventKind::JobRetainedPreAuth,
                     retainedInReady.sourceSessionToken);
    retainedInReady.lastSourceEvent.jobToken = 93002;
    retainedInReady.lastSourceEvent.jobIdentity = digest(94);
    check("JobRetainedPreAuth rejects ready-session no-op context",
          retainedInReady.invariantsHold() &&
              !retainedInReadyBus.publish(retainedInReady));

    OwnedObserverBus duplicateAvailableBus;
    check("duplicate JobAvailable base publishes",
          duplicateAvailableBus.publish(noOpSessionBase));
    PearlObserverSnapshot duplicateAvailable = noOpSessionBase;
    ++duplicateAvailable.sourceEventsDrained;
    ++duplicateAvailable.lastSourceEventToken;
    ++duplicateAvailable.lastSourceEventMs;
    ++duplicateAvailable.publishedAtMs;
    stampSourceEvent(&duplicateAvailable,
                     PearlSourceEventKind::JobAvailable,
                     duplicateAvailable.sourceSessionToken);
    duplicateAvailable.lastSourceEvent.authorizationToken =
        duplicateAvailable.sourceAuthorizationToken;
    duplicateAvailable.lastSourceEvent.jobToken =
        duplicateAvailable.activeJobToken;
    duplicateAvailable.lastSourceEvent.jobIdentity =
        duplicateAvailable.activeJobIdentity;
    check("JobAvailable cannot repeat existing active ownership",
          duplicateAvailable.invariantsHold() &&
              !duplicateAvailableBus.publish(duplicateAvailable));

    OwnedObserverBus orphanRetirementBus;
    check("orphan JobRetired base publishes",
          orphanRetirementBus.publish(authorizationReady));
    PearlObserverSnapshot orphanRetirement = authorizationReady;
    ++orphanRetirement.sourceEventsDrained;
    ++orphanRetirement.lastSourceEventToken;
    ++orphanRetirement.lastSourceEventMs;
    ++orphanRetirement.publishedAtMs;
    stampSourceEvent(&orphanRetirement,
                     PearlSourceEventKind::JobRetired,
                     orphanRetirement.sourceSessionToken);
    orphanRetirement.lastSourceEvent.authorizationToken =
        orphanRetirement.sourceAuthorizationToken;
    orphanRetirement.lastSourceEvent.jobToken = 93003;
    orphanRetirement.lastSourceEvent.jobIdentity = digest(95);
    orphanRetirement.lastSourceEvent.reason = PearlSourceReason::Stale;
    check("JobRetired cannot retire absent ready-session ownership",
          orphanRetirement.invariantsHold() &&
              !orphanRetirementBus.publish(orphanRetirement));
    PearlObserverSnapshot bootstrapFaultContradiction = tagged(3);
    stampSourceFaultEvent(&bootstrapFaultContradiction,
                          PearlSourceReason::Protocol);
    bootstrapFaultContradiction.sourceFaultObserved = false;
    OwnedObserverBus bootstrapFaultBus;
    check("initial bootstrap cannot contradict current SourceFault state",
          bootstrapFaultContradiction.invariantsHold() &&
              !bootstrapFaultBus.publish(bootstrapFaultContradiction));
    PearlObserverSnapshot bootstrapJobContradiction = tagged(3);
    ++bootstrapJobContradiction.lastSourceEvent.jobToken;
    OwnedObserverBus bootstrapJobBus;
    check("initial bootstrap JobAvailable must match the exact active tuple",
          bootstrapJobContradiction.invariantsHold() &&
              !bootstrapJobBus.publish(bootstrapJobContradiction));
    PearlObserverSnapshot bootstrapAuthorizationContradiction = tagged(3);
    stampSourceEvent(&bootstrapAuthorizationContradiction,
                     PearlSourceEventKind::AuthorizationSucceeded,
                     bootstrapAuthorizationContradiction.sourceSessionToken);
    bootstrapAuthorizationContradiction.lastSourceEvent.authorizationToken =
        bootstrapAuthorizationContradiction.sourceAuthorizationToken;
    OwnedObserverBus bootstrapAuthorizationBus;
    check("initial authorization event cannot invent later job ownership",
          bootstrapAuthorizationContradiction.invariantsHold() &&
              !bootstrapAuthorizationBus.publish(
                  bootstrapAuthorizationContradiction));
    PearlObserverSnapshot bootstrapSessionFault = awaitingAuthorization;
    bootstrapSessionFault.sourceConnected = false;
    bootstrapSessionFault.physicalTransportConnected = false;
    bootstrapSessionFault.sourceFaultObserved = true;
    bootstrapSessionFault.sourceFaultEvents = 1;
    stampSourceEvent(&bootstrapSessionFault,
                     PearlSourceEventKind::SessionStarted,
                     bootstrapSessionFault.sourceSessionToken);
    OwnedObserverBus bootstrapSessionFaultBus;
    check("initial SessionStarted cannot retain an earlier fault latch",
          bootstrapSessionFault.invariantsHold() &&
              !bootstrapSessionFaultBus.publish(bootstrapSessionFault));
    PearlObserverSnapshot emptyTrustedBootstrap;
    emptyTrustedBootstrap.sourceState =
        PearlObserverSourceState::Trusted;
    emptyTrustedBootstrap.sourceResourcesReleased = true;
    OwnedObserverBus emptyTrustedBootstrapBus;
    check("eventless trusted bootstrap is exactly empty",
          emptyTrustedBootstrap.invariantsHold() &&
              emptyTrustedBootstrapBus.publish(emptyTrustedBootstrap));
    PearlObserverSnapshot historicalNoEventBootstrap =
        emptyTrustedBootstrap;
    historicalNoEventBootstrap.progress.sessionGeneration = 1;
    historicalNoEventBootstrap.liveness.sessionGeneration = 1;
    OwnedObserverBus historicalNoEventBootstrapBus;
    check("eventless bootstrap cannot invent historical sessions",
          historicalNoEventBootstrap.invariantsHold() &&
              !historicalNoEventBootstrapBus.publish(
                  historicalNoEventBootstrap));
    PearlObserverSnapshot unclassifiedIdleBootstrap = tagged(31);
    unclassifiedIdleBootstrap.progress.submitGeneration = 1;
    unclassifiedIdleBootstrap.progress.submitAttempts = 1;
    unclassifiedIdleBootstrap.liveness.submitGeneration = 1;
    unclassifiedIdleBootstrap.liveness.submitAttempts = 1;
    OwnedObserverBus unclassifiedIdleBootstrapBus;
    check("trusted bootstrap rejects an unclassified idle submit attempt",
          !unclassifiedIdleBootstrap.invariantsHold() &&
              !unclassifiedIdleBootstrapBus.publish(
                  unclassifiedIdleBootstrap));
    PearlObserverSnapshot ackPendingIdleBootstrap = accepted(32);
    ++ackPendingIdleBootstrap.progress.submitGeneration;
    ++ackPendingIdleBootstrap.progress.submitAttempts;
    ++ackPendingIdleBootstrap.progress.submitted;
    ++ackPendingIdleBootstrap.progress.acknowledged;
    ackPendingIdleBootstrap.progress.pending = 1;
    ++ackPendingIdleBootstrap.liveness.submitGeneration;
    ++ackPendingIdleBootstrap.liveness.submitAttempts;
    ++ackPendingIdleBootstrap.liveness.acknowledgements;
    OwnedObserverBus ackPendingIdleBootstrapBus;
    check("trusted bootstrap rejects acknowledgement pending while idle",
          !ackPendingIdleBootstrap.invariantsHold() &&
              !ackPendingIdleBootstrapBus.publish(
                  ackPendingIdleBootstrap));
    PearlObserverSnapshot ackPendingBootstrap = openDispatchedSubmit(33);
    ackPendingBootstrap.progress.phase = PearlProgressPhase::AwaitingOutcome;
    ackPendingBootstrap.progress.submitted = 1;
    ackPendingBootstrap.progress.acknowledged = 1;
    ackPendingBootstrap.progress.pending = 1;
    ackPendingBootstrap.liveness.candidate =
        PearlCandidateState::AwaitingOutcome;
    ackPendingBootstrap.liveness.acknowledgements = 1;
    OwnedObserverBus ackPendingBootstrapBus;
    check("trusted bootstrap accepts the exact one-slot acknowledged state",
          ackPendingBootstrap.invariantsHold() &&
              ackPendingBootstrapBus.publish(ackPendingBootstrap));
    OwnedObserverBus consumedBootstrapBus;
    const PearlObserverSnapshot capabilityOnly =
        withoutSourceCapability(tagged(2));
    check("capability-only first publication consumes bootstrap",
          consumedBootstrapBus.publish(capabilityOnly));
    PearlObserverSnapshot lateTrustedShortcut = tagged(3);
    lateTrustedShortcut.publishedAtMs = capabilityOnly.publishedAtMs + 10;
    lateTrustedShortcut.progress.publishedAtMs =
        lateTrustedShortcut.publishedAtMs;
    lateTrustedShortcut.lastSourceEventMs = lateTrustedShortcut.publishedAtMs;
    stampSourceEvent(&lateTrustedShortcut,
                     PearlSourceEventKind::JobAvailable,
                     lateTrustedShortcut.sourceSessionToken);
    lateTrustedShortcut.lastSourceEvent.authorizationToken =
        lateTrustedShortcut.sourceAuthorizationToken;
    lateTrustedShortcut.lastSourceEvent.jobToken =
        lateTrustedShortcut.activeJobToken;
    lateTrustedShortcut.lastSourceEvent.jobIdentity =
        lateTrustedShortcut.activeJobIdentity;
    check("bootstrap bypass is unavailable after a real publication",
          lateTrustedShortcut.invariantsHold() &&
              !consumedBootstrapBus.publish(lateTrustedShortcut));
    OwnedObserverBus identicalDefaultConsumesBootstrapBus;
    check("an identical default publication still consumes bootstrap",
          identicalDefaultConsumesBootstrapBus.publish(
              PearlObserverSnapshot()));
    PearlObserverSnapshot trustedAfterDefault = tagged(1);
    check("bootstrap is tied to bus publication history, not payload equality",
          trustedAfterDefault.invariantsHold() &&
              !identicalDefaultConsumesBootstrapBus.publish(
                  trustedAfterDefault));
    PearlObserverSnapshot erasedHistory;
    erasedHistory.publishedAtMs = disconnected.publishedAtMs + 1;
    check("Unsupported is initial-only and cannot erase trusted history",
          !disconnectBus.publish(erasedHistory) &&
              disconnectBus.tryRead(&sample) &&
              sample.sourceState == PearlObserverSourceState::Quarantined &&
              sample.sourceEventsDrained == disconnected.sourceEventsDrained);

    OwnedObserverBus staleBus;
    check("monotonic fixture publishes", staleBus.publish(tagged(10)));
    PearlObserverSnapshot staleCounters = tagged(10);
    --staleCounters.progress.attemptsStarted;
    --staleCounters.progress.attemptsCompleted;
    ++staleCounters.sourceEventsDrained;
    ++staleCounters.lastSourceEventToken;
    ++staleCounters.lastSourceEventMs;
    ++staleCounters.publishedAtMs;
    stampSourceEvent(&staleCounters, PearlSourceEventKind::JobAvailable,
                     staleCounters.sourceSessionToken);
    staleCounters.lastSourceEvent.authorizationToken =
        staleCounters.sourceAuthorizationToken;
    staleCounters.lastSourceEvent.jobToken = staleCounters.activeJobToken;
    staleCounters.lastSourceEvent.jobIdentity =
        staleCounters.activeJobIdentity;
    check("stale but standalone-valid cumulative counters are rejected",
          staleCounters.invariantsHold() &&
              !staleBus.publish(staleCounters) &&
              staleBus.tryRead(&sample) &&
              sample.progress.attemptsCompleted == 10 &&
              sample.sourceState == PearlObserverSourceState::Quarantined);
    OwnedObserverBus generationBus;
    check("generation rollback fixture publishes",
          generationBus.publish(tagged(10)));
    PearlObserverSnapshot staleGeneration = tagged(10);
    --staleGeneration.progress.searchGeneration;
    ++staleGeneration.sourceEventsDrained;
    ++staleGeneration.lastSourceEventToken;
    ++staleGeneration.lastSourceEventMs;
    ++staleGeneration.publishedAtMs;
    stampSourceEvent(&staleGeneration, PearlSourceEventKind::JobAvailable,
                     staleGeneration.sourceSessionToken);
    staleGeneration.lastSourceEvent.authorizationToken =
        staleGeneration.sourceAuthorizationToken;
    staleGeneration.lastSourceEvent.jobToken =
        staleGeneration.activeJobToken;
    staleGeneration.lastSourceEvent.jobIdentity =
        staleGeneration.activeJobIdentity;
    check("stale but standalone-valid generation is rejected",
          staleGeneration.invariantsHold() &&
              !generationBus.publish(staleGeneration));
    OwnedObserverBus cursorBus;
    check("source cursor rollback fixture publishes",
          cursorBus.publish(tagged(10)));
    PearlObserverSnapshot staleCursor = tagged(10);
    --staleCursor.lastSourceEventToken;
    ++staleCursor.lastSourceEventMs;
    ++staleCursor.publishedAtMs;
    stampSourceEvent(&staleCursor, PearlSourceEventKind::JobAvailable,
                     staleCursor.sourceSessionToken);
    staleCursor.lastSourceEvent.authorizationToken =
        staleCursor.sourceAuthorizationToken;
    staleCursor.lastSourceEvent.jobToken = staleCursor.activeJobToken;
    staleCursor.lastSourceEvent.jobIdentity = staleCursor.activeJobIdentity;
    check("stale but standalone-valid source cursor is rejected",
          staleCursor.invariantsHold() && !cursorBus.publish(staleCursor));
    OwnedObserverBus cursorDeltaBus;
    PearlObserverSnapshot cursorDeltaBase = tagged(15);
    check("cursor-delta base publishes",
          cursorDeltaBus.publish(cursorDeltaBase));
    PearlObserverSnapshot skippedCursorDelta = cursorDeltaBase;
    skippedCursorDelta.sourceEventsDrained += 2;
    ++skippedCursorDelta.lastSourceEventToken;
    ++skippedCursorDelta.lastSourceEventMs;
    ++skippedCursorDelta.publishedAtMs;
    stampSourceEvent(&skippedCursorDelta,
                     PearlSourceEventKind::JobAvailable,
                     skippedCursorDelta.sourceSessionToken);
    skippedCursorDelta.lastSourceEvent.authorizationToken =
        skippedCursorDelta.sourceAuthorizationToken;
    skippedCursorDelta.lastSourceEvent.jobToken =
        skippedCursorDelta.activeJobToken;
    skippedCursorDelta.lastSourceEvent.jobIdentity =
        skippedCursorDelta.activeJobIdentity;
    check("drained and event-token deltas must conserve exactly",
          skippedCursorDelta.invariantsHold() &&
              !cursorDeltaBus.publish(skippedCursorDelta));
    OwnedObserverBus drainedOnlyDeltaBus;
    check("drained-only delta base publishes",
          drainedOnlyDeltaBus.publish(cursorDeltaBase));
    PearlObserverSnapshot drainedOnlyDelta = cursorDeltaBase;
    ++drainedOnlyDelta.sourceEventsDrained;
    ++drainedOnlyDelta.publishedAtMs;
    check("drained advance without event-token advance is rejected",
          drainedOnlyDelta.invariantsHold() &&
              !drainedOnlyDeltaBus.publish(drainedOnlyDelta));
    OwnedObserverBus tokenOnlyDeltaBus;
    check("token-only delta base publishes",
          tokenOnlyDeltaBus.publish(cursorDeltaBase));
    PearlObserverSnapshot tokenOnlyDelta = cursorDeltaBase;
    ++tokenOnlyDelta.lastSourceEventToken;
    ++tokenOnlyDelta.lastSourceEventMs;
    ++tokenOnlyDelta.publishedAtMs;
    stampSourceEvent(&tokenOnlyDelta, PearlSourceEventKind::JobAvailable,
                     tokenOnlyDelta.sourceSessionToken);
    tokenOnlyDelta.lastSourceEvent.authorizationToken =
        tokenOnlyDelta.sourceAuthorizationToken;
    tokenOnlyDelta.lastSourceEvent.jobToken = tokenOnlyDelta.activeJobToken;
    tokenOnlyDelta.lastSourceEvent.jobIdentity =
        tokenOnlyDelta.activeJobIdentity;
    check("event-token advance without drained advance is rejected",
          tokenOnlyDelta.invariantsHold() &&
              !tokenOnlyDeltaBus.publish(tokenOnlyDelta));

    PearlObserverSnapshot freshAtPublication = tagged(11);
    freshAtPublication.liveness.attemptDeadline.state =
        PearlDeadlineState::Fresh;
    freshAtPublication.liveness.attemptDeadline.atMs =
        freshAtPublication.publishedAtMs;
    check("Fresh deadline must be strictly after publication",
          !freshAtPublication.invariantsHold());
    PearlObserverSnapshot expiredAfterPublication = tagged(11);
    expiredAfterPublication.liveness.attemptDeadline.state =
        PearlDeadlineState::Expired;
    expiredAfterPublication.liveness.attemptDeadline.atMs =
        expiredAfterPublication.publishedAtMs + 1;
    check("Expired deadline cannot be after publication",
          !expiredAfterPublication.invariantsHold());

    PearlObserverSnapshot expiredAttempt = tagged(12);
    expiredAttempt.liveness.attemptDeadline.state =
        PearlDeadlineState::Expired;
    expiredAttempt.liveness.attemptDeadline.atMs =
        expiredAttempt.publishedAtMs;
    OwnedObserverBus staleDeadlineBus;
    check("tracker-shaped expired attempt deadline publishes",
          expiredAttempt.invariantsHold() &&
              staleDeadlineBus.publish(expiredAttempt));
    PearlObserverSnapshot fabricatedRefresh = expiredAttempt;
    ++fabricatedRefresh.publishedAtMs;
    fabricatedRefresh.liveness.attemptDeadline.state =
        PearlDeadlineState::Fresh;
    fabricatedRefresh.liveness.attemptDeadline.atMs =
        fabricatedRefresh.publishedAtMs + 10;
    check("Expired-to-Fresh without exact progress is rejected",
          fabricatedRefresh.invariantsHold() &&
              !staleDeadlineBus.publish(fabricatedRefresh));

    OwnedObserverBus progressedDeadlineBus;
    check("progress deadline base publishes",
          progressedDeadlineBus.publish(expiredAttempt));
    PearlObserverSnapshot progressedRefresh = expiredAttempt;
    ++progressedRefresh.progress.attemptsStarted;
    ++progressedRefresh.progress.attemptsCompleted;
    ++progressedRefresh.liveness.completedAttempts;
    ++progressedRefresh.liveness.completedAttemptsForJob;
    ++progressedRefresh.publishedAtMs;
    progressedRefresh.liveness.attemptDeadline.state =
        PearlDeadlineState::Fresh;
    progressedRefresh.liveness.attemptDeadline.atMs =
        progressedRefresh.publishedAtMs + 10;
    check("Expired-to-Fresh follows exact attempt progress",
          progressedRefresh.invariantsHold() &&
              progressedDeadlineBus.publish(progressedRefresh));

    PearlObserverSnapshot freshAttempt = tagged(14);
    freshAttempt.liveness.attemptDeadline.state =
        PearlDeadlineState::Fresh;
    freshAttempt.liveness.attemptDeadline.atMs =
        freshAttempt.publishedAtMs + 20;
    OwnedObserverBus slidingDeadlineBus;
    check("fresh deadline base publishes",
          slidingDeadlineBus.publish(freshAttempt));
    PearlObserverSnapshot slidingFresh = freshAttempt;
    ++slidingFresh.publishedAtMs;
    ++slidingFresh.liveness.attemptDeadline.atMs;
    check("Fresh-to-Fresh sliding without progress is rejected",
          slidingFresh.invariantsHold() &&
              !slidingDeadlineBus.publish(slidingFresh));
    OwnedObserverBus freshDisabledBus;
    check("Fresh-to-Disabled base publishes",
          freshDisabledBus.publish(freshAttempt));
    PearlObserverSnapshot freshDisabled = freshAttempt;
    ++freshDisabled.publishedAtMs;
    freshDisabled.liveness.attemptDeadline.state =
        PearlDeadlineState::Disabled;
    freshDisabled.liveness.attemptDeadline.atMs = 0;
    check("Fresh-to-Disabled cannot erase an applicable timer",
          freshDisabled.invariantsHold() &&
              !freshDisabledBus.publish(freshDisabled));
    OwnedObserverBus disabledFreshBus;
    PearlObserverSnapshot disabledAttempt = tagged(14);
    check("Disabled-to-Fresh base publishes",
          disabledFreshBus.publish(disabledAttempt));
    PearlObserverSnapshot enabledFresh = disabledAttempt;
    ++enabledFresh.publishedAtMs;
    enabledFresh.liveness.attemptDeadline.state =
        PearlDeadlineState::Fresh;
    enabledFresh.liveness.attemptDeadline.atMs =
        enabledFresh.publishedAtMs + 20;
    check("Disabled-to-Fresh cannot silently enable a timer",
          enabledFresh.invariantsHold() &&
              !disabledFreshBus.publish(enabledFresh));
    OwnedObserverBus lifecycleDeadlineBus;
    check("lifecycle-regression deadline base publishes",
          lifecycleDeadlineBus.publish(freshAttempt));
    PearlObserverSnapshot regressedFresh = freshAttempt;
    regressedFresh.liveness.jobProgress =
        PearlJobProgressState::AwaitingFirstAttempt;
    ++regressedFresh.publishedAtMs;
    ++regressedFresh.liveness.attemptDeadline.atMs;
    check("lifecycle regression cannot refresh a deadline",
          regressedFresh.invariantsHold() &&
              !lifecycleDeadlineBus.publish(regressedFresh));
    OwnedObserverBus slidingProgressBus;
    check("progress-backed sliding base publishes",
          slidingProgressBus.publish(freshAttempt));
    PearlObserverSnapshot progressedFresh = freshAttempt;
    ++progressedFresh.progress.attemptsStarted;
    ++progressedFresh.progress.attemptsCompleted;
    ++progressedFresh.liveness.completedAttempts;
    ++progressedFresh.liveness.completedAttemptsForJob;
    ++progressedFresh.publishedAtMs;
    ++progressedFresh.liveness.attemptDeadline.atMs;
    check("Fresh-to-Fresh slide requires exact attempt progress",
          progressedFresh.invariantsHold() &&
              slidingProgressBus.publish(progressedFresh));

    PearlObserverSnapshot expiredConnection = awaitingAuthorization;
    expiredConnection.liveness.connectionDeadline.state =
        PearlDeadlineState::Expired;
    expiredConnection.liveness.connectionDeadline.atMs =
        expiredConnection.publishedAtMs;
    OwnedObserverBus unrelatedConnectionDeadlineBus;
    check("expired connection deadline base publishes",
          expiredConnection.invariantsHold() &&
              unrelatedConnectionDeadlineBus.publish(expiredConnection));
    PearlObserverSnapshot unrelatedConnectionRefresh = expiredConnection;
    ++unrelatedConnectionRefresh.sourceEventsDrained;
    ++unrelatedConnectionRefresh.lastSourceEventToken;
    ++unrelatedConnectionRefresh.lastSourceEventMs;
    ++unrelatedConnectionRefresh.publishedAtMs;
    stampSourceEvent(&unrelatedConnectionRefresh,
                     PearlSourceEventKind::JobRetainedPreAuth,
                     unrelatedConnectionRefresh.sourceSessionToken);
    unrelatedConnectionRefresh.lastSourceEvent.jobToken = 450001;
    unrelatedConnectionRefresh.lastSourceEvent.jobIdentity = digest(45);
    unrelatedConnectionRefresh.liveness.connectionDeadline.state =
        PearlDeadlineState::Fresh;
    unrelatedConnectionRefresh.liveness.connectionDeadline.atMs =
        unrelatedConnectionRefresh.publishedAtMs + 20;
    check("unrelated source cursor cannot refresh connection deadline",
          unrelatedConnectionRefresh.invariantsHold() &&
              !unrelatedConnectionDeadlineBus.publish(
                  unrelatedConnectionRefresh));

    OwnedObserverBus fabricatedJobGenerationDeadlineBus;
    check("fabricated-job-generation deadline base publishes",
          fabricatedJobGenerationDeadlineBus.publish(expiredConnection));
    PearlObserverSnapshot fabricatedJobGenerationDeadline =
        expiredConnection;
    ++fabricatedJobGenerationDeadline.progress.jobGeneration;
    ++fabricatedJobGenerationDeadline.liveness.jobGeneration;
    ++fabricatedJobGenerationDeadline.publishedAtMs;
    fabricatedJobGenerationDeadline.liveness.connectionDeadline.state =
        PearlDeadlineState::Fresh;
    fabricatedJobGenerationDeadline.liveness.connectionDeadline.atMs =
        fabricatedJobGenerationDeadline.publishedAtMs + 20;
    check("bare job generation cannot refresh connection deadline",
          fabricatedJobGenerationDeadline.invariantsHold() &&
              !fabricatedJobGenerationDeadlineBus.publish(
                  fabricatedJobGenerationDeadline));

    PearlObserverSnapshot faultBase = tagged(13);
    PearlObserverSnapshot fabricatedFault = faultBase;
    fabricatedFault.sourceFaultObserved = true;
    fabricatedFault.sourceFaultEvents = 1;
    ++fabricatedFault.publishedAtMs;
    OwnedObserverBus fabricatedFaultBus;
    check("source-fault base publishes",
          fabricatedFaultBus.publish(faultBase));
    check("fault latch without a causal source event is rejected",
          fabricatedFault.invariantsHold() &&
              !fabricatedFaultBus.publish(fabricatedFault));

    PearlObserverSnapshot causalFault = fabricatedFault;
    ++causalFault.sourceEventsDrained;
    ++causalFault.lastSourceEventToken;
    ++causalFault.lastSourceEventMs;
    stampSourceFaultEvent(&causalFault, PearlSourceReason::Transport);
    OwnedObserverBus faultBus;
    check("causal source-fault base publishes", faultBus.publish(faultBase));
    check("source fault latches with exact event evidence",
          causalFault.invariantsHold() && faultBus.publish(causalFault));
    OwnedObserverBus noEffectFaultBus;
    check("no-effect SourceFault base publishes",
          noEffectFaultBus.publish(causalFault));
    PearlObserverSnapshot noEffectFault = causalFault;
    ++noEffectFault.sourceEventsDrained;
    ++noEffectFault.lastSourceEventToken;
    ++noEffectFault.lastSourceEventMs;
    ++noEffectFault.publishedAtMs;
    stampSourceFaultEvent(&noEffectFault, PearlSourceReason::Transport);
    check("SourceFault cannot repeat without its exact counter effect",
          noEffectFault.invariantsHold() &&
              !noEffectFaultBus.publish(noEffectFault));
    OwnedObserverBus sameSessionRestartFaultBus;
    check("faulted same-session restart base publishes",
          sameSessionRestartFaultBus.publish(causalFault));
    PearlObserverSnapshot sameSessionRestartFault = causalFault;
    ++sameSessionRestartFault.sourceEventsDrained;
    ++sameSessionRestartFault.lastSourceEventToken;
    ++sameSessionRestartFault.lastSourceEventMs;
    ++sameSessionRestartFault.publishedAtMs;
    stampSourceEvent(&sameSessionRestartFault,
                     PearlSourceEventKind::SessionStarted,
                     sameSessionRestartFault.sourceSessionToken);
    check("same-session SessionStarted cannot retain a fault latch",
          sameSessionRestartFault.invariantsHold() &&
              !sameSessionRestartFaultBus.publish(
                  sameSessionRestartFault));
    OwnedObserverBus wrongFaultKindBus;
    check("wrong-kind fault base publishes",
          wrongFaultKindBus.publish(faultBase));
    PearlObserverSnapshot wrongFaultKind = causalFault;
    stampSourceEvent(&wrongFaultKind, PearlSourceEventKind::JobAvailable,
                     wrongFaultKind.sourceSessionToken);
    wrongFaultKind.lastSourceEvent.authorizationToken =
        wrongFaultKind.sourceAuthorizationToken;
    wrongFaultKind.lastSourceEvent.jobToken = wrongFaultKind.activeJobToken;
    wrongFaultKind.lastSourceEvent.jobIdentity =
        wrongFaultKind.activeJobIdentity;
    check("generic cursor advance cannot fabricate a SourceFault",
          wrongFaultKind.invariantsHold() &&
              !wrongFaultKindBus.publish(wrongFaultKind));
    OwnedObserverBus wrongFaultSessionBus;
    check("wrong-session fault base publishes",
          wrongFaultSessionBus.publish(faultBase));
    PearlObserverSnapshot wrongFaultSession = causalFault;
    ++wrongFaultSession.lastSourceEvent.sessionToken;
    check("SourceFault event must match the exact source session",
          wrongFaultSession.invariantsHold() &&
              !wrongFaultSessionBus.publish(wrongFaultSession));
    OwnedObserverBus sameMillisecondFaultBus;
    check("same-millisecond fault base publishes",
          sameMillisecondFaultBus.publish(faultBase));
    PearlObserverSnapshot sameMillisecondFault = causalFault;
    sameMillisecondFault.lastSourceEventMs = faultBase.lastSourceEventMs;
    sameMillisecondFault.lastSourceEvent.timestampMs =
        sameMillisecondFault.lastSourceEventMs;
    check("strict token cursor permits same-millisecond source events",
          sameMillisecondFault.invariantsHold() &&
              sameMillisecondFaultBus.publish(sameMillisecondFault));
    OwnedObserverBus skippedFaultCountBus;
    check("skipped fault-count base publishes",
          skippedFaultCountBus.publish(causalFault));
    PearlObserverSnapshot skippedFaultCount = causalFault;
    skippedFaultCount.sourceFaultEvents += 2;
    ++skippedFaultCount.sourceEventsDrained;
    ++skippedFaultCount.lastSourceEventToken;
    ++skippedFaultCount.lastSourceEventMs;
    ++skippedFaultCount.publishedAtMs;
    stampSourceFaultEvent(&skippedFaultCount, PearlSourceReason::Protocol);
    check("one SourceFault event cannot skip its saturating counter",
          skippedFaultCount.invariantsHold() &&
              !skippedFaultCountBus.publish(skippedFaultCount));
    PearlObserverSnapshot illegalFaultClear = causalFault;
    illegalFaultClear.sourceFaultObserved = false;
    ++illegalFaultClear.publishedAtMs;
    check("source fault cannot clear inside one session",
          illegalFaultClear.invariantsHold() &&
              !faultBus.publish(illegalFaultClear));

    OwnedObserverBus faultDisconnectBus;
    check("disconnect fault base publishes",
          faultDisconnectBus.publish(causalFault));
    const PearlObserverSnapshot faultRetired =
        retireActiveJob(causalFault);
    check("faulted active job retires with exact source evidence",
          faultDisconnectBus.publish(faultRetired));
    const PearlObserverSnapshot faultReleased =
        releaseIssuedJob(faultRetired);
    check("faulted issued job releases before disconnect",
          faultDisconnectBus.publish(faultReleased));
    PearlObserverSnapshot coalescedSessionStart = faultReleased;
    ++coalescedSessionStart.progress.sessionGeneration;
    ++coalescedSessionStart.liveness.sessionGeneration;
    coalescedSessionStart.progress.phase = PearlProgressPhase::Connecting;
    coalescedSessionStart.liveness.connection =
        PearlConnectionState::AwaitingAuthorization;
    coalescedSessionStart.liveness.connectionDeadline = PearlDeadline();
    coalescedSessionStart.liveness.connectionDeadline.state =
        PearlDeadlineState::Disabled;
    coalescedSessionStart.sourceSessionToken =
        faultReleased.sourceSessionToken + 1;
    coalescedSessionStart.sourceAuthorizationToken = 0;
    coalescedSessionStart.sourceConnected = false;
    coalescedSessionStart.sourceReady = false;
    coalescedSessionStart.sourceFaultObserved = false;
    coalescedSessionStart.physicalTransportConnected = false;
    coalescedSessionStart.sourceResourcesReleased = false;
    coalescedSessionStart.sourceEventsDrained += 2;
    coalescedSessionStart.lastSourceEventToken += 2;
    ++coalescedSessionStart.lastSourceEventMs;
    ++coalescedSessionStart.publishedAtMs;
    stampSourceEvent(&coalescedSessionStart,
                     PearlSourceEventKind::SessionStarted,
                     coalescedSessionStart.sourceSessionToken);
    ++coalescedSessionStart.physicalTeardownGeneration;
    coalescedSessionStart.physicalTeardownCause =
        PearlPhysicalTeardownCause::SourceDisconnected;
    OwnedObserverBus coalescedSessionBus;
    check("coalesced session-reset base sequence publishes",
          coalescedSessionBus.publish(causalFault) &&
              coalescedSessionBus.publish(faultRetired) &&
              coalescedSessionBus.publish(faultReleased));
    check("coalesced SessionStarted fixture is internally valid",
          coalescedSessionStart.invariantsHold());
    PearlObserverSnapshot threeEventRestart = coalescedSessionStart;
    ++threeEventRestart.sourceEventsDrained;
    ++threeEventRestart.lastSourceEventToken;
    threeEventRestart.lastSourceEvent.eventToken =
        threeEventRestart.lastSourceEventToken;
    OwnedObserverBus threeEventRestartBus;
    check("restart coalescing is exactly disconnect plus SessionStarted",
          threeEventRestart.invariantsHold() &&
              threeEventRestartBus.publish(causalFault) &&
              threeEventRestartBus.publish(faultRetired) &&
              threeEventRestartBus.publish(faultReleased) &&
              !threeEventRestartBus.publish(threeEventRestart));
    check("coalesced SessionStarted clears the prior fault latch",
          coalescedSessionStart.invariantsHold() &&
              coalescedSessionBus.publish(coalescedSessionStart));
    OwnedObserverBus retainedFaultSessionBus;
    check("retained-fault session base sequence publishes",
          retainedFaultSessionBus.publish(causalFault) &&
              retainedFaultSessionBus.publish(faultRetired) &&
              retainedFaultSessionBus.publish(faultReleased));
    PearlObserverSnapshot retainedFaultSession = coalescedSessionStart;
    retainedFaultSession.sourceFaultObserved = true;
    check("SessionStarted cannot retain the prior session fault latch",
          retainedFaultSession.invariantsHold() &&
              !retainedFaultSessionBus.publish(retainedFaultSession));
    PearlObserverSnapshot faultDisconnect =
        disconnectSource(faultReleased, PearlSourceReason::Transport);
    check("fault latch clears on the tracker's exact clean disconnect",
          faultDisconnect.invariantsHold() &&
              faultDisconnectBus.publish(faultDisconnect));
    PearlObserverSnapshot restartedAfterFault = faultDisconnect;
    restartedAfterFault.progress.phase = PearlProgressPhase::Connecting;
    ++restartedAfterFault.progress.sessionGeneration;
    restartedAfterFault.liveness.connection =
        PearlConnectionState::AwaitingAuthorization;
    ++restartedAfterFault.liveness.sessionGeneration;
    restartedAfterFault.liveness.connectionDeadline.state =
        PearlDeadlineState::Disabled;
    restartedAfterFault.sourceSessionToken = 880013;
    restartedAfterFault.sourceResourcesReleased = false;
    ++restartedAfterFault.sourceEventsDrained;
    ++restartedAfterFault.lastSourceEventToken;
    ++restartedAfterFault.lastSourceEventMs;
    ++restartedAfterFault.publishedAtMs;
    stampSourceEvent(&restartedAfterFault,
                     PearlSourceEventKind::SessionStarted,
                     restartedAfterFault.sourceSessionToken);
    check("later clean session can start after a cleared fault",
          restartedAfterFault.invariantsHold() &&
              faultDisconnectBus.publish(restartedAfterFault));
    PearlObserverSnapshot relatchedFault = restartedAfterFault;
    relatchedFault.sourceFaultObserved = true;
    ++relatchedFault.sourceFaultEvents;
    ++relatchedFault.sourceEventsDrained;
    ++relatchedFault.lastSourceEventToken;
    // Deliberately the same millisecond as SessionStarted.
    ++relatchedFault.publishedAtMs;
    stampSourceFaultEvent(&relatchedFault, PearlSourceReason::Protocol);
    check("later session relatches only its own exact SourceFault",
          relatchedFault.invariantsHold() &&
              faultDisconnectBus.publish(relatchedFault));

    OwnedObserverBus outcomeBus;
    PearlObserverSnapshot fabricated = tagged(1);
    addSubmitFunnel(&fabricated);
    fabricated.progress.submitted = 1;
    fabricated.progress.acknowledged = 1;
    fabricated.progress.accepted = 1;
    fabricated.progress.lastOutcome = PearlSubmitOutcome::Accepted;
    check("fabricated Accepted without liveness evidence is rejected",
          fabricated.progress.invariantsHold() &&
              pearlObserverLivenessShapeHolds(fabricated.liveness) &&
              !outcomeBus.publish(fabricated));

    OwnedObserverBus acceptedBus;
    PearlObserverSnapshot acceptedOne = accepted(2);
    PearlObserverSnapshot acceptedDispatch = openDispatchedSubmit(2);
    ++acceptedOne.sourceEventsDrained;
    ++acceptedOne.lastSourceEventToken;
    ++acceptedOne.lastSourceEventMs;
    ++acceptedOne.publishedAtMs;
    stampTerminalEvent(&acceptedOne, acceptedDispatch,
                       PearlSourceTerminal::Accepted);
    check("correlated Accepted fixture is internally valid",
          acceptedOne.invariantsHold());
    OwnedObserverBus noPriorDispatchBus;
    check("remote Accepted cannot be the initial unowned publication",
          !noPriorDispatchBus.publish(acceptedOne));
    check("correlated Accepted reconciles exactly",
          acceptedBus.publish(acceptedDispatch) &&
              acceptedBus.publish(acceptedOne));
    PearlObserverSnapshot lostAccepted = acceptedOne;
    --lostAccepted.progress.accepted;
    ++lostAccepted.progress.unknown;
    ++lostAccepted.sourceEventsDrained;
    ++lostAccepted.lastSourceEventToken;
    ++lostAccepted.lastSourceEventMs;
    ++lostAccepted.publishedAtMs;
    stampTerminalEvent(&lostAccepted, acceptedDispatch,
                       PearlSourceTerminal::Accepted);
    check("later standalone-valid loss of Accepted is rejected",
          lostAccepted.invariantsHold() &&
              !acceptedBus.publish(lostAccepted));

    OwnedObserverBus remoteCausalityBus;
    PearlObserverSnapshot remoteBase = tagged(20);
    check("remote causality base publishes",
          remoteCausalityBus.publish(remoteBase));
    PearlObserverSnapshot remoteWithoutEvent = accepted(20);
    ++remoteWithoutEvent.publishedAtMs;
    check("new correlated terminal without source cursor is rejected",
          remoteWithoutEvent.invariantsHold() &&
              !remoteCausalityBus.publish(remoteWithoutEvent));
    OwnedObserverBus orphanTerminalBus;
    check("orphan terminal base publishes",
          orphanTerminalBus.publish(awaitingAuthorization));
    PearlObserverSnapshot orphanTerminal = awaitingAuthorization;
    ++orphanTerminal.sourceEventsDrained;
    ++orphanTerminal.lastSourceEventToken;
    ++orphanTerminal.lastSourceEventMs;
    ++orphanTerminal.publishedAtMs;
    stampSourceEvent(&orphanTerminal,
                     PearlSourceEventKind::SubmitTerminal,
                     orphanTerminal.sourceSessionToken);
    orphanTerminal.lastSourceEvent.jobToken = 99101;
    orphanTerminal.lastSourceEvent.operationToken = 99102;
    orphanTerminal.lastSourceEvent.jobIdentity = digest(99);
    orphanTerminal.lastSourceEvent.delivery = PearlSourceDelivery::Sent;
    orphanTerminal.lastSourceEvent.terminal =
        PearlSourceTerminal::Accepted;
    orphanTerminal.lastSourceEvent.terminalEvidence =
        PearlSourceTerminalEvidence::CorrelatedRemoteOutcome;
    orphanTerminal.lastSourceEvent.acknowledged = 1;
    check("SubmitTerminal cannot be a no-effect cursor event",
          orphanTerminal.invariantsHold() &&
              !orphanTerminalBus.publish(orphanTerminal));
    OwnedObserverBus unrelatedCursorBus;
    check("unrelated-cursor base publishes",
          unrelatedCursorBus.publish(remoteBase));
    PearlObserverSnapshot remoteWithUnrelatedCursor = remoteWithoutEvent;
    ++remoteWithUnrelatedCursor.sourceEventsDrained;
    ++remoteWithUnrelatedCursor.lastSourceEventToken;
    ++remoteWithUnrelatedCursor.lastSourceEventMs;
    stampSourceEvent(&remoteWithUnrelatedCursor,
                     PearlSourceEventKind::JobAvailable,
                     remoteWithUnrelatedCursor.sourceSessionToken);
    remoteWithUnrelatedCursor.lastSourceEvent.authorizationToken =
        remoteWithUnrelatedCursor.sourceAuthorizationToken;
    remoteWithUnrelatedCursor.lastSourceEvent.jobToken =
        remoteWithUnrelatedCursor.activeJobToken;
    remoteWithUnrelatedCursor.lastSourceEvent.jobIdentity =
        remoteWithUnrelatedCursor.activeJobIdentity;
    check("unrelated valid source cursor cannot authorize Accepted",
          remoteWithUnrelatedCursor.invariantsHold() &&
              !unrelatedCursorBus.publish(remoteWithUnrelatedCursor));
    OwnedObserverBus causalRemoteBus;
    PearlObserverSnapshot dispatchedRemote = openDispatchedSubmit(20);
    check("causal remote dispatch publishes",
          causalRemoteBus.publish(dispatchedRemote));
    PearlObserverSnapshot remoteWithEvent = remoteWithoutEvent;
    ++remoteWithEvent.sourceEventsDrained;
    ++remoteWithEvent.lastSourceEventToken;
    ++remoteWithEvent.lastSourceEventMs;
    stampTerminalEvent(&remoteWithEvent, dispatchedRemote,
                       PearlSourceTerminal::Accepted);
    check("new correlated terminal requires prior exact dispatch and event",
          remoteWithEvent.invariantsHold() &&
              causalRemoteBus.publish(remoteWithEvent));
    OwnedObserverBus wrongOperationBus;
    check("wrong-operation dispatch base publishes",
          wrongOperationBus.publish(dispatchedRemote));
    PearlObserverSnapshot wrongOperation = remoteWithEvent;
    ++wrongOperation.lastSourceEvent.operationToken;
    check("terminal operation must match prior dispatched ownership",
          wrongOperation.invariantsHold() &&
              !wrongOperationBus.publish(wrongOperation));
    OwnedObserverBus rewrittenDispatchBus;
    check("dispatch rewrite base publishes",
          rewrittenDispatchBus.publish(dispatchedRemote));
    PearlObserverSnapshot rewrittenDispatch = dispatchedRemote;
    ++rewrittenDispatch.inFlightOperationToken;
    ++rewrittenDispatch.sourceEventsDrained;
    ++rewrittenDispatch.lastSourceEventToken;
    ++rewrittenDispatch.lastSourceEventMs;
    ++rewrittenDispatch.publishedAtMs;
    stampSourceEvent(&rewrittenDispatch,
                     PearlSourceEventKind::SubmitDispatched,
                     rewrittenDispatch.inFlightSessionToken);
    rewrittenDispatch.lastSourceEvent.authorizationToken =
        rewrittenDispatch.inFlightAuthorizationToken;
    rewrittenDispatch.lastSourceEvent.jobToken =
        rewrittenDispatch.inFlightJobToken;
    rewrittenDispatch.lastSourceEvent.operationToken =
        rewrittenDispatch.inFlightOperationToken;
    rewrittenDispatch.lastSourceEvent.jobIdentity =
        rewrittenDispatch.inFlightJobIdentity;
    rewrittenDispatch.lastSourceEvent.delivery =
        PearlSourceDelivery::NotSent;
    check("nonzero in-flight dispatch tuple is immutable until terminal",
          rewrittenDispatch.invariantsHold() &&
              !rewrittenDispatchBus.publish(rewrittenDispatch));
    OwnedObserverBus erasedDispatchBus;
    check("dispatch erasure base publishes",
          erasedDispatchBus.publish(dispatchedRemote));
    PearlObserverSnapshot erasedDispatch = dispatchedRemote;
    erasedDispatch.progress.phase = PearlProgressPhase::Searching;
    ++erasedDispatch.progress.candidateGeneration;
    ++erasedDispatch.progress.submitGeneration;
    erasedDispatch.liveness.candidate = PearlCandidateState::Idle;
    ++erasedDispatch.liveness.candidateGeneration;
    ++erasedDispatch.liveness.submitGeneration;
    erasedDispatch.liveness.candidateDeadline = PearlDeadline();
    erasedDispatch.unresolvedSubmit = false;
    erasedDispatch.sourceDispatchObserved = false;
    erasedDispatch.unresolvedSubmitGeneration = 0;
    erasedDispatch.inFlightOperationToken = 0;
    erasedDispatch.inFlightSessionToken = 0;
    erasedDispatch.inFlightAuthorizationToken = 0;
    erasedDispatch.inFlightJobToken = 0;
    erasedDispatch.inFlightJobIdentity = PearlJobIdentityDigest();
    ++erasedDispatch.sourceEventsDrained;
    ++erasedDispatch.lastSourceEventToken;
    ++erasedDispatch.lastSourceEventMs;
    ++erasedDispatch.publishedAtMs;
    stampSourceEvent(&erasedDispatch,
                     PearlSourceEventKind::JobAvailable,
                     erasedDispatch.sourceSessionToken);
    erasedDispatch.lastSourceEvent.authorizationToken =
        erasedDispatch.sourceAuthorizationToken;
    erasedDispatch.lastSourceEvent.jobToken = erasedDispatch.activeJobToken;
    erasedDispatch.lastSourceEvent.jobIdentity =
        erasedDispatch.activeJobIdentity;
    check("generation bumps and nonterminal cursor cannot erase dispatch",
          !erasedDispatch.invariantsHold() &&
              !erasedDispatchBus.publish(erasedDispatch));

    PearlObserverSnapshot generationTwoDispatch = dispatchedRemote;
    generationTwoDispatch.progress.submitGeneration = 2;
    generationTwoDispatch.liveness.submitGeneration = 2;
    generationTwoDispatch.unresolvedSubmitGeneration = 2;
    OwnedObserverBus wrongTerminalGenerationBus;
    check("generation-two dispatch base publishes",
          generationTwoDispatch.invariantsHold() &&
              wrongTerminalGenerationBus.publish(generationTwoDispatch));
    PearlObserverSnapshot wrongTerminalGeneration = remoteWithEvent;
    wrongTerminalGeneration.progress.submitGeneration = 2;
    wrongTerminalGeneration.liveness.submitGeneration = 2;
    wrongTerminalGeneration.liveness.lastOutcomeSubmitGeneration = 1;
    check("terminal must name the prior unresolved submit generation",
          wrongTerminalGeneration.invariantsHold() &&
              !wrongTerminalGenerationBus.publish(
                  wrongTerminalGeneration));

    PearlObserverSnapshot canonicalRemoteUnknown = remoteWithEvent;
    canonicalRemoteUnknown.progress.accepted = 0;
    canonicalRemoteUnknown.progress.unknown = 1;
    canonicalRemoteUnknown.progress.lastOutcome =
        PearlSubmitOutcome::Unknown;
    canonicalRemoteUnknown.liveness.lastOutcome =
        PearlSubmitOutcome::Unknown;
    stampTerminalEvent(&canonicalRemoteUnknown, dispatchedRemote,
                       PearlSourceTerminal::Unknown);
    OwnedObserverBus canonicalRemoteUnknownBus;
    check("canonical sent-and-acknowledged correlated Unknown publishes",
          canonicalRemoteUnknown.invariantsHold() &&
              canonicalRemoteUnknownBus.publish(dispatchedRemote) &&
              canonicalRemoteUnknownBus.publish(canonicalRemoteUnknown));
    PearlObserverSnapshot possiblySentAsRemote = canonicalRemoteUnknown;
    possiblySentAsRemote.lastSourceEvent.delivery =
        PearlSourceDelivery::PossiblySent;
    possiblySentAsRemote.lastSourceEvent.acknowledged = 0;
    possiblySentAsRemote.lastSourceEvent.reason =
        PearlSourceReason::Transport;
    possiblySentAsRemote.lastSourceEvent.terminalEvidence =
        PearlSourceTerminalEvidence::PossiblyDeliveredUnknown;
    OwnedObserverBus possiblySentAsRemoteBus;
    check("possibly-sent fixture base publishes",
          possiblySentAsRemoteBus.publish(dispatchedRemote));
    check("PossiblySent unacknowledged cannot claim correlated remote Unknown",
          possiblySentAsRemote.invariantsHold() &&
              !possiblySentAsRemoteBus.publish(possiblySentAsRemote));
    PearlObserverSnapshot unacknowledgedSentAsRemote =
        canonicalRemoteUnknown;
    unacknowledgedSentAsRemote.lastSourceEvent.acknowledged = 0;
    unacknowledgedSentAsRemote.lastSourceEvent.reason =
        PearlSourceReason::Timeout;
    unacknowledgedSentAsRemote.lastSourceEvent.terminalEvidence =
        PearlSourceTerminalEvidence::FullySentNoReply;
    OwnedObserverBus unacknowledgedSentAsRemoteBus;
    check("unacknowledged-sent fixture base publishes",
          unacknowledgedSentAsRemoteBus.publish(dispatchedRemote));
    check("Sent unacknowledged cannot claim correlated remote Unknown",
          unacknowledgedSentAsRemote.invariantsHold() &&
              !unacknowledgedSentAsRemoteBus.publish(
                  unacknowledgedSentAsRemote));

    const PearlObserverSnapshot localFailureTerminal =
        terminalFromDispatch(
            dispatchedRemote, PearlSubmitOutcome::SubmissionFailed,
            PearlOutcomeEvidence::LocalPreDeliveryFailure,
            PearlSourceDelivery::NotSent,
            PearlSourceTerminal::LocalFailure, false,
            PearlSourceReason::Transport);
    OwnedObserverBus localFailureTerminalBus;
    check("LocalPreDeliveryFailure has its exact terminal deltas",
          localFailureTerminal.invariantsHold() &&
              localFailureTerminalBus.publish(dispatchedRemote) &&
              localFailureTerminalBus.publish(localFailureTerminal));
    const PearlObserverSnapshot fullySentTerminal = terminalFromDispatch(
        dispatchedRemote, PearlSubmitOutcome::Unknown,
        PearlOutcomeEvidence::FullySentNoReply,
        PearlSourceDelivery::Sent, PearlSourceTerminal::Unknown, false,
        PearlSourceReason::Timeout);
    OwnedObserverBus fullySentTerminalBus;
    check("FullySentNoReply has its exact terminal deltas",
          fullySentTerminal.invariantsHold() &&
              fullySentTerminalBus.publish(dispatchedRemote) &&
              fullySentTerminalBus.publish(fullySentTerminal));
    const PearlObserverSnapshot gatewayTerminal = terminalFromDispatch(
        dispatchedRemote, PearlSubmitOutcome::Acknowledged,
        PearlOutcomeEvidence::GatewayAcknowledgementOnly,
        PearlSourceDelivery::Sent, PearlSourceTerminal::GatewayQueued, true,
        PearlSourceReason::None);
    OwnedObserverBus gatewayTerminalBus;
    check("GatewayAcknowledgementOnly has its exact terminal deltas",
          gatewayTerminal.invariantsHold() &&
              gatewayTerminalBus.publish(dispatchedRemote) &&
              gatewayTerminalBus.publish(gatewayTerminal));
    const PearlObserverSnapshot unavailableTerminal = terminalFromDispatch(
        dispatchedRemote, PearlSubmitOutcome::Unknown,
        PearlOutcomeEvidence::AcknowledgedOutcomeUnavailable,
        PearlSourceDelivery::Sent, PearlSourceTerminal::Unknown, true,
        PearlSourceReason::None);
    OwnedObserverBus unavailableTerminalBus;
    check("AcknowledgedOutcomeUnavailable has exact terminal deltas",
          unavailableTerminal.invariantsHold() &&
              unavailableTerminalBus.publish(dispatchedRemote) &&
              unavailableTerminalBus.publish(unavailableTerminal));
    PearlObserverSnapshot unavailableMislabeledRemote = unavailableTerminal;
    unavailableMislabeledRemote.lastSourceEvent.terminalEvidence =
        PearlSourceTerminalEvidence::CorrelatedRemoteOutcome;
    OwnedObserverBus unavailableMislabeledRemoteBus;
    check("unavailable outcome cannot consume correlated-remote evidence",
          unavailableMislabeledRemote.invariantsHold() &&
              unavailableMislabeledRemoteBus.publish(dispatchedRemote) &&
              !unavailableMislabeledRemoteBus.publish(
                  unavailableMislabeledRemote));
    OwnedObserverBus correlatedRemoteUnknownBus;
    check("CorrelatedRemote Unknown has exact terminal deltas",
          correlatedRemoteUnknownBus.publish(dispatchedRemote) &&
              correlatedRemoteUnknownBus.publish(canonicalRemoteUnknown));
    PearlObserverSnapshot remoteMislabeledUnavailable =
        canonicalRemoteUnknown;
    remoteMislabeledUnavailable.lastSourceEvent.terminalEvidence =
        PearlSourceTerminalEvidence::AcknowledgedOutcomeUnavailable;
    OwnedObserverBus remoteMislabeledUnavailableBus;
    check("correlated remote Unknown cannot consume unavailable evidence",
          remoteMislabeledUnavailable.invariantsHold() &&
              remoteMislabeledUnavailableBus.publish(dispatchedRemote) &&
              !remoteMislabeledUnavailableBus.publish(
                  remoteMislabeledUnavailable));
    PearlObserverSnapshot injectedAcknowledgement = dispatchedRemote;
    injectedAcknowledgement.progress.phase =
        PearlProgressPhase::AwaitingOutcome;
    injectedAcknowledgement.progress.submitted = 1;
    injectedAcknowledgement.progress.acknowledged = 1;
    injectedAcknowledgement.progress.pending = 1;
    injectedAcknowledgement.liveness.candidate =
        PearlCandidateState::AwaitingOutcome;
    injectedAcknowledgement.liveness.acknowledgements = 1;
    ++injectedAcknowledgement.publishedAtMs;
    OwnedObserverBus injectedAcknowledgementBus;
    check("eventless acknowledgement cannot pre-consume terminal evidence",
          injectedAcknowledgement.invariantsHold() &&
              injectedAcknowledgementBus.publish(dispatchedRemote) &&
              !injectedAcknowledgementBus.publish(
                  injectedAcknowledgement));
    const PearlObserverSnapshot staleRemoteTerminal = terminalFromDispatch(
        dispatchedRemote, PearlSubmitOutcome::Stale,
        PearlOutcomeEvidence::CorrelatedRemoteOutcome,
        PearlSourceDelivery::Sent, PearlSourceTerminal::Stale, true,
        PearlSourceReason::None);
    OwnedObserverBus staleRemoteTerminalBus;
    check("correlated remote Stale has its exact terminal deltas",
          staleRemoteTerminal.invariantsHold() &&
              staleRemoteTerminalBus.publish(dispatchedRemote) &&
              staleRemoteTerminalBus.publish(staleRemoteTerminal));

    PearlObserverSnapshot secondCandidate = localFailureTerminal;
    secondCandidate.progress.phase = PearlProgressPhase::AwaitingSubmit;
    secondCandidate.progress.finalizerGeneration = 2;
    secondCandidate.progress.candidateGeneration = 2;
    secondCandidate.progress.hitsObserved = 2;
    secondCandidate.progress.hitsRetained = 2;
    secondCandidate.progress.finalizersStarted = 2;
    secondCandidate.progress.candidatesSelected = 2;
    secondCandidate.progress.hostChecked = 2;
    secondCandidate.progress.hostValid = 2;
    secondCandidate.progress.proofsSerialized = 2;
    secondCandidate.liveness.candidateGeneration = 2;
    secondCandidate.liveness.candidatesObserved = 2;
    secondCandidate.liveness.candidatesFinalized = 2;
    secondCandidate.liveness.candidate = PearlCandidateState::AwaitingSubmit;
    secondCandidate.liveness.candidateDeadline.state =
        PearlDeadlineState::Disabled;
    ++secondCandidate.publishedAtMs;
    PearlObserverSnapshot twoAttemptDispatch = secondCandidate;
    twoAttemptDispatch.progress.phase =
        PearlProgressPhase::AwaitingAcknowledgement;
    twoAttemptDispatch.progress.submitGeneration = 2;
    twoAttemptDispatch.progress.submitAttempts = 2;
    twoAttemptDispatch.liveness.submitGeneration = 2;
    twoAttemptDispatch.liveness.submitAttempts = 2;
    twoAttemptDispatch.liveness.candidate =
        PearlCandidateState::AwaitingAcknowledgement;
    twoAttemptDispatch.unresolvedSubmitGeneration = 2;
    twoAttemptDispatch.unresolvedSubmit = true;
    twoAttemptDispatch.sourceDispatchObserved = true;
    twoAttemptDispatch.inFlightOperationToken = 700002;
    twoAttemptDispatch.inFlightSessionToken =
        twoAttemptDispatch.sourceSessionToken;
    twoAttemptDispatch.inFlightAuthorizationToken =
        twoAttemptDispatch.sourceAuthorizationToken;
    twoAttemptDispatch.inFlightJobToken = twoAttemptDispatch.issuedJobToken;
    twoAttemptDispatch.inFlightJobIdentity = twoAttemptDispatch.issuedJobIdentity;
    ++twoAttemptDispatch.sourceEventsDrained;
    ++twoAttemptDispatch.lastSourceEventToken;
    ++twoAttemptDispatch.lastSourceEventMs;
    ++twoAttemptDispatch.publishedAtMs;
    stampSourceEvent(&twoAttemptDispatch,
                     PearlSourceEventKind::SubmitDispatched,
                     twoAttemptDispatch.inFlightSessionToken);
    twoAttemptDispatch.lastSourceEvent.authorizationToken =
        twoAttemptDispatch.inFlightAuthorizationToken;
    twoAttemptDispatch.lastSourceEvent.jobToken =
        twoAttemptDispatch.inFlightJobToken;
    twoAttemptDispatch.lastSourceEvent.operationToken =
        twoAttemptDispatch.inFlightOperationToken;
    twoAttemptDispatch.lastSourceEvent.jobIdentity =
        twoAttemptDispatch.inFlightJobIdentity;
    twoAttemptDispatch.lastSourceEvent.delivery =
        PearlSourceDelivery::NotSent;
    PearlObserverSnapshot multiEvidenceTerminal = terminalFromDispatch(
        twoAttemptDispatch, PearlSubmitOutcome::SubmissionFailed,
        PearlOutcomeEvidence::LocalPreDeliveryFailure,
        PearlSourceDelivery::NotSent,
        PearlSourceTerminal::LocalFailure, false,
        PearlSourceReason::Transport);
    --multiEvidenceTerminal.progress.submissionFailures;
    --multiEvidenceTerminal.liveness.submissionFailures;
    ++multiEvidenceTerminal.progress.submitted;
    ++multiEvidenceTerminal.progress.acknowledged;
    ++multiEvidenceTerminal.progress.accepted;
    ++multiEvidenceTerminal.liveness.acknowledgements;
    OwnedObserverBus multiEvidenceTerminalBus;
    check("multiple-evidence dispatch base is internally valid",
          twoAttemptDispatch.invariantsHold() &&
              multiEvidenceTerminalBus.publish(dispatchedRemote) &&
              multiEvidenceTerminalBus.publish(localFailureTerminal) &&
              multiEvidenceTerminalBus.publish(secondCandidate) &&
              multiEvidenceTerminalBus.publish(twoAttemptDispatch));
    check("one terminal event cannot manufacture multiple evidence totals",
          multiEvidenceTerminal.invariantsHold() &&
              !multiEvidenceTerminalBus.publish(multiEvidenceTerminal));

    OwnedObserverBus falseSupersededBus;
    check("accepted-superseded negative base publishes",
          falseSupersededBus.publish(dispatchedRemote));
    PearlObserverSnapshot falseSuperseded = remoteWithEvent;
    falseSuperseded.progress.acceptedSuperseded = 1;
    check("acceptedSuperseded cannot increment without event evidence",
          falseSuperseded.invariantsHold() &&
              !falseSupersededBus.publish(falseSuperseded));
    OwnedObserverBus trueSupersededBus;
    check("accepted-superseded positive base publishes",
          trueSupersededBus.publish(dispatchedRemote));
    PearlObserverSnapshot trueSuperseded = remoteWithEvent;
    trueSuperseded.progress.acceptedSuperseded = 1;
    trueSuperseded.lastSourceEvent.superseded = 1;
    check("acceptedSuperseded follows the exact terminal event flag",
          trueSuperseded.invariantsHold() &&
              trueSupersededBus.publish(trueSuperseded));
    OwnedObserverBus lateSupersededBus;
    check("late-superseded base publishes",
          lateSupersededBus.publish(dispatchedRemote) &&
              lateSupersededBus.publish(remoteWithEvent));
    PearlObserverSnapshot lateSuperseded = remoteWithEvent;
    lateSuperseded.progress.acceptedSuperseded = 1;
    ++lateSuperseded.publishedAtMs;
    check("acceptedSuperseded cannot change after its terminal event",
          lateSuperseded.invariantsHold() &&
              !lateSupersededBus.publish(lateSuperseded));

    OwnedObserverBus saturatedBus;
    PearlObserverSnapshot saturated = saturatedTerminal();
    check("saturated terminal baseline is publishable",
          saturated.invariantsHold() && saturatedBus.publish(saturated));
    PearlObserverSnapshot saturatedRemote = saturated;
    saturatedRemote.progress.phase = PearlProgressPhase::Searching;
    saturatedRemote.progress.lastOutcome = PearlSubmitOutcome::Rejected;
    ++saturatedRemote.progress.rejected;
    saturatedRemote.progress.pending = 0;
    saturatedRemote.liveness.candidate = PearlCandidateState::Idle;
    saturatedRemote.liveness.lastOutcome = PearlSubmitOutcome::Rejected;
    saturatedRemote.liveness.lastOutcomeEvidence =
        PearlOutcomeEvidence::CorrelatedRemoteOutcome;
    saturatedRemote.liveness.lastOutcomeSubmitGeneration =
        saturatedRemote.liveness.submitGeneration;
    saturatedRemote.liveness.outcomeFreshness.state =
        PearlDeadlineState::Disabled;
    saturatedRemote.liveness.candidateDeadline = PearlDeadline();
    ++saturatedRemote.liveness.outcomes;
    saturatedRemote.unresolvedSubmit = false;
    saturatedRemote.sourceDispatchObserved = false;
    saturatedRemote.unresolvedSubmitGeneration = 0;
    saturatedRemote.inFlightOperationToken = 0;
    saturatedRemote.inFlightSessionToken = 0;
    saturatedRemote.inFlightAuthorizationToken = 0;
    saturatedRemote.inFlightJobToken = 0;
    saturatedRemote.inFlightJobIdentity = PearlJobIdentityDigest();
    ++saturatedRemote.sourceEventsDrained;
    ++saturatedRemote.lastSourceEventToken;
    ++saturatedRemote.lastSourceEventMs;
    ++saturatedRemote.publishedAtMs;
    stampTerminalEvent(&saturatedRemote, saturated,
                       PearlSourceTerminal::Rejected);
    check("saturated terminal update remains internally coherent",
          saturatedRemote.invariantsHold());
    check("saturated terminal counters retain causal last-outcome updates",
          saturatedBus.publish(saturatedRemote));
    PearlObserverSnapshot changedAtSaturation = saturatedRemote;
    changedAtSaturation.progress.lastOutcome = PearlSubmitOutcome::Accepted;
    changedAtSaturation.liveness.lastOutcome = PearlSubmitOutcome::Accepted;
    ++changedAtSaturation.sourceEventsDrained;
    ++changedAtSaturation.lastSourceEventToken;
    ++changedAtSaturation.publishedAtMs;
    stampTerminalEvent(&changedAtSaturation, saturated,
                       PearlSourceTerminal::Accepted);
    check("MAX-to-MAX freezes terminal and evidence identity",
          changedAtSaturation.invariantsHold() &&
              !saturatedBus.publish(changedAtSaturation));
    OwnedObserverBus saturatedSessionForgeryBus;
    check("saturated session-forgery baseline publishes",
          saturatedSessionForgeryBus.publish(saturated) &&
              saturatedSessionForgeryBus.publish(saturatedRemote));
    PearlObserverSnapshot saturatedSessionForgery = saturatedRemote;
    ++saturatedSessionForgery.progress.sessionGeneration;
    ++saturatedSessionForgery.liveness.sessionGeneration;
    saturatedSessionForgery.progress.lastOutcome =
        PearlSubmitOutcome::None;
    saturatedSessionForgery.liveness.lastOutcome =
        PearlSubmitOutcome::None;
    saturatedSessionForgery.liveness.lastOutcomeEvidence =
        PearlOutcomeEvidence::None;
    saturatedSessionForgery.liveness.lastOutcomeSubmitGeneration = 0;
    saturatedSessionForgery.liveness.outcomeFreshness = PearlDeadline();
    ++saturatedSessionForgery.publishedAtMs;
    check("fabricated session generation cannot clear MAX terminal identity",
          saturatedSessionForgery.invariantsHold() &&
              !saturatedSessionForgeryBus.publish(
                  saturatedSessionForgery));
    OwnedObserverBus sessionResetBus;
    check("last-outcome reset fixture publishes",
          sessionResetBus.publish(acceptedDispatch) &&
              sessionResetBus.publish(acceptedOne));
    const PearlObserverSnapshot acceptedRetired =
        retireActiveJob(acceptedOne);
    check("terminal session retires its active job before restart",
          sessionResetBus.publish(acceptedRetired));
    const PearlObserverSnapshot acceptedReleased =
        releaseIssuedJob(acceptedRetired);
    check("terminal session releases its issued job before restart",
          sessionResetBus.publish(acceptedReleased));
    const PearlObserverSnapshot acceptedDisconnected =
        disconnectSource(acceptedReleased, PearlSourceReason::Shutdown);
    check("terminal session disconnects before restart",
          sessionResetBus.publish(acceptedDisconnected));
    PearlObserverSnapshot newSession = acceptedDisconnected;
    ++newSession.progress.sessionGeneration;
    ++newSession.liveness.sessionGeneration;
    newSession.progress.phase = PearlProgressPhase::Connecting;
    newSession.progress.lastOutcome = PearlSubmitOutcome::None;
    newSession.liveness.connection =
        PearlConnectionState::AwaitingAuthorization;
    newSession.liveness.jobProgress = PearlJobProgressState::NoJob;
    newSession.liveness.completedAttemptsForJob = 0;
    newSession.liveness.connectionDeadline.state =
        PearlDeadlineState::Disabled;
    newSession.liveness.attemptDeadline = PearlDeadline();
    newSession.liveness.lastOutcome = PearlSubmitOutcome::None;
    newSession.liveness.lastOutcomeEvidence = PearlOutcomeEvidence::None;
    newSession.liveness.lastOutcomeSubmitGeneration = 0;
    newSession.liveness.outcomeFreshness = PearlDeadline();
    newSession.sourceSessionToken = acceptedOne.sourceSessionToken + 1;
    newSession.sourceResourcesReleased = false;
    ++newSession.sourceEventsDrained;
    ++newSession.lastSourceEventToken;
    ++newSession.lastSourceEventMs;
    ++newSession.publishedAtMs;
    stampSourceEvent(&newSession, PearlSourceEventKind::SessionStarted,
                     newSession.sourceSessionToken);
    check("new session may clear last-result gauges without resetting totals",
          sessionResetBus.publish(newSession));
    OwnedObserverBus fabricatedSessionBus;
    check("fabricated-session base publishes",
          fabricatedSessionBus.publish(acceptedDispatch) &&
              fabricatedSessionBus.publish(acceptedOne));
    PearlObserverSnapshot fabricatedSession = acceptedOne;
    ++fabricatedSession.progress.sessionGeneration;
    ++fabricatedSession.liveness.sessionGeneration;
    fabricatedSession.progress.lastOutcome = PearlSubmitOutcome::None;
    fabricatedSession.liveness.lastOutcome = PearlSubmitOutcome::None;
    fabricatedSession.liveness.lastOutcomeEvidence =
        PearlOutcomeEvidence::None;
    fabricatedSession.liveness.lastOutcomeSubmitGeneration = 0;
    fabricatedSession.liveness.outcomeFreshness = PearlDeadline();
    ++fabricatedSession.publishedAtMs;
    check("numeric session generation alone cannot clear terminal identity",
          fabricatedSession.invariantsHold() &&
              !fabricatedSessionBus.publish(fabricatedSession));

    OwnedObserverBus partialBus;
    const PearlObserverSnapshot partialDispatch = openDispatchedSubmit(3);
    PearlObserverSnapshot partialTerminal = possiblyDelivered(3);
    ++partialTerminal.sourceEventsDrained;
    ++partialTerminal.lastSourceEventToken;
    ++partialTerminal.lastSourceEventMs;
    ++partialTerminal.publishedAtMs;
    stampTerminalEvent(&partialTerminal, partialDispatch,
                       PearlSourceTerminal::Unknown);
    partialTerminal.lastSourceEvent.delivery =
        PearlSourceDelivery::PossiblySent;
    partialTerminal.lastSourceEvent.acknowledged = 0;
    partialTerminal.lastSourceEvent.reason = PearlSourceReason::Transport;
    partialTerminal.lastSourceEvent.terminalEvidence =
        PearlSourceTerminalEvidence::PossiblyDeliveredUnknown;
    check("possibly-delivered terminal fixture is internally valid",
          partialTerminal.invariantsHold());
    OwnedObserverBus initialPartialBus;
    check("possibly-delivered terminal cannot be an initial observation",
          !initialPartialBus.publish(partialTerminal));
    check("possibly-delivered terminal is not manufactured as submitted",
          partialBus.publish(partialDispatch) &&
              partialBus.publish(partialTerminal) &&
              partialBus.tryRead(&sample) && sample.progress.submitted == 0 &&
              sample.progress.unknown == 0 &&
              sample.liveness.possiblyDeliveredUnknowns == 1);

    OwnedObserverBus acknowledgementBus;
    PearlObserverSnapshot badAcknowledgement = tagged(4);
    addSubmitFunnel(&badAcknowledgement);
    badAcknowledgement.progress.phase = PearlProgressPhase::AwaitingOutcome;
    badAcknowledgement.progress.submitted = 1;
    badAcknowledgement.progress.pending = 1;
    badAcknowledgement.liveness.candidate =
        PearlCandidateState::AwaitingOutcome;
    badAcknowledgement.liveness.candidateDeadline.state =
        PearlDeadlineState::Disabled;
    badAcknowledgement.liveness.acknowledgements = 1;
    badAcknowledgement.unresolvedSubmit = true;
    badAcknowledgement.unresolvedSubmitGeneration = 1;
    check("mismatched acknowledgement totals are rejected",
          badAcknowledgement.progress.invariantsHold() &&
              pearlObserverLivenessShapeHolds(badAcknowledgement.liveness) &&
              !acknowledgementBus.publish(badAcknowledgement));

    OwnedObserverBus bindingBus;
    PearlObserverSnapshot bound = openDispatchedSubmit(5);
    check("in-flight binding fixture is internally valid",
          bound.invariantsHold());
    check("in-flight ownership binds exact session/auth/job/digest tuple",
          bindingBus.publish(bound));
    PearlObserverSnapshot wrongBinding = bound;
    ++wrongBinding.inFlightAuthorizationToken;
    ++wrongBinding.publishedAtMs;
    check("mismatched in-flight authorization is rejected",
          !bindingBus.publish(wrongBinding));

    OwnedObserverBus dispatchCausalityBus;
    PearlObserverSnapshot dispatchBase = tagged(5);
    addSubmitFunnel(&dispatchBase);
    dispatchBase.progress.submitGeneration = 0;
    dispatchBase.progress.submitAttempts = 0;
    dispatchBase.liveness.submitGeneration = 0;
    dispatchBase.liveness.submitAttempts = 0;
    dispatchBase.progress.phase = PearlProgressPhase::AwaitingSubmit;
    dispatchBase.liveness.candidate = PearlCandidateState::AwaitingSubmit;
    dispatchBase.liveness.candidateDeadline.state =
        PearlDeadlineState::Disabled;
    check("dispatch causality base publishes",
          dispatchCausalityBus.publish(dispatchBase));
    PearlObserverSnapshot dispatchWithoutEvent = openDispatchedSubmit(5);
    ++dispatchWithoutEvent.publishedAtMs;
    check("in-flight source tuple cannot appear without a source event",
          dispatchWithoutEvent.invariantsHold() &&
              !dispatchCausalityBus.publish(dispatchWithoutEvent));
    OwnedObserverBus causalDispatchBus;
    check("causal dispatch base publishes",
          causalDispatchBus.publish(dispatchBase));
    PearlObserverSnapshot dispatchWithEvent = dispatchWithoutEvent;
    ++dispatchWithEvent.sourceEventsDrained;
    ++dispatchWithEvent.lastSourceEventToken;
    ++dispatchWithEvent.lastSourceEventMs;
    stampSourceEvent(&dispatchWithEvent,
                     PearlSourceEventKind::SubmitDispatched,
                     dispatchWithEvent.inFlightSessionToken);
    dispatchWithEvent.lastSourceEvent.authorizationToken =
        dispatchWithEvent.inFlightAuthorizationToken;
    dispatchWithEvent.lastSourceEvent.jobToken =
        dispatchWithEvent.inFlightJobToken;
    dispatchWithEvent.lastSourceEvent.operationToken =
        dispatchWithEvent.inFlightOperationToken;
    dispatchWithEvent.lastSourceEvent.jobIdentity =
        dispatchWithEvent.inFlightJobIdentity;
    dispatchWithEvent.lastSourceEvent.delivery =
        PearlSourceDelivery::NotSent;
    check("in-flight source tuple follows exact cursor advance",
          dispatchWithEvent.invariantsHold() &&
              causalDispatchBus.publish(dispatchWithEvent));
    PearlObserverSnapshot jumpDispatch = dispatchWithEvent;
    jumpDispatch.progress.submitGeneration = 2;
    jumpDispatch.progress.finalizerGeneration = 2;
    jumpDispatch.progress.candidateGeneration = 2;
    jumpDispatch.progress.hitsObserved = 2;
    jumpDispatch.progress.hitsRetained = 2;
    jumpDispatch.progress.finalizersStarted = 2;
    jumpDispatch.progress.candidatesSelected = 2;
    jumpDispatch.progress.hostChecked = 2;
    jumpDispatch.progress.hostValid = 2;
    jumpDispatch.progress.proofsSerialized = 2;
    jumpDispatch.progress.submitAttempts = 2;
    jumpDispatch.progress.submissionFailures = 1;
    jumpDispatch.progress.lastOutcome =
        PearlSubmitOutcome::SubmissionFailed;
    jumpDispatch.liveness.submitGeneration = 2;
    jumpDispatch.liveness.candidateGeneration = 2;
    jumpDispatch.liveness.candidatesObserved = 2;
    jumpDispatch.liveness.candidatesFinalized = 2;
    jumpDispatch.liveness.submitAttempts = 2;
    jumpDispatch.liveness.submissionFailures = 1;
    jumpDispatch.liveness.outcomes = 1;
    jumpDispatch.liveness.lastOutcome =
        PearlSubmitOutcome::SubmissionFailed;
    jumpDispatch.liveness.lastOutcomeEvidence =
        PearlOutcomeEvidence::LocalPreDeliveryFailure;
    jumpDispatch.liveness.lastOutcomeSubmitGeneration = 1;
    jumpDispatch.liveness.outcomeFreshness.state =
        PearlDeadlineState::Disabled;
    jumpDispatch.unresolvedSubmitGeneration = 2;
    OwnedObserverBus jumpDispatchBus;
    check("coalesced local submit and dispatch cannot jump two attempts",
          jumpDispatch.invariantsHold() &&
              jumpDispatchBus.publish(dispatchBase) &&
              !jumpDispatchBus.publish(jumpDispatch));
    PearlObserverSnapshot finalizingBase = dispatchBase;
    finalizingBase.progress.phase = PearlProgressPhase::Finalizing;
    finalizingBase.liveness.candidate = PearlCandidateState::Finalizing;
    OwnedObserverBus finalizingDispatchBus;
    check("SubmitDispatched cannot originate from Finalizing",
          finalizingBase.invariantsHold() &&
              finalizingDispatchBus.publish(finalizingBase) &&
              !finalizingDispatchBus.publish(dispatchWithEvent));
    PearlObserverSnapshot bareCandidateBumpDispatch = dispatchWithEvent;
    ++bareCandidateBumpDispatch.progress.candidateGeneration;
    ++bareCandidateBumpDispatch.liveness.candidateGeneration;
    OwnedObserverBus bareCandidateBumpDispatchBus;
    check("bare candidate-generation bump cannot manufacture a local submit",
          bareCandidateBumpDispatch.invariantsHold() &&
              bareCandidateBumpDispatchBus.publish(dispatchBase) &&
              !bareCandidateBumpDispatchBus.publish(
                  bareCandidateBumpDispatch));
    PearlObserverSnapshot falseToFalseSubmitAdvance = dispatchBase;
    falseToFalseSubmitAdvance.progress.submitGeneration = 2;
    falseToFalseSubmitAdvance.liveness.submitGeneration = 2;
    ++falseToFalseSubmitAdvance.publishedAtMs;
    OwnedObserverBus falseToFalseSubmitAdvanceBus;
    check("submit generations freeze while no submit is unresolved",
          falseToFalseSubmitAdvance.invariantsHold() &&
              falseToFalseSubmitAdvanceBus.publish(dispatchBase) &&
              !falseToFalseSubmitAdvanceBus.publish(
                  falseToFalseSubmitAdvance));
    PearlObserverSnapshot duplicateDispatch = dispatchWithEvent;
    ++duplicateDispatch.sourceEventsDrained;
    ++duplicateDispatch.lastSourceEventToken;
    ++duplicateDispatch.lastSourceEventMs;
    ++duplicateDispatch.publishedAtMs;
    stampSourceEvent(&duplicateDispatch,
                     PearlSourceEventKind::SubmitDispatched,
                     duplicateDispatch.inFlightSessionToken);
    duplicateDispatch.lastSourceEvent.authorizationToken =
        duplicateDispatch.inFlightAuthorizationToken;
    duplicateDispatch.lastSourceEvent.jobToken =
        duplicateDispatch.inFlightJobToken;
    duplicateDispatch.lastSourceEvent.operationToken =
        duplicateDispatch.inFlightOperationToken;
    duplicateDispatch.lastSourceEvent.jobIdentity =
        duplicateDispatch.inFlightJobIdentity;
    duplicateDispatch.lastSourceEvent.delivery =
        PearlSourceDelivery::NotSent;
    check("SubmitDispatched cannot repeat an existing in-flight operation",
          duplicateDispatch.invariantsHold() &&
              !causalDispatchBus.publish(duplicateDispatch));

    OwnedObserverBus reusedSubmitGenerationBus;
    PearlObserverSnapshot preDispatchOwnership = tagged(24);
    addSubmitFunnel(&preDispatchOwnership);
    preDispatchOwnership.progress.phase = PearlProgressPhase::Submitting;
    preDispatchOwnership.liveness.candidate =
        PearlCandidateState::AwaitingAcknowledgement;
    preDispatchOwnership.liveness.candidateDeadline.state =
        PearlDeadlineState::Disabled;
    preDispatchOwnership.unresolvedSubmit = true;
    preDispatchOwnership.unresolvedSubmitGeneration =
        preDispatchOwnership.progress.submitGeneration;
    check("old-submit-ownership base publishes",
          preDispatchOwnership.invariantsHold() &&
              reusedSubmitGenerationBus.publish(preDispatchOwnership));
    PearlObserverSnapshot erasedPreDispatch = preDispatchOwnership;
    erasedPreDispatch.progress.phase = PearlProgressPhase::Searching;
    erasedPreDispatch.liveness.candidate = PearlCandidateState::Idle;
    erasedPreDispatch.liveness.candidateDeadline = PearlDeadline();
    erasedPreDispatch.unresolvedSubmit = false;
    erasedPreDispatch.unresolvedSubmitGeneration = 0;
    ++erasedPreDispatch.progress.candidateGeneration;
    ++erasedPreDispatch.liveness.candidateGeneration;
    ++erasedPreDispatch.publishedAtMs;
    OwnedObserverBus erasedPreDispatchBus;
    check("local pre-dispatch ownership cannot be erased eventlessly",
          !erasedPreDispatch.invariantsHold() &&
              erasedPreDispatchBus.publish(preDispatchOwnership) &&
              !erasedPreDispatchBus.publish(erasedPreDispatch));
    PearlObserverSnapshot releasedDuringPreDispatch = preDispatchOwnership;
    releasedDuringPreDispatch.issuedSessionToken = 0;
    releasedDuringPreDispatch.issuedAuthorizationToken = 0;
    releasedDuringPreDispatch.issuedJobToken = 0;
    releasedDuringPreDispatch.lastReleasedIssuedJobIdentity =
        releasedDuringPreDispatch.issuedJobIdentity;
    releasedDuringPreDispatch.issuedJobIdentity =
        PearlJobIdentityDigest();
    ++releasedDuringPreDispatch.issuedBindingGeneration;
    releasedDuringPreDispatch.issuedBindingCause =
        PearlIssuedBindingCause::Released;
    ++releasedDuringPreDispatch.publishedAtMs;
    OwnedObserverBus releasedDuringPreDispatchBus;
    check("unresolved local submit retains exact issued ownership",
          !releasedDuringPreDispatch.invariantsHold() &&
              releasedDuringPreDispatchBus.publish(preDispatchOwnership) &&
              !releasedDuringPreDispatchBus.publish(
                  releasedDuringPreDispatch));
    PearlObserverSnapshot reusedSubmitGeneration = preDispatchOwnership;
    reusedSubmitGeneration.progress.phase =
        PearlProgressPhase::AwaitingAcknowledgement;
    reusedSubmitGeneration.sourceDispatchObserved = true;
    reusedSubmitGeneration.inFlightOperationToken = 97001;
    reusedSubmitGeneration.inFlightSessionToken =
        reusedSubmitGeneration.sourceSessionToken;
    reusedSubmitGeneration.inFlightAuthorizationToken =
        reusedSubmitGeneration.sourceAuthorizationToken;
    reusedSubmitGeneration.inFlightJobToken =
        reusedSubmitGeneration.issuedJobToken;
    reusedSubmitGeneration.inFlightJobIdentity =
        reusedSubmitGeneration.issuedJobIdentity;
    ++reusedSubmitGeneration.sourceEventsDrained;
    ++reusedSubmitGeneration.lastSourceEventToken;
    ++reusedSubmitGeneration.lastSourceEventMs;
    ++reusedSubmitGeneration.publishedAtMs;
    stampSourceEvent(&reusedSubmitGeneration,
                     PearlSourceEventKind::SubmitDispatched,
                     reusedSubmitGeneration.inFlightSessionToken);
    reusedSubmitGeneration.lastSourceEvent.authorizationToken =
        reusedSubmitGeneration.inFlightAuthorizationToken;
    reusedSubmitGeneration.lastSourceEvent.jobToken =
        reusedSubmitGeneration.inFlightJobToken;
    reusedSubmitGeneration.lastSourceEvent.operationToken =
        reusedSubmitGeneration.inFlightOperationToken;
    reusedSubmitGeneration.lastSourceEvent.jobIdentity =
        reusedSubmitGeneration.inFlightJobIdentity;
    reusedSubmitGeneration.lastSourceEvent.delivery =
        PearlSourceDelivery::NotSent;
    check("SubmitDispatched binds prior unresolved local submit ownership",
          reusedSubmitGeneration.invariantsHold() &&
              reusedSubmitGenerationBus.publish(reusedSubmitGeneration));
    PearlObserverSnapshot retainedDispatchPhase = reusedSubmitGeneration;
    retainedDispatchPhase.progress.phase = PearlProgressPhase::Submitting;
    OwnedObserverBus retainedDispatchPhaseBus;
    check("SubmitDispatched must publish AwaitingAcknowledgement phase",
          retainedDispatchPhase.invariantsHold() &&
              retainedDispatchPhaseBus.publish(preDispatchOwnership) &&
              !retainedDispatchPhaseBus.publish(retainedDispatchPhase));

    OwnedObserverBus completedGenerationReuseBus;
    check("completed-generation reuse base sequence publishes",
          completedGenerationReuseBus.publish(acceptedDispatch) &&
              completedGenerationReuseBus.publish(acceptedOne));
    PearlObserverSnapshot completedGenerationReuse = acceptedOne;
    completedGenerationReuse.progress.submitted = 0;
    completedGenerationReuse.progress.acknowledged = 0;
    completedGenerationReuse.progress.accepted = 0;
    completedGenerationReuse.progress.lastOutcome = PearlSubmitOutcome::None;
    completedGenerationReuse.liveness.acknowledgements = 0;
    completedGenerationReuse.liveness.outcomes = 0;
    completedGenerationReuse.liveness.lastOutcome = PearlSubmitOutcome::None;
    completedGenerationReuse.liveness.lastOutcomeEvidence =
        PearlOutcomeEvidence::None;
    completedGenerationReuse.liveness.lastOutcomeSubmitGeneration = 0;
    completedGenerationReuse.liveness.outcomeFreshness = PearlDeadline();
    completedGenerationReuse.progress.phase =
        PearlProgressPhase::AwaitingAcknowledgement;
    completedGenerationReuse.liveness.candidate =
        PearlCandidateState::AwaitingAcknowledgement;
    completedGenerationReuse.liveness.candidateDeadline.state =
        PearlDeadlineState::Disabled;
    completedGenerationReuse.unresolvedSubmit = true;
    completedGenerationReuse.sourceDispatchObserved = true;
    completedGenerationReuse.unresolvedSubmitGeneration =
        completedGenerationReuse.progress.submitGeneration;
    completedGenerationReuse.inFlightOperationToken = 97002;
    completedGenerationReuse.inFlightSessionToken =
        completedGenerationReuse.sourceSessionToken;
    completedGenerationReuse.inFlightAuthorizationToken =
        completedGenerationReuse.sourceAuthorizationToken;
    completedGenerationReuse.inFlightJobToken =
        completedGenerationReuse.issuedJobToken;
    completedGenerationReuse.inFlightJobIdentity =
        completedGenerationReuse.issuedJobIdentity;
    ++completedGenerationReuse.sourceEventsDrained;
    ++completedGenerationReuse.lastSourceEventToken;
    ++completedGenerationReuse.lastSourceEventMs;
    ++completedGenerationReuse.publishedAtMs;
    stampSourceEvent(&completedGenerationReuse,
                     PearlSourceEventKind::SubmitDispatched,
                     completedGenerationReuse.inFlightSessionToken);
    completedGenerationReuse.lastSourceEvent.authorizationToken =
        completedGenerationReuse.inFlightAuthorizationToken;
    completedGenerationReuse.lastSourceEvent.jobToken =
        completedGenerationReuse.inFlightJobToken;
    completedGenerationReuse.lastSourceEvent.operationToken =
        completedGenerationReuse.inFlightOperationToken;
    completedGenerationReuse.lastSourceEvent.jobIdentity =
        completedGenerationReuse.inFlightJobIdentity;
    completedGenerationReuse.lastSourceEvent.delivery =
        PearlSourceDelivery::NotSent;
    check("completed terminal cannot reuse old submit generation/counters",
          completedGenerationReuse.invariantsHold() &&
              !completedGenerationReuseBus.publish(
                  completedGenerationReuse));
    PearlObserverSnapshot resurrectedPreDispatch = acceptedOne;
    resurrectedPreDispatch.progress.phase = PearlProgressPhase::Submitting;
    resurrectedPreDispatch.liveness.candidate =
        PearlCandidateState::AwaitingAcknowledgement;
    resurrectedPreDispatch.liveness.candidateDeadline.state =
        PearlDeadlineState::Disabled;
    resurrectedPreDispatch.unresolvedSubmit = true;
    resurrectedPreDispatch.unresolvedSubmitGeneration =
        resurrectedPreDispatch.progress.submitGeneration;
    ++resurrectedPreDispatch.publishedAtMs;
    OwnedObserverBus resurrectedPreDispatchBus;
    check("completed submit ownership cannot resurrect in two publications",
          !resurrectedPreDispatch.invariantsHold() &&
              resurrectedPreDispatchBus.publish(acceptedDispatch) &&
              resurrectedPreDispatchBus.publish(acceptedOne) &&
              !resurrectedPreDispatchBus.publish(
                  resurrectedPreDispatch));

    OwnedObserverBus unrelatedActiveChangeBus;
    PearlObserverSnapshot activeChangeBase = tagged(18);
    check("unrelated active-change base publishes",
          unrelatedActiveChangeBus.publish(activeChangeBase));
    PearlObserverSnapshot unrelatedActiveChange = activeChangeBase;
    unrelatedActiveChange.activeJobToken = 880001;
    unrelatedActiveChange.activeJobIdentity = digest(88);
    ++unrelatedActiveChange.sourceEventsDrained;
    ++unrelatedActiveChange.lastSourceEventToken;
    ++unrelatedActiveChange.lastSourceEventMs;
    ++unrelatedActiveChange.publishedAtMs;
    stampSourceEvent(&unrelatedActiveChange,
                     PearlSourceEventKind::JobRetainedPreAuth,
                     unrelatedActiveChange.sourceSessionToken);
    unrelatedActiveChange.lastSourceEvent.jobToken = 770001;
    unrelatedActiveChange.lastSourceEvent.jobIdentity = digest(77);
    check("active identity change requires exact JobAvailable evidence",
          unrelatedActiveChange.invariantsHold() &&
              !unrelatedActiveChangeBus.publish(unrelatedActiveChange));
    OwnedObserverBus unrelatedActiveRetireBus;
    check("unrelated active-retire base publishes",
          unrelatedActiveRetireBus.publish(activeChangeBase));
    PearlObserverSnapshot unrelatedActiveRetire = activeChangeBase;
    unrelatedActiveRetire.activeJobToken = 0;
    unrelatedActiveRetire.activeJobIdentity = PearlJobIdentityDigest();
    ++unrelatedActiveRetire.sourceEventsDrained;
    ++unrelatedActiveRetire.lastSourceEventToken;
    ++unrelatedActiveRetire.lastSourceEventMs;
    ++unrelatedActiveRetire.publishedAtMs;
    stampSourceEvent(&unrelatedActiveRetire,
                     PearlSourceEventKind::JobRetainedPreAuth,
                     unrelatedActiveRetire.sourceSessionToken);
    unrelatedActiveRetire.lastSourceEvent.jobToken = 770002;
    unrelatedActiveRetire.lastSourceEvent.jobIdentity = digest(76);
    check("active identity clear requires exact JobRetired evidence",
          unrelatedActiveRetire.invariantsHold() &&
              !unrelatedActiveRetireBus.publish(unrelatedActiveRetire));

    OwnedObserverBus issuedRebindBus;
    PearlObserverSnapshot issuedBase = tagged(21);
    check("issued-binding base publishes",
          issuedRebindBus.publish(issuedBase));
    PearlObserverSnapshot issuedReleased = issuedBase;
    issuedReleased.issuedSessionToken = 0;
    issuedReleased.issuedAuthorizationToken = 0;
    issuedReleased.issuedJobToken = 0;
    issuedReleased.lastReleasedIssuedJobIdentity =
        issuedBase.issuedJobIdentity;
    issuedReleased.issuedJobIdentity = PearlJobIdentityDigest();
    ++issuedReleased.issuedBindingGeneration;
    issuedReleased.issuedBindingCause = PearlIssuedBindingCause::Released;
    ++issuedReleased.publishedAtMs;
    check("eventless issued release has its own exact generation",
          issuedReleased.invariantsHold() &&
              issuedRebindBus.publish(issuedReleased));
    PearlObserverSnapshot sameJobRebind = issuedReleased;
    sameJobRebind.issuedSessionToken = sameJobRebind.sourceSessionToken;
    sameJobRebind.issuedAuthorizationToken =
        sameJobRebind.sourceAuthorizationToken;
    sameJobRebind.issuedJobToken = sameJobRebind.activeJobToken;
    sameJobRebind.issuedJobIdentity = sameJobRebind.activeJobIdentity;
    sameJobRebind.lastReleasedIssuedJobIdentity =
        PearlJobIdentityDigest();
    ++sameJobRebind.issuedBindingGeneration;
    sameJobRebind.issuedBindingCause =
        PearlIssuedBindingCause::SameJobRebind;
    ++sameJobRebind.publishedAtMs;
    check("eventless same-job rebind preserves adopted generation",
          sameJobRebind.invariantsHold() &&
              issuedRebindBus.publish(sameJobRebind));

    OwnedObserverBus differentAdoptionBus;
    PearlObserverSnapshot oldIssued = tagged(22);
    check("different-adoption base publishes",
          differentAdoptionBus.publish(oldIssued));
    const PearlObserverSnapshot oldActiveRetired =
        retireActiveJob(oldIssued);
    check("old active source job retires before replacement",
          differentAdoptionBus.publish(oldActiveRetired));
    PearlObserverSnapshot activeReplacement = oldActiveRetired;
    activeReplacement.activeJobToken = 990001;
    activeReplacement.activeJobIdentity = digest(199);
    ++activeReplacement.sourceEventsDrained;
    ++activeReplacement.lastSourceEventToken;
    ++activeReplacement.lastSourceEventMs;
    ++activeReplacement.publishedAtMs;
    stampSourceEvent(&activeReplacement,
                     PearlSourceEventKind::JobAvailable,
                     activeReplacement.sourceSessionToken);
    activeReplacement.lastSourceEvent.authorizationToken =
        activeReplacement.sourceAuthorizationToken;
    activeReplacement.lastSourceEvent.jobToken =
        activeReplacement.activeJobToken;
    activeReplacement.lastSourceEvent.jobIdentity =
        activeReplacement.activeJobIdentity;
    check("active-new issued-old source event publishes",
          differentAdoptionBus.publish(activeReplacement));
    PearlObserverSnapshot oldReleased = activeReplacement;
    oldReleased.issuedSessionToken = 0;
    oldReleased.issuedAuthorizationToken = 0;
    oldReleased.issuedJobToken = 0;
    oldReleased.lastReleasedIssuedJobIdentity =
        activeReplacement.issuedJobIdentity;
    oldReleased.issuedJobIdentity = PearlJobIdentityDigest();
    ++oldReleased.issuedBindingGeneration;
    oldReleased.issuedBindingCause = PearlIssuedBindingCause::Released;
    ++oldReleased.publishedAtMs;
    check("old issued job releases before adoption",
          differentAdoptionBus.publish(oldReleased));
    PearlObserverSnapshot differentAdoption = oldReleased;
    differentAdoption.issuedSessionToken =
        differentAdoption.sourceSessionToken;
    differentAdoption.issuedAuthorizationToken =
        differentAdoption.sourceAuthorizationToken;
    differentAdoption.issuedJobToken = differentAdoption.activeJobToken;
    differentAdoption.issuedJobIdentity =
        differentAdoption.activeJobIdentity;
    differentAdoption.lastReleasedIssuedJobIdentity =
        PearlJobIdentityDigest();
    ++differentAdoption.issuedBindingGeneration;
    differentAdoption.issuedBindingCause =
        PearlIssuedBindingCause::DifferentJobAdoption;
    ++differentAdoption.progress.jobGeneration;
    ++differentAdoption.liveness.jobGeneration;
    differentAdoption.liveness.completedAttemptsForJob = 0;
    ++differentAdoption.publishedAtMs;
    check("different-job adoption advances both adopted job generations",
          differentAdoption.invariantsHold() &&
              differentAdoptionBus.publish(differentAdoption));

    OwnedObserverBus badSameRebindBus;
    check("bad same-rebind release baseline publishes",
          badSameRebindBus.publish(issuedReleased));
    PearlObserverSnapshot badSameRebind = sameJobRebind;
    ++badSameRebind.progress.jobGeneration;
    ++badSameRebind.liveness.jobGeneration;
    check("same-job rebind cannot advance adopted job generation",
          badSameRebind.invariantsHold() &&
              !badSameRebindBus.publish(badSameRebind));
    OwnedObserverBus badDifferentAdoptionBus;
    check("bad different-adoption release baseline publishes",
          badDifferentAdoptionBus.publish(issuedReleased));
    PearlObserverSnapshot badDifferentAdoption = sameJobRebind;
    badDifferentAdoption.issuedBindingCause =
        PearlIssuedBindingCause::DifferentJobAdoption;
    ++badDifferentAdoption.progress.jobGeneration;
    ++badDifferentAdoption.liveness.jobGeneration;
    check("different-job cause requires an actually different identity",
          badDifferentAdoption.invariantsHold() &&
              !badDifferentAdoptionBus.publish(badDifferentAdoption));

    OwnedObserverBus falseSameJobBus;
    check("false same-job base sequence publishes",
          falseSameJobBus.publish(oldIssued) &&
              falseSameJobBus.publish(oldActiveRetired) &&
              falseSameJobBus.publish(activeReplacement) &&
              falseSameJobBus.publish(oldReleased));
    PearlObserverSnapshot falseSameJob = oldReleased;
    falseSameJob.issuedSessionToken = falseSameJob.sourceSessionToken;
    falseSameJob.issuedAuthorizationToken =
        falseSameJob.sourceAuthorizationToken;
    falseSameJob.issuedJobToken = falseSameJob.activeJobToken;
    falseSameJob.issuedJobIdentity = falseSameJob.activeJobIdentity;
    falseSameJob.lastReleasedIssuedJobIdentity = PearlJobIdentityDigest();
    ++falseSameJob.issuedBindingGeneration;
    falseSameJob.issuedBindingCause = PearlIssuedBindingCause::SameJobRebind;
    ++falseSameJob.publishedAtMs;
    check("same-job cause requires identity equal to the released job",
          falseSameJob.invariantsHold() &&
              !falseSameJobBus.publish(falseSameJob));

    OwnedObserverBus digestBus;
    PearlObserverSnapshot missingDigest = tagged(6);
    missingDigest.issuedJobIdentity = PearlJobIdentityDigest();
    check("nonzero issued token requires a nonzero digest",
          !digestBus.publish(missingDigest));
    OwnedObserverBus equalTokenBus;
    PearlObserverSnapshot splitIdentity = tagged(6);
    splitIdentity.issuedJobIdentity = digest(99);
    check("equal active and issued token requires the same digest",
          !equalTokenBus.publish(splitIdentity));

    OwnedObserverBus validationBus;
    const PearlObserverSnapshot trusted = tagged(7);
    check("complete trusted envelope publishes", validationBus.publish(trusted));
    PearlObserverSnapshot rollback = tagged(8);
    rollback.publishedAtMs = trusted.publishedAtMs - 1;
    rollback.progress.publishedAtMs = rollback.publishedAtMs;
    rollback.lastSourceEventMs = rollback.publishedAtMs;
    check("publication time rollback is rejected",
          !validationBus.publish(rollback));
    check("rollback preserves trusted mining payload and latches integrity",
          validationBus.tryRead(&sample) &&
              pearlObserverProgressEqual(sample.progress, trusted.progress) &&
              sample.sourceSessionToken == trusted.sourceSessionToken &&
              sample.sourceState == PearlObserverSourceState::Quarantined &&
              sample.hasObserverIntegrityFlag(
                  PearlObserverIntegrityFlag::InvalidCandidate));

    PearlObserverSnapshot mismatch = sample;
    ++mismatch.liveness.jobGeneration;
    ++mismatch.publishedAtMs;
    check("cross-layer generation mismatch is rejected",
          !validationBus.publish(mismatch));

    OwnedObserverBus replacementBus;
    PearlObserverSnapshot oldJob = tagged(10);
    check("replacement fixture publishes", replacementBus.publish(oldJob));
    const PearlObserverSnapshot replacementRetired =
        retireActiveJob(oldJob);
    check("replacement retires prior active source ownership",
          replacementBus.publish(replacementRetired));
    PearlObserverSnapshot replacement = replacementRetired;
    replacement.activeJobToken = 900001;
    replacement.activeJobIdentity = digest(91);
    ++replacement.sourceEventsDrained;
    ++replacement.sourceQueueHighWater;
    ++replacement.lastSourceEventToken;
    ++replacement.lastSourceEventMs;
    ++replacement.publishedAtMs;
    stampSourceEvent(&replacement, PearlSourceEventKind::JobAvailable,
                     replacement.sourceSessionToken);
    replacement.lastSourceEvent.authorizationToken =
        replacement.sourceAuthorizationToken;
    replacement.lastSourceEvent.jobToken = replacement.activeJobToken;
    replacement.lastSourceEvent.jobIdentity = replacement.activeJobIdentity;
    check("active-new issued-old replacement publishes",
          replacementBus.publish(replacement));
    check("replacement retains both exact token/digest tuples",
          replacementBus.tryRead(&sample) &&
              sample.activeJobToken == replacement.activeJobToken &&
              sample.issuedJobToken == oldJob.issuedJobToken &&
              pearlObserverDigestEqual(sample.activeJobIdentity,
                                       replacement.activeJobIdentity) &&
              pearlObserverDigestEqual(sample.issuedJobIdentity,
                                       oldJob.issuedJobIdentity) &&
              sample.progress.jobGeneration == oldJob.progress.jobGeneration &&
              sample.liveness.jobGeneration == oldJob.liveness.jobGeneration);

    PearlObserverSnapshot quarantined = replacement;
    quarantined.sourceState = PearlObserverSourceState::Quarantined;
    quarantined.sourceIntegrityFlags = 1;
    ++quarantined.sourceEventsDrained;
    ++quarantined.sourceEventsInvalid;
    ++quarantined.publishedAtMs;
    check("quarantine publishes only diagnostic growth",
          replacementBus.publish(quarantined));
    OwnedObserverBus uncasedTeardownBus;
    check("uncased teardown trusted base publishes",
          uncasedTeardownBus.publish(replacement));
    PearlObserverSnapshot uncasedQuarantine = replacement;
    uncasedQuarantine.sourceState = PearlObserverSourceState::Quarantined;
    uncasedQuarantine.sourceIntegrityFlags = 1;
    ++uncasedQuarantine.sourceEventsInvalid;
    ++uncasedQuarantine.publishedAtMs;
    check("uncased teardown quarantine publishes",
          uncasedTeardownBus.publish(uncasedQuarantine));
    PearlObserverSnapshot uncasedTeardown = uncasedQuarantine;
    uncasedTeardown.physicalTransportConnected = false;
    uncasedTeardown.sourceResourcesReleased = true;
    ++uncasedTeardown.publishedAtMs;
    check("quarantine physical change requires teardown generation/cause",
          uncasedTeardown.invariantsHold() &&
              !uncasedTeardownBus.publish(uncasedTeardown));
    PearlObserverSnapshot tornDown = quarantined;
    tornDown.physicalTransportConnected = false;
    tornDown.sourceResourcesReleased = true;
    ++tornDown.physicalTeardownGeneration;
    tornDown.physicalTeardownCause =
        PearlPhysicalTeardownCause::QuarantineRelease;
    ++tornDown.publishedAtMs;
    check("quarantine can publish physical teardown without refreshing truth",
          replacementBus.publish(tornDown) &&
              replacementBus.tryRead(&sample) &&
              sample.sourceConnected && !sample.physicalTransportConnected &&
              sample.sourceResourcesReleased &&
              pearlObserverProgressEqual(sample.progress,
                                         quarantined.progress));
    quarantined = tornDown;
    PearlObserverSnapshot physicalReconnect = tornDown;
    physicalReconnect.physicalTransportConnected = true;
    physicalReconnect.sourceResourcesReleased = false;
    ++physicalReconnect.publishedAtMs;
    check("quarantine rejects physical reconnect and release reversal",
          physicalReconnect.invariantsHold() &&
              !replacementBus.publish(physicalReconnect) &&
              replacementBus.tryRead(&sample) &&
              !sample.physicalTransportConnected &&
              sample.sourceResourcesReleased);
    PearlObserverSnapshot illegalRecovery = tagged(11);
    illegalRecovery.sourceEventsDrained = quarantined.sourceEventsDrained;
    illegalRecovery.sourceEventsInvalid = quarantined.sourceEventsInvalid;
    illegalRecovery.sourceQueueHighWater = quarantined.sourceQueueHighWater;
    illegalRecovery.sourceIntegrityFlags = quarantined.sourceIntegrityFlags;
    illegalRecovery.sourceState = PearlObserverSourceState::Trusted;
    illegalRecovery.publishedAtMs = quarantined.publishedAtMs + 1;
    illegalRecovery.progress.publishedAtMs = illegalRecovery.publishedAtMs;
    illegalRecovery.lastSourceEventMs = illegalRecovery.publishedAtMs;
    check("quarantine cannot recover or refresh mining ownership",
          !replacementBus.publish(illegalRecovery));
    check("failed recovery preserves exact pre-quarantine mining payload",
          replacementBus.tryRead(&sample) &&
              sample.sourceState == PearlObserverSourceState::Quarantined &&
              pearlObserverProgressEqual(sample.progress,
                                         quarantined.progress) &&
              pearlObserverLivenessCoreEqual(sample.liveness,
                                             quarantined.liveness) &&
              sample.activeJobToken == quarantined.activeJobToken &&
              sample.issuedJobToken == quarantined.issuedJobToken &&
              sample.hasObserverIntegrityFlag(
                  PearlObserverIntegrityFlag::InvalidCandidate));

    PearlObserverBus pausedBus;
    MidpointPause pause;
    std::atomic<bool> midpointBasePublished{false};
    std::thread pausedWriter([&] {
        PearlObserverBus::Writer writer;
        pausedBus.acquireWriter(&writer);
        midpointBasePublished.store(
            writer.publish(withoutSourceCapability(tagged(1))),
                                    std::memory_order_release);
        writer.publishWithMidpointHookForTest(
            withoutSourceCapability(tagged(2)), pauseMidpoint, &pause);
    });
    bool entered = false;
    for (uint64_t spin = 0; spin < 2000000; ++spin) {
        if (pause.entered.load(std::memory_order_acquire)) {
            entered = true;
            break;
        }
        std::this_thread::yield();
    }
    PearlObserverSnapshot untouched = tagged(999);
    const bool acceptedOdd = entered && pausedBus.tryRead(&untouched, 64);
    pause.readerFinished.store(true, std::memory_order_release);
    pausedWriter.join();
    check("midpoint fixture publishes on its owning writer",
          midpointBasePublished.load(std::memory_order_acquire));
    check("writer midpoint was reached deterministically",
          entered && !pause.timedOut.load(std::memory_order_acquire));
    check("odd publication returns bounded failure without torn output",
          !acceptedOdd && coherentTag(untouched) &&
              untouched.progress.sessionGeneration == 999);
    check("completed midpoint publication is coherent",
          pausedBus.tryRead(&sample) && coherentBenchmarkTag(sample) &&
              sample.progress.sessionGeneration == 2);

    PearlObserverBus changedBus;
    SequenceRace race;
    race.replacement = withoutSourceCapability(tagged(3));
    std::atomic<bool> changedBasePublished{false};
    std::atomic<bool> changedBaseReady{false};
    std::thread racingWriter([&] {
        PearlObserverBus::Writer writer;
        changedBus.acquireWriter(&writer);
        changedBasePublished.store(
            writer.publish(withoutSourceCapability(tagged(2))),
                                   std::memory_order_release);
        changedBaseReady.store(true, std::memory_order_release);
        for (uint64_t spin = 0; spin < 2000000; ++spin) {
            if (race.start.load(std::memory_order_acquire)) {
                writer.publish(race.replacement);
                race.done.store(true, std::memory_order_release);
                return;
            }
            std::this_thread::yield();
        }
        race.timedOut.store(true, std::memory_order_release);
        race.done.store(true, std::memory_order_release);
    });
    while (!changedBaseReady.load(std::memory_order_acquire))
        std::this_thread::yield();
    untouched = tagged(999);
    const bool acceptedChanged =
        changedBus.tryReadWithAfterFirstSequenceHookForTest(
            &untouched, 1, replaceAfterFirstSequence, &race);
    racingWriter.join();
    check("changed-sequence fixture publishes on its owning writer",
          changedBasePublished.load(std::memory_order_acquire));
    check("changed-sequence race completed deterministically",
          !race.timedOut.load(std::memory_order_acquire));
    check("reader rejects a publication crossing two sequences",
          !acceptedChanged && untouched.progress.sessionGeneration == 999);
    check("replacement is readable after rejected cross-sequence attempt",
          changedBus.tryRead(&sample) && coherentBenchmarkTag(sample) &&
              sample.progress.sessionGeneration == 3);

    PearlObserverBus concurrentBus;
    constexpr unsigned kReaders = 4;
    std::atomic<unsigned> ready{0};
    std::atomic<bool> writerDone{false};
    std::atomic<bool> concurrencyBasePublished{false};
    std::atomic<bool> concurrencyBaseReady{false};
    std::array<std::atomic<uint64_t>, kReaders> coherentReads{};
    std::atomic<uint64_t> tornReads{0};
    for (auto &count : coherentReads)
        count.store(0, std::memory_order_relaxed);
    std::vector<std::thread> readers;
    for (unsigned reader = 0; reader < kReaders; ++reader) {
        readers.emplace_back([&, reader] {
            ready.fetch_add(1, std::memory_order_release);
            while (!concurrencyBaseReady.load(std::memory_order_acquire))
                std::this_thread::yield();
            while (!writerDone.load(std::memory_order_acquire)) {
                PearlObserverSnapshot observed;
                if (!concurrentBus.tryRead(&observed, 512)) continue;
                if (coherentBenchmarkTag(observed))
                    coherentReads[reader].fetch_add(1,
                                                    std::memory_order_relaxed);
                else
                    tornReads.fetch_add(1, std::memory_order_relaxed);
            }
            for (unsigned i = 0; i < 25; ++i) {
                PearlObserverSnapshot observed;
                if (concurrentBus.tryRead(&observed, 512) &&
                    coherentBenchmarkTag(observed))
                    coherentReads[reader].fetch_add(1,
                                                    std::memory_order_relaxed);
                else
                    tornReads.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    std::thread writer([&] {
        PearlObserverBus::Writer publisher;
        concurrentBus.acquireWriter(&publisher);
        while (ready.load(std::memory_order_acquire) != kReaders)
            std::this_thread::yield();
        concurrencyBasePublished.store(
            publisher.publish(withoutSourceCapability(tagged(1))),
                                       std::memory_order_release);
        concurrencyBaseReady.store(true, std::memory_order_release);
        for (uint64_t tag = 2; tag <= 4000; ++tag) {
            publisher.publish(withoutSourceCapability(tagged(tag)));
            if ((tag & 31) == 0) std::this_thread::yield();
        }
        writerDone.store(true, std::memory_order_release);
    });
    writer.join();
    check("concurrency fixture publishes on its owning writer",
          concurrencyBasePublished.load(std::memory_order_acquire));
    for (auto &reader : readers) reader.join();
    bool everyReaderObserved = true;
    for (auto &count : coherentReads)
        everyReaderObserved =
            everyReaderObserved && count.load(std::memory_order_relaxed) >= 25;
    check("every concurrent reader observes coherent envelopes",
          everyReaderObserved);
    check("concurrent readers never accept a mixed generation or digest",
          tornReads.load(std::memory_order_relaxed) == 0);

    PearlObserverBus ownerBus;
    auto owner = ownerBus.acquireWriter();
    check("first accepted publisher captures writer ownership",
          owner.valid() && owner.publish(tagged(1)));
    auto impersonator = ownerBus.acquireWriter();
    check("second capability is inert and freezes prior mining truth",
          !impersonator.valid() && !impersonator.publish(tagged(2)) &&
              ownerBus.tryRead(&sample) &&
              sample.sourceState == PearlObserverSourceState::Quarantined &&
              sample.progress.sessionGeneration == 1 &&
              sample.hasObserverIntegrityFlag(
                  PearlObserverIntegrityFlag::WriterAuthorityViolation));

    PearlObserverBus affinityBus;
    auto affinityOwner = affinityBus.acquireWriter();
    check("thread-affinity base publishes on acquiring thread",
          affinityOwner.publish(tagged(1)));
    std::atomic<bool> crossThreadAccepted{true};
    std::thread foreignThread([&] {
        crossThreadAccepted.store(
            affinityOwner.publish(tagged(2)),
            std::memory_order_release);
    });
    foreignThread.join();
    check("writer capability rejects a foreign thread identity",
          !crossThreadAccepted.load(std::memory_order_acquire));
    check("foreign-thread rejection latches coherent prior truth",
          affinityBus.tryRead(&sample) &&
              sample.sourceState == PearlObserverSourceState::Quarantined &&
              sample.progress.sessionGeneration == 1 &&
              sample.hasObserverIntegrityFlag(
                  PearlObserverIntegrityFlag::WriterAuthorityViolation));

    PearlObserverBus reentryBus;
    auto reentryWriter = reentryBus.acquireWriter();
    check("reentry base publishes",
          reentryWriter.publish(tagged(1)));
    ReentryAttempt reentry;
    reentry.writer = &reentryWriter;
    reentry.nested = tagged(3);
    const bool outerReentryAccepted =
        reentryWriter.publishWithMidpointHookForTest(
            tagged(2), attemptReentry, &reentry);
    check("nested midpoint publication is rejected", !reentry.accepted);
    check("midpoint reentry rejects its outer publication",
          !outerReentryAccepted);
    check("midpoint reentry cannot strand an odd sequence",
          reentryBus.sequence() % 2 == 0);
    check("midpoint reentry latches coherent prior truth",
          reentryBus.tryRead(&sample) &&
              sample.sourceState == PearlObserverSourceState::Quarantined &&
              sample.progress.sessionGeneration == 1 &&
              sample.hasObserverIntegrityFlag(
                  PearlObserverIntegrityFlag::ReentrantWriter));

    PearlObserverBus firstCrossBus;
    PearlObserverBus secondCrossBus;
    auto firstCrossWriter = firstCrossBus.acquireWriter();
    auto secondCrossWriter = secondCrossBus.acquireWriter();
    check("cross-bus reentry fixtures publish",
          firstCrossWriter.publish(withoutSourceCapability(tagged(1))) &&
              secondCrossWriter.publish(withoutSourceCapability(tagged(1))));
    CrossBusReentryAttempt crossBusReentry;
    crossBusReentry.first = &firstCrossWriter;
    crossBusReentry.second = &secondCrossWriter;
    crossBusReentry.firstNested = withoutSourceCapability(tagged(3));
    crossBusReentry.secondOuter = withoutSourceCapability(tagged(2));
    const bool firstCrossOuterAccepted =
        firstCrossWriter.publishWithMidpointHookForTest(
            withoutSourceCapability(tagged(2)),
            routeReentryThroughSecondBus, &crossBusReentry);
    check("A-to-B-to-A nested publication rejects the active first bus",
          !crossBusReentry.firstNestedAccepted &&
              crossBusReentry.secondOuterAccepted &&
              !firstCrossOuterAccepted);
    check("cross-bus reentry leaves both sequences stable and even",
          firstCrossBus.sequence() % 2 == 0 &&
              secondCrossBus.sequence() % 2 == 0 &&
              firstCrossBus.tryRead(&sample) &&
              sample.sourceState == PearlObserverSourceState::Quarantined &&
              sample.hasObserverIntegrityFlag(
                  PearlObserverIntegrityFlag::ReentrantWriter));

    PearlObserverBus competingBus;
    std::atomic<bool> releasePublishers{false};
    std::atomic<unsigned> publisherReady{0};
    std::atomic<unsigned> acceptedPublishers{0};
    std::atomic<bool> owningPublisherDone{false};
    auto competingPublish = [&](uint64_t tag) {
        PearlObserverBus::Writer candidate;
        competingBus.acquireWriter(&candidate);
        publisherReady.fetch_add(1, std::memory_order_release);
        while (!releasePublishers.load(std::memory_order_acquire))
            std::this_thread::yield();
        if (!candidate.valid()) {
            while (!owningPublisherDone.load(std::memory_order_acquire))
                std::this_thread::yield();
            candidate.publish(tagged(tag));
            return;
        }
        if (candidate.publish(tagged(tag)))
            acceptedPublishers.fetch_add(1, std::memory_order_relaxed);
        owningPublisherDone.store(true, std::memory_order_release);
    };
    std::thread publisherOne(competingPublish, 1);
    std::thread publisherTwo(competingPublish, 2);
    while (publisherReady.load(std::memory_order_acquire) != 2)
        std::this_thread::yield();
    releasePublishers.store(true, std::memory_order_release);
    publisherOne.join();
    publisherTwo.join();
    check("concurrent first publishers serialize to one owner",
          acceptedPublishers.load(std::memory_order_relaxed) == 1 &&
              competingBus.tryRead(&sample) &&
              sample.sourceState == PearlObserverSourceState::Quarantined &&
              (sample.progress.sessionGeneration == 1 ||
               sample.progress.sessionGeneration == 2) &&
              sample.hasObserverIntegrityFlag(
                  PearlObserverIntegrityFlag::WriterAuthorityViolation));

    PearlObserverBus deadOwnerBus;
    {
        auto mortalOwner = deadOwnerBus.acquireWriter();
        check("publisher-death fixture owns and publishes",
              mortalOwner.valid() && mortalOwner.publish(tagged(1)));
    }
    auto reusedAddressOwner = deadOwnerBus.acquireWriter();
    check("publisher death never reissues an impersonable capability",
          !reusedAddressOwner.valid() &&
              !reusedAddressOwner.publish(tagged(2)) &&
              deadOwnerBus.tryRead(&sample) &&
              sample.progress.sessionGeneration == 1 &&
              sample.hasObserverIntegrityFlag(
                  PearlObserverIntegrityFlag::WriterAuthorityViolation));

    alignas(PearlObserverBus)
        unsigned char reusedBusStorage[sizeof(PearlObserverBus)];
    PearlObserverBus *firstAtAddress =
        new (reusedBusStorage) PearlObserverBus();
    auto staleAddressWriter = firstAtAddress->acquireWriter();
    check("stale-address fixture first bus publishes",
          staleAddressWriter.publish(withoutSourceCapability(tagged(1))));
    firstAtAddress->~PearlObserverBus();
    PearlObserverBus *replacementAtAddress =
        new (reusedBusStorage) PearlObserverBus();
    auto replacementAddressWriter = replacementAtAddress->acquireWriter();
    check("fresh bus at reused address gets independent authority",
          replacementAddressWriter.valid());
    check("stale writer cannot impersonate a replacement bus at same address",
          !staleAddressWriter.publish(withoutSourceCapability(tagged(2))) &&
              replacementAtAddress->tryRead(&sample) &&
              sample.sourceState == PearlObserverSourceState::Quarantined &&
              sample.hasObserverIntegrityFlag(
                  PearlObserverIntegrityFlag::WriterAuthorityViolation));
    replacementAtAddress->~PearlObserverBus();

    const uint64_t max = std::numeric_limits<uint64_t>::max();
    OwnedObserverBus exhaustionBus(max - 1);
    check("last sequence pair is reserved for the exhaustion latch",
          !exhaustionBus.publish(tagged(1)) &&
              exhaustionBus.sequence() == max - 1);
    check("sequence exhaustion is coherent and cannot wrap",
          exhaustionBus.tryRead(&sample) &&
              sample.hasObserverIntegrityFlag(
                  PearlObserverIntegrityFlag::SequenceExhausted) &&
              !exhaustionBus.publish(tagged(2)) &&
              exhaustionBus.sequence() == max - 1);

    sample = tagged(999);
    check("zero-attempt read is bounded and leaves output untouched",
          !concurrentBus.tryRead(&sample, 0) && coherentTag(sample) &&
              sample.progress.sessionGeneration == 999);

    if (failures) {
        std::fprintf(stderr, "FAIL: %d Pearl observer checks failed\n",
                     failures);
        return 1;
    }
    std::puts("PASS: Pearl observer envelope is coherent and bounded");
    return 0;
}
