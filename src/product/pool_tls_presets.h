// Documented SSL aliases and their exact legacy first-pool migration sources.
#pragma once

#include <string_view>

namespace om {

enum class PoolTlsMigrationCandidate {
    None, KryptexUsTcp7048, KryptexHkTcp7048, HeroMinersUsEastTcp1200,
    KryptexGlobal, KryptexEurope, KryptexBrazil, KryptexSingapore, KryptexRussia, KryptexMiddleEast
};

inline constexpr int kKryptexPoolTcpPort = 7048;
inline constexpr int kHeroMinersPoolPort = 1200;

struct PoolTlsPreset {
    PoolTlsMigrationCandidate migration;
    const char *alias;
    const char *tcpUrl;
    const char *tlsUrl;
    int tcpPort;

    constexpr std::string_view host() const {
        const std::string_view source(tcpUrl);
        constexpr std::string_view scheme = "stratum+tcp://";
        return source.substr(scheme.size(), source.rfind(':') - scheme.size());
    }
};

inline constexpr PoolTlsPreset kPoolTlsPresets[] = {
    {PoolTlsMigrationCandidate::KryptexUsTcp7048, "kryptex-tls",
     "stratum+tcp://prl-us.kryptex.network:7048", "stratum+ssl://prl-us.kryptex.network:8048", kKryptexPoolTcpPort},
    {PoolTlsMigrationCandidate::KryptexHkTcp7048, "kryptex-hk-tls",
     "stratum+tcp://prl-hk.kryptex.network:7048", "stratum+ssl://prl-hk.kryptex.network:8048", kKryptexPoolTcpPort},
    {PoolTlsMigrationCandidate::KryptexGlobal, "kryptex-global-tls",
     "stratum+tcp://prl.kryptex.network:7048", "stratum+ssl://prl.kryptex.network:8048", kKryptexPoolTcpPort},
    {PoolTlsMigrationCandidate::KryptexEurope, "kryptex-eu-tls",
     "stratum+tcp://prl-eu.kryptex.network:7048", "stratum+ssl://prl-eu.kryptex.network:8048", kKryptexPoolTcpPort},
    {PoolTlsMigrationCandidate::KryptexBrazil, "kryptex-br-tls",
     "stratum+tcp://prl-br.kryptex.network:7048", "stratum+ssl://prl-br.kryptex.network:8048", kKryptexPoolTcpPort},
    {PoolTlsMigrationCandidate::KryptexSingapore, "kryptex-sg-tls",
     "stratum+tcp://prl-sg.kryptex.network:7048", "stratum+ssl://prl-sg.kryptex.network:8048", kKryptexPoolTcpPort},
    {PoolTlsMigrationCandidate::KryptexRussia, "kryptex-ru-tls",
     "stratum+tcp://prl-ru.kryptex.network:7048", "stratum+ssl://prl-ru.kryptex.network:8048", kKryptexPoolTcpPort},
    {PoolTlsMigrationCandidate::KryptexMiddleEast, "kryptex-ae-tls",
     "stratum+tcp://prl-ae.kryptex.network:7048", "stratum+ssl://prl-ae.kryptex.network:8048", kKryptexPoolTcpPort},
    {PoolTlsMigrationCandidate::HeroMinersUsEastTcp1200, "herominers-tls",
     "stratum+tcp://us2.pearl.herominers.com:1200", "stratum+ssl://us2.pearl.herominers.com:1200", kHeroMinersPoolPort},
};

inline constexpr const PoolTlsPreset *findPoolTlsPreset(std::string_view alias) {
    for (const auto &preset : kPoolTlsPresets)
        if (alias == preset.alias) return &preset;
    return nullptr;
}

inline constexpr const PoolTlsPreset *findLegacyPoolTlsPreset(std::string_view dnsHost, int port) {
    for (const auto &preset : kPoolTlsPresets)
        if (port == preset.tcpPort && dnsHost == preset.host()) return &preset;
    return nullptr;
}

} // namespace om
