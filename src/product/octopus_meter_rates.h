#pragma once

#include "tariff_declaration.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace om {

inline constexpr std::size_t maxOctopusRateFileBytes = 2 * 1024 * 1024;

struct OctopusRateImport {
    int64_t observedUtcMs = 0;
    std::size_t intervals = 0;
    std::string paymentMethod;
    bool go = false;
};

// Validate the placeholder before reading or acquiring rate pages.
const char *checkOctopusRateDeclaration(const TariffDeclaration &declaration);

// Bind a locally captured Octopus Agile or Go rate file to a declared tariff for this review only.
const char *importOctopusRates(const std::string &bytes, int64_t cutoff, TariffDeclaration &declaration,
                              OctopusRateImport &summary, const std::function<bool()> &stop);

}  // namespace om
