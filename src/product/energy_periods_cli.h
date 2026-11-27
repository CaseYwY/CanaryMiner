#pragma once

#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace om {

inline constexpr std::size_t maxEnergyReportRates = 24;

struct EnergyPeriodsOptions {
    bool help = false;
    int stateDirArg = 0, policyArg = 0, cyclesArg = 0, throughArg = 0;
    int tariffArg = 0, octopusRatesArg = 0;
    unsigned cycles = 1;
    int64_t throughUtcMs = 0;
    std::vector<int> flatRateArgs;
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
        else if (flag == "--tariff" && !options.tariffArg) options.tariffArg = i + 1;
        else if (flag == "--octopus-rates" && !options.octopusRatesArg) options.octopusRatesArg = i + 1;
        else if (flag == "--flat-rate" && options.flatRateArgs.size() < maxEnergyReportRates)
            options.flatRateArgs.push_back(i + 1);
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
    return options.stateDirArg && options.policyArg &&
        (options.tariffArg ? options.octopusRatesArg && options.flatRateArgs.empty() : !options.octopusRatesArg);
}

inline constexpr const char *energyPeriodsHelp =
    "canary-miner energy-periods --state-dir DIR --reset-policy FILE [--cycles N] [--through-utc-ms MS]\n"
    "    [--flat-rate FILE ... | --tariff FILE --octopus-rates FILE]\n"
    "Read the existing local GPU-board energy history by monthly, two-month or annual reset periods.\n"
    "Use a reviewed reset policy and optionally saved flat declarations or a reviewed Octopus capture.\n"
    "Octopus pricing requires its matching tariff declaration and the bundled London clock data.\n"
    "An energy-only estimate requires both a valid rate and a covered observation.\n"
    "N includes the current period and defaults to one. The maximum is twelve.\n"
    "The cutoff is UTC Unix milliseconds and defaults to the current time. A development energy-runtime build is required.";

int commandEnergyPeriods(const EnergyPeriodsOptions &, int argc, const char *const *argv, const std::atomic<bool> &cancel);

} // namespace om
