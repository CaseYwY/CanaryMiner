// The destination identity is compiled into the donation lane. User pool settings cannot replace it.
#pragma once
#include <optional>
#include <string_view>

#ifndef CANARY_FEE_POOL_HOST
#define CANARY_FEE_POOL_HOST "pearl.f2pool.com"
#endif

namespace om {

struct DonationTlsCapability {
    std::string_view host;
    unsigned port;
};

// The reviewed identity is independent of the compiled legacy host and cannot move with an override.
// Qualify the endpoint and donation account before supplying a port here.
inline constexpr DonationTlsCapability qualifiedDonationTlsPeer{"pearl.f2pool.com", 0};

class DonationTlsRoute {
   public:
    static std::optional<DonationTlsRoute> forQualifiedPeer(DonationTlsCapability peer) {
        if (peer.host != host() || !peer.port || peer.port > 65535) return std::nullopt;
        return DonationTlsRoute(peer.port);
    }
    static constexpr const char *host() { return CANARY_FEE_POOL_HOST; }
    unsigned port() const { return port_; }
    bool matches(std::string_view hostName, int portNumber) const {
        return hostName == host() && portNumber == static_cast<int>(port_);
    }

   private:
    explicit DonationTlsRoute(unsigned port) : port_(port) {}
    unsigned port_;
};

}  // namespace om
