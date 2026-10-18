#pragma once
#include "../src/algos/pearl-pow/cuda_preparation_plan.h"
#if defined(CANARY_PEARL_TEST_TMA_STAGE4_BACKEND)
static_assert(CANARY_PEARL_TEST_TMA_STAGE4_BACKEND == 1, "stage4 mode must be exactly 1");
#if !defined(CANARY_PEARL_TEST_TMA_BACKEND)
#error "stage4 requires explicit TMA backend helper mode"
#endif
#if defined(CANARY_PEARL_TEST_TMA_TRAVERSAL_BACKEND)
#error "stage4 and traversal comparisons have distinct baseline roles"
#endif
#endif
#if defined(CANARY_PEARL_TEST_TMA_TRAVERSAL_BACKEND)
static_assert(CANARY_PEARL_TEST_TMA_TRAVERSAL_BACKEND == 1, "traversal mode must be1");
#if !defined(CANARY_PEARL_TEST_TMA_BACKEND)
#error "traversal requires explicit TMA backend helper mode"
#endif
#endif

#if defined(CANARY_PEARL_TEST_TMA_BACKEND)
static_assert(CANARY_PEARL_TEST_TMA_BACKEND == 1, "TMA WM8 backend mode must be exactly 1");
#if defined(CANARY_PEARL_TEST_WM4_REUSE_BACKEND) || defined(CANARY_PEARL_TEST_BK32_BACKEND)
#error "TMA WM8, WM4 reuse and BK32 backend modes are mutually exclusive"
#endif
#endif

#if defined(CANARY_PEARL_TEST_WM4_REUSE_BACKEND)
static_assert(CANARY_PEARL_TEST_WM4_REUSE_BACKEND == 1, "WM4 reuse backend mode must be exactly 1");
#if defined(CANARY_PEARL_TEST_BK32_BACKEND)
#error "WM4 reuse and BK32 backend modes are mutually exclusive"
#endif
#endif

namespace pearl_fixed_pair {
using namespace om::pearl;
enum class Mode { RuntimeWm8VsFixedWm8, FixedWm8VsBk32, FixedWm8VsWm4Reuse, FixedWm8VsTmaWm8, TmaWm8VsTraversal, TmaTraversalVsStage4 };
constexpr const char *selector(Mode mode, bool candidate) noexcept {
    if (mode == Mode::TmaTraversalVsStage4)
        return candidate ? kCudaTmaStage4ExperimentOptIn : kCudaTmaTraversalExperimentOptIn;
    if (mode == Mode::TmaWm8VsTraversal)
        return candidate ? kCudaTmaTraversalExperimentOptIn : kCudaTmaWm8ExperimentOptIn;
    if (mode == Mode::FixedWm8VsTmaWm8)
        return candidate ? kCudaTmaWm8ExperimentOptIn : kCudaWm8FixedExperimentOptIn;
    if (mode == Mode::FixedWm8VsWm4Reuse)
        return candidate ? kCudaWm4ReuseExperimentOptIn : kCudaWm8FixedExperimentOptIn;
    if (mode == Mode::FixedWm8VsBk32)
        return candidate ? kCudaBk32FixedExperimentOptIn : kCudaWm8FixedExperimentOptIn;
    return candidate ? kCudaWm8FixedExperimentOptIn : kCudaWm8ExperimentOptIn;
}
constexpr uint64_t plan(Mode mode, bool candidate) noexcept {
    if (mode == Mode::TmaTraversalVsStage4)
        return candidate ? kCudaTmaStage4ExperimentPlan.preparationPlanId : kCudaTmaTraversalExperimentPlan.preparationPlanId;
    if (mode == Mode::TmaWm8VsTraversal)
        return candidate ? kCudaTmaTraversalExperimentPlan.preparationPlanId : kCudaTmaWm8ExperimentPlan.preparationPlanId;
    if (mode == Mode::FixedWm8VsTmaWm8)
        return candidate ? kCudaTmaWm8ExperimentPlan.preparationPlanId : kCudaWm8FixedExperimentPlan.preparationPlanId;
    if (mode == Mode::FixedWm8VsWm4Reuse)
        return candidate ? kCudaWm4ReuseExperimentPlan.preparationPlanId : kCudaWm8FixedExperimentPlan.preparationPlanId;
    if (mode == Mode::FixedWm8VsBk32)
        return candidate ? kCudaBk32FixedExperimentPlan.preparationPlanId : kCudaWm8FixedExperimentPlan.preparationPlanId;
    return candidate ? kCudaWm8FixedExperimentPlan.preparationPlanId : kCudaWm8ExperimentPlan.preparationPlanId;
}
} // namespace pearl_fixed_pair
