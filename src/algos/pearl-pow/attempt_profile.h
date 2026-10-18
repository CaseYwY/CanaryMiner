#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace om { namespace pearl { namespace profile {

// Profiling is deliberately sparse. Missing/zero means OFF; malformed values
// also fail closed to OFF, never to profiling every attempt.
inline bool parseInterval(const char *text, uint32_t *out) noexcept {
    if (!out) return false;
    *out = 0;
    if (!text) return true;
    uint32_t value = 0;
    size_t length = 0;
    for (; text[length]; ++length) {
        if (length >= 10 || text[length] < '0' || text[length] > '9')
            return false;
        const uint32_t digit = static_cast<uint32_t>(text[length] - '0');
        if (value > (1000000000u - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    if (!length || (value != 0 && value < 100)) return false;
    *out = value;
    return true;
}

class Schedule {
public:
    explicit Schedule(uint32_t interval = 0) noexcept : interval_(interval) {}
    bool next() noexcept {
        if (!interval_) return false;
        if (++pending_ != interval_) return false;
        pending_ = 0;
        return true;
    }
private:
    uint32_t interval_ = 0, pending_ = 0;
};

constexpr size_t kStages = 7;
struct Sample {
    std::array<float, kStages> gpuMs{};
    double enqueueMs = 0, readbackWaitMs = 0;
    bool valid() const noexcept {
        for (float time : gpuMs)
            if (!std::isfinite(time) || time <= 0) return false;
        return std::isfinite(enqueueMs) && enqueueMs >= 0 &&
               std::isfinite(readbackWaitMs) && readbackWaitMs >= 0;
    }
    double gpuTotalMs() const noexcept {
        double total = 0;
        for (float time : gpuMs) total += time;
        return total;
    }
};

// Adapter owns no profiler state; it only supplies nonthrowing CUDA/clock/log
// operations. Production and fake tests execute this exact state machine.
template<class Adapter>
class AttemptProfiler {
public:
    explicit AttemptProfiler(const char *intervalText = nullptr,
                             Adapter adapter = Adapter{}) noexcept
        : adapter_(adapter), intervalPresentAtConstruction_(intervalText != nullptr) {
        if (!parseInterval(intervalText, &interval_))
            adapter_.disabled("interval must be 0 or 100..1000000000");
        schedule_ = Schedule(interval_);
    }
    ~AttemptProfiler() noexcept { close(); }
    AttemptProfiler(const AttemptProfiler &) = delete;
    AttemptProfiler &operator=(const AttemptProfiler &) = delete;
    // Formal-v1 has no instrumentation identity. Presence, even zero or an
    // invalid value, is forbidden; unsetting after construction cannot erase it.
    bool formalCompatible(const char *currentIntervalText) const noexcept {
        return !intervalPresentAtConstruction_ && currentIntervalText == nullptr;
    }
    void begin() noexcept {
        active_ = false;
        enqueued_ = false;
        if (!interval_ || disabled_) return;
        if (!initialized_) {
            initialized_ = true;
            for (auto &event : events_) {
                if (!adapter_.create(&event)) {
                    disable("event-create");
                    return;
                }
            }
        }
        active_ = schedule_.next();
        nextBoundary_ = 0;
        if (active_) enqueueStart_ = adapter_.now();
    }
    void boundary(size_t index, typename Adapter::Stream stream) noexcept {
        if (!active_) return;
        if (enqueued_ || index != nextBoundary_ || index >= events_.size() ||
            !adapter_.record(events_[index], stream)) {
            disable("event-record");
            return;
        }
        ++nextBoundary_;
    }
    void enqueued() noexcept {
        if (!active_) return;
        if (enqueued_ || nextBoundary_ != events_.size()) {
            disable("incomplete-enqueue");
            return;
        }
        sample_.enqueueMs = adapter_.milliseconds(enqueueStart_, adapter_.now());
        readbackStart_ = adapter_.now();
        enqueued_ = true;
    }
    // Caller must have completed its existing successful stream synchronization.
    void complete(uint64_t nonce, uint32_t m, uint32_t n, size_t tile,
                  int arch, std::string_view device) noexcept {
        if (!active_) return;
        if (!enqueued_ || nextBoundary_ != events_.size()) {
            disable("incomplete-boundaries");
            return;
        }
        sample_.readbackWaitMs = adapter_.milliseconds(readbackStart_, adapter_.now());
        for (size_t i = 0; i < kStages; ++i) {
            if (!adapter_.elapsed(&sample_.gpuMs[i], events_[i], events_[i + 1])) {
                disable("event-elapsed");
                return;
            }
        }
        active_ = false;
        enqueued_ = false;
        if (!sample_.valid()) {
            disable("invalid-duration");
            return;
        }
        adapter_.publish(sample_, interval_, nonce, m, n, tile, arch, device);
    }
    void cancel() noexcept { active_ = false; enqueued_ = false; }
    void close() noexcept {
        cancel();
        disabled_ = true;
        for (auto &event : events_) {
            if (event != typename Adapter::Event{}) adapter_.destroy(event);
            event = typename Adapter::Event{};
        }
    }
private:
    void disable(const char *reason) noexcept {
        cancel();
        disabled_ = true;
        adapter_.disabled(reason);
        // Retain handles until synchronized backend teardown. No barrier,
        // retry, or clearing of CUDA's last-error state on diagnostic failure.
    }
    Adapter adapter_;
    const bool intervalPresentAtConstruction_;
    uint32_t interval_ = 0;
    Schedule schedule_;
    std::array<typename Adapter::Event, kStages + 1> events_{};
    bool initialized_ = false, disabled_ = false, active_ = false, enqueued_ = false;
    size_t nextBoundary_ = 0;
    typename Adapter::TimePoint enqueueStart_{}, readbackStart_{};
    Sample sample_;
};

}}} // namespace om::pearl::profile
