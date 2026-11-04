// Explicit single-owner native TLS steps over an already connected nonblocking byte channel.
// No resolver, socket creation, settings activation, fallback, pool selection or donation authority.
#pragma once
#include "pearl_pool_transport.h"
#include <atomic>
#include <cstdint>
#include <memory>
namespace om {
enum class PearlTlsState { Dormant, Handshaking, VerifiedOpen, Closed, Failed };
// VerifiedOpen asserts this backend's chain/time/server-SAN checks, not qualified revocation, process isolation or endpoint support.
enum class PearlTlsError { None, Disabled, Unavailable, InvalidPolicy, Verification, Protocol, Transport, Clock, Cancelled, Timeout, Capacity };
struct PearlTlsPolicy {
    bool enabled=false;
    std::string serverName; // reference identity, not a URL or a value learned from the peer
    std::string caPem; // empty: system trust; otherwise exclusive explicit in-memory anchors, never an OS-store install
    uint64_t handshakeDeadlineMonoMs=0;
};
class PearlTlsStream {
   public:
    // The raw channel must make bounded nonblocking receive/send calls after setNonblocking(true).
    // An enabled well-shaped begin adopts close authority. A dormant/disabled object never closes the borrowed channel.
    // All calls/context destruction belong to one thread; cancel is the only shared input.
    PearlTlsStream(PearlPoolTransport &,const std::atomic<bool> &cancel);
    ~PearlTlsStream();
    PearlTlsStream(const PearlTlsStream &)=delete;
    PearlTlsStream &operator=(const PearlTlsStream &)=delete;
    bool begin(const PearlTlsPolicy &,uint64_t nowMonoMs)noexcept;
    PearlPoolTransfer handshake(uint64_t nowMonoMs)noexcept; // Progress when verified; inspect state(), never application bytes
    PearlPoolTransfer receive(char *,std::size_t,uint64_t nowMonoMs)noexcept;
    PearlPoolTransfer send(const char *,std::size_t,uint64_t nowMonoMs)noexcept;
    bool bufferedRead()const noexcept;
    PearlTlsState state()const noexcept;
    PearlTlsError error()const noexcept;
    void abort()noexcept; // terminal local close, NOT authenticated peer EOF or graceful TLS shutdown
   private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace om
