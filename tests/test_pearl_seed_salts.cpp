// Host-only regression for Pearl V3's two commitment seed salts.
//
// The salts are reached independently by multiple GPU workers.  This test
// releases many host threads onto both accessors at once, then checks the
// consensus-domain values, immutable pointer lifetime, and repeatability.
// It opens no socket and uses no CUDA runtime, GPU, wallet, or miner process.

#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <thread>
#include <type_traits>
#include <vector>

#include "../src/algos/pearl-pow/job.h"

using namespace om::pearl;

namespace {

int gPass = 0;
int gFail = 0;

void check(const char *name, bool ok) {
    if (ok) {
        ++gPass;
        printf("  ok   %s\n", name);
    } else {
        ++gFail;
        printf("  FAIL %s\n", name);
    }
}

constexpr uint8_t kExpectedA[32] = {
    0x82, 0x49, 0x40, 0x6c, 0xa0, 0xed, 0x15, 0x16,
    0x96, 0x16, 0xf6, 0x92, 0xfc, 0xf0, 0x76, 0xf8,
    0x92, 0xdb, 0xdb, 0x2a, 0x70, 0x23, 0xb8, 0x52,
    0xf0, 0xd4, 0x77, 0x19, 0xc3, 0x90, 0x01, 0x7b,
};

constexpr uint8_t kExpectedB[32] = {
    0x11, 0x30, 0x06, 0x32, 0xec, 0x63, 0x01, 0xca,
    0x2b, 0xe2, 0xaf, 0x71, 0x8b, 0x3f, 0x4d, 0x4f,
    0x1a, 0xe9, 0xc6, 0x39, 0x88, 0xe8, 0xcc, 0x04,
    0x48, 0x44, 0x30, 0x1d, 0x71, 0xb8, 0x9a, 0xa9,
};

}  // namespace

int main() {
    static_assert(std::is_same<decltype(seedSaltA()), const uint8_t *>::value,
                  "seedSaltA must not expose mutable storage");
    static_assert(std::is_same<decltype(seedSaltB()), const uint8_t *>::value,
                  "seedSaltB must not expose mutable storage");

    constexpr size_t kThreadCount = 64;
    constexpr size_t kCallsPerThread = 4096;
    std::atomic<size_t> ready{0};
    std::atomic<bool> start{false};
    std::atomic<bool> valuesOk{true};
    std::vector<const uint8_t *> aPointers(kThreadCount, nullptr);
    std::vector<const uint8_t *> bPointers(kThreadCount, nullptr);
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (size_t i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&, i] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();

            // Reverse the first-access order in half the threads so A and B
            // can both be under first-use contention in this process.
            const uint8_t *a = nullptr;
            const uint8_t *b = nullptr;
            if (i & 1) {
                b = seedSaltB();
                a = seedSaltA();
            } else {
                a = seedSaltA();
                b = seedSaltB();
            }
            aPointers[i] = a;
            bPointers[i] = b;

            for (size_t call = 0; call < kCallsPerThread; ++call) {
                if (seedSaltA() != a || seedSaltB() != b ||
                    memcmp(a, kExpectedA, sizeof(kExpectedA)) != 0 ||
                    memcmp(b, kExpectedB, sizeof(kExpectedB)) != 0) {
                    valuesOk.store(false, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    while (ready.load(std::memory_order_acquire) != kThreadCount)
        std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (std::thread &thread : threads) thread.join();

    bool sameAPointer = aPointers[0] != nullptr;
    bool sameBPointer = bPointers[0] != nullptr;
    for (size_t i = 1; i < kThreadCount; ++i) {
        sameAPointer = sameAPointer && aPointers[i] == aPointers[0];
        sameBPointer = sameBPointer && bPointers[i] == bPointers[0];
    }

    check("A is the exact V3 domain digest",
          memcmp(seedSaltA(), kExpectedA, sizeof(kExpectedA)) == 0);
    check("B is the exact V3 domain digest",
          memcmp(seedSaltB(), kExpectedB, sizeof(kExpectedB)) == 0);
    check("concurrent repeated reads retain both values", valuesOk.load());
    check("all threads receive one stable A object", sameAPointer);
    check("all threads receive one stable B object", sameBPointer);
    check("A and B remain distinct", memcmp(seedSaltA(), seedSaltB(), 32) != 0);

    printf("\n%d passed, %d failed\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
