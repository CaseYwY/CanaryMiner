#pragma once
#include <cstring>
#include <cstdint>
namespace om::pearl::tma_fused {
// Production exposes only the qualified combined design and unchanged control.
enum class Mode { Ordinary, EarlyFused, AsymmetricB, Combined, Invalid };
inline Mode parse(const char* value) noexcept {
    if (!value) return Mode::Ordinary;
    return !std::strcmp(value,"early-fused-v1") ? Mode::EarlyFused :
        !std::strcmp(value,"asymmetric-b-v1") ? Mode::AsymmetricB :
        !std::strcmp(value,"combined-v1") ? Mode::Combined : Mode::Invalid;
}
inline bool combined(Mode mode) noexcept { return mode==Mode::Combined; }
inline bool productModeAllowed(Mode mode,bool productFixed,bool compiled,bool exactTma) noexcept {
    if (!productFixed) return false;
    return mode==Mode::Combined && compiled && exactTma;
}
inline bool fused(Mode mode) noexcept { return mode==Mode::EarlyFused || mode==Mode::AsymmetricB || combined(mode); }
inline bool early(Mode mode) noexcept { return fused(mode); }
inline bool asymmetric(Mode mode) noexcept { return mode==Mode::AsymmetricB || combined(mode); }
inline const char* name(Mode mode) noexcept {
    return mode==Mode::Ordinary ? "ordinary-tma-separate-scan-v1" :
        mode==Mode::EarlyFused ? "early-reuse-tma-fused-scan-v1" :
        mode==Mode::AsymmetricB ? "asymmetric-b-tma-fused-scan-v1" :
        combined(mode) ? "combined-tma-fused-scan-v1" : "invalid";
}
inline bool admitted(Mode mode,bool compiled,bool exactTma,bool ownedLoopback,
                     bool bounded,bool overlap,bool production) noexcept {
    if (mode==Mode::Ordinary) return true;
    if (combined(mode)) return compiled && exactTma && ownedLoopback && (bounded || production) && !overlap;
    if (mode==Mode::AsymmetricB) return compiled && exactTma && ownedLoopback && (bounded || production) && !overlap;
    return mode==Mode::EarlyFused && compiled && exactTma && ownedLoopback &&
        (bounded || production) && !overlap;
}
template<class Options> inline bool ownedLoopbackGuard(const Options& options) noexcept {
#ifdef CANARY_PEARL_GUARD_RETIREMENT_EXPERIMENT
    return options.pearlGuardRetirementSession && options.poolHost=="127.0.0.1";
#else
    (void)options;
    return false;
#endif
}
inline bool needsRecovery(Mode mode,uint32_t count,uint32_t capacity) noexcept {
    return fused(mode) && count>capacity;
}
inline bool exactTmaPin(bool profile,uint32_t m,uint32_t n,uint32_t k,uint32_t rank,
                        uint32_t sm,bool tile) noexcept {
    return profile&&m==16384&&n==131072&&k==8192&&rank==128&&sm==120&&tile;
}
} // namespace om::pearl::tma_fused
