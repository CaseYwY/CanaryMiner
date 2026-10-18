// Pure-host, bounded source event transport for Pearl observability.
//
// No socket, CUDA object, endpoint, proof, header, wallet, or worker thread is
// owned here. A source publishes fixed-schema lifecycle facts; one observer
// drains them in exact enqueue order and may read one coherent state snapshot.

#pragma once

#if defined(CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT) && CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT != 1
#error "CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT must be exactly 1 or undefined"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <type_traits>

namespace om {

class PearlSourceMonotonicClock {
   public:
    virtual ~PearlSourceMonotonicClock() = default;
    virtual uint64_t nowMs() noexcept = 0;
};

struct PearlJobIdentityDigest {
    std::array<uint8_t, 32> bytes{};
};

enum class PearlSourceEventKind : uint8_t {
    None,
    SessionStarted,
    TransportConnected,
    AuthorizationSucceeded,
    SessionReadyNoAuth,
    JobRetainedPreAuth,
    JobAvailable,
    JobRetired,
    SubmitDispatched,
    SubmitTerminal,
    Disconnected,
    SourceFault,
};

enum class PearlSourceDelivery : uint8_t {
    None,
    NotSent,
    PossiblySent,
    Sent,
};

enum class PearlSourceTerminal : uint8_t {
    None,
    LocalFailure,
    Accepted,
    Rejected,
    Stale,
    Unknown,
    GatewayQueued,
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    GuardRetired,
#endif
};

enum class PearlSourceReason : uint8_t {
    None,
    Transport,
    Protocol,
    Authentication,
    Timeout,
    RemoteRejected,
    Stale,
    InvalidJob,
    Shutdown,
};

/** Canonical producer fact for the otherwise ambiguous terminal tuple. */
enum class PearlSourceTerminalEvidence : uint8_t {
    None,
    LocalPreDeliveryFailure,
    PossiblyDeliveredUnknown,
    FullySentNoReply,
    GatewayAcknowledgementOnly,
    AcknowledgedOutcomeUnavailable,
    CorrelatedRemoteOutcome,
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    ConfirmedGuardNonForwarding,
#endif
};

/** One shared, reason-aware terminal schema for producers and consumers. */
inline bool pearlSourceTerminalEvidenceValid(
    PearlSourceDelivery delivery, PearlSourceTerminal terminal,
    bool acknowledged, bool superseded, PearlSourceReason reason) {
    if (delivery > PearlSourceDelivery::Sent ||
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        terminal > PearlSourceTerminal::GuardRetired ||
#else
        terminal > PearlSourceTerminal::GatewayQueued ||
#endif
        reason > PearlSourceReason::Shutdown)
        return false;
    if (superseded && terminal != PearlSourceTerminal::Accepted) return false;
    switch (terminal) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        case PearlSourceTerminal::GuardRetired:
            return delivery == PearlSourceDelivery::Sent && !acknowledged &&
                   !superseded && reason == PearlSourceReason::Stale;
#endif
        case PearlSourceTerminal::LocalFailure:
            return delivery == PearlSourceDelivery::NotSent &&
                   !acknowledged && !superseded &&
                   reason != PearlSourceReason::None;
        case PearlSourceTerminal::Accepted:
            return delivery == PearlSourceDelivery::Sent && acknowledged &&
                   reason == PearlSourceReason::None;
        case PearlSourceTerminal::Rejected:
        case PearlSourceTerminal::Stale:
        case PearlSourceTerminal::GatewayQueued:
            return delivery == PearlSourceDelivery::Sent && acknowledged &&
                   !superseded && reason == PearlSourceReason::None;
        case PearlSourceTerminal::Unknown:
            if (superseded) return false;
            if (delivery == PearlSourceDelivery::PossiblySent)
                return !acknowledged && reason != PearlSourceReason::None;
            if (delivery != PearlSourceDelivery::Sent) return false;
            if (acknowledged) return reason == PearlSourceReason::None;
            return reason == PearlSourceReason::Timeout ||
                   reason == PearlSourceReason::Transport ||
                   reason == PearlSourceReason::Protocol;
        case PearlSourceTerminal::None:
            return false;
    }
    return false;
}

inline PearlSourceReason pearlSourceCanonicalTerminalReason(
    PearlSourceDelivery delivery, PearlSourceTerminal terminal,
    bool acknowledged) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    if (terminal == PearlSourceTerminal::GuardRetired) return PearlSourceReason::Stale;
