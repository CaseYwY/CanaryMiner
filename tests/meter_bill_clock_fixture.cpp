// Test-only realtime clock for a successor published after the initial planning sample.
#include <cstdlib>
#include <ctime>
#include <dlfcn.h>

extern "C" int clock_gettime(clockid_t clock, struct timespec *value) noexcept {
    static const auto realClock = reinterpret_cast<decltype(&clock_gettime)>(dlsym(RTLD_NEXT, "clock_gettime"));
    const char *boundary = std::getenv("CANARY_TEST_SUCCESSOR_CUTOFF_MS");
    if (clock != CLOCK_REALTIME || !boundary) return realClock ? realClock(clock, value) : -1;
    const auto cutoff = std::atoll(boundary);
    if (cutoff <= 0) return -1;
    static unsigned realtimeSamples = 0;
    const bool elapsed = std::getenv("CANARY_TEST_SUCCESSOR_CLOCK_ADVANCE") && realtimeSamples >= 2;
    ++realtimeSamples;
    const auto ms = elapsed ? cutoff : cutoff - 1;
    value->tv_sec = static_cast<time_t>(ms / 1000);
    value->tv_nsec = static_cast<long>(ms % 1000) * 1000000;
    return 0;
}
