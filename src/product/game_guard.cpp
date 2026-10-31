#include "game_guard.h"
#include <algorithm>
namespace om {
namespace {
using S=GameGuardState;
bool key(const std::string &s){return !s.empty()&&s.size()<=128&&std::all_of(s.begin(),s.end(),[](unsigned char c){return c>=33&&c<=126;});}
bool fixture(const std::string &s){return key(s)&&s.size()>8&&s.rfind("fixture.",0)==0;}
bool instant(int64_t t){return t>=0&&t<=253402300799999LL;}
bool sameDetection(const GameDetection &a,const GameDetection &b){return a.state==b.state&&a.gameKey==b.gameKey&&a.episodeKey==b.episodeKey&&a.generation==b.generation&&
    a.revision==b.revision&&a.observed.utcMs==b.observed.utcMs&&a.observed.monotonicMs==b.observed.monotonicMs;}
}
GameSessionGuard::GameSessionGuard(GameGuardPolicy p,GameProfitBinding b,const SessionIntent &i,SessionToken t):policy_(p),binding_(std::move(b)),intent_(i),token_(t){
    if((p.mode!=GameIntent::Off&&p.mode!=GameIntent::Pause&&p.mode!=GameIntent::Coexist)||p.exitDebounceMs>30000||!p.maximumDetectionGapMs||p.maximumDetectionGapMs>30000||
        !p.maximumProfitAgeMs||p.maximumProfitAgeMs>60000||(p.mode!=GameIntent::Off&&!key(binding_.gameKey))){terminal_=true;view_.state=S::Faulted;return;}
    qualified_=p.coexistence==ForecastQualification::SyntheticFixture&&fixture(binding_.gameKey)&&fixture(binding_.profileKey)&&
        binding_.power.basis==PowerBasis::GpuBoard&&binding_.power.origin==PowerOrigin::SyntheticFixture&&fixture(binding_.power.scope)&&fixture(binding_.power.adapter)&&
        validCreditSourceContract(binding_.production)&&binding_.production.scope==CreditScope::Worker;
    view_.state=p.mode==GameIntent::Off?S::Off:S::PausedUnknown;
}
void GameSessionGuard::requestStop(){terminal_=true;forceArmed_=forceActive_=false;view_.forceResumeActive=false;view_.state=S::Stopped;}
bool GameSessionGuard::requestForceResume(){
    if(terminal_)return false;
    if(forceActive_||forceArmed_){cancelForceResume();return true;}
    if(!gameForceResumeEligible(view_.state))return false;
    forceArmed_=true;
    return true;
}
void GameSessionGuard::cancelForceResume(){forceArmed_=forceActive_=false;view_.forceResumeActive=false;}
bool GameSessionGuard::time(GameGuardTime now){
    if(!instant(now.utcMs)||(clockStarted_&&(now.utcMs<last_.utcMs||now.monotonicMs<last_.monotonicMs))){terminal_=true;cancelForceResume();view_.state=S::ClockFault;return false;}
    if(!clockStarted_){clockStarted_=true;anchor_=now;}
    const auto utc=static_cast<uint64_t>(now.utcMs-anchor_.utcMs),mono=now.monotonicMs-anchor_.monotonicMs;
    if((utc>mono?utc-mono:mono-utc)>2000){terminal_=true;view_.state=S::ClockFault;return false;}
    return true;
}
void GameSessionGuard::clearEpisode(){view_.episodeKey.clear();view_.profitLocked=view_.profitCheckDue=false;view_.coexistBeginMonoMs.reset();view_.lastProfit.reset();
    absentSince_.reset();lastProfitRevision_=lastProfitEndMonoMs_=0;coexistBeginUtcMs_=0;binding_.episodeKey.clear();}
bool GameSessionGuard::acceptProfit(const std::shared_ptr<const GameProfitWindow> &profit,GameGuardTime now){
    if(!profit||!view_.coexistBeginMonoMs)return false;
    const auto &p=profit->input();
    if(p.generation!=token_.epoch||!sameGameProfitBinding(p.binding,binding_)||p.beginMonoMs<*view_.coexistBeginMonoMs||p.beginUtcMs<coexistBeginUtcMs_||
        p.endMonoMs>now.monotonicMs||p.endUtcMs>now.utcMs||p.revision<lastProfitRevision_||p.endMonoMs<lastProfitEndMonoMs_)return false;
    if(static_cast<uint64_t>(p.beginUtcMs-coexistBeginUtcMs_)!=p.beginMonoMs-*view_.coexistBeginMonoMs)return false;
    // A same-revision alternative is not a replacement proof, even if its amounts happen to agree.
    if(p.revision==lastProfitRevision_&&profit!=view_.lastProfit)return false;
    if(now.monotonicMs-p.endMonoMs>policy_.maximumProfitAgeMs||static_cast<uint64_t>(now.utcMs-p.endUtcMs)>policy_.maximumProfitAgeMs)return false;
    lastProfitRevision_=p.revision;lastProfitEndMonoMs_=p.endMonoMs;view_.lastProfit=profit;
    if(!profit->positive())view_.profitLocked=true;
    return true;
}
const GameGuardSnapshot &GameSessionGuard::update(GameGuardTime now,const GameDetection &d,const GameSafety &s,std::shared_ptr<const GameProfitWindow> profit){
    if(!intent_.permits(token_)){requestStop();return view_;}
    if(terminal_)return view_;
    if(!time(now))return view_;
    last_=now;
    if(s.fault){terminal_=true;cancelForceResume();view_.state=S::Faulted;return view_;}
    if(policy_.mode==GameIntent::Off){cancelForceResume();view_.state=S::Off;return view_;}
    const bool safe=s.telemetryValid&&s.thermalSafe;
    // The pause state is computed first, then the one-shot operator override is applied. A stale,
    // cooling, faulted, unknown or changed pause clears the override instead of reviving it.
    auto set=[&](S state)->const GameGuardSnapshot &{
        if(!safe){cancelForceResume();view_.state=S::Cooling;return view_;}
        if(forceArmed_){
            forceArmed_=false;
            if(gameForceResumeEligible(state))forceActive_=true;
        }
        if(forceActive_&&!gameForceResumeEligible(state))forceActive_=false;
        view_.forceResumeActive=forceActive_;
        view_.state=forceActive_&&state!=S::ForceResumeEligible?S::ForceResumeEligible:state;
        return view_;
    };
    if((d.state!=GamePresence::Unknown&&d.state!=GamePresence::Absent&&d.state!=GamePresence::Present)||d.gameKey!=binding_.gameKey||d.generation!=token_.epoch||!d.revision||!instant(d.observed.utcMs)||d.observed.utcMs>now.utcMs||d.observed.monotonicMs>now.monotonicMs||
        static_cast<uint64_t>(now.utcMs-d.observed.utcMs)>policy_.maximumDetectionGapMs||now.monotonicMs-d.observed.monotonicMs>policy_.maximumDetectionGapMs||
        (d.state==GamePresence::Present?!key(d.episodeKey):!d.episodeKey.empty())||
        (detection_&&(d.revision<detection_->revision||(d.revision==detection_->revision&&!sameDetection(d,*detection_))||
            d.observed.utcMs<detection_->observed.utcMs||d.observed.monotonicMs<detection_->observed.monotonicMs))){absentSince_.reset();return set(S::PausedUnknown);}
    const auto gap=detection_?d.observed.monotonicMs-detection_->observed.monotonicMs:policy_.maximumDetectionGapMs;
    detection_=d;
    if(d.state==GamePresence::Unknown){absentSince_.reset();return set(S::PausedUnknown);}
    if(d.state==GamePresence::Absent){
        if(view_.episodeKey.empty())return set(S::NormalEligible);
        if(!absentSince_||gap>policy_.maximumDetectionGapMs)absentSince_=d.observed.monotonicMs;
        if(d.observed.monotonicMs-*absentSince_>=policy_.exitDebounceMs){clearEpisode();return set(S::NormalEligible);}
        return set(S::PausedUnknown);
    }
    if(d.state!=GamePresence::Present){absentSince_.reset();return set(S::PausedUnknown);}
    absentSince_.reset();
    if(view_.episodeKey.empty()){view_.episodeKey=d.episodeKey;binding_.episodeKey=d.episodeKey;}
    else if(view_.episodeKey!=d.episodeKey)return set(S::PausedUnknown); // identity churn cannot erase the old episode's loss latch
    if(policy_.mode==GameIntent::Pause)return set(S::PausedPolicy);
    if(!qualified_||!fixture(d.episodeKey))return set(S::NeedsQualification);
    if(!view_.coexistBeginMonoMs){if(!safe)return set(S::Cooling);view_.coexistBeginMonoMs=now.monotonicMs;coexistBeginUtcMs_=now.utcMs;}
    view_.profitCheckDue=policy_.tenMinuteProfitRule&&now.monotonicMs-*view_.coexistBeginMonoMs>=gameProfitWindowMs;
    if(view_.profitLocked)return set(S::ProfitLocked);
    if(view_.profitCheckDue){
        const auto evidence=profit?std::move(profit):view_.lastProfit;
        if(!acceptProfit(evidence,now))return set(S::ProfitUnavailable);
        if(view_.profitLocked)return set(S::ProfitLocked);
    }
    return set(S::CoexistEligible);
}
} // namespace om
