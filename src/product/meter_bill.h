// Explicit, read-only review of a tariff declaration and imported meter intervals.
#pragma once

#include <charconv>
#include <csignal>
#include <cstdint>
#include <string>
#include <utility>

namespace om {

inline constexpr int64_t meterBillMaxUtcMs = 253402300799999LL;

struct MeterBillOptions {
    int tariffArg = 0, intervalsArg = 0, reviewCutoffArg = 0;
    int64_t reviewCutoffUtcMs = 0;
    bool help = false;
};

inline bool parseMeterBillCli(int argc, const char *const *argv, int begin, MeterBillOptions *out) {
    if (!out || !argv || begin < 1 || begin > argc) return false;
    MeterBillOptions options;
    if (argc - begin == 1 && (std::string(argv[begin]) == "--help" || std::string(argv[begin]) == "-h")) {
        options.help = true;
    } else {
        const int operands = argc - begin;
        if (operands != 4 && operands != 6) return false;
        for (int i = begin; i < argc; i += 2) {
            const std::string name = argv[i];
            const std::string value = argv[i+1];
            if (value.empty() || value.front() == '-') return false;
            if (name == "--tariff" && !options.tariffArg) options.tariffArg = i+1;
            else if (name == "--intervals" && !options.intervalsArg) options.intervalsArg = i+1;
            else if (name == "--as-of-utc-ms" && !options.reviewCutoffArg) {
                const auto *end = value.data() + value.size();
                const auto parsed = std::from_chars(value.data(), end, options.reviewCutoffUtcMs);
                if (value.front() < '1' || value.front() > '9' || parsed.ec != std::errc{} ||
                    parsed.ptr != end || options.reviewCutoffUtcMs > meterBillMaxUtcMs) return false;
                options.reviewCutoffArg = i+1;
            } else return false;
        }
        if (!options.tariffArg || !options.intervalsArg) return false;
    }
    *out = std::move(options);
    return true;
}

int commandMeterBill(const MeterBillOptions &options, int argc, const char *const *argv, const volatile sig_atomic_t *stop);

}  // namespace om
