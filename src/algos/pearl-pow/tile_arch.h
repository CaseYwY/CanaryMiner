#pragma once

#include <cstddef>

namespace om::pearl {

/**
 * Runtime compute-capability requirement for one CUDA tile configuration.
 *
 * Both policies require a named factory. This prevents a future bare integer
 * from looking like an exact architecture while silently retaining the old
 * minimum-only semantics.
 */
enum class TileArchMatch {
    AtLeast,
    Exact,
};

struct TileArchEligibility {
    int capability;
    TileArchMatch match;

    TileArchEligibility(int) = delete;
    constexpr TileArchEligibility(int required, TileArchMatch policy)
        : capability(required), match(policy) {}
};

constexpr TileArchEligibility minimumTileArchitecture(int capability) {
    return TileArchEligibility(capability, TileArchMatch::AtLeast);
}

constexpr TileArchEligibility exactTileArchitecture(int capability) {
    return TileArchEligibility(capability, TileArchMatch::Exact);
}

constexpr bool tileArchitectureEligible(TileArchEligibility requirement,
                                        int deviceCapability) {
    // Device discovery is part of the correctness boundary.  An unknown or
    // malformed capability must not make an architecture-specific kernel
    // eligible by accident.
    if (deviceCapability <= 0 || requirement.capability <= 0) return false;
    switch (requirement.match) {
        case TileArchMatch::AtLeast:
            return deviceCapability >= requirement.capability;
        case TileArchMatch::Exact:
            return deviceCapability == requirement.capability;
    }
    return false;
}

/**
 * Visit only configurations whose architecture policy admits this device.
 *
 * Keeping this tiny boundary outside the CUDA translation unit lets a host
 * regression prove that an ineligible configuration cannot reach the same
 * callback the production tuner uses to build its launch candidates.
 */
template <typename Config, std::size_t Count, typename Visitor>
constexpr std::size_t forEachArchitectureEligible(
    const Config (&configs)[Count], int deviceCapability, Visitor &&visitor) {
    std::size_t visited = 0;
    for (std::size_t i = 0; i < Count; ++i) {
        if (!tileArchitectureEligible(configs[i].architecture,
                                      deviceCapability))
            continue;
        visitor(i, configs[i]);
        ++visited;
    }
    return visited;
}

}  // namespace om::pearl
