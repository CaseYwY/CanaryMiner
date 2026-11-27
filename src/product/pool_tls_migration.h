// Fixed proposals for migrating an existing first user pool to strict TLS.
#pragma once

#include "config.h"

namespace om {

inline PoolTlsMigrationCandidate poolTlsMigrationCandidate(const ProductConfig &config) {
    if (config.pools.empty()) return PoolTlsMigrationCandidate::None;
    const auto &first = config.pools.front();
    if (first.tls) return PoolTlsMigrationCandidate::None;
    auto dnsHost = first.host;
    if (!normalizePoolTlsDns(&dnsHost)) return PoolTlsMigrationCandidate::None;
    const auto *preset = findLegacyPoolTlsPreset(dnsHost, first.port);
    return preset ? preset->migration : PoolTlsMigrationCandidate::None;
}

inline constexpr const char *poolTlsMigrationSource(PoolTlsMigrationCandidate candidate) {
    for (const auto &preset : kPoolTlsPresets)
        if (candidate == preset.migration) return preset.tcpUrl;
    return nullptr;
}

inline constexpr const char *poolTlsMigrationTarget(PoolTlsMigrationCandidate candidate) {
    for (const auto &preset : kPoolTlsPresets)
        if (candidate == preset.migration) return preset.tlsUrl;
    return nullptr;
}

} // namespace om
