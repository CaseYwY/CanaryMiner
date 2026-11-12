// Fixed proposals for migrating an existing first user pool to strict TLS.
#pragma once

#include "config.h"

namespace om {

enum class PoolTlsMigrationCandidate { None, KryptexUsTcp7048 };

inline PoolTlsMigrationCandidate poolTlsMigrationCandidate(const ProductConfig &config) {
    if (config.pools.empty()) return PoolTlsMigrationCandidate::None;
    const auto &first = config.pools.front();
    if (first.tls || first.host != kKryptexUsPoolHost || first.port != kKryptexUsPoolTcpPort)
        return PoolTlsMigrationCandidate::None;
    return PoolTlsMigrationCandidate::KryptexUsTcp7048;
}

inline constexpr const char *poolTlsMigrationSource(PoolTlsMigrationCandidate candidate) {
    return candidate == PoolTlsMigrationCandidate::KryptexUsTcp7048
        ? "stratum+tcp://prl-us.kryptex.network:7048" : nullptr;
}

inline constexpr const char *poolTlsMigrationTarget(PoolTlsMigrationCandidate candidate) {
    return candidate == PoolTlsMigrationCandidate::KryptexUsTcp7048 ? kKryptexUsPoolTlsUrl : nullptr;
}

} // namespace om
