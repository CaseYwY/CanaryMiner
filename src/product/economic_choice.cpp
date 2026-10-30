#include "economic_choice.h"
namespace om {
namespace {
using S=EconomicChoiceState;using K=EconomicCurrentKind;
bool bounded(const ExactDecimal &n){return !n.negative()&&n.digits()<=38&&n.scale()<=18;}
bool positive(const ForecastRatio &r){return !r.numerator.negative()&&!r.numerator.zero();}
bool atLeast(const ForecastRatio &r,const ExactDecimal &minimum,bool &yes){ExactDecimal threshold;
    if(!minimum.multiply(r.denominator,&threshold))return false;
    yes=r.numerator.compare(threshold)>=0;return true;
}
EconomicChoice chosen(S state,const EconomicCandidate &candidate){EconomicChoice out;out.state=state;out.point=candidate.point;out.hostWaitMs=candidate.hostWaitMs;return out;}
}
bool validEconomicChoicePolicy(const EconomicChoicePolicy &p){ExactDecimal one;ExactDecimal::parse("1",&one);
    return bounded(p.resumeMinimumUsdPerSecond)&&bounded(p.switchMinimumGainUsdPerSecond)&&bounded(p.switchMinimumFraction)&&p.switchMinimumFraction.compare(one)<=0;
}
EconomicChoice economicChoice(const EconomicStudy *study,const EconomicChoicePolicy &policy,EconomicCurrentChoice current){
    const auto result=[](S state){EconomicChoice out;out.state=state;return out;};
    if(!policy.enabled)return result(S::Disabled);
    if(!validEconomicChoicePolicy(policy)||(current.kind!=K::Unknown&&current.kind!=K::Paused&&current.kind!=K::AppliedWait)||
        current.hostWaitMs>1000||(current.kind!=K::AppliedWait&&current.hostWaitMs))return result(S::Invalid);
    if(!study||current.kind==K::Unknown)return result(S::Unavailable);
    try{
        const auto &summary=study->summary();const EconomicCandidate *best=nullptr,*active=nullptr;
        for(const auto &candidate:summary.candidates){if(summary.bestPoint&&candidate.point==*summary.bestPoint)best=&candidate;
            if(current.kind==K::AppliedWait&&candidate.hostWaitMs==current.hostWaitMs)active=&candidate;}
        if(current.kind==K::AppliedWait&&!active)return result(S::PauseIneligible);
        if(!best||(active&&!positive(active->netUsdPerSecond)))return result(S::PauseNonpositive);
        bool enough=false;
        if(!active){if(!atLeast(best->netUsdPerSecond,policy.resumeMinimumUsdPerSecond,enough))return result(S::Capacity);
            return enough?chosen(S::Resume,*best):result(S::PauseMargin);}
        if(best==active)return chosen(S::Hold,*active);
        const auto &a=active->netUsdPerSecond,&b=best->netUsdPerSecond;
        ExactDecimal left,right,delta,denominator,absolute,relative;
        if(!b.numerator.multiply(a.denominator,&left)||!a.numerator.multiply(b.denominator,&right)||!left.subtract(right,&delta)||
            !a.denominator.multiply(b.denominator,&denominator)||!policy.switchMinimumGainUsdPerSecond.multiply(denominator,&absolute)||
            !policy.switchMinimumFraction.multiply(right,&relative))return result(S::Capacity);
        // Exact ties retain the currently installed point, regardless of the study's declared-order tie breaker.
        if(delta.negative()||delta.zero()||delta.compare(absolute)<0||delta.compare(relative)<0)return chosen(S::Hold,*active);
        return chosen(S::Switch,*best);
    }catch(...){return result(S::Capacity);}
}
} // namespace om
