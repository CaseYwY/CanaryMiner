#include "product/executable_path.h"
#include "product/market_hour.h"
#include "product/market_sample_options.h"
#include "product/market_ticker_runtime.h"
#include "product/native_process_admission.h"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#if defined(__linux__)
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#endif

namespace {
void advanceEpoch() {
    om::NativeProcessAdmission foreign;
    foreign.markUnresolved();
    foreign.settled();
}

#if defined(__linux__)
thread_local unsigned faultOnThread = 0;
thread_local bool tickerClockRollback = false;
thread_local size_t tickerClockRead = 0;
thread_local volatile sig_atomic_t *stopAfterSync = nullptr;
#endif
}

#if defined(__linux__)
// Interpose only this fixture's async thread creation. The process backend remains real.
extern "C" int pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                              void *(*function)(void *), void *argument) noexcept {
    static const auto create = reinterpret_cast<decltype(&pthread_create)>(dlsym(RTLD_NEXT, "pthread_create"));
    if (!create) return EAGAIN;
    if (faultOnThread && --faultOnThread == 0) {
        advanceEpoch();
        std::fputs("fixture epoch advanced\n", stderr);
    }
    return create(thread, attributes, function, argument);
}

extern "C" int clock_gettime(clockid_t clock, struct timespec *value) noexcept {
    static const auto readClock = reinterpret_cast<decltype(&clock_gettime)>(dlsym(RTLD_NEXT, "clock_gettime"));
    // Start, Gate send/receipt/completion, then Kraken send/receipt/completion. Only Gate completion moves back.
    constexpr time_t utc[] = {1760000060, 1760000060, 1760000062, 1760000061, 1760000062, 1760000062, 1760000062};
    if (tickerClockRollback && clock == CLOCK_REALTIME && tickerClockRead < std::size(utc)) {
        *value = {utc[tickerClockRead++], 0};
        return 0;
    }
    return readClock ? readClock(clock, value) : -1;
}

extern "C" int fsync(int fd) {
    static const auto sync = reinterpret_cast<decltype(&fsync)>(dlsym(RTLD_NEXT, "fsync"));
    const int result = sync ? sync(fd) : -1;
    if (!result && stopAfterSync) *stopAfterSync = 1;
    return result;
}
#endif

int main(int argc, char **argv) {
#if defined(__linux__)
#if !defined(__GLIBC__)
    return 77;
#elif !__GLIBC_PREREQ(2,40)
    return 77;
#endif
#endif
    if (argc < 3) return 2;
    const auto module = std::filesystem::u8path(om::selfExecutablePath());
    if (module.empty()) return 2;
    std::string mode;
    std::ifstream(module.parent_path() / "sample-lifetime-mode") >> mode;
    const auto epoch = om::nativeProcessEpoch();
    if (mode == "stale") advanceEpoch();
#if defined(__linux__)
    if (mode == "first") faultOnThread = 1;
    if (mode == "second") faultOnThread = 2;
#endif
    const om::NativeProcessScope scope(epoch);
    const auto command = std::string_view(argv[1]);
    if (command == "market-hour") {
        om::MarketHourOptions options;
        if (!om::parseMarketHourCli(argc, argv, 2, options) || !options.samplesPublic()) return 2;
        return om::commandPublicMarketHour(options, module, argc, argv, nullptr);
    }
    if (command == "market-tickers") {
        om::MarketTickerOptions options;
        if (!om::parseMarketTickerCli(argc, argv, 2, options)) return 2;
        volatile sig_atomic_t stop = 0;
#if defined(__linux__)
        tickerClockRollback = mode == "clock-rollback";
        if (mode == "stop-after-sync") stopAfterSync = &stop;
#endif
        const auto result = om::commandPublicTickers(options, module, argc, argv, &stop);
#if defined(__linux__)
        stopAfterSync = nullptr;
        if (tickerClockRollback && tickerClockRead < 4) return 4;
#endif
        return result;
    }
    return 2;
}
