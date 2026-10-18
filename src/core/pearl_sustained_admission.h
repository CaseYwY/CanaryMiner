#pragma once
#include "pearl_accepted_quota.h"
#include "pearl_sustained_duration.h"

namespace om {
inline bool pearlSustainedAdmission(const RunOptions &opt, const char *algorithm,
    const char *mode, const char *seconds, const char *quota,
    const char *local, const char *runId, const char *selector, bool conflicts,
    const char *nativeMode=nullptr) noexcept {
    if (!pearlProductionRequest(opt,algorithm,getenv("CANARY_PEARL_PRODUCTION_MODE"),local,runId,selector,conflicts,nativeMode)) return false;
    PearlSustainedPolicy policy;
    if (!pearlPrivateEnvironment(opt.pearlPrivateExecution)) return false;
    const bool privateExecution=pearlPrivateEnabled(opt.pearlPrivateExecution);
    if (privateExecution && opt.pearlSustainedPublic) return false;
    if (!pearlParseSustained(mode, seconds, &policy) ||
        policy.seconds != opt.pearlSustainedSeconds || policy.publicRun != opt.pearlSustainedPublic)
        return false;
    if (!pearlGenericNativeEvidenceRequest(nativeMode,selector,
            policy.seconds || opt.pearlAcceptedQuota || opt.pearlProduction)) return false;
    if (policy.public24h && (opt.pearlPublicExecution != PearlPublicExecution::Serial ||
        !pearlPublicNativeMode(nativeMode) || !pearlPublicEnvironment(opt.pearlPublicExecution))) return false;
    if (!policy.seconds) return true;
    const bool generic = pearlGenericNativeEvidenceMode(nativeMode) || pearlPrivateGeneric(opt.pearlPrivateExecution);
    return !quota && opt.pearlAcceptedQuota == 0 && algorithm &&
        !std::strcmp(algorithm,"pearl-pow") && local && !std::strcmp(local,"1") &&
        pearlEvidenceRunIdValid(runId) &&
        (generic ? !selector : privateExecution ? selector && !std::strcmp(selector,"sm120-wm8-wn2-tm2-tn4-runtime-v1") : (pearlAcceptedQuotaSelector(selector) &&
        !std::strcmp(selector,"sm120-tma-wm8-region16x32-st4-k2048-r128-v1"))) &&
        !conflicts && opt.batch == (generic ? kPearlGenericSingleAttemptBudget : kPearlAcceptedQuotaWork) &&
        !opt.bench && !opt.formalBenchmark.enabled &&
        !opt.memOc && opt.memOffsetMhz == 0 &&
        opt.poolHost == "127.0.0.1" && opt.poolPort > 0 && opt.poolPort <= 65535 &&
        opt.pearlPolicy.network == PearlNetwork::Mainnet &&
        opt.pearlPolicy.targetSemantics == PearlTargetSemantics::BaseBeforePenalty &&
        !opt.pearlTranscript.empty() && opt.pearlTranscript.size() <= 4096 &&
        opt.pearlTranscript.find('\0') == std::string::npos;
}
} // namespace om
