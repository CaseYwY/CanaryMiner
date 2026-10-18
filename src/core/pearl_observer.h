// Coherent publication of Pearl progress, liveness, and source ownership.
//
// This is a bounded, pure-host bridge. It owns no clock, source, thread, CUDA
// object, or watchdog policy. One coordinator constructs and validates complete
// snapshots; any number of observers may take bounded coherent reads.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <type_traits>

#include "pearl_progress.h"
#include "pearl_source_observability.h"

namespace om {

constexpr uint32_t kPearlObserverSchemaVersion = 1;

enum class PearlObserverSourceState : uint8_t {
    Unsupported = 0,
    Trusted = 1,
    Quarantined = 2,
};

enum class PearlObserverIntegrityFlag : uint64_t {
    None = 0,
    InvalidCandidate = 1ULL << 0,
    SequenceExhausted = 1ULL << 1,
    WriterAuthorityViolation = 1ULL << 2,
    ReentrantWriter = 1ULL << 3,
};

enum class PearlIssuedBindingCause : uint8_t {
    None = 0,
    SameJobRebind = 1,
    DifferentJobAdoption = 2,
    Released = 3,
};

enum class PearlPhysicalTeardownCause : uint8_t {
    None = 0,
    SourceDisconnected = 1,
    QuarantineRelease = 2,
};

inline uint64_t pearlObserverIntegrityBit(PearlObserverIntegrityFlag flag) {
    return static_cast<uint64_t>(flag);
}

inline uint64_t pearlObserverAcquireUniqueId(
    std::atomic<uint64_t> *counter) noexcept {
    if (!counter) return 0;
    uint64_t current = counter->load(std::memory_order_relaxed);
    for (;;) {
        if (current == 0 ||
            current == std::numeric_limits<uint64_t>::max())
            return 0;
        if (counter->compare_exchange_weak(
                current, current + 1, std::memory_order_relaxed,
                std::memory_order_relaxed))
            return current;
    }
}

inline std::atomic<uint64_t> pearlObserverNextBusId{1};
inline std::atomic<uint64_t> pearlObserverNextThreadId{1};

#if defined(__GNUC__) && !defined(__clang__)
__attribute__((noinline, noipa))
#endif
inline uint64_t pearlObserverCurrentThreadId() noexcept {
    thread_local uint64_t id = 0;
    if (id == 0)
        id = pearlObserverAcquireUniqueId(&pearlObserverNextThreadId);
    return id;
}

inline bool pearlObserverDigestZero(const PearlJobIdentityDigest &digest) {
    for (uint8_t byte : digest.bytes)
        if (byte != 0) return false;
    return true;
}

inline bool pearlObserverDigestEqual(const PearlJobIdentityDigest &a,
                                     const PearlJobIdentityDigest &b) {
    return a.bytes == b.bytes;
}

inline bool pearlObserverSourceEventEqual(const PearlSourceEvent &a,
                                          const PearlSourceEvent &b) {
    return a.eventToken == b.eventToken &&
           a.timestampMs == b.timestampMs &&
           a.sessionToken == b.sessionToken &&
           a.authorizationToken == b.authorizationToken &&
           a.jobToken == b.jobToken &&
           a.operationToken == b.operationToken &&
           pearlObserverDigestEqual(a.jobIdentity, b.jobIdentity) &&
           a.kind == b.kind && a.delivery == b.delivery &&
           a.terminal == b.terminal && a.reason == b.reason &&
           a.terminalEvidence == b.terminalEvidence &&
           a.acknowledged == b.acknowledged &&
           a.superseded == b.superseded && a.reserved1 == b.reserved1;
}

inline bool pearlObserverSourceEventShapeHolds(
    const PearlSourceEvent &event) {
    if (static_cast<unsigned>(event.kind) >
            static_cast<unsigned>(PearlSourceEventKind::SourceFault) ||
        static_cast<unsigned>(event.delivery) >
            static_cast<unsigned>(PearlSourceDelivery::Sent) ||
        static_cast<unsigned>(event.terminal) >
            static_cast<unsigned>(PearlSourceTerminal::GatewayQueued) ||
        static_cast<unsigned>(event.reason) >
            static_cast<unsigned>(PearlSourceReason::Shutdown) ||
        static_cast<unsigned>(event.terminalEvidence) >
            static_cast<unsigned>(
                PearlSourceTerminalEvidence::CorrelatedRemoteOutcome) ||
        event.acknowledged > 1 || event.superseded > 1 ||
        event.reserved1 != 0)
        return false;
    if (event.kind == PearlSourceEventKind::None)
        return event.eventToken == 0 && event.timestampMs == 0 &&
               event.sessionToken == 0 && event.authorizationToken == 0 &&
               event.jobToken == 0 && event.operationToken == 0 &&
               pearlObserverDigestZero(event.jobIdentity) &&
               event.delivery == PearlSourceDelivery::None &&
               event.terminal == PearlSourceTerminal::None &&
               event.reason == PearlSourceReason::None &&
               event.terminalEvidence ==
                   PearlSourceTerminalEvidence::None &&
               event.acknowledged == 0 && event.superseded == 0;
    if (event.eventToken == 0 || event.sessionToken == 0) return false;
    if (event.kind == PearlSourceEventKind::SubmitTerminal)
        return event.jobToken != 0 && event.operationToken != 0 &&
               !pearlObserverDigestZero(event.jobIdentity) &&
               pearlSourceTerminalFactValid(
                   event.delivery, event.terminal,
                   event.acknowledged != 0, event.superseded != 0,
                   event.reason, event.terminalEvidence);
    if (event.kind == PearlSourceEventKind::SubmitDispatched)
        return event.delivery == PearlSourceDelivery::NotSent &&
               event.jobToken != 0 && event.operationToken != 0 &&
               !pearlObserverDigestZero(event.jobIdentity) &&
               event.terminal == PearlSourceTerminal::None &&
               event.reason == PearlSourceReason::None &&
               event.terminalEvidence ==
                   PearlSourceTerminalEvidence::None &&
               event.acknowledged == 0 && event.superseded == 0;
    if (event.delivery != PearlSourceDelivery::None ||
        event.terminal != PearlSourceTerminal::None ||
        event.terminalEvidence != PearlSourceTerminalEvidence::None ||
        event.acknowledged != 0 || event.superseded != 0)
        return false;
    switch (event.kind) {
        case PearlSourceEventKind::SessionStarted:
        case PearlSourceEventKind::TransportConnected:
        case PearlSourceEventKind::SessionReadyNoAuth:
            return event.authorizationToken == 0 && event.jobToken == 0 &&
                   event.operationToken == 0 &&
                   pearlObserverDigestZero(event.jobIdentity) &&
                   event.reason == PearlSourceReason::None;
        case PearlSourceEventKind::AuthorizationSucceeded:
            return event.authorizationToken != 0 && event.jobToken == 0 &&
                   event.operationToken == 0 &&
                   pearlObserverDigestZero(event.jobIdentity) &&
                   event.reason == PearlSourceReason::None;
        case PearlSourceEventKind::JobRetainedPreAuth:
            return event.authorizationToken == 0 && event.jobToken != 0 &&
                   event.operationToken == 0 &&
                   !pearlObserverDigestZero(event.jobIdentity) &&
                   event.reason == PearlSourceReason::None;
        case PearlSourceEventKind::JobAvailable:
            return event.jobToken != 0 && event.operationToken == 0 &&
                   !pearlObserverDigestZero(event.jobIdentity) &&
                   event.reason == PearlSourceReason::None;
        case PearlSourceEventKind::JobRetired:
            return event.jobToken != 0 && event.operationToken == 0 &&
                   !pearlObserverDigestZero(event.jobIdentity) &&
                   event.reason != PearlSourceReason::None;
        case PearlSourceEventKind::Disconnected:
            return event.jobToken == 0 && event.operationToken == 0 &&
                   pearlObserverDigestZero(event.jobIdentity) &&
                   event.reason != PearlSourceReason::None;
        case PearlSourceEventKind::SourceFault:
            return event.operationToken == 0 &&
                   ((event.jobToken == 0) ==
                    pearlObserverDigestZero(event.jobIdentity)) &&
                   event.reason != PearlSourceReason::None;
        case PearlSourceEventKind::None:
        case PearlSourceEventKind::SubmitDispatched:
        case PearlSourceEventKind::SubmitTerminal:
            break;
    }
    return false;
}

inline bool pearlObserverDeadlineShapeHolds(const PearlDeadline &deadline) {
    if (static_cast<unsigned>(deadline.state) >
        static_cast<unsigned>(PearlDeadlineState::Expired))
        return false;
    if (deadline.state == PearlDeadlineState::NotApplicable ||
        deadline.state == PearlDeadlineState::Disabled)
        return deadline.atMs == 0;
    return deadline.atMs != 0;
}

inline bool pearlObserverLivenessShapeHolds(
    const PearlLivenessSnapshot &snapshot) {
    if (static_cast<unsigned>(snapshot.connection) >
            static_cast<unsigned>(PearlConnectionState::Ready) ||
        static_cast<unsigned>(snapshot.jobProgress) >
            static_cast<unsigned>(PearlJobProgressState::Active) ||
        static_cast<unsigned>(snapshot.candidate) >
            static_cast<unsigned>(PearlCandidateState::AwaitingOutcome) ||
        static_cast<unsigned>(snapshot.lastOutcome) >
            static_cast<unsigned>(PearlSubmitOutcome::Unknown) ||
        static_cast<unsigned>(snapshot.lastOutcomeEvidence) >
            static_cast<unsigned>(
                PearlOutcomeEvidence::PossiblyDeliveredUnknown) ||
        !snapshot.queryTimeValid || !snapshot.submissionAccountingHolds() ||
        !snapshot.outcomeEvidenceHolds() ||
        !pearlObserverDeadlineShapeHolds(snapshot.connectionDeadline) ||
        !pearlObserverDeadlineShapeHolds(snapshot.attemptDeadline) ||
        !pearlObserverDeadlineShapeHolds(snapshot.candidateDeadline) ||
        !pearlObserverDeadlineShapeHolds(snapshot.outcomeFreshness))
        return false;

    if (snapshot.completedAttemptsForJob > snapshot.completedAttempts ||
        snapshot.candidatesFinalized > snapshot.candidatesObserved ||
        snapshot.candidatesInvalid > snapshot.candidatesFinalized)
        return false;

    switch (snapshot.connection) {
        case PearlConnectionState::AwaitingAuthorization:
        case PearlConnectionState::AwaitingJob:
        case PearlConnectionState::Ready:
            if (snapshot.sessionGeneration == 0) return false;
            break;
        case PearlConnectionState::Disconnected:
            break;
    }
    if (snapshot.jobProgress != PearlJobProgressState::NoJob &&
        (snapshot.connection != PearlConnectionState::Ready ||
         snapshot.jobGeneration == 0))
        return false;
    if (snapshot.candidate != PearlCandidateState::Idle &&
        snapshot.candidateGeneration == 0)
        return false;
    if ((snapshot.candidate ==
             PearlCandidateState::AwaitingAcknowledgement ||
         snapshot.candidate == PearlCandidateState::AwaitingOutcome) &&
        snapshot.submitGeneration == 0)
        return false;
    return true;
}

inline bool pearlObserverDeadlineAtPublicationHolds(
    const PearlDeadline &deadline, bool applicable, uint64_t publishedAtMs) {
    if (!applicable)
        return deadline.state == PearlDeadlineState::NotApplicable &&
               deadline.atMs == 0;
    if (deadline.state == PearlDeadlineState::Disabled)
        return deadline.atMs == 0;
    if (deadline.state == PearlDeadlineState::Fresh)
        return deadline.atMs != 0 && publishedAtMs < deadline.atMs;
    if (deadline.state == PearlDeadlineState::Expired)
        return deadline.atMs != 0 && publishedAtMs >= deadline.atMs;
    return false;
}

struct PearlObserverSnapshot {
    uint32_t schemaVersion = kPearlObserverSchemaVersion;
    PearlProgressSnapshot progress;
    PearlLivenessSnapshot liveness;

    PearlObserverSourceState sourceState =
        PearlObserverSourceState::Unsupported;
    bool sourceConnected = false;
    bool sourceReady = false;
    bool sourceFaultObserved = false;
    // Physical teardown is distinct from the frozen last-trusted logical
    // state. After quarantine it may only move connected -> disconnected and
    // unreleased -> released.
    bool physicalTransportConnected = false;
    bool sourceResourcesReleased = false;
    uint64_t sourceIntegrityFlags = 0;
    uint64_t sourceMailboxFaultBits = 0;
    uint64_t observerIntegrityFlags = 0;

    uint64_t sourceEventsDrained = 0;
    uint64_t sourceEventsInvalid = 0;
    uint64_t sourceRefusedEvents = 0;
    uint64_t sourceFaultEvents = 0;
    uint64_t sourceQueueHighWater = 0;
    uint64_t lastSourceEventToken = 0;
    uint64_t lastSourceEventMs = 0;
    // Exact last event is retained whenever source history is nonempty.
    PearlSourceEvent lastSourceEvent;

    uint64_t sourceSessionToken = 0;
    uint64_t sourceAuthorizationToken = 0;
    uint64_t activeJobToken = 0;
    uint64_t issuedSessionToken = 0;
    uint64_t issuedAuthorizationToken = 0;
    uint64_t issuedJobToken = 0;
    uint64_t issuedBindingGeneration = 0;
    PearlIssuedBindingCause issuedBindingCause =
        PearlIssuedBindingCause::None;
    uint64_t inFlightOperationToken = 0;
    uint64_t inFlightSessionToken = 0;
    uint64_t inFlightAuthorizationToken = 0;
    uint64_t inFlightJobToken = 0;
    PearlJobIdentityDigest activeJobIdentity;
    PearlJobIdentityDigest issuedJobIdentity;
    // Exact identity retained across the eventless released interval so the
    // next bind can prove SameJobRebind versus DifferentJobAdoption.
    PearlJobIdentityDigest lastReleasedIssuedJobIdentity;
    PearlJobIdentityDigest inFlightJobIdentity;

