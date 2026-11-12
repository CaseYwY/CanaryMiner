// Display-only projection of observations and frozen policy. No authorization decisions.
#pragma once
#include "stats.h"
#include "pool_transport_messages.h"
#include "terminal_style.h"
namespace om {
enum class PoolTransportPolicy { Unknown, Plaintext, StrictTls, PreferTls };
inline std::string poolTransportText(StatsPoolTransport value, uint64_t nowMs = poolTransportNowMs()) {
    using Key=i18n::PoolTransportMessageKey;
    value=currentPoolTransport(value,nowMs);
    if(value.mixed)return i18n::poolTransportEnglish(Key::Mixed);
    switch(value.observation.security){
    case PoolTransportSecurity::Disconnected:return i18n::poolTransportEnglish(Key::Disconnected);
    case PoolTransportSecurity::Connecting:return i18n::poolTransportEnglish(Key::Connecting);
    case PoolTransportSecurity::VerifiedTls:return i18n::poolTransportEnglish(Key::VerifiedTls,{{"port",std::to_string(value.observation.effectivePort)}});
    case PoolTransportSecurity::Plaintext:return i18n::poolTransportEnglish(Key::Plaintext,{{"port",std::to_string(value.observation.effectivePort)}});
    default:return i18n::poolTransportEnglish(Key::Unknown);
    }
}
inline std::string poolTransportErrorText(StatsPoolTransport value, uint64_t nowMs = poolTransportNowMs()) {
    value=currentPoolTransport(value,nowMs);
    return value.observation.error==PoolTransportError::None?std::string{}:
        i18n::poolTransportEnglish(i18n::PoolTransportMessageKey::Error,{{"code",poolTransportErrorName(value.observation.error)}});
}
inline Tone poolTransportTone(StatsPoolTransport value, uint64_t nowMs = poolTransportNowMs()) {
    value=currentPoolTransport(value,nowMs);
    if(value.mixed||value.observation.security==PoolTransportSecurity::Plaintext||value.observation.error!=PoolTransportError::None)return Tone::Amber;
    return value.observation.security==PoolTransportSecurity::VerifiedTls?Tone::Green:Tone::Dark;
}
inline std::string poolTransportPolicyText(PoolTransportPolicy policy) {
    using Key=i18n::PoolTransportMessageKey;
    return i18n::poolTransportEnglish(policy==PoolTransportPolicy::StrictTls?Key::PolicyStrict:
        policy==PoolTransportPolicy::PreferTls?Key::PolicyPrefer:policy==PoolTransportPolicy::Plaintext?Key::PolicyPlaintext:Key::PolicyUnknown);
}
} // namespace om
