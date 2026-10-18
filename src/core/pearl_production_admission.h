// Explicit owned private production admission; legacy qualification is unchanged.
#pragma once
#include "run.h"
#include "pearl_evidence_io.h"
#include "pearl_generic_native_evidence.h"
namespace om {
inline bool pearlProductionValue(const char *value) noexcept {
    return value && !std::strcmp(value,"private-v1");
}
inline bool pearlProductionRequest(const RunOptions &opt,const char *algorithm,
        const char *value,const char *local,const char *runId,const char *selector,
        bool conflicts,const char *nativeMode) noexcept {
    if (!opt.pearlProduction) return !value;
    return pearlProductionValue(value) && algorithm && !std::strcmp(algorithm,"pearl-pow") &&
        !opt.pearlAcceptedQuota && !opt.pearlSustainedSeconds && !opt.pearlSustainedPublic &&
        opt.pearlPrivateExecution==PearlPrivateExecution::Disabled &&
        opt.pearlPublicExecution==PearlPublicExecution::Disabled &&
        !getenv("CANARY_PEARL_ACCEPTED_QUOTA") && !getenv("CANARY_PEARL_SUSTAINED_MODE") &&
        !getenv("CANARY_PEARL_SUSTAINED_SECONDS") && local && !std::strcmp(local,"1") &&
        pearlEvidenceRunIdValid(runId) && !selector && !conflicts &&
        pearlSm120NativeEvidenceMode(nativeMode) &&
        opt.pearlShapeExperiment==PearlShapeExperiment::Exact16384x131072K8192V1 &&
        opt.batch==kPearlGenericSingleAttemptBudget && !opt.bench && !opt.formalBenchmark.enabled &&
        !opt.memOc && opt.memOffsetMhz==0 &&
        opt.poolHost=="127.0.0.1" && opt.poolPort>0 && opt.poolPort<=65535 &&
        opt.pearlPolicy.network==PearlNetwork::Mainnet &&
        opt.pearlPolicy.targetSemantics==PearlTargetSemantics::BaseBeforePenalty &&
        !opt.pearlTranscript.empty() && opt.pearlTranscript.size()<=4096 &&
        opt.pearlTranscript.find('\0')==std::string::npos && opt.pearlWorkRange.enabled;
}
} // namespace om
