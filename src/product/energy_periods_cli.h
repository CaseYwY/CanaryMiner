#pragma once

#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace om {

struct EnergyPeriodsOptions {
    bool help = false;
    int stateDirArg = 0, policyArg = 0, cyclesArg = 0, throughArg = 0;
    unsigned cycles = 1;
    int64_t throughUtcMs = 0;
};

inline bool parseEnergyPeriodsCli(int argc, const char *const *argv, int first, EnergyPeriodsOptions &options) {
    if (argc == first + 1) {
        const std::string_view arg(argv[first]);
        if (arg == "--help" || arg == "-h") {
            options.help = true;
            return true;
        }
    }
    for (int i = first; i < argc; i += 2) {
        if (i + 1 >= argc || !argv[i + 1][0] || argv[i + 1][0] == '-') return false;
        const std::string_view flag(argv[i]);
        if (flag == "--state-dir" && !options.stateDirArg) options.stateDirArg = i + 1;
        else if (flag == "--reset-policy" && !options.policyArg) options.policyArg = i + 1;
        else if ((flag == "--cycles" && !options.cyclesArg) || (flag == "--through-utc-ms" && !options.throughArg)) {
            const auto *start = argv[i + 1];
            const auto *end = start + std::strlen(start);
            int64_t number = 0;
            const auto parsed = std::from_chars(start, end, number);
            if (parsed.ec != std::errc{} || parsed.ptr != end || *start == '0' || number < 1) return false;
            if (flag == "--cycles") {
                if (number > 12) return false;
                options.cycles = static_cast<unsigned>(number);
                options.cyclesArg = i + 1;
            } else {
                if (number > 253402300799999LL) return false;
                options.throughUtcMs = number;
                options.throughArg = i + 1;
            }
        } else return false;
    }
    return options.stateDirArg && options.policyArg;
}

inline constexpr const char *energyPeriodsHelp =
    "canary-miner energy-periods --state-dir DIR --reset-policy FILE [--cycles N] [--through-utc-ms MS]\n"
    "Read the existing local GPU-board energy history by independent monthly reset periods.\n"
    "The reset policy is a JSON object with monthly_day (1 to 31) and an explicit clock.\n"
    "N includes the current period and defaults to one. The maximum is twelve.\n"
    "The cutoff is UTC Unix milliseconds and defaults to the current time. A development energy-runtime build is required.";

int commandEnergyPeriods(const EnergyPeriodsOptions &, int argc, const char *const *argv, const std::atomic<bool> &cancel);

} // namespace om
