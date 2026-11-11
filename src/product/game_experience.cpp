#include "game_experience.h"
#include <algorithm>
#include <limits>
namespace om {
namespace {
constexpr size_t maximumFrames=65536,maximumPairs=16;
constexpr uint64_t maximumTestNs=3600000000000ULL; // fixed one-hour offline audit/input ceiling, not a running test budget
bool text(const std::string &s){
    if(s.empty()||s.size()>256)return false;
    for(unsigned char c:s)if(c<0x20||c>0x7e)return false;
    return true;
}
bool identity(const GameExperienceIdentity &v){return text(v.gameBuild)&&text(v.scene)&&text(v.device)&&text(v.driver)&&text(v.minerBuild)&&text(v.kernel)&&text(v.display)&&text(v.settings)&&text(v.environment)&&text(v.adapter);}
bool cancelled(const std::atomic<bool> *cancel){return cancel&&cancel->load();}
bool product(uint64_t a,uint64_t b,uint64_t c,ExactDecimal *out){
    ExactDecimal x,y,z,t;
    return ExactDecimal::parse(std::to_string(a),&x)&&ExactDecimal::parse(std::to_string(b),&y)&&ExactDecimal::parse(std::to_string(c),&z)&&x.multiply(y,&t)&&t.multiply(z,out);
}
bool floor(const GameExperienceRate &rate,uint32_t milliFps){
    ExactDecimal observed,limit;
    return product(rate.frames,1000000000000ULL,1,&observed)&&product(milliFps,rate.denominatorNs,1,&limit)&&observed.compare(limit)>=0;
}
bool relative(const GameExperienceRate &baseline,const GameExperienceRate &candidate,unsigned lossBp){
    ExactDecimal observed,limit;
    return product(candidate.frames,baseline.denominatorNs,10000,&observed)&&
           product(baseline.frames,candidate.denominatorNs,10000-lossBp,&limit)&&observed.compare(limit)>=0;
}
GameExperienceStatus metrics(const GameExperienceWindow &window,const GameExperiencePlan &plan,GameExperienceMetrics *out,const std::atomic<bool> *cancel){
    if(!identity(window.identity)||!sameGameExperienceIdentity(window.identity,plan.identity)||!window.session||!window.sequence)return GameExperienceStatus::Attribution;
    if(window.endNs<=window.beginNs)return GameExperienceStatus::Clock;
    const auto elapsed=window.endNs-window.beginNs;
    if(elapsed<plan.minimumWindowNs||elapsed>plan.maximumTestNs||window.warmupNs<plan.warmupNs||window.warmupNs>window.beginNs)return GameExperienceStatus::Budget;
    if(!window.complete||window.framesNs.size()<100||!window.peakMemoryBytes||!window.peakTemperatureMilliC)return GameExperienceStatus::Coverage;
    if(window.framesNs.size()>maximumFrames)return GameExperienceStatus::Capacity;
    if(!window.safetyValid||!window.thermalSafe||window.fault||!*window.peakMemoryBytes||*window.peakTemperatureMilliC<0||*window.peakTemperatureMilliC>150000)return GameExperienceStatus::Unsafe;
    uint64_t sum=0;
    for(auto frame:window.framesNs){
        if(cancelled(cancel))return GameExperienceStatus::Cancelled;
        if(!frame||frame>elapsed||sum>elapsed-frame)return GameExperienceStatus::Coverage;
        sum+=frame;
    }
    if(sum!=elapsed)return GameExperienceStatus::Coverage;
    auto sorted=window.framesNs;std::sort(sorted.begin(),sorted.end());
    const size_t slowCount=(sorted.size()+99)/100;
    uint64_t slowSum=0;for(size_t i=sorted.size()-slowCount;i<sorted.size();++i)slowSum+=sorted[i]; // subset of checked total
    GameExperienceMetrics result;result.mean={static_cast<uint64_t>(sorted.size()),sum};result.slowestOnePercentMean={static_cast<uint64_t>(slowCount),slowSum};
    // Nearest-rank p99: ceil(0.99*N), 1-based. The explicitly named low metric is inverse mean of slowest ceil(0.01*N), not p99 FPS.
    const size_t rank=(sorted.size()*99+99)/100;
    result.p99FrameNs=sorted[rank-1];result.maximumFrameNs=sorted.back();result.peakMemoryBytes=*window.peakMemoryBytes;result.peakTemperatureMilliC=*window.peakTemperatureMilliC;
    *out=result;return GameExperienceStatus::Ok;
}
bool within(const GameExperienceMetrics &v,const GameExperienceLimits &limits){
    return floor(v.mean,limits.minimumMeanMilliFps)&&floor(v.slowestOnePercentMean,limits.minimumLowMilliFps)&&
        v.maximumFrameNs<=limits.maximumFrameNs&&v.p99FrameNs<=limits.maximumP99FrameNs&&v.peakMemoryBytes<=limits.maximumMemoryBytes&&v.peakTemperatureMilliC<=limits.maximumTemperatureMilliC;
}
} // namespace
bool sameGameExperienceIdentity(const GameExperienceIdentity &a,const GameExperienceIdentity &b){
    return a.gameBuild==b.gameBuild&&a.scene==b.scene&&a.device==b.device&&a.driver==b.driver&&a.minerBuild==b.minerBuild&&a.kernel==b.kernel&&a.display==b.display&&a.settings==b.settings&&a.environment==b.environment&&a.adapter==b.adapter;
}
GameExperienceStatus compareGameExperience(const GameExperienceInput &input,GameExperienceComparison *out,const std::atomic<bool> *cancel){
    if(cancelled(cancel))return GameExperienceStatus::Cancelled;
    if(!out)return GameExperienceStatus::Invalid;
    const auto &plan=input.plan;const auto &limits=plan.limits;
    if(!plan.enabled)return GameExperienceStatus::Disabled;
    if(plan.qualification!=GameExperienceQualification::SyntheticFixture)return GameExperienceStatus::Unsupported;
    if(!identity(plan.identity)||plan.hostWaitMs>1000||plan.repetitions<2||plan.repetitions>maximumPairs||!plan.warmupNs||!plan.minimumWindowNs||
       !plan.maximumTestNs||plan.maximumTestNs>maximumTestNs||plan.minimumWindowNs>plan.maximumTestNs||plan.warmupNs>plan.maximumTestNs||
       !limits.minimumMeanMilliFps||!limits.minimumLowMilliFps||limits.minimumLowMilliFps>limits.minimumMeanMilliFps||
       limits.maximumMeanLossBp>10000||limits.maximumLowLossBp>10000||!limits.maximumFrameNs||!limits.maximumP99FrameNs||limits.maximumP99FrameNs>limits.maximumFrameNs||
       !limits.maximumMemoryBytes||limits.maximumTemperatureMilliC<=0||limits.maximumTemperatureMilliC>150000)return GameExperienceStatus::Invalid;
    if(input.pairs.size()!=plan.repetitions)return GameExperienceStatus::Coverage;
    GameExperienceComparison result;result.withinDeclaredLimits=true;uint64_t session=0,lastSequence=0,lastEnd=0,firstBegin=0;
    for(const auto &pair:input.pairs){
        if(cancelled(cancel))return GameExperienceStatus::Cancelled;
        if(pair.baseline.mining||!pair.candidate.mining||pair.baseline.hostWaitMs!=0||pair.candidate.hostWaitMs!=plan.hostWaitMs)return GameExperienceStatus::Attribution;
        GameExperienceTrial trial;
        auto status=metrics(pair.baseline,plan,&trial.baseline,cancel);if(status!=GameExperienceStatus::Ok)return status;
        status=metrics(pair.candidate,plan,&trial.candidate,cancel);if(status!=GameExperienceStatus::Ok)return status;
        if(trial.baseline.mean.denominatorNs!=trial.candidate.mean.denominatorNs)return GameExperienceStatus::Coverage;
        for(const auto *window:{&pair.baseline,&pair.candidate}){
            if(!session){session=window->session;firstBegin=window->beginNs-window->warmupNs;}
            if(window->session!=session||window->sequence<=lastSequence||window->beginNs-window->warmupNs<lastEnd)return GameExperienceStatus::Attribution;
            lastSequence=window->sequence;lastEnd=window->endNs;
            if(lastEnd-firstBegin>plan.maximumTestNs)return GameExperienceStatus::Budget;
        }
        // Each trial, not just a favorable aggregate, must pass absolute and paired relative limits.
        trial.withinLimits=within(trial.baseline,limits)&&within(trial.candidate,limits)&&
            relative(trial.baseline.mean,trial.candidate.mean,limits.maximumMeanLossBp)&&
            relative(trial.baseline.slowestOnePercentMean,trial.candidate.slowestOnePercentMean,limits.maximumLowLossBp);
        result.withinDeclaredLimits=result.withinDeclaredLimits&&trial.withinLimits;result.trials.push_back(trial);
    }
    result.totalElapsedNs=lastEnd-firstBegin;
    if(cancelled(cancel))return GameExperienceStatus::Cancelled;
    const bool passed=result.withinDeclaredLimits;*out=std::move(result);
    return passed?GameExperienceStatus::Ok:GameExperienceStatus::OutsideLimits;
}
} // namespace om
