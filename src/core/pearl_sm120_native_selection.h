// Catalog selection validation. Catalog metadata depends on the complete Job ABI;
// algo.h includes only pearl_private_execution_types.h, never this header.
#pragma once
#include "pearl_native_selection.h"
#include "pearl_sm120_native_mode.h"
#include "../algos/pearl-pow/cuda_preparation_plan.h"
namespace om {
inline bool pearlSm120NativeSelectionValid(const PearlNativeSelection &view) noexcept {
    if (view.sm!=120 || !pearlNativeSelectionValid(view) ||
        !pearl::findCudaPreparationShape(view.m,view.n) ||
        view.workUnitsPerAttempt<kPearlSm120SingleAttemptBudget) return false;
    const auto &tma=pearl::kCudaTmaDeliveryPlan;
    if (view.preparationPlanId==tma.preparationPlanId) {
        pearl::PreparedJobRequest request;
        return view.m==16384 && view.n==131072 && view.k==8192 && view.rank==128 &&
            view.kernelId==tma.kernelId && view.tileId==tma.tileId &&
            pearl::buildCudaPreparedJobRequest(tma,view.m,view.n,120,
                pearl::PreparedJobMode::Normal,&request);
    }
    for (const auto &plan : pearl::kCudaPreparationPlans) {
        if (plan.preparationPlanId!=view.preparationPlanId) continue;
        pearl::PreparedJobRequest request;
        return view.kernelId==plan.kernelId && view.tileId==plan.tileId &&
            pearl::buildCudaPreparedJobRequest(plan,view.m,view.n,120,
                pearl::PreparedJobMode::Normal,&request);
    }
    return false;
}

} // namespace om
