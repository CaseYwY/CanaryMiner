// Host-only admission vocabulary for the isolated fused-scan experiment.
#pragma once
#include <cstring>
#include <cstdint>
namespace om::pearl {
constexpr bool kFusedScanCompiled=false;
enum class FusedScanMode { Disabled, StoreV1, StorePairedV1, Invalid };
inline FusedScanMode parseFusedScan(const char *value) noexcept {
    if (!value) return FusedScanMode::Disabled;
    if (std::strcmp(value,"store-v1")==0) return FusedScanMode::StoreV1;
    // Retain the isolated enum/admission ABI; this serial derivative has its
    // own closed selector and explicit physical dispatch, not a paired path.
    return std::strcmp(value,"store-packed-v1")==0 ? FusedScanMode::StorePairedV1 : FusedScanMode::Invalid;
}
inline bool pairedScanAllowed(uint32_t sm,uint32_t m,uint32_t n,uint32_t k,
        uint32_t rank,uint64_t plan) noexcept {
    return kFusedScanCompiled && sm==89 && m==16384 && n==131072 && k==8192 &&
        rank==128 && plan==UINT64_C(0x435544410001001a);
}
inline bool pairedScanBounded(bool ownedLoopback,bool bounded,bool production) noexcept {
    // Packed fused-scan admission for bounded accepted-quota/sustained runs. The caller still requires the owned
    // loopback guard session, exact K8192/rank128 shape and plan, compact readback and the generic native evidence
    // profile. Inactive in the SM120 build (kFusedScanCompiled is false).
    (void)production;
    return kFusedScanCompiled && ownedLoopback && bounded;
}
inline bool fusedScanExactShape(uint32_t sm,uint32_t m,uint32_t n,
        uint32_t k,uint32_t rank,uint64_t plan) noexcept {
    return (sm==89 || sm==120) && m==16384 && n==131072 && k==8192 && rank==128 &&
        plan==UINT64_C(0x435544410001001a);
}
} // namespace om::pearl
