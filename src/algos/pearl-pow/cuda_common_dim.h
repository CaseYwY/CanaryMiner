#pragma once
#include <cstdint>
#ifndef CANARY_PEARL_EXPERIMENT_COMMON_DIM
#define CANARY_PEARL_EXPERIMENT_COMMON_DIM 2048
#endif
namespace om::pearl {
static_assert(CANARY_PEARL_EXPERIMENT_COMMON_DIM == 2048 ||
              CANARY_PEARL_EXPERIMENT_COMMON_DIM == 8192,
              "only K2048 baseline and K8192 research builds are admitted");
inline constexpr uint32_t kCudaCommonDim = CANARY_PEARL_EXPERIMENT_COMMON_DIM;
}
