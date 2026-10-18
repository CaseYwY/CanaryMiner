// Test-only pure energy-window admission, framing and ordering helpers.
#pragma once
#include <cstdint>
#include <string>

namespace pearl_energy_test {
inline constexpr unsigned BatchLaunches = 32, MaxWindowLaunches = 8192;
inline constexpr unsigned WindowSeconds = 10, WarmSeconds = 30, MaxWarmPairs = 4096;
inline constexpr unsigned Sessions = 2, Positions = 4;
constexpr unsigned planAt(unsigned session, unsigned position) {
    return session < Sessions && position < Positions
        ? unsigned((session == 0) == (position == 1 || position == 2)) : 2U;
}
inline bool canonicalUuid(const std::string &text) {
    if (text.size() != 40 || text.compare(0, 4, "GPU-") != 0) return false;
    for (size_t i = 4; i < text.size(); ++i) {
        if (i == 12 || i == 17 || i == 22 || i == 27) {
            if (text[i] != '-') return false;
        } else if (!((text[i] >= '0' && text[i] <= '9') ||
                     (text[i] >= 'a' && text[i] <= 'f'))) return false;
    }
    return true;
}
inline std::string cudaUuid(const char *bytes) {
    constexpr char hex[] = "0123456789abcdef";
    std::string text = "GPU-";
    for (unsigned i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) text += '-';
        const auto value = static_cast<unsigned char>(bytes[i]);
        text += hex[value >> 4]; text += hex[value & 15];
    }
    return text;
}
struct Endpoint {
    uint64_t millijoules = 0;
    int64_t monotonicBefore = 0, monotonicAfter = 0, utcBefore = 0, utcAfter = 0;
    int status = -1;
};
inline bool validEndpoint(const Endpoint &point) {
    return point.status == 0 && point.monotonicBefore >= 0 &&
           point.monotonicAfter >= point.monotonicBefore;
}
inline bool validWindow(const Endpoint &begin, const Endpoint &end,
                        int64_t workBegin, int64_t workEnd, unsigned completed) {
    return validEndpoint(begin) && validEndpoint(end) &&
        begin.monotonicAfter <= workBegin && workBegin <= workEnd &&
        workEnd <= end.monotonicBefore &&
        workEnd - workBegin >= int64_t(WindowSeconds) * 1000000000 &&
        completed > 0 && completed <= MaxWindowLaunches && completed % BatchLaunches == 0 &&
        end.millijoules > begin.millijoules;
}
static_assert(planAt(0, 0) == 0 && planAt(0, 1) == 1 && planAt(0, 2) == 1 && planAt(0, 3) == 0,
              "ABBA order");
static_assert(planAt(1, 0) == 1 && planAt(1, 1) == 0 && planAt(1, 2) == 0 && planAt(1, 3) == 1,
              "BAAB order");
static_assert(MaxWindowLaunches % BatchLaunches == 0, "bounded complete batches");
} // namespace pearl_energy_test