#endif
    if (terminal == PearlSourceTerminal::LocalFailure ||
        delivery == PearlSourceDelivery::PossiblySent)
        return PearlSourceReason::Transport;
    if (terminal == PearlSourceTerminal::Unknown &&
        delivery == PearlSourceDelivery::Sent && !acknowledged)
        return PearlSourceReason::Timeout;
    return PearlSourceReason::None;
}

inline PearlSourceTerminalEvidence pearlSourceCanonicalTerminalEvidence(
    PearlSourceDelivery delivery, PearlSourceTerminal terminal,
    bool acknowledged) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    if (terminal == PearlSourceTerminal::GuardRetired)
        return PearlSourceTerminalEvidence::ConfirmedGuardNonForwarding;
#endif
    if (terminal == PearlSourceTerminal::LocalFailure)
        return PearlSourceTerminalEvidence::LocalPreDeliveryFailure;
    if (delivery == PearlSourceDelivery::PossiblySent)
        return PearlSourceTerminalEvidence::PossiblyDeliveredUnknown;
    if (terminal == PearlSourceTerminal::Unknown && !acknowledged)
        return PearlSourceTerminalEvidence::FullySentNoReply;
    if (terminal == PearlSourceTerminal::GatewayQueued)
        return PearlSourceTerminalEvidence::GatewayAcknowledgementOnly;
    return PearlSourceTerminalEvidence::CorrelatedRemoteOutcome;
}

inline bool pearlSourceTerminalFactValid(
    PearlSourceDelivery delivery, PearlSourceTerminal terminal,
    bool acknowledged, bool superseded, PearlSourceReason reason,
    PearlSourceTerminalEvidence evidence) {
    if (!pearlSourceTerminalEvidenceValid(delivery, terminal, acknowledged,
                                          superseded, reason))
        return false;
    switch (evidence) {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
        case PearlSourceTerminalEvidence::ConfirmedGuardNonForwarding:
            return terminal == PearlSourceTerminal::GuardRetired;
#endif
        case PearlSourceTerminalEvidence::LocalPreDeliveryFailure:
            return terminal == PearlSourceTerminal::LocalFailure;
        case PearlSourceTerminalEvidence::PossiblyDeliveredUnknown:
            return delivery == PearlSourceDelivery::PossiblySent &&
                   terminal == PearlSourceTerminal::Unknown &&
                   !acknowledged;
        case PearlSourceTerminalEvidence::FullySentNoReply:
            return delivery == PearlSourceDelivery::Sent &&
                   terminal == PearlSourceTerminal::Unknown &&
                   !acknowledged;
        case PearlSourceTerminalEvidence::GatewayAcknowledgementOnly:
            return terminal == PearlSourceTerminal::GatewayQueued;
        case PearlSourceTerminalEvidence::AcknowledgedOutcomeUnavailable:
            return delivery == PearlSourceDelivery::Sent &&
                   terminal == PearlSourceTerminal::Unknown && acknowledged &&
                   reason == PearlSourceReason::None;
        case PearlSourceTerminalEvidence::CorrelatedRemoteOutcome:
            return delivery == PearlSourceDelivery::Sent && acknowledged &&
                   reason == PearlSourceReason::None &&
                   (terminal == PearlSourceTerminal::Accepted ||
                    terminal == PearlSourceTerminal::Rejected ||
                    terminal == PearlSourceTerminal::Stale ||
                    terminal == PearlSourceTerminal::Unknown);
        case PearlSourceTerminalEvidence::None:
            return false;
    }
    return false;
}

enum class PearlSourceMailboxFault : uint8_t {
    None = 0,
    Overflow = 1,
    TokenExhaustion = 2,
    TimestampRollback = 4,
};

inline uint8_t pearlSourceMailboxFaultBit(PearlSourceMailboxFault fault) {
    return static_cast<uint8_t>(fault);
}

struct PearlSourceEvent {
    uint64_t eventToken = 0;
    uint64_t timestampMs = 0;
    uint64_t sessionToken = 0;
    uint64_t authorizationToken = 0;
    uint64_t jobToken = 0;
    uint64_t operationToken = 0;
    PearlJobIdentityDigest jobIdentity;
    PearlSourceEventKind kind = PearlSourceEventKind::None;
    PearlSourceDelivery delivery = PearlSourceDelivery::None;
    PearlSourceTerminal terminal = PearlSourceTerminal::None;
    PearlSourceReason reason = PearlSourceReason::None;
    PearlSourceTerminalEvidence terminalEvidence =
        PearlSourceTerminalEvidence::None;
    uint8_t acknowledged = 0;
    uint8_t superseded = 0;
    uint8_t reserved1 = 0;
};

