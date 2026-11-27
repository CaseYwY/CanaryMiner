#pragma once

#include "tariff_declaration.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <csignal>
#include <filesystem>
#include <string>

namespace om {

inline constexpr std::size_t maxOctopusRateFileBytes = 2 * 1024 * 1024;

struct OctopusRateImport {
    int64_t observedUtcMs = 0;
    std::size_t intervals = 0;
    std::string paymentMethod;
    bool go = false;
};

class NativeProcessLifetime;
struct OctopusRateCapture {
    std::string bytes;
    OctopusRateImport summary;
};

// Capture and verify original pages under the caller's lifetime. The caller owns publication.
const char *capturePublicOctopusRates(TariffDeclaration declaration, const std::string &payment,
                                      const std::string &dns, const std::filesystem::path &module,
                                      const NativeProcessLifetime &lifetime, const volatile sig_atomic_t *stop,
                                      OctopusRateCapture &out);

// Validate the placeholder before reading or acquiring rate pages.
const char *checkOctopusRateDeclaration(const TariffDeclaration &declaration);

// Bind a locally captured Octopus Agile or Go rate file to a declared tariff for this review only.
const char *importOctopusRates(const std::string &bytes, int64_t cutoff, TariffDeclaration &declaration,
                              OctopusRateImport &summary, const std::function<bool()> &stop);

}  // namespace om
