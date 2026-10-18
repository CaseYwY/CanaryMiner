#pragma once
#include <cstdint>
#include <cstring>

namespace om {
// This mode grants no public qualification. Both compile and runtime opt-ins
// are required; absence preserves the historical native evidence contract.
constexpr std::uint64_t kPearlSm120SingleAttemptBudget = 256;
inline bool pearlSm120NativeEvidenceMode(const char *mode) noexcept {
#if defined(CANARY_PEARL_ENABLE_SM120_NATIVE_EVIDENCE) && CANARY_PEARL_ENABLE_SM120_NATIVE_EVIDENCE == 1 && \
    defined(CANARY_PEARL_ONLY_BUILD) && CANARY_PEARL_ONLY_BUILD == 1
    return mode && !std::strcmp(mode,"sm120-generic-v1");
#else
    (void)mode;
    return false;
#endif
}
inline bool pearlSm120NativeEvidenceRequest(const char *mode, const char *selector,
                                           bool boundedStop) noexcept {
    return !mode || (pearlSm120NativeEvidenceMode(mode) && !selector && boundedStop);
}
inline bool pearlSm120AttemptBatch(std::uint64_t boundUnits, std::uint64_t units,
                                  std::uint64_t work, std::uint64_t attempts) noexcept {
    return boundUnits >= kPearlSm120SingleAttemptBudget && units == boundUnits &&
           work == boundUnits && attempts == 1;
}
inline bool pearlSm120WorkConserved(std::uint64_t boundUnits, std::uint64_t work,
                                   std::uint64_t attempts) noexcept {
    return boundUnits >= kPearlSm120SingleAttemptBudget && boundUnits != UINT64_MAX &&
           attempts != UINT64_MAX && attempts <= UINT64_MAX / boundUnits &&
           work != UINT64_MAX && work == attempts * boundUnits;
}
} // namespace om
