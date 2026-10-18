#pragma once
#include <cstdint>
#include <string_view>

namespace om::pearl {
enum class ARootFusionMode { Disabled, Enabled, Invalid };
inline constexpr const char *kARootFusionOptIn = "a-root-commit-v1";
inline ARootFusionMode parseARootFusion(const char *text) noexcept {
    if (!text) return ARootFusionMode::Disabled;
    return std::string_view(text) == kARootFusionOptIn ? ARootFusionMode::Enabled : ARootFusionMode::Invalid;
}
inline bool aRootFusionAdmission(ARootFusionMode mode, bool compiled, bool formal,
    bool profiling, bool synthetic, std::string_view host, int port, bool originalWm8) noexcept {
    if (mode == ARootFusionMode::Disabled) return true;
    return mode == ARootFusionMode::Enabled && compiled && !formal && !profiling &&
        !synthetic && originalWm8 && host == "127.0.0.1" && port > 0 && port <= 65535;
}
constexpr bool aRootFusionPreparationValid(int architecture, uint32_t m, uint32_t n,
    uint32_t chunks, unsigned threads, unsigned certificate) noexcept {
    return architecture == 120 && m == 4096 && n == 65536 && chunks == 8192 &&
        (threads == 64 || threads == 256) && (certificate == 2 || certificate == 3);
}

// Test-only consumer instantiates this in the CUDA backend. No device reads or
// inferred configuration: prepare records the branch actually enqueued, and
// retirement consumes precisely that slot/nonce once. Search/rewrite/failure
// invalidates all records, so a prior job or calibration cannot supply evidence.
class ARootPreparationEvidence {
    struct Slot { bool valid = false, fused = false; uint64_t nonce = 0; } slots_[2];
 public:
    void invalidate() noexcept { slots_[0] = {}; slots_[1] = {}; }
    bool invalidateSlot(unsigned slot) noexcept {
        if (slot >= 2) return false;
        slots_[slot] = {};
        return true;
    }
    bool prepared(unsigned slot, uint64_t nonce, bool fused) noexcept {
        if (slot >= 2 || slots_[slot].valid) return false;
        slots_[slot] = {true, fused, nonce};
        return true;
    }
    bool retire(unsigned slot, uint64_t nonce, bool *fused) noexcept {
        if (!fused || slot >= 2 || !slots_[slot].valid || slots_[slot].nonce != nonce) return false;
        *fused = slots_[slot].fused;
        slots_[slot] = {};
        return true;
    }
};
} // namespace om::pearl
