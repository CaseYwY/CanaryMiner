// Explicit review and private archive of a tariff declaration and imported meter intervals.
#pragma once

#include "local_export_name.h"

#include <charconv>
#include <cstddef>
#include <csignal>
#include <cstdint>
#include <string>
#include <utility>

namespace om {

inline constexpr int64_t meterBillMaxUtcMs = 253402300799999LL;
inline constexpr std::size_t maxInvoiceBytes = 16 * 1024;

struct MeterBillOptions {
    int tariffArg = 0, intervalsArg = 0, reviewCutoffArg = 0;
    int archiveArg = 0, extendArg = 0, saveArg = 0, ratesArg = 0, invoiceArg = 0;
    std::string archiveName;
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
        if (operands < 2 || operands > 12 || operands % 2) return false;
        for (int i = begin; i < argc; i += 2) {
            const std::string name = argv[i];
            const std::string value = argv[i+1];
            if (value.empty() || value.front() == '-') return false;
            if (name == "--tariff" && !options.tariffArg) options.tariffArg = i+1;
            else if (name == "--intervals" && !options.intervalsArg) options.intervalsArg = i+1;
            else if (name == "--octopus-rates" && !options.ratesArg) options.ratesArg = i+1;
            else if (name == "--invoice" && !options.invoiceArg) options.invoiceArg = i+1;
            else if (name == "--open-review" && !options.archiveArg) options.archiveArg = i+1;
            else if (name == "--extend-review" && !options.extendArg) options.extendArg = i+1;
            else if (name == "--save-review" && !options.saveArg && localExportName(value, ".cmr")) {
                options.saveArg = i+1;
                options.archiveName = value;
            } else if (name == "--as-of-utc-ms" && !options.reviewCutoffArg) {
                const auto *end = value.data() + value.size();
                const auto parsed = std::from_chars(value.data(), end, options.reviewCutoffUtcMs);
                if (value.front() < '1' || value.front() > '9' || parsed.ec != std::errc{} ||
                    parsed.ptr != end || options.reviewCutoffUtcMs > meterBillMaxUtcMs) return false;
                options.reviewCutoffArg = i+1;
            } else return false;
        }
        if (options.archiveArg) {
            if (operands != (options.invoiceArg ? 4 : 2)) return false;
        } else if (options.extendArg) {
            if (options.tariffArg || options.ratesArg || !options.intervalsArg || !options.saveArg) return false;
        } else if (!options.tariffArg || !options.intervalsArg) return false;
    }
    *out = std::move(options);
    return true;
}

int commandMeterBill(const MeterBillOptions &options, int argc, const char *const *argv, const volatile sig_atomic_t *stop);

}  // namespace om
