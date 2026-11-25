// Fixed proposals for migrating an existing first user pool to strict TLS.
#pragma once

#include "config.h"

namespace om {

enum class PoolTlsMigrationCandidate { None, KryptexUsTcp7048, KryptexHkTcp7048, HeroMinersUsEastTcp1200 };

inline PoolTlsMigrationCandidate poolTlsMigrationCandidate(const ProductConfig &config) {
    if (config.pools.empty()) return PoolTlsMigrationCandidate::None;
    const auto &first = config.pools.front();
    if (first.tls) return PoolTlsMigrationCandidate::None;
    if (first.port == kKryptexPoolTcpPort) {
        if (first.host == kKryptexUsPoolHost) return PoolTlsMigrationCandidate::KryptexUsTcp7048;
        if (first.host == kKryptexHkPoolHost) return PoolTlsMigrationCandidate::KryptexHkTcp7048;
    }
    if (first.port == kHeroMinersPoolPort && first.host == kHeroMinersUsEastPoolHost)
        return PoolTlsMigrationCandidate::HeroMinersUsEastTcp1200;
    return PoolTlsMigrationCandidate::None;
}

inline constexpr const char *poolTlsMigrationSource(PoolTlsMigrationCandidate candidate) {
    switch (candidate) {
    case PoolTlsMigrationCandidate::KryptexUsTcp7048: return "stratum+tcp://prl-us.kryptex.network:7048";
    case PoolTlsMigrationCandidate::KryptexHkTcp7048: return "stratum+tcp://prl-hk.kryptex.network:7048";
    case PoolTlsMigrationCandidate::HeroMinersUsEastTcp1200: return "stratum+tcp://us2.pearl.herominers.com:1200";
    default: return nullptr;
    }
}

inline constexpr const char *poolTlsMigrationTarget(PoolTlsMigrationCandidate candidate) {
    switch (candidate) {
    case PoolTlsMigrationCandidate::KryptexUsTcp7048: return kKryptexUsPoolTlsUrl;
    case PoolTlsMigrationCandidate::KryptexHkTcp7048: return kKryptexHkPoolTlsUrl;
    case PoolTlsMigrationCandidate::HeroMinersUsEastTcp1200: return kHeroMinersUsEastPoolTlsUrl;
    default: return nullptr;
    }
}

} // namespace om
