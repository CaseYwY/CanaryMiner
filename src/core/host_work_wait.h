// Host-only wait between completed/reconciled work batches. Milliseconds, never a duty-cycle percentage.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <string>

namespace om {

constexpr unsigned kMaxWorkWaitMs = 1000;
constexpr unsigned kWorkWaitPollMs = 10;
enum class WorkWaitResult { Ready, Stopped, InvalidClockOrDuration };

inline bool parseWorkWaitMs(const std::string &text, unsigned *out) {
    if (!out || text.empty() || text.size() > 4) return false;
    unsigned value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value > kMaxWorkWaitMs) return false;
    *out = value;
    return true;
}

inline bool workWaitEnvironmentCompatible(unsigned duration) {
    if (!duration) return true;
    for (const char *name : {"CANARY_PEARL_LOCAL_RETIREMENTS", "CANARY_PEARL_NATIVE_EVIDENCE", "CANARY_PEARL_INTEGRATED_PIPELINE",
            "CANARY_PEARL_SHAPE_EXPERIMENT", "CANARY_PEARL_ATTEMPT_OVERLAP", "CANARY_PEARL_A_ROOT_FUSION", "CANARY_PEARL_EXPERIMENT",
            "CANARY_PEARL_PROFILE_INTERVAL", "CANARY_PEARL_ASYNC_SUBMIT"})
        if (std::getenv(name)) return false;
    return true;
}

template <typename Stopped, typename NowMs, typename SleepMs>
WorkWaitResult waitAfterWork(unsigned duration, Stopped stopped, NowMs nowMs, SleepMs sleepMs) {
    if (stopped()) return WorkWaitResult::Stopped;
    if (duration > kMaxWorkWaitMs) return WorkWaitResult::InvalidClockOrDuration;
    if (!duration) return WorkWaitResult::Ready;  // zero adds no timing, sleeping or work
    uint64_t previous = nowMs();
    if (previous > (std::numeric_limits<uint64_t>::max)() - duration) return WorkWaitResult::InvalidClockOrDuration;
    const uint64_t deadline = previous + duration;
    for (;;) {
        if (stopped()) return WorkWaitResult::Stopped;
        const uint64_t now = nowMs();
        if (now < previous) return WorkWaitResult::InvalidClockOrDuration;
        if (now >= deadline) return WorkWaitResult::Ready;
        previous = now;
        sleepMs(std::min<uint64_t>(kWorkWaitPollMs, deadline - now));
    }
}

}  // namespace om
