// Explicit exact-profile experiment. No CUDA or mining side effects here.
#pragma once
#include <cstring>
#include <cstdint>

namespace om::pearl {
#if defined(CANARY_PEARL_ENABLE_INTEGRATED_PIPELINE)
#if CANARY_PEARL_ENABLE_INTEGRATED_PIPELINE != 1
#error "CANARY_PEARL_ENABLE_INTEGRATED_PIPELINE must be absent or exactly 1"
#endif
constexpr bool kIntegratedPipelineCompiled = true;
#else
constexpr bool kIntegratedPipelineCompiled = false;
#endif

// Host monotonic elapsed intervals, not CUDA kernel stage durations.
struct IntegratedPipelineHostTimings {
    uint64_t preparationEnqueues = 0, preparationEnqueueNs = 0;
    uint64_t computeWaits = 0, computeWaitNs = 0;
    uint64_t compactCopyWaits = 0, compactCopyWaitNs = 0;
    uint64_t cpuFinalizers = 0, cpuFinalizationNs = 0;
};

enum class IntegratedPipelineMode { Disabled, NextAttemptV1, Invalid };
inline IntegratedPipelineMode parseIntegratedPipeline(const char *value) noexcept {
    if (!value) return IntegratedPipelineMode::Disabled;
    return std::strcmp(value, "next-attempt-v1") == 0
        ? IntegratedPipelineMode::NextAttemptV1 : IntegratedPipelineMode::Invalid;
}
inline bool integratedPipelineEnabled(IntegratedPipelineMode mode) noexcept {
    return mode == IntegratedPipelineMode::NextAttemptV1;
}
inline bool integratedPipelineShape(uint32_t sm, uint32_t m, uint32_t n,
        uint32_t k, uint32_t rank, uint64_t plan) noexcept {
    return (sm == 89 || sm == 120) && m == 16384 && n == 131072 &&
        k == 8192 && rank == 128 && plan == UINT64_C(0x435544410001001a);
}
} // namespace om::pearl
