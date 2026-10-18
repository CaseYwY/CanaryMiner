// Closed admission; does not grant proof ownership or replace native validation.
#pragma once
#include "run.h"
#include <cstdlib>
#include <cstring>

namespace om {
enum class PearlIntegratedPipelineMode { Disabled, NextAttemptV1, Invalid };
inline PearlIntegratedPipelineMode pearlParseIntegratedPipeline(const char *value) noexcept {
    return !value ? PearlIntegratedPipelineMode::Disabled :
        !std::strcmp(value, "next-attempt-v1") ? PearlIntegratedPipelineMode::NextAttemptV1 :
        PearlIntegratedPipelineMode::Invalid;
}
struct PearlIntegratedPipelineContext {
    bool compiled = false, canonicalPearl = false, ownedLoopback = false;
    bool bounded = false, exactShape = false, exactNativeProfile = false;
    bool oneAttemptBatch = false, compactReadback = false, mainnetBaseTarget = false;
    bool conflicting = false;
};
inline bool pearlIntegratedPipelineAdmission(PearlIntegratedPipelineMode mode,
        const PearlIntegratedPipelineContext &c) noexcept {
    if (mode == PearlIntegratedPipelineMode::Disabled) return true;
    return mode == PearlIntegratedPipelineMode::NextAttemptV1 && c.compiled &&
        c.canonicalPearl && c.ownedLoopback && c.bounded && c.exactShape &&
        c.exactNativeProfile && c.oneAttemptBatch && c.compactReadback &&
        c.mainnetBaseTarget && !c.conflicting;
}
inline PearlIntegratedPipelineContext pearlIntegratedPipelineContext(
        const RunOptions &opt, const char *algorithmName, bool compiled,
        bool ownedGuard) noexcept {
    PearlIntegratedPipelineContext c;
    c.compiled = compiled;
    c.canonicalPearl = algorithmName && !std::strcmp(algorithmName, "pearl-pow");
    c.ownedLoopback = ownedGuard && opt.poolHost == "127.0.0.1" &&
        opt.poolPort > 0 && opt.poolPort <= 65535;
    c.bounded = opt.pearlAcceptedQuota || opt.pearlSustainedSeconds || opt.pearlProduction;
    const auto shape = pearlParseShapeExperiment(getenv("CANARY_PEARL_SHAPE_EXPERIMENT"),
        getenv("CANARY_PEARL_SHAPE"), getenv("CANARY_PEARL_TILE"));
    c.exactShape = shape == PearlShapeExperiment::Exact16384x131072K8192V1 &&
        opt.pearlShapeExperiment == shape;
    const char *native = getenv("CANARY_PEARL_NATIVE_EVIDENCE");
    c.exactNativeProfile = native && !std::strcmp(native, "sm120-generic-v1");
    c.oneAttemptBatch = opt.batch == 256;
    const char *compact = getenv("CANARY_PEARL_COMPACT_READBACK");
    c.compactReadback = compact && !std::strcmp(compact, "1");
    c.mainnetBaseTarget = opt.pearlPolicy.network == PearlNetwork::Mainnet &&
        opt.pearlPolicy.targetSemantics == PearlTargetSemantics::BaseBeforePenalty;
    c.conflicting = opt.bench || opt.formalBenchmark.enabled ||
        opt.pearlPrivateExecution != PearlPrivateExecution::Disabled ||
        opt.pearlPublicExecution != PearlPublicExecution::Disabled;
    for (const char *key : {"CANARY_PEARL_EXPERIMENT", "CANARY_PEARL_ATTEMPT_OVERLAP",
            "CANARY_PEARL_A_ROOT_FUSION", "CANARY_PEARL_STAGE_TIMING", "CANARY_PEARL_PROFILE_INTERVAL",
            "CANARY_PEARL_HARD_TARGET", "CANARY_PEARL_BENCH_ALL_HIT",
            "CANARY_PEARL_PRIVATE_EXECUTION", "CANARY_PEARL_PRIVATE_EXECUTION_MODE",
            "CANARY_PEARL_PUBLIC_EXECUTION"})
        if (getenv(key)) c.conflicting = true;
    return c;
}
class PearlIntegratedPipelineBinding final {
 public:
    explicit PearlIntegratedPipelineBinding(const char *value) noexcept
        : captured_(pearlParseIntegratedPipeline(value)),
          failed_(captured_ == PearlIntegratedPipelineMode::Invalid) {}
    bool revalidate(const char *value, const PearlIntegratedPipelineContext &context) noexcept {
        if (failed_ || pearlParseIntegratedPipeline(value) != captured_ ||
            !pearlIntegratedPipelineAdmission(captured_, context)) failed_ = true;
        return !failed_;
    }
    bool failed() const noexcept { return failed_; }
 private:
    const PearlIntegratedPipelineMode captured_;
    bool failed_;
};
}  // namespace om
