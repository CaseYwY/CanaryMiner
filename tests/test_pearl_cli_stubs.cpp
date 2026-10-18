// Link actual CLI/registry code against inert factories, never a GPU backend.
#include "../src/core/run.h"
#include "../src/algos/pearl-pow/cuda_preparation_plan.h"
#include "pearl_cli_stubs/cuda_stub.h"
namespace om {
bool pearlCudaIntegratedPipelineCompiled() noexcept {
#if defined(PEARL_CLI_INTEGRATED_PIPELINE_CAPABLE_STUB) && PEARL_CLI_INTEGRATED_PIPELINE_CAPABLE_STUB == 1
    return true;
#else
    return false;
#endif
}
bool pearlCudaShapeExperimentCompiled() noexcept {
#if defined(PEARL_CLI_SHAPE_CAPABLE_STUB) && PEARL_CLI_SHAPE_CAPABLE_STUB == 1
    return true;
#else
    return false;
#endif
}
bool pearlCudaFixedExperimentCompiled() noexcept { return false; }
bool pearlCudaExperimentCompiled(const char *selection) noexcept {
#if defined(PEARL_CLI_PRIVATE_WM8_CAPABLE_STUB)
    return pearl::parseCudaFixedExperiment(selection) == pearl::CudaFixedExperimentSelection::Wm8Runtime;
#elif defined(PEARL_CLI_TMA_WM8_CAPABLE_STUB)
    return pearl::parseCudaFixedExperiment(selection) == pearl::CudaFixedExperimentSelection::TmaWm8FixedK2048R128;
#elif defined(PEARL_CLI_WM4_REUSE_CAPABLE_STUB)
    return pearl::parseCudaFixedExperiment(selection) == pearl::CudaFixedExperimentSelection::Wm4ReuseFixedK2048R128;
#elif defined(PEARL_CLI_BK32_FIXED_CAPABLE_STUB)
    return pearl::parseCudaFixedExperiment(selection) == pearl::CudaFixedExperimentSelection::Bk32FixedK2048R128;
#elif defined(PEARL_CLI_WM8_FIXED_CAPABLE_STUB)
    return pearl::parseCudaFixedExperiment(selection) == pearl::CudaFixedExperimentSelection::Wm8FixedK2048R128;
#elif defined(PEARL_CLI_A_ROOT_FUSION_CAPABLE_STUB)
    return pearl::parseCudaFixedExperiment(selection) == pearl::CudaFixedExperimentSelection::Wm8Runtime;
#else
    (void)selection;
    return false;
#endif
}
bool pearlCudaARootFusionCompiled() noexcept {
#ifdef PEARL_CLI_A_ROOT_FUSION_CAPABLE_STUB
    return true;
#else
    return false;
#endif
}
bool pearlCudaAttemptOverlapCompiled() noexcept {
#ifdef PEARL_CLI_OVERLAP_CAPABLE_STUB
    return true;
#else
    return false;
#endif
}
Algorithm *makePearlPow() { forbiddenPearlCliSetup(); return nullptr; }
int runMiner(Algorithm *, const RunOptions &, const char *, double,
             const char *, volatile sig_atomic_t *, const char *, unsigned) {
    return forbiddenPearlCliSetup();
}
}

