// Synthetic monotonic clocks only: no GPU, sleeping or network.
#include <limits>
#include <string>
#include <vector>

#include "../src/core/host_work_wait.h"
#include "../src/core/run.h"
#include "product_test_util.h"

using namespace om;

int main() {
    for (unsigned duration : {0u, 1u, 9u, 10u, 11u, 999u, 1000u}) {
        uint64_t now = 42, maxSlice = 0, totalSleep = 0;
        unsigned clocks = 0;
        const auto result = waitAfterWork(duration, [] { return false; }, [&] { ++clocks; return now; }, [&](uint64_t slice) {
            totalSleep += slice;
            if (slice > maxSlice) maxSlice = slice;
            now += slice;
        });
        CHECK(result == WorkWaitResult::Ready);
        CHECK(totalSleep == duration && maxSlice <= kWorkWaitPollMs);
        CHECK(duration || clocks == 0);
    }
    uint64_t now = 0;
    bool stopped = false;
    CHECK(waitAfterWork(1000, [&] { return stopped; }, [&] { return now; }, [&](uint64_t slice) { now += slice; stopped = true; }) == WorkWaitResult::Stopped);
    CHECK(now == kWorkWaitPollMs);
    unsigned calls = 0;
    CHECK(waitAfterWork(0, [] { return true; }, [&] { ++calls; return uint64_t{0}; }, [&](uint64_t) { ++calls; }) == WorkWaitResult::Stopped);
    CHECK(calls == 0);
    CHECK(waitAfterWork(1001, [] { return false; }, [&] { ++calls; return uint64_t{0}; }, [&](uint64_t) { ++calls; }) == WorkWaitResult::InvalidClockOrDuration);
    CHECK(calls == 0);
    CHECK(waitAfterWork(1000, [] { return false; }, [] { return std::numeric_limits<uint64_t>::max() - 10; }, [](uint64_t) {}) == WorkWaitResult::InvalidClockOrDuration);
    now = 10;
    CHECK(waitAfterWork(20, [] { return false; }, [&] { return now--; }, [](uint64_t) {}) == WorkWaitResult::InvalidClockOrDuration);
    now = 10;
    CHECK(waitAfterWork(20, [] { return false; }, [&] { return now; }, [&](uint64_t slice) { now += slice + 10; }) == WorkWaitResult::Ready);
    CHECK(now == 30);  // oversleep is elapsed real time, not extra productive work or another renewed deadline
    unsigned value = 123;
    for (const auto &text : {"0", "1", "10", "200", "1000"}) CHECK(parseWorkWaitMs(text, &value));
    for (const auto &text : {"", "-1", "+1", "1.0", "1e2", " 10", "1001", "99999"}) CHECK(!parseWorkWaitMs(text, &value));
    RunOptions options;
    options.poolHost = "synthetic.example";
    options.poolPort = 1000;
    options.wallet = "prl1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq";
    options.pearlPolicy.network = PearlNetwork::Mainnet;
    options.pearlPolicy.targetSemantics = PearlTargetSemantics::BaseBeforePenalty;
    options.workWaitMs = 200;
    CHECK(pearlRuntimeAdmissionProblem(options, "pearl-pow", PearlRuntimeCapability::CanonicalCudaPoolV1) == nullptr);
    options.bench = true;
    CHECK(pearlRuntimeAdmissionProblem(options, "pearl-pow", PearlRuntimeCapability::CanonicalCudaPoolV1) != nullptr);
    options.bench = false;
    options.pearlAcceptedQuota = 2;
    CHECK(pearlRuntimeAdmissionProblem(options, "pearl-pow", PearlRuntimeCapability::CanonicalCudaPoolV1) != nullptr);
    options.pearlAcceptedQuota = 0;
    options.workWaitMs = 1001;
    CHECK(pearlRuntimeAdmissionProblem(options, "pearl-pow", PearlRuntimeCapability::CanonicalCudaPoolV1) != nullptr);
    return finishTests("product_throttle");
}
