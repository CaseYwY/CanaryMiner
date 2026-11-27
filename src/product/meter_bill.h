// Explicit review and private archive of a tariff declaration and imported meter intervals.
#pragma once

#include "local_export_name.h"
#include "public_https.h"

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
    int archiveArg = 0, extendArg = 0, saveArg = 0, ratesArg = 0, invoiceArg = 0, prepaidArg = 0;
    int nextCycleArg = 0, previousCycleArg = 0, rollForwardArg = 0, watchArg = 0, quietWatchArg = 0;
    int readingsPlanArg = 0;
    bool scheduledReadingsPlan = false;
    unsigned watchSeconds = 0;
    int prepareCycleArg = 0, saveTariffArg = 0, saveRegisterArg = 0;
    int allowPublicRatesArg = 0, dnsServerArg = 0;
    int importPagesArg = 0, confirmImportArg = 0, saveReadingsArg = 0;
    std::string outputName, dnsServer;
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
        if (operands < 2 || operands > 14) return false;
        for (int i = begin; i < argc;) {
            const std::string name = argv[i];
            if (name == "--allow-public-rates" && !options.allowPublicRatesArg) {
                options.allowPublicRatesArg = i++;
                continue;
            }
            if (name == "--confirm-electricity-import" && !options.confirmImportArg) {
                options.confirmImportArg = i++;
                continue;
            }
            if (name == "--watch-quiet" && !options.quietWatchArg) {
                options.quietWatchArg = i++;
                continue;
            }
            if (i + 1 >= argc) return false;
            const std::string value = argv[i+1];
            if (value.empty() || value.front() == '-') return false;
            if (name == "--tariff" && !options.tariffArg) options.tariffArg = i+1;
            else if (name == "--intervals" && !options.intervalsArg) options.intervalsArg = i+1;
            else if (name == "--octopus-rates" && !options.ratesArg) options.ratesArg = i+1;
            else if (name == "--dns-server" && !options.dnsServerArg && validPublicDnsServer(value)) {
                options.dnsServerArg = i+1;
                options.dnsServer = value;
            } else if (name == "--invoice" && !options.invoiceArg) options.invoiceArg = i+1;
            else if (name == "--prepaid" && !options.prepaidArg) options.prepaidArg = i+1;
            else if (name == "--open-review" && !options.archiveArg) options.archiveArg = i+1;
            else if (name == "--prepare-next-cycle" && !options.prepareCycleArg) options.prepareCycleArg = i+1;
            else if (name == "--next-cycle-review" && !options.nextCycleArg) options.nextCycleArg = i+1;
            else if ((name == "--next-readings-plan" || name == "--scheduled-readings-plan") && !options.readingsPlanArg) {
                options.readingsPlanArg = i+1;
                options.scheduledReadingsPlan = name == "--scheduled-readings-plan";
            }
            else if (name == "--roll-forward" && !options.rollForwardArg) options.rollForwardArg = i+1;
            else if (name == "--watch-every-seconds" && !options.watchArg) {
                const auto *end = value.data() + value.size();
                const auto parsed = std::from_chars(value.data(), end, options.watchSeconds);
                if (value.front() < '1' || value.front() > '9' || parsed.ec != std::errc{} ||
                    parsed.ptr != end || options.watchSeconds > 86400) return false;
                options.watchArg = i+1;
            } else if (name == "--import-octopus-pages" && !options.importPagesArg) options.importPagesArg = i+1;
            else if (name == "--previous-cycle" && !options.previousCycleArg) options.previousCycleArg = i+1;
            else if (name == "--extend-review" && !options.extendArg) options.extendArg = i+1;
            else if (options.outputName.empty() &&
                     ((name == "--save-review" && localExportName(value, ".cmr")) ||
                      ((name == "--save-tariff" || name == "--save-register" || name == "--save-readings") &&
                       localExportName(value, ".json")))) {
                if (name == "--save-tariff") options.saveTariffArg = i+1;
                else if (name == "--save-register") options.saveRegisterArg = i+1;
                else if (name == "--save-readings") options.saveReadingsArg = i+1;
                else options.saveArg = i+1;
                options.outputName = value;
            } else if (name == "--as-of-utc-ms" && !options.reviewCutoffArg) {
                const auto *end = value.data() + value.size();
                const auto parsed = std::from_chars(value.data(), end, options.reviewCutoffUtcMs);
                if (value.front() < '1' || value.front() > '9' || parsed.ec != std::errc{} ||
                    parsed.ptr != end || options.reviewCutoffUtcMs > meterBillMaxUtcMs) return false;
                options.reviewCutoffArg = i+1;
            } else return false;
            i += 2;
        }
        if (options.importPagesArg) {
            if (operands != 7 || !options.tariffArg || !options.confirmImportArg || !options.saveReadingsArg ||
                options.quietWatchArg) return false;
            *out = std::move(options);
            return true;
        }
        if (options.saveReadingsArg || (options.confirmImportArg && !options.rollForwardArg) ||
            (options.watchArg && (!options.rollForwardArg || options.reviewCutoffArg)) ||
            (options.quietWatchArg && !options.watchArg)) return false;
        if (options.allowPublicRatesArg && ((!options.nextCycleArg && !options.rollForwardArg) ||
                                          options.archiveArg || options.prepareCycleArg ||
                                          options.tariffArg || options.ratesArg || options.reviewCutoffArg)) return false;
        if (options.dnsServerArg && !options.allowPublicRatesArg) return false;
        if (options.readingsPlanArg) {
            if (operands != 2) return false;
        } else if (options.rollForwardArg) {
            if (operands != 2 + (options.reviewCutoffArg ? 2 : 0) + (options.allowPublicRatesArg ? 1 : 0) +
                                (options.dnsServerArg ? 2 : 0) + (options.confirmImportArg ? 1 : 0) +
                                (options.watchArg ? 2 : 0) + (options.quietWatchArg ? 1 : 0)) return false;
        } else if (options.prepareCycleArg) {
            if (operands != 4 || (!options.saveTariffArg && !options.saveRegisterArg)) return false;
        } else if (options.saveTariffArg || options.saveRegisterArg) return false;
        else if (options.archiveArg) {
            if ((options.saveArg && !options.invoiceArg) ||
                operands != 2 + (options.invoiceArg ? 2 : 0) + (options.previousCycleArg ? 2 : 0) +
                                (options.saveArg ? 2 : 0)) return false;
        } else if (options.previousCycleArg) return false;
        else if (options.nextCycleArg) {
            if (options.extendArg || options.prepaidArg || !options.intervalsArg || !options.saveArg ||
                (!options.ratesArg && !options.allowPublicRatesArg)) return false;
        } else if (options.extendArg) {
            if (options.tariffArg || options.ratesArg || !options.intervalsArg || !options.saveArg) return false;
        } else if (!options.tariffArg || !options.intervalsArg) return false;
    }
    *out = std::move(options);
    return true;
}

struct OctopusRateCaptureSource;

int commandMeterBill(const MeterBillOptions &options, int argc, const char *const *argv,
                     const volatile sig_atomic_t *stop, const OctopusRateCaptureSource *publicRates = nullptr);

}  // namespace om
