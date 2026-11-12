// User-pool TLS with a new owned helper for each connection and an optional frozen fallback authorization.
#pragma once
#include "pool_tls_process_transport.h"
#include "native_process_admission.h"
#include <csignal>

namespace om {
class PoolTlsRuntimeTransport final : public PearlPoolTransport {
public:
    PoolTlsRuntimeTransport(std::filesystem::path helper, PoolTlsCheckRequest request,
                            volatile sig_atomic_t *stop, std::string authorizedPlaintextUrl = {});
    ~PoolTlsRuntimeTransport() override;
    PearlPoolConnectStatus open(const std::string &, int) override;
    bool isOpen() const override;
    PoolTransportObservation observation() const override;
    void close() override;
    bool progressIO() const override { return true; }
    bool bufferedRead() const override;
    bool localReadPending() const override;
    PearlPoolWaitStatus waitLocalRead(int) override;
    bool setNonblocking(bool) override;
    PearlPoolWaitStatus waitReadable(int ms) override { return waitFor(PearlPoolInterest::Read, ms); }
    PearlPoolWaitStatus waitWritable(int ms) override { return waitFor(PearlPoolInterest::Write, ms); }
    PearlPoolWaitStatus waitFor(PearlPoolInterest, int) override;
    PearlPoolTransfer send(const char *, std::size_t) override;
    PearlPoolTransfer receive(char *, std::size_t) override;
    PearlTlsError error() const;
    bool plaintextActive() const;
    uint64_t plaintextConnections() const;
private:
    bool current();
    bool readable() const;
    bool foreign() const;
    void retire();
    std::filesystem::path helper_;
    PoolTlsCheckRequest request_;
    std::string fallbackUrl_;
    volatile sig_atomic_t *stop_;
    NativeProcessLifetime life_;
    mutable std::atomic<bool> cancelled_{false};
    std::unique_ptr<PoolTlsProcessTransport> connection_;
    mutable PearlTlsError failure_ = PearlTlsError::None;
    PoolTlsTransportMode mode_ = PoolTlsTransportMode::Tls;
    uint16_t connectedPort_ = 0;
    PoolTransportError observedOpenError_ = PoolTransportError::None;
    uint64_t attempts_ = 0, plaintextConnections_ = 0;
};
} // namespace om