    bool unresolvedSubmit = false;
    bool sourceDispatchObserved = false;
    uint64_t unresolvedSubmitGeneration = 0;
    uint64_t physicalTeardownGeneration = 0;
    PearlPhysicalTeardownCause physicalTeardownCause =
        PearlPhysicalTeardownCause::None;
    uint64_t publishedAtMs = 0;

    bool hasObserverIntegrityFlag(PearlObserverIntegrityFlag flag) const {
        return (observerIntegrityFlags & pearlObserverIntegrityBit(flag)) != 0;
    }

    bool submissionReconciliationHolds() const {
        if (progress.submitAttempts != liveness.submitAttempts ||
            progress.submissionFailures != liveness.submissionFailures ||
            progress.acknowledged != liveness.acknowledgements ||
            liveness.acknowledgements >
                std::numeric_limits<uint64_t>::max() -
                    liveness.unacknowledgedUnknowns ||
            progress.submitted != liveness.acknowledgements +
                                      liveness.unacknowledgedUnknowns ||
            progress.unknown < liveness.unacknowledgedUnknowns)
            return false;

        uint64_t livenessTerminals = liveness.outcomes;
        if (liveness.submissionFailures > livenessTerminals) return false;
        livenessTerminals -= liveness.submissionFailures;
        if (liveness.possiblyDeliveredUnknowns > livenessTerminals)
            return false;
        livenessTerminals -= liveness.possiblyDeliveredUnknowns;
        if (progress.submitted - progress.pending != livenessTerminals)
            return false;

        uint64_t unclassified = liveness.submitAttempts;
        if (liveness.submissionFailures > unclassified) return false;
        unclassified -= liveness.submissionFailures;
        if (liveness.possiblyDeliveredUnknowns > unclassified) return false;
        unclassified -= liveness.possiblyDeliveredUnknowns;
        if (liveness.unacknowledgedUnknowns > unclassified) return false;
        unclassified -= liveness.unacknowledgedUnknowns;
        if (liveness.acknowledgements > unclassified) return false;
        unclassified -= liveness.acknowledgements;
        const uint64_t expectedUnclassified =
            liveness.candidate ==
                    PearlCandidateState::AwaitingAcknowledgement
                ? 1
                : 0;
        if (unclassified != expectedUnclassified) return false;

        uint64_t acknowledgedTerminals = liveness.outcomes;
        if (liveness.submissionFailures > acknowledgedTerminals) return false;
        acknowledgedTerminals -= liveness.submissionFailures;
        if (liveness.possiblyDeliveredUnknowns > acknowledgedTerminals)
            return false;
        acknowledgedTerminals -= liveness.possiblyDeliveredUnknowns;
        if (liveness.unacknowledgedUnknowns > acknowledgedTerminals)
            return false;
        acknowledgedTerminals -= liveness.unacknowledgedUnknowns;
        if (acknowledgedTerminals > liveness.acknowledgements) return false;
        const uint64_t acknowledgedPending =
            liveness.acknowledgements - acknowledgedTerminals;
        const uint64_t expectedAcknowledgedPending =
            liveness.candidate == PearlCandidateState::AwaitingOutcome ? 1
                                                                       : 0;
        return acknowledgedPending == expectedAcknowledgedPending;
    }

    bool lifecycleCompatibilityHolds() const {
        switch (progress.phase) {
            case PearlProgressPhase::Idle:
                if (liveness.connection !=
                    PearlConnectionState::Disconnected)
                    return false;
                break;
            case PearlProgressPhase::Connecting:
            case PearlProgressPhase::AwaitingAuthorization:
                if (liveness.connection !=
                    PearlConnectionState::AwaitingAuthorization)
                    return false;
                break;
            case PearlProgressPhase::AwaitingJob:
                if (liveness.connection != PearlConnectionState::AwaitingJob)
                    return false;
                break;
            case PearlProgressPhase::Preparing:
            case PearlProgressPhase::Searching:
                if (liveness.connection != PearlConnectionState::Ready ||
                    liveness.candidate != PearlCandidateState::Idle)
                    return false;
                break;
            case PearlProgressPhase::Finalizing:
                if (liveness.connection != PearlConnectionState::Ready ||
                    liveness.candidate != PearlCandidateState::Finalizing)
                    return false;
                break;
            case PearlProgressPhase::AwaitingSubmit:
                if (liveness.connection != PearlConnectionState::Ready ||
                    liveness.candidate != PearlCandidateState::AwaitingSubmit)
                    return false;
                break;
            case PearlProgressPhase::Submitting:
            case PearlProgressPhase::AwaitingAcknowledgement:
                if (liveness.connection != PearlConnectionState::Ready ||
                    liveness.candidate !=
                        PearlCandidateState::AwaitingAcknowledgement)
                    return false;
                break;
            case PearlProgressPhase::AwaitingOutcome:
                if (liveness.connection != PearlConnectionState::Ready ||
                    liveness.candidate != PearlCandidateState::AwaitingOutcome)
                    return false;
                break;
            case PearlProgressPhase::Stopped:
                if (liveness.connection !=
                        PearlConnectionState::Disconnected ||
                    unresolvedSubmit)
                    return false;
                break;
            case PearlProgressPhase::Stopping:
            case PearlProgressPhase::Failed:
                break;
        }

        const bool liveUnresolved =
            liveness.candidate ==
                PearlCandidateState::AwaitingAcknowledgement ||
            liveness.candidate == PearlCandidateState::AwaitingOutcome;
        if (unresolvedSubmit != liveUnresolved) return false;

        if (sourceState == PearlObserverSourceState::Trusted) {
            switch (liveness.connection) {
                case PearlConnectionState::Disconnected:
                    if (sourceSessionToken != 0 || sourceConnected ||
                        sourceReady)
                        return false;
                    break;
                case PearlConnectionState::AwaitingAuthorization:
                    if (sourceSessionToken == 0 || sourceReady) return false;
                    break;
                case PearlConnectionState::AwaitingJob:
                case PearlConnectionState::Ready:
                    if (sourceSessionToken == 0 || !sourceConnected ||
                        !sourceReady)
                        return false;
                    break;
            }
        }
        if (sourceState == PearlObserverSourceState::Trusted &&
            unresolvedSubmit &&
            progress.phase != PearlProgressPhase::Submitting &&
            !sourceDispatchObserved)
            return false;
        return true;
    }

    bool deadlineBindingsHold() const {
        const bool connectionApplicable =
            liveness.connection ==
                PearlConnectionState::AwaitingAuthorization ||
            liveness.connection == PearlConnectionState::AwaitingJob;
        const bool attemptApplicable =
            liveness.jobProgress != PearlJobProgressState::NoJob;
        const bool candidateApplicable =
            liveness.candidate != PearlCandidateState::Idle;
        const bool outcomeApplicable =
            liveness.lastOutcome != PearlSubmitOutcome::None;
        return pearlObserverDeadlineAtPublicationHolds(
                   liveness.connectionDeadline, connectionApplicable,
                   publishedAtMs) &&
               pearlObserverDeadlineAtPublicationHolds(
                   liveness.attemptDeadline, attemptApplicable,
                   publishedAtMs) &&
               pearlObserverDeadlineAtPublicationHolds(
                   liveness.candidateDeadline, candidateApplicable,
                   publishedAtMs) &&
               pearlObserverDeadlineAtPublicationHolds(
                   liveness.outcomeFreshness, outcomeApplicable,
                   publishedAtMs);
    }

