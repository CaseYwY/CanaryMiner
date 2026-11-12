#include "pool_tls_runtime_transport.h"
#include "pool_security_preferences.h"
#include <chrono>

namespace om {
namespace {
uint64_t runtimeNowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
PoolTransportError observedError(PearlTlsError error) {
    switch (error) {
    case PearlTlsError::None: return PoolTransportError::None;
    case PearlTlsError::Disabled: case PearlTlsError::InvalidPolicy: return PoolTransportError::Policy;
    case PearlTlsError::Unavailable: return PoolTransportError::Unavailable;
    case PearlTlsError::Verification: return PoolTransportError::Verification;
    case PearlTlsError::Protocol: return PoolTransportError::Protocol;
    case PearlTlsError::Transport: return PoolTransportError::Transport;
    case PearlTlsError::Clock: return PoolTransportError::Clock;
    case PearlTlsError::Cancelled: return PoolTransportError::Cancelled;
    case PearlTlsError::Timeout: return PoolTransportError::Timeout;
    case PearlTlsError::Capacity: return PoolTransportError::Capacity;
    }
    return PoolTransportError::Unavailable;
}
PoolTransportError observedOpenError(const std::optional<PoolTlsCheckReply> &reply) {
    if (!reply || reply->tlsError != PearlTlsError::None) return PoolTransportError::None;
    if (reply->stage == PoolTlsCheckStage::Resolve && reply->status == PoolTlsCheckStatus::Dns)
        return PoolTransportError::Dns;
    if (reply->stage == PoolTlsCheckStage::Connect && reply->status == PoolTlsCheckStatus::Connect)
        return PoolTransportError::Connect;
    return PoolTransportError::None;
}
PoolFallbackFailure fallbackEvidence(const PoolTlsCheckReply &reply, uint64_t attempt, const std::string &tlsUrl) {
    PoolFallbackFailure evidence;
    evidence.attempt = attempt;
    evidence.tlsUrl = tlsUrl;
    if (reply.stage == PoolTlsCheckStage::Resolve) evidence.phase = PoolFallbackPhase::Resolve;
    else if (reply.stage == PoolTlsCheckStage::Connect) evidence.phase = PoolFallbackPhase::Connect;
    else if (reply.stage == PoolTlsCheckStage::Handshake) evidence.phase = PoolFallbackPhase::Handshake;
    // Do not infer a phase from a generic parent-side error, or classify a TLS failure as a socket failure.
    if (reply.tlsError != PearlTlsError::None) return evidence;
    if (reply.status == PoolTlsCheckStatus::Dns) evidence.failure = PoolTlsFailure::Dns;
    else if (reply.status == PoolTlsCheckStatus::Connect) evidence.failure = PoolTlsFailure::Connect;
    else if (reply.status == PoolTlsCheckStatus::Timeout) evidence.failure = PoolTlsFailure::NetworkTimeout;
    return evidence;
}
}
PoolTlsRuntimeTransport::PoolTlsRuntimeTransport(std::filesystem::path helper, PoolTlsCheckRequest request,
                                               volatile sig_atomic_t *stop, std::string authorizedPlaintextUrl)
    : helper_(std::move(helper)), request_(std::move(request)), fallbackUrl_(std::move(authorizedPlaintextUrl)), stop_(stop) {
    if (!life_.begin()) { cancelled_ = true; failure_ = PearlTlsError::Unavailable; }
    if (!fallbackUrl_.empty()) {
        PoolSecurityEndpoint endpoint;
        if (!parsePoolSecurityUrl(fallbackUrl_, &endpoint) || endpoint.tls ||
            poolSecurityUrl(endpoint) != fallbackUrl_ || endpoint.host != request_.host) {
            cancelled_ = true;
            failure_ = PearlTlsError::InvalidPolicy;
        }
    }
}
PoolTlsRuntimeTransport::~PoolTlsRuntimeTransport() { retire(); }

bool PoolTlsRuntimeTransport::foreign() const { return life_.current() && !life_.permitted(); }

bool PoolTlsRuntimeTransport::readable() const {
    if (foreign()) return false;
    if (!cancelled_.load()) {
        if (stop_ && *stop_) { cancelled_ = true; failure_ = PearlTlsError::Cancelled; }
        else if (!life_.current()) { cancelled_ = true; failure_ = PearlTlsError::Unavailable; }
    }
    return !cancelled_.load();
}
bool PoolTlsRuntimeTransport::current() {
    return readable(); // observed stop/epoch loss stays latched; foreign callers cannot revoke the owner
}
PearlPoolConnectStatus PoolTlsRuntimeTransport::open(const std::string &host, int port) {
    if (foreign()) return PearlPoolConnectStatus::ConnectFailure;
    // Destroy and settle the exact previous owner before acquiring another process reservation.
    // A failed/ambiguous write is never retained here for retransmission on the new connection.
    close();
    if (!current()) return PearlPoolConnectStatus::ConnectFailure;
    observedOpenError_ = PoolTransportError::None;
    if (!request_.enabled || host != request_.host || port < 1 || static_cast<unsigned>(port) != request_.port ||
        !validPoolTlsCheckRequest(request_) || !request_.caPem.empty() || request_.windowsPrivateTrust) {
        failure_ = PearlTlsError::InvalidPolicy;
        return PearlPoolConnectStatus::ConnectFailure;
    }
    const auto begin = runtimeNowMs();
    if (!begin || begin > UINT64_MAX - request_.totalMs || attempts_ == UINT64_MAX) {
        failure_ = PearlTlsError::Clock;
        return PearlPoolConnectStatus::ConnectFailure;
    }
    const auto deadline = begin + request_.totalMs;
    ++attempts_;
    failure_ = PearlTlsError::None;
    mode_ = PoolTlsTransportMode::Tls;
    connection_ = std::make_unique<PoolTlsProcessTransport>(helper_, request_, cancelled_, stop_);
    const auto result = connection_->openUntil(host, port, deadline);
    if (current() && result == PearlPoolConnectStatus::Connected) {
        const auto completed = runtimeNowMs();
        if (completed >= begin && completed < deadline) {
            connectedPort_ = static_cast<uint16_t>(request_.port);
            return result;
        }
        failure_ = completed < begin ? PearlTlsError::Clock : PearlTlsError::Timeout;
    }
    const auto evidence = connection_->openFailure();
    observedOpenError_ = observedOpenError(evidence);
    if (failure_ == PearlTlsError::None) failure_ = connection_->error();
    close();
    // The failed TLS helper must be settled before evaluating permission or opening a new peer.
    // No helper reply (including a parent timeout) means no downgrade evidence.
    if (!current() || fallbackUrl_.empty() || !evidence) return PearlPoolConnectStatus::ConnectFailure;
    PoolSecurityPreferences preferences;
    const auto tlsUrl = std::string("stratum+ssl://") + request_.host + ":" + std::to_string(request_.port);
    preferences.users[0] = {PoolSecurityMode::PreferTls, tlsUrl, fallbackUrl_, true};
    PoolFallbackGate gate(std::move(preferences), PoolSecurityScope::User, 0, attempts_);
    const auto endpoint = gate.select(fallbackEvidence(*evidence, attempts_, tlsUrl));
    if (!endpoint) return PearlPoolConnectStatus::ConnectFailure;
    const auto now = runtimeNowMs();
    if (now < begin || now >= deadline || deadline - now < 50) {
        failure_ = now < begin ? PearlTlsError::Clock : PearlTlsError::Timeout;
        return PearlPoolConnectStatus::ConnectFailure;
    }
    auto plaintext = request_;
    plaintext.port = endpoint->port;
    plaintext.totalMs = static_cast<unsigned>(deadline - now); // one original budget, never another full TLS budget
    if (!current()) return PearlPoolConnectStatus::ConnectFailure;
    connection_ = std::make_unique<PoolTlsProcessTransport>(helper_, std::move(plaintext), cancelled_, stop_, PoolTlsTransportMode::Plaintext);
    failure_ = PearlTlsError::None;
    observedOpenError_ = PoolTransportError::None;
    const auto fallback = connection_->openUntil(endpoint->host, static_cast<int>(endpoint->port), deadline);
    if (fallback == PearlPoolConnectStatus::Connected) {
        const auto completed = runtimeNowMs();
        if (completed < begin || completed >= deadline) {
            failure_ = completed < begin ? PearlTlsError::Clock : PearlTlsError::Timeout;
            close();
            return PearlPoolConnectStatus::ConnectFailure;
        }
    }
    if (!current() || fallback != PearlPoolConnectStatus::Connected) {
        if (failure_ == PearlTlsError::None) failure_ = connection_->error();
        observedOpenError_ = observedOpenError(connection_->openFailure());
        close();
        return PearlPoolConnectStatus::ConnectFailure;
    }
    mode_ = PoolTlsTransportMode::Plaintext;
    connectedPort_ = static_cast<uint16_t>(endpoint->port);
    ++plaintextConnections_; // at most one per attempt; attempts_ already guards overflow
    return fallback;
}
bool PoolTlsRuntimeTransport::isOpen() const { return readable() && connection_ && connection_->isOpen(); }
PoolTransportObservation PoolTlsRuntimeTransport::observation() const {
    if (foreign()) return {PoolTransportSecurity::Unknown, PoolTransportError::Unavailable, 0, poolTransportNowMs()};
    const bool connected = isOpen(); // observes and permanently latches stop/epoch loss
    auto failure = observedError(error());
    if (failure == PoolTransportError::Transport && observedOpenError_ != PoolTransportError::None)
        failure = observedOpenError_;
    return {connected ? (mode_ == PoolTlsTransportMode::Plaintext ? PoolTransportSecurity::Plaintext : PoolTransportSecurity::VerifiedTls)
                      : PoolTransportSecurity::Disconnected,
            failure, connected ? connectedPort_ : uint16_t{0}, poolTransportNowMs()};
}
bool PoolTlsRuntimeTransport::plaintextActive() const { return isOpen() && mode_ == PoolTlsTransportMode::Plaintext; }
uint64_t PoolTlsRuntimeTransport::plaintextConnections() const { return readable() ? plaintextConnections_ : 0; }
void PoolTlsRuntimeTransport::close() {
    if (foreign()) return;
    retire();
}
void PoolTlsRuntimeTransport::retire() {
    connectedPort_ = 0;
    if (connection_) {
        if (failure_ == PearlTlsError::None) failure_ = connection_->error();
        connection_->close();
        connection_.reset();
    }
    // An unresolved child changes the shared native epoch. It cannot be forgotten by reconnecting.
    (void)current();
}
bool PoolTlsRuntimeTransport::bufferedRead() const { return isOpen() && connection_->bufferedRead(); }
bool PoolTlsRuntimeTransport::localReadPending() const { return isOpen() && connection_->localReadPending(); }
PearlPoolWaitStatus PoolTlsRuntimeTransport::waitLocalRead(int ms) {
    return current() && connection_ ? connection_->waitLocalRead(ms) : PearlPoolWaitStatus::Error;
}
bool PoolTlsRuntimeTransport::setNonblocking(bool value) {
    return current() && connection_ && connection_->setNonblocking(value);
}
PearlPoolWaitStatus PoolTlsRuntimeTransport::waitFor(PearlPoolInterest interest, int ms) {
    return current() && connection_ ? connection_->waitFor(interest, ms) : PearlPoolWaitStatus::Error;
}
PearlPoolTransfer PoolTlsRuntimeTransport::send(const char *data, std::size_t length) {
    return current() && connection_ ? connection_->send(data, length) : PearlPoolTransfer{};
}
PearlPoolTransfer PoolTlsRuntimeTransport::receive(char *data, std::size_t length) {
    return current() && connection_ ? connection_->receive(data, length) : PearlPoolTransfer{};
}
PearlTlsError PoolTlsRuntimeTransport::error() const {
    if (foreign()) return PearlTlsError::Unavailable;
    (void)readable();
    if (failure_ != PearlTlsError::None) return failure_;
    return connection_ ? connection_->error() : PearlTlsError::None;
}
} // namespace om
