// Explicit, read-only review of a tariff declaration and imported meter intervals.
#pragma once

#include <csignal>
#include <string>
#include <utility>

namespace om {

struct MeterBillOptions {
    int tariffArg = 0, intervalsArg = 0;
    bool help = false;
};

inline bool parseMeterBillCli(int argc, const char *const *argv, int begin, MeterBillOptions *out) {
    if (!out || !argv || begin < 1 || begin > argc) return false;
    MeterBillOptions options;
    if (argc - begin == 1 && (std::string(argv[begin]) == "--help" || std::string(argv[begin]) == "-h")) {
        options.help = true;
    } else {
        if (argc - begin != 4) return false;
        for (int i = begin; i < argc; i += 2) {
            const std::string name = argv[i];
            const std::string value = argv[i+1];
            if (value.empty() || value.front() == '-') return false;
            if (name == "--tariff" && !options.tariffArg) options.tariffArg = i+1;
            else if (name == "--intervals" && !options.intervalsArg) options.intervalsArg = i+1;
            else return false;
        }
    }
    *out = std::move(options);
    return true;
}

int commandMeterBill(const MeterBillOptions &options, int argc, const char *const *argv, const volatile sig_atomic_t *stop);

}  // namespace om