    bool invariantsHold() const {
        if (schemaVersion != kPearlObserverSchemaVersion ||
            static_cast<unsigned>(sourceState) >
                static_cast<unsigned>(
                    PearlObserverSourceState::Quarantined) ||
            static_cast<unsigned>(issuedBindingCause) >
                static_cast<unsigned>(PearlIssuedBindingCause::Released) ||
            static_cast<unsigned>(physicalTeardownCause) >
                static_cast<unsigned>(
                    PearlPhysicalTeardownCause::QuarantineRelease) ||
            !progress.invariantsHold() ||
            !pearlObserverLivenessShapeHolds(liveness) ||
            !submissionReconciliationHolds() ||
            !lifecycleCompatibilityHolds() || !deadlineBindingsHold())
            return false;

        if (progress.sessionGeneration != liveness.sessionGeneration ||
            progress.jobGeneration != liveness.jobGeneration ||
            progress.candidateGeneration != liveness.candidateGeneration ||
            progress.submitGeneration != liveness.submitGeneration ||
            progress.lastOutcome != liveness.lastOutcome ||
            progress.publishedAtMs > publishedAtMs ||
            lastSourceEventMs > publishedAtMs || progress.pending > 1)
            return false;

        if (sourceReady && !sourceConnected) return false;
        if (sourceResourcesReleased && physicalTransportConnected) return false;
        if (!pearlObserverSourceEventShapeHolds(lastSourceEvent)) return false;
        if ((lastSourceEventToken == 0) !=
                (lastSourceEvent.kind == PearlSourceEventKind::None) ||
            (lastSourceEventToken == 0 && lastSourceEventMs != 0) ||
            (lastSourceEventToken != 0 && sourceEventsDrained == 0) ||
            (lastSourceEvent.kind != PearlSourceEventKind::None &&
             (lastSourceEvent.eventToken != lastSourceEventToken ||
              lastSourceEvent.timestampMs != lastSourceEventMs)))
            return false;
        if ((issuedBindingGeneration == 0) !=
            (issuedBindingCause == PearlIssuedBindingCause::None))
            return false;
        if ((physicalTeardownGeneration == 0) !=
            (physicalTeardownCause == PearlPhysicalTeardownCause::None))
            return false;
        if (sourceFaultObserved &&
            (sourceFaultEvents == 0 || sourceSessionToken == 0))
            return false;
        if (sourceConnected && sourceSessionToken == 0) return false;
        if (sourceAuthorizationToken != 0 && sourceSessionToken == 0)
            return false;
        if (activeJobToken != 0 &&
            (sourceSessionToken == 0 || !sourceReady))
            return false;
        if (issuedJobToken == 0) {
            if (issuedSessionToken != 0 || issuedAuthorizationToken != 0 ||
                (issuedBindingGeneration != 0 &&
                 issuedBindingCause != PearlIssuedBindingCause::Released) ||
                (issuedBindingGeneration == 0 &&
                 !pearlObserverDigestZero(
                     lastReleasedIssuedJobIdentity)) ||
                (issuedBindingGeneration != 0 &&
                 pearlObserverDigestZero(lastReleasedIssuedJobIdentity)))
                return false;
        } else if (!sourceReady || issuedSessionToken != sourceSessionToken ||
                   issuedAuthorizationToken != sourceAuthorizationToken ||
                   (issuedBindingCause !=
                        PearlIssuedBindingCause::SameJobRebind &&
                    issuedBindingCause !=
                        PearlIssuedBindingCause::DifferentJobAdoption) ||
                   !pearlObserverDigestZero(
                       lastReleasedIssuedJobIdentity)) {
            return false;
        }
        if ((inFlightOperationToken != 0 ||
             (sourceState == PearlObserverSourceState::Trusted &&
              unresolvedSubmit)) &&
            issuedJobToken == 0)
            return false;
        if ((activeJobToken == 0) !=
                pearlObserverDigestZero(activeJobIdentity) ||
            (issuedJobToken == 0) !=
                pearlObserverDigestZero(issuedJobIdentity) ||
            (inFlightOperationToken == 0) !=
                pearlObserverDigestZero(inFlightJobIdentity))
            return false;
        if (activeJobToken != 0 && activeJobToken == issuedJobToken &&
            !pearlObserverDigestEqual(activeJobIdentity, issuedJobIdentity))
            return false;
        if (inFlightOperationToken == 0) {
            if (inFlightSessionToken != 0 ||
                inFlightAuthorizationToken != 0 || inFlightJobToken != 0)
                return false;
        } else if (inFlightSessionToken != sourceSessionToken ||
                   inFlightAuthorizationToken != sourceAuthorizationToken ||
                   inFlightJobToken != issuedJobToken ||
                   !pearlObserverDigestEqual(inFlightJobIdentity,
                                            issuedJobIdentity)) {
            return false;
        }

        if (unresolvedSubmit) {
            if (unresolvedSubmitGeneration == 0 ||
                unresolvedSubmitGeneration != progress.submitGeneration ||
                unresolvedSubmitGeneration != liveness.submitGeneration ||
                (liveness.candidate !=
                     PearlCandidateState::AwaitingAcknowledgement &&
                 liveness.candidate !=
                     PearlCandidateState::AwaitingOutcome))
                return false;
        } else if (unresolvedSubmitGeneration != 0 ||
                   sourceDispatchObserved || inFlightOperationToken != 0) {
            return false;
        }
        if (sourceDispatchObserved != (inFlightOperationToken != 0))
            return false;

        const bool hasSourceClaim =
            sourceConnected || sourceReady || sourceFaultObserved ||
            physicalTransportConnected || sourceResourcesReleased ||
            sourceIntegrityFlags != 0 || sourceMailboxFaultBits != 0 ||
            sourceEventsDrained != 0 || sourceEventsInvalid != 0 ||
            sourceRefusedEvents != 0 || sourceFaultEvents != 0 ||
            sourceQueueHighWater != 0 || lastSourceEventToken != 0 ||
            lastSourceEventMs != 0 ||
            lastSourceEvent.kind != PearlSourceEventKind::None ||
            sourceSessionToken != 0 ||
            sourceAuthorizationToken != 0 || activeJobToken != 0 ||
            issuedSessionToken != 0 || issuedAuthorizationToken != 0 ||
            issuedJobToken != 0 || issuedBindingGeneration != 0 ||
            inFlightOperationToken != 0 ||
            inFlightSessionToken != 0 ||
            inFlightAuthorizationToken != 0 || inFlightJobToken != 0 ||
            !pearlObserverDigestZero(activeJobIdentity) ||
            !pearlObserverDigestZero(issuedJobIdentity) ||
            !pearlObserverDigestZero(lastReleasedIssuedJobIdentity) ||
            !pearlObserverDigestZero(inFlightJobIdentity) ||
            sourceDispatchObserved || physicalTeardownGeneration != 0;
        if (sourceState == PearlObserverSourceState::Unsupported)
            return !hasSourceClaim;
        if (sourceState == PearlObserverSourceState::Trusted) {
            if (sourceIntegrityFlags != 0 || sourceMailboxFaultBits != 0)
                return false;
            if (physicalTransportConnected != sourceConnected) return false;
            if (sourceResourcesReleased && sourceSessionToken != 0)
                return false;
            // A cleanly disconnected but still instrumented source remains
            // Trusted so its bounded history is not erased or mislabeled as
            // capability-Unsupported. Only current ownership is absent.
            if (sourceSessionToken == 0)
                return !sourceConnected && !sourceReady &&
                       !sourceFaultObserved && sourceAuthorizationToken == 0 &&
                       activeJobToken == 0 && issuedSessionToken == 0 &&
                       issuedAuthorizationToken == 0 && issuedJobToken == 0 &&
                       inFlightOperationToken == 0 &&
                       !sourceDispatchObserved && sourceResourcesReleased;
            return true;
        }
        return sourceIntegrityFlags != 0 || sourceMailboxFaultBits != 0 ||
               observerIntegrityFlags != 0;
    }
};

static_assert(std::is_standard_layout<PearlObserverSnapshot>::value,
              "observer snapshots must retain a fixed field layout");
static_assert(std::is_trivially_copyable<PearlObserverSnapshot>::value,
              "observer snapshots must not acquire dynamic ownership");
static_assert(sizeof(PearlObserverSnapshot) <= 2048,
              "observer envelope must remain a small bounded payload");

inline bool pearlObserverProgressEqual(const PearlProgressSnapshot &a,
                                       const PearlProgressSnapshot &b) {
    if (a.phase != b.phase || a.lastReason != b.lastReason ||
        a.lastOutcome != b.lastOutcome || a.localDrops != b.localDrops)
        return false;
#define OM_PEARL_OBSERVER_PROGRESS_U64_FIELDS(X)                           \
    X(sessionGeneration) X(authorizationGeneration) X(jobGeneration)       \
    X(prepareGeneration) X(searchGeneration) X(finalizerGeneration)        \
    X(candidateGeneration) X(submitGeneration) X(sessionStartedMs)         \
    X(authorizedMs) X(jobAdoptedMs) X(jobRetiredMs) X(prepareStartedMs)    \
    X(prepareCompletedMs) X(searchStartedMs) X(searchCompletedMs)          \
    X(lastAttemptStartedMs) X(lastAttemptCompletedMs)                      \
    X(lastFinalizerStartedMs) X(lastFinalizerProgressMs)                   \
    X(lastProofSerializedMs) X(lastSubmitAttemptMs) X(lastAcknowledgedMs)  \
    X(lastOutcomeMs) X(publishedAtMs) X(searchesStarted)                    \
    X(searchesCompleted) X(searchesFailed) X(attemptsStarted)              \
    X(attemptsCompleted) X(hitsObserved) X(hitsRetained)                   \
    X(finalizersStarted) X(hitSaturationEvents)                            \
    X(hitsDroppedBySaturation) X(candidatesSelected) X(hostChecked)        \
    X(hostValid) X(hostInvalid) X(proofsSerialized) X(submitAttempts)      \
    X(submissionFailures) X(submitted) X(acknowledged) X(accepted)         \
    X(rejected) X(stale) X(unknown) X(pending) X(acceptedSuperseded)
#define OM_COMPARE_OBSERVER_PROGRESS_FIELD(name) \
    if (a.name != b.name) return false;
    OM_PEARL_OBSERVER_PROGRESS_U64_FIELDS(
        OM_COMPARE_OBSERVER_PROGRESS_FIELD)
#undef OM_COMPARE_OBSERVER_PROGRESS_FIELD
#undef OM_PEARL_OBSERVER_PROGRESS_U64_FIELDS
    return true;
}

inline bool pearlObserverLivenessCoreEqual(const PearlLivenessSnapshot &a,
                                           const PearlLivenessSnapshot &b) {
    return a.connection == b.connection && a.jobProgress == b.jobProgress &&
           a.candidate == b.candidate && a.lastOutcome == b.lastOutcome &&
           a.lastOutcomeEvidence == b.lastOutcomeEvidence &&
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
           a.outcomes == b.outcomes;
}

inline bool pearlObserverDeadlineCanAge(const PearlDeadline &before,
                                        const PearlDeadline &after) {
    if (before.atMs != after.atMs) return false;
    if (before.state == after.state) return true;
    return before.state == PearlDeadlineState::Fresh &&
           after.state == PearlDeadlineState::Expired;
}

/**
 * Single-writer, multi-reader atomic mirror for the complete envelope.
 *
 * Payload bytes are themselves atomic. This matters in standard C++: a
 * seqlock around an ordinary struct would still race when a rejected reader
 * overlaps the writer. Sequentially consistent odd/even sequence operations
 * retain the established PearlProgressBus publication contract.
 */
class PearlObserverBus {
   public:
    class Writer {
       public:
        Writer() = default;
        Writer(const Writer &) = delete;
        Writer &operator=(const Writer &) = delete;
        Writer(Writer &&other) noexcept
            : bus_(other.bus_),
              capability_(other.capability_),
              busIdentity_(other.busIdentity_) {
            other.invalidate();
        }
        Writer &operator=(Writer &&other) noexcept {
            if (this != &other) {
                bus_ = other.bus_;
                capability_ = other.capability_;
                busIdentity_ = other.busIdentity_;
                other.invalidate();
            }
            return *this;
        }

        bool valid() const { return capability_ != 0; }

        bool publish(const PearlObserverSnapshot &snapshot) {
            return bus_ &&
                   bus_->publishImpl(capability_, busIdentity_, snapshot,
                                     nullptr, nullptr);
        }

        bool publishWithMidpointHookForTest(
            const PearlObserverSnapshot &snapshot,
            void (*hook)(void *) noexcept,
            void *context) {
            return bus_ &&
                   bus_->publishImpl(capability_, busIdentity_, snapshot, hook,
                                     context);
        }

       private:
        friend class PearlObserverBus;
        Writer(PearlObserverBus *bus, uint64_t capability,
               uint64_t busIdentity)
            : bus_(bus),
              capability_(capability),
              busIdentity_(busIdentity) {}
        void invalidate() noexcept {
            bus_ = nullptr;
            capability_ = 0;
            busIdentity_ = 0;
        }
        PearlObserverBus *bus_ = nullptr;
        uint64_t capability_ = 0;
        uint64_t busIdentity_ = 0;
    };

    PearlObserverBus()
        : busIdentity_(
              pearlObserverAcquireUniqueId(&pearlObserverNextBusId)) {
        initialize(0);
    }

    // Test-only seam for deterministic sequence-exhaustion coverage. Odd
    // seeds are normalized down to the preceding stable sequence.
    explicit PearlObserverBus(uint64_t initialSequenceForTest)
        : busIdentity_(
              pearlObserverAcquireUniqueId(&pearlObserverNextBusId)) {
        uint64_t stable = initialSequenceForTest & ~uint64_t{1};
        const uint64_t lastLatchable =
            std::numeric_limits<uint64_t>::max() - 3;
        if (stable > lastLatchable) stable = lastLatchable;
        initialize(stable);
    }

    PearlObserverBus(const PearlObserverBus &) = delete;
    PearlObserverBus &operator=(const PearlObserverBus &) = delete;

    // One capability for the lifetime of the bus. It is noncopyable and moves
    // transfer rather than duplicate authority; a dead owner is never
    // replaced. A second
    // acquisition is an inert handle whose attempted publication latches the
    // writer-integrity fault without acquiring authority. As with every C++
    // owner handle, invoking it while no PearlObserverBus object occupies its
    // stored address is forbidden; a non-reused bus identity rejects a stale
    // handle if a new bus is later constructed at that exact address.
    Writer acquireWriter() {
        Writer result;
        acquireWriter(&result);
        return result;
    }

    // Equivalent out-parameter form for callers that must construct the
    // noncopyable handle inside a thread-entry routine. Any previous contents
    // of *out are invalidated before the one lifetime capability is attempted.
    bool acquireWriter(Writer *out) {
        if (!out) return false;
        out->invalidate();
        const uint64_t threadIdentity = pearlObserverCurrentThreadId();
        bool expected = false;
        uint64_t capability = 0;
        if (busIdentity_ != 0 &&
            writerIssued_.compare_exchange_strong(
                expected, true, std::memory_order_seq_cst,
                std::memory_order_seq_cst)) {
            capability = writerCapability_;
            writerThreadIdentity_.store(threadIdentity,
                                        std::memory_order_seq_cst);
        }
        out->bus_ = this;
        out->capability_ = capability;
        out->busIdentity_ = busIdentity_;
        return capability != 0;
    }

    bool tryRead(PearlObserverSnapshot *out,
                 unsigned maxAttempts = 128) const {
        return tryReadImpl(out, maxAttempts, nullptr, nullptr);
    }

    // Deterministic test seam for the second sequence check.
    bool tryReadWithAfterFirstSequenceHookForTest(
        PearlObserverSnapshot *out, unsigned maxAttempts,
        void (*hook)(void *), void *context) const {
        return tryReadImpl(out, maxAttempts, hook, context);
    }

    uint64_t sequence() const {
        return sequence_.load(std::memory_order_seq_cst);
    }

   private:
    void initialize(uint64_t sequence) {
        for (auto &byte : payload_)
            byte.store(0, std::memory_order_relaxed);
        PearlObserverSnapshot initial;
        storePayload(initial, nullptr, nullptr);
        sequence_.store(sequence, std::memory_order_relaxed);
        exhausted_ = false;
        publicationSeen_ = false;
    }

    static bool monotonicCountersHold(const PearlObserverSnapshot &before,
                                      const PearlObserverSnapshot &after) {
        return after.sourceEventsDrained >= before.sourceEventsDrained &&
               after.sourceEventsInvalid >= before.sourceEventsInvalid &&
               after.sourceRefusedEvents >= before.sourceRefusedEvents &&
               after.sourceFaultEvents >= before.sourceFaultEvents &&
               after.sourceQueueHighWater >= before.sourceQueueHighWater &&
               after.issuedBindingGeneration >=
                   before.issuedBindingGeneration &&
               after.physicalTeardownGeneration >=
                   before.physicalTeardownGeneration &&
               (after.sourceIntegrityFlags | before.sourceIntegrityFlags) ==
                   after.sourceIntegrityFlags &&
               (after.sourceMailboxFaultBits |
                before.sourceMailboxFaultBits) ==
                   after.sourceMailboxFaultBits &&
               (after.observerIntegrityFlags |
                before.observerIntegrityFlags) ==
                   after.observerIntegrityFlags;
    }

