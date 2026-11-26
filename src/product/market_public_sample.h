#pragma once

#include "market_native_arguments.h"
#include "market_owner.h"
#include "native_process_admission.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <future>
#include <string_view>

namespace om::market_detail {

using MarketSampleClock = std::pair<uint64_t, uint64_t>;

inline bool marketSampleClockContinuous(const MarketSampleClock &initial, const MarketSampleClock &previous,
                                        const MarketSampleClock &current) {
    if (current.first < previous.first || current.second < previous.second ||
        current.first < initial.first || current.second < initial.second || current.second > 253402300799ULL)
        return false;
    const auto steadySeconds = (current.first - initial.first) / 1000;
    const auto utcSeconds = current.second - initial.second;
    return (std::max)(steadySeconds, utcSeconds) - (std::min)(steadySeconds, utcSeconds) <= 5;
}

inline constexpr uint64_t maxMarketUtcMs=253402300799999ULL;
inline uint64_t marketUtcClockMs() {
    const auto value=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return value>0 ? static_cast<uint64_t>(value) : 0;
}
inline MarketSampleClock marketTradeClock() {
    const auto mono=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return {static_cast<uint64_t>(mono),marketUtcClockMs()};
}
inline bool marketTradeClockContinuous(const MarketSampleClock &initial,const MarketSampleClock &previous,
                                       const MarketSampleClock &current) {
    if (!initial.second || current.first<previous.first || current.second<previous.second ||
        current.first<initial.first || current.second<initial.second || current.second>maxMarketUtcMs)
        return false;
    const auto steady=current.first-initial.first, utc=current.second-initial.second;
    return (std::max)(steady,utc)-(std::min)(steady,utc)<=5000;
}

inline bool marketSampleStopped(const volatile sig_atomic_t *stop) { return stop && *stop; }

inline PublicJsonResponse marketSampleFailure(PublicHttpsStatus status) {
    PublicJsonResponse response;
    response.status = status;
    return response;
}

inline PublicJsonResponse awaitPublicMarketReply(const MarketFetch &fetch, const PublicJsonRequest &request,
                                                 PublicHttpsLimits limits, const NativeProcessLifetime &lifetime,
                                                 const volatile sig_atomic_t *stop) {
    if (!lifetime.permitted()) return marketSampleFailure(PublicHttpsStatus::Unreaped);
    std::atomic<bool> cancel{marketSampleStopped(stop)};
    if (cancel.load()) return marketSampleFailure(PublicHttpsStatus::Cancelled);

    // A new std::async thread does not inherit the command's original native scope.
    auto job = std::async(std::launch::async, [&] {
        const NativeProcessScope scope(lifetime.epoch());
        if (!lifetime.permitted()) return marketSampleFailure(PublicHttpsStatus::Unreaped);
        return fetch(request, cancel, limits);
    });
    while (job.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready) {
        if (marketSampleStopped(stop) || !lifetime.permitted()) cancel.store(true);
    }
    auto reply = job.get();
    if (!lifetime.permitted()) return marketSampleFailure(PublicHttpsStatus::Unreaped);
    return reply;
}

}  // namespace om::market_detail
