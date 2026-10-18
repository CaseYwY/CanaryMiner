#pragma once
#include "../src/algos/pearl-pow/cuda_preparation_plan.h"
#include "pearl_energy_window.h"
#include "pearl_fixed_backend_pair.h"
#include <string_view>

// These are entry/schema controls, not kernel-body or production flags.
#if defined(CANARY_PEARL_TEST_TMA_TRAVERSAL_SEARCH_ENERGY)
static_assert(CANARY_PEARL_TEST_TMA_TRAVERSAL_SEARCH_ENERGY == 1, "traversal energy mode must be1");
#if !defined(CANARY_PEARL_TEST_TMA_SEARCH_ENERGY)
#error "traversal requires explicit TMA energy helper mode"
#endif
#endif
#if defined(CANARY_PEARL_TEST_TMA_TRAVERSAL_SEARCH_ENERGY) != defined(CANARY_PEARL_TEST_TMA_TRAVERSAL_BACKEND)
#error "traversal requires matching backend and energy entry modes"
#endif
#if defined(CANARY_PEARL_TEST_TMA_SEARCH_ENERGY)
static_assert(CANARY_PEARL_TEST_TMA_SEARCH_ENERGY == 1, "TMA WM8 search energy mode must be exactly 1");
#if defined(CANARY_PEARL_TEST_BK32_SEARCH_ENERGY) || defined(CANARY_PEARL_TEST_WM4_REUSE_SEARCH_ENERGY)
#error "TMA WM8, BK32 and WM4 reuse energy modes are mutually exclusive"
#endif
#endif
#if defined(CANARY_PEARL_TEST_TMA_SEARCH_ENERGY) != defined(CANARY_PEARL_TEST_TMA_BACKEND)
#error "TMA WM8 search energy requires explicit paired energy and backend modes"
#endif

#if defined(CANARY_PEARL_TEST_BK32_SEARCH_ENERGY) && CANARY_PEARL_TEST_BK32_SEARCH_ENERGY != 1
#error "BK32 search energy mode must be absent or exactly 1"
#endif
#if defined(CANARY_PEARL_TEST_BK32_BACKEND) && CANARY_PEARL_TEST_BK32_BACKEND != 1
#error "BK32 search energy backend mode must be absent or exactly 1"
#endif
#if defined(CANARY_PEARL_TEST_BK32_SEARCH_ENERGY) != defined(CANARY_PEARL_TEST_BK32_BACKEND)
#error "BK32 search energy requires the explicit paired energy and backend modes"
#endif

#if defined(CANARY_PEARL_TEST_WM4_REUSE_SEARCH_ENERGY)
static_assert(CANARY_PEARL_TEST_WM4_REUSE_SEARCH_ENERGY == 1, "WM4 reuse search energy mode must be exactly 1");
#if defined(CANARY_PEARL_TEST_BK32_SEARCH_ENERGY)
#error "WM4 reuse and BK32 energy modes are mutually exclusive"
#endif
#endif
#if defined(CANARY_PEARL_TEST_WM4_REUSE_SEARCH_ENERGY) != defined(CANARY_PEARL_TEST_WM4_REUSE_BACKEND)
#error "WM4 reuse search energy requires explicit paired energy and backend modes"
#endif