    static bool miningTotalsMonotonic(const PearlObserverSnapshot &a,
                                      const PearlObserverSnapshot &b) {
        if (b.progress.sessionGeneration < a.progress.sessionGeneration ||
            b.progress.authorizationGeneration <
                a.progress.authorizationGeneration ||
            b.progress.jobGeneration < a.progress.jobGeneration ||
            b.progress.prepareGeneration < a.progress.prepareGeneration ||
            b.progress.searchGeneration < a.progress.searchGeneration ||
            b.progress.finalizerGeneration < a.progress.finalizerGeneration ||
            b.progress.candidateGeneration < a.progress.candidateGeneration ||
            b.progress.submitGeneration < a.progress.submitGeneration ||
            b.liveness.sessionGeneration < a.liveness.sessionGeneration ||
            b.liveness.jobGeneration < a.liveness.jobGeneration ||
            b.liveness.candidateGeneration < a.liveness.candidateGeneration ||
            b.liveness.submitGeneration < a.liveness.submitGeneration)
            return false;
        if (b.liveness.lastOutcomeSubmitGeneration <
                a.liveness.lastOutcomeSubmitGeneration &&
            !(b.liveness.sessionGeneration > a.liveness.sessionGeneration &&
              b.liveness.lastOutcomeSubmitGeneration == 0))
            return false;

#define OM_OBSERVER_MONOTONIC_PROGRESS(name) \
    if (b.progress.name < a.progress.name) return false;
        OM_OBSERVER_MONOTONIC_PROGRESS(searchesStarted)
        OM_OBSERVER_MONOTONIC_PROGRESS(searchesCompleted)
        OM_OBSERVER_MONOTONIC_PROGRESS(searchesFailed)
        OM_OBSERVER_MONOTONIC_PROGRESS(attemptsStarted)
        OM_OBSERVER_MONOTONIC_PROGRESS(attemptsCompleted)
        OM_OBSERVER_MONOTONIC_PROGRESS(hitsObserved)
        OM_OBSERVER_MONOTONIC_PROGRESS(hitsRetained)
        OM_OBSERVER_MONOTONIC_PROGRESS(finalizersStarted)
        OM_OBSERVER_MONOTONIC_PROGRESS(hitSaturationEvents)
        OM_OBSERVER_MONOTONIC_PROGRESS(hitsDroppedBySaturation)
        OM_OBSERVER_MONOTONIC_PROGRESS(candidatesSelected)
        OM_OBSERVER_MONOTONIC_PROGRESS(hostChecked)
        OM_OBSERVER_MONOTONIC_PROGRESS(hostValid)
        OM_OBSERVER_MONOTONIC_PROGRESS(hostInvalid)
        OM_OBSERVER_MONOTONIC_PROGRESS(proofsSerialized)
        OM_OBSERVER_MONOTONIC_PROGRESS(submitAttempts)
        OM_OBSERVER_MONOTONIC_PROGRESS(submissionFailures)
        OM_OBSERVER_MONOTONIC_PROGRESS(submitted)
        OM_OBSERVER_MONOTONIC_PROGRESS(acknowledged)
        OM_OBSERVER_MONOTONIC_PROGRESS(accepted)
        OM_OBSERVER_MONOTONIC_PROGRESS(rejected)
        OM_OBSERVER_MONOTONIC_PROGRESS(stale)
        OM_OBSERVER_MONOTONIC_PROGRESS(unknown)
        OM_OBSERVER_MONOTONIC_PROGRESS(acceptedSuperseded)
#undef OM_OBSERVER_MONOTONIC_PROGRESS
        for (std::size_t i = 0; i < a.progress.localDrops.size(); ++i)
            if (b.progress.localDrops[i] < a.progress.localDrops[i])
                return false;

#define OM_OBSERVER_MONOTONIC_LIVENESS(name) \
    if (b.liveness.name < a.liveness.name) return false;
        OM_OBSERVER_MONOTONIC_LIVENESS(completedAttempts)
        // completedAttemptsForJob is the sole resettable liveness counter; it
        // resets only when the job or transport session generation advances.
        if (b.liveness.completedAttemptsForJob <
            a.liveness.completedAttemptsForJob) {
            const bool generationReset =
                b.liveness.jobGeneration > a.liveness.jobGeneration ||
                b.liveness.sessionGeneration > a.liveness.sessionGeneration;
            const bool retiredOrDisconnected =
                a.liveness.jobProgress != PearlJobProgressState::NoJob &&
                b.liveness.jobProgress == PearlJobProgressState::NoJob &&
                b.activeJobToken == 0;
            if (!generationReset && !retiredOrDisconnected) return false;
        }
        OM_OBSERVER_MONOTONIC_LIVENESS(candidatesObserved)
        OM_OBSERVER_MONOTONIC_LIVENESS(finalizerProgressEvents)
        OM_OBSERVER_MONOTONIC_LIVENESS(candidatesFinalized)
        OM_OBSERVER_MONOTONIC_LIVENESS(candidatesInvalid)
        OM_OBSERVER_MONOTONIC_LIVENESS(submitAttempts)
        OM_OBSERVER_MONOTONIC_LIVENESS(submissionFailures)
        OM_OBSERVER_MONOTONIC_LIVENESS(possiblyDeliveredUnknowns)
        OM_OBSERVER_MONOTONIC_LIVENESS(unacknowledgedUnknowns)
        OM_OBSERVER_MONOTONIC_LIVENESS(acknowledgements)
        OM_OBSERVER_MONOTONIC_LIVENESS(outcomes)
#undef OM_OBSERVER_MONOTONIC_LIVENESS
        return true;
    }

    static bool terminalDeltasHold(const PearlObserverSnapshot &a,
                                   const PearlObserverSnapshot &b,
                                   bool allowBootstrap) {
        const bool pristineInitial =
            a.sourceState == PearlObserverSourceState::Unsupported &&
            a.progress.sessionGeneration == 0 &&
            a.progress.submitAttempts == 0 &&
            a.liveness.submitAttempts == 0 &&
            a.liveness.outcomes == 0 && a.publishedAtMs == 0 &&
            a.observerIntegrityFlags == 0;
        if (allowBootstrap && pristineInitial)
            return b.liveness.lastOutcome == PearlSubmitOutcome::None &&
                   b.liveness.lastOutcomeEvidence ==
                       PearlOutcomeEvidence::None &&
                   b.liveness.lastOutcomeSubmitGeneration == 0;
        const uint64_t max = std::numeric_limits<uint64_t>::max();
        const auto unchanged = [](uint64_t before, uint64_t after) {
            return before == after;
        };
        const auto oneStep = [max](uint64_t before, uint64_t after) {
            return before != max && after == before + 1;
        };
        if (b.liveness.outcomes == a.liveness.outcomes) {
            if (!unchanged(a.progress.accepted, b.progress.accepted) ||
                !unchanged(a.progress.rejected, b.progress.rejected) ||
                !unchanged(a.progress.stale, b.progress.stale) ||
                !unchanged(a.progress.unknown, b.progress.unknown) ||
                !unchanged(a.progress.submissionFailures,
                           b.progress.submissionFailures) ||
                !unchanged(a.progress.submitted,
                           b.progress.submitted) ||
                !unchanged(a.progress.acknowledged,
                           b.progress.acknowledged) ||
                !unchanged(a.liveness.submissionFailures,
                           b.liveness.submissionFailures) ||
                !unchanged(a.liveness.possiblyDeliveredUnknowns,
                           b.liveness.possiblyDeliveredUnknowns) ||
                !unchanged(a.liveness.unacknowledgedUnknowns,
                           b.liveness.unacknowledgedUnknowns) ||
                !unchanged(a.liveness.acknowledgements,
                           b.liveness.acknowledgements) ||
                !unchanged(a.progress.acceptedSuperseded,
                           b.progress.acceptedSuperseded))
                return false;
            const bool terminalUnchanged =
                b.liveness.lastOutcome == a.liveness.lastOutcome &&
                b.liveness.lastOutcomeEvidence ==
                    a.liveness.lastOutcomeEvidence &&
                b.liveness.lastOutcomeSubmitGeneration ==
                    a.liveness.lastOutcomeSubmitGeneration;
            const bool clearedForNewSession =
                b.liveness.sessionGeneration >
                    a.liveness.sessionGeneration &&
                b.liveness.lastOutcome == PearlSubmitOutcome::None &&
                b.liveness.lastOutcomeEvidence ==
                    PearlOutcomeEvidence::None &&
                b.liveness.lastOutcomeSubmitGeneration == 0;
            return terminalUnchanged || clearedForNewSession;
        }
        if (!oneStep(a.liveness.outcomes, b.liveness.outcomes))
            return false;

        const PearlSourceEvent &event = b.lastSourceEvent;
        const bool exactPriorDispatch =
            a.unresolvedSubmit && a.sourceDispatchObserved &&
            a.inFlightOperationToken != 0 && !b.unresolvedSubmit &&
            !b.sourceDispatchObserved && b.inFlightOperationToken == 0 &&
            b.liveness.lastOutcomeSubmitGeneration ==
                a.unresolvedSubmitGeneration &&
            event.kind == PearlSourceEventKind::SubmitTerminal &&
            event.sessionToken == a.inFlightSessionToken &&
            event.authorizationToken == a.inFlightAuthorizationToken &&
            event.jobToken == a.inFlightJobToken &&
            event.operationToken == a.inFlightOperationToken &&
            pearlObserverDigestEqual(event.jobIdentity,
                                     a.inFlightJobIdentity) &&
            b.sourceEventsDrained > a.sourceEventsDrained &&
            b.lastSourceEventToken > a.lastSourceEventToken &&
            b.lastSourceEventMs >= a.lastSourceEventMs &&
            event.eventToken == b.lastSourceEventToken &&
            event.timestampMs == b.lastSourceEventMs;
        bool terminalMatches = false;
        switch (b.liveness.lastOutcomeEvidence) {
            case PearlOutcomeEvidence::LocalPreDeliveryFailure:
                terminalMatches =
                    b.liveness.lastOutcome ==
                        PearlSubmitOutcome::SubmissionFailed &&
                    event.terminal == PearlSourceTerminal::LocalFailure &&
                    event.delivery == PearlSourceDelivery::NotSent &&
                    event.terminalEvidence ==
                        PearlSourceTerminalEvidence::LocalPreDeliveryFailure &&
                    event.acknowledged == 0;
                break;
            case PearlOutcomeEvidence::PossiblyDeliveredUnknown:
                terminalMatches =
                    b.liveness.lastOutcome == PearlSubmitOutcome::Unknown &&
                    event.terminal == PearlSourceTerminal::Unknown &&
                    event.delivery == PearlSourceDelivery::PossiblySent &&
                    event.terminalEvidence ==
                        PearlSourceTerminalEvidence::PossiblyDeliveredUnknown &&
                    event.acknowledged == 0;
                break;
            case PearlOutcomeEvidence::FullySentNoReply:
                terminalMatches =
                    b.liveness.lastOutcome == PearlSubmitOutcome::Unknown &&
                    event.terminal == PearlSourceTerminal::Unknown &&
                    event.delivery == PearlSourceDelivery::Sent &&
                    event.terminalEvidence ==
                        PearlSourceTerminalEvidence::FullySentNoReply &&
                    event.acknowledged == 0;
                break;
            case PearlOutcomeEvidence::GatewayAcknowledgementOnly:
                terminalMatches =
                    b.liveness.lastOutcome ==
                        PearlSubmitOutcome::Acknowledged &&
                    event.terminal == PearlSourceTerminal::GatewayQueued &&
                    event.delivery == PearlSourceDelivery::Sent &&
                    event.terminalEvidence ==
                        PearlSourceTerminalEvidence::GatewayAcknowledgementOnly &&
                    event.acknowledged != 0;
                break;
            case PearlOutcomeEvidence::AcknowledgedOutcomeUnavailable:
                terminalMatches =
                    b.liveness.lastOutcome == PearlSubmitOutcome::Unknown &&
                    event.terminal == PearlSourceTerminal::Unknown &&
                    event.delivery == PearlSourceDelivery::Sent &&
                    event.terminalEvidence ==
                        PearlSourceTerminalEvidence::AcknowledgedOutcomeUnavailable &&
                    event.acknowledged != 0;
                break;
            case PearlOutcomeEvidence::CorrelatedRemoteOutcome:
                terminalMatches =
                    event.delivery == PearlSourceDelivery::Sent &&
                    event.acknowledged != 0 &&
                    event.reason == PearlSourceReason::None &&
                    event.terminalEvidence ==
                        PearlSourceTerminalEvidence::CorrelatedRemoteOutcome &&
                    ((b.liveness.lastOutcome ==
                          PearlSubmitOutcome::Accepted &&
                      event.terminal == PearlSourceTerminal::Accepted) ||
                     (b.liveness.lastOutcome ==
                          PearlSubmitOutcome::Rejected &&
                      event.terminal == PearlSourceTerminal::Rejected) ||
                     (b.liveness.lastOutcome == PearlSubmitOutcome::Stale &&
                      event.terminal == PearlSourceTerminal::Stale) ||
                     (b.liveness.lastOutcome ==
                          PearlSubmitOutcome::Unknown &&
                      event.terminal == PearlSourceTerminal::Unknown));
                break;
            case PearlOutcomeEvidence::None:
                break;
        }
        if (!exactPriorDispatch || !terminalMatches) return false;

        bool expectedAccepted = false, expectedRejected = false;
        bool expectedStale = false, expectedUnknown = false;
        switch (b.liveness.lastOutcomeEvidence) {
            case PearlOutcomeEvidence::LocalPreDeliveryFailure:
            case PearlOutcomeEvidence::PossiblyDeliveredUnknown:
                break;
            case PearlOutcomeEvidence::FullySentNoReply:
            case PearlOutcomeEvidence::GatewayAcknowledgementOnly:
            case PearlOutcomeEvidence::AcknowledgedOutcomeUnavailable:
                expectedUnknown = 1;
                break;
            case PearlOutcomeEvidence::CorrelatedRemoteOutcome:
                switch (b.liveness.lastOutcome) {
                    case PearlSubmitOutcome::Accepted:
                        expectedAccepted = true;
                        break;
                    case PearlSubmitOutcome::Rejected:
                        expectedRejected = true;
                        break;
                    case PearlSubmitOutcome::Stale:
                        expectedStale = true;
                        break;
                    case PearlSubmitOutcome::Unknown:
                        expectedUnknown = true;
                        break;
                    default:
                        return false;
                }
                break;
            case PearlOutcomeEvidence::None:
                return false;
        }
        const auto mapped = [&](uint64_t before, uint64_t after,
                                bool expected) {
            return expected ? oneStep(before, after)
                            : unchanged(before, after);
        };
        const bool expectedLocalFailure =
            b.liveness.lastOutcomeEvidence ==
            PearlOutcomeEvidence::LocalPreDeliveryFailure;
        const bool expectedPossiblyDelivered =
            b.liveness.lastOutcomeEvidence ==
            PearlOutcomeEvidence::PossiblyDeliveredUnknown;
        const bool expectedFullySentNoReply =
            b.liveness.lastOutcomeEvidence ==
            PearlOutcomeEvidence::FullySentNoReply;
        const bool acknowledgementDeltaHolds =
            event.acknowledged == 0
                ? unchanged(a.liveness.acknowledgements,
                            b.liveness.acknowledgements)
                : (a.liveness.candidate ==
                           PearlCandidateState::AwaitingOutcome
                       ? unchanged(a.liveness.acknowledgements,
                                   b.liveness.acknowledgements)
                       : oneStep(a.liveness.acknowledgements,
                                 b.liveness.acknowledgements));
        return mapped(a.liveness.submissionFailures,
                      b.liveness.submissionFailures,
                      expectedLocalFailure) &&
               mapped(a.liveness.possiblyDeliveredUnknowns,
                      b.liveness.possiblyDeliveredUnknowns,
                      expectedPossiblyDelivered) &&
               mapped(a.liveness.unacknowledgedUnknowns,
                      b.liveness.unacknowledgedUnknowns,
                      expectedFullySentNoReply) &&
               acknowledgementDeltaHolds &&
               mapped(a.progress.accepted, b.progress.accepted,
                      expectedAccepted) &&
               mapped(a.progress.rejected, b.progress.rejected,
                      expectedRejected) &&
               mapped(a.progress.stale, b.progress.stale, expectedStale) &&
               mapped(a.progress.unknown, b.progress.unknown,
                      expectedUnknown) &&
               mapped(a.progress.acceptedSuperseded,
                      b.progress.acceptedSuperseded,
                      expectedAccepted && event.superseded != 0);
    }