static_assert(std::is_standard_layout<PearlSourceEvent>::value,
              "source events must retain a fixed field layout");
static_assert(std::is_trivially_copyable<PearlSourceEvent>::value,
              "source events must not acquire dynamic ownership");
static_assert(sizeof(PearlJobIdentityDigest) == 32,
              "job identity must remain an opaque 32-byte digest");
static_assert(sizeof(PearlSourceEvent) == 88,
              "adding an event field requires an explicit schema review");

struct PearlSourceObservabilitySnapshot {
    bool failClosed = false;
    uint8_t faultBits = 0;
    std::size_t queued = 0;
    uint64_t publishedEvents = 0;
    uint64_t drainedEvents = 0;
    uint64_t refusedEvents = 0;
    uint64_t lastEventToken = 0;
    uint64_t lastSessionToken = 0;
    uint64_t lastAuthorizationToken = 0;
    uint64_t lastJobToken = 0;
    uint64_t lastOperationToken = 0;
    uint64_t lastTimestampMs = 0;
    uint64_t activeSessionToken = 0;
    bool transportConnected = false;
    uint64_t activeAuthorizationToken = 0;
    bool sessionReady = false;
    bool sessionReadyWithoutAuthorization = false;
    uint64_t retainedPreAuthJobToken = 0;
    PearlJobIdentityDigest retainedPreAuthJobIdentity;
    uint64_t activeJobToken = 0;
    PearlJobIdentityDigest activeJobIdentity;
    bool issuedJobBound = false;
    uint64_t issuedSessionToken = 0;
    uint64_t issuedAuthorizationToken = 0;
    uint64_t issuedJobToken = 0;
    PearlJobIdentityDigest issuedJobIdentity;
    bool submitInFlight = false;
    uint64_t inFlightOperationToken = 0;
    uint64_t inFlightAuthorizationToken = 0;
    uint64_t inFlightJobToken = 0;
    PearlJobIdentityDigest inFlightJobIdentity;
    bool sourceFaultObserved = false;
    bool hasLastPublishedEvent = false;
    PearlSourceEvent lastPublishedEvent;

    bool hasFault(PearlSourceMailboxFault fault) const {
        return (faultBits & pearlSourceMailboxFaultBit(fault)) != 0;
    }

    bool accountingHolds() const {
        if (queued > publishedEvents || drainedEvents > publishedEvents)
            return false;
        return queued == publishedEvents - drainedEvents;
    }
};

/** Read-only capability exposed by a supporting JobSource. */
class PearlSourceObservability {
   public:
    virtual ~PearlSourceObservability() = default;
    virtual std::size_t drain(PearlSourceEvent *out,
                              std::size_t maximum) = 0;
    virtual PearlSourceObservabilitySnapshot snapshot() const = 0;
};

/** Optional seeds permit deterministic exhaustion tests and sequence restore. */
struct PearlSourceTokenSeeds {
    uint64_t event = 0;
    uint64_t session = 0;
    uint64_t authorization = 0;
    uint64_t job = 0;
    uint64_t operation = 0;
};

