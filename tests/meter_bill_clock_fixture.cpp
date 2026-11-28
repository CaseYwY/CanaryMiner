// Test-only clock and publication interleavings for recurring meter reviews.
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
bool watchClockLifted = false;
bool watchClockLiftUsed = false;
}

extern "C" int clock_gettime(clockid_t clock, struct timespec *value) noexcept {
    static const auto realClock = reinterpret_cast<decltype(&clock_gettime)>(dlsym(RTLD_NEXT, "clock_gettime"));
    const char *boundary = std::getenv("CANARY_TEST_SUCCESSOR_CUTOFF_MS");
    if (clock != CLOCK_REALTIME || !boundary) {
        const auto result = realClock ? realClock(clock, value) : -1;
        if (result == 0 && clock == CLOCK_REALTIME && watchClockLifted) {
            value->tv_sec += 2;
            value->tv_nsec += 500000000L;
            if (value->tv_nsec >= 1000000000L) {
                ++value->tv_sec;
                value->tv_nsec -= 1000000000L;
            }
        }
        return result;
    }
    const auto cutoff = std::atoll(boundary);
    if (cutoff <= 0) return -1;
    static unsigned realtimeSamples = 0;
    const bool elapsed = std::getenv("CANARY_TEST_SUCCESSOR_CLOCK_ADVANCE") && realtimeSamples >= 2;
    const bool regressed = std::getenv("CANARY_TEST_COMPARISON_CLOCK_REGRESSION") && realtimeSamples >= 2;
    ++realtimeSamples;
    const auto ms = regressed ? cutoff - 6000 : elapsed ? cutoff : cutoff - 1;
    value->tv_sec = static_cast<time_t>(ms / 1000);
    value->tv_nsec = static_cast<long>(ms % 1000) * 1000000;
    return 0;
}

extern "C" int nanosleep(const struct timespec *duration, struct timespec *remaining) {
    static const auto realSleep = reinterpret_cast<decltype(&nanosleep)>(dlsym(RTLD_NEXT, "nanosleep"));
    watchClockLifted = false;
    return realSleep ? realSleep(duration, remaining) : -1;
}

extern "C" int open(const char *path, int flags, ...) {
    static const auto realOpen = reinterpret_cast<decltype(&open)>(dlsym(RTLD_NEXT, "open"));
    if (!realOpen) {
        errno = ENOSYS;
        return -1;
    }
    if ((flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list arguments;
        va_start(arguments, flags);
        const auto mode = va_arg(arguments, mode_t);
        va_end(arguments);
        return realOpen(path, flags, mode);
    }
    const auto fd = realOpen(path, flags);
    const auto *lift = std::getenv("CANARY_TEST_LIFT_WATCH_CLOCK_ON_INVOICE");
    if (fd >= 0 && lift && !watchClockLiftUsed && std::strcmp(path, lift) == 0) {
        watchClockLifted = true;
        watchClockLiftUsed = true;
    }
    const auto *readings = std::getenv("CANARY_TEST_REMOVE_COMPARISON_ON_READINGS");
    if (fd >= 0 && readings && std::strcmp(path, readings) == 0) {
        const auto *comparison = std::getenv("CANARY_TEST_COMPARISON_TO_REMOVE");
        if (!comparison) std::abort();
        const auto held = std::string(comparison) + ".held";
        if (std::rename(comparison, held.c_str()) != 0) std::abort();
    }
    const auto *invoice = std::getenv("CANARY_TEST_PUBLISH_ON_INVOICE_INPUT");
    if (fd >= 0 && invoice && std::strcmp(path, invoice) == 0) {
        const auto *target = std::getenv("CANARY_TEST_PUBLISH_SUCCESSOR_ON_INVOICE");
        if (!target) std::abort();
        const auto held = std::string(target) + ".held";
        if (::link(held.c_str(), target) != 0 || ::unlink(held.c_str()) != 0) std::abort();
    }
    if (fd < 0 && errno == ENOENT) {
        const auto *target = std::getenv("CANARY_TEST_PUBLISH_COMPARISON");
        if (target && std::strcmp(path, target) == 0) {
            const auto temporary = std::string(target) + ".tmp";
            if (std::rename(temporary.c_str(), target) != 0) std::abort();
            errno = ENOENT;
        }
    }
    return fd;
}

extern "C" int lstat(const char *path, struct stat *info) noexcept {
    static const auto realStat = reinterpret_cast<decltype(&lstat)>(dlsym(RTLD_NEXT, "lstat"));
    if (!realStat) {
        errno = ENOSYS;
        return -1;
    }
    const auto *target = std::getenv("CANARY_TEST_PUBLISH_SUCCESSOR");
    if (target) {
        const auto temporary = std::string(target) + ".tmp";
        if (path == temporary && (::link(temporary.c_str(), target) != 0 || ::unlink(temporary.c_str()) != 0))
            std::abort();
    }
    return realStat(path, info);
}
