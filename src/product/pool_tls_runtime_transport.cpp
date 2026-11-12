#include "pool_tls_runtime_transport.h"

namespace om {
PoolTlsRuntimeTransport::PoolTlsRuntimeTransport(std::filesystem::path helper, PoolTlsCheckRequest request,
                                               volatile sig_atomic_t *stop)
    : helper_(std::move(helper)), request_(std::move(request)), stop_(stop) {
    if (!life_.begin()) { cancelled_ = true; failure_ = PearlTlsError::Unavailable; }
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
    if (!request_.enabled || host != request_.host || port < 1 || static_cast<unsigned>(port) != request_.port ||
        !validPoolTlsCheckRequest(request_) || !request_.caPem.empty() || request_.windowsPrivateTrust) {
        failure_ = PearlTlsError::InvalidPolicy;
        return PearlPoolConnectStatus::ConnectFailure;
    }
    failure_ = PearlTlsError::None;
    connection_ = std::make_unique<PoolTlsProcessTransport>(helper_, request_, cancelled_, stop_);
    const auto result = connection_->open(host, port);
    if (!current() || result != PearlPoolConnectStatus::Connected) {
        if (failure_ == PearlTlsError::None) failure_ = connection_->error();
        close();
        return PearlPoolConnectStatus::ConnectFailure;
    }
    return result;
}
bool PoolTlsRuntimeTransport::isOpen() const { return readable() && connection_ && connection_->isOpen(); }
void PoolTlsRuntimeTransport::close() {
    if (foreign()) return;
    retire();
}
void PoolTlsRuntimeTransport::retire() {
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