class PearlSourceObservabilityMailbox final
    : public PearlSourceObservability {
   public:
    static constexpr std::size_t kCapacity = 256;

    explicit PearlSourceObservabilityMailbox(
        PearlSourceMonotonicClock &clock,
        const PearlSourceTokenSeeds &seeds = PearlSourceTokenSeeds())
        : clock_(clock),
          lastEventToken_(seeds.event),
          lastSessionToken_(seeds.session),
          lastAuthorizationToken_(seeds.authorization),
          lastJobToken_(seeds.job),
          lastOperationToken_(seeds.operation) {}

    uint64_t publishSessionStarted() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (activeSessionToken_ != 0) return refuseInvalidLocked();
        PearlSourceEvent event;
        event.kind = PearlSourceEventKind::SessionStarted;
        uint64_t token = 0;
        if (!prepareLocked(TokenDomain::Session, &event, &token)) return 0;
        event.sessionToken = token;
        enqueueLocked(event);
        resetSessionStateLocked();
        activeSessionToken_ = token;
        return token;
    }

    bool publishTransportConnected(uint64_t sessionToken) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validSessionLocked(sessionToken) || transportConnected_)
            return refuseInvalidBooleanLocked();
        PearlSourceEvent event;
        event.kind = PearlSourceEventKind::TransportConnected;
        event.sessionToken = sessionToken;
        uint64_t unused = 0;
        if (!prepareLocked(TokenDomain::None, &event, &unused)) return false;
        enqueueLocked(event);
        transportConnected_ = true;
        return true;
    }

    uint64_t publishAuthorizationSucceeded(uint64_t sessionToken) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validSessionLocked(sessionToken) || !transportConnected_ ||
            sessionReady_)
            return refuseInvalidLocked();
        PearlSourceEvent event;
        event.kind = PearlSourceEventKind::AuthorizationSucceeded;
        event.sessionToken = sessionToken;
        uint64_t token = 0;
        if (!prepareLocked(TokenDomain::Authorization, &event, &token))
            return 0;
        event.authorizationToken = token;
        enqueueLocked(event);
        activeAuthorizationToken_ = token;
        sessionReady_ = true;
        sessionReadyWithoutAuthorization_ = false;
        return token;
    }

    bool publishSessionReadyNoAuth(uint64_t sessionToken) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validSessionLocked(sessionToken) || !transportConnected_ ||
            sessionReady_)
            return refuseInvalidBooleanLocked();
        PearlSourceEvent event;
        event.kind = PearlSourceEventKind::SessionReadyNoAuth;
        event.sessionToken = sessionToken;
        uint64_t unused = 0;
        if (!prepareLocked(TokenDomain::None, &event, &unused)) return false;
        enqueueLocked(event);
        sessionReady_ = true;
        sessionReadyWithoutAuthorization_ = true;
        return true;
    }

    uint64_t publishJobRetainedPreAuth(
        uint64_t sessionToken, const PearlJobIdentityDigest &identity) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validSessionLocked(sessionToken) || !transportConnected_ ||
            sessionReady_ || retainedPreAuthJobToken_ != 0 ||
            activeJobToken_ != 0)
            return refuseInvalidLocked();
        PearlSourceEvent event;
        event.kind = PearlSourceEventKind::JobRetainedPreAuth;
        event.sessionToken = sessionToken;
        event.jobIdentity = identity;
        uint64_t token = 0;
        if (!prepareLocked(TokenDomain::Job, &event, &token)) return 0;
        event.jobToken = token;
        enqueueLocked(event);
        retainedPreAuthJobToken_ = token;
        retainedPreAuthJobIdentity_ = identity;
        return token;
    }

    uint64_t publishJobAvailable(uint64_t sessionToken,
                                 uint64_t authorizationToken,
                                 const PearlJobIdentityDigest &identity) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validReadyContextLocked(sessionToken, authorizationToken) ||
            retainedPreAuthJobToken_ != 0 || activeJobToken_ != 0)
            return refuseInvalidLocked();
        PearlSourceEvent event;
        event.kind = PearlSourceEventKind::JobAvailable;
        event.sessionToken = sessionToken;
        event.authorizationToken = authorizationToken;
        event.jobIdentity = identity;
        uint64_t token = 0;
        if (!prepareLocked(TokenDomain::Job, &event, &token)) return 0;
        event.jobToken = token;
        enqueueLocked(event);
        activeJobToken_ = token;
        activeJobIdentity_ = identity;
        return token;
    }

    bool publishRetainedJobAvailable(uint64_t sessionToken,
                                     uint64_t authorizationToken,
                                     uint64_t jobToken) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validReadyContextLocked(sessionToken, authorizationToken) ||
            jobToken == 0 || jobToken != retainedPreAuthJobToken_ ||
            activeJobToken_ != 0)
            return refuseInvalidBooleanLocked();
        PearlSourceEvent event;
        event.kind = PearlSourceEventKind::JobAvailable;
        event.sessionToken = sessionToken;
        event.authorizationToken = authorizationToken;
        event.jobToken = jobToken;
        event.jobIdentity = retainedPreAuthJobIdentity_;
        uint64_t unused = 0;
        if (!prepareLocked(TokenDomain::None, &event, &unused)) return false;
        enqueueLocked(event);
        activeJobToken_ = jobToken;
        activeJobIdentity_ = retainedPreAuthJobIdentity_;
        retainedPreAuthJobToken_ = 0;
        retainedPreAuthJobIdentity_ = PearlJobIdentityDigest();
        return true;
    }

    bool publishJobRetired(uint64_t sessionToken, uint64_t jobToken,
                           PearlSourceReason reason) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validSessionLocked(sessionToken) || jobToken == 0 ||
            reason == PearlSourceReason::None)
            return refuseInvalidBooleanLocked();
        const bool retained = jobToken == retainedPreAuthJobToken_;
        const bool active = jobToken == activeJobToken_;
        if (retained == active) return refuseInvalidBooleanLocked();
        PearlSourceEvent event;
        event.kind = PearlSourceEventKind::JobRetired;
        event.sessionToken = sessionToken;
        event.authorizationToken = active ? activeAuthorizationToken_ : 0;
        event.jobToken = jobToken;
        event.jobIdentity =
            active ? activeJobIdentity_ : retainedPreAuthJobIdentity_;
        event.reason = reason;
        uint64_t unused = 0;
        if (!prepareLocked(TokenDomain::None, &event, &unused)) return false;
        enqueueLocked(event);
        if (active) {
            activeJobToken_ = 0;
            activeJobIdentity_ = PearlJobIdentityDigest();
        } else {
            retainedPreAuthJobToken_ = 0;
            retainedPreAuthJobIdentity_ = PearlJobIdentityDigest();
        }
        return true;
    }

