// Read-only Pearl selection evidence, not job ownership or public admission.
#pragma once

#include <cstdint>
#include "../algos/pearl-pow/cuda_common_dim.h"
#include <limits>
#include <string_view>

namespace om {

struct PearlNativeSelection {
    bool valid = false;
    uint32_t m = 0, n = 0, k = 0, rank = 0, sm = 0;
    uint64_t preparationPlanId = 0, workUnitsPerAttempt = 0;
    // Backend-owned immutable catalog strings with static lifetime.
    std::string_view kernelId, tileId;
};

// Logical 16x16 transcript tiles, NOT measured GPU operations or pool credits.
// Accept wide inputs so overflow checks remain explicit at caller boundaries.
constexpr bool pearlNativeSelectionWork(uint64_t m, uint64_t n,
                                        uint64_t *out) noexcept {
    if (!out || !m || !n || m % 16 || n % 16) return false;
    const uint64_t rows = m / 16, cols = n / 16;
    if (rows > std::numeric_limits<uint64_t>::max() / cols) return false;
    *out = rows * cols;
    return true;
}

constexpr bool pearlNativeSelectionIdValid(std::string_view id) noexcept {
    if (id.empty() || id.size() > 96) return false;
    if (!((id.front() >= 'a' && id.front() <= 'z') ||
          (id.front() >= '0' && id.front() <= '9'))) return false;
    for (const char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.'))
            return false;
    return true;
}

// Structural validation only. The backend additionally validates the actual
// compiled launch row against its canonical plan, shape and architecture.
constexpr bool pearlNativeSelectionValid(const PearlNativeSelection &s) noexcept {
    uint64_t work = 0;
    return s.valid && s.k == pearl::kCudaCommonDim && s.rank == 128 && s.sm != 0 &&
           s.preparationPlanId != 0 && pearlNativeSelectionIdValid(s.kernelId) &&
           pearlNativeSelectionIdValid(s.tileId) &&
           pearlNativeSelectionWork(s.m, s.n, &work) &&
           work <= std::numeric_limits<uint32_t>::max() - 255u &&
           s.workUnitsPerAttempt == work;
}

constexpr bool pearlNativeSelectionEqual(const PearlNativeSelection &a,
                                         const PearlNativeSelection &b) noexcept {
    return a.valid == b.valid && a.m == b.m && a.n == b.n && a.k == b.k &&
           a.rank == b.rank && a.sm == b.sm &&
           a.preparationPlanId == b.preparationPlanId &&
           a.workUnitsPerAttempt == b.workUnitsPerAttempt &&
           a.kernelId == b.kernelId && a.tileId == b.tileId;
}

// Worker-thread-only lifecycle, shared by the backend and CPU regression.
// A changed/invalid current selection permanently revokes the old snapshot;
// restoring fields cannot revive it without another successful prepare.
class PearlNativeSelectionState {
   public:
    void invalidate() noexcept { selected_ = {}; }
    bool publish(const PearlNativeSelection &selection) noexcept {
        invalidate();
        if (!pearlNativeSelectionValid(selection)) return false;
        selected_ = selection;
        return true;
    }
    PearlNativeSelection read(const PearlNativeSelection &current) noexcept {
        if (!pearlNativeSelectionValid(current) ||
            !pearlNativeSelectionEqual(selected_, current)) invalidate();
        return selected_;
    }
   private:
    PearlNativeSelection selected_;
};

}  // namespace om