    static bool cursorAdvanced(const PearlObserverSnapshot &a,
                               const PearlObserverSnapshot &b) {
        return b.sourceEventsDrained > a.sourceEventsDrained &&
               b.lastSourceEventToken > a.lastSourceEventToken &&
               b.lastSourceEventMs >= a.lastSourceEventMs;
    }

    static bool exactEventAdvanced(const PearlObserverSnapshot &a,
                                   const PearlObserverSnapshot &b,
                                   PearlSourceEventKind kind) {
        return cursorAdvanced(a, b) && b.lastSourceEvent.kind == kind &&
               b.lastSourceEvent.eventToken == b.lastSourceEventToken &&
               b.lastSourceEvent.timestampMs == b.lastSourceEventMs;
    }

    static bool issuedBindingTransitionHolds(
        const PearlObserverSnapshot &a, const PearlObserverSnapshot &b) {
        const bool tupleChanged =
            a.issuedSessionToken != b.issuedSessionToken ||
            a.issuedAuthorizationToken != b.issuedAuthorizationToken ||
            a.issuedJobToken != b.issuedJobToken ||
            !pearlObserverDigestEqual(a.issuedJobIdentity,
                                     b.issuedJobIdentity) ||
            !pearlObserverDigestEqual(a.lastReleasedIssuedJobIdentity,
                                     b.lastReleasedIssuedJobIdentity);
        if (!tupleChanged)
            return b.issuedBindingGeneration ==
                       a.issuedBindingGeneration &&
                   b.issuedBindingCause == a.issuedBindingCause;
        if (a.unresolvedSubmit) return false;
        if (a.issuedBindingGeneration ==
                std::numeric_limits<uint64_t>::max() ||
            b.issuedBindingGeneration != a.issuedBindingGeneration + 1)
            return false;
        if (a.issuedJobToken != 0 && b.issuedJobToken == 0)
            return b.issuedBindingCause == PearlIssuedBindingCause::Released &&
                   pearlObserverDigestEqual(
                       b.lastReleasedIssuedJobIdentity,
                       a.issuedJobIdentity) &&
                   b.progress.jobGeneration == a.progress.jobGeneration;
        if (a.issuedJobToken != 0 || b.issuedJobToken == 0 ||
            b.issuedJobToken != b.activeJobToken ||
            !pearlObserverDigestEqual(b.issuedJobIdentity,
                                     b.activeJobIdentity) ||
            !pearlObserverDigestZero(b.lastReleasedIssuedJobIdentity))
            return false;
        if (b.issuedBindingCause == PearlIssuedBindingCause::SameJobRebind)
            return !pearlObserverDigestZero(
                       a.lastReleasedIssuedJobIdentity) &&
                   pearlObserverDigestEqual(
                       b.activeJobIdentity,
                       a.lastReleasedIssuedJobIdentity) &&
                   b.progress.jobGeneration == a.progress.jobGeneration;
        const bool firstAdoption =
            a.issuedBindingGeneration == 0 &&
            a.issuedBindingCause == PearlIssuedBindingCause::None &&
            pearlObserverDigestZero(a.lastReleasedIssuedJobIdentity);
        const bool genuinelyDifferent =
            !pearlObserverDigestZero(a.lastReleasedIssuedJobIdentity) &&
            !pearlObserverDigestEqual(
                b.activeJobIdentity, a.lastReleasedIssuedJobIdentity);
        return b.issuedBindingCause ==
                   PearlIssuedBindingCause::DifferentJobAdoption &&
               (firstAdoption || genuinelyDifferent) &&
               a.progress.jobGeneration !=
                   std::numeric_limits<uint64_t>::max() &&
               b.progress.jobGeneration == a.progress.jobGeneration + 1;
    }

    static bool physicalTransitionHolds(const PearlObserverSnapshot &a,
                                        const PearlObserverSnapshot &b) {
        const bool changed =
            a.physicalTransportConnected != b.physicalTransportConnected ||
            a.sourceResourcesReleased != b.sourceResourcesReleased;
        if (!changed)
            return b.physicalTeardownGeneration ==
                       a.physicalTeardownGeneration &&
                   b.physicalTeardownCause == a.physicalTeardownCause;

        const bool disconnected =
            exactEventAdvanced(a, b, PearlSourceEventKind::Disconnected) &&
            b.lastSourceEvent.sessionToken == a.sourceSessionToken;
        if (disconnected)
            return a.physicalTeardownGeneration !=
                       std::numeric_limits<uint64_t>::max() &&
                   b.physicalTeardownGeneration ==
                       a.physicalTeardownGeneration + 1 &&
                   b.physicalTeardownCause ==
                       PearlPhysicalTeardownCause::SourceDisconnected &&
                   !b.physicalTransportConnected;

        const bool restarted =
            a.sourceSessionToken != 0 && b.sourceSessionToken != 0 &&
            a.sourceSessionToken != b.sourceSessionToken &&
            exactEventAdvanced(a, b, PearlSourceEventKind::SessionStarted) &&
            b.lastSourceEvent.sessionToken == b.sourceSessionToken &&
            b.sourceEventsDrained - a.sourceEventsDrained >= 2;
        if (restarted)
            return a.physicalTeardownGeneration !=
                       std::numeric_limits<uint64_t>::max() &&
                   b.physicalTeardownGeneration ==
                       a.physicalTeardownGeneration + 1 &&
                   b.physicalTeardownCause ==
                       PearlPhysicalTeardownCause::SourceDisconnected &&
                   !b.physicalTransportConnected;

        const bool quarantinedTeardown =
            b.sourceState == PearlObserverSourceState::Quarantined &&
            !b.physicalTransportConnected &&
            !(a.sourceResourcesReleased && !b.sourceResourcesReleased) &&
            a.physicalTeardownGeneration !=
                std::numeric_limits<uint64_t>::max() &&
            b.physicalTeardownGeneration ==
                a.physicalTeardownGeneration + 1 &&
            b.physicalTeardownCause ==
                PearlPhysicalTeardownCause::QuarantineRelease;
        if (quarantinedTeardown) return true;

        const bool connected =
            exactEventAdvanced(a, b,
                               PearlSourceEventKind::TransportConnected) &&
            b.lastSourceEvent.sessionToken == b.sourceSessionToken;
        const bool sessionAllocated =
            exactEventAdvanced(a, b, PearlSourceEventKind::SessionStarted) &&
            b.lastSourceEvent.sessionToken == b.sourceSessionToken &&
            !b.physicalTransportConnected && !b.sourceResourcesReleased;
        return (connected || sessionAllocated) &&
               (!connected || (b.physicalTransportConnected &&
                                !b.sourceResourcesReleased)) &&
               b.physicalTeardownGeneration ==
                   a.physicalTeardownGeneration &&
               b.physicalTeardownCause == a.physicalTeardownCause;
    }

    static bool initialBootstrapHolds(const PearlObserverSnapshot &b) {
        if (b.sourceState == PearlObserverSourceState::Unsupported)
            return true;
        if (b.sourceState != PearlObserverSourceState::Trusted) return false;

        const PearlSourceEvent &event = b.lastSourceEvent;
        if (event.kind == PearlSourceEventKind::None) {
            const PearlProgressSnapshot emptyProgress;
            const PearlLivenessSnapshot emptyLiveness;
            return b.sourceSessionToken == 0 && !b.sourceConnected &&
                   !b.sourceReady && b.sourceResourcesReleased &&
                   b.sourceEventsDrained == 0 && b.lastSourceEventToken == 0 &&
                   pearlObserverProgressEqual(b.progress, emptyProgress) &&
                   pearlObserverLivenessCoreEqual(b.liveness,
                                                  emptyLiveness);
        }
        if (b.sourceEventsDrained == 0 || b.lastSourceEventToken == 0 ||
            event.eventToken != b.lastSourceEventToken ||
            event.timestampMs != b.lastSourceEventMs)
            return false;

        if (event.kind != PearlSourceEventKind::Disconnected &&
            event.sessionToken != b.sourceSessionToken)
            return false;
        if (event.kind == PearlSourceEventKind::SourceFault &&
            !b.sourceFaultObserved)
            return false;
        if (b.sourceFaultObserved && b.sourceFaultEvents == 0) return false;

        switch (event.kind) {
            case PearlSourceEventKind::SessionStarted:
                return b.sourceSessionToken != 0 && !b.sourceConnected &&
                       !b.sourceReady && !b.physicalTransportConnected &&
                       !b.sourceResourcesReleased &&
                       !b.sourceFaultObserved &&
                       b.sourceAuthorizationToken == 0 &&
                       b.activeJobToken == 0 && b.issuedJobToken == 0 &&
                       b.inFlightOperationToken == 0;
            case PearlSourceEventKind::TransportConnected:
                return b.sourceSessionToken != 0 && b.sourceConnected &&
                       b.physicalTransportConnected &&
                       !b.sourceResourcesReleased && !b.sourceReady &&
                       b.sourceAuthorizationToken == 0 &&
                       b.activeJobToken == 0 && b.issuedJobToken == 0 &&
                       b.inFlightOperationToken == 0;
            case PearlSourceEventKind::AuthorizationSucceeded:
                return b.sourceConnected && b.sourceReady &&
                       b.sourceAuthorizationToken != 0 &&
                       event.authorizationToken ==
                           b.sourceAuthorizationToken &&
                       b.activeJobToken == 0 && b.issuedJobToken == 0 &&
                       b.inFlightOperationToken == 0;
            case PearlSourceEventKind::SessionReadyNoAuth:
                return b.sourceConnected && b.sourceReady &&
                       b.sourceAuthorizationToken == 0 &&
                       event.authorizationToken == 0 &&
                       b.activeJobToken == 0 && b.issuedJobToken == 0 &&
                       b.inFlightOperationToken == 0;
            case PearlSourceEventKind::JobRetainedPreAuth:
                return b.sourceConnected && !b.sourceReady &&
                       b.sourceAuthorizationToken == 0 &&
                       b.activeJobToken == 0 && event.jobToken != 0 &&
                       !pearlObserverDigestZero(event.jobIdentity);
            case PearlSourceEventKind::JobAvailable:
                return b.sourceReady && b.activeJobToken != 0 &&
                       event.authorizationToken ==
                           b.sourceAuthorizationToken &&
                       event.jobToken == b.activeJobToken &&
                       pearlObserverDigestEqual(event.jobIdentity,
                                                b.activeJobIdentity) &&
                       b.inFlightOperationToken == 0;
            case PearlSourceEventKind::JobRetired:
                return b.sourceReady && b.activeJobToken == 0 &&
                       event.authorizationToken ==
                           b.sourceAuthorizationToken &&
                       event.jobToken != 0 &&
                       !pearlObserverDigestZero(event.jobIdentity) &&
                       b.inFlightOperationToken == 0;
            case PearlSourceEventKind::SubmitDispatched:
                return b.unresolvedSubmit && b.sourceDispatchObserved &&
                       b.inFlightOperationToken != 0 &&
                       event.sessionToken == b.inFlightSessionToken &&
                       event.authorizationToken ==
                           b.inFlightAuthorizationToken &&
                       event.jobToken == b.inFlightJobToken &&
                       event.operationToken == b.inFlightOperationToken &&
                       pearlObserverDigestEqual(event.jobIdentity,
                                                b.inFlightJobIdentity);
            case PearlSourceEventKind::SubmitTerminal:
                // A terminal cannot be trusted as the first observation: its
                // prior dispatched tuple and local submit generation are not
                // present in the bus history.
                return false;
            case PearlSourceEventKind::Disconnected:
                return b.sourceSessionToken == 0 && !b.sourceConnected &&
                       !b.sourceReady && !b.physicalTransportConnected &&
                       b.sourceResourcesReleased &&
                       b.sourceAuthorizationToken == 0 &&
                       b.activeJobToken == 0 && b.issuedJobToken == 0 &&
                       b.inFlightOperationToken == 0;
            case PearlSourceEventKind::SourceFault:
                return b.sourceSessionToken != 0 &&
                       b.sourceFaultObserved &&
                       event.authorizationToken ==
                           b.sourceAuthorizationToken &&
                       event.jobToken == b.activeJobToken &&
                       pearlObserverDigestEqual(event.jobIdentity,
                                                b.activeJobIdentity);
            case PearlSourceEventKind::None:
                break;
        }
        return false;
    }

