#include "economic_study_use.h"
#include <algorithm>
namespace om {
namespace {using S=EconomicUseState;constexpr int64_t lastUtc=253402300799999LL;constexpr uint64_t day=86400000;}
EconomicStudyUse::EconomicStudyUse(std::shared_ptr<const EconomicStudy> study,EconomicUsePolicy policy,const SessionIntent &intent,SessionToken token)
    :study_(std::move(study)),policy_(policy),intent_(intent),token_(token){}
void EconomicStudyUse::halt(S state){terminal_=true;view_.state=state;view_.validUntilMonoMs=0;view_.point.reset();view_.hostWaitMs.reset();}
void EconomicStudyUse::requestStop(){halt(S::Stopped);}
bool EconomicStudyUse::wallBound(int64_t observed,uint64_t age){
    if(observed<0||observed>lastUtc||age>static_cast<uint64_t>(lastUtc-observed)){halt(S::ClockFault);return false;}
    wallLimit_=(std::min)(wallLimit_,observed+static_cast<int64_t>(age));return true;
}
bool EconomicStudyUse::monoBound(uint64_t observed,uint64_t age){
    if(age>UINT64_MAX-observed){halt(S::ClockFault);return false;}
    monoLimit_=(std::min)(monoLimit_,observed+age);return true;
}
bool EconomicStudyUse::bind(){
    if(!policy_.enabled){halt(S::Disabled);return false;}
    if(!policy_.maximumResultAgeMs||policy_.maximumResultAgeMs>day||!study_){halt(S::Invalid);return false;}
    const auto &in=study_->input();const auto &summary=study_->summary();
    if(in.generation!=token_.epoch){halt(S::Attribution);return false;}
    anchor_=last_={in.nowUtcMs,in.nowMonoMs};view_.epoch=token_.epoch;view_.evaluated=anchor_;
    planKey_=workProfilePlanFingerprint(in.currentPlan);
    // All inputs below came from the immutable validated study; never replace their original times with check time.
    if(!wallBound(in.nowUtcMs,policy_.maximumResultAgeMs)||!monoBound(in.nowMonoMs,policy_.maximumResultAgeMs)||
        !wallBound(in.profile->summary().endUtcMs,in.currentPlan.validForMs)||!wallBound(in.reward->input().validUntilUtcMs,0)||
        !wallBound(summary.endUtcMs,0))return false;
    for(const auto *market:{&*in.pearl,&*in.fx}){
        if(!wallBound(static_cast<int64_t>(market->observation.observedUtc*1000),in.policy.maximumMarketAgeMs)||
            !monoBound(market->observation.receivedMonoMs,in.policy.maximumMarketAgeMs))return false;
    }
    if(in.electricity.fx&&!wallBound(in.electricity.fx->observedUtcMs,in.electricity.fx->maxAgeMs))return false;
    for(const auto &candidate:in.candidates)if(candidate.permitted&&!wallBound(candidate.observedUtcMs,candidate.validForMs))return false;
    // UTC-only sources had a known remaining age at evaluation. Bind it there, not at first consumption:
    // a slow wall clock between evaluation and first check must not buy extra monotonic lifetime.
    if(wallLimit_<=anchor_.utcMs){halt(S::Expired);return false;}
    if(!monoBound(anchor_.monotonicMs,static_cast<uint64_t>(wallLimit_-anchor_.utcMs)))return false;
    bound_=true;return true;
}
const EconomicUseSnapshot &EconomicStudyUse::check(const WorkProfilePlan &current,OptimizationTime now,const WorkProfileConditions &conditions){
    if(!intent_.permits(token_)){requestStop();return view_;}
    if(terminal_)return view_;
    try{
    if(!bound_&&!bind())return view_;
    if(now.utcMs<last_.utcMs||now.utcMs>lastUtc||now.monotonicMs<last_.monotonicMs){halt(S::ClockFault);return view_;}
    const auto wall=static_cast<uint64_t>(now.utcMs-anchor_.utcMs),mono=now.monotonicMs-anchor_.monotonicMs;
    if((wall>mono?wall-mono:mono-wall)>2000){halt(S::ClockFault);return view_;}last_=now;
    if(planKey_.empty()||workProfilePlanFingerprint(current)!=planKey_){halt(S::Attribution);return view_;}
    if(conditions.fault||!conditions.safetyTelemetryValid||!conditions.thermalSafe){halt(S::Unsafe);return view_;}
    if(!conditions.gameStateKnown||conditions.gameActive){halt(S::Game);return view_;}
    if(conditions.drift){halt(S::Drift);return view_;}
    if(now.utcMs>=wallLimit_||now.monotonicMs>=monoLimit_){halt(S::Expired);return view_;}
    // Intersect independent original UTC/monotonic ceilings. A decreasing clock skew cannot renew this result.
    const auto remaining=(std::min)(static_cast<uint64_t>(wallLimit_-now.utcMs),monoLimit_-now.monotonicMs);
    const auto calculated=now.monotonicMs+remaining; // remaining <= monoLimit_ - now, so addition is bounded
    if(!ceilingBound_||calculated<ceiling_)ceiling_=calculated;
    ceilingBound_=true;
    if(ceiling_<=now.monotonicMs){halt(S::Expired);return view_;}
    view_.validUntilMonoMs=ceiling_;view_.point=study_->summary().bestPoint;view_.hostWaitMs.reset();
    if(!view_.point){view_.state=S::NoPositiveCandidate;return view_;}
    for(const auto &candidate:study_->summary().candidates)if(candidate.point==*view_.point){
        view_.hostWaitMs=candidate.hostWaitMs;view_.state=S::Eligible;return view_;}
    halt(S::Invalid);return view_;
    }catch(...){halt(S::Invalid);return view_;}
}
} // namespace om
