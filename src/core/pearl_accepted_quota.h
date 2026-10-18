// Default-off, one-attempt native accepted-stop contract. No transport actions.
#pragma once
#include "run.h"
#include "pearl_evidence_io.h"
#include "pearl_generic_native_evidence.h"
#include "pearl_production_admission.h"

namespace om {
constexpr uint64_t kPearlAcceptedQuotaWork = 1048576;
inline bool pearlParseAcceptedQuota(const char *value, unsigned *out) noexcept {
    if (!out) return false;
    *out = 0;
    if (!value) return true;
    if (value[0] >= '1' && value[0] <= '9' && value[1] == '\0') {
        *out = static_cast<unsigned>(value[0] - '0'); return true;
    }
    if (!std::strcmp(value, "10")) { *out = 10; return true; }
    return false;
}
inline bool pearlAcceptedQuotaSelector(const char *selector) noexcept {
    if (!selector) return false;
    if (!std::strcmp(selector, "sm120-wm8-wn2-tm2-tn4-fixed-k2048-r128-v1")) return true;
#if defined(CANARY_PEARL_ENABLE_TMA_WM8_EXPERIMENT) && CANARY_PEARL_ENABLE_TMA_WM8_EXPERIMENT
    // Exact default-off TMA build only; no runtime/other architecture admission.
    if (!std::strcmp(selector, "sm120-tma-wm8-fixed-k2048-r128-v1")) return true;
#endif
#if defined(CANARY_PEARL_ENABLE_TMA_WM8_EXPERIMENT) && CANARY_PEARL_ENABLE_TMA_WM8_EXPERIMENT && \
    defined(CANARY_PEARL_ENABLE_TMA_TRAVERSAL_EXPERIMENT) && CANARY_PEARL_ENABLE_TMA_TRAVERSAL_EXPERIMENT
    // Traversal quota is compiled only after its native proof and paired-energy gates.
    if (!std::strcmp(selector, "sm120-tma-wm8-region16x32-k2048-r128-v1")) return true;
#endif
#if defined(CANARY_PEARL_ENABLE_TMA_WM8_EXPERIMENT) && CANARY_PEARL_ENABLE_TMA_WM8_EXPERIMENT == 1 && \
    defined(CANARY_PEARL_ENABLE_TMA_TRAVERSAL_EXPERIMENT) && CANARY_PEARL_ENABLE_TMA_TRAVERSAL_EXPERIMENT == 1 && \
    defined(CANARY_PEARL_ENABLE_TMA_STAGE4_EXPERIMENT) && CANARY_PEARL_ENABLE_TMA_STAGE4_EXPERIMENT == 1
    // Separate default-off ST4 selector; retain the exact native quota contract.
    if (!std::strcmp(selector, "sm120-tma-wm8-region16x32-st4-k2048-r128-v1")) return true;
#endif
    return false;
}
inline bool pearlAcceptedQuotaAdmission(const RunOptions &opt, const char *algorithm,
        const char *quotaValue, const char *localValue, const char *runId,
        const char *selector, bool conflictingOverride, const char *nativeMode=nullptr) noexcept {
    if (!pearlProductionRequest(opt,algorithm,getenv("CANARY_PEARL_PRODUCTION_MODE"),localValue,runId,selector,conflictingOverride,nativeMode)) return false;
    unsigned parsed = 0;
    if (!pearlPrivateEnvironment(opt.pearlPrivateExecution)) return false;
    const bool privateExecution=pearlPrivateEnabled(opt.pearlPrivateExecution);
    if (privateExecution && ((!opt.pearlAcceptedQuota && !opt.pearlSustainedSeconds) ||
            opt.pearlSustainedPublic || (opt.pearlAcceptedQuota && opt.pearlAcceptedQuota!=2))) return false;
    if (!pearlParseAcceptedQuota(quotaValue, &parsed) || parsed != opt.pearlAcceptedQuota) return false;
    if (!pearlGenericNativeEvidenceRequest(nativeMode,selector,
            parsed || opt.pearlSustainedSeconds || opt.pearlProduction)) return false;
    if (!parsed) return true;
    const bool generic = pearlGenericNativeEvidenceMode(nativeMode) || pearlPrivateGeneric(opt.pearlPrivateExecution);
    return algorithm && !std::strcmp(algorithm, "pearl-pow") &&
        localValue && !std::strcmp(localValue, "1") && pearlEvidenceRunIdValid(runId) &&
        (generic ? !selector : privateExecution ? selector && !std::strcmp(selector,"sm120-wm8-wn2-tm2-tn4-runtime-v1") : pearlAcceptedQuotaSelector(selector)) &&
        !conflictingOverride && opt.batch == (generic ? kPearlGenericSingleAttemptBudget : kPearlAcceptedQuotaWork) &&
        !opt.bench && !opt.formalBenchmark.enabled &&
        !opt.memOc && opt.memOffsetMhz == 0 &&
        opt.poolHost == "127.0.0.1" && opt.poolPort > 0 && opt.poolPort <= 65535 &&
        opt.pearlPolicy.network == PearlNetwork::Mainnet &&
        opt.pearlPolicy.targetSemantics == PearlTargetSemantics::BaseBeforePenalty &&
        !opt.pearlTranscript.empty() && opt.pearlTranscript.size() <= 4096 &&
        opt.pearlTranscript.find('\0') == std::string::npos;
}
inline bool pearlAcceptedQuotaBatch(uint64_t units, uint64_t work, uint64_t attempts) noexcept {
    return units == kPearlAcceptedQuotaWork && work == kPearlAcceptedQuotaWork && attempts == 1;
}
class PearlAcceptedQuotaTracker final {
 public:
    explicit PearlAcceptedQuotaTracker(unsigned quota) noexcept : quota_(quota) {}
    // Called once per fully accounted batch; never after an asynchronous reply.
    bool observe(OutcomeSnapshotStatus status, const PoolOutcomeSnapshot &next,
                 std::size_t outputs) noexcept {
        if (failed_ || reached_ || quota_ == 0 || quota_ > 10 || outputs > 1 ||
            status != OutcomeSnapshotStatus::Valid || !next.conserved() || next.pending ||
            next.accepted < accepted_ || next.accepted > quota_ || next.accepted - accepted_ > outputs) {
            failed_ = true; return false;
        }
        accepted_ = next.accepted;
        reached_ = accepted_ == quota_;
        return true;
    }
    bool reached() const noexcept { return !failed_ && reached_; }
 private:
    unsigned quota_;
    uint64_t accepted_ = 0;
    bool failed_ = false, reached_ = false;
};
} // namespace om
