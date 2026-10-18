#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "../src/core/pearl_source_observability.h"
#include "../src/core/run.h"

namespace {

int failures = 0;

void check(const char *name, bool condition) {
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", name);
    if (!condition) ++failures;
}

class ManualClock final : public om::PearlSourceMonotonicClock {
   public:
    explicit ManualClock(uint64_t initial) : now_(initial) {}
    uint64_t nowMs() noexcept override {
        return now_.load(std::memory_order_relaxed);
    }
    void set(uint64_t value) { now_.store(value, std::memory_order_relaxed); }

   private:
    std::atomic<uint64_t> now_;
};

class IncrementingClock final : public om::PearlSourceMonotonicClock {
   public:
    explicit IncrementingClock(uint64_t initial) : next_(initial) {}
    uint64_t nowMs() noexcept override {
        return next_.fetch_add(1, std::memory_order_relaxed);
    }

   private:
    std::atomic<uint64_t> next_;
};

class DefaultCapabilitySource : public om::JobSource {
   public:
    const char *describe() const override { return "default capability"; }
    bool fetch(om::Job *) override { return false; }
    bool submit(const om::Job &, const om::Solution &,
                std::string *) override {
        return false;
    }
};

class MailboxCapabilitySource final : public DefaultCapabilitySource {
   public:
    explicit MailboxCapabilitySource(om::PearlSourceObservability *mailbox)
        : mailbox_(mailbox) {}
    om::PearlSourceObservability *pearlSourceObservability() override {
        return mailbox_;
    }

   private:
    om::PearlSourceObservability *mailbox_;
};

om::PearlJobIdentityDigest digestWithSeed(uint8_t seed) {
    om::PearlJobIdentityDigest out;
    for (std::size_t i = 0; i < out.bytes.size(); ++i)
        out.bytes[i] = static_cast<uint8_t>(seed + i * 7);
    return out;
}

bool allZero(const om::PearlJobIdentityDigest &digest) {
    for (uint8_t byte : digest.bytes)
        if (byte != 0) return false;
    return true;
}

bool sameEvent(const om::PearlSourceEvent &a,
               const om::PearlSourceEvent &b) {
    return a.eventToken == b.eventToken && a.timestampMs == b.timestampMs &&
           a.sessionToken == b.sessionToken &&
           a.authorizationToken == b.authorizationToken &&
           a.jobToken == b.jobToken &&
           a.operationToken == b.operationToken &&
           a.jobIdentity.bytes == b.jobIdentity.bytes && a.kind == b.kind &&
           a.delivery == b.delivery && a.terminal == b.terminal &&
           a.reason == b.reason &&
           a.terminalEvidence == b.terminalEvidence &&
           a.acknowledged == b.acknowledged &&
           a.superseded == b.superseded;
}

bool samePublishedState(const om::PearlSourceObservabilitySnapshot &a,
                        const om::PearlSourceObservabilitySnapshot &b) {
    return a.failClosed == b.failClosed && a.faultBits == b.faultBits &&
           a.queued == b.queued &&
           a.publishedEvents == b.publishedEvents &&
           a.drainedEvents == b.drainedEvents &&
           a.lastEventToken == b.lastEventToken &&
           a.lastSessionToken == b.lastSessionToken &&
           a.lastAuthorizationToken == b.lastAuthorizationToken &&
           a.lastJobToken == b.lastJobToken &&
           a.lastOperationToken == b.lastOperationToken &&
           a.lastTimestampMs == b.lastTimestampMs &&
           a.activeSessionToken == b.activeSessionToken &&
           a.transportConnected == b.transportConnected &&
           a.activeAuthorizationToken == b.activeAuthorizationToken &&
           a.sessionReady == b.sessionReady &&
           a.sessionReadyWithoutAuthorization ==
               b.sessionReadyWithoutAuthorization &&
           a.retainedPreAuthJobToken == b.retainedPreAuthJobToken &&
           a.retainedPreAuthJobIdentity.bytes ==
               b.retainedPreAuthJobIdentity.bytes &&
           a.activeJobToken == b.activeJobToken &&
           a.activeJobIdentity.bytes == b.activeJobIdentity.bytes &&
           a.issuedJobBound == b.issuedJobBound &&
           a.issuedSessionToken == b.issuedSessionToken &&
           a.issuedAuthorizationToken == b.issuedAuthorizationToken &&
           a.issuedJobToken == b.issuedJobToken &&
           a.issuedJobIdentity.bytes == b.issuedJobIdentity.bytes &&
           a.submitInFlight == b.submitInFlight &&
           a.inFlightOperationToken == b.inFlightOperationToken &&
           a.inFlightAuthorizationToken == b.inFlightAuthorizationToken &&
           a.inFlightJobToken == b.inFlightJobToken &&
           a.inFlightJobIdentity.bytes == b.inFlightJobIdentity.bytes &&
           a.sourceFaultObserved == b.sourceFaultObserved &&
           a.hasLastPublishedEvent == b.hasLastPublishedEvent &&
           sameEvent(a.lastPublishedEvent, b.lastPublishedEvent);
}

bool setupNoAuthSubmit(om::PearlSourceObservabilityMailbox *mailbox,
                       uint64_t *session, uint64_t *job,
                       uint64_t *operation,
                       om::PearlSourceDelivery dispatched) {
    if (!mailbox || !session || !job || !operation) return false;
    *session = mailbox->publishSessionStarted();
    if (*session == 0 || !mailbox->publishTransportConnected(*session) ||
        !mailbox->publishSessionReadyNoAuth(*session))
        return false;
    const om::PearlJobIdentityDigest digest = digestWithSeed(91);
    *job = mailbox->publishJobAvailable(*session, 0, digest);
    if (*job == 0 || !mailbox->bindIssuedJob(*session, *job)) return false;
    *operation = mailbox->publishSubmitDispatched(*session, *job, dispatched);
    return *operation != 0;
}

bool expectedTerminalEvidence(om::PearlSourceDelivery delivery,
                              om::PearlSourceTerminal terminal,
                              bool acknowledged, bool superseded) {
    using namespace om;
    if (superseded && terminal != PearlSourceTerminal::Accepted) return false;
    switch (terminal) {
        case PearlSourceTerminal::LocalFailure:
            return delivery == PearlSourceDelivery::NotSent &&
                   !acknowledged;
        case PearlSourceTerminal::Accepted:
            return delivery == PearlSourceDelivery::Sent && acknowledged;
        case PearlSourceTerminal::Rejected:
        case PearlSourceTerminal::Stale:
        case PearlSourceTerminal::GatewayQueued:
            return delivery == PearlSourceDelivery::Sent && acknowledged &&
                   !superseded;
        case PearlSourceTerminal::Unknown:
            return !superseded &&
                   ((delivery == PearlSourceDelivery::PossiblySent &&
                     !acknowledged) ||
                    delivery == PearlSourceDelivery::Sent);
        case PearlSourceTerminal::None:
            return false;
    }
    return false;
}

}  // namespace