#ifdef CANARY_PEARL_ASYNC_SUBMIT_EXPERIMENT
    // Configure before any source facts exist. Submission identity remains in
    // the independent in-flight slot when the search owner releases/rebinds.
    bool enableDeferredIssuedOwnership() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (publishedEvents_ || faultBits_ || deferredIssuedOwnership_) return false;
        deferredIssuedOwnership_ = true;
        return true;
    }
#endif
    // Records the one exact Job object currently owned by the run
    // loop. This is coherent state, not a second JobAvailable source fact.
    bool bindIssuedJob(uint64_t sessionToken, uint64_t jobToken) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (faultBits_ != 0 || !validSessionLocked(sessionToken) ||
            !sessionReady_ ||
            jobToken == 0 || jobToken != activeJobToken_ ||
            issuedJobBound_ || (submitInFlight_ && !deferredIssuedOwnership_))
            return refuseInvalidBooleanLocked();
        issuedJobBound_ = true;
        issuedSessionToken_ = sessionToken;
        issuedAuthorizationToken_ = activeAuthorizationToken_;
        issuedJobToken_ = jobToken;
        issuedJobIdentity_ = activeJobIdentity_;
        return true;
    }

    bool releaseIssuedJob(uint64_t sessionToken, uint64_t jobToken) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validSessionLocked(sessionToken) || !issuedJobBound_ ||
            (submitInFlight_ && !deferredIssuedOwnership_) || sessionToken != issuedSessionToken_ ||
            jobToken == 0 || jobToken != issuedJobToken_)
            return refuseInvalidBooleanLocked();
        clearIssuedJobLocked();
        return true;
    }

    uint64_t publishSubmitDispatched(uint64_t sessionToken,
                                     uint64_t jobToken,
                                     PearlSourceDelivery delivery) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validSessionLocked(sessionToken) || !sessionReady_ ||
            !issuedJobBound_ || sessionToken != issuedSessionToken_ ||
            jobToken == 0 || jobToken != issuedJobToken_ ||
            delivery != PearlSourceDelivery::NotSent || submitInFlight_)
            return refuseInvalidLocked();
        PearlSourceEvent event;
        event.kind = PearlSourceEventKind::SubmitDispatched;
        event.sessionToken = sessionToken;
        event.authorizationToken = issuedAuthorizationToken_;
        event.jobToken = jobToken;
        event.jobIdentity = issuedJobIdentity_;
        event.delivery = delivery;
        uint64_t token = 0;
        if (!prepareLocked(TokenDomain::Operation, &event, &token)) return 0;
        event.operationToken = token;
        enqueueLocked(event);
        submitInFlight_ = true;
        inFlightOperationToken_ = token;
        inFlightSessionToken_ = sessionToken;
        inFlightAuthorizationToken_ = issuedAuthorizationToken_;
        inFlightJobToken_ = jobToken;
        inFlightJobIdentity_ = issuedJobIdentity_;
        return token;
    }

    bool publishSubmitTerminal(
        uint64_t sessionToken, uint64_t operationToken,
        PearlSourceDelivery delivery, PearlSourceTerminal terminal,
        bool acknowledged, bool superseded) {
        return publishSubmitTerminal(
            sessionToken, operationToken, delivery, terminal, acknowledged,
            superseded, pearlSourceCanonicalTerminalReason(
                            delivery, terminal, acknowledged),
            pearlSourceCanonicalTerminalEvidence(delivery, terminal,
                                                 acknowledged));
    }

    bool publishSubmitTerminal(
        uint64_t sessionToken, uint64_t operationToken,
        PearlSourceDelivery delivery, PearlSourceTerminal terminal,
        bool acknowledged, bool superseded,
        PearlSourceReason reason) {
        return publishSubmitTerminal(
            sessionToken, operationToken, delivery, terminal, acknowledged,
            superseded, reason,
            pearlSourceCanonicalTerminalEvidence(delivery, terminal,
                                                 acknowledged));
    }

    bool publishSubmitTerminal(
        uint64_t sessionToken, uint64_t operationToken,
        PearlSourceDelivery delivery, PearlSourceTerminal terminal,
        bool acknowledged, bool superseded, PearlSourceReason reason,
        PearlSourceTerminalEvidence evidence) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validSessionLocked(sessionToken) || !submitInFlight_ ||
            sessionToken != inFlightSessionToken_ || operationToken == 0 ||
            operationToken != inFlightOperationToken_ ||
            !pearlSourceTerminalFactValid(delivery, terminal, acknowledged,
                                          superseded, reason, evidence))
            return refuseInvalidBooleanLocked();
        PearlSourceEvent event;
        event.kind = PearlSourceEventKind::SubmitTerminal;
        event.sessionToken = inFlightSessionToken_;
        event.authorizationToken = inFlightAuthorizationToken_;
        event.jobToken = inFlightJobToken_;
        event.operationToken = inFlightOperationToken_;
        event.jobIdentity = inFlightJobIdentity_;
        event.delivery = delivery;
        event.terminal = terminal;
        event.reason = reason;
        event.terminalEvidence = evidence;
        event.acknowledged = acknowledged ? 1 : 0;
        event.superseded = superseded ? 1 : 0;
        uint64_t unused = 0;
        if (!prepareLocked(TokenDomain::None, &event, &unused)) return false;
        enqueueLocked(event);
        clearInFlightLocked();
        return true;
    }

    bool publishSourceFault(uint64_t sessionToken, PearlSourceReason reason) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validSessionLocked(sessionToken) ||
            reason == PearlSourceReason::None)
            return refuseInvalidBooleanLocked();
        PearlSourceEvent event;
        event.kind = PearlSourceEventKind::SourceFault;
        event.sessionToken = sessionToken;
        event.authorizationToken = activeAuthorizationToken_;
        event.jobToken = activeJobToken_;
        event.jobIdentity = activeJobIdentity_;
        event.reason = reason;
        uint64_t unused = 0;
        if (!prepareLocked(TokenDomain::None, &event, &unused)) return false;
        enqueueLocked(event);
        sourceFaultObserved_ = true;
        return true;
    }

    bool publishDisconnected(uint64_t sessionToken, PearlSourceReason reason) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!validSessionLocked(sessionToken) || submitInFlight_ ||
            retainedPreAuthJobToken_ != 0 || activeJobToken_ != 0 ||
            issuedJobBound_ || reason == PearlSourceReason::None)
            return refuseInvalidBooleanLocked();
        PearlSourceEvent event;
        event.kind = PearlSourceEventKind::Disconnected;
        event.sessionToken = sessionToken;
        event.authorizationToken = activeAuthorizationToken_;
        event.jobToken = activeJobToken_;
        event.jobIdentity = activeJobIdentity_;
        event.reason = reason;
        uint64_t unused = 0;
        if (!prepareLocked(TokenDomain::None, &event, &unused)) return false;
        enqueueLocked(event);
        activeSessionToken_ = 0;
        resetSessionStateLocked();
        return true;
    }

    std::size_t drain(PearlSourceEvent *out,
                      std::size_t maximum) override {
        if (!out || maximum == 0) return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t count = maximum < size_ ? maximum : size_;
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = ring_[head_];
            head_ = (head_ + 1) % kCapacity;
        }
        size_ -= count;
        drainedEvents_ = saturatingAdd(drainedEvents_, count);
        return count;
    }

    PearlSourceObservabilitySnapshot snapshot() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        PearlSourceObservabilitySnapshot out;
        out.failClosed = faultBits_ != 0;
        out.faultBits = faultBits_;
        out.queued = size_;
        out.publishedEvents = publishedEvents_;
        out.drainedEvents = drainedEvents_;
        out.refusedEvents = refusedEvents_;
        out.lastEventToken = lastEventToken_;
        out.lastSessionToken = lastSessionToken_;
        out.lastAuthorizationToken = lastAuthorizationToken_;
        out.lastJobToken = lastJobToken_;
        out.lastOperationToken = lastOperationToken_;
        out.lastTimestampMs = lastTimestampMs_;
        out.activeSessionToken = activeSessionToken_;
        out.transportConnected = transportConnected_;
        out.activeAuthorizationToken = activeAuthorizationToken_;
        out.sessionReady = sessionReady_;
        out.sessionReadyWithoutAuthorization =
            sessionReadyWithoutAuthorization_;
        out.retainedPreAuthJobToken = retainedPreAuthJobToken_;
        out.retainedPreAuthJobIdentity = retainedPreAuthJobIdentity_;
        out.activeJobToken = activeJobToken_;
        out.activeJobIdentity = activeJobIdentity_;
        out.issuedJobBound = issuedJobBound_;
        out.issuedSessionToken = issuedSessionToken_;
        out.issuedAuthorizationToken = issuedAuthorizationToken_;
        out.issuedJobToken = issuedJobToken_;
        out.issuedJobIdentity = issuedJobIdentity_;
        out.submitInFlight = submitInFlight_;
        out.inFlightOperationToken = inFlightOperationToken_;
        out.inFlightAuthorizationToken = inFlightAuthorizationToken_;
        out.inFlightJobToken = inFlightJobToken_;
        out.inFlightJobIdentity = inFlightJobIdentity_;
        out.sourceFaultObserved = sourceFaultObserved_;
        out.hasLastPublishedEvent = hasLastPublishedEvent_;
        out.lastPublishedEvent = lastPublishedEvent_;
        return out;
    }

   private:
    enum class TokenDomain { None, Session, Authorization, Job, Operation };

    static uint64_t saturatingAdd(uint64_t value, std::size_t amount) {
        const uint64_t converted = static_cast<uint64_t>(amount);
        return converted > std::numeric_limits<uint64_t>::max() - value
                   ? std::numeric_limits<uint64_t>::max()
                   : value + converted;
    }

    bool validSessionLocked(uint64_t sessionToken) const {
        return sessionToken != 0 && sessionToken == activeSessionToken_;
    }

    bool validReadyContextLocked(uint64_t sessionToken,
                                 uint64_t authorizationToken) const {
        if (!validSessionLocked(sessionToken) || !sessionReady_) return false;
        if (sessionReadyWithoutAuthorization_)
            return authorizationToken == 0 && activeAuthorizationToken_ == 0;
        return authorizationToken != 0 &&
               authorizationToken == activeAuthorizationToken_;
    }

    uint64_t refuseInvalidLocked() {
        refusedEvents_ = saturatingAdd(refusedEvents_, 1);
        return 0;
    }

    bool refuseInvalidBooleanLocked() {
        refuseInvalidLocked();
        return false;
    }

    uint64_t *domainCounterLocked(TokenDomain domain) {
        switch (domain) {
            case TokenDomain::Session:
                return &lastSessionToken_;
            case TokenDomain::Authorization:
                return &lastAuthorizationToken_;
            case TokenDomain::Job:
                return &lastJobToken_;
            case TokenDomain::Operation:
                return &lastOperationToken_;
            default:
                return nullptr;
        }
    }

    void latchFaultLocked(PearlSourceMailboxFault fault) {
        faultBits_ |= pearlSourceMailboxFaultBit(fault);
        refusedEvents_ = saturatingAdd(refusedEvents_, 1);
    }

    bool prepareLocked(TokenDomain domain, PearlSourceEvent *event,
                       uint64_t *domainToken) {
        if (!event || !domainToken) return false;
        if (faultBits_ != 0) {
            refusedEvents_ = saturatingAdd(refusedEvents_, 1);
            return false;
        }
        if (size_ == kCapacity) {
            latchFaultLocked(PearlSourceMailboxFault::Overflow);
            return false;
        }
        uint64_t *counter = domainCounterLocked(domain);
        if (lastEventToken_ == std::numeric_limits<uint64_t>::max() ||
            (counter && *counter == std::numeric_limits<uint64_t>::max())) {
            latchFaultLocked(PearlSourceMailboxFault::TokenExhaustion);
            return false;
        }
        const uint64_t nowMs = clock_.nowMs();
        if (haveTimestamp_ && nowMs < lastTimestampMs_) {
            latchFaultLocked(PearlSourceMailboxFault::TimestampRollback);
            return false;
        }
        ++lastEventToken_;
        if (counter) {
            ++*counter;
            *domainToken = *counter;
        } else {
            *domainToken = 0;
        }
        event->eventToken = lastEventToken_;
        event->timestampMs = nowMs;
        lastTimestampMs_ = nowMs;
        haveTimestamp_ = true;
        return true;
    }

    void enqueueLocked(const PearlSourceEvent &event) {
        const std::size_t tail = (head_ + size_) % kCapacity;
        ring_[tail] = event;
        ++size_;
        publishedEvents_ = saturatingAdd(publishedEvents_, 1);
        lastPublishedEvent_ = event;
        hasLastPublishedEvent_ = true;
    }

    void clearInFlightLocked() {
        submitInFlight_ = false;
        inFlightOperationToken_ = 0;
        inFlightSessionToken_ = 0;
        inFlightAuthorizationToken_ = 0;
        inFlightJobToken_ = 0;
        inFlightJobIdentity_ = PearlJobIdentityDigest();
    }

    void clearIssuedJobLocked() {
        issuedJobBound_ = false;
        issuedSessionToken_ = 0;
        issuedAuthorizationToken_ = 0;
        issuedJobToken_ = 0;
        issuedJobIdentity_ = PearlJobIdentityDigest();
    }

    void resetSessionStateLocked() {
        transportConnected_ = false;
        activeAuthorizationToken_ = 0;
        sessionReady_ = false;
        sessionReadyWithoutAuthorization_ = false;
        retainedPreAuthJobToken_ = 0;
        retainedPreAuthJobIdentity_ = PearlJobIdentityDigest();
        activeJobToken_ = 0;
        activeJobIdentity_ = PearlJobIdentityDigest();
        clearIssuedJobLocked();
        clearInFlightLocked();
        sourceFaultObserved_ = false;
    }

    PearlSourceMonotonicClock &clock_;
    mutable std::mutex mutex_;
    std::array<PearlSourceEvent, kCapacity> ring_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    uint8_t faultBits_ = 0;
    uint64_t publishedEvents_ = 0;
    uint64_t drainedEvents_ = 0;
    uint64_t refusedEvents_ = 0;
    uint64_t lastEventToken_ = 0;
    uint64_t lastSessionToken_ = 0;
    uint64_t lastAuthorizationToken_ = 0;
    uint64_t lastJobToken_ = 0;
    uint64_t lastOperationToken_ = 0;
    uint64_t lastTimestampMs_ = 0;
    bool haveTimestamp_ = false;
    uint64_t activeSessionToken_ = 0;
    bool transportConnected_ = false;
    uint64_t activeAuthorizationToken_ = 0;
    bool sessionReady_ = false;
    bool sessionReadyWithoutAuthorization_ = false;
    uint64_t retainedPreAuthJobToken_ = 0;
    PearlJobIdentityDigest retainedPreAuthJobIdentity_;
    uint64_t activeJobToken_ = 0;
    PearlJobIdentityDigest activeJobIdentity_;
    bool issuedJobBound_ = false;
    bool deferredIssuedOwnership_ = false;
    uint64_t issuedSessionToken_ = 0;
    uint64_t issuedAuthorizationToken_ = 0;
    uint64_t issuedJobToken_ = 0;
    PearlJobIdentityDigest issuedJobIdentity_;
    bool submitInFlight_ = false;
    uint64_t inFlightOperationToken_ = 0;
    uint64_t inFlightSessionToken_ = 0;
    uint64_t inFlightAuthorizationToken_ = 0;
    uint64_t inFlightJobToken_ = 0;
    PearlJobIdentityDigest inFlightJobIdentity_;
    bool sourceFaultObserved_ = false;
    bool hasLastPublishedEvent_ = false;
    PearlSourceEvent lastPublishedEvent_;
};

}  // namespace om