    static bool sourceCausalityHolds(const PearlObserverSnapshot &a,
                                     const PearlObserverSnapshot &b,
                                     bool allowBootstrap) {
        const bool faultRaised =
            !a.sourceFaultObserved && b.sourceFaultObserved;
        const bool faultCleared =
            a.sourceFaultObserved && !b.sourceFaultObserved;
        const bool exactFaultEvent =
            exactEventAdvanced(a, b, PearlSourceEventKind::SourceFault) &&
            b.lastSourceEvent.sessionToken == b.sourceSessionToken &&
            b.lastSourceEvent.authorizationToken ==
                b.sourceAuthorizationToken &&
            b.lastSourceEvent.jobToken == b.activeJobToken &&
            pearlObserverDigestEqual(b.lastSourceEvent.jobIdentity,
                                     b.activeJobIdentity);
        const bool saturatedFaultIncrement =
            (a.sourceFaultEvents == std::numeric_limits<uint64_t>::max() &&
             b.sourceFaultEvents == a.sourceFaultEvents) ||
            (a.sourceFaultEvents != std::numeric_limits<uint64_t>::max() &&
             b.sourceFaultEvents == a.sourceFaultEvents + 1);
        if (faultRaised && (!exactFaultEvent || !saturatedFaultIncrement))
            return false;
        if (exactFaultEvent &&
            (!b.sourceFaultObserved || !saturatedFaultIncrement))
            return false;
        if (faultCleared) {
            const bool laterSession =
                b.sourceSessionToken != 0 &&
                b.sourceSessionToken != a.sourceSessionToken &&
                b.progress.sessionGeneration > a.progress.sessionGeneration &&
                exactEventAdvanced(a, b,
                                   PearlSourceEventKind::SessionStarted) &&
                b.lastSourceEvent.sessionToken == b.sourceSessionToken;
            const bool cleanDisconnect =
                b.sourceSessionToken == 0 && !b.sourceConnected &&
                !b.sourceReady && b.sourceResourcesReleased &&
                b.liveness.connection == PearlConnectionState::Disconnected &&
                exactEventAdvanced(a, b,
                                   PearlSourceEventKind::Disconnected) &&
                b.lastSourceEvent.sessionToken == a.sourceSessionToken;
            if (!laterSession && !cleanDisconnect) return false;
        }
        if (b.sourceFaultEvents != a.sourceFaultEvents &&
            !exactFaultEvent)
            return false;
        if (b.sourceSessionToken == a.sourceSessionToken &&
            b.sourceFaultEvents != a.sourceFaultEvents &&
            !b.sourceFaultObserved)
            return false;

        const bool sourceOwnershipChanged =
            a.sourceSessionToken != b.sourceSessionToken ||
            a.sourceAuthorizationToken != b.sourceAuthorizationToken ||
            a.activeJobToken != b.activeJobToken ||
            a.inFlightOperationToken != b.inFlightOperationToken ||
            a.inFlightSessionToken != b.inFlightSessionToken ||
            a.inFlightAuthorizationToken !=
                b.inFlightAuthorizationToken ||
            a.inFlightJobToken != b.inFlightJobToken ||
            a.sourceDispatchObserved != b.sourceDispatchObserved ||
            !pearlObserverDigestEqual(a.activeJobIdentity,
                                     b.activeJobIdentity) ||
            !pearlObserverDigestEqual(a.inFlightJobIdentity,
                                     b.inFlightJobIdentity);
        const bool initialBaseline =
            allowBootstrap &&
            a.sourceState == PearlObserverSourceState::Unsupported &&
            a.lastSourceEventToken == 0 && a.sourceEventsDrained == 0 &&
            a.progress.sessionGeneration == 0 &&
            a.progress.authorizationGeneration == 0 &&
            a.progress.jobGeneration == 0 &&
            a.liveness.sessionGeneration == 0 &&
            a.liveness.jobGeneration == 0 && a.publishedAtMs == 0 &&
            a.observerIntegrityFlags == 0;
        if (initialBaseline) return initialBootstrapHolds(b);

        const bool sourceInstrumented =
            b.sourceState != PearlObserverSourceState::Unsupported;
        const uint64_t counterMax = std::numeric_limits<uint64_t>::max();
        const auto exactCounterStep = [counterMax](uint64_t before,
                                                   uint64_t after) {
            return before != counterMax && after == before + 1;
        };
        const bool priorLocalPreDispatch =
            a.unresolvedSubmit && !a.sourceDispatchObserved &&
            a.inFlightOperationToken == 0;
        const bool nextLocalPreDispatch =
            b.unresolvedSubmit && !b.sourceDispatchObserved &&
            b.inFlightOperationToken == 0;
        const bool submitCountersChanged =
            b.progress.submitGeneration != a.progress.submitGeneration ||
            b.liveness.submitGeneration != a.liveness.submitGeneration ||
            b.progress.submitAttempts != a.progress.submitAttempts ||
            b.liveness.submitAttempts != a.liveness.submitAttempts;
        if (sourceInstrumented && !a.unresolvedSubmit &&
            b.unresolvedSubmit) {
            const bool priorCandidateAlreadyPublished =
                a.progress.phase == PearlProgressPhase::AwaitingSubmit &&
                a.liveness.candidate ==
                    PearlCandidateState::AwaitingSubmit &&
                b.progress.candidateGeneration ==
                    a.progress.candidateGeneration &&
                b.liveness.candidateGeneration ==
                    a.liveness.candidateGeneration;
            if (!priorCandidateAlreadyPublished ||
                !exactCounterStep(a.progress.submitGeneration,
                                   b.progress.submitGeneration) ||
                !exactCounterStep(a.liveness.submitGeneration,
                                  b.liveness.submitGeneration) ||
                !exactCounterStep(a.progress.submitAttempts,
                                  b.progress.submitAttempts) ||
                !exactCounterStep(a.liveness.submitAttempts,
                                  b.liveness.submitAttempts) ||
                b.unresolvedSubmitGeneration !=
                    b.progress.submitGeneration ||
                b.liveness.candidate !=
                    PearlCandidateState::AwaitingAcknowledgement ||
                (nextLocalPreDispatch
                      ? b.progress.phase != PearlProgressPhase::Submitting
                      : b.progress.phase !=
                            PearlProgressPhase::AwaitingAcknowledgement))
                return false;
        }
        if (sourceInstrumented && submitCountersChanged &&
            (a.unresolvedSubmit || !b.unresolvedSubmit))
            return false;
        if (sourceInstrumented && a.unresolvedSubmit &&
            b.unresolvedSubmit &&
            (b.unresolvedSubmitGeneration !=
                 a.unresolvedSubmitGeneration ||
             b.progress.candidateGeneration !=
                 a.progress.candidateGeneration ||
             b.liveness.candidateGeneration !=
                 a.liveness.candidateGeneration ||
             b.progress.submitGeneration !=
                 a.progress.submitGeneration ||
             b.liveness.submitGeneration !=
                 a.liveness.submitGeneration ||
             b.progress.submitAttempts != a.progress.submitAttempts ||
             b.liveness.submitAttempts != a.liveness.submitAttempts ||
             b.progress.pending != a.progress.pending ||
             b.liveness.candidate != a.liveness.candidate))
            return false;
        if (sourceInstrumented && priorLocalPreDispatch &&
            !b.unresolvedSubmit)
            return false;
        if (sourceInstrumented && cursorAdvanced(a, b)) {
            const PearlSourceEvent &event = b.lastSourceEvent;
            switch (event.kind) {
                case PearlSourceEventKind::SessionStarted:
                    if (b.sourceSessionToken == 0 ||
                        b.sourceSessionToken == a.sourceSessionToken ||
                        event.sessionToken != b.sourceSessionToken ||
                        b.sourceConnected || b.sourceReady ||
                        b.sourceAuthorizationToken != 0 ||
                        b.activeJobToken != 0 || b.issuedJobToken != 0 ||
                        b.inFlightOperationToken != 0 ||
                        b.sourceFaultObserved ||
                        b.physicalTransportConnected ||
                        b.sourceResourcesReleased)
                        return false;
                    break;
                case PearlSourceEventKind::TransportConnected:
                    if (a.sourceSessionToken == 0 ||
                        b.sourceSessionToken != a.sourceSessionToken ||
                        event.sessionToken != a.sourceSessionToken ||
                        a.sourceConnected || !b.sourceConnected ||
                        a.sourceReady || b.sourceReady ||
                        a.sourceAuthorizationToken != 0 ||
                        b.sourceAuthorizationToken != 0 ||
                        !b.physicalTransportConnected ||
                        b.sourceResourcesReleased)
                        return false;
                    break;
                case PearlSourceEventKind::AuthorizationSucceeded:
                    if (a.sourceSessionToken == 0 ||
                        b.sourceSessionToken != a.sourceSessionToken ||
                        event.sessionToken != a.sourceSessionToken ||
                        !a.sourceConnected || !b.sourceConnected ||
                        a.sourceReady || !b.sourceReady ||
                        a.sourceAuthorizationToken != 0 ||
                        b.sourceAuthorizationToken == 0 ||
                        event.authorizationToken !=
                            b.sourceAuthorizationToken)
                        return false;
                    break;
                case PearlSourceEventKind::SessionReadyNoAuth:
                    if (a.sourceSessionToken == 0 ||
                        b.sourceSessionToken != a.sourceSessionToken ||
                        event.sessionToken != a.sourceSessionToken ||
                        !a.sourceConnected || !b.sourceConnected ||
                        a.sourceReady || !b.sourceReady ||
                        a.sourceAuthorizationToken != 0 ||
                        b.sourceAuthorizationToken != 0)
                        return false;
                    break;
                case PearlSourceEventKind::JobRetainedPreAuth:
                    // Retained pre-auth ownership is intentionally not part
                    // of the public envelope. The exact mailbox event is
                    // therefore visible only in its legal pre-auth context.
                    if (a.sourceSessionToken == 0 ||
                        b.sourceSessionToken != a.sourceSessionToken ||
                        event.sessionToken != a.sourceSessionToken ||
                        !a.sourceConnected || !b.sourceConnected ||
                        a.sourceReady || b.sourceReady ||
                        a.sourceAuthorizationToken != 0 ||
                        b.sourceAuthorizationToken != 0 ||
                        a.activeJobToken != 0 || b.activeJobToken != 0)
                        return false;
                    break;
                case PearlSourceEventKind::JobAvailable:
                    if (a.sourceSessionToken == 0 ||
                        b.sourceSessionToken != a.sourceSessionToken ||
                        event.sessionToken != a.sourceSessionToken ||
                        !a.sourceConnected || !b.sourceConnected ||
                        !a.sourceReady || !b.sourceReady ||
                        event.authorizationToken !=
                            b.sourceAuthorizationToken ||
                        a.activeJobToken != 0 || b.activeJobToken == 0 ||
                        event.jobToken != b.activeJobToken ||
                        !pearlObserverDigestEqual(event.jobIdentity,
                                                  b.activeJobIdentity))
                        return false;
                    break;
                case PearlSourceEventKind::JobRetired: {
                    const bool activeRetirement =
                        a.activeJobToken != 0 && b.activeJobToken == 0 &&
                        event.sessionToken == a.sourceSessionToken &&
                        event.authorizationToken ==
                            a.sourceAuthorizationToken &&
                        event.jobToken == a.activeJobToken &&
                        pearlObserverDigestEqual(event.jobIdentity,
                                                 a.activeJobIdentity);
                    const bool invisibleRetainedRetirement =
                        a.sourceSessionToken != 0 &&
                        b.sourceSessionToken == a.sourceSessionToken &&
                        event.sessionToken == a.sourceSessionToken &&
                        a.sourceConnected && b.sourceConnected &&
                        !a.sourceReady && !b.sourceReady &&
                        a.sourceAuthorizationToken == 0 &&
                        b.sourceAuthorizationToken == 0 &&
                        event.authorizationToken == 0 &&
                        a.activeJobToken == 0 && b.activeJobToken == 0;
                    if (!activeRetirement &&
                        !invisibleRetainedRetirement)
                        return false;
                    break;
                }
                case PearlSourceEventKind::SubmitDispatched:
                    if (a.inFlightOperationToken != 0 ||
                        b.inFlightOperationToken == 0 ||
                        !b.unresolvedSubmit || !b.sourceDispatchObserved ||
                        b.unresolvedSubmitGeneration !=
                            b.progress.submitGeneration ||
                        event.sessionToken != b.inFlightSessionToken ||
                        event.authorizationToken !=
                            b.inFlightAuthorizationToken ||
                        event.jobToken != b.inFlightJobToken ||
                        event.operationToken !=
                            b.inFlightOperationToken ||
                        !pearlObserverDigestEqual(event.jobIdentity,
                                                  b.inFlightJobIdentity))
                        return false;
                    {
                        const bool bindsPriorLocalSubmit =
                            a.unresolvedSubmit &&
                            !a.sourceDispatchObserved &&
                            a.progress.phase ==
                                PearlProgressPhase::Submitting &&
                            a.liveness.candidate ==
                                PearlCandidateState::AwaitingAcknowledgement &&
                            b.progress.phase ==
                                PearlProgressPhase::AwaitingAcknowledgement &&
                            b.liveness.candidate ==
                                PearlCandidateState::AwaitingAcknowledgement &&
                            a.unresolvedSubmitGeneration ==
                                a.progress.submitGeneration &&
                            b.progress.submitGeneration ==
                                a.progress.submitGeneration &&
                            b.liveness.submitGeneration ==
                                a.liveness.submitGeneration &&
                            b.progress.submitAttempts ==
                                a.progress.submitAttempts &&
                            b.liveness.submitAttempts ==
                                a.liveness.submitAttempts &&
                            b.unresolvedSubmitGeneration ==
                                a.unresolvedSubmitGeneration;
                        const bool coalescesNewLocalSubmit =
                            !a.unresolvedSubmit &&
                            !a.sourceDispatchObserved &&
                            exactCounterStep(
                                a.progress.submitGeneration,
                                b.progress.submitGeneration) &&
                            exactCounterStep(
                                a.liveness.submitGeneration,
                                b.liveness.submitGeneration) &&
                            exactCounterStep(a.progress.submitAttempts,
                                             b.progress.submitAttempts) &&
                            exactCounterStep(a.liveness.submitAttempts,
                                             b.liveness.submitAttempts) &&
                            b.progress.phase ==
                                PearlProgressPhase::AwaitingAcknowledgement &&
                            b.liveness.candidate ==
                                PearlCandidateState::AwaitingAcknowledgement;
                        if (!bindsPriorLocalSubmit &&
                            !coalescesNewLocalSubmit)
                            return false;
                    }
                    break;
                case PearlSourceEventKind::SubmitTerminal:
                    if (a.inFlightOperationToken == 0 ||
                        b.inFlightOperationToken != 0)
                        return false;
                    break;
                case PearlSourceEventKind::Disconnected:
                    if (a.sourceSessionToken == 0 ||
                        event.sessionToken != a.sourceSessionToken ||
                        b.sourceSessionToken != 0 || b.sourceConnected ||
                        b.sourceReady ||
                        b.sourceAuthorizationToken != 0 ||
                        b.activeJobToken != 0 || b.issuedJobToken != 0 ||
                        b.inFlightOperationToken != 0 ||
                        b.unresolvedSubmit || b.sourceDispatchObserved ||
                        b.physicalTransportConnected ||
                        !b.sourceResourcesReleased)
                        return false;
                    break;
                case PearlSourceEventKind::SourceFault:
                    // Exact fault identity and the one-step/saturated counter
                    // effect are checked below.
                    break;
                case PearlSourceEventKind::None:
                    return false;
            }
        }
        const bool sessionGenerationAdvanced =
            b.progress.sessionGeneration > a.progress.sessionGeneration;
        if (sourceInstrumented && sessionGenerationAdvanced) {
            if (a.progress.sessionGeneration ==
                    std::numeric_limits<uint64_t>::max() ||
                b.progress.sessionGeneration !=
                    a.progress.sessionGeneration + 1 ||
                b.sourceSessionToken == 0 ||
                b.sourceSessionToken == a.sourceSessionToken ||
                !exactEventAdvanced(a, b,
                                    PearlSourceEventKind::SessionStarted) ||
                b.lastSourceEvent.sessionToken != b.sourceSessionToken)
                return false;
            if (b.sourceFaultObserved) return false;
        }
        if (sourceInstrumented && b.sourceSessionToken != 0 &&
            b.sourceSessionToken != a.sourceSessionToken &&
            !sessionGenerationAdvanced)
            return false;

        const bool authorizationGenerationAdvanced =
            b.progress.authorizationGeneration >
            a.progress.authorizationGeneration;
        if (sourceInstrumented &&
            (exactEventAdvanced(
                 a, b, PearlSourceEventKind::AuthorizationSucceeded) ||
             exactEventAdvanced(
                 a, b, PearlSourceEventKind::SessionReadyNoAuth)) &&
            !authorizationGenerationAdvanced)
            return false;
        if (sourceInstrumented &&
            ((exactEventAdvanced(
                  a, b, PearlSourceEventKind::AuthorizationSucceeded) &&
              (a.sourceReady || a.sourceAuthorizationToken != 0)) ||
             (exactEventAdvanced(
                  a, b, PearlSourceEventKind::SessionReadyNoAuth) &&
              a.sourceReady)))
            return false;
        if (sourceInstrumented && authorizationGenerationAdvanced) {
            const bool exactAuthorization =
                exactEventAdvanced(
                    a, b, PearlSourceEventKind::AuthorizationSucceeded) &&
                !a.sourceReady && a.sourceAuthorizationToken == 0 &&
                b.sourceReady && b.sourceAuthorizationToken != 0 &&
                b.lastSourceEvent.sessionToken == b.sourceSessionToken &&
                b.lastSourceEvent.authorizationToken ==
                    b.sourceAuthorizationToken;
            const bool exactNoAuthorization =
                exactEventAdvanced(
                    a, b, PearlSourceEventKind::SessionReadyNoAuth) &&
                !a.sourceReady && a.sourceAuthorizationToken == 0 &&
                b.sourceReady && b.sourceAuthorizationToken == 0 &&
                b.lastSourceEvent.sessionToken == b.sourceSessionToken;
            if (a.progress.authorizationGeneration ==
                    std::numeric_limits<uint64_t>::max() ||
                b.progress.authorizationGeneration !=
                    a.progress.authorizationGeneration + 1 ||
                (!exactAuthorization && !exactNoAuthorization))
                return false;
        }

        const bool jobGenerationAdvanced =
            b.progress.jobGeneration > a.progress.jobGeneration;
        if (sourceInstrumented && jobGenerationAdvanced) {
            const bool exactDifferentJobAdoption =
                a.progress.jobGeneration !=
                    std::numeric_limits<uint64_t>::max() &&
                b.progress.jobGeneration == a.progress.jobGeneration + 1 &&
                a.issuedJobToken == 0 && b.issuedJobToken != 0 &&
                a.issuedBindingGeneration !=
                    std::numeric_limits<uint64_t>::max() &&
                b.issuedBindingGeneration ==
                    a.issuedBindingGeneration + 1 &&
                b.issuedBindingCause ==
                    PearlIssuedBindingCause::DifferentJobAdoption;
            if (!exactDifferentJobAdoption) return false;
        }

        const bool activeJobChanged =
            a.activeJobToken != b.activeJobToken ||
            !pearlObserverDigestEqual(a.activeJobIdentity,
                                     b.activeJobIdentity);
        if (sourceInstrumented && activeJobChanged) {
            const bool available =
                b.activeJobToken != 0 &&
                exactEventAdvanced(a, b,
                                   PearlSourceEventKind::JobAvailable) &&
                b.lastSourceEvent.sessionToken == b.sourceSessionToken &&
                b.lastSourceEvent.authorizationToken ==
                    b.sourceAuthorizationToken &&
                b.lastSourceEvent.jobToken == b.activeJobToken &&
                pearlObserverDigestEqual(b.lastSourceEvent.jobIdentity,
                                         b.activeJobIdentity);
            const bool retired =
                a.activeJobToken != 0 && b.activeJobToken == 0 &&
                exactEventAdvanced(a, b,
                                   PearlSourceEventKind::JobRetired) &&
                b.lastSourceEvent.sessionToken == a.sourceSessionToken &&
                b.lastSourceEvent.authorizationToken ==
                    a.sourceAuthorizationToken &&
                b.lastSourceEvent.jobToken == a.activeJobToken &&
                pearlObserverDigestEqual(b.lastSourceEvent.jobIdentity,
                                         a.activeJobIdentity);
            if (!available && !retired) return false;
        }

        const bool authorizationChanged =
            a.sourceAuthorizationToken != b.sourceAuthorizationToken;
        if (sourceInstrumented && authorizationChanged) {
            const bool authorized =
                !a.sourceReady && a.sourceAuthorizationToken == 0 &&
                b.sourceReady && b.sourceAuthorizationToken != 0 &&
                exactEventAdvanced(
                    a, b, PearlSourceEventKind::AuthorizationSucceeded) &&
                b.lastSourceEvent.sessionToken == b.sourceSessionToken &&
                b.lastSourceEvent.authorizationToken ==
                    b.sourceAuthorizationToken;
            const bool cleared =
                b.sourceAuthorizationToken == 0 &&
                (exactEventAdvanced(a, b,
                                    PearlSourceEventKind::SessionStarted) ||
                 exactEventAdvanced(a, b,
                                    PearlSourceEventKind::Disconnected));
            if (!authorized && !cleared) return false;
        }
        const bool readinessEstablished = !a.sourceReady && b.sourceReady;
        if (sourceInstrumented && readinessEstablished &&
            !authorizationGenerationAdvanced)
            return false;

        if (sourceInstrumented) {
            const uint64_t drainedDelta =
                b.sourceEventsDrained - a.sourceEventsDrained;
            const uint64_t tokenDelta =
                b.lastSourceEventToken - a.lastSourceEventToken;
            if (drainedDelta != tokenDelta) return false;
            const bool coalescedRestart =
                drainedDelta == 2 && a.sourceSessionToken != 0 &&
                b.sourceSessionToken != 0 &&
                b.sourceSessionToken != a.sourceSessionToken &&
                b.lastSourceEvent.kind ==
                    PearlSourceEventKind::SessionStarted;
            if (drainedDelta != 0 && drainedDelta != 1 &&
                !coalescedRestart)
                return false;
        }

        if (sourceInstrumented && a.inFlightOperationToken != 0 &&
            b.inFlightOperationToken != 0 &&
            (a.inFlightOperationToken != b.inFlightOperationToken ||
             a.inFlightSessionToken != b.inFlightSessionToken ||
             a.inFlightAuthorizationToken !=
                 b.inFlightAuthorizationToken ||
             a.inFlightJobToken != b.inFlightJobToken ||
             !pearlObserverDigestEqual(a.inFlightJobIdentity,
                                      b.inFlightJobIdentity) ||
             a.unresolvedSubmitGeneration !=
                b.unresolvedSubmitGeneration))
            return false;
        if (sourceInstrumented && a.inFlightOperationToken != 0 &&
            b.inFlightOperationToken == 0) {
            const bool exactTerminal =
                exactEventAdvanced(a, b,
                                   PearlSourceEventKind::SubmitTerminal) &&
                b.lastSourceEvent.sessionToken == a.inFlightSessionToken &&
                b.lastSourceEvent.authorizationToken ==
                    a.inFlightAuthorizationToken &&
                b.lastSourceEvent.jobToken == a.inFlightJobToken &&
                b.lastSourceEvent.operationToken ==
                    a.inFlightOperationToken &&
                pearlObserverDigestEqual(b.lastSourceEvent.jobIdentity,
                                         a.inFlightJobIdentity);
            if (!exactTerminal ||
                a.liveness.outcomes ==
                    std::numeric_limits<uint64_t>::max() ||
                b.liveness.outcomes != a.liveness.outcomes + 1)
                return false;
        }
        if (sourceInstrumented &&
            exactEventAdvanced(a, b,
                               PearlSourceEventKind::SubmitTerminal) &&
            (a.inFlightOperationToken == 0 ||
             b.inFlightOperationToken != 0))
            return false;

        if (sourceInstrumented &&
            exactEventAdvanced(a, b,
                               PearlSourceEventKind::Disconnected) &&
            (b.sourceSessionToken != 0 || b.sourceConnected ||
             b.sourceReady || b.sourceAuthorizationToken != 0 ||
             b.activeJobToken != 0 || b.issuedJobToken != 0 ||
             b.inFlightOperationToken != 0 || b.unresolvedSubmit ||
             b.sourceDispatchObserved || b.physicalTransportConnected ||
             !b.sourceResourcesReleased))
            return false;
        if (sourceInstrumented &&
            exactEventAdvanced(a, b,
                               PearlSourceEventKind::Disconnected) &&
            b.lastSourceEvent.authorizationToken !=
                a.sourceAuthorizationToken)
            return false;
        if (sourceOwnershipChanged && !initialBaseline && !cursorAdvanced(a, b))
            return false;
        const bool sourceEventChanged =
            !pearlObserverSourceEventEqual(a.lastSourceEvent,
                                           b.lastSourceEvent);
        if (cursorAdvanced(a, b) &&
            b.lastSourceEvent.kind == PearlSourceEventKind::None)
            return false;
        if (sourceEventChanged && !cursorAdvanced(a, b)) return false;

        if (a.sourceSessionToken != b.sourceSessionToken && !initialBaseline) {
            const bool started =
                b.sourceSessionToken != 0 &&
                exactEventAdvanced(a, b,
                                   PearlSourceEventKind::SessionStarted) &&
                b.lastSourceEvent.sessionToken == b.sourceSessionToken;
            const bool disconnected =
                b.sourceSessionToken == 0 &&
                exactEventAdvanced(a, b,
                                   PearlSourceEventKind::Disconnected) &&
                b.lastSourceEvent.sessionToken == a.sourceSessionToken;
            if (!started && !disconnected) return false;
        }
        if (a.sourceConnected != b.sourceConnected && !initialBaseline) {
            const bool connected =
                b.sourceConnected &&
                exactEventAdvanced(a, b,
                                   PearlSourceEventKind::TransportConnected) &&
                b.lastSourceEvent.sessionToken == b.sourceSessionToken &&
                b.lastSourceEvent.authorizationToken == 0;
            const bool disconnected =
                !b.sourceConnected &&
                ((exactEventAdvanced(a, b,
                                     PearlSourceEventKind::Disconnected) &&
                  b.lastSourceEvent.sessionToken == a.sourceSessionToken) ||
                 (a.sourceSessionToken != b.sourceSessionToken &&
                  exactEventAdvanced(a, b,
                                     PearlSourceEventKind::SessionStarted) &&
                  b.lastSourceEvent.sessionToken == b.sourceSessionToken &&
                  b.sourceEventsDrained - a.sourceEventsDrained >= 2));
            if (!connected && !disconnected) return false;
        }
        if (a.sourceReady != b.sourceReady && !initialBaseline) {
            const bool authorizationReady =
                !a.sourceReady && a.sourceAuthorizationToken == 0 &&
                b.sourceReady && b.sourceAuthorizationToken != 0 &&
                exactEventAdvanced(
                    a, b, PearlSourceEventKind::AuthorizationSucceeded) &&
                b.lastSourceEvent.sessionToken == b.sourceSessionToken &&
                b.lastSourceEvent.authorizationToken ==
                    b.sourceAuthorizationToken;
            const bool noAuthorizationReady =
                !a.sourceReady && a.sourceAuthorizationToken == 0 &&
                b.sourceReady && b.sourceAuthorizationToken == 0 &&
                exactEventAdvanced(
                    a, b, PearlSourceEventKind::SessionReadyNoAuth) &&
                b.lastSourceEvent.sessionToken == b.sourceSessionToken &&
                b.lastSourceEvent.authorizationToken == 0;
            const bool unready =
                !b.sourceReady &&
                ((exactEventAdvanced(a, b,
                                     PearlSourceEventKind::Disconnected) &&
                  b.lastSourceEvent.sessionToken == a.sourceSessionToken) ||
                 (exactEventAdvanced(a, b,
                                     PearlSourceEventKind::SessionStarted) &&
                  b.lastSourceEvent.sessionToken == b.sourceSessionToken));
            if (!authorizationReady && !noAuthorizationReady && !unready)
                return false;
        }
        const bool dispatchStarted =
            a.inFlightOperationToken == 0 && b.inFlightOperationToken != 0;
        if (dispatchStarted &&
            (!exactEventAdvanced(a, b,
                                 PearlSourceEventKind::SubmitDispatched) ||
             b.lastSourceEvent.sessionToken != b.inFlightSessionToken ||
             b.lastSourceEvent.authorizationToken !=
                 b.inFlightAuthorizationToken ||
             b.lastSourceEvent.jobToken != b.inFlightJobToken ||
             b.lastSourceEvent.operationToken != b.inFlightOperationToken ||
             !pearlObserverDigestEqual(b.lastSourceEvent.jobIdentity,
                                      b.inFlightJobIdentity)))
            return false;
        return issuedBindingTransitionHolds(a, b) &&
               physicalTransitionHolds(a, b);
    }