int main() {
    using namespace om;

    static_assert(std::is_standard_layout<PearlSourceEvent>::value,
                  "event schema must remain fixed-layout");
    static_assert(std::is_trivially_copyable<PearlSourceEvent>::value,
                  "event schema must own no strings or buffers");
    static_assert(!std::is_pointer<decltype(PearlSourceEvent::jobIdentity)>::value,
                  "job identity must be inline, not a raw header pointer");
    static_assert(sizeof(PearlJobIdentityDigest) == 32,
                  "job identity must be exactly 32 opaque bytes");
    static_assert(sizeof(PearlSourceEvent) == 88,
                  "adding a field requires explicit schema review");

    ManualClock clock(10);
    PearlSourceObservabilityMailbox mailbox(clock);
    DefaultCapabilitySource defaultSource;
    MailboxCapabilitySource capableSource(&mailbox);
    check("JobSource capability is default-null and opt-in",
          defaultSource.pearlSourceObservability() == nullptr &&
              capableSource.pearlSourceObservability() == &mailbox);

    const PearlJobIdentityDigest digest1 = digestWithSeed(3);
    const PearlJobIdentityDigest digest2 = digestWithSeed(19);
    const uint64_t session = mailbox.publishSessionStarted();
    clock.set(11);
    const bool connected = mailbox.publishTransportConnected(session);
    clock.set(12);
    const uint64_t firstRetainedJob =
        mailbox.publishJobRetainedPreAuth(session, digest1);
    PearlSourceObservabilitySnapshot state = mailbox.snapshot();
    check("pre-authorization job is retained with exact opaque identity",
          session != 0 && connected && firstRetainedJob != 0 &&
              state.transportConnected && !state.sessionReady &&
              state.retainedPreAuthJobToken == firstRetainedJob &&
              state.retainedPreAuthJobIdentity.bytes == digest1.bytes &&
              state.activeJobToken == 0 && state.accountingHolds());
    const PearlSourceObservabilitySnapshot beforeRetainedOverwrite =
        mailbox.snapshot();
    check("pre-auth replacement cannot silently overwrite retained ownership",
          mailbox.publishJobRetainedPreAuth(session, digest2) == 0 &&
              samePublishedState(beforeRetainedOverwrite,
                                 mailbox.snapshot()));
    clock.set(13);
    const bool firstRetired = mailbox.publishJobRetired(
        session, firstRetainedJob, PearlSourceReason::Stale);
    clock.set(14);
    const uint64_t retainedJob =
        mailbox.publishJobRetainedPreAuth(session, digest2);
    check("pre-auth replacement is an explicit retire-then-retain sequence",
          firstRetired && retainedJob > firstRetainedJob &&
              mailbox.snapshot().retainedPreAuthJobToken == retainedJob &&
              mailbox.snapshot().retainedPreAuthJobIdentity.bytes ==
                  digest2.bytes);
    clock.set(15);
    const uint64_t authorization =
        mailbox.publishAuthorizationSucceeded(session);
    clock.set(16);
    const bool retainedAvailable = mailbox.publishRetainedJobAvailable(
        session, authorization, retainedJob);
    const bool issued = mailbox.bindIssuedJob(session, retainedJob);
    const PearlSourceObservabilitySnapshot beforeInvalidDispatch =
        mailbox.snapshot();
    check("dispatch starts only from explicit NotSent delivery evidence",
          mailbox.publishSubmitDispatched(
              session, retainedJob, PearlSourceDelivery::Sent) == 0 &&
              samePublishedState(beforeInvalidDispatch, mailbox.snapshot()));
    clock.set(17);
    const uint64_t operation1 = mailbox.publishSubmitDispatched(
        session, retainedJob, PearlSourceDelivery::NotSent);
    clock.set(18);
    const bool issuedJobRetired = mailbox.publishJobRetired(
        session, retainedJob, PearlSourceReason::Stale);
    clock.set(19);
    const uint64_t replacementJob =
        mailbox.publishJobAvailable(session, authorization, digest1);
    state = mailbox.snapshot();
    check("retired issued job remains bound across active replacement",
          authorization != 0 && retainedAvailable && issued &&
              operation1 != 0 && issuedJobRetired &&
              replacementJob > retainedJob &&
              state.activeJobToken == replacementJob &&
              state.activeJobIdentity.bytes == digest1.bytes &&
              state.issuedJobBound &&
              state.issuedSessionToken == session &&
              state.issuedAuthorizationToken == authorization &&
              state.issuedJobToken == retainedJob &&
              state.issuedJobIdentity.bytes == digest2.bytes &&
              state.submitInFlight &&
              state.inFlightOperationToken == operation1 &&
              state.inFlightAuthorizationToken == authorization &&
              state.inFlightJobToken == retainedJob &&
              state.inFlightJobIdentity.bytes == digest2.bytes);

    const PearlSourceObservabilitySnapshot beforeActiveOverwrite =
        mailbox.snapshot();
    check("available replacement cannot silently overwrite active ownership",
          mailbox.publishJobAvailable(session, authorization, digest2) == 0 &&
              samePublishedState(beforeActiveOverwrite, mailbox.snapshot()));

    const PearlSourceObservabilitySnapshot beforeRefusal = mailbox.snapshot();
    check("disconnect refuses an unterminated submit without state mutation",
          !mailbox.publishDisconnected(session, PearlSourceReason::Transport) &&
              samePublishedState(beforeRefusal, mailbox.snapshot()));
    check("terminal submit rejects nonexistent token and inconsistent evidence",
          !mailbox.publishSubmitTerminal(
              session + 1, operation1, PearlSourceDelivery::Sent,
              PearlSourceTerminal::Accepted, true, false) &&
              !mailbox.publishSubmitTerminal(
              session, operation1 + 1, PearlSourceDelivery::Sent,
              PearlSourceTerminal::Accepted, true, false) &&
              !mailbox.publishSubmitTerminal(
                  session, operation1, PearlSourceDelivery::Sent,
                  PearlSourceTerminal::Accepted, false, false) &&
              samePublishedState(beforeRefusal, mailbox.snapshot()));
    check("possibly-sent acknowledged Unknown is impossible",
          !mailbox.publishSubmitTerminal(
              session, operation1, PearlSourceDelivery::PossiblySent,
              PearlSourceTerminal::Unknown, true, false) &&
              samePublishedState(beforeRefusal, mailbox.snapshot()));
    clock.set(20);
    check("first exact submit terminalizes against retired issued ownership",
          mailbox.publishSubmitTerminal(
              session, operation1, PearlSourceDelivery::Sent,
              PearlSourceTerminal::Accepted, true, true));
    const PearlSourceObservabilitySnapshot afterTerminal = mailbox.snapshot();
    check("duplicate terminal is refused and cannot mutate closed ownership",
          !mailbox.publishSubmitTerminal(
              session, operation1, PearlSourceDelivery::Sent,
              PearlSourceTerminal::Accepted, true, true) &&
              samePublishedState(afterTerminal, mailbox.snapshot()));
    clock.set(21);
    const uint64_t operation2 = mailbox.publishSubmitDispatched(
        session, retainedJob, PearlSourceDelivery::NotSent);
    clock.set(22);
    const bool secondTerminal = mailbox.publishSubmitTerminal(
        session, operation2, PearlSourceDelivery::Sent,
        PearlSourceTerminal::Accepted, true, true);
    const bool issuedReleased = mailbox.releaseIssuedJob(session, retainedJob);
    state = mailbox.snapshot();
    check("second solution keeps the retired issued token and digest",
          operation2 > operation1 && secondTerminal && issuedReleased &&
              !state.issuedJobBound && state.issuedJobToken == 0 &&
              allZero(state.issuedJobIdentity) &&
              state.activeJobToken == replacementJob &&
              state.activeJobIdentity.bytes == digest1.bytes);
    clock.set(24);
    const bool sourceFault =
        mailbox.publishSourceFault(session, PearlSourceReason::Protocol);
    clock.set(25);
    const bool jobRetired = mailbox.publishJobRetired(
        session, replacementJob, PearlSourceReason::Stale);
    const PearlSourceObservabilitySnapshot retiredState = mailbox.snapshot();
    check("job retirement publishes exact identity then clears active ownership",
          jobRetired && retiredState.activeJobToken == 0 &&
              allZero(retiredState.activeJobIdentity) &&
              retiredState.lastPublishedEvent.kind ==
                  PearlSourceEventKind::JobRetired &&
              retiredState.lastPublishedEvent.jobToken == replacementJob &&
              retiredState.lastPublishedEvent.jobIdentity.bytes ==
                  digest1.bytes);
    clock.set(26);
    const bool disconnected =
        mailbox.publishDisconnected(session, PearlSourceReason::Shutdown);
    check("source fault, exact job retirement and disconnect are ordered facts",
          sourceFault && jobRetired && disconnected);

    PearlSourceEvent ordered[16];
    const std::size_t firstDrain = mailbox.drain(ordered, 3);
    const std::size_t secondDrain = mailbox.drain(ordered + firstDrain, 16);
    bool strictOrder = firstDrain == 3 && secondDrain == 13;
    const uint64_t expectedTimestamps[] = {
        10, 11, 12, 13, 14, 15, 16, 17,
        18, 19, 20, 21, 22, 24, 25, 26,
    };
    for (std::size_t i = 0; i < 16; ++i)
        strictOrder = strictOrder && ordered[i].eventToken == i + 1 &&
                      ordered[i].timestampMs == expectedTimestamps[i];
    check("bounded drains preserve exact global enqueue order", strictOrder);
    check("pre-auth replacement and two-solution lifecycle stay ordered",
          ordered[0].kind == PearlSourceEventKind::SessionStarted &&
              ordered[1].kind == PearlSourceEventKind::TransportConnected &&
              ordered[2].kind == PearlSourceEventKind::JobRetainedPreAuth &&
              ordered[3].kind == PearlSourceEventKind::JobRetired &&
              ordered[4].kind == PearlSourceEventKind::JobRetainedPreAuth &&
              ordered[5].kind ==
                  PearlSourceEventKind::AuthorizationSucceeded &&
              ordered[6].kind == PearlSourceEventKind::JobAvailable &&
              ordered[7].kind == PearlSourceEventKind::SubmitDispatched &&
              ordered[8].kind == PearlSourceEventKind::JobRetired &&
              ordered[9].kind == PearlSourceEventKind::JobAvailable &&
              ordered[10].kind == PearlSourceEventKind::SubmitTerminal &&
              ordered[11].kind == PearlSourceEventKind::SubmitDispatched &&
              ordered[12].kind == PearlSourceEventKind::SubmitTerminal &&
              ordered[13].kind == PearlSourceEventKind::SourceFault &&
              ordered[14].kind == PearlSourceEventKind::JobRetired &&
              ordered[15].kind == PearlSourceEventKind::Disconnected);
    check("both terminals retain exact retired issued job and outcome evidence",
          ordered[10].operationToken == operation1 &&
              ordered[12].operationToken == operation2 &&
              ordered[10].jobToken == retainedJob &&
              ordered[12].jobToken == retainedJob &&
              ordered[10].jobIdentity.bytes == digest2.bytes &&
              ordered[12].jobIdentity.bytes == digest2.bytes &&
              ordered[10].delivery == PearlSourceDelivery::Sent &&
              ordered[12].delivery == PearlSourceDelivery::Sent &&
               ordered[10].terminal == PearlSourceTerminal::Accepted &&
               ordered[12].terminal == PearlSourceTerminal::Accepted &&
               ordered[10].terminalEvidence ==
                   PearlSourceTerminalEvidence::CorrelatedRemoteOutcome &&
               ordered[12].terminalEvidence ==
                   PearlSourceTerminalEvidence::CorrelatedRemoteOutcome &&
              ordered[10].acknowledged == 1 &&
              ordered[12].acknowledged == 1 &&
              ordered[10].superseded == 1 && ordered[12].superseded == 1);
    check("secret-minimal schema uses digest only on job-owned facts",
          allZero(ordered[0].jobIdentity) &&
              allZero(ordered[1].jobIdentity) &&
              ordered[2].jobIdentity.bytes == digest1.bytes &&
              ordered[3].jobIdentity.bytes == digest1.bytes &&
              ordered[4].jobIdentity.bytes == digest2.bytes &&
              ordered[6].jobIdentity.bytes == digest2.bytes &&
              ordered[7].jobIdentity.bytes == digest2.bytes &&
              ordered[8].jobIdentity.bytes == digest2.bytes &&
              ordered[9].jobIdentity.bytes == digest1.bytes &&
              ordered[10].jobIdentity.bytes == digest2.bytes &&
              ordered[11].jobIdentity.bytes == digest2.bytes &&
              ordered[12].jobIdentity.bytes == digest2.bytes &&
              ordered[13].jobIdentity.bytes == digest1.bytes &&
              ordered[14].jobIdentity.bytes == digest1.bytes &&
              allZero(ordered[15].jobIdentity));
    state = mailbox.snapshot();
    check("drain accounting and disconnected state remain coherent",
          state.queued == 0 && state.publishedEvents == 16 &&
              state.drainedEvents == 16 && state.activeSessionToken == 0 &&
              !state.transportConnected && !state.sessionReady &&
              !state.issuedJobBound && !state.submitInFlight &&
              state.accountingHolds());

    ManualClock noAuthClock(40);
    PearlSourceObservabilityMailbox noAuth(noAuthClock);
    const uint64_t noAuthSession = noAuth.publishSessionStarted();
    const bool noAuthConnected =
        noAuth.publishTransportConnected(noAuthSession);
    const bool readyNoAuth = noAuth.publishSessionReadyNoAuth(noAuthSession);
    const uint64_t noAuthJob =
        noAuth.publishJobAvailable(noAuthSession, 0, digest1);
    check("explicit no-authorization readiness supports exact jobs",
          noAuthConnected && readyNoAuth && noAuthJob != 0 &&
              noAuth.snapshot().sessionReadyWithoutAuthorization &&
              noAuth.snapshot().activeAuthorizationToken == 0);

    ManualClock retainedDisconnectClock(50);
    PearlSourceObservabilityMailbox retainedDisconnect(
        retainedDisconnectClock);
    const uint64_t retainedDisconnectSession =
        retainedDisconnect.publishSessionStarted();
    const bool retainedDisconnectConnected =
        retainedDisconnect.publishTransportConnected(
            retainedDisconnectSession);
    const uint64_t retainedDisconnectJob =
        retainedDisconnect.publishJobRetainedPreAuth(
            retainedDisconnectSession, digest1);
    const PearlSourceObservabilitySnapshot retainedBeforeDisconnect =
        retainedDisconnect.snapshot();
    const bool retainedDisconnectRefused =
        !retainedDisconnect.publishDisconnected(
            retainedDisconnectSession, PearlSourceReason::Transport) &&
        samePublishedState(retainedBeforeDisconnect,
                           retainedDisconnect.snapshot());
    const bool noReasonRetireRefused =
        !retainedDisconnect.publishJobRetired(
            retainedDisconnectSession, retainedDisconnectJob,
            PearlSourceReason::None) &&
        samePublishedState(retainedBeforeDisconnect,
                           retainedDisconnect.snapshot());
    const bool retainedClosed =
        retainedDisconnect.publishJobRetired(
            retainedDisconnectSession, retainedDisconnectJob,
            PearlSourceReason::Stale);
    const PearlSourceObservabilitySnapshot beforeReasonlessDisconnect =
        retainedDisconnect.snapshot();
    check("disconnect cannot erase retained ownership or omit a reason",
          retainedDisconnectConnected && retainedDisconnectJob != 0 &&
              retainedDisconnectRefused && noReasonRetireRefused &&
              retainedClosed &&
              !retainedDisconnect.publishDisconnected(
                  retainedDisconnectSession, PearlSourceReason::None) &&
              samePublishedState(beforeReasonlessDisconnect,
                                 retainedDisconnect.snapshot()) &&
              retainedDisconnect.publishDisconnected(
                  retainedDisconnectSession, PearlSourceReason::Shutdown));

    ManualClock activeDisconnectClock(51);
    PearlSourceObservabilityMailbox activeDisconnect(activeDisconnectClock);
    const uint64_t activeDisconnectSession =
        activeDisconnect.publishSessionStarted();
    const bool activeReady =
        activeDisconnect.publishTransportConnected(activeDisconnectSession) &&
        activeDisconnect.publishSessionReadyNoAuth(activeDisconnectSession);
    const uint64_t activeDisconnectJob = activeDisconnect.publishJobAvailable(
        activeDisconnectSession, 0, digest1);
    const PearlSourceObservabilitySnapshot activeBeforeDisconnect =
        activeDisconnect.snapshot();
    check("disconnect cannot silently erase an active unissued job",
          activeReady && activeDisconnectJob != 0 &&
              !activeDisconnect.publishDisconnected(
                  activeDisconnectSession, PearlSourceReason::Transport) &&
              samePublishedState(activeBeforeDisconnect,
                                 activeDisconnect.snapshot()) &&
              activeDisconnect.publishJobRetired(
                  activeDisconnectSession, activeDisconnectJob,
                  PearlSourceReason::Shutdown) &&
              activeDisconnect.publishDisconnected(
                  activeDisconnectSession, PearlSourceReason::Shutdown));

    ManualClock issuedDisconnectClock(52);
    PearlSourceObservabilityMailbox issuedDisconnect(issuedDisconnectClock);
    const uint64_t issuedDisconnectSession =
        issuedDisconnect.publishSessionStarted();
    const bool issuedReady =
        issuedDisconnect.publishTransportConnected(issuedDisconnectSession) &&
        issuedDisconnect.publishSessionReadyNoAuth(issuedDisconnectSession);
    const uint64_t issuedDisconnectJob = issuedDisconnect.publishJobAvailable(
        issuedDisconnectSession, 0, digest1);
    const bool issuedBound =
        issuedDisconnect.bindIssuedJob(issuedDisconnectSession,
                                       issuedDisconnectJob);
    const bool issuedActiveRetired = issuedDisconnect.publishJobRetired(
        issuedDisconnectSession, issuedDisconnectJob,
        PearlSourceReason::Shutdown);
    const PearlSourceObservabilitySnapshot issuedBeforeDisconnect =
        issuedDisconnect.snapshot();
    check("disconnect cannot silently erase retired-but-issued ownership",
          issuedReady && issuedDisconnectJob != 0 && issuedBound &&
              issuedActiveRetired && issuedBeforeDisconnect.issuedJobBound &&
              issuedBeforeDisconnect.activeJobToken == 0 &&
              !issuedDisconnect.publishDisconnected(
                  issuedDisconnectSession, PearlSourceReason::Transport) &&
              samePublishedState(issuedBeforeDisconnect,
                                 issuedDisconnect.snapshot()) &&
              issuedDisconnect.releaseIssuedJob(
                  issuedDisconnectSession, issuedDisconnectJob) &&
              issuedDisconnect.publishDisconnected(
                  issuedDisconnectSession, PearlSourceReason::Shutdown));

    const PearlSourceDelivery deliveries[] = {
        PearlSourceDelivery::None, PearlSourceDelivery::NotSent,
        PearlSourceDelivery::PossiblySent, PearlSourceDelivery::Sent,
    };
    const PearlSourceTerminal terminals[] = {
        PearlSourceTerminal::None,       PearlSourceTerminal::LocalFailure,
        PearlSourceTerminal::Accepted,   PearlSourceTerminal::Rejected,
        PearlSourceTerminal::Stale,      PearlSourceTerminal::Unknown,
        PearlSourceTerminal::GatewayQueued,
    };
    bool terminalMatrix = true;
    std::size_t legalTerminalCases = 0;
    for (PearlSourceDelivery delivery : deliveries) {
        for (PearlSourceTerminal terminal : terminals) {
            for (int acknowledgedValue = 0; acknowledgedValue <= 1;
                 ++acknowledgedValue) {
                for (int supersededValue = 0; supersededValue <= 1;
                     ++supersededValue) {
                    const bool acknowledged = acknowledgedValue != 0;
                    const bool superseded = supersededValue != 0;
                    const bool expected = expectedTerminalEvidence(
                        delivery, terminal, acknowledged, superseded);
                    ManualClock terminalClock(60);
                    PearlSourceObservabilityMailbox terminalMailbox(
                        terminalClock);
                    uint64_t terminalSession = 0, terminalJob = 0;
                    uint64_t terminalOperation = 0;
                    bool oneCase = setupNoAuthSubmit(
                        &terminalMailbox, &terminalSession, &terminalJob,
                        &terminalOperation, PearlSourceDelivery::NotSent);
                    const PearlSourceObservabilitySnapshot beforeTerminal =
                        terminalMailbox.snapshot();
                    const bool observed =
                        terminalMailbox.publishSubmitTerminal(
                            terminalSession, terminalOperation, delivery,
                            terminal, acknowledged, superseded);
                    oneCase = oneCase && observed == expected;
                    if (expected) {
                        ++legalTerminalCases;
                        oneCase =
                            oneCase &&
                            !terminalMailbox.publishSubmitTerminal(
                                terminalSession, terminalOperation, delivery,
                                terminal, acknowledged, superseded);
                    } else {
                        oneCase =
                            oneCase &&
                            samePublishedState(beforeTerminal,
                                               terminalMailbox.snapshot()) &&
                            terminalMailbox.publishSubmitTerminal(
                                terminalSession, terminalOperation,
                                PearlSourceDelivery::NotSent,
                                PearlSourceTerminal::LocalFailure, false,
                                false);
                    }
                    oneCase =
                        oneCase &&
                        terminalMailbox.releaseIssuedJob(terminalSession,
                                                         terminalJob) &&
                        terminalMailbox.publishJobRetired(
                            terminalSession, terminalJob,
                            PearlSourceReason::Shutdown) &&
                        terminalMailbox.publishDisconnected(
                            terminalSession, PearlSourceReason::Shutdown);
                    terminalMatrix = terminalMatrix && oneCase;
                }
            }
        }
    }
    check("exhaustive terminal evidence matrix accepts only nine legal forms",
          terminalMatrix && legalTerminalCases == 9);

    ManualClock unavailableClock(80);
    PearlSourceObservabilityMailbox unavailableMailbox(unavailableClock);
    uint64_t unavailableSession = 0, unavailableJob = 0;
    uint64_t unavailableOperation = 0;
    const bool unavailableSetup = setupNoAuthSubmit(
        &unavailableMailbox, &unavailableSession, &unavailableJob,
        &unavailableOperation, PearlSourceDelivery::NotSent);
    const bool unavailablePublished = unavailableMailbox.publishSubmitTerminal(
        unavailableSession, unavailableOperation, PearlSourceDelivery::Sent,
        PearlSourceTerminal::Unknown, true, false, PearlSourceReason::None,
        PearlSourceTerminalEvidence::AcknowledgedOutcomeUnavailable);
    const PearlSourceObservabilitySnapshot unavailableState =
        unavailableMailbox.snapshot();
    check("acknowledged-unavailable is an explicit canonical producer fact",
          unavailableSetup && unavailablePublished &&
              unavailableState.lastPublishedEvent.terminalEvidence ==
                  PearlSourceTerminalEvidence::
                      AcknowledgedOutcomeUnavailable);

    ManualClock crossClassClock(90);
    PearlSourceObservabilityMailbox crossClassMailbox(crossClassClock);
    uint64_t crossSession = 0, crossJob = 0, crossOperation = 0;
    const bool crossSetup = setupNoAuthSubmit(
        &crossClassMailbox, &crossSession, &crossJob, &crossOperation,
        PearlSourceDelivery::NotSent);
    const PearlSourceObservabilitySnapshot beforeCrossClass =
        crossClassMailbox.snapshot();
    const bool crossClassRejected =
        !crossClassMailbox.publishSubmitTerminal(
            crossSession, crossOperation, PearlSourceDelivery::Sent,
            PearlSourceTerminal::Accepted, true, false,
            PearlSourceReason::None,
            PearlSourceTerminalEvidence::AcknowledgedOutcomeUnavailable) &&
        samePublishedState(beforeCrossClass, crossClassMailbox.snapshot());
    const bool unknownEnumRejected =
        !crossClassMailbox.publishSubmitTerminal(
            crossSession, crossOperation, PearlSourceDelivery::Sent,
            PearlSourceTerminal::Unknown, true, false,
            PearlSourceReason::None,
            static_cast<PearlSourceTerminalEvidence>(255)) &&
        samePublishedState(beforeCrossClass, crossClassMailbox.snapshot());
    check("cross-class and unknown terminal evidence fail closed",
          crossSetup && crossClassRejected && unknownEnumRejected &&
              crossClassMailbox.publishSubmitTerminal(
                  crossSession, crossOperation, PearlSourceDelivery::Sent,
                  PearlSourceTerminal::Unknown, true, false,
                  PearlSourceReason::None,
                  PearlSourceTerminalEvidence::CorrelatedRemoteOutcome));

    ManualClock overflowClock(100);
    PearlSourceObservabilityMailbox overflow(overflowClock);
    const uint64_t overflowSession = overflow.publishSessionStarted();
    bool filledExactly = overflowSession != 0;
    for (std::size_t i = 1;
         i < PearlSourceObservabilityMailbox::kCapacity; ++i) {
        filledExactly = filledExactly &&
                        overflow.publishSourceFault(
                            overflowSession, PearlSourceReason::Protocol);
    }
    const bool refusedAtCapacity = overflow.publishSourceFault(
        overflowSession, PearlSourceReason::Protocol);
    PearlSourceObservabilitySnapshot overflowed = overflow.snapshot();
    check("capacity plus one latches overflow without overwrite",
          filledExactly && !refusedAtCapacity && overflowed.failClosed &&
              overflowed.hasFault(PearlSourceMailboxFault::Overflow) &&
              overflowed.queued ==
                  PearlSourceObservabilityMailbox::kCapacity &&
              overflowed.publishedEvents ==
                  PearlSourceObservabilityMailbox::kCapacity &&
              overflowed.lastEventToken ==
                  PearlSourceObservabilityMailbox::kCapacity &&
              overflowed.accountingHolds());
    std::vector<PearlSourceEvent> overflowEvents(
        PearlSourceObservabilityMailbox::kCapacity);
    const std::size_t overflowDrained =
        overflow.drain(overflowEvents.data(), overflowEvents.size());
    bool noOverwrite = overflowDrained == overflowEvents.size();
    for (std::size_t i = 0; i < overflowEvents.size(); ++i)
        noOverwrite = noOverwrite && overflowEvents[i].eventToken == i + 1;
    check("overflow preserves earlier events and remains fail-closed",
          noOverwrite &&
              !overflow.publishSourceFault(
                  overflowSession, PearlSourceReason::Protocol) &&
              overflow.snapshot().queued == 0 &&
              overflow.snapshot().accountingHolds());

    ManualClock rollbackClock(200);
    PearlSourceObservabilityMailbox rollback(rollbackClock);
    const uint64_t rollbackSession = rollback.publishSessionStarted();
    rollbackClock.set(199);
    const bool rollbackEvent =
        rollback.publishTransportConnected(rollbackSession);
    const PearlSourceObservabilitySnapshot rolledBack = rollback.snapshot();
    rollbackClock.set(201);
    check("timestamp rollback is out-of-band, latched, and fail-closed",
          !rollbackEvent && rolledBack.failClosed &&
              rolledBack.hasFault(
                  PearlSourceMailboxFault::TimestampRollback) &&
              rolledBack.lastTimestampMs == 200 &&
              rolledBack.lastEventToken == 1 && rolledBack.queued == 1 &&
              !rollback.publishSourceFault(
                  rollbackSession, PearlSourceReason::Protocol));

    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    ManualClock exhaustionClock(300);
    PearlSourceTokenSeeds eventSeed;
    eventSeed.event = maximum - 1;
    PearlSourceObservabilityMailbox eventExhaustion(exhaustionClock,
                                                    eventSeed);
    const uint64_t finalEventSession =
        eventExhaustion.publishSessionStarted();
    const bool wrappedEvent = eventExhaustion.publishSourceFault(
        finalEventSession, PearlSourceReason::Protocol);
    const PearlSourceObservabilitySnapshot eventExhausted =
        eventExhaustion.snapshot();
    check("event token reaches UINT64_MAX once and never wraps or aliases",
          finalEventSession != 0 && !wrappedEvent &&
              eventExhausted.lastEventToken == maximum &&
              eventExhausted.publishedEvents == 1 &&
              eventExhausted.hasFault(
                  PearlSourceMailboxFault::TokenExhaustion));

    PearlSourceTokenSeeds sessionSeed;
    sessionSeed.session = maximum;
    PearlSourceObservabilityMailbox sessionExhaustion(exhaustionClock,
                                                      sessionSeed);
    PearlSourceTokenSeeds authorizationSeed;
    authorizationSeed.authorization = maximum;
    PearlSourceObservabilityMailbox authorizationExhaustion(exhaustionClock,
                                                            authorizationSeed);
    const uint64_t authorizationSession =
        authorizationExhaustion.publishSessionStarted();
    const bool authorizationConnected =
        authorizationExhaustion.publishTransportConnected(
            authorizationSession);
    PearlSourceTokenSeeds jobSeed;
    jobSeed.job = maximum;
    PearlSourceObservabilityMailbox jobExhaustion(exhaustionClock, jobSeed);
    const uint64_t jobSession = jobExhaustion.publishSessionStarted();
    const bool jobConnected =
        jobExhaustion.publishTransportConnected(jobSession);
    const bool jobReady = jobExhaustion.publishSessionReadyNoAuth(jobSession);
    check("session, authorization, and job tokens fail closed before wrap",
          sessionExhaustion.publishSessionStarted() == 0 &&
              sessionExhaustion.snapshot().hasFault(
                  PearlSourceMailboxFault::TokenExhaustion) &&
              authorizationConnected &&
              authorizationExhaustion.publishAuthorizationSucceeded(
                  authorizationSession) == 0 &&
              authorizationExhaustion.snapshot().hasFault(
                  PearlSourceMailboxFault::TokenExhaustion) &&
              jobConnected && jobReady &&
              jobExhaustion.publishJobAvailable(jobSession, 0, digest1) == 0 &&
              jobExhaustion.snapshot().hasFault(
                  PearlSourceMailboxFault::TokenExhaustion));

    PearlSourceTokenSeeds operationSeed;
    operationSeed.operation = maximum - 1;
    PearlSourceObservabilityMailbox operationExhaustion(exhaustionClock,
                                                        operationSeed);
    uint64_t operationSession = 0, operationJob = 0, finalOperation = 0;
    const bool operationSetup = setupNoAuthSubmit(
        &operationExhaustion, &operationSession, &operationJob,
        &finalOperation, PearlSourceDelivery::NotSent);
    const bool finalOperationClosed =
        operationExhaustion.publishSubmitTerminal(
            operationSession, finalOperation, PearlSourceDelivery::NotSent,
            PearlSourceTerminal::LocalFailure, false, false);
    const uint64_t wrappedOperation =
        operationExhaustion.publishSubmitDispatched(
            operationSession, operationJob, PearlSourceDelivery::NotSent);
    const PearlSourceObservabilitySnapshot operationExhausted =
        operationExhaustion.snapshot();
    check("operation token reaches UINT64_MAX once and never wraps or aliases",
          operationSetup && finalOperation == maximum &&
              finalOperationClosed && wrappedOperation == 0 &&
              operationExhausted.lastOperationToken == maximum &&
              operationExhausted.hasFault(
                  PearlSourceMailboxFault::TokenExhaustion));
    const bool exhaustedCleanup = operationExhaustion.releaseIssuedJob(
        operationSession, operationJob);
    check("fail-closed mailbox permits cleanup but binds no new issued work",
          exhaustedCleanup &&
              !operationExhaustion.bindIssuedJob(operationSession,
                                                  operationJob) &&
              !operationExhaustion.snapshot().issuedJobBound);

    IncrementingClock concurrentClock(1000);
    PearlSourceObservabilityMailbox concurrent(concurrentClock);
    const uint64_t concurrentSession = concurrent.publishSessionStarted();
    PearlSourceEvent setupEvent;
    check("concurrent fixture drains its setup event",
          concurrentSession != 0 && concurrent.drain(&setupEvent, 1) == 1);

    constexpr std::size_t kProducerCount = 3;
    constexpr std::size_t kEventsPerProducer = 60;
    std::atomic<bool> start{false};
    std::atomic<bool> firstPublished{false};
    std::atomic<bool> firstConsumed{false};
    std::atomic<bool> producerFailure{false};
    std::atomic<std::size_t> producersRemaining{kProducerCount};
    std::vector<std::thread> producers;
    for (std::size_t producer = 0; producer < kProducerCount; ++producer) {
        producers.emplace_back([&, producer]() {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (std::size_t i = 0; i < kEventsPerProducer; ++i) {
                if (!concurrent.publishSourceFault(
                        concurrentSession, PearlSourceReason::Protocol))
                    producerFailure.store(true, std::memory_order_relaxed);
                if (producer == 0 && i == 0) {
                    firstPublished.store(true, std::memory_order_release);
                    while (!firstConsumed.load(std::memory_order_acquire))
                        std::this_thread::yield();
                }
                std::this_thread::yield();
            }
            producersRemaining.fetch_sub(1, std::memory_order_release);
        });
    }

    std::vector<PearlSourceEvent> concurrentEvents;
    concurrentEvents.reserve(kProducerCount * kEventsPerProducer);
    bool coherentSnapshots = true;
    bool observedWhileProducerActive = false;
    std::thread consumer([&]() {
        while (!firstPublished.load(std::memory_order_acquire))
            std::this_thread::yield();
        while (producersRemaining.load(std::memory_order_acquire) != 0 ||
               concurrent.snapshot().queued != 0) {
            PearlSourceEvent batch[17];
            const std::size_t count = concurrent.drain(batch, 17);
            if (count != 0 &&
                producersRemaining.load(std::memory_order_acquire) != 0)
                observedWhileProducerActive = true;
            for (std::size_t i = 0; i < count; ++i)
                concurrentEvents.push_back(batch[i]);
            coherentSnapshots = coherentSnapshots &&
                                concurrent.snapshot().accountingHolds();
            if (!firstConsumed.load(std::memory_order_relaxed) && count != 0)
                firstConsumed.store(true, std::memory_order_release);
            std::this_thread::yield();
        }
    });
    start.store(true, std::memory_order_release);
    for (std::thread &producer : producers) producer.join();
    consumer.join();

    bool concurrentOrder =
        concurrentEvents.size() == kProducerCount * kEventsPerProducer;
    for (std::size_t i = 0; i < concurrentEvents.size(); ++i) {
        concurrentOrder = concurrentOrder &&
                          concurrentEvents[i].eventToken == i + 2 &&
                          concurrentEvents[i].kind ==
                              PearlSourceEventKind::SourceFault;
    }
    const PearlSourceObservabilitySnapshot concurrentFinal =
        concurrent.snapshot();
    check("concurrent producers and one consumer retain strict total order",
          !producerFailure.load(std::memory_order_relaxed) &&
              observedWhileProducerActive && concurrentOrder &&
              coherentSnapshots && !concurrentFinal.failClosed &&
              concurrentFinal.queued == 0 &&
              concurrentFinal.accountingHolds());

    if (failures) {
        std::printf("FAIL: %d Pearl source-observability checks failed\n",
                    failures);
        return 1;
    }
    std::puts("PASS: Pearl source observability is bounded and ordered");
    return 0;
}
