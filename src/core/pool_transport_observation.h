// Read-only observations of a current pool connection, never configuration or authorization.
#pragma once
#include <chrono>
#include <cstdint>

namespace om {
enum class PoolTransportSecurity { Unknown, Disconnected, Connecting, VerifiedTls, Plaintext };
enum class PoolTransportError {
    None, Dns, Socket, Connect, Policy, Unavailable, Verification, Protocol,
    Transport, Timeout, Cancelled, Clock, Capacity, Authentication
};
constexpr uint64_t kPoolTransportObservationMaxAgeMs = 5000;
inline uint64_t poolTransportNowMs() noexcept {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct PoolTransportObservation {
    PoolTransportSecurity security = PoolTransportSecurity::Unknown;
    PoolTransportError error = PoolTransportError::None;
    uint16_t effectivePort = 0; // nonzero only for an observed connected transport
    uint64_t observedMonoMs = 0; // original producer observation; reading a snapshot must not renew it
};
// Borrowed by one run-loop thread. Callbacks are bounded, memory-only, non-reentrant and noexcept.
// VerifiedTls proves the current transport handshake, not mining.authorize or a productive job.
// Observations contain no peer-provided text, credentials, certificate data or endpoint strings.
class PoolTransportObserver {
public:
    virtual ~PoolTransportObserver() = default;
    virtual void onPoolTransportObservation(const PoolTransportObservation &) noexcept {}
};
} // namespace om
