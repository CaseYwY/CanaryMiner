#pragma once

#include "local_export_name.h"
#include "public_https.h"

#include <csignal>
#include <filesystem>

namespace om {

inline constexpr char octopusRatesHelp[] =
    "canary-miner meter-rates --allow-public-rates --tariff FILE --payment-method METHOD --save-rates NAME.json [--dns-server IP|cloudflare-doh]\n"
    "Save public Octopus Agile or Go rates for the elapsed window in a compatible tariff declaration.\n"
    "METHOD is DIRECT_DEBIT or NON_DIRECT_DEBIT. Use the resulting JSON with meter-bill --octopus-rates.\n"
    "Requires public HTTPS and the bundled tariff time database.";

struct OctopusRateOptions {
    bool help = false;
    std::string payment, output, dns;
};

inline bool parseOctopusRateCli(int argc, const char *const *argv, int i, OctopusRateOptions &out) {
    if (i >= argc) return false;
    const std::string_view consent(argv[i]);
    if (argc == i + 1 && (consent == "-h" || consent == "--help")) {
        out.help = true;
        return true;
    }
    if ((argc != i + 7 && argc != i + 9) || consent != "--allow-public-rates" ||
        std::string_view(argv[i + 1]) != "--tariff" || !argv[i + 2][0] || argv[i + 2][0] == '-' ||
        std::string_view(argv[i + 3]) != "--payment-method" || std::string_view(argv[i + 5]) != "--save-rates" ||
        !localExportName(argv[i + 6], ".json")) return false;
    const std::string_view payment(argv[i + 4]);
    if (payment != "DIRECT_DEBIT" && payment != "NON_DIRECT_DEBIT") return false;
    if (argc == i + 9 &&
        (std::string_view(argv[i + 7]) != "--dns-server" || !validPublicDnsServer(argv[i + 8]))) return false;
    out = {false, std::string(payment), argv[i + 6], argc == i + 9 ? argv[i + 8] : ""};
    return true;
}

bool nativeOctopusRateInvocation(const OctopusRateOptions &, int argc, const char *const *argv);

int commandPublicOctopusRates(const OctopusRateOptions &, const std::filesystem::path &module, int argc,
                              const char *const *argv, const volatile sig_atomic_t *stop);

}  // namespace om