namespace pearl_search_energy_pair {
using namespace om::pearl;
using Mode = pearl_fixed_pair::Mode;
constexpr const char *schema(Mode mode) noexcept {
    if (mode == Mode::TmaWm8VsTraversal) return "pearl-tma-traversal-whole-search-energy-v1";
    return mode == Mode::FixedWm8VsTmaWm8 ? "pearl-tma-wm8-whole-search-energy-v1" :
        (mode == Mode::FixedWm8VsWm4Reuse ? "pearl-wm4-reuse-whole-search-energy-v1" :
        (mode == Mode::FixedWm8VsBk32 ? "pearl-bk32-whole-search-energy-v1" : "pearl-wm8-whole-search-energy-v1"));
}
constexpr const char *selector(Mode mode, bool candidate) noexcept { return pearl_fixed_pair::selector(mode, candidate); }
constexpr uint64_t plan(Mode mode, bool candidate) noexcept { return pearl_fixed_pair::plan(mode, candidate); }
constexpr const char *schema(bool bk32) noexcept {
    return bk32 ? "pearl-bk32-whole-search-energy-v1" : "pearl-wm8-whole-search-energy-v1";
}
constexpr const char *selector(bool bk32, bool candidate) noexcept {
    return bk32 ? (candidate ? kCudaBk32FixedExperimentOptIn : kCudaWm8FixedExperimentOptIn)
                : (candidate ? kCudaWm8FixedExperimentOptIn : kCudaWm8ExperimentOptIn);
}
constexpr uint64_t plan(bool bk32, bool candidate) noexcept {
    return bk32 ? (candidate ? kCudaBk32FixedExperimentPlan.preparationPlanId : kCudaWm8FixedExperimentPlan.preparationPlanId)
                : (candidate ? kCudaWm8FixedExperimentPlan.preparationPlanId : kCudaWm8ExperimentPlan.preparationPlanId);
}
inline bool validCli(int argc, const char *const *argv) noexcept {
    return argc == 5 && argv && argv[0] && argv[1] && argv[2] && argv[3] && argv[4] &&
        std::string_view(argv[1]) == "--run" && std::string_view(argv[2]) == "--gpu-uuid" &&
        pearl_energy_test::canonicalUuid(argv[3]) && std::string_view(argv[4]) == "--energy";
}
inline bool admitted(bool bk32, int argc, const char *const *argv, const char *current,
                     bool baselineCompiled, bool candidateCompiled, bool conflict) noexcept {
    return validCli(argc, argv) && baselineCompiled && candidateCompiled && !conflict &&
        current && std::string_view(current) == selector(bk32, true);
}
inline unsigned batchAttempts(Mode mode, int argc, const char *const *argv) noexcept {
    if (mode != Mode::FixedWm8VsWm4Reuse && mode != Mode::FixedWm8VsTmaWm8 && mode != Mode::TmaWm8VsTraversal) return validCli(argc, argv) ? 4U : 0U;
    if ((argc != 7 && argc != 9) || !validCli(5, argv) || !argv[5] || !argv[6] ||
        std::string_view(argv[5]) != "--attempts") return 0;
    if (argc == 9 && (!argv[7] || !argv[8] || std::string_view(argv[7]) != "--required-power-limit-mw")) return 0;
    if (argc == 9) {
        std::string_view value(argv[8]);
        if (value.empty() || value.size() > 7 || (value.size() > 1 && value.front() == '0')) return 0;
        uint64_t parsed = 0;
        for (char digit : value) {
            if (digit < '0' || digit > '9') return 0;
            parsed = parsed * 10 + unsigned(digit - '0');
        }
        if (parsed == 0 || parsed > 1000000) return 0;
    }
    return std::string_view(argv[6]) == "1" ? 1U : (std::string_view(argv[6]) == "4" ? 4U : 0U);
}
inline unsigned requiredPowerLimitMilliwatts(Mode mode, int argc, const char *const *argv) noexcept {
    if (!batchAttempts(mode, argc, argv)) return 0;
    if (argc == 5 || argc == 7) return 175000;
    uint64_t parsed = 0;
    for (char digit : std::string_view(argv[8])) parsed = parsed * 10 + unsigned(digit - '0');
    return static_cast<unsigned>(parsed);
}
inline bool admitted(Mode mode, int argc, const char *const *argv, const char *current,
                     bool baselineCompiled, bool candidateCompiled, bool conflict) noexcept {
    return batchAttempts(mode, argc, argv) != 0 && baselineCompiled && candidateCompiled && !conflict &&
        current && std::string_view(current) == pearl_fixed_pair::selector(mode, true);
}
} // namespace pearl_search_energy_pair
