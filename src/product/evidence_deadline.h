// One immutable source revision's exclusive deadline can shrink, never grow or revive after expiry.
#pragma once
#include "game_guard.h"
#include <algorithm>
namespace om {
class EvidenceDeadline {
   public:
    // The caller owns session/source identity and checks its clock/lifecycle before calling.
    uint64_t clip(uint64_t revision,GameGuardTime now,GameGuardTime observed,uint64_t maximumAgeMs){
        if(!revision||revision<revision_)return 0;
        if(revision!=revision_){
            if(revision_&&(maximumAgeMs!=age_||observed.utcMs<observed_.utcMs||observed.monotonicMs<observed_.monotonicMs||
                (observed.monotonicMs==observed_.monotonicMs&&observed.utcMs!=observed_.utcMs))){revision_=revision;ceiling_=0;bound_=true;return 0;}
            // Renumbering a cached observation is not new elapsed-time evidence.
            if(!revision_||observed.monotonicMs!=observed_.monotonicMs)bound_=false;
            revision_=revision;observed_=observed;age_=maximumAgeMs;
        }
        else if(observed.utcMs!=observed_.utcMs||observed.monotonicMs!=observed_.monotonicMs||maximumAgeMs!=age_){ceiling_=0;bound_=true;return 0;}
        uint64_t deadline=0;
        if(age_&&observed.utcMs>=0&&now.utcMs>=observed.utcMs&&now.utcMs<=253402300799999LL&&observed.monotonicMs<=now.monotonicMs){
            const auto elapsed=(std::max)(static_cast<uint64_t>(now.utcMs-observed.utcMs),now.monotonicMs-observed.monotonicMs);
            if(elapsed<age_&&now.monotonicMs<=UINT64_MAX-(age_-elapsed))deadline=now.monotonicMs+age_-elapsed;
        }
        if(!bound_||deadline<ceiling_)ceiling_=deadline;
        bound_=true;return ceiling_>now.monotonicMs?ceiling_:0;
    }
   private:
    uint64_t revision_=0,age_=0,ceiling_=0;GameGuardTime observed_;bool bound_=false;
};
} // namespace om