    static bool deadlineRefreshHolds(const PearlObserverSnapshot &a,
                                     const PearlObserverSnapshot &b) {
        const auto transition = [](const PearlDeadline &oldDeadline,
                                   const PearlDeadline &newDeadline,
                                   bool ownerAdvanced) {
            if (oldDeadline.state == newDeadline.state &&
                oldDeadline.atMs == newDeadline.atMs)
                return true;
            if (oldDeadline.state == PearlDeadlineState::Fresh &&
                newDeadline.state == PearlDeadlineState::Expired)
                return oldDeadline.atMs == newDeadline.atMs;
            return ownerAdvanced;
        };
        const bool adoptedJobRetired =
            a.liveness.jobProgress != PearlJobProgressState::NoJob &&
            b.liveness.jobProgress == PearlJobProgressState::NoJob &&
            b.issuedBindingGeneration ==
                a.issuedBindingGeneration + 1 &&
            b.issuedBindingCause == PearlIssuedBindingCause::Released;
        const bool exactDisconnect =
            exactEventAdvanced(a, b,
                               PearlSourceEventKind::Disconnected) &&
            b.lastSourceEvent.sessionToken == a.sourceSessionToken;
        const bool connectionAdvanced =
            b.liveness.sessionGeneration > a.liveness.sessionGeneration ||
            b.progress.authorizationGeneration >
                a.progress.authorizationGeneration ||
            b.liveness.jobGeneration > a.liveness.jobGeneration ||
            adoptedJobRetired || exactDisconnect;
        const bool attemptAdvanced =
            b.liveness.jobGeneration > a.liveness.jobGeneration ||
            (b.liveness.completedAttempts > a.liveness.completedAttempts &&
             b.progress.attemptsCompleted > a.progress.attemptsCompleted) ||
            adoptedJobRetired ||
            b.liveness.sessionGeneration > a.liveness.sessionGeneration ||
            exactDisconnect;
        const bool candidateAdvanced =
            b.liveness.candidateGeneration >
                a.liveness.candidateGeneration ||
            b.liveness.submitGeneration > a.liveness.submitGeneration ||
            b.liveness.finalizerProgressEvents >
                a.liveness.finalizerProgressEvents ||
            b.liveness.candidatesFinalized >
                a.liveness.candidatesFinalized ||
            b.liveness.outcomes > a.liveness.outcomes ||
            b.liveness.submissionFailures >
                a.liveness.submissionFailures;
        const bool outcomeAdvanced =
            b.liveness.outcomes > a.liveness.outcomes ||
            b.liveness.sessionGeneration > a.liveness.sessionGeneration;
        return transition(a.liveness.connectionDeadline,
                          b.liveness.connectionDeadline,
                          connectionAdvanced) &&
               transition(a.liveness.attemptDeadline,
                          b.liveness.attemptDeadline, attemptAdvanced) &&
               transition(a.liveness.candidateDeadline,
                          b.liveness.candidateDeadline, candidateAdvanced) &&
               transition(a.liveness.outcomeFreshness,
                          b.liveness.outcomeFreshness, outcomeAdvanced);
    }

