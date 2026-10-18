#pragma once
#include <cstdint>
#include <string_view>

namespace om { namespace pearl { namespace stage_timing {
inline constexpr const char *kOption = "k8192-v1";
inline constexpr const char *kInterval = "100";
inline constexpr uint64_t kPlan = UINT64_C(0x435544410001001a);
enum class Mode { Disabled, Enabled, Invalid };
inline Mode parse(const char *value) noexcept {
    if (!value) return Mode::Disabled;
    return std::string_view(value) == kOption ? Mode::Enabled : Mode::Invalid;
}
// Capture diagnostic identity for the algorithm lifetime. A later environment
// change cannot relabel an already prepared backend or erase a bad request.
class Binding {
 public:
    Binding(const char *option, const char *legacyInterval) noexcept
        : mode_(parse(option)), legacyConflict_(option && legacyInterval),
          failed_(mode_ == Mode::Invalid || legacyConflict_) {}
    bool enabled() const noexcept { return mode_ == Mode::Enabled && !failed_; }
    const char *interval(const char *legacyInterval) const noexcept {
        return mode_ == Mode::Disabled ? legacyInterval : enabled() ? kInterval : nullptr;
    }
    bool environment(const char *option, const char *legacyInterval,
                     bool incompatibleMode) const noexcept {
        if (failed_ || parse(option) != mode_ ||
            (mode_ == Mode::Enabled && (legacyInterval || incompatibleMode))) failed_ = true;
        return !failed_;
    }
    bool architecture(int sm) const noexcept { return !failed_ && (!enabled() || sm == 120); }
    bool configuration(int sm, uint32_t m, uint32_t n, uint32_t k,
                       uint32_t rank, uint64_t plan) const noexcept {
        return !failed_ && (!enabled() || (sm == 120 && m == 16384 && n == 131072 &&
            k == 8192 && rank == 128 && plan == kPlan));
    }
 private:
    const Mode mode_;
    const bool legacyConflict_;
    mutable bool failed_;
};

struct JobSample {
    uint64_t startNs = 0, endNs = 0, elapsedNs = 0;
    uint32_t m = 0, n = 0, k = 0, rank = 0;
    int sm = 0;
    bool complete = false, valid = true;
};
// Measures the existing host-visible job setup interval, including its existing
// stream synchronization. No CUDA operation or wait is introduced by this type.
// Partial/clock-invalid paths remain visible. Off means no clock or publication.
template<class Adapter> class JobScope {
 public:
    JobScope(bool enabled, uint32_t m, uint32_t n, uint32_t k, uint32_t rank,
             int sm, Adapter adapter) noexcept : enabled_(enabled), adapter_(adapter) {
        sample_.m=m; sample_.n=n; sample_.k=k; sample_.rank=rank; sample_.sm=sm;
        if (enabled_) sample_.startNs=adapter_.now();
    }
    JobScope(const JobScope &) = delete;
    JobScope &operator=(const JobScope &) = delete;
    ~JobScope() noexcept {
        if (!enabled_) return;
        if (!finished_) finish(false);
        adapter_.publish(sample_);
    }
    void complete() noexcept { if (enabled_ && !finished_) finish(true); }
 private:
    void finish(bool complete) noexcept {
        sample_.endNs=adapter_.now();
        sample_.valid=sample_.endNs>=sample_.startNs;
        if (sample_.valid) sample_.elapsedNs=sample_.endNs-sample_.startNs;
        sample_.complete=complete && sample_.valid;
        finished_=true;
    }
    const bool enabled_;
    Adapter adapter_;
    JobSample sample_;
    bool finished_=false;
};
}}} // namespace om::pearl::stage_timing
