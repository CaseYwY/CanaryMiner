// Fixed proposals for migrating an existing first user pool to strict TLS.
#pragma once

#include "config.h"

namespace om {

enum class PoolTlsMigrationCandidate { None, KryptexUsTcp7048, KryptexHkTcp7048 };

inline PoolTlsMigrationCandidate poolTlsMigrationCandidate(const ProductConfig &config) {
    if (config.pools.empty()) return PoolTlsMigrationCandidate::None;
    const auto &first = config.pools.front();
    if (first.tls || first.port != kKryptexPoolTcpPort)
        return PoolTlsMigrationCandidate::None;
    if (first.host == kKryptexUsPoolHost) return PoolTlsMigrationCandidate::KryptexUsTcp7048;
    if (first.host == kKryptexHkPoolHost) return PoolTlsMigrationCandidate::KryptexHkTcp7048;
    return PoolTlsMigrationCandidate::None;
}

inline constexpr const char *poolTlsMigrationSource(PoolTlsMigrationCandidate candidate) {
    switch (candidate) {
    case PoolTlsMigrationCandidate::KryptexUsTcp7048: return "stratum+tcp://prl-us.kryptex.network:7048";
    case PoolTlsMigrationCandidate::KryptexHkTcp7048: return "stratum+tcp://prl-hk.kryptex.network:7048";
    default: return nullptr;
    }
}

inline constexpr const char *poolTlsMigrationTarget(PoolTlsMigrationCandidate candidate) {
    switch (candidate) {
    case PoolTlsMigrationCandidate::KryptexUsTcp7048: return kKryptexUsPoolTlsUrl;
    case PoolTlsMigrationCandidate::KryptexHkTcp7048: return kKryptexHkPoolTlsUrl;
    default: return nullptr;
    }
}

} // namespace om