    static bool preservedAfterQuarantine(
        const PearlObserverSnapshot &before,
        const PearlObserverSnapshot &after) {
        return pearlObserverProgressEqual(before.progress, after.progress) &&
               pearlObserverLivenessCoreEqual(before.liveness,
                                               after.liveness) &&
               pearlObserverDeadlineCanAge(
                   before.liveness.connectionDeadline,
                   after.liveness.connectionDeadline) &&
               pearlObserverDeadlineCanAge(before.liveness.attemptDeadline,
                                           after.liveness.attemptDeadline) &&
               pearlObserverDeadlineCanAge(before.liveness.candidateDeadline,
                                           after.liveness.candidateDeadline) &&
               pearlObserverDeadlineCanAge(before.liveness.outcomeFreshness,
                                           after.liveness.outcomeFreshness) &&
               before.sourceConnected == after.sourceConnected &&
               before.sourceReady == after.sourceReady &&
               before.sourceFaultObserved == after.sourceFaultObserved &&
               before.lastSourceEventToken == after.lastSourceEventToken &&
               before.lastSourceEventMs == after.lastSourceEventMs &&
               pearlObserverSourceEventEqual(before.lastSourceEvent,
                                             after.lastSourceEvent) &&
               before.sourceSessionToken == after.sourceSessionToken &&
               before.sourceAuthorizationToken ==
                   after.sourceAuthorizationToken &&
               before.activeJobToken == after.activeJobToken &&
               before.issuedSessionToken == after.issuedSessionToken &&
               before.issuedAuthorizationToken ==
                   after.issuedAuthorizationToken &&
               before.issuedJobToken == after.issuedJobToken &&
               before.issuedBindingGeneration ==
                   after.issuedBindingGeneration &&
               before.issuedBindingCause == after.issuedBindingCause &&
               before.inFlightOperationToken ==
                   after.inFlightOperationToken &&
               before.inFlightSessionToken == after.inFlightSessionToken &&
               before.inFlightAuthorizationToken ==
                   after.inFlightAuthorizationToken &&
               before.inFlightJobToken == after.inFlightJobToken &&
               pearlObserverDigestEqual(before.activeJobIdentity,
                                        after.activeJobIdentity) &&
               pearlObserverDigestEqual(before.issuedJobIdentity,
                                        after.issuedJobIdentity) &&
               pearlObserverDigestEqual(
                   before.lastReleasedIssuedJobIdentity,
                   after.lastReleasedIssuedJobIdentity) &&
               pearlObserverDigestEqual(before.inFlightJobIdentity,
                                        after.inFlightJobIdentity) &&
               before.unresolvedSubmit == after.unresolvedSubmit &&
               before.sourceDispatchObserved ==
                   after.sourceDispatchObserved &&
               before.unresolvedSubmitGeneration ==
                   after.unresolvedSubmitGeneration;
    }

    static bool transitionHolds(const PearlObserverSnapshot &before,
                                const PearlObserverSnapshot &after,
                                bool allowBootstrap) {
        if (after.publishedAtMs < before.publishedAtMs ||
            !monotonicCountersHold(before, after))
            return false;
        if (before.sourceState != PearlObserverSourceState::Unsupported &&
            after.sourceState == PearlObserverSourceState::Unsupported)
            return false;
        if (before.sourceState == PearlObserverSourceState::Quarantined)
            return after.sourceState ==
                       PearlObserverSourceState::Quarantined &&
                   preservedAfterQuarantine(before, after) &&
                   physicalTransitionHolds(before, after);
        if (after.sourceState == PearlObserverSourceState::Quarantined)
            return preservedAfterQuarantine(before, after) &&
                   physicalTransitionHolds(before, after);
        return miningTotalsMonotonic(before, after) &&
               after.lastSourceEventToken >= before.lastSourceEventToken &&
               after.lastSourceEventMs >= before.lastSourceEventMs &&
               terminalDeltasHold(before, after, allowBootstrap) &&
               sourceCausalityHolds(before, after, allowBootstrap) &&
               deadlineRefreshHolds(before, after);
    }

    bool publishImpl(uint64_t capability, uint64_t busIdentity,
                     const PearlObserverSnapshot &requested,
                     void (*hook)(void *) noexcept, void *context) {
        const uint64_t currentThreadIdentity =
            pearlObserverCurrentThreadId();
        if (currentThreadIdentity != 0 &&
            activePublisherThreadIdentity_.load(std::memory_order_relaxed) ==
                currentThreadIdentity) {
            reentryDetected_.store(true, std::memory_order_relaxed);
            return false;
        }
        std::lock_guard<std::mutex> lock(writerMutex_);
        struct ActivePublishGuard {
            std::atomic<uint64_t> *slot;
            explicit ActivePublishGuard(std::atomic<uint64_t> *identity,
                                        uint64_t threadIdentity)
                : slot(identity) {
                slot->store(threadIdentity, std::memory_order_relaxed);
            }
            ~ActivePublishGuard() {
                slot->store(0, std::memory_order_relaxed);
            }
        } activeGuard(&activePublisherThreadIdentity_,
                      currentThreadIdentity);
        PearlObserverSnapshot current;
        loadPayload(&current);
        const uint64_t stable = sequence_.load(std::memory_order_seq_cst);
        if (exhausted_ || (stable & 1)) return false;

        PearlObserverSnapshot candidate = requested;
        const bool wrongWriter =
            capability == 0 || capability != writerCapability_ ||
            busIdentity == 0 || busIdentity != busIdentity_ ||
            currentThreadIdentity == 0 ||
            writerThreadIdentity_.load(std::memory_order_seq_cst) !=
                currentThreadIdentity;
        bool accepted =
            !wrongWriter && candidate.invariantsHold() &&
            transitionHolds(current, candidate, !publicationSeen_);
        if (!accepted) {
            candidate = current;
            candidate.sourceState = PearlObserverSourceState::Quarantined;
            candidate.observerIntegrityFlags |= pearlObserverIntegrityBit(
                wrongWriter
                    ? PearlObserverIntegrityFlag::WriterAuthorityViolation
                    : PearlObserverIntegrityFlag::InvalidCandidate);
        }

        const uint64_t max = std::numeric_limits<uint64_t>::max();
        if (stable >= max - 3) {
            candidate = current;
            candidate.sourceState = PearlObserverSourceState::Quarantined;
            candidate.observerIntegrityFlags |= pearlObserverIntegrityBit(
                PearlObserverIntegrityFlag::SequenceExhausted);
            accepted = false;
            exhausted_ = true;
        }

        publicationSeen_ = true;

        sequence_.store(stable + 1, std::memory_order_seq_cst);
        storePayload(candidate, hook, context);
        if (reentryDetected_.exchange(false, std::memory_order_relaxed)) {
            candidate = current;
            candidate.sourceState = PearlObserverSourceState::Quarantined;
            candidate.observerIntegrityFlags |= pearlObserverIntegrityBit(
                PearlObserverIntegrityFlag::ReentrantWriter);
            storePayload(candidate, nullptr, nullptr);
            accepted = false;
        }
        sequence_.store(stable + 2, std::memory_order_seq_cst);
        return accepted;
    }

    bool tryReadImpl(PearlObserverSnapshot *out, unsigned maxAttempts,
                     void (*hook)(void *), void *context) const {
        if (!out || maxAttempts == 0) return false;
        for (unsigned attempt = 0; attempt < maxAttempts; ++attempt) {
            const uint64_t before = sequence_.load(std::memory_order_seq_cst);
            if (before & 1) continue;
            if (attempt == 0 && hook) hook(context);
            PearlObserverSnapshot candidate;
            loadPayload(&candidate);
            const uint64_t after = sequence_.load(std::memory_order_seq_cst);
            if (before == after && !(after & 1) &&
                candidate.invariantsHold()) {
                *out = candidate;
                return true;
            }
        }
        return false;
    }

    void storePayload(const PearlObserverSnapshot &snapshot,
                      void (*hook)(void *) noexcept, void *context) {
        const auto *bytes = reinterpret_cast<const uint8_t *>(&snapshot);
        const std::size_t midpoint = payload_.size() / 2;
        for (std::size_t i = 0; i < midpoint; ++i)
            payload_[i].store(bytes[i], std::memory_order_seq_cst);
        if (hook) hook(context);
        for (std::size_t i = midpoint; i < payload_.size(); ++i)
            payload_[i].store(bytes[i], std::memory_order_seq_cst);
    }

    void loadPayload(PearlObserverSnapshot *snapshot) const {
        auto *bytes = reinterpret_cast<uint8_t *>(snapshot);
        for (std::size_t i = 0; i < payload_.size(); ++i)
            bytes[i] = payload_[i].load(std::memory_order_seq_cst);
    }

    std::atomic<uint64_t> sequence_{0};
    std::array<std::atomic<uint8_t>, sizeof(PearlObserverSnapshot)> payload_{};
    std::mutex writerMutex_;
    const uint64_t busIdentity_;
    const uint64_t writerCapability_ = 1;
    std::atomic<bool> writerIssued_{false};
    std::atomic<uint64_t> writerThreadIdentity_{0};
    bool exhausted_ = false;
    bool publicationSeen_ = false;
    std::atomic<bool> reentryDetected_{false};
    std::atomic<uint64_t> activePublisherThreadIdentity_{0};
};

}  // namespace om
